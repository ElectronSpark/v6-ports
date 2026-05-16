#include "pixel_fps_overlay.h"

#include <stddef.h>

static void overlay_rect(uint32_t *pixels, int width, int height, int stride,
                         int x, int y, int w, int h, uint32_t color)
{
    if (pixels == NULL || w <= 0 || h <= 0)
        return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= width || y >= height)
        return;
    if (x + w > width)
        w = width - x;
    if (y + h > height)
        h = height - y;
    for (int yy = y; yy < y + h; yy++) {
        uint32_t *row = (uint32_t *)((uint8_t *)pixels +
                                     (size_t)yy * (size_t)stride);
        for (int xx = x; xx < x + w; xx++)
            row[xx] = color;
    }
}

static const char *overlay_glyph(char c)
{
    switch (c) {
    case 'F': return "111100111100100";
    case 'P': return "111101111100100";
    case 'S': return "111100111001111";
    case '0': return "111101101101111";
    case '1': return "010110010010111";
    case '2': return "111001111100111";
    case '3': return "111001111001111";
    case '4': return "101101111001001";
    case '5': return "111100111001111";
    case '6': return "111100111101111";
    case '7': return "111001010010010";
    case '8': return "111101111101111";
    case '9': return "111101111001111";
    case '.': return "000000000000010";
    case '-': return "000000111000000";
    default: return NULL;
    }
}

static void overlay_text(uint32_t *pixels, int width, int height, int stride,
                         int x, int y, const char *text, int scale,
                         uint32_t color)
{
    for (const char *p = text; *p; p++, x += 4 * scale) {
        const char *glyph = overlay_glyph(*p);

        if (*p == ' ') {
            x += 2 * scale;
            continue;
        }
        if (!glyph)
            continue;
        for (int row = 0; row < 5; row++) {
            for (int col = 0; col < 3; col++) {
                if (glyph[row * 3 + col] == '1')
                    overlay_rect(pixels, width, height, stride,
                                 x + col * scale, y + row * scale,
                                 scale, scale, color);
            }
        }
    }
}

void pixel_fps_overlay_draw(uint32_t *pixels, int width, int height,
                            int stride, const char *text)
{
    int scale = width >= 640 ? 10 : 6;
    int x = 20;
    int y = 20;
    int panel_w = 34 * scale;
    int panel_h = 8 * scale;
    uint32_t black = 0xe8000000u;
    uint32_t cyan = 0xff16c8f2u;
    uint32_t white = 0xffedf6ffu;

    if (text == NULL || text[0] == '\0')
        return;

    overlay_rect(pixels, width, height, stride,
                 x - 8, y - 8, panel_w, panel_h, black);
    overlay_rect(pixels, width, height, stride,
                 x - 8, y - 8, panel_w, 4, cyan);
    overlay_rect(pixels, width, height, stride,
                 x - 8, y + panel_h - 12, panel_w, 4, cyan);
    overlay_rect(pixels, width, height, stride,
                 x - 8, y - 8, 4, panel_h, cyan);
    overlay_rect(pixels, width, height, stride,
                 x + panel_w - 12, y - 8, 4, panel_h, cyan);
    overlay_text(pixels, width, height, stride, x, y, text, scale, white);
}
