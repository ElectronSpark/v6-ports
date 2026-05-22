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

struct netconf_req_compat {
    int mode;
    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns;
    char hostname[NETCONF_HOSTNAME_MAX];
};

struct webkit_gpu_contract {
    int have_backend;
    int virgl_opengl;
    int render_node;
    int dxg_transport;
    int d3dkmt;
    int opengl_submit;
    int validated_shared_surface;
    int shared_surface;
    int d3d12_present;
    int d3d12_contract_evidence;
    int d3d12_same_adapter;
    int d3d12_no_readback;
    int d3d12_shared_resource;
    int d3d12_fence;
    uint64_t d3d12_present_complete;
    uint64_t d3d12_release_fence;
};

static volatile sig_atomic_t g_running = 1;
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
static int webkit_contract_wait_ms_from_cmdline(int fallback);
static int gpu_validate_enabled_by_cmdline(void);
static int desktop_disabled_by_cmdline(void);
static int desktop_exit_after_smoke_by_cmdline(void);
static void xv6_webkit_gpu_contract(struct webkit_gpu_contract *contract);
static int webkit_gpu_contract_allows_accel(
    const struct webkit_gpu_contract *contract);
static void webkit_wait_for_gpu_contract(int wait_ms);
static long long monotonic_ms(void);
static int cmdline_int_value(const char *cmdline, const char *key,
                             int fallback);
static int read_cmdline(char *buf, size_t buf_size);
static int write_all_fd(int fd, const void *buf, size_t len);
static const char *http_content_type_for_path(const char *path);
static int http_try_serve_webkit_file(int cfd, const char *path,
                                      const char *extra);
static void http_smoke_self_probe(const char *path);
static void webkit_print_runtime_probe(void);
static void write_webkit_gpu_policy_file(const char *name, int requested_accel,
                                         int effective_accel,
                                         const struct webkit_gpu_contract *c,
                                         int dmabuf_requested,
                                         int dmabuf_effective);

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

static int webkit_evidence_key_u64(const char *text, const char *key,
                                   uint64_t *out)
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

static int webkit_evidence_key_string(const char *text, const char *key,
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

static int xv6_d3d12_present_evidence_fresh(void)
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

static int webkit_read_file(const char *path, char *buf, size_t buf_size)
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

static void xv6_webkit_d3d12_present_evidence(
    struct webkit_gpu_contract *contract)
{
    char evidence[2048];
    char source_luid[32];
    char matched_luid[32];
    uint64_t resource = 0;
    uint64_t allocations = 0;
    uint64_t fence = 0;
    uint64_t fence_target = 0;
    uint64_t release_fence = 0;
    uint64_t present_complete = 0;
    uint64_t cpu_readback = 1;
    uint64_t cpu_mapping = 1;
    uint64_t cpu_copy = 1;

    if (!contract ||
        !xv6_d3d12_present_evidence_fresh() ||
        !webkit_read_file(XV6_D3D12_PRESENT_EVIDENCE_PATH, evidence,
                          sizeof(evidence)))
        return;
    (void)webkit_evidence_key_u64(evidence, "d3d12_present_resource",
                                  &resource);
    (void)webkit_evidence_key_u64(evidence,
                                  "d3d12_present_allocation_count",
                                  &allocations);
    (void)webkit_evidence_key_u64(evidence, "d3d12_present_fence", &fence);
    (void)webkit_evidence_key_u64(evidence, "d3d12_present_fence_target",
                                  &fence_target);
    (void)webkit_evidence_key_u64(evidence, "d3d12_present_release_fence",
                                  &release_fence);
    (void)webkit_evidence_key_u64(evidence, "d3d12_gpu_present_complete",
                                  &present_complete);
    (void)webkit_evidence_key_u64(evidence, "d3d12_cpu_readback",
                                  &cpu_readback);
    (void)webkit_evidence_key_u64(evidence, "d3d12_cpu_mapping",
                                  &cpu_mapping);
    (void)webkit_evidence_key_u64(evidence, "d3d12_cpu_copy", &cpu_copy);
    contract->d3d12_same_adapter =
        webkit_evidence_key_string(evidence, "d3d12_present_luid",
                                   source_luid, sizeof(source_luid)) &&
        webkit_evidence_key_string(evidence, "d3d12_present_matched_luid",
                                   matched_luid, sizeof(matched_luid)) &&
        strcmp(source_luid, matched_luid) == 0;
    contract->d3d12_no_readback =
        cpu_readback == 0 && cpu_mapping == 0 && cpu_copy == 0;
    contract->d3d12_shared_resource =
        resource != 0 && allocations != 0;
    contract->d3d12_fence =
        fence != 0 && fence_target != 0 && release_fence != 0;
    contract->d3d12_present_complete = present_complete;
    contract->d3d12_release_fence = release_fence;
    contract->d3d12_contract_evidence =
        contract->d3d12_same_adapter &&
        contract->d3d12_no_readback &&
        contract->d3d12_shared_resource &&
        contract->d3d12_fence &&
        present_complete != 0;
}

static void xv6_webkit_gpu_contract(struct webkit_gpu_contract *contract)
{
    struct fb_gpu_backend_info_compat info;
    int virgl;
    int d3d12;

    if (!contract)
        return;
    memset(contract, 0, sizeof(*contract));
    if (!xv6_gpu_backend_info(&info))
        return;

    contract->have_backend = 1;
    contract->render_node =
        (info.flags & FB_GPU_BACKEND_F_RENDER_NODE) != 0;
    contract->dxg_transport =
        info.backend == FB_GPU_BACKEND_HYPERV_DXG &&
        (info.flags & FB_GPU_BACKEND_F_DXG_TRANSPORT) != 0;
    contract->d3dkmt =
        info.backend == FB_GPU_BACKEND_HYPERV_DXG &&
        (info.flags & FB_GPU_BACKEND_F_D3DKMT) != 0;
    contract->opengl_submit =
        (info.flags & FB_GPU_BACKEND_F_OPENGL_SUBMIT) != 0;
    virgl = info.backend == FB_GPU_BACKEND_VIRGL &&
            (info.flags & FB_GPU_BACKEND_F_VIRGL_OPENGL) != 0;
    contract->virgl_opengl = virgl;
    d3d12 = contract->dxg_transport && contract->d3dkmt;
    if (d3d12)
        xv6_webkit_d3d12_present_evidence(contract);
    contract->d3d12_present = d3d12 && contract->render_node &&
                              contract->opengl_submit &&
                              contract->d3d12_contract_evidence;
    contract->validated_shared_surface =
        contract->render_node && contract->opengl_submit &&
        (virgl || contract->d3d12_present);
    contract->shared_surface = contract->validated_shared_surface;
}

static int webkit_gpu_contract_allows_accel(
    const struct webkit_gpu_contract *contract)
{
    if (!contract || !contract->validated_shared_surface ||
        !contract->shared_surface || !contract->opengl_submit)
        return 0;
    if (contract->virgl_opengl)
        return 1;
    return contract->render_node && contract->dxg_transport &&
           contract->d3dkmt && contract->d3d12_present;
}

static void webkit_wait_for_gpu_contract(int wait_ms)
{
    long long deadline;
    long long next_log_ms = 0;
    struct webkit_gpu_contract contract;

    if (wait_ms <= 0)
        return;
    deadline = monotonic_ms() + wait_ms;
    do {
        long long now_ms = monotonic_ms();

        xv6_webkit_gpu_contract(&contract);
        if (webkit_gpu_contract_allows_accel(&contract)) {
            fprintf(stderr,
                    "[desktop] WebKit D3D12 shared-surface contract "
                    "evidence ready same_adapter=%d no_readback=%d "
                    "shared_resource=%d fence=%d complete=%lu "
                    "release=%lu\n",
                    contract.d3d12_same_adapter,
                    contract.d3d12_no_readback,
                    contract.d3d12_shared_resource,
                    contract.d3d12_fence,
                    (unsigned long)contract.d3d12_present_complete,
                    (unsigned long)contract.d3d12_release_fence);
            fflush(stderr);
            return;
        }
        if (!contract.dxg_transport || !contract.opengl_submit)
            return;
        if (contract.dxg_transport && !contract.d3d12_contract_evidence &&
            now_ms >= next_log_ms) {
            fprintf(stderr,
                    "[desktop] waiting for WebKit D3D12 shared-surface "
                    "contract evidence same_adapter=%d no_readback=%d "
                    "shared_resource=%d fence=%d complete=%lu\n",
                    contract.d3d12_same_adapter,
                    contract.d3d12_no_readback,
                    contract.d3d12_shared_resource,
                    contract.d3d12_fence,
                    (unsigned long)contract.d3d12_present_complete);
            fflush(stderr);
            next_log_ms = now_ms + 1000;
        }
        usleep(100000);
    } while (monotonic_ms() < deadline);
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
            "sh",
            "/bin/gpu-substrate-validate",
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

        execve("/bin/sh", argv, envp);
        fprintf(stderr, "gpu-substrate-validate: execve failed errno=%d (%s)\n",
                errno, errno ? strerror(errno) : "no errno from kernel");
        _exit(127);
    }
    if (pid > 0)
        setpgid(pid, pid);
    return pid;
}

#include "desktop_clients.inc"

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

static void write_webkit_gpu_policy_file(const char *name, int requested_accel,
                                         int effective_accel,
                                         const struct webkit_gpu_contract *c,
                                         int dmabuf_requested,
                                         int dmabuf_effective)
{
    const char *fallback = "none";
    const char *contract = "none";
    int d3d12_contract = 0;
    char buf[1200];
    int fd;
    int n;

    if (effective_accel && c && c->d3d12_present) {
        contract = "d3d12-shared-surface";
        d3d12_contract = 1;
    } else if (effective_accel && c && c->virgl_opengl) {
        contract = "virgl-opengl-submit";
    }

    if (requested_accel && !effective_accel && !dmabuf_effective) {
        if (!c || !c->opengl_submit)
            fallback = "opengl_submit_unavailable";
        else if (!c->render_node)
            fallback = "render_node_unavailable";
        else if (c->dxg_transport && !c->d3d12_contract_evidence)
            fallback = "d3d12_contract_evidence_unavailable";
        else
            fallback = "shared_surface_unavailable";
    } else if (dmabuf_requested && !dmabuf_effective &&
               (!c || !c->render_node)) {
        fallback = "render_node_unavailable";
    } else if (dmabuf_requested && !dmabuf_effective &&
               (!c || !c->opengl_submit)) {
        fallback = "opengl_submit_unavailable";
    } else if (dmabuf_requested && !dmabuf_effective &&
               (!c || !c->shared_surface)) {
        fallback = "shared_surface_unavailable";
    }

    n = snprintf(buf, sizeof(buf),
                 "webkit_gpu_policy name=%s requested_accel=%d "
                 "effective_accel=%d render_node=%d shared_surface=%d "
                 "validated_shared_surface=%d "
                 "d3d12_present=%d opengl_submit=%d dxg_transport=%d "
                 "d3dkmt=%d virgl_opengl=%d "
                 "d3d12_contract_evidence=%d d3d12_same_adapter=%d "
                 "d3d12_no_readback=%d d3d12_shared_resource=%d "
                 "d3d12_fence=%d d3d12_present_complete=%lu "
                 "d3d12_release_fence=%lu "
                 "dmabuf=%d requested_dmabuf=%d gpu_contract=%s "
                 "d3d12_native_present_required=%d "
                 "d3d12_copy_export=%d d3d12_readback=0 "
                 "fallback=%s\n",
                 name, requested_accel, effective_accel,
                 c ? c->render_node : 0,
                 c ? c->shared_surface : 0,
                 c ? c->validated_shared_surface : 0,
                 c ? c->d3d12_present : 0,
                 c ? c->opengl_submit : 0,
                 c ? c->dxg_transport : 0,
                 c ? c->d3dkmt : 0,
                 c ? c->virgl_opengl : 0,
                 c ? c->d3d12_contract_evidence : 0,
                 c ? c->d3d12_same_adapter : 0,
                 c ? c->d3d12_no_readback : 0,
                 c ? c->d3d12_shared_resource : 0,
                 c ? c->d3d12_fence : 0,
                 (unsigned long)(c ? c->d3d12_present_complete : 0),
                 (unsigned long)(c ? c->d3d12_release_fence : 0),
                 dmabuf_effective, dmabuf_requested, contract,
                 d3d12_contract, 0, fallback);
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

static int webkit_contract_wait_ms_from_cmdline(int fallback)
{
    char buf[512];
    int wait_ms = fallback;

    if (read_cmdline(buf, sizeof(buf)) == 0)
        wait_ms = cmdline_int_value(buf, "webkit_contract_wait_ms", wait_ms);
    if (wait_ms < 0)
        wait_ms = 0;
    if (wait_ms > 600000)
        wait_ms = 600000;
    return wait_ms;
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
        if (accel)
            webkit_wait_for_gpu_contract(
                webkit_contract_wait_ms_from_cmdline(0));
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
            if (exited > 0) {
                if (exited == wlcomp_pid) {
                    fprintf(stderr, "[desktop] wlcomp exited (status %d)\n",
                            WEXITSTATUS(status));
                    wlcomp_pid = 0;
                    break;
                }
                if (exited == client_pid) {
                    int ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;

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
            fprintf(stderr, "[desktop] shutting down\n");
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

    fprintf(stderr, "[desktop] shutting down\n");
    cleanup();
    return 0;
}
