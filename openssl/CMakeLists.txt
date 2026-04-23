# xv6-os openssl port — standalone CMake project (perl Configure upstream).
#
# Builds static libcrypto.a + libssl.a into ${XV6_SYSROOT}.
# Mirrors xv6-tmp/user/CMakeLists.txt:720+ (openssl section), simplified
# because the toolchain ships its own working musl sysroot — no manual
# CRT/dynamic-linker LDFLAGS needed.
#
# OpenSSL's build system is not autoconf/cmake — we drive its perl
# Configure script directly via add_custom_command (xv6_port doesn't
# model this build system).
#
# Outputs (installed into ${XV6_SYSROOT}):
#   /lib/libcrypto.a, /lib/libssl.a
#   /include/openssl/*.h

cmake_minimum_required(VERSION 3.16)
project(xv6-port-openssl LANGUAGES NONE)

if(NOT DEFINED XV6_SYSROOT OR XV6_SYSROOT STREQUAL "")
    message(FATAL_ERROR "Port openssl: -DXV6_SYSROOT=<path> required")
endif()
foreach(v CMAKE_C_COMPILER CMAKE_AR CMAKE_RANLIB)
    if(NOT ${v} OR ${v} STREQUAL "")
        message(FATAL_ERROR "Port openssl: ${v} is not set.")
    endif()
endforeach()

find_program(PERL_PROG perl REQUIRED)

set(_src   ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(_build ${CMAKE_BINARY_DIR}/openssl-build)

# Strip the trailing 'gcc' off CMAKE_C_COMPILER to derive CROSS_COMPILE.
get_filename_component(_cc_path "${CMAKE_C_COMPILER}" REALPATH)
get_filename_component(_cc_dir  "${_cc_path}" DIRECTORY)
get_filename_component(_cc_name "${_cc_path}" NAME)
string(REGEX REPLACE "gcc$" "" _cc_stem "${_cc_name}")
set(_cross_prefix "${_cc_dir}/${_cc_stem}")

# Detect arch from compiler name (x86_64-... or riscv64-...).
if(_cc_name MATCHES "^x86_64")
    set(_openssl_target "linux-x86_64")
    set(_openssl_asm_opt "")
elseif(_cc_name MATCHES "^riscv64")
    set(_openssl_target "linux64-riscv64")
    set(_openssl_asm_opt "no-asm")
else()
    message(FATAL_ERROR "Port openssl: unknown target arch in ${_cc_name}")
endif()

set(_libcrypto ${XV6_SYSROOT}/lib/libcrypto.a)
set(_libssl    ${XV6_SYSROOT}/lib/libssl.a)
set(_header    ${XV6_SYSROOT}/include/openssl/ssl.h)

# Parallel jobs (umbrella sets XV6_PARALLEL_JOBS; standalone falls back).
if(NOT DEFINED XV6_PARALLEL_JOBS OR XV6_PARALLEL_JOBS STREQUAL "")
    include(ProcessorCount)
    ProcessorCount(XV6_PARALLEL_JOBS)
    if(XV6_PARALLEL_JOBS EQUAL 0)
        set(XV6_PARALLEL_JOBS 4)
    endif()
endif()

add_custom_command(
    OUTPUT  ${_libcrypto} ${_libssl} ${_header}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_build}
    COMMAND ${CMAKE_COMMAND} -E chdir ${_build}
            ${CMAKE_COMMAND} -E env
                CROSS_COMPILE=${_cross_prefix}
                "CFLAGS=-O2 -fPIC"
            ${PERL_PROG} ${_src}/Configure
                ${_openssl_target}
                --cross-compile-prefix=${_cross_prefix}
                --prefix=/
                --openssldir=/etc/ssl
                --libdir=lib
                enable-pic
                threads
                no-shared
                no-dso
                no-engine
                no-async
                ${_openssl_asm_opt}
                no-tests
                no-ui-console
                no-afalgeng
                no-ktls
                no-devcryptoeng
                -DOPENSSL_NO_SECURE_MEMORY
                -DOPENSSL_RAND_SEED_GETRANDOM
    COMMAND ${CMAKE_COMMAND} -E chdir ${_build} make build_libs -j${XV6_PARALLEL_JOBS}
    COMMAND ${CMAKE_COMMAND} -E chdir ${_build} make install_dev DESTDIR=${XV6_SYSROOT}
    COMMENT "Building port openssl"
    VERBATIM)

add_custom_target(port-openssl ALL
    DEPENDS ${_libcrypto} ${_libssl} ${_header})

add_custom_target(port-openssl-clean
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${_build}
    COMMAND ${CMAKE_COMMAND} -E rm -f  ${_libcrypto} ${_libssl}
    COMMENT "Removing openssl build artifacts")
