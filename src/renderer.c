#include <stdio.h>

#include "minirend.h"

/* Placeholder renderer that will eventually integrate Lexbor layout.
 * For now, it clears the background and can be extended to draw
 * simple UI elements. WebGL canvases render directly via OpenGL.
 */

void
minirend_renderer_init(MinirendApp *app) {
    (void)app;
}

void
minirend_renderer_load_html(MinirendApp *app, const char *path) {
    (void)app;
    (void)path;
    /* TODO: use Lexbor to parse HTML and the layout engine to compute
     * positions, then draw into an offscreen surface / texture which
     * can be composited in draw().
     */
}

void
minirend_renderer_draw(MinirendApp *app) {
    (void)app;
    /* For now, nothing beyond the clear in main.c. WebGL content is drawn
     * by three.js using the WebGL bindings directly.
     */
}


