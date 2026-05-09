cmake_minimum_required(VERSION 3.16)
project(xv6-port-wayland LANGUAGES NONE)

if(NOT DEFINED XV6_SYSROOT OR XV6_SYSROOT STREQUAL "")
    message(FATAL_ERROR "ports/wayland: -DXV6_SYSROOT=<path> required")
endif()
if(NOT DEFINED CMAKE_C_COMPILER OR CMAKE_C_COMPILER STREQUAL "")
    message(FATAL_ERROR "ports/wayland: CMAKE_C_COMPILER is required")
endif()

set(WAYLAND_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

foreach(_src wlcomp.c desktop.c glsmoke.c mesaglsmoke.c mesawlegl.c mesaeglinfo.c dmabufsmoke.c drmgpuprobe.c webkitgpusmoke.c xv6_egl_gles.c xv6memshim.c font8x16.h)
    if(NOT EXISTS "${WAYLAND_SRC_DIR}/${_src}")
        message(FATAL_ERROR "ports/wayland: missing ${WAYLAND_SRC_DIR}/${_src}")
    endif()
endforeach()

set(_wl_build     ${CMAKE_CURRENT_BINARY_DIR}/wlcomp-build)
set(_host_wl_build ${CMAKE_CURRENT_BINARY_DIR}/wayland-host-glibc-build)
set(_wl_proto_dir ${_wl_build}/wayland)
set(_wl_src       ${_wl_build}/wlcomp.c)
set(_wl_font      ${_wl_build}/font8x16.h)
set(_xdg_xml      ${CMAKE_CURRENT_SOURCE_DIR}/../wayland-protocols/src/stable/xdg-shell/xdg-shell.xml)
set(_xdg_header   ${_wl_proto_dir}/xdg-shell-server-protocol.h)
set(_xdg_code     ${_wl_build}/xdg-shell-protocol.c)
set(_xdg_client_header ${_wl_proto_dir}/xdg-shell-client-protocol.h)
set(_xdg_client_code   ${_wl_build}/xdg-shell-client-protocol.c)
set(_dmabuf_xml   ${CMAKE_CURRENT_SOURCE_DIR}/../wayland-protocols/src/stable/linux-dmabuf/linux-dmabuf-v1.xml)
set(_dmabuf_header ${_wl_proto_dir}/linux-dmabuf-v1-server-protocol.h)
set(_dmabuf_code  ${_wl_build}/linux-dmabuf-v1-protocol.c)
set(_dmabuf_client_header ${_wl_proto_dir}/linux-dmabuf-v1-client-protocol.h)
set(_dmabuf_client_code ${_wl_build}/linux-dmabuf-v1-client-protocol.c)
set(_wl_bin       ${_wl_build}/wlcomp)
set(_desktop_bin  ${_wl_build}/desktop)
set(_glsmoke_bin  ${_wl_build}/glsmoke)
set(_mesaglsmoke_bin ${_wl_build}/mesaglsmoke)
set(_mesawlegl_bin ${_wl_build}/mesawlegl)
set(_mesaeglinfo_bin ${_wl_build}/mesaeglinfo)
set(_dmabufsmoke_bin ${_wl_build}/dmabufsmoke)
set(_drmgpuprobe_bin ${_wl_build}/drmgpuprobe)
set(_webkitgpusmoke_bin ${_wl_build}/webkitgpusmoke)
set(_memshim_lib  ${_wl_build}/libxv6memshim.so)
set(_xv6egl_obj   ${_wl_build}/xv6_egl_gles.o)
set(_xv6egl_lib   ${_wl_build}/libEGL.a)
set(_xv6gles_lib  ${_wl_build}/libGLESv2.a)
set(_host_xv6egl_obj  ${_wl_build}/host-xv6_egl_gles.o)
set(_host_xv6egl_lib  ${_wl_build}/host-libEGL.a)
set(_host_xv6gles_lib ${_wl_build}/host-libGLESv2.a)
set(_wl_installed ${XV6_SYSROOT}/bin/wlcomp)
set(_desktop_installed ${XV6_SYSROOT}/bin/desktop)
set(_glsmoke_installed ${XV6_SYSROOT}/bin/glsmoke)
set(_mesaglsmoke_installed ${XV6_SYSROOT}/bin/mesaglsmoke)
set(_mesawlegl_installed ${XV6_SYSROOT}/bin/mesawlegl)
set(_mesaeglinfo_installed ${XV6_SYSROOT}/bin/mesaeglinfo)
set(_dmabufsmoke_installed ${XV6_SYSROOT}/bin/dmabufsmoke)
set(_drmgpuprobe_installed ${XV6_SYSROOT}/bin/drmgpuprobe)
set(_webkitgpusmoke_installed ${XV6_SYSROOT}/bin/webkitgpusmoke)
set(_memshim_installed ${XV6_SYSROOT}/lib/libxv6memshim.so)
# The small xv6 EGL/GLES shim is linked directly into wlcomp/glsmoke from the
# build directory. Do not install it under Mesa's public libEGL/libGLESv2 names:
# GTK/WebKit depend on Mesa's shared EGL stack, and a stale static shim archive
# in the sysroot can be picked up by Meson and linked into shared libraries.
set(_egl_installed ${XV6_SYSROOT}/lib/libEGL.a)
set(_gles_installed ${XV6_SYSROOT}/lib/libGLESv2.a)
file(MAKE_DIRECTORY ${_wl_build} ${_wl_proto_dir} ${XV6_SYSROOT}/bin)

set(_wayland_opt_cflags -O2 -g)

set(XV6_WAYLAND_HOST_GLIBC_AUTO OFF)
if(CMAKE_C_COMPILER MATCHES "x86_64")
    set(XV6_WAYLAND_HOST_GLIBC_AUTO ON)
endif()
option(XV6_WAYLAND_HOST_GLIBC
       "Build the core desktop session programs with the host compiler and host glibc"
       ${XV6_WAYLAND_HOST_GLIBC_AUTO})
set(_memshim_cc ${CMAKE_C_COMPILER})

if(XV6_WAYLAND_HOST_GLIBC)
    find_program(XV6_HOST_CC NAMES cc gcc clang REQUIRED)
    find_program(XV6_HOST_AR NAMES ar REQUIRED)
    find_program(XV6_HOST_RANLIB NAMES ranlib REQUIRED)
    find_program(XV6_MESON NAMES meson REQUIRED)
    set(_memshim_cc ${XV6_HOST_CC})
    set(_host_wl_stamp ${_host_wl_build}/.host-wayland-built)
    set(_host_wayland_server_lib ${_host_wl_build}/src/libwayland-server.a)
    set(_host_wayland_client_lib ${_host_wl_build}/src/libwayland-client.a)
    set(_host_wayland_private_lib ${_host_wl_build}/src/libwayland-private.a)
    set(_host_wayland_util_lib ${_host_wl_build}/src/libwayland-util.a)

    add_custom_command(
        OUTPUT  ${_host_wl_stamp}
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${_host_wl_build}
        COMMAND ${XV6_MESON} setup ${_host_wl_build}
                    ${CMAKE_CURRENT_SOURCE_DIR}/../wayland-src/src
                    --buildtype=release
                    --default-library=static
                    -Dprefix=${_host_wl_build}/install
                    -Dlibraries=true
                    -Dscanner=true
                    -Dtests=false
                    -Ddocumentation=false
                    -Ddtd_validation=false
        COMMAND ${XV6_MESON} compile -C ${_host_wl_build}
        COMMAND ${CMAKE_COMMAND} -E touch ${_host_wl_stamp}
        COMMENT "Building private host-glibc Wayland static libraries"
        VERBATIM)
endif()

add_custom_command(
    OUTPUT  ${_wl_src} ${_wl_font}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${WAYLAND_SRC_DIR}/font8x16.h ${_wl_font}
    COMMAND sed -e "s|(uint32_t)ev\\.dx|(uint32_t)(uint16_t)ev.dx|"
                -e "s|(uint32_t)ev\\.dy|(uint32_t)(uint16_t)ev.dy|"
                ${WAYLAND_SRC_DIR}/wlcomp.c > ${_wl_src}
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
    OUTPUT  ${_xdg_client_header} ${_xdg_client_code}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_wl_proto_dir}
    COMMAND ${XV6_SYSROOT}/host-tools/bin/wayland-scanner client-header
                ${_xdg_xml} ${_xdg_client_header}
    COMMAND ${XV6_SYSROOT}/host-tools/bin/wayland-scanner private-code
                ${_xdg_xml} ${_xdg_client_code}
    DEPENDS ${_xdg_xml}
    COMMENT "Generating xdg-shell Wayland client protocol sources"
    VERBATIM)

add_custom_command(
    OUTPUT  ${_dmabuf_header} ${_dmabuf_code}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_wl_proto_dir}
    COMMAND ${XV6_SYSROOT}/host-tools/bin/wayland-scanner server-header
                ${_dmabuf_xml} ${_dmabuf_header}
    COMMAND ${XV6_SYSROOT}/host-tools/bin/wayland-scanner private-code
                ${_dmabuf_xml} ${_dmabuf_code}
    DEPENDS ${_dmabuf_xml}
    COMMENT "Generating linux-dmabuf Wayland protocol sources"
    VERBATIM)

add_custom_command(
    OUTPUT  ${_dmabuf_client_header} ${_dmabuf_client_code}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_wl_proto_dir}
    COMMAND ${XV6_SYSROOT}/host-tools/bin/wayland-scanner client-header
                ${_dmabuf_xml} ${_dmabuf_client_header}
    COMMAND ${XV6_SYSROOT}/host-tools/bin/wayland-scanner private-code
                ${_dmabuf_xml} ${_dmabuf_client_code}
    DEPENDS ${_dmabuf_xml}
    COMMENT "Generating linux-dmabuf Wayland client protocol sources"
    VERBATIM)

if(XV6_WAYLAND_HOST_GLIBC)
    add_custom_command(
        OUTPUT  ${_wl_bin}
        COMMAND ${XV6_HOST_CC} ${_wayland_opt_cflags} -Wall -Wno-unused-result
                    -I ${_host_wl_build}
                    -I ${_host_wl_build}/src
                    -I ${CMAKE_CURRENT_SOURCE_DIR}/../wayland-src/src/src
                    -I ${_wl_build}
                    -I ${_wl_proto_dir}
                    -I ${XV6_SYSROOT}/include
                    -I ${XV6_SYSROOT}/include/wayland
                    -I ${XV6_SYSROOT}/include/libdrm
                    -o ${_wl_bin} ${_wl_src} ${_xdg_code} ${_dmabuf_code}
                    ${_host_wayland_server_lib}
                    ${_host_wayland_private_lib}
                    ${_host_wayland_util_lib}
                    -lffi -lpthread -lrt -lm
        DEPENDS ${_host_wl_stamp}
                ${_wl_src} ${_wl_font} ${_xdg_header} ${_xdg_code}
                ${_dmabuf_header} ${_dmabuf_code}
        COMMENT "Compiling wlcomp with host glibc"
        VERBATIM)

    add_custom_command(
        OUTPUT  ${_desktop_bin}
        COMMAND ${XV6_HOST_CC} ${_wayland_opt_cflags} -Wall -Wno-unused-result
                    -o ${_desktop_bin} ${WAYLAND_SRC_DIR}/desktop.c
        DEPENDS ${WAYLAND_SRC_DIR}/desktop.c
        COMMENT "Compiling desktop session manager with host glibc"
        VERBATIM)
else()
    add_custom_command(
        OUTPUT  ${_wl_bin}
        COMMAND ${CMAKE_C_COMPILER} ${_wayland_opt_cflags} -Wall -Wno-unused-result -static
                    -isystem ${XV6_SYSROOT}/include
                    -I ${_wl_build}
                    -I ${_wl_proto_dir}
                    -I ${XV6_SYSROOT}/include/wayland
                    -L ${XV6_SYSROOT}/lib
                    -Wl,-rpath-link,${XV6_SYSROOT}/lib
                    -o ${_wl_bin} ${_wl_src} ${_xdg_code} ${_dmabuf_code}
                    ${XV6_SYSROOT}/lib/libwayland-server.a
                    -lffi -lpthread -lrt -lm
        DEPENDS ${_wl_src} ${_wl_font} ${_xdg_header} ${_xdg_code}
                ${_dmabuf_header} ${_dmabuf_code}
                ${XV6_SYSROOT}/lib/libwayland-server.a
        COMMENT "Compiling wlcomp from source"
        VERBATIM)

    add_custom_command(
        OUTPUT  ${_desktop_bin}
        COMMAND ${CMAKE_C_COMPILER} ${_wayland_opt_cflags} -Wall -static
                    -o ${_desktop_bin} ${WAYLAND_SRC_DIR}/desktop.c
        DEPENDS ${WAYLAND_SRC_DIR}/desktop.c
        COMMENT "Compiling desktop session manager from source"
        VERBATIM)
endif()

add_custom_command(
    OUTPUT  ${_memshim_lib}
    COMMAND ${_memshim_cc} ${_wayland_opt_cflags} -Wall -fPIC -shared
                -fno-builtin -fno-stack-protector
                -Wl,-soname,libxv6memshim.so
                -o ${_memshim_lib}
                ${WAYLAND_SRC_DIR}/xv6memshim.c
    DEPENDS ${WAYLAND_SRC_DIR}/xv6memshim.c
    COMMENT "Compiling xv6 WebKit memory-function shim"
    VERBATIM)

add_custom_command(
    OUTPUT  ${_xv6egl_obj}
    COMMAND ${CMAKE_C_COMPILER} ${_wayland_opt_cflags} -Wall -fPIC
                -DWL_EGL_PLATFORM
                -isystem ${XV6_SYSROOT}/include
                -I ${XV6_SYSROOT}/include/wayland
                -o ${_xv6egl_obj}
                -c ${WAYLAND_SRC_DIR}/xv6_egl_gles.c
    DEPENDS ${WAYLAND_SRC_DIR}/xv6_egl_gles.c
    COMMENT "Compiling xv6 EGL/GLES compatibility layer"
    VERBATIM)

add_custom_command(
    OUTPUT  ${_xv6egl_lib} ${_xv6gles_lib}
    COMMAND ${CMAKE_AR} rcs ${_xv6egl_lib} ${_xv6egl_obj}
    COMMAND ${CMAKE_RANLIB} ${_xv6egl_lib}
    COMMAND ${CMAKE_AR} rcs ${_xv6gles_lib} ${_xv6egl_obj}
    COMMAND ${CMAKE_RANLIB} ${_xv6gles_lib}
    DEPENDS ${_xv6egl_obj}
    COMMENT "Archiving xv6 EGL/GLES compatibility libraries"
    VERBATIM)

if(XV6_WAYLAND_HOST_GLIBC)
    add_custom_command(
        OUTPUT  ${_host_xv6egl_obj}
        COMMAND ${XV6_HOST_CC} ${_wayland_opt_cflags} -Wall -fPIC
                    -DWL_EGL_PLATFORM
                    -isystem ${XV6_SYSROOT}/include
                    -I ${XV6_SYSROOT}/include/wayland
                    -o ${_host_xv6egl_obj}
                    -c ${WAYLAND_SRC_DIR}/xv6_egl_gles.c
        DEPENDS ${WAYLAND_SRC_DIR}/xv6_egl_gles.c
        COMMENT "Compiling host-glibc xv6 EGL/GLES compatibility layer"
        VERBATIM)

    add_custom_command(
        OUTPUT  ${_host_xv6egl_lib} ${_host_xv6gles_lib}
        COMMAND ${XV6_HOST_AR} rcs ${_host_xv6egl_lib} ${_host_xv6egl_obj}
        COMMAND ${XV6_HOST_RANLIB} ${_host_xv6egl_lib}
        COMMAND ${XV6_HOST_AR} rcs ${_host_xv6gles_lib} ${_host_xv6egl_obj}
        COMMAND ${XV6_HOST_RANLIB} ${_host_xv6gles_lib}
        DEPENDS ${_host_xv6egl_obj}
        COMMENT "Archiving host-glibc xv6 EGL/GLES compatibility libraries"
        VERBATIM)

    add_custom_command(
        OUTPUT  ${_glsmoke_bin}
        COMMAND ${XV6_HOST_CC} ${_wayland_opt_cflags} -Wall
                    -DWL_EGL_PLATFORM
                    -I ${_host_wl_build}
                    -I ${_host_wl_build}/src
                    -I ${CMAKE_CURRENT_SOURCE_DIR}/../wayland-src/src/src
                    -I ${_wl_build}
                    -I ${_wl_proto_dir}
                    -I ${XV6_SYSROOT}/include
                    -I ${XV6_SYSROOT}/include/wayland
                    -o ${_glsmoke_bin}
                    ${WAYLAND_SRC_DIR}/glsmoke.c ${_xdg_client_code}
                    ${_host_xv6gles_lib}
                    ${_host_xv6egl_lib}
                    ${_host_wayland_client_lib}
                    ${_host_wayland_private_lib}
                    ${_host_wayland_util_lib}
                    -lffi -lpthread -lrt -lm
        DEPENDS ${_host_wl_stamp}
                ${WAYLAND_SRC_DIR}/glsmoke.c ${_xdg_client_header}
                ${_xdg_client_code} ${_host_xv6egl_lib} ${_host_xv6gles_lib}
        COMMENT "Compiling glsmoke Wayland client with host glibc"
        VERBATIM)
else()
    add_custom_command(
        OUTPUT  ${_glsmoke_bin}
        COMMAND ${CMAKE_C_COMPILER} ${_wayland_opt_cflags} -Wall -static
                    -DWL_EGL_PLATFORM
                    -isystem ${XV6_SYSROOT}/include
                    -I ${_wl_proto_dir}
                    -I ${XV6_SYSROOT}/include/wayland
                    -L ${XV6_SYSROOT}/lib
                    -Wl,-rpath-link,${XV6_SYSROOT}/lib
                    -o ${_glsmoke_bin}
                    ${WAYLAND_SRC_DIR}/glsmoke.c ${_xdg_client_code}
                    ${_xv6gles_lib}
                    ${_xv6egl_lib}
                    ${XV6_SYSROOT}/lib/libwayland-client.a
                    -lffi -lpthread -lrt -lm
        DEPENDS ${WAYLAND_SRC_DIR}/glsmoke.c ${_xdg_client_header}
                ${_xdg_client_code} ${_xv6egl_lib} ${_xv6gles_lib}
                ${XV6_SYSROOT}/lib/libwayland-client.a
        COMMENT "Compiling glsmoke Wayland client"
        VERBATIM)
endif()

add_custom_command(
    OUTPUT  ${_mesaeglinfo_bin}
    COMMAND ${CMAKE_C_COMPILER} ${_wayland_opt_cflags} -Wall
                -isystem ${XV6_SYSROOT}/include
                -isystem ${XV6_SYSROOT}/include/libdrm
                -L ${XV6_SYSROOT}/lib
                -Wl,-rpath-link,${XV6_SYSROOT}/lib
                -Wl,-rpath,/lib
                -o ${_mesaeglinfo_bin}
                ${WAYLAND_SRC_DIR}/mesaeglinfo.c
                -lEGL -lGLESv2 -lpthread -ldl -lrt -lm
    DEPENDS ${WAYLAND_SRC_DIR}/mesaeglinfo.c
    COMMENT "Compiling Mesa surfaceless EGL smoke probe"
    VERBATIM)

add_custom_command(
    OUTPUT  ${_mesaglsmoke_bin}
    COMMAND ${CMAKE_C_COMPILER} ${_wayland_opt_cflags} -Wall
                -isystem ${XV6_SYSROOT}/include
                -I ${_wl_proto_dir}
                -I ${XV6_SYSROOT}/include/wayland
                -L ${XV6_SYSROOT}/lib
                -Wl,-rpath-link,${XV6_SYSROOT}/lib
                -Wl,-rpath,/lib
                -o ${_mesaglsmoke_bin}
                ${WAYLAND_SRC_DIR}/mesaglsmoke.c ${_xdg_client_code}
                -lEGL -lGLESv2 -lwayland-client
                -lffi -lpthread -ldl -lrt -lm
    DEPENDS ${WAYLAND_SRC_DIR}/mesaglsmoke.c ${_xdg_client_header}
            ${_xdg_client_code}
    COMMENT "Compiling Mesa Wayland GL smoke client"
    VERBATIM)

add_custom_command(
    OUTPUT  ${_mesawlegl_bin}
    COMMAND ${CMAKE_C_COMPILER} ${_wayland_opt_cflags} -Wall
                -isystem ${XV6_SYSROOT}/include
                -I ${_wl_proto_dir}
                -I ${XV6_SYSROOT}/include/wayland
                -L ${XV6_SYSROOT}/lib
                -Wl,-rpath-link,${XV6_SYSROOT}/lib
                -Wl,-rpath,/lib
                -o ${_mesawlegl_bin}
                ${WAYLAND_SRC_DIR}/mesawlegl.c ${_xdg_client_code}
                -lEGL -lGLESv2 -lwayland-egl -lwayland-client
                -lffi -lpthread -ldl -lrt -lm
    DEPENDS ${WAYLAND_SRC_DIR}/mesawlegl.c ${_xdg_client_header}
            ${_xdg_client_code}
    COMMENT "Compiling Mesa native Wayland EGL smoke client"
    VERBATIM)

add_custom_command(
    OUTPUT  ${_dmabufsmoke_bin}
    COMMAND ${CMAKE_C_COMPILER} ${_wayland_opt_cflags} -Wall
                -isystem ${XV6_SYSROOT}/include
                -I ${_wl_proto_dir}
                -I ${XV6_SYSROOT}/include/wayland
                -L ${XV6_SYSROOT}/lib
                -Wl,-rpath-link,${XV6_SYSROOT}/lib
                -o ${_dmabufsmoke_bin}
                ${WAYLAND_SRC_DIR}/dmabufsmoke.c ${_xdg_client_code}
                ${_dmabuf_client_code}
                -lgbm -lwayland-client
                -lffi -lpthread -ldl -lrt -lm
    DEPENDS ${WAYLAND_SRC_DIR}/dmabufsmoke.c ${_xdg_client_header}
            ${_xdg_client_code} ${_dmabuf_client_header}
            ${_dmabuf_client_code}
    COMMENT "Compiling linux-dmabuf smoke client"
    VERBATIM)

set(_webkit_smoke_ok OFF)
option(XV6_BUILD_WEBKIT_GPU_SMOKE
    "Build the optional WebKit GPU policy smoke helper" OFF)
set(_webkit_lib "${XV6_SYSROOT}/lib/libwebkit2gtk-4.1.so")
if(XV6_BUILD_WEBKIT_GPU_SMOKE AND EXISTS "${_webkit_lib}")
    find_program(_READELF NAMES readelf)
    if(_READELF)
        execute_process(
            COMMAND ${_READELF} -d ${_webkit_lib}
            OUTPUT_VARIABLE _webkit_dynamic
            ERROR_QUIET)
        if(NOT _webkit_dynamic MATCHES "Shared library: \\[libc\\.so\\]")
            set(_webkit_smoke_ok ON)
        else()
            message(STATUS
                "ports/wayland: disabling WebKit GPU smoke client; "
                "${_webkit_lib} is a legacy libc.so runtime")
        endif()
    endif()
endif()

if(_webkit_smoke_ok)
    add_custom_command(
        OUTPUT  ${_webkitgpusmoke_bin}
        COMMAND ${CMAKE_C_COMPILER} ${_wayland_opt_cflags} -Wall
                    -isystem ${XV6_SYSROOT}/include
                    -isystem ${XV6_SYSROOT}/include/gtk-3.0
                    -isystem ${XV6_SYSROOT}/include/glib-2.0
                    -isystem ${XV6_SYSROOT}/lib/glib-2.0/include
                    -isystem ${XV6_SYSROOT}/include/pango-1.0
                    -isystem ${XV6_SYSROOT}/include/atk-1.0
                    -isystem ${XV6_SYSROOT}/include/cairo
                    -isystem ${XV6_SYSROOT}/include/gdk-pixbuf-2.0
                    -isystem ${XV6_SYSROOT}/include/freetype2
                    -isystem ${XV6_SYSROOT}/include/harfbuzz
                    -isystem ${XV6_SYSROOT}/include/pixman-1
                    -L ${XV6_SYSROOT}/lib
                    -Wl,-rpath-link,${XV6_SYSROOT}/lib
                    -o ${_webkitgpusmoke_bin}
                    ${WAYLAND_SRC_DIR}/webkitgpusmoke.c
                    -lwebkit2gtk-4.1 -lgtk-3 -lgdk-3 -lgobject-2.0 -lglib-2.0
                    -lpthread -ldl -lrt -lm
        DEPENDS ${WAYLAND_SRC_DIR}/webkitgpusmoke.c
        COMMENT "Compiling WebKit GPU policy smoke client"
        VERBATIM)
else()
    set(_webkit_placeholder ${CMAKE_CURRENT_BINARY_DIR}/webkitgpusmoke-placeholder.sh)
    file(WRITE ${_webkit_placeholder}
        "#!/bin/sh\n"
        "echo webkitgpusmoke: no host-glibc WebKitGTK runtime staged >&2\n"
        "exit 77\n")
    add_custom_command(
        OUTPUT  ${_webkitgpusmoke_bin}
        COMMAND ${CMAKE_COMMAND} -E copy
                ${_webkit_placeholder} ${_webkitgpusmoke_bin}
        COMMAND chmod 0755 ${_webkitgpusmoke_bin}
        DEPENDS ${_webkit_placeholder}
        COMMENT "Writing WebKit GPU smoke placeholder"
        VERBATIM)
endif()

add_custom_command(
    OUTPUT  ${_drmgpuprobe_bin}
    COMMAND ${CMAKE_C_COMPILER} ${_wayland_opt_cflags} -Wall
                -isystem ${XV6_SYSROOT}/include
                -isystem ${XV6_SYSROOT}/include/libdrm
                -L ${XV6_SYSROOT}/lib
                -Wl,-rpath-link,${XV6_SYSROOT}/lib
                -o ${_drmgpuprobe_bin}
                ${WAYLAND_SRC_DIR}/drmgpuprobe.c
                -lgbm -ldrm -lpthread -ldl -lrt -lm
    DEPENDS ${WAYLAND_SRC_DIR}/drmgpuprobe.c
    COMMENT "Compiling DRM/GBM render-node probe"
    VERBATIM)

add_custom_command(
    OUTPUT  ${_wl_installed} ${_desktop_installed} ${_glsmoke_installed}
            ${_mesaglsmoke_installed} ${_mesawlegl_installed}
            ${_mesaeglinfo_installed} ${_dmabufsmoke_installed}
            ${_drmgpuprobe_installed} ${_webkitgpusmoke_installed}
            ${_memshim_installed}
    COMMAND ${CMAKE_COMMAND} -E copy ${_wl_bin} ${_wl_installed}
    COMMAND ${CMAKE_COMMAND} -E copy ${_desktop_bin} ${_desktop_installed}
    COMMAND ${CMAKE_COMMAND} -E copy ${_glsmoke_bin} ${_glsmoke_installed}
    COMMAND ${CMAKE_COMMAND} -E copy ${_mesaglsmoke_bin} ${_mesaglsmoke_installed}
    COMMAND ${CMAKE_COMMAND} -E copy ${_mesawlegl_bin} ${_mesawlegl_installed}
    COMMAND ${CMAKE_COMMAND} -E copy ${_mesaeglinfo_bin} ${_mesaeglinfo_installed}
    COMMAND ${CMAKE_COMMAND} -E copy ${_dmabufsmoke_bin} ${_dmabufsmoke_installed}
    COMMAND ${CMAKE_COMMAND} -E copy ${_drmgpuprobe_bin} ${_drmgpuprobe_installed}
    COMMAND ${CMAKE_COMMAND} -E copy ${_webkitgpusmoke_bin} ${_webkitgpusmoke_installed}
    COMMAND ${CMAKE_COMMAND} -E copy ${_memshim_lib} ${_memshim_installed}
    COMMAND ${CMAKE_COMMAND} -E rm -f ${_egl_installed} ${_gles_installed}
    DEPENDS ${_wl_bin} ${_desktop_bin} ${_glsmoke_bin} ${_mesaglsmoke_bin}
            ${_mesawlegl_bin} ${_mesaeglinfo_bin} ${_dmabufsmoke_bin}
            ${_drmgpuprobe_bin} ${_webkitgpusmoke_bin} ${_memshim_lib}
    COMMENT "Installing GUI session into ${XV6_SYSROOT}/bin"
    VERBATIM)

add_custom_target(port-wayland ALL DEPENDS
    ${_wl_installed} ${_desktop_installed} ${_glsmoke_installed}
    ${_mesaglsmoke_installed} ${_mesawlegl_installed}
    ${_mesaeglinfo_installed} ${_dmabufsmoke_installed}
    ${_drmgpuprobe_installed}
    ${_webkitgpusmoke_installed} ${_memshim_installed})
if(TARGET port-wayland-libs)
    add_dependencies(port-wayland port-wayland-libs)
endif()
if(TARGET port-mesa)
    add_dependencies(port-wayland port-mesa)
endif()
if(TARGET port-webkit)
    add_dependencies(port-wayland port-webkit)
endif()

add_custom_target(port-wayland-clean
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${_wl_build} ${_host_wl_build}
    COMMAND ${CMAKE_COMMAND} -E remove -f
            ${_wl_installed} ${_desktop_installed} ${_glsmoke_installed}
            ${_mesaglsmoke_installed} ${_mesawlegl_installed}
            ${_mesaeglinfo_installed} ${_dmabufsmoke_installed}
            ${_drmgpuprobe_installed} ${_webkitgpusmoke_installed}
            ${_memshim_installed} ${_memshim_lib}
            ${_egl_installed} ${_gles_installed}
            ${_host_xv6egl_obj} ${_host_xv6egl_lib} ${_host_xv6gles_lib}
    COMMENT "Removing wayland artifacts from ${XV6_SYSROOT}")
