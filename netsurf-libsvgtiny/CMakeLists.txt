cmake_minimum_required(VERSION 3.16)
project(xv6-port-netsurf-libsvgtiny LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

xv6_port(
    NAME            netsurf-libsvgtiny
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    netsurf
    DEPENDS         netsurf-buildsystem libexpat netsurf-libwapcaplet
                    netsurf-libparserutils netsurf-libhubbub netsurf-libdom
    OUTPUT_FILES    lib/libsvgtiny.a
                    include/svgtiny.h
)
