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
    Atom *stable_exact_pattern;
    pthread_barrier_t *start;
    bool ok;
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
        value = bindings_lookup_value_id(&match->bindings, QUERY_VALUE_ID).skeleton;
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

/* Cursors belong to individual readers, but their pinned occurrence view is
 * shared.  Keep two independently owned cursors alive across writer updates,
 * then release them without an enclosing caller transition. */
static bool validate_occurrence_cursors(Space *space, Atom *pattern) {
    SpaceOccurrenceCursor cursor, clone;
    space_occurrence_cursor_init_empty(&cursor);
    space_occurrence_cursor_init_empty(&clone);
    bool ok = space_occurrence_cursor_init(space, pattern, &cursor);
    if (ok)
        ok = space_occurrence_cursor_clone(&cursor, &clone);
    sched_yield();
    for (unsigned pass = 0u; pass < 2u && ok; pass++) {
        SpaceOccurrenceCursor *current = pass ? &clone : &cursor;
        CettaIndex index;
        CettaCount count = 0u;
        bool seen[BASE_ROW_COUNT + 1u] = {false};
        SpaceOccurrenceCursorStep step;
        while ((step = space_occurrence_cursor_next(current, &index)) ==
               SPACE_OCCURRENCE_CURSOR_ITEM) {
            Atom *atom = space_occurrence_cursor_atom(current, index);
            if (!atom || atom->kind != ATOM_EXPR || atom->expr.len != 2u) {
                ok = false;
                break;
            }
            Atom *value = atom->expr.elems[1];
            if (value->kind != ATOM_GROUNDED || value->ground.gkind != GV_INT) {
                ok = false;
                break;
            }
            int64_t number = value->ground.ival;
            size_t slot = number == OPTIONAL_VALUE ? BASE_ROW_COUNT :
                number >= 0 && number < BASE_ROW_COUNT ? (size_t)number :
                BASE_ROW_COUNT + 1u;
            if (slot > BASE_ROW_COUNT || seen[slot]) {
                ok = false;
                break;
            }
            seen[slot] = true;
            count++;
        }
        ok = ok && step == SPACE_OCCURRENCE_CURSOR_END &&
            count == current->ceiling;
        for (size_t i = 0u; i < BASE_ROW_COUNT; i++)
            ok = ok && seen[i];
        space_occurrence_cursor_release(current);
        sched_yield();
    }
    space_occurrence_cursor_release(&clone);
    space_occurrence_cursor_release(&cursor);
    return ok;
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
        smset_free(&matches);
        if (task->ok) {
            bool found = false;
            task->ok = space_match_backend_ground_exact_exists_frontier(
                           task->space, task->stable_exact_pattern,
                           &found) &&
                       found;
        }
        if (task->ok)
            task->ok = validate_occurrence_cursors(
                task->space, (round & 1u) ? task->query :
                    task->query->expr.elems[1]);
        arena_reset(&scratch, mark);
        if ((round & 7u) == 0u)
            sched_yield();
    }
    cetta_shared_transition_scope_leave();
    arena_free(&scratch);
    bindings_thread_cache_free();
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

static void assert_concurrent_snapshot(Space *space, Arena *scratch,
                                       Atom *query,
                                       bool expected_optional_present) {
    SubstMatchSet matches;
    bool optional_present = false;

    cetta_shared_transition_scope_enter();
    smset_init(&matches);
    space_subst_query(space, scratch, query, &matches);
    assert(validate_snapshot(&matches, &optional_present));
    assert(optional_present == expected_optional_present);
    smset_free(&matches);
    cetta_shared_transition_scope_leave();
}

/* Concurrent reads begin with dirty indices. Each worker owns its result
 * arena; the caller keeps the shared spaces, universe and symbols live.
 * Mutations occur only after joining the previous episode. */
typedef struct {
    SymbolTable *symbols;
    Space *space;
    Atom *subject;
    const SymbolId *expected;
    uint32_t expected_count;
    uint32_t mode;
    pthread_barrier_t *start;
    uint64_t checks;
} TypeReaderTask;

static void check_type_result(TypeReaderTask *task, Atom **types,
                               uint32_t count, bool inferred) {
    if (inferred && task->expected_count == 0u) {
        assert(count == 1u);
        assert(atom_is_symbol_id(types[0], g_builtin_syms.undefined_type));
        task->checks += 2u;
        return;
    }
    assert(count == task->expected_count);
    task->checks++;
    for (uint32_t i = 0u; i < count; ++i) {
        assert(atom_is_symbol_id(types[i], task->expected[i]));
        task->checks++;
    }
}

static void *type_reader_main(void *raw) {
    TypeReaderTask *task = raw;
    assert(g_symbols == task->symbols);
    g_hashcons = NULL;
    assert(g_var_intern == NULL);
    Arena results;
    arena_init(&results);
    int started = pthread_barrier_wait(task->start);
    assert(started == 0 || started == PTHREAD_BARRIER_SERIAL_THREAD);
    cetta_shared_transition_scope_enter();
    for (uint32_t round = 0u; round < 80u; ++round) {
        ArenaMark mark = arena_mark(&results);
        Atom **types = NULL;
        uint32_t count = 0u;
        bool inferred = task->mode == 0u;
        if (task->mode == 0u) {
            count = get_atom_types(task->space, &results, task->subject, &types);
        } else if (task->mode == 1u) {
            assert(space_get_declared_types_checked(task->space, &results,
                task->subject, &types, &count));
            task->checks++;
        } else {
            assert(space_head_declares_type(task->space, atom_symbol_current_id(task->subject))
                == (task->expected_count != 0u));
            count = space_get_declared_types(task->space, &results,
                task->subject, &types);
            task->checks++;
        }
        check_type_result(task, types, count, inferred);
        free(types);
        assert(!cetta_shared_transition_guard_held_by_current_thread());
        assert(cetta_shared_transition_scope_active());
        task->checks += 2u;
        if (round == 0u) {
            CETTA_SCOPED_SHARED_TRANSITION(outer_read);
            assert(cetta_shared_transition_guard_held_by_current_thread());
            types = NULL;
            assert(space_get_declared_types_checked(task->space, &results,
                task->subject, &types, &count));
            check_type_result(task, types, count, false);
            free(types);
            assert(cetta_shared_transition_guard_held_by_current_thread());
            task->checks += 3u;
        }
        assert(!cetta_shared_transition_guard_held_by_current_thread());
        task->checks++;
        arena_reset(&results, mark);
        if ((round & 7u) == 0u) sched_yield();
    }
    cetta_shared_transition_scope_leave();
    assert(!cetta_shared_transition_scope_active());
    assert(!cetta_shared_transition_guard_held_by_current_thread());
    task->checks += 2u;
    arena_free(&results);
    bindings_thread_cache_free();
    return NULL;
}

static uint64_t type_read_episode(SymbolTable *symbols, Space *space,
        Atom *subject, const SymbolId *expected, uint32_t count) {
    enum { READERS = 6 };
    pthread_barrier_t start;
    pthread_t threads[READERS];
    TypeReaderTask tasks[READERS];
    assert(pthread_barrier_init(&start, NULL, READERS) == 0);
    for (uint32_t i = 0u; i < READERS; ++i) {
        tasks[i] = (TypeReaderTask){.symbols=symbols, .space=space,
            .subject=subject, .expected=expected, .expected_count=count,
            .mode=i%3u, .start=&start};
        assert(pthread_create(&threads[i], NULL, type_reader_main, &tasks[i]) == 0);
    }
    uint64_t checks = 0u;
    for (uint32_t i = 0u; i < READERS; ++i) {
        assert(pthread_join(threads[i], NULL) == 0);
        checks += tasks[i].checks;
    }
    assert(pthread_barrier_destroy(&start) == 0);
    return checks;
}

static AtomId type_row(TermUniverse *universe, Arena *construction,
                       Atom *subject, SymbolId type) {
    Atom *items[] = {atom_symbol_id(construction, g_builtin_syms.colon),
        subject, atom_symbol_id(construction, type)};
    AtomId id = term_universe_store_atom_id(universe, NULL,
        atom_expr(construction, items, 3u));
    assert(id != CETTA_ATOM_ID_NONE);
    return id;
}

static void check_concurrent_declared_types(SymbolTable *symbols) {
    Arena persistent, construction;
    arena_init(&persistent); arena_init(&construction);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    Space own, dependency, empty;
    space_init_with_universe(&own, &universe);
    space_init_with_universe(&dependency, &universe);
    space_init_with_universe(&empty, &universe);
    SymbolId name = symbol_intern_cstr(symbols, "cold-concurrent-subject");
    Atom *subject = atom_symbol_id(&construction, name);
    SymbolId types[5];
    const char *names[] = {"ColdT0", "ColdT1", "ColdT2", "ColdT3", "ColdT4"};
    AtomId rows[5];
    for (uint32_t i = 0u; i < 5u; ++i) {
        types[i] = symbol_intern_cstr(symbols, names[i]);
        rows[i] = type_row(&universe, &construction, subject, types[i]);
    }
    space_add_atom_id(&own, rows[0]); space_add_atom_id(&own, rows[0]);
    space_add_atom_id(&own, rows[1]); space_add_atom_id(&own, rows[4]);
    space_add_atom_id(&dependency, rows[2]);
    space_add_atom_id(&dependency, rows[2]);
    space_add_atom_id(&dependency, rows[4]);
    assert(space_remove_atom_id(&own, rows[4]));
    assert(space_remove_atom_id(&dependency, rows[4]));
    assert(space_add_dependency(&own, &dependency));
    assert(own.native.ty_idx_dirty && dependency.native.ty_idx_dirty);
    SpaceReadToken before = space_read_token(&own);
    SymbolId first[] = {types[0], types[0], types[1], types[2], types[2]};
    uint64_t checks = type_read_episode(symbols, &own, subject, first, 5u);
    assert(space_read_token_is_current(before));
    assert(!own.native.ty_idx_dirty && !dependency.native.ty_idx_dirty);
    assert(space_remove_atom_id(&own, rows[1]));
    space_add_atom_id(&own, rows[3]);
    assert(!space_read_token_is_current(before));
    SpaceReadToken after_own = space_read_token(&own);
    space_add_atom_id(&dependency, rows[4]);
    assert(!space_read_token_is_current(after_own));
    assert(own.native.ty_idx_dirty);
    /* Removing and restoring a dependency row makes its next read cold too. */
    assert(space_remove_atom_id(&dependency, rows[4]));
    space_add_atom_id(&dependency, rows[4]);
    assert(dependency.native.ty_idx_dirty);
    SymbolId second[] = {types[0], types[0], types[3], types[2], types[2], types[4]};
    checks += type_read_episode(symbols, &own, subject, second, 6u);
    checks += type_read_episode(symbols, &empty, subject, NULL, 0u);
    Atom **serial = NULL;
    uint32_t count = space_get_declared_types(&own, &construction, subject, &serial);
    assert(count == 6u && !cetta_shared_transition_scope_active());
    for (uint32_t i = 0u; i < count; ++i)
        assert(atom_is_symbol_id(serial[i], second[i]));
    free(serial);
    space_free(&empty); space_free(&own); space_free(&dependency);
    term_universe_free(&universe);
    arena_free(&construction); arena_free(&persistent);
    printf("PASS: cold concurrent type reads, revisions, nested guards (%llu checks)\n",
        (unsigned long long)checks);
}

typedef struct {
    Space *space;
    Atom *present;
    Atom *absent;
    pthread_barrier_t *start;
    unsigned mode;
    uint64_t checks;
} MembershipReaderTask;

static bool membership_read(MembershipReaderTask *task, Atom *query) {
    bool applicable = false;
    bool found = false;
    switch (task->mode) {
    case 0u:
        return space_contains_exact(task->space, query);
    case 1u:
        found = space_contains_exact_symbol_application(task->space,
            atom_head_symbol_id(query), query->expr.elems + 1u,
            query->expr.len - 1u, &applicable);
        break;
    case 2u:
        found = space_match_exists_ground_exact_expression_coordinates(
            task->space, query->expr.elems, query->expr.len, &applicable);
        break;
    case 3u:
        found = space_contains_canonical(task->space, query, &applicable);
        break;
    default:
        found = space_match_exists_ground_exact(task->space, query, &applicable);
        break;
    }
    /* A ground match finds an identical row by identity; a miss of a row
     * holding a number is left to the matcher, which HE lets match another
     * spelling of that number (1 with 1.0). */
    assert(applicable || (task->mode == 4u && !found));
    task->checks++;
    return found;
}

static void *membership_reader_main(void *raw) {
    MembershipReaderTask *task = raw;
    cetta_shared_transition_scope_enter();
    int started = pthread_barrier_wait(task->start);
    assert(started == 0 || started == PTHREAD_BARRIER_SERIAL_THREAD);
    for (unsigned round = 0u; round < 80u; round++) {
        assert(membership_read(task, task->present));
        assert(!membership_read(task, task->absent));
        assert(!cetta_shared_transition_guard_held_by_current_thread());
        task->checks += 3u;
        if (round == 0u) {
            CETTA_SCOPED_SHARED_TRANSITION(outer);
            assert(membership_read(task, task->present));
            assert(!membership_read(task, task->absent));
            assert(cetta_shared_transition_guard_held_by_current_thread());
            task->checks += 3u;
        }
        if ((round & 7u) == 0u) sched_yield();
    }
    assert(!cetta_shared_transition_guard_held_by_current_thread());
    cetta_shared_transition_scope_leave();
    assert(!cetta_shared_transition_scope_active());
    task->checks += 2u;
    return NULL;
}

static uint64_t membership_read_episode(Space *space, Atom *present, Atom *absent) {
    enum { READERS = 10 };
    pthread_barrier_t start;
    pthread_t threads[READERS];
    MembershipReaderTask tasks[READERS];
    assert(pthread_barrier_init(&start, NULL, READERS) == 0);
    for (unsigned i = 0u; i < READERS; i++) {
        tasks[i] = (MembershipReaderTask){.space=space, .present=present,
            .absent=absent, .start=&start, .mode=i%5u};
        assert(pthread_create(&threads[i], NULL, membership_reader_main, &tasks[i]) == 0);
    }
    uint64_t checks = 0u;
    for (unsigned i = 0u; i < READERS; i++) {
        assert(pthread_join(threads[i], NULL) == 0);
        checks += tasks[i].checks;
    }
    assert(pthread_barrier_destroy(&start) == 0);
    return checks;
}

static void check_concurrent_membership(SymbolTable *symbols) {
    Arena persistent, construction;
    arena_init(&persistent); arena_init(&construction);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    Space space; space_init_with_universe(&space, &universe);
    SymbolId head = symbol_intern_cstr(symbols, "cold-membership-row");
    AtomId last = CETTA_ATOM_ID_NONE;
    for (unsigned i = 0u; i < 256u; i++) {
        last = term_universe_store_atom_id(&universe, NULL, row(&construction, head, i));
        assert(last != CETTA_ATOM_ID_NONE);
        space_add_atom_id(&space, last);
    }
    assert(space.native.id_present_synced_len < space_length64(&space));
    Atom *present = row(&construction, head, 255), *absent = row(&construction, head, 999);
    SpaceReadToken before = space_read_token(&space);
    uint64_t checks = membership_read_episode(&space, present, absent);
    assert(space_read_token_is_current(before));
    assert(space_remove_atom_id(&space, last));
    space_add_atom_id(&space, term_universe_store_atom_id(&universe, NULL, absent));
    assert(!space_read_token_is_current(before));
    SpaceReadToken after = space_read_token(&space);
    checks += membership_read_episode(&space, absent, present);
    assert(space_read_token_is_current(after));
    assert(space_contains_exact(&space, absent) && !space_contains_exact(&space, present));
    assert(!cetta_shared_transition_scope_active());
    space_free(&space); term_universe_free(&universe);
    arena_free(&construction); arena_free(&persistent);
    printf("PASS: cold concurrent membership, misses, revisions, nested guards (%llu checks)\n",
        (unsigned long long)checks);
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
    Atom *stable_exact_pattern;

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
    stable_exact_pattern = term_universe_get_atom(&universe, base_ids[0]);
    assert(stable_exact_pattern != NULL);

    seed_space(&deferred_space, &universe, base_ids, optional_id);
    assert_deferred_outside_concurrent_scope(
        &deferred_space, &construction, query);
    space_free(&deferred_space);

    seed_space(&concurrent_space, &universe, base_ids, optional_id);
    assert_concurrent_snapshot(
        &concurrent_space, &construction, query, true);
    {
        CETTA_SCOPED_SHARED_TRANSITION(remove_transition);
        assert(space_remove_atom_id(&concurrent_space, optional_id));
    }
    assert_concurrent_snapshot(
        &concurrent_space, &construction, query, false);
    {
        CETTA_SCOPED_SHARED_TRANSITION(add_transition);
        space_add_atom_id(&concurrent_space, optional_id);
    }
    assert_concurrent_snapshot(
        &concurrent_space, &construction, query, true);

    assert(pthread_barrier_init(&start, NULL, READER_COUNT + 1u) == 0);
    for (size_t i = 0u; i < READER_COUNT; i++) {
        reader_tasks[i] = (ReaderTask){
            .space = &concurrent_space,
            .query = query,
            .stable_exact_pattern = stable_exact_pattern,
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
    }
    assert(pthread_join(writer, NULL) == 0);
    assert(writer_task.ok);
    assert(pthread_barrier_destroy(&start) == 0);
    assert(concurrent_space.match_backend.native.pinned == NULL);
    assert(concurrent_space.match_backend.native.match_trie_pins == 0u);
    assert(space_length64(&concurrent_space) == BASE_ROW_COUNT + 1u);
    assert_concurrent_snapshot(
        &concurrent_space, &construction, query, true);
    assert(concurrent_space.match_backend.native.stree != NULL);
    assert(!concurrent_space.match_backend.native.stree_dirty);

    space_free(&concurrent_space);
    term_universe_free(&universe);
    arena_free(&construction);
    arena_free(&persistent);
    bindings_thread_cache_free();
    check_concurrent_declared_types(&symbols);
    check_concurrent_membership(&symbols);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    puts("PASS: shared Space concurrent index snapshots");
    return 0;
}
