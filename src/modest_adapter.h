#ifndef MINIREND_MODEST_ADAPTER_H
#define MINIREND_MODEST_ADAPTER_H

/* Modest integration adapter.
 *
 * For now this is a placeholder: Modest is disabled in the current build,
 * but we keep the adapter boundary so input/hit-test can be wired later.
 */

typedef struct JSContext JSContext;

void minirend_modest_adapter_init(void);
void minirend_modest_adapter_shutdown(void);

/* Rebuild layout tree from HTML/DOM and update UI bounds for hit-testing. */
void minirend_modest_adapter_rebuild_layout(JSContext *ctx);

#endif /* MINIREND_MODEST_ADAPTER_H */


