#!/bin/sh
# One-command macOS build of DREAM (Qt6, CMake, Homebrew). Apple Silicon and Intel.
#
#   macx/build.sh                 GUI app  -> build/gui/Dream.app
#   macx/build.sh --console       console-only binary (no Qt) -> build/console/dream
#   macx/build.sh --deps-only     only fetch/build the non-Homebrew deps
#   macx/build.sh --brew          also 'brew install' missing Homebrew packages
#   macx/build.sh --deploy        after building, make a self-contained Dream.app + dmg (macx/deploy.sh)
#   macx/build.sh --deploy --with-fdk   ...and bundle libfdk-aac (private use only: FDK licence)
#
# Env: BUILD_DIR (default build/gui or build/console), BUILD_TYPE (Release),
#      ARCH (uname -m), JOBS (sysctl hw.ncpu), PREFIX for local deps (.deps/$ARCH),
#      CMAKE_ARGS (extra args passed to cmake configure).
#
# Local (non-Homebrew) deps, built into .deps/$ARCH (rebuilt when their patches change):
#   fdk-aac 2.0.3 + macx/patches/fdk-aac-2.0.3-drm-encoder.patch – AAC/HE-AAC/xHE-AAC decoder
#                   and DRM AAC/HE-AAC encoder (Homebrew formula disabled 2026-09)
#   libxaacenc_drm – libxaac (Apache-2.0) + macx/patches/libxaac-*.patch: DRM AAC/HE-AAC/
#                   HE-AACv2 encoder (macx/build-xaac.sh), dlopen()ed; used when fdk-aac has none
#   libfaac_drm   – fallback DRM AAC encoder (macx/build-faac-drm.sh), dlopen()ed
#   libfaad_drm   – patched faad2 2.11.4, fallback DRM AAC decoder (macx/build-faad-drm.sh), dlopen()ed
set -eu

REPO=$(cd "$(dirname "$0")/.." && pwd)
ARCH=${ARCH:-$(uname -m)}
PREFIX=${PREFIX:-$REPO/.deps/$ARCH}
BUILD_TYPE=${BUILD_TYPE:-Release}
JOBS=${JOBS:-$(sysctl -n hw.ncpu)}
# Local deps are built for an old macOS; the app itself targets the host's
# macOS (Homebrew bottles are built for it anyway), so this is not exported.
DEPS_DEPLOYMENT_TARGET=${DEPS_DEPLOYMENT_TARGET:-11.0}

FDK_TAG=v2.0.3
FDK_COMMIT=716f4394641d53f0d79c9ddac3fa93b03a49f278
FDK_URL=https://github.com/mstorsjo/fdk-aac.git

console=0; deps_only=0; do_brew=0; do_deploy=0; with_fdk=0
for a in "$@"; do
    case "$a" in
        --console) console=1 ;;
        --deps-only) deps_only=1 ;;
        --brew) do_brew=1 ;;
        --deploy) do_deploy=1 ;;
        --with-fdk) with_fdk=1 ;;
        -h|--help) sed -n '2,19p' "$0"; exit 0 ;;
        *) echo "unknown option: $a" >&2; exit 2 ;;
    esac
done

command -v brew >/dev/null 2>&1 || { echo "error: Homebrew (brew) is required" >&2; exit 1; }
BREW_PREFIX=$(brew --prefix)

# ---------------------------------------------------------------- Homebrew deps
BREW_TOOLS="cmake ninja pkgconf"
BREW_LIBS="fftw speexdsp portaudio libsndfile opus hamlib gpsd soapysdr"
BREW_QT="qtbase qtsvg qwt"
want="$BREW_TOOLS $BREW_LIBS"
[ $console = 1 ] || want="$want $BREW_QT"
missing=""
for f in $want; do
    [ -d "$BREW_PREFIX/opt/$f" ] || missing="$missing $f"
done
if [ -n "$missing" ]; then
    if [ $do_brew = 1 ]; then
        # shellcheck disable=SC2086
        brew install $missing
    else
        echo "error: missing Homebrew packages:$missing" >&2
        echo "       run: brew install$missing   (or re-run with --brew)" >&2
        exit 1
    fi
fi

# ---------------------------------------------------------------- fdk-aac
# Stock fdk-aac 2.0.3 + macx/patches/fdk-aac-2.0.3-drm-encoder.patch (DRM AAC/HE-AAC
# encoder: TT_DRM, 960-sample granule, DRM CRC). The git checkout in .deps/src/fdk-aac
# is only a cache: the pristine commit is exported to a scratch tree and patched
# there, so a hand-edited checkout never leaks into the build. A stamp file with the
# patch's sha256 makes this idempotent: an unpatched or older-patch install is rebuilt.
FDK_PATCH=$REPO/macx/patches/fdk-aac-2.0.3-drm-encoder.patch
FDK_STAMP=$PREFIX/lib/fdk-aac.dream-patch
if [ -f "$FDK_PATCH" ]; then
    fdk_want=$(shasum -a 256 "$FDK_PATCH" | cut -d' ' -f1)
else
    fdk_want=none
fi
fdk_have=$(cat "$FDK_STAMP" 2>/dev/null || echo missing)
if [ ! -f "$PREFIX/lib/pkgconfig/fdk-aac.pc" ] || [ "$fdk_have" != "$fdk_want" ]; then
    echo "== building fdk-aac $FDK_TAG (DRM patch: $fdk_want) into $PREFIX"
    cache=$REPO/.deps/src/fdk-aac
    if [ ! -d "$cache/.git" ]; then
        mkdir -p "$REPO/.deps/src"
        git clone --quiet --depth 1 --branch "$FDK_TAG" "$FDK_URL" "$cache"
    fi
    git -C "$cache" cat-file -e "$FDK_COMMIT^{commit}" 2>/dev/null ||
        git -C "$cache" fetch --quiet --depth 1 origin "refs/tags/$FDK_TAG:refs/tags/$FDK_TAG"
    got=$(git -C "$cache" rev-parse "$FDK_TAG^{commit}")
    if [ "$got" != "$FDK_COMMIT" ]; then
        echo "error: tag $FDK_TAG in $cache is $got, expected $FDK_COMMIT" >&2
        exit 1
    fi
    src=$REPO/.deps/build/fdk-aac-src-$ARCH
    bld=$REPO/.deps/build/fdk-aac-$ARCH
    rm -rf "$src" "$bld"
    mkdir -p "$src"
    git -C "$cache" archive "$FDK_COMMIT" | tar -x -C "$src"
    if [ -f "$FDK_PATCH" ]; then
        (cd "$src" && patch -p1 --forward --quiet < "$FDK_PATCH")
    else
        echo "warning: $FDK_PATCH not found - fdk-aac will have no DRM encoder" >&2
    fi
    # remove an older install so no stale headers/libs survive
    rm -f "$PREFIX"/lib/libfdk-aac.* "$PREFIX/lib/pkgconfig/fdk-aac.pc" "$FDK_STAMP"
    rm -rf "$PREFIX/include/fdk-aac" "$PREFIX/lib/cmake/fdk-aac"
    MACOSX_DEPLOYMENT_TARGET=$DEPS_DEPLOYMENT_TARGET cmake -S "$src" -B "$bld" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="$ARCH" \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_INSTALL_NAME_DIR=@rpath \
        -DBUILD_SHARED_LIBS=ON -DBUILD_PROGRAMS=OFF
    cmake --build "$bld" -j "$JOBS"
    cmake --install "$bld"
    echo "$fdk_want" > "$FDK_STAMP"
fi

# ---------------------------------------------------------------- libxaac (DRM encoder)
# libxaacenc_drm.dylib is dlopen()ed by XaacCodec (DRM encoder after fdk-aac, before
# faac). Rebuilt when the script or its patches change (stamp = sha256 over both).
if [ -x "$REPO/macx/build-xaac.sh" ]; then
    XAAC_STAMP=$PREFIX/lib/libxaacenc_drm.dream-build
    xaac_want=$(cat "$REPO/macx/build-xaac.sh" "$REPO"/macx/patches/libxaac-*.patch 2>/dev/null |
                shasum -a 256 | cut -d' ' -f1)
    xaac_have=$(cat "$XAAC_STAMP" 2>/dev/null || echo missing)
    if [ ! -f "$PREFIX/lib/libxaacenc_drm.dylib" ] || [ "$xaac_have" != "$xaac_want" ]; then
        echo "== building libxaacenc_drm (patched libxaac) into $PREFIX"
        ARCH=$ARCH MACOSX_DEPLOYMENT_TARGET=$DEPS_DEPLOYMENT_TARGET "$REPO/macx/build-xaac.sh" "$PREFIX"
        echo "$xaac_want" > "$XAAC_STAMP"
    fi
fi

# ---------------------------------------------------------------- faac (DRM encoder)
# libfaac_drm.dylib is dlopen()ed by AacCodec (fallback DRM encoder). Rebuilt when
# the script or its patches change (stamp = sha256 over both).
if [ -x "$REPO/macx/build-faac-drm.sh" ]; then
    FAAC_STAMP=$PREFIX/lib/libfaac_drm.dream-stamp
    faac_want=$(cat "$REPO/macx/build-faac-drm.sh" "$REPO"/macx/patches/faac-*.patch 2>/dev/null |
                shasum -a 256 | cut -d' ' -f1)
    faac_have=$(cat "$FAAC_STAMP" 2>/dev/null || echo missing)
    if [ ! -f "$PREFIX/lib/libfaac_drm.dylib" ] || [ "$faac_have" != "$faac_want" ]; then
        echo "== building libfaac_drm into $PREFIX"
        ARCH=$ARCH MACOSX_DEPLOYMENT_TARGET=$DEPS_DEPLOYMENT_TARGET "$REPO/macx/build-faac-drm.sh" "$PREFIX"
        echo "$faac_want" > "$FAAC_STAMP"
    fi
fi

# libfaad_drm.dylib: faad2 2.11.4 + macx/patches/faad2-2.11.4-*.patch (DRM HCR short-block
# order, MPEG-4 PS, marker NeAACDecDreamDrmFixes). dlopen()ed DRM AAC decoder used when
# fdk-aac is absent. Rebuilt when the script or its patches change.
if [ -x "$REPO/macx/build-faad-drm.sh" ]; then
    FAAD_STAMP=$PREFIX/lib/libfaad_drm.dream-stamp
    faad_want=$(cat "$REPO/macx/build-faad-drm.sh" "$REPO"/macx/patches/faad2-*.patch 2>/dev/null |
                shasum -a 256 | cut -d' ' -f1)
    faad_have=$(cat "$FAAD_STAMP" 2>/dev/null || echo missing)
    if [ ! -f "$PREFIX/lib/libfaad_drm.dylib" ] || [ "$faad_have" != "$faad_want" ]; then
        echo "== building libfaad_drm (patched faad2) into $PREFIX"
        ARCH=$ARCH MACOSX_DEPLOYMENT_TARGET=$DEPS_DEPLOYMENT_TARGET "$REPO/macx/build-faad-drm.sh" "$PREFIX"
        echo "$faad_want" > "$FAAD_STAMP"
    fi
fi

[ $deps_only = 1 ] && exit 0

# ---------------------------------------------------------------- configure + build
if [ $console = 1 ]; then
    BUILD_DIR=${BUILD_DIR:-$REPO/build/console}
    variant="-DDREAM_CONSOLE_ONLY=ON"
else
    BUILD_DIR=${BUILD_DIR:-$REPO/build/gui}
    variant=""
fi

PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export PKG_CONFIG_PATH

# shellcheck disable=SC2086
cmake -S "$REPO" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_OSX_ARCHITECTURES="$ARCH" \
    -DCMAKE_PREFIX_PATH="$PREFIX;$BREW_PREFIX" \
    -DDREAM_REQUIRE_FDK_AAC=ON \
    $variant ${CMAKE_ARGS:-}
cmake --build "$BUILD_DIR" -j "$JOBS"

if [ $console = 1 ]; then
    echo "== built $BUILD_DIR/dream"
    "$BUILD_DIR/dream" --help 2>&1 | head -1
else
    echo "== built $BUILD_DIR/Dream.app"
    # smoke test: loads every dylib (catches rpath/dyld problems) and prints usage
    "$BUILD_DIR/Dream.app/Contents/MacOS/Dream" --console --help 2>&1 | head -1
    if [ $do_deploy = 1 ]; then
        if [ $with_fdk = 1 ]; then
            PREFIX=$PREFIX "$REPO/macx/deploy.sh" --with-fdk "$BUILD_DIR"
        else
            PREFIX=$PREFIX "$REPO/macx/deploy.sh" "$BUILD_DIR"
        fi
    fi
    echo "run:  open $BUILD_DIR/Dream.app    (or $BUILD_DIR/Dream.app/Contents/MacOS/Dream for log output)"
fi
