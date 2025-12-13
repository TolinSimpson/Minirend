#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "minirend.h"
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

/* Native function to load storage file contents as a string.
 * Returns the file contents or null if file doesn't exist.
 * This avoids JS code injection by keeping file data as a string. */
static JSValue
js_localStorage_load(JSContext *ctx, JSValueConst this_val,
                     int argc, JSValueConst *argv) {
    (void)this_val;
    (void)argc;
    (void)argv;
    char *on_disk = read_file_simple(k_storage_file);
    if (!on_disk) {
        return JS_NULL;
    }
    JSValue result = JS_NewString(ctx, on_disk);
    free(on_disk);
    return result;
}

void
minirend_storage_register(JSContext *ctx) {
    JSValue global_obj = JS_GetGlobalObject(ctx);

    /* Expose helpers for storage persistence. */
    JS_SetPropertyStr(ctx, global_obj, "__localStorageFlush",
                      JS_NewCFunction(ctx, js_localStorage_flush,
                                      "__localStorageFlush", 1));
    JS_SetPropertyStr(ctx, global_obj, "__localStorageLoad",
                      JS_NewCFunction(ctx, js_localStorage_load,
                                      "__localStorageLoad", 0));

    /* Initialize localStorage/sessionStorage in JS.
     * SECURITY: File contents are loaded as a string and parsed with JSON.parse
     * to prevent code injection from malicious storage.json files. */
    const char *init_src =
        "(function() {\n"
        "  if (typeof localStorage !== 'undefined') return;\n"
        "  var __localData = {};\n"
        "  var __rawData = __localStorageLoad();\n"
        "  if (__rawData !== null) {\n"
        "    try {\n"
        "      var parsed = JSON.parse(__rawData);\n"
        "      if (parsed && typeof parsed === 'object' && !Array.isArray(parsed)) {\n"
        "        __localData = parsed;\n"
        "      }\n"
        "    } catch (e) {\n"
        "      /* Invalid JSON in storage file - start fresh */\n"
        "    }\n"
        "  }\n"
        "  globalThis.localStorage = {\n"
        "    getItem: function(k) {\n"
        "      return Object.prototype.hasOwnProperty.call(__localData, k) ? __localData[k] : null;\n"
        "    },\n"
        "    setItem: function(k, v) {\n"
        "      __localData[k] = String(v);\n"
        "      __localStorageFlush(JSON.stringify(__localData));\n"
        "    },\n"
        "    removeItem: function(k) {\n"
        "      delete __localData[k];\n"
        "      __localStorageFlush(JSON.stringify(__localData));\n"
        "    },\n"
        "    clear: function() {\n"
        "      __localData = {};\n"
        "      __localStorageFlush(JSON.stringify(__localData));\n"
        "    },\n"
        "    get length() {\n"
        "      return Object.keys(__localData).length;\n"
        "    },\n"
        "    key: function(n) {\n"
        "      var keys = Object.keys(__localData);\n"
        "      return n >= 0 && n < keys.length ? keys[n] : null;\n"
        "    }\n"
        "  };\n"
        "  /* Clean up loader from global scope */\n"
        "  delete globalThis.__localStorageLoad;\n"
        "})();\n"
        "(function() {\n"
        "  if (typeof sessionStorage !== 'undefined') return;\n"
        "  var __sessionData = {};\n"
        "  globalThis.sessionStorage = {\n"
        "    getItem: function(k) {\n"
        "      return Object.prototype.hasOwnProperty.call(__sessionData, k) ? __sessionData[k] : null;\n"
        "    },\n"
        "    setItem: function(k, v) { __sessionData[k] = String(v); },\n"
        "    removeItem: function(k) { delete __sessionData[k]; },\n"
        "    clear: function() { __sessionData = {}; },\n"
        "    get length() {\n"
        "      return Object.keys(__sessionData).length;\n"
        "    },\n"
        "    key: function(n) {\n"
        "      var keys = Object.keys(__sessionData);\n"
        "      return n >= 0 && n < keys.length ? keys[n] : null;\n"
        "    }\n"
        "  };\n"
        "})();\n";

    JSValue ret = JS_Eval(ctx, init_src, strlen(init_src),
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

    JS_FreeValue(ctx, global_obj);
}


