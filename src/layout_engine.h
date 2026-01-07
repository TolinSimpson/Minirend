#ifndef MINIREND_LAYOUT_ENGINE_H
#define MINIREND_LAYOUT_ENGINE_H

/*
 * Layout Engine - Computes element positions using clay.h
 *
 * This module:
 * - Takes a DOM tree and computed styles
 * - Feeds elements into clay.h for layout computation
 * - Outputs positioned render commands for the renderer
 */

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "style_resolver.h"

/* Forward declarations */
typedef struct LexborDocument LexborDocument;
typedef struct lxb_dom_node lxb_dom_node_t;
typedef struct MinirendStyleResolver MinirendStyleResolver;

/* ============================================================================
 * Layout Node - A positioned element ready for rendering
 * ============================================================================ */

typedef enum {
    MINIREND_LAYOUT_NONE = 0,
    MINIREND_LAYOUT_BOX,         /* Rectangle/background */
    MINIREND_LAYOUT_TEXT,        /* Text content */
    MINIREND_LAYOUT_BORDER,      /* Border edges */
    MINIREND_LAYOUT_SCISSOR_START,  /* Begin clipping */
    MINIREND_LAYOUT_SCISSOR_END,    /* End clipping */
} MinirendLayoutType;

typedef struct {
    /* Bounding box in screen coordinates (pixels) */
    float x, y, width, height;
    
    /* Element identity */
    int32_t  node_id;       /* DOM node id for hit testing */
    uint32_t clay_id;       /* Internal clay element id */
    
    /* Render type */
    MinirendLayoutType type;
    
    /* Style data (copied from computed style for rendering) */
    MinirendColor background_color;
    MinirendColor border_color;
    float border_top_width;
    float border_right_width;
    float border_bottom_width;
    float border_left_width;
    float corner_radius;
    
    /* Text data (when type == MINIREND_LAYOUT_TEXT) */
    const char *text;
    int32_t     text_len;
    MinirendColor text_color;
    float       font_size;
    int         font_weight;
    
    /* Opacity for compositing */
    float opacity;
    
    /* Transform (2D affine matrix) */
    bool  has_transform;
    float transform[6];
    
} MinirendLayoutNode;

/* ============================================================================
 * Layout Engine Context
 * ============================================================================ */

typedef struct MinirendLayoutEngine MinirendLayoutEngine;

/* Create a layout engine.
 * viewport_width/height set the layout dimensions. */
MinirendLayoutEngine *minirend_layout_engine_create(float viewport_width,
                                                     float viewport_height);

/* Destroy the layout engine and free resources. */
void minirend_layout_engine_destroy(MinirendLayoutEngine *engine);

/* Update viewport dimensions. */
void minirend_layout_engine_set_viewport(MinirendLayoutEngine *engine,
                                         float width, float height);

/* Perform layout on a DOM tree.
 * doc: The parsed HTML document
 * style_resolver: For computing element styles
 *
 * After this call, use minirend_layout_get_nodes() to retrieve positioned elements.
 * Returns the number of layout nodes generated. */
int minirend_layout_engine_compute(MinirendLayoutEngine *engine,
                                   LexborDocument *doc,
                                   MinirendStyleResolver *style_resolver);

/* Get the array of positioned layout nodes after compute().
 * out_count receives the number of nodes.
 * Returns pointer to internal array (valid until next compute call). */
const MinirendLayoutNode *minirend_layout_get_nodes(MinirendLayoutEngine *engine,
                                                    int *out_count);

/* Helper to check if a point is inside a layout node's bounds. */
bool minirend_layout_node_contains(const MinirendLayoutNode *node, float x, float y);

/* Text measurement callback type (provided by text renderer) */
typedef void (*MinirendMeasureTextFn)(const char *text, int32_t len,
                                      float font_size, int font_weight,
                                      float *out_width, float *out_height,
                                      void *user_data);

/* Set the text measurement callback for proper text layout. */
void minirend_layout_engine_set_measure_text(MinirendLayoutEngine *engine,
                                             MinirendMeasureTextFn fn,
                                             void *user_data);

#endif /* MINIREND_LAYOUT_ENGINE_H */

