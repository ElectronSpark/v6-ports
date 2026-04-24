# AddPort.cmake — shared helper for individual port CMakeLists.txt.
#
# Each port lives at xv6-os/ports/<name>/ with its own standalone
# CMakeLists.txt (so it can be built independently or via the umbrella).
# This module provides the xv6_port() function which wraps the
# upstream port's native build system in CMake custom_commands.
#
# Usage (inside ports/<name>/CMakeLists.txt):
#
#   cmake_minimum_required(VERSION 3.16)
#   project(xv6-port-zlib LANGUAGES NONE)
#   include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)
#   xv6_port(
#       NAME            zlib
#       SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
#       BUILD_SYSTEM    cmake               # cmake | autoconf | make
#       OUTPUT_FILES    lib/libz.a include/zlib.h
#       CMAKE_ARGS      -DZLIB_BUILD_SHARED=OFF -DZLIB_BUILD_STATIC=ON
#       DEPENDS                             # other port targets
#   )
#
# Required configuration vars (set by caller or umbrella):
#   CMAKE_C_COMPILER, CMAKE_AR, CMAKE_RANLIB — cross toolchain
#   XV6_SYSROOT          — install destination (= musl sysroot)
#   XV6_PORT_CFLAGS      — common compile flags for ports (sysroot, isystem...)
#
# Targets created per port:
#   port-<name>          — aggregate (default ALL target)
#   port-<name>-clean    — wipe build dir (sysroot not rolled back)

include_guard(GLOBAL)

# ----------------------------------------------------------------------------
# Resolve common configuration with sensible standalone fallbacks.
# ----------------------------------------------------------------------------
function(_xv6_port_resolve_config)
    if(NOT DEFINED XV6_SYSROOT OR XV6_SYSROOT STREQUAL "")
        set(XV6_SYSROOT "${CMAKE_BINARY_DIR}/sysroot" PARENT_SCOPE)
        set(_resolved_sysroot "${CMAKE_BINARY_DIR}/sysroot")
    else()
        set(_resolved_sysroot "${XV6_SYSROOT}")
    endif()
    file(MAKE_DIRECTORY "${_resolved_sysroot}/lib")
    file(MAKE_DIRECTORY "${_resolved_sysroot}/include")

    if(NOT DEFINED XV6_PORT_CFLAGS OR XV6_PORT_CFLAGS STREQUAL "")
        # Minimal default — caller usually overrides with the full
        # musl --sysroot=... -isystem... incantation.
        set(XV6_PORT_CFLAGS "-O2 -fPIC" PARENT_SCOPE)
    endif()
endfunction()

# ----------------------------------------------------------------------------
# xv6_port(NAME ... SOURCE_DIR ... BUILD_SYSTEM ... OUTPUT_FILES ... ...)
# ----------------------------------------------------------------------------
function(xv6_port)
    set(opts)
    set(one_value NAME SOURCE_DIR BUILD_SYSTEM JOBS)
    set(multi_value DEPENDS
                    OUTPUT_FILES
                    CMAKE_ARGS
                    CONFIGURE_ARGS
                    MAKE_ARGS
                    INSTALL_ARGS)
    cmake_parse_arguments(P "${opts}" "${one_value}" "${multi_value}" ${ARGN})

    foreach(req NAME SOURCE_DIR BUILD_SYSTEM OUTPUT_FILES)
        if(NOT P_${req})
            message(FATAL_ERROR "xv6_port: ${req} is required")
        endif()
    endforeach()

    _xv6_port_resolve_config()

    if(NOT P_JOBS)
        include(ProcessorCount)
        ProcessorCount(P_JOBS)
        if(P_JOBS EQUAL 0)
            set(P_JOBS 4)
        endif()
    endif()

    set(_name   ${P_NAME})
    set(_src    ${P_SOURCE_DIR})
    set(_build  ${CMAKE_BINARY_DIR}/${_name}-build)

    # Translate OUTPUT_FILES (relative to sysroot) to absolute paths.
    set(_out_abs "")
    foreach(f IN LISTS P_OUTPUT_FILES)
        list(APPEND _out_abs "${XV6_SYSROOT}/${f}")
    endforeach()

    # Inter-port deps: depend on aggregate targets; the OUTPUT files
    # of those ports show up in the sysroot, but cmake doesn't see
    # them as file deps unless we add them — that's the OUTPUT_FILES
    # contract on the dep side.
    set(_dep_targets "")
    foreach(d IN LISTS P_DEPENDS)
        list(APPEND _dep_targets port-${d})
    endforeach()

    # ------------------------------------------------------------------
    # Per-build-system command tuple.
    # ------------------------------------------------------------------
    if(P_BUILD_SYSTEM STREQUAL "cmake")
        set(_cmake_configure
            ${CMAKE_COMMAND}
                -S ${_src}
                -B ${_build}
                -DCMAKE_SYSTEM_NAME=Linux
                -DCMAKE_BUILD_TYPE=Release
                -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
                -DCMAKE_AR=${CMAKE_AR}
                -DCMAKE_RANLIB=${CMAKE_RANLIB}
                -DCMAKE_C_FLAGS=${XV6_PORT_CFLAGS}
                -DCMAKE_INSTALL_PREFIX=/
                -DCMAKE_INSTALL_LIBDIR=/lib
                -DCMAKE_INSTALL_INCLUDEDIR=/include
                -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                ${P_CMAKE_ARGS})
        set(_build_cmd
            ${CMAKE_COMMAND} --build ${_build} -j${P_JOBS} ${P_MAKE_ARGS})
        set(_install_cmd
            ${CMAKE_COMMAND} -E env DESTDIR=${XV6_SYSROOT}
            ${CMAKE_COMMAND} --install ${_build} ${P_INSTALL_ARGS})

    elseif(P_BUILD_SYSTEM STREQUAL "autoconf")
        # autoconf in-tree builds: copy src -> build first to keep
        # source tree clean.
        get_filename_component(_cc_name "${CMAKE_C_COMPILER}" NAME)
        string(REGEX REPLACE "-gcc$" "" _triple "${_cc_name}")
        set(_cmake_configure
            ${CMAKE_COMMAND} -E make_directory ${_build}
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${_src} ${_build}
            COMMAND ${CMAKE_COMMAND} -E chdir ${_build}
                ${CMAKE_COMMAND} -E env
                    CC=${CMAKE_C_COMPILER}
                    AR=${CMAKE_AR}
                    RANLIB=${CMAKE_RANLIB}
                    "CFLAGS=${XV6_PORT_CFLAGS}"
                ./configure
                    --prefix=/
                    --libdir=/lib
                    --includedir=/include
                    --host=${_triple}
                    ${P_CONFIGURE_ARGS})
        set(_build_cmd
            ${CMAKE_COMMAND} -E chdir ${_build} make -j${P_JOBS} ${P_MAKE_ARGS})
        set(_install_cmd
            ${CMAKE_COMMAND} -E chdir ${_build}
            make DESTDIR=${XV6_SYSROOT} install ${P_INSTALL_ARGS})

    elseif(P_BUILD_SYSTEM STREQUAL "meson")
        # Meson cross-build. We synthesize a cross-file at CMake
        # configure time from the toolchain we already know about,
        # then drive `meson setup / compile / install`.
        get_filename_component(_cc_name "${CMAKE_C_COMPILER}" NAME)
        get_filename_component(_cc_dir  "${CMAKE_C_COMPILER}" DIRECTORY)
        string(REGEX REPLACE "-gcc$" "" _triple "${_cc_name}")
        set(_strip "${_cc_dir}/${_triple}-strip")
        set(_cxx   "${_cc_dir}/${_triple}-g++")
        if(NOT EXISTS "${_strip}")
            set(_strip "strip")
        endif()
        if(NOT EXISTS "${_cxx}")
            set(_cxx "${CMAKE_C_COMPILER}")
        endif()

        # Split XV6_PORT_CFLAGS into a meson list literal: ['-a','-b']
        separate_arguments(_cflag_list UNIX_COMMAND "${XV6_PORT_CFLAGS}")
        set(_cflag_meson "")
        foreach(f IN LISTS _cflag_list)
            if(_cflag_meson STREQUAL "")
                set(_cflag_meson "'${f}'")
            else()
                set(_cflag_meson "${_cflag_meson}, '${f}'")
            endif()
        endforeach()

        set(_crossfile "${CMAKE_BINARY_DIR}/${_name}-cross.ini")
        file(WRITE "${_crossfile}"
"# Auto-generated by xv6_port() — do not edit.\n"
"[binaries]\n"
"c          = '${CMAKE_C_COMPILER}'\n"
"cpp        = '${_cxx}'\n"
"ar         = '${CMAKE_AR}'\n"
"strip      = '${_strip}'\n"
"ranlib     = '${CMAKE_RANLIB}'\n"
"pkg-config = 'pkg-config'\n"
"glib-compile-resources = '${XV6_SYSROOT}/host-tools/bin/glib-compile-resources'\n"
"glib-compile-schemas   = '${XV6_SYSROOT}/host-tools/bin/glib-compile-schemas'\n"
"glib-genmarshal        = '${XV6_SYSROOT}/bin/glib-genmarshal'\n"
"glib-mkenums           = '${XV6_SYSROOT}/bin/glib-mkenums'\n"
"glib-gettextize        = '${XV6_SYSROOT}/bin/glib-gettextize'\n"
"wayland-scanner        = '${XV6_SYSROOT}/host-tools/bin/wayland-scanner'\n"
"\n"
"[host_machine]\n"
"system     = 'linux'\n"
"cpu_family = 'x86_64'\n"
"cpu        = 'x86_64'\n"
"endian     = 'little'\n"
"\n"
"[properties]\n"
"sys_root          = '${XV6_SYSROOT}'\n"
"pkg_config_libdir = '${XV6_SYSROOT}/lib/pkgconfig:${XV6_SYSROOT}/share/pkgconfig'\n"
"needs_exe_wrapper = true\n"
"\n"
"[built-in options]\n"
"c_args      = [${_cflag_meson}]\n"
"c_link_args = [${_cflag_meson}]\n"
"cpp_args    = [${_cflag_meson}]\n"
"cpp_link_args = [${_cflag_meson}]\n"
"prefix      = '/'\n"
"libdir      = 'lib'\n"
"includedir  = 'include'\n"
"default_library = 'static'\n"
)

        # Meson refuses to re-setup if the build dir already exists.
        # Wipe it on every drive so CMAKE_ARGS changes take effect.
        set(_meson_setup
            ${CMAKE_COMMAND} -E rm -rf ${_build}
            COMMAND ${CMAKE_COMMAND} -E env
                "PATH=${XV6_SYSROOT}/host-tools/bin:$ENV{PATH}"
                "PKG_CONFIG_SYSROOT_DIR=${XV6_SYSROOT}"
                "PKG_CONFIG_LIBDIR=${XV6_SYSROOT}/lib/pkgconfig:${XV6_SYSROOT}/share/pkgconfig"
                "PKG_CONFIG_PATH=${XV6_SYSROOT}/host-tools/lib/pkgconfig:${XV6_SYSROOT}/host-tools/lib/x86_64-linux-gnu/pkgconfig:${XV6_SYSROOT}/host-tools/share/pkgconfig"
                "PKG_CONFIG_PATH_FOR_BUILD=${XV6_SYSROOT}/host-tools/lib/pkgconfig:${XV6_SYSROOT}/host-tools/lib/x86_64-linux-gnu/pkgconfig:${XV6_SYSROOT}/host-tools/share/pkgconfig"
                meson setup ${_build} ${_src}
                    --cross-file=${_crossfile}
                    --buildtype=release
                    --default-library=static
                    -Dprefix=/
                    -Dlibdir=lib
                    -Dincludedir=include
                    ${P_CMAKE_ARGS})
        set(_cmake_configure ${_meson_setup})
        set(_build_cmd
            ${CMAKE_COMMAND} -E env
                "PATH=${XV6_SYSROOT}/host-tools/bin:$ENV{PATH}"
                "PKG_CONFIG_SYSROOT_DIR=${XV6_SYSROOT}"
                "PKG_CONFIG_LIBDIR=${XV6_SYSROOT}/lib/pkgconfig:${XV6_SYSROOT}/share/pkgconfig"
                "PKG_CONFIG_PATH=${XV6_SYSROOT}/host-tools/lib/pkgconfig:${XV6_SYSROOT}/host-tools/lib/x86_64-linux-gnu/pkgconfig:${XV6_SYSROOT}/host-tools/share/pkgconfig"
                "PKG_CONFIG_PATH_FOR_BUILD=${XV6_SYSROOT}/host-tools/lib/pkgconfig:${XV6_SYSROOT}/host-tools/lib/x86_64-linux-gnu/pkgconfig:${XV6_SYSROOT}/host-tools/share/pkgconfig"
                meson compile -C ${_build} -j ${P_JOBS} ${P_MAKE_ARGS})
        set(_install_cmd
            ${CMAKE_COMMAND} -E env
                "PATH=${XV6_SYSROOT}/host-tools/bin:$ENV{PATH}"
                "DESTDIR=${XV6_SYSROOT}"
                meson install -C ${_build} --no-rebuild ${P_INSTALL_ARGS})

    elseif(P_BUILD_SYSTEM STREQUAL "make")
        set(_cmake_configure
            ${CMAKE_COMMAND} -E echo "no configure step for ${_name}")
        set(_build_cmd
            make -C ${_src}
                CC=${CMAKE_C_COMPILER}
                AR=${CMAKE_AR}
                RANLIB=${CMAKE_RANLIB}
                "CFLAGS=${XV6_PORT_CFLAGS}"
                PREFIX=/
                -j${P_JOBS} ${P_MAKE_ARGS})
        set(_install_cmd
            make -C ${_src}
                DESTDIR=${XV6_SYSROOT}
                PREFIX=/
                install ${P_INSTALL_ARGS})

    else()
        message(FATAL_ERROR
            "xv6_port(${_name}): unsupported BUILD_SYSTEM=${P_BUILD_SYSTEM} "
            "(expected cmake|autoconf|meson|make)")
    endif()

    # ------------------------------------------------------------------
    # Wire it as a single custom_command -> output files in sysroot.
    # ------------------------------------------------------------------
    add_custom_command(
        OUTPUT  ${_out_abs}
        COMMAND ${_cmake_configure}
        COMMAND ${_build_cmd}
        COMMAND ${_install_cmd}
        DEPENDS ${_dep_targets}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Building port ${_name}"
        VERBATIM)

    add_custom_target(port-${_name} ALL DEPENDS ${_out_abs})
    foreach(d IN LISTS _dep_targets)
        add_dependencies(port-${_name} ${d})
    endforeach()

    add_custom_target(port-${_name}-clean
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${_build}
        COMMAND ${CMAKE_COMMAND} -E rm -f  ${_out_abs}
        COMMENT "Removing build artifacts + installed files for port ${_name}")
endfunction()
