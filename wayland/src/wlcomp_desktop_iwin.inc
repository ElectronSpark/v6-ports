/* ══════════════════════════════════════════════════════════════════════
 *  Compositing / rendering
 * ══════════════════════════════════════════════════════════════════════ */

static int g_selected_icon = -1;       /* currently selected desktop icon */

/* ── Taskbar button definitions ────────────────────────────────────── */

#define TB_BTN_H           28
#define TB_BTN_PAD         4

/* ── Child process management ──────────────────────────────────────── */

static void terminate_client_pid(pid_t pid)
{
    wlcomp_launcher_terminate_pid(pid);
}

static void remember_pending_reap(pid_t pid)
{
    wlcomp_launcher_remember_reap(pid);
}

static void signal_process_group(pid_t pid, int sig)
{
    wlcomp_launcher_signal_process_group(pid, sig);
}

static void launch_desktop_app_arg(const char *path, const char *name,
                                   const char *arg)
{
    wlcomp_launcher_launch(path, name, arg);
}

static void launch_desktop_app(const char *path, const char *name)
{
    wlcomp_launcher_launch_noarg(path, name);
}

static void reap_children(void)
{
    wlcomp_launcher_reap();
}

static void destroy_surfaces_for_pid(pid_t pid)
{
    struct wlcomp_surface *surf;
    struct wl_client *client = NULL;

    if (pid <= 0)
        return;

    wl_list_for_each(surf, &g_surfaces, link) {
        if (surf->client_pid != pid)
            continue;

        damage_surface(surf);
        surf->mapped = 0;
        surf->minimized = 0;
        if (g_focused == surf)
            g_focused = NULL;
        if (g_kbd_focused == surf)
            g_kbd_focused = NULL;
        if (!client && surf->resource)
            client = wl_resource_get_client(surf->resource);
    }

    damage_taskbar();

    if (!client)
        return;

    fprintf(stderr, "wlcomp: destroying Wayland client for exited pid %d\n",
            pid);
    wl_client_destroy(client);
    damage_full_reason(FULL_DAMAGE_CLIENT);
}

static int process_exists(pid_t pid)
{
    if (pid <= 0)
        return 0;
    if (kill(pid, 0) == 0)
        return 1;
    return errno != ESRCH;
}

static void destroy_dead_client_surfaces(void)
{
    struct wlcomp_surface *surf;
    struct wlcomp_surface *tmp;

    wl_list_for_each_safe(surf, tmp, &g_surfaces, link) {
        pid_t pid = surf->client_pid;

        if (pid <= 0)
            continue;

        if (process_exists(pid))
            continue;

        destroy_surfaces_for_pid(pid);
        return;
    }
}


static void destroy_surface_client(struct wlcomp_surface *surf)
{
    struct wl_client *client;

    if (!surf || !surf->resource)
        return;

    terminate_client_pid(surf->client_pid);
    client = wl_resource_get_client(surf->resource);
    if (client)
        wl_client_destroy(client);
}





/* ── Clock ─────────────────────────────────────────────────────────── */

static char g_clock_str[16] = "00:00:00";

static void update_clock(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    int sec = (int)(ts.tv_sec % 86400);
    int h = sec / 3600;
    int m = (sec % 3600) / 60;
    int s = sec % 60;
    g_clock_str[0] = '0' + h / 10;
    g_clock_str[1] = '0' + h % 10;
    g_clock_str[2] = ':';
    g_clock_str[3] = '0' + m / 10;
    g_clock_str[4] = '0' + m % 10;
    g_clock_str[5] = ':';
    g_clock_str[6] = '0' + s / 10;
    g_clock_str[7] = '0' + s % 10;
    g_clock_str[8] = '\0';
}

/* ══════════════════════════════════════════════════════════════════════
 *  Internal Windows (compositor-rendered apps like the LVGL desktop)
 * ══════════════════════════════════════════════════════════════════════ */

static iwin_t g_iwin[MAX_IWIN];
static int g_iwin_cascade;      /* cascade counter for positioning */

static int surface_is_foreground(const struct wlcomp_surface *surf)
{
    return surf && g_iwin_focus < 0 && g_kbd_focused == surf &&
           !surf->minimized;
}

#define TB_TASK_W   120   /* max width of each task button */
#define TB_TASK_GAP  4    /* gap between task buttons */

static int surface_has_taskbar_button(const struct wlcomp_surface *surf)
{
    /*
     * GTK/WebKit can briefly tear down an xdg_toplevel while the wl_surface
     * and client process remain alive during complex window-state changes.
     * Keep such surfaces visible and taskbar-addressable until the wl_surface
     * itself is destroyed; otherwise a live browser can appear to vanish.
     */
    return surf && !surf->is_cursor && !surf->is_subsurface &&
           (surf->xdg_toplevel || surf->title[0] || surf->app_id[0]) &&
           (surf->mapped || surf->minimized);
}

static const char *surface_taskbar_label(const struct wlcomp_surface *surf)
{
    if (strcmp(surf->app_id, "netsurf") == 0)
        return "NetSurf";
    if (surf->title[0])
        return surf->title;
    if (surf->app_id[0])
        return surf->app_id;
    return "Window";
}

static uint32_t surface_taskbar_color(const struct wlcomp_surface *surf,
                                      int focused)
{
    if (focused)
        return 0xFF4A6FA5;
    if (strcmp(surf->app_id, "netsurf") == 0)
        return 0xFF315A66;
    return 0xFF2E3440;
}

static int surface_close_hit(const struct wlcomp_surface *surf, int mx, int my)
{
    int32_t gx, gy, gw, gh;

    if (surface_is_demo_window(surf)) {
        int frame_x, frame_y, close_x, close_y;

        surface_window_geometry(surf, &gx, &gy, &gw, &gh);
        frame_x = surf->x - WAYLAND_DEMO_BORDER;
        frame_y = surf->y - WAYLAND_DEMO_TITLE_H - WAYLAND_DEMO_BORDER;
        close_x = frame_x + gw + WAYLAND_DEMO_BORDER * 2 - WAYLAND_CLOSE_SZ - 5;
        close_y = frame_y + WAYLAND_DEMO_BORDER + 3;
        return mx >= close_x && mx < close_x + WAYLAND_CLOSE_SZ &&
               my >= close_y && my < close_y + WAYLAND_CLOSE_SZ;
    }
    if (surface_is_webkit_window(surf)) {
        /*
         * WebKit/GTK uses client-side decorations.  Let GTK receive clicks on
         * its headerbar buttons; a compositor-side approximate close rectangle
         * can misclassify maximize/drag gestures and SIGKILL the browser.
         */
        return 0;
    }
    return 0;
}

static uint32_t surface_demo_resize_edges(const struct wlcomp_surface *surf,
                                          int mx, int my)
{
    int32_t gx, gy, gw, gh;
    int frame_x, frame_y, frame_w, frame_h;
    uint32_t edges = 0;
    const int grip = 10;

    if (!surface_is_demo_window(surf))
        return 0;

    surface_window_geometry(surf, &gx, &gy, &gw, &gh);
    frame_x = surf->x - WAYLAND_DEMO_BORDER;
    frame_y = surf->y - WAYLAND_DEMO_TITLE_H - WAYLAND_DEMO_BORDER;
    frame_w = gw + WAYLAND_DEMO_BORDER * 2;
    frame_h = gh + WAYLAND_DEMO_TITLE_H + WAYLAND_DEMO_BORDER * 2;

    if (mx >= frame_x + frame_w - grip && mx < frame_x + frame_w)
        edges |= 8; /* XDG_TOPLEVEL_RESIZE_EDGE_RIGHT */
    if (my >= frame_y + frame_h - grip && my < frame_y + frame_h)
        edges |= 2; /* XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM */
    return edges;
}

static int surface_demo_title_hit(const struct wlcomp_surface *surf,
                                  int mx, int my)
{
    int32_t gx, gy, gw, gh;
    int frame_x, frame_y, frame_w;

    if (!surface_is_demo_window(surf))
        return 0;

    surface_window_geometry(surf, &gx, &gy, &gw, &gh);
    frame_x = surf->x - WAYLAND_DEMO_BORDER;
    frame_y = surf->y - WAYLAND_DEMO_TITLE_H - WAYLAND_DEMO_BORDER;
    frame_w = gw + WAYLAND_DEMO_BORDER * 2;
    return mx >= frame_x && mx < frame_x + frame_w &&
           my >= frame_y && my < frame_y + WAYLAND_DEMO_TITLE_H +
                                  WAYLAND_DEMO_BORDER * 2;
}

/* Draw the bottom taskbar */
static void draw_taskbar(uint32_t *fb, int fb_w, int fb_h)
{
    int tb_y = fb_h - TASKBAR_H;

    /* Taskbar background */
    draw_rect(fb, fb_w, fb_h, 0, tb_y, fb_w, TASKBAR_H, 0xFF1E1E21);
    /* Top border line */
    draw_rect(fb, fb_w, fb_h, 0, tb_y, fb_w, 1, 0xFF3C5078);

    /* Menu button */
    int btn_x = TB_BTN_PAD;
    int btn_y = tb_y + (TASKBAR_H - TB_BTN_H) / 2;
    draw_rounded_rect(fb, fb_w, fb_h, btn_x, btn_y, 55, TB_BTN_H, 4, 0xFF3C5078);
    draw_string(fb, fb_w, fb_h, btn_x + 8, btn_y + 6, "Menu", 0xFFD2DAE2, 1);

    /* Task buttons (after menu button) */
    int tx = TB_BTN_PAD + 55 + TB_TASK_GAP + 4;
    int right_limit = fb_w - 230; /* leave space for xv6 + clock */

    /* Internal windows */
    for (int i = 0; i < MAX_IWIN && tx + TB_TASK_W <= right_limit; i++) {
        if (!g_iwin[i].active) continue;
        int focused = (i == g_iwin_focus);
        uint32_t bg = focused ? 0xFF4A6FA5 : 0xFF2E3440;
        draw_rounded_rect(fb, fb_w, fb_h, tx, btn_y, TB_TASK_W, TB_BTN_H, 3, bg);
        /* Truncate title to fit */
        char lbl[16];
        int j;
        for (j = 0; j < 14 && g_iwin[i].title[j]; j++)
            lbl[j] = g_iwin[i].title[j];
        lbl[j] = '\0';
        draw_string(fb, fb_w, fb_h, tx + 6, btn_y + 6, lbl, 0xFFD2DAE2, 1);
        tx += TB_TASK_W + TB_TASK_GAP;
    }

    /* Wayland surfaces */
    struct wlcomp_surface *surf;
    wl_list_for_each(surf, &g_surfaces, link) {
        if (!surface_has_taskbar_button(surf) ||
            tx + TB_TASK_W > right_limit) continue;
        int focused = surface_is_foreground(surf);
        uint32_t bg = surface_taskbar_color(surf, focused);
        draw_rounded_rect(fb, fb_w, fb_h, tx, btn_y, TB_TASK_W, TB_BTN_H, 3, bg);
        const char *name = surface_taskbar_label(surf);
        char lbl[16];
        int j;
        for (j = 0; j < 14 && name[j]; j++)
            lbl[j] = name[j];
        lbl[j] = '\0';
        draw_string(fb, fb_w, fb_h, tx + 6, btn_y + 6, lbl, 0xFFD2DAE2, 1);
        tx += TB_TASK_W + TB_TASK_GAP;
    }

    /* "xv6" title */
    draw_string(fb, fb_w, fb_h, fb_w - 210,
                tb_y + (TASKBAR_H - 16) / 2, "xv6", 0xFF8C8C96, 1);

    /* Clock (right side) */
    update_clock();
    int cw = string_pixel_width(g_clock_str, 1);
    draw_string(fb, fb_w, fb_h, fb_w - cw - 16,
                tb_y + (TASKBAR_H - 16) / 2, g_clock_str, 0xFF64C8FF, 1);
}

/* Check if (mx,my) hits a desktop icon; returns index or -1 */
/* Check if (mx,my) is on the taskbar menu button */
static int menu_hit_test(int mx, int my, int fb_h)
{
    int tb_y = fb_h - TASKBAR_H;
    int btn_y = tb_y + (TASKBAR_H - TB_BTN_H) / 2;
    return (mx >= TB_BTN_PAD && mx < TB_BTN_PAD + 55 &&
            my >= btn_y && my < btn_y + TB_BTN_H);
}

/* Check if (mx,my) hits a task button on the taskbar.
   Returns: 1 if hit (and switches focus), 0 if not. */
static int taskbar_task_hit(int mx, int my, int fb_w, int fb_h)
{
    int tb_y = fb_h - TASKBAR_H;
    int btn_y = tb_y + (TASKBAR_H - TB_BTN_H) / 2;
    if (my < btn_y || my >= btn_y + TB_BTN_H) return 0;

    int tx = TB_BTN_PAD + 55 + TB_TASK_GAP + 4;
    int right_limit = fb_w - 230;

    /* Internal windows */
    for (int i = 0; i < MAX_IWIN && tx + TB_TASK_W <= right_limit; i++) {
        if (!g_iwin[i].active) continue;
        if (mx >= tx && mx < tx + TB_TASK_W) {
            if (g_iwin_focus != i)
                damage_full_reason(FULL_DAMAGE_IWIN);
            g_iwin_focus = i;
            damage_taskbar();
            return 1;
        }
        tx += TB_TASK_W + TB_TASK_GAP;
    }

    /* Wayland surfaces */
    struct wlcomp_surface *surf;
    wl_list_for_each(surf, &g_surfaces, link) {
        if (!surface_has_taskbar_button(surf) ||
            tx + TB_TASK_W > right_limit) continue;
        if (mx >= tx && mx < tx + TB_TASK_W) {
            int was_foreground = surface_is_foreground(surf);

            damage_surface(surf);
            g_iwin_focus = -1;
            if (surf->minimized) {
                /* Restore */
                surf->minimized = 0;
                surface_set_keyboard_focus(surf);
                surface_raise_to_top(surf);
            } else if (was_foreground) {
                /* Already focused — minimize */
                surf->minimized = 1;
                if (g_focused == surf)
                    g_focused = NULL;
                surface_set_keyboard_focus(NULL);
            } else {
                /* Not focused — raise and focus */
                surface_set_keyboard_focus(surf);
                surface_raise_to_top(surf);
            }
            damage_surface(surf);
            damage_taskbar();
            return 1;
        }
        tx += TB_TASK_W + TB_TASK_GAP;
    }
    return 0;
}

/* ── Menu state ────────────────────────────────────────────────────── */

static int g_menu_open;

#define MENU_W          160
#define MENU_ITEM_H      24

static int menu_item_count(void)
{
    return wlcomp_desktop_icon_count() + 2; /* separator + Power Off */
}

static const char *menu_item_label(int idx)
{
    int icon_count = wlcomp_desktop_icon_count();
    const desktop_icon_t *icon = wlcomp_desktop_icon_at(idx);

    if (icon)
        return icon->label;
    if (idx == icon_count)
        return "---";
    return "Power Off";
}

static void menu_bounds(int fb_h, int *x, int *y, int *w, int *h)
{
    int item_count = menu_item_count();

    *w = MENU_W;
    *h = item_count * MENU_ITEM_H + 8;
    *x = TB_BTN_PAD;
    *y = fb_h - TASKBAR_H - *h;
}

static void damage_menu(void)
{
    int x, y, w, h;

    if (!g_menu_open || g_fb_h == 0)
        return;
    menu_bounds((int)g_fb_h, &x, &y, &w, &h);
    damage_rect(x, y, w + 4, h + 4);
}

static void draw_menu(uint32_t *fb, int fb_w, int fb_h)
{
    if (!g_menu_open) return;

    int item_count = menu_item_count();
    int menu_x, menu_y, menu_w, menu_h;
    menu_bounds(fb_h, &menu_x, &menu_y, &menu_w, &menu_h);

    /* Shadow */
    draw_rect(fb, fb_w, fb_h, menu_x + 3, menu_y + 3,
              menu_w, menu_h, 0xFF101418);
    /* Background */
    draw_rounded_rect(fb, fb_w, fb_h, menu_x, menu_y,
                      menu_w, menu_h, 6, 0xFF252528);
    /* Border */
    draw_rect(fb, fb_w, fb_h, menu_x, menu_y, menu_w, 1, 0xFF3C5078);
    draw_rect(fb, fb_w, fb_h, menu_x, menu_y + menu_h - 1, menu_w, 1, 0xFF3C5078);
    draw_rect(fb, fb_w, fb_h, menu_x, menu_y, 1, menu_h, 0xFF3C5078);
    draw_rect(fb, fb_w, fb_h, menu_x + menu_w - 1, menu_y, 1, menu_h, 0xFF3C5078);

    for (int i = 0; i < item_count; i++) {
        int iy = menu_y + 4 + i * MENU_ITEM_H;
        const char *label = menu_item_label(i);
        if (label[0] == '-') {
            /* Separator */
            draw_rect(fb, fb_w, fb_h, menu_x + 8, iy + MENU_ITEM_H / 2,
                      menu_w - 16, 1, 0xFF485460);
        } else {
            /* Hover highlight */
            int mx = g_cursor_x, my = g_cursor_y;
            if (mx >= menu_x && mx < menu_x + menu_w &&
                my >= iy && my < iy + MENU_ITEM_H) {
                draw_rect(fb, fb_w, fb_h, menu_x + 2, iy,
                          menu_w - 4, MENU_ITEM_H, 0xFF3C5078);
            }
            draw_string(fb, fb_w, fb_h, menu_x + 12, iy + 4,
                        label, 0xFFD2DAE2, 1);
        }
    }
}

static void draw_default_cursor(void)
{
    static const uint8_t arrow[19][12] = {
        {1,0,0,0,0,0,0,0,0,0,0,0},
        {1,1,0,0,0,0,0,0,0,0,0,0},
        {1,2,1,0,0,0,0,0,0,0,0,0},
        {1,2,2,1,0,0,0,0,0,0,0,0},
        {1,2,2,2,1,0,0,0,0,0,0,0},
        {1,2,2,2,2,1,0,0,0,0,0,0},
        {1,2,2,2,2,2,1,0,0,0,0,0},
        {1,2,2,2,2,2,2,1,0,0,0,0},
        {1,2,2,2,2,2,2,2,1,0,0,0},
        {1,2,2,2,2,2,2,2,2,1,0,0},
        {1,2,2,2,2,2,2,2,2,2,1,0},
        {1,2,2,2,2,2,2,2,2,2,2,1},
        {1,2,2,2,2,2,1,1,1,1,1,1},
        {1,2,2,2,2,2,1,0,0,0,0,0},
        {1,2,2,1,1,2,2,1,0,0,0,0},
        {1,2,1,0,0,1,2,2,1,0,0,0},
        {1,1,0,0,0,0,1,2,2,1,0,0},
        {1,0,0,0,0,0,0,1,2,1,0,0},
        {0,0,0,0,0,0,0,0,1,1,0,0},
    };

    int32_t ox, oy, ow, oh;

    cursor_bounds_at(g_cursor_x, g_cursor_y, &ox, &oy, &ow, &oh);
    for (int y = 0; y < 19; y++) {
        int py = oy + y;
        if (py < 0 || py >= (int)g_fb_h) continue;
        for (int x = 0; x < 12; x++) {
            int px = ox + x;
            if (px < 0 || px >= (int)g_fb_w) continue;
            uint8_t v = arrow[y][x];
            if (v == 1)
                g_fb_buf[py * g_fb_w + px] = 0xFF000000;
            else if (v == 2)
                g_fb_buf[py * g_fb_w + px] = 0xFFFFFFFF;
        }
    }
}

/* Returns menu item index if clicked, or -1 */
static int menu_click_test(int mx, int my, int fb_h)
{
    if (!g_menu_open) return -1;
    int item_count = menu_item_count();
    int menu_x, menu_y, menu_w, menu_h;
    menu_bounds(fb_h, &menu_x, &menu_y, &menu_w, &menu_h);
    if (mx < menu_x || mx >= menu_x + menu_w ||
        my < menu_y || my >= menu_y + menu_h)
        return -2;  /* clicked outside menu → close it */
    int idx = (my - menu_y - 4) / MENU_ITEM_H;
    if (idx < 0 || idx >= item_count) return -1;
    if (menu_item_label(idx)[0] == '-') return -1;
    return idx;
}

/* Handle a desktop left-click (called from process_mouse on press) */
/* Forward declarations for internal window system */
static uint32_t get_time_ms(void);
static void open_terminal(void);
static void open_editor(void);
static void open_editor_path(const char *path);
static void open_text_app(int type, const char *title, int win_w, int win_h);
static void open_calc(void);
static void open_3ddemo(void);
static void open_filemgr(void);
static void open_filemgr_path(const char *path);
static void open_regular_file(const char *path);
static int handle_iwin_click(int mx, int my);
static void iwin_update_drag(int mx, int my, int buttons);
static void iwin_key_input(uint8_t keycode, uint8_t scancode, uint8_t pressed, uint8_t modifiers);
static void draw_iwin_all(uint32_t *fb, int fb_w, int fb_h);
static void process_terminals(void);

static void run_shortcut(const desktop_icon_t *sc)
{
    if (!sc)
        return;

    switch (sc->action) {
    case SHORTCUT_TERMINAL:
        open_terminal();
        break;
    case SHORTCUT_FILES:
        open_filemgr();
        break;
    case SHORTCUT_SYSINFO:
        open_text_app(APP_SYSINFO, "System Info", 500, 400);
        break;
    case SHORTCUT_CALC:
        open_calc();
        break;
    case SHORTCUT_NETWORK:
        open_text_app(APP_NETWORK, "Network", 500, 400);
        break;
    case SHORTCUT_SETTINGS:
        open_text_app(APP_SETTINGS, "Settings", 480, 400);
        break;
    case SHORTCUT_MONITOR:
        open_text_app(APP_MONITOR, "System Monitor", 500, 400);
        break;
    case SHORTCUT_3DDEMO:
        open_3ddemo();
        break;
    case SHORTCUT_EDITOR:
        open_editor();
        break;
    case SHORTCUT_FILE:
        if (sc->exec_path[0])
            open_regular_file(sc->exec_path);
        break;
    case SHORTCUT_EXEC:
    default:
        if (sc->exec_path[0])
            launch_desktop_app_arg(sc->exec_path,
                                   sc->exec_name[0] ? sc->exec_name :
                                   wlcomp_path_basename(sc->exec_path),
                                   sc->exec_arg[0] ? sc->exec_arg : NULL);
        break;
    }
}

static void handle_desktop_click(int mx, int my, int fb_w, int fb_h,
                                 int force_dblclick)
{
    /* Check menu first */
    if (g_menu_open) {
        int idx = menu_click_test(mx, my, fb_h);
        if (idx >= 0) {
            int icon_count = wlcomp_desktop_icon_count();
            const desktop_icon_t *icon = wlcomp_desktop_icon_at(idx);
            if (icon)
                run_shortcut(icon);
            else if (idx == icon_count + 1)
                syscall(XV6_SYS_poweroff);
            g_menu_open = 0;
        } else {
            g_menu_open = 0;  /* close on outside click or separator */
        }
        damage_full_reason(FULL_DAMAGE_MENU);
        return;
    }

    /* Check menu button on taskbar */
    if (menu_hit_test(mx, my, fb_h)) {
        g_menu_open = !g_menu_open;
        damage_full_reason(FULL_DAMAGE_MENU);
        return;
    }

    /* Check task buttons on taskbar */
    if (taskbar_task_hit(mx, my, fb_w, fb_h))
        return;

    /* Check desktop icons — first click selects, second click launches */
    int hit = wlcomp_desktop_icon_hit_test(mx, my);
    if (hit >= 0) {
        int is_launch = force_dblclick || (hit == g_selected_icon);
        if (is_launch) {
            run_shortcut(wlcomp_desktop_icon_at(hit));
            g_selected_icon = -1;
        } else {
            /* First click — select */
            g_selected_icon = hit;
        }
        damage_full_reason(FULL_DAMAGE_DESKTOP);
        return;
    }

    /* Click on empty desktop: deselect icon and close menu */
    g_selected_icon = -1;
    g_menu_open = 0;
    damage_full_reason(FULL_DAMAGE_DESKTOP);
}

/* ── Internal window management ────────────────────────────────────── */

static iwin_t *iwin_alloc(void)
{
    for (int i = 0; i < MAX_IWIN; i++)
        if (!g_iwin[i].active) return &g_iwin[i];
    return NULL;
}

static int iwin_index(iwin_t *w) { return (int)(w - g_iwin); }

static void damage_iwin(const iwin_t *w)
{
    if (!w || !w->active)
        return;
    damage_rect(w->x - 4, w->y - 4, w->w + 8, w->h + 8);
}

static void iwin_close(int idx)
{
    iwin_t *w = &g_iwin[idx];
    damage_iwin(w);
    if (w->type == APP_TERMINAL) {
        pid_t fg_pgid = 0;
        if (w->master_fd >= 0)
            ioctl(w->master_fd, XV6_TIOCGPGRP, &fg_pgid);
        fprintf(stderr, "wlcomp: closing terminal shell=%d fg_pgid=%d master=%d\n",
                w->shell_pid, fg_pgid, w->master_fd);
        if (fg_pgid > 0 && fg_pgid != w->shell_pid) {
            signal_process_group(fg_pgid, SIGHUP);
            signal_process_group(fg_pgid, SIGTERM);
            signal_process_group(fg_pgid, SIGKILL);
            remember_pending_reap(fg_pgid);
        }
        if (w->shell_pid > 0) {
            signal_process_group(w->shell_pid, SIGHUP);
            signal_process_group(w->shell_pid, SIGTERM);
            signal_process_group(w->shell_pid, SIGKILL);
            waitpid(w->shell_pid, NULL, WNOHANG);
            remember_pending_reap(w->shell_pid);
            w->shell_pid = 0;
        }
        if (w->master_fd >= 0) {
            close(w->master_fd);
            w->master_fd = -1;
        }
    }
    w->active = 0;
    if (g_iwin_focus == idx) g_iwin_focus = -1;
    damage_full_reason(FULL_DAMAGE_IWIN);
}

static void iwin_focus(int idx)
{
    if (g_iwin_focus >= 0 && g_iwin[g_iwin_focus].active)
        damage_iwin(&g_iwin[g_iwin_focus]);
    g_iwin_focus = idx;
    if (g_iwin_focus >= 0 && g_iwin[g_iwin_focus].active)
        damage_iwin(&g_iwin[g_iwin_focus]);
}

static void iwin_setup_pos(iwin_t *w, int win_w, int win_h)
{
    int off = (g_iwin_cascade % 5) * 30;
    w->w = win_w;
    w->h = win_h;
    w->x = 60 + off;
    w->y = 30 + off;
    /* Clamp to screen */
    if (w->x + w->w > (int)g_fb_w - 20) w->x = 20;
    if (w->y + w->h > (int)g_fb_h - TASKBAR_H - 20) w->y = 20;
    g_iwin_cascade++;
    damage_iwin(w);
}

/* ── Draw internal window frame ────────────────────────────────────── */

static void draw_iwin(uint32_t *fb, int fb_w, int fb_h, iwin_t *w, int focused)
{
    /* Shadow */
    draw_rect(fb, fb_w, fb_h, w->x + 3, w->y + 3, w->w, w->h, 0xFF101418);

    /* Window background */
    draw_rect(fb, fb_w, fb_h, w->x, w->y, w->w, w->h, 0xFF1E2228);

    /* Title bar */
    uint32_t tb_color = focused ? 0xFF2C4F7C : 0xFF2A2E35;
    draw_rect(fb, fb_w, fb_h, w->x, w->y, w->w, IWIN_TITLE_H, tb_color);

    /* Title text */
    draw_string(fb, fb_w, fb_h, w->x + 8, w->y + 4, w->title, 0xFFD2DAE2, 1);

    /* Close button (top-right) */
    int cx = w->x + w->w - IWIN_CLOSE_SZ - 4;
    int cy = w->y + 3;
    draw_rect(fb, fb_w, fb_h, cx, cy, IWIN_CLOSE_SZ, IWIN_CLOSE_SZ, 0xFFA63D3D);
    draw_char(fb, fb_w, fb_h, cx + 5, cy + 1, 'x', 0xFFFFFFFF, 1);

    /* Border */
    draw_rect(fb, fb_w, fb_h, w->x, w->y, w->w, 1, 0xFF3C5078);
    draw_rect(fb, fb_w, fb_h, w->x, w->y + w->h - 1, w->w, 1, 0xFF3C5078);
    draw_rect(fb, fb_w, fb_h, w->x, w->y, 1, w->h, 0xFF3C5078);
    draw_rect(fb, fb_w, fb_h, w->x + w->w - 1, w->y, 1, w->h, 0xFF3C5078);

    /* Resize grip (bottom-right corner) */
    uint32_t gc = 0xFF5A6878;
    int gx = w->x + w->w - 4;
    int gy = w->y + w->h - 4;
    draw_rect(fb, fb_w, fb_h, w->x + w->w - 18, w->y + w->h - 18,
              17, 17, 0xFF242B33);
    draw_rect(fb, fb_w, fb_h, gx, gy, 2, 2, gc);
    draw_rect(fb, fb_w, fb_h, gx - 4, gy, 2, 2, gc);
    draw_rect(fb, fb_w, fb_h, gx, gy - 4, 2, 2, gc);
    draw_rect(fb, fb_w, fb_h, gx - 8, gy, 2, 2, gc);
    draw_rect(fb, fb_w, fb_h, gx - 4, gy - 4, 2, 2, gc);
    draw_rect(fb, fb_w, fb_h, gx, gy - 8, 2, 2, gc);
}

static void open_terminal(void)
{
    iwin_t *w = iwin_alloc();
    if (!w) return;

    memset(w, 0, sizeof(*w));
    w->active = 1;
    w->type = APP_TERMINAL;
    w->master_fd = -1;
    snprintf(w->title, sizeof(w->title), "Terminal");

    /* Size: default terminal grid plus sidebar. */
    iwin_setup_pos(w, wlcomp_terminal_window_w_for_cols(TERM_DEFAULT_COLS),
                   wlcomp_terminal_window_h_for_rows(TERM_DEFAULT_ROWS));
    wlcomp_terminal_apply_window_size(w, 0);
    wlcomp_terminal_clear(w);

    /* Open PTY */
    int master = open("/dev/ptmx", O_RDWR | O_NOCTTY);
    if (master < 0) {
        snprintf(w->text, sizeof(w->text), "Failed to open /dev/ptmx");
        w->type = APP_SYSINFO;  /* fallback to text display */
        w->text_len = (int)strlen(w->text);
        iwin_focus(iwin_index(w));
        return;
    }

    unsigned int pts_idx = 0;
    ioctl(master, XV6_TIOCGPTN, &pts_idx);

    char pts_path[32];
    snprintf(pts_path, sizeof(pts_path), "/dev/pts/%u", pts_idx);

    /* Non-blocking master */
    int fl = fcntl(master, F_GETFL, 0);
    fcntl(master, F_SETFL, fl | O_NONBLOCK);

    pid_t pid = fork();
    if (pid < 0) {
        close(master);
        w->active = 0;
        return;
    }

    if (pid == 0) {
        /* Child: shell */
        close(master);
        setsid();
        int slave = open(pts_path, O_RDWR);
        if (slave < 0) _exit(1);
        ioctl(slave, XV6_TIOCSCTTY, 0);
        struct { unsigned short ws_row, ws_col, ws_xpixel, ws_ypixel; } ws;
        memset(&ws, 0, sizeof(ws));
        ws.ws_row = (unsigned short)w->term_rows;
        ws.ws_col = (unsigned short)w->term_cols;
        ws.ws_xpixel = (unsigned short)(w->term_cols * TERM_CELL_W);
        ws.ws_ypixel = (unsigned short)(w->term_rows * TERM_CELL_H);
        ioctl(slave, XV6_TIOCSWINSZ, &ws);
        dup2(slave, 0);
        dup2(slave, 1);
        dup2(slave, 2);
        if (slave > 2) close(slave);
        char *argv[] = { "sh", "--gui-session", NULL };
        char *envp[] = {
            "TERM=dumb",
            "HOME=/root",
            "PATH=/bin:/usr/bin",
            "PS1=\\w# ",
            "XDG_RUNTIME_DIR=/tmp",
            "XDG_CACHE_HOME=/tmp/.cache",
            "XDG_DATA_DIRS=/share:/usr/share",
            "WAYLAND_DISPLAY=wayland-0",
            "GDK_BACKEND=wayland",
            "XCURSOR_PATH=/share/icons",
            "XCURSOR_THEME=Adwaita",
            "SSL_CERT_FILE=/share/netsurf/ca-bundle",
            "XV6_GUI_SESSION=wayland",
            NULL
        };
        execve("/bin/sh", argv, envp);
        _exit(1);
    }

    w->master_fd = master;
    w->shell_pid = pid;
    wlcomp_terminal_send_winsz(w);
    iwin_focus(iwin_index(w));
    fprintf(stderr, "wlcomp: terminal opened (master=%d shell=%d)\n", master, pid);
}

static void open_editor_path(const char *path)
{
    iwin_t *w = iwin_alloc();
    if (!w) return;

    memset(w, 0, sizeof(*w));
    w->active = 1;
    w->type = APP_TERMINAL;
    w->master_fd = -1;
    if (path && path[0])
    snprintf(w->title, sizeof(w->title), "Editor - %s",
             wlcomp_path_basename(path));
    else
        snprintf(w->title, sizeof(w->title), "Editor");

    iwin_setup_pos(w, wlcomp_terminal_window_w_for_cols(TERM_DEFAULT_COLS),
                   wlcomp_terminal_window_h_for_rows(TERM_DEFAULT_ROWS));
    wlcomp_terminal_apply_window_size(w, 0);
    wlcomp_terminal_clear(w);

    int master = open("/dev/ptmx", O_RDWR | O_NOCTTY);
    if (master < 0) {
        snprintf(w->text, sizeof(w->text), "Failed to open /dev/ptmx");
        w->type = APP_SYSINFO;
        w->text_len = (int)strlen(w->text);
        iwin_focus(iwin_index(w));
        return;
    }

    unsigned int pts_idx = 0;
    ioctl(master, XV6_TIOCGPTN, &pts_idx);

    char pts_path[32];
    snprintf(pts_path, sizeof(pts_path), "/dev/pts/%u", pts_idx);

    int fl = fcntl(master, F_GETFL, 0);
    fcntl(master, F_SETFL, fl | O_NONBLOCK);

    pid_t pid = fork();
    if (pid < 0) {
        close(master);
        w->active = 0;
        return;
    }

    if (pid == 0) {
        close(master);
        setsid();
        int slave = open(pts_path, O_RDWR);
        if (slave < 0) _exit(1);
        ioctl(slave, XV6_TIOCSCTTY, 0);
        struct { unsigned short ws_row, ws_col, ws_xpixel, ws_ypixel; } ws;
        memset(&ws, 0, sizeof(ws));
        ws.ws_row = (unsigned short)w->term_rows;
        ws.ws_col = (unsigned short)w->term_cols;
        ws.ws_xpixel = (unsigned short)(w->term_cols * TERM_CELL_W);
        ws.ws_ypixel = (unsigned short)(w->term_rows * TERM_CELL_H);
        ioctl(slave, XV6_TIOCSWINSZ, &ws);
        dup2(slave, 0);
        dup2(slave, 1);
        dup2(slave, 2);
        if (slave > 2) close(slave);
        char *argv_noarg[] = { "vim", NULL };
        char *argv_file[] = { "vim", (char *)path, NULL };
        char **argv = (path && path[0]) ? argv_file : argv_noarg;
        char *envp[] = { "TERM=dumb", "HOME=/root", "PATH=/bin:/usr/bin",
                         NULL };
        execve("/bin/vim", argv, envp);
        _exit(1);
    }

    w->master_fd = master;
    w->shell_pid = pid;
    wlcomp_terminal_send_winsz(w);
    iwin_focus(iwin_index(w));
    fprintf(stderr, "wlcomp: editor opened (master=%d vim=%d)\n", master, pid);
}

static void open_editor(void)
{
    open_editor_path(NULL);
}

/* ── Text-display apps ─────────────────────────────────────────────── */

/* ── Calculator app ────────────────────────────────────────────────── */

/* ── App openers ───────────────────────────────────────────────────── */

static void open_text_app(int type, const char *title, int win_w, int win_h)
{
    iwin_t *w = iwin_alloc();
    if (!w) return;
    memset(w, 0, sizeof(*w));
    w->active = 1;
    w->type = type;
    w->master_fd = -1;
    snprintf(w->title, sizeof(w->title), "%s", title);
    iwin_setup_pos(w, win_w, win_h);

    switch (type) {
    case APP_SYSINFO:
        wlcomp_text_fill_sysinfo(w, g_fb_w, g_fb_h);
        break;
    case APP_NETWORK:
        wlcomp_text_fill_network(w, get_time_ms());
        break;
    case APP_FILES:
        wlcomp_text_fill_files(w);
        break;
    case APP_MONITOR:
        wlcomp_text_fill_monitor(w, get_time_ms());
        break;
    case APP_SETTINGS:
        wlcomp_text_fill_settings(w, g_fb_w, g_fb_h);
        break;
    }
    iwin_focus(iwin_index(w));
}

static void open_calc(void)
{
    iwin_t *w = iwin_alloc();
    if (!w) return;
    memset(w, 0, sizeof(*w));
    w->active = 1;
    w->type = APP_CALC;
    w->master_fd = -1;
    snprintf(w->title, sizeof(w->title), "Calculator");
    iwin_setup_pos(w, wlcomp_calc_window_w(), wlcomp_calc_window_h());
    wlcomp_calc_reset(w);
    iwin_focus(iwin_index(w));
}

/* ── 3D Demo (software OpenGL-style renderer) ──────────────────────── */

static void open_3ddemo(void)
{
    if (access("/bin/mesawlegl", 0) == 0) {
        launch_desktop_app_arg("/bin/mesawlegl", "mesawlegl",
                               "--demo");
        return;
    }

    if (access("/bin/mesaglsmoke", X_OK) == 0) {
        launch_desktop_app_arg("/bin/mesaglsmoke", "mesaglsmoke",
                               "--demo");
        return;
    }

    iwin_t *w = iwin_alloc();
    if (!w) return;
    memset(w, 0, sizeof(*w));
    w->active = 1;
    w->type = APP_3DDEMO;
    w->master_fd = -1;
    snprintf(w->title, sizeof(w->title), "OpenGL Demo");
    iwin_setup_pos(w, 360, 320);
    w->demo_angle = 0;
    iwin_focus(iwin_index(w));
}

static void open_regular_file(const char *path)
{
    struct stat st;

    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        open_filemgr_path(path);
        return;
    }

    if (wlcomp_path_has_suffix(path, ".desktop")) {
        desktop_icon_t sc;
        if (wlcomp_desktop_shortcut_parse(path, &sc) == 0) {
            run_shortcut(&sc);
            return;
        }
    }

    if (wlcomp_path_is_html(path)) {
        char url[320];
        snprintf(url, sizeof(url), "file://%s", path);
        launch_desktop_app_arg("/bin/netsurf", "netsurf", url);
        return;
    }

    if (wlcomp_path_is_text(path)) {
        open_editor_path(path);
        return;
    }

    if (stat(path, &st) == 0 &&
        (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
        launch_desktop_app(path, wlcomp_path_basename(path));
        return;
    }

    open_editor_path(path);
}

static void open_filemgr_path(const char *path)
{
    iwin_t *w = iwin_alloc();
    if (!w) return;
    memset(w, 0, sizeof(*w));
    w->active = 1;
    w->type = APP_FILEMGR;
    w->master_fd = -1;
    snprintf(w->title, sizeof(w->title), "Files - %s",
             (path && path[0]) ? path : "/");
    snprintf(w->fm_path, sizeof(w->fm_path), "%s",
             (path && path[0]) ? path : "/");
    iwin_setup_pos(w, 500, 420);
    wlcomp_filemgr_read_dir(w);
    iwin_focus(iwin_index(w));
}

static void open_filemgr(void)
{
    open_filemgr_path("/");
}

/* Helper: draw content of an internal window */
static void draw_iwin_content(uint32_t *fb, int fb_w, int fb_h, iwin_t *w)
{
    switch (w->type) {
    case APP_TERMINAL: wlcomp_terminal_draw(fb, fb_w, fb_h, w);  break;
    case APP_CALC:     wlcomp_calc_draw(fb, fb_w, fb_h, w);      break;
    case APP_3DDEMO:   wlcomp_demo_draw(fb, fb_w, fb_h, w);      break;
    case APP_FILEMGR:  wlcomp_filemgr_draw(fb, fb_w, fb_h, w);   break;
    case APP_SETTINGS: wlcomp_settings_draw(fb, fb_w, fb_h, w);  break;
    default:           wlcomp_text_draw(fb, fb_w, fb_h, w);      break;
    }
}

/* Draw all internal windows (focused last = on top) */
static void draw_iwin_all(uint32_t *fb, int fb_w, int fb_h)
{
    /* Draw non-focused windows first */
    for (int i = 0; i < MAX_IWIN; i++) {
        iwin_t *w = &g_iwin[i];
        if (!w->active || i == g_iwin_focus) continue;
        draw_iwin(fb, fb_w, fb_h, w, 0);
        draw_iwin_content(fb, fb_w, fb_h, w);
    }
    /* Draw focused window on top */
    if (g_iwin_focus >= 0 && g_iwin[g_iwin_focus].active) {
        iwin_t *w = &g_iwin[g_iwin_focus];
        draw_iwin(fb, fb_w, fb_h, w, 1);
        draw_iwin_content(fb, fb_w, fb_h, w);
    }
}

/* Process all terminal PTYs */
static void process_terminals(void)
{
    for (int i = 0; i < MAX_IWIN; i++) {
        iwin_t *w = &g_iwin[i];
        if (w->active && w->type == APP_TERMINAL && w->master_fd >= 0) {
            if (wlcomp_terminal_process_output(w))
                damage_iwin(w);
        }
    }
    /* Auto-refresh monitor and network windows */
    uint32_t now = get_time_ms();
    for (int i = 0; i < MAX_IWIN; i++) {
        iwin_t *w = &g_iwin[i];
        if (w->active && w->type == APP_MONITOR &&
            now - w->last_refresh > 2000) {
            wlcomp_text_fill_monitor(w, now);
            damage_iwin(w);
        } else if (w->active && w->type == APP_NETWORK &&
                   now - w->last_refresh > 2000) {
            wlcomp_text_fill_network(w, now);
            damage_iwin(w);
        } else if (w->active && w->type == APP_3DDEMO) {
            damage_iwin(w);
        }
    }
}

/* Hit-test: returns iwin index if (mx,my) is inside a window, -1 otherwise */
static int iwin_hit(int mx, int my)
{
    /* Check in reverse order (topmost = highest index or focused) */
    if (g_iwin_focus >= 0 && g_iwin[g_iwin_focus].active) {
        iwin_t *w = &g_iwin[g_iwin_focus];
        if (mx >= w->x && mx < w->x + w->w &&
            my >= w->y && my < w->y + w->h)
            return g_iwin_focus;
    }
    for (int i = MAX_IWIN - 1; i >= 0; i--) {
        if (!g_iwin[i].active) continue;
        if (i == g_iwin_focus) continue;  /* already checked */
        iwin_t *w = &g_iwin[i];
        if (mx >= w->x && mx < w->x + w->w &&
            my >= w->y && my < w->y + w->h)
            return i;
    }
    return -1;
}

/* Handle mouse click on an internal window */
static int handle_iwin_click(int mx, int my)
{
    int idx = iwin_hit(mx, my);
    if (idx < 0) return 0;  /* not on any window */

    iwin_t *w = &g_iwin[idx];
    iwin_focus(idx);

    /* Close button? */
    int cbx = w->x + w->w - IWIN_CLOSE_SZ - 4;
    int cby = w->y + 3;
    if (mx >= cbx && mx < cbx + IWIN_CLOSE_SZ &&
        my >= cby && my < cby + IWIN_CLOSE_SZ) {
        iwin_close(idx);
        return 1;
    }

    /* Resize edges: keep this generous enough to grab in the VM. */
    #define RESIZE_BORDER 18
    {
        int on_right  = (mx >= w->x + w->w - RESIZE_BORDER && mx < w->x + w->w);
        int on_bottom = (my >= w->y + w->h - RESIZE_BORDER && my < w->y + w->h);
        if (on_right || on_bottom) {
            w->resizing = 1;
            w->resize_edge = (on_right ? 1 : 0) | (on_bottom ? 2 : 0);
            w->resize_start_x = mx;
            w->resize_start_y = my;
            w->resize_start_w = w->w;
            w->resize_start_h = w->h;
            return 1;
        }
    }

    /* Title bar → start drag */
    if (my >= w->y && my < w->y + IWIN_TITLE_H) {
        w->dragging = 1;
        w->drag_ox = mx - w->x;
        w->drag_oy = my - w->y;
        return 1;
    }

    /* Calculator button press */
    if (w->type == APP_CALC) {
        int btn = wlcomp_calc_button_hit(w, mx, my);
        if (btn >= 0) {
            wlcomp_calc_press_index(w, btn);
            return 1;
        }
    }

    /* File manager icon click */
    if (w->type == APP_FILEMGR) {
        int fmi = wlcomp_filemgr_icon_hit(w, mx, my);
        if (fmi >= 0) {
            if (fmi == w->fm_selected) {
                if (w->fm_types[fmi] == 1) {
                    wlcomp_filemgr_navigate(w, fmi);
                } else {
                    char full[320];
                    wlcomp_filemgr_entry_path(w, fmi, full, sizeof(full));
                    open_regular_file(full);
                }
            } else {
                w->fm_selected = fmi;
            }
            return 1;
        }
    }

    /* Settings resolution buttons */
    if (w->type == APP_SETTINGS) {
        int cx0 = w->x + 4;
        int cy0 = w->y + IWIN_TITLE_H + 2;
        /* Resolution buttons start at row ~10 (y offset 170) */
        int by = cy0 + 170;
        static const struct { int w, h; } res_modes[] = {
            {800,500}, {960,600}, {1024,640}, {1024,768},
            {1280,720}, {1280,800}, {1280,1024}, {1920,1080},
        };
        for (size_t i = 0; i < sizeof(res_modes) / sizeof(res_modes[0]); i++) {
            int bx = cx0 + 8 + (i % 3) * 140;
            int bby = by + (i / 3) * 32;
            if (mx >= bx && mx < bx + 130 && my >= bby && my < bby + 26) {
                /* Apply resolution change */
                struct fb_var_screeninfo vinfo;
                vinfo.xres = (uint32_t)res_modes[i].w;
                vinfo.yres = (uint32_t)res_modes[i].h;
                vinfo.bits_per_pixel = 32;
                vinfo.pitch = 0;
                if (ioctl(g_fb_fd, FBIOPUT_VSCREENINFO, &vinfo) == 0 &&
                    ioctl(g_fb_fd, FBIOGET_VSCREENINFO, &vinfo) == 0) {
                    g_fb_w = vinfo.xres;
                    g_fb_h = vinfo.yres;
                    g_fb_pitch = vinfo.pitch;
                    release_framebuffer_backing();
                    if (alloc_framebuffer_backing() < 0) {
                        fprintf(stderr, "wlcomp: realloc framebuf failed\n");
                        return 1;
                    }
                    wlcomp_desktop_invalidate_layout();
                    damage_full_reason(FULL_DAMAGE_RESIZE);
                    fprintf(stderr, "wlcomp: resolution changed to %ux%u\n", g_fb_w, g_fb_h);
                    /* Refresh settings text */
                    wlcomp_text_fill_settings(w, g_fb_w, g_fb_h);
                }
                return 1;
            }
        }
    }

    return 1;  /* consumed by window */
}

static int handle_iwin_wheel(int mx, int my, int dz)
{
    if (dz == 0)
        return 0;

    int idx = iwin_hit(mx, my);
    if (idx < 0)
        return 0;

    iwin_t *w = &g_iwin[idx];
    if (w->type == APP_TERMINAL) {
        if (wlcomp_terminal_scroll_view(w, dz > 0 ? 3 : -3))
            damage_iwin(w);
        return 1;
    }
    if (w->type == APP_SYSINFO || w->type == APP_NETWORK ||
        w->type == APP_FILES || w->type == APP_MONITOR ||
        w->type == APP_SETTINGS) {
        if (dz > 0) w->text_scroll = (w->text_scroll > 0) ? w->text_scroll - 1 : 0;
        if (dz < 0) w->text_scroll++;
        damage_iwin(w);
        return 1;
    }
    if (w->type == APP_FILEMGR) {
        if (dz > 0) w->fm_scroll = (w->fm_scroll > 0) ? w->fm_scroll - 1 : 0;
        if (dz < 0) w->fm_scroll++;
        damage_iwin(w);
        return 1;
    }
    return 0;
}

/* Update dragging and resizing windows */
static void iwin_update_drag(int mx, int my, int buttons)
{
    for (int i = 0; i < MAX_IWIN; i++) {
        iwin_t *w = &g_iwin[i];
        if (!w->active) continue;

        /* Handle resize */
        if (w->resizing) {
            if (!(buttons & 1)) {
                w->resizing = 0;
                damage_iwin(w);
                continue;
            }
            damage_iwin(w);
            int dw = (w->resize_edge & 1) ? (mx - w->resize_start_x) : 0;
            int dh = (w->resize_edge & 2) ? (my - w->resize_start_y) : 0;
            int new_w = w->resize_start_w + dw;
            int new_h = w->resize_start_h + dh;
            /* Minimum size */
            int min_w = 120;
            int min_h = IWIN_TITLE_H + 40;
            if (w->type == APP_TERMINAL) {
                min_w = wlcomp_terminal_window_w_for_cols(TERM_MIN_COLS);
                min_h = wlcomp_terminal_window_h_for_rows(TERM_MIN_ROWS);
            }
            if (new_w < min_w) new_w = min_w;
            if (new_h < min_h) new_h = min_h;
            /* Maximum: screen bounds */
            if (w->x + new_w > (int)g_fb_w) new_w = (int)g_fb_w - w->x;
            if (w->y + new_h > (int)g_fb_h) new_h = (int)g_fb_h - w->y;
            w->w = new_w;
            w->h = new_h;
            if (w->type == APP_TERMINAL)
                wlcomp_terminal_apply_window_size(w, 1);
            damage_iwin(w);
            continue;
        }

        /* Handle drag */
        if (!w->dragging) continue;
        if (!(buttons & 1)) {
            w->dragging = 0;
            damage_iwin(w);
            continue;
        }
        damage_iwin(w);
        w->x = mx - w->drag_ox;
        w->y = my - w->drag_oy;
        /* Clamp */
        if (w->x < 0) w->x = 0;
        if (w->y < 0) w->y = 0;
        if (w->x + w->w > (int)g_fb_w) w->x = (int)g_fb_w - w->w;
        if (w->y + w->h > (int)g_fb_h) w->y = (int)g_fb_h - w->h;
        damage_iwin(w);
    }
}

/* Send keyboard input to focused internal window */
static void iwin_key_input(uint8_t keycode, uint8_t scancode, uint8_t pressed, uint8_t modifiers)
{
    if (!pressed) return;  /* only handle key presses */
    if (g_iwin_focus < 0) return;
    iwin_t *w = &g_iwin[g_iwin_focus];
    if (!w->active) return;

    if (w->type == APP_TERMINAL && w->master_fd >= 0) {
        char buf[8];
        int len = 0;
        /* Extended keys → ANSI escape sequences */
        if (keycode == 0x80) { /* UP */
            buf[0] = '\033'; buf[1] = '['; buf[2] = 'A'; len = 3;
        } else if (keycode == 0x81) { /* DOWN */
            buf[0] = '\033'; buf[1] = '['; buf[2] = 'B'; len = 3;
        } else if (keycode == 0x83) { /* RIGHT */
            buf[0] = '\033'; buf[1] = '['; buf[2] = 'C'; len = 3;
        } else if (keycode == 0x82) { /* LEFT */
            buf[0] = '\033'; buf[1] = '['; buf[2] = 'D'; len = 3;
        } else if (keycode == 0x84) { /* HOME */
            buf[0] = '\033'; buf[1] = '['; buf[2] = 'H'; len = 3;
        } else if (keycode == 0x85) { /* END */
            buf[0] = '\033'; buf[1] = '['; buf[2] = 'F'; len = 3;
        } else if (keycode == 0x89) { /* DELETE */
            buf[0] = '\033'; buf[1] = '['; buf[2] = '3'; buf[3] = '~'; len = 4;
        } else if (keycode != 0) {
            buf[0] = (char)keycode;
            len = 1;
        }
        if (len > 0)
            write(w->master_fd, buf, len);
    } else if (w->type == APP_SYSINFO || w->type == APP_NETWORK ||
               w->type == APP_FILES || w->type == APP_MONITOR ||
               w->type == APP_SETTINGS) {
        /* Scroll with arrow keys */
        if (keycode == 0x80) w->text_scroll = (w->text_scroll > 0) ? w->text_scroll - 1 : 0;
        if (keycode == 0x81) w->text_scroll++;
    } else if (w->type == APP_FILEMGR) {
        if (keycode == 0x80) w->fm_scroll = (w->fm_scroll > 0) ? w->fm_scroll - 1 : 0;
        if (keycode == 0x81) w->fm_scroll++;
    }
}
