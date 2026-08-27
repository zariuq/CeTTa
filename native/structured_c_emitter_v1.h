#ifndef CETTA_STRUCTURED_C_EMITTER_V1_H
#define CETTA_STRUCTURED_C_EMITTER_V1_H

#include "language_def_core_v1.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef enum {
    CETTA_STRUCTURED_C_EMIT_OK_V1 = 0,
    CETTA_STRUCTURED_C_EMIT_BAD_ARGUMENT_V1,
    CETTA_STRUCTURED_C_EMIT_UNSUPPORTED_LANGUAGE_V1,
    CETTA_STRUCTURED_C_EMIT_UNSUPPORTED_PROGRAM_V1,
    CETTA_STRUCTURED_C_EMIT_IO_FAILURE_V1
} CettaStructuredCEmitStatusV1;

/*
 * Structural realization of one actual StructuredC Program Pattern.  The
 * emitter traverses target constructors directly; it accepts no operation
 * enum, compiler plan, or parallel instruction representation.
 */
bool cetta_structured_c_emit_v1(
    FILE *output,
    const CettaLanguageDefCoreV1 *language,
    const CettaLdPatternV1 *program,
    const char *abi_include,
    CettaStructuredCEmitStatusV1 *status,
    char *error_buf,
    size_t error_buf_size);

const char *cetta_structured_c_emit_status_v1_name(
    CettaStructuredCEmitStatusV1 status);

#endif /* CETTA_STRUCTURED_C_EMITTER_V1_H */
