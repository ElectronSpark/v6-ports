#include <errno.h>
#include <fcntl.h>
#include <signal.h>
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
#include "linux-explicit-synchronization-unstable-v1-client-protocol.h"

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
    struct zwp_linux_explicit_synchronization_v1 *explicit_sync;
    struct zwp_linux_surface_synchronization_v1 *surface_sync;
    struct zwp_linux_buffer_release_v1 *release;
    struct wl_buffer *buffer;
    int configured;
    int running;
    int use_nv12;
    int use_explicit_sync;
    int release_seen;
    int release_fenced;
    int hold_ms;
    char gbm_path[64];
    char gbm_backend[32];
    int gbm_plane_count;
    uint64_t gbm_modifier;
};

static const char *g_phase = "startup";

static void timeout_handler(int sig)
{
    (void)sig;
    fprintf(stderr, "dmabufsmoke: timeout phase=%s\n", g_phase);
    _exit(2);
}

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

static void fill_nv12(uint8_t *base, uint32_t width, uint32_t height,
                      uint32_t y_stride, uint32_t uv_offset,
                      uint32_t uv_stride)
{
    uint8_t *y_plane = base;
    uint8_t *uv_plane = base + uv_offset;

    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++)
            y_plane[y * y_stride + x] =
                (uint8_t)(48 + (x * 160) / (width ? width : 1));
    }
    for (uint32_t y = 0; y < (height + 1) / 2; y++) {
        for (uint32_t x = 0; x + 1 < width; x += 2) {
            uv_plane[y * uv_stride + x] = (uint8_t)(96 + (y * 48) /
                                                    (height ? height : 1));
            uv_plane[y * uv_stride + x + 1] =
                (uint8_t)(144 + (x * 48) / (width ? width : 1));
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
    } else if (strcmp(interface,
                      zwp_linux_explicit_synchronization_v1_interface.name) == 0) {
        app->explicit_sync = wl_registry_bind(
            registry, name, &zwp_linux_explicit_synchronization_v1_interface,
            version < 2 ? version : 2);
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

static void release_fenced(void *data,
                           struct zwp_linux_buffer_release_v1 *release,
                           int32_t fence)
{
    struct app *app = data;

    (void)release;
    if (fence >= 0)
        close(fence);
    app->release_seen = 1;
    app->release_fenced = 1;
}

static void release_immediate(void *data,
                              struct zwp_linux_buffer_release_v1 *release)
{
    struct app *app = data;

    (void)release;
    app->release_seen = 1;
    app->release_fenced = 0;
}

static const struct zwp_linux_buffer_release_v1_listener release_listener = {
    .fenced_release = release_fenced,
    .immediate_release = release_immediate,
};

static int create_dmabuf_buffer(struct app *app)
{
    static const char *const gpu_paths[] = {
        "/dev/dri/renderD128",
        "/dev/gpu0",
        "/dev/fb0",
    };
    struct gbm_device *gbm = NULL;
    struct gbm_bo *bo = NULL;
    struct zwp_linux_buffer_params_v1 *params = NULL;
    void *map = NULL;
    void *map_data = NULL;
    int gpu_fd = -1;
    int bo_fd = -1;
    int bo_fd1 = -1;
    uint32_t stride;
    uint32_t width = 240;
    uint32_t height = 160;
    uint32_t format = app->use_nv12 ? GBM_FORMAT_NV12 : GBM_FORMAT_XRGB8888;
    uint64_t modifier = DRM_FORMAT_MOD_LINEAR;
    const char *selected_path = NULL;
    const char *backend = NULL;
    int create_errno = 0;

    for (size_t i = 0; i < sizeof(gpu_paths) / sizeof(gpu_paths[0]); i++) {
        gpu_fd = open(gpu_paths[i], O_RDWR | O_CLOEXEC);
        if (gpu_fd < 0)
            continue;
        gbm = gbm_create_device(gpu_fd);
        if (!gbm) {
            create_errno = errno;
            fprintf(stderr,
                    "dmabufsmoke: gbm_create_device path=%s failed: %s\n",
                    gpu_paths[i], strerror(create_errno));
            close(gpu_fd);
            gpu_fd = -1;
            continue;
        }
        backend = gbm_device_get_backend_name(gbm);
        if (app->use_nv12)
            bo = gbm_bo_create_with_modifiers2(gbm, width, height, format,
                                               &modifier, 1,
                                               GBM_BO_USE_RENDERING |
                                               GBM_BO_USE_WRITE);
        else
            bo = gbm_bo_create(gbm, width, height, format,
                               GBM_BO_USE_RENDERING | GBM_BO_USE_WRITE |
                               GBM_BO_USE_LINEAR);
        if (bo) {
            selected_path = gpu_paths[i];
            break;
        }
        create_errno = errno;
        fprintf(stderr,
                "dmabufsmoke: gbm_bo_create path=%s format=%s backend=%s failed: %s\n",
                gpu_paths[i], app->use_nv12 ? "NV12" : "XRGB8888",
                backend ? backend : "?", strerror(create_errno));
        gbm_device_destroy(gbm);
        gbm = NULL;
        backend = NULL;
        close(gpu_fd);
        gpu_fd = -1;
    }
    if (!bo) {
        errno = create_errno ? create_errno : ENODEV;
        perror("dmabufsmoke: gbm_bo_create");
        goto fail;
    }
    fprintf(stderr, "dmabufsmoke: using gbm path=%s format=%s backend=%s\n",
            selected_path ? selected_path : "?",
            app->use_nv12 ? "NV12" : "XRGB8888",
            backend ? backend : "?");
    snprintf(app->gbm_path, sizeof(app->gbm_path), "%s",
             selected_path ? selected_path : "?");
    snprintf(app->gbm_backend, sizeof(app->gbm_backend), "%s",
             backend ? backend : "?");
    app->gbm_plane_count = gbm_bo_get_plane_count(bo);
    app->gbm_modifier = gbm_bo_get_modifier(bo);
    if ((app->use_nv12 && gbm_bo_get_plane_count(bo) != 2) ||
        (!app->use_nv12 && gbm_bo_get_plane_count(bo) != 1)) {
        fprintf(stderr, "dmabufsmoke: unexpected GBM plane count\n");
        goto fail;
    }
    if (app->use_nv12) {
        fprintf(stderr,
                "dmabufsmoke: NV12 metadata planes=%d y_stride=%u uv_stride=%u uv_offset=%u modifier=0x%lx\n",
                gbm_bo_get_plane_count(bo),
                gbm_bo_get_stride_for_plane(bo, 0),
                gbm_bo_get_stride_for_plane(bo, 1),
                gbm_bo_get_offset(bo, 1),
                (unsigned long)gbm_bo_get_modifier(bo));
    }

    stride = gbm_bo_get_stride_for_plane(bo, 0);
    map = gbm_bo_map(bo, 0, 0, width, height, GBM_BO_TRANSFER_WRITE,
                     &stride, &map_data);
    if (!map) {
        perror("dmabufsmoke: gbm_bo_map");
        goto fail;
    }
    if (app->use_nv12)
        fill_nv12(map, width, height, stride, gbm_bo_get_offset(bo, 1),
                  gbm_bo_get_stride_for_plane(bo, 1));
    else
        fill_pattern(map, width, height, stride);
    gbm_bo_unmap(bo, map_data);
    map = NULL;
    map_data = NULL;

    bo_fd = app->use_nv12 ? gbm_bo_get_fd_for_plane(bo, 0) :
                            gbm_bo_get_fd(bo);
    if (bo_fd < 0) {
        perror("dmabufsmoke: gbm_bo_get_fd");
        goto fail;
    }
    if (app->use_nv12) {
        bo_fd1 = gbm_bo_get_fd_for_plane(bo, 1);
        if (bo_fd1 < 0) {
            perror("dmabufsmoke: gbm_bo_get_fd_for_plane");
            goto fail;
        }
    }

    params = zwp_linux_dmabuf_v1_create_params(app->dmabuf);
    if (!params) {
        fprintf(stderr, "dmabufsmoke: create_params failed\n");
        goto fail;
    }
    zwp_linux_buffer_params_v1_add(params, bo_fd, 0, 0, stride, 0,
                                  DRM_FORMAT_MOD_LINEAR);
    bo_fd = -1;
    if (app->use_nv12) {
        zwp_linux_buffer_params_v1_add(params, bo_fd1, 1,
                                      gbm_bo_get_offset(bo, 1),
                                      gbm_bo_get_stride_for_plane(bo, 1), 0,
                                      DRM_FORMAT_MOD_LINEAR);
        bo_fd1 = -1;
    }
    app->buffer = zwp_linux_buffer_params_v1_create_immed(
        params, width, height, format, 0);
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
    if (bo_fd1 >= 0)
        close(bo_fd1);
    if (params)
        zwp_linux_buffer_params_v1_destroy(params);
    if (map_data)
        gbm_bo_unmap(bo, map_data);
    if (bo)
        gbm_bo_destroy(bo);
    if (gbm)
        gbm_device_destroy(gbm);
    if (gpu_fd >= 0)
        close(gpu_fd);
    return -1;
}

int main(int argc, char **argv)
{
    struct app app;
    int frames = 0;

    fprintf(stderr, "dmabufsmoke: start\n");
    fflush(stderr);
    signal(SIGALRM, timeout_handler);
    alarm(10);

    setenv("XDG_RUNTIME_DIR", "/tmp", 0);
    setenv("WAYLAND_DISPLAY", "wayland-0", 0);

    memset(&app, 0, sizeof(app));
    app.running = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--nv12") == 0)
            app.use_nv12 = 1;
        else if (strcmp(argv[i], "--explicit-sync") == 0)
            app.use_explicit_sync = 1;
        else if (strncmp(argv[i], "--hold-ms=", 10) == 0)
            app.hold_ms = atoi(argv[i] + 10);
    }
    g_phase = "connect";
    app.display = wl_display_connect(NULL);
    if (!app.display) {
        fprintf(stderr, "dmabufsmoke: wl_display_connect failed\n");
        return 1;
    }
    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, &app);
    g_phase = "registry";
    wl_display_roundtrip(app.display);

    if (!app.compositor || !app.wm_base || !app.dmabuf ||
        (app.use_explicit_sync && !app.explicit_sync)) {
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
    if (app.use_explicit_sync)
        app.surface_sync =
            zwp_linux_explicit_synchronization_v1_get_synchronization(
                app.explicit_sync, app.surface);
    g_phase = "initial-commit";
    wl_surface_commit(app.surface);

    g_phase = "configure";
    while (!app.configured && wl_display_dispatch(app.display) >= 0)
        ;
    g_phase = "create-buffer";
    if (!app.configured || create_dmabuf_buffer(&app) != 0)
        return 1;

    if (app.use_explicit_sync) {
        app.release =
            zwp_linux_surface_synchronization_v1_get_release(app.surface_sync);
        zwp_linux_buffer_release_v1_add_listener(app.release,
                                                 &release_listener, &app);
    }
    wl_surface_attach(app.surface, app.buffer, 0, 0);
    wl_surface_damage(app.surface, 0, 0, 240, 160);
    g_phase = "buffer-commit";
    wl_surface_commit(app.surface);
    g_phase = "present-roundtrip";
    wl_display_roundtrip(app.display);
    if (app.hold_ms > 0) {
        g_phase = "hold";
        usleep((useconds_t)app.hold_ms * 1000);
        wl_display_dispatch_pending(app.display);
    }

    g_phase = "release-wait";
    while (app.running && frames++ < 20) {
        wl_display_dispatch_pending(app.display);
        if (!app.use_explicit_sync || app.release_seen)
            break;
    }
    if (app.use_explicit_sync && !app.release_seen) {
        g_phase = "explicit-release-roundtrip";
        for (int i = 0; i < 20 && !app.release_seen; i++)
            wl_display_roundtrip(app.display);
    }
    if (app.use_explicit_sync && !app.release_seen) {
        fprintf(stderr, "dmabufsmoke: explicit sync release missing\n");
        return 1;
    }

    alarm(0);
    printf("dmabufsmoke: presented linux-dmabuf buffer format=%s planes=%d\n",
           app.use_nv12 ? "NV12" : "XRGB8888",
           app.use_nv12 ? 2 : 1);
    if (app.use_nv12) {
        printf("dmabufsmoke: linux-dmabuf NV12 GBM backend ok "
               "path=%s backend=%s planes=%d modifier=0x%lx\n",
               app.gbm_path, app.gbm_backend, app.gbm_plane_count,
               (unsigned long)app.gbm_modifier);
    }
    if (app.use_explicit_sync)
        printf("dmabufsmoke: explicit-sync release=%s\n",
               app.release_fenced ? "fenced" : "immediate");
    fflush(stdout);
    wl_buffer_destroy(app.buffer);
    xdg_toplevel_destroy(app.toplevel);
    xdg_surface_destroy(app.xdg_surface);
    wl_surface_destroy(app.surface);
    zwp_linux_dmabuf_v1_destroy(app.dmabuf);
    if (app.surface_sync)
        zwp_linux_surface_synchronization_v1_destroy(app.surface_sync);
    if (app.explicit_sync)
        zwp_linux_explicit_synchronization_v1_destroy(app.explicit_sync);
    xdg_wm_base_destroy(app.wm_base);
    wl_compositor_destroy(app.compositor);
    wl_registry_destroy(app.registry);
    wl_display_disconnect(app.display);
    return 0;
}
