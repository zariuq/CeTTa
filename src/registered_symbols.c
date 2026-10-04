#include "registered_symbols.h"

#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

enum { MIN_BUCKETS = 32 };
typedef struct RegisteredEntry RegisteredEntry;
struct RegisteredEntry {
    RegisteredEntry *spelling_next;
    RegisteredEntry *token_next;
    uint64_t token;
    uint64_t hash;
    uint64_t holds;
    uint32_t length;
    uint8_t bytes[];
};

struct RegisteredSymbols {
    pthread_mutex_t mutex;
    uint64_t identity;
    uint64_t next_token;
    RegisteredEntry **spellings;
    RegisteredEntry **tokens;
    size_t capacity;
    RegisteredSymbolStats stats;
};

static _Atomic uint64_t next_table = 1;

static uint64_t spelling_hash(const uint8_t *bytes, uint32_t length) {
    uint64_t hash = UINT64_C(1469598103934665603);
    for (uint32_t i = 0; i < length; i++) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash ? hash : 1; /* Same spelling hash as the Atom/Symbol interface. */
}

static size_t token_bucket(uint64_t token, size_t capacity) {
    token ^= token >> 30;
    token *= UINT64_C(0xbf58476d1ce4e5b9);
    token ^= token >> 27;
    token *= UINT64_C(0x94d049bb133111eb);
    return (size_t)(token ^ (token >> 31)) & (capacity - 1);
}

static void note_peak(RegisteredSymbols *table, uint64_t temporary) {
    uint64_t bytes = table->stats.payload_bytes + table->stats.metadata_bytes;
    if (temporary > UINT64_MAX - bytes) bytes = UINT64_MAX;
    else bytes += temporary;
    if (bytes > table->stats.peak_bytes) table->stats.peak_bytes = bytes;
}

/* Both indexes are weak: they contain no reference-count contribution.
 * Rehashing changes links, never entry addresses or borrowed bytes. */
static bool rehash(RegisteredSymbols *table, size_t capacity) {
    if (capacity > SIZE_MAX / (2 * sizeof(RegisteredEntry *))) return false;
    RegisteredEntry **both = calloc(capacity * 2, sizeof(*both));
    if (!both) return false;
    RegisteredEntry **tokens = both + capacity;
    note_peak(table, (uint64_t)capacity * 2 * sizeof(*both));
    for (size_t i = 0; i < table->capacity; i++) {
        RegisteredEntry *entry = table->spellings[i];
        while (entry) {
            RegisteredEntry *next = entry->spelling_next;
            size_t b = (size_t)entry->hash & (capacity - 1);
            entry->spelling_next = both[b];
            both[b] = entry;
            b = token_bucket(entry->token, capacity);
            entry->token_next = tokens[b];
            tokens[b] = entry;
            entry = next;
        }
    }
    table->stats.metadata_bytes -=
        (uint64_t)table->capacity * 2 * sizeof(*both);
    table->stats.metadata_bytes += (uint64_t)capacity * 2 * sizeof(*both);
    free(table->spellings);
    table->spellings = both;
    table->tokens = tokens;
    table->capacity = capacity;
    return true;
}

RegisteredSymbols *registered_symbols_new(void) {
    uint64_t identity = atomic_load_explicit(&next_table, memory_order_relaxed);
    do {
        if (identity == UINT64_MAX) return NULL;
    } while (!atomic_compare_exchange_weak_explicit(&next_table, &identity,
                identity + 1, memory_order_relaxed, memory_order_relaxed));
    RegisteredSymbols *table = calloc(1, sizeof(*table));
    if (!table) return NULL;
    if (pthread_mutex_init(&table->mutex, NULL)) { free(table); return NULL; }
    table->identity = identity;
    table->next_token = 1;
    table->stats.metadata_bytes = sizeof(*table);
    if (!rehash(table, MIN_BUCKETS)) {
        pthread_mutex_destroy(&table->mutex);
        free(table);
        return NULL;
    }
    note_peak(table, 0);
    return table;
}

static RegisteredEntry *find_owned(RegisteredSymbols *table, RegisteredSymbol h) {
    if (h.table != table->identity || !h.token) return NULL;
    RegisteredEntry *entry = table->tokens[token_bucket(h.token, table->capacity)];
    while (entry && entry->token != h.token) entry = entry->token_next;
    return entry;
}

static bool retain(RegisteredSymbols *table, RegisteredEntry *entry) {
    if (!entry || entry->holds == UINT64_MAX || table->stats.holds == UINT64_MAX)
        return false;
    entry->holds++;
    table->stats.holds++;
    return true;
}

bool registered_symbol_acquire(RegisteredSymbols *table, const uint8_t *bytes,
                                uint32_t length, RegisteredSymbol *out) {
    if (!table || !out || (!bytes && length)) return false;
    uint64_t hash = spelling_hash(bytes, length);
    pthread_mutex_lock(&table->mutex);
    size_t bucket = (size_t)hash & (table->capacity - 1);
    RegisteredEntry *entry = table->spellings[bucket];
    for (; entry; entry = entry->spelling_next) {
        if (entry->hash == hash && entry->length == length &&
            (!length || !memcmp(entry->bytes, bytes, length))) break;
    }
    bool ok = true;
    if (!entry) {
        if (table->next_token == UINT64_MAX ||
            table->stats.entries == UINT64_MAX ||
            table->stats.holds == UINT64_MAX ||
            (size_t)length > SIZE_MAX - sizeof(*entry) - 1) {
            ok = false;
            goto done;
        }
        if (table->stats.entries >= table->capacity &&
            (table->capacity > SIZE_MAX / 2 || !rehash(table, table->capacity * 2))) {
            ok = false;
            goto done;
        }
        entry = malloc(sizeof(*entry) + (size_t)length + 1);
        if (!entry) { ok = false; goto done; }
        entry->token = table->next_token++;
        entry->hash = hash;
        entry->length = length;
        entry->holds = 0;
        if (length) memcpy(entry->bytes, bytes, length);
        entry->bytes[length] = 0;
        bucket = (size_t)hash & (table->capacity - 1);
        entry->spelling_next = table->spellings[bucket];
        table->spellings[bucket] = entry;
        bucket = token_bucket(entry->token, table->capacity);
        entry->token_next = table->tokens[bucket];
        table->tokens[bucket] = entry;
        table->stats.entries++;
        table->stats.payload_bytes += (uint64_t)length + 1;
        table->stats.metadata_bytes += sizeof(*entry);
        note_peak(table, 0);
    }
    ok = retain(table, entry);
    if (ok) *out = (RegisteredSymbol){table->identity, entry->token};
done:
    pthread_mutex_unlock(&table->mutex);
    return ok;
}

bool registered_symbol_clone(RegisteredSymbols *table, RegisteredSymbol source,
                              RegisteredSymbol *out) {
    if (!table || !out) return false;
    pthread_mutex_lock(&table->mutex);
    bool ok = retain(table, find_owned(table, source));
    if (ok) *out = source;
    pthread_mutex_unlock(&table->mutex);
    return ok;
}

bool registered_symbol_find(RegisteredSymbols *table, const uint8_t *bytes,
                             uint32_t length, RegisteredSymbol *out) {
    if (!table || !out || (!bytes && length)) return false;
    uint64_t hash = spelling_hash(bytes, length);
    pthread_mutex_lock(&table->mutex);
    RegisteredEntry *entry = table->spellings[(size_t)hash & (table->capacity - 1)];
    while (entry && (entry->hash != hash || entry->length != length ||
           (length && memcmp(entry->bytes, bytes, length))))
        entry = entry->spelling_next;
    bool ok = retain(table, entry);
    if (ok) *out = (RegisteredSymbol){table->identity, entry->token};
    pthread_mutex_unlock(&table->mutex);
    return ok;
}

bool registered_symbol_borrow(RegisteredSymbols *table, RegisteredSymbol source,
                               RegisteredSymbolBorrow *out) {
    if (!table || !out) return false;
    pthread_mutex_lock(&table->mutex);
    RegisteredEntry *entry = find_owned(table, source);
    bool ok = retain(table, entry);
    if (ok) *out = (RegisteredSymbolBorrow){source, entry->bytes,
                                           entry->length, entry->hash};
    pthread_mutex_unlock(&table->mutex);
    return ok;
}

bool registered_symbol_release(RegisteredSymbols *table, RegisteredSymbol *owned) {
    if (!table || !owned) return false;
    pthread_mutex_lock(&table->mutex);
    RegisteredEntry *entry = find_owned(table, *owned);
    bool ok = entry != NULL;
    if (ok) {
        *owned = (RegisteredSymbol){0};
        table->stats.holds--;
        if (--entry->holds == 0) {
            RegisteredEntry **link = &table->spellings[
                (size_t)entry->hash & (table->capacity - 1)];
            while (*link != entry) link = &(*link)->spelling_next;
            *link = entry->spelling_next;
            link = &table->tokens[token_bucket(entry->token, table->capacity)];
            while (*link != entry) link = &(*link)->token_next;
            *link = entry->token_next;
            uint64_t bytes = (uint64_t)entry->length + 1;
            table->stats.payload_bytes -= bytes;
            table->stats.metadata_bytes -= sizeof(*entry);
            table->stats.entries--;
            if (table->stats.reclaimed_entries != UINT64_MAX)
                table->stats.reclaimed_entries++;
            if (bytes > UINT64_MAX - table->stats.reclaimed_bytes)
                table->stats.reclaimed_bytes = UINT64_MAX;
            else table->stats.reclaimed_bytes += bytes;
            free(entry);
            /* Shrink reserved indexes too, so metadata does not retain the
             * maximum number of keys ever present. Allocation failure keeps
             * the old index and is visible in metadata_bytes. */
            if (table->capacity > MIN_BUCKETS &&
                table->stats.entries <= table->capacity / 4)
                (void)rehash(table, table->capacity / 2);
        }
    }
    pthread_mutex_unlock(&table->mutex);
    return ok;
}

bool registered_symbol_borrow_end(RegisteredSymbols *table,
                                  RegisteredSymbolBorrow *borrow) {
    if (!borrow || !registered_symbol_release(table, &borrow->hold)) return false;
    *borrow = (RegisteredSymbolBorrow){0};
    return true;
}

void registered_symbols_stats(RegisteredSymbols *table, RegisteredSymbolStats *out) {
    if (!table || !out) return;
    pthread_mutex_lock(&table->mutex);
    *out = table->stats;
    pthread_mutex_unlock(&table->mutex);
}

bool registered_symbols_destroy(RegisteredSymbols *table) {
    if (!table) return true;
    pthread_mutex_lock(&table->mutex);
    if (table->stats.holds) { pthread_mutex_unlock(&table->mutex); return false; }
    free(table->spellings);
    pthread_mutex_unlock(&table->mutex);
    pthread_mutex_destroy(&table->mutex);
    free(table);
    return true;
}

#ifdef CETTA_TEST_HOOKS
bool registered_symbols_set_next_token_test(RegisteredSymbols *table, uint64_t token) {
    if (!table) return false;
    pthread_mutex_lock(&table->mutex);
    bool ok = !table->stats.entries && token >= table->next_token;
    if (ok) table->next_token = token;
    pthread_mutex_unlock(&table->mutex);
    return ok;
}
#endif
