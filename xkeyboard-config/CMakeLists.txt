cmake_minimum_required(VERSION 3.16)
project(xv6-port-xkeyboard-config LANGUAGES NONE)

set(XKEYBOARD_CONFIG_DIR "/usr/share/X11/xkb"
    CACHE PATH "Host xkeyboard-config directory to package")
set(_xkb_runtime_dir ${XV6_SYSROOT}/share/X11/xkb)
set(_xkb_data_stamp ${CMAKE_CURRENT_BINARY_DIR}/xkeyboard-config.stamp)

if(NOT EXISTS "${XKEYBOARD_CONFIG_DIR}/rules/evdev")
    message(FATAL_ERROR
        "ports/xkeyboard-config: ${XKEYBOARD_CONFIG_DIR}/rules/evdev not found")
endif()

add_custom_command(
    OUTPUT  ${_xkb_data_stamp}
    BYPRODUCTS ${_xkb_runtime_dir}/rules/evdev
               ${_xkb_runtime_dir}/keycodes/evdev
               ${_xkb_runtime_dir}/symbols/us
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${_xkb_runtime_dir}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${XV6_SYSROOT}/share/X11
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${XKEYBOARD_CONFIG_DIR}
            ${_xkb_runtime_dir}
    COMMAND ${CMAKE_COMMAND} -E touch ${_xkb_data_stamp}
    COMMENT "Packaging xkeyboard-config runtime data"
    VERBATIM)

add_custom_target(port-xkeyboard-config ALL
    DEPENDS ${_xkb_data_stamp})

add_custom_target(port-xkeyboard-config-clean
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${_xkb_runtime_dir}
    COMMAND ${CMAKE_COMMAND} -E rm -f ${_xkb_data_stamp}
    COMMENT "Removing xkeyboard-config runtime data")
