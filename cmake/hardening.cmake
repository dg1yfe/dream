# Compiler/linker hardening (OpenSSF recommendations), adapted from origin/qt6.
# https://best.openssf.org/Compiler-Hardening-Guides/Compiler-Options-Hardening-Guide-for-C-and-C++.html
#
# Usage: dream_apply_hardening(<target>) – no-op unless DREAM_HARDENING is ON.
# Apple ld64 does not understand the ELF -z options, and PIE is the default on
# macOS, so only the compile-side flags are used there.

include_guard(GLOBAL)

function(dream_apply_hardening target)
    if(NOT DREAM_HARDENING)
        return()
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        return()
    endif()

    target_compile_options(${target} PRIVATE
        -Wformat
        -Wformat=2
        -Wimplicit-fallthrough
        -fstack-protector-strong
        # _FORTIFY_SOURCE needs optimisation; only set it for optimised configs.
        $<$<NOT:$<CONFIG:Debug>>:-U_FORTIFY_SOURCE>
        $<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=2>
    )

    if(NOT APPLE)
        target_compile_options(${target} PRIVATE -fstack-clash-protection -fPIE)
        target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-D_GLIBCXX_ASSERTIONS>)
        target_link_options(${target} PRIVATE
            -pie
            -Wl,-z,relro
            -Wl,-z,now
            -Wl,-z,noexecstack
        )
    endif()
endfunction()
