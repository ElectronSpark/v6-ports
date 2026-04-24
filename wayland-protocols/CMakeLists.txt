cmake_minimum_required(VERSION 3.16)
project(xv6-port-wayland-protocols LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

# wayland-protocols ships only XML files + a pkg-config file; no
# compiled artifacts. Built with meson but architecture-independent.
xv6_port(
    NAME            wayland-protocols
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    meson
    DEPENDS         wayland-libs wayland-host
    OUTPUT_FILES    share/pkgconfig/wayland-protocols.pc
    CMAKE_ARGS
        -Dtests=false
)
