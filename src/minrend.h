#ifndef MINREND_H
#define MINREND_H

#include <stdbool.h>
#include <stdint.h>

/* Forward declarations from third‑party libs (headers provided externally). */
typedef struct JSRuntime JSRuntime;
typedef struct JSContext JSContext;

/* Opaque handles for the runtime subsystems. */
typedef struct MinrendApp MinrendApp;

/* Window mode options */
typedef enum {
    MINREND_WINDOW_WINDOWED = 0,
    MINREND_WINDOW_FULLSCREEN,
    MINREND_WINDOW_BORDERLESS
} MinrendWindowMode;

typedef struct MinrendConfig {
    const char *entry_html_path;   /* Path to the HTML file to load. */
    const char *entry_js_path;     /* Optional JS entry file to execute. */
    int         width;
    int         height;
    const char *title;
    MinrendWindowMode window_mode; /* windowed, fullscreen, borderless */
    bool        vsync;
    int         gl_major;          /* OpenGL major version */
    int         gl_minor;          /* OpenGL minor version */
} MinrendConfig;

/* Main lifecycle (implemented in main.c) */
int  minrend_run(const MinrendConfig *cfg);

/* JS engine integration (js_engine.c) */
JSRuntime *minrend_js_init(void);
JSContext *minrend_js_create_context(JSRuntime *rt);
void       minrend_js_register_bindings(JSContext *ctx, MinrendApp *app);
void       minrend_js_dispose(JSRuntime *rt, JSContext *ctx);
int        minrend_js_eval_file(JSContext *ctx, const char *path);

/* DOM / window bindings (dom_bindings.c) */
void minrend_dom_init(JSContext *ctx, MinrendApp *app);

/* Renderer / HTML (renderer.c) */
void minrend_renderer_init(MinrendApp *app);
void minrend_renderer_load_html(MinrendApp *app, const char *path);
void minrend_renderer_draw(MinrendApp *app);

/* WebGL / Canvas (webgl_bindings.c, canvas_bindings.c) */
void minrend_webgl_register(JSContext *ctx, MinrendApp *app);
void minrend_canvas_register(JSContext *ctx, MinrendApp *app);

/* Timing / animation (implemented in js_engine.c using SDL2 timers) */
void minrend_register_timers(JSContext *ctx, MinrendApp *app);
void minrend_js_tick_frame(JSContext *ctx);

/* Console (js_engine.c) */
void minrend_register_console(JSContext *ctx);

/* Networking (fetch_bindings.c) */
void minrend_fetch_register(JSContext *ctx);

/* Storage (storage_bindings.c) */
void minrend_storage_register(JSContext *ctx);

#endif /* MINREND_H */


