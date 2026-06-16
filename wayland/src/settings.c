/*
 * settings.c - small native Wayland settings/status panel for xv6.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#include "xv6_draw.h"
#include "xv6_present_buffer.h"
#include "xv6_titlebar.h"
#include "xdg-shell-client-protocol.h"

#define APP_W 560
#define APP_H 480
#define MIN_W 480
#define MIN_H 460
#define FBIOGET_VSCREENINFO 0x4600
#define FBIOPUT_VSCREENINFO 0x4601
#define SETTINGS_SIOCADDRT 0x890B
#define SETTINGS_RTF_UP 0x0001
#define SETTINGS_RTF_GATEWAY 0x0002
#define MODE_CONFIRM_SECONDS 15

struct fb_bitfield_compat {
    uint32_t offset;
    uint32_t length;
    uint32_t msb_right;
};

struct fb_var_screeninfo_compat {
    uint32_t xres;
    uint32_t yres;
    uint32_t xres_virtual;
    uint32_t yres_virtual;
    uint32_t xoffset;
    uint32_t yoffset;
    uint32_t bits_per_pixel;
    uint32_t grayscale;
    struct fb_bitfield_compat red;
    struct fb_bitfield_compat green;
    struct fb_bitfield_compat blue;
    struct fb_bitfield_compat transp;
    uint32_t nonstd;
    uint32_t activate;
    uint32_t height;
    uint32_t width;
    uint32_t accel_flags;
    uint32_t pixclock;
    uint32_t left_margin;
    uint32_t right_margin;
    uint32_t upper_margin;
    uint32_t lower_margin;
    uint32_t hsync_len;
    uint32_t vsync_len;
    uint32_t sync;
    uint32_t vmode;
    uint32_t rotate;
    uint32_t colorspace;
    uint32_t reserved[4];
};

struct rtentry_compat {
    unsigned long rt_pad1;
    struct sockaddr rt_dst;
    struct sockaddr rt_gateway;
    struct sockaddr rt_genmask;
    unsigned short rt_flags;
    short rt_pad2;
    unsigned long rt_pad3;
    void *rt_pad4;
    short rt_metric;
    char *rt_dev;
    unsigned long rt_mtu;
    unsigned long rt_window;
    unsigned short rt_irtt;
};

enum net_state {
    NET_OFFLINE = 0,
    NET_LINK,
    NET_ONLINE,
};

struct status_snapshot {
    int fb_ok;
    uint32_t xres;
    uint32_t yres;
    uint32_t bpp;
    enum net_state net;
    char ifname[IFNAMSIZ];
    char ip[INET_ADDRSTRLEN];
    char dns[96];
    char network_note[128];
};

struct app {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_proxy *gpu_manager;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct wl_keyboard *keyboard;
    struct wl_surface *surface;
    struct xdg_wm_base *wm_base;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct xv6_present_buffer buffer;
    int width;
    int height;
    int pending_width;
    int pending_height;
    int configured;
    int running;
    int dirty;
    int maximized;
    int pointer_x;
    int pointer_y;
    int pointer_inside;
    int hovered_button;
    uint32_t mods;
    struct status_snapshot status;
    char message[160];
    int mode_pending;
    uint32_t mode_old_width;
    uint32_t mode_old_height;
    uint32_t mode_new_width;
    uint32_t mode_new_height;
    uint64_t mode_deadline_ms;
    int mode_last_remaining;
};

struct mode_preset {
    uint32_t width;
    uint32_t height;
};

struct button {
    int x;
    int y;
    int w;
    int h;
};

enum settings_button_id {
    SETTINGS_BUTTON_NONE = -1,
    SETTINGS_BUTTON_MODE0,
    SETTINGS_BUTTON_MODE1,
    SETTINGS_BUTTON_MODE2,
    SETTINGS_BUTTON_NETWORK_STATIC,
    SETTINGS_BUTTON_NETWORK_UP,
    SETTINGS_BUTTON_NETWORK_DOWN,
    SETTINGS_BUTTON_CONFIRM,
    SETTINGS_BUTTON_REVERT,
    SETTINGS_BUTTON_REFRESH,
};

static const struct mode_preset mode_presets[] = {
    { 1024, 640 },
    { 1280, 800 },
    { 1440, 900 },
};

static uint64_t now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static int seconds_remaining(uint64_t deadline, uint64_t now)
{
    uint64_t left;

    if (now >= deadline)
        return 0;
    left = deadline - now;
    return (int)((left + 999) / 1000);
}

static void draw_text_fit(uint32_t *fb, int w, int h, int x, int y,
                          int max_px, const char *text, uint32_t color)
{
    char tmp[192];
    int max_chars = max_px / 8;
    int len = (int)strlen(text);

    if (max_chars <= 0)
        return;
    if (len <= max_chars) {
        draw_string(fb, w, h, x, y, text, color, 1);
        return;
    }
    if (max_chars < 4)
        max_chars = 4;
    snprintf(tmp, sizeof(tmp), "%.*s...", max_chars - 3, text);
    draw_string(fb, w, h, x, y, tmp, color, 1);
}

static void read_fb_status(struct status_snapshot *st)
{
    struct fb_var_screeninfo_compat info;
    int fd;

    st->fb_ok = 0;
    st->xres = st->yres = st->bpp = 0;
    fd = open("/dev/fb0", O_RDONLY);
    if (fd < 0)
        return;
    memset(&info, 0, sizeof(info));
    if (ioctl(fd, FBIOGET_VSCREENINFO, &info) == 0 &&
        info.xres > 0 && info.yres > 0) {
        st->fb_ok = 1;
        st->xres = info.xres;
        st->yres = info.yres;
        st->bpp = info.bits_per_pixel;
    }
    close(fd);
}

static void read_dns_status(struct status_snapshot *st)
{
    FILE *f;
    char line[160];

    snprintf(st->dns, sizeof(st->dns), "unavailable");
    f = fopen("/etc/resolv.conf", "r");
    if (!f)
        return;
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        char *end;

        while (*p == ' ' || *p == '\t')
            p++;
        if (strncmp(p, "nameserver", 10) != 0)
            continue;
        p += 10;
        while (*p == ' ' || *p == '\t')
            p++;
        end = p;
        while (*end && *end != '\n' && *end != ' ' && *end != '\t')
            end++;
        *end = '\0';
        if (*p)
            snprintf(st->dns, sizeof(st->dns), "%s", p);
        break;
    }
    fclose(f);
}

static void read_network_status(struct status_snapshot *st)
{
    static const char *names[] = { "en0", "en1", "eth0" };
    int fd;

    st->net = NET_OFFLINE;
    st->ifname[0] = '\0';
    st->ip[0] = '\0';
    snprintf(st->network_note, sizeof(st->network_note),
             "No configured IPv4 interface is up.");

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        snprintf(st->network_note, sizeof(st->network_note),
                 "socket(AF_INET) failed: %s", strerror(errno));
        return;
    }

    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        struct ifreq ifr;
        int up;
        int running;

        memset(&ifr, 0, sizeof(ifr));
        snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", names[i]);
        if (ioctl(fd, SIOCGIFFLAGS, &ifr) != 0)
            continue;
        up = (ifr.ifr_flags & IFF_UP) != 0;
        running = (ifr.ifr_flags & IFF_RUNNING) != 0;
        if (!up)
            continue;
        snprintf(st->ifname, sizeof(st->ifname), "%s", names[i]);
        st->net = running ? NET_LINK : NET_OFFLINE;
        snprintf(st->network_note, sizeof(st->network_note),
                 "%s is up%s.", names[i], running ? " with link" : "");

        memset(&ifr, 0, sizeof(ifr));
        snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", names[i]);
        if (ioctl(fd, SIOCGIFADDR, &ifr) == 0) {
            struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_addr;

            if (sin->sin_family == AF_INET &&
                inet_ntop(AF_INET, &sin->sin_addr, st->ip,
                          sizeof(st->ip))) {
                st->net = NET_ONLINE;
                snprintf(st->network_note, sizeof(st->network_note),
                         "%s has IPv4 connectivity.", names[i]);
                break;
            }
        }
        if (st->net == NET_LINK)
            break;
    }
    close(fd);
}

static void refresh_status(struct app *app)
{
    memset(&app->status, 0, sizeof(app->status));
    read_fb_status(&app->status);
    read_network_status(&app->status);
    read_dns_status(&app->status);
    app->dirty = 1;
}

static int sockaddr_set_ipv4(struct sockaddr *sa, const char *addr)
{
    struct sockaddr_in *sin = (struct sockaddr_in *)sa;

    memset(sa, 0, sizeof(*sa));
    sin->sin_family = AF_INET;
    return inet_pton(AF_INET, addr, &sin->sin_addr) == 1 ? 0 : -1;
}

static int write_dns(const char *addr, char *err, size_t err_size)
{
    char body[96];
    int fd;
    int len;

    len = snprintf(body, sizeof(body), "nameserver %s\n", addr);
    if (len <= 0 || (size_t)len >= sizeof(body)) {
        snprintf(err, err_size, "DNS address is too long.");
        return -1;
    }
    fd = open("/etc/resolv.conf", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        snprintf(err, err_size, "open /etc/resolv.conf: %s", strerror(errno));
        return -1;
    }
    if (write(fd, body, (size_t)len) != len) {
        snprintf(err, err_size, "write /etc/resolv.conf: %s",
                 strerror(errno));
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

static int set_fb_mode(uint32_t width, uint32_t height, char *err,
                       size_t err_size)
{
    struct fb_var_screeninfo_compat info;
    int fd;

    fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) {
        snprintf(err, err_size, "open /dev/fb0: %s", strerror(errno));
        return -1;
    }
    memset(&info, 0, sizeof(info));
    if (ioctl(fd, FBIOGET_VSCREENINFO, &info) != 0) {
        snprintf(err, err_size, "read current mode: %s", strerror(errno));
        close(fd);
        return -1;
    }
    info.xres = width;
    info.yres = height;
    info.xres_virtual = width;
    info.yres_virtual = height;
    if (info.bits_per_pixel == 0)
        info.bits_per_pixel = 32;
    if (ioctl(fd, FBIOPUT_VSCREENINFO, &info) != 0) {
        snprintf(err, err_size, "set %ux%u: %s", width, height,
                 strerror(errno));
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

static void update_hovered_button(struct app *app);

static void clear_pending_mode(struct app *app)
{
    app->mode_pending = 0;
    app->mode_old_width = 0;
    app->mode_old_height = 0;
    app->mode_new_width = 0;
    app->mode_new_height = 0;
    app->mode_deadline_ms = 0;
    app->mode_last_remaining = -1;
}

static int start_pending_mode(struct app *app, uint32_t width, uint32_t height,
                              char *err, size_t err_size)
{
    uint32_t old_width;
    uint32_t old_height;

    if (!app->status.fb_ok) {
        snprintf(err, err_size, "current mode is unavailable");
        return -1;
    }
    if (app->mode_pending) {
        old_width = app->mode_old_width;
        old_height = app->mode_old_height;
    } else {
        old_width = app->status.xres;
        old_height = app->status.yres;
    }
    if (app->mode_pending && old_width == width && old_height == height) {
        if (set_fb_mode(width, height, err, err_size) != 0)
            return -1;
        clear_pending_mode(app);
        snprintf(app->message, sizeof(app->message),
                 "Resolution reverted to %ux%u.", width, height);
        return 0;
    }
    if (!app->mode_pending && old_width == width && old_height == height) {
        clear_pending_mode(app);
        snprintf(app->message, sizeof(app->message),
                 "Resolution is already %ux%u.", width, height);
        return 0;
    }
    if (set_fb_mode(width, height, err, err_size) != 0)
        return -1;
    app->mode_pending = 1;
    app->mode_old_width = old_width;
    app->mode_old_height = old_height;
    app->mode_new_width = width;
    app->mode_new_height = height;
    app->mode_deadline_ms = now_ms() + MODE_CONFIRM_SECONDS * 1000ULL;
    app->mode_last_remaining = MODE_CONFIRM_SECONDS + 1;
    snprintf(app->message, sizeof(app->message),
             "Confirm %ux%u within %d seconds or it will roll back.",
             width, height, MODE_CONFIRM_SECONDS);
    return 0;
}

static void confirm_pending_mode(struct app *app)
{
    if (!app->mode_pending)
        return;
    snprintf(app->message, sizeof(app->message),
             "Resolution %ux%u confirmed.", app->mode_new_width,
             app->mode_new_height);
    clear_pending_mode(app);
    update_hovered_button(app);
    refresh_status(app);
}

static void revert_pending_mode(struct app *app, const char *reason)
{
    char err[128];
    uint32_t old_width = app->mode_old_width;
    uint32_t old_height = app->mode_old_height;
    uint32_t new_width = app->mode_new_width;
    uint32_t new_height = app->mode_new_height;

    if (!app->mode_pending)
        return;
    if (set_fb_mode(old_width, old_height, err, sizeof(err)) == 0) {
        clear_pending_mode(app);
        snprintf(app->message, sizeof(app->message),
                 "%s: reverted from %ux%u to %ux%u.", reason,
                 new_width, new_height, old_width, old_height);
    } else {
        clear_pending_mode(app);
        snprintf(app->message, sizeof(app->message),
                 "%s: rollback to %ux%u failed: %s", reason,
                 old_width, old_height, err);
    }
    update_hovered_button(app);
    refresh_status(app);
}

static int pending_mode_timeout_ms(struct app *app)
{
    uint64_t now;
    int remaining;

    if (!app->mode_pending)
        return -1;
    now = now_ms();
    remaining = seconds_remaining(app->mode_deadline_ms, now);
    if (remaining <= 0) {
        revert_pending_mode(app, "Resolution not confirmed");
        return -1;
    }
    if (remaining != app->mode_last_remaining) {
        app->mode_last_remaining = remaining;
        snprintf(app->message, sizeof(app->message),
                 "Confirm %ux%u within %d seconds or it will roll back.",
                 app->mode_new_width, app->mode_new_height, remaining);
        app->dirty = 1;
    }
    return remaining > 1 ? 1000 : 200;
}

static int get_interface_flags(int fd, const char *name, short *flags)
{
    struct ifreq ifr;

    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", name);
    if (ioctl(fd, SIOCGIFFLAGS, &ifr) != 0)
        return -1;
    *flags = ifr.ifr_flags;
    return 0;
}

static int set_interface_flags(int fd, const char *name, short flags)
{
    struct ifreq ifr;

    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", name);
    ifr.ifr_flags = flags;
    return ioctl(fd, SIOCSIFFLAGS, &ifr);
}

static const char *pick_interface(int fd, const struct status_snapshot *st)
{
    static const char *names[] = { "en0", "en1", "eth0" };
    static char chosen[IFNAMSIZ];
    short flags;

    if (st->ifname[0]) {
        snprintf(chosen, sizeof(chosen), "%s", st->ifname);
        return chosen;
    }
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (get_interface_flags(fd, names[i], &flags) == 0) {
            snprintf(chosen, sizeof(chosen), "%s", names[i]);
            return chosen;
        }
    }
    return NULL;
}

static int set_interface_addr(int fd, const char *name, unsigned long cmd,
                              const char *addr)
{
    struct ifreq ifr;

    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", name);
    if (sockaddr_set_ipv4(&ifr.ifr_addr, addr) != 0)
        return -1;
    return ioctl(fd, cmd, &ifr);
}

static int set_default_route(int fd, const char *name, const char *gateway)
{
    struct rtentry_compat rt;

    memset(&rt, 0, sizeof(rt));
    if (sockaddr_set_ipv4(&rt.rt_dst, "0.0.0.0") != 0 ||
        sockaddr_set_ipv4(&rt.rt_genmask, "0.0.0.0") != 0 ||
        sockaddr_set_ipv4(&rt.rt_gateway, gateway) != 0)
        return -1;
    rt.rt_flags = SETTINGS_RTF_UP | SETTINGS_RTF_GATEWAY;
    rt.rt_dev = (char *)name;
    return ioctl(fd, SETTINGS_SIOCADDRT, &rt);
}

static int network_set_up(struct app *app, int up, char *err, size_t err_size)
{
    int fd;
    const char *name;
    short flags;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        snprintf(err, err_size, "socket(AF_INET): %s", strerror(errno));
        return -1;
    }
    name = pick_interface(fd, &app->status);
    if (!name) {
        snprintf(err, err_size, "No network interface found.");
        close(fd);
        return -1;
    }
    if (get_interface_flags(fd, name, &flags) != 0) {
        snprintf(err, err_size, "read %s flags: %s", name, strerror(errno));
        close(fd);
        return -1;
    }
    if (up)
        flags |= IFF_UP;
    else
        flags &= (short)~IFF_UP;
    if (set_interface_flags(fd, name, flags) != 0) {
        snprintf(err, err_size, "set %s %s: %s", name,
                 up ? "up" : "down", strerror(errno));
        close(fd);
        return -1;
    }
    close(fd);
    snprintf(app->message, sizeof(app->message), "%s is now %s.", name,
             up ? "up" : "down");
    return 0;
}

static int network_set_qemu_static(struct app *app, char *err, size_t err_size)
{
    int fd;
    const char *name;
    short flags;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        snprintf(err, err_size, "socket(AF_INET): %s", strerror(errno));
        return -1;
    }
    name = pick_interface(fd, &app->status);
    if (!name) {
        snprintf(err, err_size, "No network interface found.");
        close(fd);
        return -1;
    }
    if (set_interface_addr(fd, name, SIOCSIFADDR, "10.0.2.15") != 0) {
        snprintf(err, err_size, "set IPv4 on %s: %s", name, strerror(errno));
        close(fd);
        return -1;
    }
    if (set_interface_addr(fd, name, SIOCSIFNETMASK, "255.255.255.0") != 0) {
        snprintf(err, err_size, "set netmask on %s: %s", name,
                 strerror(errno));
        close(fd);
        return -1;
    }
    if (get_interface_flags(fd, name, &flags) == 0)
        (void)set_interface_flags(fd, name, flags | IFF_UP);
    if (set_default_route(fd, name, "10.0.2.2") != 0) {
        snprintf(err, err_size, "set default route: %s", strerror(errno));
        close(fd);
        return -1;
    }
    close(fd);
    if (write_dns("10.0.2.3", err, err_size) != 0)
        return -1;
    snprintf(app->message, sizeof(app->message),
             "%s set to 10.0.2.15, gateway 10.0.2.2, DNS 10.0.2.3.",
             name);
    return 0;
}

static const char *net_label(enum net_state state)
{
    switch (state) {
    case NET_ONLINE:
        return "connected";
    case NET_LINK:
        return "link, no IPv4";
    default:
        return "offline";
    }
}

static uint32_t net_color(enum net_state state)
{
    switch (state) {
    case NET_ONLINE:
        return 0xFF51D68A;
    case NET_LINK:
        return 0xFFE0B24D;
    default:
        return 0xFFE05C66;
    }
}

static void draw_panel(uint32_t *fb, int w, int h, int x, int y, int pw,
                       int ph, const char *title)
{
    draw_rounded_rect(fb, w, h, x, y, pw, ph, 7, 0xFF141B24);
    draw_rect(fb, w, h, x, y, pw, 28, 0xFF1F2C3A);
    draw_text_fit(fb, w, h, x + 12, y + 7, pw - 24, title, 0xFFE9F0F8);
}

static int button_hit(struct button b, int x, int y)
{
    return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

static uint32_t hover_color(uint32_t color)
{
    uint32_t a = color & 0xFF000000;
    uint32_t r = (color >> 16) & 0xff;
    uint32_t g = (color >> 8) & 0xff;
    uint32_t b = color & 0xff;

    r += (255 - r) / 5;
    g += (255 - g) / 5;
    b += (255 - b) / 5;
    return a | (r << 16) | (g << 8) | b;
}

static void draw_button(uint32_t *fb, int w, int h, struct button b,
                        const char *label, uint32_t bg, uint32_t fg,
                        int hovered)
{
    int text_w = (int)strlen(label) * 8;
    int text_x = b.x + (b.w - text_w) / 2;

    if (text_x < b.x + 8)
        text_x = b.x + 8;
    draw_rounded_rect(fb, w, h, b.x, b.y, b.w, b.h, 5,
                      hovered ? hover_color(bg) : bg);
    if (hovered) {
        draw_rect(fb, w, h, b.x + 5, b.y, b.w - 10, 1, 0x99FFFFFF);
        draw_rect(fb, w, h, b.x + 5, b.y + b.h - 1, b.w - 10, 1,
                  0x55FFFFFF);
        draw_rect(fb, w, h, b.x, b.y + 5, 1, b.h - 10, 0x66FFFFFF);
        draw_rect(fb, w, h, b.x + b.w - 1, b.y + 5, 1, b.h - 10,
                  0x44FFFFFF);
    }
    draw_text_fit(fb, w, h, text_x, b.y + 8, b.w - 16, label, fg);
}

static struct button refresh_button(const struct app *app)
{
    struct button b = { app->width - 112, app->height - 42, 90, 26 };
    return b;
}

static struct button confirm_button(const struct app *app)
{
    struct button b = { app->width - 324, app->height - 42, 94, 26 };
    return b;
}

static struct button revert_button(const struct app *app)
{
    struct button b = { app->width - 218, app->height - 42, 94, 26 };
    return b;
}

static struct button mode_button(int panel_x, int panel_y, int idx)
{
    struct button b = { panel_x + 16 + idx * 112, panel_y + 72, 100, 28 };
    return b;
}

static struct button network_button(int panel_x, int panel_y, int idx)
{
    struct button b = { panel_x + 16 + idx * 124, panel_y + 142, 112, 28 };
    return b;
}

static int hovered_button(const struct app *app)
{
    int margin = 18;
    int content_y = XV6_TITLEBAR_HEIGHT + 18;
    int network_y = content_y + 118 + 16;

    if (!app->pointer_inside)
        return SETTINGS_BUTTON_NONE;
    if (app->mode_pending &&
        button_hit(confirm_button(app), app->pointer_x, app->pointer_y))
        return SETTINGS_BUTTON_CONFIRM;
    if (app->mode_pending &&
        button_hit(revert_button(app), app->pointer_x, app->pointer_y))
        return SETTINGS_BUTTON_REVERT;
    if (button_hit(refresh_button(app), app->pointer_x, app->pointer_y))
        return SETTINGS_BUTTON_REFRESH;
    for (int i = 0; i < (int)(sizeof(mode_presets) / sizeof(mode_presets[0]));
         i++) {
        if (button_hit(mode_button(margin, content_y, i), app->pointer_x,
                       app->pointer_y))
            return SETTINGS_BUTTON_MODE0 + i;
    }
    for (int i = 0; i < 3; i++) {
        if (button_hit(network_button(margin, network_y, i), app->pointer_x,
                       app->pointer_y))
            return SETTINGS_BUTTON_NETWORK_STATIC + i;
    }
    return SETTINGS_BUTTON_NONE;
}

static void update_hovered_button(struct app *app)
{
    int next = hovered_button(app);

    if (next != app->hovered_button) {
        app->hovered_button = next;
        app->dirty = 1;
    }
}

static void draw_network_mark(uint32_t *fb, int w, int h, int x, int y,
                              enum net_state state)
{
    uint32_t color = net_color(state);

    draw_circle(fb, w, h, x + 16, y + 23, 2, color);
    draw_rect(fb, w, h, x + 9, y + 17, 14, 2, color);
    draw_rect(fb, w, h, x + 6, y + 12, 20, 2, color);
    draw_rect(fb, w, h, x + 3, y + 7, 26, 2, color);
    if (state == NET_OFFLINE) {
        for (int i = 0; i < 22; i++)
            draw_rect(fb, w, h, x + 5 + i, y + 5 + i, 2, 2, 0xFFFF8A8A);
    }
}

static void draw_app(struct app *app)
{
    uint32_t *fb = app->buffer.pixels;
    int w = app->width;
    int h = app->height;
    int margin = 18;
    int content_y = XV6_TITLEBAR_HEIGHT + 18;
    int panel_w = w - margin * 2;
    int panel_h = 118;
    char line[192];

    if (!fb)
        return;
    draw_rect(fb, w, h, 0, 0, w, h, 0xFF0E1319);
    xv6_titlebar_draw(fb, w, h, "Settings", app->maximized, NULL);

    draw_panel(fb, w, h, margin, content_y, panel_w, panel_h, "Resolution");
    if (app->status.fb_ok) {
        snprintf(line, sizeof(line), "Current mode: %ux%u, %u bpp",
                 app->status.xres, app->status.yres, app->status.bpp);
        draw_text_fit(fb, w, h, margin + 16, content_y + 42,
                      panel_w - 32, line, 0xFFCAD7E5);
    } else {
        draw_text_fit(fb, w, h, margin + 16, content_y + 42,
                      panel_w - 32, "Current mode unavailable from /dev/fb0.",
                      0xFFE0B24D);
    }
    for (size_t i = 0; i < sizeof(mode_presets) / sizeof(mode_presets[0]);
         i++) {
        struct button b = mode_button(margin, content_y, (int)i);

        snprintf(line, sizeof(line), "%ux%u", mode_presets[i].width,
                 mode_presets[i].height);
        draw_button(fb, w, h, b, line, 0xFF26384E, 0xFFFFFFFF,
                    app->hovered_button == SETTINGS_BUTTON_MODE0 + (int)i);
    }

    content_y += panel_h + 16;
    draw_panel(fb, w, h, margin, content_y, panel_w, 176, "Network");
    draw_network_mark(fb, w, h, margin + 15, content_y + 39,
                      app->status.net);
    snprintf(line, sizeof(line), "Status: %s%s%s",
             net_label(app->status.net),
             app->status.ifname[0] ? " on " : "",
             app->status.ifname[0] ? app->status.ifname : "");
    draw_text_fit(fb, w, h, margin + 58, content_y + 42, panel_w - 74,
                  line, 0xFFE7EEF7);
    snprintf(line, sizeof(line), "IPv4: %s",
             app->status.ip[0] ? app->status.ip : "none");
    draw_text_fit(fb, w, h, margin + 58, content_y + 66, panel_w - 74,
                  line, 0xFFCAD7E5);
    snprintf(line, sizeof(line), "DNS: %s", app->status.dns);
    draw_text_fit(fb, w, h, margin + 58, content_y + 90, panel_w - 74,
                  line, 0xFFCAD7E5);
    draw_text_fit(fb, w, h, margin + 16, content_y + 114, panel_w - 32,
                  app->status.network_note, 0xFF9BAFC5);
    draw_button(fb, w, h, network_button(margin, content_y, 0),
                "QEMU static", 0xFF26384E, 0xFFFFFFFF,
                app->hovered_button == SETTINGS_BUTTON_NETWORK_STATIC);
    draw_button(fb, w, h, network_button(margin, content_y, 1),
                "Link up", 0xFF244535, 0xFFFFFFFF,
                app->hovered_button == SETTINGS_BUTTON_NETWORK_UP);
    draw_button(fb, w, h, network_button(margin, content_y, 2),
                "Link down", 0xFF513039, 0xFFFFFFFF,
                app->hovered_button == SETTINGS_BUTTON_NETWORK_DOWN);

    draw_text_fit(fb, w, h, margin, h - 58, w - 36,
                  app->message[0] ? app->message :
                  "Changes apply immediately to xv6 runtime state.",
                  app->message[0] ? 0xFFE7EEF7 : 0xFF8091A5);
    if (app->mode_pending) {
        draw_button(fb, w, h, confirm_button(app), "Confirm", 0xFF285B3E,
                    0xFFFFFFFF,
                    app->hovered_button == SETTINGS_BUTTON_CONFIRM);
        draw_button(fb, w, h, revert_button(app), "Revert", 0xFF5B3741,
                    0xFFFFFFFF,
                    app->hovered_button == SETTINGS_BUTTON_REVERT);
    }
    draw_button(fb, w, h, refresh_button(app), "Refresh", 0xFF2C4058,
                0xFFFFFFFF,
                app->hovered_button == SETTINGS_BUTTON_REFRESH);
}

static void commit_frame(struct app *app)
{
    draw_app(app);
    wl_surface_attach(app->surface, app->buffer.wl_buffer, 0, 0);
    wl_surface_damage(app->surface, 0, 0, app->width, app->height);
    wl_surface_commit(app->surface);
    app->dirty = 0;
}

static int resize_buffer(struct app *app, int width, int height)
{
    if (width < MIN_W)
        width = MIN_W;
    if (height < MIN_H)
        height = MIN_H;
    if (width == app->width && height == app->height &&
        app->buffer.wl_buffer)
        return 0;
    xv6_present_buffer_destroy(&app->buffer);
    app->width = width;
    app->height = height;
    if (xv6_present_buffer_init(&app->buffer, app->width, app->height,
                                app->shm, app->gpu_manager) != 0)
        return -1;
    app->dirty = 1;
    return 0;
}

static void handle_click(struct app *app, uint32_t serial)
{
    enum xv6_titlebar_action action;
    int margin = 18;
    int content_y = XV6_TITLEBAR_HEIGHT + 18;
    int network_y;
    char err[128];

    action = xv6_titlebar_hit_test(app->width, app->pointer_x,
                                   app->pointer_y);
    if (action == XV6_TITLEBAR_DRAG) {
        xdg_toplevel_move(app->toplevel, app->seat, serial);
        return;
    }
    if (action != XV6_TITLEBAR_NONE) {
        xv6_titlebar_activate(action, app->toplevel, &app->maximized,
                              &app->running);
        app->dirty = 1;
        return;
    }
    if (app->mode_pending &&
        button_hit(confirm_button(app), app->pointer_x, app->pointer_y)) {
        confirm_pending_mode(app);
        return;
    }
    if (app->mode_pending &&
        button_hit(revert_button(app), app->pointer_x, app->pointer_y)) {
        revert_pending_mode(app, "Resolution change canceled");
        return;
    }
    for (size_t i = 0; i < sizeof(mode_presets) / sizeof(mode_presets[0]);
         i++) {
        if (button_hit(mode_button(margin, content_y, (int)i),
                       app->pointer_x, app->pointer_y)) {
            if (start_pending_mode(app, mode_presets[i].width,
                                   mode_presets[i].height, err,
                                   sizeof(err)) != 0) {
                snprintf(app->message, sizeof(app->message),
                         "Resolution change failed: %s", err);
            }
            refresh_status(app);
            return;
        }
    }

    network_y = content_y + 118 + 16;
    if (button_hit(network_button(margin, network_y, 0),
                   app->pointer_x, app->pointer_y)) {
        if (network_set_qemu_static(app, err, sizeof(err)) != 0)
            snprintf(app->message, sizeof(app->message),
                     "Network preset failed: %s", err);
        refresh_status(app);
        return;
    }
    if (button_hit(network_button(margin, network_y, 1),
                   app->pointer_x, app->pointer_y)) {
        if (network_set_up(app, 1, err, sizeof(err)) != 0)
            snprintf(app->message, sizeof(app->message),
                     "Link up failed: %s", err);
        refresh_status(app);
        return;
    }
    if (button_hit(network_button(margin, network_y, 2),
                   app->pointer_x, app->pointer_y)) {
        if (network_set_up(app, 0, err, sizeof(err)) != 0)
            snprintf(app->message, sizeof(app->message),
                     "Link down failed: %s", err);
        refresh_status(app);
        return;
    }
    if (button_hit(refresh_button(app), app->pointer_x, app->pointer_y))
        refresh_status(app);
}

static void handle_key(struct app *app, uint32_t key)
{
    switch (key) {
    case 1:
    case 16:
        app->running = 0;
        break;
    case 19:
    case 63:
        refresh_status(app);
        break;
    default:
        break;
    }
}

static void pointer_enter(void *data, struct wl_pointer *pointer,
                          uint32_t serial, struct wl_surface *surface,
                          wl_fixed_t sx, wl_fixed_t sy)
{
    struct app *app = data;
    (void)pointer;
    (void)serial;
    (void)surface;
    app->pointer_inside = 1;
    app->pointer_x = wl_fixed_to_int(sx);
    app->pointer_y = wl_fixed_to_int(sy);
    update_hovered_button(app);
}

static void pointer_leave(void *data, struct wl_pointer *pointer,
                          uint32_t serial, struct wl_surface *surface)
{
    struct app *app = data;
    (void)pointer;
    (void)serial;
    (void)surface;
    app->pointer_inside = 0;
    update_hovered_button(app);
}

static void pointer_motion(void *data, struct wl_pointer *pointer,
                           uint32_t time, wl_fixed_t sx, wl_fixed_t sy)
{
    struct app *app = data;
    (void)pointer;
    (void)time;
    app->pointer_x = wl_fixed_to_int(sx);
    app->pointer_y = wl_fixed_to_int(sy);
    update_hovered_button(app);
}

static void pointer_button(void *data, struct wl_pointer *pointer,
                           uint32_t serial, uint32_t time, uint32_t button,
                           uint32_t state)
{
    struct app *app = data;
    (void)pointer;
    (void)time;

    if (button == 0x110 && state == WL_POINTER_BUTTON_STATE_PRESSED)
        handle_click(app, serial);
}

static void pointer_axis(void *data, struct wl_pointer *pointer,
                         uint32_t time, uint32_t axis, wl_fixed_t value)
{
    (void)data;
    (void)pointer;
    (void)time;
    (void)axis;
    (void)value;
}

static void pointer_frame(void *data, struct wl_pointer *pointer)
{
    (void)data;
    (void)pointer;
}

static void pointer_axis_source(void *data, struct wl_pointer *pointer,
                                uint32_t axis_source)
{
    (void)data;
    (void)pointer;
    (void)axis_source;
}

static void pointer_axis_stop(void *data, struct wl_pointer *pointer,
                              uint32_t time, uint32_t axis)
{
    (void)data;
    (void)pointer;
    (void)time;
    (void)axis;
}

static void pointer_axis_discrete(void *data, struct wl_pointer *pointer,
                                  uint32_t axis, int32_t discrete)
{
    (void)data;
    (void)pointer;
    (void)axis;
    (void)discrete;
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

static void keyboard_keymap(void *data, struct wl_keyboard *keyboard,
                            uint32_t format, int32_t fd, uint32_t size)
{
    (void)data;
    (void)keyboard;
    (void)format;
    (void)size;
    if (fd >= 0)
        close(fd);
}

static void keyboard_enter(void *data, struct wl_keyboard *keyboard,
                           uint32_t serial, struct wl_surface *surface,
                           struct wl_array *keys)
{
    (void)data;
    (void)keyboard;
    (void)serial;
    (void)surface;
    (void)keys;
}

static void keyboard_leave(void *data, struct wl_keyboard *keyboard,
                           uint32_t serial, struct wl_surface *surface)
{
    (void)data;
    (void)keyboard;
    (void)serial;
    (void)surface;
}

static void keyboard_key(void *data, struct wl_keyboard *keyboard,
                         uint32_t serial, uint32_t time, uint32_t key,
                         uint32_t state)
{
    struct app *app = data;
    (void)keyboard;
    (void)serial;
    (void)time;

    if (state == WL_KEYBOARD_KEY_STATE_PRESSED)
        handle_key(app, key);
}

static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard,
                               uint32_t serial, uint32_t mods_depressed,
                               uint32_t mods_latched, uint32_t mods_locked,
                               uint32_t group)
{
    struct app *app = data;
    (void)keyboard;
    (void)serial;
    (void)mods_latched;
    (void)mods_locked;
    (void)group;
    app->mods = mods_depressed;
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *keyboard,
                                 int32_t rate, int32_t delay)
{
    (void)data;
    (void)keyboard;
    (void)rate;
    (void)delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

static void seat_capabilities(void *data, struct wl_seat *seat,
                              uint32_t capabilities)
{
    struct app *app = data;

    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !app->pointer) {
        app->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(app->pointer, &pointer_listener, app);
    }
    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !app->keyboard) {
        app->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(app->keyboard, &keyboard_listener, app);
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name)
{
    (void)data;
    (void)seat;
    (void)name;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

static void xdg_surface_configure(void *data, struct xdg_surface *surface,
                                  uint32_t serial)
{
    struct app *app = data;

    xdg_surface_ack_configure(surface, serial);
    if (app->pending_width > 0 && app->pending_height > 0) {
        if (resize_buffer(app, app->pending_width,
                          app->pending_height) != 0)
            app->running = 0;
        app->pending_width = 0;
        app->pending_height = 0;
    }
    app->configured = 1;
    commit_frame(app);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                               int32_t width, int32_t height,
                               struct wl_array *states)
{
    struct app *app = data;
    (void)toplevel;

    app->maximized = 0;
    if (states) {
        uint32_t *state;

        wl_array_for_each(state, states) {
            if (*state == XDG_TOPLEVEL_STATE_MAXIMIZED)
                app->maximized = 1;
        }
    }
    if (width > 0 && height > 0) {
        app->pending_width = width;
        app->pending_height = height;
    }
}

static void toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
    struct app *app = data;
    (void)toplevel;
    app->running = 0;
}

static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = toplevel_configure,
    .close = toplevel_close,
};

static void wm_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_listener = {
    .ping = wm_ping,
};

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface,
                            uint32_t version)
{
    struct app *app = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        app->compositor = wl_registry_bind(registry, name,
                                           &wl_compositor_interface,
                                           version > 4 ? 4 : version);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        app->shm = wl_registry_bind(registry, name, &wl_shm_interface,
                                    version > 1 ? 1 : version);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        app->seat = wl_registry_bind(registry, name, &wl_seat_interface,
                                     version > 5 ? 5 : version);
        wl_seat_add_listener(app->seat, &seat_listener, app);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        app->wm_base = wl_registry_bind(registry, name,
                                        &xdg_wm_base_interface,
                                        version > 2 ? 2 : version);
        xdg_wm_base_add_listener(app->wm_base, &wm_listener, app);
    } else if (strcmp(interface, xv6_gpu_buffer_manager_interface.name) == 0) {
        app->gpu_manager = wl_registry_bind(
            registry, name, &xv6_gpu_buffer_manager_interface,
            version > 3 ? 3 : version);
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

static int init_wayland(struct app *app)
{
    app->display = wl_display_connect(NULL);
    if (!app->display)
        return -1;
    app->registry = wl_display_get_registry(app->display);
    wl_registry_add_listener(app->registry, &registry_listener, app);
    wl_display_roundtrip(app->display);
    wl_display_roundtrip(app->display);

    if (!app->compositor || !app->wm_base || (!app->gpu_manager && !app->shm))
        return -1;

    app->surface = wl_compositor_create_surface(app->compositor);
    app->xdg_surface = xdg_wm_base_get_xdg_surface(app->wm_base, app->surface);
    xdg_surface_add_listener(app->xdg_surface, &xdg_surface_listener, app);
    app->toplevel = xdg_surface_get_toplevel(app->xdg_surface);
    xdg_toplevel_add_listener(app->toplevel, &toplevel_listener, app);
    xdg_toplevel_set_title(app->toplevel, "Settings");
    xdg_toplevel_set_app_id(app->toplevel, "xv6-settings");
    xdg_toplevel_set_min_size(app->toplevel, MIN_W, MIN_H);

    if (resize_buffer(app, app->width, app->height) != 0)
        return -1;
    wl_surface_commit(app->surface);
    return 0;
}

static void cleanup(struct app *app)
{
    if (app->display)
        wl_display_roundtrip(app->display);
    xv6_present_buffer_destroy(&app->buffer);
    if (app->keyboard)
        wl_keyboard_destroy(app->keyboard);
    if (app->pointer)
        wl_pointer_destroy(app->pointer);
    if (app->seat)
        wl_seat_destroy(app->seat);
    if (app->toplevel)
        xdg_toplevel_destroy(app->toplevel);
    if (app->xdg_surface)
        xdg_surface_destroy(app->xdg_surface);
    if (app->surface)
        wl_surface_destroy(app->surface);
    if (app->gpu_manager)
        wl_proxy_destroy(app->gpu_manager);
    if (app->wm_base)
        xdg_wm_base_destroy(app->wm_base);
    if (app->shm)
        wl_shm_destroy(app->shm);
    if (app->compositor)
        wl_compositor_destroy(app->compositor);
    if (app->registry)
        wl_registry_destroy(app->registry);
    if (app->display)
        wl_display_disconnect(app->display);
}

static void run_event_loop(struct app *app)
{
    int fd = wl_display_get_fd(app->display);

    while (app->running) {
        struct pollfd pfd;
        int timeout_ms;
        int ret;

        while ((ret = wl_display_dispatch_pending(app->display)) > 0)
            ;
        if (ret < 0)
            break;

        timeout_ms = pending_mode_timeout_ms(app);
        if (app->dirty && app->configured)
            commit_frame(app);
        if (wl_display_flush(app->display) < 0)
            break;

        while (wl_display_prepare_read(app->display) != 0) {
            ret = wl_display_dispatch_pending(app->display);
            if (ret < 0)
                return;
        }
        if (wl_display_flush(app->display) < 0) {
            wl_display_cancel_read(app->display);
            break;
        }

        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        ret = poll(&pfd, 1, timeout_ms);
        if (ret < 0) {
            wl_display_cancel_read(app->display);
            if (errno == EINTR)
                continue;
            break;
        }
        if (ret == 0) {
            wl_display_cancel_read(app->display);
            continue;
        }
        if (pfd.revents & (POLLIN | POLLHUP | POLLERR)) {
            if (wl_display_read_events(app->display) < 0)
                break;
        } else {
            wl_display_cancel_read(app->display);
        }
    }
}

int main(void)
{
    struct app app;

    memset(&app, 0, sizeof(app));
    app.width = APP_W;
    app.height = APP_H;
    app.running = 1;
    app.hovered_button = SETTINGS_BUTTON_NONE;
    app.buffer.fd = -1;
    app.buffer.fb_fd = -1;
    setenv("XDG_RUNTIME_DIR", "/tmp", 0);
    setenv("WAYLAND_DISPLAY", "wayland-0", 0);

    refresh_status(&app);
    if (init_wayland(&app) != 0) {
        fprintf(stderr, "xv6-settings: Wayland initialization failed\n");
        cleanup(&app);
        return 1;
    }

    run_event_loop(&app);
    if (app.mode_pending)
        revert_pending_mode(&app, "Settings closed before confirmation");

    cleanup(&app);
    return 0;
}
