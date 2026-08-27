#ifndef CETTA_EXTERNAL_CALL_TO_STRUCTURED_C_TRANSFORM_V1_H
#define CETTA_EXTERNAL_CALL_TO_STRUCTURED_C_TRANSFORM_V1_H

#include "exact_arithmetic_to_external_call_transform_v1.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    CettaLdPatternV1 target_program;
} CettaExternalCallStructuredCTransformV1;

typedef enum {
    CETTA_EXTERNAL_CALL_STRUCTURED_C_TRANSFORM_OK_V1 = 0,
    CETTA_EXTERNAL_CALL_STRUCTURED_C_TRANSFORM_BAD_ARGUMENT_V1,
    CETTA_EXTERNAL_CALL_STRUCTURED_C_TRANSFORM_UNSUPPORTED_SOURCE_V1,
    CETTA_EXTERNAL_CALL_STRUCTURED_C_TRANSFORM_UNSUPPORTED_TARGET_V1,
    CETTA_EXTERNAL_CALL_STRUCTURED_C_TRANSFORM_ALLOCATION_FAILURE_V1
} CettaExternalCallStructuredCTransformStatusV1;

void cetta_external_call_structured_c_transform_v1_init(
    CettaExternalCallStructuredCTransformV1 *transform);
void cetta_external_call_structured_c_transform_v1_free(
    CettaExternalCallStructuredCTransformV1 *transform);

/*
 * Fixed-language lowering from actual ExternalCallMachine program Patterns to
 * one actual StructuredC Program Pattern.  The source transform is the exact
 * output of the preceding stage; the supplied target language owns every
 * constructor placed in the result.  Replacement is atomic.
 */
bool cetta_external_call_to_structured_c_transform_v1(
    CettaExternalCallStructuredCTransformV1 *out,
    const CettaLanguageDefCoreV1 *external_call_language,
    const CettaLanguageDefCoreV1 *structured_c_language,
    const CettaExactArithmeticExternalCallTransformV1 *source,
    CettaExternalCallStructuredCTransformStatusV1 *status,
    char *error_buf,
    size_t error_buf_size);

const char *cetta_external_call_structured_c_transform_status_v1_name(
    CettaExternalCallStructuredCTransformStatusV1 status);

#endif /* CETTA_EXTERNAL_CALL_TO_STRUCTURED_C_TRANSFORM_V1_H */
