#ifndef CETTA_LANGDEF_METTA_EQUATION_COMPILER_V1_H
#define CETTA_LANGDEF_METTA_EQUATION_COMPILER_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool cetta_langdef_metta_equations_v1(
    const uint8_t *canonical_presentation,
    size_t canonical_len,
    uint8_t **program_out,
    size_t *program_len_out,
    char *error,
    size_t error_size);

#endif
