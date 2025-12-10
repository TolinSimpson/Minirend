/*
 * X11/Xresource.h shim for Cosmopolitan
 */

#ifndef _X11_XRESOURCE_H_SHIM
#define _X11_XRESOURCE_H_SHIM

#include "Xlib.h"

/* XrmDatabase is already defined in Xlib.h */

/* Resource manager string types */
typedef char* XrmString;

/* Quark types */
typedef int XrmQuark;
typedef int* XrmQuarkList;

#define NULLQUARK ((XrmQuark)0)

/* Resource spec/value types */
typedef struct {
    XrmQuark name;
    XrmQuark class;
} XrmBinding;

typedef enum {
    XrmBindTightly,
    XrmBindLoosely
} XrmBindingList;

typedef struct {
    unsigned int size;
    XPointer addr;
} XrmValue, *XrmValuePtr;

/* Database operations */
typedef enum {
    XrmMerge,
    XrmMergeRelative
} XrmOptionKind;

typedef struct {
    char *option;
    char *specifier;
    XrmOptionKind argKind;
    XPointer value;
} XrmOptionDescRec, *XrmOptionDescList;

#endif /* _X11_XRESOURCE_H_SHIM */

