cmake_minimum_required(VERSION 3.16)
project(xv6-port-libdrm LANGUAGES NONE)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AddPort.cmake)

# Mesa's software DRI/EGL path expects the libdrm userspace ABI library and
# headers.  Nouveau is enabled now that xv6 exposes a fail-closed Nouveau DRM
# UAPI skeleton for native NVIDIA PCI devices.
xv6_port(
    NAME            libdrm
    SOURCE_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src
    BUILD_SYSTEM    meson
    MESON_DEFAULT_LIBRARY both
    OUTPUT_FILES    lib/libdrm.a
                    lib/libdrm.so
                    lib/libdrm.so.2
                    lib/libdrm.so.2.133.0
                    lib/libdrm_nouveau.a
                    lib/libdrm_nouveau.so
                    lib/libdrm_nouveau.so.2
                    lib/libdrm_nouveau.so.2.133.0
                    lib/pkgconfig/libdrm.pc
                    lib/pkgconfig/libdrm_nouveau.pc
                    include/xf86drm.h
                    include/libdrm/drm.h
                    include/libdrm/nouveau/nouveau.h
    CMAKE_ARGS
        -Dintel=disabled
        -Dradeon=disabled
        -Damdgpu=disabled
        -Dnouveau=enabled
        -Dvmwgfx=disabled
        -Domap=disabled
        -Dexynos=disabled
        -Dfreedreno=disabled
        -Dtegra=disabled
        -Dvc4=disabled
        -Detnaviv=disabled
        -Dcairo-tests=disabled
        -Dman-pages=disabled
        -Dvalgrind=disabled
        -Dfreedreno-kgsl=false
        -Dinstall-test-programs=true
        -Dudev=false
        -Dtests=true
)
