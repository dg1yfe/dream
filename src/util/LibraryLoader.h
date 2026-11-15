/******************************************************************************\
 *
 * Copyright (c) 2012
 *
 * Author(s):
 *	David Flamand
 *
 * Description:
 *	Dynamic Link Library Loader
 *
 ******************************************************************************
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
\******************************************************************************/

#ifndef _LIBRARYLOADER_H_
#define _LIBRARYLOADER_H_

#ifdef _WIN32
# include <windows.h>
# define LOADLIB(a) (void*)LoadLibraryA(a)
# define GETPROC(a, b) (void*)GetProcAddress((HMODULE)a, b)
# define FREELIB(a) FreeLibrary((HMODULE)a)
#else
# include <dlfcn.h>
# define LOADLIB(a) dlopen(a, RTLD_LOCAL | RTLD_NOW)
# define GETPROC(a, b) dlsym(a, b)
# define FREELIB(a) dlclose(a)
#endif
#ifndef _WIN32
# include <cstdlib>
# include <cstring>
# include <string>
# include <vector>
#endif
#if defined(__linux__)
# include <unistd.h>
# include <climits>
#endif

typedef struct LIBFUNC
{
	const char *pcFunctionName;
	void **ppvFunctionAddress;
	void *pvDummyFunctionAddress;
} LIBFUNC;

class CLibraryLoader
{
public:
	/* Try every candidate (see Candidates()) until one exports all LibFuncs and
	   passes LibCheckCallback. The function pointers are left pointing into the
	   accepted library, or reset to their dummy values if none is found. */
	static void* Load(const char** LibraryNames, const LIBFUNC* LibFuncs, bool (*LibCheckCallback)()=nullptr)
	{
		void* hLib = nullptr;
#ifndef _WIN32
		const std::vector<std::string> names = Candidates(LibraryNames);
		for (size_t l = 0; l < names.size(); l++)
		{
			hLib = LOADLIB(names[l].c_str());
#else
		for (int l = 0; LibraryNames[l]; l++)
		{
			hLib = LOADLIB(LibraryNames[l]);
#endif
			if (hLib != nullptr)
			{
				int f;
				for (f = 0; LibFuncs[f].pcFunctionName; f++)
				{
					void *pvFunc = GETPROC(hLib, LibFuncs[f].pcFunctionName);
					if (!pvFunc)
						break;
					*LibFuncs[f].ppvFunctionAddress = pvFunc;
				}
				if (!LibFuncs[f].pcFunctionName)
				{
					if (LibCheckCallback==nullptr || LibCheckCallback())
						break;
				}
				FREELIB(hLib);
				hLib = nullptr;
			}
		}
		if (hLib == nullptr)
		{
			for (int f = 0; LibFuncs[f].pcFunctionName; f++)
				*LibFuncs[f].ppvFunctionAddress = LibFuncs[f].pvDummyFunctionAddress;
		}
		return hLib;
	}
#ifndef _WIN32
	/* macOS: dlopen() of a bare leaf name only searches DYLD_LIBRARY_PATH, LC_RPATH,
	   the current directory and DYLD_FALLBACK_LIBRARY_PATH (/usr/local/lib,
	   /usr/lib) - never Homebrew's /opt/homebrew/lib or the app bundle. So for
	   every leaf name try, in order:
	     1. each directory in $DREAM_LIB_PATH (colon separated; explicit user override)
	     2. <app>.app/Contents/Frameworks   (@executable_path/../Frameworks)
	     3. @loader_path                    (directory of the dream binary)
	     4. @rpath                          (LC_RPATH entries, e.g. .deps/<arch>/lib in dev builds)
	     5. DREAM_DEPS_LIBDIR               (compile-time define, e.g. <repo>/.deps/arm64/lib,
	                                         so self-built codec libs win over Homebrew's)
	     6. /opt/homebrew/lib               (Homebrew, Apple Silicon)
	     7. /usr/local/lib                  (Homebrew Intel / manual installs)
	     8. the bare name                   (dyld default search)
	   Linux and other ELF systems:
	     1. each directory in $DREAM_LIB_PATH
	     2. <dir of the dream binary>/../lib/dream  (install / tarball layout)
	     3. DREAM_DEPS_LIBDIR               (compile-time define, <repo>/.deps/linux-<arch>/lib)
	     4. the bare name                   (DT_RUNPATH, LD_LIBRARY_PATH, ld.so.cache)
	   A candidate that loads but fails the symbol/check callback is skipped,
	   so e.g. a non-DRM libfaac in an earlier location does not hide a good one. */
	static std::vector<std::string> Candidates(const char** LibraryNames)
	{
		std::vector<std::string> dirs;
		const char* env = getenv("DREAM_LIB_PATH");
		if (env != nullptr)
		{
			std::string s(env);
			size_t start = 0;
			while (start <= s.size())
			{
				size_t end = s.find(':', start);
				if (end == std::string::npos)
					end = s.size();
				if (end > start)
					dirs.push_back(s.substr(start, end - start));
				start = end + 1;
			}
		}
#ifdef __APPLE__
		dirs.push_back("@executable_path/../Frameworks");
		dirs.push_back("@loader_path");
		dirs.push_back("@rpath");
#elif defined(__linux__)
		char exe[PATH_MAX];
		ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
		if (n > 0)
		{
			std::string e(exe, size_t(n));
			size_t slash = e.rfind('/');
			if (slash != std::string::npos)
				dirs.push_back(e.substr(0, slash) + "/../lib/dream");
		}
#endif
#ifdef DREAM_DEPS_LIBDIR
		dirs.push_back(DREAM_DEPS_LIBDIR);
#endif
#ifdef __APPLE__
		dirs.push_back("/opt/homebrew/lib");
		dirs.push_back("/usr/local/lib");
#endif

		std::vector<std::string> names;
		for (size_t d = 0; d < dirs.size(); d++)
			for (int l = 0; LibraryNames[l]; l++)
				if (strchr(LibraryNames[l], '/') == nullptr)
					names.push_back(dirs[d] + "/" + LibraryNames[l]);
		for (int l = 0; LibraryNames[l]; l++)
			names.push_back(LibraryNames[l]);
		return names;
	}
#endif
	static void Free(void** hLib, const LIBFUNC* LibFuncs)
	{
		for (int f = 0; LibFuncs[f].pcFunctionName; f++)
			*LibFuncs[f].ppvFunctionAddress = LibFuncs[f].pvDummyFunctionAddress;
		if (*hLib != nullptr)
		{
			FREELIB(*hLib);
			*hLib = nullptr;
		}
	}
};

#endif
