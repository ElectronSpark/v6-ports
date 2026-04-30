#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gbm.h>
#include <libdrm/drm_fourcc.h>
#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"
#include "linux-dmabuf-v1-client-protocol.h"

#ifndef DRM_FORMAT_MOD_LINEAR
#define DRM_FORMAT_MOD_LINEAR 0
#endif
#ifndef GBM_BO_TRANSFER_WRITE
#define GBM_BO_TRANSFER_WRITE GBM_BO_USE_WRITE
#endif

struct app {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct xdg_wm_base *wm_base;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct zwp_linux_dmabuf_v1 *dmabuf;
    struct wl_buffer *buffer;
    int configured;
    int running;
};

static void fill_pattern(uint32_t *pixels, uint32_t width, uint32_t height,
                         uint32_t stride)
{
    uint32_t stride_px = stride / 4;

    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint8_t r = (uint8_t)(40 + (x * 180) / width);
            uint8_t g = (uint8_t)(40 + (y * 180) / height);
            uint8_t b = ((x / 16 + y / 16) & 1) ? 0xd8 : 0x40;
            pixels[y * stride_px + x] =
                0xff000000u | ((uint32_t)r << 16) |
                ((uint32_t)g << 8) | b;
        }
    }
}

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *wm_base,
                             uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static void xdg_surface_configure(void *data, struct xdg_surface *surface,
                                  uint32_t serial)
{
    struct app *app = data;

    xdg_surface_ack_configure(surface, serial);
    app->configured = 1;
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                                   int32_t width, int32_t height,
                                   struct wl_array *states)
{
    (void)data;
    (void)toplevel;
    (void)width;
    (void)height;
    (void)states;
}

static void xdg_toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
    struct app *app = data;

    (void)toplevel;
    app->running = 0;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close = xdg_toplevel_close,
};

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface,
                            uint32_t version)
{
    struct app *app = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        app->compositor = wl_registry_bind(
            registry, name, &wl_compositor_interface, version < 4 ? version : 4);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        app->wm_base = wl_registry_bind(
            registry, name, &xdg_wm_base_interface, version < 2 ? version : 2);
        xdg_wm_base_add_listener(app->wm_base, &wm_base_listener, app);
    } else if (strcmp(interface, zwp_linux_dmabuf_v1_interface.name) == 0) {
        app->dmabuf = wl_registry_bind(
            registry, name, &zwp_linux_dmabuf_v1_interface,
            version < 3 ? version : 3);
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry,
                                   uint32_t name)
{
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static int create_dmabuf_buffer(struct app *app)
{
    struct gbm_device *gbm = NULL;
    struct gbm_bo *bo = NULL;
    struct zwp_linux_buffer_params_v1 *params = NULL;
    void *map = NULL;
    int gpu_fd = -1;
    int bo_fd = -1;
    uint32_t stride;
    uint32_t width = 240;
    uint32_t height = 160;

    gpu_fd = open("/dev/gpu0", O_RDWR);
    if (gpu_fd < 0)
        gpu_fd = open("/dev/fb0", O_RDWR);
    if (gpu_fd < 0) {
        perror("dmabufsmoke: open gpu");
        return -1;
    }
    gbm = gbm_create_device(gpu_fd);
    if (!gbm) {
        perror("dmabufsmoke: gbm_create_device");
        close(gpu_fd);
        return -1;
    }
    bo = gbm_bo_create(gbm, width, height, GBM_FORMAT_XRGB8888,
                       GBM_BO_USE_RENDERING | GBM_BO_USE_WRITE |
                       GBM_BO_USE_LINEAR);
    if (!bo) {
        perror("dmabufsmoke: gbm_bo_create");
        goto fail;
    }

    stride = gbm_bo_get_stride(bo);
    map = gbm_bo_map(bo, 0, 0, width, height, GBM_BO_TRANSFER_WRITE,
                     &stride, NULL);
    if (!map) {
        perror("dmabufsmoke: gbm_bo_map");
        goto fail;
    }
    fill_pattern(map, width, height, stride);
    gbm_bo_unmap(bo, map);
    map = NULL;

    bo_fd = gbm_bo_get_fd(bo);
    if (bo_fd < 0) {
        perror("dmabufsmoke: gbm_bo_get_fd");
        goto fail;
    }

    params = zwp_linux_dmabuf_v1_create_params(app->dmabuf);
    if (!params) {
        fprintf(stderr, "dmabufsmoke: create_params failed\n");
        goto fail;
    }
    zwp_linux_buffer_params_v1_add(params, bo_fd, 0, 0, stride, 0,
                                  DRM_FORMAT_MOD_LINEAR);
    bo_fd = -1;
    app->buffer = zwp_linux_buffer_params_v1_create_immed(
        params, width, height, GBM_FORMAT_XRGB8888, 0);
    zwp_linux_buffer_params_v1_destroy(params);
    params = NULL;
    if (!app->buffer) {
        fprintf(stderr, "dmabufsmoke: create_immed returned no buffer\n");
        goto fail;
    }

    gbm_bo_destroy(bo);
    gbm_device_destroy(gbm);
    close(gpu_fd);
    return 0;

fail:
    if (bo_fd >= 0)
        close(bo_fd);
    if (params)
        zwp_linux_buffer_params_v1_destroy(params);
    if (bo)
        gbm_bo_destroy(bo);
    if (gbm)
        gbm_device_destroy(gbm);
    if (gpu_fd >= 0)
        close(gpu_fd);
    return -1;
}

int main(void)
{
    struct app app;
    int frames = 0;

    memset(&app, 0, sizeof(app));
    app.running = 1;
    app.display = wl_display_connect(NULL);
    if (!app.display) {
        fprintf(stderr, "dmabufsmoke: wl_display_connect failed\n");
        return 1;
    }
    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, &app);
    wl_display_roundtrip(app.display);

    if (!app.compositor || !app.wm_base || !app.dmabuf) {
        fprintf(stderr, "dmabufsmoke: missing compositor/xdg/dmabuf global\n");
        return 1;
    }
    app.surface = wl_compositor_create_surface(app.compositor);
    app.xdg_surface = xdg_wm_base_get_xdg_surface(app.wm_base, app.surface);
    xdg_surface_add_listener(app.xdg_surface, &xdg_surface_listener, &app);
    app.toplevel = xdg_surface_get_toplevel(app.xdg_surface);
    xdg_toplevel_add_listener(app.toplevel, &xdg_toplevel_listener, &app);
    xdg_toplevel_set_title(app.toplevel, "linux-dmabuf smoke");
    xdg_toplevel_set_app_id(app.toplevel, "dmabufsmoke");
    wl_surface_commit(app.surface);

    while (!app.configured && wl_display_dispatch(app.display) >= 0)
        ;
    if (!app.configured || create_dmabuf_buffer(&app) != 0)
        return 1;

    wl_surface_attach(app.surface, app.buffer, 0, 0);
    wl_surface_damage(app.surface, 0, 0, 240, 160);
    wl_surface_commit(app.surface);
    wl_display_roundtrip(app.display);

    while (app.running && frames++ < 20)
        wl_display_dispatch_pending(app.display);

    printf("dmabufsmoke: presented linux-dmabuf buffer\n");
    wl_buffer_destroy(app.buffer);
    xdg_toplevel_destroy(app.toplevel);
    xdg_surface_destroy(app.xdg_surface);
    wl_surface_destroy(app.surface);
    zwp_linux_dmabuf_v1_destroy(app.dmabuf);
    xdg_wm_base_destroy(app.wm_base);
    wl_compositor_destroy(app.compositor);
    wl_registry_destroy(app.registry);
    wl_display_disconnect(app.display);
    return 0;
}
