# xv6-os ncurses port — standalone CMake project (autoconf upstream).
#
# Builds static libncurses.a and headers into ${XV6_SYSROOT}.
# Mirrors the static branch of xv6-tmp/user/CMakeLists.txt:357+ (ncurses).
#
# Outputs (installed into ${XV6_SYSROOT}):
#   /lib/libncurses.a
#   /include/curses.h, /include/ncurses.h (symlink), term.h, ...

cmake_minimum_required(VERSION 3.16)
project(xv6-port-ncurses LANGUAGES NONE)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

foreach(v CMAKE_C_COMPILER CMAKE_AR CMAKE_RANLIB)
    if(NOT ${v} OR ${v} STREQUAL "")
        message(FATAL_ERROR "Port ncurses: ${v} is not set.")
    endif()
endforeach()

xv6_port(
    NAME            ncurses
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    autoconf
    JOBS            1
    OUTPUT_FILES    lib/libncurses.a
                    include/curses.h
    CONFIGURE_ARGS
        --disable-widec
        --without-shared
        --without-debug
        --without-ada
        --without-cxx
        --without-cxx-binding
        --without-manpages
        --without-progs
        --without-tests
        --with-fallbacks=xterm,xterm-256color,vt100,dumb
        cf_cv_have_tcgetattr=yes
        ac_cv_header_termios_h=yes
)

# Post-install: create ncurses.h symlink (CPython's py_curses.h expects it).
add_custom_command(
    OUTPUT  ${XV6_SYSROOT}/include/ncurses.h
    COMMAND ${CMAKE_COMMAND} -E create_symlink curses.h
            ${XV6_SYSROOT}/include/ncurses.h
    DEPENDS ${XV6_SYSROOT}/include/curses.h
    COMMENT "Creating ncurses.h -> curses.h symlink")
add_custom_target(port-ncurses-symlink ALL
    DEPENDS ${XV6_SYSROOT}/include/ncurses.h)
add_dependencies(port-ncurses-symlink port-ncurses)
