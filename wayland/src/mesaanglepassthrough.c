/*
 * mesaanglepassthrough.c - focused ANGLE passthrough semantics reducer.
 *
 * This intentionally resolves ANGLE-only entry points dynamically so it can
 * build while Mesa support is still being staged.
 */

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif
#ifndef EGL_CONTEXT_WEBGL_COMPATIBILITY_ANGLE
#define EGL_CONTEXT_WEBGL_COMPATIBILITY_ANGLE 0x33AC
#endif
#ifndef EGL_OPENGL_ES3_BIT_KHR
#define EGL_OPENGL_ES3_BIT_KHR 0x00000040
#endif
#ifndef GL_NUM_EXTENSIONS
#define GL_NUM_EXTENSIONS 0x821D
#endif
#ifndef GL_FRAGMENT_SHADER_DERIVATIVE_HINT
#define GL_FRAGMENT_SHADER_DERIVATIVE_HINT 0x8B8B
#endif

#define SKIP_RC 77
#define LENGTH_SENTINEL ((GLsizei)-12345)

typedef const GLubyte *(*gl_get_string_i_fn)(GLenum, GLuint);
typedef void (*gl_get_uniformfv_robust_angle_fn)(GLuint, GLint, GLsizei,
                                                 GLsizei *, GLfloat *);
typedef void (*gl_get_uniformiv_robust_angle_fn)(GLuint, GLint, GLsizei,
                                                 GLsizei *, GLint *);
typedef void (*gl_get_uniformuiv_robust_angle_fn)(GLuint, GLint, GLsizei,
                                                  GLsizei *, GLuint *);
typedef void (*gl_uniform1ui_fn)(GLint, GLuint);
typedef void (*gl_uniform4uiv_fn)(GLint, GLsizei, const GLuint *);

struct egl_state {
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    EGLConfig config;
    int requested_context_version;
};

struct robust_fns {
    gl_get_uniformfv_robust_angle_fn get_uniformfv;
    gl_get_uniformiv_robust_angle_fn get_uniformiv;
    gl_get_uniformuiv_robust_angle_fn get_uniformuiv;
    gl_uniform1ui_fn uniform1ui;
    gl_uniform4uiv_fn uniform4uiv;
};

struct uniform_case {
    const char *name;
    GLint location;
    int components;
};

static void clear_gl_errors(void)
{
    while (glGetError() != GL_NO_ERROR)
        ;
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

static int extension_list_has(const char *all, const char *name)
{
    const char *exts = all;
    size_t name_len;

    if (!all || !name || !*name)
        return 0;
    name_len = strlen(name);
    while ((exts = strstr(exts, name)) != NULL) {
        char before = exts == all ? ' ' : exts[-1];
        char after = exts[name_len];

        if ((before == ' ' || before == '\0') &&
            (after == ' ' || after == '\0'))
            return 1;
        exts += name_len;
    }
    return 0;
}

static int egl_has_display_extension(EGLDisplay display, const char *name)
{
    return extension_list_has(eglQueryString(display, EGL_EXTENSIONS), name);
}

static int gl_has_extension(const char *name)
{
    if (!name || !*name)
        return 0;

    if (gl_version_at_least(3, 0)) {
        gl_get_string_i_fn get_string_i =
            (gl_get_string_i_fn)eglGetProcAddress("glGetStringi");
        GLint num_exts = 0;

        if (get_string_i) {
            clear_gl_errors();
            glGetIntegerv(GL_NUM_EXTENSIONS, &num_exts);
            if (glGetError() == GL_NO_ERROR) {
                for (GLint i = 0; i < num_exts; i++) {
                    const char *ext =
                        (const char *)get_string_i(GL_EXTENSIONS, (GLuint)i);

                    if (ext && strcmp(ext, name) == 0)
                        return 1;
                }
            }
        }
    }

    clear_gl_errors();
    return extension_list_has((const char *)glGetString(GL_EXTENSIONS), name);
}

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

static int init_egl(struct egl_state *egl, int version, int webgl_compatible)
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
    EGLint context_attrs[7];
    EGLint nconfigs = 0;
    EGLint major = 0;
    EGLint minor = 0;
    int attr = 0;

    memset(egl, 0, sizeof(*egl));
    egl->display = EGL_NO_DISPLAY;
    egl->surface = EGL_NO_SURFACE;
    egl->context = EGL_NO_CONTEXT;
    egl->config = NULL;

    egl->display = get_surfaceless_display();
    if (egl->display == EGL_NO_DISPLAY) {
        printf("MESAANGLE-FAIL case=egl_setup step=get_display error=0x%x line=%d\n",
               eglGetError(), __LINE__);
        return 1;
    }
    if (!eglInitialize(egl->display, &major, &minor)) {
        printf("MESAANGLE-FAIL case=egl_setup step=initialize error=0x%x line=%d\n",
               eglGetError(), __LINE__);
        return 1;
    }
    if (!eglChooseConfig(egl->display, config_attrs, &egl->config, 1,
                         &nconfigs) || nconfigs < 1) {
        printf("MESAANGLE-FAIL case=egl_setup step=choose_config error=0x%x line=%d\n",
               eglGetError(), __LINE__);
        return 1;
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        printf("MESAANGLE-FAIL case=egl_setup step=bind_api error=0x%x line=%d\n",
               eglGetError(), __LINE__);
        return 1;
    }

    egl->surface = eglCreatePbufferSurface(egl->display, egl->config,
                                           pbuffer_attrs);
    for (int try_version = version; try_version >= 2; try_version--) {
        attr = 0;
        context_attrs[attr++] = EGL_CONTEXT_CLIENT_VERSION;
        context_attrs[attr++] = try_version;
        if (webgl_compatible) {
            context_attrs[attr++] = EGL_CONTEXT_WEBGL_COMPATIBILITY_ANGLE;
            context_attrs[attr++] = EGL_TRUE;
        }
        context_attrs[attr++] = EGL_NONE;

        egl->context = eglCreateContext(egl->display, egl->config,
                                        EGL_NO_CONTEXT, context_attrs);
        if (egl->context != EGL_NO_CONTEXT) {
            egl->requested_context_version = try_version;
            break;
        }
    }
    if (egl->surface == EGL_NO_SURFACE || egl->context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(egl->display, egl->surface, egl->surface,
                        egl->context)) {
        printf("MESAANGLE-FAIL case=egl_setup step=make_current webgl=%d error=0x%x line=%d\n",
               webgl_compatible, eglGetError(), __LINE__);
        return 1;
    }

    printf("MESAANGLE-PASS case=egl_setup webgl=%d egl=%d.%d gles_request=%d version=\"%s\" renderer=\"%s\"\n",
           webgl_compatible, major, minor, egl->requested_context_version,
           glGetString(GL_VERSION) ? (const char *)glGetString(GL_VERSION) :
           "(null)",
           glGetString(GL_RENDERER) ? (const char *)glGetString(GL_RENDERER) :
           "(null)");
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

static GLuint compile_shader(GLenum type, const char *source,
                             const char *case_name)
{
    GLuint shader = glCreateShader(type);
    GLint ok = GL_FALSE;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        GLsizei len = 0;

        glGetShaderInfoLog(shader, (GLsizei)sizeof(log), &len, log);
        printf("MESAANGLE-FAIL case=%s step=compile_shader type=0x%x log=\"%.*s\" line=%d\n",
               case_name, type, len, log, __LINE__);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint link_program(const char *vs_source, const char *fs_source,
                           const char *case_name)
{
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_source, case_name);
    GLuint fs = 0;
    GLuint program = 0;
    GLint ok = GL_FALSE;

    if (!vs)
        return 0;
    fs = compile_shader(GL_FRAGMENT_SHADER, fs_source, case_name);
    if (!fs) {
        glDeleteShader(vs);
        return 0;
    }

    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!ok) {
        char log[1024];
        GLsizei len = 0;

        glGetProgramInfoLog(program, (GLsizei)sizeof(log), &len, log);
        printf("MESAANGLE-FAIL case=%s step=link log=\"%.*s\" line=%d\n",
               case_name, len, log, __LINE__);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

static int expect_gl_error(const char *case_name, GLenum expected)
{
    GLenum got = glGetError();

    if (got != expected) {
        printf("MESAANGLE-FAIL case=%s error=0x%x expected=0x%x line=%d\n",
               case_name, got, expected, __LINE__);
        return 1;
    }
    printf("MESAANGLE-PASS case=%s error=0x%x\n", case_name, got);
    return 0;
}

static int check_float_case(const struct robust_fns *fns, GLuint program,
                            const struct uniform_case *test)
{
    GLfloat params[16];
    GLsizei length = LENGTH_SENTINEL;

    for (int i = 0; i < 16; i++)
        params[i] = -99.0f;

    clear_gl_errors();
    fns->get_uniformfv(program, test->location,
                       (GLsizei)(test->components * sizeof(params[0])),
                       &length, params);
    if (expect_gl_error(test->name, GL_NO_ERROR))
        return 1;
    if (length != test->components) {
        printf("MESAANGLE-FAIL case=%s length=%d expected=%d line=%d\n",
               test->name, length, test->components, __LINE__);
        return 1;
    }
    printf("MESAANGLE-PASS case=%s length=%d\n", test->name, length);
    return 0;
}

static int check_int_case(const struct robust_fns *fns, GLuint program,
                          const struct uniform_case *test)
{
    GLint params[8];
    GLsizei length = LENGTH_SENTINEL;

    for (int i = 0; i < 8; i++)
        params[i] = -99;

    clear_gl_errors();
    fns->get_uniformiv(program, test->location,
                       (GLsizei)(test->components * sizeof(params[0])),
                       &length, params);
    if (expect_gl_error(test->name, GL_NO_ERROR))
        return 1;
    if (length != test->components) {
        printf("MESAANGLE-FAIL case=%s length=%d expected=%d line=%d\n",
               test->name, length, test->components, __LINE__);
        return 1;
    }
    printf("MESAANGLE-PASS case=%s length=%d\n", test->name, length);
    return 0;
}

static int check_uint_case(const struct robust_fns *fns, GLuint program,
                           const struct uniform_case *test)
{
    GLuint params[8];
    GLsizei length = LENGTH_SENTINEL;

    for (int i = 0; i < 8; i++)
        params[i] = 0xbadbad00u;

    clear_gl_errors();
    fns->get_uniformuiv(program, test->location,
                        (GLsizei)(test->components * sizeof(params[0])),
                        &length, params);
    if (expect_gl_error(test->name, GL_NO_ERROR))
        return 1;
    if (length != test->components) {
        printf("MESAANGLE-FAIL case=%s length=%d expected=%d line=%d\n",
               test->name, length, test->components, __LINE__);
        return 1;
    }
    printf("MESAANGLE-PASS case=%s length=%d\n", test->name, length);
    return 0;
}

static int check_invalid_location(const struct robust_fns *fns, GLuint program)
{
    GLfloat params[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    GLsizei length = LENGTH_SENTINEL;

    clear_gl_errors();
    fns->get_uniformfv(program, -1, (GLsizei)sizeof(params), &length, params);
    if (expect_gl_error("robust_uniform_location_minus_one",
                        GL_INVALID_OPERATION))
        return 1;
    if (length != LENGTH_SENTINEL) {
        printf("MESAANGLE-FAIL case=robust_uniform_location_minus_one length=%d expected=sentinel line=%d\n",
               length, __LINE__);
        return 1;
    }
    printf("MESAANGLE-PASS case=robust_uniform_location_minus_one length=sentinel\n");
    return 0;
}

static int check_small_buffer(const struct robust_fns *fns, GLuint program,
                              GLint location)
{
    GLfloat params[4] = { 7.0f, 7.0f, 7.0f, 7.0f };
    GLsizei length = LENGTH_SENTINEL;

    clear_gl_errors();
    fns->get_uniformfv(program, location, (GLsizei)(sizeof(GLfloat) * 3),
                       &length, params);
    if (expect_gl_error("robust_uniform_too_small_buffer",
                        GL_INVALID_OPERATION))
        return 1;
    if (length != LENGTH_SENTINEL) {
        printf("MESAANGLE-FAIL case=robust_uniform_too_small_buffer length=%d expected=sentinel line=%d\n",
               length, __LINE__);
        return 1;
    }
    printf("MESAANGLE-PASS case=robust_uniform_too_small_buffer length=sentinel\n");
    return 0;
}

static int run_webgl_extension_tests(void)
{
    struct egl_state normal;
    struct egl_state webgl;
    int failed = 0;
    int has_egl_webgl = 0;

    if (init_egl(&normal, 3, 0))
        return 1;

    if (gl_has_extension("GL_ANGLE_webgl_compatibility")) {
        printf("MESAANGLE-FAIL case=normal_context_webgl_extension present=1 expected=0 line=%d\n",
               __LINE__);
        failed = 1;
    } else {
        printf("MESAANGLE-PASS case=normal_context_webgl_extension present=0\n");
    }
    has_egl_webgl = egl_has_display_extension(
        normal.display, "EGL_ANGLE_create_context_webgl_compatibility");
    destroy_egl(&normal);

    if (!has_egl_webgl) {
        printf("MESAANGLE-SKIP case=webgl_context reason=missing_egl_extension rc=%d line=%d\n",
               SKIP_RC, __LINE__);
        return failed;
    }

    if (init_egl(&webgl, 3, 1))
        return 1;
    if (!gl_has_extension("GL_ANGLE_webgl_compatibility")) {
        printf("MESAANGLE-FAIL case=webgl_context_webgl_extension present=0 expected=1 line=%d\n",
               __LINE__);
        failed = 1;
    } else {
        printf("MESAANGLE-PASS case=webgl_context_webgl_extension present=1\n");
    }
    destroy_egl(&webgl);
    return failed;
}

static int load_robust_fns(struct robust_fns *fns)
{
    memset(fns, 0, sizeof(*fns));
    fns->get_uniformfv = (gl_get_uniformfv_robust_angle_fn)
        eglGetProcAddress("glGetUniformfvRobustANGLE");
    fns->get_uniformiv = (gl_get_uniformiv_robust_angle_fn)
        eglGetProcAddress("glGetUniformivRobustANGLE");
    fns->get_uniformuiv = (gl_get_uniformuiv_robust_angle_fn)
        eglGetProcAddress("glGetUniformuivRobustANGLE");
    fns->uniform1ui = (gl_uniform1ui_fn)eglGetProcAddress("glUniform1ui");
    fns->uniform4uiv = (gl_uniform4uiv_fn)eglGetProcAddress("glUniform4uiv");

    if (!gl_has_extension("GL_ANGLE_robust_client_memory")) {
        printf("MESAANGLE-SKIP case=robust_client_memory reason=missing_gl_extension rc=%d line=%d\n",
               SKIP_RC, __LINE__);
        return SKIP_RC;
    }
    if (!fns->get_uniformfv || !fns->get_uniformiv) {
        printf("MESAANGLE-SKIP case=robust_client_memory reason=missing_required_procs fv=%d iv=%d rc=%d line=%d\n",
               fns->get_uniformfv != NULL, fns->get_uniformiv != NULL,
               SKIP_RC, __LINE__);
        return SKIP_RC;
    }
    printf("MESAANGLE-PASS case=robust_client_memory_procs fv=1 iv=1 uiv=%d\n",
           fns->get_uniformuiv != NULL);
    return 0;
}

static int run_float_int_uniform_tests(const struct robust_fns *fns)
{
    static const char *vs_source =
        "attribute vec4 a_pos;\n"
        "void main() { gl_Position = a_pos; }\n";
    static const char *fs_source =
        "precision highp float;\n"
        "uniform float u_float;\n"
        "uniform vec4 u_vec4;\n"
        "uniform mat3 u_mat3;\n"
        "uniform int u_int;\n"
        "uniform ivec3 u_ivec3;\n"
        "void main() {\n"
        "  float f = u_float + u_vec4.x + u_mat3[0][0] +\n"
        "            float(u_int) + float(u_ivec3.x);\n"
        "  gl_FragColor = vec4(f, 0.0, 0.0, 1.0);\n"
        "}\n";
    GLuint program = link_program(vs_source, fs_source, "robust_uniform_link");
    GLfloat mat3_value[9] = {
        1.0f, 2.0f, 3.0f,
        4.0f, 5.0f, 6.0f,
        7.0f, 8.0f, 9.0f
    };
    GLint ivec3_value[3] = { 5, 6, 7 };
    struct uniform_case float_cases[3];
    struct uniform_case int_cases[2];
    int failed = 0;

    if (!program)
        return 1;

    glUseProgram(program);
    float_cases[0].name = "robust_uniform_float_scalar";
    float_cases[0].location = glGetUniformLocation(program, "u_float");
    float_cases[0].components = 1;
    float_cases[1].name = "robust_uniform_float_vec4";
    float_cases[1].location = glGetUniformLocation(program, "u_vec4");
    float_cases[1].components = 4;
    float_cases[2].name = "robust_uniform_float_mat3";
    float_cases[2].location = glGetUniformLocation(program, "u_mat3");
    float_cases[2].components = 9;
    int_cases[0].name = "robust_uniform_int_scalar";
    int_cases[0].location = glGetUniformLocation(program, "u_int");
    int_cases[0].components = 1;
    int_cases[1].name = "robust_uniform_int_ivec3";
    int_cases[1].location = glGetUniformLocation(program, "u_ivec3");
    int_cases[1].components = 3;

    for (int i = 0; i < 3; i++) {
        if (float_cases[i].location < 0) {
            printf("MESAANGLE-FAIL case=%s step=get_location location=%d line=%d\n",
                   float_cases[i].name, float_cases[i].location, __LINE__);
            failed = 1;
        }
    }
    for (int i = 0; i < 2; i++) {
        if (int_cases[i].location < 0) {
            printf("MESAANGLE-FAIL case=%s step=get_location location=%d line=%d\n",
                   int_cases[i].name, int_cases[i].location, __LINE__);
            failed = 1;
        }
    }
    if (failed) {
        glDeleteProgram(program);
        return 1;
    }

    glUniform1f(float_cases[0].location, 1.25f);
    glUniform4f(float_cases[1].location, 2.0f, 3.0f, 4.0f, 5.0f);
    glUniformMatrix3fv(float_cases[2].location, 1, GL_FALSE, mat3_value);
    glUniform1i(int_cases[0].location, 3);
    glUniform3iv(int_cases[1].location, 1, ivec3_value);

    for (int i = 0; i < 3; i++)
        failed |= check_float_case(fns, program, &float_cases[i]);
    for (int i = 0; i < 2; i++)
        failed |= check_int_case(fns, program, &int_cases[i]);
    failed |= check_invalid_location(fns, program);
    failed |= check_small_buffer(fns, program, float_cases[1].location);

    glDeleteProgram(program);
    return failed;
}

static int run_uint_uniform_tests(const struct robust_fns *fns)
{
    static const char *vs_source =
        "#version 300 es\n"
        "in vec4 a_pos;\n"
        "void main() { gl_Position = a_pos; }\n";
    static const char *fs_source =
        "#version 300 es\n"
        "precision highp float;\n"
        "uniform uint u_uint;\n"
        "uniform uvec4 u_uvec4;\n"
        "out vec4 frag_color;\n"
        "void main() {\n"
        "  frag_color = vec4(float(u_uint + u_uvec4.x), 0.0, 0.0, 1.0);\n"
        "}\n";
    GLuint program = 0;
    GLuint uvec4_value[4] = { 11u, 12u, 13u, 14u };
    struct uniform_case uint_cases[2];
    int failed = 0;

    if (!gl_version_at_least(3, 0) || !fns->get_uniformuiv ||
        !fns->uniform1ui || !fns->uniform4uiv) {
        printf("MESAANGLE-SKIP case=robust_uniform_uint reason=missing_es3_or_procs es3=%d uiv=%d uniform1ui=%d uniform4uiv=%d rc=%d line=%d\n",
               gl_version_at_least(3, 0), fns->get_uniformuiv != NULL,
               fns->uniform1ui != NULL, fns->uniform4uiv != NULL, SKIP_RC,
               __LINE__);
        return 0;
    }

    program = link_program(vs_source, fs_source, "robust_uniform_uint_link");
    if (!program)
        return 1;
    glUseProgram(program);
    uint_cases[0].name = "robust_uniform_uint_scalar";
    uint_cases[0].location = glGetUniformLocation(program, "u_uint");
    uint_cases[0].components = 1;
    uint_cases[1].name = "robust_uniform_uint_uvec4";
    uint_cases[1].location = glGetUniformLocation(program, "u_uvec4");
    uint_cases[1].components = 4;

    for (int i = 0; i < 2; i++) {
        if (uint_cases[i].location < 0) {
            printf("MESAANGLE-FAIL case=%s step=get_location location=%d line=%d\n",
                   uint_cases[i].name, uint_cases[i].location, __LINE__);
            failed = 1;
        }
    }
    if (failed) {
        glDeleteProgram(program);
        return 1;
    }

    fns->uniform1ui(uint_cases[0].location, 10u);
    fns->uniform4uiv(uint_cases[1].location, 1, uvec4_value);
    for (int i = 0; i < 2; i++)
        failed |= check_uint_case(fns, program, &uint_cases[i]);

    glDeleteProgram(program);
    return failed;
}

static int run_robust_uniform_tests(void)
{
    struct egl_state egl;
    struct robust_fns fns;
    int loaded;
    int failed = 0;

    if (init_egl(&egl, 3, 0))
        return 1;

    loaded = load_robust_fns(&fns);
    if (loaded == SKIP_RC) {
        destroy_egl(&egl);
        return 0;
    }
    if (loaded) {
        destroy_egl(&egl);
        return 1;
    }

    failed |= run_float_int_uniform_tests(&fns);
    failed |= run_uint_uniform_tests(&fns);
    destroy_egl(&egl);
    return failed;
}

int main(void)
{
    int failed = 0;

    failed |= run_webgl_extension_tests();
    failed |= run_robust_uniform_tests();

    if (failed) {
        printf("MESAANGLE-FAIL case=summary\n");
        return 1;
    }
    printf("MESAANGLE-PASS case=summary\n");
    return 0;
}
