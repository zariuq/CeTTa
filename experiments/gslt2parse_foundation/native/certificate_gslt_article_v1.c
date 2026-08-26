#include "certificate_gslt_article_v1.h"
#include "gslt_chronological_builder_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t remaining;
    uint32_t used;
    uint32_t maximum_depth;
} PPCertificateGSLTMaterialBudgetV1;

typedef struct {
    const PPCertificateGSLTPresentationV1 *presentation;
    const PPCertificateGSLTRuleSchemaV1 *rule;
    uint32_t remaining;
    uint32_t maximum_depth;
    uint32_t used_capabilities;
    PPCertificateGSLTArticleV1Result failure_result;
    bool schema_mode;
    char *error_buf;
    size_t error_buf_size;
} PPCertificateGSLTValidationV1;

typedef struct {
    const PPCertificateGSLTPatternV1 *pattern;
    uint32_t ambient_depth;
    uint32_t traversal_depth;
    bool judgment_root;
    bool finish;
} PPCertificateGSLTGroundValidationFrameV1;

typedef struct {
    const PPCertificateGSLTPatternV1 *pattern;
    uint32_t ambient_depth;
    uint32_t maximum_relative_depth;
    bool judgment_root;
    bool occupied;
} PPCertificateGSLTGroundValidationCacheEntryV1;

typedef struct {
    PPCertificateGSLTGroundValidationCacheEntryV1 *entries;
    PPCertificateGSLTGroundValidationFrameV1 *frames;
    uint32_t len;
    uint32_t cap;
    uint32_t frame_cap;
    uint32_t maximum_entries;
} PPCertificateGSLTGroundValidationCacheV1;

typedef struct {
    uint32_t id;
    uint32_t index;
    bool occupied;
} PPCertificateGSLTArticleIdIndexEntryV1;

typedef struct {
    PPCertificateGSLTArticleIdIndexEntryV1 *entries;
    uint32_t cap;
} PPCertificateGSLTArticleIdIndexV1;

static void ppcertificate_gslt_v1_set_error(char *buf, size_t size,
                                      const char *format, ...) {
    va_list args;

    if (!buf || size == 0u)
        return;
    va_start(args, format);
    (void)vsnprintf(buf, size, format, args);
    va_end(args);
}

PPCertificateGSLTArticleV1Limits ppcertificate_gslt_article_v1_default_limits(void) {
    return (PPCertificateGSLTArticleV1Limits){
        .maximum_pattern_depth = UINT32_C(1000000),
        .maximum_presentation_pattern_nodes = UINT32_C(10000000),
        .maximum_materialized_pattern_nodes = UINT32_C(40000000),
        .maximum_article_nodes = UINT32_C(4000000),
    };
}

bool ppcertificate_gslt_article_v1_name_equal(PPCertificateGSLTNameV1 left,
                                        PPCertificateGSLTNameV1 right) {
    if (left.len != right.len)
        return false;
    if (left.len == 0u)
        return true;
    return left.bytes && right.bytes &&
           memcmp(left.bytes, right.bytes, left.len) == 0;
}

static bool ppcertificate_gslt_v1_name_well_formed(PPCertificateGSLTNameV1 name,
                                             bool allow_empty) {
    return (allow_empty || name.len != 0u) &&
           (name.len == 0u || name.bytes != NULL);
}

static bool ppcertificate_gslt_v1_array_well_formed(const void *items,
                                              uint32_t len) {
    return len == 0u || items != NULL;
}

static bool ppcertificate_gslt_v1_allocation_fits(uint32_t count,
                                           size_t item_size) {
    return count == 0u || item_size <= SIZE_MAX / (size_t)count;
}

static bool ppcertificate_gslt_v1_add_depth(uint32_t depth, uint32_t amount,
                                      uint32_t *out) {
    if (UINT32_MAX - depth < amount)
        return false;
    *out = depth + amount;
    return true;
}

static const PPCertificateGSLTConstructorV1 *ppcertificate_gslt_v1_find_constructor(
    const PPCertificateGSLTPresentationV1 *presentation,
    PPCertificateGSLTNameV1 name) {
    uint32_t index;

    for (index = 0u; index < presentation->constructor_len; index++) {
        if (ppcertificate_gslt_article_v1_name_equal(
                presentation->constructors[index].name, name))
            return &presentation->constructors[index];
    }
    return NULL;
}

static const PPCertificateGSLTJudgmentV1 *ppcertificate_gslt_v1_find_judgment(
    const PPCertificateGSLTPresentationV1 *presentation,
    PPCertificateGSLTNameV1 name) {
    uint32_t index;

    for (index = 0u; index < presentation->judgment_len; index++) {
        if (ppcertificate_gslt_article_v1_name_equal(
                presentation->judgments[index].head, name))
            return &presentation->judgments[index];
    }
    return NULL;
}

static const PPCertificateGSLTRuleSchemaV1 *ppcertificate_gslt_v1_find_rule(
    const PPCertificateGSLTPresentationV1 *presentation,
    PPCertificateGSLTNameV1 id) {
    uint32_t index;

    for (index = 0u; index < presentation->rule_len; index++) {
        if (ppcertificate_gslt_article_v1_name_equal(
                presentation->rules[index].id, id))
            return &presentation->rules[index];
    }
    return NULL;
}

static int32_t ppcertificate_gslt_v1_find_formal(
    const PPCertificateGSLTRuleSchemaV1 *rule,
    PPCertificateGSLTNameV1 name, uint32_t depth) {
    uint32_t index;

    if (!rule)
        return -1;
    for (index = 0u; index < rule->formal_len; index++) {
        if (rule->formals[index].depth == depth &&
            ppcertificate_gslt_article_v1_name_equal(
                rule->formals[index].name, name))
            return (int32_t)index;
    }
    return -1;
}

static bool ppcertificate_gslt_v1_validate_pattern(
    PPCertificateGSLTValidationV1 *validation,
    const PPCertificateGSLTPatternV1 *pattern,
    uint32_t ambient_depth,
    uint32_t traversal_depth,
    bool judgment_root);

static bool ppcertificate_gslt_v1_validate_pattern_array(
    PPCertificateGSLTValidationV1 *validation,
    const PPCertificateGSLTPatternV1 *patterns,
    uint32_t pattern_len,
    uint32_t ambient_depth,
    uint32_t traversal_depth) {
    uint32_t index;

    if (!ppcertificate_gslt_v1_array_well_formed(patterns, pattern_len)) {
        ppcertificate_gslt_v1_set_error(
            validation->error_buf, validation->error_buf_size,
            "pattern vector has no storage");
        return false;
    }
    for (index = 0u; index < pattern_len; index++) {
        if (!ppcertificate_gslt_v1_validate_pattern(
                validation, &patterns[index], ambient_depth,
                traversal_depth, false))
            return false;
    }
    return true;
}

static bool ppcertificate_gslt_v1_validate_pattern(
    PPCertificateGSLTValidationV1 *validation,
    const PPCertificateGSLTPatternV1 *pattern,
    uint32_t ambient_depth,
    uint32_t traversal_depth,
    bool judgment_root) {
    uint32_t inner_depth;
    uint32_t child_traversal_depth;

    if (!pattern) {
        ppcertificate_gslt_v1_set_error(
            validation->error_buf, validation->error_buf_size,
            "pattern is absent");
        return false;
    }
    if (traversal_depth > validation->maximum_depth) {
        validation->failure_result = PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
        ppcertificate_gslt_v1_set_error(
            validation->error_buf, validation->error_buf_size,
            "pattern validation depth %u exceeds limit %u",
            traversal_depth, validation->maximum_depth);
        return false;
    }
    if (validation->remaining == 0u) {
        validation->failure_result = PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
        ppcertificate_gslt_v1_set_error(
            validation->error_buf, validation->error_buf_size,
            "pattern validation exhausts its node budget");
        return false;
    }
    validation->remaining--;
    if (judgment_root &&
        pattern->kind != PPCERTIFICATE_GSLT_PATTERN_V1_APPLY) {
        ppcertificate_gslt_v1_set_error(
            validation->error_buf, validation->error_buf_size,
            "judgment pattern root is not an application");
        return false;
    }

    switch (pattern->kind) {
    case PPCERTIFICATE_GSLT_PATTERN_V1_BVAR:
        if (pattern->as.bvar >= ambient_depth) {
            ppcertificate_gslt_v1_set_error(
                validation->error_buf, validation->error_buf_size,
                "pattern contains an out-of-scope bound variable");
            return false;
        }
        return true;
    case PPCERTIFICATE_GSLT_PATTERN_V1_FVAR:
        if (!validation->schema_mode ||
            !ppcertificate_gslt_v1_name_well_formed(pattern->as.fvar, false) ||
            ppcertificate_gslt_v1_find_formal(
                validation->rule, pattern->as.fvar, ambient_depth) < 0) {
            ppcertificate_gslt_v1_set_error(
                validation->error_buf, validation->error_buf_size,
                "pattern contains an invalid free metavariable occurrence");
            return false;
        }
        return true;
    case PPCERTIFICATE_GSLT_PATTERN_V1_APPLY: {
        uint32_t expected_arity = 0u;
        bool declared_shape = false;

        if (!ppcertificate_gslt_v1_name_well_formed(
                pattern->as.apply.constructor, false)) {
            ppcertificate_gslt_v1_set_error(
                validation->error_buf, validation->error_buf_size,
                "application constructor is empty");
            return false;
        }
        if (judgment_root) {
            const PPCertificateGSLTJudgmentV1 *judgment =
                ppcertificate_gslt_v1_find_judgment(
                    validation->presentation,
                    pattern->as.apply.constructor);
            if (!judgment) {
                ppcertificate_gslt_v1_set_error(
                    validation->error_buf, validation->error_buf_size,
                    "pattern has an undeclared judgment head");
                return false;
            }
            expected_arity = judgment->arity;
            declared_shape = true;
        } else if (validation->schema_mode) {
            const PPCertificateGSLTConstructorV1 *constructor =
                ppcertificate_gslt_v1_find_constructor(
                    validation->presentation,
                    pattern->as.apply.constructor);
            if (!constructor) {
                ppcertificate_gslt_v1_set_error(
                    validation->error_buf, validation->error_buf_size,
                    "pattern has an undeclared data constructor");
                return false;
            }
            expected_arity = constructor->arity;
            declared_shape = true;
        }
        /* Authored schema constructors are checked against the five-field
         * language.  Runtime metavariable payloads are only required to be
         * ground and structurally canonical; their carrier is established by
         * the surrounding authored judgments and premises. */
        if (declared_shape &&
            expected_arity != pattern->as.apply.argument_len) {
            ppcertificate_gslt_v1_set_error(
                validation->error_buf, validation->error_buf_size,
                "application arity disagrees with its declaration");
            return false;
        }
        if (pattern->as.apply.argument_len != 0u &&
            !ppcertificate_gslt_v1_add_depth(
                traversal_depth, 1u, &child_traversal_depth)) {
            validation->failure_result =
                PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
            ppcertificate_gslt_v1_set_error(
                validation->error_buf, validation->error_buf_size,
                "pattern validation depth overflows");
            return false;
        }
        return ppcertificate_gslt_v1_validate_pattern_array(
            validation, pattern->as.apply.arguments,
            pattern->as.apply.argument_len, ambient_depth,
            pattern->as.apply.argument_len == 0u
                ? traversal_depth
                : child_traversal_depth);
    }
    case PPCERTIFICATE_GSLT_PATTERN_V1_LAMBDA:
        validation->used_capabilities |=
            PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_BINDERS;
        if (pattern->as.lambda.binder_name_present ||
            !pattern->as.lambda.body ||
            !ppcertificate_gslt_v1_add_depth(ambient_depth, 1u, &inner_depth)) {
            ppcertificate_gslt_v1_set_error(
                validation->error_buf, validation->error_buf_size,
                "lambda pattern has noncanonical metadata or invalid depth");
            return false;
        }
        if (!ppcertificate_gslt_v1_add_depth(
                traversal_depth, 1u, &child_traversal_depth)) {
            validation->failure_result =
                PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
            ppcertificate_gslt_v1_set_error(
                validation->error_buf, validation->error_buf_size,
                "pattern validation depth overflows");
            return false;
        }
        return ppcertificate_gslt_v1_validate_pattern(
            validation, pattern->as.lambda.body, inner_depth,
            child_traversal_depth, false);
    case PPCERTIFICATE_GSLT_PATTERN_V1_MULTI_LAMBDA:
        validation->used_capabilities |=
            PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_BINDERS;
        if (pattern->as.multi_lambda.binder_name_len != 0u ||
            pattern->as.multi_lambda.binder_names != NULL ||
            !pattern->as.multi_lambda.body ||
            !ppcertificate_gslt_v1_add_depth(
                ambient_depth, pattern->as.multi_lambda.arity,
                &inner_depth)) {
            ppcertificate_gslt_v1_set_error(
                validation->error_buf, validation->error_buf_size,
                "multi-binder pattern has noncanonical metadata or invalid depth");
            return false;
        }
        if (!ppcertificate_gslt_v1_add_depth(
                traversal_depth, 1u, &child_traversal_depth)) {
            validation->failure_result =
                PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
            ppcertificate_gslt_v1_set_error(
                validation->error_buf, validation->error_buf_size,
                "pattern validation depth overflows");
            return false;
        }
        return ppcertificate_gslt_v1_validate_pattern(
            validation, pattern->as.multi_lambda.body, inner_depth,
            child_traversal_depth, false);
    case PPCERTIFICATE_GSLT_PATTERN_V1_SUBST:
        validation->used_capabilities |=
            PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_EXPLICIT_SUBSTITUTION;
        if (!pattern->as.subst.body || !pattern->as.subst.replacement ||
            !ppcertificate_gslt_v1_add_depth(ambient_depth, 1u, &inner_depth)) {
            ppcertificate_gslt_v1_set_error(
                validation->error_buf, validation->error_buf_size,
                "explicit-substitution pattern is malformed");
            return false;
        }
        if (!ppcertificate_gslt_v1_add_depth(
                traversal_depth, 1u, &child_traversal_depth)) {
            validation->failure_result =
                PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
            ppcertificate_gslt_v1_set_error(
                validation->error_buf, validation->error_buf_size,
                "pattern validation depth overflows");
            return false;
        }
        return ppcertificate_gslt_v1_validate_pattern(
                   validation, pattern->as.subst.body, inner_depth,
                   child_traversal_depth, false) &&
               ppcertificate_gslt_v1_validate_pattern(
                   validation, pattern->as.subst.replacement,
                   ambient_depth, child_traversal_depth, false);
    case PPCERTIFICATE_GSLT_PATTERN_V1_COLLECTION:
        validation->used_capabilities |=
            PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_COLLECTIONS;
        if (pattern->as.collection.collection_kind >
                PPCERTIFICATE_GSLT_COLLECTION_V1_HASH_SET ||
            !ppcertificate_gslt_v1_array_well_formed(
                pattern->as.collection.elements,
                pattern->as.collection.element_len)) {
            ppcertificate_gslt_v1_set_error(
                validation->error_buf, validation->error_buf_size,
                "collection pattern is malformed");
            return false;
        }
        if (pattern->as.collection.rest_present) {
            validation->used_capabilities |=
                PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_COLLECTION_REST;
            validation->failure_result =
                PPCERTIFICATE_GSLT_ARTICLE_V1_UNSUPPORTED;
            ppcertificate_gslt_v1_set_error(
                validation->error_buf, validation->error_buf_size,
                "collection-rest patterns are not supported by this checker");
            return false;
        }
        if (pattern->as.collection.element_len != 0u &&
            !ppcertificate_gslt_v1_add_depth(
                traversal_depth, 1u, &child_traversal_depth)) {
            validation->failure_result =
                PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
            ppcertificate_gslt_v1_set_error(
                validation->error_buf, validation->error_buf_size,
                "pattern validation depth overflows");
            return false;
        }
        return ppcertificate_gslt_v1_validate_pattern_array(
            validation, pattern->as.collection.elements,
            pattern->as.collection.element_len, ambient_depth,
            pattern->as.collection.element_len == 0u
                ? traversal_depth
                : child_traversal_depth);
    default:
        ppcertificate_gslt_v1_set_error(
            validation->error_buf, validation->error_buf_size,
            "pattern kind is unknown");
        return false;
    }
}

static bool ppcertificate_gslt_v1_pattern_contains_formal(
    const PPCertificateGSLTPatternV1 *pattern,
    PPCertificateGSLTNameV1 name,
    uint32_t target_depth,
    uint32_t ambient_depth) {
    uint32_t index;
    uint32_t inner_depth;

    if (!pattern)
        return false;
    switch (pattern->kind) {
    case PPCERTIFICATE_GSLT_PATTERN_V1_FVAR:
        return ambient_depth == target_depth &&
               ppcertificate_gslt_article_v1_name_equal(pattern->as.fvar, name);
    case PPCERTIFICATE_GSLT_PATTERN_V1_APPLY:
        for (index = 0u; index < pattern->as.apply.argument_len; index++) {
            if (ppcertificate_gslt_v1_pattern_contains_formal(
                    &pattern->as.apply.arguments[index], name,
                    target_depth, ambient_depth))
                return true;
        }
        return false;
    case PPCERTIFICATE_GSLT_PATTERN_V1_LAMBDA:
        return ppcertificate_gslt_v1_add_depth(
                   ambient_depth, 1u, &inner_depth) &&
               ppcertificate_gslt_v1_pattern_contains_formal(
                   pattern->as.lambda.body, name, target_depth,
                   inner_depth);
    case PPCERTIFICATE_GSLT_PATTERN_V1_MULTI_LAMBDA:
        return ppcertificate_gslt_v1_add_depth(
                   ambient_depth, pattern->as.multi_lambda.arity,
                   &inner_depth) &&
               ppcertificate_gslt_v1_pattern_contains_formal(
                   pattern->as.multi_lambda.body, name, target_depth,
                   inner_depth);
    case PPCERTIFICATE_GSLT_PATTERN_V1_SUBST:
        return (ppcertificate_gslt_v1_add_depth(
                    ambient_depth, 1u, &inner_depth) &&
                ppcertificate_gslt_v1_pattern_contains_formal(
                    pattern->as.subst.body, name, target_depth,
                    inner_depth)) ||
               ppcertificate_gslt_v1_pattern_contains_formal(
                   pattern->as.subst.replacement, name, target_depth,
                   ambient_depth);
    case PPCERTIFICATE_GSLT_PATTERN_V1_COLLECTION:
        for (index = 0u; index < pattern->as.collection.element_len; index++) {
            if (ppcertificate_gslt_v1_pattern_contains_formal(
                    &pattern->as.collection.elements[index], name,
                    target_depth, ambient_depth))
                return true;
        }
        return false;
    case PPCERTIFICATE_GSLT_PATTERN_V1_BVAR:
    default:
        return false;
    }
}

static bool ppcertificate_gslt_v1_rule_contains_formal(
    const PPCertificateGSLTRuleSchemaV1 *rule,
    const PPCertificateGSLTFormalV1 *formal) {
    uint32_t index;

    for (index = 0u; index < rule->premise_len; index++) {
        if (ppcertificate_gslt_v1_pattern_contains_formal(
                &rule->premises[index], formal->name,
                formal->depth, 0u))
            return true;
    }
    return ppcertificate_gslt_v1_pattern_contains_formal(
        rule->conclusion, formal->name, formal->depth, 0u);
}

static bool ppcertificate_gslt_v1_names_unique_constructors(
    const PPCertificateGSLTPresentationV1 *presentation,
    char *error_buf, size_t error_buf_size) {
    uint32_t left;
    uint32_t right;

    for (left = 0u; left < presentation->constructor_len; left++) {
        if (!ppcertificate_gslt_v1_name_well_formed(
                presentation->constructors[left].name, false)) {
            ppcertificate_gslt_v1_set_error(
                error_buf, error_buf_size,
                "constructor declaration has an empty name");
            return false;
        }
        for (right = left + 1u;
             right < presentation->constructor_len; right++) {
            if (ppcertificate_gslt_article_v1_name_equal(
                    presentation->constructors[left].name,
                    presentation->constructors[right].name)) {
                ppcertificate_gslt_v1_set_error(
                    error_buf, error_buf_size,
                    "constructor declarations repeat a name");
                return false;
            }
        }
    }
    return true;
}

static bool ppcertificate_gslt_v1_names_unique_judgments(
    const PPCertificateGSLTPresentationV1 *presentation,
    char *error_buf, size_t error_buf_size) {
    uint32_t left;
    uint32_t right;

    for (left = 0u; left < presentation->judgment_len; left++) {
        if (!ppcertificate_gslt_v1_name_well_formed(
                presentation->judgments[left].head, false) ||
            ppcertificate_gslt_v1_find_constructor(
                presentation,
                presentation->judgments[left].head) != NULL) {
            ppcertificate_gslt_v1_set_error(
                error_buf, error_buf_size,
                "judgment declaration is empty or collides with a constructor");
            return false;
        }
        for (right = left + 1u;
             right < presentation->judgment_len; right++) {
            if (ppcertificate_gslt_article_v1_name_equal(
                    presentation->judgments[left].head,
                    presentation->judgments[right].head)) {
                ppcertificate_gslt_v1_set_error(
                    error_buf, error_buf_size,
                    "judgment declarations repeat a head");
                return false;
            }
        }
    }
    return true;
}

static PPCertificateGSLTArticleV1Result ppcertificate_gslt_v1_validate_rule(
    const PPCertificateGSLTPresentationV1 *presentation,
    const PPCertificateGSLTRuleSchemaV1 *rule,
    const PPCertificateGSLTArticleV1Limits *limits,
    uint32_t *remaining_io,
    uint32_t *capabilities_io,
    char *error_buf,
    size_t error_buf_size) {
    PPCertificateGSLTValidationV1 validation;
    uint32_t left;
    uint32_t right;

    if (!ppcertificate_gslt_v1_name_well_formed(rule->id, false) ||
        !ppcertificate_gslt_v1_array_well_formed(rule->formals,
                                           rule->formal_len) ||
        !ppcertificate_gslt_v1_array_well_formed(rule->premises,
                                           rule->premise_len) ||
        !rule->conclusion ||
        !ppcertificate_gslt_v1_array_well_formed(rule->side_conditions,
                                           rule->side_condition_len)) {
        ppcertificate_gslt_v1_set_error(
            error_buf, error_buf_size,
            "rule schema has an invalid outer shape");
        return PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID;
    }
    for (left = 0u; left < rule->formal_len; left++) {
        if (!ppcertificate_gslt_v1_name_well_formed(
                rule->formals[left].name, false)) {
            ppcertificate_gslt_v1_set_error(
                error_buf, error_buf_size,
                "rule schema has an empty formal name");
            return PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID;
        }
        for (right = left + 1u; right < rule->formal_len; right++) {
            if (ppcertificate_gslt_article_v1_name_equal(
                    rule->formals[left].name,
                    rule->formals[right].name)) {
                ppcertificate_gslt_v1_set_error(
                    error_buf, error_buf_size,
                    "rule schema repeats a formal name");
                return PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID;
            }
        }
    }

    validation = (PPCertificateGSLTValidationV1){
        .presentation = presentation,
        .rule = rule,
        .remaining = *remaining_io,
        .maximum_depth = limits->maximum_pattern_depth,
        .used_capabilities = *capabilities_io,
        .failure_result = PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID,
        .schema_mode = true,
        .error_buf = error_buf,
        .error_buf_size = error_buf_size,
    };
    for (left = 0u; left < rule->premise_len; left++) {
        if (!ppcertificate_gslt_v1_validate_pattern(
                &validation, &rule->premises[left], 0u, 0u, true)) {
            *remaining_io = validation.remaining;
            *capabilities_io = validation.used_capabilities;
            return validation.failure_result;
        }
    }
    if (!ppcertificate_gslt_v1_validate_pattern(
            &validation, rule->conclusion, 0u, 0u, true)) {
        *remaining_io = validation.remaining;
        *capabilities_io = validation.used_capabilities;
        return validation.failure_result;
    }
    *remaining_io = validation.remaining;
    *capabilities_io = validation.used_capabilities;

    for (left = 0u; left < rule->formal_len; left++) {
        if (!ppcertificate_gslt_v1_rule_contains_formal(
                rule, &rule->formals[left])) {
            ppcertificate_gslt_v1_set_error(
                error_buf, error_buf_size,
                "rule schema declares an unused formal");
            return PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID;
        }
    }

    for (left = 0u; left < rule->side_condition_len; left++) {
        const PPCertificateGSLTSideConditionV1 *condition =
            &rule->side_conditions[left];
        const PPCertificateGSLTFormalV1 *body;
        const PPCertificateGSLTFormalV1 *result;
        const PPCertificateGSLTFormalV1 *replacement = NULL;
        uint32_t body_depth;

        if (condition->body_argument >= rule->formal_len ||
            condition->result_argument >= rule->formal_len) {
            ppcertificate_gslt_v1_set_error(
                error_buf, error_buf_size,
                "side condition references an absent argument");
            return PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID;
        }
        body = &rule->formals[condition->body_argument];
        result = &rule->formals[condition->result_argument];
        if (!ppcertificate_gslt_v1_add_depth(
                condition->ambient_depth, 1u, &body_depth) ||
            body->depth != body_depth ||
            result->depth != condition->ambient_depth) {
            ppcertificate_gslt_v1_set_error(
                error_buf, error_buf_size,
                "side-condition depths disagree with their formals");
            return PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID;
        }
        switch (condition->kind) {
        case PPCERTIFICATE_GSLT_SIDE_CONDITION_V1_EXPLICIT_SUBSTITUTION:
            *capabilities_io |=
                PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_SUBSTITUTION_CONDITION;
            if (condition->replacement_argument >= rule->formal_len) {
                ppcertificate_gslt_v1_set_error(
                    error_buf, error_buf_size,
                    "substitution condition references an absent replacement");
                return PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID;
            }
            replacement = &rule->formals[condition->replacement_argument];
            if (replacement->depth != condition->ambient_depth) {
                ppcertificate_gslt_v1_set_error(
                    error_buf, error_buf_size,
                    "substitution replacement has the wrong depth");
                return PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID;
            }
            break;
        case PPCERTIFICATE_GSLT_SIDE_CONDITION_V1_UNUSED_BINDER_ELIMINATION:
            *capabilities_io |=
                PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_UNUSED_BINDER_CONDITION;
            break;
        default:
            ppcertificate_gslt_v1_set_error(
                error_buf, error_buf_size,
                "side-condition operation is unknown");
            return PPCERTIFICATE_GSLT_ARTICLE_V1_UNSUPPORTED;
        }
    }
    return PPCERTIFICATE_GSLT_ARTICLE_V1_OK;
}

PPCertificateGSLTArticleV1Result ppcertificate_gslt_article_v1_presentation_validate(
    const PPCertificateGSLTPresentationV1 *presentation,
    const PPCertificateGSLTArticleV1Limits *limits_argument,
    uint32_t *used_capabilities_out,
    char *error_buf,
    size_t error_buf_size) {
    PPCertificateGSLTArticleV1Limits default_limits;
    const PPCertificateGSLTArticleV1Limits *limits = limits_argument;
    uint32_t remaining;
    uint32_t used_capabilities = 0u;
    uint32_t left;
    uint32_t right;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!limits) {
        default_limits = ppcertificate_gslt_article_v1_default_limits();
        limits = &default_limits;
    }
    if (!presentation || limits->maximum_pattern_depth == 0u ||
        limits->maximum_presentation_pattern_nodes == 0u ||
        limits->maximum_materialized_pattern_nodes == 0u ||
        limits->maximum_article_nodes == 0u ||
        !ppcertificate_gslt_v1_array_well_formed(
            presentation ? presentation->constructors : NULL,
            presentation ? presentation->constructor_len : 0u) ||
        !ppcertificate_gslt_v1_array_well_formed(
            presentation ? presentation->judgments : NULL,
            presentation ? presentation->judgment_len : 0u) ||
        !ppcertificate_gslt_v1_array_well_formed(
            presentation ? presentation->rules : NULL,
            presentation ? presentation->rule_len : 0u)) {
        ppcertificate_gslt_v1_set_error(
            error_buf, error_buf_size,
            "presentation or resource limits are malformed");
        return PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID;
    }
    if ((presentation->required_capabilities &
         ~PPCERTIFICATE_GSLT_ARTICLE_V1_SUPPORTED_CAPABILITIES) != 0u) {
        ppcertificate_gslt_v1_set_error(
            error_buf, error_buf_size,
            "presentation requires an unsupported capability");
        return PPCERTIFICATE_GSLT_ARTICLE_V1_UNSUPPORTED;
    }
    if (!ppcertificate_gslt_v1_names_unique_constructors(
            presentation, error_buf, error_buf_size) ||
        !ppcertificate_gslt_v1_names_unique_judgments(
            presentation, error_buf, error_buf_size))
        return PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID;

    remaining = limits->maximum_presentation_pattern_nodes;
    for (left = 0u; left < presentation->rule_len; left++) {
        PPCertificateGSLTArticleV1Result result;

        for (right = left + 1u; right < presentation->rule_len; right++) {
            if (ppcertificate_gslt_article_v1_name_equal(
                    presentation->rules[left].id,
                    presentation->rules[right].id)) {
                ppcertificate_gslt_v1_set_error(
                    error_buf, error_buf_size,
                    "presentation repeats a rule identifier");
                return PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID;
            }
        }
        result = ppcertificate_gslt_v1_validate_rule(
            presentation, &presentation->rules[left], limits,
            &remaining, &used_capabilities,
            error_buf, error_buf_size);
        if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            return result;
    }

    if (presentation->conversion.present) {
        const PPCertificateGSLTJudgmentV1 *judgment;

        used_capabilities |=
            PPCERTIFICATE_GSLT_ARTICLE_V1_CAP_CONVERSION_DECLARATION;
        judgment = ppcertificate_gslt_v1_find_judgment(
            presentation, presentation->conversion.judgment_head);
        if (!judgment || judgment->arity != 2u ||
            !ppcertificate_gslt_v1_name_well_formed(
                presentation->conversion.version, false)) {
            ppcertificate_gslt_v1_set_error(
                error_buf, error_buf_size,
                "conversion declaration is not a versioned binary judgment");
            return PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID;
        }
    }
    if ((used_capabilities & ~presentation->required_capabilities) != 0u) {
        ppcertificate_gslt_v1_set_error(
            error_buf, error_buf_size,
            "presentation uses a capability it did not declare");
        return PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID;
    }
    if (used_capabilities_out)
        *used_capabilities_out = used_capabilities;
    return PPCERTIFICATE_GSLT_ARTICLE_V1_OK;
}

static void ppcertificate_gslt_v1_owned_pattern_clear(
    PPCertificateGSLTPatternV1 *pattern) {
    uint32_t index;

    if (!pattern)
        return;
    switch (pattern->kind) {
    case PPCERTIFICATE_GSLT_PATTERN_V1_APPLY:
        if (!pattern->owns_children)
            break;
        for (index = 0u; index < pattern->as.apply.argument_len; index++)
            ppcertificate_gslt_v1_owned_pattern_clear(
                (PPCertificateGSLTPatternV1 *)&pattern->as.apply.arguments[index]);
        free((void *)pattern->as.apply.arguments);
        break;
    case PPCERTIFICATE_GSLT_PATTERN_V1_LAMBDA:
        if (!pattern->owns_children)
            break;
        ppcertificate_gslt_v1_owned_pattern_clear(
            (PPCertificateGSLTPatternV1 *)pattern->as.lambda.body);
        free((void *)pattern->as.lambda.body);
        break;
    case PPCERTIFICATE_GSLT_PATTERN_V1_MULTI_LAMBDA:
        if (!pattern->owns_children)
            break;
        ppcertificate_gslt_v1_owned_pattern_clear(
            (PPCertificateGSLTPatternV1 *)pattern->as.multi_lambda.body);
        free((void *)pattern->as.multi_lambda.body);
        break;
    case PPCERTIFICATE_GSLT_PATTERN_V1_SUBST:
        if (!pattern->owns_children)
            break;
        ppcertificate_gslt_v1_owned_pattern_clear(
            (PPCertificateGSLTPatternV1 *)pattern->as.subst.body);
        ppcertificate_gslt_v1_owned_pattern_clear(
            (PPCertificateGSLTPatternV1 *)pattern->as.subst.replacement);
        free((void *)pattern->as.subst.body);
        free((void *)pattern->as.subst.replacement);
        break;
    case PPCERTIFICATE_GSLT_PATTERN_V1_COLLECTION:
        if (!pattern->owns_children)
            break;
        for (index = 0u; index < pattern->as.collection.element_len; index++)
            ppcertificate_gslt_v1_owned_pattern_clear(
                (PPCertificateGSLTPatternV1 *)&pattern->as.collection.elements[index]);
        free((void *)pattern->as.collection.elements);
        break;
    case PPCERTIFICATE_GSLT_PATTERN_V1_BVAR:
    case PPCERTIFICATE_GSLT_PATTERN_V1_FVAR:
    default:
        break;
    }
    memset(pattern, 0, sizeof(*pattern));
}

static PPCertificateGSLTArticleV1Result ppcertificate_gslt_v1_allocate_patterns(
    PPCertificateGSLTMaterialBudgetV1 *budget,
    uint32_t count,
    PPCertificateGSLTPatternV1 **out) {
    PPCertificateGSLTPatternV1 *items;

    *out = NULL;
    if (count == 0u)
        return PPCERTIFICATE_GSLT_ARTICLE_V1_OK;
    if (count > budget->remaining ||
        !ppcertificate_gslt_v1_allocation_fits(count, sizeof(*items)))
        return PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
    items = calloc(count, sizeof(*items));
    if (!items)
        return PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
    budget->remaining -= count;
    budget->used += count;
    *out = items;
    return PPCERTIFICATE_GSLT_ARTICLE_V1_OK;
}

static PPCertificateGSLTArticleV1Result ppcertificate_gslt_v1_clone_pattern(
    const PPCertificateGSLTPatternV1 *source,
    PPCertificateGSLTPatternV1 *target,
    PPCertificateGSLTMaterialBudgetV1 *budget,
    uint32_t traversal_depth) {
    if (!source || !target || !budget ||
        traversal_depth > budget->maximum_depth)
        return PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
    if (source->kind < PPCERTIFICATE_GSLT_PATTERN_V1_BVAR ||
        source->kind > PPCERTIFICATE_GSLT_PATTERN_V1_COLLECTION)
        return PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID;
    *target = *source;
    target->owns_children = false;
    return PPCERTIFICATE_GSLT_ARTICLE_V1_OK;
}

static bool ppcertificate_gslt_v1_pattern_equal_at(
    const PPCertificateGSLTPatternV1 *left,
    const PPCertificateGSLTPatternV1 *right,
    uint32_t traversal_depth,
    uint32_t maximum_depth) {
    uint32_t index;

    if (!left || !right)
        return false;
    if (left == right)
        return true;
    if (traversal_depth > maximum_depth || left->kind != right->kind)
        return false;
    switch (left->kind) {
    case PPCERTIFICATE_GSLT_PATTERN_V1_BVAR:
        return left->as.bvar == right->as.bvar;
    case PPCERTIFICATE_GSLT_PATTERN_V1_FVAR:
        return ppcertificate_gslt_article_v1_name_equal(
            left->as.fvar, right->as.fvar);
    case PPCERTIFICATE_GSLT_PATTERN_V1_APPLY:
        if (!ppcertificate_gslt_article_v1_name_equal(
                left->as.apply.constructor,
                right->as.apply.constructor) ||
            left->as.apply.argument_len != right->as.apply.argument_len)
            return false;
        for (index = 0u; index < left->as.apply.argument_len; index++) {
            if (!ppcertificate_gslt_v1_pattern_equal_at(
                    &left->as.apply.arguments[index],
                    &right->as.apply.arguments[index],
                    traversal_depth + 1u, maximum_depth))
                return false;
        }
        return true;
    case PPCERTIFICATE_GSLT_PATTERN_V1_LAMBDA:
        return left->as.lambda.binder_name_present ==
                   right->as.lambda.binder_name_present &&
               (!left->as.lambda.binder_name_present ||
                ppcertificate_gslt_article_v1_name_equal(
                    left->as.lambda.binder_name,
                    right->as.lambda.binder_name)) &&
               ppcertificate_gslt_v1_pattern_equal_at(
                   left->as.lambda.body, right->as.lambda.body,
                   traversal_depth + 1u, maximum_depth);
    case PPCERTIFICATE_GSLT_PATTERN_V1_MULTI_LAMBDA:
        if (left->as.multi_lambda.arity !=
                right->as.multi_lambda.arity ||
            left->as.multi_lambda.binder_name_len !=
                right->as.multi_lambda.binder_name_len)
            return false;
        for (index = 0u;
             index < left->as.multi_lambda.binder_name_len; index++) {
            if (!ppcertificate_gslt_article_v1_name_equal(
                    left->as.multi_lambda.binder_names[index],
                    right->as.multi_lambda.binder_names[index]))
                return false;
        }
        return ppcertificate_gslt_v1_pattern_equal_at(
            left->as.multi_lambda.body,
            right->as.multi_lambda.body,
            traversal_depth + 1u, maximum_depth);
    case PPCERTIFICATE_GSLT_PATTERN_V1_SUBST:
        return ppcertificate_gslt_v1_pattern_equal_at(
                   left->as.subst.body, right->as.subst.body,
                   traversal_depth + 1u, maximum_depth) &&
               ppcertificate_gslt_v1_pattern_equal_at(
                   left->as.subst.replacement,
                   right->as.subst.replacement,
                   traversal_depth + 1u, maximum_depth);
    case PPCERTIFICATE_GSLT_PATTERN_V1_COLLECTION:
        if (left->as.collection.collection_kind !=
                right->as.collection.collection_kind ||
            left->as.collection.element_len !=
                right->as.collection.element_len ||
            left->as.collection.rest_present !=
                right->as.collection.rest_present ||
            (left->as.collection.rest_present &&
             !ppcertificate_gslt_article_v1_name_equal(
                 left->as.collection.rest,
                 right->as.collection.rest)))
            return false;
        for (index = 0u; index < left->as.collection.element_len; index++) {
            if (!ppcertificate_gslt_v1_pattern_equal_at(
                    &left->as.collection.elements[index],
                    &right->as.collection.elements[index],
                    traversal_depth + 1u, maximum_depth))
                return false;
        }
        return true;
    default:
        return false;
    }
}

static PPCertificateGSLTArticleV1Result ppcertificate_gslt_v1_instantiate_pattern(
    const PPCertificateGSLTRuleSchemaV1 *rule,
    const PPCertificateGSLTPatternV1 *arguments,
    const PPCertificateGSLTPatternV1 *schema,
    uint32_t ambient_depth,
    PPCertificateGSLTPatternV1 *target,
    PPCertificateGSLTMaterialBudgetV1 *budget,
    uint32_t traversal_depth) {
    PPCertificateGSLTArticleV1Result result;
    PPCertificateGSLTPatternV1 *children = NULL;
    PPCertificateGSLTPatternV1 *body = NULL;
    PPCertificateGSLTPatternV1 *replacement = NULL;
    uint32_t index;
    uint32_t inner_depth;

    if (traversal_depth > budget->maximum_depth)
        return PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
    if (schema->kind == PPCERTIFICATE_GSLT_PATTERN_V1_FVAR) {
        int32_t formal = ppcertificate_gslt_v1_find_formal(
            rule, schema->as.fvar, ambient_depth);
        if (formal < 0)
            return PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID;
        return ppcertificate_gslt_v1_clone_pattern(
            &arguments[(uint32_t)formal], target, budget,
            traversal_depth);
    }

    target->kind = schema->kind;
    switch (schema->kind) {
    case PPCERTIFICATE_GSLT_PATTERN_V1_BVAR:
        target->as.bvar = schema->as.bvar;
        return PPCERTIFICATE_GSLT_ARTICLE_V1_OK;
    case PPCERTIFICATE_GSLT_PATTERN_V1_APPLY:
        target->as.apply.constructor = schema->as.apply.constructor;
        target->as.apply.arguments = NULL;
        target->as.apply.argument_len = 0u;
        result = ppcertificate_gslt_v1_allocate_patterns(
            budget, schema->as.apply.argument_len, &children);
        if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            return result;
        target->as.apply.arguments = children;
        target->as.apply.argument_len = schema->as.apply.argument_len;
        target->owns_children = true;
        for (index = 0u; index < schema->as.apply.argument_len; index++) {
            result = ppcertificate_gslt_v1_instantiate_pattern(
                rule, arguments, &schema->as.apply.arguments[index],
                ambient_depth, &children[index], budget,
                traversal_depth + 1u);
            if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK) {
                ppcertificate_gslt_v1_owned_pattern_clear(target);
                return result;
            }
        }
        return PPCERTIFICATE_GSLT_ARTICLE_V1_OK;
    case PPCERTIFICATE_GSLT_PATTERN_V1_LAMBDA:
        if (!ppcertificate_gslt_v1_add_depth(
                ambient_depth, 1u, &inner_depth))
            return PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID;
        result = ppcertificate_gslt_v1_allocate_patterns(budget, 1u, &body);
        if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            return result;
        target->as.lambda.binder_name_present =
            schema->as.lambda.binder_name_present;
        target->as.lambda.binder_name = schema->as.lambda.binder_name;
        target->as.lambda.body = body;
        target->owns_children = true;
        result = ppcertificate_gslt_v1_instantiate_pattern(
            rule, arguments, schema->as.lambda.body, inner_depth,
            body, budget, traversal_depth + 1u);
        if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            ppcertificate_gslt_v1_owned_pattern_clear(target);
        return result;
    case PPCERTIFICATE_GSLT_PATTERN_V1_MULTI_LAMBDA:
        if (!ppcertificate_gslt_v1_add_depth(
                ambient_depth, schema->as.multi_lambda.arity,
                &inner_depth))
            return PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID;
        result = ppcertificate_gslt_v1_allocate_patterns(budget, 1u, &body);
        if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            return result;
        target->as.multi_lambda.arity = schema->as.multi_lambda.arity;
        target->as.multi_lambda.binder_names =
            schema->as.multi_lambda.binder_names;
        target->as.multi_lambda.binder_name_len =
            schema->as.multi_lambda.binder_name_len;
        target->as.multi_lambda.body = body;
        target->owns_children = true;
        result = ppcertificate_gslt_v1_instantiate_pattern(
            rule, arguments, schema->as.multi_lambda.body,
            inner_depth, body, budget, traversal_depth + 1u);
        if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            ppcertificate_gslt_v1_owned_pattern_clear(target);
        return result;
    case PPCERTIFICATE_GSLT_PATTERN_V1_SUBST:
        if (!ppcertificate_gslt_v1_add_depth(
                ambient_depth, 1u, &inner_depth))
            return PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID;
        result = ppcertificate_gslt_v1_allocate_patterns(budget, 1u, &body);
        if (result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            result = ppcertificate_gslt_v1_allocate_patterns(
                budget, 1u, &replacement);
        if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK) {
            free(body);
            return result;
        }
        target->as.subst.body = body;
        target->as.subst.replacement = replacement;
        target->owns_children = true;
        result = ppcertificate_gslt_v1_instantiate_pattern(
            rule, arguments, schema->as.subst.body, inner_depth,
            body, budget, traversal_depth + 1u);
        if (result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            result = ppcertificate_gslt_v1_instantiate_pattern(
                rule, arguments, schema->as.subst.replacement,
                ambient_depth, replacement, budget,
                traversal_depth + 1u);
        if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            ppcertificate_gslt_v1_owned_pattern_clear(target);
        return result;
    case PPCERTIFICATE_GSLT_PATTERN_V1_COLLECTION:
        target->as.collection.collection_kind =
            schema->as.collection.collection_kind;
        target->as.collection.elements = NULL;
        target->as.collection.element_len = 0u;
        target->as.collection.rest_present = false;
        target->as.collection.rest = schema->as.collection.rest;
        result = ppcertificate_gslt_v1_allocate_patterns(
            budget, schema->as.collection.element_len, &children);
        if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            return result;
        target->as.collection.elements = children;
        target->as.collection.element_len =
            schema->as.collection.element_len;
        target->owns_children = true;
        for (index = 0u;
             index < schema->as.collection.element_len; index++) {
            result = ppcertificate_gslt_v1_instantiate_pattern(
                rule, arguments,
                &schema->as.collection.elements[index], ambient_depth,
                &children[index], budget, traversal_depth + 1u);
            if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK) {
                ppcertificate_gslt_v1_owned_pattern_clear(target);
                return result;
            }
        }
        return PPCERTIFICATE_GSLT_ARTICLE_V1_OK;
    case PPCERTIFICATE_GSLT_PATTERN_V1_FVAR:
    default:
        return PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID;
    }
}

static PPCertificateGSLTArticleV1Result ppcertificate_gslt_v1_lift_pattern(
    const PPCertificateGSLTPatternV1 *source,
    uint32_t cutoff,
    uint32_t shift,
    PPCertificateGSLTPatternV1 *target,
    PPCertificateGSLTMaterialBudgetV1 *budget,
    uint32_t traversal_depth);

static PPCertificateGSLTArticleV1Result ppcertificate_gslt_v1_transform_children_lift(
    const PPCertificateGSLTPatternV1 *sources,
    uint32_t source_len,
    uint32_t cutoff,
    uint32_t shift,
    PPCertificateGSLTPatternV1 **targets_out,
    PPCertificateGSLTMaterialBudgetV1 *budget,
    uint32_t traversal_depth) {
    PPCertificateGSLTPatternV1 *targets = NULL;
    PPCertificateGSLTArticleV1Result result;
    uint32_t index;

    result = ppcertificate_gslt_v1_allocate_patterns(
        budget, source_len, &targets);
    if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
        return result;
    for (index = 0u; index < source_len; index++) {
        result = ppcertificate_gslt_v1_lift_pattern(
            &sources[index], cutoff, shift, &targets[index], budget,
            traversal_depth);
        if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK) {
            uint32_t clear_index;
            for (clear_index = 0u; clear_index <= index; clear_index++)
                ppcertificate_gslt_v1_owned_pattern_clear(&targets[clear_index]);
            free(targets);
            return result;
        }
    }
    *targets_out = targets;
    return PPCERTIFICATE_GSLT_ARTICLE_V1_OK;
}

static PPCertificateGSLTArticleV1Result ppcertificate_gslt_v1_lift_pattern(
    const PPCertificateGSLTPatternV1 *source,
    uint32_t cutoff,
    uint32_t shift,
    PPCertificateGSLTPatternV1 *target,
    PPCertificateGSLTMaterialBudgetV1 *budget,
    uint32_t traversal_depth) {
    PPCertificateGSLTArticleV1Result result;
    PPCertificateGSLTPatternV1 *children = NULL;
    PPCertificateGSLTPatternV1 *body = NULL;
    PPCertificateGSLTPatternV1 *replacement = NULL;
    uint32_t inner_cutoff;

    if (traversal_depth > budget->maximum_depth)
        return PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
    target->kind = source->kind;
    switch (source->kind) {
    case PPCERTIFICATE_GSLT_PATTERN_V1_BVAR:
        if (source->as.bvar >= cutoff &&
            UINT32_MAX - source->as.bvar < shift)
            return PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
        target->as.bvar = source->as.bvar >= cutoff
                              ? source->as.bvar + shift
                              : source->as.bvar;
        return PPCERTIFICATE_GSLT_ARTICLE_V1_OK;
    case PPCERTIFICATE_GSLT_PATTERN_V1_FVAR:
        target->as.fvar = source->as.fvar;
        return PPCERTIFICATE_GSLT_ARTICLE_V1_OK;
    case PPCERTIFICATE_GSLT_PATTERN_V1_APPLY:
        target->as.apply.constructor = source->as.apply.constructor;
        target->as.apply.arguments = NULL;
        target->as.apply.argument_len = 0u;
        result = ppcertificate_gslt_v1_transform_children_lift(
            source->as.apply.arguments, source->as.apply.argument_len,
            cutoff, shift, &children,
            budget, traversal_depth + 1u);
        if (result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK) {
            target->as.apply.arguments = children;
            target->as.apply.argument_len = source->as.apply.argument_len;
            target->owns_children = true;
        }
        return result;
    case PPCERTIFICATE_GSLT_PATTERN_V1_LAMBDA:
        if (!ppcertificate_gslt_v1_add_depth(cutoff, 1u, &inner_cutoff))
            return PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
        result = ppcertificate_gslt_v1_allocate_patterns(budget, 1u, &body);
        if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            return result;
        target->as.lambda = source->as.lambda;
        target->as.lambda.body = body;
        target->owns_children = true;
        result = ppcertificate_gslt_v1_lift_pattern(
            source->as.lambda.body, inner_cutoff, shift, body, budget,
            traversal_depth + 1u);
        if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            ppcertificate_gslt_v1_owned_pattern_clear(target);
        return result;
    case PPCERTIFICATE_GSLT_PATTERN_V1_MULTI_LAMBDA:
        if (!ppcertificate_gslt_v1_add_depth(
                cutoff, source->as.multi_lambda.arity, &inner_cutoff))
            return PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
        result = ppcertificate_gslt_v1_allocate_patterns(budget, 1u, &body);
        if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            return result;
        target->as.multi_lambda = source->as.multi_lambda;
        target->as.multi_lambda.body = body;
        target->owns_children = true;
        result = ppcertificate_gslt_v1_lift_pattern(
            source->as.multi_lambda.body, inner_cutoff, shift,
            body, budget, traversal_depth + 1u);
        if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            ppcertificate_gslt_v1_owned_pattern_clear(target);
        return result;
    case PPCERTIFICATE_GSLT_PATTERN_V1_SUBST:
        if (!ppcertificate_gslt_v1_add_depth(cutoff, 1u, &inner_cutoff))
            return PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
        result = ppcertificate_gslt_v1_allocate_patterns(budget, 1u, &body);
        if (result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            result = ppcertificate_gslt_v1_allocate_patterns(
                budget, 1u, &replacement);
        if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK) {
            free(body);
            return result;
        }
        target->as.subst.body = body;
        target->as.subst.replacement = replacement;
        target->owns_children = true;
        result = ppcertificate_gslt_v1_lift_pattern(
            source->as.subst.body, inner_cutoff, shift, body, budget,
            traversal_depth + 1u);
        if (result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            result = ppcertificate_gslt_v1_lift_pattern(
                source->as.subst.replacement, cutoff, shift,
                replacement, budget, traversal_depth + 1u);
        if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            ppcertificate_gslt_v1_owned_pattern_clear(target);
        return result;
    case PPCERTIFICATE_GSLT_PATTERN_V1_COLLECTION:
        target->as.collection.collection_kind =
            source->as.collection.collection_kind;
        target->as.collection.elements = NULL;
        target->as.collection.element_len = 0u;
        target->as.collection.rest_present =
            source->as.collection.rest_present;
        target->as.collection.rest = source->as.collection.rest;
        result = ppcertificate_gslt_v1_transform_children_lift(
            source->as.collection.elements,
            source->as.collection.element_len, cutoff, shift,
            &children,
            budget, traversal_depth + 1u);
        if (result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK) {
            target->as.collection.elements = children;
            target->as.collection.element_len =
                source->as.collection.element_len;
            target->owns_children = true;
        }
        return result;
    default:
        return PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID;
    }
}

static PPCertificateGSLTArticleV1Result ppcertificate_gslt_v1_instantiate_bvar_at(
    const PPCertificateGSLTPatternV1 *source,
    uint32_t depth,
    const PPCertificateGSLTPatternV1 *replacement_source,
    PPCertificateGSLTPatternV1 *target,
    PPCertificateGSLTMaterialBudgetV1 *budget,
    uint32_t traversal_depth) {
    PPCertificateGSLTArticleV1Result result;
    PPCertificateGSLTPatternV1 *children = NULL;
    PPCertificateGSLTPatternV1 *body = NULL;
    PPCertificateGSLTPatternV1 *replacement = NULL;
    uint32_t index;
    uint32_t inner_depth;

    if (traversal_depth > budget->maximum_depth)
        return PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
    target->kind = source->kind;
    switch (source->kind) {
    case PPCERTIFICATE_GSLT_PATTERN_V1_BVAR:
        if (source->as.bvar < depth) {
            target->as.bvar = source->as.bvar;
            return PPCERTIFICATE_GSLT_ARTICLE_V1_OK;
        }
        if (source->as.bvar == depth)
            return ppcertificate_gslt_v1_lift_pattern(
                replacement_source, 0u, depth, target, budget,
                traversal_depth);
        target->as.bvar = source->as.bvar - 1u;
        return PPCERTIFICATE_GSLT_ARTICLE_V1_OK;
    case PPCERTIFICATE_GSLT_PATTERN_V1_FVAR:
        target->as.fvar = source->as.fvar;
        return PPCERTIFICATE_GSLT_ARTICLE_V1_OK;
    case PPCERTIFICATE_GSLT_PATTERN_V1_APPLY:
        target->as.apply.constructor = source->as.apply.constructor;
        target->as.apply.arguments = NULL;
        target->as.apply.argument_len = 0u;
        result = ppcertificate_gslt_v1_allocate_patterns(
            budget, source->as.apply.argument_len, &children);
        if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            return result;
        target->as.apply.arguments = children;
        target->as.apply.argument_len = source->as.apply.argument_len;
        target->owns_children = true;
        for (index = 0u; index < source->as.apply.argument_len; index++) {
            result = ppcertificate_gslt_v1_instantiate_bvar_at(
                &source->as.apply.arguments[index], depth,
                replacement_source, &children[index], budget,
                traversal_depth + 1u);
            if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK) {
                ppcertificate_gslt_v1_owned_pattern_clear(target);
                return result;
            }
        }
        return PPCERTIFICATE_GSLT_ARTICLE_V1_OK;
    case PPCERTIFICATE_GSLT_PATTERN_V1_LAMBDA:
        if (!ppcertificate_gslt_v1_add_depth(depth, 1u, &inner_depth))
            return PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
        result = ppcertificate_gslt_v1_allocate_patterns(budget, 1u, &body);
        if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            return result;
        target->as.lambda = source->as.lambda;
        target->as.lambda.body = body;
        target->owns_children = true;
        result = ppcertificate_gslt_v1_instantiate_bvar_at(
            source->as.lambda.body, inner_depth, replacement_source,
            body, budget, traversal_depth + 1u);
        if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            ppcertificate_gslt_v1_owned_pattern_clear(target);
        return result;
    case PPCERTIFICATE_GSLT_PATTERN_V1_MULTI_LAMBDA:
        if (!ppcertificate_gslt_v1_add_depth(
                depth, source->as.multi_lambda.arity, &inner_depth))
            return PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
        result = ppcertificate_gslt_v1_allocate_patterns(budget, 1u, &body);
        if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            return result;
        target->as.multi_lambda = source->as.multi_lambda;
        target->as.multi_lambda.body = body;
        target->owns_children = true;
        result = ppcertificate_gslt_v1_instantiate_bvar_at(
            source->as.multi_lambda.body, inner_depth,
            replacement_source, body, budget, traversal_depth + 1u);
        if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            ppcertificate_gslt_v1_owned_pattern_clear(target);
        return result;
    case PPCERTIFICATE_GSLT_PATTERN_V1_SUBST:
        if (!ppcertificate_gslt_v1_add_depth(depth, 1u, &inner_depth))
            return PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
        result = ppcertificate_gslt_v1_allocate_patterns(budget, 1u, &body);
        if (result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            result = ppcertificate_gslt_v1_allocate_patterns(
                budget, 1u, &replacement);
        if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK) {
            free(body);
            return result;
        }
        target->as.subst.body = body;
        target->as.subst.replacement = replacement;
        target->owns_children = true;
        result = ppcertificate_gslt_v1_instantiate_bvar_at(
            source->as.subst.body, inner_depth, replacement_source,
            body, budget, traversal_depth + 1u);
        if (result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            result = ppcertificate_gslt_v1_instantiate_bvar_at(
                source->as.subst.replacement, depth,
                replacement_source, replacement, budget,
                traversal_depth + 1u);
        if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            ppcertificate_gslt_v1_owned_pattern_clear(target);
        return result;
    case PPCERTIFICATE_GSLT_PATTERN_V1_COLLECTION:
        target->as.collection.collection_kind =
            source->as.collection.collection_kind;
        target->as.collection.elements = NULL;
        target->as.collection.element_len = 0u;
        target->as.collection.rest_present =
            source->as.collection.rest_present;
        target->as.collection.rest = source->as.collection.rest;
        result = ppcertificate_gslt_v1_allocate_patterns(
            budget, source->as.collection.element_len, &children);
        if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            return result;
        target->as.collection.elements = children;
        target->as.collection.element_len =
            source->as.collection.element_len;
        target->owns_children = true;
        for (index = 0u;
             index < source->as.collection.element_len; index++) {
            result = ppcertificate_gslt_v1_instantiate_bvar_at(
                &source->as.collection.elements[index], depth,
                replacement_source, &children[index], budget,
                traversal_depth + 1u);
            if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK) {
                ppcertificate_gslt_v1_owned_pattern_clear(target);
                return result;
            }
        }
        return PPCERTIFICATE_GSLT_ARTICLE_V1_OK;
    default:
        return PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID;
    }
}

static PPCertificateGSLTArticleV1Result ppcertificate_gslt_v1_drop_bvar_at(
    const PPCertificateGSLTPatternV1 *source,
    uint32_t cutoff,
    PPCertificateGSLTPatternV1 *target,
    PPCertificateGSLTMaterialBudgetV1 *budget,
    uint32_t traversal_depth,
    bool *dropped_out) {
    PPCertificateGSLTArticleV1Result result;
    PPCertificateGSLTPatternV1 *children = NULL;
    PPCertificateGSLTPatternV1 *body = NULL;
    PPCertificateGSLTPatternV1 *replacement = NULL;
    uint32_t index;
    uint32_t inner_cutoff;

    if (traversal_depth > budget->maximum_depth)
        return PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
    target->kind = source->kind;
    switch (source->kind) {
    case PPCERTIFICATE_GSLT_PATTERN_V1_BVAR:
        if (source->as.bvar == cutoff) {
            *dropped_out = false;
            return PPCERTIFICATE_GSLT_ARTICLE_V1_OK;
        }
        target->as.bvar = source->as.bvar > cutoff
                              ? source->as.bvar - 1u
                              : source->as.bvar;
        return PPCERTIFICATE_GSLT_ARTICLE_V1_OK;
    case PPCERTIFICATE_GSLT_PATTERN_V1_FVAR:
        target->as.fvar = source->as.fvar;
        return PPCERTIFICATE_GSLT_ARTICLE_V1_OK;
    case PPCERTIFICATE_GSLT_PATTERN_V1_APPLY:
        target->as.apply.constructor = source->as.apply.constructor;
        target->as.apply.arguments = NULL;
        target->as.apply.argument_len = 0u;
        result = ppcertificate_gslt_v1_allocate_patterns(
            budget, source->as.apply.argument_len, &children);
        if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            return result;
        target->as.apply.arguments = children;
        target->as.apply.argument_len = source->as.apply.argument_len;
        target->owns_children = true;
        for (index = 0u; index < source->as.apply.argument_len; index++) {
            result = ppcertificate_gslt_v1_drop_bvar_at(
                &source->as.apply.arguments[index], cutoff,
                &children[index], budget, traversal_depth + 1u,
                dropped_out);
            if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK || !*dropped_out) {
                ppcertificate_gslt_v1_owned_pattern_clear(target);
                return result;
            }
        }
        return PPCERTIFICATE_GSLT_ARTICLE_V1_OK;
    case PPCERTIFICATE_GSLT_PATTERN_V1_LAMBDA:
        if (!ppcertificate_gslt_v1_add_depth(cutoff, 1u, &inner_cutoff))
            return PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
        result = ppcertificate_gslt_v1_allocate_patterns(budget, 1u, &body);
        if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            return result;
        target->as.lambda = source->as.lambda;
        target->as.lambda.body = body;
        target->owns_children = true;
        result = ppcertificate_gslt_v1_drop_bvar_at(
            source->as.lambda.body, inner_cutoff, body, budget,
            traversal_depth + 1u, dropped_out);
        if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK || !*dropped_out)
            ppcertificate_gslt_v1_owned_pattern_clear(target);
        return result;
    case PPCERTIFICATE_GSLT_PATTERN_V1_MULTI_LAMBDA:
        if (!ppcertificate_gslt_v1_add_depth(
                cutoff, source->as.multi_lambda.arity,
                &inner_cutoff))
            return PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
        result = ppcertificate_gslt_v1_allocate_patterns(budget, 1u, &body);
        if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            return result;
        target->as.multi_lambda = source->as.multi_lambda;
        target->as.multi_lambda.body = body;
        target->owns_children = true;
        result = ppcertificate_gslt_v1_drop_bvar_at(
            source->as.multi_lambda.body, inner_cutoff, body, budget,
            traversal_depth + 1u, dropped_out);
        if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK || !*dropped_out)
            ppcertificate_gslt_v1_owned_pattern_clear(target);
        return result;
    case PPCERTIFICATE_GSLT_PATTERN_V1_SUBST:
        if (!ppcertificate_gslt_v1_add_depth(cutoff, 1u, &inner_cutoff))
            return PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
        result = ppcertificate_gslt_v1_allocate_patterns(budget, 1u, &body);
        if (result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            result = ppcertificate_gslt_v1_allocate_patterns(
                budget, 1u, &replacement);
        if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK) {
            free(body);
            return result;
        }
        target->as.subst.body = body;
        target->as.subst.replacement = replacement;
        target->owns_children = true;
        result = ppcertificate_gslt_v1_drop_bvar_at(
            source->as.subst.body, inner_cutoff, body, budget,
            traversal_depth + 1u, dropped_out);
        if (result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK && *dropped_out)
            result = ppcertificate_gslt_v1_drop_bvar_at(
                source->as.subst.replacement, cutoff, replacement,
                budget, traversal_depth + 1u, dropped_out);
        if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK || !*dropped_out)
            ppcertificate_gslt_v1_owned_pattern_clear(target);
        return result;
    case PPCERTIFICATE_GSLT_PATTERN_V1_COLLECTION:
        target->as.collection.collection_kind =
            source->as.collection.collection_kind;
        target->as.collection.elements = NULL;
        target->as.collection.element_len = 0u;
        target->as.collection.rest_present =
            source->as.collection.rest_present;
        target->as.collection.rest = source->as.collection.rest;
        result = ppcertificate_gslt_v1_allocate_patterns(
            budget, source->as.collection.element_len, &children);
        if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            return result;
        target->as.collection.elements = children;
        target->as.collection.element_len =
            source->as.collection.element_len;
        target->owns_children = true;
        for (index = 0u;
             index < source->as.collection.element_len; index++) {
            result = ppcertificate_gslt_v1_drop_bvar_at(
                &source->as.collection.elements[index], cutoff,
                &children[index], budget, traversal_depth + 1u,
                dropped_out);
            if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK || !*dropped_out) {
                ppcertificate_gslt_v1_owned_pattern_clear(target);
                return result;
            }
        }
        return PPCERTIFICATE_GSLT_ARTICLE_V1_OK;
    default:
        return PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID;
    }
}

static PPCertificateGSLTArticleV1Result ppcertificate_gslt_v1_side_conditions_hold(
    const PPCertificateGSLTRuleSchemaV1 *rule,
    const PPCertificateGSLTPatternV1 *arguments,
    PPCertificateGSLTMaterialBudgetV1 *budget,
    bool *holds_out) {
    uint32_t index;

    *holds_out = false;
    for (index = 0u; index < rule->side_condition_len; index++) {
        const PPCertificateGSLTSideConditionV1 *condition =
            &rule->side_conditions[index];
        const PPCertificateGSLTPatternV1 *body =
            &arguments[condition->body_argument];
        const PPCertificateGSLTPatternV1 *result_argument =
            &arguments[condition->result_argument];
        PPCertificateGSLTPatternV1 transformed = {0};
        PPCertificateGSLTArticleV1Result result;
        bool dropped = true;
        bool equal;

        if (budget->remaining == 0u)
            return PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
        budget->remaining--;
        budget->used++;

        if (condition->kind ==
            PPCERTIFICATE_GSLT_SIDE_CONDITION_V1_EXPLICIT_SUBSTITUTION) {
            result = ppcertificate_gslt_v1_instantiate_bvar_at(
                body, 0u,
                &arguments[condition->replacement_argument],
                &transformed, budget, 0u);
        } else {
            result = ppcertificate_gslt_v1_drop_bvar_at(
                body, 0u, &transformed, budget, 0u, &dropped);
        }
        if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            return result;
        if (!dropped) {
            ppcertificate_gslt_v1_owned_pattern_clear(&transformed);
            *holds_out = false;
            return PPCERTIFICATE_GSLT_ARTICLE_V1_OK;
        }
        equal = ppcertificate_gslt_v1_pattern_equal_at(
            &transformed, result_argument, 0u, budget->maximum_depth);
        ppcertificate_gslt_v1_owned_pattern_clear(&transformed);
        if (!equal) {
            *holds_out = false;
            return PPCERTIFICATE_GSLT_ARTICLE_V1_OK;
        }
    }
    *holds_out = true;
    return PPCERTIFICATE_GSLT_ARTICLE_V1_OK;
}

static size_t ppcertificate_gslt_v1_ground_validation_hash(
    const PPCertificateGSLTPatternV1 *pattern,
    uint32_t ambient_depth,
    bool judgment_root) {
    uintptr_t value = (uintptr_t)pattern;

    value >>= 3u;
    value ^= (uintptr_t)ambient_depth * (uintptr_t)UINT32_C(2654435761);
    value ^= judgment_root ? (uintptr_t)UINT32_C(2246822519) : 0u;
    value ^= value >> 16u;
#if UINTPTR_MAX > UINT32_MAX
    value *= UINT64_C(0x9e3779b97f4a7c15);
    value ^= value >> 32u;
#else
    value *= UINT32_C(2246822519);
    value ^= value >> 16u;
#endif
    return (size_t)value;
}

static const PPCertificateGSLTGroundValidationCacheEntryV1 *
ppcertificate_gslt_v1_ground_validation_cache_find(
    const PPCertificateGSLTGroundValidationCacheV1 *cache,
    const PPCertificateGSLTPatternV1 *pattern,
    uint32_t ambient_depth,
    bool judgment_root) {
    size_t slot;
    uint32_t probes;

    if (!cache || cache->cap == 0u)
        return NULL;
    slot = ppcertificate_gslt_v1_ground_validation_hash(
               pattern, ambient_depth, judgment_root) &
           ((size_t)cache->cap - 1u);
    for (probes = 0u; probes < cache->cap; probes++) {
        const PPCertificateGSLTGroundValidationCacheEntryV1 *entry =
            &cache->entries[slot];
        if (!entry->occupied)
            return NULL;
        if (entry->pattern == pattern &&
            entry->ambient_depth == ambient_depth &&
            entry->judgment_root == judgment_root)
            return entry;
        slot = (slot + 1u) & ((size_t)cache->cap - 1u);
    }
    return NULL;
}

static bool ppcertificate_gslt_v1_ground_validation_cache_insert_raw(
    PPCertificateGSLTGroundValidationCacheEntryV1 *entries,
    uint32_t cap,
    PPCertificateGSLTGroundValidationCacheEntryV1 value) {
    size_t slot = ppcertificate_gslt_v1_ground_validation_hash(
                      value.pattern, value.ambient_depth,
                      value.judgment_root) &
                  ((size_t)cap - 1u);
    uint32_t probes;

    for (probes = 0u; probes < cap; probes++) {
        PPCertificateGSLTGroundValidationCacheEntryV1 *entry = &entries[slot];
        if (!entry->occupied) {
            *entry = value;
            entry->occupied = true;
            return true;
        }
        if (entry->pattern == value.pattern &&
            entry->ambient_depth == value.ambient_depth &&
            entry->judgment_root == value.judgment_root)
            return true;
        slot = (slot + 1u) & ((size_t)cap - 1u);
    }
    return false;
}

static bool ppcertificate_gslt_v1_ground_validation_cache_grow(
    PPCertificateGSLTGroundValidationCacheV1 *cache) {
    PPCertificateGSLTGroundValidationCacheEntryV1 *grown;
    uint32_t next_cap;
    uint32_t index;

    next_cap = cache->cap == 0u ? 16u : cache->cap * 2u;
    if (next_cap < cache->cap ||
        !ppcertificate_gslt_v1_allocation_fits(next_cap, sizeof(*grown)))
        return false;
    grown = calloc(next_cap, sizeof(*grown));
    if (!grown)
        return false;
    for (index = 0u; index < cache->cap; index++) {
        if (cache->entries[index].occupied &&
            !ppcertificate_gslt_v1_ground_validation_cache_insert_raw(
                grown, next_cap, cache->entries[index])) {
            free(grown);
            return false;
        }
    }
    free(cache->entries);
    cache->entries = grown;
    cache->cap = next_cap;
    return true;
}

static bool ppcertificate_gslt_v1_ground_validation_cache_add(
    PPCertificateGSLTGroundValidationCacheV1 *cache,
    const PPCertificateGSLTPatternV1 *pattern,
    uint32_t ambient_depth,
    bool judgment_root,
    uint32_t maximum_relative_depth) {
    PPCertificateGSLTGroundValidationCacheEntryV1 value = {
        .pattern = pattern,
        .ambient_depth = ambient_depth,
        .maximum_relative_depth = maximum_relative_depth,
        .judgment_root = judgment_root,
        .occupied = true,
    };

    if (ppcertificate_gslt_v1_ground_validation_cache_find(
            cache, pattern, ambient_depth, judgment_root))
        return true;
    if (cache->len >= cache->maximum_entries)
        return false;
    if (cache->cap == 0u ||
        ((uint64_t)cache->len + 1u) * 10u >=
            (uint64_t)cache->cap * 7u) {
        if (!ppcertificate_gslt_v1_ground_validation_cache_grow(cache))
            return false;
    }
    if (!ppcertificate_gslt_v1_ground_validation_cache_insert_raw(
            cache->entries, cache->cap, value))
        return false;
    cache->len++;
    return true;
}

static bool ppcertificate_gslt_v1_ground_validation_cached_depth(
    const PPCertificateGSLTGroundValidationCacheV1 *cache,
    const PPCertificateGSLTPatternV1 *pattern,
    uint32_t ambient_depth,
    bool judgment_root,
    uint32_t *maximum_relative_depth_out) {
    const PPCertificateGSLTGroundValidationCacheEntryV1 *entry =
        ppcertificate_gslt_v1_ground_validation_cache_find(
            cache, pattern, ambient_depth, judgment_root);

    if (!entry)
        return false;
    *maximum_relative_depth_out = entry->maximum_relative_depth;
    return true;
}

static bool ppcertificate_gslt_v1_ground_validation_push(
    PPCertificateGSLTGroundValidationFrameV1 **frames,
    uint32_t *frame_len,
    uint32_t *frame_cap,
    uint32_t maximum_frames,
    PPCertificateGSLTGroundValidationFrameV1 frame) {
    PPCertificateGSLTGroundValidationFrameV1 *grown;
    uint32_t next_cap;

    if (*frame_len >= maximum_frames)
        return false;
    if (*frame_len < *frame_cap) {
        (*frames)[(*frame_len)++] = frame;
        return true;
    }
    next_cap = *frame_cap == 0u ? 64u : *frame_cap * 2u;
    if (next_cap < *frame_cap || next_cap > maximum_frames)
        next_cap = maximum_frames;
    if (next_cap <= *frame_cap ||
        !ppcertificate_gslt_v1_allocation_fits(next_cap, sizeof(*grown)))
        return false;
    grown = realloc(*frames, (size_t)next_cap * sizeof(*grown));
    if (!grown)
        return false;
    *frames = grown;
    *frame_cap = next_cap;
    (*frames)[(*frame_len)++] = frame;
    return true;
}

static bool ppcertificate_gslt_v1_ground_validation_relative_depth(
    const PPCertificateGSLTGroundValidationCacheV1 *cache,
    const PPCertificateGSLTPatternV1 *pattern,
    uint32_t ambient_depth,
    uint32_t *maximum_relative_depth_out) {
    uint32_t maximum_child_depth = 0u;
    uint32_t child_depth;
    uint32_t child_ambient;
    uint32_t index;
    bool has_child = false;

#define PP_ACCUMULATE_CHILD(child_pattern, child_ambient_depth)              \
    do {                                                                      \
        if (!ppcertificate_gslt_v1_ground_validation_cached_depth(                 \
                cache, (child_pattern), (child_ambient_depth), false,         \
                &child_depth))                                                \
            return false;                                                     \
        if (!has_child || child_depth > maximum_child_depth)                  \
            maximum_child_depth = child_depth;                               \
        has_child = true;                                                     \
    } while (0)

    switch (pattern->kind) {
    case PPCERTIFICATE_GSLT_PATTERN_V1_BVAR:
        break;
    case PPCERTIFICATE_GSLT_PATTERN_V1_APPLY:
        for (index = 0u; index < pattern->as.apply.argument_len; index++)
            PP_ACCUMULATE_CHILD(
                &pattern->as.apply.arguments[index], ambient_depth);
        break;
    case PPCERTIFICATE_GSLT_PATTERN_V1_LAMBDA:
        if (!ppcertificate_gslt_v1_add_depth(ambient_depth, 1u, &child_ambient))
            return false;
        PP_ACCUMULATE_CHILD(pattern->as.lambda.body, child_ambient);
        break;
    case PPCERTIFICATE_GSLT_PATTERN_V1_MULTI_LAMBDA:
        if (!ppcertificate_gslt_v1_add_depth(
                ambient_depth, pattern->as.multi_lambda.arity,
                &child_ambient))
            return false;
        PP_ACCUMULATE_CHILD(
            pattern->as.multi_lambda.body, child_ambient);
        break;
    case PPCERTIFICATE_GSLT_PATTERN_V1_SUBST:
        if (!ppcertificate_gslt_v1_add_depth(ambient_depth, 1u, &child_ambient))
            return false;
        PP_ACCUMULATE_CHILD(pattern->as.subst.body, child_ambient);
        PP_ACCUMULATE_CHILD(
            pattern->as.subst.replacement, ambient_depth);
        break;
    case PPCERTIFICATE_GSLT_PATTERN_V1_COLLECTION:
        for (index = 0u; index < pattern->as.collection.element_len; index++)
            PP_ACCUMULATE_CHILD(
                &pattern->as.collection.elements[index], ambient_depth);
        break;
    case PPCERTIFICATE_GSLT_PATTERN_V1_FVAR:
    default:
        return false;
    }
    if (has_child && maximum_child_depth == UINT32_MAX)
        return false;
    *maximum_relative_depth_out =
        has_child ? maximum_child_depth + 1u : 0u;
#undef PP_ACCUMULATE_CHILD
    return true;
}

static PPCertificateGSLTArticleV1Result ppcertificate_gslt_v1_validate_ground_pattern(
    const PPCertificateGSLTPresentationV1 *presentation,
    const PPCertificateGSLTPatternV1 *pattern,
    uint32_t ambient_depth,
    bool judgment_root,
    const PPCertificateGSLTArticleV1Limits *limits,
    PPCertificateGSLTGroundValidationCacheV1 *cache,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t frame_len = 0u;
    uint32_t remaining = limits->maximum_materialized_pattern_nodes;
    PPCertificateGSLTArticleV1Result result = PPCERTIFICATE_GSLT_ARTICLE_V1_OK;

    if (!cache) {
        ppcertificate_gslt_v1_set_error(
            error_buf, error_buf_size,
            "pattern validation cache is absent");
        return PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID;
    }

#define PP_GROUND_REJECT(message)                                             \
    do {                                                                      \
        ppcertificate_gslt_v1_set_error(                                            \
            error_buf, error_buf_size, (message));                            \
        result = PPCERTIFICATE_GSLT_ARTICLE_V1_REJECTED;                            \
        goto done;                                                            \
    } while (0)

#define PP_GROUND_PUSH_KIND(                                                  \
    next_pattern, next_ambient, next_traversal, next_root, next_finish)       \
    do {                                                                      \
        if (!ppcertificate_gslt_v1_ground_validation_push(                          \
                &cache->frames, &frame_len, &cache->frame_cap,                \
                limits->maximum_materialized_pattern_nodes,                  \
                (PPCertificateGSLTGroundValidationFrameV1){                         \
                    .pattern = (next_pattern),                                \
                    .ambient_depth = (next_ambient),                          \
                    .traversal_depth = (next_traversal),                      \
                    .judgment_root = (next_root),                             \
                    .finish = (next_finish),                                  \
                })) {                                                        \
            ppcertificate_gslt_v1_set_error(                                        \
                error_buf, error_buf_size,                                    \
                "pattern validation work stack exceeds its resource limit"); \
            result = PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;                        \
            goto done;                                                        \
        }                                                                     \
    } while (0)

#define PP_GROUND_PUSH(next_pattern, next_ambient, next_traversal, next_root) \
    PP_GROUND_PUSH_KIND(                                                       \
        next_pattern, next_ambient, next_traversal, next_root, false)

#define PP_GROUND_FINISH(                                                     \
    next_pattern, next_ambient, next_traversal, next_root)                    \
    PP_GROUND_PUSH_KIND(                                                       \
        next_pattern, next_ambient, next_traversal, next_root, true)

    PP_GROUND_PUSH(pattern, ambient_depth, 0u, judgment_root);
    while (frame_len != 0u) {
        PPCertificateGSLTGroundValidationFrameV1 frame =
            cache->frames[--frame_len];
        const PPCertificateGSLTPatternV1 *current = frame.pattern;
        uint32_t child_depth;
        uint32_t inner_depth;
        uint32_t index;
        uint32_t cached_relative_depth;
        bool has_children = false;

        if (!current)
            PP_GROUND_REJECT("pattern is absent");
        if (frame.finish) {
            if (!ppcertificate_gslt_v1_ground_validation_relative_depth(
                    cache, current, frame.ambient_depth,
                    &cached_relative_depth)) {
                ppcertificate_gslt_v1_set_error(
                    error_buf, error_buf_size,
                    "pattern validation cache has an incomplete child");
                result = PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID;
                goto done;
            }
            if (!ppcertificate_gslt_v1_ground_validation_cache_add(
                    cache, current, frame.ambient_depth,
                    frame.judgment_root, cached_relative_depth)) {
                ppcertificate_gslt_v1_set_error(
                    error_buf, error_buf_size,
                    "pattern validation cache exceeds its resource limit");
                result = PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
                goto done;
            }
            continue;
        }
        if (ppcertificate_gslt_v1_ground_validation_cached_depth(
                cache, current, frame.ambient_depth,
                frame.judgment_root, &cached_relative_depth)) {
            if (frame.traversal_depth > limits->maximum_pattern_depth ||
                cached_relative_depth >
                    limits->maximum_pattern_depth - frame.traversal_depth) {
                ppcertificate_gslt_v1_set_error(
                    error_buf, error_buf_size,
                    "pattern validation cached depth exceeds limit %u",
                    limits->maximum_pattern_depth);
                result = PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
                goto done;
            }
            continue;
        }
        if (frame.traversal_depth > limits->maximum_pattern_depth) {
            ppcertificate_gslt_v1_set_error(
                error_buf, error_buf_size,
                "pattern validation depth %u exceeds limit %u",
                frame.traversal_depth, limits->maximum_pattern_depth);
            result = PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
            goto done;
        }
        if (remaining == 0u) {
            ppcertificate_gslt_v1_set_error(
                error_buf, error_buf_size,
                "pattern validation exhausts its node budget");
            result = PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
            goto done;
        }
        remaining--;
        if (frame.judgment_root &&
            current->kind != PPCERTIFICATE_GSLT_PATTERN_V1_APPLY)
            PP_GROUND_REJECT(
                "judgment pattern root is not an application");

        switch (current->kind) {
        case PPCERTIFICATE_GSLT_PATTERN_V1_BVAR:
            if (current->as.bvar >= frame.ambient_depth)
                PP_GROUND_REJECT(
                    "pattern contains an out-of-scope bound variable");
            break;
        case PPCERTIFICATE_GSLT_PATTERN_V1_FVAR:
            PP_GROUND_REJECT(
                "pattern contains an invalid free metavariable occurrence");
        case PPCERTIFICATE_GSLT_PATTERN_V1_APPLY:
            if (!ppcertificate_gslt_v1_name_well_formed(
                    current->as.apply.constructor, false))
                PP_GROUND_REJECT("application constructor is empty");
            if (frame.judgment_root) {
                const PPCertificateGSLTJudgmentV1 *judgment =
                    ppcertificate_gslt_v1_find_judgment(
                        presentation, current->as.apply.constructor);
                if (!judgment)
                    PP_GROUND_REJECT(
                        "pattern has an undeclared judgment head");
                if (judgment->arity != current->as.apply.argument_len)
                    PP_GROUND_REJECT(
                        "application arity disagrees with its declaration");
            }
            if (!ppcertificate_gslt_v1_array_well_formed(
                    current->as.apply.arguments,
                    current->as.apply.argument_len))
                PP_GROUND_REJECT("pattern vector has no storage");
            if (current->as.apply.argument_len == 0u)
                break;
            if (!ppcertificate_gslt_v1_add_depth(
                    frame.traversal_depth, 1u, &child_depth)) {
                ppcertificate_gslt_v1_set_error(
                    error_buf, error_buf_size,
                    "pattern validation depth overflows");
                result = PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
                goto done;
            }
            PP_GROUND_FINISH(
                current, frame.ambient_depth,
                frame.traversal_depth, frame.judgment_root);
            has_children = true;
            index = current->as.apply.argument_len;
            while (index > 0u) {
                index--;
                PP_GROUND_PUSH(
                    &current->as.apply.arguments[index],
                    frame.ambient_depth, child_depth, false);
            }
            break;
        case PPCERTIFICATE_GSLT_PATTERN_V1_LAMBDA:
            if (current->as.lambda.binder_name_present ||
                !current->as.lambda.body ||
                !ppcertificate_gslt_v1_add_depth(
                    frame.ambient_depth, 1u, &inner_depth))
                PP_GROUND_REJECT(
                    "lambda pattern has noncanonical metadata or invalid depth");
            if (!ppcertificate_gslt_v1_add_depth(
                    frame.traversal_depth, 1u, &child_depth)) {
                ppcertificate_gslt_v1_set_error(
                    error_buf, error_buf_size,
                    "pattern validation depth overflows");
                result = PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
                goto done;
            }
            PP_GROUND_FINISH(
                current, frame.ambient_depth,
                frame.traversal_depth, frame.judgment_root);
            has_children = true;
            PP_GROUND_PUSH(
                current->as.lambda.body, inner_depth, child_depth, false);
            break;
        case PPCERTIFICATE_GSLT_PATTERN_V1_MULTI_LAMBDA:
            if (current->as.multi_lambda.binder_name_len != 0u ||
                current->as.multi_lambda.binder_names != NULL ||
                !current->as.multi_lambda.body ||
                !ppcertificate_gslt_v1_add_depth(
                    frame.ambient_depth, current->as.multi_lambda.arity,
                    &inner_depth))
                PP_GROUND_REJECT(
                    "multi-binder pattern has noncanonical metadata or invalid depth");
            if (!ppcertificate_gslt_v1_add_depth(
                    frame.traversal_depth, 1u, &child_depth)) {
                ppcertificate_gslt_v1_set_error(
                    error_buf, error_buf_size,
                    "pattern validation depth overflows");
                result = PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
                goto done;
            }
            PP_GROUND_FINISH(
                current, frame.ambient_depth,
                frame.traversal_depth, frame.judgment_root);
            has_children = true;
            PP_GROUND_PUSH(
                current->as.multi_lambda.body, inner_depth,
                child_depth, false);
            break;
        case PPCERTIFICATE_GSLT_PATTERN_V1_SUBST:
            if (!current->as.subst.body ||
                !current->as.subst.replacement ||
                !ppcertificate_gslt_v1_add_depth(
                    frame.ambient_depth, 1u, &inner_depth))
                PP_GROUND_REJECT(
                    "explicit-substitution pattern is malformed");
            if (!ppcertificate_gslt_v1_add_depth(
                    frame.traversal_depth, 1u, &child_depth)) {
                ppcertificate_gslt_v1_set_error(
                    error_buf, error_buf_size,
                    "pattern validation depth overflows");
                result = PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
                goto done;
            }
            PP_GROUND_FINISH(
                current, frame.ambient_depth,
                frame.traversal_depth, frame.judgment_root);
            has_children = true;
            PP_GROUND_PUSH(
                current->as.subst.replacement, frame.ambient_depth,
                child_depth, false);
            PP_GROUND_PUSH(
                current->as.subst.body, inner_depth, child_depth, false);
            break;
        case PPCERTIFICATE_GSLT_PATTERN_V1_COLLECTION:
            if (current->as.collection.collection_kind >
                    PPCERTIFICATE_GSLT_COLLECTION_V1_HASH_SET ||
                !ppcertificate_gslt_v1_array_well_formed(
                    current->as.collection.elements,
                    current->as.collection.element_len))
                PP_GROUND_REJECT("collection pattern is malformed");
            if (current->as.collection.rest_present) {
                ppcertificate_gslt_v1_set_error(
                    error_buf, error_buf_size,
                    "collection-rest patterns are not supported by this checker");
                result = PPCERTIFICATE_GSLT_ARTICLE_V1_UNSUPPORTED;
                goto done;
            }
            if (current->as.collection.element_len == 0u)
                break;
            if (!ppcertificate_gslt_v1_add_depth(
                    frame.traversal_depth, 1u, &child_depth)) {
                ppcertificate_gslt_v1_set_error(
                    error_buf, error_buf_size,
                    "pattern validation depth overflows");
                result = PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
                goto done;
            }
            PP_GROUND_FINISH(
                current, frame.ambient_depth,
                frame.traversal_depth, frame.judgment_root);
            has_children = true;
            index = current->as.collection.element_len;
            while (index > 0u) {
                index--;
                PP_GROUND_PUSH(
                    &current->as.collection.elements[index],
                    frame.ambient_depth, child_depth, false);
            }
            break;
        default:
            PP_GROUND_REJECT("pattern kind is unknown");
        }
        if (!has_children &&
            !ppcertificate_gslt_v1_ground_validation_cache_add(
                cache, current, frame.ambient_depth,
                frame.judgment_root, 0u)) {
            ppcertificate_gslt_v1_set_error(
                error_buf, error_buf_size,
                "pattern validation cache exceeds its resource limit");
            result = PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
            goto done;
        }
    }

done:
#undef PP_GROUND_PUSH
#undef PP_GROUND_FINISH
#undef PP_GROUND_PUSH_KIND
#undef PP_GROUND_REJECT
    return result;
}

static bool ppcertificate_gslt_v1_article_id_index_find(
    const PPCertificateGSLTArticleIdIndexV1 *id_index,
    uint32_t id,
    uint32_t *index_out) {
    size_t slot;
    uint32_t probes;

    if (!id_index || id_index->cap == 0u)
        return false;
    slot = ((size_t)id * (size_t)UINT32_C(2654435761)) &
           ((size_t)id_index->cap - 1u);
    for (probes = 0u; probes < id_index->cap; probes++) {
        const PPCertificateGSLTArticleIdIndexEntryV1 *entry =
            &id_index->entries[slot];
        if (!entry->occupied)
            return false;
        if (entry->id == id) {
            *index_out = entry->index;
            return true;
        }
        slot = (slot + 1u) & ((size_t)id_index->cap - 1u);
    }
    return false;
}

static PPCertificateGSLTArticleV1Result ppcertificate_gslt_v1_article_id_index_build(
    const PPCertificateGSLTArticleV1 *article,
    PPCertificateGSLTArticleIdIndexV1 *id_index) {
    uint32_t required_cap;
    uint32_t cap = 16u;
    uint32_t index;

    if (article->node_len > UINT32_MAX / 2u)
        return PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
    required_cap = article->node_len * 2u;
    while (cap < required_cap) {
        if (cap > UINT32_MAX / 2u)
            return PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
        cap *= 2u;
    }
    if (!ppcertificate_gslt_v1_allocation_fits(
            cap, sizeof(*id_index->entries)))
        return PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
    id_index->entries = calloc(cap, sizeof(*id_index->entries));
    if (!id_index->entries)
        return PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
    id_index->cap = cap;
    for (index = 0u; index < article->node_len; index++) {
        uint32_t ignored;
        size_t slot;

        if (ppcertificate_gslt_v1_article_id_index_find(
                id_index, article->nodes[index].id, &ignored))
            return PPCERTIFICATE_GSLT_ARTICLE_V1_REJECTED;
        slot = ((size_t)article->nodes[index].id *
                (size_t)UINT32_C(2654435761)) &
               ((size_t)cap - 1u);
        while (id_index->entries[slot].occupied)
            slot = (slot + 1u) & ((size_t)cap - 1u);
        id_index->entries[slot] = (PPCertificateGSLTArticleIdIndexEntryV1){
            .id = article->nodes[index].id,
            .index = index,
            .occupied = true,
        };
    }
    return PPCERTIFICATE_GSLT_ARTICLE_V1_OK;
}

static PPCertificateGSLTArticleV1Result ppcertificate_gslt_v1_article_rooted(
    const PPCertificateGSLTArticleV1 *article,
    const PPCertificateGSLTArticleIdIndexV1 *id_index,
    bool *rooted_out) {
    uint8_t *state = NULL;
    uint32_t *stack = NULL;
    uint32_t stack_len = 0u;
    uint32_t index;
    uint32_t root_index;

    *rooted_out = false;
    if (!ppcertificate_gslt_v1_article_id_index_find(
            id_index, article->root_id, &root_index))
        return PPCERTIFICATE_GSLT_ARTICLE_V1_REJECTED;
    if (article->node_len == 0u ||
        !ppcertificate_gslt_v1_allocation_fits(
            article->node_len, sizeof(*stack)))
        return PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
    state = calloc(article->node_len, sizeof(*state));
    stack = malloc((size_t)article->node_len * sizeof(*stack));
    if (!state || !stack) {
        free(state);
        free(stack);
        return PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
    }
    stack[stack_len++] = root_index;
    state[root_index] = 1u;
    while (stack_len > 0u) {
        const PPCertificateGSLTArticleNodeV1 *node;
        uint32_t node_index = stack[--stack_len];
        uint32_t child_index;

        if (state[node_index] == 2u)
            continue;
        state[node_index] = 2u;
        node = &article->nodes[node_index];
        for (child_index = 0u; child_index < node->child_len;
             child_index++) {
            uint32_t cited_index;
            if (node->children[child_index].kind !=
                PPCERTIFICATE_GSLT_REFERENCE_V1_NODE)
                continue;
            if (!ppcertificate_gslt_v1_article_id_index_find(
                    id_index, node->children[child_index].index,
                    &cited_index)) {
                free(state);
                free(stack);
                return PPCERTIFICATE_GSLT_ARTICLE_V1_REJECTED;
            }
            if (state[cited_index] == 0u) {
                state[cited_index] = 1u;
                stack[stack_len++] = cited_index;
            }
        }
    }
    *rooted_out = true;
    for (index = 0u; index < article->node_len; index++) {
        if (state[index] != 2u) {
            *rooted_out = false;
            break;
        }
    }
    free(state);
    free(stack);
    return PPCERTIFICATE_GSLT_ARTICLE_V1_OK;
}

typedef struct {
    const PPCertificateGSLTPresentationV1 *presentation;
    const PPCertificateGSLTArticleNodeV1 *node;
    uint32_t node_index;
    const PPCertificateGSLTArticleV1Limits *limits;
    PPCertificateGSLTMaterialBudgetV1 *material_budget;
    PPCertificateGSLTGroundValidationCacheV1 *validation_cache;
    PPCertificateGSLTArticleV1Result result;
    char *error_buf;
    size_t error_buf_size;
} PPCertificateGSLTChronologicalApplyContextV1;

static void ppcertificate_gslt_v1_chronological_discard(
    void *context, uintptr_t value) {
    PPCertificateGSLTPatternV1 *pattern = (PPCertificateGSLTPatternV1 *)value;
    (void)context;
    if (!pattern)
        return;
    ppcertificate_gslt_v1_owned_pattern_clear(pattern);
    free(pattern);
}

static bool ppcertificate_gslt_v1_chronological_equal(
    void *context, uintptr_t left, uintptr_t right) {
    const PPCertificateGSLTChronologicalApplyContextV1 *apply_context = context;

    return apply_context &&
           ppcertificate_gslt_v1_pattern_equal_at(
               (const PPCertificateGSLTPatternV1 *)left,
               (const PPCertificateGSLTPatternV1 *)right, 0u,
               apply_context->limits->maximum_pattern_depth);
}

static bool ppcertificate_gslt_v1_chronological_apply(
    void *context,
    uint32_t action,
    const uintptr_t *inputs,
    uint32_t input_len,
    uintptr_t *value_out) {
    PPCertificateGSLTChronologicalApplyContextV1 *apply_context = context;
    const PPCertificateGSLTArticleNodeV1 *node;
    const PPCertificateGSLTRuleSchemaV1 *rule;
    PPCertificateGSLTPatternV1 *premises = NULL;
    PPCertificateGSLTPatternV1 *conclusion = NULL;
    bool conditions_hold = false;
    uint32_t argument_index;
    uint32_t child_index;

    if (!apply_context || !value_out ||
        (input_len != 0u && !inputs) ||
        !(node = apply_context->node) ||
        action >= apply_context->presentation->rule_len) {
        if (apply_context)
            apply_context->result = PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID;
        return false;
    }
    rule = &apply_context->presentation->rules[action];
    if (!ppcertificate_gslt_article_v1_name_equal(
            rule->id, node->rule_instance.rule_id) ||
        rule->formal_len != node->rule_instance.argument_len ||
        rule->premise_len != input_len) {
        ppcertificate_gslt_v1_set_error(
            apply_context->error_buf, apply_context->error_buf_size,
            "proof node does not match a rule signature");
        apply_context->result = PPCERTIFICATE_GSLT_ARTICLE_V1_REJECTED;
        return false;
    }
    for (argument_index = 0u;
         argument_index < rule->formal_len; argument_index++) {
        apply_context->result = ppcertificate_gslt_v1_validate_ground_pattern(
            apply_context->presentation,
            &node->rule_instance.arguments[argument_index],
            rule->formals[argument_index].depth, false,
            apply_context->limits, apply_context->validation_cache,
            apply_context->error_buf, apply_context->error_buf_size);
        if (apply_context->result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            return false;
    }
    apply_context->result = ppcertificate_gslt_v1_side_conditions_hold(
        rule, node->rule_instance.arguments,
        apply_context->material_budget, &conditions_hold);
    if (apply_context->result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
        return false;
    if (!conditions_hold) {
        ppcertificate_gslt_v1_set_error(
            apply_context->error_buf, apply_context->error_buf_size,
            "proof rule side condition is false");
        apply_context->result = PPCERTIFICATE_GSLT_ARTICLE_V1_REJECTED;
        return false;
    }
    apply_context->result = ppcertificate_gslt_v1_allocate_patterns(
        apply_context->material_budget, rule->premise_len, &premises);
    if (apply_context->result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
        return false;
    for (child_index = 0u; child_index < rule->premise_len;
         child_index++) {
        apply_context->result = ppcertificate_gslt_v1_instantiate_pattern(
            rule, node->rule_instance.arguments,
            &rule->premises[child_index], 0u,
            &premises[child_index], apply_context->material_budget, 0u);
        if (apply_context->result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            goto done;
    }
    apply_context->result = ppcertificate_gslt_v1_allocate_patterns(
        apply_context->material_budget, 1u, &conclusion);
    if (apply_context->result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
        goto done;
    apply_context->result = ppcertificate_gslt_v1_instantiate_pattern(
        rule, node->rule_instance.arguments, rule->conclusion, 0u,
        conclusion, apply_context->material_budget, 0u);
    if (apply_context->result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
        goto done;
    for (child_index = 0u; child_index < input_len; child_index++) {
        if (!ppcertificate_gslt_v1_pattern_equal_at(
                (const PPCertificateGSLTPatternV1 *)inputs[child_index],
                &premises[child_index], 0u,
                apply_context->limits->maximum_pattern_depth)) {
            ppcertificate_gslt_v1_set_error(
                apply_context->error_buf, apply_context->error_buf_size,
                "proof node %u (id %u, rule %.*s) child %u does not "
                "establish its ordered premise",
                apply_context->node_index, node->id,
                (int)node->rule_instance.rule_id.len,
                (const char *)node->rule_instance.rule_id.bytes,
                child_index);
            apply_context->result = PPCERTIFICATE_GSLT_ARTICLE_V1_REJECTED;
            goto done;
        }
    }

done:
    for (child_index = 0u; child_index < rule->premise_len;
         child_index++)
        ppcertificate_gslt_v1_owned_pattern_clear(&premises[child_index]);
    free(premises);
    if (apply_context->result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK) {
        ppcertificate_gslt_v1_chronological_discard(
            apply_context, (uintptr_t)conclusion);
        return false;
    }
    *value_out = (uintptr_t)conclusion;
    return true;
}

PPCertificateGSLTArticleV1Result ppcertificate_gslt_article_v1_check_open(
    const PPCertificateGSLTPresentationV1 *presentation,
    const PPCertificateGSLTPatternV1 *context,
    uint32_t context_len,
    const PPCertificateGSLTArticleV1 *article,
    bool require_rooted,
    const PPCertificateGSLTArticleV1Limits *limits_argument,
    PPCertificateGSLTArticleV1Receipt *receipt_out,
    char *error_buf,
    size_t error_buf_size) {
    PPCertificateGSLTArticleV1Limits default_limits;
    const PPCertificateGSLTArticleV1Limits *limits = limits_argument;
    PPCertificateGSLTArticleV1Receipt receipt = {0};
    PPCertificateGSLTMaterialBudgetV1 material_budget = {0};
    PPCertificateGSLTGroundValidationCacheV1 validation_cache = {0};
    PPCertificateGSLTArticleIdIndexV1 id_index = {0};
    CettaGsltChronologicalBuilderV1 builder;
    CettaGsltChronologicalReceiptV1 chronological_receipt = {0};
    CettaGsltChronologicalRefV1 *references = NULL;
    uintptr_t *premise_values = NULL;
    PPCertificateGSLTChronologicalApplyContextV1 apply_context = {0};
    PPCertificateGSLTArticleV1Result result;
    uint32_t used_capabilities = 0u;
    uint32_t reference_cap = 0u;
    uint32_t index = 0u;

    cetta_gslt_chronological_builder_init_v1(&builder);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (receipt_out)
        *receipt_out = receipt;
    if (!limits) {
        default_limits = ppcertificate_gslt_article_v1_default_limits();
        limits = &default_limits;
    }
    result = ppcertificate_gslt_article_v1_presentation_validate(
        presentation, limits, &used_capabilities,
        error_buf, error_buf_size);
    if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
        return result;
    receipt.used_capabilities = used_capabilities;
    if (!article || article->version != 1u || !article->target ||
        !ppcertificate_gslt_v1_array_well_formed(context, context_len) ||
        !ppcertificate_gslt_v1_array_well_formed(article->nodes,
                                           article->node_len) ||
        article->node_len == 0u) {
        ppcertificate_gslt_v1_set_error(
            error_buf, error_buf_size,
            "proof article has an invalid version or outer shape");
        return PPCERTIFICATE_GSLT_ARTICLE_V1_REJECTED;
    }
    if (article->node_len > limits->maximum_article_nodes ||
        !ppcertificate_gslt_v1_allocation_fits(
            article->node_len, sizeof(*article->nodes))) {
        ppcertificate_gslt_v1_set_error(
            error_buf, error_buf_size,
            "proof article exceeds its node limit");
        result = PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
        goto done;
    }
    validation_cache.maximum_entries =
        limits->maximum_materialized_pattern_nodes;
    for (index = 0u; index < context_len; index++) {
        result = ppcertificate_gslt_v1_validate_ground_pattern(
            presentation, &context[index], 0u, true, limits,
            &validation_cache, error_buf, error_buf_size);
        if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
            goto done;
    }
    result = ppcertificate_gslt_v1_validate_ground_pattern(
        presentation, article->target, 0u, true, limits,
        &validation_cache, error_buf, error_buf_size);
    if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
        goto done;

    material_budget = (PPCertificateGSLTMaterialBudgetV1){
        .remaining = limits->maximum_materialized_pattern_nodes,
        .used = 0u,
        .maximum_depth = limits->maximum_pattern_depth,
    };
    if (!ppcertificate_gslt_v1_allocation_fits(
            context_len, sizeof(*premise_values))) {
        result = PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
        goto done;
    }
    if (context_len > 0u) {
        premise_values = malloc(
            (size_t)context_len * sizeof(*premise_values));
        if (!premise_values) {
            result = PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
            goto done;
        }
        for (index = 0u; index < context_len; index++)
            premise_values[index] = (uintptr_t)&context[index];
    }
    apply_context = (PPCertificateGSLTChronologicalApplyContextV1){
        .presentation = presentation,
        .limits = limits,
        .material_budget = &material_budget,
        .validation_cache = &validation_cache,
        .result = PPCERTIFICATE_GSLT_ARTICLE_V1_OK,
        .error_buf = error_buf,
        .error_buf_size = error_buf_size,
    };
    if (!cetta_gslt_chronological_builder_begin_v1(
            &builder, premise_values, context_len,
            ppcertificate_gslt_v1_chronological_apply,
            ppcertificate_gslt_v1_chronological_equal,
            ppcertificate_gslt_v1_chronological_discard,
            &apply_context)) {
        result = PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
        goto done;
    }
    free(premise_values);
    premise_values = NULL;

    for (index = 0u; index < article->node_len; index++) {
        const PPCertificateGSLTArticleNodeV1 *node = &article->nodes[index];
        const PPCertificateGSLTRuleSchemaV1 *rule;
        CettaGsltChronologicalAppendResultV1 append_result;
        uintptr_t value;
        uint32_t child_index;

        if (!ppcertificate_gslt_v1_array_well_formed(
                node->rule_instance.arguments,
                node->rule_instance.argument_len) ||
            !ppcertificate_gslt_v1_array_well_formed(
                node->children, node->child_len)) {
            ppcertificate_gslt_v1_set_error(
                error_buf, error_buf_size,
                "proof node has an invalid vector");
            result = PPCERTIFICATE_GSLT_ARTICLE_V1_REJECTED;
            goto done;
        }
        rule = ppcertificate_gslt_v1_find_rule(
            presentation, node->rule_instance.rule_id);
        if (!rule || rule->formal_len !=
                         node->rule_instance.argument_len ||
            rule->premise_len != node->child_len) {
            ppcertificate_gslt_v1_set_error(
                error_buf, error_buf_size,
                "proof node does not match a rule signature");
            result = PPCERTIFICATE_GSLT_ARTICLE_V1_REJECTED;
            goto done;
        }
        if (node->child_len > reference_cap) {
            CettaGsltChronologicalRefV1 *grown;
            if (!ppcertificate_gslt_v1_allocation_fits(
                    node->child_len, sizeof(*grown))) {
                result = PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
                goto done;
            }
            grown = realloc(
                references, (size_t)node->child_len * sizeof(*grown));
            if (!grown) {
                result = PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE;
                goto done;
            }
            references = grown;
            reference_cap = node->child_len;
        }
        for (child_index = 0u; child_index < node->child_len;
             child_index++) {
            switch (node->children[child_index].kind) {
            case PPCERTIFICATE_GSLT_REFERENCE_V1_PREMISE:
                references[child_index].kind =
                    CETTA_GSLT_CHRONOLOGICAL_PREMISE_REF_V1;
                break;
            case PPCERTIFICATE_GSLT_REFERENCE_V1_NODE:
                references[child_index].kind =
                    CETTA_GSLT_CHRONOLOGICAL_NODE_REF_V1;
                break;
            default:
                ppcertificate_gslt_v1_set_error(
                    error_buf, error_buf_size,
                    "proof node has an invalid reference kind");
                result = PPCERTIFICATE_GSLT_ARTICLE_V1_REJECTED;
                goto done;
            }
            references[child_index].value =
                node->children[child_index].index;
        }

        apply_context.node = node;
        apply_context.node_index = index;
        apply_context.result = PPCERTIFICATE_GSLT_ARTICLE_V1_OK;
        append_result = cetta_gslt_chronological_builder_append_v1(
            &builder, node->id,
            (uint32_t)(rule - presentation->rules),
            references, node->child_len, &value);
        if (append_result == CETTA_GSLT_CHRONOLOGICAL_APPENDED_V1) {
            receipt.checked_node_len++;
            continue;
        }
        if (append_result == CETTA_GSLT_CHRONOLOGICAL_DUPLICATE_V1) {
            ppcertificate_gslt_v1_set_error(
                error_buf, error_buf_size,
                "proof article repeats a node identifier");
            result = PPCERTIFICATE_GSLT_ARTICLE_V1_REJECTED;
            goto done;
        }
        if (append_result ==
            CETTA_GSLT_CHRONOLOGICAL_UNKNOWN_REFERENCE_V1) {
            ppcertificate_gslt_v1_set_error(
                error_buf, error_buf_size,
                "proof node %u cites an unavailable chronological reference",
                index);
            result = PPCERTIFICATE_GSLT_ARTICLE_V1_REJECTED;
            goto done;
        }
        if (append_result == CETTA_GSLT_CHRONOLOGICAL_ACTION_REJECTED_V1) {
            result = apply_context.result;
            if (result == PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
                result = PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID;
            goto done;
        }
        result = append_result == CETTA_GSLT_CHRONOLOGICAL_RESOURCE_V1
            ? PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE
            : PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID;
        goto done;
    }

    if (!cetta_gslt_chronological_builder_finish_v1(
            &builder, article->root_id, (uintptr_t)article->target,
            &chronological_receipt)) {
        ppcertificate_gslt_v1_set_error(
            error_buf, error_buf_size,
            "proof root does not establish the declared target");
        result = PPCERTIFICATE_GSLT_ARTICLE_V1_REJECTED;
        goto done;
    }
    if (chronological_receipt.node_len != receipt.checked_node_len) {
        result = PPCERTIFICATE_GSLT_ARTICLE_V1_INVALID;
        goto done;
    }
    result = ppcertificate_gslt_v1_article_id_index_build(
        article, &id_index);
    if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK) {
        ppcertificate_gslt_v1_set_error(
            error_buf, error_buf_size,
            result == PPCERTIFICATE_GSLT_ARTICLE_V1_REJECTED
                ? "proof article repeats a node identifier"
                : "proof article node index exceeds its resource limit");
        goto done;
    }
    result = ppcertificate_gslt_v1_article_rooted(
        article, &id_index, &receipt.rooted);
    if (result != PPCERTIFICATE_GSLT_ARTICLE_V1_OK)
        goto done;
    if (require_rooted && !receipt.rooted) {
        ppcertificate_gslt_v1_set_error(
            error_buf, error_buf_size,
            "proof article contains a node outside the selected root");
        result = PPCERTIFICATE_GSLT_ARTICLE_V1_REJECTED;
        goto done;
    }
    result = PPCERTIFICATE_GSLT_ARTICLE_V1_OK;

done:
    if (result == PPCERTIFICATE_GSLT_ARTICLE_V1_RESOURCE &&
        error_buf && error_buf_size != 0u && error_buf[0] == '\0') {
        ppcertificate_gslt_v1_set_error(
            error_buf, error_buf_size,
            "proof article exhausted materialization resources at node %u "
            "after %u pattern nodes",
            index, material_budget.used);
    }
    receipt.materialized_pattern_node_len = material_budget.used;
    free(references);
    free(premise_values);
    cetta_gslt_chronological_builder_free_v1(&builder);
    free(validation_cache.frames);
    free(validation_cache.entries);
    free(id_index.entries);
    if (receipt_out)
        *receipt_out = receipt;
    return result;
}

PPCertificateGSLTArticleV1Result ppcertificate_gslt_article_v1_check(
    const PPCertificateGSLTPresentationV1 *presentation,
    const PPCertificateGSLTArticleV1 *article,
    bool require_rooted,
    const PPCertificateGSLTArticleV1Limits *limits,
    PPCertificateGSLTArticleV1Receipt *receipt_out,
    char *error_buf,
    size_t error_buf_size) {
    return ppcertificate_gslt_article_v1_check_open(
        presentation, NULL, 0u, article, require_rooted,
        limits, receipt_out, error_buf, error_buf_size);
}
