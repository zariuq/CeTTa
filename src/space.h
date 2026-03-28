#ifndef CETTA_SPACE_H
#define CETTA_SPACE_H

#include "atom.h"
#include "match.h"
#include "subst_tree.h"

/* ── Discrimination Trie (à la Vampire SubstitutionTree) ───────────────── */

typedef struct DiscNode {
    /* Symbol branches: name → child */
    struct { SymbolId key; struct DiscNode *child; } *sym;
    uint32_t nsym, csym;
    /* Variable branch: wildcard matches anything */
    struct DiscNode *var_child;
    /* Expression branches: arity → child */
    struct { uint32_t arity; struct DiscNode *child; } *expr;
    uint32_t nexpr, cexpr;
    /* Grounded int branches */
    struct { int64_t val; struct DiscNode *child; } *ints;
    uint32_t nints, cints;
    /* Leaf data: indices of equations that match this path */
    uint32_t *leaves;
    uint32_t nleaves, cleaves;
} DiscNode;

DiscNode *disc_node_new(void);
void disc_node_free(DiscNode *n);
void disc_insert(DiscNode *root, Atom *lhs, uint32_t eq_idx);
/* Collect all matching equation indices into result array */
void disc_lookup(DiscNode *root, Atom *query, uint32_t **out, uint32_t *nout, uint32_t *cout);

#include "space_match_backend.h"

/* ── Equation Index (head-symbol → equations, à la Vampire LiteralIndex) ── */

#define EQ_INDEX_BUCKETS 256

typedef struct {
    Atom **lhs;   /* equation LHS atoms */
    Atom **rhs;   /* equation RHS atoms */
    uint32_t len, cap;
    DiscNode *trie; /* discrimination trie over LHS patterns */
    SubstBucket subst; /* epoch-tagged substitution tree over LHS patterns */
    bool subst_safe; /* all LHS atoms are safe for the subst-tree fast path */
} EqBucket;

typedef struct {
    EqBucket buckets[EQ_INDEX_BUCKETS];
    EqBucket wildcard; /* equations with variable/expression LHS head */
} EqIndex;

/* ── Type Annotation Index (: atom type) → fast lookup ─────────────────── */

typedef struct {
    Atom **annotated_atoms;  /* the atom in (: atom type) */
    Atom **types;            /* the type in (: atom type) */
    uint32_t len, cap;
} TypeAnnBucket;

typedef struct {
    TypeAnnBucket buckets[EQ_INDEX_BUCKETS];
} TypeAnnIndex;

#define EXACT_INDEX_BUCKETS 4096

typedef struct {
    uint32_t *indices;
    uint32_t len, cap;
} ExactAtomBucket;

typedef struct {
    ExactAtomBucket buckets[EXACT_INDEX_BUCKETS];
} ExactAtomIndex;

typedef enum {
    SPACE_KIND_ATOM = 0,
    SPACE_KIND_STACK = 1,
    SPACE_KIND_QUEUE = 2,
    SPACE_KIND_HASH = 3,
} SpaceKind;

/* ── Space ──────────────────────────────────────────────────────────────── */

#define MATCH_TRIE_THRESHOLD 16

typedef struct Space {
    Atom **atoms;
    uint32_t start;
    uint32_t len, cap;
    SpaceKind kind;
    EqIndex eq_idx;      /* indexed equations for fast lookup */
    TypeAnnIndex ty_idx; /* indexed type annotations for fast lookup */
    ExactAtomIndex exact_idx; /* exact stable-atom membership index */
    bool eq_idx_dirty;
    bool ty_idx_dirty;
    bool exact_idx_dirty;
    /* Matching backend is explicit so future PathMap/MORK import can slot in
       behind one seam instead of rewriting eval.c again. */
    SpaceMatchBackend match_backend;
} Space;

void space_init(Space *s);
void space_free(Space *s);
void space_add(Space *s, Atom *atom);
void space_linearize(Space *s);
Space *space_heap_clone_shallow(Space *src);
void space_replace_contents(Space *dst, Space *src);
const char *space_kind_name(SpaceKind kind);
bool space_kind_from_name(const char *name, SpaceKind *out);
bool space_is_ordered(const Space *s);
bool space_is_stack(const Space *s);
bool space_is_queue(const Space *s);
bool space_is_hash(const Space *s);
Atom *space_get_at(const Space *s, uint32_t idx);
Atom *space_peek(const Space *s);
bool space_pop(Space *s, Atom **out);
bool space_truncate(Space *s, uint32_t new_len);
uint32_t space_length(const Space *s);
bool space_contains_exact(Space *s, Atom *atom);
uint32_t space_exact_match_indices(Space *s, Atom *atom, uint32_t **out);

/* ── Equation Query ─────────────────────────────────────────────────────── */

/* A single query result: the RHS with bindings applied, plus the bindings
   themselves (needed for propagating variable bindings to the caller). */
typedef struct {
    Atom *result;
    Bindings bindings;
} QueryResult;

typedef struct {
    QueryResult *items;
    uint32_t len, cap;
} QueryResults;

void query_results_init(QueryResults *qr);
void query_results_push(QueryResults *qr, Atom *result, Bindings *b);
void query_results_push_move(QueryResults *qr, Atom *result, Bindings *b);
void query_results_free(QueryResults *qr);

/* Find all (= lhs rhs) in space where lhs matches query (bidirectional).
   Returns substituted RHS for each match, plus bindings. */
void query_equations(Space *s, Atom *query, Arena *a, QueryResults *out);

/* ── Space Registry (named spaces) ─────────────────────────────────────── */

#define MAX_REGISTRY 64

typedef struct {
    SymbolId key;
    Atom *value;  /* Usually a grounded space atom, but can be anything */
} RegistryEntry;

typedef struct {
    RegistryEntry entries[MAX_REGISTRY];
    uint32_t len;
} Registry;

void registry_init(Registry *r);
void registry_bind_id(Registry *r, SymbolId key, Atom *value);
Atom *registry_lookup_id(Registry *r, SymbolId key);
void registry_bind(Registry *r, const char *name, Atom *value);
Atom *registry_lookup(Registry *r, const char *name);

/* Resolve a space reference (symbol like &self, &ws, or grounded space atom)
   to a Space pointer. Returns NULL if not a space. */
Space *resolve_space(Registry *r, Atom *ref);

/* Remove an atom from a space (by structural equality). Returns true if found. */
bool space_remove(Space *s, Atom *atom);

/* ── Match Indexing ─────────────────────────────────────────────────────── */

/* Return candidate atom indices for a match pattern via discrimination trie.
   For small spaces (< MATCH_TRIE_THRESHOLD), returns all indices.
   Caller must free(*out). Returns count. */
uint32_t space_match_candidates(Space *s, Atom *pattern, uint32_t **out);

/* ── Substitution Tree Query ────────────────────────────────────────────── */

/* Find all atoms in space unifying with query, producing bindings directly.
   Replaces: space_match_candidates + rename_vars + match_atoms pipeline.
   Caller must free(out->items). */
void space_subst_query(Space *s, Arena *a, Atom *query, SubstMatchSet *out);
bool space_subst_match_with_seed(Space *space, Atom *pattern, const SubstMatch *sm,
                                 const Bindings *seed, Arena *a, Bindings *out);
void space_query_conjunction(Space *s, Arena *a, Atom **patterns, uint32_t npatterns,
                             const Bindings *seed, BindingSet *out);

/* ── Type Lookup (from HE spec Space.lean) ─────────────────────────────── */

/* Get intrinsic type for grounded atom: int/float→Number, bool→Bool, string→String */
Atom *get_grounded_type(Arena *a, Atom *atom);

/* Get all types for an atom from the space.
   Returns count; fills out_types (arena-allocated array). */
uint32_t get_atom_types(Space *s, Arena *a, Atom *atom,
                        Atom ***out_types);

#endif /* CETTA_SPACE_H */
