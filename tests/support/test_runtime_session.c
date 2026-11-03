#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "runtime_session.h"
#include "runtime_names.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

static bool refuse_resources;
static Space *watched[3];
static unsigned frees[3], native_closes;
static bool refuse_malloc_request, refuse_realloc_request;
static size_t realloc_at, realloc_failure;
void *__real_malloc(size_t);
void *__real_realloc(void *, size_t);
void *__wrap_malloc(size_t size) {
    if (refuse_malloc_request) { errno = ENOMEM; return NULL; }
    return __real_malloc(size);
}
void *__wrap_realloc(void *pointer, size_t size) {
    if (refuse_realloc_request && realloc_at++ == realloc_failure) {
        errno = ENOMEM; return NULL;
    }
    return __real_realloc(pointer, size);
}
CettaEvalResources *__real_eval_resources_new(void);
CettaEvalResources *__wrap_eval_resources_new(void) {
    return refuse_resources ? NULL : __real_eval_resources_new();
}
void __real_space_free(Space *);
void __wrap_space_free(Space *space) {
    for (unsigned i = 0u; i < 3u; ++i) if (watched[i] && watched[i] == space) frees[i]++;
    __real_space_free(space);
}
typedef struct { CettaRuntimeSession session; CettaNameDomain names; Arena storage; } Fixture;
static Fixture *fixture_new(void) {
    Fixture *fixture = calloc(1, sizeof(*fixture));
    assert(fixture);
    cetta_name_domain_init(&fixture->names);
    arena_init_detached(&fixture->storage);
    return fixture;
}
static CettaNameContext select_names(Fixture *fixture) {
    CettaNameContext previous = cetta_name_context_current();
    CettaNameContext names = cetta_name_domain_context(&fixture->names);
    cetta_name_context_install(&names);
    return previous;
}
static void fixture_free(Fixture *fixture) {
    assert(!fixture->session.libraries_ready && !fixture->session.resources);
    arena_free(&fixture->storage);
    cetta_name_domain_free(&fixture->names);
    free(fixture);
}
static Space *created(Fixture *fixture, int value) {
    Space *space = arena_alloc(&fixture->storage, sizeof(*space));
    assert(space);
    space_init_with_universe(space, &fixture->session.libraries.term_universe);
    space_add(space, atom_int(&fixture->storage, value));
    eval_track_new_space(space);
    return space;
}
typedef struct { CettaRuntimeSession *session; Space *space; } Native;
static void native_close(void *pointer) {
    Native *native = pointer;
    assert(eval_current_library_context() == &native->session->libraries);
    assert(space_length64(native->space) == 1u);
    native_closes++;
}
static void arena_reservations(void) {
    Arena arena;
    assert(arena_try_init_detached(&arena));
    refuse_malloc_request = true;
    assert(!arena_try_reserve(&arena, 32u) && errno == ENOMEM);
    assert(!arena.head && !arena.live_bytes && !arena.reserved_bytes);
    refuse_malloc_request = false;
    assert(arena_try_reserve(&arena, 32u));
    assert(arena.head && !arena.head->used && !arena.live_bytes);
    refuse_malloc_request = true;
    assert(arena_try_reserve(&arena, 16u));
    unsigned char *retained = arena_alloc(&arena, 16u);
    memset(retained, 0x4b, 16u);
    ArenaBlock *head = arena.head;
    size_t live = arena.live_bytes, reserved = arena.reserved_bytes;
    assert(!arena_try_reserve(&arena, SIZE_MAX) && errno == EOVERFLOW);
    assert(!arena_try_reserve(&arena, ARENA_BLOCK_SIZE + 8u) && errno == ENOMEM);
    assert(arena.head == head && arena.live_bytes == live && arena.reserved_bytes == reserved);
    for (unsigned i = 0; i < 16u; ++i) assert(retained[i] == 0x4b);
    refuse_malloc_request = false;
    arena_free(&arena);
}
static void registration_rollback(void) {
    Fixture *fixture = fixture_new();
    CettaNameContext previous_names = select_names(fixture);
    assert(cetta_runtime_session_init(&fixture->session, &fixture->storage, CETTA_LANGUAGE_PETTA, NULL));
    assert(cetta_runtime_session_open_root(&fixture->session, &fixture->storage));
    CettaLibraryContext *previous_library = eval_exchange_library_context(&fixture->session.libraries);
    CettaEvalResources *previous_resources = eval_resources_exchange(fixture->session.resources);
    CettaEvalGc *previous_collector = eval_collector_exchange(eval_resources_collector(fixture->session.resources));
    TermUniverse *universe = &fixture->session.libraries.term_universe;
    term_universe_set_collection_mode(universe, TERM_UNIVERSE_COLLECTION_RECLAIM);
    Space *spaces[64] = {&fixture->session.space};
    space_add(spaces[0], atom_int(&fixture->storage, 100));
    unsigned count = 1;
    while (universe->observer_len != universe->observer_cap ||
           universe->root_provider_len != universe->root_provider_cap) {
        assert(count < 64u);
        spaces[count] = created(fixture, 100 + (int)count);
        count++;
    }
    size_t observers = universe->observer_len, providers = universe->root_provider_len;
    for (size_t at = 0; at < 2; ++at) {
        Space refused;
        realloc_at = 0; realloc_failure = at; refuse_realloc_request = true;
        assert(!space_try_init_with_universe(&refused, universe) && errno == ENOMEM);
        refuse_realloc_request = false;
        assert(realloc_at == at + 1u);
        assert(universe->observer_len == observers && universe->root_provider_len == providers);
        assert(universe->collection_mode == TERM_UNIVERSE_COLLECTION_RECLAIM);
        assert(!refused.ledger_counted);
        space_free(&refused);
    }
    assert(tu_intern_int(universe, 9999) != CETTA_ATOM_ID_NONE);
    TermUniverseCollectionReport report;
    assert(term_universe_collect(universe, &report));
    assert(report.reclaimed > 0u);
    for (unsigned i = 0; i < count; ++i) {
        Atom *value = space_get_at(spaces[i], 0u);
        assert(space_length64(spaces[i]) == 1u && value && value->kind == ATOM_GROUNDED);
        assert(value->ground.gkind == GV_INT && value->ground.ival == 100 + (int)i);
    }
    cetta_runtime_session_dispose(&fixture->session);
    eval_exchange_library_context(previous_library);
    eval_resources_exchange(previous_resources);
    eval_collector_exchange(previous_collector);
    cetta_name_context_install(&previous_names);
    fixture_free(fixture);
    puts("runtime registration: refused observer/provider growth preserves existing roots and collection progress");
}
int main(void) {
    Fixture *left = fixture_new(), *right = fixture_new(), *partial = fixture_new();
    CettaNameContext previous_names = select_names(left);
    assert(cetta_runtime_session_init(&left->session, &left->storage, CETTA_LANGUAGE_PETTA, NULL));
    assert(cetta_runtime_session_open_root(&left->session, &left->storage));
    CettaLibraryContext *previous_library = eval_exchange_library_context(&left->session.libraries);
    CettaEvalResources *previous_resources = eval_resources_exchange(left->session.resources);
    CettaEvalGc *previous_collector = eval_collector_exchange(eval_resources_collector(left->session.resources));
    Space *bound = created(left, 11), *unbound = created(left, 22);
    watched[0] = bound; watched[1] = unbound; watched[2] = &left->session.space;
    Atom *handle = atom_space(&left->storage, bound);
    registry_bind_id(&left->session.registry, symbol_intern_cstr(g_symbols, "bound"), handle);
    registry_bind_id(&left->session.registry, symbol_intern_cstr(g_symbols, "alias"), handle);
    Native native = {&left->session, bound};
    uint64_t id;
    assert(cetta_native_handle_alloc(&left->session.libraries, "session-test", &native, native_close, &id));
    select_names(right);
    assert(cetta_runtime_session_init(&right->session, &right->storage, CETTA_LANGUAGE_HE, NULL));
    assert(cetta_runtime_session_open_root(&right->session, &right->storage));
    eval_exchange_library_context(&right->session.libraries);
    eval_resources_exchange(right->session.resources);
    CettaEvalGc *right_collector = eval_resources_collector(right->session.resources);
    eval_collector_exchange(right_collector);
    Space *sibling = created(right, 37);
    select_names(left);
    cetta_runtime_session_dispose(&left->session);
    assert(native_closes == 1u);
    assert(frees[0] == 1u && frees[1] == 1u && frees[2] == 1u);
    assert(eval_current_library_context() == &right->session.libraries);
    assert(eval_resources_exchange(right->session.resources) == right->session.resources);
    assert(eval_collector_exchange(right_collector) == right_collector);
    assert(space_length64(sibling) == 1u);
    cetta_runtime_session_dispose(&left->session);
    assert(native_closes == 1u && frees[0] == 1u && frees[1] == 1u && frees[2] == 1u);
    select_names(partial);
    refuse_resources = true;
    assert(!cetta_runtime_session_init(&partial->session, &partial->storage, CETTA_LANGUAGE_PETTA, NULL));
    refuse_resources = false;
    assert(partial->session.libraries_ready && !partial->session.resources);
    assert(!cetta_runtime_session_open_root(&partial->session, &partial->storage));
    cetta_runtime_session_dispose(&partial->session);
    cetta_runtime_session_dispose(&partial->session);
    assert(eval_current_library_context() == &right->session.libraries);
    select_names(right);
    assert(space_length64(sibling) == 1u);
    cetta_runtime_session_dispose(&right->session);
    eval_exchange_library_context(previous_library);
    eval_resources_exchange(previous_resources);
    eval_collector_exchange(previous_collector);
    cetta_name_context_install(&previous_names);
    fixture_free(left); fixture_free(right); fixture_free(partial);
    for (unsigned i = 0; i < 3u; ++i) watched[i] = NULL;
    arena_reservations();
    registration_rollback();
    puts("runtime sessions: partial construction, unique registry aliases, ordered closure and sibling isolation pass");
    return 0;
}
