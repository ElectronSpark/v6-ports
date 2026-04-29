cmake_minimum_required(VERSION 3.16)
project(xv6-port-gdk-pixbuf LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

xv6_port(
    NAME            gdk-pixbuf
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    meson
    DEPENDS         glib libpng libjpeg-turbo glib-host
    OUTPUT_FILES    lib/libgdk_pixbuf-2.0.a
                    include/gdk-pixbuf-2.0/gdk-pixbuf/gdk-pixbuf.h
    CMAKE_ARGS
        -Dpng=enabled
        -Djpeg=enabled
        -Dtiff=disabled
        -Dgif=disabled
        -Dglycin=disabled
        -Dandroid=disabled
        -Dothers=disabled
        -Dbuiltin_loaders=all
        -Ddocumentation=false
        -Dgtk_doc=false
        -Dintrospection=disabled
        -Dman=false
        -Drelocatable=false
        -Dtests=false
        -Dinstalled_tests=false
        -Dbuild-tools=false
        -Dthumbnailer=disabled
        -Dgio_sniffing=false
)
