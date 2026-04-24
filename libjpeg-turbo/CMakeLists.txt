cmake_minimum_required(VERSION 3.16)
project(xv6-port-libjpeg-turbo LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

xv6_port(
    NAME            libjpeg-turbo
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    cmake
    OUTPUT_FILES    lib/libjpeg.a
                    include/jpeglib.h
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=/
        -DCMAKE_SYSTEM_PROCESSOR=x86_64
        -DENABLE_SHARED=OFF
        -DENABLE_STATIC=ON
        -DWITH_TURBOJPEG=OFF
        -DWITH_JAVA=OFF
        -DWITH_SIMD=OFF
        -DWITH_JPEG8=ON
)
