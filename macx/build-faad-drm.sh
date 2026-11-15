#!/bin/sh
# Build a fixed faad2 DRM decoder (libfaad_drm.dylib) for DREAM on macOS.
#
# Produces  $PREFIX/lib/libfaad_drm.dylib   (install_name @rpath/libfaad_drm.dylib)
#           $PREFIX/include/faad_drm/neaacdec.h
#
# Source: faad2 2.11.4 (same version and tarball as Homebrew's faad2 formula),
# https://github.com/FreewareAdvancedAudio/faad2, GPL-2.0-or-later.
# Patches from macx/patches/faad2-2.11.4-*.patch are applied:
#   - drm-hcr-short: HCR codeword order for grouped short windows as in
#     ISO/IEC 14496-3 / fdk-aac (unit-wise window interleave)
#   - drm-mpeg-ps: decode MPEG-4 PS (bs_extension_id 2) in DRM mode, which is
#     the PS flavour ETSI ES 201 980 (V4.x) prescribes
#
# Compiled directly with clang from libfaad/*.c using the defines of faad2's
# CMakeLists.txt for the faad_drm target (+ DRM_SUPPORT), so no build scripts
# from the download are executed.
#
# Usage: macx/build-faad-drm.sh [PREFIX]
#   PREFIX  default: <repo>/.deps/<arch>
#   ARCH    env, default: uname -m (arm64 or x86_64)
#   MACOSX_DEPLOYMENT_TARGET env, default 11.0
#   FAAD_NO_PATCHES=1  build stock 2.11.4 (for comparison tests)
set -eu

FAAD_VERSION=2.11.4
FAAD_URL=https://github.com/FreewareAdvancedAudio/faad2/archive/refs/tags/$FAAD_VERSION.tar.gz
FAAD_SHA256=ee479ccbae4a8387ab696e6f21a481bd83fe3881471cafa81b4ae59d7d3aed43

REPO=$(cd "$(dirname "$0")/.." && pwd)
ARCH=${ARCH:-$(uname -m)}
PREFIX=${1:-$REPO/.deps/$ARCH}
DL=$REPO/.deps/src/faad2-dl
SRC=$REPO/.deps/src/faad2-$FAAD_VERSION
BUILD=$REPO/.deps/build/faad2-$FAAD_VERSION-drm-$ARCH
export MACOSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET:-11.0}
CC=${CC:-clang}

# 1. fetch + verify the tarball, unpack a pristine tree every time
mkdir -p "$DL"
TARBALL=$DL/faad2-$FAAD_VERSION.tar.gz
if [ ! -f "$TARBALL" ]; then
    curl -fsSL -o "$TARBALL.part" "$FAAD_URL"
    mv "$TARBALL.part" "$TARBALL"
fi
got=$(shasum -a 256 "$TARBALL" | cut -d' ' -f1)
if [ "$got" != "$FAAD_SHA256" ]; then
    echo "error: $TARBALL sha256 $got, expected $FAAD_SHA256" >&2
    exit 1
fi
rm -rf "$SRC"
mkdir -p "$SRC"
tar -xzf "$TARBALL" -C "$SRC" --strip-components 1

# 2. apply DREAM patches
if [ "${FAAD_NO_PATCHES:-0}" != 1 ]; then
    for p in "$REPO"/macx/patches/faad2-$FAAD_VERSION-*.patch; do
        [ -e "$p" ] || continue
        echo "applying $(basename "$p")"
        patch -s -p1 -d "$SRC" < "$p"
    done
fi

# 3. compile (defines = FAAD_DEFINES of CMakeLists.txt + DRM_SUPPORT for faad_drm)
CFLAGS="-arch $ARCH -O2 -fPIC -fvisibility=hidden -Wall -Wno-unused-function
-DHAVE_INTTYPES_H=1 -DHAVE_MEMCPY=1 -DHAVE_STRING_H=1 -DHAVE_STRINGS_H=1
-DHAVE_SYS_STAT_H=1 -DHAVE_SYS_TYPES_H=1 -DHAVE_LRINTF=1 -DAPPLY_DRC
-DPACKAGE_VERSION=\"$FAAD_VERSION\" -DDRM_SUPPORT
-I$SRC/include -I$SRC/libfaad"

rm -rf "$BUILD"
mkdir -p "$BUILD"
objs=""
for s in "$SRC"/libfaad/*.c; do
    o=$BUILD/$(basename "$s" .c).o
    # shellcheck disable=SC2086
    $CC $CFLAGS -c "$s" -o "$o"
    objs="$objs $o"
done

mkdir -p "$PREFIX/lib" "$PREFIX/include/faad_drm"
# shellcheck disable=SC2086
$CC -arch "$ARCH" -dynamiclib $objs -lm \
    -install_name @rpath/libfaad_drm.dylib \
    -compatibility_version 2.0.0 -current_version $FAAD_VERSION \
    -o "$PREFIX/lib/libfaad_drm.dylib"
cp "$SRC/include/neaacdec.h" "$PREFIX/include/faad_drm/"

for sym in NeAACDecOpen NeAACDecInitDRM NeAACDecDecode NeAACDecClose; do
    nm -gU "$PREFIX/lib/libfaad_drm.dylib" | grep -q " _$sym\$" || {
        echo "error: $sym not exported" >&2; exit 1; }
done
echo "built $PREFIX/lib/libfaad_drm.dylib ($(lipo -archs "$PREFIX/lib/libfaad_drm.dylib"), id $(otool -D "$PREFIX/lib/libfaad_drm.dylib" | tail -1))"
