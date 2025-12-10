/*
 * cosmo_window.h - Minimal windowing for Cosmopolitan
 * 
 * A thin windowing abstraction that uses cosmo_dlopen to load
 * platform-specific APIs at runtime, enabling true cross-platform
 * portability in a single APE binary.
 */

#ifndef COSMO_WINDOW_H
#define COSMO_WINDOW_H

#include <stdint.h>
#include <stdbool.h>

/* Window mode */
typedef enum {
    COSMO_WINDOW_WINDOWED = 0,
    COSMO_WINDOW_FULLSCREEN,
    COSMO_WINDOW_BORDERLESS
} CosmoWindowMode;

/* Window configuration */
typedef struct {
    const char *title;
    int width;
    int height;
    CosmoWindowMode mode;
    bool vsync;
    bool resizable;
} CosmoWindowConfig;

/* Event types */
typedef enum {
    COSMO_EVENT_NONE = 0,
    COSMO_EVENT_QUIT,
    COSMO_EVENT_RESIZE,
    COSMO_EVENT_KEY_DOWN,
    COSMO_EVENT_KEY_UP,
    COSMO_EVENT_MOUSE_MOVE,
    COSMO_EVENT_MOUSE_DOWN,
    COSMO_EVENT_MOUSE_UP
} CosmoEventType;

/* Key codes (subset) */
typedef enum {
    COSMO_KEY_UNKNOWN = 0,
    COSMO_KEY_ESCAPE = 27,
    COSMO_KEY_SPACE = 32,
    COSMO_KEY_F1 = 256,
    COSMO_KEY_F11 = 266,
    COSMO_KEY_F12 = 267,
    COSMO_KEY_LEFT = 300,
    COSMO_KEY_RIGHT,
    COSMO_KEY_UP,
    COSMO_KEY_DOWN
} CosmoKeyCode;

/* Event structure */
typedef struct {
    CosmoEventType type;
    union {
        struct { int width, height; } resize;
        struct { int key; int mods; } key;
        struct { int x, y, button; } mouse;
    };
} CosmoEvent;

/* Opaque window handle */
typedef struct CosmoWindow CosmoWindow;

/* API */
CosmoWindow* cosmo_window_create(const CosmoWindowConfig *config);
void cosmo_window_destroy(CosmoWindow *window);
bool cosmo_window_poll_event(CosmoWindow *window, CosmoEvent *event);
void cosmo_window_swap_buffers(CosmoWindow *window);
bool cosmo_window_make_gl_current(CosmoWindow *window);
void cosmo_window_get_size(CosmoWindow *window, int *width, int *height);
void cosmo_window_set_fullscreen(CosmoWindow *window, bool fullscreen);
const char* cosmo_window_get_error(void);

/* Framebuffer mode (software rendering fallback) */
uint32_t* cosmo_window_get_framebuffer(CosmoWindow *window);
void cosmo_window_present_framebuffer(CosmoWindow *window);

#endif /* COSMO_WINDOW_H */

