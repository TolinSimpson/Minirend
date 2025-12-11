#include <stdio.h>

#include "minirend.h"
#include "quickjs.h"

/* Minimal DOM implementation sufficient for three.js and basic UIs.
 *
 * - window.innerWidth / innerHeight
 * - document.body
 * - document.createElement('canvas')
 * - document.getElementById / querySelector (single, simple)
 * - element.appendChild
 */

typedef struct DOMElement {
    JSValue js_obj;
} DOMElement;

/* For now we keep a very small registry of elements; a real implementation
 * would maintain a full tree and integrate tightly with Modest.
 */

static JSValue
js_document_createElement(JSContext *ctx, JSValueConst this_val,
                          int argc, JSValueConst *argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "tag name required");
    }
    const char *tag = JS_ToCString(ctx, argv[0]);
    if (!tag) return JS_EXCEPTION;

    JSValue elem = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, elem, "tagName",
                      JS_NewString(ctx, tag));

    /* Basic style and dataset placeholders. */
    JS_SetPropertyStr(ctx, elem, "style", JS_NewObject(ctx));

    /* canvas specific defaults */
    if (strcmp(tag, "canvas") == 0) {
        JS_SetPropertyStr(ctx, elem, "width", JS_NewInt32(ctx, 800));
        JS_SetPropertyStr(ctx, elem, "height", JS_NewInt32(ctx, 600));
    }

    JS_FreeCString(ctx, tag);
    return elem;
}

static JSValue
js_document_getElementById(JSContext *ctx, JSValueConst this_val,
                           int argc, JSValueConst *argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
    /* For now, return undefined – IDs are not yet tracked. */
    return JS_UNDEFINED;
}

static JSValue
js_document_querySelector(JSContext *ctx, JSValueConst this_val,
                          int argc, JSValueConst *argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
    /* Minimal stub; a richer CSS selector implementation would integrate
     * with Modest's DOM tree. */
    return JS_UNDEFINED;
}

static JSValue
js_element_appendChild(JSContext *ctx, JSValueConst this_val,
                       int argc, JSValueConst *argv) {
    (void)ctx;
    (void)argc;
    (void)argv;
    /* In a full DOM, we'd update the parent/child relationship. At this
     * stage we only need appendChild to be callable without error. */
    return JS_DupValue(ctx, this_val);
}

void
minirend_dom_init(JSContext *ctx, MinirendApp *app) {
    (void)app;

    JSValue global_obj = JS_GetGlobalObject(ctx);

    /* window === global object (for our purposes). */
    JS_SetPropertyStr(ctx, global_obj, "window",
                      JS_DupValue(ctx, global_obj));

    /* window.innerWidth / innerHeight placeholders; these can be updated
     * from the host when the SDL window is resized. */
    JS_SetPropertyStr(ctx, global_obj, "innerWidth",
                      JS_NewInt32(ctx, 1280));
    JS_SetPropertyStr(ctx, global_obj, "innerHeight",
                      JS_NewInt32(ctx, 720));

    /* document object */
    JSValue document = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, document, "createElement",
                      JS_NewCFunction(ctx, js_document_createElement,
                                      "createElement", 1));
    JS_SetPropertyStr(ctx, document, "getElementById",
                      JS_NewCFunction(ctx, js_document_getElementById,
                                      "getElementById", 1));
    JS_SetPropertyStr(ctx, document, "querySelector",
                      JS_NewCFunction(ctx, js_document_querySelector,
                                      "querySelector", 1));

    JSValue body = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, body, "appendChild",
                      JS_NewCFunction(ctx, js_element_appendChild,
                                      "appendChild", 1));
    JS_SetPropertyStr(ctx, document, "body", body);

    JS_SetPropertyStr(ctx, global_obj, "document", document);

    JS_FreeValue(ctx, global_obj);
}


