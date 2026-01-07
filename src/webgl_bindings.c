/*
 * WebGL 2.0 Bindings for Minirend
 * 
 * Phase 1: Core Architecture
 * - Resource management (JS handle -> GL handle mapping)
 * - WebGLContext state tracking
 * - JS class registration for all WebGL object types
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "minirend.h"
#include "quickjs.h"
#include "platform/shims/GL/gl.h"

/* ============================================================================
 * Simple Hash Map for Resource Management
 * Maps uint32 keys (JS handles) to uint32 values (GL handles)
 * ============================================================================ */

#define HASHMAP_INITIAL_CAPACITY 64
#define HASHMAP_LOAD_FACTOR 0.75

typedef struct HashMapEntry {
    uint32_t key;
    uint32_t value;
    uint8_t  occupied;
} HashMapEntry;

typedef struct HashMap {
    HashMapEntry *entries;
    uint32_t      capacity;
    uint32_t      count;
    uint32_t      next_handle;  /* Auto-incrementing handle generator */
} HashMap;

static HashMap *hashmap_create(void) {
    HashMap *map = (HashMap *)calloc(1, sizeof(HashMap));
    if (!map) return NULL;
    
    map->entries = (HashMapEntry *)calloc(HASHMAP_INITIAL_CAPACITY, sizeof(HashMapEntry));
    if (!map->entries) {
        free(map);
        return NULL;
    }
    
    map->capacity = HASHMAP_INITIAL_CAPACITY;
    map->count = 0;
    map->next_handle = 1;  /* Start handles at 1 (0 = invalid/null) */
    return map;
}

static void hashmap_destroy(HashMap *map) {
    if (map) {
        free(map->entries);
        free(map);
    }
}

static uint32_t hashmap_hash(uint32_t key, uint32_t capacity) {
    /* Simple multiplicative hash */
    return (key * 2654435761u) % capacity;
}

static int hashmap_resize(HashMap *map, uint32_t new_capacity) {
    HashMapEntry *old_entries = map->entries;
    uint32_t old_capacity = map->capacity;
    
    map->entries = (HashMapEntry *)calloc(new_capacity, sizeof(HashMapEntry));
    if (!map->entries) {
        map->entries = old_entries;
        return 0;
    }
    
    map->capacity = new_capacity;
    map->count = 0;
    
    /* Re-insert all entries */
    for (uint32_t i = 0; i < old_capacity; i++) {
        if (old_entries[i].occupied) {
            uint32_t idx = hashmap_hash(old_entries[i].key, new_capacity);
            while (map->entries[idx].occupied) {
                idx = (idx + 1) % new_capacity;
            }
            map->entries[idx] = old_entries[i];
            map->count++;
        }
    }
    
    free(old_entries);
    return 1;
}

/* Insert a key-value pair, returns 1 on success */
static int hashmap_put(HashMap *map, uint32_t key, uint32_t value) {
    if ((float)map->count / map->capacity >= HASHMAP_LOAD_FACTOR) {
        if (!hashmap_resize(map, map->capacity * 2)) {
            return 0;
        }
    }
    
    uint32_t idx = hashmap_hash(key, map->capacity);
    while (map->entries[idx].occupied && map->entries[idx].key != key) {
        idx = (idx + 1) % map->capacity;
    }
    
    if (!map->entries[idx].occupied) {
        map->count++;
    }
    
    map->entries[idx].key = key;
    map->entries[idx].value = value;
    map->entries[idx].occupied = 1;
    return 1;
}

/* Get value by key, returns 0 if not found (GL handle 0 is invalid anyway) */
static uint32_t hashmap_get(HashMap *map, uint32_t key) {
    uint32_t idx = hashmap_hash(key, map->capacity);
    uint32_t start = idx;
    
    while (map->entries[idx].occupied) {
        if (map->entries[idx].key == key) {
            return map->entries[idx].value;
        }
        idx = (idx + 1) % map->capacity;
        if (idx == start) break;
    }
    return 0;
}

/* Remove by key, returns the removed value (0 if not found) */
static uint32_t hashmap_remove(HashMap *map, uint32_t key) {
    uint32_t idx = hashmap_hash(key, map->capacity);
    uint32_t start = idx;
    
    while (map->entries[idx].occupied) {
        if (map->entries[idx].key == key) {
            uint32_t value = map->entries[idx].value;
            map->entries[idx].occupied = 0;
            map->count--;
            return value;
        }
        idx = (idx + 1) % map->capacity;
        if (idx == start) break;
    }
    return 0;
}

/* Allocate a new handle and associate it with a GL handle */
static uint32_t hashmap_alloc(HashMap *map, uint32_t gl_handle) {
    uint32_t js_handle = map->next_handle++;
    hashmap_put(map, js_handle, gl_handle);
    return js_handle;
}

/* ============================================================================
 * Uniform Location Map
 * Maps uint32 keys to GLint values (which can be -1)
 * ============================================================================ */

typedef struct UniformMapEntry {
    uint32_t key;
    GLint    value;
    uint8_t  occupied;
} UniformMapEntry;

typedef struct UniformMap {
    UniformMapEntry *entries;
    uint32_t         capacity;
    uint32_t         count;
    uint32_t         next_handle;
} UniformMap;

static UniformMap *uniformmap_create(void) {
    UniformMap *map = (UniformMap *)calloc(1, sizeof(UniformMap));
    if (!map) return NULL;
    
    map->entries = (UniformMapEntry *)calloc(HASHMAP_INITIAL_CAPACITY, sizeof(UniformMapEntry));
    if (!map->entries) {
        free(map);
        return NULL;
    }
    
    map->capacity = HASHMAP_INITIAL_CAPACITY;
    map->count = 0;
    map->next_handle = 1;
    return map;
}

static void uniformmap_destroy(UniformMap *map) {
    if (map) {
        free(map->entries);
        free(map);
    }
}

static int uniformmap_put(UniformMap *map, uint32_t key, GLint value) {
    if ((float)map->count / map->capacity >= HASHMAP_LOAD_FACTOR) {
        /* Resize */
        UniformMapEntry *old = map->entries;
        uint32_t old_cap = map->capacity;
        map->capacity *= 2;
        map->entries = (UniformMapEntry *)calloc(map->capacity, sizeof(UniformMapEntry));
        if (!map->entries) {
            map->entries = old;
            map->capacity = old_cap;
            return 0;
        }
        map->count = 0;
        for (uint32_t i = 0; i < old_cap; i++) {
            if (old[i].occupied) {
                uint32_t idx = hashmap_hash(old[i].key, map->capacity);
                while (map->entries[idx].occupied) idx = (idx + 1) % map->capacity;
                map->entries[idx] = old[i];
                map->count++;
            }
        }
        free(old);
    }
    
    uint32_t idx = hashmap_hash(key, map->capacity);
    while (map->entries[idx].occupied && map->entries[idx].key != key) {
        idx = (idx + 1) % map->capacity;
    }
    if (!map->entries[idx].occupied) map->count++;
    map->entries[idx].key = key;
    map->entries[idx].value = value;
    map->entries[idx].occupied = 1;
    return 1;
}

static GLint uniformmap_get(UniformMap *map, uint32_t key, int *found) {
    uint32_t idx = hashmap_hash(key, map->capacity);
    uint32_t start = idx;
    while (map->entries[idx].occupied) {
        if (map->entries[idx].key == key) {
            if (found) *found = 1;
            return map->entries[idx].value;
        }
        idx = (idx + 1) % map->capacity;
        if (idx == start) break;
    }
    if (found) *found = 0;
    return -1;
}

static uint32_t uniformmap_alloc(UniformMap *map, GLint gl_location) {
    uint32_t js_handle = map->next_handle++;
    uniformmap_put(map, js_handle, gl_location);
    return js_handle;
}

/* ============================================================================
 * WebGL Context State
 * ============================================================================ */

typedef struct WebGLContext {
    /* Resource maps (JS handle -> GL handle) */
    HashMap    *buffers;
    HashMap    *textures;
    HashMap    *programs;
    HashMap    *shaders;
    HashMap    *framebuffers;
    HashMap    *renderbuffers;
    HashMap    *samplers;
    HashMap    *vaos;
    HashMap    *queries;
    HashMap    *transform_feedbacks;
    UniformMap *uniform_locations;
    
    /* Current state cache */
    GLuint current_program;
    GLuint current_vao;
    GLuint bound_array_buffer;
    GLuint bound_element_buffer;
    GLuint bound_framebuffer;
    GLuint bound_renderbuffer;
    GLuint active_texture;  /* GL_TEXTUREx enum */
    
    /* Viewport state */
    GLint viewport_x, viewport_y;
    GLsizei viewport_width, viewport_height;
    
    /* Clear state */
    GLfloat clear_color[4];
    GLfloat clear_depth;
    GLint   clear_stencil;
    
    /* Blend state */
    GLboolean blend_enabled;
    GLenum blend_src_rgb, blend_dst_rgb;
    GLenum blend_src_alpha, blend_dst_alpha;
    GLenum blend_equation_rgb, blend_equation_alpha;
    
    /* Depth state */
    GLboolean depth_test_enabled;
    GLboolean depth_mask;
    GLenum    depth_func;
    
    /* Stencil state */
    GLboolean stencil_test_enabled;
    
    /* Cull state */
    GLboolean cull_face_enabled;
    GLenum    cull_face_mode;
    GLenum    front_face;
    
    /* Scissor state */
    GLboolean scissor_test_enabled;
    GLint scissor_x, scissor_y;
    GLsizei scissor_width, scissor_height;
    
    /* Pixel store state */
    GLint unpack_alignment;
    GLint pack_alignment;
    GLboolean unpack_flip_y;
    GLboolean unpack_premultiply_alpha;
    
    /* Error state */
    GLenum last_error;
    
    /* Context state */
    GLboolean context_lost;
    
} WebGLContext;

/* ============================================================================
 * JS Class IDs and Definitions
 * ============================================================================ */

static JSClassID js_webgl_ctx_class_id;
static JSClassID js_webgl_buffer_class_id;
static JSClassID js_webgl_texture_class_id;
static JSClassID js_webgl_program_class_id;
static JSClassID js_webgl_shader_class_id;
static JSClassID js_webgl_framebuffer_class_id;
static JSClassID js_webgl_renderbuffer_class_id;
static JSClassID js_webgl_uniform_location_class_id;
static JSClassID js_webgl_active_info_class_id;
static JSClassID js_webgl_shader_precision_format_class_id;
static JSClassID js_webgl_sampler_class_id;
static JSClassID js_webgl_vao_class_id;
static JSClassID js_webgl_query_class_id;
static JSClassID js_webgl_sync_class_id;
static JSClassID js_webgl_transform_feedback_class_id;

/* WebGL object wrappers store their JS handle */
typedef struct WebGLObject {
    uint32_t handle;  /* JS-side handle for lookup in context maps */
} WebGLObject;

/* ============================================================================
 * Finalizers
 * ============================================================================ */

static void js_webgl_ctx_finalizer(JSRuntime *rt, JSValue val) {
    WebGLContext *ctx = (WebGLContext *)JS_GetOpaque(val, js_webgl_ctx_class_id);
    if (ctx) {
        hashmap_destroy(ctx->buffers);
        hashmap_destroy(ctx->textures);
        hashmap_destroy(ctx->programs);
        hashmap_destroy(ctx->shaders);
        hashmap_destroy(ctx->framebuffers);
        hashmap_destroy(ctx->renderbuffers);
        hashmap_destroy(ctx->samplers);
        hashmap_destroy(ctx->vaos);
        hashmap_destroy(ctx->queries);
        hashmap_destroy(ctx->transform_feedbacks);
        uniformmap_destroy(ctx->uniform_locations);
        free(ctx);
    }
}

static void js_webgl_object_finalizer(JSRuntime *rt, JSValue val) {
    /* Objects just store a handle reference, actual GL resources 
     * are cleaned up when deleteX() is called or context is destroyed */
    WebGLObject *obj = (WebGLObject *)JS_GetOpaque(val, js_webgl_buffer_class_id);
    if (obj) free(obj);
}

/* Generic finalizer for all WebGL object types */
#define DEFINE_WEBGL_OBJECT_FINALIZER(name, class_id) \
    static void js_##name##_finalizer(JSRuntime *rt, JSValue val) { \
        WebGLObject *obj = (WebGLObject *)JS_GetOpaque(val, class_id); \
        if (obj) free(obj); \
    }

DEFINE_WEBGL_OBJECT_FINALIZER(webgl_buffer, js_webgl_buffer_class_id)
DEFINE_WEBGL_OBJECT_FINALIZER(webgl_texture, js_webgl_texture_class_id)
DEFINE_WEBGL_OBJECT_FINALIZER(webgl_program, js_webgl_program_class_id)
DEFINE_WEBGL_OBJECT_FINALIZER(webgl_shader, js_webgl_shader_class_id)
DEFINE_WEBGL_OBJECT_FINALIZER(webgl_framebuffer, js_webgl_framebuffer_class_id)
DEFINE_WEBGL_OBJECT_FINALIZER(webgl_renderbuffer, js_webgl_renderbuffer_class_id)
DEFINE_WEBGL_OBJECT_FINALIZER(webgl_uniform_location, js_webgl_uniform_location_class_id)
DEFINE_WEBGL_OBJECT_FINALIZER(webgl_sampler, js_webgl_sampler_class_id)
DEFINE_WEBGL_OBJECT_FINALIZER(webgl_vao, js_webgl_vao_class_id)
DEFINE_WEBGL_OBJECT_FINALIZER(webgl_query, js_webgl_query_class_id)
DEFINE_WEBGL_OBJECT_FINALIZER(webgl_sync, js_webgl_sync_class_id)
DEFINE_WEBGL_OBJECT_FINALIZER(webgl_transform_feedback, js_webgl_transform_feedback_class_id)

/* ============================================================================
 * JS Class Definitions
 * ============================================================================ */

static JSClassDef js_webgl_ctx_class = {
    "WebGL2RenderingContext",
    .finalizer = js_webgl_ctx_finalizer,
};

static JSClassDef js_webgl_buffer_class = {
    "WebGLBuffer",
    .finalizer = js_webgl_buffer_finalizer,
};

static JSClassDef js_webgl_texture_class = {
    "WebGLTexture",
    .finalizer = js_webgl_texture_finalizer,
};

static JSClassDef js_webgl_program_class = {
    "WebGLProgram",
    .finalizer = js_webgl_program_finalizer,
};

static JSClassDef js_webgl_shader_class = {
    "WebGLShader",
    .finalizer = js_webgl_shader_finalizer,
};

static JSClassDef js_webgl_framebuffer_class = {
    "WebGLFramebuffer",
    .finalizer = js_webgl_framebuffer_finalizer,
};

static JSClassDef js_webgl_renderbuffer_class = {
    "WebGLRenderbuffer",
    .finalizer = js_webgl_renderbuffer_finalizer,
};

static JSClassDef js_webgl_uniform_location_class = {
    "WebGLUniformLocation",
    .finalizer = js_webgl_uniform_location_finalizer,
};

static JSClassDef js_webgl_active_info_class = {
    "WebGLActiveInfo",
    .finalizer = NULL,  /* Plain JS object, no special cleanup */
};

static JSClassDef js_webgl_shader_precision_format_class = {
    "WebGLShaderPrecisionFormat",
    .finalizer = NULL,
};

static JSClassDef js_webgl_sampler_class = {
    "WebGLSampler",
    .finalizer = js_webgl_sampler_finalizer,
};

static JSClassDef js_webgl_vao_class = {
    "WebGLVertexArrayObject",
    .finalizer = js_webgl_vao_finalizer,
};

static JSClassDef js_webgl_query_class = {
    "WebGLQuery",
    .finalizer = js_webgl_query_finalizer,
};

static JSClassDef js_webgl_sync_class = {
    "WebGLSync",
    .finalizer = js_webgl_sync_finalizer,
};

static JSClassDef js_webgl_transform_feedback_class = {
    "WebGLTransformFeedback",
    .finalizer = js_webgl_transform_feedback_finalizer,
};

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/* Get WebGLContext from JS 'this' value */
static WebGLContext *get_webgl_context(JSContext *ctx, JSValueConst this_val) {
    return (WebGLContext *)JS_GetOpaque(this_val, js_webgl_ctx_class_id);
}

/* Create a new WebGL object wrapper */
static JSValue create_webgl_object(JSContext *ctx, JSClassID class_id, uint32_t handle) {
    WebGLObject *obj = (WebGLObject *)malloc(sizeof(WebGLObject));
    if (!obj) return JS_EXCEPTION;
    
    obj->handle = handle;
    
    JSValue jsobj = JS_NewObjectClass(ctx, class_id);
    if (JS_IsException(jsobj)) {
        free(obj);
        return JS_EXCEPTION;
    }
    
    JS_SetOpaque(jsobj, obj);
    return jsobj;
}

/* Get handle from a WebGL object, returns 0 for null/undefined */
static uint32_t get_webgl_object_handle(JSContext *ctx, JSValueConst val, JSClassID class_id) {
    if (JS_IsNull(val) || JS_IsUndefined(val)) {
        return 0;
    }
    WebGLObject *obj = (WebGLObject *)JS_GetOpaque(val, class_id);
    return obj ? obj->handle : 0;
}

/* Set and return GL error */
static void set_gl_error(WebGLContext *wctx, GLenum error) {
    if (wctx->last_error == GL_NO_ERROR) {
        wctx->last_error = error;
    }
}

/* Check for GL errors and cache them */
static void check_gl_error(WebGLContext *wctx) {
    GLenum err = glGetError();
    if (err != GL_NO_ERROR && wctx->last_error == GL_NO_ERROR) {
        wctx->last_error = err;
    }
}

/* ============================================================================
 * Context Creation
 * ============================================================================ */

static WebGLContext *create_webgl_context(void) {
    WebGLContext *wctx = (WebGLContext *)calloc(1, sizeof(WebGLContext));
    if (!wctx) return NULL;
    
    /* Initialize resource maps */
    wctx->buffers = hashmap_create();
    wctx->textures = hashmap_create();
    wctx->programs = hashmap_create();
    wctx->shaders = hashmap_create();
    wctx->framebuffers = hashmap_create();
    wctx->renderbuffers = hashmap_create();
    wctx->samplers = hashmap_create();
    wctx->vaos = hashmap_create();
    wctx->queries = hashmap_create();
    wctx->transform_feedbacks = hashmap_create();
    wctx->uniform_locations = uniformmap_create();
    
    if (!wctx->buffers || !wctx->textures || !wctx->programs || 
        !wctx->shaders || !wctx->framebuffers || !wctx->renderbuffers ||
        !wctx->samplers || !wctx->vaos || !wctx->queries ||
        !wctx->transform_feedbacks || !wctx->uniform_locations) {
        /* Cleanup on failure */
        hashmap_destroy(wctx->buffers);
        hashmap_destroy(wctx->textures);
        hashmap_destroy(wctx->programs);
        hashmap_destroy(wctx->shaders);
        hashmap_destroy(wctx->framebuffers);
        hashmap_destroy(wctx->renderbuffers);
        hashmap_destroy(wctx->samplers);
        hashmap_destroy(wctx->vaos);
        hashmap_destroy(wctx->queries);
        hashmap_destroy(wctx->transform_feedbacks);
        uniformmap_destroy(wctx->uniform_locations);
        free(wctx);
        return NULL;
    }
    
    /* Initialize default state */
    wctx->active_texture = GL_TEXTURE0;
    wctx->clear_color[0] = 0.0f;
    wctx->clear_color[1] = 0.0f;
    wctx->clear_color[2] = 0.0f;
    wctx->clear_color[3] = 0.0f;
    wctx->clear_depth = 1.0f;
    wctx->clear_stencil = 0;
    wctx->depth_mask = GL_TRUE;
    wctx->depth_func = GL_LESS;
    wctx->cull_face_mode = GL_BACK;
    wctx->front_face = GL_CCW;
    wctx->blend_src_rgb = GL_ONE;
    wctx->blend_dst_rgb = GL_ZERO;
    wctx->blend_src_alpha = GL_ONE;
    wctx->blend_dst_alpha = GL_ZERO;
    wctx->blend_equation_rgb = GL_FUNC_ADD;
    wctx->blend_equation_alpha = GL_FUNC_ADD;
    wctx->unpack_alignment = 4;
    wctx->pack_alignment = 4;
    wctx->last_error = GL_NO_ERROR;
    wctx->context_lost = GL_FALSE;
    
    return wctx;
}

/* ============================================================================
 * WebGL Constants Registration
 * ============================================================================ */

static void register_webgl_constants(JSContext *ctx, JSValue proto) {
    /* Boolean values */
    JS_SetPropertyStr(ctx, proto, "FALSE", JS_NewInt32(ctx, GL_FALSE));
    JS_SetPropertyStr(ctx, proto, "TRUE", JS_NewInt32(ctx, GL_TRUE));
    
    /* Data types */
    JS_SetPropertyStr(ctx, proto, "BYTE", JS_NewInt32(ctx, GL_BYTE));
    JS_SetPropertyStr(ctx, proto, "UNSIGNED_BYTE", JS_NewInt32(ctx, GL_UNSIGNED_BYTE));
    JS_SetPropertyStr(ctx, proto, "SHORT", JS_NewInt32(ctx, GL_SHORT));
    JS_SetPropertyStr(ctx, proto, "UNSIGNED_SHORT", JS_NewInt32(ctx, GL_UNSIGNED_SHORT));
    JS_SetPropertyStr(ctx, proto, "INT", JS_NewInt32(ctx, GL_INT));
    JS_SetPropertyStr(ctx, proto, "UNSIGNED_INT", JS_NewInt32(ctx, GL_UNSIGNED_INT));
    JS_SetPropertyStr(ctx, proto, "FLOAT", JS_NewInt32(ctx, GL_FLOAT));
    JS_SetPropertyStr(ctx, proto, "HALF_FLOAT", JS_NewInt32(ctx, GL_HALF_FLOAT));
    
    /* Primitives */
    JS_SetPropertyStr(ctx, proto, "POINTS", JS_NewInt32(ctx, GL_POINTS));
    JS_SetPropertyStr(ctx, proto, "LINES", JS_NewInt32(ctx, GL_LINES));
    JS_SetPropertyStr(ctx, proto, "LINE_LOOP", JS_NewInt32(ctx, GL_LINE_LOOP));
    JS_SetPropertyStr(ctx, proto, "LINE_STRIP", JS_NewInt32(ctx, GL_LINE_STRIP));
    JS_SetPropertyStr(ctx, proto, "TRIANGLES", JS_NewInt32(ctx, GL_TRIANGLES));
    JS_SetPropertyStr(ctx, proto, "TRIANGLE_STRIP", JS_NewInt32(ctx, GL_TRIANGLE_STRIP));
    JS_SetPropertyStr(ctx, proto, "TRIANGLE_FAN", JS_NewInt32(ctx, GL_TRIANGLE_FAN));
    
    /* Clear buffer bits */
    JS_SetPropertyStr(ctx, proto, "DEPTH_BUFFER_BIT", JS_NewInt32(ctx, GL_DEPTH_BUFFER_BIT));
    JS_SetPropertyStr(ctx, proto, "STENCIL_BUFFER_BIT", JS_NewInt32(ctx, GL_STENCIL_BUFFER_BIT));
    JS_SetPropertyStr(ctx, proto, "COLOR_BUFFER_BIT", JS_NewInt32(ctx, GL_COLOR_BUFFER_BIT));
    
    /* Enable/Disable caps */
    JS_SetPropertyStr(ctx, proto, "CULL_FACE", JS_NewInt32(ctx, GL_CULL_FACE));
    JS_SetPropertyStr(ctx, proto, "DEPTH_TEST", JS_NewInt32(ctx, GL_DEPTH_TEST));
    JS_SetPropertyStr(ctx, proto, "STENCIL_TEST", JS_NewInt32(ctx, GL_STENCIL_TEST));
    JS_SetPropertyStr(ctx, proto, "DITHER", JS_NewInt32(ctx, GL_DITHER));
    JS_SetPropertyStr(ctx, proto, "BLEND", JS_NewInt32(ctx, GL_BLEND));
    JS_SetPropertyStr(ctx, proto, "SCISSOR_TEST", JS_NewInt32(ctx, GL_SCISSOR_TEST));
    JS_SetPropertyStr(ctx, proto, "POLYGON_OFFSET_FILL", JS_NewInt32(ctx, GL_POLYGON_OFFSET_FILL));
    JS_SetPropertyStr(ctx, proto, "SAMPLE_ALPHA_TO_COVERAGE", JS_NewInt32(ctx, GL_SAMPLE_ALPHA_TO_COVERAGE));
    JS_SetPropertyStr(ctx, proto, "SAMPLE_COVERAGE", JS_NewInt32(ctx, GL_SAMPLE_COVERAGE));
    
    /* Blend functions */
    JS_SetPropertyStr(ctx, proto, "ZERO", JS_NewInt32(ctx, GL_ZERO));
    JS_SetPropertyStr(ctx, proto, "ONE", JS_NewInt32(ctx, GL_ONE));
    JS_SetPropertyStr(ctx, proto, "SRC_COLOR", JS_NewInt32(ctx, GL_SRC_COLOR));
    JS_SetPropertyStr(ctx, proto, "ONE_MINUS_SRC_COLOR", JS_NewInt32(ctx, GL_ONE_MINUS_SRC_COLOR));
    JS_SetPropertyStr(ctx, proto, "SRC_ALPHA", JS_NewInt32(ctx, GL_SRC_ALPHA));
    JS_SetPropertyStr(ctx, proto, "ONE_MINUS_SRC_ALPHA", JS_NewInt32(ctx, GL_ONE_MINUS_SRC_ALPHA));
    JS_SetPropertyStr(ctx, proto, "DST_ALPHA", JS_NewInt32(ctx, GL_DST_ALPHA));
    JS_SetPropertyStr(ctx, proto, "ONE_MINUS_DST_ALPHA", JS_NewInt32(ctx, GL_ONE_MINUS_DST_ALPHA));
    JS_SetPropertyStr(ctx, proto, "DST_COLOR", JS_NewInt32(ctx, GL_DST_COLOR));
    JS_SetPropertyStr(ctx, proto, "ONE_MINUS_DST_COLOR", JS_NewInt32(ctx, GL_ONE_MINUS_DST_COLOR));
    JS_SetPropertyStr(ctx, proto, "SRC_ALPHA_SATURATE", JS_NewInt32(ctx, GL_SRC_ALPHA_SATURATE));
    JS_SetPropertyStr(ctx, proto, "CONSTANT_COLOR", JS_NewInt32(ctx, GL_CONSTANT_COLOR));
    JS_SetPropertyStr(ctx, proto, "ONE_MINUS_CONSTANT_COLOR", JS_NewInt32(ctx, GL_ONE_MINUS_CONSTANT_COLOR));
    JS_SetPropertyStr(ctx, proto, "CONSTANT_ALPHA", JS_NewInt32(ctx, GL_CONSTANT_ALPHA));
    JS_SetPropertyStr(ctx, proto, "ONE_MINUS_CONSTANT_ALPHA", JS_NewInt32(ctx, GL_ONE_MINUS_CONSTANT_ALPHA));
    
    /* Blend equations */
    JS_SetPropertyStr(ctx, proto, "FUNC_ADD", JS_NewInt32(ctx, GL_FUNC_ADD));
    JS_SetPropertyStr(ctx, proto, "FUNC_SUBTRACT", JS_NewInt32(ctx, GL_FUNC_SUBTRACT));
    JS_SetPropertyStr(ctx, proto, "FUNC_REVERSE_SUBTRACT", JS_NewInt32(ctx, GL_FUNC_REVERSE_SUBTRACT));
    JS_SetPropertyStr(ctx, proto, "MIN", JS_NewInt32(ctx, GL_MIN));
    JS_SetPropertyStr(ctx, proto, "MAX", JS_NewInt32(ctx, GL_MAX));
    
    /* Buffer targets */
    JS_SetPropertyStr(ctx, proto, "ARRAY_BUFFER", JS_NewInt32(ctx, GL_ARRAY_BUFFER));
    JS_SetPropertyStr(ctx, proto, "ELEMENT_ARRAY_BUFFER", JS_NewInt32(ctx, GL_ELEMENT_ARRAY_BUFFER));
    JS_SetPropertyStr(ctx, proto, "UNIFORM_BUFFER", JS_NewInt32(ctx, GL_UNIFORM_BUFFER));
    JS_SetPropertyStr(ctx, proto, "PIXEL_PACK_BUFFER", JS_NewInt32(ctx, GL_PIXEL_PACK_BUFFER));
    JS_SetPropertyStr(ctx, proto, "PIXEL_UNPACK_BUFFER", JS_NewInt32(ctx, GL_PIXEL_UNPACK_BUFFER));
    JS_SetPropertyStr(ctx, proto, "COPY_READ_BUFFER", JS_NewInt32(ctx, GL_COPY_READ_BUFFER));
    JS_SetPropertyStr(ctx, proto, "COPY_WRITE_BUFFER", JS_NewInt32(ctx, GL_COPY_WRITE_BUFFER));
    JS_SetPropertyStr(ctx, proto, "TRANSFORM_FEEDBACK_BUFFER", JS_NewInt32(ctx, GL_TRANSFORM_FEEDBACK_BUFFER));
    
    /* Buffer usage */
    JS_SetPropertyStr(ctx, proto, "STREAM_DRAW", JS_NewInt32(ctx, GL_STREAM_DRAW));
    JS_SetPropertyStr(ctx, proto, "STREAM_READ", JS_NewInt32(ctx, GL_STREAM_READ));
    JS_SetPropertyStr(ctx, proto, "STREAM_COPY", JS_NewInt32(ctx, GL_STREAM_COPY));
    JS_SetPropertyStr(ctx, proto, "STATIC_DRAW", JS_NewInt32(ctx, GL_STATIC_DRAW));
    JS_SetPropertyStr(ctx, proto, "STATIC_READ", JS_NewInt32(ctx, GL_STATIC_READ));
    JS_SetPropertyStr(ctx, proto, "STATIC_COPY", JS_NewInt32(ctx, GL_STATIC_COPY));
    JS_SetPropertyStr(ctx, proto, "DYNAMIC_DRAW", JS_NewInt32(ctx, GL_DYNAMIC_DRAW));
    JS_SetPropertyStr(ctx, proto, "DYNAMIC_READ", JS_NewInt32(ctx, GL_DYNAMIC_READ));
    JS_SetPropertyStr(ctx, proto, "DYNAMIC_COPY", JS_NewInt32(ctx, GL_DYNAMIC_COPY));
    
    /* Texture targets */
    JS_SetPropertyStr(ctx, proto, "TEXTURE_2D", JS_NewInt32(ctx, GL_TEXTURE_2D));
    JS_SetPropertyStr(ctx, proto, "TEXTURE_3D", JS_NewInt32(ctx, GL_TEXTURE_3D));
    JS_SetPropertyStr(ctx, proto, "TEXTURE_CUBE_MAP", JS_NewInt32(ctx, GL_TEXTURE_CUBE_MAP));
    JS_SetPropertyStr(ctx, proto, "TEXTURE_2D_ARRAY", JS_NewInt32(ctx, GL_TEXTURE_2D_ARRAY));
    JS_SetPropertyStr(ctx, proto, "TEXTURE_CUBE_MAP_POSITIVE_X", JS_NewInt32(ctx, GL_TEXTURE_CUBE_MAP_POSITIVE_X));
    JS_SetPropertyStr(ctx, proto, "TEXTURE_CUBE_MAP_NEGATIVE_X", JS_NewInt32(ctx, GL_TEXTURE_CUBE_MAP_NEGATIVE_X));
    JS_SetPropertyStr(ctx, proto, "TEXTURE_CUBE_MAP_POSITIVE_Y", JS_NewInt32(ctx, GL_TEXTURE_CUBE_MAP_POSITIVE_Y));
    JS_SetPropertyStr(ctx, proto, "TEXTURE_CUBE_MAP_NEGATIVE_Y", JS_NewInt32(ctx, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y));
    JS_SetPropertyStr(ctx, proto, "TEXTURE_CUBE_MAP_POSITIVE_Z", JS_NewInt32(ctx, GL_TEXTURE_CUBE_MAP_POSITIVE_Z));
    JS_SetPropertyStr(ctx, proto, "TEXTURE_CUBE_MAP_NEGATIVE_Z", JS_NewInt32(ctx, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z));
    
    /* Texture parameters */
    JS_SetPropertyStr(ctx, proto, "TEXTURE_MAG_FILTER", JS_NewInt32(ctx, GL_TEXTURE_MAG_FILTER));
    JS_SetPropertyStr(ctx, proto, "TEXTURE_MIN_FILTER", JS_NewInt32(ctx, GL_TEXTURE_MIN_FILTER));
    JS_SetPropertyStr(ctx, proto, "TEXTURE_WRAP_S", JS_NewInt32(ctx, GL_TEXTURE_WRAP_S));
    JS_SetPropertyStr(ctx, proto, "TEXTURE_WRAP_T", JS_NewInt32(ctx, GL_TEXTURE_WRAP_T));
    JS_SetPropertyStr(ctx, proto, "TEXTURE_WRAP_R", JS_NewInt32(ctx, GL_TEXTURE_WRAP_R));
    JS_SetPropertyStr(ctx, proto, "TEXTURE_MIN_LOD", JS_NewInt32(ctx, GL_TEXTURE_MIN_LOD));
    JS_SetPropertyStr(ctx, proto, "TEXTURE_MAX_LOD", JS_NewInt32(ctx, GL_TEXTURE_MAX_LOD));
    JS_SetPropertyStr(ctx, proto, "TEXTURE_BASE_LEVEL", JS_NewInt32(ctx, GL_TEXTURE_BASE_LEVEL));
    JS_SetPropertyStr(ctx, proto, "TEXTURE_MAX_LEVEL", JS_NewInt32(ctx, GL_TEXTURE_MAX_LEVEL));
    JS_SetPropertyStr(ctx, proto, "TEXTURE_COMPARE_MODE", JS_NewInt32(ctx, GL_TEXTURE_COMPARE_MODE));
    JS_SetPropertyStr(ctx, proto, "TEXTURE_COMPARE_FUNC", JS_NewInt32(ctx, GL_TEXTURE_COMPARE_FUNC));
    
    /* Texture filter modes */
    JS_SetPropertyStr(ctx, proto, "NEAREST", JS_NewInt32(ctx, GL_NEAREST));
    JS_SetPropertyStr(ctx, proto, "LINEAR", JS_NewInt32(ctx, GL_LINEAR));
    JS_SetPropertyStr(ctx, proto, "NEAREST_MIPMAP_NEAREST", JS_NewInt32(ctx, GL_NEAREST_MIPMAP_NEAREST));
    JS_SetPropertyStr(ctx, proto, "LINEAR_MIPMAP_NEAREST", JS_NewInt32(ctx, GL_LINEAR_MIPMAP_NEAREST));
    JS_SetPropertyStr(ctx, proto, "NEAREST_MIPMAP_LINEAR", JS_NewInt32(ctx, GL_NEAREST_MIPMAP_LINEAR));
    JS_SetPropertyStr(ctx, proto, "LINEAR_MIPMAP_LINEAR", JS_NewInt32(ctx, GL_LINEAR_MIPMAP_LINEAR));
    
    /* Texture wrap modes */
    JS_SetPropertyStr(ctx, proto, "REPEAT", JS_NewInt32(ctx, GL_REPEAT));
    JS_SetPropertyStr(ctx, proto, "CLAMP_TO_EDGE", JS_NewInt32(ctx, GL_CLAMP_TO_EDGE));
    JS_SetPropertyStr(ctx, proto, "MIRRORED_REPEAT", JS_NewInt32(ctx, GL_MIRRORED_REPEAT));
    
    /* Pixel formats */
    JS_SetPropertyStr(ctx, proto, "DEPTH_COMPONENT", JS_NewInt32(ctx, GL_DEPTH_COMPONENT));
    JS_SetPropertyStr(ctx, proto, "DEPTH_STENCIL", JS_NewInt32(ctx, GL_DEPTH_STENCIL));
    JS_SetPropertyStr(ctx, proto, "RED", JS_NewInt32(ctx, GL_RED));
    JS_SetPropertyStr(ctx, proto, "RG", JS_NewInt32(ctx, GL_RG));
    JS_SetPropertyStr(ctx, proto, "RGB", JS_NewInt32(ctx, GL_RGB));
    JS_SetPropertyStr(ctx, proto, "RGBA", JS_NewInt32(ctx, GL_RGBA));
    JS_SetPropertyStr(ctx, proto, "LUMINANCE", JS_NewInt32(ctx, GL_LUMINANCE));
    JS_SetPropertyStr(ctx, proto, "LUMINANCE_ALPHA", JS_NewInt32(ctx, GL_LUMINANCE_ALPHA));
    JS_SetPropertyStr(ctx, proto, "ALPHA", JS_NewInt32(ctx, GL_ALPHA));
    JS_SetPropertyStr(ctx, proto, "RED_INTEGER", JS_NewInt32(ctx, GL_RED_INTEGER));
    JS_SetPropertyStr(ctx, proto, "RG_INTEGER", JS_NewInt32(ctx, GL_RG_INTEGER));
    JS_SetPropertyStr(ctx, proto, "RGB_INTEGER", JS_NewInt32(ctx, GL_RGB_INTEGER));
    JS_SetPropertyStr(ctx, proto, "RGBA_INTEGER", JS_NewInt32(ctx, GL_RGBA_INTEGER));
    
    /* Internal formats */
    JS_SetPropertyStr(ctx, proto, "R8", JS_NewInt32(ctx, GL_R8));
    JS_SetPropertyStr(ctx, proto, "R16F", JS_NewInt32(ctx, GL_R16F));
    JS_SetPropertyStr(ctx, proto, "R32F", JS_NewInt32(ctx, GL_R32F));
    JS_SetPropertyStr(ctx, proto, "R8UI", JS_NewInt32(ctx, GL_R8UI));
    JS_SetPropertyStr(ctx, proto, "RG8", JS_NewInt32(ctx, GL_RG8));
    JS_SetPropertyStr(ctx, proto, "RG16F", JS_NewInt32(ctx, GL_RG16F));
    JS_SetPropertyStr(ctx, proto, "RG32F", JS_NewInt32(ctx, GL_RG32F));
    JS_SetPropertyStr(ctx, proto, "RG8UI", JS_NewInt32(ctx, GL_RG8UI));
    JS_SetPropertyStr(ctx, proto, "RGB8", JS_NewInt32(ctx, GL_RGB8));
    JS_SetPropertyStr(ctx, proto, "SRGB8", JS_NewInt32(ctx, GL_SRGB8));
    JS_SetPropertyStr(ctx, proto, "RGB565", JS_NewInt32(ctx, 0x8D62));  /* Not in gl.h */
    JS_SetPropertyStr(ctx, proto, "R11F_G11F_B10F", JS_NewInt32(ctx, GL_R11F_G11F_B10F));
    JS_SetPropertyStr(ctx, proto, "RGB9_E5", JS_NewInt32(ctx, GL_RGB9_E5));
    JS_SetPropertyStr(ctx, proto, "RGB16F", JS_NewInt32(ctx, GL_RGB16F));
    JS_SetPropertyStr(ctx, proto, "RGB32F", JS_NewInt32(ctx, GL_RGB32F));
    JS_SetPropertyStr(ctx, proto, "RGB8UI", JS_NewInt32(ctx, GL_RGB8UI));
    JS_SetPropertyStr(ctx, proto, "RGBA8", JS_NewInt32(ctx, GL_RGBA8));
    JS_SetPropertyStr(ctx, proto, "SRGB8_ALPHA8", JS_NewInt32(ctx, GL_SRGB8_ALPHA8));
    JS_SetPropertyStr(ctx, proto, "RGB5_A1", JS_NewInt32(ctx, GL_RGB5_A1));
    JS_SetPropertyStr(ctx, proto, "RGBA4", JS_NewInt32(ctx, GL_RGBA4));
    JS_SetPropertyStr(ctx, proto, "RGB10_A2", JS_NewInt32(ctx, GL_RGB10_A2));
    JS_SetPropertyStr(ctx, proto, "RGBA16F", JS_NewInt32(ctx, GL_RGBA16F));
    JS_SetPropertyStr(ctx, proto, "RGBA32F", JS_NewInt32(ctx, GL_RGBA32F));
    JS_SetPropertyStr(ctx, proto, "RGBA8UI", JS_NewInt32(ctx, GL_RGBA8UI));
    
    /* Depth/stencil formats */
    JS_SetPropertyStr(ctx, proto, "DEPTH_COMPONENT16", JS_NewInt32(ctx, GL_DEPTH_COMPONENT16));
    JS_SetPropertyStr(ctx, proto, "DEPTH_COMPONENT24", JS_NewInt32(ctx, GL_DEPTH_COMPONENT24));
    JS_SetPropertyStr(ctx, proto, "DEPTH_COMPONENT32F", JS_NewInt32(ctx, GL_DEPTH_COMPONENT32F));
    JS_SetPropertyStr(ctx, proto, "DEPTH24_STENCIL8", JS_NewInt32(ctx, GL_DEPTH24_STENCIL8));
    JS_SetPropertyStr(ctx, proto, "DEPTH32F_STENCIL8", JS_NewInt32(ctx, GL_DEPTH32F_STENCIL8));
    JS_SetPropertyStr(ctx, proto, "STENCIL_INDEX8", JS_NewInt32(ctx, GL_STENCIL_INDEX8));
    
    /* Framebuffer */
    JS_SetPropertyStr(ctx, proto, "FRAMEBUFFER", JS_NewInt32(ctx, GL_FRAMEBUFFER));
    JS_SetPropertyStr(ctx, proto, "READ_FRAMEBUFFER", JS_NewInt32(ctx, GL_READ_FRAMEBUFFER));
    JS_SetPropertyStr(ctx, proto, "DRAW_FRAMEBUFFER", JS_NewInt32(ctx, GL_DRAW_FRAMEBUFFER));
    JS_SetPropertyStr(ctx, proto, "RENDERBUFFER", JS_NewInt32(ctx, GL_RENDERBUFFER));
    
    /* Framebuffer attachments */
    JS_SetPropertyStr(ctx, proto, "COLOR_ATTACHMENT0", JS_NewInt32(ctx, GL_COLOR_ATTACHMENT0));
    JS_SetPropertyStr(ctx, proto, "COLOR_ATTACHMENT1", JS_NewInt32(ctx, GL_COLOR_ATTACHMENT1));
    JS_SetPropertyStr(ctx, proto, "COLOR_ATTACHMENT2", JS_NewInt32(ctx, GL_COLOR_ATTACHMENT2));
    JS_SetPropertyStr(ctx, proto, "COLOR_ATTACHMENT3", JS_NewInt32(ctx, GL_COLOR_ATTACHMENT3));
    JS_SetPropertyStr(ctx, proto, "COLOR_ATTACHMENT4", JS_NewInt32(ctx, GL_COLOR_ATTACHMENT4));
    JS_SetPropertyStr(ctx, proto, "COLOR_ATTACHMENT5", JS_NewInt32(ctx, GL_COLOR_ATTACHMENT5));
    JS_SetPropertyStr(ctx, proto, "COLOR_ATTACHMENT6", JS_NewInt32(ctx, GL_COLOR_ATTACHMENT6));
    JS_SetPropertyStr(ctx, proto, "COLOR_ATTACHMENT7", JS_NewInt32(ctx, GL_COLOR_ATTACHMENT7));
    JS_SetPropertyStr(ctx, proto, "COLOR_ATTACHMENT8", JS_NewInt32(ctx, GL_COLOR_ATTACHMENT8));
    JS_SetPropertyStr(ctx, proto, "COLOR_ATTACHMENT9", JS_NewInt32(ctx, GL_COLOR_ATTACHMENT9));
    JS_SetPropertyStr(ctx, proto, "COLOR_ATTACHMENT10", JS_NewInt32(ctx, GL_COLOR_ATTACHMENT10));
    JS_SetPropertyStr(ctx, proto, "COLOR_ATTACHMENT11", JS_NewInt32(ctx, GL_COLOR_ATTACHMENT11));
    JS_SetPropertyStr(ctx, proto, "COLOR_ATTACHMENT12", JS_NewInt32(ctx, GL_COLOR_ATTACHMENT12));
    JS_SetPropertyStr(ctx, proto, "COLOR_ATTACHMENT13", JS_NewInt32(ctx, GL_COLOR_ATTACHMENT13));
    JS_SetPropertyStr(ctx, proto, "COLOR_ATTACHMENT14", JS_NewInt32(ctx, GL_COLOR_ATTACHMENT14));
    JS_SetPropertyStr(ctx, proto, "COLOR_ATTACHMENT15", JS_NewInt32(ctx, GL_COLOR_ATTACHMENT15));
    JS_SetPropertyStr(ctx, proto, "DEPTH_ATTACHMENT", JS_NewInt32(ctx, GL_DEPTH_ATTACHMENT));
    JS_SetPropertyStr(ctx, proto, "STENCIL_ATTACHMENT", JS_NewInt32(ctx, GL_STENCIL_ATTACHMENT));
    JS_SetPropertyStr(ctx, proto, "DEPTH_STENCIL_ATTACHMENT", JS_NewInt32(ctx, GL_DEPTH_STENCIL_ATTACHMENT));
    
    /* Framebuffer status */
    JS_SetPropertyStr(ctx, proto, "FRAMEBUFFER_COMPLETE", JS_NewInt32(ctx, GL_FRAMEBUFFER_COMPLETE));
    JS_SetPropertyStr(ctx, proto, "FRAMEBUFFER_INCOMPLETE_ATTACHMENT", JS_NewInt32(ctx, GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT));
    JS_SetPropertyStr(ctx, proto, "FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT", JS_NewInt32(ctx, GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT));
    JS_SetPropertyStr(ctx, proto, "FRAMEBUFFER_UNSUPPORTED", JS_NewInt32(ctx, GL_FRAMEBUFFER_UNSUPPORTED));
    JS_SetPropertyStr(ctx, proto, "FRAMEBUFFER_INCOMPLETE_MULTISAMPLE", JS_NewInt32(ctx, GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE));
    
    /* Shaders */
    JS_SetPropertyStr(ctx, proto, "FRAGMENT_SHADER", JS_NewInt32(ctx, GL_FRAGMENT_SHADER));
    JS_SetPropertyStr(ctx, proto, "VERTEX_SHADER", JS_NewInt32(ctx, GL_VERTEX_SHADER));
    JS_SetPropertyStr(ctx, proto, "COMPILE_STATUS", JS_NewInt32(ctx, GL_COMPILE_STATUS));
    JS_SetPropertyStr(ctx, proto, "LINK_STATUS", JS_NewInt32(ctx, GL_LINK_STATUS));
    JS_SetPropertyStr(ctx, proto, "VALIDATE_STATUS", JS_NewInt32(ctx, GL_VALIDATE_STATUS));
    JS_SetPropertyStr(ctx, proto, "ATTACHED_SHADERS", JS_NewInt32(ctx, GL_ATTACHED_SHADERS));
    JS_SetPropertyStr(ctx, proto, "ACTIVE_UNIFORMS", JS_NewInt32(ctx, GL_ACTIVE_UNIFORMS));
    JS_SetPropertyStr(ctx, proto, "ACTIVE_ATTRIBUTES", JS_NewInt32(ctx, GL_ACTIVE_ATTRIBUTES));
    JS_SetPropertyStr(ctx, proto, "SHADER_TYPE", JS_NewInt32(ctx, GL_SHADER_TYPE));
    JS_SetPropertyStr(ctx, proto, "DELETE_STATUS", JS_NewInt32(ctx, 0x8B80));
    JS_SetPropertyStr(ctx, proto, "CURRENT_PROGRAM", JS_NewInt32(ctx, GL_CURRENT_PROGRAM));
    
    /* Comparison functions */
    JS_SetPropertyStr(ctx, proto, "NEVER", JS_NewInt32(ctx, GL_NEVER));
    JS_SetPropertyStr(ctx, proto, "LESS", JS_NewInt32(ctx, GL_LESS));
    JS_SetPropertyStr(ctx, proto, "EQUAL", JS_NewInt32(ctx, GL_EQUAL));
    JS_SetPropertyStr(ctx, proto, "LEQUAL", JS_NewInt32(ctx, GL_LEQUAL));
    JS_SetPropertyStr(ctx, proto, "GREATER", JS_NewInt32(ctx, GL_GREATER));
    JS_SetPropertyStr(ctx, proto, "NOTEQUAL", JS_NewInt32(ctx, GL_NOTEQUAL));
    JS_SetPropertyStr(ctx, proto, "GEQUAL", JS_NewInt32(ctx, GL_GEQUAL));
    JS_SetPropertyStr(ctx, proto, "ALWAYS", JS_NewInt32(ctx, GL_ALWAYS));
    
    /* Stencil operations */
    JS_SetPropertyStr(ctx, proto, "KEEP", JS_NewInt32(ctx, GL_KEEP));
    JS_SetPropertyStr(ctx, proto, "REPLACE", JS_NewInt32(ctx, GL_REPLACE));
    JS_SetPropertyStr(ctx, proto, "INCR", JS_NewInt32(ctx, GL_INCR));
    JS_SetPropertyStr(ctx, proto, "DECR", JS_NewInt32(ctx, GL_DECR));
    JS_SetPropertyStr(ctx, proto, "INVERT", JS_NewInt32(ctx, GL_INVERT));
    JS_SetPropertyStr(ctx, proto, "INCR_WRAP", JS_NewInt32(ctx, GL_INCR_WRAP));
    JS_SetPropertyStr(ctx, proto, "DECR_WRAP", JS_NewInt32(ctx, GL_DECR_WRAP));
    
    /* Face culling */
    JS_SetPropertyStr(ctx, proto, "FRONT", JS_NewInt32(ctx, GL_FRONT));
    JS_SetPropertyStr(ctx, proto, "BACK", JS_NewInt32(ctx, GL_BACK));
    JS_SetPropertyStr(ctx, proto, "FRONT_AND_BACK", JS_NewInt32(ctx, GL_FRONT_AND_BACK));
    JS_SetPropertyStr(ctx, proto, "CW", JS_NewInt32(ctx, GL_CW));
    JS_SetPropertyStr(ctx, proto, "CCW", JS_NewInt32(ctx, GL_CCW));
    
    /* Queries/Gets */
    JS_SetPropertyStr(ctx, proto, "VENDOR", JS_NewInt32(ctx, GL_VENDOR));
    JS_SetPropertyStr(ctx, proto, "RENDERER", JS_NewInt32(ctx, GL_RENDERER));
    JS_SetPropertyStr(ctx, proto, "VERSION", JS_NewInt32(ctx, GL_VERSION));
    JS_SetPropertyStr(ctx, proto, "SHADING_LANGUAGE_VERSION", JS_NewInt32(ctx, GL_SHADING_LANGUAGE_VERSION));
    JS_SetPropertyStr(ctx, proto, "MAX_TEXTURE_SIZE", JS_NewInt32(ctx, GL_MAX_TEXTURE_SIZE));
    JS_SetPropertyStr(ctx, proto, "MAX_CUBE_MAP_TEXTURE_SIZE", JS_NewInt32(ctx, GL_MAX_CUBE_MAP_TEXTURE_SIZE));
    JS_SetPropertyStr(ctx, proto, "MAX_TEXTURE_IMAGE_UNITS", JS_NewInt32(ctx, GL_MAX_TEXTURE_IMAGE_UNITS));
    JS_SetPropertyStr(ctx, proto, "MAX_VERTEX_TEXTURE_IMAGE_UNITS", JS_NewInt32(ctx, GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS));
    JS_SetPropertyStr(ctx, proto, "MAX_COMBINED_TEXTURE_IMAGE_UNITS", JS_NewInt32(ctx, GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS));
    JS_SetPropertyStr(ctx, proto, "MAX_VERTEX_ATTRIBS", JS_NewInt32(ctx, GL_MAX_VERTEX_ATTRIBS));
    JS_SetPropertyStr(ctx, proto, "MAX_VERTEX_UNIFORM_COMPONENTS", JS_NewInt32(ctx, GL_MAX_VERTEX_UNIFORM_COMPONENTS));
    JS_SetPropertyStr(ctx, proto, "MAX_FRAGMENT_UNIFORM_COMPONENTS", JS_NewInt32(ctx, GL_MAX_FRAGMENT_UNIFORM_COMPONENTS));
    JS_SetPropertyStr(ctx, proto, "MAX_RENDERBUFFER_SIZE", JS_NewInt32(ctx, GL_MAX_RENDERBUFFER_SIZE));
    JS_SetPropertyStr(ctx, proto, "VIEWPORT", JS_NewInt32(ctx, GL_VIEWPORT));
    JS_SetPropertyStr(ctx, proto, "SCISSOR_BOX", JS_NewInt32(ctx, GL_SCISSOR_BOX));
    
    /* Error codes */
    JS_SetPropertyStr(ctx, proto, "NO_ERROR", JS_NewInt32(ctx, GL_NO_ERROR));
    JS_SetPropertyStr(ctx, proto, "INVALID_ENUM", JS_NewInt32(ctx, GL_INVALID_ENUM));
    JS_SetPropertyStr(ctx, proto, "INVALID_VALUE", JS_NewInt32(ctx, GL_INVALID_VALUE));
    JS_SetPropertyStr(ctx, proto, "INVALID_OPERATION", JS_NewInt32(ctx, GL_INVALID_OPERATION));
    JS_SetPropertyStr(ctx, proto, "OUT_OF_MEMORY", JS_NewInt32(ctx, GL_OUT_OF_MEMORY));
    JS_SetPropertyStr(ctx, proto, "INVALID_FRAMEBUFFER_OPERATION", JS_NewInt32(ctx, GL_INVALID_FRAMEBUFFER_OPERATION));
    JS_SetPropertyStr(ctx, proto, "CONTEXT_LOST_WEBGL", JS_NewInt32(ctx, 0x9242));
    
    /* Pixel store */
    JS_SetPropertyStr(ctx, proto, "UNPACK_ALIGNMENT", JS_NewInt32(ctx, GL_UNPACK_ALIGNMENT));
    JS_SetPropertyStr(ctx, proto, "PACK_ALIGNMENT", JS_NewInt32(ctx, GL_PACK_ALIGNMENT));
    JS_SetPropertyStr(ctx, proto, "UNPACK_ROW_LENGTH", JS_NewInt32(ctx, GL_UNPACK_ROW_LENGTH));
    JS_SetPropertyStr(ctx, proto, "UNPACK_SKIP_ROWS", JS_NewInt32(ctx, GL_UNPACK_SKIP_ROWS));
    JS_SetPropertyStr(ctx, proto, "UNPACK_SKIP_PIXELS", JS_NewInt32(ctx, GL_UNPACK_SKIP_PIXELS));
    JS_SetPropertyStr(ctx, proto, "UNPACK_SKIP_IMAGES", JS_NewInt32(ctx, GL_UNPACK_SKIP_IMAGES));
    JS_SetPropertyStr(ctx, proto, "UNPACK_IMAGE_HEIGHT", JS_NewInt32(ctx, GL_UNPACK_IMAGE_HEIGHT));
    JS_SetPropertyStr(ctx, proto, "PACK_ROW_LENGTH", JS_NewInt32(ctx, GL_PACK_ROW_LENGTH));
    JS_SetPropertyStr(ctx, proto, "PACK_SKIP_ROWS", JS_NewInt32(ctx, GL_PACK_SKIP_ROWS));
    JS_SetPropertyStr(ctx, proto, "PACK_SKIP_PIXELS", JS_NewInt32(ctx, GL_PACK_SKIP_PIXELS));
    JS_SetPropertyStr(ctx, proto, "UNPACK_FLIP_Y_WEBGL", JS_NewInt32(ctx, 0x9240));
    JS_SetPropertyStr(ctx, proto, "UNPACK_PREMULTIPLY_ALPHA_WEBGL", JS_NewInt32(ctx, 0x9241));
    JS_SetPropertyStr(ctx, proto, "UNPACK_COLORSPACE_CONVERSION_WEBGL", JS_NewInt32(ctx, 0x9243));
    
    /* Texture units */
    JS_SetPropertyStr(ctx, proto, "TEXTURE0", JS_NewInt32(ctx, GL_TEXTURE0));
    JS_SetPropertyStr(ctx, proto, "TEXTURE1", JS_NewInt32(ctx, GL_TEXTURE1));
    JS_SetPropertyStr(ctx, proto, "TEXTURE2", JS_NewInt32(ctx, GL_TEXTURE2));
    JS_SetPropertyStr(ctx, proto, "TEXTURE3", JS_NewInt32(ctx, GL_TEXTURE3));
    JS_SetPropertyStr(ctx, proto, "TEXTURE4", JS_NewInt32(ctx, GL_TEXTURE4));
    JS_SetPropertyStr(ctx, proto, "TEXTURE5", JS_NewInt32(ctx, GL_TEXTURE5));
    JS_SetPropertyStr(ctx, proto, "TEXTURE6", JS_NewInt32(ctx, GL_TEXTURE6));
    JS_SetPropertyStr(ctx, proto, "TEXTURE7", JS_NewInt32(ctx, GL_TEXTURE7));
    JS_SetPropertyStr(ctx, proto, "TEXTURE8", JS_NewInt32(ctx, GL_TEXTURE8));
    JS_SetPropertyStr(ctx, proto, "TEXTURE9", JS_NewInt32(ctx, GL_TEXTURE9));
    JS_SetPropertyStr(ctx, proto, "TEXTURE10", JS_NewInt32(ctx, GL_TEXTURE10));
    JS_SetPropertyStr(ctx, proto, "TEXTURE11", JS_NewInt32(ctx, GL_TEXTURE11));
    JS_SetPropertyStr(ctx, proto, "TEXTURE12", JS_NewInt32(ctx, GL_TEXTURE12));
    JS_SetPropertyStr(ctx, proto, "TEXTURE13", JS_NewInt32(ctx, GL_TEXTURE13));
    JS_SetPropertyStr(ctx, proto, "TEXTURE14", JS_NewInt32(ctx, GL_TEXTURE14));
    JS_SetPropertyStr(ctx, proto, "TEXTURE15", JS_NewInt32(ctx, GL_TEXTURE15));
    JS_SetPropertyStr(ctx, proto, "TEXTURE16", JS_NewInt32(ctx, GL_TEXTURE16));
    JS_SetPropertyStr(ctx, proto, "TEXTURE17", JS_NewInt32(ctx, GL_TEXTURE17));
    JS_SetPropertyStr(ctx, proto, "TEXTURE18", JS_NewInt32(ctx, GL_TEXTURE18));
    JS_SetPropertyStr(ctx, proto, "TEXTURE19", JS_NewInt32(ctx, GL_TEXTURE19));
    JS_SetPropertyStr(ctx, proto, "TEXTURE20", JS_NewInt32(ctx, GL_TEXTURE20));
    JS_SetPropertyStr(ctx, proto, "TEXTURE21", JS_NewInt32(ctx, GL_TEXTURE21));
    JS_SetPropertyStr(ctx, proto, "TEXTURE22", JS_NewInt32(ctx, GL_TEXTURE22));
    JS_SetPropertyStr(ctx, proto, "TEXTURE23", JS_NewInt32(ctx, GL_TEXTURE23));
    JS_SetPropertyStr(ctx, proto, "TEXTURE24", JS_NewInt32(ctx, GL_TEXTURE24));
    JS_SetPropertyStr(ctx, proto, "TEXTURE25", JS_NewInt32(ctx, GL_TEXTURE25));
    JS_SetPropertyStr(ctx, proto, "TEXTURE26", JS_NewInt32(ctx, GL_TEXTURE26));
    JS_SetPropertyStr(ctx, proto, "TEXTURE27", JS_NewInt32(ctx, GL_TEXTURE27));
    JS_SetPropertyStr(ctx, proto, "TEXTURE28", JS_NewInt32(ctx, GL_TEXTURE28));
    JS_SetPropertyStr(ctx, proto, "TEXTURE29", JS_NewInt32(ctx, GL_TEXTURE29));
    JS_SetPropertyStr(ctx, proto, "TEXTURE30", JS_NewInt32(ctx, GL_TEXTURE30));
    JS_SetPropertyStr(ctx, proto, "TEXTURE31", JS_NewInt32(ctx, GL_TEXTURE31));
    JS_SetPropertyStr(ctx, proto, "ACTIVE_TEXTURE", JS_NewInt32(ctx, GL_ACTIVE_TEXTURE));
    
    /* WebGL 2 specific */
    JS_SetPropertyStr(ctx, proto, "READ_BUFFER", JS_NewInt32(ctx, 0x0C02));
    JS_SetPropertyStr(ctx, proto, "UNPACK_ROW_LENGTH", JS_NewInt32(ctx, GL_UNPACK_ROW_LENGTH));
    JS_SetPropertyStr(ctx, proto, "UNPACK_SKIP_ROWS", JS_NewInt32(ctx, GL_UNPACK_SKIP_ROWS));
    JS_SetPropertyStr(ctx, proto, "UNPACK_SKIP_PIXELS", JS_NewInt32(ctx, GL_UNPACK_SKIP_PIXELS));
    JS_SetPropertyStr(ctx, proto, "PACK_ROW_LENGTH", JS_NewInt32(ctx, GL_PACK_ROW_LENGTH));
    JS_SetPropertyStr(ctx, proto, "PACK_SKIP_ROWS", JS_NewInt32(ctx, GL_PACK_SKIP_ROWS));
    JS_SetPropertyStr(ctx, proto, "PACK_SKIP_PIXELS", JS_NewInt32(ctx, GL_PACK_SKIP_PIXELS));
    JS_SetPropertyStr(ctx, proto, "COLOR", JS_NewInt32(ctx, GL_COLOR));
    JS_SetPropertyStr(ctx, proto, "DEPTH", JS_NewInt32(ctx, GL_DEPTH));
    JS_SetPropertyStr(ctx, proto, "STENCIL", JS_NewInt32(ctx, GL_STENCIL));
    JS_SetPropertyStr(ctx, proto, "MAX_3D_TEXTURE_SIZE", JS_NewInt32(ctx, GL_MAX_3D_TEXTURE_SIZE));
    JS_SetPropertyStr(ctx, proto, "MAX_ARRAY_TEXTURE_LAYERS", JS_NewInt32(ctx, GL_MAX_ARRAY_TEXTURE_LAYERS));
    JS_SetPropertyStr(ctx, proto, "MAX_DRAW_BUFFERS", JS_NewInt32(ctx, GL_MAX_DRAW_BUFFERS));
    JS_SetPropertyStr(ctx, proto, "DRAW_BUFFER0", JS_NewInt32(ctx, 0x8825));
    JS_SetPropertyStr(ctx, proto, "DRAW_BUFFER1", JS_NewInt32(ctx, 0x8826));
    JS_SetPropertyStr(ctx, proto, "DRAW_BUFFER2", JS_NewInt32(ctx, 0x8827));
    JS_SetPropertyStr(ctx, proto, "DRAW_BUFFER3", JS_NewInt32(ctx, 0x8828));
    JS_SetPropertyStr(ctx, proto, "DRAW_BUFFER4", JS_NewInt32(ctx, 0x8829));
    JS_SetPropertyStr(ctx, proto, "DRAW_BUFFER5", JS_NewInt32(ctx, 0x882A));
    JS_SetPropertyStr(ctx, proto, "DRAW_BUFFER6", JS_NewInt32(ctx, 0x882B));
    JS_SetPropertyStr(ctx, proto, "DRAW_BUFFER7", JS_NewInt32(ctx, 0x882C));
    JS_SetPropertyStr(ctx, proto, "MAX_COLOR_ATTACHMENTS", JS_NewInt32(ctx, GL_MAX_COLOR_ATTACHMENTS));
    JS_SetPropertyStr(ctx, proto, "MAX_SAMPLES", JS_NewInt32(ctx, GL_MAX_SAMPLES));
    
    /* Uniform buffer object */
    JS_SetPropertyStr(ctx, proto, "MAX_UNIFORM_BUFFER_BINDINGS", JS_NewInt32(ctx, GL_MAX_UNIFORM_BUFFER_BINDINGS));
    JS_SetPropertyStr(ctx, proto, "MAX_UNIFORM_BLOCK_SIZE", JS_NewInt32(ctx, GL_MAX_UNIFORM_BLOCK_SIZE));
    JS_SetPropertyStr(ctx, proto, "UNIFORM_BUFFER_BINDING", JS_NewInt32(ctx, 0x8A28));
    JS_SetPropertyStr(ctx, proto, "UNIFORM_BLOCK_BINDING", JS_NewInt32(ctx, 0x8A3F));
    JS_SetPropertyStr(ctx, proto, "UNIFORM_BLOCK_DATA_SIZE", JS_NewInt32(ctx, 0x8A40));
    JS_SetPropertyStr(ctx, proto, "UNIFORM_BLOCK_ACTIVE_UNIFORMS", JS_NewInt32(ctx, 0x8A42));
    
    /* Sync objects */
    JS_SetPropertyStr(ctx, proto, "SYNC_GPU_COMMANDS_COMPLETE", JS_NewInt32(ctx, GL_SYNC_GPU_COMMANDS_COMPLETE));
    JS_SetPropertyStr(ctx, proto, "ALREADY_SIGNALED", JS_NewInt32(ctx, GL_ALREADY_SIGNALED));
    JS_SetPropertyStr(ctx, proto, "TIMEOUT_EXPIRED", JS_NewInt32(ctx, GL_TIMEOUT_EXPIRED));
    JS_SetPropertyStr(ctx, proto, "CONDITION_SATISFIED", JS_NewInt32(ctx, GL_CONDITION_SATISFIED));
    JS_SetPropertyStr(ctx, proto, "WAIT_FAILED", JS_NewInt32(ctx, GL_WAIT_FAILED));
    JS_SetPropertyStr(ctx, proto, "SYNC_FLUSH_COMMANDS_BIT", JS_NewInt32(ctx, GL_SYNC_FLUSH_COMMANDS_BIT));
    
    /* Transform feedback */
    JS_SetPropertyStr(ctx, proto, "TRANSFORM_FEEDBACK", JS_NewInt32(ctx, 0x8E22));
    JS_SetPropertyStr(ctx, proto, "TRANSFORM_FEEDBACK_PAUSED", JS_NewInt32(ctx, 0x8E23));
    JS_SetPropertyStr(ctx, proto, "TRANSFORM_FEEDBACK_ACTIVE", JS_NewInt32(ctx, 0x8E24));
    JS_SetPropertyStr(ctx, proto, "TRANSFORM_FEEDBACK_BINDING", JS_NewInt32(ctx, 0x8E25));
    JS_SetPropertyStr(ctx, proto, "INTERLEAVED_ATTRIBS", JS_NewInt32(ctx, 0x8C8C));
    JS_SetPropertyStr(ctx, proto, "SEPARATE_ATTRIBS", JS_NewInt32(ctx, 0x8C8D));
    
    /* Queries */
    JS_SetPropertyStr(ctx, proto, "ANY_SAMPLES_PASSED", JS_NewInt32(ctx, 0x8C2F));
    JS_SetPropertyStr(ctx, proto, "ANY_SAMPLES_PASSED_CONSERVATIVE", JS_NewInt32(ctx, 0x8D6A));
    JS_SetPropertyStr(ctx, proto, "TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN", JS_NewInt32(ctx, 0x8C88));
    JS_SetPropertyStr(ctx, proto, "QUERY_RESULT", JS_NewInt32(ctx, 0x8866));
    JS_SetPropertyStr(ctx, proto, "QUERY_RESULT_AVAILABLE", JS_NewInt32(ctx, 0x8867));
    
    /* Samplers */
    JS_SetPropertyStr(ctx, proto, "SAMPLER_BINDING", JS_NewInt32(ctx, GL_SAMPLER_BINDING));
    JS_SetPropertyStr(ctx, proto, "COMPARE_REF_TO_TEXTURE", JS_NewInt32(ctx, GL_COMPARE_REF_TO_TEXTURE));
    
    /* VAO */
    JS_SetPropertyStr(ctx, proto, "VERTEX_ARRAY_BINDING", JS_NewInt32(ctx, GL_VERTEX_ARRAY_BINDING));
}

/* ============================================================================
 * Stub Methods (to be implemented in Phase 2+)
 * ============================================================================ */

/* getError */
static JSValue js_webgl_getError(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    GLenum err = wctx->last_error;
    wctx->last_error = GL_NO_ERROR;
    
    /* Also check for any pending GL error */
    if (err == GL_NO_ERROR) {
        err = glGetError();
    }
    
    return JS_NewInt32(ctx, err);
}

/* isContextLost */
static JSValue js_webgl_isContextLost(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    return JS_NewBool(ctx, wctx->context_lost);
}

/* getContextAttributes */
static JSValue js_webgl_getContextAttributes(JSContext *ctx, JSValueConst this_val,
                                              int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_NULL;
    if (wctx->context_lost) return JS_NULL;
    
    JSValue attrs = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, attrs, "alpha", JS_TRUE);
    JS_SetPropertyStr(ctx, attrs, "depth", JS_TRUE);
    JS_SetPropertyStr(ctx, attrs, "stencil", JS_FALSE);
    JS_SetPropertyStr(ctx, attrs, "antialias", JS_TRUE);
    JS_SetPropertyStr(ctx, attrs, "premultipliedAlpha", JS_TRUE);
    JS_SetPropertyStr(ctx, attrs, "preserveDrawingBuffer", JS_FALSE);
    JS_SetPropertyStr(ctx, attrs, "powerPreference", JS_NewString(ctx, "default"));
    JS_SetPropertyStr(ctx, attrs, "failIfMajorPerformanceCaveat", JS_FALSE);
    JS_SetPropertyStr(ctx, attrs, "desynchronized", JS_FALSE);
    return attrs;
}

/* getSupportedExtensions */
static JSValue js_webgl_getSupportedExtensions(JSContext *ctx, JSValueConst this_val,
                                                int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx || wctx->context_lost) return JS_NULL;
    
    /* Return an empty array for now - extensions to be added later */
    return JS_NewArray(ctx);
}

/* getExtension */
static JSValue js_webgl_getExtension(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv) {
    /* No extensions implemented yet */
    return JS_NULL;
}

/* ============================================================================
 * Context Registration  
 * ============================================================================ */

/* Prototype object for WebGL context */
static JSValue webgl_ctx_proto;

static JSValue js_canvas_getContext(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "context id required");
    }
    
    const char *kind = JS_ToCString(ctx, argv[0]);
    if (!kind) return JS_EXCEPTION;

    JSValue result = JS_UNDEFINED;

    if (strcmp(kind, "webgl") == 0 || strcmp(kind, "experimental-webgl") == 0 ||
        strcmp(kind, "webgl2") == 0) {
        
        WebGLContext *wctx = create_webgl_context();
        if (!wctx) {
            JS_FreeCString(ctx, kind);
            return JS_ThrowInternalError(ctx, "out of memory");
        }
        
        JSValue obj = JS_NewObjectClass(ctx, js_webgl_ctx_class_id);
        if (JS_IsException(obj)) {
            /* Cleanup wctx */
            hashmap_destroy(wctx->buffers);
            hashmap_destroy(wctx->textures);
            hashmap_destroy(wctx->programs);
            hashmap_destroy(wctx->shaders);
            hashmap_destroy(wctx->framebuffers);
            hashmap_destroy(wctx->renderbuffers);
            hashmap_destroy(wctx->samplers);
            hashmap_destroy(wctx->vaos);
            hashmap_destroy(wctx->queries);
            hashmap_destroy(wctx->transform_feedbacks);
            uniformmap_destroy(wctx->uniform_locations);
            free(wctx);
            JS_FreeCString(ctx, kind);
            return JS_EXCEPTION;
        }
        
        JS_SetOpaque(obj, wctx);
        JS_SetPrototype(ctx, obj, webgl_ctx_proto);
        result = obj;
    }

    JS_FreeCString(ctx, kind);
    return result;
}

/* Register all JS classes */
static void register_webgl_classes(JSContext *ctx) {
    JSRuntime *rt = JS_GetRuntime(ctx);
    
    /* Register class IDs */
    JS_NewClassID(&js_webgl_ctx_class_id);
    JS_NewClassID(&js_webgl_buffer_class_id);
    JS_NewClassID(&js_webgl_texture_class_id);
    JS_NewClassID(&js_webgl_program_class_id);
    JS_NewClassID(&js_webgl_shader_class_id);
    JS_NewClassID(&js_webgl_framebuffer_class_id);
    JS_NewClassID(&js_webgl_renderbuffer_class_id);
    JS_NewClassID(&js_webgl_uniform_location_class_id);
    JS_NewClassID(&js_webgl_active_info_class_id);
    JS_NewClassID(&js_webgl_shader_precision_format_class_id);
    JS_NewClassID(&js_webgl_sampler_class_id);
    JS_NewClassID(&js_webgl_vao_class_id);
    JS_NewClassID(&js_webgl_query_class_id);
    JS_NewClassID(&js_webgl_sync_class_id);
    JS_NewClassID(&js_webgl_transform_feedback_class_id);
    
    /* Register classes with runtime */
    JS_NewClass(rt, js_webgl_ctx_class_id, &js_webgl_ctx_class);
    JS_NewClass(rt, js_webgl_buffer_class_id, &js_webgl_buffer_class);
    JS_NewClass(rt, js_webgl_texture_class_id, &js_webgl_texture_class);
    JS_NewClass(rt, js_webgl_program_class_id, &js_webgl_program_class);
    JS_NewClass(rt, js_webgl_shader_class_id, &js_webgl_shader_class);
    JS_NewClass(rt, js_webgl_framebuffer_class_id, &js_webgl_framebuffer_class);
    JS_NewClass(rt, js_webgl_renderbuffer_class_id, &js_webgl_renderbuffer_class);
    JS_NewClass(rt, js_webgl_uniform_location_class_id, &js_webgl_uniform_location_class);
    JS_NewClass(rt, js_webgl_active_info_class_id, &js_webgl_active_info_class);
    JS_NewClass(rt, js_webgl_shader_precision_format_class_id, &js_webgl_shader_precision_format_class);
    JS_NewClass(rt, js_webgl_sampler_class_id, &js_webgl_sampler_class);
    JS_NewClass(rt, js_webgl_vao_class_id, &js_webgl_vao_class);
    JS_NewClass(rt, js_webgl_query_class_id, &js_webgl_query_class);
    JS_NewClass(rt, js_webgl_sync_class_id, &js_webgl_sync_class);
    JS_NewClass(rt, js_webgl_transform_feedback_class_id, &js_webgl_transform_feedback_class);
}

void minirend_webgl_register(JSContext *ctx, MinirendApp *app) {
    (void)app;
    
    /* Register all WebGL classes */
    register_webgl_classes(ctx);
    
    /* Create context prototype with all methods and constants */
    webgl_ctx_proto = JS_NewObject(ctx);
    
    /* Register base methods */
    JS_SetPropertyStr(ctx, webgl_ctx_proto, "getError",
                      JS_NewCFunction(ctx, js_webgl_getError, "getError", 0));
    JS_SetPropertyStr(ctx, webgl_ctx_proto, "isContextLost",
                      JS_NewCFunction(ctx, js_webgl_isContextLost, "isContextLost", 0));
    JS_SetPropertyStr(ctx, webgl_ctx_proto, "getContextAttributes",
                      JS_NewCFunction(ctx, js_webgl_getContextAttributes, "getContextAttributes", 0));
    JS_SetPropertyStr(ctx, webgl_ctx_proto, "getSupportedExtensions",
                      JS_NewCFunction(ctx, js_webgl_getSupportedExtensions, "getSupportedExtensions", 0));
    JS_SetPropertyStr(ctx, webgl_ctx_proto, "getExtension",
                      JS_NewCFunction(ctx, js_webgl_getExtension, "getExtension", 1));
    
    /* Register all WebGL constants */
    register_webgl_constants(ctx, webgl_ctx_proto);
    
    /* Set class prototype */
    JS_SetClassProto(ctx, js_webgl_ctx_class_id, webgl_ctx_proto);
    
    /* Patch canvas prototype to add getContext */
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JSValue document   = JS_GetPropertyStr(ctx, global_obj, "document");
    JSValue body       = JS_GetPropertyStr(ctx, document, "body");
    JSValue canvas     = JS_GetPropertyStr(ctx, body, "prototype_canvas");

    if (JS_IsUndefined(canvas)) {
        /* Create a prototype object for canvas-like elements */
        canvas = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, canvas, "getContext",
                          JS_NewCFunction(ctx, js_canvas_getContext, "getContext", 1));
        JS_SetPropertyStr(ctx, body, "prototype_canvas", JS_DupValue(ctx, canvas));
    }

    JS_FreeValue(ctx, canvas);
    JS_FreeValue(ctx, body);
    JS_FreeValue(ctx, document);
    JS_FreeValue(ctx, global_obj);
}
