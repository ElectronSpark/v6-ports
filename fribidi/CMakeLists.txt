cmake_minimum_required(VERSION 3.16)
project(xv6-port-fribidi LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

xv6_port(
    NAME            fribidi
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    meson
    OUTPUT_FILES    lib/libfribidi.a
                    include/fribidi/fribidi.h
    CMAKE_ARGS
        -Ddocs=false
        -Dtests=false
        -Dbin=false
)
