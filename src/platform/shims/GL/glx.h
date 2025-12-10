/*
 * GL/glx.h shim for Cosmopolitan
 */

#ifndef _GL_GLX_H_SHIM
#define _GL_GLX_H_SHIM

#include "gl.h"
#include "../X11/Xlib.h"

typedef struct __GLXcontextRec *GLXContext;
typedef XID GLXPixmap;
typedef XID GLXDrawable;
typedef struct __GLXFBConfigRec *GLXFBConfig;
typedef XID GLXFBConfigID;
typedef XID GLXContextID;
typedef XID GLXWindow;
typedef XID GLXPbuffer;

/* GLX 1.0/1.1 */
#define GLX_USE_GL           1
#define GLX_BUFFER_SIZE      2
#define GLX_LEVEL            3
#define GLX_RGBA             4
#define GLX_DOUBLEBUFFER     5
#define GLX_STEREO           6
#define GLX_AUX_BUFFERS      7
#define GLX_RED_SIZE         8
#define GLX_GREEN_SIZE       9
#define GLX_BLUE_SIZE        10
#define GLX_ALPHA_SIZE       11
#define GLX_DEPTH_SIZE       12
#define GLX_STENCIL_SIZE     13
#define GLX_ACCUM_RED_SIZE   14
#define GLX_ACCUM_GREEN_SIZE 15
#define GLX_ACCUM_BLUE_SIZE  16
#define GLX_ACCUM_ALPHA_SIZE 17

/* GLX 1.3 */
#define GLX_X_VISUAL_TYPE      0x22
#define GLX_CONFIG_CAVEAT      0x20
#define GLX_TRANSPARENT_TYPE   0x23
#define GLX_TRANSPARENT_INDEX_VALUE 0x24
#define GLX_TRANSPARENT_RED_VALUE   0x25
#define GLX_TRANSPARENT_GREEN_VALUE 0x26
#define GLX_TRANSPARENT_BLUE_VALUE  0x27
#define GLX_TRANSPARENT_ALPHA_VALUE 0x28
#define GLX_DRAWABLE_TYPE      0x8010
#define GLX_RENDER_TYPE        0x8011
#define GLX_X_RENDERABLE       0x8012
#define GLX_FBCONFIG_ID        0x8013
#define GLX_RGBA_TYPE          0x8014
#define GLX_COLOR_INDEX_TYPE   0x8015
#define GLX_MAX_PBUFFER_WIDTH  0x8016
#define GLX_MAX_PBUFFER_HEIGHT 0x8017
#define GLX_MAX_PBUFFER_PIXELS 0x8018
#define GLX_VISUAL_ID          0x800B
#define GLX_SCREEN             0x800C

/* GLX_DRAWABLE_TYPE bits */
#define GLX_WINDOW_BIT         0x00000001
#define GLX_PIXMAP_BIT         0x00000002
#define GLX_PBUFFER_BIT        0x00000004

/* GLX_RENDER_TYPE bits */
#define GLX_RGBA_BIT           0x00000001
#define GLX_COLOR_INDEX_BIT    0x00000002

/* GLX_CONFIG_CAVEAT values */
#define GLX_NONE               0x8000
#define GLX_SLOW_CONFIG        0x8001
#define GLX_NON_CONFORMANT_CONFIG 0x800D

/* GLX_X_VISUAL_TYPE values */
#define GLX_TRUE_COLOR         0x8002
#define GLX_DIRECT_COLOR       0x8003
#define GLX_PSEUDO_COLOR       0x8004
#define GLX_STATIC_COLOR       0x8005
#define GLX_GRAY_SCALE         0x8006
#define GLX_STATIC_GRAY        0x8007

/* GLX_TRANSPARENT_TYPE values */
#define GLX_TRANSPARENT_RGB    0x8008
#define GLX_TRANSPARENT_INDEX  0x8009

/* GLX 1.4+ */
#define GLX_SAMPLE_BUFFERS     0x186a0
#define GLX_SAMPLES            0x186a1

/* ARB_create_context */
#define GLX_CONTEXT_MAJOR_VERSION_ARB             0x2091
#define GLX_CONTEXT_MINOR_VERSION_ARB             0x2092
#define GLX_CONTEXT_FLAGS_ARB                     0x2094
#define GLX_CONTEXT_PROFILE_MASK_ARB              0x9126
#define GLX_CONTEXT_DEBUG_BIT_ARB                 0x0001
#define GLX_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB    0x0002
#define GLX_CONTEXT_CORE_PROFILE_BIT_ARB          0x0001
#define GLX_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB 0x0002

/* ARB_multisample */
#define GLX_SAMPLE_BUFFERS_ARB 100000
#define GLX_SAMPLES_ARB        100001

/* EXT_swap_control */
#define GLX_SWAP_INTERVAL_EXT     0x20F1
#define GLX_MAX_SWAP_INTERVAL_EXT 0x20F2

/* ARB_framebuffer_sRGB */
#define GLX_FRAMEBUFFER_SRGB_CAPABLE_ARB 0x20B2

#endif /* _GL_GLX_H_SHIM */

