#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"

#define WIN_W 220
#define WIN_H 120
#define CUR_W 32
#define CUR_H 32

struct shm_buf {
    struct wl_buffer *buffer;
    uint32_t *pixels;
    size_t size;
    int fd;
    int width;
    int height;
    int stride;
};

struct app {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct wl_surface *surface;
    struct xdg_wm_base *wm_base;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct wl_surface *cursor_surface;
    struct shm_buf window_buf;
    struct shm_buf cursor_buf;
    uint32_t enter_serial;
    int configured;
    int cursor_ready;
    int cursor_set;
    int running;
};

static int create_shm_file(size_t size)
{
    char path[64];
    int fd;

    snprintf(path, sizeof(path), "/tmp/cursorsmoke-%ld", (long)getpid());
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

static int shm_buf_init(struct app *app, struct shm_buf *buf, int width,
                        int height, uint32_t format)
{
    struct wl_shm_pool *pool;

    memset(buf, 0, sizeof(*buf));
    buf->fd = -1;
    buf->width = width;
    buf->height = height;
    buf->stride = width * 4;
    buf->size = (size_t)buf->stride * (size_t)height;
    buf->fd = create_shm_file(buf->size);
    if (buf->fd < 0)
        return -1;
    buf->pixels = mmap(NULL, buf->size, PROT_READ | PROT_WRITE, MAP_SHARED,
                       buf->fd, 0);
    if (buf->pixels == MAP_FAILED) {
        buf->pixels = NULL;
        return -1;
    }
    pool = wl_shm_create_pool(app->shm, buf->fd, (int)buf->size);
    buf->buffer = wl_shm_pool_create_buffer(pool, 0, width, height,
                                            buf->stride, format);
    wl_shm_pool_destroy(pool);
    return buf->buffer ? 0 : -1;
}

static void shm_buf_destroy(struct shm_buf *buf)
{
    if (buf->buffer)
        wl_buffer_destroy(buf->buffer);
    if (buf->pixels)
        munmap(buf->pixels, buf->size);
    if (buf->fd >= 0)
        close(buf->fd);
    memset(buf, 0, sizeof(*buf));
    buf->fd = -1;
}

static uint32_t now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

static void fill_window(struct shm_buf *buf)
{
    int stride_px = buf->stride / 4;

    for (int y = 0; y < buf->height; y++) {
        for (int x = 0; x < buf->width; x++) {
            uint8_t r = (uint8_t)(32 + (x * 96) / buf->width);
            uint8_t g = (uint8_t)(70 + (y * 120) / buf->height);
            uint8_t b = ((x / 10 + y / 10) & 1) ? 0xe8 : 0x54;
            buf->pixels[y * stride_px + x] =
                0xff000000u | ((uint32_t)r << 16) |
                ((uint32_t)g << 8) | b;
        }
    }
}

static void fill_cursor(struct shm_buf *buf)
{
    int stride_px = buf->stride / 4;

    memset(buf->pixels, 0, buf->size);
    for (int y = 0; y < buf->height; y++) {
        for (int x = 0; x < buf->width; x++) {
            int dx = x - 7;
            int dy = y - 7;
            int cross = (x >= 5 && x <= 9) || (y >= 5 && y <= 9);
            int ring = dx * dx + dy * dy;
            uint32_t px = 0;

            if (cross)
                px = 0xffff2040u;
            else if (ring >= 110 && ring <= 170)
                px = 0xff30f0ffu;
            else if ((x == y || x + y == 31) && x >= 12)
                px = 0xffffffffu;
            buf->pixels[y * stride_px + x] = px;
        }
    }
}

static void try_set_cursor(struct app *app)
{
    if (!app->pointer || !app->cursor_ready || !app->enter_serial ||
        app->cursor_set)
        return;
    wl_pointer_set_cursor(app->pointer, app->enter_serial,
                          app->cursor_surface, 7, 7);
    wl_display_flush(app->display);
    app->cursor_set = 1;
    fprintf(stderr,
            "cursorsmoke: set_cursor sent size=%dx%d hotspot=7,7 serial=%u\n",
            CUR_W, CUR_H, app->enter_serial);
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

static void pointer_enter(void *data, struct wl_pointer *pointer,
                          uint32_t serial, struct wl_surface *surface,
                          wl_fixed_t sx, wl_fixed_t sy)
{
    struct app *app = data;

    (void)pointer;
    (void)surface;
    (void)sx;
    (void)sy;
    app->enter_serial = serial;
    try_set_cursor(app);
}

static void pointer_leave(void *data, struct wl_pointer *pointer,
                          uint32_t serial, struct wl_surface *surface)
{
    (void)data;
    (void)pointer;
    (void)serial;
    (void)surface;
}

static void pointer_motion(void *data, struct wl_pointer *pointer,
                           uint32_t time, wl_fixed_t sx, wl_fixed_t sy)
{
    (void)data;
    (void)pointer;
    (void)time;
    (void)sx;
    (void)sy;
}

static void pointer_button(void *data, struct wl_pointer *pointer,
                           uint32_t serial, uint32_t time, uint32_t button,
                           uint32_t state)
{
    (void)data;
    (void)pointer;
    (void)serial;
    (void)time;
    (void)button;
    (void)state;
}

static void pointer_axis(void *data, struct wl_pointer *pointer,
                         uint32_t time, uint32_t axis, wl_fixed_t value)
{
    (void)data;
    (void)pointer;
    (void)time;
    (void)axis;
    (void)value;
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
};

static void seat_capabilities(void *data, struct wl_seat *seat,
                              uint32_t capabilities)
{
    struct app *app = data;

    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !app->pointer) {
        app->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(app->pointer, &pointer_listener, app);
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name)
{
    (void)data;
    (void)seat;
    (void)name;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

static void shm_format(void *data, struct wl_shm *shm, uint32_t format)
{
    (void)data;
    (void)shm;
    (void)format;
}

static const struct wl_shm_listener shm_listener = {
    .format = shm_format,
};

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface,
                            uint32_t version)
{
    struct app *app = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        app->compositor = wl_registry_bind(
            registry, name, &wl_compositor_interface, version < 4 ? version : 4);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        app->shm = wl_registry_bind(
            registry, name, &wl_shm_interface, version < 1 ? version : 1);
        wl_shm_add_listener(app->shm, &shm_listener, app);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        app->seat = wl_registry_bind(
            registry, name, &wl_seat_interface, version < 5 ? version : 5);
        wl_seat_add_listener(app->seat, &seat_listener, app);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        app->wm_base = wl_registry_bind(
            registry, name, &xdg_wm_base_interface, version < 2 ? version : 2);
        xdg_wm_base_add_listener(app->wm_base, &wm_base_listener, app);
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

static int map_window(struct app *app)
{
    app->surface = wl_compositor_create_surface(app->compositor);
    app->xdg_surface = xdg_wm_base_get_xdg_surface(app->wm_base, app->surface);
    xdg_surface_add_listener(app->xdg_surface, &xdg_surface_listener, app);
    app->toplevel = xdg_surface_get_toplevel(app->xdg_surface);
    xdg_toplevel_add_listener(app->toplevel, &xdg_toplevel_listener, app);
    xdg_toplevel_set_title(app->toplevel, "Cursor Smoke");
    wl_surface_commit(app->surface);

    while (!app->configured) {
        if (wl_display_dispatch(app->display) < 0)
            return -1;
    }

    if (shm_buf_init(app, &app->window_buf, WIN_W, WIN_H,
                     WL_SHM_FORMAT_XRGB8888) != 0)
        return -1;
    fill_window(&app->window_buf);
    wl_surface_attach(app->surface, app->window_buf.buffer, 0, 0);
    wl_surface_damage(app->surface, 0, 0, WIN_W, WIN_H);
    wl_surface_commit(app->surface);
    return 0;
}

static int make_cursor(struct app *app)
{
    app->cursor_surface = wl_compositor_create_surface(app->compositor);
    if (!app->cursor_surface)
        return -1;
    if (shm_buf_init(app, &app->cursor_buf, CUR_W, CUR_H,
                     WL_SHM_FORMAT_ARGB8888) != 0)
        return -1;
    fill_cursor(&app->cursor_buf);
    wl_surface_attach(app->cursor_surface, app->cursor_buf.buffer, 0, 0);
    wl_surface_damage(app->cursor_surface, 0, 0, CUR_W, CUR_H);
    wl_surface_commit(app->cursor_surface);
    app->cursor_ready = 1;
    try_set_cursor(app);
    return 0;
}

int main(void)
{
    struct app app;
    uint32_t start;

    memset(&app, 0, sizeof(app));
    app.running = 1;
    app.window_buf.fd = -1;
    app.cursor_buf.fd = -1;
    setenv("XDG_RUNTIME_DIR", "/tmp", 0);
    setenv("WAYLAND_DISPLAY", "wayland-0", 0);

    app.display = wl_display_connect(NULL);
    if (!app.display) {
        fprintf(stderr, "cursorsmoke: wl_display_connect failed\n");
        return 1;
    }
    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, &app);
    wl_display_roundtrip(app.display);
    wl_display_roundtrip(app.display);

    if (!app.compositor || !app.shm || !app.wm_base || !app.seat ||
        !app.pointer) {
        fprintf(stderr,
                "cursorsmoke: missing globals compositor=%d shm=%d wm_base=%d seat=%d pointer=%d\n",
                !!app.compositor, !!app.shm, !!app.wm_base, !!app.seat,
                !!app.pointer);
        return 2;
    }
    if (map_window(&app) != 0 || make_cursor(&app) != 0) {
        fprintf(stderr, "cursorsmoke: setup failed errno=%d (%s)\n",
                errno, strerror(errno));
        return 3;
    }

    fprintf(stderr, "cursorsmoke: ready; move pointer over window\n");
    start = now_ms();
    while (app.running && now_ms() - start < 25000) {
        if (wl_display_dispatch(app.display) < 0)
            break;
    }

    fprintf(stderr, "cursorsmoke: complete cursor_set=%d\n", app.cursor_set);
    if (app.pointer)
        wl_pointer_destroy(app.pointer);
    if (app.seat)
        wl_seat_destroy(app.seat);
    if (app.cursor_surface)
        wl_surface_destroy(app.cursor_surface);
    if (app.toplevel)
        xdg_toplevel_destroy(app.toplevel);
    if (app.xdg_surface)
        xdg_surface_destroy(app.xdg_surface);
    if (app.surface)
        wl_surface_destroy(app.surface);
    shm_buf_destroy(&app.cursor_buf);
    shm_buf_destroy(&app.window_buf);
    if (app.shm)
        wl_shm_destroy(app.shm);
    if (app.wm_base)
        xdg_wm_base_destroy(app.wm_base);
    if (app.compositor)
        wl_compositor_destroy(app.compositor);
    if (app.registry)
        wl_registry_destroy(app.registry);
    if (app.display)
        wl_display_disconnect(app.display);
    return app.cursor_set ? 0 : 4;
}
