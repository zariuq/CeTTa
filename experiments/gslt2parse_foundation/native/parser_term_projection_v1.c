#include "parser_term_projection_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    PPTP_V1_MAX_PLAN_ITEMS = 65536u,
    PPTP_V1_MAX_TEXT_BYTES = 16u * 1024u * 1024u
};

typedef enum {
    PPTP_V1_RULE_SYMBOL = 0,
    PPTP_V1_RULE_FIXED = 1,
    PPTP_V1_RULE_QUOTE = 2,
    PPTP_V1_RULE_MONOID = 3,
    PPTP_V1_RULE_BINDER = 4
} PPTPV1RuleKind;

typedef struct {
    PPTPV1RuleKind kind;
    char *source;
    char *target;
    char *result_sort;
    uint32_t *indexes;
    char **child_sorts;
    uint32_t child_len;
    uint32_t quote_index;
    char *unit;
    char *element_sort;
    uint32_t channel_index;
    uint32_t binder_index;
    uint32_t body_index;
    char *channel_sort;
    char *binder_sort;
    char *body_sort;
} PPTPV1Rule;

typedef struct {
    char *node_label;
    char *document_label;
    char *document_sort;
    char *variable_sort;
    char *pair_label;
    char *cons_label;
    char *nil_label;
    char *codepoint_label;
    char **text_wrappers;
    uint32_t text_wrapper_len;
    char **variable_labels;
    uint32_t variable_label_len;
    char *binder_label;
    PPTPV1Rule *rules;
    uint32_t rule_len;
} PPTPV1PlanImpl;

typedef struct {
    Atom **data;
    uint32_t len;
    uint32_t cap;
} PPTPV1AtomVec;

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} PPTPV1ByteVec;

typedef enum {
    PPTP_V1_SCOPED_BOUND = 0,
    PPTP_V1_SCOPED_FREE = 1,
    PPTP_V1_SCOPED_SYMBOL = 2,
    PPTP_V1_SCOPED_APPLY = 3,
    PPTP_V1_SCOPED_QUOTE = 4,
    PPTP_V1_SCOPED_MONOID = 5,
    PPTP_V1_SCOPED_BINDER = 6
} PPTPV1ScopedKind;

typedef struct PPTPV1ScopedTerm PPTPV1ScopedTerm;
struct PPTPV1ScopedTerm {
    PPTPV1ScopedKind kind;
    char *sort;
    char *head;
    char *spelling;
    uint32_t bound_index;
    PPTPV1ScopedTerm **children;
    uint32_t child_len;
    char *key;
};

typedef struct {
    PPTPV1ScopedTerm **data;
    uint32_t len;
    uint32_t cap;
} PPTPV1ScopedVec;

typedef struct PPTPV1ScopeFrame PPTPV1ScopeFrame;
struct PPTPV1ScopeFrame {
    const char *spelling;
    const PPTPV1ScopeFrame *parent;
};

typedef struct {
    const PPTPV1PlanImpl *plan;
    uint32_t depth_limit;
    char *error;
    size_t error_cap;
} PPTPV1BuildContext;

typedef struct {
    const char *spelling;
    Atom *variable;
} PPTPV1MaterialBinding;

typedef struct {
    Arena *arena;
    PPTPV1MaterialBinding *binders;
    uint32_t binder_len;
    uint32_t binder_cap;
    uint32_t binder_base;
    PPTPV1MaterialBinding *free_names;
    uint32_t free_name_len;
    uint32_t free_name_cap;
    uint32_t depth_limit;
    char *error;
    size_t error_cap;
} PPTPV1MaterialContext;

static void pptp_v1_set_error(char *buf, size_t size, const char *fmt, ...) {
    va_list arguments;

    if (!buf || size == 0u)
        return;
    va_start(arguments, fmt);
    (void)vsnprintf(buf, size, fmt, arguments);
    va_end(arguments);
}

static char *pptp_v1_strdup(const char *text) {
    size_t len;
    char *copy;

    if (!text)
        return NULL;
    len = strlen(text);
    copy = malloc(len + 1u);
    if (!copy)
        return NULL;
    memcpy(copy, text, len + 1u);
    return copy;
}

static bool pptp_v1_expr_head(
    const Atom *atom,
    const char *head,
    CettaExprLen argument_len) {
    return atom && atom->kind == ATOM_EXPR &&
        atom->expr.len == argument_len + 1u &&
        atom_is_symbol(atom->expr.elems[0], head);
}

static const char *pptp_v1_symbol_name(const Atom *atom) {
    if (!atom || atom->kind != ATOM_SYMBOL)
        return NULL;
    return atom_name_cstr((Atom *)atom);
}

static char *pptp_v1_copy_symbol(
    const Atom *atom,
    const char *context,
    char *error,
    size_t error_cap) {
    const char *name = pptp_v1_symbol_name(atom);
    char *copy;

    if (!name || name[0] == '\0') {
        pptp_v1_set_error(error, error_cap, "%s must be a nonempty symbol",
                          context);
        return NULL;
    }
    copy = pptp_v1_strdup(name);
    if (!copy)
        pptp_v1_set_error(error, error_cap, "out of memory copying %s",
                          context);
    return copy;
}

static bool pptp_v1_atom_vec_push(PPTPV1AtomVec *vec, Atom *atom) {
    Atom **next;
    uint32_t cap;

    if (vec->len == vec->cap) {
        cap = vec->cap ? vec->cap * 2u : 8u;
        if (cap < vec->cap || cap > PPTP_V1_MAX_PLAN_ITEMS)
            cap = PPTP_V1_MAX_PLAN_ITEMS;
        if (cap == vec->cap)
            return false;
        next = realloc(vec->data, sizeof(*next) * (size_t)cap);
        if (!next)
            return false;
        vec->data = next;
        vec->cap = cap;
    }
    vec->data[vec->len++] = atom;
    return true;
}

static bool pptp_v1_decode_atom_list(
    const Atom *term,
    const char *cons_a,
    const char *cons_b,
    const char *nil,
    PPTPV1AtomVec *out,
    const char *context,
    char *error,
    size_t error_cap) {
    const Atom *cursor = term;

    memset(out, 0, sizeof(*out));
    while (!atom_is_symbol((Atom *)cursor, nil)) {
        if (!cursor || cursor->kind != ATOM_EXPR ||
            cursor->expr.len != 3u ||
            !(atom_is_symbol(cursor->expr.elems[0], cons_a) ||
              (cons_b && atom_is_symbol(cursor->expr.elems[0], cons_b)))) {
            pptp_v1_set_error(error, error_cap,
                              "%s is not a proper compiled list", context);
            free(out->data);
            memset(out, 0, sizeof(*out));
            return false;
        }
        if (out->len >= PPTP_V1_MAX_PLAN_ITEMS ||
            !pptp_v1_atom_vec_push(out, cursor->expr.elems[1])) {
            pptp_v1_set_error(error, error_cap,
                              "%s exceeds the compiled-list limit", context);
            free(out->data);
            memset(out, 0, sizeof(*out));
            return false;
        }
        cursor = cursor->expr.elems[2];
    }
    return true;
}

static bool pptp_v1_atom_u32(
    const Atom *atom,
    uint32_t *out,
    const char *context,
    char *error,
    size_t error_cap) {
    int64_t value;

    if (!atom || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_INT) {
        pptp_v1_set_error(error, error_cap, "%s must be an integer", context);
        return false;
    }
    value = atom->ground.ival;
    if (value < 0 || (uint64_t)value > UINT32_MAX) {
        pptp_v1_set_error(error, error_cap, "%s is outside uint32", context);
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

static bool pptp_v1_string_in(
    char *const *values,
    uint32_t len,
    const char *value) {
    uint32_t index;

    for (index = 0u; index < len; index++) {
        if (strcmp(values[index], value) == 0)
            return true;
    }
    return false;
}

static void pptp_v1_free_strings(char **values, uint32_t len) {
    uint32_t index;

    for (index = 0u; index < len; index++)
        free(values[index]);
    free(values);
}

static bool pptp_v1_copy_symbol_list(
    const Atom *term,
    char ***out_values,
    uint32_t *out_len,
    const char *context,
    char *error,
    size_t error_cap) {
    PPTPV1AtomVec atoms;
    char **values = NULL;
    uint32_t index;

    if (!pptp_v1_decode_atom_list(term, "ptp-cons", NULL, "ptp-nil",
                                  &atoms, context, error, error_cap))
        return false;
    if (atoms.len == 0u) {
        pptp_v1_set_error(error, error_cap, "%s must not be empty", context);
        free(atoms.data);
        return false;
    }
    values = calloc(atoms.len, sizeof(*values));
    if (!values) {
        pptp_v1_set_error(error, error_cap, "out of memory copying %s",
                          context);
        free(atoms.data);
        return false;
    }
    for (index = 0u; index < atoms.len; index++) {
        values[index] = pptp_v1_copy_symbol(
            atoms.data[index], context, error, error_cap);
        if (!values[index] ||
            pptp_v1_string_in(values, index, values[index])) {
            if (values[index])
                pptp_v1_set_error(error, error_cap,
                                  "%s contains a duplicate symbol", context);
            pptp_v1_free_strings(values, atoms.len);
            free(atoms.data);
            return false;
        }
    }
    *out_values = values;
    *out_len = atoms.len;
    free(atoms.data);
    return true;
}

static void pptp_v1_rule_free(PPTPV1Rule *rule) {
    free(rule->source);
    free(rule->target);
    free(rule->result_sort);
    free(rule->indexes);
    pptp_v1_free_strings(rule->child_sorts, rule->child_len);
    free(rule->unit);
    free(rule->element_sort);
    free(rule->channel_sort);
    free(rule->binder_sort);
    free(rule->body_sort);
    memset(rule, 0, sizeof(*rule));
}

static void pptp_v1_impl_free(PPTPV1PlanImpl *impl) {
    uint32_t index;

    if (!impl)
        return;
    free(impl->node_label);
    free(impl->document_label);
    free(impl->document_sort);
    free(impl->variable_sort);
    free(impl->pair_label);
    free(impl->cons_label);
    free(impl->nil_label);
    free(impl->codepoint_label);
    pptp_v1_free_strings(impl->text_wrappers, impl->text_wrapper_len);
    pptp_v1_free_strings(impl->variable_labels, impl->variable_label_len);
    free(impl->binder_label);
    for (index = 0u; index < impl->rule_len; index++)
        pptp_v1_rule_free(&impl->rules[index]);
    free(impl->rules);
    free(impl);
}

static bool pptp_v1_parse_indexes(
    const Atom *term,
    uint32_t **out_indexes,
    uint32_t *out_len,
    const char *context,
    char *error,
    size_t error_cap) {
    PPTPV1AtomVec atoms;
    uint32_t *indexes = NULL;
    uint32_t index;

    if (!pptp_v1_decode_atom_list(term, "ptp-cons", NULL, "ptp-nil",
                                  &atoms, context, error, error_cap))
        return false;
    if (atoms.len == 0u) {
        pptp_v1_set_error(error, error_cap, "%s must not be empty", context);
        free(atoms.data);
        return false;
    }
    indexes = malloc(sizeof(*indexes) * (size_t)atoms.len);
    if (!indexes) {
        pptp_v1_set_error(error, error_cap, "out of memory copying %s",
                          context);
        free(atoms.data);
        return false;
    }
    for (index = 0u; index < atoms.len; index++) {
        if (!pptp_v1_atom_u32(atoms.data[index], &indexes[index], context,
                              error, error_cap)) {
            free(indexes);
            free(atoms.data);
            return false;
        }
    }
    *out_indexes = indexes;
    *out_len = atoms.len;
    free(atoms.data);
    return true;
}

static bool pptp_v1_validate_permutation(
    const uint32_t *indexes,
    uint32_t len,
    const char *context,
    char *error,
    size_t error_cap) {
    uint8_t *seen = calloc(len, sizeof(*seen));
    uint32_t index;

    if (!seen) {
        pptp_v1_set_error(error, error_cap,
                          "out of memory validating %s", context);
        return false;
    }
    for (index = 0u; index < len; index++) {
        if (indexes[index] >= len || seen[indexes[index]]) {
            pptp_v1_set_error(error, error_cap,
                              "%s is not a permutation", context);
            free(seen);
            return false;
        }
        seen[indexes[index]] = 1u;
    }
    free(seen);
    return true;
}

static bool pptp_v1_parse_rule(
    const Atom *atom,
    PPTPV1Rule *rule,
    char *error,
    size_t error_cap) {
    const Atom *const *items;
    uint32_t index_len = 0u;

    memset(rule, 0, sizeof(*rule));
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len == 0u) {
        pptp_v1_set_error(error, error_cap,
                          "projection rule is not an expression");
        return false;
    }
    items = (const Atom *const *)atom->expr.elems;
    if (pptp_v1_expr_head(atom, "ptp-symbol-rule", 3u)) {
        rule->kind = PPTP_V1_RULE_SYMBOL;
        rule->source = pptp_v1_copy_symbol(
            items[1], "symbol-rule source", error, error_cap);
        rule->target = pptp_v1_copy_symbol(
            items[2], "symbol-rule target", error, error_cap);
        rule->result_sort = pptp_v1_copy_symbol(
            items[3], "symbol-rule result sort", error, error_cap);
    } else if (pptp_v1_expr_head(atom, "ptp-fixed-rule", 5u)) {
        rule->kind = PPTP_V1_RULE_FIXED;
        rule->source = pptp_v1_copy_symbol(
            items[1], "fixed-rule source", error, error_cap);
        rule->target = pptp_v1_copy_symbol(
            items[2], "fixed-rule target", error, error_cap);
        rule->result_sort = pptp_v1_copy_symbol(
            items[3], "fixed-rule result sort", error, error_cap);
        if (!pptp_v1_parse_indexes(items[4], &rule->indexes, &index_len,
                                   "fixed-rule indexes", error, error_cap) ||
            !pptp_v1_copy_symbol_list(
                items[5], &rule->child_sorts, &rule->child_len,
                "fixed-rule child sorts", error, error_cap))
            goto fail;
        if (index_len != rule->child_len ||
            !pptp_v1_validate_permutation(
                rule->indexes, index_len, "fixed-rule indexes",
                error, error_cap))
            goto fail;
    } else if (pptp_v1_expr_head(atom, "ptp-quote-rule", 5u)) {
        rule->kind = PPTP_V1_RULE_QUOTE;
        rule->source = pptp_v1_copy_symbol(
            items[1], "quote-rule source", error, error_cap);
        rule->target = pptp_v1_copy_symbol(
            items[2], "quote-rule target", error, error_cap);
        rule->result_sort = pptp_v1_copy_symbol(
            items[3], "quote-rule result sort", error, error_cap);
        rule->element_sort = pptp_v1_copy_symbol(
            items[5], "quote-rule child sort", error, error_cap);
        if (!pptp_v1_atom_u32(items[4], &rule->quote_index,
                              "quote-rule index", error, error_cap) ||
            rule->quote_index != 0u) {
            if (rule->quote_index != 0u)
                pptp_v1_set_error(error, error_cap,
                                  "quote-rule index must be zero");
            goto fail;
        }
    } else if (pptp_v1_expr_head(atom, "ptp-monoid-rule", 5u)) {
        rule->kind = PPTP_V1_RULE_MONOID;
        rule->source = pptp_v1_copy_symbol(
            items[1], "monoid-rule source", error, error_cap);
        rule->target = pptp_v1_copy_symbol(
            items[2], "monoid-rule target", error, error_cap);
        rule->result_sort = pptp_v1_copy_symbol(
            items[3], "monoid-rule result sort", error, error_cap);
        rule->unit = pptp_v1_copy_symbol(
            items[4], "monoid-rule unit", error, error_cap);
        rule->element_sort = pptp_v1_copy_symbol(
            items[5], "monoid-rule element sort", error, error_cap);
        if (rule->result_sort && rule->element_sort &&
            strcmp(rule->result_sort, rule->element_sort) != 0) {
            pptp_v1_set_error(
                error, error_cap,
                "monoid result and element sorts must agree");
            goto fail;
        }
    } else if (pptp_v1_expr_head(atom, "ptp-binder-rule", 9u)) {
        uint32_t indexes[3];

        rule->kind = PPTP_V1_RULE_BINDER;
        rule->source = pptp_v1_copy_symbol(
            items[1], "binder-rule source", error, error_cap);
        rule->target = pptp_v1_copy_symbol(
            items[2], "binder-rule target", error, error_cap);
        rule->result_sort = pptp_v1_copy_symbol(
            items[3], "binder-rule result sort", error, error_cap);
        if (!pptp_v1_atom_u32(items[4], &rule->channel_index,
                              "binder channel index", error, error_cap) ||
            !pptp_v1_atom_u32(items[5], &rule->binder_index,
                              "binder variable index", error, error_cap) ||
            !pptp_v1_atom_u32(items[6], &rule->body_index,
                              "binder body index", error, error_cap))
            goto fail;
        indexes[0] = rule->channel_index;
        indexes[1] = rule->binder_index;
        indexes[2] = rule->body_index;
        if (!pptp_v1_validate_permutation(
                indexes, 3u, "binder source indexes", error, error_cap))
            goto fail;
        rule->channel_sort = pptp_v1_copy_symbol(
            items[7], "binder channel sort", error, error_cap);
        rule->binder_sort = pptp_v1_copy_symbol(
            items[8], "binder variable sort", error, error_cap);
        rule->body_sort = pptp_v1_copy_symbol(
            items[9], "binder body sort", error, error_cap);
    } else {
        pptp_v1_set_error(error, error_cap,
                          "unknown or malformed projection rule");
        return false;
    }
    if (!rule->source || !rule->target || !rule->result_sort ||
        (rule->kind == PPTP_V1_RULE_QUOTE && !rule->element_sort) ||
        (rule->kind == PPTP_V1_RULE_MONOID &&
         (!rule->unit || !rule->element_sort)) ||
        (rule->kind == PPTP_V1_RULE_BINDER &&
         (!rule->channel_sort || !rule->binder_sort || !rule->body_sort)))
        goto fail;
    return true;

fail:
    pptp_v1_rule_free(rule);
    return false;
}

void ppterm_projection_v1_plan_init(PPTermProjectionV1Plan *plan) {
    if (plan)
        plan->implementation = NULL;
}

void ppterm_projection_v1_plan_free(PPTermProjectionV1Plan *plan) {
    if (!plan)
        return;
    pptp_v1_impl_free((PPTPV1PlanImpl *)plan->implementation);
    plan->implementation = NULL;
}

bool ppterm_projection_v1_plan_load(
    PPTermProjectionV1Plan *plan,
    const Atom *compiled_plan,
    char *error_buf,
    size_t error_buf_size) {
    PPTPV1PlanImpl *impl = NULL;
    PPTPV1AtomVec rules;
    uint32_t index;

    if (error_buf && error_buf_size)
        error_buf[0] = '\0';
    if (!plan || !pptp_v1_expr_head(compiled_plan, "ptp-plan-v1", 12u)) {
        pptp_v1_set_error(error_buf, error_buf_size,
                          "expected a ptp-plan-v1 with twelve fields");
        return false;
    }
    impl = calloc(1u, sizeof(*impl));
    if (!impl) {
        pptp_v1_set_error(error_buf, error_buf_size,
                          "out of memory allocating projection plan");
        return false;
    }
#define PPTP_COPY_PLAN_FIELD(field, position, label)                       \
    do {                                                                  \
        impl->field = pptp_v1_copy_symbol(                                \
            compiled_plan->expr.elems[position], label,                   \
            error_buf, error_buf_size);                                   \
        if (!impl->field)                                                 \
            goto fail;                                                    \
    } while (0)
    PPTP_COPY_PLAN_FIELD(node_label, 1u, "node label");
    PPTP_COPY_PLAN_FIELD(document_label, 2u, "document label");
    PPTP_COPY_PLAN_FIELD(document_sort, 3u, "document sort");
    PPTP_COPY_PLAN_FIELD(variable_sort, 4u, "variable sort");
    PPTP_COPY_PLAN_FIELD(pair_label, 5u, "pair label");
    PPTP_COPY_PLAN_FIELD(cons_label, 6u, "cons label");
    PPTP_COPY_PLAN_FIELD(nil_label, 7u, "nil label");
    PPTP_COPY_PLAN_FIELD(codepoint_label, 8u, "codepoint label");
    PPTP_COPY_PLAN_FIELD(binder_label, 11u, "binder wrapper label");
#undef PPTP_COPY_PLAN_FIELD
    if (strcmp(impl->document_sort, impl->variable_sort) == 0) {
        pptp_v1_set_error(error_buf, error_buf_size,
                          "document and variable sorts must differ");
        goto fail;
    }
    if (!pptp_v1_copy_symbol_list(
            compiled_plan->expr.elems[9], &impl->text_wrappers,
            &impl->text_wrapper_len, "text wrappers",
            error_buf, error_buf_size) ||
        !pptp_v1_copy_symbol_list(
            compiled_plan->expr.elems[10], &impl->variable_labels,
            &impl->variable_label_len, "variable labels",
            error_buf, error_buf_size))
        goto fail;
    if (!pptp_v1_string_in(impl->text_wrappers,
                           impl->text_wrapper_len,
                           impl->binder_label)) {
        pptp_v1_set_error(error_buf, error_buf_size,
                          "binder label is not a text wrapper");
        goto fail;
    }
    if (pptp_v1_string_in(impl->variable_labels,
                          impl->variable_label_len,
                          impl->binder_label)) {
        pptp_v1_set_error(error_buf, error_buf_size,
                          "binder label overlaps a variable label");
        goto fail;
    }
    for (index = 0u; index < impl->variable_label_len; index++) {
        if (!pptp_v1_string_in(impl->text_wrappers,
                               impl->text_wrapper_len,
                               impl->variable_labels[index])) {
            pptp_v1_set_error(error_buf, error_buf_size,
                              "variable label is not a text wrapper");
            goto fail;
        }
    }
    if (!pptp_v1_decode_atom_list(
            compiled_plan->expr.elems[12], "ptp-cons", NULL, "ptp-nil",
            &rules, "projection rules", error_buf, error_buf_size))
        goto fail;
    if (rules.len == 0u) {
        pptp_v1_set_error(error_buf, error_buf_size,
                          "projection plan has no rules");
        free(rules.data);
        goto fail;
    }
    impl->rules = calloc(rules.len, sizeof(*impl->rules));
    if (!impl->rules) {
        pptp_v1_set_error(error_buf, error_buf_size,
                          "out of memory allocating projection rules");
        free(rules.data);
        goto fail;
    }
    impl->rule_len = rules.len;
    for (index = 0u; index < rules.len; index++) {
        uint32_t prior;

        if (!pptp_v1_parse_rule(
                rules.data[index], &impl->rules[index],
                error_buf, error_buf_size)) {
            free(rules.data);
            goto fail;
        }
        if (pptp_v1_string_in(
                impl->variable_labels, impl->variable_label_len,
                impl->rules[index].source)) {
            pptp_v1_set_error(error_buf, error_buf_size,
                              "projection rule overlaps a variable label");
            free(rules.data);
            goto fail;
        }
        if (impl->rules[index].kind == PPTP_V1_RULE_BINDER &&
            strcmp(impl->rules[index].binder_sort,
                   impl->variable_sort) != 0) {
            pptp_v1_set_error(
                error_buf, error_buf_size,
                "binder sort differs from the plan variable sort");
            free(rules.data);
            goto fail;
        }
        for (prior = 0u; prior < index; prior++) {
            if (strcmp(impl->rules[prior].source,
                       impl->rules[index].source) == 0) {
                pptp_v1_set_error(error_buf, error_buf_size,
                                  "duplicate projection source label");
                free(rules.data);
                goto fail;
            }
        }
    }
    free(rules.data);
    pptp_v1_impl_free((PPTPV1PlanImpl *)plan->implementation);
    plan->implementation = impl;
    return true;

fail:
    pptp_v1_impl_free(impl);
    return false;
}

static bool pptp_v1_bytes_reserve(PPTPV1ByteVec *vec, size_t added) {
    size_t required;
    size_t cap;
    char *next;

    if (added > SIZE_MAX - vec->len - 1u)
        return false;
    required = vec->len + added + 1u;
    if (required > PPTP_V1_MAX_TEXT_BYTES)
        return false;
    if (required <= vec->cap)
        return true;
    cap = vec->cap ? vec->cap : 64u;
    while (cap < required) {
        if (cap > PPTP_V1_MAX_TEXT_BYTES / 2u) {
            cap = PPTP_V1_MAX_TEXT_BYTES;
            break;
        }
        cap *= 2u;
    }
    next = realloc(vec->data, cap);
    if (!next)
        return false;
    vec->data = next;
    vec->cap = cap;
    return true;
}

static bool pptp_v1_bytes_append(
    PPTPV1ByteVec *vec,
    const char *bytes,
    size_t len) {
    if (!pptp_v1_bytes_reserve(vec, len))
        return false;
    if (len)
        memcpy(vec->data + vec->len, bytes, len);
    vec->len += len;
    vec->data[vec->len] = '\0';
    return true;
}

static bool pptp_v1_bytes_field(
    PPTPV1ByteVec *vec,
    const char *text) {
    char length[32];
    int used;
    size_t len = text ? strlen(text) : 0u;

    used = snprintf(length, sizeof(length), "%zu:", len);
    return used > 0 && (size_t)used < sizeof(length) &&
        pptp_v1_bytes_append(vec, length, (size_t)used) &&
        pptp_v1_bytes_append(vec, text ? text : "", len);
}

static bool pptp_v1_bytes_u32(PPTPV1ByteVec *vec, uint32_t value) {
    char number[32];
    int used = snprintf(number, sizeof(number), "%u", value);

    return used > 0 && (size_t)used < sizeof(number) &&
        pptp_v1_bytes_append(vec, number, (size_t)used);
}

static void pptp_v1_scoped_free(PPTPV1ScopedTerm *term) {
    uint32_t index;

    if (!term)
        return;
    for (index = 0u; index < term->child_len; index++)
        pptp_v1_scoped_free(term->children[index]);
    free(term->children);
    free(term->sort);
    free(term->head);
    free(term->spelling);
    free(term->key);
    free(term);
}

static char *pptp_v1_scoped_key(const PPTPV1ScopedTerm *term) {
    PPTPV1ByteVec key;
    uint32_t index;
    char kind;

    memset(&key, 0, sizeof(key));
    switch (term->kind) {
    case PPTP_V1_SCOPED_BOUND: kind = 'B'; break;
    case PPTP_V1_SCOPED_FREE: kind = 'F'; break;
    case PPTP_V1_SCOPED_SYMBOL: kind = 'S'; break;
    case PPTP_V1_SCOPED_APPLY: kind = 'A'; break;
    case PPTP_V1_SCOPED_QUOTE: kind = 'Q'; break;
    case PPTP_V1_SCOPED_MONOID: kind = 'M'; break;
    case PPTP_V1_SCOPED_BINDER: kind = 'D'; break;
    default: return NULL;
    }
    if (!pptp_v1_bytes_append(&key, &kind, 1u) ||
        !pptp_v1_bytes_field(&key, term->sort) ||
        !pptp_v1_bytes_field(&key, term->head))
        goto fail;
    if (term->kind == PPTP_V1_SCOPED_BOUND) {
        if (!pptp_v1_bytes_u32(&key, term->bound_index))
            goto fail;
    } else if (term->kind == PPTP_V1_SCOPED_FREE) {
        if (!pptp_v1_bytes_field(&key, term->spelling))
            goto fail;
    }
    if (!pptp_v1_bytes_append(&key, "[", 1u) ||
        !pptp_v1_bytes_u32(&key, term->child_len) ||
        !pptp_v1_bytes_append(&key, "]", 1u))
        goto fail;
    for (index = 0u; index < term->child_len; index++) {
        if (!pptp_v1_bytes_field(&key, term->children[index]->key))
            goto fail;
    }
    if (!key.data) {
        key.data = pptp_v1_strdup("");
        if (!key.data)
            return NULL;
    }
    return key.data;

fail:
    free(key.data);
    return NULL;
}

/*
 * This constructor always consumes spelling and children, including on
 * failure.  Callers therefore have a single ownership path.
 */
static PPTPV1ScopedTerm *pptp_v1_scoped_make(
    PPTPV1ScopedKind kind,
    const char *sort,
    const char *head,
    char *spelling,
    uint32_t bound_index,
    PPTPV1ScopedTerm **children,
    uint32_t child_len,
    PPTPV1BuildContext *context) {
    PPTPV1ScopedTerm *term = calloc(1u, sizeof(*term));

    if (!term)
        goto fail;
    term->kind = kind;
    term->sort = pptp_v1_strdup(sort);
    term->head = head ? pptp_v1_strdup(head) : NULL;
    term->spelling = spelling;
    spelling = NULL;
    term->bound_index = bound_index;
    term->children = children;
    children = NULL;
    term->child_len = child_len;
    if (!term->sort || (head && !term->head))
        goto fail;
    term->key = pptp_v1_scoped_key(term);
    if (!term->key)
        goto fail;
    return term;

fail:
    if (term)
        pptp_v1_scoped_free(term);
    else {
        uint32_t index;
        free(spelling);
        for (index = 0u; index < child_len; index++)
            pptp_v1_scoped_free(children[index]);
        free(children);
    }
    pptp_v1_set_error(context->error, context->error_cap,
                      "out of memory constructing scoped term");
    return NULL;
}

static bool pptp_v1_utf8_append(
    PPTPV1ByteVec *text,
    uint32_t codepoint) {
    char bytes[4];
    size_t len;

    if (codepoint == 0u || codepoint > 0x10ffffu ||
        (codepoint >= 0xd800u && codepoint <= 0xdfffu))
        return false;
    if (codepoint <= 0x7fu) {
        bytes[0] = (char)codepoint;
        len = 1u;
    } else if (codepoint <= 0x7ffu) {
        bytes[0] = (char)(0xc0u | (codepoint >> 6));
        bytes[1] = (char)(0x80u | (codepoint & 0x3fu));
        len = 2u;
    } else if (codepoint <= 0xffffu) {
        bytes[0] = (char)(0xe0u | (codepoint >> 12));
        bytes[1] = (char)(0x80u | ((codepoint >> 6) & 0x3fu));
        bytes[2] = (char)(0x80u | (codepoint & 0x3fu));
        len = 3u;
    } else {
        bytes[0] = (char)(0xf0u | (codepoint >> 18));
        bytes[1] = (char)(0x80u | ((codepoint >> 12) & 0x3fu));
        bytes[2] = (char)(0x80u | ((codepoint >> 6) & 0x3fu));
        bytes[3] = (char)(0x80u | (codepoint & 0x3fu));
        len = 4u;
    }
    return pptp_v1_bytes_append(text, bytes, len);
}

static bool pptp_v1_decode_text_step(
    PPTPV1BuildContext *context,
    const Atom *term,
    PPTPV1ByteVec *text,
    uint32_t depth) {
    const PPTPV1PlanImpl *plan = context->plan;
    const char *wrapper;
    uint32_t codepoint;

    if (depth > context->depth_limit) {
        pptp_v1_set_error(context->error, context->error_cap,
                          "text projection exceeded the depth limit");
        return false;
    }
    if (atom_is_symbol((Atom *)term, plan->nil_label))
        return true;
    if (pptp_v1_expr_head(term, plan->codepoint_label, 1u)) {
        if (!pptp_v1_atom_u32(term->expr.elems[1], &codepoint,
                              "text codepoint",
                              context->error, context->error_cap) ||
            !pptp_v1_utf8_append(text, codepoint)) {
            if (!context->error || !context->error[0])
                pptp_v1_set_error(context->error, context->error_cap,
                                  "invalid Unicode scalar in text");
            return false;
        }
        return true;
    }
    if (term && term->kind == ATOM_EXPR && term->expr.len == 3u &&
        atom_is_symbol(term->expr.elems[0], plan->node_label)) {
        wrapper = pptp_v1_symbol_name(term->expr.elems[1]);
        if (!wrapper ||
            !pptp_v1_string_in(plan->text_wrappers,
                               plan->text_wrapper_len, wrapper)) {
            pptp_v1_set_error(context->error, context->error_cap,
                              "unknown text wrapper");
            return false;
        }
        return pptp_v1_decode_text_step(
            context, term->expr.elems[2], text, depth + 1u);
    }
    if (term && term->kind == ATOM_EXPR && term->expr.len == 3u &&
        (atom_is_symbol(term->expr.elems[0], plan->pair_label) ||
         atom_is_symbol(term->expr.elems[0], plan->cons_label))) {
        return pptp_v1_decode_text_step(
                   context, term->expr.elems[1], text, depth + 1u) &&
            pptp_v1_decode_text_step(
                   context, term->expr.elems[2], text, depth + 1u);
    }
    pptp_v1_set_error(context->error, context->error_cap,
                      "malformed codepoint text spine");
    return false;
}

static char *pptp_v1_decode_text(
    PPTPV1BuildContext *context,
    const Atom *term,
    uint32_t depth) {
    PPTPV1ByteVec text;

    memset(&text, 0, sizeof(text));
    if (!pptp_v1_decode_text_step(context, term, &text, depth)) {
        free(text.data);
        return NULL;
    }
    if (!text.data) {
        text.data = pptp_v1_strdup("");
        if (!text.data)
            pptp_v1_set_error(context->error, context->error_cap,
                              "out of memory decoding text");
    }
    return text.data;
}

static const PPTPV1Rule *pptp_v1_find_rule(
    const PPTPV1PlanImpl *plan,
    const char *source) {
    uint32_t index;

    for (index = 0u; index < plan->rule_len; index++) {
        if (strcmp(plan->rules[index].source, source) == 0)
            return &plan->rules[index];
    }
    return NULL;
}

static bool pptp_v1_unpack_fixed(
    PPTPV1BuildContext *context,
    const Atom *payload,
    uint32_t count,
    Atom ***out) {
    Atom **values;
    const Atom *cursor = payload;
    uint32_t index;

    if (count == 0u) {
        pptp_v1_set_error(context->error, context->error_cap,
                          "zero-field fixed projection is invalid");
        return false;
    }
    values = malloc(sizeof(*values) * (size_t)count);
    if (!values) {
        pptp_v1_set_error(context->error, context->error_cap,
                          "out of memory unpacking fixed projection");
        return false;
    }
    for (index = 0u; index + 1u < count; index++) {
        if (!cursor || cursor->kind != ATOM_EXPR ||
            cursor->expr.len != 3u ||
            !(atom_is_symbol(cursor->expr.elems[0],
                             context->plan->pair_label) ||
              atom_is_symbol(cursor->expr.elems[0],
                             context->plan->cons_label))) {
            pptp_v1_set_error(context->error, context->error_cap,
                              "malformed fixed-field spine");
            free(values);
            return false;
        }
        values[index] = cursor->expr.elems[1];
        cursor = cursor->expr.elems[2];
    }
    values[count - 1u] = (Atom *)cursor;
    *out = values;
    return true;
}

static PPTPV1ScopedTerm *pptp_v1_build_node(
    PPTPV1BuildContext *context,
    const Atom *term,
    const PPTPV1ScopeFrame *scope,
    const char *expected_sort,
    uint32_t depth);

static PPTPV1ScopedTerm *pptp_v1_build_variable(
    PPTPV1BuildContext *context,
    const Atom *term,
    const PPTPV1ScopeFrame *scope,
    uint32_t depth) {
    const PPTPV1ScopeFrame *frame;
    char *spelling = pptp_v1_decode_text(context, term, depth + 1u);
    uint32_t index = 0u;

    if (!spelling)
        return NULL;
    if (spelling[0] == '\0') {
        pptp_v1_set_error(context->error, context->error_cap,
                          "variable spelling must not be empty");
        free(spelling);
        return NULL;
    }
    for (frame = scope; frame; frame = frame->parent, index++) {
        if (strcmp(frame->spelling, spelling) == 0) {
            free(spelling);
            return pptp_v1_scoped_make(
                PPTP_V1_SCOPED_BOUND, context->plan->variable_sort,
                NULL, NULL, index, NULL, 0u, context);
        }
    }
    return pptp_v1_scoped_make(
        PPTP_V1_SCOPED_FREE, context->plan->variable_sort,
        NULL, spelling, 0u, NULL, 0u, context);
}

static bool pptp_v1_scoped_vec_push(
    PPTPV1ScopedVec *vec,
    PPTPV1ScopedTerm *term) {
    PPTPV1ScopedTerm **next;
    uint32_t cap;

    if (vec->len == vec->cap) {
        cap = vec->cap ? vec->cap * 2u : 8u;
        if (cap < vec->cap || cap > PPTP_V1_MAX_PLAN_ITEMS)
            cap = PPTP_V1_MAX_PLAN_ITEMS;
        if (cap == vec->cap)
            return false;
        next = realloc(vec->data, sizeof(*next) * (size_t)cap);
        if (!next)
            return false;
        vec->data = next;
        vec->cap = cap;
    }
    vec->data[vec->len++] = term;
    return true;
}

static int pptp_v1_compare_scoped(const void *left, const void *right) {
    const PPTPV1ScopedTerm *const *lhs = left;
    const PPTPV1ScopedTerm *const *rhs = right;
    return strcmp((*lhs)->key, (*rhs)->key);
}

static PPTPV1ScopedTerm *pptp_v1_build_monoid(
    PPTPV1BuildContext *context,
    const PPTPV1Rule *rule,
    const Atom *payload,
    const PPTPV1ScopeFrame *scope,
    uint32_t depth) {
    PPTPV1AtomVec source;
    PPTPV1ScopedVec normalized;
    uint32_t index;

    memset(&normalized, 0, sizeof(normalized));
    if (!pptp_v1_decode_atom_list(
            payload, context->plan->pair_label,
            context->plan->cons_label, context->plan->nil_label,
            &source, "collection payload",
            context->error, context->error_cap))
        return NULL;
    for (index = 0u; index < source.len; index++) {
        PPTPV1ScopedTerm *child = pptp_v1_build_node(
            context, source.data[index], scope,
            rule->element_sort, depth + 1u);
        uint32_t nested;

        if (!child)
            goto fail;
        if (child->kind == PPTP_V1_SCOPED_SYMBOL &&
            child->head && strcmp(child->head, rule->unit) == 0) {
            pptp_v1_scoped_free(child);
            continue;
        }
        if (child->kind == PPTP_V1_SCOPED_MONOID &&
            child->head && strcmp(child->head, rule->target) == 0) {
            for (nested = 0u; nested < child->child_len; nested++) {
                if (!pptp_v1_scoped_vec_push(
                        &normalized, child->children[nested])) {
                    pptp_v1_set_error(
                        context->error, context->error_cap,
                        "collection projection exceeds its item limit");
                    goto child_fail;
                }
                child->children[nested] = NULL;
            }
            pptp_v1_scoped_free(child);
            continue;
child_fail:
            pptp_v1_scoped_free(child);
            goto fail;
        }
        if (!pptp_v1_scoped_vec_push(&normalized, child)) {
            pptp_v1_set_error(context->error, context->error_cap,
                              "collection projection exceeds its item limit");
            pptp_v1_scoped_free(child);
            goto fail;
        }
    }
    free(source.data);
    if (normalized.len == 0u) {
        free(normalized.data);
        return pptp_v1_scoped_make(
            PPTP_V1_SCOPED_SYMBOL, rule->result_sort, rule->unit,
            NULL, 0u, NULL, 0u, context);
    }
    if (normalized.len == 1u) {
        PPTPV1ScopedTerm *only = normalized.data[0];
        free(normalized.data);
        return only;
    }
    qsort(normalized.data, normalized.len, sizeof(*normalized.data),
          pptp_v1_compare_scoped);
    return pptp_v1_scoped_make(
        PPTP_V1_SCOPED_MONOID, rule->result_sort, rule->target,
        NULL, 0u, normalized.data, normalized.len, context);

fail:
    for (index = 0u; index < normalized.len; index++)
        pptp_v1_scoped_free(normalized.data[index]);
    free(normalized.data);
    free(source.data);
    return NULL;
}

static PPTPV1ScopedTerm *pptp_v1_build_node(
    PPTPV1BuildContext *context,
    const Atom *term,
    const PPTPV1ScopeFrame *scope,
    const char *expected_sort,
    uint32_t depth) {
    const char *label;
    const Atom *payload;
    const PPTPV1Rule *rule;
    PPTPV1ScopedTerm *result = NULL;

    if (depth > context->depth_limit) {
        pptp_v1_set_error(context->error, context->error_cap,
                          "term projection exceeded the depth limit");
        return NULL;
    }
    if (!term || term->kind != ATOM_EXPR || term->expr.len != 3u ||
        !atom_is_symbol(term->expr.elems[0], context->plan->node_label) ||
        !(label = pptp_v1_symbol_name(term->expr.elems[1]))) {
        pptp_v1_set_error(context->error, context->error_cap,
                          "expected a typed neutral node");
        return NULL;
    }
    payload = term->expr.elems[2];
    if (pptp_v1_string_in(context->plan->variable_labels,
                          context->plan->variable_label_len, label)) {
        result = pptp_v1_build_variable(
            context, term, scope, depth + 1u);
        goto check_sort;
    }
    rule = pptp_v1_find_rule(context->plan, label);
    if (!rule) {
        pptp_v1_set_error(context->error, context->error_cap,
                          "neutral node has no projection rule");
        return NULL;
    }
    switch (rule->kind) {
    case PPTP_V1_RULE_SYMBOL:
        /* Concrete lexeme evidence is intentionally erased at a nullary
         * abstract constructor boundary.  It remains available in the
         * neutral semantic result and ParserPack provenance. */
        result = pptp_v1_scoped_make(
            PPTP_V1_SCOPED_SYMBOL, rule->result_sort, rule->target,
            NULL, 0u, NULL, 0u, context);
        break;
    case PPTP_V1_RULE_FIXED: {
        Atom **source = NULL;
        PPTPV1ScopedTerm **children = NULL;
        uint32_t index;

        if (!pptp_v1_unpack_fixed(
                context, payload, rule->child_len, &source))
            return NULL;
        children = calloc(rule->child_len, sizeof(*children));
        if (!children) {
            pptp_v1_set_error(context->error, context->error_cap,
                              "out of memory projecting fixed node");
            free(source);
            return NULL;
        }
        for (index = 0u; index < rule->child_len; index++) {
            children[index] = pptp_v1_build_node(
                context, source[rule->indexes[index]], scope,
                rule->child_sorts[index], depth + 1u);
            if (!children[index]) {
                uint32_t prior;
                for (prior = 0u; prior < index; prior++)
                    pptp_v1_scoped_free(children[prior]);
                free(children);
                free(source);
                return NULL;
            }
        }
        free(source);
        result = pptp_v1_scoped_make(
            PPTP_V1_SCOPED_APPLY, rule->result_sort, rule->target,
            NULL, 0u, children, rule->child_len, context);
        break;
    }
    case PPTP_V1_RULE_QUOTE: {
        PPTPV1ScopedTerm **children = calloc(1u, sizeof(*children));

        if (!children) {
            pptp_v1_set_error(context->error, context->error_cap,
                              "out of memory projecting quote node");
            return NULL;
        }
        children[0] = pptp_v1_build_node(
            context, payload, NULL, rule->element_sort, depth + 1u);
        if (!children[0]) {
            free(children);
            return NULL;
        }
        result = pptp_v1_scoped_make(
            PPTP_V1_SCOPED_QUOTE, rule->result_sort, rule->target,
            NULL, 0u, children, 1u, context);
        break;
    }
    case PPTP_V1_RULE_MONOID:
        result = pptp_v1_build_monoid(
            context, rule, payload, scope, depth + 1u);
        break;
    case PPTP_V1_RULE_BINDER: {
        Atom **source = NULL;
        PPTPV1ScopedTerm **children = NULL;
        PPTPV1ScopeFrame frame;
        char *binder_spelling;
        const Atom *binder_term;
        const char *binder_node_label;

        if (!pptp_v1_unpack_fixed(context, payload, 3u, &source))
            return NULL;
        binder_term = source[rule->binder_index];
        if (!binder_term || binder_term->kind != ATOM_EXPR ||
            binder_term->expr.len != 3u ||
            !atom_is_symbol(binder_term->expr.elems[0],
                            context->plan->node_label) ||
            !(binder_node_label =
                  pptp_v1_symbol_name(binder_term->expr.elems[1])) ||
            strcmp(binder_node_label, context->plan->binder_label) != 0) {
            pptp_v1_set_error(context->error, context->error_cap,
                              "binder field has the wrong text wrapper");
            free(source);
            return NULL;
        }
        binder_spelling = pptp_v1_decode_text(
            context, binder_term, depth + 1u);
        if (!binder_spelling) {
            free(source);
            return NULL;
        }
        if (binder_spelling[0] == '\0') {
            pptp_v1_set_error(context->error, context->error_cap,
                              "binder spelling must not be empty");
            free(binder_spelling);
            free(source);
            return NULL;
        }
        children = calloc(2u, sizeof(*children));
        if (!children) {
            pptp_v1_set_error(context->error, context->error_cap,
                              "out of memory projecting binder");
            free(binder_spelling);
            free(source);
            return NULL;
        }
        children[0] = pptp_v1_build_node(
            context, source[rule->channel_index], scope,
            rule->channel_sort, depth + 1u);
        if (!children[0]) {
            free(children);
            free(binder_spelling);
            free(source);
            return NULL;
        }
        frame.spelling = binder_spelling;
        frame.parent = scope;
        children[1] = pptp_v1_build_node(
            context, source[rule->body_index], &frame,
            rule->body_sort, depth + 1u);
        free(source);
        if (!children[1]) {
            pptp_v1_scoped_free(children[0]);
            free(children);
            free(binder_spelling);
            return NULL;
        }
        result = pptp_v1_scoped_make(
            PPTP_V1_SCOPED_BINDER, rule->result_sort, rule->target,
            binder_spelling, 0u, children, 2u, context);
        break;
    }
    default:
        pptp_v1_set_error(context->error, context->error_cap,
                          "invalid projection rule kind");
        return NULL;
    }

check_sort:
    if (result && expected_sort &&
        strcmp(result->sort, expected_sort) != 0) {
        pptp_v1_set_error(context->error, context->error_cap,
                          "neutral child has the wrong abstract sort");
        pptp_v1_scoped_free(result);
        return NULL;
    }
    return result;
}

static bool pptp_v1_material_push(
    PPTPV1MaterialBinding **values,
    uint32_t *len,
    uint32_t *cap,
    const char *spelling,
    Atom *variable) {
    PPTPV1MaterialBinding *next;
    uint32_t new_cap;

    if (*len == *cap) {
        new_cap = *cap ? *cap * 2u : 8u;
        if (new_cap < *cap || new_cap > PPTP_V1_MAX_PLAN_ITEMS)
            new_cap = PPTP_V1_MAX_PLAN_ITEMS;
        if (new_cap == *cap)
            return false;
        next = realloc(*values, sizeof(*next) * (size_t)new_cap);
        if (!next)
            return false;
        *values = next;
        *cap = new_cap;
    }
    (*values)[*len].spelling = spelling;
    (*values)[*len].variable = variable;
    (*len)++;
    return true;
}

static Atom *pptp_v1_material_term(
    PPTPV1MaterialContext *context,
    const PPTPV1ScopedTerm *term,
    uint32_t depth) {
    Atom *result = NULL;
    uint32_t index;

    if (depth > context->depth_limit) {
        pptp_v1_set_error(context->error, context->error_cap,
                          "Atom materialization exceeded the depth limit");
        return NULL;
    }
    switch (term->kind) {
    case PPTP_V1_SCOPED_BOUND:
        if (context->binder_base > context->binder_len ||
            term->bound_index >=
                context->binder_len - context->binder_base) {
            pptp_v1_set_error(context->error, context->error_cap,
                              "scoped bound index escaped its binder");
            return NULL;
        }
        return context->binders[
            context->binder_len - 1u - term->bound_index].variable;
    case PPTP_V1_SCOPED_FREE:
        for (index = 0u; index < context->free_name_len; index++) {
            if (strcmp(context->free_names[index].spelling,
                       term->spelling) == 0)
                return context->free_names[index].variable;
        }
        result = atom_var_with_id(
            context->arena, term->spelling, fresh_var_id());
        if (!result ||
            !pptp_v1_material_push(
                &context->free_names, &context->free_name_len,
                &context->free_name_cap, term->spelling, result)) {
            pptp_v1_set_error(context->error, context->error_cap,
                              "out of memory materializing free variable");
            return NULL;
        }
        return result;
    case PPTP_V1_SCOPED_SYMBOL:
        return atom_symbol(context->arena, term->head);
    case PPTP_V1_SCOPED_APPLY:
    case PPTP_V1_SCOPED_MONOID:
    case PPTP_V1_SCOPED_QUOTE: {
        Atom **elements;
        uint32_t saved_binder_len = context->binder_len;
        uint32_t saved_binder_base = context->binder_base;

        if (term->kind == PPTP_V1_SCOPED_QUOTE)
            context->binder_base = context->binder_len;
        elements = arena_alloc(
            context->arena,
            sizeof(*elements) * ((size_t)term->child_len + 1u));
        if (!elements) {
            pptp_v1_set_error(context->error, context->error_cap,
                              "out of memory materializing expression");
            context->binder_len = saved_binder_len;
            context->binder_base = saved_binder_base;
            return NULL;
        }
        elements[0] = atom_symbol(context->arena, term->head);
        for (index = 0u; index < term->child_len; index++) {
            elements[index + 1u] = pptp_v1_material_term(
                context, term->children[index], depth + 1u);
            if (!elements[index + 1u]) {
                context->binder_len = saved_binder_len;
                context->binder_base = saved_binder_base;
                return NULL;
            }
        }
        result = atom_expr(
            context->arena, elements, (CettaExprLen)term->child_len + 1u);
        context->binder_len = saved_binder_len;
        context->binder_base = saved_binder_base;
        return result;
    }
    case PPTP_V1_SCOPED_BINDER: {
        Atom **elements;
        Atom *variable;
        Atom *channel;
        Atom *body;
        uint32_t saved_binder_len = context->binder_len;

        channel = pptp_v1_material_term(
            context, term->children[0], depth + 1u);
        if (!channel)
            return NULL;
        variable = atom_var_with_id(
            context->arena, term->spelling, fresh_var_id());
        if (!variable ||
            !pptp_v1_material_push(
                &context->binders, &context->binder_len,
                &context->binder_cap, term->spelling, variable)) {
            pptp_v1_set_error(context->error, context->error_cap,
                              "out of memory materializing binder");
            context->binder_len = saved_binder_len;
            return NULL;
        }
        body = pptp_v1_material_term(
            context, term->children[1], depth + 1u);
        context->binder_len = saved_binder_len;
        if (!body)
            return NULL;
        elements = arena_alloc(context->arena, sizeof(*elements) * 4u);
        if (!elements) {
            pptp_v1_set_error(context->error, context->error_cap,
                              "out of memory materializing binder node");
            return NULL;
        }
        elements[0] = atom_symbol(context->arena, term->head);
        elements[1] = channel;
        elements[2] = variable;
        elements[3] = body;
        return atom_expr(context->arena, elements, 4u);
    }
    default:
        pptp_v1_set_error(context->error, context->error_cap,
                          "invalid scoped term kind");
        return NULL;
    }
}

bool ppterm_projection_v1_project_result(
    const PPTermProjectionV1Plan *plan,
    const Atom *semantic_result,
    Arena *output_arena,
    uint32_t depth_limit,
    Atom **out,
    char *error_buf,
    size_t error_buf_size) {
    const PPTPV1PlanImpl *impl;
    const Atom *document;
    PPTPV1BuildContext build;
    PPTPV1MaterialContext material;
    PPTPV1ScopedTerm *scoped = NULL;
    ArenaMark mark;
    Atom *result;

    if (error_buf && error_buf_size)
        error_buf[0] = '\0';
    if (out)
        *out = NULL;
    if (!plan || !(impl = plan->implementation) || !semantic_result ||
        !output_arena || !out || depth_limit == 0u) {
        pptp_v1_set_error(error_buf, error_buf_size,
                          "invalid term-projection arguments");
        return false;
    }
    if (!pptp_v1_expr_head(semantic_result, "result", 2u) ||
        !atom_is_symbol(semantic_result->expr.elems[2], impl->nil_label)) {
        pptp_v1_set_error(error_buf, error_buf_size,
                          "malformed or trailing semantic result");
        return false;
    }
    document = semantic_result->expr.elems[1];
    if (!document || document->kind != ATOM_EXPR ||
        document->expr.len != 3u ||
        !atom_is_symbol(document->expr.elems[0], impl->node_label) ||
        !atom_is_symbol(document->expr.elems[1], impl->document_label)) {
        pptp_v1_set_error(error_buf, error_buf_size,
                          "semantic result lacks its document node");
        return false;
    }
    memset(&build, 0, sizeof(build));
    build.plan = impl;
    build.depth_limit = depth_limit;
    build.error = error_buf;
    build.error_cap = error_buf_size;
    scoped = pptp_v1_build_node(
        &build, document->expr.elems[2], NULL,
        impl->document_sort, 1u);
    if (!scoped)
        return false;

    memset(&material, 0, sizeof(material));
    material.arena = output_arena;
    material.depth_limit = depth_limit;
    material.error = error_buf;
    material.error_cap = error_buf_size;
    mark = arena_mark(output_arena);
    result = pptp_v1_material_term(&material, scoped, 1u);
    free(material.binders);
    free(material.free_names);
    pptp_v1_scoped_free(scoped);
    if (!result) {
        arena_reset(output_arena, mark);
        return false;
    }
    *out = result;
    return true;
}
