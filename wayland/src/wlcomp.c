/*
 * wlcomp.c — Minimal Wayland compositor for xv6.
 *
 * Direct framebuffer rendering via /dev/fb0 GPU ioctl.
 * PS/2 mouse (/dev/mouse) and keyboard (/dev/kbd) input.
 * Implements: wl_compositor, wl_shm, wl_seat, wl_output, xdg_wm_base.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#include <poll.h>
#include <time.h>
#include <dirent.h>
#include <stdarg.h>
#include <dlfcn.h>

#include <wayland/wayland-server-core.h>
#include <wayland/wayland-server-protocol.h>
#include <wayland/xdg-shell-server-protocol.h>
#include <wayland/linux-dmabuf-v1-server-protocol.h>
#include <wayland/linux-explicit-synchronization-unstable-v1-server-protocol.h>
#include <libdrm/drm_fourcc.h>

#include "wlcomp_desktop.h"
#include "wlcomp_draw.h"
#include "wlcomp_iwin.h"
#include "wlcomp_iwin_calc.h"
#include "wlcomp_iwin_demo.h"
#include "wlcomp_iwin_filemgr.h"
#include "wlcomp_iwin_terminal.h"
#include "wlcomp_iwin_text.h"
#include "wlcomp_keymap.h"
#include "wlcomp_launcher.h"
#include "wlcomp_swgl.h"

/* xv6-specific syscall numbers (not in musl headers) */
#define XV6_SYS_poweroff  166
#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif
#define XV6_DRM_RENDER_NODE "/dev/dri/renderD128"
#define DRM_IOCTL_VIRTGPU_GETPARAM 0xc0106443UL
#define VIRTGPU_PARAM_3D_FEATURES  1

struct drm_virtgpu_getparam_compat {
    uint64_t param;
    uint64_t value;
};


/* PTY/TTY ioctls used by the built-in terminal. */
#define XV6_TIOCGPGRP  0x540F
#define XV6_TIOCSCTTY  0x540E
#define XV6_TIOCSWINSZ 0x5414
#define XV6_TIOCGPTN   0x80045430

/* ══════════════════════════════════════════════════════════════════════
 *  Framebuffer
 * ══════════════════════════════════════════════════════════════════════ */

#define FBIOGET_VSCREENINFO  0x4600
#define FBIOPUT_VSCREENINFO  0x4601
#define FB_GPU_FILL_RECT     0x4610
#define FB_GPU_BLIT          0x4611
#define FB_GPU_BO_CREATE     0x4614
#define FB_GPU_BO_PRESENT    0x4615
#define FB_GPU_BO_DESTROY    0x4616
#define FB_GPU_BO_IMPORT     0x4617
#define FB_GPU_BO_FENCE      0x4618
#define FB_GPU_FENCE_EXPORT_FD 0x4624
#define FB_GPU_BO_IMPORT_FD  0x4623
#define FB_GPU_SCANOUT_MAP   0x4629
#define FB_GPU_SCANOUT_FLUSH 0x462A
#define FB_GPU_DISPLAY_WAIT  0x462D
#define FB_GPU_DISPLAY_WAIT_F_WAIT 0x1

#ifndef DRM_FORMAT_MOD_LINEAR
#define DRM_FORMAT_MOD_LINEAR 0
#endif
#define WLCOMP_INVALID_FORMAT UINT32_MAX
#define FB_GPU_BO_F_EXPORTABLE 0x1
#define MAX_DAMAGE_RECTS     32

struct fb_var_screeninfo {
    uint32_t xres, yres, bits_per_pixel, pitch;
};

struct fb_gpu_fill {
    uint32_t x, y, w, h, color;
};

struct fb_gpu_blit {
    uint32_t x, y, w, h, src_pitch;
    uint64_t pixels;
};

struct fb_gpu_bo_create {
    uint32_t width, height, flags, pitch;
    uint64_t size, addr;
    uint32_t handle, reserved;
};

struct fb_gpu_bo_present {
    uint32_t x, y, w, h, src_pitch;
    uint64_t pixels;
    uint32_t handle, flags;
    uint64_t fence;
};

struct fb_gpu_display_wait {
    uint32_t flags;
    uint32_t refresh_millihz;
    uint64_t wait_for;
    uint64_t presented;
    uint64_t completed;
};

struct fb_gpu_scanout_map {
    uint32_t width, height, pitch, reserved;
    uint64_t size, addr;
};

struct fb_gpu_scanout_flush {
    uint32_t x, y, w, h;
};

struct fb_gpu_bo_destroy {
    uint32_t handle, flags;
};

struct fb_gpu_bo_import {
    uint32_t handle, flags, width, height, pitch, reserved;
    uint64_t size, addr;
};

struct fb_gpu_bo_fence {
    uint32_t handle, flags;
    uint64_t wait_for;
    uint64_t signaled;
    uint64_t last_present;
};

struct fb_gpu_fence_export_fd {
    uint32_t handle, flags;
    uint64_t fence;
    int32_t fd;
    uint32_t reserved;
    uint64_t signaled;
};

struct fb_gpu_bo_import_fd {
    int32_t fd;
    uint32_t flags, width, height, pitch, handle;
    uint64_t size, addr;
};

/* ── Mouse event (matches kernel struct mouse_event) ──────────────── */

struct mouse_event {
    int16_t  dx, dy;
    uint8_t  buttons;
    uint8_t  flags;
    int8_t   dz;
    uint8_t  pad[1];
};
#define MOUSE_EVENT_F_ABSOLUTE 0x01

/* ── Kbd event (matches kernel struct kbd_event) ──────────────────── */

struct kbd_event {
    uint8_t keycode;
    uint8_t scancode;
    uint8_t pressed;
    uint8_t modifiers;
};

/* ══════════════════════════════════════════════════════════════════════
 *  Globals
 * ══════════════════════════════════════════════════════════════════════ */

static struct wl_display *g_display;

static int cmdline_flag_enabled(const char *key);
static int cmdline_int_value(const char *key, int fallback);
static uint32_t get_time_ms(void);
static uint64_t get_time_us(void);
static int framebuffer_display_completion_query(uint64_t wait_for,
                                                int wait,
                                                uint64_t *presented,
                                                uint64_t *completed);

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

static void wlcomp_wayland_log(const char *fmt, va_list args)
{
    char buf[256];

    vsnprintf(buf, sizeof(buf), fmt, args);
    if (strstr(buf, "failed to read client connection") != NULL)
        return;
    fputs(buf, stderr);
}

static uint64_t get_time_us(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL +
           (uint64_t)ts.tv_nsec / 1000ULL;
}

#include "wlcomp_fb.inc"

#include "wlcomp_surface_state.inc"

#include "wlcomp_buffer_shm.inc"

#include "wlcomp_dmabuf.inc"

#include "wlcomp_gpu_sync.inc"

#include "wlcomp_compositor_subsurface.inc"

#include "wlcomp_xdg.inc"

#include "wlcomp_seat_output.inc"

#include "wlcomp_desktop_iwin.inc"

#include "wlcomp_render_loop.inc"

#include "wlcomp_input_init.inc"

/* ══════════════════════════════════════════════════════════════════════
 *  Main
 * ══════════════════════════════════════════════════════════════════════ */

static void sig_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    wl_log_set_handler_server(wlcomp_wayland_log);

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGCHLD, SIG_DFL);

    fprintf(stderr, "wlcomp: starting Wayland compositor\n");

    wl_list_init(&g_surfaces);

    if (init_framebuffer() < 0)
        return 1;
    init_input();
    {
        const struct wlcomp_launcher_ops launcher_ops = {
            .get_time_ms = get_time_ms,
            .cmdline_flag_enabled = cmdline_flag_enabled,
            .cmdline_int_value = cmdline_int_value,
            .virgl_available = xv6_virgl_available,
            .destroy_surfaces_for_pid = destroy_surfaces_for_pid,
        };

        wlcomp_launcher_init(&launcher_ops);
    }
    damage_full_reason(FULL_DAMAGE_INIT);

    /* Create Wayland display */
    g_display = wl_display_create();
    if (!g_display) {
        fprintf(stderr, "wlcomp: wl_display_create failed\n");
        return 1;
    }
    wl_display_set_default_max_buffer_size(g_display,
                                           WAYLAND_CLIENT_BUFFER_LIMIT);

    int virgl_available = xv6_virgl_available();
    int dmabuf_enabled = virgl_available ||
        cmdline_flag_enabled("wayland_dmabuf");

    /* Register globals */
    g_compositor_global = wl_global_create(g_display, &wl_compositor_interface,
                                           5, NULL, compositor_bind);
    g_subcompositor_global = wl_global_create(g_display,
                                              &wl_subcompositor_interface,
                                              1, NULL, subcompositor_bind);
    g_shm_global = wl_global_create(g_display, &wl_shm_interface,
                                    1, NULL, shm_bind);
    g_seat_global = wl_global_create(g_display, &wl_seat_interface,
                                     3, NULL, seat_bind);
    g_output_global = wl_global_create(g_display, &wl_output_interface,
                                       3, NULL, output_bind);
    g_xdg_wm_global = wl_global_create(g_display, &xdg_wm_base_interface,
                                        2, NULL, xdg_wm_bind);
    g_ddm_global = wl_global_create(g_display, &wl_data_device_manager_interface,
                                    3, NULL, ddm_bind);
    g_xv6_gpu_global = wl_global_create(g_display,
                                        &xv6_gpu_buffer_manager_interface,
                                        5, NULL, xv6_gpu_bind);
    if (dmabuf_enabled) {
        g_dmabuf_global = wl_global_create(g_display,
                                           &zwp_linux_dmabuf_v1_interface,
                                           4, NULL, dmabuf_bind);
        g_explicit_sync_global = wl_global_create(
            g_display, &zwp_linux_explicit_synchronization_v1_interface,
            2, NULL, explicit_sync_bind);
        fprintf(stderr, "wlcomp: linux-dmabuf enabled (%s)\n",
                virgl_available ? "virgl" : "cmdline");
    } else {
        fprintf(stderr, "wlcomp: linux-dmabuf disabled (no virgl)\n");
    }

    /* Add socket */
    if (wl_display_add_socket(g_display, "wayland-0") < 0) {
        fprintf(stderr, "wlcomp: wl_display_add_socket failed\n");
        return 1;
    }
    fprintf(stderr, "wlcomp: listening on wayland-0\n");

    /* Get the wayland event loop fd for polling */
    struct wl_event_loop *loop = wl_display_get_event_loop(g_display);
    int wl_fd = wl_event_loop_get_fd(loop);

    /* Set up epoll: monitor wayland fd + mouse + kbd */
    int epfd = epoll_create1(0);
    if (epfd < 0) {
        fprintf(stderr, "wlcomp: epoll_create1: %s\n", strerror(errno));
        return 1;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = wl_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, wl_fd, &ev);

    if (g_mouse_fd >= 0) {
        ev.data.fd = g_mouse_fd;
        epoll_ctl(epfd, EPOLL_CTL_ADD, g_mouse_fd, &ev);
    }
    if (g_kbd_fd >= 0) {
        ev.data.fd = g_kbd_fd;
        epoll_ctl(epfd, EPOLL_CTL_ADD, g_kbd_fd, &ev);
    }

    if (cmdline_flag_enabled("demo3d")) {
        open_3ddemo();
        fprintf(stderr, "wlcomp: opened OpenGL Demo from demo3d=1\n");
    }

    /* Main compositor loop */
    fprintf(stderr, "wlcomp: entering main loop\n");

    while (g_running) {
        struct epoll_event events[8];
        int nready;

        wl_display_flush_clients(g_display);
        wl_event_loop_dispatch(loop, 0);

        nready = epoll_wait(epfd, events, 8,
                            any_frame_callbacks_pending() ? 4 : 16);
        for (int i = 0; i < nready; i++) {
            if (events[i].data.fd == wl_fd)
                wl_event_loop_dispatch(loop, 0);
        }

        /*
         * Input devices are opened O_NONBLOCK and their cdev reads return
         * -EAGAIN when empty. Poll them once per frame instead of relying on
         * epoll readiness; this keeps pointer motion alive even if a wakeup is
         * coalesced or missed during heavy WebKit/GL repaint.
         */
        if (g_mouse_fd >= 0)
            process_mouse();
        if (g_kbd_fd >= 0)
            process_keyboard();

        /* Process terminal PTY output and auto-refresh monitors */
        process_terminals();
        poll_framebuffer_mode();

        /* Clients may be launched by desktop, not wlcomp; clean their stale
         * Wayland resources once their owner process has gone away. */
        destroy_dead_client_surfaces();

        /* Composite and present */
        composite_and_flip();

        /* Reap terminated child processes */
        reap_children();

        /* Flush events to clients */
        wl_display_flush_clients(g_display);
    }

    fprintf(stderr, "wlcomp: shutting down\n");
    wl_display_destroy(g_display);
    release_framebuffer_backing();
    if (g_fb_fd >= 0) close(g_fb_fd);
    if (g_mouse_fd >= 0) close(g_mouse_fd);
    if (g_kbd_fd >= 0) close(g_kbd_fd);
    close(epfd);

    return 0;
}
