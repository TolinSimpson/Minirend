/*
 * GL/gl.h shim for Cosmopolitan
 * 
 * Comprehensive OpenGL type definitions and constants needed by sokol.
 * Actual GL functions are loaded dynamically via dlopen at runtime.
 * 
 * This header provides all constants required by sokol_gfx.h and sokol_app.h
 * for cross-compilation without system OpenGL headers.
 */

#ifndef _GL_GL_H_SHIM
#define _GL_GL_H_SHIM

#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * OpenGL Types
 * ============================================================================ */

typedef unsigned int GLenum;
typedef unsigned int GLbitfield;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLboolean;
typedef signed char GLbyte;
typedef short GLshort;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned long GLulong;
typedef float GLfloat;
typedef float GLclampf;
typedef double GLdouble;
typedef double GLclampd;
typedef void GLvoid;
typedef intptr_t GLintptr;
typedef size_t GLsizeiptr;
typedef char GLchar;
typedef int64_t GLint64;
typedef uint64_t GLuint64;
typedef struct __GLsync *GLsync;

/* ============================================================================
 * Boolean Values
 * ============================================================================ */

#define GL_FALSE 0
#define GL_TRUE  1
#define GL_NONE  0

/* ============================================================================
 * Data Types
 * ============================================================================ */

#define GL_BYTE                           0x1400
#define GL_UNSIGNED_BYTE                  0x1401
#define GL_SHORT                          0x1402
#define GL_UNSIGNED_SHORT                 0x1403
#define GL_INT                            0x1404
#define GL_UNSIGNED_INT                   0x1405
#define GL_FLOAT                          0x1406
#define GL_2_BYTES                        0x1407
#define GL_3_BYTES                        0x1408
#define GL_4_BYTES                        0x1409
#define GL_DOUBLE                         0x140A
#define GL_HALF_FLOAT                     0x140B
#define GL_FIXED                          0x140C
#define GL_UNSIGNED_INT_2_10_10_10_REV    0x8368
#define GL_UNSIGNED_INT_10F_11F_11F_REV   0x8C3B
#define GL_UNSIGNED_INT_5_9_9_9_REV       0x8C3E
#define GL_UNSIGNED_INT_24_8              0x84FA

/* ============================================================================
 * Primitives
 * ============================================================================ */

#define GL_POINTS         0x0000
#define GL_LINES          0x0001
#define GL_LINE_LOOP      0x0002
#define GL_LINE_STRIP     0x0003
#define GL_TRIANGLES      0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TRIANGLE_FAN   0x0006
#define GL_QUADS          0x0007

/* ============================================================================
 * Clear Buffer Bits
 * ============================================================================ */

#define GL_DEPTH_BUFFER_BIT   0x00000100
#define GL_STENCIL_BUFFER_BIT 0x00000400
#define GL_COLOR_BUFFER_BIT   0x00004000

/* ============================================================================
 * Enable/Disable Caps
 * ============================================================================ */

#define GL_CULL_FACE                0x0B44
#define GL_DEPTH_TEST               0x0B71
#define GL_STENCIL_TEST             0x0B90
#define GL_DITHER                   0x0BD0
#define GL_BLEND                    0x0BE2
#define GL_SCISSOR_TEST             0x0C11
#define GL_POLYGON_OFFSET_FILL      0x8037
#define GL_SAMPLE_ALPHA_TO_COVERAGE 0x809E
#define GL_SAMPLE_COVERAGE          0x80A0
#define GL_MULTISAMPLE              0x809D
#define GL_PROGRAM_POINT_SIZE       0x8642
#define GL_TEXTURE_CUBE_MAP_SEAMLESS 0x884F
#define GL_FRAMEBUFFER_SRGB         0x8DB9
#define GL_PRIMITIVE_RESTART        0x8F9D
#define GL_PRIMITIVE_RESTART_FIXED_INDEX 0x8D69

/* ============================================================================
 * Blend Functions
 * ============================================================================ */

#define GL_ZERO                     0
#define GL_ONE                      1
#define GL_SRC_COLOR                0x0300
#define GL_ONE_MINUS_SRC_COLOR      0x0301
#define GL_SRC_ALPHA                0x0302
#define GL_ONE_MINUS_SRC_ALPHA      0x0303
#define GL_DST_ALPHA                0x0304
#define GL_ONE_MINUS_DST_ALPHA      0x0305
#define GL_DST_COLOR                0x0306
#define GL_ONE_MINUS_DST_COLOR      0x0307
#define GL_SRC_ALPHA_SATURATE       0x0308
#define GL_CONSTANT_COLOR           0x8001
#define GL_ONE_MINUS_CONSTANT_COLOR 0x8002
#define GL_CONSTANT_ALPHA           0x8003
#define GL_ONE_MINUS_CONSTANT_ALPHA 0x8004
#define GL_SRC1_ALPHA               0x8589
#define GL_SRC1_COLOR               0x88F9
#define GL_ONE_MINUS_SRC1_COLOR     0x88FA
#define GL_ONE_MINUS_SRC1_ALPHA     0x88FB

/* ============================================================================
 * Blend Equations
 * ============================================================================ */

#define GL_FUNC_ADD              0x8006
#define GL_FUNC_SUBTRACT         0x800A
#define GL_FUNC_REVERSE_SUBTRACT 0x800B
#define GL_MIN                   0x8007
#define GL_MAX                   0x8008

/* ============================================================================
 * Texture Targets
 * ============================================================================ */

#define GL_TEXTURE_1D                   0x0DE0
#define GL_TEXTURE_2D                   0x0DE1
#define GL_TEXTURE_3D                   0x806F
#define GL_TEXTURE_CUBE_MAP             0x8513
#define GL_TEXTURE_RECTANGLE            0x84F5
#define GL_TEXTURE_1D_ARRAY             0x8C18
#define GL_TEXTURE_2D_ARRAY             0x8C1A
#define GL_TEXTURE_BUFFER               0x8C2A
#define GL_TEXTURE_2D_MULTISAMPLE       0x9100
#define GL_TEXTURE_2D_MULTISAMPLE_ARRAY 0x9102
#define GL_TEXTURE_CUBE_MAP_ARRAY       0x9009

/* Cube map faces */
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X  0x8515
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_X  0x8516
#define GL_TEXTURE_CUBE_MAP_POSITIVE_Y  0x8517
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Y  0x8518
#define GL_TEXTURE_CUBE_MAP_POSITIVE_Z  0x8519
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Z  0x851A

/* ============================================================================
 * Texture Parameters
 * ============================================================================ */

#define GL_TEXTURE_MAG_FILTER     0x2800
#define GL_TEXTURE_MIN_FILTER     0x2801
#define GL_TEXTURE_WRAP_S         0x2802
#define GL_TEXTURE_WRAP_T         0x2803
#define GL_TEXTURE_WRAP_R         0x8072
#define GL_TEXTURE_MIN_LOD        0x813A
#define GL_TEXTURE_MAX_LOD        0x813B
#define GL_TEXTURE_BASE_LEVEL     0x813C
#define GL_TEXTURE_MAX_LEVEL      0x813D
#define GL_TEXTURE_COMPARE_MODE   0x884C
#define GL_TEXTURE_COMPARE_FUNC   0x884D
#define GL_TEXTURE_SWIZZLE_R      0x8E42
#define GL_TEXTURE_SWIZZLE_G      0x8E43
#define GL_TEXTURE_SWIZZLE_B      0x8E44
#define GL_TEXTURE_SWIZZLE_A      0x8E45
#define GL_TEXTURE_BORDER_COLOR   0x1004

/* ============================================================================
 * Texture Filter Modes
 * ============================================================================ */

#define GL_NEAREST                0x2600
#define GL_LINEAR                 0x2601
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#define GL_LINEAR_MIPMAP_NEAREST  0x2701
#define GL_NEAREST_MIPMAP_LINEAR  0x2702
#define GL_LINEAR_MIPMAP_LINEAR   0x2703

/* ============================================================================
 * Texture Wrap Modes
 * ============================================================================ */

#define GL_REPEAT               0x2901
#define GL_CLAMP_TO_EDGE        0x812F
#define GL_CLAMP_TO_BORDER      0x812D
#define GL_MIRRORED_REPEAT      0x8370
#define GL_MIRROR_CLAMP_TO_EDGE 0x8743

/* ============================================================================
 * Pixel Formats
 * ============================================================================ */

#define GL_DEPTH_COMPONENT 0x1902
#define GL_RED             0x1903
#define GL_GREEN           0x1904
#define GL_BLUE            0x1905
#define GL_ALPHA           0x1906
#define GL_RGB             0x1907
#define GL_RGBA            0x1908
#define GL_LUMINANCE       0x1909
#define GL_LUMINANCE_ALPHA 0x190A
#define GL_STENCIL_INDEX   0x1901
#define GL_DEPTH_STENCIL   0x84F9
#define GL_RG              0x8227
#define GL_BGRA            0x80E1
#define GL_BGR             0x80E0
#define GL_RED_INTEGER     0x8D94
#define GL_GREEN_INTEGER   0x8D95
#define GL_BLUE_INTEGER    0x8D96
#define GL_RG_INTEGER      0x8228
#define GL_RGB_INTEGER     0x8D98
#define GL_RGBA_INTEGER    0x8D99
#define GL_BGR_INTEGER     0x8D9A
#define GL_BGRA_INTEGER    0x8D9B

/* ============================================================================
 * Internal Formats - Base
 * ============================================================================ */

#define GL_R8                   0x8229
#define GL_R8_SNORM             0x8F94
#define GL_R16                  0x822A
#define GL_R16_SNORM            0x8F98
#define GL_RG8                  0x822B
#define GL_RG8_SNORM            0x8F95
#define GL_RG16                 0x822C
#define GL_RG16_SNORM           0x8F99
#define GL_R3_G3_B2             0x2A10
#define GL_RGB4                 0x804F
#define GL_RGB5                 0x8050
#define GL_RGB8                 0x8051
#define GL_RGB8_SNORM           0x8F96
#define GL_RGB10                0x8052
#define GL_RGB12                0x8053
#define GL_RGB16                0x8054
#define GL_RGB16_SNORM          0x8F9A
#define GL_RGBA2                0x8055
#define GL_RGBA4                0x8056
#define GL_RGB5_A1              0x8057
#define GL_RGBA8                0x8058
#define GL_RGBA8_SNORM          0x8F97
#define GL_RGB10_A2             0x8059
#define GL_RGB10_A2UI           0x906F
#define GL_RGBA12               0x805A
#define GL_RGBA16               0x805B
#define GL_RGBA16_SNORM         0x8F9B
#define GL_SRGB8                0x8C41
#define GL_SRGB8_ALPHA8         0x8C43

/* Internal Formats - Float */
#define GL_R16F    0x822D
#define GL_RG16F   0x822F
#define GL_RGB16F  0x881B
#define GL_RGBA16F 0x881A
#define GL_R32F    0x822E
#define GL_RG32F   0x8230
#define GL_RGB32F  0x8815
#define GL_RGBA32F 0x8814
#define GL_R11F_G11F_B10F 0x8C3A
#define GL_RGB9_E5 0x8C3D

/* Internal Formats - Integer */
#define GL_R8I      0x8231
#define GL_R8UI     0x8232
#define GL_R16I     0x8233
#define GL_R16UI    0x8234
#define GL_R32I     0x8235
#define GL_R32UI    0x8236
#define GL_RG8I     0x8237
#define GL_RG8UI    0x8238
#define GL_RG16I    0x8239
#define GL_RG16UI   0x823A
#define GL_RG32I    0x823B
#define GL_RG32UI   0x823C
#define GL_RGB8I    0x8D8F
#define GL_RGB8UI   0x8D7D
#define GL_RGB16I   0x8D89
#define GL_RGB16UI  0x8D77
#define GL_RGB32I   0x8D83
#define GL_RGB32UI  0x8D71
#define GL_RGBA8I   0x8D8E
#define GL_RGBA8UI  0x8D7C
#define GL_RGBA16I  0x8D88
#define GL_RGBA16UI 0x8D76
#define GL_RGBA32I  0x8D82
#define GL_RGBA32UI 0x8D70

/* Internal Formats - Depth/Stencil */
#define GL_DEPTH_COMPONENT16  0x81A5
#define GL_DEPTH_COMPONENT24  0x81A6
#define GL_DEPTH_COMPONENT32  0x81A7
#define GL_DEPTH_COMPONENT32F 0x8CAC
#define GL_DEPTH24_STENCIL8   0x88F0
#define GL_DEPTH32F_STENCIL8  0x8CAD
#define GL_STENCIL_INDEX1     0x8D46
#define GL_STENCIL_INDEX4     0x8D47
#define GL_STENCIL_INDEX8     0x8D48
#define GL_STENCIL_INDEX16    0x8D49

/* ============================================================================
 * Compressed Formats
 * ============================================================================ */

#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT        0x83F0
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT       0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT       0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT       0x83F3
#define GL_COMPRESSED_SRGB_S3TC_DXT1_EXT       0x8C4C
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT 0x8C4D
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT 0x8C4E
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT 0x8C4F
#define GL_COMPRESSED_RED_RGTC1                0x8DBB
#define GL_COMPRESSED_SIGNED_RED_RGTC1         0x8DBC
#define GL_COMPRESSED_RG_RGTC2                 0x8DBD
#define GL_COMPRESSED_SIGNED_RG_RGTC2          0x8DBE
#define GL_COMPRESSED_RGBA_BPTC_UNORM          0x8E8C
#define GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM    0x8E8D
#define GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT    0x8E8E
#define GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT  0x8E8F

/* ============================================================================
 * Buffer Targets
 * ============================================================================ */

#define GL_ARRAY_BUFFER              0x8892
#define GL_ELEMENT_ARRAY_BUFFER      0x8893
#define GL_PIXEL_PACK_BUFFER         0x88EB
#define GL_PIXEL_UNPACK_BUFFER       0x88EC
#define GL_UNIFORM_BUFFER            0x8A11
#define GL_TEXTURE_BUFFER            0x8C2A
#define GL_TRANSFORM_FEEDBACK_BUFFER 0x8C8E
#define GL_COPY_READ_BUFFER          0x8F36
#define GL_COPY_WRITE_BUFFER         0x8F37
#define GL_DRAW_INDIRECT_BUFFER      0x8F3F
#define GL_SHADER_STORAGE_BUFFER     0x90D2
#define GL_DISPATCH_INDIRECT_BUFFER  0x90EE
#define GL_QUERY_BUFFER              0x9192
#define GL_ATOMIC_COUNTER_BUFFER     0x92C0

/* ============================================================================
 * Buffer Usage
 * ============================================================================ */

#define GL_STREAM_DRAW  0x88E0
#define GL_STREAM_READ  0x88E1
#define GL_STREAM_COPY  0x88E2
#define GL_STATIC_DRAW  0x88E4
#define GL_STATIC_READ  0x88E5
#define GL_STATIC_COPY  0x88E6
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_DYNAMIC_READ 0x88E9
#define GL_DYNAMIC_COPY 0x88EA

/* ============================================================================
 * Framebuffer
 * ============================================================================ */

#define GL_FRAMEBUFFER               0x8D40
#define GL_READ_FRAMEBUFFER          0x8CA8
#define GL_DRAW_FRAMEBUFFER          0x8CA9
#define GL_RENDERBUFFER              0x8D41
#define GL_FRAMEBUFFER_BINDING       0x8CA6
#define GL_RENDERBUFFER_BINDING      0x8CA7
#define GL_MAX_COLOR_ATTACHMENTS     0x8CDF
#define GL_MAX_RENDERBUFFER_SIZE     0x84E8
#define GL_MAX_SAMPLES               0x8D57
#define GL_MAX_DRAW_BUFFERS          0x8824

/* Framebuffer attachments */
#define GL_COLOR_ATTACHMENT0         0x8CE0
#define GL_COLOR_ATTACHMENT1         0x8CE1
#define GL_COLOR_ATTACHMENT2         0x8CE2
#define GL_COLOR_ATTACHMENT3         0x8CE3
#define GL_COLOR_ATTACHMENT4         0x8CE4
#define GL_COLOR_ATTACHMENT5         0x8CE5
#define GL_COLOR_ATTACHMENT6         0x8CE6
#define GL_COLOR_ATTACHMENT7         0x8CE7
#define GL_COLOR_ATTACHMENT8         0x8CE8
#define GL_COLOR_ATTACHMENT9         0x8CE9
#define GL_COLOR_ATTACHMENT10        0x8CEA
#define GL_COLOR_ATTACHMENT11        0x8CEB
#define GL_COLOR_ATTACHMENT12        0x8CEC
#define GL_COLOR_ATTACHMENT13        0x8CED
#define GL_COLOR_ATTACHMENT14        0x8CEE
#define GL_COLOR_ATTACHMENT15        0x8CEF
#define GL_DEPTH_ATTACHMENT          0x8D00
#define GL_STENCIL_ATTACHMENT        0x8D20
#define GL_DEPTH_STENCIL_ATTACHMENT  0x821A

/* Framebuffer status */
#define GL_FRAMEBUFFER_COMPLETE                      0x8CD5
#define GL_FRAMEBUFFER_UNDEFINED                     0x8219
#define GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT         0x8CD6
#define GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT 0x8CD7
#define GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER        0x8CDB
#define GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER        0x8CDC
#define GL_FRAMEBUFFER_UNSUPPORTED                   0x8CDD
#define GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE        0x8D56
#define GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS      0x8DA8

/* Clear buffer targets */
#define GL_COLOR   0x1800
#define GL_DEPTH   0x1801
#define GL_STENCIL 0x1802

/* ============================================================================
 * Shaders
 * ============================================================================ */

#define GL_FRAGMENT_SHADER        0x8B30
#define GL_VERTEX_SHADER          0x8B31
#define GL_GEOMETRY_SHADER        0x8DD9
#define GL_TESS_EVALUATION_SHADER 0x8E87
#define GL_TESS_CONTROL_SHADER    0x8E88
#define GL_COMPUTE_SHADER         0x91B9
#define GL_COMPILE_STATUS         0x8B81
#define GL_LINK_STATUS            0x8B82
#define GL_VALIDATE_STATUS        0x8B83
#define GL_INFO_LOG_LENGTH        0x8B84
#define GL_ATTACHED_SHADERS       0x8B85
#define GL_ACTIVE_UNIFORMS        0x8B86
#define GL_ACTIVE_ATTRIBUTES      0x8B89
#define GL_CURRENT_PROGRAM        0x8B8D
#define GL_SHADER_TYPE            0x8B4F

/* ============================================================================
 * Comparison Functions
 * ============================================================================ */

#define GL_NEVER    0x0200
#define GL_LESS     0x0201
#define GL_EQUAL    0x0202
#define GL_LEQUAL   0x0203
#define GL_GREATER  0x0204
#define GL_NOTEQUAL 0x0205
#define GL_GEQUAL   0x0206
#define GL_ALWAYS   0x0207

/* ============================================================================
 * Stencil Operations
 * ============================================================================ */

#define GL_KEEP      0x1E00
#define GL_REPLACE   0x1E01
#define GL_INCR      0x1E02
#define GL_DECR      0x1E03
#define GL_INVERT    0x150A
#define GL_INCR_WRAP 0x8507
#define GL_DECR_WRAP 0x8508

/* ============================================================================
 * Face Culling
 * ============================================================================ */

#define GL_FRONT          0x0404
#define GL_BACK           0x0405
#define GL_FRONT_AND_BACK 0x0408
#define GL_CW             0x0900
#define GL_CCW            0x0901

/* ============================================================================
 * Queries / Gets
 * ============================================================================ */

#define GL_VENDOR                            0x1F00
#define GL_RENDERER                          0x1F01
#define GL_VERSION                           0x1F02
#define GL_EXTENSIONS                        0x1F03
#define GL_SHADING_LANGUAGE_VERSION          0x8B8C
#define GL_NUM_EXTENSIONS                    0x821D
#define GL_MAJOR_VERSION                     0x821B
#define GL_MINOR_VERSION                     0x821C
#define GL_MAX_TEXTURE_SIZE                  0x0D33
#define GL_MAX_3D_TEXTURE_SIZE               0x8073
#define GL_MAX_CUBE_MAP_TEXTURE_SIZE         0x851C
#define GL_MAX_ARRAY_TEXTURE_LAYERS          0x88FF
#define GL_MAX_TEXTURE_IMAGE_UNITS           0x8872
#define GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS    0x8B4C
#define GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS  0x8B4D
#define GL_MAX_VERTEX_ATTRIBS                0x8869
#define GL_MAX_VERTEX_UNIFORM_COMPONENTS     0x8B4A
#define GL_MAX_FRAGMENT_UNIFORM_COMPONENTS   0x8B49
#define GL_MAX_UNIFORM_BLOCK_SIZE            0x8A30
#define GL_MAX_UNIFORM_BUFFER_BINDINGS       0x8A2F
#define GL_MAX_VARYING_COMPONENTS            0x8B4B
#define GL_VIEWPORT                          0x0BA2
#define GL_SCISSOR_BOX                       0x0C10

/* ============================================================================
 * Error Codes
 * ============================================================================ */

#define GL_NO_ERROR          0
#define GL_INVALID_ENUM      0x0500
#define GL_INVALID_VALUE     0x0501
#define GL_INVALID_OPERATION 0x0502
#define GL_STACK_OVERFLOW    0x0503
#define GL_STACK_UNDERFLOW   0x0504
#define GL_OUT_OF_MEMORY     0x0505
#define GL_INVALID_FRAMEBUFFER_OPERATION 0x0506

/* ============================================================================
 * Pixel Store
 * ============================================================================ */

#define GL_UNPACK_SWAP_BYTES    0x0CF0
#define GL_UNPACK_LSB_FIRST     0x0CF1
#define GL_UNPACK_ROW_LENGTH    0x0CF2
#define GL_UNPACK_SKIP_ROWS     0x0CF3
#define GL_UNPACK_SKIP_PIXELS   0x0CF4
#define GL_UNPACK_ALIGNMENT     0x0CF5
#define GL_PACK_SWAP_BYTES      0x0D00
#define GL_PACK_LSB_FIRST       0x0D01
#define GL_PACK_ROW_LENGTH      0x0D02
#define GL_PACK_SKIP_ROWS       0x0D03
#define GL_PACK_SKIP_PIXELS     0x0D04
#define GL_PACK_ALIGNMENT       0x0D05
#define GL_UNPACK_SKIP_IMAGES   0x806D
#define GL_UNPACK_IMAGE_HEIGHT  0x806E

/* ============================================================================
 * Texture Units
 * ============================================================================ */

#define GL_TEXTURE0  0x84C0
#define GL_TEXTURE1  0x84C1
#define GL_TEXTURE2  0x84C2
#define GL_TEXTURE3  0x84C3
#define GL_TEXTURE4  0x84C4
#define GL_TEXTURE5  0x84C5
#define GL_TEXTURE6  0x84C6
#define GL_TEXTURE7  0x84C7
#define GL_TEXTURE8  0x84C8
#define GL_TEXTURE9  0x84C9
#define GL_TEXTURE10 0x84CA
#define GL_TEXTURE11 0x84CB
#define GL_TEXTURE12 0x84CC
#define GL_TEXTURE13 0x84CD
#define GL_TEXTURE14 0x84CE
#define GL_TEXTURE15 0x84CF
#define GL_TEXTURE16 0x84D0
#define GL_TEXTURE17 0x84D1
#define GL_TEXTURE18 0x84D2
#define GL_TEXTURE19 0x84D3
#define GL_TEXTURE20 0x84D4
#define GL_TEXTURE21 0x84D5
#define GL_TEXTURE22 0x84D6
#define GL_TEXTURE23 0x84D7
#define GL_TEXTURE24 0x84D8
#define GL_TEXTURE25 0x84D9
#define GL_TEXTURE26 0x84DA
#define GL_TEXTURE27 0x84DB
#define GL_TEXTURE28 0x84DC
#define GL_TEXTURE29 0x84DD
#define GL_TEXTURE30 0x84DE
#define GL_TEXTURE31 0x84DF
#define GL_ACTIVE_TEXTURE 0x84E0

/* ============================================================================
 * Vertex Arrays
 * ============================================================================ */

#define GL_VERTEX_ARRAY_BINDING        0x85B5
#define GL_VERTEX_ATTRIB_ARRAY_DIVISOR 0x88FE

/* ============================================================================
 * Samplers
 * ============================================================================ */

#define GL_SAMPLER_BINDING       0x8919
#define GL_COMPARE_REF_TO_TEXTURE 0x884E

/* ============================================================================
 * Sync Objects
 * ============================================================================ */

#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#define GL_ALREADY_SIGNALED           0x911A
#define GL_TIMEOUT_EXPIRED            0x911B
#define GL_CONDITION_SATISFIED        0x911C
#define GL_WAIT_FAILED                0x911D
#define GL_SYNC_FLUSH_COMMANDS_BIT    0x00000001
#define GL_TIMEOUT_IGNORED            0xFFFFFFFFFFFFFFFFull

/* ============================================================================
 * Debug
 * ============================================================================ */

#define GL_DEBUG_OUTPUT             0x92E0
#define GL_DEBUG_OUTPUT_SYNCHRONOUS 0x8242

/* ============================================================================
 * Write Masks
 * ============================================================================ */

#define GL_COLOR_WRITEMASK 0x0C23
#define GL_DEPTH_WRITEMASK 0x0B72

#endif /* _GL_GL_H_SHIM */
