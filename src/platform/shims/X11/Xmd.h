/*
 * X11/Xmd.h shim for Cosmopolitan
 * 
 * Provides machine-dependent type definitions used by X11.
 */

#ifndef _X11_XMD_H_SHIM
#define _X11_XMD_H_SHIM

#include <stdint.h>

/* X11 standard types */
typedef uint32_t CARD32;
typedef uint16_t CARD16;
typedef uint8_t  CARD8;
typedef int32_t  INT32;
typedef int16_t  INT16;
typedef int8_t   INT8;

typedef CARD32 BITS32;
typedef CARD16 BITS16;

typedef CARD8 BYTE;
typedef CARD8 BOOL;

#endif /* _X11_XMD_H_SHIM */

