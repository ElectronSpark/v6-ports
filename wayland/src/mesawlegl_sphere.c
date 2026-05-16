#include "mesawlegl_sphere.h"

#include <math.h>
#include <string.h>

static void mat4_identity(float m[16])
{
    memset(m, 0, sizeof(float) * 16);
    m[0] = 1.0f;
    m[5] = 1.0f;
    m[10] = 1.0f;
    m[15] = 1.0f;
}

void mesawlegl_mat4_mul(float out[16], const float a[16], const float b[16])
{
    float r[16];

    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            r[col * 4 + row] =
                a[0 * 4 + row] * b[col * 4 + 0] +
                a[1 * 4 + row] * b[col * 4 + 1] +
                a[2 * 4 + row] * b[col * 4 + 2] +
                a[3 * 4 + row] * b[col * 4 + 3];
        }
    }
    memcpy(out, r, sizeof(r));
}

void mesawlegl_mat4_perspective(float m[16], float fovy, float aspect,
                                float znear, float zfar)
{
    float f = 1.0f / tanf(fovy * 0.5f);

    memset(m, 0, sizeof(float) * 16);
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (zfar + znear) / (znear - zfar);
    m[11] = -1.0f;
    m[14] = (2.0f * zfar * znear) / (znear - zfar);
}

void mesawlegl_mat4_translate(float m[16], float x, float y, float z)
{
    mat4_identity(m);
    m[12] = x;
    m[13] = y;
    m[14] = z;
}

void mesawlegl_mat4_rotate_x(float m[16], float angle)
{
    float s = sinf(angle);
    float c = cosf(angle);

    mat4_identity(m);
    m[5] = c;
    m[6] = s;
    m[9] = -s;
    m[10] = c;
}

void mesawlegl_mat4_rotate_y(float m[16], float angle)
{
    float s = sinf(angle);
    float c = cosf(angle);

    mat4_identity(m);
    m[0] = c;
    m[2] = -s;
    m[8] = s;
    m[10] = c;
}

static struct sphere_vertex make_poly_vertex(float x, float y, float z)
{
    float inv_len = 1.0f / sqrtf(x * x + y * y + z * z);
    struct sphere_vertex v;

    v.x = x * inv_len;
    v.y = y * inv_len;
    v.z = z * inv_len;
    v.nx = 0.0f;
    v.ny = 0.0f;
    v.nz = 1.0f;
    return v;
}

static void sphere_emit_flat(struct sphere_vertex *dst, int *idx,
                             struct sphere_vertex a, struct sphere_vertex b,
                             struct sphere_vertex c)
{
    struct sphere_vertex out[3] = { a, b, c };
    float ux = b.x - a.x;
    float uy = b.y - a.y;
    float uz = b.z - a.z;
    float vx = c.x - a.x;
    float vy = c.y - a.y;
    float vz = c.z - a.z;
    float nx = uy * vz - uz * vy;
    float ny = uz * vx - ux * vz;
    float nz = ux * vy - uy * vx;
    float inv_len = 1.0f / sqrtf(nx * nx + ny * ny + nz * nz);
    float cx = (a.x + b.x + c.x) / 3.0f;
    float cy = (a.y + b.y + c.y) / 3.0f;
    float cz = (a.z + b.z + c.z) / 3.0f;

    nx *= inv_len;
    ny *= inv_len;
    nz *= inv_len;
    if (nx * cx + ny * cy + nz * cz < 0.0f) {
        struct sphere_vertex tmp = out[1];

        out[1] = out[2];
        out[2] = tmp;
        nx = -nx;
        ny = -ny;
        nz = -nz;
    }
    for (int i = 0; i < 3; i++) {
        out[i].nx = nx;
        out[i].ny = ny;
        out[i].nz = nz;
        dst[(*idx)++] = out[i];
    }
}

static struct sphere_vertex barycentric_sphere_point(struct sphere_vertex a,
                                                     struct sphere_vertex b,
                                                     struct sphere_vertex c,
                                                     int ia, int ib, int ic,
                                                     int frequency)
{
    float fa = (float)ia / (float)frequency;
    float fb = (float)ib / (float)frequency;
    float fc = (float)ic / (float)frequency;

    return make_poly_vertex(a.x * fa + b.x * fb + c.x * fc,
                            a.y * fa + b.y * fb + c.y * fc,
                            a.z * fa + b.z * fb + c.z * fc);
}

int mesawlegl_poly_sphere_vertex_count(int frequency)
{
    if (frequency <= 0)
        return 0;
    return 20 * frequency * frequency * 3;
}

int mesawlegl_build_poly_sphere(struct sphere_vertex *dst, int max_vertices,
                                int frequency)
{
    static const float phi = 1.61803398875f;
    static const struct {
        int a;
        int b;
        int c;
    } faces[20] = {
        { 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 },
        { 0, 10, 11 }, { 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 },
        { 10, 7, 6 }, { 7, 1, 8 }, { 3, 9, 4 }, { 3, 4, 2 },
        { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 }, { 4, 9, 5 },
        { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 },
    };
    struct sphere_vertex base[12] = {
        { -1.0f,  phi, 0.0f, 0.0f, 0.0f, 1.0f },
        {  1.0f,  phi, 0.0f, 0.0f, 0.0f, 1.0f },
        { -1.0f, -phi, 0.0f, 0.0f, 0.0f, 1.0f },
        {  1.0f, -phi, 0.0f, 0.0f, 0.0f, 1.0f },
        { 0.0f, -1.0f,  phi, 0.0f, 0.0f, 1.0f },
        { 0.0f,  1.0f,  phi, 0.0f, 0.0f, 1.0f },
        { 0.0f, -1.0f, -phi, 0.0f, 0.0f, 1.0f },
        { 0.0f,  1.0f, -phi, 0.0f, 0.0f, 1.0f },
        {  phi, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f },
        {  phi, 0.0f,  1.0f, 0.0f, 0.0f, 1.0f },
        { -phi, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f },
        { -phi, 0.0f,  1.0f, 0.0f, 0.0f, 1.0f },
    };
    int idx = 0;
    int required = mesawlegl_poly_sphere_vertex_count(frequency);

    if (dst == NULL || max_vertices < required)
        return -1;

    for (int i = 0; i < 12; i++)
        base[i] = make_poly_vertex(base[i].x, base[i].y, base[i].z);

    for (int face = 0; face < 20; face++) {
        struct sphere_vertex a = base[faces[face].a];
        struct sphere_vertex b = base[faces[face].b];
        struct sphere_vertex c = base[faces[face].c];

        for (int row = 0; row < frequency; row++) {
            for (int col = 0; col < frequency - row; col++) {
                struct sphere_vertex p0 =
                    barycentric_sphere_point(a, b, c,
                                             frequency - row - col,
                                             col, row, frequency);
                struct sphere_vertex p1 =
                    barycentric_sphere_point(a, b, c,
                                             frequency - row - col - 1,
                                             col + 1, row, frequency);
                struct sphere_vertex p2 =
                    barycentric_sphere_point(a, b, c,
                                             frequency - row - col - 1,
                                             col, row + 1, frequency);

                sphere_emit_flat(dst, &idx, p0, p1, p2);
                if (col < frequency - row - 1) {
                    struct sphere_vertex p3 =
                        barycentric_sphere_point(a, b, c,
                                                 frequency - row - col - 2,
                                                 col + 1, row + 1,
                                                 frequency);
                    sphere_emit_flat(dst, &idx, p1, p3, p2);
                }
            }
        }
    }

    return idx;
}
