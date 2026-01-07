/*
 * CSS Transform Matrix Implementation
 */

#include "transform.h"
#include <math.h>
#include <string.h>

/* ============================================================================
 * 2D Transforms
 * ============================================================================ */

MinirendTransform2D minirend_transform_identity(void) {
    MinirendTransform2D t;
    t.m[0] = 1.0f; t.m[1] = 0.0f;  /* a, b */
    t.m[2] = 0.0f; t.m[3] = 1.0f;  /* c, d */
    t.m[4] = 0.0f; t.m[5] = 0.0f;  /* tx, ty */
    return t;
}

MinirendTransform2D minirend_transform_translate(float tx, float ty) {
    MinirendTransform2D t = minirend_transform_identity();
    t.m[4] = tx;
    t.m[5] = ty;
    return t;
}

MinirendTransform2D minirend_transform_scale(float sx, float sy) {
    MinirendTransform2D t = minirend_transform_identity();
    t.m[0] = sx;
    t.m[3] = sy;
    return t;
}

MinirendTransform2D minirend_transform_rotate(float angle) {
    float c = cosf(angle);
    float s = sinf(angle);
    
    MinirendTransform2D t;
    t.m[0] = c;   t.m[1] = s;
    t.m[2] = -s;  t.m[3] = c;
    t.m[4] = 0;   t.m[5] = 0;
    return t;
}

MinirendTransform2D minirend_transform_skew(float ax, float ay) {
    MinirendTransform2D t = minirend_transform_identity();
    t.m[2] = tanf(ax);  /* c = tan(ax) */
    t.m[1] = tanf(ay);  /* b = tan(ay) */
    return t;
}

MinirendTransform2D minirend_transform_multiply(MinirendTransform2D a,
                                                 MinirendTransform2D b) {
    MinirendTransform2D r;
    
    /* | a0 a2 a4 |   | b0 b2 b4 |
     * | a1 a3 a5 | * | b1 b3 b5 |
     * | 0  0  1  |   | 0  0  1  |
     */
    r.m[0] = a.m[0] * b.m[0] + a.m[2] * b.m[1];
    r.m[1] = a.m[1] * b.m[0] + a.m[3] * b.m[1];
    r.m[2] = a.m[0] * b.m[2] + a.m[2] * b.m[3];
    r.m[3] = a.m[1] * b.m[2] + a.m[3] * b.m[3];
    r.m[4] = a.m[0] * b.m[4] + a.m[2] * b.m[5] + a.m[4];
    r.m[5] = a.m[1] * b.m[4] + a.m[3] * b.m[5] + a.m[5];
    
    return r;
}

void minirend_transform_point(MinirendTransform2D t, float x, float y,
                              float *out_x, float *out_y) {
    *out_x = t.m[0] * x + t.m[2] * y + t.m[4];
    *out_y = t.m[1] * x + t.m[3] * y + t.m[5];
}

bool minirend_transform_is_identity(MinirendTransform2D t) {
    const float eps = 1e-6f;
    return fabsf(t.m[0] - 1.0f) < eps &&
           fabsf(t.m[1]) < eps &&
           fabsf(t.m[2]) < eps &&
           fabsf(t.m[3] - 1.0f) < eps &&
           fabsf(t.m[4]) < eps &&
           fabsf(t.m[5]) < eps;
}

bool minirend_transform_invert(MinirendTransform2D t, MinirendTransform2D *out) {
    float det = t.m[0] * t.m[3] - t.m[1] * t.m[2];
    if (fabsf(det) < 1e-10f) return false;
    
    float inv_det = 1.0f / det;
    
    out->m[0] = t.m[3] * inv_det;
    out->m[1] = -t.m[1] * inv_det;
    out->m[2] = -t.m[2] * inv_det;
    out->m[3] = t.m[0] * inv_det;
    out->m[4] = (t.m[2] * t.m[5] - t.m[3] * t.m[4]) * inv_det;
    out->m[5] = (t.m[1] * t.m[4] - t.m[0] * t.m[5]) * inv_det;
    
    return true;
}

/* ============================================================================
 * 3D Transforms
 * ============================================================================ */

MinirendTransform3D minirend_transform3d_identity(void) {
    MinirendTransform3D t;
    memset(t.m, 0, sizeof(t.m));
    t.m[0] = 1.0f;
    t.m[5] = 1.0f;
    t.m[10] = 1.0f;
    t.m[15] = 1.0f;
    return t;
}

MinirendTransform3D minirend_transform3d_translate(float tx, float ty, float tz) {
    MinirendTransform3D t = minirend_transform3d_identity();
    t.m[12] = tx;
    t.m[13] = ty;
    t.m[14] = tz;
    return t;
}

MinirendTransform3D minirend_transform3d_scale(float sx, float sy, float sz) {
    MinirendTransform3D t = minirend_transform3d_identity();
    t.m[0] = sx;
    t.m[5] = sy;
    t.m[10] = sz;
    return t;
}

MinirendTransform3D minirend_transform3d_rotate_x(float angle) {
    float c = cosf(angle);
    float s = sinf(angle);
    
    MinirendTransform3D t = minirend_transform3d_identity();
    t.m[5] = c;
    t.m[6] = s;
    t.m[9] = -s;
    t.m[10] = c;
    return t;
}

MinirendTransform3D minirend_transform3d_rotate_y(float angle) {
    float c = cosf(angle);
    float s = sinf(angle);
    
    MinirendTransform3D t = minirend_transform3d_identity();
    t.m[0] = c;
    t.m[2] = -s;
    t.m[8] = s;
    t.m[10] = c;
    return t;
}

MinirendTransform3D minirend_transform3d_rotate_z(float angle) {
    float c = cosf(angle);
    float s = sinf(angle);
    
    MinirendTransform3D t = minirend_transform3d_identity();
    t.m[0] = c;
    t.m[1] = s;
    t.m[4] = -s;
    t.m[5] = c;
    return t;
}

MinirendTransform3D minirend_transform3d_rotate(float x, float y, float z, float angle) {
    /* Normalize axis */
    float len = sqrtf(x*x + y*y + z*z);
    if (len < 1e-10f) return minirend_transform3d_identity();
    
    x /= len;
    y /= len;
    z /= len;
    
    float c = cosf(angle);
    float s = sinf(angle);
    float t = 1.0f - c;
    
    MinirendTransform3D m = minirend_transform3d_identity();
    
    m.m[0] = t*x*x + c;
    m.m[1] = t*x*y + s*z;
    m.m[2] = t*x*z - s*y;
    
    m.m[4] = t*x*y - s*z;
    m.m[5] = t*y*y + c;
    m.m[6] = t*y*z + s*x;
    
    m.m[8] = t*x*z + s*y;
    m.m[9] = t*y*z - s*x;
    m.m[10] = t*z*z + c;
    
    return m;
}

MinirendTransform3D minirend_transform3d_perspective(float d) {
    MinirendTransform3D t = minirend_transform3d_identity();
    if (fabsf(d) > 1e-10f) {
        t.m[11] = -1.0f / d;
    }
    return t;
}

MinirendTransform3D minirend_transform3d_multiply(MinirendTransform3D a,
                                                   MinirendTransform3D b) {
    MinirendTransform3D r;
    
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) {
                sum += a.m[row + k*4] * b.m[k + col*4];
            }
            r.m[row + col*4] = sum;
        }
    }
    
    return r;
}

void minirend_transform3d_point(MinirendTransform3D t, float x, float y, float z,
                                float *out_x, float *out_y, float *out_z) {
    float w = t.m[3]*x + t.m[7]*y + t.m[11]*z + t.m[15];
    
    if (fabsf(w) < 1e-10f) w = 1.0f;
    
    *out_x = (t.m[0]*x + t.m[4]*y + t.m[8]*z + t.m[12]) / w;
    *out_y = (t.m[1]*x + t.m[5]*y + t.m[9]*z + t.m[13]) / w;
    *out_z = (t.m[2]*x + t.m[6]*y + t.m[10]*z + t.m[14]) / w;
}

MinirendTransform3D minirend_transform_to_3d(MinirendTransform2D t) {
    MinirendTransform3D r = minirend_transform3d_identity();
    
    r.m[0] = t.m[0];   /* a */
    r.m[1] = t.m[1];   /* b */
    r.m[4] = t.m[2];   /* c */
    r.m[5] = t.m[3];   /* d */
    r.m[12] = t.m[4];  /* tx */
    r.m[13] = t.m[5];  /* ty */
    
    return r;
}

MinirendTransform2D minirend_transform_from_3d(MinirendTransform3D t) {
    MinirendTransform2D r;
    
    r.m[0] = t.m[0];   /* a */
    r.m[1] = t.m[1];   /* b */
    r.m[2] = t.m[4];   /* c */
    r.m[3] = t.m[5];   /* d */
    r.m[4] = t.m[12];  /* tx */
    r.m[5] = t.m[13];  /* ty */
    
    return r;
}

