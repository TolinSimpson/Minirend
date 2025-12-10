/*
 * X11/Xcursor/Xcursor.h shim for Cosmopolitan
 */

#ifndef _X11_XCURSOR_H_SHIM
#define _X11_XCURSOR_H_SHIM

#include "../Xlib.h"
#include <stdint.h>

/* Xcursor types */
typedef uint32_t XcursorPixel;
typedef unsigned int XcursorDim;
typedef unsigned int XcursorBool;
typedef unsigned int XcursorUInt;

#define XcursorTrue  1
#define XcursorFalse 0

typedef struct _XcursorImage {
    XcursorUInt version;
    XcursorDim size;
    XcursorDim width;
    XcursorDim height;
    XcursorDim xhot;
    XcursorDim yhot;
    XcursorUInt delay;
    XcursorPixel *pixels;
} XcursorImage;

typedef struct _XcursorImages {
    int nimage;
    XcursorImage **images;
    char *name;
} XcursorImages;

typedef struct _XcursorCursors {
    Display *dpy;
    int ref;
    int ncursor;
    Cursor *cursors;
} XcursorCursors;

typedef struct _XcursorAnimate {
    XcursorCursors *cursors;
    int sequence;
} XcursorAnimate;

#endif /* _X11_XCURSOR_H_SHIM */

