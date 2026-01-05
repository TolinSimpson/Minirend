#include "dom_runtime.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct NodeSlot {
    int32_t id;
    JSValue obj; /* owned ref */
} NodeSlot;

static NodeSlot *g_slots = NULL;
static int       g_slots_len = 0;
static int       g_slots_cap = 0;

static JSValue g_elem_proto = JS_UNDEFINED; /* owned ref */
static JSValue g_doc_obj    = JS_UNDEFINED; /* weak-ish (dup) */

static void dump_exception(JSContext *ctx) {
    JSValue ex = JS_GetException(ctx);
    const char *s = JS_ToCString(ctx, ex);
    if (s) {
        fprintf(stderr, "JS exception: %s\n", s);
        JS_FreeCString(ctx, s);
    }
    JS_FreeValue(ctx, ex);
}

static NodeSlot *find_slot(int32_t id) {
    for (int i = 0; i < g_slots_len; i++) {
        if (g_slots[i].id == id) return &g_slots[i];
    }
    return NULL;
}

static NodeSlot *ensure_slot(int32_t id) {
    NodeSlot *s = find_slot(id);
    if (s) return s;

    if (g_slots_len == g_slots_cap) {
        int new_cap = g_slots_cap ? (g_slots_cap * 2) : 32;
        NodeSlot *nn = (NodeSlot *)realloc(g_slots, (size_t)new_cap * sizeof(NodeSlot));
        if (!nn) return NULL;
        g_slots = nn;
        g_slots_cap = new_cap;
    }
    g_slots[g_slots_len++] = (NodeSlot){ .id = id, .obj = JS_UNDEFINED };
    return &g_slots[g_slots_len - 1];
}

static const char *k_dom_bootstrap =
    "(function(){\n"
    "  function Event(type, init){\n"
    "    init = init || {};\n"
    "    this.type = String(type);\n"
    "    this.bubbles = !!init.bubbles;\n"
    "    this.cancelable = !!init.cancelable;\n"
    "    this.defaultPrevented = false;\n"
    "    this._stop = false;\n"
    "    this._stopImmediate = false;\n"
    "    this.eventPhase = 0;\n"
    "    this.target = null;\n"
    "    this.currentTarget = null;\n"
    "    this.timeStamp = init.timeStamp || 0;\n"
    "  }\n"
    "  Event.prototype.preventDefault = function(){ if (this.cancelable) this.defaultPrevented = true; };\n"
    "  Event.prototype.stopPropagation = function(){ this._stop = true; };\n"
    "  Event.prototype.stopImmediatePropagation = function(){ this._stopImmediate = true; this._stop = true; };\n"
    "\n"
    "  function makeProto(){\n"
    "    const proto = {};\n"
    "    proto.addEventListener = function(type, listener, options){\n"
    "      if (!listener) return;\n"
    "      type = String(type);\n"
    "      const capture = (typeof options === 'boolean') ? options : !!(options && options.capture);\n"
    "      const once = !!(options && options.once);\n"
    "      const passive = !!(options && options.passive);\n"
    "      if (!this.__listeners) this.__listeners = Object.create(null);\n"
    "      const arr = (this.__listeners[type] || (this.__listeners[type] = []));\n"
    "      arr.push({ listener, capture, once, passive });\n"
    "    };\n"
    "    proto.removeEventListener = function(type, listener, options){\n"
    "      type = String(type);\n"
    "      const capture = (typeof options === 'boolean') ? options : !!(options && options.capture);\n"
    "      const map = this.__listeners;\n"
    "      if (!map) return;\n"
    "      const arr = map[type];\n"
    "      if (!arr) return;\n"
    "      for (let i = arr.length - 1; i >= 0; i--){\n"
    "        const e = arr[i];\n"
    "        if (e.listener === listener && e.capture === capture) arr.splice(i, 1);\n"
    "      }\n"
    "    };\n"
    "    function getPath(target){\n"
    "      const path = [];\n"
    "      let n = target;\n"
    "      while (n){ path.push(n); n = n.parentNode || null; }\n"
    "      return path;\n"
    "    }\n"
    "    function invokeHandlers(node, event){\n"
    "      const prop = 'on' + event.type;\n"
    "      const h = node[prop];\n"
    "      if (typeof h === 'function'){\n"
    "        try { h.call(node, event); } catch (e) { (console && console.error) ? console.error(e) : 0; }\n"
    "      }\n"
    "    }\n"
    "    function invokeListeners(node, event, capture){\n"
    "      const map = node.__listeners;\n"
    "      if (!map) return;\n"
    "      const arr = map[event.type];\n"
    "      if (!arr) return;\n"
    "      for (let i = 0; i < arr.length; i++){\n"
    "        const e = arr[i];\n"
    "        if (!!e.capture !== !!capture) continue;\n"
    "        if (event._stopImmediate) break;\n"
    "        try { e.listener.call(node, event); } catch (err) { (console && console.error) ? console.error(err) : 0; }\n"
    "        if (e.once){ arr.splice(i, 1); i--; }\n"
    "      }\n"
    "    }\n"
    "    proto.dispatchEvent = function(event){\n"
    "      if (!event || !event.type) throw new TypeError('event required');\n"
    "      if (!(event instanceof Event)){\n"
    "        const wrapped = new Event(event.type, event);\n"
    "        for (const k in event){ wrapped[k] = event[k]; }\n"
    "        event = wrapped;\n"
    "      }\n"
    "      const target = this;\n"
    "      const path = getPath(target);\n"
    "      event.target = target;\n"
    "\n"
    "      /* CAPTURE */\n"
    "      if (event.bubbles){\n"
    "        for (let i = path.length - 1; i >= 1; i--){\n"
    "          if (event._stop) break;\n"
    "          const node = path[i];\n"
    "          event.eventPhase = 1;\n"
    "          event.currentTarget = node;\n"
    "          invokeHandlers(node, event);\n"
    "          invokeListeners(node, event, true);\n"
    "        }\n"
    "      }\n"
    "\n"
    "      /* AT_TARGET */\n"
    "      if (!event._stop){\n"
    "        event.eventPhase = 2;\n"
    "        event.currentTarget = target;\n"
    "        invokeHandlers(target, event);\n"
    "        invokeListeners(target, event, true);\n"
    "        invokeListeners(target, event, false);\n"
    "      }\n"
    "\n"
    "      /* BUBBLE */\n"
    "      if (event.bubbles){\n"
    "        for (let i = 1; i < path.length; i++){\n"
    "          if (event._stop) break;\n"
    "          const node = path[i];\n"
    "          event.eventPhase = 3;\n"
    "          event.currentTarget = node;\n"
    "          invokeHandlers(node, event);\n"
    "          invokeListeners(node, event, false);\n"
    "        }\n"
    "      }\n"
    "\n"
    "      event.eventPhase = 0;\n"
    "      event.currentTarget = null;\n"
    "      return !event.defaultPrevented;\n"
    "    };\n"
    "\n"
    "    proto.focus = function(){ if (typeof __minirendNativeFocus === 'function') __minirendNativeFocus(this.__nodeId|0); };\n"
    "    proto.blur = function(){ if (typeof __minirendNativeBlur === 'function') __minirendNativeBlur(this.__nodeId|0); };\n"
    "    proto.setPointerCapture = function(pointerId){ if (typeof __minirendNativeSetPointerCapture === 'function') __minirendNativeSetPointerCapture(this.__nodeId|0, pointerId|0); };\n"
    "    proto.releasePointerCapture = function(pointerId){ if (typeof __minirendNativeReleasePointerCapture === 'function') __minirendNativeReleasePointerCapture(this.__nodeId|0, pointerId|0); };\n"
    "    return proto;\n"
    "  }\n"
    "\n"
    "  globalThis.Event = Event;\n"
    "  globalThis.__MinirendElementProto = makeProto();\n"
    "})();\n";

void minirend_dom_runtime_init(JSContext *ctx) {
    JSValue global = JS_GetGlobalObject(ctx);

    /* Keep a ref to document for activeElement updates. */
    JSValue doc = JS_GetPropertyStr(ctx, global, "document");
    if (!JS_IsUndefined(g_doc_obj)) {
        JS_FreeValue(ctx, g_doc_obj);
        g_doc_obj = JS_UNDEFINED;
    }
    g_doc_obj = JS_DupValue(ctx, doc);

    JSValue val = JS_Eval(ctx, k_dom_bootstrap, strlen(k_dom_bootstrap), "<dom_bootstrap>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(val)) {
        dump_exception(ctx);
    }
    JS_FreeValue(ctx, val);

    /* Grab prototype object created by bootstrap. */
    JSValue proto = JS_GetPropertyStr(ctx, global, "__MinirendElementProto");
    if (!JS_IsUndefined(g_elem_proto)) {
        JS_FreeValue(ctx, g_elem_proto);
        g_elem_proto = JS_UNDEFINED;
    }
    g_elem_proto = JS_DupValue(ctx, proto);

    JS_FreeValue(ctx, proto);
    JS_FreeValue(ctx, doc);
    JS_FreeValue(ctx, global);
}

void minirend_dom_runtime_shutdown(JSContext *ctx) {
    if (ctx) {
        for (int i = 0; i < g_slots_len; i++) {
            if (!JS_IsUndefined(g_slots[i].obj)) {
                JS_FreeValue(ctx, g_slots[i].obj);
                g_slots[i].obj = JS_UNDEFINED;
            }
        }
        if (!JS_IsUndefined(g_elem_proto)) {
            JS_FreeValue(ctx, g_elem_proto);
            g_elem_proto = JS_UNDEFINED;
        }
        if (!JS_IsUndefined(g_doc_obj)) {
            JS_FreeValue(ctx, g_doc_obj);
            g_doc_obj = JS_UNDEFINED;
        }
    }
    free(g_slots);
    g_slots = NULL;
    g_slots_len = 0;
    g_slots_cap = 0;
}

void minirend_dom_register_node(JSContext *ctx, int32_t node_id, JSValue obj) {
    NodeSlot *s = ensure_slot(node_id);
    if (!s) return;
    if (!JS_IsUndefined(s->obj)) {
        JS_FreeValue(ctx, s->obj);
    }
    s->obj = JS_DupValue(ctx, obj);

    /* Apply our event proto if present. */
    if (!JS_IsUndefined(g_elem_proto)) {
        JS_SetPrototype(ctx, s->obj, JS_DupValue(ctx, g_elem_proto));
    }
}

JSValue minirend_dom_lookup_node(JSContext *ctx, int32_t node_id) {
    NodeSlot *s = find_slot(node_id);
    if (!s || JS_IsUndefined(s->obj)) return JS_UNDEFINED;
    return JS_DupValue(ctx, s->obj);
}

void minirend_dom_set_active_element(JSContext *ctx, int32_t node_id) {
    if (JS_IsUndefined(g_doc_obj)) return;
    JSValue elem = minirend_dom_lookup_node(ctx, node_id);
    if (JS_IsUndefined(elem)) return;
    JS_SetPropertyStr(ctx, g_doc_obj, "activeElement", elem);
    /* elem consumed by SetPropertyStr */
}

bool minirend_dom_dispatch_event(JSContext *ctx, int32_t target_node_id, JSValue eventObj) {
    JSValue target = minirend_dom_lookup_node(ctx, target_node_id);
    if (JS_IsUndefined(target)) {
        JS_FreeValue(ctx, eventObj);
        return true;
    }

    JSValue args[1] = { eventObj };
    JSAtom atom = JS_NewAtom(ctx, "dispatchEvent");
    JSValue ret = JS_Invoke(ctx, target, atom, 1, args);
    JS_FreeAtom(ctx, atom);
    JS_FreeValue(ctx, target);
    /* eventObj consumed by Invoke args; we still own it, so free now */
    JS_FreeValue(ctx, eventObj);

    if (JS_IsException(ret)) {
        dump_exception(ctx);
        JS_FreeValue(ctx, ret);
        return true;
    }
    int b = JS_ToBool(ctx, ret);
    JS_FreeValue(ctx, ret);
    return b != 0;
}


