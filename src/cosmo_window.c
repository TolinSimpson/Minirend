/*
 * cosmo_window.c - Minimal windowing for Cosmopolitan
 * 
 * Uses cosmo_dlopen to dynamically load platform-specific windowing
 * APIs at runtime, enabling a single APE binary to run on any OS.
 */

#include "cosmo_window.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cosmo.h>
#include <dlfcn.h>

/* Error message buffer */
static char error_msg[512] = "";

/* Platform detection */
static int platform_detected = 0;
static int platform_type = 0;  /* 0=unknown, 1=windows, 2=linux, 3=macos */

static void detect_platform(void) {
    if (platform_detected) return;
    platform_detected = 1;
    
    if (IsWindows()) {
        platform_type = 1;
        fprintf(stderr, "[CosmoWindow] Platform: Windows\n");
    } else if (IsXnu()) {
        platform_type = 3;
        fprintf(stderr, "[CosmoWindow] Platform: macOS\n");
    } else {
        platform_type = 2;
        fprintf(stderr, "[CosmoWindow] Platform: Linux\n");
    }
}

/*
 * ============================================================================
 * WINDOWS IMPLEMENTATION
 * ============================================================================
 */
#pragma region Windows

/* Win32 types (defined here to avoid windows.h dependency) */
typedef void* HWND;
typedef void* HDC;
typedef void* HGLRC;
typedef void* HINSTANCE;
typedef void* HICON;
typedef void* HCURSOR;
typedef void* HBRUSH;
typedef void* HMENU;
typedef unsigned int UINT;
typedef long LONG;
typedef unsigned long DWORD;
typedef int BOOL;
typedef long long LONGLONG;
typedef unsigned short WORD;
typedef unsigned char BYTE;
typedef intptr_t WPARAM;
typedef intptr_t LPARAM;
typedef intptr_t LRESULT;
typedef const char* LPCSTR;
typedef void* LPVOID;
typedef LRESULT (*WNDPROC)(HWND, UINT, WPARAM, LPARAM);

typedef struct { LONG x, y; } POINT;
typedef struct { LONG left, top, right, bottom; } RECT;
typedef struct {
    UINT style; WNDPROC lpfnWndProc; int cbClsExtra; int cbWndExtra;
    HINSTANCE hInstance; HICON hIcon; HCURSOR hCursor; HBRUSH hbrBackground;
    LPCSTR lpszMenuName; LPCSTR lpszClassName;
} WNDCLASSA;
typedef struct {
    HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam;
    DWORD time; POINT pt;
} MSG;
typedef struct {
    WORD nSize; WORD nVersion; DWORD dwFlags; BYTE iPixelType;
    BYTE cColorBits; BYTE cRedBits; BYTE cRedShift; BYTE cGreenBits;
    BYTE cGreenShift; BYTE cBlueBits; BYTE cBlueShift; BYTE cAlphaBits;
    BYTE cAlphaShift; BYTE cAccumBits; BYTE cAccumRedBits; BYTE cAccumGreenBits;
    BYTE cAccumBlueBits; BYTE cAccumAlphaBits; BYTE cDepthBits; BYTE cStencilBits;
    BYTE cAuxBuffers; BYTE iLayerType; BYTE bReserved; DWORD dwLayerMask;
    DWORD dwVisibleMask; DWORD dwDamageMask;
} PIXELFORMATDESCRIPTOR;

/* Win32 constants */
#define WS_OVERLAPPEDWINDOW 0x00CF0000
#define WS_VISIBLE          0x10000000
#define WS_POPUP            0x80000000
#define WS_THICKFRAME       0x00040000
#define CW_USEDEFAULT       0x80000000
#define PM_REMOVE           0x0001
#define WM_QUIT             0x0012
#define WM_CLOSE            0x0010
#define WM_SIZE             0x0005
#define WM_KEYDOWN          0x0100
#define WM_KEYUP            0x0101
#define CS_OWNDC            0x0020
#define PFD_DRAW_TO_WINDOW  0x00000004
#define PFD_SUPPORT_OPENGL  0x00000020
#define PFD_DOUBLEBUFFER    0x00000001
#define PFD_TYPE_RGBA       0

/* Win32 function pointers */
static void* user32_lib = NULL;
static void* gdi32_lib = NULL;
static void* opengl32_lib = NULL;

static HWND (*p_CreateWindowExA)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID) = NULL;
static BOOL (*p_DestroyWindow)(HWND) = NULL;
static BOOL (*p_ShowWindow)(HWND, int) = NULL;
static BOOL (*p_UpdateWindow)(HWND) = NULL;
static BOOL (*p_PeekMessageA)(MSG*, HWND, UINT, UINT, UINT) = NULL;
static BOOL (*p_TranslateMessage)(const MSG*) = NULL;
static LRESULT (*p_DispatchMessageA)(const MSG*) = NULL;
static LRESULT (*p_DefWindowProcA)(HWND, UINT, WPARAM, LPARAM) = NULL;
static WORD (*p_RegisterClassA)(const WNDCLASSA*) = NULL;
static HDC (*p_GetDC)(HWND) = NULL;
static int (*p_ReleaseDC)(HWND, HDC) = NULL;
static BOOL (*p_SwapBuffers)(HDC) = NULL;
static int (*p_ChoosePixelFormat)(HDC, const PIXELFORMATDESCRIPTOR*) = NULL;
static BOOL (*p_SetPixelFormat)(HDC, int, const PIXELFORMATDESCRIPTOR*) = NULL;
static HGLRC (*p_wglCreateContext)(HDC) = NULL;
static BOOL (*p_wglMakeCurrent)(HDC, HGLRC) = NULL;
static BOOL (*p_wglDeleteContext)(HGLRC) = NULL;
static HINSTANCE (*p_GetModuleHandleA)(LPCSTR) = NULL;
static HCURSOR (*p_LoadCursorA)(HINSTANCE, LPCSTR) = NULL;

static LRESULT win32_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (p_DefWindowProcA) {
        return p_DefWindowProcA(hwnd, msg, wp, lp);
    }
    return 0;
}

static int load_win32_libs(void) {
    fprintf(stderr, "[CosmoWindow] Loading Win32 libraries...\n");
    
    user32_lib = cosmo_dlopen("user32.dll", RTLD_LAZY);
    gdi32_lib = cosmo_dlopen("gdi32.dll", RTLD_LAZY);
    opengl32_lib = cosmo_dlopen("opengl32.dll", RTLD_LAZY);
    
    if (!user32_lib || !gdi32_lib) {
        snprintf(error_msg, sizeof(error_msg), "Failed to load Win32 libraries");
        return -1;
    }
    
    /* Load user32 functions */
    p_CreateWindowExA = cosmo_dlsym(user32_lib, "CreateWindowExA");
    p_DestroyWindow = cosmo_dlsym(user32_lib, "DestroyWindow");
    p_ShowWindow = cosmo_dlsym(user32_lib, "ShowWindow");
    p_UpdateWindow = cosmo_dlsym(user32_lib, "UpdateWindow");
    p_PeekMessageA = cosmo_dlsym(user32_lib, "PeekMessageA");
    p_TranslateMessage = cosmo_dlsym(user32_lib, "TranslateMessage");
    p_DispatchMessageA = cosmo_dlsym(user32_lib, "DispatchMessageA");
    p_DefWindowProcA = cosmo_dlsym(user32_lib, "DefWindowProcA");
    p_RegisterClassA = cosmo_dlsym(user32_lib, "RegisterClassA");
    p_GetDC = cosmo_dlsym(user32_lib, "GetDC");
    p_ReleaseDC = cosmo_dlsym(user32_lib, "ReleaseDC");
    p_LoadCursorA = cosmo_dlsym(user32_lib, "LoadCursorA");
    
    /* Load kernel32 function */
    void* kernel32 = cosmo_dlopen("kernel32.dll", RTLD_LAZY);
    if (kernel32) {
        p_GetModuleHandleA = cosmo_dlsym(kernel32, "GetModuleHandleA");
    }
    
    /* Load gdi32 functions */
    p_SwapBuffers = cosmo_dlsym(gdi32_lib, "SwapBuffers");
    p_ChoosePixelFormat = cosmo_dlsym(gdi32_lib, "ChoosePixelFormat");
    p_SetPixelFormat = cosmo_dlsym(gdi32_lib, "SetPixelFormat");
    
    /* Load opengl32 functions */
    if (opengl32_lib) {
        p_wglCreateContext = cosmo_dlsym(opengl32_lib, "wglCreateContext");
        p_wglMakeCurrent = cosmo_dlsym(opengl32_lib, "wglMakeCurrent");
        p_wglDeleteContext = cosmo_dlsym(opengl32_lib, "wglDeleteContext");
    }
    
    if (!p_CreateWindowExA || !p_DestroyWindow || !p_PeekMessageA) {
        snprintf(error_msg, sizeof(error_msg), "Failed to load required Win32 functions");
        return -1;
    }
    
    fprintf(stderr, "[CosmoWindow] Win32 libraries loaded successfully\n");
    return 0;
}

#pragma endregion

/*
 * ============================================================================
 * WINDOW STRUCTURE
 * ============================================================================
 */

struct CosmoWindow {
    int width;
    int height;
    char title[256];
    CosmoWindowMode mode;
    bool running;
    uint32_t *framebuffer;
    
    /* Platform-specific handles */
    union {
        struct {
            HWND hwnd;
            HDC hdc;
            HGLRC hglrc;
        } win32;
        struct {
            void *display;
            unsigned long window;
            void *glx_context;
        } x11;
    };
};

/*
 * ============================================================================
 * PUBLIC API
 * ============================================================================
 */

CosmoWindow* cosmo_window_create(const CosmoWindowConfig *config) {
    CosmoWindow *window;
    
    detect_platform();
    
    window = calloc(1, sizeof(CosmoWindow));
    if (!window) {
        snprintf(error_msg, sizeof(error_msg), "Failed to allocate window");
        return NULL;
    }
    
    window->width = config->width > 0 ? config->width : 800;
    window->height = config->height > 0 ? config->height : 600;
    strncpy(window->title, config->title ? config->title : "Minrend", sizeof(window->title) - 1);
    window->mode = config->mode;
    window->running = true;
    
    /* Allocate framebuffer for software rendering fallback */
    window->framebuffer = calloc(window->width * window->height, sizeof(uint32_t));
    
    if (platform_type == 1) {
        /* Windows */
        if (load_win32_libs() != 0) {
            free(window->framebuffer);
            free(window);
            return NULL;
        }
        
        /* Register window class */
        HINSTANCE hinst = p_GetModuleHandleA ? p_GetModuleHandleA(NULL) : NULL;
        
        WNDCLASSA wc = {0};
        wc.style = CS_OWNDC;
        wc.lpfnWndProc = win32_wndproc;
        wc.hInstance = hinst;
        wc.hCursor = p_LoadCursorA ? p_LoadCursorA(NULL, (LPCSTR)32512) : NULL;  /* IDC_ARROW */
        wc.lpszClassName = "CosmoWindowClass";
        
        if (p_RegisterClassA) {
            p_RegisterClassA(&wc);
        }
        
        /* Create window */
        DWORD style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
        if (config->mode == COSMO_WINDOW_BORDERLESS) {
            style = WS_POPUP | WS_VISIBLE;
        }
        
        fprintf(stderr, "[CosmoWindow] Creating Win32 window %dx%d...\n", window->width, window->height);
        
        window->win32.hwnd = p_CreateWindowExA(
            0,
            "CosmoWindowClass",
            window->title,
            style,
            CW_USEDEFAULT, CW_USEDEFAULT,
            window->width, window->height,
            NULL, NULL, hinst, NULL
        );
        
        if (!window->win32.hwnd) {
            snprintf(error_msg, sizeof(error_msg), "CreateWindowExA failed");
            free(window->framebuffer);
            free(window);
            return NULL;
        }
        
        fprintf(stderr, "[CosmoWindow] Window created: %p\n", window->win32.hwnd);
        
        /* Get DC and set up OpenGL */
        window->win32.hdc = p_GetDC(window->win32.hwnd);
        
        if (opengl32_lib && p_ChoosePixelFormat && p_SetPixelFormat && p_wglCreateContext) {
            PIXELFORMATDESCRIPTOR pfd = {0};
            pfd.nSize = sizeof(pfd);
            pfd.nVersion = 1;
            pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
            pfd.iPixelType = PFD_TYPE_RGBA;
            pfd.cColorBits = 32;
            pfd.cDepthBits = 24;
            
            int pf = p_ChoosePixelFormat(window->win32.hdc, &pfd);
            if (pf) {
                p_SetPixelFormat(window->win32.hdc, pf, &pfd);
                window->win32.hglrc = p_wglCreateContext(window->win32.hdc);
                if (window->win32.hglrc) {
                    p_wglMakeCurrent(window->win32.hdc, window->win32.hglrc);
                    fprintf(stderr, "[CosmoWindow] OpenGL context created\n");
                }
            }
        }
        
        if (p_ShowWindow) p_ShowWindow(window->win32.hwnd, 1);
        if (p_UpdateWindow) p_UpdateWindow(window->win32.hwnd);
        
    } else if (platform_type == 2) {
        /* Linux/X11 - TODO */
        snprintf(error_msg, sizeof(error_msg), "X11 support not yet implemented");
        free(window->framebuffer);
        free(window);
        return NULL;
    } else if (platform_type == 3) {
        /* macOS - TODO */
        snprintf(error_msg, sizeof(error_msg), "macOS support not yet implemented");
        free(window->framebuffer);
        free(window);
        return NULL;
    } else {
        snprintf(error_msg, sizeof(error_msg), "Unknown platform");
        free(window->framebuffer);
        free(window);
        return NULL;
    }
    
    return window;
}

void cosmo_window_destroy(CosmoWindow *window) {
    if (!window) return;
    
    if (platform_type == 1) {
        if (window->win32.hglrc && p_wglDeleteContext) {
            p_wglMakeCurrent(NULL, NULL);
            p_wglDeleteContext(window->win32.hglrc);
        }
        if (window->win32.hdc && p_ReleaseDC) {
            p_ReleaseDC(window->win32.hwnd, window->win32.hdc);
        }
        if (window->win32.hwnd && p_DestroyWindow) {
            p_DestroyWindow(window->win32.hwnd);
        }
    }
    
    free(window->framebuffer);
    free(window);
}

bool cosmo_window_poll_event(CosmoWindow *window, CosmoEvent *event) {
    if (!window || !event) return false;
    
    event->type = COSMO_EVENT_NONE;
    
    if (platform_type == 1 && p_PeekMessageA) {
        MSG msg;
        if (p_PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                event->type = COSMO_EVENT_QUIT;
                window->running = false;
                return true;
            }
            
            if (p_TranslateMessage) p_TranslateMessage(&msg);
            if (p_DispatchMessageA) p_DispatchMessageA(&msg);
            
            if (msg.message == WM_CLOSE) {
                event->type = COSMO_EVENT_QUIT;
                window->running = false;
                return true;
            }
            if (msg.message == WM_KEYDOWN) {
                event->type = COSMO_EVENT_KEY_DOWN;
                event->key.key = (int)msg.wParam;
                return true;
            }
            if (msg.message == WM_SIZE) {
                event->type = COSMO_EVENT_RESIZE;
                event->resize.width = msg.lParam & 0xFFFF;
                event->resize.height = (msg.lParam >> 16) & 0xFFFF;
                window->width = event->resize.width;
                window->height = event->resize.height;
                return true;
            }
        }
    }
    
    return event->type != COSMO_EVENT_NONE;
}

void cosmo_window_swap_buffers(CosmoWindow *window) {
    if (!window) return;
    
    if (platform_type == 1 && window->win32.hdc && p_SwapBuffers) {
        p_SwapBuffers(window->win32.hdc);
    }
}

bool cosmo_window_make_gl_current(CosmoWindow *window) {
    if (!window) return false;
    
    if (platform_type == 1 && window->win32.hglrc && p_wglMakeCurrent) {
        return p_wglMakeCurrent(window->win32.hdc, window->win32.hglrc) != 0;
    }
    
    return false;
}

void cosmo_window_get_size(CosmoWindow *window, int *width, int *height) {
    if (!window) return;
    if (width) *width = window->width;
    if (height) *height = window->height;
}

void cosmo_window_set_fullscreen(CosmoWindow *window, bool fullscreen) {
    /* TODO: Implement fullscreen toggle */
    (void)window;
    (void)fullscreen;
}

const char* cosmo_window_get_error(void) {
    return error_msg;
}

uint32_t* cosmo_window_get_framebuffer(CosmoWindow *window) {
    return window ? window->framebuffer : NULL;
}

void cosmo_window_present_framebuffer(CosmoWindow *window) {
    /* TODO: Blit framebuffer to window */
    (void)window;
}

