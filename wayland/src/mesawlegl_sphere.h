#ifndef XV6_MESAWLEGL_SPHERE_H
#define XV6_MESAWLEGL_SPHERE_H

#include <GLES2/gl2.h>

struct sphere_vertex {
    GLfloat x;
    GLfloat y;
    GLfloat z;
    GLfloat nx;
    GLfloat ny;
    GLfloat nz;
};

void mesawlegl_mat4_mul(float out[16], const float a[16], const float b[16]);
void mesawlegl_mat4_perspective(float m[16], float fovy, float aspect,
                                float znear, float zfar);
void mesawlegl_mat4_translate(float m[16], float x, float y, float z);
void mesawlegl_mat4_rotate_x(float m[16], float angle);
void mesawlegl_mat4_rotate_y(float m[16], float angle);

int mesawlegl_poly_sphere_vertex_count(int frequency);
int mesawlegl_build_poly_sphere(struct sphere_vertex *dst, int max_vertices,
                                int frequency);

#endif
