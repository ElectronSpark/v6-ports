/*
 * glsmoke.c - small repo-local EGL/GLES2 smoke client.
 *
 * This intentionally does not depend on Mesa.  It uses the xv6-local
 * libEGL/libGLESv2 compatibility layer so the GUI path can validate
 * context/surface/swap semantics before a real EGL/Mesa stack exists.
 */

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "xdg-shell-client-protocol.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define GL_WINDOW_W 480
#define GL_WINDOW_H 360

struct vec2 {
    float x;
    float y;
};

struct vertex {
    GLfloat x;
    GLfloat y;
    GLfloat r;
    GLfloat g;
    GLfloat b;
    GLfloat a;
};

struct app_state {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct xdg_wm_base *wm_base;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct wl_callback *frame_cb;
    struct wl_egl_window *egl_window;
    EGLDisplay egl_display;
    EGLConfig egl_config;
    EGLContext egl_context;
    EGLSurface egl_surface;
    GLuint program;
    GLint attr_pos;
    GLint attr_color;
    int configured;
    int running;
    int frame;
    int max_seconds;
    uint64_t start_ns;
    int resize_seconds;
    uint64_t last_resize_ns;
    int width;
    int height;
    int loop;
};

static uint64_t now_ns(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static struct vec2 rotate_point(float x, float y, float angle, float cx, float cy)
{
    float s = sinf(angle);
    float c = cosf(angle);
    struct vec2 out;

    out.x = cx + x * c - y * s;
    out.y = cy + x * s + y * c;
    return out;
}

static void render_frame(struct app_state *app)
{
    float cx = 0.0f;
    float cy = -0.04f;
    float angle = app->frame * 0.055f;
    float radius = 0.72f;
    struct vec2 a = rotate_point(0.0f, radius, angle, cx, cy);
    struct vec2 b = rotate_point(radius * 0.92f, -radius * 0.72f, angle, cx, cy);
    struct vec2 c = rotate_point(-radius * 0.92f, -radius * 0.72f, angle, cx, cy);
    struct vertex vertices[3] = {
        { a.x, a.y, 0.98f, 0.21f, 0.18f, 1.0f },
        { b.x, b.y, 0.18f, 0.80f, 0.42f, 1.0f },
        { c.x, c.y, 0.20f, 0.42f, 1.0f, 1.0f },
    };

    glViewport(0, 0, app->width, app->height);
    glClearColor(0.04f, 0.07f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(app->program);
    glVertexAttribPointer((GLuint)app->attr_pos, 2, GL_FLOAT, GL_FALSE,
                          sizeof(vertices[0]), &vertices[0].x);
    glVertexAttribPointer((GLuint)app->attr_color, 4, GL_FLOAT, GL_FALSE,
                          sizeof(vertices[0]), &vertices[0].r);
    glEnableVertexAttribArray((GLuint)app->attr_pos);
    glEnableVertexAttribArray((GLuint)app->attr_color);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

static void draw_and_commit(struct app_state *app);

static int recreate_egl_surface(struct app_state *app)
{
    EGLSurface new_surface;

    if (!app->egl_display || !app->egl_window || !app->egl_context)
        return -1;
    if (app->egl_surface != EGL_NO_SURFACE) {
        eglMakeCurrent(app->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
        eglDestroySurface(app->egl_display, app->egl_surface);
        app->egl_surface = EGL_NO_SURFACE;
    }
    wl_egl_window_resize(app->egl_window, app->width, app->height, 0, 0);
    new_surface = eglCreateWindowSurface(app->egl_display, app->egl_config,
                                         (EGLNativeWindowType)app->egl_window,
                                         NULL);
    if (new_surface == EGL_NO_SURFACE ||
        !eglMakeCurrent(app->egl_display, new_surface, new_surface,
                        app->egl_context)) {
        fprintf(stderr, "glsmoke[%d]: resize surface recreate failed (0x%x)\n",
                app->loop, eglGetError());
        return -1;
    }
    app->egl_surface = new_surface;
    return 0;
}

static void frame_done(void *data, struct wl_callback *cb, uint32_t time)
{
    struct app_state *app = data;
    (void)time;

    if (cb)
        wl_callback_destroy(cb);
    app->frame_cb = NULL;
    app->frame++;
    if (app->max_seconds > 0 && app->start_ns > 0 &&
        now_ns() - app->start_ns >= (uint64_t)app->max_seconds * 1000000000ull) {
        app->running = 0;
        return;
    }
    if (app->resize_seconds > 0 && app->last_resize_ns > 0 &&
        now_ns() - app->last_resize_ns >=
            (uint64_t)app->resize_seconds * 1000000000ull) {
        app->last_resize_ns = now_ns();
        if (app->width == GL_WINDOW_W) {
            app->width = 360;
            app->height = 260;
        } else {
            app->width = GL_WINDOW_W;
            app->height = GL_WINDOW_H;
        }
        if (recreate_egl_surface(app) < 0) {
            app->running = 0;
            return;
        }
    }
    draw_and_commit(app);
}

static const struct wl_callback_listener frame_listener = {
    .done = frame_done,
};

static void draw_and_commit(struct app_state *app)
{
    render_frame(app);
    app->frame_cb = wl_surface_frame(app->surface);
    wl_callback_add_listener(app->frame_cb, &frame_listener, app);
    eglSwapBuffers(app->egl_display, app->egl_surface);
}

static GLuint compile_shader(GLenum type, const char *src)
{
    GLuint shader = glCreateShader(type);
    GLint ok = GL_FALSE;

    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
        fprintf(stderr, "glsmoke: shader compile failed\n");
    return shader;
}

static int init_gl(struct app_state *app)
{
    static const char *vs =
        "attribute vec2 a_pos;\n"
        "attribute vec4 a_color;\n"
        "varying vec4 v_color;\n"
        "void main() { v_color = a_color; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
    static const char *fs =
        "precision mediump float;\n"
        "varying vec4 v_color;\n"
        "void main() { gl_FragColor = v_color; }\n";
    EGLint major = 0;
    EGLint minor = 0;
    EGLint nconfigs = 0;
    EGLint context_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    GLuint vshader;
    GLuint fshader;
    GLint ok = GL_FALSE;

    app->egl_display = eglGetDisplay((EGLNativeDisplayType)app->display);
    if (app->egl_display == EGL_NO_DISPLAY ||
        !eglInitialize(app->egl_display, &major, &minor)) {
        fprintf(stderr, "glsmoke: eglInitialize failed (0x%x)\n", eglGetError());
        return -1;
    }
    if (!eglChooseConfig(app->egl_display, NULL, &app->egl_config, 1,
                         &nconfigs) || nconfigs < 1) {
        fprintf(stderr, "glsmoke: eglChooseConfig failed\n");
        return -1;
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        fprintf(stderr, "glsmoke: eglBindAPI failed\n");
        return -1;
    }

    app->egl_window = wl_egl_window_create(app->surface, app->width,
                                           app->height);
    if (!app->egl_window) {
        fprintf(stderr, "glsmoke: wl_egl_window_create failed\n");
        return -1;
    }
    app->egl_context = eglCreateContext(app->egl_display, app->egl_config,
                                        EGL_NO_CONTEXT, context_attrs);
    app->egl_surface = eglCreateWindowSurface(app->egl_display, app->egl_config,
                                             (EGLNativeWindowType)app->egl_window,
                                             NULL);
    if (app->egl_context == EGL_NO_CONTEXT ||
        app->egl_surface == EGL_NO_SURFACE ||
        !eglMakeCurrent(app->egl_display, app->egl_surface, app->egl_surface,
                        app->egl_context)) {
        fprintf(stderr, "glsmoke: EGL context/surface setup failed (0x%x)\n",
                eglGetError());
        return -1;
    }

    vshader = compile_shader(GL_VERTEX_SHADER, vs);
    fshader = compile_shader(GL_FRAGMENT_SHADER, fs);
    app->program = glCreateProgram();
    glAttachShader(app->program, vshader);
    glAttachShader(app->program, fshader);
    glBindAttribLocation(app->program, 0, "a_pos");
    glBindAttribLocation(app->program, 1, "a_color");
    glLinkProgram(app->program);
    glGetProgramiv(app->program, GL_LINK_STATUS, &ok);
    glDeleteShader(vshader);
    glDeleteShader(fshader);
    if (!ok) {
        fprintf(stderr, "glsmoke: program link failed\n");
        return -1;
    }
    app->attr_pos = glGetAttribLocation(app->program, "a_pos");
    app->attr_color = glGetAttribLocation(app->program, "a_color");
    if (app->attr_pos < 0 || app->attr_color < 0) {
        fprintf(stderr, "glsmoke: shader attributes unavailable\n");
        return -1;
    }
    fprintf(stderr, "glsmoke[%d]: EGL %d.%d, GL %s\n", app->loop, major, minor,
            glGetString(GL_VERSION));
    return 0;
}

static void xdg_surface_configure(void *data, struct xdg_surface *surface,
                                  uint32_t serial)
{
    struct app_state *app = data;

    xdg_surface_ack_configure(surface, serial);
    if (!app->configured) {
        app->configured = 1;
        draw_and_commit(app);
    }
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                               int32_t width, int32_t height,
                               struct wl_array *states)
{
    (void)data;
    (void)toplevel;
    (void)width;
    (void)height;
    (void)states;
}

static void toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
    struct app_state *app = data;
    (void)toplevel;
    app->running = 0;
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
    struct app_state *app = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        app->compositor = wl_registry_bind(registry, name,
                                           &wl_compositor_interface,
                                           version > 4 ? 4 : version);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        app->wm_base = wl_registry_bind(registry, name,
                                        &xdg_wm_base_interface,
                                        version > 2 ? 2 : version);
        xdg_wm_base_add_listener(app->wm_base, &wm_base_listener, app);
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

static int init_wayland(struct app_state *app)
{
    app->display = wl_display_connect(NULL);
    if (!app->display) {
        fprintf(stderr, "glsmoke: wl_display_connect failed\n");
        return -1;
    }

    app->registry = wl_display_get_registry(app->display);
    wl_registry_add_listener(app->registry, &registry_listener, app);
    wl_display_roundtrip(app->display);
    wl_display_roundtrip(app->display);

    if (!app->compositor || !app->wm_base) {
        fprintf(stderr, "glsmoke: compositor/xdg globals unavailable\n");
        return -1;
    }

    app->surface = wl_compositor_create_surface(app->compositor);
    app->xdg_surface = xdg_wm_base_get_xdg_surface(app->wm_base, app->surface);
    xdg_surface_add_listener(app->xdg_surface, &xdg_surface_listener, app);
    app->toplevel = xdg_surface_get_toplevel(app->xdg_surface);
    xdg_toplevel_add_listener(app->toplevel, &toplevel_listener, app);
    xdg_toplevel_set_title(app->toplevel, "xv6 GL Smoke");
    xdg_toplevel_set_app_id(app->toplevel, "glsmoke");
    if (init_gl(app) < 0)
        return -1;
    wl_surface_commit(app->surface);
    return 0;
}

static void cleanup(struct app_state *app)
{
    if (app->display)
        wl_display_roundtrip(app->display);
    if (app->frame_cb)
        wl_callback_destroy(app->frame_cb);
    if (app->egl_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(app->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
        if (app->program)
            glDeleteProgram(app->program);
        if (app->egl_surface != EGL_NO_SURFACE)
            eglDestroySurface(app->egl_display, app->egl_surface);
        if (app->egl_context != EGL_NO_CONTEXT)
            eglDestroyContext(app->egl_display, app->egl_context);
        eglTerminate(app->egl_display);
    }
    if (app->egl_window)
        wl_egl_window_destroy(app->egl_window);
    if (app->toplevel)
        xdg_toplevel_destroy(app->toplevel);
    if (app->xdg_surface)
        xdg_surface_destroy(app->xdg_surface);
    if (app->surface)
        wl_surface_destroy(app->surface);
    if (app->wm_base)
        xdg_wm_base_destroy(app->wm_base);
    if (app->compositor)
        wl_compositor_destroy(app->compositor);
    if (app->registry)
        wl_registry_destroy(app->registry);
    if (app->display)
        wl_display_disconnect(app->display);
}

static int parse_positive_arg(const char *arg, const char *prefix,
                              int fallback)
{
    size_t len = strlen(prefix);
    int value;

    if (strncmp(arg, prefix, len) != 0)
        return fallback;
    value = atoi(arg + len);
    return value > 0 ? value : fallback;
}

static void print_usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s [--seconds=N] [--loops=N] [--resize-seconds=N]\n"
            "  --seconds=N  stop each EGL lifecycle after N seconds\n"
            "  --loops=N   repeat Wayland/EGL create-draw-destroy N times\n"
            "  --resize-seconds=N  recreate the EGL surface every N seconds\n",
            argv0);
}

static int run_loop(int loop, int seconds, int resize_seconds)
{
    struct app_state app;
    int rc = 0;

    memset(&app, 0, sizeof(app));
    app.egl_display = EGL_NO_DISPLAY;
    app.egl_context = EGL_NO_CONTEXT;
    app.egl_surface = EGL_NO_SURFACE;
    app.running = 1;
    app.max_seconds = seconds;
    app.resize_seconds = resize_seconds;
    app.width = GL_WINDOW_W;
    app.height = GL_WINDOW_H;
    app.loop = loop;

    if (init_wayland(&app) < 0) {
        cleanup(&app);
        return 1;
    }

    app.start_ns = now_ns();
    app.last_resize_ns = app.start_ns;
    while (app.running && wl_display_dispatch(app.display) >= 0)
        ;

    if (app.running)
        rc = 1;
    cleanup(&app);
    fprintf(stderr, "glsmoke[%d]: complete frames=%d status=%d\n", loop,
            app.frame, rc);
    return rc;
}

int main(int argc, char **argv)
{
    int seconds = 0;
    int loops = 1;
    int resize_seconds = 0;

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--seconds=", 10) == 0) {
            seconds = parse_positive_arg(argv[i], "--seconds=", seconds);
        } else if (strncmp(argv[i], "--loops=", 8) == 0) {
            loops = parse_positive_arg(argv[i], "--loops=", loops);
        } else if (strncmp(argv[i], "--resize-seconds=", 17) == 0) {
            resize_seconds = parse_positive_arg(argv[i], "--resize-seconds=",
                                                resize_seconds);
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            print_usage(argv[0]);
            return 2;
        }
    }

    for (int loop = 0; loop < loops; loop++) {
        int rc = run_loop(loop, seconds, resize_seconds);

        if (rc != 0)
            return rc;
    }
    return 0;
}
