#include "gbm.h"

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

int main(void)
{
    int fd = open("/dev/gpu0", O_RDWR);
    struct gbm_device *dev;
    struct gbm_bo *bo;
    struct gbm_bo *imported;
    struct gbm_import_fd_data import_data;
    uint32_t stride = 0;
    void *map_data = NULL;
    uint32_t *pixels;
    int prime_fd;
    int ok = 1;

    if (fd < 0)
        fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) {
        printf("gbmtest: open gpu device failed: %s\n", strerror(errno));
        return 1;
    }

    dev = gbm_create_device(fd);
    close(fd);
    if (!dev) {
        printf("gbmtest: gbm_create_device failed: %s\n", strerror(errno));
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
                        gbm_bo_get_height(bo), GBM_BO_USE_WRITE,
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

    memset(&import_data, 0, sizeof(import_data));
    import_data.fd = prime_fd;
    import_data.width = gbm_bo_get_width(bo);
    import_data.height = gbm_bo_get_height(bo);
    import_data.stride = gbm_bo_get_stride(bo);
    import_data.format = gbm_bo_get_format(bo);
    imported = gbm_bo_import(dev, GBM_BO_IMPORT_FD, &import_data,
                             GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR);
    if (!imported) {
        printf("gbmtest: gbm_bo_import failed: %s\n", strerror(errno));
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

out_bo:
    gbm_bo_destroy(bo);
    gbm_device_destroy(dev);
    if (!ok)
        return 1;
    printf("gbmtest: passed linear BO create/map/export/import/destroy\n");
    return 0;
}
