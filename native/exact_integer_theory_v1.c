#include "exact_integer_theory_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    const char *wire_tag;
    bool is_partial;
} ExactIntegerOperationSpec;

static const ExactIntegerOperationSpec exact_integer_specs[] = {
    {"exact-int:add", "CoreAdd", false},
    {"exact-int:sub", "CoreSub", false},
    {"exact-int:mul", "CoreMul", false},
    {"exact-int:tquot", "CoreTQuot", true},
    {"exact-int:fquot", "CoreFQuot", true},
    {"exact-int:trem", "CoreTRem", true},
    {"exact-int:frem", "CoreFRem", true},
};

static void exact_integer_set_status(
    CettaExactIntegerTheoryV1Status *status,
    CettaExactIntegerTheoryV1Status value) {
    if (status)
        *status = value;
}

static void exact_integer_set_error(char *buf,
                                    size_t size,
                                    const char *format,
                                    ...) {
    va_list arguments;

    if (!buf || size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(buf, size, format, arguments);
    va_end(arguments);
}

static bool exact_integer_consume(uint32_t *work,
                                  uint32_t work_limit,
                                  CettaExactIntegerTheoryV1Status *status,
                                  char *error_buf,
                                  size_t error_buf_size) {
    if (!work || *work >= work_limit) {
        exact_integer_set_status(
            status, CETTA_EXACT_INTEGER_THEORY_V1_RESOURCE_LIMIT);
        exact_integer_set_error(
            error_buf, error_buf_size,
            "exact-integer theory decode work limit reached");
        return false;
    }
    (*work)++;
    return true;
}

static bool exact_integer_string_equals(const CettaOpLangV1SExpr *term,
                                        const char *expected) {
    size_t len;

    if (!term || !expected ||
        term->kind != CETTA_OP_LANG_V1_SEXPR_STRING) {
        return false;
    }
    len = strlen(expected);
    return term->as.string.len == len &&
        (len == 0u ||
         (term->as.string.bytes &&
          memcmp(term->as.string.bytes, expected, len) == 0));
}

static bool exact_integer_inputs_are_binary_integer(
    const CettaOpLangV1SExpr *term,
    uint32_t *work,
    uint32_t work_limit,
    CettaExactIntegerTheoryV1Status *status,
    char *error_buf,
    size_t error_buf_size) {
    const CettaOpLangV1SExpr *tail;

    if (!exact_integer_consume(
            work, work_limit, status, error_buf, error_buf_size)) {
        return false;
    }
    if (!cetta_op_lang_v1_application_is(term, "LCons", 2u) ||
        !cetta_op_lang_v1_symbol_is(
            term->as.application.arguments[0], "NumInteger")) {
        return false;
    }
    tail = term->as.application.arguments[1];
    if (!exact_integer_consume(
            work, work_limit, status, error_buf, error_buf_size)) {
        return false;
    }
    if (!cetta_op_lang_v1_application_is(tail, "LCons", 2u) ||
        !cetta_op_lang_v1_symbol_is(
            tail->as.application.arguments[0], "NumInteger")) {
        return false;
    }
    if (!exact_integer_consume(
            work, work_limit, status, error_buf, error_buf_size)) {
        return false;
    }
    return cetta_op_lang_v1_symbol_is(
        tail->as.application.arguments[1], "LNil");
}

static bool exact_integer_operation_from_wire(
    const CettaOpLangV1SExpr *term,
    CettaExactIntegerV1Operation *operation) {
    uint32_t index;

    if (!term || !operation)
        return false;
    for (index = 0u;
         index < (uint32_t)CETTA_EXACT_INTEGER_V1_OPERATION_COUNT;
         index++) {
        if (cetta_op_lang_v1_symbol_is(
                term, exact_integer_specs[index].wire_tag)) {
            *operation = (CettaExactIntegerV1Operation)index;
            return true;
        }
    }
    return false;
}

static bool exact_integer_decode_declaration(
    const CettaOpLangV1SExpr *term,
    CettaExactIntegerV1Declaration *out,
    uint32_t *work,
    uint32_t work_limit,
    CettaExactIntegerTheoryV1Status *status,
    char *error_buf,
    size_t error_buf_size) {
    CettaExactIntegerV1Operation operation;
    const ExactIntegerOperationSpec *spec;
    const CettaOpLangV1SExpr *partial;

    if (!exact_integer_consume(
            work, work_limit, status, error_buf, error_buf_size)) {
        return false;
    }
    if (!term || !out ||
        !cetta_op_lang_v1_application_is(
            term, "ExactIntegerOperation", 5u) ||
        !exact_integer_operation_from_wire(
            term->as.application.arguments[4], &operation)) {
        exact_integer_set_status(
            status, CETTA_EXACT_INTEGER_THEORY_V1_MALFORMED_DECLARATION);
        exact_integer_set_error(
            error_buf, error_buf_size,
            "expected a known five-field ExactIntegerOperation declaration");
        return false;
    }
    spec = &exact_integer_specs[(uint32_t)operation];
    partial = term->as.application.arguments[3];
    if (!exact_integer_string_equals(
            term->as.application.arguments[0], spec->name) ||
        !exact_integer_inputs_are_binary_integer(
            term->as.application.arguments[1], work, work_limit,
            status, error_buf, error_buf_size) ||
        !cetta_op_lang_v1_symbol_is(
            term->as.application.arguments[2], "NumInteger") ||
        !cetta_op_lang_v1_symbol_is(
            partial, spec->is_partial ? "BoolTrue" : "BoolFalse")) {
        if (status &&
            *status == CETTA_EXACT_INTEGER_THEORY_V1_RESOURCE_LIMIT) {
            return false;
        }
        exact_integer_set_status(
            status, CETTA_EXACT_INTEGER_THEORY_V1_MALFORMED_DECLARATION);
        exact_integer_set_error(
            error_buf, error_buf_size,
            "exact-integer declaration disagrees with its operation identity");
        return false;
    }
    out->operation = operation;
    out->source_byte_left = term->byte_left;
    out->source_byte_right = term->byte_right;
    return true;
}

void cetta_exact_integer_theory_v1_init(CettaExactIntegerTheoryV1 *theory) {
    if (theory)
        memset(theory, 0, sizeof(*theory));
}

void cetta_exact_integer_theory_v1_free(CettaExactIntegerTheoryV1 *theory) {
    if (!theory)
        return;
    free(theory->declarations);
    memset(theory, 0, sizeof(*theory));
}

bool cetta_exact_integer_theory_v1_decode(
    CettaExactIntegerTheoryV1 *out,
    const CettaOpLangV1SExpr *root,
    uint32_t work_limit,
    CettaExactIntegerTheoryV1Status *status,
    char *error_buf,
    size_t error_buf_size) {
    CettaExactIntegerTheoryV1 candidate;
    const CettaOpLangV1SExpr *cursor;
    bool seen[CETTA_EXACT_INTEGER_V1_OPERATION_COUNT] = {false};
    uint32_t work = 0u;
    uint32_t length = 0u;
    uint32_t index;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    exact_integer_set_status(status, CETTA_EXACT_INTEGER_THEORY_V1_OK);
    if (!out || !root || work_limit == 0u) {
        exact_integer_set_status(
            status, CETTA_EXACT_INTEGER_THEORY_V1_BAD_ARGUMENT);
        exact_integer_set_error(
            error_buf, error_buf_size,
            "bad exact-integer theory decode arguments");
        return false;
    }
    cetta_exact_integer_theory_v1_init(&candidate);
    if (!exact_integer_consume(
            &work, work_limit, status, error_buf, error_buf_size)) {
        return false;
    }
    if (!cetta_op_lang_v1_application_is(
            root, "ExactIntegerTheoryV1", 1u)) {
        exact_integer_set_status(
            status, CETTA_EXACT_INTEGER_THEORY_V1_MALFORMED_DOCUMENT);
        exact_integer_set_error(
            error_buf, error_buf_size,
            "expected (ExactIntegerTheoryV1 declarations)");
        return false;
    }
    cursor = root->as.application.arguments[0];
    while (!cetta_op_lang_v1_symbol_is(cursor, "LNil")) {
        if (!exact_integer_consume(
                &work, work_limit, status, error_buf, error_buf_size)) {
            goto fail;
        }
        if (!cetta_op_lang_v1_application_is(cursor, "LCons", 2u)) {
            exact_integer_set_status(
                status, CETTA_EXACT_INTEGER_THEORY_V1_MALFORMED_DOCUMENT);
            exact_integer_set_error(
                error_buf, error_buf_size,
                "exact-integer declarations must be a canonical list");
            goto fail;
        }
        if (length == UINT32_MAX) {
            exact_integer_set_status(
                status, CETTA_EXACT_INTEGER_THEORY_V1_RESOURCE_LIMIT);
            exact_integer_set_error(
                error_buf, error_buf_size,
                "exact-integer declaration count exceeds uint32 range");
            goto fail;
        }
        length++;
        cursor = cursor->as.application.arguments[1];
    }
    if (length > 0u) {
        candidate.declarations = calloc(
            length, sizeof(*candidate.declarations));
        if (!candidate.declarations) {
            exact_integer_set_status(
                status, CETTA_EXACT_INTEGER_THEORY_V1_ALLOCATION_FAILURE);
            exact_integer_set_error(
                error_buf, error_buf_size,
                "failed to allocate exact-integer theory declarations");
            goto fail;
        }
    }
    candidate.declaration_len = length;
    cursor = root->as.application.arguments[0];
    for (index = 0u; index < length; index++) {
        const CettaOpLangV1SExpr *declaration =
            cursor->as.application.arguments[0];
        uint32_t operation;

        if (!exact_integer_decode_declaration(
                declaration, &candidate.declarations[index],
                &work, work_limit, status, error_buf, error_buf_size)) {
            goto fail;
        }
        operation = (uint32_t)candidate.declarations[index].operation;
        if (seen[operation]) {
            exact_integer_set_status(
                status, CETTA_EXACT_INTEGER_THEORY_V1_DUPLICATE_OPERATION);
            exact_integer_set_error(
                error_buf, error_buf_size,
                "duplicate exact-integer operation identity '%s'",
                exact_integer_specs[operation].name);
            goto fail;
        }
        seen[operation] = true;
        cursor = cursor->as.application.arguments[1];
    }
    cetta_exact_integer_theory_v1_free(out);
    *out = candidate;
    memset(&candidate, 0, sizeof(candidate));
    exact_integer_set_status(status, CETTA_EXACT_INTEGER_THEORY_V1_OK);
    return true;

fail:
    cetta_exact_integer_theory_v1_free(&candidate);
    return false;
}

const char *cetta_exact_integer_v1_operation_name(
    CettaExactIntegerV1Operation operation) {
    uint32_t index = (uint32_t)operation;

    if (index >= (uint32_t)CETTA_EXACT_INTEGER_V1_OPERATION_COUNT)
        return "<invalid-exact-integer-operation>";
    return exact_integer_specs[index].name;
}

bool cetta_exact_integer_v1_operation_is_partial(
    CettaExactIntegerV1Operation operation) {
    uint32_t index = (uint32_t)operation;

    return index < (uint32_t)CETTA_EXACT_INTEGER_V1_OPERATION_COUNT &&
        exact_integer_specs[index].is_partial;
}

const char *cetta_exact_integer_theory_v1_status_name(
    CettaExactIntegerTheoryV1Status status) {
    switch (status) {
    case CETTA_EXACT_INTEGER_THEORY_V1_OK:
        return "ok";
    case CETTA_EXACT_INTEGER_THEORY_V1_BAD_ARGUMENT:
        return "bad-argument";
    case CETTA_EXACT_INTEGER_THEORY_V1_MALFORMED_DOCUMENT:
        return "malformed-document";
    case CETTA_EXACT_INTEGER_THEORY_V1_MALFORMED_DECLARATION:
        return "malformed-declaration";
    case CETTA_EXACT_INTEGER_THEORY_V1_DUPLICATE_OPERATION:
        return "duplicate-operation";
    case CETTA_EXACT_INTEGER_THEORY_V1_RESOURCE_LIMIT:
        return "resource-limit";
    case CETTA_EXACT_INTEGER_THEORY_V1_ALLOCATION_FAILURE:
        return "allocation-failure";
    }
    return "unknown";
}
