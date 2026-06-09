cmake_minimum_required(VERSION 3.16)
project(xv6-port-libseat LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

xv6_port(
    NAME            libseat
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    cmake
    OUTPUT_FILES    lib/libseat.a
                    lib/libseat.so
                    lib/libseat.so.1
                    lib/libseat.so.1.0.0
                    lib/pkgconfig/libseat.pc
                    include/libseat.h
)
