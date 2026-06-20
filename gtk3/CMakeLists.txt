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
    MESON_DEFAULT_LIBRARY both
    DEPENDS         glib glib-host gdk-pixbuf pango atk cairo
                    wayland-libs wayland-protocols libxkbcommon
                    fribidi harfbuzz fontconfig freetype libepoxy
                    linux-uapi-headers
    SOURCE_DEPENDS  ${CMAKE_CURRENT_SOURCE_DIR}/src/gdk/wayland/gdkscreen-wayland.c
                    ${CMAKE_CURRENT_SOURCE_DIR}/src/gtk/gtkheaderbar.c
    OUTPUT_FILES    lib/libgtk-3.a
                    lib/libgtk-3.so
                    lib/libgtk-3.so.0
                    lib/libgtk-3.so.0.2421.32
                    lib/libgdk-3.a
                    lib/libgdk-3.so
                    lib/libgdk-3.so.0
                    lib/libgdk-3.so.0.2421.32
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
        -Dbuiltin_immodules=all
        -Ddemos=false
        -Dexamples=false
        -Dtests=false
        -Dinstalled_tests=false
)

set(GTK_CURSOR_THEME_DIR "/usr/share/icons/Adwaita"
    CACHE PATH "Host Adwaita cursor theme directory to stage for GTK/Wayland")

if(EXISTS "${GTK_CURSOR_THEME_DIR}/cursors/default")
    set(_gtk_cursor_stamp ${CMAKE_CURRENT_BINARY_DIR}/cursor-theme.stamp)
    add_custom_command(
        OUTPUT  ${_gtk_cursor_stamp}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${XV6_SYSROOT}/share/icons
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                    ${GTK_CURSOR_THEME_DIR}
                    ${XV6_SYSROOT}/share/icons/Adwaita
        COMMAND ${CMAKE_COMMAND} -E make_directory ${XV6_SYSROOT}/share/icons/default
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${GTK_CURSOR_THEME_DIR}/cursor.theme
                    ${XV6_SYSROOT}/share/icons/default/index.theme
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${GTK_CURSOR_THEME_DIR}/cursors
                ${XV6_SYSROOT}/share/icons/default/cursors
        COMMAND ${CMAKE_COMMAND} -E touch ${_gtk_cursor_stamp}
        COMMENT "Staging Adwaita cursor theme for GTK/Wayland"
        VERBATIM)
    add_custom_target(port-gtk3-cursor-theme ALL
        DEPENDS ${_gtk_cursor_stamp})
else()
    message(WARNING
        "ports/gtk3: ${GTK_CURSOR_THEME_DIR}/cursors/default not found; "
        "GTK/Wayland clients may fail to load cursor theme default.")
endif()

set(_gtk_schemas_dir ${XV6_SYSROOT}/share/glib-2.0/schemas)
set(_gtk_schemas_compiled ${_gtk_schemas_dir}/gschemas.compiled)
add_custom_command(
    OUTPUT  ${_gtk_schemas_compiled}
    COMMAND ${CMAKE_COMMAND} -E env
                "PATH=${XV6_SYSROOT}/host-tools/bin:$ENV{PATH}"
                ${XV6_SYSROOT}/host-tools/bin/glib-compile-schemas
                ${_gtk_schemas_dir}
    DEPENDS port-gtk3 port-glib-host
    COMMENT "Compiling GTK GSettings schemas"
    VERBATIM)
add_custom_target(port-gtk3-schemas ALL
    DEPENDS ${_gtk_schemas_compiled})

add_custom_target(port-gtk3-schemas-refresh
    COMMAND ${CMAKE_COMMAND} -E env
                "PATH=${XV6_SYSROOT}/host-tools/bin:$ENV{PATH}"
                ${XV6_SYSROOT}/host-tools/bin/glib-compile-schemas
                ${_gtk_schemas_dir}
    DEPENDS port-glib-host
    COMMENT "Refreshing GTK GSettings schema cache"
    VERBATIM)

set(_gtk_runtime_links_stamp ${CMAKE_CURRENT_BINARY_DIR}/runtime-links.stamp)
add_custom_command(
    OUTPUT  ${_gtk_runtime_links_stamp}
    COMMAND ${CMAKE_COMMAND} -E rm -f
                ${XV6_SYSROOT}/lib/libgdk-3.so
                ${XV6_SYSROOT}/lib/libgdk-3.so.0
                ${XV6_SYSROOT}/lib/libgdk-3.so.0.2409.32
                ${XV6_SYSROOT}/lib/libgtk-3.so
                ${XV6_SYSROOT}/lib/libgtk-3.so.0
                ${XV6_SYSROOT}/lib/libgtk-3.so.0.2409.32
    COMMAND ${CMAKE_COMMAND} -E create_symlink
                libgdk-3.so.0
                ${XV6_SYSROOT}/lib/libgdk-3.so
    COMMAND ${CMAKE_COMMAND} -E create_symlink
                libgdk-3.so.0.2421.32
                ${XV6_SYSROOT}/lib/libgdk-3.so.0
    COMMAND ${CMAKE_COMMAND} -E create_symlink
                libgtk-3.so.0
                ${XV6_SYSROOT}/lib/libgtk-3.so
    COMMAND ${CMAKE_COMMAND} -E create_symlink
                libgtk-3.so.0.2421.32
                ${XV6_SYSROOT}/lib/libgtk-3.so.0
    COMMAND ${CMAKE_COMMAND} -E touch ${_gtk_runtime_links_stamp}
    DEPENDS port-gtk3
    COMMENT "Normalizing GTK/GDK runtime soname links"
    VERBATIM)
add_custom_target(port-gtk3-runtime-links ALL
    DEPENDS ${_gtk_runtime_links_stamp})
