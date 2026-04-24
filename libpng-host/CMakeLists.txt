cmake_minimum_required(VERSION 3.16)
project(xv6-port-libpng-host LANGUAGES NONE)

# Native build of libpng — used by NetSurf's build-time convert_image
# tool which compiles PNG icon resources into C arrays. Installs into
# ${XV6_SYSROOT}/host-tools (alongside glib-compile-resources, nsgenbind
# and wayland-scanner).

set(_src    ${CMAKE_CURRENT_SOURCE_DIR}/../libpng/src)
set(_build  ${CMAKE_BINARY_DIR}/libpng-host-build)
set(_prefix ${XV6_SYSROOT}/host-tools)
set(_marker ${_prefix}/lib/libpng.a)

add_custom_command(
    OUTPUT  ${_marker}
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${_build}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_build}
    COMMAND ${CMAKE_COMMAND} -E chdir ${_build}
            ${CMAKE_COMMAND} ${_src}
                -DCMAKE_C_COMPILER=cc
                -DCMAKE_BUILD_TYPE=Release
                -DCMAKE_INSTALL_PREFIX=${_prefix}
                -DPNG_SHARED=OFF
                -DPNG_TESTS=OFF
                -DPNG_TOOLS=OFF
    COMMAND ${CMAKE_COMMAND} --build ${_build} -j
    COMMAND ${CMAKE_COMMAND} --install ${_build}
    COMMENT "Building host libpng"
    VERBATIM)

add_custom_target(port-libpng-host ALL DEPENDS ${_marker})

add_custom_target(port-libpng-host-clean
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${_build}
    COMMAND ${CMAKE_COMMAND} -E rm -f ${_marker})
