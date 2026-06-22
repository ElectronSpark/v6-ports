cmake_minimum_required(VERSION 3.16)
project(xv6-port-weston LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

set(_weston_build "${CMAKE_BINARY_DIR}/weston-build")

xv6_port(
    NAME            weston
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    meson
    MESON_DEFAULT_LIBRARY shared
    DEPENDS         cairo
                    hwdata
                    libdrm
                    libevdev
                    libinput
                    libseat
                    libudev
                    libxkbcommon
                    mesa
                    pixman
                    wayland-libs
                    wayland-protocols
    OUTPUT_FILES    bin/weston
                    bin/libexec_weston.so
                    bin/libexec_weston.so.0
                    bin/libexec_weston.so.0.0.0
                    bin/weston-terminal
                    lib/libweston-15.so.0
                    lib/libweston-15/drm-backend.so
                    lib/libweston-15/gl-renderer.so
                    lib/libweston-15/xwayland.so
                    lib/weston/desktop-shell.so
                    lib/weston/kiosk-shell.so
                    lib/weston/libexec_weston.so
                    lib/weston/libexec_weston.so.0
                    lib/weston/libexec_weston.so.0.0.0
                    libexec/weston-desktop-shell
    CMAKE_ARGS
        -Dbackend-drm=true
        -Dbackend-default=drm
        -Dbackend-headless=false
        -Dbackend-pipewire=false
        -Dbackend-rdp=false
        -Dbackend-vnc=false
        -Dbackend-wayland=false
        -Dbackend-x11=false
        -Drenderer-gl=true
        -Drenderer-vulkan=false
        -Dxwayland=true
        -Dxwayland-path=/bin/Xwayland
        -Dsystemd=false
        -Dremoting=false
        -Dpipewire=false
        -Dshell-desktop=true
        -Dshell-kiosk=true
        -Dshell-ivi=false
        -Dshell-lua=false
        -Dcolor-management-lcms=false
        -Dimage-jpeg=false
        -Dimage-webp=false
        -Dtools=terminal
        -Ddemo-clients=false
        -Dsimple-clients=[]
        -Dresize-pool=false
        -Dwcap-decode=false
        -Dtests=false
        -Dtest-junit-xml=false
        -Ddoc=false
        -Dperfetto=false
    POST_INSTALL_COMMANDS
        ${CMAKE_COMMAND} -E make_directory
            ${XV6_SYSROOT}/bin
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_weston_build}/frontend/weston
            ${XV6_SYSROOT}/bin/weston
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${XV6_SYSROOT}/lib/weston/libexec_weston.so.0.0.0
            ${XV6_SYSROOT}/bin/libexec_weston.so.0.0.0
        COMMAND ${CMAKE_COMMAND} -E rm -f
            ${XV6_SYSROOT}/bin/libexec_weston.so.0
            ${XV6_SYSROOT}/bin/libexec_weston.so
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            libexec_weston.so.0.0.0
            ${XV6_SYSROOT}/bin/libexec_weston.so.0
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            libexec_weston.so.0
            ${XV6_SYSROOT}/bin/libexec_weston.so
)

set(_weston_xv6_config "${XV6_SYSROOT}/etc/xdg/weston/weston.ini")
add_custom_command(
    OUTPUT  ${_weston_xv6_config}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${XV6_SYSROOT}/etc/xdg/weston"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/xv6-weston.ini"
            "${_weston_xv6_config}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/xv6-weston.ini"
    COMMENT "Installing xv6 Weston runtime signature"
    VERBATIM)
add_custom_target(port-weston-config DEPENDS ${_weston_xv6_config})
add_dependencies(port-weston port-weston-config)

set(_xwayland_runtime_stamp "${CMAKE_CURRENT_BINARY_DIR}/xwayland-runtime.stamp")
set(_xwayland_wrapper_src "${CMAKE_CURRENT_LIST_DIR}/xwayland-glamor-wrapper.c")
set(_xwayland_wrapper_bin "${CMAKE_CURRENT_BINARY_DIR}/Xwayland")
set(_xwayland_stage_script
    "${CMAKE_CURRENT_LIST_DIR}/../../scripts/image/stage-xwayland-runtime.sh")
add_custom_command(
    OUTPUT  ${_xwayland_runtime_stamp}
    COMMAND /usr/bin/cc -O2 -Wall -Wextra
            "${_xwayland_wrapper_src}"
            -o "${_xwayland_wrapper_bin}"
    COMMAND "${_xwayland_stage_script}"
            "${XV6_SYSROOT}"
            "${_xwayland_wrapper_bin}"
    COMMAND ${CMAKE_COMMAND} -E touch ${_xwayland_runtime_stamp}
    DEPENDS "${_xwayland_wrapper_src}"
            "${_xwayland_stage_script}"
    COMMENT "Installing staged Xwayland runtime"
    VERBATIM)
add_custom_target(port-weston-xwayland-runtime DEPENDS ${_xwayland_runtime_stamp})
add_dependencies(port-weston port-weston-xwayland-runtime)

set(_weston_meson_env
    "PATH=${XV6_SYSROOT}/host-tools/bin:$ENV{PATH}"
    "PKG_CONFIG_SYSROOT_DIR=${XV6_SYSROOT}"
    "PKG_CONFIG_LIBDIR=${XV6_SYSROOT}/lib/pkgconfig:${XV6_SYSROOT}/share/pkgconfig"
    "PKG_CONFIG_PATH="
    "PKG_CONFIG_PATH_FOR_BUILD=${XV6_SYSROOT}/host-tools/lib/pkgconfig:${XV6_SYSROOT}/host-tools/lib/x86_64-linux-gnu/pkgconfig:${XV6_SYSROOT}/host-tools/share/pkgconfig")
add_custom_target(port-weston-runtime-refresh
    COMMAND ${CMAKE_COMMAND} -E env
            ${_weston_meson_env}
            meson compile -C "${_weston_build}"
    COMMAND ${CMAKE_COMMAND} -E env
            ${_weston_meson_env}
            "DESTDIR=${XV6_SYSROOT}"
            meson install -C "${_weston_build}" --no-rebuild
    COMMENT "Rebuilding/restaging Weston without the aggregate port dependency graph"
    VERBATIM)
add_dependencies(port-weston-runtime-refresh
    port-weston-config
    port-weston-xwayland-runtime)
