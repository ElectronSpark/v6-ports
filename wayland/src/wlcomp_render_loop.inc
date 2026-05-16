static uint32_t get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void paint_scene(int fb_w, int fb_h, int *acquire_blocked)
{
    struct wlcomp_surface *surf;

    /* Draw desktop wallpaper */
    wlcomp_desktop_draw_wallpaper(g_fb_buf, fb_w, fb_h);

    /* Draw desktop icons */
    wlcomp_desktop_draw_icons(g_fb_buf, fb_w, fb_h, g_selected_icon);

    /* Draw background layer: internal windows behind Wayland when no iwin focused */
    if (g_iwin_focus < 0)
        draw_iwin_all(g_fb_buf, fb_w, fb_h);

    /* Draw client (Wayland) surfaces */
    wl_list_for_each(surf, &g_surfaces, link) {
        if (!surface_tree_visible(surf) || !surf->committed_buf ||
            surf->is_cursor)
            continue;

        struct wlcomp_buffer *buf = surf->committed_buf;
        uint8_t *src_bytes;
        uint32_t *src;
        if (!buffer_acquire_ready(buf)) {
            *acquire_blocked = 1;
            continue;
        }
        src_bytes = buffer_data(buf);
        if (!src_bytes) continue;
        src = (uint32_t *)src_bytes;

        int32_t bw = buf->width;
        int32_t bh = buf->height;
        int32_t src_stride_px = buf->plane_stride[0] / 4;
        int32_t gx, gy, gw, gh;
        int32_t ax, ay;
        int32_t draw_x, draw_y;
        int32_t row0 = 0, row1 = bh;
        int32_t col0 = 0, col1 = bw;

        surface_window_geometry(surf, &gx, &gy, &gw, &gh);
        surface_absolute_position(surf, &ax, &ay);
        draw_x = ax - gx;
        draw_y = ay - gy;
        if (surf->has_window_geometry) {
            row0 = gy < 0 ? 0 : gy;
            col0 = gx < 0 ? 0 : gx;
            row1 = gy + gh;
            col1 = gx + gw;
            if (row1 > bh) row1 = bh;
            if (col1 > bw) col1 = bw;
            if (row0 > row1) row0 = row1;
            if (col0 > col1) col0 = col1;
        }

        if (surface_is_demo_window(surf)) {
            int title_h = WAYLAND_DEMO_TITLE_H;
            int border = WAYLAND_DEMO_BORDER;
            int frame_x = ax - border;
            int frame_y = ay - title_h - border;
            int frame_w = gw + border * 2;
            int frame_h = gh + title_h + border * 2;
            int focused = surface_is_foreground(surf);
            int close_x = frame_x + frame_w - WAYLAND_CLOSE_SZ - 5;
            int close_y = frame_y + border + 3;
            uint32_t title_color = focused ? 0xFF2C4F7C : 0xFF2A2E35;
            uint32_t border_color = focused ? 0xFF5F7EA8 : 0xFF3C5078;
            const char *title = surf->title[0] ? surf->title : "3D Demo";

            draw_rect(g_fb_buf, fb_w, fb_h, frame_x + WAYLAND_DEMO_SHADOW,
                      frame_y + WAYLAND_DEMO_SHADOW,
                      frame_w, frame_h, 0xFF101418);
            draw_rect(g_fb_buf, fb_w, fb_h, frame_x, frame_y,
                      frame_w, frame_h, 0xFF15191F);
            draw_rect(g_fb_buf, fb_w, fb_h, frame_x + border,
                      frame_y + border, gw, title_h, title_color);
            draw_string(g_fb_buf, fb_w, fb_h, frame_x + 10,
                        frame_y + border + 4, title, 0xFFD2DAE2, 1);
            draw_rect(g_fb_buf, fb_w, fb_h, close_x, close_y,
                      WAYLAND_CLOSE_SZ, WAYLAND_CLOSE_SZ, 0xFFA63D3D);
            draw_char(g_fb_buf, fb_w, fb_h, close_x + 5, close_y + 1,
                      'x', 0xFFFFFFFF, 1);
            draw_rect(g_fb_buf, fb_w, fb_h, frame_x, frame_y,
                      frame_w, border, border_color);
            draw_rect(g_fb_buf, fb_w, fb_h, frame_x,
                      frame_y + frame_h - border, frame_w, border,
                      border_color);
            draw_rect(g_fb_buf, fb_w, fb_h, frame_x, frame_y,
                      border, frame_h, border_color);
            draw_rect(g_fb_buf, fb_w, fb_h,
                      frame_x + frame_w - border, frame_y,
                      border, frame_h, border_color);
        }

        if (g_paint_clip_enabled) {
            if (row0 < g_paint_clip.y1 - draw_y)
                row0 = g_paint_clip.y1 - draw_y;
            if (row1 > g_paint_clip.y2 - draw_y)
                row1 = g_paint_clip.y2 - draw_y;
            if (col0 < g_paint_clip.x1 - draw_x)
                col0 = g_paint_clip.x1 - draw_x;
            if (col1 > g_paint_clip.x2 - draw_x)
                col1 = g_paint_clip.x2 - draw_x;
        }

        if (row0 < 0) row0 = 0;
        if (col0 < 0) col0 = 0;
        if (row1 > bh) row1 = bh;
        if (col1 > bw) col1 = bw;
        if (row0 < -draw_y) row0 = -draw_y;
        if (col0 < -draw_x) col0 = -draw_x;
        if (row1 > (int32_t)g_fb_h - draw_y)
            row1 = (int32_t)g_fb_h - draw_y;
        if (col1 > (int32_t)g_fb_w - draw_x)
            col1 = (int32_t)g_fb_w - draw_x;
        if (row0 >= row1 || col0 >= col1)
            continue;

        if (gpu_compose_present_buffer(buf, draw_x, draw_y,
                                       row0, row1, col0, col1))
            continue;

        if (buf->format == DRM_FORMAT_NV12) {
            for (int32_t row = row0; row < row1; row++) {
                int32_t dy = draw_y + row;
                int32_t src_row = buf->y_inverted ? (bh - 1 - row) : row;
                if (dy < 0 || dy >= (int32_t)g_fb_h) continue;
                for (int32_t col = col0; col < col1; col++) {
                    int32_t dx = draw_x + col;
                    if (dx < 0 || dx >= (int32_t)g_fb_w) continue;
                    g_fb_buf[dy * g_fb_w + dx] =
                        nv12_pixel_argb(buf, col, src_row);
                }
            }
            continue;
        }

        if (buf->format == WL_SHM_FORMAT_XRGB8888) {
            size_t copy_bytes = (size_t)(col1 - col0) * sizeof(uint32_t);

            for (int32_t row = row0; row < row1; row++) {
                int32_t dy = draw_y + row;
                int32_t dx = draw_x + col0;
                int32_t src_row = buf->y_inverted ? (bh - 1 - row) : row;

                memcpy(&g_fb_buf[dy * g_fb_w + dx],
                       &src[src_row * src_stride_px + col0], copy_bytes);
            }
            continue;
        }

        for (int32_t row = row0; row < row1; row++) {
            int32_t dy = draw_y + row;
            int32_t src_row = buf->y_inverted ? (bh - 1 - row) : row;
            if (dy < 0 || dy >= (int32_t)g_fb_h) continue;
            for (int32_t col = col0; col < col1; col++) {
                int32_t dx = draw_x + col;
                if (dx < 0 || dx >= (int32_t)g_fb_w) continue;
                blend_pixel(&g_fb_buf[dy * g_fb_w + dx], buf,
                            src[src_row * src_stride_px + col]);
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

    /* Draw one compositor-owned cursor. */
    draw_default_cursor();
}

static void composite_and_flip(void)
{
    int fb_w = (int)g_fb_w;
    int fb_h = (int)g_fb_h;
    static uint32_t next_clock_damage_ms;
    static uint32_t next_repair_damage_ms;
    static uint32_t startup_full_until_ms;
    static uint32_t next_startup_full_ms;
    uint32_t now = get_time_ms();
    int repair_ms = damage_repair_interval_ms();

    damage_stats_maybe_log(now);
    fps_probe_maybe_write(now);

    /* Lay out icons if not done */
    wlcomp_desktop_layout_icons(fb_w, fb_h);

    if (now >= next_clock_damage_ms) {
        damage_taskbar();
        next_clock_damage_ms = now + 1000;
    }

    if (repair_ms > 0 && now >= next_repair_damage_ms) {
        damage_full_reason(FULL_DAMAGE_REPAIR);
        next_repair_damage_ms = now + (uint32_t)repair_ms;
    }

    /*
     * Hyper-V synthvid can come online a little after fb0 is registered and
     * after wlcomp has already drawn its first desktop frame.  Keep issuing a
     * few full-frame presents during startup so the firmware boot logo cannot
     * survive behind tiny incremental updates.
     */
    if (startup_full_until_ms == 0) {
        startup_full_until_ms = now + 3000;
        next_startup_full_ms = now;
    }
    if (now <= startup_full_until_ms && now >= next_startup_full_ms) {
        damage_full_reason(FULL_DAMAGE_INIT);
        next_startup_full_ms = now + 250;
    }

    if (!damage_has_any()) {
        damage_all_frame_callbacks(now);
        return;
    }
    damage_cursor_at(g_cursor_x, g_cursor_y);
    tune_damage_for_present();

    int acquire_blocked = 0;
    int paint_count = g_damage_count;
    struct damage_rect paint_rects[MAX_DAMAGE_RECTS];
    struct wlcomp_surface *surf;

    for (int i = 0; i < paint_count; i++)
        paint_rects[i] = g_damage[i];

    for (int i = 0; i < paint_count; i++) {
        struct wlcomp_draw_clip draw_clip;
        g_paint_clip = paint_rects[i];
        g_paint_clip_enabled = 1;
        draw_clip.x1 = g_paint_clip.x1;
        draw_clip.y1 = g_paint_clip.y1;
        draw_clip.x2 = g_paint_clip.x2;
        draw_clip.y2 = g_paint_clip.y2;
        wlcomp_draw_set_clip(&draw_clip);
        paint_scene(fb_w, fb_h, &acquire_blocked);
    }
    g_paint_clip_enabled = 0;
    wlcomp_draw_clear_clip();

    present_damage_rects(fb_w, now);
    if (acquire_blocked) {
        g_damage_stats.acquire_blocked_frames++;
        damage_full_reason(FULL_DAMAGE_ACQUIRE);
    }

    flush_buffer_releases_after_present();

    /* Once the frame is copied into fb0 and the present fence has signaled,
     * release committed buffers so clients can recycle their storage.  The
     * buffer_released flag keeps replacement commits from sending duplicate
     * releases for the same wl_buffer. */
    int present_ready = framebuffer_present_fence_ready();
    int callbacks_due = frame_callbacks_due(now);
    wl_list_for_each(surf, &g_surfaces, link) {
        int ready = !surf->committed_buf ||
                    (present_ready &&
                     buffer_acquire_ready(surf->committed_buf));

        if (ready)
            surface_release_committed_buffer(surf);
        if (ready && callbacks_due) {
            struct wlcomp_frame_callback *cb;
            struct wlcomp_frame_callback *tmp;

            wl_list_for_each_safe(cb, tmp, &surf->frame_callbacks, link) {
                wl_callback_send_done(cb->resource, now);
                wl_resource_destroy(cb->resource);
                wl_list_remove(&cb->link);
                free(cb);
            }
        }
    }
    flush_buffer_releases_after_present();
    if (!present_ready)
        damage_full_reason(FULL_DAMAGE_ACQUIRE);
}

