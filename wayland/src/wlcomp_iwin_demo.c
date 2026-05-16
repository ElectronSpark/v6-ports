#include "wlcomp_iwin_demo.h"

#include <stdio.h>

#include "wlcomp_draw.h"
#include "wlcomp_swgl.h"

void wlcomp_demo_draw(uint32_t *fb, int fb_w, int fb_h, iwin_t *w)
{
    int cx0 = w->x + 4;
    int cy0 = w->y + IWIN_TITLE_H + 2;
    int cw = w->w - 8;
    int ch = w->h - IWIN_TITLE_H - 4;
    struct wlcomp_swgl_context gl = { fb, fb_w, fb_h, cx0, cy0, cw, ch };

    wlcomp_swgl_clear(&gl, 0xFF080810);
    wlcomp_swgl_init_trig();

    int vx[12], vy[12], vz[12];
    for (int i = 0; i < 6; i++) {
        int deg = i * 60;
        vx[i]     = wlcomp_swgl_cos(deg) * 90 / 1024;
        vy[i]     = -70;
        vz[i]     = wlcomp_swgl_sin(deg) * 90 / 1024;
        vx[i + 6] = vx[i];
        vy[i + 6] = 70;
        vz[i + 6] = vz[i];
    }

    int a = w->demo_angle;
    int sa = wlcomp_swgl_sin(a), ca = wlcomp_swgl_cos(a);
    int sb = wlcomp_swgl_sin(a * 7 / 10), cb = wlcomp_swgl_cos(a * 7 / 10);

    struct wlcomp_swgl_vertex projected[12];
    int rz_arr[12];
    int midx = cx0 + cw / 2, midy = cy0 + ch / 2;

    for (int i = 0; i < 12; i++) {
        int x = vx[i], y = vy[i], z = vz[i];
        int rx = (x * ca - z * sa) / 1024;
        int rz = (x * sa + z * ca) / 1024;
        int ry = (y * cb - rz * sb) / 1024;
        int rz2 = (y * sb + rz * cb) / 1024;
        int d = 450 + rz2;
        if (d < 50) d = 50;
        projected[i].x = midx + rx * 300 / d;
        projected[i].y = midy + ry * 300 / d;
        projected[i].z = rz2;
        rz_arr[i] = rz2;
    }

    struct face { int v[4]; int nv; int depth; uint32_t color; } faces[20];
    int nf = 0;

    uint32_t top_col = 0xFF2288DD;
    for (int i = 0; i < 4; i++) {
        faces[nf].v[0] = 0; faces[nf].v[1] = i + 1; faces[nf].v[2] = i + 2; faces[nf].nv = 3;
        faces[nf].depth = (rz_arr[0] + rz_arr[i + 1] + rz_arr[i + 2]) / 3;
        faces[nf].color = top_col;
        nf++;
    }

    uint32_t bot_col = 0xFF1166AA;
    for (int i = 0; i < 4; i++) {
        faces[nf].v[0] = 6; faces[nf].v[1] = 6 + i + 1; faces[nf].v[2] = 6 + i + 2; faces[nf].nv = 3;
        faces[nf].depth = (rz_arr[6] + rz_arr[6 + i + 1] + rz_arr[6 + i + 2]) / 3;
        faces[nf].color = bot_col;
        nf++;
    }

    uint32_t side_cols[6] = { 0xFF33AAFF, 0xFF22CC88, 0xFFDDAA22,
                              0xFFFF6644, 0xFFCC44CC, 0xFF44CCCC };
    for (int i = 0; i < 6; i++) {
        int a0 = i, a1 = (i + 1) % 6, b0 = i + 6, b1 = (i + 1) % 6 + 6;
        int d = (rz_arr[a0] + rz_arr[a1] + rz_arr[b0] + rz_arr[b1]) / 4;
        int bright = 160 + d / 2;
        if (bright < 60) bright = 60;
        if (bright > 255) bright = 255;
        uint32_t r = ((side_cols[i] >> 16) & 0xFF) * bright / 255;
        uint32_t g = ((side_cols[i] >>  8) & 0xFF) * bright / 255;
        uint32_t b = ((side_cols[i]      ) & 0xFF) * bright / 255;
        uint32_t col = 0xFF000000 | (r << 16) | (g << 8) | b;
        faces[nf].v[0] = a0; faces[nf].v[1] = a1; faces[nf].v[2] = b1; faces[nf].nv = 3;
        faces[nf].depth = d; faces[nf].color = col; nf++;
        faces[nf].v[0] = a0; faces[nf].v[1] = b1; faces[nf].v[2] = b0; faces[nf].nv = 3;
        faces[nf].depth = d; faces[nf].color = col; nf++;
    }

    for (int i = 0; i < nf - 1; i++)
        for (int j = i + 1; j < nf; j++)
            if (faces[i].depth < faces[j].depth) {
                struct face tmp = faces[i]; faces[i] = faces[j]; faces[j] = tmp;
            }

    for (int fi = 0; fi < nf; fi++) {
        int shade = 190 + faces[fi].depth / 3;
        uint32_t col = wlcomp_swgl_shade(faces[fi].color, shade);
        wlcomp_swgl_draw_triangle(&gl,
                                  projected[faces[fi].v[0]],
                                  projected[faces[fi].v[1]],
                                  projected[faces[fi].v[2]],
                                  col);
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "OpenGL angle: %d", a % 360);
    draw_string(fb, fb_w, fb_h, cx0 + 8, cy0 + ch - 20, buf, 0xFF90D8FF, 1);
    w->demo_angle = (w->demo_angle + 2) % 3600;
}
