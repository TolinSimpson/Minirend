/*
 * X11/Xlib.h shim for Cosmopolitan
 * 
 * Provides X11 type definitions needed by sokol.
 * Actual X11 functions are loaded dynamically via dlopen at runtime.
 */

#ifndef _X11_XLIB_H_SHIM
#define _X11_XLIB_H_SHIM

#include <stdint.h>
#include <stddef.h>

/* Basic X11 types */
typedef unsigned long XID;
typedef XID Window;
typedef XID Drawable;
typedef XID Pixmap;
typedef XID Cursor;
typedef XID Colormap;
typedef XID GContext;
typedef XID KeySym;
typedef XID Atom;
typedef unsigned long VisualID;
typedef unsigned long Time;
typedef unsigned char KeyCode;

typedef struct _XDisplay Display;
typedef struct _XVisual Visual;
typedef struct _XScreen Screen;
typedef struct _XGC *GC;
typedef struct _XIC *XIC;
typedef struct _XIM *XIM;
typedef struct _XrmHashBucketRec *XrmDatabase;

typedef int Bool;
typedef int Status;
typedef char* XPointer;

/* Error handler and compose status (needed by x11_stub.c) */
/* NOTE: XErrorEvent is defined further below in this shim. We don't need an
 * exact signature here; x11_stub.c only stores and forwards the function ptr.
 */
typedef int (*XErrorHandler)(Display* display, void* error_event);

typedef struct {
    XPointer compose_ptr;
    int chars_matched;
} XComposeStatus;

#ifndef True
#define True 1
#endif
#ifndef False
#define False 0
#endif
#ifndef None
#define None 0L
#endif

/* Success/Failure return codes */
#define Success 0
#define BadRequest 1
#define BadValue 2
#define BadWindow 3
#define BadPixmap 4
#define BadAtom 5
#define BadCursor 6
#define BadFont 7
#define BadMatch 8
#define BadDrawable 9
#define BadAccess 10
#define BadAlloc 11
#define BadColor 12
#define BadGC 13
#define BadIDChoice 14
#define BadName 15
#define BadLength 16
#define BadImplementation 17

/* Time constants */
#define CurrentTime 0L

/* Window gravity */
#define ForgetGravity    0
#define NorthWestGravity 1
#define NorthGravity     2
#define NorthEastGravity 3
#define WestGravity      4
#define CenterGravity    5
#define EastGravity      6
#define SouthWestGravity 7
#define SouthGravity     8
#define SouthEastGravity 9
#define StaticGravity    10

/* Map states */
#define IsUnmapped   0
#define IsUnviewable 1
#define IsViewable   2

/* Property change states */
#define PropertyNewValue 0
#define PropertyDelete   1

/* Event types */
#define KeyPress         2
#define KeyRelease       3
#define ButtonPress      4
#define ButtonRelease    5
#define MotionNotify     6
#define EnterNotify      7
#define LeaveNotify      8
#define FocusIn          9
#define FocusOut         10
#define KeymapNotify     11
#define Expose           12
#define GraphicsExpose   13
#define NoExpose         14
#define VisibilityNotify 15
#define CreateNotify     16
#define DestroyNotify    17
#define UnmapNotify      18
#define MapNotify        19
#define MapRequest       20
#define ReparentNotify   21
#define ConfigureNotify  22
#define ConfigureRequest 23
#define GravityNotify    24
#define ResizeRequest    25
#define CirculateNotify  26
#define CirculateRequest 27
#define PropertyNotify   28
#define SelectionClear   29
#define SelectionRequest 30
#define SelectionNotify  31
#define ColormapNotify   32
#define ClientMessage    33
#define MappingNotify    34
#define GenericEvent     35
#define LASTEvent        36

/* Event masks */
#define NoEventMask              0L
#define KeyPressMask             (1L<<0)
#define KeyReleaseMask           (1L<<1)
#define ButtonPressMask          (1L<<2)
#define ButtonReleaseMask        (1L<<3)
#define EnterWindowMask          (1L<<4)
#define LeaveWindowMask          (1L<<5)
#define PointerMotionMask        (1L<<6)
#define PointerMotionHintMask    (1L<<7)
#define Button1MotionMask        (1L<<8)
#define Button2MotionMask        (1L<<9)
#define Button3MotionMask        (1L<<10)
#define Button4MotionMask        (1L<<11)
#define Button5MotionMask        (1L<<12)
#define ButtonMotionMask         (1L<<13)
#define KeymapStateMask          (1L<<14)
#define ExposureMask             (1L<<15)
#define VisibilityChangeMask     (1L<<16)
#define StructureNotifyMask      (1L<<17)
#define ResizeRedirectMask       (1L<<18)
#define SubstructureNotifyMask   (1L<<19)
#define SubstructureRedirectMask (1L<<20)
#define FocusChangeMask          (1L<<21)
#define PropertyChangeMask       (1L<<22)
#define ColormapChangeMask       (1L<<23)
#define OwnerGrabButtonMask      (1L<<24)

/* Window attributes */
#define CWBackPixmap       (1L<<0)
#define CWBackPixel        (1L<<1)
#define CWBorderPixmap     (1L<<2)
#define CWBorderPixel      (1L<<3)
#define CWBitGravity       (1L<<4)
#define CWWinGravity       (1L<<5)
#define CWBackingStore     (1L<<6)
#define CWBackingPlanes    (1L<<7)
#define CWBackingPixel     (1L<<8)
#define CWOverrideRedirect (1L<<9)
#define CWSaveUnder        (1L<<10)
#define CWEventMask        (1L<<11)
#define CWDontPropagate    (1L<<12)
#define CWColormap         (1L<<13)
#define CWCursor           (1L<<14)

/* Input modes */
#define AllocNone   0
#define AllocAll    1
#define InputOutput 1
#define InputOnly   2

/* Property modes */
#define PropModeReplace 0
#define PropModePrepend 1
#define PropModeAppend  2

/* Atom values */
#define XA_PRIMARY       1
#define XA_SECONDARY     2
#define XA_ATOM          4
#define XA_STRING        31
#define XA_CARDINAL      6
#define XA_WM_NAME       39
#define XA_WM_ICON_NAME  37

/* Key masks */
#define ShiftMask   (1<<0)
#define LockMask    (1<<1)
#define ControlMask (1<<2)
#define Mod1Mask    (1<<3)
#define Mod2Mask    (1<<4)
#define Mod3Mask    (1<<5)
#define Mod4Mask    (1<<6)
#define Mod5Mask    (1<<7)

/* Button masks */
#define Button1Mask (1<<8)
#define Button2Mask (1<<9)
#define Button3Mask (1<<10)
#define Button4Mask (1<<11)
#define Button5Mask (1<<12)

/* Button names */
#define Button1 1
#define Button2 2
#define Button3 3
#define Button4 4
#define Button5 5

/* Grab modes */
#define GrabModeSync  0
#define GrabModeAsync 1

/* Notify modes */
#define NotifyNormal       0
#define NotifyGrab         1
#define NotifyUngrab       2
#define NotifyWhileGrabbed 3

/* Notify detail */
#define NotifyAncestor         0
#define NotifyVirtual          1
#define NotifyInferior         2
#define NotifyNonlinear        3
#define NotifyNonlinearVirtual 4
#define NotifyPointer          5
#define NotifyPointerRoot      6
#define NotifyDetailNone       7

/* Visibility */
#define VisibilityUnobscured        0
#define VisibilityPartiallyObscured 1
#define VisibilityFullyObscured     2

/* Focus revert modes */
#define RevertToNone        0
#define RevertToPointerRoot 1
#define RevertToParent      2

/* Cursor shapes */
#define XC_X_cursor            0
#define XC_arrow               2
#define XC_based_arrow_down    4
#define XC_based_arrow_up      6
#define XC_crosshair           34
#define XC_hand1               58
#define XC_hand2               60
#define XC_left_ptr            68
#define XC_sb_h_double_arrow   108
#define XC_sb_v_double_arrow   116
#define XC_xterm               152

/* Structures */
typedef struct {
    int x, y;
    int width, height;
    int border_width;
    int depth;
    Visual *visual;
    Window root;
    int class;
    int bit_gravity;
    int win_gravity;
    int backing_store;
    unsigned long backing_planes;
    unsigned long backing_pixel;
    Bool save_under;
    Colormap colormap;
    Bool map_installed;
    int map_state;
    long all_event_masks;
    long your_event_mask;
    long do_not_propagate_mask;
    Bool override_redirect;
    Screen *screen;
} XWindowAttributes;

typedef struct {
    Pixmap background_pixmap;
    unsigned long background_pixel;
    Pixmap border_pixmap;
    unsigned long border_pixel;
    int bit_gravity;
    int win_gravity;
    int backing_store;
    unsigned long backing_planes;
    unsigned long backing_pixel;
    Bool save_under;
    long event_mask;
    long do_not_propagate_mask;
    Bool override_redirect;
    Colormap colormap;
    Cursor cursor;
} XSetWindowAttributes;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window window;
} XAnyEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window window;
    Window root;
    Window subwindow;
    Time time;
    int x, y;
    int x_root, y_root;
    unsigned int state;
    unsigned int keycode;
    Bool same_screen;
} XKeyEvent;

typedef XKeyEvent XKeyPressedEvent;
typedef XKeyEvent XKeyReleasedEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window window;
    Window root;
    Window subwindow;
    Time time;
    int x, y;
    int x_root, y_root;
    unsigned int state;
    unsigned int button;
    Bool same_screen;
} XButtonEvent;

typedef XButtonEvent XButtonPressedEvent;
typedef XButtonEvent XButtonReleasedEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window window;
    Window root;
    Window subwindow;
    Time time;
    int x, y;
    int x_root, y_root;
    unsigned int state;
    char is_hint;
    Bool same_screen;
} XMotionEvent;

typedef XMotionEvent XPointerMovedEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window window;
    int x, y;
    int width, height;
    int count;
} XExposeEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window event;
    Window window;
    int x, y;
    int width, height;
    int border_width;
    Window above;
    Bool override_redirect;
} XConfigureEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window window;
    int mode;
    int detail;
} XFocusChangeEvent;

typedef XFocusChangeEvent XFocusInEvent;
typedef XFocusChangeEvent XFocusOutEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window window;
    Atom message_type;
    int format;
    union {
        char b[20];
        short s[10];
        long l[5];
    } data;
} XClientMessageEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window window;
    Atom atom;
    Time time;
    int state;
} XPropertyEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window window;
    Atom selection;
    Time time;
} XSelectionClearEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window owner;
    Window requestor;
    Atom selection;
    Atom target;
    Atom property;
    Time time;
} XSelectionRequestEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window requestor;
    Atom selection;
    Atom target;
    Atom property;
    Time time;
} XSelectionEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window window;
    int state;
} XVisibilityEvent;

typedef struct {
    int type;
    Display *display;
    XID resourceid;
    unsigned long serial;
    unsigned char error_code;
    unsigned char request_code;
    unsigned char minor_code;
} XErrorEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window window;
} XMapEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window event;
    Window window;
    Bool from_configure;
} XUnmapEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window event;
    Window window;
} XDestroyWindowEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window window;
    Window root;
    Window subwindow;
    Time time;
    int x, y;
    int x_root, y_root;
    int mode;
    int detail;
    Bool same_screen;
    Bool focus;
    unsigned int state;
} XCrossingEvent;

typedef XCrossingEvent XEnterWindowEvent;
typedef XCrossingEvent XLeaveWindowEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    int extension;
    int evtype;
} XGenericEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    int extension;
    int evtype;
    unsigned int cookie;
    void *data;
} XGenericEventCookie;

typedef union _XEvent {
    int type;
    XAnyEvent xany;
    XKeyEvent xkey;
    XButtonEvent xbutton;
    XMotionEvent xmotion;
    XCrossingEvent xcrossing;
    XFocusChangeEvent xfocus;
    XExposeEvent xexpose;
    XVisibilityEvent xvisibility;
    XConfigureEvent xconfigure;
    XPropertyEvent xproperty;
    XSelectionClearEvent xselectionclear;
    XSelectionRequestEvent xselectionrequest;
    XSelectionEvent xselection;
    XClientMessageEvent xclient;
    XMapEvent xmap;
    XUnmapEvent xunmap;
    XDestroyWindowEvent xdestroywindow;
    XGenericEvent xgeneric;
    XGenericEventCookie xcookie;
    long pad[24];
} XEvent;

typedef struct {
    Visual *visual;
    VisualID visualid;
    int screen;
    int depth;
    int class;
    unsigned long red_mask;
    unsigned long green_mask;
    unsigned long blue_mask;
    int colormap_size;
    int bits_per_rgb;
} XVisualInfo;

typedef struct {
    Colormap colormap;
    unsigned long red_max;
    unsigned long red_mult;
    unsigned long green_max;
    unsigned long green_mult;
    unsigned long blue_max;
    unsigned long blue_mult;
    unsigned long base_pixel;
    VisualID visualid;
    XID killid;
} XStandardColormap;

typedef struct {
    unsigned char *value;
    Atom encoding;
    int format;
    unsigned long nitems;
} XTextProperty;

typedef struct {
    int max_keypermod;
    KeyCode *modifiermap;
} XModifierKeymap;

/* Function prototypes - these will be loaded via dlopen */
/* Defined in gl_x11_stubs.c */

#endif /* _X11_XLIB_H_SHIM */

