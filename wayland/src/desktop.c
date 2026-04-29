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
#include <fcntl.h>

#define WAYLAND_SOCKET_PATH  "/tmp/wayland-0.lock" /* lockfile (real VFS file) */
#define SOCKET_WAIT_TRIES    200      /* 200 × 20 ms = 4 s */
#define SOCKET_WAIT_US       20000

static volatile sig_atomic_t g_running = 1;
static pid_t wlcomp_pid;
static pid_t client_pid;

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

static pid_t launch_client(const char *path, const char *name, const char *arg1,
                           const char *arg2)
{
    pid_t pid = fork();
    if (pid == 0) {
        int is_netsurf = strcmp(name, "netsurf") == 0;
        int is_minibrowser = strcmp(name, "MiniBrowser") == 0;

        if (is_netsurf || is_minibrowser) {
            int logfd = open("/tmp/app_log.txt",
                             O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (logfd >= 0) {
                dup2(logfd, 1);
                dup2(logfd, 2);
                close(logfd);
            }
        }

        if (is_netsurf) {
            mkdir("/.netsurf", 0755);
            mkdir("/tmp/.cache", 0755);
            mkdir("/tmp/.cache/fontconfig", 0755);
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

        char *argv_default[] = { (char *)name, (char *)arg1, (char *)arg2,
                                 NULL };
        if (arg1 == NULL)
            argv_default[1] = NULL;
        if (arg2 == NULL)
            argv_default[2] = NULL;
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
        char *envp_default[] = {
            "HOME=/",
            "PATH=/bin:/usr/bin",
            "XDG_RUNTIME_DIR=/tmp",
            "XDG_CACHE_HOME=/tmp/.cache",
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
            "WAYLAND_DISPLAY=wayland-0",
            "GDK_BACKEND=wayland",
            "GDK_DPI_SCALE=1.55",
            "XCURSOR_PATH=/share/icons",
            "XCURSOR_THEME=Adwaita",
            "SSL_CERT_FILE=/share/netsurf/ca-bundle",
            "GIO_MODULE_DIR=/lib/gio/modules",
            "GIO_USE_TLS=openssl",
            "WEBKIT_EXEC_PATH=/libexec/webkit2gtk-4.1",
            "WEBKIT_INJECTED_BUNDLE_PATH=/lib/webkit2gtk-4.1/injected-bundle",
            "WEBKIT_DISABLE_NETWORK_CACHE=1",
            "WEBKIT_DISABLE_COMPOSITING_MODE=1",
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
        execve(path,
               is_minibrowser ? argv_minibrowser : argv_default,
               is_minibrowser ? envp_minibrowser : envp_default);
        _exit(127);
    }
    return pid;
}

static void kill_and_reap(pid_t *pidp)
{
    if (*pidp > 0) {
        kill(*pidp, SIGTERM);
        waitpid(*pidp, NULL, 0);
        *pidp = 0;
    }
}

static void cleanup(void)
{
    kill_and_reap(&client_pid);
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

static int glsmoke_enabled_by_cmdline(void)
{
    char buf[512];

    if (read_cmdline(buf, sizeof(buf)) < 0)
        return 0;

    return !token_is_disabled(buf, "glsmoke") &&
           strstr(buf, "glsmoke=1") != NULL;
}

static void glsmoke_args_from_cmdline(char *frames_arg, size_t frames_size,
                                      char *loops_arg, size_t loops_size)
{
    char buf[512];
    int frames = 120;
    int loops = 1;

    if (read_cmdline(buf, sizeof(buf)) == 0) {
        frames = cmdline_int_value(buf, "glsmoke_frames", frames);
        loops = cmdline_int_value(buf, "glsmoke_loops", loops);
    }
    snprintf(frames_arg, frames_size, "--frames=%d", frames);
    snprintf(loops_arg, loops_size, "--loops=%d", loops);
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

        glsmoke_args_from_cmdline(frames_arg, sizeof(frames_arg), loops_arg,
                                  sizeof(loops_arg));
        client_pid = launch_client("/bin/glsmoke", "glsmoke", frames_arg,
                                   loops_arg);
        if (client_pid < 0) {
            perror("[desktop] fork glsmoke");
            cleanup();
            return 1;
        }
        fprintf(stderr, "[desktop] glsmoke pid=%d %s %s\n", client_pid,
                frames_arg, loops_arg);
    } else if (webkit_enabled_by_cmdline()) {
        client_pid = launch_client("/libexec/webkit2gtk-4.1/MiniBrowser",
                                   "MiniBrowser",
                                   "https://www.google.com/", NULL);
        if (client_pid < 0) {
            perror("[desktop] fork MiniBrowser");
            cleanup();
            return 1;
        }
        fprintf(stderr, "[desktop] MiniBrowser pid=%d\n", client_pid);
    } else if (netsurf_disabled_by_cmdline()) {
        client_pid = 0;
        fprintf(stderr, "[desktop] netsurf disabled by cmdline\n");
    } else {
        client_pid = launch_client("/bin/netsurf", "netsurf", NULL, NULL);
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
