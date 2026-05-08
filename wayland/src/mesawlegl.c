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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-egl.h>

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

#define WINDOW_W 480
#define WINDOW_H 360

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

struct sphere_vertex {
    GLfloat x;
    GLfloat y;
    GLfloat z;
    GLfloat nx;
    GLfloat ny;
    GLfloat nz;
};

struct app_state {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct xdg_wm_base *wm_base;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct wl_egl_window *egl_window;
    EGLDisplay egl_display;
    EGLConfig egl_config;
    EGLContext egl_context;
    EGLSurface egl_surface;
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
    int loop;
    int api_smoke;
    int sphere_demo;
    int sphere_vertex_count;
};

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

static GLuint compile_shader(GLenum type, const char *src)
{
    GLuint shader = glCreateShader(type);
    GLint ok = GL_FALSE;

    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
        fprintf(stderr, "mesawlegl: shader compile failed\n");
    return shader;
}

static GLuint link_program(const char *vs, const char *fs)
{
    GLuint vshader = compile_shader(GL_VERTEX_SHADER, vs);
    GLuint fshader = compile_shader(GL_FRAGMENT_SHADER, fs);
    GLuint program = glCreateProgram();
    GLint ok = GL_FALSE;

    glAttachShader(program, vshader);
    glAttachShader(program, fshader);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    glDeleteShader(vshader);
    glDeleteShader(fshader);
    if (!ok) {
        fprintf(stderr, "mesawlegl: program link failed\n");
        glDeleteProgram(program);
        return 0;
    }
    return program;
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

static void mat4_identity(float m[16])
{
    memset(m, 0, sizeof(float) * 16);
    m[0] = 1.0f;
    m[5] = 1.0f;
    m[10] = 1.0f;
    m[15] = 1.0f;
}

static void mat4_mul(float out[16], const float a[16], const float b[16])
{
    float r[16];

    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            r[col * 4 + row] =
                a[0 * 4 + row] * b[col * 4 + 0] +
                a[1 * 4 + row] * b[col * 4 + 1] +
                a[2 * 4 + row] * b[col * 4 + 2] +
                a[3 * 4 + row] * b[col * 4 + 3];
        }
    }
    memcpy(out, r, sizeof(r));
}

static void mat4_perspective(float m[16], float fovy, float aspect,
                             float znear, float zfar)
{
    float f = 1.0f / tanf(fovy * 0.5f);

    memset(m, 0, sizeof(float) * 16);
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (zfar + znear) / (znear - zfar);
    m[11] = -1.0f;
    m[14] = (2.0f * zfar * znear) / (znear - zfar);
}

static void mat4_translate(float m[16], float x, float y, float z)
{
    mat4_identity(m);
    m[12] = x;
    m[13] = y;
    m[14] = z;
}

static void mat4_rotate_x(float m[16], float angle)
{
    float s = sinf(angle);
    float c = cosf(angle);

    mat4_identity(m);
    m[5] = c;
    m[6] = s;
    m[9] = -s;
    m[10] = c;
}

static void mat4_rotate_y(float m[16], float angle)
{
    float s = sinf(angle);
    float c = cosf(angle);

    mat4_identity(m);
    m[0] = c;
    m[2] = -s;
    m[8] = s;
    m[10] = c;
}

static struct sphere_vertex make_poly_vertex(float x, float y, float z)
{
    float inv_len = 1.0f / sqrtf(x * x + y * y + z * z);
    struct sphere_vertex v;

    v.x = x * inv_len;
    v.y = y * inv_len;
    v.z = z * inv_len;
    v.nx = 0.0f;
    v.ny = 0.0f;
    v.nz = 1.0f;
    return v;
}

static void sphere_emit_flat(struct sphere_vertex *dst, int *idx,
                             struct sphere_vertex a, struct sphere_vertex b,
                             struct sphere_vertex c)
{
    struct sphere_vertex out[3] = { a, b, c };
    float ux = b.x - a.x;
    float uy = b.y - a.y;
    float uz = b.z - a.z;
    float vx = c.x - a.x;
    float vy = c.y - a.y;
    float vz = c.z - a.z;
    float nx = uy * vz - uz * vy;
    float ny = uz * vx - ux * vz;
    float nz = ux * vy - uy * vx;
    float inv_len = 1.0f / sqrtf(nx * nx + ny * ny + nz * nz);
    float cx = (a.x + b.x + c.x) / 3.0f;
    float cy = (a.y + b.y + c.y) / 3.0f;
    float cz = (a.z + b.z + c.z) / 3.0f;

    nx *= inv_len;
    ny *= inv_len;
    nz *= inv_len;
    if (nx * cx + ny * cy + nz * cz < 0.0f) {
        struct sphere_vertex tmp = out[1];

        out[1] = out[2];
        out[2] = tmp;
        nx = -nx;
        ny = -ny;
        nz = -nz;
    }
    for (int i = 0; i < 3; i++) {
        out[i].nx = nx;
        out[i].ny = ny;
        out[i].nz = nz;
        dst[(*idx)++] = out[i];
    }
}

static struct sphere_vertex barycentric_sphere_point(struct sphere_vertex a,
                                                     struct sphere_vertex b,
                                                     struct sphere_vertex c,
                                                     int ia, int ib, int ic,
                                                     int frequency)
{
    float fa = (float)ia / (float)frequency;
    float fb = (float)ib / (float)frequency;
    float fc = (float)ic / (float)frequency;

    return make_poly_vertex(a.x * fa + b.x * fb + c.x * fc,
                            a.y * fa + b.y * fb + c.y * fc,
                            a.z * fa + b.z * fb + c.z * fc);
}

static int init_sphere_resources(struct app_state *app)
{
    static const int frequency = 4;
    static const int base_faces = 20;
    static const int verts_per_face = frequency * frequency * 3;
    static const float phi = 1.61803398875f;
    static const struct {
        int a;
        int b;
        int c;
    } faces[20] = {
        { 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 },
        { 0, 10, 11 }, { 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 },
        { 10, 7, 6 }, { 7, 1, 8 }, { 3, 9, 4 }, { 3, 4, 2 },
        { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 }, { 4, 9, 5 },
        { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 },
    };
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
    struct sphere_vertex base[12] = {
        { -1.0f,  phi, 0.0f, 0.0f, 0.0f, 1.0f },
        {  1.0f,  phi, 0.0f, 0.0f, 0.0f, 1.0f },
        { -1.0f, -phi, 0.0f, 0.0f, 0.0f, 1.0f },
        {  1.0f, -phi, 0.0f, 0.0f, 0.0f, 1.0f },
        { 0.0f, -1.0f,  phi, 0.0f, 0.0f, 1.0f },
        { 0.0f,  1.0f,  phi, 0.0f, 0.0f, 1.0f },
        { 0.0f, -1.0f, -phi, 0.0f, 0.0f, 1.0f },
        { 0.0f,  1.0f, -phi, 0.0f, 0.0f, 1.0f },
        {  phi, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f },
        {  phi, 0.0f,  1.0f, 0.0f, 0.0f, 1.0f },
        { -phi, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f },
        { -phi, 0.0f,  1.0f, 0.0f, 0.0f, 1.0f },
    };
    int count = base_faces * verts_per_face;
    struct sphere_vertex *vertices = calloc((size_t)count, sizeof(*vertices));
    int idx = 0;

    if (!vertices)
        return -1;

    for (int i = 0; i < 12; i++)
        base[i] = make_poly_vertex(base[i].x, base[i].y, base[i].z);

    for (int face = 0; face < base_faces; face++) {
        struct sphere_vertex a = base[faces[face].a];
        struct sphere_vertex b = base[faces[face].b];
        struct sphere_vertex c = base[faces[face].c];

        for (int row = 0; row < frequency; row++) {
            for (int col = 0; col < frequency - row; col++) {
                struct sphere_vertex p0 =
                    barycentric_sphere_point(a, b, c,
                                             frequency - row - col,
                                             col, row, frequency);
                struct sphere_vertex p1 =
                    barycentric_sphere_point(a, b, c,
                                             frequency - row - col - 1,
                                             col + 1, row, frequency);
                struct sphere_vertex p2 =
                    barycentric_sphere_point(a, b, c,
                                             frequency - row - col - 1,
                                             col, row + 1, frequency);

                sphere_emit_flat(vertices, &idx, p0, p1, p2);
                if (col < frequency - row - 1) {
                    struct sphere_vertex p3 =
                        barycentric_sphere_point(a, b, c,
                                                 frequency - row - col - 2,
                                                 col + 1, row + 1,
                                                 frequency);
                    sphere_emit_flat(vertices, &idx, p1, p3, p2);
                }
            }
        }
    }

    app->sphere_program = link_program(sphere_vs, sphere_fs);
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
    int sx = app->width / 10;
    int sy = app->height / 10;
    int sw = app->width - sx * 2;
    int sh = app->height - sy * 2;

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
    glViewport(0, 0, app->width, app->height);
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

    mat4_perspective(projection, 58.0f * (float)M_PI / 180.0f, aspect,
                     0.1f, 16.0f);
    mat4_translate(view, 0.0f, 0.0f, -3.6f);
    mat4_rotate_y(ry, angle);
    mat4_rotate_x(rx, 0.35f * sinf(angle * 0.43f));
    mat4_mul(model, ry, rx);
    mat4_mul(pv, projection, view);
    mat4_mul(mvp, pv, model);

    glViewport(0, 0, app->width, app->height);
    glClearColor(0.015f, 0.022f, 0.032f, 1.0f);
    glClearDepthf(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);
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
    return 0;
}

static int recreate_window_surface(struct app_state *app)
{
    if (app->egl_display == EGL_NO_DISPLAY ||
        app->egl_context == EGL_NO_CONTEXT || !app->egl_window)
        return -1;

    eglMakeCurrent(app->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                   EGL_NO_CONTEXT);
    if (app->egl_surface != EGL_NO_SURFACE) {
        eglDestroySurface(app->egl_display, app->egl_surface);
        app->egl_surface = EGL_NO_SURFACE;
    }
    wl_egl_window_resize(app->egl_window, app->width, app->height, 0, 0);
    return create_window_surface(app);
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

    app->egl_window = wl_egl_window_create(app->surface, app->width,
                                           app->height);
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

    app->program = link_program(vs, fs);
    app->tex_program = link_program(tex_vs, tex_fs);
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
    return 0;
}

static void sleep_frame(void)
{
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 16000000L };
    nanosleep(&ts, NULL);
}

static double monotonic_seconds(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0.0;
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static int draw_and_swap(struct app_state *app)
{
    if (app->sphere_demo)
        render_sphere_frame(app);
    else if (app->api_smoke)
        render_api_frame(app);
    else
        render_simple_frame(app);
    if (glGetError() != GL_NO_ERROR) {
        fprintf(stderr, "mesawlegl[%d]: GL error during frame\n", app->loop);
        return -1;
    }
    if (!eglSwapBuffers(app->egl_display, app->egl_surface)) {
        fprintf(stderr, "mesawlegl[%d]: eglSwapBuffers failed (0x%x)\n",
                app->loop, eglGetError());
        return -1;
    }
    wl_display_flush(app->display);
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
    xdg_toplevel_set_title(app->toplevel, "Mesa Native Wayland EGL");
    xdg_toplevel_set_app_id(app->toplevel, "mesawlegl");

    if (init_mesa(app) < 0)
        return -1;
    wl_surface_commit(app->surface);
    return 0;
}

static void cleanup(struct app_state *app)
{
    if (app->display)
        wl_display_roundtrip(app->display);
    if (app->egl_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(app->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
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

static int run_client(int loop, int frames, int resize_every, int api_smoke,
                      int sphere_demo)
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
    app.width = WINDOW_W;
    app.height = WINDOW_H;
    app.loop = loop;
    app.api_smoke = api_smoke;
    app.sphere_demo = sphere_demo;

    if (init_wayland(&app) < 0) {
        cleanup(&app);
        return 1;
    }

    while (app.running && !app.configured &&
           wl_display_dispatch(app.display) >= 0)
        ;
    if (app.configured && recreate_window_surface(&app) < 0)
        rc = 1;
    start_sec = monotonic_seconds();
    for (app.frame = 0; rc == 0 && app.running && app.frame < app.max_frames;
         app.frame++) {
        if (app.resize_every > 0 && app.frame > 0 &&
            app.frame % app.resize_every == 0) {
            if (app.width == WINDOW_W) {
                app.width = 360;
                app.height = 260;
            } else {
                app.width = WINDOW_W;
                app.height = WINDOW_H;
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
        wl_display_dispatch_pending(app.display);
        sleep_frame();
    }
    elapsed_sec = monotonic_seconds() - start_sec;
    if (!app.configured)
        rc = 1;
    cleanup(&app);
    fprintf(stderr,
            "mesawlegl[%d]: complete frames=%d status=%d elapsed=%.3fs fps=%.1f\n",
            loop, app.frame, rc, elapsed_sec,
            elapsed_sec > 0.0 ? (double)app.frame / elapsed_sec : 0.0);
    return rc;
}

int main(int argc, char **argv)
{
    int frames = 120;
    int loops = 1;
    int resize_every = 0;
    int api_smoke = 1;
    int sphere_demo = 0;
    int rc = 0;

    setenv("LIBGL_ALWAYS_SOFTWARE", "1", 0);
    setenv("MESA_LOADER_DRIVER_OVERRIDE", "softpipe", 0);

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--frames=", 9) == 0) {
            frames = parse_positive_arg(argv[i], "--frames=", frames);
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
            frames = 3600;
            resize_every = 0;
            api_smoke = 0;
            sphere_demo = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            fprintf(stderr,
                    "usage: %s [--frames=N] [--loops=N] [--resize-every=N] [--api-smoke|--simple|--demo]\n",
                    argv[0]);
            return 0;
        } else {
            fprintf(stderr, "mesawlegl: unknown option '%s'\n", argv[i]);
            return 2;
        }
    }

    for (int loop = 1; loop <= loops; loop++) {
        rc = run_client(loop, frames, resize_every, api_smoke, sphere_demo);
        if (rc != 0)
            break;
    }
    return rc;
}
