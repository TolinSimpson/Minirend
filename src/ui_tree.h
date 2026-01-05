#ifndef MINIREND_UI_TREE_H
#define MINIREND_UI_TREE_H

#include <stdint.h>
#include <stdbool.h>

/* A small native UI tree abstraction.
 *
 * Today this is a stub backing hit-testing for DOM events.
 * Later it will be fed by Modest layout and/or platform-native widgets.
 */

typedef struct MinirendRect {
    float x;
    float y;
    float w;
    float h;
} MinirendRect;

/* Node id conventions (kept in sync with dom_runtime/dom_bindings): */
enum {
    MINIREND_NODE_DOCUMENT = 1,
    MINIREND_NODE_BODY     = 2,
};

void    minirend_ui_tree_init(void);
void    minirend_ui_tree_shutdown(void);

void    minirend_ui_tree_set_viewport(int width_css_px, int height_css_px);
void    minirend_ui_tree_register_node(int32_t node_id);
void    minirend_ui_tree_set_bounds(int32_t node_id, MinirendRect r);
bool    minirend_ui_tree_get_bounds(int32_t node_id, MinirendRect *out_r);

/* Returns node_id hit at (x,y) in CSS pixels. Always returns at least BODY. */
int32_t minirend_ui_hit_test(float x_css_px, float y_css_px);

#endif /* MINIREND_UI_TREE_H */


