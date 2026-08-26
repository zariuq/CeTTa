#ifndef CETTA_EXACT_INTEGER_THEORY_V1_H
#define CETTA_EXACT_INTEGER_THEORY_V1_H

#include "operational_language_def_v1.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Decoded payload of the exact-integer operation-interface document.  The raw
 * document deliberately carries a redundant signature; this decoder validates
 * it and retains only the operation identity plus its source occurrence.  It
 * supplies no arithmetic semantics and licenses no implementation.
 */
typedef enum {
    CETTA_EXACT_INTEGER_V1_ADD = 0,
    CETTA_EXACT_INTEGER_V1_SUB,
    CETTA_EXACT_INTEGER_V1_MUL,
    CETTA_EXACT_INTEGER_V1_TQUOT,
    CETTA_EXACT_INTEGER_V1_FQUOT,
    CETTA_EXACT_INTEGER_V1_TREM,
    CETTA_EXACT_INTEGER_V1_FREM,
    CETTA_EXACT_INTEGER_V1_OPERATION_COUNT
} CettaExactIntegerV1Operation;

typedef struct {
    CettaExactIntegerV1Operation operation;
    uint32_t source_byte_left;
    uint32_t source_byte_right;
} CettaExactIntegerV1Declaration;

typedef struct {
    CettaExactIntegerV1Declaration *declarations;
    uint32_t declaration_len;
} CettaExactIntegerTheoryV1;

typedef enum {
    CETTA_EXACT_INTEGER_THEORY_V1_OK = 0,
    CETTA_EXACT_INTEGER_THEORY_V1_BAD_ARGUMENT,
    CETTA_EXACT_INTEGER_THEORY_V1_MALFORMED_DOCUMENT,
    CETTA_EXACT_INTEGER_THEORY_V1_MALFORMED_DECLARATION,
    CETTA_EXACT_INTEGER_THEORY_V1_DUPLICATE_OPERATION,
    CETTA_EXACT_INTEGER_THEORY_V1_RESOURCE_LIMIT,
    CETTA_EXACT_INTEGER_THEORY_V1_ALLOCATION_FAILURE
} CettaExactIntegerTheoryV1Status;

void cetta_exact_integer_theory_v1_init(CettaExactIntegerTheoryV1 *theory);
void cetta_exact_integer_theory_v1_free(CettaExactIntegerTheoryV1 *theory);

/*
 * Decode one already-parsed interface document.  Replacement is atomic.
 * Subinterfaces are permitted; duplicate operation identities are not.
 */
bool cetta_exact_integer_theory_v1_decode(
    CettaExactIntegerTheoryV1 *out,
    const CettaOpLangV1SExpr *root,
    uint32_t work_limit,
    CettaExactIntegerTheoryV1Status *status,
    char *error_buf,
    size_t error_buf_size);

const char *cetta_exact_integer_v1_operation_name(
    CettaExactIntegerV1Operation operation);

bool cetta_exact_integer_v1_operation_is_partial(
    CettaExactIntegerV1Operation operation);

const char *cetta_exact_integer_theory_v1_status_name(
    CettaExactIntegerTheoryV1Status status);

#endif /* CETTA_EXACT_INTEGER_THEORY_V1_H */
