#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../../src/embedding.c"

static cetta_engine_t *target;
static atomic_bool armed, at_unlock, released;
static atomic_uint external_closes;
typedef struct {
    atomic_bool waiting, callback_started, finished;
    cetta_status_t status;
    cetta_engine_t *engine;
} EntryWitness;
static EntryWitness *entry_witness;

static void wait_until(atomic_bool *event) {
    struct timespec start, now;
    assert(!clock_gettime(CLOCK_MONOTONIC, &start));
    while (!atomic_load(event)) {
        nanosleep(&(struct timespec){.tv_nsec = 1000000L}, NULL);
        assert(!clock_gettime(CLOCK_MONOTONIC, &now));
        assert(now.tv_sec - start.tv_sec < 5 && "cross-thread cleanup waited on the engine lock");
    }
}
int __real_pthread_mutex_unlock(pthread_mutex_t *);
int __real_pthread_mutex_lock(pthread_mutex_t *);
int __real_pthread_mutex_timedlock(pthread_mutex_t *, const struct timespec *);
static void witness_wait(pthread_mutex_t *mutex) {
    if (entry_witness && target && mutex == &target->mutex) {
        atomic_store(&entry_witness->waiting, true);
        wait_until(&entry_witness->callback_started);
    }
}
int __wrap_pthread_mutex_lock(pthread_mutex_t *mutex) {
    witness_wait(mutex);
    return __real_pthread_mutex_lock(mutex);
}
int __wrap_pthread_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *deadline) {
    witness_wait(mutex);
    return __real_pthread_mutex_timedlock(mutex, deadline);
}
int __wrap_pthread_mutex_unlock(pthread_mutex_t *mutex) {
    if (target && mutex == &target->mutex && atomic_exchange(&armed, false)) {
        /* The engine has already drained its queue, but still owns its lock.
         * Publish the peer's last release in this exact handoff interval. */
        atomic_store(&at_unlock, true);
        wait_until(&released);
        assert(atomic_load(&external_closes) == 0u);
    }
    return __real_pthread_mutex_unlock(mutex);
}
static void retain_external(void *pointer) { (void)pointer; }
static void release_external(void *pointer) { atomic_fetch_add((atomic_uint *)pointer, 1u); }
typedef struct { cetta_atom_t *atom; cetta_query_t *query; } Peer;
static void *last_release(void *pointer) {
    Peer *peer = pointer;
    wait_until(&at_unlock);
    if (peer->query) cetta_embed_query_release(peer->query);
    else cetta_embed_atom_release(peer->atom);
    atomic_store(&released, true);
    return NULL;
}
static void release_at_unlock(bool query_case) {
    assert(cetta_embed_engine_new(NULL, &target) == CETTA_OK);
    atomic_store(&armed, false);
    atomic_store(&at_unlock, false);
    atomic_store(&released, false);
    atomic_store(&external_closes, 0u);
    int64_t number = 17;
    cetta_buffer_descriptor_t descriptor = {.size = sizeof(descriptor), .dtype = "i64",
        .allocation = &number, .allocation_bytes = sizeof(number)};
    cetta_buffer_owner_t owner = {&external_closes, retain_external, release_external};
    cetta_buffer_t *buffer = NULL;
    Peer peer = {0};
    assert(cetta_embed_buffer_borrow(&descriptor, &owner, &buffer) == CETTA_OK);
    assert(cetta_embed_atom_buffer(target, buffer, &peer.atom) == CETTA_OK);
    cetta_embed_buffer_release(buffer);
    if (query_case) {
        assert(cetta_embed_query_open(target, peer.atom, NULL, &peer.query) == CETTA_OK);
        cetta_embed_atom_release(peer.atom);
        peer.atom = NULL;
    }
    EmbeddingScope scope;
    assert(engine_enter(target, &scope) == CETTA_OK);
    pthread_t worker;
    assert(!pthread_create(&worker, NULL, last_release, &peer));
    atomic_store(&armed, true);
    engine_leave(target, &scope);
    assert(!pthread_join(worker, NULL));
    assert(atomic_load(&external_closes) == 1u && "cleanup handoff left the external owner retained");
    assert(atomic_load(&target->references) == 1u);
    cetta_embed_engine_release(target);
    target = NULL;
}
static void *enter_before_callback(void *pointer) {
    EntryWitness *witness = pointer;
    cetta_atom_t *atom = NULL;
    witness->status = cetta_embed_atom_integer(witness->engine, 1, &atom);
    cetta_embed_atom_release(atom);
    atomic_store(&witness->finished, true);
    return NULL;
}
static cetta_status_t open_witness(void *context, cetta_engine_t *engine,
        cetta_atom_t *const *arguments, size_t arity, void **cursor) {
    (void)engine; (void)arguments; assert(!arity);
    *cursor = context;
    return CETTA_OK;
}
static cetta_status_t next_witness(void *cursor, cetta_callback_state_t *state, cetta_atom_t **atom) {
    EntryWitness *witness = cursor;
    atomic_store(&witness->callback_started, true);
    wait_until(&witness->finished);
    assert(witness->status == CETTA_BUSY);
    *state = CETTA_CALLBACK_VALUE;
    return cetta_embed_atom_integer(witness->engine, 7, atom);
}
static void close_witness(void *cursor, cetta_callback_close_mode_t mode) { (void)cursor; (void)mode; }
static void waiting_entry_observes_later_callback(void) {
    assert(cetta_embed_engine_new(NULL, &target) == CETTA_OK);
    EntryWitness witness = {.engine = target};
    cetta_callback_descriptor_t descriptor = {.size = sizeof(descriptor),
        .kind = CETTA_CALLBACK_SINGLE, .context = &witness,
        .open = open_witness, .next = next_witness, .close = close_witness};
    cetta_atom_t *callback = NULL;
    assert(cetta_embed_callback_new(target, &descriptor, &callback) == CETTA_OK);
    EmbeddingScope scope;
    assert(engine_enter(target, &scope) == CETTA_OK);
    entry_witness = &witness;
    pthread_t worker;
    assert(!pthread_create(&worker, NULL, enter_before_callback, &witness));
    wait_until(&witness.waiting);
    cetta_result_t *result = NULL;
    assert(cetta_embed_call(target, callback, NULL, 0, NULL, &result) == CETTA_OK);
    assert(cetta_embed_result_count(result) == 1u);
    assert(!pthread_join(worker, NULL));
    entry_witness = NULL;
    engine_leave(target, &scope);
    cetta_embed_result_release(result);
    cetta_embed_atom_release(callback);
    cetta_embed_engine_release(target);
    target = NULL;
}
int main(void) {
    for (unsigned i = 0; i < 8u; ++i) {
        release_at_unlock(false);
        release_at_unlock(true);
    }
    waiting_entry_observes_later_callback();
    puts("embedding cleanup: last atom/query releases during unlock reclaim exactly once without another entry");
    return 0;
}
