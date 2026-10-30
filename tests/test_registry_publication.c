#include "space.h"
#include "eval.h"
#include "library.h"
#include "term_universe.h"
#include "registry_resolver.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
bool __real_registry_binding_publication_copy(RegistryBindingPublication *,
    const Registry *, uint32_t, Atom *);

static bool allocation_probe;
static size_t allocation_count, failure_at = SIZE_MAX;
static bool fail_transaction_copy, transaction_copy_failed;

static bool fail_allocation(void) {
    return allocation_probe && allocation_count++ == failure_at;
}
void *__wrap_malloc(size_t size) {
    return fail_allocation() ? NULL : __real_malloc(size);
}
void *__wrap_calloc(size_t count, size_t width) {
    return fail_allocation() ? NULL : __real_calloc(count, width);
}
void *__wrap_realloc(void *pointer, size_t size) {
    return fail_allocation() ? NULL : __real_realloc(pointer, size);
}
bool __wrap_registry_binding_publication_copy(RegistryBindingPublication *publication,
        const Registry *source, uint32_t index, Atom *value) {
    if (!fail_transaction_copy)
        return __real_registry_binding_publication_copy(publication, source, index, value);
    fail_transaction_copy = false;
    assert(source->entries[index].text_key.hold.token);
    allocation_count = 0u;
    failure_at = 0u;
    allocation_probe = true;
    bool copied = __real_registry_binding_publication_copy(publication, source, index, value);
    allocation_probe = false;
    failure_at = SIZE_MAX;
    transaction_copy_failed = !copied && allocation_count > 0u;
    return copied;
}

static void check_publication(Arena *arena, unsigned width) {
    Registry destination, changes;
    registry_init(&destination);
    registry_init(&changes);
    Atom *old = atom_int(arena, 1), *replacement = atom_int(arena, 2);
    for (unsigned i = 0u; i < width; i++) {
        char text[48];
        snprintf(text, sizeof(text), "&publication-%u", i);
        assert(registry_bind_symbol(&destination, atom_symbol(arena, text), old));
    }
    Atom *first = atom_symbol(arena, "&publication-0");
    Atom *structured = atom_expr2(arena, atom_symbol(arena, "name"), atom_int(arena, 7));
    assert(registry_bind_name(&destination, structured, old));
    RegistryResolver resolver;
    registry_resolver_init(&resolver);
    RegistryCapability *capability = registry_capability_new();
    assert(capability && registry_resolver_add_capability(&resolver, &destination,
        42u, capability, registry_recognize_all, NULL));
    const RegistryCapability *presented[] = {capability};
    RegistryResolutionSet resolved;
    registry_resolution_set_init(&resolved);
    assert(registry_resolver_lookup_name(&resolver, structured, NULL, 0u, &resolved)
        == REGISTRY_RESOLUTION_NO_MATCH);
    Atom *state = atom_symbol(arena, "separate-state");
    assert(registry_state_set_symbol(&destination, state, old));
    struct RegistryStates *state_owner = destination.states;
    assert(registry_bind_symbol(&changes, first, replacement));
    assert(registry_bind_name(&changes, structured, replacement));
    Atom *fresh = atom_symbol_data(arena, "&unregistered-publication-name");
    assert(registry_bind_symbol(&changes, fresh, replacement));
    RegistryReadView captured;
    assert(registry_read_view_init(&captured, &destination, arena));
    RegistryReadView *prior = registry_read_view_enter(&captured, &destination);
    assert(registry_lookup_symbol(&destination, first));
    registry_read_view_leave(prior);
    RegistryBindingPublication *publication = registry_binding_publication_new(&destination);
    assert(publication);
    for (uint32_t i = 0u; i < changes.len; i++)
        assert(registry_binding_publication_copy(publication, &changes, i, replacement));
    assert(registry_lookup_symbol(&destination, first) == old);
    assert(!registry_lookup_symbol(&destination, fresh));
    /* The state namespace is independent of a prepared binding snapshot. */
    assert(registry_state_set_symbol(&destination, state, replacement));
    allocation_probe = true;
    allocation_count = 0u;
    registry_binding_publication_commit(publication);
    allocation_probe = false;
    assert(allocation_count == 0u);
    registry_binding_publication_free(publication);
    assert(destination.states == state_owner);
    assert(atom_eq(registry_state_lookup_symbol(&destination, state), replacement));
    assert(registry_lookup_symbol(&destination, first) == replacement);
    assert(registry_lookup_name(&destination, structured) == replacement);
    assert(registry_resolver_lookup_name(&resolver, structured, presented, 1u, &resolved)
        == REGISTRY_RESOLUTION_UNIQUE);
    assert(resolved.len == 1u && resolved.items[0].value == replacement &&
           resolved.items[0].source_id == 42u);
    assert(registry_resolver_lookup_name(&resolver, structured, NULL, 0u, &resolved)
        == REGISTRY_RESOLUTION_NO_MATCH);
    assert(registry_lookup_symbol(&destination, fresh) == replacement);
    assert(!registry_read_view_validate(&captured, &destination));
    registry_read_view_free(&captured);
    registry_resolution_set_free(&resolved);
    registry_resolver_free(&resolver);
    registry_capability_delete(capability);
    registry_free(&changes);
    registry_free(&destination);
}

static void check_numbered_preparation_failures(Arena *arena) {
    Registry destination;
    registry_init(&destination);
    Atom *value = atom_int(arena, 17);
    for (unsigned i = 0u; i < 80u; i++) {
        char name[48];
        snprintf(name, sizeof(name), "publication-growth-%u", i);
        assert(registry_bind_symbol(&destination, atom_symbol(arena, name), value));
    }
    allocation_probe = true;
    allocation_count = 0u;
    RegistryBindingPublication *publication = registry_binding_publication_new(&destination);
    allocation_probe = false;
    assert(publication);
    size_t allocations = allocation_count;
    assert(allocations >= 8u);
    registry_binding_publication_free(publication);
    for (size_t fail = 0u; fail < allocations; fail++) {
        allocation_probe = true;
        allocation_count = 0u;
        failure_at = fail;
        publication = registry_binding_publication_new(&destination);
        allocation_probe = false;
        failure_at = SIZE_MAX;
        assert(!publication && destination.len == 80u && destination.word_len == 80u);
        for (uint32_t i = 0u; i < destination.len; i++)
            assert(destination.entries[i].value == value);
    }
    registry_free(&destination);
    printf("PASS: %zu constructor/entry/index allocation failures preserve the destination\n", allocations);
}

#if CETTA_MANAGED_SYMBOLS
static void check_preparation_failures(Arena *arena) {
    Registry destination, changes;
    registry_init(&destination);
    registry_init(&changes);
    Atom *name = atom_symbol(arena, "&existing-publication");
    Atom *old = atom_int(arena, 1), *next = atom_int(arena, 2);
    assert(registry_bind_symbol(&destination, name, old));
    assert(registry_bind_symbol(&changes, name, next));
    const char bytes[] = {'&', 'n', 0, (char)255, 'k'};
    Atom *fresh = atom_symbol_data_n(arena, bytes, sizeof(bytes));
    assert(atom_symbol_current_id(fresh) == SYMBOL_ID_NONE);
    assert(registry_bind_symbol(&changes, fresh, next));
    allocation_probe = true;
    allocation_count = 0u;
    failure_at = 0u;
    assert(!registry_binding_publication_new(&destination));
    allocation_probe = false;
    failure_at = SIZE_MAX;
    assert(registry_lookup_symbol(&destination, name) == old);
    RegistryBindingPublication *trial = registry_binding_publication_new(&destination);
    assert(trial);
    allocation_probe = true;
    allocation_count = 0u;
    assert(registry_binding_publication_copy(trial, &changes, 1u, next));
    allocation_probe = false;
    size_t allocations = allocation_count;
    assert(allocations > 0u);
    registry_binding_publication_free(trial);
    for (size_t fail = 0u; fail < allocations; fail++) {
        trial = registry_binding_publication_new(&destination);
        assert(trial && registry_binding_publication_copy(trial, &changes, 0u, next));
        allocation_probe = true;
        allocation_count = 0u;
        failure_at = fail;
        assert(!registry_binding_publication_copy(trial, &changes, 1u, next));
        allocation_probe = false;
        failure_at = SIZE_MAX;
        assert(!registry_binding_publication_copy(trial, &changes, 0u, next));
        registry_binding_publication_free(trial);
        assert(registry_lookup_symbol(&destination, name) == old);
        assert(!registry_lookup_symbol(&destination, fresh));
        assert(destination.len == 1u);
    }
    registry_free(&changes);
    registry_free(&destination);
    printf("PASS: %zu recoverable name allocations fail before binding publication\n", allocations);
}

static void check_transaction_failure(void) {
    for (unsigned fail = 0u; fail < 2u; fail++) {
        Arena persistent, answers;
        arena_init(&persistent);
        arena_set_runtime_kind(&persistent, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
        arena_init(&answers);
        arena_set_runtime_kind(&answers, CETTA_ARENA_RUNTIME_KIND_EVAL);
        TermUniverse universe;
        term_universe_init(&universe);
        term_universe_set_persistent_arena(&universe, &persistent);
        Space space;
        space_init_with_universe(&space, &universe);
        Registry registry;
        registry_init(&registry);
        assert(registry_bind_id_checked(&registry, g_builtin_syms.self,
            atom_space(&persistent, &space)));
        CettaLibraryContext *library = calloc(1u, sizeof(*library));
        assert(library);
        cetta_library_context_init_for_language_profile(library, CETTA_LANGUAGE_PETTA, NULL);
        CettaLibraryContext *previous = eval_current_library_context();
        eval_set_library_context(library);
        Atom *fresh = atom_symbol_data(&persistent, "&transaction-publication-target");
        assert(atom_symbol_current_id(fresh) == SYMBOL_ID_NONE);
        Atom *fact = atom_expr2(&persistent, atom_symbol_data(&persistent, "publication-fact"),
            atom_int(&persistent, 1));
        Atom *add = atom_symbol_id(&persistent, g_builtin_syms.add_atom);
        Atom *first = atom_expr3(&persistent, add,
            atom_symbol_id(&persistent, g_builtin_syms.self), fact);
        Atom *second = atom_expr3(&persistent, add, fresh, fact);
        Atom *body = atom_expr3(&persistent, atom_symbol(&persistent, "progn"), first, second);
        Atom *expression = atom_expr2(&persistent, atom_symbol(&persistent, "transaction"), body);
        ResultSet results;
        result_set_init(&results);
        fail_transaction_copy = fail != 0u;
        transaction_copy_failed = false;
        CettaEvalCompletion completion = eval_top_with_registry_completion(
            &space, &answers, &persistent, &registry, expression, &results);
        fail_transaction_copy = false;
        if (fail) {
            assert(transaction_copy_failed);
            assert(completion == CETTA_EVAL_INCOMPLETE_CAPACITY);
            assert(space_length64(&space) == 0u);
            assert(!registry_lookup_symbol(&registry, fresh));
        } else {
            assert(completion == CETTA_EVAL_COMPLETE);
            assert(space_length64(&space) == 1u);
            Atom *bound = registry_lookup_symbol(&registry, fresh);
            assert(bound && bound->kind == ATOM_GROUNDED && bound->ground.gkind == GV_SPACE);
            assert(space_length64(bound->ground.ptr) == 1u);
        }
        result_set_free(&results);
        eval_release_temporary_spaces();
        eval_cleanup_owned_new_spaces_for_current_thread();
        registry_free(&registry);
        eval_set_library_context(previous);
        cetta_library_context_free(library);
        free(library);
        space_free(&space);
        term_universe_free(&universe);
        arena_free(&answers);
        arena_free(&persistent);
    }
    puts("PASS: failed registry preparation leaves transaction Spaces unchanged and reports capacity");
}
#endif

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    g_symbols = &symbols;
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_hashcons = NULL;
    g_var_intern = NULL;
    Arena arena;
    arena_init(&arena);
    check_publication(&arena, 1u);
    check_publication(&arena, 80u);
    check_numbered_preparation_failures(&arena);
#if CETTA_MANAGED_SYMBOLS
    check_preparation_failures(&arena);
    check_transaction_failure();
#endif
    arena_free(&arena);
    symbol_table_free(&symbols);
    puts("PASS: prepared inline/heap binding publication preserves names, state and captured views without allocating");
    return 0;
}
