/*
 * mesakmsgl.c - Alpine/kmscube-style direct KMS + GBM + EGL/GLES demo.
 *
 * This deliberately bypasses the Wayland compositor.  Alpine's smooth virgl
 * reference path renders into GBM front buffers, wraps them with ADDFB2, and
 * presents with SETCRTC/PAGE_FLIP.  Keep this program close to that shape so
 * xv6 can validate the same GL/display pipeline.
 */

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <gbm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "mesawlegl_sphere.h"

#ifndef EGL_PLATFORM_GBM_KHR
#define EGL_PLATFORM_GBM_KHR 0x31D7
#endif

#ifndef DRM_FORMAT_XRGB8888
#define DRM_FORMAT_XRGB8888 0x34325258u
#endif

#ifndef GBM_FORMAT_XRGB8888
#define GBM_FORMAT_XRGB8888 DRM_FORMAT_XRGB8888
#endif

#ifndef GBM_BO_USE_SCANOUT
#define GBM_BO_USE_SCANOUT (1u << 0)
#endif
#ifndef GBM_BO_USE_RENDERING
#define GBM_BO_USE_RENDERING (1u << 2)
#endif

struct kms_state {
    int fd;
    uint32_t connector_id;
    uint32_t crtc_id;
    drmModeModeInfo mode;
    drmModeCrtcPtr old_crtc;
};

struct egl_state {
    struct gbm_device *gbm;
    struct gbm_surface *surface;
    EGLDisplay display;
    EGLContext context;
    EGLSurface egl_surface;
};

struct fb_for_bo {
    int fd;
    uint32_t fb_id;
};

struct gl_state {
    GLuint program;
    GLuint vbo;
    GLint u_mvp;
    GLint u_light;
    int vertices;
};

static double now_sec(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static void bo_fb_destroy(struct gbm_bo *bo, void *data)
{
    struct fb_for_bo *fb = data;

    (void)bo;
    if (!fb)
        return;
    if (fb->fb_id)
        drmModeRmFB(fb->fd, fb->fb_id);
    free(fb);
}

static int find_kms(int fd, struct kms_state *kms)
{
    drmModeResPtr res = drmModeGetResources(fd);
    drmModeConnectorPtr conn = NULL;
    drmModeEncoderPtr enc = NULL;
    int ret = -1;

    if (!res) {
        fprintf(stderr, "mesakmsgl: drmModeGetResources failed errno=%d\n",
                errno);
        return -1;
    }

    for (int i = 0; i < res->count_connectors; i++) {
        conn = drmModeGetConnector(fd, res->connectors[i]);
        if (!conn)
            continue;
        if (conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0)
            break;
        drmModeFreeConnector(conn);
        conn = NULL;
    }
    if (!conn && res->count_connectors > 0) {
        conn = drmModeGetConnector(fd, res->connectors[0]);
        if (conn && conn->count_modes == 0) {
            drmModeFreeConnector(conn);
            conn = NULL;
        }
    }
    if (!conn)
        goto out;

    if (conn->encoder_id)
        enc = drmModeGetEncoder(fd, conn->encoder_id);
    if (!enc && conn->count_encoders > 0)
        enc = drmModeGetEncoder(fd, conn->encoders[0]);

    memset(kms, 0, sizeof(*kms));
    kms->fd = fd;
    kms->connector_id = conn->connector_id;
    kms->crtc_id = enc && enc->crtc_id ? enc->crtc_id :
                   (res->count_crtcs > 0 ? res->crtcs[0] : 0);
    kms->mode = conn->modes[0];
    if (!kms->crtc_id)
        goto out;
    kms->old_crtc = drmModeGetCrtc(fd, kms->crtc_id);

    fprintf(stderr,
            "mesakmsgl: kms connector=%u crtc=%u mode=%ux%u@%u name=%s\n",
            kms->connector_id, kms->crtc_id, kms->mode.hdisplay,
            kms->mode.vdisplay, kms->mode.vrefresh, kms->mode.name);
    ret = 0;

out:
    if (enc)
        drmModeFreeEncoder(enc);
    if (conn)
        drmModeFreeConnector(conn);
    drmModeFreeResources(res);
    return ret;
}

static EGLDisplay get_gbm_display(struct gbm_device *gbm)
{
    typedef EGLDisplay (*get_platform_display_fn)(EGLenum, void *,
                                                  const EGLAttrib *);
    const char *ext = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    get_platform_display_fn get_platform_display =
        (get_platform_display_fn)eglGetProcAddress("eglGetPlatformDisplay");

    if (get_platform_display && ext &&
        (strstr(ext, "EGL_KHR_platform_gbm") ||
         strstr(ext, "EGL_MESA_platform_gbm")))
        return get_platform_display(EGL_PLATFORM_GBM_KHR, gbm, NULL);

    get_platform_display =
        (get_platform_display_fn)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (get_platform_display && ext &&
        (strstr(ext, "EGL_KHR_platform_gbm") ||
         strstr(ext, "EGL_MESA_platform_gbm")))
        return get_platform_display(EGL_PLATFORM_GBM_KHR, gbm, NULL);

    return eglGetDisplay((EGLNativeDisplayType)gbm);
}

static int choose_gbm_config(EGLDisplay display, EGLConfig *out)
{
    static const EGLint config_attrs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, EGL_DONT_CARE,
        EGL_DEPTH_SIZE, 16,
        EGL_NONE
    };
    EGLConfig configs[64];
    EGLint nconfigs = 0;

    if (!eglChooseConfig(display, config_attrs, configs,
                         (EGLint)(sizeof(configs) / sizeof(configs[0])),
                         &nconfigs) ||
        nconfigs < 1)
        return -1;

    for (EGLint i = 0; i < nconfigs; i++) {
        EGLint visual = 0;

        if (eglGetConfigAttrib(display, configs[i], EGL_NATIVE_VISUAL_ID,
                               &visual) &&
            (uint32_t)visual == GBM_FORMAT_XRGB8888) {
            *out = configs[i];
            return 0;
        }
    }

    *out = configs[0];
    return 0;
}

static int init_egl(struct kms_state *kms, struct egl_state *egl)
{
    static const EGLint ctx_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    EGLConfig config = NULL;
    EGLint major = 0;
    EGLint minor = 0;

    memset(egl, 0, sizeof(*egl));
    egl->display = EGL_NO_DISPLAY;
    egl->context = EGL_NO_CONTEXT;
    egl->egl_surface = EGL_NO_SURFACE;

    egl->gbm = gbm_create_device(kms->fd);
    if (!egl->gbm) {
        fprintf(stderr, "mesakmsgl: gbm_create_device failed errno=%d\n",
                errno);
        return -1;
    }
    fprintf(stderr, "mesakmsgl: gbm backend=%s\n",
            gbm_device_get_backend_name(egl->gbm));

    egl->surface = gbm_surface_create(egl->gbm, kms->mode.hdisplay,
                                      kms->mode.vdisplay,
                                      GBM_FORMAT_XRGB8888,
                                      GBM_BO_USE_SCANOUT |
                                      GBM_BO_USE_RENDERING);
    if (!egl->surface) {
        fprintf(stderr, "mesakmsgl: gbm_surface_create failed errno=%d\n",
                errno);
        return -1;
    }

    egl->display = get_gbm_display(egl->gbm);
    if (egl->display == EGL_NO_DISPLAY ||
        !eglInitialize(egl->display, &major, &minor)) {
        fprintf(stderr, "mesakmsgl: eglInitialize failed err=0x%x\n",
                eglGetError());
        return -1;
    }
    if (choose_gbm_config(egl->display, &config) != 0) {
        fprintf(stderr, "mesakmsgl: eglChooseConfig failed err=0x%x\n",
                eglGetError());
        return -1;
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        fprintf(stderr, "mesakmsgl: eglBindAPI failed err=0x%x\n",
                eglGetError());
        return -1;
    }
    egl->context = eglCreateContext(egl->display, config, EGL_NO_CONTEXT,
                                    ctx_attrs);
    if (egl->context == EGL_NO_CONTEXT) {
        fprintf(stderr, "mesakmsgl: eglCreateContext failed err=0x%x\n",
                eglGetError());
        return -1;
    }
    egl->egl_surface =
        eglCreateWindowSurface(egl->display, config,
                               (EGLNativeWindowType)egl->surface, NULL);
    if (egl->egl_surface == EGL_NO_SURFACE) {
        EGLint visual = 0;

        eglGetConfigAttrib(egl->display, config, EGL_NATIVE_VISUAL_ID,
                           &visual);
        fprintf(stderr,
                "mesakmsgl: eglCreateWindowSurface failed err=0x%x visual=0x%x gbm=0x%x\n",
                eglGetError(), visual, GBM_FORMAT_XRGB8888);
        return -1;
    }
    if (!eglMakeCurrent(egl->display, egl->egl_surface, egl->egl_surface,
                        egl->context)) {
        fprintf(stderr, "mesakmsgl: eglMakeCurrent failed err=0x%x\n",
                eglGetError());
        return -1;
    }
    eglSwapInterval(egl->display, 1);

    fprintf(stderr, "mesakmsgl: EGL %d.%d vendor=%s\n", major, minor,
            eglQueryString(egl->display, EGL_VENDOR));
    fprintf(stderr, "mesakmsgl: GL vendor=%s renderer=%s version=%s\n",
            glGetString(GL_VENDOR), glGetString(GL_RENDERER),
            glGetString(GL_VERSION));
    return 0;
}

static GLuint compile_shader(GLenum type, const char *src)
{
    GLuint shader = glCreateShader(type);
    GLint ok = 0;
    char log[512];

    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "mesakmsgl: shader compile failed: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static int init_gl(struct gl_state *gls)
{
    static const char *vs =
        "attribute vec3 a_pos;\n"
        "attribute vec3 a_normal;\n"
        "uniform mat4 u_mvp;\n"
        "uniform vec3 u_light;\n"
        "varying float v_lit;\n"
        "varying vec3 v_normal;\n"
        "void main() {\n"
        "  vec3 n = normalize(a_normal);\n"
        "  v_lit = max(dot(n, normalize(u_light)), 0.0);\n"
        "  v_normal = n;\n"
        "  gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
        "}\n";
    static const char *fs =
        "precision mediump float;\n"
        "varying float v_lit;\n"
        "varying vec3 v_normal;\n"
        "void main() {\n"
        "  vec3 base = vec3(0.10, 0.58, 0.88);\n"
        "  vec3 warm = vec3(1.0, 0.62, 0.28);\n"
        "  vec3 color = mix(base, warm, v_normal.y * 0.5 + 0.5);\n"
        "  color *= 0.20 + 0.80 * v_lit;\n"
        "  gl_FragColor = vec4(color, 1.0);\n"
        "}\n";
    GLuint vert = compile_shader(GL_VERTEX_SHADER, vs);
    GLuint frag = compile_shader(GL_FRAGMENT_SHADER, fs);
    struct sphere_vertex *vertices;
    GLint ok = 0;
    char log[512];

    memset(gls, 0, sizeof(*gls));
    if (!vert || !frag)
        return -1;

    gls->program = glCreateProgram();
    glAttachShader(gls->program, vert);
    glAttachShader(gls->program, frag);
    glBindAttribLocation(gls->program, 0, "a_pos");
    glBindAttribLocation(gls->program, 1, "a_normal");
    glLinkProgram(gls->program);
    glDeleteShader(vert);
    glDeleteShader(frag);
    glGetProgramiv(gls->program, GL_LINK_STATUS, &ok);
    if (!ok) {
        glGetProgramInfoLog(gls->program, sizeof(log), NULL, log);
        fprintf(stderr, "mesakmsgl: program link failed: %s\n", log);
        return -1;
    }

    gls->vertices = mesawlegl_poly_sphere_vertex_count(5);
    vertices = calloc((size_t)gls->vertices, sizeof(*vertices));
    if (!vertices)
        return -1;
    if (mesawlegl_build_poly_sphere(vertices, gls->vertices, 5) !=
        gls->vertices) {
        free(vertices);
        return -1;
    }

    glGenBuffers(1, &gls->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, gls->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)gls->vertices *
                 sizeof(*vertices)), vertices, GL_STATIC_DRAW);
    free(vertices);

    gls->u_mvp = glGetUniformLocation(gls->program, "u_mvp");
    gls->u_light = glGetUniformLocation(gls->program, "u_light");
    glEnable(GL_DEPTH_TEST);
    return 0;
}

static void draw_frame(struct gl_state *gls, int width, int height, double t)
{
    float aspect = (float)width / (float)height;
    float proj[16], tx[16], rx[16], ry[16], model[16], mv[16], mvp[16];

    mesawlegl_mat4_perspective(proj, 1.05f, aspect, 0.1f, 32.0f);
    mesawlegl_mat4_translate(tx, 0.0f, 0.0f, -3.1f);
    mesawlegl_mat4_rotate_x(rx, (float)(t * 0.73));
    mesawlegl_mat4_rotate_y(ry, (float)(t * 1.11));
    mesawlegl_mat4_mul(model, ry, rx);
    mesawlegl_mat4_mul(mv, tx, model);
    mesawlegl_mat4_mul(mvp, proj, mv);

    glViewport(0, 0, width, height);
    glClearColor(0.02f, 0.06f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(gls->program);
    glUniformMatrix4fv(gls->u_mvp, 1, GL_FALSE, mvp);
    glUniform3f(gls->u_light, -0.35f, 0.55f, 0.75f);
    glBindBuffer(GL_ARRAY_BUFFER, gls->vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                          sizeof(struct sphere_vertex), (const void *)0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                          sizeof(struct sphere_vertex),
                          (const void *)(sizeof(GLfloat) * 3));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLES, 0, gls->vertices);
}

static uint32_t fb_for_bo(int fd, struct gbm_bo *bo)
{
    struct fb_for_bo *fb = gbm_bo_get_user_data(bo);
    uint32_t handles[4] = { 0 };
    uint32_t strides[4] = { 0 };
    uint32_t offsets[4] = { 0 };
    union gbm_bo_handle handle;

    if (fb)
        return fb->fb_id;

    fb = calloc(1, sizeof(*fb));
    if (!fb)
        return 0;
    fb->fd = fd;

    handle = gbm_bo_get_handle(bo);
    handles[0] = handle.u32;
    strides[0] = gbm_bo_get_stride(bo);
    if (drmModeAddFB2(fd, gbm_bo_get_width(bo), gbm_bo_get_height(bo),
                      GBM_FORMAT_XRGB8888, handles, strides, offsets,
                      &fb->fb_id, 0) != 0) {
        fprintf(stderr,
                "mesakmsgl: drmModeAddFB2 failed handle=%u stride=%u errno=%d\n",
                handles[0], strides[0], errno);
        free(fb);
        return 0;
    }
    gbm_bo_set_user_data(bo, fb, bo_fb_destroy);
    return fb->fb_id;
}

static void page_flip_handler(int fd, unsigned frame, unsigned sec,
                              unsigned usec, void *data)
{
    int *waiting = data;

    (void)fd;
    (void)frame;
    (void)sec;
    (void)usec;
    *waiting = 0;
}

static int present_loop(struct kms_state *kms, struct egl_state *egl,
                        struct gl_state *gls, int max_seconds)
{
    drmEventContext evctx;
    struct gbm_bo *previous_bo = NULL;
    double start = now_sec();
    double fps_start = start;
    int frames = 0;
    int fps_frames = 0;

    memset(&evctx, 0, sizeof(evctx));
    evctx.version = DRM_EVENT_CONTEXT_VERSION;
    evctx.page_flip_handler = page_flip_handler;

    for (;;) {
        struct gbm_bo *bo;
        uint32_t fb_id;
        int waiting = 1;
        double t = now_sec() - start;

        draw_frame(gls, kms->mode.hdisplay, kms->mode.vdisplay, t);
        if (!eglSwapBuffers(egl->display, egl->egl_surface)) {
            fprintf(stderr, "mesakmsgl: eglSwapBuffers failed err=0x%x\n",
                    eglGetError());
            return -1;
        }
        bo = gbm_surface_lock_front_buffer(egl->surface);
        if (!bo) {
            fprintf(stderr, "mesakmsgl: lock_front_buffer failed errno=%d\n",
                    errno);
            return -1;
        }
        fb_id = fb_for_bo(kms->fd, bo);
        if (!fb_id)
            return -1;

        if (!previous_bo) {
            if (drmModeSetCrtc(kms->fd, kms->crtc_id, fb_id, 0, 0,
                               &kms->connector_id, 1, &kms->mode) != 0) {
                fprintf(stderr, "mesakmsgl: drmModeSetCrtc failed errno=%d\n",
                        errno);
                return -1;
            }
        } else {
            if (drmModePageFlip(kms->fd, kms->crtc_id, fb_id,
                                DRM_MODE_PAGE_FLIP_EVENT, &waiting) != 0) {
                fprintf(stderr, "mesakmsgl: drmModePageFlip failed errno=%d\n",
                        errno);
                return -1;
            }
            while (waiting) {
                if (drmHandleEvent(kms->fd, &evctx) != 0) {
                    fprintf(stderr, "mesakmsgl: drmHandleEvent failed errno=%d\n",
                            errno);
                    return -1;
                }
            }
            gbm_surface_release_buffer(egl->surface, previous_bo);
        }
        previous_bo = bo;
        frames++;
        fps_frames++;

        if (now_sec() - fps_start >= 1.0) {
            double elapsed = now_sec() - fps_start;
            fprintf(stderr, "mesakmsgl: %.1f FPS frames=%d\n",
                    (double)fps_frames / elapsed, frames);
            fps_start = now_sec();
            fps_frames = 0;
        }
        if (max_seconds > 0 && now_sec() - start >= (double)max_seconds)
            break;
    }

    if (previous_bo)
        gbm_surface_release_buffer(egl->surface, previous_bo);
    return 0;
}

static void cleanup_egl(struct egl_state *egl)
{
    if (egl->display != EGL_NO_DISPLAY) {
        eglMakeCurrent(egl->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
        if (egl->egl_surface != EGL_NO_SURFACE)
            eglDestroySurface(egl->display, egl->egl_surface);
        if (egl->context != EGL_NO_CONTEXT)
            eglDestroyContext(egl->display, egl->context);
        eglTerminate(egl->display);
    }
    if (egl->surface)
        gbm_surface_destroy(egl->surface);
    if (egl->gbm)
        gbm_device_destroy(egl->gbm);
}

int main(int argc, char **argv)
{
    struct kms_state kms;
    struct egl_state egl;
    struct gl_state gls;
    const char *device = "/dev/dri/card0";
    int max_seconds = 0;
    int rc = 1;

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--device=", 9) == 0) {
            device = argv[i] + 9;
        } else if (strncmp(argv[i], "--seconds=", 10) == 0) {
            max_seconds = atoi(argv[i] + 10);
        } else {
            fprintf(stderr,
                    "usage: mesakmsgl [--device=/dev/dri/card0] [--seconds=N]\n"
                    "  --seconds=N runs for N seconds\n");
            return 2;
        }
    }

    memset(&kms, 0, sizeof(kms));
    memset(&egl, 0, sizeof(egl));
    egl.display = EGL_NO_DISPLAY;
    egl.context = EGL_NO_CONTEXT;
    egl.egl_surface = EGL_NO_SURFACE;

    kms.fd = open(device, O_RDWR | O_CLOEXEC);
    if (kms.fd < 0) {
        fprintf(stderr, "mesakmsgl: open %s failed errno=%d\n", device,
                errno);
        return 1;
    }
    if (find_kms(kms.fd, &kms) != 0 ||
        init_egl(&kms, &egl) != 0 ||
        init_gl(&gls) != 0)
        goto out;

    rc = present_loop(&kms, &egl, &gls, max_seconds) == 0 ? 0 : 1;

out:
    if (kms.old_crtc) {
        drmModeSetCrtc(kms.fd, kms.old_crtc->crtc_id,
                       kms.old_crtc->buffer_id, kms.old_crtc->x,
                       kms.old_crtc->y, &kms.connector_id, 1,
                       &kms.old_crtc->mode);
        drmModeFreeCrtc(kms.old_crtc);
    }
    cleanup_egl(&egl);
    if (kms.fd >= 0)
        close(kms.fd);
    return rc;
}
