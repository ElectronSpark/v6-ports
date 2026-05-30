/*
 * mesaglfeature.c - focused Mesa EGL/GLES feature smoke probe.
 *
 * This is intentionally small but broader than a clear/readback smoke.  It
 * exercises shader compile/link, VBOs, textures, FBO color/depth/stencil
 * attachments, viewport/scissor, blending, resize, and teardown.
 */

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif
#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8 0x88F0
#endif

static EGLDisplay get_surfaceless_display(void)
{
    typedef EGLDisplay (*get_platform_display_fn)(EGLenum, void *,
                                                  const EGLAttrib *);
    const char *client_ext = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    get_platform_display_fn get_platform_display =
        (get_platform_display_fn)eglGetProcAddress("eglGetPlatformDisplay");

    if (get_platform_display && client_ext &&
        strstr(client_ext, "EGL_MESA_platform_surfaceless"))
        return get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, NULL, NULL);

    get_platform_display =
        (get_platform_display_fn)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (get_platform_display && client_ext &&
        strstr(client_ext, "EGL_MESA_platform_surfaceless"))
        return get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, NULL, NULL);

    return eglGetDisplay(EGL_DEFAULT_DISPLAY);
}

static int check_gl(const char *where)
{
    GLenum err = glGetError();

    if (err != GL_NO_ERROR) {
        fprintf(stderr, "mesaglfeature: GL error 0x%x at %s\n", err, where);
        return 1;
    }
    return 0;
}

static int env_is(const char *name, const char *value)
{
    const char *env = getenv(name);

    return env && strcmp(env, value) == 0;
}

static int env_truthy(const char *name)
{
    const char *env = getenv(name);

    return env && env[0] && strcmp(env, "0") != 0;
}

static void report_runtime_file(const char *label, const char *path)
{
    int rc = access(path, F_OK);

    fprintf(stderr,
            "mesaglfeature: runtime path %s=%s present=%d errno=%d\n",
            label, path, rc == 0, rc == 0 ? 0 : errno);
}

static void report_runtime_env(void)
{
    fprintf(stderr,
            "mesaglfeature: env GALLIUM_DRIVER=%s MESA_LOADER_DRIVER_OVERRIDE=%s LIBGL_DRIVERS_PATH=%s LD_LIBRARY_PATH=%s LIBGL_ALWAYS_SOFTWARE=%s MESA_D3D12_DEFAULT_ADAPTER_NAME=%s\n",
            getenv("GALLIUM_DRIVER") ? getenv("GALLIUM_DRIVER") : "(unset)",
            getenv("MESA_LOADER_DRIVER_OVERRIDE") ?
                getenv("MESA_LOADER_DRIVER_OVERRIDE") : "(unset)",
            getenv("LIBGL_DRIVERS_PATH") ? getenv("LIBGL_DRIVERS_PATH") :
                "(unset)",
            getenv("LD_LIBRARY_PATH") ? getenv("LD_LIBRARY_PATH") :
                "(unset)",
            getenv("LIBGL_ALWAYS_SOFTWARE") ?
                getenv("LIBGL_ALWAYS_SOFTWARE") : "(unset)",
            getenv("MESA_D3D12_DEFAULT_ADAPTER_NAME") ?
                getenv("MESA_D3D12_DEFAULT_ADAPTER_NAME") : "(unset)");
    report_runtime_file("d3d12_dri", "/lib/dri/d3d12_dri.so");
    report_runtime_file("d3d12_dri_multiarch",
                        "/usr/lib/x86_64-linux-gnu/dri/d3d12_dri.so");
    report_runtime_file("dxcore", "/lib/libdxcore.so");
    report_runtime_file("d3d12", "/lib/libd3d12.so");
}

static int contains_ci(const char *haystack, const char *needle)
{
    size_t needle_len;

    if (!haystack || !needle)
        return 0;
    needle_len = strlen(needle);
    if (needle_len == 0)
        return 1;

    for (const char *p = haystack; *p; p++) {
        size_t i;

        for (i = 0; i < needle_len; i++) {
            char a = p[i];
            char b = needle[i];

            if (!a)
                return 0;
            if (a >= 'A' && a <= 'Z')
                a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z')
                b = (char)(b - 'A' + 'a');
            if (a != b)
                break;
        }
        if (i == needle_len)
            return 1;
    }
    return 0;
}

static int require_d3d12_renderer(const char *renderer)
{
    if (!env_truthy("XV6_MESAGLFEATURE_REQUIRE_D3D12") &&
        !env_is("GALLIUM_DRIVER", "d3d12") &&
        !env_is("MESA_LOADER_DRIVER_OVERRIDE", "d3d12"))
        return 0;

    if (!renderer ||
        contains_ci(renderer, "softpipe") ||
        contains_ci(renderer, "llvmpipe") ||
        !contains_ci(renderer, "d3d12")) {
        fprintf(stderr,
                "mesaglfeature: required D3D12 renderer but got renderer=%s GALLIUM_DRIVER=%s MESA_LOADER_DRIVER_OVERRIDE=%s\n",
                renderer ? renderer : "(null)",
                getenv("GALLIUM_DRIVER") ? getenv("GALLIUM_DRIVER") : "(unset)",
                getenv("MESA_LOADER_DRIVER_OVERRIDE") ?
                    getenv("MESA_LOADER_DRIVER_OVERRIDE") : "(unset)");
        return 1;
    }

    printf("mesaglfeature: D3D12 renderer confirmed renderer=%s\n", renderer);
    return 0;
}

static GLuint compile_shader(GLenum type, const char *src)
{
    GLuint shader = glCreateShader(type);
    GLint ok = 0;
    char log[256];

    fprintf(stderr, "mesaglfeature: glCreateShader type=0x%x -> %u\n",
            type, shader);
    glShaderSource(shader, 1, &src, NULL);
    fprintf(stderr, "mesaglfeature: glShaderSource type=0x%x done\n", type);
    glCompileShader(shader);
    fprintf(stderr, "mesaglfeature: glCompileShader type=0x%x done\n", type);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "mesaglfeature: shader compile failed: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint create_program(void)
{
    static const char *vs =
        "attribute vec3 a_pos;\n"
        "attribute vec2 a_uv;\n"
        "varying vec2 v_uv;\n"
        "void main() {\n"
        "  v_uv = a_uv;\n"
        "  gl_Position = vec4(a_pos, 1.0);\n"
        "}\n";
    static const char *fs =
        "precision mediump float;\n"
        "uniform sampler2D u_tex;\n"
        "varying vec2 v_uv;\n"
        "void main() {\n"
        "  gl_FragColor = texture2D(u_tex, v_uv);\n"
        "}\n";
    GLuint vert = compile_shader(GL_VERTEX_SHADER, vs);
    GLuint frag = compile_shader(GL_FRAGMENT_SHADER, fs);
    GLuint prog;
    GLint ok = 0;
    char log[256];

    if (!vert || !frag)
        return 0;

    prog = glCreateProgram();
    fprintf(stderr, "mesaglfeature: glCreateProgram -> %u\n", prog);
    glAttachShader(prog, vert);
    fprintf(stderr, "mesaglfeature: glAttachShader vert done\n");
    glAttachShader(prog, frag);
    fprintf(stderr, "mesaglfeature: glAttachShader frag done\n");
    glBindAttribLocation(prog, 0, "a_pos");
    glBindAttribLocation(prog, 1, "a_uv");
    fprintf(stderr, "mesaglfeature: glBindAttribLocation done\n");
    glLinkProgram(prog);
    fprintf(stderr, "mesaglfeature: glLinkProgram done\n");
    glDeleteShader(vert);
    glDeleteShader(frag);

    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        fprintf(stderr, "mesaglfeature: program link failed: %s\n", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

static int create_fbo(int width, int height, GLuint *fbo, GLuint *color,
                      GLuint *depth, GLuint *stencil)
{
    fprintf(stderr, "mesaglfeature: create_fbo begin size=%dx%d\n",
            width, height);
    glGenFramebuffers(1, fbo);
    fprintf(stderr, "mesaglfeature: glGenFramebuffers -> %u\n", *fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
    fprintf(stderr, "mesaglfeature: glBindFramebuffer done\n");

    glGenTextures(1, color);
    fprintf(stderr, "mesaglfeature: glGenTextures color -> %u\n", *color);
    glBindTexture(GL_TEXTURE_2D, *color);
    fprintf(stderr, "mesaglfeature: glBindTexture color done\n");
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    fprintf(stderr, "mesaglfeature: glTexParameteri color done\n");
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    fprintf(stderr, "mesaglfeature: glTexImage2D color done\n");
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, *color, 0);
    fprintf(stderr, "mesaglfeature: glFramebufferTexture2D done\n");

    glGenRenderbuffers(1, depth);
    fprintf(stderr, "mesaglfeature: glGenRenderbuffers -> %u\n", *depth);
    glBindRenderbuffer(GL_RENDERBUFFER, *depth);
    fprintf(stderr, "mesaglfeature: glBindRenderbuffer done\n");
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
                          width, height);
    fprintf(stderr, "mesaglfeature: glRenderbufferStorage done\n");
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, *depth);
    fprintf(stderr, "mesaglfeature: glFramebufferRenderbuffer depth done\n");
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, *depth);
    fprintf(stderr, "mesaglfeature: glFramebufferRenderbuffer stencil done\n");
    *stencil = 0;

    fprintf(stderr, "mesaglfeature: glCheckFramebufferStatus begin\n");
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "mesaglfeature: FBO incomplete size=%dx%d\n",
                width, height);
        return 1;
    }
    fprintf(stderr, "mesaglfeature: create_fbo complete size=%dx%d\n",
            width, height);
    return check_gl("create_fbo");
}

static void destroy_fbo(GLuint fbo, GLuint color, GLuint depth, GLuint stencil)
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (stencil)
        glDeleteRenderbuffers(1, &stencil);
    if (depth)
        glDeleteRenderbuffers(1, &depth);
    if (color)
        glDeleteTextures(1, &color);
    if (fbo)
        glDeleteFramebuffers(1, &fbo);
}

static int draw_case(GLuint prog, GLuint vbo, GLuint tex, int width, int height)
{
    GLuint fbo = 0;
    GLuint color = 0;
    GLuint depth = 0;
    GLuint stencil = 0;
    uint8_t center[4] = {0};
    uint8_t corner[4] = {0};
    int sx = width / 4;
    int sy = height / 4;
    int sw = width / 2;
    int sh = height / 2;
    int status = 1;

    if (create_fbo(width, height, &fbo, &color, &depth, &stencil) != 0)
        goto out;

    fprintf(stderr, "mesaglfeature: draw_case begin size=%dx%d\n",
            width, height);
    glViewport(0, 0, width, height);
    fprintf(stderr, "mesaglfeature: glViewport done\n");
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepthf(1.0f);
    glClearStencil(0);
    fprintf(stderr, "mesaglfeature: full clear begin\n");
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    fprintf(stderr, "mesaglfeature: full clear done\n");

    glEnable(GL_SCISSOR_TEST);
    glScissor(sx, sy, sw, sh);
    glClearColor(0.0625f, 0.125f, 0.1875f, 1.0f);
    fprintf(stderr, "mesaglfeature: scissor clear begin rect=%d,%d %dx%d\n",
            sx, sy, sw, sh);
    glClear(GL_COLOR_BUFFER_BIT);
    fprintf(stderr, "mesaglfeature: scissor clear done\n");

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_ALWAYS, 1, 0xff);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    fprintf(stderr, "mesaglfeature: fixed state done\n");

    glUseProgram(prog);
    fprintf(stderr, "mesaglfeature: glUseProgram done\n");
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          (const void *)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          (const void *)(3 * sizeof(float)));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(glGetUniformLocation(prog, "u_tex"), 0);
    fprintf(stderr, "mesaglfeature: draw resources bound\n");
    if (check_gl("draw setup") != 0)
        goto out;
    fprintf(stderr, "mesaglfeature: glDrawArrays begin\n");
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    fprintf(stderr, "mesaglfeature: glDrawArrays done\n");

    fprintf(stderr, "mesaglfeature: glFinish begin\n");
    glFinish();
    fprintf(stderr, "mesaglfeature: glFinish done\n");
    fprintf(stderr, "mesaglfeature: glReadPixels center begin\n");
    glReadPixels(width / 2, height / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                 center);
    fprintf(stderr,
            "mesaglfeature: glReadPixels center done pixel=%u,%u,%u,%u\n",
            center[0], center[1], center[2], center[3]);
    fprintf(stderr, "mesaglfeature: glReadPixels corner begin\n");
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, corner);
    fprintf(stderr,
            "mesaglfeature: glReadPixels corner done pixel=%u,%u,%u,%u\n",
            corner[0], corner[1], corner[2], corner[3]);
    if (check_gl("draw/read") != 0)
        goto out;

    if (center[0] < 70 || center[0] > 95 ||
        center[1] < 145 || center[1] > 175 ||
        center[2] < 225 || center[2] > 255 ||
        center[3] < 250 ||
        corner[0] > 5 || corner[1] > 5 || corner[2] > 5) {
        fprintf(stderr,
                "mesaglfeature: unexpected pixels size=%dx%d center=%u,%u,%u,%u corner=%u,%u,%u,%u\n",
                width, height, center[0], center[1], center[2], center[3],
                corner[0], corner[1], corner[2], corner[3]);
        goto out;
    }

    printf("mesaglfeature: pass size=%dx%d center=%u,%u,%u,%u corner=%u,%u,%u,%u\n",
           width, height, center[0], center[1], center[2], center[3],
           corner[0], corner[1], corner[2], corner[3]);
    status = 0;

out:
    destroy_fbo(fbo, color, depth, stencil);
    return status;
}

int main(void)
{
    static const EGLint pbuffer_attrs[] = {
        EGL_WIDTH, 16,
        EGL_HEIGHT, 16,
        EGL_NONE
    };
    static const EGLint context_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    EGLint config_attrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_STENCIL_SIZE, 8,
        EGL_NONE
    };
    static const float verts[] = {
        -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
    };
    static const uint8_t texel[4] = {64, 128, 192, 255};
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLConfig config = NULL;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLint major = 0;
    EGLint minor = 0;
    EGLint nconfigs = 0;
    const char *renderer = NULL;
    const char *version = NULL;
    GLuint prog = 0;
    GLuint vbo = 0;
    GLuint tex = 0;
    int status = 1;

    /*
     * Decide between the hardware GPU path (Mesa d3d12 Gallium driver on the
     * real adapter) and the software smoke fallback (softpipe) BEFORE touching
     * the renderer-selection env.  The GUI shell exports GALLIUM_DRIVER=d3d12
     * (+ LIBGL_ALWAYS_SOFTWARE=0) on a /dev/dxg host but does NOT export
     * MESA_LOADER_DRIVER_OVERRIDE.  Forcing softpipe here unconditionally would
     * therefore override GALLIUM_DRIVER=d3d12 and silently demote the probe to
     * software, which then fail-closes in require_d3d12_renderer().  Only force
     * software when the GPU path was not requested.
     */
    {
        int want_d3d12 = env_truthy("XV6_MESAGLFEATURE_REQUIRE_D3D12") ||
                         env_is("GALLIUM_DRIVER", "d3d12") ||
                         env_is("MESA_LOADER_DRIVER_OVERRIDE", "d3d12");

        if (want_d3d12) {
            /* Hardware GPU path: never force the software rasterizer. */
            setenv("LIBGL_ALWAYS_SOFTWARE", "0", 0);
            setenv("GALLIUM_DRIVER", "d3d12", 0);
            setenv("LIBGL_DRIVERS_PATH", "/lib/dri", 0);
            fprintf(stderr, "mesaglfeature: mode=gpu-d3d12 (hardware render)\n");
        } else {
            setenv("LIBGL_ALWAYS_SOFTWARE", "1", 0);
            setenv("MESA_LOADER_DRIVER_OVERRIDE", "softpipe", 0);
            setenv("LIBGL_DRIVERS_PATH", "/usr/lib/x86_64-linux-gnu/dri", 0);
            fprintf(stderr, "mesaglfeature: mode=software (softpipe smoke)\n");
        }
    }
    report_runtime_env();

    display = get_surfaceless_display();
    if (display == EGL_NO_DISPLAY ||
        !eglInitialize(display, &major, &minor) ||
        !eglChooseConfig(display, config_attrs, &config, 1, &nconfigs) ||
        nconfigs < 1 ||
        !eglBindAPI(EGL_OPENGL_ES_API)) {
        fprintf(stderr, "mesaglfeature: EGL setup failed error=0x%x\n",
                eglGetError());
        goto out;
    }
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attrs);
    surface = eglCreatePbufferSurface(display, config, pbuffer_attrs);
    if (context == EGL_NO_CONTEXT || surface == EGL_NO_SURFACE ||
        !eglMakeCurrent(display, surface, surface, context)) {
        fprintf(stderr, "mesaglfeature: EGL context failed error=0x%x\n",
                eglGetError());
        goto out;
    }

    renderer = (const char *)glGetString(GL_RENDERER);
    version = (const char *)glGetString(GL_VERSION);
    printf("mesaglfeature: EGL %d.%d renderer=%s version=%s\n",
           major, minor, renderer ? renderer : "(null)",
           version ? version : "(null)");
    if (require_d3d12_renderer(renderer) != 0)
        goto out;

    fprintf(stderr, "mesaglfeature: create_program begin\n");
    prog = create_program();
    fprintf(stderr, "mesaglfeature: create_program -> %u\n", prog);
    if (!prog)
        goto out;
    fprintf(stderr, "mesaglfeature: buffer setup begin\n");
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    fprintf(stderr, "mesaglfeature: buffer setup done vbo=%u\n", vbo);

    fprintf(stderr, "mesaglfeature: texture setup begin\n");
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, texel);
    fprintf(stderr, "mesaglfeature: texture setup done tex=%u\n", tex);
    if (check_gl("resource setup") != 0)
        goto out;

    if (draw_case(prog, vbo, tex, 32, 32) != 0)
        goto out;
    if (draw_case(prog, vbo, tex, 64, 32) != 0)
        goto out;

    printf("mesaglfeature: ok\n");
    status = 0;

out:
    if (tex)
        glDeleteTextures(1, &tex);
    if (vbo)
        glDeleteBuffers(1, &vbo);
    if (prog)
        glDeleteProgram(prog);
    if (display != EGL_NO_DISPLAY) {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
        if (surface != EGL_NO_SURFACE)
            eglDestroySurface(display, surface);
        if (context != EGL_NO_CONTEXT)
            eglDestroyContext(display, context);
        eglTerminate(display);
    }
    return status;
}
