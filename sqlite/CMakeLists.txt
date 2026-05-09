# xv6-os sqlite port — standalone CMake project (autosetup upstream).
#
# Mirrors xv6-tmp/user/CMakeLists.txt:1090+ (sqlite section), now built with
# the host compiler/glibc.
#
# Outputs (installed into ${XV6_SYSROOT}):
#   /lib/libsqlite3.a
#   /include/sqlite3.h
#   /bin/sqlite3

cmake_minimum_required(VERSION 3.16)
project(xv6-port-sqlite LANGUAGES NONE)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

foreach(v CMAKE_C_COMPILER CMAKE_AR CMAKE_RANLIB)
    if(NOT ${v} OR ${v} STREQUAL "")
        message(FATAL_ERROR "Port sqlite: ${v} is not set.")
    endif()
endforeach()

xv6_port(
    NAME            sqlite
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    autoconf
    OUTPUT_FILES    lib/libsqlite3.a
                    include/sqlite3.h
    CONFIGURE_ARGS
        --disable-shared
        --disable-readline
        --disable-load-extension
        --disable-math
)
