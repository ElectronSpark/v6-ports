/* ── xv6_gpu_buffer_manager ──────────────────────────────────────── */

static const struct wl_interface *xv6_gpu_buffer_create_types[] = {
    &wl_buffer_interface,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
};

static const struct wl_message xv6_gpu_buffer_manager_requests[] = {
    { "create_buffer", "nuiiiu", xv6_gpu_buffer_create_types },
    { "create_buffer_with_fence", "nuiiiuh", xv6_gpu_buffer_create_types },
};

static const struct wl_interface xv6_gpu_buffer_manager_interface = {
    "xv6_gpu_buffer_manager",
    2,
    2,
    xv6_gpu_buffer_manager_requests,
    0,
    NULL,
};

struct xv6_gpu_buffer_manager_impl {
    void (*create_buffer)(struct wl_client *client,
                          struct wl_resource *resource,
                          uint32_t id,
                          uint32_t handle,
                          int32_t width,
                          int32_t height,
                          int32_t stride,
                          uint32_t format);
    void (*create_buffer_with_fence)(struct wl_client *client,
                                     struct wl_resource *resource,
                                     uint32_t id,
                                     uint32_t handle,
                                     int32_t width,
                                     int32_t height,
                                     int32_t stride,
                                     uint32_t format,
                                     int32_t acquire_fence_fd);
};

static void xv6_gpu_create_buffer_common(struct wl_client *client,
                                         struct wl_resource *resource,
                                         uint32_t id,
                                         uint32_t handle,
                                         int32_t width,
                                         int32_t height,
                                         int32_t stride,
                                         uint32_t format,
                                         int32_t acquire_fence_fd)
{
    struct fb_gpu_bo_import import;
    struct wlcomp_buffer *buf;
    struct wl_resource *buf_res;

    if (handle == 0 || width <= 0 || height <= 0 || stride <= 0 ||
        (format != WL_SHM_FORMAT_ARGB8888 &&
         format != WL_SHM_FORMAT_XRGB8888)) {
        if (acquire_fence_fd >= 0)
            close(acquire_fence_fd);
        wl_resource_post_error(resource, 0, "invalid xv6 GPU buffer");
        return;
    }

    memset(&import, 0, sizeof(import));
    import.handle = handle;
    if (ioctl(g_fb_fd, FB_GPU_BO_IMPORT, &import) < 0) {
        if (acquire_fence_fd >= 0)
            close(acquire_fence_fd);
        wl_resource_post_error(resource, 0, "FB_GPU_BO_IMPORT failed");
        return;
    }
    if ((int32_t)import.width != width ||
        (int32_t)import.height != height ||
        (int32_t)import.pitch != stride ||
        import.addr == 0 || import.size == 0) {
        munmap((void *)import.addr, (size_t)import.size);
        if (acquire_fence_fd >= 0)
            close(acquire_fence_fd);
        wl_resource_post_error(resource, 0, "xv6 GPU buffer metadata mismatch");
        return;
    }

    buf = calloc(1, sizeof(*buf));
    if (!buf) {
        munmap((void *)import.addr, (size_t)import.size);
        if (acquire_fence_fd >= 0)
            close(acquire_fence_fd);
        wl_resource_post_no_memory(resource);
        return;
    }
    buf->is_gpu_bo = 1;
    buf->gpu_addr = import.addr;
    buf->gpu_size = import.size;
    buf->gpu_handle = handle;
    buf->plane_gpu_addr[0] = import.addr;
    buf->plane_gpu_size[0] = import.size;
    buf->plane_gpu_handle[0] = handle;
    buf->acquire_fence_fd = acquire_fence_fd;
    buf->acquire_wait_start_ms = 0;
    buf->width = width;
    buf->height = height;
    buf->stride = stride;
    buf->plane_count = 1;
    buf->plane_stride[0] = stride;
    buf->plane_offset[0] = 0;
    buf->format = format;

    buf_res = wl_resource_create(client, &wl_buffer_interface, 1, id);
    if (!buf_res) {
        buffer_free(buf);
        wl_resource_post_no_memory(resource);
        return;
    }
    buf->resource = buf_res;
    wl_resource_set_implementation(buf_res, &buffer_impl, buf,
                                   buffer_destroy_handler);
    {
        pid_t client_pid = 0;
        uid_t client_uid = 0;
        gid_t client_gid = 0;
        wl_client_get_credentials(client, &client_pid, &client_uid,
                                  &client_gid);
        fprintf(stderr,
                "wlcomp: xv6gpu buffer pid=%d size=%dx%d stride=%d fmt=0x%x handle=%u\n",
                client_pid, width, height, stride, format, handle);
    }
}

static void xv6_gpu_create_buffer(struct wl_client *client,
                                  struct wl_resource *resource,
                                  uint32_t id,
                                  uint32_t handle,
                                  int32_t width,
                                  int32_t height,
                                  int32_t stride,
                                  uint32_t format)
{
    xv6_gpu_create_buffer_common(client, resource, id, handle, width, height,
                                 stride, format, -1);
}

static void xv6_gpu_create_buffer_with_fence(struct wl_client *client,
                                             struct wl_resource *resource,
                                             uint32_t id,
                                             uint32_t handle,
                                             int32_t width,
                                             int32_t height,
                                             int32_t stride,
                                             uint32_t format,
                                             int32_t acquire_fence_fd)
{
    xv6_gpu_create_buffer_common(client, resource, id, handle, width, height,
                                 stride, format, acquire_fence_fd);
}

static const struct xv6_gpu_buffer_manager_impl xv6_gpu_buffer_manager_impl = {
    .create_buffer = xv6_gpu_create_buffer,
    .create_buffer_with_fence = xv6_gpu_create_buffer_with_fence,
};

static void xv6_gpu_bind(struct wl_client *client, void *data,
                         uint32_t version, uint32_t id)
{
    struct wl_resource *res;

    (void)data;
    if (version > 2)
        version = 2;
    res = wl_resource_create(client, &xv6_gpu_buffer_manager_interface,
                             version, id);
    if (!res) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(res, &xv6_gpu_buffer_manager_impl, NULL,
                                   NULL);
}

/* ── zwp_linux_explicit_synchronization_v1 ───────────────────────── */

static void explicit_sync_destroy(struct wl_client *client,
                                  struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void surface_sync_destroy(struct wl_client *client,
                                 struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void surface_sync_set_acquire_fence(struct wl_client *client,
                                           struct wl_resource *resource,
                                           int32_t fd)
{
    struct wlcomp_surface *surf = wl_resource_get_user_data(resource);

    (void)client;
    if (!surf || !surf->resource) {
        if (fd >= 0)
            close(fd);
        wl_resource_post_error(
            resource, ZWP_LINUX_SURFACE_SYNCHRONIZATION_V1_ERROR_NO_SURFACE,
            "surface was destroyed");
        return;
    }
    if (surf->has_pending_acquire_fence) {
        if (fd >= 0)
            close(fd);
        wl_resource_post_error(
            resource,
            ZWP_LINUX_SURFACE_SYNCHRONIZATION_V1_ERROR_DUPLICATE_FENCE,
            "duplicate acquire fence");
        return;
    }
    surf->pending_acquire_fence_fd = fd;
    surf->has_pending_acquire_fence = 1;
}

static void surface_sync_get_release(struct wl_client *client,
                                     struct wl_resource *resource,
                                     uint32_t id)
{
    struct wlcomp_surface *surf = wl_resource_get_user_data(resource);
    struct wl_resource *release;

    if (!surf || !surf->resource) {
        wl_resource_post_error(
            resource, ZWP_LINUX_SURFACE_SYNCHRONIZATION_V1_ERROR_NO_SURFACE,
            "surface was destroyed");
        return;
    }
    if (surf->pending_release) {
        wl_resource_post_error(
            resource,
            ZWP_LINUX_SURFACE_SYNCHRONIZATION_V1_ERROR_DUPLICATE_RELEASE,
            "duplicate release request");
        return;
    }
    release = wl_resource_create(client, &zwp_linux_buffer_release_v1_interface,
                                 1, id);
    if (!release) {
        wl_resource_post_no_memory(resource);
        return;
    }
    surf->pending_release = release;
}

static void surface_sync_destroy_handler(struct wl_resource *resource)
{
    struct wlcomp_surface *surf = wl_resource_get_user_data(resource);

    if (!surf)
        return;
    if (surf->pending_acquire_fence_fd >= 0) {
        close(surf->pending_acquire_fence_fd);
        surf->pending_acquire_fence_fd = -1;
    }
    surf->has_pending_acquire_fence = 0;
    surf->sync_resource = NULL;
}

static const struct zwp_linux_surface_synchronization_v1_interface
surface_sync_impl = {
    .destroy = surface_sync_destroy,
    .set_acquire_fence = surface_sync_set_acquire_fence,
    .get_release = surface_sync_get_release,
};

static void explicit_sync_get_synchronization(struct wl_client *client,
                                              struct wl_resource *resource,
                                              uint32_t id,
                                              struct wl_resource *surface)
{
    struct wlcomp_surface *surf = surface_from_resource(surface);
    struct wl_resource *sync;

    if (!surf || !surf->resource) {
        wl_resource_post_error(resource,
                               ZWP_LINUX_SURFACE_SYNCHRONIZATION_V1_ERROR_NO_SURFACE,
                               "surface was destroyed");
        return;
    }
    if (surf->sync_resource) {
        wl_resource_post_error(
            resource,
            ZWP_LINUX_EXPLICIT_SYNCHRONIZATION_V1_ERROR_SYNCHRONIZATION_EXISTS,
            "surface already has explicit synchronization");
        return;
    }
    sync = wl_resource_create(client,
                              &zwp_linux_surface_synchronization_v1_interface,
                              2, id);
    if (!sync) {
        wl_resource_post_no_memory(resource);
        return;
    }
    surf->sync_resource = sync;
    wl_resource_set_implementation(sync, &surface_sync_impl, surf,
                                   surface_sync_destroy_handler);
}

static const struct zwp_linux_explicit_synchronization_v1_interface
explicit_sync_impl = {
    .destroy = explicit_sync_destroy,
    .get_synchronization = explicit_sync_get_synchronization,
};

static void explicit_sync_bind(struct wl_client *client, void *data,
                               uint32_t version, uint32_t id)
{
    struct wl_resource *res;

    (void)data;
    if (version > 2)
        version = 2;
    res = wl_resource_create(client,
                             &zwp_linux_explicit_synchronization_v1_interface,
                             version, id);
    if (!res) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(res, &explicit_sync_impl, NULL, NULL);
}

