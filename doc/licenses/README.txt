DREAM - licences of a binary distribution
=========================================

DREAM itself is free software under the GNU General Public License version 2
or (at your option) any later version (COPYING). Binary distributions that
include the libxaac DRM encoder (Apache-2.0, not compatible with GPLv2-only) are
distributed as a combined work under the GNU General Public License version 3
(GPL-3.0.txt).

Texts in this directory
  GPL-3.0.txt              the combined work; GPL-2.0.txt: DREAM, faad2, fftw, Journaline
  LGPL-2.1.txt             faac (libfaac_drm), Qwt (with the Qwt exception, see qwt/COPYING),
                           libsndfile, hamlib
  LGPL-3.0.txt             Qt 6
  Apache-2.0.txt           libxaac (see libxaac/LICENSE and libxaac/NOTICE)
  Opus-BSD-3-Clause.txt    Opus-derived code compiled into DREAM (CELT concealment port)
  PortAudio-MIT.txt        PortAudio ring buffer compiled into DREAM

Runtime-loaded codec libraries shipped with a binary distribution, each with
its licence text, the DREAM patches applied to it and the pinned upstream source
(SOURCE): faad2/ (libfaad_drm, GPL-2.0-or-later), faac/ (libfaac_drm, LGPL; keep
the ISO/IEC notice of faac/README), libxaac/ (libxaacenc_drm, Apache-2.0 + NOTICE).
The patches are also in the DREAM source tree (macx/patches/), and the build
scripts that reproduce the libraries are macx/build-*.sh and linux/build-deps.sh.

fdk-aac is NOT distributed with DREAM (its licence is not GPL compatible); DREAM
loads a user-installed libfdk-aac at run time if present.

Libraries provided by the operating system / distribution packages (Qt, fftw,
speexdsp, opus, portaudio, libsndfile, hamlib, gpsd, SoapySDR, zlib, libpcap,
ALSA, PulseAudio, JACK) keep their licences with those packages.

Source code offer
  The complete corresponding source of DREAM, of the patched codec libraries
  (pinned upstream source + patches) and of any LGPL library shipped with a
  binary distribution (e.g. Qwt 6.3.0 in the Linux tarball) is available from the
  DREAM project together with the binaries, or on request for at least three
  years from the date of distribution.
