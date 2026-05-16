#ifndef WLCOMP_DRAW_H
#define WLCOMP_DRAW_H

#include <stdint.h>

struct wlcomp_draw_clip {
    int x1;
    int y1;
    int x2;
    int y2;
};

void wlcomp_draw_set_clip(const struct wlcomp_draw_clip *clip);
void wlcomp_draw_clear_clip(void);
int wlcomp_draw_clip_xyxy(int fb_w, int fb_h,
                          int *x0, int *y0, int *x1, int *y1);
int wlcomp_draw_pixel_in_clip(int x, int y, int fb_w, int fb_h);

void draw_char(uint32_t *fb, int fb_w, int fb_h,
               int x, int y, char ch, uint32_t color, int scale);
void draw_string(uint32_t *fb, int fb_w, int fb_h,
                 int x, int y, const char *s, uint32_t color, int scale);
int string_pixel_width(const char *s, int scale);
void draw_rect(uint32_t *fb, int fb_w, int fb_h,
               int x, int y, int w, int h, uint32_t color);
void draw_rounded_rect(uint32_t *fb, int fb_w, int fb_h,
                       int x, int y, int w, int h, int r,
                       uint32_t color);
void draw_circle(uint32_t *fb, int fb_w, int fb_h,
                 int cx, int cy, int r, uint32_t color);

#endif
