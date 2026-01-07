/*
 * Text Renderer Implementation
 *
 * Batched textured quad rendering for text using font cache.
 */

#include "text_renderer.h"
#include "font_cache.h"
#include "sokol_gfx.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ============================================================================
 * Constants
 * ============================================================================ */

#define MAX_GLYPHS 4096
#define VERTICES_PER_GLYPH 4
#define INDICES_PER_GLYPH 6

/* ============================================================================
 * Vertex Format
 * ============================================================================ */

typedef struct {
    float x, y;           /* Position */
    float u, v;           /* Texture coordinates */
    float r, g, b, a;     /* Color */
} TextVertex;

/* ============================================================================
 * Shader Source
 * ============================================================================ */

static const char *text_vs_glsl330 =
    "#version 330\n"
    "uniform vec2 u_viewport;\n"
    "in vec2 a_pos;\n"
    "in vec2 a_uv;\n"
    "in vec4 a_color;\n"
    "out vec2 v_uv;\n"
    "out vec4 v_color;\n"
    "void main() {\n"
    "    vec2 pos = a_pos / u_viewport * 2.0 - 1.0;\n"
    "    pos.y = -pos.y;\n"
    "    gl_Position = vec4(pos, 0.0, 1.0);\n"
    "    v_uv = a_uv;\n"
    "    v_color = a_color;\n"
    "}\n";

static const char *text_fs_glsl330 =
    "#version 330\n"
    "uniform sampler2D u_texture;\n"
    "in vec2 v_uv;\n"
    "in vec4 v_color;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "    float alpha = texture(u_texture, v_uv).r;\n"
    "    frag_color = vec4(v_color.rgb, v_color.a * alpha);\n"
    "}\n";

static const char *text_vs_glsl100 =
    "#version 100\n"
    "uniform vec2 u_viewport;\n"
    "attribute vec2 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "attribute vec4 a_color;\n"
    "varying vec2 v_uv;\n"
    "varying vec4 v_color;\n"
    "void main() {\n"
    "    vec2 pos = a_pos / u_viewport * 2.0 - 1.0;\n"
    "    pos.y = -pos.y;\n"
    "    gl_Position = vec4(pos, 0.0, 1.0);\n"
    "    v_uv = a_uv;\n"
    "    v_color = a_color;\n"
    "}\n";

static const char *text_fs_glsl100 =
    "#version 100\n"
    "precision mediump float;\n"
    "uniform sampler2D u_texture;\n"
    "varying vec2 v_uv;\n"
    "varying vec4 v_color;\n"
    "void main() {\n"
    "    float alpha = texture2D(u_texture, v_uv).r;\n"
    "    gl_FragColor = vec4(v_color.rgb, v_color.a * alpha);\n"
    "}\n";

/* ============================================================================
 * Renderer Structure
 * ============================================================================ */

struct MinirendTextRenderer {
    MinirendFontCache *font_cache;  /* Borrowed, not owned */
    
    sg_shader     shader;
    sg_pipeline   pipeline;
    sg_buffer     vbuf;
    sg_buffer     ibuf;
    sg_bindings   bindings;
    sg_sampler    sampler;
    
    TextVertex   *vertices;
    int           vertex_count;
    int           glyph_count;
    
    float         viewport_width;
    float         viewport_height;
    
    bool          in_frame;
};

/* ============================================================================
 * Create/Destroy
 * ============================================================================ */

MinirendTextRenderer *minirend_text_renderer_create(MinirendFontCache *font_cache) {
    if (!font_cache) return NULL;
    
    MinirendTextRenderer *r = calloc(1, sizeof(MinirendTextRenderer));
    if (!r) return NULL;
    
    r->font_cache = font_cache;
    
    /* Allocate CPU-side vertex buffer */
    r->vertices = calloc(MAX_GLYPHS * VERTICES_PER_GLYPH, sizeof(TextVertex));
    if (!r->vertices) {
        free(r);
        return NULL;
    }
    
    /* Create shader */
    sg_shader_desc shader_desc = {
        .vs = {
            .source = text_vs_glsl330,
            .uniform_blocks[0] = {
                .size = 8,
                .uniforms[0] = {
                    .name = "u_viewport",
                    .type = SG_UNIFORMTYPE_FLOAT2,
                },
            },
        },
        .fs = {
            .source = text_fs_glsl330,
            .images[0] = {
                .used = true,
                .image_type = SG_IMAGETYPE_2D,
                .sample_type = SG_IMAGESAMPLETYPE_FLOAT,
            },
            .samplers[0] = {
                .used = true,
                .sampler_type = SG_SAMPLERTYPE_FILTERING,
            },
            .image_sampler_pairs[0] = {
                .used = true,
                .glsl_name = "u_texture",
                .image_slot = 0,
                .sampler_slot = 0,
            },
        },
        .attrs = {
            [0] = { .name = "a_pos" },
            [1] = { .name = "a_uv" },
            [2] = { .name = "a_color" },
        },
    };
    
    r->shader = sg_make_shader(&shader_desc);
    if (r->shader.id == SG_INVALID_ID) {
        /* Try GLSL 100 fallback */
        shader_desc.vs.source = text_vs_glsl100;
        shader_desc.fs.source = text_fs_glsl100;
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
                [0] = { .format = SG_VERTEXFORMAT_FLOAT2 },  /* a_pos */
                [1] = { .format = SG_VERTEXFORMAT_FLOAT2 },  /* a_uv */
                [2] = { .format = SG_VERTEXFORMAT_FLOAT4 },  /* a_color */
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
    
    /* Create sampler */
    r->sampler = sg_make_sampler(&(sg_sampler_desc){
        .min_filter = SG_FILTER_LINEAR,
        .mag_filter = SG_FILTER_LINEAR,
        .wrap_u = SG_WRAP_CLAMP_TO_EDGE,
        .wrap_v = SG_WRAP_CLAMP_TO_EDGE,
    });
    
    /* Create dynamic vertex buffer */
    r->vbuf = sg_make_buffer(&(sg_buffer_desc){
        .size = MAX_GLYPHS * VERTICES_PER_GLYPH * sizeof(TextVertex),
        .usage = SG_USAGE_STREAM,
    });
    
    /* Create static index buffer */
    uint16_t *indices = calloc(MAX_GLYPHS * INDICES_PER_GLYPH, sizeof(uint16_t));
    if (indices) {
        for (int i = 0; i < MAX_GLYPHS; i++) {
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
    r->bindings.fs.samplers[0] = r->sampler;
    
    return r;
}

void minirend_text_renderer_destroy(MinirendTextRenderer *r) {
    if (!r) return;
    
    sg_destroy_buffer(r->vbuf);
    sg_destroy_buffer(r->ibuf);
    sg_destroy_pipeline(r->pipeline);
    sg_destroy_shader(r->shader);
    sg_destroy_sampler(r->sampler);
    
    free(r->vertices);
    free(r);
}

/* ============================================================================
 * Frame Management
 * ============================================================================ */

void minirend_text_renderer_begin(MinirendTextRenderer *r,
                                  float viewport_width, float viewport_height) {
    if (!r) return;
    
    r->viewport_width = viewport_width;
    r->viewport_height = viewport_height;
    r->vertex_count = 0;
    r->glyph_count = 0;
    r->in_frame = true;
}

static void flush_batch(MinirendTextRenderer *r) {
    if (!r || r->glyph_count == 0) return;
    
    /* Get atlas texture */
    uint32_t tex_id = minirend_font_cache_get_texture(r->font_cache);
    r->bindings.fs.images[0] = (sg_image){ tex_id };
    
    /* Upload vertices */
    sg_update_buffer(r->vbuf, &(sg_range){
        .ptr = r->vertices,
        .size = r->vertex_count * sizeof(TextVertex),
    });
    
    /* Apply pipeline and bindings */
    sg_apply_pipeline(r->pipeline);
    sg_apply_bindings(&r->bindings);
    
    /* Set viewport uniform */
    float viewport[2] = { r->viewport_width, r->viewport_height };
    sg_apply_uniforms(SG_SHADERSTAGE_VS, 0, &SG_RANGE(viewport));
    
    /* Draw */
    sg_draw(0, r->glyph_count * INDICES_PER_GLYPH, 1);
    
    /* Reset batch */
    r->vertex_count = 0;
    r->glyph_count = 0;
}

void minirend_text_renderer_end(MinirendTextRenderer *r) {
    if (!r || !r->in_frame) return;
    
    flush_batch(r);
    r->in_frame = false;
}

/* ============================================================================
 * Drawing Functions
 * ============================================================================ */

static void push_glyph_quad(MinirendTextRenderer *r,
                            float x0, float y0, float x1, float y1,
                            float u0, float v0, float u1, float v1,
                            float cr, float cg, float cb, float ca) {
    if (!r || r->glyph_count >= MAX_GLYPHS) {
        flush_batch(r);
        if (r->glyph_count >= MAX_GLYPHS) return;
    }
    
    TextVertex *v = &r->vertices[r->vertex_count];
    
    /* Top-left */
    v[0].x = x0; v[0].y = y0;
    v[0].u = u0; v[0].v = v0;
    v[0].r = cr; v[0].g = cg; v[0].b = cb; v[0].a = ca;
    
    /* Top-right */
    v[1].x = x1; v[1].y = y0;
    v[1].u = u1; v[1].v = v0;
    v[1].r = cr; v[1].g = cg; v[1].b = cb; v[1].a = ca;
    
    /* Bottom-right */
    v[2].x = x1; v[2].y = y1;
    v[2].u = u1; v[2].v = v1;
    v[2].r = cr; v[2].g = cg; v[2].b = cb; v[2].a = ca;
    
    /* Bottom-left */
    v[3].x = x0; v[3].y = y1;
    v[3].u = u0; v[3].v = v1;
    v[3].r = cr; v[3].g = cg; v[3].b = cb; v[3].a = ca;
    
    r->vertex_count += 4;
    r->glyph_count++;
}

void minirend_text_draw_with_font(MinirendTextRenderer *r,
                                  int font_id,
                                  const char *text, int32_t len,
                                  float x, float y,
                                  float font_size, int font_weight,
                                  MinirendColor color) {
    if (!r || !text) return;
    
    (void)font_weight;  /* TODO: select font variant by weight */
    
    float cr = color.r / 255.0f;
    float cg = color.g / 255.0f;
    float cb = color.b / 255.0f;
    float ca = color.a / 255.0f;
    
    if (len < 0) len = (int32_t)strlen(text);
    
    float cursor_x = x;
    
    for (int32_t i = 0; i < len; i++) {
        int codepoint = (unsigned char)text[i];
        
        MinirendGlyph glyph;
        if (!minirend_font_cache_get_glyph(r->font_cache, font_id, codepoint,
                                          font_size, &glyph)) {
            continue;
        }
        
        /* Calculate quad position */
        float x0 = cursor_x + glyph.x_offset;
        float y0 = y + glyph.y_offset;
        float x1 = x0 + glyph.width;
        float y1 = y0 + glyph.height;
        
        /* Draw glyph quad */
        push_glyph_quad(r, x0, y0, x1, y1,
                        glyph.u0, glyph.v0, glyph.u1, glyph.v1,
                        cr, cg, cb, ca);
        
        cursor_x += glyph.advance;
    }
}

void minirend_text_draw(MinirendTextRenderer *r,
                        const char *text, int32_t len,
                        float x, float y,
                        float font_size, int font_weight,
                        MinirendColor color) {
    minirend_text_draw_with_font(r, -1, text, len, x, y, font_size, font_weight, color);
}

void minirend_text_measure(MinirendTextRenderer *r,
                           const char *text, int32_t len,
                           float font_size, int font_weight,
                           float *out_width, float *out_height) {
    if (!r || !r->font_cache) {
        if (out_width) *out_width = 0;
        if (out_height) *out_height = font_size;
        return;
    }
    
    (void)font_weight;
    minirend_font_cache_measure_text(r->font_cache, -1, text, len,
                                     font_size, out_width, out_height);
}

