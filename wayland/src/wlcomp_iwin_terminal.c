#include "wlcomp_iwin_terminal.h"

#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "wlcomp_draw.h"

#define XV6_TIOCSWINSZ 0x5414

static uint32_t terminal_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static int terminal_content_cols_for_width(int win_w)
{
    int text_w = win_w - TERM_SCROLLBAR_W - 16;
    int cols = text_w / TERM_CELL_W;
    if (cols < TERM_MIN_COLS) cols = TERM_MIN_COLS;
    if (cols > TERM_MAX_COLS) cols = TERM_MAX_COLS;
    return cols;
}

static int terminal_content_rows_for_height(int win_h)
{
    int text_h = win_h - IWIN_TITLE_H - 8;
    int rows = text_h / TERM_CELL_H;
    if (rows < TERM_MIN_ROWS) rows = TERM_MIN_ROWS;
    if (rows > TERM_MAX_ROWS) rows = TERM_MAX_ROWS;
    return rows;
}

int wlcomp_terminal_window_w_for_cols(int cols)
{
    return TERM_SCROLLBAR_W + 16 + cols * TERM_CELL_W;
}

int wlcomp_terminal_window_h_for_rows(int rows)
{
    return IWIN_TITLE_H + 8 + rows * TERM_CELL_H;
}

void wlcomp_terminal_send_winsz(iwin_t *w)
{
    if (w->master_fd < 0)
        return;

    struct { unsigned short ws_row, ws_col, ws_xpixel, ws_ypixel; } ws;
    memset(&ws, 0, sizeof(ws));
    ws.ws_row = (unsigned short)w->term_rows;
    ws.ws_col = (unsigned short)w->term_cols;
    ws.ws_xpixel = (unsigned short)(w->term_cols * TERM_CELL_W);
    ws.ws_ypixel = (unsigned short)(w->term_rows * TERM_CELL_H);
    ioctl(w->master_fd, XV6_TIOCSWINSZ, &ws);
}

void wlcomp_terminal_apply_window_size(iwin_t *w, int notify_pty)
{
    int old_rows = w->term_rows;
    int old_cols = w->term_cols;
    int rows = terminal_content_rows_for_height(w->h);
    int cols = terminal_content_cols_for_width(w->w);

    if (old_rows == 0) old_rows = TERM_DEFAULT_ROWS;
    if (old_cols == 0) old_cols = TERM_DEFAULT_COLS;
    w->term_rows = rows;
    w->term_cols = cols;

    if (cols > old_cols) {
        for (int r = 0; r < TERM_MAX_ROWS; r++)
            memset(w->cells[r] + old_cols, ' ', cols - old_cols);
    }
    if (rows > old_rows) {
        for (int r = old_rows; r < rows; r++)
            memset(w->cells[r], ' ', TERM_MAX_COLS);
    }

    if (w->cur_row >= rows) w->cur_row = rows - 1;
    if (w->cur_col >= cols) w->cur_col = cols - 1;
    if (w->cur_row < 0) w->cur_row = 0;
    if (w->cur_col < 0) w->cur_col = 0;

    if (w->scroll_offset > w->scrollback_count)
        w->scroll_offset = w->scrollback_count;

    if (notify_pty && (rows != old_rows || cols != old_cols))
        wlcomp_terminal_send_winsz(w);
}

void wlcomp_terminal_clear(iwin_t *w)
{
    memset(w->cells, ' ', sizeof(w->cells));
    w->scrollback_count = 0;
    w->scroll_offset = 0;
    if (w->term_rows <= 0) w->term_rows = TERM_DEFAULT_ROWS;
    if (w->term_cols <= 0) w->term_cols = TERM_DEFAULT_COLS;
    w->cur_row = 0;
    w->cur_col = 0;
}

static void terminal_push_scrollback(iwin_t *w, const char *line)
{
    if (w->scrollback_count >= TERM_SCROLLBACK) {
        memmove(w->scrollback[0], w->scrollback[1],
                (TERM_SCROLLBACK - 1) * TERM_MAX_COLS);
        w->scrollback_count = TERM_SCROLLBACK - 1;
    }
    memcpy(w->scrollback[w->scrollback_count], line, TERM_MAX_COLS);
    w->scrollback_count++;
    if (w->scroll_offset > 0 && w->scroll_offset < w->scrollback_count)
        w->scroll_offset++;
    if (w->scroll_offset > w->scrollback_count)
        w->scroll_offset = w->scrollback_count;
}

static void terminal_scroll_up(iwin_t *w)
{
    terminal_push_scrollback(w, w->cells[0]);
    for (int r = 1; r < w->term_rows; r++)
        memcpy(w->cells[r - 1], w->cells[r], TERM_MAX_COLS);
    memset(w->cells[w->term_rows - 1], ' ', TERM_MAX_COLS);
}

static void terminal_putc(iwin_t *w, char c)
{
    if (c == '\n') {
        w->cur_row++;
        if (w->cur_row >= w->term_rows) {
            terminal_scroll_up(w);
            w->cur_row = w->term_rows - 1;
        }
    } else if (c == '\r') {
        w->cur_col = 0;
    } else if (c == '\b' || c == 0x7f) {
        if (w->cur_col > 0) w->cur_col--;
    } else if (c == '\t') {
        w->cur_col = (w->cur_col + 8) & ~7;
        if (w->cur_col >= w->term_cols) w->cur_col = w->term_cols - 1;
    } else if (c == '\033') {
    } else if ((unsigned char)c >= 0x20) {
        if (w->cur_col >= w->term_cols) {
            w->cur_col = 0;
            w->cur_row++;
            if (w->cur_row >= w->term_rows) {
                terminal_scroll_up(w);
                w->cur_row = w->term_rows - 1;
            }
        }
        w->cells[w->cur_row][w->cur_col] = c;
        w->cur_col++;
    }
}

static int terminal_process_esc(iwin_t *w, const char *buf, int len)
{
    if (len < 2) return 0;
    if (buf[1] == '[') {
        int i = 2;
        int params[8] = {0};
        int np = 0;
        while (i < len && ((buf[i] >= '0' && buf[i] <= '9') || buf[i] == ';')) {
            if (buf[i] == ';') {
                np++;
                if (np >= 8) np = 7;
            } else {
                params[np] = params[np] * 10 + (buf[i] - '0');
            }
            i++;
        }
        if (i >= len) return 0;
        np++;
        char cmd = buf[i];
        switch (cmd) {
        case 'H': case 'f':
            w->cur_row = (params[0] > 0 ? params[0] - 1 : 0);
            w->cur_col = (np > 1 && params[1] > 0 ? params[1] - 1 : 0);
            if (w->cur_row >= w->term_rows) w->cur_row = w->term_rows - 1;
            if (w->cur_col >= w->term_cols) w->cur_col = w->term_cols - 1;
            break;
        case 'A':
            w->cur_row -= (params[0] > 0 ? params[0] : 1);
            if (w->cur_row < 0) w->cur_row = 0;
            break;
        case 'B':
            w->cur_row += (params[0] > 0 ? params[0] : 1);
            if (w->cur_row >= w->term_rows) w->cur_row = w->term_rows - 1;
            break;
        case 'C':
            w->cur_col += (params[0] > 0 ? params[0] : 1);
            if (w->cur_col >= w->term_cols) w->cur_col = w->term_cols - 1;
            break;
        case 'D':
            w->cur_col -= (params[0] > 0 ? params[0] : 1);
            if (w->cur_col < 0) w->cur_col = 0;
            break;
        case 'J':
            if (params[0] == 0) {
                memset(w->cells[w->cur_row] + w->cur_col, ' ',
                       w->term_cols - w->cur_col);
                for (int r = w->cur_row + 1; r < w->term_rows; r++)
                    memset(w->cells[r], ' ', w->term_cols);
            } else if (params[0] == 2 || params[0] == 3) {
                wlcomp_terminal_clear(w);
            }
            break;
        case 'K':
            if (params[0] == 0)
                memset(w->cells[w->cur_row] + w->cur_col, ' ',
                       w->term_cols - w->cur_col);
            else if (params[0] == 1)
                memset(w->cells[w->cur_row], ' ', w->cur_col + 1);
            else if (params[0] == 2)
                memset(w->cells[w->cur_row], ' ', w->term_cols);
            break;
        default:
            break;
        }
        return i + 1;
    } else if (buf[1] == ']') {
        for (int i = 2; i < len; i++) {
            if (buf[i] == '\007' || (buf[i] == '\\' && i > 2 && buf[i-1] == '\033'))
                return i + 1;
        }
        return 0;
    }
    return 2;
}

int wlcomp_terminal_process_output(iwin_t *w)
{
    int changed = 0;

    if (w->master_fd < 0) return 0;
    char buf[1024];
    int n;
    while ((n = read(w->master_fd, buf, sizeof(buf))) > 0) {
        changed = 1;
        int i = 0;
        while (i < n) {
            if (buf[i] == '\033') {
                int consumed = terminal_process_esc(w, buf + i, n - i);
                if (consumed > 0) { i += consumed; continue; }
                else { i++; continue; }
            }
            terminal_putc(w, buf[i]);
            i++;
        }
    }
    return changed;
}

static const char *terminal_line_for_display(iwin_t *w, int row)
{
    static char blank[TERM_MAX_COLS];
    int total = w->scrollback_count + w->term_rows;
    int first = total - w->term_rows - w->scroll_offset;
    int idx = first + row;

    if (idx < 0)
        return blank;
    if (idx < w->scrollback_count)
        return w->scrollback[idx];
    idx -= w->scrollback_count;
    if (idx >= 0 && idx < w->term_rows)
        return w->cells[idx];
    return blank;
}

int wlcomp_terminal_scroll_view(iwin_t *w, int delta)
{
    int next = w->scroll_offset + delta;
    if (next < 0) next = 0;
    if (next > w->scrollback_count) next = w->scrollback_count;
    if (next == w->scroll_offset)
        return 0;
    w->scroll_offset = next;
    return 1;
}

void wlcomp_terminal_draw(uint32_t *fb, int fb_w, int fb_h, iwin_t *w)
{
    int cx0 = w->x + 4;
    int cy0 = w->y + IWIN_TITLE_H + 2;
    int content_w = w->w - TERM_SCROLLBAR_W - 8;
    int content_h = w->h - IWIN_TITLE_H - 4;
    int sbx = cx0 + content_w;

    draw_rect(fb, fb_w, fb_h, cx0, cy0, content_w, content_h, 0xFF000000);
    draw_rect(fb, fb_w, fb_h, sbx, cy0, TERM_SCROLLBAR_W, content_h, 0xFF151B22);
    draw_rect(fb, fb_w, fb_h, sbx, cy0, 1, content_h, 0xFF2B3642);

    int total_lines = w->scrollback_count + w->term_rows;
    if (total_lines > w->term_rows && content_h > 8) {
        int thumb_h = content_h * w->term_rows / total_lines;
        if (thumb_h < 18) thumb_h = 18;
        if (thumb_h > content_h) thumb_h = content_h;
        int range = content_h - thumb_h;
        int thumb_y = cy0;
        if (range > 0)
            thumb_y += range - (range * w->scroll_offset) / w->scrollback_count;
        draw_rect(fb, fb_w, fb_h, sbx + 3, thumb_y, TERM_SCROLLBAR_W - 6,
                  thumb_h, 0xFF6B7786);
    } else {
        draw_rect(fb, fb_w, fb_h, sbx + 3, cy0 + 3, TERM_SCROLLBAR_W - 6,
                  content_h > 6 ? content_h - 6 : content_h, 0xFF39424D);
    }

    for (int row = 0; row < w->term_rows; row++) {
        const char *line = terminal_line_for_display(w, row);
        for (int col = 0; col < w->term_cols; col++) {
            char c = line[col];
            if (c > ' ' && c <= '~') {
                int px = cx0 + 2 + col * TERM_CELL_W;
                int py = cy0 + 2 + row * TERM_CELL_H;
                draw_char(fb, fb_w, fb_h, px, py, c, 0xFF00FF00, 1);
            }
        }
    }

    uint32_t t = terminal_time_ms();
    if (w->scroll_offset == 0 && ((t / 800) & 1)) {
        int cpx = cx0 + 2 + w->cur_col * TERM_CELL_W;
        int cpy = cy0 + 2 + w->cur_row * TERM_CELL_H;
        draw_rect(fb, fb_w, fb_h, cpx, cpy, TERM_CELL_W, TERM_CELL_H, 0xFF00FF00);
    }
}
