cmake_minimum_required(VERSION 3.16)
project(xv6-port-xv6-gbm LANGUAGES NONE)

if(NOT DEFINED XV6_SYSROOT OR XV6_SYSROOT STREQUAL "")
    message(FATAL_ERROR "ports/xv6-gbm: -DXV6_SYSROOT=<path> required")
endif()
if(NOT DEFINED CMAKE_C_COMPILER OR CMAKE_C_COMPILER STREQUAL "")
    message(FATAL_ERROR "ports/xv6-gbm: CMAKE_C_COMPILER is required")
endif()

set(XV6_GBM_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(_build_dir      "${CMAKE_CURRENT_BINARY_DIR}/xv6-gbm-build")
set(_obj            "${_build_dir}/xv6_gbm.o")
set(_lib            "${_build_dir}/libgbm.a")
set(_shared_lib     "${_build_dir}/libgbm.so.1.0.0")
set(_test_bin       "${_build_dir}/gbmtest")
set(_pc             "${_build_dir}/gbm.pc")
set(_installed_hdr  "${XV6_SYSROOT}/include/gbm.h")
set(_installed_lib  "${XV6_SYSROOT}/lib/libgbm.a")
set(_installed_shared_lib "${XV6_SYSROOT}/lib/libgbm.so.1.0.0")
set(_installed_shared_soname "${XV6_SYSROOT}/lib/libgbm.so.1")
set(_installed_shared_link "${XV6_SYSROOT}/lib/libgbm.so")
set(_installed_pc   "${XV6_SYSROOT}/lib/pkgconfig/gbm.pc")
set(_installed_test "${XV6_SYSROOT}/bin/gbmtest")

file(MAKE_DIRECTORY ${_build_dir} ${XV6_SYSROOT}/include
                    ${XV6_SYSROOT}/lib/pkgconfig ${XV6_SYSROOT}/bin)

add_custom_command(
    OUTPUT ${_obj}
    COMMAND ${CMAKE_C_COMPILER} -O2 -g -Wall -Werror -fPIC
            -isystem ${XV6_SYSROOT}/include
            -I ${XV6_GBM_SRC_DIR}
            -o ${_obj}
            -c ${XV6_GBM_SRC_DIR}/xv6_gbm.c
    DEPENDS ${XV6_GBM_SRC_DIR}/xv6_gbm.c ${XV6_GBM_SRC_DIR}/gbm.h
    COMMENT "Compiling xv6 GBM compatibility library"
    VERBATIM)

add_custom_command(
    OUTPUT ${_lib}
    COMMAND ${CMAKE_AR} rcs ${_lib} ${_obj}
    COMMAND ${CMAKE_RANLIB} ${_lib}
    DEPENDS ${_obj}
    COMMENT "Archiving xv6 GBM compatibility library"
    VERBATIM)

add_custom_command(
    OUTPUT ${_shared_lib}
    COMMAND ${CMAKE_C_COMPILER} -shared
            -Wl,-soname,libgbm.so.1
            -o ${_shared_lib}
            ${_obj}
    DEPENDS ${_obj}
    COMMENT "Linking xv6 GBM shared compatibility library"
    VERBATIM)

add_custom_command(
    OUTPUT ${_test_bin}
    COMMAND ${CMAKE_C_COMPILER} -O2 -g -Wall -Werror
            -isystem ${XV6_SYSROOT}/include
            -I ${XV6_GBM_SRC_DIR}
            -L ${_build_dir}
            -L ${XV6_SYSROOT}/lib
            -Wl,-rpath-link,${XV6_SYSROOT}/lib
            -o ${_test_bin}
            ${XV6_GBM_SRC_DIR}/gbmtest.c
            -lgbm
    DEPENDS ${XV6_GBM_SRC_DIR}/gbmtest.c ${XV6_GBM_SRC_DIR}/gbm.h ${_lib}
    COMMENT "Compiling xv6 GBM smoke test"
    VERBATIM)

add_custom_command(
    OUTPUT ${_pc}
    COMMAND ${CMAKE_COMMAND} -E echo "prefix=/" > ${_pc}
    COMMAND ${CMAKE_COMMAND} -E echo "exec_prefix=\${prefix}" >> ${_pc}
    COMMAND ${CMAKE_COMMAND} -E echo "libdir=\${prefix}/lib" >> ${_pc}
    COMMAND ${CMAKE_COMMAND} -E echo "includedir=\${prefix}/include" >> ${_pc}
    COMMAND ${CMAKE_COMMAND} -E echo "" >> ${_pc}
    COMMAND ${CMAKE_COMMAND} -E echo "Name: gbm" >> ${_pc}
    COMMAND ${CMAKE_COMMAND} -E echo "Description: xv6 linear GBM compatibility subset" >> ${_pc}
    COMMAND ${CMAKE_COMMAND} -E echo "Version: 0.1" >> ${_pc}
    COMMAND ${CMAKE_COMMAND} -E echo "Libs: -L\${libdir} -lgbm" >> ${_pc}
    COMMAND ${CMAKE_COMMAND} -E echo "Cflags: -I\${includedir}" >> ${_pc}
    COMMENT "Generating xv6 GBM pkg-config file"
    VERBATIM)

add_custom_command(
    OUTPUT ${_installed_hdr} ${_installed_lib} ${_installed_pc}
           ${_installed_shared_lib} ${_installed_shared_soname}
           ${_installed_shared_link} ${_installed_test}
    COMMAND ${CMAKE_COMMAND} -E copy ${XV6_GBM_SRC_DIR}/gbm.h ${_installed_hdr}
    COMMAND ${CMAKE_COMMAND} -E copy ${_lib} ${_installed_lib}
    COMMAND ${CMAKE_COMMAND} -E copy ${_shared_lib} ${_installed_shared_lib}
    COMMAND ${CMAKE_COMMAND} -E create_symlink libgbm.so.1.0.0
            ${_installed_shared_soname}
    COMMAND ${CMAKE_COMMAND} -E create_symlink libgbm.so.1
            ${_installed_shared_link}
    COMMAND ${CMAKE_COMMAND} -E copy ${_pc} ${_installed_pc}
    COMMAND ${CMAKE_COMMAND} -E copy ${_test_bin} ${_installed_test}
    DEPENDS ${XV6_GBM_SRC_DIR}/gbm.h ${_lib} ${_shared_lib} ${_pc}
            ${_test_bin}
    COMMENT "Installing xv6 GBM compatibility library"
    VERBATIM)

add_custom_target(port-xv6-gbm ALL DEPENDS
    ${_installed_hdr} ${_installed_lib} ${_installed_pc} ${_installed_test})

add_custom_target(port-xv6-gbm-clean
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${_build_dir}
    COMMAND ${CMAKE_COMMAND} -E remove -f
            ${_installed_hdr} ${_installed_lib}
            ${_installed_shared_lib} ${_installed_shared_soname}
            ${_installed_shared_link} ${_installed_pc}
            ${_installed_test}
    COMMENT "Removing xv6-gbm artifacts from ${XV6_SYSROOT}")
