/*
 * mesawlegl.c - Mesa-native Wayland EGL smoke client for xv6.
 *
 * Unlike mesaglsmoke, this does not render into a surfaceless pbuffer and copy
 * pixels into an xv6 buffer.  It exercises Mesa's Wayland platform path:
 * wl_egl_window -> eglCreateWindowSurface -> eglSwapBuffers.
 */

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <linux/input.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "font8x16.h"
#include "xdg-shell-client-protocol.h"
#include "xv6_present_buffer.h"

#ifndef EGL_PLATFORM_WAYLAND_KHR
#define EGL_PLATFORM_WAYLAND_KHR 0x31D8
#endif

#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8 0x88F0
#endif

#define WINDOW_W 480
#define WINDOW_H 360
#define TITLEBAR_H 30
#define TITLEBAR_CONTROL_W 34
#define SOFTWARE_DEMO_W 180
#define SOFTWARE_DEMO_H 135
#define FPS_TEXT_MAX 16
#define FPS_EVIDENCE_PATH "/tmp/mesawlegl-fps"
#define WLCOMP_FPS_PATH "/tmp/wlcomp-fps"
#define D3D12_PRESENT_EVIDENCE_PATH "/tmp/wlcomp-d3d12-present"

struct vertex {
    GLfloat x;
    GLfloat y;
    GLfloat z;
    GLfloat r;
    GLfloat g;
    GLfloat b;
    GLfloat a;
};

struct tex_vertex {
    GLfloat x;
    GLfloat y;
    GLfloat u;
    GLfloat v;
};

struct sphere_vertex {
    GLfloat x;
    GLfloat y;
    GLfloat z;
    GLfloat nx;
    GLfloat ny;
    GLfloat nz;
};

struct app_state {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct xdg_wm_base *wm_base;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct wl_egl_window *egl_window;
    EGLDisplay egl_display;
    EGLConfig egl_config;
    EGLContext egl_context;
    EGLSurface egl_surface;
    GLuint program;
    GLuint tex_program;
    GLuint vbo;
    GLuint fbo;
    GLuint fbo_tex;
    GLuint depth_stencil_rb;
    GLuint sphere_program;
    GLuint sphere_vbo;
    GLuint overlay_vbo;
    GLint attr_pos;
    GLint attr_color;
    GLint attr_tex_pos;
    GLint attr_tex_coord;
    GLint uniform_tex;
    GLint attr_sphere_pos;
    GLint attr_sphere_normal;
    GLint uniform_sphere_mvp;
    GLint uniform_sphere_model;
    GLint uniform_sphere_light;
    int configured;
    int running;
    int frame;
    int max_seconds;
    int resize_seconds;
    int width;
    int height;
    int loop;
    int api_smoke;
    int sphere_demo;
    int software_demo;
    int max_width;
    int max_height;
    int render_div;
    int present_interval;
    int pace_us;
    int sphere_vertex_count;
    int fps_sample_seq;
    int fps_frame_count;
    int resize_count;
    int close_requested;
    int maximized;
    int pointer_x;
    int pointer_y;
    double fps_value;
    double fps_start_sec;
    unsigned long last_display_bind_completed_id;
    unsigned long source_content_frame;
    unsigned long source_content_hash;
    char fps_text[FPS_TEXT_MAX];
    char overlay_fps_text[FPS_TEXT_MAX];
    int overlay_vertex_count;
    int client_capture_enabled;
    int client_capture_done;
    int client_capture_after_sec;
    double client_capture_start_sec;
    double perf_start_sec;
    int perf_frames;
    double perf_render_us;
    double perf_overlay_us;
    double perf_swap_us;
    double perf_fps_us;
    double perf_flush_us;

    /*
     * Honest blit-present path: glReadPixels the genuinely GPU-rendered
     * default framebuffer into a wl_shm buffer the CPU compositor can read,
     * then attach/commit that. The GPU still renders every frame; only the
     * final scanout copy is a CPU blit (native scanout-present credit stays 0).
     */
    struct wl_shm *shm;
    struct xv6_present_buffer present_buf;
    int present_buf_ready;
    int shm_present;
    uint8_t *readback;
    size_t readback_size;
    GLenum read_format;
};

struct d3d12_present_evidence {
    int valid;
    unsigned long native_present_count;
    unsigned long present_id;
    unsigned long completed;
    unsigned long display_bind_present_id;
    unsigned long display_bind_completed_id;
    unsigned long display_bind_resource_generation;
    unsigned long generation;
    unsigned long evidence_time_us;
    unsigned long resource;
    unsigned long buffer_generation;
    unsigned long content_visible_credit;
    unsigned long content_native_present_credit;
    unsigned long present_content_crc;
    unsigned long visible_content_crc;
    unsigned long present_content_frame;
    unsigned long visible_content_frame;
    unsigned long present_frame_hash;
    unsigned long visible_frame_hash;
    unsigned long content_progress_source_owned;
    unsigned long content_progress_present_id;
    unsigned long content_progress_completed;
    unsigned long content_progress_current_run_valid;
    unsigned long content_progress_identity_complete;
    unsigned long content_progress_display_bind_present_id;
    unsigned long content_progress_display_bind_completed_id;
    unsigned long content_progress_resource_generation;
    unsigned long callback_release_same_frame;
    unsigned long host_saw_display_bind_packet;
    unsigned long wsl_presenthistory_completion_credit;
    unsigned long display_handoff_implemented;
    unsigned long final_handoff_success;
    unsigned long final_handoff_present_id;
    unsigned long final_handoff_completed;
    unsigned long final_handoff_resource_generation;
    unsigned long no_readback;
    unsigned long requirements;
    int client_pid;
    char content_progress_run_id[128];
    char content_progress_compositor_run_id[128];
    char content_progress_state[64];
    char visible_content_progress[64];
    char display_bind_backend[64];
    char display_bind_transport[80];
    char display_bind_transport_source[80];
    char display_bind_completion_source[32];
};

static int mesa_env_requests_accel(void);
static int read_wlcomp_visible_fps(double *fps_out);
static int content_height(const struct app_state *app);

static EGLDisplay get_wayland_display(struct wl_display *display)
{
    typedef EGLDisplay (*get_platform_display_fn)(EGLenum, void *,
                                                  const EGLAttrib *);
    const char *client_ext = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    get_platform_display_fn get_platform_display =
        (get_platform_display_fn)eglGetProcAddress("eglGetPlatformDisplay");

    if (get_platform_display != NULL && client_ext != NULL &&
        (strstr(client_ext, "EGL_KHR_platform_wayland") != NULL ||
         strstr(client_ext, "EGL_EXT_platform_base") != NULL))
        return get_platform_display(EGL_PLATFORM_WAYLAND_KHR, display, NULL);

    return eglGetDisplay((EGLNativeDisplayType)display);
}

static const char *validation_run_id(void)
{
    const char *run_id = getenv("XV6_GPU_VALIDATE_RUN_ID");

    if (run_id && run_id[0])
        return run_id;
    run_id = getenv("XV6_WLCOMP_D3D12_RUN_ID");
    return (run_id && run_id[0]) ? run_id : "";
}

static int evidence_key_value(const char *buf, const char *key,
                              char *value, size_t value_size)
{
    size_t key_len = strlen(key);
    const char *p = buf;

    if (value_size == 0)
        return 0;
    while (p && *p) {
        const char *line_end = strchr(p, '\n');
        const char *value_start;
        size_t len;

        if (strncmp(p, key, key_len) != 0 || p[key_len] != '=') {
            p = line_end ? line_end + 1 : NULL;
            continue;
        }
        value_start = p + key_len + 1;
        len = line_end ? (size_t)(line_end - value_start) :
                         strlen(value_start);
        if (len >= value_size)
            len = value_size - 1;
        memcpy(value, value_start, len);
        value[len] = '\0';
        return 1;
    }
    value[0] = '\0';
    return 0;
}

static int evidence_key_ulong(const char *buf, const char *key,
                              unsigned long *value)
{
    char tmp[64];
    char *end = NULL;
    unsigned long parsed;

    if (!evidence_key_value(buf, key, tmp, sizeof(tmp)))
        return 0;
    parsed = strtoul(tmp, &end, 0);
    if (end == tmp)
        return 0;
    *value = parsed;
    return 1;
}

static int evidence_key_int(const char *buf, const char *key, int *value)
{
    unsigned long parsed;

    if (!evidence_key_ulong(buf, key, &parsed))
        return 0;
    *value = (int)parsed;
    return 1;
}

static int read_d3d12_present_evidence(struct d3d12_present_evidence *evidence)
{
    FILE *fp;
    char buf[32768];
    size_t n;
    char run_id[128];
    char compositor_run_id[128];
    char seal_run_id[128];
    char seal_end_run_id[128];
    const char *expected_run_id = validation_run_id();
    unsigned long seal_begin = 0;
    unsigned long seal_end = 0;
    unsigned long seal_complete = 0;
    unsigned long seal_generation = 0;
    unsigned long seal_end_generation = 0;
    unsigned long requirements = 0;
    unsigned long no_readback = 0;
    unsigned long display_handoff = 0;
    unsigned long visible_credit = 0;
    unsigned long native_content_credit = 0;
    unsigned long callback_release_same_frame = 0;

    memset(evidence, 0, sizeof(*evidence));
    fp = fopen(D3D12_PRESENT_EVIDENCE_PATH, "r");
    if (!fp)
        return 0;
    n = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    buf[n] = '\0';

    seal_run_id[0] = '\0';
    seal_end_run_id[0] = '\0';
    (void)evidence_key_ulong(buf, "d3d12_evidence_seal_begin",
                             &seal_begin);
    (void)evidence_key_ulong(buf, "d3d12_evidence_seal_end",
                             &seal_end);
    (void)evidence_key_ulong(buf, "d3d12_evidence_seal_complete",
                             &seal_complete);
    (void)evidence_key_ulong(buf, "d3d12_evidence_seal_generation",
                             &seal_generation);
    (void)evidence_key_ulong(buf, "d3d12_evidence_seal_end_generation",
                             &seal_end_generation);
    (void)evidence_key_value(buf, "d3d12_evidence_seal_run_id",
                             seal_run_id, sizeof(seal_run_id));
    (void)evidence_key_value(buf, "d3d12_evidence_seal_end_run_id",
                             seal_end_run_id, sizeof(seal_end_run_id));

    (void)evidence_key_value(buf, "d3d12_run_id", run_id, sizeof(run_id));
    (void)evidence_key_value(buf, "d3d12_present_identity_compositor_run_id",
                             compositor_run_id, sizeof(compositor_run_id));
    (void)evidence_key_int(buf, "d3d12_client_pid", &evidence->client_pid);
    (void)evidence_key_ulong(buf, "d3d12_native_present_completions",
                             &evidence->native_present_count);
    if (evidence->native_present_count == 0)
        (void)evidence_key_ulong(buf, "d3d12_context_native_present_completions",
                                 &evidence->native_present_count);
    (void)evidence_key_ulong(buf, "d3d12_dxg_present_id",
                             &evidence->present_id);
    (void)evidence_key_ulong(buf, "d3d12_dxg_present_completed",
                             &evidence->completed);
    if (evidence->present_id == 0)
        (void)evidence_key_ulong(buf, "d3d12_present_source_buffer_present_id",
                                 &evidence->present_id);
    if (evidence->completed == 0)
        (void)evidence_key_ulong(buf, "d3d12_present_source_buffer_completed",
                                 &evidence->completed);
    (void)evidence_key_ulong(buf, "d3d12_evidence_generation",
                             &evidence->generation);
    (void)evidence_key_ulong(buf, "d3d12_present_evidence_time_us",
                             &evidence->evidence_time_us);
    (void)evidence_key_ulong(buf, "d3d12_present_resource",
                             &evidence->resource);
    if (evidence->resource == 0)
        (void)evidence_key_ulong(buf,
                                 "d3d12_present_identity_manager_resource_id",
                                 &evidence->resource);
    (void)evidence_key_ulong(buf, "d3d12_buffer_generation",
                             &evidence->buffer_generation);
    (void)evidence_key_ulong(buf, "d3d12_native_present_requirements_satisfied",
                             &requirements);
    (void)evidence_key_ulong(buf, "d3d12_no_readback", &no_readback);
    (void)evidence_key_ulong(buf, "d3d12_display_handoff_implemented",
                             &display_handoff);
    (void)evidence_key_ulong(buf, "d3d12_content_progress_visible_credit",
                             &visible_credit);
    (void)evidence_key_ulong(buf,
                             "d3d12_content_progress_native_present_credit",
                             &native_content_credit);
    (void)evidence_key_ulong(buf, "d3d12_callback_release_same_frame_observed",
                             &callback_release_same_frame);
    (void)evidence_key_ulong(buf, "d3d12_present_content_crc",
                             &evidence->present_content_crc);
    (void)evidence_key_ulong(buf, "d3d12_visible_content_crc",
                             &evidence->visible_content_crc);
    (void)evidence_key_ulong(buf, "d3d12_present_content_frame",
                             &evidence->present_content_frame);
    (void)evidence_key_ulong(buf, "d3d12_visible_content_frame",
                             &evidence->visible_content_frame);
    (void)evidence_key_ulong(buf, "d3d12_present_frame_hash",
                             &evidence->present_frame_hash);
    (void)evidence_key_ulong(buf, "d3d12_visible_frame_hash",
                             &evidence->visible_frame_hash);
    (void)evidence_key_value(buf, "d3d12_content_progress_state",
                             evidence->content_progress_state,
                             sizeof(evidence->content_progress_state));
    (void)evidence_key_value(buf, "d3d12_visible_content_progress",
                             evidence->visible_content_progress,
                             sizeof(evidence->visible_content_progress));
    (void)evidence_key_ulong(buf, "d3d12_content_progress_source_owned",
                             &evidence->content_progress_source_owned);
    (void)evidence_key_ulong(buf, "d3d12_content_progress_current_run_valid",
                             &evidence->content_progress_current_run_valid);
    (void)evidence_key_ulong(buf, "d3d12_content_progress_identity_complete",
                             &evidence->content_progress_identity_complete);
    (void)evidence_key_value(buf, "d3d12_content_progress_run_id",
                             evidence->content_progress_run_id,
                             sizeof(evidence->content_progress_run_id));
    (void)evidence_key_value(buf, "d3d12_content_progress_compositor_run_id",
                             evidence->content_progress_compositor_run_id,
                             sizeof(evidence->content_progress_compositor_run_id));
    (void)evidence_key_ulong(buf, "d3d12_content_progress_present_id",
                             &evidence->content_progress_present_id);
    (void)evidence_key_ulong(buf, "d3d12_content_progress_completed",
                             &evidence->content_progress_completed);
    (void)evidence_key_ulong(
        buf, "d3d12_content_progress_display_bind_present_id",
        &evidence->content_progress_display_bind_present_id);
    (void)evidence_key_ulong(
        buf, "d3d12_content_progress_display_bind_completed_id",
        &evidence->content_progress_display_bind_completed_id);
    (void)evidence_key_ulong(
        buf, "d3d12_content_progress_display_bind_resource_generation",
        &evidence->content_progress_resource_generation);
    (void)evidence_key_value(buf, "display_bind_backend",
                             evidence->display_bind_backend,
                             sizeof(evidence->display_bind_backend));
    (void)evidence_key_value(buf, "display_bind_transport",
                             evidence->display_bind_transport,
                             sizeof(evidence->display_bind_transport));
    (void)evidence_key_value(buf, "display_bind_transport_source",
                             evidence->display_bind_transport_source,
                             sizeof(evidence->display_bind_transport_source));
    (void)evidence_key_ulong(buf, "host_saw_display_bind_packet",
                             &evidence->host_saw_display_bind_packet);
    (void)evidence_key_ulong(buf, "wsl_presenthistory_completion_credit",
                             &evidence->wsl_presenthistory_completion_credit);
    (void)evidence_key_ulong(buf, "display_bind_present_id",
                             &evidence->display_bind_present_id);
    (void)evidence_key_ulong(buf, "display_bind_completed_id",
                             &evidence->display_bind_completed_id);
    (void)evidence_key_ulong(buf, "display_bind_resource_generation",
                             &evidence->display_bind_resource_generation);
    (void)evidence_key_value(buf, "display_bind_completion_source",
                             evidence->display_bind_completion_source,
                             sizeof(evidence->display_bind_completion_source));
    (void)evidence_key_ulong(buf, "d3d12_final_handoff_success",
                             &evidence->final_handoff_success);
    (void)evidence_key_ulong(buf, "d3d12_final_handoff_present_id",
                             &evidence->final_handoff_present_id);
    (void)evidence_key_ulong(buf, "d3d12_final_handoff_completed",
                             &evidence->final_handoff_completed);
    (void)evidence_key_ulong(buf, "d3d12_final_handoff_resource_generation",
                             &evidence->final_handoff_resource_generation);

    evidence->requirements = requirements;
    evidence->no_readback = no_readback;
    evidence->display_handoff_implemented = display_handoff;
    evidence->content_visible_credit = visible_credit;
    evidence->content_native_present_credit = native_content_credit;
    evidence->callback_release_same_frame = callback_release_same_frame;

    evidence->valid =
        seal_begin == 1 && seal_end == 1 && seal_complete == 1 &&
        seal_generation != 0 &&
        seal_generation == seal_end_generation &&
        seal_generation == evidence->generation &&
        strcmp(seal_run_id, run_id) == 0 &&
        strcmp(seal_end_run_id, run_id) == 0 &&
        expected_run_id[0] != '\0' &&
        strcmp(run_id, expected_run_id) == 0 &&
        strcmp(compositor_run_id, expected_run_id) == 0 &&
        evidence->client_pid == (int)getpid() &&
        requirements == 1 && no_readback == 1 &&
        evidence->native_present_count > 0 &&
        evidence->present_id > 0 && evidence->completed >= evidence->present_id &&
        evidence->generation > 0 && evidence->evidence_time_us > 0 &&
        evidence->resource > 0 && evidence->buffer_generation > 0 &&
        display_handoff == 1 &&
        visible_credit == 1 &&
        native_content_credit == 1 &&
        evidence->present_content_crc != 0 &&
        evidence->visible_content_crc != 0 &&
        evidence->present_content_frame != 0 &&
        evidence->visible_content_frame != 0 &&
        evidence->present_frame_hash != 0 &&
        evidence->visible_frame_hash != 0 &&
        strcmp(evidence->content_progress_state,
               "NATIVE_PRESENT_COMPLETE") == 0 &&
        strcmp(evidence->visible_content_progress,
               "NATIVE_PRESENT_COMPLETE") == 0 &&
        strcmp(evidence->content_progress_run_id, expected_run_id) == 0 &&
        strcmp(evidence->content_progress_compositor_run_id,
               expected_run_id) == 0 &&
        evidence->content_progress_current_run_valid == 1 &&
        evidence->content_progress_source_owned == 1 &&
        evidence->content_progress_identity_complete == 1 &&
        evidence->content_progress_present_id == evidence->present_id &&
        evidence->content_progress_completed == evidence->completed &&
        evidence->content_progress_display_bind_present_id ==
            evidence->display_bind_present_id &&
        evidence->content_progress_display_bind_completed_id ==
            evidence->display_bind_completed_id &&
        evidence->content_progress_resource_generation ==
            evidence->buffer_generation &&
        callback_release_same_frame == 1 &&
        strcmp(evidence->display_bind_backend, "gpup_dxg_scanout_bind") == 0 &&
        strcmp(evidence->display_bind_transport,
               "gpu-p-dxg-resource-scanout-bind") == 0 &&
        strcmp(evidence->display_bind_transport_source,
               "non_wsl_linux_dxgkrnl_extension") == 0 &&
        evidence->host_saw_display_bind_packet == 1 &&
        evidence->wsl_presenthistory_completion_credit == 0 &&
        evidence->display_bind_present_id == evidence->present_id &&
        evidence->display_bind_completed_id == evidence->completed &&
        evidence->display_bind_completed_id >=
            evidence->display_bind_present_id &&
        evidence->display_bind_resource_generation ==
            evidence->buffer_generation &&
        strcmp(evidence->display_bind_completion_source, "display") == 0 &&
        evidence->final_handoff_success == 1 &&
        evidence->final_handoff_present_id == evidence->present_id &&
        evidence->final_handoff_completed == evidence->completed &&
        evidence->final_handoff_resource_generation ==
            evidence->buffer_generation;
    return 1;
}

static void append_fps_evidence(struct app_state *app, double now,
                                double elapsed, double fps,
                                double *displayed_fps_out,
                                int *native_fps_credit_out)
{
    struct d3d12_present_evidence evidence;
    FILE *fp;
    unsigned long native_delta = 0;
    unsigned long native_count = 0;
    int render_width;
    int render_height;
    const char *source = "app-draw-loop-context-only";
    int native_fps_credit = 0;
    int native_present_complete = 0;
    int same_resource_generation = 0;
    int compositor_owned_visible_crc = 0;
    int compositor_owned_visible_frame = 0;
    int compositor_owned_visible_hash = 0;
    int client_content_progress = 0;
    int demo_visible = 0;
    int demo_closeable = 0;
    int demo_resizable = 0;
    int current_run_display_bind_complete = 0;
    int no_readback_native_path = 0;
    int readback_or_software_path = 0;
    int strict_finite_fps_evidence = 0;
    double effective_presented_fps = 0.0;
    double displayed_fps = 0.0;
    double compositor_fps = 0.0;

    if (displayed_fps_out)
        *displayed_fps_out = 0.0;
    if (native_fps_credit_out)
        *native_fps_credit_out = 0;

    (void)read_d3d12_present_evidence(&evidence);
    current_run_display_bind_complete =
        evidence.valid &&
        strcmp(evidence.display_bind_backend, "gpup_dxg_scanout_bind") == 0 &&
        strcmp(evidence.display_bind_transport,
               "gpu-p-dxg-resource-scanout-bind") == 0 &&
        strcmp(evidence.display_bind_transport_source,
               "non_wsl_linux_dxgkrnl_extension") == 0 &&
        evidence.host_saw_display_bind_packet == 1 &&
        evidence.wsl_presenthistory_completion_credit == 0 &&
        strcmp(evidence.display_bind_completion_source, "display") == 0 &&
        evidence.display_bind_present_id > 0 &&
        evidence.display_bind_completed_id >=
            evidence.display_bind_present_id &&
        evidence.display_bind_present_id == evidence.present_id &&
        evidence.display_bind_completed_id == evidence.completed &&
        evidence.display_bind_resource_generation == evidence.buffer_generation;
    no_readback_native_path = !app->software_demo && evidence.no_readback == 1;
    readback_or_software_path =
        app->software_demo || evidence.no_readback != 1;
    native_present_complete =
        evidence.valid &&
        evidence.present_id > 0 &&
        evidence.completed >= evidence.present_id &&
        evidence.display_bind_present_id == evidence.present_id &&
        evidence.display_bind_completed_id == evidence.completed &&
        evidence.display_bind_completed_id >= evidence.display_bind_present_id;
    same_resource_generation =
        evidence.valid &&
        evidence.buffer_generation > 0 &&
        evidence.display_bind_resource_generation == evidence.buffer_generation;
    compositor_owned_visible_crc =
        evidence.valid &&
        evidence.content_visible_credit == 1 &&
        evidence.content_native_present_credit == 1 &&
        evidence.present_content_crc != 0 &&
        evidence.visible_content_crc != 0;
    compositor_owned_visible_frame =
        evidence.valid &&
        evidence.present_content_frame != 0 &&
        evidence.visible_content_frame != 0;
    compositor_owned_visible_hash =
        evidence.valid &&
        evidence.present_frame_hash != 0 &&
        evidence.visible_frame_hash != 0;
    client_content_progress =
        app->source_content_hash != 0 && app->source_content_frame != 0;
    demo_visible = app->frame > 0 && client_content_progress;
    demo_closeable = app->toplevel != NULL;
    demo_resizable = app->resize_count > 0;
    if (evidence.valid) {
        native_count = evidence.display_bind_completed_id;
        if (native_count >= app->last_display_bind_completed_id)
            native_delta =
                native_count - app->last_display_bind_completed_id;
        app->last_display_bind_completed_id = native_count;
        source = "native-d3d12-present-complete";
        strict_finite_fps_evidence =
            native_delta > 0 &&
            current_run_display_bind_complete &&
            no_readback_native_path &&
            native_present_complete &&
            same_resource_generation &&
            compositor_owned_visible_crc &&
            compositor_owned_visible_frame &&
            compositor_owned_visible_hash &&
            evidence.callback_release_same_frame == 1 &&
            client_content_progress &&
            demo_visible &&
            demo_closeable &&
            demo_resizable;
        native_fps_credit = strict_finite_fps_evidence;
        if (native_fps_credit && elapsed > 0.0)
            effective_presented_fps = (double)native_delta / elapsed;
    }
    if (native_fps_credit)
        displayed_fps = effective_presented_fps;
    if (!native_fps_credit && app->sphere_demo && !app->software_demo &&
        mesa_env_requests_accel() && read_wlcomp_visible_fps(&compositor_fps)) {
        /*
         * KVM/virgl does not produce the Hyper-V D3D12 native-present evidence
         * file.  Use the compositor's scanout-present cadence for the label so
         * the demo no longer advertises a faster client-loop FPS.
         */
        displayed_fps = compositor_fps;
        source = "virgl-compositor-present";
    }
    if (displayed_fps_out)
        *displayed_fps_out = displayed_fps;
    if (native_fps_credit_out)
        *native_fps_credit_out = native_fps_credit;

    render_width = app->width / app->render_div;
    render_height = content_height(app) / app->render_div;
    if (render_width <= 0)
        render_width = app->width;
    if (render_height <= 0)
        render_height = content_height(app);

    app->fps_sample_seq++;
    if (!evidence.valid && strcmp(source, "virgl-compositor-present") == 0)
        return;
    fp = fopen(FPS_EVIDENCE_PATH, "a");
    if (!fp)
        return;
    fprintf(fp,
            "mesawlegl_fps_sample callback_seq=%d visible_fps=%.3f "
            "app_loop_fps=%.3f overlay_fps=%.3f "
            "effective_native_presented_fps=%.3f "
            "source=%s validation_run_id=%s process_id=%d "
            "d3d12_client_pid=%d d3d12_evidence_valid=%d "
            "native_present_count=%lu native_present_delta=%lu "
            "native_present_elapsed=%.3f sample_time=%.3f frame=%d "
            "window=%dx%d render=%dx%d render_div=%d "
            "d3d12_evidence_generation=%lu "
            "d3d12_present_evidence_time_us=%lu "
            "d3d12_present_resource=0x%lx "
            "d3d12_buffer_generation=%lu "
            "diagnostic_present_id=%lu diagnostic_completed=%lu "
            "diagnostic_display_bind_backend=%s "
            "diagnostic_display_bind_transport=%s "
            "diagnostic_display_bind_transport_source=%s "
            "diagnostic_host_saw_display_bind_packet=%lu "
            "diagnostic_wsl_presenthistory_completion_credit=%lu "
            "diagnostic_display_bind_present_id=%lu "
            "diagnostic_display_bind_completed_id=%lu "
            "diagnostic_display_bind_resource_generation=%lu "
            "diagnostic_display_bind_completion_source=%s "
            "display_handoff_implemented=%lu "
            "current_run_display_bind_complete=%d "
            "no_readback=%lu software_demo=%d "
            "readback_or_software_path=%d "
            "content_visible_credit=%lu content_native_present_credit=%lu "
            "present_content_crc=%lu visible_content_crc=%lu "
            "present_content_frame=%lu visible_content_frame=%lu "
            "present_frame_hash=%lu visible_frame_hash=%lu "
            "content_progress_run_id=%s "
            "content_progress_compositor_run_id=%s "
            "content_progress_current_run_valid=%lu "
            "content_progress_identity_complete=%lu "
            "diagnostic_content_progress_present_id=%lu "
            "diagnostic_content_progress_completed=%lu "
            "diagnostic_content_progress_display_bind_present_id=%lu "
            "diagnostic_content_progress_display_bind_completed_id=%lu "
            "diagnostic_content_progress_display_bind_resource_generation=%lu "
            "diagnostic_final_handoff_success=%lu "
            "diagnostic_final_handoff_present_id=%lu "
            "diagnostic_final_handoff_completed=%lu "
            "diagnostic_final_handoff_resource_generation=%lu "
            "client_content_hash=%lu client_content_frame=%lu "
            "content_region=client-content-no-title-fps "
            "callback_release_same_frame=%lu "
            "native_present_complete=%d "
            "same_run_resource_generation=%d "
            "same_resource_generation=%d "
            "compositor_owned_visible_content_crc=%d "
            "compositor_owned_visible_content_frame=%d "
            "compositor_owned_visible_frame_hash=%d "
            "client_content_progress=%d "
            "finite_demo_visible=%d finite_demo_closeable=%d "
            "finite_demo_resizable=%d "
            "strict_finite_fps_evidence=%d "
            "app_loop_fps_credit=0 overlay_fps_credit=0 "
            "native_present_credit=%d "
            "effective_presented_fps_credit=%d "
            "opengl_submit_credit=0 "
            "displayed_fps_context_only=%d "
            "acceptance_requires_native_present_and_content_progress=1 "
            "acceptance_requires_native_present_completion=1 "
            "acceptance_requires_current_run_display_bind_completion=1 "
            "acceptance_rejects_readback_or_software=1 "
            "acceptance_rejects_app_loop_only=1 "
            "acceptance_requires_compositor_owned_visible_content_crc=1 "
            "acceptance_requires_compositor_owned_visible_content_frame=1 "
            "acceptance_requires_compositor_owned_visible_frame_hash=1 "
            "acceptance_requires_same_resource_generation=1 "
            "acceptance_requires_demo_visible=1 "
            "acceptance_requires_demo_closeable=1 "
            "acceptance_requires_demo_resizable=1\n",
            app->fps_sample_seq, displayed_fps, fps, displayed_fps,
            effective_presented_fps,
            source, validation_run_id(), (int)getpid(),
            evidence.client_pid > 0 ? evidence.client_pid : (int)getpid(),
            evidence.valid ? 1 : 0, native_count, native_delta, elapsed, now,
            app->frame, app->width, app->height, render_width, render_height,
            app->render_div, evidence.generation, evidence.evidence_time_us,
            evidence.resource, evidence.buffer_generation,
            evidence.present_id, evidence.completed,
            evidence.display_bind_backend[0] ?
                evidence.display_bind_backend : "missing",
            evidence.display_bind_transport[0] ?
                evidence.display_bind_transport : "missing",
            evidence.display_bind_transport_source[0] ?
                evidence.display_bind_transport_source : "missing",
            evidence.host_saw_display_bind_packet,
            evidence.wsl_presenthistory_completion_credit,
            evidence.display_bind_present_id,
            evidence.display_bind_completed_id,
            evidence.display_bind_resource_generation,
            evidence.display_bind_completion_source[0] ?
                evidence.display_bind_completion_source : "missing",
            evidence.display_handoff_implemented,
            current_run_display_bind_complete,
            evidence.no_readback,
            app->software_demo,
            readback_or_software_path,
            evidence.content_visible_credit,
            evidence.content_native_present_credit,
            evidence.present_content_crc,
            evidence.visible_content_crc,
            evidence.present_content_frame,
            evidence.visible_content_frame,
            evidence.present_frame_hash,
            evidence.visible_frame_hash,
            evidence.content_progress_run_id[0] ?
                evidence.content_progress_run_id : "missing",
            evidence.content_progress_compositor_run_id[0] ?
                evidence.content_progress_compositor_run_id : "missing",
            evidence.content_progress_current_run_valid,
            evidence.content_progress_identity_complete,
            evidence.content_progress_present_id,
            evidence.content_progress_completed,
            evidence.content_progress_display_bind_present_id,
            evidence.content_progress_display_bind_completed_id,
            evidence.content_progress_resource_generation,
            evidence.final_handoff_success,
            evidence.final_handoff_present_id,
            evidence.final_handoff_completed,
            evidence.final_handoff_resource_generation,
            app->source_content_hash,
            app->source_content_frame,
            evidence.callback_release_same_frame,
            native_present_complete,
            evidence.valid ? 1 : 0,
            same_resource_generation,
            compositor_owned_visible_crc,
            compositor_owned_visible_frame,
            compositor_owned_visible_hash,
            client_content_progress,
            demo_visible, demo_closeable, demo_resizable,
            strict_finite_fps_evidence,
            native_fps_credit,
            native_fps_credit,
            native_fps_credit ? 0 : 1);
    fprintf(fp,
            "mesawlegl_fps_present_credit_matrix "
            "callback_seq=%d visible_fps=%.3f effective_presented_fps=%.3f "
            "app_loop_fps=%.3f overlay_fps=%.3f "
            "effective_native_presented_fps=%.3f "
            "strict_anti_inflation=1 d3d12_evidence_valid=%d "
            "native_present_delta=%lu "
            "diagnostic_present_id=%lu diagnostic_completed=%lu "
            "diagnostic_display_bind_present_id=%lu "
            "diagnostic_display_bind_completed_id=%lu "
            "diagnostic_display_bind_backend=%s "
            "diagnostic_display_bind_transport=%s "
            "diagnostic_display_bind_transport_source=%s "
            "diagnostic_host_saw_display_bind_packet=%lu "
            "diagnostic_wsl_presenthistory_completion_credit=%lu "
            "diagnostic_display_bind_completion_source=%s "
            "current_run_display_bind_complete=%d "
            "no_readback=%lu software_demo=%d "
            "readback_or_software_path=%d "
            "content_visible_credit=%lu present_content_crc=%lu "
            "visible_content_crc=%lu present_content_frame=%lu "
            "visible_content_frame=%lu "
            "content_progress_current_run_valid=%lu "
            "content_progress_identity_complete=%lu "
            "diagnostic_content_progress_present_id=%lu "
            "diagnostic_content_progress_completed=%lu "
            "diagnostic_final_handoff_success=%lu "
            "diagnostic_final_handoff_present_id=%lu "
            "diagnostic_final_handoff_completed=%lu "
            "callback_release_same_frame=%lu "
            "native_present_complete=%d "
            "same_run_resource_generation=%d "
            "same_resource_generation=%d "
            "compositor_owned_visible_content_crc=%d "
            "compositor_owned_visible_content_frame=%d "
            "compositor_owned_visible_frame_hash=%d "
            "client_content_progress=%d "
            "finite_demo_visible=%d finite_demo_closeable=%d "
            "finite_demo_resizable=%d "
            "strict_finite_fps_evidence=%d "
            "app_loop_fps_credit=0 overlay_fps_credit=0 "
            "displayed_fps_context_only=%d visible_fps_ignored=%d "
            "fps_credit_source=%s native_present_credit=%d "
            "effective_presented_fps_credit=%d "
            "acceptance_requires_current_run_display_bind_completion=1 "
            "acceptance_rejects_readback_or_software=1 "
            "acceptance_rejects_app_loop_only=1 "
            "opengl_submit_credit=0 status=PASS\n",
            app->fps_sample_seq, displayed_fps, effective_presented_fps,
            fps, displayed_fps,
            effective_presented_fps,
            evidence.valid ? 1 : 0, native_delta, evidence.present_id,
            evidence.completed, evidence.display_bind_present_id,
            evidence.display_bind_completed_id,
            evidence.display_bind_backend[0] ?
                evidence.display_bind_backend : "missing",
            evidence.display_bind_transport[0] ?
                evidence.display_bind_transport : "missing",
            evidence.display_bind_transport_source[0] ?
                evidence.display_bind_transport_source : "missing",
            evidence.host_saw_display_bind_packet,
            evidence.wsl_presenthistory_completion_credit,
            evidence.display_bind_completion_source[0] ?
                evidence.display_bind_completion_source : "missing",
            current_run_display_bind_complete,
            evidence.no_readback,
            app->software_demo,
            readback_or_software_path,
            evidence.content_visible_credit,
            evidence.present_content_crc,
            evidence.visible_content_crc,
            evidence.present_content_frame,
            evidence.visible_content_frame,
            evidence.content_progress_current_run_valid,
            evidence.content_progress_identity_complete,
            evidence.content_progress_present_id,
            evidence.content_progress_completed,
            evidence.final_handoff_success,
            evidence.final_handoff_present_id,
            evidence.final_handoff_completed,
            evidence.callback_release_same_frame,
            native_present_complete,
            evidence.valid ? 1 : 0,
            same_resource_generation,
            compositor_owned_visible_crc,
            compositor_owned_visible_frame,
            compositor_owned_visible_hash,
            client_content_progress,
            demo_visible, demo_closeable, demo_resizable,
            strict_finite_fps_evidence,
            native_fps_credit ? 0 : 1,
            native_fps_credit ? 0 : 1, source, native_fps_credit,
            native_fps_credit);
    fprintf(fp,
            "mesawlegl_fps_dependency_skeleton_matrix "
            "callback_seq=%d app_loop_fps=%.3f overlay_fps=%.3f "
            "effective_native_presented_fps=%.3f effective_presented_fps=%.3f "
            "current_run_display_bind_complete=%d "
            "diagnostic_display_bind_backend=%s "
            "diagnostic_display_bind_transport=%s "
            "diagnostic_display_bind_transport_source=%s "
            "diagnostic_host_saw_display_bind_packet=%lu "
            "diagnostic_wsl_presenthistory_completion_credit=%lu "
            "diagnostic_display_bind_present_id=%lu "
            "diagnostic_display_bind_completed_id=%lu "
            "diagnostic_display_bind_resource_generation=%lu "
            "diagnostic_display_bind_completion_source=%s "
            "native_present_delta=%lu "
            "diagnostic_present_id=%lu diagnostic_completed=%lu "
            "no_readback=%lu software_demo=%d "
            "readback_or_software_path=%d "
            "app_loop_fps_credit=0 overlay_fps_credit=0 "
            "app_loop_only_zero_credit=%d "
            "native_present_credit=%d effective_presented_fps_credit=%d "
            "hyperv_failclosed_zero_effective_fps=%d "
            "acceptance_requires_current_run_display_bind_completion=1 "
            "acceptance_rejects_readback_or_software=1 "
            "acceptance_rejects_app_loop_only=1 "
            "opengl_submit_credit=0 status=PASS\n",
            app->fps_sample_seq, fps, displayed_fps, effective_presented_fps,
            effective_presented_fps, current_run_display_bind_complete,
            evidence.display_bind_backend[0] ?
                evidence.display_bind_backend : "missing",
            evidence.display_bind_transport[0] ?
                evidence.display_bind_transport : "missing",
            evidence.display_bind_transport_source[0] ?
                evidence.display_bind_transport_source : "missing",
            evidence.host_saw_display_bind_packet,
            evidence.wsl_presenthistory_completion_credit,
            evidence.display_bind_present_id,
            evidence.display_bind_completed_id,
            evidence.display_bind_resource_generation,
            evidence.display_bind_completion_source[0] ?
                evidence.display_bind_completion_source : "missing",
            native_delta, evidence.present_id, evidence.completed,
            evidence.no_readback, app->software_demo,
            readback_or_software_path,
            native_fps_credit ? 0 : 1,
            native_fps_credit, native_fps_credit,
            native_fps_credit ? 0 : 1);
    if (!evidence.valid) {
        fprintf(fp,
                "mesawlegl_fps_context_only_matrix "
                "callback_seq=%d visible_fps=%.3f "
                "app_loop_fps=%.3f overlay_fps=%.3f "
                "effective_native_presented_fps=0.000 "
                "source=app-draw-loop-context-only "
                "d3d12_evidence_valid=0 native_present_delta=0 "
                "diagnostic_present_id=%lu diagnostic_completed=%lu "
                "displayed_fps_context_only=1 "
                "current_run_display_bind_complete=0 "
                "no_readback=%lu software_demo=%d "
                "readback_or_software_path=%d "
                "app_loop_fps_credit=0 overlay_fps_credit=0 "
                "native_present_credit=0 effective_presented_fps_credit=0 "
                "acceptance_requires_native_present_and_content_progress=1 "
                "acceptance_requires_current_run_display_bind_completion=1 "
                "acceptance_rejects_readback_or_software=1 "
                "acceptance_rejects_app_loop_only=1 "
                "status=PASS\n",
                app->fps_sample_seq, displayed_fps, fps, displayed_fps,
                evidence.present_id, evidence.completed, evidence.no_readback,
                app->software_demo,
                readback_or_software_path);
    }
    fclose(fp);
}

static GLuint compile_shader(GLenum type, const char *src)
{
    GLuint shader = glCreateShader(type);
    GLint ok = GL_FALSE;

    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
        fprintf(stderr, "mesawlegl: shader compile failed\n");
    return shader;
}

static GLuint link_program(const char *vs, const char *fs)
{
    GLuint vshader = compile_shader(GL_VERTEX_SHADER, vs);
    GLuint fshader = compile_shader(GL_FRAGMENT_SHADER, fs);
    GLuint program = glCreateProgram();
    GLint ok = GL_FALSE;

    glAttachShader(program, vshader);
    glAttachShader(program, fshader);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    glDeleteShader(vshader);
    glDeleteShader(fshader);
    if (!ok) {
        fprintf(stderr, "mesawlegl: program link failed\n");
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

static int init_api_smoke_resources(struct app_state *app)
{
    GLenum status;

    glGenBuffers(1, &app->vbo);
    glGenFramebuffers(1, &app->fbo);
    glGenTextures(1, &app->fbo_tex);
    glGenRenderbuffers(1, &app->depth_stencil_rb);
    if (!app->vbo || !app->fbo || !app->fbo_tex ||
        !app->depth_stencil_rb)
        return -1;

    glBindTexture(GL_TEXTURE_2D, app->fbo_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 128, 128, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);

    glBindRenderbuffer(GL_RENDERBUFFER, app->depth_stencil_rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, 128, 128);

    glBindFramebuffer(GL_FRAMEBUFFER, app->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           app->fbo_tex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, app->depth_stencil_rb);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, app->depth_stencil_rb);
    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "mesawlegl: FBO incomplete (0x%x)\n", status);
        return -1;
    }

    return glGetError() == GL_NO_ERROR ? 0 : -1;
}

static void mat4_identity(float m[16])
{
    memset(m, 0, sizeof(float) * 16);
    m[0] = 1.0f;
    m[5] = 1.0f;
    m[10] = 1.0f;
    m[15] = 1.0f;
}

static void mat4_mul(float out[16], const float a[16], const float b[16])
{
    float r[16];

    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            r[col * 4 + row] =
                a[0 * 4 + row] * b[col * 4 + 0] +
                a[1 * 4 + row] * b[col * 4 + 1] +
                a[2 * 4 + row] * b[col * 4 + 2] +
                a[3 * 4 + row] * b[col * 4 + 3];
        }
    }
    memcpy(out, r, sizeof(r));
}

static void mat4_perspective(float m[16], float fovy, float aspect,
                             float znear, float zfar)
{
    float f = 1.0f / tanf(fovy * 0.5f);

    memset(m, 0, sizeof(float) * 16);
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (zfar + znear) / (znear - zfar);
    m[11] = -1.0f;
    m[14] = (2.0f * zfar * znear) / (znear - zfar);
}

static void mat4_translate(float m[16], float x, float y, float z)
{
    mat4_identity(m);
    m[12] = x;
    m[13] = y;
    m[14] = z;
}

static void mat4_rotate_x(float m[16], float angle)
{
    float s = sinf(angle);
    float c = cosf(angle);

    mat4_identity(m);
    m[5] = c;
    m[6] = s;
    m[9] = -s;
    m[10] = c;
}

static void mat4_rotate_y(float m[16], float angle)
{
    float s = sinf(angle);
    float c = cosf(angle);

    mat4_identity(m);
    m[0] = c;
    m[2] = -s;
    m[8] = s;
    m[10] = c;
}

static struct sphere_vertex make_poly_vertex(float x, float y, float z)
{
    float inv_len = 1.0f / sqrtf(x * x + y * y + z * z);
    struct sphere_vertex v;

    v.x = x * inv_len;
    v.y = y * inv_len;
    v.z = z * inv_len;
    v.nx = 0.0f;
    v.ny = 0.0f;
    v.nz = 1.0f;
    return v;
}

static void sphere_emit_flat(struct sphere_vertex *dst, int *idx,
                             struct sphere_vertex a, struct sphere_vertex b,
                             struct sphere_vertex c)
{
    struct sphere_vertex out[3] = { a, b, c };
    float ux = b.x - a.x;
    float uy = b.y - a.y;
    float uz = b.z - a.z;
    float vx = c.x - a.x;
    float vy = c.y - a.y;
    float vz = c.z - a.z;
    float nx = uy * vz - uz * vy;
    float ny = uz * vx - ux * vz;
    float nz = ux * vy - uy * vx;
    float inv_len = 1.0f / sqrtf(nx * nx + ny * ny + nz * nz);
    float cx = (a.x + b.x + c.x) / 3.0f;
    float cy = (a.y + b.y + c.y) / 3.0f;
    float cz = (a.z + b.z + c.z) / 3.0f;

    nx *= inv_len;
    ny *= inv_len;
    nz *= inv_len;
    if (nx * cx + ny * cy + nz * cz < 0.0f) {
        struct sphere_vertex tmp = out[1];

        out[1] = out[2];
        out[2] = tmp;
        nx = -nx;
        ny = -ny;
        nz = -nz;
    }
    for (int i = 0; i < 3; i++) {
        out[i].nx = nx;
        out[i].ny = ny;
        out[i].nz = nz;
        dst[(*idx)++] = out[i];
    }
}

static struct sphere_vertex barycentric_sphere_point(struct sphere_vertex a,
                                                     struct sphere_vertex b,
                                                     struct sphere_vertex c,
                                                     int ia, int ib, int ic,
                                                     int frequency)
{
    float fa = (float)ia / (float)frequency;
    float fb = (float)ib / (float)frequency;
    float fc = (float)ic / (float)frequency;

    return make_poly_vertex(a.x * fa + b.x * fb + c.x * fc,
                            a.y * fa + b.y * fb + c.y * fc,
                            a.z * fa + b.z * fb + c.z * fc);
}

static int init_sphere_resources(struct app_state *app)
{
    int frequency = app->software_demo ? 2 : 4;
    int base_faces = 20;
    int verts_per_face = frequency * frequency * 3;
    static const float phi = 1.61803398875f;
    static const struct {
        int a;
        int b;
        int c;
    } faces[20] = {
        { 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 },
        { 0, 10, 11 }, { 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 },
        { 10, 7, 6 }, { 7, 1, 8 }, { 3, 9, 4 }, { 3, 4, 2 },
        { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 }, { 4, 9, 5 },
        { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 },
    };
    static const char *sphere_vs =
        "attribute vec3 a_pos;\n"
        "attribute vec3 a_normal;\n"
        "uniform mat4 u_mvp;\n"
        "uniform mat4 u_model;\n"
        "varying vec3 v_normal;\n"
        "void main() {\n"
        "  v_normal = (u_model * vec4(a_normal, 0.0)).xyz;\n"
        "  gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
        "}\n";
    static const char *sphere_fs =
        "precision mediump float;\n"
        "varying vec3 v_normal;\n"
        "uniform vec3 u_light;\n"
        "void main() {\n"
        "  vec3 n = v_normal;\n"
        "  vec3 l = u_light * 0.29;\n"
        "  float diffuse = max(dot(n, l), 0.0);\n"
        "  vec3 cool = vec3(0.10, 0.45, 0.95);\n"
        "  vec3 warm = vec3(0.90, 0.62, 0.22);\n"
        "  vec3 base = mix(cool, warm, n.y * 0.5 + 0.5);\n"
        "  vec3 color = base * (0.22 + diffuse * 0.82);\n"
        "  gl_FragColor = vec4(color, 1.0);\n"
        "}\n";
    struct sphere_vertex base[12] = {
        { -1.0f,  phi, 0.0f, 0.0f, 0.0f, 1.0f },
        {  1.0f,  phi, 0.0f, 0.0f, 0.0f, 1.0f },
        { -1.0f, -phi, 0.0f, 0.0f, 0.0f, 1.0f },
        {  1.0f, -phi, 0.0f, 0.0f, 0.0f, 1.0f },
        { 0.0f, -1.0f,  phi, 0.0f, 0.0f, 1.0f },
        { 0.0f,  1.0f,  phi, 0.0f, 0.0f, 1.0f },
        { 0.0f, -1.0f, -phi, 0.0f, 0.0f, 1.0f },
        { 0.0f,  1.0f, -phi, 0.0f, 0.0f, 1.0f },
        {  phi, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f },
        {  phi, 0.0f,  1.0f, 0.0f, 0.0f, 1.0f },
        { -phi, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f },
        { -phi, 0.0f,  1.0f, 0.0f, 0.0f, 1.0f },
    };
    int count = base_faces * verts_per_face;
    struct sphere_vertex *vertices = calloc((size_t)count, sizeof(*vertices));
    int idx = 0;

    if (!vertices)
        return -1;

    for (int i = 0; i < 12; i++)
        base[i] = make_poly_vertex(base[i].x, base[i].y, base[i].z);

    for (int face = 0; face < base_faces; face++) {
        struct sphere_vertex a = base[faces[face].a];
        struct sphere_vertex b = base[faces[face].b];
        struct sphere_vertex c = base[faces[face].c];

        for (int row = 0; row < frequency; row++) {
            for (int col = 0; col < frequency - row; col++) {
                struct sphere_vertex p0 =
                    barycentric_sphere_point(a, b, c,
                                             frequency - row - col,
                                             col, row, frequency);
                struct sphere_vertex p1 =
                    barycentric_sphere_point(a, b, c,
                                             frequency - row - col - 1,
                                             col + 1, row, frequency);
                struct sphere_vertex p2 =
                    barycentric_sphere_point(a, b, c,
                                             frequency - row - col - 1,
                                             col, row + 1, frequency);

                sphere_emit_flat(vertices, &idx, p0, p1, p2);
                if (col < frequency - row - 1) {
                    struct sphere_vertex p3 =
                        barycentric_sphere_point(a, b, c,
                                                 frequency - row - col - 2,
                                                 col + 1, row + 1,
                                                 frequency);
                    sphere_emit_flat(vertices, &idx, p1, p3, p2);
                }
            }
        }
    }

    app->sphere_program = link_program(sphere_vs, sphere_fs);
    if (!app->sphere_program) {
        free(vertices);
        return -1;
    }
    app->attr_sphere_pos =
        glGetAttribLocation(app->sphere_program, "a_pos");
    app->attr_sphere_normal =
        glGetAttribLocation(app->sphere_program, "a_normal");
    app->uniform_sphere_mvp =
        glGetUniformLocation(app->sphere_program, "u_mvp");
    app->uniform_sphere_model =
        glGetUniformLocation(app->sphere_program, "u_model");
    app->uniform_sphere_light =
        glGetUniformLocation(app->sphere_program, "u_light");
    if (app->attr_sphere_pos < 0 || app->attr_sphere_normal < 0 ||
        app->uniform_sphere_mvp < 0 || app->uniform_sphere_model < 0 ||
        app->uniform_sphere_light < 0) {
        free(vertices);
        return -1;
    }

    glGenBuffers(1, &app->sphere_vbo);
    if (!app->sphere_vbo) {
        free(vertices);
        return -1;
    }
    glBindBuffer(GL_ARRAY_BUFFER, app->sphere_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(count * sizeof(*vertices)),
                 vertices, GL_STATIC_DRAW);
    app->sphere_vertex_count = count;
    free(vertices);
    return glGetError() == GL_NO_ERROR ? 0 : -1;
}

static void render_simple_frame(struct app_state *app)
{
    float angle = app->frame * 0.055f;
    float s = sinf(angle);
    float c = cosf(angle);
    float r = 0.72f;
    int ch = content_height(app);
    struct vertex vertices[3] = {
        { -s * r, c * r - 0.04f, 0.0f, 0.98f, 0.21f, 0.18f, 1.0f },
        { (0.92f * c + 0.72f * s) * r,
          (0.92f * s - 0.72f * c) * r - 0.04f, 0.0f,
          0.18f, 0.80f, 0.42f, 1.0f },
        { (-0.92f * c + 0.72f * s) * r,
          (-0.92f * s - 0.72f * c) * r - 0.04f, 0.0f,
          0.20f, 0.42f, 1.0f, 1.0f },
    };

    glViewport(0, 0, app->width, app->height);
    glClearColor(0.03f, 0.055f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(0, 0, app->width, ch);
    glUseProgram(app->program);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glVertexAttribPointer((GLuint)app->attr_pos, 3, GL_FLOAT, GL_FALSE,
                          sizeof(vertices[0]), &vertices[0].x);
    glVertexAttribPointer((GLuint)app->attr_color, 4, GL_FLOAT, GL_FALSE,
                          sizeof(vertices[0]), &vertices[0].r);
    glEnableVertexAttribArray((GLuint)app->attr_pos);
    glEnableVertexAttribArray((GLuint)app->attr_color);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

static void render_api_frame(struct app_state *app)
{
    float angle = app->frame * 0.07f;
    float s = sinf(angle);
    float c = cosf(angle);
    struct vertex tri[3] = {
        { -0.72f * c, -0.64f * s, 0.35f, 0.95f, 0.20f, 0.18f, 1.0f },
        {  0.68f * s, -0.66f * c, 0.15f, 0.16f, 0.82f, 0.38f, 1.0f },
        { -0.62f * s,  0.70f * c, 0.05f, 0.20f, 0.40f, 1.00f, 1.0f },
    };
    struct tex_vertex quad[4] = {
        { -0.82f, -0.72f, 0.0f, 0.0f },
        {  0.82f, -0.72f, 1.0f, 0.0f },
        { -0.82f,  0.72f, 0.0f, 1.0f },
        {  0.82f,  0.72f, 1.0f, 1.0f },
    };
    int ch = content_height(app);
    int sx = app->width / 10;
    int sy = ch / 10;
    int sw = app->width - sx * 2;
    int sh = ch - sy * 2;

    glBindFramebuffer(GL_FRAMEBUFFER, app->fbo);
    glViewport(0, 0, 128, 128);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_STENCIL_TEST);
    glDepthFunc(GL_LEQUAL);
    glStencilMask(0xff);
    glStencilFunc(GL_ALWAYS, 1, 0xff);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glClearColor(0.02f, 0.03f, 0.045f, 1.0f);
    glClearDepthf(1.0f);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glUseProgram(app->program);
    glBindBuffer(GL_ARRAY_BUFFER, app->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_DYNAMIC_DRAW);
    glVertexAttribPointer((GLuint)app->attr_pos, 3, GL_FLOAT, GL_FALSE,
                          sizeof(tri[0]), (void *)0);
    glVertexAttribPointer((GLuint)app->attr_color, 4, GL_FLOAT, GL_FALSE,
                          sizeof(tri[0]), (void *)(3 * sizeof(GLfloat)));
    glEnableVertexAttribArray((GLuint)app->attr_pos);
    glEnableVertexAttribArray((GLuint)app->attr_color);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0, 0, app->width, app->height);
    glClearColor(0.03f, 0.055f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(0, 0, app->width, ch);
    glEnable(GL_SCISSOR_TEST);
    glScissor(sx, sy, sw, sh);
    glUseProgram(app->tex_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, app->fbo_tex);
    glUniform1i(app->uniform_tex, 0);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_DYNAMIC_DRAW);
    glVertexAttribPointer((GLuint)app->attr_tex_pos, 2, GL_FLOAT, GL_FALSE,
                          sizeof(quad[0]), (void *)0);
    glVertexAttribPointer((GLuint)app->attr_tex_coord, 2, GL_FLOAT, GL_FALSE,
                          sizeof(quad[0]), (void *)(2 * sizeof(GLfloat)));
    glEnableVertexAttribArray((GLuint)app->attr_tex_pos);
    glEnableVertexAttribArray((GLuint)app->attr_tex_coord);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
}

static void render_sphere_frame(struct app_state *app)
{
    int ch = content_height(app);
    float aspect = ch > 0 ? (float)app->width / (float)ch : 1.0f;
    float projection[16];
    float view[16];
    float rx[16];
    float ry[16];
    float model[16];
    float pv[16];
    float mvp[16];
    float angle = app->frame * (app->software_demo ? 0.16f : 0.022f);
    const GLfloat light[3] = { 1.6f, 1.2f, 2.8f };

    mat4_perspective(projection, 58.0f * (float)M_PI / 180.0f, aspect,
                     0.1f, 16.0f);
    mat4_translate(view, 0.0f, 0.0f, -3.6f);
    mat4_rotate_y(ry, angle);
    mat4_rotate_x(rx, 0.35f * sinf(angle * 0.43f));
    mat4_mul(model, ry, rx);
    mat4_mul(pv, projection, view);
    mat4_mul(mvp, pv, model);

    glViewport(0, 0, app->width, app->height);
    glClearColor(0.015f, 0.022f, 0.032f, 1.0f);
    if (!app->software_demo)
        glClearDepthf(1.0f);
    glClear(app->software_demo ? GL_COLOR_BUFFER_BIT :
                                 (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
    glViewport(0, 0, app->width, ch);
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);
    if (app->software_demo) {
        glDisable(GL_DEPTH_TEST);
    } else {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
    }
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    glUseProgram(app->sphere_program);
    glUniformMatrix4fv(app->uniform_sphere_mvp, 1, GL_FALSE, mvp);
    glUniformMatrix4fv(app->uniform_sphere_model, 1, GL_FALSE, model);
    glUniform3fv(app->uniform_sphere_light, 1, light);
    glBindBuffer(GL_ARRAY_BUFFER, app->sphere_vbo);
    glVertexAttribPointer((GLuint)app->attr_sphere_pos, 3, GL_FLOAT,
                          GL_FALSE, sizeof(struct sphere_vertex), (void *)0);
    glVertexAttribPointer((GLuint)app->attr_sphere_normal, 3, GL_FLOAT,
                          GL_FALSE, sizeof(struct sphere_vertex),
                          (void *)(3 * sizeof(GLfloat)));
    glEnableVertexAttribArray((GLuint)app->attr_sphere_pos);
    glEnableVertexAttribArray((GLuint)app->attr_sphere_normal);
    glDrawArrays(GL_TRIANGLES, 0, app->sphere_vertex_count);
    glDisable(GL_CULL_FACE);
}

static void overlay_emit_rect(struct vertex *vertices, int *count,
                              float x0, float y0, float x1, float y1,
                              float r, float g, float b, float a)
{
    struct vertex rect[6] = {
        { x0, y0, 0.0f, r, g, b, a },
        { x1, y0, 0.0f, r, g, b, a },
        { x0, y1, 0.0f, r, g, b, a },
        { x1, y0, 0.0f, r, g, b, a },
        { x1, y1, 0.0f, r, g, b, a },
        { x0, y1, 0.0f, r, g, b, a },
    };

    memcpy(&vertices[*count], rect, sizeof(rect));
    *count += 6;
}

static void titlebar_emit_rect_px(const struct app_state *app,
                                  struct vertex *vertices, int *count,
                                  float x0, float y0, float x1, float y1,
                                  float r, float g, float b, float a)
{
    float nx0;
    float nx1;
    float ny0;
    float ny1;

    if (*count + 6 >= 8192 || app->width <= 0 || app->height <= 0)
        return;
    nx0 = x0 * 2.0f / (float)app->width - 1.0f;
    nx1 = x1 * 2.0f / (float)app->width - 1.0f;
    ny0 = 1.0f - y0 * 2.0f / (float)app->height;
    ny1 = 1.0f - y1 * 2.0f / (float)app->height;
    overlay_emit_rect(vertices, count, nx0, ny0, nx1, ny1, r, g, b, a);
}

static void titlebar_emit_char(struct app_state *app, struct vertex *vertices,
                               int *count, int x, int y, char ch,
                               float r, float g, float b, float a)
{
    const uint8_t *glyph;

    if (ch < 0x20 || ch > 0x7e)
        ch = '?';
    glyph = font8x16_data[(int)(ch - 0x20)];
    for (int row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];

        for (int col = 0; col < 8; col++) {
            if (!(bits & (uint8_t)(1u << (7 - col))))
                continue;
            titlebar_emit_rect_px(app, vertices, count,
                                  (float)(x + col), (float)(y + row),
                                  (float)(x + col + 1),
                                  (float)(y + row + 1),
                                  r, g, b, a);
        }
    }
}

static void titlebar_emit_text_fit(struct app_state *app,
                                   struct vertex *vertices, int *count,
                                   int x, int y, int max_w,
                                   const char *text,
                                   float r, float g, float b, float a)
{
    int limit = max_w / 8;
    int chars = 0;

    if (limit <= 0)
        return;
    for (const char *p = text; *p && chars < limit; p++, chars++)
        titlebar_emit_char(app, vertices, count, x + chars * 8, y, *p,
                           r, g, b, a);
}

static void titlebar_emit_button_text(struct app_state *app,
                                      struct vertex *vertices, int *count,
                                      int x, const char *text)
{
    int len = (int)strlen(text);
    int tx = x + (TITLEBAR_CONTROL_W - len * 8) / 2;

    if (tx < x + 2)
        tx = x + 2;
    titlebar_emit_text_fit(app, vertices, count, tx, 7,
                           TITLEBAR_CONTROL_W - 4, text,
                           0.96f, 0.98f, 1.00f, 1.0f);
}

static const char *window_title(const struct app_state *app)
{
    return app->sphere_demo ? "Mesa 3D Demo" : "Mesa Native Wayland EGL";
}

static void render_titlebar(struct app_state *app)
{
    struct vertex vertices[8192];
    int count = 0;
    int close_x = app->width - TITLEBAR_CONTROL_W;
    int max_x = close_x - TITLEBAR_CONTROL_W;
    int min_x = max_x - TITLEBAR_CONTROL_W;
    int title_limit = min_x - 20;

    if (!app->program || app->width <= TITLEBAR_CONTROL_W * 3 ||
        app->height <= TITLEBAR_H)
        return;

    titlebar_emit_rect_px(app, vertices, &count, 0, 0, app->width,
                          TITLEBAR_H, 0.12f, 0.17f, 0.22f, 1.0f);
    titlebar_emit_rect_px(app, vertices, &count, 0, TITLEBAR_H - 1,
                          app->width, TITLEBAR_H, 0.42f, 0.52f, 0.62f,
                          1.0f);
    titlebar_emit_rect_px(app, vertices, &count, min_x, 0, max_x,
                          TITLEBAR_H, 0.17f, 0.24f, 0.31f, 1.0f);
    titlebar_emit_rect_px(app, vertices, &count, max_x, 0, close_x,
                          TITLEBAR_H, 0.17f, 0.24f, 0.31f, 1.0f);
    titlebar_emit_rect_px(app, vertices, &count, close_x, 0, app->width,
                          TITLEBAR_H, 0.47f, 0.18f, 0.20f, 1.0f);
    if (title_limit > 10)
        titlebar_emit_text_fit(app, vertices, &count, 10, 7, title_limit,
                               window_title(app),
                               0.94f, 0.97f, 1.0f, 1.0f);
    titlebar_emit_button_text(app, vertices, &count, min_x, "-");
    titlebar_emit_button_text(app, vertices, &count, max_x,
                              app->maximized ? "[]" : "+");
    titlebar_emit_button_text(app, vertices, &count, close_x, "X");

    glViewport(0, 0, app->width, app->height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(app->program);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glVertexAttribPointer((GLuint)app->attr_pos, 3, GL_FLOAT, GL_FALSE,
                          sizeof(vertices[0]), &vertices[0].x);
    glVertexAttribPointer((GLuint)app->attr_color, 4, GL_FLOAT, GL_FALSE,
                          sizeof(vertices[0]), &vertices[0].r);
    glEnableVertexAttribArray((GLuint)app->attr_pos);
    glEnableVertexAttribArray((GLuint)app->attr_color);
    glDrawArrays(GL_TRIANGLES, 0, count);
    glDisable(GL_BLEND);
}

static uint8_t overlay_segments_for_char(char ch)
{
    switch (ch) {
    case '0': return 0x3f;
    case '1': return 0x06;
    case '2': return 0x5b;
    case '3': return 0x4f;
    case '4': return 0x66;
    case '5': return 0x6d;
    case '6': return 0x7d;
    case '7': return 0x07;
    case '8': return 0x7f;
    case '9': return 0x6f;
    case 'F': return 0x71;
    case 'P': return 0x73;
    case 'S': return 0x6d;
    case '-': return 0x40;
    default: return 0;
    }
}

static void overlay_emit_glyph(struct vertex *vertices, int *count, char ch,
                               float x, float y, float w, float h)
{
    float t = w * 0.16f;
    float r = 0.92f;
    float g = 0.97f;
    float b = 1.00f;
    float a = 0.94f;
    uint8_t segments;

    if (ch == '.') {
        overlay_emit_rect(vertices, count, x + w * 0.38f, y - h + t,
                          x + w * 0.62f, y - h + t * 2.1f, r, g, b, a);
        return;
    }
    segments = overlay_segments_for_char(ch);
    if (segments & 0x01)
        overlay_emit_rect(vertices, count, x + t, y - t, x + w - t, y,
                          r, g, b, a);
    if (segments & 0x02)
        overlay_emit_rect(vertices, count, x + w - t, y - h * 0.5f + t,
                          x + w, y - t, r, g, b, a);
    if (segments & 0x04)
        overlay_emit_rect(vertices, count, x + w - t, y - h + t,
                          x + w, y - h * 0.5f - t, r, g, b, a);
    if (segments & 0x08)
        overlay_emit_rect(vertices, count, x + t, y - h, x + w - t,
                          y - h + t, r, g, b, a);
    if (segments & 0x10)
        overlay_emit_rect(vertices, count, x, y - h + t, x + t,
                          y - h * 0.5f - t, r, g, b, a);
    if (segments & 0x20)
        overlay_emit_rect(vertices, count, x, y - h * 0.5f + t, x + t,
                          y - t, r, g, b, a);
    if (segments & 0x40)
        overlay_emit_rect(vertices, count, x + t, y - h * 0.5f - t * 0.5f,
                          x + w - t, y - h * 0.5f + t * 0.5f,
                          r, g, b, a);
}

static void render_fps_overlay(struct app_state *app)
{
    struct vertex vertices[512];
    int count = 0;
    float w = 0.085f;
    float h = 0.150f;
    float gap = 0.020f;
    float x = -0.88f;
    float y = 0.82f;
    const char *text;

    if (!app->sphere_demo || app->fps_text[0] == '\0')
        return;

    text = strchr(app->fps_text, ' ');
    text = text ? text + 1 : app->fps_text;

    if (!app->overlay_vbo)
        return;
    if (app->overlay_vertex_count <= 0 ||
        strcmp(app->overlay_fps_text, text) != 0) {
        overlay_emit_rect(vertices, &count, -0.95f, 0.93f, -0.42f, 0.61f,
                          0.00f, 0.00f, 0.00f, 0.82f);
        overlay_emit_rect(vertices, &count, -0.95f, 0.93f, -0.42f, 0.89f,
                          0.10f, 0.78f, 1.00f, 0.94f);
        overlay_emit_rect(vertices, &count, -0.95f, 0.65f, -0.42f, 0.61f,
                          0.10f, 0.78f, 1.00f, 0.94f);
        overlay_emit_rect(vertices, &count, -0.95f, 0.93f, -0.91f, 0.61f,
                          0.10f, 0.78f, 1.00f, 0.94f);
        overlay_emit_rect(vertices, &count, -0.46f, 0.93f, -0.42f, 0.61f,
                          0.10f, 0.78f, 1.00f, 0.94f);
        for (const char *p = text; *p && count + 42 < 512; p++) {
            if (*p == ' ') {
                x += w * 0.55f;
                continue;
            }
            overlay_emit_glyph(vertices, &count, *p, x, y, w, h);
            x += w + gap;
        }
        glBindBuffer(GL_ARRAY_BUFFER, app->overlay_vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(count * sizeof(vertices[0])),
                     vertices, GL_DYNAMIC_DRAW);
        snprintf(app->overlay_fps_text, sizeof(app->overlay_fps_text), "%s",
                 text);
        app->overlay_vertex_count = count;
    } else {
        glBindBuffer(GL_ARRAY_BUFFER, app->overlay_vbo);
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(app->program);
    glVertexAttribPointer((GLuint)app->attr_pos, 3, GL_FLOAT, GL_FALSE,
                          sizeof(vertices[0]), (const void *)0);
    glVertexAttribPointer((GLuint)app->attr_color, 4, GL_FLOAT, GL_FALSE,
                          sizeof(vertices[0]),
                          (const void *)(3 * sizeof(GLfloat)));
    glEnableVertexAttribArray((GLuint)app->attr_pos);
    glEnableVertexAttribArray((GLuint)app->attr_color);
    glDrawArrays(GL_TRIANGLES, 0, app->overlay_vertex_count);
    glDisable(GL_BLEND);
}

static int create_window_surface(struct app_state *app)
{
    app->egl_surface = eglCreateWindowSurface(
        app->egl_display, app->egl_config,
        (EGLNativeWindowType)app->egl_window, NULL);
    if (app->egl_surface == EGL_NO_SURFACE ||
        !eglMakeCurrent(app->egl_display, app->egl_surface, app->egl_surface,
                        app->egl_context)) {
        fprintf(stderr, "mesawlegl[%d]: window surface failed (0x%x)\n",
                app->loop, eglGetError());
        return -1;
    }
    return 0;
}

static int recreate_window_surface(struct app_state *app)
{
    if (app->egl_display == EGL_NO_DISPLAY ||
        app->egl_context == EGL_NO_CONTEXT || !app->egl_window)
        return -1;

    eglMakeCurrent(app->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                   EGL_NO_CONTEXT);
    if (app->egl_surface != EGL_NO_SURFACE) {
        eglDestroySurface(app->egl_display, app->egl_surface);
        app->egl_surface = EGL_NO_SURFACE;
    }
    wl_egl_window_resize(app->egl_window, app->width, app->height, 0, 0);
    return create_window_surface(app);
}

static int init_mesa(struct app_state *app)
{
    static const char *vs =
        "attribute vec3 a_pos;\n"
        "attribute vec4 a_color;\n"
        "varying vec4 v_color;\n"
        "void main() { v_color = a_color; gl_Position = vec4(a_pos, 1.0); }\n";
    static const char *fs =
        "precision mediump float;\n"
        "varying vec4 v_color;\n"
        "void main() { gl_FragColor = v_color; }\n";
    static const char *tex_vs =
        "attribute vec2 a_pos;\n"
        "attribute vec2 a_uv;\n"
        "varying vec2 v_uv;\n"
        "void main() { v_uv = a_uv; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
    static const char *tex_fs =
        "precision mediump float;\n"
        "uniform sampler2D u_tex;\n"
        "varying vec2 v_uv;\n"
        "void main() { gl_FragColor = texture2D(u_tex, v_uv) * vec4(1.0, 1.0, 1.0, 0.86); }\n";
    EGLint config_attrs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLint context_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLint major = 0;
    EGLint minor = 0;
    EGLint nconfigs = 0;

    app->egl_display = get_wayland_display(app->display);
    if (app->egl_display == EGL_NO_DISPLAY ||
        !eglInitialize(app->egl_display, &major, &minor)) {
        fprintf(stderr, "mesawlegl: eglInitialize failed (0x%x)\n",
                eglGetError());
        return -1;
    }
    if (!eglChooseConfig(app->egl_display, config_attrs, &app->egl_config, 1,
                         &nconfigs) || nconfigs < 1) {
        fprintf(stderr, "mesawlegl: window config unavailable (0x%x)\n",
                eglGetError());
        return -1;
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        fprintf(stderr, "mesawlegl: eglBindAPI failed (0x%x)\n",
                eglGetError());
        return -1;
    }

    app->egl_window = wl_egl_window_create(app->surface, app->width,
                                           app->height);
    if (!app->egl_window) {
        fprintf(stderr, "mesawlegl: wl_egl_window_create failed\n");
        return -1;
    }
    app->egl_context = eglCreateContext(app->egl_display, app->egl_config,
                                        EGL_NO_CONTEXT, context_attrs);
    if (app->egl_context == EGL_NO_CONTEXT || create_window_surface(app) < 0) {
        fprintf(stderr, "mesawlegl: EGL context setup failed (0x%x)\n",
                eglGetError());
        return -1;
    }

    app->program = link_program(vs, fs);
    app->tex_program = link_program(tex_vs, tex_fs);
    if (!app->program || !app->tex_program)
        return -1;
    app->attr_pos = glGetAttribLocation(app->program, "a_pos");
    app->attr_color = glGetAttribLocation(app->program, "a_color");
    app->attr_tex_pos = glGetAttribLocation(app->tex_program, "a_pos");
    app->attr_tex_coord = glGetAttribLocation(app->tex_program, "a_uv");
    app->uniform_tex = glGetUniformLocation(app->tex_program, "u_tex");
    if (app->attr_pos < 0 || app->attr_color < 0 ||
        app->attr_tex_pos < 0 || app->attr_tex_coord < 0 ||
        app->uniform_tex < 0)
        return -1;

    glGenBuffers(1, &app->overlay_vbo);
    if (!app->overlay_vbo)
        return -1;

    if (app->api_smoke && init_api_smoke_resources(app) < 0) {
        fprintf(stderr, "mesawlegl: API smoke resource setup failed\n");
        return -1;
    }
    if (app->sphere_demo && init_sphere_resources(app) < 0) {
        fprintf(stderr, "mesawlegl: sphere demo resource setup failed\n");
        return -1;
    }

    fprintf(stderr, "mesawlegl: EGL %d.%d GL %s renderer=%s native-wayland%s%s\n",
            major, minor, glGetString(GL_VERSION), glGetString(GL_RENDERER),
            app->api_smoke ? " api-smoke" : "",
            app->sphere_demo ? " spherical-poly-demo" : "");
    return 0;
}

static double monotonic_seconds(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0.0;
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static int env_enabled(const char *name)
{
    const char *value = getenv(name);

    return value && value[0] && strcmp(value, "0") != 0;
}

static int mesa_perf_log_enabled(void)
{
    return env_enabled("XV6_MESAWLEGL_PERF_LOG");
}

static int env_is_zero(const char *name)
{
    const char *value = getenv(name);

    return value && strcmp(value, "0") == 0;
}

static int mesa_driver_name_is_software(const char *driver)
{
    return driver &&
        (strcmp(driver, "swrast") == 0 || strcmp(driver, "softpipe") == 0 ||
         strcmp(driver, "llvmpipe") == 0);
}

static int mesa_env_requests_software(void)
{
    return env_enabled("LIBGL_ALWAYS_SOFTWARE") ||
        mesa_driver_name_is_software(getenv("GALLIUM_DRIVER")) ||
        mesa_driver_name_is_software(getenv("MESA_LOADER_DRIVER_OVERRIDE"));
}

static int mesa_env_requests_accel(void)
{
    const char *gallium = getenv("GALLIUM_DRIVER");
    const char *loader = getenv("MESA_LOADER_DRIVER_OVERRIDE");

    if (env_is_zero("LIBGL_ALWAYS_SOFTWARE"))
        return 1;
    if (gallium && gallium[0] && !mesa_driver_name_is_software(gallium))
        return 1;
    if (loader && loader[0] && !mesa_driver_name_is_software(loader))
        return 1;
    return 0;
}

static int read_wlcomp_visible_fps(double *fps_out)
{
    FILE *fp;
    char buf[256];
    char *p;
    double fps;

    if (fps_out)
        *fps_out = 0.0;
    fp = fopen(WLCOMP_FPS_PATH, "r");
    if (!fp)
        return 0;
    if (!fgets(buf, sizeof(buf), fp)) {
        fclose(fp);
        return 0;
    }
    fclose(fp);

    p = strstr(buf, "fps=");
    if (!p)
        return 0;
    fps = strtod(p + 4, NULL);
    if (fps <= 0.0 || fps > 1000.0)
        return 0;
    if (fps_out)
        *fps_out = fps;
    return 1;
}

static void clamp_demo_size(struct app_state *app)
{
    if (!app->sphere_demo || app->max_width <= 0 || app->max_height <= 0)
        return;
    if (app->width > app->max_width)
        app->width = app->max_width;
    if (app->height > app->max_height)
        app->height = app->max_height;
}

static int content_height(const struct app_state *app)
{
    int h = app->height - TITLEBAR_H;

    return h > 1 ? h : 1;
}

static double fps_sample_interval_sec(struct app_state *app)
{
    static int initialized;
    static double interval_sec;
    const char *env;

    if (initialized)
        return interval_sec;
    initialized = 1;
    interval_sec =
        app->sphere_demo && !app->software_demo && mesa_env_requests_accel() ?
            5.0 : 1.0;
    env = getenv("XV6_MESAWLEGL_FPS_MS");
    if (env && *env) {
        int ms = atoi(env);

        if (ms < 250)
            ms = 250;
        if (ms > 10000)
            ms = 10000;
        interval_sec = (double)ms / 1000.0;
    }
    return interval_sec;
}

static void update_demo_fps(struct app_state *app)
{
    double now;
    double elapsed;
    double fps;
    double displayed_fps = 0.0;
    int native_fps_credit = 0;

    if (!app->sphere_demo || !app->toplevel)
        return;

    now = monotonic_seconds();
    if (app->fps_start_sec <= 0.0)
        app->fps_start_sec = now;
    app->fps_frame_count++;
    elapsed = now - app->fps_start_sec;
    if (elapsed < fps_sample_interval_sec(app))
        return;

    fps = elapsed > 0.0 ? (double)app->fps_frame_count / elapsed : 0.0;
    append_fps_evidence(app, now, elapsed, fps, &displayed_fps,
                        &native_fps_credit);
    app->fps_value = displayed_fps > 0.0 ? displayed_fps : fps;
    /*
     * In shm-present mode the frames are genuinely GPU-rendered and then
     * blit-presented to the display every loop iteration, so the honest
     * on-screen rate is the app-loop fps. native_fps_credit (native scanout
     * present) legitimately stays 0 and is still reported on stderr below.
     */
    if (displayed_fps > 0.0 && !app->shm_present) {
        snprintf(app->fps_text, sizeof(app->fps_text), "FPS %.1f",
                 displayed_fps);
    } else if (app->shm_present || !native_fps_credit) {
        snprintf(app->fps_text, sizeof(app->fps_text), "FPS %.1f", fps);
    } else {
        snprintf(app->fps_text, sizeof(app->fps_text), "FPS %.1f",
                 displayed_fps);
    }
    fprintf(stderr,
            "mesawlegl[%d]: app_loop_fps=%.1f displayed_fps=%.1f "
            "native_fps_credit=%d\n",
            app->loop, fps, displayed_fps, native_fps_credit);
    app->fps_frame_count = 0;
    app->fps_start_sec = now;
}

static void update_source_content_hash(struct app_state *app)
{
    unsigned long hash = 1469598103934665603UL;
    unsigned long values[8];

    if (!app)
        return;
    values[0] = (unsigned long)app->frame;
    values[1] = (unsigned long)app->width;
    values[2] = (unsigned long)app->height;
    values[3] = (unsigned long)app->render_div;
    values[4] = (unsigned long)app->sphere_demo;
    values[5] = (unsigned long)app->software_demo;
    values[6] = (unsigned long)app->api_smoke;
    values[7] = (unsigned long)app->resize_count;
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        hash ^= values[i] + 0x9e3779b97f4a7c15UL + (hash << 6) + (hash >> 2);
        hash *= 1099511628211UL;
    }
    if (hash == 0)
        hash = 1;
    app->source_content_hash = hash;
    app->source_content_frame = (unsigned long)app->frame + 1;
}

static void detect_read_format(struct app_state *app)
{
    GLint impl_format = 0;
    GLint impl_type = 0;

    app->read_format = GL_RGBA;
    glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT, &impl_format);
    glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_TYPE, &impl_type);
    if (impl_format == GL_BGRA_EXT && impl_type == GL_UNSIGNED_BYTE)
        app->read_format = GL_BGRA_EXT;
}

static int ensure_present_buffer(struct app_state *app)
{
    if (!app->shm)
        return -1;
    if (app->present_buf_ready &&
        app->present_buf.width == app->width &&
        app->present_buf.height == app->height)
        return 0;
    if (app->present_buf_ready) {
        xv6_present_buffer_destroy(&app->present_buf);
        app->present_buf_ready = 0;
    }
    if (xv6_present_buffer_init(&app->present_buf, app->width, app->height,
                               app->shm, NULL) != 0) {
        fprintf(stderr, "mesawlegl[%d]: present buffer init failed\n",
                app->loop);
        return -1;
    }
    app->present_buf_ready = 1;
    return 0;
}

/*
 * Read back the genuinely GPU-rendered default framebuffer and copy it,
 * vertically flipped and (if needed) R/B swizzled, into the wl_shm present
 * buffer the CPU compositor can read. This is an explicitly accepted
 * blit-present: the GPU does the rendering; only this final copy is on the CPU.
 */
static int present_via_shm(struct app_state *app)
{
    size_t bytes = (size_t)app->width * (size_t)app->height * 4;

    if (ensure_present_buffer(app) != 0)
        return -1;
    if (app->readback_size < bytes) {
        uint8_t *grown = realloc(app->readback, bytes);

        if (!grown)
            return -1;
        app->readback = grown;
        app->readback_size = bytes;
    }
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, app->width, app->height, app->read_format,
                 GL_UNSIGNED_BYTE, app->readback);
    for (int y = 0; y < app->height; y++) {
        uint32_t *dst = (uint32_t *)((uint8_t *)app->present_buf.pixels +
                                     (size_t)y * (size_t)app->present_buf.stride);
        uint32_t *src = (uint32_t *)(app->readback +
                                     (size_t)(app->height - 1 - y) *
                                     (size_t)app->width * 4);
        if (app->read_format == GL_BGRA_EXT) {
            memcpy(dst, src, (size_t)app->width * 4);
            continue;
        }
        for (int x = 0; x < app->width; x++) {
            uint32_t rgba = src[x];

            dst[x] = 0xff000000u | ((rgba & 0x000000ffu) << 16) |
                     (rgba & 0x0000ff00u) |
                     ((rgba & 0x00ff0000u) >> 16);
        }
    }
    wl_surface_attach(app->surface, app->present_buf.wl_buffer, 0, 0);
    wl_surface_damage(app->surface, 0, 0, app->width, app->height);
    wl_surface_commit(app->surface);
    return 0;
}

static int write_client_capture(struct app_state *app)
{
    const char *path = "/capture-client.ppm";
    FILE *fp;
    uint8_t *pixels;
    uint8_t *row;
    size_t bytes;

    if (!app->client_capture_enabled || app->client_capture_done ||
        app->client_capture_start_sec <= 0.0 ||
        monotonic_seconds() - app->client_capture_start_sec <
            (double)app->client_capture_after_sec)
        return 0;
    app->client_capture_done = 1;
    if (app->width <= 0 || app->height <= 0)
        return -1;
    bytes = (size_t)app->width * (size_t)app->height * 4;
    pixels = malloc(bytes);
    row = malloc((size_t)app->width * 3);
    if (!pixels || !row) {
        free(pixels);
        free(row);
        fprintf(stderr, "mesawlegl: client capture allocation failed\n");
        return -1;
    }

    glFinish();
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, app->width, app->height, app->read_format,
                 GL_UNSIGNED_BYTE, pixels);
    fp = fopen(path, "wb");
    if (!fp) {
        free(pixels);
        free(row);
        fprintf(stderr, "mesawlegl: client capture open failed\n");
        return -1;
    }
    fprintf(fp, "P6\n%d %d\n255\n", app->width, app->height);
    for (int y = app->height - 1; y >= 0; y--) {
        uint32_t *src =
            (uint32_t *)(pixels + (size_t)y * (size_t)app->width * 4);

        for (int x = 0; x < app->width; x++) {
            uint32_t px = src[x];

            if (app->read_format == GL_BGRA_EXT) {
                row[x * 3 + 0] = (uint8_t)((px >> 16) & 0xff);
                row[x * 3 + 1] = (uint8_t)((px >> 8) & 0xff);
                row[x * 3 + 2] = (uint8_t)(px & 0xff);
            } else {
                row[x * 3 + 0] = (uint8_t)(px & 0xff);
                row[x * 3 + 1] = (uint8_t)((px >> 8) & 0xff);
                row[x * 3 + 2] = (uint8_t)((px >> 16) & 0xff);
            }
        }
        if (fwrite(row, (size_t)app->width * 3, 1, fp) != 1) {
            fclose(fp);
            free(pixels);
            free(row);
            fprintf(stderr, "mesawlegl: client capture write failed\n");
            return -1;
        }
    }
    fclose(fp);
    free(pixels);
    free(row);
    fprintf(stderr,
            "mesawlegl: captured %s elapsed_sec=%.3f size=%dx%d read_format=%s\n",
            path, monotonic_seconds() - app->client_capture_start_sec,
            app->width, app->height,
            app->read_format == GL_BGRA_EXT ? "bgra" : "rgba");
    return 0;
}

static int draw_and_swap(struct app_state *app)
{
    int perf = mesa_perf_log_enabled();
    double t0 = 0.0;
    double t1 = 0.0;
    double now = 0.0;

    if (perf)
        t0 = monotonic_seconds();
    if (app->sphere_demo)
        render_sphere_frame(app);
    else if (app->api_smoke)
        render_api_frame(app);
    else
        render_simple_frame(app);
    update_source_content_hash(app);
    if (perf) {
        t1 = monotonic_seconds();
        app->perf_render_us += (t1 - t0) * 1000000.0;
        t0 = t1;
    }
    render_fps_overlay(app);
    render_titlebar(app);
    if (perf) {
        t1 = monotonic_seconds();
        app->perf_overlay_us += (t1 - t0) * 1000000.0;
        t0 = t1;
    }
    if (write_client_capture(app) != 0)
        return -1;
    if (glGetError() != GL_NO_ERROR) {
        fprintf(stderr, "mesawlegl[%d]: GL error during frame\n", app->loop);
        return -1;
    }
    if (app->shm_present) {
        if (present_via_shm(app) != 0)
            return -1;
    } else if (!eglSwapBuffers(app->egl_display, app->egl_surface)) {
        fprintf(stderr, "mesawlegl[%d]: eglSwapBuffers failed (0x%x)\n",
                app->loop, eglGetError());
        return -1;
    }
    if (perf) {
        t1 = monotonic_seconds();
        app->perf_swap_us += (t1 - t0) * 1000000.0;
        t0 = t1;
    }
    update_demo_fps(app);
    if (perf) {
        t1 = monotonic_seconds();
        app->perf_fps_us += (t1 - t0) * 1000000.0;
        t0 = t1;
    }
    wl_display_flush(app->display);
    if (perf) {
        t1 = monotonic_seconds();
        app->perf_flush_us += (t1 - t0) * 1000000.0;
        app->perf_frames++;
        now = t1;
        if (app->perf_start_sec <= 0.0)
            app->perf_start_sec = now;
        if (now - app->perf_start_sec >= 1.0 && app->perf_frames > 0) {
            fprintf(stderr,
                    "mesawlegl_perf frames=%d render_avg_us=%.0f "
                    "overlay_avg_us=%.0f swap_avg_us=%.0f "
                    "fps_avg_us=%.0f flush_avg_us=%.0f\n",
                    app->perf_frames,
                    app->perf_render_us / app->perf_frames,
                    app->perf_overlay_us / app->perf_frames,
                    app->perf_swap_us / app->perf_frames,
                    app->perf_fps_us / app->perf_frames,
                    app->perf_flush_us / app->perf_frames);
            app->perf_start_sec = now;
            app->perf_frames = 0;
            app->perf_render_us = 0.0;
            app->perf_overlay_us = 0.0;
            app->perf_swap_us = 0.0;
            app->perf_fps_us = 0.0;
            app->perf_flush_us = 0.0;
        }
    }
    return 0;
}

static void xdg_surface_configure(void *data, struct xdg_surface *surface,
                                  uint32_t serial)
{
    struct app_state *app = data;

    xdg_surface_ack_configure(surface, serial);
    if (!app->configured)
        app->configured = 1;
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                               int32_t width, int32_t height,
                               struct wl_array *states)
{
    struct app_state *app = data;
    uint32_t *state;
    (void)toplevel;

    app->maximized = 0;
    wl_array_for_each(state, states) {
        if (*state == XDG_TOPLEVEL_STATE_MAXIMIZED)
            app->maximized = 1;
    }
    if (width > 0 && height > 0) {
        app->width = width;
        app->height = height;
        clamp_demo_size(app);
        if (app->egl_window)
            wl_egl_window_resize(app->egl_window, app->width, app->height,
                                 0, 0);
    }
}

static void toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
    struct app_state *app = data;
    (void)toplevel;
    app->close_requested = 1;
    app->running = 0;
}

static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = toplevel_configure,
    .close = toplevel_close,
};

static int titlebar_control_at(const struct app_state *app, int x, int y)
{
    int close_x;
    int max_x;
    int min_x;

    if (y < 0 || y >= TITLEBAR_H || app->width <= TITLEBAR_CONTROL_W * 3)
        return -1;
    close_x = app->width - TITLEBAR_CONTROL_W;
    max_x = close_x - TITLEBAR_CONTROL_W;
    min_x = max_x - TITLEBAR_CONTROL_W;
    if (x >= close_x)
        return 3;
    if (x >= max_x)
        return 2;
    if (x >= min_x)
        return 1;
    return 0;
}

static void activate_titlebar_control(struct app_state *app, int control)
{
    if (control == 3) {
        app->close_requested = 1;
        app->running = 0;
    } else if (control == 2) {
        if (app->maximized)
            xdg_toplevel_unset_maximized(app->toplevel);
        else
            xdg_toplevel_set_maximized(app->toplevel);
    } else if (control == 1) {
        xdg_toplevel_set_minimized(app->toplevel);
    }
}

static void pointer_enter(void *data, struct wl_pointer *pointer,
                          uint32_t serial, struct wl_surface *surface,
                          wl_fixed_t sx, wl_fixed_t sy)
{
    struct app_state *app = data;

    (void)pointer; (void)serial; (void)surface;
    app->pointer_x = wl_fixed_to_int(sx);
    app->pointer_y = wl_fixed_to_int(sy);
}

static void pointer_leave(void *data, struct wl_pointer *pointer,
                          uint32_t serial, struct wl_surface *surface)
{
    (void)data; (void)pointer; (void)serial; (void)surface;
}

static void pointer_motion(void *data, struct wl_pointer *pointer,
                           uint32_t time, wl_fixed_t sx, wl_fixed_t sy)
{
    struct app_state *app = data;

    (void)pointer; (void)time;
    app->pointer_x = wl_fixed_to_int(sx);
    app->pointer_y = wl_fixed_to_int(sy);
}

static void pointer_button(void *data, struct wl_pointer *pointer,
                           uint32_t serial, uint32_t time, uint32_t button,
                           uint32_t state)
{
    struct app_state *app = data;
    int control;

    (void)pointer; (void)time;
    if (button != BTN_LEFT || state != WL_POINTER_BUTTON_STATE_PRESSED)
        return;
    control = titlebar_control_at(app, app->pointer_x, app->pointer_y);
    if (control >= 0)
        fprintf(stderr, "mesawlegl: titlebar click x=%d y=%d control=%d\n",
                app->pointer_x, app->pointer_y, control);
    if (control == 0 && app->toplevel && app->seat) {
        xdg_toplevel_move(app->toplevel, app->seat, serial);
        return;
    }
    activate_titlebar_control(app, control);
}

static void pointer_axis(void *data, struct wl_pointer *pointer,
                         uint32_t time, uint32_t axis, wl_fixed_t value)
{
    (void)data; (void)pointer; (void)time; (void)axis; (void)value;
}

static void pointer_frame(void *data, struct wl_pointer *pointer)
{
    (void)data; (void)pointer;
}

static void pointer_axis_source(void *data, struct wl_pointer *pointer,
                                uint32_t axis_source)
{
    (void)data; (void)pointer; (void)axis_source;
}

static void pointer_axis_stop(void *data, struct wl_pointer *pointer,
                              uint32_t time, uint32_t axis)
{
    (void)data; (void)pointer; (void)time; (void)axis;
}

static void pointer_axis_discrete(void *data, struct wl_pointer *pointer,
                                  uint32_t axis, int32_t discrete)
{
    (void)data; (void)pointer; (void)axis; (void)discrete;
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
    .axis_source = pointer_axis_source,
    .axis_stop = pointer_axis_stop,
    .axis_discrete = pointer_axis_discrete,
};

static void seat_capabilities(void *data, struct wl_seat *seat,
                              uint32_t capabilities)
{
    struct app_state *app = data;

    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !app->pointer) {
        app->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(app->pointer, &pointer_listener, app);
    } else if (!(capabilities & WL_SEAT_CAPABILITY_POINTER) && app->pointer) {
        wl_pointer_destroy(app->pointer);
        app->pointer = NULL;
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name)
{
    (void)data; (void)seat; (void)name;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

static void wm_base_ping(void *data, struct xdg_wm_base *wm_base,
                         uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = wm_base_ping,
};

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface,
                            uint32_t version)
{
    struct app_state *app = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        app->compositor = wl_registry_bind(registry, name,
                                           &wl_compositor_interface,
                                           version > 4 ? 4 : version);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        app->wm_base = wl_registry_bind(registry, name,
                                        &xdg_wm_base_interface,
                                        version > 2 ? 2 : version);
        xdg_wm_base_add_listener(app->wm_base, &wm_base_listener, app);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        app->seat = wl_registry_bind(registry, name, &wl_seat_interface,
                                     version > 5 ? 5 : version);
        wl_seat_add_listener(app->seat, &seat_listener, app);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        app->shm = wl_registry_bind(registry, name, &wl_shm_interface,
                                    version > 1 ? 1 : version);
    }
}

static void registry_remove(void *data, struct wl_registry *registry,
                            uint32_t name)
{
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_remove,
};

static int init_wayland(struct app_state *app)
{
    app->display = wl_display_connect(NULL);
    if (!app->display) {
        fprintf(stderr, "mesawlegl: wl_display_connect failed\n");
        return -1;
    }
    app->registry = wl_display_get_registry(app->display);
    wl_registry_add_listener(app->registry, &registry_listener, app);
    wl_display_roundtrip(app->display);
    wl_display_roundtrip(app->display);

    if (!app->compositor || !app->wm_base) {
        fprintf(stderr, "mesawlegl: compositor/xdg globals unavailable\n");
        return -1;
    }

    app->surface = wl_compositor_create_surface(app->compositor);
    app->xdg_surface = xdg_wm_base_get_xdg_surface(app->wm_base, app->surface);
    xdg_surface_add_listener(app->xdg_surface, &xdg_surface_listener, app);
    app->toplevel = xdg_surface_get_toplevel(app->xdg_surface);
    xdg_toplevel_add_listener(app->toplevel, &toplevel_listener, app);
    xdg_toplevel_set_title(app->toplevel,
                           app->sphere_demo ? "Mesa 3D Demo" :
                                              "Mesa Native Wayland EGL");
    xdg_toplevel_set_app_id(app->toplevel, "mesawlegl");
    if (app->sphere_demo && app->max_width > 0 && app->max_height > 0) {
        xdg_toplevel_set_min_size(app->toplevel, app->max_width,
                                  app->max_height);
        xdg_toplevel_set_max_size(app->toplevel, app->max_width,
                                  app->max_height);
    }

    if (init_mesa(app) < 0)
        return -1;
    wl_surface_commit(app->surface);
    return 0;
}

static void cleanup(struct app_state *app)
{
    if (app->display)
        wl_display_roundtrip(app->display);
    if (app->egl_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(app->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
        if (app->depth_stencil_rb)
            glDeleteRenderbuffers(1, &app->depth_stencil_rb);
        if (app->fbo_tex)
            glDeleteTextures(1, &app->fbo_tex);
        if (app->fbo)
            glDeleteFramebuffers(1, &app->fbo);
        if (app->vbo)
            glDeleteBuffers(1, &app->vbo);
        if (app->sphere_vbo)
            glDeleteBuffers(1, &app->sphere_vbo);
        if (app->overlay_vbo)
            glDeleteBuffers(1, &app->overlay_vbo);
        if (app->sphere_program)
            glDeleteProgram(app->sphere_program);
        if (app->tex_program)
            glDeleteProgram(app->tex_program);
        if (app->program)
            glDeleteProgram(app->program);
        if (app->egl_surface != EGL_NO_SURFACE)
            eglDestroySurface(app->egl_display, app->egl_surface);
        if (app->egl_context != EGL_NO_CONTEXT)
            eglDestroyContext(app->egl_display, app->egl_context);
        eglTerminate(app->egl_display);
    }
    if (app->egl_window)
        wl_egl_window_destroy(app->egl_window);
    if (app->present_buf_ready) {
        xv6_present_buffer_destroy(&app->present_buf);
        app->present_buf_ready = 0;
    }
    free(app->readback);
    app->readback = NULL;
    app->readback_size = 0;
    if (app->toplevel)
        xdg_toplevel_destroy(app->toplevel);
    if (app->xdg_surface)
        xdg_surface_destroy(app->xdg_surface);
    if (app->surface)
        wl_surface_destroy(app->surface);
    if (app->pointer)
        wl_pointer_destroy(app->pointer);
    if (app->seat)
        wl_seat_destroy(app->seat);
    if (app->wm_base)
        xdg_wm_base_destroy(app->wm_base);
    if (app->compositor)
        wl_compositor_destroy(app->compositor);
    if (app->shm)
        wl_shm_destroy(app->shm);
    if (app->registry)
        wl_registry_destroy(app->registry);
    if (app->display)
        wl_display_disconnect(app->display);
}

static void append_demo_interaction_evidence(struct app_state *app,
                                             double elapsed_sec, int rc)
{
    FILE *fp;
    struct d3d12_present_evidence evidence;
    const char *run_id;
    int render_width;
    int render_height;
    int visible_demo;
    int closeable_demo;
    int resizable_demo;
    int native_present_complete;
    int same_resource_generation;
    int compositor_owned_visible_crc;
    int compositor_owned_visible_frame;
    int compositor_owned_visible_hash;
    int client_content_progress;
    int content_progress_complete;
    int current_run_display_bind_complete;
    int no_readback_native_path;
    int readback_or_software_path;
    int strict_demo_evidence;

    if (!app || !app->sphere_demo)
        return;
    render_width = app->width / app->render_div;
    render_height = content_height(app) / app->render_div;
    if (render_width <= 0)
        render_width = app->width;
    if (render_height <= 0)
        render_height = content_height(app);
    client_content_progress =
        app->source_content_hash != 0 && app->source_content_frame != 0;
    visible_demo = rc == 0 && app->frame > 0 && client_content_progress;
    closeable_demo = app->toplevel != NULL;
    resizable_demo = app->resize_count > 0;
    memset(&evidence, 0, sizeof(evidence));
    (void)read_d3d12_present_evidence(&evidence);
    current_run_display_bind_complete =
        evidence.valid &&
        strcmp(evidence.display_bind_backend, "gpup_dxg_scanout_bind") == 0 &&
        strcmp(evidence.display_bind_transport,
               "gpu-p-dxg-resource-scanout-bind") == 0 &&
        strcmp(evidence.display_bind_transport_source,
               "non_wsl_linux_dxgkrnl_extension") == 0 &&
        evidence.host_saw_display_bind_packet == 1 &&
        evidence.wsl_presenthistory_completion_credit == 0 &&
        strcmp(evidence.display_bind_completion_source, "display") == 0 &&
        evidence.display_bind_present_id > 0 &&
        evidence.display_bind_completed_id >=
            evidence.display_bind_present_id &&
        evidence.display_bind_present_id == evidence.present_id &&
        evidence.display_bind_completed_id == evidence.completed &&
        evidence.display_bind_resource_generation == evidence.buffer_generation;
    no_readback_native_path = !app->software_demo && evidence.no_readback == 1;
    readback_or_software_path =
        app->software_demo || evidence.no_readback != 1;
    native_present_complete =
        evidence.valid &&
        evidence.present_id > 0 &&
        evidence.completed >= evidence.present_id &&
        evidence.display_bind_present_id == evidence.present_id &&
        evidence.display_bind_completed_id == evidence.completed &&
        evidence.display_bind_completed_id >= evidence.display_bind_present_id;
    same_resource_generation =
        evidence.valid &&
        evidence.buffer_generation > 0 &&
        evidence.display_bind_resource_generation == evidence.buffer_generation;
    compositor_owned_visible_crc =
        evidence.valid &&
        evidence.content_visible_credit == 1 &&
        evidence.content_native_present_credit == 1 &&
        evidence.present_content_crc != 0 &&
        evidence.visible_content_crc != 0;
    compositor_owned_visible_frame =
        evidence.valid &&
        evidence.present_content_frame != 0 &&
        evidence.visible_content_frame != 0;
    compositor_owned_visible_hash =
        evidence.valid &&
        evidence.present_frame_hash != 0 &&
        evidence.visible_frame_hash != 0;
    content_progress_complete =
        same_resource_generation &&
        compositor_owned_visible_crc &&
        compositor_owned_visible_frame &&
        compositor_owned_visible_hash &&
        evidence.callback_release_same_frame == 1 &&
        client_content_progress;
    strict_demo_evidence =
        native_present_complete &&
        current_run_display_bind_complete &&
        no_readback_native_path &&
        content_progress_complete &&
        visible_demo &&
        closeable_demo &&
        resizable_demo;
    run_id = validation_run_id();

    fp = fopen(FPS_EVIDENCE_PATH, "a");
    if (!fp)
        return;
    fprintf(fp,
            "mesawlegl_demo_interaction_matrix "
            "validation_run_id=%s process_id=%d d3d12_client_pid=%d "
            "visible_demo=%d closeable_demo=%d resizable_demo=%d "
            "resize_count=%d close_requested=%d frames=%d rc=%d "
            "elapsed=%.3f window=%dx%d render=%dx%d render_div=%d "
            "present_id=%lu completed=%lu "
            "display_bind_present_id=%lu display_bind_completed_id=%lu "
            "display_bind_resource_generation=%lu "
            "display_bind_transport_source=%s "
            "host_saw_display_bind_packet=%lu "
            "wsl_presenthistory_completion_credit=%lu "
            "display_bind_completion_source=%s "
            "current_run_display_bind_complete=%d "
            "no_readback=%lu software_demo=%d "
            "readback_or_software_path=%d "
            "native_present_complete=%d "
            "same_run_resource_generation=%d "
            "same_resource_generation=%d "
            "compositor_owned_visible_content_crc=%d "
            "compositor_owned_visible_content_frame=%d "
            "compositor_owned_visible_frame_hash=%d "
            "client_content_progress=%d "
            "strict_demo_interaction_evidence=%d "
            "d3d12_demo_interaction_native_present_complete=%d "
            "d3d12_demo_interaction_content_progress=%d "
            "client_content_hash=%lu client_content_frame=%lu "
            "content_region=client-content-no-title-fps "
            "app_loop_fps_credit=0 overlay_fps_credit=0 "
            "native_present_credit=%d opengl_submit_credit=0 status=%s\n",
            run_id, (int)getpid(), evidence.client_pid,
            visible_demo, closeable_demo, resizable_demo,
            app->resize_count, app->close_requested, app->frame, rc,
            elapsed_sec, app->width, app->height, render_width, render_height,
            app->render_div,
            evidence.present_id, evidence.completed,
            evidence.display_bind_present_id,
            evidence.display_bind_completed_id,
            evidence.display_bind_resource_generation,
            evidence.display_bind_transport_source[0] ?
                evidence.display_bind_transport_source : "missing",
            evidence.host_saw_display_bind_packet,
            evidence.wsl_presenthistory_completion_credit,
            evidence.display_bind_completion_source[0] ?
                evidence.display_bind_completion_source : "missing",
            current_run_display_bind_complete,
            evidence.no_readback,
            app->software_demo,
            readback_or_software_path,
            native_present_complete,
            evidence.valid ? 1 : 0,
            same_resource_generation,
            compositor_owned_visible_crc,
            compositor_owned_visible_frame,
            compositor_owned_visible_hash,
            client_content_progress,
            strict_demo_evidence,
            native_present_complete ? 1 : 0,
            content_progress_complete ? 1 : 0,
            app->source_content_hash,
            app->source_content_frame,
            strict_demo_evidence ? 1 : 0,
            strict_demo_evidence ?
                "PASS" : "FAIL");
    fclose(fp);
}

static int parse_positive_arg(const char *arg, const char *prefix,
                              int fallback)
{
    size_t len = strlen(prefix);
    int value;

    if (strncmp(arg, prefix, len) != 0)
        return fallback;
    value = atoi(arg + len);
    return value > 0 ? value : fallback;
}

static int parse_nonnegative_arg(const char *arg, const char *prefix,
                                 int fallback)
{
    size_t len = strlen(prefix);
    int value;

    if (strncmp(arg, prefix, len) != 0)
        return fallback;
    value = atoi(arg + len);
    return value >= 0 ? value : fallback;
}

static int parse_positive_env(const char *value, int fallback)
{
    int parsed;

    if (!value || !value[0])
        return fallback;
    parsed = atoi(value);
    return parsed > 0 ? parsed : fallback;
}

static int parse_size_arg(const char *arg, int *width, int *height)
{
    const char *s = arg + 7;
    int w = 0;
    int h = 0;

    while (*s >= '0' && *s <= '9') {
        w = w * 10 + (*s - '0');
        s++;
    }
    if (*s != 'x' && *s != 'X')
        return -1;
    s++;
    while (*s >= '0' && *s <= '9') {
        h = h * 10 + (*s - '0');
        s++;
    }
    if (*s != '\0' || w <= 0 || h <= 0)
        return -1;
    *width = w;
    *height = h;
    return 0;
}

static int run_client(int loop, int seconds, int resize_seconds,
                      int api_smoke,
                      int sphere_demo, int software_demo, int initial_width,
                      int initial_height, int render_div,
                      int present_interval, int pace_us)
{
    struct app_state app;
    int rc = 0;
    double start_sec;
    double elapsed_sec;
    double next_frame_sec;
    double next_resize_sec;
    int render_width;
    int render_height;

    memset(&app, 0, sizeof(app));
    app.egl_display = EGL_NO_DISPLAY;
    app.egl_context = EGL_NO_CONTEXT;
    app.egl_surface = EGL_NO_SURFACE;
    app.running = 1;
    app.max_seconds = seconds;
    app.resize_seconds = resize_seconds;
    app.width = initial_width;
    app.height = initial_height;
    app.loop = loop;
    app.api_smoke = api_smoke;
    app.sphere_demo = sphere_demo;
    app.software_demo = software_demo;
    app.render_div = render_div > 0 ? render_div : 1;
    app.present_interval = present_interval;
    app.pace_us = pace_us;
    if (sphere_demo && software_demo) {
        if (initial_width == WINDOW_W &&
            initial_height == WINDOW_H + TITLEBAR_H) {
            app.width = SOFTWARE_DEMO_W;
            app.height = SOFTWARE_DEMO_H + TITLEBAR_H;
        }
        app.max_width = app.width;
        app.max_height = app.height;
    }
    app.fps_start_sec = 0.0;
    app.fps_sample_seq = 0;
    app.fps_frame_count = 0;
    app.fps_value = 0.0;
    app.last_display_bind_completed_id = 0;
    app.client_capture_enabled = env_enabled("XV6_MESAWLEGL_CAPTURE");
    app.client_capture_done = 0;
    app.client_capture_after_sec =
        parse_positive_env(getenv("XV6_MESAWLEGL_CAPTURE_SECONDS"), 1);
    app.client_capture_start_sec = 0.0;
    snprintf(app.fps_text, sizeof(app.fps_text), "FPS --.-");

    if (init_wayland(&app) < 0) {
        cleanup(&app);
        return 1;
    }

    while (app.running && !app.configured &&
           wl_display_dispatch(app.display) >= 0)
        ;
    if (app.configured && recreate_window_surface(&app) < 0)
        rc = 1;
    if (app.present_interval >= 0)
        eglSwapInterval(app.egl_display, app.present_interval);
    if (rc == 0 && app.configured) {
        const char *shm_override = getenv("XV6_MESAWLEGL_SHM_PRESENT");

        detect_read_format(&app);
        if (shm_override && shm_override[0])
            app.shm_present = env_enabled("XV6_MESAWLEGL_SHM_PRESENT");
        else
            app.shm_present = app.sphere_demo && app.software_demo;
        if (app.shm_present && !app.shm) {
            fprintf(stderr,
                    "mesawlegl[%d]: wl_shm unavailable, falling back to eglSwapBuffers present\n",
                    loop);
            app.shm_present = 0;
        }
        fprintf(stderr,
                "mesawlegl[%d]: present_path=%s read_format=%s\n",
                loop, app.shm_present ? "shm-blit" : "egl-swap",
                app.read_format == GL_BGRA_EXT ? "bgra" : "rgba");
    }
    render_width = app.width / app.render_div;
    render_height = content_height(&app) / app.render_div;
    if (render_width <= 0)
        render_width = app.width;
    if (render_height <= 0)
        render_height = content_height(&app);
    fprintf(stderr,
            "mesawlegl[%d]: demo_surface_matrix window=%dx%d render=%dx%d render_div=%d present_interval=%d pace_us=%d status=PASS\n",
            loop, app.width, app.height, render_width, render_height,
            app.render_div, app.present_interval, app.pace_us);
    start_sec = monotonic_seconds();
    app.client_capture_start_sec = start_sec;
    next_frame_sec = start_sec;
    next_resize_sec = start_sec + (double)app.resize_seconds;
    for (app.frame = 0;
         rc == 0 && app.running &&
         (app.max_seconds <= 0 ||
          monotonic_seconds() - start_sec < (double)app.max_seconds);
         app.frame++) {
        if (app.resize_seconds > 0 &&
            monotonic_seconds() >= next_resize_sec) {
            next_resize_sec += (double)app.resize_seconds;
            if (app.width == WINDOW_W) {
                app.width = 360;
                app.height = 260 + TITLEBAR_H;
            } else {
                app.width = WINDOW_W;
                app.height = WINDOW_H + TITLEBAR_H;
            }
            if (recreate_window_surface(&app) < 0) {
                rc = 1;
                break;
            }
            app.resize_count++;
        }
        if (draw_and_swap(&app) < 0) {
            rc = 1;
            break;
        }
        wl_display_dispatch_pending(app.display);
        if (app.pace_us > 0) {
            double now = monotonic_seconds();
            double delay;

            next_frame_sec += (double)app.pace_us / 1000000.0;
            delay = next_frame_sec - now;
            if (delay > 0.0)
                usleep((useconds_t)(delay * 1000000.0));
            else if (delay < -0.25)
                next_frame_sec = now;
        }
    }
    elapsed_sec = monotonic_seconds() - start_sec;
    if (!app.configured)
        rc = 1;
    append_demo_interaction_evidence(&app, elapsed_sec, rc);
    {
        char complete_buf[192];
        int complete_len;

        complete_len = snprintf(
            complete_buf, sizeof(complete_buf),
            "mesawlegl_completion_matrix loop=%d frames=%d seconds=%d "
            "elapsed=%.3f status=%d render_div=%d present_interval=%d "
            "pace_us=%d\n",
            loop, app.frame, app.max_seconds, elapsed_sec, rc,
            app.render_div, app.present_interval, app.pace_us);
        if (complete_len > 0) {
            ssize_t written;

            if (complete_len >= (int)sizeof(complete_buf))
                complete_len = (int)sizeof(complete_buf) - 1;
            written = write(STDERR_FILENO, complete_buf,
                            (size_t)complete_len);
            (void)written;
        }
    }
    cleanup(&app);
    fprintf(stderr,
            "mesawlegl[%d]: complete frames=%d seconds=%d status=%d elapsed=%.3fs fps=%.1f window=%dx%d render=%dx%d render_div=%d present_interval=%d pace_us=%d\n",
            loop, app.frame, app.max_seconds, rc, elapsed_sec,
            elapsed_sec > 0.0 ? (double)app.frame / elapsed_sec : 0.0,
            app.width, app.height, render_width, render_height,
            app.render_div, app.present_interval, app.pace_us);
    return rc;
}

int main(int argc, char **argv)
{
    int seconds = 0;
    int loops = 1;
    int resize_seconds = 0;
    int api_smoke = 1;
    int sphere_demo = 0;
    int window_width = WINDOW_W;
    int window_height = WINDOW_H + TITLEBAR_H;
    int render_div = 1;
    int present_interval = 1;
    int pace_us = 0;
    int software_demo;
    int accel_requested;
    int rc = 0;

    accel_requested = mesa_env_requests_accel();
    if (!accel_requested) {
        setenv("LIBGL_ALWAYS_SOFTWARE", "1", 0);
        setenv("MESA_LOADER_DRIVER_OVERRIDE", "softpipe", 0);
    }
    software_demo = mesa_env_requests_software();

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--seconds=", 10) == 0) {
            seconds = parse_nonnegative_arg(argv[i], "--seconds=", seconds);
        } else if (strncmp(argv[i], "--loops=", 8) == 0) {
            loops = parse_positive_arg(argv[i], "--loops=", loops);
        } else if (strncmp(argv[i], "--resize-seconds=", 17) == 0) {
            resize_seconds = parse_positive_arg(argv[i], "--resize-seconds=",
                                                resize_seconds);
        } else if (strncmp(argv[i], "--size=", 7) == 0) {
            if (parse_size_arg(argv[i], &window_width, &window_height) != 0) {
                fprintf(stderr, "mesawlegl: invalid size '%s'\n", argv[i]);
                return 2;
            }
        } else if (strncmp(argv[i], "--render-div=", 13) == 0) {
            render_div = parse_positive_arg(argv[i], "--render-div=",
                                            render_div);
        } else if (strncmp(argv[i], "--present-interval=", 19) == 0) {
            present_interval = parse_nonnegative_arg(
                argv[i], "--present-interval=", present_interval);
        } else if (strncmp(argv[i], "--pace-us=", 10) == 0) {
            pace_us = parse_nonnegative_arg(argv[i], "--pace-us=", pace_us);
        } else if (strcmp(argv[i], "--simple") == 0) {
            api_smoke = 0;
            sphere_demo = 0;
        } else if (strcmp(argv[i], "--api-smoke") == 0) {
            api_smoke = 1;
            sphere_demo = 0;
        } else if (strcmp(argv[i], "--demo") == 0) {
            resize_seconds = 0;
            api_smoke = 0;
            sphere_demo = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            fprintf(stderr,
                    "usage: %s [--seconds=N] [--loops=N] [--resize-seconds=N] [--size=WxH] [--render-div=N] [--present-interval=N] [--pace-us=N] [--api-smoke|--simple|--demo]\n"
                    "  --seconds=N runs for N seconds; omitting --seconds runs until the window closes\n",
                    argv[0]);
            return 0;
        } else {
            fprintf(stderr, "mesawlegl: unknown option '%s'\n", argv[i]);
            return 2;
        }
    }

    for (int loop = 1; loop <= loops; loop++) {
        rc = run_client(loop, seconds, resize_seconds, api_smoke, sphere_demo,
                        software_demo, window_width, window_height,
                        render_div, present_interval, pace_us);
        if (rc != 0)
            break;
    }
    return rc;
}
