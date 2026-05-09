cmake_minimum_required(VERSION 3.16)
project(xv6-port-harfbuzz LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

xv6_port(
    NAME            harfbuzz
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    meson
    MESON_DEFAULT_LIBRARY both
    DEPENDS         freetype
    OUTPUT_FILES    lib/libharfbuzz.a
                    lib/libharfbuzz.so
                    lib/libharfbuzz.so.0
                    lib/libharfbuzz.so.0.61420.0
                    include/harfbuzz/hb.h
    CMAKE_ARGS
        -Dfreetype=enabled
        -Dglib=disabled
        -Dgobject=disabled
        -Dcairo=disabled
        -Dchafa=disabled
        -Dicu=disabled
        -Dgraphite=disabled
        -Dgraphite2=disabled
        -Dtests=disabled
        -Ddocs=disabled
        -Dutilities=disabled
        -Dintrospection=disabled
        -Dcoretext=disabled
        -Ddirectwrite=disabled
        -Dgdi=disabled
)
