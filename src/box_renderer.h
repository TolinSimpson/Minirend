#ifndef MINIREND_BOX_RENDERER_H
#define MINIREND_BOX_RENDERER_H

/*
 * Box Renderer - Draws rectangles, backgrounds, and borders using sokol_gfx.
 *
 * Uses batched quad rendering with a simple color shader.
 */

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "style_resolver.h"  /* For MinirendColor */

/* ============================================================================
 * Box Renderer Context
 * ============================================================================ */

typedef struct MinirendBoxRenderer MinirendBoxRenderer;

/* Create the box renderer. Must be called after sokol_gfx is initialized. */
MinirendBoxRenderer *minirend_box_renderer_create(void);

/* Destroy the box renderer and free GPU resources. */
void minirend_box_renderer_destroy(MinirendBoxRenderer *renderer);

/* Begin a new frame. Call before any draw calls. */
void minirend_box_renderer_begin(MinirendBoxRenderer *renderer,
                                 float viewport_width, float viewport_height);

/* End the frame and flush all batched draws. */
void minirend_box_renderer_end(MinirendBoxRenderer *renderer);

/* ============================================================================
 * Drawing Functions
 * ============================================================================ */

/* Draw a filled rectangle. */
void minirend_box_draw_rect(MinirendBoxRenderer *renderer,
                            float x, float y, float width, float height,
                            MinirendColor color);

/* Draw a filled rectangle with rounded corners. */
void minirend_box_draw_rounded_rect(MinirendBoxRenderer *renderer,
                                    float x, float y, float width, float height,
                                    MinirendColor color, float radius);

/* Draw a border (outline) around a rectangle. */
void minirend_box_draw_border(MinirendBoxRenderer *renderer,
                              float x, float y, float width, float height,
                              float border_top, float border_right,
                              float border_bottom, float border_left,
                              MinirendColor color);

/* Draw a border with rounded corners. */
void minirend_box_draw_rounded_border(MinirendBoxRenderer *renderer,
                                      float x, float y, float width, float height,
                                      float border_width,
                                      MinirendColor color, float radius);

/* Set scissor rectangle for clipping. */
void minirend_box_set_scissor(MinirendBoxRenderer *renderer,
                              float x, float y, float width, float height);

/* Clear scissor (disable clipping). */
void minirend_box_clear_scissor(MinirendBoxRenderer *renderer);

#endif /* MINIREND_BOX_RENDERER_H */

