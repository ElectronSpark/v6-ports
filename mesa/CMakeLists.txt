cmake_minimum_required(VERSION 3.16)
project(xv6-port-mesa LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

# First Mesa milestone: upstream software Gallium/EGL, no LLVM, no Vulkan, no
# GLX/X11, and no hardware Gallium drivers.  xv6 presentation still goes
# through the existing Wayland compositor path; this port establishes the real
# Mesa build surface before adding an xv6-specific winsys/platform hook.
xv6_port(
    NAME            mesa
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    meson
    C_ARGS          -DDETECT_OS_XV6=1
    CPP_ARGS        -DDETECT_OS_XV6=1
                    -include
                    ${CMAKE_CURRENT_LIST_DIR}/../../toolchain/musl-xv6/compat/cxx_mutex_compat.h
    DEPENDS         khronos-headers
                    libdrm
                    libexpat
                    wayland-libs
                    wayland-protocols
                    zlib
    OUTPUT_FILES    lib/libEGL.so.1.0.0
                    lib/libGLESv2.so.2.0.0
                    lib/libgallium-26.2.0-devel.so
                    lib/dri/virtio_gpu_dri.so
                    lib/dri/swrast_dri.so
                    lib/pkgconfig/egl.pc
                    lib/pkgconfig/glesv2.pc
                    lib/pkgconfig/dri.pc
                    include/EGL/egl.h
                    include/GLES2/gl2.h
                    include/GL/gl.h
    CMAKE_ARGS
        -Dplatforms=wayland
        -Degl-native-platform=wayland
        -Degl=enabled
        -Dgles1=disabled
        -Dgles2=enabled
        -Dopengl=true
        -Dglx=disabled
        -Dgbm=disabled
        -Dglvnd=disabled
        -Dgallium-drivers=softpipe,virgl
        -Dvulkan-drivers=
        -Dllvm=disabled
        -Dshared-llvm=disabled
        -Ddraw-use-llvm=false
        -Dgallium-va=disabled
        -Dgallium-mediafoundation=disabled
        -Dgallium-mediafoundation-test=false
        -Dgallium-d3d10umd=false
        -Dgallium-rusticl=false
        -Dgallium-d3d12-video=disabled
        -Dgallium-d3d12-graphics=disabled
        -Dvideo-codecs=
        -Dshader-cache=disabled
        -Dxmlconfig=disabled
        -Dallow-kcmp=disabled
        -Dexpat=enabled
        -Dzlib=enabled
        -Dzstd=disabled
        -Dlibunwind=disabled
        -Dvalgrind=disabled
        -Dperfetto=false
        -Dgpuvis=false
        -Dsysprof=false
        -Dbuild-tests=false
        -Denable-glcpp-tests=false
        -Dbuild-radv-tests=false
        -Dbuild-aco-tests=false
        -Dinstall-intel-gpu-tests=false
        -Dtools=
        -Dhtml-docs=disabled
)

set(_mesa_libgl ${XV6_SYSROOT}/lib/libGL.so)
set(_mesa_libgl_pc ${XV6_SYSROOT}/lib/pkgconfig/gl.pc)
set(_mesa_dri_dir ${XV6_SYSROOT}/lib/dri)
set(_mesa_virtio_dri ${_mesa_dri_dir}/virtio_gpu_dri.so)
set(_mesa_swrast_dri ${_mesa_dri_dir}/swrast_dri.so)

add_custom_command(
    OUTPUT  ${_mesa_libgl} ${_mesa_libgl_pc}
    COMMAND ${CMAKE_COMMAND} -E create_symlink libGLESv2.so ${_mesa_libgl}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${XV6_SYSROOT}/lib/pkgconfig
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_LIST_DIR}/gl.pc
            ${_mesa_libgl_pc}
    DEPENDS port-mesa
    COMMENT "Installing xv6 Mesa libGL EGL-context shim"
    VERBATIM)

add_custom_target(port-mesa-libgl-shim ALL DEPENDS ${_mesa_libgl} ${_mesa_libgl_pc})
add_dependencies(port-mesa-libgl-shim port-mesa)

add_custom_command(
    OUTPUT  ${_mesa_virtio_dri} ${_mesa_swrast_dri}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_mesa_dri_dir}
    COMMAND ${CMAKE_COMMAND} -E create_symlink ../libgallium-26.2.0-devel.so
            ${_mesa_virtio_dri}
    COMMAND ${CMAKE_COMMAND} -E create_symlink ../libgallium-26.2.0-devel.so
            ${_mesa_swrast_dri}
    DEPENDS port-mesa
    COMMENT "Installing Mesa DRI driver aliases"
    VERBATIM)

add_custom_target(port-mesa-dri-aliases ALL
    DEPENDS ${_mesa_virtio_dri} ${_mesa_swrast_dri})
add_dependencies(port-mesa-dri-aliases port-mesa)
