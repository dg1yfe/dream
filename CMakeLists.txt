# DREAM – DRM receiver/transmitter. CMake build (Qt6).
#
# Quick start (macOS/Homebrew):   macx/build.sh
# Generic:
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#
# Variants:
#   default                    GUI application (Qt6 + Qwt); Dream.app bundle on macOS
#   -DDREAM_CONSOLE_ONLY=ON    console-only binary, no Qt at all (dream.pro CONFIG+=console)
#   -DDREAM_BUILD_CONSOLE=ON   additionally build the console binary next to the GUI ("dream-console")
#
# fdk-aac (AAC/HE-AAC/xHE-AAC decoder, DRM encoder with the patch in macx/patches)
# is LOADED AT RUNTIME (dlopen), never linked: its licence is not GPL compatible.
# Only its headers are needed at build time (pkg-config / CMAKE_PREFIX_PATH; a
# prefix at <source>/.deps/<arch>, see macx/build.sh, is picked up automatically).
# Users add the library themselves (DREAM_LIB_PATH, Homebrew/system lib dirs, or
# Dream.app/Contents/Frameworks); without it DREAM falls back to faad/faac.

cmake_minimum_required(VERSION 3.21)
project(dream VERSION 2.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(DREAM_CONSOLE_ONLY   "Build only the console (no Qt) binary" OFF)
option(DREAM_BUILD_CONSOLE  "Also build the console (no Qt) binary next to the GUI" OFF)
option(DREAM_WITH_FDK_AAC   "Build runtime-loaded fdk-aac support if the fdk-aac headers are found" ON)
option(DREAM_REQUIRE_FDK_AAC "Fail configure if the fdk-aac headers are not found" OFF)
option(DREAM_WITH_USAC      "Enable xHE-AAC (USAC) decoding via fdk-aac (HAVE_USAC)" ON)
option(DREAM_WITH_OPUS      "Link libopus if found" ON)
option(DREAM_WITH_SNDFILE   "Use libsndfile if found" ON)
option(DREAM_WITH_HAMLIB    "Use hamlib (rig control) if found" ON)
option(DREAM_WITH_GPS       "Use libgps (gpsd) if found" ON)
option(DREAM_WITH_SOAPYSDR  "Use SoapySDR input if found" ON)
option(DREAM_WITH_PORTAUDIO "Use PortAudio sound I/O if found" ON)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_dream_is_linux ON)
else()
    set(_dream_is_linux OFF)
endif()
option(DREAM_WITH_PULSEAUDIO "Use PulseAudio sound I/O if found (Linux)" ${_dream_is_linux})
option(DREAM_WITH_ALSA      "Use ALSA sound I/O if found (Linux)" OFF)
option(DREAM_WITH_JACK      "Use JACK sound I/O if found" OFF)
option(DREAM_WITH_BWS       "Build the BWS (broadcast website) viewer with QtWebEngine" OFF)
option(DREAM_HARDENING      "Enable compiler hardening flags" ON)
option(DREAM_QT5COMPAT      "Link Qt6::Core5Compat (only needed for not-yet-ported Qt5 APIs such as QRegExp)" OFF)

include(sources)
include(hardening)
include(audiobackends)

# ---------------------------------------------------------------------------
# Dependency search paths (no hard-coded /usr/local or /opt/homebrew)
# ---------------------------------------------------------------------------
if(CMAKE_OSX_ARCHITECTURES)
    set(_dream_arch "${CMAKE_OSX_ARCHITECTURES}")
else()
    set(_dream_arch "${CMAKE_SYSTEM_PROCESSOR}")
endif()
if(IS_DIRECTORY "${CMAKE_SOURCE_DIR}/.deps/${_dream_arch}")
    # Locally built deps (e.g. fdk-aac from macx/build.sh)
    list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/.deps/${_dream_arch}")
endif()

# Directories searched at runtime: also used by dlopen() of leaf names
# (libfaad_drm / libfaac_drm / libopus) because dyld consults LC_RPATH for them.
set(DREAM_EXTRA_RPATHS "")
if(APPLE AND IS_DIRECTORY "${CMAKE_SOURCE_DIR}/.deps/${_dream_arch}/lib")
    list(APPEND DREAM_EXTRA_RPATHS "${CMAKE_SOURCE_DIR}/.deps/${_dream_arch}/lib")
endif()

if(APPLE)
    find_program(BREW_EXECUTABLE brew)
    if(BREW_EXECUTABLE AND NOT DREAM_BREW_PREFIX)
        execute_process(COMMAND "${BREW_EXECUTABLE}" --prefix
                        OUTPUT_VARIABLE _brew_prefix OUTPUT_STRIP_TRAILING_WHITESPACE
                        RESULT_VARIABLE _brew_rc ERROR_QUIET)
        if(_brew_rc EQUAL 0 AND IS_DIRECTORY "${_brew_prefix}")
            set(DREAM_BREW_PREFIX "${_brew_prefix}" CACHE PATH "Homebrew prefix")
        endif()
    endif()
    if(DREAM_BREW_PREFIX)
        list(APPEND CMAKE_PREFIX_PATH "${DREAM_BREW_PREFIX}")
        # Qwt (Qt6) is keg-style under opt/qwt
        if(IS_DIRECTORY "${DREAM_BREW_PREFIX}/opt/qwt")
            list(APPEND CMAKE_PREFIX_PATH "${DREAM_BREW_PREFIX}/opt/qwt")
        endif()
        # libfaad_drm / libfaac_drm / libopus are dlopen()ed by leaf name; dyld
        # searches the main executable's LC_RPATHs for leaf names, so give it
        # the Homebrew lib dir.
        list(APPEND DREAM_EXTRA_RPATHS "${DREAM_BREW_PREFIX}/lib")
    endif()
endif()

if(APPLE)
    # Minimum macOS for Info.plist: explicit deployment target, else the host
    # version (Homebrew bottles are built for the running macOS).
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
        set(DREAM_MACOS_MIN "${CMAKE_OSX_DEPLOYMENT_TARGET}")
    else()
        execute_process(COMMAND sw_vers -productVersion OUTPUT_VARIABLE _osver OUTPUT_STRIP_TRAILING_WHITESPACE)
        string(REGEX MATCH "^[0-9]+" _osmajor "${_osver}")
        set(DREAM_MACOS_MIN "${_osmajor}.0")
    endif()
endif()

find_package(PkgConfig REQUIRED)

# ---------------------------------------------------------------------------
# Mandatory libraries
# ---------------------------------------------------------------------------
pkg_check_modules(FFTW3 REQUIRED IMPORTED_TARGET fftw3)
pkg_check_modules(SPEEXDSP REQUIRED IMPORTED_TARGET speexdsp)   # src/resample/speexresampler.* is always compiled
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

set(DREAM_HAVE_PCAP OFF)
if(UNIX)
    find_library(PCAP_LIBRARY pcap)
    find_path(PCAP_INCLUDE_DIR pcap.h)
    if(PCAP_LIBRARY AND PCAP_INCLUDE_DIR)
        set(DREAM_HAVE_PCAP ON)
    else()
        message(FATAL_ERROR "libpcap not found (needed for RSCI/MDI file input)")
    endif()
endif()

# ---------------------------------------------------------------------------
# Optional libraries
# ---------------------------------------------------------------------------
macro(dream_optional_pkg flag prefix module)
    set(DREAM_HAVE_${prefix} OFF)
    if(${flag})
        pkg_check_modules(${prefix} IMPORTED_TARGET ${module})
        if(${prefix}_FOUND)
            set(DREAM_HAVE_${prefix} ON)
        endif()
    endif()
endmacro()

# fdk-aac: headers only (the library is dlopen()ed at runtime, see fdk_dll.h)
set(DREAM_HAVE_FDKAAC OFF)
if(DREAM_WITH_FDK_AAC)
    pkg_check_modules(FDKAAC QUIET fdk-aac)
    find_path(FDKAAC_INCLUDE_DIR fdk-aac/aacenc_lib.h HINTS ${FDKAAC_INCLUDE_DIRS})
    if(FDKAAC_INCLUDE_DIR AND EXISTS "${FDKAAC_INCLUDE_DIR}/fdk-aac/aacdecoder_lib.h")
        set(DREAM_HAVE_FDKAAC ON)
        if(NOT FDKAAC_VERSION)
            set(FDKAAC_VERSION "headers")
        endif()
        set(FDKAAC_VERSION "${FDKAAC_VERSION}, headers ${FDKAAC_INCLUDE_DIR}, runtime-loaded")
    endif()
endif()
dream_optional_pkg(DREAM_WITH_OPUS     OPUS       opus)
dream_optional_pkg(DREAM_WITH_SNDFILE  SNDFILE    sndfile)
dream_optional_pkg(DREAM_WITH_HAMLIB   HAMLIB     hamlib)
dream_optional_pkg(DREAM_WITH_GPS      GPS        libgps)
dream_optional_pkg(DREAM_WITH_SOAPYSDR SOAPYSDR   SoapySDR)
dream_optional_pkg(DREAM_WITH_PORTAUDIO PORTAUDIO portaudio-2.0)
dream_optional_pkg(DREAM_WITH_PULSEAUDIO PULSE    libpulse)
dream_optional_pkg(DREAM_WITH_ALSA     ALSA       alsa)
dream_optional_pkg(DREAM_WITH_JACK     JACK       jack)

if(DREAM_REQUIRE_FDK_AAC AND NOT DREAM_HAVE_FDKAAC)
    message(FATAL_ERROR "fdk-aac headers required but not found. Build fdk-aac (macx/build.sh does) and set "
                        "CMAKE_PREFIX_PATH or PKG_CONFIG_PATH to its prefix.")
elseif(DREAM_WITH_FDK_AAC AND NOT DREAM_HAVE_FDKAAC)
    message(WARNING "fdk-aac headers not found: building WITHOUT FDK support. DRM AAC decoding/encoding "
                    "will rely on libfaad_drm / libfaac_drm being loadable at runtime.")
endif()
if(APPLE AND DREAM_HAVE_FDKAAC AND FDKAAC_LIBRARY_DIRS)
    # Not linked: only a runtime search hint for dev runs (dlopen of @rpath/libfdk-aac.2.dylib),
    # placed before the Homebrew lib dir so a locally built (DRM-patched) fdk-aac wins.
    list(PREPEND DREAM_EXTRA_RPATHS ${FDKAAC_LIBRARY_DIRS})
    list(REMOVE_DUPLICATES DREAM_EXTRA_RPATHS)
endif()

# ---------------------------------------------------------------------------
# Common target setup
# ---------------------------------------------------------------------------
function(dream_setup_common target)
    target_include_directories(${target} PRIVATE "${CMAKE_SOURCE_DIR}" "${DREAM_SRC}")
    target_compile_definitions(${target} PRIVATE EXECUTABLE_NAME=dream)

    if(UNIX)
        target_compile_definitions(${target} PRIVATE
            HAVE_DLFCN_H HAVE_MEMORY_H HAVE_STDINT_H HAVE_STDLIB_H
            HAVE_STRINGS_H HAVE_STRING_H STDC_HEADERS
            HAVE_INTTYPES_H HAVE_SYS_STAT_H HAVE_SYS_TYPES_H HAVE_UNISTD_H)
    endif()
    if(WIN32)
        target_compile_definitions(${target} PRIVATE _USE_MATH_DEFINES NOMINMAX _CRT_SECURE_NO_WARNINGS HAVE_STDINT_H)
    endif()

    target_compile_definitions(${target} PRIVATE HAVE_LIBZ HAVE_SPEEX)
    target_link_libraries(${target} PRIVATE PkgConfig::FFTW3 PkgConfig::SPEEXDSP ZLIB::ZLIB Threads::Threads)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(${target} PRIVATE ${CMAKE_DL_LIBS} rt)
    endif()

    if(DREAM_HAVE_PCAP)
        target_compile_definitions(${target} PRIVATE HAVE_LIBPCAP)
        target_include_directories(${target} PRIVATE "${PCAP_INCLUDE_DIR}")
        target_link_libraries(${target} PRIVATE "${PCAP_LIBRARY}")
    endif()

    if(DREAM_HAVE_FDKAAC)
        target_compile_definitions(${target} PRIVATE HAVE_LIBFDK_AAC)
        if(DREAM_WITH_USAC)
            target_compile_definitions(${target} PRIVATE HAVE_USAC)
        endif()
        target_sources(${target} PRIVATE ${DREAM_SRC}/sourcedecoders/fdk_aac_codec.cpp)
        # headers only - libfdk-aac is NOT linked (runtime-loaded, licence)
        target_include_directories(${target} PRIVATE "${FDKAAC_INCLUDE_DIR}")
    endif()

    if(DREAM_HAVE_OPUS)
        target_compile_definitions(${target} PRIVATE HAVE_LIBOPUS USE_OPUS_LIBRARY)
        # pkg-config reports <prefix>/include/opus, the code includes <opus/opus.h>
        foreach(_d IN LISTS OPUS_INCLUDE_DIRS)
            get_filename_component(_p "${_d}" DIRECTORY)
            target_include_directories(${target} PRIVATE "${_p}")
        endforeach()
        target_link_libraries(${target} PRIVATE PkgConfig::OPUS)
    endif()

    if(DREAM_HAVE_SNDFILE)
        target_compile_definitions(${target} PRIVATE HAVE_LIBSNDFILE)
        target_link_libraries(${target} PRIVATE PkgConfig::SNDFILE)
    endif()

    if(DREAM_HAVE_GPS)
        target_compile_definitions(${target} PRIVATE HAVE_LIBGPS)
        target_link_libraries(${target} PRIVATE PkgConfig::GPS)
    endif()

    if(DREAM_HAVE_HAMLIB)
        target_compile_definitions(${target} PRIVATE HAVE_LIBHAMLIB)
        target_sources(${target} PRIVATE ${DREAM_SRC}/util/Hamlib.cpp)
        target_link_libraries(${target} PRIVATE PkgConfig::HAMLIB)
        if(APPLE)
            target_link_libraries(${target} PRIVATE "-framework IOKit")
        endif()
    endif()

    if(APPLE)
        target_link_libraries(${target} PRIVATE
            "-framework CoreFoundation" "-framework CoreServices"
            "-framework CoreAudio" "-framework AudioToolbox" "-framework AudioUnit")
        set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH ${DREAM_EXTRA_RPATHS})
        set_property(TARGET ${target} APPEND PROPERTY INSTALL_RPATH ${DREAM_EXTRA_RPATHS})
    endif()

    dream_configure_backends(${target})
    dream_apply_hardening(${target})
endfunction()

# ---------------------------------------------------------------------------
# Console (no Qt) binary
# ---------------------------------------------------------------------------
if(DREAM_CONSOLE_ONLY OR DREAM_BUILD_CONSOLE)
    dream_prefix_sources(_core ${DREAM_CORE_SOURCES})
    dream_prefix_sources(_con  ${DREAM_CONSOLE_SOURCES})
    add_executable(dream_console ${_core} ${_con})
    if(DREAM_CONSOLE_ONLY)
        set_target_properties(dream_console PROPERTIES OUTPUT_NAME dream)
    else()
        set_target_properties(dream_console PROPERTIES OUTPUT_NAME dream-console)
    endif()
    target_compile_definitions(dream_console PRIVATE NO_QT)
    dream_setup_common(dream_console)
    install(TARGETS dream_console RUNTIME DESTINATION bin)
endif()

# ---------------------------------------------------------------------------
# GUI (Qt6 + Qwt)
# ---------------------------------------------------------------------------
if(NOT DREAM_CONSOLE_ONLY)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets Network Xml Svg)
    if(DREAM_QT5COMPAT)
        find_package(Qt6 REQUIRED COMPONENTS Core5Compat)
    endif()

    # Qwt (Qt6 build). Homebrew: framework at opt/qwt/lib/qwt.framework.
    find_path(QWT_INCLUDE_DIR qwt_plot.h
        PATH_SUFFIXES qwt qwt6 qwt-qt6 qt6/qwt lib/qwt.framework/Headers)
    find_library(QWT_LIBRARY NAMES qwt-qt6 qwt6-qt6 qwt)
    if(NOT QWT_INCLUDE_DIR OR NOT QWT_LIBRARY)
        message(FATAL_ERROR "Qwt for Qt6 not found (brew install qwt / apt install libqwt-qt6-dev); "
                            "set QWT_INCLUDE_DIR and QWT_LIBRARY")
    endif()

    # moc: run per header via qt_wrap_cpp (below) rather than AUTOMOC. AUTOMOC
    # concatenates all moc files into one mocs_compilation.cpp, which breaks on
    # headers without include guards (AACCodecParams.h, OpusCodecParams.h,
    # AnalogDemDlg.h). Per-file moc is also what qmake does.
    set(CMAKE_AUTOMOC OFF)
    set(CMAKE_AUTOUIC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC_SEARCH_PATHS "${DREAM_SRC}/GUI-QT")

    set(_gui_rel ${DREAM_QTCORE_SOURCES} ${DREAM_GUI_SOURCES})
    set(_forms_rel ${DREAM_GUI_FORMS})
    if(DREAM_HAVE_HAMLIB)
        list(APPEND _gui_rel util-QT/Rig.cpp GUI-QT/RigDlg.cpp)
        list(APPEND _forms_rel GUI-QT/RigDlg.ui)
    endif()
    if(DREAM_WITH_BWS)
        find_package(Qt6 REQUIRED COMPONENTS WebEngineWidgets)
        list(APPEND _gui_rel GUI-QT/BWSViewer.cpp)
        list(APPEND _forms_rel GUI-QT/BWSViewer.ui)
    endif()

    dream_prefix_sources(_core ${DREAM_CORE_SOURCES})
    dream_prefix_sources(_gui ${_gui_rel})
    dream_prefix_sources(_forms ${_forms_rel})
    dream_companion_headers(_gui_hdrs ${_gui})
    set(_rc "${DREAM_SRC}/GUI-QT/res/icons.qrc")

    if(APPLE)
        set(_icon "${DREAM_SRC}/GUI-QT/res/macicons.icns")
        set_source_files_properties(${_icon} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
        add_executable(dream MACOSX_BUNDLE ${_core} ${_gui} ${_gui_hdrs} ${_forms} ${_rc} ${_icon})
        set_target_properties(dream PROPERTIES
            OUTPUT_NAME Dream
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/macx/Info.plist.in"
            MACOSX_BUNDLE_BUNDLE_NAME Dream
            MACOSX_BUNDLE_EXECUTABLE_NAME Dream
            MACOSX_BUNDLE_GUI_IDENTIFIER net.sourceforge.drm.dream
            MACOSX_BUNDLE_ICON_FILE macicons.icns
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
    elseif(WIN32)
        add_executable(dream WIN32 ${_core} ${_gui} ${_gui_hdrs} ${_forms} ${_rc} "${CMAKE_SOURCE_DIR}/windows/dream.rc")
    else()
        add_executable(dream ${_core} ${_gui} ${_gui_hdrs} ${_forms} ${_rc})
    endif()

    set(_moc_hdrs "")
    foreach(_h IN LISTS _gui_hdrs)
        file(STRINGS "${_h}" _qobj REGEX "Q_OBJECT")
        if(_qobj)
            list(APPEND _moc_hdrs "${_h}")
        endif()
    endforeach()
    qt_wrap_cpp(_mocs ${_moc_hdrs} TARGET dream)
    target_sources(dream PRIVATE ${_mocs})

    target_include_directories(dream PRIVATE "${DREAM_SRC}/GUI-QT" "${QWT_INCLUDE_DIR}")
    target_link_libraries(dream PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Xml Qt6::Svg "${QWT_LIBRARY}")
    if(DREAM_QT5COMPAT)
        target_link_libraries(dream PRIVATE Qt6::Core5Compat)
    endif()
    if(DREAM_WITH_BWS)
        target_link_libraries(dream PRIVATE Qt6::WebEngineWidgets)
        # src/GUI-QT/fdrmdialog.cpp keys BWS support on the Qt5 macro name
        target_compile_definitions(dream PRIVATE QT_WEBENGINE_LIB)
    endif()
    dream_setup_common(dream)

    if(APPLE)
        install(TARGETS dream BUNDLE DESTINATION .)
    else()
        install(TARGETS dream RUNTIME DESTINATION bin)
        if(UNIX)
            install(FILES "${CMAKE_SOURCE_DIR}/linux/dream.1" DESTINATION share/man/man1)
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "DREAM ${PROJECT_VERSION} configuration (${CMAKE_BUILD_TYPE}, ${_dream_arch})")
if(DREAM_CONSOLE_ONLY)
    message(STATUS "  targets     : console only (no Qt)")
else()
    message(STATUS "  targets     : GUI (Qt ${Qt6_VERSION}, Qwt ${QWT_LIBRARY})")
    if(DREAM_BUILD_CONSOLE)
        message(STATUS "                + console (dream-console)")
    endif()
endif()
foreach(_f FDKAAC OPUS SNDFILE HAMLIB GPS SOAPYSDR PORTAUDIO PULSE ALSA JACK)
    if(DREAM_HAVE_${_f})
        message(STATUS "  ${_f}: yes (${${_f}_VERSION})")
    else()
        message(STATUS "  ${_f}: no")
    endif()
endforeach()
message(STATUS "  BWS viewer  : ${DREAM_WITH_BWS}")
message(STATUS "")
