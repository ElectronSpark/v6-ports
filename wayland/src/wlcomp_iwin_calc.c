#include "wlcomp_iwin_calc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wlcomp_draw.h"

#define CALC_COLS 4
#define CALC_ROWS 5
#define CALC_BTN_W 52
#define CALC_BTN_H 36
#define CALC_BTN_PAD 4

static const char *calc_buttons[] = {
    "C", "(", ")", "/",
    "7", "8", "9", "*",
    "4", "5", "6", "-",
    "1", "2", "3", "+",
    "0", ".", "=", " ",
};

int wlcomp_calc_window_w(void)
{
    return CALC_COLS * (CALC_BTN_W + CALC_BTN_PAD) + 12;
}

int wlcomp_calc_window_h(void)
{
    return CALC_ROWS * (CALC_BTN_H + CALC_BTN_PAD) + IWIN_TITLE_H + 48;
}

void wlcomp_calc_reset(iwin_t *w)
{
    snprintf(w->calc_display, sizeof(w->calc_display), "0");
    w->calc_accum = 0;
    w->calc_operand = 0;
    w->calc_op = 0;
    w->calc_fresh = 1;
}

static void calc_press(iwin_t *w, const char *btn)
{
    if (btn[0] == 'C') {
        wlcomp_calc_reset(w);
        return;
    }
    if (btn[0] >= '0' && btn[0] <= '9') {
        if (w->calc_fresh) {
            snprintf(w->calc_display, sizeof(w->calc_display), "%c", btn[0]);
            w->calc_fresh = 0;
        } else {
            int len = (int)strlen(w->calc_display);
            if (len < 15) {
                w->calc_display[len] = btn[0];
                w->calc_display[len + 1] = '\0';
            }
        }
        return;
    }
    if (btn[0] == '.') {
        if (!strchr(w->calc_display, '.')) {
            int len = (int)strlen(w->calc_display);
            if (len < 15) {
                w->calc_display[len] = '.';
                w->calc_display[len + 1] = '\0';
            }
        }
        return;
    }
    if (btn[0] == '=' || btn[0] == '+' || btn[0] == '-' ||
        btn[0] == '*' || btn[0] == '/') {
        long cur = strtol(w->calc_display, NULL, 10);
        if (w->calc_op) {
            switch (w->calc_op) {
            case '+': w->calc_accum += cur; break;
            case '-': w->calc_accum -= cur; break;
            case '*': w->calc_accum *= cur; break;
            case '/': w->calc_accum = (cur != 0) ? w->calc_accum / cur : 0; break;
            }
        } else {
            w->calc_accum = cur;
        }
        snprintf(w->calc_display, sizeof(w->calc_display), "%ld", w->calc_accum);
        w->calc_op = (btn[0] == '=') ? 0 : btn[0];
        w->calc_fresh = 1;
    }
}

void wlcomp_calc_press_index(iwin_t *w, int idx)
{
    int count = (int)(sizeof(calc_buttons) / sizeof(calc_buttons[0]));
    if (idx < 0 || idx >= count)
        return;
    calc_press(w, calc_buttons[idx]);
}

void wlcomp_calc_draw(uint32_t *fb, int fb_w, int fb_h, iwin_t *w)
{
    int cx0 = w->x + 4;
    int cy0 = w->y + IWIN_TITLE_H + 2;
    int content_w = w->w - 8;

    draw_rect(fb, fb_w, fb_h, cx0, cy0, content_w, 32, 0xFF0A1020);
    int dw = string_pixel_width(w->calc_display, 2);
    draw_string(fb, fb_w, fb_h, cx0 + content_w - dw - 8, cy0 + 4,
                w->calc_display, 0xFF64C8FF, 2);

    for (int r = 0; r < CALC_ROWS; r++) {
        for (int c = 0; c < CALC_COLS; c++) {
            int idx = r * CALC_COLS + c;
            const char *lbl = calc_buttons[idx];
            if (lbl[0] == ' ')
                continue;
            int bx = cx0 + c * (CALC_BTN_W + CALC_BTN_PAD);
            int by = cy0 + 40 + r * (CALC_BTN_H + CALC_BTN_PAD);
            uint32_t bg;
            if (lbl[0] >= '0' && lbl[0] <= '9')
                bg = 0xFF2A3040;
            else if (lbl[0] == '=')
                bg = 0xFF2C4F7C;
            else
                bg = 0xFF3A4050;
            draw_rounded_rect(fb, fb_w, fb_h, bx, by,
                              CALC_BTN_W, CALC_BTN_H, 4, bg);
            int lw = string_pixel_width(lbl, 1);
            draw_string(fb, fb_w, fb_h,
                        bx + (CALC_BTN_W - lw) / 2,
                        by + (CALC_BTN_H - 16) / 2,
                        lbl, 0xFFD2DAE2, 1);
        }
    }
}

int wlcomp_calc_button_hit(iwin_t *w, int mx, int my)
{
    int cx0 = w->x + 4;
    int cy0 = w->y + IWIN_TITLE_H + 2;

    for (int r = 0; r < CALC_ROWS; r++) {
        for (int c = 0; c < CALC_COLS; c++) {
            int bx = cx0 + c * (CALC_BTN_W + CALC_BTN_PAD);
            int by = cy0 + 40 + r * (CALC_BTN_H + CALC_BTN_PAD);
            if (mx >= bx && mx < bx + CALC_BTN_W &&
                my >= by && my < by + CALC_BTN_H)
                return r * CALC_COLS + c;
        }
    }
    return -1;
}
