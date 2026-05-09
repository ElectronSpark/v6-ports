# xv6-os bzip2 port — standalone CMake project.
#
# Wraps the vendored bzip2 source tree (src/) in an xv6_port() call.
# Mirrors xv6-tmp/user/CMakeLists.txt:1330+ (bzip2 section), now built with
# the host compiler/glibc.
#
# Outputs (installed into ${XV6_SYSROOT}):
#   /lib/libbz2_static.a   (alias of libbz2.a, see install rename below)
#   /include/bzlib.h

cmake_minimum_required(VERSION 3.16)
project(xv6-port-bzip2 LANGUAGES NONE)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

foreach(v CMAKE_C_COMPILER CMAKE_AR CMAKE_RANLIB)
    if(NOT ${v} OR ${v} STREQUAL "")
        message(FATAL_ERROR
            "Port bzip2: ${v} is not set. Pass compiler tools on "
            "the cmake command line (or via the umbrella).")
    endif()
endforeach()

xv6_port(
    NAME            bzip2
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    cmake
    OUTPUT_FILES    lib/libbz2_static.a
                    include/bzlib.h
    CMAKE_ARGS
        # Override AddPort's CMAKE_INSTALL_PREFIX=/ — bzip2's man/CMakeLists.txt
        # hardcodes `${CMAKE_INSTALL_PREFIX}/man/man1` which yields the invalid
        # network path "//man/man1" under DESTDIR. Using /usr makes it valid
        # (lib/include are still pinned to /lib /include via *_LIBDIR/_INCLUDEDIR).
        -DCMAKE_INSTALL_PREFIX=/usr
        -DENABLE_LIB_ONLY=ON
        -DENABLE_APP=OFF
        -DENABLE_EXAMPLES=OFF
        -DENABLE_DOCS=OFF
        -DENABLE_SHARED_LIB=OFF
        -DENABLE_STATIC_LIB=ON
)

# bzip2 installs only `libbz2_static.a`. Many downstream ports
# (cairo via fontconfig, freetype, etc.) link with -lbz2, so create a
# stable symlink in the sysroot.
add_custom_command(
    OUTPUT  ${XV6_SYSROOT}/lib/libbz2.a
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            libbz2_static.a ${XV6_SYSROOT}/lib/libbz2.a
    DEPENDS port-bzip2
    COMMENT "Creating libbz2.a -> libbz2_static.a symlink")
add_custom_target(port-bzip2-symlink ALL
    DEPENDS ${XV6_SYSROOT}/lib/libbz2.a)
