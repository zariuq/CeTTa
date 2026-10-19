#ifndef CETTA_SCALAR_TRIE_H
#define CETTA_SCALAR_TRIE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct CettaScalarTrie CettaScalarTrie;
#define CETTA_TRIE_NONE UINT32_MAX

/* Keys are sequences of Unicode scalars. Terminals are occurrences: adding
 * the same key twice retains both payloads. Node numbers remain stable while
 * growing. Mutation and traversal require external synchronization. */
CettaScalarTrie *cetta_scalar_trie_new(void);
void cetta_scalar_trie_free(CettaScalarTrie *);
uint32_t cetta_scalar_trie_child(const CettaScalarTrie *, uint32_t, uint32_t);
bool cetta_scalar_trie_extend(CettaScalarTrie *, uint32_t, uint32_t, uint32_t *);
bool cetta_scalar_trie_terminal(CettaScalarTrie *, uint32_t, uint64_t, size_t);
bool cetta_scalar_trie_insert_utf8(CettaScalarTrie *, const uint8_t *, size_t, uint64_t);
bool cetta_scalar_trie_decode(const uint8_t *, size_t, size_t, uint32_t *, size_t *);
bool cetta_scalar_trie_valid_utf8(const uint8_t *, size_t);

/* A read-only Aho--Corasick preparation. The summary at each state retains
 * the longest terminal suffix, then the smallest payload for equal lengths.
 * This is sufficient for minimum-start, then minimum-alternative selection;
 * it is not a longest-match policy at a fixed start. */
bool cetta_scalar_trie_prepare(CettaScalarTrie *);
uint32_t cetta_scalar_trie_advance(const CettaScalarTrie *, uint32_t, uint32_t);
bool cetta_scalar_trie_best(const CettaScalarTrie *, uint32_t, bool suffixes,
                           uint64_t *, size_t *);
size_t cetta_scalar_trie_max_length(const CettaScalarTrie *);

typedef struct {
    size_t state_size;
    void *context;
    /* step: -1 operational failure, 0 dead continuation, 1 live. */
    int (*step)(void *, const void *, uint32_t, void *);
    /* accepts: -1 operational failure, 0 reject, 1 accept. */
    int (*accepts)(void *, const void *);
} CettaTrieAutomaton;
typedef struct { uint64_t nodes, transitions, terminals; } CettaTrieWalkStats;
/* Product walk visits only live automaton/trie pairs. Initial state is
 * borrowed; destination states are owned by the traversal. Terminal payloads
 * are emitted once per inserted occurrence, never once per NFA path. */
bool cetta_scalar_trie_walk(const CettaScalarTrie *, const CettaTrieAutomaton *,
    const void *initial, bool (*emit)(void *, uint64_t), void *emit_context,
    CettaTrieWalkStats *);

#endif
