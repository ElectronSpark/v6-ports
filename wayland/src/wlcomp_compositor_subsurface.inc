/* ══════════════════════════════════════════════════════════════════════
 *  wl_compositor / wl_surface
 * ══════════════════════════════════════════════════════════════════════ */

static void surface_destroy(struct wl_client *client, struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void surface_attach(struct wl_client *client, struct wl_resource *resource,
                           struct wl_resource *buffer, int32_t x, int32_t y)
{
    (void)client; (void)x; (void)y;
    struct wlcomp_surface *surf = surface_from_resource(resource);
    struct wlcomp_buffer *old_pending;

    if (!surf) return;
    old_pending = surf->pending_buf;

    if (buffer) {
        surf->pending_buf = wl_resource_get_user_data(buffer);
    } else {
        surf->pending_buf = NULL;
    }
    surf->has_pending_buffer = 1;
    surface_log_limited(surf, "attach", surf->pending_buf);

    if (old_pending && old_pending != surf->pending_buf)
        buffer_maybe_free(old_pending);
}

static void surface_damage(struct wl_client *client, struct wl_resource *resource,
                           int32_t x, int32_t y, int32_t w, int32_t h)
{
    (void)client;
    surface_pending_damage_add(surface_from_resource(resource), x, y, w, h);
}

static void surface_frame(struct wl_client *client, struct wl_resource *resource,
                          uint32_t callback_id)
{
    struct wlcomp_surface *surf = surface_from_resource(resource);
    struct wlcomp_frame_callback *frame_cb;
    if (!surf) return;

    frame_cb = calloc(1, sizeof(*frame_cb));
    if (!frame_cb) {
        wl_resource_post_no_memory(resource);
        return;
    }

    struct wl_resource *cb = wl_resource_create(client,
        &wl_callback_interface, 1, callback_id);
    if (!cb) {
        free(frame_cb);
        wl_resource_post_no_memory(resource);
        return;
    }
    frame_cb->resource = cb;
    wl_list_insert(surf->pending_frame_callbacks.prev, &frame_cb->link);
    surface_log_limited(surf, "frame", surf->committed_buf);
}

static void surface_set_opaque_region(struct wl_client *client,
                                      struct wl_resource *resource,
                                      struct wl_resource *region)
{
    (void)client; (void)resource; (void)region;
}

static void surface_set_input_region(struct wl_client *client,
                                     struct wl_resource *resource,
                                     struct wl_resource *region)
{
    (void)client; (void)resource; (void)region;
}

static int commit_frame_callbacks_enabled(void)
{
    static int initialized;
    static int enabled;
    const char *env;

    if (initialized)
        return enabled;
    initialized = 1;
    enabled = cmdline_int_value("wlcomp_callback_on_commit", 0) != 0;
    env = getenv("XV6_WLCOMP_CALLBACK_ON_COMMIT");
    if (env && *env)
        enabled = strcmp(env, "0") != 0 && strcmp(env, "no") != 0 &&
                  strcmp(env, "false") != 0 && strcmp(env, "off") != 0;
    return enabled;
}

static int surface_commit_can_send_frame_callbacks(struct wlcomp_surface *surf)
{
    struct wlcomp_buffer *buf;

    if (!commit_frame_callbacks_enabled())
        return 0;
    if (!surf || surf->is_cursor || !surf->mapped || !surf->committed_buf)
        return 0;
    if (!g_fb_virgl_backed || !gpu_compose_enabled() ||
        !wlcomp_gl_compose_enabled())
        return 0;
    buf = surf->committed_buf;
    if (!buf->is_gpu_bo || buf->is_d3d12_shared)
        return 0;
    if (!buffer_ready_for_gl_compose_latch(buf))
        return 0;
    if (!gpu_compose_can_present_surface(surf, buf))
        return 0;
    if (!wlcomp_gl_compose_probe_buffer(buf))
        return 0;
    return 1;
}

static int surface_commit_can_stage_gpu_buffer(struct wlcomp_surface *surf,
                                               struct wlcomp_buffer *old_buf,
                                               struct wlcomp_buffer *new_buf)
{
    if (!commit_frame_callbacks_enabled())
        return 0;
    if (!surf || !old_buf || !new_buf || surf->is_cursor)
        return 0;
    if (!g_fb_virgl_backed || !gpu_compose_enabled() ||
        !wlcomp_gl_compose_enabled())
        return 0;
    if (!old_buf->is_gpu_bo || old_buf->is_d3d12_shared ||
        !new_buf->is_gpu_bo || new_buf->is_d3d12_shared)
        return 0;
    if (old_buf->gpu_compose_presented || surf->buffer_released)
        return 0;
    if (!buffer_ready_for_gl_compose_latch(new_buf))
        return 0;
    if (!gpu_compose_can_present_surface(surf, new_buf))
        return 0;
    if (!wlcomp_gl_compose_probe_buffer(new_buf))
        return 0;
    return 1;
}

static int surface_send_frame_callbacks_now(struct wlcomp_surface *surf,
                                            uint32_t now, const char *stage)
{
    struct wlcomp_frame_callback *cb;
    struct wlcomp_frame_callback *tmp;
    int pending_before = surface_frame_callback_count(surf);
    int delivered = 0;

    if (pending_before <= 0) {
        frame_perf_note(now, stage, pending_before, 0, 0, 0);
        return 0;
    }
    if (!frame_callbacks_due(now)) {
        frame_perf_note(now, stage, pending_before, 0, 0, 0);
        return 0;
    }
    wl_list_for_each_safe(cb, tmp, &surf->frame_callbacks, link) {
        xv6_d3d12_observe_frame_callback(surf, now);
        wl_callback_send_done(cb->resource, now);
        wl_resource_destroy(cb->resource);
        wl_list_remove(&cb->link);
        free(cb);
        delivered++;
    }
    frame_perf_note(now, stage, pending_before, delivered, 0, 1);
    return delivered;
}

static void surface_commit(struct wl_client *client, struct wl_resource *resource)
{
    (void)client;
    struct wlcomp_surface *surf = surface_from_resource(resource);
    struct wlcomp_buffer *old_committed = NULL;
    int old_buffer_released = 0;
    int old_w = 0, old_h = 0;
    int buffer_size_changed = 0;
    int first_map = 0;
    int had_pending_buffer;
    int buffer_changed = 0;
    if (!surf) return;

    had_pending_buffer = surf->has_pending_buffer;
    if ((surf->has_pending_acquire_fence || surf->pending_release) &&
        (!surf->has_pending_buffer || !surf->pending_buf)) {
        if (surf->pending_acquire_fence_fd >= 0) {
            close(surf->pending_acquire_fence_fd);
            surf->pending_acquire_fence_fd = -1;
        }
        surf->has_pending_acquire_fence = 0;
        wl_resource_post_error(
            surf->sync_resource ? surf->sync_resource : resource,
            ZWP_LINUX_SURFACE_SYNCHRONIZATION_V1_ERROR_NO_BUFFER,
            "explicit sync commit has no attached buffer");
        return;
    }
    if (surf->has_pending_buffer) {
        surface_log_limited(surf, "commit-pending", surf->pending_buf);
        if (surf->has_pending_acquire_fence && surf->pending_buf) {
            if (surf->pending_buf->acquire_fence_fd >= 0)
                close(surf->pending_buf->acquire_fence_fd);
            surf->pending_buf->acquire_fence_fd =
                surf->pending_acquire_fence_fd;
            surf->pending_buf->acquire_wait_start_ms = 0;
            surf->pending_acquire_fence_fd = -1;
            surf->has_pending_acquire_fence = 0;
        }
        if (surf->pending_release && surf->pending_buf) {
            surf->pending_buf->explicit_release = surf->pending_release;
            surf->pending_release = NULL;
        }
        old_committed = surf->committed_buf;
        old_buffer_released = surf->buffer_released;
        if (old_committed) {
            old_w = old_committed->width;
            old_h = old_committed->height;
        }
        if (old_committed && !surf->pending_buf)
            damage_surface(surf);
        if (old_committed && surf->pending_buf &&
            surf->pending_buf->is_gpu_bo &&
            !surf->pending_buf->is_d3d12_shared &&
            !buffer_ready_for_gl_compose_latch(surf->pending_buf)) {
            static int defer_logs;
            struct wlcomp_buffer *old_deferred = surf->deferred_buf;

            surf->deferred_buf = surf->pending_buf;
            surf->pending_buf = NULL;
            surf->has_pending_buffer = 0;
            if (old_deferred && old_deferred != surf->deferred_buf) {
                queue_buffer_release(old_deferred);
                buffer_maybe_free(old_deferred);
            }
            if (defer_logs < 16) {
                fprintf(stderr,
                        "wlcomp: deferred gpu buffer commit pid=%d res=%u until acquire-ready\n",
                        surf->client_pid,
                        surf->deferred_buf->plane_virtio_resource_id[0]);
                defer_logs++;
            }
            surface_pending_damage_clear(surf);
            return;
        }
        if (surface_commit_can_stage_gpu_buffer(surf, old_committed,
                                                surf->pending_buf)) {
            static int stage_logs;
            struct wlcomp_buffer *old_staged = surf->staged_buf;

            surf->staged_buf = surf->pending_buf;
            surf->pending_buf = NULL;
            surf->has_pending_buffer = 0;
            surf->staged_buf->gpu_compose_presented = 0;
            frame_callback_list_append(&surf->staged_frame_callbacks,
                                       &surf->pending_frame_callbacks);
            if (old_staged && old_staged != surf->staged_buf) {
                queue_buffer_release(old_staged);
                buffer_maybe_free(old_staged);
            }
            if (stage_logs < 16) {
                fprintf(stderr,
                        "wlcomp: staged gpu buffer pid=%d res=%u behind unlatched res=%u callbacks=%d\n",
                        surf->client_pid,
                        surf->staged_buf->plane_virtio_resource_id[0],
                        old_committed->plane_virtio_resource_id[0],
                        frame_callback_list_count(
                            &surf->staged_frame_callbacks));
                stage_logs++;
            }
            surface_pending_damage_clear(surf);
            return;
        }
        surf->committed_buf = surf->pending_buf;
        surf->pending_buf = NULL;
        surf->has_pending_buffer = 0;
        frame_callback_list_append(&surf->frame_callbacks,
                                   &surf->pending_frame_callbacks);
        if (surf->committed_buf)
            surf->committed_buf->gpu_compose_presented = 0;
        {
            static int commit_diag_logs;

            if (commit_diag_logs < 96) {
                struct wlcomp_buffer *b = surf->committed_buf;

                fprintf(stderr,
                        "wlcomp: commit diag pid=%d id=%u mapped=%d "
                        "xdg=%d top=%d sub=%d parent=%u cursor=%d "
                        "buf=%dx%d stride=%d fmt=0x%x gpu=%d res=%u\n",
                        surf->client_pid,
                        surf->resource ?
                            wl_resource_get_id(surf->resource) : 0,
                        surf->mapped,
                        surf->xdg_surface != NULL,
                        surf->xdg_toplevel != NULL,
                        surf->is_subsurface,
                        surf->parent && surf->parent->resource ?
                            wl_resource_get_id(surf->parent->resource) : 0,
                        surf->is_cursor,
                        b ? b->width : 0,
                        b ? b->height : 0,
                        b ? b->stride : 0,
                        b ? b->format : 0,
                        b ? b->is_gpu_bo : 0,
                        b ? b->plane_virtio_resource_id[0] : 0);
                commit_diag_logs++;
            }
        }
        if (surf->committed_buf && surf->committed_buf->is_d3d12_shared) {
            surf->committed_buf->d3d12_present_complete = 0;
            surf->committed_buf->d3d12_failclosed_client_unblocked = 0;
            surf->committed_buf->d3d12_gpu_copy_complete = 0;
            surf->committed_buf->d3d12_present_evidence_pending = 0;
            surf->committed_buf->d3d12_present_wait_confirmed = 0;
            surf->committed_buf->d3d12_gpu_present_start_time = 0;
            surf->committed_buf->d3d12_gpu_copy_complete_time = 0;
            surf->committed_buf->d3d12_native_present_attempt_id = 0;
            surf->committed_buf->d3d12_native_present_completion_id = 0;
            surf->committed_buf->d3d12_present_content_crc = 0;
            surf->committed_buf->d3d12_visible_content_crc = 0;
            surf->committed_buf->d3d12_present_content_frame = 0;
            surf->committed_buf->d3d12_visible_content_frame = 0;
            surf->committed_buf->d3d12_present_frame_hash = 0;
            surf->committed_buf->d3d12_visible_frame_hash = 0;
            surf->committed_buf->d3d12_content_sample_present_id = 0;
            surf->committed_buf->d3d12_content_sample_completed_id = 0;
            surf->committed_buf->d3d12_content_sample_resource_generation = 0;
            surf->committed_buf->d3d12_content_sample_source_owned = 0;
            surf->committed_buf->d3d12_fb_blit_attempts_at_present =
                g_fb_blit_present_attempt_count;
            surf->committed_buf->d3d12_fb_blit_successes_at_present =
                g_fb_blit_present_success_count;
            surf->committed_buf->d3d12_cpu_map_rejects_at_present =
                g_d3d12_cpu_map_reject_count;
            surf->committed_buf->d3d12_cpu_plane_rejects_at_present =
                g_d3d12_cpu_plane_reject_count;
        }
        buffer_changed = old_committed != surf->committed_buf;
        if (old_committed && surf->committed_buf &&
            (old_w != surf->committed_buf->width ||
             old_h != surf->committed_buf->height))
            buffer_size_changed = 1;

        if (old_committed && old_committed != surf->committed_buf) {
            if (old_committed->resource && !old_buffer_released)
                queue_buffer_release(old_committed);
            buffer_maybe_free(old_committed);
        }

        surf->buffer_released = 0;
        if (surf->is_cursor && surf == g_cursor_surface)
            (void)hw_cursor_upload_surface(surf);
    }
    else {
        frame_callback_list_append(&surf->frame_callbacks,
                                   &surf->pending_frame_callbacks);
        surface_log_limited(surf, "commit-empty", surf->committed_buf);
    }

    if (!surf->has_pending_buffer && !surf->committed_buf)
        xdg_surface_send_initial_configure_if_needed(surf);

    if (surf->committed_buf && surf->xdg_toplevel && !surf->mapped) {
        /* Center the logical xdg window, not any buffer margins. */
        int32_t gx, gy, gw, gh;
        int cascade = surface_existing_mapped_toplevels();
        int32_t desktop_h = (int32_t)g_fb_h - 36;
        int32_t max_x, max_y;
        surface_window_geometry(surf, &gx, &gy, &gw, &gh);
        (void)gx;
        (void)gy;
        surf->x = ((int32_t)g_fb_w - gw) / 2;
        surf->y = (desktop_h - gh) / 2;
        if (cascade > 0) {
            surf->x += (cascade % 3) * 220;
            surf->y += (cascade % 3) * 120;
        }
        if (surf->x < 0) surf->x = 0;
        if (surf->y < 0) surf->y = 0;
        max_x = (int32_t)g_fb_w - gw;
        max_y = desktop_h - gh;
        if (max_x < 0) max_x = 0;
        if (max_y < 0) max_y = 0;
        if (surf->x > max_x) surf->x = max_x;
        if (surf->y > max_y) surf->y = max_y;
        if (surface_is_fullscreen_direct(surf)) {
            /* Unredirected client owns the whole scanout: pin to origin. */
            surf->x = 0;
            surf->y = 0;
        }
        surf->mapped = 1;
        first_map = 1;
        fprintf(stderr, "wlcomp: mapped toplevel pid=%d size=%dx%d geom=%dx%d pos=%d,%d cascade=%d\n",
                surf->client_pid,
                surf->committed_buf ? surf->committed_buf->width : 0,
                surf->committed_buf ? surf->committed_buf->height : 0,
                gw, gh, surf->x, surf->y, cascade);

        /*
         * A newly mapped Wayland toplevel is a foreground activation.  Clear
         * internal-window focus as well, otherwise an older terminal/calculator
         * can remain logically on top and steal clicks through the WebKit
         * surface until the next explicit focus transition.
         */
        g_iwin_focus = -1;
        surface_raise_to_top(surf);
        surface_set_keyboard_focus(surf);
        wlcomp_emit_demo_interaction_matrix(surf, "mapped");
    }

    if (surf->committed_buf && surf->is_subsurface && surf->parent &&
        !surf->mapped) {
        surf->mapped = 1;
        first_map = 1;
        fprintf(stderr,
                "wlcomp: mapped subsurface pid=%d parent_pid=%d pos=%d,%d size=%dx%d\n",
                surf->client_pid,
                surf->parent ? surf->parent->client_pid : 0,
                surf->sub_x, surf->sub_y,
                surf->committed_buf->width, surf->committed_buf->height);
    }
    if (surf->committed_buf && surf->committed_buf->is_d3d12_shared &&
        !surf->xdg_toplevel && !surf->is_subsurface && !surf->mapped) {
        surf->mapped = 1;
        first_map = 1;
        fprintf(stderr,
                "wlcomp: mapped roleless D3D12 shared-resource surface pid=%d size=%dx%d resource=0x%x fence=0x%x target=%lu import_complete=1 release_after_present=1\n",
                surf->client_pid, surf->committed_buf->width,
                surf->committed_buf->height,
                surf->committed_buf->d3d12_opened_resource,
                surf->committed_buf->d3d12_opened_fence,
                (unsigned long)surf->committed_buf->d3d12_fence_value);
    }
    if (xv6_wlcomp_trace_enabled() && surf->committed_buf &&
        !surf->xdg_toplevel && !surf->is_subsurface &&
        !surf->is_cursor && !surf->mapped) {
        static int roleless_logs;

        if (roleless_logs < 32 || wlcomp_verbose_log_enabled()) {
            fprintf(stderr,
                    "wlcomp: roleless committed surface pid=%d id=%u "
                    "size=%dx%d fmt=0x%x gpu=%d res=%u mapped=0\n",
                    surf->client_pid,
                    surf->resource ? wl_resource_get_id(surf->resource) : 0,
                    surf->committed_buf->width, surf->committed_buf->height,
                    surf->committed_buf->format,
                    surf->committed_buf->is_gpu_bo,
                    surf->committed_buf->plane_virtio_resource_id[0]);
            roleless_logs++;
        }
    }

    if (first_map || buffer_size_changed)
        damage_full_reason(FULL_DAMAGE_CLIENT);
    else if (damage_surface_pending(surf)) {
        /* The pending Wayland damage was enough to repaint this commit. */
    } else if (had_pending_buffer && buffer_changed && surf->committed_buf) {
        damage_surface_gpu_buffer_swap(surf);
    }
    if (surf->maximized && surf->xdg_toplevel && surf->committed_buf &&
        (first_map || buffer_size_changed || buffer_changed)) {
        static int maximized_commit_logs;

        if (wlcomp_verbose_log_enabled() || maximized_commit_logs < 2) {
            int32_t gx, gy, gw, gh;

            surface_window_geometry(surf, &gx, &gy, &gw, &gh);
            fprintf(stderr,
                    "wlcomp: maximized commit pid=%d buf=%dx%d geom=%d,%d %dx%d work=%dx%d\n",
                    surf->client_pid,
                    surf->committed_buf->width, surf->committed_buf->height,
                    gx, gy, gw, gh, work_area_w(), work_area_h());
            if (!wlcomp_verbose_log_enabled())
                maximized_commit_logs++;
        }
    }
    if (surf->fullscreen && surf->xdg_toplevel && surf->committed_buf &&
        (first_map || buffer_size_changed || buffer_changed)) {
        static int fullscreen_commit_logs;

        if (wlcomp_verbose_log_enabled() || fullscreen_commit_logs < 8) {
            int32_t gx, gy, gw, gh;

            surface_window_geometry(surf, &gx, &gy, &gw, &gh);
            fprintf(stderr,
                    "wlcomp: fullscreen commit pid=%d buf=%dx%d geom=%d,%d %dx%d fb=%ux%u window_geom=%d,%d %dx%d\n",
                    surf->client_pid,
                    surf->committed_buf->width, surf->committed_buf->height,
                    gx, gy, gw, gh, g_fb_w, g_fb_h,
                    surf->window_x, surf->window_y,
                    surf->window_w, surf->window_h);
            if (!wlcomp_verbose_log_enabled())
                fullscreen_commit_logs++;
        }
    }
    if (buffer_size_changed && surface_is_demo_window(surf)) {
        surf->demo_resize_requested = 1;
        surf->demo_resize_buffer_changed = 1;
        surf->demo_resize_w = surf->committed_buf ?
            surf->committed_buf->width : 0;
        surf->demo_resize_h = surf->committed_buf ?
            surf->committed_buf->height : 0;
        wlcomp_emit_demo_interaction_matrix(surf, "buffer-size-change");
    }
    if (had_pending_buffer && buffer_changed &&
        old_committed &&
        (old_committed->gpu_compose_presented || old_buffer_released) &&
        !surf->staged_buf &&
        surface_commit_can_send_frame_callbacks(surf)) {
        int delivered =
            surface_send_frame_callbacks_now(surf, get_time_ms(), "commit");

        if (delivered > 0)
            wl_display_flush_clients(g_display);
    }
    if (surf->committed_buf && surf->committed_buf->is_d3d12_shared) {
        int32_t gx, gy, gw, gh;
        int32_t ax, ay;

        surface_window_geometry(surf, &gx, &gy, &gw, &gh);
        surface_absolute_position(surf, &ax, &ay);
        if (gw > 0 && gh > 0) {
            int32_t draw_x = ax - gx;
            int32_t draw_y = ay - gy;
            int32_t src_x = gx < 0 ? 0 : gx;
            int32_t src_y = gy < 0 ? 0 : gy;
            int32_t src_x2 = gx + gw;
            int32_t src_y2 = gy + gh;
            int32_t dst_x;
            int32_t dst_y;
            uint32_t width;
            uint32_t height;

            if (src_x2 > surf->committed_buf->width)
                src_x2 = surf->committed_buf->width;
            if (src_y2 > surf->committed_buf->height)
                src_y2 = surf->committed_buf->height;
            if (src_x2 < src_x)
                src_x2 = src_x;
            if (src_y2 < src_y)
                src_y2 = src_y;
            dst_x = draw_x + src_x;
            dst_y = draw_y + src_y;
            width = (uint32_t)(src_x2 - src_x);
            height = (uint32_t)(src_y2 - src_y);
            if (dst_x < 0) {
                int32_t clip = -dst_x;
                if (clip < (int32_t)width) {
                    src_x += clip;
                    width -= (uint32_t)clip;
                    dst_x = 0;
                } else {
                    width = 0;
                }
            }
            if (dst_y < 0) {
                int32_t clip = -dst_y;
                if (clip < (int32_t)height) {
                    src_y += clip;
                    height -= (uint32_t)clip;
                    dst_y = 0;
                } else {
                    height = 0;
                }
            }
            if (dst_x >= (int32_t)g_fb_w || dst_y >= (int32_t)g_fb_h)
                width = height = 0;
            if (width > 0 && dst_x + (int32_t)width > (int32_t)g_fb_w)
                width = (uint32_t)((int32_t)g_fb_w - dst_x);
            if (height > 0 && dst_y + (int32_t)height > (int32_t)g_fb_h)
                height = (uint32_t)((int32_t)g_fb_h - dst_y);
            if (width > 0 && height > 0 &&
                !xv6_d3d12_present_imported_buffer(
                    surf, surf->committed_buf, dst_x, dst_y, src_x, src_y,
                    width, height))
                damage_full_reason(FULL_DAMAGE_ACQUIRE);
        }
    }
    surface_pending_damage_clear(surf);
}

static void surface_promote_deferred_buffers(void)
{
    struct wlcomp_surface *surf;
    static int promote_logs;
    static int stage_promote_logs;

    wl_list_for_each(surf, &g_surfaces, link) {
        struct wlcomp_buffer *buf = surf->deferred_buf;
        struct wlcomp_buffer *old_committed;
        int old_buffer_released;

        if (!buf)
            continue;
        if (!buffer_ready_for_gl_compose_latch(buf))
            continue;

        old_committed = surf->committed_buf;
        old_buffer_released = surf->buffer_released;
        surf->deferred_buf = NULL;
        surf->committed_buf = buf;
        surf->buffer_released = 0;
        buf->gpu_compose_presented = 0;

        if (old_committed && old_committed != buf) {
            if (old_committed->resource && !old_buffer_released)
                queue_buffer_release(old_committed);
            buffer_maybe_free(old_committed);
        }
        damage_surface_gpu_buffer_swap(surf);
        if (promote_logs < 16) {
            fprintf(stderr,
                    "wlcomp: promoted deferred gpu buffer pid=%d res=%u acquire-ready\n",
                    surf->client_pid,
                    buf->plane_virtio_resource_id[0]);
            promote_logs++;
        }
    }
    wl_list_for_each(surf, &g_surfaces, link) {
        struct wlcomp_buffer *buf = surf->staged_buf;
        struct wlcomp_buffer *old_committed;
        int old_buffer_released;

        if (!buf)
            continue;
        if (surf->committed_buf && !surf->committed_buf->gpu_compose_presented &&
            !surf->buffer_released)
            continue;
        if (!buffer_ready_for_gl_compose_latch(buf))
            continue;

        old_committed = surf->committed_buf;
        old_buffer_released = surf->buffer_released;
        surf->staged_buf = NULL;
        surf->committed_buf = buf;
        surf->buffer_released = 0;
        buf->gpu_compose_presented = 0;
        frame_callback_list_append(&surf->frame_callbacks,
                                   &surf->staged_frame_callbacks);
        frame_callbacks_force_due();

        if (old_committed && old_committed != buf) {
            if (old_committed->resource && !old_buffer_released)
                queue_buffer_release(old_committed);
            buffer_maybe_free(old_committed);
        }
        damage_surface_gpu_buffer_swap(surf);
        if (stage_promote_logs < 16) {
            fprintf(stderr,
                    "wlcomp: promoted staged gpu buffer pid=%d res=%u callbacks=%d\n",
                    surf->client_pid, buf->plane_virtio_resource_id[0],
                    surface_frame_callback_count(surf));
            stage_promote_logs++;
        }
    }
}

static void surface_set_buffer_transform(struct wl_client *client,
                                         struct wl_resource *resource,
                                         int32_t transform)
{
    (void)client; (void)resource; (void)transform;
}

static void surface_set_buffer_scale(struct wl_client *client,
                                     struct wl_resource *resource,
                                     int32_t scale)
{
    (void)client; (void)resource; (void)scale;
}

static void surface_damage_buffer(struct wl_client *client,
                                  struct wl_resource *resource,
                                  int32_t x, int32_t y, int32_t w, int32_t h)
{
    (void)client;
    surface_pending_damage_add(surface_from_resource(resource), x, y, w, h);
}

static void surface_offset(struct wl_client *client,
                           struct wl_resource *resource,
                           int32_t x, int32_t y)
{
    (void)client; (void)resource; (void)x; (void)y;
}

static const struct wl_surface_interface surface_impl = {
    .destroy              = surface_destroy,
    .attach               = surface_attach,
    .damage               = surface_damage,
    .frame                = surface_frame,
    .set_opaque_region    = surface_set_opaque_region,
    .set_input_region     = surface_set_input_region,
    .commit               = surface_commit,
    .set_buffer_transform = surface_set_buffer_transform,
    .set_buffer_scale     = surface_set_buffer_scale,
    .damage_buffer        = surface_damage_buffer,
    .offset               = surface_offset,
};

static int surface_client_exited(pid_t pid)
{
    char path[64];
    char buf[256];
    int fd;
    ssize_t n;

    if (pid <= 0)
        return 0;

    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    fd = open(path, O_RDONLY);
    if (fd >= 0) {
        n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n > 0) {
            buf[n] = '\0';
            if (strstr(buf, "State:\tZ") || strstr(buf, "State:\tX"))
                return 1;
            return 0;
        }
    }

    if (kill(pid, 0) != 0 && errno == ESRCH)
        return 1;
    return 0;
}

static void surface_destroy_handler(struct wl_resource *resource)
{
    struct wlcomp_surface *surf = surface_from_resource(resource);
    if (surf) {
        int client_dead = 0;
        pid_t client_pid = surf->client_pid;
        struct wlcomp_buffer *pending_buf = surf->pending_buf;
        struct wlcomp_buffer *committed_buf = surf->committed_buf;
        struct wlcomp_buffer *deferred_buf = surf->deferred_buf;
        struct wlcomp_buffer *staged_buf = surf->staged_buf;
        struct wlcomp_surface *child;
        struct wlcomp_frame_callback *cb;
        struct wlcomp_frame_callback *tmp;

        if (surface_client_exited(client_pid))
            client_dead = 1;
        if (surf->mapped || surf->title[0] || surf->app_id[0])
            fprintf(stderr,
                    "wlcomp: surface destroy pid=%d mapped=%d title='%s' app_id='%s'\n",
                    surf->client_pid, surf->mapped, surf->title,
                    surf->app_id);
        damage_surface(surf);
        wl_list_for_each_safe(cb, tmp, &surf->pending_frame_callbacks, link) {
            wl_resource_destroy(cb->resource);
            wl_list_remove(&cb->link);
            free(cb);
        }
        wl_list_for_each_safe(cb, tmp, &surf->frame_callbacks, link) {
            wl_resource_destroy(cb->resource);
            wl_list_remove(&cb->link);
            free(cb);
        }
        wl_list_for_each_safe(cb, tmp, &surf->staged_frame_callbacks, link) {
            wl_resource_destroy(cb->resource);
            wl_list_remove(&cb->link);
            free(cb);
        }
        wl_list_for_each(child, &g_surfaces, link) {
            if (child->parent == surf) {
                child->parent = NULL;
                child->is_subsurface = 0;
                child->mapped = 0;
            }
        }
        if (g_focused == surf)
            g_focused = NULL;
        if (g_kbd_focused == surf)
            g_kbd_focused = NULL;
        if (g_cursor_surface == surf)
            g_cursor_surface = NULL;
        if (g_grab_surface == surf) {
            g_grab_surface = NULL;
            g_grab_mode = 0;
        }
        if (surf->xdg_toplevel)
            wl_resource_set_user_data(surf->xdg_toplevel, NULL);
        if (surf->xdg_surface)
            wl_resource_set_user_data(surf->xdg_surface, NULL);
        if (surf->subsurface)
            wl_resource_set_user_data(surf->subsurface, NULL);
        if (surf->sync_resource)
            wl_resource_set_user_data(surf->sync_resource, NULL);
        if (surf->pending_release)
            wl_resource_destroy(surf->pending_release);
        if (surf->pending_acquire_fence_fd >= 0)
            close(surf->pending_acquire_fence_fd);
        wl_resource_set_user_data(resource, NULL);
        wl_list_remove(&surf->link);
        free(surf);

        buffer_flush_release_for_surface_destroy(pending_buf);
        buffer_flush_release_for_surface_destroy(deferred_buf);
        buffer_flush_release_for_surface_destroy(staged_buf);
        buffer_flush_release_for_surface_destroy(committed_buf);
        if (client_dead) {
            buffer_forget_dead_client_resource(pending_buf);
            buffer_forget_dead_client_resource(deferred_buf);
            buffer_forget_dead_client_resource(staged_buf);
            buffer_forget_dead_client_resource(committed_buf);
        }
        if (pending_buf && pending_buf != committed_buf &&
            pending_buf != deferred_buf && pending_buf != staged_buf)
            buffer_maybe_free(pending_buf);
        if (deferred_buf && deferred_buf != committed_buf)
            buffer_maybe_free(deferred_buf);
        if (staged_buf && staged_buf != committed_buf &&
            staged_buf != deferred_buf)
            buffer_maybe_free(staged_buf);
        buffer_maybe_free(committed_buf);
        (void)client_dead;
        if (client_pid > 0)
            buffer_flush_dead_client_releases(client_pid);
    }
}

/* wl_region — stub (we don't use regions) */

static void region_destroy(struct wl_client *client, struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void region_add(struct wl_client *client, struct wl_resource *resource,
                       int32_t x, int32_t y, int32_t w, int32_t h)
{
    (void)client; (void)resource; (void)x; (void)y; (void)w; (void)h;
}

static void region_subtract(struct wl_client *client, struct wl_resource *resource,
                            int32_t x, int32_t y, int32_t w, int32_t h)
{
    (void)client; (void)resource; (void)x; (void)y; (void)w; (void)h;
}

static const struct wl_region_interface region_impl = {
    .destroy  = region_destroy,
    .add      = region_add,
    .subtract = region_subtract,
};

/* wl_compositor */

static void comp_create_surface(struct wl_client *client,
                                struct wl_resource *resource,
                                uint32_t id)
{
    (void)resource;
    pid_t client_pid = 0;
    uid_t client_uid = 0;
    gid_t client_gid = 0;

    struct wlcomp_surface *surf = calloc(1, sizeof(*surf));
    if (!surf) {
        wl_client_post_no_memory(client);
        return;
    }

    struct wl_resource *res = wl_resource_create(
        client, &wl_surface_interface, wl_resource_get_version(resource), id);
    if (!res) {
        free(surf);
        wl_client_post_no_memory(client);
        return;
    }

    surf->resource = res;
    surf->pending_acquire_fence_fd = -1;
    wl_list_init(&surf->pending_frame_callbacks);
    wl_list_init(&surf->frame_callbacks);
    wl_list_init(&surf->staged_frame_callbacks);
    wl_client_get_credentials(client, &client_pid, &client_uid, &client_gid);
    surf->client_pid = client_pid;
    wl_resource_set_implementation(res, &surface_impl, surf,
                                  surface_destroy_handler);
    wl_list_insert(g_surfaces.prev, &surf->link);
    surface_send_output_enter(surf);
    fprintf(stderr, "wlcomp: created surface pid=%d id=%u\n",
            client_pid, id);
}

static void comp_create_region(struct wl_client *client,
                               struct wl_resource *resource,
                               uint32_t id)
{
    (void)resource;
    struct wl_resource *res = wl_resource_create(
        client, &wl_region_interface, 1, id);
    if (!res) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(res, &region_impl, NULL, NULL);
}

static const struct wl_compositor_interface compositor_impl = {
    .create_surface = comp_create_surface,
    .create_region  = comp_create_region,
};

static void compositor_bind(struct wl_client *client, void *data,
                            uint32_t version, uint32_t id)
{
    (void)data;
    struct wl_resource *res = wl_resource_create(
        client, &wl_compositor_interface, version, id);
    if (!res) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(res, &compositor_impl, NULL, NULL);
}

/* wl_subcompositor / wl_subsurface
 *
 * WebKitGTK embeds page content in child Wayland surfaces.  wlcomp used to
 * advertise only top-level surfaces, so those child buffers could be committed
 * but never mapped or composited.
 */

static void subsurface_destroy(struct wl_client *client,
                               struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void subsurface_set_position(struct wl_client *client,
                                    struct wl_resource *resource,
                                    int32_t x, int32_t y)
{
    (void)client;
    struct wlcomp_surface *surf = surface_from_resource(resource);

    if (!surf)
        return;
    damage_surface(surf);
    surf->sub_x = x;
    surf->sub_y = y;
    damage_surface(surf);
}

static void subsurface_place_above(struct wl_client *client,
                                   struct wl_resource *resource,
                                   struct wl_resource *sibling_resource)
{
    (void)client;
    struct wlcomp_surface *surf = surface_from_resource(resource);
    struct wlcomp_surface *sibling = surface_from_resource(sibling_resource);

    if (!surf || !sibling)
        return;
    wl_list_remove(&surf->link);
    wl_list_insert(&sibling->link, &surf->link);
    damage_surface(surf);
}

static void subsurface_place_below(struct wl_client *client,
                                   struct wl_resource *resource,
                                   struct wl_resource *sibling_resource)
{
    (void)client;
    struct wlcomp_surface *surf = surface_from_resource(resource);
    struct wlcomp_surface *sibling = surface_from_resource(sibling_resource);

    if (!surf || !sibling)
        return;
    wl_list_remove(&surf->link);
    wl_list_insert(sibling->link.prev, &surf->link);
    damage_surface(surf);
}

static void subsurface_set_sync(struct wl_client *client,
                                struct wl_resource *resource)
{
    (void)client;
    (void)resource;
}

static void subsurface_set_desync(struct wl_client *client,
                                  struct wl_resource *resource)
{
    (void)client;
    (void)resource;
}

static const struct wl_subsurface_interface subsurface_impl = {
    .destroy = subsurface_destroy,
    .set_position = subsurface_set_position,
    .place_above = subsurface_place_above,
    .place_below = subsurface_place_below,
    .set_sync = subsurface_set_sync,
    .set_desync = subsurface_set_desync,
};

static void subsurface_destroy_handler(struct wl_resource *resource)
{
    struct wlcomp_surface *surf = surface_from_resource(resource);

    if (!surf)
        return;
    damage_surface(surf);
    surf->subsurface = NULL;
    surf->is_subsurface = 0;
    surf->parent = NULL;
    surf->mapped = 0;
    damage_full_reason(FULL_DAMAGE_CLIENT);
}

static void subcompositor_destroy(struct wl_client *client,
                                  struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void subcompositor_get_subsurface(struct wl_client *client,
                                         struct wl_resource *resource,
                                         uint32_t id,
                                         struct wl_resource *surface_resource,
                                         struct wl_resource *parent_resource)
{
    struct wlcomp_surface *surf = surface_from_resource(surface_resource);
    struct wlcomp_surface *parent = surface_from_resource(parent_resource);
    struct wl_resource *sub;

    if (!surf || !parent || surf == parent || surf->subsurface) {
        wl_resource_post_error(resource, 0, "invalid subsurface request");
        return;
    }

    sub = wl_resource_create(client, &wl_subsurface_interface, 1, id);
    if (!sub) {
        wl_client_post_no_memory(client);
        return;
    }

    surf->is_subsurface = 1;
    surf->parent = parent;
    surf->subsurface = sub;
    surf->sub_x = 0;
    surf->sub_y = 0;
    wl_resource_set_implementation(sub, &subsurface_impl, surf,
                                   subsurface_destroy_handler);
    fprintf(stderr, "wlcomp: assigned subsurface pid=%d parent_pid=%d\n",
            surf->client_pid, parent->client_pid);
}

static const struct wl_subcompositor_interface subcompositor_impl = {
    .destroy = subcompositor_destroy,
    .get_subsurface = subcompositor_get_subsurface,
};

static void subcompositor_bind(struct wl_client *client, void *data,
                               uint32_t version, uint32_t id)
{
    (void)data;
    struct wl_resource *res = wl_resource_create(
        client, &wl_subcompositor_interface, version, id);
    if (!res) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(res, &subcompositor_impl, NULL, NULL);
}
