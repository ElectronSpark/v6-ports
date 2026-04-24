cmake_minimum_required(VERSION 3.16)
project(xv6-port-wayland-host LANGUAGES NONE)

# Native build of wayland just to get the host wayland-scanner binary.
# wayland-scanner reads .xml protocol descriptions and emits C code +
# headers; it must run on the host because it is invoked as a build
# tool by both wayland (for itself), wayland-protocols (when generating
# headers for clients), and gtk3 (for the wayland backend).
#
# Output:
#   ${XV6_SYSROOT}/host-tools/bin/wayland-scanner

set(_src    ${CMAKE_CURRENT_SOURCE_DIR}/../wayland-src/src)
set(_build  ${CMAKE_BINARY_DIR}/wayland-host-build)
set(_marker ${XV6_SYSROOT}/host-tools/bin/wayland-scanner)

add_custom_command(
    OUTPUT  ${_marker}
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${_build}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${XV6_SYSROOT}/host-tools/bin
    COMMAND meson setup ${_build} ${_src}
                --buildtype=release
                --default-library=static
                -Dprefix=${XV6_SYSROOT}/host-tools
                -Dlibraries=false
                -Dscanner=true
                -Dtests=false
                -Ddocumentation=false
                -Ddtd_validation=false
    COMMAND meson compile -C ${_build} src/wayland-scanner
    COMMAND meson install -C ${_build} --no-rebuild
    COMMENT "Building host wayland-scanner"
    VERBATIM)

add_custom_target(port-wayland-host ALL DEPENDS ${_marker})

add_custom_target(port-wayland-host-clean
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${_build}
    COMMAND ${CMAKE_COMMAND} -E rm -f ${_marker}
    COMMENT "Cleaning wayland-host build")
