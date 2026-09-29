#ifndef CETTA_GSLT2PARSE_PARSER_ACTION_PRIMITIVE_V1_H
#define CETTA_GSLT2PARSE_PARSER_ACTION_PRIMITIVE_V1_H

#include "atom.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Shared value operations for tree actions and postfix action bytecode.
 * These operations construct data; they never invoke the host evaluator.
 * Strings use the current Atom carrier's NUL-terminated representation.
 * Length-bearing input boundaries must reject embedded U+0000 before making
 * an Atom (as the finite-Horn wire reader does). No embedded-NUL representation
 * or lossless conversion from arbitrary Lean String is asserted here.
 */
typedef enum {
    PP_ACTION_PRIMITIVE_V1_SYMBOL,
    PP_ACTION_PRIMITIVE_V1_CONS,
    PP_ACTION_PRIMITIVE_V1_APPEND,
    PP_ACTION_PRIMITIVE_V1_TEXT_APPEND,
    PP_ACTION_PRIMITIVE_V1_DECIMAL_INTEGER_VALUE,
    PP_ACTION_PRIMITIVE_V1_DECIMAL_RATIONAL_COMPONENTS
} PPActionPrimitiveV1;

typedef enum {
    PP_ACTION_PRIMITIVE_V1_OK,
    PP_ACTION_PRIMITIVE_V1_UNKNOWN,
    PP_ACTION_PRIMITIVE_V1_ARITY,
    PP_ACTION_PRIMITIVE_V1_SHAPE,
    PP_ACTION_PRIMITIVE_V1_OVERFLOW,
    PP_ACTION_PRIMITIVE_V1_ALLOCATION
} PPActionPrimitiveV1Status;

static inline bool pp_action_primitive_v1_decode(
    const Atom *term, PPActionPrimitiveV1 *operation, uint32_t *arity) {
    PPActionPrimitiveV1 value;
    uint32_t count;
    if (atom_is_symbol((Atom *)term, "atom-symbol")) {
        value = PP_ACTION_PRIMITIVE_V1_SYMBOL;
        count = 1u;
    } else if (atom_is_symbol((Atom *)term, "expression-cons")) {
        value = PP_ACTION_PRIMITIVE_V1_CONS;
        count = 2u;
    } else if (atom_is_symbol((Atom *)term, "expression-append")) {
        value = PP_ACTION_PRIMITIVE_V1_APPEND;
        count = 2u;
    } else if (atom_is_symbol((Atom *)term, "text-append")) {
        value = PP_ACTION_PRIMITIVE_V1_TEXT_APPEND;
        count = 2u;
    } else if (atom_is_symbol((Atom *)term, "decimal-integer-value")) {
        value = PP_ACTION_PRIMITIVE_V1_DECIMAL_INTEGER_VALUE;
        count = 1u;
    } else if (atom_is_symbol(
                   (Atom *)term, "decimal-rational-components")) {
        value = PP_ACTION_PRIMITIVE_V1_DECIMAL_RATIONAL_COMPONENTS;
        count = 1u;
    } else {
        return false;
    }
    if (operation)
        *operation = value;
    if (arity)
        *arity = count;
    return true;
}

/* This checks the finite operation inventory and its exact action-list arity.
 * The enclosing validator separately checks each argument's action syntax.
 */
static inline bool pp_action_primitive_v1_action_shape(
    const Atom *operation, const Atom *arguments) {
    uint32_t arity;
    uint32_t index;
    if (!pp_action_primitive_v1_decode(operation, NULL, &arity))
        return false;
    for (index = 0u; index < arity; index++) {
        if (!arguments || arguments->kind != ATOM_EXPR ||
            arguments->expr.len != 3u || !arguments->expr.elems ||
            !atom_is_symbol(arguments->expr.elems[0], "pa-cons"))
            return false;
        arguments = arguments->expr.elems[2];
    }
    return atom_is_symbol((Atom *)arguments, "pa-nil");
}

static inline const char *pp_action_primitive_v1_status_message(
    PPActionPrimitiveV1Status status) {
    switch (status) {
    case PP_ACTION_PRIMITIVE_V1_OK: return "action primitive completed";
    case PP_ACTION_PRIMITIVE_V1_UNKNOWN: return "unknown action primitive";
    case PP_ACTION_PRIMITIVE_V1_ARITY: return "action primitive arity mismatch";
    case PP_ACTION_PRIMITIVE_V1_SHAPE: return "action primitive operand shape mismatch";
    case PP_ACTION_PRIMITIVE_V1_OVERFLOW: return "action primitive result size overflow";
    case PP_ACTION_PRIMITIVE_V1_ALLOCATION: return "action primitive allocation failed";
    }
    return "invalid action primitive status";
}

static inline bool pp_action_primitive_v1_string(const Atom *term) {
    return term && term->kind == ATOM_GROUNDED &&
        term->ground.gkind == GV_STRING && term->ground.sval;
}

static inline bool pp_action_primitive_v1_unsigned_decimal(
    const char *text, size_t length, bool positive) {
    size_t index;
    if (!text || length == 0u)
        return false;
    if (text[0] == '0')
        return !positive && length == 1u;
    if (text[0] < '1' || text[0] > '9')
        return false;
    for (index = 1u; index < length; index++)
        if (text[index] < '0' || text[index] > '9')
            return false;
    return true;
}

static inline bool pp_action_primitive_v1_integer_decimal(
    const char *text, size_t length) {
    size_t begin = 0u;
    if (!text || length == 0u)
        return false;
    if (text[0] == '+' || text[0] == '-')
        begin = 1u;
    return begin < length &&
        pp_action_primitive_v1_unsigned_decimal(
            text + begin, length - begin, false);
}

/* Every constructed expression owns its child vector in the result arena.
 * Children remain borrowed immutable atoms, just as in existing pa-apply.
 * Allocation exhaustion follows the host arena/atom allocator's policy;
 * arithmetic overflow is refused before allocation or vector traversal.
 */
static inline PPActionPrimitiveV1Status pp_action_primitive_v1_execute(
    const Atom *primitive, Atom *const *arguments, uint32_t argument_len,
    Arena *arena, Atom **out) {
    PPActionPrimitiveV1 operation;
    uint32_t arity;
    if (out)
        *out = NULL;
    if (!pp_action_primitive_v1_decode(primitive, &operation, &arity))
        return PP_ACTION_PRIMITIVE_V1_UNKNOWN;
    if (argument_len != arity)
        return PP_ACTION_PRIMITIVE_V1_ARITY;
    if (!arena || !out || !arguments || !arguments[0] ||
        (arity == 2u && !arguments[1]))
        return PP_ACTION_PRIMITIVE_V1_SHAPE;
    if (operation == PP_ACTION_PRIMITIVE_V1_SYMBOL) {
        if (!pp_action_primitive_v1_string(arguments[0]))
            return PP_ACTION_PRIMITIVE_V1_SHAPE;
        *out = atom_symbol(arena, arguments[0]->ground.sval);
    } else if (operation == PP_ACTION_PRIMITIVE_V1_TEXT_APPEND) {
        size_t left_len;
        size_t right_len;
        char *text;
        if (!pp_action_primitive_v1_string(arguments[0]) ||
            !pp_action_primitive_v1_string(arguments[1]))
            return PP_ACTION_PRIMITIVE_V1_SHAPE;
        left_len = strlen(arguments[0]->ground.sval);
        right_len = strlen(arguments[1]->ground.sval);
        if (right_len == SIZE_MAX || left_len > SIZE_MAX - right_len - 1u)
            return PP_ACTION_PRIMITIVE_V1_OVERFLOW;
        text = malloc(left_len + right_len + 1u);
        if (!text)
            return PP_ACTION_PRIMITIVE_V1_ALLOCATION;
        memcpy(text, arguments[0]->ground.sval, left_len);
        memcpy(text + left_len, arguments[1]->ground.sval, right_len + 1u);
        *out = atom_string(arena, text);
        free(text);
    } else if (operation ==
                   PP_ACTION_PRIMITIVE_V1_DECIMAL_INTEGER_VALUE) {
        const char *text;
        size_t length;
        if (!pp_action_primitive_v1_string(arguments[0]))
            return PP_ACTION_PRIMITIVE_V1_SHAPE;
        text = arguments[0]->ground.sval;
        length = strlen(text);
        if (!pp_action_primitive_v1_integer_decimal(text, length))
            return PP_ACTION_PRIMITIVE_V1_SHAPE;
        *out = atom_bigint(arena, text);
    } else if (operation ==
                   PP_ACTION_PRIMITIVE_V1_DECIMAL_RATIONAL_COMPONENTS) {
        const char *text;
        const char *slash;
        size_t length;
        size_t numerator_len;
        char *numerator_text;
        Atom *numerator;
        Atom *denominator;
        Atom *result;
        if (!pp_action_primitive_v1_string(arguments[0]))
            return PP_ACTION_PRIMITIVE_V1_SHAPE;
        text = arguments[0]->ground.sval;
        length = strlen(text);
        slash = strchr(text, '/');
        if (!slash || slash == text || slash[1] == '\0' ||
            strchr(slash + 1, '/'))
            return PP_ACTION_PRIMITIVE_V1_SHAPE;
        numerator_len = (size_t)(slash - text);
        if (!pp_action_primitive_v1_integer_decimal(
                text, numerator_len) ||
            !pp_action_primitive_v1_unsigned_decimal(
                slash + 1, length - numerator_len - 1u, true)) {
            return PP_ACTION_PRIMITIVE_V1_SHAPE;
        }
        numerator_text = malloc(numerator_len + 1u);
        if (!numerator_text)
            return PP_ACTION_PRIMITIVE_V1_ALLOCATION;
        memcpy(numerator_text, text, numerator_len);
        numerator_text[numerator_len] = '\0';
        numerator = atom_bigint(arena, numerator_text);
        free(numerator_text);
        denominator = atom_bigint(arena, slash + 1);
        if (!numerator || !denominator)
            return PP_ACTION_PRIMITIVE_V1_ALLOCATION;
        result = atom_expr_builder_begin(arena, 2u);
        if (!result)
            return PP_ACTION_PRIMITIVE_V1_ALLOCATION;
        result->expr.elems[0] = numerator;
        result->expr.elems[1] = denominator;
        *out = atom_expr_builder_finish(arena, result);
    } else {
        const bool cons = operation == PP_ACTION_PRIMITIVE_V1_CONS;
        Atom *left = arguments[0];
        Atom *right = arguments[1];
        CettaExprLen left_len;
        CettaExprLen right_len;
        CettaExprLen length;
        CettaExprLen index;
        size_t bytes;
        Atom *result;
        if (right->kind != ATOM_EXPR || (!cons && left->kind != ATOM_EXPR))
            return PP_ACTION_PRIMITIVE_V1_SHAPE;
        left_len = cons ? 1u : left->expr.len;
        right_len = right->expr.len;
        if (right_len > UINT64_MAX - left_len)
            return PP_ACTION_PRIMITIVE_V1_OVERFLOW;
        length = left_len + right_len;
        if (!atom_expr_allocation_bound(length, &bytes))
            return PP_ACTION_PRIMITIVE_V1_OVERFLOW;
        (void)bytes;
        if ((right_len && !right->expr.elems) ||
            (!cons && left_len && !left->expr.elems))
            return PP_ACTION_PRIMITIVE_V1_SHAPE;
        for (index = 0u; index < right_len; index++)
            if (!right->expr.elems[index])
                return PP_ACTION_PRIMITIVE_V1_SHAPE;
        if (!cons)
            for (index = 0u; index < left_len; index++)
                if (!left->expr.elems[index])
                    return PP_ACTION_PRIMITIVE_V1_SHAPE;
        result = atom_expr_builder_begin(arena, length);
        if (!result)
            return PP_ACTION_PRIMITIVE_V1_ALLOCATION;
        if (cons)
            result->expr.elems[0] = left;
        else if (left_len)
            memcpy(result->expr.elems, left->expr.elems,
                (size_t)left_len * sizeof(Atom *));
        if (right_len)
            memcpy(result->expr.elems + left_len, right->expr.elems,
                (size_t)right_len * sizeof(Atom *));
        *out = atom_expr_builder_finish(arena, result);
    }
    return *out ? PP_ACTION_PRIMITIVE_V1_OK : PP_ACTION_PRIMITIVE_V1_ALLOCATION;
}

#endif
