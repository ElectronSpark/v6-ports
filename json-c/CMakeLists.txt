cmake_minimum_required(VERSION 3.16)
project(xv6-port-json-c LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

xv6_port(
    NAME            json-c
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    cmake
    OUTPUT_FILES    lib/libjson-c.a
                    lib/pkgconfig/json-c.pc
                    include/json-c/json.h
    CMAKE_ARGS
        -DBUILD_SHARED_LIBS=OFF
        -DBUILD_STATIC_LIBS=ON
        -DBUILD_TESTING=OFF
        -DDISABLE_WERROR=ON
        -DDISABLE_EXTRA_LIBS=ON
        -DENABLE_RDRAND=OFF
        -DENABLE_THREADING=OFF
)
