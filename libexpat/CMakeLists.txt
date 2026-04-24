cmake_minimum_required(VERSION 3.16)
project(xv6-port-libexpat LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

# libexpat's CMakeLists.txt lives in src/expat/, not at src/ root.
xv6_port(
    NAME            libexpat
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src/expat
    BUILD_SYSTEM    cmake
    OUTPUT_FILES    lib/libexpat.a
                    include/expat.h
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=/
        -DEXPAT_SHARED_LIBS=OFF
        -DEXPAT_BUILD_TESTS=OFF
        -DEXPAT_BUILD_TOOLS=OFF
        -DEXPAT_BUILD_EXAMPLES=OFF
        -DEXPAT_BUILD_DOCS=OFF
)
