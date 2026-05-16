#ifndef WLCOMP_IWIN_TEXT_H
#define WLCOMP_IWIN_TEXT_H

#include <stdint.h>

#include "wlcomp_iwin.h"

int wlcomp_sync_resolv_conf_from_netconf(int wait_us);

void wlcomp_text_fill_sysinfo(iwin_t *w, uint32_t fb_w, uint32_t fb_h);
void wlcomp_text_fill_network(iwin_t *w, uint32_t now_ms);
void wlcomp_text_fill_files(iwin_t *w);
void wlcomp_text_fill_monitor(iwin_t *w, uint32_t now_ms);
void wlcomp_text_fill_settings(iwin_t *w, uint32_t fb_w, uint32_t fb_h);

void wlcomp_text_draw(uint32_t *fb, int fb_w, int fb_h, iwin_t *w);
void wlcomp_settings_draw(uint32_t *fb, int fb_w, int fb_h, iwin_t *w);

#endif
