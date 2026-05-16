#ifndef XV6_GL_FPS_OVERLAY_H
#define XV6_GL_FPS_OVERLAY_H

#include <GLES2/gl2.h>

#define GL_FPS_OVERLAY_TEXT_MAX 16

void gl_fps_overlay_draw(const char *text, GLuint program,
                         GLint attr_pos, GLint attr_color);

#endif
