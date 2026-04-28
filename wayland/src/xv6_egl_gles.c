/*
 * Tiny xv6-local EGL/GLES2 compatibility layer.
 *
 * This is intentionally small: it backs a Wayland wl_egl_window with one SHM
 * buffer and implements just enough GLES2 entry points for /bin/glsmoke.  It
 * is not Mesa, not hardware accelerated, and not a general OpenGL ABI.
 */

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-egl.h>

struct wl_egl_window {
    struct wl_surface *surface;
    int width;
    int height;
};

struct xv6egl_display {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_shm *shm;
    EGLint last_error;
    int initialized;
};

struct xv6egl_context {
    int marker;
};

struct xv6egl_surface {
    struct xv6egl_display *display;
    struct wl_egl_window *window;
    struct wl_buffer *buffer;
    uint32_t *pixels;
    size_t pixels_size;
    int fd;
    int width;
    int height;
};

struct attr_state {
    const GLfloat *ptr;
    GLint size;
    GLsizei stride;
    int enabled;
};

struct clear_state {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};

static struct xv6egl_display g_display;
static struct xv6egl_context g_context;
static struct xv6egl_surface *g_draw_surface;
static struct attr_state g_attrs[8];
static struct clear_state g_clear = { 0, 0, 0, 255 };
static GLuint g_next_id = 1;

static void set_error(struct xv6egl_display *display, EGLint error)
{
    if (display)
        display->last_error = error;
}

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface,
                            uint32_t version)
{
    struct xv6egl_display *display = data;

    if (strcmp(interface, wl_shm_interface.name) == 0 && !display->shm) {
        display->shm = wl_registry_bind(registry, name, &wl_shm_interface,
                                        version > 1 ? 1 : version);
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

static int create_shm_file(size_t size)
{
    char path[64];
    int fd;

    snprintf(path, sizeof(path), "/tmp/xv6egl-%ld", (long)getpid());
    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return -1;
    unlink(path);
    if (ftruncate(fd, (off_t)size) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static uint32_t pack_clear(void)
{
    return ((uint32_t)g_clear.a << 24) | ((uint32_t)g_clear.r << 16) |
           ((uint32_t)g_clear.g << 8) | g_clear.b;
}

static float edge_fn(float ax, float ay, float bx, float by, float px, float py)
{
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static const GLfloat *attr_vertex(const struct attr_state *attr, GLint first,
                                  GLsizei index)
{
    uintptr_t base = (uintptr_t)attr->ptr;
    GLsizei stride = attr->stride ? attr->stride : attr->size * (GLsizei)sizeof(GLfloat);

    return (const GLfloat *)(base + (uintptr_t)(first + index) * (uintptr_t)stride);
}

static uint32_t pack_color(const GLfloat *color)
{
    uint32_t r = (uint32_t)(color[0] * 255.0f);
    uint32_t g = (uint32_t)(color[1] * 255.0f);
    uint32_t b = (uint32_t)(color[2] * 255.0f);
    uint32_t a = color[3] <= 0.0f ? 255 : (uint32_t)(color[3] * 255.0f);

    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    if (a > 255) a = 255;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

static void draw_triangle(const GLfloat *pa, const GLfloat *pb,
                          const GLfloat *pc, const GLfloat *ca,
                          const GLfloat *cb, const GLfloat *cc)
{
    struct xv6egl_surface *surface = g_draw_surface;
    float ax, ay, bx, by, cx, cy;
    float area;
    int min_x, max_x, min_y, max_y;

    if (!surface || !surface->pixels)
        return;

    ax = (pa[0] * 0.5f + 0.5f) * (float)surface->width;
    ay = (0.5f - pa[1] * 0.5f) * (float)surface->height;
    bx = (pb[0] * 0.5f + 0.5f) * (float)surface->width;
    by = (0.5f - pb[1] * 0.5f) * (float)surface->height;
    cx = (pc[0] * 0.5f + 0.5f) * (float)surface->width;
    cy = (0.5f - pc[1] * 0.5f) * (float)surface->height;

    area = edge_fn(ax, ay, bx, by, cx, cy);
    if (area == 0.0f)
        return;

    min_x = (int)floorf(fminf(ax, fminf(bx, cx)));
    max_x = (int)ceilf(fmaxf(ax, fmaxf(bx, cx)));
    min_y = (int)floorf(fminf(ay, fminf(by, cy)));
    max_y = (int)ceilf(fmaxf(ay, fmaxf(by, cy)));

    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= surface->width) max_x = surface->width - 1;
    if (max_y >= surface->height) max_y = surface->height - 1;

    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            float px = (float)x + 0.5f;
            float py = (float)y + 0.5f;
            float w0 = edge_fn(bx, by, cx, cy, px, py) / area;
            float w1 = edge_fn(cx, cy, ax, ay, px, py) / area;
            float w2 = edge_fn(ax, ay, bx, by, px, py) / area;

            if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) {
                GLfloat color[4];

                color[0] = ca[0] * w0 + cb[0] * w1 + cc[0] * w2;
                color[1] = ca[1] * w0 + cb[1] * w1 + cc[1] * w2;
                color[2] = ca[2] * w0 + cb[2] * w1 + cc[2] * w2;
                color[3] = ca[3] * w0 + cb[3] * w1 + cc[3] * w2;
                surface->pixels[y * surface->width + x] = pack_color(color);
            }
        }
    }
}

struct wl_egl_window *wl_egl_window_create(struct wl_surface *surface,
                                           int width, int height)
{
    struct wl_egl_window *window;

    if (!surface || width <= 0 || height <= 0)
        return NULL;
    window = calloc(1, sizeof(*window));
    if (!window)
        return NULL;
    window->surface = surface;
    window->width = width;
    window->height = height;
    return window;
}

void wl_egl_window_destroy(struct wl_egl_window *egl_window)
{
    free(egl_window);
}

void wl_egl_window_resize(struct wl_egl_window *egl_window, int width,
                          int height, int dx, int dy)
{
    (void)dx;
    (void)dy;
    if (!egl_window || width <= 0 || height <= 0)
        return;
    egl_window->width = width;
    egl_window->height = height;
}

void wl_egl_window_get_attached_size(struct wl_egl_window *egl_window,
                                     int *width, int *height)
{
    if (width)
        *width = egl_window ? egl_window->width : 0;
    if (height)
        *height = egl_window ? egl_window->height : 0;
}

EGLDisplay eglGetDisplay(EGLNativeDisplayType display_id)
{
    if (!display_id)
        return EGL_NO_DISPLAY;
    memset(&g_display, 0, sizeof(g_display));
    g_display.display = (struct wl_display *)display_id;
    g_display.last_error = EGL_SUCCESS;
    return (EGLDisplay)&g_display;
}

EGLBoolean eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor)
{
    struct xv6egl_display *display = (struct xv6egl_display *)dpy;

    if (!display || !display->display)
        return EGL_FALSE;
    if (!display->initialized) {
        display->registry = wl_display_get_registry(display->display);
        wl_registry_add_listener(display->registry, &registry_listener, display);
        wl_display_roundtrip(display->display);
        wl_display_roundtrip(display->display);
        if (!display->shm) {
            set_error(display, EGL_NOT_INITIALIZED);
            return EGL_FALSE;
        }
        display->initialized = 1;
    }
    if (major)
        *major = 1;
    if (minor)
        *minor = 4;
    set_error(display, EGL_SUCCESS);
    return EGL_TRUE;
}

EGLBoolean eglTerminate(EGLDisplay dpy)
{
    struct xv6egl_display *display = (struct xv6egl_display *)dpy;

    if (!display)
        return EGL_FALSE;
    if (display->shm)
        wl_shm_destroy(display->shm);
    if (display->registry)
        wl_registry_destroy(display->registry);
    memset(display, 0, sizeof(*display));
    return EGL_TRUE;
}

EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list,
                           EGLConfig *configs, EGLint config_size,
                           EGLint *num_config)
{
    (void)attrib_list;
    if (!dpy)
        return EGL_FALSE;
    if (configs && config_size > 0)
        configs[0] = (EGLConfig)1;
    if (num_config)
        *num_config = 1;
    return EGL_TRUE;
}

EGLBoolean eglBindAPI(EGLenum api)
{
    return api == EGL_OPENGL_ES_API ? EGL_TRUE : EGL_FALSE;
}

EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config,
                            EGLContext share_context,
                            const EGLint *attrib_list)
{
    (void)config;
    (void)share_context;
    (void)attrib_list;
    if (!dpy)
        return EGL_NO_CONTEXT;
    g_context.marker = 1;
    return (EGLContext)&g_context;
}

EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                  EGLNativeWindowType win,
                                  const EGLint *attrib_list)
{
    struct xv6egl_display *display = (struct xv6egl_display *)dpy;
    struct wl_egl_window *window = (struct wl_egl_window *)win;
    struct xv6egl_surface *surface;
    struct wl_shm_pool *pool;
    int stride;

    (void)config;
    (void)attrib_list;
    if (!display || !display->shm || !window || window->width <= 0 ||
        window->height <= 0)
        return EGL_NO_SURFACE;

    surface = calloc(1, sizeof(*surface));
    if (!surface)
        return EGL_NO_SURFACE;
    surface->fd = -1;
    surface->display = display;
    surface->window = window;
    surface->width = window->width;
    surface->height = window->height;
    stride = surface->width * 4;
    surface->pixels_size = (size_t)stride * (size_t)surface->height;
    surface->fd = create_shm_file(surface->pixels_size);
    if (surface->fd < 0)
        goto fail;
    surface->pixels = mmap(NULL, surface->pixels_size, PROT_READ | PROT_WRITE,
                           MAP_SHARED, surface->fd, 0);
    if (surface->pixels == MAP_FAILED) {
        surface->pixels = NULL;
        goto fail;
    }
    pool = wl_shm_create_pool(display->shm, surface->fd,
                              (int)surface->pixels_size);
    surface->buffer = wl_shm_pool_create_buffer(pool, 0, surface->width,
                                                surface->height, stride,
                                                WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    if (!surface->buffer)
        goto fail;
    return (EGLSurface)surface;

fail:
    if (surface->pixels)
        munmap(surface->pixels, surface->pixels_size);
    if (surface->fd >= 0)
        close(surface->fd);
    free(surface);
    return EGL_NO_SURFACE;
}

EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surface)
{
    struct xv6egl_surface *egl_surface = (struct xv6egl_surface *)surface;

    (void)dpy;
    if (!egl_surface)
        return EGL_FALSE;
    if (g_draw_surface == egl_surface)
        g_draw_surface = NULL;
    if (egl_surface->buffer)
        wl_buffer_destroy(egl_surface->buffer);
    if (egl_surface->pixels)
        munmap(egl_surface->pixels, egl_surface->pixels_size);
    if (egl_surface->fd >= 0)
        close(egl_surface->fd);
    free(egl_surface);
    return EGL_TRUE;
}

EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext ctx)
{
    (void)dpy;
    (void)ctx;
    memset(&g_context, 0, sizeof(g_context));
    return EGL_TRUE;
}

EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read,
                          EGLContext ctx)
{
    (void)dpy;
    (void)read;
    if (ctx == EGL_NO_CONTEXT || draw == EGL_NO_SURFACE) {
        g_draw_surface = NULL;
        return EGL_TRUE;
    }
    g_draw_surface = (struct xv6egl_surface *)draw;
    return EGL_TRUE;
}

EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface)
{
    struct xv6egl_surface *egl_surface = (struct xv6egl_surface *)surface;

    (void)dpy;
    if (!egl_surface || !egl_surface->window)
        return EGL_FALSE;
    wl_surface_attach(egl_surface->window->surface, egl_surface->buffer, 0, 0);
    wl_surface_damage(egl_surface->window->surface, 0, 0, egl_surface->width,
                      egl_surface->height);
    wl_surface_commit(egl_surface->window->surface);
    return EGL_TRUE;
}

EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval)
{
    (void)dpy;
    (void)interval;
    return EGL_TRUE;
}

EGLint eglGetError(void)
{
    EGLint error = g_display.last_error ? g_display.last_error : EGL_SUCCESS;

    g_display.last_error = EGL_SUCCESS;
    return error;
}

const char *eglQueryString(EGLDisplay dpy, EGLint name)
{
    (void)dpy;
    switch (name) {
    case EGL_VENDOR:
        return "xv6";
    case EGL_VERSION:
        return "1.4 xv6-compat";
    case EGL_CLIENT_APIS:
        return "OpenGL_ES";
    case EGL_EXTENSIONS:
        return "";
    default:
        return NULL;
    }
}

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    g_clear.r = (uint8_t)(fmaxf(0.0f, fminf(red, 1.0f)) * 255.0f);
    g_clear.g = (uint8_t)(fmaxf(0.0f, fminf(green, 1.0f)) * 255.0f);
    g_clear.b = (uint8_t)(fmaxf(0.0f, fminf(blue, 1.0f)) * 255.0f);
    g_clear.a = (uint8_t)(fmaxf(0.0f, fminf(alpha, 1.0f)) * 255.0f);
}

void glClear(GLbitfield mask)
{
    uint32_t color;

    if (!(mask & GL_COLOR_BUFFER_BIT) || !g_draw_surface ||
        !g_draw_surface->pixels)
        return;
    color = pack_clear();
    for (int i = 0; i < g_draw_surface->width * g_draw_surface->height; i++)
        g_draw_surface->pixels[i] = color;
}

GLuint glCreateShader(GLenum type)
{
    (void)type;
    return g_next_id++;
}

void glShaderSource(GLuint shader, GLsizei count, const GLchar *const *string,
                    const GLint *length)
{
    (void)shader;
    (void)count;
    (void)string;
    (void)length;
}

void glCompileShader(GLuint shader)
{
    (void)shader;
}

void glGetShaderiv(GLuint shader, GLenum pname, GLint *params)
{
    (void)shader;
    if (params && pname == GL_COMPILE_STATUS)
        *params = GL_TRUE;
}

GLuint glCreateProgram(void)
{
    return g_next_id++;
}

void glAttachShader(GLuint program, GLuint shader)
{
    (void)program;
    (void)shader;
}

void glBindAttribLocation(GLuint program, GLuint index, const GLchar *name)
{
    (void)program;
    (void)index;
    (void)name;
}

void glLinkProgram(GLuint program)
{
    (void)program;
}

void glGetProgramiv(GLuint program, GLenum pname, GLint *params)
{
    (void)program;
    if (params && pname == GL_LINK_STATUS)
        *params = GL_TRUE;
}

void glUseProgram(GLuint program)
{
    (void)program;
}

GLint glGetAttribLocation(GLuint program, const GLchar *name)
{
    (void)program;
    if (strcmp(name, "a_pos") == 0)
        return 0;
    if (strcmp(name, "a_color") == 0)
        return 1;
    return -1;
}

void glVertexAttribPointer(GLuint index, GLint size, GLenum type,
                           GLboolean normalized, GLsizei stride,
                           const void *pointer)
{
    (void)normalized;
    if (index >= sizeof(g_attrs) / sizeof(g_attrs[0]) || type != GL_FLOAT)
        return;
    g_attrs[index].ptr = pointer;
    g_attrs[index].size = size;
    g_attrs[index].stride = stride;
}

void glEnableVertexAttribArray(GLuint index)
{
    if (index < sizeof(g_attrs) / sizeof(g_attrs[0]))
        g_attrs[index].enabled = 1;
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    struct attr_state *pos = &g_attrs[0];
    struct attr_state *color = &g_attrs[1];

    if (mode != GL_TRIANGLES || count < 3 || !pos->enabled ||
        !color->enabled || !pos->ptr || !color->ptr)
        return;
    for (GLsizei i = 0; i + 2 < count; i += 3) {
        draw_triangle(attr_vertex(pos, first, i),
                      attr_vertex(pos, first, i + 1),
                      attr_vertex(pos, first, i + 2),
                      attr_vertex(color, first, i),
                      attr_vertex(color, first, i + 1),
                      attr_vertex(color, first, i + 2));
    }
}

void glDeleteShader(GLuint shader)
{
    (void)shader;
}

void glDeleteProgram(GLuint program)
{
    (void)program;
}

const GLubyte *glGetString(GLenum name)
{
    switch (name) {
    case GL_VENDOR:
        return (const GLubyte *)"xv6";
    case GL_RENDERER:
        return (const GLubyte *)"xv6 software GLES smoke";
    case GL_VERSION:
        return (const GLubyte *)"OpenGL ES 2.0 xv6-compat";
    case GL_SHADING_LANGUAGE_VERSION:
        return (const GLubyte *)"OpenGL ES GLSL ES 1.00 xv6-compat";
    default:
        return (const GLubyte *)"";
    }
}

GLenum glGetError(void)
{
    return GL_NO_ERROR;
}
