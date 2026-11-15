DREAM - licences and source of a binary distribution
====================================================

Not legal advice. This describes what the DREAM build scripts in this tree
actually bundle, as of the build scripts in the tree; check it against each
release before shipping.

1. Licence of DREAM and of the combined work
--------------------------------------------
DREAM's own code is free software under the GNU General Public License version 2
or (at your option) any later version (COPYING, source file headers).

Binary distributions that include the libxaac DRM encoder (libxaacenc_drm,
Apache-2.0) are distributed as one combined work under the GNU General Public
License version 3 (GPL-3.0.txt). Apache-2.0 is compatible with GPL-3.0 but not
with GPL-2.0-only, which is why the combined work is GPL-3.0. DREAM's
"or later" wording allows this.

2. Texts in this directory
--------------------------
  GPL-3.0.txt             the combined work
  GPL-2.0.txt             DREAM (or later), faad2, fftw, Journaline
  LGPL-3.0.txt            Qt 6 (we take Qt's LGPL-3.0 option of its dual licence)
  LGPL-2.1.txt            faac (libfaac_drm), qwt, libsndfile, hamlib
  Qwt-COPYING.txt         qwt 6.3.0 licence incl. the Qwt exception
  Apache-2.0.txt          libxaac (also libxaac/LICENSE and libxaac/NOTICE)
  Opus-BSD-3-Clause.txt   Opus-derived code compiled into DREAM (concealment.cpp,
                          opus_dll.h)
  Opus-COPYING.txt        libopus 1.6.1 as bundled
  PortAudio-MIT.txt       PortAudio ring buffer compiled into DREAM (pa_ringbuffer.c)
  PortAudio-LICENSE.txt   PortAudio 19.7.0 library as bundled
  speexdsp-COPYING.txt    speexdsp 1.2.1 as bundled (BSD-3-Clause)
  gpsd-COPYING.txt        libgps 3.27.5 as bundled (BSD-2-Clause)

3. Components actually bundled (macOS app; the Linux tarball/.deb lists its own)
-----------------------------------------------------------------------------
  Component            Version        Licence
  Qt 6 (qtbase, qtsvg) 6.12.0         LGPL-3.0 option of Qt's dual licence
  qwt                  6.3.0          LGPL-2.1 with Qwt exception
  libsndfile           1.2.2          LGPL-2.1-or-later
  hamlib               4.7.2          LGPL-2.1-or-later
  faac (libfaac_drm)   faac1_28       LGPL-2.1-or-later for FAAC changes; ISO/IEC notice
  faad2 (libfaad_drm)  2.11.4         GPL-2.0-or-later
  libxaacenc_drm       android-16.0.0_r4 (= v0.1.12)  Apache-2.0
  fftw                 3.3.11         GPL-2.0-or-later AND BSD-2-Clause
  speexdsp             1.2.1          BSD-3-Clause
  opus                 1.6.1          BSD-3-Clause
  portaudio            19.7.0         MIT
  libgps (gpsd)        3.27.5         BSD-2-Clause
  SoapySDR             0.9.0          BSL-1.0 (licence text: see SoapySDR upstream)
Compiled into DREAM (no separate library): Journaline (GPL-2.0-or-later),
PortAudio ring buffer (MIT), Opus-derived concealment code (BSD-3-Clause).

Not bundled: zlib, libpcap, the macOS system frameworks, and on Linux the
distribution's ALSA, PulseAudio and JACK libraries. These keep their licences
with their own packages.

fdk-aac is NOT included in DREAM, in the macOS app, or in the Linux packages. Its
licence (Fraunhofer FDK AAC) is not GPL compatible. DREAM loads a user-installed
libfdk-aac at run time if one is present, and works without it.

4. Source of the binary (GPL-3.0 section 6, LGPL, Apache-2.0)
-------------------------------------------------------------
Corresponding source is given by pointing to the exact source versions below
(GPL-3.0 section 6(d), access from a network server). We keep these pointers
available for as long as we distribute the binary.

  DREAM:       https://github.com/Drm-tools/dream, at the exact git commit this
               binary was built from (shown in the package version, e.g.
               "+git<commit>"). Before distributing a binary, that commit must be
               published there; until it is, only the written offer below
               applies.
  faad2:       https://github.com/FreewareAdvancedAudio/faad2, tag 2.11.4
               (sha256 pinned in macx/build-faad-drm.sh and linux/build-deps.sh).
  faac:        https://github.com/knik0/faac, tag faac1_28,
               commit 065f22740d34437fa74805a32008674d0a9b64e7.
  libxaac:     https://android.googlesource.com/platform/external/libxaac,
               tag android-16.0.0_r4, commit 567c7713c94e09ebff6d0c95fd1edb9c1f7e76c8.
  Qt, qwt, libsndfile, hamlib, fftw, speexdsp, portaudio, opus, gpsd, SoapySDR:
               the upstream release of the version in section 3, as built by
               Homebrew. Verify per release that no formula patch is applied; if
               one is, its source must be published too.

The DREAM patches applied to faad2, faac and libxaac are in the DREAM source tree
(macx/patches/) and in licenses/*/patches/ of the bundle. The scripts that build
the codec libraries are macx/build-*.sh and linux/build-deps.sh.

LGPL: the libraries in section 3 are separate shared libraries, not statically
linked. You may replace them with a modified version of the same library (the
bundled copies can be swapped for another build with the same name).

Written offer: where a copy cannot be linked to the URLs above, a written offer
(GPL-3.0 section 6(b)) is valid for at least three years from the date of
distribution, to any third party, at no more than the cost of making the source
available. Request it from the DREAM project (see the project home page).

Apache-2.0 (libxaac) section 4:
  (a) the Apache-2.0 licence is included (Apache-2.0.txt, libxaac/LICENSE);
  (b) changed files are marked: patch 0006 adds a "Modified for DREAM" line to
      each modified file, and new DREAM files carry "Copyright (C) 2026 The DREAM
      project" headers. The patches themselves are in libxaac/patches/;
  (c) copyright, patent and attribution notices of the original sources are kept;
  (d) NOTICE is included (libxaac/NOTICE).

faac: the ISO/IEC notice in faac/README is kept with the sources in faac/.

BSD-3-Clause (Opus-derived code, speexdsp, opus): the copyright notices, list of
conditions and disclaimer are reproduced in Opus-BSD-3-Clause.txt, Opus-COPYING.txt
and speexdsp-COPYING.txt, as binary distribution requires.

MIT (PortAudio): the MIT notice is in PortAudio-MIT.txt and PortAudio-LICENSE.txt.
