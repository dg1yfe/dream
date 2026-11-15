# Sound I/O backends (adapted from origin/qt6 cmake/audiobackends.cmake).
#
# Each dream_enable_* adds sources/defines/libs to <target>. Detection happens
# once in CMakeLists.txt (pkg_check_modules ... IMPORTED_TARGET), so these
# functions only wire up targets that were found.
#
# macOS: PortAudio (CoreAudio) is THE audio route. QtMultimedia is never used
# as a backend (it is not one in the current tree; forcing it as dream.pro did
# breaks src/ctransmitdata.cpp).

include_guard(GLOBAL)

function(dream_enable_portaudio target)
    target_compile_definitions(${target} PRIVATE USE_PORTAUDIO)
    target_sources(${target} PRIVATE
        ${DREAM_SRC}/sound/drm_portaudio.cpp
        ${DREAM_SRC}/sound/pa_ringbuffer.c
    )
    target_link_libraries(${target} PRIVATE PkgConfig::PORTAUDIO)
endfunction()

function(dream_enable_pulseaudio target)
    target_compile_definitions(${target} PRIVATE USE_PULSEAUDIO)
    target_sources(${target} PRIVATE ${DREAM_SRC}/sound/drm_pulseaudio.cpp)
    target_link_libraries(${target} PRIVATE PkgConfig::PULSE)
endfunction()

function(dream_enable_alsa target)
    target_compile_definitions(${target} PRIVATE USE_ALSA)
    target_sources(${target} PRIVATE
        ${DREAM_SRC}/linux/alsain.cpp
        ${DREAM_SRC}/linux/alsaout.cpp
        ${DREAM_SRC}/linux/alsacommon.cpp
    )
    target_link_libraries(${target} PRIVATE PkgConfig::ALSA)
endfunction()

function(dream_enable_jack target)
    target_compile_definitions(${target} PRIVATE USE_JACK)
    target_sources(${target} PRIVATE ${DREAM_SRC}/linux/jack.cpp)
    target_link_libraries(${target} PRIVATE PkgConfig::JACK)
endfunction()

function(dream_enable_soapy target)
    target_compile_definitions(${target} PRIVATE USE_SOAPYSDR)
    target_sources(${target} PRIVATE ${DREAM_SRC}/sound/drm_soapySDR.cpp)
    target_link_libraries(${target} PRIVATE PkgConfig::SOAPYSDR)
endfunction()

# Wire every detected backend into <target>; fail if there is no real sound I/O.
function(dream_configure_backends target)
    set(have_sound OFF)
    if(DREAM_HAVE_PORTAUDIO)
        dream_enable_portaudio(${target})
        set(have_sound ON)
    endif()
    if(DREAM_HAVE_PULSEAUDIO)
        dream_enable_pulseaudio(${target})
        set(have_sound ON)
    endif()
    if(DREAM_HAVE_ALSA)
        dream_enable_alsa(${target})
        set(have_sound ON)
    endif()
    if(DREAM_HAVE_JACK)
        dream_enable_jack(${target})
        set(have_sound ON)
    endif()
    if(DREAM_HAVE_SOAPYSDR)
        dream_enable_soapy(${target})
    endif()
    if(NOT have_sound)
        message(FATAL_ERROR "No usable audio interface found - install the portaudio (macOS/Windows) "
                            "or pulseaudio/alsa (Linux) development package")
    endif()
endfunction()
