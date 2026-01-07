#include "style_resolver.h"
#include "lexbor_adapter.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* Lexbor headers */
#include <lexbor/html/html.h>
#include <lexbor/dom/dom.h>
#include <lexbor/css/css.h>
#include <lexbor/css/stylesheet.h>
#include <lexbor/css/declaration.h>
#include <lexbor/css/property.h>
#include <lexbor/css/value.h>
#include <lexbor/selectors/selectors.h>

/* ============================================================================
 * Internal Structures
 * ============================================================================ */

/* A single parsed stylesheet */
typedef struct StyleSheet {
    lxb_css_stylesheet_t *lxb_sheet;
    struct StyleSheet    *next;
} StyleSheet;

struct MinirendStyleResolver {
    LexborDocument  *doc;
    lxb_css_parser_t *css_parser;
    lxb_selectors_t  *selectors;
    lxb_css_memory_t *css_memory;
    
    StyleSheet      *stylesheets;  /* linked list of parsed stylesheets */
    
    float viewport_width;
    float viewport_height;
    float base_font_size;   /* default 16px */
};

/* ============================================================================
 * Color Helpers
 * ============================================================================ */

static MinirendColor color_transparent(void) {
    return (MinirendColor){ 0, 0, 0, 0 };
}

static MinirendColor color_black(void) {
    return (MinirendColor){ 0, 0, 0, 255 };
}

static MinirendColor color_white(void) {
    return (MinirendColor){ 255, 255, 255, 255 };
}

/* Convert Lexbor color to our format */
static MinirendColor convert_lxb_color(const lxb_css_value_color_t *lxb_color) {
    if (!lxb_color) return color_black();
    
    MinirendColor c = { 0, 0, 0, 255 };
    
    switch (lxb_color->type) {
        case LXB_CSS_VALUE_TRANSPARENT:
            return color_transparent();
            
        case LXB_CSS_VALUE_CURRENTCOLOR:
            /* Will be resolved to parent's color later */
            return color_black();
            
        case LXB_CSS_VALUE_HEX:
            c.r = lxb_color->u.hex.rgba.r;
            c.g = lxb_color->u.hex.rgba.g;
            c.b = lxb_color->u.hex.rgba.b;
            c.a = lxb_color->u.hex.rgba.a;
            return c;
            
        default:
            /* For RGB/HSL/etc, we'd need more complex conversion.
             * For now, approximate from the raw values if available. */
            if (lxb_color->type >= LXB_CSS_VALUE_ALICEBLUE &&
                lxb_color->type <= LXB_CSS_VALUE_YELLOWGREEN) {
                /* Named color - look up in a table would be ideal.
                 * For now return a placeholder. */
                return color_black();
            }
            break;
    }
    
    return c;
}

/* ============================================================================
 * Length Resolution
 * ============================================================================ */

static float resolve_length_value(const lxb_css_value_length_t *len,
                                  float parent_size,
                                  float viewport_w,
                                  float viewport_h,
                                  float base_font_size) {
    if (!len) return 0.0f;
    
    double val = len->num;
    
    switch (len->unit) {
        case LXB_CSS_UNIT_PX:
            return (float)val;
        case LXB_CSS_UNIT_EM:
            return (float)(val * base_font_size);
        case LXB_CSS_UNIT_REM:
            return (float)(val * 16.0);  /* root em, assume 16px base */
        case LXB_CSS_UNIT_VW:
            return (float)(val * viewport_w / 100.0);
        case LXB_CSS_UNIT_VH:
            return (float)(val * viewport_h / 100.0);
        case LXB_CSS_UNIT_VMIN:
            return (float)(val * fmin(viewport_w, viewport_h) / 100.0);
        case LXB_CSS_UNIT_VMAX:
            return (float)(val * fmax(viewport_w, viewport_h) / 100.0);
        case LXB_CSS_UNIT_PT:
            return (float)(val * 96.0 / 72.0);  /* 1pt = 1/72 inch, 96 DPI */
        case LXB_CSS_UNIT_CM:
            return (float)(val * 96.0 / 2.54);
        case LXB_CSS_UNIT_MM:
            return (float)(val * 96.0 / 25.4);
        case LXB_CSS_UNIT_IN:
            return (float)(val * 96.0);
        case LXB_CSS_UNIT_PC:
            return (float)(val * 96.0 / 6.0);  /* 1pc = 12pt = 1/6 inch */
        default:
            /* Assume px for unknown */
            return (float)val;
    }
}

static MinirendSizeValue resolve_length_percentage(
    const lxb_css_value_length_percentage_t *lp,
    float parent_size,
    float viewport_w,
    float viewport_h,
    float base_font_size)
{
    MinirendSizeValue result = { MINIREND_SIZE_AUTO, 0.0f };
    
    if (!lp) return result;
    
    switch (lp->type) {
        case LXB_CSS_VALUE__LENGTH:
            result.type = MINIREND_SIZE_PX;
            result.value = resolve_length_value(&lp->u.length, parent_size,
                                                viewport_w, viewport_h, base_font_size);
            break;
            
        case LXB_CSS_VALUE__PERCENTAGE:
            result.type = MINIREND_SIZE_PERCENT;
            result.value = (float)lp->u.percentage.num;
            break;
            
        case LXB_CSS_VALUE_AUTO:
            result.type = MINIREND_SIZE_AUTO;
            break;
            
        default:
            result.type = MINIREND_SIZE_AUTO;
            break;
    }
    
    return result;
}

float minirend_style_resolve_length(MinirendSizeValue val,
                                    float parent_size,
                                    float viewport_size) {
    switch (val.type) {
        case MINIREND_SIZE_PX:
            return val.value;
        case MINIREND_SIZE_PERCENT:
            return val.value * parent_size / 100.0f;
        case MINIREND_SIZE_AUTO:
        default:
            return 0.0f;
    }
}

/* ============================================================================
 * Initial Style Values
 * ============================================================================ */

void minirend_style_get_initial(MinirendComputedStyle *s) {
    memset(s, 0, sizeof(*s));
    
    /* Box model defaults */
    s->width.type = MINIREND_SIZE_AUTO;
    s->height.type = MINIREND_SIZE_AUTO;
    s->min_width.type = MINIREND_SIZE_AUTO;
    s->min_height.type = MINIREND_SIZE_AUTO;
    s->max_width.type = MINIREND_SIZE_AUTO;
    s->max_height.type = MINIREND_SIZE_AUTO;
    
    /* Colors */
    s->color = color_black();
    s->background_color = color_transparent();
    s->border_top_color = color_black();
    s->border_right_color = color_black();
    s->border_bottom_color = color_black();
    s->border_left_color = color_black();
    
    /* Display */
    s->display = MINIREND_DISPLAY_INLINE;  /* inline is default for most elements */
    s->position = MINIREND_POSITION_STATIC;
    s->z_index_auto = true;
    
    /* Flexbox defaults */
    s->flex_direction = MINIREND_FLEX_DIR_ROW;
    s->flex_wrap = MINIREND_FLEX_WRAP_NOWRAP;
    s->justify_content = MINIREND_JUSTIFY_FLEX_START;
    s->align_items = MINIREND_ALIGN_STRETCH;
    s->align_self = MINIREND_ALIGN_STRETCH;
    s->flex_grow = 0.0f;
    s->flex_shrink = 1.0f;
    s->flex_basis.type = MINIREND_SIZE_AUTO;
    
    /* Text */
    s->font_size = 16.0f;
    s->line_height = 0.0f;  /* normal */
    s->font_weight = 400;
    s->text_align = MINIREND_TEXT_ALIGN_LEFT;
    s->letter_spacing = 0.0f;
    
    /* Opacity */
    s->opacity = 1.0f;
    s->visible = true;
    
    /* Transform */
    s->has_transform = false;
    s->transform[0] = 1.0f; s->transform[1] = 0.0f;
    s->transform[2] = 0.0f; s->transform[3] = 1.0f;
    s->transform[4] = 0.0f; s->transform[5] = 0.0f;
}

/* ============================================================================
 * Apply Declaration to Style
 * ============================================================================ */

static void apply_declaration(MinirendStyleResolver *resolver,
                              const lxb_css_rule_declaration_t *decl,
                              MinirendComputedStyle *style) {
    if (!decl) return;
    
    float vw = resolver->viewport_width;
    float vh = resolver->viewport_height;
    float fs = style->font_size > 0 ? style->font_size : 16.0f;
    
    switch (decl->type) {
        /* Display */
        case LXB_CSS_PROPERTY_DISPLAY:
            if (decl->u.display) {
                lxb_css_display_type_t dt = decl->u.display->a;
                switch (dt) {
                    case LXB_CSS_DISPLAY_NONE:
                        style->display = MINIREND_DISPLAY_NONE;
                        break;
                    case LXB_CSS_DISPLAY_BLOCK:
                        style->display = MINIREND_DISPLAY_BLOCK;
                        break;
                    case LXB_CSS_DISPLAY_INLINE:
                        style->display = MINIREND_DISPLAY_INLINE;
                        break;
                    case LXB_CSS_DISPLAY_INLINE_BLOCK:
                        style->display = MINIREND_DISPLAY_INLINE_BLOCK;
                        break;
                    case LXB_CSS_DISPLAY_FLEX:
                        style->display = MINIREND_DISPLAY_FLEX;
                        break;
                    case LXB_CSS_DISPLAY_INLINE_FLEX:
                        style->display = MINIREND_DISPLAY_INLINE_FLEX;
                        break;
                    case LXB_CSS_DISPLAY_GRID:
                        style->display = MINIREND_DISPLAY_GRID;
                        break;
                    case LXB_CSS_DISPLAY_INLINE_GRID:
                        style->display = MINIREND_DISPLAY_INLINE_GRID;
                        break;
                    default:
                        break;
                }
            }
            break;
            
        /* Position */
        case LXB_CSS_PROPERTY_POSITION:
            if (decl->u.position) {
                switch (decl->u.position->type) {
                    case LXB_CSS_POSITION_STATIC:
                        style->position = MINIREND_POSITION_STATIC;
                        break;
                    case LXB_CSS_POSITION_RELATIVE:
                        style->position = MINIREND_POSITION_RELATIVE;
                        break;
                    case LXB_CSS_POSITION_ABSOLUTE:
                        style->position = MINIREND_POSITION_ABSOLUTE;
                        break;
                    case LXB_CSS_POSITION_FIXED:
                        style->position = MINIREND_POSITION_FIXED;
                        break;
                    case LXB_CSS_POSITION_STICKY:
                        style->position = MINIREND_POSITION_STICKY;
                        break;
                    default:
                        break;
                }
            }
            break;
            
        /* Width/Height */
        case LXB_CSS_PROPERTY_WIDTH:
            style->width = resolve_length_percentage(decl->u.width, 0, vw, vh, fs);
            break;
        case LXB_CSS_PROPERTY_HEIGHT:
            style->height = resolve_length_percentage(decl->u.height, 0, vw, vh, fs);
            break;
        case LXB_CSS_PROPERTY_MIN_WIDTH:
            style->min_width = resolve_length_percentage(decl->u.min_width, 0, vw, vh, fs);
            break;
        case LXB_CSS_PROPERTY_MIN_HEIGHT:
            style->min_height = resolve_length_percentage(decl->u.min_height, 0, vw, vh, fs);
            break;
        case LXB_CSS_PROPERTY_MAX_WIDTH:
            style->max_width = resolve_length_percentage(decl->u.max_width, 0, vw, vh, fs);
            break;
        case LXB_CSS_PROPERTY_MAX_HEIGHT:
            style->max_height = resolve_length_percentage(decl->u.max_height, 0, vw, vh, fs);
            break;
            
        /* Margin */
        case LXB_CSS_PROPERTY_MARGIN_TOP:
            if (decl->u.margin_top) {
                MinirendSizeValue v = resolve_length_percentage(decl->u.margin_top, 0, vw, vh, fs);
                if (v.type == MINIREND_SIZE_PX) style->margin_top = v.value;
            }
            break;
        case LXB_CSS_PROPERTY_MARGIN_RIGHT:
            if (decl->u.margin_right) {
                MinirendSizeValue v = resolve_length_percentage(decl->u.margin_right, 0, vw, vh, fs);
                if (v.type == MINIREND_SIZE_PX) style->margin_right = v.value;
            }
            break;
        case LXB_CSS_PROPERTY_MARGIN_BOTTOM:
            if (decl->u.margin_bottom) {
                MinirendSizeValue v = resolve_length_percentage(decl->u.margin_bottom, 0, vw, vh, fs);
                if (v.type == MINIREND_SIZE_PX) style->margin_bottom = v.value;
            }
            break;
        case LXB_CSS_PROPERTY_MARGIN_LEFT:
            if (decl->u.margin_left) {
                MinirendSizeValue v = resolve_length_percentage(decl->u.margin_left, 0, vw, vh, fs);
                if (v.type == MINIREND_SIZE_PX) style->margin_left = v.value;
            }
            break;
            
        /* Padding */
        case LXB_CSS_PROPERTY_PADDING_TOP:
            if (decl->u.padding_top) {
                MinirendSizeValue v = resolve_length_percentage(decl->u.padding_top, 0, vw, vh, fs);
                if (v.type == MINIREND_SIZE_PX) style->padding_top = v.value;
            }
            break;
        case LXB_CSS_PROPERTY_PADDING_RIGHT:
            if (decl->u.padding_right) {
                MinirendSizeValue v = resolve_length_percentage(decl->u.padding_right, 0, vw, vh, fs);
                if (v.type == MINIREND_SIZE_PX) style->padding_right = v.value;
            }
            break;
        case LXB_CSS_PROPERTY_PADDING_BOTTOM:
            if (decl->u.padding_bottom) {
                MinirendSizeValue v = resolve_length_percentage(decl->u.padding_bottom, 0, vw, vh, fs);
                if (v.type == MINIREND_SIZE_PX) style->padding_bottom = v.value;
            }
            break;
        case LXB_CSS_PROPERTY_PADDING_LEFT:
            if (decl->u.padding_left) {
                MinirendSizeValue v = resolve_length_percentage(decl->u.padding_left, 0, vw, vh, fs);
                if (v.type == MINIREND_SIZE_PX) style->padding_left = v.value;
            }
            break;
            
        /* Border widths */
        case LXB_CSS_PROPERTY_BORDER_TOP:
            if (decl->u.border_top) {
                style->border_top_width = resolve_length_value(
                    &decl->u.border_top->width.length, 0, vw, vh, fs);
                style->border_top_color = convert_lxb_color(&decl->u.border_top->color);
            }
            break;
        case LXB_CSS_PROPERTY_BORDER_RIGHT:
            if (decl->u.border_right) {
                style->border_right_width = resolve_length_value(
                    &decl->u.border_right->width.length, 0, vw, vh, fs);
                style->border_right_color = convert_lxb_color(&decl->u.border_right->color);
            }
            break;
        case LXB_CSS_PROPERTY_BORDER_BOTTOM:
            if (decl->u.border_bottom) {
                style->border_bottom_width = resolve_length_value(
                    &decl->u.border_bottom->width.length, 0, vw, vh, fs);
                style->border_bottom_color = convert_lxb_color(&decl->u.border_bottom->color);
            }
            break;
        case LXB_CSS_PROPERTY_BORDER_LEFT:
            if (decl->u.border_left) {
                style->border_left_width = resolve_length_value(
                    &decl->u.border_left->width.length, 0, vw, vh, fs);
                style->border_left_color = convert_lxb_color(&decl->u.border_left->color);
            }
            break;
            
        /* Colors */
        case LXB_CSS_PROPERTY_COLOR:
            style->color = convert_lxb_color(decl->u.color);
            break;
        case LXB_CSS_PROPERTY_BACKGROUND_COLOR:
            style->background_color = convert_lxb_color(decl->u.background_color);
            break;
            
        /* Opacity */
        case LXB_CSS_PROPERTY_OPACITY:
            if (decl->u.opacity) {
                if (decl->u.opacity->type == LXB_CSS_VALUE__NUMBER) {
                    style->opacity = (float)decl->u.opacity->u.number.num;
                    if (style->opacity < 0.0f) style->opacity = 0.0f;
                    if (style->opacity > 1.0f) style->opacity = 1.0f;
                } else if (decl->u.opacity->type == LXB_CSS_VALUE__PERCENTAGE) {
                    style->opacity = (float)(decl->u.opacity->u.percentage.num / 100.0);
                    if (style->opacity < 0.0f) style->opacity = 0.0f;
                    if (style->opacity > 1.0f) style->opacity = 1.0f;
                }
            }
            break;
            
        /* Z-index */
        case LXB_CSS_PROPERTY_Z_INDEX:
            if (decl->u.z_index) {
                if (decl->u.z_index->type == LXB_CSS_VALUE_AUTO) {
                    style->z_index_auto = true;
                } else {
                    style->z_index_auto = false;
                    style->z_index = (int32_t)decl->u.z_index->integer.num;
                }
            }
            break;
            
        /* Flexbox properties */
        case LXB_CSS_PROPERTY_FLEX_DIRECTION:
            if (decl->u.flex_direction) {
                switch (decl->u.flex_direction->type) {
                    case LXB_CSS_FLEX_DIRECTION_ROW:
                        style->flex_direction = MINIREND_FLEX_DIR_ROW;
                        break;
                    case LXB_CSS_FLEX_DIRECTION_ROW_REVERSE:
                        style->flex_direction = MINIREND_FLEX_DIR_ROW_REVERSE;
                        break;
                    case LXB_CSS_FLEX_DIRECTION_COLUMN:
                        style->flex_direction = MINIREND_FLEX_DIR_COLUMN;
                        break;
                    case LXB_CSS_FLEX_DIRECTION_COLUMN_REVERSE:
                        style->flex_direction = MINIREND_FLEX_DIR_COLUMN_REVERSE;
                        break;
                    default:
                        break;
                }
            }
            break;
            
        case LXB_CSS_PROPERTY_FLEX_WRAP:
            if (decl->u.flex_wrap) {
                switch (decl->u.flex_wrap->type) {
                    case LXB_CSS_FLEX_WRAP_NOWRAP:
                        style->flex_wrap = MINIREND_FLEX_WRAP_NOWRAP;
                        break;
                    case LXB_CSS_FLEX_WRAP_WRAP:
                        style->flex_wrap = MINIREND_FLEX_WRAP_WRAP;
                        break;
                    case LXB_CSS_FLEX_WRAP_WRAP_REVERSE:
                        style->flex_wrap = MINIREND_FLEX_WRAP_WRAP_REVERSE;
                        break;
                    default:
                        break;
                }
            }
            break;
            
        case LXB_CSS_PROPERTY_FLEX_GROW:
            if (decl->u.flex_grow && decl->u.flex_grow->type == LXB_CSS_VALUE__NUMBER) {
                style->flex_grow = (float)decl->u.flex_grow->number.num;
            }
            break;
            
        case LXB_CSS_PROPERTY_FLEX_SHRINK:
            if (decl->u.flex_shrink && decl->u.flex_shrink->type == LXB_CSS_VALUE__NUMBER) {
                style->flex_shrink = (float)decl->u.flex_shrink->number.num;
            }
            break;
            
        case LXB_CSS_PROPERTY_FLEX_BASIS:
            style->flex_basis = resolve_length_percentage(decl->u.flex_basis, 0, vw, vh, fs);
            break;
            
        /* Font size */
        case LXB_CSS_PROPERTY_FONT_SIZE:
            if (decl->u.font_size) {
                if (decl->u.font_size->type == LXB_CSS_VALUE__LENGTH) {
                    style->font_size = resolve_length_value(
                        &decl->u.font_size->length.u.length, 0, vw, vh, fs);
                } else if (decl->u.font_size->type == LXB_CSS_VALUE__PERCENTAGE) {
                    style->font_size = fs * (float)(decl->u.font_size->length.u.percentage.num / 100.0);
                }
            }
            break;
            
        /* Font weight */
        case LXB_CSS_PROPERTY_FONT_WEIGHT:
            if (decl->u.font_weight) {
                if (decl->u.font_weight->type == LXB_CSS_VALUE__NUMBER) {
                    style->font_weight = (int)decl->u.font_weight->number.num;
                } else if (decl->u.font_weight->type == LXB_CSS_VALUE_NORMAL) {
                    style->font_weight = 400;
                } else if (decl->u.font_weight->type == LXB_CSS_VALUE_BOLD) {
                    style->font_weight = 700;
                }
            }
            break;
            
        /* Line height */
        case LXB_CSS_PROPERTY_LINE_HEIGHT:
            if (decl->u.line_height) {
                if (decl->u.line_height->type == LXB_CSS_VALUE__NUMBER) {
                    style->line_height = style->font_size * (float)decl->u.line_height->u.number.num;
                } else if (decl->u.line_height->type == LXB_CSS_VALUE__LENGTH) {
                    style->line_height = resolve_length_value(
                        &decl->u.line_height->u.length, 0, vw, vh, fs);
                } else if (decl->u.line_height->type == LXB_CSS_VALUE__PERCENTAGE) {
                    style->line_height = style->font_size * 
                        (float)(decl->u.line_height->u.percentage.num / 100.0);
                }
            }
            break;
            
        /* Text align */
        case LXB_CSS_PROPERTY_TEXT_ALIGN:
            if (decl->u.text_align) {
                switch (decl->u.text_align->type) {
                    case LXB_CSS_TEXT_ALIGN_LEFT:
                    case LXB_CSS_TEXT_ALIGN_START:
                        style->text_align = MINIREND_TEXT_ALIGN_LEFT;
                        break;
                    case LXB_CSS_TEXT_ALIGN_RIGHT:
                    case LXB_CSS_TEXT_ALIGN_END:
                        style->text_align = MINIREND_TEXT_ALIGN_RIGHT;
                        break;
                    case LXB_CSS_TEXT_ALIGN_CENTER:
                        style->text_align = MINIREND_TEXT_ALIGN_CENTER;
                        break;
                    case LXB_CSS_TEXT_ALIGN_JUSTIFY:
                        style->text_align = MINIREND_TEXT_ALIGN_JUSTIFY;
                        break;
                    default:
                        break;
                }
            }
            break;
            
        /* Visibility */
        case LXB_CSS_PROPERTY_VISIBILITY:
            if (decl->u.visibility) {
                style->visible = (decl->u.visibility->type != LXB_CSS_VISIBILITY_HIDDEN &&
                                  decl->u.visibility->type != LXB_CSS_VISIBILITY_COLLAPSE);
            }
            break;
            
        default:
            /* Unsupported property, ignore */
            break;
    }
}

/* ============================================================================
 * Parse Inline Style
 * ============================================================================ */

static void apply_inline_style(MinirendStyleResolver *resolver,
                               const char *style_str,
                               MinirendComputedStyle *style) {
    if (!style_str || !*style_str) return;
    
    /* Parse inline declarations */
    lxb_css_rule_declaration_list_t *decl_list = lxb_css_declaration_list_parse(
        resolver->css_parser,
        resolver->css_memory,
        (const lxb_char_t *)style_str,
        strlen(style_str));
    
    if (!decl_list) return;
    
    /* Apply each declaration */
    lxb_css_rule_t *rule = decl_list->first;
    while (rule) {
        if (rule->type == LXB_CSS_RULE_DECLARATION) {
            apply_declaration(resolver, lxb_css_rule_declaration(rule), style);
        }
        rule = rule->next;
    }
    
    /* Cleanup - the memory belongs to css_memory, will be cleaned on resolver destroy */
}

/* ============================================================================
 * Stylesheet Matching
 * ============================================================================ */

typedef struct {
    MinirendStyleResolver *resolver;
    MinirendComputedStyle *style;
    lxb_dom_node_t        *element;
} MatchCtx;

static lxb_status_t match_callback(lxb_dom_node_t *node,
                                   lxb_css_selector_specificity_t spec,
                                   void *ctx) {
    (void)node;
    (void)spec;
    MatchCtx *mctx = ctx;
    
    /* Mark that we matched - specificity handling would go here for proper cascade */
    return LXB_STATUS_OK;
}

static void apply_stylesheet_rules(MinirendStyleResolver *resolver,
                                   lxb_css_stylesheet_t *sheet,
                                   lxb_dom_node_t *element,
                                   MinirendComputedStyle *style) {
    if (!sheet || !sheet->root) return;
    
    /* Walk the stylesheet rules */
    lxb_css_rule_t *rule = sheet->root;
    
    while (rule) {
        if (rule->type == LXB_CSS_RULE_LIST) {
            /* Process rule list */
            lxb_css_rule_list_t *list = lxb_css_rule_list(rule);
            lxb_css_rule_t *child = list->first;
            
            while (child) {
                if (child->type == LXB_CSS_RULE_STYLE) {
                    lxb_css_rule_style_t *style_rule = lxb_css_rule_style(child);
                    
                    if (style_rule->selector) {
                        /* Check if this selector matches our element */
                        MatchCtx mctx = { resolver, style, element };
                        
                        lxb_selectors_opt_set(resolver->selectors, LXB_SELECTORS_OPT_MATCH_ROOT);
                        lxb_status_t status = lxb_selectors_match_node(
                            resolver->selectors,
                            element,
                            style_rule->selector,
                            match_callback,
                            &mctx);
                        
                        if (status == LXB_STATUS_OK) {
                            /* Apply declarations from this rule */
                            if (style_rule->declarations) {
                                lxb_css_rule_t *decl_rule = style_rule->declarations->first;
                                while (decl_rule) {
                                    if (decl_rule->type == LXB_CSS_RULE_DECLARATION) {
                                        apply_declaration(resolver, 
                                            lxb_css_rule_declaration(decl_rule), style);
                                    }
                                    decl_rule = decl_rule->next;
                                }
                            }
                        }
                    }
                }
                child = child->next;
            }
        }
        rule = rule->next;
    }
}

/* ============================================================================
 * Public API
 * ============================================================================ */

MinirendStyleResolver *minirend_style_resolver_create(LexborDocument *doc,
                                                       float viewport_width,
                                                       float viewport_height) {
    MinirendStyleResolver *resolver = calloc(1, sizeof(MinirendStyleResolver));
    if (!resolver) return NULL;
    
    resolver->doc = doc;
    resolver->viewport_width = viewport_width;
    resolver->viewport_height = viewport_height;
    resolver->base_font_size = 16.0f;
    
    /* Get or create CSS parser */
    resolver->css_parser = minirend_lexbor_get_css_parser(doc);
    if (!resolver->css_parser) {
        resolver->css_parser = lxb_css_parser_create();
        if (!resolver->css_parser || 
            lxb_css_parser_init(resolver->css_parser, NULL) != LXB_STATUS_OK) {
            free(resolver);
            return NULL;
        }
    }
    
    /* Get or create selectors engine */
    resolver->selectors = minirend_lexbor_get_selectors(doc);
    if (!resolver->selectors) {
        resolver->selectors = lxb_selectors_create();
        if (!resolver->selectors ||
            lxb_selectors_init(resolver->selectors) != LXB_STATUS_OK) {
            free(resolver);
            return NULL;
        }
    }
    
    /* Create CSS memory for parsed stylesheets */
    resolver->css_memory = lxb_css_memory_create();
    if (!resolver->css_memory ||
        lxb_css_memory_init(resolver->css_memory, 4096) != LXB_STATUS_OK) {
        if (resolver->css_memory) {
            lxb_css_memory_destroy(resolver->css_memory, true);
        }
        free(resolver);
        return NULL;
    }
    
    return resolver;
}

void minirend_style_resolver_destroy(MinirendStyleResolver *resolver) {
    if (!resolver) return;
    
    /* Free stylesheets */
    StyleSheet *sheet = resolver->stylesheets;
    while (sheet) {
        StyleSheet *next = sheet->next;
        if (sheet->lxb_sheet) {
            lxb_css_stylesheet_destroy(sheet->lxb_sheet, false);
        }
        free(sheet);
        sheet = next;
    }
    
    /* Free CSS memory */
    if (resolver->css_memory) {
        lxb_css_memory_destroy(resolver->css_memory, true);
    }
    
    free(resolver);
}

void minirend_style_resolver_set_viewport(MinirendStyleResolver *resolver,
                                          float width, float height) {
    if (resolver) {
        resolver->viewport_width = width;
        resolver->viewport_height = height;
    }
}

bool minirend_style_resolver_add_stylesheet(MinirendStyleResolver *resolver,
                                            const char *css, size_t len) {
    if (!resolver || !css) return false;
    if (len == 0) len = strlen(css);
    
    /* Parse the stylesheet */
    lxb_css_stylesheet_t *lxb_sheet = lxb_css_stylesheet_parse(
        resolver->css_parser,
        (const lxb_char_t *)css,
        len);
    
    if (!lxb_sheet) return false;
    
    /* Add to our list */
    StyleSheet *sheet = calloc(1, sizeof(StyleSheet));
    if (!sheet) {
        lxb_css_stylesheet_destroy(lxb_sheet, true);
        return false;
    }
    
    sheet->lxb_sheet = lxb_sheet;
    sheet->next = resolver->stylesheets;
    resolver->stylesheets = sheet;
    
    return true;
}

void minirend_style_resolver_compute(MinirendStyleResolver *resolver,
                                     lxb_dom_node_t *element,
                                     const MinirendComputedStyle *parent_style,
                                     MinirendComputedStyle *out_style) {
    if (!resolver || !element || !out_style) return;
    
    /* Start with initial values */
    minirend_style_get_initial(out_style);
    
    /* Inherit inherited properties from parent */
    if (parent_style) {
        out_style->color = parent_style->color;
        out_style->font_size = parent_style->font_size;
        out_style->font_weight = parent_style->font_weight;
        out_style->line_height = parent_style->line_height;
        out_style->text_align = parent_style->text_align;
        out_style->letter_spacing = parent_style->letter_spacing;
        out_style->visible = parent_style->visible;
    }
    
    /* Apply user-agent default styles based on tag name */
    const char *tag = minirend_lexbor_get_tag_name(element);
    if (tag) {
        /* Block-level elements */
        if (strcasecmp(tag, "div") == 0 ||
            strcasecmp(tag, "p") == 0 ||
            strcasecmp(tag, "section") == 0 ||
            strcasecmp(tag, "article") == 0 ||
            strcasecmp(tag, "header") == 0 ||
            strcasecmp(tag, "footer") == 0 ||
            strcasecmp(tag, "main") == 0 ||
            strcasecmp(tag, "nav") == 0 ||
            strcasecmp(tag, "aside") == 0 ||
            strcasecmp(tag, "h1") == 0 ||
            strcasecmp(tag, "h2") == 0 ||
            strcasecmp(tag, "h3") == 0 ||
            strcasecmp(tag, "h4") == 0 ||
            strcasecmp(tag, "h5") == 0 ||
            strcasecmp(tag, "h6") == 0 ||
            strcasecmp(tag, "ul") == 0 ||
            strcasecmp(tag, "ol") == 0 ||
            strcasecmp(tag, "li") == 0 ||
            strcasecmp(tag, "form") == 0 ||
            strcasecmp(tag, "table") == 0 ||
            strcasecmp(tag, "body") == 0 ||
            strcasecmp(tag, "html") == 0) {
            out_style->display = MINIREND_DISPLAY_BLOCK;
        }
        
        /* Heading font sizes */
        if (strcasecmp(tag, "h1") == 0) {
            out_style->font_size = 32.0f;
            out_style->font_weight = 700;
        } else if (strcasecmp(tag, "h2") == 0) {
            out_style->font_size = 24.0f;
            out_style->font_weight = 700;
        } else if (strcasecmp(tag, "h3") == 0) {
            out_style->font_size = 18.72f;
            out_style->font_weight = 700;
        } else if (strcasecmp(tag, "h4") == 0) {
            out_style->font_size = 16.0f;
            out_style->font_weight = 700;
        } else if (strcasecmp(tag, "h5") == 0) {
            out_style->font_size = 13.28f;
            out_style->font_weight = 700;
        } else if (strcasecmp(tag, "h6") == 0) {
            out_style->font_size = 10.72f;
            out_style->font_weight = 700;
        }
        
        /* Bold/italic */
        if (strcasecmp(tag, "b") == 0 || strcasecmp(tag, "strong") == 0) {
            out_style->font_weight = 700;
        }
    }
    
    /* Apply rules from all stylesheets (in order) */
    StyleSheet *sheet = resolver->stylesheets;
    while (sheet) {
        apply_stylesheet_rules(resolver, sheet->lxb_sheet, element, out_style);
        sheet = sheet->next;
    }
    
    /* Apply inline style (highest specificity) */
    const char *inline_style = minirend_lexbor_get_inline_style(element);
    if (inline_style) {
        apply_inline_style(resolver, inline_style, out_style);
    }
}

