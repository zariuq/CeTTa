#include "gslt_pure_provider_v1.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

typedef enum {
    PURE_TYPE_ATOM_V1 = 0,
    PURE_TYPE_F64_V1 = 1,
    PURE_TYPE_INVALID_V1 = 2,
} PureTypeV1;

typedef struct {
    PureTypeV1 type;
    Atom *atom;
    double f64;
} PureValueV1;

typedef enum {
    PURE_F64_FROM_STRING_V1 = 0,
    PURE_F64_TO_STRING_V1 = 1,
    PURE_F64_SIGNUM_V1 = 2,
    PURE_F64_SUB_V1 = 3,
    PURE_F64_DIV_V1 = 4,
    PURE_F64_SUM_V1 = 5,
    PURE_F64_PRODUCT_V1 = 6,
} PureOperationV1;

typedef struct {
    const char *name;
    PureOperationV1 operation;
    size_t minimum_arguments;
    size_t maximum_arguments;
    PureTypeV1 result_type;
} PureOperationDeclV1;

static const PureOperationDeclV1 PURE_OPERATIONS_V1[] = {
    {"f64_from_string", PURE_F64_FROM_STRING_V1, 1u, 1u,
     PURE_TYPE_F64_V1},
    {"f64_to_string", PURE_F64_TO_STRING_V1, 1u, 1u,
     PURE_TYPE_ATOM_V1},
    {"signum_f64", PURE_F64_SIGNUM_V1, 1u, 1u, PURE_TYPE_F64_V1},
    {"sub_f64", PURE_F64_SUB_V1, 2u, 2u, PURE_TYPE_F64_V1},
    {"div_f64", PURE_F64_DIV_V1, 2u, 2u, PURE_TYPE_F64_V1},
    {"sum_f64", PURE_F64_SUM_V1, 0u, SIZE_MAX, PURE_TYPE_F64_V1},
    {"product_f64", PURE_F64_PRODUCT_V1, 0u, SIZE_MAX,
     PURE_TYPE_F64_V1},
};

static void pure_error_v1(char *error, size_t error_size,
                          const char *format, ...) {
    if (!error || error_size == 0u)
        return;
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(error, error_size, format, arguments);
    va_end(arguments);
}

static const PureOperationDeclV1 *pure_operation_v1(const Atom *call) {
    if (!call || call->kind != ATOM_EXPR || call->expr.len == 0u ||
        call->expr.elems[0]->kind != ATOM_SYMBOL)
        return NULL;
    for (size_t index = 0u;
         index < sizeof(PURE_OPERATIONS_V1) / sizeof(PURE_OPERATIONS_V1[0]);
         index++) {
        const PureOperationDeclV1 *declaration = &PURE_OPERATIONS_V1[index];
        size_t argument_count = (size_t)call->expr.len - 1u;
        if (atom_is_symbol(call->expr.elems[0], declaration->name) &&
            argument_count >= declaration->minimum_arguments &&
            argument_count <= declaration->maximum_arguments)
            return declaration;
    }
    return NULL;
}

static PureTypeV1 pure_infer_v1(const Atom *term) {
    if (!term || term->kind != ATOM_EXPR)
        return PURE_TYPE_ATOM_V1;
    const PureOperationDeclV1 *declaration = pure_operation_v1(term);
    if (!declaration)
        return PURE_TYPE_INVALID_V1;
    for (CettaExprIndex index = 1u; index < term->expr.len; index++) {
        PureTypeV1 argument_type = pure_infer_v1(term->expr.elems[index]);
        PureTypeV1 required = declaration->operation == PURE_F64_FROM_STRING_V1
            ? PURE_TYPE_ATOM_V1 : PURE_TYPE_F64_V1;
        if (argument_type != required)
            return PURE_TYPE_INVALID_V1;
    }
    return declaration->result_type;
}

bool cetta_gslt_pure_f64_accepts_v1(const Atom *call) {
    return pure_infer_v1(call) == PURE_TYPE_ATOM_V1 &&
        call && call->kind == ATOM_EXPR;
}

static bool atom_as_f64_text_v1(Arena *arena, Atom *atom, double *value) {
    if (!arena || !atom || !value || atom->kind == ATOM_VAR ||
        atom->kind == ATOM_EXPR)
        return false;
    const char *text = NULL;
    if (atom->kind == ATOM_SYMBOL)
        text = atom_name_cstr(atom);
    else if (atom->kind == ATOM_GROUNDED &&
             (atom->ground.gkind == GV_INT ||
              atom->ground.gkind == GV_FLOAT ||
              atom->ground.gkind == GV_BIGINT ||
              atom->ground.gkind == GV_RATIONAL))
        text = atom_to_parseable_string(arena, atom);
    if (!text || text[0] == '\0')
        return false;
    char *end = NULL;
    double parsed = strtod(text, &end);
    if (!end || end == text || *end != '\0')
        return false;
    *value = parsed;
    return true;
}

static CettaGsltPureOutcomeV1 pure_evaluate_value_v1(
    Arena *arena, Atom *term, PureValueV1 *result,
    char *error, size_t error_size) {
    if (!arena || !term || !result) {
        pure_error_v1(error, error_size,
                      "invalid recursive pure-provider request");
        return CETTA_GSLT_PURE_FAULT_V1;
    }
    if (term->kind != ATOM_EXPR) {
        result->type = PURE_TYPE_ATOM_V1;
        result->atom = term;
        return CETTA_GSLT_PURE_PRODUCED_V1;
    }
    const PureOperationDeclV1 *declaration = pure_operation_v1(term);
    if (!declaration)
        return CETTA_GSLT_PURE_DECLINED_V1;
    size_t argument_count = (size_t)term->expr.len - 1u;
    PureValueV1 *arguments = argument_count
        ? calloc(argument_count, sizeof(*arguments)) : NULL;
    if (argument_count && !arguments) {
        pure_error_v1(error, error_size,
                      "cannot allocate pure-provider arguments");
        return CETTA_GSLT_PURE_FAULT_V1;
    }
    for (size_t index = 0u; index < argument_count; index++) {
        CettaGsltPureOutcomeV1 outcome = pure_evaluate_value_v1(
            arena, term->expr.elems[index + 1u], &arguments[index],
            error, error_size);
        if (outcome != CETTA_GSLT_PURE_PRODUCED_V1) {
            free(arguments);
            return outcome;
        }
    }

    bool produced = true;
    result->type = declaration->result_type;
    switch (declaration->operation) {
    case PURE_F64_FROM_STRING_V1:
        produced = arguments[0].type == PURE_TYPE_ATOM_V1 &&
            atom_as_f64_text_v1(arena, arguments[0].atom, &result->f64);
        break;
    case PURE_F64_TO_STRING_V1:
        produced = arguments[0].type == PURE_TYPE_F64_V1;
        if (produced)
            result->atom = atom_float(arena, arguments[0].f64);
        produced = produced && result->atom;
        break;
    case PURE_F64_SIGNUM_V1:
        produced = arguments[0].type == PURE_TYPE_F64_V1;
        if (produced) {
            double value = arguments[0].f64;
            result->f64 = isnan(value) ? value : copysign(1.0, value);
        }
        break;
    case PURE_F64_SUB_V1:
        produced = arguments[0].type == PURE_TYPE_F64_V1 &&
            arguments[1].type == PURE_TYPE_F64_V1;
        if (produced)
            result->f64 = arguments[0].f64 - arguments[1].f64;
        break;
    case PURE_F64_DIV_V1:
        produced = arguments[0].type == PURE_TYPE_F64_V1 &&
            arguments[1].type == PURE_TYPE_F64_V1;
        if (produced)
            result->f64 = arguments[0].f64 / arguments[1].f64;
        break;
    case PURE_F64_SUM_V1:
    case PURE_F64_PRODUCT_V1: {
        double accumulator = declaration->operation == PURE_F64_SUM_V1
            ? 0.0 : 1.0;
        for (size_t index = 0u; produced && index < argument_count; index++) {
            produced = arguments[index].type == PURE_TYPE_F64_V1;
            if (produced)
                accumulator = declaration->operation == PURE_F64_SUM_V1
                    ? accumulator + arguments[index].f64
                    : accumulator * arguments[index].f64;
        }
        result->f64 = accumulator;
        break;
    }
    }
    free(arguments);
    return produced
        ? CETTA_GSLT_PURE_PRODUCED_V1
        : CETTA_GSLT_PURE_DECLINED_V1;
}

CettaGsltPureOutcomeV1 cetta_gslt_pure_f64_evaluate_v1(
    Arena *arena, Atom *call, Atom **result,
    char *error, size_t error_size) {
    if (result)
        *result = NULL;
    if (!arena || !call || !result) {
        pure_error_v1(error, error_size, "invalid pure-provider request");
        return CETTA_GSLT_PURE_FAULT_V1;
    }
    if (!cetta_gslt_pure_f64_accepts_v1(call))
        return CETTA_GSLT_PURE_DECLINED_V1;
    PureValueV1 value = {0};
    CettaGsltPureOutcomeV1 outcome = pure_evaluate_value_v1(
        arena, call, &value, error, error_size);
    if (outcome != CETTA_GSLT_PURE_PRODUCED_V1)
        return outcome;
    if (value.type != PURE_TYPE_ATOM_V1 || !value.atom) {
        pure_error_v1(error, error_size,
                      "accepted pure-provider call produced the wrong type");
        return CETTA_GSLT_PURE_FAULT_V1;
    }
    *result = value.atom;
    return CETTA_GSLT_PURE_PRODUCED_V1;
}
