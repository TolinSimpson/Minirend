/*
 * Box Renderer Implementation
 *
 * Batched quad rendering using sokol_gfx for backgrounds and borders.
 */

#include "box_renderer.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Sokol headers */
#include "sokol_gfx.h"

/* ============================================================================
 * Constants
 * ============================================================================ */

#define MAX_QUADS 4096
#define VERTICES_PER_QUAD 4
#define INDICES_PER_QUAD 6

/* ============================================================================
 * Vertex Format
 * ============================================================================ */

typedef struct {
    float x, y;           /* Position */
    float r, g, b, a;     /* Color (normalized 0-1) */
} BoxVertex;

/* ============================================================================
 * Shader Source
 * ============================================================================ */

/* Vertex shader - transforms 2D positions to clip space */
static const char *vs_source_glsl330 =
    "#version 330\n"
    "uniform vec2 u_viewport;\n"
    "in vec2 a_pos;\n"
    "in vec4 a_color;\n"
    "out vec4 v_color;\n"
    "void main() {\n"
    "    vec2 pos = a_pos / u_viewport * 2.0 - 1.0;\n"
    "    pos.y = -pos.y;\n"  /* Flip Y for screen coords */
    "    gl_Position = vec4(pos, 0.0, 1.0);\n"
    "    v_color = a_color;\n"
    "}\n";

/* Fragment shader - outputs vertex color */
static const char *fs_source_glsl330 =
    "#version 330\n"
    "in vec4 v_color;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "    frag_color = v_color;\n"
    "}\n";

/* GLSL 100 (OpenGL ES 2.0 / WebGL 1) */
static const char *vs_source_glsl100 =
    "#version 100\n"
    "uniform vec2 u_viewport;\n"
    "attribute vec2 a_pos;\n"
    "attribute vec4 a_color;\n"
    "varying vec4 v_color;\n"
    "void main() {\n"
    "    vec2 pos = a_pos / u_viewport * 2.0 - 1.0;\n"
    "    pos.y = -pos.y;\n"
    "    gl_Position = vec4(pos, 0.0, 1.0);\n"
    "    v_color = a_color;\n"
    "}\n";

static const char *fs_source_glsl100 =
    "#version 100\n"
    "precision mediump float;\n"
    "varying vec4 v_color;\n"
    "void main() {\n"
    "    gl_FragColor = v_color;\n"
    "}\n";

/* ============================================================================
 * Renderer Structure
 * ============================================================================ */

struct MinirendBoxRenderer {
    sg_shader     shader;
    sg_pipeline   pipeline;
    sg_buffer     vbuf;
    sg_buffer     ibuf;
    sg_bindings   bindings;
    
    BoxVertex    *vertices;
    int           vertex_count;
    int           quad_count;
    
    float         viewport_width;
    float         viewport_height;
    
    bool          in_frame;
    bool          scissor_active;
};

/* ============================================================================
 * Create/Destroy
 * ============================================================================ */

MinirendBoxRenderer *minirend_box_renderer_create(void) {
    MinirendBoxRenderer *r = calloc(1, sizeof(MinirendBoxRenderer));
    if (!r) return NULL;
    
    /* Allocate CPU-side vertex buffer */
    r->vertices = calloc(MAX_QUADS * VERTICES_PER_QUAD, sizeof(BoxVertex));
    if (!r->vertices) {
        free(r);
        return NULL;
    }
    
    /* Create shader */
    sg_shader_desc shader_desc = {
        .vs = {
            .source = vs_source_glsl330,
            .uniform_blocks[0] = {
                .size = 8,  /* 2 floats */
                .uniforms[0] = {
                    .name = "u_viewport",
                    .type = SG_UNIFORMTYPE_FLOAT2,
                },
            },
        },
        .fs = {
            .source = fs_source_glsl330,
        },
        .attrs = {
            [0] = { .name = "a_pos" },
            [1] = { .name = "a_color" },
        },
    };
    
    /* Try GLSL 330 first, fall back to GLSL 100 if needed */
    r->shader = sg_make_shader(&shader_desc);
    if (r->shader.id == SG_INVALID_ID) {
        shader_desc.vs.source = vs_source_glsl100;
        shader_desc.fs.source = fs_source_glsl100;
        r->shader = sg_make_shader(&shader_desc);
    }
    
    if (r->shader.id == SG_INVALID_ID) {
        free(r->vertices);
        free(r);
        return NULL;
    }
    
    /* Create pipeline */
    sg_pipeline_desc pipeline_desc = {
        .shader = r->shader,
        .layout = {
            .attrs = {
                [0] = { .format = SG_VERTEXFORMAT_FLOAT2 },   /* a_pos */
                [1] = { .format = SG_VERTEXFORMAT_FLOAT4 },   /* a_color */
            },
        },
        .colors[0] = {
            .blend = {
                .enabled = true,
                .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
                .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                .src_factor_alpha = SG_BLENDFACTOR_ONE,
                .dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            },
        },
        .depth = {
            .write_enabled = false,
            .compare = SG_COMPAREFUNC_ALWAYS,
        },
        .primitive_type = SG_PRIMITIVETYPE_TRIANGLES,
    };
    
    r->pipeline = sg_make_pipeline(&pipeline_desc);
    
    /* Create dynamic vertex buffer */
    r->vbuf = sg_make_buffer(&(sg_buffer_desc){
        .size = MAX_QUADS * VERTICES_PER_QUAD * sizeof(BoxVertex),
        .usage = SG_USAGE_STREAM,
    });
    
    /* Create static index buffer */
    uint16_t *indices = calloc(MAX_QUADS * INDICES_PER_QUAD, sizeof(uint16_t));
    if (indices) {
        for (int i = 0; i < MAX_QUADS; i++) {
            int vi = i * 4;
            int ii = i * 6;
            indices[ii + 0] = vi + 0;
            indices[ii + 1] = vi + 1;
            indices[ii + 2] = vi + 2;
            indices[ii + 3] = vi + 0;
            indices[ii + 4] = vi + 2;
            indices[ii + 5] = vi + 3;
        }
        
        r->ibuf = sg_make_buffer(&(sg_buffer_desc){
            .type = SG_BUFFERTYPE_INDEXBUFFER,
            .data = SG_RANGE(indices),
        });
        
        free(indices);
    }
    
    /* Set up bindings */
    r->bindings.vertex_buffers[0] = r->vbuf;
    r->bindings.index_buffer = r->ibuf;
    
    return r;
}

void minirend_box_renderer_destroy(MinirendBoxRenderer *r) {
    if (!r) return;
    
    sg_destroy_buffer(r->vbuf);
    sg_destroy_buffer(r->ibuf);
    sg_destroy_pipeline(r->pipeline);
    sg_destroy_shader(r->shader);
    
    free(r->vertices);
    free(r);
}

/* ============================================================================
 * Frame Management
 * ============================================================================ */

void minirend_box_renderer_begin(MinirendBoxRenderer *r,
                                 float viewport_width, float viewport_height) {
    if (!r) return;
    
    r->viewport_width = viewport_width;
    r->viewport_height = viewport_height;
    r->vertex_count = 0;
    r->quad_count = 0;
    r->in_frame = true;
    r->scissor_active = false;
}

static void flush_batch(MinirendBoxRenderer *r) {
    if (!r || r->quad_count == 0) return;
    
    /* Upload vertices */
    sg_update_buffer(r->vbuf, &(sg_range){
        .ptr = r->vertices,
        .size = r->vertex_count * sizeof(BoxVertex),
    });
    
    /* Apply pipeline and bindings */
    sg_apply_pipeline(r->pipeline);
    sg_apply_bindings(&r->bindings);
    
    /* Set viewport uniform */
    float viewport[2] = { r->viewport_width, r->viewport_height };
    sg_apply_uniforms(SG_SHADERSTAGE_VS, 0, &SG_RANGE(viewport));
    
    /* Draw */
    sg_draw(0, r->quad_count * INDICES_PER_QUAD, 1);
    
    /* Reset batch */
    r->vertex_count = 0;
    r->quad_count = 0;
}

void minirend_box_renderer_end(MinirendBoxRenderer *r) {
    if (!r || !r->in_frame) return;
    
    flush_batch(r);
    r->in_frame = false;
}

/* ============================================================================
 * Drawing Functions
 * ============================================================================ */

static void push_quad(MinirendBoxRenderer *r,
                      float x0, float y0, float x1, float y1,
                      float cr, float cg, float cb, float ca) {
    if (!r || r->quad_count >= MAX_QUADS) {
        flush_batch(r);
        if (r->quad_count >= MAX_QUADS) return;  /* Still full, give up */
    }
    
    BoxVertex *v = &r->vertices[r->vertex_count];
    
    /* Top-left */
    v[0].x = x0; v[0].y = y0;
    v[0].r = cr; v[0].g = cg; v[0].b = cb; v[0].a = ca;
    
    /* Top-right */
    v[1].x = x1; v[1].y = y0;
    v[1].r = cr; v[1].g = cg; v[1].b = cb; v[1].a = ca;
    
    /* Bottom-right */
    v[2].x = x1; v[2].y = y1;
    v[2].r = cr; v[2].g = cg; v[2].b = cb; v[2].a = ca;
    
    /* Bottom-left */
    v[3].x = x0; v[3].y = y1;
    v[3].r = cr; v[3].g = cg; v[3].b = cb; v[3].a = ca;
    
    r->vertex_count += 4;
    r->quad_count++;
}

void minirend_box_draw_rect(MinirendBoxRenderer *r,
                            float x, float y, float width, float height,
                            MinirendColor color) {
    if (!r || color.a == 0) return;
    
    float cr = color.r / 255.0f;
    float cg = color.g / 255.0f;
    float cb = color.b / 255.0f;
    float ca = color.a / 255.0f;
    
    push_quad(r, x, y, x + width, y + height, cr, cg, cb, ca);
}

void minirend_box_draw_rounded_rect(MinirendBoxRenderer *r,
                                    float x, float y, float width, float height,
                                    MinirendColor color, float radius) {
    /* For now, just draw a regular rect. Rounded corners would require
     * more complex geometry or a signed distance field shader. */
    (void)radius;
    minirend_box_draw_rect(r, x, y, width, height, color);
}

void minirend_box_draw_border(MinirendBoxRenderer *r,
                              float x, float y, float width, float height,
                              float border_top, float border_right,
                              float border_bottom, float border_left,
                              MinirendColor color) {
    if (!r || color.a == 0) return;
    
    float cr = color.r / 255.0f;
    float cg = color.g / 255.0f;
    float cb = color.b / 255.0f;
    float ca = color.a / 255.0f;
    
    /* Top border */
    if (border_top > 0) {
        push_quad(r, x, y, x + width, y + border_top, cr, cg, cb, ca);
    }
    
    /* Bottom border */
    if (border_bottom > 0) {
        push_quad(r, x, y + height - border_bottom, x + width, y + height,
                  cr, cg, cb, ca);
    }
    
    /* Left border (excluding corners) */
    if (border_left > 0) {
        push_quad(r, x, y + border_top,
                  x + border_left, y + height - border_bottom,
                  cr, cg, cb, ca);
    }
    
    /* Right border (excluding corners) */
    if (border_right > 0) {
        push_quad(r, x + width - border_right, y + border_top,
                  x + width, y + height - border_bottom,
                  cr, cg, cb, ca);
    }
}

void minirend_box_draw_rounded_border(MinirendBoxRenderer *r,
                                      float x, float y, float width, float height,
                                      float border_width,
                                      MinirendColor color, float radius) {
    /* For now, draw as regular border */
    (void)radius;
    minirend_box_draw_border(r, x, y, width, height,
                             border_width, border_width,
                             border_width, border_width, color);
}

void minirend_box_set_scissor(MinirendBoxRenderer *r,
                              float x, float y, float width, float height) {
    if (!r) return;
    
    /* Flush current batch before changing scissor */
    flush_batch(r);
    
    sg_apply_scissor_rect((int)x, (int)y, (int)width, (int)height, true);
    r->scissor_active = true;
}

void minirend_box_clear_scissor(MinirendBoxRenderer *r) {
    if (!r) return;
    
    /* Flush current batch before changing scissor */
    flush_batch(r);
    
    sg_apply_scissor_rect(0, 0, (int)r->viewport_width, (int)r->viewport_height, true);
    r->scissor_active = false;
}

