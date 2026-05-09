cmake_minimum_required(VERSION 3.16)
project(xv6-port-webkit LANGUAGES NONE)

set(_webkit_overrides_dir "${CMAKE_CURRENT_LIST_DIR}/overrides/webkitgtk-2.42.5")
file(GLOB_RECURSE _webkit_override_files
    RELATIVE "${_webkit_overrides_dir}"
    "${_webkit_overrides_dir}/*")
list(LENGTH _webkit_override_files _webkit_override_count)
if(_webkit_override_count GREATER 0)
    message(STATUS
        "ports/webkit: ${_webkit_override_count} WebKitGTK 2.42.5 xv6 "
        "source override(s) are available at ${_webkit_overrides_dir}")
else()
    message(STATUS
        "ports/webkit: no WebKitGTK 2.42.5 xv6 source overrides are active")
endif()

if(NOT DEFINED XV6_SYSROOT OR XV6_SYSROOT STREQUAL "")
    message(FATAL_ERROR "ports/webkit: -DXV6_SYSROOT=<path> is required.")
endif()

if(DEFINED ENV{XV6_WEBKIT_REF_SYSROOT})
    set(_webkit_default_ref_sysroot "$ENV{XV6_WEBKIT_REF_SYSROOT}")
    set(_webkit_force_ref_sysroot TRUE)
else()
    set(_webkit_default_ref_sysroot "")
    set(_webkit_force_ref_sysroot FALSE)
endif()

set(XV6_WEBKIT_REF_SYSROOT "${_webkit_default_ref_sysroot}"
    CACHE PATH "Optional host-glibc sysroot containing a WebKitGTK runtime to stage")
set(XV6_WEBKIT_STRICT_STAGE OFF
    CACHE BOOL "Fail port-webkit when the selected runtime sysroot is incomplete")
if(_webkit_force_ref_sysroot
   AND NOT XV6_WEBKIT_REF_SYSROOT STREQUAL _webkit_default_ref_sysroot)
    set(XV6_WEBKIT_REF_SYSROOT "${_webkit_default_ref_sysroot}" CACHE PATH
        "Optional sysroot containing a WebKitGTK xv6 runtime to stage" FORCE)
elseif(XV6_WEBKIT_REF_SYSROOT MATCHES "/xv6-tmp/build-x86/sysroot$"
   AND NOT DEFINED ENV{XV6_WEBKIT_REF_SYSROOT})
    set(XV6_WEBKIT_REF_SYSROOT "${_webkit_default_ref_sysroot}" CACHE PATH
        "Optional sysroot containing a WebKitGTK xv6 runtime to stage" FORCE)
elseif(XV6_WEBKIT_REF_SYSROOT STREQUAL "${CMAKE_CURRENT_LIST_DIR}/sysroot"
   AND NOT DEFINED ENV{XV6_WEBKIT_REF_SYSROOT})
    set(XV6_WEBKIT_REF_SYSROOT "" CACHE PATH
        "Optional host-glibc sysroot containing a WebKitGTK runtime to stage" FORCE)
endif()
if(NOT DEFINED ENV{XV6_WEBKIT_REF_SYSROOT}
   AND XV6_WEBKIT_REF_SYSROOT STREQUAL "")
    set(XV6_WEBKIT_STRICT_STAGE OFF CACHE BOOL
        "Fail port-webkit when the selected runtime sysroot is incomplete" FORCE)
endif()

set(_webkit_stage_stamp
    "${XV6_SYSROOT}/libexec/webkit2gtk-4.1/.webkit-stage.stamp")
set(_webkit_stage_manifest
    "${XV6_SYSROOT}/libexec/webkit2gtk-4.1/.webkit-stage-manifest")
set(_webkit_stage_script
    "${CMAKE_CURRENT_LIST_DIR}/stage-webkit-runtime.sh")
set(_webkit_ref_runtime "")
if(NOT XV6_WEBKIT_REF_SYSROOT STREQUAL ""
   AND EXISTS "${XV6_WEBKIT_REF_SYSROOT}/libexec/webkit2gtk-4.1/MiniBrowser")
    set(_webkit_ref_runtime
        "${XV6_WEBKIT_REF_SYSROOT}/libexec/webkit2gtk-4.1/MiniBrowser"
        "${XV6_WEBKIT_REF_SYSROOT}/libexec/webkit2gtk-4.1/WebKitNetworkProcess"
        "${XV6_WEBKIT_REF_SYSROOT}/libexec/webkit2gtk-4.1/WebKitWebProcess"
        "${XV6_WEBKIT_REF_SYSROOT}/libexec/webkit2gtk-4.1/jsc"
        "${XV6_WEBKIT_REF_SYSROOT}/libexec/webkit2gtk-4.1/.webkit_install_stamp"
        "${XV6_WEBKIT_REF_SYSROOT}/lib/libwebkit2gtk-4.1.so"
        "${XV6_WEBKIT_REF_SYSROOT}/lib/libjavascriptcoregtk-4.1.so"
        "${XV6_WEBKIT_REF_SYSROOT}/lib/webkit2gtk-4.1/injected-bundle/libwebkit2gtkinjectedbundle.so")
else()
    message(STATUS
        "ports/webkit: no host-glibc WebKitGTK runtime selected; "
        "set -DXV6_WEBKIT_REF_SYSROOT=<sysroot> or env XV6_WEBKIT_REF_SYSROOT to stage one")
endif()

set(_webkit_deps "")
foreach(_dep gtk3 glib gdk-pixbuf pango atk cairo fontconfig freetype
             fribidi libepoxy libffi libpng libxkbcommon openssl)
    if(TARGET port-${_dep})
        list(APPEND _webkit_deps port-${_dep})
    endif()
endforeach()

set(_webkit_runtime_stack_files
    "${XV6_SYSROOT}/lib/libgtk-3.so.0"
    "${XV6_SYSROOT}/lib/libgdk-3.so.0"
    "${XV6_SYSROOT}/lib/libharfbuzz.so.0"
    "${XV6_SYSROOT}/lib/libpango-1.0.so.0"
    "${XV6_SYSROOT}/lib/libpangocairo-1.0.so.0"
    "${XV6_SYSROOT}/lib/libcairo.so.2"
    "${XV6_SYSROOT}/lib/libcairo-gobject.so.2")

add_custom_command(
    OUTPUT  ${_webkit_stage_stamp}
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${XV6_SYSROOT}/libexec/webkit2gtk-4.1"
    COMMAND ${_webkit_stage_script}
            "${XV6_WEBKIT_REF_SYSROOT}"
            "${XV6_SYSROOT}"
            "$<IF:$<BOOL:${XV6_WEBKIT_STRICT_STAGE}>,1,0>"
    DEPENDS ${_webkit_stage_script} ${_webkit_ref_runtime} ${_webkit_deps}
            ${_webkit_runtime_stack_files}
    BYPRODUCTS ${_webkit_stage_manifest}
    COMMENT "Staging optional WebKitGTK runtime from ${XV6_WEBKIT_REF_SYSROOT}"
    VERBATIM)

add_custom_target(port-webkit ALL
    DEPENDS ${_webkit_stage_stamp})

add_custom_target(port-webkit-clean
    COMMAND ${_webkit_stage_script} "" "${XV6_SYSROOT}" 0
    COMMAND ${CMAKE_COMMAND} -E rm -f
            "${_webkit_stage_stamp}"
            "${_webkit_stage_manifest}"
    COMMENT "Removing staged WebKitGTK runtime")
