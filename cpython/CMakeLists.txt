# xv6-os cpython port — from-source CPython 3.12 cross build.
#
# Builds CPython 3.12 (from `src/` submodule, branch v6-3.12) against the
# from-source ports already installed in ${XV6_SYSROOT}: openssl, sqlite,
# ncurses, readline, libffi, zlib, bzip2, xz. The cross toolchain
# (x86_64-xv6-linux-musl-gcc) provides musl libc + sysroot.
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
#       -DCMAKE_C_COMPILER=/path/to/x86_64-xv6-linux-musl-gcc \
#       -DXV6_SYSROOT=/path/to/sysroot \
#       -DPHASE2_LIB=/path/to/toolchain/x86_64-xv6-linux-musl/lib \
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
    message(FATAL_ERROR "ports/cpython: -DCMAKE_C_COMPILER=<cross-gcc> required")
endif()

# Repo root — for finding toolchain/musl-xv6/compat (kqueue, etc).
# Default: ports/cpython/../.. = repo root.
get_filename_component(_default_repo_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(XV6_REPO_ROOT "${_default_repo_root}"
    CACHE PATH "xv6-os repo root (for musl-xv6/compat sources)")

set(_compat_dir "${XV6_REPO_ROOT}/toolchain/musl-xv6/compat")
if(NOT EXISTS "${_compat_dir}/kqueue.c")
    message(FATAL_ERROR
        "ports/cpython: musl-xv6/compat sources missing at ${_compat_dir}")
endif()

# Source dir (submodule).
set(_src "${CMAKE_CURRENT_SOURCE_DIR}/src")
if(NOT EXISTS "${_src}/configure")
    message(FATAL_ERROR
        "ports/cpython: src/configure missing — run\n"
        "  git submodule update --init --recursive ports/cpython/src")
endif()

# Cross toolchain + naming
get_filename_component(_cc_realpath "${CMAKE_C_COMPILER}" REALPATH)
get_filename_component(_cc_dir       "${_cc_realpath}"     DIRECTORY)
get_filename_component(_cc_name      "${_cc_realpath}"     NAME)
string(REGEX REPLACE "gcc$" ""  _xv6_prefix "${_cc_name}")  # e.g. x86_64-xv6-linux-musl-
set(_cross_cc      "${_cc_dir}/${_xv6_prefix}gcc")
set(_cross_ar      "${_cc_dir}/${_xv6_prefix}ar")
set(_cross_ranlib  "${_cc_dir}/${_xv6_prefix}ranlib")
set(_cross_readelf "${_cc_dir}/${_xv6_prefix}readelf")

# Host triple: derived from prefix (strip trailing dash).
string(REGEX REPLACE "-$" "" _xv6_triple "${_xv6_prefix}")
# CPython config.sub doesn't know "*-xv6-linux-musl"; use a triple it accepts.
if(_xv6_triple MATCHES "^x86_64")
    set(_cpy_host    "x86_64-unknown-linux-musl")
    set(_cpy_dynld   "/lib/ld-musl-x86_64.so.1")
elseif(_xv6_triple MATCHES "^riscv64")
    set(_cpy_host    "riscv64-unknown-linux-musl")
    set(_cpy_dynld   "/lib/ld-musl-riscv64.so.1")
else()
    message(FATAL_ERROR "ports/cpython: unsupported triple ${_xv6_triple}")
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
# kqueue_compat.o — provides kqueue/kevent for musl on xv6.
# ---------------------------------------------------------------------
set(_kqueue_obj "${CMAKE_CURRENT_BINARY_DIR}/kqueue_compat.o")
add_custom_command(
    OUTPUT  ${_kqueue_obj}
    COMMAND ${_cross_cc}
            -O2 -fPIC
            -isystem ${XV6_SYSROOT}/include
            -isystem ${_compat_dir}
            -c -o ${_kqueue_obj}
            ${_compat_dir}/kqueue.c
    DEPENDS ${_compat_dir}/kqueue.c
    COMMENT "[cpython] kqueue_compat.o"
    VERBATIM)

# ---------------------------------------------------------------------
# Setup.local — link-time hints for cross-build extension modules.
# We set explicit -l flags for modules whose autoprobe doesn't reliably
# detect cross-compiled libs.
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
# CFLAGS / LDFLAGS / LIBS for cross configure.
# We rely on the cross toolchain's built-in musl sysroot for libc; only
# add explicit -I/-L for ports installed under XV6_SYSROOT.
# ---------------------------------------------------------------------
set(_cppflags
    "-I${XV6_SYSROOT}/include"
    "-isystem ${_compat_dir}"
    "-D_POSIX_C_SOURCE=200809L")
string(REPLACE ";" " " _cppflags "${_cppflags}")

set(_cflags "${_cppflags} -O2 -fPIC")

set(_ldflags
    "-L${XV6_SYSROOT}/lib"
    "-Wl,-rpath,/lib"
    "-Wl,--dynamic-linker=${_cpy_dynld}")
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
                CC=${_cross_cc}
                AR=${_cross_ar}
                RANLIB=${_cross_ranlib}
                READELF=${_cross_readelf}
                PKG_CONFIG=false
                "CPPFLAGS=${_cppflags}"
                "CFLAGS=${_cflags}"
                "LDFLAGS=${_ldflags}"
                "LIBS=${_libs}"
                "LIBFFI_CFLAGS=-I${XV6_SYSROOT}/include"
                "LIBFFI_LIBS=-L${XV6_SYSROOT}/lib -lffi"
            ${_src}/configure
                --host=${_cpy_host}
                --build=x86_64-pc-linux-gnu
                --prefix=/
                --enable-shared
                --without-static-libpython
                --without-ensurepip
                --disable-test-modules
                --disable-ipv6
                --with-build-python=${Python3_EXECUTABLE}
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
                ac_cv_header_sys_event_h=yes
                ac_cv_func_kqueue=yes
                ac_cv_func_select=yes
                ac_cv_func_pselect=yes
                ac_cv_search_dlopen=none\ required
    DEPENDS ${_src}/configure ${_setup_local}
    COMMENT "[cpython] configure (cross, host=${_cpy_host})"
    VERBATIM)
add_custom_target(cpython-configure DEPENDS ${_makefile})

# ---------------------------------------------------------------------
# Build CPython (the python binary + sharedmods, with kqueue_compat.o
# linked into SYSLIBS so the kqueue extension module resolves).
# ---------------------------------------------------------------------
set(_python_bin   "${_build}/python")
set(_libpython_so "${_build}/libpython3.12.so.1.0")
set(_build_stamp  "${CMAKE_CURRENT_BINARY_DIR}/.cpython_built")

set(_syslibs "${_kqueue_obj} -lreadline -lncurses -lm -lpthread")

add_custom_command(
    OUTPUT  ${_python_bin} ${_libpython_so} ${_build_stamp}
    WORKING_DIRECTORY ${_build}
    COMMAND make -j${XV6_PARALLEL_JOBS} python "SYSLIBS=${_syslibs}"
    COMMAND make -j${XV6_PARALLEL_JOBS} sharedmods "SYSLIBS=${_syslibs}"
    COMMAND ${CMAKE_COMMAND} -E touch ${_build_stamp}
    DEPENDS ${_makefile} ${_kqueue_obj}
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
                ${_kqueue_obj}
                ${XV6_SYSROOT}/bin/python3.12
                ${XV6_SYSROOT}/bin/python3
                ${XV6_SYSROOT}/bin/python
    COMMAND bash -c "rm -f ${XV6_SYSROOT}/lib/libpython3.12.so* ${XV6_SYSROOT}/lib/libpython3.so*"
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${XV6_SYSROOT}/lib/python3.12 ${XV6_SYSROOT}/include/python3.12
    COMMENT "[cpython] clean")
