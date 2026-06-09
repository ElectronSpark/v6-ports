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

static void surface_set_keyboard_focus(struct wlcomp_surface *surf)
{
    if (g_kbd_focused == surf)
        return;

    if (g_kbd_focused && g_kbd_focused->resource) {
        struct wl_client *old_client =
            wl_resource_get_client(g_kbd_focused->resource);
        struct wl_resource *kbd = find_resource_for_client(
            g_keyboard_resources, MAX_INPUT_RES, old_client);
        if (kbd)
            wl_keyboard_send_leave(kbd, ++g_serial,
                                   g_kbd_focused->resource);
    }

    g_kbd_focused = surf;
    g_kbd_enter_pending = 0;

    if (g_kbd_focused && g_kbd_focused->resource) {
        struct wl_client *new_client =
            wl_resource_get_client(g_kbd_focused->resource);
        struct wl_resource *kbd = find_resource_for_client(
            g_keyboard_resources, MAX_INPUT_RES, new_client);
        if (kbd) {
            struct wl_array keys;
            wl_array_init(&keys);
            wl_keyboard_send_enter(kbd, ++g_serial,
                                   g_kbd_focused->resource, &keys);
            wl_array_release(&keys);
        }
    }

    damage_taskbar();
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
    if (!g_hw_cursor)
        damage_cursor_at(g_cursor_x, g_cursor_y);
    if (surface) {
        struct wlcomp_surface *surf = surface_from_resource(surface);
        if (surf) {
            if (g_cursor_surface && g_cursor_surface != surf)
                g_cursor_surface->is_cursor = 0;
            surf->is_cursor = 1;
            g_cursor_surface = surf;
            g_cursor_hotspot_x = hotspot_x;
            g_cursor_hotspot_y = hotspot_y;
            if (hw_cursor_client_images_enabled())
                (void)hw_cursor_upload_surface(surf);
            else
                (void)hw_cursor_upload_default_arrow();
        }
    } else {
        /* NULL surface = use default cursor */
        if (g_cursor_surface) {
            g_cursor_surface->is_cursor = 0;
            g_cursor_surface = NULL;
        }
        (void)hw_cursor_upload_default_arrow();
    }
    if (!g_hw_cursor)
        damage_cursor_at(g_cursor_x, g_cursor_y);
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

    wlcomp_keymap_send(res);
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

#define MAX_OUTPUT_RES 16
static struct wl_resource *g_output_resources[MAX_OUTPUT_RES];

static void surface_send_output_enter(struct wlcomp_surface *surf)
{
    struct wl_client *client;

    if (!surf || !surf->resource)
        return;

    client = wl_resource_get_client(surf->resource);
    for (int i = 0; i < MAX_OUTPUT_RES; i++) {
        struct wl_resource *output = g_output_resources[i];

        if (!output || wl_resource_get_client(output) != client)
            continue;
        wl_surface_send_enter(surf->resource, output);
    }
}

static void output_release(struct wl_client *c, struct wl_resource *r)
{
    (void)c;
    wl_resource_destroy(r);
}

static const struct wl_output_interface output_impl = {
    .release = output_release,
};

static void output_destroy_handler(struct wl_resource *resource)
{
    remove_resource(g_output_resources, MAX_OUTPUT_RES, resource);
}

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
    wl_resource_set_implementation(res, &output_impl, NULL,
                                  output_destroy_handler);
    add_resource(g_output_resources, MAX_OUTPUT_RES, res);

    wl_output_send_geometry(res, 0, 0, 0, 0,
                            WL_OUTPUT_SUBPIXEL_UNKNOWN,
                            "xv6", "BochsVGA",
                            WL_OUTPUT_TRANSFORM_NORMAL);
    wl_output_send_mode(res, WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED,
                        (int32_t)g_fb_w, (int32_t)g_fb_h, 60000);
    if (version >= 2)
        wl_output_send_scale(res, 1);
    if (version >= 2)
        wl_output_send_done(res);

    struct wlcomp_surface *surf;
    wl_list_for_each(surf, &g_surfaces, link) {
        if (surf->resource && wl_resource_get_client(surf->resource) == client)
            wl_surface_send_enter(surf->resource, res);
    }
}
