#ifndef CETTA_TERM_UNIVERSE_H
#define CETTA_TERM_UNIVERSE_H

#include "atom.h"
#include <stddef.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#ifndef CETTA_BUILD_WITH_TERM_UNIVERSE_DIAGNOSTICS
#define CETTA_BUILD_WITH_TERM_UNIVERSE_DIAGNOSTICS 0
#endif

/* A build with collection checks traps every read of an entry the last
 * collection found unreachable, unless a lookup has found it again since. */
#ifndef CETTA_BUILD_WITH_TERM_UNIVERSE_COLLECTION_CHECKS
#define CETTA_BUILD_WITH_TERM_UNIVERSE_COLLECTION_CHECKS 0
#endif

/*
 * TermUniverse isolates persistent term ownership from evaluator-local
 * scratch allocation.
 *
 * Positive example:
 *   - `bind!`, `add-atom`, and persistent space cloning all store atoms
 *     through one policy seam.
 *
 * Negative example:
 *   - Open-coding `persistent ? persistent : a` and copy policy at every
 *     storage boundary in eval.c and backend code.
 */

typedef struct {
    uint64_t direct_constructor_leaf_hits;
    uint64_t direct_constructor_expr_hits;
    uint64_t legacy_top_down_stable_admissions;
    uint64_t direct_lookup_hits;
    uint64_t direct_lookup_misses;
    uint64_t direct_lookup_probes;
    uint64_t direct_lookup_max_probe;
    uint64_t lazy_decode_count;
    uint64_t legacy_hash_recompute_count;
    uint64_t store_format_migrations;
} CettaTermUniverseDiagnostics;

typedef enum {
    TERM_UNIVERSE_ERROR_NONE = 0,
    TERM_UNIVERSE_ERROR_ATOM_ID_EXHAUSTED = 1,
    TERM_UNIVERSE_ERROR_STORAGE_TOO_LARGE = 2,
    TERM_UNIVERSE_ERROR_ALLOCATION_FAILED = 3,
    TERM_UNIVERSE_ERROR_UNSUPPORTED_STORE_FORMAT = 4,
    TERM_UNIVERSE_ERROR_STORE_FORMAT_MIGRATION_FAILED = 5,
} TermUniverseError;

typedef enum {
    TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1 = 1,
    TERM_UNIVERSE_STORE_FORMAT_WIDE64_V1 = 2,
} TermUniverseStoreFormat;

typedef struct TermUniverse TermUniverse;

typedef uint64_t CettaAtomId;
typedef CettaAtomId AtomId;
typedef uint64_t CettaCount;
typedef uint64_t CettaIndex;

typedef enum {
    CETTA_TERM_VARIABLE_SUPPORT_DENSE64 = 1,
    CETTA_TERM_VARIABLE_SUPPORT_SPARSE64 = 2,
} CettaTermVariableSupportRepresentation;

/* Immutable variable support derived only from an interned term's syntax.
 * Dense terms use a base-relative bitmap; unusually wide supports use the
 * sorted flexible array.  The object is owned by its TermUniverse and remains
 * valid until that universe's storage is reset or freed. */
typedef struct CettaTermVariableSupport {
    uint32_t count;
    CettaTermVariableSupportRepresentation representation;
    uint64_t first_base_id;
    uint64_t dense_mask;
    uint64_t sparse_base_ids[];
} CettaTermVariableSupport;

struct CettaTermVariableSupportBlock;

typedef struct {
    const CettaTermVariableSupport *support;
    uint64_t dense_remaining;
    uint32_t sparse_index;
} CettaTermVariableSupportIterator;

static inline void cetta_term_variable_support_iterator_init(
    CettaTermVariableSupportIterator *iterator,
    const CettaTermVariableSupport *support) {
    if (!iterator)
        return;
    iterator->support = support;
    iterator->dense_remaining =
        support && support->representation ==
                       CETTA_TERM_VARIABLE_SUPPORT_DENSE64
            ? support->dense_mask
            : 0u;
    iterator->sparse_index = 0u;
}

static inline bool cetta_term_variable_support_iterator_next(
    CettaTermVariableSupportIterator *iterator,
    uint64_t *base_id) {
    if (!iterator || !base_id || !iterator->support)
        return false;
    if (iterator->support->representation ==
        CETTA_TERM_VARIABLE_SUPPORT_DENSE64) {
        if (iterator->dense_remaining == 0u)
            return false;
        uint32_t offset =
            (uint32_t)__builtin_ctzll(iterator->dense_remaining);
        iterator->dense_remaining &= iterator->dense_remaining - 1u;
        *base_id = iterator->support->first_base_id + offset;
        return true;
    }
    if (iterator->sparse_index >= iterator->support->count)
        return false;
    *base_id = iterator->support
                   ->sparse_base_ids[iterator->sparse_index++];
    return true;
}

typedef enum {
    CETTA_VAR_SPELLING_SYMBOL = 0,
    CETTA_VAR_SPELLING_NAME_KEY = 1,
} CettaVarSpellingKind;

typedef bool (*TermUniverseStoreFormatObserver)(
    TermUniverse *universe,
    TermUniverseStoreFormat old_format,
    TermUniverseStoreFormat new_format,
    void *ctx);

typedef struct {
    TermUniverseStoreFormatObserver fn;
    void *ctx;
} TermUniverseStoreFormatObserverEntry;

/* Reclamation.  An entry is reachable when a root holds it or a reachable
 * record has it as a child.  Roots are the ids a registered provider lists
 * at a safe point: a space lists its rows, and every other structure that
 * keeps ids beyond one call lists its own.
 *
 * Positive example:
 *   - A space registers itself when it attaches to the universe and lists
 *     its rows; an atom no row and no other provider reaches is unreachable.
 *
 * Negative example:
 *   - A cache that keeps ids between calls without listing them, or without
 *     keying on collection_epoch, reads entries a collection may reclaim. */
typedef void (*TermUniverseRootVisit)(void *visit_context, AtomId id);
typedef void (*TermUniverseRootProvider)(const TermUniverse *universe,
                                         TermUniverseRootVisit visit,
                                         void *visit_context,
                                         void *provider_context);

typedef struct {
    TermUniverseRootProvider fn;
    void *ctx;
    /* A counted holder keeps a count on each id it holds (as a space does
     * for its rows), so a counting collection need not list it; only the
     * tracing check does. */
    bool counted;
} TermUniverseRootProviderEntry;

typedef enum {
    TERM_UNIVERSE_COLLECTION_OFF = 0,
    /* Find and report what a collection would reclaim; free nothing. */
    TERM_UNIVERSE_COLLECTION_SHADOW = 1,
    /* Free what it finds dead.  A freed entry stays as a tombstone: no id is
     * named twice, and an id held past the collection names nothing. */
    TERM_UNIVERSE_COLLECTION_RECLAIM = 2,
} TermUniverseCollectionMode;

/* What one collection found.  Entry counts include entries without a
 * record; byte counts are record bytes in the blob pool. */
typedef struct {
    uint64_t entries;
    uint64_t records;
    uint64_t record_bytes;
    uint64_t decoded;
    uint64_t roots;
    /* Listed ids naming no entry of this universe. */
    uint64_t invalid_roots;
    uint64_t live;
    uint64_t live_record_bytes;
    uint64_t dead;
    uint64_t dead_record_bytes;
    /* Entries this collection freed, and their record bytes. */
    uint64_t reclaimed;
    uint64_t reclaimed_record_bytes;
} TermUniverseCollectionReport;

/* Size classes of freed records: eight bytes a class, up to 256 bytes. */
#define TERM_UNIVERSE_RECORD_CLASSES 33u

struct TermUniverse {
    CettaFrameIdentityScope frame_identities;
    /* Process-local identity plus a storage generation make derived pointer
       accelerators unable to revive AtomIds after object reuse or reset. */
    uint64_t instance_id;
    uint64_t storage_epoch;
    Arena *persistent_arena;
    uint8_t *blob_pool;
    size_t blob_len, blob_cap;
    struct TermEntry *entries;
    size_t len, cap;
    /* A held snapshot may contain equal values with distinct pointer IDs.
     * Structural membership must not use canonical-ID equality in this mode. */
    bool snapshot_entries;
    /* The atom each entry decodes to, in pages made when an entry in them
     * first decodes, so an entry never read as an atom costs nothing here.
     * An entry without a record keeps its atom here alone. */
    Atom ***decoded_pages;
    size_t decoded_page_cap;
    _Atomic(struct CettaTermVariableSupportBlock *)
        *variable_support_blocks;
    size_t variable_support_block_cap;
    uint8_t *intern_slots;
    size_t intern_mask;
    size_t intern_used;
    /* Integer pages retain exact coordinates and physical record positions.
       Positions remain valid when the AtomId storage format changes. */
    struct CettaIntegerInternSlot *integer_slots;
    size_t integer_mask;
    size_t integer_used;
    uint8_t *ptr_slots;
    size_t ptr_mask;
    size_t ptr_used;
    TermUniverseStoreFormatObserverEntry *observers;
    size_t observer_len;
    size_t observer_cap;
    TermUniverseRootProviderEntry *root_providers;
    size_t root_provider_len;
    size_t root_provider_cap;
    TermUniverseCollectionMode collection_mode;
    /* Collect at every safe point offered, and often inside statements: the
     * policy of a build with collection checks, and of tests. */
    bool collection_eager;
    /* Open regions of C code holding ids no provider lists.  A safe point
     * collects only when there are none. */
    uint32_t collection_inhibit;
    /* Advanced by every collection.  A structure keeping ids between calls
     * either lists them as roots or keys on this and drops them. */
    uint64_t collection_epoch;
    /* Entries after the last collection, and the live ones among them: the
     * trigger collects once the entries made since reach the live count. */
    uint64_t collection_len_after;
    uint64_t collection_live_after;
    /* Rows released since the last collection (spaces report them): an
     * estimate of the garbage made since. */
    uint64_t collection_released;
    /* A collection is due once the entries made since the last reach
     * collection_due, or the rows released reach collection_due_released. */
    uint64_t collection_due;
    uint64_t collection_due_released;
    /* Counting: the least wait for the next collection after one that found
     * little garbage among its candidates, doubled each time; 0 otherwise. */
    uint64_t collection_backoff;
    /* With collection checks: the entries the last collection found dead,
     * one bit each. */
    uint64_t *collection_dead;
    size_t collection_dead_words;
    /* Variable supports of reclaimed terms.  A holder of a decoded atom may
     * keep its support, and the atom outlives its entry, so the support is
     * kept until the storage is reset. */
    CettaTermVariableSupport **retired_supports;
    size_t retired_support_len, retired_support_cap;
    /* Reference counts, while reclamation counts: one per entry, held by
     * each record that has the entry as a child and by counted holders (a
     * space's rows).  UINT16_MAX saturates, and such an entry is kept. */
    bool counting;
    uint16_t *counts;
    size_t counts_cap;
    /* Entries whose count has saturated. */
    uint64_t saturated;
    /* The entries whose count fell to zero, each once (its bit set): with
     * the entries made since the last collection, the only candidates a
     * counting collection examines. */
    size_t *zero;
    size_t zero_len, zero_cap;
    uint64_t *zero_bits;
    /* The entries an uncounted holder lists, while a collection runs. */
    uint64_t *root_bits;
    /* Entries holding a term: made and not reclaimed. */
    uint64_t live_entries;
    /* Freed entry slots, which later entries take before the entries grow.
     * A build with collection checks holds a freed slot back for two
     * counting collections first (quarantine, with the collection it was
     * freed at), so a stale read in between still traps, and counts each
     * slot's reuses (generations). */
    size_t *free_slots;
    size_t free_slot_len, free_slot_cap;
    size_t *quarantine;
    uint64_t *quarantine_at;
    size_t quarantine_len, quarantine_cap;
    uint32_t *generations;
    uint64_t counting_collections;
    /* Ids the last counting collection listed from uncounted holders. */
    uint64_t collection_roots_after;
    /* Freed record bytes by size class, eight bytes a class up to 256: each
     * list runs through the freed bytes, as offset + 1.  Larger freed
     * records are holes until the records are compacted. */
    uint64_t record_free[TERM_UNIVERSE_RECORD_CLASSES];
    uint64_t record_free_bytes;
    uint64_t record_hole_bytes;
    TermUniverseError last_error;
    TermUniverseStoreFormat store_format;
#if CETTA_BUILD_WITH_TERM_UNIVERSE_DIAGNOSTICS
    CettaTermUniverseDiagnostics diagnostics;
    uint64_t diag_atom_id_capacity_override;
    uint64_t diag_logical_atom_id_base_override;
#endif
};

/* A process-local capability for one stored root in one TermUniverse
 * generation.  It does not extend the lifetime of the TermUniverse object;
 * callers validate it against an independently live universe before use.
 * Space revisions may change while this token remains valid.  A collection
 * that frees entries ends it: the root may be gone and its slot reused. */
typedef struct {
    const TermUniverse *universe;
    uint64_t instance_id;
    uint64_t storage_epoch;
    uint64_t collection_epoch;
    AtomId root_id;
} TermUniverseRootToken;

#define CETTA_ATOM_ID_NONE UINT64_MAX
#define CETTA_ATOM_ID_MAX ((AtomId)(UINT64_MAX - 1u))
#define CETTA_TERM_ENTRY_BLOB_NONE UINT64_MAX

static inline size_t cetta_atom_id_storage_width_bytes_from_bits(
    uint32_t bits) {
    switch (bits) {
    case 32u:
        return sizeof(uint32_t);
    case 64u:
        return sizeof(uint64_t);
    }
    return 0;
}

static inline bool cetta_atom_id_fits_width_bits(uint32_t bits, AtomId id) {
    switch (bits) {
    case 32u:
        return id <= (AtomId)UINT32_MAX - 1u;
    case 64u:
        return id != CETTA_ATOM_ID_NONE;
    }
    return false;
}

static inline AtomId cetta_atom_id_storage_load_bits(const uint8_t *src,
                                                     uint32_t bits) {
    if (!src)
        return CETTA_ATOM_ID_NONE;
    switch (bits) {
    case 32u: {
        uint32_t value = UINT32_MAX;
        memcpy(&value, src, sizeof(value));
        return (AtomId)value;
    }
    case 64u: {
        uint64_t value = CETTA_ATOM_ID_NONE;
        memcpy(&value, src, sizeof(value));
        return (AtomId)value;
    }
    }
    return CETTA_ATOM_ID_NONE;
}

static inline bool cetta_atom_id_storage_store_bits(uint8_t *dst,
                                                    uint32_t bits,
                                                    AtomId value) {
    if (!dst || !cetta_atom_id_fits_width_bits(bits, value))
        return false;
    switch (bits) {
    case 32u: {
        uint32_t narrowed = (uint32_t)value;
        memcpy(dst, &narrowed, sizeof(narrowed));
        return true;
    }
    case 64u:
        memcpy(dst, &value, sizeof(value));
        return true;
    }
    return false;
}

typedef struct {
    uint8_t tag;
    uint8_t subtag;
    uint16_t arity_or_len;
    uint32_t sym_or_head;
    uint32_t aux32;
    uint32_t hash32;
} CettaTermHdr;

typedef struct TermEntry {
    uint64_t byte_off;
    uint32_t byte_len;
    /* The record's coordinate in the intern table, kept so the table grows
     * and the record is placed without reading the record again. */
    uint32_t coordinate;
} TermEntry;

_Static_assert(sizeof(AtomId) == 8, "AtomId must remain 64-bit");
_Static_assert(CETTA_ATOM_ID_NONE == UINT64_MAX, "AtomId sentinel must stay all-ones");
_Static_assert(sizeof(VarId) == 16, "VarId is two words: base and epoch");
_Static_assert(sizeof(CettaTermHdr) == 16, "CettaTermHdr ABI drifted");
_Static_assert(offsetof(CettaTermHdr, tag) == 0, "CettaTermHdr.tag offset drifted");
_Static_assert(offsetof(CettaTermHdr, subtag) == 1, "CettaTermHdr.subtag offset drifted");
_Static_assert(offsetof(CettaTermHdr, arity_or_len) == 2,
               "CettaTermHdr.arity_or_len offset drifted");
_Static_assert(offsetof(CettaTermHdr, sym_or_head) == 4,
               "CettaTermHdr.sym_or_head offset drifted");
_Static_assert(offsetof(CettaTermHdr, aux32) == 8, "CettaTermHdr.aux32 offset drifted");
_Static_assert(offsetof(CettaTermHdr, hash32) == 12, "CettaTermHdr.hash32 offset drifted");

void term_universe_init(TermUniverse *universe);
bool term_universe_init_with_store_format(TermUniverse *universe,
                                          TermUniverseStoreFormat format);
void term_universe_free(TermUniverse *universe);
void term_universe_storage_census(const TermUniverse *universe, CettaStorageCensus *census);
void term_universe_set_persistent_arena(TermUniverse *universe,
                                        Arena *persistent_arena);
void term_universe_diag_reset(TermUniverse *universe);
void term_universe_diag_snapshot(const TermUniverse *universe,
                                 CettaTermUniverseDiagnostics *out);
#if CETTA_BUILD_WITH_TERM_UNIVERSE_DIAGNOSTICS
void term_universe_diag_set_atom_id_capacity_override(TermUniverse *universe,
                                                      uint64_t capacity);
void term_universe_diag_set_logical_atom_id_base_override(
    TermUniverse *universe, uint64_t base);
#endif
void term_universe_clear_error(TermUniverse *universe);
TermUniverseError term_universe_last_error_code(const TermUniverse *universe);
const char *term_universe_error_name(TermUniverseError error);
TermUniverseStoreFormat term_universe_store_format(const TermUniverse *universe);
const char *term_universe_store_format_name(TermUniverseStoreFormat format);
uint32_t term_universe_store_format_version(TermUniverseStoreFormat format);
uint32_t term_universe_store_format_atom_id_width_bits(TermUniverseStoreFormat format);
bool term_universe_add_store_format_observer(
    TermUniverse *universe,
    TermUniverseStoreFormatObserver fn,
    void *ctx);
void term_universe_remove_store_format_observer(
    TermUniverse *universe,
    TermUniverseStoreFormatObserver fn,
    void *ctx);
bool term_universe_add_root_provider(TermUniverse *universe,
                                     TermUniverseRootProvider fn, void *ctx);
/* Register a holder that counts what it holds through term_universe_hold
 * and term_universe_release; its listing serves the tracing check. */
bool term_universe_add_counted_root_provider(TermUniverse *universe,
                                             TermUniverseRootProvider fn,
                                             void *ctx);
void term_universe_remove_root_provider(TermUniverse *universe,
                                        TermUniverseRootProvider fn,
                                        void *ctx);
void term_universe_set_collection_mode(TermUniverse *universe,
                                       TermUniverseCollectionMode mode);
/* Bracket a region of C code that holds ids no provider lists. */
void term_universe_collection_inhibit_enter(TermUniverse *universe);
void term_universe_collection_inhibit_leave(TermUniverse *universe);
/* Collect now, unless a region inhibits it.  True when a collection ran;
 * `out` (optional) receives what it found.  The caller is the only thread
 * using the universe: a safe point lies outside concurrent evaluation. */
bool term_universe_collect(TermUniverse *universe,
                           TermUniverseCollectionReport *out);
/* A safe point: collect when the trigger asks for it.  The trigger keeps a
 * collection's work proportional to the entries made since the last one. */
bool term_universe_collect_at_safe_point(TermUniverse *universe,
                                         TermUniverseCollectionReport *out);
/* A holder released COUNT references to entries: rows a space removed. */
static inline void term_universe_collection_note_released(
    TermUniverse *universe, uint64_t count) {
    if (universe)
        universe->collection_released += count;
}

/* A counted holder takes or drops one reference to ID. */
void term_universe_hold_counted(TermUniverse *universe, AtomId id);
void term_universe_release_counted(TermUniverse *universe, AtomId id);
#if CETTA_BUILD_WITH_TERM_UNIVERSE_COLLECTION_CHECKS || \
    CETTA_BUILD_WITH_TERM_UNIVERSE_DIAGNOSTICS
static inline void term_universe_hold(TermUniverse *universe, AtomId id) {
    if (universe && universe->counting)
        term_universe_hold_counted(universe, id);
}
static inline void term_universe_release(TermUniverse *universe, AtomId id) {
    if (universe && universe->counting)
        term_universe_release_counted(universe, id);
}
#else
/* An id is its entry's index here.  A count that saturates, or a release
 * that reaches zero, takes the general path. */
static inline void term_universe_hold(TermUniverse *universe, AtomId id) {
    if (!universe || !universe->counting || id == CETTA_ATOM_ID_NONE ||
        id >= universe->len)
        return;
    uint16_t count = universe->counts[id];
    if (count >= UINT16_MAX - 1u) {
        term_universe_hold_counted(universe, id);
        return;
    }
    universe->counts[id] = (uint16_t)(count + 1u);
}
static inline void term_universe_release(TermUniverse *universe, AtomId id) {
    if (!universe || !universe->counting || id == CETTA_ATOM_ID_NONE ||
        id >= universe->len)
        return;
    uint16_t count = universe->counts[id];
    if (count <= 1u || count == UINT16_MAX) {
        term_universe_release_counted(universe, id);
        return;
    }
    universe->counts[id] = (uint16_t)(count - 1u);
}
#endif

/* Whether a safe point inside a statement should collect.  Counting, when
 * the candidates, the entries made since the last collection and those
 * whose count fell to zero, reach their due; tracing, when enough entries
 * were made, or enough rows released, since the last collection.  Cheap
 * enough to ask at every step. */
static inline bool term_universe_collection_due(const TermUniverse *universe) {
    if (universe->collection_mode == TERM_UNIVERSE_COLLECTION_OFF)
        return false;
    if (universe->counting)
        return (uint64_t)universe->len - universe->collection_len_after +
                   universe->zero_len >=
               universe->collection_due;
    return (uint64_t)universe->len - universe->collection_len_after >=
               universe->collection_due ||
           universe->collection_released >= universe->collection_due_released;
}
uint64_t term_universe_atom_id_capacity(const TermUniverse *universe);
bool term_universe_migrate_store_format(TermUniverse *universe,
                                        TermUniverseStoreFormat format);
Atom *term_universe_canonicalize_atom(Arena *dst, Atom *src);
/* True exactly when structural identity is stable enough for find-or-intern
 * lookup. Mutable spaces/state, foreign values, captures, and Prime runtime
 * capabilities deliberately fall outside this fragment. */
bool term_universe_atom_is_stable(Atom *atom);
/* Key-only alpha canonicalization: every variable is renamed by
 * first-occurrence ordinal and its presentation is erased.  Unlike
 * term_universe_canonicalize_atom, this is not a presentation-preserving
 * epoch cleanup and must not be used to materialize user-visible terms. */
Atom *term_universe_alpha_canonicalize_atom(Arena *dst, Atom *src);
AtomId term_universe_store_atom_id(TermUniverse *universe, Arena *fallback,
                                   Atom *src);
/* Store a source term owned by one resettable arena.  Reused expression
 * children may retain their AtomIds for the exact universe generation and
 * arena reset epoch; the structural store remains authoritative. */
AtomId term_universe_store_atom_id_from_source_arena(
    TermUniverse *universe, Arena *fallback, const Arena *source_arena,
    Atom *src);
/* Insert the exact already-forwarded Atom into the existing fallback
 * entry representation. The universe's persistent arena must be the actual
 * snapshot destination, held without reset until all universe users retire.
 * Distinct equal images keep distinct IDs; repeated pointers reuse one ID.
 * This admits no resource and does not acquire an arena lifetime. It is for
 * independently owned read-only snapshots, not ordinary canonical ingress.
 * Refusal may retain unpublished metadata but never a partial new entry. */
AtomId term_universe_store_snapshot_image(TermUniverse *universe,
    const AtomDeepCopySession *snapshot, const Atom *source);
AtomId term_universe_lookup_atom_id(const TermUniverse *universe, Atom *src);
/* Non-inserting lookup of an expression presented as borrowed child
 * coordinates.  This is structurally identical to lookup of an expression
 * Atom with those children, but it never manufactures that wrapper. */
AtomId term_universe_lookup_expression_coordinates(
    const TermUniverse *universe, Atom *const *coordinates,
    CettaExprLen coordinate_count);
/* Non-inserting lookup of `(head argument...)` without constructing an Atom
 * wrapper for either the symbol head or the application. */
AtomId term_universe_lookup_symbol_application(
    const TermUniverse *universe, SymbolId head,
    Atom *const *arguments, CettaExprLen argument_count);
bool term_universe_root_token_capture(
    const TermUniverse *universe, AtomId root_id,
    TermUniverseRootToken *out);
bool term_universe_root_token_matches_live_universe(
    TermUniverseRootToken token, const TermUniverse *live_universe);
Atom *term_universe_root_token_resolve(
    TermUniverseRootToken token, const TermUniverse *live_universe);
/* Return the write-once support summary for `id`.  A ground term succeeds
 * with `*out == NULL`.  Concurrent first demand is safe while TermUniverse
 * storage is otherwise stable; mutation of the universe remains externally
 * synchronized like every other TermUniverse operation. */
bool term_universe_variable_support(
    TermUniverse *universe, AtomId id,
    const CettaTermVariableSupport **out);
bool term_universe_atom_id_eq(const TermUniverse *universe, AtomId id,
                              Atom *src);
Atom *term_universe_get_atom(const TermUniverse *universe, AtomId id);
/* The atom the entry ID names has decoded to, without decoding it: NULL
 * until the entry is first read as an atom, except for an entry without a
 * record, whose atom it always is. */
Atom *term_universe_decoded_atom_at(const TermUniverse *universe, AtomId id);
Atom *term_universe_copy_atom(const TermUniverse *universe, Arena *dst,
                              AtomId id);
/* Read ordinary symbol leaves while copying a form. The callback's meaning
 * stays fixed for the copy. NULL means unchanged;
 * a returned value stays live for the copy and is copied without reading its
 * contents again. Variable name keys and opaque values are not source text. */
typedef Atom *(*TermUniverseSymbolRead)(void *context, SymbolId symbol);
Atom *term_universe_copy_atom_read_symbols(
    const TermUniverse *universe, Arena *dst, AtomId id,
    TermUniverseSymbolRead read, void *context);
Atom *term_universe_copy_atom_epoch(const TermUniverse *universe, Arena *dst,
                                    AtomId id, CettaFrameIdentity epoch);
char *term_universe_atom_to_string(Arena *a, const TermUniverse *universe,
                                   AtomId id);
char *term_universe_atom_to_parseable_string(Arena *a,
                                             const TermUniverse *universe,
                                             AtomId id);
Atom *term_universe_store_atom(TermUniverse *universe, Arena *fallback,
                               Atom *src);
/* A record header's aux word: its data (an expression's arity, a string's
 * length) and whether the term has variables. */
#define CETTA_TERM_HDR_HAS_VARS 0x80000000u
#define CETTA_TERM_HDR_DATA_MASK 0x7fffffffu

/* The accessors below read an id's record header inline when the record is
 * in the blob pool, as nearly every stored term's is; these answer the rest,
 * a term kept only as its decoded atom, and every id in a diagnostics
 * build, whose ids may be offset. */
const CettaTermHdr *term_universe_hdr_checked(const TermUniverse *universe,
                                              AtomId id);
AtomKind term_universe_kind_unrecorded(const TermUniverse *universe,
                                       AtomId id);
CettaExprLen term_universe_arity_unrecorded(const TermUniverse *universe,
                                            AtomId id);
SymbolId term_universe_head_sym_unrecorded(const TermUniverse *universe,
                                           AtomId id);

/* The record header of `id`, or NULL when it has none. */
#if defined(__GNUC__)
__attribute__((always_inline))
#endif
static inline const CettaTermHdr *tu_hdr(const TermUniverse *universe,
                                         AtomId id) {
#if CETTA_BUILD_WITH_TERM_UNIVERSE_DIAGNOSTICS || \
    CETTA_BUILD_WITH_TERM_UNIVERSE_COLLECTION_CHECKS
    return term_universe_hdr_checked(universe, id);
#else
    /* The sentinel id is past every universe's length. */
    if (!universe || id >= universe->len)
        return NULL;
    const TermEntry *entry = &universe->entries[id];
    if (entry->byte_off == CETTA_TERM_ENTRY_BLOB_NONE ||
        entry->byte_len < sizeof(CettaTermHdr) || !universe->blob_pool ||
        entry->byte_off > universe->blob_len ||
        entry->byte_len > universe->blob_len - entry->byte_off)
        return NULL;
    return (const CettaTermHdr *)(universe->blob_pool + entry->byte_off);
#endif
}

static inline AtomKind tu_kind(const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    return hdr ? (AtomKind)hdr->tag
               : term_universe_kind_unrecorded(universe, id);
}

static inline CettaExprLen tu_arity(const TermUniverse *universe,
                                    AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (!hdr)
        return term_universe_arity_unrecorded(universe, id);
    return hdr->tag == ATOM_EXPR
        ? (CettaExprLen)(hdr->aux32 & CETTA_TERM_HDR_DATA_MASK) : 0u;
}

static inline SymbolId tu_head_sym(const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (!hdr)
        return term_universe_head_sym_unrecorded(universe, id);
    return hdr->tag == ATOM_SYMBOL || hdr->tag == ATOM_EXPR
        ? hdr->sym_or_head : SYMBOL_ID_NONE;
}

uint32_t tu_hash32(const TermUniverse *universe, AtomId id);
SymbolId tu_sym(const TermUniverse *universe, AtomId id);
VarId tu_var_id(const TermUniverse *universe, AtomId id);
AtomId tu_var_name_key_id(const TermUniverse *universe, AtomId id);
GroundedKind tu_ground_kind(const TermUniverse *universe, AtomId id);
int64_t tu_int(const TermUniverse *universe, AtomId id);
double tu_float(const TermUniverse *universe, AtomId id);
bool tu_bool(const TermUniverse *universe, AtomId id);
/* The value of an internal tag, or 0 for any other atom. */
int64_t tu_internal_tag(const TermUniverse *universe, AtomId id);
/* The arity of a stored expression as the authored term has it: an
 * elaborated Prime template's own list left out (atom_authored_len). */
CettaExprLen tu_authored_arity(const TermUniverse *universe, AtomId id);
PeTTaValueRepresentation tu_petta_value_representation(
    const TermUniverse *universe, AtomId id);
const char *tu_string_cstr(const TermUniverse *universe, AtomId id);
/* The byte length of a string; its bytes may hold NUL. */
size_t tu_string_len(const TermUniverse *universe, AtomId id);
const char *tu_bigint_cstr(const TermUniverse *universe, AtomId id);
const char *tu_rational_cstr(const TermUniverse *universe, AtomId id);
/* The id of child `idx` of the expression `id`: its payload follows its
 * header, one id per child at the store format's width. A borrowed header
 * must come from this universe and remain valid: no universe mutation may
 * occur between obtaining it and reading its child. */
#if defined(__GNUC__)
__attribute__((always_inline))
#endif
static inline AtomId tu_child_from_header(const TermUniverse *universe,
                                          const CettaTermHdr *hdr,
                                          CettaExprIndex idx) {
    if (!universe || !hdr || hdr->tag != ATOM_EXPR ||
        idx >= (CettaExprIndex)(hdr->aux32 & CETTA_TERM_HDR_DATA_MASK))
        return CETTA_ATOM_ID_NONE;
    const uint8_t *payload = (const uint8_t *)(hdr + 1);
    switch (universe->store_format) {
    case TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1:
        return cetta_atom_id_storage_load_bits(
            payload + (size_t)idx * sizeof(uint32_t), 32u);
    case TERM_UNIVERSE_STORE_FORMAT_WIDE64_V1:
        return cetta_atom_id_storage_load_bits(
            payload + (size_t)idx * sizeof(uint64_t), 64u);
    }
    return CETTA_ATOM_ID_NONE;
}
#if defined(__GNUC__)
__attribute__((always_inline))
#endif
static inline AtomId tu_child(const TermUniverse *universe, AtomId id,
                              CettaExprIndex idx) {
    return tu_child_from_header(universe, tu_hdr(universe, id), idx);
}

bool tu_has_vars(const TermUniverse *universe, AtomId id);

AtomId tu_intern_symbol(TermUniverse *universe, SymbolId sym_id);
AtomId tu_intern_var(TermUniverse *universe, SymbolId sym_id, VarId var_id);
AtomId tu_intern_named_var(TermUniverse *universe, AtomId name_key_id,
                           VarId var_id);
AtomId tu_intern_int(TermUniverse *universe, int64_t value);
/* Canonical scalar admission in input order, with bounded internal windows.
 * Duplicate values retain the same identity as singular admission. A false
 * result may have interned a prefix, but never publishes rows into a Space. */
bool tu_intern_ints(TermUniverse *universe, const int64_t *values,
                    size_t count, AtomId *ids);
AtomId tu_intern_float(TermUniverse *universe, double value);
AtomId tu_intern_bool(TermUniverse *universe, bool value);
AtomId tu_intern_stable_tag(TermUniverse *universe, int64_t tag);
AtomId tu_intern_string_n(TermUniverse *universe, const char *bytes,
                          size_t len);
AtomId tu_intern_string(TermUniverse *universe, const char *value);
AtomId tu_intern_bigint(TermUniverse *universe, const char *value);
AtomId tu_intern_rational(TermUniverse *universe, const char *value);
/* The list [elems...], or [elems... | rest] when rest is not CETTA_ATOM_ID_NONE;
 * a rest that is itself a list or list pattern is spliced in. */
AtomId tu_list_from_ids(TermUniverse *universe, const AtomId *elems,
                        CettaExprLen elem_len, AtomId rest);
/* The braces node {elems...}. */
AtomId tu_braces_from_ids(TermUniverse *universe, const AtomId *elems,
                          CettaExprLen elem_len);
AtomId tu_expr_from_ids(TermUniverse *universe, const AtomId *child_ids,
                        CettaExprLen arity);
/* The expression of these children when the universe holds it; it is never
 * made. */
AtomId tu_expr_lookup_ids(const TermUniverse *universe,
                          const AtomId *child_ids, CettaExprLen arity);
/* Canonical fixed-arity rows in input order. An optional presence mask skips
 * rows without reading their coordinates and returns NONE in those positions.
 * Failure retains admitted prefix identities, with remaining outputs NONE;
 * no Space rows are published. Coordinates must precede the call's admission. */
bool tu_exprs_from_ids(TermUniverse *universe, const AtomId *child_ids,
                       size_t count, CettaExprLen arity, const bool *present,
                       AtomId *ids);

#endif /* CETTA_TERM_UNIVERSE_H */
