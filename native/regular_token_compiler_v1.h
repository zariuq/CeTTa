#ifndef CETTA_REGULAR_TOKEN_COMPILER_V1_H
#define CETTA_REGULAR_TOKEN_COMPILER_V1_H

#include "deterministic_equation_plan_v1.h"
#include "regular_span_dfa_v1.h"
#include "parser_pack_abi_v1.h"

typedef struct CettaRegularTokenCompilerV1 CettaRegularTokenCompilerV1;

/* Thompson construction is authored once as equations. Ordinary language
 * subtraction uses the interval-DFA product, then returns the same NFA wire.
 * The immutable class pack is borrowed for the lifetime of the compiler. */
bool cetta_regular_token_compiler_v1_load(
    const PPABIV1Pack *classes, uint32_t work_limit,
    CettaRegularTokenCompilerV1 **out, char *error, size_t error_size);
bool cetta_regular_token_compiler_v1_restore(
    const PPABIV1Pack *classes, uint32_t work_limit, const uint8_t *bytes,
    size_t length, bool audit, CettaRegularTokenCompilerV1 **out,
    char *error, size_t error_size);
bool cetta_regular_token_compiler_v1_store(const CettaRegularTokenCompilerV1 *,
    uint8_t **bytes, size_t *length, char *error, size_t error_size);
void cetta_regular_token_compiler_v1_free(CettaRegularTokenCompilerV1 *compiler);
bool cetta_regular_token_compiler_v1_answers(
    CettaRegularTokenCompilerV1 *compiler, Arena *arena,
    Atom *tag, Atom *regex, Atom **out, char *error, size_t error_size);
bool cetta_regular_token_compiler_v1_program(
    CettaRegularTokenCompilerV1 *compiler, Arena *arena,
    Atom *tag, Atom *regex, RSDFAV1Program *out,
    char *error, size_t error_size);

#endif
