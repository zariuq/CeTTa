#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SYMBOL_TABLE_MIN_SLOTS 4096u
#define SYMBOL_TABLE_MAX_LOAD_NUM 7u
#define SYMBOL_TABLE_MAX_LOAD_DEN 10u

SymbolTable *g_symbols = NULL;
BuiltinSyms g_builtin_syms;

static void symbol_oom(size_t size) {
    fprintf(stderr, "fatal: out of memory allocating %zu bytes\n", size);
    abort();
}

static void *symbol_malloc(size_t size) {
    void *ptr = malloc(size == 0 ? 1 : size);
    if (!ptr) symbol_oom(size);
    return ptr;
}

static void *symbol_realloc(void *ptr, size_t size) {
    void *out = realloc(ptr, size == 0 ? 1 : size);
    if (!out) symbol_oom(size);
    return out;
}

static uint64_t symbol_hash_bytes(const uint8_t *bytes, uint32_t len) {
    uint64_t h = 1469598103934665603ULL;
    for (uint32_t i = 0; i < len; i++) {
        h ^= (uint64_t)bytes[i];
        h *= 1099511628211ULL;
    }
    return h ? h : 1ULL;
}

static void symbol_table_grow_entries(SymbolTable *st, uint32_t needed) {
    if (needed <= st->entry_cap) return;
    uint32_t next_cap = st->entry_cap ? st->entry_cap : 64;
    while (next_cap < needed) next_cap *= 2;
    st->entries = symbol_realloc(st->entries, sizeof(SymbolEntry) * next_cap);
    st->entry_cap = next_cap;
}

static bool symbol_entry_matches(const SymbolEntry *entry, const uint8_t *bytes,
                                 uint32_t len, uint64_t hash) {
    return entry && entry->bytes && entry->len == len && entry->hash == hash &&
           memcmp(entry->bytes, bytes, len) == 0;
}

static void symbol_table_resize(SymbolTable *st, uint32_t new_cap) {
    SymbolSlot *old_slots = st->slots;
    uint32_t old_cap = st->slot_cap;

    st->slots = symbol_malloc(sizeof(SymbolSlot) * new_cap);
    memset(st->slots, 0, sizeof(SymbolSlot) * new_cap);
    st->slot_cap = new_cap;
    st->slot_used = 0;

    for (uint32_t i = 1; i < st->entry_len; i++) {
        const SymbolEntry *entry = &st->entries[i];
        if (!entry->bytes) continue;
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
    if (old_cap == 0) {
        /* nothing else to do */
    }
}

void symbol_table_init(SymbolTable *st) {
    if (!st) return;
    st->slots = NULL;
    st->slot_cap = 0;
    st->slot_used = 0;
    st->entries = NULL;
    st->entry_len = 0;
    st->entry_cap = 0;

    symbol_table_grow_entries(st, 64);
    memset(st->entries, 0, sizeof(SymbolEntry) * st->entry_cap);
    st->entry_len = 1; /* entries[0] reserved invalid */
    symbol_table_resize(st, SYMBOL_TABLE_MIN_SLOTS);
}

void symbol_table_free(SymbolTable *st) {
    if (!st) return;
    for (uint32_t i = 1; i < st->entry_len; i++) {
        free((void *)st->entries[i].bytes);
        st->entries[i].bytes = NULL;
    }
    free(st->entries);
    free(st->slots);
    st->entries = NULL;
    st->slots = NULL;
    st->entry_len = 0;
    st->entry_cap = 0;
    st->slot_cap = 0;
    st->slot_used = 0;
}

SymbolId symbol_intern_span_hashed(SymbolTable *st, const uint8_t *bytes,
                                   uint32_t len, uint64_t hash) {
    if (!st || !bytes || len == 0) return SYMBOL_ID_NONE;
    if ((st->slot_used + 1) * SYMBOL_TABLE_MAX_LOAD_DEN >
        st->slot_cap * SYMBOL_TABLE_MAX_LOAD_NUM) {
        symbol_table_resize(st, st->slot_cap ? st->slot_cap * 2 : SYMBOL_TABLE_MIN_SLOTS);
    }

    uint32_t slot = (uint32_t)(hash % st->slot_cap);
    while (true) {
        SymbolSlot *entry = &st->slots[slot];
        if (entry->id == SYMBOL_ID_NONE) {
            symbol_table_grow_entries(st, st->entry_len + 1);
            char *copy = symbol_malloc((size_t)len + 1);
            memcpy(copy, bytes, len);
            copy[len] = '\0';

            SymbolId id = (SymbolId)st->entry_len++;
            st->entries[id].bytes = copy;
            st->entries[id].len = len;
            st->entries[id].hash = hash;
            st->entries[id].flags = 0;

            entry->hash = hash;
            entry->len = len;
            entry->id = id;
            st->slot_used++;
            return id;
        }
        if (entry->hash == hash) {
            const SymbolEntry *sym = &st->entries[entry->id];
            if (symbol_entry_matches(sym, bytes, len, hash))
                return entry->id;
        }
        slot = (slot + 1) % st->slot_cap;
    }
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
    if (!st || id == SYMBOL_ID_NONE || id >= st->entry_len) return "";
    return st->entries[id].bytes ? st->entries[id].bytes : "";
}

uint32_t symbol_len(const SymbolTable *st, SymbolId id) {
    if (!st || id == SYMBOL_ID_NONE || id >= st->entry_len) return 0;
    return st->entries[id].len;
}

uint64_t symbol_hash_value(const SymbolTable *st, SymbolId id) {
    if (!st || id == SYMBOL_ID_NONE || id >= st->entry_len) return (uint64_t)id;
    return st->entries[id].hash;
}

bool symbol_eq_cstr(const SymbolTable *st, SymbolId id, const char *text) {
    if (!st || id == SYMBOL_ID_NONE || !text) return false;
    uint32_t len = (uint32_t)strlen(text);
    const SymbolEntry *entry = &st->entries[id];
    return entry->len == len && memcmp(entry->bytes, text, len) == 0;
}

void symbol_table_init_builtins(SymbolTable *st, BuiltinSyms *builtins) {
    if (!st || !builtins) return;
#define CETTA_INIT_BUILTIN(field, text) builtins->field = symbol_intern_cstr(st, text);
    CETTA_BUILTIN_SYMBOLS(CETTA_INIT_BUILTIN)
#undef CETTA_INIT_BUILTIN
}
