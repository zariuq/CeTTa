#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "space.h"
#include "space_match_backend.h"
#include "shared_transition.h"
#include "stats.h"
#include "symbol.h"

/* A cursor over a space with dependencies reads as match composes the space:
 * its own rows, then each dependency's own rows (past its imported prefix),
 * in import order.  Each dependency is pinned when the cursor opens, so a row
 * added to one later is not read; an index lent reads back its row while the
 * cursor lives; a clone continues from its source's place, and a rewound
 * clone reads from the start.  Every cursor over one state of the store names
 * a dependency's row alike, whatever its pattern selects among the facts the
 * space's relations hold. */

typedef struct {
    CettaIndex index;
    Atom *atom;
} Lent;

static Atom *row(Arena *arena, const char *head, int64_t value) {
    Atom *items[2] = {atom_symbol(arena, head), atom_int(arena, value)};
    return atom_expr(arena, items, 2u);
}

static Atom *pair(Arena *arena, const char *head, int64_t left, int64_t right) {
    Atom *items[3] = {atom_symbol(arena, head), atom_int(arena, left),
                      atom_int(arena, right)};
    return atom_expr(arena, items, 3u);
}

static AtomId store(TermUniverse *universe, Atom *atom) {
    AtomId id = term_universe_store_atom_id(universe, NULL, atom);
    assert(id != CETTA_ATOM_ID_NONE);
    return id;
}

static int64_t column(const Atom *atom, uint32_t k) {
    assert(atom && atom->kind == ATOM_EXPR && k < atom->expr.len);
    return atom->expr.elems[k]->ground.ival;
}

/* Every item of CURSOR, each read back through its index as it is lent and
 * again once the cursor has ended. */
static size_t drain(SpaceOccurrenceCursor *cursor, Lent *out, size_t cap) {
    size_t n = 0u;
    for (;;) {
        CettaIndex index = 0u;
        Atom *atom = NULL;
        SpaceOccurrenceCursorStep step =
            space_occurrence_cursor_next(cursor, &index, &atom);
        if (step == SPACE_OCCURRENCE_CURSOR_END)
            break;
        assert(step == SPACE_OCCURRENCE_CURSOR_ITEM);
        assert(atom && n < cap);
        assert(atom_eq(space_occurrence_cursor_atom(cursor, index), atom));
        out[n++] = (Lent){index, atom};
    }
    for (size_t i = 0u; i < n; i++)
        assert(atom_eq(space_occurrence_cursor_atom(cursor, out[i].index),
                       out[i].atom));
    return n;
}

static void expect(const Lent *lent, size_t n, const int64_t *values,
                   const CettaIndex *indexes, size_t want) {
    assert(n == want);
    for (size_t i = 0u; i < n; i++) {
        assert(column(lent[i].atom, 1u) == values[i]);
        assert(lent[i].index == indexes[i]);
    }
}

/* The index lent for the row (e LEFT RIGHT). */
static CettaIndex index_of(const Lent *lent, size_t n, int64_t left,
                           int64_t right) {
    for (size_t i = 0u; i < n; i++) {
        if (lent[i].atom->expr.len == 3u &&
            column(lent[i].atom, 1u) == left &&
            column(lent[i].atom, 2u) == right)
            return lent[i].index;
    }
    assert(!"the row was not lent");
    return 0u;
}

/* The relation's facts among the rows lent: (e i 1000+i). */
static size_t facts_lent(const Lent *lent, size_t n) {
    size_t facts = 0u;
    for (size_t i = 0u; i < n; i++)
        facts += lent[i].atom->expr.len == 3u &&
                 column(lent[i].atom, 2u) >= 1000;
    return facts;
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    Arena persistent, scratch;
    arena_init(&persistent);
    arena_init(&scratch);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);

    /* The space: (p 1) (p 2) (p 3).  A module: (p 90) imported into it
     * before it was linked, then its own (p 9) (p 8) (p 10).  A second
     * module: (p 20).  Small spaces read every row: the cursor lends each
     * row as a candidate, in row order. */
    Space self, module, second;
    space_init_with_universe(&module, &universe);
    space_init_with_universe(&second, &universe);
    /* The imported guards rank below the owner. Opening the joined view
     * must discover and acquire this set before taking maintenance locks. */
    space_init_with_universe(&self, &universe);
    space_add_atom_id(&self, store(&universe, row(&scratch, "p", 1)));
    space_add_atom_id(&self, store(&universe, row(&scratch, "p", 2)));
    space_add_atom_id(&self, store(&universe, row(&scratch, "p", 3)));
    space_add_atom_id(&module, store(&universe, row(&scratch, "p", 90)));
    assert(space_mark_imported_prefix(&module));
    space_add_atom_id(&module, store(&universe, row(&scratch, "p", 9)));
    space_add_atom_id(&module, store(&universe, row(&scratch, "p", 8)));
    space_add_atom_id(&module, store(&universe, row(&scratch, "p", 10)));
    space_add_atom_id(&second, store(&universe, row(&scratch, "p", 20)));
    assert(space_add_dependency(&self, &module));
    assert(space_add_dependency(&self, &second));

    Atom *items[2] = {atom_symbol(&scratch, "p"), atom_var(&scratch, "x")};
    Atom *pattern = atom_expr(&scratch, items, 2u);

    Lent lent[16];
    SpaceOccurrenceCursor cursor;
    cetta_shared_transition_scope_enter();
    space_occurrence_cursor_init_empty(&cursor);
    assert(space_occurrence_cursor_init(&self, pattern, &cursor));
    /* own (p 1) (p 2) (p 3); the module's own rows after the space's three;
     * the second module's (p 20) after the module's three own rows.  The
     * module's imported (p 90) is not read through it. */
    const int64_t all_values[] = {1, 2, 3, 9, 8, 10, 20};
    const CettaIndex all_indexes[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u};
    expect(lent, drain(&cursor, lent, 16u), all_values, all_indexes, 7u);
    space_occurrence_cursor_release(&cursor);

    /* A clone made inside the first module continues where its source is. */
    space_occurrence_cursor_init_empty(&cursor);
    assert(space_occurrence_cursor_init(&self, pattern, &cursor));
    for (int i = 0; i < 4; i++) {
        CettaIndex index = 0u;
        Atom *atom = NULL;
        assert(space_occurrence_cursor_next(&cursor, &index, &atom) ==
               SPACE_OCCURRENCE_CURSOR_ITEM);
    }
    SpaceOccurrenceCursor clone, rewound;
    space_occurrence_cursor_init_empty(&clone);
    space_occurrence_cursor_init_empty(&rewound);
    assert(space_occurrence_cursor_clone(&cursor, &clone));
    const int64_t rest_values[] = {8, 10, 20};
    const CettaIndex rest_indexes[] = {4u, 5u, 6u};
    expect(lent, drain(&clone, lent, 16u), rest_values, rest_indexes, 3u);
    expect(lent, drain(&cursor, lent, 16u), rest_values, rest_indexes, 3u);
    space_occurrence_cursor_release(&clone);
    space_occurrence_cursor_release(&cursor);

    /* An unstarted clone of an unstarted cursor reads everything. */
    space_occurrence_cursor_init_empty(&cursor);
    assert(space_occurrence_cursor_init(&self, pattern, &cursor));
    assert(space_occurrence_cursor_clone_unstarted(&cursor, &rewound));
    expect(lent, drain(&rewound, lent, 16u), all_values, all_indexes, 7u);
    space_occurrence_cursor_release(&rewound);
    space_occurrence_cursor_release(&cursor);

    /* A space without dependencies has no composition. */
    space_occurrence_cursor_init_empty(&cursor);
    assert(space_occurrence_cursor_init(&second, pattern, &cursor));
    assert(cursor.composition == NULL);
    const int64_t second_values[] = {20};
    const CettaIndex second_indexes[] = {0u};
    expect(lent, drain(&cursor, lent, 16u), second_values, second_indexes, 1u);
    space_occurrence_cursor_release(&cursor);

    /* Rows added to the dependencies after the cursor opened are not read
     * by it; a cursor opened after them reads them in their places. */
    space_occurrence_cursor_init_empty(&cursor);
    assert(space_occurrence_cursor_init(&self, pattern, &cursor));
    space_add_atom_id(&second, store(&universe, row(&scratch, "p", 30)));
    space_add_atom_id(&module, store(&universe, row(&scratch, "p", 31)));
    expect(lent, drain(&cursor, lent, 16u), all_values, all_indexes, 7u);
    space_occurrence_cursor_release(&cursor);
    space_occurrence_cursor_init_empty(&cursor);
    assert(space_occurrence_cursor_init(&self, pattern, &cursor));
    const int64_t grown_values[] = {1, 2, 3, 9, 8, 10, 31, 20, 30};
    const CettaIndex grown_indexes[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};
    expect(lent, drain(&cursor, lent, 16u), grown_values, grown_indexes, 9u);
    space_occurrence_cursor_release(&cursor);
    cetta_shared_transition_scope_leave();

    /* A space of (f 0) imports a module whose relation holds (e 0 1000)
     * (e 1 1001) (e 2 1002) beside its row (f 1), then a module of (e 0 5)
     * (f 2).  A cursor for (f $x) leaves the relation as it is; one for
     * (e $x $y), opened after it, gives the relation back to rows.  Both
     * name (e 0 5) alike, past every index the first module's rows and
     * facts can take. */
    SymbolId e = symbol_intern_cstr(g_symbols, "e");
    AtomId e_head = store(&universe, atom_symbol_id(&scratch, e));
    AtomId columns[6];
    for (int64_t i = 0; i < 3; i++) {
        columns[2 * i] = store(&universe, atom_int(&scratch, i));
        columns[2 * i + 1] = store(&universe, atom_int(&scratch, i + 1000));
    }
    Space composed, related, after;
    space_init_with_universe(&composed, &universe);
    space_init_with_universe(&related, &universe);
    space_init_with_universe(&after, &universe);
    /* The row first: from then on the space takes its facts as relations. */
    space_add_atom_id(&related, store(&universe, row(&scratch, "f", 1)));
    related.dense_from_first = true;
    assert(space_add_dense_tuples(&related, e, e_head, 2u, columns, 3u));
    assert(space_dense_facts(&related) == 3u);
    space_add_atom_id(&after, store(&universe, pair(&scratch, "e", 0, 5)));
    space_add_atom_id(&after, store(&universe, row(&scratch, "f", 2)));
    space_add_atom_id(&composed, store(&universe, row(&scratch, "f", 0)));
    assert(space_add_dependency(&composed, &related));
    assert(space_add_dependency(&composed, &after));
    Atom *f_items[2] = {atom_symbol(&scratch, "f"), atom_var(&scratch, "x")};
    Atom *e_items[3] = {atom_symbol_id(&scratch, e), atom_var(&scratch, "x"),
                        atom_var(&scratch, "y")};
    Atom *f_pattern = atom_expr(&scratch, f_items, 2u);
    Atom *e_pattern = atom_expr(&scratch, e_items, 3u);
    SpaceOccurrenceCursor by_f, by_e;
    space_occurrence_cursor_init_empty(&by_f);
    space_occurrence_cursor_init_empty(&by_e);
    assert(space_occurrence_cursor_init(&composed, f_pattern, &by_f));
    assert(space_dense_facts(&related) == 3u);
    assert(space_occurrence_cursor_init(&composed, e_pattern, &by_e));
    assert(space_dense_facts(&related) == 0u);
    Lent f_lent[16], e_lent[16];
    size_t f_n = drain(&by_f, f_lent, 16u);
    size_t e_n = drain(&by_e, e_lent, 16u);
    assert(facts_lent(e_lent, e_n) == 3u && facts_lent(f_lent, f_n) == 0u);
    CettaIndex in_f = index_of(f_lent, f_n, 0, 5);
    CettaIndex in_e = index_of(e_lent, e_n, 0, 5);
    assert(in_f == in_e);
    assert(in_f >= by_f.ceiling + space_length64(&related));
    space_occurrence_cursor_release(&by_f);
    space_occurrence_cursor_release(&by_e);

    space_free(&composed);
    space_free(&related);
    space_free(&after);
    space_free(&self);
    space_free(&module);
    space_free(&second);
    term_universe_free(&universe);
    arena_free(&scratch);
    arena_free(&persistent);
    printf("PASS: a cursor over a space with dependencies reads its own rows, "
           "then each dependency's own rows as they were when it opened, "
           "names them alike whatever its pattern, and clones keep their "
           "place\n");
    return 0;
}
