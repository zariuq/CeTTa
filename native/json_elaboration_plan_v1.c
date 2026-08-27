#include "json_elaboration_plan_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *label;
    const char *category;
    uint32_t child_len;
    const char *params[4];
    CettaJsonElaborationOpV1 op;
} JsonSourceSpecV1;

typedef struct {
    const char *label;
    const char *category;
    uint32_t param_len;
    const char *params[4];
} JsonTargetSpecV1;

static const JsonSourceSpecV1 json_source_specs[] = {
    {"json:text", "JsonText", 3u, {"JsonWs", "JsonValue", "JsonWs"}, CETTA_JSON_ELAB_TEXT_V1},
    {"json:ws-empty", "JsonWs", 0u, {NULL}, CETTA_JSON_ELAB_WS_EMPTY_V1},
    {"json:ws-cons", "JsonWs", 2u, {"JsonWsChar", "JsonWs"}, CETTA_JSON_ELAB_WS_CONS_V1},
    {"json:value-false", "JsonValue", 0u, {NULL}, CETTA_JSON_ELAB_VALUE_FALSE_V1},
    {"json:value-null", "JsonValue", 0u, {NULL}, CETTA_JSON_ELAB_VALUE_NULL_V1},
    {"json:value-true", "JsonValue", 0u, {NULL}, CETTA_JSON_ELAB_VALUE_TRUE_V1},
    {"json:value-object", "JsonValue", 1u, {"JsonObject"}, CETTA_JSON_ELAB_VALUE_OBJECT_V1},
    {"json:value-array", "JsonValue", 1u, {"JsonArray"}, CETTA_JSON_ELAB_VALUE_ARRAY_V1},
    {"json:value-number", "JsonValue", 1u, {"JsonNumber"}, CETTA_JSON_ELAB_VALUE_NUMBER_V1},
    {"json:value-string", "JsonValue", 1u, {"JsonString"}, CETTA_JSON_ELAB_VALUE_STRING_V1},
    {"json:object", "JsonObject", 3u, {"JsonWs", "JsonMembersOpt", "JsonWs"}, CETTA_JSON_ELAB_OBJECT_V1},
    {"json:members-none", "JsonMembersOpt", 0u, {NULL}, CETTA_JSON_ELAB_MEMBERS_NONE_V1},
    {"json:members-some", "JsonMembersOpt", 1u, {"JsonMembers"}, CETTA_JSON_ELAB_MEMBERS_SOME_V1},
    {"json:members", "JsonMembers", 2u, {"JsonMember", "JsonMemberTail"}, CETTA_JSON_ELAB_MEMBERS_V1},
    {"json:member-tail-empty", "JsonMemberTail", 0u, {NULL}, CETTA_JSON_ELAB_MEMBER_TAIL_EMPTY_V1},
    {"json:member-tail-cons", "JsonMemberTail", 4u, {"JsonWs", "JsonWs", "JsonMember", "JsonMemberTail"}, CETTA_JSON_ELAB_MEMBER_TAIL_CONS_V1},
    {"json:member", "JsonMember", 4u, {"JsonString", "JsonWs", "JsonWs", "JsonValue"}, CETTA_JSON_ELAB_MEMBER_V1},
    {"json:array", "JsonArray", 3u, {"JsonWs", "JsonElementsOpt", "JsonWs"}, CETTA_JSON_ELAB_ARRAY_V1},
    {"json:elements-none", "JsonElementsOpt", 0u, {NULL}, CETTA_JSON_ELAB_ELEMENTS_NONE_V1},
    {"json:elements-some", "JsonElementsOpt", 1u, {"JsonElements"}, CETTA_JSON_ELAB_ELEMENTS_SOME_V1},
    {"json:elements", "JsonElements", 2u, {"JsonValue", "JsonElementTail"}, CETTA_JSON_ELAB_ELEMENTS_V1},
    {"json:element-tail-empty", "JsonElementTail", 0u, {NULL}, CETTA_JSON_ELAB_ELEMENT_TAIL_EMPTY_V1},
    {"json:element-tail-cons", "JsonElementTail", 4u, {"JsonWs", "JsonWs", "JsonValue", "JsonElementTail"}, CETTA_JSON_ELAB_ELEMENT_TAIL_CONS_V1},
    {"json:string", "JsonString", 1u, {"JsonStringChars"}, CETTA_JSON_ELAB_STRING_V1},
    {"json:string-chars-empty", "JsonStringChars", 0u, {NULL}, CETTA_JSON_ELAB_STRING_CHARS_EMPTY_V1},
    {"json:string-chars-cons", "JsonStringChars", 2u, {"JsonStringChar", "JsonStringChars"}, CETTA_JSON_ELAB_STRING_CHARS_CONS_V1},
    {"json:string-char-plain", "JsonStringChar", 1u, {"JsonUnescaped"}, CETTA_JSON_ELAB_STRING_CHAR_PLAIN_V1},
    {"json:string-char-escape", "JsonStringChar", 1u, {"JsonEscape"}, CETTA_JSON_ELAB_STRING_CHAR_ESCAPE_V1},
    {"json:escape-simple", "JsonEscape", 1u, {"JsonSimpleEscape"}, CETTA_JSON_ELAB_ESCAPE_SIMPLE_V1},
    {"json:escape-unicode", "JsonEscape", 4u, {"JsonHexDigit", "JsonHexDigit", "JsonHexDigit", "JsonHexDigit"}, CETTA_JSON_ELAB_ESCAPE_UNICODE_V1},
    {"json:number", "JsonNumber", 4u, {"JsonMinusOpt", "JsonInt", "JsonFracOpt", "JsonExpOpt"}, CETTA_JSON_ELAB_NUMBER_V1},
    {"json:minus-none", "JsonMinusOpt", 0u, {NULL}, CETTA_JSON_ELAB_MINUS_NONE_V1},
    {"json:minus-some", "JsonMinusOpt", 0u, {NULL}, CETTA_JSON_ELAB_MINUS_SOME_V1},
    {"json:int-zero", "JsonInt", 0u, {NULL}, CETTA_JSON_ELAB_INT_ZERO_V1},
    {"json:int-nonzero", "JsonInt", 2u, {"JsonDigit19", "JsonDigits"}, CETTA_JSON_ELAB_INT_NONZERO_V1},
    {"json:digits-empty", "JsonDigits", 0u, {NULL}, CETTA_JSON_ELAB_DIGITS_EMPTY_V1},
    {"json:digits-cons", "JsonDigits", 2u, {"JsonDigit", "JsonDigits"}, CETTA_JSON_ELAB_DIGITS_CONS_V1},
    {"json:frac-none", "JsonFracOpt", 0u, {NULL}, CETTA_JSON_ELAB_FRAC_NONE_V1},
    {"json:frac-some", "JsonFracOpt", 1u, {"JsonFrac"}, CETTA_JSON_ELAB_FRAC_SOME_V1},
    {"json:frac", "JsonFrac", 2u, {"JsonDigit", "JsonDigits"}, CETTA_JSON_ELAB_FRAC_V1},
    {"json:exp-none", "JsonExpOpt", 0u, {NULL}, CETTA_JSON_ELAB_EXP_NONE_V1},
    {"json:exp-some", "JsonExpOpt", 1u, {"JsonExp"}, CETTA_JSON_ELAB_EXP_SOME_V1},
    {"json:exp", "JsonExp", 4u, {"JsonExpMark", "JsonSignOpt", "JsonDigit", "JsonDigits"}, CETTA_JSON_ELAB_EXP_V1},
    {"json:sign-none", "JsonSignOpt", 0u, {NULL}, CETTA_JSON_ELAB_SIGN_NONE_V1},
    {"json:sign-some", "JsonSignOpt", 1u, {"JsonSign"}, CETTA_JSON_ELAB_SIGN_SOME_V1},
};

typedef struct {
    const char *name;
    CettaLdCarrierKindV1 carrier;
} JsonTypeSpecV1;

static const JsonTypeSpecV1 json_target_type_specs[] = {
    {"Bool", CETTA_LD_CARRIER_BUILTIN_BOOL_V1},
    {"NumberLexeme", CETTA_LD_CARRIER_TOKEN_RAW_V1},
    {"OccurrenceId", CETTA_LD_CARRIER_TOKEN_PROOF_V1},
    {"SourceSpan", CETTA_LD_CARRIER_TOKEN_PATH_V1},
    {"UnicodeScalars", CETTA_LD_CARRIER_AST_V1},
    {"Nat", CETTA_LD_CARRIER_BUILTIN_INT_V1},
    {"Value", CETTA_LD_CARRIER_AST_V1},
    {"Member", CETTA_LD_CARRIER_AST_V1},
    {"MemberList", CETTA_LD_CARRIER_AST_V1},
    {"ValueList", CETTA_LD_CARRIER_AST_V1},
};

static const JsonTargetSpecV1 json_target_specs[] = {
    {"JsonNullV1", "Value", 0u, {NULL}},
    {"JsonBoolV1", "Value", 1u, {"Bool"}},
    {"JsonStringV1", "Value", 1u, {"UnicodeScalars"}},
    {"JsonNumberV1", "Value", 1u, {"NumberLexeme"}},
    {"JsonArrayV1", "Value", 1u, {"ValueList"}},
    {"JsonObjectV1", "Value", 1u, {"MemberList"}},
    {"JsonMemberV1", "Member", 4u,
        {"OccurrenceId", "Value", "Value", "SourceSpan"}},
    {"JsonSourceSpanV1", "SourceSpan", 2u, {"Nat", "Nat"}},
    {"JsonNoSourceSpanV1", "SourceSpan", 0u, {NULL}},
};

static bool json_plan_error(char *error_buf, size_t error_buf_size,
                            const char *format, ...) {
    if (error_buf && error_buf_size > 0u) {
        va_list arguments;
        va_start(arguments, format);
        (void)vsnprintf(error_buf, error_buf_size, format, arguments);
        va_end(arguments);
    }
    return false;
}

static bool text_is(const CettaLdTextV1 *text, const char *expected) {
    size_t len = expected ? strlen(expected) : 0u;
    return text && expected && len == text->len &&
        (len == 0u || memcmp(text->bytes, expected, len) == 0);
}

static char *text_copy(const CettaLdTextV1 *text) {
    char *copy;
    if (!text || text->len == UINT32_MAX) return NULL;
    copy = (char *)malloc((size_t)text->len + 1u);
    if (!copy) return NULL;
    if (text->len > 0u) memcpy(copy, text->bytes, text->len);
    copy[text->len] = '\0';
    return copy;
}

static const CettaLdGrammarRuleV1 *find_term(
    const CettaLanguageDefCoreV1 *language, const char *label,
    uint32_t *index_out) {
    uint32_t index;
    for (index = 0u; index < language->term_len; index++) {
        if (text_is(&language->terms[index].label, label)) {
            if (index_out) *index_out = index;
            return &language->terms[index];
        }
    }
    return NULL;
}

static const CettaLdTypeDeclV1 *find_type(
    const CettaLanguageDefCoreV1 *language, const char *name) {
    uint32_t index;
    for (index = 0u; index < language->type_len; index++) {
        if (text_is(&language->types[index].name, name))
            return &language->types[index];
    }
    return NULL;
}

void cetta_json_elaboration_plan_v1_init(
    CettaJsonElaborationPlanV1 *plan) {
    uint32_t index;
    if (!plan) return;
    memset(plan, 0, sizeof(*plan));
    for (index = 0u; index < CETTA_JSON_TARGET_CONSTRUCTOR_COUNT_V1; index++)
        plan->target_term_indices[index] = UINT32_MAX;
}

void cetta_json_elaboration_plan_v1_free(
    CettaJsonElaborationPlanV1 *plan) {
    uint32_t index;
    if (!plan) return;
    for (index = 0u; index < plan->entry_len; index++)
        free(plan->entries[index].label);
    free(plan->entries);
    for (index = 0u; index < CETTA_JSON_TARGET_CONSTRUCTOR_COUNT_V1; index++)
        free(plan->target_names[index]);
    cetta_json_elaboration_plan_v1_init(plan);
}

static bool validate_source(CettaJsonElaborationPlanV1 *candidate,
                            const CettaLanguageDefCoreV1 *source,
                            CettaJsonElaborationPlanV1Status *status,
                            char *error_buf, size_t error_buf_size) {
    uint32_t index;
    if (source->term_len !=
            sizeof(json_source_specs) / sizeof(json_source_specs[0]) ||
        source->equation_len != 0u || source->rewrite_len != 0u) {
        *status = CETTA_JSON_ELAB_PLAN_V1_UNSUPPORTED_SOURCE;
        return json_plan_error(error_buf, error_buf_size,
                               "JSON source constructor inventory changed");
    }
    for (index = 0u; index < source->term_len; index++) {
        const JsonSourceSpecV1 *spec = &json_source_specs[index];
        const CettaLdGrammarRuleV1 *term;
        uint32_t term_index = UINT32_MAX;
        uint32_t param;
        term = find_term(source, spec->label, &term_index);
        if (!term || !text_is(&term->category, spec->category) ||
            term->param_len != spec->child_len || term->eval_policy.present) {
            *status = CETTA_JSON_ELAB_PLAN_V1_UNSUPPORTED_SOURCE;
            return json_plan_error(error_buf, error_buf_size,
                                   "JSON source constructor %s changed shape",
                                   spec->label);
        }
        for (param = 0u; param < spec->child_len; param++) {
            if (term->params[param].kind != CETTA_LD_PARAM_SIMPLE_V1 ||
                term->params[param].type.kind != CETTA_LD_TYPE_BASE_V1 ||
                !text_is(&term->params[param].type.as.base,
                         spec->params[param])) {
                *status = CETTA_JSON_ELAB_PLAN_V1_UNSUPPORTED_SOURCE;
                return json_plan_error(
                    error_buf, error_buf_size,
                    "JSON source constructor %s parameter %u changed type",
                    spec->label, param);
            }
        }
        candidate->entries[index].label = text_copy(&term->label);
        if (!candidate->entries[index].label) {
            *status = CETTA_JSON_ELAB_PLAN_V1_ALLOCATION_FAILURE;
            return json_plan_error(error_buf, error_buf_size,
                                   "out of memory retaining JSON source labels");
        }
        candidate->entries[index].source_term_index = term_index;
        candidate->entries[index].child_len = term->param_len;
        candidate->entries[index].op = spec->op;
        candidate->entry_len++;
    }
    return true;
}

static bool validate_profile(CettaJsonElaborationPlanV1 *candidate,
                             const CettaLdParserProfileV1 *profile,
                             CettaJsonElaborationPlanV1Status *status,
                             char *error_buf, size_t error_buf_size) {
    uint32_t index;
    if (profile->state_len != 8u) {
        *status = CETTA_JSON_ELAB_PLAN_V1_UNSUPPORTED_PROFILE;
        return json_plan_error(error_buf, error_buf_size,
                               "JSON lexical-state inventory changed");
    }
    for (index = 0u; index < profile->state_len; index++) {
        const CettaLdLexicalStateV1 *state = &profile->states[index];
        CettaJsonElaborationPlanEntryV1 *entry =
            &candidate->entries[candidate->entry_len];
        uint32_t duplicate;
        for (duplicate = 0u; duplicate < candidate->entry_len; duplicate++) {
            if (candidate->entries[duplicate].label &&
                strlen(candidate->entries[duplicate].label) == state->label.len &&
                memcmp(candidate->entries[duplicate].label,
                       state->label.bytes, state->label.len) == 0) {
                *status = CETTA_JSON_ELAB_PLAN_V1_UNSUPPORTED_PROFILE;
                return json_plan_error(error_buf, error_buf_size,
                                       "JSON lexical label collides with syntax label");
            }
        }
        entry->label = text_copy(&state->label);
        if (!entry->label) {
            *status = CETTA_JSON_ELAB_PLAN_V1_ALLOCATION_FAILURE;
            return json_plan_error(error_buf, error_buf_size,
                                   "out of memory retaining JSON lexical labels");
        }
        entry->source_term_index = index;
        entry->child_len = 1u;
        entry->op = CETTA_JSON_ELAB_LEXICAL_SCALAR_V1;
        candidate->entry_len++;
    }
    return true;
}

static bool validate_target(CettaJsonElaborationPlanV1 *candidate,
                            const CettaLanguageDefCoreV1 *target,
                            CettaJsonElaborationPlanV1Status *status,
                            char *error_buf, size_t error_buf_size) {
    uint32_t kind;
    if (target->type_len !=
            sizeof(json_target_type_specs) / sizeof(json_target_type_specs[0]) ||
        target->term_len != CETTA_JSON_TARGET_CONSTRUCTOR_COUNT_V1 ||
        target->equation_len != 0u || target->rewrite_len != 0u) {
        *status = CETTA_JSON_ELAB_PLAN_V1_UNSUPPORTED_TARGET;
        return json_plan_error(error_buf, error_buf_size,
                               "JSON target constructor inventory changed");
    }
    for (kind = 0u;
         kind < sizeof(json_target_type_specs) /
                    sizeof(json_target_type_specs[0]);
         kind++) {
        const JsonTypeSpecV1 *spec = &json_target_type_specs[kind];
        const CettaLdTypeDeclV1 *type = find_type(target, spec->name);
        if (!type || type->carrier != spec->carrier) {
            *status = CETTA_JSON_ELAB_PLAN_V1_UNSUPPORTED_TARGET;
            return json_plan_error(
                error_buf, error_buf_size,
                "JSON target type %s changed carrier", spec->name);
        }
    }
    for (kind = 0u; kind < CETTA_JSON_TARGET_CONSTRUCTOR_COUNT_V1; kind++) {
        const JsonTargetSpecV1 *spec = &json_target_specs[kind];
        const CettaLdGrammarRuleV1 *term;
        uint32_t term_index = UINT32_MAX;
        uint32_t param;
        term = find_term(target, spec->label, &term_index);
        if (!term || !text_is(&term->category, spec->category) ||
            term->param_len != spec->param_len || term->eval_policy.present) {
            *status = CETTA_JSON_ELAB_PLAN_V1_UNSUPPORTED_TARGET;
            return json_plan_error(error_buf, error_buf_size,
                                   "JSON target constructor %s changed shape",
                                   spec->label);
        }
        for (param = 0u; param < spec->param_len; param++) {
            if (term->params[param].kind != CETTA_LD_PARAM_SIMPLE_V1 ||
                term->params[param].type.kind != CETTA_LD_TYPE_BASE_V1 ||
                !text_is(&term->params[param].type.as.base,
                         spec->params[param])) {
                *status = CETTA_JSON_ELAB_PLAN_V1_UNSUPPORTED_TARGET;
                return json_plan_error(
                    error_buf, error_buf_size,
                    "JSON target constructor %s parameter %u changed type",
                    spec->label, param);
            }
        }
        candidate->target_names[kind] = text_copy(&term->label);
        if (!candidate->target_names[kind]) {
            *status = CETTA_JSON_ELAB_PLAN_V1_ALLOCATION_FAILURE;
            return json_plan_error(error_buf, error_buf_size,
                                   "out of memory retaining JSON target labels");
        }
        candidate->target_term_indices[kind] = term_index;
    }
    return true;
}

bool cetta_json_elaboration_plan_v1_compile(
    CettaJsonElaborationPlanV1 *out,
    const CettaLanguageDefCoreV1 *source,
    const char source_sha256[65],
    const CettaLdParserProfileV1 *profile,
    const CettaLanguageDefCoreV1 *target,
    const char target_sha256[65],
    CettaJsonElaborationPlanV1Status *status,
    char *error_buf,
    size_t error_buf_size) {
    CettaJsonElaborationPlanV1 candidate;
    uint32_t capacity =
        (uint32_t)(sizeof(json_source_specs) / sizeof(json_source_specs[0])) +
        8u;
    if (error_buf && error_buf_size > 0u) error_buf[0] = '\0';
    if (status) *status = CETTA_JSON_ELAB_PLAN_V1_BAD_ARGUMENT;
    if (!out || !source || !source_sha256 || !profile || !target ||
        !target_sha256 || !status) {
        return json_plan_error(error_buf, error_buf_size,
                               "bad JSON elaboration-plan arguments");
    }
    cetta_json_elaboration_plan_v1_init(&candidate);
    candidate.entries = (CettaJsonElaborationPlanEntryV1 *)calloc(
        capacity, sizeof(*candidate.entries));
    if (!candidate.entries) {
        *status = CETTA_JSON_ELAB_PLAN_V1_ALLOCATION_FAILURE;
        return json_plan_error(error_buf, error_buf_size,
                               "out of memory allocating JSON elaboration plan");
    }
    if (!validate_source(&candidate, source, status,
                         error_buf, error_buf_size) ||
        !validate_profile(&candidate, profile, status,
                          error_buf, error_buf_size) ||
        !validate_target(&candidate, target, status,
                         error_buf, error_buf_size)) {
        cetta_json_elaboration_plan_v1_free(&candidate);
        return false;
    }
    memcpy(candidate.source_sha256, source_sha256, 65u);
    memcpy(candidate.profile_sha256, profile->source_sha256, 65u);
    memcpy(candidate.target_sha256, target_sha256, 65u);
    cetta_json_elaboration_plan_v1_free(out);
    *out = candidate;
    *status = CETTA_JSON_ELAB_PLAN_V1_OK;
    return true;
}

const CettaJsonElaborationPlanEntryV1 *
cetta_json_elaboration_plan_v1_find(
    const CettaJsonElaborationPlanV1 *plan,
    const char *label) {
    uint32_t index;
    if (!plan || !label) return NULL;
    for (index = 0u; index < plan->entry_len; index++) {
        if (plan->entries[index].label &&
            strcmp(plan->entries[index].label, label) == 0)
            return &plan->entries[index];
    }
    return NULL;
}

const char *cetta_json_elaboration_plan_v1_target_name(
    const CettaJsonElaborationPlanV1 *plan,
    CettaJsonTargetConstructorV1 constructor) {
    if (!plan || constructor >= CETTA_JSON_TARGET_CONSTRUCTOR_COUNT_V1)
        return NULL;
    return plan->target_names[constructor];
}

const char *cetta_json_elaboration_plan_v1_status_name(
    CettaJsonElaborationPlanV1Status status) {
    switch (status) {
    case CETTA_JSON_ELAB_PLAN_V1_OK: return "ok";
    case CETTA_JSON_ELAB_PLAN_V1_BAD_ARGUMENT: return "bad-argument";
    case CETTA_JSON_ELAB_PLAN_V1_UNSUPPORTED_SOURCE:
        return "unsupported-source";
    case CETTA_JSON_ELAB_PLAN_V1_UNSUPPORTED_PROFILE:
        return "unsupported-profile";
    case CETTA_JSON_ELAB_PLAN_V1_UNSUPPORTED_TARGET:
        return "unsupported-target";
    case CETTA_JSON_ELAB_PLAN_V1_ALLOCATION_FAILURE:
        return "allocation-failure";
    }
    return "unknown";
}
