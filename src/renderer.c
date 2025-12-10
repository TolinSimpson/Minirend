#include <stdio.h>

#include <SDL_opengl.h>

#include "minrend.h"

/* Placeholder renderer that will eventually integrate Modest.
 * For now, it clears the background and can be extended to draw
 * simple UI elements. WebGL canvases render directly via OpenGL.
 */

void
minrend_renderer_init(MinrendApp *app) {
    (void)app;
}

void
minrend_renderer_load_html(MinrendApp *app, const char *path) {
    (void)app;
    (void)path;
    /* TODO: use Modest to parse and lay out HTML, then draw into an
     * offscreen surface / texture which can be composited in draw().
     */
}

void
minrend_renderer_draw(MinrendApp *app) {
    (void)app;
    /* For now, nothing beyond the clear in main.c. WebGL content is drawn
     * by three.js using the WebGL bindings directly.
     */
}


