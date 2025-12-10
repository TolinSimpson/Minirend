/*
 * X11/Xutil.h shim for Cosmopolitan
 */

#ifndef _X11_XUTIL_H_SHIM
#define _X11_XUTIL_H_SHIM

#include "Xlib.h"

/* Visual classes */
#define StaticGray  0
#define GrayScale   1
#define StaticColor 2
#define PseudoColor 3
#define TrueColor   4
#define DirectColor 5

/* Size hints */
#define USPosition  (1L<<0)
#define USSize      (1L<<1)
#define PPosition   (1L<<2)
#define PSize       (1L<<3)
#define PMinSize    (1L<<4)
#define PMaxSize    (1L<<5)
#define PResizeInc  (1L<<6)
#define PAspect     (1L<<7)
#define PBaseSize   (1L<<8)
#define PWinGravity (1L<<9)

/* Window manager hints */
#define InputHint        (1L<<0)
#define StateHint        (1L<<1)
#define IconPixmapHint   (1L<<2)
#define IconWindowHint   (1L<<3)
#define IconPositionHint (1L<<4)
#define IconMaskHint     (1L<<5)
#define WindowGroupHint  (1L<<6)
#define AllHints         (InputHint|StateHint|IconPixmapHint|IconWindowHint|IconPositionHint|IconMaskHint|WindowGroupHint)

/* Initial window states */
#define WithdrawnState 0
#define NormalState    1
#define IconicState    3

typedef struct {
    long flags;
    int x, y;
    int width, height;
    int min_width, min_height;
    int max_width, max_height;
    int width_inc, height_inc;
    struct {
        int x;
        int y;
    } min_aspect, max_aspect;
    int base_width, base_height;
    int win_gravity;
} XSizeHints;

typedef struct {
    long flags;
    Bool input;
    int initial_state;
    Pixmap icon_pixmap;
    Window icon_window;
    int icon_x, icon_y;
    Pixmap icon_mask;
    XID window_group;
} XWMHints;

typedef struct {
    char *res_name;
    char *res_class;
} XClassHint;

typedef struct {
    long flags;
    long functions;
    long decorations;
    long input_mode;
    long status;
} MotifWmHints;

#define MWM_HINTS_FUNCTIONS   (1L<<0)
#define MWM_HINTS_DECORATIONS (1L<<1)
#define MWM_FUNC_ALL          (1L<<0)
#define MWM_FUNC_RESIZE       (1L<<1)
#define MWM_FUNC_MOVE         (1L<<2)
#define MWM_FUNC_MINIMIZE     (1L<<3)
#define MWM_FUNC_MAXIMIZE     (1L<<4)
#define MWM_FUNC_CLOSE        (1L<<5)
#define MWM_DECOR_ALL         (1L<<0)
#define MWM_DECOR_BORDER      (1L<<1)
#define MWM_DECOR_RESIZEH     (1L<<2)
#define MWM_DECOR_TITLE       (1L<<3)
#define MWM_DECOR_MENU        (1L<<4)
#define MWM_DECOR_MINIMIZE    (1L<<5)
#define MWM_DECOR_MAXIMIZE    (1L<<6)

/* Visual info masks */
#define VisualNoMask           0x0
#define VisualIDMask           0x1
#define VisualScreenMask       0x2
#define VisualDepthMask        0x4
#define VisualClassMask        0x8
#define VisualRedMaskMask      0x10
#define VisualGreenMaskMask    0x20
#define VisualBlueMaskMask     0x40
#define VisualColormapSizeMask 0x80
#define VisualBitsPerRGBMask   0x100
#define VisualAllMask          0x1FF

#endif /* _X11_XUTIL_H_SHIM */

