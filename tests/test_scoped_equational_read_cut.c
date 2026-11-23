#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>

#include "runtime_names.h"
#include "shared_transition.h"
#include "space.h"
#include "symbol.h"

enum { READ_ROUNDS = 1200, EDIT_ROUNDS = 900 };

typedef struct {
    Space *read;
    Space *edit;
    pthread_barrier_t start;
    CettaNameContext names;
    Atom *pattern, *output, *stable, *only_with_alias;
    AtomId transient, alias;
} Fixture;

static void begin(Fixture *f) {
    cetta_name_context_install(&f->names);
    cetta_shared_transition_scope_enter();
    int status = pthread_barrier_wait(&f->start);
    assert(status == 0 || status == PTHREAD_BARRIER_SERIAL_THREAD);
}

static void assert_answer(Fixture *f, Arena *read_arena, int expected) {
    ArenaMark mark = arena_mark(read_arena);
    Atom **values = NULL;
    size_t n = 0;
    assert(space_alias_instantiate_matches(f->read, read_arena, f->pattern,
                                           f->output, &values, &n));
    /* This checks the effect boundary as well as the selected values. */
    assert(!space_guard_held(f->read));
    assert(!space_guard_held(f->edit));
    assert(n >= 1 && n <= 2);
    assert(atom_eq(values[0], f->stable));
    if (n == 2) assert(atom_eq(values[1], f->only_with_alias));
    if (expected >= 0) assert(n == (size_t)expected);
    free(values);
    arena_reset(read_arena, mark);
}

static void *reader(void *opaque) {
    Fixture *f = opaque;
    Arena scratch;
    arena_init(&scratch);
    begin(f);
    for (unsigned i = 0; i < READ_ROUNDS; ++i) {
        assert_answer(f, &scratch, -1);
        if (i % 8 == 0) sched_yield();
    }
    cetta_shared_transition_scope_leave();
    bindings_thread_cache_free();
    arena_free(&scratch);
    return NULL;
}

static void *writer(void *opaque) {
    Fixture *f = opaque;
    begin(f);
    for (unsigned i = 0; i < EDIT_ROUNDS; ++i) {
        /* Neither state has a transient match. In one, the row is present
         * but its keys differ; in the other, the keys agree but the row is
         * absent. Only comparing across the cut could invent an answer. */
        {
            CETTA_SCOPED_SPACE_WRITE(publish, f->edit);
            assert(space_remove_atom_id(f->edit, f->transient));
            space_add_atom_id(f->edit, f->alias);
        }
        sched_yield();
        {
            CETTA_SCOPED_SPACE_WRITE(publish, f->edit);
            assert(space_remove_atom_id(f->edit, f->alias));
            space_add_atom_id(f->edit, f->transient);
        }
    }
    cetta_shared_transition_scope_leave();
    bindings_thread_cache_free();
    return NULL;
}

static void run_cut(int imported) {
    Arena persistent, input;
    TermUniverse universe;
    Space owner, importer;
    arena_init(&persistent);
    arena_init(&input);
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    space_init_with_universe(&owner, &universe);
    space_init_with_universe(&importer, &universe);
    Atom *head = atom_symbol(&input, "PairForCut");
    Atom *left = atom_symbol(&input, "Left");
    Atom *right = atom_symbol(&input, "Right");
    Atom *must_see = atom_symbol(&input, "stable");
    Atom *only_with_alias = atom_symbol(&input, "with-alias");
    Atom *must_not_see = atom_symbol(&input, "transient");
    Atom *same = atom_var_with_id(&input, "key", fresh_var_id());
    Atom *payload = atom_var_with_id(&input, "payload", fresh_var_id());
    Atom *positive[] = {head, left, left, must_see};
    Atom *negative[] = {head, left, right, must_not_see};
    Atom *conditional[] = {head, left, right, only_with_alias};
    Atom *edge[] = {atom_symbol(&input, "=e"), left, right};
    Atom *query[] = {head, same, same, payload};
    Fixture f = {
        .read = imported ? &importer : &owner,
        .edit = &owner, .names = cetta_name_context_current(),
        .pattern = atom_expr(&input, query, 4), .output = payload,
        .stable = must_see, .only_with_alias = only_with_alias,
        .transient = term_universe_store_atom_id(&universe, NULL, atom_expr(&input, negative, 4)),
        .alias = term_universe_store_atom_id(&universe, NULL, atom_expr(&input, edge, 3)),
    };
    assert(f.transient != CETTA_ATOM_ID_NONE && f.alias != CETTA_ATOM_ID_NONE);
    space_add(&owner, atom_expr(&input, positive, 4));
    space_add(&owner, atom_expr(&input, conditional, 4));
    space_add_atom_id(&owner, f.transient);
    if (imported) assert(space_add_dependency(&importer, &owner));
    assert_answer(&f, &input, 1);
    assert(space_remove_atom_id(&owner, f.transient));
    space_add_atom_id(&owner, f.alias);
    assert_answer(&f, &input, 2);
    assert(space_remove_atom_id(&owner, f.alias));
    space_add_atom_id(&owner, f.transient);
    assert(pthread_barrier_init(&f.start, NULL, 3) == 0);
    pthread_t reads[2], edit;
    for (unsigned i = 0; i < 2; ++i) assert(pthread_create(&reads[i], NULL, reader, &f) == 0);
    assert(pthread_create(&edit, NULL, writer, &f) == 0);
    for (unsigned i = 0; i < 2; ++i) assert(pthread_join(reads[i], NULL) == 0);
    assert(pthread_join(edit, NULL) == 0);
    assert(pthread_barrier_destroy(&f.start) == 0);
    assert_answer(&f, &input, 1);
    space_free(&importer);
    space_free(&owner);
    term_universe_free(&universe);
    arena_free(&input);
    arena_free(&persistent);
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    run_cut(0);
    run_cut(1);
    bindings_thread_cache_free();
    symbol_table_free(&symbols);
    puts("PASS: 4800 guarded reads cannot invent a match across an equation edit");
}
