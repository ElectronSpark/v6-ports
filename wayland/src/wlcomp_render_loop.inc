static uint32_t get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static int paint_scaled_window_geometry_buffer(struct wlcomp_surface *surf,
                                               struct wlcomp_buffer *buf,
                                               int32_t draw_x,
                                               int32_t draw_y,
                                               int32_t gw,
                                               int32_t gh)
{
    uint8_t *src_bytes;
    uint32_t *src;
    int32_t bw;
    int32_t bh;
    int32_t src_stride_px;
    int32_t dst_x0;
    int32_t dst_y0;
    int32_t dst_x1;
    int32_t dst_y1;
    int opaque_fast;

    if (!surf->has_window_geometry || gw <= 0 || gh <= 0)
        return 0;

    bw = buf->width;
    bh = buf->height;
    if (bw <= 0 || bh <= 0 || (bw == gw && bh == gh))
        return 0;

    /*
     * Window geometry carries no scale factor: it names a 1:1 sub-rectangle of
     * the buffer.  Client-side-decoration toplevels (GTK/WebKit) commit a
     * buffer that is LARGER than the window geometry -- the extra pixels are
     * the CSD shadow/resize margins around the geometry box.  In that case the
     * geometry is a CROP, not a scale request, so decline here and let the
     * normal 1:1 crop/blit path copy the geometry sub-rect to the screen.
     * Genuine upscaling (e.g. the GPU demo, whose small buffer fills a larger
     * window) has gw > bw / gh > bh, which makes the sub-rect spill outside the
     * buffer and keeps taking the scaling path below.
     */
    {
        int32_t gx = surf->window_x;
        int32_t gy = surf->window_y;

        if (gx >= 0 && gy >= 0 &&
            (int64_t)gx + gw <= bw && (int64_t)gy + gh <= bh)
            return 0;
    }

    src_bytes = buffer_data(buf);
    if (!src_bytes) {
        static int missing_source_logs;

        if (missing_source_logs < 8) {
            fprintf(stderr,
                    "wlcomp: scaled paint missing source buf=%p gpu=%d size=%dx%d geom=%dx%d\n",
                    (void *)buf, buf ? buf->is_gpu_bo : 0, bw, bh, gw, gh);
            missing_source_logs++;
        }
        return 0;
    }
    src = (uint32_t *)src_bytes;
    src_stride_px = buf->plane_stride[0] / 4;
    if (surface_is_demo_window(surf) && getenv("WLCOMP_GPU_PIXEL_PROBE")) {
        static int scaled_probe_logs;

        if (scaled_probe_logs < 12) {
            uint32_t samples[5] = { 0, 0, 0, 0, 0 };
            int nonblack = 0;
            int points[5][2] = {
                { bw / 2, bh / 2 },
                { bw / 4, bh / 4 },
                { (bw * 3) / 4, bh / 4 },
                { bw / 4, (bh * 3) / 4 },
                { (bw * 3) / 4, (bh * 3) / 4 },
            };

            for (int i = 0; i < 5; i++) {
                int sx = points[i][0];
                int sy = points[i][1];
                uint32_t p;

                if (sx < 0) sx = 0;
                if (sx >= bw) sx = bw - 1;
                if (sy < 0) sy = 0;
                if (sy >= bh) sy = bh - 1;
                p = src[sy * src_stride_px + sx];
                samples[i] = p;
                if ((p & 0x00ffffffu) != 0)
                    nonblack++;
            }
            fprintf(stderr,
                    "wlcomp: scaled probe frame=%d buf=%dx%d geom=%dx%d stride=%d nonblack=%d samples=%08x,%08x,%08x,%08x,%08x\n",
                    scaled_probe_logs, bw, bh, gw, gh, buf->plane_stride[0],
                    nonblack, samples[0], samples[1], samples[2],
                    samples[3], samples[4]);
            scaled_probe_logs++;
        }
    }
    opaque_fast =
        buf->format == WL_SHM_FORMAT_XRGB8888 ||
        buf->format == DRM_FORMAT_XRGB8888 ||
        ((buf->format == WL_SHM_FORMAT_ARGB8888 ||
          buf->format == DRM_FORMAT_ARGB8888) &&
         (buf->is_gpu_bo || surface_is_demo_window(surf)));

    dst_x0 = draw_x;
    dst_y0 = draw_y;
    dst_x1 = draw_x + gw;
    dst_y1 = draw_y + gh;
    if (dst_x0 < 0) dst_x0 = 0;
    if (dst_y0 < 0) dst_y0 = 0;
    if (dst_x1 > (int32_t)g_fb_w) dst_x1 = (int32_t)g_fb_w;
    if (dst_y1 > (int32_t)g_fb_h) dst_y1 = (int32_t)g_fb_h;
    if (g_paint_clip_enabled) {
        if (dst_x0 < g_paint_clip.x1) dst_x0 = g_paint_clip.x1;
        if (dst_y0 < g_paint_clip.y1) dst_y0 = g_paint_clip.y1;
        if (dst_x1 > g_paint_clip.x2) dst_x1 = g_paint_clip.x2;
        if (dst_y1 > g_paint_clip.y2) dst_y1 = g_paint_clip.y2;
    }
    if (dst_x0 >= dst_x1 || dst_y0 >= dst_y1)
        return 1;

    if (opaque_fast && gw % bw == 0 && gh % bh == 0) {
        int32_t scale_x = gw / bw;
        int32_t scale_y = gh / bh;

        if (scale_x > 1 && scale_y > 1) {
            static uint32_t *scale_line;
            static size_t scale_line_pixels;
            int32_t sy0 = (dst_y0 - draw_y) / scale_y;
            int32_t sy1 = (dst_y1 - draw_y + scale_y - 1) / scale_y;
            int32_t sx0 = (dst_x0 - draw_x) / scale_x;
            int32_t sx1 = (dst_x1 - draw_x + scale_x - 1) / scale_x;
            size_t needed_pixels = g_fb_w;

            if (sy0 < 0) sy0 = 0;
            if (sx0 < 0) sx0 = 0;
            if (sy1 > bh) sy1 = bh;
            if (sx1 > bw) sx1 = bw;
            if (scale_line_pixels < needed_pixels) {
                uint32_t *new_line = realloc(scale_line,
                                             needed_pixels * sizeof(uint32_t));

                if (!new_line)
                    return 0;
                scale_line = new_line;
                scale_line_pixels = needed_pixels;
            }

            for (int32_t sy = sy0; sy < sy1; sy++) {
                int32_t src_y = buf->y_inverted ? (bh - 1 - sy) : sy;
                int32_t dy0 = draw_y + sy * scale_y;
                int32_t dy1 = dy0 + scale_y;
                int32_t line_x0 = dst_x1;
                int32_t line_x1 = dst_x0;

                if (dy0 < dst_y0) dy0 = dst_y0;
                if (dy1 > dst_y1) dy1 = dst_y1;
                for (int32_t sx = sx0; sx < sx1; sx++) {
                    uint32_t pixel =
                        src[src_y * src_stride_px + sx] | 0xFF000000u;
                    int32_t dx0 = draw_x + sx * scale_x;
                    int32_t dx1 = dx0 + scale_x;

                    if (dx0 < dst_x0) dx0 = dst_x0;
                    if (dx1 > dst_x1) dx1 = dst_x1;
                    if (dx0 < line_x0) line_x0 = dx0;
                    if (dx1 > line_x1) line_x1 = dx1;
                    for (int32_t dx = dx0; dx < dx1; dx++)
                        scale_line[dx] = pixel;
                }
                if (line_x0 >= line_x1)
                    continue;
                for (int32_t dy = dy0; dy < dy1; dy++) {
                    memcpy(&g_fb_buf[dy * g_fb_w + line_x0],
                           &scale_line[line_x0],
                           (size_t)(line_x1 - line_x0) * sizeof(uint32_t));
                }
            }
            return 1;
        }
    }

    int64_t src_y_fp = (((int64_t)(dst_y0 - draw_y) * bh) << 16) / gh;
    int64_t src_x_start_fp = (((int64_t)(dst_x0 - draw_x) * bw) << 16) / gw;
    int64_t src_y_step = ((int64_t)bh << 16) / gh;
    int64_t src_x_step = ((int64_t)bw << 16) / gw;

    for (int32_t dy = dst_y0; dy < dst_y1; dy++, src_y_fp += src_y_step) {
        int32_t src_y = (int32_t)(src_y_fp >> 16);
        int64_t src_x_fp = src_x_start_fp;
        if (src_y < 0) src_y = 0;
        if (src_y >= bh) src_y = bh - 1;
        if (buf->y_inverted)
            src_y = bh - 1 - src_y;

        for (int32_t dx = dst_x0; dx < dst_x1; dx++, src_x_fp += src_x_step) {
            int32_t src_x = (int32_t)(src_x_fp >> 16);
            if (src_x < 0) src_x = 0;
            if (src_x >= bw) src_x = bw - 1;

            if (buf->format == DRM_FORMAT_NV12) {
                g_fb_buf[dy * g_fb_w + dx] =
                    nv12_pixel_argb(buf, src_x, src_y);
            } else if (opaque_fast) {
                g_fb_buf[dy * g_fb_w + dx] =
                    src[src_y * src_stride_px + src_x] | 0xFF000000u;
            } else {
                blend_pixel(&g_fb_buf[dy * g_fb_w + dx], buf,
                            src[src_y * src_stride_px + src_x]);
            }
        }
    }
    return 1;
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

        if (buf->is_d3d12_shared) {
            if (!xv6_d3d12_present_imported_buffer(
                    surf, buf, draw_x + col0, draw_y + row0,
                    col0, row0, (uint32_t)(col1 - col0),
                    (uint32_t)(row1 - row0))) {
                *acquire_blocked = 1;
                continue;
            }
            continue;
        }

        src_bytes = buffer_data(buf);
        if (!src_bytes) continue;
        src = (uint32_t *)src_bytes;

        if (paint_scaled_window_geometry_buffer(surf, buf, draw_x, draw_y,
                                                gw, gh))
            continue;

        if (gpu_compose_can_present_surface(surf, buf) &&
            gpu_compose_present_buffer(buf, draw_x, draw_y,
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

        if (buf->format == WL_SHM_FORMAT_XRGB8888 ||
            buf->format == DRM_FORMAT_XRGB8888 ||
            ((buf->format == WL_SHM_FORMAT_ARGB8888 ||
              buf->format == DRM_FORMAT_ARGB8888) &&
             (buf->is_gpu_bo || surface_is_demo_window(surf)))) {
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

static void release_cpu_copied_buffers_after_paint(void)
{
    struct wlcomp_surface *surf;

    wl_list_for_each(surf, &g_surfaces, link) {
        struct wlcomp_buffer *buf = surf->committed_buf;
        int32_t gx, gy, gw, gh;

        if (!buf || surf->buffer_released || buf->release_pending)
            continue;
        if (buf->is_d3d12_shared)
            continue;
        /*
         * Plain CPU/shm buffers are re-read by FULL_DAMAGE_REPAIR (and any
         * clipped repaint) on later frames.  Releasing them right after the
         * first paint lets the client recycle the buffer and, with damage
         * tracking, repaint only the changed region -- corrupting the
         * undamaged areas we still re-read (e.g. a CSD toolbar turns
         * transparent and the desktop bleeds through).  Hold these buffers
         * until the client commits a replacement; the commit handler already
         * releases the previous buffer (standard double buffering).
         */
        if (!buf->is_gpu_bo)
            continue;
        if (buf->is_gpu_bo) {
            surface_window_geometry(surf, &gx, &gy, &gw, &gh);
            if (!surf->has_window_geometry ||
                (buf->width == gw && buf->height == gh))
                continue;
        }
        if (!buffer_acquire_ready(buf))
            continue;

        buffer_send_immediate_release_events(buf);
        surf->buffer_released = 1;
        buf->release_pending = 0;
        buffer_maybe_free(buf);
    }
}

static void damage_fast_gpu_windows(uint32_t now)
{
    static uint32_t next_gpu_damage_ms;
    struct wlcomp_surface *surf;
    int damaged = 0;

    if (now < next_gpu_damage_ms)
        return;

    wl_list_for_each(surf, &g_surfaces, link) {
        if (!surface_tree_visible(surf) || !surf->committed_buf ||
            !surf->committed_buf->is_gpu_bo || !surface_is_demo_window(surf))
            continue;
        damage_surface(surf);
        damaged = 1;
    }

    if (damaged)
        next_gpu_damage_ms = now + 16;
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
    damage_fast_gpu_windows(now);

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

    /*
     * A wallpaper-exposing full repaint may have wiped the static chrome of a
     * mapped surface that was transiently absent (or had not yet committed its
     * chrome) at the time.  Such regions are never re-damaged by the client.
     * Force a full-geometry re-damage of every mapped surface until at least
     * one is actually available to repaint, so the wallpaper cannot persist
     * over a window's static content.
     */
    if (g_full_repaint_pending) {
        struct wlcomp_surface *rs;
        int repainted = 0;

        wl_list_for_each(rs, &g_surfaces, link) {
            if (rs->is_cursor || !surface_tree_visible(rs) ||
                !rs->committed_buf)
                continue;
            damage_surface(rs);
            repainted = 1;
        }
        if (repainted)
            g_full_repaint_pending = 0;
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

    release_cpu_copied_buffers_after_paint();
    int display_present_ok = present_damage_rects(fb_w, now);
    if (acquire_blocked) {
        g_damage_stats.acquire_blocked_frames++;
        damage_full_reason(FULL_DAMAGE_ACQUIRE);
    }

    /* Once the frame is copied into fb0 and the present fence has signaled,
     * release committed buffers so clients can recycle their storage.  The
     * buffer_released flag keeps replacement commits from sending duplicate
     * releases for the same wl_buffer. */
    int present_ready = display_present_ok && framebuffer_present_fence_ready();
    int callbacks_due = frame_callbacks_due(now);

    if (present_ready)
        xv6_d3d12_complete_pending_presents_after_display();
    flush_buffer_releases_after_present();

    wl_list_for_each(surf, &g_surfaces, link) {
        int ready = !surf->committed_buf ||
                    (!surface_has_unpresented_d3d12_buffer(surf) &&
                     present_ready &&
                     buffer_acquire_ready(surf->committed_buf));

        if (ready)
            surface_release_committed_buffer(surf);
        if (ready && callbacks_due) {
            struct wlcomp_frame_callback *cb;
            struct wlcomp_frame_callback *tmp;

            wl_list_for_each_safe(cb, tmp, &surf->frame_callbacks, link) {
                xv6_d3d12_observe_frame_callback(surf, now);
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
