#include <gtk/gtk.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/ioctl.h>
#include <unistd.h>

typedef struct _WebKitSettings WebKitSettings;
typedef struct _WebKitWebView WebKitWebView;

typedef enum {
    WEBKIT_HARDWARE_ACCELERATION_POLICY_ON_DEMAND,
    WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS,
    WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER
} WebKitHardwareAccelerationPolicy;

typedef enum {
    WEBKIT_LOAD_STARTED,
    WEBKIT_LOAD_REDIRECTED,
    WEBKIT_LOAD_COMMITTED,
    WEBKIT_LOAD_FINISHED
} WebKitLoadEvent;

extern WebKitSettings *webkit_settings_new(void);
extern void webkit_settings_set_enable_developer_extras(WebKitSettings *, gboolean);
extern void webkit_settings_set_enable_webgl(WebKitSettings *, gboolean);
extern void webkit_settings_set_hardware_acceleration_policy(WebKitSettings *,
                                                             WebKitHardwareAccelerationPolicy);
extern GtkWidget *webkit_web_view_new(void);
extern WebKitSettings *webkit_web_view_get_settings(WebKitWebView *);
extern void webkit_web_view_load_html(WebKitWebView *, const gchar *, const gchar *);
extern void webkit_web_view_load_uri(WebKitWebView *, const gchar *);

#define WEBKIT_WEB_VIEW(obj) ((WebKitWebView *)(obj))
#define XV6_DRM_RENDER_NODE "/dev/dri/renderD128"
#define XV6_GPU_CONTROL_NODE "/dev/gpu0"
#define XV6_FB_CONTROL_NODE "/dev/fb0"
#define XV6_D3D12_PRESENT_EVIDENCE_PATH "/tmp/wlcomp-d3d12-present"
#define XV6_D3D12_PRESENT_EVIDENCE_MAX_AGE_SEC 120
#define FB_GPU_BACKEND_QUERY 0x462C
#define FB_GPU_BACKEND_VIRGL 1
#define FB_GPU_BACKEND_HYPERV_DXG 2
#define FB_GPU_BACKEND_F_RENDER_NODE 0x0001
#define FB_GPU_BACKEND_F_VIRGL_OPENGL 0x0004
#define FB_GPU_BACKEND_F_DXG_TRANSPORT 0x0008
#define FB_GPU_BACKEND_F_D3DKMT 0x0010
#define FB_GPU_BACKEND_F_OPENGL_SUBMIT 0x0020

struct fb_gpu_backend_info_compat {
    uint32_t backend;
    uint32_t flags;
    uint32_t capset_id;
    uint32_t capset_version;
    uint32_t capset_size;
    uint32_t dxg_global_open;
    uint32_t dxg_vgpu_open;
    uint32_t dxg_d3dkmt;
    uint32_t dxg_global_status;
    uint32_t dxg_vgpu_status;
    uint32_t dxg_global_rx;
    uint32_t dxg_vgpu_rx;
    char name[32];
    char renderer[64];
};

struct SmokeLoad {
    WebKitWebView *view;
    char *uri;
};

struct SmokeRuntime {
    const char *uri;
    int completion_seen;
};

static void phase(const char *message)
{
    fprintf(stderr, "webkitgpusmoke: %s\n", message);
    fflush(stderr);
}

static gboolean quit_cb(gpointer data)
{
    (void)data;
    gtk_main_quit();
    return G_SOURCE_REMOVE;
}

static void title_changed_cb(GObject *object, GParamSpec *pspec, gpointer data)
{
    gchar *title = NULL;
    struct SmokeRuntime *runtime = data;

    (void)pspec;
    g_object_get(object, "title", &title, NULL);
    if (title) {
        fprintf(stderr, "webkitgpusmoke: title=%s\n", title);
        fflush(stderr);
        if (runtime && strstr(title, "webgl spherical poly complete")) {
            runtime->completion_seen = 1;
            g_idle_add(quit_cb, NULL);
        } else if (runtime) {
            const char *animated =
                strstr(title, "native present animated content frame ");
            if (animated &&
                atoi(animated + strlen("native present animated content frame ")) >= 30) {
                runtime->completion_seen = 1;
                g_idle_add(quit_cb, NULL);
            }
        }
        g_free(title);
    }
}

static void load_changed_cb(WebKitWebView *view, WebKitLoadEvent event, gpointer data)
{
    (void)view;
    (void)data;
    fprintf(stderr, "webkitgpusmoke: load-event=%d\n", event);
    fflush(stderr);
}

static char *read_text_file(const char *uri)
{
    const char *path = uri;
    if (g_str_has_prefix(uri, "file://"))
        path = uri + 7;

    gchar *contents = NULL;
    gsize length = 0;
    GError *error = NULL;
    if (!g_file_get_contents(path, &contents, &length, &error)) {
        fprintf(stderr, "webkitgpusmoke: failed to read %s: %s\n",
                path, error ? error->message : "unknown error");
        g_clear_error(&error);
        return NULL;
    }

    (void)length;
    return contents;
}

static gboolean start_load_cb(gpointer data)
{
    struct SmokeLoad *load = data;
    char *html = read_text_file(load->uri);
    if (html) {
        webkit_web_view_load_html(load->view, html, "file:///share/webkit/");
        g_free(html);
    } else
        webkit_web_view_load_uri(load->view, load->uri);
    phase("uri load requested");
    g_object_unref(load->view);
    free(load->uri);
    free(load);
    return G_SOURCE_REMOVE;
}

static int env_enabled(const char *name)
{
    const char *value = getenv(name);
    return value && value[0] && strcmp(value, "0") != 0;
}

static int env_is(const char *name, const char *expected)
{
    const char *value = getenv(name);

    return value && strcmp(value, expected) == 0;
}

static int query_backend(struct fb_gpu_backend_info_compat *info)
{
    static const char *paths[] = {
        XV6_GPU_CONTROL_NODE,
        XV6_FB_CONTROL_NODE,
        XV6_DRM_RENDER_NODE,
    };
    int fd;
    size_t i;

    if (!info)
        return 0;
    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        memset(info, 0, sizeof(*info));
        fd = open(paths[i], O_RDONLY | O_CLOEXEC);
        if (fd < 0)
            continue;
        if (ioctl(fd, FB_GPU_BACKEND_QUERY, info) == 0) {
            close(fd);
            return 1;
        }
        close(fd);
    }
    memset(info, 0, sizeof(*info));
    return 0;
}

static const char *backend_name(const struct fb_gpu_backend_info_compat *info)
{
    if (!info)
        return "unknown";
    if (info->name[0])
        return info->name;
    switch (info->backend) {
    case FB_GPU_BACKEND_VIRGL:
        return "virgl";
    case FB_GPU_BACKEND_HYPERV_DXG:
        return "hyperv-dxg";
    default:
        return "unknown";
    }
}

static int policy_value(const char *line, const char *key, int fallback)
{
    size_t key_len = strlen(key);
    const char *p = line;

    while (p && *p) {
        p = strstr(p, key);
        if (!p)
            break;
        if ((p == line || p[-1] == ' ') && p[key_len] == '=')
            return atoi(p + key_len + 1);
        p += key_len;
    }
    return fallback;
}

static int evidence_key_u64(const char *text, const char *key, uint64_t *out)
{
    char needle[80];
    const char *p;
    char *end = NULL;

    if (!text || !key || !out)
        return 0;
    snprintf(needle, sizeof(needle), "%s=", key);
    p = strstr(text, needle);
    if (!p)
        return 0;
    p += strlen(needle);
    errno = 0;
    *out = strtoull(p, &end, 0);
    return errno == 0 && end != p;
}

static void evidence_key_u64_alias_max(const char *text, const char *key,
                                       uint64_t *out)
{
    uint64_t value = 0;

    if (evidence_key_u64(text, key, &value) && value > *out)
        *out = value;
}

static int evidence_key_string(const char *text, const char *key,
                               char *out, size_t out_size)
{
    char needle[80];
    const char *p;
    size_t n = 0;

    if (!text || !key || !out || out_size == 0)
        return 0;
    out[0] = '\0';
    snprintf(needle, sizeof(needle), "%s=", key);
    p = strstr(text, needle);
    if (!p)
        return 0;
    p += strlen(needle);
    while (p[n] && p[n] != '\n' && p[n] != '\r' && n + 1 < out_size)
        n++;
    memcpy(out, p, n);
    out[n] = '\0';
    return n != 0;
}

static int evidence_string_is(const char *text, const char *key,
                              const char *expected)
{
    char value[128];

    return evidence_key_string(text, key, value, sizeof(value)) &&
           strcmp(value, expected) == 0;
}

static int d3d12_present_evidence_fresh(void)
{
    struct stat st;
    time_t now;

    if (stat(XV6_D3D12_PRESENT_EVIDENCE_PATH, &st) != 0 ||
        st.st_size <= 0)
        return 0;
    now = time(NULL);
    if (now == (time_t)-1)
        return 1;
    if (st.st_mtime > now)
        return 1;
    return now - st.st_mtime <= XV6_D3D12_PRESENT_EVIDENCE_MAX_AGE_SEC;
}

static const char *last_policy_line_for(const char *text, const char *name)
{
    const char *line = text;
    const char *best = NULL;
    char needle[64];

    snprintf(needle, sizeof(needle), "name=%s ", name);
    while (line && *line) {
        const char *next = strchr(line, '\n');
        if (strstr(line, "webkit_gpu_policy") &&
            (strstr(line, needle) || !best))
            best = line;
        if (!next)
            break;
        line = next + 1;
    }
    return best;
}

static int validate_gpu_contract(void)
{
    struct fb_gpu_backend_info_compat info;
    int have_backend = query_backend(&info);
    int render_node = have_backend &&
        (info.flags & FB_GPU_BACKEND_F_RENDER_NODE) != 0;
    int virgl = have_backend && info.backend == FB_GPU_BACKEND_VIRGL &&
        (info.flags & FB_GPU_BACKEND_F_VIRGL_OPENGL) != 0;
    int dxg_transport = have_backend &&
        info.backend == FB_GPU_BACKEND_HYPERV_DXG &&
        (info.flags & FB_GPU_BACKEND_F_DXG_TRANSPORT) != 0;
    int d3dkmt = have_backend &&
        info.backend == FB_GPU_BACKEND_HYPERV_DXG &&
        (info.flags & FB_GPU_BACKEND_F_D3DKMT) != 0;
    int opengl_submit = have_backend &&
        (info.flags & FB_GPU_BACKEND_F_OPENGL_SUBMIT) != 0;
    char *evidence = d3d12_present_evidence_fresh() ?
        read_text_file(XV6_D3D12_PRESENT_EVIDENCE_PATH) : NULL;
    char source_luid[32] = { 0 };
    char matched_luid[32] = { 0 };
    char evidence_run_id[64] = { 0 };
    uint64_t evidence_resource = 0;
    uint64_t evidence_allocations = 0;
    uint64_t evidence_resource_import_successes = 0;
    uint64_t evidence_runtime_resource = 0;
    uint64_t evidence_present_source_registered = 0;
    uint64_t evidence_fence = 0;
    uint64_t evidence_fence_target = 0;
    uint64_t evidence_release_fence = 0;
    uint64_t evidence_fence_import_successes = 0;
    uint64_t evidence_time_us = 0;
    uint64_t evidence_generation = 0;
    uint64_t evidence_present_rejected = 1;
    uint64_t evidence_present_complete = 0;
    uint64_t evidence_dxg_present_id = 0;
    uint64_t evidence_dxg_completed = 0;
    uint64_t evidence_buffer_completion_correlated = 0;
    uint64_t evidence_display_handoff = 0;
    uint64_t evidence_native_requirements = 0;
    uint64_t evidence_identity_current = 0;
    uint64_t evidence_source_luid_valid = 0;
    uint64_t evidence_present_same_luid = 0;
    uint64_t evidence_native_present_attempt_id = 0;
    uint64_t evidence_native_present_completion_id = 0;
    uint64_t evidence_callback_release_same_frame = 0;
    uint64_t evidence_callback_release_same_frame_required = 0;
    uint64_t evidence_frame_callback = 0;
    uint64_t evidence_buffer_release = 0;
    uint64_t evidence_buffer_release_same_resource = 0;
    uint64_t evidence_buffer_release_same_generation = 0;
    uint64_t evidence_buffer_release_same_attempt = 0;
    uint64_t evidence_buffer_release_same_present_id = 0;
    uint64_t evidence_buffer_release_present_id = 0;
    uint64_t evidence_buffer_release_completion_id = 0;
    uint64_t evidence_frame_callback_same_resource = 0;
    uint64_t evidence_frame_callback_same_generation = 0;
    uint64_t evidence_frame_callback_same_attempt = 0;
    uint64_t evidence_frame_callback_same_present_id = 0;
    uint64_t evidence_frame_callback_present_id = 0;
    uint64_t evidence_frame_callback_completion_id = 0;
    uint64_t evidence_content_crc = 0;
    uint64_t evidence_content_frame = 0;
    uint64_t evidence_cpu_readback = 1;
    uint64_t evidence_cpu_mapping = 1;
    uint64_t evidence_cpu_copy = 1;
    uint64_t evidence_no_cpu_map_no_readback = 0;
    uint64_t evidence_final_no_cpu_map_no_readback = 0;
    uint64_t evidence_fb_blit_used = 1;
    uint64_t evidence_cpu_map_used = 1;
    uint64_t evidence_cpu_readback_used = 1;
    uint64_t evidence_cpu_copy_used = 1;
    uint64_t evidence_software_dri_used = 1;
    uint64_t evidence_software_dri_present = 1;
    uint64_t evidence_framebuffer_blit_only = 1;
    uint64_t evidence_copy_export = 0;
    uint64_t evidence_copy_export_fallback = 0;
    uint64_t evidence_native_present_unimplemented = 1;
    uint64_t evidence_present_state_only = 1;
    uint64_t evidence_present_fence_only = 1;
    uint64_t evidence_present_import_only = 1;
    uint64_t evidence_present_open_only = 1;
    uint64_t evidence_present_callback_only = 1;
    uint64_t evidence_present_release_only = 1;
    uint64_t evidence_present_errno = 1;
    uint64_t evidence_callbacks_blocked = 1;
    uint64_t evidence_releases_blocked = 1;
    uint64_t evidence_framebuffer_fallback_used = 1;
    uint64_t evidence_framebuffer_fallback_count = 1;
    uint64_t evidence_row_major_cpu_visible_target_used = 1;
    uint64_t evidence_readback_present_target_used = 1;
    uint64_t evidence_final_user_display_channel = 1;
    uint64_t evidence_final_existing_sysmem_allowed = 1;
    uint64_t evidence_final_synthvid_dirty_only_allowed = 1;
    uint64_t evidence_final_runtime_resource_required = 0;
    uint64_t evidence_final_runtime_resource_observed = 0;
    uint64_t evidence_final_kernel_abi_required = 0;
    uint64_t evidence_final_kernel_abi_missing = 1;
    uint64_t evidence_final_host_display_commit_success = 0;
    uint64_t evidence_final_present_id = 0;
    uint64_t evidence_final_completed = 0;
    uint64_t evidence_final_completion_correlated = 0;
    uint64_t evidence_final_release_fence = 0;
    uint64_t evidence_final_success = 0;
    uint64_t evidence_commit_expected_eopnotsupp = 1;
    int evidence_same_adapter = 0;
    int evidence_no_readback = 0;
    int evidence_shared_resource = 0;
    int evidence_fence_ok = 0;
    int evidence_terminal_success = 0;
    int evidence_native_completion = 0;
    int evidence_present_path_ok = 0;
    int evidence_identity_ok = 0;
    int evidence_callback_release_ok = 0;
    int evidence_content_progress = 0;
    int evidence_final_handoff_ok = 0;
    int evidence_fail_closed_rejected = 0;
    int evidence_soft_claim_rejected = 0;
    int evidence_ok = 0;
    int d3d12_present = 0;
    int shared_surface = 0;
    int env_d3d12 = env_is("GALLIUM_DRIVER", "d3d12") ||
        env_enabled("XV6_MESA_WAYLAND_XV6GPU") ||
        env_enabled("XV6_MESA_WAYLAND_INPLACE_PRESENT");
    int env_d3d12_driver = env_is("GALLIUM_DRIVER", "d3d12");
    int env_d3d12_loader = env_is("MESA_LOADER_DRIVER_OVERRIDE", "d3d12");
    int env_d3d12_xv6gpu = env_is("XV6_MESA_WAYLAND_XV6GPU", "1");
    int env_d3d12_inplace =
        env_is("XV6_MESA_WAYLAND_INPLACE_PRESENT", "1");
    int env_d3d12_throttle0 = env_is("XV6_MESA_WAYLAND_THROTTLE", "0");
    int env_d3d12_perf0 = env_is("XV6_MESA_PERF_LOG", "0");
    int env_d3d12_vblank0 = env_is("vblank_mode", "0");
    int env_egl_wayland = env_is("EGL_PLATFORM", "wayland");
    int env_libgl_dri = env_is("LIBGL_DRIVERS_PATH", "/lib/dri");
    int env_virgl = env_is("GALLIUM_DRIVER", "virgl");
    int env_software = env_is("LIBGL_ALWAYS_SOFTWARE", "1");
    int env_d3d12_native_present_enabled =
        env_is("XV6_D3D12_ENABLE_NATIVE_PRESENT", "1") ||
        env_is("XV6_D3D12_ENABLE_NATIVE_PRESENT", "true");
    int env_d3d12_native_present_required =
        env_is("XV6_D3D12_REQUIRE_NATIVE_PRESENT", "1") ||
        env_is("XV6_D3D12_REQUIRE_NATIVE_PRESENT", "true");
    int env_d3d12_native_present_disabled =
        env_is("XV6_D3D12_REQUIRE_NATIVE_PRESENT", "0");
    int env_d3d12_copy_export = env_enabled("XV6_D3D12_COPY_EXPORT");
    int env_accel = env_d3d12 || env_virgl ||
        env_is("LIBGL_ALWAYS_SOFTWARE", "0");
    const char *env_contract = getenv("WEBKIT_XV6_GPU_CONTRACT");
    int env_contract_d3d12 = env_contract &&
        strcmp(env_contract, "d3d12-shared-surface") == 0;
    int env_contract_virgl = env_contract &&
        strcmp(env_contract, "virgl-opengl-submit") == 0;
    const char *env_run_id = getenv("WEBKIT_XV6_GPU_RUN_ID");
    int env_run_id_present = env_run_id && env_run_id[0];
    int env_d3d12_run_id_required = env_d3d12 || env_contract_d3d12;
    int evidence_run_id_match = 0;
    int require = env_enabled("WEBKIT_XV6_REQUIRE_GPU_CONTRACT");
    int force_compositing =
        env_enabled("WEBKIT_XV6_FORCE_COMPOSITING_MODE");
    char *policy = read_text_file("/tmp/webkit-gpu-policy");
    const char *line = policy ? last_policy_line_for(policy, "webkitgpusmoke") : NULL;
    int policy_effective = line ? policy_value(line, "effective_accel", -1) : -1;
    int policy_opengl = line ? policy_value(line, "opengl_submit", -1) : -1;
    int policy_shared = line ? policy_value(line, "shared_surface", -1) : -1;
    int policy_d3d12 = line ? policy_value(line, "d3d12_present", -1) : -1;
    int policy_native_present = line ?
        policy_value(line, "d3d12_native_present_required", -1) : -1;
    int policy_contract_evidence = line ?
        policy_value(line, "d3d12_contract_evidence", -1) : -1;
    int policy_same_adapter = line ?
        policy_value(line, "d3d12_same_adapter", -1) : -1;
    int policy_no_readback = line ?
        policy_value(line, "d3d12_no_readback", -1) : -1;
    int policy_shared_resource = line ?
        policy_value(line, "d3d12_shared_resource", -1) : -1;
    int policy_fence = line ?
        policy_value(line, "d3d12_fence", -1) : -1;
    int policy_copy_export = line ?
        policy_value(line, "d3d12_copy_export", -1) : -1;
    int policy_readback = line ?
        policy_value(line, "d3d12_readback", -1) : -1;
    int ok = 1;
    char evidence_stage[64] = { 0 };

    if (evidence) {
        (void)evidence_key_string(evidence, "d3d12_evidence_stage",
                                  evidence_stage, sizeof(evidence_stage));
        (void)evidence_key_u64(evidence, "d3d12_present_evidence_time_us",
                               &evidence_time_us);
        (void)evidence_key_u64(evidence, "d3d12_evidence_generation",
                               &evidence_generation);
        (void)evidence_key_u64(evidence, "d3d12_present_rejected",
                               &evidence_present_rejected);
        (void)evidence_key_u64(evidence, "d3d12_present_resource",
                               &evidence_resource);
        (void)evidence_key_u64(evidence,
                               "d3d12_present_allocation_count",
                               &evidence_allocations);
        (void)evidence_key_u64(evidence, "d3d12_resource_import_successes",
                               &evidence_resource_import_successes);
        (void)evidence_key_u64(
            evidence, "d3d12_runtime_created_d3d12_resource_present",
            &evidence_runtime_resource);
        (void)evidence_key_u64(evidence, "d3d12_present_source_registered",
                               &evidence_present_source_registered);
        (void)evidence_key_u64(evidence, "d3d12_present_fence",
                               &evidence_fence);
        (void)evidence_key_u64(evidence, "d3d12_present_fence_target",
                               &evidence_fence_target);
        (void)evidence_key_u64(evidence, "d3d12_present_release_fence",
                               &evidence_release_fence);
        (void)evidence_key_u64(evidence, "d3d12_fence_import_successes",
                               &evidence_fence_import_successes);
        (void)evidence_key_u64(evidence, "d3d12_gpu_present_complete",
                               &evidence_present_complete);
        (void)evidence_key_u64(evidence, "d3d12_dxg_present_id",
                               &evidence_dxg_present_id);
        (void)evidence_key_u64(evidence, "d3d12_dxg_present_completed",
                               &evidence_dxg_completed);
        (void)evidence_key_u64(
            evidence, "d3d12_present_source_buffer_completion_correlated",
            &evidence_buffer_completion_correlated);
        (void)evidence_key_u64(evidence, "d3d12_display_handoff_implemented",
                               &evidence_display_handoff);
        (void)evidence_key_u64(
            evidence, "d3d12_native_present_requirements_satisfied",
            &evidence_native_requirements);
        (void)evidence_key_u64(evidence,
                               "d3d12_present_identity_current_run_valid",
                               &evidence_identity_current);
        (void)evidence_key_u64(evidence, "d3d12_present_source_luid_valid",
                               &evidence_source_luid_valid);
        (void)evidence_key_u64(evidence, "d3d12_present_same_luid",
                               &evidence_present_same_luid);
        (void)evidence_key_u64(evidence, "d3d12_native_present_attempt_id",
                               &evidence_native_present_attempt_id);
        (void)evidence_key_u64(evidence, "d3d12_native_present_completion_id",
                               &evidence_native_present_completion_id);
        (void)evidence_key_string(evidence, "d3d12_run_id",
                                  evidence_run_id,
                                  sizeof(evidence_run_id));
        (void)evidence_key_u64(evidence,
                               "d3d12_callback_release_same_frame_required",
                               &evidence_callback_release_same_frame_required);
        (void)evidence_key_u64(evidence,
                               "d3d12_callback_release_same_frame_observed",
                               &evidence_callback_release_same_frame);
        (void)evidence_key_u64(evidence, "d3d12_frame_callback_observed",
                               &evidence_frame_callback);
        (void)evidence_key_u64(evidence, "d3d12_buffer_release_observed",
                               &evidence_buffer_release);
        (void)evidence_key_u64(evidence, "d3d12_buffer_release_same_resource",
                               &evidence_buffer_release_same_resource);
        (void)evidence_key_u64(evidence,
                               "d3d12_buffer_release_same_generation",
                               &evidence_buffer_release_same_generation);
        (void)evidence_key_u64(evidence, "d3d12_buffer_release_same_attempt",
                               &evidence_buffer_release_same_attempt);
        (void)evidence_key_u64(evidence,
                               "d3d12_buffer_release_same_present_id",
                               &evidence_buffer_release_same_present_id);
        (void)evidence_key_u64(evidence, "d3d12_buffer_release_present_id",
                               &evidence_buffer_release_present_id);
        (void)evidence_key_u64(evidence,
                               "d3d12_buffer_release_completion_id",
                               &evidence_buffer_release_completion_id);
        (void)evidence_key_u64(evidence, "d3d12_frame_callback_same_resource",
                               &evidence_frame_callback_same_resource);
        (void)evidence_key_u64(evidence,
                               "d3d12_frame_callback_same_generation",
                               &evidence_frame_callback_same_generation);
        (void)evidence_key_u64(evidence, "d3d12_frame_callback_same_attempt",
                               &evidence_frame_callback_same_attempt);
        (void)evidence_key_u64(evidence,
                               "d3d12_frame_callback_same_present_id",
                               &evidence_frame_callback_same_present_id);
        (void)evidence_key_u64(evidence, "d3d12_frame_callback_present_id",
                               &evidence_frame_callback_present_id);
        (void)evidence_key_u64(evidence,
                               "d3d12_frame_callback_completion_id",
                               &evidence_frame_callback_completion_id);
        evidence_key_u64_alias_max(evidence, "d3d12_present_content_crc",
                                   &evidence_content_crc);
        evidence_key_u64_alias_max(evidence, "d3d12_present_region_crc",
                                   &evidence_content_crc);
        evidence_key_u64_alias_max(evidence, "d3d12_client_content_crc",
                                   &evidence_content_crc);
        evidence_key_u64_alias_max(evidence, "d3d12_visible_content_crc",
                                   &evidence_content_crc);
        evidence_key_u64_alias_max(evidence, "d3d12_present_crc",
                                   &evidence_content_crc);
        evidence_key_u64_alias_max(evidence, "d3d12_present_content_frame",
                                   &evidence_content_frame);
        evidence_key_u64_alias_max(evidence, "d3d12_present_content_frames",
                                   &evidence_content_frame);
        evidence_key_u64_alias_max(evidence, "d3d12_present_content_change",
                                   &evidence_content_frame);
        evidence_key_u64_alias_max(evidence, "d3d12_present_content_changes",
                                   &evidence_content_frame);
        evidence_key_u64_alias_max(evidence, "d3d12_visible_content_frame",
                                   &evidence_content_frame);
        evidence_key_u64_alias_max(evidence, "d3d12_visible_content_frames",
                                   &evidence_content_frame);
        (void)evidence_key_u64(evidence, "d3d12_cpu_readback",
                               &evidence_cpu_readback);
        (void)evidence_key_u64(evidence, "d3d12_cpu_mapping",
                               &evidence_cpu_mapping);
        (void)evidence_key_u64(evidence, "d3d12_cpu_copy",
                               &evidence_cpu_copy);
        (void)evidence_key_u64(evidence, "d3d12_no_cpu_map_no_readback_confirmed",
                               &evidence_no_cpu_map_no_readback);
        (void)evidence_key_u64(
            evidence, "d3d12_final_handoff_no_cpu_map_no_readback",
            &evidence_final_no_cpu_map_no_readback);
        (void)evidence_key_u64(
            evidence, "d3d12_present_sequence_framebuffer_blit_used",
            &evidence_fb_blit_used);
        (void)evidence_key_u64(evidence,
                               "d3d12_present_sequence_cpu_map_used",
                               &evidence_cpu_map_used);
        (void)evidence_key_u64(evidence,
                               "d3d12_present_sequence_cpu_readback_used",
                               &evidence_cpu_readback_used);
        (void)evidence_key_u64(evidence,
                               "d3d12_present_sequence_cpu_copy_used",
                               &evidence_cpu_copy_used);
        (void)evidence_key_u64(evidence,
                               "d3d12_present_sequence_software_dri_used",
                               &evidence_software_dri_used);
        (void)evidence_key_u64(evidence, "d3d12_software_dri_present_used",
                               &evidence_software_dri_present);
        (void)evidence_key_u64(evidence, "d3d12_framebuffer_blit_only",
                               &evidence_framebuffer_blit_only);
        (void)evidence_key_u64(evidence, "d3d12_copy_export",
                               &evidence_copy_export);
        (void)evidence_key_u64(evidence, "d3d12_copy_export_fallback",
                               &evidence_copy_export_fallback);
        (void)evidence_key_u64(evidence, "d3d12_native_present_unimplemented",
                               &evidence_native_present_unimplemented);
        (void)evidence_key_u64(evidence, "d3d12_present_state_only",
                               &evidence_present_state_only);
        (void)evidence_key_u64(evidence, "d3d12_present_fence_only",
                               &evidence_present_fence_only);
        (void)evidence_key_u64(evidence, "d3d12_present_import_only",
                               &evidence_present_import_only);
        (void)evidence_key_u64(evidence, "d3d12_present_open_only",
                               &evidence_present_open_only);
        (void)evidence_key_u64(evidence, "d3d12_present_callback_only",
                               &evidence_present_callback_only);
        (void)evidence_key_u64(evidence, "d3d12_present_release_only",
                               &evidence_present_release_only);
        (void)evidence_key_u64(evidence, "d3d12_present_errno",
                               &evidence_present_errno);
        (void)evidence_key_u64(evidence, "callbacks_blocked",
                               &evidence_callbacks_blocked);
        (void)evidence_key_u64(evidence, "releases_blocked",
                               &evidence_releases_blocked);
        (void)evidence_key_u64(evidence, "d3d12_framebuffer_fallback_used",
                               &evidence_framebuffer_fallback_used);
        (void)evidence_key_u64(evidence, "d3d12_framebuffer_fallback_count",
                               &evidence_framebuffer_fallback_count);
        (void)evidence_key_u64(
            evidence, "d3d12_row_major_cpu_visible_present_target_used",
            &evidence_row_major_cpu_visible_target_used);
        (void)evidence_key_u64(evidence,
                               "d3d12_readback_present_target_used",
                               &evidence_readback_present_target_used);
        (void)evidence_key_u64(
            evidence, "d3d12_final_handoff_user_display_channel_selected",
            &evidence_final_user_display_channel);
        (void)evidence_key_u64(
            evidence, "d3d12_final_handoff_existing_sysmem_allowed",
            &evidence_final_existing_sysmem_allowed);
        (void)evidence_key_u64(
            evidence, "d3d12_final_handoff_synthvid_dirty_only_allowed",
            &evidence_final_synthvid_dirty_only_allowed);
        (void)evidence_key_u64(
            evidence, "d3d12_final_handoff_runtime_resource_required",
            &evidence_final_runtime_resource_required);
        (void)evidence_key_u64(
            evidence, "d3d12_final_handoff_runtime_resource_observed",
            &evidence_final_runtime_resource_observed);
        (void)evidence_key_u64(
            evidence, "d3d12_final_handoff_kernel_abi_required",
            &evidence_final_kernel_abi_required);
        (void)evidence_key_u64(
            evidence, "d3d12_final_handoff_kernel_abi_missing",
            &evidence_final_kernel_abi_missing);
        (void)evidence_key_u64(
            evidence, "d3d12_final_handoff_host_display_commit_success",
            &evidence_final_host_display_commit_success);
        (void)evidence_key_u64(evidence, "d3d12_final_handoff_present_id",
                               &evidence_final_present_id);
        (void)evidence_key_u64(evidence, "d3d12_final_handoff_completed",
                               &evidence_final_completed);
        (void)evidence_key_u64(
            evidence, "d3d12_final_handoff_completion_correlated",
            &evidence_final_completion_correlated);
        (void)evidence_key_u64(evidence, "d3d12_final_handoff_release_fence",
                               &evidence_final_release_fence);
        (void)evidence_key_u64(evidence, "d3d12_final_handoff_success",
                               &evidence_final_success);
        (void)evidence_key_u64(
            evidence, "d3d12_dxg_present_source_commit_expected_eopnotsupp",
            &evidence_commit_expected_eopnotsupp);
        evidence_same_adapter =
            evidence_source_luid_valid == 1 &&
            evidence_present_same_luid == 1 &&
            evidence_key_string(evidence, "d3d12_present_luid",
                                source_luid, sizeof(source_luid)) &&
            evidence_key_string(evidence, "d3d12_present_matched_luid",
                                matched_luid, sizeof(matched_luid)) &&
            strcmp(source_luid, "00000000:00000000") != 0 &&
            strcmp(matched_luid, "00000000:00000000") != 0 &&
            strcmp(source_luid, matched_luid) == 0;
        evidence_present_path_ok =
            evidence_string_is(
                evidence, "d3d12_present_path",
                "d3d12-dxg-present-source-display-handoff");
        evidence_run_id_match =
            env_run_id_present && evidence_run_id[0] &&
            strcmp(evidence_run_id, env_run_id) == 0;
    }
    evidence_terminal_success =
        evidence_stage[0] != '\0' &&
        strcmp(evidence_stage, "present_completed") == 0 &&
        evidence_time_us != 0 &&
        evidence_generation != 0 &&
        evidence_present_rejected == 0 &&
        evidence_native_present_unimplemented == 0 &&
        evidence_present_errno == 0;
    evidence_fail_closed_rejected =
        evidence_present_rejected != 0 ||
        evidence_native_present_unimplemented != 0 ||
        evidence_present_state_only != 0 ||
        evidence_present_fence_only != 0 ||
        evidence_present_import_only != 0 ||
        evidence_present_open_only != 0 ||
        evidence_present_callback_only != 0 ||
        evidence_present_release_only != 0 ||
        evidence_present_errno != 0 ||
        evidence_callbacks_blocked != 0 ||
        evidence_releases_blocked != 0 ||
        evidence_commit_expected_eopnotsupp != 0;
    evidence_soft_claim_rejected =
        evidence &&
        (strstr(evidence, "native_present_claim=0") ||
         strstr(evidence, "present_claim=requires-compositor-completion") ||
         strstr(evidence, "require_present=0"));
    evidence_no_readback =
        evidence_cpu_readback == 0 && evidence_cpu_mapping == 0 &&
        evidence_cpu_copy == 0 &&
        evidence_no_cpu_map_no_readback == 1 &&
        evidence_final_no_cpu_map_no_readback == 1 &&
        evidence_fb_blit_used == 0 &&
        evidence_cpu_map_used == 0 &&
        evidence_cpu_readback_used == 0 &&
        evidence_cpu_copy_used == 0 &&
        evidence_software_dri_used == 0 &&
        evidence_software_dri_present == 0 &&
        evidence_framebuffer_blit_only == 0 &&
        evidence_framebuffer_fallback_used == 0 &&
        evidence_framebuffer_fallback_count == 0 &&
        evidence_row_major_cpu_visible_target_used == 0 &&
        evidence_readback_present_target_used == 0 &&
        evidence_final_user_display_channel == 0 &&
        evidence_final_existing_sysmem_allowed == 0 &&
        evidence_final_synthvid_dirty_only_allowed == 0 &&
        evidence_copy_export == 0 &&
        evidence_copy_export_fallback == 0;
    evidence_shared_resource =
        evidence_resource != 0 && evidence_allocations != 0 &&
        evidence_resource_import_successes != 0 &&
        evidence_runtime_resource == 1 &&
        evidence_present_source_registered == 1;
    evidence_fence_ok =
        evidence_fence != 0 && evidence_fence_target != 0 &&
        evidence_release_fence != 0 &&
        evidence_fence_import_successes != 0;
    evidence_native_completion =
        evidence_present_complete != 0 &&
        evidence_dxg_present_id != 0 &&
        evidence_dxg_completed >= evidence_dxg_present_id &&
        evidence_buffer_completion_correlated == 1 &&
        evidence_display_handoff == 1 &&
        evidence_native_requirements == 1;
    evidence_final_handoff_ok =
        evidence_final_runtime_resource_required == 1 &&
        evidence_final_runtime_resource_observed == 1 &&
        evidence_final_kernel_abi_required == 1 &&
        evidence_final_kernel_abi_missing == 0 &&
        evidence_final_host_display_commit_success == 1 &&
        evidence_final_present_id != 0 &&
        evidence_final_present_id == evidence_dxg_present_id &&
        evidence_final_completed >= evidence_final_present_id &&
        evidence_final_completed == evidence_dxg_completed &&
        evidence_final_completion_correlated == 1 &&
        evidence_final_no_cpu_map_no_readback == 1 &&
        evidence_final_release_fence != 0 &&
        evidence_final_release_fence == evidence_release_fence &&
        evidence_final_success == 1;
    evidence_identity_ok = evidence_identity_current == 1 &&
        (!env_d3d12_run_id_required || evidence_run_id_match);
    evidence_callback_release_ok =
        evidence_callback_release_same_frame_required == 1 &&
        evidence_callback_release_same_frame == 1 &&
        evidence_frame_callback == 1 &&
        evidence_buffer_release == 1 &&
        evidence_buffer_release_same_resource == 1 &&
        evidence_buffer_release_same_generation == 1 &&
        evidence_buffer_release_same_attempt == 1 &&
        evidence_buffer_release_same_present_id == 1 &&
        evidence_buffer_release_present_id == evidence_dxg_present_id &&
        evidence_buffer_release_completion_id != 0 &&
        evidence_buffer_release_completion_id ==
            evidence_native_present_completion_id &&
        evidence_frame_callback_same_resource == 1 &&
        evidence_frame_callback_same_generation == 1 &&
        evidence_frame_callback_same_attempt == 1 &&
        evidence_frame_callback_same_present_id == 1 &&
        evidence_frame_callback_present_id == evidence_dxg_present_id &&
        evidence_frame_callback_completion_id != 0 &&
        evidence_frame_callback_completion_id ==
            evidence_native_present_completion_id &&
        evidence_native_present_attempt_id != 0 &&
        evidence_native_present_completion_id != 0;
    evidence_content_progress =
        evidence_content_crc != 0 &&
        evidence_content_frame != 0;
    evidence_ok =
        evidence_terminal_success && !evidence_fail_closed_rejected &&
        !evidence_soft_claim_rejected &&
        evidence_same_adapter && evidence_no_readback &&
        evidence_shared_resource && evidence_fence_ok &&
        evidence_present_path_ok && evidence_native_completion &&
        evidence_final_handoff_ok && evidence_identity_ok &&
        evidence_callback_release_ok && evidence_content_progress;
    d3d12_present = dxg_transport && d3dkmt && render_node &&
        opengl_submit && evidence_ok;
    shared_surface = render_node && opengl_submit &&
        (virgl || d3d12_present);

    if (policy_effective == 0 && env_accel)
        ok = 0;
    if ((require || policy_effective == 1) &&
        (!have_backend || !render_node || !opengl_submit ||
         !shared_surface || env_software))
        ok = 0;
    if (env_d3d12 && !d3d12_present)
        ok = 0;
    if (env_d3d12_run_id_required &&
        (!env_run_id_present || !evidence_run_id_match))
        ok = 0;
    if (env_d3d12 && (!evidence_ok || !evidence_same_adapter ||
                      !evidence_no_readback || !evidence_shared_resource ||
                      !evidence_fence_ok || !evidence_terminal_success ||
                      evidence_fail_closed_rejected ||
                      evidence_soft_claim_rejected ||
                      !evidence_final_handoff_ok ||
                      !evidence_callback_release_ok ||
                      !evidence_content_progress))
        ok = 0;
    if (env_d3d12 &&
        (!env_d3d12_driver || !env_d3d12_loader ||
         !env_d3d12_xv6gpu || !env_d3d12_inplace ||
         !env_d3d12_throttle0 || !env_d3d12_perf0 ||
         !env_d3d12_vblank0 || !env_egl_wayland || !env_libgl_dri ||
         !env_d3d12_native_present_enabled ||
         !env_d3d12_native_present_required ||
         env_d3d12_native_present_disabled || env_d3d12_copy_export ||
         !force_compositing))
        ok = 0;
    if (env_virgl && (!virgl || !opengl_submit || !shared_surface))
        ok = 0;
    if (env_contract_d3d12 && (!env_d3d12 || !render_node ||
                               !dxg_transport || !d3dkmt ||
                               !shared_surface || !d3d12_present ||
                               !opengl_submit))
        ok = 0;
    if (env_contract_virgl && (!env_virgl || !virgl || !opengl_submit))
        ok = 0;
    if (require && !env_contract_d3d12 && !env_contract_virgl)
        ok = 0;
    if (policy_opengl >= 0 && policy_opengl != opengl_submit)
        ok = 0;
    if (policy_shared >= 0 && policy_shared != shared_surface)
        ok = 0;
    if (policy_d3d12 >= 0 && policy_d3d12 != d3d12_present)
        ok = 0;
    if (line && env_contract_d3d12) {
        if (policy_native_present != 1 ||
            policy_contract_evidence != 1 ||
            policy_same_adapter != 1 ||
            policy_no_readback != 1 ||
            policy_shared_resource != 1 ||
            policy_fence != 1 ||
            policy_copy_export != 0 || policy_readback != 0)
            ok = 0;
    }

    fprintf(stderr,
            "webkitgpusmoke: gpu-contract backend=%s flags=0x%x "
            "render_node=%d shared_surface=%d d3d12_present=%d "
            "opengl_submit=%d dxg_transport=%d d3dkmt=%d virgl_opengl=%d "
            "d3d12_contract_evidence=%d d3d12_same_adapter=%d "
            "d3d12_no_readback=%d d3d12_shared_resource=%d "
            "d3d12_fence=%d d3d12_present_complete=%lu "
            "d3d12_evidence_stage=%s d3d12_evidence_generation=%lu "
            "d3d12_present_evidence_time_us=%lu "
            "d3d12_terminal_success=%d d3d12_fail_closed_rejected=%d "
            "d3d12_soft_claim_rejected=%d "
            "d3d12_present_rejected=%lu d3d12_present_errno=%lu "
            "d3d12_resource_import_successes=%lu "
            "d3d12_runtime_resource=%lu "
            "d3d12_present_source_registered=%lu "
            "d3d12_fence_import_successes=%lu "
            "d3d12_release_fence=%lu d3d12_present_path_ok=%d "
            "d3d12_present_id=%lu d3d12_completed=%lu "
            "d3d12_buffer_completion_correlated=%lu "
            "d3d12_display_handoff=%lu d3d12_native_requirements=%lu "
            "d3d12_final_handoff_ok=%d "
            "d3d12_final_handoff_commit=%lu "
            "d3d12_final_handoff_present_id=%lu "
            "d3d12_final_handoff_completed=%lu "
            "d3d12_final_handoff_success=%lu "
            "d3d12_identity_current=%lu d3d12_run_id=%s "
            "env_run_id=%s d3d12_run_id_match=%d "
            "d3d12_source_luid_valid=%lu d3d12_present_same_luid=%lu "
            "d3d12_native_present_attempt_id=%lu "
            "d3d12_native_present_completion_id=%lu "
            "d3d12_callback_release_same_frame=%lu "
            "d3d12_frame_callback=%lu d3d12_buffer_release=%lu "
            "d3d12_frame_callback_present_id=%lu "
            "d3d12_buffer_release_present_id=%lu "
            "d3d12_frame_callback_completion_id=%lu "
            "d3d12_buffer_release_completion_id=%lu "
            "d3d12_callback_release_ok=%d "
            "d3d12_content_progress=%d d3d12_content_crc=%lu "
            "d3d12_content_frame=%lu d3d12_no_cpu_map=%lu "
            "d3d12_final_no_cpu_map=%lu d3d12_fb_blit_used=%lu "
            "d3d12_cpu_map_used=%lu d3d12_cpu_readback_used=%lu "
            "d3d12_cpu_copy_used=%lu d3d12_software_dri_used=%lu "
            "d3d12_software_dri_present=%lu "
            "d3d12_framebuffer_blit_only=%lu "
            "d3d12_framebuffer_fallback_used=%lu "
            "d3d12_framebuffer_fallback_count=%lu "
            "d3d12_copy_export=%lu "
            "d3d12_copy_export_fallback=%lu "
            "policy_effective=%d policy_opengl_submit=%d "
            "policy_shared_surface=%d policy_d3d12_present=%d "
            "policy_contract_evidence=%d policy_same_adapter=%d "
            "policy_no_readback=%d policy_shared_resource=%d "
            "policy_fence=%d policy_native_present_required=%d "
            "policy_copy_export=%d "
            "policy_readback=%d "
            "env_contract=%s env_d3d12=%d env_virgl=%d env_software=%d "
            "env_d3d12_driver=%d env_d3d12_loader=%d "
            "env_d3d12_xv6gpu=%d env_d3d12_inplace=%d "
            "env_d3d12_throttle0=%d env_d3d12_perf0=%d "
            "env_d3d12_vblank0=%d env_egl_wayland=%d env_libgl_dri=%d "
            "env_d3d12_native_present_enabled=%d "
            "env_d3d12_native_present_required=%d "
            "env_d3d12_native_present_disabled=%d "
            "env_d3d12_copy_export=%d force_compositing=%d "
            "require=%d ok=%d\n",
            have_backend ? backend_name(&info) : "unavailable",
            have_backend ? info.flags : 0,
            render_node, shared_surface, d3d12_present, opengl_submit,
            dxg_transport, d3dkmt, virgl, evidence_ok,
            evidence_same_adapter, evidence_no_readback,
            evidence_shared_resource, evidence_fence_ok,
            (unsigned long)evidence_present_complete,
            evidence_stage[0] ? evidence_stage : "none",
            (unsigned long)evidence_generation,
            (unsigned long)evidence_time_us,
            evidence_terminal_success,
            evidence_fail_closed_rejected,
            evidence_soft_claim_rejected,
            (unsigned long)evidence_present_rejected,
            (unsigned long)evidence_present_errno,
            (unsigned long)evidence_resource_import_successes,
            (unsigned long)evidence_runtime_resource,
            (unsigned long)evidence_present_source_registered,
            (unsigned long)evidence_fence_import_successes,
            (unsigned long)evidence_release_fence,
            evidence_present_path_ok,
            (unsigned long)evidence_dxg_present_id,
            (unsigned long)evidence_dxg_completed,
            (unsigned long)evidence_buffer_completion_correlated,
            (unsigned long)evidence_display_handoff,
            (unsigned long)evidence_native_requirements,
            evidence_final_handoff_ok,
            (unsigned long)evidence_final_host_display_commit_success,
            (unsigned long)evidence_final_present_id,
            (unsigned long)evidence_final_completed,
            (unsigned long)evidence_final_success,
            (unsigned long)evidence_identity_current,
            evidence_run_id[0] ? evidence_run_id : "none",
            env_run_id_present ? env_run_id : "none",
            evidence_run_id_match,
            (unsigned long)evidence_source_luid_valid,
            (unsigned long)evidence_present_same_luid,
            (unsigned long)evidence_native_present_attempt_id,
            (unsigned long)evidence_native_present_completion_id,
            (unsigned long)evidence_callback_release_same_frame,
            (unsigned long)evidence_frame_callback,
            (unsigned long)evidence_buffer_release,
            (unsigned long)evidence_frame_callback_present_id,
            (unsigned long)evidence_buffer_release_present_id,
            (unsigned long)evidence_frame_callback_completion_id,
            (unsigned long)evidence_buffer_release_completion_id,
            evidence_callback_release_ok,
            evidence_content_progress,
            (unsigned long)evidence_content_crc,
            (unsigned long)evidence_content_frame,
            (unsigned long)evidence_no_cpu_map_no_readback,
            (unsigned long)evidence_final_no_cpu_map_no_readback,
            (unsigned long)evidence_fb_blit_used,
            (unsigned long)evidence_cpu_map_used,
            (unsigned long)evidence_cpu_readback_used,
            (unsigned long)evidence_cpu_copy_used,
            (unsigned long)evidence_software_dri_used,
            (unsigned long)evidence_software_dri_present,
            (unsigned long)evidence_framebuffer_blit_only,
            (unsigned long)evidence_framebuffer_fallback_used,
            (unsigned long)evidence_framebuffer_fallback_count,
            (unsigned long)evidence_copy_export,
            (unsigned long)evidence_copy_export_fallback,
            policy_effective, policy_opengl,
            policy_shared, policy_d3d12,
            policy_contract_evidence, policy_same_adapter,
            policy_no_readback, policy_shared_resource, policy_fence,
            policy_native_present, policy_copy_export, policy_readback,
            env_contract ? env_contract : "none",
            env_d3d12, env_virgl, env_software,
            env_d3d12_driver, env_d3d12_loader, env_d3d12_xv6gpu,
            env_d3d12_inplace, env_d3d12_throttle0, env_d3d12_perf0,
            env_d3d12_vblank0, env_egl_wayland, env_libgl_dri,
            env_d3d12_native_present_enabled,
            env_d3d12_native_present_required,
            env_d3d12_native_present_disabled, env_d3d12_copy_export,
            force_compositing, require, ok);
    fflush(stderr);
    if (evidence)
        g_free(evidence);
    if (policy)
        g_free(policy);
    return ok ? 0 : -1;
}

static void set_d3d12_contract_env(void)
{
    setenv("WEBKIT_XV6_REQUIRE_GPU_CONTRACT", "1", 1);
    setenv("WEBKIT_XV6_GPU_CONTRACT", "d3d12-shared-surface", 1);
    setenv("WEBKIT_XV6_GPU_RUN_ID", "d3d12-contract-negative", 1);
    setenv("GALLIUM_DRIVER", "d3d12", 1);
    setenv("MESA_LOADER_DRIVER_OVERRIDE", "d3d12", 1);
    setenv("XV6_MESA_WAYLAND_XV6GPU", "1", 1);
    setenv("XV6_MESA_WAYLAND_INPLACE_PRESENT", "1", 1);
    setenv("XV6_MESA_WAYLAND_THROTTLE", "0", 1);
    setenv("XV6_MESA_PERF_LOG", "0", 1);
    setenv("vblank_mode", "0", 1);
    setenv("EGL_PLATFORM", "wayland", 1);
    setenv("LIBGL_DRIVERS_PATH", "/lib/dri", 1);
    setenv("XV6_D3D12_ENABLE_NATIVE_PRESENT", "1", 1);
    setenv("XV6_D3D12_REQUIRE_NATIVE_PRESENT", "1", 1);
    setenv("WEBKIT_XV6_FORCE_COMPOSITING_MODE", "1", 1);
}

int main(int argc, char **argv)
{
    const char *uri = argc > 1 ? argv[1] : "file:///share/webkit/gpu-smoke.html";
    int timeout_ms = 0;
    int contract_rc;
    struct SmokeRuntime runtime;

    if (argc > 1 && strcmp(argv[1], "--validate-gpu-contract") == 0) {
        phase("contract validation start");
        contract_rc = validate_gpu_contract();
        fprintf(stderr, "webkitgpusmoke: contract-only expected=pass rc=%d\n",
                contract_rc);
        return contract_rc == 0 ? 0 : 2;
    }
    if (argc > 1 && strcmp(argv[1], "--expect-gpu-contract-fail") == 0) {
        phase("contract negative validation start");
        contract_rc = validate_gpu_contract();
        fprintf(stderr, "webkitgpusmoke: contract-only expected=fail rc=%d\n",
                contract_rc);
        return contract_rc != 0 ? 0 : 3;
    }
    if (argc > 1 && strcmp(argv[1], "--expect-d3d12-gpu-contract-fail") == 0) {
        phase("D3D12 contract negative validation start");
        set_d3d12_contract_env();
        contract_rc = validate_gpu_contract();
        fprintf(stderr, "webkitgpusmoke: d3d12-contract-only expected=fail rc=%d\n",
                contract_rc);
        return contract_rc != 0 ? 0 : 3;
    }

    if (argc > 2) {
        timeout_ms = atoi(argv[2]);
    }

    phase("start");
    memset(&runtime, 0, sizeof(runtime));
    runtime.uri = uri;
    if (validate_gpu_contract() != 0) {
        fprintf(stderr, "webkitgpusmoke: GPU contract validation failed\n");
        return 2;
    }
    if (!gtk_init_check(&argc, &argv)) {
        fprintf(stderr, "webkitgpusmoke: gtk_init_check failed\n");
        return 1;
    }
    phase("gtk initialized");

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(window), 820, 560);
    gtk_window_set_title(GTK_WINDOW(window), "xv6 WebKit GPU API Smoke");
    g_signal_connect(window, "destroy", G_CALLBACK(quit_cb), NULL);
    phase("window created");

    GtkWidget *view = webkit_web_view_new();
    phase("web view created");
    WebKitSettings *settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(view));
    phase("settings acquired");
    webkit_settings_set_enable_developer_extras(settings, TRUE);
    webkit_settings_set_enable_webgl(settings, TRUE);
    webkit_settings_set_hardware_acceleration_policy(
        settings,
        env_enabled("WEBKIT_XV6_FORCE_COMPOSITING_MODE") ?
            WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS :
            WEBKIT_HARDWARE_ACCELERATION_POLICY_ON_DEMAND);
    phase("settings applied");
    g_signal_connect(view, "notify::title", G_CALLBACK(title_changed_cb),
                     &runtime);
    g_signal_connect(view, "load-changed", G_CALLBACK(load_changed_cb), NULL);
    gtk_container_add(GTK_CONTAINER(window), view);
    gtk_widget_show_all(window);
    phase("window shown");
    struct SmokeLoad *load = calloc(1, sizeof(*load));
    load->view = WEBKIT_WEB_VIEW(g_object_ref(view));
    load->uri = strdup(uri);
    g_timeout_add(1500, start_load_cb, load);

    if (timeout_ms > 0)
        g_timeout_add(timeout_ms, quit_cb, NULL);
    gtk_main();
    fprintf(stderr,
            "webkitgpusmoke: complete uri=%s timeout_ms=%d title_complete=%d\n",
            uri, timeout_ms, runtime.completion_seen);
    return 0;
}
