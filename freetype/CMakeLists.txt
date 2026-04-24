cmake_minimum_required(VERSION 3.16)
project(xv6-port-freetype LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

xv6_port(
    NAME            freetype
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    cmake
    DEPENDS         zlib bzip2 libpng
    OUTPUT_FILES    lib/libfreetype.a
                    include/freetype2/ft2build.h
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=/
        -DBUILD_SHARED_LIBS=OFF
        -DFT_DISABLE_BROTLI=ON
        -DFT_DISABLE_HARFBUZZ=ON
        -DFT_REQUIRE_ZLIB=ON
        -DFT_REQUIRE_PNG=ON
        -DFT_REQUIRE_BZIP2=ON
        -DZLIB_LIBRARY=${XV6_SYSROOT}/lib/libz.a
        -DZLIB_INCLUDE_DIR=${XV6_SYSROOT}/include
        -DPNG_LIBRARY=${XV6_SYSROOT}/lib/libpng.a
        -DPNG_PNG_INCLUDE_DIR=${XV6_SYSROOT}/include
        -DBZIP2_LIBRARIES=${XV6_SYSROOT}/lib/libbz2_static.a
        -DBZIP2_LIBRARY_RELEASE=${XV6_SYSROOT}/lib/libbz2_static.a
        -DBZIP2_INCLUDE_DIR=${XV6_SYSROOT}/include
)
