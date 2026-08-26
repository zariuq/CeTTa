#ifndef CETTA_C_SUBSET_EMIT_V1_H
#define CETTA_C_SUBSET_EMIT_V1_H

#include "c_subset_ir_v1.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
    CETTA_C_SUBSET_EMIT_V1_OK = 0,
    CETTA_C_SUBSET_EMIT_V1_BAD_ARGUMENT,
    CETTA_C_SUBSET_EMIT_V1_INVALID_TARGET,
    CETTA_C_SUBSET_EMIT_V1_IO_FAILURE
} CettaCSubsetEmitV1Status;

bool cetta_c_subset_emit_v1_header(
    FILE *output,
    const CettaCSubsetV1Module *module,
    const char *include_guard,
    CettaCSubsetEmitV1Status *status,
    char *error_buf,
    size_t error_buf_size);

bool cetta_c_subset_emit_v1_source(
    FILE *output,
    const CettaCSubsetV1Module *module,
    const char *generated_header_include,
    CettaCSubsetEmitV1Status *status,
    char *error_buf,
    size_t error_buf_size);

const char *cetta_c_subset_emit_v1_status_name(
    CettaCSubsetEmitV1Status status);

#endif /* CETTA_C_SUBSET_EMIT_V1_H */
