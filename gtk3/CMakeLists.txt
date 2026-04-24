cmake_minimum_required(VERSION 3.16)
project(xv6-port-gtk3 LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

# GTK 3 (gtk-3-24 branch) — Wayland-only build for xv6.
# X11/win32/quartz/broadway backends are disabled because xv6 does not
# host any of them. Documentation, demos, tests and introspection are
# off to keep the static library footprint small.
xv6_port(
    NAME            gtk3
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    meson
    DEPENDS         glib glib-host gdk-pixbuf pango atk cairo
                    wayland-libs wayland-protocols libxkbcommon
                    fribidi harfbuzz fontconfig freetype libepoxy
                    linux-uapi-headers
    OUTPUT_FILES    lib/libgtk-3.a
                    lib/libgdk-3.a
                    include/gtk-3.0/gtk/gtk.h
    CMAKE_ARGS
        -Dwayland_backend=true
        -Dx11_backend=false
        -Dbroadway_backend=false
        -Dwin32_backend=false
        -Dquartz_backend=false
        -Dxinerama=no
        -Dcloudproviders=false
        -Dprofiler=false
        -Dtracker3=false
        -Dprint_backends=file
        -Dcolord=no
        -Dgtk_doc=false
        -Dman=false
        -Dintrospection=false
        -Ddemos=false
        -Dexamples=false
        -Dtests=false
        -Dinstalled_tests=false
)
