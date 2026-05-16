#ifndef XV6_PRESENT_BUFFER_H
#define XV6_PRESENT_BUFFER_H

#include <stddef.h>
#include <stdint.h>
#include <wayland-client.h>

extern const struct wl_interface xv6_gpu_buffer_manager_interface;

struct xv6_present_buffer {
    struct wl_buffer *wl_buffer;
    uint32_t *pixels;
    size_t size;
    int stride;
    int width;
    int height;
    int fd;
    int fb_fd;
    uint32_t bo_handle;
    int bo_backed;
};

int xv6_present_buffer_init(struct xv6_present_buffer *buf, int width,
                            int height, struct wl_shm *shm,
                            struct wl_proxy *gpu_manager);
void xv6_present_buffer_destroy(struct xv6_present_buffer *buf);

#endif
