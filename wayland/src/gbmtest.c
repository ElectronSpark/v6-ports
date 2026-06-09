#include <gbm.h>

#include <drm_fourcc.h>
#include <xf86drm.h>
#include <libdrm/virtgpu_drm.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

static void fill_pattern(uint32_t *pixels, uint32_t width, uint32_t height,
                         uint32_t stride)
{
    uint32_t stride_px = stride / 4;

    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t r = (x * 255) / (width ? width : 1);
            uint32_t g = (y * 255) / (height ? height : 1);
            uint32_t b = ((x ^ y) * 255) / ((width | height) ? (width | height) : 1);

            pixels[y * stride_px + x] = 0xff000000u | (r << 16) | (g << 8) | b;
        }
    }
}

static struct gbm_bo *try_import_xrgb(struct gbm_device *dev,
                                      struct gbm_bo *source, int prime_fd)
{
    struct gbm_import_fd_data fd_data;
    struct gbm_import_fd_modifier_data mod_data;
    struct {
        const char *name;
        uint32_t type;
        uint32_t usage;
    } attempts[] = {
        { "fd/usage=0", GBM_BO_IMPORT_FD, 0 },
        { "fd/linear", GBM_BO_IMPORT_FD, GBM_BO_USE_LINEAR },
        { "fd/render-linear", GBM_BO_IMPORT_FD,
          GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR },
        { "modifier/usage=0", GBM_BO_IMPORT_FD_MODIFIER, 0 },
        { "modifier/linear", GBM_BO_IMPORT_FD_MODIFIER, GBM_BO_USE_LINEAR },
        { "modifier/render-linear", GBM_BO_IMPORT_FD_MODIFIER,
          GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR },
    };

    memset(&fd_data, 0, sizeof(fd_data));
    fd_data.fd = prime_fd;
    fd_data.width = gbm_bo_get_width(source);
    fd_data.height = gbm_bo_get_height(source);
    fd_data.stride = (int)gbm_bo_get_stride(source);
    fd_data.format = gbm_bo_get_format(source);

    memset(&mod_data, 0, sizeof(mod_data));
    mod_data.width = fd_data.width;
    mod_data.height = fd_data.height;
    mod_data.format = fd_data.format;
    mod_data.num_fds = 1;
    mod_data.fds[0] = prime_fd;
    mod_data.strides[0] = fd_data.stride;
    mod_data.offsets[0] = 0;
    mod_data.modifier = DRM_FORMAT_MOD_LINEAR;

    for (size_t i = 0; i < sizeof(attempts) / sizeof(attempts[0]); i++) {
        struct gbm_bo *imported;
        void *data;

        errno = 0;
        data = attempts[i].type == GBM_BO_IMPORT_FD ? (void *)&fd_data
                                                    : (void *)&mod_data;
        imported = gbm_bo_import(dev, attempts[i].type, data, attempts[i].usage);
        if (imported) {
            printf("gbmtest: gbm_bo_import %s ok\n", attempts[i].name);
            return imported;
        }
        printf("gbmtest: gbm_bo_import %s failed: %s\n", attempts[i].name,
               strerror(errno));
    }

    return NULL;
}

static void probe_drm_prime_resource_info(struct gbm_device *dev,
                                          struct gbm_bo *source, int prime_fd)
{
    struct drm_virtgpu_resource_info info;
    struct drm_gem_close close_req;
    uint32_t imported = 0;
    int fd = gbm_device_get_fd(dev);
    int ret;

    errno = 0;
    ret = drmPrimeFDToHandle(fd, prime_fd, &imported);
    printf("gbmtest: drmPrimeFDToHandle ret=%d errno=%d handle=%u source=%u\n",
           ret, ret == 0 ? 0 : errno, imported, gbm_bo_get_handle(source).u32);
    if (ret != 0 || imported == 0)
        return;

    memset(&info, 0, sizeof(info));
    info.bo_handle = imported;
    errno = 0;
    ret = drmIoctl(fd, DRM_IOCTL_VIRTGPU_RESOURCE_INFO, &info);
    printf("gbmtest: DRM_IOCTL_VIRTGPU_RESOURCE_INFO imported ret=%d errno=%d "
           "res=%u size=%u blob_mem=%u\n",
           ret, ret == 0 ? 0 : errno, info.res_handle, info.size,
           info.blob_mem);
    if (ret == 0 && info.res_handle != 0 && info.size != 0)
        printf("__GBMTEST_PRIME_RESOURCE_INFO_0__ handle=%u res=%u size=%u\n",
               imported, info.res_handle, info.size);

    memset(&close_req, 0, sizeof(close_req));
    close_req.handle = imported;
    (void)drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &close_req);
}

int main(void)
{
    int fd = open("/dev/dri/card0", O_RDWR);
    struct gbm_device *dev;
    struct gbm_bo *bo;
    struct gbm_bo *imported;
    struct gbm_bo *nv12;
    struct gbm_bo *nv12_imported;
    struct gbm_import_fd_modifier_data mod_import;
    uint32_t stride = 0;
    void *map_data = NULL;
    uint32_t *pixels;
    uint64_t linear_mod = DRM_FORMAT_MOD_LINEAR;
    int prime_fd;
    int nv12_fd0 = -1;
    int nv12_fd1 = -1;
    int ok = 1;

    if (fd < 0)
        fd = open("/dev/dri/renderD128", O_RDWR);
    if (fd < 0) {
        printf("gbmtest: open gpu device failed: %s\n", strerror(errno));
        return 1;
    }

    dev = gbm_create_device(fd);
    if (!dev) {
        printf("gbmtest: gbm_create_device failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    printf("gbmtest: backend=%s fd=%d\n",
           gbm_device_get_backend_name(dev), gbm_device_get_fd(dev));

    if (!gbm_device_is_format_supported(dev, GBM_FORMAT_XRGB8888,
                                        GBM_BO_USE_RENDERING |
                                        GBM_BO_USE_LINEAR |
                                        GBM_BO_USE_WRITE)) {
        printf("gbmtest: XRGB8888 linear rendering unsupported\n");
        gbm_device_destroy(dev);
        return 1;
    }

    bo = gbm_bo_create(dev, 96, 64, GBM_FORMAT_XRGB8888,
                       GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR |
                       GBM_BO_USE_WRITE);
    if (!bo) {
        printf("gbmtest: gbm_bo_create failed: %s\n", strerror(errno));
        gbm_device_destroy(dev);
        return 1;
    }

    pixels = gbm_bo_map(bo, 0, 0, gbm_bo_get_width(bo),
                        gbm_bo_get_height(bo), GBM_BO_TRANSFER_WRITE,
                        &stride, &map_data);
    if (!pixels || stride < gbm_bo_get_width(bo) * 4) {
        printf("gbmtest: gbm_bo_map failed: %s\n", strerror(errno));
        ok = 0;
        goto out_bo;
    }
    fill_pattern(pixels, gbm_bo_get_width(bo), gbm_bo_get_height(bo), stride);
    gbm_bo_unmap(bo, map_data);

    prime_fd = gbm_bo_get_fd(bo);
    if (prime_fd < 0) {
        printf("gbmtest: gbm_bo_get_fd failed: %s\n", strerror(errno));
        ok = 0;
        goto out_bo;
    }
    probe_drm_prime_resource_info(dev, bo, prime_fd);

    imported = try_import_xrgb(dev, bo, prime_fd);
    if (!imported) {
        printf("gbmtest: gbm_bo_import failed for every XRGB8888 variant\n");
        close(prime_fd);
        ok = 0;
        goto out_bo;
    }

    if (gbm_bo_get_width(imported) != gbm_bo_get_width(bo) ||
        gbm_bo_get_height(imported) != gbm_bo_get_height(bo) ||
        gbm_bo_get_stride(imported) != gbm_bo_get_stride(bo) ||
        gbm_bo_get_format(imported) != gbm_bo_get_format(bo)) {
        printf("gbmtest: imported metadata mismatch\n");
        ok = 0;
    }

    gbm_bo_destroy(imported);
    close(prime_fd);

    if (gbm_device_get_format_modifier_plane_count(dev, GBM_FORMAT_NV12,
                                                   DRM_FORMAT_MOD_LINEAR) != 2) {
        printf("gbmtest: NV12 linear plane metadata unsupported by backend=%s\n",
               gbm_device_get_backend_name(dev));
        goto out_bo;
    }

    nv12 = gbm_bo_create_with_modifiers2(dev, 64, 32, GBM_FORMAT_NV12,
                                         &linear_mod, 1,
                                         GBM_BO_USE_RENDERING |
                                         GBM_BO_USE_WRITE);
    if (!nv12) {
        printf("gbmtest: NV12 modifier create failed: %s\n", strerror(errno));
        ok = 0;
        goto out_bo;
    }

    if (gbm_bo_get_modifier(nv12) != DRM_FORMAT_MOD_LINEAR ||
        gbm_bo_get_plane_count(nv12) != 2 ||
        gbm_bo_get_stride_for_plane(nv12, 0) < gbm_bo_get_width(nv12) ||
        gbm_bo_get_stride_for_plane(nv12, 1) < gbm_bo_get_width(nv12) ||
        gbm_bo_get_offset(nv12, 0) != 0 ||
        gbm_bo_get_offset(nv12, 1) <
            gbm_bo_get_stride_for_plane(nv12, 0) * gbm_bo_get_height(nv12) ||
        gbm_bo_get_handle_for_plane(nv12, 0).u32 == 0 ||
        gbm_bo_get_handle_for_plane(nv12, 1).u32 == 0) {
        printf("gbmtest: NV12 plane metadata mismatch\n");
        ok = 0;
        gbm_bo_destroy(nv12);
        goto out_bo;
    }

    nv12_fd0 = gbm_bo_get_fd_for_plane(nv12, 0);
    nv12_fd1 = gbm_bo_get_fd_for_plane(nv12, 1);
    if (nv12_fd0 < 0 || nv12_fd1 < 0) {
        printf("gbmtest: NV12 plane fd export failed: %s\n", strerror(errno));
        ok = 0;
        gbm_bo_destroy(nv12);
        goto out_bo;
    }

    memset(&mod_import, 0, sizeof(mod_import));
    mod_import.width = gbm_bo_get_width(nv12);
    mod_import.height = gbm_bo_get_height(nv12);
    mod_import.format = gbm_bo_get_format(nv12);
    mod_import.num_fds = 2;
    mod_import.fds[0] = nv12_fd0;
    mod_import.fds[1] = nv12_fd1;
    mod_import.strides[0] = (int)gbm_bo_get_stride_for_plane(nv12, 0);
    mod_import.strides[1] = (int)gbm_bo_get_stride_for_plane(nv12, 1);
    mod_import.offsets[0] = (int)gbm_bo_get_offset(nv12, 0);
    mod_import.offsets[1] = (int)gbm_bo_get_offset(nv12, 1);
    mod_import.modifier = gbm_bo_get_modifier(nv12);

    nv12_imported = gbm_bo_import(dev, GBM_BO_IMPORT_FD_MODIFIER, &mod_import,
                                  GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR);
    if (!nv12_imported) {
        printf("gbmtest: NV12 modifier import failed: %s\n", strerror(errno));
        ok = 0;
        gbm_bo_destroy(nv12);
        goto out_bo;
    }
    if (gbm_bo_get_plane_count(nv12_imported) != 2 ||
        gbm_bo_get_modifier(nv12_imported) != DRM_FORMAT_MOD_LINEAR ||
        gbm_bo_get_stride_for_plane(nv12_imported, 1) !=
            gbm_bo_get_stride_for_plane(nv12, 1) ||
        gbm_bo_get_offset(nv12_imported, 1) != gbm_bo_get_offset(nv12, 1)) {
        printf("gbmtest: imported NV12 modifier metadata mismatch\n");
        ok = 0;
    }
    gbm_bo_destroy(nv12_imported);
    gbm_bo_destroy(nv12);
    close(nv12_fd0);
    close(nv12_fd1);
    nv12_fd0 = -1;
    nv12_fd1 = -1;
    if (ok)
        printf("gbmtest: passed linear NV12 modifier plane metadata import\n");

out_bo:
    if (nv12_fd0 >= 0)
        close(nv12_fd0);
    if (nv12_fd1 >= 0)
        close(nv12_fd1);
    gbm_bo_destroy(bo);
    gbm_device_destroy(dev);
    close(fd);
    if (!ok)
        return 1;
    printf("__GBMTEST_BO_ROUNDTRIP_0__\n");
    printf("gbmtest: passed linear BO create/map/export/import/destroy\n");
    return 0;
}
