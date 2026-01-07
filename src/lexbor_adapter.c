#include "lexbor_adapter.h"

#include <stdio.h>
#include <string.h>

/* Lexbor headers */
#include <lexbor/html/html.h>
#include <lexbor/dom/dom.h>
#include <lexbor/css/css.h>
#include <lexbor/selectors/selectors.h>

/* Document wrapper */
struct LexborDocument {
    lxb_html_document_t *html_doc;
    lxb_css_parser_t    *css_parser;     /* Reusable CSS parser */
    lxb_selectors_t     *selectors;      /* Reusable selectors engine */
};

void minirend_lexbor_adapter_init(void) {
    /* Currently no global state needed.
     * Future: could pre-warm caches or set memory hooks. */
}

void minirend_lexbor_adapter_shutdown(void) {
    /* Currently no global state to clean up. */
}

LexborDocument *minirend_lexbor_parse_html(const char *html, size_t len) {
    if (!html) return NULL;
    if (len == 0) len = strlen(html);

    LexborDocument *doc = calloc(1, sizeof(LexborDocument));
    if (!doc) return NULL;

    /* Create and parse HTML document */
    doc->html_doc = lxb_html_document_create();
    if (!doc->html_doc) {
        free(doc);
        return NULL;
    }

    lxb_status_t status = lxb_html_document_parse(doc->html_doc,
                                                   (const lxb_char_t *)html,
                                                   len);
    if (status != LXB_STATUS_OK) {
        lxb_html_document_destroy(doc->html_doc);
        free(doc);
        return NULL;
    }

    /* Create reusable CSS parser */
    doc->css_parser = lxb_css_parser_create();
    if (doc->css_parser) {
        lxb_css_parser_init(doc->css_parser, NULL);
    }

    /* Create reusable selectors engine */
    doc->selectors = lxb_selectors_create();
    if (doc->selectors) {
        lxb_selectors_init(doc->selectors);
    }

    return doc;
}

void minirend_lexbor_document_destroy(LexborDocument *doc) {
    if (!doc) return;

    if (doc->selectors) {
        lxb_selectors_destroy(doc->selectors, true);
    }
    if (doc->css_parser) {
        lxb_css_parser_destroy(doc->css_parser, true);
    }
    if (doc->html_doc) {
        lxb_html_document_destroy(doc->html_doc);
    }
    free(doc);
}

lxb_html_document_t *minirend_lexbor_get_lxb_document(LexborDocument *doc) {
    return doc ? doc->html_doc : NULL;
}

lxb_css_parser_t *minirend_lexbor_get_css_parser(LexborDocument *doc) {
    return doc ? doc->css_parser : NULL;
}

lxb_selectors_t *minirend_lexbor_get_selectors(LexborDocument *doc) {
    return doc ? doc->selectors : NULL;
}

lxb_dom_node_t *minirend_lexbor_get_body(LexborDocument *doc) {
    if (!doc || !doc->html_doc) return NULL;
    return lxb_dom_interface_node(lxb_html_document_body_element(doc->html_doc));
}

/* Callback context for selector queries */
typedef struct {
    minirend_lexbor_node_cb user_cb;
    void                    *user_ctx;
    lxb_dom_node_t          *first_match;
    size_t                   count;
    bool                     stop_after_first;
} SelectorCtx;

static lxb_status_t selector_cb(lxb_dom_node_t *node,
                                 lxb_css_selector_specificity_t spec,
                                 void *ctx) {
    (void)spec;
    SelectorCtx *sctx = ctx;
    sctx->count++;

    if (sctx->stop_after_first) {
        sctx->first_match = node;
        return LXB_STATUS_STOP;
    }

    if (sctx->user_cb) {
        if (!sctx->user_cb(node, sctx->user_ctx)) {
            return LXB_STATUS_STOP;
        }
    }

    return LXB_STATUS_OK;
}

lxb_dom_node_t *minirend_lexbor_query_selector(LexborDocument *doc,
                                                lxb_dom_node_t *root,
                                                const char *selector) {
    if (!doc || !doc->css_parser || !doc->selectors || !selector) return NULL;

    /* Default to document element if no root specified */
    if (!root && doc->html_doc) {
        lxb_dom_document_t *dom_doc = lxb_dom_interface_document(doc->html_doc);
        root = lxb_dom_interface_node(lxb_dom_document_element(dom_doc));
    }
    if (!root) return NULL;

    /* Parse selector */
    lxb_css_selector_list_t *list = lxb_css_selectors_parse(
        doc->css_parser,
        (const lxb_char_t *)selector,
        strlen(selector));
    if (!list) return NULL;

    /* Find first match */
    SelectorCtx ctx = {
        .user_cb = NULL,
        .user_ctx = NULL,
        .first_match = NULL,
        .count = 0,
        .stop_after_first = true
    };

    lxb_selectors_opt_set(doc->selectors, LXB_SELECTORS_OPT_MATCH_FIRST);
    lxb_selectors_find(doc->selectors, root, list, selector_cb, &ctx);

    lxb_css_selector_list_destroy_memory(list);

    return ctx.first_match;
}

size_t minirend_lexbor_query_selector_all(LexborDocument *doc,
                                          lxb_dom_node_t *root,
                                          const char *selector,
                                          minirend_lexbor_node_cb cb,
                                          void *user_ctx) {
    if (!doc || !doc->css_parser || !doc->selectors || !selector) return 0;

    /* Default to document element if no root specified */
    if (!root && doc->html_doc) {
        lxb_dom_document_t *dom_doc = lxb_dom_interface_document(doc->html_doc);
        root = lxb_dom_interface_node(lxb_dom_document_element(dom_doc));
    }
    if (!root) return 0;

    /* Parse selector */
    lxb_css_selector_list_t *list = lxb_css_selectors_parse(
        doc->css_parser,
        (const lxb_char_t *)selector,
        strlen(selector));
    if (!list) return 0;

    /* Find all matches */
    SelectorCtx ctx = {
        .user_cb = cb,
        .user_ctx = user_ctx,
        .first_match = NULL,
        .count = 0,
        .stop_after_first = false
    };

    lxb_selectors_opt_set(doc->selectors, LXB_SELECTORS_OPT_MATCH_FIRST);
    lxb_selectors_find(doc->selectors, root, list, selector_cb, &ctx);

    lxb_css_selector_list_destroy_memory(list);

    return ctx.count;
}

const char *minirend_lexbor_get_inline_style(lxb_dom_node_t *element) {
    if (!element) return NULL;

    /* Get style attribute */
    lxb_dom_element_t *el = lxb_dom_interface_element(element);
    if (!el) return NULL;

    size_t len = 0;
    const lxb_char_t *style = lxb_dom_element_get_attribute(
        el,
        (const lxb_char_t *)"style", 5,
        &len);

    return (const char *)style;
}

const char *minirend_lexbor_get_tag_name(lxb_dom_node_t *element) {
    if (!element) return NULL;

    lxb_dom_element_t *el = lxb_dom_interface_element(element);
    if (!el) return NULL;

    size_t len = 0;
    const lxb_char_t *name = lxb_dom_element_qualified_name(el, &len);
    return (const char *)name;
}

const char *minirend_lexbor_get_attribute(lxb_dom_node_t *element,
                                          const char *name) {
    if (!element || !name) return NULL;

    lxb_dom_element_t *el = lxb_dom_interface_element(element);
    if (!el) return NULL;

    size_t len = 0;
    const lxb_char_t *val = lxb_dom_element_get_attribute(
        el,
        (const lxb_char_t *)name, strlen(name),
        &len);

    return (const char *)val;
}


