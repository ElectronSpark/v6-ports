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
         (buf->is_gpu_bo || surface_has_window_frame(surf)));

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

struct gpu_compose_pending {
    struct wlcomp_buffer *buf;
    int32_t draw_x;
    int32_t draw_y;
    int32_t row0;
    int32_t row1;
    int32_t col0;
    int32_t col1;
};

#define MAX_GPU_COMPOSE_PENDING 16

/*
 * Optional present-cadence instrumentation (Phase 3 of the Alpine virgl
 * desktop parity plan).  Gated behind cmdline/env "wlcomp_trace_present=1" so
 * it stays off by default.  Accumulates per-phase microseconds and logs an
 * average breakdown roughly once per second.  This is what lets us measure
 * where a presented frame actually spends its time instead of guessing.
 */
static int present_trace_enabled(void)
{
    static int initialized;
    static int enabled;
    const char *env;

    if (initialized)
        return enabled;
    initialized = 1;
    enabled = cmdline_int_value("wlcomp_trace_present", 0) != 0;
    env = getenv("XV6_WLCOMP_TRACE_PRESENT");
    if (env && *env)
        enabled = strcmp(env, "0") != 0 && strcmp(env, "no") != 0 &&
                  strcmp(env, "false") != 0 && strcmp(env, "off") != 0;
    return enabled;
}

/*
 * Pipelined frame callbacks: release each client to render its next frame as
 * soon as its current buffer has been latched for compositing, instead of
 * waiting for the scanout present to finish.  This overlaps the client's GPU
 * render of frame N+1 with the compositor's present of frame N, roughly
 * doubling the achievable cadence on hosts where the present round-trip and
 * the client render are each a sizeable slice of the frame budget (the WSL
 * D3D12 virgl path).  Default on; clear wlcomp_pipeline_callbacks=0 to fall
 * back to the strictly-ordered "present then notify" behaviour.
 */
static int pipeline_frame_callbacks_enabled(void)
{
    static int initialized;
    static int enabled;
    const char *env;

    if (initialized)
        return enabled;
    initialized = 1;
    enabled = cmdline_int_value("wlcomp_pipeline_callbacks", 1) != 0;
    env = getenv("XV6_WLCOMP_PIPELINE_CALLBACKS");
    if (env && *env)
        enabled = strcmp(env, "0") != 0 && strcmp(env, "no") != 0 &&
                  strcmp(env, "false") != 0 && strcmp(env, "off") != 0;
    return enabled;
}

static int pipeline_gpu_release_enabled(void)
{
    static int initialized;
    static int enabled;
    const char *env;

    if (initialized)
        return enabled;
    initialized = 1;
    enabled = cmdline_int_value("wlcomp_pipeline_gpu_release", 0) != 0;
    env = getenv("XV6_WLCOMP_PIPELINE_GPU_RELEASE");
    if (env && *env)
        enabled = strcmp(env, "0") != 0 && strcmp(env, "no") != 0 &&
                  strcmp(env, "false") != 0 && strcmp(env, "off") != 0;
    return enabled;
}

/*
 * Software-cursor damage coalescing.  The compositor draws its arrow cursor
 * into the CPU framebuffer at the end of every paint_scene().  Re-damaging the
 * cursor region on *every* composite (even when the pointer is stationary)
 * leaves one CPU damage rect that the GPU-compose filter cannot cut, forcing a
 * TRANSFER_TO_HOST upload of the scanout resource every frame -- the single
 * largest per-frame serialisation on the WSL D3D12 virgl path.  A stationary
 * cursor never needs a fresh upload: its pixels are already correct on the host
 * from the last time it moved, and any CPU damage rect that overlaps it carries
 * the freshly redrawn cursor anyway (draw_default_cursor runs after every
 * paint_scene).  Pointer motion is already damaged in the input path.  So only
 * assert cursor damage from the composite path when the pointer actually moved
 * since the previous composite.  Matches the Alpine/Weston "no per-frame
 * full-screen transfer" behaviour.  Default on; clear
 * wlcomp_cursor_damage_on_move=0 to restore the unconditional re-damage.
 */
static int cursor_damage_on_move_enabled(void)
{
    static int initialized;
    static int enabled;
    const char *env;

    if (initialized)
        return enabled;
    initialized = 1;
    enabled = cmdline_int_value("wlcomp_cursor_damage_on_move", 1) != 0;
    env = getenv("XV6_WLCOMP_CURSOR_DAMAGE_ON_MOVE");
    if (env && *env)
        enabled = strcmp(env, "0") != 0 && strcmp(env, "no") != 0 &&
                  strcmp(env, "false") != 0 && strcmp(env, "off") != 0;
    return enabled;
}

static int prefilter_gpu_damage_enabled(void)
{
    static int initialized;
    static int enabled;
    const char *env;

    if (initialized)
        return enabled;
    initialized = 1;
    enabled = cmdline_int_value("wlcomp_prefilter_gpu_damage", 1) != 0;
    env = getenv("XV6_WLCOMP_PREFILTER_GPU_DAMAGE");
    if (env && *env)
        enabled = strcmp(env, "0") != 0 && strcmp(env, "no") != 0 &&
                  strcmp(env, "false") != 0 && strcmp(env, "off") != 0;
    return enabled;
}

static int startup_full_damage_ms(void)
{
    static int initialized;
    static int interval_ms;
    const char *env;

    if (initialized)
        return interval_ms;
    initialized = 1;
    /*
     * Hyper-V/synthvid can need a longer startup repaint window to clear a
     * late firmware logo.  The normal virgl desktop target is available when
     * wlcomp starts, so keep its startup full-screen damage shorter to avoid
     * paying avoidable full-target uploads during the first demo seconds.
     */
    interval_ms = g_fb_virgl_backed ? 1000 : 3000;
    env = getenv("XV6_WLCOMP_STARTUP_FULL_MS");
    if (env && *env)
        interval_ms = atoi(env);
    interval_ms = cmdline_int_value("wlcomp_startup_full_ms", interval_ms);
    if (interval_ms < 0)
        interval_ms = 0;
    return interval_ms;
}

struct present_trace_accum {
    uint64_t frames;
    uint64_t cpu_upload_us;
    uint64_t cpu_upload_rects;
    uint64_t gl_compose_us;
    uint64_t gl_compose_bufs;
    uint64_t scanout_us;
    uint64_t scanout_rects;
    uint64_t frame_us;
    uint64_t gl_preflush_frames;
    uint64_t cpu_only_frames;
};

static struct present_trace_accum g_present_trace;

static void present_trace_maybe_log(uint32_t now)
{
    static uint32_t next_log_ms;
    static uint64_t prev_release_total;
    static uint64_t prev_release_gpu_immediate;
    static uint64_t prev_release_queued;
    static uint64_t prev_release_flushed;
    static uint64_t prev_scanout_submits;
    static uint64_t prev_scanout_rebinds;
    struct present_trace_accum *a = &g_present_trace;
    uint64_t frames;
    uint64_t release_total;
    uint64_t release_gpu_immediate;
    uint64_t release_queued;
    uint64_t release_flushed;
    uint64_t scanout_submits;
    uint64_t scanout_rebinds;

    if (!present_trace_enabled())
        return;
    if (next_log_ms == 0) {
        next_log_ms = now + 1000;
        return;
    }
    if (now < next_log_ms)
        return;
    frames = a->frames ? a->frames : 1;
    release_total = g_buffer_release_trace_total - prev_release_total;
    release_gpu_immediate =
        g_buffer_release_trace_gpu_immediate - prev_release_gpu_immediate;
    release_queued = g_buffer_release_trace_queued - prev_release_queued;
    release_flushed = g_buffer_release_trace_flushed - prev_release_flushed;
    scanout_submits = g_scanout_trace_submits - prev_scanout_submits;
    scanout_rebinds = g_scanout_trace_rebinds - prev_scanout_rebinds;
    fprintf(stderr,
            "wlcomp: present-trace frames=%lu frame_avg_us=%lu "
            "cpu_upload_avg_us=%lu cpu_rects/frame=%lu cpu_rects_total=%lu "
            "gl_compose_avg_us=%lu gl_bufs/frame=%lu gl_bufs_total=%lu "
            "scanout_avg_us=%lu scanout_rects/frame=%lu "
            "scanout_rects_x100/frame=%lu scanout_rects_total=%lu "
            "path_gl_preflush=%lu path_cpu_only=%lu "
            "scanout_submits=%lu scanout_rebinds=%lu "
            "release_total=%lu release_gpu_immediate=%lu "
            "release_queued=%lu release_flushed=%lu release_pending=%d\n",
            (unsigned long)a->frames,
            (unsigned long)(a->frame_us / frames),
            (unsigned long)(a->cpu_upload_us / frames),
            (unsigned long)(a->cpu_upload_rects / frames),
            (unsigned long)a->cpu_upload_rects,
            (unsigned long)(a->gl_compose_us / frames),
            (unsigned long)(a->gl_compose_bufs / frames),
            (unsigned long)a->gl_compose_bufs,
            (unsigned long)(a->scanout_us / frames),
            (unsigned long)(a->scanout_rects / frames),
            (unsigned long)((a->scanout_rects * 100) / frames),
            (unsigned long)a->scanout_rects,
            (unsigned long)a->gl_preflush_frames,
            (unsigned long)a->cpu_only_frames,
            (unsigned long)scanout_submits,
            (unsigned long)scanout_rebinds,
            (unsigned long)release_total,
            (unsigned long)release_gpu_immediate,
            (unsigned long)release_queued,
            (unsigned long)release_flushed,
            g_release_queue_count);
    prev_release_total = g_buffer_release_trace_total;
    prev_release_gpu_immediate = g_buffer_release_trace_gpu_immediate;
    prev_release_queued = g_buffer_release_trace_queued;
    prev_release_flushed = g_buffer_release_trace_flushed;
    prev_scanout_submits = g_scanout_trace_submits;
    prev_scanout_rebinds = g_scanout_trace_rebinds;
    memset(a, 0, sizeof(*a));
    next_log_ms = now + 1000;
}
static struct gpu_compose_pending g_gpu_compose_pending[MAX_GPU_COMPOSE_PENDING];
static int g_gpu_compose_pending_count;
static struct damage_rect g_virgl_flip_prev_copy_rects[MAX_DAMAGE_RECTS];
static int g_virgl_flip_prev_copy_rect_count;
static int g_virgl_flip_gpu_warm_remaining;

static void gpu_compose_rect_append(struct damage_rect *rects, int *count,
                                    const struct damage_rect *r);

static void gpu_compose_filter_present_damage(void);
static int gpu_compose_damage_prefilter(void);

static int wlcomp_virgl_fb_damage_flip_enabled(void)
{
    static int initialized;
    static int enabled;
    const char *env;

    if (initialized)
        return enabled;
    initialized = 1;
    enabled = cmdline_int_value("wlcomp_virgl_fb_damage_flip", 0) != 0;
    env = getenv("XV6_WLCOMP_VIRGL_FB_DAMAGE_FLIP");
    if (env && *env)
        enabled = strcmp(env, "0") != 0 && strcmp(env, "no") != 0 &&
                  strcmp(env, "false") != 0 && strcmp(env, "off") != 0;
    return enabled;
}

static int virgl_damage_flip_history_ready(void)
{
    static uint32_t warmed_res_a;
    static uint32_t warmed_res_b;
    static int warmed;
    static int logs;
    uint32_t a = g_fb_virgl_resource_id;
    uint32_t b = g_fb_virgl_back_target.resource_id;

    if (a == 0 || b == 0)
        return 0;
    if (g_fb_virgl_extra_target_ready) {
        if (g_fb_virgl_extra_target.resource_id == 0 ||
            g_fb_virgl_flip_count < 3) {
            if (logs < 16) {
                fprintf(stderr,
                        "wlcomp: virgl framebuffer damage-flip warmup res=%u/%u/%u\n",
                        g_fb_virgl_resource_id,
                        g_fb_virgl_back_target.resource_id,
                        g_fb_virgl_extra_target.resource_id);
                logs++;
            }
            return 0;
        }
        if (logs < 16) {
            fprintf(stderr,
                    "wlcomp: virgl framebuffer damage-flip preserving damage res=%u/%u/%u\n",
                    g_fb_virgl_resource_id,
                    g_fb_virgl_back_target.resource_id,
                    g_fb_virgl_extra_target.resource_id);
            logs++;
        }
        return 1;
    }
    if (a > b) {
        uint32_t tmp = a;
        a = b;
        b = tmp;
    }
    if (!warmed || warmed_res_a != a || warmed_res_b != b) {
        warmed_res_a = a;
        warmed_res_b = b;
        warmed = 1;
        if (logs < 16) {
            fprintf(stderr,
                    "wlcomp: virgl framebuffer damage-flip warmup res=%u/%u\n",
                    warmed_res_a, warmed_res_b);
            logs++;
        }
        return 0;
    }
    if (logs < 16) {
        fprintf(stderr,
                "wlcomp: virgl framebuffer damage-flip preserving damage res=%u/%u\n",
                warmed_res_a, warmed_res_b);
        logs++;
    }
    return 1;
}

static void virgl_flip_copy_rects_remember(const struct damage_rect *rects,
                                           int count)
{
    int out_count = 0;

    if (!rects || count <= 0) {
        g_virgl_flip_prev_copy_rect_count = 0;
        return;
    }
    for (int i = 0; i < count && out_count < MAX_DAMAGE_RECTS; i++) {
        struct damage_rect r = rects[i];

        if (r.x1 < 0) r.x1 = 0;
        if (r.y1 < 0) r.y1 = 0;
        if (r.x2 > (int32_t)g_fb_w) r.x2 = (int32_t)g_fb_w;
        if (r.y2 > (int32_t)g_fb_h) r.y2 = (int32_t)g_fb_h;
        if (r.x1 >= r.x2 || r.y1 >= r.y2)
            continue;
        g_virgl_flip_prev_copy_rects[out_count++] = r;
    }
    g_virgl_flip_prev_copy_rect_count = out_count;
}

static int virgl_flip_target_count(void)
{
    if (!g_fb_virgl_backed || !g_fb_virgl_back_target_ready)
        return 1;
    return g_fb_virgl_extra_target_ready ? 3 : 2;
}

static void virgl_flip_schedule_gpu_warmup(void)
{
    int count = virgl_flip_target_count();

    if (count > g_virgl_flip_gpu_warm_remaining)
        g_virgl_flip_gpu_warm_remaining = count;
}

static void virgl_damage_flip_gpu_window_warmup(void)
{
    static int seen_gpu_window;
    static int warm_logs;
    struct wlcomp_surface *surf;
    int visible_gpu_window = 0;
    int damaged = 0;

    if (!wlcomp_virgl_fb_damage_flip_enabled() || !g_fb_virgl_backed ||
        !g_fb_virgl_back_target_ready) {
        g_virgl_flip_gpu_warm_remaining = 0;
        seen_gpu_window = 0;
        return;
    }

    wl_list_for_each(surf, &g_surfaces, link) {
        struct wlcomp_buffer *buf;

        if (!surface_tree_visible(surf) || !surf->committed_buf ||
            surf->is_cursor)
            continue;
        buf = surf->committed_buf;
        if (!buf->is_gpu_bo || !gpu_compose_can_present_surface(surf, buf))
            continue;
        visible_gpu_window = 1;
        if (!buffer_acquire_ready(buf))
            continue;
        if (!seen_gpu_window) {
            seen_gpu_window = 1;
            virgl_flip_schedule_gpu_warmup();
        }
        if (g_virgl_flip_gpu_warm_remaining > 0) {
            damage_surface(surf);
            damaged = 1;
        }
    }

    if (!visible_gpu_window) {
        seen_gpu_window = 0;
        g_virgl_flip_gpu_warm_remaining = 0;
        return;
    }
    if (damaged) {
        if (warm_logs < 12) {
            fprintf(stderr,
                    "wlcomp: virgl framebuffer gpu-window damage-flip warmup remaining=%d targets=%d\n",
                    g_virgl_flip_gpu_warm_remaining,
                    virgl_flip_target_count());
            warm_logs++;
        }
        g_virgl_flip_gpu_warm_remaining--;
    }
}

static void gpu_compose_pending_reset(void)
{
    g_gpu_compose_pending_count = 0;
}

static int gpu_compose_pending_add(struct wlcomp_buffer *buf,
                                   int32_t draw_x, int32_t draw_y,
                                   int32_t row0, int32_t row1,
                                   int32_t col0, int32_t col1)
{
    struct gpu_compose_pending *item;

    if (!gpu_compose_enabled() ||
        g_gpu_compose_pending_count >= MAX_GPU_COMPOSE_PENDING)
        return 0;
    item = &g_gpu_compose_pending[g_gpu_compose_pending_count++];
    item->buf = buf;
    item->draw_x = draw_x;
    item->draw_y = draw_y;
    item->row0 = row0;
    item->row1 = row1;
    item->col0 = col0;
    item->col1 = col1;
    return 1;
}

static int damage_rect_intersect(const struct damage_rect *a,
                                 const struct damage_rect *b,
                                 struct damage_rect *out)
{
    struct damage_rect r;

    if (!a || !b || !out)
        return 0;
    r.x1 = a->x1 > b->x1 ? a->x1 : b->x1;
    r.y1 = a->y1 > b->y1 ? a->y1 : b->y1;
    r.x2 = a->x2 < b->x2 ? a->x2 : b->x2;
    r.y2 = a->y2 < b->y2 ? a->y2 : b->y2;
    if (r.x1 >= r.x2 || r.y1 >= r.y2)
        return 0;
    *out = r;
    return 1;
}

static int gpu_compose_pending_add_damage(struct wlcomp_surface *surf,
                                          struct wlcomp_buffer *buf)
{
    int32_t gx, gy, gw, gh;
    int32_t ax, ay;
    int32_t draw_x, draw_y;
    int32_t row0 = 0, row1;
    int32_t col0 = 0, col1;
    struct damage_rect dst;
    int added = 0;

    if (!gpu_compose_can_present_surface(surf, buf))
        return 0;

    surface_window_geometry(surf, &gx, &gy, &gw, &gh);
    surface_absolute_position(surf, &ax, &ay);
    draw_x = ax - gx;
    draw_y = ay - gy;
    row1 = buf->height;
    col1 = buf->width;
    if (surf->has_window_geometry) {
        row0 = gy < 0 ? 0 : gy;
        col0 = gx < 0 ? 0 : gx;
        row1 = gy + gh;
        col1 = gx + gw;
        if (row1 > buf->height) row1 = buf->height;
        if (col1 > buf->width) col1 = buf->width;
        if (row0 > row1) row0 = row1;
        if (col0 > col1) col0 = col1;
    }
    if (row0 < 0) row0 = 0;
    if (col0 < 0) col0 = 0;
    if (row1 > buf->height) row1 = buf->height;
    if (col1 > buf->width) col1 = buf->width;
    if (row0 < -draw_y) row0 = -draw_y;
    if (col0 < -draw_x) col0 = -draw_x;
    if (row1 > (int32_t)g_fb_h - draw_y)
        row1 = (int32_t)g_fb_h - draw_y;
    if (col1 > (int32_t)g_fb_w - draw_x)
        col1 = (int32_t)g_fb_w - draw_x;
    if (row0 >= row1 || col0 >= col1)
        return 0;

    dst = (struct damage_rect){
        draw_x + col0, draw_y + row0, draw_x + col1, draw_y + row1
    };
    for (int i = 0; i < g_damage_count; i++) {
        struct damage_rect hit;

        if (!damage_rect_intersect(&g_damage[i], &dst, &hit))
            continue;
        if (!gpu_compose_pending_add(buf, draw_x, draw_y,
                                     hit.y1 - draw_y,
                                     hit.y2 - draw_y,
                                     hit.x1 - draw_x,
                                     hit.x2 - draw_x))
            break;
        added = 1;
    }
    return added;
}

static int gpu_compose_damage_prefilter(void)
{
    struct wlcomp_surface *surf;
    int added = 0;

    if (!prefilter_gpu_damage_enabled() || !gpu_compose_enabled() ||
        g_damage_count <= 0)
        return 0;

    wl_list_for_each(surf, &g_surfaces, link) {
        struct wlcomp_buffer *buf;

        if (!surface_tree_visible(surf) || !surf->committed_buf ||
            surf->is_cursor)
            continue;
        buf = surf->committed_buf;
        if (buf->is_d3d12_shared)
            continue;
        if (!gpu_compose_can_present_surface(surf, buf))
            continue;
        if (!buffer_acquire_ready(buf))
            continue;
        if (gpu_compose_pending_add_damage(surf, buf))
            added = 1;
    }
    if (added)
        gpu_compose_filter_present_damage();
    return added;
}

static int gpu_compose_pending_flush(void)
{
    int presented = 0;

    for (int i = 0; i < g_gpu_compose_pending_count; i++) {
        struct gpu_compose_pending *item = &g_gpu_compose_pending[i];

        if (gpu_compose_present_buffer(item->buf, item->draw_x,
                                       item->draw_y, item->row0,
                                       item->row1, item->col0,
                                       item->col1))
            presented++;
    }
    gpu_compose_pending_reset();
    return presented;
}

static int gpu_compose_pending_append_rects(struct damage_rect *rects,
                                            int count)
{
    for (int i = 0; i < g_gpu_compose_pending_count; i++) {
        struct gpu_compose_pending *item = &g_gpu_compose_pending[i];
        struct damage_rect r = {
            item->draw_x + item->col0,
            item->draw_y + item->row0,
            item->draw_x + item->col1,
            item->draw_y + item->row1,
        };

        if (r.x1 < 0) r.x1 = 0;
        if (r.y1 < 0) r.y1 = 0;
        if (r.x2 > (int32_t)g_fb_w) r.x2 = (int32_t)g_fb_w;
        if (r.y2 > (int32_t)g_fb_h) r.y2 = (int32_t)g_fb_h;
        gpu_compose_rect_append(rects, &count, &r);
    }
    return count;
}

static void gpu_compose_rect_append(struct damage_rect *rects, int *count,
                                    const struct damage_rect *r)
{
    if (!r || r->x1 >= r->x2 || r->y1 >= r->y2 ||
        *count >= MAX_DAMAGE_RECTS)
        return;
    rects[(*count)++] = *r;
}

static void gpu_compose_subtract_one(const struct damage_rect *src,
                                     const struct damage_rect *cut,
                                     struct damage_rect *out,
                                     int *out_count)
{
    int32_t ix1;
    int32_t iy1;
    int32_t ix2;
    int32_t iy2;
    struct damage_rect r;

    if (!src || !cut)
        return;
    ix1 = src->x1 > cut->x1 ? src->x1 : cut->x1;
    iy1 = src->y1 > cut->y1 ? src->y1 : cut->y1;
    ix2 = src->x2 < cut->x2 ? src->x2 : cut->x2;
    iy2 = src->y2 < cut->y2 ? src->y2 : cut->y2;
    if (ix1 >= ix2 || iy1 >= iy2) {
        gpu_compose_rect_append(out, out_count, src);
        return;
    }

    r = (struct damage_rect){ src->x1, src->y1, src->x2, iy1 };
    gpu_compose_rect_append(out, out_count, &r);
    r = (struct damage_rect){ src->x1, iy2, src->x2, src->y2 };
    gpu_compose_rect_append(out, out_count, &r);
    r = (struct damage_rect){ src->x1, iy1, ix1, iy2 };
    gpu_compose_rect_append(out, out_count, &r);
    r = (struct damage_rect){ ix2, iy1, src->x2, iy2 };
    gpu_compose_rect_append(out, out_count, &r);
}

static void gpu_compose_filter_present_damage(void)
{
    struct damage_rect filtered[MAX_DAMAGE_RECTS];
    int filtered_count = 0;

    if (g_gpu_compose_pending_count == 0)
        return;
    for (int i = 0; i < g_damage_count; i++) {
        struct damage_rect work[MAX_DAMAGE_RECTS];
        int work_count = 1;

        work[0] = g_damage[i];
        for (int j = 0; j < g_gpu_compose_pending_count && work_count > 0;
             j++) {
            struct gpu_compose_pending *item = &g_gpu_compose_pending[j];
            struct damage_rect cut = {
                item->draw_x + item->col0,
                item->draw_y + item->row0,
                item->draw_x + item->col1,
                item->draw_y + item->row1,
            };
            struct damage_rect next[MAX_DAMAGE_RECTS];
            int next_count = 0;

            for (int k = 0; k < work_count; k++)
                gpu_compose_subtract_one(&work[k], &cut, next,
                                         &next_count);
            memcpy(work, next, sizeof(work));
            work_count = next_count;
        }
        for (int j = 0; j < work_count; j++)
            gpu_compose_rect_append(filtered, &filtered_count, &work[j]);
    }
    memcpy(g_damage, filtered, sizeof(filtered));
    g_damage_count = filtered_count;
}

static uint32_t paint_probe_pixel(uint32_t *fb, int fb_w, int fb_h,
                                  int x, int y)
{
    if (!fb || x < 0 || y < 0 || x >= fb_w || y >= fb_h)
        return 0;
    return fb[y * fb_w + x];
}

static int paint_probe_clip_contains(int x, int y)
{
    if (!g_paint_clip_enabled)
        return 1;
    return x >= g_paint_clip.x1 && x < g_paint_clip.x2 &&
           y >= g_paint_clip.y1 && y < g_paint_clip.y2;
}

static void paint_scene(int fb_w, int fb_h, int *acquire_blocked)
{
    struct wlcomp_surface *surf;
    static int paint_probe_logged;
    int paint_probe = 0;
    int paint_probe_icon_x = 40;
    int paint_probe_icon_y = 20;
    int paint_probe_task_x = 20;
    int paint_probe_task_y = fb_h > TASKBAR_H ? fb_h - TASKBAR_H / 2 : fb_h / 2;
    const desktop_icon_t *paint_probe_icon = NULL;

    if (!paint_probe_logged) {
        paint_probe_icon = wlcomp_desktop_icon_at(0);
        if (paint_probe_icon) {
            paint_probe_icon_x = paint_probe_icon->x + paint_probe_icon->w / 2;
            paint_probe_icon_y = paint_probe_icon->y + 20;
        }
        paint_probe =
            paint_probe_clip_contains(paint_probe_icon_x, paint_probe_icon_y) &&
            paint_probe_clip_contains(paint_probe_task_x, paint_probe_task_y);
        if (paint_probe) {
            fprintf(stderr,
                    "wlcomp: paint probe begin clip=%d,%d-%d,%d icon='%s' icon_xy=%d,%d task_xy=%d,%d before=%08x/%08x\n",
                    g_paint_clip.x1, g_paint_clip.y1, g_paint_clip.x2,
                    g_paint_clip.y2,
                    paint_probe_icon ? paint_probe_icon->label : "(none)",
                    paint_probe_icon_x, paint_probe_icon_y,
                    paint_probe_task_x, paint_probe_task_y,
                    paint_probe_pixel(g_fb_buf, fb_w, fb_h,
                                      paint_probe_icon_x, paint_probe_icon_y),
                    paint_probe_pixel(g_fb_buf, fb_w, fb_h,
                                      paint_probe_task_x, paint_probe_task_y));
        }
    }

    /* Draw desktop wallpaper */
    wlcomp_desktop_draw_wallpaper(g_fb_buf, fb_w, fb_h);
    if (paint_probe) {
        fprintf(stderr,
                "wlcomp: paint probe after_wallpaper icon=%08x task=%08x\n",
                paint_probe_pixel(g_fb_buf, fb_w, fb_h,
                                  paint_probe_icon_x, paint_probe_icon_y),
                paint_probe_pixel(g_fb_buf, fb_w, fb_h,
                                  paint_probe_task_x, paint_probe_task_y));
    }

    /* Draw desktop icons */
    wlcomp_desktop_draw_icons(g_fb_buf, fb_w, fb_h, g_selected_icon);
    if (paint_probe) {
        fprintf(stderr,
                "wlcomp: paint probe after_icons icon=%08x task=%08x\n",
                paint_probe_pixel(g_fb_buf, fb_w, fb_h,
                                  paint_probe_icon_x, paint_probe_icon_y),
                paint_probe_pixel(g_fb_buf, fb_w, fb_h,
                                  paint_probe_task_x, paint_probe_task_y));
    }

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

        if (surface_has_window_frame(surf)) {
            int title_h = WAYLAND_DEMO_TITLE_H;
            int border = WAYLAND_DEMO_BORDER;
            int frame_x = ax - border;
            int frame_y = ay - title_h - border;
            int frame_w = gw + border * 2;
            int frame_h = gh + title_h + border * 2;
            int focused = surface_is_foreground(surf);
            int min_x, max_x, close_x, btn_y;
            uint32_t title_color = focused ? 0xFF2C4F7C : 0xFF2A2E35;
            uint32_t border_color = focused ? 0xFF5F7EA8 : 0xFF3C5078;
            const char *title = surface_taskbar_label(surf);

            surface_window_button_rects(surf, &min_x, &max_x, &close_x,
                                        &btn_y);
            draw_rect(g_fb_buf, fb_w, fb_h, frame_x + WAYLAND_DEMO_SHADOW,
                      frame_y + WAYLAND_DEMO_SHADOW,
                      frame_w, frame_h, 0xFF101418);
            draw_rect(g_fb_buf, fb_w, fb_h, frame_x, frame_y,
                      frame_w, frame_h, 0xFF15191F);
            draw_rect(g_fb_buf, fb_w, fb_h, frame_x + border,
                      frame_y + border, gw, title_h, title_color);
            draw_string(g_fb_buf, fb_w, fb_h, frame_x + 10,
                        frame_y + border + 4, title, 0xFFD2DAE2, 1);
            draw_rect(g_fb_buf, fb_w, fb_h, min_x, btn_y,
                      WAYLAND_MINIMIZE_SZ, WAYLAND_MINIMIZE_SZ, 0xFF607080);
            draw_char(g_fb_buf, fb_w, fb_h, min_x + 5, btn_y + 1,
                      '_', 0xFFFFFFFF, 1);
            draw_rect(g_fb_buf, fb_w, fb_h, max_x, btn_y,
                      WAYLAND_MAXIMIZE_SZ, WAYLAND_MAXIMIZE_SZ,
                      surf->maximized ? 0xFF6E8A3D : 0xFF3D6EA6);
            draw_char(g_fb_buf, fb_w, fb_h, max_x + 5, btn_y + 1,
                      surf->maximized ? 'r' : 'M', 0xFFFFFFFF, 1);
            draw_rect(g_fb_buf, fb_w, fb_h, close_x, btn_y,
                      WAYLAND_CLOSE_SZ, WAYLAND_CLOSE_SZ, 0xFFA63D3D);
            draw_char(g_fb_buf, fb_w, fb_h, close_x + 5, btn_y + 1,
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

        if (gpu_compose_can_present_surface(surf, buf) &&
            gpu_compose_pending_add(buf, draw_x, draw_y,
                                    row0, row1, col0, col1))
            continue;

        src_bytes = buffer_data(buf);
        if (!src_bytes) continue;
        src = (uint32_t *)src_bytes;

        if (paint_scaled_window_geometry_buffer(surf, buf, draw_x, draw_y,
                                                gw, gh))
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
             (buf->is_gpu_bo || surface_has_window_frame(surf)))) {
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
    if (paint_probe) {
        fprintf(stderr,
                "wlcomp: paint probe after_taskbar icon=%08x task=%08x\n",
                paint_probe_pixel(g_fb_buf, fb_w, fb_h,
                                  paint_probe_icon_x, paint_probe_icon_y),
                paint_probe_pixel(g_fb_buf, fb_w, fb_h,
                                  paint_probe_task_x, paint_probe_task_y));
        paint_probe_logged = 1;
    }

    /* Draw popup menu if open */
    draw_menu(g_fb_buf, fb_w, fb_h);

    /* Draw one compositor-owned cursor. */
    draw_default_cursor();
}

static void release_cpu_copied_buffers_after_paint(void)
{
    struct wlcomp_surface *surf;
    static int gpu_compose_release_logs;
    int gl_release_sync = wlcomp_gl_compose_submit_fence_enabled();

    wl_list_for_each(surf, &g_surfaces, link) {
        struct wlcomp_buffer *buf = surf->committed_buf;
        int32_t gx, gy, gw, gh;
        int exact_gpu_window = 0;

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
            exact_gpu_window =
                surf->has_window_geometry &&
                buf->width == gw && buf->height == gh;
            (void)gx;
            (void)gy;
            /*
             * Match the Wayland compositor contract for GPU buffers: after a
             * frame has consumed the buffer through GPU composition, release it
             * so Mesa can recycle the swapchain.  Requiring exact window
             * geometry starves clients whose xdg geometry excludes decoration
             * or whose buffer is clipped by damage.
             */
            if (!buf->gpu_compose_presented)
                continue;
        }
        if (!buffer_acquire_ready(buf))
            continue;

        if (gpu_compose_release_logs < 12) {
            fprintf(stderr,
                    "wlcomp: releasing gpu-composed dmabuf pid=%d size=%dx%d geom=%d,%d exact=%d queued=%d\n",
                    surf->client_pid, buf->width, buf->height, gw, gh,
                    exact_gpu_window, gl_release_sync);
            gpu_compose_release_logs++;
        }
        if (gl_release_sync) {
            queue_buffer_release(buf);
        } else {
            buffer_send_release_events(buf);
            buf->release_pending = 0;
            g_buffer_release_trace_gpu_immediate++;
        }
        surf->buffer_released = 1;
        buf->gpu_compose_presented = 0;
        if (!buf->release_pending)
            buffer_maybe_free(buf);
    }
}

static void release_gpu_composed_buffers_before_scanout(void)
{
    static int early_release_logs;
    uint64_t before;

    if (!pipeline_gpu_release_enabled())
        return;
    before = g_buffer_release_trace_gpu_immediate;
    release_cpu_copied_buffers_after_paint();
    if (g_buffer_release_trace_gpu_immediate == before)
        return;
    if (early_release_logs < 8) {
        fprintf(stderr,
                "wlcomp: pipelined gpu buffer release before scanout released=%lu\n",
                (unsigned long)(g_buffer_release_trace_gpu_immediate -
                                before));
        early_release_logs++;
    }
    wl_display_flush_clients(g_display);
}

static void damage_fast_gpu_windows(uint32_t now)
{
    static uint32_t next_gpu_damage_ms;
    struct wlcomp_surface *surf;
    int damaged = 0;
    int interval_ms = cmdline_int_value("wlcomp_gpu_damage_ms", 0);
    const char *env = getenv("XV6_WLCOMP_GPU_DAMAGE_MS");

    if (env && *env)
        interval_ms = atoi(env);
    if (interval_ms <= 0)
        return;
    if (interval_ms > 1000)
        interval_ms = 1000;

    if (now < next_gpu_damage_ms)
        return;

    wl_list_for_each(surf, &g_surfaces, link) {
        int32_t gx, gy, gw, gh;
        int32_t ax, ay;
        int32_t draw_x, draw_y;

        if (!surface_tree_visible(surf) || !surf->committed_buf ||
            !surf->committed_buf->is_gpu_bo || !surface_is_demo_window(surf))
            continue;
        surface_window_geometry(surf, &gx, &gy, &gw, &gh);
        if (gw <= 0 || gh <= 0)
            continue;
        surface_absolute_position(surf, &ax, &ay);
        draw_x = ax - gx;
        draw_y = ay - gy;
        damage_rect(draw_x + gx, draw_y + gy, gw, gh);
        damaged = 1;
    }

    if (damaged)
        next_gpu_damage_ms = now + (uint32_t)interval_ms;
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
    {
        static int logged_desktop_frame;

        if (!logged_desktop_frame) {
            int gpu_enabled = gpu_compose_enabled();

            fprintf(stderr,
                    "wlcomp: desktop frame fb=%ux%u pitch=%u mode=%s icons=%d taskbar_h=%d gpu_compose=%d direct_scanout=%d bo_backed=%d\n",
                    g_fb_w, g_fb_h, g_fb_pitch,
                    g_fb_direct_scanout ? "direct-scanout" :
                        (g_fb_bo_backed ? "bo-present" : "user-blit"),
                    wlcomp_desktop_icon_count(), TASKBAR_H,
                    gpu_enabled, g_fb_direct_scanout, g_fb_bo_backed);
            logged_desktop_frame = 1;
        }
    }

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
        startup_full_until_ms = now + (uint32_t)startup_full_damage_ms();
        next_startup_full_ms = now;
    }
    if (now <= startup_full_until_ms && now >= next_startup_full_ms) {
        damage_full_reason(FULL_DAMAGE_INIT);
        next_startup_full_ms = now + 250;
    }
    virgl_damage_flip_gpu_window_warmup();

    if (!damage_has_any()) {
        damage_all_frame_callbacks(now);
        return;
    }

    /*
     * Experimental Tier-1 path from the Alpine parity plan.  When explicitly
     * enabled, render this damaged frame into the inactive full-screen virgl
     * target, then present that target.  The conservative path forces a full
     * repaint because the inactive target may be one frame older than the
     * currently displayed target.  The damage-flip path warms both targets with
     * one full repaint, then relies on mirror_cpu_damage_to_virgl_back_target()
     * for CPU/chrome history and on GPU composition to redraw damaged client
     * regions.  The older copy-before-flip diagnostic remains available but is
     * unsafe on this host when FB_GPU_BO_COPY reports virgl context errors.
     */
    if (wlcomp_virgl_swap_render_target()) {
        int copied_for_flip = 0;

        if (wlcomp_virgl_fb_damage_flip_enabled()) {
            if (!virgl_damage_flip_history_ready())
                damage_full_for_flip();
            copied_for_flip = 1;
        } else {
            if (wlcomp_virgl_fb_copy_damage_before_flip_enabled() &&
                g_virgl_flip_prev_copy_rect_count > 0) {
                copied_for_flip =
                    wlcomp_virgl_copy_displayed_to_render_target_rects(
                        g_virgl_flip_prev_copy_rects,
                        g_virgl_flip_prev_copy_rect_count);
            }
            if (!copied_for_flip &&
                !wlcomp_virgl_copy_displayed_to_render_target())
                damage_full_for_flip();
        }
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

    if (cursor_damage_on_move_enabled()) {
        static int16_t last_cursor_x;
        static int16_t last_cursor_y;
        static int last_cursor_valid;

        if (!last_cursor_valid ||
            g_cursor_x != last_cursor_x || g_cursor_y != last_cursor_y) {
            damage_cursor_at(g_cursor_x, g_cursor_y);
            last_cursor_x = g_cursor_x;
            last_cursor_y = g_cursor_y;
            last_cursor_valid = 1;
        }
    } else {
        damage_cursor_at(g_cursor_x, g_cursor_y);
    }
    surface_promote_deferred_buffers();
    tune_damage_for_present();
    int acquire_blocked = 0;
    int paint_count;
    struct damage_rect paint_rects[MAX_DAMAGE_RECTS];
    struct wlcomp_surface *surf;

    gpu_compose_pending_reset();
    gpu_compose_damage_prefilter();
    paint_count = g_damage_count;
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

    gpu_compose_filter_present_damage();
    /*
     * The client buffers for this frame are now latched into the GL compose
     * pending list and the CPU damage has been filtered.  Release the clients
     * to render their next frame *before* the (potentially multi-millisecond)
     * scanout present, then flush so the wakeup reaches them immediately.
     * This overlaps the next client render with our present.  Buffer releases
     * stay gated on present completion below, so a client only recycles its
     * storage once it is safe.  d3d12-shared single-buffer clients are skipped
     * by damage_all_frame_callbacks() until their present completes.
     */
    if (pipeline_frame_callbacks_enabled()) {
        damage_all_frame_callbacks(now);
        wl_display_flush_clients(g_display);
    }
    /*
     * The virgl framebuffer path presents the whole render target after
     * uploading CPU-damaged regions.  Draw imported GPU surfaces into that
     * same target before the final scanout present so the host never sees the
     * intermediate wallpaper/CPU-only frame.
     */
    int gpu_compose_preflushed = g_fb_virgl_backed &&
        g_gpu_compose_pending_count > 0;

    int display_present_ok;
    int trace_present = present_trace_enabled();
    uint64_t frame_t0 = trace_present ? get_time_us() : 0;
    int traced_cpu_rects = 0;
    int traced_gl_bufs = 0;
    int traced_scanout_rects = 0;

    if (gpu_compose_preflushed) {
        int gpu_presented;
        struct damage_rect scanout_rects[MAX_DAMAGE_RECTS];
        int scanout_rect_count = paint_count;
        struct wlcomp_surface *fsdirect =
            compositor_fullscreen_direct_surface();

        if (fsdirect) {
            /*
             * Unredirection: a single client owns the whole scanout this
             * frame.  Skip the desktop chrome upload and the compositor's
             * own scanout present entirely -- issue only the client's
             * resource-scanout bind.  This removes the compositor's
             * compose + present work from the single virtio-gpu control
             * queue so the client can render at its native cadence.  The
             * composited chrome is intentionally not shown while the client
             * is unredirected.
             */
            traced_gl_bufs = g_gpu_compose_pending_count;
            {
                uint64_t t0 = trace_present ? get_time_us() : 0;
                gpu_presented = gpu_compose_pending_flush();
                if (trace_present)
                    g_present_trace.gl_compose_us += get_time_us() - t0;
            }
            display_present_ok = gpu_presented;
            /*
             * The chrome present path normally accounts the frame and clears
             * the CPU damage list.  In the bypass we present only the client
             * resource, so do the same bookkeeping here -- otherwise the
             * visible-fps stats (displayed_fps) never advance and stale CPU
             * damage accumulates.
             */
            if (gpu_presented) {
                framebuffer_account_present_frame(
                    now, (uint64_t)g_fb_w * g_fb_h, 1, 1);
                {
                    struct damage_rect full = {
                        0, 0, (int32_t)g_fb_w, (int32_t)g_fb_h
                    };

                    virgl_flip_copy_rects_remember(&full, 1);
                }
            }
            g_damage_count = 0;
        } else {
        /*
         * Match the Linux/Weston/Alpine order: first upload CPU-rendered
         * desktop chrome into the full-screen virgl target, then draw GPU
         * client buffers into that same target, and only then flush scanout.
         */
        /*
         * g_damage has already been filtered to remove regions covered by
         * GPU-composed clients.  Use that filtered CPU damage for the final
         * scanout flush instead of the pre-filter paint list; otherwise an
         * animated GPU-only frame carries a stale CPU rect alongside the GPU
         * client rect and present tracing reports two scanout rects even
         * though the kernel later collapses them to one RESOURCE_FLUSH.
         */
        scanout_rect_count = g_damage_count;
        for (int i = 0; i < scanout_rect_count; i++)
            scanout_rects[i] = g_damage[i];
        traced_gl_bufs = g_gpu_compose_pending_count;
        scanout_rect_count =
            gpu_compose_pending_append_rects(scanout_rects,
                                             scanout_rect_count);
        traced_cpu_rects = g_damage_count;
        {
            uint64_t t0 = trace_present ? get_time_us() : 0;
            display_present_ok = present_damage_rects(
                fb_w, now, paint_rects, paint_count, 1);
            if (trace_present)
                g_present_trace.cpu_upload_us += get_time_us() - t0;
        }
        {
            uint64_t t0 = trace_present ? get_time_us() : 0;
            gpu_presented = gpu_compose_pending_flush();
            if (trace_present)
                g_present_trace.gl_compose_us += get_time_us() - t0;
        }
        if (gpu_presented)
            release_gpu_composed_buffers_before_scanout();
        if (gpu_presented) {
            uint64_t t0 = trace_present ? get_time_us() : 0;
            traced_scanout_rects = scanout_rect_count;
            display_present_ok =
                present_virgl_scanout_rects(scanout_rects,
                                            scanout_rect_count);
            if (display_present_ok && wlcomp_gl_compose_flush_after_scanout())
                wlcomp_gl_compose_flush_submitted();
            if (display_present_ok)
                virgl_flip_copy_rects_remember(scanout_rects,
                                               scanout_rect_count);
            if (trace_present)
                g_present_trace.scanout_us += get_time_us() - t0;
        }
        }
    } else {
        uint64_t t0 = trace_present ? get_time_us() : 0;
        traced_cpu_rects = g_damage_count;
        display_present_ok = present_damage_rects(
            fb_w, now, paint_rects, paint_count, 0);
        if (display_present_ok)
            virgl_flip_copy_rects_remember(paint_rects, paint_count);
        if (trace_present)
            g_present_trace.cpu_upload_us += get_time_us() - t0;
    }
    if (!gpu_compose_preflushed) {
        uint64_t t0 = trace_present ? get_time_us() : 0;
        traced_gl_bufs = g_gpu_compose_pending_count;
        int gpu_presented = gpu_compose_pending_flush();
        (void)gpu_presented;
        if (trace_present)
            g_present_trace.gl_compose_us += get_time_us() - t0;
    }
    if (trace_present) {
        g_present_trace.frames++;
        g_present_trace.frame_us += get_time_us() - frame_t0;
        g_present_trace.cpu_upload_rects += (uint64_t)traced_cpu_rects;
        g_present_trace.gl_compose_bufs += (uint64_t)traced_gl_bufs;
        g_present_trace.scanout_rects += (uint64_t)traced_scanout_rects;
        if (gpu_compose_preflushed)
            g_present_trace.gl_preflush_frames++;
        else
            g_present_trace.cpu_only_frames++;
        present_trace_maybe_log(now);
    }
    /* Once the frame is copied into fb0 and the present fence has signaled,
     * release committed buffers so clients can recycle their storage.  The
     * buffer_released flag keeps replacement commits from sending duplicate
     * releases for the same wl_buffer. */
    int present_ready = display_present_ok && framebuffer_present_fence_ready();
    int callbacks_pending_before_present = frame_callbacks_pending_count();
    int callbacks_due = callbacks_pending_before_present > 0 ?
        frame_callbacks_due(now) : 0;
    int callbacks_delivered_after_present = 0;

    if (present_ready)
        xv6_d3d12_complete_pending_presents_after_display();
    flush_buffer_releases_after_present();
    if (display_present_ok)
        release_cpu_copied_buffers_after_paint();
    if (acquire_blocked) {
        g_damage_stats.acquire_blocked_frames++;
        virgl_flip_schedule_gpu_warmup();
        damage_full_reason(FULL_DAMAGE_ACQUIRE);
    }

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
                callbacks_delivered_after_present++;
            }
        }
    }
    frame_perf_note_after_present(now, callbacks_pending_before_present,
                                  callbacks_delivered_after_present,
                                  callbacks_due);
    if (!present_ready)
        damage_full_reason(FULL_DAMAGE_ACQUIRE);

}
