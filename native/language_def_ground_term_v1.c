#include "language_def_ground_term_v1.h"
#include "utf8_scalar_v1.h"

#include "src/symbol.h"

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    SymbolId symbol;
    const CettaLdGrammarRuleV1 *rule;
    const CettaLdTypeDeclV1 *result_type;
    const CettaLdTypeDeclV1 **parameter_types;
    bool constructor_profile;
} LdGroundTermRuleIndexEntryV1;

typedef struct {
    const CettaLdTypeDeclV1 **types;
    uint32_t type_len;
    LdGroundTermRuleIndexEntryV1 *rules;
    uint32_t rule_len;
} LdGroundTermIndexV1;

typedef struct {
    const Atom *term;
    const CettaLdTypeDeclV1 *type;
} LdGroundTermMemoEntryV1;

typedef struct {
    LdGroundTermMemoEntryV1 *entries;
    size_t capacity;
    size_t used;
} LdGroundTermMemoV1;

typedef struct {
    const CettaLanguageDefCoreV1 *language;
    const LdGroundTermIndexV1 *index;
    LdGroundTermMemoV1 memo;
    uint32_t depth_limit;
    uint64_t remaining_work;
    CettaLdGroundTermV1Status status;
    char *error_buf;
    size_t error_buf_size;
} LdGroundTermContextV1;

typedef struct {
    const Atom *term;
    const CettaLdTypeDeclV1 *expected_type;
    const LdGroundTermRuleIndexEntryV1 *selected;
    uint32_t depth;
    uint32_t next_child;
    bool entered;
} LdGroundTermAdmissionFrameV1;

typedef struct {
    LdGroundTermAdmissionFrameV1 *items;
    size_t len;
    size_t capacity;
} LdGroundTermAdmissionStackV1;

static bool ground_term_fail(LdGroundTermContextV1 *context,
                             CettaLdGroundTermV1Status status,
                             const char *message);

static size_t ground_term_pointer_hash(const Atom *term,
                                       const CettaLdTypeDeclV1 *type) {
    uintptr_t left = (uintptr_t)term;
    uintptr_t right = (uintptr_t)type;
    uint64_t mixed = (uint64_t)(left >> 3u) ^
        ((uint64_t)(right >> 3u) * UINT64_C(0x9e3779b97f4a7c15));
    mixed ^= mixed >> 30u;
    mixed *= UINT64_C(0xbf58476d1ce4e5b9);
    mixed ^= mixed >> 27u;
    mixed *= UINT64_C(0x94d049bb133111eb);
    mixed ^= mixed >> 31u;
    return (size_t)mixed;
}

static void ground_term_memo_clear(LdGroundTermMemoV1 *memo) {
    if (!memo)
        return;
    free(memo->entries);
    memset(memo, 0, sizeof(*memo));
}

static bool ground_term_memo_grow(LdGroundTermContextV1 *context) {
    LdGroundTermMemoV1 *memo;
    LdGroundTermMemoEntryV1 *grown;
    size_t next;

    if (!context)
        return false;
    memo = &context->memo;
    next = memo->capacity ? memo->capacity * 2u : 1024u;
    if (next < memo->capacity ||
        next > SIZE_MAX / sizeof(*grown))
        return ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT,
            "ground-term admission memo is too large");
    grown = calloc(next, sizeof(*grown));
    if (!grown)
        return ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT,
            "ground-term admission could not allocate its memo");
    for (size_t index = 0u; index < memo->capacity; index++) {
        LdGroundTermMemoEntryV1 entry = memo->entries[index];
        size_t slot;
        if (!entry.term)
            continue;
        slot = ground_term_pointer_hash(entry.term, entry.type) &
            (next - 1u);
        while (grown[slot].term)
            slot = (slot + 1u) & (next - 1u);
        grown[slot] = entry;
    }
    free(memo->entries);
    memo->entries = grown;
    memo->capacity = next;
    return true;
}

static bool ground_term_memo_contains(
    const LdGroundTermContextV1 *context,
    const Atom *term, const CettaLdTypeDeclV1 *type) {
    const LdGroundTermMemoV1 *memo;
    size_t slot;

    if (!context || !term || !type)
        return false;
    memo = &context->memo;
    if (memo->capacity == 0u)
        return false;
    slot = ground_term_pointer_hash(term, type) & (memo->capacity - 1u);
    while (memo->entries[slot].term) {
        if (memo->entries[slot].term == term &&
            memo->entries[slot].type == type)
            return true;
        slot = (slot + 1u) & (memo->capacity - 1u);
    }
    return false;
}

static bool ground_term_admission_stack_push(
    LdGroundTermContextV1 *context,
    LdGroundTermAdmissionStackV1 *stack,
    const CettaLdTypeDeclV1 *expected_type,
    const Atom *term,
    uint32_t depth) {
    LdGroundTermAdmissionFrameV1 *grown;
    size_t next;

    if (!context || !stack)
        return false;
    if (stack->len == stack->capacity) {
        next = stack->capacity ? stack->capacity * 2u : 128u;
        if (next < stack->capacity ||
            next > SIZE_MAX / sizeof(*grown))
            return ground_term_fail(
                context, CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT,
                "ground-term admission stack is too large");
        grown = realloc(stack->items, next * sizeof(*grown));
        if (!grown)
            return ground_term_fail(
                context, CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT,
                "ground-term admission could not grow its stack");
        stack->items = grown;
        stack->capacity = next;
    }
    stack->items[stack->len++] = (LdGroundTermAdmissionFrameV1){
        .term = term,
        .expected_type = expected_type,
        .selected = NULL,
        .depth = depth,
        .next_child = 0u,
        .entered = false,
    };
    return true;
}

static bool ground_term_memo_insert(
    LdGroundTermContextV1 *context,
    const Atom *term, const CettaLdTypeDeclV1 *type) {
    LdGroundTermMemoV1 *memo;
    size_t slot;

    if (!context || !term || !type)
        return false;
    memo = &context->memo;
    if (memo->capacity == 0u ||
        memo->used + 1u > memo->capacity - memo->capacity / 4u) {
        if (!ground_term_memo_grow(context))
            return false;
    }
    slot = ground_term_pointer_hash(term, type) & (memo->capacity - 1u);
    while (memo->entries[slot].term) {
        if (memo->entries[slot].term == term &&
            memo->entries[slot].type == type)
            return true;
        slot = (slot + 1u) & (memo->capacity - 1u);
    }
    memo->entries[slot] = (LdGroundTermMemoEntryV1){term, type};
    memo->used++;
    return true;
}

static int ld_text_compare(const CettaLdTextV1 *left,
                           const CettaLdTextV1 *right) {
    uint32_t common;
    int order;

    if (left == right)
        return 0;
    if (!left)
        return -1;
    if (!right)
        return 1;
    common = left->len < right->len ? left->len : right->len;
    if (common > 0u) {
        if (!left->bytes)
            return right->bytes ? -1 : 0;
        if (!right->bytes)
            return 1;
        order = memcmp(left->bytes, right->bytes, common);
        if (order != 0)
            return order;
    }
    if (left->len < right->len)
        return -1;
    if (left->len > right->len)
        return 1;
    return 0;
}

static int ground_term_type_pointer_compare(const void *left,
                                            const void *right) {
    const CettaLdTypeDeclV1 *const *left_type = left;
    const CettaLdTypeDeclV1 *const *right_type = right;
    return ld_text_compare(
        *left_type ? &(*left_type)->name : NULL,
        *right_type ? &(*right_type)->name : NULL);
}

static int ground_term_rule_index_compare(const void *left,
                                          const void *right) {
    const LdGroundTermRuleIndexEntryV1 *left_entry = left;
    const LdGroundTermRuleIndexEntryV1 *right_entry = right;
    if (left_entry->symbol < right_entry->symbol)
        return -1;
    if (left_entry->symbol > right_entry->symbol)
        return 1;
    if ((uintptr_t)left_entry->result_type <
        (uintptr_t)right_entry->result_type)
        return -1;
    if ((uintptr_t)left_entry->result_type >
        (uintptr_t)right_entry->result_type)
        return 1;
    return 0;
}

static void ground_term_index_clear(LdGroundTermIndexV1 *index) {
    if (!index)
        return;
    if (index->rules) {
        for (uint32_t position = 0u; position < index->rule_len; position++)
            free(index->rules[position].parameter_types);
    }
    free(index->types);
    free(index->rules);
    memset(index, 0, sizeof(*index));
}

static void ground_term_set_error(LdGroundTermContextV1 *context,
                                  const char *format, ...) {
    va_list arguments;

    if (!context || !context->error_buf || context->error_buf_size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(context->error_buf, context->error_buf_size,
                    format, arguments);
    va_end(arguments);
}

static bool ground_term_fail(LdGroundTermContextV1 *context,
                             CettaLdGroundTermV1Status status,
                             const char *message) {
    if (context) {
        context->status = status;
        ground_term_set_error(context, "%s", message);
    }
    return false;
}

static bool ground_term_take_work(LdGroundTermContextV1 *context,
                                  uint32_t depth) {
    if (!context)
        return false;
    if (depth > context->depth_limit)
        return ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT,
            "ground term exceeds the LanguageDef admission depth limit");
    if (context->remaining_work == 0u)
        return ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT,
            "ground term exhausted the LanguageDef admission work limit");
    context->remaining_work--;
    return true;
}

static const CettaLdTypeDeclV1 *ground_term_index_type(
    LdGroundTermContextV1 *context,
    const LdGroundTermIndexV1 *index,
    const CettaLdTextV1 *name,
    uint32_t depth,
    uint32_t *matches) {
    const CettaLdTypeDeclV1 *found = NULL;
    uint32_t lower = 0u;
    uint32_t upper = index ? index->type_len : 0u;

    if (matches)
        *matches = 0u;
    if (!context || !index || !name)
        return NULL;
    while (lower < upper) {
        uint32_t middle = lower + (upper - lower) / 2u;
        const CettaLdTypeDeclV1 *candidate = index->types[middle];
        if (!ground_term_take_work(context, depth))
            return NULL;
        if (ld_text_compare(candidate ? &candidate->name : NULL, name) < 0)
            lower = middle + 1u;
        else
            upper = middle;
    }
    for (uint32_t position = lower;
         position < index->type_len; position++) {
        const CettaLdTypeDeclV1 *candidate = index->types[position];
        if (!ground_term_take_work(context, depth))
            return NULL;
        if (ld_text_compare(candidate ? &candidate->name : NULL, name) != 0)
            break;
        found = candidate;
        if (matches)
            (*matches)++;
    }
    return found;
}

static bool ground_term_index_init(LdGroundTermContextV1 *context,
                                   LdGroundTermIndexV1 *index) {
    uint32_t position;

    if (!context || !context->language || !index)
        return ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT,
            "ground-term admission index has invalid inputs");
    memset(index, 0, sizeof(*index));
    index->type_len = context->language->type_len;
    index->rule_len = context->language->term_len;
    if ((index->type_len > 0u && !context->language->types) ||
        (index->rule_len > 0u && !context->language->terms))
        return ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT,
            "LanguageDef declarations are missing from ground admission");
    if (index->type_len > 0u) {
        if ((size_t)index->type_len >
            SIZE_MAX / sizeof(*index->types))
            goto resource_failure;
        index->types = malloc(
            (size_t)index->type_len * sizeof(*index->types));
        if (!index->types)
            goto resource_failure;
        for (position = 0u; position < index->type_len; position++) {
            if (!ground_term_take_work(context, 0u))
                goto failure;
            index->types[position] = &context->language->types[position];
        }
        qsort(index->types, index->type_len, sizeof(*index->types),
              ground_term_type_pointer_compare);
        for (position = 0u; position < index->type_len; position++) {
            const CettaLdTypeDeclV1 *type = index->types[position];
            if (!type || (type->name.len > 0u && !type->name.bytes)) {
                (void)ground_term_fail(
                    context, CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT,
                    "LanguageDef type declaration has invalid text");
                goto failure;
            }
            if (position > 0u &&
                ld_text_compare(
                    &index->types[position - 1u]->name,
                    &type->name) == 0) {
                (void)ground_term_fail(
                    context, CETTA_LD_GROUND_TERM_V1_AMBIGUOUS_TYPE,
                    "LanguageDef type is declared more than once");
                goto failure;
            }
        }
    }
    if (index->rule_len > 0u) {
        if ((size_t)index->rule_len >
            SIZE_MAX / sizeof(*index->rules))
            goto resource_failure;
        index->rules = calloc(
            index->rule_len, sizeof(*index->rules));
        if (!index->rules)
            goto resource_failure;
        for (position = 0u; position < index->rule_len; position++) {
            const CettaLdGrammarRuleV1 *rule =
                &context->language->terms[position];
            const CettaLdTypeDeclV1 *result_type;
            const CettaLdTypeDeclV1 **parameter_types = NULL;
            bool constructor_profile = true;
            uint32_t type_matches = 0u;
            SymbolId symbol;
            if (!ground_term_take_work(context, 0u))
                goto failure;
            if ((rule->label.len > 0u && !rule->label.bytes) ||
                (rule->category.len > 0u && !rule->category.bytes) ||
                (rule->param_len > 0u && !rule->params)) {
                (void)ground_term_fail(
                    context, CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT,
                    "LanguageDef constructor declaration is malformed");
                goto failure;
            }
            symbol = symbol_intern_bytes(
                g_symbols, rule->label.bytes, rule->label.len);
            if (symbol == SYMBOL_ID_NONE)
                goto resource_failure;
            result_type = ground_term_index_type(
                context, index, &rule->category, 0u, &type_matches);
            if (context->status != CETTA_LD_GROUND_TERM_V1_OK)
                goto failure;
            if (type_matches == 0u) {
                (void)ground_term_fail(
                    context, CETTA_LD_GROUND_TERM_V1_UNKNOWN_TYPE,
                    "LanguageDef constructor result type is not declared");
                goto failure;
            }
            if (type_matches != 1u) {
                (void)ground_term_fail(
                    context, CETTA_LD_GROUND_TERM_V1_AMBIGUOUS_TYPE,
                    "LanguageDef constructor result type is ambiguous");
                goto failure;
            }
            if (rule->param_len > 0u) {
                if ((size_t)rule->param_len >
                    SIZE_MAX / sizeof(*parameter_types))
                    goto resource_failure;
                parameter_types = calloc(
                    rule->param_len, sizeof(*parameter_types));
                if (!parameter_types)
                    goto resource_failure;
            }
            for (uint32_t parameter_index = 0u;
                 parameter_index < rule->param_len; parameter_index++) {
                const CettaLdTermParamV1 *parameter =
                    &rule->params[parameter_index];
                if (parameter->kind != CETTA_LD_PARAM_SIMPLE_V1 ||
                    parameter->type.kind != CETTA_LD_TYPE_BASE_V1) {
                    /* A partial consumer's domain is not a restriction on
                     * the whole language. Keep this declaration in selection
                     * (including ambiguity checks), but require its profile
                     * only if it is selected or reached by a codec query. */
                    constructor_profile = false;
                    continue;
                }
                type_matches = 0u;
                parameter_types[parameter_index] =
                    ground_term_index_type(
                        context, index, &parameter->type.as.base,
                        0u, &type_matches);
                if (context->status != CETTA_LD_GROUND_TERM_V1_OK) {
                    free(parameter_types);
                    goto failure;
                }
                if (type_matches != 1u) {
                    free(parameter_types);
                    (void)ground_term_fail(
                        context,
                        type_matches == 0u
                            ? CETTA_LD_GROUND_TERM_V1_UNKNOWN_TYPE
                            : CETTA_LD_GROUND_TERM_V1_AMBIGUOUS_TYPE,
                        type_matches == 0u
                            ? "LanguageDef constructor parameter type is not declared"
                            : "LanguageDef constructor parameter type is ambiguous");
                    goto failure;
                }
            }
            index->rules[position] = (LdGroundTermRuleIndexEntryV1){
                .symbol = symbol,
                .rule = rule,
                .result_type = result_type,
                .parameter_types = parameter_types,
                .constructor_profile = constructor_profile,
            };
        }
        qsort(index->rules, index->rule_len, sizeof(*index->rules),
              ground_term_rule_index_compare);
    }
    return true;

resource_failure:
    (void)ground_term_fail(
        context, CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT,
        "ground-term admission could not allocate its declaration index");
failure:
    ground_term_index_clear(index);
    return false;
}

static const CettaLdTypeDeclV1 *ground_term_find_type(
    LdGroundTermContextV1 *context,
    const CettaLdTextV1 *name,
    uint32_t depth) {
    const CettaLdTypeDeclV1 *found = NULL;
    uint32_t matches = 0u;
    uint32_t lower = 0u;
    uint32_t upper;
    uint32_t position;

    if (!context || !context->index || !name)
        return NULL;
    upper = context->index->type_len;
    while (lower < upper) {
        uint32_t middle = lower + (upper - lower) / 2u;
        const CettaLdTypeDeclV1 *candidate =
            context->index->types[middle];
        if (!ground_term_take_work(context, depth))
            return NULL;
        if (ld_text_compare(candidate ? &candidate->name : NULL, name) < 0)
            lower = middle + 1u;
        else
            upper = middle;
    }
    for (position = lower; position < context->index->type_len; position++) {
        const CettaLdTypeDeclV1 *candidate =
            context->index->types[position];
        if (!ground_term_take_work(context, depth))
            return NULL;
        if (ld_text_compare(candidate ? &candidate->name : NULL, name) != 0)
            break;
        found = candidate;
        matches++;
    }
    if (matches == 0u) {
        (void)ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_UNKNOWN_TYPE,
            "expected type is not declared by the supplied LanguageDef");
        return NULL;
    }
    if (matches != 1u) {
        (void)ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_AMBIGUOUS_TYPE,
            "expected type is declared more than once");
        return NULL;
    }
    return found;
}

static const LdGroundTermRuleIndexEntryV1 *ground_term_select_rule(
    LdGroundTermContextV1 *context,
    const CettaLdTypeDeclV1 *expected_type,
    SymbolId symbol,
    uint32_t argument_len,
    uint32_t depth) {
    const LdGroundTermRuleIndexEntryV1 *selected = NULL;
    uint32_t label_matches = 0u;
    uint32_t typed_matches = 0u;
    uint32_t lower = 0u;
    uint32_t upper;

    if (!context || !context->index || !expected_type ||
        symbol == SYMBOL_ID_NONE) {
        (void)ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT,
            "ground-term constructor selection has invalid inputs");
        return NULL;
    }
    upper = context->index->rule_len;
    while (lower < upper) {
        uint32_t middle = lower + (upper - lower) / 2u;
        SymbolId candidate_symbol =
            context->index->rules[middle].symbol;
        if (!ground_term_take_work(context, depth))
            return NULL;
        if (candidate_symbol < symbol)
            lower = middle + 1u;
        else
            upper = middle;
    }
    for (uint32_t index = lower; index < context->index->rule_len; index++) {
        const LdGroundTermRuleIndexEntryV1 *entry =
            &context->index->rules[index];
        if (!ground_term_take_work(context, depth))
            return NULL;
        if (entry->symbol != symbol)
            break;
        label_matches++;
        if (entry->rule && entry->result_type == expected_type) {
            selected = entry;
            typed_matches++;
        }
    }
    if (label_matches == 0u) {
        (void)ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_UNKNOWN_CONSTRUCTOR,
            "constructor head is not declared by the supplied LanguageDef");
        return NULL;
    }
    if (typed_matches == 0u) {
        (void)ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_TYPE_MISMATCH,
            "constructor does not produce the expected LanguageDef type");
        return NULL;
    }
    if (typed_matches != 1u) {
        (void)ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_AMBIGUOUS_CONSTRUCTOR,
            "constructor and result type do not select a unique declaration");
        return NULL;
    }
    if (!selected || !selected->rule ||
        argument_len != selected->rule->param_len) {
        (void)ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_ARITY_MISMATCH,
            "constructor arity differs from its LanguageDef declaration");
        return NULL;
    }
    if (!selected->constructor_profile) {
        (void)ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
            "selected constructor requires a binding or non-base parameter codec");
        return NULL;
    }
    return selected;
}

static const LdGroundTermRuleIndexEntryV1 *ground_term_select_rule_text(
    LdGroundTermContextV1 *context,
    const CettaLdTypeDeclV1 *expected_type,
    const CettaLdTextV1 *label,
    uint32_t argument_len,
    uint32_t depth) {
    const LdGroundTermRuleIndexEntryV1 *selected = NULL;
    uint32_t label_matches = 0u;
    uint32_t typed_matches = 0u;

    if (!context || !context->index || !expected_type || !label ||
        (label->len > 0u && !label->bytes)) {
        (void)ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT,
            "ground-term constructor text selection has invalid inputs");
        return NULL;
    }
    for (uint32_t index = 0u; index < context->index->rule_len; index++) {
        const LdGroundTermRuleIndexEntryV1 *entry =
            &context->index->rules[index];
        if (!ground_term_take_work(context, depth))
            return NULL;
        if (!entry->rule ||
            ld_text_compare(&entry->rule->label, label) != 0)
            continue;
        label_matches++;
        if (entry->result_type == expected_type) {
            selected = entry;
            typed_matches++;
        }
    }
    if (label_matches == 0u) {
        (void)ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_UNKNOWN_CONSTRUCTOR,
            "constructor head is not declared by the supplied LanguageDef");
        return NULL;
    }
    if (typed_matches == 0u) {
        (void)ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_TYPE_MISMATCH,
            "constructor does not produce the expected LanguageDef type");
        return NULL;
    }
    if (typed_matches != 1u) {
        (void)ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_AMBIGUOUS_CONSTRUCTOR,
            "constructor and result type do not select a unique declaration");
        return NULL;
    }
    if (!selected || !selected->rule ||
        argument_len != selected->rule->param_len) {
        (void)ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_ARITY_MISMATCH,
            "constructor arity differs from its LanguageDef declaration");
        return NULL;
    }
    if (!selected->constructor_profile) {
        (void)ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
            "selected constructor requires a binding or non-base parameter codec");
        return NULL;
    }
    return selected;
}

static bool ground_term_admit_iterative(
    LdGroundTermContextV1 *context,
    const CettaLdTypeDeclV1 *expected_type,
    const Atom *term) {
    LdGroundTermAdmissionStackV1 stack = {0};
    bool accepted = false;

    if (!ground_term_admission_stack_push(
            context, &stack, expected_type, term, 0u))
        goto done;
    while (stack.len > 0u) {
        LdGroundTermAdmissionFrameV1 *frame =
            &stack.items[stack.len - 1u];

        if (!frame->entered) {
            const Atom *head;

            if (!ground_term_take_work(context, frame->depth))
                goto done;
            if (!frame->term) {
                (void)ground_term_fail(
                    context, CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT,
                    "ground term contains a null child");
                goto done;
            }
            if (frame->term->kind == ATOM_VAR) {
                (void)ground_term_fail(
                    context, CETTA_LD_GROUND_TERM_V1_NON_GROUND_TERM,
                    "MeTTa variables are not ground LanguageDef constructor terms");
                goto done;
            }
            if (!frame->expected_type) {
                (void)ground_term_fail(
                    context, CETTA_LD_GROUND_TERM_V1_UNKNOWN_TYPE,
                    "expected type is not declared by the supplied LanguageDef");
                goto done;
            }
            if (ground_term_memo_contains(
                    context, frame->term, frame->expected_type)) {
                stack.len--;
                continue;
            }
            /* A built-in carrier adds grounded leaves; it does not remove
             * the language's declared constructors at that sort. */
            if (frame->term->kind == ATOM_EXPR && frame->term->expr.len &&
                frame->term->expr.elems && frame->term->expr.elems[0] &&
                frame->term->expr.elems[0]->kind == ATOM_SYMBOL) {
                if (frame->term->expr.len - 1u > UINT32_MAX) {
                    (void)ground_term_fail(
                        context, CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT,
                        "constructor arity exceeds the LanguageDef wire limit");
                    goto done;
                }
                head = frame->term->expr.elems[0];
                frame->selected = ground_term_select_rule(
                    context, frame->expected_type, head->sym_id,
                    (uint32_t)(frame->term->expr.len - 1u),
                    frame->depth);
                if (!frame->selected)
                    goto done;
                frame->entered = true;
                continue;
            }
            switch (frame->expected_type->carrier) {
            case CETTA_LD_CARRIER_AST_V1:
                (void)ground_term_fail(
                    context, CETTA_LD_GROUND_TERM_V1_TYPE_MISMATCH,
                    "CarrierAst values require an expression with a constructor head");
                goto done;
            case CETTA_LD_CARRIER_BUILTIN_INT_V1:
                if (frame->term->kind != ATOM_GROUNDED ||
                    frame->term->ground.gkind != GV_INT) {
                    (void)ground_term_fail(
                        context, CETTA_LD_GROUND_TERM_V1_TYPE_MISMATCH,
                        "term is not a grounded integer");
                    goto done;
                }
                break;
            case CETTA_LD_CARRIER_BUILTIN_STRING_V1:
                if (frame->term->kind != ATOM_GROUNDED ||
                    frame->term->ground.gkind != GV_STRING ||
                    !frame->term->ground.sval) {
                    (void)ground_term_fail(
                        context, CETTA_LD_GROUND_TERM_V1_TYPE_MISMATCH,
                        "term is not a grounded string");
                    goto done;
                }
                break;
            case CETTA_LD_CARRIER_BUILTIN_BOOL_V1:
                if (frame->term->kind != ATOM_GROUNDED ||
                    frame->term->ground.gkind != GV_BOOL) {
                    (void)ground_term_fail(
                        context, CETTA_LD_GROUND_TERM_V1_TYPE_MISMATCH,
                        "term is not a grounded Boolean");
                    goto done;
                }
                break;
            case CETTA_LD_CARRIER_TOKEN_LABEL_V1:
            case CETTA_LD_CARRIER_TOKEN_RAW_V1:
            case CETTA_LD_CARRIER_TOKEN_PROOF_V1:
            case CETTA_LD_CARRIER_TOKEN_PATH_V1:
                (void)ground_term_fail(
                    context, CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
                    "token carriers require an explicit token admission profile");
                goto done;
            default:
                (void)ground_term_fail(
                    context, CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
                    "unknown LanguageDef carrier profile");
                goto done;
            }
            if (!ground_term_memo_insert(
                    context, frame->term, frame->expected_type))
                goto done;
            stack.len--;
            continue;
        }
        if (frame->next_child < frame->selected->rule->param_len) {
            const CettaLdTypeDeclV1 *child_type;
            const Atom *child_term;
            uint32_t child_index = frame->next_child++;
            uint32_t child_depth;

            if (frame->depth == UINT32_MAX) {
                (void)ground_term_fail(
                    context, CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT,
                    "ground term depth cannot be represented");
                goto done;
            }
            child_depth = frame->depth + 1u;
            child_type = frame->selected->parameter_types[child_index];
            child_term = frame->term->expr.elems[
                (CettaExprIndex)child_index + 1u];
            if (!ground_term_admission_stack_push(
                    context, &stack, child_type, child_term, child_depth))
                goto done;
            continue;
        }
        if (!ground_term_memo_insert(
                context, frame->term, frame->expected_type))
            goto done;
        stack.len--;
    }
    accepted = true;

done:
    free(stack.items);
    return accepted;
}

static bool ground_term_text_copy(LdGroundTermContextV1 *context,
                                  CettaLdTextV1 *out,
                                  const uint8_t *bytes,
                                  uint32_t len) {
    CettaLdTextV1 result = {0};

    if (!out || (len > 0u && !bytes))
        return ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT,
            "ground-term Pattern text has invalid storage");
    if (len > 0u) {
        result.bytes = malloc(len);
        if (!result.bytes)
            return ground_term_fail(
                context, CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT,
                "ground-term Pattern text allocation failed");
        memcpy(result.bytes, bytes, len);
    }
    result.len = len;
    *out = result;
    return true;
}

static bool ground_term_pattern_leaf(LdGroundTermContextV1 *context,
                                     CettaLdPatternV1 *out,
                                     const uint8_t *head,
                                     uint32_t head_len) {
    CettaLdPatternV1 result;

    if (!out)
        return ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT,
            "ground-term Pattern output is null");
    cetta_ld_pattern_v1_init(&result);
    result.kind = CETTA_LD_PATTERN_APPLY_V1;
    if (!ground_term_text_copy(
            context, &result.as.apply.head, head, head_len)) {
        cetta_ld_pattern_v1_free(&result);
        return false;
    }
    *out = result;
    return true;
}

static bool ground_term_to_pattern_at(
    LdGroundTermContextV1 *context,
    const CettaLdTypeDeclV1 *expected_type,
    const Atom *term,
    uint32_t depth,
    CettaLdPatternV1 *out) {
    CettaLdPatternV1 result;

    cetta_ld_pattern_v1_init(&result);
    if (!context || !expected_type || !term || !out ||
        !ground_term_take_work(context, depth))
        return false;
    if (term->kind == ATOM_VAR)
        return ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_NON_GROUND_TERM,
            "MeTTa variables are not ground LanguageDef constructor terms");
    switch (expected_type->carrier) {
    case CETTA_LD_CARRIER_AST_V1: {
        const LdGroundTermRuleIndexEntryV1 *selected;
        const Atom *head;
        uint32_t argument_len;

        if (term->kind != ATOM_EXPR || term->expr.len == 0u ||
            !term->expr.elems || !term->expr.elems[0] ||
            term->expr.elems[0]->kind != ATOM_SYMBOL)
            return ground_term_fail(
                context, CETTA_LD_GROUND_TERM_V1_TYPE_MISMATCH,
                "CarrierAst values require an expression with a constructor head");
        if (term->expr.len - 1u > UINT32_MAX)
            return ground_term_fail(
                context, CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT,
                "constructor arity exceeds the LanguageDef wire limit");
        head = term->expr.elems[0];
        argument_len = (uint32_t)(term->expr.len - 1u);
        selected = ground_term_select_rule(
            context, expected_type, head->sym_id, argument_len, depth);
        if (!selected)
            return false;
        result.kind = CETTA_LD_PATTERN_APPLY_V1;
        if (!ground_term_text_copy(
                context, &result.as.apply.head,
                selected->rule->label.bytes,
                selected->rule->label.len))
            goto fail;
        result.as.apply.arguments.len = argument_len;
        if (argument_len > 0u) {
            if ((size_t)argument_len >
                SIZE_MAX / sizeof(*result.as.apply.arguments.items)) {
                (void)ground_term_fail(
                    context, CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT,
                    "constructor argument list exceeds allocation limits");
                goto fail;
            }
            result.as.apply.arguments.items = calloc(
                argument_len, sizeof(*result.as.apply.arguments.items));
            if (!result.as.apply.arguments.items) {
                (void)ground_term_fail(
                    context, CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT,
                    "constructor Pattern argument allocation failed");
                goto fail;
            }
        }
        for (uint32_t index = 0u; index < argument_len; index++) {
            if (depth == UINT32_MAX) {
                (void)ground_term_fail(
                    context, CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT,
                    "ground term depth cannot be represented");
                goto fail;
            }
            if (!ground_term_to_pattern_at(
                    context, selected->parameter_types[index],
                    term->expr.elems[(CettaExprIndex)index + 1u],
                    depth + 1u,
                    &result.as.apply.arguments.items[index]))
                goto fail;
        }
        break;
    }
    case CETTA_LD_CARRIER_BUILTIN_STRING_V1: {
        const char *value;
        size_t len;

        if (term->kind != ATOM_GROUNDED ||
            term->ground.gkind != GV_STRING || !term->ground.sval)
            return ground_term_fail(
                context, CETTA_LD_GROUND_TERM_V1_TYPE_MISMATCH,
                "term is not a grounded string");
        value = term->ground.sval;
        len = atom_string_len(term);
        if (len > UINT32_MAX)
            return ground_term_fail(
                context, CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT,
                "grounded string exceeds the Pattern wire limit");
        if (!ground_term_pattern_leaf(
                context, &result, (const uint8_t *)value,
                (uint32_t)len))
            return false;
        break;
    }
    case CETTA_LD_CARRIER_BUILTIN_INT_V1: {
        char decimal[32];
        int len;

        if (term->kind != ATOM_GROUNDED ||
            term->ground.gkind != GV_INT)
            return ground_term_fail(
                context, CETTA_LD_GROUND_TERM_V1_TYPE_MISMATCH,
                "term is not a grounded integer");
        len = snprintf(decimal, sizeof(decimal), "%" PRId64,
                       term->ground.ival);
        if (len <= 0 || (size_t)len >= sizeof(decimal))
            return ground_term_fail(
                context, CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT,
                "grounded integer could not be encoded canonically");
        if (!ground_term_pattern_leaf(
                context, &result, (const uint8_t *)decimal,
                (uint32_t)len))
            return false;
        break;
    }
    case CETTA_LD_CARRIER_BUILTIN_BOOL_V1:
        return ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
            "Boolean atoms are not in the Lean Pattern carrier profile");
    case CETTA_LD_CARRIER_TOKEN_LABEL_V1:
    case CETTA_LD_CARRIER_TOKEN_RAW_V1:
    case CETTA_LD_CARRIER_TOKEN_PROOF_V1:
    case CETTA_LD_CARRIER_TOKEN_PATH_V1:
        return ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
            "token carriers require an explicit token Pattern profile");
    default:
        return ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
            "unknown LanguageDef carrier profile");
    }
    *out = result;
    return true;

fail:
    cetta_ld_pattern_v1_free(&result);
    return false;
}

static bool ground_term_pattern_is_leaf(
    const CettaLdPatternV1 *pattern) {
    return pattern && pattern->kind == CETTA_LD_PATTERN_APPLY_V1 &&
        pattern->as.apply.arguments.len == 0u &&
        (pattern->as.apply.head.len == 0u ||
         pattern->as.apply.head.bytes != NULL);
}

static bool ground_term_pattern_parse_int64(
    LdGroundTermContextV1 *context,
    const CettaLdTextV1 *text,
    int64_t *out) {
    char input[32];
    char canonical[32];
    char *end = NULL;
    intmax_t parsed;
    int canonical_len;

    if (!text || !out || text->len == 0u ||
        text->len >= sizeof(input) || !text->bytes)
        return ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_NONCANONICAL_BUILTIN,
            "integer Pattern atom is outside the canonical int64 image");
    if (memchr(text->bytes, '\0', text->len))
        return ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_NONCANONICAL_BUILTIN,
            "integer Pattern atom contains an embedded NUL byte");
    memcpy(input, text->bytes, text->len);
    input[text->len] = '\0';
    errno = 0;
    parsed = strtoimax(input, &end, 10);
    if (errno == ERANGE || !end || *end != '\0' ||
        parsed < INT64_MIN || parsed > INT64_MAX)
        return ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_NONCANONICAL_BUILTIN,
            "integer Pattern atom is outside the canonical int64 image");
    canonical_len = snprintf(
        canonical, sizeof(canonical), "%" PRId64, (int64_t)parsed);
    if (canonical_len <= 0 || (size_t)canonical_len != text->len ||
        memcmp(canonical, text->bytes, text->len) != 0)
        return ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_NONCANONICAL_BUILTIN,
            "integer Pattern atom is not canonical decimal int64 text");
    *out = (int64_t)parsed;
    return true;
}

static Atom *ground_term_from_pattern_at(
    LdGroundTermContextV1 *context,
    Arena *arena,
    const CettaLdTypeDeclV1 *expected_type,
    const CettaLdPatternV1 *pattern,
    uint32_t depth) {
    if (!context || !arena || !expected_type || !pattern ||
        !ground_term_take_work(context, depth))
        return NULL;
    if (pattern->kind == CETTA_LD_PATTERN_FVAR_V1) {
        (void)ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_NON_GROUND_TERM,
            "free Pattern variables cannot become ground guest terms");
        return NULL;
    }
    if (pattern->kind != CETTA_LD_PATTERN_APPLY_V1) {
        (void)ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
            "ground guest terms support only PApp Pattern values");
        return NULL;
    }
    if ((pattern->as.apply.head.len > 0u &&
         !pattern->as.apply.head.bytes) ||
        (pattern->as.apply.arguments.len > 0u &&
         !pattern->as.apply.arguments.items)) {
        (void)ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT,
            "Pattern application has invalid storage");
        return NULL;
    }
    switch (expected_type->carrier) {
    case CETTA_LD_CARRIER_AST_V1: {
        const LdGroundTermRuleIndexEntryV1 *selected;
        Atom **elements;
        Atom *result;
        SymbolId symbol;

        selected = ground_term_select_rule_text(
            context, expected_type, &pattern->as.apply.head,
            pattern->as.apply.arguments.len, depth);
        if (!selected)
            return NULL;
        symbol = selected->symbol;
        if ((size_t)selected->rule->param_len + 1u >
            SIZE_MAX / sizeof(*elements)) {
            (void)ground_term_fail(
                context, CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT,
                "guest constructor argument vector is too large");
            return NULL;
        }
        elements = malloc(
            ((size_t)selected->rule->param_len + 1u) *
            sizeof(*elements));
        if (!elements) {
            (void)ground_term_fail(
                context, CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT,
                "guest constructor argument allocation failed");
            return NULL;
        }
        elements[0] = atom_symbol_id(arena, symbol);
        if (!elements[0]) {
            free(elements);
            (void)ground_term_fail(
                context, CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT,
                "guest constructor head allocation failed");
            return NULL;
        }
        for (uint32_t index = 0u;
             index < selected->rule->param_len; index++) {
            if (depth == UINT32_MAX) {
                free(elements);
                (void)ground_term_fail(
                    context, CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT,
                    "Pattern term depth cannot be represented");
                return NULL;
            }
            elements[index + 1u] = ground_term_from_pattern_at(
                context, arena, selected->parameter_types[index],
                &pattern->as.apply.arguments.items[index], depth + 1u);
            if (!elements[index + 1u]) {
                free(elements);
                return NULL;
            }
        }
        result = atom_expr(
            arena, elements,
            (CettaExprLen)selected->rule->param_len + 1u);
        free(elements);
        if (!result)
            (void)ground_term_fail(
                context, CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT,
                "guest constructor allocation failed");
        return result;
    }
    case CETTA_LD_CARRIER_BUILTIN_STRING_V1: {
        Atom *result;

        if (!ground_term_pattern_is_leaf(pattern)) {
            (void)ground_term_fail(
                context, CETTA_LD_GROUND_TERM_V1_TYPE_MISMATCH,
                "String carrier requires a nullary Pattern atom");
            return NULL;
        }
        result = atom_string_n(arena, (const char *)pattern->as.apply.head.bytes,
                               pattern->as.apply.head.len);
        if (!result)
            (void)ground_term_fail(
                context, CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT,
                "grounded string allocation failed");
        return result;
    }
    case CETTA_LD_CARRIER_BUILTIN_INT_V1: {
        int64_t value = 0;
        Atom *result;

        if (!ground_term_pattern_is_leaf(pattern)) {
            (void)ground_term_fail(
                context, CETTA_LD_GROUND_TERM_V1_TYPE_MISMATCH,
                "Integer carrier requires a nullary Pattern atom");
            return NULL;
        }
        if (!ground_term_pattern_parse_int64(
                context, &pattern->as.apply.head, &value))
            return NULL;
        result = atom_int(arena, value);
        if (!result)
            (void)ground_term_fail(
                context, CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT,
                "grounded integer allocation failed");
        return result;
    }
    case CETTA_LD_CARRIER_BUILTIN_BOOL_V1:
        (void)ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
            "Boolean atoms are not in the Lean Pattern carrier profile");
        return NULL;
    case CETTA_LD_CARRIER_TOKEN_LABEL_V1:
    case CETTA_LD_CARRIER_TOKEN_RAW_V1:
    case CETTA_LD_CARRIER_TOKEN_PROOF_V1:
    case CETTA_LD_CARRIER_TOKEN_PATH_V1:
        (void)ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
            "token carriers require an explicit token Pattern profile");
        return NULL;
    default:
        (void)ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
            "unknown LanguageDef carrier profile");
        return NULL;
    }
}

static bool ground_term_type_supports_pattern_codec(
    LdGroundTermContextV1 *context,
    const CettaLdTypeDeclV1 *root_type) {
    const CettaLdTypeDeclV1 **pending = NULL;
    bool *visited = NULL;
    uint32_t pending_len = 0u;
    uint32_t cursor = 0u;
    bool supported = false;

    if (!context || !context->language || !context->index || !root_type)
        return ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT,
            "typed Pattern codec received an invalid type");
    if (context->language->type_len == 0u ||
        !context->language->types ||
        root_type < context->language->types ||
        root_type >= context->language->types + context->language->type_len)
        return ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT,
            "typed Pattern codec root is outside the LanguageDef type table");
    if ((size_t)context->language->type_len >
        SIZE_MAX / sizeof(*pending))
        goto resource_failure;
    pending = malloc(
        (size_t)context->language->type_len * sizeof(*pending));
    visited = calloc(context->language->type_len, sizeof(*visited));
    if (!pending || !visited)
        goto resource_failure;
    pending[pending_len++] = root_type;
    visited[(uint32_t)(root_type - context->language->types)] = true;

    while (cursor < pending_len) {
        const CettaLdTypeDeclV1 *type = pending[cursor++];
        if (!ground_term_take_work(context, 0u))
            goto done;
        switch (type->carrier) {
        case CETTA_LD_CARRIER_AST_V1:
            break;
        case CETTA_LD_CARRIER_BUILTIN_STRING_V1:
        case CETTA_LD_CARRIER_BUILTIN_INT_V1:
            break;
        case CETTA_LD_CARRIER_BUILTIN_BOOL_V1:
            (void)ground_term_fail(
                context, CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
                "Boolean atoms are not in the Lean Pattern carrier profile");
            goto done;
        case CETTA_LD_CARRIER_TOKEN_LABEL_V1:
        case CETTA_LD_CARRIER_TOKEN_RAW_V1:
        case CETTA_LD_CARRIER_TOKEN_PROOF_V1:
        case CETTA_LD_CARRIER_TOKEN_PATH_V1:
            (void)ground_term_fail(
                context, CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
                "token carriers require an explicit token Pattern profile");
            goto done;
        default:
            (void)ground_term_fail(
                context, CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
                "unknown LanguageDef carrier profile");
            goto done;
        }
        for (uint32_t rule_index = 0u;
             rule_index < context->index->rule_len; rule_index++) {
            const LdGroundTermRuleIndexEntryV1 *rule =
                &context->index->rules[rule_index];
            if (!ground_term_take_work(context, 0u))
                goto done;
            if (rule->result_type != type)
                continue;
            /* Admission permits constructors in builtin carriers too. The
             * untagged leaf codec cannot distinguish a nullary constructor
             * from a string with the same text; do not claim a full codec. */
            if (type->carrier != CETTA_LD_CARRIER_AST_V1) {
                (void)ground_term_fail(
                    context, CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
                    "builtin-plus-constructor carrier needs a tagged Pattern interpretation");
                goto done;
            }
            if (!rule->constructor_profile) {
                (void)ground_term_fail(
                    context, CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
                    "reachable constructor requires a binding or non-base parameter codec");
                goto done;
            }
            for (uint32_t parameter_index = 0u;
                 parameter_index < rule->rule->param_len;
                 parameter_index++) {
                const CettaLdTypeDeclV1 *parameter_type =
                    rule->parameter_types[parameter_index];
                uint32_t type_index = (uint32_t)(
                    parameter_type - context->language->types);
                if (!visited[type_index]) {
                    visited[type_index] = true;
                    pending[pending_len++] = parameter_type;
                }
            }
        }
    }
    supported = true;
    goto done;

resource_failure:
    (void)ground_term_fail(
        context, CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT,
        "typed Pattern codec type-closure allocation failed");
done:
    free(visited);
    free(pending);
    return supported;
}

bool cetta_language_def_ground_term_v1_supports_pattern_codec(
    const CettaLanguageDefCoreV1 *language,
    const char *expected_type,
    uint64_t work_limit,
    CettaLdGroundTermV1Status *status,
    char *error_buf,
    size_t error_buf_size) {
    LdGroundTermContextV1 context;
    LdGroundTermIndexV1 index;
    CettaLdTextV1 expected;
    const CettaLdTypeDeclV1 *root_type;
    size_t expected_len;
    bool supported;

    if (status)
        *status = CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT;
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!language || !expected_type || !status || !g_symbols ||
        work_limit == 0u) {
        if (error_buf && error_buf_size > 0u) {
            (void)snprintf(
                error_buf, error_buf_size,
                "invalid typed Pattern codec profile arguments");
        }
        return false;
    }
    expected_len = strlen(expected_type);
    if (expected_len == 0u || expected_len > UINT32_MAX) {
        if (error_buf && error_buf_size > 0u) {
            (void)snprintf(error_buf, error_buf_size,
                           "expected type name is empty or too large");
        }
        return false;
    }
    expected.bytes = (uint8_t *)(uintptr_t)expected_type;
    expected.len = (uint32_t)expected_len;
    memset(&context, 0, sizeof(context));
    context.language = language;
    context.depth_limit = 0u;
    context.remaining_work = work_limit;
    context.status = CETTA_LD_GROUND_TERM_V1_OK;
    context.error_buf = error_buf;
    context.error_buf_size = error_buf_size;
    memset(&index, 0, sizeof(index));
    if (!ground_term_index_init(&context, &index)) {
        *status = context.status;
        return false;
    }
    context.index = &index;
    root_type = ground_term_find_type(&context, &expected, 0u);
    supported = root_type &&
        ground_term_type_supports_pattern_codec(&context, root_type);
    if (supported)
        context.status = CETTA_LD_GROUND_TERM_V1_OK;
    *status = context.status;
    ground_term_index_clear(&index);
    return supported;
}

bool cetta_language_def_ground_term_v1_admit(
    const CettaLanguageDefCoreV1 *language,
    const char *expected_type,
    const Atom *term,
    uint32_t depth_limit,
    uint64_t work_limit,
    CettaLdGroundTermV1Status *status,
    char *error_buf,
    size_t error_buf_size) {
    LdGroundTermContextV1 context;
    LdGroundTermIndexV1 index;
    CettaLdTextV1 expected;
    const CettaLdTypeDeclV1 *root_type;
    size_t expected_len;
    bool accepted;

    if (status)
        *status = CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT;
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!language || !expected_type || !term || !status || !g_symbols ||
        work_limit == 0u) {
        if (error_buf && error_buf_size > 0u) {
            (void)snprintf(error_buf, error_buf_size,
                           "invalid ground-term admission arguments");
        }
        return false;
    }
    expected_len = strlen(expected_type);
    if (expected_len == 0u || expected_len > UINT32_MAX) {
        if (error_buf && error_buf_size > 0u) {
            (void)snprintf(error_buf, error_buf_size,
                           "expected type name is empty or too large");
        }
        return false;
    }
    expected.bytes = (uint8_t *)(uintptr_t)expected_type;
    expected.len = (uint32_t)expected_len;
    memset(&context, 0, sizeof(context));
    context.language = language;
    context.depth_limit = depth_limit;
    context.remaining_work = work_limit;
    context.status = CETTA_LD_GROUND_TERM_V1_OK;
    context.error_buf = error_buf;
    context.error_buf_size = error_buf_size;
    memset(&index, 0, sizeof(index));
    if (!ground_term_index_init(&context, &index)) {
        *status = context.status;
        return false;
    }
    context.index = &index;
    root_type = ground_term_find_type(&context, &expected, 0u);
    accepted = root_type &&
        ground_term_admit_iterative(&context, root_type, term);
    if (accepted)
        context.status = CETTA_LD_GROUND_TERM_V1_OK;
    *status = context.status;
    ground_term_memo_clear(&context.memo);
    ground_term_index_clear(&index);
    return accepted;
}

bool cetta_language_def_ground_term_v1_to_pattern(
    const CettaLanguageDefCoreV1 *language,
    const char *expected_type,
    const Atom *term,
    CettaLdPatternV1 *out,
    uint32_t depth_limit,
    uint64_t work_limit,
    CettaLdGroundTermV1Status *status,
    char *error_buf,
    size_t error_buf_size) {
    LdGroundTermContextV1 context;
    LdGroundTermIndexV1 index;
    CettaLdPatternV1 result;
    CettaLdTextV1 expected;
    const CettaLdTypeDeclV1 *root_type;
    size_t expected_len;
    bool converted;

    if (status)
        *status = CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT;
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!language || !expected_type || !term || !out || !status ||
        !g_symbols || work_limit == 0u) {
        if (error_buf && error_buf_size > 0u) {
            (void)snprintf(
                error_buf, error_buf_size,
                "invalid ground-term to Pattern conversion arguments");
        }
        return false;
    }
    expected_len = strlen(expected_type);
    if (expected_len == 0u || expected_len > UINT32_MAX) {
        if (error_buf && error_buf_size > 0u) {
            (void)snprintf(error_buf, error_buf_size,
                           "expected type name is empty or too large");
        }
        return false;
    }
    expected.bytes = (uint8_t *)(uintptr_t)expected_type;
    expected.len = (uint32_t)expected_len;
    memset(&context, 0, sizeof(context));
    context.language = language;
    context.depth_limit = depth_limit;
    context.remaining_work = work_limit;
    context.status = CETTA_LD_GROUND_TERM_V1_OK;
    context.error_buf = error_buf;
    context.error_buf_size = error_buf_size;
    memset(&index, 0, sizeof(index));
    cetta_ld_pattern_v1_init(&result);
    if (!ground_term_index_init(&context, &index)) {
        *status = context.status;
        return false;
    }
    context.index = &index;
    root_type = ground_term_find_type(&context, &expected, 0u);
    converted = root_type &&
        ground_term_to_pattern_at(
            &context, root_type, term, 0u, &result);
    if (converted) {
        context.status = CETTA_LD_GROUND_TERM_V1_OK;
        cetta_ld_pattern_v1_free(out);
        *out = result;
    } else {
        cetta_ld_pattern_v1_free(&result);
    }
    *status = context.status;
    ground_term_memo_clear(&context.memo);
    ground_term_index_clear(&index);
    return converted;
}

Atom *cetta_language_def_ground_term_v1_from_pattern(
    Arena *arena,
    const CettaLanguageDefCoreV1 *language,
    const char *expected_type,
    const CettaLdPatternV1 *pattern,
    uint32_t depth_limit,
    uint64_t work_limit,
    CettaLdGroundTermV1Status *status,
    char *error_buf,
    size_t error_buf_size) {
    LdGroundTermContextV1 context;
    LdGroundTermIndexV1 index;
    CettaLdTextV1 expected;
    const CettaLdTypeDeclV1 *root_type;
    Atom *result;
    size_t expected_len;

    if (status)
        *status = CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT;
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!arena || !language || !expected_type || !pattern || !status ||
        !g_symbols || work_limit == 0u) {
        if (error_buf && error_buf_size > 0u) {
            (void)snprintf(
                error_buf, error_buf_size,
                "invalid Pattern to ground-term conversion arguments");
        }
        return NULL;
    }
    expected_len = strlen(expected_type);
    if (expected_len == 0u || expected_len > UINT32_MAX) {
        if (error_buf && error_buf_size > 0u) {
            (void)snprintf(error_buf, error_buf_size,
                           "expected type name is empty or too large");
        }
        return NULL;
    }
    expected.bytes = (uint8_t *)(uintptr_t)expected_type;
    expected.len = (uint32_t)expected_len;
    memset(&context, 0, sizeof(context));
    context.language = language;
    context.depth_limit = depth_limit;
    context.remaining_work = work_limit;
    context.status = CETTA_LD_GROUND_TERM_V1_OK;
    context.error_buf = error_buf;
    context.error_buf_size = error_buf_size;
    memset(&index, 0, sizeof(index));
    if (!ground_term_index_init(&context, &index)) {
        *status = context.status;
        return NULL;
    }
    context.index = &index;
    root_type = ground_term_find_type(&context, &expected, 0u);
    result = root_type
        ? ground_term_from_pattern_at(
            &context, arena, root_type, pattern, 0u)
        : NULL;
    if (result)
        context.status = CETTA_LD_GROUND_TERM_V1_OK;
    *status = context.status;
    ground_term_memo_clear(&context.memo);
    ground_term_index_clear(&index);
    return result;
}

typedef struct {
    const Atom *ast;
    const CettaLdTypeDeclV1 *type;
    const LdGroundTermRuleIndexEntryV1 *rule;
    Atom *term, *span, *position;
    uint32_t depth, first_child, child_len, parent, slot;
} LdAstNodeV1;

static bool ground_ast_form(const Atom *value, const char *head, uint32_t arity) {
    return value && value->kind == ATOM_EXPR && value->expr.len == arity + 1u &&
        atom_is_symbol(value->expr.elems[0], head);
}

static bool ground_ast_string(const Atom *value) {
    return value && value->kind == ATOM_GROUNDED &&
        value->ground.gkind == GV_STRING && value->ground.sval;
}

static Atom *ground_ast_span(Arena *arena, const Atom *span) {
    if (!ground_ast_form(span, "LbnfSourceSpanV1", 4u)) return NULL;
    Atom *elements[5] = {atom_symbol(arena, "LbnfSourceSpanV1")};
    for (uint32_t i = 1u; i <= 4u; ++i) {
        const Atom *n = span->expr.elems[i];
        if (!n || n->kind != ATOM_GROUNDED || n->ground.gkind != GV_INT ||
            n->ground.ival < 0) return NULL;
        elements[i] = atom_int(arena, n->ground.ival);
    }
    if (elements[2]->ground.ival < elements[1]->ground.ival ||
        elements[4]->ground.ival < elements[3]->ground.ival) return NULL;
    return atom_expr(arena, elements, 5u);
}

static bool ground_ast_contains(const Atom *parent, const Atom *child) {
    return parent->expr.elems[1]->ground.ival <= child->expr.elems[1]->ground.ival &&
        child->expr.elems[2]->ground.ival <= parent->expr.elems[2]->ground.ival &&
        parent->expr.elems[3]->ground.ival <= child->expr.elems[3]->ground.ival &&
        child->expr.elems[4]->ground.ival <= parent->expr.elems[4]->ground.ival;
}

static bool ground_ast_text(LdGroundTermContextV1 *context, const Atom *value,
                            uint32_t depth, uint8_t **out, size_t *out_len) {
    uint8_t *bytes = NULL;
    size_t len = 0u, cap = 0u;
    const Atom *cursor = value;
    while (ground_ast_form(cursor, "bnf-v1:text-cons", 2u)) {
        if (!ground_term_take_work(context, depth)) goto failed;
        const Atom *point = cursor->expr.elems[1];
        if (!point || point->kind != ATOM_GROUNDED || point->ground.gkind != GV_INT ||
            point->ground.ival <= 0 || point->ground.ival > 0x10ffff ||
            (point->ground.ival >= 0xd800 && point->ground.ival <= 0xdfff)) {
            (void)ground_term_fail(context, CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
                "token payload is outside the NUL-free builtin string carrier");
            goto failed;
        }
        if (!cetta_utf8_append_scalar_impl_v1(&bytes, &len, &cap, point->ground.ival)) {
            (void)ground_term_fail(context, CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT,
                "could not allocate token text projection");
            goto failed;
        }
        cursor = cursor->expr.elems[2];
    }
    if (!ground_ast_form(cursor, "bnf-v1:text-nil", 0u)) {
        (void)ground_term_fail(context, CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT,
            "parser token payload is not finite scalar text");
        goto failed;
    }
    if (!bytes) {
        bytes = calloc(1u, 1u);
        if (!bytes) {
            (void)ground_term_fail(context, CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT,
                "could not allocate empty token text");
            goto failed;
        }
    }
    *out = bytes; *out_len = len;
    return true;
failed:
    free(bytes);
    return false;
}

static Atom *ground_ast_builtin(LdGroundTermContextV1 *context, Arena *arena,
                                LdAstNodeV1 *node) {
    uint8_t *bytes = NULL;
    size_t len = 0u;
    Atom *result = NULL;
    CettaLdCarrierKindV1 carrier = node->type->carrier;
    if (carrier != CETTA_LD_CARRIER_BUILTIN_INT_V1 &&
        carrier != CETTA_LD_CARRIER_BUILTIN_STRING_V1 &&
        carrier != CETTA_LD_CARRIER_BUILTIN_BOOL_V1) {
        (void)ground_term_fail(context, CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
            "token carriers require an explicit token admission profile");
        return NULL;
    }
    if (!ground_ast_text(context, node->ast->expr.elems[3], node->depth, &bytes, &len))
        return NULL;
    if (carrier == CETTA_LD_CARRIER_BUILTIN_STRING_V1) result = atom_string(arena, (char *)bytes);
    else if (carrier == CETTA_LD_CARRIER_BUILTIN_BOOL_V1) {
        if (len == 4u && !memcmp(bytes, "True", 4u)) result = atom_bool(arena, true);
        else if (len == 5u && !memcmp(bytes, "False", 5u)) result = atom_bool(arena, false);
    } else {
        /* Decimal lexical spellings project to the builtin int64 value;
         * canonical decimal is required only by the Pattern codec. */
        size_t at = len && bytes[0] == '-' ? 1u : 0u;
        bool decimal = at < len;
        for (; at < len; ++at) decimal = decimal && bytes[at] >= '0' && bytes[at] <= '9';
        if (decimal) {
            char *end = NULL;
            errno = 0;
            intmax_t n = strtoimax((char *)bytes, &end, 10);
            if (errno != ERANGE && end == (char *)bytes + len && n >= INT64_MIN && n <= INT64_MAX)
                result = atom_int(arena, (int64_t)n);
        }
    }
    free(bytes);
    if (!result) (void)ground_term_fail(context, CETTA_LD_GROUND_TERM_V1_NONCANONICAL_BUILTIN,
        "token payload has no value in the declared builtin carrier");
    return result;
}

bool cetta_language_def_ground_term_v1_project_ast(
    Arena *arena, const CettaLanguageDefCoreV1 *language,
    const char *expected_type, const Atom *ast,
    uint32_t depth_limit, uint64_t work_limit,
    Atom **out_term, Atom **out_sources, const Atom **blame_span,
    CettaLdGroundTermV1Status *status, char *error_buf, size_t error_buf_size) {
    LdGroundTermContextV1 context = {0};
    LdGroundTermIndexV1 index = {0};
    LdAstNodeV1 *nodes = NULL;
    size_t len = 1u, capacity = 32u;
    bool accepted = false;
    if (status) *status = CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT;
    if (blame_span) *blame_span = NULL;
    if (error_buf && error_buf_size) error_buf[0] = '\0';
    if (!arena || !language || !expected_type || !*expected_type ||
        strlen(expected_type) > UINT32_MAX || !ast || !out_term || !out_sources ||
        !status || !g_symbols || !work_limit) return false;
    context.language = language; context.depth_limit = depth_limit;
    context.remaining_work = work_limit; context.error_buf = error_buf;
    context.error_buf_size = error_buf_size;
    if (!ground_term_index_init(&context, &index)) goto done;
    context.index = &index;
    CettaLdTextV1 expected = {.bytes = (uint8_t *)(uintptr_t)expected_type,
                              .len = (uint32_t)strlen(expected_type)};
    const CettaLdTypeDeclV1 *root_type = ground_term_find_type(&context, &expected, 0u);
    if (!root_type) goto done;
    nodes = calloc(capacity, sizeof(*nodes));
    if (!nodes) goto resource;
    nodes[0] = (LdAstNodeV1){.ast = ast, .type = root_type, .parent = UINT32_MAX, .slot = UINT32_MAX};
    /* Parents precede children. Reverse traversal constructs values without
     * C recursion; the source map keeps exact occurrence edges, not a DAG
     * quotient of equal terms. */
    for (size_t i = 0u; i < len; ++i) {
        LdAstNodeV1 *node = &nodes[i];
        bool token = ground_ast_form(node->ast, "LbnfAstTokenV1", 6u);
        bool ctor = ground_ast_form(node->ast, "LbnfAstNodeV1", 4u);
        bool list = ground_ast_form(node->ast, "LbnfAstListV1", 3u);
        if (blame_span) *blame_span = token || ctor || list
            ? node->ast->expr.elems[token ? 5u : ctor ? 3u : 2u] : NULL;
        if (!ground_term_take_work(&context, node->depth)) goto done;
        if (!token && !ctor && !list) {
            (void)ground_term_fail(&context, CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT,
                "expected a labelled parser AST node, token, or list"); goto done;
        }
        const Atom *span = node->ast->expr.elems[token ? 5u : ctor ? 3u : 2u];
        if (blame_span) *blame_span = span;
        node->span = ground_ast_span(arena, span);
        const Atom *category = node->ast->expr.elems[1];
        if (!node->span || !ground_ast_string(category)) {
            (void)ground_term_fail(&context, CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT,
                "AST category or source span is malformed"); goto done;
        }
        if (node->parent != UINT32_MAX && !ground_ast_contains(nodes[node->parent].span, node->span)) {
            (void)ground_term_fail(&context, CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT,
                "AST child span lies outside its parent span"); goto done;
        }
        CettaLdTextV1 category_text = {.bytes = (uint8_t *)category->ground.sval,
                                     .len = category->ground.slen};
        if (ld_text_compare(&category_text, &node->type->name)) {
            (void)ground_term_fail(&context, CETTA_LD_GROUND_TERM_V1_TYPE_MISMATCH,
                "AST category differs from the selected LanguageDef type"); goto done;
        }
        Atom *no_position[] = {atom_symbol(arena, "LbnfNoTokenPositionV1")};
        node->position = atom_expr(arena, no_position, 1u);
        if (token) {
            const Atom *position = node->ast->expr.elems[4];
            if (ground_ast_form(position, "LbnfTokenPositionV1", 2u)) {
                Atom *elements[3] = {atom_symbol(arena, "LbnfTokenPositionV1")};
                for (uint32_t p = 1u; p <= 2u; ++p) {
                    const Atom *n = position->expr.elems[p];
                    if (!n || n->kind != ATOM_GROUNDED || n->ground.gkind != GV_INT || n->ground.ival < 1) {
                        (void)ground_term_fail(&context, CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT,
                            "AST token position is malformed"); goto done;
                    }
                    elements[p] = atom_int(arena, n->ground.ival);
                }
                node->position = atom_expr(arena, elements, 3u);
            } else if (!atom_is_symbol((Atom *)position, "LbnfNoTokenPositionV1") &&
                       !ground_ast_form(position, "LbnfNoTokenPositionV1", 0u)) {
                (void)ground_term_fail(&context, CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT,
                    "AST token position is malformed"); goto done;
            }
            node->term = ground_ast_builtin(&context, arena, node);
            if (!node->term) goto done;
            continue;
        }
        if (list || node->type->carrier != CETTA_LD_CARRIER_AST_V1) {
            (void)ground_term_fail(&context, CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
                "AST list or constructor requires a declared ground-carrier projection"); goto done;
        }
        const Atom *label = node->ast->expr.elems[2];
        if (!ground_ast_string(label)) {
            (void)ground_term_fail(&context, CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT,
                "AST constructor label is malformed"); goto done;
        }
        const Atom *children = node->ast->expr.elems[4], *cursor = children;
        uint32_t count = 0u;
        while (ground_ast_form(cursor, "LCons", 2u)) {
            if (!ground_term_take_work(&context, node->depth)) goto done;
            if (count == UINT32_MAX) goto resource;
            ++count; cursor = cursor->expr.elems[2];
        }
        if (!atom_is_symbol((Atom *)cursor, "LNil")) {
            (void)ground_term_fail(&context, CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT,
                "AST constructor fields are not a finite list"); goto done;
        }
        node->rule = ground_term_select_rule(&context, node->type,
            symbol_intern_bytes(g_symbols, (uint8_t *)label->ground.sval, label->ground.slen), count, node->depth);
        if (!node->rule) goto done;
        if (count && node->depth == UINT32_MAX) goto resource;
        if (len > UINT32_MAX - count || len + count > SIZE_MAX / sizeof(*nodes)) goto resource;
        if (len + count > capacity) {
            size_t next = capacity;
            while (next < len + count) {
                if (next > SIZE_MAX / 2u) goto resource;
                next *= 2u;
            }
            if (next > SIZE_MAX / sizeof(*nodes)) goto resource;
            LdAstNodeV1 *grown = realloc(nodes, next * sizeof(*nodes));
            if (!grown) goto resource;
            nodes = grown; capacity = next; node = &nodes[i];
        }
        node->first_child = (uint32_t)len; node->child_len = count;
        cursor = children;
        for (uint32_t c = 0u; c < count; ++c) {
            nodes[len++] = (LdAstNodeV1){.ast = cursor->expr.elems[1],
                .type = node->rule->parameter_types[c], .depth = node->depth + 1u,
                .parent = (uint32_t)i, .slot = c};
            cursor = cursor->expr.elems[2];
        }
    }
    Atom **sources = arena_alloc(arena, len * sizeof(*sources));
    if (!sources) goto resource;
    for (size_t i = len; i-- > 0u;) {
        LdAstNodeV1 *node = &nodes[i];
        if (blame_span) *blame_span = node->span;
        if (!ground_term_take_work(&context, node->depth)) goto done;
        if (node->rule) {
            size_t n = (size_t)node->child_len + 1u;
            Atom **elements = arena_alloc(arena, n * sizeof(*elements));
            if (!elements) goto resource;
            elements[0] = atom_symbol_id(arena, node->rule->symbol);
            for (uint32_t c = 0u; c < node->child_len; ++c) elements[c + 1u] = nodes[node->first_child + c].term;
            node->term = atom_expr(arena, elements, (CettaExprLen)n);
            if (!node->term) goto resource;
        }
        Atom *row[] = {atom_symbol(arena, "LangDef:TermSourceV1"), atom_int(arena, (int64_t)i),
            atom_int(arena, node->parent == UINT32_MAX ? -1 : (int64_t)node->parent),
            atom_int(arena, node->slot == UINT32_MAX ? -1 : (int64_t)node->slot), node->span, node->position};
        sources[i] = atom_expr(arena, row, 6u);
        if (!sources[i]) goto resource;
    }
    Atom *source_map = atom_expr(arena, sources, (CettaExprLen)len);
    if (!source_map) goto resource;
    *out_term = nodes[0].term;
    *out_sources = source_map;
    if (blame_span) *blame_span = NULL;
    accepted = true;
    goto done;
resource:
    (void)ground_term_fail(&context, CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT,
        "AST projection exhausted its allocation or occurrence bound");
done:
    *status = context.status;
    ground_term_index_clear(&index);
    free(nodes);
    return accepted;
}

/* Named-token projection shares the AST boundary and source-map convention
 * above, but targets the existing locally nameless Pattern carrier. */
typedef struct {
    const Atom *ast;
    const CettaLdTextV1 *sort;
    const CettaLdGrammarRuleV1 *rule;
    Atom *span, *position, *term;
    Atom *binder_name, *binder_span, *binder_position;
    const CettaLdTextV1 *binder_sort;
    uint32_t parent, slot, environment, depth, first_child, child_len;
} LdNamedNodeV1;

typedef struct {
    Atom *name;
    const CettaLdTextV1 *sort;
    uint32_t outer;
} LdNamedScopeV1;

static Atom *named_form(Arena *arena, const char *head, Atom **arguments, uint32_t count) {
    Atom **items = arena_alloc(arena, ((size_t)count + 1u) * sizeof(*items));
    if (!items) return NULL;
    items[0] = atom_symbol(arena, head);
    for (uint32_t i = 0u; i < count; ++i) items[i + 1u] = arguments[i];
    return atom_expr(arena, items, count + 1u);
}

static bool named_equal(const CettaLdTextV1 *a, const CettaLdTextV1 *b) {
    return a && b && !ld_text_compare(a, b);
}

static const CettaLdTypeDeclV1 *named_type(LdGroundTermContextV1 *w, const CettaLdTextV1 *name) {
    const CettaLdTypeDeclV1 *found = NULL;
    for (uint32_t i = 0u; i < w->language->type_len; ++i) {
        if (!ground_term_take_work(w, 0u)) return NULL;
        const CettaLdTypeDeclV1 *type = &w->language->types[i];
        if (!named_equal(name, &type->name)) continue;
        if (found && found->carrier != type->carrier) {
            ground_term_fail(w, CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
                "The selected name sort has conflicting carrier interpretations"); return NULL;
        }
        found = type;
    }
    if (!found) ground_term_fail(w, CETTA_LD_GROUND_TERM_V1_UNKNOWN_TYPE,
        "The projection sort is not declared");
    return found;
}

static bool named_profile_has(LdGroundTermContextV1 *w, const Atom *profile,
                              const CettaLdTextV1 *sort, bool validate, bool *has) {
    *has = false;
    const Atom *list = profile->expr.elems[1];
    while (ground_ast_form(list, "LCons", 2u)) {
        if (!ground_term_take_work(w, 0u)) return false;
        const Atom *name = list->expr.elems[1];
        if (!ground_ast_string(name)) return ground_term_fail(w, CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT,
            "Named-token profile expects a finite list of category strings");
        CettaLdTextV1 text = {.bytes = (uint8_t *)name->ground.sval, .len = name->ground.slen};
        if (validate) {
            const CettaLdTypeDeclV1 *type = named_type(w, &text);
            if (!type) return false;
            if (type->carrier != CETTA_LD_CARRIER_TOKEN_LABEL_V1 && type->carrier != CETTA_LD_CARRIER_TOKEN_RAW_V1)
                return ground_term_fail(w, CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
                    "Named-token interpretation requires a declared raw or label token carrier");
        }
        if (named_equal(sort, &text)) *has = true;
        list = list->expr.elems[2];
    }
    return atom_is_symbol((Atom *)list, "LNil") || ground_term_fail(w, CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT,
        "Named-token profile expects a finite category list");
}

static bool named_metadata(LdGroundTermContextV1 *w, Arena *arena, const Atom *ast,
    const CettaLdTextV1 *sort, const Atom *parent, Atom **span, Atom **position, const Atom **blame) {
    bool token = ground_ast_form(ast, "LbnfAstTokenV1", 6u);
    bool ctor = ground_ast_form(ast, "LbnfAstNodeV1", 4u);
    if (!token && !ctor) return ground_term_fail(w, CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
        "This interpretation accepts labelled constructors and named tokens");
    const Atom *source_span = ast->expr.elems[token ? 5u : 3u];
    if (blame) *blame = source_span;
    *span = ground_ast_span(arena, source_span);
    const Atom *category = ast->expr.elems[1];
    if (!*span || !ground_ast_string(category)) return ground_term_fail(w, CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT,
        "Malformed AST category or span");
    CettaLdTextV1 text = {.bytes = (uint8_t *)category->ground.sval, .len = category->ground.slen};
    if (!named_equal(sort, &text)) return ground_term_fail(w, CETTA_LD_GROUND_TERM_V1_TYPE_MISMATCH,
        "AST category differs from the declared argument sort");
    if (parent && !ground_ast_contains(parent, *span)) return ground_term_fail(w, CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT,
        "AST child span lies outside its parent span");
    *position = named_form(arena, "LbnfNoTokenPositionV1", NULL, 0u);
    if (!token) return *position != NULL;
    const Atom *p = ast->expr.elems[4];
    if (atom_is_symbol((Atom *)p, "LbnfNoTokenPositionV1") || ground_ast_form(p, "LbnfNoTokenPositionV1", 0u)) return true;
    if (!ground_ast_form(p, "LbnfTokenPositionV1", 2u)) return ground_term_fail(w, CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT,
        "Malformed named-token position");
    Atom *coords[2];
    for (uint32_t i = 0u; i < 2u; ++i) {
        const Atom *n = p->expr.elems[i + 1u];
        if (!n || n->kind != ATOM_GROUNDED || n->ground.gkind != GV_INT || n->ground.ival < 1)
            return ground_term_fail(w, CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT, "Malformed named-token coordinate");
        coords[i] = atom_int(arena, n->ground.ival);
    }
    *position = named_form(arena, "LbnfTokenPositionV1", coords, 2u);
    return *position != NULL;
}

static Atom *named_token(LdGroundTermContextV1 *w, Arena *arena, const Atom *ast, uint32_t depth) {
    if (!ground_ast_form(ast, "LbnfAstTokenV1", 6u)) {
        ground_term_fail(w, CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
            "This name interpretation needs a token, not a constructor-valued spelling"); return NULL;
    }
    uint8_t *bytes = NULL; size_t length = 0u;
    if (!ground_ast_text(w, ast->expr.elems[3], depth, &bytes, &length)) return NULL;
    Atom *name = atom_string(arena, (const char *)bytes);
    free(bytes);
    return name;
}

/* Equal-valued declaration occurrences need no arbitrary selection. Different
 * signatures with one label require an occurrence-aware interpretation. */
static bool named_same_type(const CettaLdTypeExprV1 *a, const CettaLdTypeExprV1 *b) {
    if (!a || !b || a->kind != b->kind) return false;
    if (a->kind == CETTA_LD_TYPE_BASE_V1) return named_equal(&a->as.base, &b->as.base);
    return a->kind == CETTA_LD_TYPE_ARROW_V1 && a->as.arrow.domain && b->as.arrow.domain &&
        a->as.arrow.codomain && b->as.arrow.codomain &&
        a->as.arrow.domain->kind == CETTA_LD_TYPE_BASE_V1 && b->as.arrow.domain->kind == CETTA_LD_TYPE_BASE_V1 &&
        a->as.arrow.codomain->kind == CETTA_LD_TYPE_BASE_V1 && b->as.arrow.codomain->kind == CETTA_LD_TYPE_BASE_V1 &&
        named_equal(&a->as.arrow.domain->as.base, &b->as.arrow.domain->as.base) &&
        named_equal(&a->as.arrow.codomain->as.base, &b->as.arrow.codomain->as.base);
}

static const CettaLdGrammarRuleV1 *named_rule(LdGroundTermContextV1 *w,
    const CettaLdTextV1 *sort, const Atom *label) {
    if (!ground_ast_string(label)) {
        ground_term_fail(w, CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT, "Malformed AST constructor label"); return NULL;
    }
    CettaLdTextV1 name = {.bytes = (uint8_t *)label->ground.sval, .len = label->ground.slen};
    const CettaLdGrammarRuleV1 *found = NULL;
    for (uint32_t i = 0u; i < w->language->term_len; ++i) {
        if (!ground_term_take_work(w, 0u)) return NULL;
        const CettaLdGrammarRuleV1 *r = &w->language->terms[i];
        if (!named_equal(&r->category, sort) || !named_equal(&r->label, &name)) continue;
        bool same = !found || (found->param_len == r->param_len && found->syntax_pattern.len == r->syntax_pattern.len);
        for (uint32_t p = 0u; found && same && p < r->param_len; ++p) {
            if (!ground_term_take_work(w, 0u)) return NULL;
            const CettaLdTermParamV1 *a = &found->params[p], *b = &r->params[p];
            same = a->kind == b->kind && named_equal(&a->body_name, &b->body_name) && named_same_type(&a->type, &b->type);
            if (same && a->kind == CETTA_LD_PARAM_ABSTRACTION_NAMED_V1)
                same = a->names.binder.present == b->names.binder.present &&
                    (!a->names.binder.present || named_equal(&a->names.binder.value, &b->names.binder.value));
        }
        for (uint32_t p = 0u; found && same && p < r->syntax_pattern.len; ++p) {
            if (!ground_term_take_work(w, 0u)) return NULL;
            const CettaLdSyntaxItemV1 *a = &found->syntax_pattern.items[p], *b = &r->syntax_pattern.items[p];
            same = a->kind == b->kind && (a->kind != CETTA_LD_SYNTAX_NONTERMINAL_V1 || named_equal(&a->as.text, &b->as.text));
        }
        if (!same) {
            ground_term_fail(w, CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
                "One AST label has distinct projection signatures; occurrence correspondence is unresolved"); return NULL;
        }
        found = r;
    }
    if (!found) ground_term_fail(w, CETTA_LD_GROUND_TERM_V1_UNKNOWN_CONSTRUCTOR,
        "AST constructor is absent from the selected core category");
    return found;
}

static bool named_grow(void **items, size_t *capacity, size_t needed, size_t size) {
    if (needed > UINT32_MAX || needed > SIZE_MAX / size) return false;
    if (*capacity >= needed) return true;
    size_t cap = *capacity ? *capacity : 32u;
    while (cap < needed) {
        if (cap > SIZE_MAX / 2u) return false;
        cap *= 2u;
    }
    if (cap > SIZE_MAX / size) return false;
    void *grown = realloc(*items, cap * size);
    if (!grown) return false;
    *items = grown; *capacity = cap; return true;
}

bool cetta_language_def_pattern_v1_project_ast(
    Arena *arena, const CettaLanguageDefCoreV1 *language,
    const char *expected_type, const Atom *profile, const Atom *ast,
    uint32_t depth_limit, uint64_t work_limit,
    Atom **out_pattern, Atom **out_sources, const Atom **blame_span,
    CettaLdGroundTermV1Status *status, char *error_buf, size_t error_buf_size) {
    LdGroundTermContextV1 w = {.language = language, .depth_limit = depth_limit,
        .remaining_work = work_limit, .error_buf = error_buf, .error_buf_size = error_buf_size};
    LdNamedNodeV1 *nodes = NULL;
    LdNamedScopeV1 *scopes = NULL;
    size_t count = 0u, capacity = 0u, scope_count = 0u, scope_capacity = 0u;
    const Atom **fields = NULL;
    const CettaLdTextV1 **slots = NULL;
    bool *used = NULL, ok = false;
    if (status) *status = CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT;
    if (blame_span) *blame_span = NULL;
    if (error_buf && error_buf_size) *error_buf = '\0';
    if (!arena || !language || !expected_type || !*expected_type || strlen(expected_type) > UINT32_MAX ||
        !ast || !status || !out_pattern || !out_sources || !work_limit ||
        !ground_ast_form(profile, "LangDef:NamedTokensV1", 1u)) return false;
    CettaLdTextV1 root = {.bytes = (uint8_t *)(uintptr_t)expected_type, .len = (uint32_t)strlen(expected_type)};
    bool ignored;
    if (!named_type(&w, &root) || !named_profile_has(&w, profile, &root, true, &ignored)) goto done;
    if (!named_grow((void **)&nodes, &capacity, 1u, sizeof(*nodes))) goto resource;
    nodes[count++] = (LdNamedNodeV1){.ast = ast, .sort = &root, .parent = UINT32_MAX,
        .slot = UINT32_MAX, .environment = UINT32_MAX};
    for (size_t i = 0u; i < count; ++i) {
        LdNamedNodeV1 node = nodes[i];
        if (!ground_term_take_work(&w, node.depth)) goto done;
        if (!named_metadata(&w, arena, node.ast, node.sort,
                node.parent == UINT32_MAX ? NULL : nodes[node.parent].span,
                &node.span, &node.position, blame_span)) goto done;
        nodes[i].span = node.span; nodes[i].position = node.position;
        if (node.binder_name) {
            if (node.depth == UINT32_MAX || !named_grow((void **)&scopes, &scope_capacity, scope_count + 1u, sizeof(*scopes)) ||
                !named_grow((void **)&nodes, &capacity, count + 1u, sizeof(*nodes))) goto resource;
            scopes[scope_count] = (LdNamedScopeV1){node.binder_name, node.binder_sort, node.environment};
            nodes[i].first_child = (uint32_t)count; nodes[i].child_len = 1u;
            nodes[count++] = (LdNamedNodeV1){.ast = node.ast, .sort = node.sort, .parent = (uint32_t)i,
                .slot = 0u, .environment = (uint32_t)scope_count++, .depth = node.depth + 1u};
            continue;
        }
        if (ground_ast_form(node.ast, "LbnfAstTokenV1", 6u)) {
            bool names;
            if (!named_profile_has(&w, profile, node.sort, false, &names)) goto done;
            if (!names) { ground_term_fail(&w, CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
                "No name interpretation was supplied for this token category"); goto done; }
            Atom *name = named_token(&w, arena, node.ast, node.depth);
            if (!name) goto done;
            uint32_t frame = node.environment, index = 0u;
            while (frame != UINT32_MAX) {
                if (!ground_term_take_work(&w, node.depth)) goto done;
                const LdNamedScopeV1 *scope = &scopes[frame];
                if (named_equal(scope->sort, node.sort) && atom_eq(scope->name, name)) break;
                frame = scope->outer; ++index;
            }
            if (frame == UINT32_MAX) { ground_term_fail(&w, CETTA_LD_GROUND_TERM_V1_SCOPE_MISMATCH,
                "A name has no binding in its declared category namespace"); goto done; }
            Atom *number = atom_int(arena, index);
            nodes[i].term = named_form(arena, "Var", &number, 1u);
            if (!nodes[i].term) goto resource;
            continue;
        }
        const CettaLdTypeDeclV1 *type = named_type(&w, node.sort);
        if (!type) goto done;
        if (type->carrier != CETTA_LD_CARRIER_AST_V1) { ground_term_fail(&w, CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
            "Constructor projection needs an AST carrier"); goto done; }
        const CettaLdGrammarRuleV1 *rule = named_rule(&w, node.sort, node.ast->expr.elems[2]);
        if (!rule) goto done;
        nodes[i].rule = rule;
        uint32_t nslots = 0u;
        for (uint32_t j = 0u; j < rule->syntax_pattern.len; ++j) {
            if (!ground_term_take_work(&w, node.depth)) goto done;
            CettaLdSyntaxItemKindV1 kind = rule->syntax_pattern.items[j].kind;
            if (kind == CETTA_LD_SYNTAX_NONTERMINAL_V1) ++nslots;
            else if (kind != CETTA_LD_SYNTAX_TERMINAL_V1) { ground_term_fail(&w, CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
                "Structured syntax operations need their own projection correspondence"); goto done; }
        }
        fields = calloc(nslots ? nslots : 1u, sizeof(*fields));
        slots = calloc(nslots ? nslots : 1u, sizeof(*slots));
        used = calloc(nslots ? nslots : 1u, sizeof(*used));
        if (!fields || !slots || !used) goto resource;
        const Atom *cursor = node.ast->expr.elems[4];
        uint32_t field = 0u;
        for (uint32_t j = 0u; j < rule->syntax_pattern.len; ++j) {
            if (!ground_term_take_work(&w, node.depth)) goto done;
            const CettaLdSyntaxItemV1 *item = &rule->syntax_pattern.items[j];
            if (item->kind != CETTA_LD_SYNTAX_NONTERMINAL_V1) continue;
            if (!ground_ast_form(cursor, "LCons", 2u)) goto arity;
            slots[field] = &item->as.text; fields[field++] = cursor->expr.elems[1]; cursor = cursor->expr.elems[2];
        }
        if (!atom_is_symbol((Atom *)cursor, "LNil")) goto arity;
        if (node.depth == UINT32_MAX || !named_grow((void **)&nodes, &capacity, count + rule->param_len, sizeof(*nodes))) goto resource;
        nodes[i].first_child = (uint32_t)count; nodes[i].child_len = rule->param_len;
        for (uint32_t p = 0u; p < rule->param_len; ++p) {
            const CettaLdTermParamV1 *param = &rule->params[p];
            const CettaLdTypeExprV1 *body_type = &param->type, *domain = NULL;
            bool binding = param->kind == CETTA_LD_PARAM_ABSTRACTION_NAMED_V1;
            if (binding && param->type.kind == CETTA_LD_TYPE_ARROW_V1) {
                domain = param->type.as.arrow.domain; body_type = param->type.as.arrow.codomain;
            } else if (binding) domain = body_type;
            if ((!binding && param->kind != CETTA_LD_PARAM_SIMPLE_V1) || !body_type || body_type->kind != CETTA_LD_TYPE_BASE_V1 ||
                (binding && (!domain || domain->kind != CETTA_LD_TYPE_BASE_V1 || !param->names.binder.present))) {
                ground_term_fail(&w, CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
                    "This projection covers base arguments and explicitly named single binders"); goto done;
            }
            uint32_t body = UINT32_MAX, binder = UINT32_MAX;
            for (uint32_t j = 0u; j < nslots; ++j) {
                if (!ground_term_take_work(&w, node.depth)) goto done;
                if (named_equal(slots[j], &param->body_name)) {
                    if (body != UINT32_MAX) goto mapping;
                    body = j;
                }
                if (binding && named_equal(slots[j], &param->names.binder.value)) {
                    if (binder != UINT32_MAX) goto mapping;
                    binder = j;
                }
            }
            if (body == UINT32_MAX || used[body] || (binding && (binder == UINT32_MAX || binder == body))) goto mapping;
            used[body] = true;
            LdNamedNodeV1 child = {.ast = fields[body], .sort = &body_type->as.base, .parent = (uint32_t)i,
                .slot = p, .environment = node.environment, .depth = node.depth + 1u};
            if (binding) {
                bool names;
                if (!named_profile_has(&w, profile, &domain->as.base, false, &names)) goto done;
                if (!names) goto mapping;
                child.binder_sort = &domain->as.base;
                if (!named_metadata(&w, arena, fields[binder], child.binder_sort, node.span,
                        &child.binder_span, &child.binder_position, blame_span)) goto done;
                child.binder_name = named_token(&w, arena, fields[binder], node.depth);
                if (!child.binder_name) goto done;
                used[binder] = true;
            }
            nodes[count++] = child;
        }
        for (uint32_t j = 0u; j < nslots; ++j) if (!used[j]) goto mapping;
        free(fields); fields = NULL; free(slots); slots = NULL; free(used); used = NULL;
    }
    if (count + scope_count > UINT32_MAX || count + scope_count > SIZE_MAX / sizeof(Atom *)) goto resource;
    Atom **sources = arena_alloc(arena, (count + scope_count) * sizeof(*sources));
    if (!sources) goto resource;
    size_t source_count = count;
    for (size_t i = count; i-- > 0u;) {
        LdNamedNodeV1 *node = &nodes[i];
        if (!ground_term_take_work(&w, node->depth)) goto done;
        if (node->binder_name) {
            Atom *args[] = {atom_symbol(arena, "BNone"), nodes[node->first_child].term};
            node->term = named_form(arena, "PLam", args, 2u);
            Atom *location[] = {atom_int(arena, i), node->binder_name, node->binder_span, node->binder_position};
            sources[source_count++] = named_form(arena, "LangDef:BinderSourceV1", location, 4u);
        } else if (node->rule) {
            Atom *list = atom_symbol(arena, "LNil");
            for (uint32_t c = node->child_len; c-- > 0u;) {
                Atom *args[] = {nodes[node->first_child + c].term, list};
                list = named_form(arena, "LCons", args, 2u);
                if (!list) goto resource;
            }
            Atom *label = atom_deep_copy(arena, node->ast->expr.elems[2]);
            Atom *args[] = {label, list};
            node->term = named_form(arena, "PApp", args, 2u);
        }
        if (!node->term) goto resource;
        Atom *location[] = {atom_int(arena, i), atom_int(arena, node->parent == UINT32_MAX ? -1 : (int64_t)node->parent),
            atom_int(arena, node->slot == UINT32_MAX ? -1 : (int64_t)node->slot), node->span, node->position};
        sources[i] = named_form(arena, "LangDef:TermSourceV1", location, 5u);
        if (!sources[i]) goto resource;
    }
    Atom *map = atom_expr(arena, sources, (CettaExprLen)source_count);
    if (!map) goto resource;
    *out_pattern = nodes[0].term; *out_sources = map;
    if (blame_span) *blame_span = NULL;
    ok = true; goto done;
mapping:
    ground_term_fail(&w, CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
        "The declaration does not determine this profile's argument and binder-slot correspondence"); goto done;
arity:
    ground_term_fail(&w, CETTA_LD_GROUND_TERM_V1_ARITY_MISMATCH,
        "AST fields do not match the declared concrete notation"); goto done;
resource:
    ground_term_fail(&w, CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT, "Named projection allocation or occurrence limit");
done:
    free(fields); free(slots); free(used); free(nodes); free(scopes);
    if (!ok && w.status == CETTA_LD_GROUND_TERM_V1_OK)
        ground_term_fail(&w, CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT, "Could not allocate a projected Pattern value");
    *status = w.status; return ok;
}

const char *cetta_ld_ground_term_v1_status_name(
    CettaLdGroundTermV1Status status) {
    switch (status) {
    case CETTA_LD_GROUND_TERM_V1_SCOPE_MISMATCH:
        return "scope_mismatch";
    case CETTA_LD_GROUND_TERM_V1_OK:
        return "ok";
    case CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT:
        return "bad_argument";
    case CETTA_LD_GROUND_TERM_V1_UNKNOWN_TYPE:
        return "unknown_type";
    case CETTA_LD_GROUND_TERM_V1_AMBIGUOUS_TYPE:
        return "ambiguous_type";
    case CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE:
        return "unsupported_profile";
    case CETTA_LD_GROUND_TERM_V1_NON_GROUND_TERM:
        return "non_ground_term";
    case CETTA_LD_GROUND_TERM_V1_UNKNOWN_CONSTRUCTOR:
        return "unknown_constructor";
    case CETTA_LD_GROUND_TERM_V1_AMBIGUOUS_CONSTRUCTOR:
        return "ambiguous_constructor";
    case CETTA_LD_GROUND_TERM_V1_ARITY_MISMATCH:
        return "arity_mismatch";
    case CETTA_LD_GROUND_TERM_V1_TYPE_MISMATCH:
        return "type_mismatch";
    case CETTA_LD_GROUND_TERM_V1_NONCANONICAL_BUILTIN:
        return "noncanonical_builtin";
    case CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT:
        return "resource_limit";
    }
    return "unknown";
}
