#ifndef CETTA_SPACE_H
#define CETTA_SPACE_H

#include "atom.h"
#include "name_key.h"
#include "match.h"
#include "subst_tree.h"
#include "term_universe.h"

/* ── Discrimination Trie (à la Vampire SubstitutionTree) ───────────────── */

#define DISC_HASH_THRESHOLD 16

typedef struct {
    SymbolId key;
    struct DiscNode *child;
} DiscSymBranch;

typedef struct {
    SymbolId key;
    struct DiscNode *child;
} DiscSymHashEntry;

typedef struct {
    DiscSymHashEntry *entries;
    uint32_t mask;
    uint32_t count;
} DiscSymHashTable;

typedef struct DiscNode {
    /* Symbol branches: name → child */
    DiscSymBranch *sym;
    uint32_t nsym, csym;
    DiscSymHashTable sym_ht;
    bool sym_hashed;
    /* Variable branch: wildcard matches anything */
    struct DiscNode *var_child;
    /* Expression branches: arity → child */
    struct { CettaExprLen arity; struct DiscNode *child; } *expr;
    uint32_t nexpr, cexpr;
    /* Grounded int branches */
    struct { int64_t val; struct DiscNode *child; } *ints;
    uint32_t nints, cints;
    /* Leaf data: indices of equations that match this path */
    CettaIndex *leaves;
    CettaIndex nleaves, cleaves;
} DiscNode;

DiscNode *disc_node_new(void);
void disc_node_free(DiscNode *n);
void disc_insert(DiscNode *root, Atom *lhs, CettaIndex eq_idx);
bool disc_insert_id(DiscNode *root, const TermUniverse *universe,
                    AtomId atom_id, CettaIndex eq_idx);
/* Collect all matching equation indices into result array */
void disc_lookup(DiscNode *root, Atom *query, CettaIndex **out,
                 CettaIndex *nout, CettaIndex *cout);

#include "space_match_backend.h"

/* ── Equation Index (head-symbol → equations, à la Vampire LiteralIndex) ── */

#define EQ_INDEX_BUCKETS 256

typedef struct {
    CettaIndex *atom_indices; /* indices into the logical atom sequence */
    CettaIndex len, cap;
    DiscNode *trie; /* discrimination trie over LHS patterns */
    SubstBucket subst; /* epoch-tagged substitution tree over LHS patterns */
    SymbolId head; /* exact head if the hash bucket is homogeneous */
    bool mixed_heads; /* true when distinct exact heads share this bucket */
    bool subst_safe; /* all LHS atoms are safe for the subst-tree fast path */
} EqBucket;

typedef struct {
    EqBucket buckets[EQ_INDEX_BUCKETS];
    EqBucket wildcard; /* equations with variable/expression LHS head */
} EqIndex;

/* ── Type Annotation Index (: atom type) → fast lookup ─────────────────── */

typedef struct {
    CettaIndex *atom_indices;  /* indices into the logical atom sequence */
    CettaIndex len, cap;
} TypeAnnBucket;

typedef struct {
    TypeAnnBucket buckets[EQ_INDEX_BUCKETS];
} TypeAnnIndex;

#define EXACT_INDEX_BUCKETS 4096

typedef struct {
    CettaIndex *indices;
    CettaIndex len, cap;
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

typedef struct {
    uint8_t *atom_ids;
    uint8_t atom_id_width_bits;
    CettaIndex start;
    CettaIndex len, cap;
    TermUniverse *universe;
    EqIndex eq_idx;      /* indexed equations for fast lookup */
    TypeAnnIndex ty_idx; /* indexed type annotations for fast lookup */
    ExactAtomIndex exact_idx; /* exact stable-atom membership index */
    bool eq_idx_dirty;
    bool ty_idx_dirty;
    bool exact_idx_dirty;
    bool has_non_exact_atoms;
    bool has_non_exact_atoms_dirty;
    uint32_t secondary_index_deferral_depth;
} SpaceNativeStorage;

typedef struct Space {
    union {
        SpaceNativeStorage native;
        struct {
            uint8_t *atom_ids;
            uint8_t atom_id_width_bits;
            CettaIndex start;
            CettaIndex len, cap;
            TermUniverse *universe;
            EqIndex eq_idx;
            TypeAnnIndex ty_idx;
            ExactAtomIndex exact_idx;
            bool eq_idx_dirty;
            bool ty_idx_dirty;
            bool exact_idx_dirty;
            bool has_non_exact_atoms;
            bool has_non_exact_atoms_dirty;
            uint32_t secondary_index_deferral_depth;
        };
    };
    SpaceKind kind;
    uint64_t revision;
    /* Space engine state is explicit so native, PathMap, and MORK lanes can
       share one runtime seam without confusing storage with execution. */
    SpaceMatchBackend match_backend;
    const struct Space *overlay_base;
    CettaIndex overlay_base_visible_len;
    CettaIndex *overlay_removed_base_indices;
    CettaIndex overlay_removed_base_len;
    CettaIndex overlay_removed_base_cap;
    uint64_t payload_owner_epoch;
    uint64_t payload_export_owner_epoch;
} Space;

void space_init_with_universe(Space *s, TermUniverse *universe);
void space_init_overlay(Space *s, const Space *base);
void space_init(Space *s);
void space_free(Space *s);
Atom *space_store_atom(Space *s, Arena *fallback, Atom *atom);
void space_add(Space *s, Atom *atom);
void space_add_atom_id(Space *s, AtomId atom_id);
bool space_admit_atom(Space *s, Arena *fallback, Atom *atom);
TermUniverseError space_term_universe_last_error_code(const Space *s);
void space_linearize(Space *s);
void space_mark_derived_state_dirty(Space *s);
void space_discard_native_logical_view(Space *s);
void space_note_external_backend_mutation(Space *s);
Space *space_heap_clone_shallow(Space *src);
void space_replace_contents(Space *dst, Space *src);
const char *space_kind_name(SpaceKind kind);
bool space_kind_from_name(const char *name, SpaceKind *out);
bool space_is_ordered(const Space *s);
bool space_is_stack(const Space *s);
bool space_is_queue(const Space *s);
bool space_is_hash(const Space *s);
AtomId space_get_atom_id_at(const Space *s, uint32_t idx);
CettaCount space_length64(const Space *s);
AtomId space_get_atom_id_at64(const Space *s, CettaIndex idx);
Atom *space_get_at64(const Space *s, CettaIndex idx);
Atom *space_get_at(const Space *s, uint32_t idx);
Atom *space_peek(const Space *s);
bool space_pop(Space *s, Atom **out);
bool space_truncate64(Space *s, CettaCount new_len);
bool space_truncate(Space *s, uint32_t new_len);
bool space_length_u32_checked(const Space *s, uint32_t *out_len);
static inline uint64_t space_revision(const Space *s) {
    return s ? s->revision : 0;
}
bool space_contains_exact(Space *s, Atom *atom);
CettaIndex space_exact_match_indices64(Space *s, Atom *atom, CettaIndex **out);
uint32_t space_exact_match_indices(Space *s, Atom *atom, uint32_t **out);
bool space_contains_only_exact_atoms(Space *s);
bool space_atom_is_exact_indexable(Atom *atom);
void space_begin_secondary_index_deferral(Space *s);
void space_end_secondary_index_deferral(Space *s);

/* ── Equation Query ─────────────────────────────────────────────────────── */

/* A single query result: the RHS with bindings applied, plus the bindings
   themselves (needed for propagating variable bindings to the caller). */
typedef struct {
    Atom *result;
    Bindings bindings;
} QueryResult;

typedef struct {
    QueryResult *items;
    CettaCount len, cap;
    QueryResult inline_items[1];
} QueryResults;

typedef bool (*QueryResultVisitor)(Atom *result, const Bindings *bindings,
                                   void *ctx);

void query_results_init(QueryResults *qr);
bool query_results_push(QueryResults *qr, Atom *result, Bindings *b);
bool query_results_push_move(QueryResults *qr, Atom *result, Bindings *b);
void query_results_diag_set_capacity_limit_override(CettaCount limit);
CettaCount query_equations_visit(Space *s, Atom *query, Arena *a,
                                 QueryResultVisitor visitor, void *ctx);
CettaCount query_results_visit(const QueryResults *qr,
                               QueryResultVisitor visitor, void *ctx);
void query_results_free(QueryResults *qr);

/* Find all (= lhs rhs) in space where lhs matches query (bidirectional).
   Returns substituted RHS for each match, plus bindings. */
void query_equations(Space *s, Atom *query, Arena *a, QueryResults *out);
bool space_equations_may_match_known_head(Space *s, SymbolId head);

/* ── Space Registry (named spaces) ─────────────────────────────────────── */

typedef struct {
    SymbolId key;
    NameId name_id;
    Atom *value;  /* Usually a grounded space atom, but can be anything */
} RegistryEntry;

typedef struct {
    RegistryEntry *entries;
    uint32_t len, cap;
    RegistryEntry inline_entries[16];
    NameKeyTable *name_keys;
    /* Open-addressed entry index. Slots store entry-index + 1; zero is empty.
       Iteration order remains the insertion order in entries. */
    uint32_t *index_slots;
    uint32_t index_cap;
    uint32_t inline_index_slots[32];
} Registry;

void registry_init(Registry *r);
void registry_free(Registry *r);
void registry_bind_id(Registry *r, SymbolId key, Atom *value);
Atom *registry_lookup_id(Registry *r, SymbolId key);
void registry_bind(Registry *r, const char *name, Atom *value);
Atom *registry_lookup(Registry *r, const char *name);
bool registry_bind_name(Registry *r, Atom *name_key, Atom *value);
Atom *registry_lookup_name(Registry *r, Atom *name_key);
const Atom *registry_entry_name_key(const Registry *r, uint32_t index);

/* Canonical structural reference: (resolve-name (quote <closed-key>)). */
bool registry_ref_name_key(Atom *ref, Atom **name_key_out);
Atom *registry_lookup_ref(Registry *r, Atom *ref);

/* Resolve a space reference (symbol like &self, &ws, or grounded space atom)
   to a Space pointer. Returns NULL if not a space. */
Space *resolve_space(Registry *r, Atom *ref);

/* Remove an atom from a space (by structural equality). Returns true if found. */
bool space_remove(Space *s, Atom *atom);
bool space_contains_atom_id(const Space *s, AtomId atom_id);
bool space_remove_atom_id(Space *s, AtomId atom_id);

/* ── Match Indexing ─────────────────────────────────────────────────────── */

/* Return candidate atom indices for a match pattern via discrimination trie.
   For small spaces (< MATCH_TRIE_THRESHOLD), returns all indices.
   Caller must free(*out). Returns count. */
CettaIndex space_match_candidates64(Space *s, Atom *pattern, CettaIndex **out);
uint32_t space_match_candidates(Space *s, Atom *pattern, uint32_t **out);

/* ── Substitution Tree Query ────────────────────────────────────────────── */

/* Find all atoms in space unifying with query, producing bindings directly.
   Replaces: space_match_candidates + rename_vars + match_atoms pipeline.
   Caller must free(out->items). */
void space_subst_query(Space *s, Arena *a, Atom *query, SubstMatchSet *out);
bool space_subst_match_with_seed(Space *space, Atom *pattern, const SubstMatch *sm,
                                 const Bindings *seed, Arena *a, Bindings *out);
void space_query_conjunction(Space *s, Arena *a, Atom **patterns, CettaExprLen npatterns,
                             const Bindings *seed, BindingSet *out);

/* ── Type Lookup (from HE spec Space.lean) ─────────────────────────────── */

/* Get intrinsic type for grounded atom: int/float→Number, bool→Bool, string→String */
Atom *get_grounded_type(Arena *a, Atom *atom);

/* Get all types for an atom from the space.
   Returns count; fills out_types (arena-allocated array). */
/* Reduce grounded operators inside a type expression (e.g. (+ 1 1) -> 2). */
Atom *normalize_type_expr(Arena *a, Atom *ty);
Atom *normalize_type_expr_head(Arena *a, Atom *ty);

uint32_t get_atom_types(Space *s, Arena *a, Atom *atom,
                        Atom ***out_types);
/* Optional logical-step budget for callers that must distinguish a complete
   type set from a prefix.  Ordinary HE inference keeps using the unbudgeted
   API above; Prime threads one instance through the complete inference tree. */
typedef struct CettaTypeInferenceBudget {
    /* Optional bound for partial producers reached during inference. Structural
       traversal itself is total over the finite input and only contributes to
       `work_steps_observed`. */
    bool steps_limited;
    uint64_t steps_remaining;
    uint64_t steps_spent;
    uint64_t work_steps_observed;
    uint32_t type_capacity;
    uint32_t max_depth_observed;
    bool complete;
    bool type_capacity_exhausted;
    bool evaluator_stack_exhausted;
    bool evaluator_capacity_exhausted;
    bool allow_marked_user_type_functions;
} CettaTypeInferenceBudget;

uint32_t get_atom_types_budgeted(Space *s, Arena *a, Atom *atom,
                                 Atom ***out_types,
                                 CettaTypeInferenceBudget *budget);
/* Same engine, but ignores an expression subject's own top-level (: term T)
   declaration. Used to audit a declaration against its structural derivation. */
uint32_t get_atom_types_structural(Space *s, Arena *a, Atom *atom,
                                   Atom ***out_types);
uint32_t get_atom_types_structural_budgeted(
    Space *s, Arena *a, Atom *atom, Atom ***out_types,
    CettaTypeInferenceBudget *budget);

#endif /* CETTA_SPACE_H */
