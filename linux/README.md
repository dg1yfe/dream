# DREAM on Linux

DREAM builds on Linux with CMake and Qt 6. The reference environment is the
Debian 13 ("trixie") container in `linux/docker/`. That container is the build
that is tested (arm64 natively, amd64 under emulation). Other distributions
with Qt ≥ 6.2 should work with the same packages.

## One command (Docker, any host)

    linux/docker/run.sh --test --package --deb                  # host architecture
    linux/docker/run.sh --platform linux/amd64 --test --package # x86_64 (emulated on Apple Silicon: slow)
    linux/docker/run.sh --shell                                 # interactive shell, checkout at /src

This builds the image (toolchain, Qt 6, Qwt 6.3.0 built from its pinned tarball), then runs
`linux/build.sh` inside it with the checkout bind-mounted:

| output | where |
|---|---|
| codec libraries (`libfaad_drm.so.2`, `libfaac_drm.so.0`, `libxaacenc_drm.so`) | `.deps/linux-<arch>/lib` |
| `dream` (GUI) and `dream-console` | `build/linux-<arch>/` |
| loopback test files (`--test`) | `build/linux-<arch>/loopback/` |
| tarball (`--package`) | `dist/dream-linux-<arch>.tar.gz` (+ `.sha256`) |
| Debian package (`--deb`) | `dist/dream_<version>+git<rev>_<arch>.deb` |

## Native build (Debian 13 / Ubuntu 24.04+)

    sudo apt install build-essential cmake ninja-build pkgconf git curl patch python3 \
        qt6-base-dev qt6-svg-dev qmake6 libqt6opengl6-dev libfftw3-dev libspeexdsp-dev zlib1g-dev \
        libpcap-dev portaudio19-dev libasound2-dev libpulse-dev libjack-jackd2-dev libsndfile1-dev \
        libopus-dev libhamlib-dev libgps-dev libsoapysdr-dev
    # Qwt for Qt 6 is not packaged by Debian 13 / Ubuntu 24.04: build 6.3.0 as in linux/docker/Dockerfile,
    # then pass its prefix:   CMAKE_PREFIX_PATH=/opt/qwt
    linux/build.sh --test

## Codec libraries (`linux/build-deps.sh`)

DREAM loads its DRM AAC codecs at run time (dlopen). `linux/build-deps.sh` builds them from
the same pinned sources and the same patch files (`macx/patches/<lib>-*.patch`, applied in
name order) as the macOS scripts:

- **libfaad_drm.so.2**: faad2 2.11.4, DRM fixes. Fallback AAC decoder. GPL-2.0-or-later.
- **libfaac_drm.so.0**: faac 1.28 with DRM. AAC-LC encoder. LGPL.
- **libxaacenc_drm.so**: libxaac android-16.0.0_r4. AAC/HE-AAC/HE-AACv2 DRM encoder. Apache-2.0.

fdk-aac is not built: its licence is not GPL compatible. DREAM uses a user-installed
`libfdk-aac.so.2` if one is present.

DREAM searches for the codec libraries in this order:

1. `$DREAM_LIB_PATH` (a colon-separated list)
2. `<dir of dream>/../lib/dream` (the tarball and install layout)
3. `.deps/linux-<arch>/lib` of the build tree
4. the normal `ld.so` search

The log says which file was loaded, for example `Got FAAD2 library ...` or
`xaac: DRM encoder opened`.

## Tarball

`dist/dream-linux-<arch>.tar.gz` unpacks to `dream-linux-<arch>/`:

- `bin/dream`, `bin/dream-console`
- `lib/dream/`: the codec libraries and `libqwt.so.6`. They are found through
  `RUNPATH $ORIGIN/../lib/dream`.
- `share/doc/dream/`: `COPYING` and `licenses/`. These cover GPL-3.0 (the combined work
  with libxaac), the per-library licence, NOTICE, applied patches and pinned SOURCE, the
  Opus BSD notice, PortAudio MIT, and LGPL/Apache texts.
- `share/man/man1/dream.1`

Qt 6 and the other system libraries come from the distribution. On Debian 13:

    sudo apt install libqt6widgets6 libqt6network6 libqt6xml6 libqt6svg6 libqt6opengl6 \
        libfftw3-double3 libspeexdsp1 libportaudio2 libpulse0 libsndfile1 libopus0 libhamlib4 \
        libgps30 libsoapysdr0.8 libpcap0.8

Run `bin/dream` (GUI) or `bin/dream-console -h`.

## Audio backends

| backend | default | sources | notes |
|---|---|---|---|
| PortAudio | on | `src/sound/drm_portaudio.cpp` | ALSA/Pulse/JACK through PortAudio |
| PulseAudio | on (Linux) | `src/sound/drm_pulseaudio.cpp` | |
| ALSA | `-DDREAM_WITH_ALSA=ON` | `src/linux/alsa*.cpp` | |
| JACK | `-DDREAM_WITH_JACK=ON` | `src/linux/jack.cpp` | |

Pass these options with `CMAKE_ARGS="-DDREAM_WITH_ALSA=ON ..." linux/build.sh`.

## Debian package

`linux/build.sh --deb` (or `linux/docker/run.sh --deb`) writes an unofficial package to
`dist/dream_<version>+git<rev>_<arch>.deb`. It is built from the same install tree as the
tarball, with the prefix `/usr`:

- Codec libraries and Qwt go to `/usr/lib/dream`, licences to `/usr/share/doc/dream/licenses`.
- `Depends` is computed by `dpkg-shlibdeps`.
- `Suggests: libfdk-aac2`.

It installs with `apt install ./dream_*.deb` on Debian 13.

The old `debian/` directory is not usable as it stands:

- It targets Qt 5/qmake.
- It has a hard `Depends: libfdk-aac2`. That must become `Suggests`.
- `debian/copyright` is still a template.
- It does not build the runtime codec libraries or Qwt for Qt 6.
