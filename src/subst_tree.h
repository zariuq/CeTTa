#ifndef CETTA_SUBST_TREE_H
#define CETTA_SUBST_TREE_H

#include "atom.h"
#include "match.h"
#include "term_universe.h"

/* Both native indexes serialize observations, not entire shared graphs.
 * Reserve one token for each immediate child before expanding an expression;
 * later cuts consume their reserved token as a wildcard. This bounds depth,
 * expression work and the total serialized path even for very wide nodes. */
enum {
    CETTA_INDEX_DEPTH_LIMIT = 8u,
    CETTA_INDEX_EXPRESSION_LIMIT = 128u,
    CETTA_INDEX_CHILD_TOKEN_LIMIT = 511u
};

typedef struct {
    unsigned expressions_left;
    unsigned child_tokens_left;
} CettaIndexExpansionBudget;

static inline CettaIndexExpansionBudget cetta_index_expansion_budget(void) {
    return (CettaIndexExpansionBudget){CETTA_INDEX_EXPRESSION_LIMIT,
                                       CETTA_INDEX_CHILD_TOKEN_LIMIT};
}

static inline bool cetta_index_expand_expression(CettaIndexExpansionBudget *budget,
                                                unsigned depth, CettaExprLen arity) {
    if (depth >= CETTA_INDEX_DEPTH_LIMIT || !budget->expressions_left ||
        arity > budget->child_tokens_left)
        return false;
    budget->expressions_left--;
    budget->child_tokens_left -= (unsigned)arity;
    return true;
}

/* Flat substitution-index queries need the same bounded observation window.
 * Larger queries use candidate selection and canonical matching instead. */
bool stree_query_within_expansion_budget(const Atom *query);

/* ── Substitution Tree (Space-internal fast-path for match) ──────────── *
 *
 * A discrimination tree whose variable edges are slots, head-partitioned by
 * outer expression symbol (like EqIndex).  It only selects rows.  A row's
 * variables are numbered by first occurrence: a variable edge is a new slot
 * or a slot met again.  A query's walk keeps the rows whose indexed terms
 * may unify with it: a query variable meets any indexed term; a slot's
 * first occurrence keeps the query subterm it meets, and the slot met again
 * must meet a subterm that unifies with that one, in a table that holds the
 * query's variables only.  A slot is no variable, so a query cannot name a
 * row's variable, and rows that differ only in their variables' names share
 * one path.  The rows are named by position, with no bindings: the consumer
 * renames each apart and matches it, for each use.  One stored row met
 * twice in one derivation -- by two conjuncts, or by a read nested in
 * another -- has a renaming of its own each time, and the tree keeps no
 * variable scope of its own: no row holds a frame identity.             */

/* ── SubstNode ─────────────────────────────────────────────────────────── */

/* ── Adaptive symbol indexing for high-fanout trie nodes ──────────────── *
 * Symbol children stay in a flat array while fan-out is small, then
 * promote to an open-addressing table once the node becomes wide.
 * Positive example: the eqtl-link node with tens of thousands of SNP
 * children. Negative example: every small trie node paying hash-table
 * overhead for four children. */

#define SNODE_HASH_THRESHOLD 16

/* A slot edge: the next slot on its path, the slot k again (k + 1), or a
 * placeholder, which meets any query term and keeps nothing. */
#define SNODE_SLOT_NEW 0u
#define SNODE_SLOT_PLACEHOLDER UINT32_MAX

typedef struct {
    SymbolId key;
    struct SubstNode *child;
} SubstSymBranch;

typedef struct {
    SymbolId key;
    struct SubstNode *child;
} SymHashEntry;

typedef struct {
    SymHashEntry *entries;  /* power-of-2 sized, SYMBOL_ID_NONE = empty slot */
    uint32_t mask;          /* table_size - 1 (for & masking) */
    uint32_t count;         /* number of occupied slots */
} SymHashTable;

typedef struct {
    int64_t val;
    struct SubstNode *child;
} IntHashEntry;

typedef struct {
    IntHashEntry *entries;  /* power-of-2 sized, child==NULL = empty slot
                               (every int64 value is a valid key, so occupancy
                               is tracked by the child pointer, not the key) */
    uint32_t mask;          /* table_size - 1 (for & masking) */
    uint32_t count;         /* number of occupied slots */
} IntHashTable;

typedef struct SubstNode {
    /* Symbol branches (SymbolId -> child) */
    SubstSymBranch *sym;
    uint32_t nsym, csym;
    SymHashTable sym_ht;
    bool sym_hashed;

    /* Slot edges: SNODE_SLOT_NEW, the slot k again as k + 1, or
       SNODE_SLOT_PLACEHOLDER for a value the tree does not discriminate. */
    struct {
        uint32_t edge;
        struct SubstNode *child;
    } *slots;
    uint32_t nslots, cslots;

    /* Expression branches (arity → child) */
    struct { CettaExprLen arity; struct SubstNode *child; } *expr;
    uint32_t nexpr, cexpr;

    /* Grounded int branches */
    struct { int64_t val; struct SubstNode *child; } *ints;
    uint32_t nints, cints;
    IntHashTable int_ht;
    bool int_hashed;

    /* Leaves: the positions of the rows filed under this path */
    CettaIndex *leaves;
    CettaIndex nleaves, cleaves;
} SubstNode;

/* ── SubstTree (head-partitioned) ──────────────────────────────────────── */

#define STREE_BUCKETS 256

typedef struct {
    SubstNode *root;
    CettaIndex count;
} SubstBucket;

typedef struct {
    SubstBucket buckets[STREE_BUCKETS];
    SubstBucket wildcard;   /* var-headed / non-expression atoms */
} SubstTree;

/* ── Query Result ──────────────────────────────────────────────────────── */

/* A row a space's query names.  An exact row carries the producer's
 * complete match, and `epoch` the renaming that match used, if it used one.
 * Any other row is named by position alone and carries no bindings: its
 * consumer renames it apart and matches it, for each use. */
typedef struct {
    CettaIndex atom_idx;    /* index into the logical space atom sequence */
    CettaFrameIdentity epoch; /* the renaming of the producer's match, if any */
    bool owns_identity;
    Bindings bindings;      /* the producer's match when exact, else empty */
    bool exact;             /* backend already proved this match exactly */
} SubstMatch;

typedef struct {
    SubstMatch *items;
    CettaIndex len, cap;
    SubstMatch inline_items[1];
} SubstMatchSet;

/* ── API ───────────────────────────────────────────────────────────────── */

SubstNode *snode_new(void);
void       snode_free(SubstNode *n);

void stree_init(SubstTree *t);
void stree_free(SubstTree *t);
void stree_insert(SubstTree *t, Atom *atom, CettaIndex atom_idx);
bool stree_insert_id(SubstTree *t, const TermUniverse *universe,
                     AtomId atom_id, CettaIndex atom_idx);
/* Transport retained leaf coordinates through one validated stable
 * occurrence contraction and restore every bucket's exact leaf count.
 * Returns the number of removed leaves. */
/* Remove the posting of row `idx` for an indexed atom, or move it to row
 * `to`; false when the posting is not in the tree. */
bool stree_remove_id(SubstTree *t, const TermUniverse *universe,
                     AtomId atom_id, CettaIndex idx);
bool stree_relabel_id(SubstTree *t, const TermUniverse *universe,
                      AtomId atom_id, CettaIndex from, CettaIndex to);
CettaCount stree_transport_stable_coordinates(
    SubstTree *t, const CettaIndex *source_to_target,
    CettaCount source_len);
void stree_bucket_init(SubstBucket *bucket);
void stree_bucket_free(SubstBucket *bucket);
void stree_bucket_insert(SubstBucket *bucket, Atom *atom, CettaIndex atom_idx);
bool stree_bucket_insert_id(SubstBucket *bucket, const TermUniverse *universe,
                            AtomId atom_id, CettaIndex atom_idx);

/* The rows of one bucket whose indexed terms may unify with QUERY,
 * appended by position to a vector the caller frees (as disc_lookup's
 * are), in the order of the walk.  No row carries bindings: the caller
 * renames each apart and matches it for every use. */
void stree_query_bucket(SubstBucket *bucket, Arena *a, Atom *query,
                        CettaIndex **rows, CettaIndex *nrows,
                        CettaIndex *crows);

/* Head symbol hash for partitioning (same bucket count as EqIndex) */
uint32_t stree_head_hash(SymbolId id);

void smset_init(SubstMatchSet *s);
void smset_free(SubstMatchSet *s);

#endif /* CETTA_SUBST_TREE_H */
