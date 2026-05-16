#include "wlcomp_swgl.h"

#include "wlcomp_draw.h"

static int sin_tab[256];
static int sin_tab_ready;

void wlcomp_swgl_init_trig(void)
{
    if (sin_tab_ready)
        return;
    for (int i = 0; i < 256; i++) {
        long a_mr;
        int neg = 0;

        if (i < 128)
            a_mr = (long)i * 3142 / 128;
        else {
            a_mr = (long)(i - 128) * 3142 / 128;
            neg = 1;
        }

        long pi_mr = 3142;
        long num = 16 * a_mr * (pi_mr - a_mr);
        long den = 5 * pi_mr * pi_mr - 4 * a_mr * (pi_mr - a_mr);
        if (den == 0)
            den = 1;

        int v = (int)(num * 1024 / den);
        if (neg)
            v = -v;
        sin_tab[i] = v;
    }
    sin_tab_ready = 1;
}

int wlcomp_swgl_sin(int deg)
{
    return sin_tab[((deg % 360 + 360) * 256 / 360) & 255];
}

int wlcomp_swgl_cos(int deg)
{
    return sin_tab[(((deg + 90) % 360 + 360) * 256 / 360) & 255];
}

uint32_t wlcomp_swgl_shade(uint32_t color, int shade)
{
    uint32_t r, g, b;

    if (shade < 32) shade = 32;
    if (shade > 255) shade = 255;
    r = ((color >> 16) & 0xFF) * shade / 255;
    g = ((color >> 8) & 0xFF) * shade / 255;
    b = (color & 0xFF) * shade / 255;
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

void wlcomp_swgl_clear(struct wlcomp_swgl_context *gl, uint32_t color)
{
    draw_rect(gl->fb, gl->fb_w, gl->fb_h, gl->x, gl->y, gl->w, gl->h, color);
}

static int swgl_edge(struct wlcomp_swgl_vertex a,
                     struct wlcomp_swgl_vertex b,
                     int x, int y)
{
    return (x - a.x) * (b.y - a.y) - (y - a.y) * (b.x - a.x);
}

void wlcomp_swgl_draw_triangle(struct wlcomp_swgl_context *gl,
                               struct wlcomp_swgl_vertex a,
                               struct wlcomp_swgl_vertex b,
                               struct wlcomp_swgl_vertex c,
                               uint32_t color)
{
    int min_x = a.x, max_x = a.x;
    int min_y = a.y, max_y = a.y;
    int area;

    if (b.x < min_x) min_x = b.x;
    if (c.x < min_x) min_x = c.x;
    if (b.x > max_x) max_x = b.x;
    if (c.x > max_x) max_x = c.x;
    if (b.y < min_y) min_y = b.y;
    if (c.y < min_y) min_y = c.y;
    if (b.y > max_y) max_y = b.y;
    if (c.y > max_y) max_y = c.y;

    if (min_x < gl->x) min_x = gl->x;
    if (min_y < gl->y) min_y = gl->y;
    if (max_x >= gl->x + gl->w) max_x = gl->x + gl->w - 1;
    if (max_y >= gl->y + gl->h) max_y = gl->y + gl->h - 1;

    area = swgl_edge(a, b, c.x, c.y);
    if (area == 0)
        return;

    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            int w0 = swgl_edge(b, c, x, y);
            int w1 = swgl_edge(c, a, x, y);
            int w2 = swgl_edge(a, b, x, y);
            if ((area > 0 && w0 >= 0 && w1 >= 0 && w2 >= 0) ||
                (area < 0 && w0 <= 0 && w1 <= 0 && w2 <= 0)) {
                if (wlcomp_draw_pixel_in_clip(x, y, gl->fb_w, gl->fb_h))
                    gl->fb[y * gl->fb_w + x] = color;
            }
        }
    }
}
