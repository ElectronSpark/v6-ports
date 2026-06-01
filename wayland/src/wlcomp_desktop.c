#include "wlcomp_desktop.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "wlcomp_draw.h"
#include "wlcomp_launcher.h"

#define DESKTOP_DIR        "/root/desktop"
#define DESKTOP_ICON_MAX   32
#define ICON_CELL_W        80
#define ICON_CELL_H        72
#define ICON_BOX_SIZE      42
#define ICON_GRID_X0       20
#define ICON_GRID_Y0       16

static desktop_icon_t g_icons[DESKTOP_ICON_MAX];
static int g_icon_count;
static int g_icons_laid_out;
static uint64_t g_desktop_signature;
static int g_desktop_signature_valid;

static char *desktop_trim_space(char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
        s++;
    char *end = s + strlen(s);
    while (end > s &&
           (end[-1] == ' ' || end[-1] == '\t' ||
            end[-1] == '\r' || end[-1] == '\n')) {
        *--end = '\0';
    }
    return s;
}

static int shortcut_exec_allowed(const char *path)
{
    return path && path[0] && access(path, X_OK) == 0;
}

static void copy_shortcut_field(char *dst, size_t dst_size, const char *src)
{
    size_t i = 0;

    if (dst_size == 0)
        return;
    if (!src)
        src = "";
    while (i + 1 < dst_size && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void shortcut_set(desktop_icon_t *sc, const char *label, int action,
                         const char *exec_path, const char *exec_name,
                         const char *exec_arg, uint32_t color, char symbol)
{
    memset(sc, 0, sizeof(*sc));
    copy_shortcut_field(sc->label, sizeof(sc->label), label);
    copy_shortcut_field(sc->exec_path, sizeof(sc->exec_path), exec_path);
    copy_shortcut_field(sc->exec_name, sizeof(sc->exec_name), exec_name);
    copy_shortcut_field(sc->exec_arg, sizeof(sc->exec_arg), exec_arg);
    sc->action = action;
    sc->icon_color = color;
    sc->symbol = symbol ? symbol : '?';
    if (exec_path && exec_path[0])
        sc->has_bitmap_icon =
            xv6_icon_load_from_elf(exec_path, &sc->bitmap_icon) == 0;
}

static int shortcut_action_from_name(const char *name)
{
    if (strcmp(name, "terminal") == 0) return SHORTCUT_TERMINAL;
    if (strcmp(name, "files") == 0) return SHORTCUT_FILES;
    if (strcmp(name, "sysinfo") == 0) return SHORTCUT_SYSINFO;
    if (strcmp(name, "calc") == 0) return SHORTCUT_CALC;
    if (strcmp(name, "network") == 0) return SHORTCUT_NETWORK;
    if (strcmp(name, "settings") == 0) return SHORTCUT_SETTINGS;
    if (strcmp(name, "monitor") == 0) return SHORTCUT_MONITOR;
    if (strcmp(name, "3ddemo") == 0) return SHORTCUT_3DDEMO;
    if (strcmp(name, "editor") == 0) return SHORTCUT_EDITOR;
    return SHORTCUT_EXEC;
}

static void desktop_copy_unquoted(char *dst, size_t dst_size,
                                  const char *start, const char *end)
{
    size_t i = 0;

    if (dst_size == 0)
        return;
    while (start < end && i + 1 < dst_size)
        dst[i++] = *start++;
    dst[i] = '\0';
}

static void split_exec_command(const char *cmd, char *path, size_t path_size,
                               char *arg, size_t arg_size)
{
    const char *s = cmd ? cmd : "";

    copy_shortcut_field(path, path_size, "");
    copy_shortcut_field(arg, arg_size, "");

    while (*s == ' ' || *s == '\t')
        s++;
    if (!*s)
        return;

    if (*s == '\'' || *s == '"') {
        char quote = *s++;
        const char *start = s;
        while (*s && *s != quote)
            s++;
        desktop_copy_unquoted(path, path_size, start, s);
        if (*s == quote)
            s++;
    } else {
        const char *start = s;
        while (*s && *s != ' ' && *s != '\t')
            s++;
        desktop_copy_unquoted(path, path_size, start, s);
    }

    while (*s == ' ' || *s == '\t')
        s++;
    copy_shortcut_field(arg, arg_size, s);
}

int wlcomp_desktop_shortcut_parse(const char *path, desktop_icon_t *out)
{
    FILE *fp = fopen(path, "r");
    if (!fp)
        return -1;

    char name[48] = "";
    char exec_cmd[256] = "";
    char exec_path[256] = "";
    char exec_arg[256] = "";
    char arg[256] = "";
    char builtin[32] = "";
    uint32_t color = 0xFF5A7090;
    char symbol = 'A';
    char line[320];

    while (fgets(line, sizeof(line), fp)) {
        char *s = desktop_trim_space(line);
        if (!s[0] || s[0] == '#' || s[0] == '[')
            continue;
        char *eq = strchr(s, '=');
        if (!eq)
            continue;
        *eq++ = '\0';
        char *key = desktop_trim_space(s);
        char *val = desktop_trim_space(eq);
        if (strcmp(key, "Name") == 0)
            snprintf(name, sizeof(name), "%s", val);
        else if (strcmp(key, "Exec") == 0)
            snprintf(exec_cmd, sizeof(exec_cmd), "%s", val);
        else if (strcmp(key, "Arg") == 0)
            snprintf(arg, sizeof(arg), "%s", val);
        else if (strcmp(key, "X-XV6-Builtin") == 0)
            snprintf(builtin, sizeof(builtin), "%s", val);
        else if (strcmp(key, "IconChar") == 0 && val[0])
            symbol = val[0];
        else if (strcmp(key, "IconColor") == 0 && val[0])
            color = (uint32_t)strtoul(val, NULL, 0);
    }

    fclose(fp);

    if (!name[0])
        snprintf(name, sizeof(name), "%s", wlcomp_path_basename(path));

    int action = builtin[0] ? shortcut_action_from_name(builtin) : SHORTCUT_EXEC;
    if (exec_cmd[0])
        split_exec_command(exec_cmd, exec_path, sizeof(exec_path),
                           exec_arg, sizeof(exec_arg));
    if (arg[0])
        copy_shortcut_field(exec_arg, sizeof(exec_arg), arg);
    if (action == SHORTCUT_EXEC && !exec_path[0])
        return -1;
    if (action == SHORTCUT_EXEC && !shortcut_exec_allowed(exec_path))
        return -1;

    shortcut_set(out, name, action, exec_path,
                 exec_path[0] ? wlcomp_path_basename(exec_path) : "",
                 exec_arg[0] ? exec_arg : NULL, color, symbol);
    return 0;
}

static uint32_t icon_color_for_path(const char *name, mode_t mode)
{
    if (S_ISDIR(mode))
        return 0xFFA67C52;
    if (mode & (S_IXUSR | S_IXGRP | S_IXOTH))
        return 0xFF5A7090;
    if (wlcomp_path_is_html(name))
        return 0xFF3D6E9E;
    if (wlcomp_path_is_text(name))
        return 0xFFA65C3D;
    return 0xFF607080;
}

static char icon_symbol_for_path(const char *name, mode_t mode)
{
    if (S_ISDIR(mode))
        return 'D';
    if (mode & (S_IXUSR | S_IXGRP | S_IXOTH))
        return 'X';
    if (wlcomp_path_is_html(name))
        return 'W';
    if (wlcomp_path_is_text(name))
        return 'T';
    return 'F';
}

static int icon_compare_label(const void *a, const void *b)
{
    const desktop_icon_t *ia = (const desktop_icon_t *)a;
    const desktop_icon_t *ib = (const desktop_icon_t *)b;
    return strcmp(ia->label, ib->label);
}

static int ensure_desktop_dir(void)
{
    struct stat st;

    if (stat(DESKTOP_DIR, &st) == 0)
        return S_ISDIR(st.st_mode) ? 0 : -1;

    if (mkdir("/root", 0755) != 0 && errno != EEXIST)
        return -1;
    if (mkdir(DESKTOP_DIR, 0755) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

static uint64_t desktop_hash_bytes(uint64_t hash, const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;

    for (size_t i = 0; i < len; i++) {
        hash ^= p[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static uint64_t desktop_hash_u64(uint64_t hash, uint64_t value)
{
    return desktop_hash_bytes(hash, &value, sizeof(value));
}

static uint64_t desktop_hash_file_prefix(uint64_t hash, const char *path)
{
    unsigned char buf[256];
    size_t total = 0;
    FILE *fp = fopen(path, "r");

    if (!fp)
        return hash;
    while (total < 4096) {
        size_t want = sizeof(buf);
        if (want > 4096 - total)
            want = 4096 - total;
        size_t n = fread(buf, 1, want, fp);
        if (n == 0)
            break;
        hash = desktop_hash_bytes(hash, buf, n);
        total += n;
    }
    fclose(fp);
    return hash;
}

static uint64_t desktop_state_signature(void)
{
    uint64_t hash = 1469598103934665603ULL;
    DIR *dir;
    struct dirent *de;
    uint64_t count = 0;

    if (ensure_desktop_dir() != 0)
        return 0;

    dir = opendir(DESKTOP_DIR);
    if (!dir)
        return 0;

    while ((de = readdir(dir)) != NULL) {
        char full[320];
        struct stat st;

        if (!de->d_name[0] || de->d_name[0] == '.')
            continue;
        snprintf(full, sizeof(full), "%s/%s", DESKTOP_DIR, de->d_name);
        if (stat(full, &st) != 0)
            continue;
        hash = desktop_hash_bytes(hash, de->d_name, strlen(de->d_name));
        hash = desktop_hash_u64(hash, (uint64_t)st.st_mode);
        hash = desktop_hash_u64(hash, (uint64_t)st.st_size);
        hash = desktop_hash_u64(hash, (uint64_t)st.st_mtime);
        hash = desktop_hash_u64(hash, (uint64_t)st.st_ctime);
        if (S_ISREG(st.st_mode) &&
            wlcomp_path_has_suffix(de->d_name, ".desktop"))
            hash = desktop_hash_file_prefix(hash, full);
        count++;
    }
    closedir(dir);

    return desktop_hash_u64(hash, count);
}

static int desktop_dir_changed(void)
{
    uint64_t signature = desktop_state_signature();

    if (g_desktop_signature_valid && g_desktop_signature == signature)
        return 0;
    g_desktop_signature = signature;
    g_desktop_signature_valid = 1;
    return 1;
}

static void add_path_icon(const char *full, const char *name,
                          const struct stat *st)
{
    int action;

    if (g_icon_count >= DESKTOP_ICON_MAX)
        return;

    if (S_ISREG(st->st_mode) && wlcomp_path_has_suffix(name, ".desktop")) {
        desktop_icon_t sc;
        if (wlcomp_desktop_shortcut_parse(full, &sc) == 0)
            g_icons[g_icon_count++] = sc;
        return;
    }

    if (S_ISREG(st->st_mode) &&
        (st->st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)))
        action = SHORTCUT_EXEC;
    else if (S_ISREG(st->st_mode) || S_ISDIR(st->st_mode))
        action = SHORTCUT_FILE;
    else
        return;

    shortcut_set(&g_icons[g_icon_count++], name, action,
                 full, action == SHORTCUT_EXEC ? name : "", NULL,
                 icon_color_for_path(name, st->st_mode),
                 icon_symbol_for_path(name, st->st_mode));
}

static void load_desktop_shortcuts(void)
{
    static int logged_state;

    if (!desktop_dir_changed())
        return;

    g_icon_count = 0;
    g_icons_laid_out = 0;

    DIR *dir = opendir(DESKTOP_DIR);
    if (dir) {
        struct dirent *de;
        while ((de = readdir(dir)) != NULL && g_icon_count < DESKTOP_ICON_MAX) {
            if (!de->d_name[0] || de->d_name[0] == '.')
                continue;

            char full[320];
            snprintf(full, sizeof(full), "%s/%s", DESKTOP_DIR, de->d_name);

            struct stat st;
            if (stat(full, &st) != 0)
                continue;

            add_path_icon(full, de->d_name, &st);
        }
        closedir(dir);
    }

    if (g_icon_count > 1)
        qsort(g_icons, (size_t)g_icon_count, sizeof(g_icons[0]),
              icon_compare_label);

    if (g_icon_count == 0) {
        static const struct {
            const char *label;
            int action;
            const char *exec_path;
            const char *exec_name;
            const char *exec_arg;
            uint32_t color;
            char symbol;
        } defaults[] = {
            { "3D Demo", SHORTCUT_3DDEMO, "", "", "", 0xFF6EAA3D, '3' },
            { "Browser", SHORTCUT_EXEC, "/bin/netsurf", "netsurf", "",
              0xFF3D6EA6, 'W' },
            { "Calc", SHORTCUT_CALC, "", "", "", 0xFF8556B5, 'C' },
            { "EGL Demo", SHORTCUT_EXEC, "/bin/mesawlegl", "mesawlegl", "",
              0xFFA67C3D, 'E' },
            { "Editor", SHORTCUT_EDITOR, "", "", "", 0xFFA65C3D, 'E' },
            { "Files", SHORTCUT_FILES, "", "", "", 0xFFA67C52, 'F' },
            { "GL Maze", SHORTCUT_EXEC, "/bin/glmaze", "glmaze", "",
              0xFF3DA67B, 'G' },
            { "GL Smoke", SHORTCUT_EXEC, "/bin/glsmoke", "glsmoke", "",
              0xFF7B3DA6, 'G' },
            { "GL Sphere", SHORTCUT_EXEC, "/bin/mesawlegl", "mesawlegl", "--demo",
              0xFF3D6E9E, 'S' },
            { "Game Boy", SHORTCUT_EXEC, "/bin/peanutgb", "peanutgb", "",
              0xFFA63D83, 'G' },
            { "Info", SHORTCUT_SYSINFO, "", "", "", 0xFF3DA66E, 'i' },
            { "Monitor", SHORTCUT_MONITOR, "", "", "", 0xFFA63D91, 'M' },
            { "Network", SHORTCUT_NETWORK, "", "", "", 0xFF3DA6A6, 'N' },
            { "Settings", SHORTCUT_SETTINGS, "", "", "", 0xFF707070, 'S' },
            { "Terminal", SHORTCUT_TERMINAL, "", "", "", 0xFF5A7090, '>' },
            { "WebKit", SHORTCUT_EXEC, "/libexec/webkit2gtk-4.1/MiniBrowser",
              "MiniBrowser", "", 0xFFA65CB5, 'W' },
        };

        for (size_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]) &&
             g_icon_count < DESKTOP_ICON_MAX; i++) {
            if (defaults[i].action == SHORTCUT_EXEC &&
                !shortcut_exec_allowed(defaults[i].exec_path))
                continue;
            shortcut_set(&g_icons[g_icon_count++], defaults[i].label,
                         defaults[i].action, defaults[i].exec_path,
                         defaults[i].exec_name, defaults[i].exec_arg,
                         defaults[i].color, defaults[i].symbol);
        }
        fprintf(stderr,
                "wlcomp: desktop shortcuts directory empty; using %d built-in launchers\n",
                g_icon_count);
        logged_state = 1;
    } else if (!logged_state) {
        fprintf(stderr, "wlcomp: desktop shortcuts loaded count=%d\n",
                g_icon_count);
        logged_state = 1;
    }
}

int wlcomp_desktop_icon_count(void)
{
    load_desktop_shortcuts();
    return g_icon_count;
}

const desktop_icon_t *wlcomp_desktop_icon_at(int idx)
{
    load_desktop_shortcuts();
    if (idx < 0 || idx >= g_icon_count)
        return NULL;
    return &g_icons[idx];
}

void wlcomp_desktop_invalidate_layout(void)
{
    g_icons_laid_out = 0;
}

void wlcomp_desktop_layout_icons(int fb_w, int fb_h)
{
    (void)fb_h;
    load_desktop_shortcuts();
    if (g_icons_laid_out)
        return;
    int cols = (fb_w - ICON_GRID_X0 * 2) / ICON_CELL_W;
    if (cols < 1) cols = 1;
    for (int i = 0; i < g_icon_count; i++) {
        int col = i % cols;
        int row = i / cols;
        g_icons[i].x = ICON_GRID_X0 + col * ICON_CELL_W;
        g_icons[i].y = ICON_GRID_Y0 + row * ICON_CELL_H;
        g_icons[i].w = ICON_CELL_W;
        g_icons[i].h = ICON_CELL_H;
    }
    g_icons_laid_out = 1;
}

void wlcomp_desktop_draw_wallpaper(uint32_t *fb, int fb_w, int fb_h)
{
    int x0 = 0;
    int y0 = 0;
    int x1 = fb_w;
    int y1 = fb_h;

    if (!wlcomp_draw_clip_xyxy(fb_w, fb_h, &x0, &y0, &x1, &y1))
        return;

    for (int y = y0; y < y1; y++) {
        int frac = y * 1000 / (fb_h > 1 ? fb_h - 1 : 1);
        uint8_t r, g, b;
        if (frac < 600) {
            r = (uint8_t)(12 + (30 - 12) * frac / 600);
            g = (uint8_t)(20 + (60 - 20) * frac / 600);
            b = (uint8_t)(48 + (110 - 48) * frac / 600);
        } else if (frac < 750) {
            int f2 = frac - 600;
            r = (uint8_t)(30 + (20 - 30) * f2 / 150);
            g = (uint8_t)(60 + (80 - 60) * f2 / 150);
            b = (uint8_t)(110 + (100 - 110) * f2 / 150);
        } else {
            int f3 = frac - 750;
            r = (uint8_t)(20 + (16 - 20) * f3 / 250);
            g = (uint8_t)(80 + (40 - 80) * f3 / 250);
            b = (uint8_t)(100 + (65 - 100) * f3 / 250);
        }
        uint32_t *row_p = fb + y * fb_w;
        for (int x = x0; x < x1; x++) {
            int noise = ((x * 7 + y * 13) & 7) - 4;
            int rn = (int)r + noise; if (rn < 0) rn = 0; if (rn > 255) rn = 255;
            int gn = (int)g + noise; if (gn < 0) gn = 0; if (gn > 255) gn = 255;
            int bn = (int)b + noise; if (bn < 0) bn = 0; if (bn > 255) bn = 255;
            row_p[x] = 0xFF000000 | ((uint32_t)rn << 16) |
                       ((uint32_t)gn << 8) | (uint32_t)bn;
        }
    }
}

static void draw_icon(uint32_t *fb, int fb_w, int fb_h, desktop_icon_t *ico,
                      int selected)
{
    int bx = ico->x + (ICON_CELL_W - ICON_BOX_SIZE) / 2;
    int by = ico->y + 2;

    if (selected)
        draw_rounded_rect(fb, fb_w, fb_h, ico->x + 2, ico->y,
                          ICON_CELL_W - 4, ICON_CELL_H, 4, 0xFF3C5078);

    draw_rounded_rect(fb, fb_w, fb_h, bx, by,
                      ICON_BOX_SIZE, ICON_BOX_SIZE, 6, ico->icon_color);

    if (ico->has_bitmap_icon) {
        xv6_icon_draw(fb, fb_w, fb_h, bx + 5, by + 5,
                      ICON_BOX_SIZE - 10, ICON_BOX_SIZE - 10,
                      &ico->bitmap_icon);
    } else {
        int sx = bx + (ICON_BOX_SIZE - 8 * 2) / 2;
        int sy = by + (ICON_BOX_SIZE - 16 * 2) / 2;

        draw_char(fb, fb_w, fb_h, sx, sy, ico->symbol, 0xFFFFFFFF, 2);
    }

    int lw = string_pixel_width(ico->label, 1);
    int lx = ico->x + (ICON_CELL_W - lw) / 2;
    int ly = by + ICON_BOX_SIZE + 4;
    draw_string(fb, fb_w, fb_h, lx, ly, ico->label, 0xFFD2DAE2, 1);
}

void wlcomp_desktop_draw_icons(uint32_t *fb, int fb_w, int fb_h,
                               int selected_icon)
{
    load_desktop_shortcuts();
    for (int i = 0; i < g_icon_count; i++)
        draw_icon(fb, fb_w, fb_h, &g_icons[i], i == selected_icon);
}

int wlcomp_desktop_icon_hit_test(int mx, int my)
{
    load_desktop_shortcuts();
    for (int i = 0; i < g_icon_count; i++) {
        if (mx >= g_icons[i].x && mx < g_icons[i].x + g_icons[i].w &&
            my >= g_icons[i].y && my < g_icons[i].y + g_icons[i].h)
            return i;
    }
    return -1;
}
