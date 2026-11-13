#ifndef CETTA_SPACE_H
#define CETTA_SPACE_H

#include <stdatomic.h>

#include "atom.h"
#include "rule_binding.h"
#include "generated/cetta_execution_contracts.generated.h"
#include "gslt_term_view_v1.h"
#include "name_key.h"
#include "match.h"
#include "subst_tree.h"
#include "term_universe.h"
#include "space_dense.h"
#include "space_tuples.h"
#include "space_skeleton.h"

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
    /* EQ_INDEX_BUCKETS buckets by head, made by the first equation with a
     * head: a space holding none keeps none. */
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
    /* A declaration row (: or :<) holds an identity-carrying value, such as
     * a state cell, whose observation can change while the declaration
     * revision stands still: typings computed through it are not kept. */
    bool has_identity_rows;
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
    /* A multiset read in the standard order of terms: its first atom is its
     * least, a read meets its atoms ascending, and the whole atom is the
     * key, so the order is a function of the atoms held and of nothing
     * else.  Its rows are a multiset's; the order is an index over them. */
    SPACE_KIND_PRIORITY = 5,
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
    /* The rows' presence keys, in the form their density allows
     * (PresenceSetForm): a table of the keys (open addressing,
     * CETTA_ATOM_ID_NONE empty) while it is smaller than a bitset over the
     * ids it covers, then that bitset. */
    uint8_t *id_present;      /* the bitset, or NULL while the keys are a table */
    uint64_t id_present_bits; /* its capacity in bits */
    AtomId *id_present_table;
    uint32_t id_present_table_cap, id_present_table_used;
    CettaIndex id_present_synced_len; /* logical prefix already reflected in id_present (lazy catch-up) */
    bool id_present_dirty;    /* removal/bulk invalidated bits: clear + full resync on next membership query */
    /* Two synced rows share a key: a removal then marks the bits stale
     * rather than taking its row's key out, which the other row still has. */
    bool id_present_shared;
    /* The bitset's keys that are no row's id, the alpha-canonical forms of
     * rows with variables: the bitset holds them, one reference a key set. */
    AtomId *id_present_keys;
    CettaIndex id_present_key_len, id_present_key_cap;
    TermUniverseResourceOwner *resource_owner;
    /* The heads whose ground facts the space keeps in relations (space.c,
     * "Relations kept as bits or tables"); NULL while it keeps none.  The
     * facts they hold, over every head, which every positional read past the
     * rows asks.  The epoch moves whenever a head is made, takes its
     * relation or gives it back: a reader that keeps a head's relation
     * between uses checks it. */
    struct SpaceDense *dense;
    CettaCount dense_facts;
    uint64_t dense_epoch;
    /* The head last found to keep rows, whose facts an add asks no more
     * than their header. */
    SymbolId dense_rows_head;
    bool eq_idx_dirty;
    bool ty_idx_dirty;
    bool exact_idx_dirty;
    bool has_non_exact_atoms;
    bool has_non_exact_atoms_dirty;
    /* Cached representation presence; occupies existing flag padding. */
    _Atomic bool has_proof_image;
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
            AtomId *id_present_table;
            uint32_t id_present_table_cap, id_present_table_used;
            CettaIndex id_present_synced_len;
            bool id_present_dirty;
            bool id_present_shared;
            AtomId *id_present_keys;
            CettaIndex id_present_key_len, id_present_key_cap;
            TermUniverseResourceOwner *resource_owner;
            struct SpaceDense *dense;
            CettaCount dense_facts;
            uint64_t dense_epoch;
            SymbolId dense_rows_head;
            bool eq_idx_dirty;
            bool ty_idx_dirty;
            bool exact_idx_dirty;
            bool has_non_exact_atoms;
            bool has_non_exact_atoms_dirty;
            _Atomic bool has_proof_image;
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
    /* Heads take their relations from their first fact
     * (space_dense_threshold), as under CETTA_SPACE_DENSE=eager. */
    bool dense_from_first;
    /* A live projection of a module's own storage, not a writable overlay. */
    bool module_own_view;
    /* Process-local lifetime identity.  A revision identifies a state only
       within one Space lifetime; instance_id prevents pointer reuse from
       reviving cached state from a freed Space at the same address. */
    uint64_t instance_id;
    uint64_t revision;
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
       publications preserve it: the rows keep their positions and new rows
       follow them, while the facts kept in relations, which follow the
       rows, move as rows are appended (space_rule_rows64).  An overlay
       folds the stamps of its actual base chain into projection currency,
       so unrelated Spaces cannot invalidate its equation view. */
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
    /* Set when the space lives only while an arena holds an atom of it: the
       evaluator's record of those holds.  NULL for a space its owner frees. */
    struct SpaceHold *arena_hold;
    /* A priority space's order over its rows (space.c, "Order index"); NULL
       for every other kind, and until an ordered read first asks for it. */
    struct SpaceOrder *order;
    /* The atoms added and removed while a reader reads the space as it
       stood when it began (space.c, "Entry reads"); NULL while none does. */
    struct SpaceJournal *journal;
} Space;

void space_init_with_universe(Space *s, TermUniverse *universe);
/* Failure leaves no registered observer/root or owned payload. */
bool space_try_init_with_universe(Space *s, TermUniverse *universe);
void space_init_overlay(Space *s, const Space *base);
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
    return s && s->module_own_view ? (Space *)s->overlay_base : s;
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
void space_free(Space *s);
/* Explicit holds for native descriptors that borrow a temporary Space
 * without carrying an Atom. The token survives an explicit owner teardown;
 * releasing it never dereferences the retired Space. Embeddings without
 * temporary-space ownership may omit these hooks. */
void *space_lifetime_pin(Space *s) __attribute__((weak));
void space_lifetime_unpin(void *hold) __attribute__((weak));
bool space_lifetime_pin_keeps_storage(void *hold) __attribute__((weak));
void space_lifetime_rebase(Space *s, const Space *base) __attribute__((weak));
/* Drain released temporary-space owners at an evaluator-safe boundary.
 * False when an enclosing execution still requires deferred cleanup. */
bool space_reclaim_unheld(void) __attribute__((weak));
void space_reclaim_resource_cycles(TermUniverse *) __attribute__((weak));
bool space_reclamation_due(void) __attribute__((weak));
void space_execution_analysis_cache_free_for_current_thread(void);
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
/* As space_add_atom_ids_batch, for atoms each its own alpha key (ground,
 * or with its variables numbered as an alpha key numbers them): a set finds
 * the ones it holds by their ids. */
bool space_add_alpha_keys_batch(Space *s, const AtomId *keys,
                                CettaCount count);
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

/* Relations kept as bits or tables.  The facts a space keeps in relations
 * count in its length and answer its membership tests, adds and removals; a
 * reader of rows that does not read relations gives them back to the rows
 * first: space_dense_settle for every head, or for the head HEAD only
 * (SYMBOL_ID_NONE: every head).  space_dense_settle_for_pattern settles the
 * head a pattern names, or every head when its head is not a symbol. */
void space_dense_settle(Space *s, SymbolId head);
void space_dense_settle_for_pattern(Space *s, const Atom *pattern);
/* space_dense_settle_for_pattern for an expression of LEN elements headed by
 * the symbol HEAD. */
void space_dense_settle_for_head(Space *s, SymbolId head, CettaExprLen len);
void space_dense_settle_for_patterns(Space *s, Atom *const *patterns,
                                     CettaExprLen count);
/* The facts held in relations, over every head. */
static inline CettaCount space_dense_facts(const Space *s) {
    return s ? s->native.dense_facts : 0u;
}
/* A value that moves whenever the space's relations change form: a head
 * made (its entries may move), a relation taken or given back. */
static inline uint64_t space_dense_epoch(const Space *s) {
    return s ? s->native.dense_epoch : 0u;
}
/* Count the facts the relations hold that match (h c1 ... ck), given as
 * the LEN COLUMNS: each a variable, distinct from the others, which takes
 * anything, or a ground atom.  A head that is a variable reads every
 * relation of k columns.  False when the relations cannot answer: in a
 * concurrent-evaluation scope, for a column a number other than an
 * integer, which may equal one by value, or for columns no relation has. */
bool space_dense_count_columns(const Space *s, Atom *const *columns,
                               CettaExprLen len, CettaCount *count);
/* A head's relation as its readers see it: ARITY columns, held as BITS
 * (two columns) or as a TABLE (the other unset).  DEEP: a relation of deep
 * facts whose spine is its head over holes, whose columns may hold
 * expressions. */
typedef struct {
    uint32_t arity;
    AtomId head_id;
    const CettaDenseRelation *bits;
    CettaTupleTable *table;
    bool deep;
} SpaceDenseForm;
/* The relation of HEAD in *FORM; false when the space keeps none for it
 * (or a concurrent-evaluation scope is open).  Facts of HEAD may also be
 * rows: a reader of a relation asks the rows too, between
 * space_dense_reading_begin and _end, which keep the guards from giving
 * the relation back meanwhile. */
typedef struct CettaProofImage CettaProofImage;
typedef struct CettaProofRowLayout CettaProofRowLayout;
typedef enum {
    SPACE_PROOF_IMAGE_DECLINED = 0,
    SPACE_PROOF_IMAGE_READY,
    SPACE_PROOF_IMAGE_CAPACITY,
} SpaceProofImageStatus;
/* Explicit data storage only: no source producer or execution permission.
 * Success retains IMAGE independently and publishes one append mutation.
 * The borrowed TU/symbol session must outlive the Space as for native rows. */
SpaceProofImageStatus space_attach_proof_image(Space *, SymbolId head,
    const CettaProofRowLayout *, CettaProofImage *);
typedef struct CettaProofBatchStore CettaProofBatchStore;
typedef struct CettaProofBatchTransaction CettaProofBatchTransaction;
typedef struct CettaProofBatchView CettaProofBatchView;
/* The full batch declarations are separate to keep ordinary Space clients
 * independent of producer storage internals. */
/* Failure keeps the old logical bag/image; READY preserves semantic revisions. */
SpaceProofImageStatus space_settle_proof_images(Space *);
/* The guarded bodies are cold on ordinary storage. False reports an
 * expansion refusal through the existing backend error channel. */
bool space_prepare_proof_read_slow(Space *s, const Atom *pattern);
bool space_has_proof_image_slow(const Space *, SymbolId head);
static inline bool space_prepare_proof_read(Space *s, const Atom *pattern) {
    const Space *read = s;
    while (read && !atomic_load_explicit(&read->native.has_proof_image, memory_order_acquire))
        read = read->overlay_base;
    return !read || space_prepare_proof_read_slow(s, pattern);
}
static inline bool space_has_proof_image(const Space *s, SymbolId head) {
    return s && atomic_load_explicit(&s->native.has_proof_image, memory_order_acquire) &&
        (head == SYMBOL_ID_NONE || space_has_proof_image_slow(s, head));
}

bool space_dense_form(const Space *s, SymbolId head, SpaceDenseForm *form);
/* Whether the relation PATTERN's head keeps is flat (a relation of deep
 * facts whose spine is the head over holes is), with PATTERN's length: then
 * each of PATTERN's elements reads one column of the relation's facts, an
 * expression holding variables matching the column's terms. */
bool space_dense_flat_reads(const Space *s, const Atom *pattern);
/* The relation's facts whose columns equal FIXED's (CETTA_DENSE_ANY free;
 * FIXED NULL leaves every column free), visited or counted, and every fact
 * held, counted.  A walk may build an index of the relation: it takes no
 * fact and changes no answer. */
bool space_dense_form_each(SpaceDenseForm form, const CettaDenseId *fixed,
                           CettaTupleVisit visit, void *context);
uint64_t space_dense_form_count(SpaceDenseForm form,
                                const CettaDenseId *fixed);
uint64_t space_dense_form_facts(SpaceDenseForm form);
/* The bits of HEAD, or NULL when the space keeps no bits for it: the
 * relations of two columns that are bits, as space_dense_form gives them. */
const CettaDenseRelation *space_dense_relation(const Space *s,
                                               SymbolId head);
/* Whether no row can match a fact of `head` beside its relation: the head
 * keeps a relation, none of its facts is a row, and no row's head is open. */
bool space_dense_head_rows_absent(Space *s, SymbolId head);
/* Whether the space keeps a flat relation of ARITY columns for HEAD, and
 * then in *HELD whether it holds the fact (HEAD c1 ... ck) of the ids
 * COLUMNS, which needs no record of its own. */
bool space_dense_holds_columns(const Space *s, SymbolId head,
                               const AtomId *columns, uint32_t arity,
                               bool *held);
/* Whether a fact the space keeps in a relation may meet PATTERN: some are
 * kept, and the pattern is no expression headed by a symbol, or its head
 * keeps facts in a relation.  A reader of rows alone answers PATTERN only
 * where this is false. */
bool space_dense_may_meet(const Space *s, const Atom *pattern);
/* The spine of the relation of deep facts S keeps for HEAD, copied into
 * OUT; false when S keeps none. */
bool space_dense_deep_spine(Space *s, SymbolId head, CettaSkeleton *out);
/* Add to the set S the COUNT facts of HEAD whose holes under SPINE are
 * HOLES (SPINE->holes ids each), as adding each fact's atom would: into
 * HEAD's relation while it keeps that spine, unless the set holds the fact,
 * and as a row otherwise.  False when a fact could not be added. */
bool space_add_deep_holes(Space *s, SymbolId head, const CettaSkeleton *spine,
                          const AtomId *holes, CettaCount count);
/* Whether a pattern names a head the space keeps a relation of its columns
 * for: the native query's loops read relations; the planners that read
 * rows would give them back to the rows. */
/* Whether one of PATTERNS may read the facts of a relation of deep facts
 * (its head keeps facts as a skeleton and holes), which the joins over
 * relations do not read. */
bool space_dense_deep_meets(const Space *s, Atom *const *patterns,
                            size_t count);
bool space_dense_meets_patterns(const Space *s, Atom *const *patterns,
                                size_t count);
/* Whether no fact a relation of the space keeps can match any of the
 * patterns: each is an expression whose head is a symbol with no relation
 * of its columns, so a reader of them may read the rows alone. */
bool space_dense_patterns_miss(const Space *s, Atom *const *patterns,
                               size_t count);
void space_dense_reading_begin(void);
void space_dense_reading_end(void);
/* Before adding IDS: a head whose facts among them reach the threshold
 * for a relation takes its relation now, so that none of them is a row. */
void space_dense_prepare_adds(Space *s, const AtomId *ids, CettaCount count);
/* Whether the space may keep the facts of HEAD in a relation: it admits
 * relations, and HEAD has not given its relation back to rows. */
bool space_dense_may_hold(const Space *s, SymbolId head);
/* Add the facts (h c1 ... ck), k = ARITY: h by its symbol and id, each
 * fact by the k ids of its columns in TUPLES.  While the space keeps a
 * relation of k columns for h the facts need no records; the others are
 * rows.  A set adds no fact it holds. */
bool space_add_dense_tuples(Space *s, SymbolId head, AtomId head_id,
                            uint32_t arity, const AtomId *tuples,
                            CettaCount count);
/* Take the ground fact ATOM from its relation, by its parts (a fact a
 * relation keeps may have no record of its own): the copies removed, 0 or
 * 1. */
CettaCount space_dense_remove_ground(Space *s, Atom *atom);
void space_discard_native_logical_view(Space *s);
/* Drop the full logical tuple projection while retaining revision-independent
 * equation/type descriptors. The backend mutation caller remains responsible
 * for invalidating either descriptor when its corresponding row class changes. */
void space_discard_native_logical_view_preserving_dispatch(Space *s);
void space_note_external_backend_mutation(Space *s);
Space *space_heap_clone_shallow(Space *src);
typedef enum {
    SPACE_ADDITIVE_COPY_UNSUPPORTED = 0,
    SPACE_ADDITIVE_COPY_READY,
    SPACE_ADDITIVE_COPY_FAILED,
} SpaceAdditiveCopyResult;
/* Make a fresh heap-owned bag from both inputs without giving their flat
 * kept relations back to rows. READY returns the complete result in OUT;
 * the caller owns it. UNSUPPORTED changes nothing and admits a generic
 * atom-copy route. FAILED destroys its private partial result and must not
 * be retried as if it were an unsupported representation. OUT is otherwise
 * NULL. The native route requires plain same-universe storage outside a
 * concurrent scope; ordered raw rows retain their relative order. */
SpaceAdditiveCopyResult space_heap_additive_copy(
    TermUniverse *destination_universe, Space *left, Space *right, Space **out);
void space_replace_contents(Space *dst, Space *src);
const char *space_kind_name(SpaceKind kind);
bool space_kind_from_name(const char *name, SpaceKind *out);
void space_set_strict(Space *s, bool strict);
/* A kind that keeps its atoms in an order and gives the order's own
 * operations (peek, pop, get, truncate): a stack, a queue, a priority
 * space.  Its reads follow that order, so a reader of rows in any other
 * order declines for it. */
bool space_is_ordered(const Space *s);
/* Whether that order is the order of its rows: a stack's and a queue's,
 * which keep their history.  A priority space's rows are a bag's, and its
 * order an index over them: its rows need not be kept in sequence, and a
 * read that takes no order (a count, a membership) reads it as a bag. */
bool space_rows_in_kind_order(const Space *s);
/* Whether a read of the space runs from its newest row to its oldest: a
 * stack's, whose order is the reverse of its rows'.  A queue's reads run
 * from the oldest. */
bool space_reads_newest_first(const Space *s);
bool space_is_stack(const Space *s);
bool space_is_queue(const Space *s);
bool space_is_priority(const Space *s);
/* The atom of a priority space with RANK atoms before it in its order; NULL
 * when it holds no such atom. */
Atom *space_priority_get_at64(const Space *s, CettaIndex rank);
/* Put COUNT logical positions of a priority space's live rows in the order
 * the space reads them. */
void space_priority_order_positions(Space *s, CettaIndex *positions,
                                    CettaIndex count);
/* The logical positions of the rows of a priority space equal to the ground
 * ATOM.  False when they could not be found; *OUT is the caller's to free. */
bool space_priority_equal_positions(Space *s, Atom *atom, CettaIndex **out,
                                    CettaIndex *count_out);
/* The logical positions of the rows of a priority space that may match
 * PATTERN, for a reader that takes them all, as a bag: the rows equal to a
 * ground pattern, or the rows of the ranges of the order the pattern's
 * ground front selects (the rows that begin with it and the rows holding a
 * variable inside it); in a space that keeps its general indexes, the rows
 * an index hands over where they are fewer.  False where the order cannot
 * search for the pattern; *OUT is the caller's to free. */
bool space_priority_candidates(Space *s, Atom *pattern, CettaIndex **out,
                               CettaIndex *count_out);
/* The logical positions of the rows a ground PATTERN matches, found through
 * the order where they are exactly the rows equal to it.  False where that
 * does not hold; *OUT is the caller's to free. */
bool space_priority_exact_positions(Space *s, Atom *pattern, CettaIndex **out,
                                    CettaIndex *count_out);
/* The logical positions of the rows that are the open ATOM, the same or the
 * same but for its variables' names; false when the order cannot find them. */
bool space_priority_alike_positions(Space *s, Atom *atom, CettaIndex **out,
                                    CettaIndex *count_out);
/* RANKS[i]: how many of the COUNT positions stand before POSITIONS[i] in the
 * order the space reads them.  False when the places could not be found. */
bool space_priority_dense_ranks(Space *s, const CettaIndex *positions,
                                CettaIndex count, CettaIndex *ranks);
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
/* Whether the set `s` keeps the row `atom`, which has variables, as its
 * alpha key when it admits it: a record no naming of the variables
 * changes. */
bool space_keys_open_row(Space *s, Atom *atom);
AtomId space_get_atom_id_at(const Space *s, uint32_t idx);
CettaCount space_length64(const Space *s);
AtomId space_get_atom_id_at64(const Space *s, CettaIndex idx);
Atom *space_get_at64(const Space *s, CettaIndex idx);
/* The row at IDX as a view built in DST (term_universe_view_atom) when the
 * space keeps it as an id of its universe; otherwise its atom, as
 * space_get_at64 reads it. */
Atom *space_view_at64(const Space *s, CettaIndex idx, Arena *dst);
Atom *space_get_at(const Space *s, uint32_t idx);
/* The logical positions that may hold a rule or a declaration: the rows,
 * which come before the facts kept in relations.  An append-only
 * publication keeps them in place and adds after them, so a reader of the
 * space's rules that reads on from where it stopped counts these, not the
 * length, whose relation facts move as rows are appended. */
CettaCount space_rule_rows64(const Space *s);
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

bool space_equation_cursor_init(Space *s, SymbolId head,
                                SpaceEquationCursor *cursor);
/* The extended projection uses the same revision-qualified occurrence order.
 * Its initial realization scans the logical member; ordinary-only readers
 * keep the indexed cursor. The mask is part of the captured query policy. */
bool space_rule_cursor_init(Space *s, SymbolId head, unsigned admitted_forms,
                            SpaceEquationCursor *cursor);
SpaceEquationCursorStep space_equation_cursor_next(
    SpaceEquationCursor *cursor, SpaceEquationOccurrenceId *out);

/*
 * Revision-qualified zipper over matching discrimination-trie leaves.
 * Pin a read token, prefix epoch, and occurrence ceiling at init; `next`
 * yields logical indices without copying the bag: in row order for a stack
 * or a queue, in the standard order of terms for a priority space, in the
 * index's own order for any other, since a space answers a bag.
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
    /* A stack's reads run from its newest row down: the scan, the flat
     * candidates and the merge of trie nodes go backwards, and a node's
     * leaf_pos counts the leaves left to read below (space_reads_newest_first). */
    bool descending;
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
    CettaIndex flat_len;
    CettaIndex flat_next;
    bool flat_mode;
    bool full_scan;
    CettaIndex full_next;
    /* A priority space read through its order index: the rows from the least
     * on, order_last the row yielded last. */
    bool order_scan;
    bool order_started;
    bool order_rent;
    bool order_offer_trie;
    uint32_t order_last;
    /* On a frozen view the cursor keeps its place in two things read
     * together: the Space's order, where order_live is the next row it may
     * read once known; and the rows marked since the view froze, of which
     * it has taken in the first order_gone_seen and holds, as a heap in the
     * order, those it has yet to read. */
    bool order_live_known;
    uint32_t order_live;
    CettaIndex order_gone_seen;
    uint32_t *order_gone;
    uint32_t order_gone_len, order_gone_cap;
    /* Rent or buy (space_order_price): the rows the walk has passed, and
     * the rows an index would hand the read in place of the rest of the
     * walk (the rows of an order are numbered in 32 bits), while the read
     * holds that index's offer: the trie's nodes a ground pattern descends
     * to, ending in none (order_offer_trie), or a position index's demand.
     * order_rent: the walk is rent toward an index that serves no read
     * yet. */
    uint32_t order_walked;
    uint32_t order_price;
    union {
        void *any;
        DiscNode **trie;
        CettaSpacePathDemand *demand;
    } order_offer;
    /* A pattern read as the ranges of the order whose rows may match it,
     * one after another (space_priority_front_search): pairs of a range's
     * first row and the row it ends before, found when the cursor opened;
     * the range being read, and whether its first row has been.
     * order_purge: rows a frozen cursor held to read later that stand
     * before the range now begun are dropped. */
    uint32_t *order_ranges;
    uint32_t order_range_len, order_range_at;
    bool order_range_open;
    bool order_purge;
    /* Whether the cursor reads the match trie's leaves, which a removal
     * takes rows out of: its view is then copied at a removal, where a view
     * of rows (and of the head index, which keeps a removed row until the
     * rows are contracted) is left on them. */
    bool index_reader;
    bool pinned;
    /* The row of the logical index `next` yielded last, which the atom read
     * that follows it takes without searching the marks. */
    bool has_last;
    CettaIndex last_logical;
    CettaIndex last_row;
    /* The facts the space's relation for the pattern kept when the cursor
     * began, lent after the rows, at logical indexes from the ceiling on;
     * shared by clones.  kept_turn: the rows are done, and kept_last is the
     * fact lent last. */
    struct SpaceKeptOccurrences *kept;
    size_t kept_next;
    size_t kept_last;
    bool kept_turn;
} SpaceOccurrenceCursor;

typedef enum {
    SPACE_OCCURRENCE_CURSOR_END = 0,
    SPACE_OCCURRENCE_CURSOR_ITEM,
    SPACE_OCCURRENCE_CURSOR_INVALIDATED,
} SpaceOccurrenceCursorStep;

void space_occurrence_cursor_init_empty(SpaceOccurrenceCursor *cursor);
bool space_occurrence_cursor_init(Space *s, Atom *pattern,
                                  SpaceOccurrenceCursor *cursor);
/* New attempt over the same pin and discrimination frontier: copy the
 * borrowed trie nodes, reset the leaf cursor, and take one pin. */
bool space_occurrence_cursor_clone_unstarted(
    const SpaceOccurrenceCursor *src, SpaceOccurrenceCursor *dst);
/* Clone the exact logical-update position of a live cursor.  The clone owns
 * an independent pin and traversal arrays while observing the same captured
 * rows, ceiling, and next occurrence as the source. */
bool space_occurrence_cursor_clone(
    const SpaceOccurrenceCursor *src, SpaceOccurrenceCursor *dst);
/* Select and lend the next captured row in one shared read transition.
 * An optional payload stays borrowed from the pinned view; the optional
 * index has view-local coordinates. No matching is performed. */
SpaceOccurrenceCursorStep space_occurrence_cursor_next(
    SpaceOccurrenceCursor *cursor, CettaIndex *logical_index_out, Atom **atom_out);
/* As space_occurrence_cursor_next, lending the row as its id in the
 * cursor's universe (space_occurrence_cursor_universe): nothing is decoded.
 * The id names a row the pin keeps until the cursor is released. */
SpaceOccurrenceCursorStep space_occurrence_cursor_next_id(
    SpaceOccurrenceCursor *cursor, CettaIndex *logical_index_out,
    AtomId *id_out);
/* As space_occurrence_cursor_next_id, but a fact the space keeps in a
 * relation of deep facts comes without a record: as its index among the
 * cursor's kept facts in *KEPT_OUT (SIZE_MAX for any other row, which comes
 * as its id in *ID_OUT). */
SpaceOccurrenceCursorStep space_occurrence_cursor_next_kept(
    SpaceOccurrenceCursor *cursor, AtomId *id_out, size_t *kept_out);
/* The spine and holes of the kept fact KEPT a cursor yielded: the holes
 * stay valid while the cursor lives, the spine while its space does. */
void space_occurrence_cursor_kept_fact(const SpaceOccurrenceCursor *cursor,
                                       size_t kept,
                                       const CettaSkeleton **skeleton_out,
                                       const AtomId **holes_out);
/* The record of the kept fact KEPT, made when it is first lent. */
AtomId space_occurrence_cursor_lend_kept(SpaceOccurrenceCursor *cursor,
                                         size_t kept);
/* The universe naming the rows a cursor lends by id. */
TermUniverse *space_occurrence_cursor_universe(
    const SpaceOccurrenceCursor *cursor);
/* The atom at an index the cursor yielded, from the rows it captured. */
Atom *space_occurrence_cursor_atom(const SpaceOccurrenceCursor *cursor,
                                   CettaIndex index);
/* Cursor-owned traversal arrays, plus the detached row/mark storage when
 * INCLUDE_VIEW is true.  Sharing owners pass INCLUDE_VIEW for exactly one
 * cursor on each view.  Live store rows, shared trie/head indexes and dense
 * kept-fact storage are not charged to the cursor by this account. */
size_t space_occurrence_cursor_accounted_bytes(
    const SpaceOccurrenceCursor *cursor, bool include_view);
void space_occurrence_cursor_release(SpaceOccurrenceCursor *cursor);

/* A captured occurrence view may select its declared heads later. The
 * shared index is pinned with the rows; appends, deletions and rewrites do
 * not turn the later selection into a query of the live Space. */
typedef struct {
    SpaceOccurrenceCursor rows;
    CettaSpaceHeadIndex *heads;
} SpaceHeadSnapshot;
bool space_head_snapshot_init(Space *space, SpaceHeadSnapshot *snapshot);
bool space_head_snapshot_select(const SpaceHeadSnapshot *snapshot,
    const SymbolId *heads, size_t count, SpaceOccurrenceCursor *cursor);
void space_head_snapshot_release(SpaceHeadSnapshot *snapshot);

/* ── Entry reads ─────────────────────────────────────────────────────────
 * A reader that begins now and reads later, as the later patterns of a
 * conjunctive match do, reads the space as it stood when it began, whatever
 * the program adds or removes meanwhile.  While one is open the space
 * journals the atoms it adds and removes, and nothing while none is.  A
 * reader of a space written since it began reads the space as it is, less
 * the atoms added since and with the atoms removed since: nothing is
 * copied.  The writes are those of the space the reader names and, for a
 * module's own view, of the storage it shows.
 *
 * The point a reader began at is data (space_entry_point) that an evaluator
 * carries from one pattern to the next, as it carries bindings.  A hold
 * keeps the journal, and with it the space, while something that may read
 * at the point lives; between holds the journal lasts until the space's
 * next write, so a pattern begun at once after its predecessor let go
 * (space_entry_reacquire) still reads at the point. */
typedef struct {
    /* The space the reader holds; NULL while none is held. */
    Space *space;
    /* The journal's count of writes when the reader began.  Each journal
     * counts in serials of its own, so the serial names the journal too: a
     * space makes a journal afresh once its last one went with a write. */
    uint64_t serial;
} SpaceEntry;

/* Begin reading S as it stands now, and hold it.  False, with nothing
 * begun, when out of memory. */
bool space_entry_open(Space *s, SpaceEntry *entry);
/* Begin reading S as it stands now without holding it: a reader that
 * reads at the point holds it again (space_entry_reacquire) before the
 * space's next write. */
bool space_entry_begin(Space *s, SpaceEntry *entry);
/* As space_entry_begin and space_entry_open, for a reader that names every
 * pattern it will read at the point (space_entry_watch), at once and before
 * any write: the space then journals only the writes of atoms some named
 * pattern's head may meet, so writes no pattern can meet cost nothing.  A
 * point begun otherwise reads every atom, and so does any pattern that may
 * begin with any head (SYMBOL_ID_NONE for space_entry_watch_head). */
bool space_entry_begin_watching(Space *s, SpaceEntry *entry);
bool space_entry_open_watching(Space *s, SpaceEntry *entry);
void space_entry_watch(Space *s, const Atom *pattern);
void space_entry_watch_head(Space *s, SymbolId head);
/* Hold the point ENTRY began at again, for a read of S, the space its
 * pattern names: false, counted as lost, when S is not the space it began
 * on or its journal no longer keeps every write since. */
bool space_entry_reacquire(Space *s, SpaceEntry *entry);
/* Let go of a hold of the space S (an entry's `space`), which holds one
 * more of the same while a copy of a continuation does. */
void space_entry_hold_again(Space *s);
void space_entry_release(Space *s);
void space_entry_close(SpaceEntry *entry);
/* ENTRY's point as data, its serial as a number, and back; the point read
 * back holds nothing. */
Atom *space_entry_point(Arena *arena, const SpaceEntry *entry);
bool space_entry_from_point(const Atom *point, SpaceEntry *entry);
/* Whether the space was written since the reader began. */
bool space_entry_written(const SpaceEntry *entry);
/* The same of a point not held, for a read of S, the space its pattern
 * names: also true when S is not the space it began on or its journal is
 * gone. */
bool space_entry_written_at(Space *s, const SpaceEntry *entry);
/* Whether a write was made since the reader began that the journal does
 * not itemize (a backend's own rewrite, a change to an imported module):
 * the space as it stood then cannot be read. */
bool space_entry_lost(const SpaceEntry *entry);
/* How many more times the space holds the atom ID than when the reader
 * began: its adds since, less its removals since. */
int64_t space_entry_added(const SpaceEntry *entry, AtomId id);
/* Visit each atom the space held more times when the reader began than it
 * does now and that PATTERN may match (the caller matches), with how many
 * more: the atoms removed since, less those added again.  A visit returning
 * false stops.  False when out of memory. */
typedef bool (*SpaceEntryRemovedVisit)(void *context, AtomId id,
                                       uint64_t copies);
bool space_entry_removed_each(const SpaceEntry *entry, Atom *pattern,
                              SpaceEntryRemovedVisit visit, void *context);
/* The occurrences PATTERN may match in the space as it stood when ENTRY
 * began, as the universe's atoms in a malloc'd array: those it holds now,
 * less the copies of atoms added since, with the atoms removed since.
 * False, counted as lost, when the space as it stood cannot be read (a
 * write the journal does not itemize) or out of memory; the reader then
 * reads the space as it is. */
bool space_entry_candidates(const SpaceEntry *entry, Atom *pattern,
                            Atom ***out, CettaCount *out_len);

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
/* space_exact_frontier_certified for a ground expression of LEN elements
 * headed by the symbol HEAD. */
bool space_exact_frontier_certified_head(Space *s, SymbolId head,
                                         CettaExprLen len);
/* Whether no row holding a variable is under HEAD or without a symbol head,
 * read from the exact index as space_exact_frontier_certified reads it,
 * without settling the facts kept in relations, which are ground.  False when
 * the index cannot answer, or the space has imports. */
bool space_exact_head_certified(Space *s, SymbolId head);
/* Remove every row equal to one of the ground atoms IDS, all of head HEAD,
 * in one pass through the exact index.  False, having removed nothing, when
 * the index cannot answer for them: no current index, a row under HEAD or
 * without a head that holds a variable, or rows the space cannot remove by
 * position. */
bool space_remove_exact_ids(Space *s, SymbolId head, const AtomId *ids,
                            size_t count);
/* Immediate exact removal with the same completed selection as retained
 * preparation.  The caller's uninterrupted transition licenses publication
 * without a generic effect record or a second validation.  OUT_ROWS excludes
 * relation-only facts.  False means the exact route declined or refused before
 * publication; no partial dense-first fallback is permitted. */
bool space_remove_exact_ids_immediate(Space *s, SymbolId head,
                                      const AtomId *ids, size_t count,
                                      Atom *dense_atom,
                                      CettaCount *out_removed,
                                      CettaCount *out_rows);
/* COPIES rows holding the stored term ID leave S, the first by position;
 * where S holds fewer rows of it, the fact a relation keeps for DENSE_ATOM
 * is its one further copy.  By identity: a row holding a variable, which ID
 * could unify with, is another atom and stays.  *OUT_REMOVED is COPIES, or
 * zero with nothing removed where S holds fewer; *OUT_ROWS excludes a
 * relation's fact.  False where S keeps no current exact index of its rows
 * and the caller must read them. */
bool space_take_exact_id(Space *s, SymbolId head, AtomId id, Atom *dense_atom,
                         CettaCount copies, CettaCount *out_removed,
                         CettaCount *out_rows);
/* The rows holding a ground pattern, ascending, read from a current exact
 * index without settling marks: physical rows while rows are marked. */
CettaIndex space_exact_index_rows64(Space *s, Atom *pattern, CettaIndex **out);
/* space_exact_index_rows64 for the ground expression whose LEN elements are
 * the ids IDS, headed by the symbol HEAD. */
CettaIndex space_exact_index_rows_ids64(Space *s, SymbolId head,
                                        const AtomId *ids, CettaExprLen len,
                                        CettaIndex **out);
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
/* A revision-qualified caller may exclude an impossible equation or
 * supply its program-owned head, body and structural matching plan. A partial
 * syntax override retains only the independent complete frame schema.
 * Selection does not consume body guards or establish a match. All borrowed
 * syntax and metadata must outlive the synchronous visit; activations retain
 * their frame schema independently. */
typedef struct {
    Atom *match_pattern;
    Atom *result_template;
    BindingsFrameSchema *frame_schema;
    const CettaOpenPatternPlan *match_plan;
} QueryEquationPlan;

typedef bool (*QueryEquationCandidatePlan)(
    Atom *equation, void *ctx, QueryEquationPlan *plan);

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
/* Whether a rule of the admitted syntax (rule_binding.h) may answer a call
 * of `head`, in `s` or a module it imports.  Without the named-head
 * qualifier this is the two predicates above for the admitted forms; with
 * it, an equation counts only when its left side applies `head`, and a
 * directional rule only when the index files it under `head`. */
bool space_rules_may_match_known_head(Space *s, SymbolId head,
                                      unsigned admitted_forms);
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

struct RegistryStates;

typedef struct {
    RegistryEntry *entries;
    uint32_t len, cap;
    /* Bindings not beginning with `&`. HE reads both kinds as tokens;
       other dialects retain their own named-space evaluation policy. */
    uint32_t word_len;
    RegistryEntry inline_entries[16];
    NameKeyTable *name_keys;
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
/* Failure releases its incomplete name-key owner. */
bool registry_try_init(Registry *r);
void registry_free(Registry *r);
void registry_bind_id(Registry *r, SymbolId key, Atom *value);
Atom *registry_lookup_id(Registry *r, SymbolId key);
/* registry_lookup_id trying the entry at `*hint` first, and leaving there
 * the index it found.  A key has one entry and an entry keeps its index, so
 * an entry still holding the key is its binding. */
Atom *registry_lookup_id_hinted(Registry *r, SymbolId key, uint32_t *hint);
void registry_bind(Registry *r, const char *name, Atom *value);
Atom *registry_lookup(Registry *r, const char *name);
bool registry_bind_name(Registry *r, Atom *name_key, Atom *value);
Atom *registry_lookup_name(Registry *r, Atom *name_key);
const Atom *registry_entry_name_key(const Registry *r, uint32_t index);

/* PeTTa's named state: the reference's global variables, whose names are
   independent of the names bound above.  Each state owns its value's
   storage, and setting a state copies the value in and releases the value
   it replaces, so a reader copies a stored value out before the state can
   next change, and never keeps it. */
Atom *registry_state_lookup(const Registry *r, SymbolId key);
bool registry_state_set(Registry *r, SymbolId key, Atom *value);
uint32_t registry_state_count(const Registry *r);
bool registry_state_entry(const Registry *r, uint32_t index,
                          SymbolId *key_out, Atom **value_out);

/* Canonical structural reference: (resolve-name (quote <closed-key>)). */
bool registry_ref_name_key(Atom *ref, Atom **name_key_out);
Atom *registry_lookup_ref(Registry *r, Atom *ref);

/* Resolve a space reference (symbol like &self, &ws, or grounded space atom)
   to a Space pointer. Returns NULL if not a space. */
Space *resolve_space(Registry *r, Atom *ref);

/* Remove an atom from a space (by structural equality). Returns true if found. */
bool space_remove(Space *s, Atom *atom);
bool space_contains_atom_id(const Space *s, AtomId atom_id);
/* Whether a row of the space is the stored atom `atom_id`: by the exact
 * index while it is current, one probe rather than a scan of the rows. */
bool space_holds_atom_id_exact(Space *s, AtomId atom_id);
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

/* One prepared primitive store effect.  This is the publication role shared
 * by add/remove adapters and transform's ordered takes/routing, conceptually
 * TcPreparedStep plus tc_commit_prepared (not tcalc's ABI or outcome model).
 * Preparation never publishes.  Borrowed atom/arena inputs must outlive the
 * preparation; a mask is owned only after its successful preparation.  For a
 * positional or mask selection, the read token names the exact Space lifetime
 * and captured prefix.  Appends do not stale that prefix; a prefix rewrite
 * requires the caller to select again, and is reported separately from no
 * match and from host failure. */
typedef enum {
    SPACE_STORE_EFFECT_ADD = 0,
    SPACE_STORE_EFFECT_ADD_BATCH,
    SPACE_STORE_EFFECT_REMOVE_ONE,
    SPACE_STORE_EFFECT_REMOVE_MASK,
    SPACE_STORE_EFFECT_REMOVE_POSITIONS,
} SpaceStoreEffectKind;

typedef enum {
    SPACE_STORE_BATCH_ALPHA_KEYS = 0,
    SPACE_STORE_BATCH_DENSE_TUPLES,
    SPACE_STORE_BATCH_DEEP_HOLES,
} SpaceStoreBatchKind;

typedef enum {
    SPACE_STORE_PUBLISH_ERROR = 0,
    SPACE_STORE_PUBLISH_STALE,
    SPACE_STORE_PUBLISH_NO_MATCH,
    SPACE_STORE_PUBLISH_PUBLISHED,
} SpaceStorePublishResult;

typedef struct {
    Space *space;
    SpaceReadToken read;
    SpaceStoreEffectKind kind;
    Arena *storage_arena;
    const Arena *source_arena;
    Atom *atom;
    AtomId atom_id;
    SpaceStoreBatchKind batch_kind;
    const AtomId *batch_items;
    const CettaSkeleton *batch_skeleton;
    SymbolId batch_head;
    AtomId batch_head_id;
    uint32_t batch_arity;
    CettaCount batch_len;
    uint8_t *remove_mask;
    CettaCount mask_len;
    /* Physical rows validated before publication.  Dense relation selections
     * complement row copies without settling the relation on the exact path. */
    CettaIndex *remove_positions;
    CettaIndex *remove_rows;
    CettaCount remove_row_len;
    Atom *dense_atom;
    CettaCount dense_selected;
    AtomId *remove_ids;
    uint8_t *remove_id_dense;
    CettaCount remove_id_len;
    /* A single exact selection stays in the effect itself.  Null arrays select
     * these values, so moving the effect structure never leaves self-pointers. */
    AtomId remove_id_single;
    uint8_t remove_id_dense_single;
    CettaCount selected_len;
    /* Stable under structure-assignment moves; binds an optional receipt to
     * this preparation rather than to a same-kind operation on the space. */
    uint64_t preparation_id;
    bool consumed;
} SpaceStoreEffect;

/* An optional retained record, not a proof.  Its atoms and array are owned
 * independently of the source arena, stored rows, and occurrence cursors.
 * The selected values and their positions are copied before publication;
 * space instance and committed revision identify the publication scope.
 * A no-match/stale/error result has no committed items.  An idempotent add
 * that admits an already-held set row likewise records no new make. */
typedef struct {
    Arena owned;
    Atom **items;
    CettaIndex *positions;
    CettaCount len;
    CettaCount cap;
    uint64_t space_instance;
    uint64_t preparation_id;
    uint64_t committed_revision;
    SpaceStoreEffectKind kind;
    bool initialized;
} SpaceStoreReceipt;

void space_store_effect_init(SpaceStoreEffect *effect);
void space_store_effect_dispose(SpaceStoreEffect *effect);
void space_store_receipt_init(SpaceStoreReceipt *receipt);
void space_store_receipt_dispose(SpaceStoreReceipt *receipt);

bool space_store_effect_prepare_add(
    SpaceStoreEffect *effect, Space *space, Arena *storage_arena,
    const Arena *source_arena, Atom *atom, SpaceStoreReceipt *receipt);
/* Fused public add-atom instances.  These borrowed batch views retain their
 * native representation and per-instance publication behavior; no receipt is
 * allocated for a batch without record-level atoms. */
bool space_store_effect_prepare_add_alpha_keys(
    SpaceStoreEffect *effect, Space *space, const AtomId *keys,
    CettaCount count);
bool space_store_effect_prepare_add_dense_tuples(
    SpaceStoreEffect *effect, Space *space, SymbolId head, AtomId head_id,
    uint32_t arity, const AtomId *tuples, CettaCount count);
bool space_store_effect_prepare_add_deep_holes(
    SpaceStoreEffect *effect, Space *space, SymbolId head,
    const CettaSkeleton *skeleton, const AtomId *holes, CettaCount count);
/* atom_id is the exact-id specialization when known; NONE selects by the
 * same structural equality as space_remove. */
bool space_store_effect_prepare_remove_one(
    SpaceStoreEffect *effect, Space *space, Atom *atom, AtomId atom_id,
    SpaceStoreReceipt *receipt);
/* Takes ownership of remove_mask only on success.  selected_len is the
 * caller's completed mask selection, so an ordinary call need not rescan it.
 * A ground dense_atom still held in a relation is folded into that mask without
 * a second logical write.  Receipt recording verifies the claimed count. */
bool space_store_effect_prepare_remove_mask(
    SpaceStoreEffect *effect, Space *space, uint8_t *remove_mask,
    CettaCount mask_len, CettaCount selected_len, Atom *dense_atom,
    SpaceStoreReceipt *receipt);
/* Validate ascending logical positions and retain their physical rows.  Takes
 * ownership of positions only on success.  A ground dense_atom is selected only
 * when its relation still holds it; appended rows are not implicitly selected. */
bool space_store_effect_prepare_remove_positions(
    SpaceStoreEffect *effect, Space *space, const CettaIndex *positions,
    CettaCount position_len, Atom *dense_atom, SpaceStoreReceipt *receipt);
/* Exact-index specialization over ground ids.  The id array is copied.  When
 * dense_atom is non-null it supplies the single id's dense selection; otherwise
 * each id's relation occurrence is selected independently. */
bool space_store_effect_prepare_remove_exact(
    SpaceStoreEffect *effect, Space *space, SymbolId head, const AtomId *ids,
    size_t count, Atom *dense_atom, SpaceStoreReceipt *receipt);
SpaceStorePublishResult space_store_effect_publish(
    SpaceStoreEffect *effect, SpaceStoreReceipt *receipt,
    CettaCount *out_removed);

/* ── Match Indexing ─────────────────────────────────────────────────────── */

/* Return backend-private candidate coordinates for a match pattern via its
   discrimination index. Dereference them with space_match_candidate_at64;
   they need not share the public logical get-atoms order.
   For small spaces (< MATCH_TRIE_THRESHOLD), returns all indices.
   Caller must free(*out). Returns count. */
CettaIndex space_match_candidates64(Space *s, Atom *pattern, CettaIndex **out);
/* Candidates for a flat pattern given by ids of UNIVERSE, the space's own:
 * COLUMNS[0] the head, a symbol, and each later column a value's id, or
 * CETTA_ATOM_ID_NONE where the pattern holds a variable.  True when the
 * native selector answers the pattern from its exact index or a prefix
 * index, which it then reads by the ids: *OUT and *COUNT are what
 * space_match_candidates64 answers for the pattern.  False when it would
 * answer otherwise, having changed nothing that asking
 * space_match_candidates64 would not. */
bool space_match_candidates_ids64(Space *s, const TermUniverse *universe,
                                  const AtomId *columns, CettaExprLen len,
                                  CettaIndex **out, CettaIndex *count);
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
 * Return only explicit `(: subject type)` declarations, in logical space
 * order.  The returned pointer array is caller-owned; its atoms live in the
 * supplied arena and are freshened per occurrence.
 */
uint32_t space_get_declared_types(
    Space *s, Arena *a, Atom *subject, Atom ***out_types);

/* Conservative exclusion for relational (: subject type) queries. False
 * certifies that no current occurrence can have the requested subject's
 * outer constructor; true retains ordinary relational search. */
bool space_type_annotation_may_match_subject(Space *s, const Atom *subject);
bool space_type_annotations_have_only_symbol_subjects(Space *s);
/* Whether no annotation can give a symbol, a variable or a grounded value a
 * metatype (Atom, Symbol, Variable, Expression, Grounded): no variable or
 * grounded subject, and no symbol subject declared at a metatype's name. */
bool space_scalar_types_name_no_metatype(Space *s);
/* Whether the type stamp store holds typings of this incarnation of `atom`:
 * an unbudgeted HE inference of a term-stable ground expression, in an arena
 * the evaluator vouches for, whose answers were ground. */
bool space_type_stamps_hold(Space *s, const Atom *atom);
/* Without copying any type out: the number of `atom`'s typings (as
 * get_atom_types answers them) where the store holds or reads them by shape,
 * else -1; and whether one of them is exactly `type`. */
int space_type_stamps_count(Space *s, Arena *a, Atom *atom);
/* The same count for get_atom_types_structural's answers: an expression's
 * own top-level declarations do not count. */
int space_type_stamps_structural_count(Space *s, Arena *a, Atom *atom);
/* A prepared pure run's typing scope, read once where it enters: enter
 * returns the enclosing run's space, which leave restores. */
const Space *space_type_stamps_run_enter(Space *s);
void space_type_stamps_run_leave(const Space *outer);
bool space_type_stamps_has_type(Space *s, Arena *a, Atom *atom, Atom *type);
/* Whether every type row whose subject `head` heads declares `head` itself
 * (no expression subject headed by it, no open or headless row). */
bool space_type_rows_only_declare(Space *s, SymbolId head);
/* Whether no type row of `s` types an expression headed by `head` (none
 * whose subject is such an expression, no open or headless row): its
 * values' typings are their applications'. */
bool space_type_rows_type_no_expression_of(Space *s, SymbolId head);
/* Whether no expression node of `atom` has a head a type row of `s` types
 * an expression by: HE reads such a row, wherever the node sits, before the
 * node's own application.  Kept per node in the type stamp store. */
bool space_type_rows_free_below(Space *s, Atom *atom);
/* Keep `type` as the one typing of this incarnation of `atom`, where the
 * store admits it (a value whose typing its parts decide). */
void space_type_stamps_record_type(Space *s, Arena *a, Atom *atom,
                                   Atom *type);
/* 1 where one of `atom`'s held typings unifies with `type`, 0 where none
 * does, -1 where the store holds none of them. */
int space_type_stamps_unifies(Space *s, Arena *a, Atom *atom, Atom *type);
/* `range` under the unifier of `atom`'s one typing that unifies with
 * `formal`: 1 with *instance set, 0 where none or several fit, -1 where the
 * store holds none of its typings.  Kept per typing state. */
int space_type_stamps_instance(Space *s, Arena *a, Atom *atom, Atom *formal,
                               Atom *range, Atom **instance);
/* The typings the store holds for this incarnation of `atom`, borrowed
 * until the store next records or clears, and their number; -1 where it
 * holds none. */
int space_type_stamps_types(Space *s, Arena *a, Atom *atom,
                            Atom *const **types);
/* Carry `from`'s kept typings onto `to`, a same-shaped copy, where they were
 * kept under one of `sources`' current incarnations. */
void space_type_stamps_transfer(Space *s, const Atom *from, Atom *to,
                                const Arena *const *sources,
                                size_t source_count);

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
