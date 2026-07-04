/*
 * mesacopytexture.c - focused GL_CHROMIUM_copy_texture reducer.
 *
 * The extension is intentionally resolved dynamically so this can be built
 * before the Mesa entry points are available in public headers.
 */

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif
#ifndef GL_RED
#define GL_RED 0x1903
#endif
#ifndef GL_RG
#define GL_RG 0x8227
#endif
#ifndef GL_R8
#define GL_R8 0x8229
#endif
#ifndef GL_RG8
#define GL_RG8 0x822B
#endif
#ifndef GL_NUM_EXTENSIONS
#define GL_NUM_EXTENSIONS 0x821D
#endif
#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_TEXTURE_RECTANGLE_ARB
#define GL_TEXTURE_RECTANGLE_ARB 0x84F5
#endif
#ifndef GL_TEXTURE_EXTERNAL_OES
#define GL_TEXTURE_EXTERNAL_OES 0x8D65
#endif
#ifndef GL_TEXTURE_IMMUTABLE_FORMAT
#define GL_TEXTURE_IMMUTABLE_FORMAT 0x912F
#endif

#define SKIP_RC 77
#define TEX_W 4
#define TEX_H 4

typedef void (*gl_copy_texture_chromium_fn)(GLuint, GLint, GLenum, GLuint,
                                            GLint, GLenum, GLenum, GLboolean,
                                            GLboolean, GLboolean);
typedef void (*gl_copy_sub_texture_chromium_fn)(GLuint, GLint, GLenum, GLuint,
                                                GLint, GLint, GLint, GLint,
                                                GLint, GLsizei, GLsizei,
                                                GLboolean, GLboolean,
                                                GLboolean);
typedef void (*gl_tex_storage_2d_fn)(GLenum, GLsizei, GLenum, GLsizei,
                                     GLsizei);
typedef const GLubyte *(*gl_get_string_i_fn)(GLenum, GLuint);

struct egl_state {
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    int requested_context_version;
};

struct chromium_copy_texture {
    gl_copy_texture_chromium_fn copy_texture;
    gl_copy_sub_texture_chromium_fn copy_sub_texture;
    gl_tex_storage_2d_fn tex_storage_2d;
};

static GLuint create_texture_typed(GLenum internal_format, GLenum format,
                                   int width, int height, GLenum type,
                                   const uint8_t *pixels, int *accepted);
static int gl_version_at_least(int major, int minor);

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

static int init_egl(struct egl_state *egl)
{
    static const EGLint pbuffer_attrs[] = {
        EGL_WIDTH, 16,
        EGL_HEIGHT, 16,
        EGL_NONE
    };
    EGLint config_attrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLConfig config = NULL;
    EGLint nconfigs = 0;
    EGLint major = 0;
    EGLint minor = 0;

    memset(egl, 0, sizeof(*egl));
    egl->display = EGL_NO_DISPLAY;
    egl->surface = EGL_NO_SURFACE;
    egl->context = EGL_NO_CONTEXT;
    egl->requested_context_version = 0;

    egl->display = get_surfaceless_display();
    if (egl->display == EGL_NO_DISPLAY) {
        printf("MESACOPYTEXTURE-FAIL case=egl_setup step=get_display error=0x%x\n",
               eglGetError());
        return 1;
    }
    if (!eglInitialize(egl->display, &major, &minor)) {
        printf("MESACOPYTEXTURE-FAIL case=egl_setup step=initialize error=0x%x\n",
               eglGetError());
        return 1;
    }
    if (!eglChooseConfig(egl->display, config_attrs, &config, 1, &nconfigs) ||
        nconfigs < 1) {
        printf("MESACOPYTEXTURE-FAIL case=egl_setup step=choose_config error=0x%x\n",
               eglGetError());
        return 1;
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        printf("MESACOPYTEXTURE-FAIL case=egl_setup step=bind_api error=0x%x\n",
               eglGetError());
        return 1;
    }
    egl->surface = eglCreatePbufferSurface(egl->display, config,
                                           pbuffer_attrs);
    for (int version = 3; version >= 2; version--) {
        EGLint context_attrs[] = {
            EGL_CONTEXT_CLIENT_VERSION, version,
            EGL_NONE
        };

        egl->context = eglCreateContext(egl->display, config,
                                        EGL_NO_CONTEXT, context_attrs);
        if (egl->context != EGL_NO_CONTEXT) {
            egl->requested_context_version = version;
            break;
        }
    }
    if (egl->surface == EGL_NO_SURFACE || egl->context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(egl->display, egl->surface, egl->surface,
                        egl->context)) {
        printf("MESACOPYTEXTURE-FAIL case=egl_setup step=make_current error=0x%x\n",
               eglGetError());
        return 1;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    printf("MESACOPYTEXTURE-PASS case=egl_setup version=%d.%d gles_request=%d\n",
           major, minor, egl->requested_context_version);
    return 0;
}

static void destroy_egl(struct egl_state *egl)
{
    if (egl->display == EGL_NO_DISPLAY)
        return;
    eglMakeCurrent(egl->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                   EGL_NO_CONTEXT);
    if (egl->surface != EGL_NO_SURFACE)
        eglDestroySurface(egl->display, egl->surface);
    if (egl->context != EGL_NO_CONTEXT)
        eglDestroyContext(egl->display, egl->context);
    eglTerminate(egl->display);
}

static void clear_gl_errors(void)
{
    while (glGetError() != GL_NO_ERROR)
        ;
}

static int gl_has_extension(const char *name)
{
    const char *all = (const char *)glGetString(GL_EXTENSIONS);
    const char *exts = all;
    size_t name_len;

    if (!name || !*name)
        return 0;
    name_len = strlen(name);
    while (exts && (exts = strstr(exts, name)) != NULL) {
        char before = exts == all ? ' ' : exts[-1];
        char after = exts[name_len];

        if ((before == ' ' || before == '\0') &&
            (after == ' ' || after == '\0'))
            return 1;
        exts += name_len;
    }
    if (!all && gl_version_at_least(3, 0)) {
        gl_get_string_i_fn get_string_i =
            (gl_get_string_i_fn)eglGetProcAddress("glGetStringi");
        GLint num_exts = 0;

        if (!get_string_i)
            return 0;
        clear_gl_errors();
        glGetIntegerv(GL_NUM_EXTENSIONS, &num_exts);
        if (glGetError() != GL_NO_ERROR)
            return 0;
        for (GLint i = 0; i < num_exts; i++) {
            const char *ext = (const char *)get_string_i(GL_EXTENSIONS, i);

            if (ext && strcmp(ext, name) == 0)
                return 1;
        }
    }
    return 0;
}

static int gl_version_at_least(int major, int minor)
{
    const char *version = (const char *)glGetString(GL_VERSION);
    const char *p = version;
    int got_major = 0;
    int got_minor = 0;

    if (!p)
        return 0;
    if (strncmp(p, "OpenGL ES ", 10) == 0)
        p += 10;
    if (sscanf(p, "%d.%d", &got_major, &got_minor) != 2)
        return 0;
    return got_major > major || (got_major == major && got_minor >= minor);
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

static int renderer_is_software(const char *renderer)
{
    return contains_ci(renderer, "softpipe") ||
           contains_ci(renderer, "llvmpipe") ||
           contains_ci(renderer, "swrast") ||
           contains_ci(renderer, "software rasterizer");
}

static int load_extension(struct chromium_copy_texture *copy)
{
    int has_ext = gl_has_extension("GL_CHROMIUM_copy_texture");

    memset(copy, 0, sizeof(*copy));
    copy->copy_texture = (gl_copy_texture_chromium_fn)
        eglGetProcAddress("glCopyTextureCHROMIUM");
    copy->copy_sub_texture = (gl_copy_sub_texture_chromium_fn)
        eglGetProcAddress("glCopySubTextureCHROMIUM");
    copy->tex_storage_2d = (gl_tex_storage_2d_fn)
        eglGetProcAddress("glTexStorage2D");
    if (!copy->tex_storage_2d)
        copy->tex_storage_2d = (gl_tex_storage_2d_fn)
            eglGetProcAddress("glTexStorage2DEXT");

    if (!has_ext) {
        printf("MESACOPYTEXTURE-SKIP missing GL_CHROMIUM_copy_texture\n");
        return SKIP_RC;
    }
    if (!copy->copy_texture || !copy->copy_sub_texture) {
        printf("MESACOPYTEXTURE-SKIP missing GL_CHROMIUM_copy_texture detail=procs\n");
        return SKIP_RC;
    }

    printf("MESACOPYTEXTURE-PASS case=extension_proc\n");
    return 0;
}

static int check_renderer(void)
{
    const char *renderer = (const char *)glGetString(GL_RENDERER);

    if (renderer_is_software(renderer)) {
        printf("MESACOPYTEXTURE-FAIL case=gpu_renderer renderer=\"%s\"\n",
               renderer ? renderer : "(null)");
        return 1;
    }
    printf("MESACOPYTEXTURE-PASS case=gpu_renderer renderer=\"%s\"\n",
           renderer ? renderer : "(null)");
    return 0;
}

static GLuint create_texture(GLenum format, int width, int height,
                             const uint8_t *pixels, int *accepted)
{
    return create_texture_typed(format, format, width, height,
                                GL_UNSIGNED_BYTE, pixels, accepted);
}

static GLuint create_texture_typed(GLenum internal_format, GLenum format,
                                   int width, int height, GLenum type,
                                   const uint8_t *pixels, int *accepted)
{
    GLuint texture = 0;
    GLenum err;

    if (accepted)
        *accepted = 0;
    clear_gl_errors();
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height, 0,
                 format, type, pixels);
    err = glGetError();
    if (err != GL_NO_ERROR) {
        glDeleteTextures(1, &texture);
        return 0;
    }
    if (accepted)
        *accepted = 1;
    return texture;
}

static GLuint create_empty_texture(void)
{
    GLuint texture = 0;

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return texture;
}

static int read_texture_target_rgba(GLenum target, GLuint texture, GLint level,
                                    int width, int height, uint8_t *pixels)
{
    GLuint fbo = 0;
    GLenum status;
    GLenum err;

    clear_gl_errors();
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target,
                           texture, level);
    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        glDeleteFramebuffers(1, &fbo);
        return 1;
    }
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    err = glGetError();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    return err == GL_NO_ERROR ? 0 : 1;
}

static int read_texture_rgba(GLuint texture, int width, int height,
                             uint8_t *pixels)
{
    return read_texture_target_rgba(GL_TEXTURE_2D, texture, 0, width, height,
                                    pixels);
}

static void fill_rgba(uint8_t *pixels)
{
    for (int y = 0; y < TEX_H; y++) {
        for (int x = 0; x < TEX_W; x++) {
            uint8_t *p = pixels + (y * TEX_W + x) * 4;

            p[0] = (uint8_t)(20 + x * 20 + y * 3);
            p[1] = (uint8_t)(30 + y * 18 + x * 5);
            p[2] = (uint8_t)(40 + x * 9 + y * 11);
            p[3] = (uint8_t)(128 + ((x + y) & 3) * 32);
        }
    }
}

static void fill_rgb(uint8_t *pixels)
{
    for (int y = 0; y < TEX_H; y++) {
        for (int x = 0; x < TEX_W; x++) {
            uint8_t *p = pixels + (y * TEX_W + x) * 3;

            p[0] = (uint8_t)(10 + x * 50);
            p[1] = (uint8_t)(20 + y * 50);
            p[2] = (uint8_t)(100 + x * 7 + y * 3);
        }
    }
}

static void fill_luminance_alpha(uint8_t *pixels)
{
    for (int y = 0; y < TEX_H; y++) {
        for (int x = 0; x < TEX_W; x++) {
            uint8_t *p = pixels + (y * TEX_W + x) * 2;

            p[0] = (uint8_t)(25 + x * 35 + y * 11);
            p[1] = (uint8_t)(90 + x * 23 + y * 17);
        }
    }
}

static void fill_alpha(uint8_t *pixels)
{
    for (int i = 0; i < TEX_W * TEX_H; i++)
        pixels[i] = (uint8_t)(30 + i * 11);
}

static void fill_luminance(uint8_t *pixels)
{
    for (int i = 0; i < TEX_W * TEX_H; i++)
        pixels[i] = (uint8_t)(20 + i * 9);
}

static void fill_bgra(uint8_t *pixels)
{
    for (int y = 0; y < TEX_H; y++) {
        for (int x = 0; x < TEX_W; x++) {
            uint8_t *p = pixels + (y * TEX_W + x) * 4;

            p[0] = (uint8_t)(40 + x * 7);
            p[1] = (uint8_t)(50 + y * 9);
            p[2] = (uint8_t)(60 + x * 11 + y * 3);
            p[3] = (uint8_t)(120 + x * 13 + y * 5);
        }
    }
}

static void fill_rg(uint8_t *pixels)
{
    for (int i = 0; i < TEX_W * TEX_H; i++) {
        pixels[i * 2 + 0] = (uint8_t)(15 + i * 5);
        pixels[i * 2 + 1] = (uint8_t)(90 + i * 3);
    }
}

static void fill_zero_alpha_rgba(uint8_t *pixels)
{
    for (int i = 0; i < TEX_W * TEX_H; i++) {
        pixels[i * 4 + 0] = (uint8_t)(80 + i * 3);
        pixels[i * 4 + 1] = (uint8_t)(60 + i * 5);
        pixels[i * 4 + 2] = (uint8_t)(40 + i * 7);
        pixels[i * 4 + 3] = 0;
    }
}

static uint8_t clamp_u8(int value)
{
    if (value < 0)
        return 0;
    if (value > 255)
        return 255;
    return (uint8_t)value;
}

static void expected_rgba_transform(const uint8_t *src, uint8_t *dst,
                                    int flip_y, int premultiply,
                                    int unmultiply)
{
    for (int y = 0; y < TEX_H; y++) {
        int sy = flip_y ? TEX_H - 1 - y : y;

        for (int x = 0; x < TEX_W; x++) {
            const uint8_t *s = src + (sy * TEX_W + x) * 4;
            uint8_t *d = dst + (y * TEX_W + x) * 4;
            int a = s[3];

            memcpy(d, s, 4);
            if (premultiply && !unmultiply) {
                d[0] = (uint8_t)((s[0] * a + 127) / 255);
                d[1] = (uint8_t)((s[1] * a + 127) / 255);
                d[2] = (uint8_t)((s[2] * a + 127) / 255);
            } else if (unmultiply && !premultiply) {
                if (a == 0) {
                    d[0] = 0;
                    d[1] = 0;
                    d[2] = 0;
                } else {
                    d[0] = clamp_u8((s[0] * 255 + a / 2) / a);
                    d[1] = clamp_u8((s[1] * 255 + a / 2) / a);
                    d[2] = clamp_u8((s[2] * 255 + a / 2) / a);
                }
            }
        }
    }
}

static int compare_pixels(const uint8_t *actual, const uint8_t *expected,
                          int count, int tolerance, const char *case_name)
{
    for (int i = 0; i < count; i++) {
        int delta = (int)actual[i] - (int)expected[i];

        if (delta < 0)
            delta = -delta;
        if (delta > tolerance) {
            printf("MESACOPYTEXTURE-FAIL case=%s byte=%d actual=%u expected=%u tolerance=%d\n",
                   case_name, i, actual[i], expected[i], tolerance);
            return 1;
        }
    }
    return 0;
}

static int run_rgba_case(const struct chromium_copy_texture *copy,
                         const char *case_name, int flip_y, int premultiply,
                         int unmultiply, int tolerance)
{
    uint8_t src_pixels[TEX_W * TEX_H * 4];
    uint8_t expected[TEX_W * TEX_H * 4];
    uint8_t actual[TEX_W * TEX_H * 4];
    GLuint src = 0;
    GLuint dst = 0;
    GLenum err;
    int accepted = 0;
    int failed = 0;

    fill_rgba(src_pixels);
    expected_rgba_transform(src_pixels, expected, flip_y, premultiply,
                            unmultiply);
    src = create_texture(GL_RGBA, TEX_W, TEX_H, src_pixels, &accepted);
    dst = create_empty_texture();
    if (!src || !dst || !accepted) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=create_texture\n",
               case_name);
        failed = 1;
        goto out;
    }

    clear_gl_errors();
    copy->copy_texture(src, 0, GL_TEXTURE_2D, dst, 0, GL_RGBA,
                       GL_UNSIGNED_BYTE, flip_y ? GL_TRUE : GL_FALSE,
                       premultiply ? GL_TRUE : GL_FALSE,
                       unmultiply ? GL_TRUE : GL_FALSE);
    err = glGetError();
    if (err != GL_NO_ERROR) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=copy error=0x%x\n",
               case_name, err);
        failed = 1;
        goto out;
    }
    if (read_texture_rgba(dst, TEX_W, TEX_H, actual)) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=readback\n", case_name);
        failed = 1;
        goto out;
    }
    failed = compare_pixels(actual, expected, TEX_W * TEX_H * 4, tolerance,
                            case_name);

out:
    if (src)
        glDeleteTextures(1, &src);
    if (dst)
        glDeleteTextures(1, &dst);
    if (!failed)
        printf("MESACOPYTEXTURE-PASS case=%s\n", case_name);
    return failed;
}

static int run_rgb_to_rgba_case(const struct chromium_copy_texture *copy)
{
    const char *case_name = "rgb_to_rgba_alpha";
    uint8_t src_pixels[TEX_W * TEX_H * 3];
    uint8_t expected[TEX_W * TEX_H * 4];
    uint8_t actual[TEX_W * TEX_H * 4];
    GLuint src = 0;
    GLuint dst = 0;
    GLenum err;
    int accepted = 0;
    int failed = 0;

    fill_rgb(src_pixels);
    for (int i = 0; i < TEX_W * TEX_H; i++) {
        expected[i * 4 + 0] = src_pixels[i * 3 + 0];
        expected[i * 4 + 1] = src_pixels[i * 3 + 1];
        expected[i * 4 + 2] = src_pixels[i * 3 + 2];
        expected[i * 4 + 3] = 255;
    }
    src = create_texture(GL_RGB, TEX_W, TEX_H, src_pixels, &accepted);
    dst = create_empty_texture();
    if (!src || !dst || !accepted) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=create_texture\n",
               case_name);
        failed = 1;
        goto out;
    }
    clear_gl_errors();
    copy->copy_texture(src, 0, GL_TEXTURE_2D, dst, 0, GL_RGBA,
                       GL_UNSIGNED_BYTE, GL_FALSE, GL_FALSE, GL_FALSE);
    err = glGetError();
    if (err != GL_NO_ERROR) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=copy error=0x%x\n",
               case_name, err);
        failed = 1;
        goto out;
    }
    if (read_texture_rgba(dst, TEX_W, TEX_H, actual)) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=readback\n", case_name);
        failed = 1;
        goto out;
    }
    failed = compare_pixels(actual, expected, TEX_W * TEX_H * 4, 0,
                            case_name);

out:
    if (src)
        glDeleteTextures(1, &src);
    if (dst)
        glDeleteTextures(1, &dst);
    if (!failed)
        printf("MESACOPYTEXTURE-PASS case=%s\n", case_name);
    return failed;
}

static int run_format_to_rgba_case(const struct chromium_copy_texture *copy,
                                   const char *case_name,
                                   GLenum internal_format, GLenum format,
                                   int width, int height,
                                   const uint8_t *src_pixels,
                                   const uint8_t *expected, int optional)
{
    uint8_t actual[TEX_W * TEX_H * 4];
    GLuint src = 0;
    GLuint dst = 0;
    GLenum err;
    int accepted = 0;
    int failed = 0;

    src = create_texture_typed(internal_format, format, width, height,
                               GL_UNSIGNED_BYTE, src_pixels, &accepted);
    dst = create_empty_texture();
    if (!src || !accepted) {
        if (optional) {
            printf("MESACOPYTEXTURE-SKIP case=%s detail=source_format_unaccepted\n",
                   case_name);
            failed = 0;
            goto out;
        }
        printf("MESACOPYTEXTURE-FAIL case=%s step=create_source\n",
               case_name);
        failed = 1;
        goto out;
    }
    if (!dst) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=create_dest\n",
               case_name);
        failed = 1;
        goto out;
    }
    clear_gl_errors();
    copy->copy_texture(src, 0, GL_TEXTURE_2D, dst, 0, GL_RGBA,
                       GL_UNSIGNED_BYTE, GL_FALSE, GL_FALSE, GL_FALSE);
    err = glGetError();
    if (err != GL_NO_ERROR) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=copy error=0x%x\n",
               case_name, err);
        failed = 1;
        goto out;
    }
    if (read_texture_rgba(dst, width, height, actual)) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=readback\n", case_name);
        failed = 1;
        goto out;
    }
    failed = compare_pixels(actual, expected, width * height * 4, 0,
                            case_name);

out:
    if (src)
        glDeleteTextures(1, &src);
    if (dst)
        glDeleteTextures(1, &dst);
    if (!failed && accepted)
        printf("MESACOPYTEXTURE-PASS case=%s\n", case_name);
    return failed;
}

static int run_alpha_luminance_cases(
    const struct chromium_copy_texture *copy)
{
    uint8_t alpha[TEX_W * TEX_H];
    uint8_t lum[TEX_W * TEX_H];
    uint8_t expected[TEX_W * TEX_H * 4];
    int failures = 0;

    fill_alpha(alpha);
    for (int i = 0; i < TEX_W * TEX_H; i++) {
        expected[i * 4 + 0] = 0;
        expected[i * 4 + 1] = 0;
        expected[i * 4 + 2] = 0;
        expected[i * 4 + 3] = alpha[i];
    }
    failures += run_format_to_rgba_case(copy, "alpha_to_rgba", GL_ALPHA,
                                        GL_ALPHA, TEX_W, TEX_H, alpha,
                                        expected, 1);

    fill_luminance(lum);
    for (int i = 0; i < TEX_W * TEX_H; i++) {
        expected[i * 4 + 0] = lum[i];
        expected[i * 4 + 1] = lum[i];
        expected[i * 4 + 2] = lum[i];
        expected[i * 4 + 3] = 255;
    }
    failures += run_format_to_rgba_case(copy, "luminance_to_rgba",
                                        GL_LUMINANCE, GL_LUMINANCE, TEX_W,
                                        TEX_H, lum, expected, 1);
    return failures;
}

static int run_bgra_case(const struct chromium_copy_texture *copy)
{
    const char *case_name = "bgra_to_rgba";
    uint8_t bgra[TEX_W * TEX_H * 4];
    uint8_t expected[TEX_W * TEX_H * 4];

    if (!gl_has_extension("GL_EXT_texture_format_BGRA8888") &&
        !gl_has_extension("GL_APPLE_texture_format_BGRA8888")) {
        printf("MESACOPYTEXTURE-SKIP case=%s missing texture_format_BGRA8888\n",
               case_name);
        return 0;
    }

    fill_bgra(bgra);
    for (int i = 0; i < TEX_W * TEX_H; i++) {
        expected[i * 4 + 0] = bgra[i * 4 + 2];
        expected[i * 4 + 1] = bgra[i * 4 + 1];
        expected[i * 4 + 2] = bgra[i * 4 + 0];
        expected[i * 4 + 3] = bgra[i * 4 + 3];
    }
    return run_format_to_rgba_case(copy, case_name, GL_BGRA_EXT, GL_BGRA_EXT,
                                   TEX_W, TEX_H, bgra, expected, 1);
}

static int run_r8_rg8_cases(const struct chromium_copy_texture *copy)
{
    uint8_t r8[TEX_W * TEX_H];
    uint8_t rg8[TEX_W * TEX_H * 2];
    uint8_t expected[TEX_W * TEX_H * 4];
    int supported = gl_version_at_least(3, 0) ||
                    gl_has_extension("GL_EXT_texture_rg") ||
                    gl_has_extension("GL_ARB_texture_rg");
    int failures = 0;

    if (!supported) {
        printf("MESACOPYTEXTURE-SKIP case=r8_to_rgba missing texture_rg\n");
        printf("MESACOPYTEXTURE-SKIP case=rg8_to_rgba missing texture_rg\n");
        return 0;
    }

    fill_luminance(r8);
    for (int i = 0; i < TEX_W * TEX_H; i++) {
        expected[i * 4 + 0] = r8[i];
        expected[i * 4 + 1] = 0;
        expected[i * 4 + 2] = 0;
        expected[i * 4 + 3] = 255;
    }
    failures += run_format_to_rgba_case(copy, "r8_to_rgba", GL_R8, GL_RED,
                                        TEX_W, TEX_H, r8, expected, 1);

    fill_rg(rg8);
    for (int i = 0; i < TEX_W * TEX_H; i++) {
        expected[i * 4 + 0] = rg8[i * 2 + 0];
        expected[i * 4 + 1] = rg8[i * 2 + 1];
        expected[i * 4 + 2] = 0;
        expected[i * 4 + 3] = 255;
    }
    failures += run_format_to_rgba_case(copy, "rg8_to_rgba", GL_RG8, GL_RG,
                                        TEX_W, TEX_H, rg8, expected, 1);
    return failures;
}

static int run_luminance_alpha_case(const struct chromium_copy_texture *copy)
{
    const char *case_name = "luminance_alpha_to_rgba";
    uint8_t src_pixels[TEX_W * TEX_H * 2];
    uint8_t expected[TEX_W * TEX_H * 4];
    uint8_t actual[TEX_W * TEX_H * 4];
    GLuint src = 0;
    GLuint dst = 0;
    GLenum err;
    int accepted = 0;
    int failed = 0;

    fill_luminance_alpha(src_pixels);
    for (int i = 0; i < TEX_W * TEX_H; i++) {
        expected[i * 4 + 0] = src_pixels[i * 2 + 0];
        expected[i * 4 + 1] = src_pixels[i * 2 + 0];
        expected[i * 4 + 2] = src_pixels[i * 2 + 0];
        expected[i * 4 + 3] = src_pixels[i * 2 + 1];
    }
    src = create_texture(GL_LUMINANCE_ALPHA, TEX_W, TEX_H, src_pixels,
                         &accepted);
    dst = create_empty_texture();
    if (!accepted) {
        printf("MESACOPYTEXTURE-SKIP case=%s detail=source_format_unaccepted\n",
               case_name);
        goto out;
    }
    if (!src || !dst) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=create_texture\n",
               case_name);
        failed = 1;
        goto out;
    }
    clear_gl_errors();
    copy->copy_texture(src, 0, GL_TEXTURE_2D, dst, 0, GL_RGBA,
                       GL_UNSIGNED_BYTE, GL_FALSE, GL_FALSE, GL_FALSE);
    err = glGetError();
    if (err != GL_NO_ERROR) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=copy error=0x%x\n",
               case_name, err);
        failed = 1;
        goto out;
    }
    if (read_texture_rgba(dst, TEX_W, TEX_H, actual)) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=readback\n", case_name);
        failed = 1;
        goto out;
    }
    failed = compare_pixels(actual, expected, TEX_W * TEX_H * 4, 0,
                            case_name);

out:
    if (src)
        glDeleteTextures(1, &src);
    if (dst)
        glDeleteTextures(1, &dst);
    if (!failed && accepted)
        printf("MESACOPYTEXTURE-PASS case=%s\n", case_name);
    return failed;
}

static int run_same_texture_invalid_case(
    const struct chromium_copy_texture *copy)
{
    const char *case_name = "same_texture_same_level_invalid";
    uint8_t pixels[TEX_W * TEX_H * 4];
    GLuint texture = 0;
    GLenum err;
    int accepted = 0;
    int failed = 0;

    fill_rgba(pixels);
    texture = create_texture(GL_RGBA, TEX_W, TEX_H, pixels, &accepted);
    if (!texture || !accepted) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=create_texture\n",
               case_name);
        failed = 1;
        goto out;
    }
    clear_gl_errors();
    copy->copy_texture(texture, 0, GL_TEXTURE_2D, texture, 0, GL_RGBA,
                       GL_UNSIGNED_BYTE, GL_FALSE, GL_FALSE, GL_FALSE);
    err = glGetError();
    if (err != GL_INVALID_OPERATION) {
        printf("MESACOPYTEXTURE-FAIL case=%s error=0x%x expected=0x%x\n",
               case_name, err, GL_INVALID_OPERATION);
        failed = 1;
        goto out;
    }

out:
    if (texture)
        glDeleteTextures(1, &texture);
    if (!failed)
        printf("MESACOPYTEXTURE-PASS case=%s\n", case_name);
    return failed;
}

static int run_generated_name_error_case(
    const struct chromium_copy_texture *copy)
{
    const char *case_name = "generated_unbound_texture_errors";
    uint8_t pixels[TEX_W * TEX_H * 4];
    GLuint valid_src = 0;
    GLuint valid_dst = 0;
    GLuint generated_src = 0;
    GLuint generated_dst = 0;
    GLenum err;
    int accepted = 0;
    int failed = 0;

    fill_rgba(pixels);
    valid_src = create_texture(GL_RGBA, TEX_W, TEX_H, pixels, &accepted);
    valid_dst = create_empty_texture();
    glGenTextures(1, &generated_src);
    glGenTextures(1, &generated_dst);
    if (!valid_src || !valid_dst || !generated_src || !generated_dst ||
        !accepted) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=create_texture\n",
               case_name);
        failed = 1;
        goto out;
    }

    clear_gl_errors();
    copy->copy_texture(generated_src, 0, GL_TEXTURE_2D, valid_dst, 0,
                       GL_RGBA, GL_UNSIGNED_BYTE, GL_FALSE, GL_FALSE,
                       GL_FALSE);
    err = glGetError();
    if (err != GL_INVALID_VALUE) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=full_source error=0x%x expected=0x%x\n",
               case_name, err, GL_INVALID_VALUE);
        failed = 1;
        goto out;
    }

    clear_gl_errors();
    copy->copy_texture(valid_src, 0, GL_TEXTURE_2D, generated_dst, 0,
                       GL_RGBA, GL_UNSIGNED_BYTE, GL_FALSE, GL_FALSE,
                       GL_FALSE);
    err = glGetError();
    if (err != GL_INVALID_VALUE) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=full_dest error=0x%x expected=0x%x\n",
               case_name, err, GL_INVALID_VALUE);
        failed = 1;
        goto out;
    }

    clear_gl_errors();
    copy->copy_sub_texture(generated_src, 0, GL_TEXTURE_2D, valid_dst, 0,
                           0, 0, 0, 0, 1, 1, GL_FALSE, GL_FALSE, GL_FALSE);
    err = glGetError();
    if (err != GL_INVALID_VALUE) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=sub_source error=0x%x expected=0x%x\n",
               case_name, err, GL_INVALID_VALUE);
        failed = 1;
        goto out;
    }

    clear_gl_errors();
    copy->copy_sub_texture(valid_src, 0, GL_TEXTURE_2D, generated_dst, 0,
                           0, 0, 0, 0, 1, 1, GL_FALSE, GL_FALSE, GL_FALSE);
    err = glGetError();
    if (err != GL_INVALID_VALUE) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=sub_dest error=0x%x expected=0x%x\n",
               case_name, err, GL_INVALID_VALUE);
        failed = 1;
        goto out;
    }

out:
    if (valid_src)
        glDeleteTextures(1, &valid_src);
    if (valid_dst)
        glDeleteTextures(1, &valid_dst);
    if (generated_src)
        glDeleteTextures(1, &generated_src);
    if (generated_dst)
        glDeleteTextures(1, &generated_dst);
    if (!failed)
        printf("MESACOPYTEXTURE-PASS case=%s\n", case_name);
    return failed;
}

static int run_same_texture_subcopy_case(
    const struct chromium_copy_texture *copy)
{
    const char *case_name = "same_texture_subcopy_nonoverlap";
    uint8_t pixels[TEX_W * TEX_H * 4];
    uint8_t expected[TEX_W * TEX_H * 4];
    uint8_t actual[TEX_W * TEX_H * 4];
    GLuint texture = 0;
    GLenum err;
    int accepted = 0;
    int failed = 0;

    fill_rgba(pixels);
    memcpy(expected, pixels, sizeof(expected));
    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 2; x++) {
            memcpy(expected + (((y + 2) * TEX_W + (x + 2)) * 4),
                   pixels + ((y * TEX_W + x) * 4), 4);
        }
    }

    texture = create_texture(GL_RGBA, TEX_W, TEX_H, pixels, &accepted);
    if (!texture || !accepted) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=create_texture\n",
               case_name);
        failed = 1;
        goto out;
    }

    clear_gl_errors();
    copy->copy_sub_texture(texture, 0, GL_TEXTURE_2D, texture, 0, 2, 2,
                           0, 0, 2, 2, GL_FALSE, GL_FALSE, GL_FALSE);
    err = glGetError();
    if (err != GL_NO_ERROR) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=sub_copy error=0x%x\n",
               case_name, err);
        failed = 1;
        goto out;
    }
    if (read_texture_rgba(texture, TEX_W, TEX_H, actual)) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=readback\n", case_name);
        failed = 1;
        goto out;
    }
    failed = compare_pixels(actual, expected, TEX_W * TEX_H * 4, 0,
                            case_name);

out:
    if (texture)
        glDeleteTextures(1, &texture);
    if (!failed)
        printf("MESACOPYTEXTURE-PASS case=%s\n", case_name);
    return failed;
}

static int define_rgba_level(GLuint texture, GLint level, int width,
                             int height, const uint8_t *pixels)
{
    GLenum err;

    clear_gl_errors();
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, pixels);
    err = glGetError();
    return err == GL_NO_ERROR ? 0 : 1;
}

static int run_level_dimension_case(const struct chromium_copy_texture *copy)
{
    const char *case_name = "level_dimension_validation";
    uint8_t level0[TEX_W * TEX_H * 4];
    uint8_t level1[2 * 2 * 4];
    uint8_t actual[2 * 2 * 4];
    GLuint src = 0;
    GLuint dst = 0;
    GLenum err;
    int accepted = 0;
    int failed = 0;
    int es3 = gl_version_at_least(3, 0);

    fill_rgba(level0);
    for (int i = 0; i < 2 * 2; i++) {
        level1[i * 4 + 0] = (uint8_t)(150 + i);
        level1[i * 4 + 1] = (uint8_t)(80 + i * 3);
        level1[i * 4 + 2] = (uint8_t)(40 + i * 5);
        level1[i * 4 + 3] = 255;
    }
    src = create_texture(GL_RGBA, TEX_W, TEX_H, level0, &accepted);
    dst = create_empty_texture();
    if (!src || !dst || !accepted) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=create_texture\n",
               case_name);
        failed = 1;
        goto out;
    }

    clear_gl_errors();
    copy->copy_texture(src, -1, GL_TEXTURE_2D, dst, 0, GL_RGBA,
                       GL_UNSIGNED_BYTE, GL_FALSE, GL_FALSE, GL_FALSE);
    err = glGetError();
    if (err != GL_INVALID_VALUE) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=negative_source_level error=0x%x expected=0x%x\n",
               case_name, err, GL_INVALID_VALUE);
        failed = 1;
        goto out;
    }

    clear_gl_errors();
    copy->copy_texture(src, 1, GL_TEXTURE_2D, dst, 0, GL_RGBA,
                       GL_UNSIGNED_BYTE, GL_FALSE, GL_FALSE, GL_FALSE);
    err = glGetError();
    if (err != GL_INVALID_VALUE) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=undefined_source_level error=0x%x expected=0x%x\n",
               case_name, err, GL_INVALID_VALUE);
        failed = 1;
        goto out;
    }

    if (define_rgba_level(src, 1, 2, 2, level1)) {
        if (es3) {
            printf("MESACOPYTEXTURE-FAIL case=%s step=define_level1\n",
                   case_name);
            failed = 1;
        } else {
            printf("MESACOPYTEXTURE-SKIP case=%s detail=level1_define_unaccepted\n",
                   case_name);
        }
        goto out;
    }

    clear_gl_errors();
    copy->copy_texture(src, 1, GL_TEXTURE_2D, dst, 0, GL_RGBA,
                       GL_UNSIGNED_BYTE, GL_FALSE, GL_FALSE, GL_FALSE);
    err = glGetError();
    if (es3) {
        if (err != GL_NO_ERROR) {
            printf("MESACOPYTEXTURE-FAIL case=%s step=es3_source_level1_copy error=0x%x\n",
                   case_name, err);
            failed = 1;
            goto out;
        }
        if (read_texture_rgba(dst, 2, 2, actual)) {
            printf("MESACOPYTEXTURE-FAIL case=%s step=es3_source_level1_readback\n",
                   case_name);
            failed = 1;
            goto out;
        }
        failed = compare_pixels(actual, level1, 2 * 2 * 4, 0, case_name);
        if (failed)
            goto out;
    } else if (err != GL_INVALID_VALUE) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=es2_source_level1_error error=0x%x expected=0x%x\n",
               case_name, err, GL_INVALID_VALUE);
        failed = 1;
        goto out;
    }

    if (es3) {
        clear_gl_errors();
        copy->copy_texture(src, 1, GL_TEXTURE_2D, dst, 1, GL_RGBA,
                           GL_UNSIGNED_BYTE, GL_FALSE, GL_FALSE, GL_FALSE);
        err = glGetError();
        if (err != GL_NO_ERROR) {
            printf("MESACOPYTEXTURE-FAIL case=%s step=es3_dest_level1_copy error=0x%x\n",
                   case_name, err);
            failed = 1;
            goto out;
        }
        if (read_texture_target_rgba(GL_TEXTURE_2D, dst, 1, 2, 2, actual)) {
            printf("MESACOPYTEXTURE-FAIL case=%s step=es3_dest_level1_readback\n",
                   case_name);
            failed = 1;
            goto out;
        }
        failed = compare_pixels(actual, level1, 2 * 2 * 4, 0, case_name);
        if (failed)
            goto out;
    } else {
        printf("MESACOPYTEXTURE-SKIP case=es3_nonzero_level_copy requires_es3\n");
    }

    if (define_rgba_level(dst, 0, TEX_W, TEX_H, level0)) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=define_dest_level0\n",
               case_name);
        failed = 1;
        goto out;
    }

    clear_gl_errors();
    copy->copy_sub_texture(src, 0, GL_TEXTURE_2D, dst, 0, TEX_W, 0, 0, 0,
                           1, 1, GL_FALSE, GL_FALSE, GL_FALSE);
    err = glGetError();
    if (err != GL_INVALID_VALUE) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=subcopy_dest_width_oob error=0x%x expected=0x%x\n",
               case_name, err, GL_INVALID_VALUE);
        failed = 1;
        goto out;
    }

    clear_gl_errors();
    copy->copy_sub_texture(src, 0, GL_TEXTURE_2D, dst, 0, -1, 0, 0, 0,
                           1, 1, GL_FALSE, GL_FALSE, GL_FALSE);
    err = glGetError();
    if (err != GL_INVALID_VALUE) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=subcopy_negative_xoffset error=0x%x expected=0x%x\n",
               case_name, err, GL_INVALID_VALUE);
        failed = 1;
        goto out;
    }

out:
    if (src)
        glDeleteTextures(1, &src);
    if (dst)
        glDeleteTextures(1, &dst);
    if (!failed)
        printf("MESACOPYTEXTURE-PASS case=%s es3=%d\n", case_name, es3);
    return failed;
}

static int run_zero_alpha_transform_cases(
    const struct chromium_copy_texture *copy)
{
    uint8_t src_pixels[TEX_W * TEX_H * 4];
    uint8_t expected[TEX_W * TEX_H * 4];
    uint8_t actual[TEX_W * TEX_H * 4];
    const char *names[] = {
        "premultiply_zero_alpha",
        "unmultiply_zero_alpha"
    };
    int failures = 0;

    fill_zero_alpha_rgba(src_pixels);
    memset(expected, 0, sizeof(expected));
    for (int mode = 0; mode < 2; mode++) {
        GLuint src = 0;
        GLuint dst = 0;
        GLenum err;
        int accepted = 0;
        int failed = 0;

        src = create_texture(GL_RGBA, TEX_W, TEX_H, src_pixels, &accepted);
        dst = create_empty_texture();
        if (!src || !dst || !accepted) {
            printf("MESACOPYTEXTURE-FAIL case=%s step=create_texture\n",
                   names[mode]);
            failed = 1;
            goto one_out;
        }
        clear_gl_errors();
        copy->copy_texture(src, 0, GL_TEXTURE_2D, dst, 0, GL_RGBA,
                           GL_UNSIGNED_BYTE, GL_FALSE,
                           mode == 0 ? GL_TRUE : GL_FALSE,
                           mode == 1 ? GL_TRUE : GL_FALSE);
        err = glGetError();
        if (err != GL_NO_ERROR) {
            printf("MESACOPYTEXTURE-FAIL case=%s step=copy error=0x%x\n",
                   names[mode], err);
            failed = 1;
            goto one_out;
        }
        if (read_texture_rgba(dst, TEX_W, TEX_H, actual)) {
            printf("MESACOPYTEXTURE-FAIL case=%s step=readback\n",
                   names[mode]);
            failed = 1;
            goto one_out;
        }
        failed = compare_pixels(actual, expected, TEX_W * TEX_H * 4, 0,
                                names[mode]);

one_out:
        if (src)
            glDeleteTextures(1, &src);
        if (dst)
            glDeleteTextures(1, &dst);
        if (!failed)
            printf("MESACOPYTEXTURE-PASS case=%s\n", names[mode]);
        failures += failed;
    }
    return failures;
}

static int run_cube_dest_case(const struct chromium_copy_texture *copy)
{
    const char *case_name = "cube_dest_positive_x";
    uint8_t pixels[TEX_W * TEX_H * 4];
    uint8_t actual[TEX_W * TEX_H * 4];
    GLuint src = 0;
    GLuint dst = 0;
    GLenum err;
    int accepted = 0;
    int failed = 0;

    fill_rgba(pixels);
    src = create_texture(GL_RGBA, TEX_W, TEX_H, pixels, &accepted);
    glGenTextures(1, &dst);
    glBindTexture(GL_TEXTURE_CUBE_MAP, dst);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    err = glGetError();
    if (!src || !dst || !accepted || err != GL_NO_ERROR) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=cube_bind error=0x%x\n",
               case_name, err);
        failed = 1;
        goto out;
    }

    clear_gl_errors();
    copy->copy_texture(src, 0, GL_TEXTURE_CUBE_MAP_POSITIVE_X, dst, 0,
                       GL_RGBA, GL_UNSIGNED_BYTE, GL_FALSE, GL_FALSE,
                       GL_FALSE);
    err = glGetError();
    if (err != GL_NO_ERROR) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=copy error=0x%x\n",
               case_name, err);
        failed = 1;
        goto out;
    }
    if (read_texture_target_rgba(GL_TEXTURE_CUBE_MAP_POSITIVE_X, dst, 0,
                                 TEX_W, TEX_H, actual)) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=readback\n", case_name);
        failed = 1;
        goto out;
    }
    failed = compare_pixels(actual, pixels, TEX_W * TEX_H * 4, 0, case_name);

out:
    if (src)
        glDeleteTextures(1, &src);
    if (dst)
        glDeleteTextures(1, &dst);
    if (!failed && err == GL_NO_ERROR)
        printf("MESACOPYTEXTURE-PASS case=%s\n", case_name);
    return failed;
}

static int run_optional_target_probe_cases(void)
{
    if (!gl_has_extension("GL_ARB_texture_rectangle") &&
        !gl_has_extension("GL_EXT_texture_rectangle") &&
        !gl_has_extension("GL_NV_texture_rectangle")) {
        printf("MESACOPYTEXTURE-SKIP case=rectangle_texture_target missing texture_rectangle\n");
    } else {
        printf("MESACOPYTEXTURE-SKIP case=rectangle_texture_target detail=no_gles_rectangle_fixture\n");
    }

    if (!gl_has_extension("GL_OES_EGL_image_external")) {
        printf("MESACOPYTEXTURE-SKIP case=external_oes_texture_source missing OES_EGL_image_external\n");
    } else {
        printf("MESACOPYTEXTURE-SKIP case=external_oes_texture_source detail=no_eglimage_fixture\n");
    }
    return 0;
}

static int run_immutable_case(const struct chromium_copy_texture *copy)
{
    const char *case_name = "immutable_full_fail_subcopy_pass";
    uint8_t src_pixels[TEX_W * TEX_H * 4];
    uint8_t zero_pixels[TEX_W * TEX_H * 4];
    uint8_t actual[TEX_W * TEX_H * 4];
    GLuint src = 0;
    GLuint dst = 0;
    GLenum err;
    int accepted = 0;
    int failed = 0;

    if (!copy->tex_storage_2d) {
        printf("MESACOPYTEXTURE-SKIP case=%s missing glTexStorage2D\n",
               case_name);
        return 0;
    }

    fill_rgba(src_pixels);
    memset(zero_pixels, 0, sizeof(zero_pixels));
    src = create_texture(GL_RGBA, TEX_W, TEX_H, src_pixels, &accepted);
    dst = create_empty_texture();
    if (!src || !dst || !accepted) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=create_texture\n",
               case_name);
        failed = 1;
        goto out;
    }
    clear_gl_errors();
    glBindTexture(GL_TEXTURE_2D, dst);
    copy->tex_storage_2d(GL_TEXTURE_2D, 1, GL_RGBA8, TEX_W, TEX_H);
    err = glGetError();
    if (err != GL_NO_ERROR) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=tex_storage error=0x%x\n",
               case_name, err);
        failed = 1;
        goto out;
    }
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, TEX_W, TEX_H, GL_RGBA,
                    GL_UNSIGNED_BYTE, zero_pixels);
    err = glGetError();
    if (err != GL_NO_ERROR) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=clear_immutable error=0x%x\n",
               case_name, err);
        failed = 1;
        goto out;
    }

    clear_gl_errors();
    copy->copy_texture(src, 0, GL_TEXTURE_2D, dst, 0, GL_RGBA,
                       GL_UNSIGNED_BYTE, GL_FALSE, GL_FALSE, GL_FALSE);
    err = glGetError();
    if (err != GL_INVALID_OPERATION) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=full_copy error=0x%x expected=0x%x\n",
               case_name, err, GL_INVALID_OPERATION);
        failed = 1;
        goto out;
    }

    clear_gl_errors();
    copy->copy_sub_texture(src, 0, GL_TEXTURE_2D, dst, 0, 0, 0, 0, 0,
                           TEX_W, TEX_H, GL_FALSE, GL_FALSE, GL_FALSE);
    err = glGetError();
    if (err != GL_NO_ERROR) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=sub_copy error=0x%x\n",
               case_name, err);
        failed = 1;
        goto out;
    }
    if (read_texture_rgba(dst, TEX_W, TEX_H, actual)) {
        printf("MESACOPYTEXTURE-FAIL case=%s step=readback\n", case_name);
        failed = 1;
        goto out;
    }
    failed = compare_pixels(actual, src_pixels, TEX_W * TEX_H * 4, 0,
                            case_name);

out:
    if (src)
        glDeleteTextures(1, &src);
    if (dst)
        glDeleteTextures(1, &dst);
    if (!failed)
        printf("MESACOPYTEXTURE-PASS case=%s\n", case_name);
    return failed;
}

int main(void)
{
    struct egl_state egl;
    struct chromium_copy_texture copy;
    int failures = 0;
    int rc;

    if (init_egl(&egl))
        return 1;

    rc = load_extension(&copy);
    if (rc == SKIP_RC) {
        destroy_egl(&egl);
        return SKIP_RC;
    }

    failures += check_renderer();
    failures += run_rgba_case(&copy, "rgba_full_copy", 0, 0, 0, 0);
    failures += run_rgba_case(&copy, "flip_y", 1, 0, 0, 0);
    failures += run_rgba_case(&copy, "premultiply", 0, 1, 0, 2);
    failures += run_rgba_case(&copy, "unmultiply", 0, 0, 1, 2);
    failures += run_rgba_case(&copy, "premultiply_unmultiply_noop", 0, 1, 1,
                              2);
    failures += run_rgb_to_rgba_case(&copy);
    failures += run_alpha_luminance_cases(&copy);
    failures += run_luminance_alpha_case(&copy);
    failures += run_bgra_case(&copy);
    failures += run_r8_rg8_cases(&copy);
    failures += run_zero_alpha_transform_cases(&copy);
    failures += run_generated_name_error_case(&copy);
    failures += run_same_texture_invalid_case(&copy);
    failures += run_same_texture_subcopy_case(&copy);
    failures += run_level_dimension_case(&copy);
    failures += run_cube_dest_case(&copy);
    failures += run_optional_target_probe_cases();
    failures += run_immutable_case(&copy);

    destroy_egl(&egl);
    return failures ? 1 : 0;
}
