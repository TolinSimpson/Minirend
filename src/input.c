#include "input.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ui_tree.h"
#include "dom_runtime.h"

typedef enum {
    INEV_NONE = 0,
    INEV_RESIZE,
    INEV_MOUSE_DOWN,
    INEV_MOUSE_UP,
    INEV_MOUSE_MOVE,
    INEV_MOUSE_SCROLL,
    INEV_KEY_DOWN,
    INEV_KEY_UP,
    INEV_CHAR,
} InputEventType;

typedef struct {
    InputEventType type;
    float x;
    float y;
    float scroll_x;
    float scroll_y;
    int   mouse_button;
    uint32_t modifiers;
    uint32_t key_code;
    uint32_t char_code;
    int window_w;
    int window_h;
    uint32_t time_ms;
} InputEvent;

enum { INPUT_QUEUE_CAP = 256 };
static InputEvent g_q[INPUT_QUEUE_CAP];
static int g_q_head = 0;
static int g_q_tail = 0;

static int g_viewport_w = 0;
static int g_viewport_h = 0;
static float g_dpi_scale = 1.0f;

/* Pointer state (single pointer for now) */
static int32_t g_active_node = MINIREND_NODE_BODY;
static int32_t g_capture_node = 0;
static uint32_t g_buttons_mask = 0; /* DOM buttons bitmask */

static int32_t g_last_down_target = MINIREND_NODE_BODY;
static uint32_t g_last_down_time = 0;
static float g_last_down_x = 0;
static float g_last_down_y = 0;

static uint32_t get_ticks_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static bool q_empty(void) { return g_q_head == g_q_tail; }
static bool q_full(void) { return ((g_q_tail + 1) % INPUT_QUEUE_CAP) == g_q_head; }

static void q_push(InputEvent ev) {
    if (q_full()) {
        /* Drop oldest. */
        g_q_head = (g_q_head + 1) % INPUT_QUEUE_CAP;
    }
    g_q[g_q_tail] = ev;
    g_q_tail = (g_q_tail + 1) % INPUT_QUEUE_CAP;
}

static bool q_pop(InputEvent *out) {
    if (q_empty()) return false;
    *out = g_q[g_q_head];
    g_q_head = (g_q_head + 1) % INPUT_QUEUE_CAP;
    return true;
}

static bool mod_shift(uint32_t m) { return (m & SAPP_MODIFIER_SHIFT) != 0; }
static bool mod_ctrl(uint32_t m)  { return (m & SAPP_MODIFIER_CTRL) != 0; }
static bool mod_alt(uint32_t m)   { return (m & SAPP_MODIFIER_ALT) != 0; }
static bool mod_super(uint32_t m) { return (m & SAPP_MODIFIER_SUPER) != 0; }

static int dom_button_from_sapp(int mouse_button) {
    switch (mouse_button) {
        case SAPP_MOUSEBUTTON_LEFT:   return 0;
        case SAPP_MOUSEBUTTON_MIDDLE: return 1;
        case SAPP_MOUSEBUTTON_RIGHT:  return 2;
        default: return 0;
    }
}

static uint32_t dom_buttons_bit_from_sapp(int mouse_button) {
    switch (mouse_button) {
        case SAPP_MOUSEBUTTON_LEFT:   return 1u; /* 1 */
        case SAPP_MOUSEBUTTON_RIGHT:  return 2u; /* 2 */
        case SAPP_MOUSEBUTTON_MIDDLE: return 4u; /* 4 */
        default: return 0u;
    }
}

static JSValue make_base_event(JSContext *ctx, const char *type, bool bubbles, bool cancelable, uint32_t time_ms) {
    JSValue ev = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, ev, "type", JS_NewString(ctx, type));
    JS_SetPropertyStr(ctx, ev, "bubbles", JS_NewBool(ctx, bubbles));
    JS_SetPropertyStr(ctx, ev, "cancelable", JS_NewBool(ctx, cancelable));
    JS_SetPropertyStr(ctx, ev, "timeStamp", JS_NewInt32(ctx, (int32_t)time_ms));
    return ev;
}

static JSValue make_pointer_like(JSContext *ctx, const char *type, float x_css, float y_css, int button, uint32_t buttons, uint32_t mods, uint32_t time_ms) {
    JSValue ev = make_base_event(ctx, type, true, true, time_ms);
    JS_SetPropertyStr(ctx, ev, "clientX", JS_NewFloat64(ctx, (double)x_css));
    JS_SetPropertyStr(ctx, ev, "clientY", JS_NewFloat64(ctx, (double)y_css));
    JS_SetPropertyStr(ctx, ev, "button", JS_NewInt32(ctx, button));
    JS_SetPropertyStr(ctx, ev, "buttons", JS_NewInt32(ctx, (int32_t)buttons));
    JS_SetPropertyStr(ctx, ev, "altKey", JS_NewBool(ctx, mod_alt(mods)));
    JS_SetPropertyStr(ctx, ev, "ctrlKey", JS_NewBool(ctx, mod_ctrl(mods)));
    JS_SetPropertyStr(ctx, ev, "shiftKey", JS_NewBool(ctx, mod_shift(mods)));
    JS_SetPropertyStr(ctx, ev, "metaKey", JS_NewBool(ctx, mod_super(mods)));
    /* PointerEvent-ish */
    JS_SetPropertyStr(ctx, ev, "pointerId", JS_NewInt32(ctx, 1));
    JS_SetPropertyStr(ctx, ev, "isPrimary", JS_NewBool(ctx, true));
    return ev;
}

static JSValue make_wheel(JSContext *ctx, float x_css, float y_css, float dx, float dy, uint32_t mods, uint32_t time_ms) {
    JSValue ev = make_base_event(ctx, "wheel", true, true, time_ms);
    JS_SetPropertyStr(ctx, ev, "clientX", JS_NewFloat64(ctx, (double)x_css));
    JS_SetPropertyStr(ctx, ev, "clientY", JS_NewFloat64(ctx, (double)y_css));
    JS_SetPropertyStr(ctx, ev, "deltaX", JS_NewFloat64(ctx, (double)dx));
    JS_SetPropertyStr(ctx, ev, "deltaY", JS_NewFloat64(ctx, (double)dy));
    JS_SetPropertyStr(ctx, ev, "altKey", JS_NewBool(ctx, mod_alt(mods)));
    JS_SetPropertyStr(ctx, ev, "ctrlKey", JS_NewBool(ctx, mod_ctrl(mods)));
    JS_SetPropertyStr(ctx, ev, "shiftKey", JS_NewBool(ctx, mod_shift(mods)));
    JS_SetPropertyStr(ctx, ev, "metaKey", JS_NewBool(ctx, mod_super(mods)));
    return ev;
}

static void utf8_from_codepoint(uint32_t cp, char out[8]) {
    /* Minimal UTF-8 encoder for BMP + supplementary planes. */
    memset(out, 0, 8);
    if (cp <= 0x7F) {
        out[0] = (char)cp;
    } else if (cp <= 0x7FF) {
        out[0] = (char)(0xC0 | ((cp >> 6) & 0x1F));
        out[1] = (char)(0x80 | (cp & 0x3F));
    } else if (cp <= 0xFFFF) {
        out[0] = (char)(0xE0 | ((cp >> 12) & 0x0F));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
    } else {
        out[0] = (char)(0xF0 | ((cp >> 18) & 0x07));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
    }
}

static void set_focus(JSContext *ctx, int32_t node_id) {
    if (node_id <= 0) node_id = MINIREND_NODE_BODY;
    if (g_active_node == node_id) return;

    int32_t prev = g_active_node;
    g_active_node = node_id;
    minirend_dom_set_active_element(ctx, g_active_node);

    /* blur prev, focus new */
    if (prev > 0) {
        JSValue blur = make_base_event(ctx, "blur", false, false, get_ticks_ms());
        minirend_dom_dispatch_event(ctx, prev, blur);
    }
    {
        JSValue focus = make_base_event(ctx, "focus", false, false, get_ticks_ms());
        minirend_dom_dispatch_event(ctx, g_active_node, focus);
    }
}

static JSValue js_native_focus(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 1) return JS_UNDEFINED;
    int32_t node_id = 0;
    if (JS_ToInt32(ctx, &node_id, argv[0]) != 0) return JS_UNDEFINED;
    set_focus(ctx, node_id);
    return JS_UNDEFINED;
}

static JSValue js_native_blur(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    (void)argv;
    /* blur active -> body */
    set_focus(ctx, MINIREND_NODE_BODY);
    return JS_UNDEFINED;
}

static JSValue js_native_set_pointer_capture(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 1) return JS_UNDEFINED;
    int32_t node_id = 0;
    if (JS_ToInt32(ctx, &node_id, argv[0]) != 0) return JS_UNDEFINED;
    g_capture_node = node_id;
    return JS_UNDEFINED;
}

static JSValue js_native_release_pointer_capture(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    (void)argv;
    g_capture_node = 0;
    return JS_UNDEFINED;
}

void minirend_input_init(JSContext *ctx) {
    g_q_head = g_q_tail = 0;
    g_viewport_w = 0;
    g_viewport_h = 0;
    g_dpi_scale = 1.0f;
    g_active_node = MINIREND_NODE_BODY;
    g_capture_node = 0;
    g_buttons_mask = 0;
    g_last_down_target = MINIREND_NODE_BODY;
    g_last_down_time = 0;
    g_last_down_x = g_last_down_y = 0;

    /* Install native hooks used by the JS bootstrap prototype. */
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "__minirendNativeFocus",
                      JS_NewCFunction(ctx, js_native_focus, "__minirendNativeFocus", 1));
    JS_SetPropertyStr(ctx, global, "__minirendNativeBlur",
                      JS_NewCFunction(ctx, js_native_blur, "__minirendNativeBlur", 1));
    JS_SetPropertyStr(ctx, global, "__minirendNativeSetPointerCapture",
                      JS_NewCFunction(ctx, js_native_set_pointer_capture, "__minirendNativeSetPointerCapture", 2));
    JS_SetPropertyStr(ctx, global, "__minirendNativeReleasePointerCapture",
                      JS_NewCFunction(ctx, js_native_release_pointer_capture, "__minirendNativeReleasePointerCapture", 2));
    JS_FreeValue(ctx, global);

    /* Default activeElement to body if present. */
    minirend_dom_set_active_element(ctx, MINIREND_NODE_BODY);
}

void minirend_input_shutdown(JSContext *ctx) {
    (void)ctx;
}

void minirend_input_push_sapp_event(const sapp_event *ev) {
    InputEvent ie;
    memset(&ie, 0, sizeof(ie));
    ie.time_ms = get_ticks_ms();
    ie.modifiers = ev->modifiers;

    switch (ev->type) {
        case SAPP_EVENTTYPE_RESIZED:
            ie.type = INEV_RESIZE;
            ie.window_w = ev->window_width;
            ie.window_h = ev->window_height;
            break;
        case SAPP_EVENTTYPE_MOUSE_DOWN:
            ie.type = INEV_MOUSE_DOWN;
            ie.x = ev->mouse_x;
            ie.y = ev->mouse_y;
            ie.mouse_button = ev->mouse_button;
            break;
        case SAPP_EVENTTYPE_MOUSE_UP:
            ie.type = INEV_MOUSE_UP;
            ie.x = ev->mouse_x;
            ie.y = ev->mouse_y;
            ie.mouse_button = ev->mouse_button;
            break;
        case SAPP_EVENTTYPE_MOUSE_MOVE:
            ie.type = INEV_MOUSE_MOVE;
            ie.x = ev->mouse_x;
            ie.y = ev->mouse_y;
            break;
        case SAPP_EVENTTYPE_MOUSE_SCROLL:
            ie.type = INEV_MOUSE_SCROLL;
            ie.x = ev->mouse_x;
            ie.y = ev->mouse_y;
            ie.scroll_x = ev->scroll_x;
            ie.scroll_y = ev->scroll_y;
            break;
        case SAPP_EVENTTYPE_KEY_DOWN:
            ie.type = INEV_KEY_DOWN;
            ie.key_code = (uint32_t)ev->key_code;
            break;
        case SAPP_EVENTTYPE_KEY_UP:
            ie.type = INEV_KEY_UP;
            ie.key_code = (uint32_t)ev->key_code;
            break;
        case SAPP_EVENTTYPE_CHAR:
            ie.type = INEV_CHAR;
            ie.char_code = ev->char_code;
            break;
        default:
            return;
    }

    q_push(ie);
}

static void update_viewport_from_sokol(void) {
    g_dpi_scale = sapp_dpi_scale();
    if (g_dpi_scale <= 0.0f) g_dpi_scale = 1.0f;
}

static int32_t hit_target(float x_css, float y_css) {
    if (g_capture_node > 0) return g_capture_node;
    return minirend_ui_hit_test(x_css, y_css);
}

static void dispatch_key(JSContext *ctx, const char *type, uint32_t key_code, uint32_t mods) {
    int32_t target = g_active_node > 0 ? g_active_node : MINIREND_NODE_BODY;
    JSValue ev = make_base_event(ctx, type, true, true, get_ticks_ms());
    JS_SetPropertyStr(ctx, ev, "keyCode", JS_NewInt32(ctx, (int32_t)key_code));
    JS_SetPropertyStr(ctx, ev, "altKey", JS_NewBool(ctx, mod_alt(mods)));
    JS_SetPropertyStr(ctx, ev, "ctrlKey", JS_NewBool(ctx, mod_ctrl(mods)));
    JS_SetPropertyStr(ctx, ev, "shiftKey", JS_NewBool(ctx, mod_shift(mods)));
    JS_SetPropertyStr(ctx, ev, "metaKey", JS_NewBool(ctx, mod_super(mods)));
    minirend_dom_dispatch_event(ctx, target, ev);
}

static void dispatch_text(JSContext *ctx, uint32_t codepoint) {
    int32_t target = g_active_node > 0 ? g_active_node : MINIREND_NODE_BODY;
    char buf[8];
    utf8_from_codepoint(codepoint, buf);
    JSValue ev = make_base_event(ctx, "textinput", true, true, get_ticks_ms());
    JS_SetPropertyStr(ctx, ev, "data", JS_NewString(ctx, buf));
    minirend_dom_dispatch_event(ctx, target, ev);
}

void minirend_input_tick(JSContext *ctx) {
    update_viewport_from_sokol();

    InputEvent ev;
    while (q_pop(&ev)) {
        if (ev.type == INEV_RESIZE) {
            /* Track viewport in CSS pixels (best-effort). */
            g_viewport_w = ev.window_w;
            g_viewport_h = ev.window_h;
            minirend_ui_tree_set_viewport(g_viewport_w, g_viewport_h);
            continue;
        }

        float x_css = ev.x / g_dpi_scale;
        float y_css = ev.y / g_dpi_scale;

        switch (ev.type) {
            case INEV_MOUSE_DOWN: {
                const int button = dom_button_from_sapp(ev.mouse_button);
                g_buttons_mask |= dom_buttons_bit_from_sapp(ev.mouse_button);

                int32_t target = hit_target(x_css, y_css);
                g_last_down_target = target;
                g_last_down_time = ev.time_ms;
                g_last_down_x = x_css;
                g_last_down_y = y_css;

                /* Default focus action: focus on pointerdown. */
                set_focus(ctx, target);

                JSValue pdown = make_pointer_like(ctx, "pointerdown", x_css, y_css, button, g_buttons_mask, ev.modifiers, ev.time_ms);
                minirend_dom_dispatch_event(ctx, target, pdown);
                JSValue mdown = make_pointer_like(ctx, "mousedown", x_css, y_css, button, g_buttons_mask, ev.modifiers, ev.time_ms);
                minirend_dom_dispatch_event(ctx, target, mdown);
                break;
            }
            case INEV_MOUSE_UP: {
                const int button = dom_button_from_sapp(ev.mouse_button);
                g_buttons_mask &= ~dom_buttons_bit_from_sapp(ev.mouse_button);

                int32_t target = hit_target(x_css, y_css);
                JSValue pup = make_pointer_like(ctx, "pointerup", x_css, y_css, button, g_buttons_mask, ev.modifiers, ev.time_ms);
                minirend_dom_dispatch_event(ctx, target, pup);
                JSValue mup = make_pointer_like(ctx, "mouseup", x_css, y_css, button, g_buttons_mask, ev.modifiers, ev.time_ms);
                minirend_dom_dispatch_event(ctx, target, mup);

                /* Click synthesis: same target, small movement, short time. */
                const float dx = x_css - g_last_down_x;
                const float dy = y_css - g_last_down_y;
                const float dist2 = dx*dx + dy*dy;
                const uint32_t dt = ev.time_ms - g_last_down_time;
                if (target == g_last_down_target && dist2 < (5.0f * 5.0f) && dt < 600) {
                    JSValue click = make_pointer_like(ctx, "click", x_css, y_css, button, g_buttons_mask, ev.modifiers, ev.time_ms);
                    minirend_dom_dispatch_event(ctx, target, click);
                    if (button == 2) {
                        JSValue ctxm = make_pointer_like(ctx, "contextmenu", x_css, y_css, button, g_buttons_mask, ev.modifiers, ev.time_ms);
                        minirend_dom_dispatch_event(ctx, target, ctxm);
                    }
                }
                break;
            }
            case INEV_MOUSE_MOVE: {
                int32_t target = hit_target(x_css, y_css);
                JSValue pmove = make_pointer_like(ctx, "pointermove", x_css, y_css, 0, g_buttons_mask, ev.modifiers, ev.time_ms);
                minirend_dom_dispatch_event(ctx, target, pmove);
                JSValue mmove = make_pointer_like(ctx, "mousemove", x_css, y_css, 0, g_buttons_mask, ev.modifiers, ev.time_ms);
                minirend_dom_dispatch_event(ctx, target, mmove);
                break;
            }
            case INEV_MOUSE_SCROLL: {
                int32_t target = hit_target(x_css, y_css);
                JSValue wheel = make_wheel(ctx, x_css, y_css, ev.scroll_x, ev.scroll_y, ev.modifiers, ev.time_ms);
                minirend_dom_dispatch_event(ctx, target, wheel);
                break;
            }
            case INEV_KEY_DOWN:
                dispatch_key(ctx, "keydown", ev.key_code, ev.modifiers);
                break;
            case INEV_KEY_UP:
                dispatch_key(ctx, "keyup", ev.key_code, ev.modifiers);
                break;
            case INEV_CHAR:
                if (ev.char_code != 0) dispatch_text(ctx, ev.char_code);
                break;
            default:
                break;
        }
    }
}


