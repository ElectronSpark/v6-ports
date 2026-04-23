# xv6-os wayland port — pragmatic stage-from-prebuilt recipe.
#
# Stages prebuilt wlcomp Wayland compositor and desktop session
# manager binaries from the xv6-tmp reference sysroot into XV6_SYSROOT.
#
# Like ports/cpython, this port currently does NOT build wayland from
# source. The from-source chain (libffi → wayland → wayland-protocols →
# wlcomp/desktop) is documented in ports/MIGRATION.md.
#
# Inputs (cache vars, with defaults):
#   WAYLAND_REF_SYSROOT  Reference sysroot to copy artefacts FROM.
#                        Default: /home/es/xv6/xv6-tmp/build-x86/sysroot
#
# Outputs (in ${XV6_SYSROOT}):
#   bin/wlcomp    Wayland compositor (writes /dev/fb0, reads /dev/{kbd,mouse})
#   bin/desktop   Wayland session manager (launches wlcomp + clients)
#
# Runtime deps (already provided by ports/cpython staging):
#   /lib/ld-musl-x86_64.so.1
#   /lib/libc.so
#   /lib/libffi.so.7
#
# Kernel deps (already in xv6-os/kernel):
#   /dev/fb0   (kernel/dev/fb.c — virtio-gpu / VGA / EFI fb)
#   /dev/kbd   (kernel/dev/ps2kbd.c)
#   /dev/mouse (kernel/dev/ps2mouse.c)

cmake_minimum_required(VERSION 3.16)
project(xv6-port-wayland LANGUAGES NONE)

if(NOT DEFINED XV6_SYSROOT OR XV6_SYSROOT STREQUAL "")
    message(FATAL_ERROR
        "ports/wayland: -DXV6_SYSROOT=<path> required")
endif()

set(WAYLAND_REF_SYSROOT
    "/home/es/xv6/xv6-tmp/build-x86/sysroot"
    CACHE PATH "Reference sysroot containing prebuilt wlcomp + desktop")

if(NOT EXISTS "${WAYLAND_REF_SYSROOT}/bin/wlcomp")
    message(FATAL_ERROR
        "ports/wayland: ${WAYLAND_REF_SYSROOT}/bin/wlcomp not found. "
        "Set WAYLAND_REF_SYSROOT or build wlcomp first.")
endif()

set(_script "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/stage-wayland.sh")
get_filename_component(_script "${_script}" ABSOLUTE)
if(NOT EXISTS "${_script}")
    message(FATAL_ERROR "ports/wayland: stage-wayland.sh missing at ${_script}")
endif()

set(_sentinel ${XV6_SYSROOT}/bin/wlcomp)
set(_stamp    ${CMAKE_CURRENT_BINARY_DIR}/staged.stamp)

add_custom_command(
    OUTPUT  ${_sentinel} ${XV6_SYSROOT}/bin/desktop ${_stamp}
    COMMAND bash ${_script} ${WAYLAND_REF_SYSROOT} ${XV6_SYSROOT}
    COMMAND ${CMAKE_COMMAND} -E touch ${_stamp}
    DEPENDS ${_script}
    COMMENT "Staging wlcomp + desktop into ${XV6_SYSROOT}"
    VERBATIM)

add_custom_target(port-wayland ALL
    DEPENDS ${_sentinel} ${XV6_SYSROOT}/bin/desktop ${_stamp})

add_custom_target(port-wayland-clean
    COMMAND ${CMAKE_COMMAND} -E remove -f
                ${_sentinel}
                ${XV6_SYSROOT}/bin/desktop
                ${_stamp}
    COMMENT "Removing staged wayland artefacts from ${XV6_SYSROOT}")
