#include "gl_fps_overlay.h"

#include <stdint.h>
#include <string.h>

struct overlay_vertex {
    GLfloat x;
    GLfloat y;
    GLfloat z;
    GLfloat r;
    GLfloat g;
    GLfloat b;
    GLfloat a;
};

static void overlay_emit_rect(struct overlay_vertex *vertices, int *count,
                              float x0, float y0, float x1, float y1,
                              float r, float g, float b, float a)
{
    struct overlay_vertex rect[6] = {
        { x0, y0, 0.0f, r, g, b, a },
        { x1, y0, 0.0f, r, g, b, a },
        { x0, y1, 0.0f, r, g, b, a },
        { x1, y0, 0.0f, r, g, b, a },
        { x1, y1, 0.0f, r, g, b, a },
        { x0, y1, 0.0f, r, g, b, a },
    };

    memcpy(&vertices[*count], rect, sizeof(rect));
    *count += 6;
}

static uint8_t overlay_segments_for_char(char ch)
{
    switch (ch) {
    case '0': return 0x3f;
    case '1': return 0x06;
    case '2': return 0x5b;
    case '3': return 0x4f;
    case '4': return 0x66;
    case '5': return 0x6d;
    case '6': return 0x7d;
    case '7': return 0x07;
    case '8': return 0x7f;
    case '9': return 0x6f;
    case 'C': return 0x39;
    case 'F': return 0x71;
    case 'P': return 0x73;
    case 'R': return 0x77;
    case 'S': return 0x6d;
    case 'T': return 0x78;
    case '-': return 0x40;
    default: return 0;
    }
}

static void overlay_emit_glyph(struct overlay_vertex *vertices, int *count,
                               char ch, float x, float y, float w, float h)
{
    float t = w * 0.16f;
    float r = 0.92f;
    float g = 0.97f;
    float b = 1.00f;
    float a = 0.94f;
    uint8_t segments;

    if (ch == '.') {
        overlay_emit_rect(vertices, count, x + w * 0.38f, y - h + t,
                          x + w * 0.62f, y - h + t * 2.1f, r, g, b, a);
        return;
    }
    segments = overlay_segments_for_char(ch);
    if (segments & 0x01)
        overlay_emit_rect(vertices, count, x + t, y - t, x + w - t, y,
                          r, g, b, a);
    if (segments & 0x02)
        overlay_emit_rect(vertices, count, x + w - t, y - h * 0.5f + t,
                          x + w, y - t, r, g, b, a);
    if (segments & 0x04)
        overlay_emit_rect(vertices, count, x + w - t, y - h + t,
                          x + w, y - h * 0.5f - t, r, g, b, a);
    if (segments & 0x08)
        overlay_emit_rect(vertices, count, x + t, y - h, x + w - t,
                          y - h + t, r, g, b, a);
    if (segments & 0x10)
        overlay_emit_rect(vertices, count, x, y - h + t, x + t,
                          y - h * 0.5f - t, r, g, b, a);
    if (segments & 0x20)
        overlay_emit_rect(vertices, count, x, y - h * 0.5f + t, x + t,
                          y - t, r, g, b, a);
    if (segments & 0x40)
        overlay_emit_rect(vertices, count, x + t, y - h * 0.5f - t * 0.5f,
                          x + w - t, y - h * 0.5f + t * 0.5f,
                          r, g, b, a);
}

void gl_fps_overlay_draw(const char *text, GLuint program,
                         GLint attr_pos, GLint attr_color)
{
    struct overlay_vertex vertices[768];
    int count = 0;
    float w = 0.078f;
    float h = 0.155f;
    float gap = 0.019f;
    float x = -0.90f;
    float y = 0.82f;

    if (text == NULL || text[0] == '\0')
        return;

    overlay_emit_rect(vertices, &count, -0.95f, 0.93f, 0.50f, 0.55f,
                      0.00f, 0.00f, 0.00f, 0.82f);
    overlay_emit_rect(vertices, &count, -0.95f, 0.93f, 0.50f, 0.89f,
                      0.10f, 0.78f, 1.00f, 0.94f);
    overlay_emit_rect(vertices, &count, -0.95f, 0.59f, 0.50f, 0.55f,
                      0.10f, 0.78f, 1.00f, 0.94f);
    overlay_emit_rect(vertices, &count, -0.95f, 0.93f, -0.91f, 0.55f,
                      0.10f, 0.78f, 1.00f, 0.94f);
    overlay_emit_rect(vertices, &count, 0.46f, 0.93f, 0.50f, 0.55f,
                      0.10f, 0.78f, 1.00f, 0.94f);

    for (const char *p = text; *p && count + 42 < (int)(sizeof(vertices) / sizeof(vertices[0])); p++) {
        if (*p == ' ') {
            x += w * 0.55f;
            continue;
        }
        overlay_emit_glyph(vertices, &count, *p, x, y, w, h);
        x += w + gap;
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(program);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glVertexAttribPointer((GLuint)attr_pos, 3, GL_FLOAT, GL_FALSE,
                          sizeof(vertices[0]), &vertices[0].x);
    glVertexAttribPointer((GLuint)attr_color, 4, GL_FLOAT, GL_FALSE,
                          sizeof(vertices[0]), &vertices[0].r);
    glEnableVertexAttribArray((GLuint)attr_pos);
    glEnableVertexAttribArray((GLuint)attr_color);
    glDrawArrays(GL_TRIANGLES, 0, count);
    glDisable(GL_BLEND);
}
