#!/bin/sh
# Make a self-contained, ad-hoc signed Dream.app and a .dmg (Homebrew Qt6, arm64 or x86_64).
#
#   macx/deploy.sh [--with-fdk] [BUILD_DIR]        default BUILD_DIR: build/gui
#
# libfdk-aac is NOT bundled by default: DREAM is GPLv2 and the Fraunhofer FDK AAC
# licence is not GPL compatible, so DREAM only loads it at runtime if the user has
# it (Dream.app/Contents/Frameworks, $DREAM_LIB_PATH, /opt/homebrew/lib, /usr/local/lib).
# --with-fdk copies .deps/<arch>/lib/libfdk-aac.2.dylib into the bundle anyway -
# for your own use only; do not redistribute such a bundle.
#
# Produces BUILD_DIR/deploy/Dream.app and BUILD_DIR/Dream.dmg; the build tree's
# Dream.app is left untouched (it keeps using the Homebrew libraries in place).
# Env: PREFIX = local deps prefix (default .deps/$(uname -m)); MACDEPLOYQT.
set -eu

REPO=$(cd "$(dirname "$0")/.." && pwd)
with_fdk=0
if [ "${1:-}" = "--with-fdk" ]; then
    with_fdk=1
    shift
fi
BUILD_DIR=${1:-$REPO/build/gui}
ARCH=${ARCH:-$(uname -m)}
PREFIX=${PREFIX:-$REPO/.deps/$ARCH}
BREW_PREFIX=$(brew --prefix)
MACDEPLOYQT=${MACDEPLOYQT:-$(command -v macdeployqt || echo "$BREW_PREFIX/opt/qtbase/bin/macdeployqt")}

src_app=$BUILD_DIR/Dream.app
[ -d "$src_app" ] || { echo "error: $src_app not found - build first (macx/build.sh)" >&2; exit 1; }

out=$BUILD_DIR/deploy
app=$out/Dream.app
exe=$app/Contents/MacOS/Dream
rm -rf "$out"
mkdir -p "$out"
cp -R "$src_app" "$app"
mkdir -p "$app/Contents/Frameworks"

# Libraries DREAM dlopen()s by leaf name (not seen by macdeployqt): the DRM AAC
# decoder/encoder fallbacks. They are found via the executable's LC_RPATH.
# libfaad_drm: the patched faad2 from macx/build-faad-drm.sh (exports NeAACDecDreamDrmFixes),
# not Homebrew's stock build.
faad=$PREFIX/lib/libfaad_drm.dylib
[ -e "$faad" ] || { echo "error: $faad not found (macx/build.sh --deps-only)" >&2; exit 1; }
nm -gU "$faad" | grep -q " _NeAACDecDreamDrmFixes\$" ||
    { echo "error: $faad is not the DREAM-patched faad2 (no NeAACDecDreamDrmFixes)" >&2; exit 1; }
libs="$PREFIX/lib/libfaac_drm.dylib $faad"
# libxaacenc_drm: DRM AAC/HE-AAC/HE-AACv2 encoder (libxaac, Apache-2.0) from
# macx/build-xaac.sh; bundled with its LICENSE and NOTICE (Apache-2.0 section 4)
xaac=$PREFIX/lib/libxaacenc_drm.dylib
if [ -e "$xaac" ]; then
    grep -q "IXHEAACE+DRM" "$xaac" ||
        { echo "error: $xaac is not the DREAM DRM build of libxaac" >&2; exit 1; }
    libs="$libs $xaac"
else
    echo "warning: $xaac not found - the bundle will have no libxaac DRM encoder" >&2
fi
if [ $with_fdk = 1 ]; then
    [ -e "$PREFIX/lib/libfdk-aac.2.dylib" ] ||
        { echo "error: $PREFIX/lib/libfdk-aac.2.dylib not found (macx/build.sh --deps-only)" >&2; exit 1; }
    libs="$libs $PREFIX/lib/libfdk-aac.2.dylib"
    echo "WARNING: bundling libfdk-aac (Fraunhofer FDK AAC licence, NOT GPL compatible)." >&2
    echo "         The resulting Dream.app / Dream.dmg is for private use only - do not redistribute it." >&2
fi
for lib in $libs; do
    if [ -e "$lib" ]; then
        cp -L "$lib" "$app/Contents/Frameworks/$(basename "$lib")"
        chmod u+w "$app/Contents/Frameworks/$(basename "$lib")"
        install_name_tool -id "@rpath/$(basename "$lib")" "$app/Contents/Frameworks/$(basename "$lib")" 2>/dev/null
        # re-sign now, otherwise macdeployqt's own signature check complains
        codesign --force --sign - "$app/Contents/Frameworks/$(basename "$lib")"
    fi
done

# Qt frameworks + plugins + all non-system linked dylibs (libfdk-aac is not linked).
# (verbose log: the "copied:" lines tell which Homebrew kegs end up in the bundle - see licences)
"$MACDEPLOYQT" "$app" -always-overwrite -verbose=2 -libpath="$PREFIX/lib" -libpath="$BREW_PREFIX/lib" \
    > "$out/macdeployqt.log" 2>&1 || { cat "$out/macdeployqt.log"; exit 1; }
grep -E "^(ERROR|WARNING)" "$out/macdeployqt.log" >&2 || true

# Drop build-machine rpaths, keep only the bundle's Frameworks dir.
for rp in $(otool -l "$exe" | awk '/cmd LC_RPATH/{getline; getline; print $2}'); do
    case "$rp" in
        @executable_path/*|@loader_path/*) ;;
        *) install_name_tool -delete_rpath "$rp" "$exe" ;;
    esac
done
otool -l "$exe" | grep -q "@executable_path/../Frameworks" ||
    install_name_tool -add_rpath "@executable_path/../Frameworks" "$exe"

# ---------------------------------------------------------------- licences
# Contents/Resources/licenses/ (sealed by the signature below) and a top-level
# "Licenses" folder in the dmg - same collection as the Linux tarball/.deb:
#   doc/licenses/*            GPL-3.0 (combined work with Apache libxaac), GPL-2.0, LGPL-2.1/3.0,
#                             Apache-2.0, Opus BSD-3, PortAudio MIT, README (components, source offer)
#   libxaac/ faad2/ faac/     per bundled codec lib: its licence texts, our patches, pinned SOURCE
#   third-party/<keg>/        licence files of every Homebrew keg macdeployqt bundled, and
#   THIRD-PARTY.txt           keg, version, SPDX licence (from brew) for each of them
#   fdk-aac/                  only with --with-fdk (its NOTICE)
lic=$app/Contents/Resources/licenses
rm -rf "$lic"
mkdir -p "$lic"
cp "$REPO"/doc/licenses/* "$lic/"
pin() { sed -n "s/^$2=//p" "$REPO/macx/$1" | head -1; }
if [ -e "$xaac" ]; then
    mkdir -p "$lic/libxaac/patches"
    cp "$PREFIX/share/doc/libxaac/LICENSE" "$PREFIX/share/doc/libxaac/NOTICE" "$lic/libxaac/"
    cp "$REPO"/macx/patches/libxaac-*.patch "$lic/libxaac/patches/"
    echo "$(pin build-xaac.sh XAAC_URL) tag $(pin build-xaac.sh XAAC_TAG) commit $(pin build-xaac.sh XAAC_COMMIT); build: macx/build-xaac.sh" > "$lic/libxaac/SOURCE"
fi
fv=$(pin build-faad-drm.sh FAAD_VERSION)
mkdir -p "$lic/faad2/patches"
cp "$REPO"/macx/patches/faad2-*.patch "$lic/faad2/patches/" 2>/dev/null || true
for f in COPYING README; do
    [ -f "$REPO/.deps/src/faad2-$fv/$f" ] && cp "$REPO/.deps/src/faad2-$fv/$f" "$lic/faad2/"
done
[ -f "$lic/faad2/COPYING" ] || cp "$REPO/doc/licenses/GPL-2.0.txt" "$lic/faad2/COPYING"
echo "$(pin build-faad-drm.sh FAAD_URL | sed "s/\$FAAD_VERSION/$fv/") sha256 $(pin build-faad-drm.sh FAAD_SHA256); build: macx/build-faad-drm.sh" > "$lic/faad2/SOURCE"
mkdir -p "$lic/faac/patches"
cp "$REPO"/macx/patches/faac-*.patch "$lic/faac/patches/" 2>/dev/null || true
for f in COPYING README; do   # README carries the ISO/IEC notice
    [ -f "$REPO/.deps/src/faac-1.28/$f" ] && cp "$REPO/.deps/src/faac-1.28/$f" "$lic/faac/"
done
[ -f "$lic/faac/COPYING" ] || cp "$REPO/doc/licenses/LGPL-2.1.txt" "$lic/faac/COPYING"
echo "$(pin build-faac-drm.sh FAAC_URL) tag $(pin build-faac-drm.sh FAAC_TAG) commit $(pin build-faac-drm.sh FAAC_COMMIT); build: macx/build-faac-drm.sh" > "$lic/faac/SOURCE"
if [ $with_fdk = 1 ]; then
    mkdir -p "$lic/fdk-aac"
    for f in NOTICE; do
        [ -f "$REPO/.deps/src/fdk-aac/$f" ] && cp "$REPO/.deps/src/fdk-aac/$f" "$lic/fdk-aac/"
    done
    echo "Fraunhofer FDK AAC: NOT GPL compatible - this bundle is for private use only." > "$lic/fdk-aac/README"
fi
# Homebrew kegs whose files macdeployqt copied (frameworks, dylibs, Qt plugins)
kegs=$(sed -n 's|.*copied: "\([^"]*\)".*|\1|p' "$out/macdeployqt.log" | grep "^$BREW_PREFIX/" |
    while read -r f; do realpath "$f" 2>/dev/null; done |
    sed -n "s|^$BREW_PREFIX/Cellar/\([^/]*\)/.*|\1|p" | sort -u | tr '\n' ' ')
[ -n "$kegs" ] || { echo "error: no bundled Homebrew kegs found in $out/macdeployqt.log" >&2; exit 1; }
for k in $kegs; do
    files=$(cd "$BREW_PREFIX/opt/$k/" && ls -d COPYING* LICENSE* LICENCE* NOTICE* COPYRIGHT* license.html LICENSES 2>/dev/null || true)
    if [ -n "$files" ]; then
        mkdir -p "$lic/third-party/$k"
        for f in $files; do cp -R "$BREW_PREFIX/opt/$k/$f" "$lic/third-party/$k/"; done
    fi
done
# shellcheck disable=SC2086
brew info --json=v2 $kegs | python3 -I -c '
import json, sys, os
lic = sys.argv[1]
print("Third-party libraries bundled in Dream.app/Contents/Frameworks by macdeployqt (from Homebrew).")
print("Licence texts: third-party/<name>/ where the keg ships them; otherwise the common texts in this")
print("folder apply (Qt: LGPL-3.0.txt, glib/SoapySDR/zstd: see licence column). Source: the Homebrew formula URL below.\n")
for f in json.load(sys.stdin)["formulae"]:
    ver = (f.get("installed") or [{}])[0].get("version", f["versions"]["stable"])
    has = "third-party/%s/" % f["name"] if os.path.isdir(os.path.join(lic, "third-party", f["name"])) else "-"
    print("%-22s %-12s %-50s %-28s %s" % (f["name"], ver, f.get("license") or "?", has, f["urls"]["stable"]["url"]))
' "$lic" > "$lic/THIRD-PARTY.txt"

# install_name_tool invalidates signatures; arm64 refuses to run unsigned code ("Killed: 9").
codesign --force --deep --sign - "$app"
codesign --verify --deep "$app"

# Sanity: nothing may still point into Homebrew or the build tree.
if find "$app" -type f -perm -u+x -exec otool -L {} \; 2>/dev/null | grep -E "^[[:space:]]+($BREW_PREFIX|$REPO)" ; then
    echo "warning: the bundle still references libraries outside itself (see above)" >&2
fi

# dmg: Dream.app + top-level "Licenses" folder
dmgroot=$out/dmg
rm -rf "$dmgroot"
mkdir -p "$dmgroot"
ditto "$app" "$dmgroot/Dream.app"
cp -R "$lic" "$dmgroot/Licenses"
hdiutil create "$BUILD_DIR/Dream.dmg" -volname Dream -srcfolder "$dmgroot" -ov -format UDZO
echo "== $app"
echo "== $BUILD_DIR/Dream.dmg"
