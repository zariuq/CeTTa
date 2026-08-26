#include "certificate_gslt_plan_v1.h"

#include "finite_horn_answer_stream_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    FHAnswerStreamV1 answers;
    void **allocations;
    size_t allocation_len;
    size_t allocation_cap;
} PPCertificateGSLTPlanStorageV1;

typedef struct {
    PPCertificateGSLTNameV1 article;
    PPCertificateGSLTNameV1 name;
    const Atom *source;
    PPCertificateGSLTPatternV1 resolved;
    uint8_t resolution_state;
    bool used;
} PPCertificateGSLTPlanTermBindingV1;

typedef struct {
    PPCertificateGSLTNameV1 article;
    PPCertificateGSLTNameV1 name;
    PPCertificateGSLTNameV1 rule;
    const Atom *arguments;
    const Atom *references;
    bool used;
} PPCertificateGSLTPlanNodeBindingV1;

typedef struct {
    PPCertificateGSLTNameV1 article;
    const Atom *order;
    bool used;
} PPCertificateGSLTPlanOrderBindingV1;

typedef struct {
    PPCertificateGSLTNameV1 name;
    uint32_t index;
} PPCertificateGSLTPlanNodeIndexV1;

typedef struct {
    PPCertificateGSLTPlanTermBindingV1 *terms;
    uint32_t term_len;
    PPCertificateGSLTNameV1 article;
} PPCertificateGSLTPlanResolutionV1;

typedef struct {
    PPCertificateGSLTPlanStorageV1 *storage;
    const PPCertificateGSLTArticleV1Limits *limits;
    uint32_t remaining_patterns;
    PPCertificateGSLTArticleV1Result result;
    char *error_buf;
    size_t error_buf_size;
} PPCertificateGSLTPlanParseV1;

typedef enum {
    PPCERTIFICATE_GSLT_PLAN_RECORD_EXTENSION = 0,
    PPCERTIFICATE_GSLT_PLAN_RECORD_CONSTRUCTOR = 1,
    PPCERTIFICATE_GSLT_PLAN_RECORD_CALCULUS_CONSTRUCTOR = 2,
    PPCERTIFICATE_GSLT_PLAN_RECORD_JUDGMENT = 3,
    PPCERTIFICATE_GSLT_PLAN_RECORD_CAPABILITY = 4,
    PPCERTIFICATE_GSLT_PLAN_RECORD_CONVERSION = 5,
    PPCERTIFICATE_GSLT_PLAN_RECORD_RULE = 6,
    PPCERTIFICATE_GSLT_PLAN_RECORD_ARTICLE = 7,
    PPCERTIFICATE_GSLT_PLAN_RECORD_TERM_BINDING = 8,
    PPCERTIFICATE_GSLT_PLAN_RECORD_NODE_BINDING = 9,
    PPCERTIFICATE_GSLT_PLAN_RECORD_ORDER_BINDING = 10,
    PPCERTIFICATE_GSLT_PLAN_RECORD_DOCUMENT = 11,
    PPCERTIFICATE_GSLT_PLAN_RECORD_UNKNOWN = 12
} PPCertificateGSLTPlanRecordKindV1;

typedef struct {
    uint32_t extension_len;
    uint32_t constructor_len;
    uint32_t calculus_constructor_len;
    uint32_t judgment_len;
    uint32_t capability_len;
    uint32_t conversion_len;
    uint32_t rule_len;
    uint32_t article_len;
    uint32_t term_binding_len;
    uint32_t node_binding_len;
    uint32_t order_binding_len;
    uint32_t document_len;
} PPCertificateGSLTPlanRecordCountsV1;

static void ppproof_plan_v1_set_error(char *buf, size_t size,
                                      const char *format, ...) {
    va_list arguments;

    if (!buf || size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(buf, size, format, arguments);
    va_end(arguments);
}

static bool ppproof_plan_v1_array_fits(size_t count,
                                       size_t element_size) {
    return element_size == 0u || count <= SIZE_MAX / element_size;
}

static bool ppproof_plan_v1_expr_head(const Atom *atom,
                                      const char *head,
                                      CettaExprLen argument_len) {
    return atom && atom->kind == ATOM_EXPR &&
           atom->expr.len == argument_len + 1u &&
           atom_is_symbol(atom->expr.elems[0], head);
}

static void ppproof_plan_v1_fail(PPCertificateGSLTPlanParseV1 *parse,
                                 PPCertificateGSLTArticleV1Result result,
                                 const char *message) {
    if (parse->result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK) {
        parse->result = result;
        ppproof_plan_v1_set_error(
            parse->error_buf, parse->error_buf_size, "%s", message);
    }
}

static void *ppproof_plan_v1_alloc(PPCertificateGSLTPlanParseV1 *parse,
                                   size_t count, size_t item_size) {
    PPCertificateGSLTPlanStorageV1 *storage = parse->storage;
    void **next;
    void *allocation;
    size_t next_cap;

    if (count == 0u)
        return NULL;
    if (!ppproof_plan_v1_array_fits(count, item_size)) {
        ppproof_plan_v1_fail(
            parse, PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE,
            "compiled proof artifact allocation overflows");
        return NULL;
    }
    allocation = calloc(count, item_size);
    if (!allocation) {
        ppproof_plan_v1_fail(
            parse, PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE,
            "compiled proof artifact allocation failed");
        return NULL;
    }
    if (storage->allocation_len == storage->allocation_cap) {
        next_cap = storage->allocation_cap
                       ? storage->allocation_cap * 2u
                       : 32u;
        if (next_cap < storage->allocation_cap ||
            !ppproof_plan_v1_array_fits(next_cap, sizeof(*next))) {
            free(allocation);
            ppproof_plan_v1_fail(
                parse, PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE,
                "compiled proof artifact allocation registry overflows");
            return NULL;
        }
        next = realloc(storage->allocations, next_cap * sizeof(*next));
        if (!next) {
            free(allocation);
            ppproof_plan_v1_fail(
                parse, PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE,
                "compiled proof artifact allocation registry failed");
            return NULL;
        }
        storage->allocations = next;
        storage->allocation_cap = next_cap;
    }
    storage->allocations[storage->allocation_len++] = allocation;
    return allocation;
}

static void ppproof_plan_v1_storage_free(
    PPCertificateGSLTPlanStorageV1 *storage) {
    size_t index;

    if (!storage)
        return;
    for (index = 0u; index < storage->allocation_len; index++)
        free(storage->allocations[index]);
    free(storage->allocations);
    fh_answer_stream_v1_free(&storage->answers);
    free(storage);
}

void ppcertificate_gslt_plan_v1_init(PPCertificateGSLTPlanV1 *plan) {
    if (plan)
        memset(plan, 0, sizeof(*plan));
}

void ppcertificate_gslt_plan_v1_free(PPCertificateGSLTPlanV1 *plan) {
    if (!plan)
        return;
    ppproof_plan_v1_storage_free(plan->storage);
    memset(plan, 0, sizeof(*plan));
}

static bool ppproof_plan_v1_name(const Atom *atom,
                                 PPCertificateGSLTNameV1 *out) {
    const char *name;
    size_t len;

    if (!atom || atom->kind != ATOM_SYMBOL)
        return false;
    name = atom_name_cstr((Atom *)atom);
    if (!name || name[0] == '\0')
        return false;
    len = strlen(name);
    if (len > UINT32_MAX)
        return false;
    *out = (PPCertificateGSLTNameV1){
        .bytes = (const uint8_t *)name,
        .len = (uint32_t)len,
    };
    return true;
}

static bool ppproof_plan_v1_name_is(PPCertificateGSLTNameV1 name,
                                    const char *text) {
    size_t len = strlen(text);
    return len <= UINT32_MAX && name.len == (uint32_t)len &&
           memcmp(name.bytes, text, len) == 0;
}

static int ppproof_plan_v1_name_compare(PPCertificateGSLTNameV1 left,
                                        PPCertificateGSLTNameV1 right) {
    uint32_t common = left.len < right.len ? left.len : right.len;
    int compared = common == 0u
                       ? 0
                       : memcmp(left.bytes, right.bytes, common);

    if (compared != 0)
        return compared;
    if (left.len < right.len)
        return -1;
    if (left.len > right.len)
        return 1;
    return 0;
}

static int ppproof_plan_v1_term_binding_compare(const void *left_value,
                                                 const void *right_value) {
    const PPCertificateGSLTPlanTermBindingV1 *left = left_value;
    const PPCertificateGSLTPlanTermBindingV1 *right = right_value;
    int compared = ppproof_plan_v1_name_compare(
        left->article, right->article);

    return compared != 0
               ? compared
               : ppproof_plan_v1_name_compare(left->name, right->name);
}

static int ppproof_plan_v1_node_binding_compare(const void *left_value,
                                                 const void *right_value) {
    const PPCertificateGSLTPlanNodeBindingV1 *left = left_value;
    const PPCertificateGSLTPlanNodeBindingV1 *right = right_value;
    int compared = ppproof_plan_v1_name_compare(
        left->article, right->article);

    return compared != 0
               ? compared
               : ppproof_plan_v1_name_compare(left->name, right->name);
}

static int ppproof_plan_v1_order_binding_compare(const void *left_value,
                                                  const void *right_value) {
    const PPCertificateGSLTPlanOrderBindingV1 *left = left_value;
    const PPCertificateGSLTPlanOrderBindingV1 *right = right_value;

    return ppproof_plan_v1_name_compare(left->article, right->article);
}

static int ppproof_plan_v1_node_index_compare(const void *left_value,
                                               const void *right_value) {
    const PPCertificateGSLTPlanNodeIndexV1 *left = left_value;
    const PPCertificateGSLTPlanNodeIndexV1 *right = right_value;

    return ppproof_plan_v1_name_compare(left->name, right->name);
}

static PPCertificateGSLTPlanTermBindingV1 *ppproof_plan_v1_find_term_binding(
    PPCertificateGSLTPlanResolutionV1 *resolution,
    PPCertificateGSLTNameV1 name) {
    uint32_t low = 0u;
    uint32_t high = resolution->term_len;

    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        PPCertificateGSLTPlanTermBindingV1 *candidate =
            &resolution->terms[middle];
        int compared = ppproof_plan_v1_name_compare(
            resolution->article, candidate->article);
        if (compared == 0)
            compared = ppproof_plan_v1_name_compare(
                name, candidate->name);
        if (compared < 0)
            high = middle;
        else if (compared > 0)
            low = middle + 1u;
        else
            return candidate;
    }
    return NULL;
}

static bool ppproof_plan_v1_nat(PPCertificateGSLTPlanParseV1 *parse,
                                const Atom *term,
                                uint32_t maximum,
                                uint32_t *out) {
    uint32_t value = 0u;

    while (!atom_is_symbol((Atom *)term, "proof-zero-v1")) {
        if (!ppproof_plan_v1_expr_head(term, "proof-succ-v1", 1u)) {
            ppproof_plan_v1_fail(
                parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
                "compiled proof artifact contains a malformed natural");
            return false;
        }
        if (value == maximum) {
            ppproof_plan_v1_fail(
                parse, PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE,
                "compiled proof artifact natural exceeds its limit");
            return false;
        }
        value++;
        term = term->expr.elems[1];
    }
    *out = value;
    return true;
}

static bool ppproof_plan_v1_list_count(
    PPCertificateGSLTPlanParseV1 *parse,
    const Atom *term,
    uint32_t maximum,
    uint32_t *out) {
    uint32_t count = 0u;

    while (!atom_is_symbol((Atom *)term, "proof-list-nil-v1")) {
        if (!ppproof_plan_v1_expr_head(
                term, "proof-list-cons-v1", 2u)) {
            ppproof_plan_v1_fail(
                parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
                "compiled proof artifact contains a malformed list");
            return false;
        }
        if (count == maximum) {
            ppproof_plan_v1_fail(
                parse, PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE,
                "compiled proof artifact list exceeds its limit");
            return false;
        }
        count++;
        term = term->expr.elems[2];
    }
    *out = count;
    return true;
}

static bool ppproof_plan_v1_pattern_resolved(
    PPCertificateGSLTPlanParseV1 *parse,
    const Atom *term,
    uint32_t traversal_depth,
    PPCertificateGSLTPlanResolutionV1 *resolution,
    PPCertificateGSLTPatternV1 *out);

static bool ppproof_plan_v1_pattern_list_resolved(
    PPCertificateGSLTPlanParseV1 *parse,
    const Atom *term,
    uint32_t traversal_depth,
    PPCertificateGSLTPlanResolutionV1 *resolution,
    PPCertificateGSLTPatternV1 **patterns_out,
    uint32_t *pattern_len_out) {
    PPCertificateGSLTPatternV1 *patterns = NULL;
    const Atom *cursor = term;
    uint32_t pattern_len;
    uint32_t index;

    if (!ppproof_plan_v1_list_count(
            parse, term, parse->remaining_patterns, &pattern_len))
        return false;
    if (pattern_len != 0u) {
        patterns = ppproof_plan_v1_alloc(
            parse, pattern_len, sizeof(*patterns));
        if (!patterns)
            return false;
    }
    for (index = 0u; index < pattern_len; index++) {
        if (!ppproof_plan_v1_pattern_resolved(
                parse, cursor->expr.elems[1], traversal_depth,
                resolution, &patterns[index]))
            return false;
        cursor = cursor->expr.elems[2];
    }
    *patterns_out = patterns;
    *pattern_len_out = pattern_len;
    return true;
}

static bool ppproof_plan_v1_pattern_resolved(
    PPCertificateGSLTPlanParseV1 *parse,
    const Atom *term,
    uint32_t traversal_depth,
    PPCertificateGSLTPlanResolutionV1 *resolution,
    PPCertificateGSLTPatternV1 *out) {
    PPCertificateGSLTPatternV1 *children;
    uint32_t child_len;
    uint32_t next_depth;

    if (traversal_depth > parse->limits->maximum_pattern_depth ||
        parse->remaining_patterns == 0u) {
        ppproof_plan_v1_fail(
            parse, PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE,
            "compiled proof artifact pattern exceeds its limit");
        return false;
    }
    parse->remaining_patterns--;
    memset(out, 0, sizeof(*out));

    if (ppproof_plan_v1_expr_head(
            term, "source-proof-term-ref-v1", 1u)) {
        PPCertificateGSLTNameV1 name;
        PPCertificateGSLTPlanTermBindingV1 *binding;

        if (!resolution ||
            !ppproof_plan_v1_name(term->expr.elems[1], &name) ||
            !(binding = ppproof_plan_v1_find_term_binding(
                  resolution, name))) {
            ppproof_plan_v1_fail(
                parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
                "compiled proof document has an unknown term reference");
            return false;
        }
        binding->used = true;
        if (binding->resolution_state == 1u) {
            ppproof_plan_v1_fail(
                parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
                "compiled proof document has a cyclic term reference");
            return false;
        }
        if (binding->resolution_state == 0u) {
            binding->resolution_state = 1u;
            if (!ppproof_plan_v1_pattern_resolved(
                    parse, binding->source, traversal_depth,
                    resolution, &binding->resolved))
                return false;
            binding->resolution_state = 2u;
        }
        *out = binding->resolved;
        return true;
    }

    if (ppproof_plan_v1_expr_head(term, "proof-bvar-v1", 1u)) {
        out->kind = PPCERTIFICATE_GSLT_PATTERN_V1_BVAR;
        return ppproof_plan_v1_nat(
            parse, term->expr.elems[1], UINT32_MAX, &out->as.bvar);
    }
    if (ppproof_plan_v1_expr_head(term, "proof-fvar-v1", 1u)) {
        out->kind = PPCERTIFICATE_GSLT_PATTERN_V1_FVAR;
        if (!ppproof_plan_v1_name(term->expr.elems[1], &out->as.fvar)) {
            ppproof_plan_v1_fail(
                parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
                "compiled proof artifact has a malformed free variable");
            return false;
        }
        return true;
    }
    if (ppproof_plan_v1_expr_head(term, "proof-apply-v1", 2u)) {
        out->kind = PPCERTIFICATE_GSLT_PATTERN_V1_APPLY;
        if (!ppproof_plan_v1_name(
                term->expr.elems[1], &out->as.apply.constructor)) {
            ppproof_plan_v1_fail(
                parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
                "compiled proof artifact has a malformed application head");
            return false;
        }
        if (traversal_depth == UINT32_MAX) {
            ppproof_plan_v1_fail(
                parse, PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE,
                "compiled proof artifact pattern depth overflows");
            return false;
        }
        next_depth = traversal_depth + 1u;
        if (!ppproof_plan_v1_pattern_list_resolved(
                parse, term->expr.elems[2], next_depth,
                resolution, &children, &child_len))
            return false;
        out->as.apply.arguments = children;
        out->as.apply.argument_len = child_len;
        return true;
    }
    if (ppproof_plan_v1_expr_head(term, "proof-lambda-v1", 1u)) {
        out->kind = PPCERTIFICATE_GSLT_PATTERN_V1_LAMBDA;
        children = ppproof_plan_v1_alloc(parse, 1u, sizeof(*children));
        if (!children)
            return false;
        if (traversal_depth == UINT32_MAX ||
            !ppproof_plan_v1_pattern_resolved(
                parse, term->expr.elems[1], traversal_depth + 1u,
                resolution, children)) {
            if (traversal_depth == UINT32_MAX)
                ppproof_plan_v1_fail(
                    parse, PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE,
                    "compiled proof artifact pattern depth overflows");
            return false;
        }
        out->as.lambda.body = children;
        return true;
    }
    if (ppproof_plan_v1_expr_head(
            term, "proof-multi-lambda-v1", 2u)) {
        out->kind = PPCERTIFICATE_GSLT_PATTERN_V1_MULTI_LAMBDA;
        if (!ppproof_plan_v1_nat(
                parse, term->expr.elems[1], UINT32_MAX,
                &out->as.multi_lambda.arity))
            return false;
        children = ppproof_plan_v1_alloc(parse, 1u, sizeof(*children));
        if (!children)
            return false;
        if (traversal_depth == UINT32_MAX ||
            !ppproof_plan_v1_pattern_resolved(
                parse, term->expr.elems[2], traversal_depth + 1u,
                resolution, children)) {
            if (traversal_depth == UINT32_MAX)
                ppproof_plan_v1_fail(
                    parse, PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE,
                    "compiled proof artifact pattern depth overflows");
            return false;
        }
        out->as.multi_lambda.body = children;
        return true;
    }
    if (ppproof_plan_v1_expr_head(term, "proof-subst-v1", 2u)) {
        out->kind = PPCERTIFICATE_GSLT_PATTERN_V1_SUBST;
        children = ppproof_plan_v1_alloc(parse, 2u, sizeof(*children));
        if (!children)
            return false;
        if (traversal_depth == UINT32_MAX) {
            ppproof_plan_v1_fail(
                parse, PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE,
                "compiled proof artifact pattern depth overflows");
            return false;
        }
        if (!ppproof_plan_v1_pattern_resolved(
                parse, term->expr.elems[1], traversal_depth + 1u,
                resolution, &children[0]) ||
            !ppproof_plan_v1_pattern_resolved(
                parse, term->expr.elems[2], traversal_depth + 1u,
                resolution, &children[1]))
            return false;
        out->as.subst.body = &children[0];
        out->as.subst.replacement = &children[1];
        return true;
    }
    if (ppproof_plan_v1_expr_head(term, "proof-collection-v1", 2u)) {
        PPCertificateGSLTNameV1 kind;

        out->kind = PPCERTIFICATE_GSLT_PATTERN_V1_COLLECTION;
        if (!ppproof_plan_v1_name(term->expr.elems[1], &kind)) {
            ppproof_plan_v1_fail(
                parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
                "compiled proof artifact has a malformed collection kind");
            return false;
        }
        if (ppproof_plan_v1_name_is(kind, "proof-vector-v1"))
            out->as.collection.collection_kind =
                PPCERTIFICATE_GSLT_COLLECTION_V1_VECTOR;
        else if (ppproof_plan_v1_name_is(kind, "proof-hash-bag-v1"))
            out->as.collection.collection_kind =
                PPCERTIFICATE_GSLT_COLLECTION_V1_HASH_BAG;
        else if (ppproof_plan_v1_name_is(kind, "proof-hash-set-v1"))
            out->as.collection.collection_kind =
                PPCERTIFICATE_GSLT_COLLECTION_V1_HASH_SET;
        else {
            ppproof_plan_v1_fail(
                parse, PPCERTIFICATE_GSLT_ARTICLE_V1_UNSUPPORTED,
                "compiled proof artifact requires an unknown collection kind");
            return false;
        }
        if (traversal_depth == UINT32_MAX) {
            ppproof_plan_v1_fail(
                parse, PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE,
                "compiled proof artifact pattern depth overflows");
            return false;
        }
        if (!ppproof_plan_v1_pattern_list_resolved(
                parse, term->expr.elems[2], traversal_depth + 1u,
                resolution, &children, &child_len))
            return false;
        out->as.collection.elements = children;
        out->as.collection.element_len = child_len;
        return true;
    }
    ppproof_plan_v1_fail(
        parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
        "compiled proof artifact contains an unknown pattern form");
    return false;
}

static bool ppproof_plan_v1_pattern(
    PPCertificateGSLTPlanParseV1 *parse,
    const Atom *term,
    uint32_t traversal_depth,
    PPCertificateGSLTPatternV1 *out) {
    return ppproof_plan_v1_pattern_resolved(
        parse, term, traversal_depth, NULL, out);
}

static bool ppproof_plan_v1_pattern_list(
    PPCertificateGSLTPlanParseV1 *parse,
    const Atom *term,
    uint32_t traversal_depth,
    PPCertificateGSLTPatternV1 **patterns_out,
    uint32_t *pattern_len_out) {
    return ppproof_plan_v1_pattern_list_resolved(
        parse, term, traversal_depth, NULL,
        patterns_out, pattern_len_out);
}

static bool ppproof_plan_v1_formals(
    PPCertificateGSLTPlanParseV1 *parse,
    const Atom *term,
    PPCertificateGSLTFormalV1 **formals_out,
    uint32_t *formal_len_out) {
    PPCertificateGSLTFormalV1 *formals = NULL;
    const Atom *cursor = term;
    uint32_t formal_len;
    uint32_t index;

    if (!ppproof_plan_v1_list_count(
            parse, term,
            parse->limits->maximum_presentation_pattern_nodes,
            &formal_len))
        return false;
    if (formal_len != 0u) {
        formals = ppproof_plan_v1_alloc(
            parse, formal_len, sizeof(*formals));
        if (!formals)
            return false;
    }
    for (index = 0u; index < formal_len; index++) {
        const Atom *formal = cursor->expr.elems[1];
        if (!ppproof_plan_v1_expr_head(
                formal, "proof-formal-v1", 2u) ||
            !ppproof_plan_v1_name(
                formal->expr.elems[1], &formals[index].name) ||
            !ppproof_plan_v1_nat(
                parse, formal->expr.elems[2], UINT32_MAX,
                &formals[index].depth)) {
            ppproof_plan_v1_fail(
                parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
                "compiled proof artifact contains a malformed formal");
            return false;
        }
        cursor = cursor->expr.elems[2];
    }
    *formals_out = formals;
    *formal_len_out = formal_len;
    return true;
}

static bool ppproof_plan_v1_conditions(
    PPCertificateGSLTPlanParseV1 *parse,
    const Atom *term,
    PPCertificateGSLTSideConditionV1 **conditions_out,
    uint32_t *condition_len_out) {
    PPCertificateGSLTSideConditionV1 *conditions = NULL;
    const Atom *cursor = term;
    uint32_t condition_len;
    uint32_t index;

    if (!ppproof_plan_v1_list_count(
            parse, term,
            parse->limits->maximum_presentation_pattern_nodes,
            &condition_len))
        return false;
    if (condition_len != 0u) {
        conditions = ppproof_plan_v1_alloc(
            parse, condition_len, sizeof(*conditions));
        if (!conditions)
            return false;
    }
    for (index = 0u; index < condition_len; index++) {
        const Atom *condition = cursor->expr.elems[1];
        if (ppproof_plan_v1_expr_head(
                condition,
                "proof-explicit-substitution-indexed-v1", 4u)) {
            conditions[index].kind =
                PPCERTIFICATE_GSLT_SIDE_CONDITION_V1_EXPLICIT_SUBSTITUTION;
            if (!ppproof_plan_v1_nat(
                    parse, condition->expr.elems[1], UINT32_MAX,
                    &conditions[index].ambient_depth) ||
                !ppproof_plan_v1_nat(
                    parse, condition->expr.elems[2], UINT32_MAX,
                    &conditions[index].body_argument) ||
                !ppproof_plan_v1_nat(
                    parse, condition->expr.elems[3], UINT32_MAX,
                    &conditions[index].replacement_argument) ||
                !ppproof_plan_v1_nat(
                    parse, condition->expr.elems[4], UINT32_MAX,
                    &conditions[index].result_argument))
                return false;
        } else if (ppproof_plan_v1_expr_head(
                       condition,
                       "proof-unused-binder-indexed-v1", 3u)) {
            conditions[index].kind =
                PPCERTIFICATE_GSLT_SIDE_CONDITION_V1_UNUSED_BINDER_ELIMINATION;
            if (!ppproof_plan_v1_nat(
                    parse, condition->expr.elems[1], UINT32_MAX,
                    &conditions[index].ambient_depth) ||
                !ppproof_plan_v1_nat(
                    parse, condition->expr.elems[2], UINT32_MAX,
                    &conditions[index].body_argument) ||
                !ppproof_plan_v1_nat(
                    parse, condition->expr.elems[3], UINT32_MAX,
                    &conditions[index].result_argument))
                return false;
        } else {
            ppproof_plan_v1_fail(
                parse, PPCERTIFICATE_GSLT_ARTICLE_V1_UNSUPPORTED,
                "compiled proof artifact contains an unknown side condition");
            return false;
        }
        cursor = cursor->expr.elems[2];
    }
    *conditions_out = conditions;
    *condition_len_out = condition_len;
    return true;
}

static bool ppproof_plan_v1_references(
    PPCertificateGSLTPlanParseV1 *parse,
    const Atom *term,
    PPCertificateGSLTReferenceV1 **references_out,
    uint32_t *reference_len_out) {
    PPCertificateGSLTReferenceV1 *references = NULL;
    const Atom *cursor = term;
    uint32_t reference_len;
    uint32_t index;

    if (!ppproof_plan_v1_list_count(
            parse, term, parse->limits->maximum_article_nodes,
            &reference_len))
        return false;
    if (reference_len != 0u) {
        references = ppproof_plan_v1_alloc(
            parse, reference_len, sizeof(*references));
        if (!references)
            return false;
    }
    for (index = 0u; index < reference_len; index++) {
        const Atom *reference = cursor->expr.elems[1];
        if (ppproof_plan_v1_expr_head(
                reference, "proof-premise-ref-v1", 1u)) {
            references[index].kind =
                PPCERTIFICATE_GSLT_REFERENCE_V1_PREMISE;
        } else if (ppproof_plan_v1_expr_head(
                       reference, "proof-node-ref-v1", 1u)) {
            references[index].kind = PPCERTIFICATE_GSLT_REFERENCE_V1_NODE;
        } else {
            ppproof_plan_v1_fail(
                parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
                "compiled proof artifact contains an unknown reference");
            return false;
        }
        if (!ppproof_plan_v1_nat(
                parse, reference->expr.elems[1], UINT32_MAX,
                &references[index].index))
            return false;
        cursor = cursor->expr.elems[2];
    }
    *references_out = references;
    *reference_len_out = reference_len;
    return true;
}

static bool ppproof_plan_v1_nodes(
    PPCertificateGSLTPlanParseV1 *parse,
    const Atom *term,
    PPCertificateGSLTArticleNodeV1 **nodes_out,
    uint32_t *node_len_out) {
    PPCertificateGSLTArticleNodeV1 *nodes = NULL;
    const Atom *cursor = term;
    uint32_t node_len;
    uint32_t index;

    if (!ppproof_plan_v1_list_count(
            parse, term, parse->limits->maximum_article_nodes,
            &node_len))
        return false;
    if (node_len != 0u) {
        nodes = ppproof_plan_v1_alloc(parse, node_len, sizeof(*nodes));
        if (!nodes)
            return false;
    }
    for (index = 0u; index < node_len; index++) {
        const Atom *node = cursor->expr.elems[1];
        PPCertificateGSLTPatternV1 *arguments;
        uint32_t argument_len;
        PPCertificateGSLTReferenceV1 *references;
        uint32_t reference_len;

        if (!ppproof_plan_v1_expr_head(node, "proof-node-v1", 4u) ||
            !ppproof_plan_v1_nat(
                parse, node->expr.elems[1], UINT32_MAX,
                &nodes[index].id) ||
            !ppproof_plan_v1_name(
                node->expr.elems[2],
                &nodes[index].rule_instance.rule_id) ||
            !ppproof_plan_v1_pattern_list(
                parse, node->expr.elems[3], 0u,
                &arguments, &argument_len) ||
            !ppproof_plan_v1_references(
                parse, node->expr.elems[4],
                &references, &reference_len)) {
            ppproof_plan_v1_fail(
                parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
                "compiled proof artifact contains a malformed node");
            return false;
        }
        nodes[index].rule_instance.arguments = arguments;
        nodes[index].rule_instance.argument_len = argument_len;
        nodes[index].children = references;
        nodes[index].child_len = reference_len;
        cursor = cursor->expr.elems[2];
    }
    *nodes_out = nodes;
    *node_len_out = node_len;
    return true;
}

static PPCertificateGSLTPlanRecordKindV1 ppproof_plan_v1_record(
    const Atom *answer, const Atom **record_out) {
    const Atom *record;

    if (!ppproof_plan_v1_expr_head(answer, "proof-artifact-v1", 1u))
        return PPCERTIFICATE_GSLT_PLAN_RECORD_UNKNOWN;
    record = answer->expr.elems[1];
    *record_out = record;
    if (ppproof_plan_v1_expr_head(record, "proof-extension-v1", 2u))
        return PPCERTIFICATE_GSLT_PLAN_RECORD_EXTENSION;
    if (ppproof_plan_v1_expr_head(
            record, "proof-base-constructor-v1", 4u))
        return PPCERTIFICATE_GSLT_PLAN_RECORD_CONSTRUCTOR;
    if (ppproof_plan_v1_expr_head(
            record, "proof-calculus-constructor-v1", 3u))
        return PPCERTIFICATE_GSLT_PLAN_RECORD_CALCULUS_CONSTRUCTOR;
    if (ppproof_plan_v1_expr_head(record, "proof-judgment-v1", 3u))
        return PPCERTIFICATE_GSLT_PLAN_RECORD_JUDGMENT;
    if (ppproof_plan_v1_expr_head(record, "proof-capability-v1", 2u))
        return PPCERTIFICATE_GSLT_PLAN_RECORD_CAPABILITY;
    if (ppproof_plan_v1_expr_head(record, "proof-conversion-v1", 3u))
        return PPCERTIFICATE_GSLT_PLAN_RECORD_CONVERSION;
    if (ppproof_plan_v1_expr_head(record, "proof-rule-v1", 6u))
        return PPCERTIFICATE_GSLT_PLAN_RECORD_RULE;
    if (ppproof_plan_v1_expr_head(record, "proof-article-v1", 7u))
        return PPCERTIFICATE_GSLT_PLAN_RECORD_ARTICLE;
    if (ppproof_plan_v1_expr_head(record, "proof-article-term-v1", 4u))
        return PPCERTIFICATE_GSLT_PLAN_RECORD_TERM_BINDING;
    if (ppproof_plan_v1_expr_head(
            record, "proof-article-named-node-v1", 6u))
        return PPCERTIFICATE_GSLT_PLAN_RECORD_NODE_BINDING;
    if (ppproof_plan_v1_expr_head(record, "proof-article-order-v1", 3u))
        return PPCERTIFICATE_GSLT_PLAN_RECORD_ORDER_BINDING;
    if (ppproof_plan_v1_expr_head(
            record, "proof-article-document-v1", 7u))
        return PPCERTIFICATE_GSLT_PLAN_RECORD_DOCUMENT;
    return PPCERTIFICATE_GSLT_PLAN_RECORD_UNKNOWN;
}

static bool ppproof_plan_v1_count_add(
    PPCertificateGSLTPlanParseV1 *parse, uint32_t *count) {
    if (*count == UINT32_MAX) {
        ppproof_plan_v1_fail(
            parse, PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE,
            "compiled proof artifact has too many records");
        return false;
    }
    (*count)++;
    return true;
}

static bool ppproof_plan_v1_count_records(
    PPCertificateGSLTPlanParseV1 *parse,
    PPCertificateGSLTPlanRecordCountsV1 *counts) {
    size_t index;

    memset(counts, 0, sizeof(*counts));
    for (index = 0u; index < parse->storage->answers.len; index++) {
        const Atom *record = NULL;
        PPCertificateGSLTPlanRecordKindV1 kind = ppproof_plan_v1_record(
            parse->storage->answers.terms[index], &record);
        uint32_t *count = NULL;

        (void)record;
        switch (kind) {
        case PPCERTIFICATE_GSLT_PLAN_RECORD_EXTENSION:
            count = &counts->extension_len;
            break;
        case PPCERTIFICATE_GSLT_PLAN_RECORD_CONSTRUCTOR:
            count = &counts->constructor_len;
            break;
        case PPCERTIFICATE_GSLT_PLAN_RECORD_CALCULUS_CONSTRUCTOR:
            count = &counts->calculus_constructor_len;
            break;
        case PPCERTIFICATE_GSLT_PLAN_RECORD_JUDGMENT:
            count = &counts->judgment_len;
            break;
        case PPCERTIFICATE_GSLT_PLAN_RECORD_CAPABILITY:
            count = &counts->capability_len;
            break;
        case PPCERTIFICATE_GSLT_PLAN_RECORD_CONVERSION:
            count = &counts->conversion_len;
            break;
        case PPCERTIFICATE_GSLT_PLAN_RECORD_RULE:
            count = &counts->rule_len;
            break;
        case PPCERTIFICATE_GSLT_PLAN_RECORD_ARTICLE:
            count = &counts->article_len;
            break;
        case PPCERTIFICATE_GSLT_PLAN_RECORD_TERM_BINDING:
            count = &counts->term_binding_len;
            break;
        case PPCERTIFICATE_GSLT_PLAN_RECORD_NODE_BINDING:
            count = &counts->node_binding_len;
            break;
        case PPCERTIFICATE_GSLT_PLAN_RECORD_ORDER_BINDING:
            count = &counts->order_binding_len;
            break;
        case PPCERTIFICATE_GSLT_PLAN_RECORD_DOCUMENT:
            count = &counts->document_len;
            break;
        default:
            ppproof_plan_v1_fail(
                parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
                "answer stream contains a non-proof artifact record");
            return false;
        }
        if (!ppproof_plan_v1_count_add(parse, count))
            return false;
    }
    if (counts->extension_len != 1u || counts->conversion_len > 1u) {
        ppproof_plan_v1_fail(
            parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
            "compiled proof artifact record cardinality is invalid");
        return false;
    }
    if (counts->order_binding_len != counts->document_len ||
        counts->article_len > UINT32_MAX - counts->document_len) {
        ppproof_plan_v1_fail(
            parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
            "compiled proof document record cardinality is invalid");
        return false;
    }
    if (counts->constructor_len >
            UINT32_MAX - counts->calculus_constructor_len ||
        counts->constructor_len + counts->calculus_constructor_len >
            parse->limits->maximum_presentation_pattern_nodes ||
        counts->judgment_len >
            parse->limits->maximum_presentation_pattern_nodes ||
        counts->rule_len >
            parse->limits->maximum_presentation_pattern_nodes ||
        counts->term_binding_len >
            parse->limits->maximum_presentation_pattern_nodes ||
        counts->node_binding_len >
            parse->limits->maximum_presentation_pattern_nodes ||
        counts->article_len + counts->document_len >
            parse->limits->maximum_article_nodes) {
        ppproof_plan_v1_fail(
            parse, PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE,
            "compiled proof artifact records exceed their resource limit");
        return false;
    }
    return true;
}

static bool ppproof_plan_v1_owner(
    PPCertificateGSLTPlanParseV1 *parse,
    const Atom *term,
    PPCertificateGSLTNameV1 expected) {
    PPCertificateGSLTNameV1 actual;

    if (!ppproof_plan_v1_name(term, &actual) ||
        !ppcertificate_gslt_article_v1_name_equal(actual, expected)) {
        ppproof_plan_v1_fail(
            parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
            "compiled proof artifact mixes extension owners");
        return false;
    }
    return true;
}

static bool ppproof_plan_v1_capability(
    PPCertificateGSLTPlanParseV1 *parse,
    const Atom *term,
    uint32_t *capability_out) {
    PPCertificateGSLTNameV1 name;

    if (!ppproof_plan_v1_name(term, &name)) {
        ppproof_plan_v1_fail(
            parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
            "compiled proof artifact has a malformed capability");
        return false;
    }
    if (ppproof_plan_v1_name_is(name, "proof-cap-binders-v1"))
        *capability_out = PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_BINDERS;
    else if (ppproof_plan_v1_name_is(
                 name, "proof-cap-explicit-substitution-v1"))
        *capability_out =
            PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_EXPLICIT_SUBSTITUTION;
    else if (ppproof_plan_v1_name_is(name, "proof-cap-collections-v1"))
        *capability_out = PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_COLLECTIONS;
    else if (ppproof_plan_v1_name_is(
                 name, "proof-cap-substitution-condition-v1"))
        *capability_out =
            PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_SUBSTITUTION_CONDITION;
    else if (ppproof_plan_v1_name_is(
                 name, "proof-cap-unused-binder-condition-v1"))
        *capability_out =
            PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_UNUSED_BINDER_CONDITION;
    else if (ppproof_plan_v1_name_is(
                 name, "proof-cap-conversion-declaration-v1"))
        *capability_out =
            PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_CONVERSION_DECLARATION;
    else {
        ppproof_plan_v1_fail(
            parse, PPCERTIFICATE_GSLT_ARTICLE_V1_UNSUPPORTED,
            "compiled proof artifact requires an unknown capability");
        return false;
    }
    return true;
}

static bool ppproof_plan_v1_rule(
    PPCertificateGSLTPlanParseV1 *parse,
    const Atom *record,
    PPCertificateGSLTNameV1 owner,
    PPCertificateGSLTRuleSchemaV1 *rule) {
    PPCertificateGSLTFormalV1 *formals;
    PPCertificateGSLTPatternV1 *premises;
    PPCertificateGSLTPatternV1 *conclusion;
    PPCertificateGSLTSideConditionV1 *conditions;

    conclusion = ppproof_plan_v1_alloc(
        parse, 1u, sizeof(*conclusion));
    if (!conclusion)
        return false;
    if (!ppproof_plan_v1_owner(parse, record->expr.elems[1], owner) ||
        !ppproof_plan_v1_name(record->expr.elems[2], &rule->id) ||
        !ppproof_plan_v1_formals(
            parse, record->expr.elems[3], &formals, &rule->formal_len) ||
        !ppproof_plan_v1_pattern_list(
            parse, record->expr.elems[4], 0u,
            &premises, &rule->premise_len) ||
        !ppproof_plan_v1_pattern(
            parse, record->expr.elems[5], 0u, conclusion) ||
        !ppproof_plan_v1_conditions(
            parse, record->expr.elems[6],
            &conditions, &rule->side_condition_len)) {
        ppproof_plan_v1_fail(
            parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
            "compiled proof artifact contains a malformed rule");
        return false;
    }
    rule->formals = formals;
    rule->premises = premises;
    rule->conclusion = conclusion;
    rule->side_conditions = conditions;
    return true;
}

static bool ppproof_plan_v1_article(
    PPCertificateGSLTPlanParseV1 *parse,
    const Atom *record,
    PPCertificateGSLTNameV1 owner,
    PPCertificateGSLTCompiledArticleV1 *compiled) {
    PPCertificateGSLTPatternV1 *context;
    PPCertificateGSLTArticleNodeV1 *nodes;
    PPCertificateGSLTPatternV1 *target;
    PPCertificateGSLTNameV1 policy;

    target = ppproof_plan_v1_alloc(parse, 1u, sizeof(*target));
    if (!target)
        return false;
    if (!ppproof_plan_v1_owner(parse, record->expr.elems[1], owner) ||
        !ppproof_plan_v1_name(record->expr.elems[2], &compiled->id) ||
        !ppproof_plan_v1_pattern_list(
            parse, record->expr.elems[3], 0u,
            &context, &compiled->context_len) ||
        !ppproof_plan_v1_nodes(
            parse, record->expr.elems[4],
            &nodes, &compiled->article.node_len) ||
        !ppproof_plan_v1_nat(
            parse, record->expr.elems[5], UINT32_MAX,
            &compiled->article.root_id) ||
        !ppproof_plan_v1_pattern(
            parse, record->expr.elems[6], 0u, target) ||
        !ppproof_plan_v1_name(record->expr.elems[7], &policy)) {
        ppproof_plan_v1_fail(
            parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
            "compiled proof artifact contains a malformed article");
        return false;
    }
    if (ppproof_plan_v1_name_is(policy, "proof-require-rooted-v1"))
        compiled->require_rooted = true;
    else if (ppproof_plan_v1_name_is(
                 policy, "proof-compatible-roots-v1"))
        compiled->require_rooted = false;
    else {
        ppproof_plan_v1_fail(
            parse, PPCERTIFICATE_GSLT_ARTICLE_V1_UNSUPPORTED,
            "compiled proof artifact requires an unknown root policy");
        return false;
    }
    compiled->context = context;
    compiled->article.version = 1u;
    compiled->article.nodes = nodes;
    compiled->article.target = target;
    return true;
}

static bool ppproof_plan_v1_term_binding(
    PPCertificateGSLTPlanParseV1 *parse,
    const Atom *record,
    PPCertificateGSLTNameV1 owner,
    PPCertificateGSLTPlanTermBindingV1 *binding) {
    if (!ppproof_plan_v1_owner(parse, record->expr.elems[1], owner) ||
        !ppproof_plan_v1_name(
            record->expr.elems[2], &binding->article) ||
        !ppproof_plan_v1_name(
            record->expr.elems[3], &binding->name)) {
        ppproof_plan_v1_fail(
            parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
            "compiled proof document has a malformed term binding");
        return false;
    }
    binding->source = record->expr.elems[4];
    return true;
}

static bool ppproof_plan_v1_node_binding(
    PPCertificateGSLTPlanParseV1 *parse,
    const Atom *record,
    PPCertificateGSLTNameV1 owner,
    PPCertificateGSLTPlanNodeBindingV1 *binding) {
    if (!ppproof_plan_v1_owner(parse, record->expr.elems[1], owner) ||
        !ppproof_plan_v1_name(
            record->expr.elems[2], &binding->article) ||
        !ppproof_plan_v1_name(
            record->expr.elems[3], &binding->name) ||
        !ppproof_plan_v1_name(
            record->expr.elems[4], &binding->rule)) {
        ppproof_plan_v1_fail(
            parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
            "compiled proof document has a malformed node binding");
        return false;
    }
    binding->arguments = record->expr.elems[5];
    binding->references = record->expr.elems[6];
    return true;
}

static bool ppproof_plan_v1_order_binding(
    PPCertificateGSLTPlanParseV1 *parse,
    const Atom *record,
    PPCertificateGSLTNameV1 owner,
    PPCertificateGSLTPlanOrderBindingV1 *binding) {
    if (!ppproof_plan_v1_owner(parse, record->expr.elems[1], owner) ||
        !ppproof_plan_v1_name(
            record->expr.elems[2], &binding->article)) {
        ppproof_plan_v1_fail(
            parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
            "compiled proof document has a malformed order binding");
        return false;
    }
    binding->order = record->expr.elems[3];
    return true;
}

static PPCertificateGSLTPlanNodeBindingV1 *ppproof_plan_v1_find_node_binding(
    PPCertificateGSLTPlanNodeBindingV1 *bindings,
    uint32_t binding_len,
    PPCertificateGSLTNameV1 article,
    PPCertificateGSLTNameV1 name) {
    uint32_t low = 0u;
    uint32_t high = binding_len;

    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        PPCertificateGSLTPlanNodeBindingV1 *candidate = &bindings[middle];
        int compared = ppproof_plan_v1_name_compare(
            article, candidate->article);
        if (compared == 0)
            compared = ppproof_plan_v1_name_compare(
                name, candidate->name);
        if (compared < 0)
            high = middle;
        else if (compared > 0)
            low = middle + 1u;
        else
            return candidate;
    }
    return NULL;
}

static PPCertificateGSLTPlanOrderBindingV1 *ppproof_plan_v1_find_order_binding(
    PPCertificateGSLTPlanOrderBindingV1 *bindings,
    uint32_t binding_len,
    PPCertificateGSLTNameV1 article) {
    uint32_t low = 0u;
    uint32_t high = binding_len;

    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        PPCertificateGSLTPlanOrderBindingV1 *candidate = &bindings[middle];
        int compared = ppproof_plan_v1_name_compare(
            article, candidate->article);
        if (compared < 0)
            high = middle;
        else if (compared > 0)
            low = middle + 1u;
        else
            return candidate;
    }
    return NULL;
}

static const PPCertificateGSLTPlanNodeIndexV1 *ppproof_plan_v1_find_node_index(
    const PPCertificateGSLTPlanNodeIndexV1 *indices,
    uint32_t index_len,
    PPCertificateGSLTNameV1 name) {
    uint32_t low = 0u;
    uint32_t high = index_len;

    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        const PPCertificateGSLTPlanNodeIndexV1 *candidate = &indices[middle];
        int compared = ppproof_plan_v1_name_compare(
            name, candidate->name);
        if (compared < 0)
            high = middle;
        else if (compared > 0)
            low = middle + 1u;
        else
            return candidate;
    }
    return NULL;
}

static bool ppproof_plan_v1_document_references(
    PPCertificateGSLTPlanParseV1 *parse,
    const Atom *term,
    const PPCertificateGSLTPlanNodeIndexV1 *indices,
    uint32_t index_len,
    PPCertificateGSLTReferenceV1 **references_out,
    uint32_t *reference_len_out) {
    PPCertificateGSLTReferenceV1 *references = NULL;
    const Atom *cursor = term;
    uint32_t reference_len;
    uint32_t index;

    if (!ppproof_plan_v1_list_count(
            parse, term, parse->limits->maximum_article_nodes,
            &reference_len))
        return false;
    if (reference_len != 0u) {
        references = ppproof_plan_v1_alloc(
            parse, reference_len, sizeof(*references));
        if (!references)
            return false;
    }
    for (index = 0u; index < reference_len; index++) {
        const Atom *reference = cursor->expr.elems[1];
        if (ppproof_plan_v1_expr_head(
                reference, "source-proof-node-ref-v1", 1u)) {
            PPCertificateGSLTNameV1 name;
            const PPCertificateGSLTPlanNodeIndexV1 *found;

            if (!ppproof_plan_v1_name(reference->expr.elems[1], &name) ||
                !(found = ppproof_plan_v1_find_node_index(
                      indices, index_len, name))) {
                ppproof_plan_v1_fail(
                    parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
                    "compiled proof document has an unknown node reference");
                return false;
            }
            references[index].kind = PPCERTIFICATE_GSLT_REFERENCE_V1_NODE;
            references[index].index = found->index;
        } else if (ppproof_plan_v1_expr_head(
                       reference, "proof-premise-ref-v1", 1u)) {
            references[index].kind = PPCERTIFICATE_GSLT_REFERENCE_V1_PREMISE;
            if (!ppproof_plan_v1_nat(
                    parse, reference->expr.elems[1], UINT32_MAX,
                    &references[index].index))
                return false;
        } else {
            ppproof_plan_v1_fail(
                parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
                "compiled proof document has an unknown reference form");
            return false;
        }
        cursor = cursor->expr.elems[2];
    }
    *references_out = references;
    *reference_len_out = reference_len;
    return true;
}

static bool ppproof_plan_v1_document(
    PPCertificateGSLTPlanParseV1 *parse,
    const Atom *record,
    PPCertificateGSLTNameV1 owner,
    PPCertificateGSLTPlanTermBindingV1 *term_bindings,
    uint32_t term_binding_len,
    PPCertificateGSLTPlanNodeBindingV1 *node_bindings,
    uint32_t node_binding_len,
    PPCertificateGSLTPlanOrderBindingV1 *order_bindings,
    uint32_t order_binding_len,
    PPCertificateGSLTCompiledArticleV1 *compiled) {
    PPCertificateGSLTPlanOrderBindingV1 *order_binding;
    PPCertificateGSLTPlanNodeIndexV1 *indices;
    PPCertificateGSLTNameV1 *ordered_names;
    PPCertificateGSLTArticleNodeV1 *nodes;
    PPCertificateGSLTPatternV1 *context;
    PPCertificateGSLTPatternV1 *target;
    PPCertificateGSLTPlanResolutionV1 resolution;
    PPCertificateGSLTNameV1 start;
    PPCertificateGSLTNameV1 root;
    PPCertificateGSLTNameV1 policy;
    const PPCertificateGSLTPlanNodeIndexV1 *root_index;
    const Atom *cursor;
    uint32_t node_len;
    uint32_t index;

    target = ppproof_plan_v1_alloc(parse, 1u, sizeof(*target));
    if (!target ||
        !ppproof_plan_v1_owner(parse, record->expr.elems[1], owner) ||
        !ppproof_plan_v1_name(record->expr.elems[2], &compiled->id) ||
        !ppproof_plan_v1_name(record->expr.elems[4], &start) ||
        !ppproof_plan_v1_name(record->expr.elems[5], &root) ||
        !ppproof_plan_v1_name(record->expr.elems[7], &policy)) {
        ppproof_plan_v1_fail(
            parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
            "compiled proof document declaration is malformed");
        return false;
    }
    order_binding = ppproof_plan_v1_find_order_binding(
        order_bindings, order_binding_len, compiled->id);
    if (!order_binding || order_binding->used ||
        !ppproof_plan_v1_list_count(
            parse, order_binding->order,
            parse->limits->maximum_article_nodes, &node_len) ||
        node_len == 0u) {
        ppproof_plan_v1_fail(
            parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
            "compiled proof document has no unique nonempty order");
        return false;
    }
    order_binding->used = true;
    indices = ppproof_plan_v1_alloc(
        parse, node_len, sizeof(*indices));
    ordered_names = ppproof_plan_v1_alloc(
        parse, node_len, sizeof(*ordered_names));
    nodes = ppproof_plan_v1_alloc(parse, node_len, sizeof(*nodes));
    if (!indices || !ordered_names || !nodes)
        return false;
    cursor = order_binding->order;
    for (index = 0u; index < node_len; index++) {
        if (!ppproof_plan_v1_name(
                cursor->expr.elems[1], &ordered_names[index])) {
            ppproof_plan_v1_fail(
                parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
                "compiled proof document order has a malformed node name");
            return false;
        }
        indices[index].name = ordered_names[index];
        indices[index].index = index;
        cursor = cursor->expr.elems[2];
    }
    qsort(indices, node_len, sizeof(*indices),
          ppproof_plan_v1_node_index_compare);
    for (index = 1u; index < node_len; index++) {
        if (ppproof_plan_v1_name_compare(
                indices[index - 1u].name, indices[index].name) == 0) {
            ppproof_plan_v1_fail(
                parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
                "compiled proof document repeats a node in its order");
            return false;
        }
    }
    if (ppproof_plan_v1_name_compare(start, ordered_names[0]) != 0 ||
        !(root_index = ppproof_plan_v1_find_node_index(
              indices, node_len, root))) {
        ppproof_plan_v1_fail(
            parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
            "compiled proof document start or root is outside its order");
        return false;
    }
    resolution = (PPCertificateGSLTPlanResolutionV1){
        .terms = term_bindings,
        .term_len = term_binding_len,
        .article = compiled->id,
    };
    if (!ppproof_plan_v1_pattern_list_resolved(
            parse, record->expr.elems[3], 0u, &resolution,
            &context, &compiled->context_len))
        return false;
    for (index = 0u; index < node_len; index++) {
        PPCertificateGSLTPlanNodeBindingV1 *binding =
            ppproof_plan_v1_find_node_binding(
                node_bindings, node_binding_len,
                compiled->id, ordered_names[index]);
        PPCertificateGSLTPatternV1 *arguments;
        PPCertificateGSLTReferenceV1 *references;
        uint32_t argument_len;
        uint32_t reference_len;

        if (!binding || binding->used) {
            ppproof_plan_v1_fail(
                parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
                "compiled proof document lacks a unique ordered node");
            return false;
        }
        binding->used = true;
        if (!ppproof_plan_v1_pattern_list_resolved(
                parse, binding->arguments, 0u, &resolution,
                &arguments, &argument_len) ||
            !ppproof_plan_v1_document_references(
                parse, binding->references, indices, node_len,
                &references, &reference_len))
            return false;
        nodes[index].id = index;
        nodes[index].rule_instance.rule_id = binding->rule;
        nodes[index].rule_instance.arguments = arguments;
        nodes[index].rule_instance.argument_len = argument_len;
        nodes[index].children = references;
        nodes[index].child_len = reference_len;
    }
    if (!ppproof_plan_v1_pattern_resolved(
            parse, record->expr.elems[6], 0u, &resolution, target))
        return false;
    if (ppproof_plan_v1_name_is(policy, "proof-require-rooted-v1"))
        compiled->require_rooted = true;
    else if (ppproof_plan_v1_name_is(
                 policy, "proof-compatible-roots-v1"))
        compiled->require_rooted = false;
    else {
        ppproof_plan_v1_fail(
            parse, PPCERTIFICATE_GSLT_ARTICLE_V1_UNSUPPORTED,
            "compiled proof document requires an unknown root policy");
        return false;
    }
    compiled->context = context;
    compiled->article.version = 1u;
    compiled->article.nodes = nodes;
    compiled->article.node_len = node_len;
    compiled->article.root_id = root_index->index;
    compiled->article.target = target;
    return true;
}

PPCertificateGSLTArticleV1Result ppcertificate_gslt_plan_v1_load(
    PPCertificateGSLTPlanV1 *plan,
    const char *answer_path,
    const PPCertificateGSLTArticleV1Limits *limits_argument,
    char *error_buf,
    size_t error_buf_size) {
    PPCertificateGSLTArticleV1Limits default_limits;
    const PPCertificateGSLTArticleV1Limits *limits = limits_argument;
    PPCertificateGSLTPlanV1 result_plan;
    PPCertificateGSLTPlanStorageV1 *storage = NULL;
    PPCertificateGSLTPlanParseV1 parse;
    PPCertificateGSLTPlanRecordCountsV1 counts;
    PPCertificateGSLTConstructorV1 *constructors = NULL;
    PPCertificateGSLTConstructorOriginV1 *constructor_origins = NULL;
    PPCertificateGSLTJudgmentV1 *judgments = NULL;
    PPCertificateGSLTRuleSchemaV1 *rules = NULL;
    PPCertificateGSLTCompiledArticleV1 *articles = NULL;
    PPCertificateGSLTPlanTermBindingV1 *term_bindings = NULL;
    PPCertificateGSLTPlanNodeBindingV1 *node_bindings = NULL;
    PPCertificateGSLTPlanOrderBindingV1 *order_bindings = NULL;
    const Atom **documents = NULL;
    uint32_t constructor_index = 0u;
    uint32_t total_constructor_len;
    uint32_t judgment_index = 0u;
    uint32_t rule_index = 0u;
    uint32_t article_index = 0u;
    uint32_t term_binding_index = 0u;
    uint32_t node_binding_index = 0u;
    uint32_t order_binding_index = 0u;
    uint32_t document_index = 0u;
    uint32_t total_article_len;
    uint32_t required_capabilities = 0u;
    bool conversion_seen = false;
    size_t index;

    memset(&result_plan, 0, sizeof(result_plan));
    if (error_buf && error_buf_size != 0u)
        error_buf[0] = '\0';
    if (!plan || !answer_path) {
        ppproof_plan_v1_set_error(
            error_buf, error_buf_size,
            "invalid compiled proof artifact request");
        return PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID;
    }
    if (!limits) {
        default_limits = ppcertificate_gslt_article_v1_default_limits();
        limits = &default_limits;
    }
    if (limits->maximum_pattern_depth == 0u ||
        limits->maximum_presentation_pattern_nodes == 0u ||
        limits->maximum_materialized_pattern_nodes == 0u ||
        limits->maximum_article_nodes == 0u) {
        ppproof_plan_v1_set_error(
            error_buf, error_buf_size,
            "compiled proof artifact limits are malformed");
        return PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID;
    }
    storage = calloc(1u, sizeof(*storage));
    if (!storage) {
        ppproof_plan_v1_set_error(
            error_buf, error_buf_size,
            "compiled proof artifact storage allocation failed");
        return PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
    }
    fh_answer_stream_v1_init(&storage->answers);
    if (!fh_answer_stream_v1_read(
            &storage->answers, answer_path, error_buf, error_buf_size)) {
        ppproof_plan_v1_storage_free(storage);
        return PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID;
    }
    parse = (PPCertificateGSLTPlanParseV1){
        .storage = storage,
        .limits = limits,
        .remaining_patterns =
            limits->maximum_presentation_pattern_nodes >
                    UINT32_MAX - limits->maximum_materialized_pattern_nodes
                ? UINT32_MAX
                : limits->maximum_presentation_pattern_nodes +
                      limits->maximum_materialized_pattern_nodes,
        .result = PPCERTIFICATE_GSLT_ARTICLE_V1_OK,
        .error_buf = error_buf,
        .error_buf_size = error_buf_size,
    };
    if (!ppproof_plan_v1_count_records(&parse, &counts))
        goto failed;

    for (index = 0u; index < storage->answers.len; index++) {
        const Atom *record = NULL;
        if (ppproof_plan_v1_record(
                storage->answers.terms[index], &record) ==
            PPCERTIFICATE_GSLT_PLAN_RECORD_EXTENSION) {
            if (!ppproof_plan_v1_name(
                    record->expr.elems[1], &result_plan.owner) ||
                !ppproof_plan_v1_name(
                    record->expr.elems[2], &result_plan.base)) {
                ppproof_plan_v1_fail(
                    &parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
                    "compiled proof extension identity is malformed");
                goto failed;
            }
            break;
        }
    }
    if (counts.constructor_len >
        UINT32_MAX - counts.calculus_constructor_len) {
        ppproof_plan_v1_fail(
            &parse, PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE,
            "compiled proof artifact has too many constructors");
        goto failed;
    }
    total_constructor_len =
        counts.constructor_len + counts.calculus_constructor_len;
    constructors = ppproof_plan_v1_alloc(
        &parse, total_constructor_len, sizeof(*constructors));
    constructor_origins = ppproof_plan_v1_alloc(
        &parse, total_constructor_len, sizeof(*constructor_origins));
    judgments = ppproof_plan_v1_alloc(
        &parse, counts.judgment_len, sizeof(*judgments));
    rules = ppproof_plan_v1_alloc(
        &parse, counts.rule_len, sizeof(*rules));
    total_article_len = counts.article_len + counts.document_len;
    articles = ppproof_plan_v1_alloc(
        &parse, total_article_len, sizeof(*articles));
    term_bindings = ppproof_plan_v1_alloc(
        &parse, counts.term_binding_len, sizeof(*term_bindings));
    node_bindings = ppproof_plan_v1_alloc(
        &parse, counts.node_binding_len, sizeof(*node_bindings));
    order_bindings = ppproof_plan_v1_alloc(
        &parse, counts.order_binding_len, sizeof(*order_bindings));
    documents = ppproof_plan_v1_alloc(
        &parse, counts.document_len, sizeof(*documents));
    if ((total_constructor_len != 0u &&
         (!constructors || !constructor_origins)) ||
        (counts.judgment_len != 0u && !judgments) ||
        (counts.rule_len != 0u && !rules) ||
        (total_article_len != 0u && !articles) ||
        (counts.term_binding_len != 0u && !term_bindings) ||
        (counts.node_binding_len != 0u && !node_bindings) ||
        (counts.order_binding_len != 0u && !order_bindings) ||
        (counts.document_len != 0u && !documents))
        goto failed;

    for (index = 0u; index < storage->answers.len; index++) {
        const Atom *record = NULL;
        PPCertificateGSLTPlanRecordKindV1 kind = ppproof_plan_v1_record(
            storage->answers.terms[index], &record);

        switch (kind) {
        case PPCERTIFICATE_GSLT_PLAN_RECORD_EXTENSION:
            break;
        case PPCERTIFICATE_GSLT_PLAN_RECORD_CONSTRUCTOR: {
            PPCertificateGSLTNameV1 base;
            if (!ppproof_plan_v1_owner(
                    &parse, record->expr.elems[1], result_plan.owner) ||
                !ppproof_plan_v1_name(record->expr.elems[2], &base) ||
                !ppcertificate_gslt_article_v1_name_equal(
                    base, result_plan.base) ||
                !ppproof_plan_v1_name(
                    record->expr.elems[3],
                    &constructors[constructor_index].name) ||
                !ppproof_plan_v1_nat(
                    &parse, record->expr.elems[4], UINT32_MAX,
                    &constructors[constructor_index].arity)) {
                ppproof_plan_v1_fail(
                    &parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
                    "compiled proof constructor record is malformed");
                goto failed;
            }
            constructor_origins[constructor_index] =
                PPCERTIFICATE_GSLT_CONSTRUCTOR_ORIGIN_V1_BASE;
            constructor_index++;
            break;
        }
        case PPCERTIFICATE_GSLT_PLAN_RECORD_CALCULUS_CONSTRUCTOR:
            if (!ppproof_plan_v1_owner(
                    &parse, record->expr.elems[1], result_plan.owner) ||
                !ppproof_plan_v1_name(
                    record->expr.elems[2],
                    &constructors[constructor_index].name) ||
                !ppproof_plan_v1_nat(
                    &parse, record->expr.elems[3], UINT32_MAX,
                    &constructors[constructor_index].arity)) {
                ppproof_plan_v1_fail(
                    &parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
                    "compiled proof calculus constructor record is malformed");
                goto failed;
            }
            constructor_origins[constructor_index] =
                PPCERTIFICATE_GSLT_CONSTRUCTOR_ORIGIN_V1_CALCULUS;
            constructor_index++;
            break;
        case PPCERTIFICATE_GSLT_PLAN_RECORD_JUDGMENT:
            if (!ppproof_plan_v1_owner(
                    &parse, record->expr.elems[1], result_plan.owner) ||
                !ppproof_plan_v1_name(
                    record->expr.elems[2],
                    &judgments[judgment_index].head) ||
                !ppproof_plan_v1_nat(
                    &parse, record->expr.elems[3], UINT32_MAX,
                    &judgments[judgment_index].arity)) {
                ppproof_plan_v1_fail(
                    &parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
                    "compiled proof judgment record is malformed");
                goto failed;
            }
            judgment_index++;
            break;
        case PPCERTIFICATE_GSLT_PLAN_RECORD_CAPABILITY: {
            uint32_t capability;
            if (!ppproof_plan_v1_owner(
                    &parse, record->expr.elems[1], result_plan.owner) ||
                !ppproof_plan_v1_capability(
                    &parse, record->expr.elems[2], &capability))
                goto failed;
            if ((required_capabilities & capability) != 0u) {
                ppproof_plan_v1_fail(
                    &parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
                    "compiled proof artifact repeats a capability");
                goto failed;
            }
            required_capabilities |= capability;
            break;
        }
        case PPCERTIFICATE_GSLT_PLAN_RECORD_CONVERSION:
            if (conversion_seen ||
                !ppproof_plan_v1_owner(
                    &parse, record->expr.elems[1], result_plan.owner) ||
                !ppproof_plan_v1_name(
                    record->expr.elems[2],
                    &result_plan.presentation.conversion.judgment_head) ||
                !ppproof_plan_v1_name(
                    record->expr.elems[3],
                    &result_plan.presentation.conversion.version)) {
                ppproof_plan_v1_fail(
                    &parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
                    "compiled proof conversion record is malformed");
                goto failed;
            }
            conversion_seen = true;
            result_plan.presentation.conversion.present = true;
            break;
        case PPCERTIFICATE_GSLT_PLAN_RECORD_RULE:
            if (!ppproof_plan_v1_rule(
                    &parse, record, result_plan.owner,
                    &rules[rule_index++]))
                goto failed;
            break;
        case PPCERTIFICATE_GSLT_PLAN_RECORD_ARTICLE:
            if (!ppproof_plan_v1_article(
                    &parse, record, result_plan.owner,
                    &articles[article_index++]))
                goto failed;
            break;
        case PPCERTIFICATE_GSLT_PLAN_RECORD_TERM_BINDING:
            if (!ppproof_plan_v1_term_binding(
                    &parse, record, result_plan.owner,
                    &term_bindings[term_binding_index++]))
                goto failed;
            break;
        case PPCERTIFICATE_GSLT_PLAN_RECORD_NODE_BINDING:
            if (!ppproof_plan_v1_node_binding(
                    &parse, record, result_plan.owner,
                    &node_bindings[node_binding_index++]))
                goto failed;
            break;
        case PPCERTIFICATE_GSLT_PLAN_RECORD_ORDER_BINDING:
            if (!ppproof_plan_v1_order_binding(
                    &parse, record, result_plan.owner,
                    &order_bindings[order_binding_index++]))
                goto failed;
            break;
        case PPCERTIFICATE_GSLT_PLAN_RECORD_DOCUMENT:
            if (!ppproof_plan_v1_owner(
                    &parse, record->expr.elems[1], result_plan.owner))
                goto failed;
            documents[document_index++] = record;
            break;
        default:
            ppproof_plan_v1_fail(
                &parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
                "compiled proof artifact contains an unknown record");
            goto failed;
        }
    }

    if (counts.term_binding_len > 1u)
        qsort(term_bindings, counts.term_binding_len,
              sizeof(*term_bindings), ppproof_plan_v1_term_binding_compare);
    if (counts.node_binding_len > 1u)
        qsort(node_bindings, counts.node_binding_len,
              sizeof(*node_bindings), ppproof_plan_v1_node_binding_compare);
    if (counts.order_binding_len > 1u)
        qsort(order_bindings, counts.order_binding_len,
              sizeof(*order_bindings), ppproof_plan_v1_order_binding_compare);
    for (index = 1u; index < counts.term_binding_len; index++) {
        if (ppproof_plan_v1_term_binding_compare(
                &term_bindings[index - 1u], &term_bindings[index]) == 0) {
            ppproof_plan_v1_fail(
                &parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
                "compiled proof document repeats a term binding");
            goto failed;
        }
    }
    for (index = 1u; index < counts.node_binding_len; index++) {
        if (ppproof_plan_v1_node_binding_compare(
                &node_bindings[index - 1u], &node_bindings[index]) == 0) {
            ppproof_plan_v1_fail(
                &parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
                "compiled proof document repeats a node binding");
            goto failed;
        }
    }
    for (index = 1u; index < counts.order_binding_len; index++) {
        if (ppproof_plan_v1_order_binding_compare(
                &order_bindings[index - 1u],
                &order_bindings[index]) == 0) {
            ppproof_plan_v1_fail(
                &parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
                "compiled proof document repeats an order binding");
            goto failed;
        }
    }
    for (index = 0u; index < counts.document_len; index++) {
        if (!ppproof_plan_v1_document(
                &parse, documents[index], result_plan.owner,
                term_bindings, counts.term_binding_len,
                node_bindings, counts.node_binding_len,
                order_bindings, counts.order_binding_len,
                &articles[article_index++]))
            goto failed;
    }
    for (index = 0u; index < counts.term_binding_len; index++) {
        if (!term_bindings[index].used) {
            ppproof_plan_v1_fail(
                &parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
                "compiled proof document contains an unused term binding");
            goto failed;
        }
    }
    for (index = 0u; index < counts.node_binding_len; index++) {
        if (!node_bindings[index].used) {
            ppproof_plan_v1_fail(
                &parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
                "compiled proof document contains an unordered node binding");
            goto failed;
        }
    }
    for (index = 0u; index < counts.order_binding_len; index++) {
        if (!order_bindings[index].used) {
            ppproof_plan_v1_fail(
                &parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
                "compiled proof document contains an unused order binding");
            goto failed;
        }
    }

    result_plan.presentation.constructors = constructors;
    result_plan.presentation.constructor_len = total_constructor_len;
    result_plan.constructor_origins = constructor_origins;
    result_plan.base_constructor_len = counts.constructor_len;
    result_plan.calculus_constructor_len = counts.calculus_constructor_len;
    result_plan.presentation.judgments = judgments;
    result_plan.presentation.judgment_len = counts.judgment_len;
    result_plan.presentation.rules = rules;
    result_plan.presentation.rule_len = counts.rule_len;
    result_plan.presentation.required_capabilities = required_capabilities;
    result_plan.articles = articles;
    result_plan.article_len = total_article_len;
    memcpy(result_plan.semantic_digest,
           storage->answers.digest, sizeof(result_plan.semantic_digest));
    result_plan.storage = storage;

    parse.result = ppcertificate_gslt_article_v1_presentation_validate(
        &result_plan.presentation, limits, NULL,
        error_buf, error_buf_size);
    if (parse.result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
        goto failed;
    for (index = 0u; index < result_plan.article_len; index++) {
        PPCertificateGSLTArticleV1Receipt receipt;
        uint32_t right;

        for (right = (uint32_t)index + 1u;
             right < result_plan.article_len; right++) {
            if (ppcertificate_gslt_article_v1_name_equal(
                    result_plan.articles[index].id,
                    result_plan.articles[right].id)) {
                ppproof_plan_v1_fail(
                    &parse, PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
                    "compiled proof artifact repeats an article identifier");
                goto failed;
            }
        }
        parse.result = ppcertificate_gslt_article_v1_check_open(
            &result_plan.presentation,
            result_plan.articles[index].context,
            result_plan.articles[index].context_len,
            &result_plan.articles[index].article,
            result_plan.articles[index].require_rooted,
            limits, &receipt, error_buf, error_buf_size);
        if (parse.result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            goto failed;
    }

    ppcertificate_gslt_plan_v1_free(plan);
    *plan = result_plan;
    return PPCERTIFICATE_GSLT_ARTICLE_V1_OK;

failed:
    ppproof_plan_v1_storage_free(storage);
    return parse.result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK
               ? PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID
               : parse.result;
}

const PPCertificateGSLTCompiledArticleV1 *ppcertificate_gslt_plan_v1_find_article(
    const PPCertificateGSLTPlanV1 *plan,
    const char *article_id) {
    size_t id_len;
    uint32_t index;

    if (!plan || !article_id)
        return NULL;
    id_len = strlen(article_id);
    if (id_len > UINT32_MAX)
        return NULL;
    for (index = 0u; index < plan->article_len; index++) {
        if (plan->articles[index].id.len == (uint32_t)id_len &&
            memcmp(plan->articles[index].id.bytes,
                   article_id, id_len) == 0)
            return &plan->articles[index];
    }
    return NULL;
}
