/*
 * Compositor Implementation
 *
 * Provides render-to-texture layer compositing for CSS effects.
 */

#include "compositor.h"
#include "sokol_gfx.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ============================================================================
 * Constants
 * ============================================================================ */

#define MAX_LAYERS 64

/* ============================================================================
 * Compositor Structure
 * ============================================================================ */

struct MinirendCompositor {
    /* Layers */
    MinirendLayer *layers[MAX_LAYERS];
    int            layer_count;
    
    /* Current render target stack */
    MinirendLayer *layer_stack[16];
    int            layer_stack_depth;
    
    /* Viewport */
    float viewport_width;
    float viewport_height;
    
    /* Compositing shader/pipeline */
    sg_shader   comp_shader;
    sg_pipeline comp_pipeline;
    sg_buffer   quad_vbuf;
    sg_buffer   quad_ibuf;
    sg_sampler  comp_sampler;
    
    /* Pass action for layer rendering */
    sg_pass_action layer_pass_action;
};

/* ============================================================================
 * Shader for Layer Compositing
 * ============================================================================ */

static const char *comp_vs_glsl330 =
    "#version 330\n"
    "uniform mat4 u_mvp;\n"
    "in vec2 a_pos;\n"
    "in vec2 a_uv;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    gl_Position = u_mvp * vec4(a_pos, 0.0, 1.0);\n"
    "    v_uv = a_uv;\n"
    "}\n";

static const char *comp_fs_glsl330 =
    "#version 330\n"
    "uniform sampler2D u_texture;\n"
    "uniform float u_opacity;\n"
    "in vec2 v_uv;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "    vec4 color = texture(u_texture, v_uv);\n"
    "    frag_color = vec4(color.rgb, color.a * u_opacity);\n"
    "}\n";

/* ============================================================================
 * Create/Destroy
 * ============================================================================ */

MinirendCompositor *minirend_compositor_create(void) {
    MinirendCompositor *c = calloc(1, sizeof(MinirendCompositor));
    if (!c) return NULL;
    
    /* Create compositing shader */
    sg_shader_desc shader_desc = {
        .vs = {
            .source = comp_vs_glsl330,
            .uniform_blocks[0] = {
                .size = 64,  /* mat4 */
                .uniforms[0] = { .name = "u_mvp", .type = SG_UNIFORMTYPE_MAT4 },
            },
        },
        .fs = {
            .source = comp_fs_glsl330,
            .uniform_blocks[0] = {
                .size = 4,  /* float */
                .uniforms[0] = { .name = "u_opacity", .type = SG_UNIFORMTYPE_FLOAT },
            },
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
        },
    };
    
    c->comp_shader = sg_make_shader(&shader_desc);
    
    /* Create pipeline */
    c->comp_pipeline = sg_make_pipeline(&(sg_pipeline_desc){
        .shader = c->comp_shader,
        .layout = {
            .attrs = {
                [0] = { .format = SG_VERTEXFORMAT_FLOAT2 },
                [1] = { .format = SG_VERTEXFORMAT_FLOAT2 },
            },
        },
        .colors[0] = {
            .blend = {
                .enabled = true,
                .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
                .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            },
        },
        .depth.write_enabled = false,
    });
    
    /* Create quad vertex buffer */
    float quad_vertices[] = {
        /* pos       uv */
        0.0f, 0.0f,  0.0f, 0.0f,
        1.0f, 0.0f,  1.0f, 0.0f,
        1.0f, 1.0f,  1.0f, 1.0f,
        0.0f, 1.0f,  0.0f, 1.0f,
    };
    c->quad_vbuf = sg_make_buffer(&(sg_buffer_desc){
        .data = SG_RANGE(quad_vertices),
    });
    
    /* Create quad index buffer */
    uint16_t quad_indices[] = { 0, 1, 2, 0, 2, 3 };
    c->quad_ibuf = sg_make_buffer(&(sg_buffer_desc){
        .type = SG_BUFFERTYPE_INDEXBUFFER,
        .data = SG_RANGE(quad_indices),
    });
    
    /* Create sampler */
    c->comp_sampler = sg_make_sampler(&(sg_sampler_desc){
        .min_filter = SG_FILTER_LINEAR,
        .mag_filter = SG_FILTER_LINEAR,
    });
    
    /* Layer pass action (clear to transparent) */
    c->layer_pass_action = (sg_pass_action){
        .colors[0] = {
            .load_action = SG_LOADACTION_CLEAR,
            .clear_value = { 0.0f, 0.0f, 0.0f, 0.0f },
        },
    };
    
    return c;
}

void minirend_compositor_destroy(MinirendCompositor *c) {
    if (!c) return;
    
    /* Destroy all layers */
    for (int i = 0; i < c->layer_count; i++) {
        if (c->layers[i]) {
            minirend_compositor_destroy_layer(c, c->layers[i]);
        }
    }
    
    sg_destroy_buffer(c->quad_vbuf);
    sg_destroy_buffer(c->quad_ibuf);
    sg_destroy_pipeline(c->comp_pipeline);
    sg_destroy_shader(c->comp_shader);
    sg_destroy_sampler(c->comp_sampler);
    
    free(c);
}

/* ============================================================================
 * Frame Management
 * ============================================================================ */

void minirend_compositor_begin(MinirendCompositor *c,
                               float viewport_width, float viewport_height) {
    if (!c) return;
    
    c->viewport_width = viewport_width;
    c->viewport_height = viewport_height;
    c->layer_stack_depth = 0;
}

void minirend_compositor_end(MinirendCompositor *c) {
    if (!c) return;
    
    /* Ensure we're back at the main framebuffer */
    while (c->layer_stack_depth > 0) {
        minirend_compositor_end_layer(c);
    }
}

/* ============================================================================
 * Layer Management
 * ============================================================================ */

MinirendLayer *minirend_compositor_create_layer(MinirendCompositor *c,
                                                float width, float height) {
    if (!c || c->layer_count >= MAX_LAYERS) return NULL;
    
    MinirendLayer *layer = calloc(1, sizeof(MinirendLayer));
    if (!layer) return NULL;
    
    layer->width = width;
    layer->height = height;
    layer->opacity = 1.0f;
    layer->transform = minirend_transform_identity();
    
    int iwidth = (int)width;
    int iheight = (int)height;
    
    /* Create render target texture */
    layer->texture = sg_make_image(&(sg_image_desc){
        .render_target = true,
        .width = iwidth,
        .height = iheight,
        .pixel_format = SG_PIXELFORMAT_RGBA8,
    }).id;
    
    /* Create depth buffer */
    layer->depth_buffer = sg_make_image(&(sg_image_desc){
        .render_target = true,
        .width = iwidth,
        .height = iheight,
        .pixel_format = SG_PIXELFORMAT_DEPTH,
    }).id;
    
    /* Create framebuffer attachments */
    sg_attachments_desc att_desc = {
        .colors[0].image = (sg_image){ layer->texture },
        .depth_stencil.image = (sg_image){ layer->depth_buffer },
    };
    layer->framebuffer = sg_make_attachments(&att_desc).id;
    
    /* Add to layer list */
    c->layers[c->layer_count++] = layer;
    
    return layer;
}

void minirend_compositor_destroy_layer(MinirendCompositor *c,
                                       MinirendLayer *layer) {
    if (!c || !layer) return;
    
    /* Remove from layer list */
    for (int i = 0; i < c->layer_count; i++) {
        if (c->layers[i] == layer) {
            c->layers[i] = c->layers[--c->layer_count];
            break;
        }
    }
    
    /* Destroy resources */
    sg_destroy_attachments((sg_attachments){ layer->framebuffer });
    sg_destroy_image((sg_image){ layer->texture });
    sg_destroy_image((sg_image){ layer->depth_buffer });
    
    free(layer);
}

void minirend_compositor_begin_layer(MinirendCompositor *c,
                                     MinirendLayer *layer) {
    if (!c || !layer) return;
    if (c->layer_stack_depth >= 16) return;
    
    c->layer_stack[c->layer_stack_depth++] = layer;
    
    /* Begin pass to layer framebuffer */
    sg_pass pass = {
        .action = c->layer_pass_action,
        .attachments = (sg_attachments){ layer->framebuffer },
    };
    sg_begin_pass(&pass);
}

void minirend_compositor_end_layer(MinirendCompositor *c) {
    if (!c || c->layer_stack_depth == 0) return;
    
    sg_end_pass();
    c->layer_stack_depth--;
}

void minirend_compositor_draw_layer(MinirendCompositor *c,
                                    MinirendLayer *layer,
                                    float x, float y) {
    if (!c || !layer) return;
    
    /* Build MVP matrix */
    float sx = 2.0f / c->viewport_width;
    float sy = -2.0f / c->viewport_height;
    float tx = -1.0f + x * sx;
    float ty = 1.0f + y * sy;
    
    float w = layer->width * sx;
    float h = layer->height * sy;
    
    /* Simple orthographic projection with position */
    float mvp[16] = {
        w, 0, 0, 0,
        0, h, 0, 0,
        0, 0, 1, 0,
        tx, ty, 0, 1,
    };
    
    /* Apply pipeline */
    sg_apply_pipeline(c->comp_pipeline);
    
    /* Apply bindings */
    sg_bindings bindings = {
        .vertex_buffers[0] = c->quad_vbuf,
        .index_buffer = c->quad_ibuf,
        .fs = {
            .images[0] = (sg_image){ layer->texture },
            .samplers[0] = c->comp_sampler,
        },
    };
    sg_apply_bindings(&bindings);
    
    /* Apply uniforms */
    sg_apply_uniforms(SG_SHADERSTAGE_VS, 0, &SG_RANGE(mvp));
    sg_apply_uniforms(SG_SHADERSTAGE_FS, 0, &SG_RANGE(layer->opacity));
    
    /* Draw */
    sg_draw(0, 6, 1);
}

bool minirend_compositor_needs_layer(const MinirendComputedStyle *style) {
    if (!style) return false;
    
    /* Elements that need their own compositing layer:
     * - Has CSS transform
     * - Has opacity < 1
     * - Has position: fixed
     * - Has will-change (not implemented yet)
     */
    
    if (style->has_transform) return true;
    if (style->opacity < 1.0f - 1e-6f) return true;
    if (style->position == MINIREND_POSITION_FIXED) return true;
    
    return false;
}

