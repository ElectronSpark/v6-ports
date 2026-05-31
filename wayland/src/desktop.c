/*
 * desktop.c — xv6 Wayland Session Manager
 *
 * Launches the wlcomp Wayland compositor, then starts Wayland clients
 * (e.g. NetSurf browser).  Monitors child processes and performs clean
 * shutdown on session-control signals.
 *
 * Started automatically by init via /etc/daemons.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>

#define WAYLAND_SOCKET_PATH  "/tmp/wayland-0.lock" /* lockfile (real VFS file) */
#define SOCKET_WAIT_TRIES    200      /* 200 × 20 ms = 4 s */
#define SOCKET_WAIT_US       20000
#define WEBKIT_NET_WAIT_US   35000000 /* DHCP fallback/network daemons need ~30s */
#define WEBKIT_GST_WAIT_US   60000000
#define WEBKIT_DEFAULT_URL   "https://www.google.com/search?q=xv6&gbv=1"
#define WEBKIT_URL_MAX       768
#define XV6_DRM_RENDER_NODE  "/dev/dri/renderD128"
#define XV6_GPU_CONTROL_NODE "/dev/gpu0"
#define XV6_FB_CONTROL_NODE  "/dev/fb0"
#define XV6_D3D12_PRESENT_EVIDENCE_PATH "/tmp/wlcomp-d3d12-present"
#define XV6_D3D12_PRESENT_EVIDENCE_MAX_AGE_SEC 120
#define FB_GPU_BACKEND_QUERY 0x462C
#define FB_GPU_BACKEND_HYPERV_DXG 2
#define FB_GPU_BACKEND_F_RENDER_NODE 0x0001
#define FB_GPU_BACKEND_F_DXG_TRANSPORT 0x0008
#define FB_GPU_BACKEND_F_OPENGL_SUBMIT 0x0020
#define D3D12_DISPLAY_BIND_FIELD_MAX 64
#define DRM_IOCTL_VIRTGPU_GETPARAM 0xc0106443UL
#define VIRTGPU_PARAM_3D_FEATURES  1
#define NETCONF_HOSTNAME_MAX 32

static const char *webkit_feature_flags =
    "--features=+OffscreenCanvas,+OffscreenCanvasInWorkers,+requestIdleCallback";
static const char *webkit_feature_flags_no_idle =
    "--features=+OffscreenCanvas,+OffscreenCanvasInWorkers,-requestIdleCallback";
static const char *webkit_youtube_compat_user_agent =
    "--user-agent=Mozilla/5.0 (X11; xv6 x86_64) AppleWebKit/605.1.15 "
    "(KHTML, like Gecko) Version/17.0 Safari/605.1.15";

struct drm_virtgpu_getparam_compat {
    uint64_t param;
    uint64_t value;
};

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

struct webkit_gpu_contract_state {
    int shared_surface;
    int validated_shared_surface;
    int d3d12_present;
    int d3d12_contract_evidence;
    int d3d12_display_bind;
    int d3d12_same_adapter;
    int d3d12_no_readback;
    int d3d12_shared_resource;
    int d3d12_fence;
    int d3d12_native_present_required;
    int d3d12_run_id_match;
    int d3d12_content_progress;
    int d3d12_evidence_seal;
    int d3d12_copy_export;
    int d3d12_readback;
    int virgl_contract;
    uint64_t display_bind_present_id;
    uint64_t display_bind_completed_id;
    uint64_t display_bind_resource_generation;
    uint64_t host_saw_display_bind_packet;
    uint64_t wsl_presenthistory_completion_credit;
    char display_bind_backend[D3D12_DISPLAY_BIND_FIELD_MAX];
    char display_bind_transport[D3D12_DISPLAY_BIND_FIELD_MAX];
    char display_bind_transport_source[D3D12_DISPLAY_BIND_FIELD_MAX];
    char display_bind_completion_source[D3D12_DISPLAY_BIND_FIELD_MAX];
    const char *gpu_contract;
};

struct d3d12_native_present_evidence {
    uint64_t display_bind_present_id;
    uint64_t display_bind_completed_id;
    uint64_t display_bind_resource_generation;
    uint64_t host_saw_display_bind_packet;
    uint64_t wsl_presenthistory_completion_credit;
    char display_bind_backend[D3D12_DISPLAY_BIND_FIELD_MAX];
    char display_bind_transport[D3D12_DISPLAY_BIND_FIELD_MAX];
    char display_bind_transport_source[D3D12_DISPLAY_BIND_FIELD_MAX];
    char display_bind_completion_source[D3D12_DISPLAY_BIND_FIELD_MAX];
};

struct netconf_req_compat {
    int mode;
    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns;
    char hostname[NETCONF_HOSTNAME_MAX];
};

#ifndef NSIG
#define NSIG 64
#endif

static volatile sig_atomic_t g_running = 1;
static volatile sig_atomic_t g_shutdown_requested;
static volatile sig_atomic_t g_pending_signals[NSIG];
static pid_t wlcomp_pid;
static pid_t client_pid;
static pid_t glsmoke_pid;
static pid_t httpd_pid;
static pid_t gst_warmup_pid;
static int gst_registry_ready;
static int http_smoke_request_count;

static int webkit_accel_enabled_by_cmdline(void);
static int webkit_gpu_smoke_enabled_by_cmdline(void);
static int webkit_webgl_smoke_enabled_by_cmdline(void);
static int webkit_api_smoke_enabled_by_cmdline(void);
static int webkit_http_smoke_enabled_by_cmdline(void);
static int webkit_coop_smoke_enabled_by_cmdline(void);
static int webkit_js_smoke_enabled_by_cmdline(void);
static int webkit_youtube_boot_smoke_enabled_by_cmdline(void);
static int webkit_youtube_waterfall_smoke_enabled_by_cmdline(void);
static int webkit_youtube_compat_disabled_by_cmdline(void);
static int webkit_logging_enabled_by_cmdline(void);
static int webkit_request_idle_disabled_by_cmdline(void);
static int webkit_feature_gate_smoke_enabled_by_cmdline(void);
static int webkit_idle_browse_smoke_enabled_by_cmdline(void);
static int webkit_compat_gate_smoke_enabled_by_cmdline(void);
static int webkit_js_disabled_by_cmdline(void);
static int webkit_disable_gdk_gl_by_cmdline(void);
static int webkit_dmabuf_enabled_by_cmdline(void);
static int webkit_reopen_count_from_cmdline(void);
static int webkit_timeout_ms_from_cmdline(int fallback);
static int gpu_validate_enabled_by_cmdline(void);
static int glmaze_enabled_by_cmdline(void);
static void glmaze_args_from_cmdline(char *frames_arg, size_t frames_size);
static int desktop_disabled_by_cmdline(void);
static int desktop_exit_after_smoke_by_cmdline(void);
static int cmdline_int_value(const char *cmdline, const char *key,
                             int fallback);
static int read_cmdline(char *buf, size_t buf_size);
static int write_all_fd(int fd, const void *buf, size_t len);
static const char *http_content_type_for_path(const char *path);
static int http_try_serve_webkit_file(int cfd, const char *path,
                                      const char *extra, const char *range);
static void http_smoke_self_probe(const char *path);
static void webkit_print_runtime_probe(void);
static void webkit_dump_gst_debug_evidence(void);
static void webkit_print_log_evidence(const char *reason);
static void write_webkit_gpu_policy_file(const char *name, int requested_accel,
                                         int effective_accel,
                                         int opengl_submit_available,
                                         int dxg_transport_available,
                                         int render_node_available,
                                         int dmabuf_requested,
                                         const struct webkit_gpu_contract_state
                                             *contract);

static int xv6_virgl_available(void)
{
    uint64_t value = 0;
    struct drm_virtgpu_getparam_compat req = {
        .param = VIRTGPU_PARAM_3D_FEATURES,
        .value = (uint64_t)(uintptr_t)&value,
    };
    int fd = open(XV6_DRM_RENDER_NODE, O_RDWR | O_CLOEXEC);
    int ok;

    if (fd < 0)
        return 0;
    ok = ioctl(fd, DRM_IOCTL_VIRTGPU_GETPARAM, &req) == 0 && value != 0;
    close(fd);
    return ok;
}

static int xv6_gpu_backend_info(struct fb_gpu_backend_info_compat *info)
{
    static const char *paths[] = {
        XV6_GPU_CONTROL_NODE,
        XV6_FB_CONTROL_NODE,
        XV6_DRM_RENDER_NODE,
    };
    size_t i;

    if (!info)
        return 0;
    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        int fd;

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

static int xv6_opengl_submit_available(void)
{
    struct fb_gpu_backend_info_compat info;

    return xv6_gpu_backend_info(&info) &&
           (info.flags & FB_GPU_BACKEND_F_OPENGL_SUBMIT) != 0;
}

static int xv6_render_node_available(void)
{
    struct fb_gpu_backend_info_compat info;

    return xv6_gpu_backend_info(&info) &&
           (info.flags & FB_GPU_BACKEND_F_RENDER_NODE) != 0;
}

static int xv6_dxg_transport_available(void)
{
    struct fb_gpu_backend_info_compat info;

    return xv6_gpu_backend_info(&info) &&
           info.backend == FB_GPU_BACKEND_HYPERV_DXG &&
           (info.flags & FB_GPU_BACKEND_F_DXG_TRANSPORT) != 0;
}

static int evidence_token_separator(char c)
{
    return c == '\0' || c == '\n' || c == '\r' ||
           c == ' ' || c == '\t';
}

static int evidence_token_start(const char *text, const char *p)
{
    return p == text || p[-1] == '\n' || p[-1] == '\r' ||
           p[-1] == ' ' || p[-1] == '\t';
}

static const char *evidence_key_value(const char *text, const char *key)
{
    size_t key_len;
    const char *p;

    if (!text || !key || !key[0])
        return NULL;
    key_len = strlen(key);
    for (p = text; *p; p++) {
        if (!evidence_token_start(text, p))
            continue;
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=')
            return p + key_len + 1;
    }
    return NULL;
}

static int evidence_key_u64(const char *text, const char *key, uint64_t *out)
{
    const char *p;
    char *end = NULL;
    uint64_t value;

    if (!text || !key || !out)
        return 0;
    p = evidence_key_value(text, key);
    if (!p)
        return 0;
    errno = 0;
    value = strtoull(p, &end, 0);
    if (errno != 0 || end == p || !evidence_token_separator(*end))
        return 0;
    *out = value;
    return 1;
}

static int evidence_key_string(const char *text, const char *key,
                               char *out, size_t out_size)
{
    const char *p;
    size_t n = 0;

    if (!text || !key || !out || out_size == 0)
        return 0;
    out[0] = '\0';
    p = evidence_key_value(text, key);
    if (!p)
        return 0;
    while (!evidence_token_separator(p[n]))
        n++;
    if (n == 0 || n >= out_size)
        return 0;
    memcpy(out, p, n);
    out[n] = '\0';
    return 1;
}

static void evidence_key_u64_alias_max(const char *text, const char *key,
                                       uint64_t *out)
{
    uint64_t value = 0;

    if (!out)
        return;
    if (evidence_key_u64(text, key, &value) && value > *out)
        *out = value;
}

static int d3d12_native_present_evidence_read(
    const char *text, struct d3d12_native_present_evidence *out)
{
    if (!text || !out)
        return 0;
    memset(out, 0, sizeof(*out));
    return evidence_key_string(text, "display_bind_backend",
                               out->display_bind_backend,
                               sizeof(out->display_bind_backend)) &&
           evidence_key_string(text, "display_bind_transport",
                               out->display_bind_transport,
                               sizeof(out->display_bind_transport)) &&
           evidence_key_string(text, "display_bind_transport_source",
                               out->display_bind_transport_source,
                               sizeof(out->display_bind_transport_source)) &&
           evidence_key_u64(text, "host_saw_display_bind_packet",
                            &out->host_saw_display_bind_packet) &&
           evidence_key_u64(text, "wsl_presenthistory_completion_credit",
                            &out->wsl_presenthistory_completion_credit) &&
           evidence_key_u64(text, "display_bind_present_id",
                            &out->display_bind_present_id) &&
           evidence_key_u64(text, "display_bind_completed_id",
                            &out->display_bind_completed_id) &&
           evidence_key_u64(text, "display_bind_resource_generation",
                            &out->display_bind_resource_generation) &&
           evidence_key_string(text, "display_bind_completion_source",
                               out->display_bind_completion_source,
                               sizeof(out->display_bind_completion_source));
}

static int d3d12_native_present_evidence_valid(
    const struct d3d12_native_present_evidence *evidence)
{
    return evidence &&
           strcmp(evidence->display_bind_backend,
                  "gpup_dxg_scanout_bind") == 0 &&
           strcmp(evidence->display_bind_transport,
                  "gpu-p-dxg-resource-scanout-bind") == 0 &&
           strcmp(evidence->display_bind_transport_source,
                  "non_wsl_linux_dxgkrnl_extension") == 0 &&
           evidence->host_saw_display_bind_packet == 1 &&
           evidence->wsl_presenthistory_completion_credit == 0 &&
           evidence->display_bind_present_id != 0 &&
           evidence->display_bind_completed_id >=
               evidence->display_bind_present_id &&
           evidence->display_bind_resource_generation != 0 &&
           strcmp(evidence->display_bind_completion_source, "display") == 0;
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

static char *read_d3d12_present_evidence(void)
{
    FILE *fp;
    char *buf;
    size_t n;

    if (!d3d12_present_evidence_fresh())
        return NULL;
    fp = fopen(XV6_D3D12_PRESENT_EVIDENCE_PATH, "r");
    if (!fp)
        return NULL;
    buf = calloc(1, 32768);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    n = fread(buf, 1, 32767, fp);
    fclose(fp);
    buf[n] = '\0';
    return buf;
}

static void compute_webkit_gpu_contract(
    int opengl_submit_available, int dxg_transport_available,
    int render_node_available, const char *expected_run_id,
    struct webkit_gpu_contract_state *contract)
{
    char source_luid[32] = { 0 };
    char matched_luid[32] = { 0 };
    char run_id[128] = { 0 };
    char compositor_run_id[128] = { 0 };
    char seal_run_id[128] = { 0 };
    char seal_end_run_id[128] = { 0 };
    char content_progress_state[96] = { 0 };
    char visible_content_progress[96] = { 0 };
    char content_display_bind_completion_source[96] = { 0 };
    uint64_t resource = 0;
    uint64_t allocations = 0;
    uint64_t resource_import_successes = 0;
    uint64_t runtime_resource = 0;
    uint64_t present_source_registered = 0;
    uint64_t fence = 0;
    uint64_t fence_target = 0;
    uint64_t release_fence = 0;
    uint64_t fence_import_successes = 0;
    uint64_t evidence_generation = 0;
    uint64_t seal_begin = 0;
    uint64_t seal_end = 0;
    uint64_t seal_complete = 0;
    uint64_t seal_generation = 0;
    uint64_t seal_end_generation = 0;
    uint64_t present_complete = 0;
    uint64_t present_id = 0;
    uint64_t completed = 0;
    uint64_t buffer_correlated = 0;
    uint64_t handoff = 0;
    uint64_t native_requirements = 0;
    uint64_t identity_current = 0;
    uint64_t buffer_generation = 0;
    uint64_t callback_release_same_frame = 0;
    uint64_t frame_callback = 0;
    uint64_t buffer_release = 0;
    uint64_t cpu_readback = 1;
    uint64_t cpu_mapping = 1;
    uint64_t cpu_copy = 1;
    uint64_t no_cpu = 0;
    uint64_t final_no_cpu = 0;
    uint64_t fb_blit = 1;
    uint64_t cpu_map_used = 1;
    uint64_t cpu_readback_used = 1;
    uint64_t cpu_copy_used = 1;
    uint64_t software_dri = 1;
    uint64_t software_dri_present = 1;
    uint64_t framebuffer_blit_only = 1;
    uint64_t copy_export = 0;
    uint64_t copy_export_fallback = 0;
    uint64_t current_run_valid = 0;
    uint64_t content_crc = 0;
    uint64_t content_frame = 0;
    uint64_t content_frame_hash = 0;
    uint64_t content_requires_native = 0;
    uint64_t visible_requires_native = 0;
    uint64_t visible_credit_before_native = 1;
    uint64_t content_native_complete = 0;
    uint64_t content_visible_credit = 0;
    uint64_t content_native_credit = 0;
    uint64_t content_source_owned = 0;
    uint64_t content_present_id = 0;
    uint64_t content_completed = 0;
    uint64_t content_display_bind_present_id = 0;
    uint64_t content_display_bind_completed_id = 0;
    uint64_t content_display_bind_resource_generation = 0;
    struct d3d12_native_present_evidence native_present;
    char *evidence;
    int virgl;
    int evidence_sealed = 0;
    int run_id_match = 0;
    int content_progress_ok = 0;

    memset(contract, 0, sizeof(*contract));
    contract->gpu_contract = "none";
    virgl = opengl_submit_available && xv6_virgl_available();
    contract->virgl_contract = virgl;

    evidence = read_d3d12_present_evidence();
    if (evidence) {
        evidence_key_u64(evidence, "d3d12_evidence_generation",
                         &evidence_generation);
        evidence_key_u64(evidence, "d3d12_evidence_seal_begin",
                         &seal_begin);
        evidence_key_u64(evidence, "d3d12_evidence_seal_end",
                         &seal_end);
        evidence_key_u64(evidence, "d3d12_evidence_seal_complete",
                         &seal_complete);
        evidence_key_u64(evidence, "d3d12_evidence_seal_generation",
                         &seal_generation);
        evidence_key_u64(evidence, "d3d12_evidence_seal_end_generation",
                         &seal_end_generation);
        evidence_key_string(evidence, "d3d12_evidence_seal_run_id",
                            seal_run_id, sizeof(seal_run_id));
        evidence_key_string(evidence, "d3d12_evidence_seal_end_run_id",
                            seal_end_run_id, sizeof(seal_end_run_id));
        evidence_key_u64(evidence, "d3d12_present_resource", &resource);
        evidence_key_u64(evidence, "d3d12_present_allocation_count",
                         &allocations);
        evidence_key_u64(evidence, "d3d12_resource_import_successes",
                         &resource_import_successes);
        evidence_key_u64(evidence,
                         "d3d12_runtime_created_d3d12_resource_present",
                         &runtime_resource);
        evidence_key_u64(evidence, "d3d12_present_source_registered",
                         &present_source_registered);
        evidence_key_u64(evidence, "d3d12_present_fence", &fence);
        evidence_key_u64(evidence, "d3d12_present_fence_target",
                         &fence_target);
        evidence_key_u64(evidence, "d3d12_present_release_fence",
                         &release_fence);
        evidence_key_u64(evidence, "d3d12_fence_import_successes",
                         &fence_import_successes);
        evidence_key_u64(evidence, "d3d12_gpu_present_complete",
                         &present_complete);
        evidence_key_u64(evidence, "d3d12_dxg_present_id", &present_id);
        evidence_key_u64(evidence, "d3d12_dxg_present_completed",
                         &completed);
        evidence_key_u64(evidence,
                         "d3d12_present_source_buffer_completion_correlated",
                         &buffer_correlated);
        evidence_key_u64(evidence, "d3d12_display_handoff_implemented",
                         &handoff);
        evidence_key_u64(evidence,
                         "d3d12_native_present_requirements_satisfied",
                         &native_requirements);
        evidence_key_u64(evidence,
                         "d3d12_present_identity_current_run_valid",
                         &identity_current);
        evidence_key_u64(evidence, "d3d12_buffer_generation",
                         &buffer_generation);
        evidence_key_u64(evidence,
                         "d3d12_callback_release_same_frame_observed",
                         &callback_release_same_frame);
        evidence_key_u64(evidence, "d3d12_frame_callback_observed",
                         &frame_callback);
        evidence_key_u64(evidence, "d3d12_buffer_release_observed",
                         &buffer_release);
        evidence_key_u64(evidence, "d3d12_cpu_readback", &cpu_readback);
        evidence_key_u64(evidence, "d3d12_cpu_mapping", &cpu_mapping);
        evidence_key_u64(evidence, "d3d12_cpu_copy", &cpu_copy);
        evidence_key_u64(evidence, "d3d12_no_cpu_map_no_readback_confirmed",
                         &no_cpu);
        evidence_key_u64(evidence,
                         "d3d12_final_handoff_no_cpu_map_no_readback",
                         &final_no_cpu);
        evidence_key_u64(evidence,
                         "d3d12_present_sequence_framebuffer_blit_used",
                         &fb_blit);
        evidence_key_u64(evidence, "d3d12_present_sequence_cpu_map_used",
                         &cpu_map_used);
        evidence_key_u64(evidence,
                         "d3d12_present_sequence_cpu_readback_used",
                         &cpu_readback_used);
        evidence_key_u64(evidence, "d3d12_present_sequence_cpu_copy_used",
                         &cpu_copy_used);
        evidence_key_u64(evidence,
                         "d3d12_present_sequence_software_dri_used",
                         &software_dri);
        evidence_key_u64(evidence, "d3d12_software_dri_present_used",
                         &software_dri_present);
        evidence_key_u64(evidence, "d3d12_framebuffer_blit_only",
                         &framebuffer_blit_only);
        evidence_key_u64(evidence, "d3d12_copy_export", &copy_export);
        evidence_key_u64(evidence, "d3d12_copy_export_fallback",
                         &copy_export_fallback);
        evidence_key_u64(evidence,
                         "d3d12_present_identity_current_run_valid",
                         &current_run_valid);
        evidence_key_string(evidence, "d3d12_run_id",
                            run_id, sizeof(run_id));
        evidence_key_string(evidence,
                            "d3d12_present_identity_compositor_run_id",
                            compositor_run_id, sizeof(compositor_run_id));
        evidence_key_u64_alias_max(evidence, "d3d12_visible_content_crc",
                                   &content_crc);
        evidence_key_u64_alias_max(evidence, "d3d12_visible_content_frame",
                                   &content_frame);
        evidence_key_u64_alias_max(evidence, "d3d12_visible_content_frames",
                                   &content_frame);
        evidence_key_u64_alias_max(evidence, "d3d12_visible_frame_hash",
                                   &content_frame_hash);
        evidence_key_u64_alias_max(evidence,
                                   "d3d12_visible_content_frame_hash",
                                   &content_frame_hash);
        evidence_key_string(evidence, "d3d12_content_progress_state",
                            content_progress_state,
                            sizeof(content_progress_state));
        evidence_key_string(evidence, "d3d12_visible_content_progress",
                            visible_content_progress,
                            sizeof(visible_content_progress));
        evidence_key_u64(evidence,
                         "d3d12_content_progress_requires_native_present",
                         &content_requires_native);
        evidence_key_u64(
            evidence,
            "d3d12_visible_content_requires_native_present_completion",
            &visible_requires_native);
        evidence_key_u64(
            evidence, "d3d12_visible_content_credit_before_native_present",
            &visible_credit_before_native);
        evidence_key_u64(evidence,
                         "d3d12_content_progress_native_present_complete",
                         &content_native_complete);
        evidence_key_u64(evidence,
                         "d3d12_content_progress_visible_credit",
                         &content_visible_credit);
        evidence_key_u64(evidence,
                         "d3d12_content_progress_native_present_credit",
                         &content_native_credit);
        evidence_key_u64(evidence,
                         "d3d12_content_progress_source_owned",
                         &content_source_owned);
        evidence_key_u64(evidence, "d3d12_content_progress_present_id",
                         &content_present_id);
        evidence_key_u64(evidence, "d3d12_content_progress_completed",
                         &content_completed);
        evidence_key_u64(
            evidence, "d3d12_content_progress_display_bind_present_id",
            &content_display_bind_present_id);
        evidence_key_u64(
            evidence, "d3d12_content_progress_display_bind_completed_id",
            &content_display_bind_completed_id);
        evidence_key_u64(
            evidence,
            "d3d12_content_progress_display_bind_resource_generation",
            &content_display_bind_resource_generation);
        evidence_key_string(
            evidence,
            "d3d12_content_progress_display_bind_completion_source",
            content_display_bind_completion_source,
            sizeof(content_display_bind_completion_source));
        if (d3d12_native_present_evidence_read(evidence, &native_present)) {
            memcpy(contract->display_bind_backend,
                   native_present.display_bind_backend,
                   sizeof(contract->display_bind_backend));
            memcpy(contract->display_bind_transport,
                   native_present.display_bind_transport,
                   sizeof(contract->display_bind_transport));
            memcpy(contract->display_bind_transport_source,
                   native_present.display_bind_transport_source,
                   sizeof(contract->display_bind_transport_source));
            memcpy(contract->display_bind_completion_source,
                   native_present.display_bind_completion_source,
                   sizeof(contract->display_bind_completion_source));
            contract->display_bind_present_id =
                native_present.display_bind_present_id;
            contract->display_bind_completed_id =
                native_present.display_bind_completed_id;
            contract->display_bind_resource_generation =
                native_present.display_bind_resource_generation;
            contract->host_saw_display_bind_packet =
                native_present.host_saw_display_bind_packet;
            contract->wsl_presenthistory_completion_credit =
                native_present.wsl_presenthistory_completion_credit;
            contract->d3d12_display_bind =
                d3d12_native_present_evidence_valid(&native_present) &&
                (buffer_generation == 0 ||
                 native_present.display_bind_resource_generation ==
                     buffer_generation);
        }

        contract->d3d12_same_adapter =
            evidence_key_string(evidence, "d3d12_present_luid",
                                source_luid, sizeof(source_luid)) &&
            evidence_key_string(evidence, "d3d12_present_matched_luid",
                                matched_luid, sizeof(matched_luid)) &&
            strcmp(source_luid, matched_luid) == 0;
        contract->d3d12_shared_resource =
            resource != 0 && allocations != 0 &&
            resource_import_successes != 0 && runtime_resource == 1 &&
            present_source_registered == 1;
        contract->d3d12_fence =
            fence != 0 && fence_target != 0 && release_fence != 0 &&
            fence_import_successes != 0;
        contract->d3d12_no_readback =
            cpu_readback == 0 && cpu_mapping == 0 && cpu_copy == 0 &&
            no_cpu == 1 && final_no_cpu == 1 && fb_blit == 0 &&
            cpu_map_used == 0 && cpu_readback_used == 0 &&
            cpu_copy_used == 0 && software_dri == 0 &&
            software_dri_present == 0 && framebuffer_blit_only == 0 &&
            copy_export == 0 && copy_export_fallback == 0;
        contract->d3d12_native_present_required =
            evidence_string_is(
                evidence, "d3d12_present_path",
                "d3d12-dxg-present-source-display-handoff") &&
            contract->d3d12_display_bind &&
            present_complete != 0 && present_id != 0 &&
            completed >= present_id && buffer_correlated == 1 &&
            present_id == contract->display_bind_present_id &&
            completed == contract->display_bind_completed_id &&
            handoff == 1 && native_requirements == 1 &&
            identity_current == 1 &&
            callback_release_same_frame == 1 &&
            frame_callback == 1 && buffer_release == 1;
        run_id_match =
            expected_run_id && expected_run_id[0] &&
            run_id[0] && compositor_run_id[0] &&
            strcmp(run_id, expected_run_id) == 0 &&
            strcmp(compositor_run_id, expected_run_id) == 0 &&
            current_run_valid == 1;
        evidence_sealed =
            seal_begin == 1 && seal_end == 1 && seal_complete == 1 &&
            evidence_generation != 0 &&
            seal_generation == evidence_generation &&
            seal_end_generation == evidence_generation &&
            strcmp(seal_run_id, run_id) == 0 &&
            strcmp(seal_end_run_id, run_id) == 0;
        content_progress_ok =
            content_crc != 0 && content_frame != 0 &&
            content_frame_hash != 0 &&
            strcmp(content_progress_state, "NATIVE_PRESENT_COMPLETE") == 0 &&
            strcmp(visible_content_progress, "NATIVE_PRESENT_COMPLETE") == 0 &&
            content_requires_native == 1 && visible_requires_native == 1 &&
            visible_credit_before_native == 0 &&
            content_native_complete == 1 &&
            content_visible_credit == 1 &&
            content_native_credit == 1 &&
            content_source_owned == 1 &&
            content_present_id == contract->display_bind_present_id &&
            content_completed == contract->display_bind_completed_id &&
            content_display_bind_present_id ==
                contract->display_bind_present_id &&
            content_display_bind_completed_id ==
                contract->display_bind_completed_id &&
            content_display_bind_resource_generation ==
                contract->display_bind_resource_generation &&
            strcmp(content_display_bind_completion_source, "display") == 0;
        contract->d3d12_copy_export =
            copy_export != 0 || copy_export_fallback != 0;
        contract->d3d12_readback =
            cpu_readback != 0 || cpu_mapping != 0 || cpu_copy != 0 ||
            fb_blit != 0 || cpu_map_used != 0 || cpu_readback_used != 0 ||
            cpu_copy_used != 0 || software_dri != 0 ||
            software_dri_present != 0 || framebuffer_blit_only != 0;
        free(evidence);
    }

    contract->d3d12_contract_evidence =
        contract->d3d12_same_adapter &&
        contract->d3d12_no_readback &&
        contract->d3d12_shared_resource &&
        contract->d3d12_fence &&
        contract->d3d12_display_bind &&
        contract->d3d12_native_present_required &&
        evidence_sealed &&
        run_id_match &&
        content_progress_ok &&
        !contract->d3d12_copy_export &&
        !contract->d3d12_readback;
    contract->d3d12_run_id_match = run_id_match;
    contract->d3d12_content_progress = content_progress_ok;
    contract->d3d12_evidence_seal = evidence_sealed;
    contract->d3d12_present =
        dxg_transport_available && render_node_available &&
        opengl_submit_available && contract->d3d12_contract_evidence;
    contract->shared_surface =
        render_node_available && opengl_submit_available &&
        (virgl || contract->d3d12_present);
    contract->validated_shared_surface = contract->shared_surface;
    if (contract->d3d12_present)
        contract->gpu_contract = "d3d12-shared-surface";
    else if (virgl)
        contract->gpu_contract = "virgl-opengl-submit";
}

static void disable_child_coredumps(void)
{
    struct rlimit lim = {0, 0};
    (void)setrlimit(RLIMIT_CORE, &lim);
}

static long long monotonic_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static const char *desktop_signal_name(int sig)
{
    switch (sig) {
    case SIGHUP:
        return "SIGHUP";
    case SIGINT:
        return "SIGINT";
    case SIGQUIT:
        return "SIGQUIT";
    case SIGTERM:
        return "SIGTERM";
    case SIGCHLD:
        return "SIGCHLD";
    case SIGPIPE:
        return "SIGPIPE";
    case SIGUSR1:
        return "SIGUSR1";
    case SIGUSR2:
        return "SIGUSR2";
    case SIGALRM:
        return "SIGALRM";
    case SIGCONT:
        return "SIGCONT";
    case SIGTSTP:
        return "SIGTSTP";
    case SIGTTIN:
        return "SIGTTIN";
    case SIGTTOU:
        return "SIGTTOU";
    default:
        return "signal";
    }
}

static int desktop_signal_requests_shutdown(int sig)
{
    return sig == SIGHUP || sig == SIGINT || sig == SIGQUIT ||
           sig == SIGTERM;
}

static int desktop_signal_is_internal(int sig)
{
    return sig == SIGCHLD || sig == SIGPIPE;
}

static void desktop_signal_child(pid_t pid, int sig)
{
    if (pid <= 0)
        return;
    if (kill(-pid, sig) != 0)
        (void)kill(pid, sig);
}

static void desktop_dispatch_signal_to_children(int sig)
{
    pid_t pids[] = {
        client_pid,
        glsmoke_pid,
        gst_warmup_pid,
        httpd_pid,
        wlcomp_pid,
    };
    size_t i;

    for (i = 0; i < sizeof(pids) / sizeof(pids[0]); i++) {
        size_t j;

        if (pids[i] <= 0)
            continue;
        for (j = 0; j < i; j++) {
            if (pids[j] == pids[i])
                break;
        }
        if (j == i)
            desktop_signal_child(pids[i], sig);
    }
}

static void desktop_process_pending_signals(void)
{
    int sig;

    for (sig = 1; sig < NSIG; sig++) {
        if (!g_pending_signals[sig])
            continue;
        g_pending_signals[sig] = 0;

        if (desktop_signal_is_internal(sig))
            continue;

        fprintf(stderr, "[desktop] caught %s (%d), dispatching\n",
                desktop_signal_name(sig), sig);
        desktop_dispatch_signal_to_children(sig);

        if (desktop_signal_requests_shutdown(sig)) {
            g_shutdown_requested = 1;
            g_running = 0;
        }
    }
}

static void desktop_signal_handler(int sig)
{
    if (sig > 0 && sig < NSIG)
        g_pending_signals[sig] = 1;
}

static int desktop_should_manage_signal(int sig)
{
    if (sig <= 0 || sig >= NSIG)
        return 0;
    if (sig == SIGKILL || sig == SIGSTOP)
        return 0;
    if (sig == SIGABRT || sig == SIGBUS || sig == SIGFPE ||
        sig == SIGILL || sig == SIGSEGV || sig == SIGTRAP)
        return 0;
    return 1;
}

static void install_signal_handlers(void)
{
    int sig;

    for (sig = 1; sig < NSIG; sig++) {
        struct sigaction sa;

        if (!desktop_should_manage_signal(sig))
            continue;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = desktop_signal_handler;
        sigemptyset(&sa.sa_mask);
        if (sig == SIGCHLD)
            sa.sa_flags = SA_NOCLDSTOP;
        if (sigaction(sig, &sa, NULL) != 0 && errno != EINVAL) {
            fprintf(stderr, "[desktop] sigaction(%d) failed errno=%d (%s)\n",
                    sig, errno, strerror(errno));
        }
    }
}

/* Wait for wlcomp to create the Wayland socket.  Returns 0 on success. */
static int wait_for_socket(void)
{
    struct stat st;
    for (int i = 0; i < SOCKET_WAIT_TRIES; i++) {
        desktop_process_pending_signals();
        if (!g_running)
            return -1;
        if (stat(WAYLAND_SOCKET_PATH, &st) == 0)
            return 0;
        usleep(SOCKET_WAIT_US);
    }
    return -1;
}

static int url_needs_network_wait(const char *url)
{
    const char *host;

    if (!url)
        return 0;
    if (strncmp(url, "http://", 7) == 0)
        host = url + 7;
    else if (strncmp(url, "https://", 8) == 0)
        host = url + 8;
    else
        return 0;

    return strncmp(host, "localhost", 9) != 0 &&
           strncmp(host, "127.", 4) != 0 &&
           strncmp(host, "0.0.0.0", 7) != 0 &&
           strncmp(host, "[::1]", 5) != 0;
}

static void ipv4_to_string(uint32_t addr, char *buf, size_t buf_size)
{
    snprintf(buf, buf_size, "%u.%u.%u.%u",
             (unsigned)((addr >> 0) & 0xff),
             (unsigned)((addr >> 8) & 0xff),
             (unsigned)((addr >> 16) & 0xff),
             (unsigned)((addr >> 24) & 0xff));
}

static int write_resolv_conf(uint32_t dns)
{
    char dns_buf[32];
    char body[96];
    int fd;
    int len;

    if (dns == 0)
        return -1;

    ipv4_to_string(dns, dns_buf, sizeof(dns_buf));
    len = snprintf(body, sizeof(body), "nameserver %s\n", dns_buf);
    if (len <= 0 || (size_t)len >= sizeof(body))
        return -1;

    fd = open("/etc/resolv.conf", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        fprintf(stderr,
                "[desktop] failed to open /etc/resolv.conf errno=%d (%s)\n",
                errno, strerror(errno));
        return -1;
    }
    if (write(fd, body, (size_t)len) != len) {
        fprintf(stderr,
                "[desktop] failed to write /etc/resolv.conf errno=%d (%s)\n",
                errno, strerror(errno));
        close(fd);
        return -1;
    }
    close(fd);
    fprintf(stderr, "[desktop] resolv.conf DNS %s\n", dns_buf);
    return 0;
}

static int sync_resolv_conf_from_netconf(int wait_us)
{
    const int step_us = 250000;
    int waited = 0;

    while (waited <= wait_us) {
        struct netconf_req_compat req;
        int fd = open("/dev/netconf", O_RDONLY | O_CLOEXEC);

        desktop_process_pending_signals();
        if (!g_running)
            return -1;

        if (fd >= 0) {
            int n = read(fd, &req, sizeof(req));

            close(fd);
            if (n == (int)sizeof(req) && req.ip != 0 && req.dns != 0)
                return write_resolv_conf(req.dns);
        }
        if (waited >= wait_us)
            break;
        usleep(step_us);
        waited += step_us;
    }

    fprintf(stderr,
            "[desktop] network DNS not ready after %d ms; keeping existing "
            "/etc/resolv.conf\n",
            wait_us / 1000);
    return -1;
}

static int webkit_youtube_compat_url(const char *url)
{
    if (!url || webkit_youtube_compat_disabled_by_cmdline())
        return 0;
    return strstr(url, "youtube.com") != NULL ||
           strstr(url, "youtube-nocookie.com") != NULL ||
           strstr(url, "youtu.be") != NULL;
}

static pid_t launch_wlcomp(void)
{
    char cmdline_buf[512] = "";
    char cmdline_env[sizeof("XV6_KERNEL_CMDLINE=") + sizeof(cmdline_buf)];
    char fb_bo_env[] = "XV6_WLCOMP_FB_BO=1";
    char fb_direct_env[] = "XV6_WLCOMP_FB_DIRECT=1";
    char gpu_compose_env[] = "XV6_WLCOMP_GPU_COMPOSE=1";
    int have_cmdline = read_cmdline(cmdline_buf, sizeof(cmdline_buf)) == 0;
    int virgl_available = xv6_virgl_available();
    /*
     * When no virgl render node is present (e.g. Hyper-V firmware FB), but a
     * kernel /dev/fb0 exists, the kernel BO/direct-scanout fast paths still
     * apply: fb_virt is PA2VA cached RAM and a single memcpy/either_copyin
     * per row beats the generic Wayland SHM blit by ~4x. Default both knobs
     * on whenever we can open the FB cdev, regardless of virgl. The cmdline
     * still wins.
     */
    int fb_cdev_available = 0;
    {
        int fbfd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
        if (fbfd >= 0) {
            fb_cdev_available = 1;
            close(fbfd);
        }
    }
    int fast_default = virgl_available || fb_cdev_available;
    int use_fb_direct = have_cmdline ?
        cmdline_int_value(cmdline_buf, "wlcomp_fb_direct",
                          fast_default) :
        fast_default;
    int use_fb_bo = have_cmdline ?
        cmdline_int_value(cmdline_buf, "wlcomp_fb_bo",
                          fast_default) :
        fast_default;
    int use_gpu_compose = have_cmdline ?
        cmdline_int_value(cmdline_buf, "wlcomp_gpu_compose", 0) : 0;
    pid_t pid = fork();
    if (pid == 0) {
        char *argv[] = { "wlcomp", NULL };
        char *envp_base[] = {
            "HOME=/",
            "PATH=/bin:/usr/bin",
            "XDG_RUNTIME_DIR=/tmp",
            "XV6_GUI_SESSION=1",
            fb_direct_env,
            fb_bo_env,
            gpu_compose_env,
            NULL
        };
        char *envp_cmdline[] = {
            "HOME=/",
            "PATH=/bin:/usr/bin",
            "XDG_RUNTIME_DIR=/tmp",
            "XV6_GUI_SESSION=1",
            fb_direct_env,
            fb_bo_env,
            gpu_compose_env,
            cmdline_env,
            NULL
        };
        snprintf(fb_direct_env, sizeof(fb_direct_env),
                 "XV6_WLCOMP_FB_DIRECT=%d", use_fb_direct ? 1 : 0);
        snprintf(fb_bo_env, sizeof(fb_bo_env), "XV6_WLCOMP_FB_BO=%d",
                 use_fb_bo ? 1 : 0);
        snprintf(gpu_compose_env, sizeof(gpu_compose_env),
                 "XV6_WLCOMP_GPU_COMPOSE=%d", use_gpu_compose ? 1 : 0);
        if (have_cmdline)
            snprintf(cmdline_env, sizeof(cmdline_env), "XV6_KERNEL_CMDLINE=%s",
                     cmdline_buf);
        execve("/bin/wlcomp", argv, have_cmdline ? envp_cmdline : envp_base);
        _exit(127);
    }
    return pid;
}

static pid_t launch_gpu_substrate_validate(void)
{
    pid_t pid = fork();

    if (pid == 0) {
        char *argv[] = {
            "gpu-substrate-validate",
            NULL,
        };
        char *envp[] = {
            "HOME=/",
            "PATH=/bin:/usr/bin",
            "XDG_RUNTIME_DIR=/tmp",
            "XDG_CACHE_HOME=/tmp/.cache",
            "XDG_DATA_DIRS=/share:/usr/share",
            "WAYLAND_DISPLAY=wayland-0",
            "GDK_BACKEND=wayland",
            "LIBGL_ALWAYS_SOFTWARE=0",
            "GALLIUM_DRIVER=virgl",
            "EGL_PLATFORM=wayland",
            "LIBGL_DRIVERS_PATH=/lib/dri",
            "XV6_GUI_SESSION=1",
            NULL,
        };

        execve("/bin/gpu-substrate-validate", argv, envp);
        fprintf(stderr, "gpu-substrate-validate: execve failed errno=%d (%s)\n",
                errno, errno ? strerror(errno) : "no errno from kernel");
        _exit(127);
    }
    if (pid > 0)
        setpgid(pid, pid);
    return pid;
}

static void run_http_smoke_server(void)
{
    static const char plain_body[] =
        "<!doctype html><title>xv6 plain HTTP smoke</title>"
        "<h1>xv6 plain HTTP smoke</h1>\n";
    static const char js_body[] =
        "<!doctype html><meta charset=utf-8>"
        "<title>xv6-js-smoke:boot</title>"
        "<h1 id=out>boot</h1><xv6-smoke></xv6-smoke>"
        "<script>"
        "window.__xv6Smoke=[];"
        "function mark(x){if(__xv6Smoke.indexOf(x)<0)__xv6Smoke.push(x);"
        "document.getElementById('out').textContent=__xv6Smoke.join(',');"
        "document.title='xv6-js-smoke:'+__xv6Smoke.join(',');"
        "console.log('XV6-JS-SMOKE '+__xv6Smoke.join(','));}"
        "function done(){var need=['external','ce','promise','microtask','timeout','raf','idle','fetch','fetch-stream','xhr','domcontent','load'];"
        "if(need.every(function(x){return __xv6Smoke.indexOf(x)>=0;})){document.title='xv6-js-smoke:PASS:'+__xv6Smoke.join(',');console.log('XV6-JS-SMOKE PASS');}}"
        "function hit(x){mark(x);done();}"
        "document.addEventListener('DOMContentLoaded',function(){hit('domcontent');});"
        "window.addEventListener('load',function(){hit('load');});"
        "customElements.define('xv6-smoke',class extends HTMLElement{connectedCallback(){hit('ce');}});"
        "Promise.resolve().then(function(){hit('promise');});"
        "queueMicrotask(function(){hit('microtask');});"
        "setTimeout(function(){hit('timeout');},20);"
        "requestAnimationFrame(function(){hit('raf');});"
        "requestIdleCallback(function(){hit('idle');},{timeout:1000});"
        "mark('fetch-start');fetch('/json').then(function(r){mark('fetch-response');return r.json();}).then(function(j){if(j.ok)hit('fetch');else mark('fetch-bad-json');}).catch(function(e){mark('fetch-error');console.error('XV6-JS-SMOKE fetch '+e);});"
        "mark('stream-start');fetch('/stream').then(function(r){mark('stream-response');if(!r.body||typeof r.body.getReader!=='function')throw new Error('missing body reader');var rd=r.body.getReader();var total=0;function pump(){return rd.read().then(function(x){if(x.done){if(total>0)hit('fetch-stream');else throw new Error('empty stream');return;}total+=x.value?x.value.byteLength:0;mark('stream-chunk');return pump();});}return pump();}).catch(function(e){mark('stream-error');console.error('XV6-JS-SMOKE fetch-stream '+e);});"
        "mark('xhr-start');var x=new XMLHttpRequest();x.onload=function(){mark('xhr-load');if(x.responseText==='ok')hit('xhr');else mark('xhr-bad');};x.onerror=function(){mark('xhr-error');console.error('XV6-JS-SMOKE xhr error');};x.open('GET','/xhr');x.send();"
        "</script><script src=/after.js></script>";
    static const char after_js[] = "hit('external');\n";
    static const char simple_inline_body[] =
        "<!doctype html><title>xv6-simple:boot</title>"
        "<script>document.title='xv6-simple:PASS';console.log('XV6-SIMPLE PASS');</script>";
    static const char media_init_body[] =
        "<!doctype html><meta charset=utf-8>"
        "<title>xv6-media-init:boot</title><pre id=out>boot</pre>"
        "<script>"
        "(function(){var out=document.getElementById('out'),steps=[];"
        "function mark(s){steps.push(s);document.title='xv6-media-init:'+steps.join(',');"
        "out.textContent=steps.join('\\n');console.log('XV6-MEDIA-INIT '+steps.join(','));}"
        "function safe(s,f){mark('before-'+s);try{mark('after-'+s+':'+f());}"
        "catch(e){mark('throw-'+s+':'+e.name+':'+e.message);}}"
        "mark('script');"
        "safe('create-video',function(){return typeof document.createElement('video');});"
        "safe('create-audio',function(){return typeof document.createElement('audio');});"
        "var video=document.createElement('video');"
        "safe('canplay-mp4',function(){return video.canPlayType('video/mp4');});"
        "safe('canplay-avc',function(){return video.canPlayType('video/mp4; codecs=\"avc1.42E01E, mp4a.40.2\"');});"
        "safe('mediasource-type',function(){return typeof window.MediaSource;});"
        "safe('mse-mp4',function(){return window.MediaSource&&MediaSource.isTypeSupported?String(MediaSource.isTypeSupported('video/mp4; codecs=\"avc1.42E01E, mp4a.40.2\"')):'missing';});"
        "safe('attach-src',function(){video.muted=true;video.playsInline=true;video.src='test-mse.mp4';return video.readyState+'/'+video.networkState;});"
        "setTimeout(function(){mark('timeout:'+video.readyState+'/'+video.networkState+':err='+(video.error?video.error.code:0));},500);"
        "})();</script>";
    /*
     * Real end-to-end decode proof: load a locally-served video file, call
     * play(), and report decode progress (currentTime + decoded frame count)
     * at the FRONT of the document title so the host probe sees genuine
     * frame-decode evidence even when truncated. This is NOT a YouTube fake;
     * it exercises the same GStreamer software decode + compositor present
     * path that real playback uses.
     */
    static const char media_play_body[] =
        "<!doctype html><meta charset=utf-8>"
        "<title>xv6-media-play:boot</title>"
        "<body style='margin:0;background:#000'>"
        "<video id=v width=640 height=360 muted playsinline autoplay"
        " style='width:640px;height:360px'></video>"
        "<script>(function(){"
        "var v=document.getElementById('v');var st='init';var maxt=0;var frames=0;"
        "function q(){try{var p=v.getVideoPlaybackQuality?v.getVideoPlaybackQuality():null;"
        "if(p)return p.totalVideoFrames;}catch(e){}"
        "return (typeof v.webkitDecodedFrameCount=='number')?v.webkitDecodedFrameCount:0;}"
        "function report(){frames=q();if(v.currentTime>maxt)maxt=v.currentTime;"
        "document.title='xv6-media-play:t='+maxt.toFixed(2)+',frames='+frames+"
        "',rs='+v.readyState+',ns='+v.networkState+',st='+st+',err='+(v.error?v.error.code:0);"
        "console.log('XV6-MEDIA-PLAY '+document.title);}"
        "v.addEventListener('loadedmetadata',function(){st='meta';report();});"
        "v.addEventListener('canplay',function(){st='canplay';v.play&&v.play().then(function(){st='playing';report();}).catch(function(e){st='playerr:'+e.name;report();});report();});"
        "v.addEventListener('playing',function(){st='playing';report();});"
        "v.addEventListener('timeupdate',function(){st='timeupdate';report();});"
        "v.addEventListener('ended',function(){st='ended';report();});"
        "v.addEventListener('error',function(){st='error';report();});"
        "v.src='long-test.mp4';v.load();"
        "setInterval(report,500);report();"
        "})();</script></body>";
    static const char ytboot_body[] =
        "<!doctype html><meta charset=utf-8>"
        "<title>xv6-ytboot:boot</title><h1 id=out>boot</h1>"
        "<script>"
        "window.__xv6Boot=[];"
        "function mark(x){if(__xv6Boot.indexOf(x)<0)__xv6Boot.push(x);"
        "document.getElementById('out').textContent=__xv6Boot.join(',');"
        "document.title='xv6-ytboot:'+__xv6Boot.join(',');"
        "console.log('XV6-YTBOOT '+__xv6Boot.join(','));done();}"
        "function done(){var need=['small','large','postlarge','browse','domcontent','load'];"
        "if(need.every(function(x){return __xv6Boot.indexOf(x)>=0;})){document.title='xv6-ytboot:PASS:'+__xv6Boot.join(',');console.log('XV6-YTBOOT PASS');}}"
        "document.addEventListener('DOMContentLoaded',function(){mark('domcontent');});"
        "window.addEventListener('load',function(){mark('load');});"
        "</script><script src=/small.js></script><script src=/large.js></script>"
        "<script>mark('postlarge');</script>";
    static const char small_js[] = "mark('small');\n";
    static const char large_js_prefix[] =
        "window.__xv6LargeSeen=1;\n";
    static const char large_js_suffix[] =
        "mark('large');\n"
        "fetch('/youtubei/v1/browse',{method:'POST',headers:{'Content-Type':'application/json'},body:'{}'})"
        ".then(function(r){return r.json();}).then(function(j){if(j.ok)mark('browse');})"
        ".catch(function(e){console.error('XV6-YTBOOT fetch '+e);});\n";
    static const char ytw_body[] =
        "<!doctype html><html><head><meta charset=utf-8>"
        "<title>xv6-ytwaterfall:boot</title>"
        "<script>"
        "window.__xv6Waterfall=[];"
        "function mark(x){if(__xv6Waterfall.indexOf(x)<0)__xv6Waterfall.push(x);"
        "document.title='xv6-ytwaterfall:'+__xv6Waterfall.join(',');"
        "console.log('XV6-YTWATERFALL '+__xv6Waterfall.join(','));done();}"
        "function done(){var need=['kevlar','webanimations','adapter','webcomponents','intersection','i18n','scheduler','spf','network','inline','body','app-connected','domcontent','load','browse'];"
        "if(need.every(function(x){return __xv6Waterfall.indexOf(x)>=0;})){document.title='xv6-ytwaterfall:PASS:'+__xv6Waterfall.join(',');console.log('XV6-YTWATERFALL PASS');}}"
        "document.addEventListener('DOMContentLoaded',function(){mark('domcontent');});"
        "window.addEventListener('load',function(){mark('load');});"
        "</script>"
        "<script src=/yt/kevlar.js></script>"
        "<script src=/yt/webanimations.js></script>"
        "<script src=/yt/adapter.js></script>"
        "<script src=/yt/webcomponents.js></script>"
        "<script src=/yt/intersection.js></script>"
        "<script src=/yt/i18n.js></script>"
        "<script src=/yt/scheduler.js></script>"
        "<script src=/yt/spf.js></script>"
        "<script src=/yt/network.js></script>"
        "<script>"
        "window.ytInitialData={contents:{twoColumnBrowseResultsRenderer:{tabs:[{tabRenderer:{content:{richGridRenderer:{contents:[{richItemRenderer:{content:{videoRenderer:{videoId:'xv6'}}}}]}}}}]}}};"
        "customElements.define('ytd-app',class extends HTMLElement{connectedCallback(){mark('app-connected');}});"
        "mark('inline');"
        "fetch('/youtubei/v1/browse',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({context:{client:{clientName:'WEB'}}})})"
        ".then(function(r){return r.json();}).then(function(j){if(j.ok)mark('browse');})"
        ".catch(function(e){console.error('XV6-YTWATERFALL fetch '+e);});"
        "</script></head><body><ytd-app><main id=feed><ytd-rich-grid-renderer>"
        "<ytd-rich-item-renderer><ytd-video-renderer><img src=/thumb.jpg></ytd-video-renderer></ytd-rich-item-renderer>"
        "</ytd-rich-grid-renderer></main></ytd-app>"
        "<script>mark('body');</script></body></html>";
    static const char ytw_kevlar_js[] =
        "window.ytcfg={data_:{EMERGENCY_BASE_URL:'http://127.0.0.1:18080',WEB_PLAYER_CONTEXT_CONFIGS:{WEB_PLAYER_CONTEXT_CONFIG_ID_KEVLAR_WATCH:{}}}};mark('kevlar');\n";
    static const char ytw_webanimations_js[] = "mark('webanimations');\n";
    static const char ytw_adapter_js[] = "mark('adapter');\n";
    static const char ytw_webcomponents_js[] = "window.ShadyCSS={};window.Polymer={};mark('webcomponents');\n";
    static const char ytw_intersection_js[] = "if(!window.IntersectionObserver)throw new Error('missing IntersectionObserver');mark('intersection');\n";
    static const char ytw_i18n_js[] = "window.yt={};mark('i18n');\n";
    static const char ytw_scheduler_js[] =
        "Promise.resolve().then(function(){mark('scheduler');});\n";
    static const char ytw_spf_js[] = "mark('spf');\n";
    static const char ytw_network_js[] = "mark('network');\n";
    static const char feature_gate_body[] =
        "<!doctype html><meta charset=utf-8>"
        "<title>xv6-feature-gates:boot</title><h1 id=out>boot</h1>"
        "<script>"
        "window.__xv6Feature=[];"
        "function mark(x){if(__xv6Feature.indexOf(x)<0)__xv6Feature.push(x);"
        "document.getElementById('out').textContent=__xv6Feature.join(',');"
        "document.title='xv6-feature-gates:'+__xv6Feature.join(',');"
        "console.log('XV6-FEATURE-GATES '+__xv6Feature.join(','));done();}"
        "function fail(x,e){document.title='xv6-feature-gates:FAIL:'+x;"
        "console.error('XV6-FEATURE-GATES FAIL '+x+' '+(e&&e.stack||e));}"
        "function done(){var need=['crypto','digest','offscreen','domcontent','load'];"
        "if(need.every(function(x){return __xv6Feature.indexOf(x)>=0;})){"
        "document.title='xv6-feature-gates:PASS:'+__xv6Feature.join(',');"
        "console.log('XV6-FEATURE-GATES PASS');}}"
        "document.addEventListener('DOMContentLoaded',function(){mark('domcontent');});"
        "window.addEventListener('load',function(){mark('load');});"
        "try{if(!window.crypto||!crypto.subtle)throw new Error('missing crypto.subtle');"
        "mark('crypto');crypto.subtle.digest('SHA-256',new Uint8Array([1,2,3])).then(function(){mark('digest');}).catch(function(e){fail('digest',e);});}"
        "catch(e){fail('crypto',e);}"
        "try{if(!window.OffscreenCanvas)throw new Error('missing OffscreenCanvas');"
        "var c=new OffscreenCanvas(8,8);var ctx=c.getContext('2d');ctx.fillRect(0,0,1,1);mark('offscreen');}"
        "catch(e){fail('offscreen',e);}"
        "</script>";
    static const char idle_browse_body[] =
        "<!doctype html><meta charset=utf-8>"
        "<title>xv6-idle-browse:boot</title><h1 id=out>boot</h1><ytd-app></ytd-app>"
        "<script>"
        "window.__xv6Idle=[];"
        "function mark(x){if(__xv6Idle.indexOf(x)<0)__xv6Idle.push(x);"
        "document.getElementById('out').textContent=__xv6Idle.join(',');"
        "document.title='xv6-idle-browse:'+__xv6Idle.join(',');"
        "console.log('XV6-IDLE-BROWSE '+__xv6Idle.join(','));done();}"
        "function fail(x,e){document.title='xv6-idle-browse:FAIL:'+x;"
        "console.error('XV6-IDLE-BROWSE FAIL '+x+' '+(e&&e.stack||e));}"
        "function done(){var need=['ce','idle-api','idle-run','browse','domcontent','load'];"
        "if(need.every(function(x){return __xv6Idle.indexOf(x)>=0;})){"
        "document.title='xv6-idle-browse:PASS:'+__xv6Idle.join(',');"
        "console.log('XV6-IDLE-BROWSE PASS');}}"
        "document.addEventListener('DOMContentLoaded',function(){mark('domcontent');});"
        "window.addEventListener('load',function(){mark('load');});"
        "customElements.define('ytd-app',class extends HTMLElement{connectedCallback(){mark('ce');}});"
        "try{if(typeof requestIdleCallback!=='function')throw new Error('missing requestIdleCallback');mark('idle-api');"
        "requestIdleCallback(function(deadline){try{if(!deadline||typeof deadline.timeRemaining!=='function')throw new Error('bad deadline');"
        "mark('idle-run');fetch('/youtubei/v1/browse',{method:'POST',headers:{'Content-Type':'application/json'},body:'{}'})"
        ".then(function(r){return r.json();}).then(function(j){if(j.ok)mark('browse');else fail('browse-status','bad json');})"
        ".catch(function(e){fail('browse-fetch',e);});}catch(e){fail('idle-run',e);}}, {timeout:500});}"
        "catch(e){fail('idle-api',e);}"
        "</script>";
    static const char compat_gate_body[] =
        "<!doctype html><meta charset=utf-8>"
        "<title>xv6-compat-gates:boot</title><h1 id=out>boot</h1><ytd-app></ytd-app>"
        "<script>"
        "window.__xv6Compat=[];"
        "function mark(x){if(__xv6Compat.indexOf(x)<0)__xv6Compat.push(x);"
        "document.getElementById('out').textContent=__xv6Compat.join(',');"
        "document.title='xv6-compat-gates:'+__xv6Compat.join(',');"
        "console.log('XV6-COMPAT-GATES '+__xv6Compat.join(','));done();}"
        "function fail(x,e){document.title='xv6-compat-gates:FAIL:'+x;"
        "console.error('XV6-COMPAT-GATES FAIL '+x+' '+(e&&e.stack||e));}"
        "function done(){var need=['ce','tt-api','tt-policy','ua-api','ua-high','idle-run','browse','domcontent','load'];"
        "if(need.every(function(x){return __xv6Compat.indexOf(x)>=0;})){"
        "document.title='xv6-compat-gates:PASS:'+__xv6Compat.join(',');"
        "console.log('XV6-COMPAT-GATES PASS');}}"
        "document.addEventListener('DOMContentLoaded',function(){mark('domcontent');});"
        "window.addEventListener('load',function(){mark('load');});"
        "customElements.define('ytd-app',class extends HTMLElement{connectedCallback(){mark('ce');}});"
        "try{if(!window.trustedTypes||typeof trustedTypes.createPolicy!=='function')throw new Error('missing trustedTypes');"
        "mark('tt-api');var p=trustedTypes.createPolicy('xv6',{createHTML:function(s){return String(s).replace('bad','good');},createScript:function(s){return String(s);},createScriptURL:function(s){return String(s);}});"
        "if(p.createHTML('bad')!=='good')throw new Error('bad Trusted Types policy');"
        "if(trustedTypes.createPolicy('xv6',{createHTML:function(s){return String(s);}})!==p)throw new Error('bad duplicate policy reuse');mark('tt-policy');}"
        "catch(e){fail('trusted-types',e);}"
        "try{if(!navigator.userAgentData||typeof navigator.userAgentData.getHighEntropyValues!=='function')throw new Error('missing userAgentData');"
        "mark('ua-api');navigator.userAgentData.getHighEntropyValues(['architecture','bitness','fullVersionList','platformVersion','uaFullVersion']).then(function(v){"
        "if(!v||!v.architecture||!v.bitness||!v.fullVersionList)throw new Error('bad high entropy values');mark('ua-high');}).catch(function(e){fail('ua-high',e);});}"
        "catch(e){fail('ua-api',e);}"
        "try{requestIdleCallback(function(){mark('idle-run');fetch('/youtubei/v1/browse',{method:'POST',headers:{'Content-Type':'application/json'},body:'{}'})"
        ".then(function(r){return r.json();}).then(function(j){if(j.ok)mark('browse');else fail('browse-status','bad json');})"
        ".catch(function(e){fail('browse-fetch',e);});},{timeout:500});}"
        "catch(e){fail('idle-run',e);}"
        "</script>";
    static const char json_body[] = "{\"ok\":true}\n";
    static const char xhr_body[] = "ok";
    static const char stream_body[] =
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n"
        "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210\n";
    int coop = webkit_coop_smoke_enabled_by_cmdline();
    int js_smoke = webkit_js_smoke_enabled_by_cmdline();
    int ytboot_smoke = webkit_youtube_boot_smoke_enabled_by_cmdline();
    int ytw_smoke = webkit_youtube_waterfall_smoke_enabled_by_cmdline();
    int feature_gate_smoke = webkit_feature_gate_smoke_enabled_by_cmdline();
    int idle_browse_smoke = webkit_idle_browse_smoke_enabled_by_cmdline();
    int compat_gate_smoke = webkit_compat_gate_smoke_enabled_by_cmdline();
    char header[384];
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    struct sockaddr_in addr;

    if (fd < 0) {
        fprintf(stderr, "[desktop] HTTP smoke socket failed errno=%d (%s)\n",
                errno, strerror(errno));
        _exit(1);
    }

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(18080);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[desktop] HTTP smoke bind failed errno=%d (%s)\n",
                errno, strerror(errno));
        _exit(1);
    }
    if (listen(fd, 4) < 0) {
        fprintf(stderr, "[desktop] HTTP smoke listen failed errno=%d (%s)\n",
                errno, strerror(errno));
        _exit(1);
    }
    fprintf(stderr, "[desktop] HTTP smoke listening on 127.0.0.1:18080\n");

    for (;;) {
        char req[2048];
        const char *path = "/";
        const char *body = plain_body;
        const char *ctype = "text/html";
        const char *extra = coop ? "Cross-Origin-Opener-Policy: same-origin\r\n" : "";
        size_t used = 0;
        int cfd = accept(fd, NULL, NULL);
        if (cfd < 0) {
            if (errno == EINTR)
                continue;
            fprintf(stderr, "[desktop] HTTP smoke accept failed errno=%d (%s)\n",
                    errno, strerror(errno));
            break;
        }
        http_smoke_request_count++;
        {
            char count_buf[32];
            int count_fd = open("/tmp/http-smoke-count",
                                O_WRONLY | O_CREAT | O_TRUNC, 0644);

            if (count_fd >= 0) {
                int count_len = snprintf(count_buf, sizeof(count_buf), "%d\n",
                                         http_smoke_request_count);
                if (count_len > 0)
                    write(count_fd, count_buf, (size_t)count_len);
                close(count_fd);
            }
        }
        while (used + 1 < sizeof(req)) {
            int n = read(cfd, req + used, sizeof(req) - used - 1);

            if (n <= 0)
                break;
            used += (size_t)n;
            req[used] = '\0';
            if (strstr(req, "\r\n\r\n"))
                break;
        }
        char *header_end = strstr(req, "\r\n\r\n");
        size_t body_seen = 0;
        size_t content_length = 0;

        if (header_end) {
            char *cl = strstr(req, "Content-Length:");

            if (!cl)
                cl = strstr(req, "content-length:");
            body_seen = used - (size_t)((header_end + 4) - req);
            if (cl) {
                cl = strchr(cl, ':');
                if (cl) {
                    cl++;
                    while (*cl == ' ' || *cl == '\t')
                        cl++;
                    content_length = strtoul(cl, NULL, 10);
                }
            }
        }
        while (body_seen < content_length) {
            char drain[512];
            size_t want = content_length - body_seen;
            int n;

            if (want > sizeof(drain))
                want = sizeof(drain);
            n = read(cfd, drain, want);
            if (n <= 0)
                break;
            body_seen += (size_t)n;
        }
        char range_buf[128];
        range_buf[0] = '\0';
        {
            /* Extract Range header before path parsing null-terminates req. */
            const char *r = strstr(req, "Range:");

            if (!r)
                r = strstr(req, "range:");
            if (r) {
                r += 6;
                while (*r == ' ' || *r == '\t')
                    r++;
                size_t i = 0;
                while (r[i] && r[i] != '\r' && r[i] != '\n' &&
                       i + 1 < sizeof(range_buf)) {
                    range_buf[i] = r[i];
                    i++;
                }
                range_buf[i] = '\0';
            }
        }
        if (strncmp(req, "GET ", 4) == 0 || strncmp(req, "POST ", 5) == 0) {
            path = req + (req[0] == 'G' ? 4 : 5);
            char *end = strchr(path, ' ');
            if (end)
                *end = '\0';
            char *query = strchr(path, '?');
            if (query)
                *query = '\0';
        }
        fprintf(stderr, "[desktop] HTTP smoke request %s\n", path);
        if (http_try_serve_webkit_file(cfd, path, extra,
                                       range_buf[0] ? range_buf : NULL)) {
            close(cfd);
            continue;
        }
        if (strcmp(path, "/simple-inline.html") == 0) {
            body = simple_inline_body;
            ctype = "text/html";
        } else if (strcmp(path, "/media-init-inline.html") == 0) {
            body = media_init_body;
            ctype = "text/html";
        } else if (strcmp(path, "/media-play-inline.html") == 0) {
            body = media_play_body;
            ctype = "text/html";
        }
        if (ytboot_smoke && strcmp(path, "/large.js") == 0) {
            static const char pad[] =
                "                                                                ";
            const unsigned pad_bytes = 10 * 1024 * 1024;
            unsigned content_length = strlen(large_js_prefix) + pad_bytes +
                strlen(large_js_suffix);

            snprintf(header, sizeof(header),
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: text/javascript\r\n"
                     "Content-Length: %u\r\n"
                     "%s"
                     "Connection: close\r\n\r\n",
                     content_length, extra);
            if (write_all_fd(cfd, header, strlen(header)) != 0 ||
                write_all_fd(cfd, large_js_prefix,
                             strlen(large_js_prefix)) != 0) {
                close(cfd);
                continue;
            }
            for (unsigned sent = 0; sent < pad_bytes; ) {
                unsigned n = sizeof(pad) - 1;

                if (n > pad_bytes - sent)
                    n = pad_bytes - sent;
                if (write_all_fd(cfd, pad, n) != 0)
                    break;
                sent += n;
            }
            (void)write_all_fd(cfd, large_js_suffix,
                               strlen(large_js_suffix));
            close(cfd);
            continue;
        }
        if (compat_gate_smoke) {
            if (strcmp(path, "/youtubei/v1/browse") == 0) {
                body = json_body;
                ctype = "application/json";
            } else {
                body = compat_gate_body;
            }
        } else if (idle_browse_smoke) {
            if (strcmp(path, "/youtubei/v1/browse") == 0) {
                body = json_body;
                ctype = "application/json";
            } else {
                body = idle_browse_body;
            }
        } else if (feature_gate_smoke) {
            body = feature_gate_body;
        } else if (ytw_smoke) {
            if (strncmp(path, "/yt/", 4) == 0) {
                ctype = "text/javascript";
                if (strcmp(path, "/yt/kevlar.js") == 0) {
                    usleep(250000);
                    body = ytw_kevlar_js;
                } else if (strcmp(path, "/yt/webanimations.js") == 0) {
                    body = ytw_webanimations_js;
                } else if (strcmp(path, "/yt/adapter.js") == 0) {
                    body = ytw_adapter_js;
                } else if (strcmp(path, "/yt/webcomponents.js") == 0) {
                    usleep(150000);
                    body = ytw_webcomponents_js;
                } else if (strcmp(path, "/yt/intersection.js") == 0) {
                    body = ytw_intersection_js;
                } else if (strcmp(path, "/yt/i18n.js") == 0) {
                    body = ytw_i18n_js;
                } else if (strcmp(path, "/yt/scheduler.js") == 0) {
                    body = ytw_scheduler_js;
                } else if (strcmp(path, "/yt/spf.js") == 0) {
                    usleep(100000);
                    body = ytw_spf_js;
                } else if (strcmp(path, "/yt/network.js") == 0) {
                    body = ytw_network_js;
                } else {
                    body = "";
                }
            } else if (strcmp(path, "/youtubei/v1/browse") == 0) {
                body = json_body;
                ctype = "application/json";
            } else if (strcmp(path, "/thumb.jpg") == 0) {
                body = "";
                ctype = "image/jpeg";
            } else {
                body = ytw_body;
            }
        } else if (ytboot_smoke) {
            if (strcmp(path, "/small.js") == 0) {
                body = small_js;
                ctype = "text/javascript";
            } else if (strcmp(path, "/youtubei/v1/browse") == 0) {
                body = json_body;
                ctype = "application/json";
            } else {
                body = ytboot_body;
            }
        } else if (js_smoke) {
            if (strcmp(path, "/after.js") == 0) {
                body = after_js;
                ctype = "text/javascript";
            } else if (strcmp(path, "/json") == 0) {
                body = json_body;
                ctype = "application/json";
            } else if (strcmp(path, "/xhr") == 0) {
                body = xhr_body;
                ctype = "text/plain";
            } else if (strcmp(path, "/stream") == 0) {
                body = stream_body;
                ctype = "application/octet-stream";
            } else {
                body = js_body;
            }
        }
        snprintf(header, sizeof(header),
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Type: %s\r\n"
                 "Content-Length: %u\r\n"
                 "%s"
                 "Connection: close\r\n\r\n",
                 ctype, (unsigned)strlen(body), extra);
        (void)write_all_fd(cfd, header, strlen(header));
        (void)write_all_fd(cfd, body, strlen(body));
        close(cfd);
    }
    close(fd);
    _exit(0);
}

static int write_all_fd(int fd, const void *buf, size_t len)
{
    const char *p = buf;

    while (len > 0) {
        ssize_t n = write(fd, p, len);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0) {
            errno = EIO;
            return -1;
        }
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static const char *http_content_type_for_path(const char *path)
{
    const char *ext = strrchr(path, '.');

    if (!ext)
        return "application/octet-stream";
    if (strcmp(ext, ".html") == 0)
        return "text/html";
    if (strcmp(ext, ".js") == 0)
        return "text/javascript";
    if (strcmp(ext, ".json") == 0)
        return "application/json";
    if (strcmp(ext, ".mp4") == 0)
        return "video/mp4";
    if (strcmp(ext, ".webm") == 0)
        return "video/webm";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0)
        return "image/jpeg";
    if (strcmp(ext, ".png") == 0)
        return "image/png";
    if (strcmp(ext, ".css") == 0)
        return "text/css";
    return "application/octet-stream";
}

static int http_try_serve_webkit_file(int cfd, const char *path,
                                      const char *extra, const char *range)
{
    char fs_path[256];
    char header[512];
    char buf[8192];
    struct stat st;
    int fd;
    long long file_size;
    long long start = 0;
    long long end;        /* inclusive */
    long long remaining;
    int partial = 0;

    if (!path || path[0] != '/' || strcmp(path, "/") == 0 ||
        strstr(path, "..") != NULL)
        return 0;
    if (snprintf(fs_path, sizeof(fs_path), "/share/webkit%s", path) >=
        (int)sizeof(fs_path))
        return 0;
    fd = open(fs_path, O_RDONLY);
    if (fd < 0)
        return 0;
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        return 0;
    }

    file_size = (long long)st.st_size;
    end = file_size - 1;

    /*
     * Honour a single "bytes=start-end" range request. Media engines
     * (WebKit/GStreamer) issue byte-range requests to read the moov atom
     * and seek; without 206 support, non-faststart MP4s fail to load
     * (HTMLMediaElement error code 4, SRC_NOT_SUPPORTED).
     */
    if (range) {
        const char *p = strstr(range, "bytes=");

        if (p) {
            p += 6;
            char *q = NULL;
            long long rs = strtoll(p, &q, 10);
            long long re = -1;

            if (q && *q == '-') {
                const char *after = q + 1;

                if (*after >= '0' && *after <= '9')
                    re = strtoll(after, NULL, 10);
            }
            if (q && q != p) {
                /* "bytes=start-" or "bytes=start-end" */
                start = rs;
            } else if (q && *q == '-') {
                /* "bytes=-suffix": last N bytes */
                start = (re >= 0 && re < file_size) ? file_size - re : 0;
                re = file_size - 1;
            }
            if (start < 0)
                start = 0;
            if (start >= file_size)
                start = file_size > 0 ? file_size - 1 : 0;
            if (re >= start && re < file_size)
                end = re;
            else
                end = file_size - 1;
            partial = 1;
        }
    }

    if (partial && start > 0)
        lseek(fd, (off_t)start, SEEK_SET);
    remaining = end - start + 1;
    if (remaining < 0)
        remaining = 0;

    if (partial) {
        snprintf(header, sizeof(header),
                 "HTTP/1.1 206 Partial Content\r\n"
                 "Content-Type: %s\r\n"
                 "Accept-Ranges: bytes\r\n"
                 "Content-Range: bytes %lld-%lld/%lld\r\n"
                 "Content-Length: %lld\r\n"
                 "%s"
                 "Connection: close\r\n\r\n",
                 http_content_type_for_path(fs_path), start, end, file_size,
                 remaining, extra);
    } else {
        snprintf(header, sizeof(header),
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Type: %s\r\n"
                 "Accept-Ranges: bytes\r\n"
                 "Content-Length: %lld\r\n"
                 "%s"
                 "Connection: close\r\n\r\n",
                 http_content_type_for_path(fs_path), file_size, extra);
    }
    if (write_all_fd(cfd, header, strlen(header)) < 0) {
        fprintf(stderr, "[desktop] HTTP file header write failed %s errno=%d (%s)\n",
                path, errno, strerror(errno));
        close(fd);
        return 1;
    }
    while (remaining > 0) {
        size_t want = sizeof(buf);
        ssize_t n;

        if ((long long)want > remaining)
            want = (size_t)remaining;
        n = read(fd, buf, want);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            fprintf(stderr, "[desktop] HTTP file read failed %s errno=%d (%s)\n",
                    path, errno, strerror(errno));
            break;
        }
        if (n == 0)
            break;
        if (write_all_fd(cfd, buf, (size_t)n) < 0) {
            fprintf(stderr, "[desktop] HTTP file body write failed %s errno=%d (%s)\n",
                    path, errno, strerror(errno));
            break;
        }
        remaining -= n;
    }
    close(fd);
    fprintf(stderr, "[desktop] HTTP file served %s bytes=%lld range=%s [%lld-%lld/%lld]\n",
            path, end - start + 1, partial ? "y" : "n", start, end, file_size);
    return 1;
}

static pid_t launch_http_smoke_server(void)
{
    pid_t pid = fork();

    if (pid == 0)
        run_http_smoke_server();
    return pid;
}

static void http_smoke_self_probe(const char *path)
{
    char req[512];
    char buf[128];
    struct sockaddr_in addr;
    int fd;
    ssize_t n;

    if (!path || path[0] != '/')
        path = "/";
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "[desktop] HTTP smoke self-probe socket failed errno=%d (%s)\n",
                errno, strerror(errno));
        return;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(18080);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[desktop] HTTP smoke self-probe connect failed errno=%d (%s)\n",
                errno, strerror(errno));
        close(fd);
        return;
    }
    snprintf(req, sizeof(req),
             "GET %s HTTP/1.0\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n",
             path);
    if (write_all_fd(fd, req, strlen(req)) < 0) {
        fprintf(stderr, "[desktop] HTTP smoke self-probe write failed errno=%d (%s)\n",
                errno, strerror(errno));
        close(fd);
        return;
    }
    n = read(fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        fprintf(stderr, "[desktop] HTTP smoke self-probe response %.64s\n",
                buf);
    } else {
        fprintf(stderr, "[desktop] HTTP smoke self-probe read failed n=%zd errno=%d (%s)\n",
                n, errno, strerror(errno));
    }
    close(fd);
}

static void webkit_read_line(const char *path, char *buf, size_t buf_size)
{
    int fd;
    ssize_t n;

    if (buf_size == 0)
        return;
    buf[0] = '\0';
    fd = open(path, O_RDONLY);
    if (fd < 0)
        return;
    n = read(fd, buf, buf_size - 1);
    close(fd);
    if (n <= 0)
        return;
    buf[n] = '\0';
    for (ssize_t i = 0; i < n; i++) {
        if (buf[i] == '\n' || buf[i] == '\r') {
            buf[i] = '\0';
            break;
        }
    }
}

static void webkit_dump_gst_debug_evidence(void)
{
    static int probe_calls;
    static int dumped;
    struct stat st;
    int fd;
    static char line[4096];
    size_t len = 0;
    char ch;
    int printed = 0;

    /* Wait a few probes so the media error has been logged, then dump once. */
    if (dumped || ++probe_calls < 5)
        return;
    if (stat("/tmp/gst-debug.log", &st) != 0 || st.st_size <= 0)
        return;
    fd = open("/tmp/gst-debug.log", O_RDONLY);
    if (fd < 0)
        return;
    while (read(fd, &ch, 1) == 1) {
        if (ch == '\n' || len + 1 >= sizeof(line)) {
            line[len] = '\0';
            if (strstr(line, "ERROR") || strstr(line, "WARN") ||
                strstr(line, "missing") || strstr(line, "not-linked") ||
                strstr(line, "no suitable") || strstr(line, "No decoder") ||
                strstr(line, "no decoder") || strstr(line, "Stopping resource") ||
                strstr(line, "could not") || strstr(line, "failed") ||
                strstr(line, "reason")) {
                fprintf(stderr, "[desktop] GST: %s\n", line);
                printed = 1;
            }
            len = 0;
            continue;
        }
        if (ch != '\r')
            line[len++] = ch;
    }
    close(fd);
    if (!printed)
        fprintf(stderr,
                "[desktop] GST debug log present (%ld bytes) but no error/warn lines\n",
                (long)st.st_size);
    dumped = 1;
}

static void webkit_print_runtime_probe(void)
{
    char title[1024];
    char count[32];
    struct stat log_st;
    long log_size = -1;
    static long last_log_size = -2;
    static int stable_log_samples;
    static int excerpt_printed;

    webkit_read_line("/tmp/webkit-title", title, sizeof(title));
    webkit_read_line("/tmp/http-smoke-count", count, sizeof(count));
    if (stat("/tmp/webkit_log.txt", &log_st) == 0)
        log_size = (long)log_st.st_size;
    fprintf(stderr,
            "[desktop] WebKit probe title='%s' http_requests=%s log_bytes=%ld\n",
            title[0] ? title : "(none)",
            count[0] ? count : "(none)",
            log_size);
    webkit_dump_gst_debug_evidence();
    if (log_size >= 0 && log_size == last_log_size)
        stable_log_samples++;
    else {
        stable_log_samples = 0;
        excerpt_printed = 0;
    }
    last_log_size = log_size;

    if (!excerpt_printed && stable_log_samples >= 2 && log_size > 0) {
        int fd = open("/tmp/webkit_log.txt", O_RDONLY);
        if (fd >= 0) {
            char buf[2049];
            ssize_t n;
            off_t off = log_size > 2048 ? (off_t)log_size - 2048 : 0;

            lseek(fd, off, SEEK_SET);
            n = read(fd, buf, sizeof(buf) - 1);
            close(fd);
            if (n > 0) {
                buf[n] = '\0';
                fprintf(stderr,
                        "[desktop] WebKit stalled log tail (%ld bytes):\n%s\n",
                        log_size, buf);
                excerpt_printed = 1;
            }
        }
        webkit_dump_gst_debug_evidence();
    }
}

static int webkit_log_line_is_evidence(const char *line)
{
    return strstr(line, "webkitgpusmoke: gpu-contract") ||
           strstr(line, "webkitgpusmoke: title=xv6 WebKit WebGL") ||
           strstr(line, "webkitgpusmoke: complete") ||
           strstr(line, "webkitgpusmoke: GPU contract validation failed");
}

static void webkit_print_log_evidence(const char *reason)
{
    int fd;
    char line[4096];
    size_t len = 0;
    int printed = 0;
    char ch;

    fd = open("/tmp/webkit_log.txt", O_RDONLY);
    if (fd < 0) {
        fprintf(stderr,
                "[desktop] WebKit log evidence unavailable reason=%s errno=%d (%s)\n",
                reason ? reason : "(none)", errno, strerror(errno));
        return;
    }

    while (read(fd, &ch, 1) == 1) {
        if (ch == '\n' || len + 1 >= sizeof(line)) {
            line[len] = '\0';
            if (webkit_log_line_is_evidence(line)) {
                fprintf(stderr,
                        "[desktop] WebKit log evidence reason=%s: %s\n",
                        reason ? reason : "(none)", line);
                printed = 1;
            }
            len = 0;
            continue;
        }
        if (ch != '\r')
            line[len++] = ch;
    }
    close(fd);

    if (len > 0) {
        line[len] = '\0';
        if (webkit_log_line_is_evidence(line)) {
            fprintf(stderr,
                    "[desktop] WebKit log evidence reason=%s: %s\n",
                    reason ? reason : "(none)", line);
            printed = 1;
        }
    }
    if (!printed) {
        fprintf(stderr,
                "[desktop] WebKit log evidence reason=%s: no contract/title lines found\n",
                reason ? reason : "(none)");
    }
}

static pid_t launch_gst_registry_warmup(void)
{
    pid_t pid = fork();

    if (pid == 0) {
        char *argv[] = {
            "gst-inspect-1.0",
            "--gst-disable-registry-fork",
            "coreelements",
            "typefindfunctions",
            "playback",
            "isomp4",
            "matroska",
            "libav",
            "vpx",
            "opus",
            "ogg",
            "videoconvertscale",
            "audioconvert",
            NULL,
        };
        char *envp[] = {
            "HOME=/",
            "PATH=/bin:/usr/bin",
            "LD_LIBRARY_PATH=/lib:/usr/lib:/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu",
            "XDG_RUNTIME_DIR=/tmp",
            "XDG_CACHE_HOME=/tmp/.cache",
            "XDG_DATA_DIRS=/share:/usr/share",
            "GST_PLUGIN_SYSTEM_PATH_1_0=/lib/gstreamer-1.0:/usr/lib/gstreamer-1.0",
            "GST_PLUGIN_PATH_1_0=/lib/gstreamer-1.0:/usr/lib/gstreamer-1.0",
            "GST_PLUGIN_SCANNER=/libexec/gstreamer-1.0/gst-plugin-scanner",
            "GST_PLUGIN_SCANNER_1_0=/libexec/gstreamer-1.0/gst-plugin-scanner",
            "GST_REGISTRY=/tmp/gstreamer-registry.bin",
            "GST_REGISTRY_REUSE_PLUGIN_SCANNER=1",
            NULL
        };
        int logfd;
        int nullfd;

        disable_child_coredumps();
        mkdir("/tmp/.cache", 0755);
        mkdir("/tmp/.cache/gstreamer-1.0", 0755);
        nullfd = open("/dev/null", O_WRONLY);
        if (nullfd >= 0) {
            dup2(nullfd, 1);
            close(nullfd);
        }
        logfd = open("/tmp/gst-warmup.log",
                     O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (logfd >= 0) {
            dup2(logfd, 2);
            close(logfd);
        }
        execve("/bin/gst-inspect-1.0", argv, envp);
        fprintf(stderr, "gst-inspect-1.0: execve failed errno=%d (%s)\n",
                errno, errno ? strerror(errno) : "no errno from kernel");
        _exit(127);
    }
    if (pid > 0)
        setpgid(pid, pid);
    return pid;
}

static void wait_for_gst_registry_warmup(int wait_us)
{
    const int step_us = 100000;
    int waited = 0;

    if (gst_warmup_pid <= 0)
        return;

    while (waited <= wait_us) {
        int status;
        pid_t exited = waitpid(gst_warmup_pid, &status, WNOHANG);

        desktop_process_pending_signals();
        if (!g_running)
            return;

        if (exited == gst_warmup_pid) {
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                gst_registry_ready = 1;
                fprintf(stderr,
                        "[desktop] GStreamer registry warmup complete\n");
            } else {
                fprintf(stderr,
                        "[desktop] GStreamer registry warmup exited "
                        "(status %d)\n",
                        WIFEXITED(status) ? WEXITSTATUS(status) : status);
            }
            gst_warmup_pid = 0;
            return;
        }
        if (exited < 0 && errno == ECHILD) {
            gst_warmup_pid = 0;
            return;
        }
        if (waited >= wait_us)
            break;
        usleep(step_us);
        waited += step_us;
    }

    fprintf(stderr,
            "[desktop] GStreamer registry warmup still running after %d ms; "
            "continuing WebKit launch\n",
            wait_us / 1000);
}

static pid_t launch_client(const char *path, const char *name, const char *arg1,
                           const char *arg2, const char *arg3)
{
    int is_netsurf = strcmp(name, "netsurf") == 0;
    int is_minibrowser = strcmp(name, "MiniBrowser") == 0;
    int is_webkitgpusmoke = strcmp(name, "webkitgpusmoke") == 0;
    int is_mesa_gl = strcmp(name, "mesawlegl") == 0 ||
                     strcmp(name, "mesaglsmoke") == 0 ||
                     strcmp(name, "mesaeglinfo") == 0;
    int is_webkit = is_minibrowser || is_webkitgpusmoke;

    if (is_netsurf || is_webkit) {
        mkdir("/tmp/.cache", 0755);
        mkdir("/tmp/.cache/fontconfig", 0755);
        mkdir("/tmp/.local", 0755);
        mkdir("/tmp/.local/share", 0755);
        mkdir("/tmp/webkitgtk-4.1", 0755);
        /*
         * GStreamer's downloadbuffer element (used by WebKit for progressive
         * media) writes scratch files to /var/tmp/WebKit-Media-XXXXXX. Ensure
         * the directory exists and is writable, otherwise media loads fail
         * with HTMLMediaElement error code 4 (temp-file creation failure).
         */
        mkdir("/var", 0755);
        mkdir("/var/tmp", 01777);
        chmod("/var/tmp", 01777);
    }
    if (is_netsurf)
        mkdir("/.netsurf", 0755);

    if (is_webkit) {
        int requested_accel = webkit_accel_enabled_by_cmdline();
        int dmabuf_requested = is_minibrowser &&
            webkit_dmabuf_enabled_by_cmdline();
        int opengl_submit_available = xv6_opengl_submit_available();
        int dxg_transport_available = xv6_dxg_transport_available();
        int render_node_available = xv6_render_node_available();
        struct webkit_gpu_contract_state contract;
        int effective_accel = requested_accel;

        compute_webkit_gpu_contract(opengl_submit_available,
                                    dxg_transport_available,
                                    render_node_available,
                                    NULL,
                                    &contract);
        if (dmabuf_requested && !render_node_available)
            dmabuf_requested = 0;
        if (dmabuf_requested && !contract.validated_shared_surface)
            dmabuf_requested = 0;
        if (effective_accel && !contract.validated_shared_surface &&
            !dmabuf_requested)
            effective_accel = 0;
        if (dmabuf_requested)
            effective_accel = 1;

        write_webkit_gpu_policy_file(name, requested_accel, effective_accel,
                                     opengl_submit_available,
                                     dxg_transport_available,
                                     render_node_available,
                                     dmabuf_requested, &contract);
    }

    pid_t pid = fork();
    if (pid == 0) {
        if (is_webkit)
            disable_child_coredumps();

        if (is_netsurf || is_webkit) {
            const char *log_path = "/tmp/app_log.txt";
            int log_flags = O_WRONLY | O_CREAT | O_TRUNC;

            if (is_webkit) {
                if (webkit_logging_enabled_by_cmdline()) {
                    log_path = "/tmp/webkit_log.txt";
                } else {
                    log_path = "/dev/null";
                    log_flags = O_WRONLY;
                }
            }

            int logfd = open(log_path, log_flags, 0644);
            if (logfd >= 0) {
                dup2(logfd, 1);
                dup2(logfd, 2);
                close(logfd);
            }
        }

        if (is_netsurf) {
            int fd = open("/.netsurf/Choices",
                          O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd >= 0) {
                const char *ch =
                    "ca_bundle:/share/netsurf/ca-bundle\n"
                    "homepage_url:file:///share/netsurf/welcome.html\n"
                    "curl_fetch_timeout:30\n";
                write(fd, ch, strlen(ch));
                close(fd);
            }
        }

        char *argv_default[] = {
            (char *)name,
            (char *)arg1,
            (char *)arg2,
            (char *)arg3,
            NULL,
        };
        if (arg1 == NULL)
            argv_default[1] = NULL;
        if (arg2 == NULL)
            argv_default[2] = NULL;
        if (arg3 == NULL)
            argv_default[3] = NULL;
        const char *minibrowser_url = arg1 ? arg1 : WEBKIT_DEFAULT_URL;
        int minibrowser_youtube_compat =
            is_minibrowser && webkit_youtube_compat_url(minibrowser_url);
        const char *minibrowser_feature_flags =
            webkit_request_idle_disabled_by_cmdline() ?
            webkit_feature_flags_no_idle : webkit_feature_flags;
        char *gst_registry_update_env = "GST_REGISTRY_UPDATE=yes";
        char *webkit_uri_log_env =
            webkit_logging_enabled_by_cmdline() ? "XV6_WEBKIT_URI_LOG=1" :
                                                  "XV6_WEBKIT_URI_LOG=0";
        char *webkit_gdk_gl_env =
            webkit_disable_gdk_gl_by_cmdline() ? "GDK_GL=disable" :
                                                 "GDK_GL=gles";
        char webkit_gpu_run_id_value[64];
        char webkit_gpu_run_id_env[96];
        char webkit_gpu_validate_run_id_env[112];
        char webkit_wlcomp_d3d12_run_id_env[112];

        snprintf(webkit_gpu_run_id_value, sizeof(webkit_gpu_run_id_value),
                 "webkit-%d-%ld", getpid(), (long)time(NULL));
        snprintf(webkit_gpu_run_id_env, sizeof(webkit_gpu_run_id_env),
                 "WEBKIT_XV6_GPU_RUN_ID=%s", webkit_gpu_run_id_value);
        snprintf(webkit_gpu_validate_run_id_env,
                 sizeof(webkit_gpu_validate_run_id_env),
                 "XV6_GPU_VALIDATE_RUN_ID=%s", webkit_gpu_run_id_value);
        snprintf(webkit_wlcomp_d3d12_run_id_env,
                 sizeof(webkit_wlcomp_d3d12_run_id_env),
                 "XV6_WLCOMP_D3D12_RUN_ID=%s", webkit_gpu_run_id_value);
        char *argv_minibrowser[] = {
            (char *)name,
            "--autoplay-policy=allow",
            "--private",
            "--enable-sandbox=false",
            "--enable-webgl=false",
            "--enable-webaudio=true",
            "--enable-mediasource=true",
            "--enable-media-stream=false",
            "--enable-page-cache=false",
            "--enable-dns-prefetching=false",
            "--enable-offline-web-application-cache=false",
            (char *)minibrowser_feature_flags,
            (char *)minibrowser_url,
            NULL,
        };
        char *argv_minibrowser_youtube[] = {
            (char *)name,
            "--autoplay-policy=allow",
            "--private",
            (char *)webkit_youtube_compat_user_agent,
            "--enable-sandbox=false",
            "--enable-webgl=false",
            "--enable-webaudio=true",
            "--enable-mediasource=true",
            "--enable-media-stream=false",
            "--enable-page-cache=false",
            "--enable-dns-prefetching=false",
            "--enable-offline-web-application-cache=false",
            (char *)minibrowser_feature_flags,
            (char *)minibrowser_url,
            NULL,
        };
        char *argv_minibrowser_js[] = {
            (char *)name,
            "--autoplay-policy=allow",
            "--private",
            "--enable-sandbox=false",
            "--enable-webgl=false",
            "--enable-webaudio=true",
            "--enable-mediasource=true",
            "--enable-media-stream=false",
            "--enable-page-cache=false",
            "--enable-dns-prefetching=false",
            "--enable-offline-web-application-cache=false",
            (char *)minibrowser_feature_flags,
            (char *)minibrowser_url,
            NULL,
        };
        char *argv_minibrowser_js_youtube[] = {
            (char *)name,
            "--autoplay-policy=allow",
            "--private",
            (char *)webkit_youtube_compat_user_agent,
            "--enable-sandbox=false",
            "--enable-webgl=false",
            "--enable-webaudio=true",
            "--enable-mediasource=true",
            "--enable-media-stream=false",
            "--enable-page-cache=false",
            "--enable-dns-prefetching=false",
            "--enable-offline-web-application-cache=false",
            (char *)minibrowser_feature_flags,
            (char *)minibrowser_url,
            NULL,
        };
        char *argv_minibrowser_accel[] = {
            (char *)name,
            "--autoplay-policy=allow",
            "--private",
            "--enable-sandbox=false",
            "--enable-webgl=false",
            "--enable-webaudio=true",
            "--enable-mediasource=true",
            "--enable-media-stream=false",
            "--enable-page-cache=false",
            "--enable-dns-prefetching=false",
            "--enable-offline-web-application-cache=false",
            (char *)minibrowser_feature_flags,
            (char *)minibrowser_url,
            NULL,
        };
        char *argv_minibrowser_accel_youtube[] = {
            (char *)name,
            "--autoplay-policy=allow",
            "--private",
            (char *)webkit_youtube_compat_user_agent,
            "--enable-sandbox=false",
            "--enable-webgl=false",
            "--enable-webaudio=true",
            "--enable-mediasource=true",
            "--enable-media-stream=false",
            "--enable-page-cache=false",
            "--enable-dns-prefetching=false",
            "--enable-offline-web-application-cache=false",
            (char *)minibrowser_feature_flags,
            (char *)minibrowser_url,
            NULL,
        };
        char *argv_minibrowser_accel_js[] = {
            (char *)name,
            "--autoplay-policy=allow",
            "--private",
            "--enable-sandbox=false",
            "--enable-webgl=true",
            "--enable-webaudio=true",
            "--enable-mediasource=true",
            "--enable-media-stream=false",
            "--enable-page-cache=false",
            "--enable-dns-prefetching=false",
            "--enable-offline-web-application-cache=false",
            (char *)minibrowser_feature_flags,
            (char *)minibrowser_url,
            NULL,
        };
        char *argv_minibrowser_accel_js_youtube[] = {
            (char *)name,
            "--autoplay-policy=allow",
            "--private",
            (char *)webkit_youtube_compat_user_agent,
            "--enable-sandbox=false",
            "--enable-webgl=true",
            "--enable-webaudio=true",
            "--enable-mediasource=true",
            "--enable-media-stream=false",
            "--enable-page-cache=false",
            "--enable-dns-prefetching=false",
            "--enable-offline-web-application-cache=false",
            (char *)minibrowser_feature_flags,
            (char *)minibrowser_url,
            NULL,
        };
        char *argv_minibrowser_accel_webgl_js[] = {
            (char *)name,
            "--autoplay-policy=allow",
            "--private",
            "--enable-sandbox=false",
            "--enable-webgl=true",
            "--enable-webaudio=true",
            "--enable-mediasource=true",
            "--enable-media-stream=false",
            "--enable-page-cache=false",
            "--enable-dns-prefetching=false",
            "--enable-offline-web-application-cache=false",
            (char *)minibrowser_feature_flags,
            (char *)minibrowser_url,
            NULL,
        };
        char *argv_minibrowser_accel_webgl_js_youtube[] = {
            (char *)name,
            "--autoplay-policy=allow",
            "--private",
            (char *)webkit_youtube_compat_user_agent,
            "--enable-sandbox=false",
            "--enable-webgl=true",
            "--enable-webaudio=true",
            "--enable-mediasource=true",
            "--enable-media-stream=false",
            "--enable-page-cache=false",
            "--enable-dns-prefetching=false",
            "--enable-offline-web-application-cache=false",
            (char *)minibrowser_feature_flags,
            (char *)minibrowser_url,
            NULL,
        };
        char *envp_default[] = {
            "HOME=/",
            "PATH=/bin:/usr/bin",
            "XDG_RUNTIME_DIR=/tmp",
            "XDG_CACHE_HOME=/tmp/.cache",
            "XDG_DATA_HOME=/tmp/.local/share",
            "XDG_DATA_DIRS=/share:/usr/share",
            "WAYLAND_DISPLAY=wayland-0",
            "GDK_BACKEND=wayland",
            "GDK_GL=gles",
            "GDK_DPI_SCALE=1.0",
            "XCURSOR_PATH=/share/icons",
            "XCURSOR_THEME=Adwaita",
            "SSL_CERT_FILE=/share/netsurf/ca-bundle",
            NULL
        };
        char *envp_minibrowser[] = {
            "HOME=/",
            "PATH=/bin:/usr/bin",
            "LD_LIBRARY_PATH=/lib:/usr/lib:/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu",
            "LD_PRELOAD=/lib/libpng16.so.16:/lib/libxv6memshim.so",
            "XDG_RUNTIME_DIR=/tmp",
            "XDG_CACHE_HOME=/tmp/.cache",
            "XDG_DATA_DIRS=/share:/usr/share",
            "WAYLAND_DISPLAY=wayland-0",
            "GDK_BACKEND=wayland",
            webkit_gdk_gl_env,
            "GDK_DPI_SCALE=1.0",
            "XCURSOR_PATH=/share/icons",
            "XCURSOR_THEME=Adwaita",
            "SSL_CERT_FILE=/share/netsurf/ca-bundle",
            "GIO_MODULE_DIR=/lib/gio/modules",
            "GIO_USE_TLS=gnutls",
            "GST_PLUGIN_SYSTEM_PATH_1_0=/lib/gstreamer-1.0:/usr/lib/gstreamer-1.0",
            "GST_PLUGIN_PATH_1_0=/lib/gstreamer-1.0:/usr/lib/gstreamer-1.0",
            "GST_PLUGIN_SCANNER=/libexec/gstreamer-1.0/gst-plugin-scanner",
            "GST_PLUGIN_SCANNER_1_0=/libexec/gstreamer-1.0/gst-plugin-scanner",
            "GST_GL_PLATFORM=egl",
            "GST_GL_WINDOW=wayland",
            "GST_REGISTRY=/tmp/gstreamer-registry.bin",
            "GST_REGISTRY_REUSE_PLUGIN_SCANNER=1",
            gst_registry_update_env,
            "XV6_GUI_SESSION=1",
            webkit_gpu_run_id_env,
            webkit_gpu_validate_run_id_env,
            webkit_wlcomp_d3d12_run_id_env,
            webkit_uri_log_env,
            "WEBKIT_EXEC_PATH=/libexec/webkit2gtk-4.1",
            "WEBKIT_INJECTED_BUNDLE_PATH=/lib/webkit2gtk-4.1/injected-bundle",
            "WEBKIT_DISABLE_NETWORK_CACHE=1",
            "WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1",
            "WEBKIT_GST_DISABLE_GL_SINK=1",
            "WEBKIT_GST_DMABUF_SINK_DISABLED=1",
            "WEBKIT_GST_USE_VIDEOCONVERT_SCALE=1",
            "WEBKIT_GST_MAX_AVC1_RESOLUTION=720P",
            "WEBKIT_DISABLE_COMPOSITING_MODE=1",
            "WEBKIT_XV6_DISABLE_COMPOSITING_UPDATE=1",
            "LIBGL_ALWAYS_SOFTWARE=1",
            "EGL_PLATFORM=wayland",
            "LIBGL_DRIVERS_PATH=/lib/dri",
            "MESA_LOADER_DRIVER_OVERRIDE=swrast",
            "ANGLE_DEFAULT_PLATFORM=gl",
            "EPOXY_XV6_ALLOW_MISSING=1",
            "WEBKIT_XV6_SKIP_RULE_FEATURES=1",
            "WEBKIT_XV6_SKIP_INITIAL_EMPTY_RENDER=1",
            "SOUP_FORCE_HTTP1=1",
            /* Keep GStreamer logging at ERROR level only: enough to capture a
             * genuine pipeline failure (scanned by webkit_dump_gst_debug_evidence)
             * without the per-frame WARN/INFO flood that slows software decode. */
            "GST_DEBUG=1",
            "GST_DEBUG_FILE=/tmp/gst-debug.log",
            "GST_DEBUG_NO_COLOR=1",
            NULL
        };
        char *envp_minibrowser_accel[] = {
            "HOME=/",
            "PATH=/bin:/usr/bin",
            "LD_LIBRARY_PATH=/lib:/usr/lib:/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu",
            "LD_PRELOAD=/lib/libpng16.so.16:/lib/libxv6memshim.so",
            "XDG_RUNTIME_DIR=/tmp",
            "XDG_CACHE_HOME=/tmp/.cache",
            "XDG_DATA_HOME=/tmp/.local/share",
            "XDG_DATA_DIRS=/share:/usr/share",
            "WAYLAND_DISPLAY=wayland-0",
            "GDK_BACKEND=wayland",
            webkit_gdk_gl_env,
            "GDK_DPI_SCALE=1.0",
            "XCURSOR_PATH=/share/icons",
            "XCURSOR_THEME=Adwaita",
            "SSL_CERT_FILE=/share/netsurf/ca-bundle",
            "GIO_MODULE_DIR=/lib/gio/modules",
            "GIO_USE_TLS=gnutls",
            "GST_PLUGIN_SYSTEM_PATH_1_0=/lib/gstreamer-1.0:/usr/lib/gstreamer-1.0",
            "GST_PLUGIN_PATH_1_0=/lib/gstreamer-1.0:/usr/lib/gstreamer-1.0",
            "GST_PLUGIN_SCANNER=/libexec/gstreamer-1.0/gst-plugin-scanner",
            "GST_PLUGIN_SCANNER_1_0=/libexec/gstreamer-1.0/gst-plugin-scanner",
            "GST_GL_PLATFORM=egl",
            "GST_GL_WINDOW=wayland",
            "GST_REGISTRY=/tmp/gstreamer-registry.bin",
            "GST_REGISTRY_REUSE_PLUGIN_SCANNER=1",
            gst_registry_update_env,
            "XV6_GUI_SESSION=1",
            webkit_gpu_run_id_env,
            webkit_gpu_validate_run_id_env,
            webkit_wlcomp_d3d12_run_id_env,
            webkit_uri_log_env,
            "WEBKIT_EXEC_PATH=/libexec/webkit2gtk-4.1",
            "WEBKIT_INJECTED_BUNDLE_PATH=/lib/webkit2gtk-4.1/injected-bundle",
            "WEBKIT_DISABLE_NETWORK_CACHE=1",
            "WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1",
            "WEBKIT_DMABUF_RENDERER_DISABLE_GBM=1",
            "WEBKIT_XV6_GPU_CONTRACT=virgl-opengl-submit",
            "WEBKIT_XV6_REQUIRE_GPU_CONTRACT=1",
            "WEBKIT_XV6_FORCE_COMPOSITING_MODE=1",
            "LIBGL_ALWAYS_SOFTWARE=0",
            "LIBGL_DRIVERS_PATH=/lib/dri",
            "GALLIUM_DRIVER=virgl",
            "EGL_PLATFORM=wayland",
            "ANGLE_DEFAULT_PLATFORM=gl",
            "SOUP_FORCE_HTTP1=1",
            "EPOXY_XV6_ALLOW_MISSING=1",
            NULL
        };
        /*
         * Hyper-V GPU-P (DXG / D3DKMT) GPU-render path.
         *
         * Unlike the virgl accel env above, this does NOT claim a native
         * present / shared-surface contract: there is no GPU scanout ABI on
         * this host, so wlcomp still composites with a CPU framebuffer blit.
         * What this env changes is *rendering*: WebKit's accelerated
         * compositing GL context runs on the host NVIDIA GPU through Mesa's
         * d3d12 Gallium driver (libdxcore + libd3d12 over /dev/dxg, no
         * /dev/dri render node), then the rendered buffer is read back and
         * presented by the compositor on the CPU.  The contract label is
         * deliberately honest ("d3d12-gpu-render") and we do NOT set
         * WEBKIT_XV6_REQUIRE_GPU_CONTRACT, so WebKit never asserts a
         * native-present capability it does not have.
         */
        char *envp_minibrowser_d3d12[] = {
            "HOME=/",
            "PATH=/bin:/usr/bin",
            "LD_LIBRARY_PATH=/usr/lib/wsl/lib:/lib:/usr/lib:/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu",
            "LD_PRELOAD=/lib/libpng16.so.16:/lib/libxv6memshim.so",
            "XDG_RUNTIME_DIR=/tmp",
            "XDG_CACHE_HOME=/tmp/.cache",
            "XDG_DATA_HOME=/tmp/.local/share",
            "XDG_DATA_DIRS=/share:/usr/share",
            "WAYLAND_DISPLAY=wayland-0",
            "GDK_BACKEND=wayland",
            webkit_gdk_gl_env,
            "GDK_DPI_SCALE=1.0",
            "XCURSOR_PATH=/share/icons",
            "XCURSOR_THEME=Adwaita",
            "SSL_CERT_FILE=/share/netsurf/ca-bundle",
            "GIO_MODULE_DIR=/lib/gio/modules",
            "GIO_USE_TLS=gnutls",
            "GST_PLUGIN_SYSTEM_PATH_1_0=/lib/gstreamer-1.0:/usr/lib/gstreamer-1.0",
            "GST_PLUGIN_PATH_1_0=/lib/gstreamer-1.0:/usr/lib/gstreamer-1.0",
            "GST_PLUGIN_SCANNER=/libexec/gstreamer-1.0/gst-plugin-scanner",
            "GST_PLUGIN_SCANNER_1_0=/libexec/gstreamer-1.0/gst-plugin-scanner",
            "GST_GL_PLATFORM=egl",
            "GST_GL_WINDOW=wayland",
            "GST_REGISTRY=/tmp/gstreamer-registry.bin",
            "GST_REGISTRY_REUSE_PLUGIN_SCANNER=1",
            gst_registry_update_env,
            "XV6_GUI_SESSION=1",
            webkit_gpu_run_id_env,
            webkit_gpu_validate_run_id_env,
            webkit_wlcomp_d3d12_run_id_env,
            webkit_uri_log_env,
            "WEBKIT_EXEC_PATH=/libexec/webkit2gtk-4.1",
            "WEBKIT_INJECTED_BUNDLE_PATH=/lib/webkit2gtk-4.1/injected-bundle",
            "WEBKIT_DISABLE_NETWORK_CACHE=1",
            "WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1",
            "WEBKIT_DMABUF_RENDERER_DISABLE_GBM=1",
            "WEBKIT_XV6_GPU_CONTRACT=d3d12-gpu-render",
            "WEBKIT_XV6_FORCE_COMPOSITING_MODE=1",
            "LIBGL_ALWAYS_SOFTWARE=0",
            "LIBGL_DRIVERS_PATH=/lib/dri",
            "GALLIUM_DRIVER=d3d12",
            "MESA_D3D12_DEFAULT_ADAPTER_NAME=NVIDIA",
            "EGL_PLATFORM=wayland",
            "ANGLE_DEFAULT_PLATFORM=gl",
            "SOUP_FORCE_HTTP1=1",
            "EPOXY_XV6_ALLOW_MISSING=1",
            NULL
        };
        char *envp_minibrowser_accel_sw[] = {
            "HOME=/",
            "PATH=/bin:/usr/bin",
            "LD_LIBRARY_PATH=/lib:/usr/lib:/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu",
            "LD_PRELOAD=/lib/libpng16.so.16:/lib/libxv6memshim.so",
            "XDG_RUNTIME_DIR=/tmp",
            "XDG_CACHE_HOME=/tmp/.cache",
            "XDG_DATA_HOME=/tmp/.local/share",
            "XDG_DATA_DIRS=/share:/usr/share",
            "WAYLAND_DISPLAY=wayland-0",
            "GDK_BACKEND=wayland",
            webkit_gdk_gl_env,
            "GDK_DPI_SCALE=1.0",
            "XCURSOR_PATH=/share/icons",
            "XCURSOR_THEME=Adwaita",
            "SSL_CERT_FILE=/share/netsurf/ca-bundle",
            "GIO_MODULE_DIR=/lib/gio/modules",
            "GIO_USE_TLS=gnutls",
            "GST_PLUGIN_SYSTEM_PATH_1_0=/lib/gstreamer-1.0:/usr/lib/gstreamer-1.0",
            "GST_PLUGIN_PATH_1_0=/lib/gstreamer-1.0:/usr/lib/gstreamer-1.0",
            "GST_PLUGIN_SCANNER=/libexec/gstreamer-1.0/gst-plugin-scanner",
            "GST_PLUGIN_SCANNER_1_0=/libexec/gstreamer-1.0/gst-plugin-scanner",
            "GST_GL_PLATFORM=egl",
            "GST_GL_WINDOW=wayland",
            "GST_REGISTRY=/tmp/gstreamer-registry.bin",
            "GST_REGISTRY_REUSE_PLUGIN_SCANNER=1",
            gst_registry_update_env,
            "XV6_GUI_SESSION=1",
            webkit_gpu_run_id_env,
            webkit_gpu_validate_run_id_env,
            webkit_wlcomp_d3d12_run_id_env,
            webkit_uri_log_env,
            "WEBKIT_EXEC_PATH=/libexec/webkit2gtk-4.1",
            "WEBKIT_INJECTED_BUNDLE_PATH=/lib/webkit2gtk-4.1/injected-bundle",
            "WEBKIT_DISABLE_NETWORK_CACHE=1",
            "WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1",
            "WEBKIT_GST_DISABLE_GL_SINK=1",
            "WEBKIT_GST_DMABUF_SINK_DISABLED=1",
            "WEBKIT_GST_USE_VIDEOCONVERT_SCALE=1",
            "WEBKIT_GST_MAX_AVC1_RESOLUTION=720P",
            "LIBGL_ALWAYS_SOFTWARE=1",
            "EGL_PLATFORM=wayland",
            "LIBGL_DRIVERS_PATH=/lib/dri",
            "MESA_LOADER_DRIVER_OVERRIDE=swrast",
            "ANGLE_DEFAULT_PLATFORM=gl",
            "WEBKIT_XV6_DISABLE_BCG_SWITCH=1",
            "WEBKIT_XV6_SKIP_RULE_FEATURES=1",
            "SOUP_FORCE_HTTP1=1",
            "EPOXY_XV6_ALLOW_MISSING=1",
            NULL
        };
        char *envp_minibrowser_dmabuf_sw[] = {
            "HOME=/",
            "PATH=/bin:/usr/bin",
            "LD_LIBRARY_PATH=/lib:/usr/lib:/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu",
            "LD_PRELOAD=/lib/libpng16.so.16:/lib/libxv6memshim.so",
            "XDG_RUNTIME_DIR=/tmp",
            "XDG_CACHE_HOME=/tmp/.cache",
            "XDG_DATA_HOME=/tmp/.local/share",
            "XDG_DATA_DIRS=/share:/usr/share",
            "WAYLAND_DISPLAY=wayland-0",
            "GDK_BACKEND=wayland",
            webkit_gdk_gl_env,
            "GDK_DPI_SCALE=1.0",
            "XCURSOR_PATH=/share/icons",
            "XCURSOR_THEME=Adwaita",
            "SSL_CERT_FILE=/share/netsurf/ca-bundle",
            "GIO_MODULE_DIR=/lib/gio/modules",
            "GIO_USE_TLS=gnutls",
            "GST_PLUGIN_SYSTEM_PATH_1_0=/lib/gstreamer-1.0:/usr/lib/gstreamer-1.0",
            "GST_PLUGIN_PATH_1_0=/lib/gstreamer-1.0:/usr/lib/gstreamer-1.0",
            "GST_PLUGIN_SCANNER=/libexec/gstreamer-1.0/gst-plugin-scanner",
            "GST_PLUGIN_SCANNER_1_0=/libexec/gstreamer-1.0/gst-plugin-scanner",
            "GST_GL_PLATFORM=egl",
            "GST_GL_WINDOW=wayland",
            "GST_REGISTRY=/tmp/gstreamer-registry.bin",
            "GST_REGISTRY_REUSE_PLUGIN_SCANNER=1",
            gst_registry_update_env,
            "XV6_GUI_SESSION=1",
            webkit_gpu_run_id_env,
            webkit_gpu_validate_run_id_env,
            webkit_wlcomp_d3d12_run_id_env,
            webkit_uri_log_env,
            "WEBKIT_EXEC_PATH=/libexec/webkit2gtk-4.1",
            "WEBKIT_INJECTED_BUNDLE_PATH=/lib/webkit2gtk-4.1/injected-bundle",
            "WEBKIT_DISABLE_NETWORK_CACHE=1",
            "WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1",
            "LIBGL_ALWAYS_SOFTWARE=1",
            "EGL_PLATFORM=wayland",
            "LIBGL_DRIVERS_PATH=/lib/dri",
            "MESA_LOADER_DRIVER_OVERRIDE=swrast",
            "ANGLE_DEFAULT_PLATFORM=gl",
            "SOUP_FORCE_HTTP1=1",
            "EPOXY_XV6_ALLOW_MISSING=1",
            NULL
        };
        char *envp_mesa_accel[] = {
            "HOME=/",
            "PATH=/bin:/usr/bin",
            "XDG_RUNTIME_DIR=/tmp",
            "XDG_CACHE_HOME=/tmp/.cache",
            "XDG_DATA_DIRS=/share:/usr/share",
            "WAYLAND_DISPLAY=wayland-0",
            "GDK_BACKEND=wayland",
            "XCURSOR_PATH=/share/icons",
            "XCURSOR_THEME=Adwaita",
            "LIBGL_ALWAYS_SOFTWARE=0",
            "GALLIUM_DRIVER=virgl",
            "EGL_PLATFORM=wayland",
            NULL
        };
        char *envp_mesa_accel_sw[] = {
            "HOME=/",
            "PATH=/bin:/usr/bin",
            "XDG_RUNTIME_DIR=/tmp",
            "XDG_CACHE_HOME=/tmp/.cache",
            "XDG_DATA_DIRS=/share:/usr/share",
            "WAYLAND_DISPLAY=wayland-0",
            "GDK_BACKEND=wayland",
            "XCURSOR_PATH=/share/icons",
            "XCURSOR_THEME=Adwaita",
            "LIBGL_ALWAYS_SOFTWARE=1",
            "EGL_PLATFORM=wayland",
            "MESA_LOADER_DRIVER_OVERRIDE=swrast",
            "LIBGL_DRIVERS_PATH=/lib/dri",
            NULL
        };
        /* Hyper-V GPU-P: render Mesa GL demos on the host GPU via d3d12. */
        char *envp_mesa_d3d12[] = {
            "HOME=/",
            "PATH=/bin:/usr/bin",
            "LD_LIBRARY_PATH=/usr/lib/wsl/lib:/lib:/usr/lib:/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu",
            "XDG_RUNTIME_DIR=/tmp",
            "XDG_CACHE_HOME=/tmp/.cache",
            "XDG_DATA_DIRS=/share:/usr/share",
            "WAYLAND_DISPLAY=wayland-0",
            "GDK_BACKEND=wayland",
            "XCURSOR_PATH=/share/icons",
            "XCURSOR_THEME=Adwaita",
            "LIBGL_ALWAYS_SOFTWARE=0",
            "GALLIUM_DRIVER=d3d12",
            "MESA_D3D12_DEFAULT_ADAPTER_NAME=NVIDIA",
            "LIBGL_DRIVERS_PATH=/lib/dri",
            "EGL_PLATFORM=wayland",
            NULL
        };
        int minibrowser_accel =
            is_minibrowser && webkit_accel_enabled_by_cmdline();
        int minibrowser_dmabuf =
            is_minibrowser && webkit_dmabuf_enabled_by_cmdline();
        int minibrowser_js =
            is_minibrowser && !webkit_js_disabled_by_cmdline();
        int minibrowser_webgl_smoke =
            is_minibrowser && webkit_webgl_smoke_enabled_by_cmdline();
        int webkit_accel =
            (is_minibrowser && minibrowser_accel) ||
            (is_webkitgpusmoke && webkit_accel_enabled_by_cmdline());
        /*
         * Honest GPU-render-with-CPU-present tier for Hyper-V GPU-P: when
         * acceleration is requested and the DXG transport is open but the
         * (native-present) shared-surface contract is unavailable, WebKit
         * still renders its compositing GL context on the host GPU via Mesa
         * d3d12.  This does NOT claim native present (see envp_minibrowser_d3d12).
         */
        int minibrowser_d3d12_render = 0;
        int opengl_submit_available = xv6_opengl_submit_available();
        int dxg_transport_available = xv6_dxg_transport_available();
        int render_node_available = xv6_render_node_available();
        struct webkit_gpu_contract_state contract;
        char **argv_exec = argv_default;
        compute_webkit_gpu_contract(opengl_submit_available,
                                    dxg_transport_available,
                                    render_node_available,
                                    webkit_gpu_run_id_value,
                                    &contract);
        if (minibrowser_dmabuf && !render_node_available) {
            fprintf(stderr,
                    "[desktop] WebKit dmabuf requested, but no render node "
                    "is available; using the stable WebKit compositor path\n");
            minibrowser_dmabuf = 0;
        }
        if (minibrowser_dmabuf && !contract.validated_shared_surface) {
            fprintf(stderr,
                    "[desktop] WebKit dmabuf requested, but no validated "
                    "shared-surface contract is available; using the stable "
                    "WebKit compositor path\n");
            minibrowser_dmabuf = 0;
        }
        if (webkit_accel && !contract.validated_shared_surface &&
            !minibrowser_dmabuf) {
            if (dxg_transport_available) {
                fprintf(stderr,
                        "[desktop] Hyper-V DXG GPU-PV transport is open and "
                        "the native-present shared-surface contract is not "
                        "available; rendering WebKit's compositing GL context "
                        "on the host GPU via Mesa d3d12 with CPU present\n");
                minibrowser_d3d12_render = 1;
                /* keep webkit_accel/minibrowser_accel set: GL compositing on */
            } else {
                fprintf(stderr,
                        "[desktop] WebKit acceleration requested, but virgl "
                        "is unavailable; using the stable WebKit compositor "
                        "path\n");
                minibrowser_accel = 0;
                webkit_accel = 0;
            }
        }
        if (minibrowser_dmabuf)
            webkit_accel = 1;
        if (is_webkit) {
            write_webkit_gpu_policy_file(name,
                                         webkit_accel_enabled_by_cmdline(),
                                         webkit_accel,
                                         opengl_submit_available,
                                         dxg_transport_available,
                                         render_node_available,
                                         minibrowser_dmabuf, &contract);
        }
        if (is_minibrowser) {
            if (minibrowser_accel) {
                if (minibrowser_js) {
                    if (minibrowser_webgl_smoke)
                        argv_exec = minibrowser_youtube_compat ?
                            argv_minibrowser_accel_webgl_js_youtube :
                            argv_minibrowser_accel_webgl_js;
                    else
                        argv_exec = minibrowser_youtube_compat ?
                            argv_minibrowser_accel_js_youtube :
                            argv_minibrowser_accel_js;
                } else {
                    argv_exec = minibrowser_youtube_compat ?
                        argv_minibrowser_accel_youtube :
                        argv_minibrowser_accel;
                }
            } else {
                argv_exec = minibrowser_js ?
                    (minibrowser_youtube_compat ?
                         argv_minibrowser_js_youtube :
                         argv_minibrowser_js) :
                    (minibrowser_youtube_compat ?
                         argv_minibrowser_youtube :
                         argv_minibrowser);
            }
            fprintf(stderr,
                    "[desktop] MiniBrowser argv js=%d accel=%d dmabuf=%d "
                    "webgl=%d youtube_compat=%d arg4=%s url=%s\n",
                    minibrowser_js, minibrowser_accel, minibrowser_dmabuf,
                    minibrowser_webgl_smoke, minibrowser_youtube_compat,
                    argv_exec[4] ? argv_exec[4] : "(none)",
                    minibrowser_url);
        }
        errno = 0;
        execve(path,
               argv_exec,
               is_webkit ?
                    (webkit_accel ?
                         (minibrowser_dmabuf ? envp_minibrowser_dmabuf_sw :
                     (opengl_submit_available ? envp_minibrowser_accel :
                      (minibrowser_d3d12_render ? envp_minibrowser_d3d12 :
                                           envp_minibrowser_accel_sw))) :
                         envp_minibrowser) :
                    (is_mesa_gl ?
                    (opengl_submit_available ? envp_mesa_accel :
                     (dxg_transport_available ? envp_mesa_d3d12 :
                                          envp_mesa_accel_sw)) :
                         envp_default));
        fprintf(stderr, "%s: execve failed errno=%d (%s)\n", path, errno,
                errno ? strerror(errno) : "no errno from kernel");
        _exit(127);
    }
    if (pid > 0)
        setpgid(pid, pid);
    return pid;
}

static void kill_and_reap(pid_t *pidp)
{
    pid_t pid;
    long long deadline_ms;
    int status;

    if (!pidp || *pidp <= 0)
        return;

    pid = *pidp;
    desktop_signal_child(pid, SIGTERM);
    deadline_ms = monotonic_ms() + 1000;
    for (;;) {
        pid_t exited = waitpid(pid, &status, WNOHANG);

        if (exited == pid || (exited < 0 && errno == ECHILD)) {
            *pidp = 0;
            return;
        }
        if (exited < 0 && errno != EINTR)
            break;
        if (monotonic_ms() >= deadline_ms)
            break;
        usleep(20000);
    }

    desktop_signal_child(pid, SIGKILL);
    for (;;) {
        pid_t exited = waitpid(pid, &status, 0);

        if (exited == pid || (exited < 0 && errno == ECHILD))
            break;
        if (exited < 0 && errno != EINTR)
            break;
    }
    *pidp = 0;
}

static void write_child_status_file(const char *path, const char *label,
                                    pid_t pid, int status)
{
    char buf[192];
    int n = snprintf(buf, sizeof(buf),
                     "%s pid=%d exited=%d status=%d signaled=%d signal=%d "
                     "raw=%d\n",
                     label, pid, WIFEXITED(status) ? 1 : 0,
                     WIFEXITED(status) ? WEXITSTATUS(status) : -1,
                     WIFSIGNALED(status) ? 1 : 0,
                     WIFSIGNALED(status) ? WTERMSIG(status) : 0, status);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (fd >= 0) {
        if (n > 0)
            (void)write(fd, buf, (size_t)n);
        close(fd);
    }
}

static void print_glmaze_status_file(void)
{
    char buf[192];
    ssize_t n;
    int fd = open("/tmp/glmaze-status", O_RDONLY);

    if (fd < 0)
        return;
    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0)
        return;
    buf[n] = '\0';
    for (ssize_t i = 0; i < n; i++) {
        if (buf[i] == '\n' || buf[i] == '\r')
            buf[i] = ' ';
    }
    fprintf(stderr, "[desktop] glmaze status %s\n", buf);
}

static void write_webkit_gpu_policy_file(const char *name, int requested_accel,
                                         int effective_accel,
                                         int opengl_submit_available,
                                         int dxg_transport_available,
                                         int render_node_available,
                                         int dmabuf_requested,
                                         const struct webkit_gpu_contract_state
                                             *contract)
{
    const char *fallback = "none";
    char buf[1280];
    int fd;
    int n;
    int shared_surface = contract ? contract->shared_surface : 0;
    int validated_shared_surface =
        contract ? contract->validated_shared_surface : 0;
    int d3d12_present = contract ? contract->d3d12_present : 0;
    int d3d12_contract_evidence =
        contract ? contract->d3d12_contract_evidence : 0;
    int d3d12_display_bind = contract ? contract->d3d12_display_bind : 0;
    int d3d12_same_adapter = contract ? contract->d3d12_same_adapter : 0;
    int d3d12_no_readback = contract ? contract->d3d12_no_readback : 0;
    int d3d12_shared_resource =
        contract ? contract->d3d12_shared_resource : 0;
    int d3d12_fence = contract ? contract->d3d12_fence : 0;
    int d3d12_native_present_required =
        contract ? contract->d3d12_native_present_required : 0;
    int d3d12_run_id_match =
        contract ? contract->d3d12_run_id_match : 0;
    int d3d12_content_progress =
        contract ? contract->d3d12_content_progress : 0;
    int d3d12_evidence_seal =
        contract ? contract->d3d12_evidence_seal : 0;
    int d3d12_copy_export = contract ? contract->d3d12_copy_export : 0;
    int d3d12_readback = contract ? contract->d3d12_readback : 0;
    const char *gpu_contract = contract ? contract->gpu_contract : "none";
    const char *display_bind_backend =
        contract && contract->display_bind_backend[0] ?
            contract->display_bind_backend : "none";
    const char *display_bind_transport =
        contract && contract->display_bind_transport[0] ?
            contract->display_bind_transport : "none";
    const char *display_bind_transport_source =
        contract && contract->display_bind_transport_source[0] ?
            contract->display_bind_transport_source : "none";
    const char *display_bind_completion_source =
        contract && contract->display_bind_completion_source[0] ?
            contract->display_bind_completion_source : "none";
    unsigned long host_saw_display_bind_packet =
        contract ? (unsigned long)contract->host_saw_display_bind_packet : 0;
    unsigned long wsl_presenthistory_completion_credit =
        contract ?
            (unsigned long)contract->wsl_presenthistory_completion_credit : 0;
    unsigned long display_bind_present_id =
        contract ? (unsigned long)contract->display_bind_present_id : 0;
    unsigned long display_bind_completed_id =
        contract ? (unsigned long)contract->display_bind_completed_id : 0;
    unsigned long display_bind_resource_generation =
        contract ? (unsigned long)contract->display_bind_resource_generation : 0;

    if (requested_accel && !effective_accel && !dmabuf_requested)
        fallback = opengl_submit_available ?
            "shared_surface_unavailable" : "opengl_submit_unavailable";
    else if (dmabuf_requested && !render_node_available)
        fallback = "render_node_unavailable";
    else if (dmabuf_requested && !validated_shared_surface)
        fallback = "shared_surface_unavailable";

    n = snprintf(buf, sizeof(buf),
                 "webkit_gpu_policy name=%s requested_accel=%d "
                 "effective_accel=%d opengl_submit=%d dxg_transport=%d "
                 "render_node=%d dmabuf=%d shared_surface=%d "
                 "validated_shared_surface=%d d3d12_present=%d "
                 "d3d12_contract_evidence=%d d3d12_display_bind=%d "
                 "display_bind_backend=%s display_bind_transport=%s "
                 "display_bind_transport_source=%s "
                 "host_saw_display_bind_packet=%lu "
                 "wsl_presenthistory_completion_credit=%lu "
                 "display_bind_present_id=%lu "
                 "display_bind_completed_id=%lu "
                 "display_bind_resource_generation=%lu "
                 "display_bind_completion_source=%s "
                 "d3d12_same_adapter=%d "
                 "d3d12_no_readback=%d d3d12_shared_resource=%d "
                 "d3d12_fence=%d d3d12_native_present_required=%d "
                 "d3d12_run_id_match=%d d3d12_content_progress=%d "
                 "d3d12_evidence_seal=%d "
                 "d3d12_copy_export=%d d3d12_readback=%d "
                 "gpu_contract=%s fallback=%s\n",
                 name, requested_accel, effective_accel,
                 opengl_submit_available, dxg_transport_available,
                 render_node_available, dmabuf_requested, shared_surface,
                 validated_shared_surface, d3d12_present,
                 d3d12_contract_evidence, d3d12_display_bind,
                 display_bind_backend, display_bind_transport,
                 display_bind_transport_source,
                 host_saw_display_bind_packet,
                 wsl_presenthistory_completion_credit,
                 display_bind_present_id, display_bind_completed_id,
                 display_bind_resource_generation,
                 display_bind_completion_source, d3d12_same_adapter,
                 d3d12_no_readback, d3d12_shared_resource, d3d12_fence,
                 d3d12_native_present_required, d3d12_run_id_match,
                 d3d12_content_progress, d3d12_evidence_seal,
                 d3d12_copy_export, d3d12_readback, gpu_contract, fallback);
    if (n > 0) {
        fputs(buf, stderr);
        fflush(stderr);
    }
    fd = open("/tmp/webkit-gpu-policy",
              O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        if (n > 0)
            (void)write(fd, buf, (size_t)n);
        close(fd);
    }
}

static void cleanup(void)
{
    kill_and_reap(&client_pid);
    kill_and_reap(&glsmoke_pid);
    kill_and_reap(&gst_warmup_pid);
    kill_and_reap(&httpd_pid);
    kill_and_reap(&wlcomp_pid);
}

static int token_is_disabled(const char *cmdline, const char *key)
{
    size_t key_len = strlen(key);
    const char *p = cmdline;

    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n')
            p++;
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=' &&
            p[key_len + 1] == '0' &&
            (p[key_len + 2] == '\0' || p[key_len + 2] == ' ' ||
             p[key_len + 2] == '\t' || p[key_len + 2] == '\n'))
            return 1;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n')
            p++;
    }
    return 0;
}

static int token_is_enabled(const char *cmdline, const char *key)
{
    size_t key_len = strlen(key);
    const char *p = cmdline;

    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n')
            p++;
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=' &&
            p[key_len + 1] == '1' &&
            (p[key_len + 2] == '\0' || p[key_len + 2] == ' ' ||
             p[key_len + 2] == '\t' || p[key_len + 2] == '\n'))
            return 1;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n')
            p++;
    }
    return 0;
}

static int read_cmdline(char *buf, size_t buf_size)
{
    int fd = open("/proc/cmdline", O_RDONLY);
    size_t total = 0;
    if (fd < 0) {
        fprintf(stderr, "[desktop] /proc/cmdline unavailable\n");
        return -1;
    }

    while (total + 1 < buf_size) {
        ssize_t n = read(fd, buf + total, buf_size - total - 1);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            close(fd);
            fprintf(stderr, "[desktop] /proc/cmdline read failed: %s\n",
                    strerror(errno));
            return -1;
        }
        if (n == 0)
            break;
        total += (size_t)n;
    }
    close(fd);
    if (total == 0) {
        fprintf(stderr, "[desktop] /proc/cmdline empty\n");
        return -1;
    }
    buf[total] = '\0';
    return 0;
}

static int cmdline_int_value(const char *cmdline, const char *key, int fallback)
{
    size_t key_len = strlen(key);
    const char *p = cmdline;

    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n')
            p++;
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            int value = atoi(p + key_len + 1);

            return value > 0 ? value : fallback;
        }
        while (*p && *p != ' ' && *p != '\t' && *p != '\n')
            p++;
    }
    return fallback;
}

static int netsurf_disabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_disabled(buf, "netsurf");
}

static int desktop_disabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_disabled(buf, "desktop");
}

static int desktop_exit_after_smoke_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return cmdline_int_value(buf, "desktop_exit_after_smoke", 0) != 0;
}

static int webkit_enabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return !token_is_disabled(buf, "webkit") && strstr(buf, "webkit=1") != NULL;
}

static int webkit_accel_enabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "webkit_accel");
}

static int webkit_dmabuf_enabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "webkit_dmabuf");
}

static int webkit_gpu_smoke_enabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "webkit_gpu_smoke");
}

static int webkit_webgl_smoke_enabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "webkit_webgl_smoke");
}

static int webkit_api_smoke_enabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "webkit_api_smoke");
}

static int webkit_http_smoke_enabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "webkit_http_smoke");
}

static int webkit_coop_smoke_enabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "webkit_coop_smoke");
}

static int webkit_js_smoke_enabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "webkit_js_smoke");
}

static int webkit_youtube_boot_smoke_enabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "webkit_youtube_boot_smoke");
}

static int webkit_youtube_waterfall_smoke_enabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "webkit_youtube_waterfall_smoke");
}

static int webkit_youtube_compat_disabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_disabled(buf, "webkit_youtube_compat");
}

static int webkit_request_idle_disabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_disabled(buf, "webkit_request_idle");
}

static int webkit_logging_enabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "webkit_log") ||
           token_is_enabled(buf, "webkit_logging");
}

static int webkit_feature_gate_smoke_enabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "webkit_feature_gate_smoke");
}

static int webkit_idle_browse_smoke_enabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "webkit_idle_browse_smoke");
}

static int webkit_compat_gate_smoke_enabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "webkit_compat_gate_smoke");
}

static int webkit_js_disabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_disabled(buf, "webkit_js");
}

static int webkit_disable_gdk_gl_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "webkit_gdk_gl_disable");
}

static int webkit_reopen_count_from_cmdline(void)
{
    char buf[512];
    int count = 1;

    if (read_cmdline(buf, sizeof(buf)) == 0)
        count = cmdline_int_value(buf, "webkit_reopen", count);
    if (count < 1)
        count = 1;
    if (count > 10)
        count = 10;
    return count;
}

static int webkit_timeout_ms_from_cmdline(int fallback)
{
    char buf[512];
    int timeout_ms = fallback;

    if (read_cmdline(buf, sizeof(buf)) == 0) {
        const char *key = "webkit_timeout_ms";
        size_t key_len = strlen(key);
        const char *p = buf;

        while (*p) {
            while (*p == ' ' || *p == '\t' || *p == '\n')
                p++;
            if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
                timeout_ms = atoi(p + key_len + 1);
                break;
            }
            while (*p && *p != ' ' && *p != '\t' && *p != '\n')
                p++;
        }
    }
    if (timeout_ms < 0)
        timeout_ms = 0;
    if (timeout_ms > 600000)
        timeout_ms = 600000;
    return timeout_ms;
}

static int webkit_url_has_scheme(const char *s)
{
    size_t i;

    if (!s || !((s[0] >= 'A' && s[0] <= 'Z') ||
                (s[0] >= 'a' && s[0] <= 'z')))
        return 0;
    for (i = 1; s[i]; i++) {
        if (s[i] == ':')
            return 1;
        if (s[i] == '/' || s[i] == '?' || s[i] == '#')
            return 0;
        if (!((s[i] >= 'A' && s[i] <= 'Z') ||
              (s[i] >= 'a' && s[i] <= 'z') ||
              (s[i] >= '0' && s[i] <= '9') ||
              s[i] == '+' || s[i] == '-' || s[i] == '.'))
            return 0;
    }
    return 0;
}

static int webkit_url_looks_like_host(const char *s)
{
    int saw_dot = 0;
    size_t i;

    if (!s || !s[0])
        return 0;
    for (i = 0; s[i] && s[i] != '/' && s[i] != '?' && s[i] != '#'; i++) {
        if (s[i] == '.')
            saw_dot = 1;
        if (!(s[i] == '.' || s[i] == '-' ||
              (s[i] >= '0' && s[i] <= '9') ||
              (s[i] >= 'A' && s[i] <= 'Z') ||
              (s[i] >= 'a' && s[i] <= 'z')))
            return 0;
    }
    return saw_dot || strncmp(s, "localhost", 9) == 0;
}

static void copy_prefixed_url(char *out, size_t out_size, const char *prefix, const char *value)
{
    size_t prefix_len = strlen(prefix);
    size_t value_len = strlen(value);

    if (out_size == 0)
        return;
    if (prefix_len > out_size - 1)
        prefix_len = out_size - 1;
    if (value_len > out_size - prefix_len - 1)
        value_len = out_size - prefix_len - 1;
    memcpy(out, prefix, prefix_len);
    memcpy(out + prefix_len, value, value_len);
    out[prefix_len + value_len] = '\0';
}

static void normalize_webkit_url(const char *in, char *out, size_t out_size)
{
    char tmp[WEBKIT_URL_MAX];
    size_t len = 0;

    if (out_size == 0)
        return;
    if (!in)
        in = "";
    while (*in == ' ' || *in == '\t' || *in == '\n')
        in++;
    while (in[len] && in[len] != ' ' && in[len] != '\t' &&
           in[len] != '\n' && len + 1 < sizeof(tmp)) {
        tmp[len] = in[len];
        len++;
    }
    tmp[len] = '\0';
    out[0] = '\0';

    if (tmp[0] == '\0') {
        snprintf(out, out_size, WEBKIT_DEFAULT_URL);
    } else if (strncmp(tmp, "http//", 6) == 0) {
        copy_prefixed_url(out, out_size, "http://", tmp + 6);
    } else if (strncmp(tmp, "https//", 7) == 0) {
        copy_prefixed_url(out, out_size, "https://", tmp + 7);
    } else if (strncmp(tmp, "http:/", 6) == 0 &&
               strncmp(tmp, "http://", 7) != 0) {
        copy_prefixed_url(out, out_size, "http://", tmp + 6);
    } else if (strncmp(tmp, "https:/", 7) == 0 &&
               strncmp(tmp, "https://", 8) != 0) {
        copy_prefixed_url(out, out_size, "https://", tmp + 7);
    } else if (webkit_url_has_scheme(tmp)) {
        snprintf(out, out_size, "%s", tmp);
    } else if (tmp[0] == '/') {
        copy_prefixed_url(out, out_size, "file://", tmp);
    } else if (webkit_url_looks_like_host(tmp)) {
        copy_prefixed_url(out, out_size, "https://", tmp);
    } else {
        snprintf(out, out_size, "%s", tmp);
    }
}

static void webkit_url_from_cmdline(char *out, size_t out_size)
{
    char buf[2048];
    const char *key = "webkit_url=";
    size_t key_len = strlen(key);
    const char *p;

    if (out_size == 0)
        return;

    if (webkit_webgl_smoke_enabled_by_cmdline()) {
        snprintf(out, out_size, "file:///share/webkit/gpu-webgl-smoke.html");
        return;
    }
    if (webkit_gpu_smoke_enabled_by_cmdline()) {
        snprintf(out, out_size, "file:///share/webkit/gpu-smoke.html");
        return;
    }
    if (read_cmdline(buf, sizeof(buf)) < 0) {
        normalize_webkit_url(WEBKIT_DEFAULT_URL, out, out_size);
        return;
    }

    p = buf;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n')
            p++;
        if (strncmp(p, key, key_len) == 0) {
            size_t i = 0;
            p += key_len;
            while (p[i] && p[i] != ' ' && p[i] != '\t' && p[i] != '\n' &&
                   i + 1 < out_size) {
                out[i] = p[i];
                i++;
            }
            out[i] = '\0';
            if (out[0]) {
                normalize_webkit_url(out, out, out_size);
                return;
            }
            break;
        }
        while (*p && *p != ' ' && *p != '\t' && *p != '\n')
            p++;
    }

    if (webkit_http_smoke_enabled_by_cmdline()) {
        snprintf(out, out_size, "http://127.0.0.1:18080/");
        return;
    }

    normalize_webkit_url(WEBKIT_DEFAULT_URL, out, out_size);
}

static int glsmoke_enabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return !token_is_disabled(buf, "glsmoke") &&
           strstr(buf, "glsmoke=1") != NULL;
}

static int glsmoke_compat_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "glsmoke_compat");
}

static int glsmoke_native_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "glsmoke_native");
}

static int glsmoke_demo_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "glsmoke_demo");
}

static int gpu_validate_enabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "gpu_validate");
}

static int glmaze_enabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "glmaze");
}

static void glmaze_args_from_cmdline(char *frames_arg, size_t frames_size)
{
    char buf[512];
    int frames = 240;

    if (read_cmdline(buf, sizeof(buf)) == 0)
        frames = cmdline_int_value(buf, "glmaze_frames", frames);
    if (frames < 1)
        frames = 1;
    if (frames > 20000)
        frames = 20000;
    snprintf(frames_arg, frames_size, "--frames=%d", frames);
}

static void glsmoke_args_from_cmdline(char *frames_arg, size_t frames_size,
                                      char *loops_arg, size_t loops_size,
                                      char *resize_arg, size_t resize_size)
{
    char buf[512];
    int frames = 120;
    int loops = 1;
    int resize_every = 0;

    if (read_cmdline(buf, sizeof(buf)) == 0) {
        frames = cmdline_int_value(buf, "glsmoke_frames", frames);
        loops = cmdline_int_value(buf, "glsmoke_loops", loops);
        resize_every = cmdline_int_value(buf, "glsmoke_resize_every",
                                         resize_every);
    }
    snprintf(frames_arg, frames_size, "--frames=%d", frames);
    snprintf(loops_arg, loops_size, "--loops=%d", loops);
    if (resize_every > 0)
        snprintf(resize_arg, resize_size, "--resize-every=%d", resize_every);
    else if (resize_size > 0)
        resize_arg[0] = '\0';
}

int main(void)
{
    install_signal_handlers();

    if (desktop_disabled_by_cmdline()) {
        fprintf(stderr, "[desktop] disabled by cmdline\n");
        return 0;
    }

    fprintf(stderr, "[desktop] starting Wayland session\n");

    /* 1. Launch compositor */
    wlcomp_pid = launch_wlcomp();
    if (wlcomp_pid < 0) {
        perror("[desktop] fork wlcomp");
        return 1;
    }
    fprintf(stderr, "[desktop] wlcomp pid=%d\n", wlcomp_pid);

    /* 2. Wait for Wayland socket */
    if (wait_for_socket() < 0) {
        fprintf(stderr, "[desktop] timed out waiting for %s\n",
                WAYLAND_SOCKET_PATH);
        cleanup();
        return 1;
    }

    if (webkit_enabled_by_cmdline()) {
        if (!webkit_js_smoke_enabled_by_cmdline() &&
            access("/share/gstreamer-1.0/registry.x86_64.bin", R_OK) == 0) {
            gst_registry_ready = 1;
            fprintf(stderr,
                    "[desktop] using staged GStreamer registry\n");
        } else {
            gst_warmup_pid = launch_gst_registry_warmup();
        }
        if (gst_warmup_pid > 0)
            fprintf(stderr, "[desktop] GStreamer registry warmup pid=%d\n",
                    gst_warmup_pid);
        else if (!gst_registry_ready)
            fprintf(stderr,
                    "[desktop] failed to start GStreamer registry warmup\n");
    }

    if (gpu_validate_enabled_by_cmdline()) {
        client_pid = launch_gpu_substrate_validate();
        if (client_pid < 0) {
            perror("[desktop] fork GPU substrate validator");
            cleanup();
            return 1;
        }
        fprintf(stderr, "[desktop] GPU substrate validator pid=%d\n",
                client_pid);
        while (g_running && client_pid > 0) {
            int status;
            pid_t exited = waitpid(-1, &status, WNOHANG);

            desktop_process_pending_signals();
            if (!g_running)
                break;

            if (exited == wlcomp_pid) {
                fprintf(stderr, "[desktop] wlcomp exited (status %d)\n",
                        WIFEXITED(status) ? WEXITSTATUS(status) : status);
                wlcomp_pid = 0;
                cleanup();
                return 1;
            }
            if (exited == client_pid) {
                int ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;

                fprintf(stderr,
                        "[desktop] GPU substrate validator exited "
                        "(status %d)\n",
                        WIFEXITED(status) ? WEXITSTATUS(status) : status);
                client_pid = 0;
                cleanup();
                return ok ? 0 : 1;
            }
            usleep(100000);
        }
        cleanup();
        return 0;
    }

    /* 3. Launch the requested Wayland client. */
    if (glmaze_enabled_by_cmdline()) {
        char frames_arg[32];

        glmaze_args_from_cmdline(frames_arg, sizeof(frames_arg));
        client_pid = launch_client("/bin/glmaze", "glmaze", frames_arg, NULL,
                                   NULL);
        if (client_pid < 0) {
            perror("[desktop] fork glmaze");
            cleanup();
            return 1;
        }
        fprintf(stderr, "[desktop] glmaze pid=%d %s\n", client_pid,
                frames_arg);
        if (desktop_exit_after_smoke_by_cmdline()) {
            while (g_running && client_pid > 0) {
                int status;
                pid_t exited = waitpid(-1, &status, WNOHANG);

                desktop_process_pending_signals();
                if (!g_running)
                    break;

                if (exited == wlcomp_pid) {
                    fprintf(stderr, "[desktop] wlcomp exited (status %d)\n",
                            WIFEXITED(status) ? WEXITSTATUS(status) : status);
                    wlcomp_pid = 0;
                    cleanup();
                    return 1;
                }
                if (exited == client_pid) {
                    int ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;

                    print_glmaze_status_file();
                    fprintf(stderr, "[desktop] glmaze exited (status %d)\n",
                            WIFEXITED(status) ? WEXITSTATUS(status) : status);
                    client_pid = 0;
                    cleanup();
                    return ok ? 0 : 1;
                }
                usleep(100000);
            }
            cleanup();
            return 0;
        }
    }

    if (glsmoke_enabled_by_cmdline()) {
        char frames_arg[32];
        char loops_arg[32];
        char resize_arg[32];
        int compat = glsmoke_compat_by_cmdline();
        int native = !compat && glsmoke_native_by_cmdline();
        int demo = !compat && glsmoke_demo_by_cmdline();
        const char *client_path = compat ? "/bin/glsmoke" :
                      demo ? "/bin/mesawlegl" :
                                  native ? "/bin/mesawlegl" :
                                           "/bin/mesaglsmoke";
        const char *client_name = compat ? "glsmoke" :
                          demo ? "mesawlegl" :
                                          native ? "mesawlegl" : "mesaglsmoke";

        glsmoke_args_from_cmdline(frames_arg, sizeof(frames_arg), loops_arg,
                                  sizeof(loops_arg), resize_arg,
                                  sizeof(resize_arg));
        client_pid = demo ?
            launch_client(client_path, client_name, "--demo", frames_arg,
                          loops_arg) :
            launch_client(client_path, client_name, frames_arg, loops_arg,
                          resize_arg[0] ? resize_arg : NULL);
        if (client_pid < 0) {
            perror("[desktop] fork GL smoke");
            cleanup();
            return 1;
        }
        glsmoke_pid = client_pid;
        fprintf(stderr, "[desktop] %s pid=%d %s %s %s\n", client_name,
                client_pid, demo ? "--demo" : frames_arg,
                frames_arg, demo ? loops_arg : resize_arg);
        if (webkit_enabled_by_cmdline()) {
            while (g_running && client_pid > 0) {
                int status;
                pid_t exited = waitpid(-1, &status, WNOHANG);

                desktop_process_pending_signals();
                if (!g_running)
                    break;

                if (exited == wlcomp_pid) {
                    fprintf(stderr, "[desktop] wlcomp exited (status %d)\n",
                            WEXITSTATUS(status));
                    wlcomp_pid = 0;
                    cleanup();
                    return 1;
                }
                if (exited == client_pid) {
                    write_child_status_file("/tmp/glsmoke-status", client_name,
                                            exited, status);
                    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                        fprintf(stderr,
                                "[desktop] GL smoke exited (status %d)\n",
                                WIFEXITED(status) ? WEXITSTATUS(status) :
                                                    status);
                    }
                    client_pid = 0;
                    glsmoke_pid = 0;
                    break;
                }
                usleep(100000);
            }
        }
    }

    if (webkit_enabled_by_cmdline()) {
        int accel = webkit_accel_enabled_by_cmdline();
        int webkit_reopen_left = webkit_reopen_count_from_cmdline();
        int webkit_api_smoke = webkit_api_smoke_enabled_by_cmdline();
        int webkit_timeout_ms = webkit_timeout_ms_from_cmdline(
            webkit_api_smoke ? 15000 : 0);
        int webkit_watchdog_ms = webkit_api_smoke && webkit_timeout_ms > 0 ?
            webkit_timeout_ms + 10000 : webkit_timeout_ms;
        long long launch_ms = 0;
        const char *webkit_path = webkit_api_smoke ?
            "/bin/webkitgpusmoke" : "/libexec/webkit2gtk-4.1/MiniBrowser";
        const char *webkit_name = webkit_api_smoke ?
            "webkitgpusmoke" : "MiniBrowser";
        char webkit_timeout_arg[24];
        const char *webkit_timeout = NULL;
        char webkit_url[WEBKIT_URL_MAX];
        long long next_probe_ms = 0;
        int webkit_failed = 0;

        if (webkit_http_smoke_enabled_by_cmdline()) {
            httpd_pid = launch_http_smoke_server();
            if (httpd_pid < 0) {
                perror("[desktop] fork HTTP smoke server");
                cleanup();
                return 1;
            }
            fprintf(stderr, "[desktop] HTTP smoke server pid=%d\n",
                    httpd_pid);
            usleep(100000);
        }

        if (webkit_api_smoke) {
            snprintf(webkit_timeout_arg, sizeof(webkit_timeout_arg), "%d",
                     webkit_timeout_ms);
            webkit_timeout = webkit_timeout_arg;
        }
        webkit_url_from_cmdline(webkit_url, sizeof(webkit_url));
        if (webkit_http_smoke_enabled_by_cmdline()) {
            const char *path = strstr(webkit_url, "://");

            if (path) {
                path = strchr(path + 3, '/');
                if (!path)
                    path = "/";
            } else {
                path = "/";
            }
            http_smoke_self_probe(path);
        }
        if (url_needs_network_wait(webkit_url)) {
            fprintf(stderr,
                    "[desktop] waiting for network before WebKit URL %s\n",
                    webkit_url);
            sync_resolv_conf_from_netconf(WEBKIT_NET_WAIT_US);
        }
        wait_for_gst_registry_warmup(WEBKIT_GST_WAIT_US);
        client_pid = launch_client(webkit_path, webkit_name, webkit_url,
                                   webkit_timeout, NULL);
        if (client_pid < 0) {
            perror("[desktop] fork WebKit");
            cleanup();
            return 1;
        }
        fprintf(stderr,
                "[desktop] %s pid=%d accel=%d reopen_left=%d timeout_ms=%d "
                "watchdog_ms=%d url=%s\n",
                webkit_name, client_pid, accel, webkit_reopen_left,
                webkit_timeout_ms, webkit_watchdog_ms, webkit_url);
        launch_ms = monotonic_ms();
        next_probe_ms = launch_ms + 5000;

        while (g_running) {
            int status;
            long long now_ms;
            pid_t exited = waitpid(-1, &status, WNOHANG);

            desktop_process_pending_signals();
            if (!g_running)
                break;

            if (exited > 0) {
                if (exited == wlcomp_pid) {
                    fprintf(stderr, "[desktop] wlcomp exited (status %d)\n",
                            WEXITSTATUS(status));
                    wlcomp_pid = 0;
                    break;
                }
                if (exited == client_pid) {
                    int ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;

                    if (webkit_api_smoke)
                        webkit_print_log_evidence(ok ? "client-exit-ok" :
                                                        "client-exit-failed");
                    if (!ok) {
                        fprintf(stderr,
                                "[desktop] client exited (status %d)\n",
                                WIFEXITED(status) ? WEXITSTATUS(status) :
                                                    status);
                        webkit_failed = 1;
                        break;
                    }
                    client_pid = 0;
                    if (webkit_reopen_left > 1) {
                        webkit_reopen_left--;
                        client_pid = launch_client(webkit_path, webkit_name,
                                                   webkit_url, webkit_timeout,
                                                   NULL);
                        if (client_pid < 0) {
                            perror("[desktop] refork WebKit");
                            break;
                        }
                        fprintf(stderr,
                                "[desktop] relaunched %s pid=%d "
                                "reopen_left=%d url=%s\n",
                                webkit_name, client_pid, webkit_reopen_left,
                                webkit_url);
                        launch_ms = monotonic_ms();
                    } else if (webkit_api_smoke) {
                        fprintf(stderr,
                                "[desktop] WebKit API reopen smoke complete\n");
                        fprintf(stderr, "__WEBKIT_API_SMOKE_DONE_0__\n");
                        break;
                    }
                }
            }
            usleep(100000);
            now_ms = monotonic_ms();
            if (client_pid > 0 && now_ms >= next_probe_ms) {
                webkit_print_runtime_probe();
                next_probe_ms = now_ms + 5000;
            }
            if (webkit_watchdog_ms > 0 && client_pid > 0 &&
                now_ms - launch_ms >= webkit_watchdog_ms) {
                fprintf(stderr,
                        "[desktop] WebKit watchdog reached, closing pid=%d\n",
                        client_pid);
                if (webkit_api_smoke)
                    webkit_print_log_evidence("watchdog-before-close");
                kill_and_reap(&client_pid);
                if (webkit_reopen_left > 1) {
                    webkit_reopen_left--;
                    client_pid = launch_client(webkit_path, webkit_name,
                                               webkit_url, webkit_timeout,
                                               NULL);
                    if (client_pid < 0) {
                        perror("[desktop] refork WebKit after timeout");
                        break;
                    }
                    fprintf(stderr,
                            "[desktop] relaunched %s pid=%d reopen_left=%d "
                            "url=%s\n",
                            webkit_name, client_pid, webkit_reopen_left,
                            webkit_url);
                    launch_ms = monotonic_ms();
                } else {
                    fprintf(stderr,
                            "[desktop] WebKit timeout smoke complete\n");
                    fprintf(stderr, "__WEBKIT_API_SMOKE_DONE_0__\n");
                    break;
                }
            }
        }
        if (webkit_failed) {
            fprintf(stderr, "[desktop] WebKit smoke failed\n");
            cleanup();
            return 1;
        }
        if (webkit_api_smoke && !desktop_exit_after_smoke_by_cmdline()) {
            client_pid = 0;
            fprintf(stderr,
                    "[desktop] WebKit API smoke complete; keeping Wayland "
                    "session alive\n");
        } else {
            fprintf(stderr, g_shutdown_requested ?
                    "[desktop] shutting down after signal\n" :
                    "[desktop] shutting down\n");
            cleanup();
            return 0;
        }
    } else if (netsurf_disabled_by_cmdline()) {
        client_pid = 0;
        fprintf(stderr, "[desktop] netsurf disabled by cmdline\n");
    } else {
        client_pid = launch_client("/bin/netsurf", "netsurf", NULL, NULL, NULL);
        if (client_pid < 0) {
            perror("[desktop] fork netsurf");
            cleanup();
            return 1;
        }
        fprintf(stderr, "[desktop] netsurf pid=%d\n", client_pid);
    }

    /* 4. Supervise compositor and client */
    while (g_running) {
        int status;
        pid_t exited = waitpid(-1, &status, WNOHANG);

        desktop_process_pending_signals();
        if (!g_running)
            break;

        if (exited > 0) {
            if (exited == wlcomp_pid) {
                fprintf(stderr, "[desktop] wlcomp exited (status %d)\n",
                        WEXITSTATUS(status));
                wlcomp_pid = 0;
                break;  /* compositor gone → session over */
            } else if (exited == glsmoke_pid) {
                write_child_status_file("/tmp/glsmoke-status", "glsmoke",
                                        exited, status);
                if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                    fprintf(stderr, "[desktop] GL smoke exited (status %d)\n",
                            WIFEXITED(status) ? WEXITSTATUS(status) : status);
                }
                glsmoke_pid = 0;
            } else if (exited == client_pid) {
                if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                    fprintf(stderr, "[desktop] client exited (status %d)\n",
                            WIFEXITED(status) ? WEXITSTATUS(status) : status);
                }
                client_pid = 0;
            }
        }
        usleep(100000);  /* 100 ms poll */
    }

    fprintf(stderr, g_shutdown_requested ?
            "[desktop] shutting down after signal\n" :
            "[desktop] shutting down\n");
    cleanup();
    return 0;
}
