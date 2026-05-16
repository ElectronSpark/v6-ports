#include "wlcomp_iwin_filemgr.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "wlcomp_draw.h"

#define FM_ICON_W  72
#define FM_ICON_H  56
#define FM_ICON_PAD 4

void wlcomp_filemgr_read_dir(iwin_t *w)
{
    w->fm_count = 0;
    w->fm_scroll = 0;
    w->fm_selected = -1;

    DIR *dir = opendir(w->fm_path);
    if (!dir)
        return;

    struct dirent *de;
    while ((de = readdir(dir)) != NULL && w->fm_count < 64) {
        if (!de->d_name[0])
            continue;

        size_t len = strlen(de->d_name);
        if (len >= sizeof(w->fm_entries[w->fm_count]))
            len = sizeof(w->fm_entries[w->fm_count]) - 1;

        memcpy(w->fm_entries[w->fm_count], de->d_name, len);
        w->fm_entries[w->fm_count][len] = '\0';

        char full[320];
        if (strcmp(w->fm_path, "/") == 0)
            snprintf(full, sizeof(full), "/%s", w->fm_entries[w->fm_count]);
        else
            snprintf(full, sizeof(full), "%s/%s", w->fm_path, w->fm_entries[w->fm_count]);

        struct stat st;
        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
            w->fm_types[w->fm_count] = 1;
        } else if ((stat(full, &st) == 0 &&
                    (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) ||
                   strstr(w->fm_entries[w->fm_count], ".desktop")) {
            w->fm_types[w->fm_count] = 2;
        } else {
            w->fm_types[w->fm_count] = 0;
        }

        w->fm_count++;
    }
    closedir(dir);
}

void wlcomp_filemgr_draw(uint32_t *fb, int fb_w, int fb_h, iwin_t *w)
{
    int cx0 = w->x + 4;
    int cy0 = w->y + IWIN_TITLE_H + 2;
    int cw = w->w - 8;
    int ch = w->h - IWIN_TITLE_H - 4;

    draw_rect(fb, fb_w, fb_h, cx0, cy0, cw, ch, 0xFF141820);
    draw_rect(fb, fb_w, fb_h, cx0, cy0, cw, 22, 0xFF1A2030);
    draw_string(fb, fb_w, fb_h, cx0 + 4, cy0 + 3, w->fm_path, 0xFF64C8FF, 1);

    int cols = (cw - 8) / (FM_ICON_W + FM_ICON_PAD);
    if (cols < 1) cols = 1;
    int gy0 = cy0 + 26;
    int vis_rows = (ch - 30) / (FM_ICON_H + FM_ICON_PAD);
    int start = w->fm_scroll * cols;

    for (int i = start; i < w->fm_count; i++) {
        int vi = i - start;
        int row = vi / cols;
        int col = vi % cols;
        if (row >= vis_rows)
            break;

        int ix = cx0 + 4 + col * (FM_ICON_W + FM_ICON_PAD);
        int iy = gy0 + row * (FM_ICON_H + FM_ICON_PAD);

        if (i == w->fm_selected)
            draw_rect(fb, fb_w, fb_h, ix, iy, FM_ICON_W, FM_ICON_H, 0xFF2C4F7C);

        int bx = ix + (FM_ICON_W - 28) / 2;
        int by = iy + 2;
        uint32_t icol = w->fm_types[i] == 1 ? 0xFFA67C52 :
                         w->fm_types[i] == 2 ? 0xFF3D6E9E : 0xFF5A7090;
        char sym = w->fm_types[i] == 1 ? 'D' :
                   w->fm_types[i] == 2 ? 'X' : 'f';
        draw_rounded_rect(fb, fb_w, fb_h, bx, by, 28, 24, 3, icol);
        draw_char(fb, fb_w, fb_h, bx + 10, by + 4, sym, 0xFFFFFFFF, 1);

        char label[12];
        int nlen = (int)strlen(w->fm_entries[i]);
        if (nlen > 10) {
            memcpy(label, w->fm_entries[i], 9);
            label[9] = '~';
            label[10] = '\0';
        } else {
            memcpy(label, w->fm_entries[i], nlen + 1);
        }
        int lw = string_pixel_width(label, 1);
        int lx = ix + (FM_ICON_W - lw) / 2;
        draw_string(fb, fb_w, fb_h, lx, iy + 30, label, 0xFFD2DAE2, 1);
    }
}

int wlcomp_filemgr_icon_hit(iwin_t *w, int mx, int my)
{
    int cx0 = w->x + 4;
    int cy0 = w->y + IWIN_TITLE_H + 2;
    int cw = w->w - 8;
    int cols = (cw - 8) / (FM_ICON_W + FM_ICON_PAD);
    if (cols < 1) cols = 1;
    int gy0 = cy0 + 26;
    int start = w->fm_scroll * cols;

    for (int i = start; i < w->fm_count; i++) {
        int vi = i - start;
        int row = vi / cols;
        int col = vi % cols;
        int ix = cx0 + 4 + col * (FM_ICON_W + FM_ICON_PAD);
        int iy = gy0 + row * (FM_ICON_H + FM_ICON_PAD);
        if (mx >= ix && mx < ix + FM_ICON_W &&
            my >= iy && my < iy + FM_ICON_H)
            return i;
    }
    return -1;
}

void wlcomp_filemgr_entry_path(iwin_t *w, int idx, char *out, size_t out_sz)
{
    if (strcmp(w->fm_path, "/") == 0)
        snprintf(out, out_sz, "/%s", w->fm_entries[idx]);
    else
        snprintf(out, out_sz, "%s/%s", w->fm_path, w->fm_entries[idx]);
}

void wlcomp_filemgr_navigate(iwin_t *w, int idx)
{
    if (strcmp(w->fm_entries[idx], "..") == 0) {
        char *slash = strrchr(w->fm_path, '/');
        if (slash && slash != w->fm_path)
            *slash = '\0';
        else
            snprintf(w->fm_path, sizeof(w->fm_path), "/");
    } else if (strcmp(w->fm_entries[idx], ".") != 0) {
        char tmp[256];
        wlcomp_filemgr_entry_path(w, idx, tmp, sizeof(tmp));
        snprintf(w->fm_path, sizeof(w->fm_path), "%s", tmp);
    }
    const char prefix[] = "Files - ";
    size_t prefix_len = sizeof(prefix) - 1;
    size_t avail = sizeof(w->title) - prefix_len - 1;
    size_t n = 0;

    memcpy(w->title, prefix, prefix_len);
    while (n < avail && w->fm_path[n])
        n++;
    memcpy(w->title + prefix_len, w->fm_path, n);
    w->title[prefix_len + n] = '\0';
    wlcomp_filemgr_read_dir(w);
}
