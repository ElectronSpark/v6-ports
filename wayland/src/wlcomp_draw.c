#include "wlcomp_draw.h"

#include "font8x16.h"

static struct wlcomp_draw_clip g_draw_clip;
static int g_draw_clip_enabled;

void wlcomp_draw_set_clip(const struct wlcomp_draw_clip *clip)
{
    if (!clip) {
        wlcomp_draw_clear_clip();
        return;
    }
    g_draw_clip = *clip;
    g_draw_clip_enabled = 1;
}

void wlcomp_draw_clear_clip(void)
{
    g_draw_clip_enabled = 0;
}

int wlcomp_draw_clip_xyxy(int fb_w, int fb_h,
                          int *x0, int *y0, int *x1, int *y1)
{
    if (*x0 < 0) *x0 = 0;
    if (*y0 < 0) *y0 = 0;
    if (*x1 > fb_w) *x1 = fb_w;
    if (*y1 > fb_h) *y1 = fb_h;
    if (g_draw_clip_enabled) {
        if (*x0 < g_draw_clip.x1) *x0 = g_draw_clip.x1;
        if (*y0 < g_draw_clip.y1) *y0 = g_draw_clip.y1;
        if (*x1 > g_draw_clip.x2) *x1 = g_draw_clip.x2;
        if (*y1 > g_draw_clip.y2) *y1 = g_draw_clip.y2;
    }
    return *x0 < *x1 && *y0 < *y1;
}

int wlcomp_draw_pixel_in_clip(int x, int y, int fb_w, int fb_h)
{
    if (x < 0 || x >= fb_w || y < 0 || y >= fb_h)
        return 0;
    if (!g_draw_clip_enabled)
        return 1;
    return x >= g_draw_clip.x1 && x < g_draw_clip.x2 &&
           y >= g_draw_clip.y1 && y < g_draw_clip.y2;
}

void draw_char(uint32_t *fb, int fb_w, int fb_h,
               int x, int y, char ch, uint32_t color, int scale)
{
    if (ch < 0x20 || ch > 0x7E)
        ch = '?';
    const uint8_t *glyph = font8x16_data[(int)(ch - 0x20)];
    for (int row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++) {
                        int px = x + col * scale + sx;
                        int py = y + row * scale + sy;
                        if (wlcomp_draw_pixel_in_clip(px, py, fb_w, fb_h))
                            fb[py * fb_w + px] = color;
                    }
            }
        }
    }
}

void draw_string(uint32_t *fb, int fb_w, int fb_h,
                 int x, int y, const char *s, uint32_t color, int scale)
{
    for (; *s; s++, x += 8 * scale)
        draw_char(fb, fb_w, fb_h, x, y, *s, color, scale);
}

int string_pixel_width(const char *s, int scale)
{
    int len = 0;
    while (*s++)
        len++;
    return len * 8 * scale;
}

void draw_rect(uint32_t *fb, int fb_w, int fb_h,
               int x, int y, int w, int h, uint32_t color)
{
    int x0 = x;
    int y0 = y;
    int x1 = x + w;
    int y1 = y + h;

    if (w <= 0 || h <= 0)
        return;
    if (!wlcomp_draw_clip_xyxy(fb_w, fb_h, &x0, &y0, &x1, &y1))
        return;

    for (int row = y0; row < y1; row++) {
        for (int col = x0; col < x1; col++) {
            fb[row * fb_w + col] = color;
        }
    }
}

void draw_rounded_rect(uint32_t *fb, int fb_w, int fb_h,
                       int x, int y, int w, int h, int r,
                       uint32_t color)
{
    draw_rect(fb, fb_w, fb_h, x + r, y, w - 2 * r, h, color);
    draw_rect(fb, fb_w, fb_h, x, y + r, w, h - 2 * r, color);
    for (int cy = 0; cy <= r; cy++) {
        for (int cx = 0; cx <= r; cx++) {
            if (cx * cx + cy * cy <= r * r) {
                int px, py;
                px = x + r - cx; py = y + r - cy;
                if (wlcomp_draw_pixel_in_clip(px, py, fb_w, fb_h))
                    fb[py * fb_w + px] = color;
                px = x + w - r - 1 + cx; py = y + r - cy;
                if (wlcomp_draw_pixel_in_clip(px, py, fb_w, fb_h))
                    fb[py * fb_w + px] = color;
                px = x + r - cx; py = y + h - r - 1 + cy;
                if (wlcomp_draw_pixel_in_clip(px, py, fb_w, fb_h))
                    fb[py * fb_w + px] = color;
                px = x + w - r - 1 + cx; py = y + h - r - 1 + cy;
                if (wlcomp_draw_pixel_in_clip(px, py, fb_w, fb_h))
                    fb[py * fb_w + px] = color;
            }
        }
    }
}

void draw_circle(uint32_t *fb, int fb_w, int fb_h,
                 int cx, int cy, int r, uint32_t color)
{
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r) {
                int px = cx + dx;
                int py = cy + dy;
                if (wlcomp_draw_pixel_in_clip(px, py, fb_w, fb_h))
                    fb[py * fb_w + px] = color;
            }
}
