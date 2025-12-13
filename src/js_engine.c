#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "minirend.h"

/* Cross-platform timer using clock_gettime (works with Cosmopolitan) */
static uint32_t get_ticks_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

#include "quickjs.h"

/* Helper to read a file from either filesystem or embedded ZIP.
 * Cosmopolitan's fopen() automatically handles ZIP files appended to the executable.
 * Paths in ZIP should be relative to the ZIP root (e.g., "index.html" not "app/index.html").
 */
static char *
read_file(const char *path, size_t *out_size) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        /* If path includes "app/", try without it for ZIP access */
        if (strncmp(path, "app/", 4) == 0) {
            fp = fopen(path + 4, "rb");
        }
        if (!fp) return NULL;
    }
    
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long len = ftell(fp);
    if (len < 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)len, fp);
    fclose(fp);
    buf[n] = '\0';
    if (out_size) *out_size = n;
    return buf;
}

JSRuntime *
minirend_js_init(void) {
    JSRuntime *rt = JS_NewRuntime();
    if (!rt) {
        fprintf(stderr, "QuickJS: failed to create runtime\n");
    }
    return rt;
}

JSContext *
minirend_js_create_context(JSRuntime *rt) {
    JSContext *ctx = JS_NewContext(rt);
    if (!ctx) {
        fprintf(stderr, "QuickJS: failed to create context\n");
        return NULL;
    }
    return ctx;
}

void
minirend_js_register_bindings(JSContext *ctx, MinirendApp *app) {
    (void)ctx;
    (void)app;
    /* High-level hook – currently unused; per‑subsystem registration
     * happens via the dedicated functions declared in minirend.h.
     */
}

void
minirend_js_dispose(JSRuntime *rt, JSContext *ctx) {
    if (ctx) JS_FreeContext(ctx);
    if (rt) JS_FreeRuntime(rt);
}

static void
dump_exception(JSContext *ctx) {
    JSValue exception_val = JS_GetException(ctx);
    const char *str = JS_ToCString(ctx, exception_val);
    if (str) {
        fprintf(stderr, "JS exception: %s\n", str);
        JS_FreeCString(ctx, str);
    }
    JS_FreeValue(ctx, exception_val);
}

/* console.log / console.error bindings
 * ------------------------------------
 * Simple implementation that prints to stderr so developers can see
 * output from JavaScript code.
 */

static JSValue
js_console_log(JSContext *ctx, JSValueConst this_val,
               int argc, JSValueConst *argv) {
    (void)this_val;
    for (int i = 0; i < argc; ++i) {
        const char *str = JS_ToCString(ctx, argv[i]);
        if (str) {
            if (i > 0) {
                fputc(' ', stderr);
            }
            fputs(str, stderr);
            JS_FreeCString(ctx, str);
        }
    }
    fputc('\n', stderr);
    return JS_UNDEFINED;
}

void
minirend_register_console(JSContext *ctx) {
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JSValue console    = JS_NewObject(ctx);

    JSValue log_fn = JS_NewCFunction(ctx, js_console_log, "log", 1);

    JS_SetPropertyStr(ctx, console, "log",   JS_DupValue(ctx, log_fn));
    JS_SetPropertyStr(ctx, console, "info",  JS_DupValue(ctx, log_fn));
    JS_SetPropertyStr(ctx, console, "warn",  JS_DupValue(ctx, log_fn));
    JS_SetPropertyStr(ctx, console, "error", log_fn);

    JS_SetPropertyStr(ctx, global_obj, "console", console);

    JS_FreeValue(ctx, global_obj);
}

int
minirend_js_eval_file(JSContext *ctx, const char *path) {
    size_t size = 0;
    char *code = read_file(path, &size);
    if (!code) {
        fprintf(stderr, "Failed to read JS file: %s\n", path);
        return -1;
    }

    JSValue val = JS_Eval(ctx, code, size, path,
                          JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_STRICT);
    free(code);

    if (JS_IsException(val)) {
        dump_exception(ctx);
        JS_FreeValue(ctx, val);
        return -1;
    }

    JS_FreeValue(ctx, val);
    return 0;
}

/* requestAnimationFrame implementation
 * ------------------------------------
 * We expose window.requestAnimationFrame(cb) which schedules `cb`
 * to be called once per frame from the sokol frame callback.
 *
 * Callbacks are one-shot: they are invoked once and then freed.
 * To create an animation loop, the callback must call requestAnimationFrame again.
 */

typedef struct RAFCallback {
    int32_t   id;
    JSValue   func;
    struct RAFCallback *next;
} RAFCallback;

static RAFCallback *g_raf_head = NULL;
static int32_t      g_raf_next_id = 1;

static JSValue
js_requestAnimationFrame(JSContext *ctx, JSValueConst this_val,
                         int argc, JSValueConst *argv) {
    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "callback required");
    }
    RAFCallback *cb = (RAFCallback *)malloc(sizeof(RAFCallback));
    if (!cb) {
        return JS_ThrowInternalError(ctx, "out of memory");
    }
    cb->id   = g_raf_next_id++;
    cb->func = JS_DupValue(ctx, argv[0]);
    cb->next = g_raf_head;
    g_raf_head = cb;
    return JS_NewInt32(ctx, cb->id);
}

static JSValue
js_cancelAnimationFrame(JSContext *ctx, JSValueConst this_val,
                        int argc, JSValueConst *argv) {
    if (argc < 1) return JS_UNDEFINED;
    int32_t id = 0;
    if (JS_ToInt32(ctx, &id, argv[0]) != 0) {
        return JS_ThrowTypeError(ctx, "invalid id");
    }
    RAFCallback **pp = &g_raf_head;
    while (*pp) {
        if ((*pp)->id == id) {
            RAFCallback *dead = *pp;
            *pp = dead->next;
            JS_FreeValue(ctx, dead->func);
            free(dead);
            break;
        }
        pp = &(*pp)->next;
    }
    return JS_UNDEFINED;
}

/* Called once per frame from the host main loop.
 * RAF callbacks are one-shot: they are cleared after invocation.
 */
static void
minirend_tick_animation(JSContext *ctx) {
    uint32_t now_ms = get_ticks_ms();
    double   now    = (double)now_ms;

    /* Take ownership of the callback list and clear the global head.
     * This ensures RAF is one-shot (callbacks must re-register each frame). */
    RAFCallback *list = g_raf_head;
    g_raf_head = NULL;

    while (list) {
        RAFCallback *cb = list;
        list = cb->next;

        JSValue arg = JS_NewFloat64(ctx, now);
        JSValue ret = JS_Call(ctx, cb->func, JS_UNDEFINED, 1, &arg);
        JS_FreeValue(ctx, arg);
        if (JS_IsException(ret)) {
            dump_exception(ctx);
        }
        JS_FreeValue(ctx, ret);

        /* Free the callback after invocation */
        JS_FreeValue(ctx, cb->func);
        free(cb);
    }
}

static JSValue
js_performance_now(JSContext *ctx, JSValueConst this_val,
                   int argc, JSValueConst *argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
    double now = (double)get_ticks_ms();
    return JS_NewFloat64(ctx, now);
}

void
minirend_register_timers(JSContext *ctx, MinirendApp *app) {
    (void)app;

    JSValue global_obj = JS_GetGlobalObject(ctx);
    JSValue window_obj = JS_GetPropertyStr(ctx, global_obj, "window");
    if (JS_IsUndefined(window_obj)) {
        /* If DOM hasn't created window yet, just use global object. */
        window_obj = JS_DupValue(ctx, global_obj);
    }

    JS_SetPropertyStr(ctx, window_obj, "requestAnimationFrame",
                      JS_NewCFunction(ctx, js_requestAnimationFrame,
                                      "requestAnimationFrame", 1));
    JS_SetPropertyStr(ctx, window_obj, "cancelAnimationFrame",
                      JS_NewCFunction(ctx, js_cancelAnimationFrame,
                                      "cancelAnimationFrame", 1));

    JSValue perf = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, perf, "now",
                      JS_NewCFunction(ctx, js_performance_now,
                                      "now", 0));
    JS_SetPropertyStr(ctx, window_obj, "performance", perf);

    JS_FreeValue(ctx, window_obj);
    JS_FreeValue(ctx, global_obj);
}

/* Expose a hook for the renderer/main loop to tick animations. */
void
minirend_js_tick_frame(JSContext *ctx) {
    minirend_tick_animation(ctx);
}


