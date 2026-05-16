#ifndef XV6_GL_PROGRAM_H
#define XV6_GL_PROGRAM_H

#include <GLES2/gl2.h>

GLuint xv6_gl_link_program(const char *log_prefix,
                           const char *vertex_shader,
                           const char *fragment_shader);

#endif
