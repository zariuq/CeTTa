#include "symbol.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define SYMBOL_TABLE_MIN_SLOTS 4096u
#define SYMBOL_TABLE_MAX_LOAD_NUM 7u
#define SYMBOL_TABLE_MAX_LOAD_DEN 10u

_Thread_local SymbolTable *g_symbols = NULL;
_Thread_local BuiltinSyms g_builtin_syms;
static _Atomic uint64_t g_symbol_table_instance_counter = 1u;

#if CETTA_SYMBOL_READ_COVERAGE
typedef struct {
    SymbolTable *table;
    uint64_t instance_id;
    SymbolId watched;
    bool collecting;
    bool armed;
    bool marked;
    bool omit_marks;
    uint64_t collections;
    uint64_t marked_copies;
    uint64_t checked_reads;
} SymbolReadCoverage;

static _Thread_local SymbolReadCoverage read_coverage;

static void symbol_read_coverage_check(const SymbolTable *st, SymbolId id) {
    SymbolReadCoverage *gate = &read_coverage;
    if (!gate->armed || gate->table != st || gate->watched != id ||
        gate->instance_id != st->instance_id)
        return;
    gate->checked_reads++;
    if (!gate->marked) {
        fprintf(stderr, "CeTTa: unmarked symbol read after collection (id=%u)\n",
                (unsigned)id);
        abort();
    }
}

bool symbol_read_coverage_watch(SymbolTable *st, SymbolId id) {
    if (!st || id == SYMBOL_ID_NONE || id >=
        atomic_load_explicit(&st->entry_len, memory_order_acquire))
        return false;
    read_coverage = (SymbolReadCoverage){.table = st,
        .instance_id = st->instance_id, .watched = id};
    return true;
}

void symbol_read_coverage_begin(SymbolTable *st) {
    SymbolReadCoverage *gate = &read_coverage;
    if (gate->table != st || gate->instance_id != st->instance_id)
        return;
    gate->collecting = true;
    gate->armed = false;
    gate->marked = false;
    gate->marked_copies = 0u;
}

void symbol_read_coverage_mark_copy(SymbolTable *st, SymbolId id) {
    SymbolReadCoverage *gate = &read_coverage;
    if (gate->collecting && gate->table == st && gate->watched == id &&
        gate->instance_id == st->instance_id && !gate->omit_marks) {
        gate->marked = true;
        gate->marked_copies++;
    }
}

void symbol_read_coverage_commit(SymbolTable *st) {
    SymbolReadCoverage *gate = &read_coverage;
    if (gate->table != st || gate->instance_id != st->instance_id ||
        !gate->collecting)
        return;
    gate->collecting = false;
    gate->armed = true;
    gate->collections++;
}

void symbol_read_coverage_cancel(SymbolTable *st) {
    SymbolReadCoverage *gate = &read_coverage;
    if (gate->table != st)
        return;
    gate->collecting = false;
    gate->armed = false;
}

void symbol_read_coverage_clear(void) {
    read_coverage = (SymbolReadCoverage){0};
}

void symbol_read_coverage_omit_marks_for_test(bool omit) {
    read_coverage.omit_marks = omit;
}

uint64_t symbol_read_coverage_collections(void) {
    return read_coverage.collections;
}

uint64_t symbol_read_coverage_marked_copies(void) {
    return read_coverage.marked_copies;
}

uint64_t symbol_read_coverage_checked_reads(void) {
    return read_coverage.checked_reads;
}
#endif

static void symbol_oom(size_t size) {
    fprintf(stderr, "fatal: out of memory allocating %zu bytes\n", size);
    abort();
}

static uint64_t symbol_table_instance_new(void) {
    uint64_t next = atomic_load_explicit(&g_symbol_table_instance_counter,
                                         memory_order_relaxed);
    while (next && next < UINT64_MAX) {
        if (atomic_compare_exchange_weak_explicit(&g_symbol_table_instance_counter,
                &next, next + 1u, memory_order_relaxed, memory_order_relaxed))
            return next;
    }
    errno = EOVERFLOW;
    return 0u;
}

static _Noreturn void symbol_fatal(const char *operation) {
    fprintf(stderr, "fatal: %s: %s\n", operation, strerror(errno));
    abort();
}

/* Spellings are packed into chunks of this many bytes; a longer one than an
 * eighth of a chunk gets a block of its own. */
#define SYMBOL_SPELLING_CHUNK_BYTES 65536u

struct SymbolSpellingChunk {
    struct SymbolSpellingChunk *next;
    char bytes[];
};

static char *symbol_spelling_alloc(SymbolTable *st, size_t size) {
    if (size > SYMBOL_SPELLING_CHUNK_BYTES / 8u) {
        struct SymbolSpellingChunk *own =
            malloc(sizeof(*own) + size);
        if (!own) return NULL;
        own->next = st->spelling_chunks;
        st->spelling_chunks = own;
        return own->bytes;
    }
    if (st->spelling_left < size) {
        struct SymbolSpellingChunk *chunk =
            malloc(sizeof(*chunk) + SYMBOL_SPELLING_CHUNK_BYTES);
        if (!chunk) return NULL;
        chunk->next = st->spelling_chunks;
        st->spelling_chunks = chunk;
        st->spelling_free = chunk->bytes;
        st->spelling_left = SYMBOL_SPELLING_CHUNK_BYTES;
    }
    char *at = st->spelling_free;
    st->spelling_free += size;
    st->spelling_left -= size;
    return at;
}

static void *symbol_calloc(size_t count, size_t size) {
    void *ptr = calloc(count == 0 ? 1 : count, size == 0 ? 1 : size);
    if (!ptr) symbol_oom(count * size);
    return ptr;
}

enum { SYMBOL_CANONICAL_CHUNK_SIZE = 256u };
struct SymbolCanonicalChunk {
    struct SymbolCanonicalChunk *next;
    void *atoms[SYMBOL_CANONICAL_CHUNK_SIZE];
};

static _Atomic uint64_t g_placement_state;
static pthread_once_t g_placement_once = PTHREAD_ONCE_INIT;

static void placement_seed(void) {
    uint64_t seed = 0u;
    const char *fixed = getenv("CETTA_PLACEMENT_KEY");
    if (fixed && fixed[0]) {
        seed = strtoull(fixed, NULL, 0);
    } else {
        FILE *random = fopen("/dev/urandom", "rb");
        if (!random || fread(&seed, sizeof(seed), 1u, random) != 1u) {
            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);
            seed = (uint64_t)now.tv_nsec ^ ((uint64_t)now.tv_sec << 20) ^
                   ((uint64_t)getpid() << 40);
        }
        if (random)
            fclose(random);
    }
    atomic_store(&g_placement_state, seed);
}

uint64_t cetta_placement_key_next(void) {
    pthread_once(&g_placement_once, placement_seed);
    uint64_t x = atomic_fetch_add(&g_placement_state,
                                  UINT64_C(0x9E3779B97F4A7C15)) +
                 UINT64_C(0x9E3779B97F4A7C15);
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}

/* A spelling's content hash: FNV-1a, public and stable, read beyond the
 * table (symbol_hash_value). */
static uint64_t symbol_hash_bytes(const uint8_t *bytes, uint32_t len) {
    uint64_t h = 1469598103934665603ULL;
    for (uint32_t i = 0; i < len; i++) {
        h ^= (uint64_t)bytes[i];
        h *= 1099511628211ULL;
    }
    return h ? h : 1ULL;
}

/* The home slot of HASH under the table's key. */
static uint32_t symbol_slot_capacity(const SymbolTable *st, uint64_t hash, uint32_t capacity) {
    uint64_t x = hash ^ st->placement_key;
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    x ^= x >> 31;
    return (uint32_t)x & (capacity - 1u);
}

static uint32_t symbol_slot(const SymbolTable *st, uint64_t hash) {
    return symbol_slot_capacity(st, hash, st->slot_cap);
}

static SymbolEntry *symbol_table_entry_chunk(const SymbolTable *st, SymbolId id) {
    if (!st || !st->entry_chunks) return NULL;
    return st->entry_chunks[id >> SYMBOL_ENTRY_CHUNK_BITS];
}

static const SymbolEntry *symbol_table_entry_ref(const SymbolTable *st, SymbolId id) {
    const SymbolEntry *chunk = symbol_table_entry_chunk(st, id);
    if (!chunk) return NULL;
    return &chunk[id & SYMBOL_ENTRY_CHUNK_MASK];
}

static SymbolEntry *symbol_table_entry_mut(SymbolTable *st, SymbolId id) {
    SymbolEntry *chunk = symbol_table_entry_chunk(st, id);
    if (!chunk) return NULL;
    return &chunk[id & SYMBOL_ENTRY_CHUNK_MASK];
}

static void symbol_table_add_flags_initial(SymbolTable *st, SymbolId id,
                                           uint32_t flags) {
    if (!st || id == SYMBOL_ID_NONE || flags == 0u)
        return;
    uint32_t entry_len = atomic_load_explicit(
        &st->entry_len, memory_order_relaxed);
    if (id >= entry_len)
        return;
    SymbolEntry *entry = symbol_table_entry_mut(st, id);
    if (entry)
        atomic_fetch_or_explicit(&entry->flags, flags, memory_order_relaxed);
}

static void symbol_table_note_metadata(SymbolTable *st, uint64_t bytes) {
    st->stats.metadata_bytes += bytes;
    if (st->stats.metadata_bytes > st->stats.peak_metadata_bytes)
        st->stats.peak_metadata_bytes = st->stats.metadata_bytes;
}

static void symbol_pin_entry(SymbolTable *st, SymbolEntry *entry) {
    uint32_t flags = atomic_load_explicit(&entry->flags, memory_order_relaxed);
    if (!(flags & CETTA_SYMBOL_FLAG_RUNTIME_DATA))
        return;
    atomic_fetch_and_explicit(&entry->flags, ~CETTA_SYMBOL_FLAG_RUNTIME_DATA,
                             memory_order_relaxed);
    uint64_t bytes = (uint64_t)entry->len + 1u;
    st->stats.data_count--;
    st->stats.data_bytes -= bytes;
    st->stats.owned_count++;
    st->stats.owned_bytes += bytes;
    st->stats.promotions++;
}

void symbol_table_add_flags(SymbolTable *st, SymbolId id,
                            uint32_t flags) {
    if (!st || id == SYMBOL_ID_NONE || flags == 0u)
        return;
    uint32_t entry_len = atomic_load_explicit(
        &st->entry_len, memory_order_relaxed);
    if (id >= entry_len)
        return;
    SymbolEntry *entry = symbol_table_entry_mut(st, id);
    if (entry)
        atomic_fetch_or_explicit(&entry->flags, flags, memory_order_relaxed);
}

static bool symbol_table_ensure_entry_chunk(SymbolTable *st, SymbolId id) {
    uint32_t chunk_index = id >> SYMBOL_ENTRY_CHUNK_BITS;
    if (chunk_index >= SYMBOL_ENTRY_CHUNK_COUNT) {
        errno = EOVERFLOW;
        return false;
    }
    if (st->entry_chunks[chunk_index]) return true;
    st->entry_chunks[chunk_index] =
        calloc(SYMBOL_ENTRY_CHUNK_SIZE, sizeof(SymbolEntry));
    if (!st->entry_chunks[chunk_index]) return false;
    symbol_table_note_metadata(
        st, (uint64_t)SYMBOL_ENTRY_CHUNK_SIZE * sizeof(SymbolEntry));
    return true;
}

static bool symbol_entry_matches(const SymbolEntry *entry, const uint8_t *bytes,
                                 uint32_t len, uint64_t hash) {
    return entry && entry->bytes && entry->len == len && entry->hash == hash &&
           memcmp(entry->bytes, bytes, len) == 0;
}

/* The table is unchanged when the new index cannot be allocated. */
static bool symbol_table_resize(SymbolTable *st, uint32_t new_cap) {
    if (!new_cap || (new_cap & (new_cap - 1u))) { errno = EOVERFLOW; return false; }
    SymbolSlot *old_slots = st->slots;
    uint32_t old_cap = st->slot_cap;
    uint32_t entry_len = atomic_load_explicit(&st->entry_len, memory_order_relaxed);

    if (!new_cap) { errno = EOVERFLOW; return false; }
    SymbolSlot *slots = calloc(new_cap, sizeof(*slots));
    if (!slots) return false;
    /* Both arrays are reserved during rebuild; include that transient
     * overlap in the peak, then subtract the released array below. */
    symbol_table_note_metadata(st, (uint64_t)new_cap * sizeof(SymbolSlot));
    uint32_t used = 0;

    for (uint32_t i = 1; i < entry_len; i++) {
        const SymbolEntry *entry = symbol_table_entry_ref(st, (SymbolId)i);
        if (!entry || !entry->bytes) continue;
        uint32_t slot = symbol_slot_capacity(st, entry->hash, new_cap);
        while (slots[slot].id != SYMBOL_ID_NONE) {
            slot = (slot + 1u) & (new_cap - 1u);
        }
        slots[slot].hash = entry->hash;
        slots[slot].len = entry->len;
        slots[slot].id = (SymbolId)i;
        used++;
    }

    st->slots = slots;
    st->slot_cap = new_cap;
    st->slot_used = used;
    free(old_slots);
    st->stats.metadata_bytes -= (uint64_t)old_cap * sizeof(SymbolSlot);
    return true;
}

bool symbol_table_try_init(SymbolTable *st) {
    if (!st) { errno = EINVAL; return false; }
    memset(st, 0, sizeof(*st));
    atomic_init(&st->entry_len, 0);
    atomic_init(&st->canonical_count, 0u);
    int error = pthread_mutex_init(&st->write_mutex, NULL);
    if (error) { errno = error; return false; }
    st->instance_id = symbol_table_instance_new();
    if (st->instance_id == 0u) {
        errno = EOVERFLOW;
        goto failed;
    }
    st->placement_key = cetta_placement_key_next();
    st->name_trie_count = 1u;
    st->entry_chunks = calloc(SYMBOL_ENTRY_CHUNK_COUNT, sizeof(SymbolEntry *));
    if (!st->entry_chunks) goto failed;
    symbol_table_note_metadata(
        st, (uint64_t)SYMBOL_ENTRY_CHUNK_COUNT * sizeof(SymbolEntry *));
    if (!symbol_table_ensure_entry_chunk(st, SYMBOL_ID_NONE)) goto failed;
    atomic_store_explicit(&st->entry_len, 1, memory_order_relaxed);
    if (!symbol_table_resize(st, SYMBOL_TABLE_MIN_SLOTS)) goto failed;
    return true;
failed:
    error = errno;
    /* The reserved first chunk belongs to the published prefix freed below. */
    if (st->entry_chunks && st->entry_chunks[0])
        atomic_store_explicit(&st->entry_len, 1, memory_order_relaxed);
    symbol_table_free(st);
    errno = error;
    return false;
}

void symbol_table_init(SymbolTable *st) {
    if (st && !symbol_table_try_init(st))
        symbol_fatal("symbol table initialization");
}

void symbol_table_free(SymbolTable *st) {
    if (!st) return;
    uint32_t entry_len = atomic_load_explicit(&st->entry_len, memory_order_relaxed);
    for (uint32_t i = 1; i < entry_len; i++) {
        SymbolEntry *entry = symbol_table_entry_mut(st, (SymbolId)i);
        if (!entry) continue;
        entry->bytes = NULL;
        free(atomic_load_explicit(&entry->canonical_atom,
                                  memory_order_relaxed));
        atomic_store_explicit(&entry->canonical_atom, NULL,
                              memory_order_relaxed);
    }
    if (st->entry_chunks) {
        /* Sequential IDs publish a dense prefix, including reserved zero.
         * The final partial chunk belongs to that prefix as well. */
        uint32_t chunks = entry_len
            ? ((entry_len - 1u) >> SYMBOL_ENTRY_CHUNK_BITS) + 1u : 0u;
        for (uint32_t i = 0; i < chunks; i++)
            free(st->entry_chunks[i]);
        free(st->entry_chunks);
    }
    free(st->slots);
    SymbolCanonicalChunk *canonical = st->canonical_first;
    while (canonical) {
        SymbolCanonicalChunk *next = canonical->next;
        free(canonical);
        canonical = next;
    }
    while (st->spelling_chunks) {
        struct SymbolSpellingChunk *next = st->spelling_chunks->next;
        free(st->spelling_chunks);
        st->spelling_chunks = next;
    }
    st->spelling_free = NULL;
    st->spelling_left = 0u;
    cetta_scalar_trie_free(st->name_trie);
    st->name_trie = NULL;
    st->name_trie_count = st->name_trie_invalid = 0u;
    pthread_mutex_destroy(&st->write_mutex);
    st->entry_chunks = NULL;
    st->slots = NULL;
    st->canonical_first = st->canonical_last = NULL;
    st->instance_id = 0u;
    atomic_store_explicit(&st->entry_len, 0, memory_order_relaxed);
    atomic_store_explicit(&st->canonical_count, 0u, memory_order_relaxed);
    st->slot_cap = 0;
    st->slot_used = 0;
    st->stats = (SymbolTableStats){0};
}

/* The empty name is a symbol like any other (the identity of name
 * concatenation, Prolog's ''): a span of length 0 may come without bytes. */
static const uint8_t symbol_empty_bytes[1] = {0};

static SymbolId symbol_intern_span_hashed_unlocked(SymbolTable *st,
                                                   const uint8_t *bytes,
                                                   uint32_t len,
                                                   uint64_t hash,
                                                   bool runtime_data) {
    uint32_t entry_len;

    if (!st || (!bytes && len)) return SYMBOL_ID_NONE;
    if (!bytes)
        bytes = symbol_empty_bytes;
    if (((uint64_t)st->slot_used + 1u) * SYMBOL_TABLE_MAX_LOAD_DEN >
        (uint64_t)st->slot_cap * SYMBOL_TABLE_MAX_LOAD_NUM) {
        if (st->slot_cap > UINT32_MAX / 2u) {
            errno = EOVERFLOW;
            return SYMBOL_ID_NONE;
        }
        if (!symbol_table_resize(st, st->slot_cap ? st->slot_cap * 2 : SYMBOL_TABLE_MIN_SLOTS))
            return SYMBOL_ID_NONE;
    }

    entry_len = atomic_load_explicit(&st->entry_len, memory_order_relaxed);
    uint32_t slot = symbol_slot(st, hash);
    while (true) {
        SymbolSlot *entry = &st->slots[slot];
        if (entry->id == SYMBOL_ID_NONE) {
            /* Leave room for the published exclusive upper bound. Never
             * wrap to the reserved zero ID, even after future retirement. */
            if (entry_len == UINT32_MAX) {
                errno = EOVERFLOW;
                return SYMBOL_ID_NONE;
            }
            SymbolId id = (SymbolId)entry_len;
            uint32_t chunk = id >> SYMBOL_ENTRY_CHUNK_BITS;
            bool new_chunk = chunk < SYMBOL_ENTRY_CHUNK_COUNT &&
                st->entry_chunks[chunk] == NULL;
            if (!symbol_table_ensure_entry_chunk(st, id)) return SYMBOL_ID_NONE;
            char *copy = symbol_spelling_alloc(st, (size_t)len + 1);
            if (!copy) {
                if (new_chunk) {
                    free(st->entry_chunks[chunk]);
                    st->entry_chunks[chunk] = NULL;
                    st->stats.metadata_bytes -=
                        (uint64_t)SYMBOL_ENTRY_CHUNK_SIZE * sizeof(SymbolEntry);
                }
                return SYMBOL_ID_NONE;
            }
            memcpy(copy, bytes, len);
            copy[len] = '\0';

            SymbolEntry *sym = symbol_table_entry_mut(st, id);
            sym->bytes = copy;
            sym->len = len;
            sym->hash = hash;
            atomic_init(&sym->flags,
                        runtime_data ? CETTA_SYMBOL_FLAG_RUNTIME_DATA : 0u);
            sym->builtin_policy = 0;
            atomic_init(&sym->canonical_atom, NULL);

            uint64_t allocated = (uint64_t)len + 1u;
            if (runtime_data) {
                st->stats.data_count++;
                st->stats.data_bytes += allocated;
                st->stats.data_allocated_bytes += allocated;
            } else {
                st->stats.owned_count++;
                st->stats.owned_bytes += allocated;
            }
            uint64_t payload = st->stats.owned_bytes + st->stats.data_bytes;
            if (payload > st->stats.peak_payload_bytes)
                st->stats.peak_payload_bytes = payload;

            entry->hash = hash;
            entry->len = len;
            entry->id = id;
            st->slot_used++;
            if (st->name_trie) {
                if (!cetta_scalar_trie_valid_utf8((const uint8_t *)copy, len)) {
                    ++st->name_trie_invalid;
                } else if (!cetta_scalar_trie_insert_utf8(st->name_trie,
                            (const uint8_t *)copy, len, id)) {
                    /* A failed optional index never prevents interning. Its
                     * next query will reconstruct from the authoritative rows. */
                    cetta_scalar_trie_free(st->name_trie); st->name_trie = NULL;
                    st->name_trie_count = 1u; st->name_trie_invalid = 0u;
                }
                if (st->name_trie) st->name_trie_count = id + 1u;
            }
            atomic_store_explicit(&st->entry_len, id + 1, memory_order_release);
            return id;
        }
        if (entry->hash == hash) {
            SymbolEntry *sym = symbol_table_entry_mut(st, entry->id);
            if (symbol_entry_matches(sym, bytes, len, hash)) {
                if (!runtime_data)
                    symbol_pin_entry(st, sym);
                return entry->id;
            }
        }
        slot = (slot + 1u) & (st->slot_cap - 1u);
    }
}

SymbolId symbol_try_intern_span_hashed(SymbolTable *st, const uint8_t *bytes,
                                       uint32_t len, uint64_t hash) {
    SymbolId id;
    if (!st || (!bytes && len)) { errno = EINVAL; return SYMBOL_ID_NONE; }
    pthread_mutex_lock(&st->write_mutex);
    id = symbol_intern_span_hashed_unlocked(st, bytes, len, hash, false);
    pthread_mutex_unlock(&st->write_mutex);
    return id;
}

SymbolId symbol_intern_span_hashed(SymbolTable *st, const uint8_t *bytes,
                                   uint32_t len, uint64_t hash) {
    if (!st || (!bytes && len)) return SYMBOL_ID_NONE;
    SymbolId id = symbol_try_intern_span_hashed(st, bytes, len, hash);
    if (!id && !(cetta_native_memory_recover && cetta_native_memory_recover()))
        symbol_fatal("symbol interning");
    return id;
}

typedef struct { SymbolNameQueryResult *result; size_t capacity; } NameCollector;
static bool symbol_name_collect(void *context, uint64_t payload) {
    NameCollector *c = context;
    if (!payload || payload >= c->result->snapshot_count) return false;
    if (c->result->len == c->capacity) {
        size_t cap = c->capacity ? c->capacity * 2u : 32u;
        if (cap < c->capacity || cap > SIZE_MAX / sizeof(SymbolId)) return false;
        SymbolId *ids = realloc(c->result->ids, cap * sizeof(*ids));
        if (!ids) return false;
        c->result->ids = ids; c->capacity = cap;
    }
    c->result->ids[c->result->len++] = (SymbolId)payload; return true;
}
static int symbol_id_compare(const void *a, const void *b) {
    SymbolId x = *(const SymbolId *)a, y = *(const SymbolId *)b;
    return x < y ? -1 : x > y;
}
void symbol_name_query_free(SymbolNameQueryResult *r) {
    if (!r) return;
    free(r->ids); memset(r, 0, sizeof(*r));
}
bool symbol_table_query_names(SymbolTable *st, const CettaTrieAutomaton *automaton,
                             const void *initial, SymbolNameQueryResult *out) {
    if (!st || !out || (automaton && !initial)) return false;
    SymbolNameQueryResult result = {0}; NameCollector collector = {&result, 0u};
    bool ok = false;
    pthread_mutex_lock(&st->write_mutex);
    result.snapshot_count = atomic_load_explicit(&st->entry_len, memory_order_acquire);
    if (!automaton) {
        for (SymbolId id = 1u; id < result.snapshot_count; ++id) {
            const SymbolEntry *e = symbol_table_entry_ref(st, id);
            if (!cetta_scalar_trie_valid_utf8((const uint8_t *)e->bytes, e->len)) {
                ++result.invalid_utf8_count; continue;
            }
            if (!symbol_name_collect(&collector, id)) goto done;
        }
        ok = true; goto done;
    }
    if (!st->name_trie) {
        st->name_trie = cetta_scalar_trie_new();
        st->name_trie_count = 1u; st->name_trie_invalid = 0u;
        if (!st->name_trie) goto done;
    }
    for (SymbolId id = st->name_trie_count; id < result.snapshot_count; ++id) {
        const SymbolEntry *e = symbol_table_entry_ref(st, id);
        if (!cetta_scalar_trie_valid_utf8((const uint8_t *)e->bytes, e->len)) {
            ++st->name_trie_invalid;
        } else if (!cetta_scalar_trie_insert_utf8(st->name_trie,
                    (const uint8_t *)e->bytes, e->len, id)) {
            cetta_scalar_trie_free(st->name_trie); st->name_trie = NULL;
            st->name_trie_count = 1u; st->name_trie_invalid = 0u; goto done;
        }
        st->name_trie_count = id + 1u;
    }
    result.invalid_utf8_count = st->name_trie_invalid;
    ok = cetta_scalar_trie_walk(st->name_trie, automaton, initial,
                              symbol_name_collect, &collector, &result.stats);
    if (ok && result.len > 1u) qsort(result.ids, result.len, sizeof(*result.ids), symbol_id_compare);
done:
    pthread_mutex_unlock(&st->write_mutex);
    if (!ok) { symbol_name_query_free(&result); return false; }
    symbol_name_query_free(out); *out = result; return true;
}

SymbolId symbol_intern_bytes(SymbolTable *st, const uint8_t *bytes, uint32_t len) {
    if (!st || (!bytes && len)) return SYMBOL_ID_NONE;
    if (!bytes)
        bytes = symbol_empty_bytes;
    return symbol_intern_span_hashed(st, bytes, len,
                                     symbol_hash_bytes(bytes, len));
}

SymbolId symbol_intern_cstr(SymbolTable *st, const char *text) {
    if (!st || !text || !*text) return SYMBOL_ID_NONE;
    return symbol_intern_bytes(st, (const uint8_t *)text, (uint32_t)strlen(text));
}

SymbolId symbol_try_intern_cstr(SymbolTable *st, const char *text) {
    if (!st || !text || !*text) { errno = EINVAL; return SYMBOL_ID_NONE; }
    size_t length = strlen(text);
    if (length > UINT32_MAX) { errno = EOVERFLOW; return SYMBOL_ID_NONE; }
    return symbol_try_intern_span_hashed(st, (const uint8_t *)text, (uint32_t)length,
        symbol_hash_bytes((const uint8_t *)text, (uint32_t)length));
}

SymbolId symbol_intern_data_bytes(SymbolTable *st, const uint8_t *bytes,
                                  uint32_t len) {
    if (!st || !bytes || len == 0u)
        return SYMBOL_ID_NONE;
    uint64_t hash = symbol_hash_bytes(bytes, len);
    pthread_mutex_lock(&st->write_mutex);
    SymbolId id = symbol_intern_span_hashed_unlocked(st, bytes, len, hash, true);
    pthread_mutex_unlock(&st->write_mutex);
    if (!id && !(cetta_native_memory_recover && cetta_native_memory_recover()))
        symbol_fatal("symbol interning");
    return id;
}

SymbolId symbol_intern_data_cstr(SymbolTable *st, const char *text) {
    if (!text || !*text)
        return SYMBOL_ID_NONE;
    return symbol_intern_data_bytes(st, (const uint8_t *)text,
                                   (uint32_t)strlen(text));
}

bool symbol_pin(SymbolTable *st, SymbolId id) {
    if (!st || id == SYMBOL_ID_NONE)
        return false;
    uint32_t published = atomic_load_explicit(&st->entry_len, memory_order_acquire);
    const SymbolEntry *existing = id < published ? symbol_table_entry_ref(st, id) : NULL;
    if (!existing || !existing->bytes)
        return false;
    /* Ownership never decreases. Repeated structural registrations of an
     * owned key therefore need neither the writer lock nor a counter update. */
    if (!(atomic_load_explicit(&existing->flags, memory_order_relaxed) &
          CETTA_SYMBOL_FLAG_RUNTIME_DATA))
        return true;
    pthread_mutex_lock(&st->write_mutex);
    uint32_t entry_len = atomic_load_explicit(&st->entry_len, memory_order_relaxed);
    SymbolEntry *entry = id < entry_len ? symbol_table_entry_mut(st, id) : NULL;
    bool valid = entry && entry->bytes;
    if (valid)
        symbol_pin_entry(st, entry);
    pthread_mutex_unlock(&st->write_mutex);
    return valid;
}

void symbol_table_stats_snapshot(SymbolTable *st, SymbolTableStats *out) {
    if (!out)
        return;
    *out = (SymbolTableStats){0};
    if (!st)
        return;
    pthread_mutex_lock(&st->write_mutex);
    *out = st->stats;
    pthread_mutex_unlock(&st->write_mutex);
}

bool symbol_shadow_begin(SymbolTable *st, SymbolShadowScan *scan) {
    if (!st || !scan)
        return false;
    *scan = (SymbolShadowScan){0};
    pthread_mutex_lock(&st->write_mutex);
    scan->table = st;
    scan->instance_id = st->instance_id;
    scan->entry_limit = atomic_load_explicit(&st->entry_len, memory_order_relaxed);
    scan->marked = symbol_calloc(((size_t)scan->entry_limit + 7u) / 8u, 1u);
    pthread_mutex_unlock(&st->write_mutex);
    return true;
}

bool symbol_shadow_mark(SymbolShadowScan *scan, SymbolId id) {
    if (!scan || !scan->marked)
        return false;
    if (id == SYMBOL_ID_NONE)
        return true;
    if (id >= scan->entry_limit) {
        scan->late_mark = true;
        return false;
    }
    scan->marked[id >> 3u] |= (uint8_t)(1u << (id & 7u));
    return true;
}

bool symbol_shadow_finish(SymbolShadowScan *scan, SymbolShadowResult *out) {
    if (!scan || !scan->table || !scan->marked || !out)
        return false;
    *out = (SymbolShadowResult){0};
    SymbolTable *st = scan->table;
    pthread_mutex_lock(&st->write_mutex);
    if (st->instance_id != scan->instance_id) {
        pthread_mutex_unlock(&st->write_mutex);
        return false;
    }
    uint32_t entry_len = atomic_load_explicit(&st->entry_len, memory_order_relaxed);
    if (entry_len < scan->entry_limit) {
        pthread_mutex_unlock(&st->write_mutex);
        return false;
    }
    out->late_births = entry_len - scan->entry_limit;
    out->snapshot_outdated = out->late_births != 0u || scan->late_mark;
    for (uint32_t id = 1u; id < scan->entry_limit; id++) {
        const SymbolEntry *entry = symbol_table_entry_ref(st, id);
        if (!entry || !entry->bytes ||
            !(atomic_load_explicit(&entry->flags, memory_order_relaxed) &
              CETTA_SYMBOL_FLAG_RUNTIME_DATA))
            continue;
        uint64_t bytes = (uint64_t)entry->len + 1u;
        if (scan->marked[id >> 3u] & (uint8_t)(1u << (id & 7u))) {
            out->marked_data_count++;
            out->marked_data_bytes += bytes;
        } else {
            out->unmarked_data_count++;
            out->unmarked_data_bytes += bytes;
        }
    }
    pthread_mutex_unlock(&st->write_mutex);
    return true;
}

void symbol_shadow_destroy(SymbolShadowScan *scan) {
    if (!scan)
        return;
    free(scan->marked);
    *scan = (SymbolShadowScan){0};
}

uint64_t symbol_hash_span(const uint8_t *bytes, uint32_t len) {
    return symbol_hash_bytes(bytes, len);
}

SymbolId symbol_lookup_span_hashed(SymbolTable *st, const uint8_t *bytes,
                                    uint32_t len, uint64_t hash) {
    if (!st || !bytes || !len) return SYMBOL_ID_NONE;
    SymbolId found = SYMBOL_ID_NONE;
    pthread_mutex_lock(&st->write_mutex);
    if (st->slot_cap) {
        uint32_t slot = symbol_slot(st, hash);
        while (st->slots[slot].id != SYMBOL_ID_NONE) {
            const SymbolSlot *entry = &st->slots[slot];
            if (entry->hash == hash &&
                symbol_entry_matches(symbol_table_entry_ref(st, entry->id),
                                     bytes, len, hash)) {
                found = entry->id;
                break;
            }
            slot = (slot + 1u) & (st->slot_cap - 1u);
        }
    }
    pthread_mutex_unlock(&st->write_mutex);
    return found;
}

SymbolId symbol_lookup_cstr(SymbolTable *st, const char *text) {
    if (!text || !*text) return SYMBOL_ID_NONE;
    size_t length = strlen(text);
    if (length > UINT32_MAX) return SYMBOL_ID_NONE;
    uint32_t len = (uint32_t)length;
    return symbol_lookup_span_hashed(st, (const uint8_t *)text, len,
                                    symbol_hash_bytes((const uint8_t *)text, len));
}

const char *symbol_bytes(const SymbolTable *st, SymbolId id) {
    if (!st || id == SYMBOL_ID_NONE) return "";
#if CETTA_SYMBOL_READ_COVERAGE
    symbol_read_coverage_check(st, id);
#endif
    uint32_t entry_len = atomic_load_explicit(&st->entry_len, memory_order_acquire);
    if (id >= entry_len) return "";
    const SymbolEntry *entry = symbol_table_entry_ref(st, id);
    return (entry && entry->bytes) ? entry->bytes : "";
}

uint32_t symbol_len(const SymbolTable *st, SymbolId id) {
    if (!st || id == SYMBOL_ID_NONE) return 0;
#if CETTA_SYMBOL_READ_COVERAGE
    symbol_read_coverage_check(st, id);
#endif
    uint32_t entry_len = atomic_load_explicit(&st->entry_len, memory_order_acquire);
    if (id >= entry_len) return 0;
    const SymbolEntry *entry = symbol_table_entry_ref(st, id);
    return entry ? entry->len : 0;
}

int symbol_compare(const SymbolTable *st, SymbolId left, SymbolId right) {
    if (left == right)
        return 0;
    uint32_t count = st
        ? atomic_load_explicit(&st->entry_len, memory_order_acquire) : 0u;
    const SymbolEntry *a = left && left < count ? symbol_table_entry_ref(st, left) : NULL;
    const SymbolEntry *b = right && right < count ? symbol_table_entry_ref(st, right) : NULL;
    const char *a_bytes = a && a->bytes ? a->bytes : "";
    const char *b_bytes = b && b->bytes ? b->bytes : "";
    size_t a_len = a && a->bytes ? a->len : 0u;
    size_t b_len = b && b->bytes ? b->len : 0u;
    size_t shared = a_len < b_len ? a_len : b_len;
    int compared = memcmp(a_bytes, b_bytes, shared);
    if (compared)
        return compared < 0 ? -1 : 1;
    return a_len < b_len ? -1 : a_len > b_len ? 1 : 0;
}

bool symbol_id_is_builtin(SymbolId id) {
    /* Registration owns membership, including append-only extensions and
     * names interned before builtin initialization. IDs are not permissions. */
    return (symbol_flags(g_symbols, id) & CETTA_SYMBOL_FLAG_BUILTIN) != 0u;
}

bool symbol_id_is_builtin_for(SymbolId id, CettaLanguageId language) {
    uint32_t flags = symbol_flags(g_symbols, id);
    return (flags & CETTA_SYMBOL_FLAG_BUILTIN) != 0u &&
        (flags & CETTA_SYMBOL_FLAG_BUILTIN_CONSTRUCTOR) == 0u &&
        ((flags & CETTA_SYMBOL_FLAG_PETTA_ONLY_BUILTIN) == 0u ||
         language == CETTA_LANGUAGE_PETTA);
}

uint64_t symbol_hash_value(const SymbolTable *st, SymbolId id) {
    if (!st || id == SYMBOL_ID_NONE) return (uint64_t)id;
#if CETTA_SYMBOL_READ_COVERAGE
    symbol_read_coverage_check(st, id);
#endif
    uint32_t entry_len = atomic_load_explicit(&st->entry_len, memory_order_acquire);
    if (id >= entry_len) return (uint64_t)id;
    const SymbolEntry *entry = symbol_table_entry_ref(st, id);
    return entry ? entry->hash : (uint64_t)id;
}

uint32_t symbol_flags(const SymbolTable *st, SymbolId id) {
    if (!st || id == SYMBOL_ID_NONE)
        return 0u;
#if CETTA_SYMBOL_READ_COVERAGE
    symbol_read_coverage_check(st, id);
#endif
    uint32_t entry_len = atomic_load_explicit(
        &st->entry_len, memory_order_acquire);
    if (id >= entry_len)
        return 0u;
    const SymbolEntry *entry = symbol_table_entry_ref(st, id);
    return entry ? atomic_load_explicit(&entry->flags, memory_order_relaxed) : 0u;
}

uint32_t symbol_builtin_policy(const SymbolTable *st, SymbolId id) {
    if (!st || id == SYMBOL_ID_NONE)
        return 0u;
    uint32_t entry_len = atomic_load_explicit(
        &st->entry_len, memory_order_acquire);
    if (id >= entry_len)
        return 0u;
    const SymbolEntry *entry = symbol_table_entry_ref(st, id);
    return entry ? __atomic_load_n(&entry->builtin_policy, __ATOMIC_RELAXED) : 0u;
}

void symbol_set_builtin_policy(SymbolTable *st, SymbolId id, uint32_t value) {
    if (!st || id == SYMBOL_ID_NONE)
        return;
    uint32_t entry_len = atomic_load_explicit(
        &st->entry_len, memory_order_acquire);
    if (id >= entry_len)
        return;
    SymbolEntry *entry = symbol_table_entry_mut(st, id);
    if (entry)
        __atomic_store_n(&entry->builtin_policy, value, __ATOMIC_RELAXED);
}

void *symbol_canonical_atom(const SymbolTable *st, SymbolId id) {
    if (!st || id == SYMBOL_ID_NONE ||
        id >= atomic_load_explicit(&st->entry_len, memory_order_acquire))
        return NULL;
    SymbolEntry *chunk = symbol_table_entry_chunk(st, id);
    return chunk
        ? atomic_load_explicit(
              &chunk[id & SYMBOL_ENTRY_CHUNK_MASK].canonical_atom,
              memory_order_acquire)
        : NULL;
}

void *symbol_publish_canonical_atom(SymbolTable *st, SymbolId id, void *atom) {
    if (!st || !atom || id == SYMBOL_ID_NONE ||
        id >= atomic_load_explicit(&st->entry_len, memory_order_acquire))
        return NULL;
    pthread_mutex_lock(&st->write_mutex);
    SymbolEntry *chunk = symbol_table_entry_chunk(st, id);
    void *published = chunk ? atomic_load_explicit(
        &chunk[id & SYMBOL_ENTRY_CHUNK_MASK].canonical_atom,memory_order_relaxed) : NULL;
    if (!published && chunk) {
        uint32_t count=atomic_load_explicit(&st->canonical_count,memory_order_relaxed);
        if ((count % SYMBOL_CANONICAL_CHUNK_SIZE)==0u) {
            SymbolCanonicalChunk *next=symbol_calloc(1u,sizeof(*next));
            if (st->canonical_last) st->canonical_last->next=next;
            else st->canonical_first=next;
            st->canonical_last=next;
            symbol_table_note_metadata(st,sizeof(*next));
        }
        st->canonical_last->atoms[count % SYMBOL_CANONICAL_CHUNK_SIZE]=atom;
        /* Publish catalogue membership before the entry can expose the atom. */
        atomic_store_explicit(&st->canonical_count,count+1u,memory_order_release);
        atomic_store_explicit(&chunk[id & SYMBOL_ENTRY_CHUNK_MASK].canonical_atom,
            atom,memory_order_release);
        published=atom;
    }
    pthread_mutex_unlock(&st->write_mutex);
    return published;
}

uint32_t symbol_canonical_count(const SymbolTable *st) {
    return st ? atomic_load_explicit(&st->canonical_count,memory_order_acquire) : 0u;
}

bool symbol_canonical_visit(const SymbolTable *st, uint64_t instance,
        uint32_t count, SymbolCanonicalVisitor visit, void *context) {
    if (!st || !instance || symbol_table_instance_id(st)!=instance ||
        symbol_canonical_count(st)!=count || (count && !visit)) return false;
    /* canonical_count is the release/acquire publication boundary for the
     * append-only pointers below. At zero there is no publishing store to
     * synchronize with, so do not touch canonical_first. The final count
     * check still rejects a concurrent first publication. */
    if (!count)
        return symbol_table_instance_id(st)==instance &&
            symbol_canonical_count(st)==0u;
    const SymbolCanonicalChunk *chunk=st->canonical_first;
    uint32_t visited=0u;
    while (visited<count) {
        if (!chunk) return false;
        uint32_t available=count-visited;
        if (available>SYMBOL_CANONICAL_CHUNK_SIZE) available=SYMBOL_CANONICAL_CHUNK_SIZE;
        for (uint32_t i=0u;i<available;i++)
            if (!chunk->atoms[i] || !visit(chunk->atoms[i],context)) return false;
        visited+=available;
        if (visited<count) chunk=chunk->next;
    }
    return symbol_table_instance_id(st)==instance && symbol_canonical_count(st)==count;
}

bool symbol_eq_cstr(const SymbolTable *st, SymbolId id, const char *text) {
    if (!text || !st || id == SYMBOL_ID_NONE) return false;
#if CETTA_SYMBOL_READ_COVERAGE
    symbol_read_coverage_check(st, id);
#endif
    uint32_t entry_len = atomic_load_explicit(&st->entry_len, memory_order_acquire);
    if (id >= entry_len) return false;
    uint32_t len = (uint32_t)strlen(text);
    const SymbolEntry *entry = symbol_table_entry_ref(st, id);
    return entry && entry->bytes && entry->len == len &&
           memcmp(entry->bytes, text, len) == 0;
}

bool symbol_table_try_init_builtins(SymbolTable *st, BuiltinSyms *builtins) {
    int error;
    if (!st || !builtins) { errno = EINVAL; return false; }
    memset(builtins, 0, sizeof(*builtins));
    pthread_mutex_lock(&st->write_mutex);
#define CETTA_INIT_BUILTIN(field, text) do { \
    builtins->field = symbol_intern_span_hashed_unlocked(st, \
        (const uint8_t *)(text), (uint32_t)(sizeof(text) - 1u), \
        symbol_hash_bytes((const uint8_t *)(text), (uint32_t)(sizeof(text) - 1u)), \
        false); \
    if (!builtins->field) goto failed; \
} while (0);
    CETTA_BUILTIN_SYMBOLS(CETTA_INIT_BUILTIN)
#undef CETTA_INIT_BUILTIN

    /* Includes append-only extensions outside the legacy contiguous range. */
/* != has a language-specific policy (is_grounded_op): with SWI, PeTTa's
 * library owns it, including permitted redefinitions, so it is no builtin. */
#define CETTA_MARK_BUILTIN(field, text) \
    if (&builtins->field != &builtins->op_neq) \
        symbol_table_add_flags(st, builtins->field, CETTA_SYMBOL_FLAG_BUILTIN);
    CETTA_BUILTIN_SYMBOLS(CETTA_MARK_BUILTIN)
#undef CETTA_MARK_BUILTIN

#define CETTA_MARK_PETTA_ONLY(field) \
    symbol_table_add_flags(st, builtins->field, CETTA_SYMBOL_FLAG_PETTA_ONLY_BUILTIN);
    CETTA_PETTA_ONLY_BUILTIN_SYMBOL_FIELDS(CETTA_MARK_PETTA_ONLY)
#undef CETTA_MARK_PETTA_ONLY
#define CETTA_MARK_CONSTRUCTOR(field) \
    symbol_table_add_flags(st, builtins->field, CETTA_SYMBOL_FLAG_BUILTIN_CONSTRUCTOR);
    CETTA_BUILTIN_CONSTRUCTOR_SYMBOL_FIELDS(CETTA_MARK_CONSTRUCTOR)
#undef CETTA_MARK_CONSTRUCTOR

#define CETTA_MARK_STATIC_GROUNDED(field) \
    symbol_table_add_flags_initial( \
        st, builtins->field, CETTA_SYMBOL_FLAG_STATIC_GROUNDED_OP | \
            CETTA_SYMBOL_FLAG_CORE_GROUNDED_OP);
    CETTA_STATIC_GROUNDED_SYMBOL_FIELDS(CETTA_MARK_STATIC_GROUNDED)
#undef CETTA_MARK_STATIC_GROUNDED

#define CETTA_MARK_TYPE_PURE_GROUNDED(field) \
    symbol_table_add_flags_initial( \
        st, builtins->field, \
        CETTA_SYMBOL_FLAG_TYPE_PURE_GROUNDED_OP);
    CETTA_TYPE_PURE_GROUNDED_SYMBOL_FIELDS(
        CETTA_MARK_TYPE_PURE_GROUNDED)
#undef CETTA_MARK_TYPE_PURE_GROUNDED

#define CETTA_MARK_SPECULATIVE(field) \
    symbol_table_add_flags(st, builtins->field, CETTA_SYMBOL_FLAG_SPECULATIVE_OP);
    CETTA_PURE_GROUNDED_SYMBOL_FIELDS(CETTA_MARK_SPECULATIVE)
    CETTA_SPECULATIVE_SYMBOL_FIELDS(CETTA_MARK_SPECULATIVE)
#undef CETTA_MARK_SPECULATIVE
#define CETTA_MARK_PUBLIC_LIBRARY_GROUNDED(field) \
    symbol_table_add_flags_initial(st, builtins->field, \
        CETTA_SYMBOL_FLAG_STATIC_GROUNDED_OP | CETTA_SYMBOL_FLAG_LIBRARY_NATIVE_OP);
    CETTA_PUBLIC_LIBRARY_NATIVE_SYMBOL_FIELDS(CETTA_MARK_PUBLIC_LIBRARY_GROUNDED)
#undef CETTA_MARK_PUBLIC_LIBRARY_GROUNDED

    /* The library ABI reserves this prefix for native grounded operators.
       Mark builtins eagerly; later dynamically interned ABI names retain the
       name-based fallback in is_grounded_op. */
#define CETTA_MARK_LIBRARY_GROUNDED(field, text) do { \
    if (strncmp((text), "__cetta_lib_", 12u) == 0) \
        symbol_table_add_flags_initial( \
            st, builtins->field, CETTA_SYMBOL_FLAG_STATIC_GROUNDED_OP | \
                CETTA_SYMBOL_FLAG_LIBRARY_NATIVE_OP); \
} while (0);
    CETTA_BUILTIN_SYMBOLS(CETTA_MARK_LIBRARY_GROUNDED)
#undef CETTA_MARK_LIBRARY_GROUNDED
    /* Pure numerical ABI extensions need no legacy BuiltinSyms slot. Their
     * implementations still require the math library to be active. */
    const char *math_heads[]={"__cetta_lib_math_binary64_parts","__cetta_lib_math_integer_divmod"};
    for (size_t i=0;i<sizeof(math_heads)/sizeof(*math_heads);++i) {
        uint32_t length=(uint32_t)strlen(math_heads[i]);
        SymbolId id=symbol_intern_span_hashed_unlocked(st,(const uint8_t *)math_heads[i],
            length,symbol_hash_bytes((const uint8_t *)math_heads[i],length),false);
        if (!id) goto failed;
        symbol_table_add_flags(st,id,CETTA_SYMBOL_FLAG_STATIC_GROUNDED_OP |
            CETTA_SYMBOL_FLAG_LIBRARY_NATIVE_OP | CETTA_SYMBOL_FLAG_TYPE_PURE_GROUNDED_OP |
            CETTA_SYMBOL_FLAG_SPECULATIVE_OP);
    }
    pthread_mutex_unlock(&st->write_mutex);
    return true;
failed:
    error = errno;
    memset(builtins, 0, sizeof(*builtins));
    pthread_mutex_unlock(&st->write_mutex);
    errno = error;
    return false;
}

void symbol_table_init_builtins(SymbolTable *st, BuiltinSyms *builtins) {
    if (st && builtins && !symbol_table_try_init_builtins(st, builtins))
        symbol_fatal("builtin symbol initialization");
}

CettaPettaArrowMode cetta_petta_arrow_mode(SymbolId id) {
    const char *name;
    size_t len;
    size_t mode_len;

    if (id == SYMBOL_ID_NONE || !g_symbols)
        return CETTA_PETTA_ARROW_MODE_NONE;
    name = symbol_bytes(g_symbols, id);
    if (!name)
        return CETTA_PETTA_ARROW_MODE_NONE;
    len = strlen(name);
    if (len < 6u || name[0] != '-' || name[1] != '[')
        return CETTA_PETTA_ARROW_MODE_NONE;
    if (strcmp(name + len - 3u, "]->") != 0)
        return CETTA_PETTA_ARROW_MODE_NONE;
    mode_len = len - 5u;
    if (mode_len == 3u && strncmp(name + 2, "det", 3) == 0)
        return CETTA_PETTA_ARROW_MODE_DET;
    if (mode_len == 6u && strncmp(name + 2, "nondet", 6) == 0)
        return CETTA_PETTA_ARROW_MODE_NONDET;
    return CETTA_PETTA_ARROW_MODE_OTHER;
}
