# xv6-os cpython port — from-source CPython 3.12 host-glibc build.
#
# Builds CPython 3.12 (from `src/` submodule, branch v6-3.12) against the
# from-source ports already installed in ${XV6_SYSROOT}: openssl, sqlite,
# ncurses, readline, libffi, zlib, bzip2, xz.
#
# Outputs into ${XV6_SYSROOT}:
#   bin/python3.12, bin/python3 (symlink), bin/python (symlink)
#   lib/libpython3.12.so.1.0 + libpython3.so symlink
#   lib/python3.12/                — pure-python stdlib
#   lib/python3.12/lib-dynload/    — extension modules (.so)
#   include/python3.12/            — public headers (Python.h)
#
# Standalone usage:
#   cmake -S . -B build \
#       -DXV6_SYSROOT=/path/to/sysroot \
#       -DXV6_REPO_ROOT=/path/to/xv6-os
#   cmake --build build

cmake_minimum_required(VERSION 3.16)
project(xv6-port-cpython LANGUAGES NONE)

# ---------------------------------------------------------------------
# Required inputs
# ---------------------------------------------------------------------
if(NOT DEFINED XV6_SYSROOT OR XV6_SYSROOT STREQUAL "")
    message(FATAL_ERROR "ports/cpython: -DXV6_SYSROOT=<path> required")
endif()
if(NOT DEFINED CMAKE_C_COMPILER OR CMAKE_C_COMPILER STREQUAL "")
    message(FATAL_ERROR "ports/cpython: -DCMAKE_C_COMPILER=<host-cc> required")
endif()

# Source dir (submodule).
set(_src "${CMAKE_CURRENT_SOURCE_DIR}/src")
if(NOT EXISTS "${_src}/configure")
    message(FATAL_ERROR
        "ports/cpython: src/configure missing — run\n"
        "  git submodule update --init --recursive ports/cpython/src")
endif()

include(ProcessorCount)
if(NOT DEFINED XV6_PARALLEL_JOBS OR XV6_PARALLEL_JOBS EQUAL 0)
    ProcessorCount(XV6_PARALLEL_JOBS)
    if(XV6_PARALLEL_JOBS EQUAL 0)
        set(XV6_PARALLEL_JOBS 4)
    endif()
endif()

find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter)

# Output build dir (out-of-tree).
set(_build "${CMAKE_CURRENT_BINARY_DIR}/build")
file(MAKE_DIRECTORY "${_build}")
file(MAKE_DIRECTORY "${_build}/Modules")

# ---------------------------------------------------------------------
# Setup.local — link-time hints for extension modules.
# We set explicit -l flags for modules whose autoprobe doesn't reliably
# detect staged libs.
# ---------------------------------------------------------------------
set(_setup_local "${_build}/Modules/Setup.local")
set(_setup_local_content
"*shared*
_curses _cursesmodule.c -I${XV6_SYSROOT}/include -L${XV6_SYSROOT}/lib -lncurses
_ssl _ssl.c -I${XV6_SYSROOT}/include -L${XV6_SYSROOT}/lib -lssl -lcrypto
_hashlib _hashopenssl.c -I${XV6_SYSROOT}/include -L${XV6_SYSROOT}/lib -lcrypto
")
file(WRITE "${_setup_local}" "${_setup_local_content}")
add_custom_command(
    OUTPUT  ${_setup_local}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_build}/Modules
    COMMAND ${CMAKE_COMMAND}
            -DOUTPUT=${_setup_local}
            -DXV6_SYSROOT=${XV6_SYSROOT}
            -P ${CMAKE_CURRENT_LIST_DIR}/write-setup-local.cmake
    COMMENT "[cpython] write Modules/Setup.local"
    VERBATIM)
add_custom_target(cpython-setup-local DEPENDS ${_setup_local})

# ---------------------------------------------------------------------
# CFLAGS / LDFLAGS / LIBS for host-glibc configure. libc comes from the host;
# libraries from other ports are found under XV6_SYSROOT.
# ---------------------------------------------------------------------
set(_cppflags
    "-I${XV6_SYSROOT}/include"
    "-D_POSIX_C_SOURCE=200809L")
string(REPLACE ";" " " _cppflags "${_cppflags}")

set(_cflags "${_cppflags} -O2 -fPIC")

set(_ldflags
    "-L${XV6_SYSROOT}/lib"
    "-Wl,-rpath,/lib")
string(REPLACE ";" " " _ldflags "${_ldflags}")

# Modules linked into libpython require these for the curses+readline
# combo (readline.a depends on ncurses symbols).
set(_libs "-lreadline -lncurses -lpthread -lm")

# ---------------------------------------------------------------------
# Configure CPython.
# ---------------------------------------------------------------------
set(_makefile "${_build}/Makefile")
add_custom_command(
    OUTPUT  ${_makefile}
    WORKING_DIRECTORY ${_build}
    COMMAND ${CMAKE_COMMAND} -E env
                CC=${CMAKE_C_COMPILER}
                AR=${CMAKE_AR}
                RANLIB=${CMAKE_RANLIB}
                PKG_CONFIG=false
                "CPPFLAGS=${_cppflags}"
                "CFLAGS=${_cflags}"
                "LDFLAGS=${_ldflags}"
                "LIBS=${_libs}"
                "LIBFFI_CFLAGS=-I${XV6_SYSROOT}/include"
                "LIBFFI_LIBS=-L${XV6_SYSROOT}/lib -lffi"
            ${_src}/configure
                --prefix=/
                --enable-shared
                --without-static-libpython
                --without-ensurepip
                --disable-test-modules
                --with-system-ffi
                --with-openssl=${XV6_SYSROOT}
                ac_cv_file__dev_ptmx=no
                ac_cv_file__dev_ptc=no
                ac_cv_header_readline_readline_h=yes
                ac_cv_lib_readline_readline=yes
                ac_cv_readline_append_history=yes
                ac_cv_readline_rl_completion_display_matches_hook=yes
                ac_cv_readline_rl_completion_matches=yes
                ac_cv_readline_rl_pre_input_hook=yes
                ac_cv_readline_rl_resize_terminal=yes
                ac_cv_func_select=yes
                ac_cv_func_pselect=yes
                ac_cv_search_dlopen=none\ required
    DEPENDS ${_src}/configure ${_setup_local}
    COMMENT "[cpython] configure (host glibc)"
    VERBATIM)
add_custom_target(cpython-configure DEPENDS ${_makefile})

# ---------------------------------------------------------------------
# Build CPython (the python binary + sharedmods).
# ---------------------------------------------------------------------
set(_python_bin   "${_build}/python")
set(_libpython_so "${_build}/libpython3.12.so.1.0")
set(_build_stamp  "${CMAKE_CURRENT_BINARY_DIR}/.cpython_built")

set(_syslibs "-lreadline -lncurses -lm -lpthread")

add_custom_command(
    OUTPUT  ${_python_bin} ${_libpython_so} ${_build_stamp}
    WORKING_DIRECTORY ${_build}
    COMMAND make -j${XV6_PARALLEL_JOBS} python "SYSLIBS=${_syslibs}"
    COMMAND make -j${XV6_PARALLEL_JOBS} sharedmods "SYSLIBS=${_syslibs}"
    COMMAND ${CMAKE_COMMAND} -E touch ${_build_stamp}
    DEPENDS ${_makefile}
    COMMENT "[cpython] build (j=${XV6_PARALLEL_JOBS})"
    VERBATIM)

# ---------------------------------------------------------------------
# Install: copy libpython, ext modules, stdlib, headers, binary into
# ${XV6_SYSROOT}.
# ---------------------------------------------------------------------
set(_install_stamp "${CMAKE_CURRENT_BINARY_DIR}/.cpython_installed")
add_custom_command(
    OUTPUT  ${_install_stamp}
    WORKING_DIRECTORY ${_build}
    # libpython shared lib(s)
    COMMAND ${CMAKE_COMMAND} -E make_directory ${XV6_SYSROOT}/lib
    COMMAND bash -c "cp -P libpython3.12.so* ${XV6_SYSROOT}/lib/ 2>/dev/null || true"
    COMMAND bash -c "cp -P libpython3.so* ${XV6_SYSROOT}/lib/ 2>/dev/null || true"
    # python binary
    COMMAND ${CMAKE_COMMAND} -E make_directory ${XV6_SYSROOT}/bin
    COMMAND ${CMAKE_COMMAND} -E copy ${_python_bin} ${XV6_SYSROOT}/bin/python3.12
    COMMAND ${CMAKE_COMMAND} -E create_symlink python3.12 ${XV6_SYSROOT}/bin/python3
    COMMAND ${CMAKE_COMMAND} -E create_symlink python3   ${XV6_SYSROOT}/bin/python
    # extension modules
    COMMAND ${CMAKE_COMMAND} -E make_directory ${XV6_SYSROOT}/lib/python3.12/lib-dynload
    COMMAND bash -c "cp -P ${_build}/Modules/*.cpython-*.so ${XV6_SYSROOT}/lib/python3.12/lib-dynload/ 2>/dev/null || true"
    # pure-python stdlib
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${_src}/Lib ${XV6_SYSROOT}/lib/python3.12
    # public headers
    COMMAND ${CMAKE_COMMAND} -E make_directory ${XV6_SYSROOT}/include/python3.12
    COMMAND bash -c "cp -RP ${_src}/Include/. ${XV6_SYSROOT}/include/python3.12/"
    COMMAND ${CMAKE_COMMAND} -E copy ${_build}/pyconfig.h ${XV6_SYSROOT}/include/python3.12/pyconfig.h
    COMMAND ${CMAKE_COMMAND} -E touch ${_install_stamp}
    DEPENDS ${_build_stamp}
    COMMENT "[cpython] install -> ${XV6_SYSROOT}"
    VERBATIM)

add_custom_target(port-cpython ALL DEPENDS ${_install_stamp})

# Inter-port deps: cpython links against many ports' static libs at
# configure-time (its `checking whether the C compiler works` test
# already pulls -lreadline -lncurses -lpthread -lm). Make sure those
# ports are installed into the sysroot before we configure.
foreach(_d IN ITEMS readline ncurses openssl sqlite libffi zlib bzip2 xz)
    if(TARGET port-${_d})
        add_dependencies(port-cpython port-${_d})
    endif()
endforeach()

add_custom_target(port-cpython-clean
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${_build}
    COMMAND ${CMAKE_COMMAND} -E rm -f
                ${_build_stamp}
                ${_install_stamp}
                ${XV6_SYSROOT}/bin/python3.12
                ${XV6_SYSROOT}/bin/python3
                ${XV6_SYSROOT}/bin/python
    COMMAND bash -c "rm -f ${XV6_SYSROOT}/lib/libpython3.12.so* ${XV6_SYSROOT}/lib/libpython3.so*"
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${XV6_SYSROOT}/lib/python3.12 ${XV6_SYSROOT}/include/python3.12
    COMMENT "[cpython] clean")
