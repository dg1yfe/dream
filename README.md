# Dream
[![Build for Linux](https://github.com/Drm-tools/dream/actions/workflows/build_linux.yaml/badge.svg)](https://github.com/Drm-tools/dream/actions/workflows/build_linux.yaml)
[![Build for Windows](https://github.com/Drm-tools/dream/actions/workflows/build_windows.yaml/badge.svg)](https://github.com/Drm-tools/dream/actions/workflows/build_windows.yaml)

 * Parts (c) 2001-2003 Technische Universität Darmstadt, Institut für Fachbereich Übertragungstechnik
 * Parts Copyright (c) 2003, 2004 Fraunhofer IIS, Erlangen, Germany
 * Parts Copyright (c) 2004 - 2009 British Broadcasting Corporation
 
Originally written by Volker Fischer and Alexander Kurpiers
 
Additional contributions by Stephane Fillod, Andrea Russo, Julian Cable, Mark J Fine, David Flamand, Andrew Murphy, Oliver Haffenden, Tomi Manninen, Robert Kesterson
   
Introduction
------------

Digital Radio Mondiale (DRM) is the digital radio standard for the long-,
medium- and short-wave bands. The system is defined by ES 201 980 which
is freely downloadable from the European Telecommunications Standards Insitute
(ETSI). The system was designed by a consortium in co-operation with the 
International Telecommunication Union (ITU). It offers radio stations and
new service providers access to the multimedia age with small bit rates for 
large target areas and long distances.

For more information, visit www.drm.org. 


The bandwidth of a DRM bandpass signal is less than 20 kHz and the number of 
carriers used in the OFDM-modulation is relatively small (max. 460). These
features motivate a real-time software implementation of a DRM-receiver on a 
conventional personal computer (PC) using the sound card as the input and output 
device. A long, medium and short wave front-end with an intermediate frequency 
(IF) between 5 kHz and 15 kHz is used to receive the DRM signal. Any commercial 
front-end with an IF of 455 kHz should be usable by adding a 455 kHz to 12 kHz 
adaptor (asumed the receiver bandwith is sufficient for a DRM signal). 
Documentation of receiver modifications for DRM reception can be found at 
www.drmrx.org. 


With this software project we intend to implement a working software receiver 
with, at least, the basic features. Since this project is created at a university 
and the fundamental idea of such an institution is to teach and stimulate the 
creativity, this source-code is free under the GNU-General Public License (GPL). 
Dream is a development project which uses the open source model to improve DRM 
technology. The main aim of this project is to implement and test new research 
results on an existing system, whereby the synchronization and channel 
estimation is of special interest. 


The programming-language is C++. Start of the project was June 2001. The
software runs on Microsoft Windows, Linux and OSX


Although this software is distributed as free software under the terms of the
GPL this does not mean that its use is free of rights of others. The use may
infringe third party IP and thus may not be legal in some countries.


The intended audience of the Dream software are people who are interested in
how to decode a DRM stream and want to learn from the algorithms used in this
software and people who want to help us improving the performance of the receiver
and the source code.


Building
--------

Dream builds with CMake and Qt 6 (GUI) or without Qt (console). The qmake
project `dream.pro` (Qt 5) is still in the tree for the existing Linux/Windows
CI jobs.

### macOS (Apple Silicon or Intel, Homebrew)

    brew install cmake ninja pkgconf qtbase qtsvg qwt \
                 fftw speexdsp portaudio libsndfile opus hamlib gpsd soapysdr faad2
    macx/build.sh                # -> build/gui/Dream.app
    open build/gui/Dream.app

`macx/build.sh` fetches and builds the two codecs Homebrew does not provide into
`.deps/<arch>` (once): **fdk-aac 2.0.3** (AAC/HE-AAC/xHE-AAC decoder) and a
DRM-capable **libfaac_drm** (the DRM AAC encoder, `macx/build-faac-drm.sh`).
Other options: `--console` (no-Qt binary in `build/console/dream`), `--brew`
(install missing Homebrew packages), `--deploy` (self-contained, ad-hoc signed
`Dream.app` + `Dream.dmg` via `macx/deploy.sh`). Sound I/O uses PortAudio
(CoreAudio). On first use of a sound card input macOS asks for microphone access.

### Generic CMake

    cmake -S . -B build/gui -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build/gui

Required: Qt 6 (Core Gui Widgets Network Xml Svg), Qwt for Qt 6, fftw3, speexdsp,
zlib, libpcap and one sound backend (PortAudio, PulseAudio or ALSA). Optional and
auto-detected via pkg-config: fdk-aac, opus, libsndfile, hamlib, libgps, SoapySDR.
Point `CMAKE_PREFIX_PATH` (or `PKG_CONFIG_PATH`) at any custom prefix.

| option | default | meaning |
|---|---|---|
| `DREAM_CONSOLE_ONLY` | OFF | build only the console binary, no Qt needed |
| `DREAM_BUILD_CONSOLE` | OFF | also build `dream-console` next to the GUI |
| `DREAM_WITH_FDK_AAC` / `DREAM_REQUIRE_FDK_AAC` | ON / OFF | use fdk-aac if found / fail if missing |
| `DREAM_WITH_USAC` | ON | xHE-AAC decoding (`HAVE_USAC`, needs fdk-aac) |
| `DREAM_WITH_BWS` | OFF | BWS web viewer (needs QtWebEngine) |
| `DREAM_WITH_{OPUS,SNDFILE,HAMLIB,GPS,SOAPYSDR,PORTAUDIO}` | ON | use library if found |
| `DREAM_WITH_{PULSEAUDIO,ALSA,JACK}` | Linux / OFF / OFF | other sound backends |
| `DREAM_HARDENING` | ON | compiler hardening flags |
