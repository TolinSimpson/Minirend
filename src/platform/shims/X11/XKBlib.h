/*
 * X11/XKBlib.h shim for Cosmopolitan
 */

#ifndef _X11_XKBLIB_H_SHIM
#define _X11_XKBLIB_H_SHIM

#include "Xlib.h"

#define XkbUseCoreKbd 0x0100

/* XKB component masks */
#define XkbKeyNamesMask     (1<<9)
#define XkbKeyAliasesMask   (1<<10)
#define XkbKeyTypesMask     (1<<0)
#define XkbKeySymsMask      (1<<1)
#define XkbModifierMapMask  (1<<2)
#define XkbExplicitMask     (1<<3)
#define XkbKeyActionsMask   (1<<4)
#define XkbKeyBehaviorsMask (1<<5)
#define XkbVirtualModsMask  (1<<6)
#define XkbVirtualModMapMask (1<<7)

/* XKB key name length */
#define XkbKeyNameLength 4

/* XKB structures */
typedef struct _XkbNamesRec {
    Atom keycodes;
    Atom geometry;
    Atom symbols;
    Atom types;
    Atom compat;
    Atom vmods[16];
    Atom indicators[32];
    Atom groups[4];
    struct {
        char name[XkbKeyNameLength];
    } *keys;
    struct {
        char real[XkbKeyNameLength];
        char alias[XkbKeyNameLength];
    } *key_aliases;
    Atom *radio_groups;
    Atom phys_symbols;
    unsigned char num_keys;
    unsigned char num_key_aliases;
    unsigned short num_rg;
} XkbNamesRec, *XkbNamesPtr;

typedef struct _XkbDescRec {
    void *dpy;
    unsigned short flags;
    unsigned short device_spec;
    KeyCode min_key_code;
    KeyCode max_key_code;
    void *ctrls;
    void *server;
    void *map;
    void *indicators;
    XkbNamesPtr names;
    void *compat;
    void *geom;
} XkbDescRec, *XkbDescPtr;

#endif /* _X11_XKBLIB_H_SHIM */

