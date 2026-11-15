#!/bin/sh
# Build a DRM-capable (Digital Radio Mondiale) faac encoder for DREAM on macOS.
#
# Produces  $PREFIX/lib/libfaac_drm.dylib   (install_name @rpath/libfaac_drm.dylib)
#           $PREFIX/include/faac_drm/{faac.h,faaccfg.h}
#
# Source: upstream faac (https://github.com/knik0/faac, formerly SourceForge),
# tag faac1_28 (2009), i.e. faac 1.28 built with DRM support (what
# "./configure --enable-drm" did): 960-sample frames, leading DRM CRC byte
# (odd maxOutputBytes - the signature DREAM's FaacCheckCallback looks for),
# DRM huffman codeword reordering. Its legacy faacEnc* API and the pack(1)
# faacEncConfiguration layout are exactly what src/sourcedecoders/faac_dll.h
# mirrors. License: LGPL-2.1 (+ ISO MPEG reference-code notice, see README).
#
# Why not a newer faac: 1.29.x-1.31.1 still compile a libfaac_drm, but the 2017
# encoder rewrite left DRM "not sure if it really works" (upstream commit
# e3e335f); tested 1.31.1: decodable mono with heavy frame-periodic amplitude
# modulation, stereo undecodable. DRM was removed in faac >= 1.40.
#
# Patches from macx/patches/faac-1.28-*.patch are applied (DRM stereo fix).
#
# The library is compiled directly with clang from the libfaac source list of
# libfaac/Makefile.am (+ kiss_fft, -DDRM), so no autotools/bootstrap scripts
# from the download are executed.
#
# Usage: macx/build-faac-drm.sh [PREFIX]
#   PREFIX  default: <repo>/.deps/<arch>
#   ARCH    env, default: uname -m (arm64 or x86_64)
#   MACOSX_DEPLOYMENT_TARGET env, default 11.0
set -eu

FAAC_TAG=faac1_28
FAAC_COMMIT=065f22740d34437fa74805a32008674d0a9b64e7
FAAC_URL=https://github.com/knik0/faac.git

REPO=$(cd "$(dirname "$0")/.." && pwd)
ARCH=${ARCH:-$(uname -m)}
PREFIX=${1:-$REPO/.deps/$ARCH}
SRC=$REPO/.deps/src/faac-1.28
BUILD=$REPO/.deps/build/faac-1.28-drm-$ARCH
export MACOSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET:-11.0}
CC=${CC:-clang}

# 1. fetch pinned tag and verify the commit; start from a pristine tree so the
#    patches always apply to the original source
if [ ! -d "$SRC/.git" ]; then
    mkdir -p "$(dirname "$SRC")"
    git -c advice.detachedHead=false clone --quiet --depth 1 --branch "$FAAC_TAG" "$FAAC_URL" "$SRC"
fi
got=$(git -C "$SRC" rev-parse HEAD)
if [ "$got" != "$FAAC_COMMIT" ]; then
    echo "error: $SRC is at $got, expected $FAAC_COMMIT ($FAAC_TAG)" >&2
    exit 1
fi
git -C "$SRC" checkout --quiet -- .
git -C "$SRC" clean --quiet -fdx

# 2. apply DREAM patches
for p in "$REPO"/macx/patches/faac-1.28-*.patch; do
    [ -e "$p" ] || continue
    echo "applying $(basename "$p")"
    git -C "$SRC" apply "$p"
done

# 3. compile (sources = libfaac_la_SOURCES of faac 1.28 libfaac/Makefile.am with USE_DRM)
SOURCES="aacquant.c bitstream.c fft.c frame.c midside.c psychkni.c util.c backpred.c
channels.c filtbank.c huffman.c ltp.c tns.c kiss_fft/kiss_fftr.c kiss_fft/kiss_fft.c"
CFLAGS="-arch $ARCH -O2 -fPIC -DDRM -DHAVE_STDINT_H -DHAVE_INT32_T
-Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -Wno-parentheses-equality
-I$SRC/include -I$SRC/libfaac"

rm -rf "$BUILD"
mkdir -p "$BUILD"
objs=""
for s in $SOURCES; do
    o=$BUILD/$(echo "$s" | tr / _ | sed 's/\.c$/.o/')
    # shellcheck disable=SC2086
    $CC $CFLAGS -c "$SRC/libfaac/$s" -o "$o"
    objs="$objs $o"
done

# export only the public faacEnc* API
EXPORTS=$BUILD/exports.txt
printf '%s\n' _faacEncGetVersion _faacEncGetCurrentConfiguration _faacEncSetConfiguration \
    _faacEncOpen _faacEncGetDecoderSpecificInfo _faacEncEncode _faacEncClose > "$EXPORTS"

mkdir -p "$PREFIX/lib" "$PREFIX/include/faac_drm"
# shellcheck disable=SC2086
$CC -arch "$ARCH" -dynamiclib $objs -lm \
    -Wl,-exported_symbols_list,"$EXPORTS" \
    -install_name @rpath/libfaac_drm.dylib \
    -compatibility_version 1.0.0 -current_version 1.28.0 \
    -o "$PREFIX/lib/libfaac_drm.dylib"
cp "$SRC/include/faac.h" "$SRC/include/faaccfg.h" "$PREFIX/include/faac_drm/"

# 4. sanity check: legacy API exported
for sym in $(cat "$EXPORTS"); do
    nm -gU "$PREFIX/lib/libfaac_drm.dylib" | grep -q " $sym\$" || {
        echo "error: $sym not exported" >&2; exit 1; }
done
echo "built $PREFIX/lib/libfaac_drm.dylib ($(lipo -archs "$PREFIX/lib/libfaac_drm.dylib"), id $(otool -D "$PREFIX/lib/libfaac_drm.dylib" | tail -1))"
