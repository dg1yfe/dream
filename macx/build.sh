#!/bin/sh
# One-command macOS build of DREAM (Qt6, CMake, Homebrew). Apple Silicon and Intel.
#
#   macx/build.sh                 GUI app  -> build/gui/Dream.app
#   macx/build.sh --console       console-only binary (no Qt) -> build/console/dream
#   macx/build.sh --deps-only     only fetch/build the non-Homebrew deps
#   macx/build.sh --brew          also 'brew install' missing Homebrew packages
#   macx/build.sh --deploy        after building, make a self-contained Dream.app + dmg (macx/deploy.sh)
#
# Env: BUILD_DIR (default build/gui or build/console), BUILD_TYPE (Release),
#      ARCH (uname -m), JOBS (sysctl hw.ncpu), PREFIX for local deps (.deps/$ARCH),
#      CMAKE_ARGS (extra args passed to cmake configure).
#
# Local (non-Homebrew) deps, built once into .deps/$ARCH:
#   fdk-aac 2.0.3  – AAC/HE-AAC/xHE-AAC decoder (Homebrew formula disabled 2026-09)
#   faac 1.31.1 DRM – libfaac_drm.dylib, the DRM AAC encoder (macx/build-faac-drm.sh), dlopen()ed
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

console=0; deps_only=0; do_brew=0; do_deploy=0
for a in "$@"; do
    case "$a" in
        --console) console=1 ;;
        --deps-only) deps_only=1 ;;
        --brew) do_brew=1 ;;
        --deploy) do_deploy=1 ;;
        -h|--help) sed -n '2,17p' "$0"; exit 0 ;;
        *) echo "unknown option: $a" >&2; exit 2 ;;
    esac
done

command -v brew >/dev/null 2>&1 || { echo "error: Homebrew (brew) is required" >&2; exit 1; }
BREW_PREFIX=$(brew --prefix)

# ---------------------------------------------------------------- Homebrew deps
BREW_TOOLS="cmake ninja pkgconf"
BREW_LIBS="fftw speexdsp portaudio libsndfile opus hamlib gpsd soapysdr faad2"
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
if [ ! -f "$PREFIX/lib/pkgconfig/fdk-aac.pc" ]; then
    echo "== building fdk-aac $FDK_TAG into $PREFIX"
    src=$REPO/.deps/src/fdk-aac
    if [ ! -d "$src/.git" ]; then
        mkdir -p "$REPO/.deps/src"
        git clone --quiet --depth 1 --branch "$FDK_TAG" "$FDK_URL" "$src"
    fi
    got=$(git -C "$src" rev-parse HEAD)
    if [ "$got" != "$FDK_COMMIT" ]; then
        echo "error: $src is at $got, expected $FDK_COMMIT ($FDK_TAG)" >&2
        exit 1
    fi
    MACOSX_DEPLOYMENT_TARGET=$DEPS_DEPLOYMENT_TARGET cmake -S "$src" -B "$REPO/.deps/build/fdk-aac-$ARCH" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="$ARCH" \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_INSTALL_NAME_DIR=@rpath \
        -DBUILD_SHARED_LIBS=ON -DBUILD_PROGRAMS=OFF
    cmake --build "$REPO/.deps/build/fdk-aac-$ARCH" -j "$JOBS"
    cmake --install "$REPO/.deps/build/fdk-aac-$ARCH"
fi

# ---------------------------------------------------------------- faac (DRM encoder)
if [ ! -f "$PREFIX/lib/libfaac_drm.dylib" ] && [ -x "$REPO/macx/build-faac-drm.sh" ]; then
    echo "== building libfaac_drm into $PREFIX"
    ARCH=$ARCH MACOSX_DEPLOYMENT_TARGET=$DEPS_DEPLOYMENT_TARGET "$REPO/macx/build-faac-drm.sh" "$PREFIX"
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
        PREFIX=$PREFIX "$REPO/macx/deploy.sh" "$BUILD_DIR"
    fi
    echo "run:  open $BUILD_DIR/Dream.app    (or $BUILD_DIR/Dream.app/Contents/MacOS/Dream for log output)"
fi
