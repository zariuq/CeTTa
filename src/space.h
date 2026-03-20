#ifndef CETTA_SPACE_H
#define CETTA_SPACE_H

#include "atom.h"
#include "match.h"
#include "subst_tree.h"

/* ── Discrimination Trie (à la Vampire SubstitutionTree) ───────────────── */

typedef struct DiscNode {
    /* Symbol branches: name → child */
    struct { const char *key; struct DiscNode *child; } *sym;
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

/* ── Space ──────────────────────────────────────────────────────────────── */

#define MATCH_TRIE_THRESHOLD 16

typedef struct Space {
    Atom **atoms;
    uint32_t len, cap;
    EqIndex eq_idx;      /* indexed equations for fast lookup */
    TypeAnnIndex ty_idx; /* indexed type annotations for fast lookup */
    /* Matching backend is explicit so future PathMap/MORK import can slot in
       behind one seam instead of rewriting eval.c again. */
    SpaceMatchBackend match_backend;
} Space;

void space_init(Space *s);
void space_free(Space *s);
void space_add(Space *s, Atom *atom);

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

/* Find all (= lhs rhs) in space where lhs matches query (bidirectional).
   Returns substituted RHS for each match, plus bindings. */
void query_equations(Space *s, Atom *query, Arena *a, QueryResults *out);

/* ── Space Registry (named spaces) ─────────────────────────────────────── */

#define MAX_REGISTRY 64

typedef struct {
    const char *name;
    Atom *value;  /* Usually a grounded space atom, but can be anything */
} RegistryEntry;

typedef struct {
    RegistryEntry entries[MAX_REGISTRY];
    uint32_t len;
} Registry;

void registry_init(Registry *r);
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

/* ── Type Lookup (from HE spec Space.lean) ─────────────────────────────── */

/* Get intrinsic type for grounded atom: int/float→Number, bool→Bool, string→String */
Atom *get_grounded_type(Arena *a, Atom *atom);

/* Get all types for an atom from the space.
   Returns count; fills out_types (arena-allocated array). */
uint32_t get_atom_types(Space *s, Arena *a, Atom *atom,
                        Atom ***out_types);

#endif /* CETTA_SPACE_H */
