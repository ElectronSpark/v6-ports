# xv6-os xz/liblzma port — standalone CMake project.
#
# Mirrors xv6-tmp/user/CMakeLists.txt:1380+ (xz section).
# Outputs (installed into ${XV6_SYSROOT}):
#   /lib/liblzma.a
#   /include/lzma.h, lzma/*.h

cmake_minimum_required(VERSION 3.16)
project(xv6-port-xz LANGUAGES NONE)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

foreach(v CMAKE_C_COMPILER CMAKE_AR CMAKE_RANLIB)
    if(NOT ${v} OR ${v} STREQUAL "")
        message(FATAL_ERROR "Port xz: ${v} is not set.")
    endif()
endforeach()

xv6_port(
    NAME            xz
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    cmake
    OUTPUT_FILES    lib/liblzma.a
                    include/lzma.h
    CMAKE_ARGS
        -DBUILD_SHARED_LIBS=OFF
        -DXZ_NLS=OFF
        -DXZ_THREADS=no
        -DXZ_SYMBOL_VERSIONING=no
        -DXZ_TOOL_XZ=OFF
        -DXZ_TOOL_XZDEC=OFF
        -DXZ_TOOL_LZMADEC=OFF
        -DXZ_TOOL_LZMAINFO=OFF
        -DXZ_TOOL_SCRIPTS=OFF
        -DXZ_DOC=OFF
)
