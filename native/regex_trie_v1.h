#ifndef CETTA_REGEX_TRIE_V1_H
#define CETTA_REGEX_TRIE_V1_H

#include "regex_selection_v1.h"
#include "scalar_trie.h"

typedef struct CettaReNameAutomatonV1 CettaReNameAutomatonV1;
/* Regular programs admit a deterministic subset-state interpretation for
 * name existence. Assertions see each entire original name. SAVE-bearing,
 * capture-dependent and nested-observation programs return false without
 * constructing an automaton and require ordinary selected filtering: raw
 * graph ingress alone does not certify valid capture endpoint order. */
bool cetta_re_v1_name_automaton(const CettaReProgramV1 *, CettaReModeV1,
    const CettaReExecutionPolicyV1 *, CettaReNameAutomatonV1 **);
const CettaTrieAutomaton *cetta_re_v1_name_automaton_view(CettaReNameAutomatonV1 *);
const void *cetta_re_v1_name_automaton_initial(const CettaReNameAutomatonV1 *);
CettaReOutcomeV1 cetta_re_v1_name_automaton_outcome(const CettaReNameAutomatonV1 *);
void cetta_re_v1_name_automaton_free(CettaReNameAutomatonV1 *);

/* Selection acceleration admits only a prefix tree of SPLIT/JUMP nodes,
 * followed by independent linear CHAR/JUMP branches ending MATCH or FAIL.
 * There are no observed captures beyond group zero, assertions, classes or
 * cycles. The general matcher remains the interpretation for all other
 * programs. Explicit policies observe trie transitions on admitted programs;
 * an interrupted transition sequence never publishes a provisional match. */
bool cetta_re_v1_literal_accelerated(const CettaReProgramV1 *);
CettaReResultV1 cetta_re_v1_find_reference_valid_utf8(const CettaReProgramV1 *,
    const uint8_t *, size_t, size_t, CettaReModeV1, const CettaReExecutionPolicyV1 *);

#endif
