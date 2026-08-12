#include "inference_checker.h"
#include "inference_side_condition_provider.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "symbol.h"

typedef struct {
    SymbolId name;
    uint64_t arity;
} InferenceDecl;

typedef struct {
    SymbolId name;
    uint64_t depth;
    bool seen;
} InferenceFormal;

typedef enum {
    INFERENCE_SIDE_EXPLICIT_SUBSTITUTION = 0,
    INFERENCE_SIDE_UNUSED_BINDER_ELIMINATION = 1,
} InferenceSideConditionKind;

typedef struct {
    InferenceSideConditionKind kind;
    size_t body_argument;
    size_t replacement_argument;
    size_t result_argument;
} InferenceSideCondition;

typedef struct {
    SymbolId id;
    InferenceFormal *formals;
    size_t formal_count;
    Atom **premises;
    size_t premise_count;
    Atom *conclusion;
    InferenceSideCondition *side_conditions;
    size_t side_condition_count;
    bool active;
} InferenceRule;

typedef struct {
    bool occupied;
    SymbolId key;
    uint64_t value;
} InferenceNameSlot;

struct CettaInferenceChecker {
    Atom *presentation;
    uint32_t wire_version;
    InferenceDecl *constructors;
    size_t constructor_count;
    size_t constructor_capacity;
    InferenceNameSlot *constructor_slots;
    size_t constructor_slot_count;
    InferenceDecl *judgments;
    size_t judgment_count;
    InferenceNameSlot *judgment_slots;
    size_t judgment_slot_count;
    InferenceRule *rules;
    size_t rule_count;
    size_t rule_capacity;
    InferenceNameSlot *rule_slots;
    size_t rule_slot_count;
};

typedef enum {
    INTEGER_OK = 0,
    INTEGER_INVALID,
    INTEGER_RESOURCE_LIMIT
} IntegerResult;

static CettaInferenceStatus inference_error(
    CettaInferenceStatus status,
    char *error_buf,
    size_t error_buf_size,
    const char *format,
    ...) {
    va_list args;

    if (error_buf && error_buf_size > 0) {
        va_start(args, format);
        vsnprintf(error_buf, error_buf_size, format, args);
        va_end(args);
    }
    return status;
}

static void inference_error_clear(char *error_buf, size_t error_buf_size) {
    if (error_buf && error_buf_size > 0)
        error_buf[0] = '\0';
}

const char *cetta_inference_status_name(CettaInferenceStatus status) {
    switch (status) {
    case CETTA_INFERENCE_OK:
        return "ok";
    case CETTA_INFERENCE_INVALID_PRESENTATION:
        return "invalid-presentation";
    case CETTA_INFERENCE_MALFORMED_PROOF:
        return "malformed-proof";
    case CETTA_INFERENCE_UNKNOWN_RULE:
        return "unknown-rule";
    case CETTA_INFERENCE_INVALID_ARGUMENTS:
        return "invalid-arguments";
    case CETTA_INFERENCE_PREMISE_MISMATCH:
        return "premise-mismatch";
    case CETTA_INFERENCE_EMPTY_STACK:
        return "empty-stack";
    case CETTA_INFERENCE_BAD_REFERENCE:
        return "bad-reference";
    case CETTA_INFERENCE_FINAL_MISMATCH:
        return "final-mismatch";
    case CETTA_INFERENCE_RESOURCE_LIMIT:
        return "resource-limit";
    }
    return "unknown-status";
}

static bool atom_expr_tag(const Atom *atom, const char *tag, size_t len) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == len &&
           atom_is_symbol(atom->expr.elems[0], tag);
}

static bool atom_string_value(const Atom *atom, const char **out) {
    if (!atom || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_STRING || !atom->ground.sval)
        return false;
    *out = atom->ground.sval;
    return true;
}

static IntegerResult atom_nonnegative_u64(const Atom *atom, uint64_t *out) {
    const char *text;
    char *end = NULL;
    unsigned long long value;

    if (!atom || atom->kind != ATOM_GROUNDED)
        return INTEGER_INVALID;
    if (atom->ground.gkind == GV_INT) {
        if (atom->ground.ival < 0)
            return INTEGER_INVALID;
        *out = (uint64_t)atom->ground.ival;
        return INTEGER_OK;
    }
    if (atom->ground.gkind != GV_BIGINT)
        return INTEGER_INVALID;
    text = atom_bigint_cstr(atom);
    if (!text || !*text || *text == '-')
        return INTEGER_INVALID;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno == ERANGE)
        return INTEGER_RESOURCE_LIMIT;
    if (!end || *end != '\0')
        return INTEGER_INVALID;
    *out = (uint64_t)value;
    return INTEGER_OK;
}

static bool checked_depth_add(uint64_t left, uint64_t right,
                              uint64_t *out) {
    if (UINT64_MAX - left < right)
        return false;
    *out = left + right;
    return true;
}

static bool checked_array_size(size_t count, size_t element_size,
                               size_t *out) {
    if (element_size != 0 && count > SIZE_MAX / element_size)
        return false;
    *out = count * element_size;
    return true;
}

static bool canonical_list_length(Atom *list, size_t *out) {
    size_t length = 0;
    Atom *cursor = list;

    while (!atom_is_symbol(cursor, "LNil")) {
        if (!atom_expr_tag(cursor, "LCons", 3) || length == SIZE_MAX)
            return false;
        length++;
        cursor = cursor->expr.elems[2];
    }
    *out = length;
    return true;
}

static bool canonical_list_copy(Atom *list, Atom **items, size_t count) {
    size_t index = 0;
    Atom *cursor = list;

    while (!atom_is_symbol(cursor, "LNil")) {
        if (!atom_expr_tag(cursor, "LCons", 3) || index >= count)
            return false;
        items[index++] = cursor->expr.elems[1];
        cursor = cursor->expr.elems[2];
    }
    return index == count;
}

static size_t inference_slot_capacity(size_t count) {
    size_t needed;
    size_t capacity = 8;

    if (count > (SIZE_MAX / 2))
        return 0;
    needed = count * 2;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2)
            return 0;
        capacity *= 2;
    }
    return capacity;
}

static size_t inference_name_hash(SymbolId key) {
    uint64_t value = (uint64_t)key;
    value ^= value >> 33;
    value *= UINT64_C(0xff51afd7ed558ccd);
    value ^= value >> 33;
    return (size_t)value;
}

static bool inference_slots_insert(InferenceNameSlot *slots,
                                   size_t slot_count,
                                   SymbolId key,
                                   uint64_t value) {
    size_t mask = slot_count - 1;
    size_t index = inference_name_hash(key) & mask;

    for (;;) {
        InferenceNameSlot *slot = &slots[index];
        if (!slot->occupied) {
            slot->occupied = true;
            slot->key = key;
            slot->value = value;
            return true;
        }
        if (slot->key == key)
            return false;
        index = (index + 1) & mask;
    }
}

static bool inference_slots_find(const InferenceNameSlot *slots,
                                 size_t slot_count,
                                 SymbolId key,
                                 uint64_t *out) {
    size_t mask;
    size_t index;

    if (!slots || slot_count == 0)
        return false;
    mask = slot_count - 1;
    index = inference_name_hash(key) & mask;
    for (;;) {
        const InferenceNameSlot *slot = &slots[index];
        if (!slot->occupied)
            return false;
        if (slot->key == key) {
            if (out)
                *out = slot->value;
            return true;
        }
        index = (index + 1) & mask;
    }
}

static bool inference_declarations_reserve(InferenceDecl **decls,
                                           size_t *capacity,
                                           size_t needed) {
    size_t next;

    if (*capacity >= needed)
        return true;
    next = *capacity ? *capacity : 8;
    while (next < needed) {
        if (next > SIZE_MAX / 2)
            return false;
        next *= 2;
    }
    if (next > SIZE_MAX / sizeof(**decls))
        return false;
    *decls = cetta_realloc(*decls, sizeof(**decls) * next);
    *capacity = next;
    return true;
}

static bool inference_rules_reserve(InferenceRule **rules,
                                    size_t *capacity,
                                    size_t needed) {
    size_t next;

    if (*capacity >= needed)
        return true;
    next = *capacity ? *capacity : 8;
    while (next < needed) {
        if (next > SIZE_MAX / 2)
            return false;
        next *= 2;
    }
    if (next > SIZE_MAX / sizeof(**rules))
        return false;
    *rules = cetta_realloc(*rules, sizeof(**rules) * next);
    memset(*rules + *capacity, 0,
           sizeof(**rules) * (next - *capacity));
    *capacity = next;
    return true;
}

static bool inference_rebuild_declaration_slots(
    const InferenceDecl *decls,
    size_t count,
    size_t needed,
    InferenceNameSlot **slots,
    size_t *slot_count) {
    InferenceNameSlot *next_slots;
    size_t next_count;
    size_t bytes;
    size_t index;

    next_count = inference_slot_capacity(needed);
    if (next_count == 0)
        return false;
    if (*slot_count >= next_count)
        return true;
    if (!checked_array_size(next_count, sizeof(*next_slots), &bytes))
        return false;
    next_slots = cetta_malloc(bytes);
    memset(next_slots, 0, bytes);
    for (index = 0; index < count; index++) {
        if (!inference_slots_insert(next_slots, next_count,
                                    decls[index].name,
                                    decls[index].arity)) {
            free(next_slots);
            return false;
        }
    }
    free(*slots);
    *slots = next_slots;
    *slot_count = next_count;
    return true;
}

static bool inference_rebuild_rule_slots(
    const InferenceRule *rules,
    size_t count,
    size_t needed,
    InferenceNameSlot **slots,
    size_t *slot_count) {
    InferenceNameSlot *next_slots;
    size_t next_count;
    size_t bytes;
    size_t index;

    next_count = inference_slot_capacity(needed);
    if (next_count == 0)
        return false;
    if (*slot_count >= next_count)
        return true;
    if (!checked_array_size(next_count, sizeof(*next_slots), &bytes))
        return false;
    next_slots = cetta_malloc(bytes);
    memset(next_slots, 0, bytes);
    for (index = 0; index < count; index++) {
        if (!inference_slots_insert(next_slots, next_count,
                                    rules[index].id,
                                    (uint64_t)index)) {
            free(next_slots);
            return false;
        }
    }
    free(*slots);
    *slots = next_slots;
    *slot_count = next_count;
    return true;
}

static CettaInferenceStatus inference_load_declarations(
    Atom *list,
    const char *tag,
    InferenceDecl **decls_out,
    size_t *decl_count_out,
    InferenceNameSlot **slots_out,
    size_t *slot_count_out,
    char *error_buf,
    size_t error_buf_size) {
    size_t count;
    size_t slot_count;
    InferenceDecl *decls = NULL;
    InferenceNameSlot *slots = NULL;
    Atom *cursor = list;
    size_t index;
    size_t decl_bytes;
    size_t slot_bytes;

    if (!canonical_list_length(list, &count))
        return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                               error_buf, error_buf_size,
                               "%s declarations are not a canonical list", tag);
    slot_count = inference_slot_capacity(count);
    if (slot_count == 0)
        return inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                               error_buf, error_buf_size,
                               "%s declaration index is too large", tag);
    if (!checked_array_size(count, sizeof(*decls), &decl_bytes) ||
        !checked_array_size(slot_count, sizeof(*slots), &slot_bytes))
        return inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                               error_buf, error_buf_size,
                               "%s declaration storage is too large", tag);
    if (count > 0)
        decls = cetta_malloc(decl_bytes);
    slots = cetta_malloc(slot_bytes);
    memset(slots, 0, slot_bytes);

    for (index = 0; index < count; index++) {
        Atom *decl = cursor->expr.elems[1];
        const char *name;
        uint64_t arity;
        IntegerResult integer_result;
        SymbolId name_id;

        if (!atom_expr_tag(decl, tag, 3) ||
            !atom_string_value(decl->expr.elems[1], &name) || !*name) {
            free(slots);
            free(decls);
            return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                                   error_buf, error_buf_size,
                                   "malformed or empty %s declaration", tag);
        }
        integer_result = atom_nonnegative_u64(decl->expr.elems[2], &arity);
        if (integer_result == INTEGER_RESOURCE_LIMIT) {
            free(slots);
            free(decls);
            return inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                                   error_buf, error_buf_size,
                                   "%s arity exceeds native range", tag);
        }
        if (integer_result != INTEGER_OK) {
            free(slots);
            free(decls);
            return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                                   error_buf, error_buf_size,
                                   "%s arity is not nonnegative", tag);
        }
        name_id = symbol_intern_cstr(g_symbols, name);
        if (!inference_slots_insert(slots, slot_count, name_id, arity)) {
            free(slots);
            free(decls);
            return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                                   error_buf, error_buf_size,
                                   "duplicate %s declaration '%s'", tag, name);
        }
        decls[index].name = name_id;
        decls[index].arity = arity;
        cursor = cursor->expr.elems[2];
    }

    *decls_out = decls;
    *decl_count_out = count;
    *slots_out = slots;
    *slot_count_out = slot_count;
    return CETTA_INFERENCE_OK;
}

static InferenceFormal *inference_find_formal(InferenceRule *rule,
                                               SymbolId name) {
    size_t index;

    for (index = 0; index < rule->formal_count; index++) {
        if (rule->formals[index].name == name)
            return &rule->formals[index];
    }
    return NULL;
}

static CettaInferenceStatus inference_validate_schema_pattern(
    const CettaInferenceChecker *checker,
    InferenceRule *rule,
    Atom *pattern,
    uint64_t depth,
    char *error_buf,
    size_t error_buf_size);

static CettaInferenceStatus inference_validate_schema_list(
    const CettaInferenceChecker *checker,
    InferenceRule *rule,
    Atom *list,
    uint64_t depth,
    char *error_buf,
    size_t error_buf_size) {
    Atom *cursor = list;

    while (!atom_is_symbol(cursor, "LNil")) {
        CettaInferenceStatus status;
        if (!atom_expr_tag(cursor, "LCons", 3))
            return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                                   error_buf, error_buf_size,
                                   "pattern arguments are not a canonical list");
        status = inference_validate_schema_pattern(
            checker, rule, cursor->expr.elems[1], depth,
            error_buf, error_buf_size);
        if (status != CETTA_INFERENCE_OK)
            return status;
        cursor = cursor->expr.elems[2];
    }
    return CETTA_INFERENCE_OK;
}

static CettaInferenceStatus inference_validate_schema_pattern(
    const CettaInferenceChecker *checker,
    InferenceRule *rule,
    Atom *pattern,
    uint64_t depth,
    char *error_buf,
    size_t error_buf_size) {
    uint64_t number;
    uint64_t nested_depth;
    IntegerResult integer_result;

    if (atom_expr_tag(pattern, "Var", 2)) {
        integer_result = atom_nonnegative_u64(pattern->expr.elems[1], &number);
        if (integer_result == INTEGER_RESOURCE_LIMIT)
            return inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                                   error_buf, error_buf_size,
                                   "variable index exceeds native range");
        if (integer_result != INTEGER_OK || number >= depth)
            return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                                   error_buf, error_buf_size,
                                   "unscoped de Bruijn variable");
        return CETTA_INFERENCE_OK;
    }
    if (atom_expr_tag(pattern, "FVar", 2)) {
        const char *name;
        SymbolId name_id;
        InferenceFormal *formal;
        if (!atom_string_value(pattern->expr.elems[1], &name))
            return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                                   error_buf, error_buf_size,
                                   "schema metavariable name is not a string");
        name_id = symbol_intern_cstr(g_symbols, name);
        formal = inference_find_formal(rule, name_id);
        if (!formal || formal->depth != depth)
            return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                                   error_buf, error_buf_size,
                                   "metavariable '%s' has no formal at depth %llu",
                                   name, (unsigned long long)depth);
        formal->seen = true;
        return CETTA_INFERENCE_OK;
    }
    if (atom_expr_tag(pattern, "PApp", 3)) {
        const char *head;
        SymbolId head_id;
        uint64_t arity;
        size_t actual_arity;
        if (!atom_string_value(pattern->expr.elems[1], &head) ||
            !canonical_list_length(pattern->expr.elems[2], &actual_arity))
            return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                                   error_buf, error_buf_size,
                                   "malformed constructor application");
        head_id = symbol_intern_cstr(g_symbols, head);
        if (!inference_slots_find(checker->constructor_slots,
                                  checker->constructor_slot_count,
                                  head_id, &arity) ||
            arity != (uint64_t)actual_arity)
            return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                                   error_buf, error_buf_size,
                                   "unknown constructor or wrong arity '%s'", head);
        return inference_validate_schema_list(
            checker, rule, pattern->expr.elems[2], depth,
            error_buf, error_buf_size);
    }
    if (atom_expr_tag(pattern, "PLam", 3)) {
        if (!atom_is_symbol(pattern->expr.elems[1], "BNone") ||
            !checked_depth_add(depth, 1, &nested_depth))
            return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                                   error_buf, error_buf_size,
                                   "noncanonical or overflowing lambda binder");
        return inference_validate_schema_pattern(
            checker, rule, pattern->expr.elems[2], nested_depth,
            error_buf, error_buf_size);
    }
    if (atom_expr_tag(pattern, "PMultiLam", 4)) {
        if (!atom_is_symbol(pattern->expr.elems[2], "LNil"))
            return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                                   error_buf, error_buf_size,
                                   "named multi-binder is not canonical");
        integer_result = atom_nonnegative_u64(pattern->expr.elems[1], &number);
        if (integer_result == INTEGER_RESOURCE_LIMIT ||
            (integer_result == INTEGER_OK &&
             !checked_depth_add(depth, number, &nested_depth)))
            return inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                                   error_buf, error_buf_size,
                                   "multi-binder depth exceeds native range");
        if (integer_result != INTEGER_OK)
            return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                                   error_buf, error_buf_size,
                                   "multi-binder arity is not nonnegative");
        return inference_validate_schema_pattern(
            checker, rule, pattern->expr.elems[3], nested_depth,
            error_buf, error_buf_size);
    }
    if (atom_expr_tag(pattern, "PSubst", 3)) {
        CettaInferenceStatus status;
        if (!checked_depth_add(depth, 1, &nested_depth))
            return inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                                   error_buf, error_buf_size,
                                   "substitution depth exceeds native range");
        status = inference_validate_schema_pattern(
            checker, rule, pattern->expr.elems[1], nested_depth,
            error_buf, error_buf_size);
        if (status != CETTA_INFERENCE_OK)
            return status;
        return inference_validate_schema_pattern(
            checker, rule, pattern->expr.elems[2], depth,
            error_buf, error_buf_size);
    }
    if (atom_expr_tag(pattern, "PCollection", 4)) {
        if (!cetta_inference_pattern_collection_type_valid_v1(
                pattern->expr.elems[1]) ||
            !atom_is_symbol(pattern->expr.elems[3], "RNone"))
            return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                                   error_buf, error_buf_size,
                                   "collection metadata is not canonical");
        return inference_validate_schema_list(
            checker, rule, pattern->expr.elems[2], depth,
            error_buf, error_buf_size);
    }
    return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                           error_buf, error_buf_size,
                           "unknown or malformed schema pattern");
}

static CettaInferenceStatus inference_validate_judgment(
    const CettaInferenceChecker *checker,
    InferenceRule *rule,
    Atom *judgment,
    char *error_buf,
    size_t error_buf_size) {
    const char *head;
    SymbolId head_id;
    uint64_t arity;
    size_t actual_arity;

    if (!atom_expr_tag(judgment, "PApp", 3) ||
        !atom_string_value(judgment->expr.elems[1], &head) ||
        !canonical_list_length(judgment->expr.elems[2], &actual_arity))
        return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                               error_buf, error_buf_size,
                               "rule judgment is not a canonical application");
    head_id = symbol_intern_cstr(g_symbols, head);
    if (!inference_slots_find(checker->judgment_slots,
                              checker->judgment_slot_count,
                              head_id, &arity) ||
        arity != (uint64_t)actual_arity)
        return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                               error_buf, error_buf_size,
                               "unknown judgment or wrong arity '%s'", head);
    return inference_validate_schema_list(
        checker, rule, judgment->expr.elems[2], 0,
        error_buf, error_buf_size);
}

static CettaInferenceStatus inference_load_formals(
    Atom *list,
    InferenceRule *rule,
    char *error_buf,
    size_t error_buf_size) {
    size_t count;
    Atom *cursor = list;
    size_t index;
    size_t formal_bytes;

    if (!canonical_list_length(list, &count))
        return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                               error_buf, error_buf_size,
                               "rule formals are not a canonical list");
    if (!checked_array_size(count, sizeof(*rule->formals), &formal_bytes))
        return inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                               error_buf, error_buf_size,
                               "rule formal storage is too large");
    if (count > 0)
        rule->formals = cetta_malloc(formal_bytes);
    rule->formal_count = count;
    for (index = 0; index < count; index++) {
        Atom *formal_atom = cursor->expr.elems[1];
        const char *name;
        SymbolId name_id;
        uint64_t depth;
        IntegerResult integer_result;
        size_t previous;

        if (!atom_expr_tag(formal_atom, "Formal", 3) ||
            !atom_string_value(formal_atom->expr.elems[1], &name) || !*name)
            return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                                   error_buf, error_buf_size,
                                   "malformed or empty rule formal");
        integer_result = atom_nonnegative_u64(formal_atom->expr.elems[2],
                                              &depth);
        if (integer_result == INTEGER_RESOURCE_LIMIT)
            return inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                                   error_buf, error_buf_size,
                                   "formal depth exceeds native range");
        if (integer_result != INTEGER_OK)
            return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                                   error_buf, error_buf_size,
                                   "formal depth is not nonnegative");
        name_id = symbol_intern_cstr(g_symbols, name);
        for (previous = 0; previous < index; previous++) {
            if (rule->formals[previous].name == name_id)
                return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                                       error_buf, error_buf_size,
                                       "duplicate rule formal '%s'", name);
        }
        rule->formals[index].name = name_id;
        rule->formals[index].depth = depth;
        rule->formals[index].seen = false;
        cursor = cursor->expr.elems[2];
    }
    return CETTA_INFERENCE_OK;
}

static CettaInferenceStatus inference_load_side_conditions(
    Atom *list,
    InferenceRule *rule,
    char *error_buf,
    size_t error_buf_size) {
    size_t count;
    size_t bytes;
    Atom *cursor = list;

    if (!canonical_list_length(list, &count))
        return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                               error_buf, error_buf_size,
                               "rule side conditions are not a canonical list");
    if (!checked_array_size(count, sizeof(*rule->side_conditions), &bytes))
        return inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                               error_buf, error_buf_size,
                               "rule side-condition storage is too large");
    if (count > 0u)
        rule->side_conditions = cetta_malloc(bytes);
    rule->side_condition_count = count;

    for (size_t index = 0u; index < count; index++) {
        Atom *condition = cursor->expr.elems[1];
        InferenceSideCondition *loaded = &rule->side_conditions[index];
        uint64_t ambient_depth;
        uint64_t body_argument;
        uint64_t replacement_argument = 0u;
        uint64_t result_argument;
        uint64_t body_depth;
        IntegerResult parsed;

        memset(loaded, 0, sizeof(*loaded));
        if (atom_expr_tag(condition, "GExplicitSubstitution", 5u)) {
            loaded->kind = INFERENCE_SIDE_EXPLICIT_SUBSTITUTION;
            parsed = atom_nonnegative_u64(
                condition->expr.elems[1], &ambient_depth);
            if (parsed == INTEGER_OK)
                parsed = atom_nonnegative_u64(
                    condition->expr.elems[2], &body_argument);
            if (parsed == INTEGER_OK)
                parsed = atom_nonnegative_u64(
                    condition->expr.elems[3], &replacement_argument);
            if (parsed == INTEGER_OK)
                parsed = atom_nonnegative_u64(
                    condition->expr.elems[4], &result_argument);
        } else if (atom_expr_tag(
                       condition, "GUnusedBinderElimination", 4u)) {
            loaded->kind = INFERENCE_SIDE_UNUSED_BINDER_ELIMINATION;
            parsed = atom_nonnegative_u64(
                condition->expr.elems[1], &ambient_depth);
            if (parsed == INTEGER_OK)
                parsed = atom_nonnegative_u64(
                    condition->expr.elems[2], &body_argument);
            if (parsed == INTEGER_OK)
                parsed = atom_nonnegative_u64(
                    condition->expr.elems[3], &result_argument);
        } else {
            return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                                   error_buf, error_buf_size,
                                   "unknown or malformed rule side condition");
        }
        if (parsed == INTEGER_RESOURCE_LIMIT)
            return inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                                   error_buf, error_buf_size,
                                   "rule side condition exceeds native range");
        if (parsed != INTEGER_OK)
            return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                                   error_buf, error_buf_size,
                                   "rule side-condition fields are not nonnegative");
        if (body_argument > SIZE_MAX || replacement_argument > SIZE_MAX ||
            result_argument > SIZE_MAX ||
            !checked_depth_add(ambient_depth, 1u, &body_depth))
            return inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                                   error_buf, error_buf_size,
                                   "rule side condition exceeds native range");
        loaded->body_argument = (size_t)body_argument;
        loaded->replacement_argument = (size_t)replacement_argument;
        loaded->result_argument = (size_t)result_argument;
        if (loaded->body_argument >= rule->formal_count ||
            loaded->result_argument >= rule->formal_count ||
            (loaded->kind == INFERENCE_SIDE_EXPLICIT_SUBSTITUTION &&
             loaded->replacement_argument >= rule->formal_count))
            return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                                   error_buf, error_buf_size,
                                   "rule side condition references a missing formal");
        if (rule->formals[loaded->body_argument].depth != body_depth ||
            rule->formals[loaded->result_argument].depth != ambient_depth ||
            (loaded->kind == INFERENCE_SIDE_EXPLICIT_SUBSTITUTION &&
             rule->formals[loaded->replacement_argument].depth !=
                 ambient_depth))
            return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                                   error_buf, error_buf_size,
                                   "rule side condition has inconsistent support depths");
        cursor = cursor->expr.elems[2];
    }
    return CETTA_INFERENCE_OK;
}

static CettaInferenceStatus inference_load_rule(
    CettaInferenceChecker *checker,
    InferenceRule *rule,
    Atom *rule_atom,
    char *error_buf,
    size_t error_buf_size) {
    const char *id;
    size_t premise_count;
    size_t index;
    size_t premise_bytes;
    CettaInferenceStatus status;

    const bool version_one = checker->wire_version == 1u;
    if (!(version_one
              ? atom_expr_tag(rule_atom, "GRuleV1", 6)
              : atom_expr_tag(rule_atom, "GRule", 5)) ||
        !atom_string_value(rule_atom->expr.elems[1], &id) || !*id)
        return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                               error_buf, error_buf_size,
                               "malformed or empty inference rule");
    rule->id = symbol_intern_cstr(g_symbols, id);
    rule->active = true;
    status = inference_load_formals(rule_atom->expr.elems[2], rule,
                                    error_buf, error_buf_size);
    if (status != CETTA_INFERENCE_OK)
        return status;
    if (version_one) {
        status = inference_load_side_conditions(
            rule_atom->expr.elems[5], rule, error_buf, error_buf_size);
        if (status != CETTA_INFERENCE_OK)
            return status;
    }
    if (!canonical_list_length(rule_atom->expr.elems[3], &premise_count))
        return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                               error_buf, error_buf_size,
                               "rule premises are not a canonical list");
    if (!checked_array_size(premise_count, sizeof(*rule->premises),
                            &premise_bytes))
        return inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                               error_buf, error_buf_size,
                               "rule premise storage is too large");
    if (premise_count > 0)
        rule->premises = cetta_malloc(premise_bytes);
    rule->premise_count = premise_count;
    if (!canonical_list_copy(rule_atom->expr.elems[3], rule->premises,
                             premise_count))
        return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                               error_buf, error_buf_size,
                               "rule premises changed during admission");
    rule->conclusion = rule_atom->expr.elems[4];

    for (index = 0; index < premise_count; index++) {
        status = inference_validate_judgment(
            checker, rule, rule->premises[index], error_buf, error_buf_size);
        if (status != CETTA_INFERENCE_OK)
            return status;
    }
    status = inference_validate_judgment(
        checker, rule, rule->conclusion, error_buf, error_buf_size);
    if (status != CETTA_INFERENCE_OK)
        return status;
    for (index = 0; index < rule->formal_count; index++) {
        if (!rule->formals[index].seen)
            return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                                   error_buf, error_buf_size,
                                   "rule has an unused formal");
    }
    return CETTA_INFERENCE_OK;
}

static void inference_rule_free(InferenceRule *rule) {
    if (!rule)
        return;
    free(rule->formals);
    free(rule->premises);
    free(rule->side_conditions);
    memset(rule, 0, sizeof(*rule));
}

void cetta_inference_checker_destroy(CettaInferenceChecker *checker) {
    size_t index;

    if (!checker)
        return;
    for (index = 0; index < checker->rule_count; index++)
        inference_rule_free(&checker->rules[index]);
    free(checker->rules);
    free(checker->rule_slots);
    free(checker->constructors);
    free(checker->constructor_slots);
    free(checker->judgments);
    free(checker->judgment_slots);
    free(checker);
}

CettaInferenceStatus cetta_inference_checker_create(
    Atom *presentation,
    CettaInferenceChecker **out,
    char *error_buf,
    size_t error_buf_size) {
    CettaInferenceChecker *checker;
    CettaInferenceStatus status;
    Atom *rule_cursor;
    size_t rule_count;
    size_t rule_slot_count;
    size_t index;
    size_t rule_bytes;
    size_t rule_slot_bytes;
    uint64_t unused;
    uint64_t wire_version = 0u;
    size_t constructor_index = 1u;
    size_t judgment_index = 2u;
    size_t rule_index = 3u;
    size_t conversion_index = 0u;
    const char *reserved[] = {"$zip", "$map", "$eval"};

    if (out)
        *out = NULL;
    inference_error_clear(error_buf, error_buf_size);
    if (!out || !g_symbols)
        return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                               error_buf, error_buf_size,
                               "checker output or symbol table is unavailable");
    if (atom_expr_tag(presentation, "GPresentationV1", 6)) {
        IntegerResult version_result = atom_nonnegative_u64(
            presentation->expr.elems[1], &wire_version);
        if (version_result == INTEGER_RESOURCE_LIMIT)
            return inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                                   error_buf, error_buf_size,
                                   "presentation version exceeds native range");
        if (version_result != INTEGER_OK || wire_version != 1u)
            return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                                   error_buf, error_buf_size,
                                   "unsupported inference presentation version");
        constructor_index = 2u;
        judgment_index = 3u;
        rule_index = 4u;
        conversion_index = 5u;
    } else if (!atom_expr_tag(presentation, "GPresentation", 4)) {
        return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                               error_buf, error_buf_size,
                               "expected GPresentationV1");
    }

    checker = cetta_malloc(sizeof(*checker));
    memset(checker, 0, sizeof(*checker));
    checker->presentation = presentation;
    checker->wire_version = (uint32_t)wire_version;

    status = inference_load_declarations(
        presentation->expr.elems[constructor_index], "CDecl",
        &checker->constructors, &checker->constructor_count,
        &checker->constructor_slots, &checker->constructor_slot_count,
        error_buf, error_buf_size);
    if (status != CETTA_INFERENCE_OK)
        goto fail;
    checker->constructor_capacity = checker->constructor_count;
    status = inference_load_declarations(
        presentation->expr.elems[judgment_index], "JDecl",
        &checker->judgments, &checker->judgment_count,
        &checker->judgment_slots, &checker->judgment_slot_count,
        error_buf, error_buf_size);
    if (status != CETTA_INFERENCE_OK)
        goto fail;

    for (index = 0; index < checker->judgment_count; index++) {
        SymbolId name = checker->judgments[index].name;
        size_t reserved_index;
        if (inference_slots_find(checker->constructor_slots,
                                 checker->constructor_slot_count,
                                 name, &unused)) {
            status = inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                                     error_buf, error_buf_size,
                                     "constructor and judgment signatures overlap");
            goto fail;
        }
        for (reserved_index = 0;
             reserved_index < sizeof(reserved) / sizeof(reserved[0]);
             reserved_index++) {
            if (name == symbol_intern_cstr(g_symbols,
                                           reserved[reserved_index])) {
                status = inference_error(
                    CETTA_INFERENCE_INVALID_PRESENTATION,
                    error_buf, error_buf_size,
                    "reserved judgment head '%s'", reserved[reserved_index]);
                goto fail;
            }
        }
    }

    if (wire_version == 1u) {
        Atom *conversion = presentation->expr.elems[conversion_index];
        if (!atom_is_symbol(conversion, "GNoConversion")) {
            const char *judgment_head;
            const char *conversion_revision;
            SymbolId judgment_id;
            uint64_t judgment_arity;
            if (!atom_expr_tag(conversion, "GConversion", 3) ||
                !atom_string_value(conversion->expr.elems[1],
                                   &judgment_head) ||
                !*judgment_head ||
                !atom_string_value(conversion->expr.elems[2],
                                   &conversion_revision) ||
                !*conversion_revision) {
                status = inference_error(
                    CETTA_INFERENCE_INVALID_PRESENTATION,
                    error_buf, error_buf_size,
                    "malformed conversion declaration");
                goto fail;
            }
            judgment_id = symbol_intern_cstr(g_symbols, judgment_head);
            if (!inference_slots_find(
                    checker->judgment_slots,
                    checker->judgment_slot_count,
                    judgment_id, &judgment_arity) ||
                judgment_arity != 2u) {
                status = inference_error(
                    CETTA_INFERENCE_INVALID_PRESENTATION,
                    error_buf, error_buf_size,
                    "conversion declaration does not name a binary judgment");
                goto fail;
            }
        }
    }

    if (!canonical_list_length(
            presentation->expr.elems[rule_index], &rule_count)) {
        status = inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                                 error_buf, error_buf_size,
                                 "rules are not a canonical list");
        goto fail;
    }
    if (rule_count > UINT32_MAX) {
        status = inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                                 error_buf, error_buf_size,
                                 "rule count exceeds native handle range");
        goto fail;
    }
    rule_slot_count = inference_slot_capacity(rule_count);
    if (rule_slot_count == 0) {
        status = inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                                 error_buf, error_buf_size,
                                 "rule index is too large");
        goto fail;
    }
    if (!checked_array_size(rule_count, sizeof(*checker->rules),
                            &rule_bytes) ||
        !checked_array_size(rule_slot_count, sizeof(*checker->rule_slots),
                            &rule_slot_bytes)) {
        status = inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                                 error_buf, error_buf_size,
                                 "rule storage is too large");
        goto fail;
    }
    if (rule_count > 0) {
        checker->rules = cetta_malloc(rule_bytes);
        memset(checker->rules, 0, rule_bytes);
    }
    checker->rule_count = rule_count;
    checker->rule_capacity = rule_count;
    checker->rule_slots = cetta_malloc(rule_slot_bytes);
    memset(checker->rule_slots, 0, rule_slot_bytes);
    checker->rule_slot_count = rule_slot_count;

    rule_cursor = presentation->expr.elems[rule_index];
    for (index = 0; index < rule_count; index++) {
        status = inference_load_rule(
            checker, &checker->rules[index], rule_cursor->expr.elems[1],
            error_buf, error_buf_size);
        if (status != CETTA_INFERENCE_OK)
            goto fail;
        if (!inference_slots_insert(checker->rule_slots,
                                    checker->rule_slot_count,
                                    checker->rules[index].id,
                                    (uint64_t)index)) {
            status = inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                                     error_buf, error_buf_size,
                                     "duplicate inference rule identifier");
            goto fail;
        }
        rule_cursor = rule_cursor->expr.elems[2];
    }

    *out = checker;
    return CETTA_INFERENCE_OK;

fail:
    cetta_inference_checker_destroy(checker);
    return status;
}

size_t cetta_inference_checker_rule_count(
    const CettaInferenceChecker *checker) {
    return checker ? checker->rule_count : 0;
}

CettaInferenceStatus cetta_inference_checker_add_constructor(
    CettaInferenceChecker *checker,
    const char *name,
    uint64_t arity,
    char *error_buf,
    size_t error_buf_size) {
    SymbolId name_id;
    uint64_t unused;
    size_t next_count;

    inference_error_clear(error_buf, error_buf_size);
    if (!checker || !name || !*name || !g_symbols)
        return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                               error_buf, error_buf_size,
                               "checker or constructor name is unavailable");
    name_id = symbol_intern_cstr(g_symbols, name);
    if (inference_slots_find(checker->constructor_slots,
                             checker->constructor_slot_count,
                             name_id, &unused))
        return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                               error_buf, error_buf_size,
                               "duplicate constructor declaration '%s'", name);
    if (inference_slots_find(checker->judgment_slots,
                             checker->judgment_slot_count,
                             name_id, &unused))
        return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                               error_buf, error_buf_size,
                               "constructor and judgment signatures overlap");
    if (checker->constructor_count == SIZE_MAX)
        return inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                               error_buf, error_buf_size,
                               "constructor count exceeds native range");
    next_count = checker->constructor_count + 1;
    if (!inference_declarations_reserve(&checker->constructors,
                                        &checker->constructor_capacity,
                                        next_count) ||
        !inference_rebuild_declaration_slots(
            checker->constructors, checker->constructor_count, next_count,
            &checker->constructor_slots, &checker->constructor_slot_count))
        return inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                               error_buf, error_buf_size,
                               "constructor storage exceeds native range");

    checker->constructors[checker->constructor_count].name = name_id;
    checker->constructors[checker->constructor_count].arity = arity;
    if (!inference_slots_insert(checker->constructor_slots,
                                checker->constructor_slot_count,
                                name_id, arity))
        return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                               error_buf, error_buf_size,
                               "duplicate constructor declaration '%s'", name);
    checker->constructor_count = next_count;
    return CETTA_INFERENCE_OK;
}

CettaInferenceStatus cetta_inference_checker_add_rule(
    CettaInferenceChecker *checker,
    Atom *rule_atom,
    CettaInferenceRuleHandle *out,
    char *error_buf,
    size_t error_buf_size) {
    InferenceRule rule;
    const char *id;
    uint64_t unused;
    size_t next_count;
    CettaInferenceStatus status;

    if (out)
        *out = CETTA_INFERENCE_RULE_HANDLE_NONE;
    inference_error_clear(error_buf, error_buf_size);
    if (!checker || !out || !g_symbols)
        return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                               error_buf, error_buf_size,
                               "checker output or symbol table is unavailable");
    if (!atom_expr_tag(rule_atom, "GRule", 5) ||
        !atom_string_value(rule_atom->expr.elems[1], &id) || !*id)
        return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                               error_buf, error_buf_size,
                               "malformed or empty inference rule");
    if (inference_slots_find(checker->rule_slots,
                             checker->rule_slot_count,
                             symbol_intern_cstr(g_symbols, id), &unused))
        return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                               error_buf, error_buf_size,
                               "duplicate inference rule identifier '%s'", id);
    if (checker->rule_count >= UINT32_MAX ||
        checker->rule_count == SIZE_MAX)
        return inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                               error_buf, error_buf_size,
                               "rule count exceeds native handle range");

    memset(&rule, 0, sizeof(rule));
    status = inference_load_rule(checker, &rule, rule_atom,
                                 error_buf, error_buf_size);
    if (status != CETTA_INFERENCE_OK) {
        inference_rule_free(&rule);
        return status;
    }
    next_count = checker->rule_count + 1;
    if (!inference_rules_reserve(&checker->rules,
                                 &checker->rule_capacity, next_count) ||
        !inference_rebuild_rule_slots(
            checker->rules, checker->rule_count, next_count,
            &checker->rule_slots, &checker->rule_slot_count)) {
        inference_rule_free(&rule);
        return inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                               error_buf, error_buf_size,
                               "rule storage exceeds native range");
    }
    checker->rules[checker->rule_count] = rule;
    if (!inference_slots_insert(checker->rule_slots,
                                checker->rule_slot_count,
                                rule.id,
                                (uint64_t)checker->rule_count)) {
        memset(&checker->rules[checker->rule_count], 0, sizeof(rule));
        inference_rule_free(&rule);
        return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                               error_buf, error_buf_size,
                               "duplicate inference rule identifier '%s'", id);
    }
    *out = (CettaInferenceRuleHandle)checker->rule_count;
    checker->rule_count = next_count;
    return CETTA_INFERENCE_OK;
}

CettaInferenceStatus cetta_inference_checker_set_rule_active(
    CettaInferenceChecker *checker,
    CettaInferenceRuleHandle rule,
    bool active,
    char *error_buf,
    size_t error_buf_size) {
    inference_error_clear(error_buf, error_buf_size);
    if (!checker || rule >= checker->rule_count)
        return inference_error(CETTA_INFERENCE_UNKNOWN_RULE,
                               error_buf, error_buf_size,
                               "unknown inference rule handle");
    checker->rules[rule].active = active;
    return CETTA_INFERENCE_OK;
}

bool cetta_inference_checker_rule_is_active(
    const CettaInferenceChecker *checker,
    CettaInferenceRuleHandle rule) {
    return checker && rule < checker->rule_count &&
           checker->rules[rule].active;
}

CettaInferenceRuleHandle cetta_inference_checker_find_rule(
    const CettaInferenceChecker *checker,
    const char *rule_id) {
    uint64_t value;
    SymbolId id;

    if (!checker || !rule_id || !g_symbols)
        return CETTA_INFERENCE_RULE_HANDLE_NONE;
    id = symbol_intern_cstr(g_symbols, rule_id);
    if (!inference_slots_find(checker->rule_slots, checker->rule_slot_count,
                              id, &value) || value > UINT32_MAX ||
        value >= checker->rule_count || !checker->rules[value].active)
        return CETTA_INFERENCE_RULE_HANDLE_NONE;
    return (CettaInferenceRuleHandle)value;
}

static CettaInferenceStatus inference_validate_ground_pattern(
    const CettaInferenceChecker *checker,
    Atom *pattern,
    uint64_t depth,
    char *error_buf,
    size_t error_buf_size);

static CettaInferenceStatus inference_validate_ground_list(
    const CettaInferenceChecker *checker,
    Atom *list,
    uint64_t depth,
    char *error_buf,
    size_t error_buf_size) {
    Atom *cursor = list;

    while (!atom_is_symbol(cursor, "LNil")) {
        CettaInferenceStatus status;
        if (!atom_expr_tag(cursor, "LCons", 3))
            return inference_error(CETTA_INFERENCE_INVALID_ARGUMENTS,
                                   error_buf, error_buf_size,
                                   "argument pattern list is malformed");
        status = inference_validate_ground_pattern(
            checker, cursor->expr.elems[1], depth,
            error_buf, error_buf_size);
        if (status != CETTA_INFERENCE_OK)
            return status;
        cursor = cursor->expr.elems[2];
    }
    return CETTA_INFERENCE_OK;
}

static CettaInferenceStatus inference_validate_ground_pattern(
    const CettaInferenceChecker *checker,
    Atom *pattern,
    uint64_t depth,
    char *error_buf,
    size_t error_buf_size) {
    uint64_t number;
    uint64_t nested_depth;
    IntegerResult integer_result;

    if (atom_expr_tag(pattern, "Var", 2)) {
        integer_result = atom_nonnegative_u64(pattern->expr.elems[1], &number);
        if (integer_result == INTEGER_RESOURCE_LIMIT)
            return inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                                   error_buf, error_buf_size,
                                   "argument variable exceeds native range");
        if (integer_result != INTEGER_OK || number >= depth)
            return inference_error(CETTA_INFERENCE_INVALID_ARGUMENTS,
                                   error_buf, error_buf_size,
                                   "argument contains an unbound variable");
        return CETTA_INFERENCE_OK;
    }
    if (atom_expr_tag(pattern, "FVar", 2))
        return inference_error(CETTA_INFERENCE_INVALID_ARGUMENTS,
                               error_buf, error_buf_size,
                               "argument contains a schema metavariable");
    if (atom_expr_tag(pattern, "PApp", 3)) {
        const char *head;
        SymbolId head_id;
        uint64_t arity;
        size_t actual_arity;
        if (!atom_string_value(pattern->expr.elems[1], &head) || !*head ||
            !canonical_list_length(pattern->expr.elems[2], &actual_arity)) {
            return inference_error(CETTA_INFERENCE_INVALID_ARGUMENTS,
                                   error_buf, error_buf_size,
                                   "argument application head is not canonical");
        }
        head_id = symbol_intern_cstr(g_symbols, head);
        if (checker &&
            inference_slots_find(checker->constructor_slots,
                                 checker->constructor_slot_count,
                                 head_id, &arity)) {
            if (arity != (uint64_t)actual_arity)
                return inference_error(
                    CETTA_INFERENCE_INVALID_ARGUMENTS,
                    error_buf, error_buf_size,
                    "argument uses wrong constructor arity '%s'", head);
        } else if (actual_arity != 0u) {
            return inference_error(
                CETTA_INFERENCE_INVALID_ARGUMENTS,
                error_buf, error_buf_size,
                "argument uses unknown constructor '%s'", head);
        }
        /* An undeclared nullary application is opaque atom data.  Once a head
         * is declared, including at nonzero arity, the declaration above is
         * authoritative. */
        if (actual_arity == 0u)
            return CETTA_INFERENCE_OK;
        return inference_validate_ground_list(
            checker, pattern->expr.elems[2], depth,
            error_buf, error_buf_size);
    }
    if (atom_expr_tag(pattern, "PLam", 3)) {
        if (!atom_is_symbol(pattern->expr.elems[1], "BNone") ||
            !checked_depth_add(depth, 1, &nested_depth))
            return inference_error(CETTA_INFERENCE_INVALID_ARGUMENTS,
                                   error_buf, error_buf_size,
                                   "argument lambda is not canonical");
        return inference_validate_ground_pattern(
            checker, pattern->expr.elems[2], nested_depth,
            error_buf, error_buf_size);
    }
    if (atom_expr_tag(pattern, "PMultiLam", 4)) {
        if (!atom_is_symbol(pattern->expr.elems[2], "LNil"))
            return inference_error(CETTA_INFERENCE_INVALID_ARGUMENTS,
                                   error_buf, error_buf_size,
                                   "argument multi-binder is not canonical");
        integer_result = atom_nonnegative_u64(pattern->expr.elems[1], &number);
        if (integer_result == INTEGER_RESOURCE_LIMIT ||
            (integer_result == INTEGER_OK &&
             !checked_depth_add(depth, number, &nested_depth)))
            return inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                                   error_buf, error_buf_size,
                                   "argument binding depth exceeds native range");
        if (integer_result != INTEGER_OK)
            return inference_error(CETTA_INFERENCE_INVALID_ARGUMENTS,
                                   error_buf, error_buf_size,
                                   "argument multi-binder arity is invalid");
        return inference_validate_ground_pattern(
            checker, pattern->expr.elems[3], nested_depth,
            error_buf, error_buf_size);
    }
    if (atom_expr_tag(pattern, "PSubst", 3)) {
        CettaInferenceStatus status;
        if (!checked_depth_add(depth, 1, &nested_depth))
            return inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                                   error_buf, error_buf_size,
                                   "argument substitution depth exceeds native range");
        status = inference_validate_ground_pattern(
            checker, pattern->expr.elems[1], nested_depth,
            error_buf, error_buf_size);
        if (status != CETTA_INFERENCE_OK)
            return status;
        return inference_validate_ground_pattern(
            checker, pattern->expr.elems[2], depth,
            error_buf, error_buf_size);
    }
    if (atom_expr_tag(pattern, "PCollection", 4)) {
        if (!cetta_inference_pattern_collection_type_valid_v1(
                pattern->expr.elems[1]) ||
            !atom_is_symbol(pattern->expr.elems[3], "RNone"))
            return inference_error(CETTA_INFERENCE_INVALID_ARGUMENTS,
                                   error_buf, error_buf_size,
                                   "argument collection metadata is not canonical");
        return inference_validate_ground_list(
            checker, pattern->expr.elems[2], depth,
            error_buf, error_buf_size);
    }
    return inference_error(CETTA_INFERENCE_INVALID_ARGUMENTS,
                           error_buf, error_buf_size,
                           "argument is not a canonical pattern");
}

static Atom *inference_instantiate_pattern(
    const InferenceRule *rule,
    Atom *const *arguments,
    Atom *schema,
    uint64_t depth,
    Arena *arena,
    CettaInferenceStatus *status);

static Atom *inference_instantiate_list(
    const InferenceRule *rule,
    Atom *const *arguments,
    Atom *list,
    uint64_t depth,
    Arena *arena,
    CettaInferenceStatus *status,
    bool *changed) {
    size_t count;
    Atom **source_items;
    Atom **items;
    Atom *result;
    size_t index;
    size_t item_bytes;

    if (!canonical_list_length(list, &count)) {
        *status = CETTA_INFERENCE_INVALID_PRESENTATION;
        return NULL;
    }
    if (count == 0) {
        *changed = false;
        return list;
    }
    if (!checked_array_size(count, sizeof(*items), &item_bytes)) {
        *status = CETTA_INFERENCE_RESOURCE_LIMIT;
        return NULL;
    }
    source_items = cetta_malloc(item_bytes);
    items = cetta_malloc(item_bytes);
    canonical_list_copy(list, source_items, count);
    *changed = false;
    for (index = 0; index < count; index++) {
        items[index] = inference_instantiate_pattern(
            rule, arguments, source_items[index], depth, arena, status);
        if (*status != CETTA_INFERENCE_OK) {
            free(items);
            free(source_items);
            return NULL;
        }
        if (items[index] != source_items[index])
            *changed = true;
    }
    if (!*changed) {
        free(items);
        free(source_items);
        return list;
    }
    result = atom_symbol(arena, "LNil");
    for (index = count; index > 0; index--)
        result = atom_expr3(arena, atom_symbol(arena, "LCons"),
                            items[index - 1], result);
    free(items);
    free(source_items);
    return result;
}

static Atom *inference_instantiate_pattern(
    const InferenceRule *rule,
    Atom *const *arguments,
    Atom *schema,
    uint64_t depth,
    Arena *arena,
    CettaInferenceStatus *status) {
    if (atom_expr_tag(schema, "Var", 2))
        return schema;
    if (atom_expr_tag(schema, "FVar", 2)) {
        const char *name;
        SymbolId name_id;
        size_t index;
        if (!atom_string_value(schema->expr.elems[1], &name)) {
            *status = CETTA_INFERENCE_INVALID_PRESENTATION;
            return NULL;
        }
        name_id = symbol_intern_cstr(g_symbols, name);
        for (index = 0; index < rule->formal_count; index++) {
            if (rule->formals[index].name == name_id &&
                rule->formals[index].depth == depth)
                return arguments[index];
        }
        *status = CETTA_INFERENCE_INVALID_PRESENTATION;
        return NULL;
    }
    if (atom_expr_tag(schema, "PApp", 3)) {
        bool changed;
        Atom *list = inference_instantiate_list(
            rule, arguments, schema->expr.elems[2], depth, arena, status,
            &changed);
        Atom *elems[3];
        if (*status != CETTA_INFERENCE_OK)
            return NULL;
        if (!changed)
            return schema;
        elems[0] = schema->expr.elems[0];
        elems[1] = schema->expr.elems[1];
        elems[2] = list;
        return atom_expr(arena, elems, 3);
    }
    if (atom_expr_tag(schema, "PLam", 3)) {
        Atom *body = inference_instantiate_pattern(
            rule, arguments, schema->expr.elems[2], depth + 1, arena, status);
        Atom *elems[3];
        if (*status != CETTA_INFERENCE_OK)
            return NULL;
        if (body == schema->expr.elems[2])
            return schema;
        elems[0] = schema->expr.elems[0];
        elems[1] = schema->expr.elems[1];
        elems[2] = body;
        return atom_expr(arena, elems, 3);
    }
    if (atom_expr_tag(schema, "PMultiLam", 4)) {
        uint64_t arity;
        Atom *body;
        Atom *elems[4];
        if (atom_nonnegative_u64(schema->expr.elems[1], &arity) != INTEGER_OK) {
            *status = CETTA_INFERENCE_INVALID_PRESENTATION;
            return NULL;
        }
        body = inference_instantiate_pattern(
            rule, arguments, schema->expr.elems[3], depth + arity,
            arena, status);
        if (*status != CETTA_INFERENCE_OK)
            return NULL;
        if (body == schema->expr.elems[3])
            return schema;
        elems[0] = schema->expr.elems[0];
        elems[1] = schema->expr.elems[1];
        elems[2] = schema->expr.elems[2];
        elems[3] = body;
        return atom_expr(arena, elems, 4);
    }
    if (atom_expr_tag(schema, "PSubst", 3)) {
        Atom *body = inference_instantiate_pattern(
            rule, arguments, schema->expr.elems[1], depth + 1, arena, status);
        Atom *replacement;
        Atom *elems[3];
        if (*status != CETTA_INFERENCE_OK)
            return NULL;
        replacement = inference_instantiate_pattern(
            rule, arguments, schema->expr.elems[2], depth, arena, status);
        if (*status != CETTA_INFERENCE_OK)
            return NULL;
        if (body == schema->expr.elems[1] &&
            replacement == schema->expr.elems[2])
            return schema;
        elems[0] = schema->expr.elems[0];
        elems[1] = body;
        elems[2] = replacement;
        return atom_expr(arena, elems, 3);
    }
    if (atom_expr_tag(schema, "PCollection", 4)) {
        bool changed;
        Atom *list = inference_instantiate_list(
            rule, arguments, schema->expr.elems[2], depth, arena, status,
            &changed);
        Atom *elems[4];
        if (*status != CETTA_INFERENCE_OK)
            return NULL;
        if (!changed)
            return schema;
        elems[0] = schema->expr.elems[0];
        elems[1] = schema->expr.elems[1];
        elems[2] = list;
        elems[3] = schema->expr.elems[3];
        return atom_expr(arena, elems, 4);
    }
    *status = CETTA_INFERENCE_INVALID_PRESENTATION;
    return NULL;
}

static bool inference_trace_reserve(Atom ***items,
                                    size_t *capacity,
                                    size_t needed) {
    size_t next;

    if (*capacity >= needed)
        return true;
    next = *capacity ? *capacity : 8;
    while (next < needed) {
        if (next > SIZE_MAX / 2)
            return false;
        next *= 2;
    }
    if (next > SIZE_MAX / sizeof(**items))
        return false;
    *items = cetta_realloc(*items, sizeof(**items) * next);
    *capacity = next;
    return true;
}

void cetta_inference_trace_init(
    CettaInferenceTrace *trace,
    const CettaInferenceChecker *checker,
    Arena *arena) {
    if (!trace)
        return;
    memset(trace, 0, sizeof(*trace));
    trace->checker = checker;
    trace->arena = arena;
}

void cetta_inference_trace_reset(CettaInferenceTrace *trace) {
    if (!trace)
        return;
    trace->stack_len = 0;
    trace->saved_len = 0;
}

void cetta_inference_trace_free(CettaInferenceTrace *trace) {
    if (!trace)
        return;
    free(trace->stack);
    free(trace->saved);
    memset(trace, 0, sizeof(*trace));
}

static CettaInferenceStatus inference_side_conditions_hold(
    const InferenceRule *rule,
    Atom *const *arguments,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size) {
    for (size_t index = 0u; index < rule->side_condition_count; index++) {
        const InferenceSideCondition *condition =
            &rule->side_conditions[index];
        ArenaMark mark = arena_mark(arena);
        Atom *computed = NULL;
        CettaInferencePatternAbtStatusV1 abt_status;

        if (condition->kind == INFERENCE_SIDE_EXPLICIT_SUBSTITUTION) {
            abt_status = cetta_inference_pattern_explicit_substitution_v1(
                arena,
                arguments[condition->body_argument],
                arguments[condition->replacement_argument],
                &computed);
        } else {
            abt_status =
                cetta_inference_pattern_unused_binder_elimination_v1(
                    arena,
                    arguments[condition->body_argument],
                    &computed);
        }
        bool holds = abt_status == CETTA_INFERENCE_PATTERN_ABT_OK &&
            atom_eq_fast(computed, arguments[condition->result_argument]);
        arena_reset(arena, mark);
        if (abt_status == CETTA_INFERENCE_PATTERN_ABT_RESOURCE_LIMIT)
            return inference_error(
                CETTA_INFERENCE_RESOURCE_LIMIT,
                error_buf, error_buf_size,
                "rule side-condition ABT lowering exceeds the native carrier");
        if (abt_status == CETTA_INFERENCE_PATTERN_ABT_INVALID)
            return inference_error(
                CETTA_INFERENCE_INVALID_PRESENTATION,
                error_buf, error_buf_size,
                "rule side-condition ABT lowering rejected admitted Pattern data");
        if (!holds)
            return inference_error(
                CETTA_INFERENCE_INVALID_ARGUMENTS,
                error_buf, error_buf_size,
                "rule side condition does not hold");
    }
    return CETTA_INFERENCE_OK;
}

CettaInferenceStatus cetta_inference_trace_apply(
    CettaInferenceTrace *trace,
    CettaInferenceRuleHandle rule_handle,
    Atom *const *arguments,
    size_t argument_count,
    char *error_buf,
    size_t error_buf_size) {
    const InferenceRule *rule;
    Atom **premises = NULL;
    Atom *conclusion;
    CettaInferenceStatus status = CETTA_INFERENCE_OK;
    size_t index;
    size_t premise_bytes;

    inference_error_clear(error_buf, error_buf_size);
    if (!trace || !trace->checker || !trace->arena ||
        rule_handle >= trace->checker->rule_count ||
        !trace->checker->rules[rule_handle].active)
        return inference_error(CETTA_INFERENCE_UNKNOWN_RULE,
                               error_buf, error_buf_size,
                               "unknown inference rule");
    rule = &trace->checker->rules[rule_handle];
    if (argument_count != rule->formal_count ||
        (argument_count > 0 && !arguments))
        return inference_error(CETTA_INFERENCE_INVALID_ARGUMENTS,
                               error_buf, error_buf_size,
                               "rule argument count mismatch");
    for (index = 0; index < argument_count; index++) {
        ArenaMark support_mark = arena_mark(trace->arena);
        CettaInferencePatternAbtStatusV1 support_status =
            cetta_inference_pattern_supported_at_v1(
                trace->arena, rule->formals[index].depth, arguments[index]);
        arena_reset(trace->arena, support_mark);
        if (support_status == CETTA_INFERENCE_PATTERN_ABT_RESOURCE_LIMIT)
            return inference_error(
                CETTA_INFERENCE_RESOURCE_LIMIT,
                error_buf, error_buf_size,
                "argument support check exceeds the native ABT carrier");
        if (support_status != CETTA_INFERENCE_PATTERN_ABT_OK)
            return inference_error(
                CETTA_INFERENCE_INVALID_ARGUMENTS,
                error_buf, error_buf_size,
                "argument is outside its declared binder support");
        status = inference_validate_ground_pattern(
            trace->checker, arguments[index], rule->formals[index].depth,
            error_buf, error_buf_size);
        if (status != CETTA_INFERENCE_OK)
            return status;
    }
    status = inference_side_conditions_hold(
        rule, arguments, trace->arena, error_buf, error_buf_size);
    if (status != CETTA_INFERENCE_OK)
        return status;
    if (!checked_array_size(rule->premise_count, sizeof(*premises),
                            &premise_bytes))
        return inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                               error_buf, error_buf_size,
                               "instantiated premise storage is too large");
    if (rule->premise_count > 0)
        premises = cetta_malloc(premise_bytes);
    for (index = 0; index < rule->premise_count; index++) {
        premises[index] = inference_instantiate_pattern(
            rule, arguments, rule->premises[index], 0, trace->arena, &status);
        if (status != CETTA_INFERENCE_OK) {
            free(premises);
            return inference_error(status, error_buf, error_buf_size,
                                   "failed to instantiate rule premise");
        }
    }
    conclusion = inference_instantiate_pattern(
        rule, arguments, rule->conclusion, 0, trace->arena, &status);
    if (status != CETTA_INFERENCE_OK) {
        free(premises);
        return inference_error(status, error_buf, error_buf_size,
                               "failed to instantiate rule conclusion");
    }
    if (trace->stack_len < rule->premise_count) {
        free(premises);
        return inference_error(CETTA_INFERENCE_PREMISE_MISMATCH,
                               error_buf, error_buf_size,
                               "proof stack has too few premises");
    }
    for (index = 0; index < rule->premise_count; index++) {
        size_t stack_index = trace->stack_len - rule->premise_count + index;
        if (!atom_eq_fast(trace->stack[stack_index], premises[index])) {
            free(premises);
            return inference_error(CETTA_INFERENCE_PREMISE_MISMATCH,
                                   error_buf, error_buf_size,
                                   "proof stack premise mismatch");
        }
    }
    free(premises);
    if (!inference_trace_reserve(&trace->stack, &trace->stack_capacity,
                                 trace->stack_len - rule->premise_count + 1))
        return inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                               error_buf, error_buf_size,
                               "proof stack capacity exceeds native range");
    trace->stack_len -= rule->premise_count;
    trace->stack[trace->stack_len++] = conclusion;
    return CETTA_INFERENCE_OK;
}

CettaInferenceStatus cetta_inference_trace_apply_named(
    CettaInferenceTrace *trace,
    const char *rule_id,
    Atom *const *arguments,
    size_t argument_count,
    char *error_buf,
    size_t error_buf_size) {
    CettaInferenceRuleHandle handle;

    if (!trace || !trace->checker)
        return inference_error(CETTA_INFERENCE_UNKNOWN_RULE,
                               error_buf, error_buf_size,
                               "trace has no inference presentation");
    handle = cetta_inference_checker_find_rule(trace->checker, rule_id);
    if (handle == CETTA_INFERENCE_RULE_HANDLE_NONE)
        return inference_error(CETTA_INFERENCE_UNKNOWN_RULE,
                               error_buf, error_buf_size,
                               "unknown inference rule '%s'",
                               rule_id ? rule_id : "");
    return cetta_inference_trace_apply(
        trace, handle, arguments, argument_count, error_buf, error_buf_size);
}

CettaInferenceStatus cetta_inference_trace_save(
    CettaInferenceTrace *trace,
    char *error_buf,
    size_t error_buf_size) {
    inference_error_clear(error_buf, error_buf_size);
    if (!trace || trace->stack_len == 0)
        return inference_error(CETTA_INFERENCE_EMPTY_STACK,
                               error_buf, error_buf_size,
                               "cannot save an empty proof stack");
    if (!inference_trace_reserve(&trace->saved, &trace->saved_capacity,
                                 trace->saved_len + 1))
        return inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                               error_buf, error_buf_size,
                               "saved-proof capacity exceeds native range");
    trace->saved[trace->saved_len++] = trace->stack[trace->stack_len - 1];
    return CETTA_INFERENCE_OK;
}

CettaInferenceStatus cetta_inference_trace_reference(
    CettaInferenceTrace *trace,
    size_t saved_index,
    char *error_buf,
    size_t error_buf_size) {
    inference_error_clear(error_buf, error_buf_size);
    if (!trace || saved_index >= trace->saved_len)
        return inference_error(CETTA_INFERENCE_BAD_REFERENCE,
                               error_buf, error_buf_size,
                               "saved-proof reference is unavailable");
    if (!inference_trace_reserve(&trace->stack, &trace->stack_capacity,
                                 trace->stack_len + 1))
        return inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                               error_buf, error_buf_size,
                               "proof stack capacity exceeds native range");
    trace->stack[trace->stack_len++] = trace->saved[saved_index];
    return CETTA_INFERENCE_OK;
}

CettaInferenceStatus cetta_inference_trace_finish(
    const CettaInferenceTrace *trace,
    Atom *goal,
    char *error_buf,
    size_t error_buf_size) {
    inference_error_clear(error_buf, error_buf_size);
    if (!trace || !goal || trace->stack_len != 1 ||
        !atom_eq_fast(trace->stack[0], goal))
        return inference_error(CETTA_INFERENCE_FINAL_MISMATCH,
                               error_buf, error_buf_size,
                               "proof stack does not contain exactly the goal");
    return CETTA_INFERENCE_OK;
}

typedef struct {
    Atom *proof;
    Atom *children;
    size_t base_stack_len;
    size_t child_count;
    size_t depth;
    bool entered;
} InferenceRawFrame;

static bool inference_raw_frames_reserve(
    InferenceRawFrame **frames,
    size_t *capacity,
    size_t needed) {
    size_t next;

    if (*capacity >= needed)
        return true;
    next = *capacity ? *capacity : 16;
    while (next < needed) {
        if (next > SIZE_MAX / 2)
            return false;
        next *= 2;
    }
    if (next > SIZE_MAX / sizeof(**frames))
        return false;
    *frames = cetta_realloc(*frames, sizeof(**frames) * next);
    *capacity = next;
    return true;
}

static CettaInferenceStatus inference_raw_malformed(
    char *error_buf,
    size_t error_buf_size,
    const char *message) {
    return inference_error(CETTA_INFERENCE_MALFORMED_PROOF,
                           error_buf, error_buf_size, "%s", message);
}

CettaInferenceStatus cetta_inference_checker_check_raw_proof(
    const CettaInferenceChecker *checker,
    Atom *goal,
    Atom *proof,
    CettaInferenceReplayLimits limits,
    CettaInferenceReplayStats *stats,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size) {
    const size_t default_max_nodes = 1000000;
    const size_t default_max_depth = 4096;
    CettaInferenceTrace trace;
    InferenceRawFrame *frames = NULL;
    size_t frame_count = 0;
    size_t frame_capacity = 0;
    size_t nodes = 0;
    CettaInferenceStatus status;

    inference_error_clear(error_buf, error_buf_size);
    if (stats)
        memset(stats, 0, sizeof(*stats));
    if (!arena)
        return inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                               error_buf, error_buf_size,
                               "proof replay arena is unavailable");
    if (!checker)
        return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                               error_buf, error_buf_size,
                               "admitted inference checker is unavailable");
    if (limits.max_nodes == 0)
        limits.max_nodes = default_max_nodes;
    if (limits.max_depth == 0)
        limits.max_depth = default_max_depth;
    cetta_inference_trace_init(&trace, checker, arena);
    if (!inference_raw_frames_reserve(
            &frames, &frame_capacity, frame_count + 1)) {
        status = inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                                 error_buf, error_buf_size,
                                 "proof frame capacity exceeds native range");
        goto done;
    }
    frames[frame_count++] = (InferenceRawFrame){
        .proof = proof,
        .base_stack_len = 0,
        .depth = 1,
    };

    while (frame_count > 0) {
        InferenceRawFrame *frame = &frames[frame_count - 1];

        if (!frame->entered) {
            if (nodes >= limits.max_nodes) {
                status = inference_error(
                    CETTA_INFERENCE_RESOURCE_LIMIT,
                    error_buf, error_buf_size,
                    "proof node limit exceeded");
                goto done;
            }
            nodes++;
            if (stats) {
                stats->nodes = nodes;
                if (frame->depth > stats->max_depth_observed)
                    stats->max_depth_observed = frame->depth;
            }
            if (!atom_expr_tag(frame->proof, "GProof", 3) ||
                !atom_expr_tag(frame->proof->expr.elems[1],
                               "GRuleInst", 3)) {
                status = inference_raw_malformed(
                    error_buf, error_buf_size,
                    "expected GProof with one GRuleInst");
                goto done;
            }
            frame->children = frame->proof->expr.elems[2];
            frame->base_stack_len = trace.stack_len;
            frame->entered = true;
        }

        if (!atom_is_symbol(frame->children, "PrNil")) {
            Atom *child;
            size_t child_depth;
            if (!atom_expr_tag(frame->children, "PrCons", 3)) {
                status = inference_raw_malformed(
                    error_buf, error_buf_size,
                    "proof children are not a canonical PrNil/PrCons list");
                goto done;
            }
            if (frame->depth >= limits.max_depth) {
                status = inference_error(
                    CETTA_INFERENCE_RESOURCE_LIMIT,
                    error_buf, error_buf_size,
                    "proof depth limit exceeded");
                goto done;
            }
            if (frame->child_count == SIZE_MAX) {
                status = inference_error(
                    CETTA_INFERENCE_RESOURCE_LIMIT,
                    error_buf, error_buf_size,
                    "proof child count exceeds native range");
                goto done;
            }
            child = frame->children->expr.elems[1];
            frame->children = frame->children->expr.elems[2];
            frame->child_count++;
            child_depth = frame->depth + 1;
            if (!inference_raw_frames_reserve(
                    &frames, &frame_capacity, frame_count + 1)) {
                status = inference_error(
                    CETTA_INFERENCE_RESOURCE_LIMIT,
                    error_buf, error_buf_size,
                    "proof frame capacity exceeds native range");
                goto done;
            }
            frames[frame_count++] = (InferenceRawFrame){
                .proof = child,
                .base_stack_len = trace.stack_len,
                .depth = child_depth,
            };
            continue;
        }

        {
            Atom *instance = frame->proof->expr.elems[1];
            const char *rule_id;
            size_t argument_count;
            size_t argument_bytes;
            Atom **arguments = NULL;
            size_t expected_stack_len;

            if (!atom_string_value(instance->expr.elems[1], &rule_id) ||
                !*rule_id ||
                !canonical_list_length(instance->expr.elems[2],
                                       &argument_count)) {
                status = inference_raw_malformed(
                    error_buf, error_buf_size,
                    "malformed GRuleInst");
                goto done;
            }
            if (frame->base_stack_len > SIZE_MAX - frame->child_count) {
                status = inference_error(
                    CETTA_INFERENCE_RESOURCE_LIMIT,
                    error_buf, error_buf_size,
                    "proof stack accounting exceeds native range");
                goto done;
            }
            expected_stack_len =
                frame->base_stack_len + frame->child_count;
            if (trace.stack_len != expected_stack_len) {
                status = inference_error(
                    CETTA_INFERENCE_PREMISE_MISMATCH,
                    error_buf, error_buf_size,
                    "a child proof consumed evidence outside its parent");
                goto done;
            }
            if (!checked_array_size(argument_count, sizeof(*arguments),
                                    &argument_bytes)) {
                status = inference_error(
                    CETTA_INFERENCE_RESOURCE_LIMIT,
                    error_buf, error_buf_size,
                    "rule argument storage is too large");
                goto done;
            }
            if (argument_count > 0) {
                arguments = cetta_malloc(argument_bytes);
                if (!canonical_list_copy(instance->expr.elems[2], arguments,
                                         argument_count)) {
                    free(arguments);
                    status = inference_raw_malformed(
                        error_buf, error_buf_size,
                        "rule arguments changed during proof decoding");
                    goto done;
                }
            }
            status = cetta_inference_trace_apply_named(
                &trace, rule_id, arguments, argument_count,
                error_buf, error_buf_size);
            free(arguments);
            if (status != CETTA_INFERENCE_OK)
                goto done;
            if (frame->base_stack_len == SIZE_MAX ||
                trace.stack_len != frame->base_stack_len + 1) {
                status = inference_error(
                    CETTA_INFERENCE_PREMISE_MISMATCH,
                    error_buf, error_buf_size,
                    "proof node does not close exactly its own children");
                goto done;
            }
        }
        frame_count--;
    }
    status = cetta_inference_trace_finish(
        &trace, goal, error_buf, error_buf_size);

done:
    free(frames);
    cetta_inference_trace_free(&trace);
    return status;
}

CettaInferenceStatus cetta_inference_check_raw_proof(
    Atom *presentation,
    Atom *goal,
    Atom *proof,
    CettaInferenceReplayLimits limits,
    CettaInferenceReplayStats *stats,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size) {
    CettaInferenceChecker *checker = NULL;
    CettaInferenceStatus status = cetta_inference_checker_create(
        presentation, &checker, error_buf, error_buf_size);
    if (status != CETTA_INFERENCE_OK)
        return status;
    status = cetta_inference_checker_check_raw_proof(
        checker, goal, proof, limits, stats, arena,
        error_buf, error_buf_size);
    cetta_inference_checker_destroy(checker);
    return status;
}

typedef struct {
    uint64_t id;
    size_t saved_index;
    size_t depth;
    bool occupied;
} InferenceDAGEntry;

typedef struct {
    InferenceDAGEntry *entries;
    size_t capacity;
    size_t count;
} InferenceDAGIndex;

static uint64_t inference_dag_hash(uint64_t value) {
    value ^= value >> 33;
    value *= UINT64_C(0xff51afd7ed558ccd);
    value ^= value >> 33;
    value *= UINT64_C(0xc4ceb9fe1a85ec53);
    value ^= value >> 33;
    return value;
}

static const InferenceDAGEntry *inference_dag_index_find(
    const InferenceDAGIndex *index, uint64_t id) {
    size_t slot;
    if (!index || index->capacity == 0u)
        return NULL;
    slot = (size_t)inference_dag_hash(id) & (index->capacity - 1u);
    while (index->entries[slot].occupied) {
        if (index->entries[slot].id == id)
            return &index->entries[slot];
        slot = (slot + 1u) & (index->capacity - 1u);
    }
    return NULL;
}

static bool inference_dag_index_reserve(
    InferenceDAGIndex *index, size_t needed) {
    InferenceDAGEntry *entries;
    size_t next;

    if (needed <= index->capacity / 2u)
        return true;
    next = index->capacity ? index->capacity : 16u;
    while (needed > next / 2u) {
        if (next > SIZE_MAX / 2u)
            return false;
        next *= 2u;
    }
    if (next > SIZE_MAX / sizeof(*entries))
        return false;
    entries = cetta_malloc(next * sizeof(*entries));
    memset(entries, 0, next * sizeof(*entries));
    for (size_t old = 0u; old < index->capacity; old++) {
        InferenceDAGEntry entry = index->entries[old];
        size_t slot;
        if (!entry.occupied)
            continue;
        slot = (size_t)inference_dag_hash(entry.id) & (next - 1u);
        while (entries[slot].occupied)
            slot = (slot + 1u) & (next - 1u);
        entries[slot] = entry;
    }
    free(index->entries);
    index->entries = entries;
    index->capacity = next;
    return true;
}

static bool inference_dag_index_insert(
    InferenceDAGIndex *index, uint64_t id,
    size_t saved_index, size_t depth) {
    size_t slot;
    if (index->count == SIZE_MAX)
        return false;
    if (!inference_dag_index_reserve(index, index->count + 1u))
        return false;
    slot = (size_t)inference_dag_hash(id) & (index->capacity - 1u);
    while (index->entries[slot].occupied) {
        if (index->entries[slot].id == id)
            return false;
        slot = (slot + 1u) & (index->capacity - 1u);
    }
    index->entries[slot] = (InferenceDAGEntry){
        .id = id,
        .saved_index = saved_index,
        .depth = depth,
        .occupied = true,
    };
    index->count++;
    return true;
}

static CettaInferenceStatus inference_dag_parse_u64(
    Atom *atom, uint64_t *value,
    char *error_buf, size_t error_buf_size,
    const char *invalid_message, const char *range_message) {
    IntegerResult result = atom_nonnegative_u64(atom, value);
    if (result == INTEGER_RESOURCE_LIMIT)
        return inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                               error_buf, error_buf_size, "%s", range_message);
    if (result != INTEGER_OK)
        return inference_raw_malformed(
            error_buf, error_buf_size, invalid_message);
    return CETTA_INFERENCE_OK;
}

static CettaInferenceStatus inference_dag_apply_instance(
    CettaInferenceTrace *trace, Atom *instance,
    char *error_buf, size_t error_buf_size) {
    const char *rule_id;
    size_t argument_count;
    size_t argument_bytes;
    Atom **arguments = NULL;
    CettaInferenceStatus status;

    if (!atom_expr_tag(instance, "GRuleInst", 3) ||
        !atom_string_value(instance->expr.elems[1], &rule_id) ||
        !canonical_list_length(instance->expr.elems[2], &argument_count))
        return inference_raw_malformed(
            error_buf, error_buf_size,
            "DAG node does not contain a canonical GRuleInst");
    if (!checked_array_size(argument_count, sizeof(*arguments),
                            &argument_bytes))
        return inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                               error_buf, error_buf_size,
                               "DAG rule argument storage is too large");
    if (argument_count > 0u) {
        arguments = cetta_malloc(argument_bytes);
        if (!canonical_list_copy(instance->expr.elems[2], arguments,
                                 argument_count)) {
            free(arguments);
            return inference_raw_malformed(
                error_buf, error_buf_size,
                "DAG rule arguments changed during decoding");
        }
    }
    status = cetta_inference_trace_apply_named(
        trace, rule_id, arguments, argument_count,
        error_buf, error_buf_size);
    free(arguments);
    return status;
}

static CettaInferenceStatus inference_checker_check_dag_article_v1(
    const CettaInferenceChecker *checker,
    Atom *goal,
    Atom *article,
    CettaInferenceReplayLimits limits,
    CettaInferenceReplayStats *stats,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size) {
    const size_t default_max_nodes = 1000000u;
    const size_t default_max_depth = 4096u;
    CettaInferenceTrace trace;
    InferenceDAGIndex index = {0};
    Atom *nodes;
    Atom *target;
    uint64_t version;
    uint64_t root_id;
    CettaInferenceStatus status = CETTA_INFERENCE_MALFORMED_PROOF;

    inference_error_clear(error_buf, error_buf_size);
    if (stats)
        memset(stats, 0, sizeof(*stats));
    if (!arena)
        return inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                               error_buf, error_buf_size,
                               "DAG replay arena is unavailable");
    if (!checker)
        return inference_error(CETTA_INFERENCE_INVALID_PRESENTATION,
                               error_buf, error_buf_size,
                               "admitted inference checker is unavailable");
    if (!goal || !atom_expr_tag(article, "GProofDAG", 5))
        return inference_raw_malformed(
            error_buf, error_buf_size,
            "expected GProofDAG version, nodes, root, and target");
    status = inference_dag_parse_u64(
        article->expr.elems[1], &version, error_buf, error_buf_size,
        "DAG article version is not nonnegative",
        "DAG article version exceeds native range");
    if (status != CETTA_INFERENCE_OK)
        return status;
    if (version != 1u)
        return inference_raw_malformed(
            error_buf, error_buf_size,
            "unsupported GProofDAG article version");
    status = inference_dag_parse_u64(
        article->expr.elems[3], &root_id, error_buf, error_buf_size,
        "DAG root identifier is not nonnegative",
        "DAG root identifier exceeds native range");
    if (status != CETTA_INFERENCE_OK)
        return status;
    target = article->expr.elems[4];
    if (!atom_eq_fast(target, goal))
        return inference_error(CETTA_INFERENCE_FINAL_MISMATCH,
                               error_buf, error_buf_size,
                               "DAG article target differs from submitted goal");
    nodes = article->expr.elems[2];
    if (limits.max_nodes == 0u)
        limits.max_nodes = default_max_nodes;
    if (limits.max_depth == 0u)
        limits.max_depth = default_max_depth;
    cetta_inference_trace_init(&trace, checker, arena);

    while (!atom_is_symbol(nodes, "LNil")) {
        Atom *node;
        Atom *references;
        uint64_t node_id;
        size_t node_depth = 1u;
        size_t saved_index;

        if (index.count >= limits.max_nodes) {
            status = inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                                     error_buf, error_buf_size,
                                     "DAG proof node limit exceeded");
            goto done;
        }
        if (!atom_expr_tag(nodes, "LCons", 3)) {
            status = inference_raw_malformed(
                error_buf, error_buf_size,
                "DAG nodes are not a canonical LNil/LCons list");
            goto done;
        }
        node = nodes->expr.elems[1];
        nodes = nodes->expr.elems[2];
        if (!atom_expr_tag(node, "GDNode", 4)) {
            status = inference_raw_malformed(
                error_buf, error_buf_size,
                "DAG node does not have id, rule instance, and references");
            goto done;
        }
        status = inference_dag_parse_u64(
            node->expr.elems[1], &node_id, error_buf, error_buf_size,
            "DAG node identifier is not nonnegative",
            "DAG node identifier exceeds native range");
        if (status != CETTA_INFERENCE_OK)
            goto done;
        if (inference_dag_index_find(&index, node_id)) {
            status = inference_raw_malformed(
                error_buf, error_buf_size,
                "DAG node identifier is duplicated");
            goto done;
        }

        trace.stack_len = 0u;
        references = node->expr.elems[3];
        while (!atom_is_symbol(references, "LNil")) {
            Atom *reference;
            uint64_t child_id;
            const InferenceDAGEntry *child;
            if (!atom_expr_tag(references, "LCons", 3)) {
                status = inference_raw_malformed(
                    error_buf, error_buf_size,
                    "DAG references are not a canonical LNil/LCons list");
                goto done;
            }
            reference = references->expr.elems[1];
            references = references->expr.elems[2];
            if (atom_expr_tag(reference, "GRPremise", 2)) {
                status = inference_error(
                    CETTA_INFERENCE_BAD_REFERENCE,
                    error_buf, error_buf_size,
                    "closed NIK article cannot cite an open premise");
                goto done;
            }
            if (!atom_expr_tag(reference, "GRNode", 2)) {
                status = inference_raw_malformed(
                    error_buf, error_buf_size,
                    "DAG edge is neither GRNode nor GRPremise");
                goto done;
            }
            status = inference_dag_parse_u64(
                reference->expr.elems[1], &child_id,
                error_buf, error_buf_size,
                "DAG child identifier is not nonnegative",
                "DAG child identifier exceeds native range");
            if (status != CETTA_INFERENCE_OK)
                goto done;
            child = inference_dag_index_find(&index, child_id);
            if (!child) {
                status = inference_error(
                    CETTA_INFERENCE_BAD_REFERENCE,
                    error_buf, error_buf_size,
                    "DAG child does not refer to an earlier checked node");
                goto done;
            }
            status = cetta_inference_trace_reference(
                &trace, child->saved_index, error_buf, error_buf_size);
            if (status != CETTA_INFERENCE_OK)
                goto done;
            if (child->depth == SIZE_MAX) {
                status = inference_error(
                    CETTA_INFERENCE_RESOURCE_LIMIT,
                    error_buf, error_buf_size,
                    "DAG proof depth exceeds native range");
                goto done;
            }
            if (node_depth < child->depth + 1u)
                node_depth = child->depth + 1u;
        }
        if (node_depth > limits.max_depth) {
            status = inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                                     error_buf, error_buf_size,
                                     "DAG proof depth limit exceeded");
            goto done;
        }
        status = inference_dag_apply_instance(
            &trace, node->expr.elems[2], error_buf, error_buf_size);
        if (status != CETTA_INFERENCE_OK)
            goto done;
        if (trace.stack_len != 1u) {
            status = inference_error(
                CETTA_INFERENCE_PREMISE_MISMATCH,
                error_buf, error_buf_size,
                "DAG node does not close exactly its referenced premises");
            goto done;
        }
        saved_index = trace.saved_len;
        status = cetta_inference_trace_save(
            &trace, error_buf, error_buf_size);
        if (status != CETTA_INFERENCE_OK)
            goto done;
        if (!inference_dag_index_insert(
                &index, node_id, saved_index, node_depth)) {
            status = inference_error(
                CETTA_INFERENCE_RESOURCE_LIMIT,
                error_buf, error_buf_size,
                "DAG index capacity exceeds native range");
            goto done;
        }
        trace.stack_len = 0u;
        if (stats) {
            stats->nodes = index.count;
            if (stats->max_depth_observed < node_depth)
                stats->max_depth_observed = node_depth;
        }
    }

    {
        const InferenceDAGEntry *root =
            inference_dag_index_find(&index, root_id);
        if (!root) {
            status = inference_error(CETTA_INFERENCE_BAD_REFERENCE,
                                     error_buf, error_buf_size,
                                     "DAG root does not name a checked node");
            goto done;
        }
        trace.stack_len = 0u;
        status = cetta_inference_trace_reference(
            &trace, root->saved_index, error_buf, error_buf_size);
        if (status != CETTA_INFERENCE_OK)
            goto done;
        status = cetta_inference_trace_finish(
            &trace, target, error_buf, error_buf_size);
    }

done:
    free(index.entries);
    cetta_inference_trace_free(&trace);
    return status;
}

static bool inference_shared_optional_string(
    Atom *value, const char *none_tag, const char *some_tag) {
    const char *ignored;
    return atom_is_symbol(value, none_tag) ||
        (atom_expr_tag(value, some_tag, 2u) &&
         atom_string_value(value->expr.elems[1], &ignored));
}

static bool inference_shared_string_list(Atom *list) {
    Atom *cursor = list;
    while (!atom_is_symbol(cursor, "LNil")) {
        const char *ignored;
        if (!atom_expr_tag(cursor, "LCons", 3u) ||
            !atom_string_value(cursor->expr.elems[1], &ignored))
            return false;
        cursor = cursor->expr.elems[2];
    }
    return true;
}

static CettaInferenceStatus inference_shared_resolve_pattern(
    Atom *encoded_id, Atom *const *patterns, const size_t *depths,
    size_t available, Atom **pattern_out, size_t *depth_out,
    char *error_buf, size_t error_buf_size) {
    uint64_t id = 0u;
    CettaInferenceStatus status = inference_dag_parse_u64(
        encoded_id, &id, error_buf, error_buf_size,
        "shared Pattern reference is not nonnegative",
        "shared Pattern reference exceeds native range");
    if (status != CETTA_INFERENCE_OK)
        return status;
    if (id >= (uint64_t)available)
        return inference_error(
            CETTA_INFERENCE_BAD_REFERENCE, error_buf, error_buf_size,
            "shared Pattern reference does not name an earlier node");
    *pattern_out = patterns[(size_t)id];
    if (depth_out)
        *depth_out = depths[(size_t)id];
    return CETTA_INFERENCE_OK;
}

static CettaInferenceStatus inference_shared_resolve_pattern_list(
    Atom *encoded, Atom *const *patterns, const size_t *depths,
    size_t available, Arena *arena, Atom **list_out, size_t *depth_out,
    char *error_buf, size_t error_buf_size) {
    size_t count;
    size_t bytes;
    size_t maximum_depth = 0u;
    Atom **resolved = NULL;
    Atom *cursor = encoded;
    Atom *result;

    if (!canonical_list_length(encoded, &count))
        return inference_raw_malformed(
            error_buf, error_buf_size,
            "shared Pattern references are not a canonical list");
    if (!checked_array_size(count, sizeof(*resolved), &bytes))
        return inference_error(
            CETTA_INFERENCE_RESOURCE_LIMIT, error_buf, error_buf_size,
            "shared Pattern reference storage is too large");
    if (count > 0u)
        resolved = cetta_malloc(bytes);
    for (size_t index = 0u; index < count; index++) {
        size_t depth = 0u;
        CettaInferenceStatus status;
        if (!atom_expr_tag(cursor, "LCons", 3u)) {
            free(resolved);
            return inference_raw_malformed(
                error_buf, error_buf_size,
                "shared Pattern reference list changed during decoding");
        }
        status = inference_shared_resolve_pattern(
            cursor->expr.elems[1], patterns, depths, available,
            &resolved[index], &depth, error_buf, error_buf_size);
        if (status != CETTA_INFERENCE_OK) {
            free(resolved);
            return status;
        }
        if (maximum_depth < depth)
            maximum_depth = depth;
        cursor = cursor->expr.elems[2];
    }
    result = atom_symbol(arena, "LNil");
    for (size_t index = count; index > 0u; index--)
        result = atom_expr3(
            arena, atom_symbol(arena, "LCons"), resolved[index - 1u], result);
    free(resolved);
    *list_out = result;
    if (depth_out)
        *depth_out = maximum_depth;
    return CETTA_INFERENCE_OK;
}

static CettaInferenceStatus inference_shared_materialize_pattern_key(
    Atom *key, Atom *const *patterns, const size_t *depths,
    size_t available, Arena *arena, Atom **pattern_out, size_t *depth_out,
    char *error_buf, size_t error_buf_size) {
    Atom *children;
    Atom *left;
    Atom *right;
    size_t child_depth = 0u;
    size_t left_depth;
    size_t right_depth;
    uint64_t ignored_number;
    const char *ignored_string;
    CettaInferenceStatus status;

    if (atom_expr_tag(key, "GPKBVar", 2u)) {
        status = inference_dag_parse_u64(
            key->expr.elems[1], &ignored_number,
            error_buf, error_buf_size,
            "shared variable index is not nonnegative",
            "shared variable index exceeds native range");
        if (status != CETTA_INFERENCE_OK)
            return status;
        *pattern_out = atom_expr2(
            arena, atom_symbol(arena, "Var"), key->expr.elems[1]);
        *depth_out = 1u;
        return CETTA_INFERENCE_OK;
    }
    if (atom_expr_tag(key, "GPKFVar", 2u)) {
        if (!atom_string_value(key->expr.elems[1], &ignored_string))
            return inference_raw_malformed(
                error_buf, error_buf_size,
                "shared free-variable name is not a string");
        *pattern_out = atom_expr2(
            arena, atom_symbol(arena, "FVar"), key->expr.elems[1]);
        *depth_out = 1u;
        return CETTA_INFERENCE_OK;
    }
    if (atom_expr_tag(key, "GPKApply", 3u)) {
        Atom *elements[3];
        if (!atom_string_value(key->expr.elems[1], &ignored_string))
            return inference_raw_malformed(
                error_buf, error_buf_size,
                "shared application head is not a string");
        status = inference_shared_resolve_pattern_list(
            key->expr.elems[2], patterns, depths, available, arena,
            &children, &child_depth, error_buf, error_buf_size);
        if (status != CETTA_INFERENCE_OK)
            return status;
        if (child_depth == SIZE_MAX)
            return inference_error(
                CETTA_INFERENCE_RESOURCE_LIMIT, error_buf, error_buf_size,
                "shared Pattern depth exceeds native range");
        elements[0] = atom_symbol(arena, "PApp");
        elements[1] = key->expr.elems[1];
        elements[2] = children;
        *pattern_out = atom_expr(arena, elements, 3u);
        *depth_out = child_depth + 1u;
        return CETTA_INFERENCE_OK;
    }
    if (atom_expr_tag(key, "GPKLambda", 3u)) {
        if (!inference_shared_optional_string(
                key->expr.elems[1], "BNone", "BSome"))
            return inference_raw_malformed(
                error_buf, error_buf_size,
                "shared lambda binder is not canonical");
        status = inference_shared_resolve_pattern(
            key->expr.elems[2], patterns, depths, available,
            &left, &left_depth, error_buf, error_buf_size);
        if (status != CETTA_INFERENCE_OK)
            return status;
        if (left_depth == SIZE_MAX)
            return inference_error(
                CETTA_INFERENCE_RESOURCE_LIMIT, error_buf, error_buf_size,
                "shared Pattern depth exceeds native range");
        *pattern_out = atom_expr3(
            arena, atom_symbol(arena, "PLam"), key->expr.elems[1], left);
        *depth_out = left_depth + 1u;
        return CETTA_INFERENCE_OK;
    }
    if (atom_expr_tag(key, "GPKMultiLambda", 4u)) {
        Atom *elements[4];
        status = inference_dag_parse_u64(
            key->expr.elems[1], &ignored_number,
            error_buf, error_buf_size,
            "shared multi-lambda arity is not nonnegative",
            "shared multi-lambda arity exceeds native range");
        if (status != CETTA_INFERENCE_OK)
            return status;
        if (!inference_shared_string_list(key->expr.elems[2]))
            return inference_raw_malformed(
                error_buf, error_buf_size,
                "shared multi-lambda binders are not canonical");
        status = inference_shared_resolve_pattern(
            key->expr.elems[3], patterns, depths, available,
            &left, &left_depth, error_buf, error_buf_size);
        if (status != CETTA_INFERENCE_OK)
            return status;
        if (left_depth == SIZE_MAX)
            return inference_error(
                CETTA_INFERENCE_RESOURCE_LIMIT, error_buf, error_buf_size,
                "shared Pattern depth exceeds native range");
        elements[0] = atom_symbol(arena, "PMultiLam");
        elements[1] = key->expr.elems[1];
        elements[2] = key->expr.elems[2];
        elements[3] = left;
        *pattern_out = atom_expr(arena, elements, 4u);
        *depth_out = left_depth + 1u;
        return CETTA_INFERENCE_OK;
    }
    if (atom_expr_tag(key, "GPKSubst", 3u)) {
        status = inference_shared_resolve_pattern(
            key->expr.elems[1], patterns, depths, available,
            &left, &left_depth, error_buf, error_buf_size);
        if (status != CETTA_INFERENCE_OK)
            return status;
        status = inference_shared_resolve_pattern(
            key->expr.elems[2], patterns, depths, available,
            &right, &right_depth, error_buf, error_buf_size);
        if (status != CETTA_INFERENCE_OK)
            return status;
        child_depth = left_depth > right_depth ? left_depth : right_depth;
        if (child_depth == SIZE_MAX)
            return inference_error(
                CETTA_INFERENCE_RESOURCE_LIMIT, error_buf, error_buf_size,
                "shared Pattern depth exceeds native range");
        *pattern_out = atom_expr3(
            arena, atom_symbol(arena, "PSubst"), left, right);
        *depth_out = child_depth + 1u;
        return CETTA_INFERENCE_OK;
    }
    if (atom_expr_tag(key, "GPKCollection", 4u)) {
        Atom *elements[4];
        if (!cetta_inference_pattern_collection_type_valid_v1(
                key->expr.elems[1]) ||
            !inference_shared_optional_string(
                key->expr.elems[3], "RNone", "RSome"))
            return inference_raw_malformed(
                error_buf, error_buf_size,
                "shared collection metadata is not canonical");
        status = inference_shared_resolve_pattern_list(
            key->expr.elems[2], patterns, depths, available, arena,
            &children, &child_depth, error_buf, error_buf_size);
        if (status != CETTA_INFERENCE_OK)
            return status;
        if (child_depth == SIZE_MAX)
            return inference_error(
                CETTA_INFERENCE_RESOURCE_LIMIT, error_buf, error_buf_size,
                "shared Pattern depth exceeds native range");
        elements[0] = atom_symbol(arena, "PCollection");
        elements[1] = key->expr.elems[1];
        elements[2] = children;
        elements[3] = key->expr.elems[3];
        *pattern_out = atom_expr(arena, elements, 4u);
        *depth_out = child_depth + 1u;
        return CETTA_INFERENCE_OK;
    }
    return inference_raw_malformed(
        error_buf, error_buf_size,
        "unknown or malformed shared Pattern node key");
}

static CettaInferenceStatus inference_shared_materialize_patterns(
    Atom *encoded_nodes, size_t max_nodes, size_t max_depth,
    Arena *arena, Atom ***patterns_out, size_t **depths_out,
    size_t *count_out, size_t *maximum_depth_out,
    char *error_buf, size_t error_buf_size) {
    size_t count;
    size_t pattern_bytes;
    size_t depth_bytes;
    size_t maximum_depth = 0u;
    Atom **patterns = NULL;
    size_t *depths = NULL;
    Atom *cursor = encoded_nodes;

    if (!canonical_list_length(encoded_nodes, &count))
        return inference_raw_malformed(
            error_buf, error_buf_size,
            "shared Pattern nodes are not a canonical list");
    if (count >= max_nodes)
        return inference_error(
            CETTA_INFERENCE_RESOURCE_LIMIT, error_buf, error_buf_size,
            "shared Pattern table exhausts the article node limit");
    if (!checked_array_size(count, sizeof(*patterns), &pattern_bytes) ||
        !checked_array_size(count, sizeof(*depths), &depth_bytes))
        return inference_error(
            CETTA_INFERENCE_RESOURCE_LIMIT, error_buf, error_buf_size,
            "shared Pattern table exceeds native storage");
    if (count > 0u) {
        patterns = cetta_malloc(pattern_bytes);
        depths = cetta_malloc(depth_bytes);
    }
    for (size_t index = 0u; index < count; index++) {
        Atom *node;
        uint64_t node_id;
        CettaInferenceStatus status;
        if (!atom_expr_tag(cursor, "LCons", 3u)) {
            free(depths);
            free(patterns);
            return inference_raw_malformed(
                error_buf, error_buf_size,
                "shared Pattern node list changed during decoding");
        }
        node = cursor->expr.elems[1];
        cursor = cursor->expr.elems[2];
        if (!atom_expr_tag(node, "GPatternNode", 3u)) {
            free(depths);
            free(patterns);
            return inference_raw_malformed(
                error_buf, error_buf_size,
                "shared Pattern node lacks an id and key");
        }
        status = inference_dag_parse_u64(
            node->expr.elems[1], &node_id, error_buf, error_buf_size,
            "shared Pattern node id is not nonnegative",
            "shared Pattern node id exceeds native range");
        if (status != CETTA_INFERENCE_OK) {
            free(depths);
            free(patterns);
            return status;
        }
        if (node_id != (uint64_t)index) {
            free(depths);
            free(patterns);
            return inference_raw_malformed(
                error_buf, error_buf_size,
                "shared Pattern node ids are not chronological ordinals");
        }
        status = inference_shared_materialize_pattern_key(
            node->expr.elems[2], patterns, depths, index, arena,
            &patterns[index], &depths[index], error_buf, error_buf_size);
        if (status != CETTA_INFERENCE_OK) {
            free(depths);
            free(patterns);
            return status;
        }
        if (depths[index] > max_depth) {
            free(depths);
            free(patterns);
            return inference_error(
                CETTA_INFERENCE_RESOURCE_LIMIT, error_buf, error_buf_size,
                "shared Pattern depth limit exceeded");
        }
        if (maximum_depth < depths[index])
            maximum_depth = depths[index];
    }
    *patterns_out = patterns;
    *depths_out = depths;
    *count_out = count;
    *maximum_depth_out = maximum_depth;
    return CETTA_INFERENCE_OK;
}

static CettaInferenceStatus inference_shared_materialize_proof_nodes(
    Atom *encoded_nodes, Atom *const *patterns, const size_t *depths,
    size_t pattern_count, size_t max_nodes, Arena *arena,
    Atom **nodes_out, char *error_buf, size_t error_buf_size) {
    size_t count;
    size_t bytes;
    Atom **nodes = NULL;
    Atom *cursor = encoded_nodes;
    Atom *result;

    if (!canonical_list_length(encoded_nodes, &count))
        return inference_raw_malformed(
            error_buf, error_buf_size,
            "shared proof nodes are not a canonical list");
    if (count > max_nodes)
        return inference_error(
            CETTA_INFERENCE_RESOURCE_LIMIT, error_buf, error_buf_size,
            "shared proof nodes exceed the remaining article node limit");
    if (!checked_array_size(count, sizeof(*nodes), &bytes))
        return inference_error(
            CETTA_INFERENCE_RESOURCE_LIMIT, error_buf, error_buf_size,
            "shared proof node storage exceeds native range");
    if (count > 0u)
        nodes = cetta_malloc(bytes);
    for (size_t index = 0u; index < count; index++) {
        Atom *node;
        Atom *rule_references;
        Atom *arguments;
        Atom *instance;
        Atom *elements[4];
        const char *ignored_rule;
        CettaInferenceStatus status;
        if (!atom_expr_tag(cursor, "LCons", 3u)) {
            free(nodes);
            return inference_raw_malformed(
                error_buf, error_buf_size,
                "shared proof node list changed during decoding");
        }
        node = cursor->expr.elems[1];
        cursor = cursor->expr.elems[2];
        if (!atom_expr_tag(node, "GDNode", 4u)) {
            free(nodes);
            return inference_raw_malformed(
                error_buf, error_buf_size,
                "shared proof node lacks id, rule references, or children");
        }
        rule_references = node->expr.elems[2];
        if (!atom_expr_tag(rule_references, "GRuleRefs", 3u) ||
            !atom_string_value(
                rule_references->expr.elems[1], &ignored_rule)) {
            free(nodes);
            return inference_raw_malformed(
                error_buf, error_buf_size,
                "shared proof node lacks a canonical GRuleRefs");
        }
        status = inference_shared_resolve_pattern_list(
            rule_references->expr.elems[2], patterns, depths,
            pattern_count, arena, &arguments, NULL,
            error_buf, error_buf_size);
        if (status != CETTA_INFERENCE_OK) {
            free(nodes);
            return status;
        }
        instance = atom_expr3(
            arena, atom_symbol(arena, "GRuleInst"),
            rule_references->expr.elems[1], arguments);
        elements[0] = atom_symbol(arena, "GDNode");
        elements[1] = node->expr.elems[1];
        elements[2] = instance;
        elements[3] = node->expr.elems[3];
        nodes[index] = atom_expr(arena, elements, 4u);
    }
    result = atom_symbol(arena, "LNil");
    for (size_t index = count; index > 0u; index--)
        result = atom_expr3(
            arena, atom_symbol(arena, "LCons"), nodes[index - 1u], result);
    free(nodes);
    *nodes_out = result;
    return CETTA_INFERENCE_OK;
}

static CettaInferenceStatus inference_checker_check_dag_article_v2(
    const CettaInferenceChecker *checker, Atom *goal, Atom *article,
    CettaInferenceReplayLimits limits, CettaInferenceReplayStats *stats,
    Arena *arena, char *error_buf, size_t error_buf_size) {
    const size_t default_max_nodes = 1000000u;
    const size_t default_max_depth = 4096u;
    Atom **patterns = NULL;
    size_t *depths = NULL;
    size_t pattern_count = 0u;
    size_t pattern_depth = 0u;
    Atom *target;
    Atom *proof_nodes = NULL;
    Atom *materialized_article;
    Atom *elements[5];
    uint64_t version;
    CettaInferenceReplayStats proof_stats = {0};
    CettaInferenceStatus status;

    inference_error_clear(error_buf, error_buf_size);
    if (stats)
        memset(stats, 0, sizeof(*stats));
    if (!arena)
        return inference_error(
            CETTA_INFERENCE_RESOURCE_LIMIT, error_buf, error_buf_size,
            "shared DAG replay arena is unavailable");
    if (!checker)
        return inference_error(
            CETTA_INFERENCE_INVALID_PRESENTATION, error_buf, error_buf_size,
            "admitted inference checker is unavailable");
    if (!goal || !atom_expr_tag(article, "GProofDAG", 6u))
        return inference_raw_malformed(
            error_buf, error_buf_size,
            "expected shared GProofDAG version, Patterns, nodes, root, target");
    status = inference_dag_parse_u64(
        article->expr.elems[1], &version, error_buf, error_buf_size,
        "shared DAG article version is not nonnegative",
        "shared DAG article version exceeds native range");
    if (status != CETTA_INFERENCE_OK)
        return status;
    if (version != 2u)
        return inference_raw_malformed(
            error_buf, error_buf_size,
            "unsupported shared GProofDAG article version");
    if (limits.max_nodes == 0u)
        limits.max_nodes = default_max_nodes;
    if (limits.max_depth == 0u)
        limits.max_depth = default_max_depth;
    status = inference_shared_materialize_patterns(
        article->expr.elems[2], limits.max_nodes, limits.max_depth,
        arena, &patterns, &depths, &pattern_count, &pattern_depth,
        error_buf, error_buf_size);
    if (status != CETTA_INFERENCE_OK)
        goto done;
    if (stats) {
        stats->nodes = pattern_count;
        stats->max_depth_observed = pattern_depth;
    }
    status = inference_shared_resolve_pattern(
        article->expr.elems[5], patterns, depths, pattern_count,
        &target, NULL, error_buf, error_buf_size);
    if (status != CETTA_INFERENCE_OK)
        goto done;
    status = inference_shared_materialize_proof_nodes(
        article->expr.elems[3], patterns, depths, pattern_count,
        limits.max_nodes - pattern_count, arena, &proof_nodes,
        error_buf, error_buf_size);
    if (status != CETTA_INFERENCE_OK)
        goto done;
    elements[0] = atom_symbol(arena, "GProofDAG");
    elements[1] = atom_int(arena, 1);
    elements[2] = proof_nodes;
    elements[3] = article->expr.elems[4];
    elements[4] = target;
    materialized_article = atom_expr(arena, elements, 5u);
    limits.max_nodes -= pattern_count;
    status = inference_checker_check_dag_article_v1(
        checker, goal, materialized_article, limits, &proof_stats,
        arena, error_buf, error_buf_size);
    if (stats) {
        if (proof_stats.nodes > SIZE_MAX - pattern_count)
            stats->nodes = SIZE_MAX;
        else
            stats->nodes = pattern_count + proof_stats.nodes;
        if (stats->max_depth_observed < proof_stats.max_depth_observed)
            stats->max_depth_observed = proof_stats.max_depth_observed;
    }

done:
    free(depths);
    free(patterns);
    return status;
}

CettaInferenceStatus cetta_inference_checker_check_dag_article(
    const CettaInferenceChecker *checker,
    Atom *goal,
    Atom *article,
    CettaInferenceReplayLimits limits,
    CettaInferenceReplayStats *stats,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size) {
    if (article && atom_expr_tag(article, "GProofDAG", 6u))
        return inference_checker_check_dag_article_v2(
            checker, goal, article, limits, stats, arena,
            error_buf, error_buf_size);
    return inference_checker_check_dag_article_v1(
        checker, goal, article, limits, stats, arena,
        error_buf, error_buf_size);
}

CettaInferenceStatus cetta_inference_check_dag_article(
    Atom *presentation,
    Atom *goal,
    Atom *article,
    CettaInferenceReplayLimits limits,
    CettaInferenceReplayStats *stats,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size) {
    CettaInferenceChecker *checker = NULL;
    CettaInferenceStatus status = cetta_inference_checker_create(
        presentation, &checker, error_buf, error_buf_size);
    if (status != CETTA_INFERENCE_OK)
        return status;
    status = cetta_inference_checker_check_dag_article(
        checker, goal, article, limits, stats, arena,
        error_buf, error_buf_size);
    cetta_inference_checker_destroy(checker);
    return status;
}

CettaInferenceStatus cetta_inference_check_trace_atoms(
    Atom *presentation,
    Atom *goal,
    Atom *actions,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size) {
    CettaInferenceChecker *checker = NULL;
    CettaInferenceTrace trace;
    CettaInferenceStatus status;
    Atom *cursor = actions;

    if (!arena)
        return inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                               error_buf, error_buf_size,
                               "trace arena is unavailable");
    status = cetta_inference_checker_create(
        presentation, &checker, error_buf, error_buf_size);
    if (status != CETTA_INFERENCE_OK)
        return status;
    cetta_inference_trace_init(&trace, checker, arena);

    while (!atom_is_symbol(cursor, "GaNil")) {
        Atom *action;
        if (!atom_expr_tag(cursor, "GaCons", 3)) {
            status = inference_error(CETTA_INFERENCE_INVALID_ARGUMENTS,
                                     error_buf, error_buf_size,
                                     "trace actions are not a canonical list");
            goto done;
        }
        action = cursor->expr.elems[1];
        if (atom_expr_tag(action, "GApply", 2)) {
            Atom *instance = action->expr.elems[1];
            const char *rule_id;
            size_t argument_count;
            size_t argument_bytes;
            Atom **arguments = NULL;

            if (!atom_expr_tag(instance, "GRuleInst", 3) ||
                !atom_string_value(instance->expr.elems[1], &rule_id) ||
                !canonical_list_length(instance->expr.elems[2],
                                       &argument_count)) {
                status = inference_error(CETTA_INFERENCE_INVALID_ARGUMENTS,
                                         error_buf, error_buf_size,
                                         "malformed GApply action");
                goto done;
            }
            if (!checked_array_size(argument_count, sizeof(*arguments),
                                    &argument_bytes)) {
                status = inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                                         error_buf, error_buf_size,
                                         "rule argument storage is too large");
                goto done;
            }
            if (argument_count > 0) {
                arguments = cetta_malloc(argument_bytes);
                if (!canonical_list_copy(instance->expr.elems[2], arguments,
                                         argument_count)) {
                    free(arguments);
                    status = inference_error(
                        CETTA_INFERENCE_INVALID_ARGUMENTS,
                        error_buf, error_buf_size,
                        "rule arguments changed during trace decoding");
                    goto done;
                }
            }
            status = cetta_inference_trace_apply_named(
                &trace, rule_id, arguments, argument_count,
                error_buf, error_buf_size);
            free(arguments);
            if (status != CETTA_INFERENCE_OK)
                goto done;
        } else if (atom_is_symbol(action, "GSave")) {
            status = cetta_inference_trace_save(
                &trace, error_buf, error_buf_size);
            if (status != CETTA_INFERENCE_OK)
                goto done;
        } else if (atom_expr_tag(action, "GRef", 2)) {
            uint64_t index;
            IntegerResult integer_result = atom_nonnegative_u64(
                action->expr.elems[1], &index);
            if (integer_result == INTEGER_RESOURCE_LIMIT || index > SIZE_MAX) {
                status = inference_error(CETTA_INFERENCE_RESOURCE_LIMIT,
                                         error_buf, error_buf_size,
                                         "saved reference exceeds native range");
                goto done;
            }
            if (integer_result != INTEGER_OK) {
                status = inference_error(CETTA_INFERENCE_BAD_REFERENCE,
                                         error_buf, error_buf_size,
                                         "saved reference is not nonnegative");
                goto done;
            }
            status = cetta_inference_trace_reference(
                &trace, (size_t)index, error_buf, error_buf_size);
            if (status != CETTA_INFERENCE_OK)
                goto done;
        } else {
            status = inference_error(CETTA_INFERENCE_INVALID_ARGUMENTS,
                                     error_buf, error_buf_size,
                                     "unknown trace action");
            goto done;
        }
        cursor = cursor->expr.elems[2];
    }
    status = cetta_inference_trace_finish(
        &trace, goal, error_buf, error_buf_size);

done:
    cetta_inference_trace_free(&trace);
    cetta_inference_checker_destroy(checker);
    return status;
}
