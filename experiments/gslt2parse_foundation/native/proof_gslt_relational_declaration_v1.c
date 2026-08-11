#include "proof_gslt_relational_declaration_v1.h"

#include "relational_value_list_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t value;
    PPProofGSLTSequenceTokenV1 *token;
} PPProofRelationalTokenEntryV1;

typedef struct {
    uint32_t value;
    PPProofGSLTTokenSequenceV1 sequence;
} PPProofRelationalFormulaEntryV1;

typedef enum {
    PPPROOF_RELATIONAL_RELATION_V1_DIFFERENT = 0,
    PPPROOF_RELATIONAL_RELATION_V1_APART = 1
} PPProofRelationalRelationKindV1;

typedef struct {
    PPProofRelationalRelationKindV1 kind;
    uint32_t authority;
    const PPProofGSLTSequenceTokenV1 *left;
    const PPProofGSLTSequenceTokenV1 *right;
    PPProofGSLTReferenceV1 reference;
} PPProofRelationalRelationEntryV1;

typedef struct {
    PPRelationalStoreV1 store;
    const PPProofGSLTRelationalAssertionPlanV1 *relational_plan;
    const PPProofGSLTSequenceEvidenceABIV1 *evidence_abi;
    PPProofGSLTArticleV1Limits limits;
    uint32_t selector_values[PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_LEN];
    PPProofRelationalTokenEntryV1 *tokens;
    uint32_t token_len;
    uint32_t token_cap;
    PPProofRelationalFormulaEntryV1 *formulas;
    uint32_t formula_len;
    uint32_t formula_cap;
    PPProofRelationalRelationEntryV1 *relations;
    uint32_t relation_len;
    uint32_t relation_cap;
    PPProofGSLTPatternV1 *premises;
    bool *premise_filled;
    uint32_t premise_len;
    uint32_t premise_cap;
    void **allocations;
    uint32_t allocation_len;
    uint32_t allocation_cap;
    uint32_t materialized_pattern_len;
} PPProofRelationalContextImplV1;

typedef struct {
    PPProofGSLTRelationalBindingSchemaV1 *bindings;
    uint32_t binding_cap;
    PPProofGSLTRelationalEssentialSchemaV1 *essentials;
    uint32_t essential_cap;
    PPProofGSLTAssertionDisjointV1 *disjoints;
    uint32_t disjoint_cap;
    PPProofGSLTRelationalOrderedHypothesisV1 *ordered;
    uint32_t ordered_cap;
} PPProofRelationalDeclarationStorageV1;

typedef struct {
    PPProofGSLTAssertionBindingV1 *bindings;
    PPProofGSLTAssertionEssentialV1 *essentials;
} PPProofRelationalPreparedStorageV1;

typedef struct {
    uint32_t position;
    uint32_t label;
} PPProofRelationalOrderedRowV1;

static void ppproof_relational_declaration_v1_set_error(
    char *buf, size_t size, const char *format, ...) {
    va_list arguments;

    if (!buf || size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(buf, size, format, arguments);
    va_end(arguments);
}

static bool ppproof_relational_declaration_v1_grow(
    void **items, uint32_t *capacity, uint32_t required,
    size_t item_size) {
    uint32_t next_capacity;
    void *next;

    if (!items || !capacity || item_size == 0u)
        return false;
    if (required <= *capacity)
        return true;
    next_capacity = *capacity ? *capacity : 8u;
    while (next_capacity < required) {
        if (next_capacity > UINT32_MAX / 2u) {
            next_capacity = required;
            break;
        }
        next_capacity *= 2u;
    }
    if ((size_t)next_capacity > SIZE_MAX / item_size)
        return false;
    next = realloc(*items, (size_t)next_capacity * item_size);
    if (!next)
        return false;
    *items = next;
    *capacity = next_capacity;
    return true;
}

static void *ppproof_relational_context_v1_allocate(
    PPProofRelationalContextImplV1 *impl,
    size_t count, size_t item_size) {
    void *allocation;

    if (!impl || count == 0u || item_size == 0u ||
        count > SIZE_MAX / item_size ||
        impl->allocation_len == UINT32_MAX ||
        !ppproof_relational_declaration_v1_grow(
            (void **)&impl->allocations, &impl->allocation_cap,
            impl->allocation_len + 1u, sizeof(*impl->allocations)))
        return NULL;
    allocation = calloc(count, item_size);
    if (!allocation)
        return NULL;
    impl->allocations[impl->allocation_len++] = allocation;
    return allocation;
}

static PPProofGSLTPatternV1 *ppproof_relational_context_v1_apply(
    PPProofRelationalContextImplV1 *impl,
    PPProofGSLTNameV1 constructor,
    const PPProofGSLTPatternV1 *const *arguments,
    uint32_t argument_len) {
    PPProofGSLTPatternV1 *pattern;
    PPProofGSLTPatternV1 *copied = NULL;
    uint32_t index;

    if (!impl || !constructor.bytes || constructor.len == 0u ||
        (argument_len != 0u && !arguments) ||
        impl->materialized_pattern_len >=
            impl->limits.maximum_materialized_pattern_nodes)
        return NULL;
    pattern = ppproof_relational_context_v1_allocate(
        impl, 1u, sizeof(*pattern));
    if (!pattern)
        return NULL;
    if (argument_len != 0u) {
        copied = ppproof_relational_context_v1_allocate(
            impl, argument_len, sizeof(*copied));
        if (!copied)
            return NULL;
    }
    for (index = 0u; index < argument_len; index++) {
        if (!arguments[index])
            return NULL;
        copied[index] = *arguments[index];
    }
    pattern->kind = PPPROOF_GSLT_PATTERN_V1_APPLY;
    pattern->as.apply.constructor = constructor;
    pattern->as.apply.arguments = copied;
    pattern->as.apply.argument_len = argument_len;
    impl->materialized_pattern_len++;
    return pattern;
}

static bool ppproof_relational_context_v1_grow_premises(
    PPProofRelationalContextImplV1 *impl, uint32_t required) {
    PPProofGSLTPatternV1 *next_premises;
    bool *next_filled;
    uint32_t next_capacity;

    if (!impl)
        return false;
    if (required <= impl->premise_cap)
        return true;
    next_capacity = impl->premise_cap ? impl->premise_cap : 16u;
    while (next_capacity < required) {
        if (next_capacity > UINT32_MAX / 2u) {
            next_capacity = required;
            break;
        }
        next_capacity *= 2u;
    }
    if ((size_t)next_capacity >
            SIZE_MAX / sizeof(*next_premises) ||
        (size_t)next_capacity > SIZE_MAX / sizeof(*next_filled))
        return false;
    next_premises = calloc(next_capacity, sizeof(*next_premises));
    next_filled = calloc(next_capacity, sizeof(*next_filled));
    if (!next_premises || !next_filled) {
        free(next_premises);
        free(next_filled);
        return false;
    }
    if (impl->premise_len != 0u) {
        memcpy(next_premises, impl->premises,
               (size_t)impl->premise_len * sizeof(*next_premises));
        memcpy(next_filled, impl->premise_filled,
               (size_t)impl->premise_len * sizeof(*next_filled));
    }
    free(impl->premises);
    free(impl->premise_filled);
    impl->premises = next_premises;
    impl->premise_filled = next_filled;
    impl->premise_cap = next_capacity;
    return true;
}

static bool ppproof_relational_context_v1_append_premise(
    PPProofRelationalContextImplV1 *impl,
    const PPProofGSLTPatternV1 *premise,
    bool filled,
    PPProofGSLTReferenceV1 *reference_out) {
    uint32_t index;

    if (!impl || !reference_out || (filled && !premise) ||
        impl->premise_len == UINT32_MAX ||
        impl->premise_len >= impl->limits.maximum_article_nodes ||
        !ppproof_relational_context_v1_grow_premises(
            impl, impl->premise_len + 1u))
        return false;
    index = impl->premise_len++;
    if (filled)
        impl->premises[index] = *premise;
    impl->premise_filled[index] = filled;
    *reference_out = (PPProofGSLTReferenceV1){
        .kind = PPPROOF_GSLT_REFERENCE_V1_PREMISE,
        .index = index,
    };
    return true;
}

static bool ppproof_relational_context_v1_table_shape(
    const PPProofRelationalContextImplV1 *impl,
    PPProofGSLTRelationalTableRoleV1 role,
    uint32_t arity,
    uint32_t key_arity,
    uint32_t *row_len_out) {
    uint32_t actual_arity = 0u;
    uint32_t actual_key_arity = 0u;
    uint32_t row_len = 0u;

    if (!impl || role >= PPPROOF_GSLT_RELATIONAL_TABLE_V1_LEN ||
        !impl->store.table_shape(
            impl->store.context,
            impl->relational_plan->tables[role],
            &actual_arity, &actual_key_arity, &row_len) ||
        actual_arity != arity || actual_key_arity != key_arity)
        return false;
    if (row_len_out)
        *row_len_out = row_len;
    return true;
}

static bool ppproof_relational_context_v1_find(
    const PPProofRelationalContextImplV1 *impl,
    PPProofGSLTRelationalTableRoleV1 role,
    const uint32_t *key,
    uint32_t key_len,
    uint32_t *row,
    uint32_t row_capacity) {
    return impl && role < PPPROOF_GSLT_RELATIONAL_TABLE_V1_LEN &&
           impl->store.table_find(
               impl->store.context,
               impl->relational_plan->tables[role],
               key, key_len, row, row_capacity);
}

static PPProofRelationalTokenEntryV1 *
ppproof_relational_context_v1_token_entry(
    PPProofRelationalContextImplV1 *impl,
    uint32_t value) {
    uint32_t index;

    for (index = 0u; impl && index < impl->token_len; index++) {
        if (impl->tokens[index].value == value)
            return &impl->tokens[index];
    }
    return NULL;
}

static PPProofRelationalTokenEntryV1 *
ppproof_relational_context_v1_token_pointer_entry(
    PPProofRelationalContextImplV1 *impl,
    const PPProofGSLTSequenceTokenV1 *token) {
    uint32_t index;

    for (index = 0u; impl && index < impl->token_len; index++) {
        if (impl->tokens[index].token == token)
            return &impl->tokens[index];
    }
    return NULL;
}

static PPProofRelationalTokenEntryV1 *
ppproof_relational_context_v1_token_term_entry(
    PPProofRelationalContextImplV1 *impl,
    const PPProofGSLTPatternV1 *term) {
    uint32_t index;

    for (index = 0u; impl && index < impl->token_len; index++) {
        if (impl->tokens[index].token->term == term)
            return &impl->tokens[index];
    }
    return NULL;
}

static PPProofGSLTArticleV1Result
ppproof_relational_context_v1_token(
    PPProofRelationalContextImplV1 *impl,
    uint32_t value,
    const PPProofGSLTSequenceTokenV1 **token_out,
    char *error_buf,
    size_t error_buf_size) {
    PPProofRelationalTokenEntryV1 *cached;
    const uint8_t *bytes = NULL;
    uint32_t byte_len = 0u;
    uint32_t kind_row[2];
    PPProofGSLTPatternV1 *term;
    PPProofGSLTSequenceTokenV1 *token;
    const PPProofGSLTPatternV1 *arguments[1];
    PPProofGSLTPatternV1 *classification;
    PPProofGSLTNameV1 judgment;
    PPProofGSLTReferenceV1 reference;
    bool literal;
    bool variable;

    if (!impl || !token_out)
        return PPPROOF_GSLT_ARTICLE_V1_INVALID;
    cached = ppproof_relational_context_v1_token_entry(impl, value);
    if (cached) {
        *token_out = cached->token;
        return PPPROOF_GSLT_ARTICLE_V1_OK;
    }
    if (impl->token_len >=
            impl->limits.maximum_materialized_pattern_nodes ||
        !impl->store.value_bytes(
            impl->store.context, value, &bytes, &byte_len) ||
        !bytes || byte_len == 0u ||
        !ppproof_relational_context_v1_find(
            impl, PPPROOF_GSLT_RELATIONAL_TABLE_V1_SYMBOL_KIND,
            &value, 1u, kind_row, 2u)) {
        ppproof_relational_declaration_v1_set_error(
            error_buf, error_buf_size,
            "relational token lacks a classified store value");
        return PPPROOF_GSLT_ARTICLE_V1_INVALID;
    }
    literal = kind_row[1] ==
        impl->selector_values[
            PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_SYMBOL_LITERAL];
    variable = kind_row[1] ==
        impl->selector_values[
            PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_SYMBOL_VARIABLE];
    if (literal == variable) {
        ppproof_relational_declaration_v1_set_error(
            error_buf, error_buf_size,
            "relational token has an unknown or ambiguous class");
        return PPPROOF_GSLT_ARTICLE_V1_INVALID;
    }
    term = ppproof_relational_context_v1_apply(
        impl, (PPProofGSLTNameV1){bytes, byte_len}, NULL, 0u);
    token = ppproof_relational_context_v1_allocate(
        impl, 1u, sizeof(*token));
    if (!term || !token)
        return PPPROOF_GSLT_ARTICLE_V1_RESOURCE;
    token->term = term;
    token->literal = literal;
    token->variable = variable;
    judgment = impl->evidence_abi->judgments[
        literal ? PPPROOF_GSLT_SEQUENCE_JUDGMENT_V1_LITERAL
                : PPPROOF_GSLT_SEQUENCE_JUDGMENT_V1_VARIABLE];
    arguments[0] = term;
    classification = ppproof_relational_context_v1_apply(
        impl, judgment, arguments, 1u);
    if (!classification ||
        !ppproof_relational_context_v1_append_premise(
            impl, classification, true, &reference))
        return PPPROOF_GSLT_ARTICLE_V1_RESOURCE;
    if (literal)
        token->literal_evidence = reference;
    else
        token->variable_evidence = reference;
    if (!ppproof_relational_declaration_v1_grow(
            (void **)&impl->tokens, &impl->token_cap,
            impl->token_len + 1u, sizeof(*impl->tokens)))
        return PPPROOF_GSLT_ARTICLE_V1_RESOURCE;
    impl->tokens[impl->token_len++] =
        (PPProofRelationalTokenEntryV1){value, token};
    *token_out = token;
    return PPPROOF_GSLT_ARTICLE_V1_OK;
}

static PPProofGSLTArticleV1Result
ppproof_relational_context_v1_formula_impl(
    PPProofRelationalContextImplV1 *impl,
    uint32_t formula_value,
    PPProofGSLTTokenSequenceV1 *formula_out,
    char *error_buf,
    size_t error_buf_size) {
    const uint8_t *bytes = NULL;
    uint32_t byte_len = 0u;
    PPRelationalValueListV1Cursor cursor;
    const PPProofGSLTSequenceTokenV1 **tokens = NULL;
    uint32_t index;

    if (!impl || !formula_out)
        return PPPROOF_GSLT_ARTICLE_V1_INVALID;
    for (index = 0u; index < impl->formula_len; index++) {
        if (impl->formulas[index].value == formula_value) {
            *formula_out = impl->formulas[index].sequence;
            return PPPROOF_GSLT_ARTICLE_V1_OK;
        }
    }
    if (!impl->store.value_bytes(
            impl->store.context, formula_value, &bytes, &byte_len) ||
        !pprelational_value_list_v1_cursor_init(
            bytes, byte_len, &cursor) ||
        cursor.item_len >
            impl->limits.maximum_materialized_pattern_nodes) {
        ppproof_relational_declaration_v1_set_error(
            error_buf, error_buf_size,
            "relational formula has an invalid value-list encoding");
        return PPPROOF_GSLT_ARTICLE_V1_INVALID;
    }
    if (cursor.item_len != 0u) {
        tokens = ppproof_relational_context_v1_allocate(
            impl, cursor.item_len, sizeof(*tokens));
        if (!tokens)
            return PPPROOF_GSLT_ARTICLE_V1_RESOURCE;
    }
    for (index = 0u; index < cursor.item_len; index++) {
        const uint8_t *item_bytes = NULL;
        uint32_t item_len = 0u;
        uint32_t item_value = 0u;
        PPProofGSLTArticleV1Result result;

        if (!pprelational_value_list_v1_cursor_next(
                &cursor, &item_bytes, &item_len) ||
            !impl->store.value_intern(
                impl->store.context, item_bytes, item_len,
                &item_value))
            return PPPROOF_GSLT_ARTICLE_V1_INVALID;
        result = ppproof_relational_context_v1_token(
            impl, item_value, &tokens[index],
            error_buf, error_buf_size);
        if (result != PPPROOF_GSLT_ARTICLE_V1_OK)
            return result;
    }
    if (!ppproof_relational_declaration_v1_grow(
            (void **)&impl->formulas, &impl->formula_cap,
            impl->formula_len + 1u, sizeof(*impl->formulas)))
        return PPPROOF_GSLT_ARTICLE_V1_RESOURCE;
    impl->formulas[impl->formula_len] =
        (PPProofRelationalFormulaEntryV1){
            .value = formula_value,
            .sequence = {tokens, cursor.item_len},
        };
    *formula_out = impl->formulas[impl->formula_len++].sequence;
    return PPPROOF_GSLT_ARTICLE_V1_OK;
}

void ppproof_gslt_relational_context_v1_init(
    PPProofGSLTRelationalContextV1 *context) {
    if (context)
        memset(context, 0, sizeof(*context));
}

void ppproof_gslt_relational_context_v1_free(
    PPProofGSLTRelationalContextV1 *context) {
    PPProofRelationalContextImplV1 *impl;
    uint32_t index;

    if (!context)
        return;
    impl = context->implementation;
    if (impl) {
        for (index = 0u; index < impl->allocation_len; index++)
            free(impl->allocations[index]);
        free(impl->allocations);
        free(impl->tokens);
        free(impl->formulas);
        free(impl->relations);
        free(impl->premises);
        free(impl->premise_filled);
        free(impl);
    }
    memset(context, 0, sizeof(*context));
}

PPProofGSLTArticleV1Result ppproof_gslt_relational_context_v1_begin(
    PPProofGSLTRelationalContextV1 *context,
    const PPRelationalStoreV1 *store,
    const PPProofGSLTRelationalAssertionPlanV1 *relational_plan,
    const PPProofGSLTSequenceEvidenceABIV1 *evidence_abi,
    const PPProofGSLTArticleV1Limits *limits_argument,
    char *error_buf,
    size_t error_buf_size) {
    PPProofGSLTArticleV1Limits default_limits;
    const PPProofGSLTArticleV1Limits *limits = limits_argument;
    PPProofRelationalContextImplV1 *impl;
    uint32_t index;
    static const uint32_t arities[
        PPPROOF_GSLT_RELATIONAL_TABLE_V1_LEN] = {
            2u, 2u, 2u, 2u, 3u, 2u, 3u, 2u,
        };
    static const uint32_t key_arities[
        PPPROOF_GSLT_RELATIONAL_TABLE_V1_LEN] = {
            1u, 1u, 1u, 2u, 2u, 2u, 3u, 1u,
        };

    if (error_buf && error_buf_size != 0u)
        error_buf[0] = '\0';
    if (!context || !pprelational_store_v1_valid(store) ||
        !relational_plan || !relational_plan->storage ||
        !evidence_abi || !evidence_abi->storage ||
        !ppproof_gslt_article_v1_name_equal(
            relational_plan->owner, evidence_abi->owner) ||
        !ppproof_gslt_article_v1_name_equal(
            relational_plan->base, evidence_abi->base)) {
        ppproof_relational_declaration_v1_set_error(
            error_buf, error_buf_size,
            "invalid relational declaration context request");
        return PPPROOF_GSLT_ARTICLE_V1_INVALID;
    }
    if (!limits) {
        default_limits = ppproof_gslt_article_v1_default_limits();
        limits = &default_limits;
    }
    if (limits->maximum_materialized_pattern_nodes == 0u ||
        limits->maximum_article_nodes == 0u ||
        limits->maximum_pattern_depth == 0u) {
        ppproof_relational_declaration_v1_set_error(
            error_buf, error_buf_size,
            "relational declaration limits are malformed");
        return PPPROOF_GSLT_ARTICLE_V1_INVALID;
    }
    impl = calloc(1u, sizeof(*impl));
    if (!impl)
        return PPPROOF_GSLT_ARTICLE_V1_RESOURCE;
    impl->store = *store;
    impl->relational_plan = relational_plan;
    impl->evidence_abi = evidence_abi;
    impl->limits = *limits;
    for (index = 0u;
         index < PPPROOF_GSLT_RELATIONAL_TABLE_V1_LEN; index++) {
        if (!ppproof_relational_context_v1_table_shape(
                impl, (PPProofGSLTRelationalTableRoleV1)index,
                arities[index], key_arities[index], NULL)) {
            free(impl);
            ppproof_relational_declaration_v1_set_error(
                error_buf, error_buf_size,
                "relational declaration store shape differs from its plan");
            return PPPROOF_GSLT_ARTICLE_V1_INVALID;
        }
    }
    for (index = 0u;
         index < PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_LEN; index++) {
        const PPProofGSLTRelationalSelectorV1 *selector =
            &relational_plan->selectors[index];
        if (!selector->bytes || selector->len == 0u ||
            !store->value_intern(
                store->context, selector->bytes, selector->len,
                &impl->selector_values[index])) {
            free(impl);
            return PPPROOF_GSLT_ARTICLE_V1_RESOURCE;
        }
    }
    if (impl->selector_values[
            PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_SYMBOL_LITERAL] ==
            impl->selector_values[
                PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_SYMBOL_VARIABLE] ||
        impl->selector_values[
            PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_HYPOTHESIS_FLOATING] ==
            impl->selector_values[
                PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_HYPOTHESIS_ESSENTIAL]) {
        free(impl);
        ppproof_relational_declaration_v1_set_error(
            error_buf, error_buf_size,
            "relational declaration selectors are ambiguous");
        return PPPROOF_GSLT_ARTICLE_V1_INVALID;
    }
    ppproof_gslt_relational_context_v1_free(context);
    context->implementation = impl;
    return PPPROOF_GSLT_ARTICLE_V1_OK;
}

PPProofGSLTArticleV1Result ppproof_gslt_relational_context_v1_formula(
    PPProofGSLTRelationalContextV1 *context,
    uint32_t formula_value,
    PPProofGSLTTokenSequenceV1 *formula_out,
    char *error_buf,
    size_t error_buf_size) {
    if (error_buf && error_buf_size != 0u)
        error_buf[0] = '\0';
    if (!context || !context->implementation) {
        ppproof_relational_declaration_v1_set_error(
            error_buf, error_buf_size,
            "relational declaration context is not active");
        return PPPROOF_GSLT_ARTICLE_V1_INVALID;
    }
    return ppproof_relational_context_v1_formula_impl(
        context->implementation, formula_value, formula_out,
        error_buf, error_buf_size);
}

PPProofGSLTArticleV1Result
ppproof_gslt_relational_context_v1_typed_formula(
    PPProofGSLTRelationalContextV1 *context,
    const PPProofGSLTPatternV1 *typecode,
    PPProofGSLTTokenSequenceV1 body,
    PPProofGSLTTokenSequenceV1 *formula_out,
    char *error_buf,
    size_t error_buf_size) {
    PPProofRelationalContextImplV1 *impl;
    PPProofRelationalTokenEntryV1 *type_entry;
    const PPProofGSLTSequenceTokenV1 **tokens;

    if (error_buf && error_buf_size != 0u)
        error_buf[0] = '\0';
    if (!context || !(impl = context->implementation) || !typecode ||
        !formula_out || (body.token_len != 0u && !body.tokens) ||
        body.token_len == UINT32_MAX ||
        body.token_len + 1u >
            impl->limits.maximum_materialized_pattern_nodes) {
        ppproof_relational_declaration_v1_set_error(
            error_buf, error_buf_size,
            "invalid typed relational formula request");
        return PPPROOF_GSLT_ARTICLE_V1_INVALID;
    }
    type_entry = ppproof_relational_context_v1_token_term_entry(
        impl, typecode);
    if (!type_entry || !type_entry->token->literal) {
        ppproof_relational_declaration_v1_set_error(
            error_buf, error_buf_size,
            "relational formula typecode is not a classified literal");
        return PPPROOF_GSLT_ARTICLE_V1_INVALID;
    }
    tokens = ppproof_relational_context_v1_allocate(
        impl, body.token_len + 1u, sizeof(*tokens));
    if (!tokens)
        return PPPROOF_GSLT_ARTICLE_V1_RESOURCE;
    tokens[0] = type_entry->token;
    if (body.token_len != 0u) {
        memcpy(tokens + 1u, body.tokens,
               (size_t)body.token_len * sizeof(*tokens));
    }
    *formula_out = (PPProofGSLTTokenSequenceV1){
        .tokens = tokens,
        .token_len = body.token_len + 1u,
    };
    return PPPROOF_GSLT_ARTICLE_V1_OK;
}

static PPProofGSLTPatternV1 *
ppproof_relational_context_v1_materialize_sequence(
    PPProofRelationalContextImplV1 *impl,
    PPProofGSLTTokenSequenceV1 sequence) {
    PPProofGSLTPatternV1 *term;
    uint32_t cursor;

    if (!impl || (sequence.token_len != 0u && !sequence.tokens))
        return NULL;
    term = ppproof_relational_context_v1_apply(
        impl,
        impl->evidence_abi->constructors[
            PPPROOF_GSLT_SEQUENCE_CONSTRUCTOR_V1_SEQUENCE_NIL],
        NULL, 0u);
    if (!term)
        return NULL;
    cursor = sequence.token_len;
    while (cursor > 0u) {
        const PPProofGSLTPatternV1 *arguments[2];
        PPProofGSLTPatternV1 *next;
        cursor--;
        if (!sequence.tokens[cursor] ||
            !sequence.tokens[cursor]->term)
            return NULL;
        arguments[0] = sequence.tokens[cursor]->term;
        arguments[1] = term;
        next = ppproof_relational_context_v1_apply(
            impl,
            impl->evidence_abi->constructors[
                PPPROOF_GSLT_SEQUENCE_CONSTRUCTOR_V1_SEQUENCE_CONS],
            arguments, 2u);
        if (!next)
            return NULL;
        term = next;
    }
    return term;
}

PPProofGSLTArticleV1Result
ppproof_gslt_relational_context_v1_add_provable_premise(
    PPProofGSLTRelationalContextV1 *context,
    PPProofGSLTTokenSequenceV1 formula,
    PPProofGSLTReferenceV1 *reference_out,
    char *error_buf,
    size_t error_buf_size) {
    PPProofRelationalContextImplV1 *impl;
    PPProofGSLTPatternV1 *sequence;
    const PPProofGSLTPatternV1 *arguments[1];
    PPProofGSLTPatternV1 *premise;

    if (error_buf && error_buf_size != 0u)
        error_buf[0] = '\0';
    if (!context || !(impl = context->implementation) || !reference_out)
        return PPPROOF_GSLT_ARTICLE_V1_INVALID;
    sequence = ppproof_relational_context_v1_materialize_sequence(
        impl, formula);
    if (!sequence)
        return PPPROOF_GSLT_ARTICLE_V1_RESOURCE;
    arguments[0] = sequence;
    premise = ppproof_relational_context_v1_apply(
        impl,
        impl->evidence_abi->assertion_judgments[
            PPPROOF_GSLT_ASSERTION_JUDGMENT_V1_PROVABLE],
        arguments, 1u);
    if (!premise ||
        !ppproof_relational_context_v1_append_premise(
            impl, premise, true, reference_out))
        return PPPROOF_GSLT_ARTICLE_V1_RESOURCE;
    return PPPROOF_GSLT_ARTICLE_V1_OK;
}

PPProofGSLTArticleV1Result
ppproof_gslt_relational_context_v1_reserve_premise(
    PPProofGSLTRelationalContextV1 *context,
    PPProofGSLTReferenceV1 *reference_out,
    char *error_buf,
    size_t error_buf_size) {
    PPProofRelationalContextImplV1 *impl;

    if (error_buf && error_buf_size != 0u)
        error_buf[0] = '\0';
    if (!context || !(impl = context->implementation) || !reference_out)
        return PPPROOF_GSLT_ARTICLE_V1_INVALID;
    if (!ppproof_relational_context_v1_append_premise(
            impl, NULL, false, reference_out))
        return PPPROOF_GSLT_ARTICLE_V1_RESOURCE;
    return PPPROOF_GSLT_ARTICLE_V1_OK;
}

PPProofGSLTArticleV1Result
ppproof_gslt_relational_context_v1_fill_premise(
    PPProofGSLTRelationalContextV1 *context,
    PPProofGSLTReferenceV1 reference,
    const PPProofGSLTPatternV1 *premise,
    char *error_buf,
    size_t error_buf_size) {
    PPProofRelationalContextImplV1 *impl;

    if (error_buf && error_buf_size != 0u)
        error_buf[0] = '\0';
    if (!context || !(impl = context->implementation) || !premise ||
        reference.kind != PPPROOF_GSLT_REFERENCE_V1_PREMISE ||
        reference.index >= impl->premise_len ||
        impl->premise_filled[reference.index]) {
        ppproof_relational_declaration_v1_set_error(
            error_buf, error_buf_size,
            "invalid reserved relational premise");
        return PPPROOF_GSLT_ARTICLE_V1_INVALID;
    }
    impl->premises[reference.index] = *premise;
    impl->premise_filled[reference.index] = true;
    return PPPROOF_GSLT_ARTICLE_V1_OK;
}

PPProofGSLTArticleV1Result ppproof_gslt_relational_context_v1_view(
    const PPProofGSLTRelationalContextV1 *context,
    const PPProofGSLTPatternV1 **premises_out,
    uint32_t *premise_len_out,
    char *error_buf,
    size_t error_buf_size) {
    const PPProofRelationalContextImplV1 *impl;
    uint32_t index;

    if (error_buf && error_buf_size != 0u)
        error_buf[0] = '\0';
    if (!context || !(impl = context->implementation) ||
        !premises_out || !premise_len_out)
        return PPPROOF_GSLT_ARTICLE_V1_INVALID;
    for (index = 0u; index < impl->premise_len; index++) {
        if (!impl->premise_filled[index]) {
            ppproof_relational_declaration_v1_set_error(
                error_buf, error_buf_size,
                "relational proof context contains an unfilled premise");
            return PPPROOF_GSLT_ARTICLE_V1_INVALID;
        }
    }
    *premises_out = impl->premises;
    *premise_len_out = impl->premise_len;
    return PPPROOF_GSLT_ARTICLE_V1_OK;
}

static bool ppproof_relational_context_v1_relation_evidence(
    PPProofGSLTRelationalEvidenceV1 *evidence,
    PPProofRelationalRelationKindV1 kind,
    const PPProofGSLTSequenceTokenV1 *left,
    const PPProofGSLTSequenceTokenV1 *right,
    PPProofGSLTReferenceV1 *reference_out) {
    PPProofRelationalContextImplV1 *impl;
    PPProofRelationalTokenEntryV1 *left_entry;
    PPProofRelationalTokenEntryV1 *right_entry;
    uint32_t authority;
    uint32_t index;
    const PPProofGSLTPatternV1 *arguments[2];
    PPProofGSLTPatternV1 *premise;
    PPProofGSLTNameV1 judgment;
    PPProofGSLTReferenceV1 reference;

    if (!evidence || !evidence->context ||
        !(impl = evidence->context->implementation) ||
        !left || !right || !reference_out)
        return false;
    left_entry = ppproof_relational_context_v1_token_pointer_entry(
        impl, left);
    right_entry = ppproof_relational_context_v1_token_pointer_entry(
        impl, right);
    if (!left_entry || !right_entry ||
        left_entry->value == right_entry->value)
        return false;
    authority = kind == PPPROOF_RELATIONAL_RELATION_V1_APART
                    ? evidence->active_apartness_table
                    : 0u;
    if (kind == PPPROOF_RELATIONAL_RELATION_V1_APART) {
        uint32_t key[2] = {
            left_entry->value, right_entry->value,
        };
        uint32_t row[2];

        if (!left->variable || !right->variable)
            return false;
        if (!impl->store.table_find(
                impl->store.context, evidence->active_apartness_table,
                key, 2u, row, 2u)) {
            key[0] = right_entry->value;
            key[1] = left_entry->value;
            if (!impl->store.table_find(
                    impl->store.context,
                    evidence->active_apartness_table,
                    key, 2u, row, 2u))
                return false;
        }
    }
    for (index = 0u; index < impl->relation_len; index++) {
        PPProofRelationalRelationEntryV1 *entry =
            &impl->relations[index];
        if (entry->kind == kind && entry->authority == authority &&
            entry->left == left && entry->right == right) {
            *reference_out = entry->reference;
            return true;
        }
    }
    judgment = impl->evidence_abi->judgments[
        kind == PPPROOF_RELATIONAL_RELATION_V1_DIFFERENT
            ? PPPROOF_GSLT_SEQUENCE_JUDGMENT_V1_DIFFERENT
            : PPPROOF_GSLT_SEQUENCE_JUDGMENT_V1_APART];
    arguments[0] = left->term;
    arguments[1] = right->term;
    premise = ppproof_relational_context_v1_apply(
        impl, judgment, arguments, 2u);
    if (!premise ||
        !ppproof_relational_context_v1_append_premise(
            impl, premise, true, &reference) ||
        !ppproof_relational_declaration_v1_grow(
            (void **)&impl->relations, &impl->relation_cap,
            impl->relation_len + 1u, sizeof(*impl->relations)))
        return false;
    impl->relations[impl->relation_len++] =
        (PPProofRelationalRelationEntryV1){
            .kind = kind,
            .authority = authority,
            .left = left,
            .right = right,
            .reference = reference,
        };
    *reference_out = reference;
    return true;
}

static bool ppproof_relational_context_v1_different(
    void *context,
    const PPProofGSLTSequenceTokenV1 *left,
    const PPProofGSLTSequenceTokenV1 *right,
    PPProofGSLTReferenceV1 *evidence_out) {
    return ppproof_relational_context_v1_relation_evidence(
        context, PPPROOF_RELATIONAL_RELATION_V1_DIFFERENT,
        left, right, evidence_out);
}

static bool ppproof_relational_context_v1_apart(
    void *context,
    const PPProofGSLTSequenceTokenV1 *left,
    const PPProofGSLTSequenceTokenV1 *right,
    PPProofGSLTReferenceV1 *evidence_out) {
    return ppproof_relational_context_v1_relation_evidence(
        context, PPPROOF_RELATIONAL_RELATION_V1_APART,
        left, right, evidence_out);
}

PPProofGSLTArticleV1Result
ppproof_gslt_relational_context_v1_evidence_sources(
    PPProofGSLTRelationalContextV1 *context,
    uint32_t active_apartness_table,
    PPProofGSLTRelationalEvidenceV1 *evidence,
    PPProofGSLTSequenceEvidenceSourcesV1 *sources_out,
    char *error_buf,
    size_t error_buf_size) {
    if (error_buf && error_buf_size != 0u)
        error_buf[0] = '\0';
    if (!context || !context->implementation || !evidence ||
        !sources_out) {
        ppproof_relational_declaration_v1_set_error(
            error_buf, error_buf_size,
            "invalid relational evidence source request");
        return PPPROOF_GSLT_ARTICLE_V1_INVALID;
    }
    {
        PPProofRelationalContextImplV1 *impl = context->implementation;
        uint32_t arity = 0u;
        uint32_t key_arity = 0u;
        uint32_t row_len = 0u;

        if (!impl->store.table_shape(
                impl->store.context, active_apartness_table,
                &arity, &key_arity, &row_len) ||
            arity != 2u || key_arity != 2u) {
            ppproof_relational_declaration_v1_set_error(
                error_buf, error_buf_size,
                "active apartness source has an incompatible shape");
            return PPPROOF_GSLT_ARTICLE_V1_INVALID;
        }
    }
    *evidence = (PPProofGSLTRelationalEvidenceV1){
        .context = context,
        .active_apartness_table = active_apartness_table,
    };
    *sources_out = (PPProofGSLTSequenceEvidenceSourcesV1){
        .context = evidence,
        .different = ppproof_relational_context_v1_different,
        .apart = ppproof_relational_context_v1_apart,
    };
    return PPPROOF_GSLT_ARTICLE_V1_OK;
}

static bool ppproof_relational_declaration_v1_prefix_next(
    const PPProofRelationalContextImplV1 *impl,
    PPProofGSLTRelationalTableRoleV1 role,
    uint32_t prefix,
    uint32_t column_mask,
    uint64_t *cursor,
    uint32_t *row,
    uint32_t row_capacity,
    bool *found_out) {
    return impl && cursor && row && found_out &&
           impl->store.table_prefix_next(
               impl->store.context,
               impl->relational_plan->tables[role],
               &prefix, 1u, column_mask,
               cursor, row, row_capacity, found_out);
}

static bool ppproof_relational_declaration_v1_contains(
    const uint32_t *values, uint32_t len, uint32_t value) {
    uint32_t index;
    for (index = 0u; index < len; index++) {
        if (values[index] == value)
            return true;
    }
    return false;
}

static int ppproof_relational_declaration_v1_order_compare(
    const void *left_argument, const void *right_argument) {
    const PPProofRelationalOrderedRowV1 *left = left_argument;
    const PPProofRelationalOrderedRowV1 *right = right_argument;
    if (left->position < right->position)
        return -1;
    if (left->position > right->position)
        return 1;
    return 0;
}

static bool ppproof_relational_declaration_v1_reference_valid(
    PPProofGSLTReferenceV1 reference) {
    return reference.kind == PPPROOF_GSLT_REFERENCE_V1_PREMISE ||
           reference.kind == PPPROOF_GSLT_REFERENCE_V1_NODE;
}

void ppproof_gslt_relational_declaration_v1_init(
    PPProofGSLTRelationalDeclarationV1 *declaration) {
    if (declaration)
        memset(declaration, 0, sizeof(*declaration));
}

void ppproof_gslt_relational_declaration_v1_free(
    PPProofGSLTRelationalDeclarationV1 *declaration) {
    PPProofRelationalDeclarationStorageV1 *storage;

    if (!declaration)
        return;
    storage = declaration->storage;
    if (storage) {
        free(storage->bindings);
        free(storage->essentials);
        free(storage->disjoints);
        free(storage->ordered);
        free(storage);
    }
    memset(declaration, 0, sizeof(*declaration));
}

PPProofGSLTArticleV1Result ppproof_gslt_relational_declaration_v1_elaborate(
    PPProofGSLTRelationalContextV1 *context,
    uint32_t assertion_value,
    PPProofGSLTRelationalDeclarationV1 *declaration,
    char *error_buf,
    size_t error_buf_size) {
    PPProofRelationalContextImplV1 *impl;
    PPProofGSLTRelationalDeclarationV1 result;
    PPProofRelationalDeclarationStorageV1 *storage = NULL;
    uint32_t *mandatory = NULL;
    uint32_t mandatory_len = 0u;
    uint32_t mandatory_cap = 0u;
    PPProofRelationalOrderedRowV1 *ordered_rows = NULL;
    uint32_t ordered_row_len = 0u;
    uint32_t ordered_row_cap = 0u;
    uint64_t cursor = UINT64_MAX;
    uint32_t index;
    PPProofGSLTArticleV1Result failure =
        PPPROOF_GSLT_ARTICLE_V1_INVALID;

    ppproof_gslt_relational_declaration_v1_init(&result);
    if (error_buf && error_buf_size != 0u)
        error_buf[0] = '\0';
    if (!context || !(impl = context->implementation) || !declaration) {
        ppproof_relational_declaration_v1_set_error(
            error_buf, error_buf_size,
            "invalid relational declaration elaboration request");
        return PPPROOF_GSLT_ARTICLE_V1_INVALID;
    }
    storage = calloc(1u, sizeof(*storage));
    if (!storage)
        return PPPROOF_GSLT_ARTICLE_V1_RESOURCE;
    result.assertion_value = assertion_value;

    for (;;) {
        uint32_t row[2];
        bool found = false;
        if (!ppproof_relational_declaration_v1_prefix_next(
                impl,
                PPPROOF_GSLT_RELATIONAL_TABLE_V1_MANDATORY_VARIABLE,
                assertion_value, UINT32_C(1) << 1u,
                &cursor, row, 2u, &found))
            goto malformed;
        if (!found)
            break;
        if (ppproof_relational_declaration_v1_contains(
                mandatory, mandatory_len, row[1]))
            goto malformed;
        if (mandatory_len >=
                impl->limits.maximum_materialized_pattern_nodes ||
            !ppproof_relational_declaration_v1_grow(
                (void **)&mandatory, &mandatory_cap,
                mandatory_len + 1u, sizeof(*mandatory))) {
            failure = PPPROOF_GSLT_ARTICLE_V1_RESOURCE;
            goto failed;
        }
        mandatory[mandatory_len++] = row[1];
    }

    cursor = UINT64_MAX;
    for (;;) {
        uint32_t row[3];
        uint32_t position;
        bool found = false;
        if (!ppproof_relational_declaration_v1_prefix_next(
                impl,
                PPPROOF_GSLT_RELATIONAL_TABLE_V1_ORDERED_HYPOTHESIS,
                assertion_value,
                (UINT32_C(1) << 1u) | (UINT32_C(1) << 2u),
                &cursor, row, 3u, &found))
            goto malformed;
        if (!found)
            break;
        if (!pprelational_store_v1_value_u32_decimal(
                &impl->store, row[1], &position))
            goto malformed;
        if (ordered_row_len >=
                impl->limits.maximum_materialized_pattern_nodes ||
            !ppproof_relational_declaration_v1_grow(
                (void **)&ordered_rows, &ordered_row_cap,
                ordered_row_len + 1u, sizeof(*ordered_rows))) {
            failure = PPPROOF_GSLT_ARTICLE_V1_RESOURCE;
            goto failed;
        }
        ordered_rows[ordered_row_len++] =
            (PPProofRelationalOrderedRowV1){position, row[2]};
    }
    qsort(ordered_rows, ordered_row_len,
          sizeof(*ordered_rows),
          ppproof_relational_declaration_v1_order_compare);
    for (index = 1u; index < ordered_row_len; index++) {
        if (ordered_rows[index - 1u].position ==
            ordered_rows[index].position)
            goto malformed;
    }

    for (index = 0u; index < ordered_row_len; index++) {
        uint32_t label = ordered_rows[index].label;
        uint32_t kind_row[2];
        uint32_t kind;
        PPProofGSLTRelationalOrderedHypothesisV1 ordered;

        if (!ppproof_relational_context_v1_find(
                impl, PPPROOF_GSLT_RELATIONAL_TABLE_V1_LABEL_KIND,
                &label, 1u, kind_row, 2u))
            goto malformed;
        kind = kind_row[1];
        if (kind == impl->selector_values[
                PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_HYPOTHESIS_FLOATING]) {
            uint32_t variable_row[2];
            uint32_t formula_row[2];
            PPProofGSLTTokenSequenceV1 formula;
            const PPProofGSLTSequenceTokenV1 *variable;
            const PPProofGSLTSequenceTokenV1 *typecode;
            PPProofGSLTArticleV1Result elaborated;
            uint32_t binding_index;

            if (!ppproof_relational_context_v1_find(
                    impl,
                    PPPROOF_GSLT_RELATIONAL_TABLE_V1_FLOATING_VARIABLE,
                    &label, 1u, variable_row, 2u))
                goto malformed;
            if (!ppproof_relational_declaration_v1_contains(
                    mandatory, mandatory_len, variable_row[1]))
                continue;
            if (!ppproof_relational_context_v1_find(
                    impl, PPPROOF_GSLT_RELATIONAL_TABLE_V1_FORMULA,
                    &label, 1u, formula_row, 2u))
                goto malformed;
            elaborated = ppproof_relational_context_v1_token(
                impl, variable_row[1], &variable,
                error_buf, error_buf_size);
            if (elaborated == PPPROOF_GSLT_ARTICLE_V1_OK)
                elaborated = ppproof_relational_context_v1_formula_impl(
                    impl, formula_row[1], &formula,
                    error_buf, error_buf_size);
            if (elaborated != PPPROOF_GSLT_ARTICLE_V1_OK) {
                failure = elaborated;
                goto failed;
            }
            if (!variable->variable || formula.token_len != 2u ||
                formula.tokens[1] != variable ||
                !formula.tokens[0]->literal)
                goto malformed;
            typecode = formula.tokens[0];
            for (binding_index = 0u;
                 binding_index < result.binding_len; binding_index++) {
                if (storage->bindings[binding_index].variable == variable)
                    goto malformed;
            }
            if (!ppproof_relational_declaration_v1_grow(
                    (void **)&storage->bindings,
                    &storage->binding_cap,
                    result.binding_len + 1u,
                    sizeof(*storage->bindings))) {
                failure = PPPROOF_GSLT_ARTICLE_V1_RESOURCE;
                goto failed;
            }
            storage->bindings[result.binding_len] =
                (PPProofGSLTRelationalBindingSchemaV1){
                    .variable = variable,
                    .typecode = typecode,
                    .hypothesis_value = label,
                };
            ordered = (PPProofGSLTRelationalOrderedHypothesisV1){
                .kind = PPPROOF_GSLT_RELATIONAL_HYPOTHESIS_V1_BINDING,
                .schema_index = result.binding_len++,
                .source_position = ordered_rows[index].position,
            };
        } else if (kind == impl->selector_values[
                       PPPROOF_GSLT_RELATIONAL_SELECTOR_V1_HYPOTHESIS_ESSENTIAL]) {
            uint32_t formula_row[2];
            PPProofGSLTTokenSequenceV1 formula;
            PPProofGSLTArticleV1Result elaborated;

            if (!ppproof_relational_context_v1_find(
                    impl, PPPROOF_GSLT_RELATIONAL_TABLE_V1_FORMULA,
                    &label, 1u, formula_row, 2u))
                goto malformed;
            elaborated = ppproof_relational_context_v1_formula_impl(
                impl, formula_row[1], &formula,
                error_buf, error_buf_size);
            if (elaborated != PPPROOF_GSLT_ARTICLE_V1_OK) {
                failure = elaborated;
                goto failed;
            }
            if (formula.token_len == 0u)
                goto malformed;
            if (!ppproof_relational_declaration_v1_grow(
                    (void **)&storage->essentials,
                    &storage->essential_cap,
                    result.essential_len + 1u,
                    sizeof(*storage->essentials))) {
                failure = PPPROOF_GSLT_ARTICLE_V1_RESOURCE;
                goto failed;
            }
            storage->essentials[result.essential_len] =
                (PPProofGSLTRelationalEssentialSchemaV1){
                    .template_sequence = formula,
                    .hypothesis_value = label,
                };
            ordered = (PPProofGSLTRelationalOrderedHypothesisV1){
                .kind = PPPROOF_GSLT_RELATIONAL_HYPOTHESIS_V1_ESSENTIAL,
                .schema_index = result.essential_len++,
                .source_position = ordered_rows[index].position,
            };
        } else {
            goto malformed;
        }
        if (!ppproof_relational_declaration_v1_grow(
                (void **)&storage->ordered, &storage->ordered_cap,
                result.ordered_len + 1u,
                sizeof(*storage->ordered))) {
            failure = PPPROOF_GSLT_ARTICLE_V1_RESOURCE;
            goto failed;
        }
        storage->ordered[result.ordered_len++] = ordered;
    }

    if (result.binding_len != mandatory_len)
        goto malformed;
    for (index = 0u; index < mandatory_len; index++) {
        uint32_t binding_index;
        bool found = false;
        for (binding_index = 0u;
             binding_index < result.binding_len; binding_index++) {
            PPProofRelationalTokenEntryV1 *entry =
                ppproof_relational_context_v1_token_pointer_entry(
                    impl, storage->bindings[binding_index].variable);
            if (entry && entry->value == mandatory[index]) {
                found = true;
                break;
            }
        }
        if (!found)
            goto malformed;
    }

    {
        uint32_t formula_row[2];
        PPProofGSLTTokenSequenceV1 formula;
        PPProofGSLTArticleV1Result elaborated;

        if (!ppproof_relational_context_v1_find(
                impl, PPPROOF_GSLT_RELATIONAL_TABLE_V1_FORMULA,
                &assertion_value, 1u, formula_row, 2u))
            goto malformed;
        elaborated = ppproof_relational_context_v1_formula_impl(
            impl, formula_row[1], &formula,
            error_buf, error_buf_size);
        if (elaborated != PPPROOF_GSLT_ARTICLE_V1_OK) {
            failure = elaborated;
            goto failed;
        }
        if (formula.token_len == 0u || !formula.tokens[0]->literal)
            goto malformed;
        result.conclusion_type = formula.tokens[0]->term;
        result.conclusion_template = (PPProofGSLTTokenSequenceV1){
            .tokens = formula.tokens + 1u,
            .token_len = formula.token_len - 1u,
        };
    }

    cursor = UINT64_MAX;
    for (;;) {
        uint32_t row[3];
        bool found = false;
        const PPProofGSLTSequenceTokenV1 *left;
        const PPProofGSLTSequenceTokenV1 *right;
        PPProofGSLTArticleV1Result elaborated;
        uint32_t prior;

        if (!ppproof_relational_declaration_v1_prefix_next(
                impl,
                PPPROOF_GSLT_RELATIONAL_TABLE_V1_ASSERTION_DISJOINT,
                assertion_value,
                (UINT32_C(1) << 1u) | (UINT32_C(1) << 2u),
                &cursor, row, 3u, &found))
            goto malformed;
        if (!found)
            break;
        if (!ppproof_relational_declaration_v1_contains(
                mandatory, mandatory_len, row[1]) ||
            !ppproof_relational_declaration_v1_contains(
                mandatory, mandatory_len, row[2]))
            continue;
        elaborated = ppproof_relational_context_v1_token(
            impl, row[1], &left, error_buf, error_buf_size);
        if (elaborated == PPPROOF_GSLT_ARTICLE_V1_OK)
            elaborated = ppproof_relational_context_v1_token(
                impl, row[2], &right, error_buf, error_buf_size);
        if (elaborated != PPPROOF_GSLT_ARTICLE_V1_OK) {
            failure = elaborated;
            goto failed;
        }
        if (!left->variable || !right->variable || left == right)
            goto malformed;
        for (prior = 0u; prior < result.disjoint_len; prior++) {
            const PPProofGSLTAssertionDisjointV1 *existing =
                &storage->disjoints[prior];
            if ((existing->left == left && existing->right == right) ||
                (existing->left == right && existing->right == left))
                goto malformed;
        }
        if (!ppproof_relational_declaration_v1_grow(
                (void **)&storage->disjoints,
                &storage->disjoint_cap,
                result.disjoint_len + 1u,
                sizeof(*storage->disjoints))) {
            failure = PPPROOF_GSLT_ARTICLE_V1_RESOURCE;
            goto failed;
        }
        storage->disjoints[result.disjoint_len++] =
            (PPProofGSLTAssertionDisjointV1){left, right};
    }

    result.bindings = storage->bindings;
    result.essentials = storage->essentials;
    result.disjoints = storage->disjoints;
    result.ordered = storage->ordered;
    result.storage = storage;
    free(mandatory);
    free(ordered_rows);
    ppproof_gslt_relational_declaration_v1_free(declaration);
    *declaration = result;
    return PPPROOF_GSLT_ARTICLE_V1_OK;

malformed:
    ppproof_relational_declaration_v1_set_error(
        error_buf, error_buf_size,
        "relational assertion rows do not elaborate coherently");
failed:
    free(mandatory);
    free(ordered_rows);
    if (storage) {
        free(storage->bindings);
        free(storage->essentials);
        free(storage->disjoints);
        free(storage->ordered);
        free(storage);
    }
    return failure;
}

void ppproof_gslt_relational_prepared_assertion_v1_init(
    PPProofGSLTRelationalPreparedAssertionV1 *prepared) {
    if (prepared)
        memset(prepared, 0, sizeof(*prepared));
}

void ppproof_gslt_relational_prepared_assertion_v1_free(
    PPProofGSLTRelationalPreparedAssertionV1 *prepared) {
    PPProofRelationalPreparedStorageV1 *storage;

    if (!prepared)
        return;
    storage = prepared->storage;
    if (storage) {
        free(storage->bindings);
        free(storage->essentials);
        free(storage);
    }
    memset(prepared, 0, sizeof(*prepared));
}

PPProofGSLTArticleV1Result
ppproof_gslt_relational_prepared_assertion_v1_build(
    const PPProofGSLTRelationalDeclarationV1 *schema,
    const PPProofGSLTRelationalActualHypothesisV1 *actuals,
    uint32_t actual_len,
    PPProofGSLTRelationalPreparedAssertionV1 *prepared,
    char *error_buf,
    size_t error_buf_size) {
    PPProofGSLTRelationalPreparedAssertionV1 result;
    PPProofRelationalPreparedStorageV1 *storage;
    uint32_t index;

    ppproof_gslt_relational_prepared_assertion_v1_init(&result);
    if (error_buf && error_buf_size != 0u)
        error_buf[0] = '\0';
    if (!schema || !schema->storage || !prepared ||
        actual_len != schema->ordered_len ||
        (actual_len != 0u && !actuals)) {
        ppproof_relational_declaration_v1_set_error(
            error_buf, error_buf_size,
            "invalid relational assertion preparation request");
        return PPPROOF_GSLT_ARTICLE_V1_INVALID;
    }
    storage = calloc(1u, sizeof(*storage));
    if (!storage)
        return PPPROOF_GSLT_ARTICLE_V1_RESOURCE;
    if (schema->binding_len != 0u) {
        if ((size_t)schema->binding_len >
            SIZE_MAX / sizeof(*storage->bindings))
            goto resource;
        storage->bindings = calloc(
            schema->binding_len, sizeof(*storage->bindings));
        if (!storage->bindings)
            goto resource;
    }
    if (schema->essential_len != 0u) {
        if ((size_t)schema->essential_len >
            SIZE_MAX / sizeof(*storage->essentials))
            goto resource;
        storage->essentials = calloc(
            schema->essential_len, sizeof(*storage->essentials));
        if (!storage->essentials)
            goto resource;
    }
    for (index = 0u; index < actual_len; index++) {
        const PPProofGSLTRelationalOrderedHypothesisV1 *ordered =
            &schema->ordered[index];
        const PPProofGSLTRelationalActualHypothesisV1 *actual =
            &actuals[index];

        if ((actual->formula.token_len != 0u &&
             !actual->formula.tokens) ||
            !ppproof_relational_declaration_v1_reference_valid(
                actual->proof))
            goto malformed;
        if (ordered->kind ==
            PPPROOF_GSLT_RELATIONAL_HYPOTHESIS_V1_BINDING) {
            const PPProofGSLTRelationalBindingSchemaV1 *binding;
            if (ordered->schema_index >= schema->binding_len ||
                actual->formula.token_len == 0u)
                goto malformed;
            binding = &schema->bindings[ordered->schema_index];
            if (actual->formula.tokens[0] != binding->typecode)
                goto malformed;
            storage->bindings[ordered->schema_index] =
                (PPProofGSLTAssertionBindingV1){
                    .variable = binding->variable,
                    .typecode = binding->typecode->term,
                    .image = {
                        .tokens = actual->formula.tokens + 1u,
                        .token_len = actual->formula.token_len - 1u,
                    },
                    .floating_proof = actual->proof,
                };
        } else if (ordered->kind ==
                   PPPROOF_GSLT_RELATIONAL_HYPOTHESIS_V1_ESSENTIAL) {
            if (ordered->schema_index >= schema->essential_len)
                goto malformed;
            storage->essentials[ordered->schema_index] =
                (PPProofGSLTAssertionEssentialV1){
                    .template_sequence =
                        schema->essentials[ordered->schema_index]
                            .template_sequence,
                    .actual_proof = actual->proof,
                };
        } else {
            goto malformed;
        }
    }
    result.declaration = (PPProofGSLTAssertionDeclarationV1){
        .bindings = storage->bindings,
        .binding_len = schema->binding_len,
        .essentials = storage->essentials,
        .essential_len = schema->essential_len,
        .disjoints = schema->disjoints,
        .disjoint_len = schema->disjoint_len,
        .conclusion_type = schema->conclusion_type,
        .conclusion_template = schema->conclusion_template,
    };
    result.storage = storage;
    ppproof_gslt_relational_prepared_assertion_v1_free(prepared);
    *prepared = result;
    return PPPROOF_GSLT_ARTICLE_V1_OK;

malformed:
    ppproof_relational_declaration_v1_set_error(
        error_buf, error_buf_size,
        "actual hypotheses do not match the relational declaration");
    free(storage->bindings);
    free(storage->essentials);
    free(storage);
    return PPPROOF_GSLT_ARTICLE_V1_REJECTED;

resource:
    free(storage->bindings);
    free(storage->essentials);
    free(storage);
    return PPPROOF_GSLT_ARTICLE_V1_RESOURCE;
}
