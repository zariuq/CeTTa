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

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    unsigned phase;
} UseGate;

static void use_gate_init(UseGate *gate) {
    *gate = (UseGate){0};
    assert(!pthread_mutex_init(&gate->mutex, NULL));
    assert(!pthread_cond_init(&gate->changed, NULL));
}

static void use_gate_set(UseGate *gate, unsigned phase) {
    assert(!pthread_mutex_lock(&gate->mutex));
    assert(phase > gate->phase);
    gate->phase = phase;
    assert(!pthread_cond_broadcast(&gate->changed));
    assert(!pthread_mutex_unlock(&gate->mutex));
}

static void use_gate_wait(UseGate *gate, unsigned phase) {
    assert(!pthread_mutex_lock(&gate->mutex));
    while (gate->phase < phase)
        assert(!pthread_cond_wait(&gate->changed, &gate->mutex));
    assert(!pthread_mutex_unlock(&gate->mutex));
}

static void use_gate_free(UseGate *gate) {
    assert(!pthread_cond_destroy(&gate->changed));
    assert(!pthread_mutex_destroy(&gate->mutex));
}

typedef struct {
    CettaLibraryContext *context;
    Atom *holder;
    UseGate gate;
    bool close_first;
    CettaNativeHandleUseMode mode;
} UseWorker;

static void *use_worker(void *raw) {
    UseWorker *worker = raw;
    CettaNativeHandleUse use = {0};
    unsigned char empty[sizeof(use)];
    memcpy(empty, &use, sizeof(use));
    if (worker->close_first) {
        use_gate_set(&worker->gate, 1u);
        use_gate_wait(&worker->gate, 2u);
        assert(!cetta_native_handle_acquire_owned(worker->context, kind,
            worker->holder, worker->mode, &use));
        assert(!memcmp(empty, &use, sizeof(use)));
        use_gate_set(&worker->gate, 3u);
        return NULL;
    }
    assert(cetta_native_handle_acquire_owned(worker->context, kind,
        worker->holder, worker->mode, &use));
    assert(cetta_native_handle_use_is_current(&use));
    assert(*(int *)cetta_native_handle_use_resource(&use) == 42);
    use_gate_set(&worker->gate, 1u);
    use_gate_wait(&worker->gate, 2u);
    assert(!cetta_native_handle_use_is_current(&use));
    int *resource = cetta_native_handle_use_resource(&use);
    assert(resource && *resource == 42);
    if (worker->mode == CETTA_NATIVE_HANDLE_USE_EXCLUSIVE)
        *resource = 43;
    assert(cetta_native_handle_use_release(&use));
    assert(!cetta_native_handle_use_resource(&use));
    assert(!cetta_native_handle_use_release(&use));
    use_gate_set(&worker->gate, 3u);
    return NULL;
}

static CettaLibraryContext *use_context(void) {
    CettaLibraryContext *context = calloc(1u, sizeof(*context));
    assert(context);
    context->native_handle_next_id = 1u;
    return context;
}

static void use_order_case(bool close_first, bool reset_holder) {
    assert(!close_first || !reset_holder);
    unsigned before = atomic_load(&released);
    CettaLibraryContext *context = use_context();
    Arena arena;
    arena_init_detached(&arena);
    ArenaMark mark = arena_mark(&arena);
    uint64_t id = new_resource(context);
    Atom *holder = cetta_native_handle_owned_atom(context, &arena, kind, id);
    assert(holder);
    UseWorker worker = {.context=context, .holder=holder, .close_first=close_first,
                        .mode=CETTA_NATIVE_HANDLE_USE_EXCLUSIVE};
    use_gate_init(&worker.gate);
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, use_worker, &worker));
    use_gate_wait(&worker.gate, 1u);
    uint64_t replacement = 0u;
    if (reset_holder) {
        arena_reset(&arena, mark);
        expect_dead(context, id);
        assert(atomic_load(&released) == before);
        replacement = new_resource(context);
        assert(replacement != id);
        expect_live(context, replacement);
    } else {
        assert(cetta_native_handle_close(context, kind, id));
        expect_dead(context, id);
        assert(!cetta_native_handle_get_owned(context, kind, holder));
        assert(!cetta_native_handle_owned_atom(context, &arena, kind, id));
        assert(!cetta_native_handle_close(context, kind, id));
        assert(atomic_load(&released) == before + (close_first ? 1u : 0u));
        CettaNativeHandleUse denied = {0};
        assert(!cetta_native_handle_acquire_owned(context, kind, holder,
            CETTA_NATIVE_HANDLE_USE_LIFETIME, &denied));
    }
    use_gate_set(&worker.gate, 2u);
    use_gate_wait(&worker.gate, 3u);
    assert(!pthread_join(thread, NULL));
    assert(atomic_load(&released) == before + 1u);
    if (replacement) {
        expect_live(context, replacement);
        assert(cetta_native_handle_close(context, kind, replacement));
        assert(atomic_load(&released) == before + 2u);
    }
    use_gate_free(&worker.gate);
    arena_free(&arena);
    cetta_native_handle_cleanup_all(context);
    free(context);
    puts(reset_holder ? "acquired use: last holder reset and slot reuse passed" :
         close_first ? "acquired use: close before acquisition passed" :
                       "acquired use: acquisition before close passed");
}

static void use_overlap_case(void) {
    unsigned before = atomic_load(&released);
    CettaLibraryContext *context = use_context();
    Arena arena;
    arena_init_detached(&arena);
    uint64_t id = new_resource(context);
    Atom *holder = cetta_native_handle_owned_atom(context, &arena, kind, id);
    UseWorker workers[2] = {{.context=context,.holder=holder},
                            {.context=context,.holder=holder}};
    pthread_t threads[2];
    for (unsigned i=0u; i<2u; ++i) {
        use_gate_init(&workers[i].gate);
        assert(!pthread_create(&threads[i], NULL, use_worker, &workers[i]));
    }
    for (unsigned i=0u; i<2u; ++i) use_gate_wait(&workers[i].gate, 1u);
    assert(cetta_native_handle_close(context, kind, id));
    arena_free(&arena);
    assert(atomic_load(&released) == before);
    use_gate_set(&workers[0].gate, 2u);
    use_gate_wait(&workers[0].gate, 3u);
    assert(!pthread_join(threads[0], NULL));
    assert(atomic_load(&released) == before);
    use_gate_set(&workers[1].gate, 2u);
    use_gate_wait(&workers[1].gate, 3u);
    assert(!pthread_join(threads[1], NULL));
    assert(atomic_load(&released) == before + 1u);
    for (unsigned i=0u; i<2u; ++i) use_gate_free(&workers[i].gate);
    cetta_native_handle_cleanup_all(context);
    free(context);
    puts("acquired use: two live thread holds and last release passed");
}

static void use_authentication_case(void) {
    unsigned before = atomic_load(&released);
    CettaLibraryContext *left = use_context(), *right = use_context();
    Arena arena;
    arena_init_detached(&arena);
    uint64_t a = new_resource(left), b = new_resource(right);
    assert(a == b);
    Atom *holder = cetta_native_handle_owned_atom(left, &arena, kind, a);
    Atom *other = cetta_native_handle_owned_atom(right, &arena, kind, b);
    Atom *textual = cetta_native_handle_atom(&arena, kind, a);
    CettaNativeHandleUse use = {0};
    unsigned char empty[sizeof(use)]; memcpy(empty, &use, sizeof(use));
    assert(!cetta_native_handle_acquire_owned(left, kind, other,
        CETTA_NATIVE_HANDLE_USE_LIFETIME, &use));
    assert(!cetta_native_handle_acquire_owned(right, kind, holder,
        CETTA_NATIVE_HANDLE_USE_LIFETIME, &use));
    assert(!cetta_native_handle_acquire_owned(left, "different-kind", holder,
        CETTA_NATIVE_HANDLE_USE_LIFETIME, &use));
    assert(!cetta_native_handle_acquire_owned(left, kind, textual,
        CETTA_NATIVE_HANDLE_USE_LIFETIME, &use));
    assert(!cetta_native_handle_acquire_owned(left, kind, holder,
        (CettaNativeHandleUseMode)99, &use));
    assert(!cetta_native_handle_acquire_owned(NULL, kind, holder,
        CETTA_NATIVE_HANDLE_USE_LIFETIME, &use));
    assert(!memcmp(empty, &use, sizeof(use)));
    assert(cetta_native_handle_acquire_owned(left, kind, holder,
        CETTA_NATIVE_HANDLE_USE_LIFETIME, &use));
    unsigned char active[sizeof(use)]; memcpy(active, &use, sizeof(use));
    assert(!cetta_native_handle_acquire_owned(left, kind, holder,
        CETTA_NATIVE_HANDLE_USE_EXCLUSIVE, &use));
    assert(!memcmp(active, &use, sizeof(use)));
    CettaNativeHandleUse copied = use;
    assert(!cetta_native_handle_use_resource(&copied));
    assert(!cetta_native_handle_use_is_current(&copied));
    assert(!cetta_native_handle_use_release(&copied));
    assert(cetta_native_handle_close(left, kind, a));
    assert(!cetta_native_handle_use_is_current(&use));
    assert(cetta_native_handle_use_release(&use));
    assert(atomic_load(&released) == before + 1u);
    assert(!cetta_native_handle_acquire_owned(left, kind, holder,
        CETTA_NATIVE_HANDLE_USE_LIFETIME, &use));
    assert(cetta_native_handle_close(right, kind, b));
    arena_free(&arena);
    /* Both real owners are retired here; copied.self still names the real
     * use's address, so these calls must reject before reading copied.owner. */
    assert(!cetta_native_handle_use_resource(&copied));
    assert(!cetta_native_handle_use_release(&copied));
    CettaStorageCensus census = {0};
    cetta_native_handle_use_storage(&copied, &census);
    assert(census.incomplete && !strcmp(census.incomplete, "InvalidNativeHandleUse"));
    cetta_storage_census_free(&census);
    assert(atomic_load(&released) == before + 2u);
    cetta_native_handle_cleanup_all(left); cetta_native_handle_cleanup_all(right);
    free(left); free(right);
    puts("acquired use: exact owner, copied receipt and retired owner refusal passed");
}

static void use_exclusive_move_case(void) {
    unsigned before = atomic_load(&released);
    CettaLibraryContext *context = use_context();
    Arena arena;
    arena_init_detached(&arena);
    uint64_t id = new_resource(context);
    Atom *holder = cetta_native_handle_owned_atom(context, &arena, kind, id);
    CettaNativeHandleUse tail = {0}, middle = {0}, head = {0}, denied = {0};
    assert(cetta_native_handle_acquire_owned(context, kind, holder,
        CETTA_NATIVE_HANDLE_USE_LIFETIME, &tail));
    assert(cetta_native_handle_acquire_owned(context, kind, holder,
        CETTA_NATIVE_HANDLE_USE_EXCLUSIVE, &middle));
    assert(cetta_native_handle_acquire_owned(context, kind, holder,
        CETTA_NATIVE_HANDLE_USE_LIFETIME, &head));
    unsigned char empty[sizeof(denied)]; memcpy(empty, &denied, sizeof(denied));
    assert(!cetta_native_handle_acquire_owned(context, kind, holder,
        CETTA_NATIVE_HANDLE_USE_EXCLUSIVE, &denied));
    assert(!memcmp(empty, &denied, sizeof(denied)));
    CettaNativeHandleUse moved_middle = {0}, moved_head = {0}, moved_tail = {0};
    assert(!cetta_native_handle_use_move(&tail, &middle));
    assert(!cetta_native_handle_use_move(&middle, &middle));
    assert(cetta_native_handle_use_move(&moved_middle, &middle));
    assert(!cetta_native_handle_use_resource(&middle));
    assert(!cetta_native_handle_acquire_owned(context, kind, holder,
        CETTA_NATIVE_HANDLE_USE_EXCLUSIVE, &denied));
    assert(cetta_native_handle_use_move(&moved_head, &head));
    assert(cetta_native_handle_use_move(&moved_tail, &tail));
    assert(cetta_native_handle_use_is_current(&moved_middle));
    assert(cetta_native_handle_use_release(&moved_middle));
    assert(cetta_native_handle_acquire_owned(context, kind, holder,
        CETTA_NATIVE_HANDLE_USE_EXCLUSIVE, &denied));
    assert(cetta_native_handle_close(context, kind, id));
    CettaNativeHandleUse moved_closed = {0};
    assert(cetta_native_handle_use_move(&moved_closed, &denied));
    assert(!cetta_native_handle_use_is_current(&moved_closed));
    assert(!cetta_native_handle_acquire_owned(context, kind, holder,
        CETTA_NATIVE_HANDLE_USE_EXCLUSIVE, &denied));
    assert(cetta_native_handle_use_release(&moved_head));
    assert(cetta_native_handle_use_release(&moved_tail));
    assert(atomic_load(&released) == before);
    assert(*(int *)cetta_native_handle_use_resource(&moved_closed) == 42);
    assert(cetta_native_handle_use_release(&moved_closed));
    assert(atomic_load(&released) == before + 1u);
    assert(!cetta_native_handle_use_release(&denied));
    arena_free(&arena); cetta_native_handle_cleanup_all(context); free(context);
    puts("acquired use: exclusive admission and head/middle/tail moves passed");
}

static Atom *use_reentrant_holder;
static CettaNativeHandleUse *use_reentrant_record;

static void use_reentrant_release(void *resource) {
    assert(!cetta_native_handle_use_resource(use_reentrant_record));
    CettaNativeHandleUse nested = {0};
    assert(cetta_native_handle_acquire_owned(reentrant_context, kind,
        use_reentrant_holder, CETTA_NATIVE_HANDLE_USE_EXCLUSIVE, &nested));
    assert(cetta_native_handle_close(reentrant_context, kind, reentrant_id));
    assert(*(int *)cetta_native_handle_use_resource(&nested) == 42);
    assert(cetta_native_handle_use_release(&nested));
    release_resource(resource);
}

static void use_reentrant_case(void) {
    unsigned before = atomic_load(&released);
    CettaLibraryContext *context = use_context();
    Arena arena;
    arena_init_detached(&arena);
    reentrant_context = context;
    reentrant_id = new_resource(context);
    use_reentrant_holder = cetta_native_handle_owned_atom(context, &arena, kind, reentrant_id);
    int *resource = malloc(sizeof(*resource)); assert(resource); *resource = 42;
    uint64_t id = 0u;
    assert(cetta_native_handle_alloc(context, kind, resource, use_reentrant_release, &id));
    Atom *holder = cetta_native_handle_owned_atom(context, &arena, kind, id);
    CettaNativeHandleUse use = {0}; use_reentrant_record = &use;
    assert(cetta_native_handle_acquire_owned(context, kind, holder,
        CETTA_NATIVE_HANDLE_USE_EXCLUSIVE, &use));
    assert(cetta_native_handle_close(context, kind, id));
    assert(atomic_load(&released) == before);
    assert(cetta_native_handle_use_release(&use));
    assert(atomic_load(&released) == before + 2u);
    use_reentrant_record = NULL; use_reentrant_holder = NULL; reentrant_context = NULL;
    arena_free(&arena); cetta_native_handle_cleanup_all(context); free(context);
    puts("acquired use: finalizer extraction and reentrant acquisition passed");
}

static void use_payload_storage(const void *resource, CettaStorageCensus *census) {
    cetta_storage_object(census, resource, sizeof(int), CETTA_STORAGE_BLOCK);
}

static void use_storage_case(void) {
    unsigned before = atomic_load(&released);
    CettaLibraryContext *context = use_context();
    Arena arena;
    arena_init_detached(&arena);
    uint64_t id = new_resource(context);
    assert(cetta_native_handle_set_storage(context, kind, id, use_payload_storage));
    Atom *holder = cetta_native_handle_owned_atom(context, &arena, kind, id);
    CettaNativeHandleUse a = {0}, b = {0};
    assert(cetta_native_handle_acquire_owned(context, kind, holder,
        CETTA_NATIVE_HANDLE_USE_LIFETIME, &a));
    assert(cetta_native_handle_acquire_owned(context, kind, holder,
        CETTA_NATIVE_HANDLE_USE_LIFETIME, &b));
    CettaStorageCensus census = {0};
    cetta_native_handle_use_storage(&a, &census); cetta_storage_finish(&census);
    assert(!census.incomplete && !census.overflow && !census.invalidated);
    assert(census.length == 2u && census.retained > sizeof(int));
    size_t held = census.retained;
    cetta_native_handle_use_storage(&b, &census); cetta_storage_finish(&census);
    assert(census.length == 2u && census.retained == held && census.repeated > 0u);
    cetta_storage_census_free(&census);
    arena_storage_census(&arena, &census); cetta_storage_finish(&census);
    assert(!census.incomplete && !census.overflow && !census.invalidated);
    size_t through_holder = census.retained;
    cetta_native_handle_use_storage(&a, &census); cetta_storage_finish(&census);
    assert(census.retained == through_holder && !census.incomplete);
    cetta_storage_census_free(&census);
    assert(cetta_native_handle_close(context, kind, id)); arena_free(&arena);
    cetta_native_handle_use_storage(&a, &census); cetta_storage_finish(&census);
    assert(!census.incomplete && !census.overflow && !census.invalidated);
    assert(census.length == 2u && census.retained == held);
    cetta_storage_census_free(&census);
    assert(cetta_native_handle_use_release(&a));
    assert(atomic_load(&released) == before);
    cetta_native_handle_use_storage(&b, &census); cetta_storage_finish(&census);
    assert(census.retained == held && !census.incomplete);
    cetta_storage_census_free(&census);
    assert(cetta_native_handle_use_release(&b));
    cetta_native_handle_use_storage(&b, &census);
    assert(!census.length && !census.retained && !census.incomplete);
    cetta_storage_census_free(&census);
    assert(atomic_load(&released) == before + 1u);
    cetta_native_handle_cleanup_all(context); free(context);
    printf("acquired use: complete shared-owner census passed; retained edge bytes=%zu use-record bytes=%zu\n",
        held, sizeof(CettaNativeHandleUse));
}

static void acquired_use_cases(void) {
    size_t count = 0u;
    assert(cetta_native_handle_test_reserve_use_count(&count) && count == 1u);
    count = SIZE_MAX - 1u;
    assert(cetta_native_handle_test_reserve_use_count(&count) && count == SIZE_MAX);
    assert(!cetta_native_handle_test_reserve_use_count(&count) && count == SIZE_MAX);
    puts("acquired use: production count reservation has no wrap");
    use_order_case(false, false);
    use_order_case(true, false);
    use_order_case(false, true);
    use_overlap_case();
    use_authentication_case();
    use_exclusive_move_case();
    use_reentrant_case();
    use_storage_case();
}

static void use_demote_case(void) {
    unsigned before = atomic_load(&released);
    CettaLibraryContext *context = use_context();
    Arena arena;
    arena_init_detached(&arena);
    uint64_t id = new_resource(context);
    Atom *holder = cetta_native_handle_owned_atom(context, &arena, kind, id);
    CettaNativeHandleUse source = {0}, forked = {0}, exclusive = {0};
    assert(cetta_native_handle_acquire_owned(context, kind, holder,
        CETTA_NATIVE_HANDLE_USE_EXCLUSIVE, &source));
    assert(cetta_native_handle_use_fork_lifetime(&forked, &source));
    assert(forked.mode == CETTA_NATIVE_HANDLE_USE_LIFETIME);
    assert(!cetta_native_handle_acquire_owned(context, kind, holder,
        CETTA_NATIVE_HANDLE_USE_EXCLUSIVE, &exclusive));
    void *resource = cetta_native_handle_use_resource(&source);
    CettaNativeHandleUse old = source;
    assert(cetta_native_handle_use_demote(&source));
    assert(source.self == old.self && source.owner == old.owner &&
        source.previous == old.previous && source.next == old.next &&
        source.resource == old.resource && source.mode == CETTA_NATIVE_HANDLE_USE_LIFETIME);
    assert(cetta_native_handle_use_is_current(&source));
    assert(cetta_native_handle_use_resource(&source) == resource);
    assert(cetta_native_handle_acquire_owned(context, kind, holder,
        CETTA_NATIVE_HANDLE_USE_EXCLUSIVE, &exclusive));
    assert(cetta_native_handle_close(context, kind, id));
    arena_free(&arena);
    assert(!cetta_native_handle_use_is_current(&source));
    assert(cetta_native_handle_use_release(&source));
    assert(cetta_native_handle_use_release(&forked));
    assert(atomic_load(&released) == before);
    assert(cetta_native_handle_use_resource(&exclusive) == resource);
    assert(cetta_native_handle_use_release(&exclusive));
    assert(atomic_load(&released) == before + 1u);
    cetta_native_handle_cleanup_all(context); free(context);
    puts("acquired use: demotion releases exclusive admission without losing lifetime");
}

static void use_detached_fork_case(void) {
    unsigned before = atomic_load(&released);
    CettaLibraryContext *context = use_context();
    Arena arena;
    arena_init_detached(&arena);
    uint64_t id = new_resource(context);
    Atom *holder = cetta_native_handle_owned_atom(context, &arena, kind, id);
    CettaNativeHandleUse source = {0}, first = {0}, second = {0}, denied = {0};
    assert(cetta_native_handle_acquire_owned(context, kind, holder,
        CETTA_NATIVE_HANDLE_USE_EXCLUSIVE, &source));
    assert(cetta_native_handle_close(context, kind, id));
    assert(cetta_native_handle_use_demote(&source));
    assert(cetta_native_handle_use_fork_lifetime(&first, &source));
    assert(cetta_native_handle_use_fork_lifetime(&second, &first));
    assert(!cetta_native_handle_use_is_current(&source));
    assert(!cetta_native_handle_use_is_current(&first));
    assert(!cetta_native_handle_use_is_current(&second));
    assert(!cetta_native_handle_get_owned(context, kind, holder));
    assert(!cetta_native_handle_acquire_owned(context, kind, holder,
        CETTA_NATIVE_HANDLE_USE_LIFETIME, &denied));
    assert(!cetta_native_handle_acquire_owned(context, kind, holder,
        CETTA_NATIVE_HANDLE_USE_EXCLUSIVE, &denied));
    arena_free(&arena);
    assert(cetta_native_handle_use_release(&source));
    assert(cetta_native_handle_use_release(&first));
    assert(atomic_load(&released) == before);
    assert(*(int *)cetta_native_handle_use_resource(&second) == 42);
    assert(cetta_native_handle_use_release(&second));
    assert(atomic_load(&released) == before + 1u);
    cetta_native_handle_cleanup_all(context); free(context);
    puts("acquired use: detached demotion and lifetime fork do not renew currency");
}

typedef struct {
    const CettaNativeHandleUse *source;
    UseGate gate;
} UseForkWorker;

static void *use_fork_worker(void *raw) {
    UseForkWorker *worker = raw;
    CettaNativeHandleUse forked = {0};
    use_gate_wait(&worker->gate, 1u);
    assert(cetta_native_handle_use_fork_lifetime(&forked, worker->source));
    assert(forked.mode == CETTA_NATIVE_HANDLE_USE_LIFETIME);
    assert(!cetta_native_handle_use_is_current(&forked));
    use_gate_set(&worker->gate, 2u);
    use_gate_wait(&worker->gate, 3u);
    assert(*(int *)cetta_native_handle_use_resource(&forked) == 42);
    assert(cetta_native_handle_use_release(&forked));
    use_gate_set(&worker->gate, 4u);
    return NULL;
}

static void use_thread_fork_case(void) {
    unsigned before = atomic_load(&released);
    CettaLibraryContext *context = use_context();
    Arena arena;
    arena_init_detached(&arena);
    uint64_t id = new_resource(context);
    Atom *holder = cetta_native_handle_owned_atom(context, &arena, kind, id);
    CettaNativeHandleUse source = {0};
    assert(cetta_native_handle_acquire_owned(context, kind, holder,
        CETTA_NATIVE_HANDLE_USE_EXCLUSIVE, &source));
    UseForkWorker workers[2] = {{.source=&source}, {.source=&source}};
    pthread_t threads[2];
    for (unsigned i = 0u; i < 2u; ++i) {
        use_gate_init(&workers[i].gate);
        assert(!pthread_create(&threads[i], NULL, use_fork_worker, &workers[i]));
    }
    assert(cetta_native_handle_close(context, kind, id));
    arena_free(&arena);
    for (unsigned i = 0u; i < 2u; ++i) use_gate_set(&workers[i].gate, 1u);
    for (unsigned i = 0u; i < 2u; ++i) use_gate_wait(&workers[i].gate, 2u);
    assert(cetta_native_handle_use_release(&source));
    assert(atomic_load(&released) == before);
    use_gate_set(&workers[0].gate, 3u);
    use_gate_wait(&workers[0].gate, 4u);
    assert(!pthread_join(threads[0], NULL));
    assert(atomic_load(&released) == before);
    use_gate_set(&workers[1].gate, 3u);
    use_gate_wait(&workers[1].gate, 4u);
    assert(!pthread_join(threads[1], NULL));
    assert(atomic_load(&released) == before + 1u);
    for (unsigned i = 0u; i < 2u; ++i) use_gate_free(&workers[i].gate);
    cetta_native_handle_cleanup_all(context); free(context);
    puts("acquired use: two thread forks preserve detached payload until last release");
}

static void use_fork_refusal_case(void) {
    unsigned before = atomic_load(&released);
    CettaLibraryContext *left = use_context(), *right = use_context();
    Arena arena;
    arena_init_detached(&arena);
    uint64_t left_id = new_resource(left), right_id = new_resource(right);
    assert(left_id == right_id);
    Atom *left_holder = cetta_native_handle_owned_atom(left, &arena, kind, left_id);
    Atom *right_holder = cetta_native_handle_owned_atom(right, &arena, kind, right_id);
    CettaNativeHandleUse source = {0}, other = {0}, empty = {0};
    assert(cetta_native_handle_acquire_owned(left, kind, left_holder,
        CETTA_NATIVE_HANDLE_USE_EXCLUSIVE, &source));
    assert(cetta_native_handle_acquire_owned(right, kind, right_holder,
        CETTA_NATIVE_HANDLE_USE_LIFETIME, &other));
    CettaNativeHandleUse copied = source;
    unsigned char source_bytes[sizeof(source)], other_bytes[sizeof(other)],
                  empty_bytes[sizeof(empty)], copied_bytes[sizeof(copied)];
    memcpy(source_bytes, &source, sizeof(source)); memcpy(other_bytes, &other, sizeof(other));
    memcpy(empty_bytes, &empty, sizeof(empty)); memcpy(copied_bytes, &copied, sizeof(copied));
    assert(!cetta_native_handle_use_demote(NULL));
    assert(!cetta_native_handle_use_demote(&empty));
    assert(!cetta_native_handle_use_demote(&other));
    assert(!cetta_native_handle_use_demote(&copied));
    assert(!cetta_native_handle_use_fork_lifetime(NULL, &source));
    assert(!cetta_native_handle_use_fork_lifetime(&empty, NULL));
    assert(!cetta_native_handle_use_fork_lifetime(&empty, &empty));
    assert(!cetta_native_handle_use_fork_lifetime(&source, &source));
    assert(!cetta_native_handle_use_fork_lifetime(&other, &source));
    assert(!cetta_native_handle_use_fork_lifetime(&empty, &copied));
    assert(!memcmp(source_bytes, &source, sizeof(source)));
    assert(!memcmp(other_bytes, &other, sizeof(other)));
    assert(!memcmp(empty_bytes, &empty, sizeof(empty)));
    assert(!memcmp(copied_bytes, &copied, sizeof(copied)));
    assert(!cetta_native_handle_acquire_owned(left, kind, left_holder,
        CETTA_NATIVE_HANDLE_USE_EXCLUSIVE, &empty));
    assert(!memcmp(empty_bytes, &empty, sizeof(empty)));
    assert(cetta_native_handle_close(left, kind, left_id));
    assert(cetta_native_handle_close(right, kind, right_id));
    assert(cetta_native_handle_use_release(&source));
    assert(cetta_native_handle_use_release(&other));
    arena_free(&arena);
    /* The copied receipt's real owner is retired. Its address identity must
     * reject these calls before any owner or list-neighbor dereference. */
    assert(!cetta_native_handle_use_demote(&copied));
    assert(!cetta_native_handle_use_fork_lifetime(&empty, &copied));
    assert(!cetta_native_handle_use_fork_lifetime(&empty, &source));
    assert(!memcmp(copied_bytes, &copied, sizeof(copied)));
    assert(!memcmp(empty_bytes, &empty, sizeof(empty)));
    assert(atomic_load(&released) == before + 2u);
    size_t count = SIZE_MAX;
    assert(!cetta_native_handle_test_reserve_use_count(&count) && count == SIZE_MAX);
    cetta_native_handle_cleanup_all(left); cetta_native_handle_cleanup_all(right);
    free(left); free(right);
    puts("acquired use: fork and demotion reject invalid receipts without mutation");
}

static void use_fork_storage_case(void) {
    unsigned before = atomic_load(&released);
    CettaLibraryContext *context = use_context();
    Arena arena;
    arena_init_detached(&arena);
    uint64_t id = new_resource(context);
    assert(cetta_native_handle_set_storage(context, kind, id, use_payload_storage));
    Atom *holder = cetta_native_handle_owned_atom(context, &arena, kind, id);
    CettaNativeHandleUse source = {0}, forked = {0}, moved = {0};
    assert(cetta_native_handle_acquire_owned(context, kind, holder,
        CETTA_NATIVE_HANDLE_USE_EXCLUSIVE, &source));
    CettaStorageCensus census = {0};
    cetta_native_handle_use_storage(&source, &census); cetta_storage_finish(&census);
    assert(!census.incomplete && !census.invalidated && !census.overflow && census.length == 2u);
    size_t baseline = census.retained;
    assert(cetta_native_handle_use_fork_lifetime(&forked, &source));
    assert(cetta_native_handle_use_demote(&source));
    cetta_native_handle_use_storage(&forked, &census); cetta_storage_finish(&census);
    assert(census.retained == baseline && census.length == 2u && census.repeated > 0u);
    cetta_storage_census_free(&census);
    assert(cetta_native_handle_close(context, kind, id)); arena_free(&arena);
    assert(cetta_native_handle_use_move(&moved, &forked));
    assert(cetta_native_handle_use_release(&source));
    cetta_native_handle_use_storage(&moved, &census); cetta_storage_finish(&census);
    assert(!census.incomplete && !census.invalidated && !census.overflow);
    assert(census.retained == baseline && census.length == 2u);
    cetta_storage_census_free(&census);
    assert(!cetta_native_handle_use_is_current(&moved));
    assert(!cetta_native_handle_use_release(&forked));
    assert(atomic_load(&released) == before);
    assert(cetta_native_handle_use_release(&moved));
    assert(atomic_load(&released) == before + 1u);
    cetta_native_handle_cleanup_all(context); free(context);
    puts("acquired use: forked owner census and moved lifetime receipt passed");
}

static void use_fork_reentrant_release(void *resource) {
    assert(!cetta_native_handle_use_resource(use_reentrant_record));
    CettaNativeHandleUse source = {0}, forked = {0};
    assert(cetta_native_handle_acquire_owned(reentrant_context, kind,
        use_reentrant_holder, CETTA_NATIVE_HANDLE_USE_EXCLUSIVE, &source));
    assert(cetta_native_handle_close(reentrant_context, kind, reentrant_id));
    assert(cetta_native_handle_use_demote(&source));
    assert(cetta_native_handle_use_fork_lifetime(&forked, &source));
    assert(cetta_native_handle_use_release(&source));
    assert(*(int *)cetta_native_handle_use_resource(&forked) == 42);
    assert(cetta_native_handle_use_release(&forked));
    release_resource(resource);
}

static void use_fork_reentrant_case(void) {
    unsigned before = atomic_load(&released);
    CettaLibraryContext *context = use_context();
    Arena arena;
    arena_init_detached(&arena);
    reentrant_context = context;
    reentrant_id = new_resource(context);
    use_reentrant_holder = cetta_native_handle_owned_atom(context, &arena, kind, reentrant_id);
    int *resource = malloc(sizeof(*resource)); assert(resource); *resource = 42;
    uint64_t id = 0u;
    assert(cetta_native_handle_alloc(context, kind, resource, use_fork_reentrant_release, &id));
    Atom *holder = cetta_native_handle_owned_atom(context, &arena, kind, id);
    CettaNativeHandleUse source = {0}, forked = {0};
    use_reentrant_record = &forked;
    assert(cetta_native_handle_acquire_owned(context, kind, holder,
        CETTA_NATIVE_HANDLE_USE_EXCLUSIVE, &source));
    assert(cetta_native_handle_use_fork_lifetime(&forked, &source));
    assert(cetta_native_handle_close(context, kind, id));
    assert(cetta_native_handle_use_release(&source));
    assert(atomic_load(&released) == before);
    assert(cetta_native_handle_use_release(&forked));
    assert(atomic_load(&released) == before + 2u);
    use_reentrant_record = NULL; use_reentrant_holder = NULL; reentrant_context = NULL;
    arena_free(&arena); cetta_native_handle_cleanup_all(context); free(context);
    puts("acquired use: demoted and forked reentrant finalization passed");
}

static void use_exchange_case(void) {
    CettaLibraryContext *context = calloc(1, sizeof(*context));
    assert(context); context->native_handle_next_id = 1;
    Arena arena, other; arena_init_detached(&arena); arena_init_detached(&other);
    unsigned before = atomic_load(&released);
    uint64_t id = new_resource(context);
    Atom *holder = cetta_native_handle_owned_atom(context, &arena, kind, id);
    Atom *alias = atom_deep_copy(&other, holder);
    CettaNativeHandleUse use = {0}, lifetime = {0}, empty = {0};
    int *image = malloc(sizeof(*image)); assert(image); *image = 17;
    void *sentinel = image, *previous = sentinel;
    assert(!cetta_native_handle_use_exchange(context,kind,holder,NULL, image, &previous));
    assert(!cetta_native_handle_use_exchange(context,kind,holder,&empty, image, &previous));
    assert(previous == sentinel);
    assert(cetta_native_handle_acquire_owned(context,kind,holder,
        CETTA_NATIVE_HANDLE_USE_EXCLUSIVE,&use));
    CettaLibraryContext *other_context = calloc(1,sizeof(*other_context));
    assert(other_context); other_context->native_handle_next_id = 1;
    uint64_t other_id = 0;
    /* A separately held borrowed registry view can name the same payload.
     * It still cannot act for this owner's exclusive use. */
    assert(cetta_native_handle_alloc(other_context,kind,use.resource,NULL,&other_id));
    assert(other_id == id);
    Atom *other_holder = cetta_native_handle_owned_atom(other_context,&other,kind,other_id);
    CettaNativeHandleUse other_use = {0};
    assert(cetta_native_handle_acquire_owned(other_context,kind,other_holder,
        CETTA_NATIVE_HANDLE_USE_EXCLUSIVE,&other_use));
    assert(cetta_native_handle_use_resource(&other_use) == use.resource);
    assert(!cetta_native_handle_use_exchange(context,kind,holder,&other_use,image,&previous));
    assert(!cetta_native_handle_use_exchange(other_context,kind,other_holder,&use,image,&previous));
    Atom *textual = cetta_native_handle_atom(&arena,kind,id);
    assert(!cetta_native_handle_use_exchange(context,kind,textual,&use,image,&previous));
    assert(previous == sentinel && cetta_native_handle_use_release(&other_use));
    assert(cetta_native_handle_close(other_context,kind,other_id));
    cetta_native_handle_cleanup_all(other_context); free(other_context);
    CettaNativeHandleUse forged = use;
    assert(!cetta_native_handle_use_exchange(context,kind,holder,&forged,image,&previous));
    assert(!cetta_native_handle_use_exchange(context,kind,holder,&use,NULL,&previous));
    assert(!cetta_native_handle_use_exchange(context,kind,holder,&use,image,NULL));
    assert(!cetta_native_handle_use_exchange(context,kind,holder,&use,use.resource,&previous));
    assert(!cetta_native_handle_use_exchange(context,kind,holder,&use,image,&use.resource));
    assert(cetta_native_handle_use_fork_lifetime(&lifetime,&use));
    assert(!cetta_native_handle_use_exchange(context,kind,holder,&lifetime,image,&previous));
    assert(!cetta_native_handle_use_exchange(context,kind,holder,&use,image,&previous));
    assert(previous == sentinel && *(int *)cetta_native_handle_use_resource(&use) == 42);
    assert(cetta_native_handle_use_release(&lifetime));
    assert(cetta_native_handle_use_exchange(context,kind,holder,&use,image,&previous));
    assert(previous != image && *(int *)previous == 42);
    assert(cetta_native_handle_use_resource(&use) == image);
    assert(cetta_native_handle_get_owned(context,kind,holder) == image);
    assert(cetta_native_handle_get_owned(context,kind,alias) == image);
    assert(cetta_native_handle_use_is_current(&use) && atomic_load(&released) == before);
    release_resource(previous);
    assert(atomic_load(&released) == before + 1u);
    assert(cetta_native_handle_close(context,kind,id));
    previous = sentinel;
    int *declined = malloc(sizeof(*declined)); assert(declined);
    assert(!cetta_native_handle_use_exchange(context,kind,holder,&use,declined,&previous));
    assert(previous == sentinel && cetta_native_handle_use_resource(&use) == image);
    assert(atomic_load(&released) == before + 1u);
    free(declined);
    assert(cetta_native_handle_use_release(&use));
    assert(atomic_load(&released) == before + 2u);
    arena_free(&arena); arena_free(&other);
    cetta_native_handle_cleanup_all(context); free(context);
    puts("acquired use: exclusive payload exchange preserves aliases and refuses live borrows");
}

static void acquired_use_return_operations_cases(void) {
    use_exchange_case();
    use_demote_case();
    use_detached_fork_case();
    use_thread_fork_case();
    use_fork_refusal_case();
    use_fork_storage_case();
    use_fork_reentrant_case();
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
        int *value = cetta_native_handle_get_owned(ctx, kinds[i % 3], handles[i]);
        assert(value && *value == (int)i);
        assert(!cetta_native_handle_get_owned(ctx, kinds[(i + 1) % 3], handles[i]));
    }
    /* The same printable id in a different context conveys no authority. */
    int *different = malloc(sizeof(*different));
    assert(different);
    uint64_t other_id;
    assert(cetta_native_handle_alloc(other_ctx, kinds[0], different, free, &other_id));
    assert(other_id == ids[0]);
    assert(cetta_native_handle_owned_atom(other_ctx, &survivor, kinds[0], other_id));
    assert(!cetta_native_handle_get_owned(other_ctx, kinds[0], handles[0]));
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
        int *value = cetta_native_handle_get_owned(ctx, kinds[i % 3], handles[i]);
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
    assert(cetta_native_handle_get_owned(ctx, kinds[1], handles[1]));
    printf("native handle registry: peak metadata=%zu, one live root metadata=%zu\n", peak_storage, stats.storage_bytes);
    cetta_native_handle_cleanup_all(ctx);
    assert(cetta_native_handle_count(ctx) == 0);
    assert(released == before + n + n / 2);
    /* Reinitializing storage must not reissue old identities or revive seals. */
    uint64_t replacement = new_resource(ctx);
    assert(replacement > ids[n - 1]);
    for (size_t i = 0; i < n; ++i)
        assert(!cetta_native_handle_get_owned(ctx, kinds[i % 3], handles[i]));
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
    assert(cetta_native_handle_get_owned(ctx, kind, handle) ==
           cetta_native_handle_get(ctx, kind, id));
    Atom *printed = cetta_native_handle_atom(&source, kind, id);
    assert(atom_eq(handle, printed));
    assert(!cetta_native_handle_get_owned(ctx, kind, printed));
    assert(!cetta_native_handle_get_owned(ctx, "wrong-kind", handle));
    arena_reset(&source, source_start);
    expect_dead(ctx, id);
    assert(released == 1);

    /* Duplicate results share the resource, not a destructible unique owner. */
    id = new_resource(ctx);
    handle = cetta_native_handle_owned_atom(ctx, &source, kind, id);
    Atom *copy = atom_deep_copy(&survivor, handle);
    Atom *second = atom_deep_copy_shared(&other, handle);
    assert(atom_eq(handle, copy) && atom_eq(copy, second));
    assert(cetta_native_handle_get_owned(ctx, kind, copy) ==
           cetta_native_handle_get_owned(ctx, kind, second));
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
    assert(cetta_native_handle_get_owned(ctx, kind, copy));
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
    assert(!cetta_native_handle_get_owned(ctx, kind, handle));
    assert(!cetta_native_handle_close(ctx, kind, id));
    copy = atom_deep_copy(&survivor, handle);
    assert(cetta_native_handle_arg(copy, kind, &decoded) && decoded == id);
    expect_dead(ctx, id);
    uint64_t replacement = new_resource(ctx);
    assert(replacement != id);
    assert(!cetta_native_handle_get_owned(ctx, kind, copy));
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
    acquired_use_cases();
    acquired_use_return_operations_cases();
    exercise_concurrent_registry();
    exercise_growth();
#ifdef CETTA_NATIVE_HANDLE_ALLOCATION_TEST
    exercise_allocation_failures();
#endif
    symbol_table_free(&symbols);
    g_symbols = NULL;
    puts("native handle ownership: release, escape, alias, reset, teardown and concurrent-copy checks passed");
    return 0;
}
