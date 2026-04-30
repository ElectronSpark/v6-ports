#include "gbm.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define FB_GPU_BO_CREATE       0x4614
#define FB_GPU_BO_DESTROY      0x4616
#define FB_GPU_BO_EXPORT_FD    0x4622
#define FB_GPU_BO_IMPORT_FD    0x4623
#define FB_GPU_BO_F_EXPORTABLE 0x1

struct fb_gpu_bo_create {
    uint32_t width, height, flags, pitch;
    uint64_t size, addr;
    uint32_t handle, reserved;
};

struct fb_gpu_bo_destroy {
    uint32_t handle, flags;
};

struct fb_gpu_bo_export_fd {
    uint32_t handle, flags;
    int32_t fd;
    uint32_t reserved;
};

struct fb_gpu_bo_import_fd {
    int32_t fd;
    uint32_t flags, width, height, pitch, handle;
    uint64_t size, addr;
};

struct gbm_device {
    int fd;
};

struct gbm_bo {
    struct gbm_device *dev;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    uint32_t handle;
    uint64_t size;
    void *addr;
    int imported_fd;
};

static int gbm_format_ok(uint32_t format)
{
    return format == GBM_FORMAT_XRGB8888 || format == GBM_FORMAT_ARGB8888;
}

struct gbm_device *gbm_create_device(int fd)
{
    struct gbm_device *gbm;
    int owned_fd;

    if (fd < 0) {
        errno = EBADF;
        return NULL;
    }

    owned_fd = dup(fd);
    if (owned_fd < 0)
        return NULL;

    gbm = calloc(1, sizeof(*gbm));
    if (!gbm) {
        close(owned_fd);
        return NULL;
    }
    gbm->fd = owned_fd;
    return gbm;
}

void gbm_device_destroy(struct gbm_device *gbm)
{
    if (!gbm)
        return;
    if (gbm->fd >= 0)
        close(gbm->fd);
    free(gbm);
}

int gbm_device_get_fd(struct gbm_device *gbm)
{
    return gbm ? gbm->fd : -1;
}

const char *gbm_device_get_backend_name(struct gbm_device *gbm)
{
    (void)gbm;
    return "xv6-gbm";
}

int gbm_device_is_format_supported(struct gbm_device *gbm, uint32_t format,
                                   uint32_t usage)
{
    (void)gbm;

    if (!gbm_format_ok(format))
        return 0;
    if (usage & ~(GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING |
                  GBM_BO_USE_WRITE | GBM_BO_USE_LINEAR))
        return 0;
    return 1;
}

static struct gbm_bo *gbm_bo_alloc_shell(struct gbm_device *gbm)
{
    struct gbm_bo *bo;

    if (!gbm || gbm->fd < 0) {
        errno = EINVAL;
        return NULL;
    }

    bo = calloc(1, sizeof(*bo));
    if (!bo)
        return NULL;
    bo->dev = gbm;
    bo->imported_fd = -1;
    return bo;
}

struct gbm_bo *gbm_bo_create(struct gbm_device *gbm, uint32_t width,
                             uint32_t height, uint32_t format,
                             uint32_t usage)
{
    struct fb_gpu_bo_create create;
    struct gbm_bo *bo;

    if (width == 0 || height == 0 ||
        !gbm_device_is_format_supported(gbm, format, usage)) {
        errno = EINVAL;
        return NULL;
    }

    bo = gbm_bo_alloc_shell(gbm);
    if (!bo)
        return NULL;

    memset(&create, 0, sizeof(create));
    create.width = width;
    create.height = height;
    create.flags = FB_GPU_BO_F_EXPORTABLE;
    if (ioctl(gbm->fd, FB_GPU_BO_CREATE, &create) < 0 ||
        create.addr == 0 || create.size == 0 || create.handle == 0) {
        free(bo);
        return NULL;
    }

    bo->width = width;
    bo->height = height;
    bo->stride = create.pitch;
    bo->format = format;
    bo->handle = create.handle;
    bo->size = create.size;
    bo->addr = (void *)(uintptr_t)create.addr;
    return bo;
}

struct gbm_bo *gbm_bo_import(struct gbm_device *gbm, uint32_t type,
                             void *buffer, uint32_t usage)
{
    struct gbm_import_fd_data *fd_data = buffer;
    struct fb_gpu_bo_import_fd import;
    struct gbm_bo *bo;

    if (type != GBM_BO_IMPORT_FD || !fd_data ||
        fd_data->width == 0 || fd_data->height == 0 ||
        !gbm_device_is_format_supported(gbm, fd_data->format, usage)) {
        errno = EINVAL;
        return NULL;
    }

    bo = gbm_bo_alloc_shell(gbm);
    if (!bo)
        return NULL;

    memset(&import, 0, sizeof(import));
    import.fd = fd_data->fd;
    if (ioctl(gbm->fd, FB_GPU_BO_IMPORT_FD, &import) < 0 ||
        import.addr == 0 || import.size == 0 || import.handle == 0) {
        free(bo);
        return NULL;
    }
    if (import.width != fd_data->width ||
        import.height != fd_data->height ||
        import.pitch != fd_data->stride) {
        munmap((void *)(uintptr_t)import.addr, (size_t)import.size);
        errno = EINVAL;
        free(bo);
        return NULL;
    }

    bo->width = import.width;
    bo->height = import.height;
    bo->stride = import.pitch;
    bo->format = fd_data->format;
    bo->handle = import.handle;
    bo->size = import.size;
    bo->addr = (void *)(uintptr_t)import.addr;
    bo->imported_fd = fd_data->fd;
    return bo;
}

void gbm_bo_destroy(struct gbm_bo *bo)
{
    if (!bo)
        return;
    if (bo->handle != 0) {
        struct fb_gpu_bo_destroy destroy;

        memset(&destroy, 0, sizeof(destroy));
        destroy.handle = bo->handle;
        (void)ioctl(bo->dev->fd, FB_GPU_BO_DESTROY, &destroy);
    }
    if (bo->addr && bo->size)
        munmap(bo->addr, (size_t)bo->size);
    free(bo);
}

struct gbm_device *gbm_bo_get_device(struct gbm_bo *bo)
{
    return bo ? bo->dev : NULL;
}

uint32_t gbm_bo_get_width(struct gbm_bo *bo)
{
    return bo ? bo->width : 0;
}

uint32_t gbm_bo_get_height(struct gbm_bo *bo)
{
    return bo ? bo->height : 0;
}

uint32_t gbm_bo_get_stride(struct gbm_bo *bo)
{
    return bo ? bo->stride : 0;
}

uint32_t gbm_bo_get_format(struct gbm_bo *bo)
{
    return bo ? bo->format : 0;
}

union gbm_bo_handle gbm_bo_get_handle(struct gbm_bo *bo)
{
    union gbm_bo_handle handle;

    memset(&handle, 0, sizeof(handle));
    if (bo)
        handle.u32 = bo->handle;
    return handle;
}

int gbm_bo_get_fd(struct gbm_bo *bo)
{
    struct fb_gpu_bo_export_fd export_fd;

    if (!bo || !bo->dev || bo->handle == 0) {
        errno = EINVAL;
        return -1;
    }

    memset(&export_fd, 0, sizeof(export_fd));
    export_fd.handle = bo->handle;
    export_fd.fd = -1;
    if (ioctl(bo->dev->fd, FB_GPU_BO_EXPORT_FD, &export_fd) < 0)
        return -1;
    return export_fd.fd;
}

int gbm_bo_write(struct gbm_bo *bo, const void *buf, size_t count)
{
    if (!bo || !bo->addr || !buf || count > bo->size) {
        errno = EINVAL;
        return -1;
    }
    memcpy(bo->addr, buf, count);
    return 0;
}

void *gbm_bo_map(struct gbm_bo *bo, uint32_t x, uint32_t y, uint32_t width,
                 uint32_t height, uint32_t flags, uint32_t *stride,
                 void **map_data)
{
    uint64_t offset;

    (void)flags;
    if (!bo || !bo->addr || x >= bo->width || y >= bo->height ||
        width == 0 || height == 0 || x + width > bo->width ||
        y + height > bo->height) {
        errno = EINVAL;
        return NULL;
    }

    offset = (uint64_t)y * bo->stride + (uint64_t)x * 4;
    if (offset >= bo->size) {
        errno = EINVAL;
        return NULL;
    }
    if (stride)
        *stride = bo->stride;
    if (map_data)
        *map_data = bo;
    return (char *)bo->addr + offset;
}

void gbm_bo_unmap(struct gbm_bo *bo, void *map_data)
{
    (void)bo;
    (void)map_data;
}
