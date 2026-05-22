/* ── zwp_linux_dmabuf_v1 ─────────────────────────────────────────── */

struct dmabuf_params {
    int fd[4];
    uint32_t plane_mask;
    int used;
    uint32_t offset[4];
    uint32_t stride[4];
    uint64_t modifier[4];
};

static uint32_t dmabuf_plane_count(uint32_t format)
{
    if (format == DRM_FORMAT_NV12)
        return 2;
    if (format == DRM_FORMAT_ARGB8888 || format == DRM_FORMAT_XRGB8888)
        return 1;
    return 0;
}

static uint32_t dmabuf_format_to_wl(uint32_t format)
{
    if (format == DRM_FORMAT_ARGB8888 || format == DRM_FORMAT_XRGB8888 ||
        format == DRM_FORMAT_NV12)
        return format;
    return WLCOMP_INVALID_FORMAT;
}

static void dmabuf_params_free(struct dmabuf_params *params)
{
    if (!params)
        return;
    for (uint32_t i = 0; i < 4; i++) {
        if (params->fd[i] >= 0)
            close(params->fd[i]);
    }
    free(params);
}

static void dmabuf_buffer_import_common(struct wl_client *client,
                                        struct wl_resource *params_res,
                                        uint32_t buffer_id,
                                        int32_t width,
                                        int32_t height,
                                        uint32_t format,
                                        uint32_t flags,
                                        int immediate)
{
    struct dmabuf_params *params = wl_resource_get_user_data(params_res);
    struct fb_gpu_bo_import_fd imports[4];
    struct wlcomp_buffer *buf;
    struct wl_resource *buf_res;
    uint32_t wl_format;
    uint32_t plane_count;
    uint32_t imported_count = 0;
    pid_t client_pid = 0;
    uid_t client_uid = 0;
    gid_t client_gid = 0;

    if (!params || params->used) {
        wl_resource_post_error(params_res,
                               ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_ALREADY_USED,
                               "linux-dmabuf params already used");
        return;
    }
    params->used = 1;

    plane_count = dmabuf_plane_count(format);
    if (plane_count == 0 ||
        (params->plane_mask & ((1u << plane_count) - 1u)) !=
            ((1u << plane_count) - 1u)) {
        wl_resource_post_error(params_res,
                               ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE,
                               "missing required dmabuf planes");
        return;
    }
    if (width <= 0 || height <= 0) {
        wl_resource_post_error(params_res,
                               ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_DIMENSIONS,
                               "invalid dmabuf dimensions");
        return;
    }
    wl_format = dmabuf_format_to_wl(format);
    if (wl_format == WLCOMP_INVALID_FORMAT ||
        (flags & ~ZWP_LINUX_BUFFER_PARAMS_V1_FLAGS_Y_INVERT) != 0) {
        wl_client_get_credentials(client, &client_pid, &client_uid,
                                  &client_gid);
        fprintf(stderr,
                "wlcomp: unsupported dmabuf pid=%d fmt=0x%x modifier=0x%lx flags=0x%x\n",
                client_pid, format, (unsigned long)params->modifier[0], flags);
        wl_resource_post_error(params_res,
                               ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT,
                               "unsupported dmabuf format/modifier/flags");
        return;
    }
    for (uint32_t i = 0; i < plane_count; i++) {
        if (params->modifier[i] != DRM_FORMAT_MOD_LINEAR) {
            wl_resource_post_error(params_res,
                                   ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT,
                                   "unsupported dmabuf modifier");
            return;
        }
    }
    if ((format == DRM_FORMAT_ARGB8888 || format == DRM_FORMAT_XRGB8888) &&
        (params->offset[0] != 0 || params->stride[0] < (uint32_t)width * 4)) {
        wl_resource_post_error(params_res,
                               ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_OUT_OF_BOUNDS,
                               "unsupported dmabuf offset/stride");
        return;
    }
    if (format == DRM_FORMAT_NV12 &&
        (params->offset[0] != 0 ||
         params->stride[0] < (uint32_t)width ||
         params->stride[1] < (uint32_t)width ||
         params->offset[1] < params->stride[0] * (uint32_t)height)) {
        wl_resource_post_error(params_res,
                               ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_OUT_OF_BOUNDS,
                               "unsupported NV12 dmabuf plane layout");
        return;
    }

    memset(imports, 0, sizeof(imports));
    for (uint32_t i = 0; i < plane_count; i++) {
        uint32_t rows = (format == DRM_FORMAT_NV12 && i == 1) ?
                        (uint32_t)((height + 1) / 2) : (uint32_t)height;
        uint64_t min_size = (uint64_t)params->offset[i] +
                            (uint64_t)params->stride[i] * rows;

        imports[i].fd = params->fd[i];
        imports[i].format = format;
        imports[i].plane_count = plane_count;
        imports[i].modifier = params->modifier[0];
        for (uint32_t p = 0; p < plane_count; p++) {
            imports[i].offsets[p] = params->offset[p];
            imports[i].strides[p] = params->stride[p];
        }
        if (ioctl(g_fb_fd, FB_GPU_BO_IMPORT_FD, &imports[i]) < 0 ||
            imports[i].addr == 0 || imports[i].size == 0 ||
            imports[i].handle == 0) {
            for (uint32_t j = 0; j < imported_count; j++) {
                struct fb_gpu_bo_destroy destroy = {
                    imports[j].handle, 0
                };

                munmap((void *)imports[j].addr, (size_t)imports[j].size);
                (void)ioctl(g_fb_fd, FB_GPU_BO_DESTROY, &destroy);
            }
            if (immediate) {
                wl_resource_post_error(params_res,
                                       ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_WL_BUFFER,
                                       "FB_GPU_BO_IMPORT_FD failed");
            } else {
                zwp_linux_buffer_params_v1_send_failed(params_res);
            }
            return;
        }
        imported_count++;
        if (imports[i].format != format ||
            imports[i].plane_count != plane_count ||
            imports[i].modifier != params->modifier[0]) {
            for (uint32_t j = 0; j < imported_count; j++) {
                struct fb_gpu_bo_destroy destroy = {
                    imports[j].handle, 0
                };

                munmap((void *)imports[j].addr, (size_t)imports[j].size);
                (void)ioctl(g_fb_fd, FB_GPU_BO_DESTROY, &destroy);
            }
            wl_resource_post_error(params_res,
                                   ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT,
                                   "dmabuf kernel metadata mismatch");
            return;
        }
        for (uint32_t p = 0; p < plane_count; p++) {
            if (imports[i].offsets[p] != params->offset[p] ||
                imports[i].strides[p] != params->stride[p]) {
                for (uint32_t j = 0; j < imported_count; j++) {
                    struct fb_gpu_bo_destroy destroy = {
                        imports[j].handle, 0
                    };

                    munmap((void *)imports[j].addr,
                           (size_t)imports[j].size);
                    (void)ioctl(g_fb_fd, FB_GPU_BO_DESTROY, &destroy);
                }
                wl_resource_post_error(params_res,
                                       ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_OUT_OF_BOUNDS,
                                       "dmabuf kernel plane metadata mismatch");
                return;
            }
        }
        if ((int32_t)imports[i].width != width ||
            (int32_t)imports[i].height != height ||
            (format != DRM_FORMAT_NV12 && i == 0 &&
             imports[i].pitch != params->stride[0]) ||
            imports[i].size < min_size) {
            for (uint32_t j = 0; j < imported_count; j++) {
                struct fb_gpu_bo_destroy destroy = {
                    imports[j].handle, 0
                };

                munmap((void *)imports[j].addr, (size_t)imports[j].size);
                (void)ioctl(g_fb_fd, FB_GPU_BO_DESTROY, &destroy);
            }
            wl_resource_post_error(params_res,
                                   ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_OUT_OF_BOUNDS,
                                   "dmabuf metadata mismatch");
            return;
        }
    }

    buf = calloc(1, sizeof(*buf));
    if (!buf) {
        for (uint32_t i = 0; i < imported_count; i++) {
            struct fb_gpu_bo_destroy destroy = { imports[i].handle, 0 };

            munmap((void *)imports[i].addr, (size_t)imports[i].size);
            (void)ioctl(g_fb_fd, FB_GPU_BO_DESTROY, &destroy);
        }
        wl_resource_post_no_memory(params_res);
        return;
    }
    buf->is_gpu_bo = 1;
    buf->gpu_addr = imports[0].addr;
    buf->gpu_size = imports[0].size;
    buf->gpu_handle = imports[0].handle;
    buf->owns_gpu_handle = 1;
    buf->acquire_fence_fd = -1;
    buf->width = width;
    buf->height = height;
    buf->stride = params->stride[0];
    buf->plane_count = plane_count;
    for (uint32_t i = 0; i < plane_count; i++) {
        buf->plane_stride[i] = (int32_t)params->stride[i];
        buf->plane_offset[i] = (int32_t)params->offset[i];
        buf->plane_gpu_addr[i] = imports[i].addr;
        buf->plane_gpu_size[i] = imports[i].size;
        buf->plane_gpu_handle[i] = imports[i].handle;
    }
    buf->format = wl_format;
    buf->y_inverted = (flags & ZWP_LINUX_BUFFER_PARAMS_V1_FLAGS_Y_INVERT) != 0;

    buf_res = wl_resource_create(client, &wl_buffer_interface, 1, buffer_id);
    if (!buf_res) {
        buffer_free(buf);
        wl_resource_post_no_memory(params_res);
        return;
    }
    buf->resource = buf_res;
    wl_resource_set_implementation(buf_res, &buffer_impl, buf,
                                   buffer_destroy_handler);
    wl_client_get_credentials(client, &client_pid, &client_uid, &client_gid);
    fprintf(stderr,
            "wlcomp: dmabuf buffer pid=%d size=%dx%d stride=%u fmt=0x%x planes=%u flags=0x%x\n",
            client_pid, width, height, params->stride[0], format,
            plane_count, flags);
    if (!immediate)
        zwp_linux_buffer_params_v1_send_created(params_res, buf_res);
}

static void dmabuf_params_destroy(struct wl_client *client,
                                  struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void dmabuf_params_add(struct wl_client *client,
                              struct wl_resource *resource,
                              int32_t fd,
                              uint32_t plane_idx,
                              uint32_t offset,
                              uint32_t stride,
                              uint32_t modifier_hi,
                              uint32_t modifier_lo)
{
    struct dmabuf_params *params = wl_resource_get_user_data(resource);
    pid_t client_pid = 0;
    uid_t client_uid = 0;
    gid_t client_gid = 0;

    (void)client;
    wl_client_get_credentials(client, &client_pid, &client_uid, &client_gid);
    fprintf(stderr,
            "wlcomp: dmabuf add pid=%d plane=%u stride=%u modifier=%08x:%08x fd=%d\n",
            client_pid, plane_idx, stride, modifier_hi, modifier_lo, fd);
    if (!params || params->used) {
        if (fd >= 0)
            close(fd);
        wl_resource_post_error(resource,
                               ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_ALREADY_USED,
                               "linux-dmabuf params already used");
        return;
    }
    if (plane_idx >= 4) {
        if (fd >= 0)
            close(fd);
        wl_resource_post_error(resource,
                               ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_IDX,
                               "dmabuf plane index out of range");
        return;
    }
    if (params->plane_mask & (1u << plane_idx)) {
        if (fd >= 0)
            close(fd);
        wl_resource_post_error(resource,
                               ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_SET,
                               "dmabuf plane already set");
        return;
    }

    params->fd[plane_idx] = fd;
    params->plane_mask |= 1u << plane_idx;
    params->offset[plane_idx] = offset;
    params->stride[plane_idx] = stride;
    params->modifier[plane_idx] = ((uint64_t)modifier_hi << 32) | modifier_lo;
}

static void dmabuf_params_create(struct wl_client *client,
                                 struct wl_resource *resource,
                                 int32_t width,
                                 int32_t height,
                                 uint32_t format,
                                 uint32_t flags)
{
    dmabuf_buffer_import_common(client, resource, 0, width, height, format,
                                flags, 0);
}

static void dmabuf_params_create_immed(struct wl_client *client,
                                       struct wl_resource *resource,
                                       uint32_t buffer_id,
                                       int32_t width,
                                       int32_t height,
                                       uint32_t format,
                                       uint32_t flags)
{
    dmabuf_buffer_import_common(client, resource, buffer_id, width, height,
                                format, flags, 1);
}

static const struct zwp_linux_buffer_params_v1_interface dmabuf_params_impl = {
    .destroy = dmabuf_params_destroy,
    .add = dmabuf_params_add,
    .create = dmabuf_params_create,
    .create_immed = dmabuf_params_create_immed,
};

static void dmabuf_params_destroy_handler(struct wl_resource *resource)
{
    struct dmabuf_params *params = wl_resource_get_user_data(resource);

    dmabuf_params_free(params);
}

struct dmabuf_feedback_entry {
    uint32_t format;
    uint32_t padding;
    uint64_t modifier;
};

static void dmabuf_feedback_destroy(struct wl_client *client,
                                    struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static const struct zwp_linux_dmabuf_feedback_v1_interface
dmabuf_feedback_impl = {
    .destroy = dmabuf_feedback_destroy,
};

static int dmabuf_feedback_table_fd(uint32_t *size_out)
{
    static const struct dmabuf_feedback_entry entries[] = {
        { DRM_FORMAT_ARGB8888, 0, DRM_FORMAT_MOD_LINEAR },
        { DRM_FORMAT_XRGB8888, 0, DRM_FORMAT_MOD_LINEAR },
        { DRM_FORMAT_NV12, 0, DRM_FORMAT_MOD_LINEAR },
    };
    char path[] = "/tmp/wlcomp-dmabuf-feedback.XXXXXX";
    int fd;
    size_t off = 0;

    fd = mkstemp(path);
    if (fd < 0)
        return -1;
    unlink(path);

    while (off < sizeof(entries)) {
        ssize_t n = write(fd, (const char *)entries + off,
                          sizeof(entries) - off);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            close(fd);
            return -1;
        }
        if (n == 0) {
            close(fd);
            errno = EIO;
            return -1;
        }
        off += (size_t)n;
    }
    lseek(fd, 0, SEEK_SET);
    *size_out = (uint32_t)sizeof(entries);
    return fd;
}

static void dmabuf_feedback_device_array(struct wl_array *device)
{
    struct stat st;
    dev_t dev;
    dev_t *slot;

    wl_array_init(device);
    if (stat("/dev/dri/renderD128", &st) == 0)
        dev = st.st_rdev;
    else
        dev = ((dev_t)226 << 20) | 128;

    slot = wl_array_add(device, sizeof(dev));
    if (slot)
        *slot = dev;
}

static int dmabuf_feedback_send(struct wl_resource *feedback)
{
    struct wl_array device;
    struct wl_array indices;
    uint16_t *idx;
    uint32_t table_size = 0;
    int table_fd;

    dmabuf_feedback_device_array(&device);
    wl_array_init(&indices);
    idx = wl_array_add(&indices, sizeof(uint16_t) * 3);
    if (!idx || device.size == 0) {
        wl_array_release(&indices);
        wl_array_release(&device);
        errno = ENOMEM;
        return -1;
    }
    idx[0] = 0;
    idx[1] = 1;
    idx[2] = 2;

    table_fd = dmabuf_feedback_table_fd(&table_size);
    if (table_fd < 0) {
        wl_array_release(&indices);
        wl_array_release(&device);
        return -1;
    }

    zwp_linux_dmabuf_feedback_v1_send_format_table(feedback, table_fd,
                                                   table_size);
    close(table_fd);
    zwp_linux_dmabuf_feedback_v1_send_main_device(feedback, &device);
    zwp_linux_dmabuf_feedback_v1_send_tranche_target_device(feedback, &device);
    zwp_linux_dmabuf_feedback_v1_send_tranche_flags(feedback, 0);
    zwp_linux_dmabuf_feedback_v1_send_tranche_formats(feedback, &indices);
    zwp_linux_dmabuf_feedback_v1_send_tranche_done(feedback);
    zwp_linux_dmabuf_feedback_v1_send_done(feedback);

    wl_array_release(&indices);
    wl_array_release(&device);
    return 0;
}

static void dmabuf_destroy(struct wl_client *client,
                           struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void dmabuf_create_params(struct wl_client *client,
                                 struct wl_resource *resource,
                                 uint32_t params_id)
{
    struct dmabuf_params *params;
    struct wl_resource *params_res;

    params = calloc(1, sizeof(*params));
    if (!params) {
        wl_resource_post_no_memory(resource);
        return;
    }
    for (uint32_t i = 0; i < 4; i++)
        params->fd[i] = -1;

    params_res = wl_resource_create(client,
                                    &zwp_linux_buffer_params_v1_interface,
                                    wl_resource_get_version(resource),
                                    params_id);
    if (!params_res) {
        free(params);
        wl_resource_post_no_memory(resource);
        return;
    }
    wl_resource_set_implementation(params_res, &dmabuf_params_impl, params,
                                   dmabuf_params_destroy_handler);
    {
        pid_t client_pid = 0;
        uid_t client_uid = 0;
        gid_t client_gid = 0;
        wl_client_get_credentials(client, &client_pid, &client_uid,
                                  &client_gid);
        fprintf(stderr, "wlcomp: dmabuf create_params pid=%d id=%u\n",
                client_pid, params_id);
    }
}

static void dmabuf_get_default_feedback(struct wl_client *client,
                                        struct wl_resource *resource,
                                        uint32_t id)
{
    struct wl_resource *feedback;
    uint32_t version = wl_resource_get_version(resource);

    feedback = wl_resource_create(client, &zwp_linux_dmabuf_feedback_v1_interface,
                                  version, id);
    if (!feedback) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(feedback, &dmabuf_feedback_impl, NULL, NULL);
    if (dmabuf_feedback_send(feedback) < 0)
        wl_resource_post_no_memory(resource);
}

static void dmabuf_get_surface_feedback(struct wl_client *client,
                                        struct wl_resource *resource,
                                        uint32_t id,
                                        struct wl_resource *surface)
{
    (void)surface;
    dmabuf_get_default_feedback(client, resource, id);
}

static const struct zwp_linux_dmabuf_v1_interface dmabuf_impl = {
    .destroy = dmabuf_destroy,
    .create_params = dmabuf_create_params,
    .get_default_feedback = dmabuf_get_default_feedback,
    .get_surface_feedback = dmabuf_get_surface_feedback,
};

static void dmabuf_bind(struct wl_client *client, void *data,
                        uint32_t version, uint32_t id)
{
    struct wl_resource *res;

    (void)data;
    if (version > 4)
        version = 4;
    res = wl_resource_create(client, &zwp_linux_dmabuf_v1_interface,
                             version, id);
    if (!res) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(res, &dmabuf_impl, NULL, NULL);
    zwp_linux_dmabuf_v1_send_format(res, DRM_FORMAT_ARGB8888);
    zwp_linux_dmabuf_v1_send_format(res, DRM_FORMAT_XRGB8888);
    zwp_linux_dmabuf_v1_send_format(res, DRM_FORMAT_NV12);
    if (version >= 3) {
        zwp_linux_dmabuf_v1_send_modifier(res, DRM_FORMAT_ARGB8888, 0,
                                          DRM_FORMAT_MOD_LINEAR);
        zwp_linux_dmabuf_v1_send_modifier(res, DRM_FORMAT_XRGB8888, 0,
                                          DRM_FORMAT_MOD_LINEAR);
        zwp_linux_dmabuf_v1_send_modifier(res, DRM_FORMAT_NV12, 0,
                                          DRM_FORMAT_MOD_LINEAR);
    }
}
