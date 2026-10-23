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
    uint64_t first_ascii[2];
    bool first_ascii_valid;
};

/* Private operation-owned storage, initialized with {0}. This owner must not
 * be copied while live. It contains no borrowed input or program and may be
 * reused after any outcome; every search resets its logical state. Results
 * own their capture arrays independently. Literal/extended routes stay lazy. */
typedef struct {
    size_t instructions, slots;
    uint8_t *seen;
    uint32_t *stack, *pc[2];
    size_t *stack_captures, *working, *captures[2], *initial, *best;
} CettaReWorkspaceV1;
void cetta_re_v1_workspace_free(CettaReWorkspaceV1 *);
CettaReResultV1 cetta_re_v1_find_workspace_valid_utf8(const CettaReProgramV1 *,
    const uint8_t *, size_t, size_t, CettaReModeV1,
    const CettaReExecutionPolicyV1 *, CettaReWorkspaceV1 *);
CettaReResultV1 cetta_re_v1_next_workspace(const CettaReProgramV1 *, CettaReCursorV1 *,
    const CettaReExecutionPolicyV1 *, CettaReWorkspaceV1 *);

CettaScalarTrie *cetta_re_v1_literal_trie_build(const CettaReProgramV1 *);
CettaReResultV1 cetta_re_v1_literal_find(const CettaReProgramV1 *, const uint8_t *,
    size_t, size_t, CettaReModeV1, const CettaReExecutionPolicyV1 *);

CettaReResultV1 cetta_re_v1_extended_find(const CettaReProgramV1 *, const uint8_t *,
    size_t, size_t, CettaReModeV1, const CettaReExecutionPolicyV1 *);

#endif
