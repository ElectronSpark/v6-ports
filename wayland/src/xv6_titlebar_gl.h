#ifndef XV6_TITLEBAR_GL_H
#define XV6_TITLEBAR_GL_H

#include <GLES2/gl2.h>

void xv6_titlebar_gl_draw(int width, int height,
                          const char *title, int maximized,
                          GLuint program, GLint attr_pos, GLint attr_color,
                          GLint pos_components);

#endif
