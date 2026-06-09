cmake_minimum_required(VERSION 3.16)
project(xv6-port-libxkbcommon LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

xv6_port(
    NAME            libxkbcommon
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    meson
    DEPENDS         wayland-libs wayland-host xkeyboard-config
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
