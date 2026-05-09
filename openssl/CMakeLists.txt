# xv6-os openssl port — standalone CMake project (perl Configure upstream).
#
# Builds static libcrypto.a + libssl.a into ${XV6_SYSROOT}.
# Mirrors xv6-tmp/user/CMakeLists.txt:720+ (openssl section), now built with
# the host compiler/glibc.
#
# OpenSSL's build system is not autoconf/cmake — we drive its perl
# Configure script directly via add_custom_command (xv6_port doesn't
# model this build system).
#
# Outputs (installed into ${XV6_SYSROOT}):
#   /lib/libcrypto.a, /lib/libssl.a
#   /include/openssl/*.h
#   /bin/openssl

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

set(_openssl_target "linux-x86_64")
set(_openssl_asm_opt "")

set(_libcrypto ${XV6_SYSROOT}/lib/libcrypto.a)
set(_libssl    ${XV6_SYSROOT}/lib/libssl.a)
set(_header    ${XV6_SYSROOT}/include/openssl/ssl.h)
set(_openssl_cli_build ${_build}/apps/openssl)
set(_openssl_cli       ${XV6_SYSROOT}/bin/openssl)

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
                CC=${CMAKE_C_COMPILER}
                AR=${CMAKE_AR}
                RANLIB=${CMAKE_RANLIB}
                "CFLAGS=-O2 -fPIC"
            ${PERL_PROG} ${_src}/Configure
                ${_openssl_target}
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
    DEPENDS ${_libcrypto} ${_libssl} ${_header} ${_openssl_cli})

add_custom_command(
    OUTPUT ${_openssl_cli}
    COMMAND ${CMAKE_COMMAND} -E chdir ${_build} make build_programs -j${XV6_PARALLEL_JOBS}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${XV6_SYSROOT}/bin
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_openssl_cli_build} ${_openssl_cli}
    DEPENDS ${_libcrypto} ${_libssl} ${_header}
    COMMENT "Installing port openssl CLI"
    VERBATIM)

add_custom_target(port-openssl-clean
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${_build}
    COMMAND ${CMAKE_COMMAND} -E rm -f  ${_libcrypto} ${_libssl} ${_openssl_cli}
    COMMENT "Removing openssl build artifacts")
