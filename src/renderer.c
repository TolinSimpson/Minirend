/*
 * Minirend HTML/CSS Renderer
 *
 * Integrates:
 * - Lexbor for HTML/CSS parsing
 * - Style resolver for computed styles
 * - Layout engine (clay.h) for positioning
 * - Box renderer for backgrounds/borders
 * - Text renderer for text content
 * - Transform support for CSS transforms
 * - Compositing for layered rendering
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "minirend.h"
#include "lexbor_adapter.h"
#include "style_resolver.h"
#include "layout_engine.h"
#include "box_renderer.h"
#include "font_cache.h"
#include "text_renderer.h"
#include "transform.h"
#include "ui_tree.h"

#include "sokol_gfx.h"

/* ============================================================================
 * Renderer State
 * ============================================================================ */

typedef struct {
    /* Parsed document */
    LexborDocument *doc;
    
    /* Style resolver */
    MinirendStyleResolver *style_resolver;
    
    /* Layout engine */
    MinirendLayoutEngine *layout_engine;
    
    /* Renderers */
    MinirendBoxRenderer  *box_renderer;
    MinirendFontCache    *font_cache;
    MinirendTextRenderer *text_renderer;
    
    /* Viewport */
    float viewport_width;
    float viewport_height;
    
    /* State */
    bool initialized;
    bool layout_dirty;
    
} RendererState;

static RendererState g_renderer = {0};

/* ============================================================================
 * Text Measurement Callback for Layout Engine
 * ============================================================================ */

static void measure_text_callback(const char *text, int32_t len,
                                  float font_size, int font_weight,
                                  float *out_width, float *out_height,
                                  void *user_data) {
    (void)user_data;
    (void)font_weight;
    
    if (g_renderer.font_cache) {
        minirend_font_cache_measure_text(g_renderer.font_cache, -1,
                                         text, len, font_size,
                                         out_width, out_height);
    } else {
        /* Fallback: approximate */
        *out_width = (float)len * font_size * 0.5f;
        *out_height = font_size;
    }
}

/* ============================================================================
 * Initialization
 * ============================================================================ */

void minirend_renderer_init(MinirendApp *app) {
    (void)app;
    
    if (g_renderer.initialized) return;
    
    /* Default viewport */
    g_renderer.viewport_width = 1280.0f;
    g_renderer.viewport_height = 720.0f;
    
    /* Create box renderer */
    g_renderer.box_renderer = minirend_box_renderer_create();
    if (!g_renderer.box_renderer) {
        fprintf(stderr, "[renderer] Failed to create box renderer\n");
    }
    
    /* Create font cache */
    g_renderer.font_cache = minirend_font_cache_create(1024, 2048);
    if (!g_renderer.font_cache) {
        fprintf(stderr, "[renderer] Failed to create font cache\n");
    }
    
    /* Create text renderer */
    if (g_renderer.font_cache) {
        g_renderer.text_renderer = minirend_text_renderer_create(g_renderer.font_cache);
        if (!g_renderer.text_renderer) {
            fprintf(stderr, "[renderer] Failed to create text renderer\n");
        }
    }
    
    /* Create layout engine */
    g_renderer.layout_engine = minirend_layout_engine_create(
        g_renderer.viewport_width, g_renderer.viewport_height);
    if (g_renderer.layout_engine) {
        minirend_layout_engine_set_measure_text(g_renderer.layout_engine,
                                                measure_text_callback, NULL);
    }
    
    g_renderer.initialized = true;
    g_renderer.layout_dirty = true;
    
    fprintf(stderr, "[renderer] HTML/CSS renderer initialized\n");
}

void minirend_renderer_shutdown(void) {
    if (!g_renderer.initialized) return;
    
    if (g_renderer.text_renderer) {
        minirend_text_renderer_destroy(g_renderer.text_renderer);
        g_renderer.text_renderer = NULL;
    }
    
    if (g_renderer.font_cache) {
        minirend_font_cache_destroy(g_renderer.font_cache);
        g_renderer.font_cache = NULL;
    }
    
    if (g_renderer.box_renderer) {
        minirend_box_renderer_destroy(g_renderer.box_renderer);
        g_renderer.box_renderer = NULL;
    }
    
    if (g_renderer.layout_engine) {
        minirend_layout_engine_destroy(g_renderer.layout_engine);
        g_renderer.layout_engine = NULL;
    }
    
    if (g_renderer.style_resolver) {
        minirend_style_resolver_destroy(g_renderer.style_resolver);
        g_renderer.style_resolver = NULL;
    }
    
    if (g_renderer.doc) {
        minirend_lexbor_document_destroy(g_renderer.doc);
        g_renderer.doc = NULL;
    }
    
    g_renderer.initialized = false;
}

/* ============================================================================
 * HTML Loading
 * ============================================================================ */

static char *read_file_contents(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (size <= 0) {
        fclose(f);
        return NULL;
    }
    
    char *data = malloc(size + 1);
    if (!data) {
        fclose(f);
        return NULL;
    }
    
    if (fread(data, 1, size, f) != (size_t)size) {
        free(data);
        fclose(f);
        return NULL;
    }
    
    data[size] = '\0';
    fclose(f);
    
    if (out_len) *out_len = size;
    return data;
}

void minirend_renderer_load_html(MinirendApp *app, const char *path) {
    (void)app;
    
    if (!g_renderer.initialized) {
        minirend_renderer_init(app);
    }
    
    /* Read HTML file */
    size_t html_len = 0;
    char *html = read_file_contents(path, &html_len);
    if (!html) {
        fprintf(stderr, "[renderer] Failed to read HTML file: %s\n", path);
        return;
    }
    
    /* Destroy previous document */
    if (g_renderer.doc) {
        minirend_lexbor_document_destroy(g_renderer.doc);
        g_renderer.doc = NULL;
    }
    if (g_renderer.style_resolver) {
        minirend_style_resolver_destroy(g_renderer.style_resolver);
        g_renderer.style_resolver = NULL;
    }
    
    /* Parse HTML */
    g_renderer.doc = minirend_lexbor_parse_html(html, html_len);
    free(html);
    
    if (!g_renderer.doc) {
        fprintf(stderr, "[renderer] Failed to parse HTML\n");
        return;
    }
    
    /* Create style resolver */
    g_renderer.style_resolver = minirend_style_resolver_create(
        g_renderer.doc,
        g_renderer.viewport_width,
        g_renderer.viewport_height);
    
    if (!g_renderer.style_resolver) {
        fprintf(stderr, "[renderer] Failed to create style resolver\n");
        return;
    }
    
    /* TODO: Extract and parse <style> blocks */
    /* TODO: Load external stylesheets */
    
    g_renderer.layout_dirty = true;
    
    fprintf(stderr, "[renderer] Loaded HTML: %s\n", path);
}

/* ============================================================================
 * Viewport Management
 * ============================================================================ */

void minirend_renderer_set_viewport(float width, float height) {
    if (width != g_renderer.viewport_width || height != g_renderer.viewport_height) {
        g_renderer.viewport_width = width;
        g_renderer.viewport_height = height;
        g_renderer.layout_dirty = true;
        
        if (g_renderer.style_resolver) {
            minirend_style_resolver_set_viewport(g_renderer.style_resolver, width, height);
        }
        if (g_renderer.layout_engine) {
            minirend_layout_engine_set_viewport(g_renderer.layout_engine, width, height);
        }
    }
}

/* ============================================================================
 * Drawing
 * ============================================================================ */

void minirend_renderer_draw(MinirendApp *app) {
    (void)app;
    
    if (!g_renderer.initialized) return;
    if (!g_renderer.doc || !g_renderer.style_resolver) return;
    
    /* Recompute layout if dirty */
    if (g_renderer.layout_dirty && g_renderer.layout_engine) {
        minirend_layout_engine_compute(g_renderer.layout_engine,
                                       g_renderer.doc,
                                       g_renderer.style_resolver);
        g_renderer.layout_dirty = false;
    }
    
    /* Get layout nodes */
    int node_count = 0;
    const MinirendLayoutNode *nodes = minirend_layout_get_nodes(
        g_renderer.layout_engine, &node_count);
    
    if (!nodes || node_count == 0) return;
    
    /* Begin rendering */
    if (g_renderer.box_renderer) {
        minirend_box_renderer_begin(g_renderer.box_renderer,
                                    g_renderer.viewport_width,
                                    g_renderer.viewport_height);
    }
    if (g_renderer.text_renderer) {
        minirend_text_renderer_begin(g_renderer.text_renderer,
                                     g_renderer.viewport_width,
                                     g_renderer.viewport_height);
    }
    
    /* Draw each layout node */
    for (int i = 0; i < node_count; i++) {
        const MinirendLayoutNode *node = &nodes[i];
        
        switch (node->type) {
            case MINIREND_LAYOUT_BOX:
                if (g_renderer.box_renderer && node->background_color.a > 0) {
                    if (node->corner_radius > 0) {
                        minirend_box_draw_rounded_rect(g_renderer.box_renderer,
                            node->x, node->y, node->width, node->height,
                            node->background_color, node->corner_radius);
                    } else {
                        minirend_box_draw_rect(g_renderer.box_renderer,
                            node->x, node->y, node->width, node->height,
                            node->background_color);
                    }
                }
                break;
                
            case MINIREND_LAYOUT_TEXT:
                if (g_renderer.text_renderer && node->text && node->text_len > 0) {
                    /* Get font ascent for baseline positioning */
                    float ascent = node->font_size * 0.8f;  /* Approximate */
                    minirend_text_draw(g_renderer.text_renderer,
                        node->text, node->text_len,
                        node->x, node->y + ascent,
                        node->font_size, node->font_weight,
                        node->text_color);
                }
                break;
                
            case MINIREND_LAYOUT_BORDER:
                if (g_renderer.box_renderer && node->border_color.a > 0) {
                    minirend_box_draw_border(g_renderer.box_renderer,
                        node->x, node->y, node->width, node->height,
                        node->border_top_width, node->border_right_width,
                        node->border_bottom_width, node->border_left_width,
                        node->border_color);
                }
                break;
                
            case MINIREND_LAYOUT_SCISSOR_START:
                if (g_renderer.box_renderer) {
                    /* Flush current batch before scissor change */
                    minirend_box_renderer_end(g_renderer.box_renderer);
                    minirend_box_renderer_begin(g_renderer.box_renderer,
                        g_renderer.viewport_width, g_renderer.viewport_height);
                    minirend_box_set_scissor(g_renderer.box_renderer,
                        node->x, node->y, node->width, node->height);
                }
                break;
                
            case MINIREND_LAYOUT_SCISSOR_END:
                if (g_renderer.box_renderer) {
                    minirend_box_clear_scissor(g_renderer.box_renderer);
                }
                break;
                
            default:
                break;
        }
        
        /* Update UI tree bounds for hit testing */
        if (node->node_id > 0) {
            MinirendRect bounds = {
                .x = node->x,
                .y = node->y,
                .w = node->width,
                .h = node->height
            };
            minirend_ui_tree_set_bounds(node->node_id, bounds);
        }
    }
    
    /* End rendering */
    if (g_renderer.box_renderer) {
        minirend_box_renderer_end(g_renderer.box_renderer);
    }
    if (g_renderer.text_renderer) {
        minirend_text_renderer_end(g_renderer.text_renderer);
    }
}

/* ============================================================================
 * Font Loading
 * ============================================================================ */

int minirend_renderer_load_font(const char *path) {
    if (!g_renderer.font_cache) return -1;
    return minirend_font_cache_load_font(g_renderer.font_cache, path);
}

/* ============================================================================
 * Stylesheet Management
 * ============================================================================ */

bool minirend_renderer_add_stylesheet(const char *css, size_t len) {
    if (!g_renderer.style_resolver) return false;
    
    bool ok = minirend_style_resolver_add_stylesheet(g_renderer.style_resolver, css, len);
    if (ok) {
        g_renderer.layout_dirty = true;
    }
    return ok;
}
