#ifndef WLCOMP_IWIN_H
#define WLCOMP_IWIN_H

#include <stdint.h>
#include <sys/types.h>

#define MAX_IWIN        8
#define IWIN_TITLE_H    24
#define IWIN_CLOSE_SZ   18

enum wlcomp_iwin_app {
    APP_NONE = 0,
    APP_TERMINAL,
    APP_SYSINFO,
    APP_FILES,
    APP_CALC,
    APP_NETWORK,
    APP_SETTINGS,
    APP_MONITOR,
    APP_3DDEMO,
    APP_FILEMGR,
};

#define TERM_DEFAULT_ROWS  24
#define TERM_DEFAULT_COLS  80
#define TERM_MIN_ROWS       8
#define TERM_MIN_COLS      24
#define TERM_MAX_ROWS      48
#define TERM_MAX_COLS     120
#define TERM_SCROLLBACK   512
#define TERM_CELL_W         8
#define TERM_CELL_H        16
#define TERM_SCROLLBAR_W   14

typedef struct {
    int   active;
    int   type;
    int   x, y, w, h;
    char  title[64];

    int   dragging;
    int   drag_ox, drag_oy;

    int   resizing;
    int   resize_edge;
    int   resize_start_x, resize_start_y;
    int   resize_start_w, resize_start_h;

    int   master_fd;
    pid_t shell_pid;
    char  cells[TERM_MAX_ROWS][TERM_MAX_COLS];
    char  scrollback[TERM_SCROLLBACK][TERM_MAX_COLS];
    int   term_rows, term_cols;
    int   scrollback_count;
    int   scroll_offset;
    int   cur_row, cur_col;

    char  text[4096];
    int   text_len;
    int   text_scroll;
    uint32_t last_refresh;

    char  calc_display[32];
    long  calc_accum;
    long  calc_operand;
    char  calc_op;
    int   calc_fresh;

    int   demo_angle;

    char  fm_path[256];
    char  fm_entries[64][64];
    int   fm_types[64];
    int   fm_count;
    int   fm_scroll;
    int   fm_selected;
} iwin_t;

#endif
