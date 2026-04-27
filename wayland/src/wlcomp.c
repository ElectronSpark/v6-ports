/*
 * wlcomp.c — Minimal Wayland compositor for xv6.
 *
 * Direct framebuffer rendering via /dev/fb0 GPU ioctl.
 * PS/2 mouse (/dev/mouse) and keyboard (/dev/kbd) input.
 * Implements: wl_compositor, wl_shm, wl_seat, wl_output, xdg_wm_base.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <time.h>
#include <dirent.h>

#include <wayland/wayland-server-core.h>
#include <wayland/wayland-server-protocol.h>
#include <wayland/xdg-shell-server-protocol.h>

/* xv6-specific syscall numbers (not in musl headers) */
#define XV6_SYS_poweroff  166

/* ══════════════════════════════════════════════════════════════════════
 *  Framebuffer
 * ══════════════════════════════════════════════════════════════════════ */

#define FBIOGET_VSCREENINFO  0x4600
#define FB_GPU_FILL_RECT     0x4610
#define FB_GPU_BLIT          0x4611

struct fb_var_screeninfo {
    uint32_t xres, yres, bits_per_pixel, pitch;
};

struct fb_gpu_fill {
    uint32_t x, y, w, h, color;
};

struct fb_gpu_blit {
    uint32_t x, y, w, h, src_pitch;
    uint64_t pixels;
};

/* ── Mouse event (matches kernel struct mouse_event) ──────────────── */

struct mouse_event {
    int16_t  dx, dy;
    uint8_t  buttons;
    uint8_t  flags;
    int8_t   dz;
    uint8_t  pad[1];
};
#define MOUSE_EVENT_F_ABSOLUTE 0x01

/* ── Kbd event (matches kernel struct kbd_event) ──────────────────── */

struct kbd_event {
    uint8_t keycode;
    uint8_t scancode;
    uint8_t pressed;
    uint8_t modifiers;
};

/* ══════════════════════════════════════════════════════════════════════
 *  Globals
 * ══════════════════════════════════════════════════════════════════════ */

static struct wl_display *g_display;

/* Framebuffer state */
static int      g_fb_fd   = -1;
static uint32_t g_fb_w, g_fb_h;
static uint32_t g_fb_pitch;
static uint32_t *g_fb_buf;  /* compositing buffer */

/* Input state */
static int      g_mouse_fd = -1;
static int      g_kbd_fd   = -1;
static int16_t  g_cursor_x, g_cursor_y;
static uint8_t  g_buttons;
static uint32_t g_serial;

/* Protocol globals */
static struct wl_global *g_compositor_global;
static struct wl_global *g_shm_global;
static struct wl_global *g_seat_global;
static struct wl_global *g_output_global;
static struct wl_global *g_xdg_wm_global;

/* ══════════════════════════════════════════════════════════════════════
 *  Surface / window tracking
 * ══════════════════════════════════════════════════════════════════════ */

#define MAX_SURFACES 32

struct wlcomp_shm_pool;

struct wlcomp_buffer {
    struct wl_resource *resource;  /* wl_buffer resource */
    struct wlcomp_shm_pool *pool;
    int32_t             offset;
    int32_t             width;
    int32_t             height;
    int32_t             stride;
    uint32_t            format;
};

struct wlcomp_surface {
    struct wl_resource *resource;
    struct wl_resource *xdg_surface;
    struct wl_resource *xdg_toplevel;
    struct wlcomp_buffer *pending_buf;
    int     has_pending_buffer;
    struct wlcomp_buffer *committed_buf;
    int     buffer_released;  /* 1 = release sent, don't re-release */
    struct wl_resource *frame_cb;
    int32_t x, y;
    int     mapped;
    int     minimized;
    int     maximized;        /* 1 = currently maximized */
    int     is_cursor;        /* 1 = cursor surface, skip normal rendering */
    /* Saved geometry for restore from maximize */
    int32_t saved_x, saved_y, saved_w, saved_h;
    char    title[64];
    char    app_id[64];
    struct wl_list link;  /* in g_surfaces */
};

static struct wl_list g_surfaces;  /* list of wlcomp_surface */
static struct wlcomp_surface *g_focused;  /* pointer-focused Wayland surface */
static struct wlcomp_surface *g_kbd_focused; /* keyboard-focused (click-to-focus) */
static int g_kbd_enter_pending; /* deferred keyboard enter for g_kbd_focused */

/* Client cursor state */
static struct wlcomp_surface *g_cursor_surface;   /* client-provided cursor */
static int32_t g_cursor_hotspot_x, g_cursor_hotspot_y;

/* Interactive move/resize state for Wayland surfaces */
static struct wlcomp_surface *g_grab_surface;  /* surface being moved/resized */
static int g_grab_mode;  /* 0=none, 1=move, 2=resize */
static uint32_t g_grab_edges;  /* resize edges bitmask */
static int32_t g_grab_start_mx, g_grab_start_my;  /* cursor pos at grab start */
static int32_t g_grab_start_x, g_grab_start_y;    /* surface pos at grab start */
static int32_t g_grab_start_w, g_grab_start_h;    /* surface size at grab start */

static struct wlcomp_surface *surface_from_resource(struct wl_resource *r)
{
    return (struct wlcomp_surface *)wl_resource_get_user_data(r);
}

static inline uint32_t buffer_pixel_alpha(struct wlcomp_buffer *buf,
                                          uint32_t pixel)
{
    if (buf->format == WL_SHM_FORMAT_XRGB8888)
        return 0xFF;
    return (pixel >> 24) & 0xFF;
}

static inline uint32_t buffer_pixel_argb(struct wlcomp_buffer *buf,
                                         uint32_t pixel)
{
    if (buf->format == WL_SHM_FORMAT_XRGB8888)
        return pixel | 0xFF000000;
    return pixel;
}

/* Find top surface at a point */
static struct wlcomp_surface *surface_at(int32_t px, int32_t py,
                                          int32_t *sx, int32_t *sy)
{
    /* Reserve taskbar area — never hit Wayland surfaces there */
    if (py >= (int32_t)(g_fb_h - 36 /* TASKBAR_H */))
        return NULL;

    struct wlcomp_surface *s;
    /* Walk reversed (top first = most recently added) */
    wl_list_for_each_reverse(s, &g_surfaces, link) {
        if (!s->mapped || s->minimized || !s->committed_buf || s->is_cursor)
            continue;
        int32_t bw = s->committed_buf->width;
        int32_t bh = s->committed_buf->height;
        if (px >= s->x && px < s->x + bw &&
            py >= s->y && py < s->y + bh) {
            if (sx) *sx = px - s->x;
            if (sy) *sy = py - s->y;
            return s;
        }
    }
    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════
 *  wl_shm_pool / wl_buffer
 * ══════════════════════════════════════════════════════════════════════ */

struct wlcomp_shm_pool {
    void   *data;
    int32_t size;
    int     fd;
    int     refcount;
    int     destroyed;
};

static int buffer_is_referenced(struct wlcomp_buffer *buf)
{
    struct wlcomp_surface *surf;

    wl_list_for_each(surf, &g_surfaces, link) {
        if (surf->committed_buf == buf || surf->pending_buf == buf)
            return 1;
    }

    return 0;
}

static void shm_pool_maybe_destroy(struct wlcomp_shm_pool *pool)
{
    if (!pool || !pool->destroyed || pool->refcount > 0)
        return;

    if (pool->data)
        munmap(pool->data, pool->size);
    if (pool->fd >= 0)
        close(pool->fd);
    free(pool);
}

static void buffer_free(struct wlcomp_buffer *buf)
{
    if (!buf)
        return;

    if (buf->pool) {
        if (buf->pool->refcount > 0)
            buf->pool->refcount--;
        shm_pool_maybe_destroy(buf->pool);
    }

    free(buf);
}

static void buffer_maybe_free(struct wlcomp_buffer *buf)
{
    if (!buf || buf->resource || buffer_is_referenced(buf))
        return;

    buffer_free(buf);
}

static void *buffer_data(struct wlcomp_buffer *buf)
{
    if (!buf || !buf->pool || !buf->pool->data)
        return NULL;

    return (char *)buf->pool->data + buf->offset;
}

static void buffer_destroy_handler(struct wl_resource *resource)
{
    struct wlcomp_buffer *buf = wl_resource_get_user_data(resource);
    if (!buf)
        return;

    buf->resource = NULL;
    buffer_maybe_free(buf);
}

static void buffer_destroy(struct wl_client *client, struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static const struct wl_buffer_interface buffer_impl = {
    .destroy = buffer_destroy,
};

static void pool_create_buffer(struct wl_client *client,
                               struct wl_resource *resource,
                               uint32_t id,
                               int32_t offset,
                               int32_t width, int32_t height,
                               int32_t stride, uint32_t format)
{
    struct wlcomp_shm_pool *pool = wl_resource_get_user_data(resource);

    struct wlcomp_buffer *buf = calloc(1, sizeof(*buf));
    if (!buf) {
        wl_resource_post_no_memory(resource);
        return;
    }

    buf->pool   = pool;
    buf->offset = offset;
    buf->width  = width;
    buf->height = height;
    buf->stride = stride;
    buf->format = format;
    pool->refcount++;

    struct wl_resource *buf_res = wl_resource_create(
        client, &wl_buffer_interface, 1, id);
    if (!buf_res) {
        buffer_free(buf);
        wl_resource_post_no_memory(resource);
        return;
    }
    buf->resource = buf_res;
    wl_resource_set_implementation(buf_res, &buffer_impl, buf,
                                  buffer_destroy_handler);
}

static void pool_destroy(struct wl_client *client, struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void pool_resize(struct wl_client *client, struct wl_resource *resource,
                        int32_t size)
{
    struct wlcomp_shm_pool *pool = wl_resource_get_user_data(resource);
    if (size <= pool->size)
        return;
    void *new_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
                          pool->fd, 0);
    if (new_data == MAP_FAILED) {
        wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_FD,
                               "mmap resize failed");
        return;
    }
    if (pool->data)
        munmap(pool->data, pool->size);
    pool->data = new_data;
    pool->size = size;
}

static const struct wl_shm_pool_interface shm_pool_impl = {
    .create_buffer = pool_create_buffer,
    .destroy       = pool_destroy,
    .resize        = pool_resize,
};

static void pool_destroy_handler(struct wl_resource *resource)
{
    struct wlcomp_shm_pool *pool = wl_resource_get_user_data(resource);
    if (!pool)
        return;

    pool->destroyed = 1;
    shm_pool_maybe_destroy(pool);
}

/* ── wl_shm ──────────────────────────────────────────────────────── */

static void shm_create_pool(struct wl_client *client,
                            struct wl_resource *resource,
                            uint32_t id, int32_t fd, int32_t size)
{
    struct wlcomp_shm_pool *pool = calloc(1, sizeof(*pool));
    if (!pool) {
        close(fd);
        wl_resource_post_no_memory(resource);
        return;
    }

    pool->fd   = fd;
    pool->size = size;
    pool->data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (pool->data == MAP_FAILED) {
        pool->data = NULL;
        wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_FD,
                               "mmap failed");
        close(fd);
        free(pool);
        return;
    }

    struct wl_resource *pool_res = wl_resource_create(
        client, &wl_shm_pool_interface, 1, id);
    if (!pool_res) {
        munmap(pool->data, pool->size);
        close(fd);
        free(pool);
        wl_resource_post_no_memory(resource);
        return;
    }

    wl_resource_set_implementation(pool_res, &shm_pool_impl, pool,
                                  pool_destroy_handler);
}

static const struct wl_shm_interface shm_impl = {
    .create_pool = shm_create_pool,
};

static void shm_bind(struct wl_client *client, void *data,
                     uint32_t version, uint32_t id)
{
    (void)data;
    struct wl_resource *res = wl_resource_create(
        client, &wl_shm_interface, version, id);
    if (!res) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(res, &shm_impl, NULL, NULL);
    /* Advertise supported formats */
    wl_shm_send_format(res, WL_SHM_FORMAT_ARGB8888);
    wl_shm_send_format(res, WL_SHM_FORMAT_XRGB8888);
}

/* ══════════════════════════════════════════════════════════════════════
 *  wl_compositor / wl_surface
 * ══════════════════════════════════════════════════════════════════════ */

static void surface_destroy(struct wl_client *client, struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void surface_attach(struct wl_client *client, struct wl_resource *resource,
                           struct wl_resource *buffer, int32_t x, int32_t y)
{
    (void)client; (void)x; (void)y;
    struct wlcomp_surface *surf = surface_from_resource(resource);
    struct wlcomp_buffer *old_pending;

    if (!surf) return;
    old_pending = surf->pending_buf;

    if (buffer) {
        surf->pending_buf = wl_resource_get_user_data(buffer);
    } else {
        surf->pending_buf = NULL;
    }
    surf->has_pending_buffer = 1;

    if (old_pending && old_pending != surf->pending_buf)
        buffer_maybe_free(old_pending);
}

static void surface_damage(struct wl_client *client, struct wl_resource *resource,
                           int32_t x, int32_t y, int32_t w, int32_t h)
{
    (void)client; (void)resource; (void)x; (void)y; (void)w; (void)h;
    /* We repaint everything each frame, so ignoring damage is fine */
}

static void surface_frame(struct wl_client *client, struct wl_resource *resource,
                          uint32_t callback_id)
{
    struct wlcomp_surface *surf = surface_from_resource(resource);
    if (!surf) return;

    struct wl_resource *cb = wl_resource_create(client,
        &wl_callback_interface, 1, callback_id);
    if (!cb) {
        wl_resource_post_no_memory(resource);
        return;
    }
    /* Store the latest frame callback; we'll fire it after compositing */
    surf->frame_cb = cb;
}

static void surface_set_opaque_region(struct wl_client *client,
                                      struct wl_resource *resource,
                                      struct wl_resource *region)
{
    (void)client; (void)resource; (void)region;
}

static void surface_set_input_region(struct wl_client *client,
                                     struct wl_resource *resource,
                                     struct wl_resource *region)
{
    (void)client; (void)resource; (void)region;
}

static void surface_commit(struct wl_client *client, struct wl_resource *resource)
{
    (void)client;
    struct wlcomp_surface *surf = surface_from_resource(resource);
    struct wlcomp_buffer *old_committed;
    int old_buffer_released;
    if (!surf) return;

    if (surf->has_pending_buffer) {
        old_committed = surf->committed_buf;
        old_buffer_released = surf->buffer_released;
        surf->committed_buf = surf->pending_buf;
        surf->pending_buf = NULL;
        surf->has_pending_buffer = 0;

        if (old_committed && old_committed != surf->committed_buf) {
            if (old_committed->resource && !old_buffer_released)
                wl_buffer_send_release(old_committed->resource);
            buffer_maybe_free(old_committed);
        }

        surf->buffer_released = 0;
    }

    if (surf->committed_buf && surf->xdg_toplevel && !surf->mapped) {
        /* Center the window on screen (above taskbar) */
        int32_t bw = surf->committed_buf->width;
        int32_t bh = surf->committed_buf->height;
        surf->x = ((int32_t)g_fb_w - bw) / 2;
        surf->y = ((int32_t)g_fb_h - 36 - bh) / 2;
        if (surf->x < 0) surf->x = 0;
        if (surf->y < 0) surf->y = 0;
        surf->mapped = 1;

        /* Auto-focus keyboard on first mapped surface (deferred — the
         * keyboard enter event is sent in process_mouse where
         * g_keyboard_resources is in scope) */
        if (!g_kbd_focused) {
            g_kbd_focused = surf;
            g_kbd_enter_pending = 1;
        }
    }
}

static void surface_set_buffer_transform(struct wl_client *client,
                                         struct wl_resource *resource,
                                         int32_t transform)
{
    (void)client; (void)resource; (void)transform;
}

static void surface_set_buffer_scale(struct wl_client *client,
                                     struct wl_resource *resource,
                                     int32_t scale)
{
    (void)client; (void)resource; (void)scale;
}

static void surface_damage_buffer(struct wl_client *client,
                                  struct wl_resource *resource,
                                  int32_t x, int32_t y, int32_t w, int32_t h)
{
    (void)client; (void)resource; (void)x; (void)y; (void)w; (void)h;
}

static void surface_offset(struct wl_client *client,
                           struct wl_resource *resource,
                           int32_t x, int32_t y)
{
    (void)client; (void)resource; (void)x; (void)y;
}

static const struct wl_surface_interface surface_impl = {
    .destroy              = surface_destroy,
    .attach               = surface_attach,
    .damage               = surface_damage,
    .frame                = surface_frame,
    .set_opaque_region    = surface_set_opaque_region,
    .set_input_region     = surface_set_input_region,
    .commit               = surface_commit,
    .set_buffer_transform = surface_set_buffer_transform,
    .set_buffer_scale     = surface_set_buffer_scale,
    .damage_buffer        = surface_damage_buffer,
    .offset               = surface_offset,
};

static void surface_destroy_handler(struct wl_resource *resource)
{
    struct wlcomp_surface *surf = surface_from_resource(resource);
    if (surf) {
        struct wlcomp_buffer *pending_buf = surf->pending_buf;
        struct wlcomp_buffer *committed_buf = surf->committed_buf;

        if (g_focused == surf)
            g_focused = NULL;
        if (g_kbd_focused == surf)
            g_kbd_focused = NULL;
        if (g_cursor_surface == surf)
            g_cursor_surface = NULL;
        if (g_grab_surface == surf) {
            g_grab_surface = NULL;
            g_grab_mode = 0;
        }
        wl_list_remove(&surf->link);
        free(surf);

        if (pending_buf && pending_buf != committed_buf)
            buffer_maybe_free(pending_buf);
        buffer_maybe_free(committed_buf);
    }
}

/* wl_region — stub (we don't use regions) */

static void region_destroy(struct wl_client *client, struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void region_add(struct wl_client *client, struct wl_resource *resource,
                       int32_t x, int32_t y, int32_t w, int32_t h)
{
    (void)client; (void)resource; (void)x; (void)y; (void)w; (void)h;
}

static void region_subtract(struct wl_client *client, struct wl_resource *resource,
                            int32_t x, int32_t y, int32_t w, int32_t h)
{
    (void)client; (void)resource; (void)x; (void)y; (void)w; (void)h;
}

static const struct wl_region_interface region_impl = {
    .destroy  = region_destroy,
    .add      = region_add,
    .subtract = region_subtract,
};

/* wl_compositor */

static void comp_create_surface(struct wl_client *client,
                                struct wl_resource *resource,
                                uint32_t id)
{
    (void)resource;

    struct wlcomp_surface *surf = calloc(1, sizeof(*surf));
    if (!surf) {
        wl_client_post_no_memory(client);
        return;
    }

    struct wl_resource *res = wl_resource_create(
        client, &wl_surface_interface, wl_resource_get_version(resource), id);
    if (!res) {
        free(surf);
        wl_client_post_no_memory(client);
        return;
    }

    surf->resource = res;
    wl_resource_set_implementation(res, &surface_impl, surf,
                                  surface_destroy_handler);
    wl_list_insert(&g_surfaces, &surf->link);
}

static void comp_create_region(struct wl_client *client,
                               struct wl_resource *resource,
                               uint32_t id)
{
    (void)resource;
    struct wl_resource *res = wl_resource_create(
        client, &wl_region_interface, 1, id);
    if (!res) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(res, &region_impl, NULL, NULL);
}

static const struct wl_compositor_interface compositor_impl = {
    .create_surface = comp_create_surface,
    .create_region  = comp_create_region,
};

static void compositor_bind(struct wl_client *client, void *data,
                            uint32_t version, uint32_t id)
{
    (void)data;
    struct wl_resource *res = wl_resource_create(
        client, &wl_compositor_interface, version, id);
    if (!res) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(res, &compositor_impl, NULL, NULL);
}

/* ══════════════════════════════════════════════════════════════════════
 *  xdg_wm_base / xdg_surface / xdg_toplevel
 * ══════════════════════════════════════════════════════════════════════ */

/* xdg_toplevel */

static void toplevel_destroy(struct wl_client *client, struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void toplevel_set_parent(struct wl_client *c, struct wl_resource *r,
                                struct wl_resource *p)
{
    (void)c; (void)r; (void)p;
}

static void toplevel_set_title(struct wl_client *c, struct wl_resource *r,
                               const char *title)
{
    (void)c;
    struct wlcomp_surface *surf = wl_resource_get_user_data(r);
    if (surf && title) {
        int i;
        for (i = 0; i < 63 && title[i]; i++)
            surf->title[i] = title[i];
        surf->title[i] = '\0';
    }
    fprintf(stderr, "wlcomp: client title: %s\n", title);
}

static void toplevel_set_app_id(struct wl_client *c, struct wl_resource *r,
                                const char *app_id)
{
    (void)c;
    struct wlcomp_surface *surf = wl_resource_get_user_data(r);
    if (surf && app_id) {
        int i;
        for (i = 0; i < 63 && app_id[i]; i++)
            surf->app_id[i] = app_id[i];
        surf->app_id[i] = '\0';
    }
    fprintf(stderr, "wlcomp: client app_id: %s\n", app_id);
}

static void toplevel_show_window_menu(struct wl_client *c, struct wl_resource *r,
                                      struct wl_resource *seat, uint32_t serial,
                                      int32_t x, int32_t y)
{
    (void)c; (void)r; (void)seat; (void)serial; (void)x; (void)y;
}

static void toplevel_move(struct wl_client *c, struct wl_resource *r,
                          struct wl_resource *seat, uint32_t serial)
{
    (void)c; (void)seat; (void)serial;
    struct wlcomp_surface *surf = wl_resource_get_user_data(r);
    if (!surf || surf->maximized) return;
    g_grab_surface = surf;
    g_grab_mode = 1; /* move */
    g_grab_start_mx = g_cursor_x;
    g_grab_start_my = g_cursor_y;
    g_grab_start_x = surf->x;
    g_grab_start_y = surf->y;
}

static void toplevel_resize(struct wl_client *c, struct wl_resource *r,
                            struct wl_resource *seat, uint32_t serial,
                            uint32_t edges)
{
    (void)c; (void)seat; (void)serial;
    struct wlcomp_surface *surf = wl_resource_get_user_data(r);
    if (!surf || surf->maximized) return;
    g_grab_surface = surf;
    g_grab_mode = 2; /* resize */
    g_grab_edges = edges;
    g_grab_start_mx = g_cursor_x;
    g_grab_start_my = g_cursor_y;
    g_grab_start_x = surf->x;
    g_grab_start_y = surf->y;
    g_grab_start_w = surf->committed_buf ? surf->committed_buf->width : 200;
    g_grab_start_h = surf->committed_buf ? surf->committed_buf->height : 200;
}

static void toplevel_set_max_size(struct wl_client *c, struct wl_resource *r,
                                  int32_t w, int32_t h)
{
    (void)c; (void)r; (void)w; (void)h;
}

static void toplevel_set_min_size(struct wl_client *c, struct wl_resource *r,
                                  int32_t w, int32_t h)
{
    (void)c; (void)r; (void)w; (void)h;
}

static void toplevel_set_maximized(struct wl_client *c, struct wl_resource *r)
{
    (void)c;
    struct wlcomp_surface *surf = wl_resource_get_user_data(r);
    if (!surf || !surf->xdg_surface || !surf->xdg_toplevel) return;
    if (surf->maximized) return;  /* already maximized */

    /* Save current geometry for restore */
    surf->saved_x = surf->x;
    surf->saved_y = surf->y;
    surf->saved_w = surf->committed_buf ? surf->committed_buf->width : 400;
    surf->saved_h = surf->committed_buf ? surf->committed_buf->height : 300;
    surf->maximized = 1;

    /* Send configure with MAXIMIZED + ACTIVATED, full screen minus taskbar */
    struct wl_array states;
    wl_array_init(&states);
    uint32_t *s;
    s = wl_array_add(&states, sizeof(uint32_t));
    *s = XDG_TOPLEVEL_STATE_MAXIMIZED;
    s = wl_array_add(&states, sizeof(uint32_t));
    *s = XDG_TOPLEVEL_STATE_ACTIVATED;
    int32_t max_h = (int32_t)g_fb_h - 36;
    xdg_toplevel_send_configure(surf->xdg_toplevel, (int32_t)g_fb_w, max_h, &states);
    wl_array_release(&states);
    xdg_surface_send_configure(surf->xdg_surface, ++g_serial);

    /* Snap to top-left */
    surf->x = 0;
    surf->y = 0;
}

static void toplevel_unset_maximized(struct wl_client *c, struct wl_resource *r)
{
    (void)c;
    struct wlcomp_surface *surf = wl_resource_get_user_data(r);
    if (!surf || !surf->xdg_surface || !surf->xdg_toplevel) return;
    if (!surf->maximized) return;  /* not maximized */

    surf->maximized = 0;

    /* Restore saved geometry */
    int32_t rw = surf->saved_w > 0 ? surf->saved_w : (int32_t)g_fb_w * 4 / 5;
    int32_t rh = surf->saved_h > 0 ? surf->saved_h : ((int32_t)g_fb_h - 36) * 4 / 5;
    surf->x = surf->saved_x;
    surf->y = surf->saved_y;

    struct wl_array states;
    wl_array_init(&states);
    uint32_t *s;
    s = wl_array_add(&states, sizeof(uint32_t));
    *s = XDG_TOPLEVEL_STATE_ACTIVATED;
    xdg_toplevel_send_configure(surf->xdg_toplevel, rw, rh, &states);
    wl_array_release(&states);
    xdg_surface_send_configure(surf->xdg_surface, ++g_serial);
}

static void toplevel_set_fullscreen(struct wl_client *c, struct wl_resource *r,
                                    struct wl_resource *output)
{
    (void)c; (void)output;
    /* Treat fullscreen same as maximize for now */
    toplevel_set_maximized(c, r);
}

static void toplevel_unset_fullscreen(struct wl_client *c, struct wl_resource *r)
{
    toplevel_unset_maximized(c, r);
}

static void toplevel_set_minimized(struct wl_client *c, struct wl_resource *r)
{
    (void)c;
    struct wlcomp_surface *surf = wl_resource_get_user_data(r);
    if (surf) {
        surf->minimized = 1;
        if (g_focused == surf)
            g_focused = NULL;
        if (g_kbd_focused == surf)
            g_kbd_focused = NULL;
    }
}

static const struct xdg_toplevel_interface toplevel_impl = {
    .destroy          = toplevel_destroy,
    .set_parent       = toplevel_set_parent,
    .set_title        = toplevel_set_title,
    .set_app_id       = toplevel_set_app_id,
    .show_window_menu = toplevel_show_window_menu,
    .move             = toplevel_move,
    .resize           = toplevel_resize,
    .set_max_size     = toplevel_set_max_size,
    .set_min_size     = toplevel_set_min_size,
    .set_maximized    = toplevel_set_maximized,
    .unset_maximized  = toplevel_unset_maximized,
    .set_fullscreen   = toplevel_set_fullscreen,
    .unset_fullscreen = toplevel_unset_fullscreen,
    .set_minimized    = toplevel_set_minimized,
};

static void toplevel_destroy_handler(struct wl_resource *resource)
{
    struct wlcomp_surface *surf = wl_resource_get_user_data(resource);
    if (surf) {
        surf->xdg_toplevel = NULL;
        surf->mapped = 0;
    }
}

/* xdg_surface */

static void xdg_surface_destroy(struct wl_client *c, struct wl_resource *r)
{
    (void)c;
    wl_resource_destroy(r);
}

static void xdg_surface_get_toplevel(struct wl_client *client,
                                     struct wl_resource *resource,
                                     uint32_t id)
{
    struct wlcomp_surface *surf = wl_resource_get_user_data(resource);
    struct wl_resource *tl = wl_resource_create(
        client, &xdg_toplevel_interface,
        wl_resource_get_version(resource), id);
    if (!tl) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(tl, &toplevel_impl, surf,
                                  toplevel_destroy_handler);
    if (surf)
        surf->xdg_toplevel = tl;

    /* Send configure: normal windowed size (not maximized) */
    struct wl_array states;
    wl_array_init(&states);
    uint32_t *s;
    s = wl_array_add(&states, sizeof(uint32_t));
    *s = XDG_TOPLEVEL_STATE_ACTIVATED;
    /* Default to 80% of screen, capped to leave room for taskbar */
    int32_t def_w = (int32_t)g_fb_w * 4 / 5;
    int32_t def_h = ((int32_t)g_fb_h - 36) * 4 / 5;
    if (def_w < 400) def_w = (int32_t)g_fb_w;
    if (def_h < 300) def_h = (int32_t)g_fb_h - 36;
    if (surf) {
        surf->saved_w = def_w;
        surf->saved_h = def_h;
        surf->saved_x = ((int32_t)g_fb_w - def_w) / 2;
        surf->saved_y = ((int32_t)g_fb_h - 36 - def_h) / 2;
    }
    xdg_toplevel_send_configure(tl, def_w, def_h, &states);
    wl_array_release(&states);
    xdg_surface_send_configure(resource, ++g_serial);
}

/* xdg_popup — minimal implementation to avoid NULL proxy on client side */

static void popup_destroy(struct wl_client *c, struct wl_resource *r)
{
    (void)c;
    wl_resource_destroy(r);
}

static void popup_grab(struct wl_client *c, struct wl_resource *r,
                       struct wl_resource *seat, uint32_t serial)
{
    (void)c; (void)r; (void)seat; (void)serial;
}

static void popup_reposition(struct wl_client *c, struct wl_resource *r,
                             struct wl_resource *positioner, uint32_t token)
{
    (void)c; (void)r; (void)positioner; (void)token;
}

static const struct xdg_popup_interface popup_impl = {
    .destroy    = popup_destroy,
    .grab       = popup_grab,
    .reposition = popup_reposition,
};

static void popup_destroy_handler(struct wl_resource *resource)
{
    (void)resource;
}

static void xdg_surface_get_popup(struct wl_client *c, struct wl_resource *r,
                                  uint32_t id, struct wl_resource *parent,
                                  struct wl_resource *positioner)
{
    (void)parent; (void)positioner;
    struct wl_resource *popup = wl_resource_create(
        c, &xdg_popup_interface, wl_resource_get_version(r), id);
    if (!popup) {
        wl_client_post_no_memory(c);
        return;
    }
    wl_resource_set_implementation(popup, &popup_impl, NULL,
                                  popup_destroy_handler);
    /* Send a configure event placing the popup at (0,0) with a small size */
    xdg_popup_send_configure(popup, 0, 0, 1, 1);
    xdg_surface_send_configure(r, ++g_serial);
}

static void xdg_surface_set_window_geometry(struct wl_client *c,
                                            struct wl_resource *r,
                                            int32_t x, int32_t y,
                                            int32_t w, int32_t h)
{
    (void)c; (void)r; (void)x; (void)y; (void)w; (void)h;
}

static void xdg_surface_ack_configure(struct wl_client *c,
                                      struct wl_resource *r,
                                      uint32_t serial)
{
    (void)c; (void)r; (void)serial;
}

static const struct xdg_surface_interface xdg_surface_impl = {
    .destroy             = xdg_surface_destroy,
    .get_toplevel        = xdg_surface_get_toplevel,
    .get_popup           = xdg_surface_get_popup,
    .set_window_geometry = xdg_surface_set_window_geometry,
    .ack_configure       = xdg_surface_ack_configure,
};

static void xdg_surface_destroy_handler(struct wl_resource *resource)
{
    struct wlcomp_surface *surf = wl_resource_get_user_data(resource);
    if (surf)
        surf->xdg_surface = NULL;
}

/* xdg_wm_base */

static void xdg_wm_destroy(struct wl_client *c, struct wl_resource *r)
{
    (void)c;
    wl_resource_destroy(r);
}

/* xdg_positioner — minimal stub (stores nothing, used for popup placement) */

static void pos_destroy(struct wl_client *c, struct wl_resource *r)
{ (void)c; wl_resource_destroy(r); }
static void pos_set_size(struct wl_client *c, struct wl_resource *r,
                         int32_t w, int32_t h)
{ (void)c; (void)r; (void)w; (void)h; }
static void pos_set_anchor_rect(struct wl_client *c, struct wl_resource *r,
                                int32_t x, int32_t y, int32_t w, int32_t h)
{ (void)c; (void)r; (void)x; (void)y; (void)w; (void)h; }
static void pos_set_anchor(struct wl_client *c, struct wl_resource *r, uint32_t a)
{ (void)c; (void)r; (void)a; }
static void pos_set_gravity(struct wl_client *c, struct wl_resource *r, uint32_t g)
{ (void)c; (void)r; (void)g; }
static void pos_set_constraint(struct wl_client *c, struct wl_resource *r, uint32_t ca)
{ (void)c; (void)r; (void)ca; }
static void pos_set_offset(struct wl_client *c, struct wl_resource *r,
                           int32_t x, int32_t y)
{ (void)c; (void)r; (void)x; (void)y; }
static void pos_set_reactive(struct wl_client *c, struct wl_resource *r)
{ (void)c; (void)r; }
static void pos_set_parent_size(struct wl_client *c, struct wl_resource *r,
                                int32_t w, int32_t h)
{ (void)c; (void)r; (void)w; (void)h; }
static void pos_set_parent_configure(struct wl_client *c, struct wl_resource *r,
                                     uint32_t serial)
{ (void)c; (void)r; (void)serial; }

static const struct xdg_positioner_interface positioner_impl = {
    .destroy                  = pos_destroy,
    .set_size                 = pos_set_size,
    .set_anchor_rect          = pos_set_anchor_rect,
    .set_anchor               = pos_set_anchor,
    .set_gravity              = pos_set_gravity,
    .set_constraint_adjustment = pos_set_constraint,
    .set_offset               = pos_set_offset,
    .set_reactive             = pos_set_reactive,
    .set_parent_size          = pos_set_parent_size,
    .set_parent_configure     = pos_set_parent_configure,
};

static void xdg_wm_create_positioner(struct wl_client *c, struct wl_resource *r,
                                     uint32_t id)
{
    struct wl_resource *pos = wl_resource_create(
        c, &xdg_positioner_interface, wl_resource_get_version(r), id);
    if (!pos) {
        wl_client_post_no_memory(c);
        return;
    }
    wl_resource_set_implementation(pos, &positioner_impl, NULL, NULL);
}

static void xdg_wm_get_xdg_surface(struct wl_client *client,
                                    struct wl_resource *resource,
                                    uint32_t id,
                                    struct wl_resource *surface_resource)
{
    struct wlcomp_surface *surf = surface_from_resource(surface_resource);
    struct wl_resource *xdg_res = wl_resource_create(
        client, &xdg_surface_interface,
        wl_resource_get_version(resource), id);
    if (!xdg_res) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(xdg_res, &xdg_surface_impl, surf,
                                  xdg_surface_destroy_handler);
    if (surf)
        surf->xdg_surface = xdg_res;
}

static void xdg_wm_pong(struct wl_client *c, struct wl_resource *r,
                         uint32_t serial)
{
    (void)c; (void)r; (void)serial;
}

static const struct xdg_wm_base_interface xdg_wm_impl = {
    .destroy           = xdg_wm_destroy,
    .create_positioner = xdg_wm_create_positioner,
    .get_xdg_surface   = xdg_wm_get_xdg_surface,
    .pong              = xdg_wm_pong,
};

static void xdg_wm_bind(struct wl_client *client, void *data,
                         uint32_t version, uint32_t id)
{
    (void)data;
    struct wl_resource *res = wl_resource_create(
        client, &xdg_wm_base_interface, version, id);
    if (!res) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(res, &xdg_wm_impl, NULL, NULL);
}

/* ══════════════════════════════════════════════════════════════════════
 *  wl_data_device_manager — stub for clipboard/DnD (required by GTK3)
 * ══════════════════════════════════════════════════════════════════════ */

/* wl_data_source (no-op) */
static void ds_offer(struct wl_client *c, struct wl_resource *r, const char *m)
{ (void)c; (void)r; (void)m; }
static void ds_destroy(struct wl_client *c, struct wl_resource *r)
{ (void)c; wl_resource_destroy(r); }
static void ds_set_actions(struct wl_client *c, struct wl_resource *r, uint32_t a)
{ (void)c; (void)r; (void)a; }

static const struct wl_data_source_interface data_source_impl = {
    .offer       = ds_offer,
    .destroy     = ds_destroy,
    .set_actions = ds_set_actions,
};

/* wl_data_device (no-op) */
static void dd_start_drag(struct wl_client *c, struct wl_resource *r,
                          struct wl_resource *source, struct wl_resource *origin,
                          struct wl_resource *icon, uint32_t serial)
{ (void)c; (void)r; (void)source; (void)origin; (void)icon; (void)serial; }
static void dd_set_selection(struct wl_client *c, struct wl_resource *r,
                             struct wl_resource *source, uint32_t serial)
{ (void)c; (void)r; (void)source; (void)serial; }
static void dd_release(struct wl_client *c, struct wl_resource *r)
{ (void)c; wl_resource_destroy(r); }

static const struct wl_data_device_interface data_device_impl = {
    .start_drag    = dd_start_drag,
    .set_selection = dd_set_selection,
    .release       = dd_release,
};

/* wl_data_device_manager */
static void ddm_create_data_source(struct wl_client *client,
                                   struct wl_resource *resource,
                                   uint32_t id)
{
    struct wl_resource *res = wl_resource_create(
        client, &wl_data_source_interface,
        wl_resource_get_version(resource), id);
    if (!res) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(res, &data_source_impl, NULL, NULL);
}

static void ddm_get_data_device(struct wl_client *client,
                                struct wl_resource *resource,
                                uint32_t id,
                                struct wl_resource *seat)
{
    (void)seat;
    struct wl_resource *res = wl_resource_create(
        client, &wl_data_device_interface,
        wl_resource_get_version(resource), id);
    if (!res) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(res, &data_device_impl, NULL, NULL);
}

static const struct wl_data_device_manager_interface ddm_impl = {
    .create_data_source = ddm_create_data_source,
    .get_data_device    = ddm_get_data_device,
};

static void ddm_bind(struct wl_client *client, void *data,
                     uint32_t version, uint32_t id)
{
    (void)data;
    struct wl_resource *res = wl_resource_create(
        client, &wl_data_device_manager_interface, version, id);
    if (!res) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(res, &ddm_impl, NULL, NULL);
}

static struct wl_global *g_ddm_global;

/* ══════════════════════════════════════════════════════════════════════
 *  wl_seat (pointer + keyboard capability advertisement)
 * ══════════════════════════════════════════════════════════════════════ */

#define MAX_INPUT_RES 8
static struct wl_resource *g_pointer_resources[MAX_INPUT_RES];
static struct wl_resource *g_keyboard_resources[MAX_INPUT_RES];

static struct wl_resource *find_resource_for_client(
    struct wl_resource **arr, int n, struct wl_client *client)
{
    for (int i = 0; i < n; i++)
        if (arr[i] && wl_resource_get_client(arr[i]) == client)
            return arr[i];
    return NULL;
}

static void add_resource(struct wl_resource **arr, int n,
                         struct wl_resource *res)
{
    for (int i = 0; i < n; i++) {
        if (!arr[i]) { arr[i] = res; return; }
    }
}

static void remove_resource(struct wl_resource **arr, int n,
                            struct wl_resource *res)
{
    for (int i = 0; i < n; i++) {
        if (arr[i] == res) { arr[i] = NULL; return; }
    }
}

/* wl_pointer */

static void pointer_set_cursor(struct wl_client *c, struct wl_resource *r,
                               uint32_t serial, struct wl_resource *surface,
                               int32_t hotspot_x, int32_t hotspot_y)
{
    (void)c; (void)r; (void)serial;
    if (surface) {
        struct wlcomp_surface *surf = surface_from_resource(surface);
        if (surf) {
            surf->is_cursor = 1;
            g_cursor_surface = surf;
            g_cursor_hotspot_x = hotspot_x;
            g_cursor_hotspot_y = hotspot_y;
        }
    } else {
        /* NULL surface = use default cursor */
        if (g_cursor_surface) {
            g_cursor_surface->is_cursor = 0;
            g_cursor_surface = NULL;
        }
    }
}

static void pointer_release(struct wl_client *c, struct wl_resource *r)
{
    (void)c;
    wl_resource_destroy(r);
}

static const struct wl_pointer_interface pointer_impl = {
    .set_cursor = pointer_set_cursor,
    .release    = pointer_release,
};

static void pointer_destroy_handler(struct wl_resource *resource)
{
    remove_resource(g_pointer_resources, MAX_INPUT_RES, resource);
}

/* wl_keyboard */

static void keyboard_release(struct wl_client *c, struct wl_resource *r)
{
    (void)c;
    wl_resource_destroy(r);
}

static const struct wl_keyboard_interface keyboard_impl = {
    .release = keyboard_release,
};

static void keyboard_destroy_handler(struct wl_resource *resource)
{
    remove_resource(g_keyboard_resources, MAX_INPUT_RES, resource);
}

/* wl_seat */

static void seat_get_pointer(struct wl_client *client, struct wl_resource *resource,
                             uint32_t id)
{
    struct wl_resource *res = wl_resource_create(
        client, &wl_pointer_interface,
        wl_resource_get_version(resource), id);
    if (!res) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(res, &pointer_impl, NULL,
                                  pointer_destroy_handler);
    add_resource(g_pointer_resources, MAX_INPUT_RES, res);
}

static void seat_get_keyboard(struct wl_client *client,
                              struct wl_resource *resource,
                              uint32_t id)
{
    struct wl_resource *res = wl_resource_create(
        client, &wl_keyboard_interface,
        wl_resource_get_version(resource), id);
    if (!res) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(res, &keyboard_impl, NULL,
                                  keyboard_destroy_handler);
    add_resource(g_keyboard_resources, MAX_INPUT_RES, res);

    /* Send a full US-QWERTY XKB keymap via temp file fd.
     * GTK3/libxkbcommon requires a real fd it can mmap.
     * Keycodes = PS/2 set1 scancode + 8 (evdev convention). */
    static const char us_keymap[] =
        "xkb_keymap {\n"
        "  xkb_keycodes \"ps2\" {\n"
        "    minimum = 8; maximum = 255;\n"
        "    <ESC>  = 9;\n"
        "    <AE01> = 10; <AE02> = 11; <AE03> = 12; <AE04> = 13;\n"
        "    <AE05> = 14; <AE06> = 15; <AE07> = 16; <AE08> = 17;\n"
        "    <AE09> = 18; <AE10> = 19; <AE11> = 20; <AE12> = 21;\n"
        "    <BKSP> = 22;\n"
        "    <TAB>  = 23;\n"
        "    <AD01> = 24; <AD02> = 25; <AD03> = 26; <AD04> = 27;\n"
        "    <AD05> = 28; <AD06> = 29; <AD07> = 30; <AD08> = 31;\n"
        "    <AD09> = 32; <AD10> = 33; <AD11> = 34; <AD12> = 35;\n"
        "    <RTRN> = 36;\n"
        "    <LCTL> = 37;\n"
        "    <AC01> = 38; <AC02> = 39; <AC03> = 40; <AC04> = 41;\n"
        "    <AC05> = 42; <AC06> = 43; <AC07> = 44; <AC08> = 45;\n"
        "    <AC09> = 46; <AC10> = 47; <AC11> = 48;\n"
        "    <TLDE> = 49;\n"
        "    <LFSH> = 50;\n"
        "    <BKSL> = 51;\n"
        "    <AB01> = 52; <AB02> = 53; <AB03> = 54; <AB04> = 55;\n"
        "    <AB05> = 56; <AB06> = 57; <AB07> = 58; <AB08> = 59;\n"
        "    <AB09> = 60; <AB10> = 61;\n"
        "    <RTSH> = 62;\n"
        "    <KPMU> = 63;\n"
        "    <LALT> = 64;\n"
        "    <SPCE> = 65;\n"
        "    <CAPS> = 66;\n"
        "    <FK01> = 67; <FK02> = 68; <FK03> = 69; <FK04> = 70;\n"
        "    <FK05> = 71; <FK06> = 72; <FK07> = 73; <FK08> = 74;\n"
        "    <FK09> = 75; <FK10> = 76;\n"
        "    <UP>   = 111; <DOWN> = 116; <LEFT> = 113; <RGHT> = 114;\n"
        "    <HOME> = 110; <END>  = 115; <PGUP> = 112; <PGDN> = 117;\n"
        "    <INS>  = 118; <DELE> = 119;\n"
        "  };\n"
        "  xkb_types \"basic\" {\n"
        "    type \"ONE_LEVEL\" {\n"
        "      modifiers = none;\n"
        "      map[none] = Level1;\n"
        "      level_name[Level1] = \"Any\";\n"
        "    };\n"
        "    type \"TWO_LEVEL\" {\n"
        "      modifiers = Shift;\n"
        "      map[none]  = Level1;\n"
        "      map[Shift] = Level2;\n"
        "      level_name[Level1] = \"Base\";\n"
        "      level_name[Level2] = \"Shift\";\n"
        "    };\n"
        "    type \"ALPHABETIC\" {\n"
        "      modifiers = Shift+Lock;\n"
        "      map[none]       = Level1;\n"
        "      map[Shift]      = Level2;\n"
        "      map[Lock]       = Level2;\n"
        "      map[Shift+Lock] = Level1;\n"
        "      level_name[Level1] = \"Base\";\n"
        "      level_name[Level2] = \"Caps\";\n"
        "    };\n"
        "  };\n"
        "  xkb_compatibility \"basic\" {\n"
        "    interpret Shift_L   { action = SetMods(modifiers=Shift); };\n"
        "    interpret Shift_R   { action = SetMods(modifiers=Shift); };\n"
        "    interpret Control_L { action = SetMods(modifiers=Control); };\n"
        "    interpret Alt_L     { action = SetMods(modifiers=Mod1); };\n"
        "    interpret Caps_Lock { action = LockMods(modifiers=Lock); };\n"
        "  };\n"
        "  xkb_symbols \"us\" {\n"
        "    key <ESC>  { [ Escape ] };\n"
        "    key <AE01> { [ 1, exclam      ] };\n"
        "    key <AE02> { [ 2, at           ] };\n"
        "    key <AE03> { [ 3, numbersign   ] };\n"
        "    key <AE04> { [ 4, dollar       ] };\n"
        "    key <AE05> { [ 5, percent      ] };\n"
        "    key <AE06> { [ 6, asciicircum  ] };\n"
        "    key <AE07> { [ 7, ampersand    ] };\n"
        "    key <AE08> { [ 8, asterisk     ] };\n"
        "    key <AE09> { [ 9, parenleft    ] };\n"
        "    key <AE10> { [ 0, parenright   ] };\n"
        "    key <AE11> { [ minus, underscore ] };\n"
        "    key <AE12> { [ equal, plus     ] };\n"
        "    key <BKSP> { [ BackSpace       ] };\n"
        "    key <TAB>  { [ Tab             ] };\n"
        "    key <AD01> { [ q, Q ] }; key <AD02> { [ w, W ] };\n"
        "    key <AD03> { [ e, E ] }; key <AD04> { [ r, R ] };\n"
        "    key <AD05> { [ t, T ] }; key <AD06> { [ y, Y ] };\n"
        "    key <AD07> { [ u, U ] }; key <AD08> { [ i, I ] };\n"
        "    key <AD09> { [ o, O ] }; key <AD10> { [ p, P ] };\n"
        "    key <AD11> { [ bracketleft, braceleft   ] };\n"
        "    key <AD12> { [ bracketright, braceright ] };\n"
        "    key <RTRN> { [ Return ] };\n"
        "    key <LCTL> { [ Control_L ] };\n"
        "    key <AC01> { [ a, A ] }; key <AC02> { [ s, S ] };\n"
        "    key <AC03> { [ d, D ] }; key <AC04> { [ f, F ] };\n"
        "    key <AC05> { [ g, G ] }; key <AC06> { [ h, H ] };\n"
        "    key <AC07> { [ j, J ] }; key <AC08> { [ k, K ] };\n"
        "    key <AC09> { [ l, L ] };\n"
        "    key <AC10> { [ semicolon, colon    ] };\n"
        "    key <AC11> { [ apostrophe, quotedbl ] };\n"
        "    key <TLDE> { [ grave, asciitilde    ] };\n"
        "    key <LFSH> { [ Shift_L   ] };\n"
        "    key <BKSL> { [ backslash, bar ] };\n"
        "    key <AB01> { [ z, Z ] }; key <AB02> { [ x, X ] };\n"
        "    key <AB03> { [ c, C ] }; key <AB04> { [ v, V ] };\n"
        "    key <AB05> { [ b, B ] }; key <AB06> { [ n, N ] };\n"
        "    key <AB07> { [ m, M ] };\n"
        "    key <AB08> { [ comma, less     ] };\n"
        "    key <AB09> { [ period, greater ] };\n"
        "    key <AB10> { [ slash, question ] };\n"
        "    key <RTSH> { [ Shift_R   ] };\n"
        "    key <LALT> { [ Alt_L     ] };\n"
        "    key <SPCE> { [ space     ] };\n"
        "    key <CAPS> { [ Caps_Lock ] };\n"
        "    key <FK01> { [ F1  ] }; key <FK02> { [ F2  ] };\n"
        "    key <FK03> { [ F3  ] }; key <FK04> { [ F4  ] };\n"
        "    key <FK05> { [ F5  ] }; key <FK06> { [ F6  ] };\n"
        "    key <FK07> { [ F7  ] }; key <FK08> { [ F8  ] };\n"
        "    key <FK09> { [ F9  ] }; key <FK10> { [ F10 ] };\n"
        "    key <UP>   { [ Up    ] }; key <DOWN> { [ Down  ] };\n"
        "    key <LEFT> { [ Left  ] }; key <RGHT> { [ Right ] };\n"
        "    key <HOME> { [ Home  ] }; key <END>  { [ End   ] };\n"
        "    key <PGUP> { [ Prior ] }; key <PGDN> { [ Next  ] };\n"
        "    key <INS>  { [ Insert ] }; key <DELE> { [ Delete ] };\n"
        "    modifier_map Shift   { <LFSH>, <RTSH> };\n"
        "    modifier_map Control { <LCTL> };\n"
        "    modifier_map Mod1    { <LALT> };\n"
        "    modifier_map Lock    { <CAPS> };\n"
        "  };\n"
        "};\n";
    size_t keymap_size = sizeof(us_keymap);  /* includes NUL */

    int km_fd = open("/tmp/.wlcomp_keymap", O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (km_fd >= 0) {
        write(km_fd, us_keymap, keymap_size);
        lseek(km_fd, 0, SEEK_SET);
        wl_keyboard_send_keymap(res, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
                                km_fd, keymap_size);
        close(km_fd);
    }

    /* Send repeat info: 25 keys/sec, 400ms delay */
    wl_keyboard_send_repeat_info(res, 25, 400);
}

static void seat_get_touch(struct wl_client *c, struct wl_resource *r,
                           uint32_t id)
{
    (void)c; (void)r; (void)id;
}

static void seat_release(struct wl_client *c, struct wl_resource *r)
{
    (void)c;
    wl_resource_destroy(r);
}

static const struct wl_seat_interface seat_impl = {
    .get_pointer  = seat_get_pointer,
    .get_keyboard = seat_get_keyboard,
    .get_touch    = seat_get_touch,
    .release      = seat_release,
};

static void seat_bind(struct wl_client *client, void *data,
                      uint32_t version, uint32_t id)
{
    (void)data;
    struct wl_resource *res = wl_resource_create(
        client, &wl_seat_interface, version, id);
    if (!res) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(res, &seat_impl, NULL, NULL);

    uint32_t caps = WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD;
    wl_seat_send_capabilities(res, caps);
    if (version >= 2)
        wl_seat_send_name(res, "seat0");
}

/* ══════════════════════════════════════════════════════════════════════
 *  wl_output
 * ══════════════════════════════════════════════════════════════════════ */

static void output_release(struct wl_client *c, struct wl_resource *r)
{
    (void)c;
    wl_resource_destroy(r);
}

static const struct wl_output_interface output_impl = {
    .release = output_release,
};

static void output_bind(struct wl_client *client, void *data,
                        uint32_t version, uint32_t id)
{
    (void)data;
    struct wl_resource *res = wl_resource_create(
        client, &wl_output_interface, version, id);
    if (!res) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(res, &output_impl, NULL, NULL);

    wl_output_send_geometry(res, 0, 0, 0, 0,
                            WL_OUTPUT_SUBPIXEL_UNKNOWN,
                            "xv6", "BochsVGA",
                            WL_OUTPUT_TRANSFORM_NORMAL);
    wl_output_send_mode(res, WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED,
                        (int32_t)g_fb_w, (int32_t)g_fb_h, 60000);
    if (version >= 2)
        wl_output_send_done(res);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Compositing / rendering
 * ══════════════════════════════════════════════════════════════════════ */

/* ── Minimal 8×16 bitmap font (printable ASCII 0x20–0x7E) ────────── */
#include "font8x16.h"

static void draw_char(uint32_t *fb, int fb_w, int fb_h,
                      int x, int y, char ch, uint32_t color, int scale)
{
    if (ch < 0x20 || ch > 0x7E) ch = '?';
    const uint8_t *glyph = font8x16_data[(int)(ch - 0x20)];
    for (int row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++) {
                        int px = x + col * scale + sx;
                        int py = y + row * scale + sy;
                        if (px >= 0 && px < fb_w && py >= 0 && py < fb_h)
                            fb[py * fb_w + px] = color;
                    }
            }
        }
    }
}

static void draw_string(uint32_t *fb, int fb_w, int fb_h,
                        int x, int y, const char *s, uint32_t color, int scale)
{
    for (; *s; s++, x += 8 * scale)
        draw_char(fb, fb_w, fb_h, x, y, *s, color, scale);
}

static int string_pixel_width(const char *s, int scale)
{
    int len = 0;
    while (*s++) len++;
    return len * 8 * scale;
}

static void draw_rect(uint32_t *fb, int fb_w, int fb_h,
                      int x, int y, int w, int h, uint32_t color)
{
    for (int row = y; row < y + h && row < fb_h; row++) {
        if (row < 0) continue;
        for (int col = x; col < x + w && col < fb_w; col++) {
            if (col < 0) continue;
            fb[row * fb_w + col] = color;
        }
    }
}

static void draw_rounded_rect(uint32_t *fb, int fb_w, int fb_h,
                               int x, int y, int w, int h, int r,
                               uint32_t color)
{
    draw_rect(fb, fb_w, fb_h, x + r, y, w - 2 * r, h, color);
    draw_rect(fb, fb_w, fb_h, x, y + r, w, h - 2 * r, color);
    for (int cy = 0; cy <= r; cy++) {
        for (int cx = 0; cx <= r; cx++) {
            if (cx * cx + cy * cy <= r * r) {
                int px, py;
                px = x + r - cx; py = y + r - cy;
                if (px >= 0 && px < fb_w && py >= 0 && py < fb_h)
                    fb[py * fb_w + px] = color;
                px = x + w - r - 1 + cx; py = y + r - cy;
                if (px >= 0 && px < fb_w && py >= 0 && py < fb_h)
                    fb[py * fb_w + px] = color;
                px = x + r - cx; py = y + h - r - 1 + cy;
                if (px >= 0 && px < fb_w && py >= 0 && py < fb_h)
                    fb[py * fb_w + px] = color;
                px = x + w - r - 1 + cx; py = y + h - r - 1 + cy;
                if (px >= 0 && px < fb_w && py >= 0 && py < fb_h)
                    fb[py * fb_w + px] = color;
            }
        }
    }
}

static void draw_circle(uint32_t *fb, int fb_w, int fb_h,
                        int cx, int cy, int r, uint32_t color)
{
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r) {
                int px = cx + dx, py = cy + dy;
                if (px >= 0 && px < fb_w && py >= 0 && py < fb_h)
                    fb[py * fb_w + px] = color;
            }
}

/* ── Desktop shortcut definitions ──────────────────────────────────── */

enum shortcut_action {
    SHORTCUT_EXEC = 0,
    SHORTCUT_TERMINAL,
    SHORTCUT_FILES,
    SHORTCUT_SYSINFO,
    SHORTCUT_CALC,
    SHORTCUT_NETWORK,
    SHORTCUT_SETTINGS,
    SHORTCUT_MONITOR,
    SHORTCUT_3DDEMO,
    SHORTCUT_EDITOR,
};

typedef struct {
    char        label[48];
    char        exec_path[256];
    char        exec_name[64];
    char        exec_arg[256];
    int         action;
    uint32_t    icon_color;
    char        symbol;       /* single char rendered as icon */
    int         x, y, w, h;    /* computed layout */
} desktop_icon_t;

#define DESKTOP_DIR        "/root/Desktop"
#define DESKTOP_ICON_MAX   32
#define ICON_CELL_W        80
#define ICON_CELL_H        72
#define ICON_BOX_SIZE      42
#define ICON_GRID_X0       20
#define ICON_GRID_Y0       16
#define APP_AREA_TOP       (ICON_GRID_Y0 + ICON_CELL_H)  /* 88: below icon row */

static desktop_icon_t g_icons[DESKTOP_ICON_MAX];
static int g_icon_count;
static int g_shortcuts_loaded;
static int g_icons_laid_out;
static int g_selected_icon = -1;       /* currently selected desktop icon */

/* ── Taskbar button definitions ────────────────────────────────────── */

#define TASKBAR_H          36
#define TB_BTN_H           28
#define TB_BTN_PAD         4

/* ── Child process management ──────────────────────────────────────── */

#define MAX_CHILDREN 16
static pid_t g_children[MAX_CHILDREN];

static void launch_desktop_app_arg(const char *path, const char *name,
                                   const char *arg)
{
    if (!path) return;

    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < MAX_CHILDREN; i++) {
        if (g_children[i] <= 0) { slot = i; break; }
    }
    if (slot < 0) return;  /* all slots full */

    pid_t pid = fork();
    if (pid < 0) return;

    if (pid == 0) {
        /* Child: redirect stderr to a log file for debugging */
        int logfd = open("/tmp/app_log.txt",
                         O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (logfd >= 0) {
            dup2(logfd, 1);  /* stdout */
            dup2(logfd, 2);  /* stderr */
            close(logfd);
        }

        /* Child: set up Wayland environment and exec */

        int is_netsurf = strcmp(name, "netsurf") == 0;
        int is_minibrowser = strcmp(name, "MiniBrowser") == 0;

        /* For netsurf, create Choices file with ca_bundle + homepage */
        if (is_netsurf) {
            mkdir("/.netsurf", 0755);
            mkdir("/tmp/.cache", 0755);
            mkdir("/tmp/.cache/fontconfig", 0755);
            int fd = open("/.netsurf/Choices",
                          O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd >= 0) {
                const char *ch =
                    "ca_bundle:/share/netsurf/ca-bundle\n"
                    "homepage_url:file:///share/netsurf/welcome.html\n"
                    "curl_fetch_timeout:30\n";
                write(fd, ch, strlen(ch));
                close(fd);
            }
        }

        if (is_minibrowser) {
            /* Wait for Flask web server on port 80 before launching. */
            for (int i = 0; i < 60; i++) {
                int s = socket(AF_INET, SOCK_STREAM, 0);
                if (s < 0) break;
                struct sockaddr_in sa;
                memset(&sa, 0, sizeof(sa));
                sa.sin_family = AF_INET;
                sa.sin_port = htons(80);
                sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                int r = connect(s, (struct sockaddr *)&sa, sizeof(sa));
                close(s);
                if (r == 0) break;
                usleep(500000);
            }
        }

        char *argv_def[] = { (char *)name, (char *)arg, NULL };
        char *argv_noarg[] = { (char *)name, NULL };
        char *argv_minibrowser[] = {
            (char *)name,
            (char *)(arg ? arg : "http://127.0.0.1/"),
            NULL,
        };
        char **argv = arg ? argv_def : argv_noarg;
        char *envp_default[] = {
            "HOME=/",
            "PATH=/bin:/usr/bin",
            "XDG_RUNTIME_DIR=/tmp",
            "XDG_CACHE_HOME=/tmp/.cache",
            "WAYLAND_DISPLAY=wayland-0",
            "GDK_BACKEND=wayland",
            "XCURSOR_PATH=/share/icons",
            "XCURSOR_THEME=Adwaita",
            "SSL_CERT_FILE=/share/netsurf/ca-bundle",
            "G_MESSAGES_DEBUG=all",
            "WEBKIT_DEBUG=all",
            NULL
        };
        char *envp_minibrowser[] = {
            "HOME=/",
            "PATH=/bin:/usr/bin",
            "XDG_RUNTIME_DIR=/tmp",
            "XDG_CACHE_HOME=/tmp/.cache",
            "WAYLAND_DISPLAY=wayland-0",
            "GDK_BACKEND=wayland",
            "XCURSOR_PATH=/share/icons",
            "XCURSOR_THEME=Adwaita",
            "SSL_CERT_FILE=/share/netsurf/ca-bundle",
            "G_MESSAGES_DEBUG=all",
            "WEBKIT_DEBUG=all",
            "SOUP_FORCE_HTTP1=1",
            NULL
        };
        char **envp = envp_default;
        if (is_minibrowser) {
            argv = argv_minibrowser;
            envp = envp_minibrowser;
        }
        execve(path, argv, envp);
        _exit(127);
    }

    g_children[slot] = pid;
    fprintf(stderr, "wlcomp: launched %s (pid %d)\n", name, pid);
}

static void launch_desktop_app(const char *path, const char *name)
{
    launch_desktop_app_arg(path, name, NULL);
}

static void reap_children(void)
{
    for (int i = 0; i < MAX_CHILDREN; i++) {
        if (g_children[i] > 0) {
            int status;
            pid_t r = waitpid(g_children[i], &status, WNOHANG);
            if (r > 0) {
                if (WIFEXITED(status))
                    fprintf(stderr, "wlcomp: child pid %d exited (status=%d)\n",
                            g_children[i], WEXITSTATUS(status));
                else if (WIFSIGNALED(status))
                    fprintf(stderr, "wlcomp: child pid %d killed by signal %d\n",
                            g_children[i], WTERMSIG(status));
                else
                    fprintf(stderr, "wlcomp: child pid %d wait status 0x%x\n",
                            g_children[i], status);
                g_children[i] = 0;
            }
        }
    }
}

static const char *path_basename(const char *path)
{
    const char *base = path;
    if (!path)
        return "";
    for (const char *p = path; *p; p++) {
        if (*p == '/')
            base = p + 1;
    }
    return base;
}

static char *trim_space(char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
        s++;
    char *end = s + strlen(s);
    while (end > s &&
           (end[-1] == ' ' || end[-1] == '\t' ||
            end[-1] == '\r' || end[-1] == '\n')) {
        *--end = '\0';
    }
    return s;
}

static void shortcut_set(desktop_icon_t *sc, const char *label, int action,
                         const char *exec_path, const char *exec_name,
                         const char *exec_arg, uint32_t color, char symbol)
{
    memset(sc, 0, sizeof(*sc));
    snprintf(sc->label, sizeof(sc->label), "%s", label ? label : "");
    snprintf(sc->exec_path, sizeof(sc->exec_path), "%s", exec_path ? exec_path : "");
    snprintf(sc->exec_name, sizeof(sc->exec_name), "%s", exec_name ? exec_name : "");
    snprintf(sc->exec_arg, sizeof(sc->exec_arg), "%s", exec_arg ? exec_arg : "");
    sc->action = action;
    sc->icon_color = color;
    sc->symbol = symbol ? symbol : '?';
}

static int shortcut_action_from_name(const char *name)
{
    if (strcmp(name, "terminal") == 0) return SHORTCUT_TERMINAL;
    if (strcmp(name, "files") == 0) return SHORTCUT_FILES;
    if (strcmp(name, "sysinfo") == 0) return SHORTCUT_SYSINFO;
    if (strcmp(name, "calc") == 0) return SHORTCUT_CALC;
    if (strcmp(name, "network") == 0) return SHORTCUT_NETWORK;
    if (strcmp(name, "settings") == 0) return SHORTCUT_SETTINGS;
    if (strcmp(name, "monitor") == 0) return SHORTCUT_MONITOR;
    if (strcmp(name, "3ddemo") == 0) return SHORTCUT_3DDEMO;
    if (strcmp(name, "editor") == 0) return SHORTCUT_EDITOR;
    return SHORTCUT_EXEC;
}

static void shortcut_add_default(const char *label, int action,
                                 const char *path, const char *name,
                                 uint32_t color, char symbol)
{
    if (g_icon_count >= DESKTOP_ICON_MAX)
        return;
    shortcut_set(&g_icons[g_icon_count++], label, action, path, name, NULL,
                 color, symbol);
}

static void load_default_shortcuts(void)
{
    shortcut_add_default("Terminal", SHORTCUT_TERMINAL, NULL, NULL, 0xFF3D6E9E, '>');
    shortcut_add_default("Files",    SHORTCUT_FILES,    NULL, NULL, 0xFFA67C52, 'F');
    shortcut_add_default("Info",     SHORTCUT_SYSINFO,  NULL, NULL, 0xFF3DA67C, 'i');
    shortcut_add_default("Calc",     SHORTCUT_CALC,     NULL, NULL, 0xFF7C3DA6, 'C');
    shortcut_add_default("Network",  SHORTCUT_NETWORK,  NULL, NULL, 0xFF3DA6A6, 'N');
    shortcut_add_default("Settings", SHORTCUT_SETTINGS, NULL, NULL, 0xFF7B7B7B, 'S');
    shortcut_add_default("Monitor",  SHORTCUT_MONITOR,  NULL, NULL, 0xFFA63D7C, 'M');
    shortcut_add_default("3D Demo",  SHORTCUT_3DDEMO,   NULL, NULL, 0xFF6EA63D, '3');
    shortcut_add_default("Editor",   SHORTCUT_EDITOR,   NULL, NULL, 0xFFA65C3D, 'V');
    shortcut_add_default("Browser",  SHORTCUT_EXEC, "/bin/netsurf", "netsurf",
                         0xFF3D6E9E, 'W');
    shortcut_add_default("WebKit",   SHORTCUT_EXEC,
                         "/libexec/webkit2gtk-4.1/MiniBrowser", "MiniBrowser",
                         0xFF9B59B6, 'K');
}

static int parse_desktop_shortcut(const char *path, desktop_icon_t *out)
{
    FILE *fp = fopen(path, "r");
    if (!fp)
        return -1;

    char name[48] = "";
    char exec[256] = "";
    char arg[256] = "";
    char builtin[32] = "";
    uint32_t color = 0xFF5A7090;
    char symbol = 'A';
    char line[320];

    while (fgets(line, sizeof(line), fp)) {
        char *s = trim_space(line);
        if (!s[0] || s[0] == '#' || s[0] == '[')
            continue;
        char *eq = strchr(s, '=');
        if (!eq)
            continue;
        *eq++ = '\0';
        char *key = trim_space(s);
        char *val = trim_space(eq);
        if (strcmp(key, "Name") == 0)
            snprintf(name, sizeof(name), "%s", val);
        else if (strcmp(key, "Exec") == 0)
            snprintf(exec, sizeof(exec), "%s", val);
        else if (strcmp(key, "Arg") == 0)
            snprintf(arg, sizeof(arg), "%s", val);
        else if (strcmp(key, "X-XV6-Builtin") == 0)
            snprintf(builtin, sizeof(builtin), "%s", val);
        else if (strcmp(key, "IconChar") == 0 && val[0])
            symbol = val[0];
        else if (strcmp(key, "IconColor") == 0 && val[0])
            color = (uint32_t)strtoul(val, NULL, 0);
    }

    fclose(fp);

    if (!name[0])
        snprintf(name, sizeof(name), "%s", path_basename(path));

    int action = builtin[0] ? shortcut_action_from_name(builtin) : SHORTCUT_EXEC;
    if (action == SHORTCUT_EXEC && !exec[0])
        return -1;

    shortcut_set(out, name, action, exec, exec[0] ? path_basename(exec) : "",
                 arg[0] ? arg : NULL, color, symbol);
    return 0;
}

static void load_desktop_shortcuts(void)
{
    if (g_shortcuts_loaded)
        return;
    g_shortcuts_loaded = 1;
    g_icon_count = 0;

    DIR *dir = opendir(DESKTOP_DIR);
    if (dir) {
        struct dirent *de;
        while ((de = readdir(dir)) != NULL && g_icon_count < DESKTOP_ICON_MAX) {
            if (!de->d_name[0] || de->d_name[0] == '.')
                continue;

            char full[320];
            snprintf(full, sizeof(full), "%s/%s", DESKTOP_DIR, de->d_name);

            struct stat st;
            if (stat(full, &st) != 0)
                continue;

            desktop_icon_t sc;
            if (S_ISREG(st.st_mode) && strstr(de->d_name, ".desktop")) {
                if (parse_desktop_shortcut(full, &sc) == 0)
                    g_icons[g_icon_count++] = sc;
            } else if (S_ISREG(st.st_mode) &&
                       (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
                shortcut_set(&g_icons[g_icon_count++], de->d_name, SHORTCUT_EXEC,
                             full, de->d_name, NULL, 0xFF5A7090, 'X');
            }
        }
        closedir(dir);
    }

    if (g_icon_count == 0)
        load_default_shortcuts();
}

/* ── Clock ─────────────────────────────────────────────────────────── */

static char g_clock_str[16] = "00:00:00";

static void update_clock(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    int sec = (int)(ts.tv_sec % 86400);
    int h = sec / 3600;
    int m = (sec % 3600) / 60;
    int s = sec % 60;
    g_clock_str[0] = '0' + h / 10;
    g_clock_str[1] = '0' + h % 10;
    g_clock_str[2] = ':';
    g_clock_str[3] = '0' + m / 10;
    g_clock_str[4] = '0' + m % 10;
    g_clock_str[5] = ':';
    g_clock_str[6] = '0' + s / 10;
    g_clock_str[7] = '0' + s % 10;
    g_clock_str[8] = '\0';
}

/* ── Desktop rendering ─────────────────────────────────────────────── */

/* Lay out icons once we know screen dimensions */
static void layout_icons(int fb_w, int fb_h)
{
    load_desktop_shortcuts();
    if (g_icons_laid_out) return;
    int cols = (fb_w - ICON_GRID_X0 * 2) / ICON_CELL_W;
    if (cols < 1) cols = 1;
    for (int i = 0; i < g_icon_count; i++) {
        int col = i % cols;
        int row = i / cols;
        g_icons[i].x = ICON_GRID_X0 + col * ICON_CELL_W;
        g_icons[i].y = ICON_GRID_Y0 + row * ICON_CELL_H;
        g_icons[i].w = ICON_CELL_W;
        g_icons[i].h = ICON_CELL_H;
    }
    g_icons_laid_out = 1;
}

/* Draw the wallpaper gradient (matches LVGL desktop style) */
static void draw_wallpaper(uint32_t *fb, int fb_w, int fb_h)
{
    for (int y = 0; y < fb_h; y++) {
        int frac = y * 1000 / (fb_h > 1 ? fb_h - 1 : 1);
        uint8_t r, g, b;
        if (frac < 600) {
            r = (uint8_t)(12 + (30 - 12) * frac / 600);
            g = (uint8_t)(20 + (60 - 20) * frac / 600);
            b = (uint8_t)(48 + (110 - 48) * frac / 600);
        } else if (frac < 750) {
            int f2 = frac - 600;
            r = (uint8_t)(30 + (20 - 30) * f2 / 150);
            g = (uint8_t)(60 + (80 - 60) * f2 / 150);
            b = (uint8_t)(110 + (100 - 110) * f2 / 150);
        } else {
            int f3 = frac - 750;
            r = (uint8_t)(20 + (16 - 20) * f3 / 250);
            g = (uint8_t)(80 + (40 - 80) * f3 / 250);
            b = (uint8_t)(100 + (65 - 100) * f3 / 250);
        }
        /* Slight noise for texture */
        uint32_t *row_p = fb + y * fb_w;
        for (int x = 0; x < fb_w; x++) {
            int noise = ((x * 7 + y * 13) & 7) - 4;
            int rn = (int)r + noise; if (rn < 0) rn = 0; if (rn > 255) rn = 255;
            int gn = (int)g + noise; if (gn < 0) gn = 0; if (gn > 255) gn = 255;
            int bn = (int)b + noise; if (bn < 0) bn = 0; if (bn > 255) bn = 255;
            row_p[x] = 0xFF000000 | ((uint32_t)rn << 16) | ((uint32_t)gn << 8) | (uint32_t)bn;
        }
    }
}

/* Draw a single desktop icon (box + symbol + label) */
static void draw_icon(uint32_t *fb, int fb_w, int fb_h, desktop_icon_t *ico,
                      int selected)
{
    int bx = ico->x + (ICON_CELL_W - ICON_BOX_SIZE) / 2;
    int by = ico->y + 2;

    /* Selection highlight behind icon */
    if (selected)
        draw_rounded_rect(fb, fb_w, fb_h, ico->x + 2, ico->y,
                          ICON_CELL_W - 4, ICON_CELL_H, 4, 0xFF3C5078);

    /* Icon box with rounded corners */
    draw_rounded_rect(fb, fb_w, fb_h, bx, by,
                      ICON_BOX_SIZE, ICON_BOX_SIZE, 6, ico->icon_color);

    /* Symbol character centered in box (scale 2) */
    int sx = bx + (ICON_BOX_SIZE - 8 * 2) / 2;
    int sy = by + (ICON_BOX_SIZE - 16 * 2) / 2;
    draw_char(fb, fb_w, fb_h, sx, sy, ico->symbol, 0xFFFFFFFF, 2);

    /* Label below */
    int lw = string_pixel_width(ico->label, 1);
    int lx = ico->x + (ICON_CELL_W - lw) / 2;
    int ly = by + ICON_BOX_SIZE + 4;
    draw_string(fb, fb_w, fb_h, lx, ly, ico->label, 0xFFD2DAE2, 1);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Internal Windows (compositor-rendered apps like the LVGL desktop)
 * ══════════════════════════════════════════════════════════════════════ */

#define MAX_IWIN        8
#define IWIN_TITLE_H    24
#define IWIN_CLOSE_SZ   18

enum {
    APP_NONE = 0,
    APP_TERMINAL,
    APP_SYSINFO,
    APP_FILES,
    APP_CALC,
    APP_NETWORK,
    APP_SETTINGS,
    APP_MONITOR,
    APP_3DDEMO,
    APP_FILEMGR,
};

/* ── Terminal constants ────────────────────────────────────────────── */
#define TERM_ROWS   24
#define TERM_COLS   80

typedef struct {
    int   active;
    int   type;
    int   x, y, w, h;         /* total window bounds */
    char  title[64];

    /* Dragging state */
    int   dragging;
    int   drag_ox, drag_oy;   /* cursor offset on drag start */

    /* Resize state */
    int   resizing;            /* 1 = currently resizing */
    int   resize_edge;         /* bitmask: 1=right, 2=bottom */
    int   resize_start_x, resize_start_y;   /* cursor pos at resize start */
    int   resize_start_w, resize_start_h;   /* window size at resize start */

    /* Terminal state */
    int   master_fd;
    pid_t shell_pid;
    char  cells[TERM_ROWS][TERM_COLS];
    int   cur_row, cur_col;

    /* Text apps (Info, Network, Monitor, Files) */
    char  text[4096];
    int   text_len;
    int   text_scroll;         /* scroll offset in lines */
    uint32_t last_refresh;     /* for auto-refresh apps */

    /* Calculator */
    char  calc_display[32];
    long  calc_accum;
    long  calc_operand;
    char  calc_op;
    int   calc_fresh;          /* next digit replaces display */

    /* 3D demo */
    int   demo_angle;          /* rotation angle in degrees */

    /* File manager */
    char  fm_path[256];        /* current directory */
    char  fm_entries[64][64];  /* file/dir names */
    int   fm_types[64];        /* 0=file, 1=dir, 2=launchable */
    int   fm_count;
    int   fm_scroll;
    int   fm_selected;
} iwin_t;

static iwin_t g_iwin[MAX_IWIN];
static int g_iwin_focus = -1;   /* index of focused internal window */
static int g_iwin_cascade;      /* cascade counter for positioning */

#define TB_TASK_W   120   /* max width of each task button */
#define TB_TASK_GAP  4    /* gap between task buttons */

static int surface_has_taskbar_button(const struct wlcomp_surface *surf)
{
    return surf && surf->xdg_toplevel && !surf->is_cursor &&
           (surf->mapped || surf->title[0] || surf->app_id[0]);
}

static const char *surface_taskbar_label(const struct wlcomp_surface *surf)
{
    if (strcmp(surf->app_id, "netsurf") == 0)
        return "NetSurf";
    if (surf->title[0])
        return surf->title;
    if (surf->app_id[0])
        return surf->app_id;
    return "Window";
}

static uint32_t surface_taskbar_color(const struct wlcomp_surface *surf,
                                      int focused)
{
    if (focused)
        return 0xFF4A6FA5;
    if (strcmp(surf->app_id, "netsurf") == 0)
        return 0xFF315A66;
    return 0xFF2E3440;
}

/* Draw the bottom taskbar */
static void draw_taskbar(uint32_t *fb, int fb_w, int fb_h)
{
    int tb_y = fb_h - TASKBAR_H;

    /* Taskbar background */
    draw_rect(fb, fb_w, fb_h, 0, tb_y, fb_w, TASKBAR_H, 0xFF1E1E21);
    /* Top border line */
    draw_rect(fb, fb_w, fb_h, 0, tb_y, fb_w, 1, 0xFF3C5078);

    /* Menu button */
    int btn_x = TB_BTN_PAD;
    int btn_y = tb_y + (TASKBAR_H - TB_BTN_H) / 2;
    draw_rounded_rect(fb, fb_w, fb_h, btn_x, btn_y, 55, TB_BTN_H, 4, 0xFF3C5078);
    draw_string(fb, fb_w, fb_h, btn_x + 8, btn_y + 6, "Menu", 0xFFD2DAE2, 1);

    /* Task buttons (after menu button) */
    int tx = TB_BTN_PAD + 55 + TB_TASK_GAP + 4;
    int right_limit = fb_w - 230; /* leave space for xv6 + clock */

    /* Internal windows */
    for (int i = 0; i < MAX_IWIN && tx + TB_TASK_W <= right_limit; i++) {
        if (!g_iwin[i].active) continue;
        int focused = (i == g_iwin_focus);
        uint32_t bg = focused ? 0xFF4A6FA5 : 0xFF2E3440;
        draw_rounded_rect(fb, fb_w, fb_h, tx, btn_y, TB_TASK_W, TB_BTN_H, 3, bg);
        /* Truncate title to fit */
        char lbl[16];
        int j;
        for (j = 0; j < 14 && g_iwin[i].title[j]; j++)
            lbl[j] = g_iwin[i].title[j];
        lbl[j] = '\0';
        draw_string(fb, fb_w, fb_h, tx + 6, btn_y + 6, lbl, 0xFFD2DAE2, 1);
        tx += TB_TASK_W + TB_TASK_GAP;
    }

    /* Wayland surfaces */
    struct wlcomp_surface *surf;
    wl_list_for_each(surf, &g_surfaces, link) {
        if (!surface_has_taskbar_button(surf) ||
            tx + TB_TASK_W > right_limit) continue;
        int focused = (surf == g_focused && g_iwin_focus < 0);
        uint32_t bg = surface_taskbar_color(surf, focused);
        draw_rounded_rect(fb, fb_w, fb_h, tx, btn_y, TB_TASK_W, TB_BTN_H, 3, bg);
        const char *name = surface_taskbar_label(surf);
        char lbl[16];
        int j;
        for (j = 0; j < 14 && name[j]; j++)
            lbl[j] = name[j];
        lbl[j] = '\0';
        draw_string(fb, fb_w, fb_h, tx + 6, btn_y + 6, lbl, 0xFFD2DAE2, 1);
        tx += TB_TASK_W + TB_TASK_GAP;
    }

    /* "xv6" title */
    draw_string(fb, fb_w, fb_h, fb_w - 210,
                tb_y + (TASKBAR_H - 16) / 2, "xv6", 0xFF8C8C96, 1);

    /* Clock (right side) */
    update_clock();
    int cw = string_pixel_width(g_clock_str, 1);
    draw_string(fb, fb_w, fb_h, fb_w - cw - 16,
                tb_y + (TASKBAR_H - 16) / 2, g_clock_str, 0xFF64C8FF, 1);
}

/* Check if (mx,my) hits a desktop icon; returns index or -1 */
static int icon_hit_test(int mx, int my)
{
    load_desktop_shortcuts();
    for (int i = 0; i < g_icon_count; i++) {
        if (mx >= g_icons[i].x && mx < g_icons[i].x + g_icons[i].w &&
            my >= g_icons[i].y && my < g_icons[i].y + g_icons[i].h)
            return i;
    }
    return -1;
}

/* Check if (mx,my) is on the taskbar menu button */
static int menu_hit_test(int mx, int my, int fb_h)
{
    int tb_y = fb_h - TASKBAR_H;
    int btn_y = tb_y + (TASKBAR_H - TB_BTN_H) / 2;
    return (mx >= TB_BTN_PAD && mx < TB_BTN_PAD + 55 &&
            my >= btn_y && my < btn_y + TB_BTN_H);
}

/* Check if (mx,my) hits a task button on the taskbar.
   Returns: 1 if hit (and switches focus), 0 if not. */
static int taskbar_task_hit(int mx, int my, int fb_w, int fb_h)
{
    int tb_y = fb_h - TASKBAR_H;
    int btn_y = tb_y + (TASKBAR_H - TB_BTN_H) / 2;
    if (my < btn_y || my >= btn_y + TB_BTN_H) return 0;

    int tx = TB_BTN_PAD + 55 + TB_TASK_GAP + 4;
    int right_limit = fb_w - 230;

    /* Internal windows */
    for (int i = 0; i < MAX_IWIN && tx + TB_TASK_W <= right_limit; i++) {
        if (!g_iwin[i].active) continue;
        if (mx >= tx && mx < tx + TB_TASK_W) {
            g_iwin_focus = i;
            return 1;
        }
        tx += TB_TASK_W + TB_TASK_GAP;
    }

    /* Wayland surfaces */
    struct wlcomp_surface *surf;
    wl_list_for_each(surf, &g_surfaces, link) {
        if (!surface_has_taskbar_button(surf) ||
            tx + TB_TASK_W > right_limit) continue;
        if (mx >= tx && mx < tx + TB_TASK_W) {
            g_iwin_focus = -1;
            if (surf->minimized) {
                /* Restore */
                surf->minimized = 0;
                g_focused = surf;
            } else if (surf == g_focused) {
                /* Already focused — minimize */
                surf->minimized = 1;
                g_focused = NULL;
            } else {
                /* Not focused — raise and focus */
                g_focused = surf;
            }
            /* Raise this surface by moving to head of list */
            wl_list_remove(&surf->link);
            wl_list_insert(&g_surfaces, &surf->link);
            return 1;
        }
        tx += TB_TASK_W + TB_TASK_GAP;
    }
    return 0;
}

/* ── Menu state ────────────────────────────────────────────────────── */

static int g_menu_open;

#define MENU_W          160
#define MENU_ITEM_H      24

static int menu_item_count(void)
{
    load_desktop_shortcuts();
    return g_icon_count + 2; /* separator + Power Off */
}

static const char *menu_item_label(int idx)
{
    load_desktop_shortcuts();
    if (idx < g_icon_count)
        return g_icons[idx].label;
    if (idx == g_icon_count)
        return "---";
    return "Power Off";
}

static void draw_menu(uint32_t *fb, int fb_w, int fb_h)
{
    if (!g_menu_open) return;

    int item_count = menu_item_count();
    int menu_h = item_count * MENU_ITEM_H + 8;
    int menu_x = TB_BTN_PAD;
    int menu_y = fb_h - TASKBAR_H - menu_h;

    /* Shadow */
    draw_rect(fb, fb_w, fb_h, menu_x + 3, menu_y + 3,
              MENU_W, menu_h, 0xFF101418);
    /* Background */
    draw_rounded_rect(fb, fb_w, fb_h, menu_x, menu_y,
                      MENU_W, menu_h, 6, 0xFF252528);
    /* Border */
    draw_rect(fb, fb_w, fb_h, menu_x, menu_y, MENU_W, 1, 0xFF3C5078);
    draw_rect(fb, fb_w, fb_h, menu_x, menu_y + menu_h - 1, MENU_W, 1, 0xFF3C5078);
    draw_rect(fb, fb_w, fb_h, menu_x, menu_y, 1, menu_h, 0xFF3C5078);
    draw_rect(fb, fb_w, fb_h, menu_x + MENU_W - 1, menu_y, 1, menu_h, 0xFF3C5078);

    for (int i = 0; i < item_count; i++) {
        int iy = menu_y + 4 + i * MENU_ITEM_H;
        const char *label = menu_item_label(i);
        if (label[0] == '-') {
            /* Separator */
            draw_rect(fb, fb_w, fb_h, menu_x + 8, iy + MENU_ITEM_H / 2,
                      MENU_W - 16, 1, 0xFF485460);
        } else {
            /* Hover highlight */
            int mx = g_cursor_x, my = g_cursor_y;
            if (mx >= menu_x && mx < menu_x + MENU_W &&
                my >= iy && my < iy + MENU_ITEM_H) {
                draw_rect(fb, fb_w, fb_h, menu_x + 2, iy,
                          MENU_W - 4, MENU_ITEM_H, 0xFF3C5078);
            }
            draw_string(fb, fb_w, fb_h, menu_x + 12, iy + 4,
                        label, 0xFFD2DAE2, 1);
        }
    }
}

/* Returns menu item index if clicked, or -1 */
static int menu_click_test(int mx, int my, int fb_h)
{
    if (!g_menu_open) return -1;
    int item_count = menu_item_count();
    int menu_h = item_count * MENU_ITEM_H + 8;
    int menu_x = TB_BTN_PAD;
    int menu_y = fb_h - TASKBAR_H - menu_h;
    if (mx < menu_x || mx >= menu_x + MENU_W ||
        my < menu_y || my >= menu_y + menu_h)
        return -2;  /* clicked outside menu → close it */
    int idx = (my - menu_y - 4) / MENU_ITEM_H;
    if (idx < 0 || idx >= item_count) return -1;
    if (menu_item_label(idx)[0] == '-') return -1;
    return idx;
}

/* Handle a desktop left-click (called from process_mouse on press) */
/* Forward declarations for internal window system */
static uint32_t get_time_ms(void);
enum {
    APP_NONE_FWD = 0,
    APP_TERMINAL_FWD,
    APP_SYSINFO_FWD,
    APP_FILES_FWD,
    APP_CALC_FWD,
    APP_NETWORK_FWD,
    APP_SETTINGS_FWD,
    APP_MONITOR_FWD,
    APP_3DDEMO_FWD,
    APP_FILEMGR_FWD,
};
static void open_terminal(void);
static void open_editor(void);
static void open_editor_path(const char *path);
static void open_text_app(int type, const char *title, int win_w, int win_h);
static void open_calc(void);
static void open_3ddemo(void);
static void open_filemgr(void);
static int handle_iwin_click(int mx, int my);
static void iwin_update_drag(int mx, int my, int buttons);
static void iwin_key_input(uint8_t keycode, uint8_t scancode, uint8_t pressed, uint8_t modifiers);
static void draw_iwin_all(uint32_t *fb, int fb_w, int fb_h);
static void process_terminals(void);

static void run_shortcut(const desktop_icon_t *sc)
{
    if (!sc)
        return;

    switch (sc->action) {
    case SHORTCUT_TERMINAL:
        open_terminal();
        break;
    case SHORTCUT_FILES:
        open_filemgr();
        break;
    case SHORTCUT_SYSINFO:
        open_text_app(APP_SYSINFO_FWD, "System Info", 500, 400);
        break;
    case SHORTCUT_CALC:
        open_calc();
        break;
    case SHORTCUT_NETWORK:
        open_text_app(APP_NETWORK_FWD, "Network", 500, 400);
        break;
    case SHORTCUT_SETTINGS:
        open_text_app(APP_SETTINGS_FWD, "Settings", 480, 400);
        break;
    case SHORTCUT_MONITOR:
        open_text_app(APP_MONITOR_FWD, "System Monitor", 500, 400);
        break;
    case SHORTCUT_3DDEMO:
        open_3ddemo();
        break;
    case SHORTCUT_EDITOR:
        open_editor();
        break;
    case SHORTCUT_EXEC:
    default:
        if (sc->exec_path[0])
            launch_desktop_app_arg(sc->exec_path,
                                   sc->exec_name[0] ? sc->exec_name : path_basename(sc->exec_path),
                                   sc->exec_arg[0] ? sc->exec_arg : NULL);
        break;
    }
}

static void handle_desktop_click(int mx, int my, int fb_w, int fb_h,
                                 int force_dblclick)
{
    /* Check menu first */
    if (g_menu_open) {
        int idx = menu_click_test(mx, my, fb_h);
        if (idx >= 0) {
            if (idx < g_icon_count)
                run_shortcut(&g_icons[idx]);
            else if (idx == g_icon_count + 1)
                syscall(XV6_SYS_poweroff);
            g_menu_open = 0;
        } else {
            g_menu_open = 0;  /* close on outside click or separator */
        }
        return;
    }

    /* Check menu button on taskbar */
    if (menu_hit_test(mx, my, fb_h)) {
        g_menu_open = !g_menu_open;
        return;
    }

    /* Check task buttons on taskbar */
    if (taskbar_task_hit(mx, my, fb_w, fb_h))
        return;

    /* Check desktop icons — first click selects, second click launches */
    int hit = icon_hit_test(mx, my);
    if (hit >= 0) {
        int is_launch = force_dblclick || (hit == g_selected_icon);
        if (is_launch) {
            run_shortcut(&g_icons[hit]);
            g_selected_icon = -1;
        } else {
            /* First click — select */
            g_selected_icon = hit;
        }
        return;
    }

    /* Click on empty desktop: deselect icon and close menu */
    g_selected_icon = -1;
    g_menu_open = 0;
}

/* ── Internal window management ────────────────────────────────────── */

static iwin_t *iwin_alloc(void)
{
    for (int i = 0; i < MAX_IWIN; i++)
        if (!g_iwin[i].active) return &g_iwin[i];
    return NULL;
}

static int iwin_index(iwin_t *w) { return (int)(w - g_iwin); }

static void iwin_close(int idx)
{
    iwin_t *w = &g_iwin[idx];
    if (w->type == APP_TERMINAL) {
        if (w->shell_pid > 0) {
            kill(w->shell_pid, SIGTERM);
            waitpid(w->shell_pid, NULL, 0);
            w->shell_pid = 0;
        }
        if (w->master_fd >= 0) {
            close(w->master_fd);
            w->master_fd = -1;
        }
    }
    w->active = 0;
    if (g_iwin_focus == idx) g_iwin_focus = -1;
}

static void iwin_focus(int idx)
{
    g_iwin_focus = idx;
}

static void iwin_setup_pos(iwin_t *w, int win_w, int win_h)
{
    int off = (g_iwin_cascade % 5) * 30;
    w->w = win_w;
    w->h = win_h;
    w->x = 60 + off;
    w->y = 30 + off;
    /* Clamp to screen */
    if (w->x + w->w > (int)g_fb_w - 20) w->x = 20;
    if (w->y + w->h > (int)g_fb_h - TASKBAR_H - 20) w->y = 20;
    g_iwin_cascade++;
}

/* ── Draw internal window frame ────────────────────────────────────── */

static void draw_iwin(uint32_t *fb, int fb_w, int fb_h, iwin_t *w, int focused)
{
    /* Shadow */
    draw_rect(fb, fb_w, fb_h, w->x + 3, w->y + 3, w->w, w->h, 0xFF101418);

    /* Window background */
    draw_rect(fb, fb_w, fb_h, w->x, w->y, w->w, w->h, 0xFF1E2228);

    /* Title bar */
    uint32_t tb_color = focused ? 0xFF2C4F7C : 0xFF2A2E35;
    draw_rect(fb, fb_w, fb_h, w->x, w->y, w->w, IWIN_TITLE_H, tb_color);

    /* Title text */
    draw_string(fb, fb_w, fb_h, w->x + 8, w->y + 4, w->title, 0xFFD2DAE2, 1);

    /* Close button (top-right) */
    int cx = w->x + w->w - IWIN_CLOSE_SZ - 4;
    int cy = w->y + 3;
    draw_rect(fb, fb_w, fb_h, cx, cy, IWIN_CLOSE_SZ, IWIN_CLOSE_SZ, 0xFFA63D3D);
    draw_char(fb, fb_w, fb_h, cx + 5, cy + 1, 'x', 0xFFFFFFFF, 1);

    /* Border */
    draw_rect(fb, fb_w, fb_h, w->x, w->y, w->w, 1, 0xFF3C5078);
    draw_rect(fb, fb_w, fb_h, w->x, w->y + w->h - 1, w->w, 1, 0xFF3C5078);
    draw_rect(fb, fb_w, fb_h, w->x, w->y, 1, w->h, 0xFF3C5078);
    draw_rect(fb, fb_w, fb_h, w->x + w->w - 1, w->y, 1, w->h, 0xFF3C5078);

    /* Resize grip (bottom-right corner, 3 diagonal dots) */
    uint32_t gc = 0xFF5A6878;
    int gx = w->x + w->w - 4;
    int gy = w->y + w->h - 4;
    draw_rect(fb, fb_w, fb_h, gx, gy, 2, 2, gc);
    draw_rect(fb, fb_w, fb_h, gx - 4, gy, 2, 2, gc);
    draw_rect(fb, fb_w, fb_h, gx, gy - 4, 2, 2, gc);
    draw_rect(fb, fb_w, fb_h, gx - 8, gy, 2, 2, gc);
    draw_rect(fb, fb_w, fb_h, gx - 4, gy - 4, 2, 2, gc);
    draw_rect(fb, fb_w, fb_h, gx, gy - 8, 2, 2, gc);
}

/* ── Terminal app ──────────────────────────────────────────────────── */

static void term_clear(iwin_t *w)
{
    memset(w->cells, ' ', sizeof(w->cells));
    w->cur_row = 0;
    w->cur_col = 0;
}

static void term_scroll_up(iwin_t *w)
{
    memmove(w->cells[0], w->cells[1], (TERM_ROWS - 1) * TERM_COLS);
    memset(w->cells[TERM_ROWS - 1], ' ', TERM_COLS);
}

static void term_putc(iwin_t *w, char c)
{
    if (c == '\n') {
        w->cur_row++;
        if (w->cur_row >= TERM_ROWS) {
            term_scroll_up(w);
            w->cur_row = TERM_ROWS - 1;
        }
    } else if (c == '\r') {
        w->cur_col = 0;
    } else if (c == '\b' || c == 0x7f) {
        if (w->cur_col > 0) w->cur_col--;
    } else if (c == '\t') {
        w->cur_col = (w->cur_col + 8) & ~7;
        if (w->cur_col >= TERM_COLS) w->cur_col = TERM_COLS - 1;
    } else if (c == '\033') {
        /* Start of ESC sequence — handled by term_process_output */
    } else if ((unsigned char)c >= 0x20) {
        if (w->cur_col >= TERM_COLS) {
            w->cur_col = 0;
            w->cur_row++;
            if (w->cur_row >= TERM_ROWS) {
                term_scroll_up(w);
                w->cur_row = TERM_ROWS - 1;
            }
        }
        w->cells[w->cur_row][w->cur_col] = c;
        w->cur_col++;
    }
}

/* Minimal ANSI CSI parser */
static int term_process_esc(iwin_t *w, const char *buf, int len)
{
    if (len < 2) return 0;  /* need more data */
    if (buf[1] == '[') {
        /* CSI sequence: ESC [ params letter */
        int i = 2;
        int params[8] = {0};
        int np = 0;
        while (i < len && ((buf[i] >= '0' && buf[i] <= '9') || buf[i] == ';')) {
            if (buf[i] == ';') {
                np++;
                if (np >= 8) np = 7;
            } else {
                params[np] = params[np] * 10 + (buf[i] - '0');
            }
            i++;
        }
        if (i >= len) return 0;  /* incomplete */
        np++;  /* count of params */
        char cmd = buf[i];
        switch (cmd) {
        case 'H': case 'f':  /* Cursor position */
            w->cur_row = (params[0] > 0 ? params[0] - 1 : 0);
            w->cur_col = (np > 1 && params[1] > 0 ? params[1] - 1 : 0);
            if (w->cur_row >= TERM_ROWS) w->cur_row = TERM_ROWS - 1;
            if (w->cur_col >= TERM_COLS) w->cur_col = TERM_COLS - 1;
            break;
        case 'A':  /* Cursor up */
            w->cur_row -= (params[0] > 0 ? params[0] : 1);
            if (w->cur_row < 0) w->cur_row = 0;
            break;
        case 'B':  /* Cursor down */
            w->cur_row += (params[0] > 0 ? params[0] : 1);
            if (w->cur_row >= TERM_ROWS) w->cur_row = TERM_ROWS - 1;
            break;
        case 'C':  /* Cursor forward */
            w->cur_col += (params[0] > 0 ? params[0] : 1);
            if (w->cur_col >= TERM_COLS) w->cur_col = TERM_COLS - 1;
            break;
        case 'D':  /* Cursor back */
            w->cur_col -= (params[0] > 0 ? params[0] : 1);
            if (w->cur_col < 0) w->cur_col = 0;
            break;
        case 'J':  /* Erase display */
            if (params[0] == 0) {
                /* Clear from cursor to end */
                memset(w->cells[w->cur_row] + w->cur_col, ' ',
                       TERM_COLS - w->cur_col);
                for (int r = w->cur_row + 1; r < TERM_ROWS; r++)
                    memset(w->cells[r], ' ', TERM_COLS);
            } else if (params[0] == 2 || params[0] == 3) {
                term_clear(w);
            }
            break;
        case 'K':  /* Erase line */
            if (params[0] == 0)
                memset(w->cells[w->cur_row] + w->cur_col, ' ',
                       TERM_COLS - w->cur_col);
            else if (params[0] == 1)
                memset(w->cells[w->cur_row], ' ', w->cur_col + 1);
            else if (params[0] == 2)
                memset(w->cells[w->cur_row], ' ', TERM_COLS);
            break;
        case 'm':  /* SGR (color/style) — ignore */
            break;
        case 'h': case 'l':  /* Mode set/reset — ignore */
            break;
        case 'r':  /* Scroll region — ignore */
            break;
        default:
            break;
        }
        return i + 1;  /* consumed bytes */
    } else if (buf[1] == ']') {
        /* OSC sequence: ESC ] ... BEL or ST — skip */
        for (int i = 2; i < len; i++) {
            if (buf[i] == '\007' || (buf[i] == '\\' && i > 2 && buf[i-1] == '\033'))
                return i + 1;
        }
        return 0;  /* incomplete */
    }
    return 2;  /* skip unknown ESC+char */
}

static void term_process_output(iwin_t *w)
{
    if (w->master_fd < 0) return;
    char buf[1024];
    int n;
    while ((n = read(w->master_fd, buf, sizeof(buf))) > 0) {
        int i = 0;
        while (i < n) {
            if (buf[i] == '\033') {
                int consumed = term_process_esc(w, buf + i, n - i);
                if (consumed > 0) { i += consumed; continue; }
                else { i++; continue; }  /* incomplete, skip ESC */
            }
            term_putc(w, buf[i]);
            i++;
        }
    }
}

static void draw_terminal_content(uint32_t *fb, int fb_w, int fb_h, iwin_t *w)
{
    int cx0 = w->x + 4;
    int cy0 = w->y + IWIN_TITLE_H + 2;
    int content_w = w->w - 8;
    int content_h = w->h - IWIN_TITLE_H - 4;

    /* Black background for terminal */
    draw_rect(fb, fb_w, fb_h, cx0, cy0, content_w, content_h, 0xFF000000);

    /* Draw characters */
    for (int row = 0; row < TERM_ROWS; row++) {
        for (int col = 0; col < TERM_COLS; col++) {
            char c = w->cells[row][col];
            if (c > ' ' && c <= '~') {
                int px = cx0 + 2 + col * 8;
                int py = cy0 + 2 + row * 16;
                draw_char(fb, fb_w, fb_h, px, py, c, 0xFF00FF00, 1);
            }
        }
    }

    /* Draw cursor (blinking block) */
    uint32_t t = get_time_ms();
    if ((t / 800) & 1) {
        int cpx = cx0 + 2 + w->cur_col * 8;
        int cpy = cy0 + 2 + w->cur_row * 16;
        draw_rect(fb, fb_w, fb_h, cpx, cpy, 8, 16, 0xFF00FF00);
    }
}

static void open_terminal(void)
{
    iwin_t *w = iwin_alloc();
    if (!w) return;

    memset(w, 0, sizeof(*w));
    w->active = 1;
    w->type = APP_TERMINAL;
    w->master_fd = -1;
    snprintf(w->title, sizeof(w->title), "Terminal");

    /* Size: 80 cols * 8px + padding, 24 rows * 16px + title + padding */
    iwin_setup_pos(w, TERM_COLS * 8 + 12, TERM_ROWS * 16 + IWIN_TITLE_H + 8);
    term_clear(w);

    /* Open PTY */
    int master = open("/dev/ptmx", O_RDWR | O_NOCTTY);
    if (master < 0) {
        snprintf(w->text, sizeof(w->text), "Failed to open /dev/ptmx");
        w->type = APP_SYSINFO;  /* fallback to text display */
        w->text_len = (int)strlen(w->text);
        iwin_focus(iwin_index(w));
        return;
    }

    unsigned int pts_idx = 0;
    ioctl(master, 0x5430 /* TIOCGPTN */, &pts_idx);

    char pts_path[32];
    snprintf(pts_path, sizeof(pts_path), "/dev/pts/%u", pts_idx);

    /* Non-blocking master */
    int fl = fcntl(master, F_GETFL, 0);
    fcntl(master, F_SETFL, fl | O_NONBLOCK);

    pid_t pid = fork();
    if (pid < 0) {
        close(master);
        w->active = 0;
        return;
    }

    if (pid == 0) {
        /* Child: shell */
        close(master);
        setsid();
        int slave = open(pts_path, O_RDWR);
        if (slave < 0) _exit(1);
        struct { unsigned short ws_row, ws_col, ws_xpixel, ws_ypixel; } ws;
        memset(&ws, 0, sizeof(ws));
        ws.ws_row = TERM_ROWS;
        ws.ws_col = TERM_COLS;
        ioctl(slave, 0x5414 /* TIOCSWINSZ */, &ws);
        dup2(slave, 0);
        dup2(slave, 1);
        dup2(slave, 2);
        if (slave > 2) close(slave);
        char *argv[] = { "sh", "--gui-session", NULL };
        char *envp[] = {
            "TERM=dumb",
            "HOME=/root",
            "PATH=/bin:/usr/bin",
            "PS1=\\w# ",
            "XDG_RUNTIME_DIR=/tmp",
            "XDG_CACHE_HOME=/tmp/.cache",
            "WAYLAND_DISPLAY=wayland-0",
            "GDK_BACKEND=wayland",
            "XCURSOR_PATH=/share/icons",
            "XCURSOR_THEME=Adwaita",
            "SSL_CERT_FILE=/share/netsurf/ca-bundle",
            "XV6_GUI_SESSION=wayland",
            NULL
        };
        execve("/bin/sh", argv, envp);
        _exit(1);
    }

    w->master_fd = master;
    w->shell_pid = pid;
    iwin_focus(iwin_index(w));
    fprintf(stderr, "wlcomp: terminal opened (master=%d shell=%d)\n", master, pid);
}

static void open_editor_path(const char *path)
{
    iwin_t *w = iwin_alloc();
    if (!w) return;

    memset(w, 0, sizeof(*w));
    w->active = 1;
    w->type = APP_TERMINAL;
    w->master_fd = -1;
    if (path && path[0])
        snprintf(w->title, sizeof(w->title), "Editor - %s", path_basename(path));
    else
        snprintf(w->title, sizeof(w->title), "Editor");

    iwin_setup_pos(w, TERM_COLS * 8 + 12, TERM_ROWS * 16 + IWIN_TITLE_H + 8);
    term_clear(w);

    int master = open("/dev/ptmx", O_RDWR | O_NOCTTY);
    if (master < 0) {
        snprintf(w->text, sizeof(w->text), "Failed to open /dev/ptmx");
        w->type = APP_SYSINFO;
        w->text_len = (int)strlen(w->text);
        iwin_focus(iwin_index(w));
        return;
    }

    unsigned int pts_idx = 0;
    ioctl(master, 0x5430 /* TIOCGPTN */, &pts_idx);

    char pts_path[32];
    snprintf(pts_path, sizeof(pts_path), "/dev/pts/%u", pts_idx);

    int fl = fcntl(master, F_GETFL, 0);
    fcntl(master, F_SETFL, fl | O_NONBLOCK);

    pid_t pid = fork();
    if (pid < 0) {
        close(master);
        w->active = 0;
        return;
    }

    if (pid == 0) {
        close(master);
        setsid();
        int slave = open(pts_path, O_RDWR);
        if (slave < 0) _exit(1);
        struct { unsigned short ws_row, ws_col, ws_xpixel, ws_ypixel; } ws;
        memset(&ws, 0, sizeof(ws));
        ws.ws_row = TERM_ROWS;
        ws.ws_col = TERM_COLS;
        ioctl(slave, 0x5414 /* TIOCSWINSZ */, &ws);
        dup2(slave, 0);
        dup2(slave, 1);
        dup2(slave, 2);
        if (slave > 2) close(slave);
        char *argv_noarg[] = { "vim", NULL };
        char *argv_file[] = { "vim", (char *)path, NULL };
        char **argv = (path && path[0]) ? argv_file : argv_noarg;
        char *envp[] = { "TERM=dumb", "HOME=/root", "PATH=/bin:/usr/bin",
                         NULL };
        execve("/bin/vim", argv, envp);
        _exit(1);
    }

    w->master_fd = master;
    w->shell_pid = pid;
    iwin_focus(iwin_index(w));
    fprintf(stderr, "wlcomp: editor opened (master=%d vim=%d)\n", master, pid);
}

static void open_editor(void)
{
    open_editor_path(NULL);
}

/* ── Text-display apps ─────────────────────────────────────────────── */

static void fill_sysinfo(iwin_t *w)
{
    w->text_len = 0;
    char *p = w->text;
    int rem = (int)sizeof(w->text) - 1;
    int n;

    n = snprintf(p, rem, "=== xv6 System Information ===\n\n");
    p += n; rem -= n;

    /* Kernel info */
    {
        char buf[128] = "";
        int fd = open("/proc/version", O_RDONLY);
        if (fd >= 0) {
            int r = read(fd, buf, sizeof(buf) - 1);
            if (r > 0) buf[r] = '\0';
            close(fd);
        }
        n = snprintf(p, rem, "Kernel: %s\n", buf[0] ? buf : "xv6 (unknown)");
        p += n; rem -= n;
    }

    /* Uptime */
    {
        char buf[64] = "";
        int fd = open("/proc/uptime", O_RDONLY);
        if (fd >= 0) {
            int r = read(fd, buf, sizeof(buf) - 1);
            if (r > 0) buf[r] = '\0';
            close(fd);
        }
        n = snprintf(p, rem, "Uptime: %s\n", buf[0] ? buf : "unknown");
        p += n; rem -= n;
    }

    /* Memory */
    {
        char buf[512] = "";
        int fd = open("/proc/meminfo", O_RDONLY);
        if (fd >= 0) {
            int r = read(fd, buf, sizeof(buf) - 1);
            if (r > 0) buf[r] = '\0';
            close(fd);
        }
        n = snprintf(p, rem, "\n--- Memory ---\n%s\n", buf[0] ? buf : "unavailable");
        p += n; rem -= n;
    }

    /* CPU info */
    {
        char buf[512] = "";
        int fd = open("/proc/cpuinfo", O_RDONLY);
        if (fd >= 0) {
            int r = read(fd, buf, sizeof(buf) - 1);
            if (r > 0) buf[r] = '\0';
            close(fd);
        }
        n = snprintf(p, rem, "--- CPU ---\n%s\n", buf[0] ? buf : "unavailable");
        p += n; rem -= n;
    }

    /* Display */
    n = snprintf(p, rem, "--- Display ---\n%ux%u framebuffer\n",
                 g_fb_w, g_fb_h);
    p += n; rem -= n;

    w->text_len = (int)(p - w->text);
}

#define NETCONF_MODE_DHCP   0
#define NETCONF_MODE_STATIC 1
#define NETCONF_HOSTNAME_MAX 32

struct netconf_req {
    int mode;
    unsigned int ip;
    unsigned int netmask;
    unsigned int gateway;
    unsigned int dns;
    char hostname[NETCONF_HOSTNAME_MAX];
};

static void format_ip4(char *buf, size_t bufsz, unsigned int ip)
{
    snprintf(buf, bufsz, "%u.%u.%u.%u",
             ip & 0xff,
             (ip >> 8) & 0xff,
             (ip >> 16) & 0xff,
             (ip >> 24) & 0xff);
}

static const char *netconf_mode_name(int mode)
{
    if (mode == NETCONF_MODE_DHCP)
        return "DHCP";
    if (mode == NETCONF_MODE_STATIC)
        return "Static";
    return "Unknown";
}

static void fill_network(iwin_t *w)
{
    w->text_len = 0;
    char *p = w->text;
    int rem = (int)sizeof(w->text) - 1;
    int n;

    n = snprintf(p, rem, "=== Network Information ===\n\n");
    p += n; rem -= n;

    int fd = open("/dev/netconf", O_RDONLY);
    if (fd < 0) {
        n = snprintf(p, rem, "Network device: unavailable\n");
        p += n; rem -= n;
        w->text_len = (int)(p - w->text);
        w->last_refresh = get_time_ms();
        return;
    }

    struct netconf_req req;
    int r = read(fd, &req, sizeof(req));
    close(fd);
    if (r != (int)sizeof(req)) {
        n = snprintf(p, rem, "Network status: waiting for lwIP\n");
        p += n; rem -= n;
        w->text_len = (int)(p - w->text);
        w->last_refresh = get_time_ms();
        return;
    }

    char ip[16], mask[16], gw[16], dns[16];
    format_ip4(ip, sizeof(ip), req.ip);
    format_ip4(mask, sizeof(mask), req.netmask);
    format_ip4(gw, sizeof(gw), req.gateway);
    format_ip4(dns, sizeof(dns), req.dns);

    n = snprintf(p, rem,
                 "Interface: e1000\n"
                 "Mode:      %s\n"
                 "Hostname:  %s\n"
                 "IPv4:      %s\n"
                 "Netmask:   %s\n"
                 "Gateway:   %s\n"
                 "DNS:       %s\n\n"
                 "(auto-refreshes every 2s)\n",
                 netconf_mode_name(req.mode),
                 req.hostname[0] ? req.hostname : "xv6",
                 req.ip ? ip : "not assigned",
                 req.netmask ? mask : "not assigned",
                 req.gateway ? gw : "not assigned",
                 req.dns ? dns : "not assigned");
    p += n; rem -= n;

    w->text_len = (int)(p - w->text);
    w->last_refresh = get_time_ms();
}

static void fill_files(iwin_t *w)
{
    w->text_len = 0;
    char *p = w->text;
    int rem = (int)sizeof(w->text) - 1;
    int n;

    n = snprintf(p, rem, "=== File Manager ===\n\nDirectory: /\n\n");
    p += n; rem -= n;

    /* List root directory via fork+exec */
    int pipefd[2];
    if (pipe(pipefd) == 0) {
        pid_t pid = fork();
        if (pid == 0) {
            close(pipefd[0]);
            dup2(pipefd[1], 1);
            close(pipefd[1]);
            char *argv[] = { "ls", "-la", "/", NULL };
            char *envp[] = { "PATH=/bin", NULL };
            execve("/bin/ls", argv, envp);
            _exit(1);
        }
        close(pipefd[1]);
        if (pid > 0) {
            int total = 0;
            while (total < rem - 1) {
                int r = read(pipefd[0], p + total, rem - 1 - total);
                if (r <= 0) break;
                total += r;
            }
            p[total] = '\0';
            p += total;
            rem -= total;
            waitpid(pid, NULL, 0);
        }
        close(pipefd[0]);
    }

    w->text_len = (int)(p - w->text);
}

static void fill_monitor(iwin_t *w)
{
    w->text_len = 0;
    char *p = w->text;
    int rem = (int)sizeof(w->text) - 1;
    int n;

    n = snprintf(p, rem, "=== System Monitor ===\n\n");
    p += n; rem -= n;

    /* Memory */
    {
        char buf[512] = "";
        int fd = open("/proc/meminfo", O_RDONLY);
        if (fd >= 0) {
            int r = read(fd, buf, sizeof(buf) - 1);
            if (r > 0) buf[r] = '\0';
            close(fd);
        }
        n = snprintf(p, rem, "--- Memory ---\n%s\n", buf[0] ? buf : "unavailable");
        p += n; rem -= n;
    }

    /* Uptime */
    {
        char buf[64] = "";
        int fd = open("/proc/uptime", O_RDONLY);
        if (fd >= 0) {
            int r = read(fd, buf, sizeof(buf) - 1);
            if (r > 0) buf[r] = '\0';
            close(fd);
        }
        n = snprintf(p, rem, "Uptime: %s\n", buf[0] ? buf : "unknown");
        p += n; rem -= n;
    }

    /* Load / processes */
    {
        char buf[1024] = "";
        int fd = open("/proc/stat", O_RDONLY);
        if (fd >= 0) {
            int r = read(fd, buf, sizeof(buf) - 1);
            if (r > 0) buf[r] = '\0';
            close(fd);
        }
        n = snprintf(p, rem, "--- CPU ---\n%s\n", buf[0] ? buf : "unavailable");
        p += n; rem -= n;
    }

    n = snprintf(p, rem, "(auto-refreshes every 2s)\n");
    p += n; rem -= n;

    w->text_len = (int)(p - w->text);
    w->last_refresh = get_time_ms();
}

static void fill_settings(iwin_t *w)
{
    w->text_len = 0;
    int n = snprintf(w->text, sizeof(w->text),
                     "=== Settings ===\n\n"
                     "Display: %ux%u\n"
                     "Color depth: 32 bpp\n\n"
                     "Compositor: wlcomp (Wayland)\n"
                     "Font: 8x16 bitmap\n\n"
                     "--- Resolution ---\n"
                     "(click a button below)\n",
                     g_fb_w, g_fb_h);
    w->text_len = n;
}

/* Draw settings with resolution buttons */
static void draw_text_content(uint32_t *fb, int fb_w, int fb_h, iwin_t *w);
static void draw_settings_content(uint32_t *fb, int fb_w, int fb_h, iwin_t *w)
{
    /* Draw text part first */
    draw_text_content(fb, fb_w, fb_h, w);

    /* Draw resolution buttons */
    int cx0 = w->x + 4;
    int cy0 = w->y + IWIN_TITLE_H + 2;
    int by = cy0 + 170;

    static const struct { int w, h; } res_modes[] = {
        {640,480}, {800,600}, {1024,768}, {1280,720}, {1280,1024}, {1920,1080},
    };

    for (int i = 0; i < 6; i++) {
        int bx = cx0 + 8 + (i % 3) * 140;
        int bby = by + (i / 3) * 32;
        uint32_t bg = 0xFF2A3040;
        /* Highlight current resolution */
        if ((uint32_t)res_modes[i].w == g_fb_w && (uint32_t)res_modes[i].h == g_fb_h)
            bg = 0xFF2C4F7C;
        draw_rounded_rect(fb, fb_w, fb_h, bx, bby, 130, 26, 4, bg);
        char label[20];
        snprintf(label, sizeof(label), "%dx%d", res_modes[i].w, res_modes[i].h);
        int lw = string_pixel_width(label, 1);
        draw_string(fb, fb_w, fb_h, bx + (130 - lw) / 2, bby + 5,
                    label, 0xFFD2DAE2, 1);
    }
}

static void draw_text_content(uint32_t *fb, int fb_w, int fb_h, iwin_t *w)
{
    int cx0 = w->x + 4;
    int cy0 = w->y + IWIN_TITLE_H + 2;
    int content_w = w->w - 8;
    int content_h = w->h - IWIN_TITLE_H - 4;

    /* Dark background */
    draw_rect(fb, fb_w, fb_h, cx0, cy0, content_w, content_h, 0xFF141820);

    /* Render text lines */
    int max_rows = content_h / 16;
    int max_cols = content_w / 8;
    int line = 0;
    int col = 0;
    int skip = w->text_scroll;

    for (int i = 0; i < w->text_len && line - skip < max_rows; i++) {
        char c = w->text[i];
        if (c == '\n') {
            line++;
            col = 0;
            continue;
        }
        if (line >= skip && col < max_cols) {
            int px = cx0 + 4 + col * 8;
            int py = cy0 + 4 + (line - skip) * 16;
            if (c > ' ' && c <= '~')
                draw_char(fb, fb_w, fb_h, px, py, c, 0xFFD2DAE2, 1);
        }
        col++;
    }
}

/* ── Calculator app ────────────────────────────────────────────────── */

static const char *calc_buttons[] = {
    "C", "(", ")", "/",
    "7", "8", "9", "*",
    "4", "5", "6", "-",
    "1", "2", "3", "+",
    "0", ".", "=", " ",
};
#define CALC_COLS 4
#define CALC_ROWS 5
#define CALC_BTN_W 52
#define CALC_BTN_H 36
#define CALC_BTN_PAD 4

static void calc_reset(iwin_t *w)
{
    snprintf(w->calc_display, sizeof(w->calc_display), "0");
    w->calc_accum = 0;
    w->calc_operand = 0;
    w->calc_op = 0;
    w->calc_fresh = 1;
}

static void calc_press(iwin_t *w, const char *btn)
{
    if (btn[0] == 'C') {
        calc_reset(w);
        return;
    }
    if (btn[0] >= '0' && btn[0] <= '9') {
        if (w->calc_fresh) {
            snprintf(w->calc_display, sizeof(w->calc_display), "%c", btn[0]);
            w->calc_fresh = 0;
        } else {
            int len = (int)strlen(w->calc_display);
            if (len < 15) {
                w->calc_display[len] = btn[0];
                w->calc_display[len + 1] = '\0';
            }
        }
        return;
    }
    if (btn[0] == '.') {
        if (!strchr(w->calc_display, '.')) {
            int len = (int)strlen(w->calc_display);
            if (len < 15) {
                w->calc_display[len] = '.';
                w->calc_display[len + 1] = '\0';
            }
        }
        return;
    }
    if (btn[0] == '=' || btn[0] == '+' || btn[0] == '-' ||
        btn[0] == '*' || btn[0] == '/') {
        long cur = strtol(w->calc_display, NULL, 10);
        if (w->calc_op) {
            switch (w->calc_op) {
            case '+': w->calc_accum += cur; break;
            case '-': w->calc_accum -= cur; break;
            case '*': w->calc_accum *= cur; break;
            case '/': w->calc_accum = (cur != 0) ? w->calc_accum / cur : 0; break;
            }
        } else {
            w->calc_accum = cur;
        }
        snprintf(w->calc_display, sizeof(w->calc_display), "%ld", w->calc_accum);
        w->calc_op = (btn[0] == '=') ? 0 : btn[0];
        w->calc_fresh = 1;
    }
}

static void draw_calc_content(uint32_t *fb, int fb_w, int fb_h, iwin_t *w)
{
    int cx0 = w->x + 4;
    int cy0 = w->y + IWIN_TITLE_H + 2;
    int content_w = w->w - 8;

    /* Display area */
    draw_rect(fb, fb_w, fb_h, cx0, cy0, content_w, 32, 0xFF0A1020);
    int dw = string_pixel_width(w->calc_display, 2);
    draw_string(fb, fb_w, fb_h, cx0 + content_w - dw - 8, cy0 + 4,
                w->calc_display, 0xFF64C8FF, 2);

    /* Buttons */
    for (int r = 0; r < CALC_ROWS; r++) {
        for (int c = 0; c < CALC_COLS; c++) {
            int idx = r * CALC_COLS + c;
            const char *lbl = calc_buttons[idx];
            if (lbl[0] == ' ') continue;
            int bx = cx0 + c * (CALC_BTN_W + CALC_BTN_PAD);
            int by = cy0 + 40 + r * (CALC_BTN_H + CALC_BTN_PAD);
            uint32_t bg;
            if (lbl[0] >= '0' && lbl[0] <= '9')
                bg = 0xFF2A3040;
            else if (lbl[0] == '=')
                bg = 0xFF2C4F7C;
            else
                bg = 0xFF3A4050;
            draw_rounded_rect(fb, fb_w, fb_h, bx, by,
                              CALC_BTN_W, CALC_BTN_H, 4, bg);
            int lw = string_pixel_width(lbl, 1);
            draw_string(fb, fb_w, fb_h,
                        bx + (CALC_BTN_W - lw) / 2,
                        by + (CALC_BTN_H - 16) / 2,
                        lbl, 0xFFD2DAE2, 1);
        }
    }
}

static int calc_button_hit(iwin_t *w, int mx, int my)
{
    int cx0 = w->x + 4;
    int cy0 = w->y + IWIN_TITLE_H + 2;
    for (int r = 0; r < CALC_ROWS; r++) {
        for (int c = 0; c < CALC_COLS; c++) {
            int bx = cx0 + c * (CALC_BTN_W + CALC_BTN_PAD);
            int by = cy0 + 40 + r * (CALC_BTN_H + CALC_BTN_PAD);
            if (mx >= bx && mx < bx + CALC_BTN_W &&
                my >= by && my < by + CALC_BTN_H)
                return r * CALC_COLS + c;
        }
    }
    return -1;
}

/* ── App openers ───────────────────────────────────────────────────── */

static void open_text_app(int type, const char *title, int win_w, int win_h)
{
    iwin_t *w = iwin_alloc();
    if (!w) return;
    memset(w, 0, sizeof(*w));
    w->active = 1;
    w->type = type;
    w->master_fd = -1;
    snprintf(w->title, sizeof(w->title), "%s", title);
    iwin_setup_pos(w, win_w, win_h);

    switch (type) {
    case APP_SYSINFO:  fill_sysinfo(w); break;
    case APP_NETWORK:  fill_network(w); break;
    case APP_FILES:    fill_files(w);   break;
    case APP_MONITOR:  fill_monitor(w); break;
    case APP_SETTINGS: fill_settings(w); break;
    }
    iwin_focus(iwin_index(w));
}

static void open_calc(void)
{
    iwin_t *w = iwin_alloc();
    if (!w) return;
    memset(w, 0, sizeof(*w));
    w->active = 1;
    w->type = APP_CALC;
    w->master_fd = -1;
    snprintf(w->title, sizeof(w->title), "Calculator");
    iwin_setup_pos(w, CALC_COLS * (CALC_BTN_W + CALC_BTN_PAD) + 12,
                   CALC_ROWS * (CALC_BTN_H + CALC_BTN_PAD) + IWIN_TITLE_H + 48);
    calc_reset(w);
    iwin_focus(iwin_index(w));
}

/* ── 3D Demo (wireframe spinning cube) ─────────────────────────────── */

/* Fixed-point sin/cos table (256 entries, scale 1024) */
static int sin_tab[256];
static int sin_tab_ready;
static void init_sin_tab(void)
{
    if (sin_tab_ready) return;
    for (int i = 0; i < 256; i++) {
        /* Bhaskara's sine approximation: sin(x) ≈ 16x(π-x) / [5π² - 4x(π-x)] */
        /* Map i in 0..255 to angle in 0..2π */
        long a_mr;  /* angle in 0..3141 (milliradians for 0..π) */
        int neg = 0;
        if (i < 128) a_mr = (long)i * 3142 / 128;
        else { a_mr = (long)(i - 128) * 3142 / 128; neg = 1; }
        long pi_mr = 3142;
        long num = 16 * a_mr * (pi_mr - a_mr);
        long den = 5 * pi_mr * pi_mr - 4 * a_mr * (pi_mr - a_mr);
        if (den == 0) den = 1;
        int v = (int)(num * 1024 / den);
        if (neg) v = -v;
        sin_tab[i] = v;
    }
    sin_tab_ready = 1;
}

static int fsin(int deg) { return sin_tab[((deg % 360 + 360) * 256 / 360) & 255]; }
static int fcos(int deg) { return sin_tab[(((deg + 90) % 360 + 360) * 256 / 360) & 255]; }

static void draw_line_fb(uint32_t *fb, int fb_w, int fb_h,
                         int x0, int y0, int x1, int y1, uint32_t color)
{
    int dx = x1 - x0, dy = y1 - y0;
    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    int err = ax - ay;
    for (;;) {
        if (x0 >= 0 && x0 < fb_w && y0 >= 0 && y0 < fb_h)
            fb[y0 * fb_w + x0] = color;
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -ay) { err -= ay; x0 += sx; }
        if (e2 < ax)  { err += ax; y0 += sy; }
    }
}

static void draw_3d_content(uint32_t *fb, int fb_w, int fb_h, iwin_t *w)
{
    int cx0 = w->x + 4;
    int cy0 = w->y + IWIN_TITLE_H + 2;
    int cw = w->w - 8;
    int ch = w->h - IWIN_TITLE_H - 4;

    /* Black background */
    draw_rect(fb, fb_w, fb_h, cx0, cy0, cw, ch, 0xFF080810);

    /* Hexagonal prism: 6 top vertices (0-5), 6 bottom vertices (6-11) */
    /* Hex radius 90, half-height 70 */
    init_sin_tab();

    int vx[12], vy[12], vz[12];
    for (int i = 0; i < 6; i++) {
        int deg = i * 60;
        vx[i]     = fcos(deg) * 90 / 1024;
        vy[i]     = -70;
        vz[i]     = fsin(deg) * 90 / 1024;
        vx[i + 6] = vx[i];
        vy[i + 6] = 70;
        vz[i + 6] = vz[i];
    }

    /* Rotate */
    int a = w->demo_angle;
    int sa = fsin(a), ca = fcos(a);
    int sb = fsin(a * 7 / 10), cb = fcos(a * 7 / 10);

    int sx[12], sy[12], rz_arr[12];
    int midx = cx0 + cw / 2, midy = cy0 + ch / 2;

    for (int i = 0; i < 12; i++) {
        int x = vx[i], y = vy[i], z = vz[i];
        int rx = (x * ca - z * sa) / 1024;
        int rz = (x * sa + z * ca) / 1024;
        int ry = (y * cb - rz * sb) / 1024;
        int rz2 = (y * sb + rz * cb) / 1024;
        int d = 450 + rz2;
        if (d < 50) d = 50;
        sx[i] = midx + rx * 300 / d;
        sy[i] = midy + ry * 300 / d;
        rz_arr[i] = rz2;
    }

    /* Faces: top (0-5), bottom (6-11), 6 side quads (each = 2 triangles) */
    /* Each face: tri verts (up to 4 tris for hex caps), depth, color */
    struct face { int v[4]; int nv; int depth; uint32_t color; } faces[20];
    int nf = 0;

    /* Top cap: 4 triangles fanning from vertex 0 */
    uint32_t top_col = 0xFF2288DD;
    for (int i = 0; i < 4; i++) {
        faces[nf].v[0] = 0; faces[nf].v[1] = i + 1; faces[nf].v[2] = i + 2; faces[nf].nv = 3;
        faces[nf].depth = (rz_arr[0] + rz_arr[i+1] + rz_arr[i+2]) / 3;
        faces[nf].color = top_col;
        nf++;
    }
    /* Bottom cap: 4 triangles fanning from vertex 6 */
    uint32_t bot_col = 0xFF1166AA;
    for (int i = 0; i < 4; i++) {
        faces[nf].v[0] = 6; faces[nf].v[1] = 6 + i + 1; faces[nf].v[2] = 6 + i + 2; faces[nf].nv = 3;
        faces[nf].depth = (rz_arr[6] + rz_arr[6+i+1] + rz_arr[6+i+2]) / 3;
        faces[nf].color = bot_col;
        nf++;
    }
    /* 6 side faces (quads as 2 triangles each) */
    uint32_t side_cols[6] = { 0xFF33AAFF, 0xFF22CC88, 0xFFDDAA22,
                              0xFFFF6644, 0xFFCC44CC, 0xFF44CCCC };
    for (int i = 0; i < 6; i++) {
        int a0 = i, a1 = (i + 1) % 6, b0 = i + 6, b1 = (i + 1) % 6 + 6;
        int d = (rz_arr[a0] + rz_arr[a1] + rz_arr[b0] + rz_arr[b1]) / 4;
        /* Shade sides by depth: further = darker */
        int bright = 160 + d / 2;
        if (bright < 60) bright = 60;
        if (bright > 255) bright = 255;
        uint32_t r = ((side_cols[i] >> 16) & 0xFF) * bright / 255;
        uint32_t g = ((side_cols[i] >>  8) & 0xFF) * bright / 255;
        uint32_t b = ((side_cols[i]      ) & 0xFF) * bright / 255;
        uint32_t col = 0xFF000000 | (r << 16) | (g << 8) | b;
        /* Triangle 1 */
        faces[nf].v[0] = a0; faces[nf].v[1] = a1; faces[nf].v[2] = b1; faces[nf].nv = 3;
        faces[nf].depth = d; faces[nf].color = col; nf++;
        /* Triangle 2 */
        faces[nf].v[0] = a0; faces[nf].v[1] = b1; faces[nf].v[2] = b0; faces[nf].nv = 3;
        faces[nf].depth = d; faces[nf].color = col; nf++;
    }

    /* Sort faces back-to-front (painter's algorithm) — simple bubble sort */
    for (int i = 0; i < nf - 1; i++)
        for (int j = i + 1; j < nf; j++)
            if (faces[i].depth < faces[j].depth) {
                struct face tmp = faces[i]; faces[i] = faces[j]; faces[j] = tmp;
            }

    /* Draw filled triangles using scanline */
    for (int fi = 0; fi < nf; fi++) {
        int x0 = sx[faces[fi].v[0]], y0 = sy[faces[fi].v[0]];
        int x1 = sx[faces[fi].v[1]], y1 = sy[faces[fi].v[1]];
        int x2 = sx[faces[fi].v[2]], y2 = sy[faces[fi].v[2]];
        uint32_t col = faces[fi].color;

        /* Sort vertices by y */
        if (y0 > y1) { int t; t=x0; x0=x1; x1=t; t=y0; y0=y1; y1=t; }
        if (y0 > y2) { int t; t=x0; x0=x2; x2=t; t=y0; y0=y2; y2=t; }
        if (y1 > y2) { int t; t=x1; x1=x2; x2=t; t=y1; y1=y2; y2=t; }

        int dy02 = y2 - y0 ? y2 - y0 : 1;
        int dy01 = y1 - y0 ? y1 - y0 : 1;
        int dy12 = y2 - y1 ? y2 - y1 : 1;

        for (int y = y0; y <= y2; y++) {
            if (y < cy0 || y >= cy0 + ch) continue;
            /* Interpolate x along edges */
            int xa = x0 + (x2 - x0) * (y - y0) / dy02;
            int xb;
            if (y < y1)
                xb = x0 + (x1 - x0) * (y - y0) / dy01;
            else
                xb = x1 + (x2 - x1) * (y - y1) / dy12;
            if (xa > xb) { int t = xa; xa = xb; xb = t; }
            if (xa < cx0) xa = cx0;
            if (xb >= cx0 + cw) xb = cx0 + cw - 1;
            for (int x = xa; x <= xb; x++) {
                if (x >= 0 && x < fb_w && y >= 0 && y < fb_h)
                    fb[y * fb_w + x] = col;
            }
        }
    }

    /* Info text */
    char buf[32];
    snprintf(buf, sizeof(buf), "angle: %d", a % 360);
    draw_string(fb, fb_w, fb_h, cx0 + 8, cy0 + ch - 20, buf, 0xFF808080, 1);

    /* Auto-advance angle */
    w->demo_angle = (w->demo_angle + 2) % 3600;
}

static void open_3ddemo(void)
{
    iwin_t *w = iwin_alloc();
    if (!w) return;
    memset(w, 0, sizeof(*w));
    w->active = 1;
    w->type = APP_3DDEMO;
    w->master_fd = -1;
    snprintf(w->title, sizeof(w->title), "3D Demo");
    iwin_setup_pos(w, 360, 320);
    w->demo_angle = 0;
    iwin_focus(iwin_index(w));
}

/* ── Graphical File Manager ────────────────────────────────────────── */

#define FM_ICON_W  72
#define FM_ICON_H  56
#define FM_ICON_PAD 4

static void fm_read_dir(iwin_t *w)
{
    w->fm_count = 0;
    w->fm_scroll = 0;
    w->fm_selected = -1;

    DIR *dir = opendir(w->fm_path);
    if (!dir)
        return;

    struct dirent *de;
    while ((de = readdir(dir)) != NULL && w->fm_count < 64) {
        if (!de->d_name[0])
            continue;

        size_t len = strlen(de->d_name);
        if (len >= sizeof(w->fm_entries[w->fm_count]))
            len = sizeof(w->fm_entries[w->fm_count]) - 1;

        memcpy(w->fm_entries[w->fm_count], de->d_name, len);
        w->fm_entries[w->fm_count][len] = '\0';

        char full[320];
        if (strcmp(w->fm_path, "/") == 0)
            snprintf(full, sizeof(full), "/%s", w->fm_entries[w->fm_count]);
        else
            snprintf(full, sizeof(full), "%s/%s", w->fm_path, w->fm_entries[w->fm_count]);

        struct stat st;
        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
            w->fm_types[w->fm_count] = 1;
        } else if ((stat(full, &st) == 0 &&
                    (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) ||
                   strstr(w->fm_entries[w->fm_count], ".desktop")) {
            w->fm_types[w->fm_count] = 2;
        } else {
            w->fm_types[w->fm_count] = 0;
        }

        w->fm_count++;
    }
    closedir(dir);
}

static void draw_filemgr_content(uint32_t *fb, int fb_w, int fb_h, iwin_t *w)
{
    int cx0 = w->x + 4;
    int cy0 = w->y + IWIN_TITLE_H + 2;
    int cw = w->w - 8;
    int ch = w->h - IWIN_TITLE_H - 4;

    /* Background */
    draw_rect(fb, fb_w, fb_h, cx0, cy0, cw, ch, 0xFF141820);

    /* Path bar */
    draw_rect(fb, fb_w, fb_h, cx0, cy0, cw, 22, 0xFF1A2030);
    draw_string(fb, fb_w, fb_h, cx0 + 4, cy0 + 3, w->fm_path, 0xFF64C8FF, 1);

    /* Icon grid */
    int cols = (cw - 8) / (FM_ICON_W + FM_ICON_PAD);
    if (cols < 1) cols = 1;
    int gy0 = cy0 + 26;
    int vis_rows = (ch - 30) / (FM_ICON_H + FM_ICON_PAD);
    int start = w->fm_scroll * cols;

    for (int i = start; i < w->fm_count; i++) {
        int vi = i - start;
        int row = vi / cols;
        int col = vi % cols;
        if (row >= vis_rows) break;

        int ix = cx0 + 4 + col * (FM_ICON_W + FM_ICON_PAD);
        int iy = gy0 + row * (FM_ICON_H + FM_ICON_PAD);

        /* Highlight selected */
        if (i == w->fm_selected)
            draw_rect(fb, fb_w, fb_h, ix, iy, FM_ICON_W, FM_ICON_H, 0xFF2C4F7C);

        /* Icon */
        int bx = ix + (FM_ICON_W - 28) / 2;
        int by = iy + 2;
        uint32_t icol = w->fm_types[i] == 1 ? 0xFFA67C52 :
                         w->fm_types[i] == 2 ? 0xFF3D6E9E : 0xFF5A7090;
        char sym = w->fm_types[i] == 1 ? 'D' :
                   w->fm_types[i] == 2 ? 'X' : 'f';
        draw_rounded_rect(fb, fb_w, fb_h, bx, by, 28, 24, 3, icol);
        draw_char(fb, fb_w, fb_h, bx + 10, by + 4, sym, 0xFFFFFFFF, 1);

        /* Label (truncated) */
        char label[12];
        int nlen = (int)strlen(w->fm_entries[i]);
        if (nlen > 10) { memcpy(label, w->fm_entries[i], 9); label[9] = '~'; label[10] = '\0'; }
        else { memcpy(label, w->fm_entries[i], nlen + 1); }
        int lw = string_pixel_width(label, 1);
        int lx = ix + (FM_ICON_W - lw) / 2;
        draw_string(fb, fb_w, fb_h, lx, iy + 30, label, 0xFFD2DAE2, 1);
    }
}

static int fm_icon_hit(iwin_t *w, int mx, int my)
{
    int cx0 = w->x + 4;
    int cy0 = w->y + IWIN_TITLE_H + 2;
    int cw = w->w - 8;
    int cols = (cw - 8) / (FM_ICON_W + FM_ICON_PAD);
    if (cols < 1) cols = 1;
    int gy0 = cy0 + 26;
    int start = w->fm_scroll * cols;

    for (int i = start; i < w->fm_count; i++) {
        int vi = i - start;
        int row = vi / cols;
        int col = vi % cols;
        int ix = cx0 + 4 + col * (FM_ICON_W + FM_ICON_PAD);
        int iy = gy0 + row * (FM_ICON_H + FM_ICON_PAD);
        if (mx >= ix && mx < ix + FM_ICON_W &&
            my >= iy && my < iy + FM_ICON_H)
            return i;
    }
    return -1;
}

static void fm_entry_path(iwin_t *w, int idx, char *out, size_t out_sz)
{
    if (strcmp(w->fm_path, "/") == 0)
        snprintf(out, out_sz, "/%s", w->fm_entries[idx]);
    else
        snprintf(out, out_sz, "%s/%s", w->fm_path, w->fm_entries[idx]);
}

static void fm_navigate(iwin_t *w, int idx)
{
    if (strcmp(w->fm_entries[idx], "..") == 0) {
        char *slash = strrchr(w->fm_path, '/');
        if (slash && slash != w->fm_path) *slash = '\0';
        else snprintf(w->fm_path, sizeof(w->fm_path), "/");
    } else if (strcmp(w->fm_entries[idx], ".") != 0) {
        char tmp[256];
        fm_entry_path(w, idx, tmp, sizeof(tmp));
        snprintf(w->fm_path, sizeof(w->fm_path), "%s", tmp);
    }
    char title[sizeof(w->title)];
    snprintf(title, sizeof(title), "Files - %s", w->fm_path);
    snprintf(w->title, sizeof(w->title), "%s", title);
    fm_read_dir(w);
}

static int path_has_suffix(const char *path, const char *suffix)
{
    size_t lp = strlen(path);
    size_t ls = strlen(suffix);
    return lp >= ls && strcmp(path + lp - ls, suffix) == 0;
}

static void open_regular_file(const char *path)
{
    if (path_has_suffix(path, ".desktop")) {
        desktop_icon_t sc;
        if (parse_desktop_shortcut(path, &sc) == 0) {
            run_shortcut(&sc);
            return;
        }
    }

    if (path_has_suffix(path, ".html") || path_has_suffix(path, ".htm")) {
        char url[320];
        snprintf(url, sizeof(url), "file://%s", path);
        launch_desktop_app_arg("/bin/netsurf", "netsurf", url);
        return;
    }

    if (path_has_suffix(path, ".txt") || path_has_suffix(path, ".log") ||
        path_has_suffix(path, ".md") || path_has_suffix(path, ".c") ||
        path_has_suffix(path, ".h") || path_has_suffix(path, ".sh")) {
        open_editor_path(path);
        return;
    }

    struct stat st;
    if (stat(path, &st) == 0 &&
        (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
        launch_desktop_app(path, path_basename(path));
        return;
    }

    open_editor_path(path);
}

static void open_filemgr(void)
{
    iwin_t *w = iwin_alloc();
    if (!w) return;
    memset(w, 0, sizeof(*w));
    w->active = 1;
    w->type = APP_FILEMGR;
    w->master_fd = -1;
    snprintf(w->title, sizeof(w->title), "Files - /");
    snprintf(w->fm_path, sizeof(w->fm_path), "/");
    iwin_setup_pos(w, 500, 420);
    fm_read_dir(w);
    iwin_focus(iwin_index(w));
}

/* Helper: draw content of an internal window */
static void draw_iwin_content(uint32_t *fb, int fb_w, int fb_h, iwin_t *w)
{
    switch (w->type) {
    case APP_TERMINAL: draw_terminal_content(fb, fb_w, fb_h, w); break;
    case APP_CALC:     draw_calc_content(fb, fb_w, fb_h, w);     break;
    case APP_3DDEMO:   draw_3d_content(fb, fb_w, fb_h, w);       break;
    case APP_FILEMGR:  draw_filemgr_content(fb, fb_w, fb_h, w);  break;
    case APP_SETTINGS: draw_settings_content(fb, fb_w, fb_h, w); break;
    default:           draw_text_content(fb, fb_w, fb_h, w);     break;
    }
}

/* Draw all internal windows (focused last = on top) */
static void draw_iwin_all(uint32_t *fb, int fb_w, int fb_h)
{
    /* Draw non-focused windows first */
    for (int i = 0; i < MAX_IWIN; i++) {
        iwin_t *w = &g_iwin[i];
        if (!w->active || i == g_iwin_focus) continue;
        draw_iwin(fb, fb_w, fb_h, w, 0);
        draw_iwin_content(fb, fb_w, fb_h, w);
    }
    /* Draw focused window on top */
    if (g_iwin_focus >= 0 && g_iwin[g_iwin_focus].active) {
        iwin_t *w = &g_iwin[g_iwin_focus];
        draw_iwin(fb, fb_w, fb_h, w, 1);
        draw_iwin_content(fb, fb_w, fb_h, w);
    }
}

/* Process all terminal PTYs */
static void process_terminals(void)
{
    for (int i = 0; i < MAX_IWIN; i++) {
        iwin_t *w = &g_iwin[i];
        if (w->active && w->type == APP_TERMINAL && w->master_fd >= 0)
            term_process_output(w);
    }
    /* Auto-refresh monitor and network windows */
    uint32_t now = get_time_ms();
    for (int i = 0; i < MAX_IWIN; i++) {
        iwin_t *w = &g_iwin[i];
        if (w->active && w->type == APP_MONITOR &&
            now - w->last_refresh > 2000) {
            fill_monitor(w);
        } else if (w->active && w->type == APP_NETWORK &&
                   now - w->last_refresh > 2000) {
            fill_network(w);
        }
    }
}

/* Hit-test: returns iwin index if (mx,my) is inside a window, -1 otherwise */
static int iwin_hit(int mx, int my)
{
    /* Check in reverse order (topmost = highest index or focused) */
    if (g_iwin_focus >= 0 && g_iwin[g_iwin_focus].active) {
        iwin_t *w = &g_iwin[g_iwin_focus];
        if (mx >= w->x && mx < w->x + w->w &&
            my >= w->y && my < w->y + w->h)
            return g_iwin_focus;
    }
    for (int i = MAX_IWIN - 1; i >= 0; i--) {
        if (!g_iwin[i].active) continue;
        if (i == g_iwin_focus) continue;  /* already checked */
        iwin_t *w = &g_iwin[i];
        if (mx >= w->x && mx < w->x + w->w &&
            my >= w->y && my < w->y + w->h)
            return i;
    }
    return -1;
}

/* Handle mouse click on an internal window */
static int handle_iwin_click(int mx, int my)
{
    int idx = iwin_hit(mx, my);
    if (idx < 0) return 0;  /* not on any window */

    iwin_t *w = &g_iwin[idx];
    iwin_focus(idx);

    /* Close button? */
    int cbx = w->x + w->w - IWIN_CLOSE_SZ - 4;
    int cby = w->y + 3;
    if (mx >= cbx && mx < cbx + IWIN_CLOSE_SZ &&
        my >= cby && my < cby + IWIN_CLOSE_SZ) {
        iwin_close(idx);
        return 1;
    }

    /* Resize edges (6px border on right, bottom, and corner) */
    #define RESIZE_BORDER 6
    {
        int on_right  = (mx >= w->x + w->w - RESIZE_BORDER && mx < w->x + w->w);
        int on_bottom = (my >= w->y + w->h - RESIZE_BORDER && my < w->y + w->h);
        if (on_right || on_bottom) {
            w->resizing = 1;
            w->resize_edge = (on_right ? 1 : 0) | (on_bottom ? 2 : 0);
            w->resize_start_x = mx;
            w->resize_start_y = my;
            w->resize_start_w = w->w;
            w->resize_start_h = w->h;
            return 1;
        }
    }

    /* Title bar → start drag */
    if (my >= w->y && my < w->y + IWIN_TITLE_H) {
        w->dragging = 1;
        w->drag_ox = mx - w->x;
        w->drag_oy = my - w->y;
        return 1;
    }

    /* Calculator button press */
    if (w->type == APP_CALC) {
        int btn = calc_button_hit(w, mx, my);
        if (btn >= 0) {
            calc_press(w, calc_buttons[btn]);
            return 1;
        }
    }

    /* File manager icon click */
    if (w->type == APP_FILEMGR) {
        int fmi = fm_icon_hit(w, mx, my);
        if (fmi >= 0) {
            if (fmi == w->fm_selected) {
                if (w->fm_types[fmi] == 1) {
                    fm_navigate(w, fmi);
                } else {
                    char full[320];
                    fm_entry_path(w, fmi, full, sizeof(full));
                    open_regular_file(full);
                }
            } else {
                w->fm_selected = fmi;
            }
            return 1;
        }
    }

    /* Settings resolution buttons */
    if (w->type == APP_SETTINGS) {
        int cx0 = w->x + 4;
        int cy0 = w->y + IWIN_TITLE_H + 2;
        /* Resolution buttons start at row ~10 (y offset 170) */
        int by = cy0 + 170;
        static const struct { int w, h; } res_modes[] = {
            {640,480}, {800,600}, {1024,768}, {1280,720}, {1280,1024}, {1920,1080},
        };
        for (int i = 0; i < 6; i++) {
            int bx = cx0 + 8 + (i % 3) * 140;
            int bby = by + (i / 3) * 32;
            if (mx >= bx && mx < bx + 130 && my >= bby && my < bby + 26) {
                /* Apply resolution change */
                struct fb_var_screeninfo vinfo;
                vinfo.xres = (uint32_t)res_modes[i].w;
                vinfo.yres = (uint32_t)res_modes[i].h;
                vinfo.bits_per_pixel = 32;
                vinfo.pitch = 0;
                if (ioctl(g_fb_fd, 0x4601 /* FBIOPUT_VSCREENINFO */, &vinfo) == 0) {
                    g_fb_w = vinfo.xres;
                    g_fb_h = vinfo.yres;
                    g_fb_pitch = vinfo.pitch;
                    free(g_fb_buf);
                    g_fb_buf = (uint32_t *)malloc(g_fb_w * g_fb_h * 4);
                    memset(g_fb_buf, 0, g_fb_w * g_fb_h * 4);
                    g_icons_laid_out = 0;  /* relayout icons */
                    fprintf(stderr, "wlcomp: resolution changed to %ux%u\n", g_fb_w, g_fb_h);
                    /* Refresh settings text */
                    fill_settings(w);
                }
                return 1;
            }
        }
    }

    return 1;  /* consumed by window */
}

/* Update dragging and resizing windows */
static void iwin_update_drag(int mx, int my, int buttons)
{
    for (int i = 0; i < MAX_IWIN; i++) {
        iwin_t *w = &g_iwin[i];
        if (!w->active) continue;

        /* Handle resize */
        if (w->resizing) {
            if (!(buttons & 1)) {
                w->resizing = 0;
                continue;
            }
            int dw = (w->resize_edge & 1) ? (mx - w->resize_start_x) : 0;
            int dh = (w->resize_edge & 2) ? (my - w->resize_start_y) : 0;
            int new_w = w->resize_start_w + dw;
            int new_h = w->resize_start_h + dh;
            /* Minimum size */
            if (new_w < 120) new_w = 120;
            if (new_h < IWIN_TITLE_H + 40) new_h = IWIN_TITLE_H + 40;
            /* Maximum: screen bounds */
            if (w->x + new_w > (int)g_fb_w) new_w = (int)g_fb_w - w->x;
            if (w->y + new_h > (int)g_fb_h) new_h = (int)g_fb_h - w->y;
            w->w = new_w;
            w->h = new_h;
            continue;
        }

        /* Handle drag */
        if (!w->dragging) continue;
        if (!(buttons & 1)) {
            w->dragging = 0;
            continue;
        }
        w->x = mx - w->drag_ox;
        w->y = my - w->drag_oy;
        /* Clamp */
        if (w->x < 0) w->x = 0;
        if (w->y < 0) w->y = 0;
        if (w->x + w->w > (int)g_fb_w) w->x = (int)g_fb_w - w->w;
        if (w->y + w->h > (int)g_fb_h) w->y = (int)g_fb_h - w->h;
    }
}

/* Send keyboard input to focused internal window */
static void iwin_key_input(uint8_t keycode, uint8_t scancode, uint8_t pressed, uint8_t modifiers)
{
    if (!pressed) return;  /* only handle key presses */
    if (g_iwin_focus < 0) return;
    iwin_t *w = &g_iwin[g_iwin_focus];
    if (!w->active) return;

    if (w->type == APP_TERMINAL && w->master_fd >= 0) {
        char buf[8];
        int len = 0;
        /* Extended keys → ANSI escape sequences */
        if (keycode == 0x80) { /* UP */
            buf[0] = '\033'; buf[1] = '['; buf[2] = 'A'; len = 3;
        } else if (keycode == 0x81) { /* DOWN */
            buf[0] = '\033'; buf[1] = '['; buf[2] = 'B'; len = 3;
        } else if (keycode == 0x83) { /* RIGHT */
            buf[0] = '\033'; buf[1] = '['; buf[2] = 'C'; len = 3;
        } else if (keycode == 0x82) { /* LEFT */
            buf[0] = '\033'; buf[1] = '['; buf[2] = 'D'; len = 3;
        } else if (keycode == 0x84) { /* HOME */
            buf[0] = '\033'; buf[1] = '['; buf[2] = 'H'; len = 3;
        } else if (keycode == 0x85) { /* END */
            buf[0] = '\033'; buf[1] = '['; buf[2] = 'F'; len = 3;
        } else if (keycode == 0x89) { /* DELETE */
            buf[0] = '\033'; buf[1] = '['; buf[2] = '3'; buf[3] = '~'; len = 4;
        } else if (keycode != 0) {
            buf[0] = (char)keycode;
            len = 1;
        }
        if (len > 0)
            write(w->master_fd, buf, len);
    } else if (w->type == APP_SYSINFO || w->type == APP_NETWORK ||
               w->type == APP_FILES || w->type == APP_MONITOR ||
               w->type == APP_SETTINGS) {
        /* Scroll with arrow keys */
        if (keycode == 0x80) w->text_scroll = (w->text_scroll > 0) ? w->text_scroll - 1 : 0;
        if (keycode == 0x81) w->text_scroll++;
    } else if (w->type == APP_FILEMGR) {
        if (keycode == 0x80) w->fm_scroll = (w->fm_scroll > 0) ? w->fm_scroll - 1 : 0;
        if (keycode == 0x81) w->fm_scroll++;
    }
}

static uint32_t get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void composite_and_flip(void)
{
    int fb_w = (int)g_fb_w;
    int fb_h = (int)g_fb_h;

    /* Lay out icons if not done */
    layout_icons(fb_w, fb_h);

    /* Draw desktop wallpaper */
    draw_wallpaper(g_fb_buf, fb_w, fb_h);

    /* Draw desktop icons */
    for (int i = 0; i < g_icon_count; i++)
        draw_icon(g_fb_buf, fb_w, fb_h, &g_icons[i], i == g_selected_icon);

    /* Draw background layer: internal windows behind Wayland when no iwin focused */
    if (g_iwin_focus < 0)
        draw_iwin_all(g_fb_buf, fb_w, fb_h);

    /* Draw client (Wayland) surfaces */
    struct wlcomp_surface *surf;
    wl_list_for_each(surf, &g_surfaces, link) {
        if (!surf->mapped || surf->minimized || !surf->committed_buf ||
            surf->is_cursor)
            continue;

        struct wlcomp_buffer *buf = surf->committed_buf;
        uint32_t *src = (uint32_t *)buffer_data(buf);
        if (!src) continue;

        int32_t bw = buf->width;
        int32_t bh = buf->height;
        int32_t src_stride_px = buf->stride / 4;

        for (int32_t row = 0; row < bh; row++) {
            int32_t dy = surf->y + row;
            if (dy < 0 || dy >= (int32_t)g_fb_h) continue;
            for (int32_t col = 0; col < bw; col++) {
                int32_t dx = surf->x + col;
                if (dx < 0 || dx >= (int32_t)g_fb_w) continue;
                uint32_t pixel = buffer_pixel_argb(buf,
                    src[row * src_stride_px + col]);
                uint32_t a = buffer_pixel_alpha(buf, pixel);
                if (a == 0xFF) {
                    g_fb_buf[dy * g_fb_w + dx] = pixel;
                } else if (a > 0) {
                    uint32_t bg = g_fb_buf[dy * g_fb_w + dx];
                    uint32_t inv_a = 255 - a;
                    uint32_t r = (((pixel >> 16) & 0xFF) * a +
                                  ((bg >> 16) & 0xFF) * inv_a) / 255;
                    uint32_t g = (((pixel >> 8) & 0xFF) * a +
                                  ((bg >> 8) & 0xFF) * inv_a) / 255;
                    uint32_t b = ((pixel & 0xFF) * a +
                                  (bg & 0xFF) * inv_a) / 255;
                    g_fb_buf[dy * g_fb_w + dx] = 0xFF000000 | (r << 16) | (g << 8) | b;
                }
            }
        }
    }

    /* Draw foreground layer: internal windows on top when an iwin is focused */
    if (g_iwin_focus >= 0)
        draw_iwin_all(g_fb_buf, fb_w, fb_h);

    /* Draw taskbar on top of everything */
    draw_taskbar(g_fb_buf, fb_w, fb_h);

    /* Draw popup menu if open */
    draw_menu(g_fb_buf, fb_w, fb_h);

    /* Draw software cursor */
    if (g_cursor_surface && g_cursor_surface->committed_buf &&
        buffer_data(g_cursor_surface->committed_buf)) {
        /* Use client-provided cursor surface */
        struct wlcomp_buffer *cbuf = g_cursor_surface->committed_buf;
        uint32_t *csrc = (uint32_t *)buffer_data(cbuf);
        int32_t cw = cbuf->width;
        int32_t ch = cbuf->height;
        int32_t cstride = cbuf->stride / 4;
        int32_t ox = g_cursor_x - g_cursor_hotspot_x;
        int32_t oy = g_cursor_y - g_cursor_hotspot_y;
        for (int32_t row = 0; row < ch; row++) {
            int32_t dy = oy + row;
            if (dy < 0 || dy >= (int32_t)g_fb_h) continue;
            for (int32_t col = 0; col < cw; col++) {
                int32_t dx = ox + col;
                if (dx < 0 || dx >= (int32_t)g_fb_w) continue;
                uint32_t pixel = buffer_pixel_argb(cbuf,
                    csrc[row * cstride + col]);
                uint32_t a = buffer_pixel_alpha(cbuf, pixel);
                if (a == 0xFF) {
                    g_fb_buf[dy * g_fb_w + dx] = pixel;
                } else if (a > 0) {
                    uint32_t bg = g_fb_buf[dy * g_fb_w + dx];
                    uint32_t inv_a = 255 - a;
                    uint32_t r = (((pixel >> 16) & 0xFF) * a +
                                  ((bg >> 16) & 0xFF) * inv_a) / 255;
                    uint32_t g = (((pixel >> 8) & 0xFF) * a +
                                  ((bg >> 8) & 0xFF) * inv_a) / 255;
                    uint32_t b = ((pixel & 0xFF) * a +
                                  (bg & 0xFF) * inv_a) / 255;
                    g_fb_buf[dy * g_fb_w + dx] = 0xFF000000 | (r << 16) | (g << 8) | b;
                }
            }
        }
    } else {
        /* Default arrow cursor (12×19) */
        static const uint8_t arrow[19][12] = {
            {1,0,0,0,0,0,0,0,0,0,0,0},
            {1,1,0,0,0,0,0,0,0,0,0,0},
            {1,2,1,0,0,0,0,0,0,0,0,0},
            {1,2,2,1,0,0,0,0,0,0,0,0},
            {1,2,2,2,1,0,0,0,0,0,0,0},
            {1,2,2,2,2,1,0,0,0,0,0,0},
            {1,2,2,2,2,2,1,0,0,0,0,0},
            {1,2,2,2,2,2,2,1,0,0,0,0},
            {1,2,2,2,2,2,2,2,1,0,0,0},
            {1,2,2,2,2,2,2,2,2,1,0,0},
            {1,2,2,2,2,2,2,2,2,2,1,0},
            {1,2,2,2,2,2,2,2,2,2,2,1},
            {1,2,2,2,2,2,1,1,1,1,1,1},
            {1,2,2,2,2,2,1,0,0,0,0,0},
            {1,2,2,1,1,2,2,1,0,0,0,0},
            {1,2,1,0,0,1,2,2,1,0,0,0},
            {1,1,0,0,0,0,1,2,2,1,0,0},
            {1,0,0,0,0,0,0,1,2,1,0,0},
            {0,0,0,0,0,0,0,0,1,1,0,0},
        };
        for (int y = 0; y < 19; y++) {
            int py = g_cursor_y + y;
            if (py < 0 || py >= (int)g_fb_h) continue;
            for (int x = 0; x < 12; x++) {
                int px = g_cursor_x + x;
                if (px < 0 || px >= (int)g_fb_w) continue;
                uint8_t v = arrow[y][x];
                if (v == 1)
                    g_fb_buf[py * g_fb_w + px] = 0xFF000000;
                else if (v == 2)
                    g_fb_buf[py * g_fb_w + px] = 0xFFFFFFFF;
            }
        }
    }

    /* Blit to /dev/fb0 via GPU ioctl */
    struct fb_gpu_blit cmd;
    cmd.x = 0;
    cmd.y = 0;
    cmd.w = g_fb_w;
    cmd.h = g_fb_h;
    cmd.src_pitch = g_fb_w * 4;
    cmd.pixels = (uint64_t)(uintptr_t)g_fb_buf;
    ioctl(g_fb_fd, FB_GPU_BLIT, &cmd);

    /* Fire frame callbacks.  Keep current committed buffers owned until
     * replacement; releasing them here lets clients reuse storage that we
     * still composite from. */
    uint32_t now = get_time_ms();
    wl_list_for_each(surf, &g_surfaces, link) {
        if (surf->frame_cb) {
            wl_callback_send_done(surf->frame_cb, now);
            wl_resource_destroy(surf->frame_cb);
            surf->frame_cb = NULL;
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════
 *  Input processing
 * ══════════════════════════════════════════════════════════════════════ */

static uint8_t g_prev_buttons;

static void process_mouse(void)
{
    /* Send deferred keyboard enter (set in surface_commit when a surface maps) */
    if (g_kbd_enter_pending && g_kbd_focused && g_kbd_focused->resource) {
        g_kbd_enter_pending = 0;
        struct wl_client *cl = wl_resource_get_client(g_kbd_focused->resource);
        struct wl_resource *kbd = find_resource_for_client(
            g_keyboard_resources, MAX_INPUT_RES, cl);
        if (kbd) {
            struct wl_array keys;
            wl_array_init(&keys);
            wl_keyboard_send_enter(kbd, ++g_serial,
                                   g_kbd_focused->resource, &keys);
            wl_array_release(&keys);
        }
    }

    struct mouse_event ev;
    uint8_t pressed_edges = 0;  /* bits that transitioned 0→1 */
    uint8_t released_edges = 0; /* bits that transitioned 1→0 */
    int left_press_count = 0;   /* number of left-button presses in batch */
    int16_t old_cursor_x = g_cursor_x;
    int16_t old_cursor_y = g_cursor_y;
    int cursor_moved = 0;
    int events_processed = 0;
    const int max_mouse_events_per_frame = 64;

    while (events_processed < max_mouse_events_per_frame &&
           read(g_mouse_fd, &ev, sizeof(ev)) == sizeof(ev)) {
        events_processed++;
        if (ev.flags & MOUSE_EVENT_F_ABSOLUTE) {
            g_cursor_x = (int16_t)((uint32_t)(uint16_t)ev.dx * g_fb_w / 65536);
            g_cursor_y = (int16_t)((uint32_t)(uint16_t)ev.dy * g_fb_h / 65536);
        } else {
            g_cursor_x += ev.dx;
            g_cursor_y += ev.dy;
        }
        if (g_cursor_x < 0) g_cursor_x = 0;
        if (g_cursor_y < 0) g_cursor_y = 0;
        if (g_cursor_x >= (int16_t)g_fb_w) g_cursor_x = (int16_t)(g_fb_w - 1);
        if (g_cursor_y >= (int16_t)g_fb_h) g_cursor_y = (int16_t)(g_fb_h - 1);

        uint8_t prev = g_buttons;
        g_buttons = ev.buttons;
        pressed_edges  |= (g_buttons & ~prev);   /* 0→1 transitions */
        released_edges |= (~g_buttons & prev);    /* 1→0 transitions */
        if ((g_buttons & 1) && !(prev & 1))
            left_press_count++;
    }
    cursor_moved = (g_cursor_x != old_cursor_x) || (g_cursor_y != old_cursor_y);

    /* Update internal window dragging */
    iwin_update_drag(g_cursor_x, g_cursor_y, g_buttons);

    /* ── Interactive grab for Wayland surface move/resize ── */
    if (g_grab_surface && g_grab_mode) {
        if (!(g_buttons & 1)) {
            /* Button released — end grab */
            if (g_grab_mode == 2 && g_grab_surface->xdg_toplevel &&
                g_grab_surface->xdg_surface) {
                /* Send final configure with the current size */
                int32_t dx = g_cursor_x - g_grab_start_mx;
                int32_t dy = g_cursor_y - g_grab_start_my;
                int32_t nw = g_grab_start_w, nh = g_grab_start_h;
                /* XDG_TOPLEVEL_RESIZE_EDGE values: right=8, bottom=2,
                   bottom_right=10, left=4, top=1, etc. */
                if (g_grab_edges & 8) nw += dx;  /* right */
                if (g_grab_edges & 4) { nw -= dx; }  /* left */
                if (g_grab_edges & 2) nh += dy;  /* bottom */
                if (g_grab_edges & 1) { nh -= dy; }  /* top */
                if (nw < 200) nw = 200;
                if (nh < 150) nh = 150;
                struct wl_array states;
                wl_array_init(&states);
                uint32_t *st = wl_array_add(&states, sizeof(uint32_t));
                *st = XDG_TOPLEVEL_STATE_ACTIVATED;
                xdg_toplevel_send_configure(g_grab_surface->xdg_toplevel,
                                            nw, nh, &states);
                wl_array_release(&states);
                xdg_surface_send_configure(g_grab_surface->xdg_surface,
                                           ++g_serial);
            }
            g_grab_surface = NULL;
            g_grab_mode = 0;
        } else {
            int32_t dx = g_cursor_x - g_grab_start_mx;
            int32_t dy = g_cursor_y - g_grab_start_my;
            if (g_grab_mode == 1) {
                /* Move: update surface position */
                g_grab_surface->x = g_grab_start_x + dx;
                g_grab_surface->y = g_grab_start_y + dy;
                /* Clamp so title area stays on screen */
                if (g_grab_surface->x < -((int32_t)(g_grab_surface->committed_buf ?
                    g_grab_surface->committed_buf->width : 200) - 60))
                    g_grab_surface->x = -((int32_t)(g_grab_surface->committed_buf ?
                        g_grab_surface->committed_buf->width : 200) - 60);
                if (g_grab_surface->y < 0) g_grab_surface->y = 0;
                if (g_grab_surface->x >= (int32_t)g_fb_w - 60)
                    g_grab_surface->x = (int32_t)g_fb_w - 60;
                if (g_grab_surface->y >= (int32_t)g_fb_h - 36)
                    g_grab_surface->y = (int32_t)g_fb_h - 36 - 1;
            } else if (g_grab_mode == 2 && g_grab_surface->xdg_toplevel &&
                       g_grab_surface->xdg_surface) {
                /* Resize: compute new size and send configure */
                int32_t nw = g_grab_start_w, nh = g_grab_start_h;
                int32_t nx = g_grab_start_x, ny = g_grab_start_y;
                if (g_grab_edges & 8) nw += dx;  /* right */
                if (g_grab_edges & 4) { nw -= dx; nx += dx; }  /* left */
                if (g_grab_edges & 2) nh += dy;  /* bottom */
                if (g_grab_edges & 1) { nh -= dy; ny += dy; }  /* top */
                if (nw < 200) nw = 200;
                if (nh < 150) nh = 150;
                g_grab_surface->x = nx;
                g_grab_surface->y = ny;
                struct wl_array states;
                wl_array_init(&states);
                uint32_t *st = wl_array_add(&states, sizeof(uint32_t));
                *st = XDG_TOPLEVEL_STATE_ACTIVATED;
                xdg_toplevel_send_configure(g_grab_surface->xdg_toplevel,
                                            nw, nh, &states);
                wl_array_release(&states);
                xdg_surface_send_configure(g_grab_surface->xdg_surface,
                                           ++g_serial);
            }
        }
        /* During grab, don't forward pointer events to clients */
        g_prev_buttons = g_buttons;
        return;
    }

    /* Hit testing respects stacking order:
     * When an internal window is focused, it's on top — check iwin first.
     * Otherwise Wayland surfaces are on top — check them first. */
    int32_t sx, sy;
    struct wlcomp_surface *target = NULL;

    if (g_iwin_focus >= 0) {
        /* Internal windows on top: check iwin first */
        int iwin_under = iwin_hit(g_cursor_x, g_cursor_y);
        if (iwin_under < 0)
            target = surface_at(g_cursor_x, g_cursor_y, &sx, &sy);
    } else {
        /* Wayland surfaces on top: check them first */
        target = surface_at(g_cursor_x, g_cursor_y, &sx, &sy);
    }

    if (pressed_edges & 1) {
        /* Left button was pressed (at least once) during this batch */
        int desktop_handled = 0;

        /* Priority: windows first, then menu/taskbar, then desktop icons.
         * This prevents icons from stealing clicks from overlapping windows. */
        if (handle_iwin_click(g_cursor_x, g_cursor_y)) {
            /* Internal window consumed the click */
        } else if (target) {
            /* Clicking on Wayland surface — unfocus internal windows */
            g_iwin_focus = -1;
        } else if (g_menu_open ||
                   g_cursor_y >= (int16_t)(g_fb_h - TASKBAR_H)) {
            /* Menu open or click on taskbar area */
            g_iwin_focus = -1;
            handle_desktop_click(g_cursor_x, g_cursor_y,
                                 (int)g_fb_w, (int)g_fb_h,
                                 left_press_count >= 2);
            desktop_handled = 1;
        } else {
            /* No window under cursor — desktop icons / background */
            g_iwin_focus = -1;
            handle_desktop_click(g_cursor_x, g_cursor_y,
                                 (int)g_fb_w, (int)g_fb_h,
                                 left_press_count >= 2);
            desktop_handled = 1;
        }

        /* Suppress pointer event to Wayland surface if desktop consumed it */
        if (desktop_handled)
            target = NULL;

        /* Click-to-focus: only change keyboard when clicking a
         * DIFFERENT Wayland surface (not desktop/taskbar/iwin) */
        if (!desktop_handled && target && target != g_kbd_focused) {
            /* Keyboard leave old surface */
            if (g_kbd_focused && g_kbd_focused->resource) {
                struct wl_client *cl = wl_resource_get_client(
                    g_kbd_focused->resource);
                struct wl_resource *kbd = find_resource_for_client(
                    g_keyboard_resources, MAX_INPUT_RES, cl);
                if (kbd)
                    wl_keyboard_send_leave(kbd, ++g_serial,
                                           g_kbd_focused->resource);
            }
            g_kbd_focused = target;
            /* Keyboard enter new surface */
            if (g_kbd_focused->resource) {
                struct wl_client *cl = wl_resource_get_client(
                    g_kbd_focused->resource);
                struct wl_resource *kbd = find_resource_for_client(
                    g_keyboard_resources, MAX_INPUT_RES, cl);
                if (kbd) {
                    struct wl_array keys;
                    wl_array_init(&keys);
                    wl_keyboard_send_enter(kbd, ++g_serial,
                                           g_kbd_focused->resource,
                                           &keys);
                    wl_array_release(&keys);
                }
            }
        }
    }

    /* Forward Wayland pointer events to clients */
    {
        int sent_pointer_event = 0;
        if (target != g_focused) {
            /* Send pointer leave to old focused surface */
            if (g_focused && g_focused->resource) {
                struct wl_client *old_cl = wl_resource_get_client(
                    g_focused->resource);
                struct wl_resource *old_ptr = find_resource_for_client(
                    g_pointer_resources, MAX_INPUT_RES, old_cl);
                if (old_ptr) {
                    wl_pointer_send_leave(old_ptr, ++g_serial,
                                          g_focused->resource);
                    wl_pointer_send_frame(old_ptr);
                    sent_pointer_event = 1;
                }
            }
            g_focused = target;
            /* Send pointer enter to new focused surface */
            if (g_focused && g_focused->resource) {
                struct wl_client *new_cl = wl_resource_get_client(
                    g_focused->resource);
                struct wl_resource *new_ptr = find_resource_for_client(
                    g_pointer_resources, MAX_INPUT_RES, new_cl);
                if (new_ptr) {
                    wl_pointer_send_enter(new_ptr, ++g_serial,
                                          g_focused->resource,
                                          wl_fixed_from_int(sx),
                                          wl_fixed_from_int(sy));
                    sent_pointer_event = 1;
                }
            }
        }

        /* Motion / button / frame only to focused surface's client */
        if (g_focused && g_focused->resource) {
            struct wl_client *cl = wl_resource_get_client(
                g_focused->resource);
            struct wl_resource *ptr = find_resource_for_client(
                g_pointer_resources, MAX_INPUT_RES, cl);
            if (ptr) {
                if (target == g_focused && cursor_moved) {
                    wl_pointer_send_motion(ptr, get_time_ms(),
                                           wl_fixed_from_int(sx),
                                           wl_fixed_from_int(sy));
                    sent_pointer_event = 1;
                }

                if (pressed_edges || released_edges) {
                    uint32_t time = get_time_ms();
                    /* Send press events for buttons that went 0→1 */
                    if (pressed_edges & 1)
                        wl_pointer_send_button(ptr, ++g_serial,
                                               time, 0x110,
                                               WL_POINTER_BUTTON_STATE_PRESSED);
                    if (pressed_edges & 2)
                        wl_pointer_send_button(ptr, ++g_serial,
                                               time, 0x111,
                                               WL_POINTER_BUTTON_STATE_PRESSED);
                    if (pressed_edges & 4)
                        wl_pointer_send_button(ptr, ++g_serial,
                                               time, 0x112,
                                               WL_POINTER_BUTTON_STATE_PRESSED);
                    /* Send release events for buttons that went 1→0 */
                    if (released_edges & 1)
                        wl_pointer_send_button(ptr, ++g_serial,
                                               time, 0x110,
                                               WL_POINTER_BUTTON_STATE_RELEASED);
                    if (released_edges & 2)
                        wl_pointer_send_button(ptr, ++g_serial,
                                               time, 0x111,
                                               WL_POINTER_BUTTON_STATE_RELEASED);
                    if (released_edges & 4)
                        wl_pointer_send_button(ptr, ++g_serial,
                                               time, 0x112,
                                               WL_POINTER_BUTTON_STATE_RELEASED);
                    sent_pointer_event = 1;
                }

                if (sent_pointer_event)
                    wl_pointer_send_frame(ptr);
            }
        }
    }

    g_prev_buttons = g_buttons;
}

/* Bitmap tracking which keys are currently held (for Wayland dedup).
 * Indexed by evdev keycode; 256 bits = 32 bytes. */
static uint8_t g_key_down[32];

static inline int key_is_down(uint32_t code)
{
    return code < 256 && (g_key_down[code / 8] & (1u << (code % 8)));
}
static inline void key_set_down(uint32_t code, int down)
{
    if (code >= 256) return;
    if (down)
        g_key_down[code / 8] |= (1u << (code % 8));
    else
        g_key_down[code / 8] &= ~(1u << (code % 8));
}

/* Key debounce: track last release time per scancode to filter key bounce */
static uint32_t g_key_debounce_sc;     /* scancode of last released key */
static uint32_t g_key_debounce_time;   /* time of last release */
#define KEY_DEBOUNCE_MS 30

/* Track last modifier state to avoid redundant modifier events */
static uint32_t g_last_mods_sent;

static void process_keyboard(void)
{
    struct kbd_event ev;
    while (read(g_kbd_fd, &ev, sizeof(ev)) == sizeof(ev)) {
        /* Key debounce: suppress press that follows release of same key
         * within KEY_DEBOUNCE_MS (catches PS/2 contact bounce) */
        if (ev.pressed && ev.scancode == g_key_debounce_sc) {
            uint32_t now = get_time_ms();
            if ((now - g_key_debounce_time) < KEY_DEBOUNCE_MS)
                continue;  /* bounce — skip */
        }
        if (!ev.pressed)  {
            g_key_debounce_sc = ev.scancode;
            g_key_debounce_time = get_time_ms();
        }

        /* Route to internal window if one is focused */
        if (g_iwin_focus >= 0 && g_iwin[g_iwin_focus].active) {
            iwin_key_input(ev.keycode, ev.scancode, ev.pressed, ev.modifiers);
            continue;
        }
        /* Otherwise send to keyboard-focused Wayland client */
        if (!g_kbd_focused || !g_kbd_focused->resource)
            continue;
        {
            struct wl_client *cl = wl_resource_get_client(
                g_kbd_focused->resource);
            struct wl_resource *kbd = find_resource_for_client(
                g_keyboard_resources, MAX_INPUT_RES, cl);
            if (!kbd)
                continue;

            /* Convert PS/2 scancode to evdev keycode.
             * For basic keys (< 0x80), PS/2 set 1 == evdev.
             * For E0-prefixed keys, detect via keycode 0x80-0x89. */
            uint32_t evdev_code = ev.scancode;
            switch (ev.keycode) {
            case 0x80: evdev_code = 103; break; /* UP */
            case 0x81: evdev_code = 108; break; /* DOWN */
            case 0x82: evdev_code = 105; break; /* LEFT */
            case 0x83: evdev_code = 106; break; /* RIGHT */
            case 0x84: evdev_code = 102; break; /* HOME */
            case 0x85: evdev_code = 107; break; /* END */
            case 0x86: evdev_code = 104; break; /* PGUP */
            case 0x87: evdev_code = 109; break; /* PGDN */
            case 0x88: evdev_code = 110; break; /* INSERT */
            case 0x89: evdev_code = 111; break; /* DELETE */
            }

            /* Suppress hardware typematic repeat: Wayland clients handle
             * key repeat themselves based on repeat_info, so we must not
             * forward duplicate press events from the PS/2 controller. */
            if (ev.pressed && key_is_down(evdev_code))
                continue;   /* already down — hardware repeat, skip */
            key_set_down(evdev_code, ev.pressed);

            uint32_t state = ev.pressed ? WL_KEYBOARD_KEY_STATE_PRESSED
                                        : WL_KEYBOARD_KEY_STATE_RELEASED;
            wl_keyboard_send_key(kbd, ++g_serial,
                                 get_time_ms(), evdev_code, state);

            /* Send modifier state only when it changes */
            uint32_t mods_dep = 0;  /* depressed (currently held) */
            if (ev.modifiers & 0x01) mods_dep |= 1;  /* Shift */
            if (ev.modifiers & 0x02) mods_dep |= 4;  /* Control */
            if (ev.modifiers & 0x04) mods_dep |= 8;  /* Mod1 (Alt) */
            if (mods_dep != g_last_mods_sent) {
                wl_keyboard_send_modifiers(kbd, ++g_serial,
                                           mods_dep, 0, 0, 0);
                g_last_mods_sent = mods_dep;
            }
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════
 *  Initialization
 * ══════════════════════════════════════════════════════════════════════ */

static int init_framebuffer(void)
{
    g_fb_fd = open("/dev/fb0", O_RDWR);
    if (g_fb_fd < 0) {
        fprintf(stderr, "wlcomp: open /dev/fb0: %s\n", strerror(errno));
        return -1;
    }

    struct fb_var_screeninfo vinfo;
    if (ioctl(g_fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        fprintf(stderr, "wlcomp: FBIOGET_VSCREENINFO: %s\n", strerror(errno));
        close(g_fb_fd);
        return -1;
    }

    g_fb_w     = vinfo.xres;
    g_fb_h     = vinfo.yres;
    g_fb_pitch = vinfo.pitch;

    g_fb_buf = (uint32_t *)malloc(g_fb_w * g_fb_h * 4);
    if (!g_fb_buf) {
        fprintf(stderr, "wlcomp: malloc framebuf failed\n");
        close(g_fb_fd);
        return -1;
    }
    memset(g_fb_buf, 0, g_fb_w * g_fb_h * 4);

    fprintf(stderr, "wlcomp: fb0 %ux%u pitch=%u\n", g_fb_w, g_fb_h, g_fb_pitch);
    return 0;
}

static int init_input(void)
{
    g_mouse_fd = open("/dev/mouse", O_RDONLY | O_NONBLOCK);
    if (g_mouse_fd < 0)
        fprintf(stderr, "wlcomp: no /dev/mouse\n");

    g_kbd_fd = open("/dev/kbd", O_RDONLY | O_NONBLOCK);
    if (g_kbd_fd < 0)
        fprintf(stderr, "wlcomp: no /dev/kbd\n");
    else
        fprintf(stderr, "wlcomp: opened /dev/kbd fd=%d\n", g_kbd_fd);

    g_cursor_x = (int16_t)(g_fb_w / 2);
    g_cursor_y = (int16_t)(g_fb_h / 2);
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Main
 * ══════════════════════════════════════════════════════════════════════ */

static volatile int g_running = 1;

static void sig_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGCHLD, SIG_IGN);  /* auto-reap children */

    fprintf(stderr, "wlcomp: starting Wayland compositor\n");

    wl_list_init(&g_surfaces);

    if (init_framebuffer() < 0)
        return 1;
    init_input();

    /* Create Wayland display */
    g_display = wl_display_create();
    if (!g_display) {
        fprintf(stderr, "wlcomp: wl_display_create failed\n");
        return 1;
    }

    /* Register globals */
    g_compositor_global = wl_global_create(g_display, &wl_compositor_interface,
                                           5, NULL, compositor_bind);
    g_shm_global = wl_global_create(g_display, &wl_shm_interface,
                                    1, NULL, shm_bind);
    g_seat_global = wl_global_create(g_display, &wl_seat_interface,
                                     5, NULL, seat_bind);
    g_output_global = wl_global_create(g_display, &wl_output_interface,
                                       3, NULL, output_bind);
    g_xdg_wm_global = wl_global_create(g_display, &xdg_wm_base_interface,
                                        2, NULL, xdg_wm_bind);
    g_ddm_global = wl_global_create(g_display, &wl_data_device_manager_interface,
                                    3, NULL, ddm_bind);

    /* Add socket */
    if (wl_display_add_socket(g_display, "wayland-0") < 0) {
        fprintf(stderr, "wlcomp: wl_display_add_socket failed\n");
        return 1;
    }
    fprintf(stderr, "wlcomp: listening on wayland-0\n");

    /* Get the wayland event loop fd for polling */
    struct wl_event_loop *loop = wl_display_get_event_loop(g_display);
    int wl_fd = wl_event_loop_get_fd(loop);

    /* Set up epoll: monitor wayland fd + mouse + kbd */
    int epfd = epoll_create1(0);
    if (epfd < 0) {
        fprintf(stderr, "wlcomp: epoll_create1: %s\n", strerror(errno));
        return 1;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = wl_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, wl_fd, &ev);

    if (g_mouse_fd >= 0) {
        ev.data.fd = g_mouse_fd;
        epoll_ctl(epfd, EPOLL_CTL_ADD, g_mouse_fd, &ev);
    }
    if (g_kbd_fd >= 0) {
        ev.data.fd = g_kbd_fd;
        epoll_ctl(epfd, EPOLL_CTL_ADD, g_kbd_fd, &ev);
    }

    /* Main compositor loop */
    fprintf(stderr, "wlcomp: entering main loop\n");

    while (g_running) {
        /* Dispatch pending Wayland events */
        wl_display_flush_clients(g_display);
        wl_event_loop_dispatch(loop, 0);

        /* Process input */
        if (g_mouse_fd >= 0)
            process_mouse();
        if (g_kbd_fd >= 0)
            process_keyboard();

        /* Process terminal PTY output and auto-refresh monitors */
        process_terminals();

        /* Composite and present */
        composite_and_flip();

        /* Reap terminated child processes */
        reap_children();

        /* Flush events to clients */
        wl_display_flush_clients(g_display);

        /* Wait for events (16ms timeout ~ 60fps) */
        struct epoll_event events[8];
        epoll_wait(epfd, events, 8, 16);
    }

    fprintf(stderr, "wlcomp: shutting down\n");
    wl_display_destroy(g_display);
    free(g_fb_buf);
    if (g_fb_fd >= 0) close(g_fb_fd);
    if (g_mouse_fd >= 0) close(g_mouse_fd);
    if (g_kbd_fd >= 0) close(g_kbd_fd);
    close(epfd);

    return 0;
}
