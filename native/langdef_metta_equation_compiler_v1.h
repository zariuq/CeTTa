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

/* Compile an ordered family of authored equation presentations into one
 * MeTTa program.  Determinism is checked across presentation boundaries:
 * every left side is symbol-headed and left-linear, no two left sides
 * overlap, and every right-side variable is bound by the left side or a
 * lexical `let`. */
bool cetta_langdef_metta_equation_composition_v1(
    const uint8_t *const *canonical_presentations,
    const size_t *canonical_lens,
    size_t presentation_count,
    uint8_t **program_out,
    size_t *program_len_out,
    char *error,
    size_t error_size);

#endif
