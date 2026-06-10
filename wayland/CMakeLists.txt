cmake_minimum_required(VERSION 3.16)
project(xv6-port-wayland LANGUAGES NONE)

if(NOT DEFINED XV6_SYSROOT OR XV6_SYSROOT STREQUAL "")
    message(FATAL_ERROR "ports/wayland: -DXV6_SYSROOT=<path> required")
endif()
if(NOT DEFINED CMAKE_C_COMPILER OR CMAKE_C_COMPILER STREQUAL "")
    message(FATAL_ERROR "ports/wayland: CMAKE_C_COMPILER is required")
endif()

set(WAYLAND_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(XV6_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(XV6_FAULT_TRACE_SRC "${XV6_REPO_ROOT}/tools/xv6_glibc_fault_trace.c")
set(PEANUT_GB_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../peanut-gb/src")
set(DIRECTX_HEADERS_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../mesa/src/subprojects/DirectX-Headers-1.0/include")
set(DESKTOP_INCLUDE_SRCS
    ${WAYLAND_SRC_DIR}/desktop_clients.inc)

foreach(_src desktop.c filemgr.c glsmoke.c glmaze.c peanutgb.c mesaglsmoke.c mesawlegl.c mesakmsgl.c mesademo.c gl_program.c gl_program.h gl_fps_overlay.c gl_fps_overlay.h mesawlegl_sphere.c mesawlegl_sphere.h pixel_fps_overlay.c pixel_fps_overlay.h xv6_draw.c xv6_draw.h xv6_icon.c xv6_icon.h xv6_icon_filemgr.c xv6_icon_glmaze.c xv6_present_buffer.c xv6_present_buffer.h mesaeglinfo.c mesaglfeature.c mesad3d12probe.c gpu_substrate_validate.c dmabufsmoke.c d3d12sharedsmoke.c drmgpuprobe.c webkitgpusmoke.c cursorsmoke.c xv6_egl_gles.c webkit_preload_wrapper.c install_webkit_preload_wrappers.sh font8x16.h)
    if(NOT EXISTS "${WAYLAND_SRC_DIR}/${_src}")
        message(FATAL_ERROR "ports/wayland: missing ${WAYLAND_SRC_DIR}/${_src}")
    endif()
endforeach()
if(NOT EXISTS "${PEANUT_GB_SRC_DIR}/peanut_gb.h")
    message(FATAL_ERROR
        "ports/wayland: missing Peanut-GB source submodule at ${PEANUT_GB_SRC_DIR}; run git submodule update --init --recursive ports/peanut-gb/src")
endif()
foreach(_src ${DESKTOP_INCLUDE_SRCS})
    if(NOT EXISTS "${_src}")
        message(FATAL_ERROR "ports/wayland: missing ${_src}")
    endif()
endforeach()
if(NOT EXISTS "${XV6_FAULT_TRACE_SRC}")
    message(FATAL_ERROR "ports/wayland: missing ${XV6_FAULT_TRACE_SRC}")
endif()
if(NOT EXISTS "${DIRECTX_HEADERS_DIR}/directx/d3d12.h")
    message(STATUS "ports/wayland: DirectX-Headers not yet present (expected after mesa meson setup)")
endif()

set(_d3d12shared_ok OFF)
if(EXISTS "/usr/lib/wsl/lib/libd3d12.so" AND EXISTS "/usr/lib/wsl/lib/libdxcore.so")
    set(_d3d12shared_ok ON)
else()
    message(STATUS "ports/wayland: libd3d12/libdxcore not found in /usr/lib/wsl/lib; d3d12sharedsmoke will be a placeholder")
endif()

set(_wl_build     ${CMAKE_CURRENT_BINARY_DIR}/wayland-build)
set(_host_wl_build ${CMAKE_CURRENT_BINARY_DIR}/wayland-host-glibc-build)
set(_wl_proto_dir ${_wl_build}/wayland)
set(_xdg_xml      ${CMAKE_CURRENT_SOURCE_DIR}/../wayland-protocols/src/stable/xdg-shell/xdg-shell.xml)
set(_xdg_client_header ${_wl_proto_dir}/xdg-shell-client-protocol.h)
set(_xdg_client_code   ${_wl_build}/xdg-shell-client-protocol.c)
set(_dmabuf_xml   ${CMAKE_CURRENT_SOURCE_DIR}/../wayland-protocols/src/stable/linux-dmabuf/linux-dmabuf-v1.xml)
set(_dmabuf_client_header ${_wl_proto_dir}/linux-dmabuf-v1-client-protocol.h)
set(_dmabuf_client_code ${_wl_build}/linux-dmabuf-v1-client-protocol.c)
set(_explicit_sync_xml ${CMAKE_CURRENT_SOURCE_DIR}/../wayland-protocols/src/unstable/linux-explicit-synchronization/linux-explicit-synchronization-unstable-v1.xml)
set(_explicit_sync_client_header ${_wl_proto_dir}/linux-explicit-synchronization-unstable-v1-client-protocol.h)
set(_explicit_sync_client_code ${_wl_build}/linux-explicit-synchronization-unstable-v1-client-protocol.c)
set(_weston_session_bin  ${_wl_build}/weston-session)
set(_filemgr_bin  ${_wl_build}/filemgr)
set(_glsmoke_bin  ${_wl_build}/glsmoke)
set(_glmaze_bin   ${_wl_build}/glmaze)
set(_peanutgb_bin ${_wl_build}/peanutgb)
set(_mesaglsmoke_bin ${_wl_build}/mesaglsmoke)
set(_mesawlegl_bin ${_wl_build}/mesawlegl)
set(_mesakmsgl_bin ${_wl_build}/mesakmsgl)
set(_mesademo_bin ${_wl_build}/mesademo)
set(_mesaeglinfo_bin ${_wl_build}/mesaeglinfo)
set(_mesaglfeature_bin ${_wl_build}/mesaglfeature)
set(_mesad3d12probe_bin ${_wl_build}/mesad3d12probe)
set(_gpu_substrate_validate_bin ${_wl_build}/gpu-substrate-validate)
set(_gbmtest_bin ${_wl_build}/gbmtest)
set(_dmabufsmoke_bin ${_wl_build}/dmabufsmoke)
set(_cursorsmoke_bin ${_wl_build}/cursorsmoke)
set(_d3d12sharedsmoke_bin ${_wl_build}/d3d12sharedsmoke)
set(_drmgpuprobe_bin ${_wl_build}/drmgpuprobe)
set(_webkitgpusmoke_bin ${_wl_build}/webkitgpusmoke)
set(_fault_trace_lib ${_wl_build}/xv6_glibc_fault_trace.so)
set(_xv6egl_obj   ${_wl_build}/xv6_egl_gles.o)
set(_xv6egl_lib   ${_wl_build}/libEGL.a)
set(_xv6gles_lib  ${_wl_build}/libGLESv2.a)
set(_host_xv6egl_obj  ${_wl_build}/host-xv6_egl_gles.o)
set(_host_xv6egl_lib  ${_wl_build}/host-libEGL.a)
set(_host_xv6gles_lib ${_wl_build}/host-libGLESv2.a)
set(_weston_session_installed ${XV6_SYSROOT}/bin/weston-session)
set(_filemgr_installed ${XV6_SYSROOT}/bin/filemgr)
set(_glsmoke_installed ${XV6_SYSROOT}/bin/glsmoke)
set(_glmaze_installed ${XV6_SYSROOT}/bin/glmaze)
set(_peanutgb_installed ${XV6_SYSROOT}/bin/peanutgb)
set(_mesaglsmoke_installed ${XV6_SYSROOT}/bin/mesaglsmoke)
set(_mesawlegl_installed ${XV6_SYSROOT}/bin/mesawlegl)
set(_mesakmsgl_installed ${XV6_SYSROOT}/bin/mesakmsgl)
set(_mesademo_installed ${XV6_SYSROOT}/bin/mesademo)
set(_mesaeglinfo_installed ${XV6_SYSROOT}/bin/mesaeglinfo)
set(_mesaglfeature_installed ${XV6_SYSROOT}/bin/mesaglfeature)
set(_mesad3d12probe_installed ${XV6_SYSROOT}/bin/mesad3d12probe)
set(_gpu_substrate_validate_installed ${XV6_SYSROOT}/bin/gpu-substrate-validate)
set(_gbmtest_installed ${XV6_SYSROOT}/bin/gbmtest)
set(_dmabufsmoke_installed ${XV6_SYSROOT}/bin/dmabufsmoke)
set(_cursorsmoke_installed ${XV6_SYSROOT}/bin/cursorsmoke)
set(_d3d12sharedsmoke_installed ${XV6_SYSROOT}/bin/d3d12sharedsmoke)
set(_drmgpuprobe_installed ${XV6_SYSROOT}/bin/drmgpuprobe)
set(_webkitgpusmoke_installed ${XV6_SYSROOT}/bin/webkitgpusmoke)
set(_fault_trace_installed ${XV6_SYSROOT}/lib/xv6_glibc_fault_trace.so)
set(_webkit_preload_wrapper_bin ${_wl_build}/webkit-preload-wrapper)
set(_adwaita_cursor_dir ${XV6_SYSROOT}/share/icons/Adwaita/cursors)
set(_dnd_move_cursor ${_adwaita_cursor_dir}/dnd-move)
set(_dnd_copy_cursor ${_adwaita_cursor_dir}/dnd-copy)
set(_dnd_none_cursor ${_adwaita_cursor_dir}/dnd-none)
# The small xv6 EGL/GLES shim is linked directly into test programs from the
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
find_program(XV6_WEBKIT_HOST_CC NAMES cc gcc clang REQUIRED)

if(XV6_WAYLAND_HOST_GLIBC)
    find_program(XV6_HOST_CC NAMES cc gcc clang REQUIRED)
endif()

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
    OUTPUT  ${_dmabuf_client_header} ${_dmabuf_client_code}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_wl_proto_dir}
    COMMAND ${XV6_SYSROOT}/host-tools/bin/wayland-scanner client-header
                ${_dmabuf_xml} ${_dmabuf_client_header}
    COMMAND ${XV6_SYSROOT}/host-tools/bin/wayland-scanner private-code
                ${_dmabuf_xml} ${_dmabuf_client_code}
    DEPENDS ${_dmabuf_xml}
    COMMENT "Generating linux-dmabuf Wayland client protocol sources"
    VERBATIM)

add_custom_command(
    OUTPUT  ${_explicit_sync_client_header} ${_explicit_sync_client_code}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_wl_proto_dir}
    COMMAND ${XV6_SYSROOT}/host-tools/bin/wayland-scanner client-header
                ${_explicit_sync_xml} ${_explicit_sync_client_header}
    COMMAND ${XV6_SYSROOT}/host-tools/bin/wayland-scanner private-code
                ${_explicit_sync_xml} ${_explicit_sync_client_code}
    DEPENDS ${_explicit_sync_xml}
    COMMENT "Generating linux-explicit-sync Wayland client protocol sources"
    VERBATIM)

if(XV6_WAYLAND_HOST_GLIBC)
    add_custom_command(
        OUTPUT  ${_weston_session_bin}
        COMMAND ${XV6_HOST_CC} ${_wayland_opt_cflags} -Wall -Wno-unused-result
                    -o ${_weston_session_bin} ${WAYLAND_SRC_DIR}/desktop.c
        DEPENDS ${WAYLAND_SRC_DIR}/desktop.c ${DESKTOP_INCLUDE_SRCS}
        COMMENT "Compiling Weston session manager with host glibc"
        VERBATIM)
else()
    add_custom_command(
        OUTPUT  ${_weston_session_bin}
        COMMAND ${CMAKE_C_COMPILER} ${_wayland_opt_cflags} -Wall -static
                    -o ${_weston_session_bin} ${WAYLAND_SRC_DIR}/desktop.c
        DEPENDS ${WAYLAND_SRC_DIR}/desktop.c ${DESKTOP_INCLUDE_SRCS}
        COMMENT "Compiling Weston session manager from source"
        VERBATIM)
endif()

add_custom_command(
    OUTPUT  ${_webkit_preload_wrapper_bin}
    COMMAND ${XV6_WEBKIT_HOST_CC} -O2 -Wall
                -L ${XV6_SYSROOT}/lib
                -Wl,-rpath,/lib
                -Wl,-rpath-link,${XV6_SYSROOT}/lib
                -o ${_webkit_preload_wrapper_bin}
                ${WAYLAND_SRC_DIR}/webkit_preload_wrapper.c
                -Wl,--no-as-needed -lwebkit2gtk-4.1
                -Wl,--as-needed -ldl -pthread
    DEPENDS ${WAYLAND_SRC_DIR}/webkit_preload_wrapper.c
    COMMENT "Compiling WebKit process shim wrapper"
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
                ${WAYLAND_SRC_DIR}/font8x16.h
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
                ${WAYLAND_SRC_DIR}/font8x16.h
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
    OUTPUT  ${_mesaglfeature_bin}
    COMMAND ${CMAKE_C_COMPILER} ${_wayland_opt_cflags} -Wall
                -isystem ${XV6_SYSROOT}/include
                -isystem ${XV6_SYSROOT}/include/libdrm
                -L ${XV6_SYSROOT}/lib
                -Wl,-rpath-link,${XV6_SYSROOT}/lib
                -Wl,-rpath,/lib
                -o ${_mesaglfeature_bin}
                ${WAYLAND_SRC_DIR}/mesaglfeature.c
                -lEGL -lGLESv2 -lpthread -ldl -lrt -lm
    DEPENDS ${WAYLAND_SRC_DIR}/mesaglfeature.c
    COMMENT "Compiling Mesa EGL/GLES feature probe"
    VERBATIM)

add_custom_command(
    OUTPUT  ${_mesad3d12probe_bin}
    COMMAND ${CMAKE_C_COMPILER} ${_wayland_opt_cflags} -Wall
                -o ${_mesad3d12probe_bin}
                ${WAYLAND_SRC_DIR}/mesad3d12probe.c
    DEPENDS ${WAYLAND_SRC_DIR}/mesad3d12probe.c
    COMMENT "Compiling Mesa D3D12 runtime probe"
    VERBATIM)

add_custom_command(
    OUTPUT  ${_fault_trace_lib}
    COMMAND ${CMAKE_C_COMPILER} ${_wayland_opt_cflags} -Wall
                -shared -fPIC
                -DON_HOST_OS
                -idirafter ${XV6_REPO_ROOT}/kernel/kernel/inc
                -o ${_fault_trace_lib}
                ${XV6_FAULT_TRACE_SRC}
                -ldl
    DEPENDS ${XV6_FAULT_TRACE_SRC}
            ${XV6_REPO_ROOT}/kernel/kernel/inc/uabi/d3dkmthk.h
    COMMENT "Compiling xv6 glibc fault/DXG ioctl trace preload"
    VERBATIM)

add_custom_command(
    OUTPUT  ${_gpu_substrate_validate_bin}
    COMMAND ${CMAKE_C_COMPILER} ${_wayland_opt_cflags} -Wall
                -o ${_gpu_substrate_validate_bin}
                ${WAYLAND_SRC_DIR}/gpu_substrate_validate.c
    DEPENDS ${WAYLAND_SRC_DIR}/gpu_substrate_validate.c
    COMMENT "Compiling pure-C GPU substrate validator"
    VERBATIM)

add_custom_command(
    OUTPUT  ${_filemgr_bin}
    COMMAND ${CMAKE_C_COMPILER} ${_wayland_opt_cflags} -Wall
                -isystem ${XV6_SYSROOT}/include
                -I ${_wl_proto_dir}
                -I ${XV6_SYSROOT}/include/wayland
                -I ${WAYLAND_SRC_DIR}
                -L ${XV6_SYSROOT}/lib
                -Wl,-rpath-link,${XV6_SYSROOT}/lib
                -Wl,-rpath,/lib
                -o ${_filemgr_bin}
                ${WAYLAND_SRC_DIR}/filemgr.c
                ${WAYLAND_SRC_DIR}/xv6_draw.c
                ${WAYLAND_SRC_DIR}/xv6_icon.c
                ${WAYLAND_SRC_DIR}/xv6_icon_filemgr.c
                ${WAYLAND_SRC_DIR}/xv6_present_buffer.c
                ${_xdg_client_code}
                -lwayland-client -lffi -lpthread -ldl -lrt -lm
    DEPENDS ${WAYLAND_SRC_DIR}/filemgr.c
            ${WAYLAND_SRC_DIR}/xv6_draw.c
            ${WAYLAND_SRC_DIR}/xv6_draw.h
            ${WAYLAND_SRC_DIR}/xv6_icon.c
            ${WAYLAND_SRC_DIR}/xv6_icon.h
            ${WAYLAND_SRC_DIR}/xv6_icon_filemgr.c
            ${WAYLAND_SRC_DIR}/font8x16.h
            ${WAYLAND_SRC_DIR}/xv6_present_buffer.c
            ${WAYLAND_SRC_DIR}/xv6_present_buffer.h
            ${_xdg_client_header}
            ${_xdg_client_code}
    COMMENT "Compiling standalone Wayland file manager"
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
                ${WAYLAND_SRC_DIR}/mesaglsmoke.c
                ${WAYLAND_SRC_DIR}/gl_program.c
                ${WAYLAND_SRC_DIR}/mesawlegl_sphere.c
                ${WAYLAND_SRC_DIR}/pixel_fps_overlay.c
                ${WAYLAND_SRC_DIR}/xv6_present_buffer.c
                ${_xdg_client_code}
                -lEGL -lGLESv2 -lwayland-client
                -lgbm
                -lffi -lpthread -ldl -lrt -lm
    DEPENDS ${WAYLAND_SRC_DIR}/mesaglsmoke.c
            ${WAYLAND_SRC_DIR}/gl_program.c
            ${WAYLAND_SRC_DIR}/gl_program.h
            ${WAYLAND_SRC_DIR}/font8x16.h
            ${WAYLAND_SRC_DIR}/mesawlegl_sphere.c
            ${WAYLAND_SRC_DIR}/mesawlegl_sphere.h
            ${WAYLAND_SRC_DIR}/pixel_fps_overlay.c
            ${WAYLAND_SRC_DIR}/pixel_fps_overlay.h
            ${WAYLAND_SRC_DIR}/xv6_present_buffer.c
            ${WAYLAND_SRC_DIR}/xv6_present_buffer.h
            ${_xdg_client_header} ${_xdg_client_code}
    COMMENT "Compiling Mesa Wayland GL smoke client"
    VERBATIM)

add_custom_command(
    OUTPUT  ${_glmaze_bin}
    COMMAND ${CMAKE_C_COMPILER} ${_wayland_opt_cflags} -Wall
                -isystem ${XV6_SYSROOT}/include
                -I ${_wl_proto_dir}
                -I ${XV6_SYSROOT}/include/wayland
                -L ${XV6_SYSROOT}/lib
                -Wl,-rpath-link,${XV6_SYSROOT}/lib
                -Wl,-rpath,/lib
                -o ${_glmaze_bin}
                ${WAYLAND_SRC_DIR}/glmaze.c
                ${WAYLAND_SRC_DIR}/xv6_icon_glmaze.c
                ${WAYLAND_SRC_DIR}/xv6_present_buffer.c
                ${_xdg_client_code}
                -lEGL -lGLESv2 -lwayland-egl -lwayland-client
                -lffi -lpthread -ldl -lrt -lm
    DEPENDS ${WAYLAND_SRC_DIR}/glmaze.c
            ${WAYLAND_SRC_DIR}/xv6_icon.h
            ${WAYLAND_SRC_DIR}/xv6_icon_glmaze.c
            ${WAYLAND_SRC_DIR}/xv6_present_buffer.c
            ${WAYLAND_SRC_DIR}/xv6_present_buffer.h
            ${_xdg_client_header}
            ${_xdg_client_code}
    COMMENT "Compiling xv6 glMaze Wayland/EGL port"
    VERBATIM)

add_custom_command(
    OUTPUT  ${_peanutgb_bin}
    COMMAND ${CMAKE_C_COMPILER} ${_wayland_opt_cflags} -Wall
                -isystem ${XV6_SYSROOT}/include
                -I ${_wl_proto_dir}
                -I ${PEANUT_GB_SRC_DIR}
                -I ${XV6_SYSROOT}/include/wayland
                -L ${XV6_SYSROOT}/lib
                -Wl,-rpath-link,${XV6_SYSROOT}/lib
                -Wl,-rpath,/lib
                -o ${_peanutgb_bin}
                ${WAYLAND_SRC_DIR}/peanutgb.c
                ${WAYLAND_SRC_DIR}/xv6_icon_peanutgb.c
                ${WAYLAND_SRC_DIR}/xv6_draw.c
                ${WAYLAND_SRC_DIR}/xv6_present_buffer.c
                ${_xdg_client_code}
                -lwayland-client
                -lffi -lpthread -ldl -lrt -lm
    DEPENDS ${WAYLAND_SRC_DIR}/peanutgb.c
            ${WAYLAND_SRC_DIR}/xv6_icon.h
            ${WAYLAND_SRC_DIR}/xv6_icon_peanutgb.c
            ${WAYLAND_SRC_DIR}/xv6_draw.c
            ${WAYLAND_SRC_DIR}/xv6_draw.h
            ${PEANUT_GB_SRC_DIR}/peanut_gb.h
            ${WAYLAND_SRC_DIR}/xv6_present_buffer.c
            ${WAYLAND_SRC_DIR}/xv6_present_buffer.h
            ${_xdg_client_header}
            ${_xdg_client_code}
    COMMENT "Compiling Peanut-GB Wayland frontend"
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
                ${WAYLAND_SRC_DIR}/mesawlegl.c
                ${WAYLAND_SRC_DIR}/gl_program.c
                ${WAYLAND_SRC_DIR}/gl_fps_overlay.c
                ${WAYLAND_SRC_DIR}/mesawlegl_sphere.c
                ${WAYLAND_SRC_DIR}/xv6_present_buffer.c
                ${_xdg_client_code}
                -lEGL -lGLESv2 -lwayland-egl -lwayland-client
                -lgbm
                -lffi -lpthread -ldl -lrt -lm
    DEPENDS ${WAYLAND_SRC_DIR}/mesawlegl.c
            ${WAYLAND_SRC_DIR}/gl_program.c
            ${WAYLAND_SRC_DIR}/gl_program.h
            ${WAYLAND_SRC_DIR}/gl_fps_overlay.c
            ${WAYLAND_SRC_DIR}/gl_fps_overlay.h
            ${WAYLAND_SRC_DIR}/mesawlegl_sphere.c
            ${WAYLAND_SRC_DIR}/mesawlegl_sphere.h
            ${WAYLAND_SRC_DIR}/xv6_present_buffer.c
            ${WAYLAND_SRC_DIR}/xv6_present_buffer.h
            ${_xdg_client_header}
            ${_xdg_client_code}
    COMMENT "Compiling Mesa native Wayland EGL smoke client"
    VERBATIM)

add_custom_command(
    OUTPUT  ${_mesakmsgl_bin}
    COMMAND ${CMAKE_C_COMPILER} ${_wayland_opt_cflags} -Wall
                -isystem ${XV6_SYSROOT}/include
                -isystem ${XV6_SYSROOT}/include/libdrm
                -I ${WAYLAND_SRC_DIR}
                -L ${XV6_SYSROOT}/lib
                -Wl,-rpath-link,${XV6_SYSROOT}/lib
                -Wl,-rpath,/lib
                -o ${_mesakmsgl_bin}
                ${WAYLAND_SRC_DIR}/mesakmsgl.c
                ${WAYLAND_SRC_DIR}/mesawlegl_sphere.c
                -lEGL -lGLESv2 -lgbm -ldrm
                -lpthread -ldl -lrt -lm
    DEPENDS ${WAYLAND_SRC_DIR}/mesakmsgl.c
            ${WAYLAND_SRC_DIR}/mesawlegl_sphere.c
            ${WAYLAND_SRC_DIR}/mesawlegl_sphere.h
    COMMENT "Compiling Mesa direct KMS/GBM GL demo"
    VERBATIM)

add_custom_command(
    OUTPUT  ${_mesademo_bin}
    COMMAND ${CMAKE_C_COMPILER} ${_wayland_opt_cflags} -Wall
                -isystem ${XV6_SYSROOT}/include
                -o ${_mesademo_bin}
                ${WAYLAND_SRC_DIR}/mesademo.c
    DEPENDS ${WAYLAND_SRC_DIR}/mesademo.c
    COMMENT "Compiling Mesa 3D demo launcher"
    VERBATIM)

add_custom_command(
    OUTPUT  ${_gbmtest_bin}
    COMMAND ${CMAKE_C_COMPILER} ${_wayland_opt_cflags} -Wall
                -isystem ${XV6_SYSROOT}/include
                -isystem ${XV6_SYSROOT}/include/libdrm
                -L ${XV6_SYSROOT}/lib
                -Wl,-rpath-link,${XV6_SYSROOT}/lib
                -o ${_gbmtest_bin}
                ${WAYLAND_SRC_DIR}/gbmtest.c
                -lgbm -ldrm -lpthread -ldl -lrt -lm
    DEPENDS ${WAYLAND_SRC_DIR}/gbmtest.c
    COMMENT "Compiling GBM DRM smoke test"
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
                ${_dmabuf_client_code} ${_explicit_sync_client_code}
                -lgbm -lwayland-client
                -lffi -lpthread -ldl -lrt -lm
    DEPENDS ${WAYLAND_SRC_DIR}/dmabufsmoke.c ${_xdg_client_header}
            ${_xdg_client_code} ${_dmabuf_client_header}
            ${_dmabuf_client_code} ${_explicit_sync_client_header}
            ${_explicit_sync_client_code}
    COMMENT "Compiling linux-dmabuf smoke client"
    VERBATIM)

add_custom_command(
    OUTPUT  ${_cursorsmoke_bin}
    COMMAND ${CMAKE_C_COMPILER} ${_wayland_opt_cflags} -Wall
                -isystem ${XV6_SYSROOT}/include
                -I ${_wl_proto_dir}
                -I ${XV6_SYSROOT}/include/wayland
                -L ${XV6_SYSROOT}/lib
                -Wl,-rpath-link,${XV6_SYSROOT}/lib
                -o ${_cursorsmoke_bin}
                ${WAYLAND_SRC_DIR}/cursorsmoke.c ${_xdg_client_code}
                -lwayland-client
                -lffi -lpthread -ldl -lrt -lm
    DEPENDS ${WAYLAND_SRC_DIR}/cursorsmoke.c ${_xdg_client_header}
            ${_xdg_client_code}
    COMMENT "Compiling hardware cursor smoke client"
    VERBATIM)

if(_d3d12shared_ok)
    add_custom_command(
        OUTPUT  ${_d3d12sharedsmoke_bin}
        COMMAND ${CMAKE_C_COMPILER} ${_wayland_opt_cflags} -Wall
                    -isystem ${XV6_SYSROOT}/include
                    -I ${XV6_SYSROOT}/include/wsl/stubs
                    -I ${_wl_proto_dir}
                    -I ${XV6_SYSROOT}/include/wayland
                    -idirafter ${XV6_REPO_ROOT}/kernel/kernel/inc
                    -L ${XV6_SYSROOT}/lib
                    -Wl,-rpath-link,${XV6_SYSROOT}/lib
                    -L /usr/lib/wsl/lib
                    -o ${_d3d12sharedsmoke_bin}
                    ${WAYLAND_SRC_DIR}/d3d12sharedsmoke.c ${_xdg_client_code}
                    -ld3d12 -ldxcore -lwayland-client
                    -lffi -lpthread -ldl -lrt -lm
        DEPENDS ${WAYLAND_SRC_DIR}/d3d12sharedsmoke.c ${_xdg_client_header}
                ${_xdg_client_code}
                ${XV6_REPO_ROOT}/kernel/kernel/inc/uabi/d3dkmthk.h
        COMMENT "Compiling D3D12 shared-resource Wayland smoke client"
        VERBATIM)
else()
    set(_d3d12sharedsmoke_placeholder
        ${CMAKE_CURRENT_BINARY_DIR}/d3d12sharedsmoke-placeholder.sh)
    file(WRITE ${_d3d12sharedsmoke_placeholder}
        "#!/bin/sh\n"
        "echo d3d12sharedsmoke: libd3d12/libdxcore WSL runtime not staged >&2\n"
        "exit 77\n")
    add_custom_command(
        OUTPUT  ${_d3d12sharedsmoke_bin}
        COMMAND ${CMAKE_COMMAND} -E copy
                ${_d3d12sharedsmoke_placeholder} ${_d3d12sharedsmoke_bin}
        COMMAND chmod 0755 ${_d3d12sharedsmoke_bin}
        DEPENDS ${_d3d12sharedsmoke_placeholder}
        COMMENT "Writing D3D12 shared-resource smoke placeholder"
        VERBATIM)
endif()

set(_webkit_smoke_ok OFF)
option(XV6_BUILD_WEBKIT_GPU_SMOKE
    "Build the optional WebKit GPU policy smoke helper" ON)
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
    OUTPUT  ${_weston_session_installed}
            ${_filemgr_installed}
            ${_glsmoke_installed} ${_glmaze_installed}
            ${_peanutgb_installed}
            ${_mesaglsmoke_installed} ${_mesawlegl_installed}
            ${_mesakmsgl_installed}
            ${_mesademo_installed}
            ${_mesaeglinfo_installed} ${_mesaglfeature_installed}
            ${_mesad3d12probe_installed}
            ${_gpu_substrate_validate_installed}
            ${_gbmtest_installed}
            ${_dmabufsmoke_installed} ${_cursorsmoke_installed}
            ${_d3d12sharedsmoke_installed}
            ${_drmgpuprobe_installed} ${_webkitgpusmoke_installed}
            ${_fault_trace_installed}
            ${_dnd_move_cursor} ${_dnd_copy_cursor} ${_dnd_none_cursor}
    COMMAND ${CMAKE_COMMAND} -E rm -f
            ${XV6_SYSROOT}/bin/wlcomp
            ${XV6_SYSROOT}/bin/desktop
            ${XV6_SYSROOT}/lib/libxv6memshim.so
    COMMAND ${CMAKE_COMMAND} -E copy ${_weston_session_bin} ${_weston_session_installed}
    COMMAND ${CMAKE_COMMAND} -E copy ${_filemgr_bin} ${_filemgr_installed}
    COMMAND ${CMAKE_COMMAND} -E copy ${_glsmoke_bin} ${_glsmoke_installed}
    COMMAND ${CMAKE_COMMAND} -E copy ${_glmaze_bin} ${_glmaze_installed}
    COMMAND ${CMAKE_COMMAND} -E copy ${_peanutgb_bin} ${_peanutgb_installed}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${XV6_SYSROOT}/share/glmaze
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${WAYLAND_SRC_DIR}/glmaze-data ${XV6_SYSROOT}/share/glmaze
    COMMAND ${CMAKE_COMMAND} -E copy ${_mesaglsmoke_bin} ${_mesaglsmoke_installed}
    COMMAND ${CMAKE_COMMAND} -E copy ${_mesawlegl_bin} ${_mesawlegl_installed}
    COMMAND ${CMAKE_COMMAND} -E copy ${_mesakmsgl_bin} ${_mesakmsgl_installed}
    COMMAND ${CMAKE_COMMAND} -E copy ${_mesademo_bin} ${_mesademo_installed}
    COMMAND ${CMAKE_COMMAND} -E copy ${_mesaeglinfo_bin} ${_mesaeglinfo_installed}
    COMMAND ${CMAKE_COMMAND} -E copy ${_mesaglfeature_bin} ${_mesaglfeature_installed}
    COMMAND ${CMAKE_COMMAND} -E copy ${_mesad3d12probe_bin} ${_mesad3d12probe_installed}
    COMMAND ${CMAKE_COMMAND} -E copy ${_gpu_substrate_validate_bin} ${_gpu_substrate_validate_installed}
    COMMAND ${CMAKE_COMMAND} -E copy ${_gbmtest_bin} ${_gbmtest_installed}
    COMMAND ${CMAKE_COMMAND} -E copy ${_dmabufsmoke_bin} ${_dmabufsmoke_installed}
    COMMAND ${CMAKE_COMMAND} -E copy ${_cursorsmoke_bin} ${_cursorsmoke_installed}
    COMMAND ${CMAKE_COMMAND} -E copy ${_d3d12sharedsmoke_bin} ${_d3d12sharedsmoke_installed}
    COMMAND ${CMAKE_COMMAND} -E copy ${_drmgpuprobe_bin} ${_drmgpuprobe_installed}
    COMMAND ${CMAKE_COMMAND} -E copy ${_webkitgpusmoke_bin} ${_webkitgpusmoke_installed}
    COMMAND ${CMAKE_COMMAND} -E copy ${_fault_trace_lib} ${_fault_trace_installed}
    COMMAND bash ${WAYLAND_SRC_DIR}/install_webkit_preload_wrappers.sh
            ${_webkit_preload_wrapper_bin} ${XV6_SYSROOT}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_adwaita_cursor_dir}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_adwaita_cursor_dir}/move ${_dnd_move_cursor}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_adwaita_cursor_dir}/copy ${_dnd_copy_cursor}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_adwaita_cursor_dir}/no-drop ${_dnd_none_cursor}
    COMMAND ${CMAKE_COMMAND} -E rm -f ${_egl_installed} ${_gles_installed}
    DEPENDS ${_weston_session_bin} ${_filemgr_bin}
            ${_glsmoke_bin} ${_glmaze_bin} ${_peanutgb_bin}
            ${_mesaglsmoke_bin}
            ${_mesawlegl_bin} ${_mesakmsgl_bin} ${_mesademo_bin}
            ${_mesaeglinfo_bin} ${_mesaglfeature_bin}
            ${_mesad3d12probe_bin} ${_gpu_substrate_validate_bin}
            ${_gbmtest_bin} ${_dmabufsmoke_bin} ${_cursorsmoke_bin}
            ${_d3d12sharedsmoke_bin}
            ${_drmgpuprobe_bin} ${_webkitgpusmoke_bin}
            ${_fault_trace_lib} ${_webkit_preload_wrapper_bin}
            ${WAYLAND_SRC_DIR}/install_webkit_preload_wrappers.sh
    COMMENT "Installing GUI session into ${XV6_SYSROOT}/bin"
    VERBATIM)

add_custom_target(port-wayland ALL DEPENDS
    ${_weston_session_installed}
    ${_filemgr_installed}
    ${_glsmoke_installed} ${_glmaze_installed} ${_peanutgb_installed}
    ${_mesaglsmoke_installed} ${_mesawlegl_installed}
    ${_mesakmsgl_installed} ${_mesademo_installed}
    ${_mesaeglinfo_installed} ${_mesaglfeature_installed}
    ${_mesad3d12probe_installed}
    ${_gpu_substrate_validate_installed} ${_gbmtest_installed}
    ${_dmabufsmoke_installed} ${_cursorsmoke_installed}
    ${_d3d12sharedsmoke_installed}
    ${_drmgpuprobe_installed}
    ${_webkitgpusmoke_installed}
    ${_fault_trace_installed}
    ${_dnd_move_cursor} ${_dnd_copy_cursor} ${_dnd_none_cursor})
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
    COMMAND ${CMAKE_COMMAND} -E rm -rf
            ${_wl_build}
            ${CMAKE_CURRENT_BINARY_DIR}/wlcomp-build
            ${_host_wl_build}
    COMMAND ${CMAKE_COMMAND} -E remove -f
            ${XV6_SYSROOT}/bin/wlcomp
            ${XV6_SYSROOT}/bin/desktop
            ${_weston_session_installed}
            ${_filemgr_installed}
            ${_glsmoke_installed}
            ${_mesaglsmoke_installed} ${_mesawlegl_installed}
            ${_mesakmsgl_installed}
            ${_mesademo_installed}
            ${_mesaeglinfo_installed} ${_mesaglfeature_installed}
            ${_mesad3d12probe_installed}
            ${_gpu_substrate_validate_installed}
            ${_dmabufsmoke_installed} ${_cursorsmoke_installed}
            ${_d3d12sharedsmoke_installed}
            ${_drmgpuprobe_installed} ${_webkitgpusmoke_installed}
            ${_dnd_move_cursor} ${_dnd_copy_cursor} ${_dnd_none_cursor}
            ${XV6_SYSROOT}/lib/libxv6memshim.so
            ${_webkit_preload_wrapper_bin}
            ${_egl_installed} ${_gles_installed}
            ${_host_xv6egl_obj} ${_host_xv6egl_lib} ${_host_xv6gles_lib}
    COMMENT "Removing wayland artifacts from ${XV6_SYSROOT}")
