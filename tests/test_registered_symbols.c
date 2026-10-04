#define _POSIX_C_SOURCE 200809L
#include "registered_symbols.h"
#include "registered_symbol_owner.h"

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void semantics(void) {
    RegisteredSymbols *table = registered_symbols_new();
    RegisteredSymbols *other = registered_symbols_new();
    assert(table && other);
    const uint8_t bytes[] = {'a', 0, 255, 'z'};
    RegisteredSymbol a, b, c, empty;
    RegisteredSymbol missing = {0};
    assert(!registered_symbol_find(table, bytes, sizeof(bytes), &missing));
    assert(!missing.token);
    assert(registered_symbol_acquire(table, bytes, sizeof(bytes), &a));
    assert(registered_symbol_acquire(table, bytes, sizeof(bytes), &b));
    assert(a.table == b.table && a.token == b.token);
    assert(registered_symbol_clone(table, a, &c));
    assert(!registered_symbol_clone(other, a, &empty));
    RegisteredSymbol stale = a;
    RegisteredSymbolBorrow borrow;
    assert(registered_symbol_borrow(table, a, &borrow));
    assert(borrow.length == sizeof(bytes) && !memcmp(bytes, borrow.bytes, sizeof(bytes)));
    assert(borrow.hash == symbol_hash_span(bytes, sizeof(bytes)));
    assert(registered_symbol_find(table, bytes, sizeof(bytes), &missing));
    assert(missing.token == a.token);
    assert(registered_symbol_release(table, &missing));
    assert(registered_symbol_release(table, &a));
    assert(!registered_symbol_release(table, &a));
    assert(registered_symbol_release(table, &b));
    assert(registered_symbol_release(table, &c));
    assert(!registered_symbols_destroy(table));
    assert(!memcmp(bytes, borrow.bytes, sizeof(bytes)));
    assert(registered_symbol_borrow_end(table, &borrow));
    assert(!borrow.bytes && !borrow.hold.token);
    assert(!registered_symbol_clone(table, stale, &c));
    assert(registered_symbol_acquire(table, bytes, sizeof(bytes), &c));
    assert(c.token != stale.token);
    assert(!registered_symbol_borrow(table, stale, &borrow));
    assert(registered_symbol_release(table, &c));
    assert(registered_symbol_acquire(table, NULL, 0, &empty));
    assert(registered_symbol_borrow(table, empty, &borrow));
    assert(borrow.length == 0 && borrow.bytes[0] == 0);
    assert(registered_symbol_release(table, &empty));
    assert(registered_symbol_borrow_end(table, &borrow));
    RegisteredSymbol prefix, longer;
    assert(registered_symbol_acquire(table, bytes, 1, &prefix));
    assert(registered_symbol_acquire(table, bytes, 2, &longer));
    assert(prefix.token != longer.token);
    assert(registered_symbol_release(table, &prefix));
    assert(registered_symbol_release(table, &longer));
    assert(registered_symbols_destroy(table));
    assert(registered_symbols_destroy(other));
    puts("PASS: binary keys, owned borrows, stale handles and table identity");
}

static void churn(void) {
    enum { WIDTH = 256, BYTES = 4096, ROUNDS = 128 };
    RegisteredSymbols *table = registered_symbols_new();
    assert(table);
    RegisteredSymbolStats empty, current;
    registered_symbols_stats(table, &empty);
    RegisteredSymbol retained[WIDTH];
    uint8_t bytes[BYTES];
    memset(bytes, 255, sizeof(bytes));
    for (uint32_t round = 0; round < ROUNDS; round++) {
        for (uint32_t i = 0; i < WIDTH; i++) {
            uint64_t n = (uint64_t)round * WIDTH + i;
            memcpy(bytes, &n, sizeof(n));
            assert(registered_symbol_acquire(table, bytes, sizeof(bytes), &retained[i]));
        }
        registered_symbols_stats(table, &current);
        assert(current.entries == WIDTH && current.holds == WIDTH);
        assert(current.payload_bytes == WIDTH * (BYTES + 1));
        RegisteredSymbolBorrow survivor;
        assert(registered_symbol_borrow(table, retained[WIDTH - 1], &survivor));
        for (uint32_t i = 0; i < WIDTH; i++)
            assert(registered_symbol_release(table, &retained[i]));
        uint64_t n;
        memcpy(&n, survivor.bytes, sizeof(n));
        assert(n == (uint64_t)(round + 1) * WIDTH - 1);
        assert(registered_symbol_borrow_end(table, &survivor));
        registered_symbols_stats(table, &current);
        assert(!current.entries && !current.holds && !current.payload_bytes);
        assert(current.metadata_bytes == empty.metadata_bytes);
    }
    assert(current.reclaimed_entries == WIDTH * ROUNDS);
    printf("PASS: %u unique keys reclaimed; empty metadata %llu bytes, peak %llu bytes\n",
           WIDTH * ROUNDS, (unsigned long long)current.metadata_bytes,
           (unsigned long long)current.peak_bytes);
    assert(registered_symbols_destroy(table));
}

typedef struct {
    RegisteredSymbols *table;
    RegisteredSymbol anchor;
    pthread_barrier_t *barrier;
    unsigned lane;
} Worker;

static void *worker(void *arg) {
    Worker *w = arg;
    pthread_barrier_wait(w->barrier);
    for (unsigned i = 0; i < 4000; i++) {
        RegisteredSymbol shared, unique;
        assert(registered_symbol_acquire(w->table, (const uint8_t *)"shared", 6, &shared));
        assert(shared.token == w->anchor.token && shared.table == w->anchor.table);
        uint32_t bytes[] = {w->lane, i};
        assert(registered_symbol_acquire(w->table, (uint8_t *)bytes, sizeof(bytes), &unique));
        RegisteredSymbolBorrow borrow;
        assert(registered_symbol_borrow(w->table, unique, &borrow));
        assert(registered_symbol_release(w->table, &unique));
        assert(!memcmp(borrow.bytes, bytes, sizeof(bytes)));
        assert(registered_symbol_borrow_end(w->table, &borrow));
        assert(registered_symbol_release(w->table, &shared));
    }
    return NULL;
}

static void concurrent_publication(void) {
    enum { WORKERS = 4 };
    RegisteredSymbols *table = registered_symbols_new();
    assert(table);
    RegisteredSymbol anchor;
    assert(registered_symbol_acquire(table, (const uint8_t *)"shared", 6, &anchor));
    pthread_barrier_t barrier;
    assert(!pthread_barrier_init(&barrier, NULL, WORKERS));
    pthread_t threads[WORKERS];
    Worker workers[WORKERS];
    for (unsigned i = 0; i < WORKERS; i++) {
        workers[i] = (Worker){table, anchor, &barrier, i};
        assert(!pthread_create(&threads[i], NULL, worker, &workers[i]));
    }
    for (unsigned i = 0; i < WORKERS; i++) assert(!pthread_join(threads[i], NULL));
    assert(!pthread_barrier_destroy(&barrier));
    RegisteredSymbolStats stats;
    registered_symbols_stats(table, &stats);
    assert(stats.entries == 1 && stats.holds == 1 && stats.payload_bytes == 7);
    assert(registered_symbol_release(table, &anchor));
    assert(registered_symbols_destroy(table));
    puts("PASS: concurrent canonical publication, independent keys and live borrows");
}

static void exhaustion(void) {
    RegisteredSymbols *table = registered_symbols_new();
    assert(table);
    assert(registered_symbols_set_next_token_test(table, UINT64_MAX - 1));
    RegisteredSymbol last, refused = {0};
    assert(registered_symbol_acquire(table, (const uint8_t *)"last", 4, &last));
    assert(last.token == UINT64_MAX - 1);
    assert(!registered_symbol_acquire(table, (const uint8_t *)"next", 4, &refused));
    assert(registered_symbol_release(table, &last));
    assert(!registered_symbol_acquire(table, (const uint8_t *)"next", 4, &refused));
    assert(!refused.token);
    assert(registered_symbols_destroy(table));
    puts("PASS: identity exhaustion refuses allocation without wraparound");
}

static void arena_transport(void) {
    RegisteredSymbols *table = registered_symbols_new();
    assert(table);
    Arena first, second;
    arena_init(&first);
    arena_init(&second);
    ArenaMark before = arena_mark(&second);
    const uint8_t bytes[] = {'v', 0, 255, 'x'};
    RegisteredSymbol handle;
    assert(registered_symbol_acquire(table, bytes, sizeof(bytes), &handle));
    RegisteredSymbol stale = handle;
    RegisteredSymbolView a, b, duplicate;
    assert(registered_symbol_own_in_arena(table, handle, &first, &a));
    assert(registered_symbol_view_copy(&second, &a, &b));
    assert(registered_symbol_view_copy(&second, &b, &duplicate));
    assert(registered_symbol_release(table, &handle));
    arena_free(&first);
    assert(!memcmp(b.bytes, bytes, sizeof(bytes)));
    assert(!registered_symbols_destroy(table));
    arena_reset(&second, before);
    RegisteredSymbolStats stats;
    registered_symbols_stats(table, &stats);
    assert(stats.entries == 0 && stats.holds == 0 && stats.payload_bytes == 0);
    assert(!registered_symbol_clone(table, stale, &handle));
    arena_free(&second);
    assert(registered_symbols_destroy(table));
    puts("PASS: actual arena transport, source release and mark/reset reclaim registered payloads");
}

int main(void) {
    semantics();
    churn();
    concurrent_publication();
    exhaustion();
    arena_transport();
    return 0;
}
