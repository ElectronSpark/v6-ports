cmake_minimum_required(VERSION 3.16)
project(xv6-port-glib LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

xv6_port(
    NAME            glib
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    meson
    MESON_DEFAULT_LIBRARY both
    DEPENDS         pcre2 libffi zlib
    OUTPUT_FILES    lib/libglib-2.0.a
                    lib/libglib-2.0.so
                    lib/libglib-2.0.so.0
                    lib/libglib-2.0.so.0.8900.0
                    lib/libgobject-2.0.a
                    lib/libgobject-2.0.so
                    lib/libgobject-2.0.so.0
                    lib/libgobject-2.0.so.0.8900.0
                    lib/libgio-2.0.a
                    lib/libgio-2.0.so
                    lib/libgio-2.0.so.0
                    lib/libgio-2.0.so.0.8900.0
                    lib/libgmodule-2.0.so
                    lib/libgmodule-2.0.so.0
                    lib/libgmodule-2.0.so.0.8900.0
                    include/glib-2.0/glib.h
    CMAKE_ARGS
        -Dselinux=disabled
        -Dxattr=false
        -Dlibmount=disabled
        -Dman-pages=disabled
        -Ddtrace=disabled
        -Dsystemtap=disabled
        -Dsysprof=disabled
        -Ddocumentation=false
        -Dbsymbolic_functions=false
        -Dtests=false
        -Dinstalled_tests=false
        -Dnls=disabled
        -Dlibelf=disabled
        -Dintrospection=disabled
        -Dglib_debug=enabled
        -Dglib_assert=true
        -Dglib_checks=true
)
