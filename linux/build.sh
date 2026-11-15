#!/bin/sh
# One-command Linux build of DREAM (Qt 6 GUI + console, CMake), meant to run inside the
# container of linux/docker/Dockerfile (Debian 13) but works on any distribution with the
# same packages (see linux/README.md).
#
#   linux/build.sh              codec libs (.deps/linux-<arch>) + GUI dream + dream-console
#   linux/build.sh --test       ... then tests/loopback.sh (DREAM_NO_FDK=1) and a GUI smoke test
#   linux/build.sh --package    ... then dist/dream-linux-<arch>.tar.gz
#   linux/build.sh --deb        ... then dist/dream_<version>_<debarch>.deb (Debian 13 / Ubuntu 24.04+)
#
# Env: BUILD_DIR (default build/linux-<arch>), BUILD_TYPE (Release), JOBS (nproc),
#      CMAKE_ARGS (extra configure args), LOOPBACK_SECONDS / LOOPBACK_CASES (tests),
#      DEB_MAINTAINER (Maintainer field of the .deb).
set -eu

REPO=$(cd "$(dirname "$0")/.." && pwd)
ARCH=$(uname -m)
BUILD_DIR=${BUILD_DIR:-$REPO/build/linux-$ARCH}
BUILD_TYPE=${BUILD_TYPE:-Release}
JOBS=${JOBS:-$(nproc)}
PREFIX=$REPO/.deps/linux-$ARCH
DIST=$REPO/dist

do_test=0; do_pkg=0; do_deb=0
for a in "$@"; do
    case "$a" in
        --test) do_test=1 ;;
        --package) do_pkg=1 ;;
        --deb) do_deb=1 ;;
        -h|--help) sed -n '2,14p' "$0"; exit 0 ;;
        *) echo "unknown option: $a" >&2; exit 2 ;;
    esac
done

# the bind-mounted checkout belongs to another uid inside the container
export GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.directory GIT_CONFIG_VALUE_0='*'

t0=$(date +%s)
# 1. runtime-loaded DRM codec libraries (patched faad2, faac 1.28, libxaac); no fdk-aac
"$REPO/linux/build-deps.sh" "$PREFIX"
t1=$(date +%s)

# 2. DREAM: GUI (dream) and console (dream-console) in one tree
# shellcheck disable=SC2086
cmake -S "$REPO" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DDREAM_BUILD_CONSOLE=ON \
    -DDREAM_WITH_FDK_AAC=OFF \
    ${CMAKE_ARGS:-}
cmake --build "$BUILD_DIR" -j "$JOBS"
t2=$(date +%s)
echo "== built $BUILD_DIR/dream and $BUILD_DIR/dream-console (deps $((t1 - t0)) s, dream $((t2 - t1)) s)"
"$BUILD_DIR/dream-console" --help 2>&1 | head -1

# 3. tests
if [ $do_test = 1 ]; then
    echo "== GUI smoke test (offscreen)"
    QT_QPA_PLATFORM=offscreen "$BUILD_DIR/dream" --console --help > "$BUILD_DIR/gui-smoke.log" 2>&1 ||
        { cat "$BUILD_DIR/gui-smoke.log"; echo "error: GUI binary failed" >&2; exit 1; }
    grep -m1 "^Usage" "$BUILD_DIR/gui-smoke.log" || { cat "$BUILD_DIR/gui-smoke.log"; exit 1; }
    echo "== loopback (console, DREAM_NO_FDK=1)"
    DREAM_NO_FDK=1 DREAM_BIN="$BUILD_DIR/dream-console" \
        LOOPBACK_WORKDIR="${LOOPBACK_WORKDIR:-$BUILD_DIR/loopback}" "$REPO/tests/loopback.sh"
fi

# install tree under $1 with prefix $2: bin/, lib/dream/ (codec .so + Qwt), share/doc/dream/
stage_into() {
    root=$1 pfx=$2
    rm -rf "$root"
    DESTDIR=$root cmake --install "$BUILD_DIR" --prefix "$pfx" > /dev/null
    [ "$pfx" = / ] && pfx=""
    # Qwt for Qt 6 is not packaged by Debian 13 / Ubuntu 24.04: ship the one we built (LGPL)
    qwt=$(ldd "$BUILD_DIR/dream" | sed -n 's/.*libqwt[^ ]* => \([^ ]*\).*/\1/p' | head -1)
    if [ -n "$qwt" ] && [ "${qwt#/usr/lib}" = "$qwt" ]; then
        cp -L "$qwt" "$root$pfx/lib/dream/$(basename "$qwt")"
        mkdir -p "$root$pfx/share/doc/dream/licenses/qwt"
        cp "$(dirname "$qwt")/../COPYING" "$root$pfx/share/doc/dream/licenses/qwt/COPYING" 2>/dev/null || true
        echo "https://downloads.sourceforge.net/project/qwt/qwt/6.3.0/qwt-6.3.0.tar.bz2 sha256 dcb085896c28aaec5518cbc08c0ee2b4e60ada7ac929d82639f6189851a6129a (built by linux/docker/Dockerfile)" \
            > "$root$pfx/share/doc/dream/licenses/qwt/SOURCE"
    fi
    cp "$REPO/linux/README.md" "$root$pfx/share/doc/dream/README-linux.md"
    # never fdk-aac (licence); binaries may only use the $ORIGIN-relative RUNPATH
    ! find "$root" -name '*fdk*' | grep . || { echo "error: fdk-aac in package" >&2; exit 1; }
    for b in "$root$pfx"/bin/*; do
        rp=$(readelf -d "$b" | sed -n 's/.*R[UN]*PATH.*\[\(.*\)\]/\1/p')
        [ "$rp" = '$ORIGIN/../lib/dream' ] || { echo "error: $b RUNPATH '$rp'" >&2; exit 1; }
    done
}
mkdir -p "$DIST"

# 4. tarball
if [ $do_pkg = 1 ]; then
    name=dream-linux-$ARCH
    stage_into "$BUILD_DIR/stage/$name" /
    cp "$REPO/linux/README.md" "$BUILD_DIR/stage/$name/README-linux.md"
    tar -C "$BUILD_DIR/stage" -czf "$DIST/$name.tar.gz" "$name"
    (cd "$DIST" && sha256sum "$name.tar.gz" > "$name.tar.gz.sha256")
    echo "== $DIST/$name.tar.gz ($(du -h "$DIST/$name.tar.gz" | cut -f1))"
fi

# 5. .deb (unofficial; built from the same install tree - the old debian/ dir is Qt5/qmake)
if [ $do_deb = 1 ]; then
    deb=$BUILD_DIR/deb
    rm -rf "$deb"
    stage_into "$deb/root" /usr
    darch=$(dpkg --print-architecture)
    ver=$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$BUILD_DIR/CMakeCache.txt")
    rev=${DREAM_GIT_REV:-$(git -C "$REPO" rev-parse --short HEAD 2>/dev/null || echo unknown)}
    version="$ver+git$rev"
    mkdir -p "$deb/debian" "$deb/root/DEBIAN"
    printf 'Source: dream\n\nPackage: dream\nArchitecture: any\n' > "$deb/debian/control"
    # dependencies of the binaries and of the private libs in /usr/lib/dream (Qwt needs QtSvg)
    deps=$(cd "$deb" && dpkg-shlibdeps -O --ignore-missing-info -l"$deb/root/usr/lib/dream" \
            $(for f in root/usr/bin/* root/usr/lib/dream/*.so*; do printf -- '-e%s ' "$f"; done) \
            2>/dev/null | sed -n 's/^shlibs:Depends=//p')
    size=$(du -sk "$deb/root/usr" | cut -f1)
    {
        echo "Package: dream"
        echo "Version: $version"
        echo "Architecture: $darch"
        echo "Maintainer: ${DEB_MAINTAINER:-DREAM build <dream@localhost>}"
        echo "Installed-Size: $size"
        echo "Depends: $deps"
        echo "Suggests: libfdk-aac2"
        echo "Section: hamradio"
        echo "Priority: optional"
        echo "Homepage: https://sourceforge.net/p/drm"
        echo "Description: software receiver and transmitter for Digital Radio Mondiale (DRM) and AM"
        echo " Dream decodes DRM, AM and FM broadcasts from a sound card or SDR input and"
        echo " can generate DRM signals. Unofficial build: Qt 6 GUI and console binaries,"
        echo " runtime-loaded DRM AAC codecs (patched faad2, faac 1.28, libxaac) and Qwt 6.3"
        echo " in /usr/lib/dream. fdk-aac is not included; install libfdk-aac2 to use it."
        echo " Distributed as a combined work under GPL-3.0 (/usr/share/doc/dream/licenses)."
    } > "$deb/root/DEBIAN/control"
    out="$DIST/dream_${version}_$darch.deb"
    dpkg-deb --root-owner-group -Zxz --build "$deb/root" "$out" > /dev/null
    echo "== $out ($(du -h "$out" | cut -f1)); Depends: $deps"
fi
echo "== total $(( $(date +%s) - t0 )) s"
