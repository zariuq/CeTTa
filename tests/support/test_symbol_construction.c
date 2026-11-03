#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <stdint.h>

static bool watching;
static size_t allocation, failure = SIZE_MAX, tracked, held;
static void *owned[64];
static bool refuse(void) {
    if (watching && allocation++ == failure) { errno = ENOMEM; return true; }
    return false;
}
static void *retain(void *pointer) {
    if (pointer && watching) {
        assert(tracked < sizeof(owned) / sizeof(*owned));
        owned[tracked++] = pointer; held++;
    }
    return pointer;
}
static void *symbol_test_malloc(size_t size) {
    return refuse() ? NULL : retain(malloc(size));
}
static void *symbol_test_calloc(size_t count, size_t size) {
    return refuse() ? NULL : retain(calloc(count, size));
}
static void symbol_test_free(void *pointer) {
    for (size_t i = 0; pointer && i < tracked; ++i) if (owned[i] == pointer) {
        owned[i] = NULL; assert(held); held--;
    }
    free(pointer);
}
#define malloc symbol_test_malloc
#define calloc symbol_test_calloc
#define free symbol_test_free
#include "../../src/symbol.c"
#undef malloc
#undef calloc
#undef free

static void begin(size_t at) {
    assert(!held);
    watching = true; allocation = tracked = 0; failure = at;
}
static void clean(void) {
    watching = false;
    assert(held == 0u && "refused symbol construction retained storage");
}
static SymbolId try_name(SymbolTable *table, const char *name, uint32_t length) {
    pthread_mutex_lock(&table->write_mutex);
    SymbolId result = symbol_intern_span_hashed_unlocked(table,
        (const uint8_t *)name, length, symbol_hash_bytes((const uint8_t *)name, length));
    pthread_mutex_unlock(&table->write_mutex);
    return result;
}
typedef struct { uint64_t values[8]; unsigned count; } ThreadIdentities;
static void *mint_identities(void *pointer) {
    ThreadIdentities *ids = pointer;
    for (unsigned i = 0; i < 8; ++i) {
        SymbolTable table;
        if (symbol_table_try_init(&table)) {
            ids->values[ids->count++] = table.instance_id;
            symbol_table_free(&table);
        } else assert(errno == EOVERFLOW);
    }
    return NULL;
}
int main(void) {
    SymbolTable table;
    BuiltinSyms builtins, empty = {0};
    begin(SIZE_MAX);
    assert(symbol_table_try_init(&table));
    size_t sites = allocation;
    assert(symbol_table_try_init_builtins(&table, &builtins));
    assert(symbol_eq_cstr(&table, builtins.empty, "Empty"));
    assert(symbol_flags(&table, builtins.op_plus) & CETTA_SYMBOL_FLAG_CORE_GROUNDED_OP);
    symbol_table_free(&table);
    clean();
    for (size_t at = 0; at < sites; ++at) {
        begin(at);
        assert(!symbol_table_try_init(&table) && errno == ENOMEM);
        assert(!table.entry_chunks && !table.slots && !table.instance_id);
        clean();
    }

    /* Leave room for Empty, then refuse the next builtin's spelling chunk.
     * The table owns the successful prefix; the descriptor stays unusable. */
    begin(SIZE_MAX);
    assert(symbol_table_try_init(&table));
    symbol_intern_cstr(&table, "seed");
    char padding[8192];
    unsigned part = 0;
    while (table.spelling_left > sizeof("Empty")) {
        size_t bytes = table.spelling_left - sizeof("Empty");
        if (bytes > sizeof(padding)) bytes = sizeof(padding);
        memset(padding, 'a' + part++, bytes - 1u);
        assert(symbol_intern_bytes(&table, (const uint8_t *)padding, (uint32_t)bytes - 1u));
    }
    uint32_t before = atomic_load(&table.entry_len);
    allocation = 0; failure = 0;
    assert(!symbol_table_try_init_builtins(&table, &builtins) && errno == ENOMEM);
    assert(!memcmp(&builtins, &empty, sizeof(builtins)));
    assert(atomic_load(&table.entry_len) == before + 1u);
    assert(symbol_lookup_cstr(&table, "Empty"));
    symbol_table_free(&table);
    clean();

    assert(symbol_table_try_init(&table));
    for (unsigned i = 1; i < SYMBOL_ENTRY_CHUNK_SIZE; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "x-%u", i);
        if (((uint64_t)table.slot_used + 1u) * SYMBOL_TABLE_MAX_LOAD_DEN >
                (uint64_t)table.slot_cap * SYMBOL_TABLE_MAX_LOAD_NUM) {
            SymbolSlot *old_slots = table.slots;
            uint32_t old_count = atomic_load(&table.entry_len);
            begin(0);
            assert(!try_name(&table, name, (uint32_t)strlen(name)) && errno == ENOMEM);
            assert(table.slots == old_slots && atomic_load(&table.entry_len) == old_count);
            assert(symbol_lookup_cstr(&table, "x-1"));
            clean();
        }
        assert(symbol_intern_cstr(&table, name));
    }
    before = atomic_load(&table.entry_len);
    char long_name[8193];
    memset(long_name, 'L', sizeof(long_name) - 1u);
    long_name[sizeof(long_name) - 1u] = '\0';
    for (size_t at = 0; at < 2; ++at) {
        begin(at);
        assert(try_name(&table, long_name, sizeof(long_name) - 1u) == SYMBOL_ID_NONE);
        assert(errno == ENOMEM && atomic_load(&table.entry_len) == before);
        assert(!table.entry_chunks[before >> SYMBOL_ENTRY_CHUNK_BITS]);
        assert(symbol_lookup_cstr(&table, "x-1"));
        assert(!symbol_lookup_cstr(&table, long_name));
        clean();
    }
    begin(SIZE_MAX);
    assert(try_name(&table, long_name, sizeof(long_name) - 1u) == before);
    symbol_table_free(&table);
    clean();

    atomic_store(&g_symbol_table_instance_counter, UINT64_MAX - 1u);
    assert(symbol_table_try_init(&table) && table.instance_id == UINT64_MAX - 1u);
    symbol_table_free(&table);
    for (unsigned i = 0; i < 2; ++i) {
        assert(!symbol_table_try_init(&table) && errno == EOVERFLOW);
        assert(atomic_load(&g_symbol_table_instance_counter) == UINT64_MAX);
    }
    atomic_store(&g_symbol_table_instance_counter, UINT64_MAX - 8u);
    ThreadIdentities ids[4] = {0};
    pthread_t threads[4];
    for (unsigned i = 0; i < 4; ++i)
        assert(!pthread_create(&threads[i], NULL, mint_identities, &ids[i]));
    for (unsigned i = 0; i < 4; ++i) assert(!pthread_join(threads[i], NULL));
    uint64_t seen[8]; unsigned count = 0;
    for (unsigned i = 0; i < 4; ++i) for (unsigned j = 0; j < ids[i].count; ++j) {
        uint64_t value = ids[i].values[j];
        assert(value >= UINT64_MAX - 8u && value < UINT64_MAX && count < 8);
        for (unsigned k = 0; k < count; ++k) assert(value != seen[k]);
        seen[count++] = value;
    }
    assert(count == 8u && atomic_load(&g_symbol_table_instance_counter) == UINT64_MAX);
    puts("symbol construction: initial refusals, builtin prefix, growth/entry/spelling rollback and concurrent identity exhaustion pass");
    return 0;
}
