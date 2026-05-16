#ifndef XV6_PIXEL_FPS_OVERLAY_H
#define XV6_PIXEL_FPS_OVERLAY_H

#include <stdint.h>

#define PIXEL_FPS_OVERLAY_TEXT_MAX 16

void pixel_fps_overlay_draw(uint32_t *pixels, int width, int height,
                            int stride, const char *text);

#endif
