#ifndef CETTA_EXACT_ARITHMETIC_TO_EXTERNAL_CALL_TRANSFORM_V1_H
#define CETTA_EXACT_ARITHMETIC_TO_EXTERNAL_CALL_TRANSFORM_V1_H

#include "language_def_core_v1.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    CETTA_EXACT_ARITHMETIC_OP_ADD_V1 = 0,
    CETTA_EXACT_ARITHMETIC_OP_SUB_V1,
    CETTA_EXACT_ARITHMETIC_OP_MUL_V1,
    CETTA_EXACT_ARITHMETIC_OP_TQUOT_V1,
    CETTA_EXACT_ARITHMETIC_OP_FQUOT_V1,
    CETTA_EXACT_ARITHMETIC_OP_TREM_V1,
    CETTA_EXACT_ARITHMETIC_OP_FREM_V1,
    CETTA_EXACT_ARITHMETIC_OP_COUNT_V1
} CettaExactArithmeticOperationV1;

typedef struct {
    CettaExactArithmeticOperationV1 operation;
    uint32_t source_rewrite_index;
    CettaLdPatternV1 source_operation;
    CettaLdPatternV1 target_program;
} CettaExactArithmeticExternalCallEntryV1;

typedef struct {
    CettaExactArithmeticExternalCallEntryV1
        entries[CETTA_EXACT_ARITHMETIC_OP_COUNT_V1];
    uint32_t entry_len;
} CettaExactArithmeticExternalCallTransformV1;

typedef enum {
    CETTA_EXACT_ARITHMETIC_EXTERNAL_CALL_TRANSFORM_OK_V1 = 0,
    CETTA_EXACT_ARITHMETIC_EXTERNAL_CALL_TRANSFORM_BAD_ARGUMENT_V1,
    CETTA_EXACT_ARITHMETIC_EXTERNAL_CALL_TRANSFORM_UNSUPPORTED_SOURCE_V1,
    CETTA_EXACT_ARITHMETIC_EXTERNAL_CALL_TRANSFORM_UNSUPPORTED_TARGET_V1,
    CETTA_EXACT_ARITHMETIC_EXTERNAL_CALL_TRANSFORM_ALLOCATION_FAILURE_V1
} CettaExactArithmeticExternalCallTransformStatusV1;

typedef struct {
    bool guarded;
    const CettaLdTextV1 *provider_link;
} CettaExactArithmeticExternalCallProgramViewV1;

void cetta_exact_arithmetic_external_call_transform_v1_init(
    CettaExactArithmeticExternalCallTransformV1 *transform);
void cetta_exact_arithmetic_external_call_transform_v1_free(
    CettaExactArithmeticExternalCallTransformV1 *transform);

/*
 * Fixed-language Libcall legalization into ordinary target-language Pattern
 * values.  The source rewrite bodies identify each operation, and the target
 * grammar supplies every constructor used in the emitted programs, including
 * the target-level zero guard for partial operations.  Replacement is atomic:
 * failure leaves an existing output unchanged.
 */
bool cetta_exact_arithmetic_to_external_call_transform_v1(
    CettaExactArithmeticExternalCallTransformV1 *out,
    const CettaLanguageDefCoreV1 *source,
    const CettaLanguageDefCoreV1 *target,
    CettaExactArithmeticExternalCallTransformStatusV1 *status,
    char *error_buf,
    size_t error_buf_size);

/* Exact, non-owning view of the supported target-program layouts. */
bool cetta_exact_arithmetic_external_call_program_v1_inspect(
    const CettaLanguageDefCoreV1 *target,
    const CettaLdPatternV1 *program,
    CettaExactArithmeticExternalCallProgramViewV1 *view);

const char *cetta_exact_arithmetic_operation_v1_name(
    CettaExactArithmeticOperationV1 operation);
const char *cetta_exact_arithmetic_external_call_transform_status_v1_name(
    CettaExactArithmeticExternalCallTransformStatusV1 status);

#endif /* CETTA_EXACT_ARITHMETIC_TO_EXTERNAL_CALL_TRANSFORM_V1_H */
