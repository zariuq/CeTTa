#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "symbol.h"

typedef struct {
    SymbolTable *table;
    bool owner;
} Worker;

static void *intern_worker(void *context) {
    Worker *worker = context;
    for (unsigned i = 0u; i < 2000u; i++) {
        SymbolId id = worker->owner
            ? symbol_intern_cstr(worker->table, "concurrent-promotion")
            : symbol_intern_data_cstr(worker->table, "concurrent-promotion");
        assert(id != SYMBOL_ID_NONE);
        assert(symbol_eq_cstr(worker->table, id, "concurrent-promotion"));
        (void)symbol_flags(worker->table, id);
    }
    return NULL;
}

int main(void) {
    SymbolTable table;
    SymbolTableStats before, after;
    symbol_table_init(&table);
    symbol_table_stats_snapshot(&table, &before);
    assert(before.owned_count == 0u && before.data_count == 0u);
    assert(before.metadata_bytes >= sizeof(SymbolEntry) * SYMBOL_ENTRY_CHUNK_SIZE);

    SymbolId data = symbol_intern_data_cstr(&table, "temporary");
    assert(data != SYMBOL_ID_NONE);
    uint64_t hash = symbol_hash_value(&table, data);
    assert(symbol_lookup_cstr(&table, "temporary") == data);
    assert(symbol_flags(&table, data) & CETTA_SYMBOL_FLAG_RUNTIME_DATA);
    assert(symbol_intern_data_cstr(&table, "temporary") == data);
    symbol_table_stats_snapshot(&table, &after);
    assert(after.data_count == 1u && after.data_bytes == strlen("temporary") + 1u);
    assert(after.data_allocated_bytes == after.data_bytes);
    assert(after.owned_count == 0u && after.promotions == 0u);

    /* Owning the existing spelling preserves both identity and hashing. */
    assert(symbol_intern_span_hashed(&table, (const uint8_t *)"temporary",
                                     strlen("temporary"), hash) == data);
    assert(!(symbol_flags(&table, data) & CETTA_SYMBOL_FLAG_RUNTIME_DATA));
    assert(symbol_intern_data_cstr(&table, "temporary") == data);
    symbol_table_stats_snapshot(&table, &after);
    assert(after.data_count == 0u && after.data_bytes == 0u);
    assert(after.owned_count == 1u && after.promotions == 1u);
    assert(after.owned_bytes == strlen("temporary") + 1u);

    SymbolId pinned = symbol_intern_data_cstr(&table, "registered");
    assert(symbol_pin(&table, pinned));
    assert(symbol_pin(&table, pinned));
    assert(!symbol_pin(&table, SYMBOL_ID_NONE));
    assert(!symbol_pin(&table, UINT32_MAX));
    assert(symbol_intern_data_cstr(&table, "registered") == pinned);
    assert(!(symbol_flags(&table, pinned) & CETTA_SYMBOL_FLAG_RUNTIME_DATA));
    assert(symbol_intern_data_cstr(&table, "") == SYMBOL_ID_NONE);
    assert(symbol_intern_data_bytes(&table, NULL, 4u) == SYMBOL_ID_NONE);
    assert(!symbol_eq_cstr(&table, SYMBOL_ID_NONE, ""));

    /* Resizing the weak lookup index preserves both lifetime classes. */
    for (unsigned i = 0u; i < 20000u; i++) {
        char text[48];
        snprintf(text, sizeof(text), "runtime-symbol-%u", i);
        SymbolId id = symbol_intern_data_cstr(&table, text);
        assert(symbol_eq_cstr(&table, id, text));
        if (i == 2999u) {
            /* First index rebuild overlaps its 4096 old slots with the
             * new array, before another entry chunk has been allocated. */
            symbol_table_stats_snapshot(&table, &after);
            assert(after.peak_metadata_bytes >= after.metadata_bytes +
                   4096u * sizeof(SymbolSlot));
        }
    }
    assert(symbol_lookup_cstr(&table, "temporary") == data);
    assert(symbol_lookup_cstr(&table, "registered") == pinned);
    symbol_table_stats_snapshot(&table, &after);
    assert(after.data_count == 20000u);
    assert(after.owned_count == 2u && after.promotions == 2u);
    assert(after.peak_payload_bytes == after.data_bytes + after.owned_bytes);
    assert(after.metadata_bytes > before.metadata_bytes);
    assert(after.peak_metadata_bytes >= after.metadata_bytes);
    assert(after.retired_count == 0u);

    symbol_intern_data_cstr(&table, "concurrent-promotion");
    pthread_t threads[4];
    Worker workers[4];
    for (unsigned i = 0u; i < 4u; i++) {
        workers[i] = (Worker){.table = &table, .owner = i == 0u};
        assert(pthread_create(&threads[i], NULL, intern_worker, &workers[i]) == 0);
    }
    for (unsigned i = 0u; i < 4u; i++)
        assert(pthread_join(threads[i], NULL) == 0);
    symbol_table_stats_snapshot(&table, &after);
    assert(after.data_count == 20000u && after.owned_count == 3u);
    assert(after.promotions == 3u);

    /* Shadow marking is an identity on payloads and IDs, even for unmarked
     * values. Pinning after the snapshot removes an entry from candidates;
     * births after it cannot be mistaken for unreachable data. */
    SymbolId held = symbol_intern_data_cstr(&table, "held-for-shadow");
    SymbolId unmarked = symbol_intern_data_cstr(&table, "unmarked-for-shadow");
    SymbolShadowScan scan;
    SymbolShadowResult result;
    assert(symbol_shadow_begin(&table, &scan));
    assert(symbol_shadow_mark(&scan, held));
    assert(symbol_shadow_mark(&scan, held));
    assert(symbol_shadow_mark(&scan, SYMBOL_ID_NONE));
    assert(symbol_shadow_finish(&scan, &result));
    assert(result.marked_data_count == 1u);
    assert(result.marked_data_bytes == strlen("held-for-shadow") + 1u);
    assert(result.unmarked_data_count == 20001u);
    assert(!result.snapshot_outdated);
    assert(symbol_eq_cstr(&table, unmarked, "unmarked-for-shadow"));
    assert(symbol_intern_data_cstr(&table, "unmarked-for-shadow") == unmarked);
    assert(symbol_pin(&table, held));
    assert(symbol_shadow_finish(&scan, &result));
    assert(result.marked_data_count == 0u);
    SymbolId late = symbol_intern_data_cstr(&table, "late-for-shadow");
    assert(!symbol_shadow_mark(&scan, late));
    assert(symbol_shadow_finish(&scan, &result));
    assert(result.snapshot_outdated && result.late_births == 1u);
    symbol_shadow_destroy(&scan);
    symbol_shadow_destroy(&scan);

    BuiltinSyms builtins;
    symbol_table_init_builtins(&table, &builtins);
#define CHECK_OWNED(field, text) \
    assert(!(symbol_flags(&table, builtins.field) & CETTA_SYMBOL_FLAG_RUNTIME_DATA));
    CETTA_BUILTIN_SYMBOLS(CHECK_OWNED)
#undef CHECK_OWNED
    symbol_table_free(&table);
    symbol_table_init(&table);
    symbol_table_stats_snapshot(&table, &after);
    assert(after.owned_count == 0u && after.data_count == 0u);
    symbol_table_free(&table);
    puts("test_symbol_lifetimes: ok");
    return 0;
}
