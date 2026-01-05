#include <stdio.h>
#include <string.h>

#include "minirend.h"
#include "quickjs.h"

#include "dom_runtime.h"
#include "ui_tree.h"
#include "modest_adapter.h"

static int32_t g_next_node_id = 3; /* 1=document, 2=body */

static JSValue js_document_createElement(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_document_elementFromPoint(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_element_appendChild(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

static JSValue make_element(JSContext *ctx, const char *tag, int32_t node_id) {
    JSValue elem = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, elem, "__nodeId", JS_NewInt32(ctx, node_id));
    JS_SetPropertyStr(ctx, elem, "tagName", JS_NewString(ctx, tag));

    /* DOM-ish tree links. */
    JS_SetPropertyStr(ctx, elem, "parentNode", JS_NULL);
    JS_SetPropertyStr(ctx, elem, "children", JS_NewArray(ctx));

    /* Style placeholder. */
    JS_SetPropertyStr(ctx, elem, "style", JS_NewObject(ctx));

    /* canvas specific defaults */
    if (strcmp(tag, "canvas") == 0) {
        JS_SetPropertyStr(ctx, elem, "width", JS_NewInt32(ctx, 800));
        JS_SetPropertyStr(ctx, elem, "height", JS_NewInt32(ctx, 600));
    }

    /* Register for nodeId -> object and hit-test. */
    minirend_dom_register_node(ctx, node_id, elem);
    minirend_ui_tree_register_node(node_id);
    return elem;
}

static JSValue js_document_createElement(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 1) return JS_ThrowTypeError(ctx, "tag name required");
    const char *tag = JS_ToCString(ctx, argv[0]);
    if (!tag) return JS_EXCEPTION;

    int32_t node_id = g_next_node_id++;
    JSValue elem = make_element(ctx, tag, node_id);

    JS_FreeCString(ctx, tag);
    return elem;
}

static JSValue js_document_elementFromPoint(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 2) return JS_UNDEFINED;
    double x = 0, y = 0;
    if (JS_ToFloat64(ctx, &x, argv[0]) != 0) return JS_UNDEFINED;
    if (JS_ToFloat64(ctx, &y, argv[1]) != 0) return JS_UNDEFINED;

    int32_t node_id = minirend_ui_hit_test((float)x, (float)y);
    JSValue elem = minirend_dom_lookup_node(ctx, node_id);
    if (JS_IsUndefined(elem)) return JS_UNDEFINED;
    return elem; /* already dup'd */
}

static JSValue js_element_appendChild(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 1 || !JS_IsObject(argv[0])) return JS_DupValue(ctx, this_val);
    JSValue child = JS_DupValue(ctx, argv[0]);

    /* child.parentNode = this */
    JS_SetPropertyStr(ctx, child, "parentNode", JS_DupValue(ctx, this_val));

    /* parent.children.push(child) */
    JSValue children = JS_GetPropertyStr(ctx, this_val, "children");
    if (!JS_IsArray(ctx, children)) {
        JS_FreeValue(ctx, children);
        children = JS_NewArray(ctx);
        JS_SetPropertyStr(ctx, this_val, "children", JS_DupValue(ctx, children));
    }
    uint32_t len = 0;
    JSValue lenv = JS_GetPropertyStr(ctx, children, "length");
    JS_ToUint32(ctx, &len, lenv);
    JS_FreeValue(ctx, lenv);
    JS_SetPropertyUint32(ctx, children, len, JS_DupValue(ctx, child));
    JS_FreeValue(ctx, children);

    /* TODO: when Modest is enabled, mark layout dirty and rebuild. */
    minirend_modest_adapter_rebuild_layout(ctx);

    return child; /* caller owns */
}

void minirend_dom_set_viewport(JSContext *ctx, int width, int height) {
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global_obj, "innerWidth", JS_NewInt32(ctx, width));
    JS_SetPropertyStr(ctx, global_obj, "innerHeight", JS_NewInt32(ctx, height));
    JS_FreeValue(ctx, global_obj);

    minirend_ui_tree_set_viewport(width, height);
}

void minirend_dom_init(JSContext *ctx, MinirendApp *app) {
    (void)app;

    /* Core subsystems used by input/hit-test. */
    minirend_ui_tree_init();
    minirend_modest_adapter_init();

    JSValue global_obj = JS_GetGlobalObject(ctx);

    /* window === global object (for our purposes). */
    JS_SetPropertyStr(ctx, global_obj, "window", JS_DupValue(ctx, global_obj));

    /* window.innerWidth / innerHeight placeholders; updated from host. */
    JS_SetPropertyStr(ctx, global_obj, "innerWidth", JS_NewInt32(ctx, 1280));
    JS_SetPropertyStr(ctx, global_obj, "innerHeight", JS_NewInt32(ctx, 720));

    /* document object */
    JSValue document = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, document, "createElement",
                      JS_NewCFunction(ctx, js_document_createElement, "createElement", 1));
    JS_SetPropertyStr(ctx, document, "elementFromPoint",
                      JS_NewCFunction(ctx, js_document_elementFromPoint, "elementFromPoint", 2));
    /* stubs for now */
    JS_SetPropertyStr(ctx, document, "getElementById", JS_UNDEFINED);
    JS_SetPropertyStr(ctx, document, "querySelector", JS_UNDEFINED);

    JS_SetPropertyStr(ctx, document, "__nodeId", JS_NewInt32(ctx, MINIREND_NODE_DOCUMENT));
    JS_SetPropertyStr(ctx, global_obj, "document", JS_DupValue(ctx, document));

    /* Install EventTarget + helpers now that document exists. */
    minirend_dom_runtime_init(ctx);

    /* Register document (node 1) after runtime init so it gets the prototype. */
    minirend_dom_register_node(ctx, MINIREND_NODE_DOCUMENT, document);

    /* body element (node 2) */
    JSValue body = make_element(ctx, "body", MINIREND_NODE_BODY);
    JS_SetPropertyStr(ctx, document, "body", JS_DupValue(ctx, body));
    JS_SetPropertyStr(ctx, document, "activeElement", JS_DupValue(ctx, body));

    /* Provide appendChild on body for basic tree building. */
    JS_SetPropertyStr(ctx, body, "appendChild",
                      JS_NewCFunction(ctx, js_element_appendChild, "appendChild", 1));
    JS_SetPropertyStr(ctx, document, "appendChild",
                      JS_NewCFunction(ctx, js_element_appendChild, "appendChild", 1));

    /* Keep UI tree viewport in sync with the default innerWidth/innerHeight. */
    minirend_ui_tree_set_viewport(1280, 720);

    JS_FreeValue(ctx, body);
    JS_FreeValue(ctx, document);
    JS_FreeValue(ctx, global_obj);
}


