#ifndef WLCOMP_IWIN_FILEMGR_H
#define WLCOMP_IWIN_FILEMGR_H

#include <stddef.h>
#include <stdint.h>

#include "wlcomp_iwin.h"

void wlcomp_filemgr_read_dir(iwin_t *w);
void wlcomp_filemgr_draw(uint32_t *fb, int fb_w, int fb_h, iwin_t *w);
int wlcomp_filemgr_icon_hit(iwin_t *w, int mx, int my);
void wlcomp_filemgr_entry_path(iwin_t *w, int idx, char *out, size_t out_sz);
void wlcomp_filemgr_navigate(iwin_t *w, int idx);

#endif
