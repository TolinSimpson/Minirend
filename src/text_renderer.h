#ifndef MINIREND_TEXT_RENDERER_H
#define MINIREND_TEXT_RENDERER_H

/*
 * Text Renderer - Draws text using the font cache and sokol_gfx.
 *
 * Uses textured quads for each glyph, batched for performance.
 */

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "style_resolver.h"  /* For MinirendColor */

/* Forward declarations */
typedef struct MinirendFontCache MinirendFontCache;

/* ============================================================================
 * Text Renderer Context
 * ============================================================================ */

typedef struct MinirendTextRenderer MinirendTextRenderer;

/* Create the text renderer. Must be called after sokol_gfx is initialized.
 * font_cache is borrowed (not owned). */
MinirendTextRenderer *minirend_text_renderer_create(MinirendFontCache *font_cache);

/* Destroy the text renderer and free GPU resources. */
void minirend_text_renderer_destroy(MinirendTextRenderer *renderer);

/* Begin a new frame. Call before any draw calls. */
void minirend_text_renderer_begin(MinirendTextRenderer *renderer,
                                  float viewport_width, float viewport_height);

/* End the frame and flush all batched draws. */
void minirend_text_renderer_end(MinirendTextRenderer *renderer);

/* ============================================================================
 * Drawing Functions
 * ============================================================================ */

/* Draw text at position (x, y is baseline). */
void minirend_text_draw(MinirendTextRenderer *renderer,
                        const char *text, int32_t len,
                        float x, float y,
                        float font_size, int font_weight,
                        MinirendColor color);

/* Draw text with a specific font. */
void minirend_text_draw_with_font(MinirendTextRenderer *renderer,
                                  int font_id,
                                  const char *text, int32_t len,
                                  float x, float y,
                                  float font_size, int font_weight,
                                  MinirendColor color);

/* Measure text dimensions. */
void minirend_text_measure(MinirendTextRenderer *renderer,
                           const char *text, int32_t len,
                           float font_size, int font_weight,
                           float *out_width, float *out_height);

#endif /* MINIREND_TEXT_RENDERER_H */

