cmake_minimum_required(VERSION 3.16)
project(xv6-port-mesa LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

# Mesa builds the software path, the existing virtio/virgl OpenGL-submit path,
# and the D3D12 Gallium driver needed for Hyper-V GPU-PV/DXG bring-up.  The
# D3D12 driver still depends on a usable dxcore/libd3d12 runtime in the guest,
# so Hyper-V only advertises OpenGL submit after runtime validation succeeds.
xv6_port(
    NAME            mesa
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    meson
    C_ARGS          -DDETECT_OS_XV6=1
    CPP_ARGS        -DDETECT_OS_XV6=1
    DEPENDS         khronos-headers
                    libdrm
                    libexpat
                    wayland-libs
                    wayland-protocols
                    zlib
    OUTPUT_FILES    lib/libEGL.so.1.0.0
                    lib/libGLESv2.so.2.0.0
                    lib/libgbm.so.1.0.0
                    lib/libgallium-26.2.0-devel.so
                    lib/dri/virtio_gpu_dri.so
                    lib/dri/d3d12_dri.so
                    lib/dri/swrast_dri.so
                    lib/dri/kms_swrast_dri.so
                    lib/pkgconfig/egl.pc
                    lib/pkgconfig/glesv2.pc
                    lib/pkgconfig/gbm.pc
                    lib/pkgconfig/dri.pc
                    include/EGL/egl.h
                    include/GLES2/gl2.h
                    include/GL/gl.h
                    include/gbm.h
    CMAKE_ARGS
        -Dplatforms=wayland
        -Degl-native-platform=wayland
        -Degl=enabled
        -Dgles1=disabled
        -Dgles2=enabled
        -Dopengl=true
        -Dglx=disabled
        -Dgbm=enabled
        -Dglvnd=disabled
        -Dgallium-drivers=softpipe,virgl,d3d12
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
        -Dgallium-d3d12-graphics=enabled
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
set(_mesa_d3d12_dri ${_mesa_dri_dir}/d3d12_dri.so)
set(_mesa_swrast_dri ${_mesa_dri_dir}/swrast_dri.so)
set(_mesa_kms_swrast_dri ${_mesa_dri_dir}/kms_swrast_dri.so)

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

add_custom_target(port-mesa-dri-aliases ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_mesa_dri_dir}
    COMMAND ${CMAKE_COMMAND} -E remove -f
            ${_mesa_virtio_dri}
            ${_mesa_d3d12_dri}
            ${_mesa_swrast_dri}
            ${_mesa_kms_swrast_dri}
    COMMAND ${CMAKE_COMMAND} -E create_symlink ../libgallium-26.2.0-devel.so
            ${_mesa_virtio_dri}
    COMMAND ${CMAKE_COMMAND} -E create_symlink ../libgallium-26.2.0-devel.so
            ${_mesa_d3d12_dri}
    COMMAND ${CMAKE_COMMAND} -E create_symlink ../libgallium-26.2.0-devel.so
            ${_mesa_swrast_dri}
    COMMAND ${CMAKE_COMMAND} -E create_symlink ../libgallium-26.2.0-devel.so
            ${_mesa_kms_swrast_dri}
    DEPENDS port-mesa
    COMMENT "Installing Mesa DRI driver aliases"
    VERBATIM)
add_dependencies(port-mesa-dri-aliases port-mesa)

set(_mesa_wsl_d3d12_runtime_dir "/usr/lib/wsl/lib" CACHE PATH
    "Directory containing the Microsoft WSL D3D12 runtime libraries")
set(_mesa_wsl_dxcore_src ${_mesa_wsl_d3d12_runtime_dir}/libdxcore.so)
set(_mesa_wsl_d3d12_src ${_mesa_wsl_d3d12_runtime_dir}/libd3d12.so)
set(_mesa_wsl_d3d12core_src ${_mesa_wsl_d3d12_runtime_dir}/libd3d12core.so)
set(_mesa_wsl_nv_d3d12_umd_src ${_mesa_wsl_d3d12_runtime_dir}/libnvwgf2umx.so)
file(GLOB _mesa_wsl_intel_d3d12_umd_dirs LIST_DIRECTORIES true
     "/usr/lib/wsl/drivers/iigd_dch.inf_amd64_*")
list(SORT _mesa_wsl_intel_d3d12_umd_dirs)
if(_mesa_wsl_intel_d3d12_umd_dirs)
    list(GET _mesa_wsl_intel_d3d12_umd_dirs -1
         _mesa_wsl_intel_d3d12_umd_dir)
endif()
set(_mesa_dxcore_installed ${XV6_SYSROOT}/lib/libdxcore.so)
set(_mesa_d3d12_installed ${XV6_SYSROOT}/lib/libd3d12.so)
set(_mesa_d3d12core_installed ${XV6_SYSROOT}/lib/libd3d12core.so)
set(_mesa_d3d12core_compat_installed ${XV6_SYSROOT}/lib/libD3D12Core.so)
set(_mesa_d3d12core_dll_compat_installed ${XV6_SYSROOT}/lib/D3D12Core.dll)
set(_mesa_nv_d3d12_umd_installed ${XV6_SYSROOT}/lib/libnvwgf2umx.so)
set(_mesa_intel_d3d12_umd_installed ${XV6_SYSROOT}/lib/libigd12umd64.so)
set(_mesa_intel_d3d12_xeh_installed ${XV6_SYSROOT}/lib/libigd12um64xeh.so)
set(_mesa_intel_d3d12_xel_installed ${XV6_SYSROOT}/lib/libigd12um64xel.so)
set(_mesa_intel_d3d12_dxva_installed ${XV6_SYSROOT}/lib/libigd12dxva64.so)
set(_mesa_intel_d3d12_gmm_w_installed ${XV6_SYSROOT}/lib/libigdgmm_w.so.12)
set(_mesa_intel_d3d12_gmm_installed ${XV6_SYSROOT}/lib/libigdgmm.so.12)
set(_mesa_intel_d3d12_igc_installed ${XV6_SYSROOT}/lib/libigc.so)
set(_mesa_intel_d3d12_llvm_installed ${XV6_SYSROOT}/lib/libLLVM-14.so)

if(EXISTS ${_mesa_wsl_dxcore_src}
   AND EXISTS ${_mesa_wsl_d3d12_src}
   AND EXISTS ${_mesa_wsl_d3d12core_src}
   AND EXISTS ${_mesa_wsl_nv_d3d12_umd_src})
    add_custom_command(
        OUTPUT  ${_mesa_dxcore_installed}
                ${_mesa_d3d12_installed}
                ${_mesa_d3d12core_installed}
                ${_mesa_d3d12core_compat_installed}
                ${_mesa_d3d12core_dll_compat_installed}
                ${_mesa_nv_d3d12_umd_installed}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_mesa_wsl_dxcore_src}
                ${_mesa_dxcore_installed}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_mesa_wsl_d3d12_src}
                ${_mesa_d3d12_installed}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_mesa_wsl_d3d12core_src}
                ${_mesa_d3d12core_installed}
        COMMAND ${CMAKE_COMMAND} -E create_symlink
                libd3d12core.so
                ${_mesa_d3d12core_compat_installed}
        COMMAND ${CMAKE_COMMAND} -E create_symlink
                libd3d12core.so
                ${_mesa_d3d12core_dll_compat_installed}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_mesa_wsl_nv_d3d12_umd_src}
                ${_mesa_nv_d3d12_umd_installed}
        DEPENDS port-mesa
        COMMENT "Staging Microsoft WSL D3D12 runtime libraries"
        VERBATIM)
    add_custom_target(port-mesa-wsl-d3d12-runtime ALL
        DEPENDS ${_mesa_dxcore_installed}
                ${_mesa_d3d12_installed}
                ${_mesa_d3d12core_installed}
                ${_mesa_d3d12core_compat_installed}
                ${_mesa_d3d12core_dll_compat_installed}
                ${_mesa_nv_d3d12_umd_installed})
    add_dependencies(port-mesa-wsl-d3d12-runtime port-mesa)
else()
    message(STATUS
        "ports/mesa: WSL D3D12 runtime libraries not found in "
        "${_mesa_wsl_d3d12_runtime_dir}; Hyper-V Mesa D3D12 will need "
        "libdxcore.so/libd3d12.so staged separately")
endif()

if(_mesa_wsl_intel_d3d12_umd_dir
   AND EXISTS ${_mesa_wsl_intel_d3d12_umd_dir}/libigd12umd64.so
   AND EXISTS ${_mesa_wsl_intel_d3d12_umd_dir}/libigd12um64xeh.so
   AND EXISTS ${_mesa_wsl_intel_d3d12_umd_dir}/libigd12um64xel.so
   AND EXISTS ${_mesa_wsl_intel_d3d12_umd_dir}/libigd12dxva64.so
   AND EXISTS ${_mesa_wsl_intel_d3d12_umd_dir}/libigdgmm_w.so.12
   AND EXISTS ${_mesa_wsl_intel_d3d12_umd_dir}/libigc.so
   AND EXISTS ${_mesa_wsl_intel_d3d12_umd_dir}/libLLVM-14.so)
    add_custom_command(
        OUTPUT  ${_mesa_intel_d3d12_umd_installed}
                ${_mesa_intel_d3d12_xeh_installed}
                ${_mesa_intel_d3d12_xel_installed}
                ${_mesa_intel_d3d12_dxva_installed}
                ${_mesa_intel_d3d12_gmm_w_installed}
                ${_mesa_intel_d3d12_gmm_installed}
                ${_mesa_intel_d3d12_igc_installed}
                ${_mesa_intel_d3d12_llvm_installed}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_mesa_wsl_intel_d3d12_umd_dir}/libigd12umd64.so
                ${_mesa_intel_d3d12_umd_installed}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_mesa_wsl_intel_d3d12_umd_dir}/libigd12um64xeh.so
                ${_mesa_intel_d3d12_xeh_installed}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_mesa_wsl_intel_d3d12_umd_dir}/libigd12um64xel.so
                ${_mesa_intel_d3d12_xel_installed}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_mesa_wsl_intel_d3d12_umd_dir}/libigd12dxva64.so
                ${_mesa_intel_d3d12_dxva_installed}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_mesa_wsl_intel_d3d12_umd_dir}/libigdgmm_w.so.12
                ${_mesa_intel_d3d12_gmm_w_installed}
        COMMAND ${CMAKE_COMMAND} -E create_symlink
                libigdgmm_w.so.12
                ${_mesa_intel_d3d12_gmm_installed}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_mesa_wsl_intel_d3d12_umd_dir}/libigc.so
                ${_mesa_intel_d3d12_igc_installed}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_mesa_wsl_intel_d3d12_umd_dir}/libLLVM-14.so
                ${_mesa_intel_d3d12_llvm_installed}
        DEPENDS port-mesa
        COMMENT "Staging Intel WSL D3D12 user-mode driver"
        VERBATIM)
    add_custom_target(port-mesa-wsl-intel-d3d12-runtime ALL
        DEPENDS ${_mesa_intel_d3d12_umd_installed}
                ${_mesa_intel_d3d12_xeh_installed}
                ${_mesa_intel_d3d12_xel_installed}
                ${_mesa_intel_d3d12_dxva_installed}
                ${_mesa_intel_d3d12_gmm_w_installed}
                ${_mesa_intel_d3d12_gmm_installed}
                ${_mesa_intel_d3d12_igc_installed}
                ${_mesa_intel_d3d12_llvm_installed})
    add_dependencies(port-mesa-wsl-intel-d3d12-runtime port-mesa)
else()
    message(STATUS
        "ports/mesa: Intel WSL D3D12 UMD libraries not found; Intel "
        "Hyper-V D3D12 will need libigd12umd64.so staged separately")
endif()
