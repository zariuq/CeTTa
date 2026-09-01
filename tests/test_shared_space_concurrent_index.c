#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "shared_transition.h"
#include "space.h"
#include "space_match_backend.h"
#include "symbol.h"

enum {
    BASE_ROW_COUNT = 64,
    READER_COUNT = 6,
    READER_ROUNDS = 240,
    WRITER_ROUNDS = 360,
};

static const VarId QUERY_VALUE_ID = UINT64_C(900001);
static const int64_t OPTIONAL_VALUE = INT64_C(10000);

typedef struct {
    Space *space;
    Atom *query;
    pthread_barrier_t *start;
    bool ok;
    bool saw_present;
    bool saw_absent;
} ReaderTask;

typedef struct {
    Space *space;
    AtomId optional_id;
    pthread_barrier_t *start;
    bool ok;
} WriterTask;

static void init_symbols(SymbolTable *symbols) {
    symbol_table_init(symbols);
    symbol_table_init_builtins(symbols, &g_builtin_syms);
    g_symbols = symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
}

static Atom *row(Arena *arena, SymbolId row_symbol, int64_t value) {
    Atom *items[2] = {
        atom_symbol_id(arena, row_symbol),
        atom_int(arena, value),
    };
    return atom_expr(arena, items, 2u);
}

static Atom *row_query(Arena *arena, SymbolId row_symbol,
                       SymbolId value_spelling) {
    Atom *items[2] = {
        atom_symbol_id(arena, row_symbol),
        atom_var_with_spelling(arena, value_spelling, QUERY_VALUE_ID),
    };
    return atom_expr(arena, items, 2u);
}

static bool validate_snapshot(SubstMatchSet *matches,
                              bool *optional_present_out) {
    bool seen[BASE_ROW_COUNT + 1u] = {false};
    bool optional_present = false;

    if (!matches ||
        (matches->len != BASE_ROW_COUNT &&
         matches->len != BASE_ROW_COUNT + 1u)) {
        return false;
    }

    for (CettaIndex i = 0u; i < matches->len; i++) {
        SubstMatch *match = &matches->items[i];
        Atom *value;
        size_t slot;

        if (!match->exact)
            return false;
        value = bindings_lookup_id(&match->bindings, QUERY_VALUE_ID);
        if (!value || value->kind != ATOM_GROUNDED ||
            value->ground.gkind != GV_INT) {
            return false;
        }
        if (value->ground.ival == OPTIONAL_VALUE) {
            slot = BASE_ROW_COUNT;
            optional_present = true;
        } else if (value->ground.ival >= 0 &&
                   value->ground.ival < BASE_ROW_COUNT) {
            slot = (size_t)value->ground.ival;
        } else {
            return false;
        }
        if (seen[slot])
            return false;
        seen[slot] = true;
    }

    for (size_t i = 0u; i < BASE_ROW_COUNT; i++) {
        if (!seen[i])
            return false;
    }
    if (seen[BASE_ROW_COUNT] != optional_present)
        return false;
    if ((matches->len == BASE_ROW_COUNT + 1u) != optional_present)
        return false;
    *optional_present_out = optional_present;
    return true;
}

static void *reader_main(void *opaque) {
    ReaderTask *task = opaque;
    Arena scratch;
    int barrier_result;

    arena_init(&scratch);
    barrier_result = pthread_barrier_wait(task->start);
    task->ok = barrier_result == 0 ||
               barrier_result == PTHREAD_BARRIER_SERIAL_THREAD;
    if (!task->ok) {
        arena_free(&scratch);
        return NULL;
    }

    cetta_shared_transition_scope_enter();
    for (uint32_t round = 0u; round < READER_ROUNDS && task->ok; round++) {
        ArenaMark mark = arena_mark(&scratch);
        SubstMatchSet matches;
        bool optional_present = false;

        smset_init(&matches);
        space_subst_query(task->space, &scratch, task->query, &matches);
        task->ok = validate_snapshot(&matches, &optional_present);
        task->saw_present |= optional_present;
        task->saw_absent |= !optional_present;
        smset_free(&matches);
        arena_reset(&scratch, mark);
        if ((round & 7u) == 0u)
            sched_yield();
    }
    cetta_shared_transition_scope_leave();
    arena_free(&scratch);
    return NULL;
}

static void *writer_main(void *opaque) {
    WriterTask *task = opaque;
    int barrier_result = pthread_barrier_wait(task->start);

    task->ok = barrier_result == 0 ||
               barrier_result == PTHREAD_BARRIER_SERIAL_THREAD;
    if (!task->ok)
        return NULL;

    cetta_shared_transition_scope_enter();
    for (uint32_t round = 0u; round < WRITER_ROUNDS && task->ok; round++) {
        {
            CETTA_SCOPED_SHARED_TRANSITION(remove_transition);
            task->ok = space_remove_atom_id(task->space, task->optional_id) &&
                       space_length64(task->space) == BASE_ROW_COUNT;
        }
        sched_yield();
        {
            CETTA_SCOPED_SHARED_TRANSITION(add_transition);
            space_add_atom_id(task->space, task->optional_id);
            task->ok = space_length64(task->space) == BASE_ROW_COUNT + 1u;
        }
        if ((round & 3u) == 0u)
            sched_yield();
    }
    cetta_shared_transition_scope_leave();
    return NULL;
}

static void seed_space(Space *space, TermUniverse *universe,
                       const AtomId *base_ids,
                       AtomId optional_id) {
    space_init_with_universe(space, universe);
    assert(space_match_backend_try_set(space, SPACE_ENGINE_NATIVE));
    for (size_t i = 0u; i < BASE_ROW_COUNT; i++)
        space_add_atom_id(space, base_ids[i]);
    space_add_atom_id(space, optional_id);
    assert(space_length64(space) == BASE_ROW_COUNT + 1u);
    assert(space->match_backend.native.stree == NULL);
}

static void assert_deferred_outside_concurrent_scope(
        Space *space, Arena *scratch, Atom *query) {
    SubstMatchSet matches;
    bool saw_deferred = false;

    smset_init(&matches);
    space_subst_query(space, scratch, query, &matches);
    assert(matches.len == BASE_ROW_COUNT + 1u);
    for (CettaIndex i = 0u; i < matches.len; i++)
        saw_deferred |= !matches.items[i].exact;
    assert(saw_deferred);
    smset_free(&matches);
}

int main(void) {
    SymbolTable symbols;
    Arena persistent;
    Arena construction;
    TermUniverse universe;
    Space deferred_space;
    Space concurrent_space;
    AtomId base_ids[BASE_ROW_COUNT];
    AtomId optional_id;
    pthread_barrier_t start;
    pthread_t readers[READER_COUNT];
    pthread_t writer;
    ReaderTask reader_tasks[READER_COUNT];
    WriterTask writer_task;
    SymbolId row_symbol;
    SymbolId value_spelling;
    Atom *query;
    bool saw_present = false;
    bool saw_absent = false;

    init_symbols(&symbols);
    arena_init(&persistent);
    arena_init(&construction);
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    row_symbol = symbol_intern_cstr(&symbols, "concurrent-index-row");
    value_spelling = symbol_intern_cstr(&symbols, "concurrent-index-value");

    for (size_t i = 0u; i < BASE_ROW_COUNT; i++) {
        base_ids[i] = term_universe_store_atom_id(
            &universe, NULL, row(&construction, row_symbol, (int64_t)i));
        assert(base_ids[i] != CETTA_ATOM_ID_NONE);
        assert(term_universe_get_atom(&universe, base_ids[i]) != NULL);
    }
    optional_id = term_universe_store_atom_id(
        &universe, NULL, row(&construction, row_symbol, OPTIONAL_VALUE));
    assert(optional_id != CETTA_ATOM_ID_NONE);
    assert(term_universe_get_atom(&universe, optional_id) != NULL);
    query = row_query(&persistent, row_symbol, value_spelling);

    seed_space(&deferred_space, &universe, base_ids, optional_id);
    assert_deferred_outside_concurrent_scope(
        &deferred_space, &construction, query);
    space_free(&deferred_space);

    seed_space(&concurrent_space, &universe, base_ids, optional_id);
    assert(pthread_barrier_init(&start, NULL, READER_COUNT + 1u) == 0);
    for (size_t i = 0u; i < READER_COUNT; i++) {
        reader_tasks[i] = (ReaderTask){
            .space = &concurrent_space,
            .query = query,
            .start = &start,
        };
        assert(pthread_create(&readers[i], NULL, reader_main,
                              &reader_tasks[i]) == 0);
    }
    writer_task = (WriterTask){
        .space = &concurrent_space,
        .optional_id = optional_id,
        .start = &start,
    };
    assert(pthread_create(&writer, NULL, writer_main, &writer_task) == 0);

    for (size_t i = 0u; i < READER_COUNT; i++) {
        assert(pthread_join(readers[i], NULL) == 0);
        assert(reader_tasks[i].ok);
        saw_present |= reader_tasks[i].saw_present;
        saw_absent |= reader_tasks[i].saw_absent;
    }
    assert(pthread_join(writer, NULL) == 0);
    assert(writer_task.ok);
    assert(pthread_barrier_destroy(&start) == 0);
    assert(saw_present);
    assert(saw_absent);
    assert(space_length64(&concurrent_space) == BASE_ROW_COUNT + 1u);
    {
        SubstMatchSet final_matches;
        bool optional_present = false;

        cetta_shared_transition_scope_enter();
        smset_init(&final_matches);
        space_subst_query(
            &concurrent_space, &construction, query, &final_matches);
        assert(validate_snapshot(&final_matches, &optional_present));
        assert(optional_present);
        smset_free(&final_matches);
        cetta_shared_transition_scope_leave();
    }
    assert(concurrent_space.match_backend.native.stree != NULL);
    assert(!concurrent_space.match_backend.native.stree_dirty);

    space_free(&concurrent_space);
    term_universe_free(&universe);
    arena_free(&construction);
    arena_free(&persistent);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    puts("PASS: shared Space concurrent index snapshots");
    return 0;
}
