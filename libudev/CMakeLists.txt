cmake_minimum_required(VERSION 3.16)
project(xv6-port-libudev LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

xv6_port(
    NAME            libudev
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    cmake
    SOURCE_DEPENDS  ${CMAKE_CURRENT_SOURCE_DIR}/src/CMakeLists.txt
                    ${CMAKE_CURRENT_SOURCE_DIR}/src/libudev.c
                    ${CMAKE_CURRENT_SOURCE_DIR}/src/libudev.h
                    ${CMAKE_CURRENT_SOURCE_DIR}/src/libudev.map
    OUTPUT_FILES    lib/libudev.a
                    lib/libudev.so
                    lib/libudev.so.1
                    lib/libudev.so.1.0.0
                    lib/pkgconfig/libudev.pc
                    include/libudev.h
)
