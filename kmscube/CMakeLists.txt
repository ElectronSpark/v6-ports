cmake_minimum_required(VERSION 3.16)
project(xv6-port-kmscube LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

xv6_port(
    NAME            kmscube
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    meson
    DEPENDS         libdrm
                    mesa
    OUTPUT_FILES    bin/kmscube
    CMAKE_ARGS
        -Dgstreamer=disabled
)
