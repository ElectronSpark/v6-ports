cmake_minimum_required(VERSION 3.16)
project(xv6-port-pango LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

xv6_port(
    NAME            pango
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    meson
    MESON_DEFAULT_LIBRARY both
    DEPENDS         glib harfbuzz fontconfig cairo fribidi freetype glib-host
    OUTPUT_FILES    lib/libpango-1.0.a
                    lib/libpango-1.0.so
                    lib/libpango-1.0.so.0
                    lib/libpango-1.0.so.0.5702.0
                    lib/libpangocairo-1.0.a
                    lib/libpangocairo-1.0.so
                    lib/libpangocairo-1.0.so.0
                    lib/libpangocairo-1.0.so.0.5702.0
                    lib/libpangoft2-1.0.a
                    lib/libpangoft2-1.0.so
                    lib/libpangoft2-1.0.so.0
                    lib/libpangoft2-1.0.so.0.5702.0
                    include/pango-1.0/pango/pango.h
    CMAKE_ARGS
        -Ddocumentation=false
        -Dgtk_doc=false
        -Dman-pages=false
        -Dintrospection=disabled
        -Dbuild-testsuite=false
        -Dbuild-examples=false
        -Dfontconfig=enabled
        -Dsysprof=disabled
        -Dlibthai=disabled
        -Dcairo=enabled
        -Dxft=disabled
        -Dfreetype=enabled
)
