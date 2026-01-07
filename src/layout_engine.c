/*
 * Layout Engine Implementation
 *
 * Uses clay.h for layout computation and outputs positioned render commands.
 */

#define CLAY_IMPLEMENTATION
#include <clay.h>

#include "layout_engine.h"
#include "lexbor_adapter.h"
#include "style_resolver.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Lexbor headers for DOM traversal */
#include <lexbor/html/html.h>
#include <lexbor/dom/dom.h>

/* ============================================================================
 * Constants
 * ============================================================================ */

#define MAX_LAYOUT_NODES 4096
#define CLAY_ARENA_SIZE  (1024 * 1024)  /* 1MB arena for clay */

/* ============================================================================
 * Layout Engine Structure
 * ============================================================================ */

struct MinirendLayoutEngine {
    float viewport_width;
    float viewport_height;
    
    /* Clay memory arena */
    void       *clay_memory;
    Clay_Arena  clay_arena;
    
    /* Output nodes */
    MinirendLayoutNode *nodes;
    int                 node_count;
    int                 node_capacity;
    
    /* Text measurement callback */
    MinirendMeasureTextFn measure_text_fn;
    void                 *measure_text_user_data;
    
    /* Current layout state (during compute) */
    MinirendStyleResolver *current_resolver;
    LexborDocument        *current_doc;
    int32_t                next_node_id;
};

/* ============================================================================
 * Clay Text Measurement Callback
 * ============================================================================ */

static MinirendLayoutEngine *g_current_engine = NULL;

static Clay_Dimensions clay_measure_text(Clay_StringSlice text, Clay_TextElementConfig *config, void *userData) {
    (void)userData;
    
    Clay_Dimensions dims = { .width = 0, .height = 16.0f };
    
    if (!g_current_engine || !g_current_engine->measure_text_fn) {
        /* Default: approximate 8px per character, 16px height */
        dims.width = (float)text.length * 8.0f;
        dims.height = config ? config->fontSize : 16.0f;
        return dims;
    }
    
    float w = 0, h = 0;
    g_current_engine->measure_text_fn(
        text.chars, text.length,
        config ? config->fontSize : 16.0f,
        config ? (int)config->fontWeight : 400,
        &w, &h,
        g_current_engine->measure_text_user_data);
    
    dims.width = w;
    dims.height = h;
    return dims;
}

/* ============================================================================
 * Create/Destroy
 * ============================================================================ */

MinirendLayoutEngine *minirend_layout_engine_create(float viewport_width,
                                                     float viewport_height) {
    MinirendLayoutEngine *engine = calloc(1, sizeof(MinirendLayoutEngine));
    if (!engine) return NULL;
    
    engine->viewport_width = viewport_width;
    engine->viewport_height = viewport_height;
    
    /* Allocate clay arena */
    engine->clay_memory = malloc(CLAY_ARENA_SIZE);
    if (!engine->clay_memory) {
        free(engine);
        return NULL;
    }
    
    engine->clay_arena = Clay_CreateArenaWithCapacityAndMemory(
        CLAY_ARENA_SIZE, engine->clay_memory);
    
    /* Initialize clay */
    Clay_Initialize(engine->clay_arena,
                    (Clay_Dimensions){ viewport_width, viewport_height },
                    (Clay_ErrorHandler){ NULL, NULL });
    
    Clay_SetMeasureTextFunction(clay_measure_text, NULL);
    
    /* Allocate output nodes */
    engine->node_capacity = MAX_LAYOUT_NODES;
    engine->nodes = calloc(engine->node_capacity, sizeof(MinirendLayoutNode));
    if (!engine->nodes) {
        free(engine->clay_memory);
        free(engine);
        return NULL;
    }
    
    return engine;
}

void minirend_layout_engine_destroy(MinirendLayoutEngine *engine) {
    if (!engine) return;
    
    free(engine->nodes);
    free(engine->clay_memory);
    free(engine);
}

void minirend_layout_engine_set_viewport(MinirendLayoutEngine *engine,
                                         float width, float height) {
    if (!engine) return;
    
    engine->viewport_width = width;
    engine->viewport_height = height;
    
    Clay_SetLayoutDimensions((Clay_Dimensions){ width, height });
}

void minirend_layout_engine_set_measure_text(MinirendLayoutEngine *engine,
                                             MinirendMeasureTextFn fn,
                                             void *user_data) {
    if (!engine) return;
    
    engine->measure_text_fn = fn;
    engine->measure_text_user_data = user_data;
}

/* ============================================================================
 * Add Output Node
 * ============================================================================ */

static MinirendLayoutNode *add_layout_node(MinirendLayoutEngine *engine) {
    if (engine->node_count >= engine->node_capacity) {
        /* Grow array */
        int new_cap = engine->node_capacity * 2;
        MinirendLayoutNode *new_nodes = realloc(engine->nodes,
            new_cap * sizeof(MinirendLayoutNode));
        if (!new_nodes) return NULL;
        
        engine->nodes = new_nodes;
        engine->node_capacity = new_cap;
    }
    
    MinirendLayoutNode *node = &engine->nodes[engine->node_count++];
    memset(node, 0, sizeof(*node));
    return node;
}

/* ============================================================================
 * Convert Clay Commands to Layout Nodes
 * ============================================================================ */

static void convert_clay_commands(MinirendLayoutEngine *engine,
                                  Clay_RenderCommandArray commands) {
    engine->node_count = 0;
    
    for (int32_t i = 0; i < commands.length; i++) {
        Clay_RenderCommand *cmd = Clay_RenderCommandArray_Get(&commands, i);
        if (!cmd) continue;
        
        MinirendLayoutNode *node = add_layout_node(engine);
        if (!node) break;
        
        /* Copy bounding box */
        node->x = cmd->boundingBox.x;
        node->y = cmd->boundingBox.y;
        node->width = cmd->boundingBox.width;
        node->height = cmd->boundingBox.height;
        node->clay_id = cmd->id;
        
        switch (cmd->commandType) {
            case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
                node->type = MINIREND_LAYOUT_BOX;
                Clay_RectangleRenderData *rect = &cmd->renderData.rectangle;
                node->background_color = (MinirendColor){
                    (uint8_t)rect->backgroundColor.r,
                    (uint8_t)rect->backgroundColor.g,
                    (uint8_t)rect->backgroundColor.b,
                    (uint8_t)rect->backgroundColor.a
                };
                node->corner_radius = rect->cornerRadius.topLeft;
                break;
            }
            
            case CLAY_RENDER_COMMAND_TYPE_TEXT: {
                node->type = MINIREND_LAYOUT_TEXT;
                Clay_TextRenderData *text = &cmd->renderData.text;
                node->text = text->stringContents.chars;
                node->text_len = text->stringContents.length;
                node->text_color = (MinirendColor){
                    (uint8_t)text->textColor.r,
                    (uint8_t)text->textColor.g,
                    (uint8_t)text->textColor.b,
                    (uint8_t)text->textColor.a
                };
                node->font_size = text->fontSize;
                node->font_weight = (int)text->fontWeight;
                break;
            }
            
            case CLAY_RENDER_COMMAND_TYPE_BORDER: {
                node->type = MINIREND_LAYOUT_BORDER;
                Clay_BorderRenderData *border = &cmd->renderData.border;
                node->border_color = (MinirendColor){
                    (uint8_t)border->color.r,
                    (uint8_t)border->color.g,
                    (uint8_t)border->color.b,
                    (uint8_t)border->color.a
                };
                node->border_top_width = border->width.top;
                node->border_right_width = border->width.right;
                node->border_bottom_width = border->width.bottom;
                node->border_left_width = border->width.left;
                node->corner_radius = border->cornerRadius.topLeft;
                break;
            }
            
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START:
                node->type = MINIREND_LAYOUT_SCISSOR_START;
                break;
                
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
                node->type = MINIREND_LAYOUT_SCISSOR_END;
                break;
                
            default:
                node->type = MINIREND_LAYOUT_NONE;
                break;
        }
    }
}

/* ============================================================================
 * Map Style to Clay Configuration
 * ============================================================================ */

static Clay_LayoutDirection get_clay_direction(MinirendFlexDirection dir) {
    switch (dir) {
        case MINIREND_FLEX_DIR_COLUMN:
        case MINIREND_FLEX_DIR_COLUMN_REVERSE:
            return CLAY_TOP_TO_BOTTOM;
        default:
            return CLAY_LEFT_TO_RIGHT;
    }
}

static Clay_LayoutAlignmentX get_clay_justify(MinirendJustifyContent jc) {
    switch (jc) {
        case MINIREND_JUSTIFY_FLEX_END:
            return CLAY_ALIGN_X_RIGHT;
        case MINIREND_JUSTIFY_CENTER:
        case MINIREND_JUSTIFY_SPACE_AROUND:
        case MINIREND_JUSTIFY_SPACE_EVENLY:
            return CLAY_ALIGN_X_CENTER;
        default:
            return CLAY_ALIGN_X_LEFT;
    }
}

static Clay_LayoutAlignmentY get_clay_align(MinirendAlignItems ai) {
    switch (ai) {
        case MINIREND_ALIGN_FLEX_END:
            return CLAY_ALIGN_Y_BOTTOM;
        case MINIREND_ALIGN_CENTER:
            return CLAY_ALIGN_Y_CENTER;
        default:
            return CLAY_ALIGN_Y_TOP;
    }
}

static Clay_SizingAxis get_clay_sizing(MinirendSizeValue val, float parent_size) {
    switch (val.type) {
        case MINIREND_SIZE_PX:
            return (Clay_SizingAxis){
                .type = CLAY__SIZING_TYPE_FIXED,
                .size.minMax = { val.value, val.value }
            };
        case MINIREND_SIZE_PERCENT:
            return (Clay_SizingAxis){
                .type = CLAY__SIZING_TYPE_PERCENT,
                .size.percent = val.value / 100.0f
            };
        default:
            /* Auto -> fit content */
            return (Clay_SizingAxis){
                .type = CLAY__SIZING_TYPE_FIT,
                .size.minMax = { 0, CLAY__MAXFLOAT }
            };
    }
}

/* ============================================================================
 * DOM Tree Walking
 * ============================================================================ */

static void process_dom_node(MinirendLayoutEngine *engine,
                             lxb_dom_node_t *node,
                             const MinirendComputedStyle *parent_style,
                             int depth);

static void process_element(MinirendLayoutEngine *engine,
                            lxb_dom_node_t *element,
                            const MinirendComputedStyle *parent_style,
                            int depth) {
    /* Compute style for this element */
    MinirendComputedStyle style;
    minirend_style_resolver_compute(engine->current_resolver, element,
                                    parent_style, &style);
    
    /* Skip hidden elements */
    if (style.display == MINIREND_DISPLAY_NONE || !style.visible) {
        return;
    }
    
    /* Create unique ID for this element */
    uint32_t elem_id = engine->next_node_id++;
    
    /* Build clay element configuration */
    Clay_Color bg_color = {
        (float)style.background_color.r,
        (float)style.background_color.g,
        (float)style.background_color.b,
        (float)style.background_color.a
    };
    
    Clay_LayoutConfig layout_config = {
        .sizing = {
            .width = get_clay_sizing(style.width, engine->viewport_width),
            .height = get_clay_sizing(style.height, engine->viewport_height),
        },
        .padding = {
            .left = (uint16_t)style.padding_left,
            .right = (uint16_t)style.padding_right,
            .top = (uint16_t)style.padding_top,
            .bottom = (uint16_t)style.padding_bottom,
        },
        .childGap = 0,
        .layoutDirection = get_clay_direction(style.flex_direction),
        .childAlignment = {
            .x = get_clay_justify(style.justify_content),
            .y = get_clay_align(style.align_items),
        },
    };
    
    /* Open clay element */
    Clay__OpenElementWithId((Clay_ElementId){ .id = elem_id });
    Clay__ConfigureOpenElement((Clay_ElementDeclaration){
        .layout = layout_config,
        .backgroundColor = bg_color,
    });
    
    /* Process children */
    lxb_dom_node_t *child = lxb_dom_node_first_child(element);
    while (child) {
        process_dom_node(engine, child, &style, depth + 1);
        child = lxb_dom_node_next(child);
    }
    
    /* Close clay element */
    Clay__CloseElement();
}

static void process_text_node(MinirendLayoutEngine *engine,
                              lxb_dom_node_t *text_node,
                              const MinirendComputedStyle *parent_style) {
    if (!parent_style) return;
    
    /* Get text content */
    size_t text_len = 0;
    lxb_char_t *text = lxb_dom_node_text_content(text_node, &text_len);
    if (!text || text_len == 0) return;
    
    /* Skip whitespace-only text */
    bool all_whitespace = true;
    for (size_t i = 0; i < text_len && all_whitespace; i++) {
        if (text[i] != ' ' && text[i] != '\t' && 
            text[i] != '\n' && text[i] != '\r') {
            all_whitespace = false;
        }
    }
    if (all_whitespace) return;
    
    /* Create text element in clay */
    Clay_String clay_text = {
        .isStaticallyAllocated = false,
        .length = (int32_t)text_len,
        .chars = (const char *)text,
    };
    
    Clay_TextElementConfig text_config = {
        .textColor = {
            (float)parent_style->color.r,
            (float)parent_style->color.g,
            (float)parent_style->color.b,
            (float)parent_style->color.a,
        },
        .fontSize = (uint16_t)parent_style->font_size,
        .fontWeight = (uint16_t)parent_style->font_weight,
        .lineHeight = (uint16_t)parent_style->line_height,
    };
    
    Clay__OpenTextElement(clay_text, Clay__StoreTextElementConfig(text_config));
}

static void process_dom_node(MinirendLayoutEngine *engine,
                             lxb_dom_node_t *node,
                             const MinirendComputedStyle *parent_style,
                             int depth) {
    if (!node) return;
    
    /* Prevent infinite recursion */
    if (depth > 100) return;
    
    lxb_dom_node_type_t type = lxb_dom_node_type(node);
    
    switch (type) {
        case LXB_DOM_NODE_TYPE_ELEMENT:
            process_element(engine, node, parent_style, depth);
            break;
            
        case LXB_DOM_NODE_TYPE_TEXT:
            process_text_node(engine, node, parent_style);
            break;
            
        default:
            /* Skip other node types (comments, etc.) */
            break;
    }
}

/* ============================================================================
 * Public API
 * ============================================================================ */

int minirend_layout_engine_compute(MinirendLayoutEngine *engine,
                                   LexborDocument *doc,
                                   MinirendStyleResolver *style_resolver) {
    if (!engine || !doc || !style_resolver) return 0;
    
    /* Set up state for DOM walking */
    engine->current_resolver = style_resolver;
    engine->current_doc = doc;
    engine->next_node_id = 1;
    
    /* Set global for text measurement */
    g_current_engine = engine;
    
    /* Begin clay layout */
    Clay_BeginLayout();
    
    /* Get body element and process */
    lxb_dom_node_t *body = minirend_lexbor_get_body(doc);
    if (body) {
        /* Root element with viewport sizing */
        Clay__OpenElementWithId((Clay_ElementId){ .id = 0 });
        Clay__ConfigureOpenElement((Clay_ElementDeclaration){
            .layout = {
                .sizing = {
                    .width = CLAY_SIZING_FIXED(engine->viewport_width),
                    .height = CLAY_SIZING_FIXED(engine->viewport_height),
                },
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
            },
        });
        
        /* Process body's children */
        lxb_dom_node_t *child = lxb_dom_node_first_child(body);
        while (child) {
            process_dom_node(engine, child, NULL, 0);
            child = lxb_dom_node_next(child);
        }
        
        Clay__CloseElement();
    }
    
    /* End layout and get render commands */
    Clay_RenderCommandArray commands = Clay_EndLayout();
    
    /* Convert to our layout nodes */
    convert_clay_commands(engine, commands);
    
    /* Clear state */
    engine->current_resolver = NULL;
    engine->current_doc = NULL;
    g_current_engine = NULL;
    
    return engine->node_count;
}

const MinirendLayoutNode *minirend_layout_get_nodes(MinirendLayoutEngine *engine,
                                                    int *out_count) {
    if (!engine) {
        if (out_count) *out_count = 0;
        return NULL;
    }
    
    if (out_count) *out_count = engine->node_count;
    return engine->nodes;
}

bool minirend_layout_node_contains(const MinirendLayoutNode *node, float x, float y) {
    if (!node) return false;
    
    return (x >= node->x && x < node->x + node->width &&
            y >= node->y && y < node->y + node->height);
}

