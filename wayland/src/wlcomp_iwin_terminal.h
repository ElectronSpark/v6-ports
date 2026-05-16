#ifndef WLCOMP_IWIN_TERMINAL_H
#define WLCOMP_IWIN_TERMINAL_H

#include <stdint.h>

#include "wlcomp_iwin.h"

int wlcomp_terminal_window_w_for_cols(int cols);
int wlcomp_terminal_window_h_for_rows(int rows);
void wlcomp_terminal_send_winsz(iwin_t *w);
void wlcomp_terminal_apply_window_size(iwin_t *w, int notify_pty);
void wlcomp_terminal_clear(iwin_t *w);
int wlcomp_terminal_process_output(iwin_t *w);
int wlcomp_terminal_scroll_view(iwin_t *w, int delta);
void wlcomp_terminal_draw(uint32_t *fb, int fb_w, int fb_h, iwin_t *w);

#endif
