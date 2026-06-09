cmake_minimum_required(VERSION 3.16)
project(xv6-port-drm-info LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

xv6_port(
    NAME            drm_info
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    meson
    DEPENDS         libdrm
                    json-c
    OUTPUT_FILES    bin/drm_info
    CMAKE_ARGS
        -Dlibpci=disabled
        -Dlibdisplay-info=disabled
        -Dman-pages=disabled
)
