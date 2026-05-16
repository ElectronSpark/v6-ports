#ifndef WLCOMP_IWIN_CALC_H
#define WLCOMP_IWIN_CALC_H

#include <stdint.h>

#include "wlcomp_iwin.h"

int wlcomp_calc_window_w(void);
int wlcomp_calc_window_h(void);
void wlcomp_calc_reset(iwin_t *w);
void wlcomp_calc_press_index(iwin_t *w, int idx);
void wlcomp_calc_draw(uint32_t *fb, int fb_w, int fb_h, iwin_t *w);
int wlcomp_calc_button_hit(iwin_t *w, int mx, int my);

#endif
