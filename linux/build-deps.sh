#!/bin/sh
# Build DREAM's runtime-loaded DRM codec libraries for Linux (.so), the Linux
# counterpart of macx/build-faad-drm.sh, macx/build-faac-drm.sh and macx/build-xaac.sh:
# same pinned sources (tarball sha256 / git commit + tree ids) and the SAME patch
# files, picked up by glob from macx/patches/ so new patches apply automatically.
#
#   libfaad_drm.so.2     faad2 2.11.4 + macx/patches/faad2-2.11.4-*.patch  (GPL-2.0-or-later)
#   libfaac_drm.so.0     faac 1.28   + macx/patches/faac-1.28-*.patch     (LGPL-2.1 + ISO notice)
#   libxaacenc_drm.so    libxaac android-16.0.0_r4 + macx/patches/libxaac-*.patch (Apache-2.0)
#
# fdk-aac is not built (licence; DREAM runs without it). Every library is compiled
# directly from its source list; no build scripts from the downloads are executed.
# Pristine sources are exported into the per-arch build dir and patched there, so the
# shared download cache (.deps/src) is never modified.
#
# Usage: linux/build-deps.sh [PREFIX]
#   PREFIX  default: <repo>/.deps/linux-<uname -m>
#   CC      default: cc
#   DEPS_FORCE=1   rebuild even if the stamps match
# Output: $PREFIX/lib/*.so*, $PREFIX/include/{faad_drm,faac_drm,xaacenc_drm}/,
#         $PREFIX/share/doc/{faad2,faac,libxaac}/ (licence texts + applied patches)
set -eu

REPO=$(cd "$(dirname "$0")/.." && pwd)
ARCH=$(uname -m)
PREFIX=${1:-$REPO/.deps/linux-$ARCH}
CACHE=$REPO/.deps/src
WORK=$REPO/.deps/build/linux-$ARCH
CC=${CC:-cc}; export CC
PATCHES=$REPO/macx/patches
FORCE=${DEPS_FORCE:-0}

sha256() { sha256sum "$@" | cut -d' ' -f1; }
# stamp = pinned version + this script + the patch files of one library
stamp_of() { { echo "$1"; cat "$0"; shift; cat "$@" /dev/null; } | sha256; }
uptodate() { [ "$FORCE" != 1 ] && [ -f "$1" ] && [ "$(cat "$2" 2>/dev/null)" = "$3" ]; }
# export a pinned git commit of a cached clone into a fresh directory
git_export() { # cache url tag commit tree dest
    if [ ! -d "$1/.git" ]; then
        mkdir -p "$(dirname "$1")"
        git -c advice.detachedHead=false clone --quiet --depth 1 --branch "$3" "$2" "$1"
    fi
    got=$(git -C "$1" rev-parse "$4^{commit}" 2>/dev/null || echo none)
    [ "$got" = "$4" ] || { echo "error: $1 lacks commit $4 ($3)" >&2; exit 1; }
    if [ -n "$5" ]; then
        tree=$(git -C "$1" rev-parse "$4^{tree}")
        [ "$tree" = "$5" ] || { echo "error: $1 tree $tree, expected $5" >&2; exit 1; }
    fi
    rm -rf "$6"; mkdir -p "$6"
    git -C "$1" archive "$4" | tar -x -C "$6"
}
# apply patches with git apply inside a throw-away repo (so paths are relative to $1)
apply_patches() { # dir patch...
    d=$1; shift
    [ $# -gt 0 ] || return 0
    git -C "$d" init -q
    for p in "$@"; do
        [ -e "$p" ] || continue
        echo "  applying $(basename "$p")"
        git -C "$d" apply --whitespace=nowarn "$p" || { echo "error: $p does not apply" >&2; exit 1; }
    done
    rm -rf "$d/.git"
}
# shared library: name soname objs... ; optional EXPORTS (space separated) via version script
link_so() { # out soname exports objs...
    out=$1 soname=$2 exports=$3; shift 3
    vs=""
    if [ -n "$exports" ]; then
        vs=$(dirname "$out")/.$(basename "$out").map
        { echo "{ global:"; for s in $exports; do echo "  $s;"; done; echo "local: *; };"; } > "$vs"
        vs="-Wl,--version-script=$vs"
    fi
    # shellcheck disable=SC2086
    $CC -shared -o "$out" "$@" -lm -Wl,-soname,"$soname" -Wl,-z,noexecstack $vs
    rm -f "$(dirname "$out")"/.*.map
}
check_exports() { # lib sym...
    lib=$1; shift
    for s in "$@"; do
        nm -D --defined-only "$lib" | grep -q " T $s\$" || { echo "error: $s not exported by $lib" >&2; exit 1; }
    done
}
compile_all() { # objdir cflags srcroot files...   (parallel; fails if any file fails)
    od=$1 cf=$2 root=$3; shift 3
    rm -rf "$od"; mkdir -p "$od"
    for s in "$@"; do
        echo "$s"
    done | xargs -P "$(nproc)" -I{} sh -c '
        o="$1/$(echo "$3" | tr / _ | sed "s/\.c\$/.o/")"
        exec $CC $2 -c "$0/$3" -o "$o"' "$root" "$od" "$cf" {} ||
        { echo "error: compilation failed in $root" >&2; exit 1; }
    [ "$(ls "$od" | wc -l)" -eq "$#" ] || { echo "error: missing objects in $od" >&2; exit 1; }
}

mkdir -p "$PREFIX/lib" "$PREFIX/include" "$PREFIX/share/doc" "$WORK"

# ------------------------------------------------------------------ faad2 2.11.4 (decoder)
FAAD_VERSION=2.11.4
FAAD_URL=https://github.com/FreewareAdvancedAudio/faad2/archive/refs/tags/$FAAD_VERSION.tar.gz
FAAD_SHA256=ee479ccbae4a8387ab696e6f21a481bd83fe3881471cafa81b4ae59d7d3aed43
faad_patches=$(ls "$PATCHES"/faad2-$FAAD_VERSION-*.patch 2>/dev/null || true)
# shellcheck disable=SC2086
want=$(stamp_of "faad2 $FAAD_VERSION $FAAD_SHA256" $faad_patches)
if uptodate "$PREFIX/lib/libfaad_drm.so.2" "$PREFIX/lib/libfaad_drm.dream-stamp" "$want"; then
    echo "== libfaad_drm.so.2 up to date"
else
    echo "== building libfaad_drm.so.2 (faad2 $FAAD_VERSION)"
    mkdir -p "$CACHE/faad2-dl"
    tb=$CACHE/faad2-dl/faad2-$FAAD_VERSION.tar.gz
    [ -f "$tb" ] || { curl -fsSL -o "$tb.part" "$FAAD_URL"; mv "$tb.part" "$tb"; }
    [ "$(sha256 "$tb")" = "$FAAD_SHA256" ] || { echo "error: $tb sha256 mismatch" >&2; exit 1; }
    src=$WORK/faad2-src; rm -rf "$src"; mkdir -p "$src"
    tar -xzf "$tb" -C "$src" --strip-components 1
    # shellcheck disable=SC2086
    apply_patches "$src" $faad_patches
    # defines = FAAD_DEFINES of faad2's CMakeLists.txt + DRM_SUPPORT (faad_drm target)
    CF="-O2 -fPIC -fvisibility=hidden -w -DHAVE_INTTYPES_H=1 -DHAVE_MEMCPY=1 -DHAVE_STRING_H=1
        -DHAVE_STRINGS_H=1 -DHAVE_SYS_STAT_H=1 -DHAVE_SYS_TYPES_H=1 -DHAVE_LRINTF=1 -DAPPLY_DRC
        -DPACKAGE_VERSION=\"$FAAD_VERSION\" -DDRM_SUPPORT -I$src/include -I$src/libfaad"
    files=$(cd "$src" && ls libfaad/*.c)
    # shellcheck disable=SC2086
    compile_all "$WORK/faad2-obj" "$CF" "$src" $files
    # shellcheck disable=SC2046
    link_so "$PREFIX/lib/libfaad_drm.so.2" libfaad_drm.so.2 "" $(ls "$WORK"/faad2-obj/*.o)
    ln -sf libfaad_drm.so.2 "$PREFIX/lib/libfaad_drm.so"
    check_exports "$PREFIX/lib/libfaad_drm.so.2" NeAACDecOpen NeAACDecInitDRM NeAACDecDecode NeAACDecClose
    mkdir -p "$PREFIX/include/faad_drm" "$PREFIX/share/doc/faad2/patches"
    cp "$src/include/neaacdec.h" "$PREFIX/include/faad_drm/"
    cp "$src/COPYING" "$PREFIX/share/doc/faad2/"
    rm -f "$PREFIX"/share/doc/faad2/patches/*
    # shellcheck disable=SC2086
    [ -z "$faad_patches" ] || cp $faad_patches "$PREFIX/share/doc/faad2/patches/"
    echo "$FAAD_URL sha256 $FAAD_SHA256" > "$PREFIX/share/doc/faad2/SOURCE"
    echo "$want" > "$PREFIX/lib/libfaad_drm.dream-stamp"
fi

# ------------------------------------------------------------------ faac 1.28 (DRM encoder)
FAAC_TAG=faac1_28
FAAC_COMMIT=065f22740d34437fa74805a32008674d0a9b64e7
FAAC_URL=https://github.com/knik0/faac.git
faac_patches=$(ls "$PATCHES"/faac-1.28-*.patch 2>/dev/null || true)
# shellcheck disable=SC2086
want=$(stamp_of "faac $FAAC_TAG $FAAC_COMMIT" $faac_patches)
if uptodate "$PREFIX/lib/libfaac_drm.so.0" "$PREFIX/lib/libfaac_drm.dream-stamp" "$want"; then
    echo "== libfaac_drm.so.0 up to date"
else
    echo "== building libfaac_drm.so.0 (faac 1.28 DRM)"
    src=$WORK/faac-src
    git_export "$CACHE/faac-1.28" "$FAAC_URL" "$FAAC_TAG" "$FAAC_COMMIT" "" "$src"
    # shellcheck disable=SC2086
    apply_patches "$src" $faac_patches
    # sources = libfaac_la_SOURCES of faac 1.28 libfaac/Makefile.am with DRM (+ kiss_fft)
    files="aacquant.c bitstream.c fft.c frame.c midside.c psychkni.c util.c backpred.c
           channels.c filtbank.c huffman.c ltp.c tns.c kiss_fft/kiss_fftr.c kiss_fft/kiss_fft.c"
    CF="-O2 -fPIC -w -DDRM -DHAVE_STDINT_H -DHAVE_INT32_T -I$src/include -I$src/libfaac"
    # shellcheck disable=SC2086
    compile_all "$WORK/faac-obj" "$CF" "$src/libfaac" $files
    EXP="faacEncGetVersion faacEncGetCurrentConfiguration faacEncSetConfiguration faacEncOpen
         faacEncGetDecoderSpecificInfo faacEncEncode faacEncClose"
    # shellcheck disable=SC2046
    link_so "$PREFIX/lib/libfaac_drm.so.0" libfaac_drm.so.0 "$EXP" $(ls "$WORK"/faac-obj/*.o)
    ln -sf libfaac_drm.so.0 "$PREFIX/lib/libfaac_drm.so"
    # shellcheck disable=SC2086
    check_exports "$PREFIX/lib/libfaac_drm.so.0" $EXP
    mkdir -p "$PREFIX/include/faac_drm" "$PREFIX/share/doc/faac/patches"
    cp "$src/include/faac.h" "$src/include/faaccfg.h" "$PREFIX/include/faac_drm/"
    for f in COPYING README; do [ -f "$src/$f" ] && cp "$src/$f" "$PREFIX/share/doc/faac/"; done
    rm -f "$PREFIX"/share/doc/faac/patches/*
    # shellcheck disable=SC2086
    [ -z "$faac_patches" ] || cp $faac_patches "$PREFIX/share/doc/faac/patches/"
    echo "$FAAC_URL tag $FAAC_TAG commit $FAAC_COMMIT" > "$PREFIX/share/doc/faac/SOURCE"
    echo "$want" > "$PREFIX/lib/libfaac_drm.dream-stamp"
fi

# ------------------------------------------------------------------ libxaac (DRM HE-AAC encoder)
XAAC_TAG=android-16.0.0_r4
XAAC_COMMIT=567c7713c94e09ebff6d0c95fd1edb9c1f7e76c8
XAAC_TREE=9ef456ddb5408369cd290dbaa1764f30d7828cb8
XAAC_URL=https://android.googlesource.com/platform/external/libxaac
xaac_patches=$(ls "$PATCHES"/libxaac-*.patch 2>/dev/null || true)
# shellcheck disable=SC2086
want=$(stamp_of "libxaac $XAAC_TAG $XAAC_COMMIT $XAAC_TREE" $xaac_patches)
if uptodate "$PREFIX/lib/libxaacenc_drm.so" "$PREFIX/lib/libxaacenc_drm.dream-stamp" "$want"; then
    echo "== libxaacenc_drm.so up to date"
else
    echo "== building libxaacenc_drm.so (libxaac $XAAC_TAG)"
    src=$WORK/libxaac-src
    git_export "$CACHE/libxaac" "$XAAC_URL" "$XAAC_TAG" "$XAAC_COMMIT" "$XAAC_TREE" "$src"
    # shellcheck disable=SC2086
    apply_patches "$src" $xaac_patches
    # source list = LIBXAACENC_SRCS (+drc) + LIBXAAC_COMMON_SRCS, read as data from the cmake files
    files=$(sed -n 's/^ *"\${XAAC_ROOT}\/\(.*\.c\)".*/\1/p' \
        "$src/encoder/libxaacenc.cmake" "$src/encoder/drc_src/libxaacenc_drc.cmake" \
        "$src/common/common.cmake")
    n=$(echo "$files" | wc -l)
    [ "$n" -gt 100 ] || { echo "error: only $n libxaac sources found" >&2; exit 1; }
    # portable C (no asm/intrinsics in the encoder); -fwrapv as upstream
    CF="-O3 -fPIC -std=gnu99 -fwrapv -w -I$src/encoder -I$src/encoder/drc_src -I$src/common"
    # shellcheck disable=SC2086
    compile_all "$WORK/libxaac-obj" "$CF" "$src" $files
    EXP="ixheaace_get_lib_id_strings ixheaace_create ixheaace_process ixheaace_delete"
    # shellcheck disable=SC2046
    link_so "$PREFIX/lib/libxaacenc_drm.so" libxaacenc_drm.so "$EXP" $(ls "$WORK"/libxaac-obj/*.o)
    # shellcheck disable=SC2086
    check_exports "$PREFIX/lib/libxaacenc_drm.so" $EXP
    grep -q "IXHEAACE+DRM" "$PREFIX/lib/libxaacenc_drm.so" ||
        { echo "error: libxaacenc_drm.so lacks the DRM marker (patches not applied?)" >&2; exit 1; }
    mkdir -p "$PREFIX/include/xaacenc_drm" "$PREFIX/share/doc/libxaac/patches"
    cp "$src/encoder/ixheaace_api.h" "$src/common/ixheaac_type_def.h" \
       "$src/common/ixheaac_error_standards.h" "$PREFIX/include/xaacenc_drm/"
    cp "$src/LICENSE" "$src/NOTICE" "$PREFIX/share/doc/libxaac/"
    rm -f "$PREFIX"/share/doc/libxaac/patches/*
    # shellcheck disable=SC2086
    [ -z "$xaac_patches" ] || cp $xaac_patches "$PREFIX/share/doc/libxaac/patches/"
    echo "$XAAC_URL tag $XAAC_TAG commit $XAAC_COMMIT tree $XAAC_TREE" > "$PREFIX/share/doc/libxaac/SOURCE"
    echo "$want" > "$PREFIX/lib/libxaacenc_drm.dream-stamp"
fi

echo "== codec libraries in $PREFIX/lib:"
ls -l "$PREFIX/lib" | grep '\.so'
