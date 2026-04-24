cmake_minimum_required(VERSION 3.16)
project(xv6-port-netsurf-libhubbub LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

xv6_port(
    NAME            netsurf-libhubbub
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    netsurf
    DEPENDS         netsurf-buildsystem netsurf-libparserutils
    OUTPUT_FILES    lib/libhubbub.a
                    include/hubbub/hubbub.h
)
