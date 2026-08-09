#ifndef CETTA_GSLT_COMPILED_RUNTIME_H
#define CETTA_GSLT_COMPILED_RUNTIME_H

#include "gslt_horn_runtime.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct CettaGsltCompiledProgram CettaGsltCompiledProgram;

typedef struct {
    const uint8_t *bytes;
    size_t length;
    const char *sha256;
} CettaGsltCompiledInputV1;

bool cetta_gslt_compiled_program_load_v1(
    const CettaGsltCompiledInputV1 *input,
    CettaGsltCompiledProgram **out,
    char *error, size_t error_size);

void cetta_gslt_compiled_program_free(CettaGsltCompiledProgram *program);

size_t cetta_gslt_compiled_program_rule_count(
    const CettaGsltCompiledProgram *program);

/*
 * Fair worklist execution of the generated finite-Horn residual program.
 * The static rule plan is already parsed, arity-checked, and variable-slotted;
 * execution therefore neither reparses authored source nor copies clauses to
 * standardize them apart.  This remains a realization of the finite-Horn
 * projection, not a claim that every GSLT is intrinsically Horn-shaped.
 */
bool cetta_gslt_compiled_query_v1(
    const CettaGsltCompiledProgram *program,
    Arena *output_arena, Atom *query,
    CettaGsltHornLimits limits,
    CettaGsltHornResult *result,
    char *error, size_t error_size);

#endif /* CETTA_GSLT_COMPILED_RUNTIME_H */
