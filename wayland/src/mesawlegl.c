/*
 * mesawlegl.c - Mesa-native Wayland EGL smoke client for xv6.
 *
 * Unlike mesaglsmoke, this does not render into a surfaceless pbuffer and copy
 * pixels into an xv6 buffer.  It exercises Mesa's Wayland platform path:
 * wl_egl_window -> eglCreateWindowSurface -> eglSwapBuffers.
 */

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <math.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "gl_fps_overlay.h"
#include "gl_program.h"
#include "mesawlegl_sphere.h"
#include "xdg-shell-client-protocol.h"

#ifndef EGL_PLATFORM_WAYLAND_KHR
#define EGL_PLATFORM_WAYLAND_KHR 0x31D8
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8 0x88F0
#endif

#define WINDOW_W 640
#define WINDOW_H 480
#define WINDOW_MIN_W 320
#define WINDOW_MIN_H 240
#define SOFTWARE_DEMO_W 180
#define SOFTWARE_DEMO_H 135

struct vertex {
    GLfloat x;
    GLfloat y;
    GLfloat z;
    GLfloat r;
    GLfloat g;
    GLfloat b;
    GLfloat a;
};

struct tex_vertex {
    GLfloat x;
    GLfloat y;
    GLfloat u;
    GLfloat v;
};

struct app_state {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct xdg_wm_base *wm_base;
    struct wl_surface *surface;
    struct wl_callback *frame_callback;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct wl_egl_window *egl_window;
    EGLDisplay egl_display;
    EGLConfig egl_config;
    EGLContext egl_context;
    EGLSurface egl_surface;
    int egl_surface_width;
    int egl_surface_height;
    GLuint program;
    GLuint tex_program;
    GLuint vbo;
    GLuint fbo;
    GLuint fbo_tex;
    GLuint depth_stencil_rb;
    GLuint sphere_program;
    GLuint sphere_vbo;
    GLint attr_pos;
    GLint attr_color;
    GLint attr_tex_pos;
    GLint attr_tex_coord;
    GLint uniform_tex;
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
    int render_width;
    int render_height;
    int render_divisor;
    int loop;
    int api_smoke;
    int sphere_demo;
    int software_demo;
    int max_width;
    int max_height;
    int throttle_frames;
    int pace_us;
    double next_frame_sec;
    int sphere_vertex_count;
    int fps_frame_count;
    int fps_title_count;
    double fps_value;
    double fps_start_sec;
    int visible_fps_frame_count;
    double visible_fps_value;
    double callback_fps_value;
    double visible_fps_start_sec;
    double draw_total_sec;
    double swap_total_sec;
    double dispatch_total_sec;
    double pace_total_sec;
    double sleep_total_sec;
    char fps_text[GL_FPS_OVERLAY_TEXT_MAX];
};

static void update_render_size(struct app_state *app);
static double monotonic_seconds(void);
static double rtc_seconds(void);
static int using_d3d12_driver(void);
static void update_demo_visible_fps(struct app_state *app);
static int dispatch_wayland_nonblocking(struct app_state *app);

static EGLDisplay get_wayland_display(struct wl_display *display)
{
    typedef EGLDisplay (*get_platform_display_fn)(EGLenum, void *,
                                                  const EGLAttrib *);
    const char *client_ext = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    get_platform_display_fn get_platform_display =
        (get_platform_display_fn)eglGetProcAddress("eglGetPlatformDisplay");

    if (get_platform_display != NULL && client_ext != NULL &&
        (strstr(client_ext, "EGL_KHR_platform_wayland") != NULL ||
         strstr(client_ext, "EGL_EXT_platform_base") != NULL))
        return get_platform_display(EGL_PLATFORM_WAYLAND_KHR, display, NULL);

    return eglGetDisplay((EGLNativeDisplayType)display);
}

static int init_api_smoke_resources(struct app_state *app)
{
    GLenum status;

    glGenBuffers(1, &app->vbo);
    glGenFramebuffers(1, &app->fbo);
    glGenTextures(1, &app->fbo_tex);
    glGenRenderbuffers(1, &app->depth_stencil_rb);
    if (!app->vbo || !app->fbo || !app->fbo_tex ||
        !app->depth_stencil_rb)
        return -1;

    glBindTexture(GL_TEXTURE_2D, app->fbo_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 128, 128, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);

    glBindRenderbuffer(GL_RENDERBUFFER, app->depth_stencil_rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, 128, 128);

    glBindFramebuffer(GL_FRAMEBUFFER, app->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           app->fbo_tex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, app->depth_stencil_rb);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, app->depth_stencil_rb);
    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "mesawlegl: FBO incomplete (0x%x)\n", status);
        return -1;
    }

    return glGetError() == GL_NO_ERROR ? 0 : -1;
}

static int init_sphere_resources(struct app_state *app)
{
    const char *driver = getenv("GALLIUM_DRIVER");
    int frequency = app->software_demo ? 2 :
                    (driver && strcmp(driver, "d3d12") == 0) ? 2 : 4;
    static const char *sphere_vs =
        "attribute vec3 a_pos;\n"
        "attribute vec3 a_normal;\n"
        "uniform mat4 u_mvp;\n"
        "uniform mat4 u_model;\n"
        "varying vec3 v_normal;\n"
        "void main() {\n"
        "  v_normal = (u_model * vec4(a_normal, 0.0)).xyz;\n"
        "  gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
        "}\n";
    static const char *sphere_fs =
        "precision mediump float;\n"
        "varying vec3 v_normal;\n"
        "uniform vec3 u_light;\n"
        "void main() {\n"
        "  vec3 n = v_normal;\n"
        "  vec3 l = u_light * 0.29;\n"
        "  float diffuse = max(dot(n, l), 0.0);\n"
        "  vec3 cool = vec3(0.10, 0.45, 0.95);\n"
        "  vec3 warm = vec3(0.90, 0.62, 0.22);\n"
        "  vec3 base = mix(cool, warm, n.y * 0.5 + 0.5);\n"
        "  vec3 color = base * (0.22 + diffuse * 0.82);\n"
        "  gl_FragColor = vec4(color, 1.0);\n"
        "}\n";
    int count = mesawlegl_poly_sphere_vertex_count(frequency);
    struct sphere_vertex *vertices = calloc((size_t)count, sizeof(*vertices));

    if (!vertices)
        return -1;

    if (mesawlegl_build_poly_sphere(vertices, count, frequency) != count) {
        free(vertices);
        return -1;
    }

    app->sphere_program =
        xv6_gl_link_program("mesawlegl", sphere_vs, sphere_fs);
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

static void render_simple_frame(struct app_state *app)
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

    glViewport(0, 0, app->render_width, app->render_height);
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

static void render_api_frame(struct app_state *app)
{
    float angle = app->frame * 0.07f;
    float s = sinf(angle);
    float c = cosf(angle);
    struct vertex tri[3] = {
        { -0.72f * c, -0.64f * s, 0.35f, 0.95f, 0.20f, 0.18f, 1.0f },
        {  0.68f * s, -0.66f * c, 0.15f, 0.16f, 0.82f, 0.38f, 1.0f },
        { -0.62f * s,  0.70f * c, 0.05f, 0.20f, 0.40f, 1.00f, 1.0f },
    };
    struct tex_vertex quad[4] = {
        { -0.82f, -0.72f, 0.0f, 0.0f },
        {  0.82f, -0.72f, 1.0f, 0.0f },
        { -0.82f,  0.72f, 0.0f, 1.0f },
        {  0.82f,  0.72f, 1.0f, 1.0f },
    };
    int sx = app->render_width / 10;
    int sy = app->render_height / 10;
    int sw = app->render_width - sx * 2;
    int sh = app->render_height - sy * 2;

    glBindFramebuffer(GL_FRAMEBUFFER, app->fbo);
    glViewport(0, 0, 128, 128);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_STENCIL_TEST);
    glDepthFunc(GL_LEQUAL);
    glStencilMask(0xff);
    glStencilFunc(GL_ALWAYS, 1, 0xff);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glClearColor(0.02f, 0.03f, 0.045f, 1.0f);
    glClearDepthf(1.0f);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glUseProgram(app->program);
    glBindBuffer(GL_ARRAY_BUFFER, app->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_DYNAMIC_DRAW);
    glVertexAttribPointer((GLuint)app->attr_pos, 3, GL_FLOAT, GL_FALSE,
                          sizeof(tri[0]), (void *)0);
    glVertexAttribPointer((GLuint)app->attr_color, 4, GL_FLOAT, GL_FALSE,
                          sizeof(tri[0]), (void *)(3 * sizeof(GLfloat)));
    glEnableVertexAttribArray((GLuint)app->attr_pos);
    glEnableVertexAttribArray((GLuint)app->attr_color);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0, 0, app->render_width, app->render_height);
    glClearColor(0.03f, 0.055f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    glScissor(sx, sy, sw, sh);
    glUseProgram(app->tex_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, app->fbo_tex);
    glUniform1i(app->uniform_tex, 0);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_DYNAMIC_DRAW);
    glVertexAttribPointer((GLuint)app->attr_tex_pos, 2, GL_FLOAT, GL_FALSE,
                          sizeof(quad[0]), (void *)0);
    glVertexAttribPointer((GLuint)app->attr_tex_coord, 2, GL_FLOAT, GL_FALSE,
                          sizeof(quad[0]), (void *)(2 * sizeof(GLfloat)));
    glEnableVertexAttribArray((GLuint)app->attr_tex_pos);
    glEnableVertexAttribArray((GLuint)app->attr_tex_coord);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
}

static void render_sphere_frame(struct app_state *app)
{
    float aspect = app->render_height > 0 ?
                   (float)app->render_width / (float)app->render_height :
                   1.0f;
    float projection[16];
    float view[16];
    float rx[16];
    float ry[16];
    float model[16];
    float pv[16];
    float mvp[16];
    float angle = app->frame * (app->software_demo ? 0.16f : 0.022f);
    const GLfloat light[3] = { 1.6f, 1.2f, 2.8f };

    mesawlegl_mat4_perspective(projection, 58.0f * (float)M_PI / 180.0f,
                               aspect, 0.1f, 16.0f);
    mesawlegl_mat4_translate(view, 0.0f, 0.0f, -3.6f);
    mesawlegl_mat4_rotate_y(ry, angle);
    mesawlegl_mat4_rotate_x(rx, 0.35f * sinf(angle * 0.43f));
    mesawlegl_mat4_mul(model, ry, rx);
    mesawlegl_mat4_mul(pv, projection, view);
    mesawlegl_mat4_mul(mvp, pv, model);

    glViewport(0, 0, app->render_width, app->render_height);
    glClearColor(0.015f, 0.022f, 0.032f, 1.0f);
    if (!app->software_demo)
        glClearDepthf(1.0f);
    glClear(app->software_demo ? GL_COLOR_BUFFER_BIT :
                                 (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);
    if (app->software_demo) {
        glDisable(GL_DEPTH_TEST);
    } else {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
    }
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

static void render_fps_overlay(struct app_state *app)
{
    if (!app->sphere_demo || app->fps_text[0] == '\0')
        return;

    gl_fps_overlay_draw(app->fps_text, app->program,
                        app->attr_pos, app->attr_color);
}

static int create_window_surface(struct app_state *app)
{
    app->egl_surface = eglCreateWindowSurface(
        app->egl_display, app->egl_config,
        (EGLNativeWindowType)app->egl_window, NULL);
    if (app->egl_surface == EGL_NO_SURFACE ||
        !eglMakeCurrent(app->egl_display, app->egl_surface, app->egl_surface,
                        app->egl_context)) {
        fprintf(stderr, "mesawlegl[%d]: window surface failed (0x%x)\n",
                app->loop, eglGetError());
        return -1;
    }
    app->egl_surface_width = app->render_width;
    app->egl_surface_height = app->render_height;
    return 0;
}

static int recreate_window_surface(struct app_state *app)
{
    if (app->egl_display == EGL_NO_DISPLAY ||
        app->egl_context == EGL_NO_CONTEXT || !app->egl_window)
        return -1;

    if (app->egl_surface == EGL_NO_SURFACE)
        return create_window_surface(app);

    update_render_size(app);
    if (app->egl_surface_width != app->render_width ||
        app->egl_surface_height != app->render_height) {
        wl_egl_window_resize(app->egl_window, app->render_width,
                             app->render_height, 0, 0);
        app->egl_surface_width = app->render_width;
        app->egl_surface_height = app->render_height;
    }
    return 0;
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
    static const char *tex_vs =
        "attribute vec2 a_pos;\n"
        "attribute vec2 a_uv;\n"
        "varying vec2 v_uv;\n"
        "void main() { v_uv = a_uv; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
    static const char *tex_fs =
        "precision mediump float;\n"
        "uniform sampler2D u_tex;\n"
        "varying vec2 v_uv;\n"
        "void main() { gl_FragColor = texture2D(u_tex, v_uv) * vec4(1.0, 1.0, 1.0, 0.86); }\n";
    EGLint config_attrs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLint context_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLint major = 0;
    EGLint minor = 0;
    EGLint nconfigs = 0;

    app->egl_display = get_wayland_display(app->display);
    if (app->egl_display == EGL_NO_DISPLAY ||
        !eglInitialize(app->egl_display, &major, &minor)) {
        fprintf(stderr, "mesawlegl: eglInitialize failed (0x%x)\n",
                eglGetError());
        return -1;
    }
    if (!eglChooseConfig(app->egl_display, config_attrs, &app->egl_config, 1,
                         &nconfigs) || nconfigs < 1) {
        fprintf(stderr, "mesawlegl: window config unavailable (0x%x)\n",
                eglGetError());
        return -1;
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        fprintf(stderr, "mesawlegl: eglBindAPI failed (0x%x)\n",
                eglGetError());
        return -1;
    }

    update_render_size(app);
    app->egl_window = wl_egl_window_create(app->surface, app->render_width,
                                           app->render_height);
    if (!app->egl_window) {
        fprintf(stderr, "mesawlegl: wl_egl_window_create failed\n");
        return -1;
    }
    app->egl_context = eglCreateContext(app->egl_display, app->egl_config,
                                        EGL_NO_CONTEXT, context_attrs);
    if (app->egl_context == EGL_NO_CONTEXT || create_window_surface(app) < 0) {
        fprintf(stderr, "mesawlegl: EGL context setup failed (0x%x)\n",
                eglGetError());
        return -1;
    }
    if (app->sphere_demo && !app->software_demo) {
        if (!eglSwapInterval(app->egl_display, 0))
            fprintf(stderr,
                    "mesawlegl: warning: eglSwapInterval(0) failed (0x%x)\n",
                    eglGetError());
    }

    app->program = xv6_gl_link_program("mesawlegl", vs, fs);
    app->tex_program = xv6_gl_link_program("mesawlegl", tex_vs, tex_fs);
    if (!app->program || !app->tex_program)
        return -1;
    app->attr_pos = glGetAttribLocation(app->program, "a_pos");
    app->attr_color = glGetAttribLocation(app->program, "a_color");
    app->attr_tex_pos = glGetAttribLocation(app->tex_program, "a_pos");
    app->attr_tex_coord = glGetAttribLocation(app->tex_program, "a_uv");
    app->uniform_tex = glGetUniformLocation(app->tex_program, "u_tex");
    if (app->attr_pos < 0 || app->attr_color < 0 ||
        app->attr_tex_pos < 0 || app->attr_tex_coord < 0 ||
        app->uniform_tex < 0)
        return -1;

    if (app->api_smoke && init_api_smoke_resources(app) < 0) {
        fprintf(stderr, "mesawlegl: API smoke resource setup failed\n");
        return -1;
    }
    if (app->sphere_demo && init_sphere_resources(app) < 0) {
        fprintf(stderr, "mesawlegl: sphere demo resource setup failed\n");
        return -1;
    }

    fprintf(stderr, "mesawlegl: EGL %d.%d GL %s renderer=%s native-wayland%s%s\n",
            major, minor, glGetString(GL_VERSION), glGetString(GL_RENDERER),
            app->api_smoke ? " api-smoke" : "",
            app->sphere_demo ? " spherical-poly-demo" : "");
    if (app->render_divisor > 1)
        fprintf(stderr,
                "mesawlegl: render-scale window=%dx%d render=%dx%d divisor=%d\n",
                app->width, app->height, app->render_width,
                app->render_height, app->render_divisor);
    return 0;
}

static void sleep_frame_if_needed(const struct app_state *app)
{
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 16000000L };

    if (!app->throttle_frames)
        return;
    nanosleep(&ts, NULL);
}

static void pace_frame_if_needed(struct app_state *app)
{
    double now;
    double remaining;
    struct timespec ts;

    if (app->pace_us <= 0)
        return;

    now = monotonic_seconds();
    if (app->next_frame_sec <= 0.0) {
        app->next_frame_sec = now + (double)app->pace_us / 1000000.0;
        return;
    }

    remaining = app->next_frame_sec - now;
    if (remaining > 0.0) {
        ts.tv_sec = (time_t)remaining;
        ts.tv_nsec = (long)((remaining - (double)ts.tv_sec) * 1000000000.0);
        if (ts.tv_nsec < 0)
            ts.tv_nsec = 0;
        nanosleep(&ts, NULL);
        now = monotonic_seconds();
    }

    app->next_frame_sec += (double)app->pace_us / 1000000.0;
    if (app->next_frame_sec < now)
        app->next_frame_sec = now + (double)app->pace_us / 1000000.0;
}

static double monotonic_seconds(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0.0;
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static double rtc_seconds(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_REALTIME, &ts) == 0)
        return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
    return monotonic_seconds();
}

static int env_enabled(const char *name)
{
    const char *value = getenv(name);

    return value && value[0] && strcmp(value, "0") != 0;
}

static int env_is_zero(const char *name)
{
    const char *value = getenv(name);

    return value && strcmp(value, "0") == 0;
}

static int env_int_value(const char *name, int fallback)
{
    const char *value = getenv(name);
    char *end = NULL;
    long parsed;

    if (!value || !*value)
        return fallback;
    parsed = strtol(value, &end, 10);
    if (end == value)
        return fallback;
    if (parsed < 1)
        parsed = 1;
    if (parsed > 32)
        parsed = 32;
    return (int)parsed;
}

static int env_int_range(const char *name, int fallback, int min_value,
                         int max_value)
{
    const char *value = getenv(name);
    char *end = NULL;
    long parsed;

    if (!value || !*value)
        return fallback;
    parsed = strtol(value, &end, 10);
    if (end == value)
        return fallback;
    if (parsed < min_value)
        parsed = min_value;
    if (parsed > max_value)
        parsed = max_value;
    return (int)parsed;
}

static int using_d3d12_driver(void)
{
    const char *driver = getenv("GALLIUM_DRIVER");

    return driver && strcmp(driver, "d3d12") == 0;
}

static int read_wlcomp_fps(double *fps_out)
{
    FILE *fp;
    char line[192];
    double fps;

    if (fps_out)
        *fps_out = 0.0;
    fp = fopen("/tmp/wlcomp-fps", "r");
    if (!fp)
        return 0;
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    if (sscanf(line, "fps=%lf", &fps) != 1)
        return 0;
    if (fps < 0.0)
        fps = 0.0;
    if (fps_out)
        *fps_out = fps;
    return 1;
}

static int dispatch_wayland_nonblocking(struct app_state *app)
{
    struct pollfd pfd;
    int ret;

    if (!app->display)
        return -1;
    while ((ret = wl_display_dispatch_pending(app->display)) > 0)
        ;
    if (ret < 0)
        return -1;
    wl_display_flush(app->display);

    pfd.fd = wl_display_get_fd(app->display);
    pfd.events = POLLIN;
    pfd.revents = 0;
    ret = poll(&pfd, 1, 0);
    if (ret < 0)
        return -1;
    if (ret > 0 && (pfd.revents & (POLLIN | POLLERR | POLLHUP)) != 0)
        return wl_display_dispatch(app->display);
    return 0;
}

static void clamp_demo_size(struct app_state *app)
{
    if (!app->sphere_demo || app->max_width <= 0 || app->max_height <= 0)
        return;
    if (app->width > app->max_width)
        app->width = app->max_width;
    if (app->height > app->max_height)
        app->height = app->max_height;
}

static void update_render_size(struct app_state *app)
{
    int divisor = app->render_divisor > 0 ? app->render_divisor : 1;
    int min_width = using_d3d12_driver() ? 80 : 160;
    int min_height = using_d3d12_driver() ? 48 : 90;

    app->render_width = app->width / divisor;
    app->render_height = app->height / divisor;
    if (app->render_width < min_width)
        app->render_width = min_width;
    if (app->render_height < min_height)
        app->render_height = min_height;
    if (app->render_width > app->width)
        app->render_width = app->width;
    if (app->render_height > app->height)
        app->render_height = app->height;
}

static void update_demo_client_fps(struct app_state *app)
{
    static int stderr_fps_counter;
    double now;
    double elapsed;
    double fps;
    double compositor_fps;
    double display_fps;
    char title[96];

    if (!app->sphere_demo || !app->toplevel)
        return;

    app->fps_frame_count++;
    if (app->fps_frame_count < 30)
        return;

    now = rtc_seconds();
    if (app->fps_start_sec <= 0.0) {
        app->fps_start_sec = now;
        return;
    }
    elapsed = now - app->fps_start_sec;
    if (elapsed < 1.0)
        return;

    fps = elapsed > 0.0 ? (double)app->fps_frame_count / elapsed : 0.0;
    app->fps_value = fps;
    if (read_wlcomp_fps(&compositor_fps)) {
        display_fps = compositor_fps;
    } else {
        compositor_fps = 0.0;
        display_fps = app->callback_fps_value;
    }
    if (display_fps > 0.0) {
        app->visible_fps_value = display_fps;
        snprintf(app->fps_text, sizeof(app->fps_text), "VIS %.1f FPS",
                 display_fps);
        app->fps_title_count++;
        if ((app->fps_title_count & 3) == 1) {
            snprintf(title, sizeof(title), "Mesa 3D Demo - VIS %.1f FPS",
                     display_fps);
            xdg_toplevel_set_title(app->toplevel, title);
            wl_display_flush(app->display);
        }
    }
    if ((stderr_fps_counter++ & 3) == 0)
        fprintf(stderr,
                "mesawlegl[%d]: client_rtc_fps=%.1f compositor_rtc_fps=%.1f callback_rtc_fps=%.1f displayed_fps=%.1f\n",
                app->loop, fps, compositor_fps, app->callback_fps_value,
                app->visible_fps_value);
    app->fps_frame_count = 0;
    app->fps_start_sec = now;
}

static void update_demo_visible_fps(struct app_state *app)
{
    static int stderr_fps_counter;
    double now;
    double elapsed;
    double fps;

    if (!app->sphere_demo || !app->toplevel)
        return;

    app->visible_fps_frame_count++;
    now = rtc_seconds();
    if (app->visible_fps_start_sec <= 0.0) {
        app->visible_fps_start_sec = now;
        return;
    }
    elapsed = now - app->visible_fps_start_sec;
    if (elapsed < 1.0)
        return;

    fps = elapsed > 0.0 ?
          (double)app->visible_fps_frame_count / elapsed : 0.0;
    app->callback_fps_value = fps;
    if ((stderr_fps_counter++ & 3) == 0)
        fprintf(stderr,
                "mesawlegl[%d]: callback_rtc_fps=%.1f client_rtc_fps=%.1f displayed_fps=%.1f\n",
                app->loop, fps, app->fps_value, app->visible_fps_value);
    app->visible_fps_frame_count = 0;
    app->visible_fps_start_sec = now;
}

static void frame_done(void *data, struct wl_callback *cb, uint32_t time)
{
    struct app_state *app = data;
    (void)time;

    if (cb)
        wl_callback_destroy(cb);
    if (app->frame_callback == cb)
        app->frame_callback = NULL;
    update_demo_visible_fps(app);
}

static const struct wl_callback_listener frame_listener = {
    .done = frame_done,
};

static void request_frame_callback(struct app_state *app)
{
    if (!app->sphere_demo || app->frame_callback || !app->surface)
        return;

    app->frame_callback = wl_surface_frame(app->surface);
    if (app->frame_callback)
        wl_callback_add_listener(app->frame_callback, &frame_listener, app);
}

static int draw_and_swap(struct app_state *app)
{
    static int gl_check_count;
    double t0 = monotonic_seconds();
    double t1;
    double t2;

    if (app->sphere_demo)
        render_sphere_frame(app);
    else if (app->api_smoke)
        render_api_frame(app);
    else
        render_simple_frame(app);
    render_fps_overlay(app);
    if (!app->sphere_demo || app->software_demo || !using_d3d12_driver()) {
        gl_check_count++;
        if (gl_check_count >= 120 && glGetError() != GL_NO_ERROR) {
            fprintf(stderr, "mesawlegl[%d]: GL error during frame\n", app->loop);
            return -1;
        }
        if (gl_check_count >= 120)
            gl_check_count = 0;
    }
    t1 = monotonic_seconds();
    request_frame_callback(app);
    if (!eglSwapBuffers(app->egl_display, app->egl_surface)) {
        fprintf(stderr, "mesawlegl[%d]: eglSwapBuffers failed (0x%x)\n",
                app->loop, eglGetError());
        return -1;
    }
    t2 = monotonic_seconds();
    if (t1 >= t0)
        app->draw_total_sec += t1 - t0;
    if (t2 >= t1)
        app->swap_total_sec += t2 - t1;
    update_demo_client_fps(app);
    return 0;
}

static void xdg_surface_configure(void *data, struct xdg_surface *surface,
                                  uint32_t serial)
{
    struct app_state *app = data;

    xdg_surface_ack_configure(surface, serial);
    if (!app->configured)
        app->configured = 1;
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

    if (width > 0 && height > 0) {
        app->width = width;
        app->height = height;
        clamp_demo_size(app);
        update_render_size(app);
        if (app->xdg_surface && app->render_divisor > 1)
            xdg_surface_set_window_geometry(app->xdg_surface, 0, 0,
                                            app->width, app->height);
        if (app->egl_window)
            wl_egl_window_resize(app->egl_window, app->render_width,
                                 app->render_height, 0, 0);
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
        fprintf(stderr, "mesawlegl: wl_display_connect failed\n");
        return -1;
    }
    app->registry = wl_display_get_registry(app->display);
    wl_registry_add_listener(app->registry, &registry_listener, app);
    wl_display_roundtrip(app->display);
    wl_display_roundtrip(app->display);

    if (!app->compositor || !app->wm_base) {
        fprintf(stderr, "mesawlegl: compositor/xdg globals unavailable\n");
        return -1;
    }

    app->surface = wl_compositor_create_surface(app->compositor);
    app->xdg_surface = xdg_wm_base_get_xdg_surface(app->wm_base, app->surface);
    xdg_surface_add_listener(app->xdg_surface, &xdg_surface_listener, app);
    app->toplevel = xdg_surface_get_toplevel(app->xdg_surface);
    xdg_toplevel_add_listener(app->toplevel, &toplevel_listener, app);
    xdg_toplevel_set_title(app->toplevel,
                           app->sphere_demo ? "Mesa 3D Demo - starting" :
                                              "Mesa Native Wayland EGL");
    xdg_toplevel_set_app_id(app->toplevel, "mesawlegl");
    if (app->render_divisor > 1)
        xdg_surface_set_window_geometry(app->xdg_surface, 0, 0,
                                        app->width, app->height);
    if (app->sphere_demo && !app->software_demo)
        xdg_toplevel_set_min_size(app->toplevel, WINDOW_MIN_W, WINDOW_MIN_H);
    if (app->sphere_demo && app->max_width > 0 && app->max_height > 0) {
        xdg_toplevel_set_min_size(app->toplevel, app->max_width,
                                  app->max_height);
        xdg_toplevel_set_max_size(app->toplevel, app->max_width,
                                  app->max_height);
    }

    if (init_mesa(app) < 0)
        return -1;
    wl_surface_commit(app->surface);
    return 0;
}

static void cleanup(struct app_state *app)
{
    if (app->display)
        wl_display_roundtrip(app->display);
    if (app->frame_callback) {
        wl_callback_destroy(app->frame_callback);
        app->frame_callback = NULL;
    }
    if (app->egl_display != EGL_NO_DISPLAY) {
        if (app->depth_stencil_rb)
            glDeleteRenderbuffers(1, &app->depth_stencil_rb);
        if (app->fbo_tex)
            glDeleteTextures(1, &app->fbo_tex);
        if (app->fbo)
            glDeleteFramebuffers(1, &app->fbo);
        if (app->vbo)
            glDeleteBuffers(1, &app->vbo);
        if (app->sphere_vbo)
            glDeleteBuffers(1, &app->sphere_vbo);
        if (app->sphere_program)
            glDeleteProgram(app->sphere_program);
        if (app->tex_program)
            glDeleteProgram(app->tex_program);
        if (app->program)
            glDeleteProgram(app->program);
        if (app->display && app->surface) {
            wl_surface_attach(app->surface, NULL, 0, 0);
            wl_surface_commit(app->surface);
            wl_display_roundtrip(app->display);
        }
        eglMakeCurrent(app->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
        if (app->sphere_demo && !app->software_demo) {
            app->egl_surface = EGL_NO_SURFACE;
            app->egl_context = EGL_NO_CONTEXT;
        } else {
            if (app->egl_surface != EGL_NO_SURFACE)
                eglDestroySurface(app->egl_display, app->egl_surface);
            if (app->egl_context != EGL_NO_CONTEXT)
                eglDestroyContext(app->egl_display, app->egl_context);
            eglTerminate(app->egl_display);
        }
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

static int parse_size_arg(const char *arg, int *width, int *height)
{
    int w;
    int h;

    if (sscanf(arg, "--size=%dx%d", &w, &h) != 2)
        return -1;
    if (w < WINDOW_MIN_W || h < WINDOW_MIN_H)
        return -1;
    *width = w;
    *height = h;
    return 0;
}

static int run_client(int loop, int frames, int resize_every, int api_smoke,
                      int sphere_demo, int software_demo, int initial_width,
                      int initial_height)
{
    struct app_state app;
    int rc = 0;
    double start_sec;
    double elapsed_sec;

    memset(&app, 0, sizeof(app));
    app.egl_display = EGL_NO_DISPLAY;
    app.egl_context = EGL_NO_CONTEXT;
    app.egl_surface = EGL_NO_SURFACE;
    app.running = 1;
    app.max_frames = frames;
    app.resize_every = resize_every;
    app.width = initial_width;
    app.height = initial_height;
    app.loop = loop;
    app.api_smoke = api_smoke;
    app.sphere_demo = sphere_demo;
    app.software_demo = software_demo;
    app.throttle_frames = !sphere_demo || software_demo;
    app.render_divisor = 1;
    if (sphere_demo && !software_demo && using_d3d12_driver())
        app.render_divisor =
            env_int_value("XV6_MESAWLEGL_RENDER_DIV", 1);
    if (sphere_demo && !software_demo && using_d3d12_driver())
        app.pace_us = env_int_range("XV6_MESAWLEGL_PACE_US", 0,
                                    0, 50000);
    if (sphere_demo && software_demo) {
        app.width = SOFTWARE_DEMO_W;
        app.height = SOFTWARE_DEMO_H;
        app.max_width = SOFTWARE_DEMO_W;
        app.max_height = SOFTWARE_DEMO_H;
    }
    app.fps_start_sec = 0.0;
    app.fps_frame_count = 0;
    app.fps_value = 0.0;
    app.visible_fps_start_sec = 0.0;
    app.visible_fps_frame_count = 0;
    app.visible_fps_value = 0.0;
    app.callback_fps_value = 0.0;
    snprintf(app.fps_text, sizeof(app.fps_text), "VIS --.- FPS");

    if (init_wayland(&app) < 0) {
        cleanup(&app);
        return 1;
    }

    while (app.running && !app.configured &&
           wl_display_dispatch(app.display) >= 0)
        ;
    if (app.configured && recreate_window_surface(&app) < 0)
        rc = 1;
    start_sec = rtc_seconds();
    for (app.frame = 0; rc == 0 && app.running &&
         (app.max_frames <= 0 || app.frame < app.max_frames);
         app.frame++) {
        if (app.resize_every > 0 && app.frame > 0 &&
            app.frame % app.resize_every == 0) {
            if (app.width == initial_width && app.height == initial_height) {
                app.width = 360;
                app.height = 260;
            } else {
                app.width = initial_width;
                app.height = initial_height;
            }
            if (recreate_window_surface(&app) < 0) {
                rc = 1;
                break;
            }
        }
        if (draw_and_swap(&app) < 0) {
            rc = 1;
            break;
        }
        {
            double t0 = monotonic_seconds();
            double t1;

            pace_frame_if_needed(&app);
            t1 = monotonic_seconds();
            if (t1 >= t0)
                app.pace_total_sec += t1 - t0;
        }
        if (app.sphere_demo || (app.frame & 3) == 0) {
            double t0 = monotonic_seconds();
            double t1;

            if (dispatch_wayland_nonblocking(&app) < 0) {
                app.running = 0;
                rc = 1;
            }
            t1 = monotonic_seconds();
            if (t1 >= t0)
                app.dispatch_total_sec += t1 - t0;
        }
        {
            double t0 = monotonic_seconds();
            double t1;

            sleep_frame_if_needed(&app);
            t1 = monotonic_seconds();
            if (t1 >= t0)
                app.sleep_total_sec += t1 - t0;
        }
    }
    elapsed_sec = rtc_seconds() - start_sec;
    if (!app.configured)
        rc = 1;
    cleanup(&app);
    fprintf(stderr,
            "mesawlegl[%d]: complete frames=%d status=%d rtc_elapsed=%.3fs rtc_fps=%.1f\n",
            loop, app.frame, rc, elapsed_sec,
            elapsed_sec > 0.0 ? (double)app.frame / elapsed_sec : 0.0);
    if (app.frame > 0) {
        double frames = (double)app.frame;

        fprintf(stderr,
                "mesawlegl[%d]: timing avg_ms draw=%.3f swap=%.3f dispatch=%.3f pace=%.3f sleep=%.3f other=%.3f\n",
                loop,
                app.draw_total_sec * 1000.0 / frames,
                app.swap_total_sec * 1000.0 / frames,
                app.dispatch_total_sec * 1000.0 / frames,
                app.pace_total_sec * 1000.0 / frames,
                app.sleep_total_sec * 1000.0 / frames,
                (elapsed_sec - app.draw_total_sec - app.swap_total_sec -
                 app.dispatch_total_sec - app.pace_total_sec -
                 app.sleep_total_sec) * 1000.0 / frames);
    }
    return rc;
}

int main(int argc, char **argv)
{
    int frames = 120;
    int loops = 1;
    int resize_every = 0;
    int api_smoke = 1;
    int sphere_demo = 0;
    int initial_width = WINDOW_W;
    int initial_height = WINDOW_H;
    int software_demo;
    int accel_requested;
    int rc = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--d3d12") == 0) {
            setenv("XDG_RUNTIME_DIR", "/tmp", 0);
            setenv("WAYLAND_DISPLAY", "wayland-0", 0);
            setenv("LIBGL_ALWAYS_SOFTWARE", "0", 1);
            setenv("GALLIUM_DRIVER", "d3d12", 1);
            setenv("LIBGL_DRIVERS_PATH", "/lib/dri", 0);
            setenv("EGL_PLATFORM", "wayland", 0);
            setenv("XV6_MESA_WAYLAND_THROTTLE", "0", 0);
            setenv("XV6_MESA_WAYLAND_XV6GPU", "1", 0);
            setenv("XV6_MESA_WAYLAND_INPLACE_PRESENT", "1", 0);
            setenv("XV6_D3D12_PRESENT_INTERVAL", "1", 0);
            setenv("vblank_mode", "0", 0);
        } else if (strcmp(argv[i], "--perf-log") == 0) {
            setenv("XV6_MESA_PERF_LOG", "1", 1);
        } else if (strcmp(argv[i], "--no-inplace") == 0) {
            setenv("XV6_MESA_WAYLAND_INPLACE_PRESENT", "0", 1);
        } else if (strcmp(argv[i], "--direct-backbuffer") == 0) {
            setenv("XV6_D3D12_DIRECT_BACKBUFFER", "1", 1);
        } else if (strcmp(argv[i], "--prefence") == 0) {
            setenv("XV6_D3D12_SWRAST_NO_PREFENCE", "0", 1);
        } else if (strcmp(argv[i], "--no-front-flush") == 0) {
            setenv("XV6_D3D12_SWRAST_NO_FRONT_FLUSH", "1", 1);
        } else if (strcmp(argv[i], "--sync-frontbuffer") == 0) {
            setenv("XV6_D3D12_ASYNC_FRONTBUFFER", "0", 1);
        } else if (strncmp(argv[i], "--render-div=", 13) == 0) {
            setenv("XV6_MESAWLEGL_RENDER_DIV", argv[i] + 13, 1);
        } else if (strncmp(argv[i], "--present-interval=", 19) == 0) {
            setenv("XV6_D3D12_PRESENT_INTERVAL", argv[i] + 19, 1);
        } else if (strncmp(argv[i], "--pace-us=", 10) == 0) {
            setenv("XV6_MESAWLEGL_PACE_US", argv[i] + 10, 1);
        }
    }

    accel_requested = env_is_zero("LIBGL_ALWAYS_SOFTWARE") ||
        getenv("GALLIUM_DRIVER") != NULL;
    if (!accel_requested) {
        setenv("LIBGL_ALWAYS_SOFTWARE", "1", 0);
        setenv("MESA_LOADER_DRIVER_OVERRIDE", "softpipe", 0);
    }
    software_demo = env_enabled("LIBGL_ALWAYS_SOFTWARE") ||
        getenv("MESA_LOADER_DRIVER_OVERRIDE") != NULL;

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--frames=", 9) == 0) {
            frames = parse_nonnegative_arg(argv[i], "--frames=", frames);
        } else if (strncmp(argv[i], "--loops=", 8) == 0) {
            loops = parse_positive_arg(argv[i], "--loops=", loops);
        } else if (strncmp(argv[i], "--resize-every=", 15) == 0) {
            resize_every = parse_positive_arg(argv[i], "--resize-every=",
                                              resize_every);
        } else if (strcmp(argv[i], "--simple") == 0) {
            api_smoke = 0;
            sphere_demo = 0;
        } else if (strcmp(argv[i], "--api-smoke") == 0) {
            api_smoke = 1;
            sphere_demo = 0;
        } else if (strcmp(argv[i], "--demo") == 0) {
            frames = 0;
            resize_every = 0;
            api_smoke = 0;
            sphere_demo = 1;
        } else if (strncmp(argv[i], "--size=", 7) == 0) {
            if (parse_size_arg(argv[i], &initial_width, &initial_height) != 0) {
                fprintf(stderr,
                        "mesawlegl: invalid size '%s' (minimum %dx%d)\n",
                        argv[i], WINDOW_MIN_W, WINDOW_MIN_H);
                return 2;
            }
        } else if (strcmp(argv[i], "--d3d12") == 0 ||
                   strcmp(argv[i], "--perf-log") == 0 ||
                   strcmp(argv[i], "--no-inplace") == 0 ||
                   strcmp(argv[i], "--direct-backbuffer") == 0 ||
                   strcmp(argv[i], "--prefence") == 0 ||
                   strcmp(argv[i], "--no-front-flush") == 0 ||
                   strcmp(argv[i], "--sync-frontbuffer") == 0 ||
                   strncmp(argv[i], "--render-div=", 13) == 0 ||
                   strncmp(argv[i], "--present-interval=", 19) == 0 ||
                   strncmp(argv[i], "--pace-us=", 10) == 0) {
            continue;
        } else if (strcmp(argv[i], "--help") == 0) {
            fprintf(stderr,
                    "usage: %s [--frames=N] [--loops=N] [--resize-every=N] [--size=WxH] [--api-smoke|--simple|--demo] [--d3d12] [--perf-log] [--no-inplace] [--direct-backbuffer] [--prefence] [--no-front-flush] [--sync-frontbuffer] [--render-div=N] [--present-interval=N] [--pace-us=N]\n",
                    argv[0]);
            return 0;
        } else {
            fprintf(stderr, "mesawlegl: unknown option '%s'\n", argv[i]);
            return 2;
        }
    }

    for (int loop = 1; loop <= loops; loop++) {
        rc = run_client(loop, frames, resize_every, api_smoke, sphere_demo,
                        software_demo, initial_width, initial_height);
        if (rc != 0)
            break;
    }
    return rc;
}
