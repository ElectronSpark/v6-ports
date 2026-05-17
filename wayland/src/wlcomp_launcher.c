#include "wlcomp_launcher.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "wlcomp_iwin_text.h"

#define MAX_CHILDREN 16
#define WEBKIT_NET_WAIT_US 35000000
#define WEBKIT_DEFAULT_URL "https://www.google.com/search?q=xv6&gbv=1"
#define XV6_DRM_RENDER_NODE "/dev/dri/renderD128"
#define FB_GPU_BACKEND_QUERY 0x462C
#define FB_GPU_BACKEND_HYPERV_DXG 2
#define FB_GPU_BACKEND_F_RENDER_NODE 0x0001
#define FB_GPU_BACKEND_F_DXG_TRANSPORT 0x0008
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

static int launcher_dxg_render_node_available(void)
{
    struct launcher_fb_gpu_backend_info info;
    int fd;
    int ok;

    memset(&info, 0, sizeof(info));
    fd = open(XV6_DRM_RENDER_NODE, O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return 0;
    ok = ioctl(fd, FB_GPU_BACKEND_QUERY, &info) == 0 &&
         info.backend == FB_GPU_BACKEND_HYPERV_DXG &&
         (info.flags & FB_GPU_BACKEND_F_RENDER_NODE) != 0 &&
         (info.flags & FB_GPU_BACKEND_F_DXG_TRANSPORT) != 0;
    close(fd);
    return ok;
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

void wlcomp_launcher_launch(const char *path, const char *name,
                            const char *arg)
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
        int is_mesa_gl = strcmp(app_name, "mesawlegl") == 0 ||
                         strcmp(app_name, "mesaglsmoke") == 0 ||
                         strcmp(app_name, "mesaeglinfo") == 0;

        if (is_webkit)
            disable_child_coredumps();

        if (!is_webkitgpusmoke) {
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

        if (is_minibrowser && url_needs_network_wait(arg)) {
            fprintf(stderr, "wlcomp: waiting for network before %s\n", arg);
            if (wlcomp_sync_resolv_conf_from_netconf(WEBKIT_NET_WAIT_US) != 0)
                usleep(WEBKIT_NET_WAIT_US);
        }

        char *argv_def[] = { (char *)app_name, (char *)arg, NULL };
        char *argv_noarg[] = { (char *)app_name, NULL };
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
            (char *)(arg ? arg : WEBKIT_DEFAULT_URL),
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
            (char *)(arg ? arg : WEBKIT_DEFAULT_URL),
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
            (char *)(arg ? arg : WEBKIT_DEFAULT_URL),
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
            (char *)(arg ? arg : WEBKIT_DEFAULT_URL),
            NULL,
        };
        char **argv = arg ? argv_def : argv_noarg;
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
            "WEBKIT_EXEC_PATH=/libexec/webkit2gtk-4.1",
            "WEBKIT_INJECTED_BUNDLE_PATH=/lib/webkit2gtk-4.1/injected-bundle",
            "WEBKIT_DISABLE_NETWORK_CACHE=1",
            "WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1",
            "LIBGL_ALWAYS_SOFTWARE=0",
            "LIBGL_DRIVERS_PATH=/lib/dri",
            "GALLIUM_DRIVER=virgl",
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
            "SOUP_FORCE_HTTP1=1",
            "EPOXY_XV6_ALLOW_MISSING=1",
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
            "GALLIUM_DRIVER=d3d12",
            "LIBGL_DRIVERS_PATH=/lib/dri",
            "EGL_PLATFORM=wayland",
            "XV6_MESA_WAYLAND_THROTTLE=0",
            "XV6_MESA_WAYLAND_XV6GPU=1",
            "XV6_MESA_PERF_LOG=0",
            "XV6_MESA_WAYLAND_INPLACE_PRESENT=1",
            "XV6_D3D12_PRESENT_INTERVAL=1",
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
            NULL
        };
        char **envp = envp_default;
        int virgl_available = launcher_virgl_available();
        int dxg_available = launcher_dxg_render_node_available();
        if (is_webkit) {
            int accel = launcher_cmdline_flag_enabled("webkit_accel");
            int js = launcher_cmdline_int_value("webkit_js", 1) != 0;
            if (accel && !virgl_available && is_minibrowser) {
                fprintf(stderr,
                        "wlcomp: WebKit acceleration requested, but virgl is "
                        "unavailable; using the stable WebKit compositor path\n");
                accel = 0;
            }
            if (is_minibrowser) {
                if (accel)
                    argv = js ? argv_minibrowser_accel_js :
                                 argv_minibrowser_accel;
                else
                    argv = js ? argv_minibrowser_js : argv_minibrowser;
            }
            if (is_webkitgpusmoke)
                accel = 0;
            envp = accel ?
                (virgl_available ? envp_minibrowser_accel :
                                   envp_minibrowser_accel_sw) :
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
