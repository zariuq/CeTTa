#define _GNU_SOURCE
#include "atom.h"
#include "state_cell.h"
#include "symbol.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef CETTA_TEST_ATOM_STANDALONE
#include "tests/test_runtime_stats_stubs.h"
#endif

static void expect_print(Atom *value, const char *expected) {
    for (unsigned petta = 0; petta < 2; petta++) {
        char *text = NULL;
        size_t length = 0;
        FILE *out = open_memstream(&text, &length);
        assert(out);
        if (petta)
            atom_print_petta(value, out);
        else
            atom_print(value, out);
        assert(fclose(out) == 0);
        if (strcmp(text, expected)) {
            fprintf(stderr, "expected %s, received %s\n", expected, text);
            abort();
        }
        assert(length == strlen(expected));
        free(text);
    }
}

typedef struct {
    CettaBindingsValue base;
    StateCell *cell;
    unsigned holds;
} StateProjection;

static void projection_retain(void *context) {
    ((StateProjection *)context)->holds++;
}

static void projection_release(void *context) {
    StateProjection *projection = context;
    assert(projection->holds);
    projection->holds--;
}

static Atom *projection_observe(Arena *arena, const CettaBindingsValue *value) {
    const StateProjection *projection = (const StateProjection *)value;
    return atom_state(arena, projection->cell);
}

static void raw_state_controls(void) {
    Arena arena;
    arena_init_detached(&arena);
    StateCell leaf = {.value = atom_int(&arena, 7)};
    Atom *shared = atom_state(&arena, &leaf);
    expect_print(shared, "(State 7)");
    expect_print(atom_expr3(&arena, atom_symbol(&arena, "pair"), shared, shared),
                 "(pair (State 7) (State 7))");

    StateCell self = {0};
    Atom *self_atom = atom_state(&arena, &self);
    self.value = self_atom;
    expect_print(self_atom, "(State ...)");
    expect_print(atom_expr3(&arena, atom_symbol(&arena, "pair"), self_atom, self_atom),
                 "(pair (State ...) (State ...))");

    StateCell left = {0}, right = {0};
    Atom *left_atom = atom_state(&arena, &left);
    Atom *right_atom = atom_state(&arena, &right);
    left.value = right_atom;
    right.value = left_atom;
    expect_print(left_atom, "(State (State ...))");

    StateCell projected = {0};
    Atom *projected_atom = atom_state(&arena, &projected);
    StateProjection projection = {.base = {
        .retain = projection_retain, .release = projection_release,
        .observe = projection_observe}, .cell = &projected};
    projected.value = atom_bindings_value(&arena, &projection.base);
    expect_print(projected_atom, "(State ...)");
    arena_free(&arena);
    assert(!projection.holds);
}

#ifndef CETTA_TEST_STATE_PRINT_SMALL
static void deep_state_control(void) {
    enum { DEPTH = 20000 };
    StateCell *cells = calloc(DEPTH, sizeof(*cells));
    assert(cells);
    Arena arena;
    arena_init_detached(&arena);
    Atom *value = atom_int(&arena, 1);
    for (unsigned i = 0; i < DEPTH; i++) {
        cells[i].value = value;
        value = atom_state(&arena, &cells[i]);
    }
    FILE *out = tmpfile();
    assert(out);
    atom_print(value, out);
    assert(ftell(out) == 8L * DEPTH + 1L);
    assert(fclose(out) == 0);
    arena_free(&arena);
    free(cells);
}
#endif

#ifdef CETTA_TEST_MANAGED_STATE
typedef struct {
    CettaBindingsValue base;
    StateCell *cell;
    Atom *replacement;
    size_t baseline_images;
} StateMutation;

static void mutation_hold(void *context) {
    (void)context;
}

static Atom *mutation_observe(Arena *arena, const CettaBindingsValue *value) {
    const StateMutation *mutation = (const StateMutation *)value;
    StateCellImage *image = state_cell_image_prepare(mutation->replacement, NULL, false);
    assert(image && state_cell_publish_prepared(mutation->cell, image));
    assert(state_cell_diagnostics().images == mutation->baseline_images + 1u);
    return atom_symbol(arena, "updated");
}

static StateCell *managed(Arena *holder, Atom *value, Atom **wrapper) {
    StateCellImage *image = state_cell_image_prepare(value, NULL, false);
    assert(image);
    StateCell *cell = state_cell_managed_new(image);
    assert(cell);
    *wrapper = atom_state(holder, cell);
    assert(*wrapper);
    state_cell_creator_release(cell);
    return cell;
}

static void publish(StateCell *cell, Atom *value) {
    StateCellImage *image = state_cell_image_prepare(value, NULL, false);
    assert(image && state_cell_publish_prepared(cell, image));
}

static void managed_state_controls(void) {
    StateCellDiagnostics before = state_cell_diagnostics();
    Arena arena;
    arena_init_detached(&arena);
    Atom *self_atom, *left_atom, *right_atom;
    Atom *zero = atom_int(&arena, 0);
    StateCell *self = managed(&arena, zero, &self_atom);
    StateCell *left = managed(&arena, zero, &left_atom);
    StateCell *right = managed(&arena, zero, &right_atom);
    publish(self, self_atom);
    publish(left, right_atom);
    publish(right, left_atom);
    expect_print(self_atom, "(State ...)");
    expect_print(left_atom, "(State (State ...))");
    publish(self, zero);
    publish(left, zero);
    publish(right, zero);
    arena_free(&arena);
    (void)state_cell_reclaim_bounded(SIZE_MAX);
    assert(state_cell_diagnostics().nodes == before.nodes);
    assert(state_cell_diagnostics().images == before.images);

    for (unsigned petta = 0; petta < 2; petta++) {
        arena_init_detached(&arena);
        Atom *wrapper;
        StateCell *cell = managed(&arena, atom_int(&arena, 7), &wrapper);
        StateMutation mutation = {.base = {.retain = mutation_hold,
                .release = mutation_hold, .observe = mutation_observe},
            .cell = cell, .replacement = atom_int(&arena, 9),
            .baseline_images = state_cell_diagnostics().images};
        Atom *effect = atom_bindings_value(&arena, &mutation.base);
        Atom *parts[] = {atom_symbol(&arena, "triple"), wrapper, effect, wrapper};
        Atom *triple = atom_expr(&arena, parts, 4u);
        char *text = NULL;
        size_t length = 0;
        FILE *out = open_memstream(&text, &length);
        assert(out);
        if (petta)
            atom_print_petta(triple, out);
        else
            atom_print(triple, out);
        assert(fclose(out) == 0);
        assert(strcmp(text, "(triple (State 7) updated (State 7))") == 0);
        free(text);
        assert(state_cell_diagnostics().images == mutation.baseline_images);
        StateCellSnapshot current = {0};
        assert(state_cell_snapshot_acquire(cell, &current));
        assert(current.value->ground.ival == 9);
        state_cell_snapshot_release(&current);
        arena_free(&arena);
        (void)state_cell_reclaim_bounded(SIZE_MAX);
        assert(state_cell_diagnostics().nodes == before.nodes);
        assert(state_cell_diagnostics().images == before.images);
    }
}
#endif

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    raw_state_controls();
#ifndef CETTA_TEST_STATE_PRINT_SMALL
    deep_state_control();
#endif
#ifdef CETTA_TEST_MANAGED_STATE
    managed_state_controls();
#endif
    symbol_table_free(&symbols);
    g_symbols = NULL;
    puts("PASS: State cycles, acyclic aliases, reentrant observations and deep paths");
    return 0;
}
