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

    if (buf->release_pending)
        return 1;

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

static void xv6_d3d12_close_buffer_import(struct wlcomp_buffer *buf);

static void buffer_free(struct wlcomp_buffer *buf)
{
    if (!buf)
        return;

    if (buf->acquire_fence_fd >= 0)
        close(buf->acquire_fence_fd);
    if (buf->is_d3d12_shared)
        xv6_d3d12_close_buffer_import(buf);
    if (buf->is_d3d12_shared && buf->d3d12_resource_fd >= 0)
        close(buf->d3d12_resource_fd);
    if (buf->is_d3d12_shared && buf->d3d12_fence_fd >= 0)
        close(buf->d3d12_fence_fd);
    if (buf->explicit_release) {
        zwp_linux_buffer_release_v1_send_immediate_release(
            buf->explicit_release);
        buf->explicit_release = NULL;
    }

    if (buf->is_gpu_bo) {
        uint32_t plane_count = buf->plane_count ? buf->plane_count : 1;

        for (uint32_t i = 0; i < plane_count && i < 4; i++) {
            uint64_t addr = buf->plane_gpu_addr[i];
            uint64_t size = buf->plane_gpu_size[i];
            uint32_t handle = buf->plane_gpu_handle[i];

            if (i == 0) {
                if (!addr)
                    addr = buf->gpu_addr;
                if (!size)
                    size = buf->gpu_size;
                if (!handle)
                    handle = buf->gpu_handle;
            }
            if (addr && size)
                munmap((void *)addr, (size_t)size);
            if (buf->owns_gpu_handle && handle != 0) {
                struct fb_gpu_bo_destroy destroy = {
                    handle, 0
                };

                (void)ioctl(g_fb_fd, FB_GPU_BO_DESTROY, &destroy);
            }
        }
    }

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

static int display_stall_recover_ms(void)
{
    static int initialized;
    static int timeout_ms = 500;
    const char *env;

    if (initialized)
        return timeout_ms;
    initialized = 1;
    env = getenv("XV6_WLCOMP_STALL_RECOVER_MS");
    if (env && *env)
        timeout_ms = atoi(env);
    timeout_ms = cmdline_int_value("wlcomp_stall_recover_ms", timeout_ms);
    if (timeout_ms < 0)
        timeout_ms = 0;
    if (timeout_ms > 60000)
        timeout_ms = 60000;
    return timeout_ms;
}

static int framebuffer_display_completion_query(uint64_t wait_for,
                                                int wait,
                                                uint64_t *presented,
                                                uint64_t *completed)
{
    struct fb_gpu_display_wait req;
    int ret;

    if (presented)
        *presented = 0;
    if (completed)
        *completed = 0;
    if (g_fb_fd < 0)
        return -1;

    memset(&req, 0, sizeof(req));
    req.flags = wait ? FB_GPU_DISPLAY_WAIT_F_WAIT : 0;
    req.wait_for = wait_for;
    ret = ioctl(g_fb_fd, FB_GPU_DISPLAY_WAIT, &req);
    if (ret == 0 || errno == EAGAIN) {
        if (presented)
            *presented = req.presented;
        if (completed)
            *completed = req.completed;
        g_fb_display_last_complete = req.completed;
        g_damage_stats.display_waits++;
        if (ret != 0)
            g_damage_stats.display_wait_not_ready++;
    }
    return ret;
}

static int framebuffer_present_fence_ready(void)
{
    uint64_t presented = 0;
    uint64_t completed = 0;
    uint32_t now;
    int timeout_ms;

    if (g_fb_display_wait_sequence != 0) {
        if (framebuffer_display_completion_query(g_fb_display_wait_sequence, 1,
                                                 &presented, &completed) == 0)
            return 1;

        now = get_time_ms();
        if (g_fb_display_stall_start_ms == 0)
            g_fb_display_stall_start_ms = now;
        timeout_ms = display_stall_recover_ms();
        if (timeout_ms > 0 &&
            now - g_fb_display_stall_start_ms >= (uint32_t)timeout_ms) {
            fprintf(stderr,
                    "wlcomp: recovered stalled display completion wait_for=%lu completed=%lu\n",
                    (unsigned long)g_fb_display_wait_sequence,
                    (unsigned long)completed);
            g_damage_stats.display_stall_recoveries++;
            g_fb_display_wait_sequence = completed;
            g_fb_display_stall_start_ms = 0;
            return 1;
        }
        return 0;
    }

    if (!g_fb_bo_backed || g_fb_bo_handle == 0 ||
        g_fb_bo_last_present_fence == 0)
        return 1;

    /*
     * FB_GPU_BO_PRESENT completes the CPU copy into the scanout before it
     * returns and the kernel reports the present fence as signaled immediately.
     * Avoid several extra ioctl round trips per frame on the hot compositor
     * path; those show up as visible jitter under WebKit video.
     */
    g_fb_bo_last_signaled_fence = g_fb_bo_last_present_fence;
    return 1;
}

static int export_present_release_fence_fd(void)
{
    struct fb_gpu_fence_export_fd export_fd;

    if (!g_fb_bo_backed || g_fb_bo_handle == 0 ||
        g_fb_bo_last_present_fence == 0)
        return -1;

    memset(&export_fd, 0, sizeof(export_fd));
    export_fd.handle = g_fb_bo_handle;
    export_fd.fence = g_fb_bo_last_present_fence;
    export_fd.fd = -1;
    if (ioctl(g_fb_fd, FB_GPU_FENCE_EXPORT_FD, &export_fd) < 0 ||
        export_fd.fd < 0)
        return -1;
    return export_fd.fd;
}

static void buffer_send_release_events(struct wlcomp_buffer *buf)
{
    static int d3d12_release_block_logs;

    if (!buf)
        return;
    if (buf->is_d3d12_shared && !buf->d3d12_present_complete &&
        !buf->d3d12_failclosed_client_unblocked) {
        if (d3d12_release_block_logs < 8) {
            fprintf(stderr,
                    "wlcomp: blocked D3D12 buffer release pending_gpu_display_present=1 resource=0x%x callbacks_blocked=1 releases_blocked=1 starts=%lu copies=%lu display_completes=%lu\n",
                    buf->d3d12_opened_resource,
                    (unsigned long)g_d3d12_present_start_count,
                    (unsigned long)g_d3d12_present_copy_complete_count,
                    (unsigned long)g_d3d12_present_display_complete_count);
            d3d12_release_block_logs++;
        }
        return;
    }
    if (buf->resource)
        wl_buffer_send_release(buf->resource);
    if (buf->explicit_release) {
        if (buf->is_d3d12_shared) {
            zwp_linux_buffer_release_v1_send_immediate_release(
                buf->explicit_release);
            if (buf->d3d12_failclosed_client_unblocked &&
                !buf->d3d12_present_complete) {
                fprintf(stderr,
                        "wlcomp: d3d12 explicit-sync immediate release after fail-closed unblock no_native_present_credit=1 seq=%lu\n",
                        (unsigned long)buf->d3d12_present_sequence);
            } else {
                fprintf(stderr,
                        "wlcomp: d3d12 explicit-sync immediate release after completed present seq=%lu fence=%lu\n",
                        (unsigned long)buf->d3d12_present_sequence,
                        (unsigned long)buf->d3d12_present_fence_value);
            }
        } else {
            int fence_fd = export_present_release_fence_fd();

            if (fence_fd >= 0) {
                zwp_linux_buffer_release_v1_send_fenced_release(
                    buf->explicit_release, fence_fd);
                fprintf(stderr,
                        "wlcomp: explicit-sync fenced release fence_fd=%d fence=%lu\n",
                        fence_fd, (unsigned long)g_fb_bo_last_present_fence);
            } else {
                zwp_linux_buffer_release_v1_send_immediate_release(
                    buf->explicit_release);
                fprintf(stderr, "wlcomp: explicit-sync immediate release\n");
            }
        }
        buf->explicit_release = NULL;
    }
    if (buf->is_d3d12_shared)
        xv6_d3d12_observe_buffer_release(buf);
}

static void buffer_send_immediate_release_events(struct wlcomp_buffer *buf)
{
    if (!buf)
        return;
    if (buf->resource)
        wl_buffer_send_release(buf->resource);
    if (buf->explicit_release) {
        zwp_linux_buffer_release_v1_send_immediate_release(
            buf->explicit_release);
        buf->explicit_release = NULL;
    }
}

static void queue_buffer_release(struct wlcomp_buffer *buf)
{
    static int d3d12_release_queue_full_logs;

    if (!buf || buf->release_pending)
        return;

    buf->release_pending = 1;
    if (g_release_queue_count < MAX_RELEASE_QUEUE) {
        g_release_queue[g_release_queue_count++] = buf;
        return;
    }
    if (buf->is_d3d12_shared && !buf->d3d12_present_complete &&
        !buf->d3d12_failclosed_client_unblocked) {
        if (d3d12_release_queue_full_logs < 8) {
            fprintf(stderr,
                    "wlcomp: D3D12 release queue full; keeping release blocked pending_gpu_display_present=1 resource=0x%x starts=%lu copies=%lu display_completes=%lu\n",
                    buf->d3d12_opened_resource,
                    (unsigned long)g_d3d12_present_start_count,
                    (unsigned long)g_d3d12_present_copy_complete_count,
                    (unsigned long)g_d3d12_present_display_complete_count);
            d3d12_release_queue_full_logs++;
        }
        return;
    }

    buffer_send_release_events(buf);
    buf->release_pending = 0;
    buffer_maybe_free(buf);
}

static void flush_buffer_releases_after_present(void)
{
    int out = 0;

    if (!framebuffer_present_fence_ready()) {
        int has_failclosed_d3d12_release = 0;

        for (int i = 0; i < g_release_queue_count; i++) {
            struct wlcomp_buffer *buf = g_release_queue[i];

            if (buf && buf->is_d3d12_shared &&
                buf->d3d12_failclosed_client_unblocked) {
                has_failclosed_d3d12_release = 1;
                break;
            }
        }
        if (!has_failclosed_d3d12_release)
            return;
    }

    for (int i = 0; i < g_release_queue_count; i++) {
        struct wlcomp_buffer *buf = g_release_queue[i];

        if (!buf)
            continue;
        if (buf->is_d3d12_shared && !buf->d3d12_present_complete &&
            !buf->d3d12_failclosed_client_unblocked) {
            g_release_queue[out++] = buf;
            continue;
        }
        buffer_send_release_events(buf);
        buf->release_pending = 0;
        buffer_maybe_free(buf);
    }
    g_release_queue_count = out;
}

static void surface_release_committed_buffer(struct wlcomp_surface *surf)
{
    if (!surf || !surf->committed_buf || surf->buffer_released)
        return;

    if (surf->committed_buf->acquire_fence_fd >= 0)
        return;

    queue_buffer_release(surf->committed_buf);
    surf->buffer_released = 1;
}

static int buffer_acquire_ready(struct wlcomp_buffer *buf)
{
    struct pollfd pfd;
    int ret;
    uint32_t now;
    int timeout_ms;

    if (!buf)
        return 1;

    if (buf->is_d3d12_shared && buf->d3d12_fence_value_valid) {
        volatile uint64_t *fence;
        uint64_t current;

        if (buf->d3d12_fence_cpu_va == 0)
            return 0;
        fence = (volatile uint64_t *)buf->d3d12_fence_cpu_va;
        current = *fence;
        if (current >= buf->d3d12_fence_value) {
            buf->d3d12_fence_wait_start_ms = 0;
            return 1;
        }
        now = get_time_ms();
        if (buf->d3d12_fence_wait_start_ms == 0)
            buf->d3d12_fence_wait_start_ms = now;
        timeout_ms = display_stall_recover_ms();
        if (timeout_ms > 0 &&
            now - buf->d3d12_fence_wait_start_ms >= (uint32_t)timeout_ms) {
            fprintf(stderr,
                    "wlcomp: d3d12 fence not ready target=%lu current=%lu wait_ms=%u\n",
                    (unsigned long)buf->d3d12_fence_value,
                    (unsigned long)current,
                    now - buf->d3d12_fence_wait_start_ms);
            buf->d3d12_fence_wait_start_ms = now;
        }
        return 0;
    }

    if (buf->acquire_fence_fd < 0)
        return 1;

    pfd.fd = buf->acquire_fence_fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    ret = poll(&pfd, 1, 0);
    if (ret > 0) {
        if (pfd.revents & (POLLIN | POLLRDNORM | POLLERR | POLLHUP)) {
            close(buf->acquire_fence_fd);
            buf->acquire_fence_fd = -1;
            buf->acquire_wait_start_ms = 0;
            return 1;
        }
        return 0;
    }
    if (ret < 0 && errno != EINTR && errno != EAGAIN) {
        close(buf->acquire_fence_fd);
        buf->acquire_fence_fd = -1;
        buf->acquire_wait_start_ms = 0;
        return 1;
    }

    now = get_time_ms();
    if (buf->acquire_wait_start_ms == 0)
        buf->acquire_wait_start_ms = now;
    timeout_ms = display_stall_recover_ms();
    if (timeout_ms > 0 &&
        now - buf->acquire_wait_start_ms >= (uint32_t)timeout_ms) {
        fprintf(stderr,
                "wlcomp: recovered stalled acquire fence fd=%d wait_ms=%u\n",
                buf->acquire_fence_fd, now - buf->acquire_wait_start_ms);
        close(buf->acquire_fence_fd);
        buf->acquire_fence_fd = -1;
        buf->acquire_wait_start_ms = 0;
        g_damage_stats.acquire_stall_recoveries++;
        return 1;
    }

    return 0;
}

static void *buffer_data(struct wlcomp_buffer *buf)
{
    static int d3d12_cpu_map_logs;

    if (buf && !buffer_acquire_ready(buf))
        return NULL;
    if (buf && buf->is_d3d12_shared) {
        g_d3d12_cpu_map_reject_count++;
        if (d3d12_cpu_map_logs < 8) {
            fprintf(stderr,
                    "wlcomp: blocked D3D12 CPU map/readback request d3d12_cpu_mapping_rejected=1 resource=0x%x present_complete=%d cpu_map_rejects=%lu\n",
                    buf->d3d12_opened_resource,
                    buf->d3d12_present_complete,
                    (unsigned long)g_d3d12_cpu_map_reject_count);
            d3d12_cpu_map_logs++;
        }
        return NULL;
    }
    if (buf && buf->is_gpu_bo)
        return (void *)buf->gpu_addr;
    if (!buf || !buf->pool || !buf->pool->data)
        return NULL;

    return (char *)buf->pool->data + buf->offset;
}

static void *buffer_plane_data(struct wlcomp_buffer *buf, uint32_t plane)
{
    static int d3d12_cpu_plane_logs;

    if (!buf || plane >= 4)
        return NULL;
    if (buf->is_d3d12_shared) {
        g_d3d12_cpu_plane_reject_count++;
        if (d3d12_cpu_plane_logs < 8) {
            fprintf(stderr,
                    "wlcomp: blocked D3D12 plane CPU map/readback request d3d12_cpu_mapping_rejected=1 resource=0x%x plane=%u present_complete=%d cpu_plane_rejects=%lu\n",
                    buf->d3d12_opened_resource, plane,
                    buf->d3d12_present_complete,
                    (unsigned long)g_d3d12_cpu_plane_reject_count);
            d3d12_cpu_plane_logs++;
        }
        return NULL;
    }
    if (buf->is_gpu_bo) {
        if (buf->plane_gpu_addr[plane]) {
            uint64_t offset = 0;

            if (buf->plane_offset[plane] > 0)
                offset = (uint64_t)buf->plane_offset[plane];
            if (offset >= buf->plane_gpu_size[plane])
                return NULL;
            return (void *)(uintptr_t)(buf->plane_gpu_addr[plane] + offset);
        }
        return plane == 0 ? (void *)buf->gpu_addr : NULL;
    }
    if (!buf->pool || !buf->pool->data || plane != 0)
        return NULL;
    return (char *)buf->pool->data + buf->offset;
}

static int gpu_compose_enabled(void)
{
    static int initialized;
    static int enabled = 1;
    const char *env;

    if (initialized)
        return enabled;
    initialized = 1;
    env = getenv("XV6_WLCOMP_GPU_COMPOSE");
    if (env && *env)
        enabled = atoi(env) != 0;
    if (cmdline_flag_enabled("wlcomp_gpu_compose"))
        enabled = 1;
    return enabled;
}

static int gpu_compose_present_direct_scanout(struct wlcomp_buffer *buf,
                                              uint32_t x, uint32_t y,
                                              uint32_t w, uint32_t h,
                                              uint64_t src_off,
                                              uint32_t src_pitch)
{
    uint8_t *src;
    uint8_t *dst;
    uint32_t bytes;
    static uint64_t direct_total_us;
    static uint64_t direct_copy_us;
    static uint64_t direct_frames;
    static int direct_log_count;
    int perf_log = cmdline_flag_enabled("wlcomp_gpu_perf");
    uint64_t t0 = 0;
    uint64_t t_copy = 0;
    const char *enable = getenv("XV6_WLCOMP_GPU_DIRECT_SCANOUT");

    if ((!enable || strcmp(enable, "1") != 0) &&
        !cmdline_flag_enabled("wlcomp_gpu_direct_scanout"))
        return 0;
    if (
        !g_fb_direct_scanout || !g_fb_buf || g_fb_pitch == 0 ||
        !buf || !buf->is_gpu_bo || buf->plane_gpu_addr[0] == 0)
        return 0;
    if ((uint64_t)y * g_fb_pitch + (uint64_t)x * 4 >= g_fb_bo_size)
        return 0;
    if (src_off >= buf->plane_gpu_size[0])
        return 0;

    bytes = w * 4;
    src = (uint8_t *)(uintptr_t)buf->plane_gpu_addr[0] + src_off;
    dst = (uint8_t *)g_fb_buf + (uint64_t)y * g_fb_pitch + (uint64_t)x * 4;
    if (perf_log)
        t0 = get_time_us();
    if (src_pitch == bytes && g_fb_pitch == bytes) {
        memcpy(dst, src, (size_t)bytes * h);
    } else {
        for (uint32_t row = 0; row < h; row++) {
            memcpy(dst, src, bytes);
            src += src_pitch;
            dst += g_fb_pitch;
        }
    }
    if (perf_log)
        t_copy = get_time_us();

    if (perf_log) {
        direct_frames++;
        direct_copy_us += t_copy - t0;
        direct_total_us += t_copy - t0;
    }
    if (perf_log && direct_frames >= 20) {
        fprintf(stderr,
                "wlcomp: gpu-compose-direct-copy avg_us=%lu copy=%lu frames=%lu rect=%u,%u %ux%u\n",
                (unsigned long)(direct_total_us / direct_frames),
                (unsigned long)(direct_copy_us / direct_frames),
                (unsigned long)direct_frames, x, y, w, h);
        fflush(stderr);
        direct_frames = 0;
        direct_copy_us = 0;
        direct_total_us = 0;
    }
    if (direct_log_count < 8) {
        uint32_t handle = buf->plane_gpu_handle[0] ? buf->plane_gpu_handle[0] :
                                                     buf->gpu_handle;

        fprintf(stderr,
                "wlcomp: gpu-compose presented path=direct-scanout handle=%u rect=%u,%u %ux%u src_off=%lu fence=0\n",
                handle, x, y, w, h, (unsigned long)src_off);
        direct_log_count++;
    }
    return 1;
}

static int gpu_compose_present_buffer(struct wlcomp_buffer *buf,
                                      int32_t draw_x, int32_t draw_y,
                                      int32_t row0, int32_t row1,
                                      int32_t col0, int32_t col1)
{
    struct fb_gpu_bo_present cmd;
    uint32_t handle;
    uint64_t offset;
    uint32_t w;
    uint32_t h;
    static int log_count;
    static int fail_log_count;
    static int skip_log_count;
    static uint64_t present_total_us;
    static uint64_t present_frames;
    int perf_log = cmdline_flag_enabled("wlcomp_gpu_perf");
    uint64_t present_t0;
    uint64_t present_dt;

    if (!gpu_compose_enabled() || !g_fb_direct_scanout || g_fb_fd < 0) {
        if (skip_log_count < 8) {
            fprintf(stderr,
                    "wlcomp: gpu-compose skip enabled=%d direct=%d fb_fd=%d\n",
                    gpu_compose_enabled(), g_fb_direct_scanout, g_fb_fd);
            skip_log_count++;
        }
        return 0;
    }
    if (!buf || !buf->is_gpu_bo ||
        (buf->format != WL_SHM_FORMAT_ARGB8888 &&
         buf->format != WL_SHM_FORMAT_XRGB8888 &&
         buf->format != DRM_FORMAT_ARGB8888 &&
         buf->format != DRM_FORMAT_XRGB8888) ||
        buf->y_inverted || row0 >= row1 || col0 >= col1) {
        if (skip_log_count < 8) {
            fprintf(stderr,
                    "wlcomp: gpu-compose skip buf=%p gpu=%d fmt=0x%x yinv=%d rows=%d..%d cols=%d..%d\n",
                    (void *)buf, buf ? buf->is_gpu_bo : 0,
                    buf ? buf->format : 0, buf ? buf->y_inverted : 0,
                    row0, row1, col0, col1);
            skip_log_count++;
        }
        return 0;
    }
    handle = buf->plane_gpu_handle[0] ? buf->plane_gpu_handle[0] :
                                        buf->gpu_handle;
    if (handle == 0 || buf->plane_stride[0] <= 0) {
        if (skip_log_count < 8) {
            fprintf(stderr,
                    "wlcomp: gpu-compose skip handle=%u stride=%d\n",
                    handle, buf->plane_stride[0]);
            skip_log_count++;
        }
        return 0;
    }

    w = (uint32_t)(col1 - col0);
    h = (uint32_t)(row1 - row0);
    offset = (uint64_t)row0 * (uint32_t)buf->plane_stride[0] +
             (uint64_t)col0 * 4;

    memset(&cmd, 0, sizeof(cmd));
    cmd.x = (uint32_t)(draw_x + col0);
    cmd.y = (uint32_t)(draw_y + row0);
    cmd.w = w;
    cmd.h = h;
    cmd.src_pitch = (uint32_t)buf->plane_stride[0];
    cmd.pixels = offset;
    cmd.handle = handle;
    if (gpu_compose_present_direct_scanout(buf, cmd.x, cmd.y, w, h,
                                           offset, cmd.src_pitch)) {
        g_damage_stats.gpu_compose_presents++;
        g_damage_stats.gpu_compose_pixels += (uint64_t)w * h;
        return 1;
    }

    present_t0 = perf_log ? get_time_us() : 0;
    if (ioctl(g_fb_fd, FB_GPU_BO_PRESENT, &cmd) != 0) {
        if (fail_log_count < 8) {
            fprintf(stderr,
                    "wlcomp: gpu-compose present failed handle=%u rect=%u,%u %ux%u off=%lu errno=%d (%s)\n",
                    handle, cmd.x, cmd.y, w, h, (unsigned long)offset,
                    errno, strerror(errno));
            fail_log_count++;
        }
        return 0;
    }
    present_dt = perf_log ? get_time_us() - present_t0 : 0;
    if (perf_log) {
        present_total_us += present_dt;
        present_frames++;
    }
    if (perf_log && present_frames >= 20) {
        fprintf(stderr,
                "wlcomp: gpu-compose-present avg_us=%lu frames=%lu rect=%u,%u %ux%u\n",
                (unsigned long)(present_total_us / present_frames),
                (unsigned long)present_frames, cmd.x, cmd.y, w, h);
        fflush(stderr);
        present_total_us = 0;
        present_frames = 0;
    }

    g_damage_stats.gpu_compose_presents++;
    g_damage_stats.gpu_compose_pixels += (uint64_t)w * h;
    if (log_count < 8) {
        fprintf(stderr,
                "wlcomp: gpu-compose presented handle=%u rect=%u,%u %ux%u src_off=%lu fence=%lu\n",
                handle, cmd.x, cmd.y, w, h, (unsigned long)offset,
                (unsigned long)cmd.fence);
        log_count++;
    }
    return 1;
}

static int gpu_compose_can_present_surface(struct wlcomp_surface *surf,
                                           struct wlcomp_buffer *buf)
{
    int32_t gx;
    int32_t gy;
    int32_t gw;
    int32_t gh;

    if (!surf || !buf || !surf->has_window_geometry)
        return 1;

    surface_window_geometry(surf, &gx, &gy, &gw, &gh);
    (void)gx;
    (void)gy;
    if (gw <= 0 || gh <= 0)
        return 1;

    /*
     * FB_GPU_BO_PRESENT and the direct scanout copy consume source pixel
     * coordinates one-to-one.  Window geometry scaling must stay on the CPU
     * paint path until the GPU present ABI grows explicit source/destination
     * rectangles.
     */
    return buf->width == gw && buf->height == gh;
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
    pid_t client_pid = 0;
    uid_t client_uid = 0;
    gid_t client_gid = 0;

    struct wlcomp_buffer *buf = calloc(1, sizeof(*buf));
    if (!buf) {
        wl_resource_post_no_memory(resource);
        return;
    }

    buf->pool   = pool;
    buf->acquire_fence_fd = -1;
    buf->offset = offset;
    buf->width  = width;
    buf->height = height;
    buf->stride = stride;
    buf->plane_count = 1;
    buf->plane_stride[0] = stride;
    buf->plane_offset[0] = 0;
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
    wl_client_get_credentials(client, &client_pid, &client_uid, &client_gid);
    if (xv6_wlcomp_trace_enabled())
        fprintf(stderr,
                "wlcomp: shm buffer pid=%d size=%dx%d stride=%d fmt=0x%x\n",
                client_pid, width, height, stride, format);
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
