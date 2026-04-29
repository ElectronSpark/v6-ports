/*
 * mesaeglinfo.c - xv6-local Mesa EGL smoke probe.
 *
 * This intentionally avoids the compositor and Wayland window path.  It proves
 * that the real Mesa libEGL/libGLESv2 stack can create a surfaceless software
 * context, render, and read pixels before we wire an xv6 winsys/buffer path.
 */

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

static EGLDisplay get_surfaceless_display(void)
{
    typedef EGLDisplay (*get_platform_display_fn)(EGLenum, void *,
                                                  const EGLAttrib *);
    get_platform_display_fn get_platform_display =
        (get_platform_display_fn)eglGetProcAddress("eglGetPlatformDisplay");

    if (get_platform_display)
        return get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, NULL, NULL);

    get_platform_display =
        (get_platform_display_fn)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (get_platform_display)
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

int main(void)
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
    static const EGLint pbuffer_attrs[] = {
        EGL_WIDTH, 16,
        EGL_HEIGHT, 16,
        EGL_NONE
    };
    static const EGLint context_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

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
        fprintf(stderr, "mesaeglinfo: no EGL display (0x%x)\n", eglGetError());
        goto out;
    }
    if (!eglInitialize(display, &major, &minor)) {
        fprintf(stderr, "mesaeglinfo: eglInitialize failed (0x%x)\n",
                eglGetError());
        goto out;
    }
    if (!eglChooseConfig(display, config_attrs, &config, 1, &nconfigs) ||
        nconfigs < 1) {
        fprintf(stderr, "mesaeglinfo: eglChooseConfig failed (0x%x)\n",
                eglGetError());
        goto out;
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        fprintf(stderr, "mesaeglinfo: eglBindAPI failed (0x%x)\n",
                eglGetError());
        goto out;
    }
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attrs);
    surface = eglCreatePbufferSurface(display, config, pbuffer_attrs);
    if (context == EGL_NO_CONTEXT || surface == EGL_NO_SURFACE ||
        !eglMakeCurrent(display, surface, surface, context)) {
        fprintf(stderr, "mesaeglinfo: context setup failed (0x%x)\n",
                eglGetError());
        goto out;
    }

    glViewport(0, 0, 16, 16);
    glClearColor(0.25f, 0.45f, 0.65f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    glFinish();

    printf("mesaeglinfo: EGL %d.%d vendor=%s\n", major, minor,
           eglQueryString(display, EGL_VENDOR));
    printf("mesaeglinfo: GL vendor=%s\n", glGetString(GL_VENDOR));
    printf("mesaeglinfo: GL renderer=%s\n", glGetString(GL_RENDERER));
    printf("mesaeglinfo: GL version=%s\n", glGetString(GL_VERSION));
    printf("mesaeglinfo: pixel rgba=%u,%u,%u,%u\n",
           pixel[0], pixel[1], pixel[2], pixel[3]);

    if (!check_pixel(pixel)) {
        fprintf(stderr, "mesaeglinfo: unexpected clear/readback pixel\n");
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
        printf("mesaeglinfo: ok\n");
    return status;
}
