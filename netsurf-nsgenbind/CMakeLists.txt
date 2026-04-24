cmake_minimum_required(VERSION 3.16)
project(xv6-port-netsurf-nsgenbind LANGUAGES NONE)

# nsgenbind is a host-side build tool used by NetSurf's JavaScript
# binding generator (DOM). It must execute on the build machine, so we
# build it natively with the host's gcc and install into
# ${XV6_SYSROOT}/host-tools/bin (alongside glib-compile-resources and
# wayland-scanner).

set(_src    ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(_shared ${CMAKE_CURRENT_SOURCE_DIR}/../netsurf-buildsystem/src)
set(_prefix ${XV6_SYSROOT}/host-tools)
set(_marker ${_prefix}/bin/nsgenbind)

add_custom_command(
    OUTPUT  ${_marker}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_prefix}/bin
    COMMAND make -C ${_src} clean
                NSSHARED=${_shared} PREFIX=${_prefix} Q=
    COMMAND make -C ${_src}
                NSSHARED=${_shared} PREFIX=${_prefix} Q=
                install
    COMMENT "Building host nsgenbind"
    VERBATIM)

add_custom_target(port-netsurf-nsgenbind ALL DEPENDS ${_marker})
add_dependencies(port-netsurf-nsgenbind port-netsurf-buildsystem)

add_custom_target(port-netsurf-nsgenbind-clean
    COMMAND make -C ${_src} clean NSSHARED=${_shared} || true
    COMMAND ${CMAKE_COMMAND} -E rm -f ${_marker})
