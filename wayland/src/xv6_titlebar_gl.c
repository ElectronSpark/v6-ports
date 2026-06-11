#include "xv6_titlebar_gl.h"

#include "font8x16.h"
#include "xv6_titlebar.h"

#include <stdint.h>
#include <string.h>

struct titlebar_vertex {
    GLfloat x;
    GLfloat y;
    GLfloat z;
    GLfloat r;
    GLfloat g;
    GLfloat b;
    GLfloat a;
};

static void emit_rect_px(struct titlebar_vertex *vertices, int *count,
                         int max_count, int width, int height,
                         int x0, int y0, int x1, int y1,
                         GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    GLfloat nx0, nx1, ny0, ny1;
    struct titlebar_vertex rect[6];

    if (*count + 6 > max_count || width <= 0 || height <= 0)
        return;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > width)
        x1 = width;
    if (y1 > height)
        y1 = height;
    if (x0 >= x1 || y0 >= y1)
        return;

    nx0 = (2.0f * (GLfloat)x0 / (GLfloat)width) - 1.0f;
    nx1 = (2.0f * (GLfloat)x1 / (GLfloat)width) - 1.0f;
    ny0 = 1.0f - (2.0f * (GLfloat)y0 / (GLfloat)height);
    ny1 = 1.0f - (2.0f * (GLfloat)y1 / (GLfloat)height);

    rect[0] = (struct titlebar_vertex){ nx0, ny0, 0.0f, r, g, b, a };
    rect[1] = (struct titlebar_vertex){ nx1, ny0, 0.0f, r, g, b, a };
    rect[2] = (struct titlebar_vertex){ nx0, ny1, 0.0f, r, g, b, a };
    rect[3] = (struct titlebar_vertex){ nx1, ny0, 0.0f, r, g, b, a };
    rect[4] = (struct titlebar_vertex){ nx1, ny1, 0.0f, r, g, b, a };
    rect[5] = (struct titlebar_vertex){ nx0, ny1, 0.0f, r, g, b, a };
    memcpy(&vertices[*count], rect, sizeof(rect));
    *count += 6;
}

static void emit_char_px(struct titlebar_vertex *vertices, int *count,
                         int max_count, int width, int height,
                         int x, int y, char ch,
                         GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    const uint8_t *glyph;

    if (ch < 0x20 || ch > 0x7e)
        ch = '?';
    glyph = font8x16_data[ch - 0x20];
    for (int row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];

        for (int col = 0; col < 8; col++) {
            if (bits & (0x80u >> col))
                emit_rect_px(vertices, count, max_count, width, height,
                             x + col, y + row, x + col + 1, y + row + 1,
                             r, g, b, a);
        }
    }
}

static void emit_text_fit(struct titlebar_vertex *vertices, int *count,
                          int max_count, int width, int height,
                          int x, int y, int max_w, const char *text,
                          GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    int used = 0;

    if (!text || max_w <= 0)
        return;
    for (const char *p = text; *p && used + 8 <= max_w; p++, used += 8)
        emit_char_px(vertices, count, max_count, width, height,
                     x + used, y, *p, r, g, b, a);
}

static void emit_button_text(struct titlebar_vertex *vertices, int *count,
                             int max_count, int width, int height,
                             int x, const char *label)
{
    int len = (int)strlen(label);
    int tx = x + (XV6_TITLEBAR_CONTROL_W - len * 8) / 2;

    if (tx < x + 2)
        tx = x + 2;
    emit_text_fit(vertices, count, max_count, width, height, tx, 7,
                  XV6_TITLEBAR_CONTROL_W - 4, label,
                  0.96f, 0.97f, 0.98f, 1.0f);
}

void xv6_titlebar_gl_draw(int width, int height,
                          const char *title, int maximized,
                          GLuint program, GLint attr_pos, GLint attr_color,
                          GLint pos_components)
{
    struct titlebar_vertex vertices[8192];
    int max_count = (int)(sizeof(vertices) / sizeof(vertices[0]));
    int count = 0;
    int close_x = width - XV6_TITLEBAR_CONTROL_W;
    int max_x = close_x - XV6_TITLEBAR_CONTROL_W;
    int min_x = max_x - XV6_TITLEBAR_CONTROL_W;
    int title_limit = min_x - 18;

    if (!program || attr_pos < 0 || attr_color < 0 ||
        width <= XV6_TITLEBAR_CONTROL_W * 3 ||
        height < XV6_TITLEBAR_HEIGHT)
        return;
    if (pos_components != 2 && pos_components != 3)
        return;

    emit_rect_px(vertices, &count, max_count, width, height,
                 0, 0, width, XV6_TITLEBAR_HEIGHT,
                 0.14f, 0.20f, 0.28f, 1.0f);
    emit_rect_px(vertices, &count, max_count, width, height,
                 0, XV6_TITLEBAR_HEIGHT - 1, width, XV6_TITLEBAR_HEIGHT,
                 0.37f, 0.46f, 0.56f, 1.0f);
    emit_text_fit(vertices, &count, max_count, width, height, 10, 7,
                  title_limit, title, 0.96f, 0.97f, 0.98f, 1.0f);
    emit_rect_px(vertices, &count, max_count, width, height,
                 min_x, 0, max_x, XV6_TITLEBAR_HEIGHT,
                 0.18f, 0.26f, 0.35f, 1.0f);
    emit_rect_px(vertices, &count, max_count, width, height,
                 max_x, 0, close_x, XV6_TITLEBAR_HEIGHT,
                 0.18f, 0.26f, 0.35f, 1.0f);
    emit_rect_px(vertices, &count, max_count, width, height,
                 close_x, 0, width, XV6_TITLEBAR_HEIGHT,
                 0.44f, 0.22f, 0.24f, 1.0f);
    emit_button_text(vertices, &count, max_count, width, height, min_x, "-");
    emit_button_text(vertices, &count, max_count, width, height, max_x,
                     maximized ? "[]" : "+");
    emit_button_text(vertices, &count, max_count, width, height, close_x, "x");

    glUseProgram(program);
    glVertexAttribPointer((GLuint)attr_pos, pos_components, GL_FLOAT,
                          GL_FALSE, sizeof(vertices[0]), &vertices[0].x);
    glVertexAttribPointer((GLuint)attr_color, 4, GL_FLOAT, GL_FALSE,
                          sizeof(vertices[0]), &vertices[0].r);
    glEnableVertexAttribArray((GLuint)attr_pos);
    glEnableVertexAttribArray((GLuint)attr_color);
    glDrawArrays(GL_TRIANGLES, 0, count);
}
