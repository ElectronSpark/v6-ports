cmake_minimum_required(VERSION 3.16)
project(xv6-port-webkit LANGUAGES NONE)

set(_webkit_overrides_dir "${CMAKE_CURRENT_LIST_DIR}/overrides/webkitgtk-2.42.5")
if(EXISTS "${_webkit_overrides_dir}/Tools/MiniBrowser/gtk/main.c")
    message(STATUS
        "ports/webkit: WebKitGTK 2.42.5 xv6 source overrides are available at "
        "${_webkit_overrides_dir}")
endif()

if(NOT DEFINED XV6_SYSROOT OR XV6_SYSROOT STREQUAL "")
    message(FATAL_ERROR "ports/webkit: -DXV6_SYSROOT=<path> is required.")
endif()

if(DEFINED ENV{XV6_WEBKIT_REF_SYSROOT})
    set(_webkit_default_ref_sysroot "$ENV{XV6_WEBKIT_REF_SYSROOT}")
    set(_webkit_force_ref_sysroot TRUE)
else()
    set(_webkit_default_ref_sysroot "${CMAKE_CURRENT_LIST_DIR}/sysroot")
    set(_webkit_force_ref_sysroot FALSE)
endif()

set(XV6_WEBKIT_REF_SYSROOT "${_webkit_default_ref_sysroot}"
    CACHE PATH "Optional sysroot containing a WebKitGTK xv6 runtime to stage")
set(XV6_WEBKIT_STRICT_STAGE ON
    CACHE BOOL "Fail port-webkit when the selected runtime sysroot is incomplete")
if(_webkit_force_ref_sysroot
   AND NOT XV6_WEBKIT_REF_SYSROOT STREQUAL _webkit_default_ref_sysroot)
    set(XV6_WEBKIT_REF_SYSROOT "${_webkit_default_ref_sysroot}" CACHE PATH
        "Optional sysroot containing a WebKitGTK xv6 runtime to stage" FORCE)
elseif(XV6_WEBKIT_REF_SYSROOT MATCHES "/xv6-tmp/build-x86/sysroot$"
   AND NOT DEFINED ENV{XV6_WEBKIT_REF_SYSROOT})
    set(XV6_WEBKIT_REF_SYSROOT "${_webkit_default_ref_sysroot}" CACHE PATH
        "Optional sysroot containing a WebKitGTK xv6 runtime to stage" FORCE)
endif()

set(_webkit_stage_stamp
    "${XV6_SYSROOT}/libexec/webkit2gtk-4.1/.webkit-stage.stamp")
set(_webkit_stage_manifest
    "${XV6_SYSROOT}/libexec/webkit2gtk-4.1/.webkit-stage-manifest")
set(_webkit_stage_script
    "${CMAKE_CURRENT_LIST_DIR}/stage-webkit-runtime.sh")
set(_webkit_ref_runtime "")
if(EXISTS "${XV6_WEBKIT_REF_SYSROOT}/libexec/webkit2gtk-4.1/MiniBrowser")
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
        "ports/webkit: no WebKitGTK runtime at ${XV6_WEBKIT_REF_SYSROOT}; "
        "set -DXV6_WEBKIT_REF_SYSROOT=<sysroot> or env XV6_WEBKIT_REF_SYSROOT to stage one")
endif()

set(_webkit_deps "")
foreach(_dep gtk3 glib gdk-pixbuf pango atk cairo fontconfig freetype
             fribidi libepoxy libffi libpng libxkbcommon openssl)
    if(TARGET port-${_dep})
        list(APPEND _webkit_deps port-${_dep})
    endif()
endforeach()

add_custom_command(
    OUTPUT  ${_webkit_stage_stamp}
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${XV6_SYSROOT}/libexec/webkit2gtk-4.1"
    COMMAND ${_webkit_stage_script}
            "${XV6_WEBKIT_REF_SYSROOT}"
            "${XV6_SYSROOT}"
            "$<IF:$<BOOL:${XV6_WEBKIT_STRICT_STAGE}>,1,0>"
    DEPENDS ${_webkit_stage_script} ${_webkit_ref_runtime} ${_webkit_deps}
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
