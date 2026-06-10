/*
 * filemgr.c - standalone xv6 Wayland file manager.
 *
 * This is intentionally toolkit-free so it can run in the small xv6 GUI stack.
 * It provides a familiar list-view file manager: navigation, open, refresh,
 * new folder, rename, delete, copy, cut, paste, sorting, and text preview.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#include "xv6_draw.h"
#include "xv6_icon.h"
#include "xdg-shell-client-protocol.h"
#include "xv6_present_buffer.h"

#ifndef PATH_MAX
#define PATH_MAX 512
#endif

#define APP_W 760
#define APP_H 550
#define MAX_ENTRIES 256
#define MAX_BUTTONS 12
#define TITLEBAR_H 30
#define TITLE_BUTTON_W 26
#define TITLE_BUTTON_GAP 4
#define TOOLBAR_H 34
#define PATH_H 24
#define HEADER_H 22
#define ROW_H 22
#define STATUS_H 24
#define SIDEBAR_W 96
#define LIST_X (SIDEBAR_W + 8)
#define MODAL_TEXT_MAX 256
#define PREVIEW_MAX 2048

enum file_type {
    FT_FILE,
    FT_DIR,
    FT_EXEC,
    FT_DESKTOP,
    FT_HTML,
    FT_TEXT,
};

enum action {
    ACT_UP,
    ACT_HOME,
    ACT_ROOT,
    ACT_REFRESH,
    ACT_NEW_FOLDER,
    ACT_RENAME,
    ACT_DELETE,
    ACT_COPY,
    ACT_CUT,
    ACT_PASTE,
    ACT_OPEN,
    ACT_SORT,
};

enum modal_kind {
    MODAL_NONE,
    MODAL_NEW_FOLDER,
    MODAL_RENAME,
    MODAL_DELETE,
    MODAL_PREVIEW,
};

enum sort_mode {
    SORT_NAME,
    SORT_TYPE,
    SORT_SIZE,
};

enum title_action {
    TITLE_ACTION_NONE,
    TITLE_ACTION_MINIMIZE,
    TITLE_ACTION_MAXIMIZE,
    TITLE_ACTION_CLOSE,
};

struct entry {
    char name[128];
    char path[PATH_MAX];
    enum file_type type;
    mode_t mode;
    off_t size;
    time_t mtime;
    int has_icon;
    struct xv6_icon icon;
};

struct button {
    enum action action;
    const char *label;
    int x;
    int y;
    int w;
    int h;
    int enabled;
};

struct app {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_proxy *gpu_manager;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct wl_keyboard *keyboard;
    struct wl_surface *surface;
    struct xdg_wm_base *wm_base;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct xv6_present_buffer buffer;
    int width;
    int height;
    int pending_width;
    int pending_height;
    int configured;
    int running;
    int dirty;
    int maximized;
    int pointer_x;
    int pointer_y;
    uint32_t last_click_ms;
    int last_click_entry;
    uint32_t mods;

    char cwd[PATH_MAX];
    struct entry entries[MAX_ENTRIES];
    int entry_count;
    int selected;
    int scroll;
    enum sort_mode sort;
    int sort_desc;

    struct button buttons[MAX_BUTTONS];
    int button_count;
    char status[192];
    char clipboard[PATH_MAX];
    int clipboard_cut;

    enum modal_kind modal;
    char modal_title[64];
    char modal_text[MODAL_TEXT_MAX];
    char preview[PREVIEW_MAX];
};

static int load_directory(struct app *app, const char *path);
static int parse_desktop_exec(const char *path, char *exec_path,
                              size_t exec_path_sz, char *exec_arg,
                              size_t exec_arg_sz);

static uint32_t now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

static const char *base_name(const char *path)
{
    const char *slash;

    if (!path || !path[0])
        return "";
    slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static int has_suffix(const char *name, const char *suffix)
{
    size_t ln;
    size_t ls;

    if (!name || !suffix)
        return 0;
    ln = strlen(name);
    ls = strlen(suffix);
    return ln >= ls && strcmp(name + ln - ls, suffix) == 0;
}

static int is_text_name(const char *name)
{
    return has_suffix(name, ".txt") || has_suffix(name, ".log") ||
           has_suffix(name, ".md") || has_suffix(name, ".c") ||
           has_suffix(name, ".h") || has_suffix(name, ".sh") ||
           has_suffix(name, ".ini") || has_suffix(name, ".conf");
}

static int is_html_name(const char *name)
{
    return has_suffix(name, ".html") || has_suffix(name, ".htm");
}

static void path_join(char *out, size_t out_sz, const char *dir,
                      const char *name)
{
    if (!dir || strcmp(dir, "/") == 0)
        snprintf(out, out_sz, "/%s", name ? name : "");
    else
        snprintf(out, out_sz, "%s/%s", dir, name ? name : "");
}

static void copy_text(char *dst, size_t dst_sz, const char *src)
{
    size_t i = 0;

    if (dst_sz == 0)
        return;
    if (!src)
        src = "";
    while (i + 1 < dst_sz && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int path_is_same_or_child(const char *path, const char *parent)
{
    size_t n;

    if (!path || !parent || !path[0] || !parent[0])
        return 0;
    n = strlen(parent);
    if (strcmp(parent, "/") == 0)
        return path[0] == '/';
    return strncmp(path, parent, n) == 0 &&
        (path[n] == '\0' || path[n] == '/');
}

static void set_status(struct app *app, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(app->status, sizeof(app->status), fmt, ap);
    va_end(ap);
    app->dirty = 1;
}

static void reload_with_status(struct app *app, const char *message)
{
    char path[PATH_MAX];

    copy_text(path, sizeof(path), app->cwd);
    load_directory(app, path);
    set_status(app, "%s", message);
}

static enum file_type classify_entry(const char *name, const struct stat *st)
{
    if (S_ISDIR(st->st_mode))
        return FT_DIR;
    if (has_suffix(name, ".desktop"))
        return FT_DESKTOP;
    if (is_html_name(name))
        return FT_HTML;
    if (is_text_name(name))
        return FT_TEXT;
    if (st->st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))
        return FT_EXEC;
    return FT_FILE;
}

static const char *type_label(enum file_type type)
{
    switch (type) {
    case FT_DIR: return "Folder";
    case FT_EXEC: return "Program";
    case FT_DESKTOP: return "Shortcut";
    case FT_HTML: return "Web page";
    case FT_TEXT: return "Text";
    case FT_FILE:
    default: return "File";
    }
}

static char type_glyph(enum file_type type)
{
    switch (type) {
    case FT_DIR: return 'D';
    case FT_EXEC: return 'X';
    case FT_DESKTOP: return 'A';
    case FT_HTML: return 'W';
    case FT_TEXT: return 'T';
    case FT_FILE:
    default: return 'F';
    }
}

static uint32_t type_color(enum file_type type)
{
    switch (type) {
    case FT_DIR: return 0xFFA67C52;
    case FT_EXEC: return 0xFF5A7090;
    case FT_DESKTOP: return 0xFF6EA63D;
    case FT_HTML: return 0xFF3D6E9E;
    case FT_TEXT: return 0xFFA65C3D;
    case FT_FILE:
    default: return 0xFF607080;
    }
}

static int entry_compare(const void *va, const void *vb)
{
    const struct entry *a = va;
    const struct entry *b = vb;
    int r;

    if (a->type == FT_DIR && b->type != FT_DIR)
        return -1;
    if (a->type != FT_DIR && b->type == FT_DIR)
        return 1;
    r = strcasecmp(a->name, b->name);
    return r;
}

static void sort_entries(struct app *app)
{
    qsort(app->entries, (size_t)app->entry_count, sizeof(app->entries[0]),
          entry_compare);

    if (app->sort == SORT_NAME && !app->sort_desc)
        return;

    for (int i = 0; i < app->entry_count - 1; i++) {
        for (int j = i + 1; j < app->entry_count; j++) {
            struct entry *a = &app->entries[i];
            struct entry *b = &app->entries[j];
            int cmp = 0;

            if (a->type == FT_DIR && b->type != FT_DIR)
                continue;
            if (a->type != FT_DIR && b->type == FT_DIR)
                continue;
            if (app->sort == SORT_TYPE)
                cmp = strcmp(type_label(a->type), type_label(b->type));
            else if (app->sort == SORT_SIZE)
                cmp = (a->size > b->size) - (a->size < b->size);
            else
                cmp = strcasecmp(a->name, b->name);
            if (app->sort_desc)
                cmp = -cmp;
            if (cmp > 0) {
                struct entry tmp = *a;
                *a = *b;
                *b = tmp;
            }
        }
    }
}

static int load_directory(struct app *app, const char *path)
{
    DIR *dir;
    struct dirent *de;

    dir = opendir(path);
    if (!dir) {
        set_status(app, "Cannot open %s: %s", path, strerror(errno));
        return -1;
    }

    snprintf(app->cwd, sizeof(app->cwd), "%s", path);
    app->entry_count = 0;
    app->selected = -1;
    app->scroll = 0;

    while ((de = readdir(dir)) != NULL && app->entry_count < MAX_ENTRIES) {
        struct entry *e;
        struct stat st;

        if (!de->d_name[0] || strcmp(de->d_name, ".") == 0)
            continue;
        e = &app->entries[app->entry_count];
        memset(e, 0, sizeof(*e));
        copy_text(e->name, sizeof(e->name), de->d_name);
        path_join(e->path, sizeof(e->path), app->cwd, e->name);
        if (stat(e->path, &st) != 0)
            continue;
        e->mode = st.st_mode;
        e->size = st.st_size;
        e->mtime = st.st_mtime;
        e->type = classify_entry(e->name, &st);
        if (e->type == FT_EXEC) {
            e->has_icon = xv6_icon_load_from_elf(e->path, &e->icon) == 0;
        } else if (e->type == FT_DESKTOP) {
            char exec_path[PATH_MAX];
            char exec_arg[PATH_MAX];

            if (parse_desktop_exec(e->path, exec_path, sizeof(exec_path),
                                   exec_arg, sizeof(exec_arg)) == 0)
                e->has_icon =
                    xv6_icon_load_from_elf(exec_path, &e->icon) == 0;
        }
        app->entry_count++;
    }
    closedir(dir);
    sort_entries(app);
    set_status(app, "%d items in %s", app->entry_count, app->cwd);
    return 0;
}

static void navigate_parent(struct app *app)
{
    char tmp[PATH_MAX];
    char *slash;

    if (strcmp(app->cwd, "/") == 0)
        return;
    snprintf(tmp, sizeof(tmp), "%s", app->cwd);
    slash = strrchr(tmp, '/');
    if (slash && slash != tmp)
        *slash = '\0';
    else
        snprintf(tmp, sizeof(tmp), "/");
    load_directory(app, tmp);
}

static int copy_file(const char *src, const char *dst, mode_t mode)
{
    int in_fd = open(src, O_RDONLY);
    int out_fd;
    char buf[4096];
    ssize_t n;

    if (in_fd < 0)
        return -1;
    out_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, mode & 0777);
    if (out_fd < 0) {
        close(in_fd);
        return -1;
    }
    while ((n = read(in_fd, buf, sizeof(buf))) > 0) {
        char *p = buf;
        ssize_t left = n;

        while (left > 0) {
            ssize_t w = write(out_fd, p, (size_t)left);

            if (w <= 0) {
                close(in_fd);
                close(out_fd);
                return -1;
            }
            p += w;
            left -= w;
        }
    }
    close(in_fd);
    close(out_fd);
    return n == 0 ? 0 : -1;
}

static int copy_tree(const char *src, const char *dst)
{
    struct stat st;

    if (stat(src, &st) != 0)
        return -1;
    if (!S_ISDIR(st.st_mode))
        return copy_file(src, dst, st.st_mode);

    if (mkdir(dst, st.st_mode & 0777) != 0 && errno != EEXIST)
        return -1;

    DIR *dir = opendir(src);
    struct dirent *de;

    if (!dir)
        return -1;
    while ((de = readdir(dir)) != NULL) {
        char child_src[PATH_MAX];
        char child_dst[PATH_MAX];

        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        path_join(child_src, sizeof(child_src), src, de->d_name);
        path_join(child_dst, sizeof(child_dst), dst, de->d_name);
        if (copy_tree(child_src, child_dst) != 0) {
            closedir(dir);
            return -1;
        }
    }
    closedir(dir);
    return 0;
}

static int remove_tree(const char *path)
{
    struct stat st;

    if (stat(path, &st) != 0)
        return -1;
    if (!S_ISDIR(st.st_mode))
        return unlink(path);

    DIR *dir = opendir(path);
    struct dirent *de;

    if (!dir)
        return -1;
    while ((de = readdir(dir)) != NULL) {
        char child[PATH_MAX];

        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        path_join(child, sizeof(child), path, de->d_name);
        if (remove_tree(child) != 0) {
            closedir(dir);
            return -1;
        }
    }
    closedir(dir);
    return rmdir(path);
}

static void unique_dest(char *out, size_t out_sz, const char *dir,
                        const char *name)
{
    char stem[128];
    char ext[64] = "";
    char *dot;

    path_join(out, out_sz, dir, name);
    if (access(out, F_OK) != 0)
        return;

    copy_text(stem, sizeof(stem), name);
    dot = strrchr(stem, '.');
    if (dot && dot != stem) {
        snprintf(ext, sizeof(ext), "%s", dot);
        *dot = '\0';
    }
    for (int i = 1; i < 1000; i++) {
        char candidate[192];

        snprintf(candidate, sizeof(candidate), "%s copy%s%s",
                 stem, i == 1 ? "" : " ", i == 1 ? ext : "");
        if (i != 1)
            snprintf(candidate, sizeof(candidate), "%s copy %d%s",
                     stem, i, ext);
        path_join(out, out_sz, dir, candidate);
        if (access(out, F_OK) != 0)
            return;
    }
}

static void launch_path(const char *path, const char *arg)
{
    pid_t pid = fork();

    if (pid < 0)
        return;
    if (pid == 0) {
        setpgid(0, 0);
        int logfd = open("/tmp/filemgr-app.log",
                         O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (logfd >= 0) {
            dup2(logfd, 1);
            dup2(logfd, 2);
            close(logfd);
        }
        if (arg)
            execl(path, base_name(path), arg, NULL);
        else
            execl(path, base_name(path), NULL);
        _exit(127);
    }
    waitpid(pid, NULL, WNOHANG);
}

static void reap_children(void)
{
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
}

static int parse_desktop_exec(const char *path, char *exec_path,
                              size_t exec_path_sz, char *exec_arg,
                              size_t exec_arg_sz)
{
    FILE *fp = fopen(path, "r");
    char line[320];
    char exec_cmd[256] = "";

    if (!fp)
        return -1;
    while (fgets(line, sizeof(line), fp)) {
        char *s = line;
        char *eq;

        while (*s == ' ' || *s == '\t')
            s++;
        if (strncmp(s, "Exec=", 5) != 0)
            continue;
        eq = s + 5;
        snprintf(exec_cmd, sizeof(exec_cmd), "%s", eq);
        break;
    }
    fclose(fp);
    if (!exec_cmd[0])
        return -1;

    char *s = exec_cmd;
    while (*s == ' ' || *s == '\t')
        s++;
    char *end = s;
    if (*s == '"' || *s == '\'') {
        char q = *s++;
        end = s;
        while (*end && *end != q)
            end++;
    } else {
        while (*end && *end != ' ' && *end != '\t' &&
               *end != '\r' && *end != '\n')
            end++;
    }
    snprintf(exec_path, exec_path_sz, "%.*s", (int)(end - s), s);
    s = end;
    if (*s == '"' || *s == '\'')
        s++;
    while (*s == ' ' || *s == '\t')
        s++;
    char *nl = strpbrk(s, "\r\n");
    if (nl)
        *nl = '\0';
    snprintf(exec_arg, exec_arg_sz, "%s", s);
    return exec_path[0] ? 0 : -1;
}

static void open_preview(struct app *app, const struct entry *e)
{
    int fd = open(e->path, O_RDONLY);
    ssize_t n;

    if (fd < 0) {
        set_status(app, "Cannot preview %s", e->name);
        return;
    }
    n = read(fd, app->preview, sizeof(app->preview) - 1);
    close(fd);
    if (n < 0) {
        set_status(app, "Cannot read %s", e->name);
        return;
    }
    app->preview[n] = '\0';
    app->modal = MODAL_PREVIEW;
    snprintf(app->modal_title, sizeof(app->modal_title), "%s", e->name);
    app->modal_text[0] = '\0';
    app->dirty = 1;
}

static void open_selected(struct app *app)
{
    struct entry *e;

    if (app->selected < 0 || app->selected >= app->entry_count)
        return;
    e = &app->entries[app->selected];
    if (e->type == FT_DIR) {
        load_directory(app, e->path);
    } else if (e->type == FT_HTML) {
        char url[PATH_MAX + 8];

        snprintf(url, sizeof(url), "file://%s", e->path);
        launch_path("/bin/netsurf", url);
        set_status(app, "Opened %s in browser", e->name);
    } else if (e->type == FT_DESKTOP) {
        char exec_path[PATH_MAX];
        char exec_arg[PATH_MAX];

        if (parse_desktop_exec(e->path, exec_path, sizeof(exec_path),
                               exec_arg, sizeof(exec_arg)) == 0) {
            launch_path(exec_path, exec_arg[0] ? exec_arg : NULL);
            set_status(app, "Launched %s", e->name);
        } else {
            set_status(app, "Shortcut has no Exec line");
        }
    } else if (e->type == FT_EXEC) {
        launch_path(e->path, NULL);
        set_status(app, "Launched %s", e->name);
    } else if (e->type == FT_TEXT) {
        open_preview(app, e);
    } else {
        set_status(app, "No default application for %s", e->name);
    }
}

static void modal_begin(struct app *app, enum modal_kind kind,
                        const char *title, const char *initial)
{
    app->modal = kind;
    snprintf(app->modal_title, sizeof(app->modal_title), "%s", title);
    snprintf(app->modal_text, sizeof(app->modal_text), "%s",
             initial ? initial : "");
    app->dirty = 1;
}

static void modal_accept(struct app *app)
{
    if (app->modal == MODAL_NEW_FOLDER) {
        char path[PATH_MAX];
        char msg[MODAL_TEXT_MAX + 32];

        if (!app->modal_text[0])
            return;
        path_join(path, sizeof(path), app->cwd, app->modal_text);
        if (mkdir(path, 0755) == 0)
            snprintf(msg, sizeof(msg), "Created folder %s", app->modal_text);
        else
            snprintf(msg, sizeof(msg), "Create failed: %s", strerror(errno));
        reload_with_status(app, msg);
    } else if (app->modal == MODAL_RENAME) {
        char new_path[PATH_MAX];
        char msg[MODAL_TEXT_MAX + 32];
        struct entry *e;

        if (app->selected < 0 || app->selected >= app->entry_count ||
            !app->modal_text[0])
            return;
        e = &app->entries[app->selected];
        path_join(new_path, sizeof(new_path), app->cwd, app->modal_text);
        if (rename(e->path, new_path) == 0)
            snprintf(msg, sizeof(msg), "Renamed to %s", app->modal_text);
        else
            snprintf(msg, sizeof(msg), "Rename failed: %s", strerror(errno));
        reload_with_status(app, msg);
    } else if (app->modal == MODAL_DELETE) {
        char msg[160];
        struct entry *e;

        if (strcmp(app->modal_text, "YES") != 0) {
            set_status(app, "Delete cancelled");
        } else if (app->selected >= 0 && app->selected < app->entry_count) {
            e = &app->entries[app->selected];
            if (remove_tree(e->path) == 0)
                snprintf(msg, sizeof(msg), "Deleted %s", e->name);
            else
                snprintf(msg, sizeof(msg), "Delete failed: %s",
                         strerror(errno));
            reload_with_status(app, msg);
        }
    }
    app->modal = MODAL_NONE;
    app->dirty = 1;
}

static void paste_clipboard(struct app *app)
{
    char dst[PATH_MAX];
    char msg[PATH_MAX + 32];
    const char *name;

    if (!app->clipboard[0])
        return;
    name = base_name(app->clipboard);
    unique_dest(dst, sizeof(dst), app->cwd, name);
    if (app->clipboard_cut &&
        path_is_same_or_child(app->cwd, app->clipboard)) {
        set_status(app, "Cannot move a folder into itself");
        return;
    }
    if (copy_tree(app->clipboard, dst) != 0) {
        set_status(app, "Paste failed: %s", strerror(errno));
        return;
    }
    if (app->clipboard_cut) {
        if (remove_tree(app->clipboard) == 0) {
            app->clipboard[0] = '\0';
            app->clipboard_cut = 0;
        }
    }
    snprintf(msg, sizeof(msg), "Pasted %s", base_name(dst));
    reload_with_status(app, msg);
}

static void run_action(struct app *app, enum action action)
{
    struct entry *e = NULL;

    if (app->selected >= 0 && app->selected < app->entry_count)
        e = &app->entries[app->selected];

    switch (action) {
    case ACT_UP:
        navigate_parent(app);
        break;
    case ACT_HOME:
        load_directory(app, "/root");
        break;
    case ACT_ROOT:
        load_directory(app, "/");
        break;
    case ACT_REFRESH:
        load_directory(app, app->cwd);
        break;
    case ACT_NEW_FOLDER:
        modal_begin(app, MODAL_NEW_FOLDER, "New folder", "New Folder");
        break;
    case ACT_RENAME:
        if (e)
            modal_begin(app, MODAL_RENAME, "Rename", e->name);
        break;
    case ACT_DELETE:
        if (e)
            modal_begin(app, MODAL_DELETE, "Type YES to delete",
                        "");
        break;
    case ACT_COPY:
        if (e) {
            snprintf(app->clipboard, sizeof(app->clipboard), "%s", e->path);
            app->clipboard_cut = 0;
            set_status(app, "Copied %s", e->name);
        }
        break;
    case ACT_CUT:
        if (e) {
            snprintf(app->clipboard, sizeof(app->clipboard), "%s", e->path);
            app->clipboard_cut = 1;
            set_status(app, "Cut %s", e->name);
        }
        break;
    case ACT_PASTE:
        paste_clipboard(app);
        break;
    case ACT_OPEN:
        open_selected(app);
        break;
    case ACT_SORT:
        if (app->sort == SORT_NAME)
            app->sort = SORT_TYPE;
        else if (app->sort == SORT_TYPE)
            app->sort = SORT_SIZE;
        else
            app->sort = SORT_NAME;
        load_directory(app, app->cwd);
        break;
    }
    app->dirty = 1;
}

static void add_button(struct app *app, enum action action, const char *label,
                       int x, int y, int w, int enabled)
{
    struct button *b;

    if (app->button_count >= MAX_BUTTONS)
        return;
    b = &app->buttons[app->button_count++];
    b->action = action;
    b->label = label;
    b->x = x;
    b->y = y;
    b->w = w;
    b->h = 24;
    b->enabled = enabled;
}

static void layout_buttons(struct app *app)
{
    int x = 8;
    int y = TITLEBAR_H + 5;
    int has_sel = app->selected >= 0 && app->selected < app->entry_count;

    app->button_count = 0;
    add_button(app, ACT_UP, "^", x, y, 28, strcmp(app->cwd, "/") != 0);
    x += 34;
    add_button(app, ACT_HOME, "Home", x, y, 48, 1);
    x += 54;
    add_button(app, ACT_ROOT, "/", x, y, 28, 1);
    x += 36;
    add_button(app, ACT_REFRESH, "Refresh", x, y, 70, 1);
    x += 78;
    add_button(app, ACT_NEW_FOLDER, "New", x, y, 46, 1);
    x += 54;
    add_button(app, ACT_RENAME, "Rename", x, y, 68, has_sel);
    x += 76;
    add_button(app, ACT_DELETE, "Delete", x, y, 66, has_sel);
    x += 74;
    add_button(app, ACT_COPY, "Copy", x, y, 52, has_sel);
    x += 60;
    add_button(app, ACT_CUT, "Cut", x, y, 42, has_sel);
    x += 50;
    add_button(app, ACT_PASTE, "Paste", x, y, 58, app->clipboard[0] != '\0');
    x += 66;
    add_button(app, ACT_OPEN, "Open", x, y, 52, has_sel);
    x += 60;
    add_button(app, ACT_SORT, "Sort", x, y, 52, 1);
}

static void draw_text_fit(uint32_t *fb, int w, int h, int x, int y,
                          int max_px, const char *text, uint32_t color)
{
    char tmp[160];
    int max_chars = max_px / 8;
    int len = (int)strlen(text);

    if (max_chars <= 0)
        return;
    if (len <= max_chars) {
        draw_string(fb, w, h, x, y, text, color, 1);
        return;
    }
    if (max_chars < 4)
        max_chars = 4;
    snprintf(tmp, sizeof(tmp), "%.*s...", max_chars - 3, text);
    draw_string(fb, w, h, x, y, tmp, color, 1);
}

static void draw_button(uint32_t *fb, int w, int h, const struct button *b)
{
    uint32_t bg = b->enabled ? 0xFF263448 : 0xFF1A2028;
    uint32_t fg = b->enabled ? 0xFFE8EDF4 : 0xFF687080;
    int tw = string_pixel_width(b->label, 1);

    draw_rounded_rect(fb, w, h, b->x, b->y, b->w, b->h, 4, bg);
    draw_string(fb, w, h, b->x + (b->w - tw) / 2, b->y + 4, b->label,
                fg, 1);
}

static void draw_title_button(uint32_t *fb, int w, int h, int x,
                              const char *label, uint32_t bg, uint32_t fg)
{
    int tw = string_pixel_width(label, 1);

    draw_rounded_rect(fb, w, h, x, 4, TITLE_BUTTON_W, 22, 4, bg);
    draw_string(fb, w, h, x + (TITLE_BUTTON_W - tw) / 2, 7, label, fg, 1);
}

static void draw_titlebar(uint32_t *fb, int w, int h, struct app *app)
{
    char title[PATH_MAX + 16];
    int close_x = w - 10 - TITLE_BUTTON_W;
    int max_x = close_x - TITLE_BUTTON_GAP - TITLE_BUTTON_W;
    int min_x = max_x - TITLE_BUTTON_GAP - TITLE_BUTTON_W;

    snprintf(title, sizeof(title), "Files - %s", app->cwd);
    draw_rect(fb, w, h, 0, 0, w, TITLEBAR_H, 0xFFE4E0DA);
    draw_rect(fb, w, h, 0, TITLEBAR_H - 1, w, 1, 0xFFB7B0A8);
    draw_text_fit(fb, w, h, 12, 7, min_x - 22, title, 0xFF20262F);
    draw_title_button(fb, w, h, min_x, "-", 0xFFD1CCC4, 0xFF20262F);
    draw_title_button(fb, w, h, max_x, app->maximized ? "[]" : "+",
                      0xFFD1CCC4, 0xFF20262F);
    draw_title_button(fb, w, h, close_x, "x", 0xFFC85454, 0xFFFFFFFF);
}

static void draw_sidebar(uint32_t *fb, int w, int h, struct app *app)
{
    const char *places[] = { "/root", "/root/desktop", "/tmp", "/" };
    int y = TITLEBAR_H + TOOLBAR_H + PATH_H + 12;

    draw_rect(fb, w, h, 0, TITLEBAR_H + TOOLBAR_H,
              SIDEBAR_W, h - (TITLEBAR_H + TOOLBAR_H),
              0xFF111722);
    draw_string(fb, w, h, 12, y, "Places", 0xFF93A7C0, 1);
    y += 24;
    for (int i = 0; i < 4; i++, y += 28) {
        uint32_t bg = strcmp(app->cwd, places[i]) == 0 ? 0xFF263B55 :
                                                      0xFF172130;
        draw_rounded_rect(fb, w, h, 8, y - 4, SIDEBAR_W - 16, 22, 4, bg);
        draw_text_fit(fb, w, h, 14, y, SIDEBAR_W - 24, places[i],
                      0xFFDCE6F0);
    }
}

static void draw_entries(uint32_t *fb, int w, int h, struct app *app)
{
    int x0 = LIST_X;
    int y0 = TITLEBAR_H + TOOLBAR_H + PATH_H;
    int list_w = w - x0 - 8;
    int list_h = h - y0 - STATUS_H - 8;
    int rows = (list_h - HEADER_H) / ROW_H;
    int max_scroll;

    if (rows < 1)
        rows = 1;
    max_scroll = app->entry_count > rows ? app->entry_count - rows : 0;
    if (app->scroll > max_scroll)
        app->scroll = max_scroll;
    if (app->scroll < 0)
        app->scroll = 0;

    draw_rect(fb, w, h, x0, y0, list_w, list_h, 0xFF121820);
    draw_rect(fb, w, h, x0, y0, list_w, HEADER_H, 0xFF202B3B);
    draw_string(fb, w, h, x0 + 36, y0 + 3, "Name", 0xFFB7C7D8, 1);
    draw_string(fb, w, h, x0 + list_w - 220, y0 + 3, "Kind", 0xFFB7C7D8, 1);
    draw_string(fb, w, h, x0 + list_w - 92, y0 + 3, "Size", 0xFFB7C7D8, 1);

    for (int r = 0; r < rows; r++) {
        int idx = app->scroll + r;
        int y = y0 + HEADER_H + r * ROW_H;
        char size_buf[32];
        struct entry *e;

        if (idx >= app->entry_count)
            break;
        e = &app->entries[idx];
        if (idx == app->selected)
            draw_rect(fb, w, h, x0, y, list_w, ROW_H, 0xFF2B4668);
        else if (r & 1)
            draw_rect(fb, w, h, x0, y, list_w, ROW_H, 0xFF151D27);

        draw_rounded_rect(fb, w, h, x0 + 8, y + 3, 16, 16, 3,
                          type_color(e->type));
        if (e->has_icon)
            xv6_icon_draw(fb, w, h, x0 + 10, y + 5, 12, 12, &e->icon);
        else
            draw_char(fb, w, h, x0 + 12, y + 3, type_glyph(e->type),
                      0xFFFFFFFF, 1);
        draw_text_fit(fb, w, h, x0 + 34, y + 3, list_w - 270, e->name,
                      0xFFE5ECF4);
        draw_text_fit(fb, w, h, x0 + list_w - 220, y + 3, 110,
                      type_label(e->type), 0xFFB7C7D8);
        if (e->type == FT_DIR)
            snprintf(size_buf, sizeof(size_buf), "-");
        else if (e->size > 1024 * 1024)
            snprintf(size_buf, sizeof(size_buf), "%ldM",
                     (long)(e->size / (1024 * 1024)));
        else if (e->size > 1024)
            snprintf(size_buf, sizeof(size_buf), "%ldK",
                     (long)(e->size / 1024));
        else
            snprintf(size_buf, sizeof(size_buf), "%ld", (long)e->size);
        draw_text_fit(fb, w, h, x0 + list_w - 92, y + 3, 82, size_buf,
                      0xFFB7C7D8);
    }

    if (app->entry_count > rows) {
        int bar_h = list_h * rows / app->entry_count;
        int bar_y = y0 + HEADER_H +
            (list_h - HEADER_H - bar_h) * app->scroll / max_scroll;
        if (bar_h < 18)
            bar_h = 18;
        draw_rect(fb, w, h, x0 + list_w - 4, y0 + HEADER_H, 3,
                  list_h - HEADER_H, 0xFF263040);
        draw_rect(fb, w, h, x0 + list_w - 4, bar_y, 3, bar_h, 0xFF6C86A6);
    }
}

static void draw_modal(uint32_t *fb, int w, int h, struct app *app)
{
    int mw = w > 620 ? 560 : w - 48;
    int mh = app->modal == MODAL_PREVIEW ? 360 : 150;
    int mx = (w - mw) / 2;
    int my = (h - mh) / 2;

    draw_rect(fb, w, h, 0, 0, w, h, 0x99000000);
    draw_rounded_rect(fb, w, h, mx, my, mw, mh, 6, 0xFF182232);
    draw_rect(fb, w, h, mx, my, mw, 28, 0xFF25364D);
    draw_text_fit(fb, w, h, mx + 12, my + 7, mw - 24, app->modal_title,
                  0xFFFFFFFF);

    if (app->modal == MODAL_PREVIEW) {
        int x = mx + 12;
        int y = my + 42;
        const char *p = app->preview;

        while (*p && y < my + mh - 28) {
            char line[72];
            int n = 0;

            while (*p && *p != '\n' && n < (int)sizeof(line) - 1)
                line[n++] = *p++;
            if (*p == '\n')
                p++;
            line[n] = '\0';
            draw_text_fit(fb, w, h, x, y, mw - 24, line, 0xFFE7EEF7);
            y += 17;
        }
        draw_string(fb, w, h, mx + 12, my + mh - 20,
                    "Esc/Enter closes preview", 0xFF9FB0C2, 1);
    } else {
        draw_rect(fb, w, h, mx + 12, my + 58, mw - 24, 28, 0xFF101722);
        draw_text_fit(fb, w, h, mx + 18, my + 64, mw - 36, app->modal_text,
                      0xFFFFFFFF);
        draw_string(fb, w, h, mx + 12, my + 108,
                    "Enter confirms, Esc cancels", 0xFF9FB0C2, 1);
    }
}

static void draw_app(struct app *app)
{
    uint32_t *fb = app->buffer.pixels;
    int w = app->width;
    int h = app->height;

    if (!fb)
        return;
    draw_rect(fb, w, h, 0, 0, w, h, 0xFF0D121A);
    draw_titlebar(fb, w, h, app);
    draw_rect(fb, w, h, 0, TITLEBAR_H, w, TOOLBAR_H, 0xFF182334);
    layout_buttons(app);
    for (int i = 0; i < app->button_count; i++)
        draw_button(fb, w, h, &app->buttons[i]);

    draw_rect(fb, w, h, 0, TITLEBAR_H + TOOLBAR_H, w, PATH_H, 0xFF101824);
    draw_text_fit(fb, w, h, 10, TITLEBAR_H + TOOLBAR_H + 4, w - 20, app->cwd,
                  0xFF7FD2FF);
    draw_sidebar(fb, w, h, app);
    draw_entries(fb, w, h, app);

    draw_rect(fb, w, h, 0, h - STATUS_H, w, STATUS_H, 0xFF151D28);
    draw_text_fit(fb, w, h, 10, h - STATUS_H + 5, w - 20, app->status,
                  0xFFBFD0E0);
    if (app->modal != MODAL_NONE)
        draw_modal(fb, w, h, app);
}

static void commit_frame(struct app *app)
{
    draw_app(app);
    wl_surface_attach(app->surface, app->buffer.wl_buffer, 0, 0);
    wl_surface_damage(app->surface, 0, 0, app->width, app->height);
    wl_surface_commit(app->surface);
    app->dirty = 0;
}

static int resize_buffer(struct app *app, int width, int height)
{
    if (width < 420)
        width = 420;
    if (height < 330)
        height = 330;
    if (width == app->width && height == app->height &&
        app->buffer.wl_buffer)
        return 0;
    xv6_present_buffer_destroy(&app->buffer);
    app->width = width;
    app->height = height;
    if (xv6_present_buffer_init(&app->buffer, app->width, app->height,
                                app->shm, app->gpu_manager) != 0)
        return -1;
    app->dirty = 1;
    return 0;
}

static int button_at(struct app *app, int x, int y)
{
    for (int i = 0; i < app->button_count; i++) {
        struct button *b = &app->buttons[i];

        if (x >= b->x && x < b->x + b->w &&
            y >= b->y && y < b->y + b->h)
            return i;
    }
    return -1;
}

static int entry_at(struct app *app, int x, int y)
{
    int x0 = LIST_X;
    int y0 = TITLEBAR_H + TOOLBAR_H + PATH_H + HEADER_H;
    int list_w = app->width - x0 - 8;
    int list_h = app->height - (TITLEBAR_H + TOOLBAR_H + PATH_H) -
        STATUS_H - 8;
    int row;
    int idx;

    if (x < x0 || x >= x0 + list_w || y < y0 || y >= y0 + list_h)
        return -1;
    row = (y - y0) / ROW_H;
    idx = app->scroll + row;
    return idx >= 0 && idx < app->entry_count ? idx : -1;
}

static int click_sidebar(struct app *app, int x, int y)
{
    const char *places[] = { "/root", "/root/desktop", "/tmp", "/" };
    int sy = TITLEBAR_H + TOOLBAR_H + PATH_H + 36;

    if (x >= SIDEBAR_W)
        return 0;
    for (int i = 0; i < 4; i++) {
        if (y >= sy + i * 28 - 4 && y < sy + i * 28 + 18) {
            load_directory(app, places[i]);
            return 1;
        }
    }
    return 0;
}

static enum title_action title_action_at(struct app *app, int x, int y)
{
    int close_x = app->width - 10 - TITLE_BUTTON_W;
    int max_x = close_x - TITLE_BUTTON_GAP - TITLE_BUTTON_W;
    int min_x = max_x - TITLE_BUTTON_GAP - TITLE_BUTTON_W;

    if (y < 4 || y >= TITLEBAR_H - 4)
        return TITLE_ACTION_NONE;
    if (x >= close_x && x < close_x + TITLE_BUTTON_W)
        return TITLE_ACTION_CLOSE;
    if (x >= max_x && x < max_x + TITLE_BUTTON_W)
        return TITLE_ACTION_MAXIMIZE;
    if (x >= min_x && x < min_x + TITLE_BUTTON_W)
        return TITLE_ACTION_MINIMIZE;
    return TITLE_ACTION_NONE;
}

static void handle_click(struct app *app, uint32_t time)
{
    enum title_action title_action;
    int btn;
    int idx;

    if (app->modal != MODAL_NONE) {
        if (app->modal == MODAL_PREVIEW)
            app->modal = MODAL_NONE;
        app->dirty = 1;
        return;
    }

    title_action = title_action_at(app, app->pointer_x, app->pointer_y);
    if (title_action == TITLE_ACTION_CLOSE) {
        app->running = 0;
        return;
    } else if (title_action == TITLE_ACTION_MAXIMIZE) {
        if (app->maximized) {
            xdg_toplevel_unset_maximized(app->toplevel);
            app->maximized = 0;
        } else {
            xdg_toplevel_set_maximized(app->toplevel);
            app->maximized = 1;
        }
        app->dirty = 1;
        return;
    } else if (title_action == TITLE_ACTION_MINIMIZE) {
        xdg_toplevel_set_minimized(app->toplevel);
        return;
    }

    btn = button_at(app, app->pointer_x, app->pointer_y);
    if (btn >= 0) {
        if (app->buttons[btn].enabled)
            run_action(app, app->buttons[btn].action);
        return;
    }

    if (click_sidebar(app, app->pointer_x, app->pointer_y))
        return;
    idx = entry_at(app, app->pointer_x, app->pointer_y);
    if (idx >= 0) {
        int dbl = idx == app->last_click_entry &&
            time - app->last_click_ms < 450;

        app->selected = idx;
        if (dbl)
            open_selected(app);
        app->last_click_entry = idx;
        app->last_click_ms = time;
        app->dirty = 1;
    }
}

static void modal_key_char(struct app *app, char ch)
{
    size_t len;

    if (app->modal == MODAL_NONE || app->modal == MODAL_PREVIEW)
        return;
    len = strlen(app->modal_text);
    if (len + 1 < sizeof(app->modal_text)) {
        app->modal_text[len] = ch;
        app->modal_text[len + 1] = '\0';
        app->dirty = 1;
    }
}

static char key_to_char(uint32_t key, uint32_t mods)
{
    int shift = mods & 1;

    if (key >= 2 && key <= 11) {
        const char *digits = "1234567890";
        const char *shifted = "!@#$%^&*()";
        return shift ? shifted[key - 2] : digits[key - 2];
    }
    if (key >= 16 && key <= 25) {
        const char *row = "qwertyuiop";
        char c = row[key - 16];
        return shift ? (char)(c - 'a' + 'A') : c;
    }
    if (key >= 30 && key <= 38) {
        const char *row = "asdfghjkl";
        char c = row[key - 30];
        return shift ? (char)(c - 'a' + 'A') : c;
    }
    if (key >= 44 && key <= 50) {
        const char *row = "zxcvbnm";
        char c = row[key - 44];
        return shift ? (char)(c - 'a' + 'A') : c;
    }
    switch (key) {
    case 12: return shift ? '_' : '-';
    case 13: return shift ? '+' : '=';
    case 26: return shift ? '{' : '[';
    case 27: return shift ? '}' : ']';
    case 39: return shift ? ':' : ';';
    case 40: return shift ? '"' : '\'';
    case 41: return shift ? '~' : '`';
    case 43: return shift ? '|' : '\\';
    case 51: return shift ? '<' : ',';
    case 52: return shift ? '>' : '.';
    case 53: return shift ? '?' : '/';
    case 57: return ' ';
    default: return 0;
    }
}

static void handle_key(struct app *app, uint32_t key)
{
    int rows = (app->height - (TITLEBAR_H + TOOLBAR_H + PATH_H) -
                STATUS_H - 8 - HEADER_H) / ROW_H;

    if (app->modal != MODAL_NONE) {
        if (key == 1) {
            app->modal = MODAL_NONE;
            app->dirty = 1;
        } else if (key == 28) {
            if (app->modal == MODAL_PREVIEW)
                app->modal = MODAL_NONE;
            else
                modal_accept(app);
            app->dirty = 1;
        } else if (key == 14 && app->modal != MODAL_PREVIEW) {
            size_t len = strlen(app->modal_text);

            if (len > 0)
                app->modal_text[len - 1] = '\0';
            app->dirty = 1;
        } else {
            char ch = key_to_char(key, app->mods);

            if (ch)
                modal_key_char(app, ch);
        }
        return;
    }

    if (app->mods & 4) {
        if (key == 46) run_action(app, ACT_COPY);
        else if (key == 45) run_action(app, ACT_CUT);
        else if (key == 47) run_action(app, ACT_PASTE);
        else if (key == 19) run_action(app, ACT_REFRESH);
        else if (key == 49) run_action(app, ACT_NEW_FOLDER);
        return;
    }

    switch (key) {
    case 1:
        app->running = 0;
        break;
    case 14:
        navigate_parent(app);
        break;
    case 28:
        open_selected(app);
        break;
    case 60:
        run_action(app, ACT_RENAME);
        break;
    case 111:
        run_action(app, ACT_DELETE);
        break;
    case 102:
        load_directory(app, "/root");
        break;
    case 103:
        if (app->selected > 0)
            app->selected--;
        else if (app->entry_count > 0)
            app->selected = 0;
        if (app->selected < app->scroll)
            app->scroll = app->selected;
        app->dirty = 1;
        break;
    case 108:
        if (app->selected + 1 < app->entry_count)
            app->selected++;
        else if (app->entry_count > 0)
            app->selected = 0;
        if (app->selected >= app->scroll + rows)
            app->scroll = app->selected - rows + 1;
        app->dirty = 1;
        break;
    case 104:
        app->scroll -= rows;
        if (app->scroll < 0)
            app->scroll = 0;
        app->dirty = 1;
        break;
    case 109:
        app->scroll += rows;
        app->dirty = 1;
        break;
    }
}

static void pointer_enter(void *data, struct wl_pointer *pointer,
                          uint32_t serial, struct wl_surface *surface,
                          wl_fixed_t sx, wl_fixed_t sy)
{
    struct app *app = data;
    (void)pointer;
    (void)serial;
    (void)surface;
    app->pointer_x = wl_fixed_to_int(sx);
    app->pointer_y = wl_fixed_to_int(sy);
}

static void pointer_leave(void *data, struct wl_pointer *pointer,
                          uint32_t serial, struct wl_surface *surface)
{
    (void)data;
    (void)pointer;
    (void)serial;
    (void)surface;
}

static void pointer_motion(void *data, struct wl_pointer *pointer,
                           uint32_t time, wl_fixed_t sx, wl_fixed_t sy)
{
    struct app *app = data;
    (void)pointer;
    (void)time;
    app->pointer_x = wl_fixed_to_int(sx);
    app->pointer_y = wl_fixed_to_int(sy);
}

static void pointer_button(void *data, struct wl_pointer *pointer,
                           uint32_t serial, uint32_t time, uint32_t button,
                           uint32_t state)
{
    struct app *app = data;
    (void)pointer;
    (void)serial;

    if (button == 0x110 && state == WL_POINTER_BUTTON_STATE_PRESSED)
        handle_click(app, time ? time : now_ms());
}

static void pointer_axis(void *data, struct wl_pointer *pointer,
                         uint32_t time, uint32_t axis, wl_fixed_t value)
{
    struct app *app = data;
    (void)pointer;
    (void)time;

    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        int delta = wl_fixed_to_int(value);

        if (delta > 0)
            app->scroll += 3;
        else if (delta < 0)
            app->scroll -= 3;
        if (app->scroll < 0)
            app->scroll = 0;
        app->dirty = 1;
    }
}

static void pointer_frame(void *data, struct wl_pointer *pointer)
{
    (void)data;
    (void)pointer;
}

static void pointer_axis_source(void *data, struct wl_pointer *pointer,
                                uint32_t axis_source)
{
    (void)data;
    (void)pointer;
    (void)axis_source;
}

static void pointer_axis_stop(void *data, struct wl_pointer *pointer,
                              uint32_t time, uint32_t axis)
{
    (void)data;
    (void)pointer;
    (void)time;
    (void)axis;
}

static void pointer_axis_discrete(void *data, struct wl_pointer *pointer,
                                  uint32_t axis, int32_t discrete)
{
    (void)data;
    (void)pointer;
    (void)axis;
    (void)discrete;
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
    .axis_source = pointer_axis_source,
    .axis_stop = pointer_axis_stop,
    .axis_discrete = pointer_axis_discrete,
};

static void keyboard_keymap(void *data, struct wl_keyboard *keyboard,
                            uint32_t format, int32_t fd, uint32_t size)
{
    (void)data;
    (void)keyboard;
    (void)format;
    (void)size;
    if (fd >= 0)
        close(fd);
}

static void keyboard_enter(void *data, struct wl_keyboard *keyboard,
                           uint32_t serial, struct wl_surface *surface,
                           struct wl_array *keys)
{
    (void)data;
    (void)keyboard;
    (void)serial;
    (void)surface;
    (void)keys;
}

static void keyboard_leave(void *data, struct wl_keyboard *keyboard,
                           uint32_t serial, struct wl_surface *surface)
{
    (void)data;
    (void)keyboard;
    (void)serial;
    (void)surface;
}

static void keyboard_key(void *data, struct wl_keyboard *keyboard,
                         uint32_t serial, uint32_t time, uint32_t key,
                         uint32_t state)
{
    struct app *app = data;
    (void)keyboard;
    (void)serial;
    (void)time;

    if (state == WL_KEYBOARD_KEY_STATE_PRESSED)
        handle_key(app, key);
}

static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard,
                               uint32_t serial, uint32_t mods_depressed,
                               uint32_t mods_latched, uint32_t mods_locked,
                               uint32_t group)
{
    struct app *app = data;
    (void)keyboard;
    (void)serial;
    (void)mods_latched;
    (void)mods_locked;
    (void)group;
    app->mods = mods_depressed;
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *keyboard,
                                 int32_t rate, int32_t delay)
{
    (void)data;
    (void)keyboard;
    (void)rate;
    (void)delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

static void seat_capabilities(void *data, struct wl_seat *seat,
                              uint32_t capabilities)
{
    struct app *app = data;

    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !app->pointer) {
        app->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(app->pointer, &pointer_listener, app);
    }
    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !app->keyboard) {
        app->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(app->keyboard, &keyboard_listener, app);
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name)
{
    (void)data;
    (void)seat;
    (void)name;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

static void xdg_surface_configure(void *data, struct xdg_surface *surface,
                                  uint32_t serial)
{
    struct app *app = data;

    xdg_surface_ack_configure(surface, serial);
    if (app->pending_width > 0 && app->pending_height > 0) {
        if (resize_buffer(app, app->pending_width, app->pending_height) != 0)
            app->running = 0;
        app->pending_width = 0;
        app->pending_height = 0;
    }
    app->configured = 1;
    commit_frame(app);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                               int32_t width, int32_t height,
                               struct wl_array *states)
{
    struct app *app = data;
    (void)toplevel;

    app->maximized = 0;
    if (states) {
        uint32_t *state;

        wl_array_for_each(state, states) {
            if (*state == XDG_TOPLEVEL_STATE_MAXIMIZED)
                app->maximized = 1;
        }
    }

    if (width > 0 && height > 0) {
        app->pending_width = width;
        app->pending_height = height;
    }
}

static void toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
    struct app *app = data;
    (void)toplevel;
    app->running = 0;
}

static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = toplevel_configure,
    .close = toplevel_close,
};

static void wm_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_listener = {
    .ping = wm_ping,
};

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface,
                            uint32_t version)
{
    struct app *app = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        app->compositor = wl_registry_bind(registry, name,
                                           &wl_compositor_interface,
                                           version > 4 ? 4 : version);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        app->shm = wl_registry_bind(registry, name, &wl_shm_interface,
                                    version > 1 ? 1 : version);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        app->seat = wl_registry_bind(registry, name, &wl_seat_interface,
                                     version > 5 ? 5 : version);
        wl_seat_add_listener(app->seat, &seat_listener, app);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        app->wm_base = wl_registry_bind(registry, name,
                                        &xdg_wm_base_interface,
                                        version > 2 ? 2 : version);
        xdg_wm_base_add_listener(app->wm_base, &wm_listener, app);
    } else if (strcmp(interface, xv6_gpu_buffer_manager_interface.name) == 0) {
        app->gpu_manager = wl_registry_bind(
            registry, name, &xv6_gpu_buffer_manager_interface,
            version > 3 ? 3 : version);
    }
}

static void registry_remove(void *data, struct wl_registry *registry,
                            uint32_t name)
{
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_remove,
};

static int init_wayland(struct app *app)
{
    app->display = wl_display_connect(NULL);
    if (!app->display)
        return -1;
    app->registry = wl_display_get_registry(app->display);
    wl_registry_add_listener(app->registry, &registry_listener, app);
    wl_display_roundtrip(app->display);
    wl_display_roundtrip(app->display);

    if (!app->compositor || !app->wm_base || (!app->gpu_manager && !app->shm))
        return -1;

    app->surface = wl_compositor_create_surface(app->compositor);
    app->xdg_surface = xdg_wm_base_get_xdg_surface(app->wm_base, app->surface);
    xdg_surface_add_listener(app->xdg_surface, &xdg_surface_listener, app);
    app->toplevel = xdg_surface_get_toplevel(app->xdg_surface);
    xdg_toplevel_add_listener(app->toplevel, &toplevel_listener, app);
    xdg_toplevel_set_title(app->toplevel, "Files");
    xdg_toplevel_set_app_id(app->toplevel, "filemgr");
    xdg_toplevel_set_min_size(app->toplevel, 420, 330);

    if (resize_buffer(app, app->width, app->height) != 0)
        return -1;
    wl_surface_commit(app->surface);
    return 0;
}

static void cleanup(struct app *app)
{
    if (app->display)
        wl_display_roundtrip(app->display);
    xv6_present_buffer_destroy(&app->buffer);
    if (app->keyboard)
        wl_keyboard_destroy(app->keyboard);
    if (app->pointer)
        wl_pointer_destroy(app->pointer);
    if (app->seat)
        wl_seat_destroy(app->seat);
    if (app->toplevel)
        xdg_toplevel_destroy(app->toplevel);
    if (app->xdg_surface)
        xdg_surface_destroy(app->xdg_surface);
    if (app->surface)
        wl_surface_destroy(app->surface);
    if (app->gpu_manager)
        wl_proxy_destroy(app->gpu_manager);
    if (app->wm_base)
        xdg_wm_base_destroy(app->wm_base);
    if (app->shm)
        wl_shm_destroy(app->shm);
    if (app->compositor)
        wl_compositor_destroy(app->compositor);
    if (app->registry)
        wl_registry_destroy(app->registry);
    if (app->display)
        wl_display_disconnect(app->display);
}

int main(int argc, char **argv)
{
    struct app app;
    const char *start = argc > 1 ? argv[1] : "/root";

    memset(&app, 0, sizeof(app));
    app.width = APP_W;
    app.height = APP_H;
    app.running = 1;
    app.selected = -1;
    app.last_click_entry = -1;
    app.buffer.fd = -1;
    app.buffer.fb_fd = -1;

    if (load_directory(&app, start) != 0)
        load_directory(&app, "/");
    if (init_wayland(&app) != 0) {
        fprintf(stderr, "filemgr: Wayland initialization failed\n");
        cleanup(&app);
        return 1;
    }

    while (app.running && wl_display_dispatch_pending(app.display) >= 0) {
        reap_children();
        if (app.dirty && app.configured)
            commit_frame(&app);
        if (wl_display_flush(app.display) < 0)
            break;
        if (wl_display_dispatch(app.display) < 0)
            break;
    }

    cleanup(&app);
    return 0;
}
