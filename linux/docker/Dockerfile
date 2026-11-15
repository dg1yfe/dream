# DREAM Linux build environment (Qt 6 + CMake), Debian 13 "trixie".
#
# Toolchain and system libraries only; the DREAM sources are bind-mounted at
# /src and built with linux/build.sh (see linux/README.md):
#
#   linux/docker/run.sh                    # native arch (arm64 on Apple Silicon)
#   linux/docker/run.sh --platform linux/amd64
#
# Debian 13 ships no Qwt built for Qt 6, so Qwt 6.3.0 is built from the pinned
# upstream tarball (same URL/sha256 as Homebrew's qwt formula) into /opt/qwt.

# debian:trixie as of 2026-11 (pinned by digest for reproducibility; the tag is informational)
FROM debian:trixie@sha256:34cd9e9fd437c0a095ec39cb2e73422c9f30821b0d0848ed74fd0d43bae4d958

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build pkgconf git curl ca-certificates patch file \
        bzip2 xz-utils python3 dpkg-dev \
        qt6-base-dev qt6-base-dev-tools qt6-svg-dev qmake6 libqt6opengl6-dev libgl-dev \
        libfftw3-dev libspeexdsp-dev zlib1g-dev libpcap-dev \
        portaudio19-dev libasound2-dev libpulse-dev libjack-jackd2-dev \
        libsndfile1-dev libopus-dev libhamlib-dev libgps-dev libsoapysdr-dev \
        ffmpeg espeak-ng \
    && rm -rf /var/lib/apt/lists/*

# Qwt 6.3.0 for Qt 6 (qmake build of the library only: no designer plugin, examples or tests)
ARG QWT_VERSION=6.3.0
ARG QWT_SHA256=dcb085896c28aaec5518cbc08c0ee2b4e60ada7ac929d82639f6189851a6129a
RUN set -eux; \
    cd /tmp; \
    curl -fsSL -o qwt.tar.bz2 "https://downloads.sourceforge.net/project/qwt/qwt/${QWT_VERSION}/qwt-${QWT_VERSION}.tar.bz2"; \
    echo "${QWT_SHA256}  qwt.tar.bz2" | sha256sum -c -; \
    tar -xjf qwt.tar.bz2; \
    cd "qwt-${QWT_VERSION}"; \
    sed -i -e 's|^\( *QWT_INSTALL_PREFIX *=\).*|\1 /opt/qwt|' \
           -e 's|^QWT_CONFIG *+= *QwtDesigner|#&|' \
           -e 's|^QWT_CONFIG *+= *QwtExamples|#&|' \
           -e 's|^QWT_CONFIG *+= *QwtPlayground|#&|' \
           -e 's|^QWT_CONFIG *+= *QwtTests|#&|' \
           -e 's|^QWT_CONFIG *+= *QwtOpenGL|#&|' qwtconfig.pri; \
    grep -E '^ *(QWT_INSTALL_PREFIX|QWT_CONFIG)' qwtconfig.pri; \
    qmake6 qwt.pro; \
    make -j"$(nproc)"; \
    make install; \
    cp COPYING /opt/qwt/COPYING; \
    echo /opt/qwt/lib > /etc/ld.so.conf.d/qwt.conf; ldconfig; \
    cd /; rm -rf /tmp/qwt*

# tests/loopback_check.py needs numpy for the tone/SNR checks (else presence check only)
RUN apt-get update && apt-get install -y --no-install-recommends python3-numpy \
    && rm -rf /var/lib/apt/lists/*

ENV CMAKE_PREFIX_PATH=/opt/qwt LANG=C.UTF-8
WORKDIR /src
