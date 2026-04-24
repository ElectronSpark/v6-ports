cmake_minimum_required(VERSION 3.16)
project(xv6-port-netsurf-libnsutils LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

xv6_port(
    NAME            netsurf-libnsutils
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    netsurf
    DEPENDS         netsurf-buildsystem
    OUTPUT_FILES    lib/libnsutils.a
                    include/nsutils/time.h
)
