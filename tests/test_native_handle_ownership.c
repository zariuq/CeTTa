#include "atom.h"
#include "library.h"
#include "native_handle.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

static const char *kind = "test.owned-resource";
static _Atomic unsigned released;

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
    arena_reset(&source, source_start);
    expect_dead(ctx, id);
    assert(released == 1);

    /* Duplicate results share the resource, not a destructible unique owner. */
    id = new_resource(ctx);
    handle = cetta_native_handle_owned_atom(ctx, &source, kind, id);
    Atom *copy = atom_deep_copy(&survivor, handle);
    Atom *second = atom_deep_copy_shared(&other, handle);
    assert(atom_eq(handle, copy) && atom_eq(copy, second));
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
    assert(!cetta_native_handle_close(ctx, kind, id));
    copy = atom_deep_copy(&survivor, handle);
    assert(cetta_native_handle_arg(copy, kind, &decoded) && decoded == id);
    expect_dead(ctx, id);
    uint64_t replacement = new_resource(ctx);
    assert(replacement != id);
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

    /* Teardown before surviving arenas: callbacks must not dereference ctx. */
    id = new_resource(ctx);
    handle = cetta_native_handle_owned_atom(ctx, &source, kind, id);
    copy = atom_deep_copy(&survivor, handle);
    cetta_native_handle_cleanup_all(ctx);
    assert(released == 11);
    free(ctx);
    assert(atom_int_copy(&source, copy->expr.elems[2]));
    arena_free(&source);
    arena_free(&survivor);
    assert(released == 11);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    puts("native handle ownership: release, escape, alias, reset, teardown and concurrent-copy checks passed");
    return 0;
}
