#ifndef MINIREND_H
#define MINIREND_H

#include <stdbool.h>
#include <stdint.h>

/* Forward declarations from third‑party libs (headers provided externally). */
typedef struct JSRuntime JSRuntime;
typedef struct JSContext JSContext;

/* Opaque handles for the runtime subsystems. */
typedef struct MinirendApp MinirendApp;

/* Window mode options */
typedef enum {
    MINIREND_WINDOW_WINDOWED = 0,
    MINIREND_WINDOW_FULLSCREEN,
    MINIREND_WINDOW_BORDERLESS
} MinirendWindowMode;

typedef struct MinirendConfig {
    const char *entry_html_path;   /* Path to the HTML file to load. */
    const char *entry_js_path;     /* Optional JS entry file to execute. */
    int         width;
    int         height;
    const char *title;
    MinirendWindowMode window_mode; /* windowed, fullscreen, borderless */
    bool        vsync;
    int         gl_major;          /* OpenGL major version */
    int         gl_minor;          /* OpenGL minor version */
} MinirendConfig;

/* Main lifecycle (implemented in main.c) */
int  minirend_run(const MinirendConfig *cfg);

/* JS engine integration (js_engine.c) */
JSRuntime *minirend_js_init(void);
JSContext *minirend_js_create_context(JSRuntime *rt);
void       minirend_js_register_bindings(JSContext *ctx, MinirendApp *app);
void       minirend_js_dispose(JSRuntime *rt, JSContext *ctx);
int        minirend_js_eval_file(JSContext *ctx, const char *path);

/* DOM / window bindings (dom_bindings.c) */
void minirend_dom_init(JSContext *ctx, MinirendApp *app);
void minirend_dom_set_viewport(JSContext *ctx, int width, int height);

/* Renderer / HTML (renderer.c) */
void minirend_renderer_init(MinirendApp *app);
void minirend_renderer_shutdown(void);
void minirend_renderer_load_html(MinirendApp *app, const char *path);
void minirend_renderer_draw(MinirendApp *app);
void minirend_renderer_set_viewport(float width, float height);
int  minirend_renderer_load_font(const char *path);
bool minirend_renderer_add_stylesheet(const char *css, size_t len);

/* WebGL / Canvas (webgl_bindings.c, canvas_bindings.c) */
void minirend_webgl_register(JSContext *ctx, MinirendApp *app);
void minirend_canvas_register(JSContext *ctx, MinirendApp *app);

/* Timing / animation (implemented in js_engine.c) */
void minirend_register_timers(JSContext *ctx, MinirendApp *app);
void minirend_js_tick_frame(JSContext *ctx);

/* Console (js_engine.c) */
void minirend_register_console(JSContext *ctx);

/* Networking (fetch_bindings.c) */
void minirend_fetch_register(JSContext *ctx);

/* Storage (storage_bindings.c) */
void minirend_storage_register(JSContext *ctx);

#endif /* MINIREND_H */


