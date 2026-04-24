cmake_minimum_required(VERSION 3.16)
project(xv6-port-netsurf-buildsystem LANGUAGES NONE)

# netsurf-buildsystem is a header-only collection of Makefile fragments
# referenced by every NetSurf library/browser via NSSHARED=. There is
# nothing to compile or install — its presence is enough. We expose a
# trivial port-netsurf-buildsystem target so other ports can DEPENDS on
# it for build ordering.

set(_marker ${XV6_SYSROOT}/host-tools/share/netsurf-buildsystem.stamp)

add_custom_command(
    OUTPUT  ${_marker}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${XV6_SYSROOT}/host-tools/share
    COMMAND ${CMAKE_COMMAND} -E touch ${_marker}
    COMMENT "Recording presence of netsurf-buildsystem")

add_custom_target(port-netsurf-buildsystem ALL DEPENDS ${_marker})

add_custom_target(port-netsurf-buildsystem-clean
    COMMAND ${CMAKE_COMMAND} -E rm -f ${_marker})
