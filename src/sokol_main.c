/*
 * sokol_main.c - minirend entry point using sokol_app + sokol_gfx
 * 
 * Sokol provides:
 *   - Cross-platform windowing (sokol_app.h)
 *   - Graphics abstraction (sokol_gfx.h) - D3D11/Metal/GL/WebGPU
 *   - All in single-header libraries (~40KB total)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* Sokol headers for types only - implementation is in platform shims */
/* For Cosmopolitan, we compile sokol separately for each platform:
 * - sokol_windows.c (D3D11 backend)
 * - sokol_linux.c (OpenGL backend)
 * - sokol_cosmo.c (runtime dispatcher)
 */
#define SOKOL_NO_ENTRY
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"

#include "minirend.h"
#include "input.h"
#include "dom_runtime.h"
#include "ui_tree.h"
#include "lexbor_adapter.h"

/* =========================================================================
 * Application State
 * ========================================================================= */

typedef struct {
    /* Window */
    int width;
    int height;
    const char *title;
    bool fullscreen;
    
    /* JavaScript engine */
    JSRuntime *js_rt;
    JSContext *js_ctx;
    
    /* Graphics state */
    sg_pass_action pass_action;
    
    /* Configuration */
    MinirendConfig config;
    
    /* Running state */
    bool initialized;
} MinirendState;

static MinirendState g_state = {0};

/* =========================================================================
 * Configuration
 * ========================================================================= */

static void parse_config_line(const char *line, MinirendConfig *cfg) {
    char key[64] = {0};
    char value[256] = {0};
    
    if (line[0] == '#' || line[0] == '\n' || line[0] == '\r' || line[0] == '\0')
        return;
    
    if (sscanf(line, "%63[^=]=%255[^\n\r]", key, value) == 2) {
        char *k = key, *v = value;
        while (*k == ' ') k++;
        while (*v == ' ') v++;
        
        if (strcmp(k, "WINDOW_WIDTH") == 0) {
            cfg->width = atoi(v);
        } else if (strcmp(k, "WINDOW_HEIGHT") == 0) {
            cfg->height = atoi(v);
        } else if (strcmp(k, "WINDOW_TITLE") == 0) {
            static char title_buf[256];
            strncpy(title_buf, v, sizeof(title_buf)-1);
            cfg->title = title_buf;
        } else if (strcmp(k, "WINDOW_MODE") == 0) {
            if (strcmp(v, "fullscreen") == 0) {
                cfg->window_mode = MINIREND_WINDOW_FULLSCREEN;
            } else if (strcmp(v, "borderless") == 0) {
                cfg->window_mode = MINIREND_WINDOW_BORDERLESS;
            } else {
                cfg->window_mode = MINIREND_WINDOW_WINDOWED;
            }
        } else if (strcmp(k, "VSYNC") == 0) {
            cfg->vsync = (strcmp(v, "true") == 0 || strcmp(v, "1") == 0);
        }
    }
}

static void load_config(MinirendConfig *cfg) {
    const char *config_paths[] = {
        "build.config",
        "app/build.config",
        "../build.config",
        NULL
    };
    
    FILE *f = NULL;
    for (int i = 0; config_paths[i] != NULL; i++) {
        f = fopen(config_paths[i], "r");
        if (f) {
            fprintf(stderr, "[minirend] Loading config from: %s\n", config_paths[i]);
            break;
        }
    }
    
    if (!f) {
        fprintf(stderr, "[minirend] No build.config found, using defaults\n");
        return;
    }
    
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        parse_config_line(line, cfg);
    }
    fclose(f);
}

/* =========================================================================
 * Sokol Callbacks
 * ========================================================================= */

static void init_cb(void) {
    fprintf(stderr, "\n");
    fprintf(stderr, "╔══════════════════════════════════════╗\n");
    fprintf(stderr, "║         MINIREND ENGINE (Sokol)       ║\n");
    fprintf(stderr, "╚══════════════════════════════════════╝\n\n");
    
    /* Initialize sokol_gfx */
    sg_desc desc = {
        .environment = sglue_environment(),
        .logger.func = NULL,  /* Use default logger */
    };
    sg_setup(&desc);
    
    /* Set up clear color (dark background) */
    g_state.pass_action = (sg_pass_action){
        .colors[0] = {
            .load_action = SG_LOADACTION_CLEAR,
            .clear_value = { 0.1f, 0.1f, 0.12f, 1.0f }
        }
    };
    
    fprintf(stderr, "[minirend] Graphics backend: ");
    #if defined(SOKOL_D3D11)
        fprintf(stderr, "D3D11\n");
    #elif defined(SOKOL_METAL)
        fprintf(stderr, "Metal\n");
    #elif defined(SOKOL_GLCORE)
        fprintf(stderr, "OpenGL Core\n");
    #elif defined(SOKOL_WGPU)
        fprintf(stderr, "WebGPU\n");
    #else
        fprintf(stderr, "Unknown\n");
    #endif
    
    /* Initialize JavaScript engine */
    fprintf(stderr, "[minirend] Initializing JavaScript engine...\n");
    g_state.js_rt = minirend_js_init();
    g_state.js_ctx = minirend_js_create_context(g_state.js_rt);
    
    /* Register host bindings */
    minirend_register_console(g_state.js_ctx);
    minirend_dom_init(g_state.js_ctx, NULL);
    minirend_register_timers(g_state.js_ctx, NULL);
    minirend_webgl_register(g_state.js_ctx, NULL);
    minirend_canvas_register(g_state.js_ctx, NULL);
    minirend_fetch_register(g_state.js_ctx);
    minirend_storage_register(g_state.js_ctx);

    /* Initialize input system after DOM/runtime are available. */
    minirend_input_init(g_state.js_ctx);
    
    /* Load entry files */
    if (g_state.config.entry_html_path) {
        fprintf(stderr, "[minirend] HTML entry: %s\n", g_state.config.entry_html_path);
        /* TODO: minirend_renderer_load_html(NULL, g_state.config.entry_html_path); */
    }
    
    if (g_state.config.entry_js_path) {
        fprintf(stderr, "[minirend] JS entry: %s\n", g_state.config.entry_js_path);
        if (minirend_js_eval_file(g_state.js_ctx, g_state.config.entry_js_path) != 0) {
            fprintf(stderr, "[minirend] Warning: Failed to evaluate JS entry\n");
        }
    }
    
    g_state.initialized = true;
    fprintf(stderr, "[minirend] Ready.\n\n");
}

static void frame_cb(void) {
    if (!g_state.initialized) return;
    
    /* Process platform input events before running JS frame callbacks. */
    if (g_state.js_ctx) {
        minirend_input_tick(g_state.js_ctx);
    }

    /* Tick JavaScript animation callbacks */
    if (g_state.js_ctx) {
        minirend_js_tick_frame(g_state.js_ctx);
    }
    
    /* Begin render pass */
    sg_pass pass = {
        .action = g_state.pass_action,
        .swapchain = sglue_swapchain()
    };
    sg_begin_pass(&pass);
    
    /* TODO: Render HTML/CSS content here */
    /* minirend_renderer_draw(NULL); */
    
    sg_end_pass();
    sg_commit();
}

static void cleanup_cb(void) {
    fprintf(stderr, "[minirend] Shutting down...\n");
    
    /* Tear down subsystems that hold JS refs before destroying the JS context. */
    if (g_state.js_ctx) {
        minirend_input_shutdown(g_state.js_ctx);
        minirend_dom_runtime_shutdown(g_state.js_ctx);
    }
    minirend_lexbor_adapter_shutdown();
    minirend_ui_tree_shutdown();

    if (g_state.js_rt || g_state.js_ctx) {
        minirend_js_dispose(g_state.js_rt, g_state.js_ctx);
    }
    
    sg_shutdown();
}

static void event_cb(const sapp_event *ev) {
    switch (ev->type) {
        case SAPP_EVENTTYPE_RESIZED:
            g_state.width = ev->window_width;
            g_state.height = ev->window_height;
            fprintf(stderr, "[minirend] Window resized: %dx%d\n", 
                    g_state.width, g_state.height);
            if (g_state.js_ctx) {
                minirend_dom_set_viewport(g_state.js_ctx, g_state.width, g_state.height);
            }
            minirend_input_push_sapp_event(ev);
            break;
            
        case SAPP_EVENTTYPE_KEY_DOWN:
            /* ESC to quit */
            if (ev->key_code == SAPP_KEYCODE_ESCAPE) {
                sapp_request_quit();
            }
            /* F11 to toggle fullscreen */
            else if (ev->key_code == SAPP_KEYCODE_F11) {
                sapp_toggle_fullscreen();
            }
            minirend_input_push_sapp_event(ev);
            break;

        case SAPP_EVENTTYPE_KEY_UP:
        case SAPP_EVENTTYPE_CHAR:
            minirend_input_push_sapp_event(ev);
            break;
            
        case SAPP_EVENTTYPE_MOUSE_DOWN:
        case SAPP_EVENTTYPE_MOUSE_UP:
        case SAPP_EVENTTYPE_MOUSE_MOVE:
        case SAPP_EVENTTYPE_MOUSE_SCROLL:
            minirend_input_push_sapp_event(ev);
            break;
            
        default:
            break;
    }
}

/* =========================================================================
 * Entry Point
 * ========================================================================= */

sapp_desc sokol_main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    /* Initialize config with defaults */
    g_state.config = (MinirendConfig){
        .width = 1280,
        .height = 720,
        .title = "minirend",
        .window_mode = MINIREND_WINDOW_WINDOWED,
        .vsync = true,
    };
    
    /* Load config from file */
    load_config(&g_state.config);
    
    /* Set entry paths */
    if (argc > 1) {
        g_state.config.entry_html_path = argv[1];
    } else {
        FILE *test = fopen("index.html", "rb");
        if (test) {
            fclose(test);
            g_state.config.entry_html_path = "index.html";
        } else {
            g_state.config.entry_html_path = "app/index.html";
        }
    }
    
    if (argc > 2) {
        g_state.config.entry_js_path = argv[2];
    } else {
        FILE *test = fopen("main.js", "rb");
        if (test) {
            fclose(test);
            g_state.config.entry_js_path = "main.js";
        } else {
            g_state.config.entry_js_path = "app/main.js";
        }
    }
    
    /* Store dimensions */
    g_state.width = g_state.config.width;
    g_state.height = g_state.config.height;
    g_state.title = g_state.config.title;
    g_state.fullscreen = (g_state.config.window_mode == MINIREND_WINDOW_FULLSCREEN);
    
    return (sapp_desc){
        .init_cb = init_cb,
        .frame_cb = frame_cb,
        .cleanup_cb = cleanup_cb,
        .event_cb = event_cb,
        .width = g_state.width,
        .height = g_state.height,
        .window_title = g_state.title,
        .fullscreen = g_state.fullscreen,
        .high_dpi = true,
        .sample_count = 4,
        .swap_interval = g_state.config.vsync ? 1 : 0,
        .icon.sokol_default = true,
        .logger.func = NULL,
    };
}

/* =========================================================================
 * Legacy API Compatibility (for minirend_run)
 * ========================================================================= */

int minirend_run(const MinirendConfig *cfg) {
    /* Store config */
    if (cfg) {
        g_state.config = *cfg;
        g_state.width = cfg->width > 0 ? cfg->width : 1280;
        g_state.height = cfg->height > 0 ? cfg->height : 720;
        g_state.title = cfg->title ? cfg->title : "minirend";
        g_state.fullscreen = (cfg->window_mode == MINIREND_WINDOW_FULLSCREEN);
    }
    
    /* With SOKOL_NO_ENTRY, sokol_main is not called automatically.
     * Instead, sapp_run() should be called, but that requires the desc.
     * For now, this function is provided for API compatibility.
     * The actual entry point is sokol_main() above.
     */
    fprintf(stderr, "[minirend] minirend_run called - use sokol_main entry point instead\n");
    return 0;
}


