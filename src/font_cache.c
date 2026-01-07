/*
 * Font Cache Implementation
 *
 * Uses stb_truetype for font loading and glyph rasterization.
 */

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include "font_cache.h"
#include "sokol_gfx.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ============================================================================
 * Constants
 * ============================================================================ */

#define MAX_FONTS 16
#define DEFAULT_ATLAS_SIZE 1024
#define DEFAULT_MAX_GLYPHS 1024

/* ============================================================================
 * Internal Types
 * ============================================================================ */

typedef struct {
    stbtt_fontinfo info;
    unsigned char *data;    /* Font file data (owned if loaded from file) */
    bool          data_owned;
    float         scale_for_pixel_height;
} LoadedFont;

typedef struct {
    int     font_id;
    int     codepoint;
    float   font_size;
    
    /* Atlas position */
    int     atlas_x, atlas_y;
    int     atlas_w, atlas_h;
    
    /* Glyph metrics */
    float   x_offset, y_offset;
    float   advance;
    
} CachedGlyph;

struct MinirendFontCache {
    /* Loaded fonts */
    LoadedFont  fonts[MAX_FONTS];
    int         font_count;
    int         default_font;
    
    /* Glyph cache */
    CachedGlyph *glyphs;
    int          glyph_count;
    int          max_glyphs;
    
    /* Atlas */
    unsigned char *atlas_data;
    int            atlas_size;
    int            atlas_row_height;
    int            atlas_x, atlas_y;  /* Current packing position */
    sg_image       atlas_texture;
    bool           atlas_dirty;
};

/* ============================================================================
 * Create/Destroy
 * ============================================================================ */

MinirendFontCache *minirend_font_cache_create(int atlas_size, int max_glyphs) {
    MinirendFontCache *cache = calloc(1, sizeof(MinirendFontCache));
    if (!cache) return NULL;
    
    cache->atlas_size = atlas_size > 0 ? atlas_size : DEFAULT_ATLAS_SIZE;
    cache->max_glyphs = max_glyphs > 0 ? max_glyphs : DEFAULT_MAX_GLYPHS;
    cache->default_font = -1;
    
    /* Allocate glyph cache */
    cache->glyphs = calloc(cache->max_glyphs, sizeof(CachedGlyph));
    if (!cache->glyphs) {
        free(cache);
        return NULL;
    }
    
    /* Allocate atlas data */
    cache->atlas_data = calloc(cache->atlas_size * cache->atlas_size, 1);
    if (!cache->atlas_data) {
        free(cache->glyphs);
        free(cache);
        return NULL;
    }
    
    /* Create atlas texture */
    cache->atlas_texture = sg_make_image(&(sg_image_desc){
        .width = cache->atlas_size,
        .height = cache->atlas_size,
        .pixel_format = SG_PIXELFORMAT_R8,
        .usage = SG_USAGE_DYNAMIC,
    });
    
    return cache;
}

void minirend_font_cache_destroy(MinirendFontCache *cache) {
    if (!cache) return;
    
    /* Free font data */
    for (int i = 0; i < cache->font_count; i++) {
        if (cache->fonts[i].data_owned && cache->fonts[i].data) {
            free(cache->fonts[i].data);
        }
    }
    
    sg_destroy_image(cache->atlas_texture);
    free(cache->atlas_data);
    free(cache->glyphs);
    free(cache);
}

/* ============================================================================
 * Font Loading
 * ============================================================================ */

static unsigned char *load_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (size <= 0) {
        fclose(f);
        return NULL;
    }
    
    unsigned char *data = malloc(size);
    if (!data) {
        fclose(f);
        return NULL;
    }
    
    if (fread(data, 1, size, f) != (size_t)size) {
        free(data);
        fclose(f);
        return NULL;
    }
    
    fclose(f);
    
    if (out_size) *out_size = size;
    return data;
}

int minirend_font_cache_load_font(MinirendFontCache *cache, const char *path) {
    if (!cache || !path) return -1;
    if (cache->font_count >= MAX_FONTS) return -1;
    
    size_t size = 0;
    unsigned char *data = load_file(path, &size);
    if (!data) return -1;
    
    int font_id = minirend_font_cache_load_font_memory(cache, data, size);
    if (font_id < 0) {
        free(data);
        return -1;
    }
    
    cache->fonts[font_id].data_owned = true;
    return font_id;
}

int minirend_font_cache_load_font_memory(MinirendFontCache *cache,
                                         const unsigned char *data, size_t size) {
    if (!cache || !data || size == 0) return -1;
    if (cache->font_count >= MAX_FONTS) return -1;
    
    int font_id = cache->font_count;
    LoadedFont *font = &cache->fonts[font_id];
    
    font->data = (unsigned char *)data;
    font->data_owned = false;
    
    if (!stbtt_InitFont(&font->info, data, 0)) {
        return -1;
    }
    
    cache->font_count++;
    
    /* Set as default if first font */
    if (cache->default_font < 0) {
        cache->default_font = font_id;
    }
    
    return font_id;
}

void minirend_font_cache_set_default_font(MinirendFontCache *cache, int font_id) {
    if (!cache) return;
    if (font_id >= 0 && font_id < cache->font_count) {
        cache->default_font = font_id;
    }
}

/* ============================================================================
 * Glyph Caching
 * ============================================================================ */

static CachedGlyph *find_cached_glyph(MinirendFontCache *cache,
                                      int font_id, int codepoint, float font_size) {
    /* Simple linear search - could be optimized with hash table */
    for (int i = 0; i < cache->glyph_count; i++) {
        CachedGlyph *g = &cache->glyphs[i];
        if (g->font_id == font_id && g->codepoint == codepoint &&
            fabsf(g->font_size - font_size) < 0.5f) {
            return g;
        }
    }
    return NULL;
}

static CachedGlyph *cache_glyph(MinirendFontCache *cache,
                                int font_id, int codepoint, float font_size) {
    if (font_id < 0 || font_id >= cache->font_count) return NULL;
    
    LoadedFont *font = &cache->fonts[font_id];
    float scale = stbtt_ScaleForPixelHeight(&font->info, font_size);
    
    /* Get glyph metrics */
    int advance, lsb;
    stbtt_GetCodepointHMetrics(&font->info, codepoint, &advance, &lsb);
    
    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(&font->info, codepoint, scale, scale,
                                &x0, &y0, &x1, &y1);
    
    int glyph_w = x1 - x0;
    int glyph_h = y1 - y0;
    
    /* Check if glyph fits in current row */
    if (cache->atlas_x + glyph_w + 1 > cache->atlas_size) {
        /* Move to next row */
        cache->atlas_x = 0;
        cache->atlas_y += cache->atlas_row_height + 1;
        cache->atlas_row_height = 0;
    }
    
    /* Check if atlas is full */
    if (cache->atlas_y + glyph_h > cache->atlas_size) {
        /* Atlas full - clear and restart */
        minirend_font_cache_clear(cache);
    }
    
    /* Ensure glyph cache has space */
    if (cache->glyph_count >= cache->max_glyphs) {
        minirend_font_cache_clear(cache);
    }
    
    /* Rasterize glyph */
    if (glyph_w > 0 && glyph_h > 0) {
        unsigned char *dest = cache->atlas_data +
                              cache->atlas_y * cache->atlas_size + cache->atlas_x;
        stbtt_MakeCodepointBitmap(&font->info, dest,
                                  glyph_w, glyph_h, cache->atlas_size,
                                  scale, scale, codepoint);
    }
    
    /* Add to cache */
    CachedGlyph *g = &cache->glyphs[cache->glyph_count++];
    g->font_id = font_id;
    g->codepoint = codepoint;
    g->font_size = font_size;
    g->atlas_x = cache->atlas_x;
    g->atlas_y = cache->atlas_y;
    g->atlas_w = glyph_w;
    g->atlas_h = glyph_h;
    g->x_offset = (float)x0;
    g->y_offset = (float)y0;
    g->advance = (float)advance * scale;
    
    /* Update packing position */
    cache->atlas_x += glyph_w + 1;
    if (glyph_h > cache->atlas_row_height) {
        cache->atlas_row_height = glyph_h;
    }
    
    cache->atlas_dirty = true;
    
    return g;
}

bool minirend_font_cache_get_glyph(MinirendFontCache *cache,
                                   int font_id, int codepoint,
                                   float font_size,
                                   MinirendGlyph *out_glyph) {
    if (!cache || !out_glyph) return false;
    
    /* Use default font if not specified */
    if (font_id < 0) font_id = cache->default_font;
    if (font_id < 0 || font_id >= cache->font_count) return false;
    
    /* Find or cache glyph */
    CachedGlyph *g = find_cached_glyph(cache, font_id, codepoint, font_size);
    if (!g) {
        g = cache_glyph(cache, font_id, codepoint, font_size);
        if (!g) return false;
    }
    
    /* Fill output */
    float inv_size = 1.0f / (float)cache->atlas_size;
    out_glyph->u0 = (float)g->atlas_x * inv_size;
    out_glyph->v0 = (float)g->atlas_y * inv_size;
    out_glyph->u1 = (float)(g->atlas_x + g->atlas_w) * inv_size;
    out_glyph->v1 = (float)(g->atlas_y + g->atlas_h) * inv_size;
    out_glyph->x_offset = g->x_offset;
    out_glyph->y_offset = g->y_offset;
    out_glyph->width = (float)g->atlas_w;
    out_glyph->height = (float)g->atlas_h;
    out_glyph->advance = g->advance;
    out_glyph->font_size = font_size;
    
    return true;
}

uint32_t minirend_font_cache_get_texture(MinirendFontCache *cache) {
    if (!cache) return 0;
    
    /* Upload atlas if dirty */
    if (cache->atlas_dirty) {
        sg_update_image(cache->atlas_texture, &(sg_image_data){
            .subimage[0][0] = {
                .ptr = cache->atlas_data,
                .size = cache->atlas_size * cache->atlas_size,
            },
        });
        cache->atlas_dirty = false;
    }
    
    return cache->atlas_texture.id;
}

void minirend_font_cache_measure_text(MinirendFontCache *cache,
                                      int font_id,
                                      const char *text, int32_t len,
                                      float font_size,
                                      float *out_width, float *out_height) {
    if (!cache || !text) {
        if (out_width) *out_width = 0;
        if (out_height) *out_height = font_size;
        return;
    }
    
    if (font_id < 0) font_id = cache->default_font;
    if (font_id < 0 || font_id >= cache->font_count) {
        if (out_width) *out_width = 0;
        if (out_height) *out_height = font_size;
        return;
    }
    
    LoadedFont *font = &cache->fonts[font_id];
    float scale = stbtt_ScaleForPixelHeight(&font->info, font_size);
    
    float width = 0;
    
    if (len < 0) len = (int32_t)strlen(text);
    
    for (int32_t i = 0; i < len; i++) {
        int codepoint = (unsigned char)text[i];
        
        /* Handle UTF-8 (basic single-byte for now) */
        int advance, lsb;
        stbtt_GetCodepointHMetrics(&font->info, codepoint, &advance, &lsb);
        width += (float)advance * scale;
    }
    
    if (out_width) *out_width = width;
    if (out_height) *out_height = font_size;
}

void minirend_font_cache_get_metrics(MinirendFontCache *cache,
                                     int font_id, float font_size,
                                     float *out_ascent, float *out_descent,
                                     float *out_line_gap) {
    if (!cache) return;
    
    if (font_id < 0) font_id = cache->default_font;
    if (font_id < 0 || font_id >= cache->font_count) return;
    
    LoadedFont *font = &cache->fonts[font_id];
    float scale = stbtt_ScaleForPixelHeight(&font->info, font_size);
    
    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&font->info, &ascent, &descent, &line_gap);
    
    if (out_ascent) *out_ascent = (float)ascent * scale;
    if (out_descent) *out_descent = (float)descent * scale;
    if (out_line_gap) *out_line_gap = (float)line_gap * scale;
}

void minirend_font_cache_clear(MinirendFontCache *cache) {
    if (!cache) return;
    
    /* Clear atlas */
    memset(cache->atlas_data, 0, cache->atlas_size * cache->atlas_size);
    cache->atlas_x = 0;
    cache->atlas_y = 0;
    cache->atlas_row_height = 0;
    cache->atlas_dirty = true;
    
    /* Clear glyph cache */
    cache->glyph_count = 0;
}

