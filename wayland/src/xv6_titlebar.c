#include "xv6_titlebar.h"

#include "font8x16.h"
#include "xdg-shell-client-protocol.h"

#include <stddef.h>
#include <stdint.h>

const struct xv6_titlebar_palette xv6_titlebar_palette_dark = {
    .bg = 0xff1b2836u,
    .border = 0xff5f7183u,
    .button_bg = 0xff26384bu,
    .close_bg = 0xff78343au,
    .fg = 0xfff3f7fbu,
};

static int text_width(const char *s)
{
    int len = 0;

    if (!s)
        return 0;
    while (*s++) {
        len++;
    }
    return len * 8;
}

static void draw_rect_stride(uint32_t *fb, int width, int height, int stride,
                             int x, int y, int w, int h, uint32_t color)
{
    int x0 = x;
    int y0 = y;
    int x1 = x + w;
    int y1 = y + h;

    if (!fb || w <= 0 || h <= 0 || stride < width * 4)
        return;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > width)
        x1 = width;
    if (y1 > height)
        y1 = height;
    if (x0 >= x1 || y0 >= y1)
        return;
    for (int row = y0; row < y1; row++) {
        uint32_t *dst = (uint32_t *)((uint8_t *)fb +
                                     (size_t)row * (size_t)stride);

        for (int col = x0; col < x1; col++)
            dst[col] = color;
    }
}

static void draw_char_stride(uint32_t *fb, int width, int height, int stride,
                             int x, int y, char ch, uint32_t color)
{
    const uint8_t *glyph;

    if (ch < 0x20 || ch > 0x7e)
        ch = '?';
    glyph = font8x16_data[ch - 0x20];
    for (int row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];

        for (int col = 0; col < 8; col++) {
            if (bits & (0x80u >> col))
                draw_rect_stride(fb, width, height, stride, x + col,
                                 y + row, 1, 1, color);
        }
    }
}

static void draw_text_fit(uint32_t *fb, int width, int height, int stride,
                          int x, int y, int max_w, const char *text,
                          uint32_t color)
{
    int used = 0;

    if (!text || max_w <= 0)
        return;
    for (const char *p = text; *p && used + 8 <= max_w; p++, used += 8)
        draw_char_stride(fb, width, height, stride, x + used, y, *p, color);
}

static void draw_button(uint32_t *fb, int width, int height, int stride, int x,
                        const char *label, uint32_t bg, uint32_t border,
                        uint32_t fg)
{
    int tw = text_width(label);

    draw_rect_stride(fb, width, height, stride, x, 0,
                     XV6_TITLEBAR_CONTROL_W, XV6_TITLEBAR_HEIGHT, bg);
    draw_rect_stride(fb, width, height, stride, x, XV6_TITLEBAR_HEIGHT - 1,
                     XV6_TITLEBAR_CONTROL_W, 1, border);
    draw_text_fit(fb, width, height, stride,
                  x + (XV6_TITLEBAR_CONTROL_W - tw) / 2, 7,
                  XV6_TITLEBAR_CONTROL_W - 4, label, fg);
}

enum xv6_titlebar_action xv6_titlebar_hit_test(int width, int x, int y)
{
    if (y < 0 || y >= XV6_TITLEBAR_HEIGHT ||
        width <= XV6_TITLEBAR_CONTROL_W * 3)
        return XV6_TITLEBAR_NONE;
    if (x >= width - XV6_TITLEBAR_CONTROL_W)
        return XV6_TITLEBAR_CLOSE;
    if (x >= width - XV6_TITLEBAR_CONTROL_W * 2)
        return XV6_TITLEBAR_MAXIMIZE;
    if (x >= width - XV6_TITLEBAR_CONTROL_W * 3)
        return XV6_TITLEBAR_MINIMIZE;
    return XV6_TITLEBAR_DRAG;
}

void xv6_titlebar_activate(enum xv6_titlebar_action action,
                           struct xdg_toplevel *toplevel,
                           int *maximized, int *running)
{
    if (action == XV6_TITLEBAR_CLOSE) {
        if (running)
            *running = 0;
    } else if (action == XV6_TITLEBAR_MAXIMIZE && toplevel && maximized) {
        if (*maximized) {
            xdg_toplevel_unset_maximized(toplevel);
            *maximized = 0;
        } else {
            xdg_toplevel_set_maximized(toplevel);
            *maximized = 1;
        }
    } else if (action == XV6_TITLEBAR_MINIMIZE && toplevel) {
        xdg_toplevel_set_minimized(toplevel);
    }
}

void xv6_titlebar_draw_stride(uint32_t *fb, int width, int height, int stride,
                              const char *title, int maximized,
                              const struct xv6_titlebar_palette *palette)
{
    const struct xv6_titlebar_palette *p =
        palette ? palette : &xv6_titlebar_palette_dark;
    int close_x = width - XV6_TITLEBAR_CONTROL_W;
    int max_x = close_x - XV6_TITLEBAR_CONTROL_W;
    int min_x = max_x - XV6_TITLEBAR_CONTROL_W;
    int title_limit = min_x - 18;

    if (!fb || width <= XV6_TITLEBAR_CONTROL_W * 3 ||
        height < XV6_TITLEBAR_HEIGHT || stride < width * 4)
        return;
    draw_rect_stride(fb, width, height, stride, 0, 0, width,
                     XV6_TITLEBAR_HEIGHT, p->bg);
    draw_rect_stride(fb, width, height, stride, 0, XV6_TITLEBAR_HEIGHT - 1,
                     width, 1, p->border);
    draw_text_fit(fb, width, height, stride, 10, 7, title_limit, title,
                  p->fg);
    draw_button(fb, width, height, stride, min_x, "-", p->button_bg,
                p->border, p->fg);
    draw_button(fb, width, height, stride, max_x, maximized ? "[]" : "+",
                p->button_bg, p->border, p->fg);
    draw_button(fb, width, height, stride, close_x, "x", p->close_bg,
                p->border, 0xffffffffu);
}

void xv6_titlebar_draw(uint32_t *fb, int width, int height,
                       const char *title, int maximized,
                       const struct xv6_titlebar_palette *palette)
{
    xv6_titlebar_draw_stride(fb, width, height, width * 4, title, maximized,
                             palette);
}
