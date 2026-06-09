#include "wlcomp_launcher.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "wlcomp_iwin_text.h"

#define MAX_CHILDREN 16
#define WEBKIT_NET_WAIT_US 35000000
#define WEBKIT_DEFAULT_URL "https://www.google.com/search?q=xv6&gbv=1"
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
#define D3D12_DISPLAY_BIND_FIELD_MAX 64
static const char *webkit_feature_flags =
    "--features=+OffscreenCanvas,+OffscreenCanvasInWorkers,+requestIdleCallback";
static const char *webkit_youtube_compat_user_agent =
    "--user-agent=Mozilla/5.0 (X11; xv6 x86_64) AppleWebKit/605.1.15 "
    "(KHTML, like Gecko) Version/17.0 Safari/605.1.15";
static pid_t g_children[MAX_CHILDREN];
static pid_t g_pending_reap[MAX_CHILDREN];
static uint32_t g_child_launch_ms[MAX_CHILDREN];
static char g_child_name[MAX_CHILDREN][32];

static struct wlcomp_launcher_ops g_ops;

void wlcomp_launcher_init(const struct wlcomp_launcher_ops *ops)
{
    if (ops)
        g_ops = *ops;
}

static uint32_t launcher_get_time_ms(void)
{
    return g_ops.get_time_ms ? g_ops.get_time_ms() : 0;
}

static int launcher_cmdline_flag_enabled(const char *key)
{
    return g_ops.cmdline_flag_enabled ? g_ops.cmdline_flag_enabled(key) : 0;
}

static int launcher_cmdline_int_value(const char *key, int fallback)
{
    return g_ops.cmdline_int_value ?
        g_ops.cmdline_int_value(key, fallback) : fallback;
}

static int launcher_virgl_available(void)
{
    return g_ops.virgl_available ? g_ops.virgl_available() : 0;
}

struct launcher_fb_gpu_backend_info {
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

struct launcher_d3d12_native_present_evidence {
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

static int launcher_gpu_backend_query(struct launcher_fb_gpu_backend_info *info)
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

static int launcher_dxg_render_node_available(void)
{
    struct launcher_fb_gpu_backend_info info;

    return launcher_gpu_backend_query(&info) &&
           info.backend == FB_GPU_BACKEND_HYPERV_DXG &&
           (info.flags & FB_GPU_BACKEND_F_RENDER_NODE) != 0 &&
           (info.flags & FB_GPU_BACKEND_F_DXG_TRANSPORT) != 0;
}

static int launcher_opengl_submit_available(void)
{
    struct launcher_fb_gpu_backend_info info;

    return launcher_gpu_backend_query(&info) &&
           (info.flags & FB_GPU_BACKEND_F_OPENGL_SUBMIT) != 0;
}

static int launcher_evidence_token_separator(char c)
{
    return c == '\0' || c == '\n' || c == '\r' ||
           c == ' ' || c == '\t';
}

static int launcher_evidence_token_start(const char *text, const char *p)
{
    return p == text || p[-1] == '\n' || p[-1] == '\r' ||
           p[-1] == ' ' || p[-1] == '\t';
}

static const char *launcher_evidence_key_value(const char *text,
                                               const char *key)
{
    size_t key_len;
    const char *p;

    if (!text || !key || !key[0])
        return NULL;
    key_len = strlen(key);
    for (p = text; *p; p++) {
        if (!launcher_evidence_token_start(text, p))
            continue;
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=')
            return p + key_len + 1;
    }
    return NULL;
}

static int launcher_evidence_key_u64(const char *text, const char *key,
                                     uint64_t *out)
{
    const char *p;
    char *end = NULL;
    uint64_t value;

    if (!text || !key || !out)
        return 0;
    p = launcher_evidence_key_value(text, key);
    if (!p)
        return 0;
    errno = 0;
    value = strtoull(p, &end, 0);
    if (errno != 0 || end == p ||
        !launcher_evidence_token_separator(*end))
        return 0;
    *out = value;
    return 1;
}

static int launcher_evidence_key_string(const char *text, const char *key,
                                        char *out, size_t out_size)
{
    const char *p;
    size_t n = 0;

    if (!text || !key || !out || out_size == 0)
        return 0;
    out[0] = '\0';
    p = launcher_evidence_key_value(text, key);
    if (!p)
        return 0;
    while (!launcher_evidence_token_separator(p[n]))
        n++;
    if (n == 0 || n >= out_size)
        return 0;
    memcpy(out, p, n);
    out[n] = '\0';
    return 1;
}

static void launcher_evidence_key_u64_alias_max(const char *text,
                                                const char *key,
                                                uint64_t *out)
{
    uint64_t value = 0;

    if (!out)
        return;
    if (launcher_evidence_key_u64(text, key, &value) && value > *out)
        *out = value;
}

static int launcher_d3d12_native_present_evidence_read(
    const char *text, struct launcher_d3d12_native_present_evidence *out)
{
    if (!text || !out)
        return 0;
    memset(out, 0, sizeof(*out));
    return launcher_evidence_key_string(text, "display_bind_backend",
                                        out->display_bind_backend,
                                        sizeof(out->display_bind_backend)) &&
           launcher_evidence_key_string(text, "display_bind_transport",
                                        out->display_bind_transport,
                                        sizeof(out->display_bind_transport)) &&
           launcher_evidence_key_string(
               text, "display_bind_transport_source",
               out->display_bind_transport_source,
               sizeof(out->display_bind_transport_source)) &&
           launcher_evidence_key_u64(
               text, "host_saw_display_bind_packet",
               &out->host_saw_display_bind_packet) &&
           launcher_evidence_key_u64(
               text, "wsl_presenthistory_completion_credit",
               &out->wsl_presenthistory_completion_credit) &&
           launcher_evidence_key_u64(text, "display_bind_present_id",
                                     &out->display_bind_present_id) &&
           launcher_evidence_key_u64(text, "display_bind_completed_id",
                                     &out->display_bind_completed_id) &&
           launcher_evidence_key_u64(
               text, "display_bind_resource_generation",
               &out->display_bind_resource_generation) &&
           launcher_evidence_key_string(
               text, "display_bind_completion_source",
               out->display_bind_completion_source,
               sizeof(out->display_bind_completion_source));
}

static int launcher_d3d12_native_present_evidence_valid(
    const struct launcher_d3d12_native_present_evidence *evidence)
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

static int launcher_d3d12_present_evidence_fresh(void)
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

static int launcher_read_file(const char *path, char *buf, size_t buf_size)
{
    int fd;
    ssize_t n;

    if (!path || !buf || buf_size == 0)
        return 0;
    buf[0] = '\0';
    fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    n = read(fd, buf, buf_size - 1);
    close(fd);
    if (n <= 0)
        return 0;
    buf[n] = '\0';
    return 1;
}

static int launcher_d3d12_present_evidence_valid(
    const char *expected_run_id,
    const struct launcher_fb_gpu_backend_info *info)
{
    char evidence[16384];
    char source_luid[32];
    char matched_luid[32];
    char run_id[128];
    char compositor_run_id[128];
    char seal_run_id[128];
    char seal_end_run_id[128];
    char path[96];
    char content_progress_state[96];
    char visible_content_progress[96];
    uint64_t evidence_generation = 0;
    uint64_t evidence_time_us = 0;
    uint64_t seal_begin = 0;
    uint64_t seal_end = 0;
    uint64_t seal_complete = 0;
    uint64_t seal_generation = 0;
    uint64_t seal_end_generation = 0;
    uint64_t present_rejected = 0;
    uint64_t resource = 0;
    uint64_t allocations = 0;
    uint64_t fence = 0;
    uint64_t fence_target = 0;
    uint64_t release_fence = 0;
    uint64_t present_complete = 0;
    uint64_t present_id = 0;
    uint64_t completed = 0;
    uint64_t display_handoff = 0;
    uint64_t requirements_satisfied = 0;
    uint64_t current_run_valid = 0;
    uint64_t client_pid = 0;
    uint64_t identity_client_pid = 0;
    uint64_t client_buffer_id = 0;
    uint64_t identity_client_buffer_id = 0;
    uint64_t manager_resource_id = 0;
    uint64_t identity_manager_resource_id = 0;
    uint64_t buffer_generation = 0;
    uint64_t identity_buffer_generation = 0;
    uint64_t source_luid_valid = 0;
    uint64_t present_same_luid = 0;
    uint64_t buffer_completion_correlated = 0;
    uint64_t native_present_attempt_id = 0;
    uint64_t native_present_completion_id = 0;
    uint64_t callback_release_required = 0;
    uint64_t same_frame_observed = 0;
    uint64_t frame_callback_observed = 0;
    uint64_t buffer_release_observed = 0;
    uint64_t buffer_release_same_resource = 0;
    uint64_t buffer_release_same_generation = 0;
    uint64_t buffer_release_same_attempt = 0;
    uint64_t buffer_release_same_present_id = 0;
    uint64_t buffer_release_present_id = 0;
    uint64_t buffer_release_completion_id = 0;
    uint64_t frame_callback_same_resource = 0;
    uint64_t frame_callback_same_generation = 0;
    uint64_t frame_callback_same_attempt = 0;
    uint64_t frame_callback_same_present_id = 0;
    uint64_t frame_callback_present_id = 0;
    uint64_t frame_callback_completion_id = 0;
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
    uint64_t cpu_readback = 1;
    uint64_t cpu_mapping = 1;
    uint64_t cpu_copy = 1;
    char content_display_bind_completion_source[96];
    struct launcher_d3d12_native_present_evidence native_present;
    int evidence_loaded = 0;
    int render_node = 0;
    int dxg_transport = 0;
    int d3dkmt = 0;
    int opengl_submit = 0;
    int same_adapter;
    int no_readback;
    int shared_resource;
    int fence_ok;
    int run_id_match;
    int client_identity_ok;
    int native_present_skeleton_ok;
    int native_present_ok;
    int callback_release_ok;
    int content_progress_ok;
    int gate_open;

    memset(evidence, 0, sizeof(evidence));
    source_luid[0] = '\0';
    matched_luid[0] = '\0';
    run_id[0] = '\0';
    compositor_run_id[0] = '\0';
    seal_run_id[0] = '\0';
    seal_end_run_id[0] = '\0';
    path[0] = '\0';
    content_progress_state[0] = '\0';
    visible_content_progress[0] = '\0';
    content_display_bind_completion_source[0] = '\0';
    memset(&native_present, 0, sizeof(native_present));
    if (info) {
        render_node = (info->flags & FB_GPU_BACKEND_F_RENDER_NODE) != 0;
        dxg_transport = (info->flags & FB_GPU_BACKEND_F_DXG_TRANSPORT) != 0;
        d3dkmt = (info->flags & FB_GPU_BACKEND_F_D3DKMT) != 0;
        opengl_submit =
            (info->flags & FB_GPU_BACKEND_F_OPENGL_SUBMIT) != 0;
    }

    if (launcher_d3d12_present_evidence_fresh() &&
        launcher_read_file(XV6_D3D12_PRESENT_EVIDENCE_PATH, evidence,
                           sizeof(evidence))) {
        evidence_loaded = 1;
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_evidence_seal_begin",
                                        &seal_begin);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_evidence_seal_end",
                                        &seal_end);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_evidence_seal_complete",
                                        &seal_complete);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_evidence_seal_generation",
                                        &seal_generation);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_evidence_seal_end_generation",
                                        &seal_end_generation);
        (void)launcher_evidence_key_string(evidence,
                                           "d3d12_evidence_seal_run_id",
                                           seal_run_id,
                                           sizeof(seal_run_id));
        (void)launcher_evidence_key_string(evidence,
                                           "d3d12_evidence_seal_end_run_id",
                                           seal_end_run_id,
                                           sizeof(seal_end_run_id));
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_present_evidence_time_us",
                                        &evidence_time_us);
        (void)launcher_evidence_key_u64(evidence, "d3d12_evidence_generation",
                                        &evidence_generation);
        (void)launcher_evidence_key_u64(evidence, "d3d12_present_rejected",
                                        &present_rejected);
        (void)launcher_evidence_key_u64(evidence, "d3d12_present_resource",
                                        &resource);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_present_allocation_count",
                                        &allocations);
        (void)launcher_evidence_key_u64(evidence, "d3d12_present_fence",
                                        &fence);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_present_fence_target",
                                        &fence_target);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_present_release_fence",
                                        &release_fence);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_gpu_present_complete",
                                        &present_complete);
        (void)launcher_evidence_key_u64(evidence, "d3d12_dxg_present_id",
                                        &present_id);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_dxg_present_completed",
                                        &completed);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_present_source_buffer_completion_correlated",
                                        &buffer_completion_correlated);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_display_handoff_implemented",
                                        &display_handoff);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_native_present_requirements_satisfied",
                                        &requirements_satisfied);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_present_identity_current_run_valid",
                                        &current_run_valid);
        (void)launcher_evidence_key_u64(evidence, "d3d12_client_pid",
                                        &client_pid);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_present_identity_client_pid",
                                        &identity_client_pid);
        (void)launcher_evidence_key_u64(evidence, "d3d12_client_buffer_id",
                                        &client_buffer_id);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_present_identity_client_buffer_id",
                                        &identity_client_buffer_id);
        (void)launcher_evidence_key_u64(evidence, "d3d12_manager_resource_id",
                                        &manager_resource_id);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_present_identity_manager_resource_id",
                                        &identity_manager_resource_id);
        (void)launcher_evidence_key_u64(evidence, "d3d12_buffer_generation",
                                        &buffer_generation);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_present_identity_buffer_generation",
                                        &identity_buffer_generation);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_present_source_luid_valid",
                                        &source_luid_valid);
        (void)launcher_evidence_key_u64(evidence, "d3d12_present_same_luid",
                                        &present_same_luid);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_native_present_attempt_id",
                                        &native_present_attempt_id);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_native_present_completion_id",
                                        &native_present_completion_id);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_callback_release_same_frame_required",
                                        &callback_release_required);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_callback_release_same_frame_observed",
                                        &same_frame_observed);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_frame_callback_observed",
                                        &frame_callback_observed);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_buffer_release_observed",
                                        &buffer_release_observed);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_buffer_release_same_resource",
                                        &buffer_release_same_resource);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_buffer_release_same_generation",
                                        &buffer_release_same_generation);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_buffer_release_same_attempt",
                                        &buffer_release_same_attempt);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_buffer_release_same_present_id",
                                        &buffer_release_same_present_id);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_buffer_release_present_id",
                                        &buffer_release_present_id);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_buffer_release_completion_id",
                                        &buffer_release_completion_id);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_frame_callback_same_resource",
                                        &frame_callback_same_resource);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_frame_callback_same_generation",
                                        &frame_callback_same_generation);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_frame_callback_same_attempt",
                                        &frame_callback_same_attempt);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_frame_callback_same_present_id",
                                        &frame_callback_same_present_id);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_frame_callback_present_id",
                                        &frame_callback_present_id);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_frame_callback_completion_id",
                                        &frame_callback_completion_id);
        launcher_evidence_key_u64_alias_max(evidence,
                                            "d3d12_visible_content_crc",
                                            &content_crc);
        launcher_evidence_key_u64_alias_max(evidence,
                                            "d3d12_visible_content_frame",
                                            &content_frame);
        launcher_evidence_key_u64_alias_max(evidence,
                                            "d3d12_visible_content_frames",
                                            &content_frame);
        launcher_evidence_key_u64_alias_max(evidence,
                                            "d3d12_visible_frame_hash",
                                            &content_frame_hash);
        launcher_evidence_key_u64_alias_max(evidence,
                                            "d3d12_visible_content_frame_hash",
                                            &content_frame_hash);
        (void)launcher_evidence_key_string(evidence,
                                           "d3d12_content_progress_state",
                                           content_progress_state,
                                           sizeof(content_progress_state));
        (void)launcher_evidence_key_string(evidence,
                                           "d3d12_visible_content_progress",
                                           visible_content_progress,
                                           sizeof(visible_content_progress));
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_content_progress_requires_native_present",
                                        &content_requires_native);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_visible_content_requires_native_present_completion",
                                        &visible_requires_native);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_visible_content_credit_before_native_present",
                                        &visible_credit_before_native);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_content_progress_native_present_complete",
                                        &content_native_complete);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_content_progress_visible_credit",
                                        &content_visible_credit);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_content_progress_native_present_credit",
                                        &content_native_credit);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_content_progress_source_owned",
                                        &content_source_owned);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_content_progress_present_id",
                                        &content_present_id);
        (void)launcher_evidence_key_u64(evidence,
                                        "d3d12_content_progress_completed",
                                        &content_completed);
        (void)launcher_evidence_key_u64(
            evidence, "d3d12_content_progress_display_bind_present_id",
            &content_display_bind_present_id);
        (void)launcher_evidence_key_u64(
            evidence, "d3d12_content_progress_display_bind_completed_id",
            &content_display_bind_completed_id);
        (void)launcher_evidence_key_u64(
            evidence, "d3d12_content_progress_display_bind_resource_generation",
            &content_display_bind_resource_generation);
        (void)launcher_evidence_key_string(
            evidence, "d3d12_content_progress_display_bind_completion_source",
            content_display_bind_completion_source,
            sizeof(content_display_bind_completion_source));
        (void)launcher_evidence_key_u64(evidence, "d3d12_cpu_readback",
                                        &cpu_readback);
        (void)launcher_evidence_key_u64(evidence, "d3d12_cpu_mapping",
                                        &cpu_mapping);
        (void)launcher_evidence_key_u64(evidence, "d3d12_cpu_copy",
                                        &cpu_copy);
        (void)launcher_evidence_key_string(evidence, "d3d12_present_path",
                                           path, sizeof(path));
        (void)launcher_evidence_key_string(evidence, "d3d12_run_id",
                                           run_id, sizeof(run_id));
        (void)launcher_evidence_key_string(
            evidence, "d3d12_present_identity_compositor_run_id",
            compositor_run_id, sizeof(compositor_run_id));
        (void)launcher_evidence_key_string(evidence, "d3d12_present_luid",
                                           source_luid,
                                           sizeof(source_luid));
        (void)launcher_evidence_key_string(evidence,
                                           "d3d12_present_matched_luid",
                                           matched_luid,
                                           sizeof(matched_luid));
        (void)launcher_d3d12_native_present_evidence_read(
            evidence, &native_present);
    }
    same_adapter =
        source_luid_valid == 1 && present_same_luid == 1 &&
        source_luid[0] != '\0' && matched_luid[0] != '\0' &&
        strcmp(source_luid, "00000000:00000000") != 0 &&
        strcmp(matched_luid, "00000000:00000000") != 0 &&
        strcmp(source_luid, matched_luid) == 0;
    no_readback =
        cpu_readback == 0 && cpu_mapping == 0 && cpu_copy == 0;
    shared_resource = resource != 0 && allocations != 0;
    fence_ok = fence != 0 && fence_target != 0 && release_fence != 0;
    run_id_match = expected_run_id != NULL && expected_run_id[0] != '\0' &&
                   run_id[0] != '\0' && compositor_run_id[0] != '\0' &&
                   strcmp(run_id, expected_run_id) == 0 &&
                   strcmp(compositor_run_id, expected_run_id) == 0 &&
                   current_run_valid == 1;
    client_identity_ok =
        client_pid != 0 && identity_client_pid == client_pid &&
        client_buffer_id != 0 &&
        identity_client_buffer_id == client_buffer_id &&
        manager_resource_id != 0 &&
        identity_manager_resource_id == manager_resource_id &&
        buffer_generation != 0 &&
        identity_buffer_generation == buffer_generation;
    native_present_skeleton_ok =
        launcher_d3d12_native_present_evidence_valid(&native_present) &&
        native_present.display_bind_resource_generation == buffer_generation;
    native_present_ok =
        evidence_loaded && present_rejected == 0 && evidence_generation != 0 &&
        seal_begin == 1 && seal_end == 1 && seal_complete == 1 &&
        seal_generation == evidence_generation &&
        seal_end_generation == evidence_generation &&
        strcmp(seal_run_id, run_id) == 0 &&
        strcmp(seal_end_run_id, run_id) == 0 &&
        evidence_time_us != 0 && present_complete != 0 && present_id != 0 &&
        completed != 0 && buffer_completion_correlated == 1 &&
        display_handoff == 1 && requirements_satisfied == 1 &&
        native_present_attempt_id != 0 &&
        native_present_completion_id != 0 &&
        present_id == native_present.display_bind_present_id &&
        completed == native_present.display_bind_completed_id &&
        native_present_skeleton_ok &&
        strcmp(path, "d3d12-dxg-present-source-display-handoff") == 0;
    callback_release_ok =
        callback_release_required == 1 && same_frame_observed == 1 &&
        frame_callback_observed != 0 && buffer_release_observed != 0 &&
        buffer_release_same_resource == 1 &&
        buffer_release_same_generation == 1 &&
        buffer_release_same_attempt == 1 &&
        buffer_release_same_present_id == 1 &&
        buffer_release_present_id == present_id &&
        buffer_release_completion_id == native_present_completion_id &&
        frame_callback_same_resource == 1 &&
        frame_callback_same_generation == 1 &&
        frame_callback_same_attempt == 1 &&
        frame_callback_same_present_id == 1 &&
        frame_callback_present_id == present_id &&
        frame_callback_completion_id == native_present_completion_id;
    content_progress_ok =
        content_crc != 0 && content_frame != 0 &&
        content_frame_hash != 0 &&
        strcmp(content_progress_state, "NATIVE_PRESENT_COMPLETE") == 0 &&
        strcmp(visible_content_progress, "NATIVE_PRESENT_COMPLETE") == 0 &&
        content_requires_native == 1 && visible_requires_native == 1 &&
        visible_credit_before_native == 0 &&
        content_native_complete == 1 && content_visible_credit == 1 &&
        content_native_credit == 1 && content_source_owned == 1 &&
        content_present_id == native_present.display_bind_present_id &&
        content_completed == native_present.display_bind_completed_id &&
        content_display_bind_present_id ==
            native_present.display_bind_present_id &&
        content_display_bind_completed_id ==
            native_present.display_bind_completed_id &&
        content_display_bind_resource_generation ==
            native_present.display_bind_resource_generation &&
        strcmp(content_display_bind_completion_source, "display") == 0;
    gate_open =
        render_node && dxg_transport && d3dkmt && opengl_submit &&
        same_adapter && no_readback && shared_resource && fence_ok &&
        run_id_match && client_identity_ok && native_present_ok &&
        callback_release_ok && content_progress_ok;
    fprintf(stderr,
            "wlcomp: webkit_gpu_contract_matrix "
            "contract=d3d12-shared-surface "
            "run_id_match=%s same_adapter_luid=%s "
            "same_client_resource_generation=%s "
            "syncfile_acquire=%s native_present=%s "
            "content_progress=%s content_progress_state=%s "
            "visible_content_progress=%s content_crc=%s content_frame=%s "
            "content_frame_hash=%s "
            "display_bind_backend=%s display_bind_transport=%s "
            "display_bind_transport_source=%s "
            "host_saw_display_bind_packet=%lu "
            "wsl_presenthistory_completion_credit=%lu "
            "display_bind_present_id=%lu display_bind_completed_id=%lu "
            "display_bind_resource_generation=%lu "
            "display_bind_completion_source=%s "
            "evidence_seal=%s "
            "backend_opengl_submit=%d render_node=%d dxg_transport=%d "
            "d3dkmt=%d fps_gate=DEFERRED no_env_only=PASS "
            "title_only=REJECT chrome_only=REJECT cursor_only=REJECT "
            "render_node_only=REJECT dmabuf_only=REJECT "
            "no_dmabuf_only=PASS no_callback_only=%s "
            "callback_release_same_resource_generation=%s "
            "native_present_credit=%d opengl_submit_credit=%d "
            "status=%s expected_run_id=%s evidence_run_id=%s "
            "compositor_run_id=%s\n",
            run_id_match ? "PASS" : "FAIL",
            same_adapter ? "PASS" : "FAIL",
            client_identity_ok ? "PASS" : "FAIL",
            fence_ok ? "PASS" : "FAIL",
            native_present_ok ? "PASS" : "FAIL",
            content_progress_ok ? "PASS" : "FAIL",
            content_progress_state[0] ? content_progress_state : "MISSING",
            visible_content_progress[0] ? visible_content_progress : "MISSING",
            content_crc != 0 ? "PASS" : "MISSING",
            content_frame != 0 ? "PASS" : "MISSING",
            content_frame_hash != 0 ? "PASS" : "MISSING",
            native_present.display_bind_backend[0] ?
                native_present.display_bind_backend : "MISSING",
            native_present.display_bind_transport[0] ?
                native_present.display_bind_transport : "MISSING",
            native_present.display_bind_transport_source[0] ?
                native_present.display_bind_transport_source : "MISSING",
            (unsigned long)native_present.host_saw_display_bind_packet,
            (unsigned long)native_present.wsl_presenthistory_completion_credit,
            (unsigned long)native_present.display_bind_present_id,
            (unsigned long)native_present.display_bind_completed_id,
            (unsigned long)native_present.display_bind_resource_generation,
            native_present.display_bind_completion_source[0] ?
                native_present.display_bind_completion_source : "MISSING",
            (seal_begin == 1 && seal_end == 1 && seal_complete == 1 &&
             seal_generation == evidence_generation &&
             seal_end_generation == evidence_generation &&
             strcmp(seal_run_id, run_id) == 0 &&
             strcmp(seal_end_run_id, run_id) == 0) ? "PASS" : "FAIL",
            opengl_submit, render_node, dxg_transport, d3dkmt,
            callback_release_ok ? "PASS" : "FAIL",
            callback_release_ok ? "PASS" : "FAIL",
            gate_open ? 1 : 0,
            gate_open ? 1 : 0,
            gate_open ? "PASS" : "PASS_FAILCLOSED",
            expected_run_id ? expected_run_id : "",
            run_id, compositor_run_id);
    return gate_open;
}

static int launcher_d3d12_present_contract_available(
    const char *expected_run_id)
{
    struct launcher_fb_gpu_backend_info info;

    if (!launcher_gpu_backend_query(&info) ||
        info.backend != FB_GPU_BACKEND_HYPERV_DXG)
        return 0;
    return launcher_d3d12_present_evidence_valid(expected_run_id, &info);
}

static void launcher_destroy_surfaces_for_pid(pid_t pid)
{
    if (g_ops.destroy_surfaces_for_pid)
        g_ops.destroy_surfaces_for_pid(pid);
}

static void disable_child_coredumps(void)
{
    struct rlimit lim = {0, 0};
    (void)setrlimit(RLIMIT_CORE, &lim);
}

void wlcomp_launcher_terminate_pid(pid_t pid)
{
    if (pid <= 0)
        return;

    if (kill(-pid, SIGTERM) == 0) {
        kill(-pid, SIGKILL);
    } else {
        kill(pid, SIGTERM);
        kill(pid, SIGKILL);
    }
}

void wlcomp_launcher_remember_reap(pid_t pid)
{
    if (pid <= 0)
        return;

    for (int i = 0; i < MAX_CHILDREN; i++) {
        if (g_pending_reap[i] == pid)
            return;
    }
    for (int i = 0; i < MAX_CHILDREN; i++) {
        if (g_pending_reap[i] == 0) {
            g_pending_reap[i] = pid;
            return;
        }
    }
}

void wlcomp_launcher_signal_process_group(pid_t pid, int sig)
{
    if (pid <= 0)
        return;
    kill(-pid, sig);
    kill(pid, sig);
}

static int url_needs_network_wait(const char *url)
{
    return url && (strncmp(url, "http://", 7) == 0 ||
                   strncmp(url, "https://", 8) == 0) &&
           strncmp(url, "http://127.0.0.1", 16) != 0 &&
           strncmp(url, "http://localhost", 16) != 0;
}

void wlcomp_launcher_launch_args(const char *path, const char *name,
                                 const char *arg1, const char *arg2,
                                 const char *arg3)
{
    if (!path) return;
    if (!path[0] || access(path, X_OK) != 0) return;

    const char *app_name = (name && name[0]) ? name :
        wlcomp_path_basename(path);

    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < MAX_CHILDREN; i++) {
        if (g_children[i] <= 0) { slot = i; break; }
    }
    if (slot < 0) return;  /* all slots full */

    pid_t pid = fork();
    if (pid < 0) return;

    if (pid == 0) {
        setpgid(0, 0);

        /* Child: redirect stderr to a log file for debugging */
        /* Child: set up Wayland environment and exec */

        int is_netsurf = strcmp(app_name, "netsurf") == 0;
        int is_minibrowser = strcmp(app_name, "MiniBrowser") == 0;
        int is_webkitgpusmoke = strcmp(app_name, "webkitgpusmoke") == 0;
        int is_webkit = is_minibrowser || is_webkitgpusmoke;
        int is_mesa_gl = strcmp(app_name, "glmaze") == 0 ||
                         strcmp(app_name, "glsmoke") == 0 ||
                         strcmp(app_name, "mesademo") == 0 ||
                         strcmp(app_name, "mesawlegl") == 0 ||
                         strcmp(app_name, "mesaglsmoke") == 0 ||
                         strcmp(app_name, "mesaeglinfo") == 0;
        const char *minibrowser_url =
            (arg1 && arg1[0]) ? arg1 : WEBKIT_DEFAULT_URL;

        if (is_webkit)
            disable_child_coredumps();

        if (!is_webkitgpusmoke && !is_mesa_gl) {
            int logfd = open("/tmp/app_log.txt",
                             O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (logfd >= 0) {
                dup2(logfd, 1);  /* stdout */
                dup2(logfd, 2);  /* stderr */
                close(logfd);
            }
        }

        if (is_netsurf || is_webkit) {
            mkdir("/tmp/.cache", 0755);
            mkdir("/tmp/.cache/fontconfig", 0755);
            mkdir("/tmp/.local", 0755);
            mkdir("/tmp/.local/share", 0755);
            mkdir("/tmp/webkitgtk-4.1", 0755);
            mkdir("/var", 0755);
            mkdir("/var/tmp", 01777);
            chmod("/var/tmp", 01777);
        }

        /* For netsurf, create Choices file with ca_bundle + homepage */
        if (is_netsurf) {
            mkdir("/.netsurf", 0755);
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

        if (is_minibrowser && url_needs_network_wait(minibrowser_url)) {
            fprintf(stderr, "wlcomp: waiting for network before %s\n",
                    minibrowser_url);
            if (wlcomp_sync_resolv_conf_from_netconf(WEBKIT_NET_WAIT_US) != 0)
                usleep(WEBKIT_NET_WAIT_US);
        }

        char *argv_def[5];
        char *argv_noarg[] = { (char *)app_name, NULL };
        int argv_def_i = 0;

        argv_def[argv_def_i++] = (char *)app_name;
        if (arg1)
            argv_def[argv_def_i++] = (char *)arg1;
        if (arg2)
            argv_def[argv_def_i++] = (char *)arg2;
        if (arg3)
            argv_def[argv_def_i++] = (char *)arg3;
        argv_def[argv_def_i] = NULL;
        char *argv_minibrowser[] = {
            (char *)app_name,
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
            (char *)webkit_feature_flags,
            (char *)minibrowser_url,
            NULL,
        };
        char *argv_minibrowser_js[] = {
            (char *)app_name,
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
            (char *)webkit_feature_flags,
            (char *)minibrowser_url,
            NULL,
        };
        char *argv_minibrowser_accel[] = {
            (char *)app_name,
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
            (char *)webkit_feature_flags,
            (char *)minibrowser_url,
            NULL,
        };
        char *argv_minibrowser_accel_js[] = {
            (char *)app_name,
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
            (char *)webkit_feature_flags,
            (char *)minibrowser_url,
            NULL,
        };
        char webkit_gpu_run_id_value[64];
        char webkit_gpu_run_id_env[96];
        char webkit_gpu_validate_run_id_env[112];
        char webkit_wlcomp_d3d12_run_id_env[112];
        char webkit_gst_disable_gl_sink_env[40];
        char webkit_gst_dmabuf_sink_disabled_env[44];
        char webkit_gst_use_videoconvert_env[48];
        char webkit_dmabuf_renderer_disable_gbm_env[48];
        char webkit_force_dmabuf_renderer_env[36];
        char mesa_perf_log_env[32];
        char mesa_wayland_color_buffers_env[48];
        int mesa_color_buffers =
            launcher_cmdline_int_value("glsmoke_color_buffers", 0);

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
        snprintf(webkit_gst_disable_gl_sink_env,
                 sizeof(webkit_gst_disable_gl_sink_env),
                 "WEBKIT_GST_DISABLE_GL_SINK=%d",
                 launcher_cmdline_flag_enabled("webkit_gst_gl") ? 0 : 1);
        snprintf(webkit_gst_dmabuf_sink_disabled_env,
                 sizeof(webkit_gst_dmabuf_sink_disabled_env),
                 "WEBKIT_GST_DMABUF_SINK_DISABLED=%d",
                 launcher_cmdline_flag_enabled("webkit_gst_dmabuf_sink") ? 0 : 1);
        snprintf(webkit_gst_use_videoconvert_env,
                 sizeof(webkit_gst_use_videoconvert_env),
                 "WEBKIT_GST_USE_VIDEOCONVERT_SCALE=%d",
                 launcher_cmdline_flag_enabled("webkit_gst_gl") ? 0 : 1);
        snprintf(webkit_dmabuf_renderer_disable_gbm_env,
                 sizeof(webkit_dmabuf_renderer_disable_gbm_env),
                 "WEBKIT_DMABUF_RENDERER_DISABLE_GBM=%d",
                 launcher_cmdline_flag_enabled("webkit_gbm") ? 0 : 1);
        snprintf(webkit_force_dmabuf_renderer_env,
                 sizeof(webkit_force_dmabuf_renderer_env),
                 "WEBKIT_FORCE_DMABUF_RENDERER=%d",
                 launcher_cmdline_flag_enabled("webkit_dmabuf") ? 1 : 0);
        snprintf(mesa_perf_log_env, sizeof(mesa_perf_log_env),
                 "XV6_MESA_PERF_LOG=%d",
                 launcher_cmdline_int_value("glsmoke_mesa_perf", 0) != 0);
        if (mesa_color_buffers < 0)
            mesa_color_buffers = 0;
        if (mesa_color_buffers > 4)
            mesa_color_buffers = 4;
        snprintf(mesa_wayland_color_buffers_env,
                 sizeof(mesa_wayland_color_buffers_env),
                 "XV6_MESA_WAYLAND_COLOR_BUFFERS=%d",
                 mesa_color_buffers);
        char **argv = (arg1 || arg2 || arg3) ? argv_def : argv_noarg;
        char *envp_default[] = {
            "HOME=/",
            "PATH=/bin:/usr/bin",
            "XDG_RUNTIME_DIR=/tmp",
            "XDG_CACHE_HOME=/tmp/.cache",
            "XDG_DATA_HOME=/tmp/.local/share",
            "XDG_DATA_DIRS=/share:/usr/share",
            "TMPDIR=/tmp",
            "TEMP=/tmp",
            "TMP=/tmp",
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
            "TMPDIR=/tmp",
            "TEMP=/tmp",
            "TMP=/tmp",
            "WAYLAND_DISPLAY=wayland-0",
            "GDK_BACKEND=wayland",
            "GDK_GL=disable",
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
            "GST_REGISTRY=/tmp/gstreamer-registry.bin",
            "GST_REGISTRY_REUSE_PLUGIN_SCANNER=1",
            "XV6_GUI_SESSION=1",
            webkit_gpu_run_id_env,
            webkit_gpu_validate_run_id_env,
            webkit_wlcomp_d3d12_run_id_env,
            "WEBKIT_EXEC_PATH=/libexec/webkit2gtk-4.1",
            "WEBKIT_INJECTED_BUNDLE_PATH=/lib/webkit2gtk-4.1/injected-bundle",
            "WEBKIT_DISABLE_NETWORK_CACHE=1",
            "WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1",
            webkit_gst_disable_gl_sink_env,
            webkit_gst_dmabuf_sink_disabled_env,
            webkit_gst_use_videoconvert_env,
            "WEBKIT_GST_MAX_AVC1_RESOLUTION=720P",
            "WEBKIT_DISABLE_COMPOSITING_MODE=1",
            "WEBKIT_XV6_DISABLE_COMPOSITING_UPDATE=1",
            "LIBGL_ALWAYS_SOFTWARE=1",
            "EGL_PLATFORM=wayland",
            "LIBGL_DRIVERS_PATH=/lib/dri",
            "MESA_LOADER_DRIVER_OVERRIDE=swrast",
            "ANGLE_DEFAULT_PLATFORM=gl",
            "WEBKIT_XV6_SKIP_RULE_FEATURES=1",
            "WEBKIT_XV6_SKIP_INITIAL_EMPTY_RENDER=1",
            "SOUP_FORCE_HTTP1=1",
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
            "TMPDIR=/tmp",
            "TEMP=/tmp",
            "TMP=/tmp",
            "WAYLAND_DISPLAY=wayland-0",
            "GDK_BACKEND=wayland",
            "GDK_GL=gles",
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
            "GST_REGISTRY=/tmp/gstreamer-registry.bin",
            "GST_REGISTRY_REUSE_PLUGIN_SCANNER=1",
            "XV6_GUI_SESSION=1",
            webkit_gpu_run_id_env,
            webkit_gpu_validate_run_id_env,
            webkit_wlcomp_d3d12_run_id_env,
            "WEBKIT_EXEC_PATH=/libexec/webkit2gtk-4.1",
            "WEBKIT_INJECTED_BUNDLE_PATH=/lib/webkit2gtk-4.1/injected-bundle",
            "WEBKIT_DISABLE_NETWORK_CACHE=1",
            "WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1",
            webkit_gst_disable_gl_sink_env,
            webkit_gst_dmabuf_sink_disabled_env,
            webkit_gst_use_videoconvert_env,
            "WEBKIT_GST_MAX_AVC1_RESOLUTION=720P",
            webkit_dmabuf_renderer_disable_gbm_env,
            webkit_force_dmabuf_renderer_env,
            "WEBKIT_XV6_GPU_CONTRACT=virgl-opengl-submit",
            "WEBKIT_XV6_REQUIRE_GPU_CONTRACT=1",
            "WEBKIT_XV6_FORCE_COMPOSITING_MODE=1",
            "LIBGL_ALWAYS_SOFTWARE=0",
            "LIBGL_DRIVERS_PATH=/lib/dri",
            "GALLIUM_DRIVER=virgl",
            "EGL_PLATFORM=wayland",
            "ANGLE_DEFAULT_PLATFORM=gl",
            "SOUP_FORCE_HTTP1=1",
            NULL
        };
        char *envp_minibrowser_dxg[] = {
            "HOME=/",
            "PATH=/bin:/usr/bin",
            "LD_LIBRARY_PATH=/lib:/usr/lib:/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu",
            "LD_PRELOAD=/lib/libpng16.so.16:/lib/libxv6memshim.so",
            "XDG_RUNTIME_DIR=/tmp",
            "XDG_CACHE_HOME=/tmp/.cache",
            "XDG_DATA_HOME=/tmp/.local/share",
            "XDG_DATA_DIRS=/share:/usr/share",
            "TMPDIR=/tmp",
            "TEMP=/tmp",
            "TMP=/tmp",
            "WAYLAND_DISPLAY=wayland-0",
            "GDK_BACKEND=wayland",
            "GDK_GL=gles",
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
            "GST_REGISTRY=/tmp/gstreamer-registry.bin",
            "GST_REGISTRY_REUSE_PLUGIN_SCANNER=1",
            "XV6_GUI_SESSION=1",
            webkit_gpu_run_id_env,
            webkit_gpu_validate_run_id_env,
            webkit_wlcomp_d3d12_run_id_env,
            "WEBKIT_EXEC_PATH=/libexec/webkit2gtk-4.1",
            "WEBKIT_INJECTED_BUNDLE_PATH=/lib/webkit2gtk-4.1/injected-bundle",
            "WEBKIT_DISABLE_NETWORK_CACHE=1",
            "WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1",
            webkit_gst_dmabuf_sink_disabled_env,
            "WEBKIT_XV6_GPU_CONTRACT=d3d12-shared-surface",
            "WEBKIT_XV6_REQUIRE_GPU_CONTRACT=1",
            "WEBKIT_XV6_FORCE_COMPOSITING_MODE=1",
            "LIBGL_ALWAYS_SOFTWARE=0",
            "LIBGL_DRIVERS_PATH=/lib/dri",
            "MESA_LOADER_DRIVER_OVERRIDE=d3d12",
            "GALLIUM_DRIVER=d3d12",
            "EGL_PLATFORM=wayland",
            "XV6_MESA_WAYLAND_THROTTLE=1",
            "XV6_MESA_WAYLAND_XV6GPU=1",
            "XV6_MESA_PERF_LOG=0",
            "XV6_MESA_WAYLAND_INPLACE_PRESENT=1",
            "XV6_D3D12_ENABLE_NATIVE_PRESENT=1",
            "XV6_D3D12_PRESENT_INTERVAL=1",
            "XV6_D3D12_REQUIRE_NATIVE_PRESENT=1",
            "XV6_D3D12_COPY_EXPORT=0",
            "vblank_mode=0",
            "ANGLE_DEFAULT_PLATFORM=gl",
            "SOUP_FORCE_HTTP1=1",
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
            "TMPDIR=/tmp",
            "TEMP=/tmp",
            "TMP=/tmp",
            "WAYLAND_DISPLAY=wayland-0",
            "GDK_BACKEND=wayland",
            "GDK_GL=gles",
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
            "GST_REGISTRY=/tmp/gstreamer-registry.bin",
            "GST_REGISTRY_REUSE_PLUGIN_SCANNER=1",
            "XV6_GUI_SESSION=1",
            webkit_gpu_run_id_env,
            webkit_gpu_validate_run_id_env,
            webkit_wlcomp_d3d12_run_id_env,
            "WEBKIT_EXEC_PATH=/libexec/webkit2gtk-4.1",
            "WEBKIT_INJECTED_BUNDLE_PATH=/lib/webkit2gtk-4.1/injected-bundle",
            "WEBKIT_DISABLE_NETWORK_CACHE=1",
            "WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1",
            webkit_gst_disable_gl_sink_env,
            webkit_gst_dmabuf_sink_disabled_env,
            webkit_gst_use_videoconvert_env,
            "WEBKIT_GST_MAX_AVC1_RESOLUTION=720P",
            "LIBGL_ALWAYS_SOFTWARE=1",
            "EGL_PLATFORM=wayland",
            "LIBGL_DRIVERS_PATH=/lib/dri",
            "MESA_LOADER_DRIVER_OVERRIDE=swrast",
            "ANGLE_DEFAULT_PLATFORM=gl",
            "SOUP_FORCE_HTTP1=1",
            "WEBKIT_XV6_DISABLE_BCG_SWITCH=1",
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
            "XV6_MESA_WAYLAND_THROTTLE=1",
            mesa_perf_log_env,
            "XV6_MESAWLEGL_SHM_PRESENT=0",
            mesa_wayland_color_buffers_env,
            NULL
        };
        char *envp_mesa_dxg[] = {
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
            "MESA_LOADER_DRIVER_OVERRIDE=d3d12",
            "GALLIUM_DRIVER=d3d12",
            "LIBGL_DRIVERS_PATH=/lib/dri",
            "EGL_PLATFORM=wayland",
            "XV6_MESA_WAYLAND_THROTTLE=0",
            "XV6_MESA_WAYLAND_XV6GPU=1",
            mesa_perf_log_env,
            mesa_wayland_color_buffers_env,
            "XV6_MESA_WAYLAND_INPLACE_PRESENT=1",
            "XV6_D3D12_ENABLE_NATIVE_PRESENT=1",
            "XV6_D3D12_PRESENT_INTERVAL=1",
            "XV6_D3D12_REQUIRE_NATIVE_PRESENT=1",
            "XV6_D3D12_COPY_EXPORT=0",
            "vblank_mode=0",
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
            mesa_wayland_color_buffers_env,
            NULL
        };
        char **envp = envp_default;
        int virgl_available = launcher_virgl_available();
        int dxg_available = launcher_dxg_render_node_available();
        int opengl_submit_available = launcher_opengl_submit_available();
        int d3d12_present_available =
            launcher_d3d12_present_contract_available(
                webkit_gpu_run_id_value);
        int webkit_accel_available =
            opengl_submit_available &&
            (virgl_available || d3d12_present_available);
        if (is_webkit) {
            int accel = launcher_cmdline_flag_enabled("webkit_accel");
            int js = launcher_cmdline_int_value("webkit_js", 1) != 0;
            if (accel && !webkit_accel_available && is_minibrowser) {
                fprintf(stderr,
                        "wlcomp: WebKit acceleration requested, but the "
                        "shared-surface/OpenGL-submit contract is unavailable; "
                        "using the stable WebKit compositor path\n");
                accel = 0;
            }
            if (is_minibrowser) {
                if (accel)
                    argv = js ? argv_minibrowser_accel_js :
                                 argv_minibrowser_accel;
                else
                    argv = js ? argv_minibrowser_js : argv_minibrowser;
            }
            envp = accel ?
                (d3d12_present_available ? envp_minibrowser_dxg :
                 (virgl_available && opengl_submit_available ?
                      envp_minibrowser_accel :
                                   envp_minibrowser_accel_sw)) :
                envp_minibrowser;
        } else if (is_mesa_gl) {
            envp = virgl_available ? envp_mesa_accel :
                (dxg_available ? envp_mesa_dxg : envp_mesa_accel_sw);
        }
        execve(path, argv, envp);
        _exit(127);
    }

    setpgid(pid, pid);
    g_children[slot] = pid;
    g_child_launch_ms[slot] = launcher_get_time_ms();
    snprintf(g_child_name[slot], sizeof(g_child_name[slot]), "%s",
             app_name);
    fprintf(stderr, "wlcomp: launched %s (pid %d)\n", app_name, pid);
}

void wlcomp_launcher_launch(const char *path, const char *name,
                            const char *arg)
{
    wlcomp_launcher_launch_args(path, name, arg, NULL, NULL);
}

void wlcomp_launcher_launch_noarg(const char *path, const char *name)
{
    wlcomp_launcher_launch(path, name, NULL);
}

void wlcomp_launcher_reap(void)
{
    uint32_t now = launcher_get_time_ms();
    int webkit_timeout_ms = -1;

    for (int i = 0; i < MAX_CHILDREN; i++) {
        if (g_children[i] > 0) {
            int status;
            pid_t r = waitpid(g_children[i], &status, WNOHANG);
            if (r > 0) {
                if (WIFEXITED(status)) {
                    if (WEXITSTATUS(status) != 0)
                        fprintf(stderr, "wlcomp: child pid %d exited (status=%d)\n",
                                g_children[i], WEXITSTATUS(status));
                } else if (WIFSIGNALED(status)) {
                    fprintf(stderr, "wlcomp: child pid %d killed by signal %d\n",
                            g_children[i], WTERMSIG(status));
                } else {
                    fprintf(stderr, "wlcomp: child pid %d wait status 0x%x\n",
                            g_children[i], status);
                }
                launcher_destroy_surfaces_for_pid(g_children[i]);
                g_children[i] = 0;
                g_child_launch_ms[i] = 0;
                g_child_name[i][0] = '\0';
            } else if (r == 0 && strcmp(g_child_name[i], "MiniBrowser") == 0) {
                if (webkit_timeout_ms < 0)
                    webkit_timeout_ms =
                        launcher_cmdline_int_value("webkit_timeout_ms", 0);
                if (webkit_timeout_ms > 0 &&
                    now - g_child_launch_ms[i] >=
                        (uint32_t)webkit_timeout_ms) {
                    fprintf(stderr,
                            "wlcomp: MiniBrowser timeout reached, closing pid %d\n",
                            g_children[i]);
                    wlcomp_launcher_terminate_pid(g_children[i]);
                    launcher_destroy_surfaces_for_pid(g_children[i]);
                    wlcomp_launcher_remember_reap(g_children[i]);
                    g_children[i] = 0;
                    g_child_launch_ms[i] = 0;
                    g_child_name[i][0] = '\0';
                }
            }
        }
    }

    for (int i = 0; i < MAX_CHILDREN; i++) {
        if (g_pending_reap[i] > 0) {
            int status;
            pid_t r = waitpid(g_pending_reap[i], &status, WNOHANG);
            if (r > 0 || (r < 0 && errno == ECHILD))
                g_pending_reap[i] = 0;
        }
    }
}

const char *wlcomp_path_basename(const char *path)
{
    const char *base = path;

    if (!path)
        return "";
    for (const char *p = path; *p; p++) {
        if (*p == '/')
            base = p + 1;
    }
    return base;
}

int wlcomp_path_has_suffix(const char *path, const char *suffix)
{
    size_t lp;
    size_t ls;

    if (!path || !suffix)
        return 0;
    lp = strlen(path);
    ls = strlen(suffix);
    return lp >= ls && strcmp(path + lp - ls, suffix) == 0;
}

int wlcomp_path_is_html(const char *path)
{
    return wlcomp_path_has_suffix(path, ".html") ||
           wlcomp_path_has_suffix(path, ".htm");
}

int wlcomp_path_is_text(const char *path)
{
    return wlcomp_path_has_suffix(path, ".txt") ||
           wlcomp_path_has_suffix(path, ".log") ||
           wlcomp_path_has_suffix(path, ".md") ||
           wlcomp_path_has_suffix(path, ".c") ||
           wlcomp_path_has_suffix(path, ".h") ||
           wlcomp_path_has_suffix(path, ".sh");
}
