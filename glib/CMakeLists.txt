cmake_minimum_required(VERSION 3.16)
project(xv6-port-glib LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

xv6_port(
    NAME            glib
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    meson
    DEPENDS         pcre2 libffi zlib
    OUTPUT_FILES    lib/libglib-2.0.a
                    lib/libgobject-2.0.a
                    lib/libgio-2.0.a
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
        -Dglib_debug=disabled
)
