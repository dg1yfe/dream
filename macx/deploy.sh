#!/bin/sh
# Make a self-contained, ad-hoc signed Dream.app and a .dmg (Homebrew Qt6, arm64 or x86_64).
#
#   macx/deploy.sh [BUILD_DIR]        default BUILD_DIR: build/gui
#
# Produces BUILD_DIR/deploy/Dream.app and BUILD_DIR/Dream.dmg; the build tree's
# Dream.app is left untouched (it keeps using the Homebrew libraries in place).
# Env: PREFIX = local deps prefix (default .deps/$(uname -m)); MACDEPLOYQT.
set -eu

REPO=$(cd "$(dirname "$0")/.." && pwd)
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
for lib in "$PREFIX/lib/libfaac_drm.dylib" "$BREW_PREFIX/lib/libfaad_drm.dylib"; do
    if [ -e "$lib" ]; then
        cp -L "$lib" "$app/Contents/Frameworks/$(basename "$lib")"
        chmod u+w "$app/Contents/Frameworks/$(basename "$lib")"
        install_name_tool -id "@rpath/$(basename "$lib")" "$app/Contents/Frameworks/$(basename "$lib")"
    fi
done

# Qt frameworks + plugins + all non-system dylibs (incl. @rpath/libfdk-aac).
"$MACDEPLOYQT" "$app" -always-overwrite -libpath="$PREFIX/lib" -libpath="$BREW_PREFIX/lib"

# Drop build-machine rpaths, keep only the bundle's Frameworks dir.
for rp in $(otool -l "$exe" | awk '/cmd LC_RPATH/{getline; getline; print $2}'); do
    case "$rp" in
        @executable_path/*|@loader_path/*) ;;
        *) install_name_tool -delete_rpath "$rp" "$exe" ;;
    esac
done
otool -l "$exe" | grep -q "@executable_path/../Frameworks" ||
    install_name_tool -add_rpath "@executable_path/../Frameworks" "$exe"

# install_name_tool invalidates signatures; arm64 refuses to run unsigned code ("Killed: 9").
codesign --force --deep --sign - "$app"
codesign --verify --deep "$app"

# Sanity: nothing may still point into Homebrew or the build tree.
if find "$app" -type f -perm -u+x -exec otool -L {} \; 2>/dev/null | grep -E "^[[:space:]]+($BREW_PREFIX|$REPO)" ; then
    echo "warning: the bundle still references libraries outside itself (see above)" >&2
fi

hdiutil create "$BUILD_DIR/Dream.dmg" -volname Dream -srcfolder "$app" -ov -format UDZO
echo "== $app"
echo "== $BUILD_DIR/Dream.dmg"
