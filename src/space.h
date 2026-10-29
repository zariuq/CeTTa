#ifndef CETTA_SPACE_H
#define CETTA_SPACE_H

#include <stdatomic.h>

#include "atom.h"
#include "rule_binding.h"
#include "generated/cetta_execution_contracts.generated.h"
#include "gslt_term_view_v1.h"
#include "name_key.h"
#include "registered_symbols.h"
#include "match.h"
#include "checkpoint_compare.h"
#include "subst_tree.h"
#include "term_universe.h"
#include "space_dense.h"

/* ── Discrimination Trie (à la Vampire SubstitutionTree) ───────────────── */

/* The discrimination-trie representation is private to space.c.  Consumers
 * observe only the finite coordinate-map operations below; this keeps storage
 * choices such as singleton or hashed branches out of the space ABI. */
typedef struct DiscNode DiscNode;

DiscNode *disc_node_new(void);
void disc_node_free(DiscNode *n);
void disc_insert(DiscNode *root, Atom *lhs, CettaIndex eq_idx);
bool disc_insert_id(DiscNode *root, const TermUniverse *universe,
                    AtomId atom_id, CettaIndex eq_idx);
bool disc_remove_id(DiscNode *root, const TermUniverse *universe,
                    AtomId atom_id, CettaIndex coordinate);
/* Transport every retained leaf coordinate through one validated stable
 * occurrence contraction.  Returns the number of removed leaves. */
CettaCount disc_transport_stable_coordinates(
    DiscNode *root, const CettaIndex *source_to_target,
    CettaCount source_len);
/* Collect all matching equation indices into result array */
void disc_lookup(DiscNode *root, Atom *query, CettaIndex **out,
                 CettaIndex *nout, CettaIndex *cout);
/* Traverse a borrowed top-level expression coordinate vector through the same
 * matcher.  The coordinate atoms remain owned by the caller. */
void disc_lookup_expression_coordinates(
    DiscNode *root, Atom *const *coordinates,
    CettaExprLen coordinate_count, CettaIndex **out,
    CettaIndex *nout, CettaIndex *cout);
/* Count the leaf occurrences on one fully rigid query path without building a
 * candidate vector.  The observation is admitted only when no stored-variable
 * edge can match at any traversed prefix.  A missing rigid edge is an exact
 * zero; unsupported grounded values and wildcard overlap decline. */
/* Fold occurrence multiplicities for a flat expression with a rigid prefix
 * and a trailing run of distinct free variables. Stored wildcard overlap in
 * the rigid prefix, approximate paths and nested suffixes decline. */
bool disc_count_flat_trailing_wildcards(
    const DiscNode *root, const Atom *pattern, CettaIndex *out_count);
bool disc_count_flat_expression_coordinates(
    const DiscNode *root, Atom *const *coordinates,
    CettaExprLen coordinate_count, CettaIndex *out_count);
bool disc_count_rigid_exact_path(
    const DiscNode *root, const Atom *query, CettaIndex *out_count);
bool disc_count_rigid_exact_expression_coordinates(
    const DiscNode *root, Atom *const *coordinates,
    CettaExprLen coordinate_count, CettaIndex *out_count);

#include "space_match_backend.h"
#include "space_path_index.h"

/* ── Head chains: the rows of one projection, chained by head symbol ────── */

/* The rows whose head is `head`, oldest first.  Row positions index the
 * SpaceHeadChains row arrays. */
typedef struct {
    SymbolId head;
    uint32_t first;
    uint32_t last;
    uint32_t count;
} SpaceHeadChain;

typedef struct {
    /* Captured logical-order keys; atom_ids survive projection disposal.
     * next[r] is the following row of row r's chain. */
    CettaIndex *atom_indices;
    AtomId *atom_ids;
    uint32_t *next;
    CettaIndex len, cap;
    /* Open addressing by the head's spelling-stable hash. */
    SpaceHeadChain *chains;
    uint32_t chain_len, chain_cap;
    /* Rows without a head symbol. */
    SpaceHeadChain headless;
} SpaceHeadChains;

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
    /* Open-addressed exact membership set for named equation heads.
     * Bucket collisions in the pattern index must not turn a callable-head
     * existence check into a scan and decode of every colliding equation. */
    SymbolId *slots;
    CettaIndex len;
    CettaIndex cap;
} EqHeadSet;

typedef struct {
    /* EQ_INDEX_BUCKETS buckets, allocated when the first headed equation is
     * filed; until then every bucket reads as empty. */
    EqBucket *buckets;
    EqBucket wildcard; /* equations with variable/structured LHS head */
    /* Each exact head's equations.  Bucket collisions in the pattern index
     * must not turn a head-scoped question into a scan of colliding rows. */
    SpaceHeadChains head_rows;
    /* Separate declaration projection. Ordinary callable lookup never reads
     * these heads; an extended execution view must opt in explicitly. */
    EqHeadSet directional_heads;
    bool directional_wildcard;
    bool directional_present;
} EqIndex;

/* ── Type Annotation Index (: subject type) by subject head ─────────────── */

/* Declarations chained by their subject's head: the symbol itself, or the
 * symbol heading an expression subject. */
typedef struct {
    SpaceHeadChains rows;
    /* Rows with an open outer constructor can match a declaration without
     * being syntactically headed by ':'. They prohibit negative exclusion. */
    bool has_open_rows;
    bool has_non_symbol_subjects;
} TypeAnnIndex;

/* One exact row: its AtomId and its position, in 32 bits each while every
 * id and row fits there (UINT32_MAX marks an empty narrow slot), in 64 bits
 * otherwise (CETTA_ATOM_ID_NONE marks an empty wide slot).  A row occurring
 * several times holds one slot per occurrence. */
typedef struct {
    uint32_t id;
    uint32_t row;
} ExactAtomNarrowSlot;

typedef struct {
    AtomId id;
    CettaIndex row;
} ExactAtomSlot;

/* How many non-exact rows (rows with variables, among others) one head
 * symbol leads. */
typedef struct {
    SymbolId head;      /* SYMBOL_ID_NONE marks an empty slot */
    CettaCount rows;
} ExactIndexInexactHead;

/* Exact rows by AtomId, in a table that grows with the space (linear
 * probing, a row per slot), built by its first reader.  The rows the index
 * cannot hold, the non-exact ones, are counted by head symbol instead, and
 * apart when they have none: a ground pattern whose head counts none, while
 * no non-exact row lacks a head, can match only an exact row, and only by
 * being it. */
typedef struct {
    void *slots;                    /* NULL until the first exact row */
    size_t mask;                    /* slot count - 1 */
    size_t used;
    bool wide;                      /* ExactAtomSlot, else ExactAtomNarrowSlot */
    ExactIndexInexactHead *inexact_heads;
    size_t inexact_head_mask;
    size_t inexact_head_used;
    CettaCount inexact_unheaded;
} ExactAtomIndex;

typedef enum {
    SPACE_KIND_ATOM = 0,
    SPACE_KIND_STACK = 1,
    SPACE_KIND_QUEUE = 2,
    SPACE_KIND_HASH = 3,
    /* Holds each atom once: adding an atom it holds, or an alpha-variant of
     * one, changes nothing. */
    SPACE_KIND_SET = 4,
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
    bool id_present_incomplete; /* pointer-backed rows omitted from the canonical projection */
    /* The bitset's keys that are no row's id, the alpha-canonical forms of
     * rows with variables: the bitset holds them, one reference a key set. */
    AtomId *id_present_keys;
    CettaIndex id_present_key_len, id_present_key_cap;
    /* The heads whose ground facts the space keeps as bits (space.c, "Relations
     * kept as bits"); NULL while it keeps none.  The pairs they hold, over
     * every head, which every positional read past the rows asks. */
    struct SpaceDense *dense;
    CettaCount dense_pairs;
    /* The head last found to keep rows, whose facts an add asks no more
     * than their header. */
    SymbolId dense_rows_head;
    bool eq_idx_dirty;
    bool ty_idx_dirty;
    bool exact_idx_dirty;
    bool has_non_exact_atoms;
    bool has_non_exact_atoms_dirty;
    bool has_graded_equations; /* derived alongside the ordinary equation index */
    uint32_t secondary_index_deferral_depth;
    /* Rows removed and not yet contracted away (space_mark_rows): one bit
     * per row, their number, and a Fenwick tree of the live rows of each
     * 64-row word, which maps logical positions to rows while any row is
     * marked. */
    uint64_t *marks;
    CettaIndex *mark_tree;
    CettaIndex marked;
    CettaIndex mark_rows;
    CettaIndex mark_cap;
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
            bool id_present_incomplete;
            AtomId *id_present_keys;
            CettaIndex id_present_key_len, id_present_key_cap;
            struct SpaceDense *dense;
            CettaCount dense_pairs;
            SymbolId dense_rows_head;
            bool eq_idx_dirty;
            bool ty_idx_dirty;
            bool exact_idx_dirty;
            bool has_non_exact_atoms;
            bool has_non_exact_atoms_dirty;
            bool has_graded_equations;
            uint32_t secondary_index_deferral_depth;
            uint64_t *marks;
            CettaIndex *mark_tree;
            CettaIndex marked;
            CettaIndex mark_rows;
            CettaIndex mark_cap;
        };
    };
    SpaceKind kind;
    /* Strict: the space keeps only what its kind needs, and a general match
     * reads its rows in turn; full: it keeps the general indexes too.  Both
     * answer every operation alike. */
    bool strict;
    /* A live projection of a module's own storage, not a writable overlay. */
    bool module_own_view;
    /* Process-local lifetime identity.  A revision identifies a state only
       within one Space lifetime; instance_id prevents pointer reuse from
       reviving cached state from a freed Space at the same address. */
    uint64_t instance_id;
    uint64_t revision;
    /* Currency for captured read views. Never copied with storage or reused
       after retirement; readers hold the header's existing lifetime owner. */
    _Atomic uint64_t read_stamp;
    /* Revision of the ordered equation-occurrence projection only.  Data-only
       mutations leave this coordinate unchanged; mutations whose projection
       effect is opaque advance it conservatively. */
    uint64_t equation_revision;
    /* Revision of the declaration projection: `(: subject type)` occurrences
       only.  Callability, named arity, and effect classification read the
       equation and declaration projections together and nothing else, so
       caches of those facts key on this pair rather than on `revision`. */
    uint64_t declaration_revision;
    /* A `(:< sub super)` row, or a mutation that could not be classified, has
       been published here. Never cleared: a read that finds it false knows
       the space has no subtype rows. */
    bool declares_subtypes;
    /* Process-wide unique prefix stamp, refreshed at initialization and when
       a publication may rewrite an already visible prefix.  Append-only
       publications preserve it.  An overlay folds the stamps of its actual
       base chain into projection currency, so unrelated Spaces cannot
       invalidate its equation view. */
    uint64_t prefix_epoch;
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
    /* The first imported_len logical atoms belong to modules this space
       imports (HE's standard library).  As in HE's module space, a query
       sees them after the space's own atoms, while listing, counting and
       removal through this space see only its own atoms.  Zero when every
       atom is the space's own. */
    CettaIndex imported_len;
    /* Modules this space imports (HE's ModuleSpace): ordered, non-owning
       handles to their spaces.  A query answers this space's own atoms, then
       each dependency's own atoms, in order; adding, removing, listing and
       counting see this space's own atoms only.  The module table owns the
       dependency spaces. */
    struct Space **deps;
    uint32_t dep_count, dep_cap;
    /* Spaces importing this one: a mutation here advances their revisions,
       so caches over their composed view go stale. */
    struct Space **importers;
    uint32_t importer_count, importer_cap;
    /* Unique stamp of changes read through the live module handles.
       Dependency appends change this, but preserve the storage prefix. */
    uint64_t module_dependency_epoch;
    /* Charged to the resource ledger's space gauges at initialization. */
    bool ledger_counted;
} Space;

/* NativeFlatWorldV1. Source headers, their TermUniverse generation and the
 * symbol service are held and quiescent throughout these calls. This is an
 * explicit target-world image, not a clone of foreign or opaque services.
 * Encoded AtomIds remain rigid in the same held, append-only TermUniverse.
 * Neither admission nor comparison decodes a row or warms an index. */
typedef struct {
    size_t work_limit, byte_limit;
    size_t work, retained_bytes, copied_bytes, allocation_attempts;
    size_t attempted_bytes; /* includes a failed malloc request, never counted as retained */
    size_t observer_work, observer_bytes; /* actual retained observer-capacity growth */
    size_t observer_requested_bytes; /* reserved full replacement-table allocation */
    const char *refusal;
} SpaceCheckpointBudget;

typedef struct {
    uint64_t instance, prefix;
} SpaceCheckpointIssuance;

SpaceCheckpointIssuance space_checkpoint_issuance(void);
/* The Space and universe headers are independently live and quiescent.
 * Verify the actual unique format-observer edge with bounded paid reads. */
bool space_universe_registration_current(const Space *, SpaceCheckpointBudget *);
bool space_checkpoint_native_admitted(const Space *, SpaceCheckpointBudget *);
/* destination is uninitialized and *destination_ready is false on entry.
 * Once initialized it remains a real owner, including after failure. The
 * caller must retain/census/free it, and must not execute a partial image.
 * A preflight refusal leaves destination and its ready flag untouched.
 * Successful return is the completion capability; ready alone is not one. */
bool space_checkpoint_native_copy(const Space *source, Space *destination,
    bool *destination_ready, SpaceCheckpointBudget *);
bool space_checkpoint_native_compare(const Space *, const Space *,
    CettaCheckpointCompare *, SpaceCheckpointBudget *);

#ifdef CETTA_TEST_HOOKS
/* The exact-membership index, observed by its sizing tests: its slot count
 * (0 while no reader has built it), the rows it files, whether it is
 * current, how many tables were built to resize one, and how many of the
 * next table allocations fail as an exhausted allocator would. */
size_t space_exact_index_test_slots(const Space *s);
size_t space_exact_index_test_used(const Space *s);
bool space_exact_index_test_current(const Space *s);
size_t space_exact_index_test_resizes(void);
void space_exact_index_test_refuse(size_t allocations);
#endif


void space_init_with_universe(Space *s, TermUniverse *universe);
void space_init_overlay(Space *s, const Space *base);
/* A public local view retains its base's admitted matching profile. Attached
 * MORK execution remains a separate service; its local view uses native
 * storage. Failure releases the initialized view's storage. */
bool space_init_overlay_local(Space *s, const Space *base);
/* Borrow the module's own rows, excluding its imported prefix/dependencies.
 * Mutations reach the original storage. Free this view before its owner, or
 * let owner teardown detach it; the view never owns that owner's storage. */
bool space_init_module_own_view(Space *s, Space *module);
/* Select observed storage without changing the view's identity. Transaction
   storage is watched until release; its previous storage remains watched so
   restoring that selection requires no allocation. */
bool space_module_own_view_select(Space *view, Space *storage);
void space_module_own_view_release(Space *view, Space *storage);
static inline Space *space_mutation_target(Space *s) {
    s = s && s->module_own_view ? (Space *)s->overlay_base : s;
    if (s && s->match_backend.kind == SPACE_ENGINE_SOURCE) {
        space_match_backend_set_error(SPACE_MATCH_BACKEND_ERROR_READ_ONLY);
        return NULL;
    }
    return s;
}
void space_init(Space *s);
/* Mark the current contents as imported-module atoms.  False, leaving the
   space unchanged, for an overlay or a stack, queue or hash space. */
bool space_mark_imported_prefix(Space *s);
/* Leading logical atoms that belong to imported modules: the space's own
   count, or for an overlay the count of the space at the bottom of its
   overlay chain. */
CettaIndex space_imported_length(const Space *s);

/* Removal by marks.  A removed data row is marked and left in place; the
 * logical view skips it, and one contraction later removes every marked row
 * and carries the indexes once (space_settle_marks).  Marks are settled
 * before any reader that reads rows directly. */
CettaIndex space_marks_select(const Space *s, CettaIndex logical);
CettaIndex space_marks_physical_to_logical(const Space *s, CettaIndex *rows,
                                           CettaIndex count);
bool space_remove_logical_positions(Space *s, const CettaIndex *positions,
                                    CettaCount count);
void space_settle_marks_slow(Space *s);
static inline bool space_has_marks(const Space *s) {
    return s && s->native.marked != 0u;
}
static inline void space_settle_marks(Space *s) {
    if (space_has_marks(s))
        space_settle_marks_slow(s);
}
/* Append `dependency` to `importer`'s dependencies unless it is already
   there.  False on allocation failure or for a space importing itself.  A
   change of the list advances the importer's revisions. */
bool space_add_dependency(Space *importer, Space *dependency);
/* The dependencies a read composes after `s`'s own atoms: an overlay reads
   through those of the space at the bottom of its overlay chain. */
static inline uint32_t space_dependency_count(const Space *s) {
    while (s && s->overlay_base) {
        if (s->module_own_view)
            return 0u;
        s = s->overlay_base;
    }
    return s ? s->dep_count : 0u;
}
static inline Space *space_dependency_at(const Space *s, uint32_t index) {
    while (s && s->overlay_base) {
        if (s->module_own_view)
            return NULL;
        s = s->overlay_base;
    }
    return s && index < s->dep_count ? s->deps[index] : NULL;
}
/* The rows a query of `s` reads, as one sequence: `s`'s logical atoms, then
   each dependency's own atoms in import order.  For a space without
   dependencies this is its logical sequence.  Listing, counting and mutation
   use the logical sequence instead. */
CettaCount space_view_length64(const Space *s);
Atom *space_view_get_at64(const Space *s, CettaIndex index);
/* Give `dst`, holding a copy of `src`'s logical atoms, src's module view:
   the same imported prefix and the same dependencies.  False on allocation
   failure. */
bool space_copy_module_view(Space *dst, const Space *src);
/* Whether reads of `s` compose dependencies.  The composed readers (equation
   queries and cursors, match, conjunctions, declared types) answer for such a
   space; an accelerator that reads one space's storage or indices declines. */
static inline bool space_has_dependencies(const Space *s) {
    return space_dependency_count(s) != 0u;
}
/* The active session's grounded operation types: in a language whose
   grounded operations type themselves (HE's Grounded::type_), type lookup
   for such a symbol answers these alone, and declarations written for the
   name do not change them.  0 for every other symbol and language.  Weak:
   a binary without the evaluator types every symbol by declarations. */
uint32_t space_session_grounded_symbol_types(SymbolId symbol,
                                             Atom *const **types)
    __attribute__((weak));
/* Whether the active session reads function types curried (HE's
 * curried-arrows setting).  Weak, as above: without the evaluator, no. */
bool space_session_curried_arrows(void) __attribute__((weak));
/* The function type `type` read curried at `given` arguments: its domains,
 * with those of a result arrow after them, the first `given` the call's and
 * the rest the result's.  NULL when it has fewer than `given` in all. */
Atom *space_curried_function_type(Arena *a, Atom *type, CettaExprLen given);
/* How a call of `given` arguments reads the function types `types`, full
 * arity first: 0 as declared (some type takes exactly `given`, or none reads
 * it curried, or a type is not a function type); 1 a partial application
 * (the types taking more); 2 an over-application (the types taking fewer
 * whose result is an arrow taking the rest). */
unsigned space_curried_call_rank(Atom *const *types, uint32_t count,
                                 CettaExprLen given);
/* The rank of one function type's reading (3: it does not read the call). */
unsigned space_curried_type_rank(Atom *type, CettaExprLen given);
/* space_curried_call_rank of the symbol `head`'s grounded or declared
 * types, read as stored: no type is instantiated, so the check takes no
 * frame identity. */
unsigned space_curried_head_rank(Space *s, Arena *a, Atom *head,
                                 CettaExprLen given);
void space_free(Space *s);
/* Owned index allocations; the Space object and borrowed TermUniverse have
 * independent owners and are not charged by this visitor. */
void space_storage_census(const Space *s, CettaStorageCensus *census);
void space_execution_analysis_cache_free_for_current_thread(void);

/* A selected image of the existing query-effect cache. Selection borrows
 * the image until the caller restores the previous one; no source lifetime
 * is acquired by copying revision tokens. All operations are serial. */
typedef struct SpaceExecutionAnalysisCache SpaceExecutionAnalysisCache;
SpaceExecutionAnalysisCache *space_execution_analysis_cache_current(void);
SpaceExecutionAnalysisCache *space_execution_analysis_cache_new(void);
/* Raw selection preserves NULL/default separately from the effective store. */
SpaceExecutionAnalysisCache *space_execution_analysis_cache_selected(void);
SpaceExecutionAnalysisCache *space_execution_analysis_cache_select(
    SpaceExecutionAnalysisCache *image);
bool space_execution_analysis_cache_admitted(const SpaceExecutionAnalysisCache *,
    size_t inspection_limit);
bool space_execution_analysis_cache_copy(const SpaceExecutionAnalysisCache *,
    size_t entry_limit, SpaceExecutionAnalysisCache **out);
void space_execution_analysis_cache_destroy(SpaceExecutionAnalysisCache *);
void space_execution_analysis_cache_storage(const SpaceExecutionAnalysisCache *,
    CettaStorageCensus *);
bool space_execution_analysis_cache_compare(const SpaceExecutionAnalysisCache *,
    const SpaceExecutionAnalysisCache *, const CettaCheckpointCompare *);
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
/* Store one already-evaluated occurrence sequence with one backend
 * publication where the active engine supports it.  Source order and
 * duplicate occurrences are authoritative; unsupported engines replay the
 * same singular admissions in order. */
bool space_admit_atoms_batch_from_source_arena(
    Space *s, Arena *fallback, const Arena *source_arena,
    Atom *const *atoms, CettaCount atom_count);
TermUniverseError space_term_universe_last_error_code(const Space *s);
void space_linearize(Space *s);
void space_mark_derived_state_dirty(Space *s);
/* Take, or drop, the reference each live row holds on its atom, when the
 * rows are adopted or given up wholesale. */
void space_rows_hold(Space *s);
void space_rows_release(Space *s);

/* Relations kept as bits.  The facts a space keeps as bits count in its
 * length and answer its membership tests, adds and removals; a reader of
 * rows that does not read bits gives them back to the rows first:
 * space_dense_settle for every head, or for the head HEAD only
 * (SYMBOL_ID_NONE: every head).  space_dense_settle_for_pattern settles the
 * head a pattern names, or every head when its head is not a symbol. */
void space_dense_settle(Space *s, SymbolId head);
void space_dense_settle_for_pattern(Space *s, const Atom *pattern);
void space_dense_settle_for_patterns(Space *s, Atom *const *patterns,
                                     CettaExprLen count);
/* The pairs held as bits, over every head. */
static inline CettaCount space_dense_pairs(const Space *s) {
    return s ? s->native.dense_pairs : 0u;
}
/* Count the facts the bits hold that match (h a b), given as COLUMNS: each
 * a variable, distinct from the others, which takes anything, or a ground
 * atom.  A head that is a variable reads every relation kept as bits.
 * False when the bits cannot answer: in a concurrent-evaluation scope, or
 * for a column a number other than an integer, which may equal one by
 * value. */
bool space_dense_count_columns(const Space *s, Atom *const *columns,
                               CettaCount *count);
/* The bits of HEAD, or NULL when the space keeps none for it (or a
 * concurrent-evaluation scope is open).  Facts of HEAD may also be rows:
 * a reader of bits asks the rows too, between space_dense_reading_bits_begin
 * and _end, which keep the guards from giving the bits back meanwhile. */
const CettaDenseRelation *space_dense_relation(const Space *s,
                                               SymbolId head);
/* Whether no row can match a fact of `head` beside its bits: the head keeps
 * bits, none of its facts is a row, and no row's head is open. */
bool space_dense_head_rows_absent(Space *s, SymbolId head);
/* Whether a pattern names a head the space keeps as bits: the native
 * query's loops read bits; the planners that read rows would give them
 * back to the rows. */
bool space_dense_meets_patterns(const Space *s, Atom *const *patterns,
                                size_t count);
void space_dense_reading_bits_begin(void);
void space_dense_reading_bits_end(void);
/* Before adding IDS: a head whose facts among them reach the threshold
 * for bits takes bits now, so that none of them is a row. */
void space_dense_prepare_adds(Space *s, const AtomId *ids, CettaCount count);
/* Whether the space may keep the facts of HEAD as bits: it admits bits,
 * and HEAD has not given its bits back to rows. */
bool space_dense_may_hold(const Space *s, SymbolId head);
/* Add the facts (h a b): h by its symbol and id, each (a, b) by two ids in
 * PAIRS.  While the space keeps h as bits the facts need no records; the
 * others are rows.  A set adds no fact it holds. */
bool space_add_dense_pairs(Space *s, SymbolId head, AtomId head_id,
                           const AtomId *pairs, CettaCount count);
/* Take the pair of the ground fact ATOM from the bits, by its parts (a fact
 * kept as bits may have no record of its own): the copies removed, 0 or 1. */
CettaCount space_dense_remove_ground(Space *s, Atom *atom);
void space_discard_native_logical_view(Space *s);
/* Drop the full logical tuple projection while retaining revision-independent
 * equation/type descriptors. The backend mutation caller remains responsible
 * for invalidating either descriptor when its corresponding row class changes. */
void space_discard_native_logical_view_preserving_dispatch(Space *s);
void space_note_external_backend_mutation(Space *s);
/* Invalidate read-view admission after a representation/profile change.
 * Shared callers hold the physical transition guard through the whole change.
 * This is neither a row publication nor a grant of ownership or authority. */
void space_read_view_invalidate(Space *s);
Space *space_heap_clone_shallow(Space *src);
void space_replace_contents(Space *dst, Space *src);
/* Notified after a space takes over the contents of a space with another
 * lifetime identity, so that records keyed by the source's identity follow
 * its contents.  There is one subscriber. */
typedef void (*SpaceContentsMovedHook)(uint64_t from_instance,
                                       uint64_t to_instance);
void space_set_contents_moved_hook(SpaceContentsMovedHook hook);
SpaceContentsMovedHook space_contents_moved_hook(void);
/* Retiring a logical instance is distinct from replacing its storage. A
 * move first publishes its old source identity to the contents subscriber,
 * then retires that identity. The destination identity survives. Calls are
 * serialized with the space's other lifetime operations; one subscriber. */
typedef void (*SpaceInstanceRetiredHook)(uint64_t instance);
void space_set_instance_retired_hook(SpaceInstanceRetiredHook hook);
const char *space_kind_name(SpaceKind kind);
bool space_kind_from_name(const char *name, SpaceKind *out);
void space_set_strict(Space *s, bool strict);
bool space_is_ordered(const Space *s);
bool space_is_stack(const Space *s);
bool space_is_queue(const Space *s);
bool space_is_hash(const Space *s);
/* Whether a general match reads LEN rows in turn instead of an index: a
 * small space, or a strict one. */
static inline bool space_match_reads_rows(const Space *s, CettaIndex len) {
    return len <= MATCH_TRIE_THRESHOLD || (s && s->strict);
}
/* The exact index is kept by a full space, and by a hash space, whose
 * lookups are its kind's own. */
static inline bool space_keeps_exact_index(const Space *s) {
    return s && (!s->strict || s->kind == SPACE_KIND_HASH);
}
bool space_is_set(const Space *s);
AtomId space_get_atom_id_at(const Space *s, uint32_t idx);
CettaCount space_length64(const Space *s);
AtomId space_get_atom_id_at64(const Space *s, CettaIndex idx);
Atom *space_get_at64(const Space *s, CettaIndex idx);
Atom *space_get_at(const Space *s, uint32_t idx);
bool space_row_may_be_rule(const Space *s, CettaIndex idx);
bool space_view_row_may_be_rule(const Space *s, CettaIndex index);
Atom *space_peek(const Space *s);
bool space_pop(Space *s, Atom **out);
bool space_truncate64(Space *s, CettaCount new_len);
bool space_truncate(Space *s, uint32_t new_len);
bool space_length_u32_checked(const Space *s, uint32_t *out_len);
static inline uint64_t space_revision(const Space *s) {
    return s ? s->revision : 0;
}
static inline uint64_t space_equation_revision(const Space *s) {
    return s ? s->equation_revision : 0;
}
static inline uint64_t space_declaration_revision(const Space *s) {
    return s ? s->declaration_revision : 0;
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
/* Compiled entry checks read this clock on every invocation. Keep the
 * accessor inline; atomic publication remains centralized in space.c. */
extern _Atomic uint64_t cetta_space_global_mutation_epoch;
static inline uint64_t space_global_mutation_epoch(void) {
    return atomic_load_explicit(&cetta_space_global_mutation_epoch,
                                memory_order_relaxed);
}

/* An in-process read token for one live Space revision.  The token carries a
   process-local lifetime identity as well as the address and revision, so a
   freed-and-reinitialized Space at the same address cannot revive it.  This is
   deliberately not a persistent or serializable identity. */
typedef struct {
    const Space *space;
    uint64_t instance_id;
    uint64_t revision;
    /* Captured at issue.  Data-only appends leave this unchanged; a prefix
     * rewrite issues a new epoch.  Occurrence resolve and equation cursors
     * consult this rather than `revision` so an in-flight pin survives
     * appends without copying the bag. */
    uint64_t prefix_epoch;
    /* A frozen base prefix and live module handles have different
       validity conditions. Only the full read notices module appends. */
    uint64_t base_prefix_epoch;
    uint64_t base_dependency_epoch;
} SpaceReadToken;

/* The caller holds both actual target headers and has authenticated their
 * completed world-copy relation. Historical wrong-instance and unrelated
 * keys stay rigid, including initialized buckets in inactive shards. */
bool space_execution_analysis_cache_transport_target(SpaceExecutionAnalysisCache *,
    SpaceReadToken source, SpaceReadToken destination, size_t inspection_limit);
bool space_execution_analysis_cache_compare_targets(const SpaceExecutionAnalysisCache *,
    const SpaceExecutionAnalysisCache *, SpaceReadToken left_target,
    SpaceReadToken right_target, const CettaCheckpointCompare *);

typedef struct {
    const _Atomic uint64_t *stamp;
    uint64_t expected;
} SpaceReadWatch;

/* Original authorities for one logical Space view. The caller retains every
 * Space and its universe; this view owns only its captured token array. A
 * non-overlay view has no allocation. Watches are immutable after capture;
 * checking them does not permit unlocked access to mutable storage. Cloning preserves the issued tokens,
 * including stale ones, rather than capturing a later revision. Capture and
 * clone require an empty output; release that output exactly once. */
typedef struct {
    SpaceReadToken root;
    SpaceReadToken *bases;
    size_t base_count;
    SpaceReadWatch root_watch;
    SpaceReadWatch *base_watches;
} SpaceReadView;

bool space_read_view_capture(const Space *space, SpaceReadView *out);
bool space_read_view_clone(const SpaceReadView *source, SpaceReadView *out);
bool space_read_view_is_current(const SpaceReadView *view);
bool space_read_view_has_native_occurrences(const SpaceReadView *view);
/* Native occurrence pins, or one standalone backend with the explicit
 * incremental occurrence interface. This does not grant native index access. */
bool space_read_view_has_incremental_occurrences(const SpaceReadView *view);
bool space_read_view_uses_occurrence_source(const SpaceReadView *view);
bool space_read_view_visit(const SpaceReadView *view,
    bool (*visitor)(SpaceReadToken read, void *context), void *context);
void space_read_view_release(SpaceReadView *view);
void space_read_view_storage_census(const SpaceReadView *view,
                                   CettaStorageCensus *census);

/* Lifetime-qualified identity of the ordered equation-occurrence projection.
   This token deliberately ignores data-only Space mutations. */
typedef struct {
    const Space *space;
    uint64_t instance_id;
    uint64_t equation_revision;
    /* Overlays read a live base whose mutations do not advance the overlay's
       own revision.  Ordinary spaces leave this dependency empty; an overlay
       records the newest state stamp in its actual base chain. */
    const Space *projection_dependency;
    uint64_t projection_dependency_epoch;
} SpaceEquationToken;

/* Lifetime-qualified identity of the program projection a Space presents to
   callability, named-arity, and effect classification: its equation and
   declaration occurrences.  Data-only mutations leave it unchanged.  An
   overlay freezes a base storage prefix but reads its modules live. Its
   token includes the newest base-prefix or module-view stamp, so module
   appends invalidate it without unrelated spaces doing so. */
typedef struct {
    const Space *space;
    uint64_t instance_id;
    uint64_t equation_revision;
    uint64_t declaration_revision;
    uint64_t base_dependency_epoch;
} SpaceProgramToken;

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
/* Instance live and prefix not rewritten.  Data-only appends that bump
 * `revision` remain intact. */
bool space_read_token_prefix_intact(SpaceReadToken token);
/* Validate a token against a Space whose lifetime is independently known to
 * be active.  Unlike the one-argument form, this never dereferences the raw
 * pointer retained by a stale token. */
bool space_read_token_matches_live_space(SpaceReadToken token,
                                         const Space *live_space);
SpaceEquationToken space_equation_token(const Space *s);
SpaceProgramToken space_program_token(const Space *s);
static inline bool space_program_token_eq(SpaceProgramToken left,
                                          SpaceProgramToken right) {
    return left.space == right.space &&
           left.instance_id == right.instance_id &&
           left.equation_revision == right.equation_revision &&
           left.declaration_revision == right.declaration_revision &&
           left.base_dependency_epoch == right.base_dependency_epoch;
}
/* The dependency epoch of an overlay's base chain. */
uint64_t space_overlay_dependency_epoch(const Space *s);
/* space_program_token_eq(token, space_program_token(token.space)), field by
 * field: a space without an overlay base has dependency epoch zero, so only
 * an overlay walks its base chain.  Inline, since every call a compiled
 * relation makes checks it. */
static inline bool space_program_token_is_current(SpaceProgramToken token) {
    const Space *s = token.space;
    return s && token.instance_id != 0u &&
           token.base_dependency_epoch != UINT64_MAX &&
           token.instance_id == s->instance_id &&
           token.equation_revision == s->equation_revision &&
           token.declaration_revision == s->declaration_revision &&
           token.base_dependency_epoch ==
               (s->overlay_base ? space_overlay_dependency_epoch(s) : 0u);
}
bool space_program_token_matches_live_space(
    SpaceProgramToken token, const Space *live_space);
bool space_equation_token_is_current(SpaceEquationToken token);
bool space_equation_token_matches_live_space(
    SpaceEquationToken token, const Space *live_space);
bool space_equation_occurrence_resolve(SpaceEquationOccurrenceId id,
                                       SpaceEquationOccurrence *out);
bool space_rule_occurrence_resolve(SpaceEquationOccurrenceId id,
    unsigned admitted_forms, SpaceEquationOccurrence *out);

/*
 * Revision-pinned, declaration-order cursor over equations whose left-hand
 * side may match a known call head.  Native spaces merge the exact-head and
 * wildcard index streams by logical occurrence index, preserving duplicate
 * equations and source order without scanning unrelated heads.  Overlay spaces
 * use the complete logical view as the correctness fallback.
 */
typedef struct {
    SpaceReadToken read;
    SymbolId head;
    /* The member being read: the space itself, then each dependency's own
       equations in import order (dependency counts the members entered
       after the space).  An item's occurrence names its member. */
    SpaceReadToken member;
    uint32_t dependency;
    bool own_only;
    CettaIndex ceiling;
    CettaIndex exact_position;
    CettaIndex wildcard_position;
    CettaIndex overlay_position;
    bool overlay;
    bool incomplete;
    bool include_graded;
    /* With imported atoms (Space.imported_len), the member's own equations
       are visited in phase 0 and the imported ones in phase 1. */
    CettaIndex imported;
    uint8_t phase;
    unsigned admitted_forms;
} SpaceEquationCursor;

typedef enum {
    SPACE_EQUATION_CURSOR_END = 0,
    SPACE_EQUATION_CURSOR_ITEM,
    SPACE_EQUATION_CURSOR_INVALIDATED,
} SpaceEquationCursorStep;

/* Experimental authored syntax is a view: stored rows and ordinary equation
 * dispatch are unchanged. A non-NULL grade belongs to the same variable scope
 * as lhs/rhs. Lowering returns one canonical equation for joint freshening. */
bool space_equation_view(Atom *row, bool include_graded,
                         Atom **lhs, Atom **rhs, Atom **grade);
Atom *space_equation_execution_view(Arena *arena, Atom *row);
Atom *space_match_where_grade(const Atom *call);
Atom *space_grade_admission_predicate(const Atom *descriptor);
Atom *space_match_execution_view(Arena *arena, Atom *call);
bool space_has_graded_equations(Space *space);
bool space_equation_head_arity_bounds_view(Space *space, SymbolId head,
    CettaExprLen *minimum, CettaExprLen *maximum, bool *has_exact,
    CettaExprLen query_arity);
bool space_equations_may_match_known_head_view(Space *space, SymbolId head);
bool space_equation_occurrence_resolve_view(SpaceEquationOccurrenceId id,
    bool include_graded, SpaceEquationOccurrence *out);
/* SYMBOL_ID_NONE selects every physical equation in the captured prefix;
 * the full-term matcher decides applicability of a computed/compound head. */
bool space_equation_cursor_init_view(Space *space, SymbolId head,
    bool include_graded, SpaceEquationCursor *cursor);
bool space_equation_cursor_init(Space *s, SymbolId head,
                                SpaceEquationCursor *cursor);
/* The extended projection uses the same revision-qualified occurrence order.
 * Its initial realization scans the logical member; ordinary-only readers
 * keep the indexed cursor. The mask is part of the captured query policy. */
bool space_rule_cursor_init(Space *s, SymbolId head, unsigned admitted_forms,
                            SpaceEquationCursor *cursor);
/* Both dimensions are fixed by the program owner; ordinary wrappers above
 * keep their existing policy. The same view resolves the yielded occurrence. */
bool space_rule_cursor_init_view(Space *s, SymbolId head, unsigned admitted_forms,
    bool include_graded, SpaceEquationCursor *cursor);
bool space_rule_occurrence_resolve_view(SpaceEquationOccurrenceId id,
    unsigned admitted_forms, bool include_graded, SpaceEquationOccurrence *out);
SpaceEquationCursorStep space_equation_cursor_next(
    SpaceEquationCursor *cursor, SpaceEquationOccurrenceId *out);

/*
 * Revision-qualified zipper over matching discrimination-trie leaves.
 * Pin a read token, prefix epoch, and occurrence ceiling at init; `next`
 * yields logical indices without copying the bag: in row order for an
 * ordered space (a stack or a queue), in the index's own order for any
 * other, since a space answers a bag.
 * It walks the Space's rows, skipping those removed by marks when it was
 * pinned, so opening one never contracts the Space.
 * An ordered space's trie nodes are merged lazily in row order. The frontier retains
 * one position per node, independently of the candidate bag's length.
 * Appends at or after the ceiling are invisible. A removal while the pin
 * is live leaves the cursor draining the rows it captured, while the Space
 * itself shows only the survivors; any other prefix rewrite invalidates the
 * pin.  Read a yielded index through space_occurrence_cursor_atom.  Release
 * exactly once.  Each cursor is confined to its reader.  Operations on its
 * shared occurrence view use the shared transition domain in concurrent
 * evaluation scopes, including clone and release outside a caller guard.
 */
typedef struct {
    SpaceReadToken read;
    uint64_t prefix_epoch;
    CettaIndex ceiling;
    Space *space;
    SpacePinnedOccurrences *occurrences;
    DiscNode **nodes;
    CettaIndex *leaf_pos;
    uint32_t node_len;
    /* Whether the cursor reads rows in row order: a stack's or a queue's do,
     * merging their trie nodes; any other reads its nodes one after another,
     * and node_next is the node it is on. */
    bool row_order;
    uint32_t node_next;
    /* Cached (occurrence, node) keys packed into one index when the captured
     * ranges permit it. Inactive heap slots also hold tied-head advances. */
    CettaIndex *heap;
    uint32_t heap_len;
    uint32_t heap_node_bits;
    /* A head over variables reads its head's rows in order (head_index). */
    bool head_mode;
    CettaSpaceHeadCursor head;
    CettaSpaceHeadCursor head_start;
    /* A cold selector can supply a compact declaration-ordered candidate list. */
    CettaIndex *flat;
    CettaIndex flat_capacity;
    CettaIndex flat_len;
    CettaIndex flat_next;
    bool flat_mode;
    bool full_scan;
    CettaIndex full_next;
    bool pinned;
    /* init_scan may borrow a live native overlay's logical view instead of
     * a detached occurrence pin. Every access checks all original sources. */
    bool logical_scan;
    SpaceReadView logical_reads;
    /* The row of the logical index `next` yielded last, which the atom read
     * that follows it takes without searching the marks. */
    bool has_last;
    CettaIndex last_logical;
    CettaIndex last_row;
    const SpaceSourceCursorOps *source_ops;
    void *source_state;
    SpaceOccurrenceCursorStep source_failure;
} SpaceOccurrenceCursor;

void space_occurrence_cursor_init_empty(SpaceOccurrenceCursor *cursor);
bool space_occurrence_cursor_init(Space *s, Atom *pattern,
                                  SpaceOccurrenceCursor *cursor);
/* Capture the same physical view without building a candidate array or trie.
 * Each next operation visits one declaration-order occurrence. For overlays
 * with native bases, use the canonical logical order and masking under the
 * captured read chain. Any source change invalidates that live scan; it does
 * not inherit the native pin's drain-after-removal law. The caller retains
 * all Spaces and their universes. General indexed init remains pin-only. */
bool space_occurrence_cursor_init_scan(Space *s, SpaceOccurrenceCursor *cursor);
/* New attempt over the same pin and discrimination frontier: copy the
 * borrowed trie nodes, reset the leaf cursor, and take one pin. */
bool space_occurrence_cursor_clone_unstarted(
    const SpaceOccurrenceCursor *src, SpaceOccurrenceCursor *dst);
/* Clone the exact logical-update position of a live cursor.  The clone owns
 * an independent pin and traversal arrays while observing the same captured
 * rows, ceiling, and next occurrence as the source. */
bool space_occurrence_cursor_clone(
    const SpaceOccurrenceCursor *src, SpaceOccurrenceCursor *dst);
bool space_occurrence_cursor_select(const SpaceOccurrenceCursor *captured,
    Atom *pattern, SpaceOccurrenceCursor *out);
/* Select and lend the next captured row in one shared read transition.
 * An optional payload stays borrowed from the pinned view; the optional
 * index has view-local coordinates. No matching is performed. */
SpaceOccurrenceCursorStep space_occurrence_cursor_next(
    SpaceOccurrenceCursor *cursor, CettaIndex *logical_index_out, Atom **atom_out);
/* The atom at an index the cursor yielded, from the rows it captured. */
Atom *space_occurrence_cursor_atom(const SpaceOccurrenceCursor *cursor,
                                   CettaIndex index);
/* Native rows remain borrowed from their pin. A backend source's lent row is
 * copied into the receiving arena so later source advancement or release
 * cannot invalidate an answer, matcher or binding which retains the value. */
Atom *space_occurrence_cursor_receive_atom(const SpaceOccurrenceCursor *cursor,
    CettaIndex index, Arena *arena);
Atom *space_occurrence_cursor_identity(const SpaceOccurrenceCursor *cursor,
    CettaIndex index, Arena *arena);
void space_occurrence_cursor_release(SpaceOccurrenceCursor *cursor);
void space_occurrence_cursor_storage_census(const SpaceOccurrenceCursor *cursor,
                                           CettaStorageCensus *census);

bool space_contains_exact(Space *s, Atom *atom);
/* Exact membership for `(head argument...)`, presented as a borrowed term
 * view.  `out_applicable` is false when the active backend cannot answer from
 * its authoritative exact-membership projection. */
bool space_contains_exact_symbol_application(
    Space *s, SymbolId head, Atom *const *arguments,
    CettaExprLen argument_count, bool *out_applicable);
/* Exact fragment of match existence.  The result is applicable only when a
   ground, structurally indexable pattern has a complete candidate frontier
   whose rows are all exact.  Backend-primary PathMap answers through its
   direct structural membership operation when its whole-row exactness summary
   proves the same premise; if that operation is unavailable, the fragment
   declines instead of materializing a native shadow. */
bool space_match_exists_ground_exact(Space *s, Atom *pattern,
                                     bool *out_applicable);
/* Exact match existence for an expression presented as borrowed top-level
 * coordinates.  Native storage answers from its canonical AtomId presence
 * projection only when every stored row is structurally exact. */
bool space_match_exists_ground_exact_expression_coordinates(
    Space *s, Atom *const *coordinates, CettaExprLen coordinate_count,
    bool *out_applicable);
/* O(1)-amortized alpha-aware membership over native spaces, including native
   overlays; *out_applicable is false for non-native backends. */
bool space_contains_canonical(Space *s, Atom *atom, bool *out_applicable);
/* Membership used by add-atom-nodup.  It preserves the active backend's
 * structural identity, exact identity for ground atoms, and alpha identity
 * for variable-bearing atoms. */
bool space_contains_for_add_nodup(Space *s, Atom *atom);
CettaMatchCompletion space_contains_for_add_nodup_checked(Space *s, Atom *atom);
CettaIndex space_exact_match_indices64(Space *s, Atom *atom, CettaIndex **out);
uint32_t space_exact_match_indices(Space *s, Atom *atom, uint32_t **out);
bool space_contains_only_exact_atoms(Space *s);
bool space_atom_is_exact_indexable(Atom *atom);
/* Whether every row that could match the ground expression `pattern` is an
 * exact row, read from the exact index's count of the other rows by head
 * symbol.  False when the index cannot answer: an overlay, a backend-primary
 * space, or a stale index it may not rebuild here (inside a deferral window,
 * or while rows are marked). */
bool space_exact_frontier_certified(Space *s, Atom *pattern);
/* Remove every row equal to one of the ground atoms IDS, all of head HEAD,
 * in one pass through the exact index.  False, having removed nothing, when
 * the index cannot answer for them: no current index, a row under HEAD or
 * without a head that holds a variable, or rows the space cannot remove by
 * position. */
bool space_remove_exact_ids(Space *s, SymbolId head, const AtomId *ids,
                            size_t count);
/* The rows holding a ground pattern, ascending, read from a current exact
 * index without settling marks: physical rows while rows are marked. */
CettaIndex space_exact_index_rows64(Space *s, Atom *pattern, CettaIndex **out);
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
/* A successful match before substituting its result template. The pattern
 * and body have this activation's fresh variable identities. substitute
 * applies the full match and restores caller-visible aliases; private rule
 * bindings remain inside the query. All fields are borrowed for the call. */
typedef struct {
    Atom *pattern;
    Atom *body;
    Atom *(*substitute)(Arena *arena, Atom *term, void *context);
    void *context;
} QueryEquationInstantiation;
/* The caller bindings are projected as usual. An evaluator may also attach
 * its operational state while instantiating values with language-specific
 * provenance. NULL reports incomplete instantiation, never a refutation. */
typedef Atom *(*QueryEquationResultInstantiator)(
    const QueryEquationInstantiation *match, Arena *arena,
    Bindings *caller_bindings, void *context);
/* Conservative pre-materialization filter.  Returning false may only exclude
 * an equation occurrence already proved unable to match.  The equation store
 * retains authority for candidate order, multiplicity, freshening, and exact
 * matching. */
/* A revision-qualified caller may reject an impossible equation or provide a
 * derived exact-match pair.  A true result with every atom output NULL selects
 * the authored left and right sides.  A complete derived plan also supplies a
 * closed result for the case where the authored head matches but the refined
 * pattern does not; this preserves the difference between an unknown call and
 * a recognized equation whose guard produces zero answers.  The optional
 * frame schema is the complete, sorted inventory of unqualified variable ids
 * owned by this equation activation.  It remains useful with the authored
 * atoms and is interpreted independently of a partial atom override.  The
 * callback never establishes a match, and syntax must outlive the synchronous visit.
 * A supplied frame schema is borrowed here and retained by each activation. */
typedef bool (*QueryEquationCandidatePlan)(
    Atom *equation, void *ctx,
    Atom **match_pattern, Atom **result_template,
    Atom **refinement_failure_template, Atom **refinement_fallback_pattern,
    BindingsFrameSchema **frame_schema);

void query_results_init(QueryResults *qr);
bool query_results_push(QueryResults *qr, Atom *result, Bindings *b);
bool query_results_push_move(QueryResults *qr, Atom *result, Bindings *b);
void query_results_diag_set_capacity_limit_override(CettaCount limit);
CettaCount query_equations_visit(Space *s, Atom *query, Arena *a,
                                 QueryResultVisitor visitor, void *ctx);
CettaCount query_equations_visit_planned(
    Space *s, Atom *query, Arena *a,
    QueryEquationCandidatePlan plan, void *plan_ctx,
    QueryResultVisitor visitor, void *visitor_ctx);
/* Match exactly one admitted (= lhs rhs) atom against query.  This is the
   single-candidate form of query_equations_visit: it applies the same
   freshening, bidirectional matching, visible-variable projection, and RHS
   substitution, but performs no candidate selection. */
CettaCount query_equation_visit(Atom *equation, Atom *query, Arena *a,
                                QueryResultVisitor visitor, void *ctx);
/* The same match and projection, with a synchronous result instantiator
 * before ordinary substitution would erase the matched fields' roles. */
CettaMatchCompletion query_equation_visit_instantiated(
    Atom *equation, Atom *query, Arena *arena,
    QueryEquationResultInstantiator instantiate, void *instantiate_context,
    QueryResultVisitor visitor, void *visitor_context, CettaCount *emitted);
/* Same per-row freshening/matching/projection, with absence certified only
 * by a completed refutation. Incomplete matching or RHS/projection failure
 * never becomes an empty successful visit. pair_budget limits the matcher's
 * worklist quanta; UINT64_MAX requests its ordinary unbounded finite visit. */
CettaMatchCompletion query_equation_visit_checked(
    Atom *equation, Atom *query, Arena *arena,
    QueryResultVisitor visitor, void *context, uint64_t pair_budget,
    CettaCount *emitted);
/* Decide the same freshened head match without instantiating or visiting the
 * RHS. Empty producers still cover their input. Incomplete matching supplies
 * neither a match nor a refutation; no rule-use occurrence is emitted. */
CettaMatchCompletion query_equation_head_match_checked(
    Atom *equation, Atom *query, Arena *arena, uint64_t pair_budget);
/* Apply an explicitly admitted descriptor. Mismatch, invalid input and
 * capacity remain distinct; emitted is zero unless a body was published. */
CettaTermMatchStatus query_rule_visit(const CettaRuleDescriptor *rule,
    Atom *query, Arena *a, QueryResultVisitor visitor, void *ctx,
    CettaCount *emitted);
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
bool space_equations_may_match_unnamed_head(Space *s);
/* A clean local equation index is already a maintained absence certificate.
 * Mutations dirty or update it; composed/overlay views need their full reader. */
static inline bool space_directional_index_proves_absent(const Space *s) {
    return s && !s->overlay_base && !s->dep_count &&
        !s->native.eq_idx_dirty && !s->native.eq_idx.directional_present;
}
bool space_directional_rules_may_match_general(Space *s, SymbolId head);
static inline bool space_directional_rules_may_match_known_head(Space *s, SymbolId head) {
    return !space_directional_index_proves_absent(s) &&
        space_directional_rules_may_match_general(s, head);
}
bool space_extended_rule_head_arity_bounds(Space *s, SymbolId head,
    CettaExprLen *minimum, CettaExprLen *maximum, bool *has_exact,
    CettaExprLen query_arity, unsigned admitted_forms);
/* Revision-pinned least fixed point of the generated relational-effect
 * algebra over user-equation dependencies.  A named symbol with no visible
 * equation is inert at the pinned revision; malformed, variable-headed, or
 * higher-order dependencies conservatively carry the generated uncertain
 * effect. */
CettaGsltQueryEffect space_query_effect_for_head(
    Space *s, SymbolId head, bool *out_defined);
/* The same analysis for an executor that admits each variable-headed call
 * when it runs, by the head the variable then holds: the effects of the heads
 * the definition names, and in *out_dynamic whether it makes such a call.
 * Every other reader uses space_query_effect_for_head, which counts such a
 * call as the uncertain effect. */
CettaGsltQueryEffect space_query_static_effect_for_head(
    Space *s, SymbolId head, bool *out_defined, bool *out_dynamic);
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
static inline bool space_rule_head_arity_bounds(Space *s, SymbolId head,
    CettaExprLen *minimum, CettaExprLen *maximum, bool *has_exact,
    CettaExprLen query_arity, unsigned admitted_forms) {
    if ((admitted_forms & CETTA_RULE_SYNTAX_ORDINARY) &&
        (!(admitted_forms & CETTA_RULE_SYNTAX_DIRECTIONAL) ||
         space_directional_index_proves_absent(s)))
        return space_equation_head_arity_bounds(s, head, minimum, maximum,
                                                has_exact, query_arity);
    return space_extended_rule_head_arity_bounds(s, head, minimum, maximum,
                                                has_exact, query_arity, admitted_forms);
}
/* Both rule syntax and graded-equation visibility are caller policy. */
static inline bool space_rule_head_arity_bounds_view(Space *s, SymbolId head,
    CettaExprLen *minimum, CettaExprLen *maximum, bool *has_exact,
    CettaExprLen query_arity, unsigned admitted_forms, bool include_graded) {
    if (!include_graded || !(admitted_forms & CETTA_RULE_SYNTAX_ORDINARY))
        return space_rule_head_arity_bounds(s, head, minimum, maximum,
            has_exact, query_arity, admitted_forms);
    CettaExprLen lower = 0u, upper = 0u;
    bool exact = false;
    bool found = space_equation_head_arity_bounds_view(
        s, head, &lower, &upper, &exact, query_arity);
    if (admitted_forms & CETTA_RULE_SYNTAX_DIRECTIONAL) {
        CettaExprLen directional_lower = 0u, directional_upper = 0u;
        bool directional_exact = false;
        if (space_rule_head_arity_bounds(s, head, &directional_lower,
                &directional_upper, &directional_exact, query_arity,
                CETTA_RULE_SYNTAX_DIRECTIONAL)) {
            if (!found || directional_lower < lower) lower = directional_lower;
            if (!found || directional_upper > upper) upper = directional_upper;
            exact = exact || directional_exact;
            found = true;
        }
    }
    if (minimum) *minimum = lower;
    if (maximum) *maximum = upper;
    if (has_exact) *has_exact = exact;
    return found;
}
/* MAM loop-body view entry guard: head resolves to exactly one equation, no
 * equation without a head symbol can match it, and the space has no overlay
 * base (necessary condition for the deterministic-tail loop lane). */
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
/* Whether a call of `head` with `arity` arguments takes the untyped call
 * policy: every argument evaluated, its result evaluated without an
 * expected type, nothing type-checked.  HE types every call of a head that
 * declares any type, so a call at an arity no arrow declares is HE's
 * IncorrectNumberOfArguments; PeTTa's typing reads arrows per arity. */
bool space_head_call_untyped(Space *s, SymbolId head, CettaExprLen arity);
/* Whether any type annotation names the head, whatever its type or arity.
 * A dialect that type-checks calls does so for such a head even at an arity
 * no signature covers. */
bool space_head_declares_type(Space *s, SymbolId head);
/* Stored declarations in the visible space and its imports, excluding the
 * host's intrinsic type metadata. PeTTa's authored builtin contracts use this
 * view; general type inquiry retains space_head_declares_type. */
bool space_head_declares_stored_type(Space *s, SymbolId head);
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
/* As above, and reports in `completed_out` the steps taken (0 when not
 * applicable), so a caller with a step budget can charge them. */
SpacePreparedRegisterStep space_prepared_equation_run_register_loop_counted(
    const SpacePreparedEquation *plan, Atom *call, Arena *result_arena,
    size_t max_steps, Atom **result_out, size_t *completed_out);
SpacePreparedRegisterStep space_prepared_equation_run_register_recursion(
    const SpacePreparedEquation *plan, Atom *call, Arena *result_arena,
    size_t max_calls, Atom **result_out);

/* ── Space Registry (named spaces) ─────────────────────────────────────── */

typedef struct {
    SymbolId key;
    NameId name_id;
    /* Only unknown, runtime-created symbol names take this route. The
       borrow owns the key until this entry is removed or its registry dies. */
    RegisteredSymbolBorrow text_key;
    Atom *value;  /* Usually a grounded space atom, but can be anything */
} RegistryEntry;

struct RegistryStates;

typedef struct {
    RegistryEntry *entries;
    uint32_t len, cap;
    /* Bindings not beginning with `&`. HE reads both kinds as tokens;
       other dialects retain their own named-space evaluation policy. */
    uint32_t word_len;
    RegistryEntry inline_entries[16];
    NameKeyTable *name_keys;
    RegisteredSymbols *text_keys; /* lazy, registry-owned namespace */
    /* Open-addressed entry index. Slots store entry-index + 1; zero is empty.
       Iteration order remains the insertion order in entries. */
    uint32_t *index_slots;
    uint32_t index_cap;
    uint32_t inline_index_slots[32];
    /* PeTTa's named state, a namespace beside the bindings; NULL until a
       state is first set. */
    struct RegistryStates *states;
} Registry;

void registry_init(Registry *r);
void registry_free(Registry *r);
void registry_bind_id(Registry *r, SymbolId key, Atom *value);
bool registry_bind_id_checked(Registry *r, SymbolId key, Atom *value);
Atom *registry_lookup_id(Registry *r, SymbolId key);
bool registry_bind_symbol(Registry *r, const Atom *name, Atom *value);
Atom *registry_lookup_symbol(const Registry *r, const Atom *name);
/* Removal releases the name, not the caller-owned value. */
bool registry_unbind_symbol(Registry *r, const Atom *name);
Atom *registry_entry_symbol(const Registry *r, uint32_t index, Arena *owner);
bool registry_copy_entry(Registry *dst, const Registry *src,
                          uint32_t index, Atom *value);

/* Prepare a replacement binding namespace without changing its destination.
 * Values remain caller-owned. The caller holds exclusive binding access from
 * preparation through publication; the separate named-state namespace is
 * neither copied nor replaced. Publication consumes the prepared namespace
 * without allocation and invalidates borrowed entry/key pointers. */
typedef struct RegistryBindingPublication RegistryBindingPublication;
RegistryBindingPublication *registry_binding_publication_new(Registry *destination);
bool registry_binding_publication_copy(RegistryBindingPublication *publication,
    const Registry *source, uint32_t index, Atom *value);
void registry_binding_publication_commit(RegistryBindingPublication *publication);
void registry_binding_publication_free(RegistryBindingPublication *publication);
void registry_binding_publication_storage(const RegistryBindingPublication *publication,
    CettaStorageCensus *census);
void registry_bind(Registry *r, const char *name, Atom *value);
Atom *registry_lookup(Registry *r, const char *name);
bool registry_bind_name(Registry *r, Atom *name_key, Atom *value);
/* Both key graph and value are independently owned until registry disposal. */
bool registry_bind_snapshot_name(Registry *r, const AtomDeepCopySession *snapshot,
    const Arena *destination, const Atom *source_key, Atom *value);
Atom *registry_lookup_name(Registry *r, Atom *name_key);
const Atom *registry_entry_name_key(const Registry *r, uint32_t index);

/* An admission-time value view and the names actually read from it.  Values
 * are copied into the caller's arena; pointed-to resources keep their own
 * revision/lifetime contracts.  A stale view is never silently rebased. */
typedef struct RegistryReadView {
    Registry initial;
    Registry consulted;
    Arena *arena;
    bool initialized;
    bool passive;
    bool stale;
    bool incomplete;
    const char *refusal;
    Atom *stale_name;
    Atom *stale_expected;
    Atom *stale_actual;
    Registry *active_source;
    struct RegistryReadView *active_parent;
} RegistryReadView;

bool registry_read_view_init(RegistryReadView *view, Registry *source,
                            Arena *arena);
/* Observe the value selected by existing admitted views without changing it.
 * Both present and absent names enter the dependency set. Failed recording
 * invalidates reuse evidence but preserves the lookup result. Atom wrappers
 * and immutable data are copied; external resource owners remain borrowed. */
bool registry_read_view_init_passive(RegistryReadView *view, Arena *arena);
void registry_read_view_free(RegistryReadView *view);
bool registry_read_view_validate(RegistryReadView *view, Registry *source);
/* Receiver compatibility does not poison the source's sticky observations. */
bool registry_read_view_compatible(const RegistryReadView *view, Registry *source);
RegistryReadView *registry_read_view_enter(RegistryReadView *view,
                                          Registry *source);
void registry_read_view_leave(RegistryReadView *previous);

/* PeTTa's named state: the reference's global variables, whose names are
   independent of the names bound above.  Each state owns its value's
   storage, and setting a state copies the value in and releases the value
   it replaces, so a reader copies a stored value out before the state can
   next change, and never keeps it. */
Atom *registry_state_lookup(const Registry *r, SymbolId key);
bool registry_state_set(Registry *r, SymbolId key, Atom *value);
Atom *registry_state_lookup_symbol(const Registry *r, const Atom *name);
bool registry_state_set_symbol(Registry *r, const Atom *name, Atom *value);
uint32_t registry_state_count(const Registry *r);
bool registry_state_entry(const Registry *r, uint32_t index, Arena *owner,
                          Atom **key_out, Atom **value_out);

/* Canonical structural reference: (resolve-name (quote <closed-key>)). */
bool registry_ref_name_key(Atom *ref, Atom **name_key_out);
Atom *registry_lookup_ref(Registry *r, Atom *ref);

/* Resolve a space reference (symbol like &self, &ws, or grounded space atom)
   to a Space pointer. Returns NULL if not a space. */
Space *resolve_space(Registry *r, Atom *ref);

/* Remove an atom from a space (by structural equality). Returns true if found. */
bool space_remove(Space *s, Atom *atom);
CettaMatchCompletion space_remove_checked(Space *s, Atom *atom);
/* Interpret comparisons while retaining the original mutable occurrence and
 * the ordinary exact-then-unique-alpha removal policy. Comparisons borrow
 * held syntax/resources and must remain stable for the complete operation.
 * Representation indexes cannot refute an interpreted comparison.
 * INCOMPLETE publishes no removal; REFUTED includes ambiguous alpha matches. */
typedef CettaMatchCompletion (*SpaceAtomComparison)(Atom *left, Atom *right);
CettaMatchCompletion space_remove_compared(Space *s, Atom *atom,
    SpaceAtomComparison equal, SpaceAtomComparison alpha_equal);
CettaMatchCompletion space_contains_compared(Space *s, Atom *atom, SpaceAtomComparison equal);
/* Resolve removal without changing the store. The caller retains the Space,
 * body and protection mask for the search lifetime. Each step performs at
 * most one comparison; incomplete comparisons retain their exact position.
 * Protection is applied after exact precedence/global alpha uniqueness, so
 * hiding a kept occurrence cannot change which removal class applies.
 * Native logical coordinates are pinned by read; mutation invalidates it. */
typedef struct {
    SpaceReadToken read;
    CettaIndex first, next, end, selected;
    uint8_t alpha_count;
    bool alpha, exact_seen, selected_protected, finished;
} SpaceRemovalSearch;
typedef enum {
    SPACE_REMOVAL_PROGRESS, SPACE_REMOVAL_SELECTED, SPACE_REMOVAL_UNMATCHED,
    SPACE_REMOVAL_PROTECTED, SPACE_REMOVAL_INCOMPLETE, SPACE_REMOVAL_INVALIDATED,
} SpaceRemovalSearchStep;
bool space_removal_search_init(Space *space, SpaceRemovalSearch *search);
SpaceRemovalSearchStep space_removal_search_step(SpaceRemovalSearch *search,
    Atom *body, SpaceAtomComparison equal, SpaceAtomComparison alpha_equal,
    const uint8_t *protected_occurrences, CettaCount protected_length);
/* Consume the selected physical native occurrence, preserving the store's
 * existing ordered/swap-removal discipline. A stale token removes nothing. */
CettaMatchCompletion space_remove_occurrence_checked(Space *space, SpaceReadToken read, CettaIndex index);
bool space_contains_atom_id(const Space *s, AtomId atom_id);
bool space_remove_atom_id(Space *s, AtomId atom_id);
bool space_remove_atom_ids_batch(Space *s, const AtomId *atom_ids,
                                 CettaCount atom_count,
                                 CettaCount *out_removed);

/* Apply one stable contraction to the current logical occurrence sequence.
 * A nonzero mask entry removes that exact occurrence; every retained
 * occurrence preserves its prior relative order and multiplicity. */
bool space_remove_occurrence_mask_stable(
    Space *s, const uint8_t *remove_mask, CettaCount mask_len,
    CettaCount *out_removed);

/* ── Match Indexing ─────────────────────────────────────────────────────── */

/* Return backend-private candidate coordinates for a match pattern via its
   discrimination index. Dereference them with space_match_candidate_at64;
   they need not share the public logical get-atoms order.
   For small spaces (< MATCH_TRIE_THRESHOLD), returns all indices.
   Caller must free(*out). Returns count. */
CettaIndex space_match_candidates64(Space *s, Atom *pattern, CettaIndex **out);
/* Native candidate selection through borrowed expression coordinates. Reads
 * finish before return; only candidate coordinates escape. False leaves the
 * caller on its ordinary query route. Order and duplicate row occurrences
 * remain those of space_match_candidates64. */
bool space_match_candidates_expression_coordinates64(
    Space *s, Atom **coordinates, CettaExprLen length,
    CettaIndex **out, CettaIndex *count);

uint32_t space_match_candidates(Space *s, Atom *pattern, uint32_t **out);
Atom *space_match_candidate_at64(const Space *s, CettaIndex idx);
/* Native flat-match aggregations; false leaves the caller on ordinary search.
 * A visitor may see a prefix before a later row declines, so its state must
 * be local and discarded on failure. Overlays and other backends decline
 * before visiting any row. */
bool space_native_flat_conjunction_count(
    Space *space, Atom *const *patterns, size_t pattern_count,
    uint64_t *count_out);

bool space_native_flat_conjunction_int_moments(
    Space *space, Atom *const *patterns, size_t pattern_count,
    size_t column_pattern, size_t column,
    uint64_t *count_out, __int128 *sum_out,
    int64_t *product_out, bool *product_ok);

bool space_native_flat_pattern_each_int(
    Space *space, Atom *pattern, size_t column,
    bool (*each)(int64_t value, void *ctx), void *ctx);

/* Count, and the sum of one integer column, over a finite path of flat
 * patterns. Neighbours share exactly one variable; non-neighbours share
 * none. Keyed messages preserve occurrence multiplicity without expanding
 * the join. Each stage scans the captured space rows; false declines. */
bool space_native_flat_chain_aggregate(
    Space *space, Atom *const *patterns, size_t pattern_count,
    bool with_column, size_t column_pattern, size_t column,
    __int128 *count_out, __int128 *sum_out);

/* The count observation of the same path, with caller bindings resolved
 * before planning. Temporary pattern views belong to scratch; no answer
 * binding escapes. Failure leaves count_out unchanged. */
bool space_native_flat_chain_count(
    Space *space, Arena *scratch, Atom *const *patterns, size_t pattern_count,
    const Bindings *seed, uint64_t *count_out);

/* Select the grouped path realization or a prepared cyclic intersection.
 * Complete cardinality is the observation; ordinary matching retains all
 * other observations and any encoding/equality the realizations decline. */
bool space_native_relational_count(
    Space *space, Arena *scratch, Atom *const *patterns, size_t pattern_count,
    const Bindings *seed, uint64_t *count_out);

/* The same ordered walk for a pattern with nested structure: each matching
 * ground row yields the integer bound to `variable`. */
bool space_native_pattern_each_int(
    Space *space, Atom *pattern, VarId variable,
    bool (*each)(int64_t value, void *ctx), void *ctx);

/*
 * Try an exact multiplicity-preserving COUNT without constructing bindings
 * or result atoms.  The admitted fragment is backend-defined but must be a
 * semantic subset of ordinary matching; false means "use the oracle path."
 */
bool space_match_count_flat_linear64(Space *s, Arena *scratch,
                                     Atom *pattern, uint64_t *count,
                                     CettaIndex *examined);
/* Count through a borrowed query presentation.  `open_bindings` governs
 * unresolved query roots; stored-row bindings are existentially hidden by
 * the count observer and may be erased only by a backend which checks their
 * remaining equality constraints exactly. */
bool space_match_count_flat_linear_view64(
    Space *s, Arena *scratch, const CettaGsltTermViewV1 *view,
    CettaGsltTermViewOpenBindingObservationV1 open_bindings,
    uint64_t *count,
    CettaIndex *examined);
/* Boolean image of the same flat match producer.  This interface keeps the
 * observer independent of evaluator syntax; the current native realization
 * reuses exact occurrence aggregation and otherwise the exact count fold. */
bool space_match_exists_flat_linear_view64(
    Space *s, Arena *scratch, const CettaGsltTermViewV1 *view,
    CettaGsltTermViewOpenBindingObservationV1 open_bindings,
    bool *exists,
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
bool space_query_is_connective(const Atom *pattern);
/* Whether a match pattern may be flattened into one leg of a chain: not a
 * connective, and neither a variable nor headed by one. */
bool space_query_can_flatten(const Atom *pattern);
bool space_query_bindings(Space *space, Arena *arena, Atom *pattern,
                          const Bindings *seed, BindingSet *out);

typedef struct SpaceQueryCursor SpaceQueryCursor;
typedef enum {
    SPACE_QUERY_CURSOR_PROGRESS,
    SPACE_QUERY_CURSOR_ROW,
    SPACE_QUERY_CURSOR_END,
    SPACE_QUERY_CURSOR_INCOMPLETE,
    SPACE_QUERY_CURSOR_INVALIDATED,
    SPACE_QUERY_CURSOR_CAPACITY,
} SpaceQueryCursorStep;

/* The caller retains the Space and its overlay bases, query syntax and result arena. The cursor
 * owns its traversal, seed bindings and native occurrence pins. One native
 * step advances a connective or checks up to 32 canonical matcher actions
 * (structural nodes and alias retries) of one physical candidate.
 * Equality/occurs-check/binding primitives have
 * separate byte and CPU complexity; a node grant is not a CPU bound.
 * Backends without this capability retain their canonical atomic query and
 * an owned result cursor. Such fallback preparation is not incremental.
 * Completed fallback leaves may publish rows even if another leaf refuses;
 * that remainder stays INCOMPLETE after these rows, without replaying the
 * backend or accepting its unqualified partial rows. INCOMPLETE parks this
 * query's undecided work; CAPACITY reports failed cursor construction or
 * representation growth. Neither permits END or discarding the cursor. */
SpaceQueryCursor *space_query_cursor_new(Space *space, Arena *arena,
                                         Atom *pattern, const Bindings *seed);
/* Capture the query's original ordered bag. Native flat bags share one
 * pinned occurrence view, including across later copy-on-write removals;
 * each factor reads that same bounded view. Other spaces retain canonical
 * atomic preparation and its complete/unknown result contract. Read tokens
 * and witnesses remain those originally observed. This grants no permission
 * to advance under changed dependencies: the controller owns that policy.
 * Spaces/universes and the result arena retain the lifetime above. */
SpaceQueryCursor *space_query_cursor_new_captured(Space *space, Arena *arena,
    Atom *pattern, const Bindings *seed);
/* Clone a quiescent query's exact continuation, captured reads, last row
 * witnesses and prepared matcher/fallback state without querying or matching.
 * The original is unchanged on refusal. Mutable cursor metadata is independent;
 * syntax, Spaces/universes and the result arena retain the same borrowed
 * lifetime contract as new(). Destroying the original cursor is safe, but this
 * does not make a clone independent of its source parent/arena lifetime. */
SpaceQueryCursor *space_query_cursor_clone(const SpaceQueryCursor *source);
/* Relocate private syntax, all binding carriers and prepared matcher state
 * through one allocation-domain copy. Query-sequence pointer spans gain
 * destination storage while their Atom entries use that same graph map.
 * Spaces/universes, occurrence pins and original read/witness tokens remain
 * under their existing external lifetime and revision authority. Explicit
 * identity pairs retain closed external Atom regions; these are not retired.
 * No query or matcher step is performed. Refusal leaves the source unchanged;
 * discard the unpublished enclosing copy and its destination arenas. */
SpaceQueryCursor *space_query_cursor_copy_to_arena(const SpaceQueryCursor *source,
    Arena *arena, AtomDeepCopySession *atoms);
bool space_query_cursor_incremental(const SpaceQueryCursor *cursor);
SpaceQueryCursorStep space_query_cursor_step(SpaceQueryCursor *cursor,
                                             Bindings *out);
SpaceQueryCursorStep space_query_cursor_step_with_budget(SpaceQueryCursor *cursor,
    uint64_t match_nodes, Bindings *out);
/* Captured source authorities, including every inherited overlay source even
 * when the query has no rows. Consumers retain these reads after the cursor
 * returns so later result publication can revalidate the same observation.
 * The visitor sees the original tokens, including an invalidated token. */
typedef bool (*SpaceQueryReadVisitor)(SpaceReadToken read, void *context);
bool space_query_cursor_visit_reads(const SpaceQueryCursor *cursor,
    SpaceQueryReadVisitor visitor, void *context);
/* Witnesses name physical rows, including repeated equal payloads. Valid
 * only after ROW and before the next step. Atomic backend fallbacks cannot
 * manufacture such witnesses and return false. */
typedef bool (*SpaceQueryWitnessVisitor)(SpaceReadToken read,
    CettaIndex logical_index, void *context);
bool space_query_cursor_visit_witnesses(const SpaceQueryCursor *cursor,
    SpaceQueryWitnessVisitor visitor, void *context);
/* General occurrence identities. The positional visitor above refuses a
 * non-positional witness before invoking its visitor at all. Both visitors
 * report evidence, not mutation authority or a complete read footprint. */
typedef bool (*SpaceQueryOccurrenceVisitor)(SpaceReadToken read,
    Atom *identity, void *context);
bool space_query_cursor_visit_occurrences(const SpaceQueryCursor *cursor,
    SpaceQueryOccurrenceVisitor visitor, void *context);
void space_query_cursor_free(SpaceQueryCursor *cursor);
void space_query_cursor_storage_census(const SpaceQueryCursor *cursor,
                                       CettaStorageCensus *census);

/* ── Type Lookup (from HE spec Space.lean) ─────────────────────────────── */

/* Get intrinsic type for grounded atom: int/float→Number, bool→Bool, string→String */
Atom *get_grounded_type(Arena *a, Atom *atom);

/* Get all types for an atom from the space.
   Returns count; fills out_types (arena-allocated array). */
/* Reduce grounded operators inside a type expression (e.g. (+ 1 1) -> 2). */
Atom *normalize_type_expr(Arena *a, Atom *ty);
Atom *normalize_type_expr_head(Arena *a, Atom *ty);

/* HE's get-type: every typing HE's type checker derives for `atom` (tuple
 * types, the atom's declared types and their supertypes, then function
 * applications, one per derivation), without evaluating it. [%Undefined%]
 * when nothing types it; none when every typing is an application error. */
uint32_t space_he_type_answers(Space *s, Arena *a, Atom *atom,
                               Atom ***out_types);
uint32_t get_atom_types(Space *s, Arena *a, Atom *atom,
                        Atom ***out_types);

/*
 * Return explicit `(: subject type)` declarations: own declarations before
 * imported ones, preserving occurrence order within each part. The returned
 * pointer array is caller-owned; its atoms live in the supplied arena and
 * are freshened per occurrence.
 */
uint32_t space_get_declared_types(
    Space *s, Arena *a, Atom *subject, Atom ***out_types);
/* Preserve the shared lookup's refusal status; a refused projection cannot
 * certify that the declaration set is empty. */
bool space_get_declared_types_checked(Space *s, Arena *a, Atom *subject,
    Atom ***out_types, uint32_t *count);

/* Conservative exclusion for relational (: subject type) queries. False
 * certifies that no current occurrence can have the requested subject's
 * outer constructor; true retains ordinary relational search. */
bool space_type_annotation_may_match_subject(Space *s, const Atom *subject);
bool space_type_annotations_have_only_symbol_subjects(Space *s);

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


void registry_storage_census(const Registry *registry, CettaStorageCensus *census);

#endif /* CETTA_SPACE_H */
