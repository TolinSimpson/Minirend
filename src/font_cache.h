#ifndef MINIREND_FONT_CACHE_H
#define MINIREND_FONT_CACHE_H

/*
 * Font Cache - Glyph atlas management using stb_truetype.
 *
 * This module:
 * - Loads TTF/OTF fonts
 * - Rasterizes glyphs on demand
 * - Manages glyph atlas textures
 * - Provides text measurement
 */

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* ============================================================================
 * Font Cache Types
 * ============================================================================ */

typedef struct MinirendFontCache MinirendFontCache;

/* Glyph information for rendering */
typedef struct {
    /* Texture coordinates (normalized 0-1) */
    float u0, v0, u1, v1;
    
    /* Quad offset from cursor (pixels) */
    float x_offset, y_offset;
    
    /* Quad dimensions (pixels) */
    float width, height;
    
    /* Advance to next glyph (pixels) */
    float advance;
    
    /* Font size this glyph was rendered at */
    float font_size;
    
} MinirendGlyph;

/* ============================================================================
 * Font Cache API
 * ============================================================================ */

/* Create the font cache. Must be called after sokol_gfx is initialized.
 * atlas_size is the texture atlas dimension (e.g., 512 or 1024).
 * max_glyphs is the maximum number of cached glyphs. */
MinirendFontCache *minirend_font_cache_create(int atlas_size, int max_glyphs);

/* Destroy the font cache and free resources. */
void minirend_font_cache_destroy(MinirendFontCache *cache);

/* Load a font from file. Returns font_id or -1 on failure. */
int minirend_font_cache_load_font(MinirendFontCache *cache, const char *path);

/* Load a font from memory. Returns font_id or -1 on failure.
 * data must remain valid for the lifetime of the font. */
int minirend_font_cache_load_font_memory(MinirendFontCache *cache,
                                         const unsigned char *data, size_t size);

/* Set the default font to use when no font_id is specified. */
void minirend_font_cache_set_default_font(MinirendFontCache *cache, int font_id);

/* Get a glyph, rasterizing it if necessary.
 * Returns false if glyph could not be retrieved. */
bool minirend_font_cache_get_glyph(MinirendFontCache *cache,
                                   int font_id, int codepoint,
                                   float font_size,
                                   MinirendGlyph *out_glyph);

/* Get the texture handle for the glyph atlas.
 * Returns a sokol_gfx sg_image handle. */
uint32_t minirend_font_cache_get_texture(MinirendFontCache *cache);

/* Measure text dimensions without rendering.
 * Returns width in out_width, height in out_height. */
void minirend_font_cache_measure_text(MinirendFontCache *cache,
                                      int font_id,
                                      const char *text, int32_t len,
                                      float font_size,
                                      float *out_width, float *out_height);

/* Get font metrics for a given size. */
void minirend_font_cache_get_metrics(MinirendFontCache *cache,
                                     int font_id, float font_size,
                                     float *out_ascent, float *out_descent,
                                     float *out_line_gap);

/* Clear all cached glyphs (useful if atlas is full). */
void minirend_font_cache_clear(MinirendFontCache *cache);

#endif /* MINIREND_FONT_CACHE_H */

