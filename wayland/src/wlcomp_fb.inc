/* Framebuffer state */
static int      g_fb_fd   = -1;
static uint32_t g_fb_w, g_fb_h;
static uint32_t g_fb_pitch;
static uint32_t *g_fb_buf;  /* compositing buffer */
static uint64_t g_fb_bo_size;
static uint32_t g_fb_bo_handle;
static int      g_fb_bo_backed;
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

#define MAX_RELEASE_QUEUE 64
static struct wlcomp_buffer *g_release_queue[MAX_RELEASE_QUEUE];
static int g_release_queue_count;

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
        if (g_fb_bo_handle) {
            struct fb_gpu_bo_destroy destroy;

            memset(&destroy, 0, sizeof(destroy));
            destroy.handle = g_fb_bo_handle;
            ioctl(g_fb_fd, FB_GPU_BO_DESTROY, &destroy);
        }
        munmap(g_fb_buf, g_fb_bo_size);
    } else {
        free(g_fb_buf);
    }
    g_fb_buf = NULL;
    g_fb_bo_size = 0;
    g_fb_bo_handle = 0;
    g_fb_bo_backed = 0;
    g_fb_direct_scanout = 0;
}

static int alloc_framebuffer_backing(void)
{
    struct fb_gpu_bo_create bo;
    struct fb_gpu_scanout_map scanout;
    size_t bytes = (size_t)g_fb_w * g_fb_h * 4;
    const char *use_direct = getenv("XV6_WLCOMP_FB_DIRECT");
    const char *use_bo = getenv("XV6_WLCOMP_FB_BO");
    const char *use_gpu_compose = getenv("XV6_WLCOMP_GPU_COMPOSE");
    int gpu_compose_requested =
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

static int present_damage_rects(int fb_w, uint32_t now)
{
    static uint32_t last_present_ms;
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
        if (g_fb_direct_scanout) {
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
    g_damage_stats.frames++;
    if (last_present_ms != 0) {
        uint32_t interval = now - last_present_ms;

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
    last_present_ms = now;
    g_damage_stats.present_rects += (uint64_t)rect_count;
    g_damage_stats.present_pixels += present_pixels;
    if (full_frame)
        g_damage_stats.present_full_frames++;
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
