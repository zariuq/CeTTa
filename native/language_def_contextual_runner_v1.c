#include "language_def_contextual_runner_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const CettaLdTextV1 *name;
    const CettaLdPatternV1 *value;
} CrBindingV1;

typedef struct {
    CrBindingV1 *items;
    uint32_t len;
    uint32_t capacity;
} CrBindingsV1;

typedef struct {
    CrBindingsV1 bindings;
    CettaLdCrV1PremiseEvidence *premises;
    uint32_t premise_len;
    uint32_t premise_capacity;
} CrBranchV1;

typedef struct {
    const CettaLdCrV1Program *program;
    const CettaLdCrV1RelationProvider *provider;
    uint64_t remaining_work;
    CettaLdCrV1Status status;
    char *error_buf;
    size_t error_buf_size;
    bool context_fuel_exhausted;
} CrContextV1;

typedef enum {
    CR_MATCH_FAILURE_V1 = -1,
    CR_MATCH_MISS_V1 = 0,
    CR_MATCH_SUCCESS_V1 = 1
} CrMatchResultV1;

static bool cr_rewrite(CrContextV1 *context, uint32_t fuel,
                       const CettaLdPatternV1 *source,
                       CettaLdCrV1Results *results);

static bool cr_text_equal(const CettaLdTextV1 *left,
                          const CettaLdTextV1 *right) {
    if (left == right)
        return true;
    if (!left || !right || left->len != right->len)
        return false;
    return left->len == 0u ||
        (left->bytes && right->bytes &&
         memcmp(left->bytes, right->bytes, left->len) == 0);
}

static bool cr_text_is(const CettaLdTextV1 *text, const char *value) {
    size_t len;
    if (!text || !value)
        return false;
    len = strlen(value);
    return len == (size_t)text->len &&
        (len == 0u || (text->bytes && memcmp(text->bytes, value, len) == 0));
}

static bool cr_optional_text_equal(const CettaLdOptionalTextV1 *left,
                                   const CettaLdOptionalTextV1 *right) {
    return left && right && left->present == right->present &&
        (!left->present || cr_text_equal(&left->value, &right->value));
}

static bool cr_text_list_equal(const CettaLdTextListV1 *left,
                               const CettaLdTextListV1 *right) {
    uint32_t index;
    if (!left || !right || left->len != right->len)
        return false;
    for (index = 0u; index < left->len; index++) {
        if (!cr_text_equal(&left->items[index], &right->items[index]))
            return false;
    }
    return true;
}

bool cetta_ld_cr_v1_pattern_equal(const CettaLdPatternV1 *left,
                                  const CettaLdPatternV1 *right) {
    uint32_t index;
    if (left == right)
        return true;
    if (!left || !right || left->kind != right->kind)
        return false;
    switch (left->kind) {
    case CETTA_LD_PATTERN_BVAR_V1:
        return left->as.bvar_decimal && right->as.bvar_decimal &&
            strcmp(left->as.bvar_decimal, right->as.bvar_decimal) == 0;
    case CETTA_LD_PATTERN_FVAR_V1:
        return cr_text_equal(&left->as.fvar, &right->as.fvar);
    case CETTA_LD_PATTERN_APPLY_V1:
        if (!cr_text_equal(&left->as.apply.head, &right->as.apply.head) ||
            left->as.apply.arguments.len != right->as.apply.arguments.len)
            return false;
        for (index = 0u; index < left->as.apply.arguments.len; index++) {
            if (!cetta_ld_cr_v1_pattern_equal(
                    &left->as.apply.arguments.items[index],
                    &right->as.apply.arguments.items[index]))
                return false;
        }
        return true;
    case CETTA_LD_PATTERN_LAMBDA_V1:
        return cr_optional_text_equal(&left->as.lambda.binder,
                                      &right->as.lambda.binder) &&
            cetta_ld_cr_v1_pattern_equal(left->as.lambda.body,
                                         right->as.lambda.body);
    case CETTA_LD_PATTERN_MULTI_LAMBDA_V1:
        return left->as.multi_lambda.arity_decimal &&
            right->as.multi_lambda.arity_decimal &&
            strcmp(left->as.multi_lambda.arity_decimal,
                   right->as.multi_lambda.arity_decimal) == 0 &&
            cr_text_list_equal(&left->as.multi_lambda.binders,
                               &right->as.multi_lambda.binders) &&
            cetta_ld_cr_v1_pattern_equal(left->as.multi_lambda.body,
                                         right->as.multi_lambda.body);
    case CETTA_LD_PATTERN_SUBST_V1:
        return cetta_ld_cr_v1_pattern_equal(left->as.subst.body,
                                            right->as.subst.body) &&
            cetta_ld_cr_v1_pattern_equal(left->as.subst.replacement,
                                         right->as.subst.replacement);
    case CETTA_LD_PATTERN_COLLECTION_V1:
        if (left->as.collection.collection_type !=
                right->as.collection.collection_type ||
            left->as.collection.elements.len !=
                right->as.collection.elements.len ||
            !cr_optional_text_equal(&left->as.collection.rest,
                                    &right->as.collection.rest))
            return false;
        for (index = 0u; index < left->as.collection.elements.len; index++) {
            if (!cetta_ld_cr_v1_pattern_equal(
                    &left->as.collection.elements.items[index],
                    &right->as.collection.elements.items[index]))
                return false;
        }
        return true;
    }
    return false;
}

static void cr_set_error(CrContextV1 *context, const char *format, ...) {
    va_list arguments;
    if (!context || !context->error_buf || context->error_buf_size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(context->error_buf, context->error_buf_size,
                    format, arguments);
    va_end(arguments);
}

static bool cr_fail(CrContextV1 *context, CettaLdCrV1Status status,
                    const char *message) {
    if (context) {
        context->status = status;
        if (message)
            cr_set_error(context, "%s", message);
    }
    return false;
}

static bool cr_take_work(CrContextV1 *context) {
    if (!context)
        return false;
    if (context->remaining_work == 0u)
        return cr_fail(context, CETTA_LD_CR_V1_WORK_LIMIT,
                       "contextual LanguageDef execution exhausted its work limit");
    context->remaining_work--;
    return true;
}

static bool cr_text_clone(CrContextV1 *context, CettaLdTextV1 *out,
                          const CettaLdTextV1 *source) {
    CettaLdTextV1 result = {0};
    if (!out || !source || (source->len > 0u && !source->bytes))
        return cr_fail(context, CETTA_LD_CR_V1_INTERNAL_FAILURE,
                       "malformed text in contextual LanguageDef execution");
    if (source->len > 0u) {
        result.bytes = malloc(source->len);
        if (!result.bytes)
            return cr_fail(context, CETTA_LD_CR_V1_ALLOCATION_FAILURE,
                           "could not allocate contextual Pattern text");
        memcpy(result.bytes, source->bytes, source->len);
    }
    result.len = source->len;
    *out = result;
    return true;
}

static bool cr_pattern_clone(CrContextV1 *context, CettaLdPatternV1 *out,
                             const CettaLdPatternV1 *source) {
    CettaLdPatternV1 result;
    uint32_t index;
    if (!out || !source)
        return cr_fail(context, CETTA_LD_CR_V1_INTERNAL_FAILURE,
                       "missing Pattern in contextual LanguageDef execution");
    if (!cr_take_work(context))
        return false;
    cetta_ld_pattern_v1_init(&result);
    result.kind = source->kind;
    switch (source->kind) {
    case CETTA_LD_PATTERN_FVAR_V1:
        if (!cr_text_clone(context, &result.as.fvar, &source->as.fvar))
            goto fail;
        break;
    case CETTA_LD_PATTERN_APPLY_V1:
        if (!cr_text_clone(context, &result.as.apply.head,
                           &source->as.apply.head))
            goto fail;
        result.as.apply.arguments.len = source->as.apply.arguments.len;
        if (result.as.apply.arguments.len > 0u) {
            result.as.apply.arguments.items = calloc(
                result.as.apply.arguments.len,
                sizeof(*result.as.apply.arguments.items));
            if (!result.as.apply.arguments.items) {
                (void)cr_fail(context, CETTA_LD_CR_V1_ALLOCATION_FAILURE,
                              "could not allocate contextual Pattern arguments");
                goto fail;
            }
            for (index = 0u; index < result.as.apply.arguments.len; index++) {
                if (!cr_pattern_clone(
                        context, &result.as.apply.arguments.items[index],
                        &source->as.apply.arguments.items[index]))
                    goto fail;
            }
        }
        break;
    default:
        (void)cr_fail(context, CETTA_LD_CR_V1_UNSUPPORTED_PROFILE,
                      "contextual runner result left the PApp/FVar profile");
        goto fail;
    }
    *out = result;
    return true;
fail:
    cetta_ld_pattern_v1_free(&result);
    return false;
}

static void cr_trace_free(CettaLdCrV1Trace *trace) {
    uint32_t index;
    if (!trace)
        return;
    for (index = 0u; index < trace->premise_len; index++) {
        if (trace->premises[index].kind == CETTA_LD_PREMISE_CONGRUENCE_V1)
            cr_trace_free(trace->premises[index].as.congruence.step);
    }
    free(trace->premises);
    free(trace);
}

static CettaLdCrV1Trace *cr_trace_clone(CrContextV1 *context,
                                        const CettaLdCrV1Trace *source) {
    CettaLdCrV1Trace *result;
    uint32_t index;
    if (!source) {
        (void)cr_fail(context, CETTA_LD_CR_V1_INTERNAL_FAILURE,
                      "missing nested contextual derivation trace");
        return NULL;
    }
    if (!cr_take_work(context))
        return NULL;
    result = calloc(1u, sizeof(*result));
    if (!result) {
        (void)cr_fail(context, CETTA_LD_CR_V1_ALLOCATION_FAILURE,
                      "could not allocate contextual derivation trace");
        return NULL;
    }
    result->rule_index = source->rule_index;
    if (source->premise_len > 0u) {
        result->premises = calloc(source->premise_len,
                                  sizeof(*result->premises));
        if (!result->premises) {
            free(result);
            (void)cr_fail(context, CETTA_LD_CR_V1_ALLOCATION_FAILURE,
                          "could not allocate contextual premise evidence");
            return NULL;
        }
        result->premise_len = source->premise_len;
        for (index = 0u; index < result->premise_len; index++) {
            result->premises[index] = source->premises[index];
            if (result->premises[index].kind ==
                    CETTA_LD_PREMISE_CONGRUENCE_V1) {
                result->premises[index].as.congruence.step = cr_trace_clone(
                    context, source->premises[index].as.congruence.step);
                if (!result->premises[index].as.congruence.step) {
                    result->premise_len = index + 1u;
                    cr_trace_free(result);
                    return NULL;
                }
            }
        }
    }
    return result;
}

void cetta_ld_cr_v1_results_init(CettaLdCrV1Results *results) {
    if (results)
        memset(results, 0, sizeof(*results));
}

void cetta_ld_cr_v1_results_free(CettaLdCrV1Results *results) {
    uint32_t index;
    if (!results)
        return;
    for (index = 0u; index < results->len; index++) {
        cetta_ld_pattern_v1_free(&results->items[index].term);
        cr_trace_free(results->items[index].trace);
    }
    free(results->items);
    memset(results, 0, sizeof(*results));
}

static bool cr_results_append(CrContextV1 *context,
                              CettaLdCrV1Results *results,
                              CettaLdCrV1ResultItem *item) {
    CettaLdCrV1ResultItem *grown;
    size_t next;
    if (!context || !results || !item)
        return cr_fail(context, CETTA_LD_CR_V1_INTERNAL_FAILURE,
                       "invalid contextual result append");
    if (results->len == UINT32_MAX)
        return cr_fail(context, CETTA_LD_CR_V1_WORK_LIMIT,
                       "contextual result multiplicity exceeds uint32 capacity");
    next = (size_t)results->len + 1u;
    if (next > SIZE_MAX / sizeof(*grown))
        return cr_fail(context, CETTA_LD_CR_V1_ALLOCATION_FAILURE,
                       "contextual result list is too large");
    grown = realloc(results->items, next * sizeof(*grown));
    if (!grown)
        return cr_fail(context, CETTA_LD_CR_V1_ALLOCATION_FAILURE,
                       "could not grow contextual result list");
    results->items = grown;
    results->items[results->len] = *item;
    results->len++;
    memset(item, 0, sizeof(*item));
    return true;
}

static void cr_bindings_free(CrBindingsV1 *bindings) {
    if (!bindings)
        return;
    free(bindings->items);
    memset(bindings, 0, sizeof(*bindings));
}

static const CettaLdPatternV1 *cr_bindings_lookup(
    const CrBindingsV1 *bindings, const CettaLdTextV1 *name) {
    uint32_t index;
    if (!bindings || !name)
        return NULL;
    for (index = 0u; index < bindings->len; index++) {
        if (cr_text_equal(bindings->items[index].name, name))
            return bindings->items[index].value;
    }
    return NULL;
}

static bool cr_bindings_clone(CrContextV1 *context, CrBindingsV1 *out,
                              const CrBindingsV1 *source) {
    CrBindingsV1 result = {0};
    size_t bytes;
    if (!out || !source)
        return cr_fail(context, CETTA_LD_CR_V1_INTERNAL_FAILURE,
                       "invalid contextual binding clone");
    if (source->len > 0u) {
        bytes = (size_t)source->len * sizeof(*result.items);
        result.items = malloc(bytes);
        if (!result.items)
            return cr_fail(context, CETTA_LD_CR_V1_ALLOCATION_FAILURE,
                           "could not clone contextual bindings");
        memcpy(result.items, source->items, bytes);
        result.len = source->len;
        result.capacity = source->len;
    }
    *out = result;
    return true;
}

static bool cr_bindings_add(CrContextV1 *context, CrBindingsV1 *bindings,
                            const CettaLdTextV1 *name,
                            const CettaLdPatternV1 *value) {
    CrBindingV1 *grown;
    uint32_t next_capacity;
    if (!bindings || !name || !value)
        return cr_fail(context, CETTA_LD_CR_V1_INTERNAL_FAILURE,
                       "invalid contextual binding");
    if (bindings->len == bindings->capacity) {
        if (bindings->capacity > UINT32_MAX / 2u)
            return cr_fail(context, CETTA_LD_CR_V1_ALLOCATION_FAILURE,
                           "too many contextual bindings");
        next_capacity = bindings->capacity ? bindings->capacity * 2u : 8u;
        grown = realloc(bindings->items,
                        (size_t)next_capacity * sizeof(*grown));
        if (!grown)
            return cr_fail(context, CETTA_LD_CR_V1_ALLOCATION_FAILURE,
                           "could not grow contextual bindings");
        bindings->items = grown;
        bindings->capacity = next_capacity;
    }
    bindings->items[bindings->len++] = (CrBindingV1){name, value};
    return true;
}

static CrMatchResultV1 cr_match_pattern(CrContextV1 *context,
                                        const CettaLdPatternV1 *pattern,
                                        const CettaLdPatternV1 *term,
                                        CrBindingsV1 *bindings) {
    const CettaLdPatternV1 *existing;
    uint32_t index;
    CrMatchResultV1 child;
    if (!pattern || !term || !bindings || !cr_take_work(context))
        return CR_MATCH_FAILURE_V1;
    switch (pattern->kind) {
    case CETTA_LD_PATTERN_FVAR_V1:
        existing = cr_bindings_lookup(bindings, &pattern->as.fvar);
        if (existing)
            return cetta_ld_cr_v1_pattern_equal(existing, term)
                ? CR_MATCH_SUCCESS_V1 : CR_MATCH_MISS_V1;
        return cr_bindings_add(context, bindings, &pattern->as.fvar, term)
            ? CR_MATCH_SUCCESS_V1 : CR_MATCH_FAILURE_V1;
    case CETTA_LD_PATTERN_APPLY_V1:
        if (term->kind != CETTA_LD_PATTERN_APPLY_V1 ||
            !cr_text_equal(&pattern->as.apply.head, &term->as.apply.head) ||
            pattern->as.apply.arguments.len != term->as.apply.arguments.len)
            return CR_MATCH_MISS_V1;
        for (index = 0u; index < pattern->as.apply.arguments.len; index++) {
            child = cr_match_pattern(
                context, &pattern->as.apply.arguments.items[index],
                &term->as.apply.arguments.items[index], bindings);
            if (child != CR_MATCH_SUCCESS_V1)
                return child;
        }
        return CR_MATCH_SUCCESS_V1;
    default:
        (void)cr_fail(context, CETTA_LD_CR_V1_UNSUPPORTED_PROFILE,
                      "matcher encountered a Pattern outside PApp/FVar");
        return CR_MATCH_FAILURE_V1;
    }
}

static bool cr_apply_bindings(CrContextV1 *context,
                              const CrBindingsV1 *bindings,
                              const CettaLdPatternV1 *pattern,
                              CettaLdPatternV1 *out) {
    const CettaLdPatternV1 *value;
    CettaLdPatternV1 result;
    uint32_t index;
    if (!bindings || !pattern || !out || !cr_take_work(context))
        return false;
    if (pattern->kind == CETTA_LD_PATTERN_FVAR_V1) {
        value = cr_bindings_lookup(bindings, &pattern->as.fvar);
        return cr_pattern_clone(context, out, value ? value : pattern);
    }
    if (pattern->kind != CETTA_LD_PATTERN_APPLY_V1)
        return cr_fail(context, CETTA_LD_CR_V1_UNSUPPORTED_PROFILE,
                       "binding application encountered a Pattern outside PApp/FVar");
    cetta_ld_pattern_v1_init(&result);
    result.kind = CETTA_LD_PATTERN_APPLY_V1;
    if (!cr_text_clone(context, &result.as.apply.head,
                       &pattern->as.apply.head))
        goto fail;
    result.as.apply.arguments.len = pattern->as.apply.arguments.len;
    if (result.as.apply.arguments.len > 0u) {
        result.as.apply.arguments.items = calloc(
            result.as.apply.arguments.len,
            sizeof(*result.as.apply.arguments.items));
        if (!result.as.apply.arguments.items) {
            (void)cr_fail(context, CETTA_LD_CR_V1_ALLOCATION_FAILURE,
                          "could not allocate substituted Pattern arguments");
            goto fail;
        }
        for (index = 0u; index < result.as.apply.arguments.len; index++) {
            if (!cr_apply_bindings(
                    context, bindings,
                    &pattern->as.apply.arguments.items[index],
                    &result.as.apply.arguments.items[index]))
                goto fail;
        }
    }
    *out = result;
    return true;
fail:
    cetta_ld_pattern_v1_free(&result);
    return false;
}

static void cr_branch_free(CrBranchV1 *branch) {
    uint32_t index;
    if (!branch)
        return;
    cr_bindings_free(&branch->bindings);
    for (index = 0u; index < branch->premise_len; index++) {
        if (branch->premises[index].kind == CETTA_LD_PREMISE_CONGRUENCE_V1)
            cr_trace_free(branch->premises[index].as.congruence.step);
    }
    free(branch->premises);
    memset(branch, 0, sizeof(*branch));
}

static bool cr_branch_clone(CrContextV1 *context, CrBranchV1 *out,
                            const CrBranchV1 *source) {
    CrBranchV1 result = {0};
    uint32_t index;
    if (!out || !source ||
        !cr_bindings_clone(context, &result.bindings, &source->bindings))
        return false;
    if (source->premise_len > 0u) {
        result.premises = calloc(source->premise_len,
                                 sizeof(*result.premises));
        if (!result.premises) {
            cr_branch_free(&result);
            return cr_fail(context, CETTA_LD_CR_V1_ALLOCATION_FAILURE,
                           "could not clone contextual premise branch");
        }
        result.premise_len = source->premise_len;
        result.premise_capacity = source->premise_len;
        for (index = 0u; index < source->premise_len; index++) {
            result.premises[index] = source->premises[index];
            if (result.premises[index].kind ==
                    CETTA_LD_PREMISE_CONGRUENCE_V1) {
                result.premises[index].as.congruence.step = cr_trace_clone(
                    context, source->premises[index].as.congruence.step);
                if (!result.premises[index].as.congruence.step) {
                    result.premise_len = index + 1u;
                    cr_branch_free(&result);
                    return false;
                }
            }
        }
    }
    *out = result;
    return true;
}

static bool cr_branch_append_evidence(
    CrContextV1 *context, CrBranchV1 *branch,
    CettaLdCrV1PremiseEvidence *evidence) {
    CettaLdCrV1PremiseEvidence *grown;
    uint32_t next_capacity;
    if (!context || !branch || !evidence)
        return cr_fail(context, CETTA_LD_CR_V1_INTERNAL_FAILURE,
                       "invalid contextual premise evidence");
    if (branch->premise_len == branch->premise_capacity) {
        if (branch->premise_capacity > UINT32_MAX / 2u)
            return cr_fail(context, CETTA_LD_CR_V1_ALLOCATION_FAILURE,
                           "too many contextual premise receipts");
        next_capacity = branch->premise_capacity
            ? branch->premise_capacity * 2u : 4u;
        grown = realloc(branch->premises,
                        (size_t)next_capacity * sizeof(*grown));
        if (!grown)
            return cr_fail(context, CETTA_LD_CR_V1_ALLOCATION_FAILURE,
                           "could not grow contextual premise receipts");
        branch->premises = grown;
        branch->premise_capacity = next_capacity;
    }
    branch->premises[branch->premise_len++] = *evidence;
    memset(evidence, 0, sizeof(*evidence));
    return true;
}

static bool cr_pattern_fresh(const CettaLdPatternV1 *pattern,
                             const CettaLdTextV1 *name) {
    uint32_t index;
    if (!pattern || !name)
        return false;
    if (pattern->kind == CETTA_LD_PATTERN_FVAR_V1)
        return !cr_text_equal(&pattern->as.fvar, name);
    if (pattern->kind != CETTA_LD_PATTERN_APPLY_V1)
        return false;
    for (index = 0u; index < pattern->as.apply.arguments.len; index++) {
        if (!cr_pattern_fresh(&pattern->as.apply.arguments.items[index], name))
            return false;
    }
    return true;
}

static bool cr_finish_rule(CrContextV1 *context, uint32_t rule_index,
                           const CettaLdRelationRuleV1 *rule,
                           CrBranchV1 *branch,
                           CettaLdCrV1Results *results) {
    CettaLdCrV1ResultItem item;
    if (!context || !rule || !branch || !results)
        return cr_fail(context, CETTA_LD_CR_V1_INTERNAL_FAILURE,
                       "invalid contextual rule completion");
    memset(&item, 0, sizeof(item));
    cetta_ld_pattern_v1_init(&item.term);
    if (!cr_apply_bindings(context, &branch->bindings, &rule->right,
                           &item.term))
        return false;
    item.trace = calloc(1u, sizeof(*item.trace));
    if (!item.trace) {
        cetta_ld_pattern_v1_free(&item.term);
        return cr_fail(context, CETTA_LD_CR_V1_ALLOCATION_FAILURE,
                       "could not allocate contextual rule trace");
    }
    item.trace->rule_index = rule_index;
    item.trace->premises = branch->premises;
    item.trace->premise_len = branch->premise_len;
    branch->premises = NULL;
    branch->premise_len = 0u;
    branch->premise_capacity = 0u;
    if (!cr_results_append(context, results, &item)) {
        cetta_ld_pattern_v1_free(&item.term);
        cr_trace_free(item.trace);
        return false;
    }
    return true;
}

static bool cr_eval_premises(CrContextV1 *context, uint32_t recursive_fuel,
                             uint32_t rule_index,
                             const CettaLdRelationRuleV1 *rule,
                             uint32_t premise_index,
                             CrBranchV1 *branch,
                             CettaLdCrV1Results *results);

static bool cr_relation_row(CrContextV1 *context, uint32_t recursive_fuel,
                            uint32_t rule_index,
                            const CettaLdRelationRuleV1 *rule,
                            uint32_t premise_index,
                            const CettaLdPremiseV1 *premise,
                            const CettaLdPatternV1 *row,
                            uint32_t row_len,
                            CettaLdCrV1RelationSource source,
                            uint32_t row_index,
                            uint64_t receipt_id,
                            CrBranchV1 *branch,
                            CettaLdCrV1Results *results) {
    CrBranchV1 next = {0};
    CettaLdCrV1PremiseEvidence evidence = {0};
    uint32_t index;
    CrMatchResultV1 matched;
    if (row_len != premise->as.relation_query.arguments.len)
        return true;
    if (!cr_branch_clone(context, &next, branch))
        return false;
    for (index = 0u; index < row_len; index++) {
        matched = cr_match_pattern(
            context, &premise->as.relation_query.arguments.items[index],
            &row[index], &next.bindings);
        if (matched == CR_MATCH_FAILURE_V1) {
            cr_branch_free(&next);
            return false;
        }
        if (matched == CR_MATCH_MISS_V1) {
            cr_branch_free(&next);
            return true;
        }
    }
    evidence.premise_index = premise_index;
    evidence.kind = CETTA_LD_PREMISE_RELATION_QUERY_V1;
    evidence.as.relation_query.source = source;
    evidence.as.relation_query.row_index = row_index;
    evidence.as.relation_query.receipt_id = receipt_id;
    if (!cr_branch_append_evidence(context, &next, &evidence) ||
        !cr_eval_premises(context, recursive_fuel, rule_index, rule,
                          premise_index + 1u, &next, results)) {
        cr_branch_free(&next);
        return false;
    }
    cr_branch_free(&next);
    return true;
}

static bool cr_eval_relation(CrContextV1 *context, uint32_t recursive_fuel,
                             uint32_t rule_index,
                             const CettaLdRelationRuleV1 *rule,
                             uint32_t premise_index,
                             const CettaLdPremiseV1 *premise,
                             CrBranchV1 *branch,
                             CettaLdCrV1Results *results) {
    CettaLdPatternV1 *applied = NULL;
    CettaLdPatternV1 builtin_row[2];
    const CettaLdPatternV1 *provider_row = NULL;
    uint32_t argument_len;
    uint32_t index;
    uint32_t provider_row_len = 0u;
    uint32_t provider_index = 0u;
    uint64_t receipt_id = 0u;
    bool present = false;
    bool ok = false;

    argument_len = premise->as.relation_query.arguments.len;
    if (argument_len > 0u) {
        applied = calloc(argument_len, sizeof(*applied));
        if (!applied)
            return cr_fail(context, CETTA_LD_CR_V1_ALLOCATION_FAILURE,
                           "could not allocate applied relation arguments");
        for (index = 0u; index < argument_len; index++) {
            if (!cr_apply_bindings(
                    context, &branch->bindings,
                    &premise->as.relation_query.arguments.items[index],
                    &applied[index]))
                goto cleanup;
        }
    }

    /* Lean's built-in eq relation precedes all external provider rows. */
    if (cr_text_is(&premise->as.relation_query.relation, "eq") &&
        argument_len == 2u) {
        builtin_row[0] = applied[0];
        builtin_row[1] = applied[0];
        if (!cr_relation_row(
                context, recursive_fuel, rule_index, rule, premise_index,
                premise, builtin_row, 2u, CETTA_LD_CR_V1_RELATION_BUILTIN,
                0u, 0u, branch, results))
            goto cleanup;
        builtin_row[0] = applied[1];
        builtin_row[1] = applied[1];
        if (!cr_relation_row(
                context, recursive_fuel, rule_index, rule, premise_index,
                premise, builtin_row, 2u, CETTA_LD_CR_V1_RELATION_BUILTIN,
                1u, 0u, branch, results))
            goto cleanup;
    }

    if (context->provider && context->provider->query) {
        for (;;) {
            if (!cr_take_work(context))
                goto cleanup;
            provider_row = NULL;
            provider_row_len = 0u;
            receipt_id = 0u;
            present = false;
            if (!context->provider->query(
                    context->provider->context,
                    &premise->as.relation_query.relation,
                    applied, argument_len, provider_index,
                    &provider_row, &provider_row_len, &receipt_id, &present,
                    context->error_buf, context->error_buf_size)) {
                context->status = CETTA_LD_CR_V1_PROVIDER_FAILURE;
                if (!context->error_buf || context->error_buf_size == 0u ||
                    context->error_buf[0] == '\0')
                    cr_set_error(context, "%s",
                                 "relation provider rejected a query");
                goto cleanup;
            }
            if (!present)
                break;
            if (!provider_row && provider_row_len > 0u) {
                (void)cr_fail(context, CETTA_LD_CR_V1_PROVIDER_FAILURE,
                              "relation provider returned a missing row");
                goto cleanup;
            }
            if (!cr_relation_row(
                    context, recursive_fuel, rule_index, rule, premise_index,
                    premise, provider_row, provider_row_len,
                    CETTA_LD_CR_V1_RELATION_EXTERNAL, provider_index,
                    receipt_id, branch, results))
                goto cleanup;
            if (provider_index == UINT32_MAX) {
                (void)cr_fail(context, CETTA_LD_CR_V1_PROVIDER_FAILURE,
                              "relation provider did not terminate");
                goto cleanup;
            }
            provider_index++;
        }
    }
    ok = true;
cleanup:
    if (applied) {
        for (index = 0u; index < argument_len; index++)
            cetta_ld_pattern_v1_free(&applied[index]);
    }
    free(applied);
    return ok;
}

static bool cr_eval_premises(CrContextV1 *context, uint32_t recursive_fuel,
                             uint32_t rule_index,
                             const CettaLdRelationRuleV1 *rule,
                             uint32_t premise_index,
                             CrBranchV1 *branch,
                             CettaLdCrV1Results *results) {
    const CettaLdPremiseV1 *premise;
    CrBranchV1 next = {0};
    CettaLdCrV1PremiseEvidence evidence = {0};
    CettaLdCrV1Results nested;
    CettaLdPatternV1 applied;
    const CettaLdPatternV1 *resolved;
    const CettaLdTextV1 *fresh_name;
    CrMatchResultV1 matched;
    uint32_t index;
    bool ok;

    if (premise_index == rule->premises.len)
        return cr_finish_rule(context, rule_index, rule, branch, results);
    premise = &rule->premises.items[premise_index];
    switch (premise->kind) {
    case CETTA_LD_PREMISE_CONGRUENCE_V1:
        cetta_ld_pattern_v1_init(&applied);
        if (!cr_apply_bindings(context, &branch->bindings,
                               &premise->as.congruence.left, &applied))
            return false;
        cetta_ld_cr_v1_results_init(&nested);
        ok = cr_rewrite(context, recursive_fuel, &applied, &nested);
        cetta_ld_pattern_v1_free(&applied);
        if (!ok) {
            cetta_ld_cr_v1_results_free(&nested);
            return false;
        }
        for (index = 0u; index < nested.len; index++) {
            if (!cr_branch_clone(context, &next, branch)) {
                cetta_ld_cr_v1_results_free(&nested);
                return false;
            }
            matched = cr_match_pattern(
                context, &premise->as.congruence.right,
                &nested.items[index].term, &next.bindings);
            if (matched == CR_MATCH_FAILURE_V1) {
                cr_branch_free(&next);
                cetta_ld_cr_v1_results_free(&nested);
                return false;
            }
            if (matched == CR_MATCH_SUCCESS_V1) {
                evidence.premise_index = premise_index;
                evidence.kind = CETTA_LD_PREMISE_CONGRUENCE_V1;
                evidence.as.congruence.step = cr_trace_clone(
                    context, nested.items[index].trace);
                if (!evidence.as.congruence.step ||
                    !cr_branch_append_evidence(context, &next, &evidence) ||
                    !cr_eval_premises(
                        context, recursive_fuel, rule_index, rule,
                        premise_index + 1u, &next, results)) {
                    cr_trace_free(evidence.as.congruence.step);
                    cr_branch_free(&next);
                    cetta_ld_cr_v1_results_free(&nested);
                    return false;
                }
            }
            cr_branch_free(&next);
        }
        cetta_ld_cr_v1_results_free(&nested);
        return true;
    case CETTA_LD_PREMISE_RELATION_QUERY_V1:
        return cr_eval_relation(context, recursive_fuel, rule_index, rule,
                                premise_index, premise, branch, results);
    case CETTA_LD_PREMISE_FRESHNESS_V1:
        resolved = cr_bindings_lookup(
            &branch->bindings, &premise->as.freshness.variable);
        if (resolved) {
            if (resolved->kind != CETTA_LD_PATTERN_FVAR_V1)
                return true;
            fresh_name = &resolved->as.fvar;
        } else {
            fresh_name = &premise->as.freshness.variable;
        }
        cetta_ld_pattern_v1_init(&applied);
        if (!cr_apply_bindings(context, &branch->bindings,
                               &premise->as.freshness.term, &applied))
            return false;
        ok = cr_pattern_fresh(&applied, fresh_name);
        cetta_ld_pattern_v1_free(&applied);
        if (!ok)
            return true;
        if (!cr_branch_clone(context, &next, branch))
            return false;
        evidence.premise_index = premise_index;
        evidence.kind = CETTA_LD_PREMISE_FRESHNESS_V1;
        if (!cr_branch_append_evidence(context, &next, &evidence) ||
            !cr_eval_premises(context, recursive_fuel, rule_index, rule,
                              premise_index + 1u, &next, results)) {
            cr_branch_free(&next);
            return false;
        }
        cr_branch_free(&next);
        return true;
    case CETTA_LD_PREMISE_FOR_ALL_V1:
        /* This is exactly the current engineBasePremises behavior. */
        return true;
    }
    return cr_fail(context, CETTA_LD_CR_V1_INTERNAL_FAILURE,
                   "unknown contextual premise kind");
}

static bool cr_rewrite(CrContextV1 *context, uint32_t fuel,
                       const CettaLdPatternV1 *source,
                       CettaLdCrV1Results *results) {
    const CettaLanguageDefCoreV1 *language;
    CrBranchV1 branch;
    CrMatchResultV1 matched;
    uint32_t rule_index;
    if (!context || !context->program || !context->program->language ||
        !source || !results)
        return cr_fail(context, CETTA_LD_CR_V1_INTERNAL_FAILURE,
                       "invalid contextual rewrite state");
    if (fuel == 0u) {
        context->context_fuel_exhausted = true;
        return true;
    }
    language = context->program->language;
    for (rule_index = 0u; rule_index < language->rewrite_len; rule_index++) {
        memset(&branch, 0, sizeof(branch));
        matched = cr_match_pattern(
            context, &language->rewrites[rule_index].left,
            source, &branch.bindings);
        if (matched == CR_MATCH_FAILURE_V1) {
            cr_branch_free(&branch);
            return false;
        }
        if (matched == CR_MATCH_SUCCESS_V1 &&
            !cr_eval_premises(
                context, fuel - 1u, rule_index,
                &language->rewrites[rule_index], 0u, &branch, results)) {
            cr_branch_free(&branch);
            return false;
        }
        cr_branch_free(&branch);
    }
    return true;
}

static bool cr_pattern_supported(const CettaLdPatternV1 *pattern) {
    uint32_t index;
    if (!pattern)
        return false;
    if (pattern->kind == CETTA_LD_PATTERN_FVAR_V1)
        return pattern->as.fvar.len == 0u || pattern->as.fvar.bytes;
    if (pattern->kind != CETTA_LD_PATTERN_APPLY_V1 ||
        (pattern->as.apply.head.len > 0u && !pattern->as.apply.head.bytes))
        return false;
    for (index = 0u; index < pattern->as.apply.arguments.len; index++) {
        if (!cr_pattern_supported(&pattern->as.apply.arguments.items[index]))
            return false;
    }
    return true;
}

static bool cr_premise_supported(const CettaLdPremiseV1 *premise) {
    uint32_t index;
    if (!premise)
        return false;
    switch (premise->kind) {
    case CETTA_LD_PREMISE_FRESHNESS_V1:
        return cr_pattern_supported(&premise->as.freshness.term);
    case CETTA_LD_PREMISE_CONGRUENCE_V1:
        return cr_pattern_supported(&premise->as.congruence.left) &&
            cr_pattern_supported(&premise->as.congruence.right);
    case CETTA_LD_PREMISE_RELATION_QUERY_V1:
        for (index = 0u;
             index < premise->as.relation_query.arguments.len; index++) {
            if (!cr_pattern_supported(
                    &premise->as.relation_query.arguments.items[index]))
                return false;
        }
        return true;
    case CETTA_LD_PREMISE_FOR_ALL_V1:
        return cr_premise_supported(premise->as.for_all.body);
    }
    return false;
}

void cetta_ld_cr_v1_program_init(CettaLdCrV1Program *program) {
    if (program)
        memset(program, 0, sizeof(*program));
}

bool cetta_ld_cr_v1_compile(CettaLdCrV1Program *out,
                            const CettaLanguageDefCoreV1 *language,
                            CettaLdCrV1Status *status,
                            char *error_buf,
                            size_t error_buf_size) {
    CettaLdCrV1Program result;
    uint32_t rule_index;
    uint32_t premise_index;
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (status)
        *status = CETTA_LD_CR_V1_BAD_ARGUMENT;
    if (!out || !language) {
        if (error_buf && error_buf_size > 0u)
            (void)snprintf(error_buf, error_buf_size,
                           "%s", "missing contextual runner argument");
        return false;
    }
    for (rule_index = 0u; rule_index < language->rewrite_len; rule_index++) {
        const CettaLdRelationRuleV1 *rule = &language->rewrites[rule_index];
        if (!cr_pattern_supported(&rule->left) ||
            !cr_pattern_supported(&rule->right)) {
            if (status)
                *status = CETTA_LD_CR_V1_UNSUPPORTED_PROFILE;
            if (error_buf && error_buf_size > 0u)
                (void)snprintf(
                    error_buf, error_buf_size,
                    "rewrite row %u leaves the PApp/FVar contextual profile",
                    rule_index);
            return false;
        }
        for (premise_index = 0u; premise_index < rule->premises.len;
             premise_index++) {
            if (!cr_premise_supported(&rule->premises.items[premise_index])) {
                if (status)
                    *status = CETTA_LD_CR_V1_UNSUPPORTED_PROFILE;
                if (error_buf && error_buf_size > 0u)
                    (void)snprintf(
                        error_buf, error_buf_size,
                        "rewrite row %u premise %u leaves the PApp/FVar contextual profile",
                        rule_index, premise_index);
                return false;
            }
        }
    }
    result.language = language;
    *out = result;
    if (status)
        *status = CETTA_LD_CR_V1_OK;
    return true;
}

bool cetta_ld_cr_v1_reducts(const CettaLdCrV1Program *program,
                            const CettaLdCrV1RelationProvider *provider,
                            uint32_t context_fuel,
                            uint64_t work_limit,
                            const CettaLdPatternV1 *source,
                            CettaLdCrV1Results *out,
                            CettaLdCrV1Status *status,
                            char *error_buf,
                            size_t error_buf_size) {
    CettaLdCrV1Results result;
    CrContextV1 context;
    bool ok;
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (status)
        *status = CETTA_LD_CR_V1_BAD_ARGUMENT;
    if (!program || !program->language || !source || !out ||
        work_limit == 0u) {
        if (error_buf && error_buf_size > 0u)
            (void)snprintf(error_buf, error_buf_size,
                           "%s", "invalid contextual runner arguments");
        return false;
    }
    cetta_ld_cr_v1_results_init(&result);
    memset(&context, 0, sizeof(context));
    context.program = program;
    context.provider = provider;
    context.remaining_work = work_limit;
    context.status = CETTA_LD_CR_V1_OK;
    context.error_buf = error_buf;
    context.error_buf_size = error_buf_size;
    ok = cr_rewrite(&context, context_fuel, source, &result);
    if (!ok) {
        cetta_ld_cr_v1_results_free(&result);
        if (status)
            *status = context.status;
        return false;
    }
    result.context_fuel_exhausted = context.context_fuel_exhausted;
    cetta_ld_cr_v1_results_free(out);
    *out = result;
    if (status)
        *status = CETTA_LD_CR_V1_OK;
    return true;
}

const char *cetta_ld_cr_v1_status_name(CettaLdCrV1Status status) {
    switch (status) {
    case CETTA_LD_CR_V1_OK: return "ok";
    case CETTA_LD_CR_V1_BAD_ARGUMENT: return "bad-argument";
    case CETTA_LD_CR_V1_UNSUPPORTED_PROFILE: return "unsupported-profile";
    case CETTA_LD_CR_V1_PROVIDER_FAILURE: return "provider-failure";
    case CETTA_LD_CR_V1_WORK_LIMIT: return "work-limit";
    case CETTA_LD_CR_V1_ALLOCATION_FAILURE: return "allocation-failure";
    case CETTA_LD_CR_V1_INTERNAL_FAILURE: return "internal-failure";
    }
    return "unknown";
}
