#include "symbol.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SYMBOL_TABLE_MIN_SLOTS 4096u
#define SYMBOL_TABLE_MAX_LOAD_NUM 7u
#define SYMBOL_TABLE_MAX_LOAD_DEN 10u

SymbolTable *g_symbols = NULL;
BuiltinSyms g_builtin_syms;
static _Atomic uint64_t g_symbol_table_instance_counter = 1u;

static void symbol_oom(size_t size) {
    fprintf(stderr, "fatal: out of memory allocating %zu bytes\n", size);
    abort();
}

static void *symbol_malloc(size_t size) {
    void *ptr = malloc(size == 0 ? 1 : size);
    if (!ptr) symbol_oom(size);
    return ptr;
}

static void *symbol_calloc(size_t count, size_t size) {
    void *ptr = calloc(count == 0 ? 1 : count, size == 0 ? 1 : size);
    if (!ptr) symbol_oom(count * size);
    return ptr;
}

static uint64_t symbol_hash_bytes(const uint8_t *bytes, uint32_t len) {
    uint64_t h = 1469598103934665603ULL;
    for (uint32_t i = 0; i < len; i++) {
        h ^= (uint64_t)bytes[i];
        h *= 1099511628211ULL;
    }
    return h ? h : 1ULL;
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

static void symbol_table_add_flags(SymbolTable *st, SymbolId id,
                                   uint32_t flags) {
    if (!st || id == SYMBOL_ID_NONE || flags == 0u)
        return;
    uint32_t entry_len = atomic_load_explicit(
        &st->entry_len, memory_order_relaxed);
    if (id >= entry_len)
        return;
    SymbolEntry *entry = symbol_table_entry_mut(st, id);
    if (entry)
        entry->flags |= flags;
}

static void symbol_table_ensure_entry_chunk(SymbolTable *st, SymbolId id) {
    uint32_t chunk_index = id >> SYMBOL_ENTRY_CHUNK_BITS;
    if (chunk_index >= SYMBOL_ENTRY_CHUNK_COUNT) {
        fprintf(stderr, "fatal: symbol table exhausted symbol id space\n");
        abort();
    }
    if (st->entry_chunks[chunk_index]) return;
    st->entry_chunks[chunk_index] =
        symbol_calloc(SYMBOL_ENTRY_CHUNK_SIZE, sizeof(SymbolEntry));
}

static bool symbol_entry_matches(const SymbolEntry *entry, const uint8_t *bytes,
                                 uint32_t len, uint64_t hash) {
    return entry && entry->bytes && entry->len == len && entry->hash == hash &&
           memcmp(entry->bytes, bytes, len) == 0;
}

static void symbol_table_resize(SymbolTable *st, uint32_t new_cap) {
    SymbolSlot *old_slots = st->slots;
    uint32_t entry_len = atomic_load_explicit(&st->entry_len, memory_order_relaxed);

    st->slots = symbol_malloc(sizeof(SymbolSlot) * new_cap);
    memset(st->slots, 0, sizeof(SymbolSlot) * new_cap);
    st->slot_cap = new_cap;
    st->slot_used = 0;

    for (uint32_t i = 1; i < entry_len; i++) {
        const SymbolEntry *entry = symbol_table_entry_ref(st, (SymbolId)i);
        if (!entry || !entry->bytes) continue;
        uint32_t slot = (uint32_t)(entry->hash % st->slot_cap);
        while (st->slots[slot].id != SYMBOL_ID_NONE) {
            slot = (slot + 1) % st->slot_cap;
        }
        st->slots[slot].hash = entry->hash;
        st->slots[slot].len = entry->len;
        st->slots[slot].id = (SymbolId)i;
        st->slot_used++;
    }

    free(old_slots);
}

void symbol_table_init(SymbolTable *st) {
    if (!st) return;
    st->instance_id = atomic_fetch_add_explicit(
        &g_symbol_table_instance_counter, 1u, memory_order_relaxed);
    if (st->instance_id == 0u) {
        fprintf(stderr, "fatal: symbol table instance id space exhausted\n");
        abort();
    }
    st->slots = NULL;
    st->slot_cap = 0;
    st->slot_used = 0;
    st->entry_chunks = symbol_calloc(SYMBOL_ENTRY_CHUNK_COUNT, sizeof(SymbolEntry *));
    atomic_init(&st->entry_len, 0);
    if (pthread_mutex_init(&st->write_mutex, NULL) != 0) {
        fprintf(stderr, "fatal: failed to initialize symbol table mutex\n");
        abort();
    }

    symbol_table_ensure_entry_chunk(st, SYMBOL_ID_NONE);
    atomic_store_explicit(&st->entry_len, 1, memory_order_relaxed);
    symbol_table_resize(st, SYMBOL_TABLE_MIN_SLOTS);
}

void symbol_table_free(SymbolTable *st) {
    if (!st) return;
    uint32_t entry_len = atomic_load_explicit(&st->entry_len, memory_order_relaxed);
    for (uint32_t i = 1; i < entry_len; i++) {
        SymbolEntry *entry = symbol_table_entry_mut(st, (SymbolId)i);
        if (!entry) continue;
        free((void *)entry->bytes);
        entry->bytes = NULL;
    }
    if (st->entry_chunks) {
        for (uint32_t i = 0; i < SYMBOL_ENTRY_CHUNK_COUNT; i++) {
            free(st->entry_chunks[i]);
        }
        free(st->entry_chunks);
    }
    free(st->slots);
    pthread_mutex_destroy(&st->write_mutex);
    st->entry_chunks = NULL;
    st->slots = NULL;
    st->instance_id = 0u;
    atomic_store_explicit(&st->entry_len, 0, memory_order_relaxed);
    st->slot_cap = 0;
    st->slot_used = 0;
}

static SymbolId symbol_intern_span_hashed_unlocked(SymbolTable *st,
                                                   const uint8_t *bytes,
                                                   uint32_t len,
                                                   uint64_t hash) {
    uint32_t entry_len;

    if (!st || !bytes || len == 0) return SYMBOL_ID_NONE;
    if ((st->slot_used + 1) * SYMBOL_TABLE_MAX_LOAD_DEN >
        st->slot_cap * SYMBOL_TABLE_MAX_LOAD_NUM) {
        symbol_table_resize(st, st->slot_cap ? st->slot_cap * 2 : SYMBOL_TABLE_MIN_SLOTS);
    }

    entry_len = atomic_load_explicit(&st->entry_len, memory_order_relaxed);
    uint32_t slot = (uint32_t)(hash % st->slot_cap);
    while (true) {
        SymbolSlot *entry = &st->slots[slot];
        if (entry->id == SYMBOL_ID_NONE) {
            char *copy = symbol_malloc((size_t)len + 1);
            memcpy(copy, bytes, len);
            copy[len] = '\0';

            SymbolId id = (SymbolId)entry_len;
            symbol_table_ensure_entry_chunk(st, id);
            SymbolEntry *sym = symbol_table_entry_mut(st, id);
            sym->bytes = copy;
            sym->len = len;
            sym->hash = hash;
            sym->flags = 0;

            entry->hash = hash;
            entry->len = len;
            entry->id = id;
            st->slot_used++;
            atomic_store_explicit(&st->entry_len, id + 1, memory_order_release);
            return id;
        }
        if (entry->hash == hash) {
            const SymbolEntry *sym = symbol_table_entry_ref(st, entry->id);
            if (symbol_entry_matches(sym, bytes, len, hash))
                return entry->id;
        }
        slot = (slot + 1) % st->slot_cap;
    }
}

SymbolId symbol_intern_span_hashed(SymbolTable *st, const uint8_t *bytes,
                                   uint32_t len, uint64_t hash) {
    SymbolId id;
    if (!st) return SYMBOL_ID_NONE;
    pthread_mutex_lock(&st->write_mutex);
    id = symbol_intern_span_hashed_unlocked(st, bytes, len, hash);
    pthread_mutex_unlock(&st->write_mutex);
    return id;
}

SymbolId symbol_intern_bytes(SymbolTable *st, const uint8_t *bytes, uint32_t len) {
    if (!st || !bytes || len == 0) return SYMBOL_ID_NONE;
    return symbol_intern_span_hashed(st, bytes, len, symbol_hash_bytes(bytes, len));
}

SymbolId symbol_intern_cstr(SymbolTable *st, const char *text) {
    if (!st || !text || !*text) return SYMBOL_ID_NONE;
    return symbol_intern_bytes(st, (const uint8_t *)text, (uint32_t)strlen(text));
}

const char *symbol_bytes(const SymbolTable *st, SymbolId id) {
    if (!st || id == SYMBOL_ID_NONE) return "";
    uint32_t entry_len = atomic_load_explicit(&st->entry_len, memory_order_acquire);
    if (id >= entry_len) return "";
    const SymbolEntry *entry = symbol_table_entry_ref(st, id);
    return (entry && entry->bytes) ? entry->bytes : "";
}

uint32_t symbol_len(const SymbolTable *st, SymbolId id) {
    if (!st || id == SYMBOL_ID_NONE) return 0;
    uint32_t entry_len = atomic_load_explicit(&st->entry_len, memory_order_acquire);
    if (id >= entry_len) return 0;
    const SymbolEntry *entry = symbol_table_entry_ref(st, id);
    return entry ? entry->len : 0;
}

bool symbol_id_is_builtin_surface(SymbolId id) {
    return id != SYMBOL_ID_NONE &&
           (id <= g_builtin_syms.native_handle ||
            id == g_builtin_syms.empty_form);
}

uint64_t symbol_hash_value(const SymbolTable *st, SymbolId id) {
    if (!st || id == SYMBOL_ID_NONE) return (uint64_t)id;
    uint32_t entry_len = atomic_load_explicit(&st->entry_len, memory_order_acquire);
    if (id >= entry_len) return (uint64_t)id;
    const SymbolEntry *entry = symbol_table_entry_ref(st, id);
    return entry ? entry->hash : (uint64_t)id;
}

uint32_t symbol_flags(const SymbolTable *st, SymbolId id) {
    if (!st || id == SYMBOL_ID_NONE)
        return 0u;
    uint32_t entry_len = atomic_load_explicit(
        &st->entry_len, memory_order_acquire);
    if (id >= entry_len)
        return 0u;
    const SymbolEntry *entry = symbol_table_entry_ref(st, id);
    return entry ? entry->flags : 0u;
}

bool symbol_eq_cstr(const SymbolTable *st, SymbolId id, const char *text) {
    if (!text || !st || id == SYMBOL_ID_NONE) return false;
    uint32_t entry_len = atomic_load_explicit(&st->entry_len, memory_order_acquire);
    if (id >= entry_len) return false;
    uint32_t len = (uint32_t)strlen(text);
    const SymbolEntry *entry = symbol_table_entry_ref(st, id);
    return entry && entry->len == len && memcmp(entry->bytes, text, len) == 0;
}

void symbol_table_init_builtins(SymbolTable *st, BuiltinSyms *builtins) {
    if (!st || !builtins) return;
#define CETTA_INIT_BUILTIN(field, text) builtins->field = symbol_intern_cstr(st, text);
    CETTA_BUILTIN_SYMBOLS(CETTA_INIT_BUILTIN)
#undef CETTA_INIT_BUILTIN

#define CETTA_MARK_STATIC_GROUNDED(field) \
    symbol_table_add_flags( \
        st, builtins->field, CETTA_SYMBOL_FLAG_STATIC_GROUNDED_OP);
    CETTA_STATIC_GROUNDED_SYMBOL_FIELDS(CETTA_MARK_STATIC_GROUNDED)
#undef CETTA_MARK_STATIC_GROUNDED

    /* The library ABI reserves this prefix for native grounded operators.
       Mark builtins eagerly; later dynamically interned ABI names retain the
       name-based fallback in is_grounded_op. */
#define CETTA_MARK_LIBRARY_GROUNDED(field, text) do { \
    if (strncmp((text), "__cetta_lib_", 12u) == 0) \
        symbol_table_add_flags( \
            st, builtins->field, CETTA_SYMBOL_FLAG_STATIC_GROUNDED_OP); \
} while (0);
    CETTA_BUILTIN_SYMBOLS(CETTA_MARK_LIBRARY_GROUNDED)
#undef CETTA_MARK_LIBRARY_GROUNDED
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
