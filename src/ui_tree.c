#include "ui_tree.h"

#include <stdlib.h>
#include <string.h>

typedef struct UiNode {
    int32_t     id;
    MinirendRect bounds;
    bool        has_bounds;
    bool        visible;
    bool        pointer_events;
    uint32_t    order; /* larger = on top */
} UiNode;

static UiNode   *g_nodes = NULL;
static int       g_nodes_len = 0;
static int       g_nodes_cap = 0;
static uint32_t  g_next_order = 1;

static int g_viewport_w = 0;
static int g_viewport_h = 0;

static UiNode *find_node(int32_t id) {
    for (int i = 0; i < g_nodes_len; i++) {
        if (g_nodes[i].id == id) return &g_nodes[i];
    }
    return NULL;
}

static UiNode *ensure_node(int32_t id) {
    UiNode *n = find_node(id);
    if (n) return n;

    if (g_nodes_len == g_nodes_cap) {
        int new_cap = g_nodes_cap ? (g_nodes_cap * 2) : 16;
        UiNode *nn = (UiNode *)realloc(g_nodes, (size_t)new_cap * sizeof(UiNode));
        if (!nn) return NULL;
        g_nodes = nn;
        g_nodes_cap = new_cap;
    }

    UiNode fresh;
    memset(&fresh, 0, sizeof(fresh));
    fresh.id = id;
    fresh.visible = true;
    fresh.pointer_events = true;
    fresh.order = g_next_order++;

    g_nodes[g_nodes_len++] = fresh;
    return &g_nodes[g_nodes_len - 1];
}

void minirend_ui_tree_init(void) {
    g_nodes_len = 0;
    g_next_order = 1;
    g_viewport_w = 0;
    g_viewport_h = 0;

    /* Ensure document/body exist. */
    minirend_ui_tree_register_node(MINIREND_NODE_DOCUMENT);
    minirend_ui_tree_register_node(MINIREND_NODE_BODY);
}

void minirend_ui_tree_shutdown(void) {
    free(g_nodes);
    g_nodes = NULL;
    g_nodes_len = 0;
    g_nodes_cap = 0;
}

void minirend_ui_tree_set_viewport(int width_css_px, int height_css_px) {
    g_viewport_w = width_css_px;
    g_viewport_h = height_css_px;

    /* Keep BODY covering the viewport by default. */
    UiNode *body = ensure_node(MINIREND_NODE_BODY);
    if (body) {
        body->bounds = (MinirendRect){ .x = 0, .y = 0, .w = (float)g_viewport_w, .h = (float)g_viewport_h };
        body->has_bounds = true;
    }
}

void minirend_ui_tree_register_node(int32_t node_id) {
    (void)ensure_node(node_id);
}

void minirend_ui_tree_set_bounds(int32_t node_id, MinirendRect r) {
    UiNode *n = ensure_node(node_id);
    if (!n) return;
    n->bounds = r;
    n->has_bounds = true;
    n->order = g_next_order++;
}

bool minirend_ui_tree_get_bounds(int32_t node_id, MinirendRect *out_r) {
    UiNode *n = find_node(node_id);
    if (!n || !n->has_bounds) return false;
    if (out_r) *out_r = n->bounds;
    return true;
}

static bool rect_contains(MinirendRect r, float x, float y) {
    return (x >= r.x) && (y >= r.y) && (x < (r.x + r.w)) && (y < (r.y + r.h));
}

int32_t minirend_ui_hit_test(float x_css_px, float y_css_px) {
    /* Z-order: pick topmost node that contains point. */
    uint32_t best_order = 0;
    int32_t best_id = MINIREND_NODE_BODY;

    for (int i = 0; i < g_nodes_len; i++) {
        UiNode *n = &g_nodes[i];
        if (!n->visible || !n->pointer_events || !n->has_bounds) continue;
        if (!rect_contains(n->bounds, x_css_px, y_css_px)) continue;
        if (n->order >= best_order) {
            best_order = n->order;
            best_id = n->id;
        }
    }

    return best_id;
}


