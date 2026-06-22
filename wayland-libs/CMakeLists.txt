cmake_minimum_required(VERSION 3.16)
project(xv6-port-wayland-libs LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

# Cross build of the wayland client + server libraries (no scanner —
# that is built by ports/wayland-host as a host tool).
xv6_port(
    NAME            wayland-libs
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/../wayland-src/src
    BUILD_SYSTEM    meson
    MESON_DEFAULT_LIBRARY both
    DEPENDS         libffi libexpat wayland-host
    C_LINK_ARGS     -Wl,-Bsymbolic
    OUTPUT_FILES    lib/libwayland-client.a
                    lib/libwayland-client.so
                    lib/libwayland-client.so.0
                    lib/libwayland-client.so.0.25.90
                    lib/libwayland-cursor.a
                    lib/libwayland-cursor.so
                    lib/libwayland-cursor.so.0
                    lib/libwayland-cursor.so.0.25.90
                    lib/libwayland-egl.a
                    lib/libwayland-egl.so
                    lib/libwayland-egl.so.1
                    lib/libwayland-egl.so.1.25.90
                    lib/libwayland-server.a
                    lib/libwayland-server.so
                    lib/libwayland-server.so.0
                    lib/libwayland-server.so.0.25.90
                    include/wayland-client.h
    CMAKE_ARGS
        -Dlibraries=true
        -Dscanner=false
        -Dtests=false
        -Ddocumentation=false
        -Ddtd_validation=false
)
