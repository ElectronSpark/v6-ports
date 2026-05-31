#ifndef WLCOMP_DESKTOP_H
#define WLCOMP_DESKTOP_H

#include <stdint.h>

#include "xv6_icon.h"

enum shortcut_action {
    SHORTCUT_EXEC = 0,
    SHORTCUT_FILE,
    SHORTCUT_TERMINAL,
    SHORTCUT_FILES,
    SHORTCUT_SYSINFO,
    SHORTCUT_CALC,
    SHORTCUT_NETWORK,
    SHORTCUT_SETTINGS,
    SHORTCUT_MONITOR,
    SHORTCUT_3DDEMO,
    SHORTCUT_EDITOR,
};

typedef struct {
    char        label[48];
    char        exec_path[256];
    char        exec_name[64];
    char        exec_arg[256];
    int         action;
    uint32_t    icon_color;
    char        symbol;
    int         has_bitmap_icon;
    struct xv6_icon bitmap_icon;
    int         x, y, w, h;
} desktop_icon_t;

int wlcomp_desktop_shortcut_parse(const char *path, desktop_icon_t *out);
int wlcomp_desktop_icon_count(void);
const desktop_icon_t *wlcomp_desktop_icon_at(int idx);
void wlcomp_desktop_invalidate_layout(void);
void wlcomp_desktop_layout_icons(int fb_w, int fb_h);
void wlcomp_desktop_draw_wallpaper(uint32_t *fb, int fb_w, int fb_h);
void wlcomp_desktop_draw_icons(uint32_t *fb, int fb_w, int fb_h,
                               int selected_icon);
int wlcomp_desktop_icon_hit_test(int mx, int my);

#endif
