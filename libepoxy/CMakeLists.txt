cmake_minimum_required(VERSION 3.16)
project(xv6-port-libepoxy LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

# libepoxy — OpenGL function pointer dispatcher used by GTK3.
# We disable GLX/X11 (no X server on xv6); EGL stays so GTK3 can talk
# to a Wayland compositor that exposes EGL.
xv6_port(
    NAME            libepoxy
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    meson
    DEPENDS         khronos-headers
    OUTPUT_FILES    lib/libepoxy.a
                    include/epoxy/gl.h
    CMAKE_ARGS
        -Ddocs=false
        -Dglx=no
        -Degl=yes
        -Dx11=false
        -Dtests=false
    PATCHES
        ${CMAKE_CURRENT_LIST_DIR}/patches/0001-xv6-missing-gl-stubs.patch
        ${CMAKE_CURRENT_LIST_DIR}/patches/0002-xv6-dispatch-generator.patch
)
