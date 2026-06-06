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
        if (surf->committed_buf == buf || surf->pending_buf == buf ||
            surf->deferred_buf == buf || surf->staged_buf == buf)
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
static void xv6_d3d12_unblock_failclosed_client(
    struct wlcomp_buffer *buf, const char *reason);

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
    wlcomp_gl_compose_destroy_buffer(buf);

    if (buf->is_gpu_bo) {
        uint32_t plane_count = buf->plane_count ? buf->plane_count : 1;

        for (uint32_t i = 0; i < plane_count && i < 4; i++) {
            uint64_t addr = buf->plane_gpu_addr[i];
            uint64_t size = buf->plane_gpu_size[i];
            uint32_t handle = buf->plane_gpu_handle[i];

            if (buf->plane_dmabuf_fd[i] >= 0) {
                close(buf->plane_dmabuf_fd[i]);
                buf->plane_dmabuf_fd[i] = -1;
            }
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

    if (wlcomp_async_scanout_present_pending())
        return 0;

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
    g_buffer_release_trace_total++;
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

static void queue_buffer_release(struct wlcomp_buffer *buf)
{
    static int d3d12_release_queue_full_logs;

    if (!buf || buf->release_pending)
        return;

    buf->release_pending = 1;
    if (g_release_queue_count < MAX_RELEASE_QUEUE) {
        g_release_queue[g_release_queue_count++] = buf;
        g_buffer_release_trace_queued++;
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
        if (!wlcomp_gl_compose_buffer_release_ready(buf)) {
            g_release_queue[out++] = buf;
            continue;
        }
        buffer_send_release_events(buf);
        g_buffer_release_trace_flushed++;
        buf->release_pending = 0;
        buffer_maybe_free(buf);
    }
    g_release_queue_count = out;
}

static int flush_buffer_releases_after_compose_latch(void)
{
    int out = 0;
    int flushed = 0;

    for (int i = 0; i < g_release_queue_count; i++) {
        struct wlcomp_buffer *buf = g_release_queue[i];

        if (!buf)
            continue;
        if (buf->is_d3d12_shared) {
            g_release_queue[out++] = buf;
            continue;
        }
        /*
         * This early path is only for GPU-composed dma-bufs whose pixels were
         * captured by the compositor GL draw.  CPU/shm releases still use the
         * present path because their pixels can be re-read for damage repair.
         */
        if (!buf->is_gpu_bo || !buf->gl_release_sync) {
            g_release_queue[out++] = buf;
            continue;
        }
        if (!wlcomp_gl_compose_buffer_release_ready(buf)) {
            g_release_queue[out++] = buf;
            continue;
        }
        buffer_send_release_events(buf);
        g_buffer_release_trace_flushed++;
        g_buffer_release_trace_latch_flushed++;
        buf->release_pending = 0;
        buffer_maybe_free(buf);
        flushed++;
    }
    g_release_queue_count = out;
    return flushed;
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

static int virgl_acquire_wait_enabled(void)
{
    static int initialized;
    static int enabled;
    const char *env;

    if (initialized)
        return enabled;
    initialized = 1;
    enabled = cmdline_int_value("wlcomp_virgl_acquire_wait", 0) != 0;
    env = getenv("XV6_WLCOMP_VIRGL_ACQUIRE_WAIT");
    if (env && *env)
        enabled = strcmp(env, "0") != 0 &&
                  strcmp(env, "no") != 0 &&
                  strcmp(env, "false") != 0 &&
                  strcmp(env, "off") != 0;
    if (enabled)
        fprintf(stderr, "wlcomp: blocking virgl acquire waits enabled\n");
    return enabled;
}

static void refresh_virgl_implicit_fence(struct wlcomp_buffer *buf)
{
    struct fb_gpu_bo_info info;
    uint32_t handle;
    static int refresh_logs;

    if (!g_fb_virgl_backed || !buf || !buf->is_gpu_bo)
        return;
    handle = buf->plane_gpu_handle[0] ? buf->plane_gpu_handle[0] :
                                        buf->gpu_handle;
    if (handle == 0)
        return;

    memset(&info, 0, sizeof(info));
    info.handle = handle;
    if (ioctl(g_fb_fd, FB_GPU_BO_INFO, &info) != 0)
        return;
    if (info.implicit_fence > buf->implicit_fence) {
        buf->implicit_fence = info.implicit_fence;
        if (refresh_logs < 16) {
            fprintf(stderr,
                    "wlcomp: refreshed virgl implicit fence target=%lu res=%u handle=%u\n",
                    (unsigned long)buf->implicit_fence,
                    info.virtio_resource_id, handle);
            refresh_logs++;
        }
    }
    if (info.explicit_fence > buf->explicit_fence)
        buf->explicit_fence = info.explicit_fence;
    if (info.virtio_resource_id != 0)
        buf->plane_virtio_resource_id[0] = info.virtio_resource_id;
}

static int buffer_acquire_ready(struct wlcomp_buffer *buf)
{
    struct fb_gpu_virgl_fence virgl_fence;
    struct pollfd pfd;
    int ret;
    uint32_t now;
    int timeout_ms;

    if (!buf)
        return 1;

    if (buf->is_d3d12_shared && buf->d3d12_failclosed_client_unblocked)
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
                    "wlcomp: d3d12 fence stalled failclosed_unblock=1 target=%lu current=%lu wait_ms=%u import=%s\n",
                    (unsigned long)buf->d3d12_fence_value,
                    (unsigned long)current,
                    now - buf->d3d12_fence_wait_start_ms,
                    buf->d3d12_fence_imported_from_syncfile ?
                    "dxg-syncfile" : "nt-handle");
            xv6_d3d12_unblock_failclosed_client(
                buf, "acquire-fence-stalled");
            buf->d3d12_fence_wait_start_ms = now;
            if (buf->d3d12_failclosed_client_unblocked)
                return 1;
        }
        return 0;
    }

    refresh_virgl_implicit_fence(buf);
    if (g_fb_virgl_backed && buf->is_gpu_bo && buf->implicit_fence != 0 &&
        buf->virgl_acquire_waited_fence < buf->implicit_fence) {
        static int virgl_wait_logs;
        int wait = virgl_acquire_wait_enabled();

        memset(&virgl_fence, 0, sizeof(virgl_fence));
        virgl_fence.flags = wait ? FB_GPU_VIRGL_FENCE_WAIT : 0;
        virgl_fence.wait_for = buf->implicit_fence;
        ret = ioctl(g_fb_fd, FB_GPU_VIRGL_FENCE, &virgl_fence);
        if (ret == 0 && virgl_fence.signaled >= buf->implicit_fence) {
            if (virgl_wait_logs < 16) {
                fprintf(stderr,
                        "wlcomp: virgl acquire fence ready target=%lu signaled=%lu res=%u wait=%d\n",
                        (unsigned long)buf->implicit_fence,
                        (unsigned long)virgl_fence.signaled,
                        buf->plane_virtio_resource_id[0], wait);
                virgl_wait_logs++;
            }
            buf->virgl_acquire_waited_fence = buf->implicit_fence;
        } else if (ret == 0) {
            if (virgl_wait_logs < 16) {
                fprintf(stderr,
                        "wlcomp: virgl acquire fence pending target=%lu signaled=%lu res=%u wait=%d\n",
                        (unsigned long)buf->implicit_fence,
                        (unsigned long)virgl_fence.signaled,
                        buf->plane_virtio_resource_id[0], wait);
                virgl_wait_logs++;
            }
            return 0;
        } else if (virgl_wait_logs < 16) {
            fprintf(stderr,
                    "wlcomp: virgl acquire fence query failed target=%lu errno=%d res=%u wait=%d\n",
                    (unsigned long)buf->implicit_fence, errno,
                    buf->plane_virtio_resource_id[0], wait);
            virgl_wait_logs++;
        }
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

static int buffer_ready_for_gl_compose_latch(struct wlcomp_buffer *buf)
{
    if (!buf)
        return 1;
    if (g_fb_virgl_backed && buf->is_gpu_bo && !buf->is_d3d12_shared &&
        buf->acquire_fence_fd < 0 && wlcomp_gl_compose_enabled())
        return 1;
    return buffer_acquire_ready(buf);
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

static int hw_cursor_upload_default_arrow(void);

static int hw_cursor_client_images_enabled(void)
{
    return cmdline_int_value("wlcomp_hw_cursor_client_images", 0) != 0;
}

static int hw_cursor_surface_is_webkit_client(struct wlcomp_surface *surf)
{
    struct wlcomp_surface *other;

    if (!surf || surf->client_pid <= 0)
        return 0;
    wl_list_for_each(other, &g_surfaces, link) {
        if (other->client_pid == surf->client_pid &&
            surface_is_webkit_window(other))
            return 1;
    }
    return 0;
}

static int hw_cursor_upload_surface(struct wlcomp_surface *surf)
{
    static int upload_logs;
    struct wlcomp_buffer *buf;
    uint32_t image[FB_GPU_CURSOR_MAX_DIM * FB_GPU_CURSOR_MAX_DIM];
    struct fb_gpu_cursor_image req;
    struct fb_gpu_cursor_move mv;
    uint32_t *src;
    int32_t hot_x;
    int32_t hot_y;
    int src_stride_px;
    int transparent_pixels = 0;
    int translucent_pixels = 0;
    int opaque_pixels = 0;
    int corner_alpha_sum = 0;
    int alpha_min_x = FB_GPU_CURSOR_MAX_DIM;
    int alpha_min_y = FB_GPU_CURSOR_MAX_DIM;
    int alpha_max_x = -1;
    int alpha_max_y = -1;

    if (!g_hw_cursor || !surf || surf != g_cursor_surface ||
        !surf->committed_buf || g_fb_fd < 0)
        return 0;
    if (!hw_cursor_client_images_enabled()) {
        if (upload_logs < 8) {
            fprintf(stderr,
                    "wlcomp: hardware cursor client image ignored reason=client-images-disabled\n");
            upload_logs++;
        }
        (void)hw_cursor_upload_default_arrow();
        return 0;
    }
    buf = surf->committed_buf;
    if (buf->width <= 0 || buf->height <= 0 ||
        buf->width > FB_GPU_CURSOR_MAX_DIM ||
        buf->height > FB_GPU_CURSOR_MAX_DIM) {
        if (upload_logs < 8) {
            fprintf(stderr,
                    "wlcomp: hardware cursor client image ignored size=%dx%d reason=bad-size\n",
                    buf->width, buf->height);
            upload_logs++;
        }
        (void)hw_cursor_upload_default_arrow();
        return 0;
    }
    if (hw_cursor_surface_is_webkit_client(surf) &&
        buf->width == 24 && buf->height == 24) {
        if (upload_logs < 8) {
            fprintf(stderr,
                    "wlcomp: hardware cursor client image ignored size=%dx%d hotspot=%d,%d reason=webkit-gtk-24x24-unsafe\n",
                    buf->width, buf->height, g_cursor_hotspot_x,
                    g_cursor_hotspot_y);
            upload_logs++;
        }
        (void)hw_cursor_upload_default_arrow();
        return 0;
    }
    if (buf->format != WL_SHM_FORMAT_ARGB8888 &&
        buf->format != DRM_FORMAT_ARGB8888) {
        if (upload_logs < 8) {
            fprintf(stderr,
                    "wlcomp: hardware cursor client image ignored size=%dx%d fmt=0x%x reason=no-alpha\n",
                    buf->width, buf->height, buf->format);
            upload_logs++;
        }
        (void)hw_cursor_upload_default_arrow();
        return 0;
    }

    src = (uint32_t *)buffer_data(buf);
    if (!src) {
        if (upload_logs < 8) {
            fprintf(stderr,
                    "wlcomp: hardware cursor client image ignored size=%dx%d fmt=0x%x reason=no-readable-buffer\n",
                    buf->width, buf->height, buf->format);
            upload_logs++;
        }
        (void)hw_cursor_upload_default_arrow();
        return 0;
    }
    src_stride_px = buf->stride > 0 ? buf->stride / 4 : buf->width;
    if (src_stride_px < buf->width) {
        if (upload_logs < 8) {
            fprintf(stderr,
                    "wlcomp: hardware cursor client image ignored size=%dx%d stride=%d reason=bad-stride\n",
                    buf->width, buf->height, buf->stride);
            upload_logs++;
        }
        (void)hw_cursor_upload_default_arrow();
        return 0;
    }

    memset(image, 0, sizeof(image));
    for (int y = 0; y < buf->height; y++) {
        int src_y = buf->y_inverted ? (buf->height - 1 - y) : y;

        for (int x = 0; x < buf->width; x++) {
            uint32_t px = src[src_y * src_stride_px + x];
            uint32_t alpha = px >> 24;

            if (alpha == 0)
                transparent_pixels++;
            else if (alpha == 0xff)
                opaque_pixels++;
            else
                translucent_pixels++;
            if (alpha != 0) {
                if (x < alpha_min_x) alpha_min_x = x;
                if (y < alpha_min_y) alpha_min_y = y;
                if (x > alpha_max_x) alpha_max_x = x;
                if (y > alpha_max_y) alpha_max_y = y;
            }
            if ((x == 0 || x == buf->width - 1) &&
                (y == 0 || y == buf->height - 1))
                corner_alpha_sum += (int)alpha;
            image[y * buf->width + x] = px;
        }
    }
    /*
     * Wayland cursor buffers are expected to carry an alpha mask.  WebKit/GTK
     * can transiently provide a 24x24 all-opaque black-ish cursor buffer; if
     * we upload that to the hardware plane, QEMU shows it as a large black
     * square.  Reject rectangular-looking client cursors and keep the
     * known-good default arrow instead of poisoning the cursor resource.
     */
    {
        int total_pixels = buf->width * buf->height;
        int visible_pixels = opaque_pixels + translucent_pixels;
        int alpha_w = alpha_max_x >= alpha_min_x ?
            alpha_max_x - alpha_min_x + 1 : 0;
        int alpha_h = alpha_max_y >= alpha_min_y ?
            alpha_max_y - alpha_min_y + 1 : 0;
        int alpha_area = alpha_w * alpha_h;
        int dense_rect = alpha_area > total_pixels / 3 &&
            visible_pixels * 100 >= alpha_area * 45;

        if ((transparent_pixels == 0 && translucent_pixels == 0 &&
             opaque_pixels == total_pixels) ||
            corner_alpha_sum != 0 ||
            transparent_pixels < total_pixels / 8 ||
            dense_rect ||
            opaque_pixels > (total_pixels * 2) / 3) {
        if (upload_logs < 8) {
            fprintf(stderr,
                    "wlcomp: hardware cursor client image ignored size=%dx%d hotspot=%d,%d alpha transparent=%d translucent=%d opaque=%d corners=%d bbox=%dx%d visible=%d reason=rectangular-alpha-mask\n",
                    buf->width, buf->height, g_cursor_hotspot_x,
                    g_cursor_hotspot_y, transparent_pixels,
                    translucent_pixels, opaque_pixels, corner_alpha_sum,
                    alpha_w, alpha_h, visible_pixels);
            upload_logs++;
        }
        (void)hw_cursor_upload_default_arrow();
        return 0;
        }
    }

    hot_x = g_cursor_hotspot_x;
    hot_y = g_cursor_hotspot_y;
    if (hot_x < 0) hot_x = 0;
    if (hot_y < 0) hot_y = 0;
    if (hot_x >= buf->width) hot_x = buf->width - 1;
    if (hot_y >= buf->height) hot_y = buf->height - 1;

    memset(&req, 0, sizeof(req));
    req.width = (uint32_t)buf->width;
    req.height = (uint32_t)buf->height;
    req.hot_x = (uint32_t)hot_x;
    req.hot_y = (uint32_t)hot_y;
    req.pixels = (uint64_t)(uintptr_t)image;
    if (ioctl(g_fb_fd, FB_GPU_SET_CURSOR, &req) != 0) {
        if (upload_logs < 8) {
            fprintf(stderr,
                    "wlcomp: hardware cursor client upload failed errno=%d (%s) size=%dx%d fmt=0x%x\n",
                    errno, strerror(errno), buf->width, buf->height,
                    buf->format);
            upload_logs++;
        }
        return 0;
    }

    memset(&mv, 0, sizeof(mv));
    mv.x = g_cursor_x;
    mv.y = g_cursor_y;
    mv.flags = FB_GPU_CURSOR_F_VISIBLE;
    (void)ioctl(g_fb_fd, FB_GPU_MOVE_CURSOR, &mv);
    if (upload_logs < 8) {
        fprintf(stderr,
                "wlcomp: hardware cursor client image uploaded size=%dx%d hotspot=%d,%d\n",
                buf->width, buf->height, hot_x, hot_y);
        upload_logs++;
    }
    return 1;
}

static int hw_cursor_upload_default_arrow(void)
{
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
    uint32_t img[12 * 19];
    struct fb_gpu_cursor_image req;
    struct fb_gpu_cursor_move mv;

    if (!g_hw_cursor || g_fb_fd < 0)
        return 0;
    for (int y = 0; y < 19; y++) {
        for (int x = 0; x < 12; x++) {
            uint8_t v = arrow[y][x];
            uint32_t px = 0;

            if (v == 1)
                px = 0xFF000000u;
            else if (v == 2)
                px = 0xFFFFFFFFu;
            img[y * 12 + x] = px;
        }
    }

    memset(&req, 0, sizeof(req));
    req.width = 12;
    req.height = 19;
    req.pixels = (uint64_t)(uintptr_t)img;
    if (ioctl(g_fb_fd, FB_GPU_SET_CURSOR, &req) != 0)
        return 0;

    memset(&mv, 0, sizeof(mv));
    mv.x = g_cursor_x;
    mv.y = g_cursor_y;
    mv.flags = FB_GPU_CURSOR_F_VISIBLE;
    (void)ioctl(g_fb_fd, FB_GPU_MOVE_CURSOR, &mv);
    return 1;
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
    enabled = cmdline_int_value("wlcomp_gpu_compose", enabled) != 0;
    env = getenv("XV6_WLCOMP_GPU_COMPOSE");
    if (env && *env)
        enabled = atoi(env) != 0;
    if (cmdline_int_value("wlcomp_pageflip_diag", 0) != 0)
        enabled = 1;
    return enabled;
}

static int gpu_compose_present_direct_scanout(struct wlcomp_buffer *buf,
                                              uint32_t x, uint32_t y,
                                              uint32_t w, uint32_t h,
                                              uint64_t src_off,
                                              uint32_t src_pitch)
{
    struct fb_gpu_scanout_flush flush;
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
    uint64_t t_flush = 0;
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
    memset(&flush, 0, sizeof(flush));
    flush.x = x;
    flush.y = y;
    flush.w = w;
    flush.h = h;
    if (ioctl(g_fb_fd, FB_GPU_SCANOUT_FLUSH, &flush) != 0)
        return 0;
    if (perf_log)
        t_flush = get_time_us();

    if (perf_log) {
        direct_frames++;
        direct_copy_us += t_copy - t0;
        direct_total_us += t_flush - t0;
    }
    if (perf_log && direct_frames >= 20) {
        fprintf(stderr,
                "wlcomp: gpu-compose-direct-scanout avg_us=%lu copy=%lu frames=%lu rect=%u,%u %ux%u\n",
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

static int gpu_compose_present_resource_scanout(struct wlcomp_buffer *buf,
                                                uint32_t handle,
                                                uint32_t x, uint32_t y,
                                                uint32_t w, uint32_t h)
{
    struct fb_gpu_bo_present cmd;
    static int log_count;
    int enabled = cmdline_flag_enabled("wlcomp_gpu_resource_scanout") ||
                  wlcomp_fullscreen_direct_enabled();
    const char *env = getenv("XV6_WLCOMP_GPU_RESOURCE_SCANOUT");

    if (env && *env)
        enabled = atoi(env) != 0;
    if (!enabled || g_fb_fd < 0 || !buf || !buf->is_gpu_bo ||
        handle == 0 || buf->y_inverted || buf->plane_stride[0] <= 0 ||
        buf->width <= 0 || buf->height <= 0)
        return 0;

    /*
     * Binding a client resource directly as scanout changes QEMU's visible
     * mode to that resource rectangle.  Only use it for true fullscreen
     * buffers; windowed surfaces must copy/compose into the desktop scanout.
     */
    if (x != 0 || y != 0 || w != (uint32_t)g_fb_w ||
        h != (uint32_t)g_fb_h || (uint32_t)buf->width != (uint32_t)g_fb_w ||
        (uint32_t)buf->height != (uint32_t)g_fb_h)
        return 0;

    memset(&cmd, 0, sizeof(cmd));
    cmd.w = (uint32_t)buf->width;
    cmd.h = (uint32_t)buf->height;
    cmd.src_pitch = (uint32_t)buf->plane_stride[0];
    cmd.pixels = 0;
    cmd.handle = handle;
    cmd.flags = FB_GPU_BO_PRESENT_F_VIRGL_SCANOUT;
    if (ioctl(g_fb_fd, FB_GPU_BO_PRESENT, &cmd) != 0)
        return 0;

    g_damage_stats.gpu_compose_presents++;
    g_damage_stats.gpu_compose_pixels += (uint64_t)cmd.w * cmd.h;
    if (log_count < 8) {
        fprintf(stderr,
                "wlcomp: gpu-compose presented path=resource-scanout handle=%u size=%ux%u fence=%lu\n",
                handle, cmd.w, cmd.h, (unsigned long)cmd.fence);
        log_count++;
    }
    return 1;
}

static int gpu_compose_shadow_sync_enabled(void)
{
    static int initialized;
    static int enabled = 1;
    const char *env;

    if (initialized)
        return enabled;
    initialized = 1;
    enabled = cmdline_int_value("wlcomp_gpu_shadow_sync", enabled) != 0;
    env = getenv("XV6_WLCOMP_GPU_SHADOW_SYNC");
    if (env && *env)
        enabled = strcmp(env, "0") != 0 &&
                  strcmp(env, "no") != 0 &&
                  strcmp(env, "false") != 0 &&
                  strcmp(env, "off") != 0;
    return enabled;
}

static int gpu_compose_virgl_copy_enabled(void)
{
    const char *env = getenv("XV6_WLCOMP_GPU_VIRGL_COPY");
    int pageflip_diag = cmdline_int_value("wlcomp_pageflip_diag", 0) != 0;
    int gl_compose = wlcomp_gl_compose_enabled();
    int enabled = g_fb_virgl_backed && !gl_compose &&
        !cmdline_flag_enabled("wlcomp_no_gpu_virgl_copy");

    if (pageflip_diag)
        enabled = g_fb_virgl_backed &&
            !cmdline_flag_enabled("wlcomp_no_gpu_virgl_copy");
    if (gl_compose && !cmdline_flag_enabled("wlcomp_force_gpu_virgl_copy"))
        return 0;
    enabled = cmdline_int_value("wlcomp_gpu_virgl_copy", enabled) != 0;
    if (env && *env)
        enabled = strcmp(env, "0") != 0 &&
                  strcmp(env, "no") != 0 &&
                  strcmp(env, "false") != 0 &&
                  strcmp(env, "off") != 0;
    return enabled;
}

static int gpu_compose_skip_target_bo_copy_enabled(void)
{
    static int initialized;
    static int enabled;
    const char *env;

    if (initialized)
        return enabled;
    initialized = 1;
    enabled =
        cmdline_int_value("wlcomp_pageflip_diag", 0) != 0 ||
        cmdline_int_value("wlcomp_gpu_skip_target_bo_copy", 0) != 0;
    env = getenv("XV6_WLCOMP_GPU_SKIP_TARGET_BO_COPY");
    if (env && *env)
        enabled = strcmp(env, "0") != 0 &&
                  strcmp(env, "no") != 0 &&
                  strcmp(env, "false") != 0 &&
                  strcmp(env, "off") != 0;
    return enabled;
}

static void gpu_compose_shadow_sync_buffer(struct wlcomp_buffer *buf,
                                           int32_t draw_x, int32_t draw_y,
                                           int32_t row0, int32_t row1,
                                           int32_t col0, int32_t col1)
{
    uint32_t *src;
    int32_t src_stride_px;
    static int sync_logs;

    if (!gpu_compose_shadow_sync_enabled() || !g_fb_buf || !buf ||
        !buf->is_gpu_bo || buf->plane_stride[0] <= 0 ||
        (buf->format != WL_SHM_FORMAT_ARGB8888 &&
         buf->format != WL_SHM_FORMAT_XRGB8888 &&
         buf->format != DRM_FORMAT_ARGB8888 &&
         buf->format != DRM_FORMAT_XRGB8888))
        return;

    if (row0 < 0) row0 = 0;
    if (col0 < 0) col0 = 0;
    if (row1 > buf->height) row1 = buf->height;
    if (col1 > buf->width) col1 = buf->width;
    if (row0 >= row1 || col0 >= col1)
        return;

    src = (uint32_t *)buffer_data(buf);
    if (!src)
        return;
    src_stride_px = buf->plane_stride[0] / 4;
    for (int32_t row = row0; row < row1; row++) {
        int32_t dy = draw_y + row;
        int32_t dx = draw_x + col0;
        int32_t src_row = buf->y_inverted ? (buf->height - 1 - row) : row;
        size_t copy_bytes = (size_t)(col1 - col0) * sizeof(uint32_t);

        if (dy < 0 || dy >= (int32_t)g_fb_h ||
            dx < 0 || dx >= (int32_t)g_fb_w)
            continue;
        if (dx + (col1 - col0) > (int32_t)g_fb_w)
            copy_bytes = (size_t)((int32_t)g_fb_w - dx) * sizeof(uint32_t);
        if (copy_bytes == 0)
            continue;
        memcpy(&g_fb_buf[dy * g_fb_w + dx],
               &src[src_row * src_stride_px + col0], copy_bytes);
    }
    if (sync_logs < 4) {
        fprintf(stderr,
                "wlcomp: gpu-compose shadow sync readback-fallback rect=%d,%d %dx%d\n",
                draw_x + col0, draw_y + row0, col1 - col0, row1 - row0);
        sync_logs++;
    }
}

static int gpu_compose_present_buffer(struct wlcomp_buffer *buf,
                                      int32_t draw_x, int32_t draw_y,
                                      int32_t row0, int32_t row1,
                                      int32_t col0, int32_t col1,
                                      int force_virgl_copy)
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
    int virgl_copy = gpu_compose_virgl_copy_enabled() ||
        (force_virgl_copy && g_fb_virgl_backed &&
         !cmdline_flag_enabled("wlcomp_no_webkit_virgl_copy"));
    int virgl_copy_delay_us =
        cmdline_int_value("wlcomp_gpu_copy_delay_us", 0);

    uint64_t present_t0;
    uint64_t present_dt;

    if (!gpu_compose_enabled() || g_fb_fd < 0) {
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
    if (g_fb_virgl_backed && virgl_copy &&
        !gpu_compose_skip_target_bo_copy_enabled()) {
        struct fb_gpu_bo_copy copy;

        memset(&copy, 0, sizeof(copy));
        copy.src_handle = handle;
        copy.dst_handle = g_fb_bo_handle;
        copy.src_x = (uint32_t)col0;
        copy.src_y = (uint32_t)row0;
        copy.dst_x = cmd.x;
        copy.dst_y = cmd.y;
        copy.w = w;
        copy.h = h;
        present_t0 = perf_log ? get_time_us() : 0;
        if (ioctl(g_fb_fd, FB_GPU_BO_COPY, &copy) == 0) {
            present_dt = perf_log ? get_time_us() - present_t0 : 0;
            if (perf_log) {
                present_total_us += present_dt;
                present_frames++;
            }
            g_damage_stats.gpu_compose_presents++;
            g_damage_stats.gpu_compose_pixels += (uint64_t)w * h;
            if (log_count < 8) {
                fprintf(stderr,
                        "wlcomp: gpu-compose copied-to-target src=%u dst=%u rect=%u,%u %ux%u src=%u,%u fence=%lu\n",
                        handle, g_fb_bo_handle, copy.dst_x, copy.dst_y,
                        copy.w, copy.h, copy.src_x, copy.src_y,
                        (unsigned long)copy.fence);
                log_count++;
            }
            buf->gpu_compose_presented = 1;
            return 1;
        }
        if (fail_log_count < 8) {
            fprintf(stderr,
                    "wlcomp: gpu-compose target copy failed src=%u dst=%u rect=%u,%u %ux%u errno=%d (%s)\n",
                    handle, g_fb_bo_handle, copy.dst_x, copy.dst_y,
                    copy.w, copy.h, errno, strerror(errno));
            fail_log_count++;
        }
    }
    if (gpu_compose_present_resource_scanout(buf, handle, cmd.x, cmd.y, w, h)) {
        buf->gpu_compose_presented = 1;
        return 1;
    }
    if (wlcomp_gl_compose_present_buffer(buf, draw_x, draw_y,
                                         row0, row1, col0, col1)) {
        buf->gpu_compose_presented = 1;
        return 1;
    }
    (void)wlcomp_gl_compose_probe_buffer(buf);
    if (virgl_copy)
        cmd.flags |= FB_GPU_BO_PRESENT_F_VIRGL_COPY;
    if (gpu_compose_present_direct_scanout(buf, cmd.x, cmd.y, w, h,
                                           offset, cmd.src_pitch)) {
        g_damage_stats.gpu_compose_presents++;
        g_damage_stats.gpu_compose_pixels += (uint64_t)w * h;
        buf->gpu_compose_presented = 1;
        return 1;
    }

    present_t0 = perf_log ? get_time_us() : 0;
    if (virgl_copy_delay_us > 0)
        usleep((useconds_t)virgl_copy_delay_us);
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
    if ((cmd.flags & FB_GPU_BO_PRESENT_F_READBACK_FALLBACK) != 0)
        gpu_compose_shadow_sync_buffer(buf, draw_x, draw_y,
                                       row0, row1, col0, col1);
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
    buf->gpu_compose_presented = 1;
    if (log_count < 8) {
        fprintf(stderr,
                "wlcomp: gpu-compose presented handle=%u flags=0x%x rect=%u,%u %ux%u src_off=%lu fence=%lu\n",
                handle, cmd.flags, cmd.x, cmd.y, w, h, (unsigned long)offset,
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

    /*
     * The GL compose path draws explicit source and destination rectangles, so
     * it can handle xdg window geometry where the client buffer is larger than
     * the visible content area.  WebKit/MiniBrowser commonly submits this shape.
     */
    if (g_fb_virgl_backed && wlcomp_gl_compose_enabled() &&
        buf->is_gpu_bo && !buf->y_inverted)
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
