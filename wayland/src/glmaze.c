/*
 * glmaze.c - xv6 Wayland/EGL port of Till Harbaum's glMaze.
 *
 * Original game: glMaze 1.1a, (c) 2001 Till Harbaum, GPL.  This xv6 port
 * keeps the maze format and single-player 3D maze game idea, but replaces
 * SDL/GLU/fixed-function OpenGL with a small Wayland/EGL/OpenGL ES renderer.
 */

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "font8x16.h"
#include "xdg-shell-client-protocol.h"
#include "xv6_present_buffer.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <math.h>
#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>

#define XV6_DRM_RENDER_NODE "/dev/dri/renderD128"
#define DRM_IOCTL_VIRTGPU_GETPARAM 0xc0106443UL
#define VIRTGPU_PARAM_3D_FEATURES  1
#define MAX_EXT 32
#define OB_X 0
#define OB_Y 1
#define OB_Z 2
#define KEY_ESC 1
#define KEY_W 17
#define KEY_A 30
#define KEY_S 31
#define KEY_D 32
#define KEY_SPACE 57
#define KEY_LEFT 105
#define KEY_RIGHT 106
#define KEY_UP 103
#define KEY_DOWN 108
#define TITLEBAR_H 30
#define TITLEBAR_CONTROL_W 34
#define TITLEBAR_TITLE "GL Maze"

#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif

struct vertex {
    float pos[3];
    float color[3];
};

struct drm_virtgpu_getparam_compat {
    uint64_t param;
    uint64_t value;
};

struct maze {
    int extent[3];
    int wall[3][MAX_EXT + 1][MAX_EXT + 1][MAX_EXT + 1];
    unsigned char solid[MAX_EXT][MAX_EXT][MAX_EXT];
};

struct app {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct wl_keyboard *keyboard;
    struct xdg_wm_base *wm_base;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct wl_egl_window *egl_window;
    EGLDisplay egl_display;
    EGLSurface egl_surface;
    EGLContext egl_context;
    EGLConfig egl_config;
    int width;
    int height;
    int configured;
    int running;
    int maximized;
    int pointer_x;
    int pointer_y;
    int keys[256];

    GLuint program;
    GLuint vbo;
    GLint attr_pos;
    GLint attr_color;
    GLint uniform_mvp;
    struct vertex *verts;
    size_t vert_count;
    size_t vert_cap;
    struct xv6_present_buffer present_buf;
    int present_buf_ready;
    uint8_t *readback;
    size_t readback_size;
    GLenum read_format;

    struct maze maze;
    float px;
    float py;
    float yaw;
    int target_x;
    int target_y;
    int score;
    uint64_t start_ms;
    uint64_t last_title_ms;
    int frames;
    char renderer[64];
};

static void draw_titlebar(struct app *app);

static uint64_t now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ull + (uint64_t)tv.tv_usec / 1000ull;
}

static int xv6_virgl_available(void)
{
    uint64_t value = 0;
    struct drm_virtgpu_getparam_compat req = {
        .param = VIRTGPU_PARAM_3D_FEATURES,
        .value = (uint64_t)(uintptr_t)&value,
    };
    int fd = open(XV6_DRM_RENDER_NODE, O_RDWR | O_CLOEXEC);
    int ok;

    if (fd < 0)
        return 0;
    ok = ioctl(fd, DRM_IOCTL_VIRTGPU_GETPARAM, &req) == 0 && value != 0;
    close(fd);
    return ok;
}

static int key_down(struct app *app, int code)
{
    if (code >= 0 && code < 256 && app->keys[code])
        return 1;
    /*
     * wl_keyboard key events are specified as evdev keycodes, but some
     * clients/toolkits think in XKB keycodes, which are evdev + 8. Accept
     * both so controls keep working if wlcomp grows stricter keymap parity.
     */
    code += 8;
    return code >= 0 && code < 256 && app->keys[code];
}

static void configure_gl_driver_env(void)
{
    int virgl = xv6_virgl_available();

    setenv("LIBGL_ALWAYS_SOFTWARE", virgl ? "0" : "1", 1);
    if (virgl) {
        setenv("GALLIUM_DRIVER", "virgl", 1);
    } else {
        unsetenv("GALLIUM_DRIVER");
        unsetenv("MESA_LOADER_DRIVER_OVERRIDE");
    }
}

static void mat4_identity(float m[16])
{
    memset(m, 0, sizeof(float) * 16);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void mat4_mul(float out[16], const float a[16], const float b[16])
{
    float r[16];
    for (int c = 0; c < 4; c++) {
        for (int row = 0; row < 4; row++) {
            r[c * 4 + row] =
                a[0 * 4 + row] * b[c * 4 + 0] +
                a[1 * 4 + row] * b[c * 4 + 1] +
                a[2 * 4 + row] * b[c * 4 + 2] +
                a[3 * 4 + row] * b[c * 4 + 3];
        }
    }
    memcpy(out, r, sizeof(r));
}

static void mat4_perspective(float m[16], float fovy, float aspect,
                             float znear, float zfar)
{
    float f = 1.0f / tanf(fovy * 0.5f);
    mat4_identity(m);
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (zfar + znear) / (znear - zfar);
    m[11] = -1.0f;
    m[14] = (2.0f * zfar * znear) / (znear - zfar);
    m[15] = 0.0f;
}

static void vec3_cross(float out[3], const float a[3], const float b[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static void vec3_norm(float v[3])
{
    float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len > 0.00001f) {
        v[0] /= len;
        v[1] /= len;
        v[2] /= len;
    }
}

static float vec3_dot(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void mat4_look_at(float m[16], const float eye[3],
                         const float center[3], const float up[3])
{
    float f[3] = { center[0] - eye[0], center[1] - eye[1],
                   center[2] - eye[2] };
    float s[3], u[3];

    vec3_norm(f);
    vec3_cross(s, f, up);
    vec3_norm(s);
    vec3_cross(u, s, f);

    mat4_identity(m);
    m[0] = s[0];
    m[4] = s[1];
    m[8] = s[2];
    m[1] = u[0];
    m[5] = u[1];
    m[9] = u[2];
    m[2] = -f[0];
    m[6] = -f[1];
    m[10] = -f[2];
    m[12] = -vec3_dot(s, eye);
    m[13] = -vec3_dot(u, eye);
    m[14] = vec3_dot(f, eye);
}

static int ensure_vertices(struct app *app, size_t add)
{
    if (app->vert_count + add <= app->vert_cap)
        return 0;
    size_t cap = app->vert_cap ? app->vert_cap * 2 : 8192;
    while (cap < app->vert_count + add)
        cap *= 2;
    struct vertex *next = realloc(app->verts, cap * sizeof(*next));
    if (!next)
        return -1;
    app->verts = next;
    app->vert_cap = cap;
    return 0;
}

static void color_for_wall(int id, float shade, float out[3])
{
    switch (id) {
    case '#': out[0] = 0.38f; out[1] = 0.48f; out[2] = 0.55f; break;
    case 'D': out[0] = 0.35f; out[1] = 0.45f; out[2] = 0.95f; break;
    case 'R': out[0] = 0.90f; out[1] = 0.25f; out[2] = 0.25f; break;
    case 'Y': out[0] = 0.95f; out[1] = 0.78f; out[2] = 0.20f; break;
    case 'G': out[0] = 0.20f; out[1] = 0.75f; out[2] = 0.35f; break;
    case 'C': out[0] = 0.15f; out[1] = 0.75f; out[2] = 0.85f; break;
    case 'B': out[0] = 0.25f; out[1] = 0.35f; out[2] = 0.95f; break;
    case 'M': out[0] = 0.75f; out[1] = 0.30f; out[2] = 0.85f; break;
    case 'X': out[0] = 0.65f; out[1] = 0.42f; out[2] = 0.24f; break;
    default: out[0] = 0.55f; out[1] = 0.55f; out[2] = 0.60f; break;
    }
    out[0] *= shade;
    out[1] *= shade;
    out[2] *= shade;
}

static void push_vertex(struct app *app, float x, float y, float z,
                        const float c[3])
{
    struct vertex *v = &app->verts[app->vert_count++];
    v->pos[0] = x;
    v->pos[1] = y;
    v->pos[2] = z;
    v->color[0] = c[0];
    v->color[1] = c[1];
    v->color[2] = c[2];
}

static int push_quad(struct app *app, const float a[3], const float b[3],
                     const float c[3], const float d[3], int id, float shade)
{
    float col[3];
    if (ensure_vertices(app, 6) != 0)
        return -1;
    color_for_wall(id, shade, col);
    push_vertex(app, a[0], a[1], a[2], col);
    push_vertex(app, b[0], b[1], b[2], col);
    push_vertex(app, c[0], c[1], c[2], col);
    push_vertex(app, a[0], a[1], a[2], col);
    push_vertex(app, c[0], c[1], c[2], col);
    push_vertex(app, d[0], d[1], d[2], col);
    return 0;
}

static void push_cube(struct app *app, float x0, float y0, float z0,
                      float x1, float y1, float z1, int id)
{
    float a[3], b[3], c[3], d[3];
    a[0] = x0; a[1] = y0; a[2] = z0;
    b[0] = x1; b[1] = y0; b[2] = z0;
    c[0] = x1; c[1] = y1; c[2] = z0;
    d[0] = x0; d[1] = y1; d[2] = z0;
    push_quad(app, a, b, c, d, id, 0.8f);
    a[2] = b[2] = c[2] = d[2] = z1;
    push_quad(app, d, c, b, a, id, 0.7f);
    a[0] = x0; a[1] = y0; a[2] = z0;
    b[0] = x0; b[1] = y0; b[2] = z1;
    c[0] = x0; c[1] = y1; c[2] = z1;
    d[0] = x0; d[1] = y1; d[2] = z0;
    push_quad(app, a, b, c, d, id, 0.65f);
    a[0] = b[0] = c[0] = d[0] = x1;
    push_quad(app, d, c, b, a, id, 0.9f);
    a[0] = x0; a[1] = y1; a[2] = z0;
    b[0] = x1; b[1] = y1; b[2] = z0;
    c[0] = x1; c[1] = y1; c[2] = z1;
    d[0] = x0; d[1] = y1; d[2] = z1;
    push_quad(app, a, b, c, d, id, 1.0f);
    a[1] = b[1] = c[1] = d[1] = y0;
    push_quad(app, d, c, b, a, id, 0.45f);
}

static FILE *open_maze_file(const char *path)
{
    const char *prefixes[] = {
        "",
        "/share/glmaze/",
        "/usr/share/glmaze/",
        "/root/share/glmaze/",
        NULL,
    };
    char full[256];

    for (int i = 0; prefixes[i]; i++) {
        snprintf(full, sizeof(full), "%s%s", prefixes[i], path);
        FILE *fp = fopen(full, "r");
        if (fp)
            return fp;
    }
    return NULL;
}

static int load_maze(struct maze *m, const char *path)
{
    FILE *fp = open_maze_file(path);
    int known[256];
    int box[256][6];
    char line[256];
    int last_blank = 1;
    int linelen = -1;

    if (!fp) {
        fprintf(stderr, "glmaze: open %s failed: %s\n", path, strerror(errno));
        return -1;
    }
    memset(m, 0, sizeof(*m));
    for (int i = 0; i < 256; i++) {
        known[i] = 0;
        box[i][0] = -1;
    }
    m->extent[OB_Z] = -1;

    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '\0' || *p == '\n' || *p == ';') {
            last_blank = 1;
            continue;
        }
        if (last_blank)
            m->extent[OB_Y] = 0;
        if (strncmp(p, "texture[", 8) == 0) {
            int key = (unsigned char)p[8];
            known[key] = strstr(p, "none") ? 0 : key;
        } else if (strncmp(p, "box[", 4) == 0) {
            int key = (unsigned char)p[4];
            if (strstr(p, "none")) {
                box[key][0] = 0;
            } else {
                for (int i = 0; i < 6; i++)
                    box[key][i] = (unsigned char)p[8 + i];
            }
        } else {
            size_t n;
            while ((n = strlen(p)) > 0 &&
                   (isblank((unsigned char)p[n - 1]) || p[n - 1] == '\n'))
                p[n - 1] = '\0';
            if (linelen < 0)
                linelen = (int)strlen(p);
            m->extent[OB_X] = linelen;
            if (last_blank)
                m->extent[OB_Z]++;
            last_blank = 0;

            int z = m->extent[OB_Z];
            int y = m->extent[OB_Y];
            if (z >= MAX_EXT || y >= MAX_EXT)
                continue;
            for (int x = 0; p[x] && x < MAX_EXT; x++) {
                int cell = (unsigned char)p[x];
                if (box[cell][0] == 0)
                    continue;
                m->solid[z][y][x] = 1;
                if (!m->wall[OB_X][x][z][y])
                    m->wall[OB_X][x][z][y] = known[box[cell][1]];
                else
                    m->wall[OB_X][x][z][y] = 0;
                if (!m->wall[OB_X][x + 1][z][y])
                    m->wall[OB_X][x + 1][z][y] = known[box[cell][3]];
                else
                    m->wall[OB_X][x + 1][z][y] = 0;
                if (!m->wall[OB_Y][y][z][x])
                    m->wall[OB_Y][y][z][x] = known[box[cell][2]];
                else
                    m->wall[OB_Y][y][z][x] = 0;
                if (!m->wall[OB_Y][y + 1][z][x])
                    m->wall[OB_Y][y + 1][z][x] = known[box[cell][4]];
                else
                    m->wall[OB_Y][y + 1][z][x] = 0;
                if (!m->wall[OB_Z][z][y][x])
                    m->wall[OB_Z][z][y][x] = known[box[cell][0]];
                else
                    m->wall[OB_Z][z][y][x] = 0;
                if (!m->wall[OB_Z][z + 1][y][x])
                    m->wall[OB_Z][z + 1][y][x] = known[box[cell][5]];
                else
                    m->wall[OB_Z][z + 1][y][x] = 0;
            }
            m->extent[OB_Y]++;
        }
    }
    fclose(fp);
    m->extent[OB_Z]++;

    for (int y = 0; y < m->extent[OB_Y]; y++) {
        for (int z = 0; z < m->extent[OB_Z]; z++) {
            if (!m->wall[OB_X][0][z][y])
                m->wall[OB_X][0][z][y] = '#';
            if (!m->wall[OB_X][m->extent[OB_X]][z][y])
                m->wall[OB_X][m->extent[OB_X]][z][y] = '#';
        }
    }
    for (int x = 0; x < m->extent[OB_X]; x++) {
        for (int z = 0; z < m->extent[OB_Z]; z++) {
            if (!m->wall[OB_Y][0][z][x])
                m->wall[OB_Y][0][z][x] = '#';
            if (!m->wall[OB_Y][m->extent[OB_Y]][z][x])
                m->wall[OB_Y][m->extent[OB_Y]][z][x] = '#';
        }
    }
    for (int y = 0; y < m->extent[OB_Y]; y++) {
        for (int x = 0; x < m->extent[OB_X]; x++) {
            if (!m->wall[OB_Z][0][y][x])
                m->wall[OB_Z][0][y][x] = '#';
            if (!m->wall[OB_Z][m->extent[OB_Z]][y][x])
                m->wall[OB_Z][m->extent[OB_Z]][y][x] = '#';
        }
    }
    return 0;
}

static int blocked(struct app *app, float x, float y)
{
    int cx = (int)floorf(x);
    int cy = (int)floorf(y);
    if (cx < 0 || cy < 0 ||
        cx >= app->maze.extent[OB_X] || cy >= app->maze.extent[OB_Y])
        return 1;
    return app->maze.solid[0][cy][cx] != 0;
}

static void choose_start_and_target(struct app *app)
{
    app->px = 1.5f;
    app->py = 1.5f;
    for (int y = 1; y < app->maze.extent[OB_Y] - 1; y++) {
        for (int x = 1; x < app->maze.extent[OB_X] - 1; x++) {
            if (!app->maze.solid[0][y][x]) {
                app->px = x + 0.5f;
                app->py = y + 0.5f;
                y = app->maze.extent[OB_Y];
                break;
            }
        }
    }
    app->target_x = app->maze.extent[OB_X] - 2;
    app->target_y = app->maze.extent[OB_Y] - 2;
    for (int y = app->maze.extent[OB_Y] - 2; y >= 1; y--) {
        for (int x = app->maze.extent[OB_X] - 2; x >= 1; x--) {
            if (!app->maze.solid[0][y][x]) {
                app->target_x = x;
                app->target_y = y;
                return;
            }
        }
    }
}

static void choose_initial_yaw(struct app *app)
{
    static const float yaw_by_dir[] = {
        0.0f,
        3.14159265f,
        1.57079633f,
        -1.57079633f,
    };
    static const int dx[] = { 1, -1, 0, 0 };
    static const int dy[] = { 0, 0, 1, -1 };
    int cx = (int)floorf(app->px);
    int cy = (int)floorf(app->py);

    for (int i = 0; i < 4; i++) {
        if (!blocked(app, (float)(cx + dx[i]) + 0.5f,
                     (float)(cy + dy[i]) + 0.5f)) {
            app->yaw = yaw_by_dir[i];
            return;
        }
    }
    app->yaw = 0.0f;
}

static void update_game(struct app *app, float dt)
{
    float turn = 2.2f * dt;
    float speed = 2.0f * dt;
    float dx = cosf(app->yaw);
    float dy = sinf(app->yaw);
    float nx = app->px;
    float ny = app->py;

    if (key_down(app, KEY_ESC))
        app->running = 0;
    if (key_down(app, KEY_LEFT))
        app->yaw -= turn;
    if (key_down(app, KEY_RIGHT))
        app->yaw += turn;
    if (key_down(app, KEY_W) || key_down(app, KEY_UP)) {
        nx += dx * speed;
        ny += dy * speed;
    }
    if (key_down(app, KEY_S) || key_down(app, KEY_DOWN)) {
        nx -= dx * speed;
        ny -= dy * speed;
    }
    if (key_down(app, KEY_A)) {
        nx += dy * speed;
        ny -= dx * speed;
    }
    if (key_down(app, KEY_D)) {
        nx -= dy * speed;
        ny += dx * speed;
    }
    if (key_down(app, KEY_SPACE)) {
        choose_start_and_target(app);
        choose_initial_yaw(app);
        app->score = 0;
    }
    if (!blocked(app, nx, app->py))
        app->px = nx;
    if (!blocked(app, app->px, ny))
        app->py = ny;

    float tx = app->target_x + 0.5f - app->px;
    float ty = app->target_y + 0.5f - app->py;
    if (tx * tx + ty * ty < 0.22f) {
        app->score++;
        app->target_x = 1 + ((app->target_x + 3 + app->score * 5) %
                             (app->maze.extent[OB_X] - 2));
        app->target_y = 1 + ((app->target_y + 5 + app->score * 3) %
                             (app->maze.extent[OB_Y] - 2));
        for (int tries = 0; tries < 64 &&
             app->maze.solid[0][app->target_y][app->target_x]; tries++) {
            app->target_x = 1 + ((app->target_x + 1) %
                                 (app->maze.extent[OB_X] - 2));
            if (app->target_x == 1)
                app->target_y = 1 + ((app->target_y + 1) %
                                     (app->maze.extent[OB_Y] - 2));
        }
    }
}

static void build_world(struct app *app)
{
    struct maze *m = &app->maze;
    app->vert_count = 0;

    for (int x = 0; x <= m->extent[OB_X]; x++) {
        for (int z = 0; z < m->extent[OB_Z]; z++) {
            for (int y = 0; y < m->extent[OB_Y]; y++) {
                int id = m->wall[OB_X][x][z][y];
                if (!id)
                    continue;
                float a[3] = { x, z, y };
                float b[3] = { x, z, y + 1 };
                float c[3] = { x, z + 1, y + 1 };
                float d[3] = { x, z + 1, y };
                push_quad(app, a, b, c, d, id, 0.72f);
            }
        }
    }
    for (int y = 0; y <= m->extent[OB_Y]; y++) {
        for (int z = 0; z < m->extent[OB_Z]; z++) {
            for (int x = 0; x < m->extent[OB_X]; x++) {
                int id = m->wall[OB_Y][y][z][x];
                if (!id)
                    continue;
                float a[3] = { x, z, y };
                float b[3] = { x + 1, z, y };
                float c[3] = { x + 1, z + 1, y };
                float d[3] = { x, z + 1, y };
                push_quad(app, a, b, c, d, id, 0.86f);
            }
        }
    }
    for (int z = 0; z <= m->extent[OB_Z]; z++) {
        for (int y = 0; y < m->extent[OB_Y]; y++) {
            for (int x = 0; x < m->extent[OB_X]; x++) {
                int id = m->wall[OB_Z][z][y][x];
                if (!id)
                    continue;
                float a[3] = { x, z, y };
                float b[3] = { x + 1, z, y };
                float c[3] = { x + 1, z, y + 1 };
                float d[3] = { x, z, y + 1 };
                push_quad(app, a, b, c, d, id, z == 0 ? 0.48f : 1.0f);
            }
        }
    }

    float t = (float)(now_ms() - app->start_ms) * 0.004f;
    float bob = 0.08f * sinf(t);
    push_cube(app, app->target_x + 0.25f, 0.18f + bob,
              app->target_y + 0.25f, app->target_x + 0.75f, 0.68f + bob,
              app->target_y + 0.75f, 'G');
}

static GLuint compile_shader(GLenum type, const char *src)
{
    GLuint shader = glCreateShader(type);
    GLint ok = 0;
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "glmaze: shader compile failed: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static int init_gl(struct app *app)
{
    static const char *vs =
        "attribute vec3 a_pos;\n"
        "attribute vec3 a_color;\n"
        "uniform mat4 u_mvp;\n"
        "varying vec3 v_color;\n"
        "void main() { v_color = a_color; gl_Position = u_mvp * vec4(a_pos, 1.0); }\n";
    static const char *fs =
        "precision mediump float;\n"
        "varying vec3 v_color;\n"
        "void main() { gl_FragColor = vec4(v_color, 1.0); }\n";
    GLuint vert = compile_shader(GL_VERTEX_SHADER, vs);
    GLuint frag = compile_shader(GL_FRAGMENT_SHADER, fs);
    GLint ok = 0;

    if (!vert || !frag)
        return -1;
    app->program = glCreateProgram();
    glAttachShader(app->program, vert);
    glAttachShader(app->program, frag);
    glBindAttribLocation(app->program, 0, "a_pos");
    glBindAttribLocation(app->program, 1, "a_color");
    glLinkProgram(app->program);
    glDeleteShader(vert);
    glDeleteShader(frag);
    glGetProgramiv(app->program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(app->program, sizeof(log), NULL, log);
        fprintf(stderr, "glmaze: program link failed: %s\n", log);
        return -1;
    }
    app->attr_pos = glGetAttribLocation(app->program, "a_pos");
    app->attr_color = glGetAttribLocation(app->program, "a_color");
    app->uniform_mvp = glGetUniformLocation(app->program, "u_mvp");
    glGenBuffers(1, &app->vbo);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glClearColor(0.02f, 0.025f, 0.04f, 1.0f);
    return 0;
}

static void detect_read_format(struct app *app)
{
    GLint impl_format = 0;
    GLint impl_type = 0;

    app->read_format = GL_RGBA;
    glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT, &impl_format);
    glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_TYPE, &impl_type);
    if (impl_format == GL_BGRA_EXT && impl_type == GL_UNSIGNED_BYTE)
        app->read_format = GL_BGRA_EXT;
}

static int ensure_present_buffer(struct app *app)
{
    if (!app->shm)
        return -1;
    if (app->present_buf_ready &&
        app->present_buf.width == app->width &&
        app->present_buf.height == app->height)
        return 0;
    if (app->present_buf_ready) {
        xv6_present_buffer_destroy(&app->present_buf);
        app->present_buf_ready = 0;
    }
    if (xv6_present_buffer_init(&app->present_buf, app->width, app->height,
                                app->shm, NULL) != 0)
        return -1;
    app->present_buf_ready = 1;
    return 0;
}

static int present_via_shm(struct app *app)
{
    size_t bytes = (size_t)app->width * (size_t)app->height * 4;

    if (ensure_present_buffer(app) != 0)
        return -1;
    if (app->readback_size < bytes) {
        uint8_t *grown = realloc(app->readback, bytes);

        if (!grown)
            return -1;
        app->readback = grown;
        app->readback_size = bytes;
    }
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, app->width, app->height, app->read_format,
                 GL_UNSIGNED_BYTE, app->readback);
    for (int y = 0; y < app->height; y++) {
        uint32_t *dst = (uint32_t *)((uint8_t *)app->present_buf.pixels +
                                     (size_t)y *
                                     (size_t)app->present_buf.stride);
        uint32_t *src = (uint32_t *)(app->readback +
                                     (size_t)(app->height - 1 - y) *
                                     (size_t)app->width * 4);

        if (app->read_format == GL_BGRA_EXT) {
            memcpy(dst, src, (size_t)app->width * 4);
            continue;
        }
        for (int x = 0; x < app->width; x++) {
            uint32_t rgba = src[x];

            dst[x] = 0xff000000u | ((rgba & 0x000000ffu) << 16) |
                     (rgba & 0x0000ff00u) |
                     ((rgba & 0x00ff0000u) >> 16);
        }
    }
    draw_titlebar(app);
    wl_surface_attach(app->surface, app->present_buf.wl_buffer, 0, 0);
    wl_surface_damage(app->surface, 0, 0, app->width, app->height);
    wl_surface_commit(app->surface);
    return 0;
}

static void draw_rect(uint32_t *pixels, int width, int height, int stride,
                      int x, int y, int w, int h, uint32_t color)
{
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > width)
        w = width - x;
    if (y + h > height)
        h = height - y;
    if (w <= 0 || h <= 0)
        return;
    for (int yy = y; yy < y + h; yy++) {
        uint32_t *row = (uint32_t *)((uint8_t *)pixels +
                                     (size_t)yy * (size_t)stride);
        for (int xx = x; xx < x + w; xx++)
            row[xx] = color;
    }
}

static void draw_char(uint32_t *pixels, int width, int height, int stride,
                      int x, int y, char ch, uint32_t color)
{
    const uint8_t *glyph;

    if (ch < 0x20 || ch > 0x7e)
        ch = '?';
    glyph = font8x16_data[ch - 0x20];
    for (int row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];

        for (int col = 0; col < 8; col++) {
            if (bits & (0x80u >> col))
                draw_rect(pixels, width, height, stride, x + col, y + row,
                          1, 1, color);
        }
    }
}

static void draw_text_fit(uint32_t *pixels, int width, int height, int stride,
                          int x, int y, int max_w, const char *text,
                          uint32_t color)
{
    int used = 0;

    if (max_w <= 0)
        return;
    for (const char *p = text; *p && used + 8 <= max_w; p++, used += 8)
        draw_char(pixels, width, height, stride, x + used, y, *p, color);
}

static void draw_title_button(uint32_t *pixels, int width, int height,
                              int stride, int x, const char *label,
                              uint32_t bg, uint32_t fg)
{
    int tx = x + (TITLEBAR_CONTROL_W - (int)strlen(label) * 8) / 2;

    draw_rect(pixels, width, height, stride, x, 0, TITLEBAR_CONTROL_W,
              TITLEBAR_H, bg);
    draw_rect(pixels, width, height, stride, x, TITLEBAR_H - 1,
              TITLEBAR_CONTROL_W, 1, 0xff677789u);
    draw_text_fit(pixels, width, height, stride, tx, 7,
                  TITLEBAR_CONTROL_W - 4, label, fg);
}

static void draw_titlebar(struct app *app)
{
    uint32_t *pixels = app->present_buf.pixels;
    int close_x = app->width - TITLEBAR_CONTROL_W;
    int max_x = close_x - TITLEBAR_CONTROL_W;
    int min_x = max_x - TITLEBAR_CONTROL_W;
    int title_limit = min_x - 18;

    if (!pixels || app->width <= TITLEBAR_CONTROL_W * 3 ||
        app->height < TITLEBAR_H)
        return;
    draw_rect(pixels, app->width, app->height, app->present_buf.stride,
              0, 0, app->width, TITLEBAR_H, 0xff1b2836u);
    draw_rect(pixels, app->width, app->height, app->present_buf.stride,
              0, TITLEBAR_H - 1, app->width, 1, 0xff5f7183u);
    if (title_limit > 0)
        draw_text_fit(pixels, app->width, app->height,
                      app->present_buf.stride, 10, 7, title_limit,
                      TITLEBAR_TITLE, 0xfff3f7fbu);
    draw_title_button(pixels, app->width, app->height,
                      app->present_buf.stride, min_x, "-", 0xff26384bu,
                      0xfff3f7fbu);
    draw_title_button(pixels, app->width, app->height,
                      app->present_buf.stride, max_x,
                      app->maximized ? "[]" : "+", 0xff26384bu,
                      0xfff3f7fbu);
    draw_title_button(pixels, app->width, app->height,
                      app->present_buf.stride, close_x, "x", 0xff78343au,
                      0xffffffffu);
}

static void draw_frame(struct app *app)
{
    float proj[16], view[16], mvp[16];
    float eye[3] = { app->px, 0.55f, app->py };
    float dir[3] = { cosf(app->yaw), 0.0f, sinf(app->yaw) };
    float center[3] = { eye[0] + dir[0], eye[1] - 0.18f,
                        eye[2] + dir[2] };
    float up[3] = { 0.0f, 1.0f, 0.0f };
    int content_h = app->height - TITLEBAR_H;

    if (content_h < 1)
        content_h = 1;
    build_world(app);
    glViewport(0, 0, app->width, app->height);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glViewport(0, 0, app->width, content_h);
    mat4_perspective(proj, 65.0f * 0.0174532925f,
                     (float)app->width / (float)content_h, 0.05f, 80.0f);
    mat4_look_at(view, eye, center, up);
    mat4_mul(mvp, proj, view);

    glUseProgram(app->program);
    glUniformMatrix4fv(app->uniform_mvp, 1, GL_FALSE, mvp);
    glBindBuffer(GL_ARRAY_BUFFER, app->vbo);
    glBufferData(GL_ARRAY_BUFFER, app->vert_count * sizeof(app->verts[0]),
                 app->verts, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(app->attr_pos);
    glEnableVertexAttribArray(app->attr_color);
    glVertexAttribPointer(app->attr_pos, 3, GL_FLOAT, GL_FALSE,
                          sizeof(struct vertex), (void *)0);
    glVertexAttribPointer(app->attr_color, 3, GL_FLOAT, GL_FALSE,
                          sizeof(struct vertex),
                          (void *)(uintptr_t)offsetof(struct vertex, color));
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)app->vert_count);
    glDisableVertexAttribArray(app->attr_pos);
    glDisableVertexAttribArray(app->attr_color);
}

static int init_egl(struct app *app)
{
    static const EGLint config_attrs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_NONE
    };
    static const EGLint ctx_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    EGLint major = 0, minor = 0, nconfigs = 0;
    const char *renderer;
    char renderer_name[64];
    int renderer_len = 0;

    app->egl_display = eglGetDisplay((EGLNativeDisplayType)app->display);
    if (app->egl_display == EGL_NO_DISPLAY ||
        !eglInitialize(app->egl_display, &major, &minor))
        return -1;
    if (!eglChooseConfig(app->egl_display, config_attrs, &app->egl_config, 1,
                         &nconfigs) || nconfigs < 1)
        return -1;
    if (!eglBindAPI(EGL_OPENGL_ES_API))
        return -1;
    app->egl_window = wl_egl_window_create(app->surface, app->width,
                                           app->height);
    app->egl_surface =
        eglCreateWindowSurface(app->egl_display, app->egl_config,
                               (EGLNativeWindowType)app->egl_window, NULL);
    app->egl_context =
        eglCreateContext(app->egl_display, app->egl_config, EGL_NO_CONTEXT,
                         ctx_attrs);
    if (app->egl_surface == EGL_NO_SURFACE ||
        app->egl_context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(app->egl_display, app->egl_surface, app->egl_surface,
                        app->egl_context))
        return -1;
    eglSwapInterval(app->egl_display, 0);

    renderer = (const char *)glGetString(GL_RENDERER);
    if (!renderer)
        renderer = "unknown";
    while (renderer[renderer_len] &&
           !isspace((unsigned char)renderer[renderer_len]) &&
           renderer_len + 1 < (int)sizeof(renderer_name)) {
        renderer_name[renderer_len] = renderer[renderer_len];
        renderer_len++;
    }
    renderer_name[renderer_len] = '\0';
    snprintf(app->renderer, sizeof(app->renderer), "%s", renderer_name);
    return init_gl(app);
}

static void write_status_file(struct app *app, int final)
{
    FILE *fp = fopen("/tmp/glmaze-status", "w");

    if (!fp)
        return;
    fprintf(fp, "renderer=%s frames=%d score=%d status=%d\n",
            app->renderer[0] ? app->renderer : "unknown", app->frames,
            app->score, final ? 0 : -1);
    fclose(fp);
}

static void keyboard_keymap(void *data, struct wl_keyboard *keyboard,
                            uint32_t format, int32_t fd, uint32_t size)
{
    (void)data;
    (void)keyboard;
    (void)format;
    (void)size;
    if (fd >= 0)
        close(fd);
}

static void keyboard_enter(void *data, struct wl_keyboard *keyboard,
                           uint32_t serial, struct wl_surface *surface,
                           struct wl_array *keys)
{
    (void)data; (void)keyboard; (void)serial; (void)surface; (void)keys;
}

static void keyboard_leave(void *data, struct wl_keyboard *keyboard,
                           uint32_t serial, struct wl_surface *surface)
{
    (void)data; (void)keyboard; (void)serial; (void)surface;
}

static void keyboard_key(void *data, struct wl_keyboard *keyboard,
                         uint32_t serial, uint32_t time, uint32_t key,
                         uint32_t state)
{
    struct app *app = data;
    (void)keyboard; (void)serial; (void)time;
    if (key < 256)
        app->keys[key] = state == WL_KEYBOARD_KEY_STATE_PRESSED;
}

static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard,
                               uint32_t serial, uint32_t mods_depressed,
                               uint32_t mods_latched, uint32_t mods_locked,
                               uint32_t group)
{
    (void)data; (void)keyboard; (void)serial; (void)mods_depressed;
    (void)mods_latched; (void)mods_locked; (void)group;
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *keyboard,
                                 int32_t rate, int32_t delay)
{
    (void)data; (void)keyboard; (void)rate; (void)delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

static int titlebar_control_at(const struct app *app, int x, int y)
{
    if (y < 0 || y >= TITLEBAR_H || app->width <= TITLEBAR_CONTROL_W * 3)
        return 0;
    if (x >= app->width - TITLEBAR_CONTROL_W)
        return 'x';
    if (x >= app->width - TITLEBAR_CONTROL_W * 2)
        return 'm';
    if (x >= app->width - TITLEBAR_CONTROL_W * 3)
        return '-';
    return 't';
}

static void activate_titlebar_control(struct app *app, int control)
{
    if (control == 'x') {
        app->running = 0;
    } else if (control == 'm') {
        if (app->maximized) {
            xdg_toplevel_unset_maximized(app->toplevel);
            app->maximized = 0;
        } else {
            xdg_toplevel_set_maximized(app->toplevel);
            app->maximized = 1;
        }
    } else if (control == '-') {
        xdg_toplevel_set_minimized(app->toplevel);
    }
}

static void pointer_enter(void *data, struct wl_pointer *pointer,
                          uint32_t serial, struct wl_surface *surface,
                          wl_fixed_t sx, wl_fixed_t sy)
{
    struct app *app = data;
    (void)pointer; (void)serial; (void)surface;
    app->pointer_x = wl_fixed_to_int(sx);
    app->pointer_y = wl_fixed_to_int(sy);
}

static void pointer_leave(void *data, struct wl_pointer *pointer,
                          uint32_t serial, struct wl_surface *surface)
{
    (void)data; (void)pointer; (void)serial; (void)surface;
}

static void pointer_motion(void *data, struct wl_pointer *pointer,
                           uint32_t time, wl_fixed_t sx, wl_fixed_t sy)
{
    struct app *app = data;
    (void)pointer; (void)time;
    app->pointer_x = wl_fixed_to_int(sx);
    app->pointer_y = wl_fixed_to_int(sy);
}

static void pointer_button(void *data, struct wl_pointer *pointer,
                           uint32_t serial, uint32_t time, uint32_t button,
                           uint32_t state)
{
    struct app *app = data;
    int control;
    (void)pointer; (void)time;

    if (button != BTN_LEFT || state != WL_POINTER_BUTTON_STATE_PRESSED)
        return;
    control = titlebar_control_at(app, app->pointer_x, app->pointer_y);
    if (control == 't') {
        xdg_toplevel_move(app->toplevel, app->seat, serial);
    } else if (control) {
        activate_titlebar_control(app, control);
    }
}

static void pointer_axis(void *data, struct wl_pointer *pointer,
                         uint32_t time, uint32_t axis, wl_fixed_t value)
{
    (void)data; (void)pointer; (void)time; (void)axis; (void)value;
}

static void pointer_frame(void *data, struct wl_pointer *pointer)
{
    (void)data; (void)pointer;
}

static void pointer_axis_source(void *data, struct wl_pointer *pointer,
                                uint32_t axis_source)
{
    (void)data; (void)pointer; (void)axis_source;
}

static void pointer_axis_stop(void *data, struct wl_pointer *pointer,
                              uint32_t time, uint32_t axis)
{
    (void)data; (void)pointer; (void)time; (void)axis;
}

static void pointer_axis_discrete(void *data, struct wl_pointer *pointer,
                                  uint32_t axis, int32_t discrete)
{
    (void)data; (void)pointer; (void)axis; (void)discrete;
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
    .axis_source = pointer_axis_source,
    .axis_stop = pointer_axis_stop,
    .axis_discrete = pointer_axis_discrete,
};

static void seat_capabilities(void *data, struct wl_seat *seat,
                              uint32_t capabilities)
{
    struct app *app = data;
    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !app->pointer) {
        app->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(app->pointer, &pointer_listener, app);
    }
    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !app->keyboard) {
        app->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(app->keyboard, &keyboard_listener, app);
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name)
{
    (void)data; (void)seat; (void)name;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

static void xdg_surface_configure(void *data, struct xdg_surface *surface,
                                  uint32_t serial)
{
    struct app *app = data;
    xdg_surface_ack_configure(surface, serial);
    app->configured = 1;
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                               int32_t width, int32_t height,
                               struct wl_array *states)
{
    struct app *app = data;
    uint32_t *state;
    (void)toplevel;
    app->maximized = 0;
    wl_array_for_each(state, states) {
        if (*state == XDG_TOPLEVEL_STATE_MAXIMIZED)
            app->maximized = 1;
    }
    if (width > 0 && height > 0) {
        app->width = width;
        app->height = height;
        if (app->egl_window)
            wl_egl_window_resize(app->egl_window, width, height, 0, 0);
    }
}

static void toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
    struct app *app = data;
    (void)toplevel;
    app->running = 0;
}

static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = toplevel_configure,
    .close = toplevel_close,
};

static void wm_base_ping(void *data, struct xdg_wm_base *wm_base,
                         uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = wm_base_ping,
};

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface,
                            uint32_t version)
{
    struct app *app = data;
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        app->compositor =
            wl_registry_bind(registry, name, &wl_compositor_interface,
                             version < 4 ? version : 4);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        app->wm_base =
            wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(app->wm_base, &wm_base_listener, app);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        app->shm = wl_registry_bind(registry, name, &wl_shm_interface,
                                    version < 1 ? version : 1);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        app->seat = wl_registry_bind(registry, name, &wl_seat_interface,
                                     version < 5 ? version : 5);
        wl_seat_add_listener(app->seat, &seat_listener, app);
    }
}

static void registry_remove(void *data, struct wl_registry *registry,
                            uint32_t name)
{
    (void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_remove,
};

static int init_wayland(struct app *app)
{
    app->display = wl_display_connect(NULL);
    if (!app->display)
        return -1;
    app->registry = wl_display_get_registry(app->display);
    wl_registry_add_listener(app->registry, &registry_listener, app);
    wl_display_roundtrip(app->display);
    wl_display_roundtrip(app->display);
    if (!app->compositor || !app->wm_base)
        return -1;

    app->surface = wl_compositor_create_surface(app->compositor);
    app->xdg_surface = xdg_wm_base_get_xdg_surface(app->wm_base, app->surface);
    xdg_surface_add_listener(app->xdg_surface, &xdg_surface_listener, app);
    app->toplevel = xdg_surface_get_toplevel(app->xdg_surface);
    xdg_toplevel_add_listener(app->toplevel, &toplevel_listener, app);
    xdg_toplevel_set_title(app->toplevel, TITLEBAR_TITLE);
    xdg_toplevel_set_app_id(app->toplevel, "glmaze");
    xdg_toplevel_set_min_size(app->toplevel, 400, 300 + TITLEBAR_H);
    wl_surface_commit(app->surface);
    while (!app->configured && wl_display_dispatch(app->display) >= 0)
        ;
    return app->configured ? 0 : -1;
}

static void update_title(struct app *app)
{
    uint64_t t = now_ms();
    if (t - app->last_title_ms < 500)
        return;
    app->last_title_ms = t;
    char title[96];
    snprintf(title, sizeof(title), "glMaze - score %d - %.1f FPS",
             app->score,
             app->frames * 1000.0f / (float)(t - app->start_ms + 1));
    xdg_toplevel_set_title(app->toplevel, title);
}

static int pump_wayland(struct app *app)
{
    struct pollfd pfd;
    int ret;

    if (wl_display_dispatch_pending(app->display) < 0)
        return -1;
    while (wl_display_prepare_read(app->display) != 0) {
        if (wl_display_dispatch_pending(app->display) < 0)
            return -1;
    }
    wl_display_flush(app->display);
    pfd.fd = wl_display_get_fd(app->display);
    pfd.events = POLLIN;
    pfd.revents = 0;
    ret = poll(&pfd, 1, 0);
    if (ret > 0 && (pfd.revents & POLLIN)) {
        if (wl_display_read_events(app->display) < 0)
            return -1;
    } else {
        wl_display_cancel_read(app->display);
        if (ret < 0 && errno != EINTR)
            return -1;
    }
    return wl_display_dispatch_pending(app->display);
}

static void cleanup(struct app *app)
{
    if (app->present_buf_ready) {
        xv6_present_buffer_destroy(&app->present_buf);
        app->present_buf_ready = 0;
    }
    if (app->egl_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(app->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
        if (app->egl_surface != EGL_NO_SURFACE)
            eglDestroySurface(app->egl_display, app->egl_surface);
        if (app->egl_context != EGL_NO_CONTEXT)
            eglDestroyContext(app->egl_display, app->egl_context);
        eglTerminate(app->egl_display);
    }
    if (app->egl_window)
        wl_egl_window_destroy(app->egl_window);
    if (app->pointer)
        wl_pointer_destroy(app->pointer);
    if (app->keyboard)
        wl_keyboard_destroy(app->keyboard);
    if (app->seat)
        wl_seat_destroy(app->seat);
    if (app->toplevel)
        xdg_toplevel_destroy(app->toplevel);
    if (app->xdg_surface)
        xdg_surface_destroy(app->xdg_surface);
    if (app->surface)
        wl_surface_destroy(app->surface);
    if (app->wm_base)
        xdg_wm_base_destroy(app->wm_base);
    if (app->compositor)
        wl_compositor_destroy(app->compositor);
    if (app->shm)
        wl_shm_destroy(app->shm);
    if (app->registry)
        wl_registry_destroy(app->registry);
    if (app->display)
        wl_display_disconnect(app->display);
    free(app->verts);
    free(app->readback);
}

int main(int argc, char **argv)
{
    struct app app;
    const char *maze_path = "levels/maze.maz";
    int max_seconds = 0;
    uint64_t last;

    memset(&app, 0, sizeof(app));
    app.width = 800;
    app.height = 600 + TITLEBAR_H;
    app.running = 1;
    app.egl_display = EGL_NO_DISPLAY;
    app.egl_surface = EGL_NO_SURFACE;
    app.egl_context = EGL_NO_CONTEXT;

    setenv("XDG_RUNTIME_DIR", "/tmp", 1);
    setenv("WAYLAND_DISPLAY", "wayland-0", 0);
    configure_gl_driver_env();

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--seconds=", 10) == 0)
            max_seconds = atoi(argv[i] + 10);
        else if (strncmp(argv[i], "--maze=", 7) == 0)
            maze_path = argv[i] + 7;
        else {
            fprintf(stderr, "usage: glmaze [--seconds=N] [--maze=PATH]\n");
            return 2;
        }
    }

    if (load_maze(&app.maze, maze_path) != 0)
        return 1;
    choose_start_and_target(&app);
    choose_initial_yaw(&app);

    if (init_wayland(&app) != 0) {
        fprintf(stderr, "glmaze: Wayland init failed\n");
        cleanup(&app);
        return 1;
    }
    if (init_egl(&app) != 0) {
        fprintf(stderr, "glmaze: EGL/GL init failed (0x%x)\n", eglGetError());
        cleanup(&app);
        return 1;
    }
    detect_read_format(&app);

    write_status_file(&app, 0);
    app.start_ms = now_ms();
    last = app.start_ms;
    while (app.running) {
        uint64_t t = now_ms();
        float dt = (float)(t - last) / 1000.0f;
        if (dt > 0.05f)
            dt = 0.05f;
        last = t;
        if (pump_wayland(&app) < 0)
            break;
        update_game(&app, dt);
        draw_frame(&app);
        if (present_via_shm(&app) != 0)
            break;
        app.frames++;
        update_title(&app);
        if (max_seconds > 0 &&
            now_ms() - app.start_ms >= (uint64_t)max_seconds * 1000ull)
            break;
        usleep(16000);
    }

    write_status_file(&app, 1);
    cleanup(&app);
    return 0;
}
