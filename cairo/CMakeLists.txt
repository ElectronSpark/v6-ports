cmake_minimum_required(VERSION 3.16)
project(xv6-port-cairo LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

xv6_port(
    NAME            cairo
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    meson
    DEPENDS         pixman freetype fontconfig libpng zlib
    OUTPUT_FILES    lib/libcairo.a
                    include/cairo/cairo.h
    CMAKE_ARGS
        -Dfreetype=enabled
        -Dfontconfig=enabled
        -Dpng=enabled
        -Dzlib=enabled
        -Dglib=disabled
        -Dxlib=disabled
        -Dxcb=disabled
        -Dquartz=disabled
        -Dtee=disabled
        -Dgtk2-utils=disabled
        -Dspectre=disabled
        -Dsymbol-lookup=disabled
        -Dlzo=disabled
        -Ddwrite=disabled
        -Dxlib-xcb=disabled
        -Dtests=disabled
)
