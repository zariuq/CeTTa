#include "language_def_ground_term_v1.h"

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
                    free(parameter_types);
                    (void)ground_term_fail(
                        context,
                        CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
                        "ground admission supports only simple base-typed parameters");
                    goto failure;
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
            switch (frame->expected_type->carrier) {
            case CETTA_LD_CARRIER_AST_V1:
                if (frame->term->kind != ATOM_EXPR ||
                    frame->term->expr.len == 0u ||
                    !frame->term->expr.elems ||
                    !frame->term->expr.elems[0] ||
                    frame->term->expr.elems[0]->kind != ATOM_SYMBOL) {
                    (void)ground_term_fail(
                        context, CETTA_LD_GROUND_TERM_V1_TYPE_MISMATCH,
                        "CarrierAst values require an expression with a constructor head");
                    goto done;
                }
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
        len = strlen(value);
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

static bool ground_term_pattern_text_to_c_string(
    LdGroundTermContextV1 *context,
    const CettaLdTextV1 *text,
    char **out) {
    char *result;

    if (!text || !out || (text->len > 0u && !text->bytes))
        return ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT,
            "Pattern atom text has invalid storage");
    if (text->len > 0u && memchr(text->bytes, '\0', text->len))
        return ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_NONCANONICAL_BUILTIN,
            "Pattern atom text contains an embedded NUL byte");
    result = malloc((size_t)text->len + 1u);
    if (!result)
        return ground_term_fail(
            context, CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT,
            "Pattern atom text allocation failed");
    if (text->len > 0u)
        memcpy(result, text->bytes, text->len);
    result[text->len] = '\0';
    *out = result;
    return true;
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
        char *value;
        Atom *result;

        if (!ground_term_pattern_is_leaf(pattern)) {
            (void)ground_term_fail(
                context, CETTA_LD_GROUND_TERM_V1_TYPE_MISMATCH,
                "String carrier requires a nullary Pattern atom");
            return NULL;
        }
        if (!ground_term_pattern_text_to_c_string(
                context, &pattern->as.apply.head, &value))
            return NULL;
        result = atom_string(arena, value);
        free(value);
        if (!result)
            (void)ground_term_fail(
                context, CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT,
                "grounded string allocation failed");
        return result;
    }
    case CETTA_LD_CARRIER_BUILTIN_INT_V1: {
        int64_t value;
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
            continue;
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

const char *cetta_ld_ground_term_v1_status_name(
    CettaLdGroundTermV1Status status) {
    switch (status) {
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
