#ifndef MINIREND_STYLE_RESOLVER_H
#define MINIREND_STYLE_RESOLVER_H

/*
 * Style Resolver - Computes final CSS styles for DOM elements.
 *
 * This module:
 * - Parses inline style="" attributes
 * - Parses <style> blocks and external stylesheets
 * - Resolves CSS cascade (specificity, source order)
 * - Resolves CSS inheritance
 * - Converts CSS units to pixels
 * - Outputs computed style values ready for layout
 */

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* Forward declarations */
typedef struct LexborDocument LexborDocument;
typedef struct lxb_dom_node lxb_dom_node_t;

/* ============================================================================
 * Computed Style Structure
 * ============================================================================
 * All values are in pixels or normalized floats after resolution.
 */

typedef struct {
    uint8_t r, g, b, a;
} MinirendColor;

typedef enum {
    MINIREND_DISPLAY_NONE = 0,
    MINIREND_DISPLAY_BLOCK,
    MINIREND_DISPLAY_INLINE,
    MINIREND_DISPLAY_INLINE_BLOCK,
    MINIREND_DISPLAY_FLEX,
    MINIREND_DISPLAY_INLINE_FLEX,
    MINIREND_DISPLAY_GRID,
    MINIREND_DISPLAY_INLINE_GRID,
} MinirendDisplay;

typedef enum {
    MINIREND_POSITION_STATIC = 0,
    MINIREND_POSITION_RELATIVE,
    MINIREND_POSITION_ABSOLUTE,
    MINIREND_POSITION_FIXED,
    MINIREND_POSITION_STICKY,
} MinirendPosition;

typedef enum {
    MINIREND_FLEX_DIR_ROW = 0,
    MINIREND_FLEX_DIR_ROW_REVERSE,
    MINIREND_FLEX_DIR_COLUMN,
    MINIREND_FLEX_DIR_COLUMN_REVERSE,
} MinirendFlexDirection;

typedef enum {
    MINIREND_FLEX_WRAP_NOWRAP = 0,
    MINIREND_FLEX_WRAP_WRAP,
    MINIREND_FLEX_WRAP_WRAP_REVERSE,
} MinirendFlexWrap;

typedef enum {
    MINIREND_JUSTIFY_FLEX_START = 0,
    MINIREND_JUSTIFY_FLEX_END,
    MINIREND_JUSTIFY_CENTER,
    MINIREND_JUSTIFY_SPACE_BETWEEN,
    MINIREND_JUSTIFY_SPACE_AROUND,
    MINIREND_JUSTIFY_SPACE_EVENLY,
} MinirendJustifyContent;

typedef enum {
    MINIREND_ALIGN_STRETCH = 0,
    MINIREND_ALIGN_FLEX_START,
    MINIREND_ALIGN_FLEX_END,
    MINIREND_ALIGN_CENTER,
    MINIREND_ALIGN_BASELINE,
} MinirendAlignItems;

typedef enum {
    MINIREND_TEXT_ALIGN_LEFT = 0,
    MINIREND_TEXT_ALIGN_RIGHT,
    MINIREND_TEXT_ALIGN_CENTER,
    MINIREND_TEXT_ALIGN_JUSTIFY,
} MinirendTextAlign;

typedef enum {
    MINIREND_SIZE_AUTO = 0,   /* auto / not specified */
    MINIREND_SIZE_PX,         /* absolute pixels */
    MINIREND_SIZE_PERCENT,    /* percentage of parent */
} MinirendSizeType;

typedef struct {
    MinirendSizeType type;
    float value;              /* px or percent (0-100) */
} MinirendSizeValue;

/* Full computed style for an element */
typedef struct {
    /* Box model (all in pixels after resolution) */
    MinirendSizeValue width;
    MinirendSizeValue height;
    MinirendSizeValue min_width;
    MinirendSizeValue min_height;
    MinirendSizeValue max_width;
    MinirendSizeValue max_height;

    float margin_top;
    float margin_right;
    float margin_bottom;
    float margin_left;

    float padding_top;
    float padding_right;
    float padding_bottom;
    float padding_left;

    float border_top_width;
    float border_right_width;
    float border_bottom_width;
    float border_left_width;

    /* Colors */
    MinirendColor color;              /* text color */
    MinirendColor background_color;
    MinirendColor border_top_color;
    MinirendColor border_right_color;
    MinirendColor border_bottom_color;
    MinirendColor border_left_color;

    /* Display & position */
    MinirendDisplay   display;
    MinirendPosition  position;
    float             top, right, bottom, left;  /* for positioned elements */
    int32_t           z_index;
    bool              z_index_auto;

    /* Flexbox */
    MinirendFlexDirection  flex_direction;
    MinirendFlexWrap       flex_wrap;
    MinirendJustifyContent justify_content;
    MinirendAlignItems     align_items;
    MinirendAlignItems     align_self;
    float                  flex_grow;
    float                  flex_shrink;
    MinirendSizeValue      flex_basis;

    /* Text / font */
    float              font_size;       /* in pixels */
    float              line_height;     /* in pixels, or 0 for normal */
    int                font_weight;     /* 100-900 */
    MinirendTextAlign  text_align;
    float              letter_spacing;  /* in pixels */

    /* Opacity and visibility */
    float opacity;                      /* 0.0 - 1.0 */
    bool  visible;                      /* visibility: visible/hidden */

    /* Transform (2D only for now) */
    bool  has_transform;
    float transform[6];  /* 2D affine: [a, b, c, d, tx, ty] */

} MinirendComputedStyle;

/* ============================================================================
 * Style Resolver Context
 * ============================================================================ */

typedef struct MinirendStyleResolver MinirendStyleResolver;

/* Create a style resolver for a given document.
 * viewport_width/height are used for resolving viewport units. */
MinirendStyleResolver *minirend_style_resolver_create(LexborDocument *doc,
                                                       float viewport_width,
                                                       float viewport_height);

/* Destroy the style resolver and free resources. */
void minirend_style_resolver_destroy(MinirendStyleResolver *resolver);

/* Update viewport dimensions (e.g., on window resize). */
void minirend_style_resolver_set_viewport(MinirendStyleResolver *resolver,
                                          float width, float height);

/* Parse and add a stylesheet (from <style> content or external CSS).
 * Returns true on success. */
bool minirend_style_resolver_add_stylesheet(MinirendStyleResolver *resolver,
                                            const char *css, size_t len);

/* Compute the final style for a DOM element.
 * parent_style may be NULL for root elements.
 * Result is written to out_style. */
void minirend_style_resolver_compute(MinirendStyleResolver *resolver,
                                     lxb_dom_node_t *element,
                                     const MinirendComputedStyle *parent_style,
                                     MinirendComputedStyle *out_style);

/* Get default (initial) style values. */
void minirend_style_get_initial(MinirendComputedStyle *out_style);

/* Helper: resolve a length value to pixels given context. */
float minirend_style_resolve_length(MinirendSizeValue val,
                                    float parent_size,
                                    float viewport_size);

#endif /* MINIREND_STYLE_RESOLVER_H */

