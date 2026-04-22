# xv6-os cpython port — pragmatic stage-from-prebuilt recipe.
#
# This port currently does NOT compile CPython from source. Instead it
# wraps scripts/stage-cpython.sh which copies a known-good ABI-compatible
# CPython 3.12 + Flask + SQLAlchemy + SQLite stack from a reference
# sysroot (REF) into ${XV6_SYSROOT}.
#
# Why a stage-only port: building cpython end-to-end needs ~7 dep ports
# (openssl, sqlite, libffi, ncurses, readline, bzip2, xz) each with
# 50–300 lines of cross-compile flags lifted from xv6-tmp. That work is
# the "real port" follow-up; this port exists so the umbrella has a
# single, deterministic dependency edge from `world` -> `python`.
#
# Inputs (cache vars, with defaults):
#   CPYTHON_REF_SYSROOT  Reference sysroot to copy artefacts FROM.
#                        Default: /home/es/xv6/xv6-tmp/build-x86/sysroot
#   CPYTHON_LIB          Pure-python stdlib source dir (cpython/Lib).
#                        Default: /home/es/xv6/xv6-tmp/user/v6-cpython/Lib
#   PHASE2_LIB           Our toolchain's musl libc.so dir (auto-set via
#                        XV6_TOOLCHAIN_BIN by the umbrella).
#
# Outputs (in ${XV6_SYSROOT}):
#   bin/python3.12, bin/python (symlink)
#   lib/libpython3.12.so.1.0, lib/libpython3.so
#   lib/libgcc_s.so.1, lib/libreadline.so.8.2, lib/libncurses.so.6.4,
#   lib/libstdc++.so.6.0.33, lib/libffi.so.7.1.0
#   lib/python3.12/ (stdlib + lib-dynload + site-packages)
#   lib/libc.so, lib/ld-musl-x86_64.so.1 (linker + libc)
#
# Standalone usage (without umbrella):
#   cmake -S . -B build -DXV6_SYSROOT=/path/to/sysroot \
#       -DPHASE2_LIB=/path/to/toolchain/x86_64-xv6-linux-musl/lib
#   cmake --build build

cmake_minimum_required(VERSION 3.16)
project(xv6-port-cpython LANGUAGES NONE)

if(NOT DEFINED XV6_SYSROOT OR XV6_SYSROOT STREQUAL "")
    message(FATAL_ERROR
        "ports/cpython: -DXV6_SYSROOT=<path> required")
endif()

# REF: prebuilt sysroot we copy from. Override via -DCPYTHON_REF_SYSROOT=...
set(CPYTHON_REF_SYSROOT
    "/home/es/xv6/xv6-tmp/build-x86/sysroot"
    CACHE PATH "Reference sysroot containing prebuilt cpython 3.12 + Flask")

set(CPYTHON_LIB
    "/home/es/xv6/xv6-tmp/user/v6-cpython/Lib"
    CACHE PATH "Path to cpython 3.12 source tree's Lib/ directory (pure-python stdlib)")

# PHASE2_LIB: directory containing musl's libc.so for our toolchain.
# The umbrella sets this via -DPHASE2_LIB=...; standalone users must too.
if(NOT DEFINED PHASE2_LIB OR PHASE2_LIB STREQUAL "")
    message(FATAL_ERROR
        "ports/cpython: -DPHASE2_LIB=<path-to-toolchain-x86_64-xv6-linux-musl-lib> required")
endif()

# ---------------------------------------------------------------------
# Sanity checks at configure time
# ---------------------------------------------------------------------
if(NOT EXISTS "${CPYTHON_REF_SYSROOT}/lib/python3.12")
    message(FATAL_ERROR
        "ports/cpython: reference sysroot has no lib/python3.12 at "
        "${CPYTHON_REF_SYSROOT}. Set CPYTHON_REF_SYSROOT or build CPython first.")
endif()
if(NOT EXISTS "${PHASE2_LIB}/libc.so")
    message(FATAL_ERROR
        "ports/cpython: musl libc.so not found at ${PHASE2_LIB}")
endif()
if(NOT EXISTS "${CPYTHON_LIB}/traceback.py")
    message(WARNING
        "ports/cpython: stdlib source dir ${CPYTHON_LIB} has no traceback.py — "
        "Python imports of stdlib modules will fail at runtime.")
endif()
find_program(STAGE_RSYNC rsync REQUIRED)

# ---------------------------------------------------------------------
# Driver: invoke scripts/stage-cpython.sh through CMake so the work is
# part of the build graph (re-runs whenever the script or env changes).
# We could inline the rsync/cp commands here, but the script already
# encapsulates the recipe and is self-documenting.
# ---------------------------------------------------------------------
set(_stamp "${CMAKE_CURRENT_BINARY_DIR}/staged.stamp")
set(_script "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/stage-cpython.sh")
get_filename_component(_script "${_script}" ABSOLUTE)
if(NOT EXISTS "${_script}")
    message(FATAL_ERROR "ports/cpython: stage-cpython.sh missing at ${_script}")
endif()

# Sentinel file the staging produces; used as the OUTPUT for incremental.
set(_sentinel "${XV6_SYSROOT}/bin/python3.12")

add_custom_command(
    OUTPUT  ${_sentinel} ${_stamp}
    COMMAND ${CMAKE_COMMAND} -E env
                CPYTHON_LIB=${CPYTHON_LIB}
            bash ${_script} ${CPYTHON_REF_SYSROOT} ${XV6_SYSROOT} ${PHASE2_LIB}
    COMMAND ${CMAKE_COMMAND} -E touch ${_stamp}
    DEPENDS ${_script}
    COMMENT "Staging CPython 3.12 + Flask stack into ${XV6_SYSROOT}"
    VERBATIM)

add_custom_target(port-cpython ALL DEPENDS ${_sentinel} ${_stamp})

add_custom_target(port-cpython-clean
    COMMAND ${CMAKE_COMMAND} -E remove -f
                ${_sentinel}
                ${_stamp}
                ${XV6_SYSROOT}/bin/python
                ${XV6_SYSROOT}/lib/libpython3.12.so.1.0
                ${XV6_SYSROOT}/lib/libpython3.so
                ${XV6_SYSROOT}/lib/libffi.so.7.1.0
                ${XV6_SYSROOT}/lib/libreadline.so.8.2
                ${XV6_SYSROOT}/lib/libncurses.so.6.4
                ${XV6_SYSROOT}/lib/libstdc++.so.6.0.33
                ${XV6_SYSROOT}/lib/libgcc_s.so.1
                ${XV6_SYSROOT}/lib/libc.so
                ${XV6_SYSROOT}/lib/ld-musl-x86_64.so.1
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${XV6_SYSROOT}/lib/python3.12
    COMMENT "Removing staged cpython artefacts from ${XV6_SYSROOT}")
