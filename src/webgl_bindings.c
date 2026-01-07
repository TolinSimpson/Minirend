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

/* Pointer map for resources that aren't GLuint (e.g., GLsync) */
typedef struct PtrMapEntry {
    uint32_t  key;
    uintptr_t value;
    uint8_t   occupied;
} PtrMapEntry;

typedef struct PtrMap {
    PtrMapEntry *entries;
    uint32_t     capacity;
    uint32_t     count;
    uint32_t     next_handle;
} PtrMap;

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

static PtrMap *ptrmap_create(void) {
    PtrMap *map = (PtrMap *)calloc(1, sizeof(PtrMap));
    if (!map) return NULL;

    map->entries = (PtrMapEntry *)calloc(HASHMAP_INITIAL_CAPACITY, sizeof(PtrMapEntry));
    if (!map->entries) {
        free(map);
        return NULL;
    }

    map->capacity = HASHMAP_INITIAL_CAPACITY;
    map->count = 0;
    map->next_handle = 1;
    return map;
}

static void ptrmap_destroy(PtrMap *map) {
    if (map) {
        free(map->entries);
        free(map);
    }
}

static int ptrmap_resize(PtrMap *map, uint32_t new_capacity) {
    PtrMapEntry *old_entries = map->entries;
    uint32_t old_capacity = map->capacity;

    map->entries = (PtrMapEntry *)calloc(new_capacity, sizeof(PtrMapEntry));
    if (!map->entries) {
        map->entries = old_entries;
        return 0;
    }

    map->capacity = new_capacity;
    map->count = 0;

    for (uint32_t i = 0; i < old_capacity; i++) {
        if (old_entries[i].occupied) {
            uint32_t idx = hashmap_hash(old_entries[i].key, new_capacity);
            while (map->entries[idx].occupied) idx = (idx + 1) % new_capacity;
            map->entries[idx] = old_entries[i];
            map->count++;
        }
    }

    free(old_entries);
    return 1;
}

static int ptrmap_put(PtrMap *map, uint32_t key, uintptr_t value) {
    if (!map) return 0;

    if ((float)map->count / (float)map->capacity > HASHMAP_LOAD_FACTOR) {
        if (!ptrmap_resize(map, map->capacity * 2)) return 0;
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

static uintptr_t ptrmap_get(PtrMap *map, uint32_t key) {
    if (!map || key == 0) return 0;

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

static uintptr_t ptrmap_remove(PtrMap *map, uint32_t key) {
    if (!map || key == 0) return 0;

    uint32_t idx = hashmap_hash(key, map->capacity);
    uint32_t start = idx;
    while (map->entries[idx].occupied) {
        if (map->entries[idx].key == key) {
            uintptr_t val = map->entries[idx].value;
            map->entries[idx].occupied = 0;
            map->entries[idx].key = 0;
            map->entries[idx].value = 0;
            map->count--;

            /* Rehash entries that might be in the same cluster */
            uint32_t next = (idx + 1) % map->capacity;
            while (map->entries[next].occupied) {
                PtrMapEntry e = map->entries[next];
                map->entries[next].occupied = 0;
                map->count--;
                ptrmap_put(map, e.key, e.value);
                next = (next + 1) % map->capacity;
            }

            return val;
        }
        idx = (idx + 1) % map->capacity;
        if (idx == start) break;
    }
    return 0;
}

static uint32_t ptrmap_alloc(PtrMap *map, uintptr_t value) {
    if (!map) return 0;
    uint32_t handle = map->next_handle++;
    if (!ptrmap_put(map, handle, value)) return 0;
    return handle;
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
    PtrMap     *syncs; /* JS handle -> GLsync (pointer) */
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
        /* Delete GL resources owned by this context */
        if (ctx->buffers) {
            for (uint32_t i = 0; i < ctx->buffers->capacity; i++) {
                if (ctx->buffers->entries[i].occupied) {
                    GLuint id = (GLuint)ctx->buffers->entries[i].value;
                    if (id) glDeleteBuffers(1, &id);
                }
            }
        }
        if (ctx->textures) {
            for (uint32_t i = 0; i < ctx->textures->capacity; i++) {
                if (ctx->textures->entries[i].occupied) {
                    GLuint id = (GLuint)ctx->textures->entries[i].value;
                    if (id) glDeleteTextures(1, &id);
                }
            }
        }
        if (ctx->programs) {
            for (uint32_t i = 0; i < ctx->programs->capacity; i++) {
                if (ctx->programs->entries[i].occupied) {
                    GLuint id = (GLuint)ctx->programs->entries[i].value;
                    if (id) glDeleteProgram(id);
                }
            }
        }
        if (ctx->shaders) {
            for (uint32_t i = 0; i < ctx->shaders->capacity; i++) {
                if (ctx->shaders->entries[i].occupied) {
                    GLuint id = (GLuint)ctx->shaders->entries[i].value;
                    if (id) glDeleteShader(id);
                }
            }
        }
        if (ctx->framebuffers) {
            for (uint32_t i = 0; i < ctx->framebuffers->capacity; i++) {
                if (ctx->framebuffers->entries[i].occupied) {
                    GLuint id = (GLuint)ctx->framebuffers->entries[i].value;
                    if (id) glDeleteFramebuffers(1, &id);
                }
            }
        }
        if (ctx->renderbuffers) {
            for (uint32_t i = 0; i < ctx->renderbuffers->capacity; i++) {
                if (ctx->renderbuffers->entries[i].occupied) {
                    GLuint id = (GLuint)ctx->renderbuffers->entries[i].value;
                    if (id) glDeleteRenderbuffers(1, &id);
                }
            }
        }
        if (ctx->samplers) {
            for (uint32_t i = 0; i < ctx->samplers->capacity; i++) {
                if (ctx->samplers->entries[i].occupied) {
                    GLuint id = (GLuint)ctx->samplers->entries[i].value;
                    if (id) glDeleteSamplers(1, &id);
                }
            }
        }
        if (ctx->vaos) {
            for (uint32_t i = 0; i < ctx->vaos->capacity; i++) {
                if (ctx->vaos->entries[i].occupied) {
                    GLuint id = (GLuint)ctx->vaos->entries[i].value;
                    if (id) glDeleteVertexArrays(1, &id);
                }
            }
        }
        if (ctx->queries) {
            for (uint32_t i = 0; i < ctx->queries->capacity; i++) {
                if (ctx->queries->entries[i].occupied) {
                    GLuint id = (GLuint)ctx->queries->entries[i].value;
                    if (id) glDeleteQueries(1, &id);
                }
            }
        }
        if (ctx->transform_feedbacks) {
            for (uint32_t i = 0; i < ctx->transform_feedbacks->capacity; i++) {
                if (ctx->transform_feedbacks->entries[i].occupied) {
                    GLuint id = (GLuint)ctx->transform_feedbacks->entries[i].value;
                    if (id) glDeleteTransformFeedbacks(1, &id);
                }
            }
        }
        if (ctx->syncs) {
            for (uint32_t i = 0; i < ctx->syncs->capacity; i++) {
                if (ctx->syncs->entries[i].occupied) {
                    GLsync s = (GLsync)ctx->syncs->entries[i].value;
                    if (s) glDeleteSync(s);
                }
            }
        }

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
        ptrmap_destroy(ctx->syncs);
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
    wctx->syncs = ptrmap_create();
    wctx->uniform_locations = uniformmap_create();
    
    if (!wctx->buffers || !wctx->textures || !wctx->programs || 
        !wctx->shaders || !wctx->framebuffers || !wctx->renderbuffers ||
        !wctx->samplers || !wctx->vaos || !wctx->queries ||
        !wctx->transform_feedbacks || !wctx->syncs || !wctx->uniform_locations) {
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
        ptrmap_destroy(wctx->syncs);
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
    
    /* ========================================================================
     * Additional WebGL 2.0 Constants (Phase 8)
     * ======================================================================== */
    
    /* Additional data types */
    JS_SetPropertyStr(ctx, proto, "UNSIGNED_INT_2_10_10_10_REV", JS_NewInt32(ctx, GL_UNSIGNED_INT_2_10_10_10_REV));
    JS_SetPropertyStr(ctx, proto, "UNSIGNED_INT_10F_11F_11F_REV", JS_NewInt32(ctx, GL_UNSIGNED_INT_10F_11F_11F_REV));
    JS_SetPropertyStr(ctx, proto, "UNSIGNED_INT_5_9_9_9_REV", JS_NewInt32(ctx, GL_UNSIGNED_INT_5_9_9_9_REV));
    JS_SetPropertyStr(ctx, proto, "UNSIGNED_INT_24_8", JS_NewInt32(ctx, GL_UNSIGNED_INT_24_8));
    JS_SetPropertyStr(ctx, proto, "FLOAT_32_UNSIGNED_INT_24_8_REV", JS_NewInt32(ctx, 0x8DAD));
    
    /* Additional internal formats - signed normalized */
    JS_SetPropertyStr(ctx, proto, "R8_SNORM", JS_NewInt32(ctx, GL_R8_SNORM));
    JS_SetPropertyStr(ctx, proto, "RG8_SNORM", JS_NewInt32(ctx, GL_RG8_SNORM));
    JS_SetPropertyStr(ctx, proto, "RGB8_SNORM", JS_NewInt32(ctx, GL_RGB8_SNORM));
    JS_SetPropertyStr(ctx, proto, "RGBA8_SNORM", JS_NewInt32(ctx, GL_RGBA8_SNORM));
    
    /* Additional internal formats - integer */
    JS_SetPropertyStr(ctx, proto, "R8I", JS_NewInt32(ctx, GL_R8I));
    JS_SetPropertyStr(ctx, proto, "R16I", JS_NewInt32(ctx, GL_R16I));
    JS_SetPropertyStr(ctx, proto, "R16UI", JS_NewInt32(ctx, GL_R16UI));
    JS_SetPropertyStr(ctx, proto, "R32I", JS_NewInt32(ctx, GL_R32I));
    JS_SetPropertyStr(ctx, proto, "R32UI", JS_NewInt32(ctx, GL_R32UI));
    JS_SetPropertyStr(ctx, proto, "RG8I", JS_NewInt32(ctx, GL_RG8I));
    JS_SetPropertyStr(ctx, proto, "RG16I", JS_NewInt32(ctx, GL_RG16I));
    JS_SetPropertyStr(ctx, proto, "RG16UI", JS_NewInt32(ctx, GL_RG16UI));
    JS_SetPropertyStr(ctx, proto, "RG32I", JS_NewInt32(ctx, GL_RG32I));
    JS_SetPropertyStr(ctx, proto, "RG32UI", JS_NewInt32(ctx, GL_RG32UI));
    JS_SetPropertyStr(ctx, proto, "RGB8I", JS_NewInt32(ctx, GL_RGB8I));
    JS_SetPropertyStr(ctx, proto, "RGB16I", JS_NewInt32(ctx, GL_RGB16I));
    JS_SetPropertyStr(ctx, proto, "RGB16UI", JS_NewInt32(ctx, GL_RGB16UI));
    JS_SetPropertyStr(ctx, proto, "RGB32I", JS_NewInt32(ctx, GL_RGB32I));
    JS_SetPropertyStr(ctx, proto, "RGB32UI", JS_NewInt32(ctx, GL_RGB32UI));
    JS_SetPropertyStr(ctx, proto, "RGBA8I", JS_NewInt32(ctx, GL_RGBA8I));
    JS_SetPropertyStr(ctx, proto, "RGBA16I", JS_NewInt32(ctx, GL_RGBA16I));
    JS_SetPropertyStr(ctx, proto, "RGBA16UI", JS_NewInt32(ctx, GL_RGBA16UI));
    JS_SetPropertyStr(ctx, proto, "RGBA32I", JS_NewInt32(ctx, GL_RGBA32I));
    JS_SetPropertyStr(ctx, proto, "RGBA32UI", JS_NewInt32(ctx, GL_RGBA32UI));
    JS_SetPropertyStr(ctx, proto, "RGB10_A2UI", JS_NewInt32(ctx, GL_RGB10_A2UI));
    
    /* Compressed texture formats - S3TC */
    JS_SetPropertyStr(ctx, proto, "COMPRESSED_RGB_S3TC_DXT1_EXT", JS_NewInt32(ctx, GL_COMPRESSED_RGB_S3TC_DXT1_EXT));
    JS_SetPropertyStr(ctx, proto, "COMPRESSED_RGBA_S3TC_DXT1_EXT", JS_NewInt32(ctx, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT));
    JS_SetPropertyStr(ctx, proto, "COMPRESSED_RGBA_S3TC_DXT3_EXT", JS_NewInt32(ctx, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT));
    JS_SetPropertyStr(ctx, proto, "COMPRESSED_RGBA_S3TC_DXT5_EXT", JS_NewInt32(ctx, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT));
    JS_SetPropertyStr(ctx, proto, "COMPRESSED_SRGB_S3TC_DXT1_EXT", JS_NewInt32(ctx, GL_COMPRESSED_SRGB_S3TC_DXT1_EXT));
    JS_SetPropertyStr(ctx, proto, "COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT", JS_NewInt32(ctx, GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT));
    JS_SetPropertyStr(ctx, proto, "COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT", JS_NewInt32(ctx, GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT));
    JS_SetPropertyStr(ctx, proto, "COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT", JS_NewInt32(ctx, GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT));
    
    /* Compressed texture formats - RGTC */
    JS_SetPropertyStr(ctx, proto, "COMPRESSED_RED_RGTC1", JS_NewInt32(ctx, GL_COMPRESSED_RED_RGTC1));
    JS_SetPropertyStr(ctx, proto, "COMPRESSED_SIGNED_RED_RGTC1", JS_NewInt32(ctx, GL_COMPRESSED_SIGNED_RED_RGTC1));
    JS_SetPropertyStr(ctx, proto, "COMPRESSED_RG_RGTC2", JS_NewInt32(ctx, GL_COMPRESSED_RG_RGTC2));
    JS_SetPropertyStr(ctx, proto, "COMPRESSED_SIGNED_RG_RGTC2", JS_NewInt32(ctx, GL_COMPRESSED_SIGNED_RG_RGTC2));
    
    /* Compressed texture formats - BPTC */
    JS_SetPropertyStr(ctx, proto, "COMPRESSED_RGBA_BPTC_UNORM", JS_NewInt32(ctx, GL_COMPRESSED_RGBA_BPTC_UNORM));
    JS_SetPropertyStr(ctx, proto, "COMPRESSED_SRGB_ALPHA_BPTC_UNORM", JS_NewInt32(ctx, GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM));
    JS_SetPropertyStr(ctx, proto, "COMPRESSED_RGB_BPTC_SIGNED_FLOAT", JS_NewInt32(ctx, GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT));
    JS_SetPropertyStr(ctx, proto, "COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT", JS_NewInt32(ctx, GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT));
    
    /* Compressed texture formats - ETC2/EAC (WebGL 2 standard) */
    JS_SetPropertyStr(ctx, proto, "COMPRESSED_R11_EAC", JS_NewInt32(ctx, 0x9270));
    JS_SetPropertyStr(ctx, proto, "COMPRESSED_SIGNED_R11_EAC", JS_NewInt32(ctx, 0x9271));
    JS_SetPropertyStr(ctx, proto, "COMPRESSED_RG11_EAC", JS_NewInt32(ctx, 0x9272));
    JS_SetPropertyStr(ctx, proto, "COMPRESSED_SIGNED_RG11_EAC", JS_NewInt32(ctx, 0x9273));
    JS_SetPropertyStr(ctx, proto, "COMPRESSED_RGB8_ETC2", JS_NewInt32(ctx, 0x9274));
    JS_SetPropertyStr(ctx, proto, "COMPRESSED_SRGB8_ETC2", JS_NewInt32(ctx, 0x9275));
    JS_SetPropertyStr(ctx, proto, "COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2", JS_NewInt32(ctx, 0x9276));
    JS_SetPropertyStr(ctx, proto, "COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2", JS_NewInt32(ctx, 0x9277));
    JS_SetPropertyStr(ctx, proto, "COMPRESSED_RGBA8_ETC2_EAC", JS_NewInt32(ctx, 0x9278));
    JS_SetPropertyStr(ctx, proto, "COMPRESSED_SRGB8_ALPHA8_ETC2_EAC", JS_NewInt32(ctx, 0x9279));
    
    /* Texture immutable format */
    JS_SetPropertyStr(ctx, proto, "TEXTURE_IMMUTABLE_FORMAT", JS_NewInt32(ctx, GL_TEXTURE_IMMUTABLE_FORMAT));
    JS_SetPropertyStr(ctx, proto, "TEXTURE_IMMUTABLE_LEVELS", JS_NewInt32(ctx, GL_TEXTURE_IMMUTABLE_LEVELS));
    
    /* Additional UBO constants */
    JS_SetPropertyStr(ctx, proto, "UNIFORM_BUFFER_START", JS_NewInt32(ctx, GL_UNIFORM_BUFFER_START));
    JS_SetPropertyStr(ctx, proto, "UNIFORM_BUFFER_SIZE", JS_NewInt32(ctx, GL_UNIFORM_BUFFER_SIZE));
    JS_SetPropertyStr(ctx, proto, "MAX_VERTEX_UNIFORM_BLOCKS", JS_NewInt32(ctx, GL_MAX_VERTEX_UNIFORM_BLOCKS));
    JS_SetPropertyStr(ctx, proto, "MAX_FRAGMENT_UNIFORM_BLOCKS", JS_NewInt32(ctx, GL_MAX_FRAGMENT_UNIFORM_BLOCKS));
    JS_SetPropertyStr(ctx, proto, "MAX_COMBINED_UNIFORM_BLOCKS", JS_NewInt32(ctx, GL_MAX_COMBINED_UNIFORM_BLOCKS));
    JS_SetPropertyStr(ctx, proto, "UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES", JS_NewInt32(ctx, GL_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES));
    JS_SetPropertyStr(ctx, proto, "UNIFORM_BLOCK_REFERENCED_BY_VERTEX_SHADER", JS_NewInt32(ctx, GL_UNIFORM_BLOCK_REFERENCED_BY_VERTEX_SHADER));
    JS_SetPropertyStr(ctx, proto, "UNIFORM_BLOCK_REFERENCED_BY_FRAGMENT_SHADER", JS_NewInt32(ctx, GL_UNIFORM_BLOCK_REFERENCED_BY_FRAGMENT_SHADER));
    JS_SetPropertyStr(ctx, proto, "UNIFORM_BLOCK_NAME_LENGTH", JS_NewInt32(ctx, GL_UNIFORM_BLOCK_NAME_LENGTH));
    JS_SetPropertyStr(ctx, proto, "UNIFORM_TYPE", JS_NewInt32(ctx, GL_UNIFORM_TYPE));
    JS_SetPropertyStr(ctx, proto, "UNIFORM_SIZE", JS_NewInt32(ctx, GL_UNIFORM_SIZE));
    JS_SetPropertyStr(ctx, proto, "UNIFORM_NAME_LENGTH", JS_NewInt32(ctx, GL_UNIFORM_NAME_LENGTH));
    JS_SetPropertyStr(ctx, proto, "UNIFORM_BLOCK_INDEX", JS_NewInt32(ctx, GL_UNIFORM_BLOCK_INDEX));
    JS_SetPropertyStr(ctx, proto, "UNIFORM_OFFSET", JS_NewInt32(ctx, GL_UNIFORM_OFFSET));
    JS_SetPropertyStr(ctx, proto, "UNIFORM_ARRAY_STRIDE", JS_NewInt32(ctx, GL_UNIFORM_ARRAY_STRIDE));
    JS_SetPropertyStr(ctx, proto, "UNIFORM_MATRIX_STRIDE", JS_NewInt32(ctx, GL_UNIFORM_MATRIX_STRIDE));
    JS_SetPropertyStr(ctx, proto, "UNIFORM_IS_ROW_MAJOR", JS_NewInt32(ctx, GL_UNIFORM_IS_ROW_MAJOR));
    JS_SetPropertyStr(ctx, proto, "INVALID_INDEX", JS_NewUint32(ctx, GL_INVALID_INDEX));
    
    /* Additional transform feedback constants */
    JS_SetPropertyStr(ctx, proto, "TRANSFORM_FEEDBACK_BUFFER_BINDING", JS_NewInt32(ctx, GL_TRANSFORM_FEEDBACK_BUFFER_BINDING));
    JS_SetPropertyStr(ctx, proto, "TRANSFORM_FEEDBACK_BUFFER_START", JS_NewInt32(ctx, GL_TRANSFORM_FEEDBACK_BUFFER_START));
    JS_SetPropertyStr(ctx, proto, "TRANSFORM_FEEDBACK_BUFFER_SIZE", JS_NewInt32(ctx, GL_TRANSFORM_FEEDBACK_BUFFER_SIZE));
    JS_SetPropertyStr(ctx, proto, "MAX_TRANSFORM_FEEDBACK_SEPARATE_COMPONENTS", JS_NewInt32(ctx, GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_COMPONENTS));
    JS_SetPropertyStr(ctx, proto, "MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS", JS_NewInt32(ctx, GL_MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS));
    JS_SetPropertyStr(ctx, proto, "MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS", JS_NewInt32(ctx, GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS));
    JS_SetPropertyStr(ctx, proto, "TRANSFORM_FEEDBACK_BUFFER_MODE", JS_NewInt32(ctx, 0x8C7F));
    JS_SetPropertyStr(ctx, proto, "TRANSFORM_FEEDBACK_VARYINGS", JS_NewInt32(ctx, 0x8C83));
    JS_SetPropertyStr(ctx, proto, "TRANSFORM_FEEDBACK_VARYING_MAX_LENGTH", JS_NewInt32(ctx, 0x8C76));
    
    /* Additional sync constants */
    JS_SetPropertyStr(ctx, proto, "SYNC_CONDITION", JS_NewInt32(ctx, GL_SYNC_CONDITION));
    JS_SetPropertyStr(ctx, proto, "SYNC_STATUS", JS_NewInt32(ctx, GL_SYNC_STATUS));
    JS_SetPropertyStr(ctx, proto, "SYNC_FLAGS", JS_NewInt32(ctx, GL_SYNC_FLAGS));
    JS_SetPropertyStr(ctx, proto, "SIGNALED", JS_NewInt32(ctx, GL_SIGNALED));
    JS_SetPropertyStr(ctx, proto, "UNSIGNALED", JS_NewInt32(ctx, GL_UNSIGNALED));
    JS_SetPropertyStr(ctx, proto, "OBJECT_TYPE", JS_NewInt32(ctx, 0x9112));
    JS_SetPropertyStr(ctx, proto, "SYNC_FENCE", JS_NewInt32(ctx, 0x9116));
    JS_SetPropertyStr(ctx, proto, "MAX_SERVER_WAIT_TIMEOUT", JS_NewInt32(ctx, 0x9111));
    
    /* Additional query constants */
    JS_SetPropertyStr(ctx, proto, "CURRENT_QUERY", JS_NewInt32(ctx, GL_CURRENT_QUERY));
    
    /* Renderbuffer parameters */
    JS_SetPropertyStr(ctx, proto, "RENDERBUFFER_WIDTH", JS_NewInt32(ctx, 0x8D42));
    JS_SetPropertyStr(ctx, proto, "RENDERBUFFER_HEIGHT", JS_NewInt32(ctx, 0x8D43));
    JS_SetPropertyStr(ctx, proto, "RENDERBUFFER_INTERNAL_FORMAT", JS_NewInt32(ctx, 0x8D44));
    JS_SetPropertyStr(ctx, proto, "RENDERBUFFER_RED_SIZE", JS_NewInt32(ctx, 0x8D50));
    JS_SetPropertyStr(ctx, proto, "RENDERBUFFER_GREEN_SIZE", JS_NewInt32(ctx, 0x8D51));
    JS_SetPropertyStr(ctx, proto, "RENDERBUFFER_BLUE_SIZE", JS_NewInt32(ctx, 0x8D52));
    JS_SetPropertyStr(ctx, proto, "RENDERBUFFER_ALPHA_SIZE", JS_NewInt32(ctx, 0x8D53));
    JS_SetPropertyStr(ctx, proto, "RENDERBUFFER_DEPTH_SIZE", JS_NewInt32(ctx, 0x8D54));
    JS_SetPropertyStr(ctx, proto, "RENDERBUFFER_STENCIL_SIZE", JS_NewInt32(ctx, 0x8D55));
    JS_SetPropertyStr(ctx, proto, "RENDERBUFFER_SAMPLES", JS_NewInt32(ctx, 0x8CAB));
    
    /* Framebuffer attachment parameters */
    JS_SetPropertyStr(ctx, proto, "FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE", JS_NewInt32(ctx, 0x8CD0));
    JS_SetPropertyStr(ctx, proto, "FRAMEBUFFER_ATTACHMENT_OBJECT_NAME", JS_NewInt32(ctx, 0x8CD1));
    JS_SetPropertyStr(ctx, proto, "FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL", JS_NewInt32(ctx, 0x8CD2));
    JS_SetPropertyStr(ctx, proto, "FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE", JS_NewInt32(ctx, 0x8CD3));
    JS_SetPropertyStr(ctx, proto, "FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER", JS_NewInt32(ctx, 0x8CD4));
    JS_SetPropertyStr(ctx, proto, "FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING", JS_NewInt32(ctx, 0x8210));
    JS_SetPropertyStr(ctx, proto, "FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE", JS_NewInt32(ctx, 0x8211));
    JS_SetPropertyStr(ctx, proto, "FRAMEBUFFER_ATTACHMENT_RED_SIZE", JS_NewInt32(ctx, 0x8212));
    JS_SetPropertyStr(ctx, proto, "FRAMEBUFFER_ATTACHMENT_GREEN_SIZE", JS_NewInt32(ctx, 0x8213));
    JS_SetPropertyStr(ctx, proto, "FRAMEBUFFER_ATTACHMENT_BLUE_SIZE", JS_NewInt32(ctx, 0x8214));
    JS_SetPropertyStr(ctx, proto, "FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE", JS_NewInt32(ctx, 0x8215));
    JS_SetPropertyStr(ctx, proto, "FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE", JS_NewInt32(ctx, 0x8216));
    JS_SetPropertyStr(ctx, proto, "FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE", JS_NewInt32(ctx, 0x8217));
    JS_SetPropertyStr(ctx, proto, "FRAMEBUFFER_DEFAULT", JS_NewInt32(ctx, 0x8218));
    JS_SetPropertyStr(ctx, proto, "UNSIGNED_NORMALIZED", JS_NewInt32(ctx, 0x8C17));
    JS_SetPropertyStr(ctx, proto, "FLOAT_MAT2x3", JS_NewInt32(ctx, 0x8B65));
    JS_SetPropertyStr(ctx, proto, "FLOAT_MAT2x4", JS_NewInt32(ctx, 0x8B66));
    JS_SetPropertyStr(ctx, proto, "FLOAT_MAT3x2", JS_NewInt32(ctx, 0x8B67));
    JS_SetPropertyStr(ctx, proto, "FLOAT_MAT3x4", JS_NewInt32(ctx, 0x8B68));
    JS_SetPropertyStr(ctx, proto, "FLOAT_MAT4x2", JS_NewInt32(ctx, 0x8B69));
    JS_SetPropertyStr(ctx, proto, "FLOAT_MAT4x3", JS_NewInt32(ctx, 0x8B6A));
    
    /* Additional draw buffers */
    JS_SetPropertyStr(ctx, proto, "DRAW_BUFFER8", JS_NewInt32(ctx, 0x882D));
    JS_SetPropertyStr(ctx, proto, "DRAW_BUFFER9", JS_NewInt32(ctx, 0x882E));
    JS_SetPropertyStr(ctx, proto, "DRAW_BUFFER10", JS_NewInt32(ctx, 0x882F));
    JS_SetPropertyStr(ctx, proto, "DRAW_BUFFER11", JS_NewInt32(ctx, 0x8830));
    JS_SetPropertyStr(ctx, proto, "DRAW_BUFFER12", JS_NewInt32(ctx, 0x8831));
    JS_SetPropertyStr(ctx, proto, "DRAW_BUFFER13", JS_NewInt32(ctx, 0x8832));
    JS_SetPropertyStr(ctx, proto, "DRAW_BUFFER14", JS_NewInt32(ctx, 0x8833));
    JS_SetPropertyStr(ctx, proto, "DRAW_BUFFER15", JS_NewInt32(ctx, 0x8834));
    
    /* Additional sampler types */
    JS_SetPropertyStr(ctx, proto, "SAMPLER_2D", JS_NewInt32(ctx, 0x8B5E));
    JS_SetPropertyStr(ctx, proto, "SAMPLER_3D", JS_NewInt32(ctx, 0x8B5F));
    JS_SetPropertyStr(ctx, proto, "SAMPLER_CUBE", JS_NewInt32(ctx, 0x8B60));
    JS_SetPropertyStr(ctx, proto, "SAMPLER_2D_SHADOW", JS_NewInt32(ctx, 0x8B62));
    JS_SetPropertyStr(ctx, proto, "SAMPLER_2D_ARRAY", JS_NewInt32(ctx, 0x8DC1));
    JS_SetPropertyStr(ctx, proto, "SAMPLER_2D_ARRAY_SHADOW", JS_NewInt32(ctx, 0x8DC4));
    JS_SetPropertyStr(ctx, proto, "SAMPLER_CUBE_SHADOW", JS_NewInt32(ctx, 0x8DC5));
    JS_SetPropertyStr(ctx, proto, "INT_SAMPLER_2D", JS_NewInt32(ctx, 0x8DCA));
    JS_SetPropertyStr(ctx, proto, "INT_SAMPLER_3D", JS_NewInt32(ctx, 0x8DCB));
    JS_SetPropertyStr(ctx, proto, "INT_SAMPLER_CUBE", JS_NewInt32(ctx, 0x8DCC));
    JS_SetPropertyStr(ctx, proto, "INT_SAMPLER_2D_ARRAY", JS_NewInt32(ctx, 0x8DCF));
    JS_SetPropertyStr(ctx, proto, "UNSIGNED_INT_SAMPLER_2D", JS_NewInt32(ctx, 0x8DD2));
    JS_SetPropertyStr(ctx, proto, "UNSIGNED_INT_SAMPLER_3D", JS_NewInt32(ctx, 0x8DD3));
    JS_SetPropertyStr(ctx, proto, "UNSIGNED_INT_SAMPLER_CUBE", JS_NewInt32(ctx, 0x8DD4));
    JS_SetPropertyStr(ctx, proto, "UNSIGNED_INT_SAMPLER_2D_ARRAY", JS_NewInt32(ctx, 0x8DD7));
    
    /* Uniform types */
    JS_SetPropertyStr(ctx, proto, "FLOAT_VEC2", JS_NewInt32(ctx, 0x8B50));
    JS_SetPropertyStr(ctx, proto, "FLOAT_VEC3", JS_NewInt32(ctx, 0x8B51));
    JS_SetPropertyStr(ctx, proto, "FLOAT_VEC4", JS_NewInt32(ctx, 0x8B52));
    JS_SetPropertyStr(ctx, proto, "INT_VEC2", JS_NewInt32(ctx, 0x8B53));
    JS_SetPropertyStr(ctx, proto, "INT_VEC3", JS_NewInt32(ctx, 0x8B54));
    JS_SetPropertyStr(ctx, proto, "INT_VEC4", JS_NewInt32(ctx, 0x8B55));
    JS_SetPropertyStr(ctx, proto, "BOOL", JS_NewInt32(ctx, 0x8B56));
    JS_SetPropertyStr(ctx, proto, "BOOL_VEC2", JS_NewInt32(ctx, 0x8B57));
    JS_SetPropertyStr(ctx, proto, "BOOL_VEC3", JS_NewInt32(ctx, 0x8B58));
    JS_SetPropertyStr(ctx, proto, "BOOL_VEC4", JS_NewInt32(ctx, 0x8B59));
    JS_SetPropertyStr(ctx, proto, "FLOAT_MAT2", JS_NewInt32(ctx, 0x8B5A));
    JS_SetPropertyStr(ctx, proto, "FLOAT_MAT3", JS_NewInt32(ctx, 0x8B5B));
    JS_SetPropertyStr(ctx, proto, "FLOAT_MAT4", JS_NewInt32(ctx, 0x8B5C));
    JS_SetPropertyStr(ctx, proto, "UNSIGNED_INT_VEC2", JS_NewInt32(ctx, 0x8DC6));
    JS_SetPropertyStr(ctx, proto, "UNSIGNED_INT_VEC3", JS_NewInt32(ctx, 0x8DC7));
    JS_SetPropertyStr(ctx, proto, "UNSIGNED_INT_VEC4", JS_NewInt32(ctx, 0x8DC8));
    
    /* Buffer bindings */
    JS_SetPropertyStr(ctx, proto, "ARRAY_BUFFER_BINDING", JS_NewInt32(ctx, 0x8894));
    JS_SetPropertyStr(ctx, proto, "ELEMENT_ARRAY_BUFFER_BINDING", JS_NewInt32(ctx, 0x8895));
    JS_SetPropertyStr(ctx, proto, "FRAMEBUFFER_BINDING", JS_NewInt32(ctx, GL_FRAMEBUFFER_BINDING));
    JS_SetPropertyStr(ctx, proto, "RENDERBUFFER_BINDING", JS_NewInt32(ctx, GL_RENDERBUFFER_BINDING));
    JS_SetPropertyStr(ctx, proto, "TEXTURE_BINDING_2D", JS_NewInt32(ctx, 0x8069));
    JS_SetPropertyStr(ctx, proto, "TEXTURE_BINDING_CUBE_MAP", JS_NewInt32(ctx, 0x8514));
    JS_SetPropertyStr(ctx, proto, "TEXTURE_BINDING_3D", JS_NewInt32(ctx, 0x806A));
    JS_SetPropertyStr(ctx, proto, "TEXTURE_BINDING_2D_ARRAY", JS_NewInt32(ctx, 0x8C1D));
    JS_SetPropertyStr(ctx, proto, "COPY_READ_BUFFER_BINDING", JS_NewInt32(ctx, 0x8F36));
    JS_SetPropertyStr(ctx, proto, "COPY_WRITE_BUFFER_BINDING", JS_NewInt32(ctx, 0x8F37));
    JS_SetPropertyStr(ctx, proto, "PIXEL_PACK_BUFFER_BINDING", JS_NewInt32(ctx, 0x88ED));
    JS_SetPropertyStr(ctx, proto, "PIXEL_UNPACK_BUFFER_BINDING", JS_NewInt32(ctx, 0x88EF));
    
    /* Additional state getters */
    JS_SetPropertyStr(ctx, proto, "BLEND_COLOR", JS_NewInt32(ctx, 0x8005));
    JS_SetPropertyStr(ctx, proto, "BLEND_EQUATION", JS_NewInt32(ctx, 0x8009));
    JS_SetPropertyStr(ctx, proto, "BLEND_EQUATION_RGB", JS_NewInt32(ctx, 0x8009));
    JS_SetPropertyStr(ctx, proto, "BLEND_EQUATION_ALPHA", JS_NewInt32(ctx, 0x883D));
    JS_SetPropertyStr(ctx, proto, "BLEND_SRC_RGB", JS_NewInt32(ctx, 0x80C9));
    JS_SetPropertyStr(ctx, proto, "BLEND_SRC_ALPHA", JS_NewInt32(ctx, 0x80CB));
    JS_SetPropertyStr(ctx, proto, "BLEND_DST_RGB", JS_NewInt32(ctx, 0x80C8));
    JS_SetPropertyStr(ctx, proto, "BLEND_DST_ALPHA", JS_NewInt32(ctx, 0x80CA));
    JS_SetPropertyStr(ctx, proto, "COLOR_CLEAR_VALUE", JS_NewInt32(ctx, 0x0C22));
    JS_SetPropertyStr(ctx, proto, "DEPTH_CLEAR_VALUE", JS_NewInt32(ctx, 0x0B73));
    JS_SetPropertyStr(ctx, proto, "STENCIL_CLEAR_VALUE", JS_NewInt32(ctx, 0x0B91));
    JS_SetPropertyStr(ctx, proto, "COLOR_WRITEMASK", JS_NewInt32(ctx, GL_COLOR_WRITEMASK));
    JS_SetPropertyStr(ctx, proto, "DEPTH_WRITEMASK", JS_NewInt32(ctx, GL_DEPTH_WRITEMASK));
    JS_SetPropertyStr(ctx, proto, "STENCIL_WRITEMASK", JS_NewInt32(ctx, 0x0B98));
    JS_SetPropertyStr(ctx, proto, "STENCIL_BACK_WRITEMASK", JS_NewInt32(ctx, 0x8CA5));
    JS_SetPropertyStr(ctx, proto, "STENCIL_FUNC", JS_NewInt32(ctx, 0x0B92));
    JS_SetPropertyStr(ctx, proto, "STENCIL_VALUE_MASK", JS_NewInt32(ctx, 0x0B93));
    JS_SetPropertyStr(ctx, proto, "STENCIL_REF", JS_NewInt32(ctx, 0x0B97));
    JS_SetPropertyStr(ctx, proto, "STENCIL_FAIL", JS_NewInt32(ctx, 0x0B94));
    JS_SetPropertyStr(ctx, proto, "STENCIL_PASS_DEPTH_FAIL", JS_NewInt32(ctx, 0x0B95));
    JS_SetPropertyStr(ctx, proto, "STENCIL_PASS_DEPTH_PASS", JS_NewInt32(ctx, 0x0B96));
    JS_SetPropertyStr(ctx, proto, "STENCIL_BACK_FUNC", JS_NewInt32(ctx, 0x8800));
    JS_SetPropertyStr(ctx, proto, "STENCIL_BACK_VALUE_MASK", JS_NewInt32(ctx, 0x8CA4));
    JS_SetPropertyStr(ctx, proto, "STENCIL_BACK_REF", JS_NewInt32(ctx, 0x8CA3));
    JS_SetPropertyStr(ctx, proto, "STENCIL_BACK_FAIL", JS_NewInt32(ctx, 0x8801));
    JS_SetPropertyStr(ctx, proto, "STENCIL_BACK_PASS_DEPTH_FAIL", JS_NewInt32(ctx, 0x8802));
    JS_SetPropertyStr(ctx, proto, "STENCIL_BACK_PASS_DEPTH_PASS", JS_NewInt32(ctx, 0x8803));
    JS_SetPropertyStr(ctx, proto, "DEPTH_FUNC", JS_NewInt32(ctx, 0x0B74));
    JS_SetPropertyStr(ctx, proto, "DEPTH_RANGE", JS_NewInt32(ctx, 0x0B70));
    JS_SetPropertyStr(ctx, proto, "FRONT_FACE", JS_NewInt32(ctx, 0x0B46));
    JS_SetPropertyStr(ctx, proto, "CULL_FACE_MODE", JS_NewInt32(ctx, 0x0B45));
    JS_SetPropertyStr(ctx, proto, "ALIASED_POINT_SIZE_RANGE", JS_NewInt32(ctx, 0x846D));
    JS_SetPropertyStr(ctx, proto, "ALIASED_LINE_WIDTH_RANGE", JS_NewInt32(ctx, 0x846E));
    JS_SetPropertyStr(ctx, proto, "LINE_WIDTH", JS_NewInt32(ctx, 0x0B21));
    JS_SetPropertyStr(ctx, proto, "POLYGON_OFFSET_FACTOR", JS_NewInt32(ctx, 0x8038));
    JS_SetPropertyStr(ctx, proto, "POLYGON_OFFSET_UNITS", JS_NewInt32(ctx, 0x2A00));
    JS_SetPropertyStr(ctx, proto, "SAMPLE_BUFFERS", JS_NewInt32(ctx, 0x80A8));
    JS_SetPropertyStr(ctx, proto, "SAMPLES", JS_NewInt32(ctx, 0x80A9));
    JS_SetPropertyStr(ctx, proto, "SAMPLE_COVERAGE_VALUE", JS_NewInt32(ctx, 0x80AA));
    JS_SetPropertyStr(ctx, proto, "SAMPLE_COVERAGE_INVERT", JS_NewInt32(ctx, 0x80AB));
    JS_SetPropertyStr(ctx, proto, "GENERATE_MIPMAP_HINT", JS_NewInt32(ctx, 0x8192));
    JS_SetPropertyStr(ctx, proto, "SUBPIXEL_BITS", JS_NewInt32(ctx, 0x0D50));
    JS_SetPropertyStr(ctx, proto, "MAX_VIEWPORT_DIMS", JS_NewInt32(ctx, 0x0D3A));
    JS_SetPropertyStr(ctx, proto, "RED_BITS", JS_NewInt32(ctx, 0x0D52));
    JS_SetPropertyStr(ctx, proto, "GREEN_BITS", JS_NewInt32(ctx, 0x0D53));
    JS_SetPropertyStr(ctx, proto, "BLUE_BITS", JS_NewInt32(ctx, 0x0D54));
    JS_SetPropertyStr(ctx, proto, "ALPHA_BITS", JS_NewInt32(ctx, 0x0D55));
    JS_SetPropertyStr(ctx, proto, "DEPTH_BITS", JS_NewInt32(ctx, 0x0D56));
    JS_SetPropertyStr(ctx, proto, "STENCIL_BITS", JS_NewInt32(ctx, 0x0D57));
    
    /* Vertex attribute getters */
    JS_SetPropertyStr(ctx, proto, "VERTEX_ATTRIB_ARRAY_ENABLED", JS_NewInt32(ctx, 0x8622));
    JS_SetPropertyStr(ctx, proto, "VERTEX_ATTRIB_ARRAY_SIZE", JS_NewInt32(ctx, 0x8623));
    JS_SetPropertyStr(ctx, proto, "VERTEX_ATTRIB_ARRAY_STRIDE", JS_NewInt32(ctx, 0x8624));
    JS_SetPropertyStr(ctx, proto, "VERTEX_ATTRIB_ARRAY_TYPE", JS_NewInt32(ctx, 0x8625));
    JS_SetPropertyStr(ctx, proto, "VERTEX_ATTRIB_ARRAY_NORMALIZED", JS_NewInt32(ctx, 0x886A));
    JS_SetPropertyStr(ctx, proto, "VERTEX_ATTRIB_ARRAY_POINTER", JS_NewInt32(ctx, 0x8645));
    JS_SetPropertyStr(ctx, proto, "VERTEX_ATTRIB_ARRAY_BUFFER_BINDING", JS_NewInt32(ctx, 0x889F));
    JS_SetPropertyStr(ctx, proto, "VERTEX_ATTRIB_ARRAY_INTEGER", JS_NewInt32(ctx, 0x88FD));
    JS_SetPropertyStr(ctx, proto, "VERTEX_ATTRIB_ARRAY_DIVISOR", JS_NewInt32(ctx, GL_VERTEX_ATTRIB_ARRAY_DIVISOR));
    JS_SetPropertyStr(ctx, proto, "CURRENT_VERTEX_ATTRIB", JS_NewInt32(ctx, 0x8626));
    
    /* Buffer info */
    JS_SetPropertyStr(ctx, proto, "BUFFER_SIZE", JS_NewInt32(ctx, 0x8764));
    JS_SetPropertyStr(ctx, proto, "BUFFER_USAGE", JS_NewInt32(ctx, 0x8765));
    
    /* Hints */
    JS_SetPropertyStr(ctx, proto, "DONT_CARE", JS_NewInt32(ctx, 0x1100));
    JS_SetPropertyStr(ctx, proto, "FASTEST", JS_NewInt32(ctx, 0x1101));
    JS_SetPropertyStr(ctx, proto, "NICEST", JS_NewInt32(ctx, 0x1102));
    JS_SetPropertyStr(ctx, proto, "FRAGMENT_SHADER_DERIVATIVE_HINT", JS_NewInt32(ctx, 0x8B8B));
    
    /* WebGL 2 misc */
    JS_SetPropertyStr(ctx, proto, "MAX_ELEMENT_INDEX", JS_NewInt32(ctx, 0x8D6B));
    JS_SetPropertyStr(ctx, proto, "MAX_ELEMENTS_INDICES", JS_NewInt32(ctx, 0x80E9));
    JS_SetPropertyStr(ctx, proto, "MAX_ELEMENTS_VERTICES", JS_NewInt32(ctx, 0x80E8));
    JS_SetPropertyStr(ctx, proto, "MAX_VERTEX_OUTPUT_COMPONENTS", JS_NewInt32(ctx, 0x9122));
    JS_SetPropertyStr(ctx, proto, "MAX_FRAGMENT_INPUT_COMPONENTS", JS_NewInt32(ctx, 0x9125));
    JS_SetPropertyStr(ctx, proto, "MAX_VARYING_COMPONENTS", JS_NewInt32(ctx, GL_MAX_VARYING_COMPONENTS));
    JS_SetPropertyStr(ctx, proto, "MAX_PROGRAM_TEXEL_OFFSET", JS_NewInt32(ctx, 0x8905));
    JS_SetPropertyStr(ctx, proto, "MIN_PROGRAM_TEXEL_OFFSET", JS_NewInt32(ctx, 0x8904));
    JS_SetPropertyStr(ctx, proto, "RASTERIZER_DISCARD", JS_NewInt32(ctx, 0x8C89));
    
    /* WebGL extension info */
    JS_SetPropertyStr(ctx, proto, "UNMASKED_VENDOR_WEBGL", JS_NewInt32(ctx, 0x9245));
    JS_SetPropertyStr(ctx, proto, "UNMASKED_RENDERER_WEBGL", JS_NewInt32(ctx, 0x9246));
    JS_SetPropertyStr(ctx, proto, "MAX_TEXTURE_LOD_BIAS", JS_NewInt32(ctx, 0x84FD));
    
    /* None/Back for draw buffers */
    JS_SetPropertyStr(ctx, proto, "NONE", JS_NewInt32(ctx, GL_NONE));
}

/* ============================================================================
 * Phase 2: Basic Rendering Pipeline
 * ============================================================================ */

/* --------------------------------------------------------------------------
 * Context Info Methods
 * -------------------------------------------------------------------------- */

static JSValue js_webgl_getError(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    GLenum err = wctx->last_error;
    wctx->last_error = GL_NO_ERROR;
    
    if (err == GL_NO_ERROR) {
        err = glGetError();
    }
    
    return JS_NewInt32(ctx, err);
}

static JSValue js_webgl_isContextLost(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    return JS_NewBool(ctx, wctx->context_lost);
}

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

static JSValue js_webgl_getSupportedExtensions(JSContext *ctx, JSValueConst this_val,
                                                int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx || wctx->context_lost) return JS_NULL;
    return JS_NewArray(ctx);
}

static JSValue js_webgl_getExtension(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv) {
    return JS_NULL;
}

/* --------------------------------------------------------------------------
 * State Methods: enable, disable, isEnabled
 * -------------------------------------------------------------------------- */

static JSValue js_webgl_enable(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    int cap;
    if (JS_ToInt32(ctx, &cap, argv[0])) return JS_EXCEPTION;
    
    glEnable(cap);
    
    /* Update state cache */
    switch (cap) {
        case GL_BLEND: wctx->blend_enabled = GL_TRUE; break;
        case GL_DEPTH_TEST: wctx->depth_test_enabled = GL_TRUE; break;
        case GL_STENCIL_TEST: wctx->stencil_test_enabled = GL_TRUE; break;
        case GL_CULL_FACE: wctx->cull_face_enabled = GL_TRUE; break;
        case GL_SCISSOR_TEST: wctx->scissor_test_enabled = GL_TRUE; break;
    }
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_disable(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    int cap;
    if (JS_ToInt32(ctx, &cap, argv[0])) return JS_EXCEPTION;
    
    glDisable(cap);
    
    switch (cap) {
        case GL_BLEND: wctx->blend_enabled = GL_FALSE; break;
        case GL_DEPTH_TEST: wctx->depth_test_enabled = GL_FALSE; break;
        case GL_STENCIL_TEST: wctx->stencil_test_enabled = GL_FALSE; break;
        case GL_CULL_FACE: wctx->cull_face_enabled = GL_FALSE; break;
        case GL_SCISSOR_TEST: wctx->scissor_test_enabled = GL_FALSE; break;
    }
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_isEnabled(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    int cap;
    if (JS_ToInt32(ctx, &cap, argv[0])) return JS_EXCEPTION;
    
    return JS_NewBool(ctx, glIsEnabled(cap));
}

/* --------------------------------------------------------------------------
 * Viewport and Scissor
 * -------------------------------------------------------------------------- */

static JSValue js_webgl_viewport(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    int x, y, w, h;
    if (JS_ToInt32(ctx, &x, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &y, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &w, argv[2])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &h, argv[3])) return JS_EXCEPTION;
    
    glViewport(x, y, w, h);
    wctx->viewport_x = x;
    wctx->viewport_y = y;
    wctx->viewport_width = w;
    wctx->viewport_height = h;
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_scissor(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    int x, y, w, h;
    if (JS_ToInt32(ctx, &x, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &y, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &w, argv[2])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &h, argv[3])) return JS_EXCEPTION;
    
    glScissor(x, y, w, h);
    wctx->scissor_x = x;
    wctx->scissor_y = y;
    wctx->scissor_width = w;
    wctx->scissor_height = h;
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_depthRange(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    double zNear, zFar;
    if (JS_ToFloat64(ctx, &zNear, argv[0])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &zFar, argv[1])) return JS_EXCEPTION;
    
    glDepthRange(zNear, zFar);
    return JS_UNDEFINED;
}

/* --------------------------------------------------------------------------
 * Clear Methods
 * -------------------------------------------------------------------------- */

static JSValue js_webgl_clearColor(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    double r, g, b, a;
    if (JS_ToFloat64(ctx, &r, argv[0])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &g, argv[1])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &b, argv[2])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &a, argv[3])) return JS_EXCEPTION;
    
    glClearColor((GLfloat)r, (GLfloat)g, (GLfloat)b, (GLfloat)a);
    wctx->clear_color[0] = (GLfloat)r;
    wctx->clear_color[1] = (GLfloat)g;
    wctx->clear_color[2] = (GLfloat)b;
    wctx->clear_color[3] = (GLfloat)a;
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_clearDepth(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    double depth;
    if (JS_ToFloat64(ctx, &depth, argv[0])) return JS_EXCEPTION;
    
    glClearDepth(depth);
    wctx->clear_depth = (GLfloat)depth;
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_clearStencil(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    int s;
    if (JS_ToInt32(ctx, &s, argv[0])) return JS_EXCEPTION;
    
    glClearStencil(s);
    wctx->clear_stencil = s;
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_clear(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv) {
    int mask;
    if (JS_ToInt32(ctx, &mask, argv[0])) return JS_EXCEPTION;
    
    glClear(mask);
    return JS_UNDEFINED;
}

/* --------------------------------------------------------------------------
 * Blend State
 * -------------------------------------------------------------------------- */

static JSValue js_webgl_blendFunc(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    int sfactor, dfactor;
    if (JS_ToInt32(ctx, &sfactor, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &dfactor, argv[1])) return JS_EXCEPTION;
    
    glBlendFunc(sfactor, dfactor);
    wctx->blend_src_rgb = wctx->blend_src_alpha = sfactor;
    wctx->blend_dst_rgb = wctx->blend_dst_alpha = dfactor;
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_blendFuncSeparate(JSContext *ctx, JSValueConst this_val,
                                           int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    int srcRGB, dstRGB, srcAlpha, dstAlpha;
    if (JS_ToInt32(ctx, &srcRGB, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &dstRGB, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &srcAlpha, argv[2])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &dstAlpha, argv[3])) return JS_EXCEPTION;
    
    glBlendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha);
    wctx->blend_src_rgb = srcRGB;
    wctx->blend_dst_rgb = dstRGB;
    wctx->blend_src_alpha = srcAlpha;
    wctx->blend_dst_alpha = dstAlpha;
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_blendEquation(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    int mode;
    if (JS_ToInt32(ctx, &mode, argv[0])) return JS_EXCEPTION;
    
    glBlendEquation(mode);
    wctx->blend_equation_rgb = wctx->blend_equation_alpha = mode;
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_blendEquationSeparate(JSContext *ctx, JSValueConst this_val,
                                               int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    int modeRGB, modeAlpha;
    if (JS_ToInt32(ctx, &modeRGB, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &modeAlpha, argv[1])) return JS_EXCEPTION;
    
    glBlendEquationSeparate(modeRGB, modeAlpha);
    wctx->blend_equation_rgb = modeRGB;
    wctx->blend_equation_alpha = modeAlpha;
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_blendColor(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    double r, g, b, a;
    if (JS_ToFloat64(ctx, &r, argv[0])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &g, argv[1])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &b, argv[2])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &a, argv[3])) return JS_EXCEPTION;
    
    glBlendColor((GLfloat)r, (GLfloat)g, (GLfloat)b, (GLfloat)a);
    return JS_UNDEFINED;
}

/* --------------------------------------------------------------------------
 * Depth State
 * -------------------------------------------------------------------------- */

static JSValue js_webgl_depthFunc(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    int func;
    if (JS_ToInt32(ctx, &func, argv[0])) return JS_EXCEPTION;
    
    glDepthFunc(func);
    wctx->depth_func = func;
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_depthMask(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    int flag = JS_ToBool(ctx, argv[0]);
    glDepthMask(flag ? GL_TRUE : GL_FALSE);
    wctx->depth_mask = flag ? GL_TRUE : GL_FALSE;
    
    return JS_UNDEFINED;
}

/* --------------------------------------------------------------------------
 * Stencil State
 * -------------------------------------------------------------------------- */

/* Helper to validate stencil comparison function */
static int is_valid_stencil_func(GLenum func) {
    switch (func) {
        case GL_NEVER:
        case GL_LESS:
        case GL_LEQUAL:
        case GL_GREATER:
        case GL_GEQUAL:
        case GL_EQUAL:
        case GL_NOTEQUAL:
        case GL_ALWAYS:
            return 1;
        default:
            return 0;
    }
}

/* Helper to validate stencil operation */
static int is_valid_stencil_op(GLenum op) {
    switch (op) {
        case GL_KEEP:
        case GL_ZERO:
        case GL_REPLACE:
        case GL_INCR:
        case GL_INCR_WRAP:
        case GL_DECR:
        case GL_DECR_WRAP:
        case GL_INVERT:
            return 1;
        default:
            return 0;
    }
}

/* Helper to validate face enum */
static int is_valid_stencil_face(GLenum face) {
    switch (face) {
        case GL_FRONT:
        case GL_BACK:
        case GL_FRONT_AND_BACK:
            return 1;
        default:
            return 0;
    }
}

static JSValue js_webgl_stencilFunc(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    if (wctx->context_lost) return JS_UNDEFINED;
    
    int func, ref;
    uint32_t mask;
    if (JS_ToInt32(ctx, &func, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &ref, argv[1])) return JS_EXCEPTION;
    if (JS_ToUint32(ctx, &mask, argv[2])) return JS_EXCEPTION;
    
    if (!is_valid_stencil_func(func)) {
        wctx->last_error = GL_INVALID_ENUM;
        return JS_UNDEFINED;
    }
    
    glStencilFunc(func, ref, mask);
    return JS_UNDEFINED;
}

static JSValue js_webgl_stencilFuncSeparate(JSContext *ctx, JSValueConst this_val,
                                             int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    if (wctx->context_lost) return JS_UNDEFINED;
    
    int face, func, ref;
    uint32_t mask;
    if (JS_ToInt32(ctx, &face, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &func, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &ref, argv[2])) return JS_EXCEPTION;
    if (JS_ToUint32(ctx, &mask, argv[3])) return JS_EXCEPTION;
    
    if (!is_valid_stencil_face(face)) {
        wctx->last_error = GL_INVALID_ENUM;
        return JS_UNDEFINED;
    }
    if (!is_valid_stencil_func(func)) {
        wctx->last_error = GL_INVALID_ENUM;
        return JS_UNDEFINED;
    }
    
    glStencilFuncSeparate(face, func, ref, mask);
    return JS_UNDEFINED;
}

static JSValue js_webgl_stencilOp(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    if (wctx->context_lost) return JS_UNDEFINED;
    
    int fail, zfail, zpass;
    if (JS_ToInt32(ctx, &fail, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &zfail, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &zpass, argv[2])) return JS_EXCEPTION;
    
    if (!is_valid_stencil_op(fail) || !is_valid_stencil_op(zfail) || !is_valid_stencil_op(zpass)) {
        wctx->last_error = GL_INVALID_ENUM;
        return JS_UNDEFINED;
    }
    
    glStencilOp(fail, zfail, zpass);
    return JS_UNDEFINED;
}

static JSValue js_webgl_stencilOpSeparate(JSContext *ctx, JSValueConst this_val,
                                           int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    if (wctx->context_lost) return JS_UNDEFINED;
    
    int face, fail, zfail, zpass;
    if (JS_ToInt32(ctx, &face, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &fail, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &zfail, argv[2])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &zpass, argv[3])) return JS_EXCEPTION;
    
    if (!is_valid_stencil_face(face)) {
        wctx->last_error = GL_INVALID_ENUM;
        return JS_UNDEFINED;
    }
    if (!is_valid_stencil_op(fail) || !is_valid_stencil_op(zfail) || !is_valid_stencil_op(zpass)) {
        wctx->last_error = GL_INVALID_ENUM;
        return JS_UNDEFINED;
    }
    
    glStencilOpSeparate(face, fail, zfail, zpass);
    return JS_UNDEFINED;
}

static JSValue js_webgl_stencilMask(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    if (wctx->context_lost) return JS_UNDEFINED;
    
    uint32_t mask;
    if (JS_ToUint32(ctx, &mask, argv[0])) return JS_EXCEPTION;
    
    glStencilMask(mask);
    return JS_UNDEFINED;
}

static JSValue js_webgl_stencilMaskSeparate(JSContext *ctx, JSValueConst this_val,
                                             int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    if (wctx->context_lost) return JS_UNDEFINED;
    
    int face;
    uint32_t mask;
    if (JS_ToInt32(ctx, &face, argv[0])) return JS_EXCEPTION;
    if (JS_ToUint32(ctx, &mask, argv[1])) return JS_EXCEPTION;
    
    if (!is_valid_stencil_face(face)) {
        wctx->last_error = GL_INVALID_ENUM;
        return JS_UNDEFINED;
    }
    
    glStencilMaskSeparate(face, mask);
    return JS_UNDEFINED;
}

/* --------------------------------------------------------------------------
 * Cull Face State
 * -------------------------------------------------------------------------- */

static JSValue js_webgl_cullFace(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    int mode;
    if (JS_ToInt32(ctx, &mode, argv[0])) return JS_EXCEPTION;
    
    glCullFace(mode);
    wctx->cull_face_mode = mode;
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_frontFace(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    int mode;
    if (JS_ToInt32(ctx, &mode, argv[0])) return JS_EXCEPTION;
    
    glFrontFace(mode);
    wctx->front_face = mode;
    
    return JS_UNDEFINED;
}

/* --------------------------------------------------------------------------
 * Color Mask
 * -------------------------------------------------------------------------- */

static JSValue js_webgl_colorMask(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
    int r = JS_ToBool(ctx, argv[0]);
    int g = JS_ToBool(ctx, argv[1]);
    int b = JS_ToBool(ctx, argv[2]);
    int a = JS_ToBool(ctx, argv[3]);
    
    glColorMask(r, g, b, a);
    return JS_UNDEFINED;
}

/* --------------------------------------------------------------------------
 * Pixel Store
 * -------------------------------------------------------------------------- */

static JSValue js_webgl_pixelStorei(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    int pname, param;
    if (JS_ToInt32(ctx, &pname, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &param, argv[1])) return JS_EXCEPTION;
    
    /* Handle WebGL-specific parameters */
    if (pname == 0x9240) { /* UNPACK_FLIP_Y_WEBGL */
        wctx->unpack_flip_y = param ? GL_TRUE : GL_FALSE;
        return JS_UNDEFINED;
    }
    if (pname == 0x9241) { /* UNPACK_PREMULTIPLY_ALPHA_WEBGL */
        wctx->unpack_premultiply_alpha = param ? GL_TRUE : GL_FALSE;
        return JS_UNDEFINED;
    }
    
    glPixelStorei(pname, param);
    
    if (pname == GL_UNPACK_ALIGNMENT) wctx->unpack_alignment = param;
    if (pname == GL_PACK_ALIGNMENT) wctx->pack_alignment = param;
    
    return JS_UNDEFINED;
}

/* --------------------------------------------------------------------------
 * Buffer Methods
 * -------------------------------------------------------------------------- */

static JSValue js_webgl_createBuffer(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    GLuint gl_buf;
    glGenBuffers(1, &gl_buf);
    
    uint32_t js_handle = hashmap_alloc(wctx->buffers, gl_buf);
    return create_webgl_object(ctx, js_webgl_buffer_class_id, js_handle);
}

static JSValue js_webgl_bindBuffer(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    int target;
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    
    GLuint gl_buf = 0;
    if (!JS_IsNull(argv[1]) && !JS_IsUndefined(argv[1])) {
        uint32_t js_handle = get_webgl_object_handle(ctx, argv[1], js_webgl_buffer_class_id);
        gl_buf = hashmap_get(wctx->buffers, js_handle);
    }
    
    glBindBuffer(target, gl_buf);
    
    if (target == GL_ARRAY_BUFFER) wctx->bound_array_buffer = gl_buf;
    else if (target == GL_ELEMENT_ARRAY_BUFFER) wctx->bound_element_buffer = gl_buf;
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_bufferData(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    int target, usage;
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    
    /* Check if second arg is size (number) or data (ArrayBuffer/TypedArray) */
    if (JS_IsNumber(argv[1])) {
        /* bufferData(target, size, usage) */
        int64_t size;
        if (JS_ToInt64(ctx, &size, argv[1])) return JS_EXCEPTION;
        if (JS_ToInt32(ctx, &usage, argv[2])) return JS_EXCEPTION;
        
        glBufferData(target, size, NULL, usage);
    } else {
        /* bufferData(target, data, usage) */
        if (JS_ToInt32(ctx, &usage, argv[2])) return JS_EXCEPTION;
        
        size_t size;
        uint8_t *data = JS_GetArrayBuffer(ctx, &size, argv[1]);
        
        if (!data) {
            /* Try TypedArray */
            size_t offset, len;
            JSValue ab = JS_GetTypedArrayBuffer(ctx, argv[1], &offset, &len, NULL);
            if (!JS_IsException(ab)) {
                data = JS_GetArrayBuffer(ctx, &size, ab);
                if (data) {
                    data += offset;
                    size = len;
                }
                JS_FreeValue(ctx, ab);
            }
        }
        
        if (data) {
            glBufferData(target, size, data, usage);
        } else {
            /* Null data */
            glBufferData(target, 0, NULL, usage);
        }
    }
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_bufferSubData(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
    int target;
    int64_t offset;
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt64(ctx, &offset, argv[1])) return JS_EXCEPTION;
    
    size_t size;
    uint8_t *data = JS_GetArrayBuffer(ctx, &size, argv[2]);
    
    if (!data) {
        size_t arr_offset, len;
        JSValue ab = JS_GetTypedArrayBuffer(ctx, argv[2], &arr_offset, &len, NULL);
        if (!JS_IsException(ab)) {
            data = JS_GetArrayBuffer(ctx, &size, ab);
            if (data) {
                data += arr_offset;
                size = len;
            }
            JS_FreeValue(ctx, ab);
        }
    }
    
    if (data) {
        glBufferSubData(target, offset, size, data);
    }
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_deleteBuffer(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) return JS_UNDEFINED;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_buffer_class_id);
    GLuint gl_buf = hashmap_remove(wctx->buffers, js_handle);
    
    if (gl_buf) {
        glDeleteBuffers(1, &gl_buf);
    }
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_isBuffer(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) return JS_FALSE;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_buffer_class_id);
    GLuint gl_buf = hashmap_get(wctx->buffers, js_handle);
    
    return JS_NewBool(ctx, gl_buf && glIsBuffer(gl_buf));
}

/* --------------------------------------------------------------------------
 * Shader Methods
 * -------------------------------------------------------------------------- */

static JSValue js_webgl_createShader(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    int type;
    if (JS_ToInt32(ctx, &type, argv[0])) return JS_EXCEPTION;
    
    GLuint gl_shader = glCreateShader(type);
    if (!gl_shader) return JS_NULL;
    
    uint32_t js_handle = hashmap_alloc(wctx->shaders, gl_shader);
    return create_webgl_object(ctx, js_webgl_shader_class_id, js_handle);
}

static JSValue js_webgl_shaderSource(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_shader_class_id);
    GLuint gl_shader = hashmap_get(wctx->shaders, js_handle);
    if (!gl_shader) return JS_UNDEFINED;
    
    const char *source = JS_ToCString(ctx, argv[1]);
    if (!source) return JS_EXCEPTION;
    
    glShaderSource(gl_shader, 1, &source, NULL);
    JS_FreeCString(ctx, source);
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_compileShader(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_shader_class_id);
    GLuint gl_shader = hashmap_get(wctx->shaders, js_handle);
    if (!gl_shader) return JS_UNDEFINED;
    
    glCompileShader(gl_shader);
    return JS_UNDEFINED;
}

static JSValue js_webgl_getShaderParameter(JSContext *ctx, JSValueConst this_val,
                                            int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_shader_class_id);
    GLuint gl_shader = hashmap_get(wctx->shaders, js_handle);
    if (!gl_shader) return JS_NULL;
    
    int pname;
    if (JS_ToInt32(ctx, &pname, argv[1])) return JS_EXCEPTION;
    
    GLint value;
    glGetShaderiv(gl_shader, pname, &value);
    
    if (pname == GL_COMPILE_STATUS || pname == 0x8B80 /* DELETE_STATUS */) {
        return JS_NewBool(ctx, value);
    }
    
    return JS_NewInt32(ctx, value);
}

static JSValue js_webgl_getShaderInfoLog(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_shader_class_id);
    GLuint gl_shader = hashmap_get(wctx->shaders, js_handle);
    if (!gl_shader) return JS_NewString(ctx, "");
    
    GLint len;
    glGetShaderiv(gl_shader, GL_INFO_LOG_LENGTH, &len);
    
    if (len <= 0) return JS_NewString(ctx, "");
    
    char *log = (char *)malloc(len);
    if (!log) return JS_ThrowOutOfMemory(ctx);
    
    glGetShaderInfoLog(gl_shader, len, NULL, log);
    JSValue result = JS_NewString(ctx, log);
    free(log);
    
    return result;
}

static JSValue js_webgl_deleteShader(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) return JS_UNDEFINED;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_shader_class_id);
    GLuint gl_shader = hashmap_remove(wctx->shaders, js_handle);
    
    if (gl_shader) {
        glDeleteShader(gl_shader);
    }
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_isShader(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) return JS_FALSE;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_shader_class_id);
    GLuint gl_shader = hashmap_get(wctx->shaders, js_handle);
    
    return JS_NewBool(ctx, gl_shader && glIsShader(gl_shader));
}

/* --------------------------------------------------------------------------
 * Program Methods
 * -------------------------------------------------------------------------- */

static JSValue js_webgl_createProgram(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    GLuint gl_prog = glCreateProgram();
    if (!gl_prog) return JS_NULL;
    
    uint32_t js_handle = hashmap_alloc(wctx->programs, gl_prog);
    return create_webgl_object(ctx, js_webgl_program_class_id, js_handle);
}

static JSValue js_webgl_attachShader(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    uint32_t prog_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_program_class_id);
    uint32_t shader_handle = get_webgl_object_handle(ctx, argv[1], js_webgl_shader_class_id);
    
    GLuint gl_prog = hashmap_get(wctx->programs, prog_handle);
    GLuint gl_shader = hashmap_get(wctx->shaders, shader_handle);
    
    if (gl_prog && gl_shader) {
        glAttachShader(gl_prog, gl_shader);
    }
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_detachShader(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    uint32_t prog_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_program_class_id);
    uint32_t shader_handle = get_webgl_object_handle(ctx, argv[1], js_webgl_shader_class_id);
    
    GLuint gl_prog = hashmap_get(wctx->programs, prog_handle);
    GLuint gl_shader = hashmap_get(wctx->shaders, shader_handle);
    
    if (gl_prog && gl_shader) {
        glDetachShader(gl_prog, gl_shader);
    }
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_linkProgram(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_program_class_id);
    GLuint gl_prog = hashmap_get(wctx->programs, js_handle);
    
    if (gl_prog) {
        glLinkProgram(gl_prog);
    }
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_useProgram(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    GLuint gl_prog = 0;
    if (!JS_IsNull(argv[0]) && !JS_IsUndefined(argv[0])) {
        uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_program_class_id);
        gl_prog = hashmap_get(wctx->programs, js_handle);
    }
    
    glUseProgram(gl_prog);
    wctx->current_program = gl_prog;
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_validateProgram(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_program_class_id);
    GLuint gl_prog = hashmap_get(wctx->programs, js_handle);
    
    if (gl_prog) {
        glValidateProgram(gl_prog);
    }
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_getProgramParameter(JSContext *ctx, JSValueConst this_val,
                                             int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_program_class_id);
    GLuint gl_prog = hashmap_get(wctx->programs, js_handle);
    if (!gl_prog) return JS_NULL;
    
    int pname;
    if (JS_ToInt32(ctx, &pname, argv[1])) return JS_EXCEPTION;
    
    GLint value;
    glGetProgramiv(gl_prog, pname, &value);
    
    if (pname == GL_LINK_STATUS || pname == GL_VALIDATE_STATUS || pname == 0x8B80) {
        return JS_NewBool(ctx, value);
    }
    
    return JS_NewInt32(ctx, value);
}

static JSValue js_webgl_getProgramInfoLog(JSContext *ctx, JSValueConst this_val,
                                           int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_program_class_id);
    GLuint gl_prog = hashmap_get(wctx->programs, js_handle);
    if (!gl_prog) return JS_NewString(ctx, "");
    
    GLint len;
    glGetProgramiv(gl_prog, GL_INFO_LOG_LENGTH, &len);
    
    if (len <= 0) return JS_NewString(ctx, "");
    
    char *log = (char *)malloc(len);
    if (!log) return JS_ThrowOutOfMemory(ctx);
    
    glGetProgramInfoLog(gl_prog, len, NULL, log);
    JSValue result = JS_NewString(ctx, log);
    free(log);
    
    return result;
}

static JSValue js_webgl_deleteProgram(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) return JS_UNDEFINED;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_program_class_id);
    GLuint gl_prog = hashmap_remove(wctx->programs, js_handle);
    
    if (gl_prog) {
        glDeleteProgram(gl_prog);
    }
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_isProgram(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) return JS_FALSE;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_program_class_id);
    GLuint gl_prog = hashmap_get(wctx->programs, js_handle);
    
    return JS_NewBool(ctx, gl_prog && glIsProgram(gl_prog));
}

/* --------------------------------------------------------------------------
 * Active Info Helpers (WebGLActiveInfo)
 * -------------------------------------------------------------------------- */

/* Helper to create a WebGLActiveInfo object with name, size, type properties */
static JSValue create_webgl_active_info(JSContext *ctx, const char *name, GLint size, GLenum type) {
    JSValue obj = JS_NewObjectClass(ctx, js_webgl_active_info_class_id);
    if (JS_IsException(obj)) return obj;
    
    JS_SetPropertyStr(ctx, obj, "name", JS_NewString(ctx, name));
    JS_SetPropertyStr(ctx, obj, "size", JS_NewInt32(ctx, size));
    JS_SetPropertyStr(ctx, obj, "type", JS_NewInt32(ctx, type));
    
    return obj;
}

static JSValue js_webgl_getActiveUniform(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    if (wctx->context_lost) return JS_NULL;
    
    /* Validate program argument */
    if (argc < 2) return JS_NULL;
    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) {
        wctx->last_error = GL_INVALID_VALUE;
        return JS_NULL;
    }
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_program_class_id);
    GLuint gl_prog = hashmap_get(wctx->programs, js_handle);
    if (!gl_prog || !glIsProgram(gl_prog)) {
        wctx->last_error = GL_INVALID_VALUE;
        return JS_NULL;
    }
    
    /* Get index */
    uint32_t index;
    if (JS_ToUint32(ctx, &index, argv[1])) return JS_EXCEPTION;
    
    /* Bounds check against active uniform count */
    GLint num_uniforms;
    glGetProgramiv(gl_prog, GL_ACTIVE_UNIFORMS, &num_uniforms);
    if ((GLint)index >= num_uniforms) {
        wctx->last_error = GL_INVALID_VALUE;
        return JS_NULL;
    }
    
    /* Get max name length and allocate buffer */
    GLint max_len;
    glGetProgramiv(gl_prog, GL_ACTIVE_UNIFORM_MAX_LENGTH, &max_len);
    if (max_len <= 0) max_len = 256;  /* Fallback */
    
    char *name = (char *)malloc(max_len);
    if (!name) return JS_ThrowOutOfMemory(ctx);
    
    GLsizei name_len;
    GLint size;
    GLenum type;
    glGetActiveUniform(gl_prog, index, max_len, &name_len, &size, &type, name);
    
    JSValue result = create_webgl_active_info(ctx, name, size, type);
    free(name);
    
    return result;
}

static JSValue js_webgl_getActiveAttrib(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    if (wctx->context_lost) return JS_NULL;
    
    /* Validate program argument */
    if (argc < 2) return JS_NULL;
    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) {
        wctx->last_error = GL_INVALID_VALUE;
        return JS_NULL;
    }
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_program_class_id);
    GLuint gl_prog = hashmap_get(wctx->programs, js_handle);
    if (!gl_prog || !glIsProgram(gl_prog)) {
        wctx->last_error = GL_INVALID_VALUE;
        return JS_NULL;
    }
    
    /* Get index */
    uint32_t index;
    if (JS_ToUint32(ctx, &index, argv[1])) return JS_EXCEPTION;
    
    /* Bounds check against active attribute count */
    GLint num_attribs;
    glGetProgramiv(gl_prog, GL_ACTIVE_ATTRIBUTES, &num_attribs);
    if ((GLint)index >= num_attribs) {
        wctx->last_error = GL_INVALID_VALUE;
        return JS_NULL;
    }
    
    /* Get max name length and allocate buffer */
    GLint max_len;
    glGetProgramiv(gl_prog, GL_ACTIVE_ATTRIBUTE_MAX_LENGTH, &max_len);
    if (max_len <= 0) max_len = 256;  /* Fallback */
    
    char *name = (char *)malloc(max_len);
    if (!name) return JS_ThrowOutOfMemory(ctx);
    
    GLsizei name_len;
    GLint size;
    GLenum type;
    glGetActiveAttrib(gl_prog, index, max_len, &name_len, &size, &type, name);
    
    JSValue result = create_webgl_active_info(ctx, name, size, type);
    free(name);
    
    return result;
}

/* --------------------------------------------------------------------------
 * Attribute Methods
 * -------------------------------------------------------------------------- */

static JSValue js_webgl_getAttribLocation(JSContext *ctx, JSValueConst this_val,
                                           int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_program_class_id);
    GLuint gl_prog = hashmap_get(wctx->programs, js_handle);
    if (!gl_prog) return JS_NewInt32(ctx, -1);
    
    const char *name = JS_ToCString(ctx, argv[1]);
    if (!name) return JS_EXCEPTION;
    
    GLint loc = glGetAttribLocation(gl_prog, name);
    JS_FreeCString(ctx, name);
    
    return JS_NewInt32(ctx, loc);
}

static JSValue js_webgl_bindAttribLocation(JSContext *ctx, JSValueConst this_val,
                                            int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_program_class_id);
    GLuint gl_prog = hashmap_get(wctx->programs, js_handle);
    if (!gl_prog) return JS_UNDEFINED;
    
    int index;
    if (JS_ToInt32(ctx, &index, argv[1])) return JS_EXCEPTION;
    
    const char *name = JS_ToCString(ctx, argv[2]);
    if (!name) return JS_EXCEPTION;
    
    glBindAttribLocation(gl_prog, index, name);
    JS_FreeCString(ctx, name);
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_enableVertexAttribArray(JSContext *ctx, JSValueConst this_val,
                                                 int argc, JSValueConst *argv) {
    int index;
    if (JS_ToInt32(ctx, &index, argv[0])) return JS_EXCEPTION;
    
    glEnableVertexAttribArray(index);
    return JS_UNDEFINED;
}

static JSValue js_webgl_disableVertexAttribArray(JSContext *ctx, JSValueConst this_val,
                                                  int argc, JSValueConst *argv) {
    int index;
    if (JS_ToInt32(ctx, &index, argv[0])) return JS_EXCEPTION;
    
    glDisableVertexAttribArray(index);
    return JS_UNDEFINED;
}

static JSValue js_webgl_vertexAttribPointer(JSContext *ctx, JSValueConst this_val,
                                             int argc, JSValueConst *argv) {
    int index, size, type, stride;
    int64_t offset;
    int normalized;
    
    if (JS_ToInt32(ctx, &index, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &size, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &type, argv[2])) return JS_EXCEPTION;
    normalized = JS_ToBool(ctx, argv[3]);
    if (JS_ToInt32(ctx, &stride, argv[4])) return JS_EXCEPTION;
    if (JS_ToInt64(ctx, &offset, argv[5])) return JS_EXCEPTION;
    
    glVertexAttribPointer(index, size, type, normalized, stride, (void *)(intptr_t)offset);
    return JS_UNDEFINED;
}

static JSValue js_webgl_vertexAttribIPointer(JSContext *ctx, JSValueConst this_val,
                                              int argc, JSValueConst *argv) {
    int index, size, type, stride;
    int64_t offset;
    
    if (JS_ToInt32(ctx, &index, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &size, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &type, argv[2])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &stride, argv[3])) return JS_EXCEPTION;
    if (JS_ToInt64(ctx, &offset, argv[4])) return JS_EXCEPTION;
    
    glVertexAttribIPointer(index, size, type, stride, (void *)(intptr_t)offset);
    return JS_UNDEFINED;
}

static JSValue js_webgl_vertexAttribDivisor(JSContext *ctx, JSValueConst this_val,
                                             int argc, JSValueConst *argv) {
    int index, divisor;
    if (JS_ToInt32(ctx, &index, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &divisor, argv[1])) return JS_EXCEPTION;
    
    glVertexAttribDivisor(index, divisor);
    return JS_UNDEFINED;
}

/* --------------------------------------------------------------------------
 * Uniform Methods
 * -------------------------------------------------------------------------- */

static JSValue js_webgl_getUniformLocation(JSContext *ctx, JSValueConst this_val,
                                            int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_program_class_id);
    GLuint gl_prog = hashmap_get(wctx->programs, js_handle);
    if (!gl_prog) return JS_NULL;
    
    const char *name = JS_ToCString(ctx, argv[1]);
    if (!name) return JS_EXCEPTION;
    
    GLint loc = glGetUniformLocation(gl_prog, name);
    JS_FreeCString(ctx, name);
    
    if (loc < 0) return JS_NULL;
    
    uint32_t loc_handle = uniformmap_alloc(wctx->uniform_locations, loc);
    return create_webgl_object(ctx, js_webgl_uniform_location_class_id, loc_handle);
}

/* Helper to get GL uniform location from JS object */
static GLint get_uniform_location(JSContext *ctx, WebGLContext *wctx, JSValueConst val) {
    if (JS_IsNull(val) || JS_IsUndefined(val)) return -1;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, val, js_webgl_uniform_location_class_id);
    int found;
    return uniformmap_get(wctx->uniform_locations, js_handle, &found);
}

static JSValue js_webgl_uniform1i(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    GLint loc = get_uniform_location(ctx, wctx, argv[0]);
    if (loc < 0) return JS_UNDEFINED;
    
    int v;
    if (JS_ToInt32(ctx, &v, argv[1])) return JS_EXCEPTION;
    
    glUniform1i(loc, v);
    return JS_UNDEFINED;
}

static JSValue js_webgl_uniform2i(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    GLint loc = get_uniform_location(ctx, wctx, argv[0]);
    if (loc < 0) return JS_UNDEFINED;
    
    int x, y;
    if (JS_ToInt32(ctx, &x, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &y, argv[2])) return JS_EXCEPTION;
    
    glUniform2i(loc, x, y);
    return JS_UNDEFINED;
}

static JSValue js_webgl_uniform3i(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    GLint loc = get_uniform_location(ctx, wctx, argv[0]);
    if (loc < 0) return JS_UNDEFINED;
    
    int x, y, z;
    if (JS_ToInt32(ctx, &x, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &y, argv[2])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &z, argv[3])) return JS_EXCEPTION;
    
    glUniform3i(loc, x, y, z);
    return JS_UNDEFINED;
}

static JSValue js_webgl_uniform4i(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    GLint loc = get_uniform_location(ctx, wctx, argv[0]);
    if (loc < 0) return JS_UNDEFINED;
    
    int x, y, z, w;
    if (JS_ToInt32(ctx, &x, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &y, argv[2])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &z, argv[3])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &w, argv[4])) return JS_EXCEPTION;
    
    glUniform4i(loc, x, y, z, w);
    return JS_UNDEFINED;
}

static JSValue js_webgl_uniform1f(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    GLint loc = get_uniform_location(ctx, wctx, argv[0]);
    if (loc < 0) return JS_UNDEFINED;
    
    double v;
    if (JS_ToFloat64(ctx, &v, argv[1])) return JS_EXCEPTION;
    
    glUniform1f(loc, (GLfloat)v);
    return JS_UNDEFINED;
}

static JSValue js_webgl_uniform2f(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    GLint loc = get_uniform_location(ctx, wctx, argv[0]);
    if (loc < 0) return JS_UNDEFINED;
    
    double x, y;
    if (JS_ToFloat64(ctx, &x, argv[1])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &y, argv[2])) return JS_EXCEPTION;
    
    glUniform2f(loc, (GLfloat)x, (GLfloat)y);
    return JS_UNDEFINED;
}

static JSValue js_webgl_uniform3f(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    GLint loc = get_uniform_location(ctx, wctx, argv[0]);
    if (loc < 0) return JS_UNDEFINED;
    
    double x, y, z;
    if (JS_ToFloat64(ctx, &x, argv[1])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &y, argv[2])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &z, argv[3])) return JS_EXCEPTION;
    
    glUniform3f(loc, (GLfloat)x, (GLfloat)y, (GLfloat)z);
    return JS_UNDEFINED;
}

static JSValue js_webgl_uniform4f(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    GLint loc = get_uniform_location(ctx, wctx, argv[0]);
    if (loc < 0) return JS_UNDEFINED;
    
    double x, y, z, w;
    if (JS_ToFloat64(ctx, &x, argv[1])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &y, argv[2])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &z, argv[3])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &w, argv[4])) return JS_EXCEPTION;
    
    glUniform4f(loc, (GLfloat)x, (GLfloat)y, (GLfloat)z, (GLfloat)w);
    return JS_UNDEFINED;
}

/* Helper to extract float array from JS array or TypedArray */
static GLfloat *get_float_array(JSContext *ctx, JSValueConst val, size_t *out_count) {
    /* Try TypedArray first */
    size_t offset, len;
    JSValue ab = JS_GetTypedArrayBuffer(ctx, val, &offset, &len, NULL);
    if (!JS_IsException(ab)) {
        size_t size;
        uint8_t *data = JS_GetArrayBuffer(ctx, &size, ab);
        JS_FreeValue(ctx, ab);
        if (data) {
            *out_count = len / sizeof(GLfloat);
            return (GLfloat *)(data + offset);
        }
    }
    
    /* Try regular array */
    JSValue len_val = JS_GetPropertyStr(ctx, val, "length");
    if (JS_IsException(len_val)) return NULL;
    
    int64_t length;
    if (JS_ToInt64(ctx, &length, len_val)) {
        JS_FreeValue(ctx, len_val);
        return NULL;
    }
    JS_FreeValue(ctx, len_val);
    
    GLfloat *arr = (GLfloat *)malloc(length * sizeof(GLfloat));
    if (!arr) return NULL;
    
    for (int64_t i = 0; i < length; i++) {
        JSValue elem = JS_GetPropertyUint32(ctx, val, i);
        double v;
        if (JS_ToFloat64(ctx, &v, elem)) {
            JS_FreeValue(ctx, elem);
            free(arr);
            return NULL;
        }
        arr[i] = (GLfloat)v;
        JS_FreeValue(ctx, elem);
    }
    
    *out_count = length;
    return arr;
}

static JSValue js_webgl_uniform1fv(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    GLint loc = get_uniform_location(ctx, wctx, argv[0]);
    if (loc < 0) return JS_UNDEFINED;
    
    size_t count;
    GLfloat *data = get_float_array(ctx, argv[1], &count);
    if (data) {
        glUniform1fv(loc, count, data);
        /* Check if we need to free (regular array case) */
        size_t offset, len;
        JSValue ab = JS_GetTypedArrayBuffer(ctx, argv[1], &offset, &len, NULL);
        if (JS_IsException(ab)) {
            free(data);
        } else {
            JS_FreeValue(ctx, ab);
        }
    }
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_uniform2fv(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    GLint loc = get_uniform_location(ctx, wctx, argv[0]);
    if (loc < 0) return JS_UNDEFINED;
    
    size_t count;
    GLfloat *data = get_float_array(ctx, argv[1], &count);
    if (data) {
        glUniform2fv(loc, count / 2, data);
        size_t offset, len;
        JSValue ab = JS_GetTypedArrayBuffer(ctx, argv[1], &offset, &len, NULL);
        if (JS_IsException(ab)) free(data);
        else JS_FreeValue(ctx, ab);
    }
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_uniform3fv(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    GLint loc = get_uniform_location(ctx, wctx, argv[0]);
    if (loc < 0) return JS_UNDEFINED;
    
    size_t count;
    GLfloat *data = get_float_array(ctx, argv[1], &count);
    if (data) {
        glUniform3fv(loc, count / 3, data);
        size_t offset, len;
        JSValue ab = JS_GetTypedArrayBuffer(ctx, argv[1], &offset, &len, NULL);
        if (JS_IsException(ab)) free(data);
        else JS_FreeValue(ctx, ab);
    }
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_uniform4fv(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    GLint loc = get_uniform_location(ctx, wctx, argv[0]);
    if (loc < 0) return JS_UNDEFINED;
    
    size_t count;
    GLfloat *data = get_float_array(ctx, argv[1], &count);
    if (data) {
        glUniform4fv(loc, count / 4, data);
        size_t offset, len;
        JSValue ab = JS_GetTypedArrayBuffer(ctx, argv[1], &offset, &len, NULL);
        if (JS_IsException(ab)) free(data);
        else JS_FreeValue(ctx, ab);
    }
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_uniformMatrix2fv(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    GLint loc = get_uniform_location(ctx, wctx, argv[0]);
    if (loc < 0) return JS_UNDEFINED;
    
    int transpose = JS_ToBool(ctx, argv[1]);
    
    size_t count;
    GLfloat *data = get_float_array(ctx, argv[2], &count);
    if (data) {
        glUniformMatrix2fv(loc, count / 4, transpose, data);
        size_t offset, len;
        JSValue ab = JS_GetTypedArrayBuffer(ctx, argv[2], &offset, &len, NULL);
        if (JS_IsException(ab)) free(data);
        else JS_FreeValue(ctx, ab);
    }
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_uniformMatrix3fv(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    GLint loc = get_uniform_location(ctx, wctx, argv[0]);
    if (loc < 0) return JS_UNDEFINED;
    
    int transpose = JS_ToBool(ctx, argv[1]);
    
    size_t count;
    GLfloat *data = get_float_array(ctx, argv[2], &count);
    if (data) {
        glUniformMatrix3fv(loc, count / 9, transpose, data);
        size_t offset, len;
        JSValue ab = JS_GetTypedArrayBuffer(ctx, argv[2], &offset, &len, NULL);
        if (JS_IsException(ab)) free(data);
        else JS_FreeValue(ctx, ab);
    }
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_uniformMatrix4fv(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    GLint loc = get_uniform_location(ctx, wctx, argv[0]);
    if (loc < 0) return JS_UNDEFINED;
    
    int transpose = JS_ToBool(ctx, argv[1]);
    
    size_t count;
    GLfloat *data = get_float_array(ctx, argv[2], &count);
    if (data) {
        glUniformMatrix4fv(loc, count / 16, transpose, data);
        size_t offset, len;
        JSValue ab = JS_GetTypedArrayBuffer(ctx, argv[2], &offset, &len, NULL);
        if (JS_IsException(ab)) free(data);
        else JS_FreeValue(ctx, ab);
    }
    
    return JS_UNDEFINED;
}

/* --------------------------------------------------------------------------
 * Drawing Methods
 * -------------------------------------------------------------------------- */

static JSValue js_webgl_drawArrays(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    int mode, first, count;
    if (JS_ToInt32(ctx, &mode, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &first, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &count, argv[2])) return JS_EXCEPTION;
    
    glDrawArrays(mode, first, count);
    return JS_UNDEFINED;
}

static JSValue js_webgl_drawElements(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv) {
    int mode, count, type;
    int64_t offset;
    if (JS_ToInt32(ctx, &mode, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &count, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &type, argv[2])) return JS_EXCEPTION;
    if (JS_ToInt64(ctx, &offset, argv[3])) return JS_EXCEPTION;
    
    glDrawElements(mode, count, type, (void *)(intptr_t)offset);
    return JS_UNDEFINED;
}

static JSValue js_webgl_drawArraysInstanced(JSContext *ctx, JSValueConst this_val,
                                             int argc, JSValueConst *argv) {
    int mode, first, count, instanceCount;
    if (JS_ToInt32(ctx, &mode, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &first, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &count, argv[2])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &instanceCount, argv[3])) return JS_EXCEPTION;
    
    glDrawArraysInstanced(mode, first, count, instanceCount);
    return JS_UNDEFINED;
}

static JSValue js_webgl_drawElementsInstanced(JSContext *ctx, JSValueConst this_val,
                                               int argc, JSValueConst *argv) {
    int mode, count, type, instanceCount;
    int64_t offset;
    if (JS_ToInt32(ctx, &mode, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &count, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &type, argv[2])) return JS_EXCEPTION;
    if (JS_ToInt64(ctx, &offset, argv[3])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &instanceCount, argv[4])) return JS_EXCEPTION;
    
    glDrawElementsInstanced(mode, count, type, (void *)(intptr_t)offset, instanceCount);
    return JS_UNDEFINED;
}

/* --------------------------------------------------------------------------
 * VAO Methods (WebGL 2)
 * -------------------------------------------------------------------------- */

static JSValue js_webgl_createVertexArray(JSContext *ctx, JSValueConst this_val,
                                           int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    GLuint gl_vao;
    glGenVertexArrays(1, &gl_vao);
    
    uint32_t js_handle = hashmap_alloc(wctx->vaos, gl_vao);
    return create_webgl_object(ctx, js_webgl_vao_class_id, js_handle);
}

static JSValue js_webgl_bindVertexArray(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    GLuint gl_vao = 0;
    if (!JS_IsNull(argv[0]) && !JS_IsUndefined(argv[0])) {
        uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_vao_class_id);
        gl_vao = hashmap_get(wctx->vaos, js_handle);
    }
    
    glBindVertexArray(gl_vao);
    wctx->current_vao = gl_vao;
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_deleteVertexArray(JSContext *ctx, JSValueConst this_val,
                                           int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) return JS_UNDEFINED;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_vao_class_id);
    GLuint gl_vao = hashmap_remove(wctx->vaos, js_handle);
    
    if (gl_vao) {
        glDeleteVertexArrays(1, &gl_vao);
    }
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_isVertexArray(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) return JS_FALSE;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_vao_class_id);
    GLuint gl_vao = hashmap_get(wctx->vaos, js_handle);
    
    return JS_NewBool(ctx, gl_vao && glIsVertexArray(gl_vao));
}

/* --------------------------------------------------------------------------
 * Texture Methods
 * -------------------------------------------------------------------------- */

static JSValue js_webgl_createTexture(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    GLuint gl_tex;
    glGenTextures(1, &gl_tex);
    
    uint32_t js_handle = hashmap_alloc(wctx->textures, gl_tex);
    return create_webgl_object(ctx, js_webgl_texture_class_id, js_handle);
}

static JSValue js_webgl_bindTexture(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    int target;
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    
    GLuint gl_tex = 0;
    if (!JS_IsNull(argv[1]) && !JS_IsUndefined(argv[1])) {
        uint32_t js_handle = get_webgl_object_handle(ctx, argv[1], js_webgl_texture_class_id);
        gl_tex = hashmap_get(wctx->textures, js_handle);
    }
    
    glBindTexture(target, gl_tex);
    return JS_UNDEFINED;
}

static JSValue js_webgl_deleteTexture(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) return JS_UNDEFINED;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_texture_class_id);
    GLuint gl_tex = hashmap_remove(wctx->textures, js_handle);
    
    if (gl_tex) {
        glDeleteTextures(1, &gl_tex);
    }
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_activeTexture(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    int texture;
    if (JS_ToInt32(ctx, &texture, argv[0])) return JS_EXCEPTION;
    
    glActiveTexture(texture);
    wctx->active_texture = texture;
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_texParameteri(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
    int target, pname, param;
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &pname, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &param, argv[2])) return JS_EXCEPTION;
    
    glTexParameteri(target, pname, param);
    return JS_UNDEFINED;
}

static JSValue js_webgl_texParameterf(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
    int target, pname;
    double param;
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &pname, argv[1])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &param, argv[2])) return JS_EXCEPTION;
    
    glTexParameterf(target, pname, (GLfloat)param);
    return JS_UNDEFINED;
}

static JSValue js_webgl_generateMipmap(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv) {
    int target;
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    
    glGenerateMipmap(target);
    return JS_UNDEFINED;
}

/* Helper to extract pixel data from various sources */
static uint8_t *get_texture_data(JSContext *ctx, JSValueConst val, size_t *out_size) {
    /* Try ArrayBuffer first */
    size_t size;
    uint8_t *data = JS_GetArrayBuffer(ctx, &size, val);
    if (data) {
        *out_size = size;
        return data;
    }
    
    /* Try TypedArray */
    size_t offset, len;
    JSValue ab = JS_GetTypedArrayBuffer(ctx, val, &offset, &len, NULL);
    if (!JS_IsException(ab)) {
        data = JS_GetArrayBuffer(ctx, &size, ab);
        JS_FreeValue(ctx, ab);
        if (data) {
            *out_size = len;
            return data + offset;
        }
    }
    
    *out_size = 0;
    return NULL;
}

/* texImage2D - multiple overloads:
 * texImage2D(target, level, internalformat, width, height, border, format, type, pixels)
 * texImage2D(target, level, internalformat, format, type, source) - WebGL convenience
 */
static JSValue js_webgl_texImage2D(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    int target, level, internalformat;
    
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &level, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &internalformat, argv[2])) return JS_EXCEPTION;
    
    if (argc >= 9) {
        /* Full form: texImage2D(target, level, internalformat, width, height, border, format, type, pixels) */
        int width, height, border, format, type;
        if (JS_ToInt32(ctx, &width, argv[3])) return JS_EXCEPTION;
        if (JS_ToInt32(ctx, &height, argv[4])) return JS_EXCEPTION;
        if (JS_ToInt32(ctx, &border, argv[5])) return JS_EXCEPTION;
        if (JS_ToInt32(ctx, &format, argv[6])) return JS_EXCEPTION;
        if (JS_ToInt32(ctx, &type, argv[7])) return JS_EXCEPTION;
        
        const void *pixels = NULL;
        if (!JS_IsNull(argv[8]) && !JS_IsUndefined(argv[8])) {
            size_t size;
            pixels = get_texture_data(ctx, argv[8], &size);
        }
        
        glTexImage2D(target, level, internalformat, width, height, border, format, type, pixels);
    } else if (argc >= 6) {
        /* Short form with source object: texImage2D(target, level, internalformat, format, type, source)
         * source can be ImageData, HTMLImageElement, HTMLCanvasElement, etc.
         * For now, we handle it as an object with width, height, and data properties */
        int format, type;
        if (JS_ToInt32(ctx, &format, argv[3])) return JS_EXCEPTION;
        if (JS_ToInt32(ctx, &type, argv[4])) return JS_EXCEPTION;
        
        JSValue source = argv[5];
        if (JS_IsNull(source) || JS_IsUndefined(source)) {
            /* null source - creates uninitialized texture */
            glTexImage2D(target, level, internalformat, 0, 0, 0, format, type, NULL);
        } else {
            /* Try to get width/height/data from source object */
            JSValue w_val = JS_GetPropertyStr(ctx, source, "width");
            JSValue h_val = JS_GetPropertyStr(ctx, source, "height");
            JSValue data_val = JS_GetPropertyStr(ctx, source, "data");
            
            int width = 0, height = 0;
            JS_ToInt32(ctx, &width, w_val);
            JS_ToInt32(ctx, &height, h_val);
            
            const void *pixels = NULL;
            size_t size = 0;
            if (!JS_IsUndefined(data_val)) {
                pixels = get_texture_data(ctx, data_val, &size);
            }
            
            glTexImage2D(target, level, internalformat, width, height, 0, format, type, pixels);
            
            JS_FreeValue(ctx, w_val);
            JS_FreeValue(ctx, h_val);
            JS_FreeValue(ctx, data_val);
        }
    }
    
    return JS_UNDEFINED;
}

/* texSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels)
 * Also has short form with source object */
static JSValue js_webgl_texSubImage2D(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
    int target, level, xoffset, yoffset;
    
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &level, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &xoffset, argv[2])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &yoffset, argv[3])) return JS_EXCEPTION;
    
    if (argc >= 9) {
        /* Full form */
        int width, height, format, type;
        if (JS_ToInt32(ctx, &width, argv[4])) return JS_EXCEPTION;
        if (JS_ToInt32(ctx, &height, argv[5])) return JS_EXCEPTION;
        if (JS_ToInt32(ctx, &format, argv[6])) return JS_EXCEPTION;
        if (JS_ToInt32(ctx, &type, argv[7])) return JS_EXCEPTION;
        
        const void *pixels = NULL;
        if (!JS_IsNull(argv[8]) && !JS_IsUndefined(argv[8])) {
            size_t size;
            pixels = get_texture_data(ctx, argv[8], &size);
        }
        
        glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
    } else if (argc >= 7) {
        /* Short form with source: texSubImage2D(target, level, xoffset, yoffset, format, type, source) */
        int format, type;
        if (JS_ToInt32(ctx, &format, argv[4])) return JS_EXCEPTION;
        if (JS_ToInt32(ctx, &type, argv[5])) return JS_EXCEPTION;
        
        JSValue source = argv[6];
        if (!JS_IsNull(source) && !JS_IsUndefined(source)) {
            JSValue w_val = JS_GetPropertyStr(ctx, source, "width");
            JSValue h_val = JS_GetPropertyStr(ctx, source, "height");
            JSValue data_val = JS_GetPropertyStr(ctx, source, "data");
            
            int width = 0, height = 0;
            JS_ToInt32(ctx, &width, w_val);
            JS_ToInt32(ctx, &height, h_val);
            
            const void *pixels = NULL;
            size_t size = 0;
            if (!JS_IsUndefined(data_val)) {
                pixels = get_texture_data(ctx, data_val, &size);
            }
            
            glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
            
            JS_FreeValue(ctx, w_val);
            JS_FreeValue(ctx, h_val);
            JS_FreeValue(ctx, data_val);
        }
    }
    
    return JS_UNDEFINED;
}

/* texImage3D (WebGL 2) */
static JSValue js_webgl_texImage3D(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    int target, level, internalformat, width, height, depth, border, format, type;
    
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &level, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &internalformat, argv[2])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &width, argv[3])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &height, argv[4])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &depth, argv[5])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &border, argv[6])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &format, argv[7])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &type, argv[8])) return JS_EXCEPTION;
    
    const void *pixels = NULL;
    if (argc > 9 && !JS_IsNull(argv[9]) && !JS_IsUndefined(argv[9])) {
        size_t size;
        pixels = get_texture_data(ctx, argv[9], &size);
    }
    
    glTexImage3D(target, level, internalformat, width, height, depth, border, format, type, pixels);
    return JS_UNDEFINED;
}

/* texSubImage3D (WebGL 2) */
static JSValue js_webgl_texSubImage3D(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
    int target, level, xoffset, yoffset, zoffset, width, height, depth, format, type;
    
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &level, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &xoffset, argv[2])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &yoffset, argv[3])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &zoffset, argv[4])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &width, argv[5])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &height, argv[6])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &depth, argv[7])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &format, argv[8])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &type, argv[9])) return JS_EXCEPTION;
    
    const void *pixels = NULL;
    if (argc > 10 && !JS_IsNull(argv[10]) && !JS_IsUndefined(argv[10])) {
        size_t size;
        pixels = get_texture_data(ctx, argv[10], &size);
    }
    
    glTexSubImage3D(target, level, xoffset, yoffset, zoffset, width, height, depth, format, type, pixels);
    return JS_UNDEFINED;
}

/* texStorage2D (WebGL 2) - immutable texture storage */
static JSValue js_webgl_texStorage2D(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv) {
    int target, levels, internalformat, width, height;
    
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &levels, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &internalformat, argv[2])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &width, argv[3])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &height, argv[4])) return JS_EXCEPTION;
    
    glTexStorage2D(target, levels, internalformat, width, height);
    return JS_UNDEFINED;
}

/* texStorage3D (WebGL 2) */
static JSValue js_webgl_texStorage3D(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv) {
    int target, levels, internalformat, width, height, depth;
    
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &levels, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &internalformat, argv[2])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &width, argv[3])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &height, argv[4])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &depth, argv[5])) return JS_EXCEPTION;
    
    glTexStorage3D(target, levels, internalformat, width, height, depth);
    return JS_UNDEFINED;
}

/* copyTexImage2D */
static JSValue js_webgl_copyTexImage2D(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv) {
    int target, level, internalformat, x, y, width, height, border;
    
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &level, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &internalformat, argv[2])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &x, argv[3])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &y, argv[4])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &width, argv[5])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &height, argv[6])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &border, argv[7])) return JS_EXCEPTION;
    
    glCopyTexImage2D(target, level, internalformat, x, y, width, height, border);
    return JS_UNDEFINED;
}

/* copyTexSubImage2D */
static JSValue js_webgl_copyTexSubImage2D(JSContext *ctx, JSValueConst this_val,
                                           int argc, JSValueConst *argv) {
    int target, level, xoffset, yoffset, x, y, width, height;
    
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &level, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &xoffset, argv[2])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &yoffset, argv[3])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &x, argv[4])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &y, argv[5])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &width, argv[6])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &height, argv[7])) return JS_EXCEPTION;
    
    glCopyTexSubImage2D(target, level, xoffset, yoffset, x, y, width, height);
    return JS_UNDEFINED;
}

/* copyTexSubImage3D (WebGL 2) */
static JSValue js_webgl_copyTexSubImage3D(JSContext *ctx, JSValueConst this_val,
                                           int argc, JSValueConst *argv) {
    int target, level, xoffset, yoffset, zoffset, x, y, width, height;
    
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &level, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &xoffset, argv[2])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &yoffset, argv[3])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &zoffset, argv[4])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &x, argv[5])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &y, argv[6])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &width, argv[7])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &height, argv[8])) return JS_EXCEPTION;
    
    glCopyTexSubImage3D(target, level, xoffset, yoffset, zoffset, x, y, width, height);
    return JS_UNDEFINED;
}

/* compressedTexImage2D */
static JSValue js_webgl_compressedTexImage2D(JSContext *ctx, JSValueConst this_val,
                                              int argc, JSValueConst *argv) {
    int target, level, internalformat, width, height, border;
    
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &level, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &internalformat, argv[2])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &width, argv[3])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &height, argv[4])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &border, argv[5])) return JS_EXCEPTION;
    
    size_t size = 0;
    const void *data = NULL;
    if (argc > 6 && !JS_IsNull(argv[6]) && !JS_IsUndefined(argv[6])) {
        data = get_texture_data(ctx, argv[6], &size);
    }
    
    glCompressedTexImage2D(target, level, internalformat, width, height, border, (GLsizei)size, data);
    return JS_UNDEFINED;
}

/* compressedTexSubImage2D */
static JSValue js_webgl_compressedTexSubImage2D(JSContext *ctx, JSValueConst this_val,
                                                 int argc, JSValueConst *argv) {
    int target, level, xoffset, yoffset, width, height, format;
    
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &level, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &xoffset, argv[2])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &yoffset, argv[3])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &width, argv[4])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &height, argv[5])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &format, argv[6])) return JS_EXCEPTION;
    
    size_t size = 0;
    const void *data = NULL;
    if (argc > 7 && !JS_IsNull(argv[7]) && !JS_IsUndefined(argv[7])) {
        data = get_texture_data(ctx, argv[7], &size);
    }
    
    glCompressedTexSubImage2D(target, level, xoffset, yoffset, width, height, format, (GLsizei)size, data);
    return JS_UNDEFINED;
}

/* compressedTexImage3D (WebGL 2) */
static JSValue js_webgl_compressedTexImage3D(JSContext *ctx, JSValueConst this_val,
                                              int argc, JSValueConst *argv) {
    int target, level, internalformat, width, height, depth, border;
    
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &level, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &internalformat, argv[2])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &width, argv[3])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &height, argv[4])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &depth, argv[5])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &border, argv[6])) return JS_EXCEPTION;
    
    size_t size = 0;
    const void *data = NULL;
    if (argc > 7 && !JS_IsNull(argv[7]) && !JS_IsUndefined(argv[7])) {
        data = get_texture_data(ctx, argv[7], &size);
    }
    
    glCompressedTexImage3D(target, level, internalformat, width, height, depth, border, (GLsizei)size, data);
    return JS_UNDEFINED;
}

/* compressedTexSubImage3D (WebGL 2) */
static JSValue js_webgl_compressedTexSubImage3D(JSContext *ctx, JSValueConst this_val,
                                                 int argc, JSValueConst *argv) {
    int target, level, xoffset, yoffset, zoffset, width, height, depth, format;
    
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &level, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &xoffset, argv[2])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &yoffset, argv[3])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &zoffset, argv[4])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &width, argv[5])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &height, argv[6])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &depth, argv[7])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &format, argv[8])) return JS_EXCEPTION;
    
    size_t size = 0;
    const void *data = NULL;
    if (argc > 9 && !JS_IsNull(argv[9]) && !JS_IsUndefined(argv[9])) {
        data = get_texture_data(ctx, argv[9], &size);
    }
    
    glCompressedTexSubImage3D(target, level, xoffset, yoffset, zoffset, width, height, depth, format, (GLsizei)size, data);
    return JS_UNDEFINED;
}

/* isTexture */
static JSValue js_webgl_isTexture(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) {
        return JS_FALSE;
    }
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_texture_class_id);
    GLuint gl_tex = hashmap_get(wctx->textures, js_handle);
    
    if (gl_tex && glIsTexture(gl_tex)) {
        return JS_TRUE;
    }
    return JS_FALSE;
}

/* getTexParameter */
static JSValue js_webgl_getTexParameter(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv) {
    int target, pname;
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &pname, argv[1])) return JS_EXCEPTION;
    
    /* Most texture parameters return integers */
    switch (pname) {
        case GL_TEXTURE_MAG_FILTER:
        case GL_TEXTURE_MIN_FILTER:
        case GL_TEXTURE_WRAP_S:
        case GL_TEXTURE_WRAP_T:
        case GL_TEXTURE_WRAP_R:
        case GL_TEXTURE_COMPARE_MODE:
        case GL_TEXTURE_COMPARE_FUNC:
        case GL_TEXTURE_BASE_LEVEL:
        case GL_TEXTURE_MAX_LEVEL:
        case GL_TEXTURE_IMMUTABLE_FORMAT:
        case GL_TEXTURE_IMMUTABLE_LEVELS: {
            GLint value;
            glGetTexParameteriv(target, pname, &value);
            return JS_NewInt32(ctx, value);
        }
        case GL_TEXTURE_MIN_LOD:
        case GL_TEXTURE_MAX_LOD: {
            GLfloat value;
            glGetTexParameterfv(target, pname, &value);
            return JS_NewFloat64(ctx, value);
        }
        default: {
            GLint value;
            glGetTexParameteriv(target, pname, &value);
            return JS_NewInt32(ctx, value);
        }
    }
}

/* --------------------------------------------------------------------------
 * Sampler Objects (WebGL 2)
 * -------------------------------------------------------------------------- */

static JSValue js_webgl_createSampler(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    GLuint gl_sampler;
    glGenSamplers(1, &gl_sampler);
    
    uint32_t js_handle = hashmap_alloc(wctx->samplers, gl_sampler);
    return create_webgl_object(ctx, js_webgl_sampler_class_id, js_handle);
}

static JSValue js_webgl_deleteSampler(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) return JS_UNDEFINED;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_sampler_class_id);
    GLuint gl_sampler = hashmap_remove(wctx->samplers, js_handle);
    
    if (gl_sampler) {
        glDeleteSamplers(1, &gl_sampler);
    }
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_isSampler(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) {
        return JS_FALSE;
    }
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_sampler_class_id);
    GLuint gl_sampler = hashmap_get(wctx->samplers, js_handle);
    
    if (gl_sampler && glIsSampler(gl_sampler)) {
        return JS_TRUE;
    }
    return JS_FALSE;
}

static JSValue js_webgl_bindSampler(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    int unit;
    if (JS_ToInt32(ctx, &unit, argv[0])) return JS_EXCEPTION;
    
    GLuint gl_sampler = 0;
    if (!JS_IsNull(argv[1]) && !JS_IsUndefined(argv[1])) {
        uint32_t js_handle = get_webgl_object_handle(ctx, argv[1], js_webgl_sampler_class_id);
        gl_sampler = hashmap_get(wctx->samplers, js_handle);
    }
    
    glBindSampler(unit, gl_sampler);
    return JS_UNDEFINED;
}

static JSValue js_webgl_samplerParameteri(JSContext *ctx, JSValueConst this_val,
                                           int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) return JS_UNDEFINED;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_sampler_class_id);
    GLuint gl_sampler = hashmap_get(wctx->samplers, js_handle);
    
    int pname, param;
    if (JS_ToInt32(ctx, &pname, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &param, argv[2])) return JS_EXCEPTION;
    
    glSamplerParameteri(gl_sampler, pname, param);
    return JS_UNDEFINED;
}

static JSValue js_webgl_samplerParameterf(JSContext *ctx, JSValueConst this_val,
                                           int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) return JS_UNDEFINED;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_sampler_class_id);
    GLuint gl_sampler = hashmap_get(wctx->samplers, js_handle);
    
    int pname;
    double param;
    if (JS_ToInt32(ctx, &pname, argv[1])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &param, argv[2])) return JS_EXCEPTION;
    
    glSamplerParameterf(gl_sampler, pname, (GLfloat)param);
    return JS_UNDEFINED;
}

static JSValue js_webgl_getSamplerParameter(JSContext *ctx, JSValueConst this_val,
                                             int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) return JS_NULL;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_sampler_class_id);
    GLuint gl_sampler = hashmap_get(wctx->samplers, js_handle);
    
    int pname;
    if (JS_ToInt32(ctx, &pname, argv[1])) return JS_EXCEPTION;
    
    switch (pname) {
        case GL_TEXTURE_MAG_FILTER:
        case GL_TEXTURE_MIN_FILTER:
        case GL_TEXTURE_WRAP_S:
        case GL_TEXTURE_WRAP_T:
        case GL_TEXTURE_WRAP_R:
        case GL_TEXTURE_COMPARE_MODE:
        case GL_TEXTURE_COMPARE_FUNC: {
            GLint value;
            glGetSamplerParameteriv(gl_sampler, pname, &value);
            return JS_NewInt32(ctx, value);
        }
        case GL_TEXTURE_MIN_LOD:
        case GL_TEXTURE_MAX_LOD: {
            GLfloat value;
            glGetSamplerParameterfv(gl_sampler, pname, &value);
            return JS_NewFloat64(ctx, value);
        }
        default: {
            GLint value;
            glGetSamplerParameteriv(gl_sampler, pname, &value);
            return JS_NewInt32(ctx, value);
        }
    }
}

/* ==========================================================================
 * Phase 4: Framebuffer Operations
 * ========================================================================== */

/* --------------------------------------------------------------------------
 * Framebuffer Objects
 * -------------------------------------------------------------------------- */

static JSValue js_webgl_createFramebuffer(JSContext *ctx, JSValueConst this_val,
                                           int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    GLuint gl_fbo;
    glGenFramebuffers(1, &gl_fbo);
    
    uint32_t js_handle = hashmap_alloc(wctx->framebuffers, gl_fbo);
    return create_webgl_object(ctx, js_webgl_framebuffer_class_id, js_handle);
}

static JSValue js_webgl_deleteFramebuffer(JSContext *ctx, JSValueConst this_val,
                                           int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) return JS_UNDEFINED;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_framebuffer_class_id);
    GLuint gl_fbo = hashmap_remove(wctx->framebuffers, js_handle);
    
    if (gl_fbo) {
        glDeleteFramebuffers(1, &gl_fbo);
    }
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_bindFramebuffer(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    int target;
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    
    GLuint gl_fbo = 0;
    if (!JS_IsNull(argv[1]) && !JS_IsUndefined(argv[1])) {
        uint32_t js_handle = get_webgl_object_handle(ctx, argv[1], js_webgl_framebuffer_class_id);
        gl_fbo = hashmap_get(wctx->framebuffers, js_handle);
    }
    
    glBindFramebuffer(target, gl_fbo);
    wctx->bound_framebuffer = gl_fbo;
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_isFramebuffer(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) return JS_FALSE;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_framebuffer_class_id);
    GLuint gl_fbo = hashmap_get(wctx->framebuffers, js_handle);
    
    if (gl_fbo && glIsFramebuffer(gl_fbo)) return JS_TRUE;
    return JS_FALSE;
}

static JSValue js_webgl_checkFramebufferStatus(JSContext *ctx, JSValueConst this_val,
                                                int argc, JSValueConst *argv) {
    int target;
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    
    GLenum status = glCheckFramebufferStatus(target);
    return JS_NewInt32(ctx, status);
}

static JSValue js_webgl_framebufferTexture2D(JSContext *ctx, JSValueConst this_val,
                                              int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    int target, attachment, textarget, level;
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &attachment, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &textarget, argv[2])) return JS_EXCEPTION;
    
    GLuint gl_tex = 0;
    if (!JS_IsNull(argv[3]) && !JS_IsUndefined(argv[3])) {
        uint32_t js_handle = get_webgl_object_handle(ctx, argv[3], js_webgl_texture_class_id);
        gl_tex = hashmap_get(wctx->textures, js_handle);
    }
    
    if (JS_ToInt32(ctx, &level, argv[4])) return JS_EXCEPTION;
    
    glFramebufferTexture2D(target, attachment, textarget, gl_tex, level);
    return JS_UNDEFINED;
}

static JSValue js_webgl_framebufferTextureLayer(JSContext *ctx, JSValueConst this_val,
                                                 int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    int target, attachment, level, layer;
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &attachment, argv[1])) return JS_EXCEPTION;
    
    GLuint gl_tex = 0;
    if (!JS_IsNull(argv[2]) && !JS_IsUndefined(argv[2])) {
        uint32_t js_handle = get_webgl_object_handle(ctx, argv[2], js_webgl_texture_class_id);
        gl_tex = hashmap_get(wctx->textures, js_handle);
    }
    
    if (JS_ToInt32(ctx, &level, argv[3])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &layer, argv[4])) return JS_EXCEPTION;
    
    glFramebufferTextureLayer(target, attachment, gl_tex, level, layer);
    return JS_UNDEFINED;
}

/* --------------------------------------------------------------------------
 * Renderbuffer Objects
 * -------------------------------------------------------------------------- */

static JSValue js_webgl_createRenderbuffer(JSContext *ctx, JSValueConst this_val,
                                            int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    GLuint gl_rbo;
    glGenRenderbuffers(1, &gl_rbo);
    
    uint32_t js_handle = hashmap_alloc(wctx->renderbuffers, gl_rbo);
    return create_webgl_object(ctx, js_webgl_renderbuffer_class_id, js_handle);
}

static JSValue js_webgl_deleteRenderbuffer(JSContext *ctx, JSValueConst this_val,
                                            int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) return JS_UNDEFINED;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_renderbuffer_class_id);
    GLuint gl_rbo = hashmap_remove(wctx->renderbuffers, js_handle);
    
    if (gl_rbo) {
        glDeleteRenderbuffers(1, &gl_rbo);
    }
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_bindRenderbuffer(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    int target;
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    
    GLuint gl_rbo = 0;
    if (!JS_IsNull(argv[1]) && !JS_IsUndefined(argv[1])) {
        uint32_t js_handle = get_webgl_object_handle(ctx, argv[1], js_webgl_renderbuffer_class_id);
        gl_rbo = hashmap_get(wctx->renderbuffers, js_handle);
    }
    
    glBindRenderbuffer(target, gl_rbo);
    wctx->bound_renderbuffer = gl_rbo;
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_isRenderbuffer(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) return JS_FALSE;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_renderbuffer_class_id);
    GLuint gl_rbo = hashmap_get(wctx->renderbuffers, js_handle);
    
    if (gl_rbo && glIsRenderbuffer(gl_rbo)) return JS_TRUE;
    return JS_FALSE;
}

static JSValue js_webgl_renderbufferStorage(JSContext *ctx, JSValueConst this_val,
                                             int argc, JSValueConst *argv) {
    int target, internalformat, width, height;
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &internalformat, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &width, argv[2])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &height, argv[3])) return JS_EXCEPTION;
    
    glRenderbufferStorage(target, internalformat, width, height);
    return JS_UNDEFINED;
}

static JSValue js_webgl_renderbufferStorageMultisample(JSContext *ctx, JSValueConst this_val,
                                                        int argc, JSValueConst *argv) {
    int target, samples, internalformat, width, height;
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &samples, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &internalformat, argv[2])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &width, argv[3])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &height, argv[4])) return JS_EXCEPTION;
    
    glRenderbufferStorageMultisample(target, samples, internalformat, width, height);
    return JS_UNDEFINED;
}

static JSValue js_webgl_framebufferRenderbuffer(JSContext *ctx, JSValueConst this_val,
                                                 int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    int target, attachment, renderbuffertarget;
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &attachment, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &renderbuffertarget, argv[2])) return JS_EXCEPTION;
    
    GLuint gl_rbo = 0;
    if (!JS_IsNull(argv[3]) && !JS_IsUndefined(argv[3])) {
        uint32_t js_handle = get_webgl_object_handle(ctx, argv[3], js_webgl_renderbuffer_class_id);
        gl_rbo = hashmap_get(wctx->renderbuffers, js_handle);
    }
    
    glFramebufferRenderbuffer(target, attachment, renderbuffertarget, gl_rbo);
    return JS_UNDEFINED;
}

static JSValue js_webgl_getRenderbufferParameter(JSContext *ctx, JSValueConst this_val,
                                                  int argc, JSValueConst *argv) {
    int target, pname;
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &pname, argv[1])) return JS_EXCEPTION;
    
    GLint value;
    glGetRenderbufferParameteriv(target, pname, &value);
    return JS_NewInt32(ctx, value);
}

static JSValue js_webgl_getFramebufferAttachmentParameter(JSContext *ctx, JSValueConst this_val,
                                                           int argc, JSValueConst *argv) {
    int target, attachment, pname;
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &attachment, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &pname, argv[2])) return JS_EXCEPTION;
    
    GLint value;
    glGetFramebufferAttachmentParameteriv(target, attachment, pname, &value);
    return JS_NewInt32(ctx, value);
}

/* --------------------------------------------------------------------------
 * Read Pixels
 * -------------------------------------------------------------------------- */

static JSValue js_webgl_readPixels(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    int x, y, width, height, format, type;
    if (JS_ToInt32(ctx, &x, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &y, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &width, argv[2])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &height, argv[3])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &format, argv[4])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &type, argv[5])) return JS_EXCEPTION;
    
    /* Get destination buffer */
    size_t size;
    uint8_t *pixels = get_texture_data(ctx, argv[6], &size);
    if (!pixels) return JS_EXCEPTION;
    
    glReadPixels(x, y, width, height, format, type, pixels);
    return JS_UNDEFINED;
}

static JSValue js_webgl_readBuffer(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    int src;
    if (JS_ToInt32(ctx, &src, argv[0])) return JS_EXCEPTION;
    
    glReadBuffer(src);
    return JS_UNDEFINED;
}

/* --------------------------------------------------------------------------
 * Blit and Invalidate (WebGL 2)
 * -------------------------------------------------------------------------- */

static JSValue js_webgl_blitFramebuffer(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv) {
    int srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter;
    if (JS_ToInt32(ctx, &srcX0, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &srcY0, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &srcX1, argv[2])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &srcY1, argv[3])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &dstX0, argv[4])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &dstY0, argv[5])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &dstX1, argv[6])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &dstY1, argv[7])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &mask, argv[8])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &filter, argv[9])) return JS_EXCEPTION;
    
    glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter);
    return JS_UNDEFINED;
}

static JSValue js_webgl_invalidateFramebuffer(JSContext *ctx, JSValueConst this_val,
                                               int argc, JSValueConst *argv) {
    int target;
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    
    /* Get attachments array */
    JSValue len_val = JS_GetPropertyStr(ctx, argv[1], "length");
    int32_t count;
    if (JS_ToInt32(ctx, &count, len_val)) {
        JS_FreeValue(ctx, len_val);
        return JS_EXCEPTION;
    }
    JS_FreeValue(ctx, len_val);
    
    GLenum *attachments = (GLenum *)malloc(count * sizeof(GLenum));
    if (!attachments) return JS_EXCEPTION;
    
    for (int i = 0; i < count; i++) {
        JSValue elem = JS_GetPropertyUint32(ctx, argv[1], i);
        int32_t att;
        if (JS_ToInt32(ctx, &att, elem)) {
            JS_FreeValue(ctx, elem);
            free(attachments);
            return JS_EXCEPTION;
        }
        attachments[i] = att;
        JS_FreeValue(ctx, elem);
    }
    
    glInvalidateFramebuffer(target, count, attachments);
    free(attachments);
    return JS_UNDEFINED;
}

static JSValue js_webgl_invalidateSubFramebuffer(JSContext *ctx, JSValueConst this_val,
                                                  int argc, JSValueConst *argv) {
    int target;
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    
    /* Get attachments array */
    JSValue len_val = JS_GetPropertyStr(ctx, argv[1], "length");
    int32_t count;
    if (JS_ToInt32(ctx, &count, len_val)) {
        JS_FreeValue(ctx, len_val);
        return JS_EXCEPTION;
    }
    JS_FreeValue(ctx, len_val);
    
    GLenum *attachments = (GLenum *)malloc(count * sizeof(GLenum));
    if (!attachments) return JS_EXCEPTION;
    
    for (int i = 0; i < count; i++) {
        JSValue elem = JS_GetPropertyUint32(ctx, argv[1], i);
        int32_t att;
        if (JS_ToInt32(ctx, &att, elem)) {
            JS_FreeValue(ctx, elem);
            free(attachments);
            return JS_EXCEPTION;
        }
        attachments[i] = att;
        JS_FreeValue(ctx, elem);
    }
    
    int x, y, width, height;
    if (JS_ToInt32(ctx, &x, argv[2])) { free(attachments); return JS_EXCEPTION; }
    if (JS_ToInt32(ctx, &y, argv[3])) { free(attachments); return JS_EXCEPTION; }
    if (JS_ToInt32(ctx, &width, argv[4])) { free(attachments); return JS_EXCEPTION; }
    if (JS_ToInt32(ctx, &height, argv[5])) { free(attachments); return JS_EXCEPTION; }
    
    glInvalidateSubFramebuffer(target, count, attachments, x, y, width, height);
    free(attachments);
    return JS_UNDEFINED;
}

/* ==========================================================================
 * Phase 6: Uniform Buffer Objects (WebGL 2)
 * ========================================================================== */

static JSValue js_webgl_getUniformBlockIndex(JSContext *ctx, JSValueConst this_val,
                                              int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) return JS_NewUint32(ctx, GL_INVALID_INDEX);
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_program_class_id);
    GLuint gl_prog = hashmap_get(wctx->programs, js_handle);
    
    const char *name = JS_ToCString(ctx, argv[1]);
    if (!name) return JS_EXCEPTION;
    
    GLuint index = glGetUniformBlockIndex(gl_prog, name);
    JS_FreeCString(ctx, name);
    
    return JS_NewUint32(ctx, index);
}

static JSValue js_webgl_uniformBlockBinding(JSContext *ctx, JSValueConst this_val,
                                             int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) return JS_UNDEFINED;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_program_class_id);
    GLuint gl_prog = hashmap_get(wctx->programs, js_handle);
    
    uint32_t blockIndex, blockBinding;
    if (JS_ToUint32(ctx, &blockIndex, argv[1])) return JS_EXCEPTION;
    if (JS_ToUint32(ctx, &blockBinding, argv[2])) return JS_EXCEPTION;
    
    glUniformBlockBinding(gl_prog, blockIndex, blockBinding);
    return JS_UNDEFINED;
}

static JSValue js_webgl_bindBufferBase(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    int target;
    uint32_t index;
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    if (JS_ToUint32(ctx, &index, argv[1])) return JS_EXCEPTION;
    
    GLuint gl_buf = 0;
    if (!JS_IsNull(argv[2]) && !JS_IsUndefined(argv[2])) {
        uint32_t js_handle = get_webgl_object_handle(ctx, argv[2], js_webgl_buffer_class_id);
        gl_buf = hashmap_get(wctx->buffers, js_handle);
    }
    
    glBindBufferBase(target, index, gl_buf);
    return JS_UNDEFINED;
}

static JSValue js_webgl_bindBufferRange(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    int target;
    uint32_t index;
    int64_t offset, size;
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    if (JS_ToUint32(ctx, &index, argv[1])) return JS_EXCEPTION;
    
    GLuint gl_buf = 0;
    if (!JS_IsNull(argv[2]) && !JS_IsUndefined(argv[2])) {
        uint32_t js_handle = get_webgl_object_handle(ctx, argv[2], js_webgl_buffer_class_id);
        gl_buf = hashmap_get(wctx->buffers, js_handle);
    }
    
    if (JS_ToInt64(ctx, &offset, argv[3])) return JS_EXCEPTION;
    if (JS_ToInt64(ctx, &size, argv[4])) return JS_EXCEPTION;
    
    glBindBufferRange(target, index, gl_buf, offset, size);
    return JS_UNDEFINED;
}

static JSValue js_webgl_getActiveUniformBlockParameter(JSContext *ctx, JSValueConst this_val,
                                                        int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) return JS_NULL;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_program_class_id);
    GLuint gl_prog = hashmap_get(wctx->programs, js_handle);
    
    uint32_t blockIndex;
    int pname;
    if (JS_ToUint32(ctx, &blockIndex, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &pname, argv[2])) return JS_EXCEPTION;
    
    switch (pname) {
        case GL_UNIFORM_BLOCK_BINDING:
        case GL_UNIFORM_BLOCK_DATA_SIZE:
        case GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS: {
            GLint value;
            glGetActiveUniformBlockiv(gl_prog, blockIndex, pname, &value);
            return JS_NewInt32(ctx, value);
        }
        case GL_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES: {
            GLint count;
            glGetActiveUniformBlockiv(gl_prog, blockIndex, GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS, &count);
            GLint *indices = (GLint *)malloc(count * sizeof(GLint));
            if (!indices) return JS_EXCEPTION;
            glGetActiveUniformBlockiv(gl_prog, blockIndex, pname, indices);
            JSValue arr = JS_NewArray(ctx);
            for (int i = 0; i < count; i++) {
                JS_SetPropertyUint32(ctx, arr, i, JS_NewInt32(ctx, indices[i]));
            }
            free(indices);
            return arr;
        }
        case GL_UNIFORM_BLOCK_REFERENCED_BY_VERTEX_SHADER:
        case GL_UNIFORM_BLOCK_REFERENCED_BY_FRAGMENT_SHADER: {
            GLint value;
            glGetActiveUniformBlockiv(gl_prog, blockIndex, pname, &value);
            return value ? JS_TRUE : JS_FALSE;
        }
        default: {
            GLint value;
            glGetActiveUniformBlockiv(gl_prog, blockIndex, pname, &value);
            return JS_NewInt32(ctx, value);
        }
    }
}

static JSValue js_webgl_getActiveUniformBlockName(JSContext *ctx, JSValueConst this_val,
                                                   int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) return JS_NULL;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_program_class_id);
    GLuint gl_prog = hashmap_get(wctx->programs, js_handle);
    
    uint32_t blockIndex;
    if (JS_ToUint32(ctx, &blockIndex, argv[1])) return JS_EXCEPTION;
    
    GLint nameLen;
    glGetActiveUniformBlockiv(gl_prog, blockIndex, GL_UNIFORM_BLOCK_NAME_LENGTH, &nameLen);
    
    char *name = (char *)malloc(nameLen);
    if (!name) return JS_EXCEPTION;
    
    glGetActiveUniformBlockName(gl_prog, blockIndex, nameLen, NULL, name);
    JSValue result = JS_NewString(ctx, name);
    free(name);
    
    return result;
}

static JSValue js_webgl_getActiveUniforms(JSContext *ctx, JSValueConst this_val,
                                           int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) return JS_NULL;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_program_class_id);
    GLuint gl_prog = hashmap_get(wctx->programs, js_handle);
    
    /* Get indices array */
    JSValue len_val = JS_GetPropertyStr(ctx, argv[1], "length");
    int32_t count;
    if (JS_ToInt32(ctx, &count, len_val)) {
        JS_FreeValue(ctx, len_val);
        return JS_EXCEPTION;
    }
    JS_FreeValue(ctx, len_val);
    
    GLuint *indices = (GLuint *)malloc(count * sizeof(GLuint));
    GLint *params = (GLint *)malloc(count * sizeof(GLint));
    if (!indices || !params) {
        free(indices);
        free(params);
        return JS_EXCEPTION;
    }
    
    for (int i = 0; i < count; i++) {
        JSValue elem = JS_GetPropertyUint32(ctx, argv[1], i);
        uint32_t idx;
        if (JS_ToUint32(ctx, &idx, elem)) {
            JS_FreeValue(ctx, elem);
            free(indices);
            free(params);
            return JS_EXCEPTION;
        }
        indices[i] = idx;
        JS_FreeValue(ctx, elem);
    }
    
    int pname;
    if (JS_ToInt32(ctx, &pname, argv[2])) {
        free(indices);
        free(params);
        return JS_EXCEPTION;
    }
    
    glGetActiveUniformsiv(gl_prog, count, indices, pname, params);
    
    JSValue arr = JS_NewArray(ctx);
    for (int i = 0; i < count; i++) {
        if (pname == GL_UNIFORM_IS_ROW_MAJOR) {
            JS_SetPropertyUint32(ctx, arr, i, params[i] ? JS_TRUE : JS_FALSE);
        } else {
            JS_SetPropertyUint32(ctx, arr, i, JS_NewInt32(ctx, params[i]));
        }
    }
    
    free(indices);
    free(params);
    return arr;
}

/* ==========================================================================
 * Phase 7: Advanced Features
 * ========================================================================== */

/* --------------------------------------------------------------------------
 * Query Objects (WebGL 2)
 * -------------------------------------------------------------------------- */

static JSValue js_webgl_createQuery(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    GLuint gl_query;
    glGenQueries(1, &gl_query);
    
    uint32_t js_handle = hashmap_alloc(wctx->queries, gl_query);
    return create_webgl_object(ctx, js_webgl_query_class_id, js_handle);
}

static JSValue js_webgl_deleteQuery(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) return JS_UNDEFINED;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_query_class_id);
    GLuint gl_query = hashmap_remove(wctx->queries, js_handle);
    
    if (gl_query) {
        glDeleteQueries(1, &gl_query);
    }
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_isQuery(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) return JS_FALSE;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_query_class_id);
    GLuint gl_query = hashmap_get(wctx->queries, js_handle);
    
    if (gl_query && glIsQuery(gl_query)) return JS_TRUE;
    return JS_FALSE;
}

static JSValue js_webgl_beginQuery(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    int target;
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    
    if (JS_IsNull(argv[1]) || JS_IsUndefined(argv[1])) return JS_UNDEFINED;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[1], js_webgl_query_class_id);
    GLuint gl_query = hashmap_get(wctx->queries, js_handle);
    
    glBeginQuery(target, gl_query);
    return JS_UNDEFINED;
}

static JSValue js_webgl_endQuery(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv) {
    int target;
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    
    glEndQuery(target);
    return JS_UNDEFINED;
}

static JSValue js_webgl_getQuery(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv) {
    int target, pname;
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &pname, argv[1])) return JS_EXCEPTION;
    
    GLint value;
    glGetQueryiv(target, pname, &value);
    
    /* CURRENT_QUERY returns a query object, others return int */
    if (pname == GL_CURRENT_QUERY) {
        if (value == 0) return JS_NULL;
        /* Note: In a full implementation, we'd need to reverse-lookup the JS object */
        return JS_NewInt32(ctx, value);
    }
    
    return JS_NewInt32(ctx, value);
}

static JSValue js_webgl_getQueryParameter(JSContext *ctx, JSValueConst this_val,
                                           int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) return JS_NULL;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_query_class_id);
    GLuint gl_query = hashmap_get(wctx->queries, js_handle);
    
    int pname;
    if (JS_ToInt32(ctx, &pname, argv[1])) return JS_EXCEPTION;
    
    if (pname == GL_QUERY_RESULT_AVAILABLE) {
        GLuint value;
        glGetQueryObjectuiv(gl_query, pname, &value);
        return value ? JS_TRUE : JS_FALSE;
    } else {
        GLuint value;
        glGetQueryObjectuiv(gl_query, pname, &value);
        return JS_NewUint32(ctx, value);
    }
}

/* --------------------------------------------------------------------------
 * Sync Objects (WebGL 2)
 * -------------------------------------------------------------------------- */

static JSValue js_webgl_fenceSync(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;

    int condition, flags;
    if (JS_ToInt32(ctx, &condition, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &flags, argv[1])) return JS_EXCEPTION;
    
    GLsync sync = glFenceSync(condition, flags);
    if (!sync) return JS_NULL;
    
    uint32_t js_handle = ptrmap_alloc(wctx->syncs, (uintptr_t)sync);
    if (!js_handle) {
        glDeleteSync(sync);
        return JS_ThrowOutOfMemory(ctx);
    }
    return create_webgl_object(ctx, js_webgl_sync_class_id, js_handle);
}

static JSValue js_webgl_deleteSync(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;

    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) return JS_UNDEFINED;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_sync_class_id);
    GLsync sync = (GLsync)ptrmap_remove(wctx->syncs, js_handle);
    
    if (sync) {
        glDeleteSync(sync);
    }
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_isSync(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;

    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) return JS_FALSE;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_sync_class_id);
    GLsync sync = (GLsync)ptrmap_get(wctx->syncs, js_handle);
    
    return JS_NewBool(ctx, sync && glIsSync(sync));
}

static JSValue js_webgl_clientWaitSync(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;

    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) {
        return JS_NewInt32(ctx, GL_WAIT_FAILED);
    }
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_sync_class_id);
    GLsync sync = (GLsync)ptrmap_get(wctx->syncs, js_handle);
    if (!sync) return JS_NewInt32(ctx, GL_WAIT_FAILED);
    
    int flags;
    int64_t timeout;
    if (JS_ToInt32(ctx, &flags, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt64(ctx, &timeout, argv[2])) return JS_EXCEPTION;
    
    GLuint64 to = (timeout < 0) ? (GLuint64)GL_TIMEOUT_IGNORED : (GLuint64)timeout;
    GLenum result = glClientWaitSync(sync, (GLbitfield)flags, to);
    return JS_NewInt32(ctx, result);
}

static JSValue js_webgl_waitSync(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;

    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) return JS_UNDEFINED;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_sync_class_id);
    GLsync sync = (GLsync)ptrmap_get(wctx->syncs, js_handle);
    if (!sync) return JS_UNDEFINED;
    
    int flags;
    int64_t timeout;
    if (JS_ToInt32(ctx, &flags, argv[1])) return JS_EXCEPTION;
    if (JS_ToInt64(ctx, &timeout, argv[2])) return JS_EXCEPTION;
    
    GLuint64 to = (timeout < 0) ? (GLuint64)GL_TIMEOUT_IGNORED : (GLuint64)timeout;
    glWaitSync(sync, (GLbitfield)flags, to);
    return JS_UNDEFINED;
}

static JSValue js_webgl_getSyncParameter(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;

    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) return JS_NULL;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_sync_class_id);
    GLsync sync = (GLsync)ptrmap_get(wctx->syncs, js_handle);
    if (!sync) return JS_NULL;
    
    int pname;
    if (JS_ToInt32(ctx, &pname, argv[1])) return JS_EXCEPTION;
    
    GLint value;
    GLsizei len;
    glGetSynciv(sync, pname, 1, &len, &value);
    
    if (pname == GL_SYNC_STATUS) {
        return JS_NewInt32(ctx, value);
    }
    return JS_NewInt32(ctx, value);
}

/* --------------------------------------------------------------------------
 * Transform Feedback (WebGL 2)
 * -------------------------------------------------------------------------- */

static JSValue js_webgl_createTransformFeedback(JSContext *ctx, JSValueConst this_val,
                                                 int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    GLuint gl_tf;
    glGenTransformFeedbacks(1, &gl_tf);
    
    uint32_t js_handle = hashmap_alloc(wctx->transform_feedbacks, gl_tf);
    return create_webgl_object(ctx, js_webgl_transform_feedback_class_id, js_handle);
}

static JSValue js_webgl_deleteTransformFeedback(JSContext *ctx, JSValueConst this_val,
                                                 int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) return JS_UNDEFINED;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_transform_feedback_class_id);
    GLuint gl_tf = hashmap_remove(wctx->transform_feedbacks, js_handle);
    
    if (gl_tf) {
        glDeleteTransformFeedbacks(1, &gl_tf);
    }
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_isTransformFeedback(JSContext *ctx, JSValueConst this_val,
                                             int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) return JS_FALSE;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_transform_feedback_class_id);
    GLuint gl_tf = hashmap_get(wctx->transform_feedbacks, js_handle);
    
    if (gl_tf && glIsTransformFeedback(gl_tf)) return JS_TRUE;
    return JS_FALSE;
}

static JSValue js_webgl_bindTransformFeedback(JSContext *ctx, JSValueConst this_val,
                                               int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    int target;
    if (JS_ToInt32(ctx, &target, argv[0])) return JS_EXCEPTION;
    
    GLuint gl_tf = 0;
    if (!JS_IsNull(argv[1]) && !JS_IsUndefined(argv[1])) {
        uint32_t js_handle = get_webgl_object_handle(ctx, argv[1], js_webgl_transform_feedback_class_id);
        gl_tf = hashmap_get(wctx->transform_feedbacks, js_handle);
    }
    
    glBindTransformFeedback(target, gl_tf);
    return JS_UNDEFINED;
}

static JSValue js_webgl_beginTransformFeedback(JSContext *ctx, JSValueConst this_val,
                                                int argc, JSValueConst *argv) {
    int primitiveMode;
    if (JS_ToInt32(ctx, &primitiveMode, argv[0])) return JS_EXCEPTION;
    
    glBeginTransformFeedback(primitiveMode);
    return JS_UNDEFINED;
}

static JSValue js_webgl_endTransformFeedback(JSContext *ctx, JSValueConst this_val,
                                              int argc, JSValueConst *argv) {
    glEndTransformFeedback();
    return JS_UNDEFINED;
}

static JSValue js_webgl_pauseTransformFeedback(JSContext *ctx, JSValueConst this_val,
                                                int argc, JSValueConst *argv) {
    glPauseTransformFeedback();
    return JS_UNDEFINED;
}

static JSValue js_webgl_resumeTransformFeedback(JSContext *ctx, JSValueConst this_val,
                                                 int argc, JSValueConst *argv) {
    glResumeTransformFeedback();
    return JS_UNDEFINED;
}

static JSValue js_webgl_transformFeedbackVaryings(JSContext *ctx, JSValueConst this_val,
                                                   int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) return JS_UNDEFINED;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_program_class_id);
    GLuint gl_prog = hashmap_get(wctx->programs, js_handle);
    
    /* Get varyings array */
    JSValue len_val = JS_GetPropertyStr(ctx, argv[1], "length");
    int32_t count;
    if (JS_ToInt32(ctx, &count, len_val)) {
        JS_FreeValue(ctx, len_val);
        return JS_EXCEPTION;
    }
    JS_FreeValue(ctx, len_val);
    
    const char **varyings = (const char **)malloc(count * sizeof(char *));
    if (!varyings) return JS_EXCEPTION;
    
    for (int i = 0; i < count; i++) {
        JSValue elem = JS_GetPropertyUint32(ctx, argv[1], i);
        varyings[i] = JS_ToCString(ctx, elem);
        JS_FreeValue(ctx, elem);
        if (!varyings[i]) {
            for (int j = 0; j < i; j++) JS_FreeCString(ctx, varyings[j]);
            free(varyings);
            return JS_EXCEPTION;
        }
    }
    
    int bufferMode;
    if (JS_ToInt32(ctx, &bufferMode, argv[2])) {
        for (int i = 0; i < count; i++) JS_FreeCString(ctx, varyings[i]);
        free(varyings);
        return JS_EXCEPTION;
    }
    
    glTransformFeedbackVaryings(gl_prog, count, varyings, bufferMode);
    
    for (int i = 0; i < count; i++) JS_FreeCString(ctx, varyings[i]);
    free(varyings);
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_getTransformFeedbackVarying(JSContext *ctx, JSValueConst this_val,
                                                     int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    if (JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) return JS_NULL;
    
    uint32_t js_handle = get_webgl_object_handle(ctx, argv[0], js_webgl_program_class_id);
    GLuint gl_prog = hashmap_get(wctx->programs, js_handle);
    
    uint32_t index;
    if (JS_ToUint32(ctx, &index, argv[1])) return JS_EXCEPTION;
    
    char name[256];
    GLsizei length;
    GLsizei size;
    GLenum type;
    
    glGetTransformFeedbackVarying(gl_prog, index, sizeof(name), &length, &size, &type, name);
    
    /* Return WebGLActiveInfo-like object */
    JSValue info = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, info, "name", JS_NewString(ctx, name));
    JS_SetPropertyStr(ctx, info, "size", JS_NewInt32(ctx, size));
    JS_SetPropertyStr(ctx, info, "type", JS_NewInt32(ctx, type));
    
    return info;
}

/* --------------------------------------------------------------------------
 * Multiple Render Targets (WebGL 2)
 * -------------------------------------------------------------------------- */

static JSValue js_webgl_drawBuffers(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {
    /* Get buffers array */
    JSValue len_val = JS_GetPropertyStr(ctx, argv[0], "length");
    int32_t count;
    if (JS_ToInt32(ctx, &count, len_val)) {
        JS_FreeValue(ctx, len_val);
        return JS_EXCEPTION;
    }
    JS_FreeValue(ctx, len_val);
    
    GLenum *buffers = (GLenum *)malloc(count * sizeof(GLenum));
    if (!buffers) return JS_EXCEPTION;
    
    for (int i = 0; i < count; i++) {
        JSValue elem = JS_GetPropertyUint32(ctx, argv[0], i);
        int32_t buf;
        if (JS_ToInt32(ctx, &buf, elem)) {
            JS_FreeValue(ctx, elem);
            free(buffers);
            return JS_EXCEPTION;
        }
        buffers[i] = buf;
        JS_FreeValue(ctx, elem);
    }
    
    glDrawBuffers(count, buffers);
    free(buffers);
    return JS_UNDEFINED;
}

static JSValue js_webgl_clearBufferfv(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
    int buffer, drawbuffer;
    if (JS_ToInt32(ctx, &buffer, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &drawbuffer, argv[1])) return JS_EXCEPTION;
    
    /* Get values from array/TypedArray */
    size_t size;
    uint8_t *data = get_texture_data(ctx, argv[2], &size);
    
    if (data) {
        glClearBufferfv(buffer, drawbuffer, (GLfloat *)data);
    } else {
        /* Try as regular array */
        GLfloat values[4] = {0};
        JSValue len_val = JS_GetPropertyStr(ctx, argv[2], "length");
        int32_t count;
        JS_ToInt32(ctx, &count, len_val);
        JS_FreeValue(ctx, len_val);
        
        for (int i = 0; i < count && i < 4; i++) {
            JSValue elem = JS_GetPropertyUint32(ctx, argv[2], i);
            double v;
            JS_ToFloat64(ctx, &v, elem);
            values[i] = (GLfloat)v;
            JS_FreeValue(ctx, elem);
        }
        glClearBufferfv(buffer, drawbuffer, values);
    }
    
    return JS_UNDEFINED;
}

static JSValue js_webgl_clearBufferiv(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
    int buffer, drawbuffer;
    if (JS_ToInt32(ctx, &buffer, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &drawbuffer, argv[1])) return JS_EXCEPTION;
    
    /* Get values from array */
    GLint values[4] = {0};
    JSValue len_val = JS_GetPropertyStr(ctx, argv[2], "length");
    int32_t count;
    JS_ToInt32(ctx, &count, len_val);
    JS_FreeValue(ctx, len_val);
    
    for (int i = 0; i < count && i < 4; i++) {
        JSValue elem = JS_GetPropertyUint32(ctx, argv[2], i);
        int32_t v;
        JS_ToInt32(ctx, &v, elem);
        values[i] = v;
        JS_FreeValue(ctx, elem);
    }
    
    glClearBufferiv(buffer, drawbuffer, values);
    return JS_UNDEFINED;
}

static JSValue js_webgl_clearBufferuiv(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv) {
    int buffer, drawbuffer;
    if (JS_ToInt32(ctx, &buffer, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &drawbuffer, argv[1])) return JS_EXCEPTION;
    
    /* Get values from array */
    GLuint values[4] = {0};
    JSValue len_val = JS_GetPropertyStr(ctx, argv[2], "length");
    int32_t count;
    JS_ToInt32(ctx, &count, len_val);
    JS_FreeValue(ctx, len_val);
    
    for (int i = 0; i < count && i < 4; i++) {
        JSValue elem = JS_GetPropertyUint32(ctx, argv[2], i);
        uint32_t v;
        JS_ToUint32(ctx, &v, elem);
        values[i] = v;
        JS_FreeValue(ctx, elem);
    }
    
    glClearBufferuiv(buffer, drawbuffer, values);
    return JS_UNDEFINED;
}

static JSValue js_webgl_clearBufferfi(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv) {
    int buffer, drawbuffer;
    double depth;
    int stencil;
    
    if (JS_ToInt32(ctx, &buffer, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &drawbuffer, argv[1])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &depth, argv[2])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &stencil, argv[3])) return JS_EXCEPTION;
    
    glClearBufferfi(buffer, drawbuffer, (GLfloat)depth, stencil);
    return JS_UNDEFINED;
}

/* --------------------------------------------------------------------------
 * Finish/Flush
 * -------------------------------------------------------------------------- */

static JSValue js_webgl_flush(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv) {
    glFlush();
    return JS_UNDEFINED;
}

static JSValue js_webgl_finish(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv) {
    glFinish();
    return JS_UNDEFINED;
}

/* --------------------------------------------------------------------------
 * getParameter (subset)
 * -------------------------------------------------------------------------- */

static JSValue js_webgl_getParameter(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv) {
    WebGLContext *wctx = get_webgl_context(ctx, this_val);
    if (!wctx) return JS_EXCEPTION;
    
    int pname;
    if (JS_ToInt32(ctx, &pname, argv[0])) return JS_EXCEPTION;
    
    switch (pname) {
        case GL_VIEWPORT: {
            GLint v[4];
            glGetIntegerv(GL_VIEWPORT, v);
            JSValue arr = JS_NewArray(ctx);
            for (int i = 0; i < 4; i++) {
                JS_SetPropertyUint32(ctx, arr, i, JS_NewInt32(ctx, v[i]));
            }
            return arr;
        }
        case GL_SCISSOR_BOX: {
            GLint v[4];
            glGetIntegerv(GL_SCISSOR_BOX, v);
            JSValue arr = JS_NewArray(ctx);
            for (int i = 0; i < 4; i++) {
                JS_SetPropertyUint32(ctx, arr, i, JS_NewInt32(ctx, v[i]));
            }
            return arr;
        }
        case GL_MAX_TEXTURE_SIZE:
        case GL_MAX_CUBE_MAP_TEXTURE_SIZE:
        case GL_MAX_TEXTURE_IMAGE_UNITS:
        case GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS:
        case GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS:
        case GL_MAX_VERTEX_ATTRIBS:
        case GL_MAX_RENDERBUFFER_SIZE:
        case GL_MAX_3D_TEXTURE_SIZE:
        case GL_MAX_ARRAY_TEXTURE_LAYERS:
        case GL_MAX_DRAW_BUFFERS:
        case GL_MAX_COLOR_ATTACHMENTS:
        case GL_MAX_SAMPLES: {
            GLint v;
            glGetIntegerv(pname, &v);
            return JS_NewInt32(ctx, v);
        }
        case GL_VENDOR:
        case GL_RENDERER:
        case GL_VERSION:
        case GL_SHADING_LANGUAGE_VERSION: {
            const char *s = (const char *)glGetString(pname);
            return s ? JS_NewString(ctx, s) : JS_NULL;
        }
        case GL_BLEND:
            return JS_NewBool(ctx, wctx->blend_enabled);
        case GL_DEPTH_TEST:
            return JS_NewBool(ctx, wctx->depth_test_enabled);
        case GL_CULL_FACE:
            return JS_NewBool(ctx, wctx->cull_face_enabled);
        case GL_SCISSOR_TEST:
            return JS_NewBool(ctx, wctx->scissor_test_enabled);
        case GL_STENCIL_TEST:
            return JS_NewBool(ctx, wctx->stencil_test_enabled);
        default:
            return JS_NULL;
    }
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
            ptrmap_destroy(wctx->syncs);
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

/* Helper macro for registering methods */
#define REG_METHOD(name, func, nargs) \
    JS_SetPropertyStr(ctx, webgl_ctx_proto, name, JS_NewCFunction(ctx, func, name, nargs))

void minirend_webgl_register(JSContext *ctx, MinirendApp *app) {
    (void)app;
    
    /* Register all WebGL classes */
    register_webgl_classes(ctx);
    
    /* Create context prototype with all methods and constants */
    webgl_ctx_proto = JS_NewObject(ctx);
    
    /* Context info methods */
    REG_METHOD("getError", js_webgl_getError, 0);
    REG_METHOD("isContextLost", js_webgl_isContextLost, 0);
    REG_METHOD("getContextAttributes", js_webgl_getContextAttributes, 0);
    REG_METHOD("getSupportedExtensions", js_webgl_getSupportedExtensions, 0);
    REG_METHOD("getExtension", js_webgl_getExtension, 1);
    REG_METHOD("getParameter", js_webgl_getParameter, 1);
    
    /* State methods */
    REG_METHOD("enable", js_webgl_enable, 1);
    REG_METHOD("disable", js_webgl_disable, 1);
    REG_METHOD("isEnabled", js_webgl_isEnabled, 1);
    
    /* Viewport and scissor */
    REG_METHOD("viewport", js_webgl_viewport, 4);
    REG_METHOD("scissor", js_webgl_scissor, 4);
    REG_METHOD("depthRange", js_webgl_depthRange, 2);
    
    /* Clear methods */
    REG_METHOD("clearColor", js_webgl_clearColor, 4);
    REG_METHOD("clearDepth", js_webgl_clearDepth, 1);
    REG_METHOD("clearStencil", js_webgl_clearStencil, 1);
    REG_METHOD("clear", js_webgl_clear, 1);
    
    /* Blend state */
    REG_METHOD("blendFunc", js_webgl_blendFunc, 2);
    REG_METHOD("blendFuncSeparate", js_webgl_blendFuncSeparate, 4);
    REG_METHOD("blendEquation", js_webgl_blendEquation, 1);
    REG_METHOD("blendEquationSeparate", js_webgl_blendEquationSeparate, 2);
    REG_METHOD("blendColor", js_webgl_blendColor, 4);
    
    /* Depth state */
    REG_METHOD("depthFunc", js_webgl_depthFunc, 1);
    REG_METHOD("depthMask", js_webgl_depthMask, 1);
    
    /* Stencil state */
    REG_METHOD("stencilFunc", js_webgl_stencilFunc, 3);
    REG_METHOD("stencilFuncSeparate", js_webgl_stencilFuncSeparate, 4);
    REG_METHOD("stencilOp", js_webgl_stencilOp, 3);
    REG_METHOD("stencilOpSeparate", js_webgl_stencilOpSeparate, 4);
    REG_METHOD("stencilMask", js_webgl_stencilMask, 1);
    REG_METHOD("stencilMaskSeparate", js_webgl_stencilMaskSeparate, 2);
    
    /* Cull face */
    REG_METHOD("cullFace", js_webgl_cullFace, 1);
    REG_METHOD("frontFace", js_webgl_frontFace, 1);
    
    /* Color mask */
    REG_METHOD("colorMask", js_webgl_colorMask, 4);
    
    /* Pixel store */
    REG_METHOD("pixelStorei", js_webgl_pixelStorei, 2);
    
    /* Buffer methods */
    REG_METHOD("createBuffer", js_webgl_createBuffer, 0);
    REG_METHOD("bindBuffer", js_webgl_bindBuffer, 2);
    REG_METHOD("bufferData", js_webgl_bufferData, 3);
    REG_METHOD("bufferSubData", js_webgl_bufferSubData, 3);
    REG_METHOD("deleteBuffer", js_webgl_deleteBuffer, 1);
    REG_METHOD("isBuffer", js_webgl_isBuffer, 1);
    
    /* Shader methods */
    REG_METHOD("createShader", js_webgl_createShader, 1);
    REG_METHOD("shaderSource", js_webgl_shaderSource, 2);
    REG_METHOD("compileShader", js_webgl_compileShader, 1);
    REG_METHOD("getShaderParameter", js_webgl_getShaderParameter, 2);
    REG_METHOD("getShaderInfoLog", js_webgl_getShaderInfoLog, 1);
    REG_METHOD("deleteShader", js_webgl_deleteShader, 1);
    REG_METHOD("isShader", js_webgl_isShader, 1);
    
    /* Program methods */
    REG_METHOD("createProgram", js_webgl_createProgram, 0);
    REG_METHOD("attachShader", js_webgl_attachShader, 2);
    REG_METHOD("detachShader", js_webgl_detachShader, 2);
    REG_METHOD("linkProgram", js_webgl_linkProgram, 1);
    REG_METHOD("useProgram", js_webgl_useProgram, 1);
    REG_METHOD("validateProgram", js_webgl_validateProgram, 1);
    REG_METHOD("getProgramParameter", js_webgl_getProgramParameter, 2);
    REG_METHOD("getProgramInfoLog", js_webgl_getProgramInfoLog, 1);
    REG_METHOD("deleteProgram", js_webgl_deleteProgram, 1);
    REG_METHOD("isProgram", js_webgl_isProgram, 1);
    REG_METHOD("getActiveUniform", js_webgl_getActiveUniform, 2);
    REG_METHOD("getActiveAttrib", js_webgl_getActiveAttrib, 2);
    
    /* Attribute methods */
    REG_METHOD("getAttribLocation", js_webgl_getAttribLocation, 2);
    REG_METHOD("bindAttribLocation", js_webgl_bindAttribLocation, 3);
    REG_METHOD("enableVertexAttribArray", js_webgl_enableVertexAttribArray, 1);
    REG_METHOD("disableVertexAttribArray", js_webgl_disableVertexAttribArray, 1);
    REG_METHOD("vertexAttribPointer", js_webgl_vertexAttribPointer, 6);
    REG_METHOD("vertexAttribIPointer", js_webgl_vertexAttribIPointer, 5);
    REG_METHOD("vertexAttribDivisor", js_webgl_vertexAttribDivisor, 2);
    
    /* Uniform methods */
    REG_METHOD("getUniformLocation", js_webgl_getUniformLocation, 2);
    REG_METHOD("uniform1i", js_webgl_uniform1i, 2);
    REG_METHOD("uniform2i", js_webgl_uniform2i, 3);
    REG_METHOD("uniform3i", js_webgl_uniform3i, 4);
    REG_METHOD("uniform4i", js_webgl_uniform4i, 5);
    REG_METHOD("uniform1f", js_webgl_uniform1f, 2);
    REG_METHOD("uniform2f", js_webgl_uniform2f, 3);
    REG_METHOD("uniform3f", js_webgl_uniform3f, 4);
    REG_METHOD("uniform4f", js_webgl_uniform4f, 5);
    REG_METHOD("uniform1fv", js_webgl_uniform1fv, 2);
    REG_METHOD("uniform2fv", js_webgl_uniform2fv, 2);
    REG_METHOD("uniform3fv", js_webgl_uniform3fv, 2);
    REG_METHOD("uniform4fv", js_webgl_uniform4fv, 2);
    REG_METHOD("uniformMatrix2fv", js_webgl_uniformMatrix2fv, 3);
    REG_METHOD("uniformMatrix3fv", js_webgl_uniformMatrix3fv, 3);
    REG_METHOD("uniformMatrix4fv", js_webgl_uniformMatrix4fv, 3);
    
    /* Drawing methods */
    REG_METHOD("drawArrays", js_webgl_drawArrays, 3);
    REG_METHOD("drawElements", js_webgl_drawElements, 4);
    REG_METHOD("drawArraysInstanced", js_webgl_drawArraysInstanced, 4);
    REG_METHOD("drawElementsInstanced", js_webgl_drawElementsInstanced, 5);
    
    /* VAO methods (WebGL 2) */
    REG_METHOD("createVertexArray", js_webgl_createVertexArray, 0);
    REG_METHOD("bindVertexArray", js_webgl_bindVertexArray, 1);
    REG_METHOD("deleteVertexArray", js_webgl_deleteVertexArray, 1);
    REG_METHOD("isVertexArray", js_webgl_isVertexArray, 1);
    
    /* Texture methods */
    REG_METHOD("createTexture", js_webgl_createTexture, 0);
    REG_METHOD("bindTexture", js_webgl_bindTexture, 2);
    REG_METHOD("deleteTexture", js_webgl_deleteTexture, 1);
    REG_METHOD("isTexture", js_webgl_isTexture, 1);
    REG_METHOD("activeTexture", js_webgl_activeTexture, 1);
    REG_METHOD("texParameteri", js_webgl_texParameteri, 3);
    REG_METHOD("texParameterf", js_webgl_texParameterf, 3);
    REG_METHOD("getTexParameter", js_webgl_getTexParameter, 2);
    REG_METHOD("generateMipmap", js_webgl_generateMipmap, 1);
    REG_METHOD("texImage2D", js_webgl_texImage2D, 9);
    REG_METHOD("texSubImage2D", js_webgl_texSubImage2D, 9);
    REG_METHOD("texImage3D", js_webgl_texImage3D, 10);
    REG_METHOD("texSubImage3D", js_webgl_texSubImage3D, 11);
    REG_METHOD("texStorage2D", js_webgl_texStorage2D, 5);
    REG_METHOD("texStorage3D", js_webgl_texStorage3D, 6);
    REG_METHOD("copyTexImage2D", js_webgl_copyTexImage2D, 8);
    REG_METHOD("copyTexSubImage2D", js_webgl_copyTexSubImage2D, 8);
    REG_METHOD("copyTexSubImage3D", js_webgl_copyTexSubImage3D, 9);
    REG_METHOD("compressedTexImage2D", js_webgl_compressedTexImage2D, 7);
    REG_METHOD("compressedTexSubImage2D", js_webgl_compressedTexSubImage2D, 8);
    REG_METHOD("compressedTexImage3D", js_webgl_compressedTexImage3D, 8);
    REG_METHOD("compressedTexSubImage3D", js_webgl_compressedTexSubImage3D, 10);
    
    /* Sampler methods (WebGL 2) */
    REG_METHOD("createSampler", js_webgl_createSampler, 0);
    REG_METHOD("deleteSampler", js_webgl_deleteSampler, 1);
    REG_METHOD("isSampler", js_webgl_isSampler, 1);
    REG_METHOD("bindSampler", js_webgl_bindSampler, 2);
    REG_METHOD("samplerParameteri", js_webgl_samplerParameteri, 3);
    REG_METHOD("samplerParameterf", js_webgl_samplerParameterf, 3);
    REG_METHOD("getSamplerParameter", js_webgl_getSamplerParameter, 2);
    
    /* Framebuffer methods */
    REG_METHOD("createFramebuffer", js_webgl_createFramebuffer, 0);
    REG_METHOD("deleteFramebuffer", js_webgl_deleteFramebuffer, 1);
    REG_METHOD("bindFramebuffer", js_webgl_bindFramebuffer, 2);
    REG_METHOD("isFramebuffer", js_webgl_isFramebuffer, 1);
    REG_METHOD("checkFramebufferStatus", js_webgl_checkFramebufferStatus, 1);
    REG_METHOD("framebufferTexture2D", js_webgl_framebufferTexture2D, 5);
    REG_METHOD("framebufferTextureLayer", js_webgl_framebufferTextureLayer, 5);
    REG_METHOD("framebufferRenderbuffer", js_webgl_framebufferRenderbuffer, 4);
    REG_METHOD("getFramebufferAttachmentParameter", js_webgl_getFramebufferAttachmentParameter, 3);
    REG_METHOD("blitFramebuffer", js_webgl_blitFramebuffer, 10);
    REG_METHOD("invalidateFramebuffer", js_webgl_invalidateFramebuffer, 2);
    REG_METHOD("invalidateSubFramebuffer", js_webgl_invalidateSubFramebuffer, 6);
    REG_METHOD("readBuffer", js_webgl_readBuffer, 1);
    REG_METHOD("readPixels", js_webgl_readPixels, 7);
    
    /* Renderbuffer methods */
    REG_METHOD("createRenderbuffer", js_webgl_createRenderbuffer, 0);
    REG_METHOD("deleteRenderbuffer", js_webgl_deleteRenderbuffer, 1);
    REG_METHOD("bindRenderbuffer", js_webgl_bindRenderbuffer, 2);
    REG_METHOD("isRenderbuffer", js_webgl_isRenderbuffer, 1);
    REG_METHOD("renderbufferStorage", js_webgl_renderbufferStorage, 4);
    REG_METHOD("renderbufferStorageMultisample", js_webgl_renderbufferStorageMultisample, 5);
    REG_METHOD("getRenderbufferParameter", js_webgl_getRenderbufferParameter, 2);
    
    /* Uniform Buffer Objects (WebGL 2) */
    REG_METHOD("getUniformBlockIndex", js_webgl_getUniformBlockIndex, 2);
    REG_METHOD("uniformBlockBinding", js_webgl_uniformBlockBinding, 3);
    REG_METHOD("bindBufferBase", js_webgl_bindBufferBase, 3);
    REG_METHOD("bindBufferRange", js_webgl_bindBufferRange, 5);
    REG_METHOD("getActiveUniformBlockParameter", js_webgl_getActiveUniformBlockParameter, 3);
    REG_METHOD("getActiveUniformBlockName", js_webgl_getActiveUniformBlockName, 2);
    REG_METHOD("getActiveUniforms", js_webgl_getActiveUniforms, 3);
    
    /* Query Objects (WebGL 2) */
    REG_METHOD("createQuery", js_webgl_createQuery, 0);
    REG_METHOD("deleteQuery", js_webgl_deleteQuery, 1);
    REG_METHOD("isQuery", js_webgl_isQuery, 1);
    REG_METHOD("beginQuery", js_webgl_beginQuery, 2);
    REG_METHOD("endQuery", js_webgl_endQuery, 1);
    REG_METHOD("getQuery", js_webgl_getQuery, 2);
    REG_METHOD("getQueryParameter", js_webgl_getQueryParameter, 2);
    
    /* Sync Objects (WebGL 2) */
    REG_METHOD("fenceSync", js_webgl_fenceSync, 2);
    REG_METHOD("deleteSync", js_webgl_deleteSync, 1);
    REG_METHOD("isSync", js_webgl_isSync, 1);
    REG_METHOD("clientWaitSync", js_webgl_clientWaitSync, 3);
    REG_METHOD("waitSync", js_webgl_waitSync, 3);
    REG_METHOD("getSyncParameter", js_webgl_getSyncParameter, 2);
    
    /* Transform Feedback (WebGL 2) */
    REG_METHOD("createTransformFeedback", js_webgl_createTransformFeedback, 0);
    REG_METHOD("deleteTransformFeedback", js_webgl_deleteTransformFeedback, 1);
    REG_METHOD("isTransformFeedback", js_webgl_isTransformFeedback, 1);
    REG_METHOD("bindTransformFeedback", js_webgl_bindTransformFeedback, 2);
    REG_METHOD("beginTransformFeedback", js_webgl_beginTransformFeedback, 1);
    REG_METHOD("endTransformFeedback", js_webgl_endTransformFeedback, 0);
    REG_METHOD("pauseTransformFeedback", js_webgl_pauseTransformFeedback, 0);
    REG_METHOD("resumeTransformFeedback", js_webgl_resumeTransformFeedback, 0);
    REG_METHOD("transformFeedbackVaryings", js_webgl_transformFeedbackVaryings, 3);
    REG_METHOD("getTransformFeedbackVarying", js_webgl_getTransformFeedbackVarying, 2);
    
    /* Multiple Render Targets (WebGL 2) */
    REG_METHOD("drawBuffers", js_webgl_drawBuffers, 1);
    REG_METHOD("clearBufferfv", js_webgl_clearBufferfv, 3);
    REG_METHOD("clearBufferiv", js_webgl_clearBufferiv, 3);
    REG_METHOD("clearBufferuiv", js_webgl_clearBufferuiv, 3);
    REG_METHOD("clearBufferfi", js_webgl_clearBufferfi, 4);
    
    /* Finish/Flush */
    REG_METHOD("flush", js_webgl_flush, 0);
    REG_METHOD("finish", js_webgl_finish, 0);
    
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
