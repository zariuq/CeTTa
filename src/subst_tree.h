#ifndef CETTA_SUBST_TREE_H
#define CETTA_SUBST_TREE_H

#include "atom.h"
#include "match.h"

/* ── Substitution Tree (Space-internal fast-path for match) ──────────── *
 *
 * A discrimination tree with named variable branches that produces
 * bindings during the walk — replacing the rename_vars + match_atoms
 * pipeline. Head-partitioned by outer expression symbol (like EqIndex).
 *
 * Epoch-based standardization apart: each inserted atom gets a unique
 * epoch. Output bindings tag indexed-side variable names with the epoch
 * ($name#epoch), eliminating per-candidate rename_vars deep-copy.       */

/* ── SubstNode ─────────────────────────────────────────────────────────── */

/* ── Adaptive symbol indexing for high-fanout trie nodes ──────────────── *
 * Symbol children stay in a flat array while fan-out is small, then
 * promote to an open-addressing table once the node becomes wide.
 * Positive example: the eqtl-link node with tens of thousands of SNP
 * children. Negative example: every small trie node paying hash-table
 * overhead for four children. */

#define SNODE_HASH_THRESHOLD 16

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

typedef struct SubstNode {
    /* Symbol branches (SymbolId -> child) */
    SubstSymBranch *sym;
    uint32_t nsym, csym;
    SymHashTable sym_ht;
    bool sym_hashed;

    /* Variable branches — each indexed variable id gets its own
       branch so retrieval can produce bindings during the walk. */
    struct { VarId var_id; SymbolId spelling; struct SubstNode *child; } *vars;
    uint32_t nvars, cvars;

    /* Expression branches (arity → child) */
    struct { uint32_t arity; struct SubstNode *child; } *expr;
    uint32_t nexpr, cexpr;

    /* Grounded int branches */
    struct { int64_t val; struct SubstNode *child; } *ints;
    uint32_t nints, cints;

    /* Leaves: (space atom index, epoch for standardization apart) */
    struct { uint32_t idx; uint32_t epoch; } *leaves;
    uint32_t nleaves, cleaves;
} SubstNode;

/* ── SubstTree (head-partitioned) ──────────────────────────────────────── */

#define STREE_BUCKETS 256

typedef struct {
    SubstNode *root;
    uint32_t count;
} SubstBucket;

typedef struct {
    SubstBucket buckets[STREE_BUCKETS];
    SubstBucket wildcard;   /* var-headed / non-expression atoms */
} SubstTree;

/* ── Query Result ──────────────────────────────────────────────────────── */

typedef struct {
    uint32_t atom_idx;      /* index into space->atoms[] */
    uint32_t epoch;         /* standardization-apart epoch for this indexed atom */
    Bindings bindings;      /* epoch-tagged, ready for bindings_apply */
    bool exact;             /* backend already proved this match exactly */
} SubstMatch;

typedef struct {
    SubstMatch *items;
    uint32_t len, cap;
} SubstMatchSet;

/* ── API ───────────────────────────────────────────────────────────────── */

SubstNode *snode_new(void);
void       snode_free(SubstNode *n);

void stree_init(SubstTree *t);
void stree_free(SubstTree *t);
void stree_insert(SubstTree *t, Atom *atom, uint32_t atom_idx);
void stree_bucket_init(SubstBucket *bucket);
void stree_bucket_free(SubstBucket *bucket);
void stree_bucket_insert(SubstBucket *bucket, Atom *atom, uint32_t atom_idx);

/* Full unification retrieval: walk tree with query, produce bindings.
   Caller must free(out->items). */
/* Query a single bucket (called from space_subst_query) */
void stree_query_bucket(SubstBucket *bucket, Arena *a, Atom *query,
                        Atom **space_atoms, SubstMatchSet *out);

/* Head symbol hash for partitioning (same bucket count as EqIndex) */
uint32_t stree_head_hash(SymbolId id);

void smset_init(SubstMatchSet *s);
void smset_free(SubstMatchSet *s);

/* Global epoch counter for standardization apart */
uint32_t stree_next_epoch(void);

#endif /* CETTA_SUBST_TREE_H */
