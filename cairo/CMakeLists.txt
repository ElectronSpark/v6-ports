cmake_minimum_required(VERSION 3.16)
project(xv6-port-cairo LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

xv6_port(
    NAME            cairo
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    meson
    MESON_DEFAULT_LIBRARY both
    DEPENDS         pixman freetype fontconfig libpng zlib glib bzip2-symlink
    SOURCE_DEPENDS  ${CMAKE_CURRENT_LIST_FILE}
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
    POST_INSTALL_COMMANDS
        ${CMAKE_COMMAND} -E rm -f
            ${XV6_SYSROOT}/lib/libcairo.so
            ${XV6_SYSROOT}/lib/libcairo.so.2
            ${XV6_SYSROOT}/lib/libcairo-gobject.so
            ${XV6_SYSROOT}/lib/libcairo-gobject.so.2
            ${XV6_SYSROOT}/include/cairo/cairo-xcb.h
            ${XV6_SYSROOT}/lib/pkgconfig/cairo-xcb.pc
            ${XV6_SYSROOT}/lib/pkgconfig/cairo-xcb-shm.pc
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            libcairo.so.2.11805.5
            ${XV6_SYSROOT}/lib/libcairo.so.2
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            libcairo.so.2
            ${XV6_SYSROOT}/lib/libcairo.so
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            libcairo-gobject.so.2.11805.5
            ${XV6_SYSROOT}/lib/libcairo-gobject.so.2
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            libcairo-gobject.so.2
            ${XV6_SYSROOT}/lib/libcairo-gobject.so
)
