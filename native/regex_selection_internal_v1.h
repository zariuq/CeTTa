#ifndef CETTA_REGEX_SELECTION_INTERNAL_V1_H
#define CETTA_REGEX_SELECTION_INTERNAL_V1_H

#include "regex_selection_v1.h"
#include <stdatomic.h>
#include "scalar_trie.h"

struct CettaReProgramV1 {
    atomic_uint references;
    CettaReInstructionV1 *instructions;
    uint32_t instruction_len, entry, group_len;
    CettaReRangeV1 *ranges, *word_ranges;
    uint32_t range_len, word_range_len;
    char **group_names;
    uint32_t *ranks;
    CettaReFoldPairV1 *fold_pairs;
    uint32_t fold_pair_len;
    bool extended;
    CettaScalarTrie *literal_trie;
};

CettaScalarTrie *cetta_re_v1_literal_trie_build(const CettaReProgramV1 *);
CettaReResultV1 cetta_re_v1_literal_find(const CettaReProgramV1 *, const uint8_t *,
    size_t, size_t, CettaReModeV1, const CettaReExecutionPolicyV1 *);

CettaReResultV1 cetta_re_v1_extended_find(const CettaReProgramV1 *, const uint8_t *,
    size_t, size_t, CettaReModeV1, const CettaReExecutionPolicyV1 *);

#endif
