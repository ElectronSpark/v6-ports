cmake_minimum_required(VERSION 3.16)
project(xv6-port-atk LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

xv6_port(
    NAME            atk
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    meson
    MESON_DEFAULT_LIBRARY both
    DEPENDS         glib
    OUTPUT_FILES    lib/libatk-1.0.a
                    lib/libatk-1.0.so
                    lib/libatk-1.0.so.0
                    lib/libatk-1.0.so.0.23810.1
                    include/atk-1.0/atk/atk.h
    CMAKE_ARGS
        -Ddocs=false
        -Dintrospection=false
)
