cmake_minimum_required(VERSION 3.16)
project(xv6-port-khronos-headers LANGUAGES NONE)

# Header-only port: install Khronos EGL/GL/KHR API headers into the
# sysroot so other ports (e.g. libepoxy with -Degl=yes, gtk3 wayland
# backend) can compile against them. No libraries — actual GL/EGL
# entry points are resolved at runtime via libepoxy + a Wayland
# compositor that exposes EGL.

set(_egl   ${CMAKE_CURRENT_SOURCE_DIR}/egl-registry/api)
set(_gl    ${CMAKE_CURRENT_SOURCE_DIR}/opengl-registry/api)
set(_dst   ${XV6_SYSROOT}/include)
set(_marker ${_dst}/EGL/eglplatform.h)

add_custom_command(
    OUTPUT  ${_marker}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_dst}
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${_egl}/EGL ${_dst}/EGL
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${_egl}/KHR ${_dst}/KHR
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${_gl}/GL   ${_dst}/GL
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${_gl}/GLES2 ${_dst}/GLES2
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${_gl}/GLES3 ${_dst}/GLES3
    COMMENT "Installing Khronos EGL/GL headers"
    VERBATIM)

add_custom_target(port-khronos-headers ALL DEPENDS ${_marker})

add_custom_target(port-khronos-headers-clean
    COMMAND ${CMAKE_COMMAND} -E rm -rf
            ${_dst}/EGL ${_dst}/KHR ${_dst}/GL ${_dst}/GLES2 ${_dst}/GLES3
    COMMENT "Removing Khronos headers")
