#include "gl_program.h"

#include <stdio.h>

static GLuint compile_shader(const char *log_prefix, GLenum type,
                             const char *src)
{
    GLuint shader = glCreateShader(type);
    GLint ok = GL_FALSE;

    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
        fprintf(stderr, "%s: shader compile failed\n", log_prefix);
    return shader;
}

GLuint xv6_gl_link_program(const char *log_prefix,
                           const char *vertex_shader,
                           const char *fragment_shader)
{
    GLuint vshader = compile_shader(log_prefix, GL_VERTEX_SHADER,
                                    vertex_shader);
    GLuint fshader = compile_shader(log_prefix, GL_FRAGMENT_SHADER,
                                    fragment_shader);
    GLuint program = glCreateProgram();
    GLint ok = GL_FALSE;

    glAttachShader(program, vshader);
    glAttachShader(program, fshader);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    glDeleteShader(vshader);
    glDeleteShader(fshader);
    if (!ok) {
        fprintf(stderr, "%s: program link failed\n", log_prefix);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}
