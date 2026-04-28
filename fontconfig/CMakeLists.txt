cmake_minimum_required(VERSION 3.16)
project(xv6-port-fontconfig LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

xv6_port(
    NAME            fontconfig
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    meson
    DEPENDS         freetype libexpat
    OUTPUT_FILES    lib/libfontconfig.a
                    include/fontconfig/fontconfig.h
    CMAKE_ARGS
        -Ddoc=disabled
        -Dnls=disabled
        -Dtests=disabled
        -Dtools=disabled
        -Dcache-build=disabled
        -Diconv=disabled
        -Ddefault-fonts-dirs=/usr/share/fonts,/usr/local/share/fonts,/share/fonts,/share/netsurf/fonts
        -Dadditional-fonts-dirs=/share/fonts,/share/netsurf/fonts
)
