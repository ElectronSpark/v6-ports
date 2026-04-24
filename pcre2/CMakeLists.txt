cmake_minimum_required(VERSION 3.16)
project(xv6-port-pcre2 LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

xv6_port(
    NAME            pcre2
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    cmake
    OUTPUT_FILES    lib/libpcre2-8.a
                    include/pcre2.h
    CMAKE_ARGS
        -DBUILD_SHARED_LIBS=OFF
        -DBUILD_STATIC_LIBS=ON
        -DPCRE2_BUILD_PCRE2_8=ON
        -DPCRE2_BUILD_PCRE2_16=OFF
        -DPCRE2_BUILD_PCRE2_32=OFF
        -DPCRE2_BUILD_TESTS=OFF
        -DPCRE2_BUILD_PCRE2GREP=OFF
        -DPCRE2_SUPPORT_LIBEDIT=OFF
        -DPCRE2_SUPPORT_LIBREADLINE=OFF
        -DPCRE2_SUPPORT_JIT=OFF
)
