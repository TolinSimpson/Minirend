#ifndef MINIREND_LEXBOR_ADAPTER_H
#define MINIREND_LEXBOR_ADAPTER_H

/* Lexbor HTML/CSS parsing adapter.
 *
 * This module provides the interface between minirend and Lexbor for:
 * - HTML document parsing
 * - CSS stylesheet parsing
 * - DOM tree construction
 * - CSS selector queries
 * - Style collection (inline + cascaded)
 *
 * The adapter will feed into the layout engine once implemented.
 */

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* Forward declarations */
typedef struct JSContext JSContext;
typedef struct lxb_html_document lxb_html_document_t;
typedef struct lxb_dom_node lxb_dom_node_t;
typedef struct lxb_css_parser lxb_css_parser_t;
typedef struct lxb_selectors lxb_selectors_t;

/* Opaque handle for parsed documents */
typedef struct LexborDocument LexborDocument;

/* Initialize Lexbor adapter (call once at startup). */
void minirend_lexbor_adapter_init(void);

/* Shutdown Lexbor adapter (call once at shutdown). */
void minirend_lexbor_adapter_shutdown(void);

/* Parse an HTML string into a Lexbor document.
 * Returns NULL on failure. Caller must free with lexbor_document_destroy(). */
LexborDocument *minirend_lexbor_parse_html(const char *html, size_t len);

/* Destroy a parsed document and free resources. */
void minirend_lexbor_document_destroy(LexborDocument *doc);

/* Get the underlying Lexbor HTML document (for direct API access). */
lxb_html_document_t *minirend_lexbor_get_lxb_document(LexborDocument *doc);

/* Get reusable CSS parser / selectors engine instances associated with doc.
 * Returned pointers are owned by LexborDocument; do not destroy them. */
lxb_css_parser_t *minirend_lexbor_get_css_parser(LexborDocument *doc);
lxb_selectors_t  *minirend_lexbor_get_selectors(LexborDocument *doc);

/* Get the document's body element, or NULL if not present. */
lxb_dom_node_t *minirend_lexbor_get_body(LexborDocument *doc);

/* Query selector: find first matching element.
 * Returns NULL if not found or on error. */
lxb_dom_node_t *minirend_lexbor_query_selector(LexborDocument *doc,
                                                lxb_dom_node_t *root,
                                                const char *selector);

/* Query selector all: find all matching elements.
 * Callback is invoked for each match. Return false from callback to stop.
 * Returns number of matches found. */
typedef bool (*minirend_lexbor_node_cb)(lxb_dom_node_t *node, void *ctx);
size_t minirend_lexbor_query_selector_all(LexborDocument *doc,
                                          lxb_dom_node_t *root,
                                          const char *selector,
                                          minirend_lexbor_node_cb cb,
                                          void *ctx);

/* Collect inline style from an element's style attribute.
 * Returns the style string or NULL if none. Caller must NOT free. */
const char *minirend_lexbor_get_inline_style(lxb_dom_node_t *element);

/* Get element tag name (e.g., "DIV", "SPAN"). Returns NULL on error.
 * Caller must NOT free the returned string. */
const char *minirend_lexbor_get_tag_name(lxb_dom_node_t *element);

/* Get element attribute value. Returns NULL if not found.
 * Caller must NOT free the returned string. */
const char *minirend_lexbor_get_attribute(lxb_dom_node_t *element,
                                          const char *name);

#endif /* MINIREND_LEXBOR_ADAPTER_H */


