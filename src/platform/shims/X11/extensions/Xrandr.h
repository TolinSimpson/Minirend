/*
 * X11/extensions/Xrandr.h shim for Cosmopolitan
 */

#ifndef _X11_EXTENSIONS_XRANDR_H_SHIM
#define _X11_EXTENSIONS_XRANDR_H_SHIM

#include "../Xlib.h"

typedef unsigned long RROutput;
typedef unsigned long RRCrtc;
typedef unsigned long RRMode;

typedef struct {
    int width;
    int height;
} XRRScreenSize;

typedef struct _XRRScreenConfiguration XRRScreenConfiguration;

#endif /* _X11_EXTENSIONS_XRANDR_H_SHIM */

