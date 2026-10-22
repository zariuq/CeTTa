#include "atom.h"
#include "library.h"
#include "native_handle.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *kind = "test.owned-resource";
static _Atomic unsigned released;

#ifdef CETTA_NATIVE_HANDLE_ALLOCATION_TEST
static long allocation_failure_after = -1;
static unsigned allocation_failures;
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
static bool fail_allocation(void) {
    if (allocation_failure_after < 0) return false;
    if (allocation_failure_after-- > 0) return false;
    allocation_failure_after = -1;
    ++allocation_failures;
    return true;
}
void *__wrap_malloc(size_t n) { return fail_allocation() ? NULL : __real_malloc(n); }
void *__wrap_calloc(size_t n, size_t size) { return fail_allocation() ? NULL : __real_calloc(n, size); }
void *__wrap_realloc(void *ptr, size_t n) { return fail_allocation() ? NULL : __real_realloc(ptr, n); }

static void exercise_allocation_failures(void) {
    for (long failure = 0; ; ++failure) {
        CettaLibraryContext *ctx = calloc(1, sizeof(*ctx));
        assert(ctx);
        ctx->native_handle_next_id = 1;
        uint64_t ids[129];
        int values[129];
        unsigned before = allocation_failures;
        allocation_failure_after = failure;
        for (size_t i = 0; i < 129; ++i) {
            values[i] = (int)i;
            ids[i] = UINT64_MAX;
            if (!cetta_native_handle_alloc(ctx, "fault", &values[i], NULL, &ids[i])) {
                assert(cetta_native_handle_count(ctx) == i);
                assert(ctx->native_handle_next_id == i + 1);
                assert(ids[i] == UINT64_MAX);
                for (size_t j = 0; j < i; ++j)
                    assert(cetta_native_handle_get(ctx, "fault", ids[j]) == &values[j]);
                assert(cetta_native_handle_alloc(ctx, "fault", &values[i], NULL, &ids[i]));
            }
            assert(ids[i] == i + 1);
        }
        allocation_failure_after = -1;
        for (size_t i = 0; i < 129; ++i)
            assert(cetta_native_handle_get(ctx, "fault", ids[i]) == &values[i]);
        cetta_native_handle_cleanup_all(ctx);
        free(ctx);
        if (before == allocation_failures) break;
    }
    assert(allocation_failures >= 8);
    printf("native handle registry: %u allocation failures preserve prior handles and retry\n", allocation_failures);
}
#endif

static void release_resource(void *resource) {
    atomic_fetch_add(&released, 1u);
    free(resource);
}

static uint64_t new_resource(CettaLibraryContext *ctx) {
    uint64_t id = 0;
    int *resource = malloc(sizeof(*resource));
    assert(resource);
    *resource = 42;
    assert(cetta_native_handle_alloc(ctx, kind, resource, release_resource, &id));
    return id;
}

static void expect_live(CettaLibraryContext *ctx, uint64_t id) {
    int *resource = cetta_native_handle_get(ctx, kind, id);
    assert(resource && *resource == 42);
}

static void expect_dead(CettaLibraryContext *ctx, uint64_t id) {
    assert(!cetta_native_handle_get(ctx, kind, id));
}

static void exercise_payload_accounting(void) {
    enum { payload_bytes = 65536, copies = 128 };
    unsigned before_releases = atomic_load(&released);
    CettaLibraryContext *ctx = calloc(1, sizeof(*ctx));
    assert(ctx); ctx->native_handle_next_id = 1;
    Arena source, survivor, closed_copy;
    arena_init_detached(&source); arena_init_detached(&survivor);
    arena_init_detached(&closed_copy);
    uint64_t id;
    void *payload = malloc(payload_bytes);
    assert(payload && cetta_native_handle_alloc_sized(ctx, kind, payload,
        release_resource, payload_bytes, &id));
    Atom *handle = cetta_native_handle_owned_atom(ctx, &source, kind, id);
    assert(handle && source.external_bytes >= payload_bytes);
    Atom *copy = atom_deep_copy(&survivor, handle);
    assert(copy && survivor.external_bytes >= payload_bytes);
    ArenaMark mark = arena_mark(&survivor);
    size_t charged = survivor.external_bytes;
    for (unsigned i = 0; i < copies; i++)
        assert(atom_int_copy(&survivor, handle->expr.elems[2]));
    const char *reference = getenv("CETTA_NATIVE_HANDLE_HOLDS_REFERENCE");
    if (reference && *reference && strcmp(reference, "0"))
        assert(survivor.external_bytes >= charged + (size_t)copies * payload_bytes);
    else assert(survivor.external_bytes == charged);
    arena_reset(&survivor, mark);
    arena_free(&source);
    assert(cetta_native_handle_get_owned(ctx, copy, kind) == payload);
    assert(atomic_load(&released) == before_releases);
    arena_free(&survivor);
    assert(cetta_native_handle_count(ctx) == 0 && atomic_load(&released) == before_releases + 1);

    arena_init_detached(&source);
    payload = malloc(payload_bytes);
    assert(payload && cetta_native_handle_alloc_sized(ctx, kind, payload,
        release_resource, payload_bytes, &id));
    handle = cetta_native_handle_owned_atom(ctx, &source, kind, id);
    assert(handle && cetta_native_handle_close(ctx, kind, id));
    assert(atom_deep_copy(&closed_copy, handle));
    assert(closed_copy.external_bytes < payload_bytes);
    arena_free(&source); arena_free(&closed_copy);
    cetta_native_handle_cleanup_all(ctx); free(ctx);
    assert(atomic_load(&released) == before_releases + 2);
    puts("native handle payload: one arena charge, cross-arena retention, rollback and closed-copy checks passed");
}

static void *copy_worker(void *input) {
    Atom *identifier = input;
    for (unsigned i = 0; i < 1000; ++i) {
        Arena owner;
        arena_init_detached(&owner);
        Atom *copy = atom_int_copy(&owner, identifier);
        assert(atom_eq(copy, identifier));
        arena_free(&owner);
    }
    return NULL;
}

static CettaLibraryContext *reentrant_context;
static uint64_t reentrant_id;

static void reentrant_release(void *resource) {
    release_resource(resource);
    assert(cetta_native_handle_close(reentrant_context, kind, reentrant_id));
}

static void *registry_worker(void *input) {
    CettaLibraryContext *ctx = input;
    uint64_t ids[65];
    int values[65];
    for (unsigned round = 0; round < 100; ++round) {
        for (unsigned i = 0; i < 65; ++i) {
            values[i] = (int)(round * 65 + i);
            assert(cetta_native_handle_alloc(ctx, "concurrent", &values[i], NULL, &ids[i]));
        }
        for (unsigned i = 0; i < 65; ++i)
            assert(cetta_native_handle_get(ctx, "concurrent", ids[i]) == &values[i]);
        for (unsigned parity = 0; parity < 2; ++parity)
            for (unsigned i = parity; i < 65; i += 2) {
                assert(cetta_native_handle_close(ctx, "concurrent", ids[i]));
                assert(!cetta_native_handle_get(ctx, "concurrent", ids[i]));
            }
    }
    return NULL;
}

static void exercise_concurrent_registry(void) {
    CettaLibraryContext *ctx = calloc(1, sizeof(*ctx));
    assert(ctx);
    ctx->native_handle_next_id = 1;
    pthread_t workers[4];
    for (unsigned i = 0; i < 4; ++i)
        assert(!pthread_create(&workers[i], NULL, registry_worker, ctx));
    for (unsigned i = 0; i < 4; ++i)
        assert(!pthread_join(workers[i], NULL));
    assert(cetta_native_handle_count(ctx) == 0);
    assert(ctx->native_handle_next_id == 26001);
    cetta_native_handle_cleanup_all(ctx);
    free(ctx);
    puts("native handle registry: 26000 concurrent allocations/lookups/retirements passed");
}

static void exercise_growth(void) {
    const size_t n = 4096;
    CettaLibraryContext *ctx = calloc(1, sizeof(*ctx));
    CettaLibraryContext *other_ctx = calloc(1, sizeof(*other_ctx));
    uint64_t *ids = calloc(n, sizeof(*ids));
    uint64_t *replacements = calloc(n / 2, sizeof(*replacements));
    Atom **handles = calloc(n, sizeof(*handles));
    assert(ctx && other_ctx && ids && replacements && handles);
    ctx->native_handle_next_id = other_ctx->native_handle_next_id = 1;
    Arena first, survivor;
    arena_init_detached(&first);
    arena_init_detached(&survivor);
    const char *kinds[] = {"growth.first", "growth.second", "growth.third"};
    unsigned before = released;
    for (size_t i = 0; i < n; ++i) {
        int *value = malloc(sizeof(*value));
        assert(value);
        *value = (int)i;
        assert(cetta_native_handle_alloc(ctx, kinds[i % 3], value, release_resource, &ids[i]));
        handles[i] = cetta_native_handle_owned_atom(ctx, &first, kinds[i % 3], ids[i]);
        assert(handles[i]);
        handles[i] = atom_deep_copy(&survivor, handles[i]);
        assert(cetta_native_handle_count(ctx) == i + 1);
    }
    arena_free(&first);
    for (size_t i = 0; i < n; ++i) {
        int *value = cetta_native_handle_get_owned(ctx, handles[i], kinds[i % 3]);
        assert(value && *value == (int)i);
        assert(!cetta_native_handle_get_owned(ctx, handles[i], kinds[(i + 1) % 3]));
    }
    /* The same printable id in a different context conveys no authority. */
    int *different = malloc(sizeof(*different));
    assert(different);
    uint64_t other_id;
    assert(cetta_native_handle_alloc(other_ctx, kinds[0], different, free, &other_id));
    assert(other_id == ids[0]);
    assert(cetta_native_handle_owned_atom(other_ctx, &survivor, kinds[0], other_id));
    assert(!cetta_native_handle_get_owned(other_ctx, handles[0], kinds[0]));
    cetta_native_handle_cleanup_all(other_ctx);
    free(other_ctx);
    for (size_t i = 0; i < n; i += 2) {
        assert(cetta_native_handle_close(ctx, kinds[i % 3], ids[i]));
        assert(!cetta_native_handle_close(ctx, kinds[i % 3], ids[i]));
    }
    assert(released == before + n / 2);
    for (size_t i = 0; i < n / 2; ++i) {
        replacements[i] = new_resource(ctx);
        assert(replacements[i] > ids[n - 1]);
    }
    for (size_t i = 0; i < n; ++i) {
        int *value = cetta_native_handle_get_owned(ctx, handles[i], kinds[i % 3]);
        assert(i % 2 ? value && *value == (int)i : value == NULL);
    }
    assert(cetta_native_handle_count(ctx) == n);
    size_t peak_storage = cetta_native_handle_stats(ctx).storage_bytes;
    for (size_t i = 3; i < n; i += 2)
        assert(cetta_native_handle_close(ctx, kinds[i % 3], ids[i]));
    for (size_t i = 0; i < n / 2; ++i)
        assert(cetta_native_handle_close(ctx, kind, replacements[i]));
    CettaNativeHandleStats stats = cetta_native_handle_stats(ctx);
    assert(stats.live == 1 && stats.slot_capacity == 64 && stats.index_capacity == 16);
    assert(stats.storage_bytes < peak_storage / 8);
    assert(cetta_native_handle_get_owned(ctx, handles[1], kinds[1]));
    printf("native handle registry: peak metadata=%zu, one live root metadata=%zu\n", peak_storage, stats.storage_bytes);
    cetta_native_handle_cleanup_all(ctx);
    assert(cetta_native_handle_count(ctx) == 0);
    assert(released == before + n + n / 2);
    /* Reinitializing storage must not reissue old identities or revive seals. */
    uint64_t replacement = new_resource(ctx);
    assert(replacement > ids[n - 1]);
    for (size_t i = 0; i < n; ++i)
        assert(!cetta_native_handle_get_owned(ctx, handles[i], kinds[i % 3]));
    cetta_native_handle_cleanup_all(ctx);
    free(ctx);
    arena_free(&survivor);
    free(ids);
    free(replacements);
    free(handles);
    assert(released == before + n + n / 2 + 1);
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    CettaLibraryContext *ctx = calloc(1, sizeof(*ctx));
    assert(ctx);
    ctx->native_handle_next_id = 1;

    Arena source, survivor, other;
    arena_init_detached(&source);
    arena_init_detached(&survivor);
    arena_init_detached(&other);
    ArenaMark source_start = arena_mark(&source);
    ArenaMark survivor_start = arena_mark(&survivor);

    /* No returned value: retiring the allocation scope releases once. */
    uint64_t id = new_resource(ctx);
    Atom *handle = cetta_native_handle_owned_atom(ctx, &source, kind, id);
    assert(handle);
    expect_live(ctx, id);
    assert(cetta_native_handle_get_owned(ctx, handle, kind) ==
           cetta_native_handle_get(ctx, kind, id));
    Atom *printed = cetta_native_handle_atom(&source, kind, id);
    assert(atom_eq(handle, printed));
    assert(!cetta_native_handle_get_owned(ctx, printed, kind));
    assert(!cetta_native_handle_get_owned(ctx, handle, "wrong-kind"));
    arena_reset(&source, source_start);
    expect_dead(ctx, id);
    assert(released == 1);

    /* Duplicate results share the resource, not a destructible unique owner. */
    id = new_resource(ctx);
    handle = cetta_native_handle_owned_atom(ctx, &source, kind, id);
    Atom *copy = atom_deep_copy(&survivor, handle);
    Atom *second = atom_deep_copy_shared(&other, handle);
    assert(atom_eq(handle, copy) && atom_eq(copy, second));
    assert(cetta_native_handle_get_owned(ctx, copy, kind) ==
           cetta_native_handle_get_owned(ctx, second, kind));
    assert(atom_hash(handle) == atom_hash(copy));
    assert(atom_hash(handle) == atom_hash(cetta_native_handle_atom(&source, kind, id)));
    arena_reset(&source, source_start);
    expect_live(ctx, id);
    arena_reset(&survivor, survivor_start);
    expect_live(ctx, id);
    arena_free(&other);
    expect_dead(ctx, id);
    assert(released == 2);

    /* Nested marks retire only holders created after their mark. */
    id = new_resource(ctx);
    handle = cetta_native_handle_owned_atom(ctx, &source, kind, id);
    ArenaMark nested = arena_mark(&source);
    assert(atom_deep_copy(&source, handle));
    arena_reset(&source, nested);
    expect_live(ctx, id);

    /* Rebuilding an expression from matched children preserves the identifier's
     * ownership when the reconstructed expression is subsequently evacuated. */
    Atom *rebuilt = atom_expr(&source, handle->expr.elems, handle->expr.len);
    copy = atom_deep_copy(&survivor, rebuilt);
    arena_reset(&source, source_start);
    expect_live(ctx, id);
    uint64_t decoded = 0;
    assert(cetta_native_handle_arg(copy, kind, &decoded) && decoded == id);
    assert(cetta_native_handle_get_owned(ctx, copy, kind));
    arena_reset(&survivor, survivor_start);
    expect_dead(ctx, id);
    assert(released == 3);

    /* Global interning must not erase a holder, including an isolated id. */
    HashConsTable hc;
    hashcons_init(&hc);
    arena_set_hashcons(&source, &hc);
    g_hashcons = &hc;
    id = new_resource(ctx);
    handle = cetta_native_handle_owned_atom(ctx, &source, kind, id);
    Atom *identifier = handle->expr.elems[2];
    assert(hashcons_get(&hc, identifier) == identifier);
    assert(identifier->arena_id == source.identity);
    assert(!atom_graph_is_closed_for_arena(&source, identifier));
    copy = atom_deep_copy_shared(&survivor, handle);
    assert(copy->arena_id == survivor.identity);
    arena_reset(&source, source_start);
    expect_live(ctx, id);
    arena_reset(&survivor, survivor_start);
    expect_dead(ctx, id);
    assert(released == 4);
    g_hashcons = NULL;
    arena_set_hashcons(&source, NULL);
    hashcons_free(&hc);

    /* Early close invalidates every alias, including later copies of a stale
     * value. Slot reuse must not revive the old id or double-destroy it. */
    id = new_resource(ctx);
    handle = cetta_native_handle_owned_atom(ctx, &source, kind, id);
    assert(cetta_native_handle_close(ctx, kind, id));
    assert(!cetta_native_handle_get_owned(ctx, handle, kind));
    assert(!cetta_native_handle_close(ctx, kind, id));
    copy = atom_deep_copy(&survivor, handle);
    assert(cetta_native_handle_arg(copy, kind, &decoded) && decoded == id);
    expect_dead(ctx, id);
    uint64_t replacement = new_resource(ctx);
    assert(replacement != id);
    assert(!cetta_native_handle_get_owned(ctx, copy, kind));
    arena_reset(&source, source_start);
    arena_reset(&survivor, survivor_start);
    expect_live(ctx, replacement);
    assert(released == 5);
    assert(cetta_native_handle_close(ctx, kind, replacement));

    /* Concurrent holder copies are safe; this does not authorize racing an
     * explicit close with a native operation using the borrowed resource. */
    id = new_resource(ctx);
    handle = cetta_native_handle_owned_atom(ctx, &source, kind, id);
    pthread_t workers[4];
    for (unsigned i = 0; i < 4; ++i)
        assert(!pthread_create(&workers[i], NULL, copy_worker, handle->expr.elems[2]));
    for (unsigned i = 0; i < 4; ++i)
        assert(!pthread_join(workers[i], NULL));
    expect_live(ctx, id);
    arena_reset(&source, source_start);
    expect_dead(ctx, id);
    assert(released == 7);

    /* Existing non-enrolled clients keep the context-owned contract. */
    id = new_resource(ctx);
    assert(cetta_native_handle_atom(&source, kind, id));
    arena_reset(&source, source_start);
    expect_live(ctx, id);
    assert(cetta_native_handle_close(ctx, kind, id));

    /* Destructors run outside the accounting lock and can retire dependents. */
    reentrant_context = ctx;
    reentrant_id = new_resource(ctx);
    assert(cetta_native_handle_alloc(ctx, kind, malloc(1), reentrant_release, &id));
    assert(cetta_native_handle_owned_atom(ctx, &source, kind, id));
    arena_reset(&source, source_start);
    expect_dead(ctx, reentrant_id);
    assert(released == 10);

    /* Grow while old holders are copied on other threads: new segments and a
     * larger index must not invalidate a previously published value. */
    enum { growth_count = 257 };
    uint64_t growing[growth_count];
    growing[0] = new_resource(ctx);
    handle = cetta_native_handle_owned_atom(ctx, &source, kind, growing[0]);
    copy = atom_deep_copy(&survivor, handle);
    for (unsigned i = 0; i < 4; ++i)
        assert(!pthread_create(&workers[i], NULL, copy_worker, handle->expr.elems[2]));
    for (unsigned i = 1; i < growth_count; ++i) {
        growing[i] = new_resource(ctx);
        assert(cetta_native_handle_owned_atom(ctx, &source, kind, growing[i]));
    }
    for (unsigned i = 0; i < 4; ++i) assert(!pthread_join(workers[i], NULL));
    for (unsigned i = 0; i < growth_count; ++i) expect_live(ctx, growing[i]);
    arena_reset(&source, source_start);
    expect_live(ctx, growing[0]);
    for (unsigned i = 1; i < growth_count; ++i) expect_dead(ctx, growing[i]);
    assert(released == 10 + growth_count - 1);
    arena_reset(&survivor, survivor_start);
    expect_dead(ctx, growing[0]);
    assert(released == 10 + growth_count);

    /* Teardown before surviving arenas: callbacks must not dereference ctx. */
    id = new_resource(ctx);
    handle = cetta_native_handle_owned_atom(ctx, &source, kind, id);
    copy = atom_deep_copy(&survivor, handle);
    cetta_native_handle_cleanup_all(ctx);
    assert(released == 11 + growth_count);
    free(ctx);
    assert(atom_int_copy(&source, copy->expr.elems[2]));
    arena_free(&source);
    arena_free(&survivor);
    assert(released == 11 + growth_count);
    exercise_concurrent_registry();
    exercise_growth();
#ifdef CETTA_NATIVE_HANDLE_ALLOCATION_TEST
    exercise_allocation_failures();
#endif
    exercise_payload_accounting();
    symbol_table_free(&symbols);
    g_symbols = NULL;
    puts("native handle ownership: release, escape, alias, reset, teardown and concurrent-copy checks passed");
    return 0;
}
