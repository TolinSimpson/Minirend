/*
 * SDL2 and OpenGL dynamic loading for Cosmopolitan builds
 * 
 * This file dynamically loads SDL2 and OpenGL at runtime using
 * Cosmopolitan's cosmo_dlopen() API.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cosmo.h>
#include <dlfcn.h>
#include <SDL.h>
#include <SDL_opengl.h>

/* Library handles */
static void* sdl2_lib = NULL;
static void* gl_lib = NULL;
static int libs_initialized = 0;
static int libs_available = 0;

/* SDL2 function pointers */
static int (*p_SDL_Init)(Uint32 flags) = NULL;
static void (*p_SDL_Quit)(void) = NULL;
static const char* (*p_SDL_GetError)(void) = NULL;
static int (*p_SDL_GL_SetAttribute)(SDL_GLattr attr, int value) = NULL;
static SDL_Window* (*p_SDL_CreateWindow)(const char *title, int x, int y, int w, int h, Uint32 flags) = NULL;
static void (*p_SDL_DestroyWindow)(SDL_Window *window) = NULL;
static SDL_GLContext (*p_SDL_GL_CreateContext)(SDL_Window *window) = NULL;
static void (*p_SDL_GL_DeleteContext)(SDL_GLContext context) = NULL;
static int (*p_SDL_GL_MakeCurrent)(SDL_Window *window, SDL_GLContext context) = NULL;
static int (*p_SDL_GL_SetSwapInterval)(int interval) = NULL;
static void (*p_SDL_GL_SwapWindow)(SDL_Window *window) = NULL;
static int (*p_SDL_PollEvent)(SDL_Event *event) = NULL;
static Uint32 (*p_SDL_GetTicks)(void) = NULL;
static SDL_bool (*p_SDL_SetHint)(const char *name, const char *value) = NULL;
static Uint32 (*p_SDL_GetWindowFlags)(SDL_Window *window) = NULL;
static int (*p_SDL_SetWindowFullscreen)(SDL_Window *window, Uint32 flags) = NULL;

/* OpenGL function pointers */
static void (*p_glViewport)(GLint x, GLint y, GLsizei width, GLsizei height) = NULL;
static void (*p_glClearColor)(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) = NULL;
static void (*p_glClear)(GLbitfield mask) = NULL;

/* Error message buffer */
static char error_msg[512] = "SDL2 not loaded";

/* Try to load a library from multiple possible names using cosmo_dlopen */
static void* try_load_lib(const char** names) {
    void* lib = NULL;
    for (int i = 0; names[i] != NULL; i++) {
        lib = cosmo_dlopen(names[i], RTLD_LAZY);
        if (lib) {
            return lib;
        }
    }
    return NULL;
}

/* Initialize dynamic libraries */
static void init_libs(void) {
    fprintf(stderr, "[SDL2 Loader] init_libs() called\n");
    if (libs_initialized) {
        fprintf(stderr, "[SDL2 Loader] Already initialized\n");
        return;
    }
    libs_initialized = 1;
    fprintf(stderr, "[SDL2 Loader] Starting library loading...\n");
    
    /* Library names to try based on platform */
    const char* sdl2_names[] = {
        /* Windows */
        "SDL2.dll",
        ".\\SDL2.dll",
        "./SDL2.dll",
        /* Linux */
        "libSDL2-2.0.so.0",
        "libSDL2.so",
        /* macOS */
        "/Library/Frameworks/SDL2.framework/SDL2",
        "/usr/local/lib/libSDL2.dylib",
        "/opt/homebrew/lib/libSDL2.dylib",
        NULL
    };
    
    const char* gl_names[] = {
        /* Windows */
        "opengl32.dll",
        /* Linux */
        "libGL.so.1",
        "libGL.so",
        /* macOS */
        "/System/Library/Frameworks/OpenGL.framework/OpenGL",
        NULL
    };
    
    /* Load SDL2 */
    sdl2_lib = try_load_lib(sdl2_names);
    
    if (!sdl2_lib) {
        snprintf(error_msg, sizeof(error_msg), 
            "Could not load SDL2 library. Run: scripts/bootstrap_sdl2");
        return;
    }
    
    /* Load OpenGL */
    gl_lib = try_load_lib(gl_names);
    
    /* Load SDL2 functions using cosmo_dlsym */
    p_SDL_Init = (int (*)(Uint32))cosmo_dlsym(sdl2_lib, "SDL_Init");
    p_SDL_Quit = (void (*)(void))cosmo_dlsym(sdl2_lib, "SDL_Quit");
    p_SDL_GetError = (const char* (*)(void))cosmo_dlsym(sdl2_lib, "SDL_GetError");
    p_SDL_GL_SetAttribute = (int (*)(SDL_GLattr, int))cosmo_dlsym(sdl2_lib, "SDL_GL_SetAttribute");
    p_SDL_CreateWindow = (SDL_Window* (*)(const char*, int, int, int, int, Uint32))cosmo_dlsym(sdl2_lib, "SDL_CreateWindow");
    p_SDL_DestroyWindow = (void (*)(SDL_Window*))cosmo_dlsym(sdl2_lib, "SDL_DestroyWindow");
    p_SDL_GL_CreateContext = (SDL_GLContext (*)(SDL_Window*))cosmo_dlsym(sdl2_lib, "SDL_GL_CreateContext");
    p_SDL_GL_DeleteContext = (void (*)(SDL_GLContext))cosmo_dlsym(sdl2_lib, "SDL_GL_DeleteContext");
    p_SDL_GL_MakeCurrent = (int (*)(SDL_Window*, SDL_GLContext))cosmo_dlsym(sdl2_lib, "SDL_GL_MakeCurrent");
    p_SDL_GL_SetSwapInterval = (int (*)(int))cosmo_dlsym(sdl2_lib, "SDL_GL_SetSwapInterval");
    p_SDL_GL_SwapWindow = (void (*)(SDL_Window*))cosmo_dlsym(sdl2_lib, "SDL_GL_SwapWindow");
    p_SDL_PollEvent = (int (*)(SDL_Event*))cosmo_dlsym(sdl2_lib, "SDL_PollEvent");
    p_SDL_GetTicks = (Uint32 (*)(void))cosmo_dlsym(sdl2_lib, "SDL_GetTicks");
    p_SDL_SetHint = (SDL_bool (*)(const char*, const char*))cosmo_dlsym(sdl2_lib, "SDL_SetHint");
    p_SDL_GetWindowFlags = (Uint32 (*)(SDL_Window*))cosmo_dlsym(sdl2_lib, "SDL_GetWindowFlags");
    p_SDL_SetWindowFullscreen = (int (*)(SDL_Window*, Uint32))cosmo_dlsym(sdl2_lib, "SDL_SetWindowFullscreen");
    
    /* Load OpenGL functions */
    if (gl_lib) {
        p_glViewport = (void (*)(GLint, GLint, GLsizei, GLsizei))cosmo_dlsym(gl_lib, "glViewport");
        p_glClearColor = (void (*)(GLfloat, GLfloat, GLfloat, GLfloat))cosmo_dlsym(gl_lib, "glClearColor");
        p_glClear = (void (*)(GLbitfield))cosmo_dlsym(gl_lib, "glClear");
    }
    
    /* Check if essential functions are available */
    if (p_SDL_Init && p_SDL_CreateWindow && p_SDL_GL_CreateContext) {
        libs_available = 1;
    }
}

/* SDL2 wrapper functions */
int SDL_Init(Uint32 flags) {
    init_libs();
    if (p_SDL_Init) {
        return p_SDL_Init(flags);
    }
    snprintf(error_msg, sizeof(error_msg), "SDL2 library not loaded");
    return -1;
}

void SDL_Quit(void) {
    if (p_SDL_Quit) p_SDL_Quit();
    if (sdl2_lib) { cosmo_dlclose(sdl2_lib); sdl2_lib = NULL; }
    if (gl_lib) { cosmo_dlclose(gl_lib); gl_lib = NULL; }
}

const char* SDL_GetError(void) {
    if (p_SDL_GetError) return p_SDL_GetError();
    return error_msg;
}

int SDL_GL_SetAttribute(SDL_GLattr attr, int value) {
    fprintf(stderr, "[SDL2] SDL_GL_SetAttribute(%d, %d)\n", attr, value);
    init_libs();
    if (p_SDL_GL_SetAttribute) {
        fprintf(stderr, "[SDL2] Calling p_SDL_GL_SetAttribute...\n");
        fflush(stderr);
        int ret = p_SDL_GL_SetAttribute(attr, value);
        fprintf(stderr, "[SDL2] p_SDL_GL_SetAttribute returned %d\n", ret);
        return ret;
    }
    return -1;
}

SDL_Window* SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags) {
    fprintf(stderr, "[SDL2] SDL_CreateWindow(\"%s\", %d, %d, %d, %d, 0x%x)\n", 
            title ? title : "NULL", x, y, w, h, flags);
    init_libs();
    if (p_SDL_CreateWindow) {
        fprintf(stderr, "[SDL2] Calling p_SDL_CreateWindow...\n");
        fflush(stderr);
        SDL_Window* result = p_SDL_CreateWindow(title, x, y, w, h, flags);
        fprintf(stderr, "[SDL2] p_SDL_CreateWindow returned %p\n", (void*)result);
        return result;
    }
    snprintf(error_msg, sizeof(error_msg), "SDL_CreateWindow not available");
    return NULL;
}

void SDL_DestroyWindow(SDL_Window *window) {
    if (p_SDL_DestroyWindow) p_SDL_DestroyWindow(window);
}

SDL_GLContext SDL_GL_CreateContext(SDL_Window *window) {
    if (p_SDL_GL_CreateContext) return p_SDL_GL_CreateContext(window);
    return NULL;
}

void SDL_GL_DeleteContext(SDL_GLContext context) {
    if (p_SDL_GL_DeleteContext) p_SDL_GL_DeleteContext(context);
}

int SDL_GL_MakeCurrent(SDL_Window *window, SDL_GLContext context) {
    if (p_SDL_GL_MakeCurrent) return p_SDL_GL_MakeCurrent(window, context);
    return -1;
}

int SDL_GL_SetSwapInterval(int interval) {
    if (p_SDL_GL_SetSwapInterval) return p_SDL_GL_SetSwapInterval(interval);
    return -1;
}

void SDL_GL_SwapWindow(SDL_Window *window) {
    if (p_SDL_GL_SwapWindow) p_SDL_GL_SwapWindow(window);
}

int SDL_PollEvent(SDL_Event *event) {
    if (p_SDL_PollEvent) return p_SDL_PollEvent(event);
    return 0;
}

Uint32 SDL_GetTicks(void) {
    if (p_SDL_GetTicks) return p_SDL_GetTicks();
    return 0;
}

/* OpenGL wrapper functions */
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    if (p_glViewport) p_glViewport(x, y, width, height);
}

void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
    if (p_glClearColor) p_glClearColor(red, green, blue, alpha);
}

void glClear(GLbitfield mask) {
    if (p_glClear) p_glClear(mask);
}

SDL_bool SDL_SetHint(const char *name, const char *value) {
    fprintf(stderr, "[SDL2] SDL_SetHint(%s, %s) called\n", name ? name : "NULL", value ? value : "NULL");
    init_libs();
    fprintf(stderr, "[SDL2] init_libs() returned, p_SDL_SetHint=%p\n", (void*)p_SDL_SetHint);
    if (p_SDL_SetHint) {
        fprintf(stderr, "[SDL2] About to call p_SDL_SetHint...\n");
        fflush(stderr);
        SDL_bool result = p_SDL_SetHint(name, value);
        fprintf(stderr, "[SDL2] p_SDL_SetHint returned %d\n", result);
        return result;
    }
    fprintf(stderr, "[SDL2] SDL_SetHint not loaded, returning false\n");
    return SDL_FALSE;
}

Uint32 SDL_GetWindowFlags(SDL_Window *window) {
    if (p_SDL_GetWindowFlags) return p_SDL_GetWindowFlags(window);
    return 0;
}

int SDL_SetWindowFullscreen(SDL_Window *window, Uint32 flags) {
    if (p_SDL_SetWindowFullscreen) return p_SDL_SetWindowFullscreen(window, flags);
    return -1;
}
