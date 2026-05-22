/*
 * mesaglsmoke.c - Mesa-backed Wayland smoke client for xv6.
 *
 * This uses real Mesa surfaceless EGL/GLES for rendering, then copies the
 * rendered pixels into a Wayland buffer.  Prefer the xv6 GPU BO import path so
 * the compositor sees the same buffer ABI used by the local EGL shim; fall back
 * to wl_shm when the private GPU manager is unavailable.
 */

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#include "gl_program.h"
#include "mesawlegl_sphere.h"
#include "pixel_fps_overlay.h"
#include "xv6_present_buffer.h"
#include "xdg-shell-client-protocol.h"

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif
#ifndef GL_IMPLEMENTATION_COLOR_READ_FORMAT
#define GL_IMPLEMENTATION_COLOR_READ_FORMAT 0x8B9B
#endif
#ifndef GL_IMPLEMENTATION_COLOR_READ_TYPE
#define GL_IMPLEMENTATION_COLOR_READ_TYPE 0x8B9A
#endif

#define WINDOW_W 480
#define WINDOW_H 360
#define DEMO_W 360
#define DEMO_H 260
#define DEFAULT_SPHERE_QUALITY 4
#define DEMO_SPHERE_QUALITY 3

struct vertex {
    GLfloat x;
    GLfloat y;
    GLfloat z;
    GLfloat r;
    GLfloat g;
    GLfloat b;
    GLfloat a;
};

struct app_state {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_proxy *gpu_manager;
    struct xdg_wm_base *wm_base;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct wl_callback *frame_cb;
    struct xv6_present_buffer buffer;
    EGLDisplay egl_display;
    EGLConfig egl_config;
    EGLContext egl_context;
    EGLSurface egl_surface;
    GLuint program;
    GLuint sphere_program;
    GLuint sphere_vbo;
    GLint attr_pos;
    GLint attr_color;
    GLint attr_sphere_pos;
    GLint attr_sphere_normal;
    GLint uniform_sphere_mvp;
    GLint uniform_sphere_model;
    GLint uniform_sphere_light;
    int configured;
    int running;
    int frame;
    int max_frames;
    int resize_every;
    int width;
    int height;
    int pending_width;
    int pending_height;
    int loop;
    int sphere_demo;
    int sphere_quality;
    int fixed_size;
    int sphere_vertex_count;
    GLenum read_format;
    uint8_t *readback;
    size_t readback_size;
    uint64_t start_ns;
    uint64_t fps_last_ns;
    int fps_last_frame;
    char fps_text[PIXEL_FPS_OVERLAY_TEXT_MAX];
};

static uint64_t monotonic_ns(void);
static void draw_and_commit(struct app_state *app);

static EGLDisplay get_surfaceless_display(void)
{
    typedef EGLDisplay (*get_platform_display_fn)(EGLenum, void *,
                                                  const EGLAttrib *);
    const char *client_ext = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    get_platform_display_fn get_platform_display =
        (get_platform_display_fn)eglGetProcAddress("eglGetPlatformDisplay");

    if (get_platform_display != NULL && client_ext != NULL &&
        strstr(client_ext, "EGL_MESA_platform_surfaceless") != NULL)
        return get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, NULL, NULL);
    return eglGetDisplay(EGL_DEFAULT_DISPLAY);
}

static int gl_has_extension(const char *name)
{
    const char *all = (const char *)glGetString(GL_EXTENSIONS);
    const char *exts = all;
    size_t name_len;

    if (!exts || !name || !*name)
        return 0;
    name_len = strlen(name);
    while ((exts = strstr(exts, name)) != NULL) {
        char before = exts == all ? ' ' : exts[-1];
        char after = exts[name_len];

        if ((before == ' ' || before == '\0') &&
            (after == ' ' || after == '\0'))
            return 1;
        exts += name_len;
    }
    return 0;
}

static int init_sphere_resources(struct app_state *app)
{
    static const char *sphere_vs =
        "attribute vec3 a_pos;\n"
        "attribute vec3 a_normal;\n"
        "uniform mat4 u_mvp;\n"
        "uniform mat4 u_model;\n"
        "varying vec3 v_normal;\n"
        "varying vec3 v_world;\n"
        "void main() {\n"
        "  vec4 world = u_model * vec4(a_pos, 1.0);\n"
        "  v_world = world.xyz;\n"
        "  v_normal = (u_model * vec4(a_normal, 0.0)).xyz;\n"
        "  gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
        "}\n";
    static const char *sphere_fs =
        "precision mediump float;\n"
        "varying vec3 v_normal;\n"
        "varying vec3 v_world;\n"
        "uniform vec3 u_light;\n"
        "void main() {\n"
        "  vec3 n = normalize(v_normal);\n"
        "  vec3 l = normalize(u_light - v_world);\n"
        "  float diffuse = max(dot(n, l), 0.0);\n"
        "  float rim = pow(1.0 - max(dot(n, vec3(0.0, 0.0, 1.0)), 0.0), 2.0);\n"
        "  vec3 cool = vec3(0.10, 0.45, 0.95);\n"
        "  vec3 warm = vec3(0.90, 0.62, 0.22);\n"
        "  vec3 base = mix(cool, warm, n.y * 0.5 + 0.5);\n"
        "  vec3 color = base * (0.18 + diffuse * 0.88) + vec3(0.80, 0.92, 1.0) * rim * 0.25;\n"
        "  gl_FragColor = vec4(color, 1.0);\n"
        "}\n";
    int frequency = app->sphere_quality;
    int count;
    struct sphere_vertex *vertices;

    if (frequency < 1)
        frequency = DEFAULT_SPHERE_QUALITY;
    if (frequency > 8)
        frequency = 8;
    count = mesawlegl_poly_sphere_vertex_count(frequency);
    vertices = calloc((size_t)count, sizeof(*vertices));
    if (!vertices)
        return -1;

    if (mesawlegl_build_poly_sphere(vertices, count, frequency) != count) {
        free(vertices);
        return -1;
    }

    app->sphere_program =
        xv6_gl_link_program("mesaglsmoke", sphere_vs, sphere_fs);
    if (!app->sphere_program) {
        free(vertices);
        return -1;
    }
    app->attr_sphere_pos =
        glGetAttribLocation(app->sphere_program, "a_pos");
    app->attr_sphere_normal =
        glGetAttribLocation(app->sphere_program, "a_normal");
    app->uniform_sphere_mvp =
        glGetUniformLocation(app->sphere_program, "u_mvp");
    app->uniform_sphere_model =
        glGetUniformLocation(app->sphere_program, "u_model");
    app->uniform_sphere_light =
        glGetUniformLocation(app->sphere_program, "u_light");
    if (app->attr_sphere_pos < 0 || app->attr_sphere_normal < 0 ||
        app->uniform_sphere_mvp < 0 || app->uniform_sphere_model < 0 ||
        app->uniform_sphere_light < 0) {
        free(vertices);
        return -1;
    }

    glGenBuffers(1, &app->sphere_vbo);
    if (!app->sphere_vbo) {
        free(vertices);
        return -1;
    }
    glBindBuffer(GL_ARRAY_BUFFER, app->sphere_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(count * sizeof(*vertices)),
                 vertices, GL_STATIC_DRAW);
    app->sphere_vertex_count = count;
    free(vertices);
    return glGetError() == GL_NO_ERROR ? 0 : -1;
}

static int init_mesa(struct app_state *app)
{
    static const char *vs =
        "attribute vec3 a_pos;\n"
        "attribute vec4 a_color;\n"
        "varying vec4 v_color;\n"
        "void main() { v_color = a_color; gl_Position = vec4(a_pos, 1.0); }\n";
    static const char *fs =
        "precision mediump float;\n"
        "varying vec4 v_color;\n"
        "void main() { gl_FragColor = v_color; }\n";
    EGLint config_attrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_NONE
    };
    EGLint pbuffer_attrs[] = {
        EGL_WIDTH, app->width,
        EGL_HEIGHT, app->height,
        EGL_NONE
    };
    EGLint context_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLint major = 0;
    EGLint minor = 0;
    EGLint nconfigs = 0;

    app->egl_display = get_surfaceless_display();
    if (app->egl_display == EGL_NO_DISPLAY ||
        !eglInitialize(app->egl_display, &major, &minor)) {
        fprintf(stderr, "mesaglsmoke: eglInitialize failed (0x%x)\n",
                eglGetError());
        return -1;
    }
    if (!eglChooseConfig(app->egl_display, config_attrs, &app->egl_config, 1,
                         &nconfigs) || nconfigs < 1) {
        fprintf(stderr, "mesaglsmoke: eglChooseConfig failed (0x%x)\n",
                eglGetError());
        return -1;
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        fprintf(stderr, "mesaglsmoke: eglBindAPI failed (0x%x)\n",
                eglGetError());
        return -1;
    }

    app->egl_context = eglCreateContext(app->egl_display, app->egl_config,
                                        EGL_NO_CONTEXT, context_attrs);
    app->egl_surface = eglCreatePbufferSurface(app->egl_display,
                                               app->egl_config,
                                               pbuffer_attrs);
    if (app->egl_context == EGL_NO_CONTEXT ||
        app->egl_surface == EGL_NO_SURFACE ||
        !eglMakeCurrent(app->egl_display, app->egl_surface, app->egl_surface,
                        app->egl_context)) {
        fprintf(stderr, "mesaglsmoke: EGL context/surface setup failed (0x%x)\n",
                eglGetError());
        return -1;
    }

    app->program = xv6_gl_link_program("mesaglsmoke", vs, fs);
    if (!app->program)
        return -1;
    app->attr_pos = glGetAttribLocation(app->program, "a_pos");
    app->attr_color = glGetAttribLocation(app->program, "a_color");
    if (app->attr_pos < 0 || app->attr_color < 0)
        return -1;

    if (app->sphere_demo && init_sphere_resources(app) < 0) {
        fprintf(stderr, "mesaglsmoke: sphere demo resource setup failed\n");
        return -1;
    }

    app->read_format = GL_RGBA;
    {
        GLint impl_format = 0;
        GLint impl_type = 0;

        glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT, &impl_format);
        glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_TYPE, &impl_type);
        if ((impl_format == GL_BGRA_EXT && impl_type == GL_UNSIGNED_BYTE) ||
            gl_has_extension("GL_EXT_read_format_bgra"))
            app->read_format = GL_BGRA_EXT;
    }

    fprintf(stderr,
            "mesaglsmoke: EGL %d.%d GL %s renderer=%s buffer=%s read=%s%s%s%d\n",
            major, minor, glGetString(GL_VERSION), glGetString(GL_RENDERER),
            app->buffer.bo_backed ? "xv6-gpu-bo" : "wl-shm",
            app->read_format == GL_BGRA_EXT ? "bgra" : "rgba",
            app->sphere_demo ? " spherical-poly-demo" : "",
            app->sphere_demo ? " quality=" : "",
            app->sphere_demo ? app->sphere_quality : 0);
    return 0;
}

static int recreate_mesa_surface(struct app_state *app)
{
    EGLint pbuffer_attrs[] = {
        EGL_WIDTH, app->width,
        EGL_HEIGHT, app->height,
        EGL_NONE
    };
    EGLSurface surface;

    if (app->egl_display == EGL_NO_DISPLAY ||
        app->egl_context == EGL_NO_CONTEXT)
        return -1;

    eglMakeCurrent(app->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                   EGL_NO_CONTEXT);
    if (app->egl_surface != EGL_NO_SURFACE) {
        eglDestroySurface(app->egl_display, app->egl_surface);
        app->egl_surface = EGL_NO_SURFACE;
    }

    surface = eglCreatePbufferSurface(app->egl_display, app->egl_config,
                                      pbuffer_attrs);
    if (surface == EGL_NO_SURFACE ||
        !eglMakeCurrent(app->egl_display, surface, surface,
                        app->egl_context)) {
        fprintf(stderr, "mesaglsmoke[%d]: pbuffer resize failed (0x%x)\n",
                app->loop, eglGetError());
        return -1;
    }
    app->egl_surface = surface;
    return 0;
}

static int resize_surface_and_buffer_to(struct app_state *app, int width,
                                        int height)
{
    if (width < 200)
        width = 200;
    if (height < 150)
        height = 150;
    if (width == app->width && height == app->height)
        return 0;

    app->width = width;
    app->height = height;

    xv6_present_buffer_destroy(&app->buffer);
    if (xv6_present_buffer_init(&app->buffer, app->width, app->height,
                                app->shm, app->gpu_manager) < 0)
        return -1;
    if (recreate_mesa_surface(app) < 0)
        return -1;
    return 0;
}

static int resize_surface_and_buffer(struct app_state *app)
{
    if (app->width == WINDOW_W)
        return resize_surface_and_buffer_to(app, 360, 260);
    return resize_surface_and_buffer_to(app, WINDOW_W, WINDOW_H);
}

static void render_frame(struct app_state *app)
{
    float angle = app->frame * 0.055f;
    float s = sinf(angle);
    float c = cosf(angle);
    float r = 0.72f;
    struct vertex vertices[3] = {
        { -s * r, c * r - 0.04f, 0.0f, 0.98f, 0.21f, 0.18f, 1.0f },
        { (0.92f * c + 0.72f * s) * r,
          (0.92f * s - 0.72f * c) * r - 0.04f, 0.0f,
          0.18f, 0.80f, 0.42f, 1.0f },
        { (-0.92f * c + 0.72f * s) * r,
          (-0.92f * s - 0.72f * c) * r - 0.04f, 0.0f,
          0.20f, 0.42f, 1.0f, 1.0f },
    };

    glViewport(0, 0, app->width, app->height);
    glClearColor(0.03f, 0.055f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(app->program);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glVertexAttribPointer((GLuint)app->attr_pos, 3, GL_FLOAT, GL_FALSE,
                          sizeof(vertices[0]), &vertices[0].x);
    glVertexAttribPointer((GLuint)app->attr_color, 4, GL_FLOAT, GL_FALSE,
                          sizeof(vertices[0]), &vertices[0].r);
    glEnableVertexAttribArray((GLuint)app->attr_pos);
    glEnableVertexAttribArray((GLuint)app->attr_color);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

static void render_sphere_frame(struct app_state *app)
{
    float aspect = app->height > 0 ? (float)app->width / (float)app->height :
                                     1.0f;
    float projection[16];
    float view[16];
    float rx[16];
    float ry[16];
    float model[16];
    float pv[16];
    float mvp[16];
    float angle = app->frame * 0.022f;
    const GLfloat light[3] = { 1.6f, 1.2f, 2.8f };

    mesawlegl_mat4_perspective(projection, 58.0f * (float)M_PI / 180.0f,
                               aspect, 0.1f, 16.0f);
    mesawlegl_mat4_translate(view, 0.0f, 0.0f, -3.6f);
    mesawlegl_mat4_rotate_y(ry, angle);
    mesawlegl_mat4_rotate_x(rx, 0.35f * sinf(angle * 0.43f));
    mesawlegl_mat4_mul(model, ry, rx);
    mesawlegl_mat4_mul(pv, projection, view);
    mesawlegl_mat4_mul(mvp, pv, model);

    glViewport(0, 0, app->width, app->height);
    glClearColor(0.015f, 0.022f, 0.032f, 1.0f);
    glClearDepthf(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    glUseProgram(app->sphere_program);
    glUniformMatrix4fv(app->uniform_sphere_mvp, 1, GL_FALSE, mvp);
    glUniformMatrix4fv(app->uniform_sphere_model, 1, GL_FALSE, model);
    glUniform3fv(app->uniform_sphere_light, 1, light);
    glBindBuffer(GL_ARRAY_BUFFER, app->sphere_vbo);
    glVertexAttribPointer((GLuint)app->attr_sphere_pos, 3, GL_FLOAT,
                          GL_FALSE, sizeof(struct sphere_vertex), (void *)0);
    glVertexAttribPointer((GLuint)app->attr_sphere_normal, 3, GL_FLOAT,
                          GL_FALSE, sizeof(struct sphere_vertex),
                          (void *)(3 * sizeof(GLfloat)));
    glEnableVertexAttribArray((GLuint)app->attr_sphere_pos);
    glEnableVertexAttribArray((GLuint)app->attr_sphere_normal);
    glDrawArrays(GL_TRIANGLES, 0, app->sphere_vertex_count);
    glDisable(GL_CULL_FACE);
}

static int copy_pixels_to_wayland_buffer(struct app_state *app)
{
    size_t bytes = (size_t)app->width * (size_t)app->height * 4;

    if (app->readback_size < bytes) {
        uint8_t *new_readback = realloc(app->readback, bytes);

        if (!new_readback)
            return -1;
        app->readback = new_readback;
        app->readback_size = bytes;
    }
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, app->width, app->height, app->read_format,
                 GL_UNSIGNED_BYTE,
                 app->readback);
    for (int y = 0; y < app->height; y++) {
        uint32_t *dst = (uint32_t *)((uint8_t *)app->buffer.pixels +
                                     (size_t)y * (size_t)app->buffer.stride);
        uint32_t *src = (uint32_t *)(app->readback +
                                     (size_t)(app->height - 1 - y) *
                                     (size_t)app->width * 4);
        if (app->read_format == GL_BGRA_EXT) {
            memcpy(dst, src, (size_t)app->width * 4);
            continue;
        }
        for (int x = 0; x < app->width; x++) {
            uint32_t rgba = src[x];

            dst[x] = 0xff000000u | ((rgba & 0x000000ffu) << 16) |
                     (rgba & 0x0000ff00u) |
                     ((rgba & 0x00ff0000u) >> 16);
        }
    }
    return 0;
}

static void update_fps_overlay(struct app_state *app)
{
    uint64_t now = monotonic_ns();

    if (app->fps_text[0] == '\0')
        snprintf(app->fps_text, sizeof(app->fps_text), "FPS --.-");
    if (app->fps_last_ns == 0) {
        app->fps_last_ns = now;
        app->fps_last_frame = app->frame;
        return;
    }
    if (now > app->fps_last_ns + 1000000000ull) {
        double elapsed = (double)(now - app->fps_last_ns) / 1000000000.0;
        double fps = elapsed > 0.0 ?
                     (double)(app->frame - app->fps_last_frame) / elapsed :
                     0.0;
        char title[64];

        snprintf(app->fps_text, sizeof(app->fps_text), "FPS %.1f", fps);
        snprintf(title, sizeof(title), "Mesa GL Smoke - %.1f FPS", fps);
        xdg_toplevel_set_title(app->toplevel, title);
        fprintf(stderr, "mesaglsmoke[%d]: fps=%.1f\n", app->loop, fps);
        app->fps_last_ns = now;
        app->fps_last_frame = app->frame;
    }
}

static void draw_fps_overlay(struct app_state *app)
{
    pixel_fps_overlay_draw(app->buffer.pixels, app->width, app->height,
                           app->buffer.stride, app->fps_text);
}

static void frame_done(void *data, struct wl_callback *cb, uint32_t time)
{
    struct app_state *app = data;
    (void)time;

    if (cb)
        wl_callback_destroy(cb);
    app->frame_cb = NULL;
    app->frame++;
    if (app->max_frames > 0 && app->frame >= app->max_frames) {
        app->running = 0;
        return;
    }
    if (app->resize_every > 0 && app->frame > 0 &&
        app->frame % app->resize_every == 0) {
        if (resize_surface_and_buffer(app) < 0) {
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
    if (app->sphere_demo)
        render_sphere_frame(app);
    else
        render_frame(app);
    if (glGetError() != GL_NO_ERROR) {
        fprintf(stderr, "mesaglsmoke[%d]: GL error during frame\n",
                app->loop);
        app->running = 0;
        return;
    }
    if (copy_pixels_to_wayland_buffer(app) < 0) {
        app->running = 0;
        return;
    }
    if (app->sphere_demo) {
        update_fps_overlay(app);
        draw_fps_overlay(app);
    }
    app->frame_cb = wl_surface_frame(app->surface);
    wl_callback_add_listener(app->frame_cb, &frame_listener, app);
    wl_surface_attach(app->surface, app->buffer.wl_buffer, 0, 0);
    wl_surface_damage(app->surface, 0, 0, app->width, app->height);
    wl_surface_commit(app->surface);
}

static void xdg_surface_configure(void *data, struct xdg_surface *surface,
                                  uint32_t serial)
{
    struct app_state *app = data;

    xdg_surface_ack_configure(surface, serial);
    if (app->pending_width > 0 && app->pending_height > 0) {
        if (resize_surface_and_buffer_to(app, app->pending_width,
                                         app->pending_height) < 0) {
            app->running = 0;
            return;
        }
        app->pending_width = 0;
        app->pending_height = 0;
    }
    if (!app->configured) {
        app->configured = 1;
        draw_and_commit(app);
    } else if (!app->frame_cb) {
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
    struct app_state *app = data;
    (void)toplevel;
    (void)states;

    if (app->fixed_size)
        return;
    if (width > 0 && height > 0) {
        app->pending_width = width;
        app->pending_height = height;
    }
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
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        app->shm = wl_registry_bind(registry, name, &wl_shm_interface,
                                    version > 1 ? 1 : version);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        app->wm_base = wl_registry_bind(registry, name,
                                        &xdg_wm_base_interface,
                                        version > 2 ? 2 : version);
        xdg_wm_base_add_listener(app->wm_base, &wm_base_listener, app);
    } else if (strcmp(interface, xv6_gpu_buffer_manager_interface.name) == 0 &&
               !app->gpu_manager) {
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

static int init_wayland(struct app_state *app)
{
    app->display = wl_display_connect(NULL);
    if (!app->display) {
        fprintf(stderr, "mesaglsmoke: wl_display_connect failed\n");
        return -1;
    }
    app->registry = wl_display_get_registry(app->display);
    wl_registry_add_listener(app->registry, &registry_listener, app);
    wl_display_roundtrip(app->display);
    wl_display_roundtrip(app->display);

    if (!app->compositor || !app->wm_base || (!app->gpu_manager && !app->shm)) {
        fprintf(stderr, "mesaglsmoke: required Wayland globals unavailable\n");
        return -1;
    }

    app->surface = wl_compositor_create_surface(app->compositor);
    app->xdg_surface = xdg_wm_base_get_xdg_surface(app->wm_base, app->surface);
    xdg_surface_add_listener(app->xdg_surface, &xdg_surface_listener, app);
    app->toplevel = xdg_surface_get_toplevel(app->xdg_surface);
    xdg_toplevel_add_listener(app->toplevel, &toplevel_listener, app);
    if (app->fixed_size) {
        xdg_toplevel_set_min_size(app->toplevel, app->width, app->height);
        xdg_toplevel_set_max_size(app->toplevel, app->width, app->height);
    }
    xdg_toplevel_set_title(app->toplevel, "Mesa GL Smoke");
    xdg_toplevel_set_app_id(app->toplevel, "mesaglsmoke");

    if (xv6_present_buffer_init(&app->buffer, app->width, app->height,
                                app->shm, app->gpu_manager) < 0 ||
        init_mesa(app) < 0)
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
        if (app->sphere_vbo)
            glDeleteBuffers(1, &app->sphere_vbo);
        if (app->sphere_program)
            glDeleteProgram(app->sphere_program);
        if (app->program)
            glDeleteProgram(app->program);
        if (app->egl_surface != EGL_NO_SURFACE)
            eglDestroySurface(app->egl_display, app->egl_surface);
        if (app->egl_context != EGL_NO_CONTEXT)
            eglDestroyContext(app->egl_display, app->egl_context);
        eglTerminate(app->egl_display);
    }
    xv6_present_buffer_destroy(&app->buffer);
    free(app->readback);
    app->readback = NULL;
    app->readback_size = 0;
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

static int parse_nonnegative_arg(const char *arg, const char *prefix,
                                 int fallback)
{
    size_t len = strlen(prefix);
    int value;

    if (strncmp(arg, prefix, len) != 0)
        return fallback;
    value = atoi(arg + len);
    return value >= 0 ? value : fallback;
}

static uint64_t monotonic_ns(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int run_client(int loop, int frames, int resize_every, int sphere_demo,
                      int width, int height, int sphere_quality,
                      int fixed_size)
{
    struct app_state app;
    int rc = 0;
    uint64_t end_ns;
    double elapsed_sec = 0.0;
    double fps = 0.0;

    memset(&app, 0, sizeof(app));
    app.egl_display = EGL_NO_DISPLAY;
    app.egl_context = EGL_NO_CONTEXT;
    app.egl_surface = EGL_NO_SURFACE;
    app.buffer.fd = -1;
    app.buffer.fb_fd = -1;
    app.running = 1;
    app.max_frames = frames;
    app.resize_every = resize_every;
    app.width = width;
    app.height = height;
    app.loop = loop;
    app.sphere_demo = sphere_demo;
    app.sphere_quality = sphere_quality;
    app.fixed_size = fixed_size;

    if (init_wayland(&app) < 0) {
        cleanup(&app);
        return 1;
    }

    app.start_ns = monotonic_ns();
    while (app.running && wl_display_dispatch(app.display) >= 0)
        ;
    if (app.running)
        rc = 1;
    end_ns = monotonic_ns();
    if (app.start_ns && end_ns > app.start_ns) {
        elapsed_sec = (double)(end_ns - app.start_ns) / 1000000000.0;
        fps = elapsed_sec > 0.0 ? (double)app.frame / elapsed_sec : 0.0;
    }
    cleanup(&app);
    fprintf(stderr,
            "mesaglsmoke[%d]: complete frames=%d status=%d elapsed=%.3fs fps=%.1f\n",
            loop, app.frame, rc, elapsed_sec, fps);
    return rc;
}

int main(int argc, char **argv)
{
    int frames = 120;
    int loops = 1;
    int resize_every = 0;
    int sphere_demo = 0;
    int width = WINDOW_W;
    int height = WINDOW_H;
    int sphere_quality = DEFAULT_SPHERE_QUALITY;
    int fixed_size = 0;
    int allow_resize = 0;
    int frames_set = 0;
    int width_set = 0;
    int height_set = 0;
    int quality_set = 0;
    int rc = 0;

    setenv("LIBGL_ALWAYS_SOFTWARE", "1", 0);
    setenv("MESA_LOADER_DRIVER_OVERRIDE", "softpipe", 0);
    setenv("LIBGL_DRIVERS_PATH", "/usr/lib/x86_64-linux-gnu/dri", 0);

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--frames=", 9) == 0) {
            frames = parse_nonnegative_arg(argv[i], "--frames=", frames);
            frames_set = 1;
        } else if (strncmp(argv[i], "--loops=", 8) == 0) {
            loops = parse_positive_arg(argv[i], "--loops=", loops);
        } else if (strncmp(argv[i], "--resize-every=", 15) == 0) {
            resize_every = parse_positive_arg(argv[i], "--resize-every=",
                                              resize_every);
        } else if (strncmp(argv[i], "--width=", 8) == 0) {
            width = parse_positive_arg(argv[i], "--width=", width);
            width_set = 1;
            fixed_size = 1;
        } else if (strncmp(argv[i], "--height=", 9) == 0) {
            height = parse_positive_arg(argv[i], "--height=", height);
            height_set = 1;
            fixed_size = 1;
        } else if (strncmp(argv[i], "--quality=", 10) == 0) {
            sphere_quality = parse_positive_arg(argv[i], "--quality=",
                                                sphere_quality);
            quality_set = 1;
        } else if (strcmp(argv[i], "--allow-resize") == 0) {
            allow_resize = 1;
        } else if (strcmp(argv[i], "--demo") == 0) {
            sphere_demo = 1;
            if (!frames_set)
                frames = 0;
            fixed_size = 1;
            if (!width_set)
                width = DEMO_W;
            if (!height_set)
                height = DEMO_H;
            if (!quality_set)
                sphere_quality = DEMO_SPHERE_QUALITY;
        } else if (strcmp(argv[i], "--help") == 0) {
            fprintf(stderr,
                    "usage: %s [--frames=N] [--loops=N] [--resize-every=N] [--width=N] [--height=N] [--quality=N] [--allow-resize] [--demo]\n",
                    argv[0]);
            return 0;
        } else {
            fprintf(stderr, "mesaglsmoke: unknown option '%s'\n", argv[i]);
            return 2;
        }
    }
    if (width < 200)
        width = 200;
    if (height < 150)
        height = 150;
    if (sphere_quality < 1)
        sphere_quality = 1;
    if (sphere_quality > 8)
        sphere_quality = 8;
    if (resize_every > 0 || allow_resize)
        fixed_size = 0;

    for (int loop = 1; loop <= loops; loop++) {
        rc = run_client(loop, frames, resize_every, sphere_demo, width,
                        height, sphere_quality, fixed_size);
        if (rc != 0)
            break;
    }
    return rc;
}
