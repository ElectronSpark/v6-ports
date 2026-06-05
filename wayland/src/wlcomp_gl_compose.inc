/* virgl/EGL compositor path.
 *
 * This mirrors the Linux/Weston/Alpine desktop primitive: import a client
 * dma-buf into EGL, bind it as a GL texture, and sample it into the
 * full-screen virgl framebuffer target before scanout.
 */

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <gbm.h>

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif
#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif
#ifndef GL_TEXTURE_EXTERNAL_OES
#define GL_TEXTURE_EXTERNAL_OES 0x8D65
#endif
#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#endif
#ifndef GL_SYNC_FLUSH_COMMANDS_BIT
#define GL_SYNC_FLUSH_COMMANDS_BIT 0x00000001
#endif
#ifndef GL_ALREADY_SIGNALED
#define GL_ALREADY_SIGNALED 0x911A
#endif
#ifndef GL_TIMEOUT_EXPIRED
#define GL_TIMEOUT_EXPIRED 0x911B
#endif
#ifndef GL_CONDITION_SATISFIED
#define GL_CONDITION_SATISFIED 0x911C
#endif
#ifndef GL_WAIT_FAILED
#define GL_WAIT_FAILED 0x911D
#endif

typedef EGLDisplay (*wlcomp_egl_get_platform_display_fn)(EGLenum, void *,
                                                         const EGLAttrib *);
typedef EGLImageKHR (*wlcomp_egl_create_image_khr_fn)(EGLDisplay, EGLContext,
                                                      EGLenum, EGLClientBuffer,
                                                      const EGLint *);
typedef EGLBoolean (*wlcomp_egl_destroy_image_khr_fn)(EGLDisplay,
                                                      EGLImageKHR);
typedef void (*wlcomp_gl_image_target_texture_2d_fn)(GLenum, GLeglImageOES);
typedef GLsync (*wlcomp_gl_fence_sync_fn)(GLenum, GLbitfield);
typedef GLenum (*wlcomp_gl_client_wait_sync_fn)(GLsync, GLbitfield, GLuint64);
typedef void (*wlcomp_gl_delete_sync_fn)(GLsync);

struct wlcomp_gl_compose_target {
    void *egl_image;
    GLuint texture;
    uint32_t handle;
    uint32_t width;
    uint32_t height;
};

#define WLCOMP_GL_COMPOSE_TARGET_CACHE_SIZE 3

struct wlcomp_gl_compose_state {
    int enabled;
    int initialized;
    int available;
    int init_logged;
    int import_logs;
    EGLDisplay display;
    EGLContext context;
    EGLSurface surface;
    wlcomp_egl_create_image_khr_fn create_image;
    wlcomp_egl_destroy_image_khr_fn destroy_image;
    wlcomp_gl_image_target_texture_2d_fn image_target_texture_2d;
    wlcomp_gl_fence_sync_fn fence_sync;
    wlcomp_gl_client_wait_sync_fn client_wait_sync;
    wlcomp_gl_delete_sync_fn delete_sync;
    GLuint fbo;
    GLuint color_tex;
    GLuint program;
    GLuint external_program;
    GLenum client_texture_target;
    GLuint vbo;
    void *target_egl_image;
    GLuint target_texture;
    GLuint fbo_attached_texture;
    int fbo_complete;
    GLint program_tex_uniform;
    GLint external_program_tex_uniform;
    GLfloat last_quad[16];
    int last_quad_valid;
    uint32_t target_handle;
    uint32_t target_width;
    uint32_t target_height;
    struct wlcomp_gl_compose_target target_cache[WLCOMP_GL_COMPOSE_TARGET_CACHE_SIZE];
    struct gbm_device *gbm;
    int drm_fd;
};

static struct wlcomp_gl_compose_state g_gl_compose;

static int wlcomp_gl_compose_enabled(void)
{
    static int initialized;

    if (!initialized) {
        const char *env = getenv("XV6_WLCOMP_GL_COMPOSE");
        int default_enabled = g_fb_virgl_backed;

        if (cmdline_int_value("wlcomp_pageflip_diag", 0) != 0)
            default_enabled = 0;
        g_gl_compose.enabled =
            cmdline_int_value("wlcomp_gl_compose", default_enabled) != 0;
        if (env && env[0])
            g_gl_compose.enabled =
                strcmp(env, "0") != 0 &&
                strcmp(env, "no") != 0 && strcmp(env, "false") != 0 &&
                strcmp(env, "off") != 0;
        initialized = 1;
    }
    return g_gl_compose.enabled;
}

static int wlcomp_gl_compose_checksum_enabled(void)
{
    static int initialized;
    static int enabled;
    const char *env;

    if (initialized)
        return enabled;
    initialized = 1;
    enabled = cmdline_flag_enabled("wlcomp_gl_checksum");
    env = getenv("XV6_WLCOMP_GL_CHECKSUM");
    if (env && *env)
        enabled = strcmp(env, "0") != 0 &&
                  strcmp(env, "no") != 0 &&
                  strcmp(env, "false") != 0 &&
                  strcmp(env, "off") != 0;
    return enabled;
}

static int wlcomp_gl_compose_reimport_each_present(void)
{
    static int initialized;
    static int enabled;

    if (initialized)
        return enabled;
    initialized = 1;
    /*
     * Match the Linux compositor model: import each wl_buffer dma-buf once,
     * keep the EGLImage/texture cached with that buffer, and repaint from it
     * while normal Wayland release/fence sequencing controls reuse.  Per-frame
     * reimport is useful as a diagnostic for stale imports, but it is much
     * noisier and slower than Alpine/Weston-style buffer lifetime.
     */
    enabled = cmdline_int_value("wlcomp_gl_reimport", 0) != 0;
    return enabled;
}

static int wlcomp_gl_compose_target_top_left(void)
{
    static int initialized;
    static int enabled;

    if (initialized)
        return enabled;
    initialized = 1;
    /*
     * The compositor target is the real scanout framebuffer imported as a
     * dma-buf.  Its memory layout is the same top-left-origin layout used by
     * the CPU desktop renderer and scanout ioctls, so destination placement
     * must use desktop coordinates directly.  The old bottom-left GL-FBO
     * transform is kept behind a switch for diagnostics against Mesa/Linux.
     */
    enabled = cmdline_int_value("wlcomp_gl_target_top_left", 1) != 0;
    return enabled;
}

static int wlcomp_gl_compose_source_top_left(void)
{
    static int initialized;
    static int enabled;

    if (initialized)
        return enabled;
    initialized = 1;
    /*
     * Wayland damage and desktop placement are top-left based, and the Mesa
     * Wayland virgl client buffers imported here already match that source
     * orientation.  Keep this separate from target placement so any future
     * host/driver inversion can still be diagnosed independently.
     */
    enabled = cmdline_int_value("wlcomp_gl_source_top_left", 1) != 0;
    return enabled;
}

static int wlcomp_gl_compose_finish_each_present(void)
{
    static int initialized;
    static int enabled;
    const char *env;

    if (initialized)
        return enabled;
    initialized = 1;
    enabled = cmdline_int_value("wlcomp_gl_finish", 0) != 0;
    env = getenv("XV6_WLCOMP_GL_FINISH");
    if (env && *env)
        enabled = strcmp(env, "0") != 0 &&
                  strcmp(env, "no") != 0 &&
                  strcmp(env, "false") != 0 &&
                  strcmp(env, "off") != 0;
    if (enabled)
        fprintf(stderr, "wlcomp: gl-compose finish-each-present enabled\n");
    return enabled;
}

static int wlcomp_gl_compose_flush_each_present(void)
{
    static int initialized;
    static int enabled;
    static int logged;
    const char *env;

    if (initialized)
        return enabled;
    initialized = 1;
    enabled = cmdline_int_value("wlcomp_gl_flush", 0) != 0;
    env = getenv("XV6_WLCOMP_GL_FLUSH");
    if (env && *env)
        enabled = strcmp(env, "0") != 0 &&
                  strcmp(env, "no") != 0 &&
                  strcmp(env, "false") != 0 &&
                  strcmp(env, "off") != 0;
    if (!enabled && !logged) {
        fprintf(stderr,
                "wlcomp: gl-compose per-present flush disabled\n");
        logged = 1;
    }
    return enabled;
}

static int wlcomp_gl_compose_flush_after_scanout(void)
{
    static int initialized;
    static int enabled;
    const char *env;

    if (initialized)
        return enabled;
    initialized = 1;
    enabled = cmdline_int_value("wlcomp_gl_flush_after_scanout", 0) != 0;
    env = getenv("XV6_WLCOMP_GL_FLUSH_AFTER_SCANOUT");
    if (env && *env)
        enabled = strcmp(env, "0") != 0 &&
                  strcmp(env, "no") != 0 &&
                  strcmp(env, "false") != 0 &&
                  strcmp(env, "off") != 0;
    if (enabled)
        fprintf(stderr,
                "wlcomp: gl-compose flush-after-scanout enabled\n");
    return enabled;
}

static void wlcomp_gl_compose_flush_submitted(void)
{
    if (!g_gl_compose.initialized || !g_gl_compose.available)
        return;
    if (!eglMakeCurrent(g_gl_compose.display, g_gl_compose.surface,
                        g_gl_compose.surface, g_gl_compose.context))
        return;
    glFlush();
}

static int wlcomp_gl_compose_fence_wait_us(void)
{
    static int initialized;
    static int wait_us;
    const char *env;

    if (initialized)
        return wait_us;
    initialized = 1;
    wait_us = cmdline_int_value("wlcomp_gl_fence_wait_us", 0);
    env = getenv("XV6_WLCOMP_GL_FENCE_WAIT_US");
    if (env && *env)
        wait_us = atoi(env);
    if (wait_us < 0)
        wait_us = 0;
    if (wait_us > 0)
        fprintf(stderr,
                "wlcomp: gl-compose fence wait enabled wait_us=%d\n",
                wait_us);
    return wait_us;
}

static int wlcomp_gl_compose_submit_fence_enabled(void)
{
    static int initialized;
    static int enabled;
    const char *env;

    if (initialized)
        return enabled;
    initialized = 1;
    enabled = cmdline_int_value("wlcomp_gl_submit_fence", 0) != 0;
    env = getenv("XV6_WLCOMP_GL_SUBMIT_FENCE");
    if (env && *env)
        enabled = strcmp(env, "0") != 0 &&
                  strcmp(env, "no") != 0 &&
                  strcmp(env, "false") != 0 &&
                  strcmp(env, "off") != 0;
    if (enabled)
        fprintf(stderr,
                "wlcomp: gl-compose submit-fence enabled\n");
    return enabled;
}

static int wlcomp_gl_compose_ensure_sync_funcs(void)
{
    if (!g_gl_compose.fence_sync) {
        g_gl_compose.fence_sync = (wlcomp_gl_fence_sync_fn)
            eglGetProcAddress("glFenceSync");
        g_gl_compose.client_wait_sync = (wlcomp_gl_client_wait_sync_fn)
            eglGetProcAddress("glClientWaitSync");
        g_gl_compose.delete_sync = (wlcomp_gl_delete_sync_fn)
            eglGetProcAddress("glDeleteSync");
    }
    return g_gl_compose.fence_sync != NULL &&
           g_gl_compose.client_wait_sync != NULL &&
           g_gl_compose.delete_sync != NULL;
}

static void wlcomp_gl_compose_clear_release_sync(struct wlcomp_buffer *buf)
{
    if (!buf || !buf->gl_release_sync)
        return;
    if (wlcomp_gl_compose_ensure_sync_funcs())
        g_gl_compose.delete_sync((GLsync)buf->gl_release_sync);
    buf->gl_release_sync = NULL;
}

static void wlcomp_gl_compose_mark_buffer_release_sync(struct wlcomp_buffer *buf)
{
    GLsync sync;
    static int logs;

    if (!buf || !wlcomp_gl_compose_submit_fence_enabled())
        return;
    if (!wlcomp_gl_compose_ensure_sync_funcs())
        return;

    sync = g_gl_compose.fence_sync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (!sync)
        return;
    wlcomp_gl_compose_clear_release_sync(buf);
    buf->gl_release_sync = (void *)sync;
    if (logs < 12) {
        fprintf(stderr,
                "wlcomp: gl-compose buffer release-sync queued\n");
        logs++;
    }
}

static int wlcomp_gl_compose_buffer_release_ready(struct wlcomp_buffer *buf)
{
    GLenum status;

    if (!buf || !buf->gl_release_sync)
        return 1;
    if (!wlcomp_gl_compose_ensure_sync_funcs())
        return 0;

    status = g_gl_compose.client_wait_sync(
        (GLsync)buf->gl_release_sync, 0, 0);
    if (status == GL_ALREADY_SIGNALED ||
        status == GL_CONDITION_SATISFIED) {
        wlcomp_gl_compose_clear_release_sync(buf);
        return 1;
    }
    if (status == GL_WAIT_FAILED) {
        wlcomp_gl_compose_clear_release_sync(buf);
        return 1;
    }
    return 0;
}

static void wlcomp_gl_compose_wait_draw_fence(void)
{
    int wait_us = wlcomp_gl_compose_fence_wait_us();
    GLsync sync;
    GLenum status;
    static int logs;

    if (wait_us <= 0)
        return;
    if (!wlcomp_gl_compose_ensure_sync_funcs()) {
        if (logs < 8) {
            fprintf(stderr,
                    "wlcomp: gl-compose fence wait unavailable\n");
            logs++;
        }
        return;
    }
    sync = g_gl_compose.fence_sync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (!sync)
        return;
    status = g_gl_compose.client_wait_sync(sync, GL_SYNC_FLUSH_COMMANDS_BIT,
                                           (GLuint64)wait_us * 1000ULL);
    g_gl_compose.delete_sync(sync);
    if (logs < 8) {
        fprintf(stderr,
                "wlcomp: gl-compose fence wait status=0x%x wait_us=%d\n",
                status, wait_us);
        logs++;
    }
}

static void wlcomp_gl_compose_capture_rect(int x, int gl_y, int w, int h)
{
    static int captured;
    static int seen;
    int target;
    uint8_t *pixels;
    FILE *fp;

    if (captured || !cmdline_flag_enabled("wlcomp_gl_capture") ||
        x < 0 || gl_y < 0 || w <= 0 || h <= 0)
        return;
    target = cmdline_int_value("wlcomp_gl_capture_frame", 1);
    if (target <= 0)
        target = 1;
    seen++;
    if (seen < target)
        return;
    captured = 1;
    pixels = malloc((size_t)w * (size_t)h * 4);
    if (!pixels) {
        fprintf(stderr, "wlcomp: gl-compose capture allocation failed\n");
        return;
    }
    memset(pixels, 0, (size_t)w * (size_t)h * 4);
    glReadPixels(x, gl_y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    fp = fopen("/capture-gl.ppm", "wb");
    if (!fp) {
        fprintf(stderr,
                "wlcomp: gl-compose capture open failed errno=%d (%s)\n",
                errno, strerror(errno));
        free(pixels);
        return;
    }
    fprintf(fp, "P6\n%d %d\n255\n", w, h);
    for (int row = h - 1; row >= 0; row--) {
        uint8_t *src = pixels + (size_t)row * (size_t)w * 4;

        for (int col = 0; col < w; col++) {
            fputc(src[col * 4 + 0], fp);
            fputc(src[col * 4 + 1], fp);
            fputc(src[col * 4 + 2], fp);
        }
    }
    fclose(fp);
    free(pixels);
    fprintf(stderr,
            "wlcomp: gl-compose captured /capture-gl.ppm rect=%d,%d %dx%d\n",
            x, gl_y, w, h);
}

static EGLDisplay wlcomp_gl_get_display(void)
{
    const char *client_ext = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    wlcomp_egl_get_platform_display_fn get_platform_display;
    EGLDisplay display;

    get_platform_display =
        (wlcomp_egl_get_platform_display_fn)eglGetProcAddress(
            "eglGetPlatformDisplay");
    if (!get_platform_display)
        get_platform_display =
            (wlcomp_egl_get_platform_display_fn)eglGetProcAddress(
                "eglGetPlatformDisplayEXT");

    if (get_platform_display && client_ext &&
        (strstr(client_ext, "EGL_KHR_platform_gbm") ||
         strstr(client_ext, "EGL_MESA_platform_gbm"))) {
        g_gl_compose.drm_fd =
            open(XV6_DRM_RENDER_NODE, O_RDWR | O_CLOEXEC);
        if (g_gl_compose.drm_fd >= 0) {
            g_gl_compose.gbm = gbm_create_device(g_gl_compose.drm_fd);
            if (g_gl_compose.gbm) {
                display = get_platform_display(EGL_PLATFORM_GBM_KHR,
                                               g_gl_compose.gbm, NULL);
                if (display != EGL_NO_DISPLAY) {
                    fprintf(stderr,
                            "wlcomp: gl-compose using GBM EGL display on %s\n",
                            XV6_DRM_RENDER_NODE);
                    return display;
                }
            }
            if (g_gl_compose.gbm) {
                gbm_device_destroy(g_gl_compose.gbm);
                g_gl_compose.gbm = NULL;
            }
            close(g_gl_compose.drm_fd);
            g_gl_compose.drm_fd = -1;
        }
    }

    if (get_platform_display && client_ext &&
        strstr(client_ext, "EGL_MESA_platform_surfaceless")) {
        fprintf(stderr,
                "wlcomp: gl-compose falling back to surfaceless EGL display\n");
        return get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, NULL, NULL);
    }

    return eglGetDisplay(EGL_DEFAULT_DISPLAY);
}

static GLuint wlcomp_gl_compile_shader(GLenum type, const char *src)
{
    GLuint shader = glCreateShader(type);
    GLint ok = GL_FALSE;

    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[256];

        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "wlcomp: gl-compose shader compile failed: %s\n",
                log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint wlcomp_gl_link_program_from_source(const char *vs,
                                                 const char *fs)
{
    GLuint vshader = wlcomp_gl_compile_shader(GL_VERTEX_SHADER, vs);
    GLuint fshader = wlcomp_gl_compile_shader(GL_FRAGMENT_SHADER, fs);
    GLuint program;
    GLint ok = GL_FALSE;

    if (!vshader || !fshader) {
        if (vshader)
            glDeleteShader(vshader);
        if (fshader)
            glDeleteShader(fshader);
        return 0;
    }
    program = glCreateProgram();
    glAttachShader(program, vshader);
    glAttachShader(program, fshader);
    glBindAttribLocation(program, 0, "a_pos");
    glBindAttribLocation(program, 1, "a_uv");
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    glDeleteShader(vshader);
    glDeleteShader(fshader);
    if (!ok) {
        char log[256];

        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        fprintf(stderr, "wlcomp: gl-compose program link failed: %s\n", log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

static GLuint wlcomp_gl_link_program(void)
{
    static const char *vs =
        "attribute vec2 a_pos;\n"
        "attribute vec2 a_uv;\n"
        "varying vec2 v_uv;\n"
        "void main() {\n"
        "  v_uv = a_uv;\n"
        "  gl_Position = vec4(a_pos, 0.0, 1.0);\n"
        "}\n";
    static const char *fs =
        "precision mediump float;\n"
        "varying vec2 v_uv;\n"
        "uniform sampler2D u_tex;\n"
        "void main() {\n"
        "  gl_FragColor = texture2D(u_tex, v_uv);\n"
        "}\n";

    return wlcomp_gl_link_program_from_source(vs, fs);
}

static GLuint wlcomp_gl_link_external_program(void)
{
    static const char *vs =
        "attribute vec2 a_pos;\n"
        "attribute vec2 a_uv;\n"
        "varying vec2 v_uv;\n"
        "void main() {\n"
        "  v_uv = a_uv;\n"
        "  gl_Position = vec4(a_pos, 0.0, 1.0);\n"
        "}\n";
    static const char *fs =
        "#extension GL_OES_EGL_image_external : require\n"
        "precision mediump float;\n"
        "varying vec2 v_uv;\n"
        "uniform samplerExternalOES u_tex;\n"
        "void main() {\n"
        "  gl_FragColor = texture2D(u_tex, v_uv);\n"
        "}\n";

    return wlcomp_gl_link_program_from_source(vs, fs);
}

static int wlcomp_gl_compose_init(void)
{
    static const EGLint config_attrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    static const EGLint context_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    static const EGLint pbuffer_attrs[] = {
        EGL_WIDTH, 16,
        EGL_HEIGHT, 16,
        EGL_NONE
    };
    static const GLfloat quad[] = {
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
    };
    EGLConfig config;
    EGLint nconfigs = 0;
    EGLint major = 0;
    EGLint minor = 0;
    const char *display_ext;
    int use_surfaceless = 0;
    const char *renderer;
    const char *gl_ext;

    if (!wlcomp_gl_compose_enabled())
        return 0;
    if (g_gl_compose.initialized)
        return g_gl_compose.available;

    g_gl_compose.initialized = 1;
    g_gl_compose.display = EGL_NO_DISPLAY;
    g_gl_compose.context = EGL_NO_CONTEXT;
    g_gl_compose.surface = EGL_NO_SURFACE;
    g_gl_compose.drm_fd = -1;

    setenv("LIBGL_ALWAYS_SOFTWARE", "0", 0);
    setenv("LIBGL_DRIVERS_PATH", "/lib/dri", 0);

    g_gl_compose.display = wlcomp_gl_get_display();
    if (g_gl_compose.display == EGL_NO_DISPLAY) {
        fprintf(stderr,
                "wlcomp: gl-compose init failed: no EGL display error=0x%x\n",
                eglGetError());
        return 0;
    }
    if (!eglInitialize(g_gl_compose.display, &major, &minor)) {
        fprintf(stderr,
                "wlcomp: gl-compose init failed: eglInitialize error=0x%x\n",
                eglGetError());
        return 0;
    }
    fprintf(stderr, "wlcomp: gl-compose eglInitialize ok EGL %d.%d\n",
            major, minor);

    display_ext = eglQueryString(g_gl_compose.display, EGL_EXTENSIONS);
    if (display_ext &&
        (strstr(display_ext, "EGL_KHR_no_config_context") ||
         strstr(display_ext, "EGL_MESA_configless_context")) &&
        strstr(display_ext, "EGL_KHR_surfaceless_context")) {
        config = EGL_NO_CONFIG_KHR;
        use_surfaceless = 1;
        fprintf(stderr,
                "wlcomp: gl-compose using configless surfaceless GLES context\n");
    } else {
        if (!eglChooseConfig(g_gl_compose.display, config_attrs, &config, 1,
                             &nconfigs)) {
            fprintf(stderr,
                    "wlcomp: gl-compose init failed: eglChooseConfig error=0x%x\n",
                    eglGetError());
            return 0;
        }
        if (nconfigs < 1) {
            fprintf(stderr,
                    "wlcomp: gl-compose init failed: eglChooseConfig returned no configs extensions=%s\n",
                    display_ext ? display_ext : "(null)");
            return 0;
        }
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        fprintf(stderr,
                "wlcomp: gl-compose init failed: eglBindAPI error=0x%x\n",
                eglGetError());
        return 0;
    }

    g_gl_compose.context =
        eglCreateContext(g_gl_compose.display, config, EGL_NO_CONTEXT,
                         context_attrs);
    if (!use_surfaceless) {
        g_gl_compose.surface =
            eglCreatePbufferSurface(g_gl_compose.display, config,
                                    pbuffer_attrs);
    }
    if (g_gl_compose.context == EGL_NO_CONTEXT ||
        (!use_surfaceless && g_gl_compose.surface == EGL_NO_SURFACE) ||
        !eglMakeCurrent(g_gl_compose.display,
                        use_surfaceless ? EGL_NO_SURFACE :
                                          g_gl_compose.surface,
                        use_surfaceless ? EGL_NO_SURFACE :
                                          g_gl_compose.surface,
                        g_gl_compose.context)) {
        fprintf(stderr,
                "wlcomp: gl-compose init failed during context setup error=0x%x\n",
                eglGetError());
        return 0;
    }

    if (!display_ext ||
        !strstr(display_ext, "EGL_EXT_image_dma_buf_import")) {
        fprintf(stderr,
                "wlcomp: gl-compose unavailable: EGL_EXT_image_dma_buf_import missing extensions=%s\n",
                display_ext ? display_ext : "(null)");
        return 0;
    }

    g_gl_compose.create_image =
        (wlcomp_egl_create_image_khr_fn)eglGetProcAddress(
            "eglCreateImageKHR");
    if (!g_gl_compose.create_image)
        g_gl_compose.create_image =
            (wlcomp_egl_create_image_khr_fn)eglGetProcAddress(
                "eglCreateImage");
    g_gl_compose.destroy_image =
        (wlcomp_egl_destroy_image_khr_fn)eglGetProcAddress(
            "eglDestroyImageKHR");
    if (!g_gl_compose.destroy_image)
        g_gl_compose.destroy_image =
            (wlcomp_egl_destroy_image_khr_fn)eglGetProcAddress(
                "eglDestroyImage");
    g_gl_compose.image_target_texture_2d =
        (wlcomp_gl_image_target_texture_2d_fn)eglGetProcAddress(
            "glEGLImageTargetTexture2DOES");
    if (!g_gl_compose.create_image || !g_gl_compose.destroy_image ||
        !g_gl_compose.image_target_texture_2d) {
        fprintf(stderr,
                "wlcomp: gl-compose unavailable: EGL image entrypoints missing create=%p destroy=%p target=%p\n",
                (void *)g_gl_compose.create_image,
                (void *)g_gl_compose.destroy_image,
                (void *)g_gl_compose.image_target_texture_2d);
        return 0;
    }

    g_gl_compose.program = wlcomp_gl_link_program();
    if (!g_gl_compose.program)
        return 0;
    g_gl_compose.program_tex_uniform =
        glGetUniformLocation(g_gl_compose.program, "u_tex");
    g_gl_compose.client_texture_target = GL_TEXTURE_2D;
    gl_ext = (const char *)glGetString(GL_EXTENSIONS);
    if (gl_ext && strstr(gl_ext, "GL_OES_EGL_image_external")) {
        g_gl_compose.external_program = wlcomp_gl_link_external_program();
        if (g_gl_compose.external_program) {
            g_gl_compose.client_texture_target = GL_TEXTURE_EXTERNAL_OES;
            g_gl_compose.external_program_tex_uniform =
                glGetUniformLocation(g_gl_compose.external_program, "u_tex");
            fprintf(stderr,
                    "wlcomp: gl-compose using GL_TEXTURE_EXTERNAL_OES for client dmabufs\n");
        }
    }
    glGenBuffers(1, &g_gl_compose.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_gl_compose.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glGenFramebuffers(1, &g_gl_compose.fbo);
    glGenTextures(1, &g_gl_compose.color_tex);
    glBindTexture(GL_TEXTURE_2D, g_gl_compose.color_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    glBindFramebuffer(GL_FRAMEBUFFER, g_gl_compose.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, g_gl_compose.color_tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "wlcomp: gl-compose unavailable: FBO incomplete\n");
        return 0;
    }

    renderer = (const char *)glGetString(GL_RENDERER);
    fprintf(stderr,
            "wlcomp: gl-compose initialized EGL %d.%d renderer=%s extensions_dma_buf=1\n",
            major, minor, renderer ? renderer : "(null)");
    g_gl_compose.available = 1;
    return 1;
}

static void wlcomp_gl_compose_destroy_buffer(struct wlcomp_buffer *buf)
{
    if (!buf)
        return;
    wlcomp_gl_compose_clear_release_sync(buf);
    if (!g_gl_compose.initialized || !g_gl_compose.available)
        return;
    if (!eglMakeCurrent(g_gl_compose.display, g_gl_compose.surface,
                        g_gl_compose.surface, g_gl_compose.context))
        return;
    if (buf->gl_texture) {
        GLuint tex = (GLuint)buf->gl_texture;

        glDeleteTextures(1, &tex);
        buf->gl_texture = 0;
    }
    if (buf->gl_egl_image) {
        g_gl_compose.destroy_image(g_gl_compose.display,
                                   (EGLImageKHR)buf->gl_egl_image);
        buf->gl_egl_image = NULL;
    }
}

static void wlcomp_gl_compose_destroy_target_slot(int index)
{
    struct wlcomp_gl_compose_target *target;

    if (index < 0 || index >= WLCOMP_GL_COMPOSE_TARGET_CACHE_SIZE)
        return;
    if (!g_gl_compose.initialized || !g_gl_compose.available)
        return;
    if (!eglMakeCurrent(g_gl_compose.display, g_gl_compose.surface,
                        g_gl_compose.surface, g_gl_compose.context))
        return;
    target = &g_gl_compose.target_cache[index];
    if (target->texture) {
        if (g_gl_compose.fbo_attached_texture == target->texture) {
            g_gl_compose.fbo_attached_texture = 0;
            g_gl_compose.fbo_complete = 0;
        }
        glDeleteTextures(1, &target->texture);
        target->texture = 0;
    }
    if (target->egl_image) {
        g_gl_compose.destroy_image(g_gl_compose.display,
            (EGLImageKHR)target->egl_image);
        target->egl_image = NULL;
    }
    if (g_gl_compose.target_handle == target->handle) {
        g_gl_compose.target_texture = 0;
        g_gl_compose.target_egl_image = NULL;
        g_gl_compose.target_handle = 0;
        g_gl_compose.target_width = 0;
        g_gl_compose.target_height = 0;
    }
    target->handle = 0;
    target->width = 0;
    target->height = 0;
}

static void wlcomp_gl_compose_destroy_target(void)
{
    for (int i = 0; i < WLCOMP_GL_COMPOSE_TARGET_CACHE_SIZE; i++)
        wlcomp_gl_compose_destroy_target_slot(i);
    g_gl_compose.target_handle = 0;
    g_gl_compose.target_width = 0;
    g_gl_compose.target_height = 0;
    g_gl_compose.target_texture = 0;
    g_gl_compose.target_egl_image = NULL;
    g_gl_compose.fbo_attached_texture = 0;
    g_gl_compose.fbo_complete = 0;
}

static int wlcomp_gl_compose_import_target(void)
{
    struct fb_gpu_bo_export_fd export_fd;
    EGLImageKHR image;
    EGLint attrs[32];
    int attr = 0;
    int fd = -1;
    GLenum glerr;
    int target_slot = -1;
    static int next_target_slot;
    static int target_logs;

    if (!g_fb_virgl_backed || g_fb_bo_handle == 0 ||
        g_fb_w == 0 || g_fb_h == 0 || g_fb_pitch == 0)
        return 0;
    if (!wlcomp_gl_compose_init())
        return 0;
    if (!eglMakeCurrent(g_gl_compose.display, g_gl_compose.surface,
                        g_gl_compose.surface, g_gl_compose.context))
        return 0;

    if (g_gl_compose.target_texture &&
        g_gl_compose.target_handle == g_fb_bo_handle &&
        g_gl_compose.target_width == g_fb_w &&
        g_gl_compose.target_height == g_fb_h)
        return 1;

    for (int i = 0; i < WLCOMP_GL_COMPOSE_TARGET_CACHE_SIZE; i++) {
        struct wlcomp_gl_compose_target *target =
            &g_gl_compose.target_cache[i];

        if (target->texture && target->handle == g_fb_bo_handle &&
            target->width == g_fb_w && target->height == g_fb_h) {
            g_gl_compose.target_texture = target->texture;
            g_gl_compose.target_egl_image = target->egl_image;
            g_gl_compose.target_handle = target->handle;
            g_gl_compose.target_width = target->width;
            g_gl_compose.target_height = target->height;
            return 1;
        }
    }

    for (int i = 0; i < WLCOMP_GL_COMPOSE_TARGET_CACHE_SIZE; i++) {
        if (!g_gl_compose.target_cache[i].texture) {
            target_slot = i;
            break;
        }
    }
    if (target_slot < 0) {
        target_slot = next_target_slot;
        next_target_slot =
            (next_target_slot + 1) % WLCOMP_GL_COMPOSE_TARGET_CACHE_SIZE;
        wlcomp_gl_compose_destroy_target_slot(target_slot);
    }

    memset(&export_fd, 0, sizeof(export_fd));
    export_fd.handle = g_fb_bo_handle;
    export_fd.fd = -1;
    if (ioctl(g_fb_fd, FB_GPU_BO_EXPORT_FD, &export_fd) != 0 ||
        export_fd.fd < 0) {
        if (target_logs < 8) {
            fprintf(stderr,
                    "wlcomp: gl-compose target export failed handle=%u errno=%d (%s)\n",
                    g_fb_bo_handle, errno, strerror(errno));
            target_logs++;
        }
        return 0;
    }
    fd = export_fd.fd;

    attrs[attr++] = EGL_WIDTH;
    attrs[attr++] = (EGLint)g_fb_w;
    attrs[attr++] = EGL_HEIGHT;
    attrs[attr++] = (EGLint)g_fb_h;
    attrs[attr++] = EGL_LINUX_DRM_FOURCC_EXT;
    attrs[attr++] = (EGLint)DRM_FORMAT_XRGB8888;
    attrs[attr++] = EGL_DMA_BUF_PLANE0_FD_EXT;
    attrs[attr++] = fd;
    attrs[attr++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
    attrs[attr++] = 0;
    attrs[attr++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
    attrs[attr++] = (EGLint)g_fb_pitch;
    attrs[attr++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
    attrs[attr++] = 0;
    attrs[attr++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
    attrs[attr++] = 0;
    attrs[attr++] = EGL_NONE;

    image = g_gl_compose.create_image(g_gl_compose.display, EGL_NO_CONTEXT,
                                      EGL_LINUX_DMA_BUF_EXT, NULL, attrs);
    close(fd);
    fd = -1;
    if (image == EGL_NO_IMAGE_KHR) {
        if (target_logs < 8) {
            fprintf(stderr,
                    "wlcomp: gl-compose target eglCreateImage failed handle=%u size=%ux%u pitch=%u error=0x%x\n",
                    g_fb_bo_handle, g_fb_w, g_fb_h, g_fb_pitch,
                    eglGetError());
            target_logs++;
        }
        return 0;
    }

    glGenTextures(1, &g_gl_compose.target_texture);
    glBindTexture(GL_TEXTURE_2D, g_gl_compose.target_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    g_gl_compose.image_target_texture_2d(GL_TEXTURE_2D,
                                         (GLeglImageOES)image);
    glerr = glGetError();
    if (glerr != GL_NO_ERROR) {
        if (target_logs < 8) {
            fprintf(stderr,
                    "wlcomp: gl-compose target texture import failed error=0x%x\n",
                    glerr);
            target_logs++;
        }
        glDeleteTextures(1, &g_gl_compose.target_texture);
        g_gl_compose.target_texture = 0;
        g_gl_compose.destroy_image(g_gl_compose.display, image);
        return 0;
    }

    g_gl_compose.target_egl_image = image;
    g_gl_compose.target_handle = g_fb_bo_handle;
    g_gl_compose.target_width = g_fb_w;
    g_gl_compose.target_height = g_fb_h;
    g_gl_compose.target_cache[target_slot].egl_image = image;
    g_gl_compose.target_cache[target_slot].texture = g_gl_compose.target_texture;
    g_gl_compose.target_cache[target_slot].handle = g_fb_bo_handle;
    g_gl_compose.target_cache[target_slot].width = g_fb_w;
    g_gl_compose.target_cache[target_slot].height = g_fb_h;
    next_target_slot =
        (target_slot + 1) % WLCOMP_GL_COMPOSE_TARGET_CACHE_SIZE;
    if (target_logs < 8) {
        fprintf(stderr,
                "wlcomp: gl-compose target import ok handle=%u tex=%u size=%ux%u pitch=%u res=%u\n",
                g_fb_bo_handle, g_gl_compose.target_texture, g_fb_w, g_fb_h,
                g_fb_pitch, g_fb_virgl_resource_id);
        target_logs++;
    }
    return 1;
}

static int wlcomp_gl_compose_probe_buffer(struct wlcomp_buffer *buf)
{
    static const GLfloat probe_quad[] = {
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
    };
    struct fb_gpu_bo_export_fd export_fd;
    EGLImageKHR image;
    EGLint attrs[32];
    int fd = -1;
    int attr = 0;
    GLenum glerr;
    uint32_t sample[16];
    uint64_t checksum = 0;
    uint32_t handle;
    uint32_t format;
    int imported_original_fd = 0;

    if (!wlcomp_gl_compose_enabled() || !buf || !buf->is_gpu_bo)
        return 0;
    if (buf->gl_import_tried && buf->gl_import_ready)
        return buf->gl_import_ready;
    /*
     * Import failure can be transient on virgl/d3d12 while the producer is
     * under pressure.  Cache successful imports, but keep failures retryable;
     * otherwise one missed EGLImage import latches this wl_buffer onto the
     * CPU path until the client destroys it.
     */
    if (buf->gl_import_tried && !buf->gl_import_ready)
        buf->gl_import_tried = 0;
    buf->gl_import_tried = 1;

    if (!wlcomp_gl_compose_init())
        goto fail_retry;
    if (!eglMakeCurrent(g_gl_compose.display, g_gl_compose.surface,
                        g_gl_compose.surface, g_gl_compose.context))
        goto fail_retry;

    handle = buf->plane_gpu_handle[0] ? buf->plane_gpu_handle[0] :
                                        buf->gpu_handle;
    if (!handle || buf->plane_stride[0] <= 0)
        goto fail_retry;

    if (buf->plane_dmabuf_fd[0] >= 0) {
        fd = dup(buf->plane_dmabuf_fd[0]);
        if (fd >= 0)
            imported_original_fd = 1;
    }
    if (fd < 0) {
        memset(&export_fd, 0, sizeof(export_fd));
        export_fd.handle = handle;
        export_fd.fd = -1;
        if (ioctl(g_fb_fd, FB_GPU_BO_EXPORT_FD, &export_fd) != 0 ||
            export_fd.fd < 0) {
            if (g_gl_compose.import_logs < 8) {
                fprintf(stderr,
                        "wlcomp: gl-compose export fd failed handle=%u errno=%d (%s)\n",
                        handle, errno, strerror(errno));
                g_gl_compose.import_logs++;
            }
            goto fail_retry;
        }
        fd = export_fd.fd;
    }

    format = buf->format;
    if (format == WL_SHM_FORMAT_ARGB8888)
        format = DRM_FORMAT_ARGB8888;
    else if (format == WL_SHM_FORMAT_XRGB8888)
        format = DRM_FORMAT_XRGB8888;

    attrs[attr++] = EGL_WIDTH;
    attrs[attr++] = buf->width;
    attrs[attr++] = EGL_HEIGHT;
    attrs[attr++] = buf->height;
    attrs[attr++] = EGL_LINUX_DRM_FOURCC_EXT;
    attrs[attr++] = (EGLint)format;
    attrs[attr++] = EGL_DMA_BUF_PLANE0_FD_EXT;
    attrs[attr++] = fd;
    attrs[attr++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
    attrs[attr++] = buf->plane_offset[0] >= 0 ? buf->plane_offset[0] : 0;
    attrs[attr++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
    attrs[attr++] = buf->plane_stride[0];
    attrs[attr++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
    attrs[attr++] = 0;
    attrs[attr++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
    attrs[attr++] = 0;
    attrs[attr++] = EGL_NONE;

    image = g_gl_compose.create_image(g_gl_compose.display, EGL_NO_CONTEXT,
                                      EGL_LINUX_DMA_BUF_EXT, NULL, attrs);
    close(fd);
    fd = -1;
    if (image == EGL_NO_IMAGE_KHR) {
        if (g_gl_compose.import_logs < 8) {
            fprintf(stderr,
                    "wlcomp: gl-compose eglCreateImage dmabuf failed handle=%u size=%dx%d stride=%d fmt=0x%x virgl_res=%u error=0x%x\n",
                    handle, buf->width, buf->height, buf->plane_stride[0],
                    format, buf->plane_virtio_resource_id[0], eglGetError());
            g_gl_compose.import_logs++;
        }
        goto fail_retry;
    }

    glGenTextures(1, (GLuint *)&buf->gl_texture);
    glBindTexture(g_gl_compose.client_texture_target,
                  (GLuint)buf->gl_texture);
    glTexParameteri(g_gl_compose.client_texture_target,
                    GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(g_gl_compose.client_texture_target,
                    GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(g_gl_compose.client_texture_target,
                    GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(g_gl_compose.client_texture_target,
                    GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    g_gl_compose.image_target_texture_2d(g_gl_compose.client_texture_target,
                                         (GLeglImageOES)image);
    glerr = glGetError();
    if (glerr != GL_NO_ERROR) {
        fprintf(stderr,
                "wlcomp: gl-compose glEGLImageTargetTexture2DOES failed error=0x%x\n",
                glerr);
        glDeleteTextures(1, (GLuint *)&buf->gl_texture);
        buf->gl_texture = 0;
        g_gl_compose.destroy_image(g_gl_compose.display, image);
        goto fail_retry;
    }

    if (wlcomp_gl_compose_checksum_enabled()) {
        glBindFramebuffer(GL_FRAMEBUFFER, g_gl_compose.fbo);
        glViewport(0, 0, 16, 16);
        glUseProgram(g_gl_compose.client_texture_target ==
                         GL_TEXTURE_EXTERNAL_OES ?
                         g_gl_compose.external_program :
                         g_gl_compose.program);
        glBindBuffer(GL_ARRAY_BUFFER, g_gl_compose.vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(probe_quad), probe_quad,
                     GL_STREAM_DRAW);
        g_gl_compose.last_quad_valid = 0;
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat),
                              (const void *)0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat),
                              (const void *)(2 * sizeof(GLfloat)));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(g_gl_compose.client_texture_target,
                      (GLuint)buf->gl_texture);
        glUniform1i(glGetUniformLocation(
                        g_gl_compose.client_texture_target ==
                            GL_TEXTURE_EXTERNAL_OES ?
                            g_gl_compose.external_program :
                            g_gl_compose.program,
                        "u_tex"), 0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        memset(sample, 0, sizeof(sample));
        glReadPixels(0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, sample);
        glFinish();
        for (uint32_t i = 0; i < 16; i++)
            checksum = checksum * 1315423911u + sample[i];
    }

    buf->gl_egl_image = image;
    buf->gl_import_ready = 1;
    if (g_gl_compose.import_logs < 16) {
        if (wlcomp_gl_compose_checksum_enabled()) {
            fprintf(stderr,
                    "wlcomp: gl-compose dmabuf import ok handle=%u tex=%u source=%s size=%dx%d stride=%d fmt=0x%x virgl_res=%u checksum=0x%lx renderer='%s'\n",
                    handle, buf->gl_texture,
                    imported_original_fd ? "client-fd" : "bo-export",
                    buf->width, buf->height,
                    buf->plane_stride[0], format,
                    buf->plane_virtio_resource_id[0],
                    (unsigned long)checksum,
                    glGetString(GL_RENDERER) ?
                        (const char *)glGetString(GL_RENDERER) : "(null)");
        } else {
            fprintf(stderr,
                    "wlcomp: gl-compose dmabuf import ok handle=%u tex=%u source=%s size=%dx%d stride=%d fmt=0x%x virgl_res=%u renderer='%s'\n",
                    handle, buf->gl_texture,
                    imported_original_fd ? "client-fd" : "bo-export",
                    buf->width, buf->height,
                    buf->plane_stride[0], format,
                    buf->plane_virtio_resource_id[0],
                    glGetString(GL_RENDERER) ?
                        (const char *)glGetString(GL_RENDERER) : "(null)");
        }
        g_gl_compose.import_logs++;
    }
    return 1;

fail_retry:
    if (fd >= 0)
        close(fd);
    buf->gl_import_tried = 0;
    buf->gl_import_ready = 0;
    return 0;
}

static int wlcomp_gl_compose_present_buffer(struct wlcomp_buffer *buf,
                                            int32_t draw_x, int32_t draw_y,
                                            int32_t row0, int32_t row1,
                                            int32_t col0, int32_t col1)
{
    GLfloat quad[16];
    int32_t dst_x0;
    int32_t dst_y0;
    int32_t dst_x1;
    int32_t dst_y1;
    int32_t src_x0;
    int32_t src_y0;
    int32_t src_x1;
    int32_t src_y1;
    int32_t gl_y0;
    int32_t gl_y1;
    GLfloat left, right, top, bottom;
    GLfloat u0, u1, v0, v1;
    GLenum status;
    static int present_logs;

    if (!g_fb_virgl_backed || !buf || !buf->is_gpu_bo ||
        buf->y_inverted || row0 >= row1 || col0 >= col1 ||
        buf->width <= 0 || buf->height <= 0)
        return 0;
    if (wlcomp_gl_compose_reimport_each_present() &&
        buf->plane_dmabuf_fd[0] >= 0 && buf->gl_import_ready) {
        wlcomp_gl_compose_destroy_buffer(buf);
        buf->gl_import_tried = 0;
        buf->gl_import_ready = 0;
    }
    if (!wlcomp_gl_compose_probe_buffer(buf) ||
        !wlcomp_gl_compose_import_target())
        return 0;
    if (!eglMakeCurrent(g_gl_compose.display, g_gl_compose.surface,
                        g_gl_compose.surface, g_gl_compose.context))
        return 0;

    src_x0 = col0;
    src_y0 = row0;
    src_x1 = col1;
    src_y1 = row1;
    dst_x0 = draw_x + col0;
    dst_y0 = draw_y + row0;
    dst_x1 = draw_x + col1;
    dst_y1 = draw_y + row1;

    if (dst_x0 < 0) {
        src_x0 += -dst_x0;
        dst_x0 = 0;
    }
    if (dst_y0 < 0) {
        src_y0 += -dst_y0;
        dst_y0 = 0;
    }
    if (dst_x1 > (int32_t)g_fb_w) {
        src_x1 -= dst_x1 - (int32_t)g_fb_w;
        dst_x1 = (int32_t)g_fb_w;
    }
    if (dst_y1 > (int32_t)g_fb_h) {
        src_y1 -= dst_y1 - (int32_t)g_fb_h;
        dst_y1 = (int32_t)g_fb_h;
    }
    if (dst_x0 >= dst_x1 || dst_y0 >= dst_y1 ||
        src_x0 >= src_x1 || src_y0 >= src_y1)
        return 0;

    if (wlcomp_gl_compose_target_top_left()) {
        gl_y0 = dst_y0;
        gl_y1 = dst_y1;
    } else {
        gl_y0 = (int32_t)g_fb_h - dst_y1;
        gl_y1 = (int32_t)g_fb_h - dst_y0;
    }

    left = -1.0f + 2.0f * (GLfloat)dst_x0 / (GLfloat)g_fb_w;
    right = -1.0f + 2.0f * (GLfloat)dst_x1 / (GLfloat)g_fb_w;
    bottom = -1.0f + 2.0f * (GLfloat)gl_y0 / (GLfloat)g_fb_h;
    top = -1.0f + 2.0f * (GLfloat)gl_y1 / (GLfloat)g_fb_h;
    u0 = (GLfloat)src_x0 / (GLfloat)buf->width;
    u1 = (GLfloat)src_x1 / (GLfloat)buf->width;
    v0 = (GLfloat)src_y0 / (GLfloat)buf->height;
    v1 = (GLfloat)src_y1 / (GLfloat)buf->height;

    if (wlcomp_gl_compose_source_top_left()) {
        quad[0] = left;  quad[1] = bottom; quad[2] = u0; quad[3] = v0;
        quad[4] = right; quad[5] = bottom; quad[6] = u1; quad[7] = v0;
        quad[8] = left;  quad[9] = top;    quad[10] = u0; quad[11] = v1;
        quad[12] = right; quad[13] = top;  quad[14] = u1; quad[15] = v1;
    } else {
        quad[0] = left;  quad[1] = bottom; quad[2] = u0; quad[3] = v1;
        quad[4] = right; quad[5] = bottom; quad[6] = u1; quad[7] = v1;
        quad[8] = left;  quad[9] = top;    quad[10] = u0; quad[11] = v0;
        quad[12] = right; quad[13] = top;  quad[14] = u1; quad[15] = v0;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, g_gl_compose.fbo);
    if (g_gl_compose.fbo_attached_texture != g_gl_compose.target_texture) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D,
                               g_gl_compose.target_texture, 0);
        status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            g_gl_compose.fbo_attached_texture = 0;
            g_gl_compose.fbo_complete = 0;
            if (present_logs < 8) {
                fprintf(stderr,
                        "wlcomp: gl-compose target FBO incomplete status=0x%x\n",
                        status);
                present_logs++;
            }
            return 0;
        }
        g_gl_compose.fbo_attached_texture = g_gl_compose.target_texture;
        g_gl_compose.fbo_complete = 1;
    } else if (!g_gl_compose.fbo_complete) {
        return 0;
    }
    glViewport(0, 0, (GLsizei)g_fb_w, (GLsizei)g_fb_h);
    glDisable(GL_BLEND);
    GLuint program = g_gl_compose.client_texture_target ==
                         GL_TEXTURE_EXTERNAL_OES ?
                         g_gl_compose.external_program : g_gl_compose.program;
    GLint tex_uniform = g_gl_compose.client_texture_target ==
                            GL_TEXTURE_EXTERNAL_OES ?
                            g_gl_compose.external_program_tex_uniform :
                            g_gl_compose.program_tex_uniform;

    glUseProgram(program);
    glBindBuffer(GL_ARRAY_BUFFER, g_gl_compose.vbo);
    if (!g_gl_compose.last_quad_valid ||
        memcmp(g_gl_compose.last_quad, quad, sizeof(quad)) != 0) {
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STREAM_DRAW);
        memcpy(g_gl_compose.last_quad, quad, sizeof(quad));
        g_gl_compose.last_quad_valid = 1;
    }
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat),
                          (const void *)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat),
                          (const void *)(2 * sizeof(GLfloat)));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(g_gl_compose.client_texture_target,
                  (GLuint)buf->gl_texture);
    glUniform1i(tex_uniform, 0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    /*
     * Match the Linux compositor shape by default: queue the GL draw and let
     * the following virtio scanout flush preserve ordering.  Per-present
     * glFlush is kept as a diagnostic fallback for hosts that need an explicit
     * compositor submit before RESOURCE_FLUSH.
     */
    if (wlcomp_gl_compose_finish_each_present())
        glFinish();
    else if (wlcomp_gl_compose_flush_each_present() &&
             !wlcomp_gl_compose_flush_after_scanout())
        glFlush();
    else if (wlcomp_gl_compose_submit_fence_enabled())
        wlcomp_gl_compose_mark_buffer_release_sync(buf);
    else
        wlcomp_gl_compose_wait_draw_fence();
    wlcomp_gl_compose_capture_rect(dst_x0, gl_y0,
                                   dst_x1 - dst_x0, gl_y1 - gl_y0);

    g_damage_stats.gpu_compose_presents++;
    g_damage_stats.gpu_compose_pixels +=
        (uint64_t)(dst_x1 - dst_x0) * (uint32_t)(dst_y1 - dst_y0);
    if (present_logs < 8) {
        uint64_t checksum = 0;
        int checksum_enabled = wlcomp_gl_compose_checksum_enabled();

        if (checksum_enabled) {
            uint32_t sample[16];
            int sample_x = dst_x0 + (dst_x1 - dst_x0) / 2 - 2;
            int sample_y = gl_y0 + (gl_y1 - gl_y0) / 2 - 2;

            if (sample_x < 0)
                sample_x = 0;
            if (sample_y < 0)
                sample_y = 0;
            if (sample_x + 4 > (int32_t)g_fb_w)
                sample_x = (int32_t)g_fb_w - 4;
            if (sample_y + 4 > (int32_t)g_fb_h)
                sample_y = (int32_t)g_fb_h - 4;
            if (sample_x >= 0 && sample_y >= 0) {
                memset(sample, 0, sizeof(sample));
                glReadPixels(sample_x, sample_y, 4, 4, GL_RGBA,
                             GL_UNSIGNED_BYTE, sample);
                for (uint32_t i = 0; i < 16; i++)
                    checksum = checksum * 1315423911u + sample[i];
            }
        }
        if (checksum_enabled) {
            fprintf(stderr,
                    "wlcomp: gl-compose drew handle=%u target=%u rect=%d,%d %dx%d src=%d,%d..%d,%d target_origin=%s source_origin=%s target_checksum=0x%lx\n",
                    buf->plane_gpu_handle[0] ? buf->plane_gpu_handle[0] :
                                               buf->gpu_handle,
                    g_fb_bo_handle, dst_x0, dst_y0, dst_x1 - dst_x0,
                    dst_y1 - dst_y0, src_x0, src_y0, src_x1, src_y1,
                    wlcomp_gl_compose_target_top_left() ? "top-left" :
                                                          "bottom-left",
                    wlcomp_gl_compose_source_top_left() ? "top-left" :
                                                          "bottom-left",
                    (unsigned long)checksum);
        } else {
            fprintf(stderr,
                    "wlcomp: gl-compose drew handle=%u target=%u rect=%d,%d %dx%d src=%d,%d..%d,%d target_origin=%s source_origin=%s\n",
                    buf->plane_gpu_handle[0] ? buf->plane_gpu_handle[0] :
                                               buf->gpu_handle,
                    g_fb_bo_handle, dst_x0, dst_y0, dst_x1 - dst_x0,
                    dst_y1 - dst_y0, src_x0, src_y0, src_x1, src_y1,
                    wlcomp_gl_compose_target_top_left() ? "top-left" :
                                                          "bottom-left",
                    wlcomp_gl_compose_source_top_left() ? "top-left" :
                                                          "bottom-left");
        }
        present_logs++;
    }
    return 1;
}
