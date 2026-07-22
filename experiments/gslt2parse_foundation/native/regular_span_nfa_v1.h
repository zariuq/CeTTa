#ifndef CETTA_GSLT2PARSE_REGULAR_SPAN_NFA_V1_H
#define CETTA_GSLT2PARSE_REGULAR_SPAN_NFA_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "atom.h"
#include "parser_pack_abi_v1.h"
#include "regular_span_dfa_v1.h"

typedef struct {
    RSDFAV1Nfa nfa;
    Atom **tags;
    char **tag_canonical;
    Atom **states;
    char **state_canonical;
    uint32_t *start_states;
    RSDFAV1NfaEdge *edges;
    CettaLpNativeUnicodeRange **owned_ranges;
    RSDFAV1NfaAccept *accepts;
} RSNFAV1Plan;

void rsnfa_v1_plan_init(RSNFAV1Plan *plan);
void rsnfa_v1_plan_free(RSNFAV1Plan *plan);

bool rsnfa_v1_plan_load(const PPABIV1Pack *class_pack,
                        Atom *const *answer_terms,
                        size_t answer_len,
                        RSNFAV1Plan *out,
                        char *error_buf,
                        size_t error_buf_size);

#endif
