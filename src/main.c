#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#include <SDL_opengl.h>

#include "minrend.h"

struct MinrendApp {
    SDL_Window   *window;
    SDL_GLContext gl_ctx;
    int           width;
    int           height;
    JSRuntime    *js_rt;
    JSContext    *js_ctx;
    int           running;
};

/* Parse a simple key=value config file */
static void parse_config_line(const char *line, MinrendConfig *cfg) {
    char key[64] = {0};
    char value[256] = {0};
    
    /* Skip comments and empty lines */
    if (line[0] == '#' || line[0] == '\n' || line[0] == '\r' || line[0] == '\0')
        return;
    
    if (sscanf(line, "%63[^=]=%255[^\n\r]", key, value) == 2) {
        /* Trim whitespace */
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
                cfg->window_mode = MINREND_WINDOW_FULLSCREEN;
            } else if (strcmp(v, "borderless") == 0) {
                cfg->window_mode = MINREND_WINDOW_BORDERLESS;
            } else {
                cfg->window_mode = MINREND_WINDOW_WINDOWED;
            }
        } else if (strcmp(k, "VSYNC") == 0) {
            cfg->vsync = (strcmp(v, "true") == 0 || strcmp(v, "1") == 0);
        } else if (strcmp(k, "OPENGL_MAJOR") == 0) {
            cfg->gl_major = atoi(v);
        } else if (strcmp(k, "OPENGL_MINOR") == 0) {
            cfg->gl_minor = atoi(v);
        }
    }
}

static void load_config(MinrendConfig *cfg) {
    /* Try multiple config locations */
    const char *config_paths[] = {
        "build.config",      /* Embedded in ZIP */
        "app/build.config",  /* Filesystem */
        "../build.config",   /* Parent directory */
        NULL
    };
    
    FILE *f = NULL;
    for (int i = 0; config_paths[i] != NULL; i++) {
        f = fopen(config_paths[i], "r");
        if (f) {
            fprintf(stderr, "Loading config from: %s\n", config_paths[i]);
            break;
        }
    }
    
    if (!f) {
        fprintf(stderr, "No build.config found, using defaults\n");
        return;
    }
    
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        parse_config_line(line, cfg);
    }
    fclose(f);
}

static int
app_init(MinrendApp *app, const MinrendConfig *cfg) {
    Uint32 window_flags;
    
    fprintf(stderr, "\n");
    fprintf(stderr, "╔══════════════════════════════════════╗\n");
    fprintf(stderr, "║          MINREND ENGINE              ║\n");
    fprintf(stderr, "╚══════════════════════════════════════╝\n\n");
    
    /* Set SDL hints for better Cosmopolitan compatibility */
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0");
    
    /* Try setting video driver via environment variable before SDL_Init */
    /* This affects which driver SDL2 uses for window creation */
    const char *force_driver = getenv("SDL_VIDEODRIVER");
    if (force_driver) {
        fprintf(stderr, "Using video driver from environment: %s\n", force_driver);
    }
    
    fprintf(stderr, "Initializing SDL2...\n");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        fprintf(stderr, "\n");
        fprintf(stderr, "┌─────────────────────────────────────────────────────────┐\n");
        fprintf(stderr, "│ SDL2 initialization failed.                             │\n");
        fprintf(stderr, "│                                                         │\n");
        fprintf(stderr, "│ On Windows: Run this executable in WSL:                 │\n");
        fprintf(stderr, "│   wsl ./minrend.exe                                     │\n");
        fprintf(stderr, "│                                                         │\n");
        fprintf(stderr, "│ On Linux: Install SDL2:                                 │\n");
        fprintf(stderr, "│   sudo apt install libsdl2-2.0-0                        │\n");
        fprintf(stderr, "│                                                         │\n");
        fprintf(stderr, "│ On macOS: Install SDL2:                                 │\n");
        fprintf(stderr, "│   brew install sdl2                                     │\n");
        fprintf(stderr, "└─────────────────────────────────────────────────────────┘\n");
        return -1;
    }
    fprintf(stderr, "SDL2 initialized successfully.\n");

    /* Set OpenGL attributes from config */
    int gl_major = cfg->gl_major > 0 ? cfg->gl_major : 3;
    int gl_minor = cfg->gl_minor > 0 ? cfg->gl_minor : 0;
    
    fprintf(stderr, "Requesting OpenGL %d.%d context...\n", gl_major, gl_minor);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, gl_major);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, gl_minor);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    /* Set window dimensions */
    app->width  = cfg->width  > 0 ? cfg->width  : 1280;
    app->height = cfg->height > 0 ? cfg->height : 720;
    
    /* Determine window flags based on mode */
    /* Try without OpenGL first to test basic window creation */
    window_flags = SDL_WINDOW_SHOWN;  /* Removed SDL_WINDOW_OPENGL for testing */
    
    switch (cfg->window_mode) {
        case MINREND_WINDOW_FULLSCREEN:
            window_flags |= SDL_WINDOW_FULLSCREEN;
            fprintf(stderr, "Window mode: fullscreen\n");
            break;
        case MINREND_WINDOW_BORDERLESS:
            window_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
            fprintf(stderr, "Window mode: borderless fullscreen\n");
            break;
        case MINREND_WINDOW_WINDOWED:
        default:
            window_flags |= SDL_WINDOW_RESIZABLE;
            fprintf(stderr, "Window mode: windowed (%dx%d) [NO OPENGL TEST]\n", app->width, app->height);
            break;
    }

    fprintf(stderr, "Creating window: %s\n", cfg->title ? cfg->title : "Minrend");
    app->window = SDL_CreateWindow(
        cfg->title ? cfg->title : "Minrend",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        app->width,
        app->height,
        window_flags
    );
    
    if (!app->window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return -1;
    }
    fprintf(stderr, "Window created successfully.\n");

    /* Skip OpenGL context for testing basic window creation */
    fprintf(stderr, "Skipping OpenGL context creation (test mode)...\n");
    app->gl_ctx = NULL;
    
    #if 0  /* Disabled for testing */
    fprintf(stderr, "Creating OpenGL context...\n");
    app->gl_ctx = SDL_GL_CreateContext(app->window);
    if (!app->gl_ctx) {
        fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        return -1;
    }
    fprintf(stderr, "OpenGL context created.\n");

    SDL_GL_MakeCurrent(app->window, app->gl_ctx);
    
    /* Set vsync based on config */
    if (cfg->vsync) {
        SDL_GL_SetSwapInterval(1);
        fprintf(stderr, "VSync: enabled\n");
    } else {
        SDL_GL_SetSwapInterval(0);
        fprintf(stderr, "VSync: disabled\n");
    }
    #endif

    /* Initialize QuickJS runtime and context. */
    fprintf(stderr, "Initializing JavaScript engine...\n");
    app->js_rt  = minrend_js_init();
    app->js_ctx = minrend_js_create_context(app->js_rt);

    /* Register host bindings. */
    minrend_register_console(app->js_ctx);
    minrend_dom_init(app->js_ctx, app);
    minrend_webgl_register(app->js_ctx, app);
    minrend_canvas_register(app->js_ctx, app);
    minrend_register_timers(app->js_ctx, app);
    minrend_fetch_register(app->js_ctx);
    minrend_storage_register(app->js_ctx);

    /* Initialize renderer and load HTML. */
    minrend_renderer_init(app);
    if (cfg->entry_html_path) {
        fprintf(stderr, "Loading HTML: %s\n", cfg->entry_html_path);
        minrend_renderer_load_html(app, cfg->entry_html_path);
    }
    if (cfg->entry_js_path) {
        fprintf(stderr, "Loading JS: %s\n", cfg->entry_js_path);
        if (minrend_js_eval_file(app->js_ctx, cfg->entry_js_path) != 0) {
            fprintf(stderr, "Warning: Failed to evaluate JS entry file: %s\n",
                    cfg->entry_js_path);
        }
    }

    fprintf(stderr, "\nMinrend ready.\n\n");
    app->running = 1;
    return 0;
}

static void
app_shutdown(MinrendApp *app) {
    if (app->js_rt || app->js_ctx) {
        minrend_js_dispose(app->js_rt, app->js_ctx);
    }

    if (app->gl_ctx) {
        SDL_GL_DeleteContext(app->gl_ctx);
    }
    if (app->window) {
        SDL_DestroyWindow(app->window);
    }
    SDL_Quit();
}

static void
app_handle_event(MinrendApp *app, const SDL_Event *ev) {
    switch (ev->type) {
    case SDL_QUIT:
        app->running = 0;
        break;
    case SDL_WINDOWEVENT:
        if (ev->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            app->width  = ev->window.data1;
            app->height = ev->window.data2;
            glViewport(0, 0, app->width, app->height);
        }
        break;
    case SDL_KEYDOWN:
        /* ESC to quit, F11 to toggle fullscreen */
        if (ev->key.keysym.sym == SDLK_ESCAPE) {
            app->running = 0;
        } else if (ev->key.keysym.sym == SDLK_F11) {
            Uint32 flags = SDL_GetWindowFlags(app->window);
            if (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) {
                SDL_SetWindowFullscreen(app->window, 0);
            } else {
                SDL_SetWindowFullscreen(app->window, SDL_WINDOW_FULLSCREEN_DESKTOP);
            }
        }
        break;
    default:
        break;
    }
}

int
minrend_run(const MinrendConfig *cfg) {
    MinrendApp app = {0};

    if (app_init(&app, cfg) != 0) {
        app_shutdown(&app);
        return EXIT_FAILURE;
    }

    while (app.running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            app_handle_event(&app, &ev);
        }

        /* Tick JS animation callbacks (requestAnimationFrame). */
        if (app.js_ctx) {
            minrend_js_tick_frame(app.js_ctx);
        }

        /* Clear frame and delegate rendering. */
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        minrend_renderer_draw(&app);

        SDL_GL_SwapWindow(app.window);
    }

    app_shutdown(&app);
    return EXIT_SUCCESS;
}

int
main(int argc, char **argv) {
    MinrendConfig cfg = {0};
    
    /* Set defaults */
    cfg.width = 1280;
    cfg.height = 720;
    cfg.title = "Minrend";
    cfg.window_mode = MINREND_WINDOW_WINDOWED;
    cfg.vsync = true;
    cfg.gl_major = 3;
    cfg.gl_minor = 0;
    
    /* Load config from file (overrides defaults) */
    load_config(&cfg);
    
    /* Command line overrides config file */
    if (argc > 1) {
        cfg.entry_html_path = argv[1];
    } else {
        /* Try embedded ZIP path first, fall back to filesystem path */
        FILE *test = fopen("index.html", "rb");
        if (test) {
            fclose(test);
            cfg.entry_html_path = "index.html";  /* Embedded ZIP */
        } else {
            cfg.entry_html_path = "app/index.html";  /* Filesystem */
        }
    }
    
    if (argc > 2) {
        cfg.entry_js_path = argv[2];
    } else {
        /* Try embedded ZIP path first, fall back to filesystem path */
        FILE *test = fopen("main.js", "rb");
        if (test) {
            fclose(test);
            cfg.entry_js_path = "main.js";  /* Embedded ZIP */
        } else {
            cfg.entry_js_path = "app/main.js";  /* Filesystem */
        }
    }

    return minrend_run(&cfg);
}
