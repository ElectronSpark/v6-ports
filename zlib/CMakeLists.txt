# xv6-os zlib port — standalone CMake project.
#
# Wraps the vendored zlib source tree (src/) in an xv6_port() call.
# Mirrors xv6-tmp/user/CMakeLists.txt:1290+ (zlib section).
#
# Standalone usage:
#   cmake -S . -B build \
#       -DCMAKE_C_COMPILER=$XV6_TOOLCHAIN_BIN/riscv64-xv6-linux-musl-gcc \
#       -DCMAKE_AR=$XV6_TOOLCHAIN_BIN/riscv64-xv6-linux-musl-ar \
#       -DCMAKE_RANLIB=$XV6_TOOLCHAIN_BIN/riscv64-xv6-linux-musl-ranlib \
#       -DXV6_SYSROOT=$XV6_SYSROOT \
#       -DXV6_PORT_CFLAGS="--sysroot=$XV6_SYSROOT -O2 -fPIC -nostdinc -ffreestanding -march=rv64gc -mabi=lp64d -mcmodel=medany -isystem $XV6_SYSROOT/include"
#   cmake --build build
#
# Outputs (installed into ${XV6_SYSROOT}):
#   /lib/libz.a
#   /include/zlib.h, zconf.h
#
# Umbrella usage: BuildPorts.cmake configures every ports/<name>/ with
# the toolchain + sysroot + CFLAGS already set; this file Just Works.

cmake_minimum_required(VERSION 3.16)

# Use LANGUAGES NONE so cmake doesn't try to compile a C test program
# with the cross compiler at configure time (we delegate compilation
# to the inner zlib build).
project(xv6-port-zlib LANGUAGES NONE)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

set(ZLIB_PREPARED_SOURCE_DIR ${CMAKE_BINARY_DIR}/zlib-src)
file(REMOVE_RECURSE ${ZLIB_PREPARED_SOURCE_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/src/ DESTINATION ${ZLIB_PREPARED_SOURCE_DIR})

if(NOT EXISTS ${ZLIB_PREPARED_SOURCE_DIR}/zconf.h
        AND EXISTS ${ZLIB_PREPARED_SOURCE_DIR}/zconf.h.in)
    configure_file(
        ${ZLIB_PREPARED_SOURCE_DIR}/zconf.h.in
        ${ZLIB_PREPARED_SOURCE_DIR}/zconf.h
        COPYONLY)
endif()

# Sanity: make sure the cross toolchain was passed.
foreach(v CMAKE_C_COMPILER CMAKE_AR CMAKE_RANLIB)
    if(NOT ${v} OR ${v} STREQUAL "")
        message(FATAL_ERROR
            "Port zlib: ${v} is not set. Pass the cross toolchain on "
            "the cmake command line (or via the umbrella).")
    endif()
endforeach()

xv6_port(
    NAME            zlib
    SOURCE_DIR      ${ZLIB_PREPARED_SOURCE_DIR}
    BUILD_SYSTEM    cmake
    OUTPUT_FILES    lib/libz.a
                    include/zlib.h
                    include/zconf.h
    CMAKE_ARGS
        -DZLIB_BUILD_SHARED=OFF
        -DZLIB_BUILD_STATIC=ON
        -DZLIB_BUILD_TESTING=OFF
        -DZLIB_INSTALL=ON
)
