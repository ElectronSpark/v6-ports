# xv6-os libffi port — standalone CMake project (autoconf upstream).
#
# Mirrors xv6-tmp/user/CMakeLists.txt:1422+ (libffi section).
# Upstream is autoconf; xv6_port(autoconf) handles autoreconf-less builds
# but libffi requires `./autogen.sh` first because the tarball-style
# configure isn't checked in. We invoke autogen.sh in a configure-time
# wrapper.
#
# Outputs (installed into ${XV6_SYSROOT}):
#   /lib/libffi.a
#   /include/ffi.h, ffitarget.h

cmake_minimum_required(VERSION 3.16)
project(xv6-port-libffi LANGUAGES NONE)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

foreach(v CMAKE_C_COMPILER CMAKE_AR CMAKE_RANLIB)
    if(NOT ${v} OR ${v} STREQUAL "")
        message(FATAL_ERROR "Port libffi: ${v} is not set.")
    endif()
endforeach()

# libffi ships configure.ac but not the generated configure script.
# Run autogen.sh once into the source tree before invoking the port.
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/configure)
    message(STATUS "Port libffi: running autogen.sh (one-time)")
    execute_process(
        COMMAND bash autogen.sh
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src
        RESULT_VARIABLE _rc
        OUTPUT_QUIET ERROR_QUIET)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR
            "Port libffi: autogen.sh failed (rc=${_rc}). "
            "Install autoconf, automake, libtool and re-configure.")
    endif()
endif()

xv6_port(
    NAME            libffi
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    autoconf
    OUTPUT_FILES    lib/libffi.a
                    include/ffi.h
                    include/ffitarget.h
    CONFIGURE_ARGS
        --disable-shared
        --enable-static
        --disable-docs
        --disable-multi-os-directory
        # tramp.c #includes <linux/limits.h>; keep the xv6 sysroot small and
        # disable static-exec trampolines.
        --disable-exec-static-tramp
)
