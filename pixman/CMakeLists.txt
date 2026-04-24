cmake_minimum_required(VERSION 3.16)
project(xv6-port-pixman LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

xv6_port(
    NAME            pixman
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    meson
    OUTPUT_FILES    lib/libpixman-1.a
                    include/pixman-1/pixman.h
    CMAKE_ARGS
        -Dlibpng=disabled
        -Dgtk=disabled
        -Dtests=disabled
        -Ddemos=disabled
        -Dopenmp=disabled
)
