cmake_minimum_required(VERSION 3.16)
project(xv6-port-libevdev LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

xv6_port(
    NAME            libevdev
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    cmake
    SOURCE_DEPENDS  ${CMAKE_CURRENT_SOURCE_DIR}/src/CMakeLists.txt
                    ${CMAKE_CURRENT_SOURCE_DIR}/src/libevdev.c
                    ${CMAKE_CURRENT_SOURCE_DIR}/src/libevdev.map
                    ${CMAKE_CURRENT_SOURCE_DIR}/src/libevdev/libevdev.h
    OUTPUT_FILES    lib/libevdev.a
                    lib/libevdev.so
                    lib/libevdev.so.2
                    lib/libevdev.so.2.0.0
                    lib/pkgconfig/libevdev.pc
                    include/libevdev/libevdev.h
)
