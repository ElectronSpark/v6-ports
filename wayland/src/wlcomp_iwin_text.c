#include "wlcomp_iwin_text.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "wlcomp_draw.h"

#define NETCONF_MODE_DHCP   0
#define NETCONF_MODE_STATIC 1
#define NETCONF_HOSTNAME_MAX 32

struct netconf_req {
    int mode;
    unsigned int ip;
    unsigned int netmask;
    unsigned int gateway;
    unsigned int dns;
    char hostname[NETCONF_HOSTNAME_MAX];
};

static void format_ip4(char *buf, size_t bufsz, unsigned int ip)
{
    snprintf(buf, bufsz, "%u.%u.%u.%u",
             ip & 0xff,
             (ip >> 8) & 0xff,
             (ip >> 16) & 0xff,
             (ip >> 24) & 0xff);
}

static int write_resolv_conf_from_dns(unsigned int dns)
{
    char dns_buf[32];
    char body[96];
    int fd;
    int len;

    if (dns == 0)
        return -1;

    format_ip4(dns_buf, sizeof(dns_buf), dns);
    len = snprintf(body, sizeof(body), "nameserver %s\n", dns_buf);
    if (len <= 0 || (size_t)len >= sizeof(body))
        return -1;

    fd = open("/etc/resolv.conf", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        fprintf(stderr,
                "wlcomp: failed to open /etc/resolv.conf errno=%d (%s)\n",
                errno, strerror(errno));
        return -1;
    }
    if (write(fd, body, (size_t)len) != len) {
        fprintf(stderr,
                "wlcomp: failed to write /etc/resolv.conf errno=%d (%s)\n",
                errno, strerror(errno));
        close(fd);
        return -1;
    }
    close(fd);
    fprintf(stderr, "wlcomp: resolv.conf DNS %s\n", dns_buf);
    return 0;
}

int wlcomp_sync_resolv_conf_from_netconf(int wait_us)
{
    const int step_us = 250000;
    int waited = 0;

    while (waited <= wait_us) {
        struct netconf_req req;
        int fd = open("/dev/netconf", O_RDONLY | O_CLOEXEC);

        if (fd >= 0) {
            int n = read(fd, &req, sizeof(req));

            close(fd);
            if (n == (int)sizeof(req) && req.ip != 0 && req.dns != 0)
                return write_resolv_conf_from_dns(req.dns);
        }
        if (waited >= wait_us)
            break;
        usleep(step_us);
        waited += step_us;
    }

    fprintf(stderr,
            "wlcomp: network DNS not ready after %d ms; keeping existing "
            "/etc/resolv.conf\n",
            wait_us / 1000);
    return -1;
}

static const char *netconf_mode_name(int mode)
{
    if (mode == NETCONF_MODE_DHCP)
        return "DHCP";
    if (mode == NETCONF_MODE_STATIC)
        return "Static";
    return "Unknown";
}

void wlcomp_text_fill_sysinfo(iwin_t *w, uint32_t fb_w, uint32_t fb_h)
{
    w->text_len = 0;
    char *p = w->text;
    int rem = (int)sizeof(w->text) - 1;
    int n;

    n = snprintf(p, rem, "=== xv6 System Information ===\n\n");
    p += n; rem -= n;

    {
        char buf[128] = "";
        int fd = open("/proc/version", O_RDONLY);
        if (fd >= 0) {
            int r = read(fd, buf, sizeof(buf) - 1);
            if (r > 0) buf[r] = '\0';
            close(fd);
        }
        n = snprintf(p, rem, "Kernel: %s\n", buf[0] ? buf : "xv6 (unknown)");
        p += n; rem -= n;
    }

    {
        char buf[64] = "";
        int fd = open("/proc/uptime", O_RDONLY);
        if (fd >= 0) {
            int r = read(fd, buf, sizeof(buf) - 1);
            if (r > 0) buf[r] = '\0';
            close(fd);
        }
        n = snprintf(p, rem, "Uptime: %s\n", buf[0] ? buf : "unknown");
        p += n; rem -= n;
    }

    {
        char buf[512] = "";
        int fd = open("/proc/meminfo", O_RDONLY);
        if (fd >= 0) {
            int r = read(fd, buf, sizeof(buf) - 1);
            if (r > 0) buf[r] = '\0';
            close(fd);
        }
        n = snprintf(p, rem, "\n--- Memory ---\n%s\n", buf[0] ? buf : "unavailable");
        p += n; rem -= n;
    }

    {
        char buf[512] = "";
        int fd = open("/proc/cpuinfo", O_RDONLY);
        if (fd >= 0) {
            int r = read(fd, buf, sizeof(buf) - 1);
            if (r > 0) buf[r] = '\0';
            close(fd);
        }
        n = snprintf(p, rem, "--- CPU ---\n%s\n", buf[0] ? buf : "unavailable");
        p += n; rem -= n;
    }

    n = snprintf(p, rem, "--- Display ---\n%ux%u framebuffer\n",
                 fb_w, fb_h);
    p += n; rem -= n;

    w->text_len = (int)(p - w->text);
}

void wlcomp_text_fill_network(iwin_t *w, uint32_t now_ms)
{
    w->text_len = 0;
    char *p = w->text;
    int rem = (int)sizeof(w->text) - 1;
    int n;

    n = snprintf(p, rem, "=== Network Information ===\n\n");
    p += n; rem -= n;

    int fd = open("/dev/netconf", O_RDONLY);
    if (fd < 0) {
        n = snprintf(p, rem, "Network device: unavailable\n");
        p += n; rem -= n;
        w->text_len = (int)(p - w->text);
        w->last_refresh = now_ms;
        return;
    }

    struct netconf_req req;
    int r = read(fd, &req, sizeof(req));
    close(fd);
    if (r != (int)sizeof(req)) {
        n = snprintf(p, rem, "Network status: waiting for lwIP\n");
        p += n; rem -= n;
        w->text_len = (int)(p - w->text);
        w->last_refresh = now_ms;
        return;
    }

    char ip[16], mask[16], gw[16], dns[16];
    format_ip4(ip, sizeof(ip), req.ip);
    format_ip4(mask, sizeof(mask), req.netmask);
    format_ip4(gw, sizeof(gw), req.gateway);
    format_ip4(dns, sizeof(dns), req.dns);

    n = snprintf(p, rem,
                 "Interface: e1000\n"
                 "Mode:      %s\n"
                 "Hostname:  %s\n"
                 "IPv4:      %s\n"
                 "Netmask:   %s\n"
                 "Gateway:   %s\n"
                 "DNS:       %s\n\n"
                 "(auto-refreshes every 2s)\n",
                 netconf_mode_name(req.mode),
                 req.hostname[0] ? req.hostname : "xv6",
                 req.ip ? ip : "not assigned",
                 req.netmask ? mask : "not assigned",
                 req.gateway ? gw : "not assigned",
                 req.dns ? dns : "not assigned");
    p += n; rem -= n;

    w->text_len = (int)(p - w->text);
    w->last_refresh = now_ms;
}

void wlcomp_text_fill_files(iwin_t *w)
{
    w->text_len = 0;
    char *p = w->text;
    int rem = (int)sizeof(w->text) - 1;
    int n;

    n = snprintf(p, rem, "=== File Manager ===\n\nDirectory: /\n\n");
    p += n; rem -= n;

    int pipefd[2];
    if (pipe(pipefd) == 0) {
        pid_t pid = fork();
        if (pid == 0) {
            close(pipefd[0]);
            dup2(pipefd[1], 1);
            close(pipefd[1]);
            char *argv[] = { "ls", "-la", "/", NULL };
            char *envp[] = { "PATH=/bin", NULL };
            execve("/bin/ls", argv, envp);
            _exit(1);
        }
        close(pipefd[1]);
        if (pid > 0) {
            int total = 0;
            while (total < rem - 1) {
                int r = read(pipefd[0], p + total, rem - 1 - total);
                if (r <= 0) break;
                total += r;
            }
            p[total] = '\0';
            p += total;
            rem -= total;
            waitpid(pid, NULL, 0);
        }
        close(pipefd[0]);
    }

    w->text_len = (int)(p - w->text);
}

void wlcomp_text_fill_monitor(iwin_t *w, uint32_t now_ms)
{
    w->text_len = 0;
    char *p = w->text;
    int rem = (int)sizeof(w->text) - 1;
    int n;

    n = snprintf(p, rem, "=== System Monitor ===\n\n");
    p += n; rem -= n;

    {
        char buf[512] = "";
        int fd = open("/proc/meminfo", O_RDONLY);
        if (fd >= 0) {
            int r = read(fd, buf, sizeof(buf) - 1);
            if (r > 0) buf[r] = '\0';
            close(fd);
        }
        n = snprintf(p, rem, "--- Memory ---\n%s\n", buf[0] ? buf : "unavailable");
        p += n; rem -= n;
    }

    {
        char buf[64] = "";
        int fd = open("/proc/uptime", O_RDONLY);
        if (fd >= 0) {
            int r = read(fd, buf, sizeof(buf) - 1);
            if (r > 0) buf[r] = '\0';
            close(fd);
        }
        n = snprintf(p, rem, "Uptime: %s\n", buf[0] ? buf : "unknown");
        p += n; rem -= n;
    }

    {
        char buf[1024] = "";
        int fd = open("/proc/stat", O_RDONLY);
        if (fd >= 0) {
            int r = read(fd, buf, sizeof(buf) - 1);
            if (r > 0) buf[r] = '\0';
            close(fd);
        }
        n = snprintf(p, rem, "--- CPU ---\n%s\n", buf[0] ? buf : "unavailable");
        p += n; rem -= n;
    }

    n = snprintf(p, rem, "(auto-refreshes every 2s)\n");
    p += n; rem -= n;

    w->text_len = (int)(p - w->text);
    w->last_refresh = now_ms;
}

void wlcomp_text_fill_settings(iwin_t *w, uint32_t fb_w, uint32_t fb_h)
{
    int n = snprintf(w->text, sizeof(w->text),
                     "=== Settings ===\n\n"
                     "Display: %ux%u\n"
                     "Color depth: 32 bpp\n\n"
                     "Compositor: wlcomp (Wayland)\n"
                     "Font: 8x16 bitmap\n\n"
                     "--- Resolution ---\n"
                     "(click a button below)\n",
                     fb_w, fb_h);
    w->text_len = n;
}

void wlcomp_settings_draw(uint32_t *fb, int fb_w, int fb_h, iwin_t *w)
{
    wlcomp_text_draw(fb, fb_w, fb_h, w);

    int cx0 = w->x + 4;
    int cy0 = w->y + IWIN_TITLE_H + 2;
    int by = cy0 + 170;

    static const struct { int w, h; } res_modes[] = {
        {800,500}, {960,600}, {1024,640}, {1024,768},
        {1280,720}, {1280,800}, {1280,1024}, {1920,1080},
    };

    for (size_t i = 0; i < sizeof(res_modes) / sizeof(res_modes[0]); i++) {
        int bx = cx0 + 8 + (i % 3) * 140;
        int bby = by + (i / 3) * 32;
        uint32_t bg = 0xFF2A3040;
        if (res_modes[i].w == fb_w && res_modes[i].h == fb_h)
            bg = 0xFF2C4F7C;
        draw_rounded_rect(fb, fb_w, fb_h, bx, bby, 130, 26, 4, bg);
        char label[20];
        snprintf(label, sizeof(label), "%dx%d", res_modes[i].w, res_modes[i].h);
        int lw = string_pixel_width(label, 1);
        draw_string(fb, fb_w, fb_h, bx + (130 - lw) / 2, bby + 5,
                    label, 0xFFD2DAE2, 1);
    }
}

void wlcomp_text_draw(uint32_t *fb, int fb_w, int fb_h, iwin_t *w)
{
    int cx0 = w->x + 4;
    int cy0 = w->y + IWIN_TITLE_H + 2;
    int content_w = w->w - 8;
    int content_h = w->h - IWIN_TITLE_H - 4;

    draw_rect(fb, fb_w, fb_h, cx0, cy0, content_w, content_h, 0xFF141820);

    int max_rows = content_h / 16;
    int max_cols = content_w / 8;
    int line = 0;
    int col = 0;
    int skip = w->text_scroll;

    for (int i = 0; i < w->text_len && line - skip < max_rows; i++) {
        char c = w->text[i];
        if (c == '\n') {
            line++;
            col = 0;
            continue;
        }
        if (line >= skip && col < max_cols) {
            int px = cx0 + 4 + col * 8;
            int py = cy0 + 4 + (line - skip) * 16;
            if (c > ' ' && c <= '~')
                draw_char(fb, fb_w, fb_h, px, py, c, 0xFFD2DAE2, 1);
        }
        col++;
    }
}
