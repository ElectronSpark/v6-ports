/* Framebuffer state */
static int      g_fb_fd   = -1;
static uint32_t g_fb_w, g_fb_h;
static uint32_t g_fb_pitch;
static uint32_t *g_fb_buf;  /* compositing buffer */
static uint64_t g_fb_bo_size;
static uint32_t g_fb_bo_handle;
static int      g_fb_bo_backed;
static int      g_fb_virgl_backed;
static uint32_t g_fb_virgl_ctx_id;
static uint32_t g_fb_virgl_resource_id;
static int      g_fb_direct_scanout;
static uint64_t g_fb_bo_last_present_fence;
static uint64_t g_fb_bo_last_signaled_fence;
static uint64_t g_fb_display_wait_sequence;
static uint64_t g_fb_display_last_complete;
static uint64_t g_fb_display_stall_start_ms;
struct damage_rect {
    int32_t x1, y1, x2, y2;
};
static struct damage_rect g_damage[MAX_DAMAGE_RECTS];
static int      g_damage_count;
static struct damage_rect g_paint_clip;
static int      g_paint_clip_enabled;

enum full_damage_reason {
    FULL_DAMAGE_OTHER,
    FULL_DAMAGE_INIT,
    FULL_DAMAGE_REPAIR,
    FULL_DAMAGE_ACQUIRE,
    FULL_DAMAGE_DESKTOP,
    FULL_DAMAGE_MENU,
    FULL_DAMAGE_RESIZE,
    FULL_DAMAGE_CLIENT,
    FULL_DAMAGE_IWIN,
    FULL_DAMAGE_COUNT,
};

struct wlcomp_damage_stats {
    uint64_t frames;
    uint64_t present_rects;
    uint64_t present_pixels;
    uint64_t present_full_frames;
    uint64_t present_union_collapses;
    uint64_t frame_interval_sum_ms;
    uint64_t frame_interval_max_ms;
    uint64_t frame_interval_over_25ms;
    uint64_t frame_interval_over_40ms;
    uint64_t acquire_blocked_frames;
    uint64_t acquire_stall_recoveries;
    uint64_t display_waits;
    uint64_t display_wait_not_ready;
    uint64_t display_stall_recoveries;
    uint64_t gpu_compose_presents;
    uint64_t gpu_compose_pixels;
    uint64_t full_damage[FULL_DAMAGE_COUNT];
};

static struct wlcomp_damage_stats g_damage_stats;
/*
 * Set whenever a full-screen wallpaper repaint is issued.  A full repaint
 * paints the desktop wallpaper everywhere first and then composites surfaces
 * on top.  If a mapped client surface is transiently absent from g_surfaces
 * (or has not yet committed its static chrome) during that frame, the
 * wallpaper is left exposed where the surface should be.  Clients only damage
 * the regions they actively redraw (e.g. animated web content), so static
 * chrome such as a GTK header/toolbar is never re-damaged and the exposed
 * wallpaper band persists forever.  This flag forces the compositor to fully
 * re-damage every mapped surface after a wallpaper repaint, until at least one
 * such surface has actually been repainted.
 */
static int g_full_repaint_pending;
static uint64_t g_damage_interval_max_ms;
static uint64_t g_fb_blit_present_attempt_count;
static uint64_t g_fb_blit_present_success_count;

struct wlcomp_virgl_fb_target {
    uint32_t *buf;
    uint32_t pitch;
    uint64_t size;
    uint32_t bo_handle;
    uint32_t resource_id;
};

static struct wlcomp_virgl_fb_target g_fb_virgl_back_target;
static int g_fb_virgl_back_target_ready;
static struct wlcomp_virgl_fb_target g_fb_virgl_extra_target;
static int g_fb_virgl_extra_target_ready;
static uint64_t g_fb_virgl_flip_count;

#define MAX_RELEASE_QUEUE 64
static struct wlcomp_buffer *g_release_queue[MAX_RELEASE_QUEUE];
static int g_release_queue_count;
static uint64_t g_buffer_release_trace_total;
static uint64_t g_buffer_release_trace_gpu_immediate;
static uint64_t g_buffer_release_trace_queued;
static uint64_t g_buffer_release_trace_flushed;
static uint64_t g_scanout_trace_submits;
static uint64_t g_scanout_trace_rebinds;

/* Input state */
static int      g_mouse_fd = -1;
static int      g_kbd_fd   = -1;
static int16_t  g_cursor_x, g_cursor_y;
static uint8_t  g_buttons;
static uint32_t g_serial;

#define TASKBAR_H          36

static int rect_area(const struct damage_rect *r)
{
    return (r->x2 - r->x1) * (r->y2 - r->y1);
}

static int rects_touch_or_overlap(const struct damage_rect *a,
                                  const struct damage_rect *b)
{
    return a->x1 <= b->x2 && a->x2 >= b->x1 &&
           a->y1 <= b->y2 && a->y2 >= b->y1;
}

static void rect_merge(struct damage_rect *dst, const struct damage_rect *src)
{
    if (src->x1 < dst->x1) dst->x1 = src->x1;
    if (src->y1 < dst->y1) dst->y1 = src->y1;
    if (src->x2 > dst->x2) dst->x2 = src->x2;
    if (src->y2 > dst->y2) dst->y2 = src->y2;
}

static void damage_union_bounds(struct damage_rect *out)
{
    *out = g_damage[0];
    for (int i = 1; i < g_damage_count; i++)
        rect_merge(out, &g_damage[i]);
}

static void damage_collapse_to_union(void)
{
    struct damage_rect u;

    if (g_damage_count <= 1)
        return;
    damage_union_bounds(&u);
    g_damage[0] = u;
    g_damage_count = 1;
}

static void damage_rect(int32_t x, int32_t y, int32_t w, int32_t h)
{
    struct damage_rect r;
    int32_t x2 = x + w;
    int32_t y2 = y + h;

    if (w <= 0 || h <= 0 || g_fb_w == 0 || g_fb_h == 0)
        return;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x2 > (int32_t)g_fb_w) x2 = (int32_t)g_fb_w;
    if (y2 > (int32_t)g_fb_h) y2 = (int32_t)g_fb_h;
    if (x >= x2 || y >= y2)
        return;

    r.x1 = x;
    r.y1 = y;
    r.x2 = x2;
    r.y2 = y2;

    for (int i = 0; i < g_damage_count; i++) {
        if (rects_touch_or_overlap(&g_damage[i], &r)) {
            rect_merge(&g_damage[i], &r);
            for (int j = 0; j < g_damage_count; j++) {
                if (j != i && rects_touch_or_overlap(&g_damage[i], &g_damage[j])) {
                    rect_merge(&g_damage[i], &g_damage[j]);
                    g_damage[j] = g_damage[g_damage_count - 1];
                    g_damage_count--;
                    j--;
                }
            }
            return;
        }
    }

    if (g_damage_count < MAX_DAMAGE_RECTS) {
        g_damage[g_damage_count++] = r;
    } else {
        g_damage[g_damage_count - 1] = r;
        damage_collapse_to_union();
    }
}

static void damage_full_reason(enum full_damage_reason reason)
{
    if (g_fb_w == 0 || g_fb_h == 0)
        return;
    if (reason < 0 || reason >= FULL_DAMAGE_COUNT)
        reason = FULL_DAMAGE_OTHER;
    g_damage_stats.full_damage[reason]++;
    g_full_repaint_pending = 1;
    g_damage[0].x1 = 0;
    g_damage[0].y1 = 0;
    g_damage[0].x2 = (int32_t)g_fb_w;
    g_damage[0].y2 = (int32_t)g_fb_h;
    g_damage_count = 1;
}

static void damage_full_for_flip(void)
{
    if (g_fb_w == 0 || g_fb_h == 0)
        return;
    g_damage_stats.full_damage[FULL_DAMAGE_OTHER]++;
    g_damage[0].x1 = 0;
    g_damage[0].y1 = 0;
    g_damage[0].x2 = (int32_t)g_fb_w;
    g_damage[0].y2 = (int32_t)g_fb_h;
    g_damage_count = 1;
}

static int damage_repair_interval_ms(void)
{
    static int initialized;
    static int interval_ms;
    const char *env;

    if (initialized)
        return interval_ms;
    initialized = 1;
    env = getenv("XV6_WLCOMP_REPAIR_MS");
    if (env && *env) {
        interval_ms = atoi(env);
        if (interval_ms < 0)
            interval_ms = 0;
    }
    interval_ms = cmdline_int_value("wlcomp_repair_ms", interval_ms);
    return interval_ms;
}

static int damage_stats_interval_ms(void)
{
    static int initialized;
    static int interval_ms;
    const char *env;

    if (initialized)
        return interval_ms;
    initialized = 1;
    env = getenv("XV6_WLCOMP_STATS_MS");
    if (env && *env) {
        interval_ms = atoi(env);
        if (interval_ms < 0)
            interval_ms = 0;
    }
    interval_ms = cmdline_int_value("wlcomp_stats_ms", interval_ms);
    return interval_ms;
}

static void damage_stats_maybe_log(uint32_t now)
{
    static uint32_t next_log_ms;
    static struct wlcomp_damage_stats last;
    struct wlcomp_damage_stats cur;
    int interval_ms = damage_stats_interval_ms();

    if (interval_ms <= 0)
        return;
    if (next_log_ms == 0)
        next_log_ms = now + (uint32_t)interval_ms;
    if (now < next_log_ms)
        return;
    cur = g_damage_stats;
    fprintf(stderr,
            "wlcomp: damage stats frames=%lu rects=%lu pixels=%lu full=%lu "
            "union=%lu avg_interval=%lums max_interval=%lums "
            "late25=%lu late40=%lu acquire_blocked=%lu "
            "acquire_recovered=%lu display_waits=%lu "
            "display_not_ready=%lu display_recovered=%lu "
            "gpu_compose=%lu gpu_pixels=%lu "
            "full_causes other=%lu init=%lu "
            "repair=%lu acquire=%lu desktop=%lu menu=%lu resize=%lu "
            "client=%lu iwin=%lu mode=%s\n",
            cur.frames - last.frames,
            cur.present_rects - last.present_rects,
            cur.present_pixels - last.present_pixels,
            cur.present_full_frames - last.present_full_frames,
            cur.present_union_collapses - last.present_union_collapses,
            (cur.frames - last.frames) > 1 ?
                (cur.frame_interval_sum_ms - last.frame_interval_sum_ms) /
                    ((cur.frames - last.frames) - 1) : 0,
            cur.frame_interval_max_ms,
            cur.frame_interval_over_25ms - last.frame_interval_over_25ms,
            cur.frame_interval_over_40ms - last.frame_interval_over_40ms,
            cur.acquire_blocked_frames - last.acquire_blocked_frames,
            cur.acquire_stall_recoveries - last.acquire_stall_recoveries,
            cur.display_waits - last.display_waits,
            cur.display_wait_not_ready - last.display_wait_not_ready,
            cur.display_stall_recoveries - last.display_stall_recoveries,
            cur.gpu_compose_presents - last.gpu_compose_presents,
            cur.gpu_compose_pixels - last.gpu_compose_pixels,
            cur.full_damage[FULL_DAMAGE_OTHER] -
                last.full_damage[FULL_DAMAGE_OTHER],
            cur.full_damage[FULL_DAMAGE_INIT] -
                last.full_damage[FULL_DAMAGE_INIT],
            cur.full_damage[FULL_DAMAGE_REPAIR] -
                last.full_damage[FULL_DAMAGE_REPAIR],
            cur.full_damage[FULL_DAMAGE_ACQUIRE] -
                last.full_damage[FULL_DAMAGE_ACQUIRE],
            cur.full_damage[FULL_DAMAGE_DESKTOP] -
                last.full_damage[FULL_DAMAGE_DESKTOP],
            cur.full_damage[FULL_DAMAGE_MENU] -
                last.full_damage[FULL_DAMAGE_MENU],
            cur.full_damage[FULL_DAMAGE_RESIZE] -
                last.full_damage[FULL_DAMAGE_RESIZE],
            cur.full_damage[FULL_DAMAGE_CLIENT] -
                last.full_damage[FULL_DAMAGE_CLIENT],
            cur.full_damage[FULL_DAMAGE_IWIN] -
                last.full_damage[FULL_DAMAGE_IWIN],
            g_fb_direct_scanout ? "direct-scanout" :
                (g_fb_bo_backed ? "bo-present" : "user-blit"));
    last = cur;
    next_log_ms = now + (uint32_t)interval_ms;
}

static void fps_probe_maybe_write(uint32_t now)
{
    static uint32_t next_write_ms;
    static uint32_t last_ms;
    static struct wlcomp_damage_stats last;
    struct wlcomp_damage_stats cur;
    uint32_t elapsed;
    uint64_t frames;
    uint64_t pixels;
    uint64_t fps_x1000;
    FILE *fp;

    if (next_write_ms == 0) {
        next_write_ms = now + 1000;
        last_ms = now;
        last = g_damage_stats;
        return;
    }
    if (now < next_write_ms)
        return;

    cur = g_damage_stats;
    elapsed = now - last_ms;
    if (elapsed == 0)
        elapsed = 1;
    frames = cur.frames - last.frames;
    pixels = cur.present_pixels - last.present_pixels;
    fps_x1000 = frames * 1000000ULL / elapsed;

    fp = fopen("/tmp/wlcomp-fps", "w");
    if (fp) {
        fprintf(fp,
                "fps=%lu.%03lu frames=%lu interval_ms=%u rects=%lu pixels=%lu full=%lu avg_frame_ms=%lu max_frame_ms=%lu late25=%lu late40=%lu acquire_blocked=%lu gpu_compose=%lu gpu_pixels=%lu mode=%s fb=%ux%u\n",
                fps_x1000 / 1000, fps_x1000 % 1000, frames, elapsed,
                cur.present_rects - last.present_rects, pixels,
                cur.present_full_frames - last.present_full_frames,
                frames > 1 ?
                    (cur.frame_interval_sum_ms - last.frame_interval_sum_ms) /
                        (frames - 1) : 0,
                g_damage_interval_max_ms,
                cur.frame_interval_over_25ms - last.frame_interval_over_25ms,
                cur.frame_interval_over_40ms - last.frame_interval_over_40ms,
                cur.acquire_blocked_frames - last.acquire_blocked_frames,
                cur.gpu_compose_presents - last.gpu_compose_presents,
                cur.gpu_compose_pixels - last.gpu_compose_pixels,
                g_fb_direct_scanout ? "direct-scanout" :
                    (g_fb_bo_backed ? "bo-present" : "user-blit"),
                g_fb_w, g_fb_h);
        fclose(fp);
    }

    last = cur;
    last_ms = now;
    g_damage_interval_max_ms = 0;
    next_write_ms = now + 1000;
}

static void release_framebuffer_backing(void)
{
    if (!g_fb_buf)
        return;
    if (g_fb_direct_scanout) {
        munmap(g_fb_buf, g_fb_bo_size);
    } else if (g_fb_bo_backed) {
        if (g_fb_virgl_back_target_ready) {
            if (g_fb_virgl_back_target.bo_handle) {
                struct fb_gpu_bo_destroy destroy;

                memset(&destroy, 0, sizeof(destroy));
                destroy.handle = g_fb_virgl_back_target.bo_handle;
                ioctl(g_fb_fd, FB_GPU_BO_DESTROY, &destroy);
            }
            if (g_fb_virgl_back_target.buf)
                munmap(g_fb_virgl_back_target.buf,
                       g_fb_virgl_back_target.size);
            if (g_fb_virgl_back_target.resource_id) {
                struct fb_gpu_virgl_resource_destroy destroy_res;

                memset(&destroy_res, 0, sizeof(destroy_res));
                destroy_res.resource_id = g_fb_virgl_back_target.resource_id;
                ioctl(g_fb_fd, FB_GPU_VIRGL_RESOURCE_DESTROY, &destroy_res);
            }
        }
        if (g_fb_virgl_extra_target_ready) {
            if (g_fb_virgl_extra_target.bo_handle) {
                struct fb_gpu_bo_destroy destroy;

                memset(&destroy, 0, sizeof(destroy));
                destroy.handle = g_fb_virgl_extra_target.bo_handle;
                ioctl(g_fb_fd, FB_GPU_BO_DESTROY, &destroy);
            }
            if (g_fb_virgl_extra_target.buf)
                munmap(g_fb_virgl_extra_target.buf,
                       g_fb_virgl_extra_target.size);
            if (g_fb_virgl_extra_target.resource_id) {
                struct fb_gpu_virgl_resource_destroy destroy_res;

                memset(&destroy_res, 0, sizeof(destroy_res));
                destroy_res.resource_id = g_fb_virgl_extra_target.resource_id;
                ioctl(g_fb_fd, FB_GPU_VIRGL_RESOURCE_DESTROY, &destroy_res);
            }
        }
        if (g_fb_bo_handle) {
            struct fb_gpu_bo_destroy destroy;

            memset(&destroy, 0, sizeof(destroy));
            destroy.handle = g_fb_bo_handle;
            ioctl(g_fb_fd, FB_GPU_BO_DESTROY, &destroy);
        }
        munmap(g_fb_buf, g_fb_bo_size);
        if (g_fb_virgl_resource_id) {
            struct fb_gpu_virgl_resource_destroy destroy_res;

            memset(&destroy_res, 0, sizeof(destroy_res));
            destroy_res.resource_id = g_fb_virgl_resource_id;
            ioctl(g_fb_fd, FB_GPU_VIRGL_RESOURCE_DESTROY, &destroy_res);
        }
        if (g_fb_virgl_ctx_id) {
            struct fb_gpu_virgl_ctx destroy_ctx;

            memset(&destroy_ctx, 0, sizeof(destroy_ctx));
            destroy_ctx.ctx_id = g_fb_virgl_ctx_id;
            ioctl(g_fb_fd, FB_GPU_VIRGL_CTX_DESTROY, &destroy_ctx);
        }
    } else {
        free(g_fb_buf);
    }
    g_fb_buf = NULL;
    g_fb_bo_size = 0;
    g_fb_bo_handle = 0;
    g_fb_bo_backed = 0;
    g_fb_virgl_backed = 0;
    g_fb_virgl_ctx_id = 0;
    g_fb_virgl_resource_id = 0;
    g_fb_direct_scanout = 0;
    memset(&g_fb_virgl_back_target, 0, sizeof(g_fb_virgl_back_target));
    g_fb_virgl_back_target_ready = 0;
    memset(&g_fb_virgl_extra_target, 0, sizeof(g_fb_virgl_extra_target));
    g_fb_virgl_extra_target_ready = 0;
    g_fb_virgl_flip_count = 0;
}

static int wlcomp_virgl_fb_flip_enabled(void)
{
    static int initialized;
    static int enabled;
    const char *env;

    if (initialized)
        return enabled;
    initialized = 1;
    enabled = cmdline_int_value("wlcomp_virgl_fb_flip", 0) != 0;
    env = getenv("XV6_WLCOMP_VIRGL_FB_FLIP");
    if (env && *env)
        enabled = strcmp(env, "0") != 0 && strcmp(env, "no") != 0 &&
                  strcmp(env, "false") != 0 && strcmp(env, "off") != 0;
    return enabled;
}

static int wlcomp_page_flip_present_enabled(void)
{
    static int initialized;
    static int enabled;
    const char *env;

    if (initialized)
        return enabled;
    initialized = 1;
    enabled = cmdline_int_value("wlcomp_page_flip_present", 0) != 0;
    env = getenv("XV6_WLCOMP_PAGE_FLIP_PRESENT");
    if (env && *env)
        enabled = strcmp(env, "0") != 0 &&
                  strcmp(env, "no") != 0 &&
                  strcmp(env, "false") != 0 &&
                  strcmp(env, "off") != 0;
    return enabled;
}

static int wlcomp_scanout_perf_enabled(void)
{
    static int initialized;
    static int enabled;
    const char *env;

    if (initialized)
        return enabled;
    initialized = 1;
    enabled = cmdline_int_value("wlcomp_scanout_perf", 0) != 0;
    env = getenv("XV6_WLCOMP_SCANOUT_PERF");
    if (env && *env)
        enabled = strcmp(env, "0") != 0 &&
                  strcmp(env, "no") != 0 &&
                  strcmp(env, "false") != 0 &&
                  strcmp(env, "off") != 0;
    if (enabled)
        fprintf(stderr, "wlcomp: scanout perf diagnostics enabled\n");
    return enabled;
}

static void wlcomp_scanout_perf_note(uint64_t dt_us, uint32_t x, uint32_t y,
                                     uint32_t w, uint32_t h, int ret,
                                     int page_flip)
{
    static uint32_t next_log_ms;
    static uint64_t submits;
    static uint64_t failures;
    static uint64_t page_flips;
    static uint64_t total_us;
    static uint64_t max_us;
    static uint64_t pixels;
    static uint64_t bucket_lt2;
    static uint64_t bucket_2_5;
    static uint64_t bucket_5_10;
    static uint64_t bucket_10_20;
    static uint64_t bucket_ge20;
    static uint32_t last_x, last_y, last_w, last_h;
    uint32_t now;
    uint64_t count;

    if (!wlcomp_scanout_perf_enabled())
        return;
    submits++;
    if (ret != 0)
        failures++;
    if (page_flip)
        page_flips++;
    total_us += dt_us;
    if (dt_us > max_us)
        max_us = dt_us;
    pixels += (uint64_t)w * h;
    if (dt_us < 2000)
        bucket_lt2++;
    else if (dt_us < 5000)
        bucket_2_5++;
    else if (dt_us < 10000)
        bucket_5_10++;
    else if (dt_us < 20000)
        bucket_10_20++;
    else
        bucket_ge20++;
    last_x = x;
    last_y = y;
    last_w = w;
    last_h = h;

    now = get_time_ms();
    if (next_log_ms == 0)
        next_log_ms = now + 1000;
    if (now < next_log_ms)
        return;
    count = submits ? submits : 1;
    fprintf(stderr,
            "wlcomp: scanout-perf submits=%lu failures=%lu page_flips=%lu "
            "avg_us=%lu max_us=%lu avg_pixels=%lu "
            "buckets_lt2=%lu b2_5=%lu b5_10=%lu b10_20=%lu b20p=%lu "
            "last_rect=%u,%u %ux%u\n",
            (unsigned long)submits,
            (unsigned long)failures,
            (unsigned long)page_flips,
            (unsigned long)(total_us / count),
            (unsigned long)max_us,
            (unsigned long)(pixels / count),
            (unsigned long)bucket_lt2,
            (unsigned long)bucket_2_5,
            (unsigned long)bucket_5_10,
            (unsigned long)bucket_10_20,
            (unsigned long)bucket_ge20,
            last_x, last_y, last_w, last_h);
    submits = 0;
    failures = 0;
    page_flips = 0;
    total_us = 0;
    max_us = 0;
    pixels = 0;
    bucket_lt2 = 0;
    bucket_2_5 = 0;
    bucket_5_10 = 0;
    bucket_10_20 = 0;
    bucket_ge20 = 0;
    next_log_ms = now + 1000;
}

static int wlcomp_virgl_framebuffer_flip_ready(void)
{
    return g_fb_virgl_backed && g_fb_virgl_back_target_ready &&
           (wlcomp_virgl_fb_flip_enabled() ||
            wlcomp_page_flip_present_enabled());
}

static int wlcomp_virgl_fb_copy_before_flip_enabled(void)
{
    static int initialized;
    static int enabled;
    const char *env;

    if (initialized)
        return enabled;
    initialized = 1;
    enabled = cmdline_int_value("wlcomp_virgl_fb_copy_before_flip", 0) != 0;
    env = getenv("XV6_WLCOMP_VIRGL_FB_COPY_BEFORE_FLIP");
    if (env && *env)
        enabled = strcmp(env, "0") != 0 && strcmp(env, "no") != 0 &&
                  strcmp(env, "false") != 0 && strcmp(env, "off") != 0;
    return enabled;
}

static int wlcomp_virgl_fb_copy_damage_before_flip_enabled(void)
{
    static int initialized;
    static int enabled;
    const char *env;

    if (initialized)
        return enabled;
    initialized = 1;
    enabled = cmdline_int_value("wlcomp_virgl_fb_copy_damage_before_flip", 0) != 0;
    env = getenv("XV6_WLCOMP_VIRGL_FB_COPY_DAMAGE_BEFORE_FLIP");
    if (env && *env)
        enabled = strcmp(env, "0") != 0 && strcmp(env, "no") != 0 &&
                  strcmp(env, "false") != 0 && strcmp(env, "off") != 0;
    return enabled;
}

static int wlcomp_virgl_swap_render_target(void)
{
    struct wlcomp_virgl_fb_target old_front;
    static int flip_logs;

    if (!wlcomp_virgl_framebuffer_flip_ready())
        return 0;
    old_front.buf = g_fb_buf;
    old_front.pitch = g_fb_pitch;
    old_front.size = g_fb_bo_size;
    old_front.bo_handle = g_fb_bo_handle;
    old_front.resource_id = g_fb_virgl_resource_id;

    if (!old_front.buf || old_front.pitch == 0 ||
        old_front.bo_handle == 0 || old_front.resource_id == 0 ||
        !g_fb_virgl_back_target.buf ||
        g_fb_virgl_back_target.pitch == 0 ||
        g_fb_virgl_back_target.bo_handle == 0 ||
        g_fb_virgl_back_target.resource_id == 0)
        return 0;

    g_fb_buf = g_fb_virgl_back_target.buf;
    g_fb_pitch = g_fb_virgl_back_target.pitch;
    g_fb_bo_size = g_fb_virgl_back_target.size;
    g_fb_bo_handle = g_fb_virgl_back_target.bo_handle;
    g_fb_virgl_resource_id = g_fb_virgl_back_target.resource_id;
    if (g_fb_virgl_extra_target_ready) {
        g_fb_virgl_back_target = g_fb_virgl_extra_target;
        g_fb_virgl_extra_target = old_front;
    } else {
        g_fb_virgl_back_target = old_front;
    }
    g_fb_virgl_flip_count++;

    if (flip_logs < 16) {
        fprintf(stderr,
                "wlcomp: virgl framebuffer flip render_res=%u render_handle=%u back_res=%u back_handle=%u extra_res=%u extra_handle=%u count=%lu\n",
                g_fb_virgl_resource_id, g_fb_bo_handle,
                g_fb_virgl_back_target.resource_id,
                g_fb_virgl_back_target.bo_handle,
                g_fb_virgl_extra_target_ready ?
                    g_fb_virgl_extra_target.resource_id : 0,
                g_fb_virgl_extra_target_ready ?
                    g_fb_virgl_extra_target.bo_handle : 0,
                (unsigned long)g_fb_virgl_flip_count);
        flip_logs++;
    }
    return 1;
}

static int wlcomp_virgl_copy_displayed_to_render_target_rects(
    const struct damage_rect *rects, int rect_count)
{
    struct fb_gpu_bo_copy copy;
    struct damage_rect u;
    uint64_t copy_pixels;
    int full_copy;
    static int copy_logs;
    static int copy_error_logs;
    static uint64_t stat_copies;
    static uint64_t stat_pixels;
    static uint64_t stat_full;
    static uint64_t stat_partial;
    static uint32_t next_stat_ms;

    if (!wlcomp_virgl_framebuffer_flip_ready() ||
        !wlcomp_virgl_fb_copy_before_flip_enabled())
        return 0;
    if (g_fb_bo_handle == 0 ||
        g_fb_virgl_back_target.bo_handle == 0 ||
        g_fb_w == 0 || g_fb_h == 0)
        return 0;

    if (rects && rect_count > 0 &&
        wlcomp_virgl_fb_copy_damage_before_flip_enabled()) {
        int copied = 0;

        for (int i = 0; i < rect_count; i++) {
            struct damage_rect r = rects[i];
            uint64_t copy_pixels;
            int full_copy;

            if (r.x1 < 0) r.x1 = 0;
            if (r.y1 < 0) r.y1 = 0;
            if (r.x2 > (int32_t)g_fb_w) r.x2 = (int32_t)g_fb_w;
            if (r.y2 > (int32_t)g_fb_h) r.y2 = (int32_t)g_fb_h;
            if (r.x1 >= r.x2 || r.y1 >= r.y2)
                continue;
            memset(&copy, 0, sizeof(copy));
            copy.src_handle = g_fb_virgl_back_target.bo_handle;
            copy.dst_handle = g_fb_bo_handle;
            copy.src_x = (uint32_t)r.x1;
            copy.src_y = (uint32_t)r.y1;
            copy.dst_x = (uint32_t)r.x1;
            copy.dst_y = (uint32_t)r.y1;
            copy.w = (uint32_t)(r.x2 - r.x1);
            copy.h = (uint32_t)(r.y2 - r.y1);
            if (ioctl(g_fb_fd, FB_GPU_BO_COPY, &copy) != 0) {
                if (copy_error_logs < 8) {
                    fprintf(stderr,
                            "wlcomp: virgl framebuffer copy-before-flip failed errno=%d (%s) src_handle=%u dst_handle=%u rect=%u,%u %ux%u\n",
                            errno, strerror(errno), copy.src_handle,
                            copy.dst_handle, copy.dst_x, copy.dst_y,
                            copy.w, copy.h);
                    copy_error_logs++;
                }
                return 0;
            }
            copied = 1;
            copy_pixels = (uint64_t)copy.w * copy.h;
            full_copy = copy.dst_x == 0 && copy.dst_y == 0 &&
                        copy.w == g_fb_w && copy.h == g_fb_h;
            stat_copies++;
            stat_pixels += copy_pixels;
            if (full_copy)
                stat_full++;
            else
                stat_partial++;
            if (copy_logs < 16) {
                fprintf(stderr,
                        "wlcomp: virgl framebuffer copy-before-flip src_handle=%u dst_handle=%u rect=%u,%u %ux%u fence=%lu\n",
                        copy.src_handle, copy.dst_handle, copy.dst_x,
                        copy.dst_y, copy.w, copy.h,
                        (unsigned long)copy.fence);
                copy_logs++;
            }
        }
        if (!copied)
            return 0;
        goto maybe_log_stats;
    }

    memset(&copy, 0, sizeof(copy));
    copy.src_handle = g_fb_virgl_back_target.bo_handle;
    copy.dst_handle = g_fb_bo_handle;
    if (rects && rect_count > 0) {
        int have = 0;

        for (int i = 0; i < rect_count; i++) {
            struct damage_rect r = rects[i];

            if (r.x1 < 0) r.x1 = 0;
            if (r.y1 < 0) r.y1 = 0;
            if (r.x2 > (int32_t)g_fb_w) r.x2 = (int32_t)g_fb_w;
            if (r.y2 > (int32_t)g_fb_h) r.y2 = (int32_t)g_fb_h;
            if (r.x1 >= r.x2 || r.y1 >= r.y2)
                continue;
            if (!have) {
                u = r;
                have = 1;
            } else {
                rect_merge(&u, &r);
            }
        }
        if (!have)
            return 0;
        copy.src_x = (uint32_t)u.x1;
        copy.src_y = (uint32_t)u.y1;
        copy.dst_x = (uint32_t)u.x1;
        copy.dst_y = (uint32_t)u.y1;
        copy.w = (uint32_t)(u.x2 - u.x1);
        copy.h = (uint32_t)(u.y2 - u.y1);
    } else {
        copy.w = g_fb_w;
        copy.h = g_fb_h;
    }
    if (ioctl(g_fb_fd, FB_GPU_BO_COPY, &copy) != 0) {
        if (copy_error_logs < 8) {
            fprintf(stderr,
                    "wlcomp: virgl framebuffer copy-before-flip failed errno=%d (%s) src_handle=%u dst_handle=%u rect=%u,%u %ux%u\n",
                    errno, strerror(errno), copy.src_handle,
                    copy.dst_handle, copy.dst_x, copy.dst_y,
                    copy.w, copy.h);
            copy_error_logs++;
        }
        return 0;
    }
    copy_pixels = (uint64_t)copy.w * copy.h;
    full_copy = copy.dst_x == 0 && copy.dst_y == 0 &&
                copy.w == g_fb_w && copy.h == g_fb_h;
    stat_copies++;
    stat_pixels += copy_pixels;
    if (full_copy)
        stat_full++;
    else
        stat_partial++;
    if (copy_logs < 16) {
        fprintf(stderr,
                "wlcomp: virgl framebuffer copy-before-flip src_handle=%u dst_handle=%u rect=%u,%u %ux%u fence=%lu\n",
                copy.src_handle, copy.dst_handle, copy.dst_x, copy.dst_y,
                copy.w, copy.h,
                (unsigned long)copy.fence);
        copy_logs++;
    }
maybe_log_stats:
    if (next_stat_ms == 0)
        next_stat_ms = get_time_ms() + 1000;
    if (get_time_ms() >= next_stat_ms) {
        uint64_t copies = stat_copies ? stat_copies : 1;

        fprintf(stderr,
                "wlcomp: virgl framebuffer copy-before-flip-stats copies=%lu avg_pixels=%lu full=%lu partial=%lu\n",
                (unsigned long)stat_copies,
                (unsigned long)(stat_pixels / copies),
                (unsigned long)stat_full,
                (unsigned long)stat_partial);
        stat_copies = 0;
        stat_pixels = 0;
        stat_full = 0;
        stat_partial = 0;
        next_stat_ms = get_time_ms() + 1000;
    }
    return 1;
}

static int wlcomp_virgl_copy_displayed_to_render_target(void)
{
    return wlcomp_virgl_copy_displayed_to_render_target_rects(NULL, 0);
}

static int alloc_virgl_framebuffer_target(uint32_t ctx_id,
                                          const char *label,
                                          struct wlcomp_virgl_fb_target *out)
{
    struct fb_gpu_virgl_resource_create res;
    struct fb_gpu_virgl_resource_export_fd export_fd;
    struct fb_gpu_bo_import_fd import_fd;
    int ret;

    memset(&res, 0, sizeof(res));
    memset(out, 0, sizeof(*out));
    res.ctx_id = ctx_id;
    res.target = WLCOMP_PIPE_TEXTURE_2D;
    res.format = WLCOMP_VIRGL_FORMAT_B8G8R8A8_UNORM;
    res.bind = WLCOMP_VIRGL_BIND_RENDER_TARGET |
               WLCOMP_VIRGL_BIND_SAMPLER_VIEW |
               WLCOMP_VIRGL_BIND_DISPLAY_TARGET |
               WLCOMP_VIRGL_BIND_SCANOUT |
               WLCOMP_VIRGL_BIND_SHARED |
               WLCOMP_VIRGL_BIND_LINEAR;
    res.width = g_fb_w;
    res.height = g_fb_h;
    res.depth = 1;
    res.array_size = 1;
    ret = ioctl(g_fb_fd, FB_GPU_VIRGL_RESOURCE_CREATE, &res);
    if (ret != 0 || res.resource_id == 0 || res.addr == 0 || res.size == 0) {
        fprintf(stderr,
                "wlcomp: virgl framebuffer %s resource create failed ret=%d errno=%d (%s) ctx=%u res=%u addr=0x%lx size=%lu\n",
                label, ret, errno, strerror(errno), ctx_id, res.resource_id,
                (unsigned long)res.addr, (unsigned long)res.size);
        return -1;
    }

    memset(&export_fd, 0, sizeof(export_fd));
    export_fd.resource_id = res.resource_id;
    export_fd.fd = -1;
    ret = ioctl(g_fb_fd, FB_GPU_VIRGL_RESOURCE_EXPORT_FD, &export_fd);
    if (ret != 0 || export_fd.fd < 0) {
        fprintf(stderr,
                "wlcomp: virgl framebuffer %s export failed ret=%d errno=%d (%s) res=%u fd=%d\n",
                label, ret, errno, strerror(errno), res.resource_id,
                export_fd.fd);
        goto fail_res;
    }

    memset(&import_fd, 0, sizeof(import_fd));
    import_fd.fd = export_fd.fd;
    import_fd.format = DRM_FORMAT_XRGB8888;
    import_fd.modifier = DRM_FORMAT_MOD_LINEAR;
    import_fd.plane_count = 1;
    import_fd.offsets[0] = 0;
    import_fd.strides[0] = export_fd.pitch;
    ret = ioctl(g_fb_fd, FB_GPU_BO_IMPORT_FD, &import_fd);
    close(export_fd.fd);
    if (ret != 0 || import_fd.handle == 0 ||
        import_fd.addr == 0 || import_fd.size == 0) {
        fprintf(stderr,
                "wlcomp: virgl framebuffer %s import failed ret=%d errno=%d (%s) res=%u handle=%u addr=0x%lx size=%lu\n",
                label, ret, errno, strerror(errno), res.resource_id,
                import_fd.handle, (unsigned long)import_fd.addr,
                (unsigned long)import_fd.size);
        goto fail_res;
    }

    out->buf = (uint32_t *)(uintptr_t)import_fd.addr;
    out->pitch = import_fd.pitch;
    out->size = import_fd.size;
    out->bo_handle = import_fd.handle;
    out->resource_id = res.resource_id;
    memset(out->buf, 0, (size_t)out->size);
    fprintf(stderr,
            "wlcomp: using virgl framebuffer %s target res=%u ctx=%u handle=%u addr=0x%lx size=%lu pitch=%u\n",
            label, out->resource_id, ctx_id, out->bo_handle,
            (unsigned long)import_fd.addr, (unsigned long)out->size,
            out->pitch);
    return 0;

fail_res:
    if (res.resource_id != 0) {
        struct fb_gpu_virgl_resource_destroy destroy_res;

        memset(&destroy_res, 0, sizeof(destroy_res));
        destroy_res.resource_id = res.resource_id;
        ioctl(g_fb_fd, FB_GPU_VIRGL_RESOURCE_DESTROY, &destroy_res);
    }
    return -1;
}

static int alloc_virgl_framebuffer_backing(void)
{
    struct fb_gpu_virgl_ctx ctx;
    struct wlcomp_virgl_fb_target front;
    int target_count;

    memset(&ctx, 0, sizeof(ctx));
    snprintf(ctx.debug_name, sizeof(ctx.debug_name), "wlcomp-scanout");
    if (ioctl(g_fb_fd, FB_GPU_VIRGL_CTX_CREATE, &ctx) != 0 ||
        ctx.ctx_id == 0) {
        fprintf(stderr,
                "wlcomp: virgl framebuffer ctx create failed errno=%d (%s)\n",
                errno, strerror(errno));
        return -1;
    }

    if (alloc_virgl_framebuffer_target(ctx.ctx_id, "front", &front) != 0)
        goto fail_ctx;

    g_fb_buf = front.buf;
    g_fb_pitch = front.pitch;
    g_fb_bo_size = front.size;
    g_fb_bo_handle = front.bo_handle;
    g_fb_bo_backed = 1;
    g_fb_virgl_backed = 1;
    g_fb_virgl_ctx_id = ctx.ctx_id;
    g_fb_virgl_resource_id = front.resource_id;

    target_count = cmdline_int_value("wlcomp_virgl_fb_buffers", 1);
    if (target_count < 1)
        target_count = 1;
    if (wlcomp_page_flip_present_enabled() && target_count < 2)
        target_count = 2;
    if (cmdline_int_value("wlcomp_virgl_fb_buffers", 1) >= 3 &&
        target_count < 3)
        target_count = 3;
    if (target_count > 1) {
        if (alloc_virgl_framebuffer_target(ctx.ctx_id, "back",
                                           &g_fb_virgl_back_target) == 0) {
            g_fb_virgl_back_target_ready = 1;
            fprintf(stderr,
                    "wlcomp: virgl framebuffer double-buffer prep ready front_res=%u back_res=%u\n",
                    g_fb_virgl_resource_id,
                    g_fb_virgl_back_target.resource_id);
        } else if (cmdline_flag_enabled("wlcomp_virgl_fb_buffers_require")) {
            goto fail_front;
        } else {
            fprintf(stderr,
                    "wlcomp: virgl framebuffer back target unavailable; continuing single-buffered\n");
        }
    }
    if (target_count > 2 && g_fb_virgl_back_target_ready) {
        if (alloc_virgl_framebuffer_target(ctx.ctx_id, "back2",
                                           &g_fb_virgl_extra_target) == 0) {
            g_fb_virgl_extra_target_ready = 1;
            fprintf(stderr,
                    "wlcomp: virgl framebuffer triple-buffer prep ready front_res=%u back_res=%u extra_res=%u\n",
                    g_fb_virgl_resource_id,
                    g_fb_virgl_back_target.resource_id,
                    g_fb_virgl_extra_target.resource_id);
        } else if (cmdline_flag_enabled("wlcomp_virgl_fb_buffers_require")) {
            goto fail_front;
        } else {
            fprintf(stderr,
                    "wlcomp: virgl framebuffer third target unavailable; continuing double-buffered\n");
        }
    }
    return 0;

fail_front:
    if (g_fb_bo_handle) {
        struct fb_gpu_bo_destroy destroy;

        memset(&destroy, 0, sizeof(destroy));
        destroy.handle = g_fb_bo_handle;
        ioctl(g_fb_fd, FB_GPU_BO_DESTROY, &destroy);
    }
    if (g_fb_buf)
        munmap(g_fb_buf, g_fb_bo_size);
    if (g_fb_virgl_resource_id) {
        struct fb_gpu_virgl_resource_destroy destroy_res;

        memset(&destroy_res, 0, sizeof(destroy_res));
        destroy_res.resource_id = g_fb_virgl_resource_id;
        ioctl(g_fb_fd, FB_GPU_VIRGL_RESOURCE_DESTROY, &destroy_res);
    }
    g_fb_buf = NULL;
    g_fb_bo_handle = 0;
    g_fb_bo_size = 0;
    g_fb_virgl_resource_id = 0;
fail_ctx:
    if (ctx.ctx_id != 0)
        ioctl(g_fb_fd, FB_GPU_VIRGL_CTX_DESTROY, &ctx);
    return -1;
}

static int alloc_framebuffer_backing(void)
{
    struct fb_gpu_bo_create bo;
    struct fb_gpu_scanout_map scanout;
    size_t bytes = (size_t)g_fb_w * g_fb_h * 4;
    const char *use_direct = getenv("XV6_WLCOMP_FB_DIRECT");
    const char *use_bo = getenv("XV6_WLCOMP_FB_BO");
    const char *use_gpu_compose = getenv("XV6_WLCOMP_GPU_COMPOSE");
    int pageflip_diag_requested =
        cmdline_int_value("wlcomp_pageflip_diag", 0) != 0;
    int page_flip_present_requested = wlcomp_page_flip_present_enabled();
    int gpu_compose_requested =
        page_flip_present_requested ||
        pageflip_diag_requested ||
        cmdline_flag_enabled("wlcomp_gpu_compose") ||
        (use_gpu_compose && *use_gpu_compose &&
         atoi(use_gpu_compose) != 0);
    int direct_requested =
        (use_direct && strcmp(use_direct, "1") == 0) ||
        cmdline_flag_enabled("wlcomp_gpu_direct_scanout") ||
        cmdline_flag_enabled("wlcomp_fb_direct");
    int bo_requested =
        (use_bo && strcmp(use_bo, "1") == 0) ||
        cmdline_flag_enabled("wlcomp_fb_bo") ||
        gpu_compose_requested;

    if (direct_requested) {
        int ret;

        memset(&scanout, 0, sizeof(scanout));
        errno = 0;
        ret = ioctl(g_fb_fd, FB_GPU_SCANOUT_MAP, &scanout);
        if (ret == 0 &&
            scanout.addr && scanout.size && scanout.pitch) {
            g_fb_buf = (uint32_t *)(uintptr_t)scanout.addr;
            g_fb_pitch = scanout.pitch;
            g_fb_bo_size = scanout.size;
            g_fb_direct_scanout = 1;
            memset(g_fb_buf, 0, (size_t)scanout.size);
            fprintf(stderr,
                    "wlcomp: using direct scanout addr=0x%lx size=%lu pitch=%u\n",
                    scanout.addr, scanout.size, scanout.pitch);
            return 0;
        }
        fprintf(stderr,
                "wlcomp: direct scanout map failed ret=%d errno=%d (%s) addr=0x%lx size=%lu pitch=%u %ux%u\n",
                ret, errno, strerror(errno), scanout.addr, scanout.size,
                scanout.pitch, scanout.width, scanout.height);
    }

    if ((page_flip_present_requested ||
         pageflip_diag_requested ||
         cmdline_flag_enabled("wlcomp_virgl_fb") ||
         cmdline_flag_enabled("wlcomp_gl_target_fb")) &&
        !cmdline_flag_enabled("wlcomp_no_virgl_fb") &&
        alloc_virgl_framebuffer_backing() == 0)
        return 0;

    if (bo_requested) {
        int ret;

        memset(&bo, 0, sizeof(bo));
        bo.width = g_fb_w;
        bo.height = g_fb_h;
        bo.flags = FB_GPU_BO_F_EXPORTABLE;
        errno = 0;
        ret = ioctl(g_fb_fd, FB_GPU_BO_CREATE, &bo);
        if (ret == 0 && bo.addr && bo.size) {
            g_fb_buf = (uint32_t *)(uintptr_t)bo.addr;
            g_fb_pitch = bo.pitch;
            g_fb_bo_size = bo.size;
            g_fb_bo_handle = bo.handle;
            g_fb_bo_backed = 1;
            memset(g_fb_buf, 0, (size_t)bo.size);
            fprintf(stderr,
                    "wlcomp: using fb GPU buffer addr=0x%lx size=%lu pitch=%u handle=%u requested_by_gpu_compose=%d\n",
                    (uint64_t)bo.addr, bo.size, bo.pitch, bo.handle,
                    gpu_compose_requested);
            return 0;
        }
        fprintf(stderr,
                "wlcomp: FB_GPU_BO_CREATE backing failed ret=%d errno=%d (%s) addr=0x%lx size=%lu pitch=%u handle=%u requested_by_gpu_compose=%d d3d12_native_present_target=missing\n",
                ret, errno, strerror(errno), (uint64_t)bo.addr, bo.size,
                bo.pitch, bo.handle, gpu_compose_requested);
    }

    g_fb_buf = (uint32_t *)malloc(bytes);
    if (!g_fb_buf)
        return -1;
    g_fb_pitch = g_fb_w * 4;
    memset(g_fb_buf, 0, bytes);
    fprintf(stderr, "wlcomp: using malloc framebuffer backing\n");
    return 0;
}

static int present_virgl_scanout_rects(const struct damage_rect *scanout_rects,
                                       int scanout_rect_count)
{
    int ok = 1;

    if (g_fb_virgl_backed) {
        const struct damage_rect full_rect = {
            0, 0, (int32_t)g_fb_w, (int32_t)g_fb_h
        };
        static int scanout_error_logs;

        if (cmdline_flag_enabled("wlcomp_virgl_full_scanout")) {
            scanout_rects = &full_rect;
            scanout_rect_count = 1;
        } else if (!scanout_rects || scanout_rect_count <= 0) {
            scanout_rects = &full_rect;
            scanout_rect_count = 1;
        }
        if (wlcomp_page_flip_present_enabled()) {
            struct fb_gpu_page_flip flip;
            static uint32_t last_scanout_resource_id;
            static int page_flip_logs;
            uint64_t t0;
            uint64_t dt;
            int ret;

            memset(&flip, 0, sizeof(flip));
            flip.handle = g_fb_bo_handle;
            if (g_fb_virgl_resource_id != last_scanout_resource_id) {
                g_scanout_trace_rebinds++;
                last_scanout_resource_id = g_fb_virgl_resource_id;
            }
            g_scanout_trace_submits++;
            t0 = wlcomp_scanout_perf_enabled() ? get_time_us() : 0;
            ret = ioctl(g_fb_fd, FB_GPU_PAGE_FLIP, &flip);
            dt = t0 ? get_time_us() - t0 : 0;
            wlcomp_scanout_perf_note(dt, 0, 0, g_fb_w, g_fb_h, ret, 1);
            if (ret == 0) {
                if (flip.fence > g_fb_bo_last_present_fence)
                    g_fb_bo_last_present_fence = flip.fence;
                if (page_flip_logs < 8) {
                    fprintf(stderr,
                            "wlcomp: virgl framebuffer page-flip handle=%u res=%u fence=%lu\n",
                            g_fb_bo_handle, g_fb_virgl_resource_id,
                            (unsigned long)flip.fence);
                    page_flip_logs++;
                }
                return 1;
            }
            ok = 0;
            if (scanout_error_logs < 8) {
                fprintf(stderr,
                        "wlcomp: virgl framebuffer page-flip failed errno=%d (%s) handle=%u res=%u\n",
                        errno, strerror(errno), g_fb_bo_handle,
                        g_fb_virgl_resource_id);
                scanout_error_logs++;
            }
            return ok;
        }
        if (scanout_rect_count > 1 &&
            !cmdline_flag_enabled("wlcomp_virgl_multi_flush")) {
            static struct damage_rect union_rect;

            union_rect = scanout_rects[0];
            for (int i = 1; i < scanout_rect_count; i++) {
                const struct damage_rect *r = &scanout_rects[i];

                if (r->x1 >= r->x2 || r->y1 >= r->y2)
                    continue;
                if (r->x1 < union_rect.x1) union_rect.x1 = r->x1;
                if (r->y1 < union_rect.y1) union_rect.y1 = r->y1;
                if (r->x2 > union_rect.x2) union_rect.x2 = r->x2;
                if (r->y2 > union_rect.y2) union_rect.y2 = r->y2;
            }
            if (union_rect.x1 < 0) union_rect.x1 = 0;
            if (union_rect.y1 < 0) union_rect.y1 = 0;
            if (union_rect.x2 > (int32_t)g_fb_w)
                union_rect.x2 = (int32_t)g_fb_w;
            if (union_rect.y2 > (int32_t)g_fb_h)
                union_rect.y2 = (int32_t)g_fb_h;
            scanout_rects = &union_rect;
            scanout_rect_count = 1;
        }
        for (int i = 0; i < scanout_rect_count; i++) {
            const struct damage_rect *r = &scanout_rects[i];
            struct fb_gpu_bo_present cmd;
            static uint32_t last_scanout_resource_id;
            uint64_t t0;
            uint64_t dt;
            int ret;

            if (r->x1 >= r->x2 || r->y1 >= r->y2)
                continue;
            memset(&cmd, 0, sizeof(cmd));
            cmd.x = (uint32_t)r->x1;
            cmd.y = (uint32_t)r->y1;
            cmd.w = (uint32_t)(r->x2 - r->x1);
            cmd.h = (uint32_t)(r->y2 - r->y1);
            cmd.src_pitch = g_fb_pitch;
            cmd.pixels = (uint64_t)r->y1 * g_fb_pitch +
                         (uint64_t)r->x1 * 4;
            cmd.handle = g_fb_bo_handle;
            cmd.flags = FB_GPU_BO_PRESENT_F_VIRGL_SCANOUT;
            if (g_fb_virgl_resource_id != last_scanout_resource_id) {
                g_scanout_trace_rebinds++;
                last_scanout_resource_id = g_fb_virgl_resource_id;
            }
            g_scanout_trace_submits++;
            t0 = wlcomp_scanout_perf_enabled() ? get_time_us() : 0;
            ret = ioctl(g_fb_fd, FB_GPU_BO_PRESENT, &cmd);
            dt = t0 ? get_time_us() - t0 : 0;
            wlcomp_scanout_perf_note(dt, cmd.x, cmd.y, cmd.w, cmd.h, ret, 0);
            if (ret == 0 && cmd.fence > g_fb_bo_last_present_fence) {
                g_fb_bo_last_present_fence = cmd.fence;
            } else if (ret != 0) {
                ok = 0;
                if (scanout_error_logs < 8) {
                    fprintf(stderr,
                            "wlcomp: virgl framebuffer scanout failed errno=%d (%s) handle=%u res=%u rect=%u,%u %ux%u\n",
                            errno, strerror(errno), g_fb_bo_handle,
                            g_fb_virgl_resource_id, cmd.x, cmd.y, cmd.w,
                            cmd.h);
                    scanout_error_logs++;
                }
                break;
            }
        }
    }
    return ok;
}

static int mirror_cpu_damage_to_virgl_target(
    const struct wlcomp_virgl_fb_target *target,
    const struct damage_rect *r)
{
    struct fb_gpu_virgl_transfer transfer;
    uint32_t w;
    uint32_t h;

    if (!target || !r || !g_fb_buf || !target->buf ||
        target->resource_id == 0 || g_fb_pitch == 0 || target->pitch == 0 ||
        r->x1 >= r->x2 || r->y1 >= r->y2)
        return 1;
    w = (uint32_t)(r->x2 - r->x1);
    h = (uint32_t)(r->y2 - r->y1);
    for (uint32_t row = 0; row < h; row++) {
        uint8_t *src = (uint8_t *)g_fb_buf +
            (uint64_t)(r->y1 + (int32_t)row) * g_fb_pitch +
            (uint64_t)r->x1 * 4;
        uint8_t *dst = (uint8_t *)target->buf +
            (uint64_t)(r->y1 + (int32_t)row) *
                target->pitch +
            (uint64_t)r->x1 * 4;

        memcpy(dst, src, (size_t)w * 4);
    }

    memset(&transfer, 0, sizeof(transfer));
    transfer.resource_id = target->resource_id;
    transfer.x = (uint32_t)r->x1;
    transfer.y = (uint32_t)r->y1;
    transfer.w = w;
    transfer.h = h;
    transfer.d = 1;
    transfer.offset = (uint64_t)r->y1 * target->pitch +
                      (uint64_t)r->x1 * 4;
    transfer.stride = target->pitch;
    transfer.layer_stride =
        target->pitch * (uint64_t)g_fb_h;
    if (ioctl(g_fb_fd, FB_GPU_VIRGL_TRANSFER_TO_HOST, &transfer) != 0) {
        static int mirror_error_logs;

        if (mirror_error_logs < 8) {
            fprintf(stderr,
                    "wlcomp: virgl framebuffer inactive-target mirror failed errno=%d (%s) res=%u rect=%u,%u %ux%u\n",
                    errno, strerror(errno),
                    target->resource_id,
                    transfer.x, transfer.y, transfer.w, transfer.h);
            mirror_error_logs++;
        }
        return 0;
    }
    return 1;
}

static int mirror_cpu_damage_to_virgl_inactive_targets(
    const struct damage_rect *r)
{
    int ok = 1;

    if (!g_fb_virgl_back_target_ready)
        return 1;
    if (!mirror_cpu_damage_to_virgl_target(&g_fb_virgl_back_target, r))
        ok = 0;
    if (g_fb_virgl_extra_target_ready &&
        !mirror_cpu_damage_to_virgl_target(&g_fb_virgl_extra_target, r))
        ok = 0;
    return ok;
}

static uint32_t g_fb_last_present_ms;

/*
 * Account one presented frame for the visible-fps stats (the /tmp/wlcomp-fps
 * file the demo reads as displayed_fps).  Shared by present_damage_rects() and
 * the fullscreen-direct unredirection path, which bypasses that function but
 * still presents a client frame to the scanout.
 */
static void framebuffer_account_present_frame(uint32_t now,
                                              uint64_t present_pixels,
                                              int rect_count, int full_frame)
{
    g_damage_stats.frames++;
    if (g_fb_last_present_ms != 0) {
        uint32_t interval = now - g_fb_last_present_ms;

        g_damage_stats.frame_interval_sum_ms += interval;
        if (interval > g_damage_stats.frame_interval_max_ms)
            g_damage_stats.frame_interval_max_ms = interval;
        if (interval > g_damage_interval_max_ms)
            g_damage_interval_max_ms = interval;
        if (interval > 25)
            g_damage_stats.frame_interval_over_25ms++;
        if (interval > 40)
            g_damage_stats.frame_interval_over_40ms++;
    }
    g_fb_last_present_ms = now;
    g_damage_stats.present_rects += (uint64_t)rect_count;
    g_damage_stats.present_pixels += present_pixels;
    if (full_frame)
        g_damage_stats.present_full_frames++;
}

static int present_damage_rects(int fb_w, uint32_t now,
                                const struct damage_rect *scanout_rects,
                                int scanout_rect_count,
                                int skip_virgl_scanout)
{
    int stride = g_fb_pitch ? (int)(g_fb_pitch / 4) : fb_w;
    int rect_count = g_damage_count;
    uint64_t present_pixels = 0;
    int full_frame = 0;
    int ok = 1;

    for (int i = 0; i < g_damage_count; i++) {
        struct damage_rect *r = &g_damage[i];
        uint32_t w = (uint32_t)(r->x2 - r->x1);
        uint32_t h = (uint32_t)(r->y2 - r->y1);

        present_pixels += (uint64_t)w * h;
        if (r->x1 == 0 && r->y1 == 0 &&
            r->x2 == (int32_t)g_fb_w && r->y2 == (int32_t)g_fb_h)
            full_frame = 1;
        if (g_fb_virgl_backed) {
            struct fb_gpu_virgl_transfer transfer;

            memset(&transfer, 0, sizeof(transfer));
            transfer.resource_id = g_fb_virgl_resource_id;
            transfer.x = (uint32_t)r->x1;
            transfer.y = (uint32_t)r->y1;
            transfer.w = w;
            transfer.h = h;
            transfer.d = 1;
            transfer.offset = (uint64_t)r->y1 * g_fb_pitch +
                              (uint64_t)r->x1 * 4;
            transfer.stride = g_fb_pitch;
            transfer.layer_stride = g_fb_pitch * g_fb_h;
            if (ioctl(g_fb_fd, FB_GPU_VIRGL_TRANSFER_TO_HOST,
                      &transfer) != 0) {
                static int upload_error_logs;

                ok = 0;
                if (upload_error_logs < 8) {
                    fprintf(stderr,
                            "wlcomp: virgl framebuffer upload failed errno=%d (%s) res=%u rect=%u,%u %ux%u\n",
                            errno, strerror(errno), g_fb_virgl_resource_id,
                            transfer.x, transfer.y, transfer.w, transfer.h);
                    upload_error_logs++;
                }
            }
            if (!mirror_cpu_damage_to_virgl_inactive_targets(r))
                ok = 0;
        } else if (g_fb_direct_scanout) {
            struct fb_gpu_scanout_flush cmd;

            cmd.x = (uint32_t)r->x1;
            cmd.y = (uint32_t)r->y1;
            cmd.w = w;
            cmd.h = h;
            if (ioctl(g_fb_fd, FB_GPU_SCANOUT_FLUSH, &cmd) != 0)
                ok = 0;
        } else if (g_fb_bo_backed) {
            struct fb_gpu_bo_present cmd;
            int ret;

            memset(&cmd, 0, sizeof(cmd));
            cmd.x = (uint32_t)r->x1;
            cmd.y = (uint32_t)r->y1;
            cmd.w = w;
            cmd.h = h;
            cmd.src_pitch = g_fb_pitch;
            cmd.pixels = (uint64_t)r->y1 * g_fb_pitch + (uint64_t)r->x1 * 4;
            cmd.handle = g_fb_bo_handle;
            cmd.flags = 0;
            ret = ioctl(g_fb_fd, FB_GPU_BO_PRESENT, &cmd);
            if (ret == 0 && cmd.fence > g_fb_bo_last_present_fence) {
                g_fb_bo_last_present_fence = cmd.fence;
            } else if (ret == 0) {
                /* Synchronous BO presents may report an unchanged fence. */
            } else {
                ok = 0;
            }
        } else {
            struct fb_gpu_blit cmd;
            uint64_t pixels =
                (uint64_t)(uintptr_t)(g_fb_buf + r->y1 * stride + r->x1);
            static int blit_error_logs;
            memset(&cmd, 0, sizeof(cmd));
            cmd.x = (uint32_t)r->x1;
            cmd.y = (uint32_t)r->y1;
            cmd.w = w;
            cmd.h = h;
            cmd.src_pitch = g_fb_pitch;
            cmd.pixels = pixels;
            g_fb_blit_present_attempt_count++;
            if (ioctl(g_fb_fd, FB_GPU_BLIT, &cmd) != 0) {
                ok = 0;
                if (blit_error_logs < 8) {
                    fprintf(stderr,
                            "wlcomp: FB_GPU_BLIT failed errno=%d (%s) rect=%u,%u %ux%u pitch=%u pixels=0x%lx\n",
                            errno, strerror(errno), cmd.x, cmd.y, cmd.w,
                            cmd.h, cmd.src_pitch, cmd.pixels);
                    blit_error_logs++;
                }
            } else {
                g_fb_blit_present_success_count++;
            }
        }
    }
    if (g_fb_virgl_backed && ok && !skip_virgl_scanout)
        ok = present_virgl_scanout_rects(scanout_rects, scanout_rect_count);
    framebuffer_account_present_frame(now, present_pixels, rect_count,
                                      full_frame);
    g_damage_count = 0;
    if (g_fb_direct_scanout || g_fb_bo_backed) {
        /*
         * xv6 direct scanout flushes and BO presents are synchronous: the
         * kernel completes the scanout update before returning.  Avoid an
         * extra display-wait ioctl on every animated frame.
         */
        g_fb_display_wait_sequence = 0;
        g_fb_display_stall_start_ms = 0;
    } else if (framebuffer_display_completion_query(0, 0,
                                                    &g_fb_display_wait_sequence,
                                                    &g_fb_display_last_complete) == 0) {
        g_fb_display_stall_start_ms = 0;
    }
    return ok;
}

static void tune_damage_for_present(void)
{
    struct damage_rect u;
    int union_area;
    int rect_area_sum = 0;

    if (g_damage_count <= 1)
        return;

    damage_union_bounds(&u);
    union_area = rect_area(&u);
    for (int i = 0; i < g_damage_count; i++)
        rect_area_sum += rect_area(&g_damage[i]);

    if (union_area <= rect_area_sum * 2) {
        g_damage[0] = u;
        g_damage_count = 1;
        g_damage_stats.present_union_collapses++;
    }
}

static int damage_has_any(void)
{
    return g_damage_count > 0;
}

static void damage_taskbar(void)
{
    if (g_fb_h >= TASKBAR_H)
        damage_rect(0, (int32_t)g_fb_h - TASKBAR_H,
                    (int32_t)g_fb_w, TASKBAR_H);
}
