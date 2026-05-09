cmake_minimum_required(VERSION 3.16)
project(xv6-port-libxkbcommon LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

xv6_port(
    NAME            libxkbcommon
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    meson
    DEPENDS         wayland-libs wayland-host
    OUTPUT_FILES    lib/libxkbcommon.a
                    include/xkbcommon/xkbcommon.h
    CMAKE_ARGS
        -Denable-x11=false
        -Denable-docs=false
        -Denable-tools=false
        -Denable-wayland=true
        -Denable-xkbregistry=false
        -Denable-bash-completion=false
        -Denable-zsh-completion=false
)

set(XKEYBOARD_CONFIG_DIR "/usr/share/X11/xkb"
    CACHE PATH "Host xkeyboard-config directory to stage for libxkbcommon")

if(EXISTS "${XKEYBOARD_CONFIG_DIR}/rules/evdev")
    set(_xkb_data_stamp ${CMAKE_CURRENT_BINARY_DIR}/xkeyboard-config.stamp)
    set(_xkb_runtime_dir ${XV6_SYSROOT}/share/X11/xkb)
    add_custom_command(
        OUTPUT  ${_xkb_data_stamp}
        BYPRODUCTS ${_xkb_runtime_dir}/rules/evdev
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${_xkb_runtime_dir}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${XV6_SYSROOT}/share/X11
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                    ${XKEYBOARD_CONFIG_DIR}
                    ${_xkb_runtime_dir}
        COMMAND ${CMAKE_COMMAND} -E touch ${_xkb_data_stamp}
        COMMENT "Staging xkeyboard-config data for libxkbcommon"
        VERBATIM)
    add_custom_target(port-libxkbcommon-xkb-data ALL
        DEPENDS ${_xkb_data_stamp})
    add_dependencies(port-libxkbcommon port-libxkbcommon-xkb-data)
else()
    message(WARNING
        "ports/libxkbcommon: ${XKEYBOARD_CONFIG_DIR}/rules/evdev not found; "
        "GTK/Wayland clients may fail to create XKB keymaps at runtime.")
endif()
