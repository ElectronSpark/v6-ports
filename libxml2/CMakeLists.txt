cmake_minimum_required(VERSION 3.16)
project(xv6-port-libxml2 LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

xv6_port(
    NAME            libxml2
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    cmake
    DEPENDS         zlib xz
    OUTPUT_FILES    lib/libxml2.a
                    include/libxml2/libxml/parser.h
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=/
        -DBUILD_SHARED_LIBS=OFF
        -DLIBXML2_WITH_PYTHON=OFF
        -DLIBXML2_WITH_TESTS=OFF
        -DLIBXML2_WITH_PROGRAMS=OFF
        -DLIBXML2_WITH_ICONV=OFF
        -DLIBXML2_WITH_ICU=OFF
        -DLIBXML2_WITH_HTTP=OFF
        -DLIBXML2_WITH_LZMA=ON
        -DLIBXML2_WITH_ZLIB=ON
        -DLIBXML2_WITH_THREADS=ON
        -DZLIB_LIBRARY=${XV6_SYSROOT}/lib/libz.a
        -DZLIB_INCLUDE_DIR=${XV6_SYSROOT}/include
        -DLIBLZMA_LIBRARY=${XV6_SYSROOT}/lib/liblzma.a
        -DLIBLZMA_INCLUDE_DIR=${XV6_SYSROOT}/include
)
