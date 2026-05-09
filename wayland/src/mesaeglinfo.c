/*
 * mesaeglinfo.c - xv6-local Mesa EGL smoke probe.
 *
 * This intentionally avoids the compositor and Wayland window path.  It proves
 * that the real Mesa libEGL/libGLESv2 stack can create a surfaceless context,
 * render, and read pixels before or without the compositor window path.
 */

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
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

static int check_pixel(const uint8_t *pixel)
{
    int r = pixel[0];
    int g = pixel[1];
    int b = pixel[2];
    int a = pixel[3];

    return r >= 45 && r <= 80 &&
           g >= 95 && g <= 140 &&
           b >= 145 && b <= 200 &&
           a >= 240;
}

static int run_probe(EGLenum api, EGLint renderable_type, const char *api_name)
{
    static const EGLint pbuffer_attrs[] = {
        EGL_WIDTH, 16,
        EGL_HEIGHT, 16,
        EGL_NONE
    };
    static const EGLint gles_context_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    EGLint config_attrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, renderable_type,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    const EGLint *context_attrs =
        api == EGL_OPENGL_ES_API ? gles_context_attrs : NULL;

    EGLDisplay display = EGL_NO_DISPLAY;
    EGLConfig config = NULL;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLint major = 0;
    EGLint minor = 0;
    EGLint nconfigs = 0;
    uint8_t pixel[4] = {0, 0, 0, 0};
    int status = 1;

    display = get_surfaceless_display();
    if (display == EGL_NO_DISPLAY) {
        fprintf(stderr, "mesaeglinfo: %s no EGL display (0x%x)\n",
                api_name, eglGetError());
        goto out;
    }
    if (!eglInitialize(display, &major, &minor)) {
        fprintf(stderr, "mesaeglinfo: %s eglInitialize failed (0x%x)\n",
                api_name, eglGetError());
        goto out;
    }
    if (!eglChooseConfig(display, config_attrs, &config, 1, &nconfigs) ||
        nconfigs < 1) {
        fprintf(stderr, "mesaeglinfo: %s eglChooseConfig failed (0x%x)\n",
                api_name, eglGetError());
        goto out;
    }
    if (!eglBindAPI(api)) {
        fprintf(stderr, "mesaeglinfo: %s eglBindAPI failed (0x%x)\n",
                api_name, eglGetError());
        goto out;
    }
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attrs);
    surface = eglCreatePbufferSurface(display, config, pbuffer_attrs);
    if (context == EGL_NO_CONTEXT || surface == EGL_NO_SURFACE ||
        !eglMakeCurrent(display, surface, surface, context)) {
        fprintf(stderr, "mesaeglinfo: %s context setup failed (0x%x)\n",
                api_name, eglGetError());
        goto out;
    }

    glViewport(0, 0, 16, 16);
    glClearColor(0.25f, 0.45f, 0.65f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    glFinish();

    printf("mesaeglinfo: %s EGL %d.%d vendor=%s\n", api_name, major, minor,
           eglQueryString(display, EGL_VENDOR));
    printf("mesaeglinfo: %s GL vendor=%s\n", api_name, glGetString(GL_VENDOR));
    printf("mesaeglinfo: %s GL renderer=%s\n", api_name,
           glGetString(GL_RENDERER));
    printf("mesaeglinfo: %s GL version=%s\n", api_name,
           glGetString(GL_VERSION));
    printf("mesaeglinfo: %s pixel rgba=%u,%u,%u,%u\n", api_name,
           pixel[0], pixel[1], pixel[2], pixel[3]);

    if (!check_pixel(pixel)) {
        fprintf(stderr, "mesaeglinfo: %s unexpected clear/readback pixel\n",
                api_name);
        goto out;
    }

    status = 0;

out:
    if (display != EGL_NO_DISPLAY) {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
        if (surface != EGL_NO_SURFACE)
            eglDestroySurface(display, surface);
        if (context != EGL_NO_CONTEXT)
            eglDestroyContext(display, context);
        eglTerminate(display);
    }
    if (status == 0)
        printf("mesaeglinfo: %s ok\n", api_name);
    return status;
}

int main(int argc, char **argv)
{
    int run_gles = 1;
    int run_gl = 0;
    int status = 0;

    setenv("LIBGL_ALWAYS_SOFTWARE", "1", 0);
    setenv("MESA_LOADER_DRIVER_OVERRIDE", "softpipe", 0);
    setenv("LIBGL_DRIVERS_PATH", "/usr/lib/x86_64-linux-gnu/dri", 0);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--api=gl") == 0) {
            run_gles = 0;
            run_gl = 1;
        } else if (strcmp(argv[i], "--api=gles") == 0) {
            run_gles = 1;
            run_gl = 0;
        } else if (strcmp(argv[i], "--all") == 0) {
            run_gles = 1;
            run_gl = 1;
        } else {
            fprintf(stderr,
                    "usage: mesaeglinfo [--api=gles|--api=gl|--all]\n");
            return 2;
        }
    }

    if (run_gles)
        status |= run_probe(EGL_OPENGL_ES_API, EGL_OPENGL_ES2_BIT, "gles");
    if (run_gl)
        status |= run_probe(EGL_OPENGL_API, EGL_OPENGL_BIT, "gl");
    return status ? 1 : 0;
}
