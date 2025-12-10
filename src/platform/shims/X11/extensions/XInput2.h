/*
 * X11/extensions/XInput2.h shim for Cosmopolitan  
 */

#ifndef _X11_EXTENSIONS_XINPUT2_H_SHIM
#define _X11_EXTENSIONS_XINPUT2_H_SHIM

#include "../Xlib.h"

#define XIAllMasterDevices 1
#define XIAllDevices       0
#define XIAllMasterDevices 1

/* Event masks */
#define XI_DeviceChanged        1
#define XI_KeyPress             2
#define XI_KeyRelease           3
#define XI_ButtonPress          4
#define XI_ButtonRelease        5
#define XI_Motion               6
#define XI_Enter                7
#define XI_Leave                8
#define XI_FocusIn              9
#define XI_FocusOut             10
#define XI_HierarchyChanged     11
#define XI_PropertyEvent        12
#define XI_RawKeyPress          13
#define XI_RawKeyRelease        14
#define XI_RawButtonPress       15
#define XI_RawButtonRelease     16
#define XI_RawMotion            17
#define XI_TouchBegin           18
#define XI_TouchUpdate          19
#define XI_TouchEnd             20
#define XI_TouchOwnership       21
#define XI_RawTouchBegin        22
#define XI_RawTouchUpdate       23
#define XI_RawTouchEnd          24
#define XI_BarrierHit           25
#define XI_BarrierLeave         26
#define XI_LASTEVENT            XI_BarrierLeave

/* Mask helper macros */
#define XIMaskLen(event) (((event) >> 3) + 1)
#define XISetMask(ptr, event) (((unsigned char*)(ptr))[(event)>>3] |= (1<<((event) & 7)))
#define XIClearMask(ptr, event) (((unsigned char*)(ptr))[(event)>>3] &= ~(1<<((event) & 7)))
#define XIMaskIsSet(ptr, event) (((unsigned char*)(ptr))[(event)>>3] & (1<<((event) & 7)))

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    int extension;
    int evtype;
    unsigned int cookie;
    void *data;
} XIEvent;

typedef struct {
    int deviceid;
    int mask_len;
    unsigned char *mask;
} XIEventMask;

typedef struct {
    int mask_len;
    unsigned char *mask;
} XIValuatorState;

typedef struct {
    int deviceid;
    int sourceid;
    int detail;
    Window root;
    Window event;
    Window child;
    double root_x;
    double root_y;
    double event_x;
    double event_y;
    int flags;
    void *buttons;
    XIValuatorState valuators;
    void *mods;
    void *group;
} XIDeviceEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    int extension;
    int evtype;
    Time time;
    int deviceid;
    int sourceid;
    int detail;
    int flags;
    XIValuatorState valuators;
    double *raw_values;
} XIRawEvent;

typedef struct {
    int type;
    int sourceid;
} XIAnyClassInfo;

typedef struct {
    int deviceid;
    char *name;
    int use;
    int attachment;
    Bool enabled;
    int num_classes;
    XIAnyClassInfo **classes;
} XIDeviceInfo;

#endif /* _X11_EXTENSIONS_XINPUT2_H_SHIM */

