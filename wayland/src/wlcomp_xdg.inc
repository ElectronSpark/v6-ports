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
    if (title) {
        int fd = open("/tmp/webkit-title", O_WRONLY | O_CREAT | O_TRUNC, 0644);

        if (fd >= 0) {
            write(fd, title, strlen(title));
            write(fd, "\n", 1);
            close(fd);
        }
    }
    const char *title_log = getenv("XV6_WLCOMP_TITLE_LOG");
    if (title_log && title_log[0] && strcmp(title_log, "0") != 0)
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
    if (surf && app_id && strcmp(app_id, "mesawlegl") == 0 &&
        !surf->xdg_initial_configure_sent) {
        int32_t w = 640;
        int32_t h = 480;
        int32_t max_w = (int32_t)g_fb_w - 32;
        int32_t max_h = (int32_t)g_fb_h - TASKBAR_H - 32;

        if (max_w < 320)
            max_w = (int32_t)g_fb_w;
        if (max_h < 240)
            max_h = (int32_t)g_fb_h - TASKBAR_H;
        if (w > max_w)
            w = max_w;
        if (h > max_h)
            h = max_h;
        surf->pending_configure_w = w;
        surf->pending_configure_h = h;
        surf->saved_w = w;
        surf->saved_h = h;
        surf->saved_x = ((int32_t)g_fb_w - w) / 2;
        surf->saved_y = ((int32_t)g_fb_h - TASKBAR_H - h) / 2;
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
    if (!surf) return;

    if (surf->maximized && surf->xdg_surface && surf->xdg_toplevel) {
        int32_t rw = surf->saved_w > 0 ? surf->saved_w :
                     (int32_t)g_fb_w * 4 / 5;
        int32_t rh = surf->saved_h > 0 ? surf->saved_h :
                     ((int32_t)g_fb_h - TASKBAR_H) * 4 / 5;
        int32_t max_x = (int32_t)g_fb_w - 60;
        int32_t max_y = (int32_t)g_fb_h - TASKBAR_H - 1;
        struct wl_array states;
        uint32_t *st;

        damage_surface(surf);
        surf->maximized = 0;
        surf->x = g_cursor_x - rw / 2;
        surf->y = g_cursor_y - 16;
        if (surf->x < -(rw - 60))
            surf->x = -(rw - 60);
        if (surf->x > max_x)
            surf->x = max_x;
        if (surf->y < 0)
            surf->y = 0;
        if (surf->y > max_y)
            surf->y = max_y;

        wl_array_init(&states);
        st = wl_array_add(&states, sizeof(uint32_t));
        *st = XDG_TOPLEVEL_STATE_ACTIVATED;
        xdg_toplevel_send_configure(surf->xdg_toplevel, rw, rh, &states);
        wl_array_release(&states);
        xdg_surface_send_configure(surf->xdg_surface, ++g_serial);
        damage_surface(surf);
    }

    damage_surface(surf);
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
    int32_t gx, gy, gw, gh;
    if (!surf || surf->maximized) return;
    surface_window_geometry(surf, &gx, &gy, &gw, &gh);
    (void)gx;
    (void)gy;
    if (surface_is_demo_window(surf)) {
        surf->demo_resize_requested = 1;
        wlcomp_emit_demo_interaction_matrix(surf, "client-resize-request");
    }
    damage_surface(surf);
    g_grab_surface = surf;
    g_grab_mode = 2; /* resize */
    g_grab_edges = edges;
    g_grab_start_mx = g_cursor_x;
    g_grab_start_my = g_cursor_y;
    g_grab_start_x = surf->x;
    g_grab_start_y = surf->y;
    g_grab_start_w = gw > 0 ? gw : 200;
    g_grab_start_h = gh > 0 ? gh : 200;
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
    int32_t gx, gy, gw, gh;
    if (!surf || !surf->xdg_surface || !surf->xdg_toplevel) return;
    if (surf->maximized) return;  /* already maximized */

    surface_window_geometry(surf, &gx, &gy, &gw, &gh);
    (void)gx;
    (void)gy;
    cancel_surface_grab(surf);
    damage_surface(surf);
    /* Save current geometry for restore */
    surf->saved_x = surf->x;
    surf->saved_y = surf->y;
    surf->saved_w = gw > 0 ? gw : 400;
    surf->saved_h = gh > 0 ? gh : 300;
    surf->maximized = 1;

    /* Send configure with MAXIMIZED + ACTIVATED, full screen minus taskbar */
    struct wl_array states;
    wl_array_init(&states);
    uint32_t *s;
    s = wl_array_add(&states, sizeof(uint32_t));
    *s = XDG_TOPLEVEL_STATE_MAXIMIZED;
    s = wl_array_add(&states, sizeof(uint32_t));
    *s = XDG_TOPLEVEL_STATE_ACTIVATED;
    int32_t max_h = (int32_t)g_fb_h - TASKBAR_H;
    fprintf(stderr,
            "wlcomp: xdg set_maximized pid=%d serial=%u size=%dx%d restore=%dx%d\n",
            surf->client_pid, g_serial + 1, (int32_t)g_fb_w, max_h,
            surf->saved_w, surf->saved_h);
    xdg_toplevel_send_configure(surf->xdg_toplevel, (int32_t)g_fb_w, max_h, &states);
    wl_array_release(&states);
    xdg_surface_send_configure(surf->xdg_surface, ++g_serial);

    /* Snap to top-left */
    surf->x = 0;
    surf->y = 0;
    damage_surface(surf);
}

static void toplevel_unset_maximized(struct wl_client *c, struct wl_resource *r)
{
    (void)c;
    struct wlcomp_surface *surf = wl_resource_get_user_data(r);
    if (!surf || !surf->xdg_surface || !surf->xdg_toplevel) return;
    if (!surf->maximized) return;  /* not maximized */

    cancel_surface_grab(surf);
    damage_surface(surf);
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
    damage_surface(surf);
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
        damage_surface(surf);
        surf->minimized = 1;
        if (g_focused == surf)
            g_focused = NULL;
        if (g_kbd_focused == surf)
            g_kbd_focused = NULL;
        damage_full_reason(FULL_DAMAGE_CLIENT);
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
        fprintf(stderr,
                "wlcomp: xdg toplevel destroy pid=%d mapped=%d title='%s' app_id='%s'\n",
                surf->client_pid, surf->mapped, surf->title, surf->app_id);
        surf->xdg_toplevel = NULL;
        if (!surf->committed_buf)
            surf->mapped = 0;
        damage_full_reason(FULL_DAMAGE_CLIENT);
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

    /* Default to 80% of screen, capped to leave room for taskbar */
    int32_t def_w = (int32_t)g_fb_w * 4 / 5;
    int32_t def_h = ((int32_t)g_fb_h - TASKBAR_H) * 4 / 5;
    if (def_w < 400) def_w = (int32_t)g_fb_w;
    if (def_h < 300) def_h = (int32_t)g_fb_h - TASKBAR_H;
    if (surf) {
        surf->pending_configure_w = def_w;
        surf->pending_configure_h = def_h;
        surf->saved_w = def_w;
        surf->saved_h = def_h;
        surf->saved_x = ((int32_t)g_fb_w - def_w) / 2;
        surf->saved_y = ((int32_t)g_fb_h - TASKBAR_H - def_h) / 2;
    }
    fprintf(stderr,
            "wlcomp: xdg toplevel pending initial configure pid=%d size=%dx%d\n",
            surf ? surf->client_pid : 0, def_w, def_h);
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
    (void)c;
    struct wlcomp_surface *surf = wl_resource_get_user_data(r);
    int geometry_changed;
    if (!surf || w <= 0 || h <= 0)
        return;

    damage_surface(surf);
    geometry_changed = !surf->has_window_geometry ||
        surf->window_x != x || surf->window_y != y ||
        surf->window_w != w || surf->window_h != h;
    surf->has_window_geometry = 1;
    surf->window_x = x;
    surf->window_y = y;
    surf->window_w = w;
    surf->window_h = h;
    if (geometry_changed)
        damage_full_reason(FULL_DAMAGE_RESIZE);
    else
        damage_surface(surf);
}

static void xdg_surface_ack_configure(struct wl_client *c,
                                      struct wl_resource *r,
                                      uint32_t serial)
{
    struct wlcomp_surface *surf = wl_resource_get_user_data(r);

    (void)c;
    fprintf(stderr, "wlcomp: xdg ack_configure pid=%d serial=%u\n",
            surf ? surf->client_pid : 0, serial);
    if (surf && surface_is_demo_window(surf) &&
        surf->demo_resize_configures > 0) {
        surf->demo_resize_acked = 1;
        wlcomp_emit_demo_interaction_matrix(surf, "resize-ack");
    }
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
    if (surf) {
        fprintf(stderr,
                "wlcomp: xdg surface destroy pid=%d mapped=%d title='%s' app_id='%s'\n",
                surf->client_pid, surf->mapped, surf->title, surf->app_id);
        surf->xdg_surface = NULL;
        damage_full_reason(FULL_DAMAGE_CLIENT);
    }
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
