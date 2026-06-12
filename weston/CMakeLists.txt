cmake_minimum_required(VERSION 3.16)
project(xv6-port-weston LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

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
                    bin/weston-terminal
                    lib/libweston-15.so.0
                    lib/libweston-15/drm-backend.so
                    lib/libweston-15/gl-renderer.so
                    lib/libweston-15/xwayland.so
                    lib/weston/desktop-shell.so
                    lib/weston/kiosk-shell.so
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

set(_xwayland_runtime_root
    "${CMAKE_CURRENT_LIST_DIR}/../../rootfs-overlay/opt/host-gui/xwayland/usr")
set(_xwayland_server_src "${_xwayland_runtime_root}/bin/Xwayland")
set(_xwayland_xkbcomp_src "${_xwayland_runtime_root}/bin/xkbcomp")
set(_xwayland_xkb_data_src "${_xwayland_runtime_root}/share/X11")
set(_xwayland_runtime_stamp "${CMAKE_CURRENT_BINARY_DIR}/xwayland-runtime.stamp")
add_custom_command(
    OUTPUT  ${_xwayland_runtime_stamp}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${XV6_SYSROOT}/bin"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${XV6_SYSROOT}/usr/bin"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${XV6_SYSROOT}/usr/share"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${XV6_SYSROOT}/lib"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_xwayland_server_src}"
            "${XV6_SYSROOT}/bin/Xwayland"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_xwayland_xkbcomp_src}"
            "${XV6_SYSROOT}/usr/bin/xkbcomp"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${_xwayland_runtime_root}/lib/x86_64-linux-gnu"
            "${XV6_SYSROOT}/lib"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${_xwayland_xkb_data_src}"
            "${XV6_SYSROOT}/usr/share/X11"
    COMMAND ${CMAKE_COMMAND} -E touch ${_xwayland_runtime_stamp}
    DEPENDS "${_xwayland_server_src}"
            "${_xwayland_xkbcomp_src}"
            "${_xwayland_xkb_data_src}/xkb/keycodes/evdev"
    COMMENT "Installing staged Xwayland runtime"
    VERBATIM)
add_custom_target(port-weston-xwayland-runtime DEPENDS ${_xwayland_runtime_stamp})
add_dependencies(port-weston port-weston-xwayland-runtime)
