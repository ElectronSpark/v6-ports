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
                    lib/libweston-15.so.0
                    lib/libweston-15/drm-backend.so
                    lib/libweston-15/gl-renderer.so
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
        -Dxwayland=false
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
        -Dtools=[]
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
