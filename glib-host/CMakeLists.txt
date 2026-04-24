cmake_minimum_required(VERSION 3.16)
project(xv6-port-glib-host LANGUAGES NONE)

# Native (host) build of glib, just to get the host-runnable tools that
# downstream meson ports demand:
#
#   glib-compile-resources   (C binary, used by gdk-pixbuf, gtk3, netsurf)
#   glib-compile-schemas     (C binary, used by gtk3 install)
#   glib-genmarshal          (Python script, also installed by cross glib)
#   glib-mkenums             (Python script, ditto)
#   gdbus-codegen            (Python script, ditto)
#
# The cross glib already drops the Python scripts into ${XV6_SYSROOT}/bin
# but those are #!-targeted at the host's /usr/bin/python3 anyway, so they
# happen to be runnable. The two C binaries are the real reason this
# port exists.
#
# Output goes under ${XV6_SYSROOT}/host-tools/bin so it doesn't pollute
# the cross sysroot's /bin (which gets staged into the xv6 image).
# AddPort.cmake's meson mode prepends that dir to PATH for downstream
# ports.

set(_glib_src ${CMAKE_CURRENT_SOURCE_DIR}/../glib/src)
set(_build    ${CMAKE_BINARY_DIR}/glib-host-build)
set(_host_tools_dir ${XV6_SYSROOT}/host-tools)
set(_marker   ${_host_tools_dir}/bin/glib-compile-resources)

add_custom_command(
    OUTPUT  ${_marker}
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${_build}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_host_tools_dir}/bin
    COMMAND meson setup ${_build} ${_glib_src}
                --buildtype=release
                --default-library=static
                -Dprefix=${_host_tools_dir}
                -Dselinux=disabled -Dxattr=false -Dlibmount=disabled
                -Dman-pages=disabled -Ddtrace=disabled -Dsystemtap=disabled
                -Dsysprof=disabled -Ddocumentation=false
                -Dbsymbolic_functions=false -Dtests=false
                -Dinstalled_tests=false -Dnls=disabled
                -Dlibelf=disabled -Dintrospection=disabled
                -Dglib_debug=disabled
    COMMAND meson compile -C ${_build}
                gio/glib-compile-resources gio/glib-compile-schemas
    COMMAND ${CMAKE_COMMAND} -E copy
                ${_build}/gio/glib-compile-resources
                ${_host_tools_dir}/bin/glib-compile-resources
    COMMAND ${CMAKE_COMMAND} -E copy
                ${_build}/gio/glib-compile-schemas
                ${_host_tools_dir}/bin/glib-compile-schemas
    COMMENT "Building host glib tools (glib-compile-resources/-schemas)"
    VERBATIM)

add_custom_target(port-glib-host ALL DEPENDS ${_marker})

add_custom_target(port-glib-host-clean
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${_build}
    COMMAND ${CMAKE_COMMAND} -E rm -f ${_marker}
    COMMENT "Cleaning glib-host build")
