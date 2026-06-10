cmake_minimum_required(VERSION 3.16)
project(xv6-port-libinput LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

xv6_port(
    NAME            libinput
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    cmake
    DEPENDS         libudev
                    libevdev
    SOURCE_DEPENDS  ${CMAKE_CURRENT_SOURCE_DIR}/src/CMakeLists.txt
                    ${CMAKE_CURRENT_SOURCE_DIR}/src/libinput.c
                    ${CMAKE_CURRENT_SOURCE_DIR}/src/libinput.h
                    ${CMAKE_CURRENT_SOURCE_DIR}/src/libinput.pc.in
    OUTPUT_FILES    lib/libinput.a
                    lib/libinput.so
                    lib/libinput.so.10
                    lib/libinput.so.10.0.0
                    lib/pkgconfig/libinput.pc
                    include/libinput.h
)
