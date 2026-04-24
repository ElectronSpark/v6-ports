cmake_minimum_required(VERSION 3.16)
project(xv6-port-linux-uapi-headers LANGUAGES NONE)

# Linux UAPI headers — copied verbatim from the build host's
# linux-libc-dev package. xv6 itself does not implement the Linux
# kernel ABI, but ports such as gtk3 (wayland backend) and libxkbcommon
# include <linux/input-event-codes.h> for symbolic key/button names
# (BTN_LEFT, KEY_TAB, ...). These headers are pure macro definitions
# and contain no architecture-specific assembly.

set(_dst    ${XV6_SYSROOT}/include)
set(_marker ${_dst}/linux/input-event-codes.h)

# linux/, asm-generic/ live under /usr/include directly; asm/ is
# multiarch on Debian (x86_64-linux-gnu/asm). We pick whichever exists.
set(_src_linux       /usr/include/linux)
set(_src_asm_generic /usr/include/asm-generic)
set(_src_asm         /usr/include/x86_64-linux-gnu/asm)

if(NOT EXISTS ${_src_linux}/input-event-codes.h)
    message(FATAL_ERROR
        "linux-uapi-headers: ${_src_linux}/input-event-codes.h missing — "
        "install the host package 'linux-libc-dev'.")
endif()

add_custom_command(
    OUTPUT  ${_marker}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_dst}
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${_src_linux}       ${_dst}/linux
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${_src_asm_generic} ${_dst}/asm-generic
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${_src_asm}         ${_dst}/asm
    COMMENT "Installing Linux UAPI headers into sysroot"
    VERBATIM)

add_custom_target(port-linux-uapi-headers ALL DEPENDS ${_marker})

add_custom_target(port-linux-uapi-headers-clean
    COMMAND ${CMAKE_COMMAND} -E rm -rf
            ${_dst}/linux ${_dst}/asm-generic ${_dst}/asm
    COMMENT "Removing Linux UAPI headers")
