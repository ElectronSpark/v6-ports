/*
 * desktop.c — xv6 Wayland Session Manager
 *
 * Launches the wlcomp Wayland compositor, then starts Wayland clients
 * (e.g. NetSurf browser).  Monitors child processes and performs clean
 * shutdown on SIGTERM / SIGINT.
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
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#define WAYLAND_SOCKET_PATH  "/tmp/wayland-0.lock" /* lockfile (real VFS file) */
#define SOCKET_WAIT_TRIES    200      /* 200 × 20 ms = 4 s */
#define SOCKET_WAIT_US       20000
#define WEBKIT_NET_WAIT_US   35000000 /* DHCP fallback/network daemons need ~30s */
#define WEBKIT_DEFAULT_URL   "https://www.google.com/search?q=xv6&gbv=1"

static volatile sig_atomic_t g_running = 1;
static pid_t wlcomp_pid;
static pid_t client_pid;
static pid_t httpd_pid;

static int webkit_accel_enabled_by_cmdline(void);
static int webkit_gpu_smoke_enabled_by_cmdline(void);
static int webkit_webgl_smoke_enabled_by_cmdline(void);
static int webkit_api_smoke_enabled_by_cmdline(void);
static int webkit_http_smoke_enabled_by_cmdline(void);
static int webkit_reopen_count_from_cmdline(void);
static int webkit_timeout_ms_from_cmdline(int fallback);
static int glsmoke_accel_enabled_by_cmdline(void);

static long long monotonic_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void sighandler(int sig)
{
    (void)sig;
    g_running = 0;
}

/* Wait for wlcomp to create the Wayland socket.  Returns 0 on success. */
static int wait_for_socket(void)
{
    struct stat st;
    for (int i = 0; i < SOCKET_WAIT_TRIES; i++) {
        if (stat(WAYLAND_SOCKET_PATH, &st) == 0)
            return 0;
        usleep(SOCKET_WAIT_US);
    }
    return -1;
}

static int url_needs_network_wait(const char *url)
{
    return url && (strncmp(url, "http://", 7) == 0 ||
                   strncmp(url, "https://", 8) == 0) &&
           strncmp(url, "http://127.0.0.1", 16) != 0 &&
           strncmp(url, "http://localhost", 16) != 0;
}

static pid_t launch_wlcomp(void)
{
    pid_t pid = fork();
    if (pid == 0) {
        char *argv[] = { "wlcomp", NULL };
        char *envp[] = {
            "HOME=/",
            "PATH=/bin:/usr/bin",
            "XDG_RUNTIME_DIR=/tmp",
            NULL
        };
        execve("/bin/wlcomp", argv, envp);
        _exit(127);
    }
    return pid;
}

static void run_http_smoke_server(void)
{
    static const char body[] =
        "<!doctype html><title>xv6 plain HTTP smoke</title>"
        "<h1>xv6 plain HTTP smoke</h1>\n";
    char header[160];
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    struct sockaddr_in addr;

    if (fd < 0)
        _exit(1);

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(18080);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(fd, 4) < 0)
        _exit(1);

    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/html\r\n"
             "Content-Length: %u\r\n"
             "Connection: close\r\n\r\n",
             (unsigned)strlen(body));

    for (;;) {
        char req[256];
        int cfd = accept(fd, NULL, NULL);
        if (cfd < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        read(cfd, req, sizeof(req));
        write(cfd, header, strlen(header));
        write(cfd, body, strlen(body));
        close(cfd);
    }
    close(fd);
    _exit(0);
}

static pid_t launch_http_smoke_server(void)
{
    pid_t pid = fork();

    if (pid == 0)
        run_http_smoke_server();
    return pid;
}

static pid_t launch_client(const char *path, const char *name, const char *arg1,
                           const char *arg2, const char *arg3)
{
    pid_t pid = fork();
    if (pid == 0) {
        setpgid(0, 0);

        int is_netsurf = strcmp(name, "netsurf") == 0;
        int is_minibrowser = strcmp(name, "MiniBrowser") == 0;
        int is_webkitgpusmoke = strcmp(name, "webkitgpusmoke") == 0;
        int is_mesa_gl = strcmp(name, "mesawlegl") == 0 ||
                         strcmp(name, "mesaglsmoke") == 0 ||
                         strcmp(name, "mesaeglinfo") == 0;
        int is_webkit = is_minibrowser || is_webkitgpusmoke;

        if (is_netsurf) {
            int logfd = open("/tmp/app_log.txt",
                             O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (logfd >= 0) {
                dup2(logfd, 1);
                dup2(logfd, 2);
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
        char *argv_minibrowser[] = {
            (char *)name,
            "--enable-webgl=false",
            "--enable-webaudio=false",
            "--enable-mediasource=false",
            "--enable-media-stream=false",
            "--enable-page-cache=false",
            "--enable-dns-prefetching=false",
            "--enable-offline-web-application-cache=false",
            (char *)(arg1 ? arg1 : "https://www.google.com/"),
            NULL,
        };
        char *argv_minibrowser_accel[] = {
            (char *)name,
            "--enable-webgl=false",
            "--enable-webaudio=false",
            "--enable-mediasource=false",
            "--enable-media-stream=false",
            "--enable-page-cache=false",
            "--enable-dns-prefetching=false",
            "--enable-offline-web-application-cache=false",
            (char *)(arg1 ? arg1 : "https://www.google.com/"),
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
            "GDK_DPI_SCALE=1.55",
            "XCURSOR_PATH=/share/icons",
            "XCURSOR_THEME=Adwaita",
            "SSL_CERT_FILE=/share/netsurf/ca-bundle",
            NULL
        };
        char *envp_minibrowser[] = {
            "HOME=/",
            "PATH=/bin:/usr/bin",
            "XDG_RUNTIME_DIR=/tmp",
            "XDG_CACHE_HOME=/tmp/.cache",
            "XDG_DATA_DIRS=/share:/usr/share",
            "WAYLAND_DISPLAY=wayland-0",
            "GDK_BACKEND=wayland",
            "GDK_DPI_SCALE=1.55",
            "XCURSOR_PATH=/share/icons",
            "XCURSOR_THEME=Adwaita",
            "SSL_CERT_FILE=/share/netsurf/ca-bundle",
            "GIO_MODULE_DIR=/lib/gio/modules",
            "GIO_USE_TLS=openssl",
            "XV6_GUI_SESSION=1",
            "WEBKIT_EXEC_PATH=/libexec/webkit2gtk-4.1",
            "WEBKIT_INJECTED_BUNDLE_PATH=/lib/webkit2gtk-4.1/injected-bundle",
            "WEBKIT_DISABLE_NETWORK_CACHE=1",
            "WEBKIT_DISABLE_COMPOSITING_MODE=1",
            "WEBKIT_XV6_DISABLE_COMPOSITING_UPDATE=1",
            "EPOXY_XV6_ALLOW_MISSING=1",
            "WEBKIT_XV6_SKIP_RULE_FEATURES=1",
            "WEBKIT_XV6_SKIP_INITIAL_EMPTY_RENDER=1",
            "SOUP_FORCE_HTTP1=1",
            "JSC_useJIT=0",
            "JSC_useBaselineJIT=0",
            "JSC_useDFGJIT=0",
            "JSC_useFTLJIT=0",
            "JSC_useRegExpJIT=0",
            "JSC_useDOMJIT=0",
            "JSC_useBBQJIT=0",
            "JSC_useOMGJIT=0",
            "JSC_useConcurrentJIT=0",
            "JSC_useConcurrentGC=0",
            "JSC_numberOfDFGCompilerThreads=1",
            "JSC_numberOfFTLCompilerThreads=1",
            "JSC_numberOfWasmCompilerThreads=1",
            "JSC_numberOfWorklistThreads=1",
            "JSC_numberOfGCMarkers=1",
            NULL
        };
        char *envp_minibrowser_accel[] = {
            "HOME=/",
            "PATH=/bin:/usr/bin",
            "XDG_RUNTIME_DIR=/tmp",
            "XDG_CACHE_HOME=/tmp/.cache",
            "XDG_DATA_HOME=/tmp/.local/share",
            "XDG_DATA_DIRS=/share:/usr/share",
            "WAYLAND_DISPLAY=wayland-0",
            "GDK_BACKEND=wayland",
            "GDK_DPI_SCALE=1.55",
            "XCURSOR_PATH=/share/icons",
            "XCURSOR_THEME=Adwaita",
            "SSL_CERT_FILE=/share/netsurf/ca-bundle",
            "GIO_MODULE_DIR=/lib/gio/modules",
            "GIO_USE_TLS=openssl",
            "XV6_GUI_SESSION=1",
            "WEBKIT_EXEC_PATH=/libexec/webkit2gtk-4.1",
            "WEBKIT_INJECTED_BUNDLE_PATH=/lib/webkit2gtk-4.1/injected-bundle",
            "WEBKIT_DISABLE_NETWORK_CACHE=1",
            "WEBKIT_DISABLE_COMPOSITING_MODE=1",
            "WEBKIT_XV6_DISABLE_COMPOSITING_UPDATE=1",
            "SOUP_FORCE_HTTP1=1",
            "LIBGL_ALWAYS_SOFTWARE=0",
            "MESA_LOADER_DRIVER_OVERRIDE=virpipe",
            "GALLIUM_DRIVER=virpipe",
            "ANGLE_DEFAULT_PLATFORM=gl",
            "EPOXY_XV6_ALLOW_MISSING=1",
            "WEBKIT_XV6_SKIP_RULE_FEATURES=1",
            "JSC_useJIT=0",
            "JSC_useBaselineJIT=0",
            "JSC_useDFGJIT=0",
            "JSC_useFTLJIT=0",
            "JSC_useRegExpJIT=0",
            "JSC_useDOMJIT=0",
            "JSC_useBBQJIT=0",
            "JSC_useOMGJIT=0",
            "JSC_useConcurrentJIT=0",
            "JSC_useConcurrentGC=0",
            "JSC_numberOfDFGCompilerThreads=1",
            "JSC_numberOfFTLCompilerThreads=1",
            "JSC_numberOfWasmCompilerThreads=1",
            "JSC_numberOfWorklistThreads=1",
            "JSC_numberOfGCMarkers=1",
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
            "MESA_LOADER_DRIVER_OVERRIDE=virpipe",
            "GALLIUM_DRIVER=virpipe",
            NULL
        };
        int minibrowser_accel =
            is_minibrowser && webkit_accel_enabled_by_cmdline();
        int webkit_accel =
            (is_minibrowser && minibrowser_accel) || is_webkitgpusmoke;
        if (is_webkitgpusmoke)
            webkit_accel = 0;
        execve(path,
               is_minibrowser ?
                    (minibrowser_accel ?
                         argv_minibrowser_accel : argv_minibrowser) :
                    argv_default,
               is_webkit ?
                    (webkit_accel ? envp_minibrowser_accel : envp_minibrowser) :
                    (is_mesa_gl && glsmoke_accel_enabled_by_cmdline() ?
                         envp_mesa_accel : envp_default));
        _exit(127);
    }
    if (pid > 0)
        setpgid(pid, pid);
    return pid;
}

static void kill_and_reap(pid_t *pidp)
{
    if (*pidp > 0) {
        kill(-*pidp, SIGTERM);
        kill(*pidp, SIGTERM);
        waitpid(*pidp, NULL, 0);
        *pidp = 0;
    }
}

static void cleanup(void)
{
    kill_and_reap(&client_pid);
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
    if (fd < 0) {
        fprintf(stderr, "[desktop] /proc/cmdline unavailable\n");
        return -1;
    }

    int n = read(fd, buf, buf_size - 1);
    close(fd);
    if (n <= 0) {
        fprintf(stderr, "[desktop] /proc/cmdline empty\n");
        return -1;
    }
    buf[n] = '\0';
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
    if (timeout_ms > 120000)
        timeout_ms = 120000;
    return timeout_ms;
}

static void webkit_url_from_cmdline(char *out, size_t out_size)
{
    char buf[512];
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
    if (webkit_http_smoke_enabled_by_cmdline()) {
        snprintf(out, out_size, "http://127.0.0.1:18080/");
        return;
    }

    if (read_cmdline(buf, sizeof(buf)) < 0) {
        snprintf(out, out_size, WEBKIT_DEFAULT_URL);
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
            if (out[0])
                return;
            break;
        }
        while (*p && *p != ' ' && *p != '\t' && *p != '\n')
            p++;
    }

    snprintf(out, out_size, WEBKIT_DEFAULT_URL);
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

static int glsmoke_accel_enabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return token_is_enabled(buf, "glsmoke_accel");
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
    signal(SIGINT,  sighandler);
    signal(SIGTERM, sighandler);

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

    /* 3. Launch the requested Wayland client. */
    if (glsmoke_enabled_by_cmdline()) {
        char frames_arg[32];
        char loops_arg[32];
        char resize_arg[32];
        int compat = glsmoke_compat_by_cmdline();
        int native = !compat && glsmoke_native_by_cmdline();
        int demo = !compat && glsmoke_demo_by_cmdline();
        const char *client_path = compat ? "/bin/glsmoke" :
                                  demo ? "/bin/mesaglsmoke" :
                                  native ? "/bin/mesawlegl" :
                                           "/bin/mesaglsmoke";
        const char *client_name = compat ? "glsmoke" :
                                          demo ? "mesaglsmoke" :
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
        fprintf(stderr, "[desktop] %s pid=%d %s %s %s\n", client_name,
                client_pid, demo ? "--demo" : frames_arg, loops_arg,
                demo ? "" : resize_arg);
    } else if (webkit_enabled_by_cmdline()) {
        int accel = webkit_accel_enabled_by_cmdline();
        int webkit_reopen_left = webkit_reopen_count_from_cmdline();
        int webkit_api_smoke = webkit_api_smoke_enabled_by_cmdline();
        int webkit_timeout_ms = webkit_timeout_ms_from_cmdline(
            webkit_api_smoke ? 15000 : 0);
        long long launch_ms = 0;
        const char *webkit_path = webkit_api_smoke ?
            "/bin/webkitgpusmoke" : "/libexec/webkit2gtk-4.1/MiniBrowser";
        const char *webkit_name = webkit_api_smoke ?
            "webkitgpusmoke" : "MiniBrowser";
        char webkit_timeout_arg[24];
        const char *webkit_timeout = NULL;
        char webkit_url[192];

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
        if (url_needs_network_wait(webkit_url)) {
            fprintf(stderr,
                    "[desktop] waiting for network before WebKit URL %s\n",
                    webkit_url);
            usleep(WEBKIT_NET_WAIT_US);
        }
        client_pid = launch_client(webkit_path, webkit_name, webkit_url,
                                   webkit_timeout, NULL);
        if (client_pid < 0) {
            perror("[desktop] fork WebKit");
            cleanup();
            return 1;
        }
        fprintf(stderr,
                "[desktop] %s pid=%d accel=%d reopen_left=%d timeout_ms=%d "
                "url=%s\n",
                webkit_name, client_pid, accel, webkit_reopen_left,
                webkit_timeout_ms, webkit_url);
        launch_ms = monotonic_ms();

        while (g_running) {
            int status;
            pid_t exited = waitpid(-1, &status, WNOHANG);
            if (exited > 0) {
                if (exited == wlcomp_pid) {
                    fprintf(stderr, "[desktop] wlcomp exited (status %d)\n",
                            WEXITSTATUS(status));
                    wlcomp_pid = 0;
                    break;
                }
                if (exited == client_pid) {
                    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                        fprintf(stderr,
                                "[desktop] client exited (status %d)\n",
                                WIFEXITED(status) ? WEXITSTATUS(status) :
                                                    status);
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
                        break;
                    }
                }
            }
            usleep(100000);
            if (webkit_timeout_ms > 0 && client_pid > 0 &&
                monotonic_ms() - launch_ms >= webkit_timeout_ms) {
                fprintf(stderr,
                        "[desktop] WebKit timeout reached, closing pid=%d\n",
                        client_pid);
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
                    break;
                }
            }
        }
        fprintf(stderr, "[desktop] shutting down\n");
        cleanup();
        return 0;
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
        if (exited > 0) {
            if (exited == wlcomp_pid) {
                fprintf(stderr, "[desktop] wlcomp exited (status %d)\n",
                        WEXITSTATUS(status));
                wlcomp_pid = 0;
                break;  /* compositor gone → session over */
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

    fprintf(stderr, "[desktop] shutting down\n");
    cleanup();
    return 0;
}
