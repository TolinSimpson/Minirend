#ifndef MINIREND_TRANSFORM_H
#define MINIREND_TRANSFORM_H

/*
 * CSS Transform Matrix Utilities
 *
 * Handles 2D/3D CSS transforms by building transformation matrices.
 */

#include <stdbool.h>

/* ============================================================================
 * 2D Transform (3x3 matrix stored as 6 floats: [a, b, c, d, tx, ty])
 *
 * Matrix form:
 *   | a  c  tx |
 *   | b  d  ty |
 *   | 0  0  1  |
 *
 * Transforms point (x, y) as:
 *   x' = a*x + c*y + tx
 *   y' = b*x + d*y + ty
 * ============================================================================ */

typedef struct {
    float m[6];  /* [a, b, c, d, tx, ty] */
} MinirendTransform2D;

/* Create identity transform */
MinirendTransform2D minirend_transform_identity(void);

/* Create translation transform */
MinirendTransform2D minirend_transform_translate(float tx, float ty);

/* Create scale transform (around origin) */
MinirendTransform2D minirend_transform_scale(float sx, float sy);

/* Create rotation transform (angle in radians, around origin) */
MinirendTransform2D minirend_transform_rotate(float angle);

/* Create skew transform (angles in radians) */
MinirendTransform2D minirend_transform_skew(float ax, float ay);

/* Multiply two transforms: result = a * b */
MinirendTransform2D minirend_transform_multiply(MinirendTransform2D a,
                                                 MinirendTransform2D b);

/* Apply transform to a point */
void minirend_transform_point(MinirendTransform2D t, float x, float y,
                              float *out_x, float *out_y);

/* Check if transform is identity */
bool minirend_transform_is_identity(MinirendTransform2D t);

/* Invert a transform. Returns false if not invertible. */
bool minirend_transform_invert(MinirendTransform2D t, MinirendTransform2D *out);

/* ============================================================================
 * 3D Transform (4x4 matrix)
 *
 * For full CSS 3D transforms. Stored column-major for OpenGL compatibility.
 * ============================================================================ */

typedef struct {
    float m[16];  /* Column-major 4x4 matrix */
} MinirendTransform3D;

/* Create identity 4x4 transform */
MinirendTransform3D minirend_transform3d_identity(void);

/* Create translation */
MinirendTransform3D minirend_transform3d_translate(float tx, float ty, float tz);

/* Create scale */
MinirendTransform3D minirend_transform3d_scale(float sx, float sy, float sz);

/* Create rotation around X axis */
MinirendTransform3D minirend_transform3d_rotate_x(float angle);

/* Create rotation around Y axis */
MinirendTransform3D minirend_transform3d_rotate_y(float angle);

/* Create rotation around Z axis */
MinirendTransform3D minirend_transform3d_rotate_z(float angle);

/* Create rotation around arbitrary axis */
MinirendTransform3D minirend_transform3d_rotate(float x, float y, float z, float angle);

/* Create perspective transform */
MinirendTransform3D minirend_transform3d_perspective(float d);

/* Multiply 4x4 matrices: result = a * b */
MinirendTransform3D minirend_transform3d_multiply(MinirendTransform3D a,
                                                   MinirendTransform3D b);

/* Apply 4x4 transform to a point (with perspective divide) */
void minirend_transform3d_point(MinirendTransform3D t, float x, float y, float z,
                                float *out_x, float *out_y, float *out_z);

/* Convert 2D transform to 3D (sets z-related components to identity) */
MinirendTransform3D minirend_transform_to_3d(MinirendTransform2D t);

/* Extract 2D transform from 3D (ignores z components) */
MinirendTransform2D minirend_transform_from_3d(MinirendTransform3D t);

#endif /* MINIREND_TRANSFORM_H */

