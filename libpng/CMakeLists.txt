cmake_minimum_required(VERSION 3.16)
project(xv6-port-libpng LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

xv6_port(
    NAME            libpng
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    cmake
    DEPENDS         zlib
    OUTPUT_FILES    lib/libpng18.a
                    include/png.h
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=/
        -DPNG_SHARED=OFF
        -DPNG_STATIC=ON
        -DPNG_EXECUTABLES=OFF
        -DPNG_TESTS=OFF
        -DPNG_TOOLS=OFF
        -DZLIB_LIBRARY=${XV6_SYSROOT}/lib/libz.a
        -DZLIB_INCLUDE_DIR=${XV6_SYSROOT}/include
)

# libpng's CMake also installs a libpng.a symlink — nothing extra to do.
