#ifndef CETTA_GSLT2PARSE_PARSER_ATOM_PROJECTION_CLOSURE_V1_H
#define CETTA_GSLT2PARSE_PARSER_ATOM_PROJECTION_CLOSURE_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "parser_atom_projection_v1.h"
#include "parser_pack_abi_v1.h"
#include "regular_span_dfa_v1.h"

typedef enum {
    PPATOM_CLOSURE_V1_COMPLETED = 0,
    PPATOM_CLOSURE_V1_SOURCE_STATE_LIMIT = 1,
    PPATOM_CLOSURE_V1_SOURCE_TRANSITION_LIMIT = 2,
    PPATOM_CLOSURE_V1_DOMAIN_STATE_LIMIT = 3,
    PPATOM_CLOSURE_V1_DOMAIN_TRANSITION_LIMIT = 4,
    PPATOM_CLOSURE_V1_INCLUSION_PAIR_LIMIT = 5
} PPAtomProjectionClosureV1Outcome;

typedef struct {
    char *label;
    uint32_t source_tag;
    bool included;
    uint32_t *counterexample;
    uint32_t counterexample_len;
    uint32_t explored_pair_len;
} PPAtomProjectionClosureV1Row;

typedef struct {
    PPAtomProjectionClosureV1Outcome outcome;
    bool all_included;
    PPAtomProjectionClosureV1Row *rows;
    uint32_t row_len;
    uint32_t wrapper_len;
    uint32_t source_root_tag_len;
    uint32_t source_state_len;
    uint32_t source_transition_len;
    uint32_t stopped_wrapper;
} PPAtomProjectionClosureV1Result;

void ppatom_projection_closure_v1_result_init(
    PPAtomProjectionClosureV1Result *result);
void ppatom_projection_closure_v1_result_free(
    PPAtomProjectionClosureV1Result *result);

/*
 * Check that the regular language of every semantic-text wrapper produced by
 * the compiled syntax relation is contained in the corresponding projection
 * domain.  Wrapper labels and semantic-text root tags must form a bijection;
 * reference tags are internal and do not participate in that correspondence.
 *
 * Structural corruption fails.  Resource exhaustion is a successful,
 * explicit non-certificate outcome.  Failed replacement is atomic.
 */
bool ppatom_projection_closure_v1_check(
    const PPABIV1Pack *class_pack,
    Atom *const *semantic_nfa_answers,
    size_t semantic_nfa_answer_len,
    const PPAtomProjectionV1Plan *projection,
    uint32_t state_limit,
    uint32_t transition_limit,
    uint32_t pair_limit,
    PPAtomProjectionClosureV1Result *out,
    char *error_buf,
    size_t error_buf_size);

#endif
