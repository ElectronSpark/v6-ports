/*
 * glsmoke.c - small repo-local software OpenGL smoke client.
 *
 * This intentionally does not depend on Mesa.  It exposes a tiny GL-shaped
 * raster path over a Wayland SHM buffer so xv6 can validate graphical-client
 * presentation before a real EGL/Mesa stack exists.
 */

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define GL_WINDOW_W 480
#define GL_WINDOW_H 360

struct xv6gl_ctx {
    uint32_t *pixels;
    int width;
    int height;
};

struct vec2 {
    float x;
    float y;
};

struct app_state {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct wl_buffer *buffer;
    struct wl_callback *frame_cb;
    struct xv6gl_ctx gl;
    uint32_t *pixels;
    size_t pixels_size;
    int shm_fd;
    int configured;
    int running;
    int frame;
    int max_frames;
};

static uint32_t pack_rgba(float r, float g, float b, float a)
{
    uint32_t ri = (uint32_t)(r * 255.0f);
    uint32_t gi = (uint32_t)(g * 255.0f);
    uint32_t bi = (uint32_t)(b * 255.0f);
    uint32_t ai = (uint32_t)(a * 255.0f);

    if (ri > 255) ri = 255;
    if (gi > 255) gi = 255;
    if (bi > 255) bi = 255;
    if (ai > 255) ai = 255;
    return (ai << 24) | (ri << 16) | (gi << 8) | bi;
}

static void xv6gl_clear(struct xv6gl_ctx *gl, uint32_t color)
{
    for (int i = 0; i < gl->width * gl->height; i++)
        gl->pixels[i] = color;
}

static float edge_fn(struct vec2 a, struct vec2 b, struct vec2 p)
{
    return (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);
}

static void xv6gl_triangle(struct xv6gl_ctx *gl, struct vec2 a, struct vec2 b,
                           struct vec2 c, uint32_t ca, uint32_t cb,
                           uint32_t cc)
{
    float area = edge_fn(a, b, c);
    int min_x = (int)floorf(fminf(a.x, fminf(b.x, c.x)));
    int max_x = (int)ceilf(fmaxf(a.x, fmaxf(b.x, c.x)));
    int min_y = (int)floorf(fminf(a.y, fminf(b.y, c.y)));
    int max_y = (int)ceilf(fmaxf(a.y, fmaxf(b.y, c.y)));

    if (area == 0.0f)
        return;
    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= gl->width) max_x = gl->width - 1;
    if (max_y >= gl->height) max_y = gl->height - 1;

    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            struct vec2 p = { (float)x + 0.5f, (float)y + 0.5f };
            float w0 = edge_fn(b, c, p) / area;
            float w1 = edge_fn(c, a, p) / area;
            float w2 = edge_fn(a, b, p) / area;

            if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) {
                uint32_t ar = (ca >> 16) & 0xff;
                uint32_t ag = (ca >> 8) & 0xff;
                uint32_t ab = ca & 0xff;
                uint32_t br = (cb >> 16) & 0xff;
                uint32_t bg = (cb >> 8) & 0xff;
                uint32_t bb = cb & 0xff;
                uint32_t cr = (cc >> 16) & 0xff;
                uint32_t cg = (cc >> 8) & 0xff;
                uint32_t cblue = cc & 0xff;
                uint32_t r = (uint32_t)(ar * w0 + br * w1 + cr * w2);
                uint32_t g = (uint32_t)(ag * w0 + bg * w1 + cg * w2);
                uint32_t blue = (uint32_t)(ab * w0 + bb * w1 + cblue * w2);
                gl->pixels[y * gl->width + x] =
                    0xff000000 | (r << 16) | (g << 8) | blue;
            }
        }
    }
}

static struct vec2 rotate_point(float x, float y, float angle, float cx, float cy)
{
    float s = sinf(angle);
    float c = cosf(angle);
    struct vec2 out;

    out.x = cx + x * c - y * s;
    out.y = cy + x * s + y * c;
    return out;
}

static void render_frame(struct app_state *app)
{
    struct xv6gl_ctx *gl = &app->gl;
    float cx = gl->width * 0.5f;
    float cy = gl->height * 0.52f;
    float angle = app->frame * 0.055f;
    float radius = gl->height * 0.36f;
    uint32_t bg0 = pack_rgba(0.04f, 0.07f, 0.08f, 1.0f);
    uint32_t bg1 = pack_rgba(0.10f, 0.13f, 0.13f, 1.0f);

    xv6gl_clear(gl, bg0);
    for (int y = 0; y < gl->height; y++) {
        uint32_t color = (y / 12) & 1 ? bg0 : bg1;
        for (int x = 0; x < gl->width; x++)
            gl->pixels[y * gl->width + x] = color;
    }

    struct vec2 a = rotate_point(0.0f, -radius, angle, cx, cy);
    struct vec2 b = rotate_point(radius * 0.92f, radius * 0.72f, angle, cx, cy);
    struct vec2 c = rotate_point(-radius * 0.92f, radius * 0.72f, angle, cx, cy);
    xv6gl_triangle(gl, a, b, c,
                   pack_rgba(0.98f, 0.21f, 0.18f, 1.0f),
                   pack_rgba(0.18f, 0.80f, 0.42f, 1.0f),
                   pack_rgba(0.20f, 0.42f, 1.0f, 1.0f));
}

static int create_shm_file(size_t size)
{
    char path[64];
    int fd;

    snprintf(path, sizeof(path), "/tmp/glsmoke-%ld", (long)getpid());
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

static int init_buffer(struct app_state *app)
{
    struct wl_shm_pool *pool;
    int stride = GL_WINDOW_W * 4;

    app->pixels_size = stride * GL_WINDOW_H;
    app->shm_fd = create_shm_file(app->pixels_size);
    if (app->shm_fd < 0) {
        fprintf(stderr, "glsmoke: create shm file: %s\n", strerror(errno));
        return -1;
    }

    app->pixels = mmap(NULL, app->pixels_size, PROT_READ | PROT_WRITE,
                       MAP_SHARED, app->shm_fd, 0);
    if (app->pixels == MAP_FAILED) {
        fprintf(stderr, "glsmoke: mmap shm: %s\n", strerror(errno));
        app->pixels = NULL;
        close(app->shm_fd);
        app->shm_fd = -1;
        return -1;
    }

    pool = wl_shm_create_pool(app->shm, app->shm_fd, (int)app->pixels_size);
    app->buffer = wl_shm_pool_create_buffer(pool, 0, GL_WINDOW_W, GL_WINDOW_H,
                                            stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);

    app->gl.pixels = app->pixels;
    app->gl.width = GL_WINDOW_W;
    app->gl.height = GL_WINDOW_H;
    return 0;
}

static void draw_and_commit(struct app_state *app);

static void frame_done(void *data, struct wl_callback *cb, uint32_t time)
{
    struct app_state *app = data;
    (void)time;

    if (cb)
        wl_callback_destroy(cb);
    app->frame_cb = NULL;
    app->frame++;
    if (app->max_frames > 0 && app->frame >= app->max_frames) {
        app->running = 0;
        return;
    }
    draw_and_commit(app);
}

static const struct wl_callback_listener frame_listener = {
    .done = frame_done,
};

static void draw_and_commit(struct app_state *app)
{
    render_frame(app);
    wl_surface_attach(app->surface, app->buffer, 0, 0);
    wl_surface_damage(app->surface, 0, 0, GL_WINDOW_W, GL_WINDOW_H);
    app->frame_cb = wl_surface_frame(app->surface);
    wl_callback_add_listener(app->frame_cb, &frame_listener, app);
    wl_surface_commit(app->surface);
}

static void xdg_surface_configure(void *data, struct xdg_surface *surface,
                                  uint32_t serial)
{
    struct app_state *app = data;

    xdg_surface_ack_configure(surface, serial);
    if (!app->configured) {
        app->configured = 1;
        draw_and_commit(app);
    }
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                               int32_t width, int32_t height,
                               struct wl_array *states)
{
    (void)data;
    (void)toplevel;
    (void)width;
    (void)height;
    (void)states;
}

static void toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
    struct app_state *app = data;
    (void)toplevel;
    app->running = 0;
}

static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = toplevel_configure,
    .close = toplevel_close,
};

static void wm_base_ping(void *data, struct xdg_wm_base *wm_base,
                         uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = wm_base_ping,
};

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface,
                            uint32_t version)
{
    struct app_state *app = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        app->compositor = wl_registry_bind(registry, name,
                                           &wl_compositor_interface,
                                           version > 4 ? 4 : version);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        app->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        app->wm_base = wl_registry_bind(registry, name,
                                        &xdg_wm_base_interface,
                                        version > 2 ? 2 : version);
        xdg_wm_base_add_listener(app->wm_base, &wm_base_listener, app);
    }
}

static void registry_remove(void *data, struct wl_registry *registry,
                            uint32_t name)
{
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_remove,
};

static int init_wayland(struct app_state *app)
{
    app->display = wl_display_connect(NULL);
    if (!app->display) {
        fprintf(stderr, "glsmoke: wl_display_connect failed\n");
        return -1;
    }

    app->registry = wl_display_get_registry(app->display);
    wl_registry_add_listener(app->registry, &registry_listener, app);
    wl_display_roundtrip(app->display);
    wl_display_roundtrip(app->display);

    if (!app->compositor || !app->shm || !app->wm_base) {
        fprintf(stderr, "glsmoke: compositor/shm/xdg globals unavailable\n");
        return -1;
    }

    if (init_buffer(app) < 0)
        return -1;

    app->surface = wl_compositor_create_surface(app->compositor);
    app->xdg_surface = xdg_wm_base_get_xdg_surface(app->wm_base, app->surface);
    xdg_surface_add_listener(app->xdg_surface, &xdg_surface_listener, app);
    app->toplevel = xdg_surface_get_toplevel(app->xdg_surface);
    xdg_toplevel_add_listener(app->toplevel, &toplevel_listener, app);
    xdg_toplevel_set_title(app->toplevel, "xv6 GL Smoke");
    xdg_toplevel_set_app_id(app->toplevel, "glsmoke");
    wl_surface_commit(app->surface);
    return 0;
}

static void cleanup(struct app_state *app)
{
    if (app->frame_cb)
        wl_callback_destroy(app->frame_cb);
    if (app->toplevel)
        xdg_toplevel_destroy(app->toplevel);
    if (app->xdg_surface)
        xdg_surface_destroy(app->xdg_surface);
    if (app->surface)
        wl_surface_destroy(app->surface);
    if (app->buffer)
        wl_buffer_destroy(app->buffer);
    if (app->pixels)
        munmap(app->pixels, app->pixels_size);
    if (app->shm_fd >= 0)
        close(app->shm_fd);
    if (app->wm_base)
        xdg_wm_base_destroy(app->wm_base);
    if (app->shm)
        wl_shm_destroy(app->shm);
    if (app->compositor)
        wl_compositor_destroy(app->compositor);
    if (app->registry)
        wl_registry_destroy(app->registry);
    if (app->display)
        wl_display_disconnect(app->display);
}

int main(int argc, char **argv)
{
    struct app_state app;

    memset(&app, 0, sizeof(app));
    app.shm_fd = -1;
    app.running = 1;
    app.max_frames = 0;

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--frames=", 9) == 0)
            app.max_frames = atoi(argv[i] + 9);
    }

    if (init_wayland(&app) < 0) {
        cleanup(&app);
        return 1;
    }

    while (app.running && wl_display_dispatch(app.display) >= 0)
        ;

    cleanup(&app);
    return 0;
}
