#include "minrend.h"
#include "quickjs.h"

/* Minimal Canvas 2D API stub. For three.js we mostly rely on WebGL;
 * this file simply ensures calls like getContext('2d') do not crash.
 */

static JSValue
js_canvas_getContext_2d(JSContext *ctx, JSValueConst this_val,
                         int argc, JSValueConst *argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
    /* Return a dummy 2D context object with no‑op methods. */
    JSValue obj = JS_NewObject(ctx);
    return obj;
}

void
minrend_canvas_register(JSContext *ctx, MinrendApp *app) {
    (void)app;

    JSValue global_obj = JS_GetGlobalObject(ctx);
    JSValue document   = JS_GetPropertyStr(ctx, global_obj, "document");
    JSValue body       = JS_GetPropertyStr(ctx, document, "body");
    JSValue canvas     = JS_GetPropertyStr(ctx, body, "prototype_canvas");

    if (JS_IsUndefined(canvas)) {
        canvas = JS_NewObject(ctx);
    }

    JS_SetPropertyStr(ctx, canvas, "getContext",
                      JS_NewCFunction(ctx, js_canvas_getContext_2d,
                                      "getContext", 1));
    JS_SetPropertyStr(ctx, body, "prototype_canvas", JS_DupValue(ctx, canvas));

    JS_FreeValue(ctx, canvas);
    JS_FreeValue(ctx, body);
    JS_FreeValue(ctx, document);
    JS_FreeValue(ctx, global_obj);
}


