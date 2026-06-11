#ifndef XV6_TITLEBAR_H
#define XV6_TITLEBAR_H

#include <stdint.h>

struct xdg_toplevel;

#define XV6_TITLEBAR_HEIGHT 30
#define XV6_TITLEBAR_CONTROL_W 34

enum xv6_titlebar_action {
    XV6_TITLEBAR_NONE = 0,
    XV6_TITLEBAR_DRAG,
    XV6_TITLEBAR_MINIMIZE,
    XV6_TITLEBAR_MAXIMIZE,
    XV6_TITLEBAR_CLOSE,
};

struct xv6_titlebar_palette {
    uint32_t bg;
    uint32_t border;
    uint32_t button_bg;
    uint32_t close_bg;
    uint32_t fg;
};

extern const struct xv6_titlebar_palette xv6_titlebar_palette_dark;

enum xv6_titlebar_action xv6_titlebar_hit_test(int width, int x, int y);
void xv6_titlebar_activate(enum xv6_titlebar_action action,
                           struct xdg_toplevel *toplevel,
                           int *maximized, int *running);
void xv6_titlebar_draw_stride(uint32_t *fb, int width, int height, int stride,
                              const char *title, int maximized,
                              const struct xv6_titlebar_palette *palette);
void xv6_titlebar_draw(uint32_t *fb, int width, int height,
                       const char *title, int maximized,
                       const struct xv6_titlebar_palette *palette);

#endif
