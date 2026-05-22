/* Protocol globals */
static struct wl_global *g_compositor_global;
static struct wl_global *g_shm_global;
static struct wl_global *g_seat_global;
static struct wl_global *g_output_global;
static struct wl_global *g_xdg_wm_global;
static struct wl_global *g_xv6_gpu_global;
static struct wl_global *g_dmabuf_global;
static struct wl_global *g_explicit_sync_global;
static struct wl_global *g_subcompositor_global;
static volatile int g_running = 1;

/* ══════════════════════════════════════════════════════════════════════
 *  Surface / window tracking
 * ══════════════════════════════════════════════════════════════════════ */

#define MAX_SURFACES 32
#define MAX_SURFACE_DAMAGE_RECTS 16
#define WAYLAND_CLIENT_BUFFER_LIMIT (1024 * 1024)
#define XV6_D3D12_PRESENT_MAX_ALLOCATIONS 64

struct wlcomp_shm_pool;

struct wlcomp_buffer {
    struct wl_resource *resource;  /* wl_buffer resource */
    struct wlcomp_shm_pool *pool;
    int                  is_gpu_bo;
    int                  is_d3d12_shared;
    uint64_t             gpu_addr;
    uint64_t             gpu_size;
    uint32_t             gpu_handle;
    int                  owns_gpu_handle;
    int                  d3d12_resource_fd;
    int                  d3d12_fence_fd;
    int                  d3d12_dxg_fd;
    pid_t                d3d12_client_pid;
    uint32_t             d3d12_client_buffer_id;
    uint32_t             d3d12_manager_resource_id;
    uint64_t             d3d12_buffer_generation;
    uint32_t             d3d12_adapter;
    uint32_t             d3d12_source_luid_a;
    uint32_t             d3d12_source_luid_b;
    uint32_t             d3d12_matched_luid_a;
    uint32_t             d3d12_matched_luid_b;
    int                  d3d12_source_luid_valid;
    uint32_t             d3d12_device;
    uint32_t             d3d12_opened_resource;
    uint32_t             d3d12_opened_fence;
    uint32_t             d3d12_allocation_count;
    uint32_t             d3d12_opened_allocation_count;
    uint32_t             d3d12_opened_allocations[
        XV6_D3D12_PRESENT_MAX_ALLOCATIONS];
    uint32_t             d3d12_total_priv_size;
    uint64_t             d3d12_fence_cpu_va;
    uint64_t             d3d12_fence_gpu_va;
    uint64_t             d3d12_fence_value;
    int                  d3d12_fence_value_valid;
    uint32_t             d3d12_fence_wait_start_ms;
    uint64_t             d3d12_present_sequence;
    uint64_t             d3d12_present_fence_value;
    uint64_t             d3d12_native_present_attempt_id;
    uint64_t             d3d12_native_present_completion_id;
    uint64_t             d3d12_fb_blit_attempts_at_present;
    uint64_t             d3d12_fb_blit_successes_at_present;
    uint64_t             d3d12_cpu_map_rejects_at_present;
    uint64_t             d3d12_cpu_plane_rejects_at_present;
    int                  d3d12_present_complete;
    int                  d3d12_present_wait_confirmed;
    int                  d3d12_gpu_copy_complete;
    int                  d3d12_present_evidence_pending;
    uint64_t             d3d12_gpu_present_start_time;
    uint64_t             d3d12_gpu_copy_complete_time;
    int32_t              d3d12_present_dst_x;
    int32_t              d3d12_present_dst_y;
    uint32_t             d3d12_present_dst_w;
    uint32_t             d3d12_present_dst_h;
    int32_t              d3d12_present_src_x;
    int32_t              d3d12_present_src_y;
    uint32_t             d3d12_present_src_w;
    uint32_t             d3d12_present_src_h;
    uint64_t             plane_gpu_addr[4];
    uint64_t             plane_gpu_size[4];
    uint32_t             plane_gpu_handle[4];
    int                  acquire_fence_fd;
    uint32_t             acquire_wait_start_ms;
    int                  release_pending;
    int32_t             offset;
    int32_t             width;
    int32_t             height;
    int32_t             stride;
    int32_t             plane_stride[4];
    int32_t             plane_offset[4];
    uint32_t            plane_count;
    uint32_t            format;
    int                 y_inverted;
    struct wl_resource *explicit_release;
};

struct wlcomp_surface;

static void *buffer_plane_data(struct wlcomp_buffer *buf, uint32_t plane);
static void xv6_d3d12_observe_buffer_release(struct wlcomp_buffer *buf);
static void xv6_d3d12_observe_frame_callback(struct wlcomp_surface *surf,
                                             uint32_t now);

struct wlcomp_frame_callback {
    struct wl_resource *resource;
    struct wl_list link;
};

struct wlcomp_surface {
    struct wl_resource *resource;
    struct wl_resource *xdg_surface;
    struct wl_resource *xdg_toplevel;
    struct wlcomp_buffer *pending_buf;
    int     has_pending_buffer;
    struct wlcomp_buffer *committed_buf;
    int     buffer_released;  /* 1 = release sent, don't re-release */
    struct wl_resource *sync_resource;
    int     pending_acquire_fence_fd;
    int     has_pending_acquire_fence;
    struct wl_resource *pending_release;
    struct wl_list frame_callbacks;
    int32_t x, y;
    int     mapped;
    int     minimized;
    int     maximized;        /* 1 = currently maximized */
    int     xdg_initial_configure_sent;
    int32_t pending_configure_w, pending_configure_h;
    int     is_cursor;        /* 1 = cursor surface, skip normal rendering */
    int     is_subsurface;
    int32_t sub_x, sub_y;
    struct wlcomp_surface *parent;
    struct wl_resource *subsurface;
    pid_t   client_pid;       /* pid from Wayland credentials, if known */
    int     has_window_geometry;
    int32_t window_x, window_y, window_w, window_h;
    /* Saved geometry for restore from maximize */
    int32_t saved_x, saved_y, saved_w, saved_h;
    struct damage_rect pending_damage[MAX_SURFACE_DAMAGE_RECTS];
    int     pending_damage_count;
    char    title[64];
    char    app_id[64];
    uint32_t log_events;
    struct wl_list link;  /* in g_surfaces */
};

static struct wl_list g_surfaces;  /* list of wlcomp_surface */
static uint64_t g_d3d12_present_start_count;
static uint64_t g_d3d12_present_copy_complete_count;
static uint64_t g_d3d12_present_display_complete_count;
static uint64_t g_d3d12_present_reject_count;
static uint64_t g_d3d12_cpu_map_reject_count;
static uint64_t g_d3d12_cpu_plane_reject_count;
static struct wlcomp_surface *g_focused;  /* pointer-focused Wayland surface */
static struct wlcomp_surface *g_kbd_focused; /* keyboard-focused (click-to-focus) */
static int g_kbd_enter_pending; /* deferred keyboard enter for g_kbd_focused */
static int g_iwin_focus = -1;   /* index of focused internal window */

static void surface_set_keyboard_focus(struct wlcomp_surface *surf);

static int xv6_wlcomp_trace_enabled(void)
{
    static int enabled = -1;

    if (enabled < 0) {
        const char *env = getenv("WLCOMP_XV6_TRACE");
        enabled = env && env[0] && strcmp(env, "0") != 0;
    }

    return enabled;
}

static void surface_log_limited(struct wlcomp_surface *surf,
                                const char *event,
                                struct wlcomp_buffer *buf)
{
    if (!xv6_wlcomp_trace_enabled() || !surf || surf->log_events >= 48)
        return;
    surf->log_events++;
    fprintf(stderr,
            "wlcomp: surface %s pid=%d sub=%d mapped=%d xdg=%d top=%d "
            "buf=%p size=%dx%d gpu=%d pending=%d committed=%d\n",
            event, surf->client_pid, surf->is_subsurface, surf->mapped,
            surf->xdg_surface != NULL, surf->xdg_toplevel != NULL,
            (void *)buf,
            buf ? buf->width : 0,
            buf ? buf->height : 0,
            buf ? buf->is_gpu_bo : 0,
            surf->has_pending_buffer,
            surf->committed_buf != NULL);
}

static void xdg_surface_send_initial_configure_if_needed(struct wlcomp_surface *surf)
{
    struct wl_array states;
    uint32_t *s;
    int32_t def_w, def_h;

    if (!surf || !surf->xdg_surface || !surf->xdg_toplevel ||
        surf->xdg_initial_configure_sent)
        return;

    def_w = surf->pending_configure_w > 0 ? surf->pending_configure_w :
        (int32_t)g_fb_w * 4 / 5;
    def_h = surf->pending_configure_h > 0 ? surf->pending_configure_h :
        ((int32_t)g_fb_h - TASKBAR_H) * 4 / 5;
    if (def_w < 400) def_w = (int32_t)g_fb_w;
    if (def_h < 300) def_h = (int32_t)g_fb_h - TASKBAR_H;

    wl_array_init(&states);
    s = wl_array_add(&states, sizeof(uint32_t));
    if (s)
        *s = XDG_TOPLEVEL_STATE_ACTIVATED;

    xdg_toplevel_send_configure(surf->xdg_toplevel, def_w, def_h, &states);
    wl_array_release(&states);
    xdg_surface_send_configure(surf->xdg_surface, ++g_serial);
    surf->xdg_initial_configure_sent = 1;

    fprintf(stderr,
            "wlcomp: xdg initial configure pid=%d serial=%u size=%dx%d\n",
            surf->client_pid, g_serial, def_w, def_h);
}

/* Client cursor state */
static struct wlcomp_surface *g_cursor_surface;   /* client-provided cursor */
static int32_t g_cursor_hotspot_x, g_cursor_hotspot_y;

/* Interactive move/resize state for Wayland surfaces */
static struct wlcomp_surface *g_grab_surface;  /* surface being moved/resized */
static int g_grab_mode;  /* 0=none, 1=move, 2=resize */
static uint32_t g_grab_edges;  /* resize edges bitmask */
static int32_t g_grab_start_mx, g_grab_start_my;  /* cursor pos at grab start */
static int32_t g_grab_start_x, g_grab_start_y;    /* surface pos at grab start */

#define WAYLAND_CLOSE_SZ 18
static int32_t g_grab_start_w, g_grab_start_h;    /* surface size at grab start */

#define WAYLAND_DEMO_TITLE_H 24
#define WAYLAND_DEMO_BORDER  2
#define WAYLAND_DEMO_SHADOW  3

static void cancel_surface_grab(struct wlcomp_surface *surf)
{
    if (g_grab_surface == surf) {
        g_grab_surface = NULL;
        g_grab_mode = 0;
        g_grab_edges = 0;
    }
}

static int surface_is_demo_window(const struct wlcomp_surface *surf)
{
    return surf && (strcmp(surf->app_id, "mesaglsmoke") == 0 ||
                    strcmp(surf->app_id, "mesawlegl") == 0);
}

static int surface_is_webkit_window(const struct wlcomp_surface *surf)
{
    return surf && (strcmp(surf->app_id, "MiniBrowser") == 0 ||
                    strcmp(surf->app_id, "webkitgpusmoke") == 0);
}

static int frame_callback_interval_ms(void)
{
    static int initialized;
    static int interval_ms = 16;
    const char *env;

    if (initialized)
        return interval_ms;
    initialized = 1;
    env = getenv("XV6_WLCOMP_FRAME_MS");
    if (env && *env) {
        interval_ms = atoi(env);
        if (interval_ms < 0)
            interval_ms = 0;
        if (interval_ms > 1000)
            interval_ms = 1000;
    }
    return interval_ms;
}

static int frame_callbacks_due(uint32_t now)
{
    static uint32_t next_frame_ms;
    int interval_ms = frame_callback_interval_ms();

    if (interval_ms <= 0)
        return 1;
    if (next_frame_ms == 0 || now >= next_frame_ms) {
        next_frame_ms = now + (uint32_t)interval_ms;
        return 1;
    }
    return 0;
}

static int surface_has_unpresented_d3d12_buffer(struct wlcomp_surface *surf)
{
    return surf && surf->committed_buf &&
           surf->committed_buf->is_d3d12_shared &&
           !surf->committed_buf->d3d12_present_complete;
}

static void damage_all_frame_callbacks(uint32_t now)
{
    struct wlcomp_surface *cb_surf;

    if (!frame_callbacks_due(now))
        return;

    wl_list_for_each(cb_surf, &g_surfaces, link) {
        struct wlcomp_frame_callback *cb;
        struct wlcomp_frame_callback *tmp;

        if (surface_has_unpresented_d3d12_buffer(cb_surf))
            continue;
        wl_list_for_each_safe(cb, tmp, &cb_surf->frame_callbacks, link) {
            xv6_d3d12_observe_frame_callback(cb_surf, now);
            wl_callback_send_done(cb->resource, now);
            wl_resource_destroy(cb->resource);
            wl_list_remove(&cb->link);
            free(cb);
        }
    }
}

static int any_frame_callbacks_pending(void)
{
    struct wlcomp_surface *surf;

    wl_list_for_each(surf, &g_surfaces, link) {
        if (!wl_list_empty(&surf->frame_callbacks))
            return 1;
    }
    return 0;
}

static struct wlcomp_surface *surface_from_resource(struct wl_resource *r)
{
    return (struct wlcomp_surface *)wl_resource_get_user_data(r);
}

static struct wlcomp_surface *surface_root(struct wlcomp_surface *surf)
{
    while (surf && surf->parent)
        surf = surf->parent;
    return surf;
}

static int surface_tree_visible(const struct wlcomp_surface *surf)
{
    while (surf) {
        if (!surf->mapped || surf->minimized)
            return 0;
        surf = surf->parent;
    }
    return 1;
}

static void surface_absolute_position(const struct wlcomp_surface *surf,
                                      int32_t *x, int32_t *y)
{
    if (!surf) {
        *x = 0;
        *y = 0;
        return;
    }
    if (surf->parent) {
        int32_t px, py;
        surface_absolute_position(surf->parent, &px, &py);
        *x = px + surf->sub_x;
        *y = py + surf->sub_y;
    } else {
        *x = surf->x;
        *y = surf->y;
    }
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

static inline void blend_pixel(uint32_t *dst, struct wlcomp_buffer *buf,
                               uint32_t src_pixel)
{
    uint32_t pixel = buffer_pixel_argb(buf, src_pixel);
    uint32_t a = buffer_pixel_alpha(buf, pixel);

    if (a == 0xFF) {
        *dst = pixel;
    } else if (a > 0) {
        uint32_t bg = *dst;
        uint32_t inv_a = 255 - a;
        uint32_t r, g, b;

        if (buf->format == WL_SHM_FORMAT_ARGB8888) {
            /* Wayland ARGB8888 is premultiplied. */
            r = ((pixel >> 16) & 0xFF) + (((bg >> 16) & 0xFF) * inv_a) / 255;
            g = ((pixel >> 8) & 0xFF) + (((bg >> 8) & 0xFF) * inv_a) / 255;
            b = (pixel & 0xFF) + ((bg & 0xFF) * inv_a) / 255;
        } else {
            r = (((pixel >> 16) & 0xFF) * a +
                 ((bg >> 16) & 0xFF) * inv_a) / 255;
            g = (((pixel >> 8) & 0xFF) * a +
                 ((bg >> 8) & 0xFF) * inv_a) / 255;
            b = ((pixel & 0xFF) * a + (bg & 0xFF) * inv_a) / 255;
        }
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        *dst = 0xFF000000 | (r << 16) | (g << 8) | b;
    }
}

static inline uint8_t clamp_u8(int value)
{
    if (value < 0)
        return 0;
    if (value > 255)
        return 255;
    return (uint8_t)value;
}

static inline uint32_t nv12_pixel_argb(struct wlcomp_buffer *buf,
                                       int32_t x,
                                       int32_t y)
{
    const uint8_t *plane_y = buffer_plane_data(buf, 0);
    const uint8_t *plane_uv = buffer_plane_data(buf, 1);
    int yy = plane_y[y * buf->plane_stride[0] + x];
    int uv_index = (y / 2) * buf->plane_stride[1] + (x & ~1);
    int u = plane_uv[uv_index] - 128;
    int v = plane_uv[uv_index + 1] - 128;
    int c = yy - 16;
    int r, g, b;

    if (c < 0)
        c = 0;
    r = (298 * c + 409 * v + 128) >> 8;
    g = (298 * c - 100 * u - 208 * v + 128) >> 8;
    b = (298 * c + 516 * u + 128) >> 8;
    return 0xFF000000u | ((uint32_t)clamp_u8(r) << 16) |
           ((uint32_t)clamp_u8(g) << 8) | clamp_u8(b);
}

static void surface_window_geometry(const struct wlcomp_surface *s,
                                    int32_t *gx, int32_t *gy,
                                    int32_t *gw, int32_t *gh)
{
    int32_t bw = s && s->committed_buf ? s->committed_buf->width : 1;
    int32_t bh = s && s->committed_buf ? s->committed_buf->height : 1;

    if (s && s->has_window_geometry && s->window_w > 0 && s->window_h > 0) {
        *gx = s->window_x;
        *gy = s->window_y;
        *gw = s->window_w;
        *gh = s->window_h;
    } else {
        *gx = 0;
        *gy = 0;
        *gw = bw;
        *gh = bh;
    }
}

static void damage_surface_self(const struct wlcomp_surface *s)
{
    int32_t gx, gy, gw, gh;
    int32_t ax, ay;
    struct wlcomp_buffer *buf;
    int frame_top = 0;
    int frame_border = 0;

    if (!s || !surface_tree_visible(s) || !s->committed_buf)
        return;

    buf = s->committed_buf;
    surface_window_geometry(s, &gx, &gy, &gw, &gh);
    surface_absolute_position(s, &ax, &ay);
    if (surface_is_demo_window(s)) {
        frame_top = WAYLAND_DEMO_TITLE_H;
        frame_border = WAYLAND_DEMO_BORDER;
    }
    (void)buf;
    damage_rect(ax - frame_border, ay - frame_top - frame_border,
                gw + frame_border * 2 +
                    (surface_is_demo_window(s) ? WAYLAND_DEMO_SHADOW : 0),
                gh + frame_top + frame_border * 2 +
                    (surface_is_demo_window(s) ? WAYLAND_DEMO_SHADOW : 0));
}

static void damage_surface(const struct wlcomp_surface *s)
{
    struct wlcomp_surface *child;

    damage_surface_self(s);
    wl_list_for_each(child, &g_surfaces, link) {
        if (child->parent == s)
            damage_surface(child);
    }
}

static void surface_pending_damage_clear(struct wlcomp_surface *surf)
{
    if (surf)
        surf->pending_damage_count = 0;
}

static void surface_pending_damage_add(struct wlcomp_surface *surf,
                                       int32_t x, int32_t y,
                                       int32_t w, int32_t h)
{
    struct damage_rect r;
    int64_t x2;
    int64_t y2;

    if (!surf || w <= 0 || h <= 0)
        return;

    x2 = (int64_t)x + w;
    y2 = (int64_t)y + h;
    if (x2 <= x || y2 <= y)
        return;

    if (x < INT32_MIN) x = INT32_MIN;
    if (y < INT32_MIN) y = INT32_MIN;
    if (x2 > INT32_MAX) x2 = INT32_MAX;
    if (y2 > INT32_MAX) y2 = INT32_MAX;

    r.x1 = x;
    r.y1 = y;
    r.x2 = (int32_t)x2;
    r.y2 = (int32_t)y2;

    for (int i = 0; i < surf->pending_damage_count; i++) {
        if (rects_touch_or_overlap(&surf->pending_damage[i], &r)) {
            rect_merge(&surf->pending_damage[i], &r);
            for (int j = 0; j < surf->pending_damage_count; j++) {
                if (j != i &&
                    rects_touch_or_overlap(&surf->pending_damage[i],
                                           &surf->pending_damage[j])) {
                    rect_merge(&surf->pending_damage[i],
                               &surf->pending_damage[j]);
                    surf->pending_damage[j] =
                        surf->pending_damage[surf->pending_damage_count - 1];
                    surf->pending_damage_count--;
                    j--;
                }
            }
            return;
        }
    }

    if (surf->pending_damage_count < MAX_SURFACE_DAMAGE_RECTS) {
        surf->pending_damage[surf->pending_damage_count++] = r;
    } else {
        surf->pending_damage[surf->pending_damage_count - 1] = r;
        for (int i = 1; i < surf->pending_damage_count; i++)
            rect_merge(&surf->pending_damage[0], &surf->pending_damage[i]);
        surf->pending_damage_count = 1;
    }
}

static int damage_surface_pending(struct wlcomp_surface *surf)
{
    int32_t gx, gy, gw, gh;
    int32_t ax, ay;
    int32_t draw_x, draw_y;
    int damaged = 0;

    if (!surf || surf->pending_damage_count <= 0 ||
        !surface_tree_visible(surf) || !surf->committed_buf)
        return 0;

    surface_window_geometry(surf, &gx, &gy, &gw, &gh);
    if (surf->has_window_geometry &&
        (gw != surf->committed_buf->width ||
         gh != surf->committed_buf->height)) {
        damage_surface_self(surf);
        return 1;
    }
    surface_absolute_position(surf, &ax, &ay);
    draw_x = ax - gx;
    draw_y = ay - gy;

    for (int i = 0; i < surf->pending_damage_count; i++) {
        int32_t x1 = surf->pending_damage[i].x1;
        int32_t y1 = surf->pending_damage[i].y1;
        int32_t x2 = surf->pending_damage[i].x2;
        int32_t y2 = surf->pending_damage[i].y2;

        if (x1 < gx) x1 = gx;
        if (y1 < gy) y1 = gy;
        if (x2 > gx + gw) x2 = gx + gw;
        if (y2 > gy + gh) y2 = gy + gh;
        if (x1 < 0) x1 = 0;
        if (y1 < 0) y1 = 0;
        if (x2 > surf->committed_buf->width) x2 = surf->committed_buf->width;
        if (y2 > surf->committed_buf->height) y2 = surf->committed_buf->height;
        if (x1 >= x2 || y1 >= y2)
            continue;

        damage_rect(draw_x + x1, draw_y + y1, x2 - x1, y2 - y1);
        damaged = 1;
    }

    return damaged;
}

static void cursor_bounds_at(int32_t cx, int32_t cy,
                             int32_t *x, int32_t *y,
                             int32_t *w, int32_t *h)
{
    *w = 12;
    *h = 19;
    *x = cx;
    *y = cy;
    if (*x + *w > (int32_t)g_fb_w) *x = (int32_t)g_fb_w - *w;
    if (*y + *h > (int32_t)g_fb_h) *y = (int32_t)g_fb_h - *h;
    if (*x < 0) *x = 0;
    if (*y < 0) *y = 0;
}

static void damage_cursor_at(int32_t cx, int32_t cy)
{
    int32_t x, y, w, h;

    cursor_bounds_at(cx, cy, &x, &y, &w, &h);
    damage_rect(x - 1, y - 1, w + 2, h + 2);
}

static void surface_raise_to_top(struct wlcomp_surface *surf)
{
    surf = surface_root(surf);
    if (!surf)
        return;

    wl_list_remove(&surf->link);
    wl_list_insert(g_surfaces.prev, &surf->link);
}

/* Find top surface at a point */
static struct wlcomp_surface *surface_at(int32_t px, int32_t py,
                                          int32_t *sx, int32_t *sy)
{
    /* Reserve taskbar area — never hit Wayland surfaces there */
    if (py >= (int32_t)(g_fb_h - 36 /* TASKBAR_H */))
        return NULL;

    struct wlcomp_surface *s;
    /* Walk reversed (top first). Topmost surfaces live at the list tail. */
    wl_list_for_each_reverse(s, &g_surfaces, link) {
        if (!surface_tree_visible(s) || !s->committed_buf || s->is_cursor)
            continue;
        int32_t gx, gy, gw, gh;
        int32_t ax, ay;
        int32_t draw_x, draw_y;
        surface_window_geometry(s, &gx, &gy, &gw, &gh);
        surface_absolute_position(s, &ax, &ay);
        draw_x = ax - gx;
        draw_y = ay - gy;
        if (surface_is_demo_window(s)) {
            int frame_x = ax - WAYLAND_DEMO_BORDER;
            int frame_y = ay - WAYLAND_DEMO_TITLE_H - WAYLAND_DEMO_BORDER;
            int frame_w = gw + WAYLAND_DEMO_BORDER * 2;
            int frame_h = gh + WAYLAND_DEMO_TITLE_H + WAYLAND_DEMO_BORDER * 2;
            if (px < frame_x || px >= frame_x + frame_w ||
                py < frame_y || py >= frame_y + frame_h)
                continue;
        } else {
            if (px < ax || px >= ax + gw ||
                py < ay || py >= ay + gh)
                continue;
        }
        if (sx) *sx = px - draw_x;
        if (sy) *sy = py - draw_y;
        return s;
    }
    return NULL;
}
