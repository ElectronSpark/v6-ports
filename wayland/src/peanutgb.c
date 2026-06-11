/*
 * peanutgb.c - small xv6 Wayland frontend for Peanut-GB.
 *
 * Peanut-GB is an MIT-licensed Game Boy emulator core by Mahyar Koshkouei.
 * This file supplies ROM/save loading, keyboard input, and a wl_shm renderer
 * for xv6's Wayland compositor.
 */

#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"
#include "xv6_draw.h"
#include "xv6_present_buffer.h"
#include "xv6_titlebar.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define ENABLE_LCD 1
#define ENABLE_SOUND 0
#define PEANUT_GB_IS_LITTLE_ENDIAN 1
#include "peanut_gb.h"

#define WINDOW_SCALE 4
#define DEFAULT_W (LCD_WIDTH * WINDOW_SCALE)
#define DEFAULT_H (LCD_HEIGHT * WINDOW_SCALE)
#define TITLEBAR_H XV6_TITLEBAR_HEIGHT
#define CONTROL_W XV6_TITLEBAR_CONTROL_W
#define KEY_ESC 1
#define KEY_BACKSPACE 14
#define KEY_ENTER 28
#define KEY_Z 44
#define KEY_X 45
#define KEY_LEFT 105
#define KEY_RIGHT 106
#define KEY_UP 103
#define KEY_DOWN 108
#define STATUS_PATH "/tmp/peanutgb-status"
#define MAX_PATH_LEN 512

struct frontend {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct wl_keyboard *keyboard;
    struct xdg_wm_base *wm_base;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct xv6_present_buffer present;
    int present_ready;
    int configured;
    int running;
    int width;
    int height;
    int maximized;
    int pointer_x;
    int pointer_y;
    int keys[256];

    struct gb_s gb;
    uint8_t *rom;
    size_t rom_size;
    uint8_t *cart_ram;
    size_t save_size;
    uint32_t fb[LCD_HEIGHT][LCD_WIDTH];
    char rom_title[17];
    char save_path[MAX_PATH_LEN];
    int frames;
    int max_frames;
    int headless;
};

static uint64_t now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ull + (uint64_t)tv.tv_usec / 1000ull;
}

static int key_down(struct frontend *fe, int code)
{
    if (code >= 0 && code < 256 && fe->keys[code])
        return 1;
    code += 8;
    return code >= 0 && code < 256 && fe->keys[code];
}

static int ends_with_case(const char *path, const char *suffix)
{
    size_t n = strlen(path);
    size_t m = strlen(suffix);

    if (n < m)
        return 0;
    path += n - m;
    for (size_t i = 0; i < m; i++) {
        if (tolower((unsigned char)path[i]) !=
            tolower((unsigned char)suffix[i]))
            return 0;
    }
    return 1;
}

static int load_file(const char *path, uint8_t **out, size_t *out_size)
{
    FILE *fp = fopen(path, "rb");
    long size;
    uint8_t *buf;

    if (!fp) {
        fprintf(stderr, "peanutgb: open %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (fseek(fp, 0, SEEK_END) != 0 || (size = ftell(fp)) <= 0 ||
        fseek(fp, 0, SEEK_SET) != 0) {
        fprintf(stderr, "peanutgb: cannot size %s\n", path);
        fclose(fp);
        return -1;
    }
    buf = malloc((size_t)size);
    if (!buf) {
        fclose(fp);
        return -1;
    }
    if (fread(buf, 1, (size_t)size, fp) != (size_t)size) {
        fprintf(stderr, "peanutgb: read %s failed\n", path);
        free(buf);
        fclose(fp);
        return -1;
    }
    fclose(fp);
    *out = buf;
    *out_size = (size_t)size;
    return 0;
}

static void make_save_path(struct frontend *fe, const char *rom_path)
{
    snprintf(fe->save_path, sizeof(fe->save_path), "%s.sav", rom_path);
}

static void load_save(struct frontend *fe)
{
    FILE *fp;

    if (!fe->save_size || !fe->cart_ram)
        return;
    fp = fopen(fe->save_path, "rb");
    if (!fp)
        return;
    if (fread(fe->cart_ram, 1, fe->save_size, fp) != fe->save_size &&
        ferror(fp))
        fprintf(stderr, "peanutgb: save read %s failed\n", fe->save_path);
    fclose(fp);
}

static void write_save(struct frontend *fe)
{
    FILE *fp;

    if (!fe->save_size || !fe->cart_ram || !fe->save_path[0])
        return;
    fp = fopen(fe->save_path, "wb");
    if (!fp) {
        fprintf(stderr, "peanutgb: save %s: %s\n", fe->save_path,
                strerror(errno));
        return;
    }
    (void)fwrite(fe->cart_ram, 1, fe->save_size, fp);
    fclose(fp);
}

uint8_t gb_rom_read(struct gb_s *gb, const uint_fast32_t addr)
{
    struct frontend *fe = gb->direct.priv;
    return addr < fe->rom_size ? fe->rom[addr] : 0xff;
}

uint8_t gb_cart_ram_read(struct gb_s *gb, const uint_fast32_t addr)
{
    struct frontend *fe = gb->direct.priv;
    return (fe->cart_ram && addr < fe->save_size) ? fe->cart_ram[addr] : 0xff;
}

void gb_cart_ram_write(struct gb_s *gb, const uint_fast32_t addr,
                       const uint8_t val)
{
    struct frontend *fe = gb->direct.priv;
    if (fe->cart_ram && addr < fe->save_size)
        fe->cart_ram[addr] = val;
}

void gb_error(struct gb_s *gb, const enum gb_error_e err, const uint16_t addr)
{
    struct frontend *fe = gb->direct.priv;
    static const char *names[] = {
        "unknown", "invalid opcode", "invalid read", "invalid write", ""
    };
    const char *name = "unknown";

    if (err >= 0 && err < GB_INVALID_MAX)
        name = names[err];
    write_save(fe);
    fprintf(stderr, "peanutgb: emulator error: %s at 0x%04x\n", name, addr);
    exit(1);
}

static uint32_t rgb555_to_xrgb(uint16_t c)
{
    uint32_t r = (uint32_t)(c & 0x1f);
    uint32_t g = (uint32_t)((c >> 5) & 0x1f);
    uint32_t b = (uint32_t)((c >> 10) & 0x1f);

    r = (r << 3) | (r >> 2);
    g = (g << 3) | (g >> 2);
    b = (b << 3) | (b >> 2);
    return (r << 16) | (g << 8) | b;
}

void lcd_draw_line(struct gb_s *gb, const uint8_t *pixels,
                   const uint_fast8_t line)
{
    struct frontend *fe = gb->direct.priv;
    static const uint16_t palette[4] = { 0x7fff, 0x56b5, 0x2d6b, 0x0000 };

    if (line >= LCD_HEIGHT)
        return;
    for (int x = 0; x < LCD_WIDTH; x++)
        fe->fb[line][x] = rgb555_to_xrgb(palette[pixels[x] & LCD_COLOUR]);
}

static void update_joypad(struct frontend *fe)
{
    uint8_t joypad = 0xff;

    if (key_down(fe, KEY_Z))
        joypad &= (uint8_t)~JOYPAD_A;
    if (key_down(fe, KEY_X))
        joypad &= (uint8_t)~JOYPAD_B;
    if (key_down(fe, KEY_BACKSPACE))
        joypad &= (uint8_t)~JOYPAD_SELECT;
    if (key_down(fe, KEY_ENTER))
        joypad &= (uint8_t)~JOYPAD_START;
    if (key_down(fe, KEY_RIGHT))
        joypad &= (uint8_t)~JOYPAD_RIGHT;
    if (key_down(fe, KEY_LEFT))
        joypad &= (uint8_t)~JOYPAD_LEFT;
    if (key_down(fe, KEY_UP))
        joypad &= (uint8_t)~JOYPAD_UP;
    if (key_down(fe, KEY_DOWN))
        joypad &= (uint8_t)~JOYPAD_DOWN;
    fe->gb.direct.joypad = joypad;
    if (key_down(fe, KEY_ESC))
        fe->running = 0;
}

static int ensure_present(struct frontend *fe)
{
    int width = fe->width > 0 ? fe->width : DEFAULT_W;
    int height = fe->height > 0 ? fe->height : DEFAULT_H + TITLEBAR_H;

    if (fe->present_ready && fe->present.width == width &&
        fe->present.height == height)
        return 0;
    if (fe->present_ready) {
        xv6_present_buffer_destroy(&fe->present);
        fe->present_ready = 0;
    }
    if (xv6_present_buffer_init(&fe->present, width, height, fe->shm,
                                NULL) < 0)
        return -1;
    fe->present_ready = 1;
    return 0;
}

static void draw_titlebar(struct frontend *fe)
{
    char title[80];

    snprintf(title, sizeof(title), "Peanut-GB - %s",
             fe->rom_title[0] ? fe->rom_title : "Game Boy");
    xv6_titlebar_draw(fe->present.pixels, fe->present.width,
                      fe->present.height, title, fe->maximized, NULL);
}

static void present_frame(struct frontend *fe)
{
    int dst_w, dst_h, scale, off_x, off_y, content_h;
    uint32_t bg = 0x0b0f14;

    if (ensure_present(fe) < 0)
        return;
    for (int y = 0; y < fe->present.height; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)fe->present.pixels +
                                    (size_t)y * (size_t)fe->present.stride);
        for (int x = 0; x < fe->present.width; x++)
            row[x] = bg;
    }
    draw_titlebar(fe);

    content_h = fe->present.height - TITLEBAR_H;
    if (content_h < 1)
        content_h = 1;
    scale = fe->present.width / LCD_WIDTH;
    if (content_h / LCD_HEIGHT < scale)
        scale = content_h / LCD_HEIGHT;
    if (scale < 1)
        scale = 1;
    dst_w = LCD_WIDTH * scale;
    dst_h = LCD_HEIGHT * scale;
    off_x = (fe->present.width - dst_w) / 2;
    off_y = TITLEBAR_H + (content_h - dst_h) / 2;

    for (int y = 0; y < dst_h; y++) {
        int sy = y / scale;
        uint32_t *dst = (uint32_t *)((uint8_t *)fe->present.pixels +
                                    (size_t)(off_y + y) *
                                        (size_t)fe->present.stride) +
                        off_x;
        for (int x = 0; x < dst_w; x++)
            dst[x] = fe->fb[sy][x / scale];
    }

    wl_surface_attach(fe->surface, fe->present.wl_buffer, 0, 0);
    wl_surface_damage(fe->surface, 0, 0, fe->present.width,
                      fe->present.height);
    wl_surface_commit(fe->surface);
}

static void write_status(struct frontend *fe, int status)
{
    FILE *fp = fopen(STATUS_PATH, "w");

    if (!fp)
        return;
    fprintf(fp, "core=peanut-gb title=%s frames=%d status=%d save=%zu\n",
            fe->rom_title[0] ? fe->rom_title : "unknown", fe->frames, status,
            fe->save_size);
    fclose(fp);
}

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
    (void)data; (void)keyboard; (void)serial; (void)surface; (void)keys;
}

static void keyboard_leave(void *data, struct wl_keyboard *keyboard,
                           uint32_t serial, struct wl_surface *surface)
{
    (void)data; (void)keyboard; (void)serial; (void)surface;
}

static void keyboard_key(void *data, struct wl_keyboard *keyboard,
                         uint32_t serial, uint32_t time, uint32_t key,
                         uint32_t state)
{
    struct frontend *fe = data;
    (void)keyboard; (void)serial; (void)time;
    if (key < 256)
        fe->keys[key] = state == WL_KEYBOARD_KEY_STATE_PRESSED;
}

static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard,
                               uint32_t serial, uint32_t mods_depressed,
                               uint32_t mods_latched, uint32_t mods_locked,
                               uint32_t group)
{
    (void)data; (void)keyboard; (void)serial; (void)mods_depressed;
    (void)mods_latched; (void)mods_locked; (void)group;
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *keyboard,
                                 int32_t rate, int32_t delay)
{
    (void)data; (void)keyboard; (void)rate; (void)delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

static void pointer_enter(void *data, struct wl_pointer *pointer,
                          uint32_t serial, struct wl_surface *surface,
                          wl_fixed_t sx, wl_fixed_t sy)
{
    struct frontend *fe = data;
    (void)pointer; (void)serial; (void)surface;
    fe->pointer_x = wl_fixed_to_int(sx);
    fe->pointer_y = wl_fixed_to_int(sy);
}

static void pointer_leave(void *data, struct wl_pointer *pointer,
                          uint32_t serial, struct wl_surface *surface)
{
    (void)data; (void)pointer; (void)serial; (void)surface;
}

static void pointer_motion(void *data, struct wl_pointer *pointer,
                           uint32_t time, wl_fixed_t sx, wl_fixed_t sy)
{
    struct frontend *fe = data;
    (void)pointer; (void)time;
    fe->pointer_x = wl_fixed_to_int(sx);
    fe->pointer_y = wl_fixed_to_int(sy);
}

static void pointer_button(void *data, struct wl_pointer *pointer,
                           uint32_t serial, uint32_t time, uint32_t button,
                           uint32_t state)
{
    struct frontend *fe = data;
    enum xv6_titlebar_action control;
    (void)pointer; (void)time;

    if (button != BTN_LEFT || state != WL_POINTER_BUTTON_STATE_PRESSED)
        return;
    control = xv6_titlebar_hit_test(fe->present_ready ? fe->present.width :
                                    fe->width, fe->pointer_x, fe->pointer_y);
    if (control == XV6_TITLEBAR_DRAG) {
        xdg_toplevel_move(fe->toplevel, fe->seat, serial);
    } else if (control != XV6_TITLEBAR_NONE) {
        xv6_titlebar_activate(control, fe->toplevel, &fe->maximized,
                              &fe->running);
    }
}

static void pointer_axis(void *data, struct wl_pointer *pointer,
                         uint32_t time, uint32_t axis, wl_fixed_t value)
{
    (void)data; (void)pointer; (void)time; (void)axis; (void)value;
}

static void pointer_frame(void *data, struct wl_pointer *pointer)
{
    (void)data; (void)pointer;
}

static void pointer_axis_source(void *data, struct wl_pointer *pointer,
                                uint32_t axis_source)
{
    (void)data; (void)pointer; (void)axis_source;
}

static void pointer_axis_stop(void *data, struct wl_pointer *pointer,
                              uint32_t time, uint32_t axis)
{
    (void)data; (void)pointer; (void)time; (void)axis;
}

static void pointer_axis_discrete(void *data, struct wl_pointer *pointer,
                                  uint32_t axis, int32_t discrete)
{
    (void)data; (void)pointer; (void)axis; (void)discrete;
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
    struct frontend *fe = data;

    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !fe->pointer) {
        fe->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(fe->pointer, &pointer_listener, fe);
    }
    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !fe->keyboard) {
        fe->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(fe->keyboard, &keyboard_listener, fe);
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name)
{
    (void)data; (void)seat; (void)name;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

static void xdg_surface_configure(void *data, struct xdg_surface *surface,
                                  uint32_t serial)
{
    struct frontend *fe = data;
    xdg_surface_ack_configure(surface, serial);
    fe->configured = 1;
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                               int32_t width, int32_t height,
                               struct wl_array *states)
{
    struct frontend *fe = data;
    uint32_t *state;
    (void)toplevel;

    if (width > 0 && height > 0) {
        fe->width = width;
        fe->height = height;
    }
    fe->maximized = 0;
    wl_array_for_each(state, states) {
        if (*state == XDG_TOPLEVEL_STATE_MAXIMIZED)
            fe->maximized = 1;
    }
}

static void toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
    struct frontend *fe = data;
    (void)toplevel;
    fe->running = 0;
}

static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = toplevel_configure,
    .close = toplevel_close,
};

static void wm_base_ping(void *data, struct xdg_wm_base *wm_base,
                         uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = wm_base_ping,
};

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface,
                            uint32_t version)
{
    struct frontend *fe = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        fe->compositor =
            wl_registry_bind(registry, name, &wl_compositor_interface,
                             version < 4 ? version : 4);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        fe->shm = wl_registry_bind(registry, name, &wl_shm_interface,
                                   version < 1 ? version : 1);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        fe->seat = wl_registry_bind(registry, name, &wl_seat_interface,
                                    version < 5 ? version : 5);
        wl_seat_add_listener(fe->seat, &seat_listener, fe);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        fe->wm_base =
            wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(fe->wm_base, &wm_base_listener, fe);
    }
}

static void registry_remove(void *data, struct wl_registry *registry,
                            uint32_t name)
{
    (void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_remove,
};

static int init_wayland(struct frontend *fe)
{
    fe->width = DEFAULT_W;
    fe->height = DEFAULT_H + TITLEBAR_H;
    fe->display = wl_display_connect(NULL);
    if (!fe->display)
        return -1;
    fe->registry = wl_display_get_registry(fe->display);
    wl_registry_add_listener(fe->registry, &registry_listener, fe);
    wl_display_roundtrip(fe->display);
    wl_display_roundtrip(fe->display);
    if (!fe->compositor || !fe->shm || !fe->wm_base)
        return -1;

    fe->surface = wl_compositor_create_surface(fe->compositor);
    fe->xdg_surface = xdg_wm_base_get_xdg_surface(fe->wm_base, fe->surface);
    xdg_surface_add_listener(fe->xdg_surface, &xdg_surface_listener, fe);
    fe->toplevel = xdg_surface_get_toplevel(fe->xdg_surface);
    xdg_toplevel_add_listener(fe->toplevel, &toplevel_listener, fe);
    xdg_toplevel_set_title(fe->toplevel, "Peanut-GB");
    xdg_toplevel_set_app_id(fe->toplevel, "peanutgb");
    xdg_toplevel_set_min_size(fe->toplevel, DEFAULT_W / 2,
                              DEFAULT_H / 2 + TITLEBAR_H);
    wl_surface_commit(fe->surface);
    while (!fe->configured && wl_display_dispatch(fe->display) >= 0)
        ;
    return fe->configured ? 0 : -1;
}

static int pump_wayland(struct frontend *fe)
{
    struct pollfd pfd;
    int ret;

    if (wl_display_dispatch_pending(fe->display) < 0)
        return -1;
    while (wl_display_prepare_read(fe->display) != 0) {
        if (wl_display_dispatch_pending(fe->display) < 0)
            return -1;
    }
    wl_display_flush(fe->display);
    pfd.fd = wl_display_get_fd(fe->display);
    pfd.events = POLLIN;
    pfd.revents = 0;
    ret = poll(&pfd, 1, 0);
    if (ret > 0 && (pfd.revents & POLLIN)) {
        if (wl_display_read_events(fe->display) < 0)
            return -1;
    } else {
        wl_display_cancel_read(fe->display);
        if (ret < 0 && errno != EINTR)
            return -1;
    }
    return wl_display_dispatch_pending(fe->display);
}

static void cleanup(struct frontend *fe)
{
    write_save(fe);
    if (fe->present_ready)
        xv6_present_buffer_destroy(&fe->present);
    if (fe->pointer)
        wl_pointer_destroy(fe->pointer);
    if (fe->keyboard)
        wl_keyboard_destroy(fe->keyboard);
    if (fe->seat)
        wl_seat_destroy(fe->seat);
    if (fe->toplevel)
        xdg_toplevel_destroy(fe->toplevel);
    if (fe->xdg_surface)
        xdg_surface_destroy(fe->xdg_surface);
    if (fe->surface)
        wl_surface_destroy(fe->surface);
    if (fe->wm_base)
        xdg_wm_base_destroy(fe->wm_base);
    if (fe->shm)
        wl_shm_destroy(fe->shm);
    if (fe->compositor)
        wl_compositor_destroy(fe->compositor);
    if (fe->registry)
        wl_registry_destroy(fe->registry);
    if (fe->display)
        wl_display_disconnect(fe->display);
    free(fe->cart_ram);
    free(fe->rom);
}

static void usage(FILE *fp)
{
    fprintf(fp, "usage: peanutgb [--headless] [--frames=N] ROM.gb\n");
    fprintf(fp, "       peanutgb --help\n");
}

static int parse_args(struct frontend *fe, int argc, char **argv,
                      const char **rom_path)
{
    *rom_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--frames=", 9) == 0) {
            fe->max_frames = atoi(argv[i] + 9);
        } else if (strcmp(argv[i], "--headless") == 0) {
            fe->headless = 1;
        } else if (strcmp(argv[i], "--help") == 0 ||
                   strcmp(argv[i], "-h") == 0) {
            usage(stdout);
            return 1;
        } else if (!*rom_path) {
            *rom_path = argv[i];
        } else {
            usage(stderr);
            return -1;
        }
    }
    if (!*rom_path) {
        usage(stderr);
        return -1;
    }
    if (ends_with_case(*rom_path, ".nds")) {
        fprintf(stderr,
                "peanutgb: %s is a Nintendo DS ROM. Pokemon Diamond needs a "
                "DS emulator and a legally dumped .nds image; this port runs "
                "original Game Boy .gb ROMs only.\n",
                *rom_path);
        return -1;
    }
    if (!ends_with_case(*rom_path, ".gb")) {
        fprintf(stderr,
                "peanutgb: expected a legally dumped original Game Boy .gb "
                "ROM, got %s\n",
                *rom_path);
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    struct frontend fe;
    const char *rom_path;
    enum gb_init_error_e init_err;
    size_t save_size = 0;
    uint64_t next_frame;
    char title[64];
    int arg_ret;

    memset(&fe, 0, sizeof(fe));
    fe.running = 1;
    arg_ret = parse_args(&fe, argc, argv, &rom_path);
    if (arg_ret != 0)
        return arg_ret > 0 ? 0 : 2;
    if (fe.headless && fe.max_frames <= 0)
        fe.max_frames = 60;
    if (load_file(rom_path, &fe.rom, &fe.rom_size) < 0)
        return 1;
    make_save_path(&fe, rom_path);

    init_err = gb_init(&fe.gb, gb_rom_read, gb_cart_ram_read,
                       gb_cart_ram_write, gb_error, &fe);
    if (init_err != GB_INIT_NO_ERROR) {
        fprintf(stderr, "peanutgb: gb_init failed: %d\n", init_err);
        cleanup(&fe);
        return 1;
    }
    if (gb_get_save_size_s(&fe.gb, &save_size) == 0 && save_size > 0) {
        fe.cart_ram = calloc(1, save_size);
        if (!fe.cart_ram) {
            cleanup(&fe);
            return 1;
        }
        fe.save_size = save_size;
        load_save(&fe);
    }
    gb_init_lcd(&fe.gb, lcd_draw_line);
    gb_get_rom_name(&fe.gb, fe.rom_title);
    if (!fe.rom_title[0])
        snprintf(fe.rom_title, sizeof(fe.rom_title), "Game Boy");
    snprintf(title, sizeof(title), "Peanut-GB - %s", fe.rom_title);

    if (!fe.headless) {
        if (init_wayland(&fe) < 0) {
            fprintf(stderr, "peanutgb: Wayland setup failed\n");
            cleanup(&fe);
            return 1;
        }
        xdg_toplevel_set_title(fe.toplevel, title);
    }
    memset(fe.fb, 0xff, sizeof(fe.fb));
    next_frame = now_ms();
    write_status(&fe, -1);

    while (fe.running) {
        if (!fe.headless && pump_wayland(&fe) < 0)
            break;
        update_joypad(&fe);
        gb_run_frame(&fe.gb);
        fe.frames++;
        if (!fe.headless)
            present_frame(&fe);
        if (fe.max_frames > 0 && fe.frames >= fe.max_frames)
            break;
        next_frame += 17;
        uint64_t t = now_ms();
        if (next_frame > t)
            usleep((useconds_t)((next_frame - t) * 1000));
        else
            next_frame = t;
    }

    write_status(&fe, 0);
    cleanup(&fe);
    return 0;
}
