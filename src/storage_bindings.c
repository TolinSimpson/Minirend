#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "minrend.h"
#include "quickjs.h"

/* Simple localStorage/sessionStorage implementation.
 * Values are stored as strings in-memory; localStorage is persisted
 * to a JSON file in the current directory (storage.json).
 */

static const char *k_storage_file = "storage.json";

static char *
read_file_simple(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
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
    return buf;
}

static int
write_file_simple(const char *path, const char *data) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;
    size_t len = strlen(data);
    size_t n   = fwrite(data, 1, len, fp);
    fclose(fp);
    return (n == len) ? 0 : -1;
}

static JSValue
js_localStorage_flush(JSContext *ctx, JSValueConst this_val,
                      int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 1) return JS_UNDEFINED;
    const char *json = JS_ToCString(ctx, argv[0]);
    if (!json) return JS_EXCEPTION;
    if (write_file_simple(k_storage_file, json) != 0) {
        JS_FreeCString(ctx, json);
        return JS_ThrowInternalError(ctx, "failed to write storage");
    }
    JS_FreeCString(ctx, json);
    return JS_UNDEFINED;
}

void
minrend_storage_register(JSContext *ctx) {
    JSValue global_obj = JS_GetGlobalObject(ctx);

    /* Expose a helper to flush serialized storage to disk. */
    JS_SetPropertyStr(ctx, global_obj, "localStorageFlush",
                      JS_NewCFunction(ctx, js_localStorage_flush,
                                      "localStorageFlush", 1));

    /* Initialize localStorage/sessionStorage in JS. */
    const char *init_src_prefix =
        "if (typeof localStorage === 'undefined') {\n"
        "  var __localData = {};\n";

    const char *init_src_suffix =
        "  globalThis.localStorage = {\n"
        "    getItem: function(k) { return Object.prototype.hasOwnProperty.call(__localData, k) ? __localData[k] : null; },\n"
        "    setItem: function(k, v) { __localData[k] = String(v); localStorageFlush(JSON.stringify(__localData)); },\n"
        "    removeItem: function(k) { delete __localData[k]; localStorageFlush(JSON.stringify(__localData)); },\n"
        "    clear: function() { __localData = {}; localStorageFlush(JSON.stringify(__localData)); }\n"
        "  };\n"
        "}\n"
        "if (typeof sessionStorage === 'undefined') {\n"
        "  var __sessionData = {};\n"
        "  globalThis.sessionStorage = {\n"
        "    getItem: function(k) { return Object.prototype.hasOwnProperty.call(__sessionData, k) ? __sessionData[k] : null; },\n"
        "    setItem: function(k, v) { __sessionData[k] = String(v); },\n"
        "    removeItem: function(k) { delete __sessionData[k]; },\n"
        "    clear: function() { __sessionData = {}; }\n"
        "  };\n"
        "}\n";

    char *on_disk = read_file_simple(k_storage_file);
    JSValue ret;
    if (on_disk) {
        /* Seed __localData from disk. */
        size_t needed = strlen(init_src_prefix) + strlen(on_disk) + 64;
        char *src = (char *)malloc(needed);
        if (!src) {
            free(on_disk);
        } else {
            snprintf(src, needed,
                     "%s"
                     "  __localData = %s;\n"
                     "%s",
                     init_src_prefix, on_disk, init_src_suffix);
            ret = JS_Eval(ctx, src, strlen(src),
                          "<storage_init>", JS_EVAL_TYPE_GLOBAL);
            free(src);
            if (JS_IsException(ret)) {
                JSValue exc = JS_GetException(ctx);
                const char *msg = JS_ToCString(ctx, exc);
                if (msg) {
                    fprintf(stderr, "storage init error: %s\n", msg);
                    JS_FreeCString(ctx, msg);
                }
                JS_FreeValue(ctx, exc);
            }
            JS_FreeValue(ctx, ret);
        }
        free(on_disk);
    } else {
        size_t needed = strlen(init_src_prefix) + strlen(init_src_suffix) + 1;
        char *src = (char *)malloc(needed);
        if (src) {
            snprintf(src, needed, "%s%s", init_src_prefix, init_src_suffix);
            ret = JS_Eval(ctx, src, strlen(src),
                          "<storage_init>", JS_EVAL_TYPE_GLOBAL);
            if (JS_IsException(ret)) {
                JSValue exc = JS_GetException(ctx);
                const char *msg = JS_ToCString(ctx, exc);
                if (msg) {
                    fprintf(stderr, "storage init error: %s\n", msg);
                    JS_FreeCString(ctx, msg);
                }
                JS_FreeValue(ctx, exc);
            }
            JS_FreeValue(ctx, ret);
            free(src);
        }
    }

    JS_FreeValue(ctx, global_obj);
}


