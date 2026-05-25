/* ══════════════════════════════════════════════════════════════════════
 *  Input processing
 * ══════════════════════════════════════════════════════════════════════ */

static uint8_t g_prev_buttons;

static void process_mouse(void)
{
    /* Send deferred keyboard enter (set in surface_commit when a surface maps) */
    if (g_kbd_enter_pending && g_kbd_focused && g_kbd_focused->resource) {
        g_kbd_enter_pending = 0;
        struct wl_client *cl = wl_resource_get_client(g_kbd_focused->resource);
        struct wl_resource *kbd = find_resource_for_client(
            g_keyboard_resources, MAX_INPUT_RES, cl);
        if (kbd) {
            struct wl_array keys;
            wl_array_init(&keys);
            wl_keyboard_send_enter(kbd, ++g_serial,
                                   g_kbd_focused->resource, &keys);
            wl_array_release(&keys);
        }
    }

    struct mouse_event ev;
    uint8_t pressed_edges = 0;  /* bits that transitioned 0→1 */
    uint8_t released_edges = 0; /* bits that transitioned 1→0 */
    int left_press_count = 0;   /* number of left-button presses in batch */
    int16_t old_cursor_x = g_cursor_x;
    int16_t old_cursor_y = g_cursor_y;
    int cursor_moved = 0;
    int events_processed = 0;
    int wheel_delta = 0;
    const int max_mouse_events_per_frame = 64;

    while (events_processed < max_mouse_events_per_frame &&
           read(g_mouse_fd, &ev, sizeof(ev)) == sizeof(ev)) {
        events_processed++;
        if (ev.flags & MOUSE_EVENT_F_ABSOLUTE) {
            g_cursor_x = (int16_t)((uint32_t)(uint16_t)ev.dx * g_fb_w / 65536);
            g_cursor_y = (int16_t)((uint32_t)(uint16_t)ev.dy * g_fb_h / 65536);
        } else {
            g_cursor_x += ev.dx;
            g_cursor_y += ev.dy;
        }
        if (g_cursor_x < 0) g_cursor_x = 0;
        if (g_cursor_y < 0) g_cursor_y = 0;
        if (g_cursor_x >= (int16_t)g_fb_w) g_cursor_x = (int16_t)(g_fb_w - 1);
        if (g_cursor_y >= (int16_t)g_fb_h) g_cursor_y = (int16_t)(g_fb_h - 1);

        uint8_t prev = g_buttons;
        g_buttons = ev.buttons;
        pressed_edges  |= (g_buttons & ~prev);   /* 0→1 transitions */
        released_edges |= (~g_buttons & prev);    /* 1→0 transitions */
        if ((g_buttons & 1) && !(prev & 1))
            left_press_count++;
        wheel_delta += ev.dz;
    }
    cursor_moved = (g_cursor_x != old_cursor_x) || (g_cursor_y != old_cursor_y);
    if (cursor_moved) {
        damage_cursor_at(old_cursor_x, old_cursor_y);
        damage_cursor_at(g_cursor_x, g_cursor_y);
        damage_menu();
    }
    if (pressed_edges || released_edges)
        damage_cursor_at(g_cursor_x, g_cursor_y);

    if (wheel_delta != 0 && handle_iwin_wheel(g_cursor_x, g_cursor_y, wheel_delta)) {
        g_prev_buttons = g_buttons;
        return;
    }

    /* Update internal window dragging */
    iwin_update_drag(g_cursor_x, g_cursor_y, g_buttons);

    /* ── Interactive grab for Wayland surface move/resize ── */
    if (g_grab_surface && g_grab_mode) {
        if (!(g_buttons & 1)) {
            /* Button released — end grab */
            if (g_grab_mode == 2 && g_grab_surface->xdg_toplevel &&
                g_grab_surface->xdg_surface) {
                /* Send final configure with the current size */
                int32_t dx = g_cursor_x - g_grab_start_mx;
                int32_t dy = g_cursor_y - g_grab_start_my;
                int32_t nw = g_grab_start_w, nh = g_grab_start_h;
                /* XDG_TOPLEVEL_RESIZE_EDGE values: right=8, bottom=2,
                   bottom_right=10, left=4, top=1, etc. */
                if (g_grab_edges & 8) nw += dx;  /* right */
                if (g_grab_edges & 4) { nw -= dx; }  /* left */
                if (g_grab_edges & 2) nh += dy;  /* bottom */
                if (g_grab_edges & 1) { nh -= dy; }  /* top */
                if (nw < 200) nw = 200;
                if (nh < 150) nh = 150;
                struct wl_array states;
                wl_array_init(&states);
                uint32_t *st = wl_array_add(&states, sizeof(uint32_t));
                *st = XDG_TOPLEVEL_STATE_ACTIVATED;
                xdg_toplevel_send_configure(g_grab_surface->xdg_toplevel,
                                            nw, nh, &states);
                wl_array_release(&states);
                xdg_surface_send_configure(g_grab_surface->xdg_surface,
                                           ++g_serial);
                if (surface_is_demo_window(g_grab_surface)) {
                    g_grab_surface->demo_resize_requested = 1;
                    g_grab_surface->demo_resize_configures++;
                    g_grab_surface->demo_resize_w = nw;
                    g_grab_surface->demo_resize_h = nh;
                    wlcomp_emit_demo_interaction_matrix(
                        g_grab_surface, "resize-release-configure");
                }
            }
            g_grab_surface = NULL;
            g_grab_mode = 0;
        } else {
            int32_t dx = g_cursor_x - g_grab_start_mx;
            int32_t dy = g_cursor_y - g_grab_start_my;
            damage_surface(g_grab_surface);
            if (g_grab_mode == 1) {
                /* Move: update surface position */
                g_grab_surface->x = g_grab_start_x + dx;
                g_grab_surface->y = g_grab_start_y + dy;
                /* Clamp so title area stays on screen */
                if (g_grab_surface->x < -((int32_t)(g_grab_surface->committed_buf ?
                    g_grab_surface->committed_buf->width : 200) - 60))
                    g_grab_surface->x = -((int32_t)(g_grab_surface->committed_buf ?
                        g_grab_surface->committed_buf->width : 200) - 60);
                if (g_grab_surface->y < 0) g_grab_surface->y = 0;
                if (g_grab_surface->x >= (int32_t)g_fb_w - 60)
                    g_grab_surface->x = (int32_t)g_fb_w - 60;
                if (g_grab_surface->y >= (int32_t)g_fb_h - 36)
                    g_grab_surface->y = (int32_t)g_fb_h - 36 - 1;
            } else if (g_grab_mode == 2 && g_grab_surface->xdg_toplevel &&
                       g_grab_surface->xdg_surface) {
                /* Resize: compute new size and send configure */
                int32_t nw = g_grab_start_w, nh = g_grab_start_h;
                int32_t nx = g_grab_start_x, ny = g_grab_start_y;
                if (g_grab_edges & 8) nw += dx;  /* right */
                if (g_grab_edges & 4) { nw -= dx; nx += dx; }  /* left */
                if (g_grab_edges & 2) nh += dy;  /* bottom */
                if (g_grab_edges & 1) { nh -= dy; ny += dy; }  /* top */
                if (nw < 200) nw = 200;
                if (nh < 150) nh = 150;
                g_grab_surface->x = nx;
                g_grab_surface->y = ny;
                struct wl_array states;
                wl_array_init(&states);
                uint32_t *st = wl_array_add(&states, sizeof(uint32_t));
                *st = XDG_TOPLEVEL_STATE_ACTIVATED;
                xdg_toplevel_send_configure(g_grab_surface->xdg_toplevel,
                                            nw, nh, &states);
                wl_array_release(&states);
                xdg_surface_send_configure(g_grab_surface->xdg_surface,
                                           ++g_serial);
                if (surface_is_demo_window(g_grab_surface)) {
                    g_grab_surface->demo_resize_requested = 1;
                    g_grab_surface->demo_resize_configures++;
                    g_grab_surface->demo_resize_w = nw;
                    g_grab_surface->demo_resize_h = nh;
                    wlcomp_emit_demo_interaction_matrix(
                        g_grab_surface, "resize-drag-configure");
                }
            }
            damage_surface(g_grab_surface);
        }
        /* During grab, don't forward pointer events to clients */
        g_prev_buttons = g_buttons;
        return;
    }

    /* Hit testing respects stacking order:
     * When an internal window is focused, it's on top — check iwin first.
     * Otherwise Wayland surfaces are on top — check them first. */
    int32_t sx, sy;
    struct wlcomp_surface *target = NULL;
    int iwin_under = -1;

    if (g_iwin_focus >= 0) {
        /* Internal windows on top: check iwin first */
        iwin_under = iwin_hit(g_cursor_x, g_cursor_y);
        if (iwin_under < 0)
            target = surface_at(g_cursor_x, g_cursor_y, &sx, &sy);
    } else {
        /* Wayland surfaces on top: check them first */
        target = surface_at(g_cursor_x, g_cursor_y, &sx, &sy);
        if (!target)
            iwin_under = iwin_hit(g_cursor_x, g_cursor_y);
    }

    if (pressed_edges & 1) {
        /* Left button was pressed (at least once) during this batch */
        int desktop_handled = 0;
        int taskbar_or_menu =
            g_menu_open || g_cursor_y >= (int16_t)(g_fb_h - TASKBAR_H);

        if (taskbar_or_menu) {
            g_iwin_focus = -1;
            handle_desktop_click(g_cursor_x, g_cursor_y,
                                 (int)g_fb_w, (int)g_fb_h,
                                 left_press_count >= 2);
            desktop_handled = 1;
        } else if (target && surface_close_hit(target, g_cursor_x, g_cursor_y)) {
            if (surface_is_demo_window(target)) {
                target->demo_close_requested = 1;
                wlcomp_emit_demo_interaction_matrix(target, "close-request");
            }
            destroy_surface_client(target);
            target = NULL;
            desktop_handled = 1;
        } else if (target && surface_is_demo_window(target)) {
            uint32_t edges = surface_demo_resize_edges(target, g_cursor_x,
                                                       g_cursor_y);

            damage_surface(target);
            g_iwin_focus = -1;
            surface_raise_to_top(target);
            surface_set_keyboard_focus(target);
            if (edges != 0) {
                target->demo_resize_requested = 1;
                g_grab_surface = target;
                g_grab_mode = 2;
                g_grab_edges = edges;
                g_grab_start_mx = g_cursor_x;
                g_grab_start_my = g_cursor_y;
                g_grab_start_x = target->x;
                g_grab_start_y = target->y;
                g_grab_start_w = target->committed_buf ?
                                 target->committed_buf->width : 200;
                g_grab_start_h = target->committed_buf ?
                                 target->committed_buf->height : 200;
                wlcomp_emit_demo_interaction_matrix(target, "resize-grab");
                desktop_handled = 1;
            } else if (surface_demo_title_hit(target, g_cursor_x,
                                              g_cursor_y)) {
                g_grab_surface = target;
                g_grab_mode = 1;
                g_grab_start_mx = g_cursor_x;
                g_grab_start_my = g_cursor_y;
                g_grab_start_x = target->x;
                g_grab_start_y = target->y;
                desktop_handled = 1;
            }
            damage_surface(target);
        } else if (target) {
            /* Clicking on Wayland surface — unfocus internal windows */
            damage_surface(target);
            g_iwin_focus = -1;
            surface_raise_to_top(target);
            damage_surface(target);
        } else if (iwin_under >= 0 &&
                   handle_iwin_click(g_cursor_x, g_cursor_y)) {
            /* Internal window consumed the click */
        } else {
            /* No window under cursor — desktop icons / background */
            g_iwin_focus = -1;
            handle_desktop_click(g_cursor_x, g_cursor_y,
                                 (int)g_fb_w, (int)g_fb_h,
                                 left_press_count >= 2);
            desktop_handled = 1;
        }

        /* Suppress pointer event to Wayland surface if desktop consumed it */
        if (desktop_handled)
            target = NULL;

        /* Click-to-focus: only change keyboard when clicking a
         * DIFFERENT Wayland surface (not desktop/taskbar/iwin) */
        if (!desktop_handled && target)
            surface_set_keyboard_focus(target);
    }

    /* Forward Wayland pointer events to clients */
    {
        int sent_pointer_event = 0;
        if (target != g_focused) {
            /* Send pointer leave to old focused surface */
            if (g_focused && g_focused->resource) {
                struct wl_client *old_cl = wl_resource_get_client(
                    g_focused->resource);
                struct wl_resource *old_ptr = find_resource_for_client(
                    g_pointer_resources, MAX_INPUT_RES, old_cl);
                if (old_ptr) {
                    wl_pointer_send_leave(old_ptr, ++g_serial,
                                          g_focused->resource);
                    wl_pointer_send_frame(old_ptr);
                    sent_pointer_event = 1;
                }
            }
            g_focused = target;
            /* Send pointer enter to new focused surface */
            if (g_focused && g_focused->resource) {
                struct wl_client *new_cl = wl_resource_get_client(
                    g_focused->resource);
                struct wl_resource *new_ptr = find_resource_for_client(
                    g_pointer_resources, MAX_INPUT_RES, new_cl);
                if (new_ptr) {
                    wl_pointer_send_enter(new_ptr, ++g_serial,
                                          g_focused->resource,
                                          wl_fixed_from_int(sx),
                                          wl_fixed_from_int(sy));
                    sent_pointer_event = 1;
                }
            }
        }

        /* Motion / button / frame only to focused surface's client */
        if (g_focused && g_focused->resource) {
            struct wl_client *cl = wl_resource_get_client(
                g_focused->resource);
            struct wl_resource *ptr = find_resource_for_client(
                g_pointer_resources, MAX_INPUT_RES, cl);
            if (ptr) {
                if (target == g_focused && cursor_moved) {
                    wl_pointer_send_motion(ptr, get_time_ms(),
                                           wl_fixed_from_int(sx),
                                           wl_fixed_from_int(sy));
                    sent_pointer_event = 1;
                }

                if (pressed_edges || released_edges) {
                    uint32_t time = get_time_ms();
                    /* Send press events for buttons that went 0→1 */
                    if (pressed_edges & 1)
                        wl_pointer_send_button(ptr, ++g_serial,
                                               time, 0x110,
                                               WL_POINTER_BUTTON_STATE_PRESSED);
                    if (pressed_edges & 2)
                        wl_pointer_send_button(ptr, ++g_serial,
                                               time, 0x111,
                                               WL_POINTER_BUTTON_STATE_PRESSED);
                    if (pressed_edges & 4)
                        wl_pointer_send_button(ptr, ++g_serial,
                                               time, 0x112,
                                               WL_POINTER_BUTTON_STATE_PRESSED);
                    /* Send release events for buttons that went 1→0 */
                    if (released_edges & 1)
                        wl_pointer_send_button(ptr, ++g_serial,
                                               time, 0x110,
                                               WL_POINTER_BUTTON_STATE_RELEASED);
                    if (released_edges & 2)
                        wl_pointer_send_button(ptr, ++g_serial,
                                               time, 0x111,
                                               WL_POINTER_BUTTON_STATE_RELEASED);
                    if (released_edges & 4)
                        wl_pointer_send_button(ptr, ++g_serial,
                                               time, 0x112,
                                               WL_POINTER_BUTTON_STATE_RELEASED);
                    sent_pointer_event = 1;
                }

                if (sent_pointer_event)
                    wl_pointer_send_frame(ptr);
            }
        }
    }

    g_prev_buttons = g_buttons;
}

/* Bitmap tracking which keys are currently held (for Wayland dedup).
 * Indexed by evdev keycode; 256 bits = 32 bytes. */
static uint8_t g_key_down[32];

static inline int key_is_down(uint32_t code)
{
    return code < 256 && (g_key_down[code / 8] & (1u << (code % 8)));
}
static inline void key_set_down(uint32_t code, int down)
{
    if (code >= 256) return;
    if (down)
        g_key_down[code / 8] |= (1u << (code % 8));
    else
        g_key_down[code / 8] &= ~(1u << (code % 8));
}

/* Key debounce: track last release time per scancode to filter key bounce */
static uint32_t g_key_debounce_sc;     /* scancode of last released key */
static uint32_t g_key_debounce_time;   /* time of last release */
#define KEY_DEBOUNCE_MS 30

/* Track last modifier state to avoid redundant modifier events */
static uint32_t g_last_mods_sent;

static void process_keyboard(void)
{
    struct kbd_event ev;
    int events_processed = 0;
    const int max_keyboard_events_per_frame = 64;

    while (events_processed < max_keyboard_events_per_frame &&
           read(g_kbd_fd, &ev, sizeof(ev)) == sizeof(ev)) {
        events_processed++;
        /* Key debounce: suppress press that follows release of same key
         * within KEY_DEBOUNCE_MS (catches PS/2 contact bounce) */
        if (ev.pressed && ev.scancode == g_key_debounce_sc) {
            uint32_t now = get_time_ms();
            if ((now - g_key_debounce_time) < KEY_DEBOUNCE_MS)
                continue;  /* bounce — skip */
        }
        if (!ev.pressed)  {
            g_key_debounce_sc = ev.scancode;
            g_key_debounce_time = get_time_ms();
        }

        /* Route to internal window if one is focused */
        if (g_iwin_focus >= 0 && g_iwin[g_iwin_focus].active) {
            iwin_key_input(ev.keycode, ev.scancode, ev.pressed, ev.modifiers);
            damage_iwin(&g_iwin[g_iwin_focus]);
            continue;
        }
        /* Otherwise send to keyboard-focused Wayland client */
        if (!g_kbd_focused || !g_kbd_focused->resource)
            continue;
        {
            struct wl_client *cl = wl_resource_get_client(
                g_kbd_focused->resource);
            struct wl_resource *kbd = find_resource_for_client(
                g_keyboard_resources, MAX_INPUT_RES, cl);
            if (!kbd)
                continue;

            /* Convert PS/2 scancode to evdev keycode.
             * For basic keys (< 0x80), PS/2 set 1 == evdev.
             * For E0-prefixed keys, detect via keycode 0x80-0x89. */
            uint32_t evdev_code = ev.scancode;
            switch (ev.keycode) {
            case 0x80: evdev_code = 103; break; /* UP */
            case 0x81: evdev_code = 108; break; /* DOWN */
            case 0x82: evdev_code = 105; break; /* LEFT */
            case 0x83: evdev_code = 106; break; /* RIGHT */
            case 0x84: evdev_code = 102; break; /* HOME */
            case 0x85: evdev_code = 107; break; /* END */
            case 0x86: evdev_code = 104; break; /* PGUP */
            case 0x87: evdev_code = 109; break; /* PGDN */
            case 0x88: evdev_code = 110; break; /* INSERT */
            case 0x89: evdev_code = 111; break; /* DELETE */
            }

            /* Suppress hardware typematic repeat: Wayland clients handle
             * key repeat themselves based on repeat_info, so we must not
             * forward duplicate press events from the PS/2 controller. */
            if (ev.pressed && key_is_down(evdev_code))
                continue;   /* already down — hardware repeat, skip */
            key_set_down(evdev_code, ev.pressed);

            uint32_t state = ev.pressed ? WL_KEYBOARD_KEY_STATE_PRESSED
                                        : WL_KEYBOARD_KEY_STATE_RELEASED;
            wl_keyboard_send_key(kbd, ++g_serial,
                                 get_time_ms(), evdev_code, state);

            /* Send modifier state only when it changes */
            uint32_t mods_dep = 0;  /* depressed (currently held) */
            if (ev.modifiers & 0x01) mods_dep |= 1;  /* Shift */
            if (ev.modifiers & 0x02) mods_dep |= 4;  /* Control */
            if (ev.modifiers & 0x04) mods_dep |= 8;  /* Mod1 (Alt) */
            if (mods_dep != g_last_mods_sent) {
                wl_keyboard_send_modifiers(kbd, ++g_serial,
                                           mods_dep, 0, 0, 0);
                g_last_mods_sent = mods_dep;
            }
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════
 *  Initialization
 * ══════════════════════════════════════════════════════════════════════ */

static int init_framebuffer(void)
{
    g_fb_fd = open("/dev/fb0", O_RDWR);
    if (g_fb_fd < 0) {
        fprintf(stderr, "wlcomp: open /dev/fb0: %s\n", strerror(errno));
        return -1;
    }

    struct fb_var_screeninfo vinfo;
    if (ioctl(g_fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        fprintf(stderr, "wlcomp: FBIOGET_VSCREENINFO: %s\n", strerror(errno));
        close(g_fb_fd);
        return -1;
    }

    g_fb_w     = vinfo.xres;
    g_fb_h     = vinfo.yres;
    g_fb_pitch = vinfo.pitch;

    if (alloc_framebuffer_backing() < 0) {
        fprintf(stderr, "wlcomp: framebuf allocation failed\n");
        close(g_fb_fd);
        return -1;
    }
    damage_full_reason(FULL_DAMAGE_INIT);

    fprintf(stderr, "wlcomp: fb0 %ux%u pitch=%u\n", g_fb_w, g_fb_h, g_fb_pitch);
    return 0;
}

static void configure_toplevel_after_mode_change(struct wlcomp_surface *surf)
{
    if (!surf || !surf->xdg_surface || !surf->xdg_toplevel)
        return;

    if (surf->maximized) {
        struct wl_array states;
        uint32_t *s;
        int32_t max_h = (int32_t)g_fb_h - TASKBAR_H;

        if (max_h < 1)
            max_h = 1;
        surf->x = 0;
        surf->y = 0;
        wl_array_init(&states);
        s = wl_array_add(&states, sizeof(uint32_t));
        if (s)
            *s = XDG_TOPLEVEL_STATE_MAXIMIZED;
        s = wl_array_add(&states, sizeof(uint32_t));
        if (s)
            *s = XDG_TOPLEVEL_STATE_ACTIVATED;
        xdg_toplevel_send_configure(surf->xdg_toplevel,
                                    (int32_t)g_fb_w, max_h, &states);
        wl_array_release(&states);
        xdg_surface_send_configure(surf->xdg_surface, ++g_serial);
        return;
    }

    if (surf->x > (int32_t)g_fb_w - 60)
        surf->x = (int32_t)g_fb_w - 60;
    if (surf->x < -200)
        surf->x = -200;
    if (surf->y > (int32_t)g_fb_h - TASKBAR_H - 1)
        surf->y = (int32_t)g_fb_h - TASKBAR_H - 1;
    if (surf->y < 0)
        surf->y = 0;
}

static void poll_framebuffer_mode(void)
{
    static uint32_t next_probe_ms;
    uint32_t now = get_time_ms();
    struct fb_var_screeninfo vinfo;
    struct wlcomp_surface *surf;

    if (g_fb_fd < 0 || now < next_probe_ms)
        return;
    next_probe_ms = now + 250;

    if (ioctl(g_fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0)
        return;
    if (vinfo.xres == g_fb_w && vinfo.yres == g_fb_h &&
        vinfo.pitch == g_fb_pitch)
        return;

    fprintf(stderr, "wlcomp: fb0 mode changed externally %ux%u pitch=%u -> %ux%u pitch=%u\n",
            g_fb_w, g_fb_h, g_fb_pitch, vinfo.xres, vinfo.yres, vinfo.pitch);

    release_framebuffer_backing();
    g_fb_w = vinfo.xres;
    g_fb_h = vinfo.yres;
    g_fb_pitch = vinfo.pitch;
    if (alloc_framebuffer_backing() < 0) {
        fprintf(stderr, "wlcomp: framebuffer remap failed after mode change\n");
        g_running = 0;
        return;
    }

    if (g_cursor_x >= (int16_t)g_fb_w)
        g_cursor_x = (int16_t)(g_fb_w ? g_fb_w - 1 : 0);
    if (g_cursor_y >= (int16_t)g_fb_h)
        g_cursor_y = (int16_t)(g_fb_h ? g_fb_h - 1 : 0);
    wlcomp_desktop_invalidate_layout();
    wl_list_for_each(surf, &g_surfaces, link)
        configure_toplevel_after_mode_change(surf);
    damage_full_reason(FULL_DAMAGE_RESIZE);
}

static int init_input(void)
{
    g_mouse_fd = open("/dev/mouse", O_RDONLY | O_NONBLOCK);
    if (g_mouse_fd < 0)
        fprintf(stderr, "wlcomp: no /dev/mouse\n");
    else
        fprintf(stderr, "wlcomp: opened /dev/mouse fd=%d\n", g_mouse_fd);

    g_kbd_fd = open("/dev/kbd", O_RDONLY | O_NONBLOCK);
    if (g_kbd_fd < 0)
        fprintf(stderr, "wlcomp: no /dev/kbd\n");
    else
        fprintf(stderr, "wlcomp: opened /dev/kbd fd=%d\n", g_kbd_fd);

    g_cursor_x = (int16_t)(g_fb_w / 2);
    g_cursor_y = (int16_t)(g_fb_h / 2);
    damage_cursor_at(g_cursor_x, g_cursor_y);
    return 0;
}

static int cmdline_flag_enabled(const char *key)
{
    const char *cmdline = getenv("XV6_KERNEL_CMDLINE");
    size_t key_len = strlen(key);
    const char *p;

    if (!cmdline)
        return 0;

    p = cmdline;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n')
            p++;
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=' &&
            p[key_len + 1] == '1' &&
            (p[key_len + 2] == '\0' || p[key_len + 2] == ' ' ||
             p[key_len + 2] == '\t' || p[key_len + 2] == '\n'))
            return 1;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n')
            p++;
    }
    return 0;
}

static int cmdline_int_value(const char *key, int fallback)
{
    const char *cmdline = getenv("XV6_KERNEL_CMDLINE");
    size_t key_len = strlen(key);
    const char *p;

    if (!cmdline)
        return fallback;

    p = cmdline;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n')
            p++;
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            int value = atoi(p + key_len + 1);

            if (value < 0)
                value = 0;
            if (value > 120000)
                value = 120000;
            return value;
        }
        while (*p && *p != ' ' && *p != '\t' && *p != '\n')
            p++;
    }
    return fallback;
}
