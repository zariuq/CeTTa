#include "gslt_petta_direct_v1.h"

#include "gslt_composition_v1.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GSLT_PETTA_DIRECT_DEPTH_LIMIT_V1 4096u

typedef struct {
    uint8_t *bytes;
    size_t len;
    size_t cap;
} DirectBufferV1;

typedef struct {
    const char *name;
    size_t arity;
} DirectOperatorV1;

typedef struct {
    DirectOperatorV1 *items;
    size_t len;
    size_t cap;
} DirectOperatorsV1;

typedef struct {
    const CettaGsltRewriteV1 *source;
    size_t source_index;
    Atom *head;
    Atom *body;
} DirectRuleV1;

typedef struct {
    DirectRuleV1 *items;
    size_t len;
    size_t cap;
} DirectRulesV1;

typedef struct {
    const char *relation;
    size_t arity;
    size_t source_index;
} DirectRuleIndexEntryV1;

typedef struct {
    DirectRuleIndexEntryV1 *items;
    size_t len;
} DirectRuleIndexV1;

typedef struct {
    uint32_t hash;
    size_t source_index;
    uint8_t occupied;
} DirectGroundHeadSlotV1;

typedef struct {
    const Atom **items;
    size_t len;
    size_t cap;
} DirectVariablesV1;

typedef struct {
    const Atom *left;
    const Atom *right;
} DirectVariablePairV1;

typedef struct {
    DirectVariablePairV1 *items;
    size_t len;
    size_t cap;
} DirectVariablePairsV1;

typedef struct {
    const char *relation;
    size_t arity;
    uint8_t *input_ground;
    uint8_t *success_ground;
    uint8_t functional;
    uint8_t input_heads_disjoint;
    uint8_t selected;
    uint8_t admitted;
} DirectBindingModeV1;

typedef struct {
    DirectBindingModeV1 *items;
    size_t len;
    size_t cap;
} DirectBindingModesV1;

/* Compile-time completion types inferred from the ordered authored sources.
 * These describe one provider occurrence, not an entire mutable MeTTa space. */
typedef struct {
    size_t occurrence;
    const char *relation;
    const Atom *left;
    bool not_less;
    bool successor;
    bool unique;
} DirectIntegerNativeTypeV1;

typedef struct {
    const Atom *term;
    uint32_t type; /* one plus an actual LanguageDef type occurrence */
} DirectDomainFactV1;

typedef struct {
    DirectDomainFactV1 *items;
    size_t len;
    bool inconsistent;
} DirectDomainEnvironmentV1;

typedef struct {
    const CettaLanguageDefCoreV1 *language;
    const DirectRulesV1 *rules;
    DirectDomainEnvironmentV1 *environments;
} DirectAdmissionDomainsV1;

typedef struct {
    DirectIntegerNativeTypeV1 *items;
    size_t len;
    const DirectAdmissionDomainsV1 *domains;
} DirectIntegerNativeTypesV1;

static bool direct_target_equations_define_capability_v1(
    const DirectRulesV1 *target_equations,
    const DirectOperatorV1 *capability);

static bool direct_error_v1(char *error, size_t error_size,
                            const char *format, ...) {
    if (error != NULL && error_size > 0u) {
        va_list arguments;
        va_start(arguments, format);
        (void)vsnprintf(error, error_size, format, arguments);
        va_end(arguments);
    }
    return false;
}

static int direct_rule_index_entry_compare_v1(const void *left,
                                              const void *right) {
    const DirectRuleIndexEntryV1 *left_entry =
        (const DirectRuleIndexEntryV1 *)left;
    const DirectRuleIndexEntryV1 *right_entry =
        (const DirectRuleIndexEntryV1 *)right;
    int relation_order = strcmp(left_entry->relation,
                                right_entry->relation);
    if (relation_order != 0)
        return relation_order;
    if (left_entry->arity < right_entry->arity)
        return -1;
    if (left_entry->arity > right_entry->arity)
        return 1;
    if (left_entry->source_index < right_entry->source_index)
        return -1;
    if (left_entry->source_index > right_entry->source_index)
        return 1;
    return 0;
}

static int direct_rule_index_key_compare_v1(
    const DirectRuleIndexEntryV1 *entry, const char *relation,
    size_t arity) {
    int relation_order = strcmp(entry->relation, relation);
    if (relation_order != 0)
        return relation_order;
    if (entry->arity < arity)
        return -1;
    if (entry->arity > arity)
        return 1;
    return 0;
}

static bool direct_rule_index_build_v1(
    const DirectRulesV1 *rules, DirectRuleIndexV1 *index,
    char *error, size_t error_size) {
    if (rules->len > SIZE_MAX / sizeof(*index->items))
        return direct_error_v1(error, error_size,
                               "direct PeTTa rule index is too large");
    index->items = (DirectRuleIndexEntryV1 *)malloc(
        rules->len * sizeof(*index->items));
    if (index->items == NULL)
        return direct_error_v1(
            error, error_size,
            "out of memory indexing direct PeTTa rules");
    index->len = rules->len;
    for (size_t source_index = 0u;
         source_index < rules->len; source_index++) {
        const Atom *head = rules->items[source_index].head;
        index->items[source_index] = (DirectRuleIndexEntryV1){
            .relation = atom_name_cstr(head->expr.elems[0]),
            .arity = (size_t)head->expr.len - 1u,
            .source_index = source_index,
        };
    }
    qsort(index->items, index->len, sizeof(*index->items),
          direct_rule_index_entry_compare_v1);
    return true;
}

static void direct_rule_index_range_v1(
    const DirectRuleIndexV1 *index, const char *relation, size_t arity,
    size_t *begin_out, size_t *end_out) {
    size_t low = 0u;
    size_t high = index->len;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        if (direct_rule_index_key_compare_v1(
                &index->items[middle], relation, arity) < 0)
            low = middle + 1u;
        else
            high = middle;
    }
    *begin_out = low;
    high = index->len;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        if (direct_rule_index_key_compare_v1(
                &index->items[middle], relation, arity) <= 0)
            low = middle + 1u;
        else
            high = middle;
    }
    *end_out = low;
}

static bool direct_reserve_v1(DirectBufferV1 *buffer, size_t extra) {
    size_t required;
    size_t next;
    uint8_t *grown;
    if (extra > SIZE_MAX - buffer->len)
        return false;
    required = buffer->len + extra;
    if (required <= buffer->cap)
        return true;
    next = buffer->cap == 0u ? 512u : buffer->cap;
    while (next < required) {
        if (next > SIZE_MAX / 2u) {
            next = required;
            break;
        }
        next *= 2u;
    }
    grown = (uint8_t *)realloc(buffer->bytes, next);
    if (grown == NULL)
        return false;
    buffer->bytes = grown;
    buffer->cap = next;
    return true;
}

static bool direct_append_v1(DirectBufferV1 *buffer,
                             const void *bytes, size_t len) {
    if (!direct_reserve_v1(buffer, len))
        return false;
    if (len > 0u)
        memcpy(buffer->bytes + buffer->len, bytes, len);
    buffer->len += len;
    return true;
}

static bool direct_literal_v1(DirectBufferV1 *buffer, const char *text) {
    return direct_append_v1(buffer, text, strlen(text));
}

static bool direct_size_v1(DirectBufferV1 *buffer, size_t value) {
    char decimal[32];
    int written = snprintf(decimal, sizeof(decimal), "%zu", value);
    return written >= 0 && (size_t)written < sizeof(decimal) &&
           direct_literal_v1(buffer, decimal);
}

static bool direct_byte_v1(DirectBufferV1 *buffer, uint8_t byte) {
    return direct_append_v1(buffer, &byte, 1u);
}

static bool direct_symbol_v1(const Atom *atom, const char *name) {
    return atom != NULL && atom->kind == ATOM_SYMBOL &&
           strcmp(atom_name_cstr((Atom *)atom), name) == 0;
}

static bool direct_head_v1(const Atom *atom, const char *name,
                           CettaExprLen arity) {
    return atom != NULL && atom->kind == ATOM_EXPR &&
           atom->expr.len == arity + 1u &&
           direct_symbol_v1(atom->expr.elems[0], name);
}

static bool direct_push_operator_v1(DirectOperatorsV1 *operators,
                                    const char *name, size_t arity,
                                    char *error, size_t error_size) {
    for (size_t index = 0u; index < operators->len; index++) {
        if (operators->items[index].arity == arity &&
            strcmp(operators->items[index].name, name) == 0)
            return true;
    }
    if (operators->len == operators->cap) {
        size_t next = operators->cap == 0u ? 32u : operators->cap * 2u;
        DirectOperatorV1 *grown;
        if (next < operators->cap ||
            next > SIZE_MAX / sizeof(*operators->items))
            return direct_error_v1(error, error_size,
                                   "GSLT operator table is too large");
        grown = (DirectOperatorV1 *)realloc(
            operators->items, next * sizeof(*operators->items));
        if (grown == NULL)
            return direct_error_v1(error, error_size,
                                   "out of memory collecting GSLT operators");
        operators->items = grown;
        operators->cap = next;
    }
    operators->items[operators->len++] =
        (DirectOperatorV1){.name = name, .arity = arity};
    return true;
}

static bool direct_push_rule_v1(
    DirectRulesV1 *rules, const CettaGsltRewriteV1 *source,
    size_t source_index, char *error, size_t error_size) {
    if (rules->len == rules->cap) {
        size_t next = rules->cap == 0u ? 64u : rules->cap * 2u;
        DirectRuleV1 *grown;
        if (next < rules->cap || next > SIZE_MAX / sizeof(*rules->items))
            return direct_error_v1(error, error_size,
                                   "GSLT rule table is too large");
        grown = (DirectRuleV1 *)realloc(
            rules->items, next * sizeof(*rules->items));
        if (grown == NULL)
            return direct_error_v1(error, error_size,
                                   "out of memory collecting GSLT rules");
        rules->items = grown;
        rules->cap = next;
    }
    rules->items[rules->len++] =
        (DirectRuleV1){
            .source = source,
            .source_index = source_index,
            .head = source->head,
            .body = source->body,
        };
    return true;
}

static bool direct_positive_arity_v1(const Atom *atom, size_t *out) {
    if (atom == NULL || out == NULL || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_INT || atom->ground.ival <= 0)
        return false;
    if ((uint64_t)atom->ground.ival > SIZE_MAX)
        return false;
    *out = (size_t)atom->ground.ival;
    return true;
}

static bool direct_has_operator_v1(const DirectOperatorsV1 *operators,
                                   const char *name, size_t arity) {
    for (size_t index = 0u; index < operators->len; index++) {
        if (operators->items[index].arity == arity &&
            strcmp(operators->items[index].name, name) == 0)
            return true;
    }
    return false;
}

static bool direct_is_source_variable_v1(const Atom *atom) {
    return cetta_gslt_source_variable_v1(atom);
}

static const char *direct_variable_name_v1(const Atom *atom) {
    return cetta_gslt_source_variable_name_v1(atom);
}

static bool direct_same_variable_v1(const Atom *left, const Atom *right) {
    return direct_is_source_variable_v1(left) &&
           direct_is_source_variable_v1(right) &&
           strcmp(direct_variable_name_v1(left),
                  direct_variable_name_v1(right)) == 0;
}

static bool direct_variables_contains_v1(const DirectVariablesV1 *variables,
                                         const Atom *variable) {
    for (size_t index = 0u; index < variables->len; index++) {
        if (direct_same_variable_v1(variables->items[index], variable))
            return true;
    }
    return false;
}

static bool direct_variables_add_v1(DirectVariablesV1 *variables,
                                    const Atom *variable,
                                    char *error, size_t error_size) {
    if (!direct_is_source_variable_v1(variable) ||
        direct_variables_contains_v1(variables, variable))
        return true;
    if (variables->len == variables->cap) {
        size_t next = variables->cap == 0u ? 16u : variables->cap * 2u;
        const Atom **grown;
        if (next < variables->cap ||
            next > SIZE_MAX / sizeof(*variables->items))
            return direct_error_v1(error, error_size,
                                   "GSLT variable table is too large");
        grown = (const Atom **)realloc(
            variables->items, next * sizeof(*variables->items));
        if (grown == NULL)
            return direct_error_v1(
                error, error_size,
                "out of memory deriving direct PeTTa binding modes");
        variables->items = grown;
        variables->cap = next;
    }
    variables->items[variables->len++] = variable;
    return true;
}

static bool direct_variables_add_term_v1(
    DirectVariablesV1 *variables, const Atom *term, size_t depth,
    char *error, size_t error_size) {
    if (term == NULL || depth > GSLT_PETTA_DIRECT_DEPTH_LIMIT_V1)
        return direct_error_v1(
            error, error_size,
            "GSLT term exceeds the binding-mode analysis depth limit");
    if (direct_is_source_variable_v1(term))
        return direct_variables_add_v1(
            variables, term, error, error_size);
    if (term->kind != ATOM_EXPR)
        return true;
    for (CettaExprIndex index = 0u; index < term->expr.len; index++) {
        if (!direct_variables_add_term_v1(
                variables, term->expr.elems[index], depth + 1u,
                error, error_size))
            return false;
    }
    return true;
}

static bool direct_term_is_ground_v1(const DirectVariablesV1 *variables,
                                     const Atom *term, size_t depth) {
    if (term == NULL || depth > GSLT_PETTA_DIRECT_DEPTH_LIMIT_V1)
        return false;
    if (direct_is_source_variable_v1(term))
        return direct_variables_contains_v1(variables, term);
    if (term->kind != ATOM_EXPR)
        return true;
    for (CettaExprIndex index = 0u; index < term->expr.len; index++) {
        if (!direct_term_is_ground_v1(
                variables, term->expr.elems[index], depth + 1u))
            return false;
    }
    return true;
}

static bool direct_binding_mode_input_equal_v1(
    const DirectBindingModeV1 *mode, const char *relation, size_t arity,
    const uint8_t *input_ground) {
    return mode->arity == arity &&
           strcmp(mode->relation, relation) == 0 &&
           memcmp(mode->input_ground, input_ground, arity) == 0;
}

static bool direct_require_binding_mode_v1(
    DirectBindingModesV1 *modes, const char *relation, size_t arity,
    const uint8_t *input_ground, size_t *mode_index_out, bool *added_out,
    char *error, size_t error_size) {
    for (size_t index = 0u; index < modes->len; index++) {
        if (direct_binding_mode_input_equal_v1(
                &modes->items[index], relation, arity, input_ground)) {
            *mode_index_out = index;
            *added_out = false;
            return true;
        }
    }
    if (modes->len == modes->cap) {
        size_t next = modes->cap == 0u ? 64u : modes->cap * 2u;
        DirectBindingModeV1 *grown;
        if (next < modes->cap ||
            next > SIZE_MAX / sizeof(*modes->items))
            return direct_error_v1(error, error_size,
                                   "GSLT binding-mode table is too large");
        grown = (DirectBindingModeV1 *)realloc(
            modes->items, next * sizeof(*modes->items));
        if (grown == NULL)
            return direct_error_v1(
                error, error_size,
                "out of memory deriving direct PeTTa binding modes");
        modes->items = grown;
        modes->cap = next;
    }
    uint8_t *input = (uint8_t *)malloc(arity);
    uint8_t *success = (uint8_t *)malloc(arity);
    if (input == NULL || success == NULL) {
        free(input);
        free(success);
        return direct_error_v1(
            error, error_size,
            "out of memory deriving direct PeTTa binding modes");
    }
    memcpy(input, input_ground, arity);
    memset(success, 1, arity);
    *mode_index_out = modes->len;
    modes->items[modes->len++] = (DirectBindingModeV1){
        .relation = relation,
        .arity = arity,
        .input_ground = input,
        .success_ground = success,
        .functional = 0u,
        .input_heads_disjoint = 0u,
        .selected = 0u,
    };
    *added_out = true;
    return true;
}

static void direct_free_binding_modes_v1(DirectBindingModesV1 *modes) {
    for (size_t index = 0u; index < modes->len; index++) {
        free(modes->items[index].input_ground);
        free(modes->items[index].success_ground);
    }
    free(modes->items);
    *modes = (DirectBindingModesV1){0};
}

static bool direct_validate_term_v1(const Atom *term,
                                    const DirectOperatorsV1 *operators,
                                    size_t depth,
                                    char *error, size_t error_size) {
    if (term == NULL || depth > GSLT_PETTA_DIRECT_DEPTH_LIMIT_V1)
        return direct_error_v1(error, error_size,
                               "GSLT term exceeds the direct lowering depth limit");
    if (term->kind == ATOM_SYMBOL) {
        const char *name = atom_name_cstr((Atom *)term);
        if (name[0] == '?' && (name[1] == '\0' ||
            (name[1] == '_' && name[2] == '\0')))
            return direct_error_v1(error, error_size,
                                   "GSLT rule contains an anonymous variable");
        return true;
    }
    if (term->kind == ATOM_VAR || term->kind == ATOM_GROUNDED)
        return true;
    if (term->kind != ATOM_EXPR || term->expr.len == 0u ||
        term->expr.elems[0]->kind != ATOM_SYMBOL)
        return direct_error_v1(error, error_size,
                               "GSLT rule contains a malformed application");
    const char *name = atom_name_cstr(term->expr.elems[0]);
    size_t arity = (size_t)term->expr.len - 1u;
    if (!direct_has_operator_v1(operators, name, arity))
        return direct_error_v1(
            error, error_size,
            "GSLT application %s/%zu lacks a signature declaration",
            name, arity);
    for (CettaExprIndex index = 1u; index < term->expr.len; index++) {
        if (!direct_validate_term_v1(term->expr.elems[index], operators,
                                     depth + 1u, error, error_size))
            return false;
    }
    return true;
}

static bool direct_render_term_v1(DirectBufferV1 *buffer, const Atom *term,
                                  Arena *scratch, size_t depth) {
    if (term == NULL || depth > GSLT_PETTA_DIRECT_DEPTH_LIMIT_V1)
        return false;
    if (term->kind == ATOM_SYMBOL) {
        const char *name = atom_name_cstr((Atom *)term);
        if (name[0] == '?' && name[1] != '\0')
            return direct_byte_v1(buffer, (uint8_t)'$') &&
                   direct_literal_v1(buffer, name + 1u);
        return direct_literal_v1(buffer, name);
    }
    if (term->kind == ATOM_VAR) {
        const char *printed = atom_to_parseable_string(scratch, (Atom *)term);
        return printed != NULL && direct_literal_v1(buffer, printed);
    }
    if (term->kind == ATOM_GROUNDED) {
        const char *printed = atom_to_parseable_string(scratch, (Atom *)term);
        return printed != NULL && direct_literal_v1(buffer, printed);
    }
    if (direct_head_v1(term, "metta-nullary", 1u)) {
        const Atom *target = term->expr.elems[1];
        return target->kind == ATOM_SYMBOL &&
               direct_byte_v1(buffer, (uint8_t)'(') &&
               direct_render_term_v1(buffer, target, scratch, depth + 1u) &&
               direct_byte_v1(buffer, (uint8_t)')');
    }
    if (term->kind != ATOM_EXPR || term->expr.len == 0u ||
        !direct_byte_v1(buffer, (uint8_t)'('))
        return false;
    for (CettaExprIndex index = 0u; index < term->expr.len; index++) {
        if (index > 0u && !direct_byte_v1(buffer, (uint8_t)' '))
            return false;
        if (!direct_render_term_v1(buffer, term->expr.elems[index],
                                   scratch, depth + 1u))
            return false;
    }
    return direct_byte_v1(buffer, (uint8_t)')');
}

static bool direct_validate_target_equations_v1(
    const DirectRulesV1 *target_equations,
    const DirectOperatorsV1 *operators,
    char *error, size_t error_size) {
    for (size_t index = 0u; index < target_equations->len; index++) {
        const Atom *equation = target_equations->items[index].head;
        const Atom *left;
        const Atom *right;
        if (!direct_head_v1(equation, "metta-equation", 2u))
            return direct_error_v1(
                error, error_size,
                "direct PeTTa target equation is malformed");
        left = equation->expr.elems[1];
        right = equation->expr.elems[2];
        if (atom_eq((Atom *)left, (Atom *)right))
            return direct_error_v1(
                error, error_size,
                "direct PeTTa rejects a structurally identical target equation");
        if (!direct_validate_term_v1(
                left, operators, 0u, error, error_size) ||
            !direct_validate_term_v1(
                right, operators, 0u, error, error_size))
            return false;
    }
    return true;
}

static bool direct_validate_rules_v1(
    const DirectRulesV1 *rules, const DirectOperatorsV1 *operators,
    DirectOperatorsV1 *relations, DirectOperatorsV1 *capabilities,
    char *error, size_t error_size) {
    for (size_t index = 0u; index < rules->len; index++) {
        Atom *head = rules->items[index].head;
        Atom *body = rules->items[index].body;
        if (head == NULL || head->kind != ATOM_EXPR || head->expr.len == 0u ||
            head->expr.elems[0]->kind != ATOM_SYMBOL)
            return direct_error_v1(error, error_size,
                                   "GSLT rule head must be an application");
        const char *relation = atom_name_cstr(head->expr.elems[0]);
        size_t arity = (size_t)head->expr.len - 1u;
        if (!direct_validate_term_v1(head, operators, 0u,
                                     error, error_size) ||
            !direct_push_operator_v1(relations, relation, arity,
                                     error, error_size))
            return false;
        if (strcmp(relation, "oslf-external-relation-decl-v1") == 0 &&
            arity == 2u && body->expr.len == 1u &&
            head->expr.elems[1]->kind == ATOM_SYMBOL) {
            size_t capability_arity;
            if (!direct_positive_arity_v1(
                    head->expr.elems[2], &capability_arity) ||
                !direct_push_operator_v1(
                    capabilities, atom_name_cstr(head->expr.elems[1]),
                    capability_arity, error, error_size))
                return direct_error_v1(
                    error, error_size,
                    "external relation declarations require a symbolic name and positive arity");
        }
        for (CettaExprIndex body_index = 1u;
             body_index < body->expr.len; body_index++) {
            Atom *goal = body->expr.elems[body_index];
            if (goal == NULL || goal->kind != ATOM_EXPR ||
                goal->expr.len == 0u ||
                goal->expr.elems[0]->kind != ATOM_SYMBOL)
                return direct_error_v1(error, error_size,
                                       "GSLT rule body must contain applications");
            if (!direct_validate_term_v1(goal, operators, 0u,
                                         error, error_size))
                return false;
        }
    }
    for (size_t rule_index = 0u; rule_index < rules->len; rule_index++) {
        Atom *body = rules->items[rule_index].body;
        for (CettaExprIndex body_index = 1u;
             body_index < body->expr.len; body_index++) {
            Atom *goal = body->expr.elems[body_index];
            const char *relation = atom_name_cstr(goal->expr.elems[0]);
            size_t arity = (size_t)goal->expr.len - 1u;
            if (!direct_has_operator_v1(relations, relation, arity) &&
                !direct_has_operator_v1(capabilities, relation, arity))
                return direct_error_v1(
                    error, error_size,
                    "direct PeTTa v1 requires an authored definition or declared backend capability for %s/%zu",
                    relation, arity);
        }
    }
    return true;
}

static bool direct_analyze_binding_mode_v1(
    const DirectRulesV1 *rules, const DirectRuleIndexV1 *rule_index,
    const DirectOperatorsV1 *relations,
    const DirectOperatorsV1 *capabilities,
    const DirectRulesV1 *target_equations, DirectBindingModesV1 *modes,
    size_t mode_index, bool *changed,
    char *error, size_t error_size) {
    const char *relation = modes->items[mode_index].relation;
    size_t arity = modes->items[mode_index].arity;
    const uint8_t *input_ground = modes->items[mode_index].input_ground;
    uint8_t *candidate = (uint8_t *)malloc(arity);
    size_t rule_begin = 0u;
    size_t rule_end = 0u;
    const char *trace_relation = getenv("CETTA_GSLT_DIRECT_MODE_TRACE");
    bool trace = trace_relation != NULL &&
        strcmp(trace_relation, relation) == 0;

    if (candidate == NULL)
        return direct_error_v1(
            error, error_size,
            "out of memory deriving direct PeTTa binding modes");
    memset(candidate, 1, arity);
    direct_rule_index_range_v1(
        rule_index, relation, arity, &rule_begin, &rule_end);

    for (size_t indexed_rule = rule_begin;
         indexed_rule < rule_end; indexed_rule++) {
        size_t source_index =
            rule_index->items[indexed_rule].source_index;
        const DirectRuleV1 *rule = &rules->items[source_index];
        Atom *head = rule->head;
        DirectVariablesV1 known = {0};
        bool rule_ok = true;
        bool rule_reachable = true;

        for (size_t argument = 0u; argument < arity; argument++) {
            if (input_ground[argument] &&
                !direct_variables_add_term_v1(
                    &known, head->expr.elems[argument + 1u], 0u,
                    error, error_size)) {
                rule_ok = false;
                break;
            }
        }
        for (CettaExprIndex body_index = 1u;
             rule_ok && body_index < rule->body->expr.len; body_index++) {
            Atom *goal = rule->body->expr.elems[body_index];
            const char *callee_relation =
                atom_name_cstr(goal->expr.elems[0]);
            size_t callee_arity = (size_t)goal->expr.len - 1u;
            uint8_t *callee_input = (uint8_t *)malloc(callee_arity);
            size_t callee_mode_index = 0u;
            bool added = false;

            if (callee_input == NULL) {
                (void)direct_error_v1(
                    error, error_size,
                    "out of memory deriving direct PeTTa binding modes");
                rule_ok = false;
                break;
            }
            for (size_t argument = 0u;
                 argument < callee_arity; argument++) {
                callee_input[argument] = direct_term_is_ground_v1(
                    &known, goal->expr.elems[argument + 1u], 0u);
            }
            if (direct_has_operator_v1(
                    relations, callee_relation, callee_arity)) {
                if (!direct_require_binding_mode_v1(
                        modes, callee_relation, callee_arity, callee_input,
                        &callee_mode_index, &added, error, error_size)) {
                    free(callee_input);
                    rule_ok = false;
                    break;
                }
                if (added)
                    *changed = true;
                for (size_t argument = 0u;
                     argument < callee_arity; argument++) {
                    if (modes->items[callee_mode_index]
                            .success_ground[argument] &&
                        !direct_variables_add_term_v1(
                            &known, goal->expr.elems[argument + 1u], 0u,
                            error, error_size)) {
                        rule_ok = false;
                        break;
                    }
                }
            } else {
                DirectOperatorV1 capability = {
                    .name = callee_relation,
                    .arity = callee_arity,
                };
                if (!direct_has_operator_v1(
                        capabilities, callee_relation, callee_arity)) {
                    (void)direct_error_v1(
                        error, error_size,
                        "binding-mode analysis found undefined relation %s/%zu",
                        callee_relation, callee_arity);
                    rule_ok = false;
                } else if (!direct_target_equations_define_capability_v1(
                               target_equations, &capability)) {
                    rule_reachable = false;
                    if (trace)
                        fprintf(stderr,
                                "[gslt-direct-mode] %s/%zu binding-body=%s default-empty=1\n",
                                relation, arity, callee_relation);
                    free(callee_input);
                    continue;
                }
            }
            free(callee_input);
        }
        if (trace) {
            fprintf(stderr,
                    "[gslt-direct-mode] %s/%zu binding-input=",
                    relation, arity);
            for (size_t argument = 0u; argument < arity; argument++)
                fprintf(stderr, "%u", (unsigned)input_ground[argument]);
            fprintf(stderr,
                    " rule=%zu reachable=%u head-ground=",
                    source_index,
                    rule_reachable ? 1u : 0u);
            for (size_t argument = 0u; argument < arity; argument++)
                fprintf(stderr, "%u", (unsigned)direct_term_is_ground_v1(
                    &known, head->expr.elems[argument + 1u], 0u));
            fputc('\n', stderr);
        }
        if (rule_ok && rule_reachable) {
            for (size_t argument = 0u; argument < arity; argument++) {
                bool ground = input_ground[argument] ||
                    direct_term_is_ground_v1(
                        &known, head->expr.elems[argument + 1u], 0u);
                candidate[argument] =
                    (uint8_t)(candidate[argument] && ground);
            }
        }
        free(known.items);
        if (!rule_ok) {
            free(candidate);
            return false;
        }
    }
    if (rule_begin == rule_end) {
        free(candidate);
        return direct_error_v1(
            error, error_size,
            "binding-mode analysis found relation %s/%zu without rules",
            relation, arity);
    }
    for (size_t argument = 0u; argument < arity; argument++) {
        if (modes->items[mode_index].success_ground[argument] &&
            !candidate[argument]) {
            modes->items[mode_index].success_ground[argument] = 0u;
            *changed = true;
        }
    }
    free(candidate);
    return true;
}

static bool direct_analyze_binding_modes_v1(
    const DirectRulesV1 *rules, const DirectRuleIndexV1 *rule_index,
    const DirectOperatorsV1 *relations,
    const DirectOperatorsV1 *capabilities,
    const DirectRulesV1 *target_equations,
    const char *const *entry_modes, size_t entry_mode_count,
    DirectBindingModesV1 *modes,
    char *error, size_t error_size) {
    for (size_t relation_index = 0u;
         relation_index < relations->len; relation_index++) {
        const DirectOperatorV1 *relation = &relations->items[relation_index];
        uint8_t *seed = (uint8_t *)calloc(relation->arity, 1u);
        size_t ignored_index;
        bool ignored_added;
        if (seed == NULL)
            return direct_error_v1(
                error, error_size,
                "out of memory deriving direct PeTTa binding modes");
        if (!direct_require_binding_mode_v1(
                modes, relation->name, relation->arity, seed,
                &ignored_index, &ignored_added, error, error_size)) {
            free(seed);
            return false;
        }
        memset(seed, 1, relation->arity);
        if (!direct_require_binding_mode_v1(
                modes, relation->name, relation->arity, seed,
                &ignored_index, &ignored_added, error, error_size)) {
            free(seed);
            return false;
        }
        for (size_t output = 0u; output < relation->arity; output++) {
            seed[output] = 0u;
            if (!direct_require_binding_mode_v1(
                    modes, relation->name, relation->arity, seed,
                    &ignored_index, &ignored_added, error, error_size)) {
                free(seed);
                return false;
            }
            seed[output] = 1u;
        }
        free(seed);
    }

    for (size_t entry_index = 0u;
         entry_index < entry_mode_count; entry_index++) {
        const char *entry = entry_modes[entry_index];
        const char *separator = entry != NULL ? strrchr(entry, ':') : NULL;
        const DirectOperatorV1 *relation = NULL;
        uint8_t *input_ground;
        size_t mode_index = 0u;
        bool added = false;
        size_t relation_len;
        size_t bit_len;

        if (separator == NULL || separator == entry || separator[1] == '\0')
            return direct_error_v1(
                error, error_size,
                "direct PeTTa entry mode must be RELATION:BITS");
        relation_len = (size_t)(separator - entry);
        bit_len = strlen(separator + 1u);
        for (size_t relation_index = 0u;
             relation_index < relations->len; relation_index++) {
            const DirectOperatorV1 *candidate =
                &relations->items[relation_index];
            if (strlen(candidate->name) == relation_len &&
                strncmp(candidate->name, entry, relation_len) == 0) {
                relation = candidate;
                break;
            }
        }
        if (relation == NULL)
            return direct_error_v1(
                error, error_size,
                "direct PeTTa entry mode names an undefined relation");
        if (bit_len != relation->arity)
            return direct_error_v1(
                error, error_size,
                "direct PeTTa entry mode arity does not match its relation");
        input_ground = (uint8_t *)malloc(relation->arity);
        if (input_ground == NULL)
            return direct_error_v1(
                error, error_size,
                "out of memory deriving direct PeTTa entry mode");
        for (size_t bit = 0u; bit < relation->arity; bit++) {
            if (separator[bit + 1u] != '0' &&
                separator[bit + 1u] != '1') {
                free(input_ground);
                return direct_error_v1(
                    error, error_size,
                    "direct PeTTa entry mode contains a non-binary bit");
            }
            input_ground[bit] =
                (uint8_t)(separator[bit + 1u] == '1');
        }
        if (!direct_require_binding_mode_v1(
                modes, relation->name, relation->arity, input_ground,
                &mode_index, &added, error, error_size)) {
            free(input_ground);
            return false;
        }
        free(input_ground);
        modes->items[mode_index].selected = 1u;
    }

    bool changed;
    do {
        changed = false;
        for (size_t mode_index = 0u; mode_index < modes->len; mode_index++) {
            if (!direct_analyze_binding_mode_v1(
                    rules, rule_index, relations, capabilities,
                    target_equations, modes, mode_index,
                    &changed, error, error_size))
                return false;
        }
    } while (changed);
    return true;
}

static const DirectBindingModeV1 *direct_find_binding_mode_v1(
    const DirectBindingModesV1 *modes, const char *relation, size_t arity,
    const uint8_t *input_ground) {
    for (size_t index = 0u; index < modes->len; index++) {
        if (direct_binding_mode_input_equal_v1(
                &modes->items[index], relation, arity, input_ground))
            return &modes->items[index];
    }
    return NULL;
}

static bool direct_patterns_may_unify_v1(const Atom *left,
                                         const Atom *right,
                                         size_t depth) {
    if (left == NULL || right == NULL ||
        depth > GSLT_PETTA_DIRECT_DEPTH_LIMIT_V1)
        return true;
    if (direct_is_source_variable_v1(left) ||
        direct_is_source_variable_v1(right))
        return true;
    if (left->kind != right->kind)
        return false;
    switch (left->kind) {
    case ATOM_SYMBOL:
        return strcmp(atom_name_cstr((Atom *)left),
                      atom_name_cstr((Atom *)right)) == 0;
    case ATOM_VAR:
        return true;
    case ATOM_GROUNDED:
        return atom_eq((Atom *)left, (Atom *)right);
    case ATOM_EXPR:
        if (left->expr.len != right->expr.len)
            return false;
        for (CettaExprIndex index = 0u;
             index < left->expr.len; index++) {
            if (!direct_patterns_may_unify_v1(
                    left->expr.elems[index], right->expr.elems[index],
                    depth + 1u))
                return false;
        }
        return true;
    }
    return true;
}

static bool direct_variable_pairs_add_v1(
    DirectVariablePairsV1 *pairs, const Atom *left, const Atom *right,
    char *error, size_t error_size) {
    for (size_t index = 0u; index < pairs->len; index++) {
        bool same_left = direct_same_variable_v1(
            pairs->items[index].left, left);
        bool same_right = direct_same_variable_v1(
            pairs->items[index].right, right);
        if (same_left || same_right)
            return same_left && same_right;
    }
    if (pairs->len == pairs->cap) {
        size_t next = pairs->cap == 0u ? 16u : pairs->cap * 2u;
        DirectVariablePairV1 *grown;
        if (next < pairs->cap ||
            next > SIZE_MAX / sizeof(*pairs->items))
            return direct_error_v1(
                error, error_size,
                "GSLT variable-pair table is too large");
        grown = (DirectVariablePairV1 *)realloc(
            pairs->items, next * sizeof(*pairs->items));
        if (grown == NULL)
            return direct_error_v1(
                error, error_size,
                "out of memory deriving functional guard exclusivity");
        pairs->items = grown;
        pairs->cap = next;
    }
    pairs->items[pairs->len++] = (DirectVariablePairV1){
        .left = left,
        .right = right,
    };
    return true;
}

static bool direct_align_input_patterns_v1(
    const Atom *left, const Atom *right, DirectVariablePairsV1 *pairs,
    size_t depth, char *error, size_t error_size) {
    bool left_variable = direct_is_source_variable_v1(left);
    bool right_variable = direct_is_source_variable_v1(right);
    if (left == NULL || right == NULL ||
        depth > GSLT_PETTA_DIRECT_DEPTH_LIMIT_V1)
        return false;
    if (left_variable || right_variable) {
        return left_variable && right_variable &&
               direct_variable_pairs_add_v1(
                   pairs, left, right, error, error_size);
    }
    if (left->kind != right->kind)
        return false;
    if (left->kind == ATOM_SYMBOL)
        return strcmp(atom_name_cstr((Atom *)left),
                      atom_name_cstr((Atom *)right)) == 0;
    if (left->kind == ATOM_GROUNDED)
        return atom_eq((Atom *)left, (Atom *)right);
    if (left->kind != ATOM_EXPR ||
        left->expr.len != right->expr.len)
        return false;
    for (CettaExprIndex index = 0u;
         index < left->expr.len; index++) {
        if (!direct_align_input_patterns_v1(
                left->expr.elems[index], right->expr.elems[index], pairs,
                depth + 1u, error, error_size))
            return false;
    }
    return true;
}

static bool direct_align_overlapping_input_patterns_v1(
    const Atom *left, const Atom *right, DirectVariablePairsV1 *pairs,
    size_t depth, char *error, size_t error_size) {
    bool left_variable = direct_is_source_variable_v1(left);
    bool right_variable = direct_is_source_variable_v1(right);
    if (left == NULL || right == NULL ||
        depth > GSLT_PETTA_DIRECT_DEPTH_LIMIT_V1)
        return false;
    if (left_variable || right_variable) {
        if (left_variable && right_variable) {
            return direct_variable_pairs_add_v1(
                pairs, left, right, error, error_size);
        }
        return true;
    }
    if (left->kind != right->kind)
        return false;
    if (left->kind == ATOM_SYMBOL)
        return strcmp(atom_name_cstr((Atom *)left),
                      atom_name_cstr((Atom *)right)) == 0;
    if (left->kind == ATOM_GROUNDED)
        return atom_eq((Atom *)left, (Atom *)right);
    if (left->kind != ATOM_EXPR ||
        left->expr.len != right->expr.len)
        return false;
    for (CettaExprIndex index = 0u;
         index < left->expr.len; index++) {
        if (!direct_align_overlapping_input_patterns_v1(
                left->expr.elems[index], right->expr.elems[index], pairs,
                depth + 1u, error, error_size))
            return false;
    }
    return true;
}

static bool direct_terms_correspond_v1(
    const Atom *left, const Atom *right,
    const DirectVariablePairsV1 *pairs, size_t depth) {
    bool left_variable = direct_is_source_variable_v1(left);
    bool right_variable = direct_is_source_variable_v1(right);
    if (left == NULL || right == NULL ||
        depth > GSLT_PETTA_DIRECT_DEPTH_LIMIT_V1)
        return false;
    if (left_variable || right_variable) {
        if (!left_variable || !right_variable)
            return false;
        for (size_t index = 0u; index < pairs->len; index++) {
            if (direct_same_variable_v1(pairs->items[index].left, left) &&
                direct_same_variable_v1(pairs->items[index].right, right))
                return true;
        }
        return false;
    }
    if (left->kind != right->kind)
        return false;
    if (left->kind == ATOM_SYMBOL)
        return strcmp(atom_name_cstr((Atom *)left),
                      atom_name_cstr((Atom *)right)) == 0;
    if (left->kind == ATOM_GROUNDED)
        return atom_eq((Atom *)left, (Atom *)right);
    if (left->kind != ATOM_EXPR ||
        left->expr.len != right->expr.len)
        return false;
    for (CettaExprIndex index = 0u;
         index < left->expr.len; index++) {
        if (!direct_terms_correspond_v1(
                left->expr.elems[index], right->expr.elems[index], pairs,
                depth + 1u))
            return false;
    }
    return true;
}

typedef struct {
    size_t unequal_left_argument;
    size_t unequal_right_argument;
    bool equality_arguments_quoted;
} DirectGroundDisequalityCapabilityV1;

static bool direct_terms_equal_v1(const Atom *left, const Atom *right,
                                  size_t depth) {
    bool left_variable = direct_is_source_variable_v1(left);
    bool right_variable = direct_is_source_variable_v1(right);
    if (left == NULL || right == NULL ||
        depth > GSLT_PETTA_DIRECT_DEPTH_LIMIT_V1)
        return false;
    if (left_variable || right_variable)
        return left_variable && right_variable &&
               direct_same_variable_v1(left, right);
    if (left->kind != right->kind)
        return false;
    if (left->kind == ATOM_SYMBOL)
        return strcmp(atom_name_cstr((Atom *)left),
                      atom_name_cstr((Atom *)right)) == 0;
    if (left->kind == ATOM_VAR)
        return direct_same_variable_v1(left, right);
    if (left->kind == ATOM_GROUNDED)
        return atom_eq((Atom *)left, (Atom *)right);
    if (left->kind != ATOM_EXPR ||
        left->expr.len != right->expr.len)
        return false;
    for (CettaExprIndex index = 0u; index < left->expr.len; index++) {
        if (!direct_terms_equal_v1(
                left->expr.elems[index], right->expr.elems[index],
                depth + 1u))
            return false;
    }
    return true;
}

/* Source authentication uses source identity, not numeric equivalence.
 * This compares the parsed constructors, including the grounded value tag.
 * It does not claim identity of original whitespace or comments. */
static bool direct_source_equal_v1(
    const Atom *left, const Atom *right, Arena *scratch, size_t depth) {
    if (left == NULL || right == NULL || left->kind != right->kind ||
        depth > GSLT_PETTA_DIRECT_DEPTH_LIMIT_V1)
        return false;
    if (left->kind == ATOM_EXPR) {
        if (left->expr.len != right->expr.len)
            return false;
        for (CettaExprIndex i = 0u; i < left->expr.len; i++) {
            if (!direct_source_equal_v1(left->expr.elems[i],
                                        right->expr.elems[i], scratch,
                                        depth + 1u))
                return false;
        }
        return true;
    }
    if (left->kind == ATOM_GROUNDED &&
        left->ground.gkind != right->ground.gkind)
        return false;
    const char *a = atom_to_parseable_string(scratch, (Atom *)left);
    const char *b = atom_to_parseable_string(scratch, (Atom *)right);
    return a != NULL && b != NULL && strcmp(a, b) == 0;
}

static bool direct_literal_integer_v1(const Atom *term, int64_t *value) {
    if (term == NULL || term->kind != ATOM_GROUNDED ||
        term->ground.gkind != GV_INT)
        return false;
    *value = term->ground.ival;
    return true;
}

/* A conservative overlap test: repeated pattern variables can only make
 * this test reject more optimizations, never establish false uniqueness. */
static bool direct_patterns_overlap_v1(
    const Atom *left, const Atom *right, size_t depth) {
    if (left == NULL || right == NULL ||
        depth > GSLT_PETTA_DIRECT_DEPTH_LIMIT_V1)
        return true;
    if (direct_is_source_variable_v1(left) ||
        direct_is_source_variable_v1(right))
        return true;
    if (left->kind != right->kind)
        return false;
    if (left->kind == ATOM_SYMBOL)
        return strcmp(atom_name_cstr((Atom *)left),
                      atom_name_cstr((Atom *)right)) == 0;
    if (left->kind == ATOM_GROUNDED)
        return atom_eq((Atom *)left, (Atom *)right);
    if (left->kind != ATOM_EXPR)
        return true;
    if (left->expr.len != right->expr.len)
        return false;
    for (CettaExprIndex i = 0u; i < left->expr.len; i++) {
        if (!direct_patterns_overlap_v1(left->expr.elems[i],
                                        right->expr.elems[i], depth + 1u))
            return false;
    }
    return true;
}

/* The finite decoder is the selected binary completion-type inference's
 * source shape. Names are extracted, never supplied by a provider registry. */
static bool direct_infer_integer_native_type_v1(
    const DirectRuleV1 *rule, DirectIntegerNativeTypeV1 *result) {
    const Atom *equation = rule->head;
    if (!direct_head_v1(equation, "metta-equation", 2u))
        return false;
    const Atom *left = equation->expr.elems[1];
    const Atom *right = equation->expr.elems[2];
    if (left->kind != ATOM_EXPR || left->expr.len != 2u ||
        left->expr.elems[0]->kind != ATOM_SYMBOL)
        return false;
    const Atom *query = left->expr.elems[1];
    if (query->kind != ATOM_EXPR || query->expr.len != 3u ||
        query->expr.elems[0]->kind != ATOM_SYMBOL)
        return false;
    const char *relation = atom_name_cstr(query->expr.elems[0]);
    const char *wrapper = atom_name_cstr(left->expr.elems[0]);
    if (strncmp(wrapper, "gslt:", 5u) != 0 ||
        strcmp(wrapper + 5u, relation) != 0 ||
        !direct_is_source_variable_v1(query->expr.elems[1]) ||
        !direct_is_source_variable_v1(query->expr.elems[2]) ||
        direct_same_variable_v1(query->expr.elems[1], query->expr.elems[2]) ||
        !direct_head_v1(right, "if", 3u))
        return false;
    const Atom *condition = right->expr.elems[1];
    const Atom *success = right->expr.elems[2];
    const Atom *failure = right->expr.elems[3];
    bool not_less = direct_head_v1(condition, ">=", 2u);
    if ((!not_less && !direct_head_v1(condition, "<", 2u)) ||
        !direct_head_v1(success, "quote", 1u) ||
        !direct_terms_equal_v1(success->expr.elems[1], query, 0u) ||
        !direct_head_v1(failure, "metta-nullary", 1u) ||
        !direct_symbol_v1(failure->expr.elems[1], "empty") ||
        !direct_same_variable_v1(condition->expr.elems[2],
                                 query->expr.elems[2]))
        return false;
    const Atom *operand = condition->expr.elems[1];
    bool successor = direct_head_v1(operand, "+", 2u);
    int64_t one;
    if (successor) {
        if (!direct_same_variable_v1(operand->expr.elems[1],
                                     query->expr.elems[1]) ||
            !direct_literal_integer_v1(operand->expr.elems[2], &one) ||
            one != 1)
            return false;
    } else if (!direct_same_variable_v1(operand, query->expr.elems[1])) {
        return false;
    }
    *result = (DirectIntegerNativeTypeV1){
        .occurrence = rule->source_index, .relation = relation,
        .left = left, .not_less = not_less, .successor = successor,
    };
    return true;
}

static bool direct_authenticate_integer_native_types_v1(
    const Atom *packet, Atom *const *presentations, size_t presentation_count,
    const DirectRulesV1 *target_equations, const DirectOperatorsV1 *relations,
    DirectIntegerNativeTypesV1 *types,
    Arena *scratch, char *error, size_t error_size) {
    if (packet == NULL)
        return true;
    if (!direct_head_v1(packet, "integer-provider-native-types-v1", 2u))
        return direct_error_v1(error, error_size,
                               "malformed integer native-type packet");
    const Atom *sources = packet->expr.elems[1];
    const Atom *rows = packet->expr.elems[2];
    if (!direct_head_v1(sources, "source-composition", presentation_count) ||
        rows->kind != ATOM_EXPR || rows->expr.len == 0u ||
        !direct_symbol_v1(rows->expr.elems[0], "types"))
        return direct_error_v1(error, error_size,
                               "malformed integer native-type source binding");
    for (size_t i = 0u; i < presentation_count; i++) {
        if (!direct_source_equal_v1(presentations[i],
                                    sources->expr.elems[i + 1u], scratch, 0u))
            return direct_error_v1(error, error_size,
                                   "integer native-type source composition differs");
    }
    if (target_equations->len > SIZE_MAX / sizeof(*types->items))
        return direct_error_v1(error, error_size,
                               "integer native-type inventory is too large");
    if (target_equations->len > 0u) {
        types->items = calloc(target_equations->len, sizeof(*types->items));
        if (types->items == NULL)
            return direct_error_v1(error, error_size,
                                   "out of memory decoding integer native types");
    }
    for (size_t i = 0u; i < target_equations->len; i++) {
        DirectIntegerNativeTypeV1 inferred;
        if (!direct_infer_integer_native_type_v1(
                &target_equations->items[i], &inferred))
            continue;
        if (types->len + 1u >= (size_t)rows->expr.len)
            return direct_error_v1(error, error_size,
                                   "integer native-type inventory is incomplete");
        const Atom *row = rows->expr.elems[types->len + 1u];
        int64_t occurrence;
        if (!direct_head_v1(row, "binary-integer-completion-v1", 4u) ||
            !direct_literal_integer_v1(row->expr.elems[1], &occurrence) ||
            occurrence < 0 || (uint64_t)occurrence != inferred.occurrence ||
            !direct_symbol_v1(row->expr.elems[2], inferred.relation) ||
            !direct_symbol_v1(row->expr.elems[3],
                             inferred.not_less ? "not-less" : "less") ||
            !direct_symbol_v1(row->expr.elems[4],
                             inferred.successor ? "successor" : "direct"))
            return direct_error_v1(error, error_size,
                                   "integer native type disagrees with its source occurrence");
        size_t matching = 0u;
        bool primitive_override = false;
        for (size_t j = 0u; j < target_equations->len; j++) {
            const Atom *lhs = target_equations->items[j].head->expr.elems[1];
            if (direct_patterns_overlap_v1(lhs, inferred.left, 0u))
                matching++;
            if (direct_is_source_variable_v1(lhs))
                primitive_override = true;
            /* Rendering decodes this constructor to a nullary target call.
             * Do not inspect its source tag as if that were the target head. */
            if (direct_head_v1(lhs, "metta-nullary", 1u))
                primitive_override = true;
            if (lhs->kind == ATOM_EXPR && lhs->expr.len > 0u) {
                const Atom *head = lhs->expr.elems[0];
                if (direct_is_source_variable_v1(head) ||
                    direct_symbol_v1(head, "if") ||
                    direct_symbol_v1(head, "quote") ||
                    direct_symbol_v1(head, "empty") ||
                    direct_symbol_v1(head, "<") ||
                    direct_symbol_v1(head, ">=") ||
                    direct_symbol_v1(head, "+"))
                    primitive_override = true;
                if (head->kind == ATOM_SYMBOL) {
                    const char *name = atom_name_cstr((Atom *)head);
                    if (strncmp(name, "gslt:fn:", 8u) == 0 ||
                        strncmp(name, "gslt:fnix:", 10u) == 0 ||
                        strncmp(name, "gslt:mode:", 10u) == 0 ||
                        strncmp(name, "gslt:entry:", 11u) == 0)
                        primitive_override = true;
                }
            }
        }
        for (size_t j = 0u; j < relations->len; j++) {
            const char *name = relations->items[j].name;
            if (strncmp(name, "fn:", 3u) == 0 ||
                strncmp(name, "fnix:", 5u) == 0 ||
                strncmp(name, "mode:", 5u) == 0 ||
                strncmp(name, "entry:", 6u) == 0)
                primitive_override = true;
        }
        /* Ordinary relation rules take precedence in body-mode selection and
         * emit their own wrappers. A target-equation NTT says nothing about
         * their answer stream, even if the names happen to coincide. */
        inferred.unique = matching == 1u && !primitive_override &&
            !direct_has_operator_v1(relations, inferred.relation, 2u);
        types->items[types->len++] = inferred;
    }
    if (types->len != (size_t)rows->expr.len - 1u)
        return direct_error_v1(error, error_size,
                               "integer native-type inventory contains extra occurrences");
    return true;
}

static bool direct_domain_text_v1(const CettaLdTextV1 *text, const char *name) {
    return strlen(name) == text->len &&
        memcmp(text->bytes, name, text->len) == 0;
}

static uint32_t direct_domain_type_v1(
    const CettaLanguageDefCoreV1 *language, const CettaLdTextV1 *name) {
    uint32_t found = 0u;
    for (uint32_t i = 0u; i < language->type_len; i++) {
        const CettaLdTextV1 *candidate = &language->types[i].name;
        if (candidate->len == name->len &&
            memcmp(candidate->bytes, name->bytes, name->len) == 0) {
            if (found != 0u) return 0u;
            found = i + 1u;
        }
    }
    return found;
}

static bool direct_domain_bind_v1(
    DirectDomainEnvironmentV1 *env, const Atom *term, uint32_t type) {
    if (type == 0u) return true;
    for (size_t i = 0u; i < env->len; i++) {
        if (direct_terms_equal_v1(env->items[i].term, term, 0u)) {
            if (env->items[i].type != type) env->inconsistent = true;
            return true;
        }
    }
    if (env->len == SIZE_MAX / sizeof(*env->items)) return false;
    DirectDomainFactV1 *grown = realloc(
        env->items, (env->len + 1u) * sizeof(*env->items));
    if (grown == NULL) return false;
    env->items = grown;
    env->items[env->len++] = (DirectDomainFactV1){term, type};
    return true;
}

/* The source nullary marker is syntax, not a second guest constructor. */
static const char *direct_domain_constructor_v1(
    const Atom *term, size_t *arity) {
    if (direct_head_v1(term, "metta-nullary", 1u) &&
        term->expr.elems[1]->kind == ATOM_SYMBOL) {
        *arity = 0u;
        return atom_name_cstr(term->expr.elems[1]);
    }
    if (term->kind != ATOM_EXPR || term->expr.len == 0u ||
        term->expr.elems[0]->kind != ATOM_SYMBOL) return NULL;
    *arity = (size_t)term->expr.len - 1u;
    return atom_name_cstr(term->expr.elems[0]);
}

static const CettaLdGrammarRuleV1 *direct_domain_rule_v1(
    const CettaLanguageDefCoreV1 *language, const Atom *term,
    uint32_t expected) {
    size_t arity;
    const char *name = direct_domain_constructor_v1(term, &arity);
    const CettaLdGrammarRuleV1 *found = NULL;
    if (name == NULL) return NULL;
    for (uint32_t i = 0u; i < language->term_len; i++) {
        const CettaLdGrammarRuleV1 *rule = &language->terms[i];
        if (!direct_domain_text_v1(&rule->label, name) ||
            rule->param_len != arity) continue;
        uint32_t category = direct_domain_type_v1(language, &rule->category);
        if (category == 0u || (expected != 0u && category != expected)) continue;
        if (found != NULL) return NULL;
        found = rule;
    }
    return found;
}

static bool direct_domain_pattern_v1(
    const CettaLanguageDefCoreV1 *language, const Atom *pattern,
    uint32_t type, DirectDomainEnvironmentV1 *env, size_t depth) {
    if (type == 0u || type > language->type_len ||
        depth > GSLT_PETTA_DIRECT_DEPTH_LIMIT_V1) return true;
    if (direct_is_source_variable_v1(pattern))
        return direct_domain_bind_v1(env, pattern, type);
    if (language->types[type - 1u].carrier != CETTA_LD_CARRIER_AST_V1)
        return true;
    const CettaLdGrammarRuleV1 *rule =
        direct_domain_rule_v1(language, pattern, type);
    if (rule == NULL) return true;
    /* Reusing this exact matched pattern reconstructs an admitted subterm.
     * This is not a license to build arbitrary new constructor expressions. */
    if (!direct_domain_bind_v1(env, pattern, type)) return false;
    for (uint32_t i = 0u; i < rule->param_len; i++) {
        const CettaLdTermParamV1 *param = &rule->params[i];
        if (param->kind != CETTA_LD_PARAM_SIMPLE_V1 ||
            param->type.kind != CETTA_LD_TYPE_BASE_V1) continue;
        if (!direct_domain_pattern_v1(language, pattern->expr.elems[i + 1u],
                direct_domain_type_v1(language, &param->type.as.base),
                env, depth + 1u)) return false;
    }
    return true;
}

static uint32_t direct_domain_term_v1(
    const CettaLanguageDefCoreV1 *language,
    const DirectDomainEnvironmentV1 *env, const Atom *term, size_t depth) {
    if (env->inconsistent || depth > GSLT_PETTA_DIRECT_DEPTH_LIMIT_V1) return 0u;
    for (size_t i = 0u; i < env->len; i++)
        if (direct_terms_equal_v1(env->items[i].term, term, 0u))
            return env->items[i].type;
    if (term->kind == ATOM_GROUNDED) {
        CettaLdCarrierKindV1 carrier;
        if (term->ground.gkind == GV_INT) carrier = CETTA_LD_CARRIER_BUILTIN_INT_V1;
        else if (term->ground.gkind == GV_STRING) carrier = CETTA_LD_CARRIER_BUILTIN_STRING_V1;
        else return 0u;
        uint32_t found = 0u;
        for (uint32_t i = 0u; i < language->type_len; i++) {
            if (language->types[i].carrier != carrier) continue;
            if (found != 0u) return 0u;
            found = i + 1u;
        }
        return found;
    }
    /* Reconstructed expressions may be evaluated at the guest call boundary.
     * Their constructor shape alone does not establish the returned carrier.
     * Only exact matched-subterm reuse and literals acquire a carrier here. */
    return 0u;
}

static size_t direct_domain_relation_v1(
    const DirectOperatorsV1 *relations, const char *name, size_t arity) {
    for (size_t i = 0u; i < relations->len; i++)
        if (relations->items[i].arity == arity &&
            strcmp(relations->items[i].name, name) == 0) return i;
    return relations->len;
}

static void direct_domains_free_v1(DirectAdmissionDomainsV1 *domains) {
    if (domains->environments != NULL)
        for (size_t i = 0u; i < domains->rules->len; i++)
            free(domains->environments[i].items);
    free(domains->environments);
    *domains = (DirectAdmissionDomainsV1){0};
}

/* A finite, conservative join over calls from ONE admitted entry. Facts
 * describe input values, never values returned by an unknown provider.
 * Conflicting call domains join to unknown. The output is compile-time
 * information only; no schema interpreter is emitted into the workers. */
static bool direct_domains_build_v1(
    DirectAdmissionDomainsV1 *domains, const CettaLanguageDefCoreV1 *language,
    const DirectRulesV1 *rules, const DirectOperatorsV1 *relations,
    const char *entry_relation, size_t entry_arity, size_t entry_input,
    uint32_t entry_type, char *error, size_t error_size) {
    uint32_t **inputs = calloc(relations->len, sizeof(*inputs));
    bool *reached = calloc(relations->len, sizeof(*reached));
    bool ok = false;
    domains->language = language;
    domains->rules = rules;
    domains->environments = calloc(rules->len, sizeof(*domains->environments));
    if (inputs == NULL || reached == NULL || domains->environments == NULL) goto done;
    for (size_t i = 0u; i < relations->len; i++) {
        size_t arity = relations->items[i].arity;
        if (arity > SIZE_MAX / sizeof(**inputs)) goto done;
        inputs[i] = malloc((arity == 0u ? 1u : arity) * sizeof(**inputs));
        if (inputs[i] == NULL) goto done;
        for (size_t j = 0u; j < arity; j++) inputs[i][j] = UINT32_MAX;
    }
    size_t entry = direct_domain_relation_v1(relations, entry_relation, entry_arity);
    if (entry == relations->len) goto done;
    reached[entry] = true;
    for (size_t j = 0u; j < entry_arity; j++) inputs[entry][j] = 0u;
    inputs[entry][entry_input] = entry_type;
    bool changed;
    do {
        changed = false;
        for (size_t i = 0u; i < rules->len; i++) {
            const DirectRuleV1 *rule = &rules->items[i];
            size_t arity = (size_t)rule->head->expr.len - 1u;
            size_t relation = direct_domain_relation_v1(relations,
                atom_name_cstr(rule->head->expr.elems[0]), arity);
            if (relation == relations->len || !reached[relation])
                continue;
            DirectDomainEnvironmentV1 *env = &domains->environments[i];
            free(env->items);
            *env = (DirectDomainEnvironmentV1){0};
            for (size_t j = 0u; j < arity; j++)
                if (!direct_domain_pattern_v1(language, rule->head->expr.elems[j + 1u],
                        inputs[relation][j], env, 0u)) goto done;
            for (CettaExprIndex b = 1u; b < rule->body->expr.len; b++) {
                const Atom *call = rule->body->expr.elems[b];
                size_t width = (size_t)call->expr.len - 1u;
                size_t callee = direct_domain_relation_v1(relations,
                    atom_name_cstr(call->expr.elems[0]), width);
                if (callee == relations->len) continue;
                if (!reached[callee]) { reached[callee] = true; changed = true; }
                for (size_t j = 0u; j < width; j++) {
                    uint32_t type = direct_domain_term_v1(language, env,
                        call->expr.elems[j + 1u], 0u);
                    uint32_t before = inputs[callee][j];
                    uint32_t after = before == UINT32_MAX ? type :
                        before == type ? before : 0u;
                    if (after != before) { inputs[callee][j] = after; changed = true; }
                }
            }
        }
    } while (changed);
    ok = true;
done:
    if (inputs != NULL)
        for (size_t i = 0u; i < relations->len; i++) free(inputs[i]);
    free(inputs);
    free(reached);
    if (!ok) {
        direct_domains_free_v1(domains);
        return direct_error_v1(error, error_size, "cannot derive admitted entry domains");
    }
    return true;
}

static bool direct_domain_integer_v1(
    const DirectAdmissionDomainsV1 *domains, const DirectRuleV1 *rule,
    const Atom *term) {
    if (domains == NULL || rule == NULL) return false;
    for (size_t i = 0u; i < domains->rules->len; i++) {
        if (&domains->rules->items[i] != rule) continue;
        uint32_t type = direct_domain_term_v1(domains->language,
            &domains->environments[i], term, 0u);
        return type != 0u && domains->language->types[type - 1u].carrier ==
            CETTA_LD_CARRIER_BUILTIN_INT_V1;
    }
    return false;
}

/* Literals are independent of caller typing. A separate admitted family
 * additionally uses transported Integer carriers. Groundness alone never
 * suffices; a variable successor still has no overflow license. */
static const DirectIntegerNativeTypeV1 *direct_integer_call_native_type_v1(
    const DirectIntegerNativeTypesV1 *types, const Atom *goal,
    const DirectRuleV1 *caller) {
    if (types->len == 0u || goal->kind != ATOM_EXPR || goal->expr.len != 3u ||
        goal->expr.elems[0]->kind != ATOM_SYMBOL ||
        direct_is_source_variable_v1(goal->expr.elems[0]))
        return NULL;
    int64_t left = 0;
    int64_t right = 0;
    bool left_literal = direct_literal_integer_v1(goal->expr.elems[1], &left);
    if ((!left_literal && !direct_domain_integer_v1(types->domains, caller,
                                                   goal->expr.elems[1])) ||
        (!direct_literal_integer_v1(goal->expr.elems[2], &right) &&
         !direct_domain_integer_v1(types->domains, caller, goal->expr.elems[2])))
        return NULL;
    (void)right;
    for (size_t i = 0u; i < types->len; i++) {
        const DirectIntegerNativeTypeV1 *type = &types->items[i];
        if (type->unique &&
            (!type->successor || (left_literal && left < INT64_MAX)) &&
            direct_symbol_v1(goal->expr.elems[0], type->relation))
            return type;
    }
    return NULL;
}

static bool direct_integer_guards_exclusive_v1(
    const DirectRuleV1 *left, const DirectRuleV1 *right,
    const DirectIntegerNativeTypesV1 *types,
    const DirectVariablePairsV1 *pairs, bool aligned) {
    if (types->len == 0u)
        return false;
    for (CettaExprIndex i = 1u; i < left->body->expr.len; i++) {
        const Atom *a = left->body->expr.elems[i];
        const DirectIntegerNativeTypeV1 *ta =
            direct_integer_call_native_type_v1(types, a, left);
        if (ta == NULL)
            continue;
        for (CettaExprIndex j = 1u; j < right->body->expr.len; j++) {
            const Atom *b = right->body->expr.elems[j];
            const DirectIntegerNativeTypeV1 *tb =
                direct_integer_call_native_type_v1(types, b, right);
            if (tb != NULL && ta->not_less != tb->not_less &&
                ta->successor == tb->successor &&
                aligned &&
                direct_terms_correspond_v1(a->expr.elems[1], b->expr.elems[1], pairs, 0u) &&
                direct_terms_correspond_v1(a->expr.elems[2], b->expr.elems[2], pairs, 0u))
                return true;
        }
    }
    return false;
}

static bool direct_target_ground_disequality_capability_v1(
    const DirectRulesV1 *target_equations, const char *capability,
    size_t arity, DirectGroundDisequalityCapabilityV1 *result) {
    const Atom *definition = NULL;
    const Atom *relation = NULL;
    char wrapper[512];
    int written = snprintf(
        wrapper, sizeof(wrapper), "gslt:%s", capability);
    if (written < 0 || (size_t)written >= sizeof(wrapper))
        return false;

    for (size_t index = 0u; index < target_equations->len; index++) {
        const Atom *equation = target_equations->items[index].head;
        const Atom *left;
        const Atom *candidate;
        if (!direct_head_v1(equation, "metta-equation", 2u) ||
            !(left = equation->expr.elems[1]) ||
            !direct_head_v1(left, wrapper, 1u) ||
            !(candidate = left->expr.elems[1]) ||
            !direct_head_v1(candidate, capability, (CettaExprLen)arity))
            continue;
        if (definition != NULL)
            return false;
        definition = equation;
        relation = candidate;
    }
    if (definition == NULL || relation == NULL)
        return false;

    for (size_t argument = 0u; argument < arity; argument++) {
        const Atom *variable = relation->expr.elems[argument + 1u];
        if (!direct_is_source_variable_v1(variable))
            return false;
        for (size_t earlier = 0u; earlier < argument; earlier++) {
            if (direct_same_variable_v1(
                    variable, relation->expr.elems[earlier + 1u]))
                return false;
        }
    }

    const Atom *right = definition->expr.elems[2];
    const Atom *condition;
    const Atom *empty_branch;
    const Atom *success_branch;
    if (!direct_head_v1(right, "if", 3u) ||
        !(condition = right->expr.elems[1]) ||
        !direct_head_v1(condition, "==", 2u) ||
        !(empty_branch = right->expr.elems[2]) ||
        !direct_head_v1(empty_branch, "metta-nullary", 1u) ||
        !direct_symbol_v1(empty_branch->expr.elems[1], "empty") ||
        !(success_branch = right->expr.elems[3]))
        return false;
    if (direct_head_v1(success_branch, "quote", 1u))
        success_branch = success_branch->expr.elems[1];
    if (!direct_terms_equal_v1(success_branch, relation, 0u))
        return false;

    const Atom *left_condition = condition->expr.elems[1];
    const Atom *right_condition = condition->expr.elems[2];
    bool left_quoted = direct_head_v1(left_condition, "quote", 1u);
    bool right_quoted = direct_head_v1(right_condition, "quote", 1u);
    if (left_quoted != right_quoted)
        return false;
    if (left_quoted) {
        left_condition = left_condition->expr.elems[1];
        right_condition = right_condition->expr.elems[1];
    }

    size_t left_argument = SIZE_MAX;
    size_t right_argument = SIZE_MAX;
    for (size_t argument = 0u; argument < arity; argument++) {
        const Atom *parameter = relation->expr.elems[argument + 1u];
        if (direct_terms_equal_v1(
                left_condition, parameter, 0u))
            left_argument = argument;
        if (direct_terms_equal_v1(
                right_condition, parameter, 0u))
            right_argument = argument;
    }
    if (left_argument == SIZE_MAX || right_argument == SIZE_MAX ||
        left_argument == right_argument)
        return false;
    result->unequal_left_argument = left_argument;
    result->unequal_right_argument = right_argument;
    result->equality_arguments_quoted = left_quoted;
    return true;
}

static bool direct_capability_call_is_ground_semidet_v1(
    const DirectRulesV1 *target_equations,
    const DirectOperatorsV1 *capabilities, const Atom *goal,
    const uint8_t *input_ground,
    const DirectIntegerNativeTypesV1 *native_types,
    const DirectRuleV1 *caller) {
    const char *relation = atom_name_cstr(goal->expr.elems[0]);
    size_t arity = (size_t)goal->expr.len - 1u;
    DirectOperatorV1 capability = {
        .name = relation,
        .arity = arity,
    };
    DirectGroundDisequalityCapabilityV1 ignored;
    if (!direct_has_operator_v1(capabilities, relation, arity))
        return false;
    if (!direct_target_equations_define_capability_v1(
            target_equations, &capability))
        return true;
    for (size_t argument = 0u; argument < arity; argument++) {
        if (!input_ground[argument])
            return false;
    }
    return direct_integer_call_native_type_v1(native_types, goal, caller) != NULL ||
           direct_target_ground_disequality_capability_v1(
               target_equations, relation, arity, &ignored);
}

static bool direct_variable_pairs_push_v1(
    DirectVariablePairsV1 *pairs, const Atom *left, const Atom *right,
    char *error, size_t error_size) {
    for (size_t index = 0u; index < pairs->len; index++) {
        if (direct_same_variable_v1(pairs->items[index].left, left) &&
            direct_same_variable_v1(pairs->items[index].right, right))
            return true;
    }
    if (pairs->len == pairs->cap) {
        size_t next = pairs->cap == 0u ? 8u : pairs->cap * 2u;
        DirectVariablePairV1 *grown;
        if (next < pairs->cap ||
            next > SIZE_MAX / sizeof(*pairs->items))
            return direct_error_v1(
                error, error_size,
                "GSLT nonlinear equality table is too large");
        grown = (DirectVariablePairV1 *)realloc(
            pairs->items, next * sizeof(*pairs->items));
        if (grown == NULL)
            return direct_error_v1(
                error, error_size,
                "out of memory deriving nonlinear guard exclusivity");
        pairs->items = grown;
        pairs->cap = next;
    }
    pairs->items[pairs->len++] = (DirectVariablePairV1){
        .left = left,
        .right = right,
    };
    return true;
}

static bool direct_collect_nonlinear_equalities_v1(
    const Atom *equality_pattern, const Atom *guarded_pattern,
    DirectVariablePairsV1 *mapping,
    DirectVariablePairsV1 *equalities, size_t depth,
    char *error, size_t error_size) {
    bool equality_variable = direct_is_source_variable_v1(equality_pattern);
    bool guarded_variable = direct_is_source_variable_v1(guarded_pattern);
    if (equality_pattern == NULL || guarded_pattern == NULL ||
        depth > GSLT_PETTA_DIRECT_DEPTH_LIMIT_V1)
        return false;
    if (equality_variable || guarded_variable) {
        if (!equality_variable || !guarded_variable)
            return false;
        for (size_t index = 0u; index < mapping->len; index++) {
            if (!direct_same_variable_v1(
                    mapping->items[index].left, equality_pattern))
                continue;
            if (direct_same_variable_v1(
                    mapping->items[index].right, guarded_pattern))
                return true;
            return direct_variable_pairs_push_v1(
                equalities, mapping->items[index].right, guarded_pattern,
                error, error_size);
        }
        return direct_variable_pairs_push_v1(
            mapping, equality_pattern, guarded_pattern, error, error_size);
    }
    if (equality_pattern->kind != guarded_pattern->kind)
        return false;
    if (equality_pattern->kind == ATOM_SYMBOL)
        return strcmp(atom_name_cstr((Atom *)equality_pattern),
                      atom_name_cstr((Atom *)guarded_pattern)) == 0;
    if (equality_pattern->kind == ATOM_GROUNDED)
        return atom_eq((Atom *)equality_pattern, (Atom *)guarded_pattern);
    if (equality_pattern->kind != ATOM_EXPR ||
        equality_pattern->expr.len != guarded_pattern->expr.len)
        return false;
    for (CettaExprIndex index = 0u;
         index < equality_pattern->expr.len; index++) {
        if (!direct_collect_nonlinear_equalities_v1(
                equality_pattern->expr.elems[index],
                guarded_pattern->expr.elems[index], mapping, equalities,
                depth + 1u, error, error_size))
            return false;
    }
    return true;
}

static bool direct_rules_have_nonlinear_guard_complement_v1(
    const DirectRuleV1 *equality_rule,
    const DirectRuleV1 *guarded_rule,
    const DirectBindingModeV1 *parent_mode,
    const DirectRulesV1 *target_equations,
    const DirectOperatorsV1 *capabilities,
    char *error, size_t error_size) {
    DirectVariablePairsV1 mapping = {0};
    DirectVariablePairsV1 equalities = {0};
    DirectVariablesV1 known = {0};
    bool exclusive = false;

    for (size_t argument = 0u; argument < parent_mode->arity; argument++) {
        if (!parent_mode->input_ground[argument])
            continue;
        if (!direct_collect_nonlinear_equalities_v1(
                equality_rule->head->expr.elems[argument + 1u],
                guarded_rule->head->expr.elems[argument + 1u],
                &mapping, &equalities, 0u, error, error_size) ||
            !direct_variables_add_term_v1(
                &known, guarded_rule->head->expr.elems[argument + 1u],
                0u, error, error_size))
            goto done;
    }
    if (equalities.len == 0u)
        goto done;

    for (CettaExprIndex body_index = 1u;
         body_index < guarded_rule->body->expr.len && !exclusive;
         body_index++) {
        const Atom *goal = guarded_rule->body->expr.elems[body_index];
        const char *relation = atom_name_cstr(goal->expr.elems[0]);
        size_t arity = (size_t)goal->expr.len - 1u;
        DirectGroundDisequalityCapabilityV1 guard;
        if (!direct_has_operator_v1(capabilities, relation, arity) ||
            !direct_target_ground_disequality_capability_v1(
                target_equations, relation, arity, &guard))
            continue;
        bool ground = true;
        for (size_t argument = 0u; argument < arity; argument++) {
            if (!direct_term_is_ground_v1(
                    &known, goal->expr.elems[argument + 1u], 0u)) {
                ground = false;
                break;
            }
        }
        if (!ground)
            continue;
        const Atom *guard_left =
            goal->expr.elems[guard.unequal_left_argument + 1u];
        const Atom *guard_right =
            goal->expr.elems[guard.unequal_right_argument + 1u];
        for (size_t index = 0u; index < equalities.len; index++) {
            const Atom *equal_left = equalities.items[index].left;
            const Atom *equal_right = equalities.items[index].right;
            if ((direct_same_variable_v1(guard_left, equal_left) &&
                 direct_same_variable_v1(guard_right, equal_right)) ||
                (direct_same_variable_v1(guard_left, equal_right) &&
                 direct_same_variable_v1(guard_right, equal_left))) {
                exclusive = true;
                break;
            }
        }
    }

done:
    free(known.items);
    free(equalities.items);
    free(mapping.items);
    return exclusive;
}

static bool direct_guard_binding_push_v1(
    DirectVariablePairsV1 *bindings, const Atom *variable,
    const Atom *term, char *error, size_t error_size) {
    for (size_t index = 0u; index < bindings->len; index++) {
        if (!direct_same_variable_v1(
                bindings->items[index].left, variable))
            continue;
        return direct_terms_equal_v1(
            bindings->items[index].right, term, 0u);
    }
    if (bindings->len == bindings->cap) {
        size_t next = bindings->cap == 0u ? 8u : bindings->cap * 2u;
        DirectVariablePairV1 *grown;
        if (next < bindings->cap ||
            next > SIZE_MAX / sizeof(*bindings->items))
            return direct_error_v1(
                error, error_size,
                "GSLT guarded-pattern binding table is too large");
        grown = (DirectVariablePairV1 *)realloc(
            bindings->items, next * sizeof(*bindings->items));
        if (grown == NULL)
            return direct_error_v1(
                error, error_size,
                "out of memory deriving guarded-pattern exclusivity");
        bindings->items = grown;
        bindings->cap = next;
    }
    bindings->items[bindings->len++] = (DirectVariablePairV1){
        .left = variable,
        .right = term,
    };
    return true;
}

/* Prove that every value matched by `specific` is also matched by `general`,
 * recording the value forced for each variable of `general`. */
static bool direct_pattern_subsumes_v1(
    const Atom *general, const Atom *specific,
    DirectVariablePairsV1 *bindings, size_t depth,
    char *error, size_t error_size) {
    if (general == NULL || specific == NULL ||
        depth > GSLT_PETTA_DIRECT_DEPTH_LIMIT_V1)
        return false;
    if (direct_is_source_variable_v1(general))
        return direct_guard_binding_push_v1(
            bindings, general, specific, error, error_size);
    if (direct_is_source_variable_v1(specific) ||
        general->kind != specific->kind)
        return false;
    if (general->kind == ATOM_SYMBOL)
        return strcmp(atom_name_cstr((Atom *)general),
                      atom_name_cstr((Atom *)specific)) == 0;
    if (general->kind == ATOM_GROUNDED)
        return atom_eq((Atom *)general, (Atom *)specific);
    if (general->kind != ATOM_EXPR ||
        general->expr.len != specific->expr.len)
        return false;
    for (CettaExprIndex index = 0u; index < general->expr.len; index++) {
        if (!direct_pattern_subsumes_v1(
                general->expr.elems[index], specific->expr.elems[index],
                bindings, depth + 1u, error, error_size))
            return false;
    }
    return true;
}

static const Atom *direct_guard_binding_value_v1(
    const DirectVariablePairsV1 *bindings, const Atom *term) {
    if (!direct_is_source_variable_v1(term))
        return term;
    for (size_t index = 0u; index < bindings->len; index++) {
        if (direct_same_variable_v1(bindings->items[index].left, term))
            return bindings->items[index].right;
    }
    return term;
}

static bool direct_guard_terms_equal_after_binding_v1(
    const Atom *left, const Atom *right,
    const DirectVariablePairsV1 *bindings, size_t depth) {
    left = direct_guard_binding_value_v1(bindings, left);
    right = direct_guard_binding_value_v1(bindings, right);
    if (left == NULL || right == NULL ||
        depth > GSLT_PETTA_DIRECT_DEPTH_LIMIT_V1 ||
        left->kind != right->kind)
        return false;
    if (left->kind == ATOM_VAR)
        return direct_same_variable_v1(left, right);
    if (left->kind == ATOM_SYMBOL)
        return strcmp(atom_name_cstr((Atom *)left),
                      atom_name_cstr((Atom *)right)) == 0;
    if (left->kind == ATOM_GROUNDED)
        return atom_eq((Atom *)left, (Atom *)right);
    if (left->kind != ATOM_EXPR || left->expr.len != right->expr.len)
        return false;
    for (CettaExprIndex index = 0u; index < left->expr.len; index++) {
        if (!direct_guard_terms_equal_after_binding_v1(
                left->expr.elems[index], right->expr.elems[index],
                bindings, depth + 1u))
            return false;
    }
    return true;
}

/* A general rule guarded by `different x K` is disjoint from a rule whose
 * input pattern forces x = K.  This is the constructor-pattern analogue of
 * the nonlinear-variable complement proved above. */
static bool direct_rules_have_pattern_guard_complement_v1(
    const DirectRuleV1 *specific_rule,
    const DirectRuleV1 *guarded_rule,
    const DirectBindingModeV1 *parent_mode,
    const DirectRulesV1 *target_equations,
    const DirectOperatorsV1 *capabilities,
    char *error, size_t error_size) {
    DirectVariablePairsV1 bindings = {0};
    bool exclusive = false;

    for (size_t argument = 0u; argument < parent_mode->arity; argument++) {
        if (!parent_mode->input_ground[argument])
            continue;
        if (!direct_pattern_subsumes_v1(
                guarded_rule->head->expr.elems[argument + 1u],
                specific_rule->head->expr.elems[argument + 1u],
                &bindings, 0u, error, error_size))
            goto done;
    }
    for (CettaExprIndex body_index = 1u;
         body_index < guarded_rule->body->expr.len; body_index++) {
        const Atom *goal = guarded_rule->body->expr.elems[body_index];
        const char *relation = atom_name_cstr(goal->expr.elems[0]);
        size_t arity = (size_t)goal->expr.len - 1u;
        DirectGroundDisequalityCapabilityV1 guard;
        if (!direct_has_operator_v1(capabilities, relation, arity) ||
            !direct_target_ground_disequality_capability_v1(
                target_equations, relation, arity, &guard))
            continue;
        if (direct_guard_terms_equal_after_binding_v1(
                goal->expr.elems[guard.unequal_left_argument + 1u],
                goal->expr.elems[guard.unequal_right_argument + 1u],
                &bindings, 0u)) {
            exclusive = true;
            break;
        }
    }

done:
    free(bindings.items);
    return exclusive;
}

static bool direct_rules_have_exclusive_functional_guards_v1(
    const DirectRuleV1 *left, const DirectRuleV1 *right,
    const DirectBindingModeV1 *parent_mode,
    const DirectBindingModesV1 *modes,
    const DirectVariablePairsV1 *pairs,
    char *error, size_t error_size) {
    DirectVariablesV1 left_ground = {0};
    DirectVariablesV1 right_ground = {0};
    bool exclusive = false;

    for (size_t argument = 0u;
         argument < parent_mode->arity; argument++) {
        if (parent_mode->input_ground[argument] &&
            (!direct_variables_add_term_v1(
                 &left_ground, left->head->expr.elems[argument + 1u], 0u,
                 error, error_size) ||
             !direct_variables_add_term_v1(
                 &right_ground, right->head->expr.elems[argument + 1u], 0u,
                 error, error_size)))
            goto done;
    }
    for (CettaExprIndex left_index = 1u;
         left_index < left->body->expr.len && !exclusive; left_index++) {
        Atom *left_goal = left->body->expr.elems[left_index];
        const char *relation = atom_name_cstr(left_goal->expr.elems[0]);
        size_t arity = (size_t)left_goal->expr.len - 1u;
        for (CettaExprIndex right_index = 1u;
             right_index < right->body->expr.len; right_index++) {
            Atom *right_goal = right->body->expr.elems[right_index];
            uint8_t *input_ground;
            bool disjoint_result = false;
            const DirectBindingModeV1 *guard_mode;
            if ((size_t)right_goal->expr.len - 1u != arity ||
                strcmp(atom_name_cstr(right_goal->expr.elems[0]),
                       relation) != 0)
                continue;
            input_ground = (uint8_t *)calloc(arity, 1u);
            if (input_ground == NULL) {
                (void)direct_error_v1(
                    error, error_size,
                    "out of memory deriving functional guard exclusivity");
                goto done;
            }
            for (size_t argument = 0u; argument < arity; argument++) {
                Atom *left_term = left_goal->expr.elems[argument + 1u];
                Atom *right_term = right_goal->expr.elems[argument + 1u];
                bool correspond = direct_terms_correspond_v1(
                    left_term, right_term, pairs, 0u);
                bool ground = direct_term_is_ground_v1(
                                  &left_ground, left_term, 0u) &&
                              direct_term_is_ground_v1(
                                  &right_ground, right_term, 0u);
                input_ground[argument] = correspond && ground;
                if (!input_ground[argument] &&
                    !direct_patterns_may_unify_v1(
                        left_term, right_term, 0u))
                    disjoint_result = true;
            }
            guard_mode = direct_find_binding_mode_v1(
                modes, relation, arity, input_ground);
            free(input_ground);
            if (disjoint_result && guard_mode != NULL &&
                guard_mode->functional) {
                exclusive = true;
                break;
            }
        }
    }

done:
    free(left_ground.items);
    free(right_ground.items);
    return exclusive;
}

static bool direct_rules_have_exclusive_functional_guard_sequence_v1(
    const DirectRuleV1 *left, const DirectRuleV1 *right,
    const DirectBindingModeV1 *parent_mode,
    const DirectBindingModesV1 *modes,
    DirectVariablePairsV1 *pairs,
    char *error, size_t error_size) {
    DirectVariablesV1 left_ground = {0};
    DirectVariablesV1 right_ground = {0};
    bool exclusive = false;
    const char *trace_relation = getenv("CETTA_GSLT_DIRECT_MODE_TRACE");
    bool trace = trace_relation != NULL &&
        strcmp(trace_relation, parent_mode->relation) == 0;

    for (size_t argument = 0u;
         argument < parent_mode->arity; argument++) {
        if (parent_mode->input_ground[argument] &&
            (!direct_variables_add_term_v1(
                 &left_ground, left->head->expr.elems[argument + 1u], 0u,
                 error, error_size) ||
             !direct_variables_add_term_v1(
                 &right_ground, right->head->expr.elems[argument + 1u], 0u,
                 error, error_size)))
            goto done;
    }
    CettaExprIndex left_index = 1u;
    CettaExprIndex right_index = 1u;
    while (left_index < left->body->expr.len &&
           right_index < right->body->expr.len) {
        Atom *left_goal = left->body->expr.elems[left_index++];
        Atom *right_goal = right->body->expr.elems[right_index++];
        const char *relation = atom_name_cstr(left_goal->expr.elems[0]);
        size_t arity = (size_t)left_goal->expr.len - 1u;
        uint8_t *input_ground;
        const DirectBindingModeV1 *guard_mode;
        bool disjoint_result = false;
        if ((size_t)right_goal->expr.len - 1u != arity ||
            strcmp(atom_name_cstr(right_goal->expr.elems[0]),
                   relation) != 0)
            goto done;
        input_ground = (uint8_t *)calloc(arity, 1u);
        if (input_ground == NULL) {
            (void)direct_error_v1(
                error, error_size,
                "out of memory deriving functional guard sequence");
            goto done;
        }
        for (size_t argument = 0u; argument < arity; argument++) {
            Atom *left_term = left_goal->expr.elems[argument + 1u];
            Atom *right_term = right_goal->expr.elems[argument + 1u];
            bool correspond = direct_terms_correspond_v1(
                left_term, right_term, pairs, 0u);
            bool ground = direct_term_is_ground_v1(
                              &left_ground, left_term, 0u) &&
                          direct_term_is_ground_v1(
                              &right_ground, right_term, 0u);
            input_ground[argument] = correspond && ground;
            if (!correspond && !direct_patterns_may_unify_v1(
                    left_term, right_term, 0u))
                disjoint_result = true;
        }
        guard_mode = direct_find_binding_mode_v1(
            modes, relation, arity, input_ground);
        if (trace) {
            fprintf(stderr, "[gslt-direct-mode] %s/%zu mode=",
                    parent_mode->relation, parent_mode->arity);
            for (size_t argument = 0u;
                 argument < parent_mode->arity; argument++)
                fprintf(stderr, "%u",
                        (unsigned)parent_mode->input_ground[argument]);
            fprintf(stderr, " guard=%s input=", relation);
            for (size_t argument = 0u; argument < arity; argument++)
                fprintf(stderr, "%u", (unsigned)input_ground[argument]);
            fprintf(stderr, " functional=%u disjoint=%u\n",
                    guard_mode != NULL && guard_mode->functional ? 1u : 0u,
                    disjoint_result ? 1u : 0u);
        }
        free(input_ground);
        if (guard_mode == NULL || !guard_mode->functional)
            goto done;
        if (disjoint_result) {
            exclusive = true;
            goto done;
        }
        for (size_t argument = 0u; argument < arity; argument++) {
            if (!guard_mode->success_ground[argument])
                continue;
            Atom *left_term = left_goal->expr.elems[argument + 1u];
            Atom *right_term = right_goal->expr.elems[argument + 1u];
            if (!direct_align_input_patterns_v1(
                    left_term, right_term, pairs, 0u,
                    error, error_size) ||
                !direct_variables_add_term_v1(
                    &left_ground, left_term, 0u,
                    error, error_size) ||
                !direct_variables_add_term_v1(
                    &right_ground, right_term, 0u,
                    error, error_size)) {
                if (trace)
                    fprintf(stderr,
                            "[gslt-direct-mode] %s/%zu guard alignment refused\n",
                            parent_mode->relation, parent_mode->arity);
                goto done;
            }
        }
    }

done:
    free(left_ground.items);
    free(right_ground.items);
    return exclusive;
}

static bool direct_rule_inputs_may_overlap_v1(
    const DirectRuleV1 *left, const DirectRuleV1 *right,
    const uint8_t *input_ground, size_t arity) {
    for (size_t argument = 0u; argument < arity; argument++) {
        if (input_ground[argument] &&
            !direct_patterns_may_unify_v1(
                left->head->expr.elems[argument + 1u],
                right->head->expr.elems[argument + 1u], 0u))
            return false;
    }
    return true;
}

static bool direct_mode_input_tuple_is_ground_v1(
    const DirectRuleV1 *rule, const DirectBindingModeV1 *mode) {
    DirectVariablesV1 no_variables = {0};
    for (size_t argument = 0u; argument < mode->arity; argument++) {
        if (mode->input_ground[argument] &&
            !direct_term_is_ground_v1(
                &no_variables, rule->head->expr.elems[argument + 1u], 0u))
            return false;
    }
    return true;
}

static uint32_t direct_mode_input_tuple_hash_v1(
    const DirectRuleV1 *rule, const DirectBindingModeV1 *mode) {
    uint32_t hash = 2166136261u;
    for (size_t argument = 0u; argument < mode->arity; argument++) {
        if (!mode->input_ground[argument])
            continue;
        hash ^= atom_hash(rule->head->expr.elems[argument + 1u]);
        hash *= 16777619u;
        hash ^= (uint32_t)argument;
        hash *= 16777619u;
    }
    return hash;
}

static bool direct_mode_input_tuples_equal_v1(
    const DirectRuleV1 *left, const DirectRuleV1 *right,
    const DirectBindingModeV1 *mode) {
    for (size_t argument = 0u; argument < mode->arity; argument++) {
        if (mode->input_ground[argument] &&
            !atom_eq(left->head->expr.elems[argument + 1u],
                     right->head->expr.elems[argument + 1u]))
            return false;
    }
    return true;
}

static bool direct_ground_mode_heads_are_disjoint_v1(
    const DirectRulesV1 *rules, const DirectRuleIndexV1 *rule_index,
    const DirectBindingModeV1 *mode, size_t begin, size_t end,
    char *error, size_t error_size) {
    size_t rule_count = end - begin;
    size_t capacity = 8u;
    DirectGroundHeadSlotV1 *slots;

    if (rule_count <= 1u)
        return true;
    if (rule_count > SIZE_MAX / 2u)
        return direct_error_v1(
            error, error_size,
            "direct PeTTa ground-head index is too large");
    while (capacity < rule_count * 2u) {
        if (capacity > SIZE_MAX / 2u)
            return direct_error_v1(
                error, error_size,
                "direct PeTTa ground-head index is too large");
        capacity *= 2u;
    }
    if (capacity > SIZE_MAX / sizeof(*slots))
        return direct_error_v1(
            error, error_size,
            "direct PeTTa ground-head index is too large");
    slots = (DirectGroundHeadSlotV1 *)calloc(capacity, sizeof(*slots));
    if (slots == NULL)
        return direct_error_v1(
            error, error_size,
            "out of memory indexing direct PeTTa ground heads");

    for (size_t offset = begin; offset < end; offset++) {
        size_t source_index = rule_index->items[offset].source_index;
        const DirectRuleV1 *rule = &rules->items[source_index];
        uint32_t hash = direct_mode_input_tuple_hash_v1(rule, mode);
        size_t slot_index = (size_t)hash & (capacity - 1u);
        while (slots[slot_index].occupied) {
            if (slots[slot_index].hash == hash &&
                direct_mode_input_tuples_equal_v1(
                    rule, &rules->items[slots[slot_index].source_index],
                    mode)) {
                free(slots);
                return false;
            }
            slot_index = (slot_index + 1u) & (capacity - 1u);
        }
        slots[slot_index] = (DirectGroundHeadSlotV1){
            .hash = hash,
            .source_index = source_index,
            .occupied = 1u,
        };
    }
    free(slots);
    return true;
}

static bool direct_mode_heads_are_disjoint_v1(
    const DirectRulesV1 *rules, const DirectRuleIndexV1 *rule_index,
    const DirectBindingModeV1 *mode, char *error, size_t error_size) {
    size_t begin;
    size_t end;
    direct_rule_index_range_v1(
        rule_index, mode->relation, mode->arity, &begin, &end);
    if (end - begin > 1u) {
        bool has_input = false;
        bool all_inputs_ground = true;
        for (size_t argument = 0u; argument < mode->arity; argument++) {
            if (mode->input_ground[argument]) {
                has_input = true;
                break;
            }
        }
        if (!has_input)
            return false;
        for (size_t offset = begin; offset < end; offset++) {
            const DirectRuleV1 *rule = &rules->items[
                rule_index->items[offset].source_index];
            if (!direct_mode_input_tuple_is_ground_v1(rule, mode)) {
                all_inputs_ground = false;
                break;
            }
        }
        if (all_inputs_ground)
            return direct_ground_mode_heads_are_disjoint_v1(
                rules, rule_index, mode, begin, end, error, error_size);
    }
    for (size_t left_offset = begin; left_offset < end; left_offset++) {
        const DirectRuleV1 *left = &rules->items[
            rule_index->items[left_offset].source_index];
        for (size_t right_offset = left_offset + 1u;
             right_offset < end; right_offset++) {
            const DirectRuleV1 *right = &rules->items[
                rule_index->items[right_offset].source_index];
            if (direct_rule_inputs_may_overlap_v1(
                    left, right, mode->input_ground, mode->arity))
                return false;
        }
    }
    return true;
}

static bool direct_relation_input_excludes_pattern_v1(
    const DirectRulesV1 *rules, const DirectRuleIndexV1 *rule_index,
    const char *relation, size_t arity, size_t input_argument,
    const Atom *excluded_pattern) {
    size_t begin;
    size_t end;
    direct_rule_index_range_v1(
        rule_index, relation, arity, &begin, &end);
    for (size_t offset = begin; offset < end; offset++) {
        const DirectRuleV1 *rule = &rules->items[
            rule_index->items[offset].source_index];
        if (direct_patterns_may_unify_v1(
                rule->head->expr.elems[input_argument + 1u],
                excluded_pattern, 0u))
            return false;
    }
    return begin != end;
}

static bool direct_rules_have_functional_domain_complement_v1(
    const DirectRuleV1 *guarded_rule,
    const DirectRuleV1 *competing_rule,
    const DirectBindingModeV1 *parent_mode,
    const DirectRulesV1 *rules, const DirectRuleIndexV1 *rule_index,
    const DirectBindingModesV1 *modes,
    char *error, size_t error_size) {
    DirectVariablesV1 known = {0};
    bool exclusive = false;

    for (size_t argument = 0u; argument < parent_mode->arity; argument++) {
        if (parent_mode->input_ground[argument] &&
            !direct_variables_add_term_v1(
                &known, guarded_rule->head->expr.elems[argument + 1u],
                0u, error, error_size))
            goto done;
    }
    for (CettaExprIndex body_index = 1u;
         body_index < guarded_rule->body->expr.len && !exclusive;
         body_index++) {
        const Atom *goal = guarded_rule->body->expr.elems[body_index];
        const char *relation = atom_name_cstr(goal->expr.elems[0]);
        size_t arity = (size_t)goal->expr.len - 1u;
        uint8_t *input_ground = (uint8_t *)malloc(arity);
        const DirectBindingModeV1 *callee_mode;
        if (input_ground == NULL) {
            (void)direct_error_v1(
                error, error_size,
                "out of memory deriving functional domain exclusivity");
            goto done;
        }
        for (size_t argument = 0u; argument < arity; argument++) {
            input_ground[argument] = direct_term_is_ground_v1(
                &known, goal->expr.elems[argument + 1u], 0u);
        }
        callee_mode = direct_find_binding_mode_v1(
            modes, relation, arity, input_ground);
        if (callee_mode != NULL && callee_mode->functional) {
            for (size_t callee_argument = 0u;
                 callee_argument < arity && !exclusive;
                 callee_argument++) {
                if (!input_ground[callee_argument])
                    continue;
                for (size_t parent_argument = 0u;
                     parent_argument < parent_mode->arity;
                     parent_argument++) {
                    if (!parent_mode->input_ground[parent_argument] ||
                        !direct_terms_equal_v1(
                            goal->expr.elems[callee_argument + 1u],
                            guarded_rule->head->expr.elems[
                                parent_argument + 1u],
                            0u))
                        continue;
                    if (direct_relation_input_excludes_pattern_v1(
                            rules, rule_index, relation, arity,
                            callee_argument,
                            competing_rule->head->expr.elems[
                                parent_argument + 1u])) {
                        exclusive = true;
                        break;
                    }
                }
            }
        }
        if (callee_mode != NULL) {
            for (size_t argument = 0u; argument < arity; argument++) {
                if (callee_mode->success_ground[argument] &&
                    !direct_variables_add_term_v1(
                        &known, goal->expr.elems[argument + 1u], 0u,
                        error, error_size)) {
                    free(input_ground);
                    goto done;
                }
            }
        }
        free(input_ground);
    }

done:
    free(known.items);
    return exclusive;
}

static bool direct_mode_rules_are_guard_exclusive_v1(
    const DirectRulesV1 *rules, const DirectRuleIndexV1 *rule_index,
    const DirectBindingModeV1 *mode,
    const DirectBindingModesV1 *modes,
    const DirectRulesV1 *target_equations,
    const DirectOperatorsV1 *capabilities,
    const DirectIntegerNativeTypesV1 *native_types,
    char *error, size_t error_size) {
    size_t begin;
    size_t end;
    direct_rule_index_range_v1(
        rule_index, mode->relation, mode->arity, &begin, &end);
    for (size_t left_offset = begin; left_offset < end; left_offset++) {
        const DirectRuleV1 *left = &rules->items[
            rule_index->items[left_offset].source_index];
        for (size_t right_offset = left_offset + 1u;
             right_offset < end; right_offset++) {
            const DirectRuleV1 *right = &rules->items[
                rule_index->items[right_offset].source_index];
            DirectVariablePairsV1 pairs = {0};
            bool aligned = true;
            bool exclusive;
            if (!direct_rule_inputs_may_overlap_v1(
                    left, right, mode->input_ground, mode->arity))
                continue;
            for (size_t argument = 0u; argument < mode->arity; argument++) {
                if (mode->input_ground[argument] &&
                    !direct_align_overlapping_input_patterns_v1(
                        left->head->expr.elems[argument + 1u],
                        right->head->expr.elems[argument + 1u],
                        &pairs, 0u, error, error_size)) {
                    aligned = false;
                    break;
                }
            }
            exclusive =
                direct_integer_guards_exclusive_v1(left, right, native_types, &pairs, aligned) ||
                direct_rules_have_nonlinear_guard_complement_v1(
                    left, right, mode, target_equations, capabilities,
                    error, error_size) ||
                direct_rules_have_nonlinear_guard_complement_v1(
                    right, left, mode, target_equations, capabilities,
                    error, error_size) ||
                direct_rules_have_pattern_guard_complement_v1(
                    left, right, mode, target_equations, capabilities,
                    error, error_size) ||
                direct_rules_have_pattern_guard_complement_v1(
                    right, left, mode, target_equations, capabilities,
                    error, error_size) ||
                direct_rules_have_functional_domain_complement_v1(
                    left, right, mode, rules, rule_index, modes,
                    error, error_size) ||
                direct_rules_have_functional_domain_complement_v1(
                    right, left, mode, rules, rule_index, modes,
                    error, error_size);
            if (!exclusive) {
                exclusive = aligned &&
                    (direct_rules_have_exclusive_functional_guards_v1(
                         left, right, mode, modes, &pairs,
                         error, error_size) ||
                     direct_rules_have_exclusive_functional_guard_sequence_v1(
                         left, right, mode, modes, &pairs,
                         error, error_size));
            }
            free(pairs.items);
            if (error != NULL && error[0] != '\0')
                return false;
            if (!exclusive)
                return false;
        }
    }
    return true;
}

static bool direct_mode_success_is_ground_v1(
    const DirectBindingModeV1 *mode) {
    for (size_t argument = 0u; argument < mode->arity; argument++) {
        if (!mode->success_ground[argument])
            return false;
    }
    return true;
}

static bool direct_mode_success_has_ground_v1(
    const DirectBindingModeV1 *mode) {
    for (size_t argument = 0u; argument < mode->arity; argument++) {
        if (mode->success_ground[argument])
            return true;
    }
    return false;
}

static bool direct_rule_has_functional_body_v1(
    const DirectRuleV1 *rule, const DirectBindingModeV1 *mode,
    const DirectOperatorsV1 *relations,
    const DirectOperatorsV1 *capabilities,
    const DirectRulesV1 *target_equations,
    const DirectBindingModesV1 *modes,
    const DirectIntegerNativeTypesV1 *native_types,
    char *error, size_t error_size) {
    DirectVariablesV1 known = {0};
    bool functional = true;
    const char *trace_relation = getenv("CETTA_GSLT_DIRECT_MODE_TRACE");
    bool trace = trace_relation != NULL &&
        strcmp(trace_relation, mode->relation) == 0;

    for (size_t argument = 0u; argument < mode->arity; argument++) {
        if (mode->input_ground[argument] &&
            !direct_variables_add_term_v1(
                &known, rule->head->expr.elems[argument + 1u], 0u,
                error, error_size)) {
            functional = false;
            goto done;
        }
    }
    for (CettaExprIndex body_index = 1u;
         body_index < rule->body->expr.len; body_index++) {
        Atom *goal = rule->body->expr.elems[body_index];
        const char *relation = atom_name_cstr(goal->expr.elems[0]);
        size_t arity = (size_t)goal->expr.len - 1u;
        uint8_t *input_ground = (uint8_t *)malloc(arity);
        const DirectBindingModeV1 *callee_mode;
        if (input_ground == NULL) {
            (void)direct_error_v1(
                error, error_size,
                "out of memory deriving direct PeTTa functional modes");
            functional = false;
            goto done;
        }
        for (size_t argument = 0u; argument < arity; argument++) {
            input_ground[argument] = direct_term_is_ground_v1(
                &known, goal->expr.elems[argument + 1u], 0u);
        }
        if (!direct_has_operator_v1(relations, relation, arity)) {
            DirectOperatorV1 capability = {
                .name = relation,
                .arity = arity,
            };
            bool declared = direct_has_operator_v1(
                capabilities, relation, arity);
            bool absent = declared &&
                !direct_target_equations_define_capability_v1(
                    target_equations, &capability);
            bool semidet = declared &&
                direct_capability_call_is_ground_semidet_v1(
                    target_equations, capabilities, goal, input_ground,
                    native_types, rule);
            if (trace) {
                fprintf(stderr, "[gslt-direct-mode] %s/%zu mode=",
                        mode->relation, mode->arity);
                for (size_t argument = 0u;
                     argument < mode->arity; argument++)
                    fprintf(stderr, "%u",
                            (unsigned)mode->input_ground[argument]);
                fprintf(stderr, " body=%s input=", relation);
                for (size_t argument = 0u; argument < arity; argument++)
                    fprintf(stderr, "%u", (unsigned)input_ground[argument]);
                fprintf(stderr,
                        " capability-semidet=%u absent=%u\n",
                        semidet ? 1u : 0u, absent ? 1u : 0u);
            }
            free(input_ground);
            /* An unavailable optional capability is rendered as an empty
             * relation.  Once such a goal is reached, the remainder of this
             * rule has no successful execution and therefore cannot add a
             * second result.  Earlier goals still have to be functional: the
             * loop checks them before reaching this point. */
            if (absent)
                goto done;
            if (!semidet) {
                functional = false;
                goto done;
            }
            continue;
        }
        callee_mode = direct_find_binding_mode_v1(
            modes, relation, arity, input_ground);
        if (trace) {
            fprintf(stderr, "[gslt-direct-mode] %s/%zu mode=",
                    mode->relation, mode->arity);
            for (size_t argument = 0u;
                 argument < mode->arity; argument++)
                fprintf(stderr, "%u",
                        (unsigned)mode->input_ground[argument]);
            fprintf(stderr, " body=%s input=", relation);
            for (size_t argument = 0u; argument < arity; argument++)
                fprintf(stderr, "%u", (unsigned)input_ground[argument]);
            fprintf(stderr, " functional=%u self=%u\n",
                    callee_mode != NULL && callee_mode->functional ? 1u : 0u,
                    callee_mode == mode ? 1u : 0u);
        }
        free(input_ground);
        if (callee_mode == NULL ||
            (!callee_mode->functional && callee_mode != mode)) {
            functional = false;
            goto done;
        }
        for (size_t argument = 0u; argument < arity; argument++) {
            if (callee_mode->success_ground[argument] &&
                !direct_variables_add_term_v1(
                    &known, goal->expr.elems[argument + 1u], 0u,
                    error, error_size)) {
                functional = false;
                goto done;
            }
        }
    }

done:
    free(known.items);
    return functional;
}

static bool direct_analyze_functional_modes_v1(
    const DirectRulesV1 *rules, const DirectRuleIndexV1 *rule_index,
    const DirectOperatorsV1 *relations,
    const DirectOperatorsV1 *capabilities,
    const DirectRulesV1 *target_equations,
    DirectBindingModesV1 *modes,
    const DirectIntegerNativeTypesV1 *native_types,
    char *error, size_t error_size) {
    const char *trace_relation = getenv("CETTA_GSLT_DIRECT_MODE_TRACE");
    size_t functional_count = 0u;
    bool advanced;
    do {
        size_t functional_before = functional_count;
        uint8_t *candidates = (uint8_t *)calloc(modes->len, 1u);
        bool changed;

        if (candidates == NULL)
            return direct_error_v1(
                error, error_size,
                "out of memory deriving direct PeTTa functional modes");
        for (size_t mode_index = 0u;
             mode_index < modes->len; mode_index++) {
            DirectBindingModeV1 *mode = &modes->items[mode_index];
            if (mode->functional || !direct_mode_success_is_ground_v1(mode))
                continue;
            if (trace_relation != NULL &&
                (strcmp(trace_relation, "*") == 0 ||
                 strcmp(trace_relation, mode->relation) == 0)) {
                size_t trace_begin;
                size_t trace_end;
                direct_rule_index_range_v1(
                    rule_index, mode->relation, mode->arity,
                    &trace_begin, &trace_end);
                fprintf(stderr, "[gslt-direct-mode] analyze %s/%zu input=",
                        mode->relation, mode->arity);
                for (size_t argument = 0u;
                     argument < mode->arity; argument++)
                    fprintf(stderr, "%u",
                            (unsigned)mode->input_ground[argument]);
                fprintf(stderr, " rules=%zu\n", trace_end - trace_begin);
            }
            mode->input_heads_disjoint = (uint8_t)
                direct_mode_heads_are_disjoint_v1(
                    rules, rule_index, mode, error, error_size);
            if (error != NULL && error[0] != '\0') {
                free(candidates);
                return false;
            }
            candidates[mode_index] = (uint8_t)(
                mode->input_heads_disjoint ||
                direct_mode_rules_are_guard_exclusive_v1(
                    rules, rule_index, mode, modes, target_equations,
                    capabilities, native_types, error, error_size));
            if (error != NULL && error[0] != '\0') {
                free(candidates);
                return false;
            }
        }
        for (size_t mode_index = 0u;
             mode_index < modes->len; mode_index++) {
            if (candidates[mode_index])
                modes->items[mode_index].functional = 1u;
        }
        free(candidates);

        do {
            changed = false;
            for (size_t mode_index = 0u;
                 mode_index < modes->len; mode_index++) {
                DirectBindingModeV1 *mode = &modes->items[mode_index];
                size_t begin;
                size_t end;
                if (!mode->functional)
                    continue;
                direct_rule_index_range_v1(
                    rule_index, mode->relation, mode->arity,
                    &begin, &end);
                for (size_t offset = begin; offset < end; offset++) {
                    const DirectRuleV1 *rule = &rules->items[
                        rule_index->items[offset].source_index];
                    if (!direct_rule_has_functional_body_v1(
                            rule, mode, relations, capabilities,
                            target_equations, modes, native_types,
                            error, error_size)) {
                        if (error != NULL && error[0] != '\0')
                            return false;
                        mode->functional = 0u;
                        changed = true;
                        break;
                    }
                }
            }
        } while (changed);

        functional_count = 0u;
        for (size_t mode_index = 0u;
             mode_index < modes->len; mode_index++) {
            if (modes->items[mode_index].functional)
                functional_count++;
        }
        advanced = functional_count > functional_before;
    } while (advanced);
    return true;
}

static DirectBindingModeV1 *direct_find_binding_mode_mutable_v1(
    DirectBindingModesV1 *modes, const char *relation, size_t arity,
    const uint8_t *input_ground) {
    return (DirectBindingModeV1 *)direct_find_binding_mode_v1(
        modes, relation, arity, input_ground);
}

static bool direct_select_rule_mode_calls_v1(
    const DirectRuleV1 *rule, const uint8_t *head_input_ground,
    size_t head_arity, const DirectOperatorsV1 *relations,
    DirectBindingModesV1 *modes, bool select_all_internal, bool *changed,
    char *error, size_t error_size) {
    DirectVariablesV1 known = {0};
    bool ok = true;

    for (size_t argument = 0u; argument < head_arity; argument++) {
        if (head_input_ground != NULL && head_input_ground[argument] &&
            !direct_variables_add_term_v1(
                &known, rule->head->expr.elems[argument + 1u], 0u,
                error, error_size)) {
            ok = false;
            goto done;
        }
    }
    for (CettaExprIndex body_index = 1u;
         body_index < rule->body->expr.len; body_index++) {
        Atom *goal = rule->body->expr.elems[body_index];
        const char *relation = atom_name_cstr(goal->expr.elems[0]);
        size_t arity = (size_t)goal->expr.len - 1u;
        uint8_t *input_ground = (uint8_t *)malloc(arity);
        DirectBindingModeV1 *callee_mode;
        bool has_ground_input = false;
        if (input_ground == NULL) {
            (void)direct_error_v1(
                error, error_size,
                "out of memory selecting direct PeTTa modes");
            ok = false;
            goto done;
        }
        for (size_t argument = 0u; argument < arity; argument++) {
            input_ground[argument] = direct_term_is_ground_v1(
                &known, goal->expr.elems[argument + 1u], 0u);
            has_ground_input = has_ground_input || input_ground[argument];
        }
        callee_mode = direct_find_binding_mode_mutable_v1(
            modes, relation, arity, input_ground);
        free(input_ground);
        if (callee_mode == NULL) {
            if (direct_has_operator_v1(relations, relation, arity)) {
                (void)direct_error_v1(
                    error, error_size,
                    "mode selection lacks binding mode %s/%zu",
                    relation, arity);
                ok = false;
                goto done;
            }
            continue;
        }
        if ((select_all_internal || has_ground_input ||
             direct_mode_success_has_ground_v1(callee_mode)) &&
            !callee_mode->selected) {
            callee_mode->selected = 1u;
            *changed = true;
        }
        for (size_t argument = 0u; argument < arity; argument++) {
            if (callee_mode->success_ground[argument] &&
                !direct_variables_add_term_v1(
                    &known, goal->expr.elems[argument + 1u], 0u,
                    error, error_size)) {
                ok = false;
                goto done;
            }
        }
    }

done:
    free(known.items);
    return ok;
}

static bool direct_select_reachable_modes_v1(
    const DirectRulesV1 *rules, const DirectOperatorsV1 *relations,
    DirectBindingModesV1 *modes, bool entry_rooted,
    char *error, size_t error_size) {
    bool changed = false;
    if (!entry_rooted) {
        for (size_t rule_index = 0u;
             rule_index < rules->len; rule_index++) {
            size_t arity =
                (size_t)rules->items[rule_index].head->expr.len - 1u;
            if (!direct_select_rule_mode_calls_v1(
                    &rules->items[rule_index], NULL, arity, relations,
                    modes, false, &changed, error, error_size))
                return false;
        }
    }
    do {
        changed = false;
        for (size_t mode_index = 0u; mode_index < modes->len; mode_index++) {
            DirectBindingModeV1 *mode = &modes->items[mode_index];
            if (!mode->selected)
                continue;
            for (size_t rule_index = 0u;
                 rule_index < rules->len; rule_index++) {
                const DirectRuleV1 *rule = &rules->items[rule_index];
                const char *relation =
                    atom_name_cstr(rule->head->expr.elems[0]);
                size_t arity = (size_t)rule->head->expr.len - 1u;
                if (arity == mode->arity &&
                    strcmp(relation, mode->relation) == 0 &&
                    !direct_select_rule_mode_calls_v1(
                        rule, mode->input_ground, mode->arity,
                        relations, modes, entry_rooted, &changed,
                        error, error_size))
                    return false;
            }
        }
    } while (changed);
    return true;
}

static bool direct_render_binding_modes_v1(
    DirectBufferV1 *program, const DirectBindingModesV1 *modes,
    bool selected_only) {
    if (!direct_literal_v1(
            program,
            "; derived binding groundness (1 = ground at call/success)\n"))
        return false;
    for (size_t mode_index = 0u; mode_index < modes->len; mode_index++) {
        const DirectBindingModeV1 *mode = &modes->items[mode_index];
        if (selected_only && !mode->selected)
            continue;
        char arity[32];
        int written = snprintf(arity, sizeof(arity), "%zu", mode->arity);
        if (written < 0 || (size_t)written >= sizeof(arity) ||
            !direct_literal_v1(program, "; gslt-binding-mode ") ||
            !direct_literal_v1(program, mode->relation) ||
            !direct_byte_v1(program, (uint8_t)'/') ||
            !direct_literal_v1(program, arity) ||
            !direct_literal_v1(program, " input="))
            return false;
        for (size_t argument = 0u; argument < mode->arity; argument++) {
            if (!direct_byte_v1(
                    program, mode->input_ground[argument]
                                 ? (uint8_t)'1' : (uint8_t)'0'))
                return false;
        }
        if (!direct_literal_v1(program, " success="))
            return false;
        for (size_t argument = 0u; argument < mode->arity; argument++) {
            if (!direct_byte_v1(
                    program, mode->success_ground[argument]
                                 ? (uint8_t)'1' : (uint8_t)'0'))
                return false;
        }
        if (!direct_literal_v1(
                program, mode->functional
                             ? (mode->selected
                                    ? " functional=semidet specialized=yes\n"
                                    : " functional=semidet specialized=no\n")
                             : (mode->selected
                                    ? " functional=relational specialized=yes\n"
                                    : " functional=relational specialized=no\n")))
            return false;
    }
    return direct_byte_v1(program, (uint8_t)'\n');
}

static const char *direct_source_rewrite_kind_v1(
    const CettaGsltRewriteV1 *rewrite) {
    if (direct_head_v1(rewrite->head, "metta-equation", 2u))
        return "target-equation";
    if (direct_head_v1(
            rewrite->head, "oslf-external-relation-decl-v1", 2u) &&
        rewrite->body->expr.len == 1u)
        return "capability-declaration";
    return "relation";
}

static bool direct_render_source_inventory_v1(
    DirectBufferV1 *program,
    const CettaGsltCompositionV1 *composition) {
    if (!direct_literal_v1(program, "; authored GSLT source inventory\n"))
        return false;
    for (size_t index = 0u; index < composition->operator_count; index++) {
        const CettaGsltOperatorV1 *operator =
            &composition->operators[index];
        if (!direct_literal_v1(program, "; gslt-source-operator-v1 index=") ||
            !direct_size_v1(program, index) ||
            !direct_literal_v1(program, " presentation=") ||
            !direct_literal_v1(program, operator->presentation_name) ||
            !direct_literal_v1(program, " name=") ||
            !direct_literal_v1(program, operator->name) ||
            !direct_literal_v1(program, " arity=") ||
            !direct_size_v1(program, operator->arity) ||
            !direct_byte_v1(program, (uint8_t)'\n'))
            return false;
    }
    for (size_t index = 0u; index < composition->rewrite_count; index++) {
        const CettaGsltRewriteV1 *rewrite =
            &composition->rewrites[index];
        if (!direct_literal_v1(program, "; gslt-source-rewrite-v1 index=") ||
            !direct_size_v1(program, index) ||
            !direct_literal_v1(program, " presentation=") ||
            !direct_literal_v1(program, rewrite->presentation_name) ||
            !direct_literal_v1(program, " rule=") ||
            !direct_literal_v1(program, rewrite->name) ||
            !direct_literal_v1(program, " kind=") ||
            !direct_literal_v1(
                program, direct_source_rewrite_kind_v1(rewrite)) ||
            !direct_byte_v1(program, (uint8_t)'\n'))
            return false;
    }
    return direct_byte_v1(program, (uint8_t)'\n');
}

static bool direct_render_rule_realization_v1(
    DirectBufferV1 *program, const DirectRuleV1 *rule,
    const char *target, const DirectBindingModeV1 *mode) {
    if (rule == NULL || rule->source == NULL || target == NULL ||
        !direct_literal_v1(
            program, "; gslt-lowered-equation-v1 source-rewrite=") ||
        !direct_size_v1(program, rule->source_index) ||
        !direct_literal_v1(program, " source-presentation=") ||
        !direct_literal_v1(program, rule->source->presentation_name) ||
        !direct_literal_v1(program, " source-rule=") ||
        !direct_literal_v1(program, rule->source->name) ||
        !direct_literal_v1(program, " target=") ||
        !direct_literal_v1(program, target))
        return false;
    if (mode != NULL) {
        if (!direct_literal_v1(program, " relation=") ||
            !direct_literal_v1(program, mode->relation) ||
            !direct_literal_v1(program, " input="))
            return false;
        for (size_t argument = 0u; argument < mode->arity; argument++) {
            if (!direct_byte_v1(
                    program, mode->input_ground[argument]
                                 ? (uint8_t)'1' : (uint8_t)'0'))
                return false;
        }
        if (!direct_literal_v1(
                program, mode->functional
                             ? " functional=semidet"
                             : " functional=relational"))
            return false;
    }
    return direct_byte_v1(program, (uint8_t)'\n');
}

static bool direct_render_type_v1(DirectBufferV1 *program,
                                  const DirectOperatorV1 *relation) {
    if (!direct_literal_v1(program, "(: gslt:") ||
        !direct_literal_v1(program, relation->name) ||
        !direct_literal_v1(program, " (-> Atom %Undefined%))\n"))
        return false;
    return true;
}

static bool direct_render_call_v1(DirectBufferV1 *program,
                                  const Atom *relation,
                                  Arena *scratch) {
    return direct_literal_v1(program, "(gslt:") &&
           direct_literal_v1(
               program, atom_name_cstr(relation->expr.elems[0])) &&
           direct_byte_v1(program, (uint8_t)' ') &&
           direct_render_term_v1(program, relation, scratch, 0u) &&
           direct_byte_v1(program, (uint8_t)')');
}

static bool direct_render_mode_name_v1(
    DirectBufferV1 *program, const char *prefix,
    const DirectBindingModeV1 *mode) {
    if (mode->admitted && strcmp(prefix, "gslt:result:") != 0 &&
        !direct_literal_v1(program, "admitted:")) return false;
    if (!direct_literal_v1(program, prefix) ||
        !direct_literal_v1(program, mode->relation) ||
        !direct_byte_v1(program, (uint8_t)':'))
        return false;
    for (size_t argument = 0u; argument < mode->arity; argument++) {
        if (!direct_byte_v1(
                program, mode->input_ground[argument]
                             ? (uint8_t)'1' : (uint8_t)'0'))
            return false;
    }
    return true;
}

static bool direct_render_input_slot_v1(
    DirectBufferV1 *program, size_t argument) {
    char name[48];
    int length = snprintf(
        name, sizeof(name), "$__gslt_input_v1_%zu", argument);
    return length > 0 && (size_t)length < sizeof(name) &&
           direct_literal_v1(program, name);
}

/* Public entry names do not expose the functional-mode optimization. The
 * wrapper forwards the complete answer stream, including zero/many answers. */
static bool direct_render_entries_v1(
    DirectBufferV1 *program, const DirectBindingModesV1 *modes,
    const char *const *entries, size_t entry_count) {
    for (size_t index = 0u; index < modes->len; index++) {
        const DirectBindingModeV1 *mode = &modes->items[index];
        bool requested = false;
        size_t name_len = strlen(mode->relation);
        for (size_t entry = 0u; entry < entry_count; entry++) {
            const char *separator = strrchr(entries[entry], ':');
            if (separator == NULL ||
                (size_t)(separator - entries[entry]) != name_len ||
                strncmp(entries[entry], mode->relation, name_len) != 0 ||
                strlen(separator + 1u) != mode->arity)
                continue;
            bool equal = true;
            for (size_t arg = 0u; arg < mode->arity; arg++) {
                if (separator[arg + 1u] !=
                    (mode->input_ground[arg] ? '1' : '0'))
                    equal = false;
            }
            requested = requested || equal;
        }
        if (!requested || !mode->selected)
            continue;
        if (!direct_literal_v1(program, "; gslt-lowered-entry-v1 ") ||
            !direct_render_mode_name_v1(program, "gslt:entry:", mode) ||
            !direct_literal_v1(program, "\n(= (") ||
            !direct_render_mode_name_v1(program, "gslt:entry:", mode))
            return false;
        for (size_t arg = 0u; arg < mode->arity; arg++) {
            if (mode->input_ground[arg] &&
                (!direct_byte_v1(program, (uint8_t)' ') ||
                 !direct_render_input_slot_v1(program, arg)))
                return false;
        }
        if (!direct_literal_v1(program, ") (") ||
            !direct_render_mode_name_v1(program,
                mode->functional ? "gslt:fn:" : "gslt:mode:", mode))
            return false;
        for (size_t arg = 0u; arg < mode->arity; arg++) {
            if (mode->input_ground[arg] &&
                (!direct_byte_v1(program, (uint8_t)' ') ||
                 !direct_render_input_slot_v1(program, arg)))
                return false;
        }
        if (!direct_literal_v1(program, "))\n"))
            return false;
    }
    return true;
}

static bool direct_petta_pattern_requires_body_match_v1(
    const Atom *term, size_t depth) {
    if (!term || depth > GSLT_PETTA_DIRECT_DEPTH_LIMIT_V1 ||
        term->kind != ATOM_EXPR || term->expr.len == 0u)
        return false;
    const Atom *head = term->expr.elems[0];
    if (head && head->kind == ATOM_SYMBOL) {
        const char *name = atom_name_cstr((Atom *)head);
        static const char *const effectful_heads[] = {
            "test", "if", "progn", "prog1", "foldall", "forall",
            "maplist", "map-atom", "foldl", "id", "append", "cons",
            "#+", "unique", "union", "intersection", "subtraction",
            "length", "msort", "sort-atom", "first-from-pair", "first",
            "second-from-pair", "is-var", "is-ground", "is-expr",
            "is-space", "is-member", "is-alpha-member",
            "alpha-unique-atom", "list_to_set", "exclude-item", "repra",
            "sread", "bind!", "get-state", "change-state!", "new-state",
            "call", "eval", "reduce", "Predicate", "translatePredicate",
            "import_prolog_function", "process_metta_string",
            "callPredicate", "assertaPredicate", "assertzPredicate",
            "retractPredicate", "tabled", "add-translator-rule!",
            "remove-translator-rule!", "cut", "catch", "|->", "let",
            "chain", "quote", "superpose", "collapse", "once", "empty",
            "match", "case", "function", "return", "+", "-", "*", "/",
            "//", "%", "<", ">", "<=", ">=", "=", "==", "=alpha",
            "not",
        };
        for (size_t index = 0u;
             index < sizeof(effectful_heads) / sizeof(effectful_heads[0]);
             index++) {
            if (strcmp(name, effectful_heads[index]) == 0)
                return true;
        }
    }
    for (CettaExprIndex index = 1u;
         index < term->expr.len; index++) {
        if (direct_petta_pattern_requires_body_match_v1(
                term->expr.elems[index], depth + 1u))
            return true;
    }
    return false;
}

/* PeTTa equation heads are evaluation positions.  Keep the generated head
 * slot-based and match the authored source pattern structurally in the
 * body; otherwise a source constructor such as `cons` can be mistaken for a
 * target operation before the rule is selected. */
static bool direct_render_function_rule_head_v1(
    DirectBufferV1 *program, const Atom *application,
    const DirectBindingModeV1 *mode, Arena *scratch) {
    if (!direct_byte_v1(program, (uint8_t)'(') ||
        !direct_render_mode_name_v1(
            program, mode->functional ? "gslt:fn:" : "gslt:mode:",
            mode))
        return false;
    for (size_t argument = 0u; argument < mode->arity; argument++) {
        if (!mode->input_ground[argument])
            continue;
        const Atom *pattern =
            application->expr.elems[argument + 1u];
        if (!direct_byte_v1(program, (uint8_t)' '))
            return false;
        if (direct_petta_pattern_requires_body_match_v1(pattern, 0u)) {
            if (!direct_render_input_slot_v1(program, argument))
                return false;
        } else if (!direct_render_term_v1(
                       program, pattern, scratch, 0u)) {
            return false;
        }
    }
    return direct_byte_v1(program, (uint8_t)')');
}

static bool direct_render_function_call_v1(
    DirectBufferV1 *program, const Atom *application,
    const DirectBindingModeV1 *mode, Arena *scratch) {
    if (!direct_byte_v1(program, (uint8_t)'(') ||
        !direct_render_mode_name_v1(
            program, mode->functional ? "gslt:fn:" : "gslt:mode:",
            mode))
        return false;
    for (size_t argument = 0u; argument < mode->arity; argument++) {
        if (mode->input_ground[argument] &&
            (!direct_byte_v1(program, (uint8_t)' ') ||
             !direct_render_term_v1(
                 program, application->expr.elems[argument + 1u],
                 scratch, 0u)))
            return false;
    }
    return direct_byte_v1(program, (uint8_t)')');
}

static bool direct_render_selected_mode_call_v1(
    DirectBufferV1 *program, const Atom *application,
    const DirectBindingModeV1 *mode,
    const DirectBindingModeV1 *caller_mode,
    Arena *scratch) {
    if (mode == NULL || !mode->selected)
        return false;
    if (!mode->functional)
        return direct_literal_v1(
                   program,
                   "(superpose (collapse ") &&
               direct_render_function_call_v1(
                   program, application, mode, scratch) &&
               direct_literal_v1(program, "))");
    if (caller_mode == NULL || !caller_mode->functional)
        return direct_literal_v1(program, "(once ") &&
               direct_render_function_call_v1(
                   program, application, mode, scratch) &&
               direct_byte_v1(program, (uint8_t)')');
    /* The projected callee is a statically named equation in this program.
     * Emit an ordinary call. PeTTa's eval translates its raw argument, so
     * eval(quote(call)) returns call data rather than executing the callee.
     * This is not a general eval/quote fusion law for dynamic source terms. */
    return direct_render_function_call_v1(
        program, application, mode, scratch);
}

static bool direct_render_function_result_v1(
    DirectBufferV1 *program, const Atom *application,
    const DirectBindingModeV1 *mode, Arena *scratch) {
    size_t output_count = 0u;
    for (size_t argument = 0u; argument < mode->arity; argument++) {
        if (!mode->input_ground[argument])
            output_count++;
    }
    if (output_count > 0u &&
        !direct_byte_v1(program, (uint8_t)'('))
        return false;
    if (!direct_render_mode_name_v1(program, "gslt:result:", mode))
        return false;
    for (size_t argument = 0u; argument < mode->arity; argument++) {
        if (!mode->input_ground[argument] &&
            (!direct_byte_v1(program, (uint8_t)' ') ||
             !direct_render_term_v1(
                 program, application->expr.elems[argument + 1u],
                 scratch, 0u)))
            return false;
    }
    return output_count == 0u ||
           direct_byte_v1(program, (uint8_t)')');
}

static bool direct_render_function_value_v1(
    DirectBufferV1 *program, const Atom *application,
    const DirectBindingModeV1 *mode, Arena *scratch) {
    return direct_literal_v1(program, "(quote ") &&
           direct_render_function_result_v1(
               program, application, mode, scratch) &&
           direct_byte_v1(program, (uint8_t)')');
}

static bool direct_render_function_rule_v1(
    DirectBufferV1 *program, const DirectRuleV1 *rule,
    const DirectBindingModesV1 *modes,
    const DirectBindingModeV1 *mode, Arena *scratch) {
    DirectVariablesV1 known = {0};
    size_t body_count = (size_t)rule->body->expr.len - 1u;
    size_t input_count = 0u;
    bool ok = false;

    for (size_t argument = 0u; argument < mode->arity; argument++) {
        if (mode->input_ground[argument] &&
            !direct_variables_add_term_v1(
                &known, rule->head->expr.elems[argument + 1u], 0u,
                NULL, 0u))
            goto done;
    }
    if (!direct_render_rule_realization_v1(
            program, rule, "mode", mode) ||
        !direct_literal_v1(program, "(= ") ||
        !direct_render_function_rule_head_v1(
            program, rule->head, mode, scratch) ||
        !direct_byte_v1(program, (uint8_t)'\n'))
        goto done;
    for (size_t argument = 0u; argument < mode->arity; argument++) {
        const Atom *pattern =
            rule->head->expr.elems[argument + 1u];
        if (!mode->input_ground[argument] ||
            !direct_petta_pattern_requires_body_match_v1(pattern, 0u))
            continue;
        if (!direct_literal_v1(program, "   (let ") ||
            !direct_render_term_v1(
                program, pattern, scratch, 0u) ||
            !direct_byte_v1(program, (uint8_t)' ') ||
            !direct_render_input_slot_v1(program, argument) ||
            !direct_byte_v1(program, (uint8_t)'\n'))
            goto done;
        input_count++;
    }
    for (size_t index = 0u; index < body_count; index++) {
        Atom *goal = rule->body->expr.elems[index + 1u];
        const char *relation = atom_name_cstr(goal->expr.elems[0]);
        size_t arity = (size_t)goal->expr.len - 1u;
        uint8_t *input_ground = (uint8_t *)malloc(arity);
        const DirectBindingModeV1 *callee_mode;
        if (input_ground == NULL)
            goto done;
        for (size_t argument = 0u; argument < arity; argument++) {
            input_ground[argument] = direct_term_is_ground_v1(
                &known, goal->expr.elems[argument + 1u], 0u);
        }
        callee_mode = direct_find_binding_mode_v1(
            modes, relation, arity, input_ground);
        free(input_ground);
        if (!direct_literal_v1(program, "   (let "))
            goto done;
        if (callee_mode != NULL && callee_mode->selected) {
            if (!direct_render_function_result_v1(
                    program, goal, callee_mode, scratch) ||
                !direct_byte_v1(program, (uint8_t)' ') ||
                !direct_render_selected_mode_call_v1(
                    program, goal, callee_mode, mode, scratch))
                goto done;
        } else if (!direct_literal_v1(program, "$_ ") ||
                   !direct_render_call_v1(program, goal, scratch)) {
            goto done;
        }
        if (!direct_byte_v1(program, (uint8_t)'\n'))
            goto done;
        if (callee_mode != NULL) {
            for (size_t argument = 0u; argument < arity; argument++) {
                if (callee_mode->success_ground[argument] &&
                    !direct_variables_add_term_v1(
                        &known, goal->expr.elems[argument + 1u], 0u,
                        NULL, 0u))
                    goto done;
            }
        }
    }
    if (!direct_literal_v1(program, "   ") ||
        !direct_render_function_value_v1(
            program, rule->head, mode, scratch))
        goto done;
    for (size_t index = 0u; index < body_count; index++) {
        if (!direct_byte_v1(program, (uint8_t)')'))
            goto done;
    }
    for (size_t index = 0u; index < input_count; index++) {
        if (!direct_byte_v1(program, (uint8_t)')'))
            goto done;
    }
    ok = direct_literal_v1(program, ")\n");

done:
    free(known.items);
    return ok;
}

/* ===== Ground fact-group factoring (emission shape only) =====
 *
 * Large ground fact families repeat one constant context per fact
 * (identity envelopes), multiplying program text, parse time, and
 * equation size.  Factoring emits the shared context once, in a single
 * wrapper equation, and each fact as its varying residues under a
 * private index relation.  The defined relation's extension is
 * unchanged by construction: the wrapper composed with the index
 * facts reproduces exactly the original equations, in the original
 * order, with the original multiplicity. */

#define DIRECT_FACTOR_MIN_GROUP_V1 64u
#define DIRECT_FACTOR_MAX_SLOTS_V1 16u
#define DIRECT_FACTOR_MIN_SAVED_BYTES_V1 48u

typedef struct DirectFactorNodeV1 {
    /* Exactly one of: slot, constant, or congruent expression. */
    bool slot;
    size_t slot_index;                /* assigned in DFS order */
    bool slot_output;                 /* slot sits in an output arg */
    const Atom *constant;             /* all facts agree here */
    struct DirectFactorNodeV1 **children; /* congruent expression */
    size_t child_count;
} DirectFactorNodeV1;

static void direct_factor_node_free_v1(DirectFactorNodeV1 *node) {
    if (node == NULL)
        return;
    for (size_t index = 0u; index < node->child_count; index++)
        direct_factor_node_free_v1(node->children[index]);
    free(node->children);
    free(node);
}

static bool direct_factor_term_ground_v1(const Atom *term,
                                         size_t depth) {
    if (term == NULL || depth > GSLT_PETTA_DIRECT_DEPTH_LIMIT_V1)
        return false;
    if (term->kind == ATOM_VAR || direct_is_source_variable_v1(term))
        return false;
    if (term->kind != ATOM_EXPR)
        return true;
    for (CettaExprIndex index = 0u; index < term->expr.len; index++) {
        if (!direct_factor_term_ground_v1(
                term->expr.elems[index], depth + 1u))
            return false;
    }
    return true;
}

/* Builds the least general generalization of the group's subterms at
 * one position.  `subs` holds the current subterm of every fact.
 * Returns NULL on allocation failure or when the slot budget is
 * exhausted (recorded in *aborted). */
static DirectFactorNodeV1 *direct_factor_scan_v1(
    const Atom *const *subs, size_t count, bool output,
    size_t *slot_count, bool *aborted) {
    DirectFactorNodeV1 *node;
    bool all_equal = true;
    for (size_t index = 1u; index < count; index++) {
        if (!direct_terms_equal_v1(subs[0], subs[index], 0u)) {
            all_equal = false;
            break;
        }
    }
    node = (DirectFactorNodeV1 *)calloc(1u, sizeof(*node));
    if (node == NULL)
        return NULL;
    if (all_equal) {
        node->constant = subs[0];
        return node;
    }
    {
        bool congruent = subs[0]->kind == ATOM_EXPR &&
                         subs[0]->expr.len > 0u;
        for (size_t index = 1u; congruent && index < count; index++) {
            congruent = subs[index]->kind == ATOM_EXPR &&
                        subs[index]->expr.len == subs[0]->expr.len;
        }
        if (congruent) {
            size_t width = (size_t)subs[0]->expr.len;
            const Atom **child_subs =
                (const Atom **)malloc(sizeof(*child_subs) * count);
            node->children = (DirectFactorNodeV1 **)calloc(
                width, sizeof(*node->children));
            if (child_subs == NULL || node->children == NULL) {
                free(child_subs);
                direct_factor_node_free_v1(node);
                return NULL;
            }
            node->child_count = width;
            for (size_t child = 0u; child < width; child++) {
                for (size_t index = 0u; index < count; index++) {
                    child_subs[index] =
                        subs[index]->expr.elems[child];
                }
                node->children[child] = direct_factor_scan_v1(
                    child_subs, count, output, slot_count, aborted);
                if (node->children[child] == NULL || *aborted) {
                    free(child_subs);
                    direct_factor_node_free_v1(node);
                    return NULL;
                }
            }
            free(child_subs);
            return node;
        }
    }
    if (*slot_count >= DIRECT_FACTOR_MAX_SLOTS_V1) {
        *aborted = true;
        direct_factor_node_free_v1(node);
        return NULL;
    }
    node->slot = true;
    node->slot_output = output;
    node->slot_index = (*slot_count)++;
    return node;
}

static bool direct_factor_render_slot_var_v1(
    DirectBufferV1 *program, const DirectFactorNodeV1 *node) {
    char name[32];
    (void)snprintf(name, sizeof(name), "$gf%c%zu",
                   node->slot_output ? 'o' : 'i', node->slot_index);
    return direct_literal_v1(program, name);
}

static bool direct_factor_render_template_v1(
    DirectBufferV1 *program, const DirectFactorNodeV1 *node,
    Arena *scratch) {
    if (node->slot)
        return direct_factor_render_slot_var_v1(program, node);
    if (node->constant != NULL)
        return direct_render_term_v1(
            program, node->constant, scratch, 0u);
    if (!direct_byte_v1(program, (uint8_t)'('))
        return false;
    for (size_t child = 0u; child < node->child_count; child++) {
        if ((child > 0u &&
             !direct_byte_v1(program, (uint8_t)' ')) ||
            !direct_factor_render_template_v1(
                program, node->children[child], scratch))
            return false;
    }
    return direct_byte_v1(program, (uint8_t)')');
}

/* Walks a fact alongside the template, emitting each slot residue.
 * `want_output` selects which slot family is emitted; residues are
 * space-separated, with `*first` tracking the separator. */
static bool direct_factor_render_residues_v1(
    DirectBufferV1 *program, const DirectFactorNodeV1 *node,
    const Atom *fact_term, bool want_output, bool *first,
    Arena *scratch) {
    if (node->slot) {
        if (node->slot_output != want_output)
            return true;
        if (!*first && !direct_byte_v1(program, (uint8_t)' '))
            return false;
        *first = false;
        return direct_render_term_v1(program, fact_term, scratch, 0u);
    }
    if (node->constant != NULL)
        return true;
    for (size_t child = 0u; child < node->child_count; child++) {
        if (!direct_factor_render_residues_v1(
                program, node->children[child],
                fact_term->expr.elems[child], want_output, first,
                scratch))
            return false;
    }
    return true;
}

static bool direct_factor_render_index_head_v1(
    DirectBufferV1 *program, const DirectBindingModeV1 *mode) {
    return direct_render_mode_name_v1(program, "gslt:fnix:", mode);
}

/* Renders the factored form of one (relation, mode) ground fact
 * group: one wrapper equation carrying the shared context, then one
 * compact index fact per source fact.  Sets *factored on success;
 * leaves *factored false (without emitting) whenever the group is
 * ineligible, so the ordinary per-fact path still applies. */
static bool direct_render_factored_group_v1(
    DirectBufferV1 *program, const DirectRuleV1 *const *group,
    size_t count, const DirectBindingModeV1 *mode, Arena *scratch,
    bool *factored) {
    DirectFactorNodeV1 **arg_nodes = NULL;
    const Atom **subs = NULL;
    size_t slot_count = 0u;
    size_t input_slots = 0u;
    size_t output_slots = 0u;
    bool aborted = false;
    bool ok = false;

    *factored = false;
    for (size_t index = 0u; index < count; index++) {
        const DirectRuleV1 *rule = group[index];
        if ((size_t)rule->body->expr.len != 1u ||
            (size_t)rule->head->expr.len != mode->arity + 1u ||
            !direct_factor_term_ground_v1(rule->head, 0u))
            return true;
    }
    arg_nodes = (DirectFactorNodeV1 **)calloc(
        mode->arity, sizeof(*arg_nodes));
    subs = (const Atom **)malloc(sizeof(*subs) * count);
    if (arg_nodes == NULL || subs == NULL)
        goto done;
    for (size_t argument = 0u; argument < mode->arity; argument++) {
        for (size_t index = 0u; index < count; index++) {
            subs[index] =
                group[index]->head->expr.elems[argument + 1u];
        }
        arg_nodes[argument] = direct_factor_scan_v1(
            subs, count, !mode->input_ground[argument],
            &slot_count, &aborted);
        if (arg_nodes[argument] == NULL) {
            ok = !aborted ? false : true;
            goto done;
        }
    }
    if (slot_count == 0u) {
        ok = true;
        goto done;
    }
    {
        /* Count input/output slots and check the per-fact saving. */
        DirectBufferV1 probe = {0};
        size_t full_len;
        bool first = true;
        for (size_t argument = 0u; argument < mode->arity;
             argument++) {
            if (!direct_render_term_v1(
                    &probe, group[0]->head->expr.elems[argument + 1u],
                    scratch, 0u)) {
                free(probe.bytes);
                goto done;
            }
        }
        full_len = probe.len;
        probe.len = 0u;
        for (size_t argument = 0u; argument < mode->arity;
             argument++) {
            if (!direct_factor_render_residues_v1(
                    &probe, arg_nodes[argument],
                    group[0]->head->expr.elems[argument + 1u], false,
                    &first, scratch) ||
                !direct_factor_render_residues_v1(
                    &probe, arg_nodes[argument],
                    group[0]->head->expr.elems[argument + 1u], true,
                    &first, scratch)) {
                free(probe.bytes);
                goto done;
            }
        }
        if (full_len < probe.len ||
            full_len - probe.len < DIRECT_FACTOR_MIN_SAVED_BYTES_V1) {
            free(probe.bytes);
            ok = true;
            goto done;
        }
        free(probe.bytes);
    }
    for (size_t argument = 0u; argument < mode->arity; argument++) {
        /* Count slots per family. */
        DirectFactorNodeV1 *pending[DIRECT_FACTOR_MAX_SLOTS_V1 * 4u];
        size_t top = 0u;
        pending[top++] = arg_nodes[argument];
        while (top > 0u) {
            DirectFactorNodeV1 *node = pending[--top];
            if (node->slot) {
                if (node->slot_output)
                    output_slots++;
                else
                    input_slots++;
                continue;
            }
            for (size_t child = 0u; child < node->child_count;
                 child++) {
                if (node->children[child]->constant == NULL &&
                    top < DIRECT_FACTOR_MAX_SLOTS_V1 * 4u)
                    pending[top++] = node->children[child];
            }
        }
    }

    /* Wrapper equation: shared context once, residues bound by the
     * private index relation. */
    if (!direct_render_rule_realization_v1(
            program, group[0], "mode-factored", mode) ||
        !direct_literal_v1(program, "(= ") ||
        !direct_byte_v1(program, (uint8_t)'(') ||
        !direct_render_mode_name_v1(
            program, mode->functional ? "gslt:fn:" : "gslt:mode:",
            mode))
        goto done;
    for (size_t argument = 0u; argument < mode->arity; argument++) {
        if (!mode->input_ground[argument])
            continue;
        if (!direct_byte_v1(program, (uint8_t)' ') ||
            !direct_factor_render_template_v1(
                program, arg_nodes[argument], scratch))
            goto done;
    }
    if (!direct_literal_v1(program, ")\n   (let (quote "))
        goto done;
    if (output_slots == 0u) {
        if (!direct_literal_v1(program, "()"))
            goto done;
    } else {
        if (output_slots > 1u &&
            !direct_byte_v1(program, (uint8_t)'('))
            goto done;
        {
            bool first_out = true;
            for (size_t slot = 0u; slot < slot_count; slot++) {
                char name[32];
                bool is_output = false;
                /* Determine family by lookup over the trees. */
                for (size_t argument = 0u;
                     argument < mode->arity && !is_output;
                     argument++) {
                    DirectFactorNodeV1 *pending[
                        DIRECT_FACTOR_MAX_SLOTS_V1 * 4u];
                    size_t top = 0u;
                    pending[top++] = arg_nodes[argument];
                    while (top > 0u) {
                        DirectFactorNodeV1 *node = pending[--top];
                        if (node->slot) {
                            if (node->slot_index == slot &&
                                node->slot_output)
                                is_output = true;
                            continue;
                        }
                        for (size_t child = 0u;
                             child < node->child_count; child++) {
                            if (node->children[child]->constant ==
                                    NULL &&
                                top < DIRECT_FACTOR_MAX_SLOTS_V1 * 4u)
                                pending[top++] =
                                    node->children[child];
                        }
                    }
                }
                if (!is_output)
                    continue;
                (void)snprintf(name, sizeof(name), "$gfo%zu", slot);
                if ((!first_out &&
                     !direct_byte_v1(program, (uint8_t)' ')) ||
                    !direct_literal_v1(program, name))
                    goto done;
                first_out = false;
            }
        }
        if (output_slots > 1u &&
            !direct_byte_v1(program, (uint8_t)')'))
            goto done;
    }
    if (!direct_literal_v1(program, ") (") ||
        !direct_factor_render_index_head_v1(program, mode))
        goto done;
    for (size_t slot = 0u; slot < slot_count; slot++) {
        char name[32];
        bool is_input = false;
        for (size_t argument = 0u;
             argument < mode->arity && !is_input; argument++) {
            DirectFactorNodeV1 *pending[
                DIRECT_FACTOR_MAX_SLOTS_V1 * 4u];
            size_t top = 0u;
            pending[top++] = arg_nodes[argument];
            while (top > 0u) {
                DirectFactorNodeV1 *node = pending[--top];
                if (node->slot) {
                    if (node->slot_index == slot &&
                        !node->slot_output)
                        is_input = true;
                    continue;
                }
                for (size_t child = 0u;
                     child < node->child_count; child++) {
                    if (node->children[child]->constant == NULL &&
                        top < DIRECT_FACTOR_MAX_SLOTS_V1 * 4u)
                        pending[top++] = node->children[child];
                }
            }
        }
        if (!is_input)
            continue;
        (void)snprintf(name, sizeof(name), " $gfi%zu", slot);
        if (!direct_literal_v1(program, name))
            goto done;
    }
    if (!direct_literal_v1(program, ")\n   (quote (") ||
        !direct_render_mode_name_v1(program, "gslt:result:", mode))
        goto done;
    for (size_t argument = 0u; argument < mode->arity; argument++) {
        if (mode->input_ground[argument])
            continue;
        if (!direct_byte_v1(program, (uint8_t)' ') ||
            !direct_factor_render_template_v1(
                program, arg_nodes[argument], scratch))
            goto done;
    }
    if (!direct_literal_v1(program, "))))\n\n"))
        goto done;

    /* Index facts: one compact equation per source fact. */
    for (size_t index = 0u; index < count; index++) {
        bool first = true;
        if (!direct_render_rule_realization_v1(
                program, group[index], "mode-fact", mode) ||
            !direct_literal_v1(program, "(= (") ||
            !direct_factor_render_index_head_v1(program, mode))
            goto done;
        first = false; /* head already emitted: every input residue
                        * receives a leading separator */
        for (size_t argument = 0u; argument < mode->arity;
             argument++) {
            if (!direct_factor_render_residues_v1(
                    program, arg_nodes[argument],
                    group[index]->head->expr.elems[argument + 1u],
                    false, &first, scratch))
                goto done;
        }
        if (!direct_literal_v1(program, ") (quote "))
            goto done;
        if (output_slots == 0u) {
            if (!direct_literal_v1(program, "()"))
                goto done;
        } else {
            bool first_out = true;
            if (output_slots > 1u &&
                !direct_byte_v1(program, (uint8_t)'('))
                goto done;
            for (size_t argument = 0u; argument < mode->arity;
                 argument++) {
                if (!direct_factor_render_residues_v1(
                        program, arg_nodes[argument],
                        group[index]->head->expr.elems[argument + 1u],
                        true, &first_out, scratch))
                    goto done;
            }
            if (output_slots > 1u &&
                !direct_byte_v1(program, (uint8_t)')'))
                goto done;
        }
        if (!direct_literal_v1(program, "))\n"))
            goto done;
    }
    *factored = true;
    ok = true;

done:
    if (subs != NULL)
        free((void *)subs);
    if (arg_nodes != NULL) {
        for (size_t argument = 0u; argument < mode->arity;
             argument++)
            direct_factor_node_free_v1(arg_nodes[argument]);
        free(arg_nodes);
    }
    return ok || aborted;
}

typedef struct {
    const DirectRuleV1 *equal_rule;
    const DirectRuleV1 *different_rule;
    const Atom *different_goal;
    size_t unequal_left_argument;
    size_t unequal_right_argument;
    bool equality_arguments_quoted;
} DirectEqualityPartitionV1;

/* Recognize the complete two-rule partition
 *
 *   R x x Equal.
 *   R x y Different :- apart x y.
 *
 * for the ground-ground-output mode, provided the authored target equation
 * proves that `apart` is exactly ground structural disequality.  This is a
 * target-generic decision-tree specialization: the source rules and target
 * capability jointly determine both branches, so no guest relation name or
 * result constructor is built into the compiler. */
static bool direct_equality_partition_v1(
    const DirectRulesV1 *rules,
    const DirectRulesV1 *target_equations,
    const DirectBindingModeV1 *mode,
    DirectEqualityPartitionV1 *partition) {
    const DirectRuleV1 *equal_rule = NULL;
    const DirectRuleV1 *different_rule = NULL;
    const Atom *different_goal = NULL;
    size_t unequal_left_argument = SIZE_MAX;
    size_t unequal_right_argument = SIZE_MAX;
    bool equality_arguments_quoted = false;
    size_t relation_rule_count = 0u;

    if (!rules || !target_equations || !mode || !partition ||
        !mode->functional || mode->arity != 3u ||
        !mode->input_ground[0] || !mode->input_ground[1] ||
        mode->input_ground[2])
        return false;

    for (size_t index = 0u; index < rules->len; index++) {
        const DirectRuleV1 *rule = &rules->items[index];
        if ((size_t)rule->head->expr.len != mode->arity + 1u ||
            strcmp(atom_name_cstr(rule->head->expr.elems[0]),
                   mode->relation) != 0)
            continue;
        relation_rule_count++;
        const Atom *left = rule->head->expr.elems[1];
        const Atom *right = rule->head->expr.elems[2];
        const Atom *output = rule->head->expr.elems[3];
        if (!direct_factor_term_ground_v1(output, 0u))
            return false;

        if ((size_t)rule->body->expr.len == 1u &&
            direct_is_source_variable_v1(left) &&
            direct_is_source_variable_v1(right) &&
            direct_same_variable_v1(left, right)) {
            if (equal_rule != NULL)
                return false;
            equal_rule = rule;
            continue;
        }

        if ((size_t)rule->body->expr.len == 2u &&
            direct_is_source_variable_v1(left) &&
            direct_is_source_variable_v1(right) &&
            !direct_same_variable_v1(left, right)) {
            const Atom *goal = rule->body->expr.elems[1];
            if (!goal || goal->kind != ATOM_EXPR ||
                goal->expr.len < 2u)
                return false;
            size_t goal_arity = (size_t)goal->expr.len - 1u;
            DirectGroundDisequalityCapabilityV1 capability;
            if (!direct_target_ground_disequality_capability_v1(
                    target_equations,
                    atom_name_cstr(goal->expr.elems[0]),
                    goal_arity, &capability))
                return false;
            const Atom *apart_left = goal->expr.elems[
                capability.unequal_left_argument + 1u];
            const Atom *apart_right = goal->expr.elems[
                capability.unequal_right_argument + 1u];
            bool aligned =
                direct_terms_equal_v1(apart_left, left, 0u) &&
                direct_terms_equal_v1(apart_right, right, 0u);
            bool reversed =
                direct_terms_equal_v1(apart_left, right, 0u) &&
                direct_terms_equal_v1(apart_right, left, 0u);
            if ((!aligned && !reversed) || different_rule != NULL)
                return false;
            different_rule = rule;
            different_goal = goal;
            unequal_left_argument = capability.unequal_left_argument;
            unequal_right_argument = capability.unequal_right_argument;
            equality_arguments_quoted =
                capability.equality_arguments_quoted;
            continue;
        }
        return false;
    }

    if (relation_rule_count != 2u || !equal_rule ||
        !different_rule || !different_goal)
        return false;
    *partition = (DirectEqualityPartitionV1){
        .equal_rule = equal_rule,
        .different_rule = different_rule,
        .different_goal = different_goal,
        .unequal_left_argument = unequal_left_argument,
        .unequal_right_argument = unequal_right_argument,
        .equality_arguments_quoted = equality_arguments_quoted,
    };
    return true;
}

static bool direct_render_equality_partition_v1(
    DirectBufferV1 *program,
    const DirectEqualityPartitionV1 *partition,
    const DirectBindingModeV1 *mode, Arena *scratch) {
    const Atom *different_goal = partition->different_goal;
    return direct_render_rule_realization_v1(
               program, partition->equal_rule,
               "mode-equality-partition-equal", mode) &&
           direct_render_rule_realization_v1(
               program, partition->different_rule,
               "mode-equality-partition-different", mode) &&
           direct_literal_v1(program, "(= ") &&
           direct_render_function_call_v1(
               program, partition->different_rule->head,
               mode, scratch) &&
           direct_literal_v1(program, "\n   (if (== ") &&
           (!partition->equality_arguments_quoted ||
            direct_literal_v1(program, "(quote ")) &&
           direct_render_term_v1(
               program,
               different_goal->expr.elems[
                   partition->unequal_left_argument + 1u],
               scratch, 0u) &&
           (!partition->equality_arguments_quoted ||
            direct_byte_v1(program, (uint8_t)')')) &&
           direct_byte_v1(program, (uint8_t)' ') &&
           (!partition->equality_arguments_quoted ||
            direct_literal_v1(program, "(quote ")) &&
           direct_render_term_v1(
               program,
               different_goal->expr.elems[
                   partition->unequal_right_argument + 1u],
               scratch, 0u) &&
           (!partition->equality_arguments_quoted ||
            direct_byte_v1(program, (uint8_t)')')) &&
           direct_literal_v1(program, ")\n      ") &&
           direct_render_function_value_v1(
               program, partition->equal_rule->head,
               mode, scratch) &&
           direct_literal_v1(program, "\n      ") &&
           direct_render_function_value_v1(
               program, partition->different_rule->head,
               mode, scratch) &&
           direct_literal_v1(program, "))\n");
}

static bool direct_render_function_modes_v1(
    DirectBufferV1 *program, const DirectRulesV1 *rules,
    const DirectRulesV1 *target_equations,
    const DirectBindingModesV1 *modes, Arena *scratch) {
    bool any = false;
    for (size_t mode_index = 0u; mode_index < modes->len; mode_index++) {
        const DirectBindingModeV1 *mode = &modes->items[mode_index];
        bool factored = false;
        if (!mode->selected)
            continue;
        if (!any &&
            !direct_literal_v1(
                program,
                "; mode-specialized relation projections\n"))
            return false;
        any = true;
        {
            DirectEqualityPartitionV1 partition;
            if (direct_equality_partition_v1(
                    rules, target_equations, mode, &partition)) {
                if (!direct_render_equality_partition_v1(
                        program, &partition, mode, scratch) ||
                    !direct_byte_v1(program, (uint8_t)'\n'))
                    return false;
                continue;
            }
        }
        {
            size_t group_count = 0u;
            for (size_t rule_index = 0u;
                 rule_index < rules->len; rule_index++) {
                const DirectRuleV1 *rule = &rules->items[rule_index];
                if ((size_t)rule->head->expr.len - 1u ==
                        mode->arity &&
                    strcmp(atom_name_cstr(rule->head->expr.elems[0]),
                           mode->relation) == 0)
                    group_count++;
            }
            if (group_count >= DIRECT_FACTOR_MIN_GROUP_V1) {
                const DirectRuleV1 **group =
                    (const DirectRuleV1 **)malloc(
                        sizeof(*group) * group_count);
                size_t cursor = 0u;
                if (group == NULL)
                    return false;
                for (size_t rule_index = 0u;
                     rule_index < rules->len; rule_index++) {
                    const DirectRuleV1 *rule =
                        &rules->items[rule_index];
                    if ((size_t)rule->head->expr.len - 1u ==
                            mode->arity &&
                        strcmp(atom_name_cstr(
                                   rule->head->expr.elems[0]),
                               mode->relation) == 0)
                        group[cursor++] = rule;
                }
                if (!direct_render_factored_group_v1(
                        program, group, group_count, mode, scratch,
                        &factored)) {
                    free(group);
                    return false;
                }
                free(group);
            }
        }
        if (factored)
            continue;
        for (size_t rule_index = 0u;
             rule_index < rules->len; rule_index++) {
            const DirectRuleV1 *rule = &rules->items[rule_index];
            const char *relation =
                atom_name_cstr(rule->head->expr.elems[0]);
            size_t arity = (size_t)rule->head->expr.len - 1u;
            if (arity == mode->arity &&
                strcmp(relation, mode->relation) == 0 &&
                (!direct_render_function_rule_v1(
                     program, rule, modes, mode, scratch) ||
                 !direct_byte_v1(program, (uint8_t)'\n')))
                return false;
        }
    }
    return !any || direct_byte_v1(program, (uint8_t)'\n');
}

static bool direct_render_rule_v1(DirectBufferV1 *program,
                                  const DirectRuleV1 *rule,
                                  const DirectBindingModesV1 *modes,
                                  Arena *scratch) {
    DirectVariablesV1 known = {0};
    size_t body_count = (size_t)rule->body->expr.len - 1u;
    bool ok = false;
    if (!direct_render_rule_realization_v1(
            program, rule, "relation", NULL) ||
        !direct_literal_v1(program, "(= ") ||
        !direct_render_call_v1(program, rule->head, scratch) ||
        !direct_byte_v1(program, (uint8_t)'\n'))
        goto done;
    for (size_t index = 0u; index < body_count; index++) {
        Atom *goal = rule->body->expr.elems[index + 1u];
        const char *relation = atom_name_cstr(goal->expr.elems[0]);
        size_t arity = (size_t)goal->expr.len - 1u;
        uint8_t *input_ground = (uint8_t *)malloc(arity);
        const DirectBindingModeV1 *callee_mode = NULL;
        if (input_ground == NULL)
            goto done;
        for (size_t argument = 0u; argument < arity; argument++) {
            input_ground[argument] = direct_term_is_ground_v1(
                &known, goal->expr.elems[argument + 1u], 0u);
        }
        callee_mode = direct_find_binding_mode_v1(
            modes, relation, arity, input_ground);
        free(input_ground);
        if (!direct_literal_v1(program, "   (let "))
            goto done;
        if (callee_mode != NULL && callee_mode->selected) {
            if (!direct_render_function_result_v1(
                    program, goal, callee_mode, scratch) ||
                !direct_byte_v1(program, (uint8_t)' ') ||
                !direct_render_selected_mode_call_v1(
                    program, goal, callee_mode, NULL, scratch))
                goto done;
        } else if (!direct_literal_v1(program, "$_ ") ||
                   !direct_render_call_v1(program, goal, scratch)) {
            goto done;
        }
        if (!direct_byte_v1(program, (uint8_t)'\n'))
            goto done;
        if (callee_mode != NULL) {
            for (size_t argument = 0u; argument < arity; argument++) {
                if (callee_mode->success_ground[argument] &&
                    !direct_variables_add_term_v1(
                        &known, goal->expr.elems[argument + 1u], 0u,
                        NULL, 0u))
                    goto done;
            }
        }
    }
    if (!direct_literal_v1(program, "   ") ||
        !direct_render_term_v1(program, rule->head, scratch, 0u))
        goto done;
    for (size_t index = 0u; index < body_count; index++) {
        if (!direct_byte_v1(program, (uint8_t)')'))
            goto done;
    }
    ok = direct_literal_v1(program, ")\n");

done:
    free(known.items);
    return ok;
}

static const DirectRuleV1 *direct_find_capability_declaration_v1(
    const DirectRulesV1 *rules, const DirectOperatorV1 *capability) {
    for (size_t index = 0u; index < rules->len; index++) {
        const DirectRuleV1 *rule = &rules->items[index];
        size_t arity;
        if (!direct_head_v1(
                rule->head, "oslf-external-relation-decl-v1", 2u) ||
            rule->body->expr.len != 1u ||
            rule->head->expr.elems[1]->kind != ATOM_SYMBOL ||
            strcmp(atom_name_cstr(rule->head->expr.elems[1]),
                   capability->name) != 0 ||
            !direct_positive_arity_v1(
                rule->head->expr.elems[2], &arity) ||
            arity != capability->arity)
            continue;
        return rule;
    }
    return NULL;
}

static bool direct_render_capability_default_v1(
    DirectBufferV1 *program, const DirectOperatorV1 *capability,
    const DirectRuleV1 *source_rule) {
    if (!direct_render_rule_realization_v1(
            program, source_rule, "capability-default", NULL) ||
        !direct_literal_v1(program, "(= (gslt:") ||
        !direct_literal_v1(program, capability->name))
        return false;
    if (!direct_literal_v1(program, " ("))
        return false;
    if (!direct_literal_v1(program, capability->name))
        return false;
    for (size_t index = 0u; index < capability->arity; index++) {
        char variable[48];
        int written = snprintf(variable, sizeof(variable),
                               " $__gslt_capability_%zu", index);
        if (written < 0 || (size_t)written >= sizeof(variable) ||
            !direct_literal_v1(program, variable))
            return false;
    }
    return direct_literal_v1(program, ")) (empty))\n");
}

static bool direct_target_equations_define_capability_v1(
    const DirectRulesV1 *target_equations,
    const DirectOperatorV1 *capability) {
    char wrapper[512];
    int written = snprintf(
        wrapper, sizeof(wrapper), "gslt:%s", capability->name);
    if (written < 0 || (size_t)written >= sizeof(wrapper))
        return false;
    for (size_t index = 0u; index < target_equations->len; index++) {
        const Atom *equation = target_equations->items[index].head;
        const Atom *left;
        const Atom *relation;
        if (!direct_head_v1(equation, "metta-equation", 2u) ||
            !(left = equation->expr.elems[1]) ||
            !direct_head_v1(left, wrapper, 1u) ||
            !(relation = left->expr.elems[1]) ||
            relation->kind != ATOM_EXPR ||
            relation->expr.len != capability->arity + 1u ||
            relation->expr.elems[0]->kind != ATOM_SYMBOL)
            continue;
        if (strcmp(atom_name_cstr(relation->expr.elems[0]),
                   capability->name) == 0)
            return true;
    }
    return false;
}

static bool direct_render_target_equations_v1(
    DirectBufferV1 *program, const DirectRulesV1 *target_equations,
    Arena *scratch) {
    if (target_equations->len == 0u)
        return true;
    if (!direct_literal_v1(program, "; authored target equations\n"))
        return false;
    for (size_t index = 0u; index < target_equations->len; index++) {
        const DirectRuleV1 *rule = &target_equations->items[index];
        const Atom *equation = rule->head;
        if (!direct_render_rule_realization_v1(
                program, rule, "authored-target-equation", NULL) ||
            !direct_literal_v1(program, "(= ") ||
            !direct_render_term_v1(
                program, equation->expr.elems[1], scratch, 0u) ||
            !direct_byte_v1(program, (uint8_t)' ') ||
            !direct_render_term_v1(
                program, equation->expr.elems[2], scratch, 0u) ||
            !direct_literal_v1(program, ")\n"))
            return false;
    }
    return direct_byte_v1(program, (uint8_t)'\n');
}

static bool direct_compile_petta_v1(
    Atom *const *presentations,
    size_t presentation_count,
    const char *const *entry_modes,
    size_t entry_mode_count,
    bool closed_entry_residual,
    const Atom *native_type_packet,
    const CettaLanguageDefCoreV1 *admitted_language,
    const char *admitted_relation, size_t admitted_input, uint32_t admitted_type,
    uint8_t **program_out,
    size_t *program_len_out,
    size_t *rule_count_out,
    char source_digest_out[65],
    char *error,
    size_t error_size) {
    CettaGsltCompositionV1 composition = {0};
    DirectOperatorsV1 operators = {0};
    DirectOperatorsV1 relations = {0};
    DirectOperatorsV1 capabilities = {0};
    DirectRulesV1 rules = {0};
    DirectRulesV1 target_equations = {0};
    DirectRuleIndexV1 rule_index = {0};
    DirectBindingModesV1 binding_modes = {0};
    DirectIntegerNativeTypesV1 native_types = {0};
    DirectAdmissionDomainsV1 domains = {0};
    DirectBufferV1 program = {0};
    Arena scratch;
    bool ok = false;

    if (presentations == NULL || presentation_count == 0u ||
        (entry_mode_count > 0u && entry_modes == NULL) ||
        (closed_entry_residual && entry_mode_count == 0u) ||
        program_out == NULL || program_len_out == NULL ||
        rule_count_out == NULL || source_digest_out == NULL)
        return direct_error_v1(error, error_size,
                               "invalid direct PeTTa compilation request");
    *program_out = NULL;
    *program_len_out = 0u;
    *rule_count_out = 0u;
    source_digest_out[0] = '\0';
    arena_init(&scratch);

    if (!cetta_gslt_composition_build_v1(
            presentations, presentation_count, &composition,
            error, error_size))
        goto done;
    if (composition.equation_count != 0u) {
        (void)direct_error_v1(
            error, error_size,
            "direct PeTTa v1 has not yet chosen an execution orientation for authored equations");
        goto done;
    }
    for (size_t index = 0u; index < composition.operator_count; index++) {
        if (!direct_push_operator_v1(
                &operators, composition.operators[index].name,
                composition.operators[index].arity, error, error_size))
            goto done;
    }
    for (size_t index = 0u; index < composition.rewrite_count; index++) {
        CettaGsltRewriteV1 *rewrite = &composition.rewrites[index];
        if (direct_head_v1(rewrite->head, "metta-equation", 2u)) {
            if (rewrite->body->expr.len != 1u) {
                (void)direct_error_v1(
                    error, error_size,
                    "direct PeTTa target equations must be unconditional");
                goto done;
            }
            if (!direct_push_rule_v1(
                    &target_equations, rewrite, index,
                    error, error_size))
                goto done;
        } else if (!direct_push_rule_v1(
                       &rules, rewrite, index,
                       error, error_size)) {
            goto done;
        }
    }
    if (rules.len == 0u) {
        (void)direct_error_v1(error, error_size,
                              "direct PeTTa composition has no rewrite rules");
        goto done;
    }
    if (!direct_validate_target_equations_v1(
            &target_equations, &operators, error, error_size) ||
        !direct_validate_rules_v1(
            &rules, &operators, &relations, &capabilities,
            error, error_size) ||
        !direct_authenticate_integer_native_types_v1(
            native_type_packet, presentations, presentation_count,
            &target_equations, &relations, &native_types, &scratch,
            error, error_size) ||
        !direct_rule_index_build_v1(
            &rules, &rule_index, error, error_size) ||
        !direct_analyze_binding_modes_v1(
            &rules, &rule_index, &relations, &capabilities,
            &target_equations, entry_modes, entry_mode_count, &binding_modes,
            error, error_size))
        goto done;
    if (admitted_language != NULL) {
        for (size_t i = 0u; i < target_equations.len; i++) {
            const Atom *lhs = target_equations.items[i].head->expr.elems[1];
            const Atom *head = lhs->kind == ATOM_EXPR && lhs->expr.len > 0u
                ? lhs->expr.elems[0] : lhs;
            if (direct_head_v1(lhs, "metta-nullary", 1u)) head = lhs->expr.elems[1];
            bool collision = direct_is_source_variable_v1(head);
            if (head->kind == ATOM_SYMBOL) {
                const char *name = atom_name_cstr((Atom *)head);
                collision = collision || strncmp(name, "admitted:gslt:", 14u) == 0 ||
                    strncmp(name, "gslt:admitted-entry:", 20u) == 0 ||
                    strcmp(name, "langdef:admit-term") == 0 ||
                    strcmp(name, "langdef:language-wire-sha256") == 0 ||
                    strncmp(name, "__cetta_lib_language_def_term_", 30u) == 0 ||
                    strcmp(name, "LangDef:LanguageWireSHA256") == 0 ||
                    strcmp(name, "LangDef:TermAdmitted") == 0 ||
                    strcmp(name, "import!") == 0 || strcmp(name, "empty") == 0 ||
                    strcmp(name, "let") == 0 ||
                    strcmp(name, "case") == 0 || strcmp(name, "==") == 0 ||
                    strcmp(name, "if") == 0;
                for (uint32_t j = 0u; j < admitted_language->term_len; j++)
                    collision = collision || direct_domain_text_v1(
                        &admitted_language->terms[j].label, name);
            }
            if (collision) {
                (void)direct_error_v1(error, error_size,
                    "authored target equation overlaps the admitted entry boundary");
                goto done;
            }
        }
        const char *bits = strrchr(entry_modes[0], ':');
        if (bits == NULL || !direct_domains_build_v1(&domains, admitted_language,
                &rules, &relations, admitted_relation, strlen(bits + 1u),
                admitted_input, admitted_type, error, error_size)) goto done;
        native_types.domains = &domains;
        for (size_t i = 0u; i < binding_modes.len; i++) binding_modes.items[i].admitted = 1u;
    }
    if (!direct_analyze_functional_modes_v1(
            &rules, &rule_index, &relations, &capabilities,
            &target_equations, &binding_modes, &native_types,
            error, error_size) ||
        !direct_select_reachable_modes_v1(
            &rules, &relations, &binding_modes, closed_entry_residual,
            error, error_size) ||
        !cetta_gslt_composition_digest_v1(
            presentations, presentation_count, source_digest_out,
            error, error_size))
        goto done;

    if (!direct_literal_v1(
            &program,
            "; generated by direct compositional GSLT-to-PeTTa lowering\n") ||
        !direct_literal_v1(&program, "; source-composition-sha256 ") ||
        !direct_literal_v1(&program, source_digest_out) ||
        !direct_literal_v1(&program, "\n") ||
        (closed_entry_residual &&
         !direct_literal_v1(&program, "; closed-entry-residual yes\n")) ||
        !direct_literal_v1(&program, "\n") ||
        (admitted_language == NULL &&
         !direct_render_source_inventory_v1(&program, &composition)) ||
        !direct_render_binding_modes_v1(
            &program, &binding_modes, closed_entry_residual))
        goto allocation_failure;
    for (size_t index = 0u; index < relations.len; index++) {
        if (admitted_language != NULL) break;
        if (!direct_render_type_v1(&program, &relations.items[index]))
            goto allocation_failure;
    }
    for (size_t index = 0u; index < capabilities.len; index++) {
        if (admitted_language != NULL) break;
        if (!direct_has_operator_v1(
                &relations, capabilities.items[index].name,
                capabilities.items[index].arity) &&
            !direct_render_type_v1(&program, &capabilities.items[index]))
            goto allocation_failure;
    }
    if (!direct_byte_v1(&program, (uint8_t)'\n'))
        goto allocation_failure;
    for (size_t index = 0u; index < capabilities.len; index++) {
        if (admitted_language != NULL) break;
        const DirectOperatorV1 *capability = &capabilities.items[index];
        if (!direct_has_operator_v1(
                &relations, capability->name, capability->arity) &&
            !direct_target_equations_define_capability_v1(
                &target_equations, capability)) {
            const DirectRuleV1 *source_rule =
                direct_find_capability_declaration_v1(
                    &rules, capability);
            if (source_rule == NULL) {
                (void)direct_error_v1(
                    error, error_size,
                    "direct PeTTa capability has no authored declaration");
                goto done;
            }
            if (!direct_render_capability_default_v1(
                    &program, capability, source_rule))
                goto allocation_failure;
        }
    }
    if (capabilities.len > 0u &&
        !direct_byte_v1(&program, (uint8_t)'\n'))
        goto allocation_failure;
    if (admitted_language == NULL && !direct_render_target_equations_v1(
            &program, &target_equations, &scratch))
        goto allocation_failure;
    if (!direct_render_function_modes_v1(
            &program, &rules, &target_equations,
            &binding_modes, &scratch))
        goto allocation_failure;
    if (!direct_render_entries_v1(
            &program, &binding_modes, entry_modes, entry_mode_count))
        goto allocation_failure;
    if (!closed_entry_residual) {
        for (size_t index = 0u; index < rules.len; index++) {
            if (!direct_render_rule_v1(
                    &program, &rules.items[index], &binding_modes,
                    &scratch) ||
                !direct_byte_v1(&program, (uint8_t)'\n'))
                goto allocation_failure;
        }
    }

    while (program.len >= 2u &&
           program.bytes[program.len - 1u] == (uint8_t)'\n' &&
           program.bytes[program.len - 2u] == (uint8_t)'\n') {
        program.len--;
    }

    *program_out = program.bytes;
    *program_len_out = program.len;
    *rule_count_out = rules.len + target_equations.len;
    program.bytes = NULL;
    ok = true;
    goto done;

allocation_failure:
    (void)direct_error_v1(error, error_size,
                          "out of memory rendering direct PeTTa program");

done:
    free(program.bytes);
    direct_domains_free_v1(&domains);
    free(native_types.items);
    direct_free_binding_modes_v1(&binding_modes);
    free(rule_index.items);
    free(target_equations.items);
    free(rules.items);
    free(relations.items);
    free(capabilities.items);
    free(operators.items);
    cetta_gslt_composition_free_v1(&composition);
    arena_free(&scratch);
    return ok;
}

bool cetta_gslt_petta_direct_selected_v1(
    Atom *const *presentations,
    size_t presentation_count,
    const char *const *entry_modes,
    size_t entry_mode_count,
    uint8_t **program_out,
    size_t *program_len_out,
    size_t *rule_count_out,
    char source_digest_out[65],
    char *error,
    size_t error_size) {
    return direct_compile_petta_v1(
        presentations, presentation_count, entry_modes, entry_mode_count,
        false, NULL, NULL, NULL, 0u, 0u, program_out, program_len_out, rule_count_out,
        source_digest_out, error, error_size);
}

bool cetta_gslt_petta_direct_closed_v1(
    Atom *const *presentations,
    size_t presentation_count,
    const char *const *entry_modes,
    size_t entry_mode_count,
    uint8_t **program_out,
    size_t *program_len_out,
    size_t *rule_count_out,
    char source_digest_out[65],
    char *error,
    size_t error_size) {
    return direct_compile_petta_v1(
        presentations, presentation_count, entry_modes, entry_mode_count,
        true, NULL, NULL, NULL, 0u, 0u, program_out, program_len_out, rule_count_out,
        source_digest_out, error, error_size);
}

bool cetta_gslt_petta_direct_v1(
    Atom *const *presentations,
    size_t presentation_count,
    uint8_t **program_out,
    size_t *program_len_out,
    size_t *rule_count_out,
    char source_digest_out[65],
    char *error,
    size_t error_size) {
    return cetta_gslt_petta_direct_selected_v1(
        presentations, presentation_count, NULL, 0u,
        program_out, program_len_out, rule_count_out,
        source_digest_out, error, error_size);
}

bool cetta_gslt_petta_direct_native_types_v1(
    Atom *const *presentations, size_t presentation_count,
    const Atom *native_type_packet,
    const char *const *entry_modes, size_t entry_mode_count,
    bool closed_entry_residual,
    uint8_t **program_out, size_t *program_len_out, size_t *rule_count_out,
    char source_digest_out[65], char *error, size_t error_size) {
    if (native_type_packet == NULL)
        return direct_error_v1(error, error_size,
                               "missing integer native-type packet");
    return direct_compile_petta_v1(
        presentations, presentation_count, entry_modes, entry_mode_count,
        closed_entry_residual, native_type_packet,
        NULL, NULL, 0u, 0u,
        program_out, program_len_out, rule_count_out,
        source_digest_out, error, error_size);
}

bool cetta_gslt_petta_direct_admitted_v1(
    Atom *const *presentations, size_t presentation_count,
    const Atom *native_type_packet,
    const char *const *entry_modes, size_t entry_mode_count,
    const CettaLanguageDefCoreV1 *admission_language,
    const char admission_wire_digest[65], const char *admission_entry,
    uint8_t **program_out, size_t *program_len_out, size_t *rule_count_out,
    char source_digest_out[65], char *error, size_t error_size) {
    const char *separator = admission_entry == NULL ? NULL : strrchr(admission_entry, ':');
    if (separator == NULL || separator == admission_entry || separator[1] == '\0' ||
        admission_language == NULL || native_type_packet == NULL ||
        admission_wire_digest == NULL || strlen(admission_wire_digest) != 64u ||
        admission_language->type_len == UINT32_MAX || entry_modes == NULL ||
        program_out == NULL || program_len_out == NULL || rule_count_out == NULL ||
        source_digest_out == NULL)
        return direct_error_v1(error, error_size, "invalid admitted PeTTa compilation request");
    for (size_t i = 0u; i < 64u; i++)
        if (!((admission_wire_digest[i] >= '0' && admission_wire_digest[i] <= '9') ||
              (admission_wire_digest[i] >= 'a' && admission_wire_digest[i] <= 'f')))
            return direct_error_v1(error, error_size, "invalid admission wire digest");
    *program_out = NULL;
    *program_len_out = 0u;
    size_t relation_len = (size_t)(separator - admission_entry);
    const char *selected = NULL;
    size_t input = 0u;
    for (size_t i = 0u; i < entry_mode_count; i++) {
        const char *bits = strrchr(entry_modes[i], ':');
        if (bits == NULL || (size_t)(bits - entry_modes[i]) != relation_len ||
            strncmp(entry_modes[i], admission_entry, relation_len) != 0) continue;
        size_t count = 0u;
        for (size_t j = 0u; bits[j + 1u] != '\0'; j++) {
            if (bits[j + 1u] == '1') { count++; input = j; }
            else if (bits[j + 1u] != '0') count = 2u;
        }
        if (count != 1u || selected != NULL)
            return direct_error_v1(error, error_size, "admitted entry needs exactly one declared single-input mode");
        selected = entry_modes[i];
    }
    if (selected == NULL)
        return direct_error_v1(error, error_size, "admitted entry is not a declared closed entry");
    uint32_t type = 0u;
    for (uint32_t i = 0u; i < admission_language->type_len; i++) {
        if (!direct_domain_text_v1(&admission_language->types[i].name, separator + 1u)) continue;
        if (type != 0u)
            return direct_error_v1(error, error_size, "ambiguous admission type");
        type = i + 1u;
    }
    if (type == 0u)
        return direct_error_v1(error, error_size, "unknown admission type");
    char *relation = malloc(relation_len + 1u);
    uint8_t *raw = NULL;
    uint8_t *typed = NULL;
    size_t raw_len = 0u, typed_len = 0u, typed_count = 0u;
    char typed_digest[65];
    DirectBufferV1 combined = {0};
    Arena scratch;
    arena_init(&scratch);
    bool ok = false;
    if (relation == NULL) goto done;
    memcpy(relation, admission_entry, relation_len);
    relation[relation_len] = '\0';
    if (!direct_compile_petta_v1(presentations, presentation_count,
            entry_modes, entry_mode_count, true, native_type_packet,
            NULL, NULL, 0u, 0u, &raw, &raw_len, rule_count_out,
            source_digest_out, error, error_size) ||
        !direct_compile_petta_v1(presentations, presentation_count,
            &selected, 1u, true, native_type_packet,
            admission_language, relation, input, type,
            &typed, &typed_len, &typed_count, typed_digest, error, error_size)) goto done;
    if (raw_len > SIZE_MAX - 2u || typed_len > SIZE_MAX - 2u - raw_len ||
        !direct_reserve_v1(&combined, raw_len + typed_len + 2u)) goto done;
    memcpy(combined.bytes, raw, raw_len);
    combined.len = raw_len;
    combined.bytes[combined.len++] = '\n';
    memcpy(combined.bytes + combined.len, typed, typed_len);
    combined.len += typed_len;
    /* The checked wrapper is the only public entry into the typed family.
     * It checks the live authority, not an input's claimed admission tag.
     * Failed admission/identity checks preserve the ordinary entry route. */
    if (!direct_literal_v1(&combined,
            "\n; Load the checked boundary before PeTTa translates its literal calls.\n"
            "!(let $_ (import! &self langdef) (empty))\n"
            "; checked LanguageDef admission boundary\n(= (gslt:admitted-entry:") ||
        !direct_literal_v1(&combined, selected) ||
        !direct_literal_v1(&combined, " $authority $input)\n  (if (== (langdef:language-wire-sha256 $authority) (LangDef:LanguageWireSHA256 ") ||
        !direct_render_term_v1(&combined, atom_string(&scratch, admission_wire_digest), &scratch, 0u) ||
        !direct_literal_v1(&combined, "))\n    (case (langdef:admit-term $authority ") ||
        !direct_render_term_v1(&combined, atom_string(&scratch, separator + 1u), &scratch, 0u) ||
        !direct_literal_v1(&combined, " $input)\n      (((LangDef:TermAdmitted) (admitted:gslt:entry:") ||
        !direct_literal_v1(&combined, selected) ||
        !direct_literal_v1(&combined, " $input))\n       ($_ (gslt:entry:") ||
        !direct_literal_v1(&combined, selected) ||
        !direct_literal_v1(&combined, " $input))))\n    (gslt:entry:") ||
        !direct_literal_v1(&combined, selected) ||
        !direct_literal_v1(&combined, " $input)))\n")) goto done;
    *program_out = combined.bytes;
    *program_len_out = combined.len;
    combined.bytes = NULL;
    ok = true;
done:
    free(combined.bytes);
    free(raw);
    free(typed);
    free(relation);
    arena_free(&scratch);
    if (!ok && error != NULL && error_size > 0u && error[0] == '\0')
        (void)direct_error_v1(error, error_size, "cannot render admitted PeTTa program");
    return ok;
}
