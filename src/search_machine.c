#include "search_machine.h"

bool search_machine_init(SearchMachine *sm, const Bindings *base,
                         Arena *scratch_arena) {
    sm->scratch_arena = scratch_arena;
    sm->owns_scratch_arena = false;
    if (!sm->scratch_arena) {
        arena_init(&sm->owned_scratch_arena);
        sm->scratch_arena = &sm->owned_scratch_arena;
        sm->owns_scratch_arena = true;
    }
    if (bindings_builder_init(&sm->bindings, base))
        return true;
    if (sm->owns_scratch_arena) {
        arena_free(&sm->owned_scratch_arena);
        sm->scratch_arena = NULL;
        sm->owns_scratch_arena = false;
    }
    return false;
}

void search_machine_init_owned(SearchMachine *sm, Bindings *owned,
                               Arena *scratch_arena) {
    sm->scratch_arena = scratch_arena;
    sm->owns_scratch_arena = false;
    if (!sm->scratch_arena) {
        arena_init(&sm->owned_scratch_arena);
        sm->scratch_arena = &sm->owned_scratch_arena;
        sm->owns_scratch_arena = true;
    }
    bindings_builder_init_owned(&sm->bindings, owned);
}

void search_machine_free(SearchMachine *sm) {
    bindings_builder_free(&sm->bindings);
    if (sm->owns_scratch_arena) {
        arena_free(&sm->owned_scratch_arena);
        sm->scratch_arena = NULL;
        sm->owns_scratch_arena = false;
    }
}

SearchMachineMark search_machine_save(const SearchMachine *sm) {
    SearchMachineMark mark = {
        .bindings_mark = bindings_builder_save(&sm->bindings),
        .has_scratch_mark = sm->scratch_arena != NULL,
    };
    if (mark.has_scratch_mark)
        mark.scratch_mark = arena_mark(sm->scratch_arena);
    return mark;
}

void search_machine_rollback(SearchMachine *sm, SearchMachineMark mark) {
    bindings_builder_rollback(&sm->bindings, mark.bindings_mark);
    if (mark.has_scratch_mark && sm->scratch_arena)
        arena_reset(sm->scratch_arena, mark.scratch_mark);
}

void search_machine_commit(SearchMachine *sm) {
    bindings_builder_commit(&sm->bindings);
}

Arena *search_machine_scratch(SearchMachine *sm) {
    return sm->scratch_arena;
}

BindingsBuilder *search_machine_builder(SearchMachine *sm) {
    return &sm->bindings;
}

const Bindings *search_machine_bindings(const SearchMachine *sm) {
    return bindings_builder_bindings(&sm->bindings);
}

void search_machine_take(SearchMachine *sm, Bindings *out) {
    bindings_builder_take(&sm->bindings, out);
}
