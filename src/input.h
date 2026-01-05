#ifndef MINIREND_INPUT_H
#define MINIREND_INPUT_H

#include "quickjs.h"
#include "sokol_app.h"

/* Input/event queue and DOM dispatch bridge.
 *
 * The Sokol event callback enqueues raw input events, then the main loop
 * calls minirend_input_tick() once per frame to translate them into
 * DOM-ish events dispatched into JS.
 */

void minirend_input_init(JSContext *ctx);
void minirend_input_shutdown(JSContext *ctx);

void minirend_input_push_sapp_event(const sapp_event *ev);
void minirend_input_tick(JSContext *ctx);

#endif /* MINIREND_INPUT_H */


