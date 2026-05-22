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

    (void)pspec;
    (void)data;
    g_object_get(object, "title", &title, NULL);
    if (title) {
        fprintf(stderr, "webkitgpusmoke: title=%s\n", title);
        fflush(stderr);
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
    int fd;
    int ok;

    if (!info)
        return 0;
    memset(info, 0, sizeof(*info));
    fd = open(XV6_DRM_RENDER_NODE, O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return 0;
    ok = ioctl(fd, FB_GPU_BACKEND_QUERY, info) == 0;
    close(fd);
    return ok;
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
    char source_luid[32];
    char matched_luid[32];
    uint64_t evidence_resource = 0;
    uint64_t evidence_allocations = 0;
    uint64_t evidence_fence = 0;
    uint64_t evidence_fence_target = 0;
    uint64_t evidence_release_fence = 0;
    uint64_t evidence_present_complete = 0;
    uint64_t evidence_cpu_readback = 1;
    uint64_t evidence_cpu_mapping = 1;
    uint64_t evidence_cpu_copy = 1;
    int evidence_same_adapter = 0;
    int evidence_no_readback = 0;
    int evidence_shared_resource = 0;
    int evidence_fence_ok = 0;
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

    if (evidence) {
        (void)evidence_key_u64(evidence, "d3d12_present_resource",
                               &evidence_resource);
        (void)evidence_key_u64(evidence,
                               "d3d12_present_allocation_count",
                               &evidence_allocations);
        (void)evidence_key_u64(evidence, "d3d12_present_fence",
                               &evidence_fence);
        (void)evidence_key_u64(evidence, "d3d12_present_fence_target",
                               &evidence_fence_target);
        (void)evidence_key_u64(evidence, "d3d12_present_release_fence",
                               &evidence_release_fence);
        (void)evidence_key_u64(evidence, "d3d12_gpu_present_complete",
                               &evidence_present_complete);
        (void)evidence_key_u64(evidence, "d3d12_cpu_readback",
                               &evidence_cpu_readback);
        (void)evidence_key_u64(evidence, "d3d12_cpu_mapping",
                               &evidence_cpu_mapping);
        (void)evidence_key_u64(evidence, "d3d12_cpu_copy",
                               &evidence_cpu_copy);
        evidence_same_adapter =
            evidence_key_string(evidence, "d3d12_present_luid",
                                source_luid, sizeof(source_luid)) &&
            evidence_key_string(evidence, "d3d12_present_matched_luid",
                                matched_luid, sizeof(matched_luid)) &&
            strcmp(source_luid, matched_luid) == 0;
    }
    evidence_no_readback =
        evidence_cpu_readback == 0 && evidence_cpu_mapping == 0 &&
        evidence_cpu_copy == 0;
    evidence_shared_resource =
        evidence_resource != 0 && evidence_allocations != 0;
    evidence_fence_ok =
        evidence_fence != 0 && evidence_fence_target != 0 &&
        evidence_release_fence != 0;
    evidence_ok =
        evidence_same_adapter && evidence_no_readback &&
        evidence_shared_resource && evidence_fence_ok &&
        evidence_present_complete != 0;
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
    if (env_d3d12 && (!evidence_ok || !evidence_same_adapter ||
                      !evidence_no_readback || !evidence_shared_resource ||
                      !evidence_fence_ok))
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
            "d3d12_release_fence=%lu "
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
            (unsigned long)evidence_release_fence,
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

int main(int argc, char **argv)
{
    const char *uri = argc > 1 ? argv[1] : "file:///share/webkit/gpu-smoke.html";
    int timeout_ms = 0;
    if (argc > 2) {
        timeout_ms = atoi(argv[2]);
    }

    phase("start");
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
    g_signal_connect(view, "notify::title", G_CALLBACK(title_changed_cb), NULL);
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
    fprintf(stderr, "webkitgpusmoke: complete uri=%s timeout_ms=%d\n", uri, timeout_ms);
    return 0;
}
