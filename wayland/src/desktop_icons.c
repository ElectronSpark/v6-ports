#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#include "xv6_draw.h"
#include "xv6_present_buffer.h"
#include "xdg-shell-client-protocol.h"

#define DESKTOP_DIR "/root/desktop"
#define APP_W 1180
#define APP_H 680
#define MIN_W 520
#define MIN_H 360
#define MAX_ENTRIES 160
#define CELL_W 132
#define CELL_H 108
#define ICON_SIZE 48
#define GRID_X 24
#define GRID_Y 28
#define DOUBLE_CLICK_MS 450

enum entry_kind {
    ENTRY_FILE,
    ENTRY_DIR,
    ENTRY_PROGRAM,
    ENTRY_SCRIPT,
    ENTRY_DESKTOP,
    ENTRY_LINK,
};

struct desktop_entry {
    char name[128];
    char path[PATH_MAX];
    mode_t mode;
    enum entry_kind kind;
    int is_link;
    int x;
    int y;
    int w;
    int h;
};

struct app {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct xdg_wm_base *wm_base;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct wl_proxy *gpu_manager;
    struct xv6_present_buffer buffer;

    char dir[PATH_MAX];
    struct desktop_entry entries[MAX_ENTRIES];
    int entry_count;
    time_t dir_mtime;
    int selected;
    int last_click_entry;
    uint32_t last_click_ms;
    int pointer_x;
    int pointer_y;
    int width;
    int height;
    int pending_width;
    int pending_height;
    int configured;
    int dirty;
    int running;
};

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

static void copy_cstr(char *dst, size_t dst_sz, const char *src)
{
    size_t i;

    if (!dst || dst_sz == 0)
        return;
    if (!src)
        src = "";
    for (i = 0; i + 1 < dst_sz && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static void path_join(char *out, size_t out_sz, const char *dir,
                      const char *name)
{
    size_t len;

    if (!out || out_sz == 0)
        return;
    if (!dir)
        dir = "/";
    if (!name)
        name = "";
    if (strcmp(dir, "/") == 0) {
        out[0] = '/';
        copy_cstr(out + 1, out_sz > 1 ? out_sz - 1 : 0, name);
        return;
    }
    copy_cstr(out, out_sz, dir);
    len = strlen(out);
    if (len + 1 < out_sz) {
        out[len++] = '/';
        out[len] = '\0';
        copy_cstr(out + len, out_sz - len, name);
    }
}

static int entry_cmp(const void *a, const void *b)
{
    const struct desktop_entry *ea = a;
    const struct desktop_entry *eb = b;

    return strcmp(ea->name, eb->name);
}

static enum entry_kind classify_entry(const char *name, const struct stat *st,
                                      int is_link)
{
    if (S_ISDIR(st->st_mode))
        return ENTRY_DIR;
    if (has_suffix(name, ".desktop"))
        return ENTRY_DESKTOP;
    if (has_suffix(name, ".sh"))
        return ENTRY_SCRIPT;
    if (st->st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))
        return is_link ? ENTRY_LINK : ENTRY_PROGRAM;
    return ENTRY_FILE;
}

static int load_entries(struct app *app)
{
    DIR *dir;
    struct dirent *de;
    int count = 0;
    struct stat dst;

    dir = opendir(app->dir);
    if (!dir)
        return -1;

    while ((de = readdir(dir)) != NULL && count < MAX_ENTRIES) {
        struct stat lst;
        struct stat st;
        struct desktop_entry *e;
        char path[PATH_MAX];
        int is_link;

        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        path_join(path, sizeof(path), app->dir, de->d_name);
        if (lstat(path, &lst) != 0)
            continue;
        is_link = S_ISLNK(lst.st_mode);
        if (stat(path, &st) != 0)
            st = lst;

        e = &app->entries[count++];
        memset(e, 0, sizeof(*e));
        copy_cstr(e->name, sizeof(e->name), de->d_name);
        copy_cstr(e->path, sizeof(e->path), path);
        e->mode = st.st_mode;
        e->is_link = is_link;
        e->kind = classify_entry(e->name, &st, is_link);
    }
    closedir(dir);
    qsort(app->entries, (size_t)count, sizeof(app->entries[0]), entry_cmp);
    app->entry_count = count;
    app->selected = -1;
    app->last_click_entry = -1;
    if (stat(app->dir, &dst) == 0)
        app->dir_mtime = dst.st_mtime;
    app->dirty = 1;
    return 0;
}

static void maybe_reload_entries(struct app *app)
{
    struct stat st;

    if (stat(app->dir, &st) != 0)
        return;
    if (st.st_mtime == app->dir_mtime)
        return;
    load_entries(app);
}

static int parse_desktop_exec(const char *path, char *exec_path,
                              size_t exec_path_sz, char *exec_arg,
                              size_t exec_arg_sz)
{
    FILE *fp = fopen(path, "r");
    char line[320];
    char exec_cmd[256] = "";
    char *s;
    char *end;

    if (!fp)
        return -1;
    while (fgets(line, sizeof(line), fp)) {
        while (line[0] == ' ' || line[0] == '\t')
            memmove(line, line + 1, strlen(line));
        if (strncmp(line, "Exec=", 5) != 0)
            continue;
        copy_cstr(exec_cmd, sizeof(exec_cmd), line + 5);
        break;
    }
    fclose(fp);
    if (!exec_cmd[0])
        return -1;

    s = exec_cmd;
    while (*s == ' ' || *s == '\t')
        s++;
    if (*s == '"' || *s == '\'') {
        char q = *s++;

        end = s;
        while (*end && *end != q)
            end++;
    } else {
        end = s;
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
    end = strpbrk(s, "\r\n");
    if (end)
        *end = '\0';
    snprintf(exec_arg, exec_arg_sz, "%s", s);
    return exec_path[0] ? 0 : -1;
}

static void launch_exec(const char *path, const char *name, const char *arg)
{
    pid_t pid = fork();

    if (pid < 0)
        return;
    if (pid == 0) {
        int logfd;

        setpgid(0, 0);
        logfd = open("/tmp/desktop-icons-app.log",
                     O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (logfd >= 0) {
            dup2(logfd, 1);
            dup2(logfd, 2);
            close(logfd);
        }
        if (arg)
            execl(path, name ? name : base_name(path), arg, NULL);
        else
            execl(path, name ? name : base_name(path), NULL);
        _exit(127);
    }
    waitpid(pid, NULL, WNOHANG);
}

static void launch_entry(const struct desktop_entry *e)
{
    if (e->kind == ENTRY_DIR) {
        launch_exec("/bin/filemgr", "filemgr", e->path);
    } else if (e->kind == ENTRY_DESKTOP) {
        char exec_path[PATH_MAX];
        char exec_arg[PATH_MAX];

        if (parse_desktop_exec(e->path, exec_path, sizeof(exec_path),
                               exec_arg, sizeof(exec_arg)) == 0)
            launch_exec(exec_path, base_name(exec_path),
                        exec_arg[0] ? exec_arg : NULL);
    } else if (e->kind == ENTRY_SCRIPT &&
               !(e->mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
        launch_exec("/bin/sh", "sh", e->path);
    } else if (e->kind == ENTRY_PROGRAM || e->kind == ENTRY_SCRIPT ||
               e->kind == ENTRY_LINK) {
        launch_exec(e->path, base_name(e->path), NULL);
    }
}

static void reap_children(void)
{
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
}

static void draw_text_centered(uint32_t *fb, int fb_w, int fb_h,
                               int x, int y, int max_w, const char *text,
                               uint32_t color)
{
    char buf[64];
    int len;
    int tw;

    snprintf(buf, sizeof(buf), "%s", text ? text : "");
    len = (int)strlen(buf);
    while (len > 0 && string_pixel_width(buf, 1) > max_w)
        buf[--len] = '\0';
    tw = string_pixel_width(buf, 1);
    draw_string(fb, fb_w, fb_h, x + (max_w - tw) / 2, y, buf, color, 1);
}

static void display_label(const char *name, char *line1, size_t line1_sz,
                          char *line2, size_t line2_sz)
{
    const char *dash;
    size_t n;

    line1[0] = '\0';
    line2[0] = '\0';
    dash = strchr(name, '-');
    if (!dash || dash == name || strlen(name) < 14) {
        snprintf(line1, line1_sz, "%s", name);
        return;
    }
    n = (size_t)(dash - name);
    if (n >= line1_sz)
        n = line1_sz - 1;
    memcpy(line1, name, n);
    line1[n] = '\0';
    snprintf(line2, line2_sz, "%s", dash + 1);
}

static void draw_entry_icon(uint32_t *fb, int w, int h,
                            const struct desktop_entry *e, int selected)
{
    uint32_t tile = selected ? 0xFF2F6CA5 : 0xBB102033;
    uint32_t accent = 0xFF6FD6FF;
    uint32_t fg = 0xFFFFFFFF;
    int ix = e->x + (CELL_W - ICON_SIZE) / 2;
    int iy = e->y + 4;
    char glyph = '>';
    char line1[48];
    char line2[64];

    if (e->kind == ENTRY_DIR) {
        accent = 0xFFFFC857;
        glyph = 'F';
    } else if (e->kind == ENTRY_SCRIPT) {
        accent = 0xFF6EE7A8;
        glyph = '$';
    } else if (e->kind == ENTRY_DESKTOP || e->kind == ENTRY_LINK) {
        accent = 0xFFFF8D6B;
        glyph = '*';
    } else if (e->kind == ENTRY_FILE) {
        accent = 0xFFC7D1DA;
        glyph = '.';
    }

    if (selected)
        draw_rounded_rect(fb, w, h, e->x + 8, e->y, e->w - 16, e->h - 4, 8,
                          0xFF2E6FA9);
    draw_rounded_rect(fb, w, h, ix, iy, ICON_SIZE, ICON_SIZE, 8, tile);
    if (e->kind == ENTRY_DIR) {
        draw_rounded_rect(fb, w, h, ix + 7, iy + 14, 34, 25, 4, accent);
        draw_rect(fb, w, h, ix + 10, iy + 9, 18, 8, accent);
        draw_rect(fb, w, h, ix + 12, iy + 30, 28, 3, 0x88FFFFFF);
    } else {
        draw_rounded_rect(fb, w, h, ix + 11, iy + 8, 27, 34, 4, accent);
        draw_rect(fb, w, h, ix + 30, iy + 8, 8, 8, 0xDDFFFFFF);
        draw_char(fb, w, h, ix + 20, iy + 20, glyph, fg, 2);
    }

    display_label(e->name, line1, sizeof(line1), line2, sizeof(line2));
    draw_text_centered(fb, w, h, e->x + 6, e->y + 60, CELL_W - 12, line1,
                       0xFFFFFFFF);
    if (line2[0]) {
        draw_text_centered(fb, w, h, e->x + 6, e->y + 76, CELL_W - 12,
                           line2, 0xFFE1ECF5);
    }
}

static void layout_entries(struct app *app)
{
    int cols = (app->width - GRID_X * 2) / CELL_W;

    if (cols < 1)
        cols = 1;
    for (int i = 0; i < app->entry_count; i++) {
        int col = i % cols;
        int row = i / cols;

        app->entries[i].x = GRID_X + col * CELL_W;
        app->entries[i].y = GRID_Y + row * CELL_H;
        app->entries[i].w = CELL_W;
        app->entries[i].h = CELL_H;
    }
}

static void draw_app(struct app *app)
{
    uint32_t *fb = app->buffer.pixels;

    if (!fb)
        return;
    draw_rect(fb, app->width, app->height, 0, 0, app->width, app->height,
              0xFF243843);
    layout_entries(app);
    for (int i = 0; i < app->entry_count; i++)
        draw_entry_icon(fb, app->width, app->height, &app->entries[i],
                        i == app->selected);
    if (app->entry_count == 0) {
        draw_string(fb, app->width, app->height, GRID_X, GRID_Y,
                    "/root/desktop is empty", 0xFFC9D8E2, 1);
    }
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
    if (width < MIN_W)
        width = MIN_W;
    if (height < MIN_H)
        height = MIN_H;
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

static int entry_at(struct app *app, int x, int y)
{
    for (int i = 0; i < app->entry_count; i++) {
        struct desktop_entry *e = &app->entries[i];

        if (x >= e->x && x < e->x + e->w &&
            y >= e->y && y < e->y + e->h)
            return i;
    }
    return -1;
}

static void handle_click(struct app *app, uint32_t time)
{
    int idx = entry_at(app, app->pointer_x, app->pointer_y);

    if (idx < 0) {
        app->selected = -1;
        app->dirty = 1;
        return;
    }
    if (idx == app->last_click_entry &&
        time - app->last_click_ms < DOUBLE_CLICK_MS)
        launch_entry(&app->entries[idx]);
    app->selected = idx;
    app->last_click_entry = idx;
    app->last_click_ms = time;
    app->dirty = 1;
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
    (void)data;
    (void)pointer;
    (void)time;
    (void)axis;
    (void)value;
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

static void seat_capabilities(void *data, struct wl_seat *seat,
                              uint32_t capabilities)
{
    struct app *app = data;

    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !app->pointer) {
        app->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(app->pointer, &pointer_listener, app);
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
    (void)states;

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
    xdg_toplevel_set_title(app->toplevel, "Desktop");
    xdg_toplevel_set_app_id(app->toplevel, "xv6-desktop-icons");
    xdg_toplevel_set_min_size(app->toplevel, MIN_W, MIN_H);
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
    const char *dir = argc > 1 ? argv[1] : DESKTOP_DIR;
    int idle_ticks = 0;

    memset(&app, 0, sizeof(app));
    snprintf(app.dir, sizeof(app.dir), "%s", dir);
    app.width = APP_W;
    app.height = APP_H;
    app.running = 1;
    app.selected = -1;
    app.last_click_entry = -1;
    app.buffer.fd = -1;
    app.buffer.fb_fd = -1;
    if (load_entries(&app) != 0)
        fprintf(stderr, "xv6-desktop-icons: cannot read %s: %s\n",
                app.dir, strerror(errno));
    if (init_wayland(&app) != 0) {
        fprintf(stderr, "xv6-desktop-icons: cannot connect to Wayland\n");
        cleanup(&app);
        return 1;
    }
    while (app.running && wl_display_dispatch_pending(app.display) != -1) {
        if (app.dirty && app.configured)
            commit_frame(&app);
        reap_children();
        if (++idle_ticks >= 60) {
            maybe_reload_entries(&app);
            idle_ticks = 0;
        }
        if (wl_display_flush(app.display) < 0)
            break;
        if (wl_display_dispatch(app.display) < 0)
            break;
        usleep(16666);
    }
    cleanup(&app);
    return 0;
}
