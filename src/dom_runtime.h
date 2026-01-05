#ifndef MINIREND_DOM_RUNTIME_H
#define MINIREND_DOM_RUNTIME_H

#include <stdint.h>
#include <stdbool.h>

#include "quickjs.h"

/* DOM runtime helpers:
 * - installs a tiny JS EventTarget/Event runtime
 * - keeps a node_id -> JS object registry
 * - helps C dispatch DOM-ish events into JS
 */

void   minirend_dom_runtime_init(JSContext *ctx);
void   minirend_dom_runtime_shutdown(JSContext *ctx);

void   minirend_dom_register_node(JSContext *ctx, int32_t node_id, JSValue obj);
JSValue minirend_dom_lookup_node(JSContext *ctx, int32_t node_id);

/* Updates document.activeElement (does not itself dispatch focus/blur). */
void   minirend_dom_set_active_element(JSContext *ctx, int32_t node_id);

/* Dispatch `eventObj` on the target element (bubbling/capture is handled in JS).
 * Returns true if NOT defaultPrevented.
 */
bool   minirend_dom_dispatch_event(JSContext *ctx, int32_t target_node_id, JSValue eventObj);

#endif /* MINIREND_DOM_RUNTIME_H */


