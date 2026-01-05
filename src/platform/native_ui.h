#ifndef MINIREND_PLATFORM_NATIVE_UI_H
#define MINIREND_PLATFORM_NATIVE_UI_H

#include <stdint.h>
#include <stdbool.h>

/* Platform-native UI abstraction (future).
 *
 * The "full-native UI" direction maps certain HTML elements to real OS widgets.
 * This header defines the boundary; implementations will live per-platform.
 *
 * For now this is only scaffolding so input/events can target stable node_ids.
 */

typedef struct MinirendNativeControl MinirendNativeControl;

typedef enum {
    MINIREND_NATIVE_CONTROL_UNKNOWN = 0,
    MINIREND_NATIVE_CONTROL_TEXT_INPUT,
    MINIREND_NATIVE_CONTROL_BUTTON,
    MINIREND_NATIVE_CONTROL_SELECT,
} MinirendNativeControlType;

/* Create/destroy a native control for the given node_id. */
MinirendNativeControl *minirend_native_ui_create(MinirendNativeControlType type, int32_t node_id);
void minirend_native_ui_destroy(MinirendNativeControl *ctrl);

/* Update geometry/visibility. Coords are CSS pixels in the minirend viewport. */
void minirend_native_ui_set_bounds(MinirendNativeControl *ctrl, float x, float y, float w, float h);
void minirend_native_ui_set_visible(MinirendNativeControl *ctrl, bool visible);

/* TODO: Route native control events back into the input/DOM pipeline. */

#endif /* MINIREND_PLATFORM_NATIVE_UI_H */


