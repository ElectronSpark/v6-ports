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
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define WINDOW_W 480
#define WINDOW_H 360

#define FB_GPU_BO_CREATE       0x4614
#define FB_GPU_BO_DESTROY      0x4616
#define FB_GPU_BO_F_EXPORTABLE 0x1

struct fb_gpu_bo_create {
    uint32_t width, height, flags, pitch;
    uint64_t size, addr;
    uint32_t handle, reserved;
};

struct fb_gpu_bo_destroy {
    uint32_t handle, flags;
};

struct vertex {
    GLfloat x;
    GLfloat y;
    GLfloat r;
    GLfloat g;
    GLfloat b;
    GLfloat a;
};

struct present_buffer {
    struct wl_buffer *wl_buffer;
    uint32_t *pixels;
    size_t size;
    int stride;
    int width;
    int height;
    int fd;
    int fb_fd;
    uint32_t bo_handle;
    int bo_backed;
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
    struct present_buffer buffer;
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
    int max_frames;
    int resize_every;
    int width;
    int height;
    int loop;
};

static const struct wl_interface *xv6_gpu_buffer_create_types[] = {
    &wl_buffer_interface,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
};

static const struct wl_message xv6_gpu_buffer_manager_requests[] = {
    { "create_buffer", "nuiiiu", xv6_gpu_buffer_create_types },
};

static const struct wl_interface xv6_gpu_buffer_manager_interface = {
    "xv6_gpu_buffer_manager",
    1,
    1,
    xv6_gpu_buffer_manager_requests,
    0,
    NULL,
};

static struct wl_buffer *xv6_gpu_buffer_manager_create_buffer(
    struct wl_proxy *manager, uint32_t handle, int32_t width, int32_t height,
    int32_t stride, uint32_t format)
{
    return (struct wl_buffer *)wl_proxy_marshal_flags(
        manager, 0, &wl_buffer_interface, wl_proxy_get_version(manager), 0,
        NULL, handle, width, height, stride, format);
}

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
    if (get_platform_display != NULL && client_ext != NULL &&
        strstr(client_ext, "EGL_EXT_platform_base") != NULL)
        return get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, NULL, NULL);
    return eglGetDisplay(EGL_DEFAULT_DISPLAY);
}

static int create_shm_file(size_t size)
{
    char path[64];
    int fd;

    snprintf(path, sizeof(path), "/tmp/mesaglsmoke-%ld", (long)getpid());
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

static int init_present_buffer(struct app_state *app)
{
    struct present_buffer *buf = &app->buffer;
    int stride = app->width * 4;

    memset(buf, 0, sizeof(*buf));
    buf->fd = -1;
    buf->fb_fd = -1;
    buf->width = app->width;
    buf->height = app->height;
    buf->stride = stride;
    buf->size = (size_t)stride * (size_t)app->height;

    if (app->gpu_manager) {
        struct fb_gpu_bo_create bo;

        memset(&bo, 0, sizeof(bo));
        bo.width = (uint32_t)buf->width;
        bo.height = (uint32_t)buf->height;
        bo.flags = FB_GPU_BO_F_EXPORTABLE;
        buf->fb_fd = open("/dev/fb0", O_RDWR);
        if (buf->fb_fd >= 0 &&
            ioctl(buf->fb_fd, FB_GPU_BO_CREATE, &bo) == 0 &&
            bo.addr != 0 && bo.size != 0 && bo.pitch >= (uint32_t)stride &&
            bo.handle != 0) {
            buf->pixels = (uint32_t *)bo.addr;
            buf->size = (size_t)bo.size;
            buf->stride = (int)bo.pitch;
            buf->bo_handle = bo.handle;
            buf->bo_backed = 1;
            buf->wl_buffer = xv6_gpu_buffer_manager_create_buffer(
                app->gpu_manager, bo.handle, buf->width, buf->height,
                buf->stride, WL_SHM_FORMAT_XRGB8888);
            if (buf->wl_buffer)
                return 0;
        }
        if (buf->bo_handle) {
            struct fb_gpu_bo_destroy destroy = { .handle = buf->bo_handle };
            ioctl(buf->fb_fd, FB_GPU_BO_DESTROY, &destroy);
        }
        if (buf->pixels && buf->bo_backed)
            munmap(buf->pixels, buf->size);
        if (buf->fb_fd >= 0)
            close(buf->fb_fd);
        memset(buf, 0, sizeof(*buf));
        buf->fd = -1;
        buf->fb_fd = -1;
        buf->width = app->width;
        buf->height = app->height;
        buf->stride = stride;
        buf->size = (size_t)stride * (size_t)app->height;
    }

    if (!app->shm)
        return -1;

    buf->fd = create_shm_file(buf->size);
    if (buf->fd < 0)
        return -1;
    buf->pixels = mmap(NULL, buf->size, PROT_READ | PROT_WRITE, MAP_SHARED,
                       buf->fd, 0);
    if (buf->pixels == MAP_FAILED) {
        buf->pixels = NULL;
        return -1;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(app->shm, buf->fd,
                                                  (int)buf->size);
    buf->wl_buffer = wl_shm_pool_create_buffer(pool, 0, buf->width,
                                               buf->height, buf->stride,
                                               WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    return buf->wl_buffer ? 0 : -1;
}

static void destroy_present_buffer(struct app_state *app)
{
    struct present_buffer *buf = &app->buffer;

    if (buf->wl_buffer)
        wl_buffer_destroy(buf->wl_buffer);
    if (buf->pixels)
        munmap(buf->pixels, buf->size);
    if (buf->bo_handle && buf->fb_fd >= 0) {
        struct fb_gpu_bo_destroy destroy = { .handle = buf->bo_handle };
        ioctl(buf->fb_fd, FB_GPU_BO_DESTROY, &destroy);
    }
    if (buf->fb_fd >= 0)
        close(buf->fb_fd);
    if (buf->fd >= 0)
        close(buf->fd);

    memset(buf, 0, sizeof(*buf));
    buf->fd = -1;
    buf->fb_fd = -1;
}

static GLuint compile_shader(GLenum type, const char *src)
{
    GLuint shader = glCreateShader(type);
    GLint ok = GL_FALSE;

    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
        fprintf(stderr, "mesaglsmoke: shader compile failed\n");
    return shader;
}

static int init_mesa(struct app_state *app)
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
    EGLint config_attrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
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
    GLint ok = GL_FALSE;
    GLuint vshader;
    GLuint fshader;

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
        fprintf(stderr, "mesaglsmoke: program link failed\n");
        return -1;
    }
    app->attr_pos = glGetAttribLocation(app->program, "a_pos");
    app->attr_color = glGetAttribLocation(app->program, "a_color");
    if (app->attr_pos < 0 || app->attr_color < 0)
        return -1;

    fprintf(stderr, "mesaglsmoke: EGL %d.%d GL %s renderer=%s buffer=%s\n",
            major, minor, glGetString(GL_VERSION), glGetString(GL_RENDERER),
            app->buffer.bo_backed ? "xv6-gpu-bo" : "wl-shm");
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

static int resize_surface_and_buffer(struct app_state *app)
{
    if (app->width == WINDOW_W) {
        app->width = 360;
        app->height = 260;
    } else {
        app->width = WINDOW_W;
        app->height = WINDOW_H;
    }

    destroy_present_buffer(app);
    if (init_present_buffer(app) < 0)
        return -1;
    if (recreate_mesa_surface(app) < 0)
        return -1;
    return 0;
}

static void render_frame(struct app_state *app)
{
    float angle = app->frame * 0.055f;
    float s = sinf(angle);
    float c = cosf(angle);
    float r = 0.72f;
    struct vertex vertices[3] = {
        { -s * r, c * r - 0.04f, 0.98f, 0.21f, 0.18f, 1.0f },
        { (0.92f * c + 0.72f * s) * r,
          (0.92f * s - 0.72f * c) * r - 0.04f, 0.18f, 0.80f, 0.42f, 1.0f },
        { (-0.92f * c + 0.72f * s) * r,
          (-0.92f * s - 0.72f * c) * r - 0.04f, 0.20f, 0.42f, 1.0f, 1.0f },
    };

    glViewport(0, 0, app->width, app->height);
    glClearColor(0.03f, 0.055f, 0.07f, 1.0f);
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

static int copy_pixels_to_wayland_buffer(struct app_state *app)
{
    size_t bytes = (size_t)app->width * (size_t)app->height * 4;
    uint8_t *rgba = malloc(bytes);

    if (!rgba)
        return -1;
    glReadPixels(0, 0, app->width, app->height, GL_RGBA, GL_UNSIGNED_BYTE,
                 rgba);
    for (int y = 0; y < app->height; y++) {
        uint32_t *dst = (uint32_t *)((uint8_t *)app->buffer.pixels +
                                     (size_t)y * (size_t)app->buffer.stride);
        uint8_t *src = rgba + (size_t)(app->height - 1 - y) *
                              (size_t)app->width * 4;
        for (int x = 0; x < app->width; x++) {
            uint8_t r = src[x * 4 + 0];
            uint8_t g = src[x * 4 + 1];
            uint8_t b = src[x * 4 + 2];
            dst[x] = 0xff000000u | ((uint32_t)r << 16) |
                     ((uint32_t)g << 8) | b;
        }
    }
    free(rgba);
    return 0;
}

static void draw_and_commit(struct app_state *app);

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
    render_frame(app);
    if (copy_pixels_to_wayland_buffer(app) < 0) {
        app->running = 0;
        return;
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
    xdg_toplevel_set_title(app->toplevel, "Mesa GL Smoke");
    xdg_toplevel_set_app_id(app->toplevel, "mesaglsmoke");

    if (init_present_buffer(app) < 0 || init_mesa(app) < 0)
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
    destroy_present_buffer(app);
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

static int run_client(int loop, int frames, int resize_every)
{
    struct app_state app;
    int rc = 0;

    memset(&app, 0, sizeof(app));
    app.egl_display = EGL_NO_DISPLAY;
    app.egl_context = EGL_NO_CONTEXT;
    app.egl_surface = EGL_NO_SURFACE;
    app.buffer.fd = -1;
    app.buffer.fb_fd = -1;
    app.running = 1;
    app.max_frames = frames;
    app.resize_every = resize_every;
    app.width = WINDOW_W;
    app.height = WINDOW_H;
    app.loop = loop;

    if (init_wayland(&app) < 0) {
        cleanup(&app);
        return 1;
    }

    while (app.running && wl_display_dispatch(app.display) >= 0)
        ;
    if (app.running)
        rc = 1;
    cleanup(&app);
    fprintf(stderr, "mesaglsmoke[%d]: complete frames=%d status=%d\n",
            loop, app.frame, rc);
    return rc;
}

int main(int argc, char **argv)
{
    int frames = 120;
    int loops = 1;
    int resize_every = 0;
    int rc = 0;

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--frames=", 9) == 0) {
            frames = parse_positive_arg(argv[i], "--frames=", frames);
        } else if (strncmp(argv[i], "--loops=", 8) == 0) {
            loops = parse_positive_arg(argv[i], "--loops=", loops);
        } else if (strncmp(argv[i], "--resize-every=", 15) == 0) {
            resize_every = parse_positive_arg(argv[i], "--resize-every=",
                                              resize_every);
        } else if (strcmp(argv[i], "--help") == 0) {
            fprintf(stderr,
                    "usage: %s [--frames=N] [--loops=N] [--resize-every=N]\n",
                    argv[0]);
            return 0;
        } else {
            fprintf(stderr, "mesaglsmoke: unknown option '%s'\n", argv[i]);
            return 2;
        }
    }

    for (int loop = 1; loop <= loops; loop++) {
        rc = run_client(loop, frames, resize_every);
        if (rc != 0)
            break;
    }
    return rc;
}
