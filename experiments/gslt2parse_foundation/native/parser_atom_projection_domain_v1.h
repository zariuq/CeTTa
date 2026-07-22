#ifndef CETTA_GSLT2PARSE_PARSER_ATOM_PROJECTION_DOMAIN_V1_H
#define CETTA_GSLT2PARSE_PARSER_ATOM_PROJECTION_DOMAIN_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "parser_atom_projection_v1.h"
#include "regular_span_dfa_v1.h"

/*
 * Compile the exact flattened-codepoint input domain of one text wrapper to
 * a single-tag DFA program.  The resulting program is independent of the
 * wrapper's output mapping: it recognizes precisely those payload spines for
 * which projection is defined.  Failed or resource-limited replacement is
 * atomic.
 */
bool ppatom_projection_domain_v1_program(
    const PPAtomProjectionV1Plan *plan,
    uint32_t wrapper_index,
    uint32_t state_limit,
    uint32_t transition_limit,
    RSDFAV1Program *out,
    RSDFAV1BuildOutcome *outcome,
    char *error_buf,
    size_t error_buf_size);

#endif
