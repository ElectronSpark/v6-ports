cmake_minimum_required(VERSION 3.16)
project(xv6-port-webkit LANGUAGES NONE)

if(NOT DEFINED XV6_SYSROOT OR XV6_SYSROOT STREQUAL "")
    message(FATAL_ERROR "ports/webkit: -DXV6_SYSROOT=<path> is required.")
endif()

if(DEFINED ENV{XV6_WEBKIT_REF_SYSROOT})
    set(_webkit_default_ref_sysroot "$ENV{XV6_WEBKIT_REF_SYSROOT}")
else()
    set(_webkit_default_ref_sysroot "${CMAKE_CURRENT_LIST_DIR}/sysroot")
endif()

set(XV6_WEBKIT_REF_SYSROOT "${_webkit_default_ref_sysroot}"
    CACHE PATH "Optional sysroot containing a WebKitGTK xv6 runtime to stage")
if(XV6_WEBKIT_REF_SYSROOT MATCHES "/xv6-tmp/build-x86/sysroot$"
   AND NOT DEFINED ENV{XV6_WEBKIT_REF_SYSROOT})
    set(XV6_WEBKIT_REF_SYSROOT "${_webkit_default_ref_sysroot}" CACHE PATH
        "Optional sysroot containing a WebKitGTK xv6 runtime to stage" FORCE)
endif()

set(_webkit_stage_stamp
    "${XV6_SYSROOT}/libexec/webkit2gtk-4.1/.webkit-stage.stamp")
set(_webkit_stage_script
    "${CMAKE_CURRENT_LIST_DIR}/stage-webkit-runtime.sh")
set(_webkit_ref_runtime "")
if(EXISTS "${XV6_WEBKIT_REF_SYSROOT}/libexec/webkit2gtk-4.1/MiniBrowser")
    set(_webkit_ref_runtime
        "${XV6_WEBKIT_REF_SYSROOT}/libexec/webkit2gtk-4.1/MiniBrowser"
        "${XV6_WEBKIT_REF_SYSROOT}/libexec/webkit2gtk-4.1/WebKitNetworkProcess"
        "${XV6_WEBKIT_REF_SYSROOT}/libexec/webkit2gtk-4.1/WebKitWebProcess"
        "${XV6_WEBKIT_REF_SYSROOT}/libexec/webkit2gtk-4.1/.webkit_install_stamp"
        "${XV6_WEBKIT_REF_SYSROOT}/lib/libwebkit2gtk-4.1.so"
        "${XV6_WEBKIT_REF_SYSROOT}/lib/libjavascriptcoregtk-4.1.so")
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
    DEPENDS ${_webkit_stage_script} ${_webkit_ref_runtime} ${_webkit_deps}
    COMMENT "Staging optional WebKitGTK runtime from ${XV6_WEBKIT_REF_SYSROOT}"
    VERBATIM)

add_custom_target(port-webkit ALL
    DEPENDS ${_webkit_stage_stamp})
