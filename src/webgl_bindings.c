#include <stdio.h>
#include <stdlib.h>

/* OpenGL types - sokol_gfx handles the actual GL calls */
typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef double GLclampd;
typedef unsigned char GLboolean;
typedef void GLvoid;

#include "minrend.h"
#include "quickjs.h"

/* Very small subset of WebGL 1.0 mapped onto OpenGL.
 * This is enough to get basic three.js examples going and can be
 * extended over time.
 */

typedef struct WebGLContext {
    /* Placeholder – a real implementation would track GL state,
     * resource maps, etc. */
    int dummy;
} WebGLContext;

static JSClassID js_webgl_ctx_class_id;

static void
js_webgl_ctx_finalizer(JSRuntime *rt, JSValue val) {
    WebGLContext *ctx = JS_GetOpaque(val, js_webgl_ctx_class_id);
    if (ctx) {
        free(ctx);
    }
}

static JSClassDef js_webgl_ctx_class = {
    "WebGLRenderingContext",
    .finalizer = js_webgl_ctx_finalizer,
};

static JSValue
js_canvas_getContext(JSContext *ctx, JSValueConst this_val,
                     int argc, JSValueConst *argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "context id required");
    }
    const char *kind = JS_ToCString(ctx, argv[0]);
    if (!kind) return JS_EXCEPTION;

    JSValue result = JS_UNDEFINED;

    if (strcmp(kind, "webgl") == 0 || strcmp(kind, "experimental-webgl") == 0 ||
        strcmp(kind, "webgl2") == 0) {
        WebGLContext *wctx = (WebGLContext *)calloc(1, sizeof(WebGLContext));
        if (!wctx) {
            JS_FreeCString(ctx, kind);
            return JS_ThrowInternalError(ctx, "out of memory");
        }
        JSValue obj = JS_NewObjectClass(ctx, js_webgl_ctx_class_id);
        JS_SetOpaque(obj, wctx);
        result = obj;
    }

    JS_FreeCString(ctx, kind);
    return result;
}

void
minrend_webgl_register(JSContext *ctx, MinrendApp *app) {
    (void)app;

    JS_NewClassID(&js_webgl_ctx_class_id);
    JSRuntime *rt = JS_GetRuntime(ctx);
    JS_NewClass(rt, js_webgl_ctx_class_id, &js_webgl_ctx_class);

    /* Patch canvas prototype to add getContext. */
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JSValue document   = JS_GetPropertyStr(ctx, global_obj, "document");
    JSValue body       = JS_GetPropertyStr(ctx, document, "body");
    JSValue canvas     = JS_GetPropertyStr(ctx, body, "prototype_canvas");

    if (JS_IsUndefined(canvas)) {
        /* Create a prototype object for canvas‑like elements. */
        canvas = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, canvas, "getContext",
                          JS_NewCFunction(ctx, js_canvas_getContext,
                                          "getContext", 1));
        JS_SetPropertyStr(ctx, body, "prototype_canvas", JS_DupValue(ctx, canvas));
    }

    JS_FreeValue(ctx, canvas);
    JS_FreeValue(ctx, body);
    JS_FreeValue(ctx, document);
    JS_FreeValue(ctx, global_obj);
}


