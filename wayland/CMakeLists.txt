cmake_minimum_required(VERSION 3.16)
project(xv6-port-wayland LANGUAGES NONE)

if(NOT DEFINED XV6_SYSROOT OR XV6_SYSROOT STREQUAL "")
    message(FATAL_ERROR "ports/wayland: -DXV6_SYSROOT=<path> required")
endif()
if(NOT DEFINED CMAKE_C_COMPILER OR CMAKE_C_COMPILER STREQUAL "")
    message(FATAL_ERROR "ports/wayland: CMAKE_C_COMPILER is required")
endif()

set(WAYLAND_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

foreach(_src wlcomp.c desktop.c font8x16.h)
    if(NOT EXISTS "${WAYLAND_SRC_DIR}/${_src}")
        message(FATAL_ERROR "ports/wayland: missing ${WAYLAND_SRC_DIR}/${_src}")
    endif()
endforeach()

set(_wl_build     ${CMAKE_CURRENT_BINARY_DIR}/wlcomp-build)
set(_wl_proto_dir ${_wl_build}/wayland)
set(_wl_src       ${_wl_build}/wlcomp.c)
set(_wl_font      ${_wl_build}/font8x16.h)
set(_xdg_xml      ${CMAKE_CURRENT_SOURCE_DIR}/../wayland-protocols/src/stable/xdg-shell/xdg-shell.xml)
set(_xdg_header   ${_wl_proto_dir}/xdg-shell-server-protocol.h)
set(_xdg_code     ${_wl_build}/xdg-shell-protocol.c)
set(_wl_bin       ${_wl_build}/wlcomp)
set(_desktop_bin  ${_wl_build}/desktop)
set(_wl_installed ${XV6_SYSROOT}/bin/wlcomp)
set(_desktop_installed ${XV6_SYSROOT}/bin/desktop)
file(MAKE_DIRECTORY ${_wl_build} ${_wl_proto_dir} ${XV6_SYSROOT}/bin)

add_custom_command(
    OUTPUT  ${_wl_src} ${_wl_font}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${WAYLAND_SRC_DIR}/font8x16.h ${_wl_font}
    COMMAND sed -e "s|(uint32_t)ev\\.dx|(uint32_t)(uint16_t)ev.dx|"
                -e "s|(uint32_t)ev\\.dy|(uint32_t)(uint16_t)ev.dy|"
                -e "s|homepage_url:http://127.0.0.1/|homepage_url:file:///share/netsurf/welcome.html|"
                -e "s|for (int i = 0\\; i < 60\\; i++)|for (int i = 0; i < 0; i++)|"
                -e "s|{ \"Browser\",  \"/bin/netsurf\",   \"netsurf\",  0xFF3D6E9E, 'W' },|{ \"Browser\",  NULL,             NULL,       0xFF3D6E9E, 'W' },|"
                -e "s|{ \"Web Browser\",     \"/bin/netsurf\",   \"netsurf\"  },|{ \"---\",             NULL,             NULL       },|"
                -e "s|case 9: launch_desktop_app(\"/bin/netsurf\", \"netsurf\"); break;|case 9: break;|g"
                ${WAYLAND_SRC_DIR}/wlcomp.c > ${_wl_src}
    COMMAND perl -0pi -e [=[s{        if \(surf->committed_buf && surf->committed_buf->resource &&\n            !surf->buffer_released\) \{\n            wl_buffer_send_release\(surf->committed_buf->resource\);\n            surf->buffer_released = 1;\n        \}\n}{        /* Keep the current committed buffer owned until replacement. */\n}]=]
                ${_wl_src}
    COMMAND perl -0pi -e [=[s{        /\* Wait for events \(16ms timeout ~ 60fps\) \*/\n        struct epoll_event events\[8\];\n        epoll_wait\(epfd, events, 8, 16\);\n}{        /* Keep the compositor loop cycling so Wayland roundtrips complete. */\n        usleep(16000);\n}]=]
                ${_wl_src}
    COMMAND perl -0pi -e [=[s{wl_event_loop_dispatch\(loop, 0\);}{wl_event_loop_dispatch(loop, 1);}g]=]
                ${_wl_src}
    DEPENDS ${WAYLAND_SRC_DIR}/wlcomp.c ${WAYLAND_SRC_DIR}/font8x16.h
    COMMENT "Patching wlcomp.c"
    VERBATIM)

add_custom_command(
    OUTPUT  ${_xdg_header} ${_xdg_code}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_wl_proto_dir}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${XV6_SYSROOT}/include/wayland-server-core.h
                ${_wl_proto_dir}/wayland-server-core.h
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${XV6_SYSROOT}/include/wayland-server-protocol.h
                ${_wl_proto_dir}/wayland-server-protocol.h
    COMMAND ${XV6_SYSROOT}/host-tools/bin/wayland-scanner server-header
                ${_xdg_xml} ${_xdg_header}
    COMMAND ${XV6_SYSROOT}/host-tools/bin/wayland-scanner private-code
                ${_xdg_xml} ${_xdg_code}
    DEPENDS ${_xdg_xml}
    COMMENT "Generating xdg-shell Wayland protocol sources"
    VERBATIM)

add_custom_command(
    OUTPUT  ${_wl_bin}
    COMMAND ${CMAKE_C_COMPILER} -O2 -g -Wall -Wno-unused-result -static
                -isystem ${XV6_SYSROOT}/include
                -I ${_wl_build}
                -I ${_wl_proto_dir}
                -I ${XV6_SYSROOT}/include/wayland
                -L ${XV6_SYSROOT}/lib
                -Wl,-rpath-link,${XV6_SYSROOT}/lib
                -o ${_wl_bin} ${_wl_src} ${_xdg_code}
                ${XV6_SYSROOT}/lib/libwayland-server.a
                -lffi -lpthread -lrt -lm
    DEPENDS ${_wl_src} ${_wl_font} ${_xdg_header} ${_xdg_code}
    COMMENT "Compiling wlcomp from source"
    VERBATIM)

add_custom_command(
    OUTPUT  ${_desktop_bin}
    COMMAND ${CMAKE_C_COMPILER} -O2 -g -Wall -static
                -o ${_desktop_bin} ${WAYLAND_SRC_DIR}/desktop.c
    DEPENDS ${WAYLAND_SRC_DIR}/desktop.c
    COMMENT "Compiling desktop session manager from source"
    VERBATIM)

add_custom_command(
    OUTPUT  ${_wl_installed} ${_desktop_installed}
    COMMAND ${CMAKE_COMMAND} -E copy ${_wl_bin} ${_wl_installed}
    COMMAND ${CMAKE_COMMAND} -E copy ${_desktop_bin} ${_desktop_installed}
    DEPENDS ${_wl_bin} ${_desktop_bin}
    COMMENT "Installing GUI session into ${XV6_SYSROOT}/bin"
    VERBATIM)

add_custom_target(port-wayland ALL DEPENDS ${_wl_installed} ${_desktop_installed})
if(TARGET port-wayland-libs)
    add_dependencies(port-wayland port-wayland-libs)
endif()

add_custom_target(port-wayland-clean
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${_wl_build}
    COMMAND ${CMAKE_COMMAND} -E remove -f ${_wl_installed} ${_desktop_installed}
    COMMENT "Removing wayland artifacts from ${XV6_SYSROOT}")
