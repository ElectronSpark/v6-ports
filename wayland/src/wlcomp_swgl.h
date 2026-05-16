#ifndef WLCOMP_SWGL_H
#define WLCOMP_SWGL_H

#include <stdint.h>

struct wlcomp_swgl_context {
    uint32_t *fb;
    int fb_w;
    int fb_h;
    int x;
    int y;
    int w;
    int h;
};

struct wlcomp_swgl_vertex {
    int x;
    int y;
    int z;
};

void wlcomp_swgl_init_trig(void);
int wlcomp_swgl_sin(int deg);
int wlcomp_swgl_cos(int deg);
uint32_t wlcomp_swgl_shade(uint32_t color, int shade);
void wlcomp_swgl_clear(struct wlcomp_swgl_context *gl, uint32_t color);
void wlcomp_swgl_draw_triangle(struct wlcomp_swgl_context *gl,
                               struct wlcomp_swgl_vertex a,
                               struct wlcomp_swgl_vertex b,
                               struct wlcomp_swgl_vertex c,
                               uint32_t color);

#endif
