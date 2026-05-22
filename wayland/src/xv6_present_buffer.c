#include "xv6_present_buffer.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#define FB_GPU_BO_CREATE       0x4614
#define FB_GPU_BO_DESTROY      0x4616
#define FB_GPU_BO_F_EXPORTABLE 0x1

struct fb_gpu_bo_create {
    uint32_t width, height, flags, pitch;
    uint64_t size, addr;
    uint32_t handle, reserved;
};

struct fb_gpu_bo_destroy {
    uint32_t handle, flags;
};

static const struct wl_interface *xv6_gpu_buffer_create_types[] = {
    &wl_buffer_interface,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
};

static const struct wl_message xv6_gpu_buffer_manager_requests[] = {
    { "create_buffer", "nuiiiu", xv6_gpu_buffer_create_types },
    { "create_buffer_with_fence", "nuiiiuh", xv6_gpu_buffer_create_types },
    { "create_d3d12_resource_buffer", "nhiiuuu",
      xv6_gpu_buffer_create_types },
    { "create_d3d12_resource_buffer_with_fence", "nhhiiuuu",
      xv6_gpu_buffer_create_types },
    { "create_d3d12_resource_buffer_luid", "nhuuiiuuu",
      xv6_gpu_buffer_create_types },
    { "create_d3d12_resource_buffer_with_fence_luid", "nhhuuiiuuu",
      xv6_gpu_buffer_create_types },
    { "create_d3d12_resource_buffer_with_fence_value_luid",
      "nhhuuuuiiuuu", xv6_gpu_buffer_create_types },
};

const struct wl_interface xv6_gpu_buffer_manager_interface = {
    "xv6_gpu_buffer_manager",
    5,
    7,
    xv6_gpu_buffer_manager_requests,
    0,
    NULL,
};

static struct wl_buffer *xv6_gpu_buffer_manager_create_buffer(
    struct wl_proxy *manager, uint32_t handle, int32_t width, int32_t height,
    int32_t stride, uint32_t format)
{
    return (struct wl_buffer *)wl_proxy_marshal_flags(
        manager, 0, &wl_buffer_interface, wl_proxy_get_version(manager), 0,
        NULL, handle, width, height, stride, format);
}

static struct wl_buffer *xv6_gpu_buffer_manager_create_buffer_with_fence(
    struct wl_proxy *manager, uint32_t handle, int32_t width, int32_t height,
    int32_t stride, uint32_t format, int acquire_fence_fd)
{
    if (wl_proxy_get_version(manager) < 2 || acquire_fence_fd < 0) {
        if (acquire_fence_fd >= 0)
            close(acquire_fence_fd);
        return xv6_gpu_buffer_manager_create_buffer(
            manager, handle, width, height, stride, format);
    }

    return (struct wl_buffer *)wl_proxy_marshal_flags(
        manager, 1, &wl_buffer_interface, wl_proxy_get_version(manager), 0,
        NULL, handle, width, height, stride, format, acquire_fence_fd);
}

static int create_shm_file(size_t size)
{
    char path[64];
    int fd;

    snprintf(path, sizeof(path), "/tmp/mesaglsmoke-%ld", (long)getpid());
    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return -1;
    unlink(path);
    if (ftruncate(fd, (off_t)size) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int xv6_present_buffer_init(struct xv6_present_buffer *buf, int width,
                            int height, struct wl_shm *shm,
                            struct wl_proxy *gpu_manager)
{
    int stride = width * 4;

    memset(buf, 0, sizeof(*buf));
    buf->fd = -1;
    buf->fb_fd = -1;
    buf->width = width;
    buf->height = height;
    buf->stride = stride;
    buf->size = (size_t)stride * (size_t)height;

    if (gpu_manager) {
        struct fb_gpu_bo_create bo;

        memset(&bo, 0, sizeof(bo));
        bo.width = (uint32_t)buf->width;
        bo.height = (uint32_t)buf->height;
        bo.flags = FB_GPU_BO_F_EXPORTABLE;
        buf->fb_fd = open("/dev/fb0", O_RDWR);
        if (buf->fb_fd >= 0 &&
            ioctl(buf->fb_fd, FB_GPU_BO_CREATE, &bo) == 0 &&
            bo.addr != 0 && bo.size != 0 && bo.pitch >= (uint32_t)stride &&
            bo.handle != 0) {
            buf->pixels = (uint32_t *)bo.addr;
            buf->size = (size_t)bo.size;
            buf->stride = (int)bo.pitch;
            buf->bo_handle = bo.handle;
            buf->bo_backed = 1;
            buf->wl_buffer = xv6_gpu_buffer_manager_create_buffer_with_fence(
                gpu_manager, bo.handle, buf->width, buf->height,
                buf->stride, WL_SHM_FORMAT_XRGB8888, -1);
            if (buf->wl_buffer)
                return 0;
        }
        if (buf->bo_handle) {
            struct fb_gpu_bo_destroy destroy = { .handle = buf->bo_handle };
            ioctl(buf->fb_fd, FB_GPU_BO_DESTROY, &destroy);
        }
        if (buf->pixels && buf->bo_backed)
            munmap(buf->pixels, buf->size);
        if (buf->fb_fd >= 0)
            close(buf->fb_fd);
        memset(buf, 0, sizeof(*buf));
        buf->fd = -1;
        buf->fb_fd = -1;
        buf->width = width;
        buf->height = height;
        buf->stride = stride;
        buf->size = (size_t)stride * (size_t)height;
    }

    if (!shm)
        return -1;

    buf->fd = create_shm_file(buf->size);
    if (buf->fd < 0)
        return -1;
    buf->pixels = mmap(NULL, buf->size, PROT_READ | PROT_WRITE, MAP_SHARED,
                       buf->fd, 0);
    if (buf->pixels == MAP_FAILED) {
        buf->pixels = NULL;
        return -1;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, buf->fd,
                                                  (int)buf->size);
    buf->wl_buffer = wl_shm_pool_create_buffer(pool, 0, buf->width,
                                               buf->height, buf->stride,
                                               WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    return buf->wl_buffer ? 0 : -1;
}

void xv6_present_buffer_destroy(struct xv6_present_buffer *buf)
{
    if (buf->wl_buffer)
        wl_buffer_destroy(buf->wl_buffer);
    if (buf->pixels)
        munmap(buf->pixels, buf->size);
    if (buf->bo_handle && buf->fb_fd >= 0) {
        struct fb_gpu_bo_destroy destroy = { .handle = buf->bo_handle };
        ioctl(buf->fb_fd, FB_GPU_BO_DESTROY, &destroy);
    }
    if (buf->fb_fd >= 0)
        close(buf->fb_fd);
    if (buf->fd >= 0)
        close(buf->fd);

    memset(buf, 0, sizeof(*buf));
    buf->fd = -1;
    buf->fb_fd = -1;
}
