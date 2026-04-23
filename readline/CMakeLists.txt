# xv6-os readline port — standalone CMake project (autoconf upstream).
#
# Builds static libreadline.a + libhistory.a into ${XV6_SYSROOT}.
# Links against ncurses (port-ncurses must run first).
# Mirrors the static branch of xv6-tmp/user/CMakeLists.txt:478+ (readline).
#
# Outputs (installed into ${XV6_SYSROOT}):
#   /lib/libreadline.a, /lib/libhistory.a
#   /include/readline/readline.h, history.h, ...

cmake_minimum_required(VERSION 3.16)
project(xv6-port-readline LANGUAGES NONE)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

foreach(v CMAKE_C_COMPILER CMAKE_AR CMAKE_RANLIB)
    if(NOT ${v} OR ${v} STREQUAL "")
        message(FATAL_ERROR "Port readline: ${v} is not set.")
    endif()
endforeach()

# readline's configure probes _POSIX_VERSION which musl sets to a value
# its old config layout doesn't recognise, plus needs explicit hints
# for ncurses-based termcap and a few sigsetjmp/strcoll quirks.
xv6_port(
    NAME            readline
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    autoconf
    DEPENDS         ncurses
    OUTPUT_FILES    lib/libreadline.a
                    lib/libhistory.a
                    include/readline/readline.h
                    include/readline/history.h
    CONFIGURE_ARGS
        --disable-shared
        --enable-static
        --with-curses
        bash_cv_termcap_lib=libncurses
        bash_cv_wcwidth_broken=no
        bash_cv_func_sigsetjmp=missing
        bash_cv_signal_vintage=posix
        bash_cv_posix_signals=yes
        bash_cv_func_strcoll_broken=no
        ac_cv_header_termios_h=yes
        ac_cv_func_tcgetattr=yes
        ac_cv_func_pselect=yes
)
