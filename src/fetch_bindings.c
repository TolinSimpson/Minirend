#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

#include "minirend.h"
#include "quickjs.h"

/* Very small HTTP helper used by JS-level fetch wrapper.
 * Only supports:
 *   - http:// URLs
 *   - GET
 *   - text responses (no binary / chunked handling)
 */

static char *
http_get_simple(const char *url) {
    const char *prefix = "http://";
    if (strncmp(url, prefix, strlen(prefix)) != 0) {
        fprintf(stderr, "http_get_simple: only http:// URLs supported\n");
        return NULL;
    }

    const char *host_start = url + strlen(prefix);
    const char *path_start = strchr(host_start, '/');
    if (!path_start) path_start = "/";

    char host[256];
    size_t host_len = (size_t)(path_start - host_start);
    if (host_len >= sizeof(host)) host_len = sizeof(host) - 1;
    memcpy(host, host_start, host_len);
    host[host_len] = '\0';

    const char *path = (*path_start == '/') ? path_start : "/";

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    int err = getaddrinfo(host, "80", &hints, &res);
    if (err != 0 || !res) {
        fprintf(stderr, "getaddrinfo failed: %s\n", gai_strerror(err));
        return NULL;
    }

    int sock = -1;
    struct addrinfo *rp;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock == -1) continue;
        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        close(sock);
        sock = -1;
    }
    freeaddrinfo(res);

    if (sock == -1) {
        fprintf(stderr, "http_get_simple: connect failed\n");
        return NULL;
    }

    char req[1024];
    snprintf(req, sizeof(req),
             "GET %s HTTP/1.0\r\n"
             "Host: %s\r\n"
             "Connection: close\r\n"
             "\r\n",
             path, host);

    if (send(sock, req, strlen(req), 0) < 0) {
        fprintf(stderr, "http_get_simple: send failed\n");
        close(sock);
        return NULL;
    }

    size_t cap = 16 * 1024;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        close(sock);
        return NULL;
    }

    for (;;) {
        if (len + 4096 > cap) {
            cap *= 2;
            char *nbuf = (char *)realloc(buf, cap);
            if (!nbuf) {
                free(buf);
                close(sock);
                return NULL;
            }
            buf = nbuf;
        }
        ssize_t n = recv(sock, buf + len, 4096, 0);
        if (n <= 0) break;
        len += (size_t)n;
    }
    close(sock);

    buf[len] = '\0';

    /* Strip HTTP headers. */
    char *body = strstr(buf, "\r\n\r\n");
    if (body) {
        body += 4;
        size_t body_len = len - (size_t)(body - buf);
        char *out = (char *)malloc(body_len + 1);
        if (!out) {
            free(buf);
            return NULL;
        }
        memcpy(out, body, body_len);
        out[body_len] = '\0';
        free(buf);
        return out;
    }

    return buf;
}

static JSValue
js_httpGet(JSContext *ctx, JSValueConst this_val,
           int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "url required");
    }
    const char *url = JS_ToCString(ctx, argv[0]);
    if (!url) return JS_EXCEPTION;
    char *body = http_get_simple(url);
    JS_FreeCString(ctx, url);
    if (!body) {
        return JS_NULL;
    }
    JSValue result = JS_NewString(ctx, body);
    free(body);
    return result;
}

void
minirend_fetch_register(JSContext *ctx) {
    JSValue global_obj = JS_GetGlobalObject(ctx);

    JS_SetPropertyStr(ctx, global_obj, "httpGet",
                      JS_NewCFunction(ctx, js_httpGet, "httpGet", 1));

    /* Define a basic fetch implementation in JS using httpGet.
     * This is Promise-based to be source-compatible with most code,
     * though the underlying I/O is synchronous at the moment.
     */
    const char *shim_src =
        "if (typeof fetch === 'undefined') {\n"
        "  function _wrapResponse(body) {\n"
        "    return {\n"
        "      ok: true,\n"
        "      text: function() { return Promise.resolve(body); },\n"
        "      json: function() { return Promise.resolve(JSON.parse(body)); }\n"
        "    };\n"
        "  }\n"
        "  globalThis.fetch = function(url) {\n"
        "    var body = httpGet(url);\n"
        "    return Promise.resolve(_wrapResponse(body || ''));\n"
        "  };\n"
        "}\n";

    JSValue ret = JS_Eval(ctx, shim_src, strlen(shim_src),
                          "<fetch_shim>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(ret)) {
        JSValue exc = JS_GetException(ctx);
        const char *msg = JS_ToCString(ctx, exc);
        if (msg) {
            fprintf(stderr, "fetch shim error: %s\n", msg);
            JS_FreeCString(ctx, msg);
        }
        JS_FreeValue(ctx, exc);
    }
    JS_FreeValue(ctx, ret);

    JS_FreeValue(ctx, global_obj);
}


