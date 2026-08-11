#ifndef CETTA_SPACE_H
#define CETTA_SPACE_H

#include "atom.h"
#include "generated/cetta_execution_contracts.generated.h"
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
    /* Captured logical-order keys.  Backend-primary spaces may discard the
     * tuple projection; atom_ids remain the authority for later resolution. */
    CettaIndex *atom_indices;
    AtomId *atom_ids; /* stable equation ids for backend-primary views */
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
    /* Captured logical-order keys; atom_ids survive projection disposal. */
    CettaIndex *atom_indices;
    AtomId *atom_ids; /* stable annotation ids for backend-primary views */
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
    uint8_t *id_present;      /* dense AtomId presence bitset: O(1) exact-contains, unclusterable */
    uint64_t id_present_bits; /* capacity in bits; 0 = unallocated */
    CettaIndex id_present_synced_len; /* logical prefix already reflected in id_present (lazy catch-up) */
    bool id_present_dirty;    /* removal/bulk invalidated bits: clear + full resync on next membership query */
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
            uint8_t *id_present;
            uint64_t id_present_bits;
            CettaIndex id_present_synced_len;
            bool id_present_dirty;
            bool eq_idx_dirty;
            bool ty_idx_dirty;
            bool exact_idx_dirty;
            bool has_non_exact_atoms;
            bool has_non_exact_atoms_dirty;
            uint32_t secondary_index_deferral_depth;
        };
    };
    SpaceKind kind;
    /* Process-local lifetime identity.  A revision identifies a state only
       within one Space lifetime; instance_id prevents pointer reuse from
       reviving cached state from a freed Space at the same address. */
    uint64_t instance_id;
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
bool space_atom_id_requires_authored_order(const Space *s,
                                           AtomId atom_id,
                                           Atom *atom);
/* Replay one ordered declaration block.  Backends may apply it transactionally
   as one publication; unsupported fragments retain singular semantics. */
bool space_add_atom_ids_batch(Space *s, const AtomId *atom_ids,
                              CettaCount atom_count);
bool space_admit_atom(Space *s, Arena *fallback, Atom *atom);
bool space_admit_atom_from_source_arena(
    Space *s, Arena *fallback, const Arena *source_arena, Atom *atom);
TermUniverseError space_term_universe_last_error_code(const Space *s);
void space_linearize(Space *s);
void space_mark_derived_state_dirty(Space *s);
void space_discard_native_logical_view(Space *s);
/* Drop the full logical tuple projection while retaining revision-independent
 * equation/type descriptors. The backend mutation caller remains responsible
 * for invalidating either descriptor when its corresponding row class changes. */
void space_discard_native_logical_view_preserving_dispatch(Space *s);
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
static inline uint64_t space_instance_id(const Space *s) {
    return s ? s->instance_id : 0;
}

/* A process-global monotonic counter bumped on every space revision bump.
 * It is a sound over-approximation of "no consulted space changed": if this
 * value is unchanged between two points, no space was mutated in between, so
 * a fortiori every space a computation consulted is unchanged.  Ground-call
 * memoization uses it as a conservative whole-episode invalidation key; it is
 * read only by opt-in memoization and changes no existing behaviour. */
uint64_t space_global_mutation_epoch(void);

/* An in-process read token for one live Space revision.  The token carries a
   process-local lifetime identity as well as the address and revision, so a
   freed-and-reinitialized Space at the same address cannot revive it.  This is
   deliberately not a persistent or serializable identity. */
typedef struct {
    const Space *space;
    uint64_t instance_id;
    uint64_t revision;
} SpaceReadToken;

typedef struct {
    SpaceReadToken read;
    CettaIndex logical_index; /* captured ordering/occurrence key */
    AtomId equation_id;
    bool has_equation_id;
} SpaceEquationOccurrenceId;

typedef struct {
    SpaceEquationOccurrenceId id;
    Atom *equation;
    Atom *lhs;
    Atom *rhs;
} SpaceEquationOccurrence;

SpaceReadToken space_read_token(const Space *s);
bool space_read_token_is_current(SpaceReadToken token);
/* Validate a token against a Space whose lifetime is independently known to
 * be active.  Unlike the one-argument form, this never dereferences the raw
 * pointer retained by a stale token. */
bool space_read_token_matches_live_space(SpaceReadToken token,
                                         const Space *live_space);
bool space_equation_occurrence_resolve(SpaceEquationOccurrenceId id,
                                       SpaceEquationOccurrence *out);

/*
 * Revision-pinned, declaration-order cursor over equations whose left-hand
 * side may match a known call head.  Native spaces merge the exact-head and
 * wildcard index streams by logical occurrence index, preserving duplicate
 * clauses and source order without scanning unrelated heads.  Overlay spaces
 * use the complete logical view as the correctness fallback.
 */
typedef struct {
    SpaceReadToken read;
    SymbolId head;
    CettaIndex exact_position;
    CettaIndex wildcard_position;
    CettaIndex overlay_position;
    bool overlay;
} SpaceEquationCursor;

typedef enum {
    SPACE_EQUATION_CURSOR_END = 0,
    SPACE_EQUATION_CURSOR_ITEM,
    SPACE_EQUATION_CURSOR_INVALIDATED,
} SpaceEquationCursorStep;

bool space_equation_cursor_init(Space *s, SymbolId head,
                                SpaceEquationCursor *cursor);
SpaceEquationCursorStep space_equation_cursor_next(
    SpaceEquationCursor *cursor, SpaceEquationOccurrenceId *out);

bool space_contains_exact(Space *s, Atom *atom);
/* Exact fragment of match existence.  The result is applicable only when a
   ground, structurally indexable pattern is queried against a relation whose
   stored rows are all exact.  Backend-primary PathMap answers through its
   direct structural membership operation; if that operation is unavailable,
   the fragment declines instead of materializing a native shadow. */
bool space_match_exists_ground_exact(Space *s, Atom *pattern,
                                     bool *out_applicable);
/* O(1)-amortized alpha-aware membership over native spaces, including native
   overlays; *out_applicable is false for non-native backends. */
bool space_contains_canonical(Space *s, Atom *atom, bool *out_applicable);
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
/* Conservative pre-materialization filter.  Returning false may only exclude
 * an equation occurrence already proved unable to match.  The equation store
 * retains authority for candidate order, multiplicity, freshening, and exact
 * matching. */
typedef bool (*QueryEquationCandidateFilter)(Atom *equation, void *ctx);

void query_results_init(QueryResults *qr);
bool query_results_push(QueryResults *qr, Atom *result, Bindings *b);
bool query_results_push_move(QueryResults *qr, Atom *result, Bindings *b);
void query_results_diag_set_capacity_limit_override(CettaCount limit);
CettaCount query_equations_visit(Space *s, Atom *query, Arena *a,
                                 QueryResultVisitor visitor, void *ctx);
CettaCount query_equations_visit_filtered(
    Space *s, Atom *query, Arena *a,
    QueryEquationCandidateFilter filter, void *filter_ctx,
    QueryResultVisitor visitor, void *visitor_ctx);
/* Match exactly one admitted (= lhs rhs) atom against query.  This is the
   single-candidate form of query_equations_visit: it applies the same
   freshening, bidirectional matching, visible-variable projection, and RHS
   substitution, but performs no candidate selection. */
CettaCount query_equation_visit(Atom *equation, Atom *query, Arena *a,
                                QueryResultVisitor visitor, void *ctx);
/* Execute a complete logical equation query whose candidate selection has
   already proved that `equation` is the only candidate.  Unlike the
   lower-level per-candidate primitive above, this preserves the ordinary
   query/candidate runtime accounting. */
CettaCount query_equations_visit_singleton(
    Atom *equation, Atom *query, Arena *a,
    QueryResultVisitor visitor, void *ctx);
CettaCount query_results_visit(const QueryResults *qr,
                               QueryResultVisitor visitor, void *ctx);
void query_results_free(QueryResults *qr);

/* Find all (= lhs rhs) in space where lhs matches query (bidirectional).
   Returns substituted RHS for each match, plus bindings. */
void query_equations(Space *s, Atom *query, Arena *a, QueryResults *out);
bool space_equations_may_match_known_head(Space *s, SymbolId head);
/* Revision-pinned least fixed point of the generated relational-effect
 * algebra over user-equation dependencies.  A named symbol with no visible
 * equation is inert at the pinned revision; malformed, variable-headed, or
 * higher-order dependencies conservatively carry the generated uncertain
 * effect. */
CettaGsltQueryEffect space_query_effect_for_head(
    Space *s, SymbolId head, bool *out_defined);
/* Eagerly drops this thread's derived entries.  Revision tokens remain the
 * cross-thread authority, so a mutation cannot revive stale analysis even
 * when it originates on another evaluator thread. */
void space_execution_analysis_note_mutation(Space *s);
/* Exact explicit-head arities admitted by (= (head ...) rhs) equations.
 * Wildcard/expression-headed rules are excluded: they do not register a
 * named PeTTa callable. */
bool space_equation_head_arity_bounds(
    Space *s, SymbolId head, CettaExprLen *minimum,
    CettaExprLen *maximum, bool *has_exact,
    CettaExprLen query_arity);
/* MAM loop-body view entry guard: head resolves to exactly one equation in a
 * clean single-head bucket, no overlay base (necessary condition for the
 * deterministic-tail loop lane; conservative, cheap). */
bool space_head_has_single_equation(Space *s, SymbolId head);
/* Resolve that single linear equation (or NULL) -- shared by the guard and the
 * revision-keyed view cache. */
Atom *space_single_linear_equation(Space *s, SymbolId head);

/*
 * Register form of a determinate equation.
 *
 * This is deliberately narrower than `space_single_linear_equation`: the LHS
 * must be `(head $r0 ... $rn)` with distinct registers, every RHS variable
 * must name one of those registers, and the plan is pinned to one Space
 * revision. Such a rule can be instantiated from a ground call without
 * freshening, matching, or constructing an intermediate Bindings object.
 * Everything outside this proved fragment stays on the general equation
 * path.
 */
#define SPACE_PREPARED_EQUATION_MAX_REGISTERS \
    CETTA_GSLT_PREPARED_EQUATION_MAX_REGISTERS
typedef struct {
    SpaceEquationOccurrenceId occurrence;
    Atom *equation;
    Atom *lhs;
    Atom *rhs;
    SymbolId head;
    CettaExprLen arity;
    uint32_t evidence;
    VarId registers[SPACE_PREPARED_EQUATION_MAX_REGISTERS];
    Atom *register_guard;
    Atom *register_base;
    Atom *register_tail;
    bool register_base_when_true;
} SpacePreparedEquation;

typedef enum {
    SPACE_PREPARED_REGISTER_NOT_APPLICABLE = 0,
    SPACE_PREPARED_REGISTER_VALUE = 1,
    SPACE_PREPARED_REGISTER_TAIL_CALL = 2,
} SpacePreparedRegisterStep;

bool space_head_has_arrow_signature(Space *s, SymbolId head,
                                    CettaExprLen arity);
bool space_prepare_single_equation(Space *s, SymbolId head,
                                   SpacePreparedEquation *out);
Atom *space_prepared_equation_instantiate_ground(
    const SpacePreparedEquation *plan, Atom *call, Arena *arena);
SpacePreparedRegisterStep space_prepared_equation_execute_register_step(
    const SpacePreparedEquation *plan, Atom *call, Arena *arena,
    Atom **result_out);
SpacePreparedRegisterStep space_prepared_equation_run_register_loop(
    const SpacePreparedEquation *plan, Atom *call, Arena *result_arena,
    size_t max_steps, Atom **result_out);
SpacePreparedRegisterStep space_prepared_equation_run_register_recursion(
    const SpacePreparedEquation *plan, Atom *call, Arena *result_arena,
    size_t max_calls, Atom **result_out);

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
bool space_remove_atom_ids_batch(Space *s, const AtomId *atom_ids,
                                 CettaCount atom_count,
                                 CettaCount *out_removed);

/* ── Match Indexing ─────────────────────────────────────────────────────── */

/* Return backend-private candidate coordinates for a match pattern via its
   discrimination index. Dereference them with space_match_candidate_at64;
   they need not share the public logical get-atoms order.
   For small spaces (< MATCH_TRIE_THRESHOLD), returns all indices.
   Caller must free(*out). Returns count. */
CettaIndex space_match_candidates64(Space *s, Atom *pattern, CettaIndex **out);
uint32_t space_match_candidates(Space *s, Atom *pattern, uint32_t **out);
Atom *space_match_candidate_at64(const Space *s, CettaIndex idx);
/*
 * Try an exact multiplicity-preserving COUNT without constructing bindings
 * or result atoms.  The admitted fragment is backend-defined but must be a
 * semantic subset of ordinary matching; false means "use the oracle path."
 */
bool space_match_count_flat_linear64(Space *s, Arena *scratch,
                                     Atom *pattern, uint64_t *count,
                                     CettaIndex *examined);
bool space_match_count_conjunction64(Space *s, Arena *scratch,
                                     Atom **patterns,
                                     CettaExprLen npatterns,
                                     const Bindings *seed,
                                     uint64_t *count);

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

/*
 * Return only explicit `(: subject type)` declarations, in logical space
 * order.  The returned pointer array is caller-owned; its atoms live in the
 * supplied arena and are freshened per occurrence.
 */
uint32_t space_get_declared_types(
    Space *s, Arena *a, Atom *subject, Atom ***out_types);

typedef struct {
    uint64_t indexed_lookups;
    uint64_t indexed_rows_examined;
    uint64_t full_space_rows_examined;
} SpaceDeclaredTypeLookupCost;

/* The costed form is observationally identical and lets focused clients
 * prove which lookup path they used.  Counts accumulate into `cost`. */
uint32_t space_get_declared_types_costed(
    Space *s, Arena *a, Atom *subject, Atom ***out_types,
    SpaceDeclaredTypeLookupCost *cost);

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
