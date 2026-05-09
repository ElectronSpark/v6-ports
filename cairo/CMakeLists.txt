cmake_minimum_required(VERSION 3.16)
project(xv6-port-cairo LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

xv6_port(
    NAME            cairo
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    meson
    MESON_DEFAULT_LIBRARY both
    DEPENDS         pixman freetype fontconfig libpng zlib glib bzip2-symlink
    OUTPUT_FILES    lib/libcairo.a
                    lib/libcairo.so
                    lib/libcairo.so.2
                    lib/libcairo.so.2.11805.5
                    lib/libcairo-gobject.a
                    lib/libcairo-gobject.so
                    lib/libcairo-gobject.so.2
                    lib/libcairo-gobject.so.2.11805.5
                    include/cairo/cairo.h
    CMAKE_ARGS
        -Dfreetype=enabled
        -Dfontconfig=enabled
        -Dpng=enabled
        -Dzlib=enabled
        -Dglib=enabled
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
