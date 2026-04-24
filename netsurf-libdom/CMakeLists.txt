cmake_minimum_required(VERSION 3.16)
project(xv6-port-netsurf-libdom LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

xv6_port(
    NAME            netsurf-libdom
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    netsurf
    DEPENDS         netsurf-buildsystem netsurf-nsgenbind libexpat
                    netsurf-libwapcaplet netsurf-libparserutils
                    netsurf-libhubbub netsurf-libcss
    OUTPUT_FILES    lib/libdom.a
                    include/dom/dom.h
)
