#ifndef CETTA_SYMBOL_H
#define CETTA_SYMBOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdint.h>
#include <pthread.h>
#include "scalar_trie.h"
#include "lang.h"

#ifndef CETTA_SYMBOL_READ_COVERAGE
#define CETTA_SYMBOL_READ_COVERAGE 0
#endif

typedef uint32_t SymbolId;

#define SYMBOL_ID_NONE ((SymbolId)0)

enum {
    CETTA_SYMBOL_FLAG_STATIC_GROUNDED_OP = 1u << 0,
    CETTA_SYMBOL_FLAG_TYPE_PURE_GROUNDED_OP = 1u << 1,
    /* Only explicitly data-born entries may be considered for collection.
     * The legacy intern API owns its result permanently. */
    CETTA_SYMBOL_FLAG_RUNTIME_DATA = 1u << 4,
    CETTA_SYMBOL_FLAG_SPECULATIVE_OP = 1u << 2,
    CETTA_SYMBOL_FLAG_BUILTIN = 1u << 3,
    CETTA_SYMBOL_FLAG_PETTA_ONLY_BUILTIN = 1u << 5,
    CETTA_SYMBOL_FLAG_BUILTIN_CONSTRUCTOR = 1u << 6,
    CETTA_SYMBOL_FLAG_LIBRARY_NATIVE_OP = 1u << 7,
    CETTA_SYMBOL_FLAG_CORE_GROUNDED_OP = 1u << 8,
    /* Negative result of the transform-name classifier, owned by this table.
     * IDs are never reused, so the flag cannot outlive its symbol. */
    CETTA_SYMBOL_FLAG_NOT_TRANSFORM_NAME = 1u << 9,
};

typedef struct {
    const char *bytes;
    uint32_t len;
    uint64_t hash;
    _Atomic uint32_t flags;
    /* The builtin policy of the name, resolved on first use: zero until
     * then, otherwise the policy's position plus one (position zero: none).
     * Read and written only through the accessors below. */
    uint32_t builtin_policy;
    /* The symbol's canonical atom, owned by the table rather than an arena:
     * made on first request (atom_symbol_canonical), freed with the table. */
    _Atomic(void *) canonical_atom;
} SymbolEntry;

/* Payload bytes include the terminating NUL. Metadata includes reserved
 * entry chunks and index slots, rather than only populated entries. These
 * gauges describe table storage; resetting runtime counters does not reset
 * them. No entries are reclaimed in the instrumentation phase. */
typedef struct {
    uint64_t owned_count;
    uint64_t data_count;
    uint64_t retired_count;
    uint64_t owned_bytes;
    uint64_t data_bytes;
    uint64_t metadata_bytes;
    uint64_t peak_payload_bytes;
    uint64_t peak_metadata_bytes;
    uint64_t data_allocated_bytes;
    uint64_t promotions;
} SymbolTableStats;

typedef struct {
    uint64_t hash;
    uint32_t len;
    SymbolId id;
} SymbolSlot;

typedef struct SymbolCanonicalChunk SymbolCanonicalChunk;

#define SYMBOL_ENTRY_CHUNK_BITS 14u
#define SYMBOL_ENTRY_CHUNK_SIZE (1u << SYMBOL_ENTRY_CHUNK_BITS)
#define SYMBOL_ENTRY_CHUNK_MASK (SYMBOL_ENTRY_CHUNK_SIZE - 1u)
#define SYMBOL_ENTRY_CHUNK_COUNT (1u << (32u - SYMBOL_ENTRY_CHUNK_BITS))

typedef struct {
    SymbolSlot *slots;
    uint32_t slot_cap;
    uint32_t slot_used;
    uint64_t instance_id;
    /* Mixed with a spelling's hash to place it, drawn when the table is
     * made: spellings crafted to share their hash's low bits do not share a
     * slot.  The hash itself, which others read, carries no key. */
    uint64_t placement_key;

    SymbolEntry **entry_chunks;
    _Atomic uint32_t entry_len;
    /* Immutable table-owned atoms in publication order. Chunks and entries
     * are append-only until table destruction; canonical_count publishes a
     * complete prefix. This lets checkpoint domains enumerate actual global
     * atoms without scanning every interned spelling. */
    SymbolCanonicalChunk *canonical_first, *canonical_last;
    _Atomic uint32_t canonical_count;
    pthread_mutex_t write_mutex;
    SymbolTableStats stats;
    /* Lazily populated spelling index, protected by write_mutex. Entries are
     * immutable scalar sequences; opaque non-UTF-8 spellings are counted. */
    CettaScalarTrie *name_trie;
    uint32_t name_trie_count, name_trie_invalid;
    /* The spellings, packed into chunks freed with the table: a symbol lives
     * as long as its table, so its spelling costs its bytes and no more.
     * Protected by write_mutex. */
    struct SymbolSpellingChunk *spelling_chunks;
    char *spelling_free;
    size_t spelling_left;
} SymbolTable;

/* Non-destructive marker substrate. Unmarked bytes are diagnostics, never
 * permission to free: the caller must still qualify every root owner and
 * mutator/borrow boundary. Late births invalidate the snapshot for sweeping.
 * This API contains no retirement or payload mutation operation. */
typedef struct {
    SymbolTable *table;
    uint64_t instance_id;
    uint32_t entry_limit;
    uint8_t *marked;
    bool late_mark;
} SymbolShadowScan;

typedef struct {
    uint64_t marked_data_count;
    uint64_t marked_data_bytes;
    uint64_t unmarked_data_count;
    uint64_t unmarked_data_bytes;
    uint64_t late_births;
    bool snapshot_outdated;
} SymbolShadowResult;

bool symbol_shadow_begin(SymbolTable *st, SymbolShadowScan *scan);
bool symbol_shadow_mark(SymbolShadowScan *scan, SymbolId id);
bool symbol_shadow_finish(SymbolShadowScan *scan, SymbolShadowResult *out);
void symbol_shadow_destroy(SymbolShadowScan *scan);

#if CETTA_SYMBOL_READ_COVERAGE
/* Test-only, thread-local read diagnostic for one pre-existing ID. The
 * collector marks copied atoms; selected symbol accessors check that mark.
 * This samples neither every holder nor direct ID reads, and cannot see a
 * borrowed bytes pointer retained across the boundary. It is not permission
 * to reclaim an entry. */
bool symbol_read_coverage_watch(SymbolTable *st, SymbolId id);
void symbol_read_coverage_begin(SymbolTable *st);
void symbol_read_coverage_mark_copy(SymbolTable *st, SymbolId id);
void symbol_read_coverage_commit(SymbolTable *st);
void symbol_read_coverage_cancel(SymbolTable *st);
void symbol_read_coverage_clear(void);
void symbol_read_coverage_omit_marks_for_test(bool omit);
uint64_t symbol_read_coverage_collections(void);
uint64_t symbol_read_coverage_marked_copies(void);
uint64_t symbol_read_coverage_checked_reads(void);
#endif

static inline uint64_t symbol_table_instance_id(const SymbolTable *st) {
    return st ? st->instance_id : 0u;
}

typedef struct {
    const char *bytes;
    uint32_t len;
} SymbolSpelling;

typedef struct {
    SymbolId *ids;
    size_t len;
    /* Exclusive upper bound on the captured table: IDs 1..count-1. */
    uint32_t snapshot_count, invalid_utf8_count;
    CettaTrieWalkStats stats;
} SymbolNameQueryResult;
/* NULL automaton obtains the valid-text snapshot for an independently
 * interpreted fallback. Product callbacks must not intern into this table.
 * IDs are returned once each in interning order. Invalid UTF-8 names remain
 * in the symbol table and are reported, not interpreted as absent matches.
 * Replacement is atomic; initialize result to zero before first use. */
bool symbol_table_query_names(SymbolTable *, const CettaTrieAutomaton *,
                             const void *initial, SymbolNameQueryResult *);
void symbol_name_query_free(SymbolNameQueryResult *);

/* One publication check for both parts of a spelling. Interned bytes and
 * lengths never change; the bytes remain borrowed from the table until it
 * is freed. The length includes bytes following an embedded zero. */
static inline SymbolSpelling symbol_spelling(
    const SymbolTable *st, SymbolId id) {
    SymbolSpelling spelling = { .bytes = "", .len = 0u };
    if (!st || id == SYMBOL_ID_NONE)
        return spelling;
    uint32_t count = atomic_load_explicit(
        &st->entry_len, memory_order_acquire);
    if (id >= count || !st->entry_chunks)
        return spelling;
    const SymbolEntry *chunk =
        st->entry_chunks[id >> SYMBOL_ENTRY_CHUNK_BITS];
    if (!chunk)
        return spelling;
    const SymbolEntry *entry = &chunk[id & SYMBOL_ENTRY_CHUNK_MASK];
    spelling.bytes = entry->bytes ? entry->bytes : "";
    spelling.len = entry->len;
    return spelling;
}

/* The builtin vocabulary and its memberships are declared once, in
 * lib/builtin_symbols.data; each column's comment there states what
 * admitting a builtin to it means.  The generated header holds the table's
 * views (tools/regenerate_registry_tables.py).  Row order is the SymbolId
 * order: a new builtin is appended, never inserted. */
#include "generated/cetta_builtin_symbols.generated.h"

#define CETTA_BUILTIN_SYMBOLS(X) CETTA_BUILTIN_SYMBOL_ROWS(X)
#define CETTA_PETTA_ONLY_BUILTIN_SYMBOL_FIELDS(X) \
    CETTA_PETTA_ONLY_BUILTIN_SYMBOL_ROWS(X)
#define CETTA_BUILTIN_CONSTRUCTOR_SYMBOL_FIELDS(X) \
    CETTA_BUILTIN_CONSTRUCTOR_SYMBOL_ROWS(X)
#define CETTA_PUBLIC_LIBRARY_NATIVE_SYMBOL_FIELDS(X) \
    CETTA_PUBLIC_LIBRARY_NATIVE_SYMBOL_ROWS(X)
#define CETTA_STATIC_GROUNDED_SYMBOL_FIELDS(X) \
    CETTA_STATIC_GROUNDED_SYMBOL_ROWS(X)
#define CETTA_PURE_GROUNDED_SYMBOL_FIELDS(X) \
    CETTA_PURE_GROUNDED_SYMBOL_ROWS(X)
/* Type purity is effect freedom plus the type-only licenses. */
#define CETTA_TYPE_PURE_GROUNDED_SYMBOL_FIELDS(X) \
    CETTA_PURE_GROUNDED_SYMBOL_ROWS(X) \
    CETTA_TYPE_PURE_EXTRA_GROUNDED_SYMBOL_ROWS(X)
#define CETTA_SPECULATIVE_SYMBOL_FIELDS(X) \
    CETTA_SPECULATIVE_SYMBOL_ROWS(X)

typedef struct {
#define CETTA_BUILTIN_SYMBOL_FIELD(field, text) SymbolId field;
    CETTA_BUILTIN_SYMBOLS(CETTA_BUILTIN_SYMBOL_FIELD)
#undef CETTA_BUILTIN_SYMBOL_FIELD
} BuiltinSyms;

extern _Thread_local SymbolTable *g_symbols;
extern _Thread_local BuiltinSyms g_builtin_syms;

/* Registration flags identify the builtin vocabulary independently of IDs.
 * Executable membership additionally depends on dialect and constructor role;
 * callers that dispatch operations use symbol_id_is_builtin_for instead. */
bool symbol_id_is_builtin(SymbolId id);
/* Executable builtin membership; registered type constructors and names owned
 * by another dialect keep ordinary data/equation resolution in that dialect. */
bool symbol_id_is_builtin_for(SymbolId id, CettaLanguageId language);

/* The next key of the sequence every table that places untrusted content
 * draws its placement key from: seeded once a process, by
 * CETTA_PLACEMENT_KEY when it is set (reproducible runs), otherwise at
 * random.  A key moves where entries sit, never what a table answers. */
uint64_t cetta_placement_key_next(void);

void symbol_table_init(SymbolTable *st);
/* Failed initialization releases acquired storage. No free is needed on
 * failure; a successful table requires symbol_table_free. errno retains
 * the allocation, mutex or identity-capacity cause. */
bool symbol_table_try_init(SymbolTable *st);
void symbol_table_free(SymbolTable *st);
void symbol_table_init_builtins(SymbolTable *st, BuiltinSyms *builtins);
/* On failure, builtins is zero and the table retains its interned prefix. */
bool symbol_table_try_init_builtins(SymbolTable *st, BuiltinSyms *builtins);

/* A span of bytes names a symbol, the empty span included: the empty name is
 * the symbol of name concatenation's identity (bytes may be NULL when len is
 * 0).  The C-string entry points below name symbols from C literals and
 * refuse "". */
SymbolId symbol_intern_bytes(SymbolTable *st, const uint8_t *bytes, uint32_t len);
SymbolId symbol_intern_span_hashed(SymbolTable *st, const uint8_t *bytes,
                                   uint32_t len, uint64_t hash);
/* Return NONE with errno describing allocation/identity refusal. */
SymbolId symbol_try_intern_span_hashed(SymbolTable *st, const uint8_t *, uint32_t, uint64_t);
SymbolId symbol_try_intern_cstr(SymbolTable *st, const char *text);
SymbolId symbol_intern_cstr(SymbolTable *st, const char *text);
/* Data births preserve the same spelling, ID equality and hash as owned
 * interning. An owned intern of an existing data spelling promotes it;
 * subsequent data interns never downgrade it. Interning does not collect. */
SymbolId symbol_intern_data_bytes(SymbolTable *st, const uint8_t *bytes,
                                  uint32_t len);
SymbolId symbol_intern_data_cstr(SymbolTable *st, const char *text);
/* Registration of a raw ID gives it permanent ownership. */
bool symbol_pin(SymbolTable *st, SymbolId id);
void symbol_table_stats_snapshot(SymbolTable *st, SymbolTableStats *out);
/* The symbol already interned for text, or SYMBOL_ID_NONE.  Never interns. */
SymbolId symbol_lookup_cstr(SymbolTable *st, const char *text);
/* A lookup never acquires ownership or inserts a missing spelling. */
SymbolId symbol_lookup_span_hashed(SymbolTable *st, const uint8_t *bytes,
                                    uint32_t len, uint64_t hash);
uint64_t symbol_hash_span(const uint8_t *bytes, uint32_t len);

/* Borrowed text: currently valid until table destruction. A future sweep
 * must restrict data-text borrows to protected roots/leases or the interval
 * before the next eligible quiescent boundary. The table mutex alone cannot
 * protect this unlocked borrow. Owned spellings remain permanent. */
const char *symbol_bytes(const SymbolTable *st, SymbolId id);
uint32_t symbol_len(const SymbolTable *st, SymbolId id);
/* Bytewise spelling order, including bytes after embedded NUL. Equal IDs
 * require no spelling read; no table entry address escapes to the caller. */
int symbol_compare(const SymbolTable *st, SymbolId left, SymbolId right);
uint64_t symbol_hash_value(const SymbolTable *st, SymbolId id);
uint32_t symbol_flags(const SymbolTable *st, SymbolId id);
/* Classification flags are monotonic within a table incarnation. */
void symbol_table_add_flags(SymbolTable *st, SymbolId id, uint32_t flags);
uint32_t symbol_builtin_policy(const SymbolTable *st, SymbolId id);
/* The value depends only on the name, so concurrent callers record the same. */
void symbol_set_builtin_policy(SymbolTable *st, SymbolId id, uint32_t value);
/* The symbol's canonical atom, or NULL until one is published. */
void *symbol_canonical_atom(const SymbolTable *st, SymbolId id);
/* Publish `atom` as the symbol's canonical atom unless one is already:
 * the published atom, which is `atom` only when it won; NULL for an id the
 * table does not hold.  The table frees the published atom. */
void *symbol_publish_canonical_atom(SymbolTable *st, SymbolId id, void *atom);
uint32_t symbol_canonical_count(const SymbolTable *st);
typedef bool (*SymbolCanonicalVisitor)(void *atom, void *context);
/* Visit exactly one stable published prefix. Concurrent later publication
 * makes the visit fail rather than silently widening its checkpoint domain. */
bool symbol_canonical_visit(const SymbolTable *st, uint64_t instance,
    uint32_t count, SymbolCanonicalVisitor visit, void *context);
bool symbol_eq_cstr(const SymbolTable *st, SymbolId id, const char *text);


/* PeTTa's determinism-annotated arrows are written `-[mode]->`.  The mode is
   a syntactic property of the arrow symbol; whether it carries authority is a
   language/profile decision made by the caller. */
typedef enum {
    CETTA_PETTA_ARROW_MODE_NONE = 0,
    CETTA_PETTA_ARROW_MODE_DET,
    CETTA_PETTA_ARROW_MODE_NONDET,
    CETTA_PETTA_ARROW_MODE_OTHER,
} CettaPettaArrowMode;

CettaPettaArrowMode cetta_petta_arrow_mode(SymbolId id);

#include "native_memory.h"
#endif /* CETTA_SYMBOL_H */
