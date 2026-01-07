#ifndef MINIREND_COMPOSITOR_H
#define MINIREND_COMPOSITOR_H

/*
 * Compositor - Render-to-texture layer compositing
 *
 * Handles:
 * - Creating offscreen render targets for layers
 * - Compositing layers with transforms and opacity
 * - Managing layer tree for elements with transforms, opacity < 1, etc.
 */

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "style_resolver.h"
#include "transform.h"

/* Forward declarations */
typedef struct MinirendBoxRenderer MinirendBoxRenderer;
typedef struct MinirendTextRenderer MinirendTextRenderer;

/* ============================================================================
 * Layer Types
 * ============================================================================ */

typedef struct MinirendLayer {
    /* Layer bounds */
    float x, y, width, height;
    
    /* Transform to apply when compositing */
    MinirendTransform2D transform;
    float transform_origin_x;
    float transform_origin_y;
    
    /* Opacity */
    float opacity;
    
    /* Render target (sokol_gfx handles) */
    uint32_t framebuffer;
    uint32_t texture;
    uint32_t depth_buffer;
    
    /* Layer tree */
    struct MinirendLayer *parent;
    struct MinirendLayer *first_child;
    struct MinirendLayer *next_sibling;
    
    /* Associated DOM node (for debugging) */
    int32_t node_id;
    
} MinirendLayer;

/* ============================================================================
 * Compositor Context
 * ============================================================================ */

typedef struct MinirendCompositor MinirendCompositor;

/* Create the compositor. */
MinirendCompositor *minirend_compositor_create(void);

/* Destroy the compositor. */
void minirend_compositor_destroy(MinirendCompositor *compositor);

/* Begin a compositing pass. */
void minirend_compositor_begin(MinirendCompositor *compositor,
                               float viewport_width, float viewport_height);

/* End compositing and render final output to screen. */
void minirend_compositor_end(MinirendCompositor *compositor);

/* Create a new layer. Returns layer handle or NULL on failure. */
MinirendLayer *minirend_compositor_create_layer(MinirendCompositor *compositor,
                                                float width, float height);

/* Destroy a layer and free its resources. */
void minirend_compositor_destroy_layer(MinirendCompositor *compositor,
                                       MinirendLayer *layer);

/* Begin rendering to a layer. */
void minirend_compositor_begin_layer(MinirendCompositor *compositor,
                                     MinirendLayer *layer);

/* End rendering to current layer. */
void minirend_compositor_end_layer(MinirendCompositor *compositor);

/* Composite a layer to the current target. */
void minirend_compositor_draw_layer(MinirendCompositor *compositor,
                                    MinirendLayer *layer,
                                    float x, float y);

/* Check if an element needs its own compositing layer. */
bool minirend_compositor_needs_layer(const MinirendComputedStyle *style);

#endif /* MINIREND_COMPOSITOR_H */

