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

# ── wlcomp rebuild from source ──────────────────────────────────────
#
# The prebuilt wlcomp staged above contains a sign-extension bug in its
# absolute-mouse-coord scaling:
#
#     g_cursor_x = (int16_t)((uint32_t)ev.dx * g_fb_w / 65536);
#
# `ev.dx` is `int16_t`, so values >32767 sign-extend to 0xFFFFnnnn under
# the (uint32_t) cast and the cursor snaps to (0,0) once the host pointer
# crosses screen-center. Fix is a zero-extending (uint16_t) cast first.
#
# Rather than carry a patched binary, we re-compile wlcomp from the
# upstream source tree against the prebuilt headers/libs already shipped
# in WAYLAND_REF_SYSROOT, then overwrite the staged binary.

set(WAYLAND_SRC_DIR
    "/home/es/xv6/xv6-tmp/user/wlcomp"
    CACHE PATH "wlcomp/desktop source directory")

if(EXISTS "${WAYLAND_SRC_DIR}/wlcomp.c" AND DEFINED CMAKE_C_COMPILER)
    set(_wl_build  ${CMAKE_CURRENT_BINARY_DIR}/wlcomp-build)
    set(_wl_src    ${_wl_build}/wlcomp.c)
    set(_wl_font   ${_wl_build}/font8x16.h)
    set(_wl_bin    ${_wl_build}/wlcomp)
    file(MAKE_DIRECTORY ${_wl_build})

    # Patch step: copy wlcomp.c into the build dir with the cast fixed.
    add_custom_command(
        OUTPUT  ${_wl_src} ${_wl_font}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${WAYLAND_SRC_DIR}/font8x16.h ${_wl_font}
        COMMAND sed -e "s|(uint32_t)ev\\.dx|(uint32_t)(uint16_t)ev.dx|"
                    -e "s|(uint32_t)ev\\.dy|(uint32_t)(uint16_t)ev.dy|"
                    ${WAYLAND_SRC_DIR}/wlcomp.c > ${_wl_src}
        DEPENDS ${WAYLAND_SRC_DIR}/wlcomp.c ${WAYLAND_SRC_DIR}/font8x16.h
        COMMENT "Patching wlcomp.c (zero-extend absolute mouse coords)"
        VERBATIM)

    # Compile against headers + libs already shipped in the reference sysroot.
    add_custom_command(
        OUTPUT  ${_wl_bin}
        COMMAND ${CMAKE_C_COMPILER} -O2 -g -Wall -Wno-unused-result
                    -isystem ${WAYLAND_REF_SYSROOT}/include
                    -I ${WAYLAND_REF_SYSROOT}/include/wayland
                    -L ${WAYLAND_REF_SYSROOT}/lib
                    -L ${WAYLAND_REF_SYSROOT}/lib64
                    -Wl,-rpath-link,${WAYLAND_REF_SYSROOT}/lib
                    -o ${_wl_bin} ${_wl_src}
                    -lwayland-server -lffi -lpthread -lrt -lm
        DEPENDS ${_wl_src} ${_wl_font}
        COMMENT "Compiling patched wlcomp from source"
        VERBATIM)

    # Install: overwrite the staged wlcomp with the freshly built one.
    set(_wl_installed ${XV6_SYSROOT}/bin/wlcomp.rebuilt.stamp)
    add_custom_command(
        OUTPUT  ${_wl_installed}
        COMMAND ${CMAKE_COMMAND} -E copy ${_wl_bin} ${XV6_SYSROOT}/bin/wlcomp
        COMMAND ${CMAKE_COMMAND} -E touch ${_wl_installed}
        DEPENDS ${_wl_bin} ${_sentinel}
        COMMENT "Installing rebuilt wlcomp into ${XV6_SYSROOT}/bin"
        VERBATIM)

    set(_wl_extra_dep ${_wl_installed})
else()
    message(WARNING
        "ports/wayland: WAYLAND_SRC_DIR=${WAYLAND_SRC_DIR}/wlcomp.c not found "
        "or no CMAKE_C_COMPILER; skipping wlcomp rebuild. Cursor will snap to "
        "(0,0) when the host pointer crosses screen-center.")
    set(_wl_extra_dep)
endif()

add_custom_target(port-wayland ALL
    DEPENDS ${_sentinel} ${XV6_SYSROOT}/bin/desktop ${_stamp} ${_wl_extra_dep})

add_custom_target(port-wayland-clean
    COMMAND ${CMAKE_COMMAND} -E remove -f
                ${_sentinel}
                ${XV6_SYSROOT}/bin/desktop
                ${_stamp}
                ${_wl_extra_dep}
    COMMENT "Removing staged wayland artefacts from ${XV6_SYSROOT}")
