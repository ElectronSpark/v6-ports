cmake_minimum_required(VERSION 3.16)
project(xv6-port-mesa LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

# Mesa builds the software path, the existing virtio/virgl OpenGL-submit path,
# and the D3D12 Gallium driver needed for Hyper-V GPU-PV/DXG bring-up.  The
# D3D12 driver still depends on a usable dxcore/libd3d12 runtime in the guest,
# so Hyper-V only advertises OpenGL submit after runtime validation succeeds.
option(XV6_MESA_ENABLE_NOUVEAU
       "Enable Mesa's Nouveau Gallium driver after xv6 Nouveau validators pass"
       OFF)
set(XV6_MESA_NOUVEAU_VALIDATION_LOG
    "${CMAKE_BINARY_DIR}/hyperv-gpu-core-validate.log"
    CACHE FILEPATH
    "Validator log required before enabling Mesa Nouveau")

set(_mesa_gallium_drivers "softpipe,virgl,d3d12")
set(_mesa_output_files
    lib/libEGL.so.1.0.0
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
    include/gbm.h)

function(_mesa_append_existing_source_deps)
    foreach(_dep IN LISTS ARGN)
        if(EXISTS "${_dep}")
            list(APPEND _mesa_d3d12_source_deps "${_dep}")
        endif()
    endforeach()
    set(_mesa_d3d12_source_deps "${_mesa_d3d12_source_deps}" PARENT_SCOPE)
endfunction()

file(GLOB_RECURSE _mesa_d3d12_source_deps CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/src/gallium/drivers/d3d12/*
    ${CMAKE_CURRENT_SOURCE_DIR}/src/src/gallium/drivers/softpipe/*
    ${CMAKE_CURRENT_SOURCE_DIR}/src/src/gallium/drivers/virgl/*
    ${CMAKE_CURRENT_SOURCE_DIR}/src/src/gallium/frontends/dri/*
    ${CMAKE_CURRENT_SOURCE_DIR}/src/src/gallium/targets/dri/*
    ${CMAKE_CURRENT_SOURCE_DIR}/src/src/gallium/winsys/d3d12/*
    ${CMAKE_CURRENT_SOURCE_DIR}/src/src/gallium/winsys/sw/dri/*
    ${CMAKE_CURRENT_SOURCE_DIR}/src/src/gallium/winsys/sw/kms-dri/*
    ${CMAKE_CURRENT_SOURCE_DIR}/src/src/gallium/winsys/virgl/*
    ${CMAKE_CURRENT_SOURCE_DIR}/src/src/gallium/auxiliary/target-helpers/*sw_helper.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/src/egl/drivers/dri2/*
    ${CMAKE_CURRENT_SOURCE_DIR}/src/src/egl/main/*
    ${CMAKE_CURRENT_SOURCE_DIR}/src/src/gallium/include/*
    ${CMAKE_CURRENT_SOURCE_DIR}/src/include/GL/internal/*
    ${CMAKE_CURRENT_SOURCE_DIR}/src/src/microsoft/*
    ${CMAKE_CURRENT_SOURCE_DIR}/src/subprojects/DirectX-Headers-1.0/*
)

_mesa_append_existing_source_deps(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/meson.build
    ${CMAKE_CURRENT_SOURCE_DIR}/src/src/meson.build
    ${CMAKE_CURRENT_SOURCE_DIR}/src/src/egl/meson.build
    ${CMAKE_CURRENT_SOURCE_DIR}/src/src/gallium/meson.build
    ${CMAKE_CURRENT_SOURCE_DIR}/src/src/gallium/auxiliary/meson.build
    ${CMAKE_CURRENT_SOURCE_DIR}/src/src/gallium/drivers/d3d12/meson.build
    ${CMAKE_CURRENT_SOURCE_DIR}/src/src/gallium/drivers/softpipe/meson.build
    ${CMAKE_CURRENT_SOURCE_DIR}/src/src/gallium/drivers/virgl/meson.build
    ${CMAKE_CURRENT_SOURCE_DIR}/src/src/gallium/frontends/dri/meson.build
    ${CMAKE_CURRENT_SOURCE_DIR}/src/src/gallium/targets/dri/meson.build
    ${CMAKE_CURRENT_SOURCE_DIR}/src/src/gallium/winsys/d3d12/wgl/meson.build
    ${CMAKE_CURRENT_SOURCE_DIR}/src/src/gallium/winsys/sw/dri/meson.build
    ${CMAKE_CURRENT_SOURCE_DIR}/src/src/gallium/winsys/sw/kms-dri/meson.build
    ${CMAKE_CURRENT_SOURCE_DIR}/src/src/gallium/winsys/virgl/common/meson.build
    ${CMAKE_CURRENT_SOURCE_DIR}/src/src/gallium/winsys/virgl/drm/meson.build
    ${CMAKE_CURRENT_SOURCE_DIR}/src/src/gallium/winsys/virgl/vtest/meson.build
    ${CMAKE_CURRENT_SOURCE_DIR}/src/src/gallium/winsys/virgl/xv6/meson.build
    ${CMAKE_CURRENT_SOURCE_DIR}/src/src/microsoft/meson.build
    ${CMAKE_CURRENT_SOURCE_DIR}/src/subprojects/DirectX-Headers-1.0/meson.build
    ${CMAKE_CURRENT_SOURCE_DIR}/src/subprojects/DirectX-Headers-1.0/meson_options.txt
)

if(XV6_MESA_ENABLE_NOUVEAU)
    if(NOT EXISTS "${XV6_MESA_NOUVEAU_VALIDATION_LOG}")
        message(FATAL_ERROR
            "XV6_MESA_ENABLE_NOUVEAU requires validator evidence: "
            "${XV6_MESA_NOUVEAU_VALIDATION_LOG}")
    endif()
    file(READ "${XV6_MESA_NOUVEAU_VALIDATION_LOG}" _mesa_nouveau_validation)
    if(NOT _mesa_nouveau_validation MATCHES "hyperv-gpu-core-validate: passed")
        message(FATAL_ERROR
            "Mesa Nouveau is gated until hyperv-gpu-core-validate passes; "
            "missing pass marker in ${XV6_MESA_NOUVEAU_VALIDATION_LOG}")
    endif()
    if(NOT _mesa_nouveau_validation MATCHES "drmiftest: ok" OR
       NOT _mesa_nouveau_validation MATCHES "ttmtest: ok" OR
       NOT _mesa_nouveau_validation MATCHES "nouveauabitest: .*ok")
        message(FATAL_ERROR
            "Mesa Nouveau is gated until DRM/KMS, TTM, and Nouveau ABI "
            "validators pass in ${XV6_MESA_NOUVEAU_VALIDATION_LOG}")
    endif()
    set(_mesa_gallium_drivers "${_mesa_gallium_drivers},nouveau")
    list(APPEND _mesa_output_files lib/dri/nouveau_dri.so)
    file(GLOB_RECURSE _mesa_nouveau_source_deps CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/src/gallium/drivers/nouveau/*
        ${CMAKE_CURRENT_SOURCE_DIR}/src/src/gallium/winsys/nouveau/*
    )
    list(APPEND _mesa_d3d12_source_deps ${_mesa_nouveau_source_deps})
    _mesa_append_existing_source_deps(
        ${CMAKE_CURRENT_SOURCE_DIR}/src/src/gallium/drivers/nouveau/meson.build
        ${CMAKE_CURRENT_SOURCE_DIR}/src/src/gallium/winsys/nouveau/drm/meson.build
    )
    message(STATUS
        "ports/mesa: enabling Nouveau Gallium after validator log "
        "${XV6_MESA_NOUVEAU_VALIDATION_LOG}")
else()
    message(STATUS
        "ports/mesa: Mesa Nouveau disabled; set XV6_MESA_ENABLE_NOUVEAU=ON "
        "only after hyperv-gpu-core-validate passes")
endif()

xv6_port(
    NAME            mesa
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    meson
    SOURCE_DEPENDS  ${_mesa_d3d12_source_deps}
    C_ARGS          -DDETECT_OS_XV6=1
    CPP_ARGS        -DDETECT_OS_XV6=1
    DEPENDS         khronos-headers
                    libdrm
                    libexpat
                    wayland-libs
                    wayland-protocols
                    zlib
    OUTPUT_FILES    ${_mesa_output_files}
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
        -Dgallium-drivers=${_mesa_gallium_drivers}
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
file(GLOB _mesa_wsl_nv_d3d12_umd_candidate_dirs LIST_DIRECTORIES true
     "/usr/lib/wsl/drivers/nv*.inf_amd64_*")
foreach(_mesa_wsl_nv_d3d12_umd_candidate_dir IN LISTS
        _mesa_wsl_nv_d3d12_umd_candidate_dirs)
    if(EXISTS ${_mesa_wsl_nv_d3d12_umd_candidate_dir}/libnvwgf2umx.so)
        list(APPEND _mesa_wsl_nv_d3d12_umd_dirs
             ${_mesa_wsl_nv_d3d12_umd_candidate_dir})
    endif()
endforeach()
list(SORT _mesa_wsl_nv_d3d12_umd_dirs)
if(_mesa_wsl_nv_d3d12_umd_dirs)
    list(GET _mesa_wsl_nv_d3d12_umd_dirs -1
         _mesa_wsl_nv_d3d12_umd_driverstore_dir)
    get_filename_component(_mesa_wsl_nv_d3d12_umd_driverstore_name
        ${_mesa_wsl_nv_d3d12_umd_driverstore_dir} NAME)
endif()
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
set(_mesa_nv_d3d12_umd_driverstore_installed
    ${XV6_SYSROOT}/usr/lib/wsl/drivers/${_mesa_wsl_nv_d3d12_umd_driverstore_name}/libnvwgf2umx.so)
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

if(_mesa_wsl_nv_d3d12_umd_driverstore_dir)
    add_custom_command(
        OUTPUT  ${_mesa_nv_d3d12_umd_driverstore_installed}
        COMMAND ${CMAKE_COMMAND} -E make_directory
                ${XV6_SYSROOT}/usr/lib/wsl/drivers/${_mesa_wsl_nv_d3d12_umd_driverstore_name}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_mesa_wsl_nv_d3d12_umd_driverstore_dir}/libnvwgf2umx.so
                ${_mesa_nv_d3d12_umd_driverstore_installed}
        DEPENDS port-mesa
        COMMENT "Staging NVIDIA WSL D3D12 user-mode driver-store path"
        VERBATIM)
    add_custom_target(port-mesa-wsl-nvidia-driverstore ALL
        DEPENDS ${_mesa_nv_d3d12_umd_driverstore_installed})
    add_dependencies(port-mesa-wsl-nvidia-driverstore port-mesa)
else()
    message(STATUS
        "ports/mesa: NVIDIA WSL D3D12 UMD not found under "
        "/usr/lib/wsl/drivers/nv*.inf_amd64_*; NVIDIA Hyper-V D3D12 "
        "will need the driver-store libnvwgf2umx.so staged separately")
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
