#!/bin/sh
# Build Google/Ittiam libxaac's AAC encoder (AAC-LC, HE-AAC v1/v2, also LD/ELD/USAC)
# for DREAM on macOS: the DRM AAC/HE-AAC/HE-AACv2 encoder DREAM loads at run time
# (XaacCodec in src/sourcedecoders/xaac_codec.cpp).
#
# The pinned tree is patched with macx/patches/libxaac-*.patch (applied in name order):
# 960-sample HE-AAC v1/v2, 12 kHz core, DRM access units (ER AAC with VCB11/HCR, CRC,
# reversed SBR), exact frame sizes. The library name carries "_drm" and its ID string
# "IXHEAACE+DRM" so DREAM never picks up a stock libxaac.
#
# Produces  $PREFIX/lib/libxaacenc_drm.dylib   (install_name @rpath/libxaacenc_drm.dylib)
#           $PREFIX/include/xaacenc_drm/{ixheaace_api.h,ixheaac_type_def.h,ixheaac_error_standards.h}
#           $PREFIX/share/doc/libxaac/{LICENSE,NOTICE}
#           $BUILD/xaacenc                 (upstream test-bench encoder, statically linked; not installed)
#
# Source: AOSP platform/external/libxaac, tag android-16.0.0_r4 (= upstream
# ittiam-systems/libxaac v0.1.12, Jan 2025). License: Apache-2.0 (LICENSE/NOTICE);
# combined with GPLv2-or-later DREAM only distributable as GPLv3.
#
# The library is compiled directly with clang from the source lists in
# encoder/libxaacenc.cmake, encoder/drc_src/libxaacenc_drc.cmake and
# common/common.cmake (read as data with sed). No CMake, script or tool from
# the download is executed. Only the first run needs the network.
#
# Usage: macx/build-xaac.sh [PREFIX]
#   PREFIX  default: <repo>/.deps/<arch>
#   ARCH    env, default: uname -m (arm64 or x86_64)
#   XAAC_EXTRA_CFLAGS env, e.g. "-O1 -g -fsanitize=address,undefined" for a scratch build
#   XAAC_BUILD env, build dir override (use with a scratch PREFIX for sanitizer builds)
#   XAAC_SRC env, source checkout override (default <repo>/.deps/src/libxaac)
#   MACOSX_DEPLOYMENT_TARGET env, default 11.0
set -eu

XAAC_TAG=android-16.0.0_r4
XAAC_COMMIT=567c7713c94e09ebff6d0c95fd1edb9c1f7e76c8
XAAC_TREE=9ef456ddb5408369cd290dbaa1764f30d7828cb8
XAAC_URL=https://android.googlesource.com/platform/external/libxaac

REPO=$(cd "$(dirname "$0")/.." && pwd)
ARCH=${ARCH:-$(uname -m)}
PREFIX=${1:-$REPO/.deps/$ARCH}
SRC=${XAAC_SRC:-$REPO/.deps/src/libxaac}
BUILD=${XAAC_BUILD:-$REPO/.deps/build/libxaac-enc-$ARCH}
export MACOSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET:-11.0}
CC=${CC:-clang}
EXTRA=${XAAC_EXTRA_CFLAGS:-}

# 1. fetch pinned tag; verify commit and tree hash (git object ids are SHA-1
#    content hashes, so a matching tree id pins every file); pristine tree
if [ ! -d "$SRC/.git" ]; then
    mkdir -p "$(dirname "$SRC")"
    git -c advice.detachedHead=false clone --quiet --depth 1 --branch "$XAAC_TAG" "$XAAC_URL" "$SRC"
fi
got=$(git -C "$SRC" rev-parse HEAD)
if [ "$got" != "$XAAC_COMMIT" ]; then
    echo "error: $SRC is at $got, expected $XAAC_COMMIT ($XAAC_TAG)" >&2
    exit 1
fi
tree=$(git -C "$SRC" rev-parse 'HEAD^{tree}')
if [ "$tree" != "$XAAC_TREE" ]; then
    echo "error: $SRC tree $tree, expected $XAAC_TREE" >&2
    exit 1
fi
git -C "$SRC" checkout --quiet -- .
git -C "$SRC" clean --quiet -fdx

# 1b. DREAM patches (960-sample HE-AAC/HE-AACv2, 12 kHz core); recorded in the stamp
PATCHES=$(ls "$REPO"/macx/patches/libxaac-*.patch 2>/dev/null || true)
for p in $PATCHES; do
    git -C "$SRC" apply --whitespace=nowarn "$p" || {
        echo "error: $p does not apply to $XAAC_TAG" >&2; exit 1; }
done
PATCH_SUM=$(cat $PATCHES /dev/null | shasum -a 256 | cut -c1-16)

# 2. source list = LIBXAACENC_SRCS (+drc) + LIBXAAC_COMMON_SRCS of the cmake files
SOURCES=$(sed -n 's/^ *"\${XAAC_ROOT}\/\(.*\.c\)".*/\1/p' \
    "$SRC/encoder/libxaacenc.cmake" "$SRC/encoder/drc_src/libxaacenc_drc.cmake" \
    "$SRC/common/common.cmake")
n=$(echo "$SOURCES" | wc -l | tr -d ' ')
[ "$n" -gt 100 ] || { echo "error: only $n sources found" >&2; exit 1; }

# portable C, no asm/NEON in the encoder; -fwrapv as upstream does for x86_64
CFLAGS="-arch $ARCH -O3 -fPIC -std=gnu99 -fwrapv -Wno-everything $EXTRA
-I$SRC/encoder -I$SRC/encoder/drc_src -I$SRC/common"

rm -rf "$BUILD"
mkdir -p "$BUILD/obj"
objs=""
for s in $SOURCES; do
    o=$BUILD/obj/$(echo "$s" | tr / _ | sed 's/\.c$/.o/')
    # shellcheck disable=SC2086
    $CC $CFLAGS -c "$SRC/$s" -o "$o"
    objs="$objs $o"
done
# shellcheck disable=SC2086
ar rcs "$BUILD/libxaacenc.a" $objs

# 3. shared library exporting only the public ixheaace_* API
EXPORTS=$BUILD/exports.txt
printf '%s\n' _ixheaace_get_lib_id_strings _ixheaace_create _ixheaace_process \
    _ixheaace_delete > "$EXPORTS"
mkdir -p "$PREFIX/lib" "$PREFIX/include/xaacenc_drm" "$PREFIX/share/doc/libxaac"
# shellcheck disable=SC2086
$CC -arch "$ARCH" -dynamiclib $objs -lm $EXTRA \
    -Wl,-exported_symbols_list,"$EXPORTS" \
    -install_name @rpath/libxaacenc_drm.dylib \
    -compatibility_version 0.1.0 -current_version 0.1.12 \
    -o "$PREFIX/lib/libxaacenc_drm.dylib"
cp "$SRC/encoder/ixheaace_api.h" "$SRC/common/ixheaac_type_def.h" \
   "$SRC/common/ixheaac_error_standards.h" "$PREFIX/include/xaacenc_drm/"
cp "$SRC/LICENSE" "$SRC/NOTICE" "$PREFIX/share/doc/libxaac/"
printf '%s %s patches %s\n' "$XAAC_TAG" "$XAAC_COMMIT" "$PATCH_SUM" > "$PREFIX/lib/libxaacenc_drm.dream-stamp"

# 4. upstream test-bench encoder (handy for experiments), linked statically
TB="test/encoder/ixheaace_testbench.c test/encoder/ixheaace_error.c test/encoder/impd_drc_user_config.c"
tbobjs=""
for s in $TB; do
    o=$BUILD/obj/$(echo "$s" | tr / _ | sed 's/\.c$/.o/')
    # shellcheck disable=SC2086
    $CC $CFLAGS -I"$SRC/test/encoder" -c "$SRC/$s" -o "$o"
    tbobjs="$tbobjs $o"
done
# shellcheck disable=SC2086
$CC -arch "$ARCH" $tbobjs "$BUILD/libxaacenc.a" -lm $EXTRA -o "$BUILD/xaacenc"

# 5. sanity check: API exported
for sym in $(cat "$EXPORTS"); do
    nm -gU "$PREFIX/lib/libxaacenc_drm.dylib" | grep -q " $sym\$" || {
        echo "error: $sym not exported" >&2; exit 1; }
done
grep -q "IXHEAACE+DRM" "$PREFIX/lib/libxaacenc_drm.dylib" ||
    { echo "error: libxaacenc_drm.dylib lacks the DRM marker (patches not applied?)" >&2; exit 1; }
echo "built $PREFIX/lib/libxaacenc_drm.dylib ($(lipo -archs "$PREFIX/lib/libxaacenc_drm.dylib"), id $(otool -D "$PREFIX/lib/libxaacenc_drm.dylib" | tail -1)), test bench $BUILD/xaacenc"
