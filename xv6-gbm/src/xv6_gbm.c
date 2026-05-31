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
    uint32_t format, plane_count;
    uint64_t modifier;
    uint32_t offsets[4];
    uint32_t strides[4];
    uint64_t implicit_fence;
    uint64_t explicit_fence;
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
    uint64_t modifier;
    uint32_t plane_count;
    uint32_t strides[4];
    uint32_t offsets[4];
    uint32_t handle;
    uint64_t size;
    void *addr;
    int imported_fd;
    void *user_data;
    void (*destroy_user_data)(struct gbm_bo *, void *);
};

struct gbm_surface {
    struct gbm_device *dev;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t flags;
    struct gbm_bo *front;
    int locked;
};

static int gbm_format_ok(uint32_t format)
{
    return format == GBM_FORMAT_XRGB8888 || format == GBM_FORMAT_ARGB8888 ||
           format == GBM_FORMAT_NV12;
}

static int gbm_modifier_ok(const uint64_t *modifiers, uint32_t count)
{
    uint32_t i;

    if (!modifiers || count == 0)
        return 1;

    for (i = 0; i < count; i++) {
        if (modifiers[i] == GBM_FORMAT_MOD_LINEAR ||
            modifiers[i] == GBM_FORMAT_MOD_INVALID)
            return 1;
    }
    return 0;
}

static int gbm_format_plane_count(uint32_t format)
{
    if (format == GBM_FORMAT_NV12)
        return 2;
    if (format == GBM_FORMAT_XRGB8888 || format == GBM_FORMAT_ARGB8888)
        return 1;
    return 0;
}

static uint64_t gbm_format_min_size(uint32_t format, uint32_t width,
                                    uint32_t height, uint32_t strides[4],
                                    uint32_t offsets[4])
{
    if (format == GBM_FORMAT_NV12) {
        strides[0] = width;
        strides[1] = width;
        offsets[0] = 0;
        offsets[1] = width * height;
        return (uint64_t)offsets[1] + (uint64_t)strides[1] *
               ((height + 1) / 2);
    }

    strides[0] = width * 4;
    offsets[0] = 0;
    return (uint64_t)strides[0] * height;
}

static uint64_t gbm_planes_min_size(uint32_t format, uint32_t height,
                                    const uint32_t strides[4],
                                    const uint32_t offsets[4])
{
    if (format == GBM_FORMAT_NV12)
        return (uint64_t)offsets[1] + (uint64_t)strides[1] *
               ((height + 1) / 2);
    return (uint64_t)offsets[0] + (uint64_t)strides[0] * height;
}

static int gbm_planes_valid(uint32_t format, uint32_t width, uint32_t height,
                            uint32_t plane_count,
                            const uint32_t strides[4],
                            const uint32_t offsets[4])
{
    if (format == GBM_FORMAT_NV12) {
        if (plane_count != 2 || strides[0] < width || strides[1] < width)
            return 0;
        return offsets[0] == 0 && offsets[1] >= strides[0] * height;
    }
    if (plane_count != 1 || strides[0] < width * 4)
        return 0;
    return offsets[0] == 0;
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
    if ((usage & GBM_BO_USE_SCANOUT) && format == GBM_FORMAT_NV12)
        return 0;
    return 1;
}

int gbm_device_get_format_modifier_plane_count(struct gbm_device *gbm,
                                               uint32_t format,
                                               uint64_t modifier)
{
    (void)gbm;
    if (!gbm_modifier_ok(&modifier, 1))
        return 0;
    return gbm_format_plane_count(format);
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
    bo->format = format;
    bo->modifier = GBM_FORMAT_MOD_LINEAR;
    bo->plane_count = (uint32_t)gbm_format_plane_count(format);
    (void)gbm_format_min_size(format, width, height, bo->strides,
                              bo->offsets);
    if (format == GBM_FORMAT_XRGB8888 || format == GBM_FORMAT_ARGB8888)
        bo->strides[0] = create.pitch;
    bo->stride = bo->strides[0];
    bo->handle = create.handle;
    bo->size = create.size;
    bo->addr = (void *)(uintptr_t)create.addr;
    return bo;
}

struct gbm_bo *gbm_bo_create_with_modifiers(struct gbm_device *gbm,
                                            uint32_t width, uint32_t height,
                                            uint32_t format,
                                            const uint64_t *modifiers,
                                            uint32_t count)
{
    if (!gbm_modifier_ok(modifiers, count)) {
        errno = EINVAL;
        return NULL;
    }

    return gbm_bo_create(gbm, width, height, format,
                         GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR);
}

struct gbm_bo *gbm_bo_create_with_modifiers2(struct gbm_device *gbm,
                                             uint32_t width, uint32_t height,
                                             uint32_t format,
                                             const uint64_t *modifiers,
                                             uint32_t count, uint32_t flags)
{
    if (!gbm_modifier_ok(modifiers, count)) {
        errno = EINVAL;
        return NULL;
    }

    return gbm_bo_create(gbm, width, height, format,
                         flags | GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR);
}

struct gbm_bo *gbm_bo_import(struct gbm_device *gbm, uint32_t type,
                             void *buffer, uint32_t usage)
{
    struct gbm_import_fd_data *fd_data = buffer;
    struct gbm_import_fd_modifier_data *mod_data = buffer;
    struct fb_gpu_bo_import_fd import;
    struct gbm_bo *bo;
    int fd;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t strides[4] = { 0 };
    uint32_t offsets[4] = { 0 };
    uint32_t plane_count;
    uint64_t modifier = GBM_FORMAT_MOD_LINEAR;
    uint64_t min_size;

    if (!buffer) {
        errno = EINVAL;
        return NULL;
    }
    if (type == GBM_BO_IMPORT_FD) {
        fd = fd_data->fd;
        width = fd_data->width;
        height = fd_data->height;
        format = fd_data->format;
        strides[0] = fd_data->stride;
        offsets[0] = 0;
    } else if (type == GBM_BO_IMPORT_FD_MODIFIER) {
        if (mod_data->num_fds == 0 || mod_data->num_fds > 4 ||
            !gbm_modifier_ok(&mod_data->modifier, 1)) {
            errno = EINVAL;
            return NULL;
        }
        fd = mod_data->fds[0];
        width = mod_data->width;
        height = mod_data->height;
        format = mod_data->format;
        modifier = mod_data->modifier == GBM_FORMAT_MOD_INVALID ?
                       GBM_FORMAT_MOD_LINEAR :
                       mod_data->modifier;
        for (uint32_t i = 0; i < mod_data->num_fds; i++) {
            if (mod_data->fds[i] < 0 || mod_data->strides[i] < 0 ||
                mod_data->offsets[i] < 0) {
                errno = EINVAL;
                return NULL;
            }
            strides[i] = (uint32_t)mod_data->strides[i];
            offsets[i] = (uint32_t)mod_data->offsets[i];
        }
    } else {
        errno = EINVAL;
        return NULL;
    }
    if (width == 0 || height == 0 ||
        !gbm_device_is_format_supported(gbm, format, usage)) {
        errno = EINVAL;
        return NULL;
    }
    plane_count = (uint32_t)gbm_format_plane_count(format);
    if (plane_count == 0 ||
        (type == GBM_BO_IMPORT_FD_MODIFIER && mod_data->num_fds != plane_count)) {
        errno = EINVAL;
        return NULL;
    }
    if (type == GBM_BO_IMPORT_FD) {
        uint32_t expected_offsets[4] = { 0 };
        uint64_t expected =
            gbm_format_min_size(format, width, height, strides,
                                expected_offsets);
        (void)expected;
        offsets[0] = expected_offsets[0];
    }

    bo = gbm_bo_alloc_shell(gbm);
    if (!bo)
        return NULL;

    memset(&import, 0, sizeof(import));
    import.fd = fd;
    import.width = width;
    import.height = height;
    import.format = format;
    import.plane_count = plane_count;
    import.modifier = modifier;
    memcpy(import.offsets, offsets, sizeof(import.offsets));
    memcpy(import.strides, strides, sizeof(import.strides));
    if (ioctl(gbm->fd, FB_GPU_BO_IMPORT_FD, &import) < 0 ||
        import.addr == 0 || import.size == 0 || import.handle == 0) {
        free(bo);
        return NULL;
    }
    if (type == GBM_BO_IMPORT_FD) {
        (void)gbm_format_min_size(format, width, height, bo->strides,
                                  bo->offsets);
        bo->strides[0] = strides[0];
        bo->offsets[0] = offsets[0];
    } else {
        for (uint32_t i = 0; i < plane_count; i++) {
            bo->strides[i] = strides[i];
            bo->offsets[i] = offsets[i];
        }
    }
    if (!gbm_planes_valid(format, width, height, plane_count, bo->strides,
                          bo->offsets)) {
        munmap((void *)(uintptr_t)import.addr, (size_t)import.size);
        errno = EINVAL;
        free(bo);
        return NULL;
    }
    min_size = gbm_planes_min_size(format, height, bo->strides, bo->offsets);
    if (import.width != width || import.height != height ||
        import.size < min_size ||
        ((format == GBM_FORMAT_XRGB8888 || format == GBM_FORMAT_ARGB8888) &&
         import.pitch != strides[0])) {
        munmap((void *)(uintptr_t)import.addr, (size_t)import.size);
        errno = EINVAL;
        free(bo);
        return NULL;
    }

    bo->width = import.width;
    bo->height = import.height;
    bo->format = format;
    bo->modifier = modifier;
    bo->plane_count = plane_count;
    if (type == GBM_BO_IMPORT_FD && format != GBM_FORMAT_NV12)
        bo->strides[0] = import.pitch;
    bo->stride = bo->strides[0];
    bo->handle = import.handle;
    bo->size = import.size;
    bo->addr = (void *)(uintptr_t)import.addr;
    bo->imported_fd = fd;
    return bo;
}

void gbm_bo_destroy(struct gbm_bo *bo)
{
    if (!bo)
        return;
    if (bo->destroy_user_data)
        bo->destroy_user_data(bo, bo->user_data);
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

uint32_t gbm_bo_get_stride_for_plane(struct gbm_bo *bo, int plane)
{
    return bo && plane >= 0 && (uint32_t)plane < bo->plane_count ?
               bo->strides[plane] :
               0;
}

uint32_t gbm_bo_get_format(struct gbm_bo *bo)
{
    return bo ? bo->format : 0;
}

uint32_t gbm_bo_get_bpp(struct gbm_bo *bo)
{
    if (!bo)
        return 0;
    switch (bo->format) {
    case GBM_FORMAT_ARGB8888:
    case GBM_FORMAT_XRGB8888:
        return 32;
    case GBM_FORMAT_NV12:
        return 8;
    default:
        return 0;
    }
}

uint64_t gbm_bo_get_modifier(struct gbm_bo *bo)
{
    return bo ? bo->modifier : GBM_FORMAT_MOD_INVALID;
}

int gbm_bo_get_plane_count(struct gbm_bo *bo)
{
    return bo ? (int)bo->plane_count : 0;
}

union gbm_bo_handle gbm_bo_get_handle(struct gbm_bo *bo)
{
    union gbm_bo_handle handle;

    memset(&handle, 0, sizeof(handle));
    if (bo)
        handle.u32 = bo->handle;
    return handle;
}

union gbm_bo_handle gbm_bo_get_handle_for_plane(struct gbm_bo *bo, int plane)
{
    if (!bo || plane < 0 || (uint32_t)plane >= bo->plane_count) {
        union gbm_bo_handle handle;

        memset(&handle, 0, sizeof(handle));
        return handle;
    }
    return gbm_bo_get_handle(bo);
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

int gbm_bo_get_fd_for_plane(struct gbm_bo *bo, int plane)
{
    if (!bo || plane < 0 || (uint32_t)plane >= bo->plane_count) {
        errno = EINVAL;
        return -1;
    }
    return gbm_bo_get_fd(bo);
}

void gbm_bo_set_user_data(struct gbm_bo *bo, void *data,
                          void (*destroy_user_data)(struct gbm_bo *, void *))
{
    if (!bo)
        return;
    if (bo->destroy_user_data)
        bo->destroy_user_data(bo, bo->user_data);
    bo->user_data = data;
    bo->destroy_user_data = destroy_user_data;
}

void *gbm_bo_get_user_data(struct gbm_bo *bo)
{
    return bo ? bo->user_data : NULL;
}

uint32_t gbm_bo_get_offset(struct gbm_bo *bo, int plane)
{
    return bo && plane >= 0 && (uint32_t)plane < bo->plane_count ?
               bo->offsets[plane] :
               0;
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

    if (bo->format == GBM_FORMAT_NV12)
        offset = (uint64_t)y * bo->stride + (uint64_t)x;
    else
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

struct gbm_surface *gbm_surface_create(struct gbm_device *gbm,
                                       uint32_t width, uint32_t height,
                                       uint32_t format, uint32_t flags)
{
    struct gbm_surface *surface;

    if (width == 0 || height == 0 ||
        !gbm_device_is_format_supported(gbm, format, flags)) {
        errno = EINVAL;
        return NULL;
    }

    surface = calloc(1, sizeof(*surface));
    if (!surface)
        return NULL;
    surface->dev = gbm;
    surface->width = width;
    surface->height = height;
    surface->format = format;
    surface->flags = flags;
    return surface;
}

struct gbm_surface *gbm_surface_create_with_modifiers(
    struct gbm_device *gbm, uint32_t width, uint32_t height, uint32_t format,
    const uint64_t *modifiers, uint32_t count)
{
    if (!gbm_modifier_ok(modifiers, count)) {
        errno = EINVAL;
        return NULL;
    }
    return gbm_surface_create(gbm, width, height, format,
                              GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR);
}

struct gbm_surface *gbm_surface_create_with_modifiers2(
    struct gbm_device *gbm, uint32_t width, uint32_t height, uint32_t format,
    const uint64_t *modifiers, uint32_t count, uint32_t flags)
{
    if (!gbm_modifier_ok(modifiers, count)) {
        errno = EINVAL;
        return NULL;
    }
    return gbm_surface_create(gbm, width, height, format,
                              flags | GBM_BO_USE_RENDERING |
                                  GBM_BO_USE_LINEAR);
}

struct gbm_bo *gbm_surface_lock_front_buffer(struct gbm_surface *surface)
{
    if (!surface) {
        errno = EINVAL;
        return NULL;
    }

    if (!surface->front) {
        surface->front = gbm_bo_create(surface->dev, surface->width,
                                       surface->height, surface->format,
                                       surface->flags | GBM_BO_USE_RENDERING |
                                           GBM_BO_USE_LINEAR);
        if (!surface->front)
            return NULL;
    }

    surface->locked = 1;
    return surface->front;
}

void gbm_surface_release_buffer(struct gbm_surface *surface, struct gbm_bo *bo)
{
    if (!surface || (bo && bo != surface->front))
        return;
    surface->locked = 0;
}

int gbm_surface_has_free_buffers(struct gbm_surface *surface)
{
    return surface ? !surface->locked : 0;
}

void gbm_surface_destroy(struct gbm_surface *surface)
{
    if (!surface)
        return;
    gbm_bo_destroy(surface->front);
    free(surface);
}

char *gbm_format_get_name(uint32_t gbm_format,
                          struct gbm_format_name_desc *desc)
{
    if (!desc)
        return NULL;
    desc->name[0] = (char)(gbm_format & 0xff);
    desc->name[1] = (char)((gbm_format >> 8) & 0xff);
    desc->name[2] = (char)((gbm_format >> 16) & 0xff);
    desc->name[3] = (char)((gbm_format >> 24) & 0xff);
    desc->name[4] = '\0';
    return desc->name;
}
