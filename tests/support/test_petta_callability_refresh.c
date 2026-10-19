#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "parser.h"
#include "petta_program.h"
#include "symbol.h"

static Atom *parse(Arena *arena, const char *source) {
    Atom **forms = NULL;
    int count = parse_metta_text(source, arena, &forms);
    assert(count == 1 && forms);
    Atom *atom = forms[0];
    free(forms);
    return atom;
}

static void add_atom(PettaProgram *program, Space *space, Atom *equation) {
    const PettaPlanNode *plan = petta_program_plan_dynamic_add(program, equation);
    assert(plan);
    CettaCount before = space_length64(space);
    space_add(space, equation);
    assert(space_length64(space) == before + 1u);
    assert(petta_program_note_add(program, space, space_get_at64(space, before), plan));
}

static void add(PettaProgram *program, Space *space, Arena *arena,
                const char *source) {
    add_atom(program, space, parse(arena, source));
}

static PettaCandidateSnapshotLease take_lease(PettaProgram *program, Space *space,
                                          Arena *arena, const char *name) {
    PettaCandidateSnapshotLease lease = {0};
    assert(petta_program_candidate_snapshot_lease_profiled(program, space,
        symbol_intern_cstr(g_symbols, name), &lease, NULL));
    if (lease.len != 1u || !lease.items[0].rhs_plan) fprintf(stderr, "%s len=%zu plan=%p\n", name, lease.len, lease.len ? (void *)lease.items[0].rhs_plan : NULL);
    assert(lease.len == 1u && lease.items[0].rhs_plan);
    return lease;
}

static void check(bool live) {
    Arena arena;
    Space space;
    arena_init(&arena);
    space_init(&space);
    PettaProgram *program = petta_program_new();
    assert(program);
    if (live)
        assert(petta_program_enable_callability_refresh(program));
    assert(petta_program_synchronize_space(program, &space));
    add(program, &space, &arena, "(= (caller $x) (future $x))");
    add(program, &space, &arena, "(= (unrelated) (inert 1))");
    Atom *element = parse(&arena, "(future 1)");
    add_atom(program, &space, atom_expr3(&arena, atom_symbol(&arena, "="),
        parse(&arena, "(held)"), atom_list(&arena, &element, 1u)));
    PettaCandidateSnapshotLease old = take_lease(program, &space, &arena, "caller");
    PettaCandidateSnapshotLease unrelated = take_lease(program, &space, &arena, "unrelated");
    PettaCandidateSnapshotLease held = take_lease(program, &space, &arena, "held");
    const PettaPlanNode *old_plan = old.items[0].rhs_plan;
    assert(old_plan->role == PETTA_PLAN_DATA);
    assert(held.items[0].rhs_plan->role == PETTA_PLAN_VALUE);
    add(program, &space, &arena, "(= (elsewhere) 9)");
    PettaCandidateSnapshotLease same = take_lease(program, &space, &arena, "caller");
    assert(same.items[0].rhs_plan == old_plan);
    petta_program_candidate_snapshot_lease_release(&same);

    /* Publish a new callable name. The old invocation remains intact. */
    add(program, &space, &arena, "(= (future $x) (+ $x 1))");
    PettaCandidateSnapshotLease current = take_lease(program, &space, &arena, "caller");
    assert(current.items[0].rhs_plan->role ==
           (live ? PETTA_PLAN_STATIC_CALL : PETTA_PLAN_DATA));
    assert((current.items[0].rhs_plan == old_plan) == !live);
    assert(old.items[0].rhs_plan == old_plan && old_plan->role == PETTA_PLAN_DATA);
    const PettaPlanNode *argument = petta_plan_child(current.items[0].rhs_plan, 1u);
    assert(argument && argument->has_equation_variable_slot);

    PettaCandidateSnapshotLease other = take_lease(program, &space, &arena, "unrelated");
    PettaCandidateSnapshotLease held_after = take_lease(program, &space, &arena, "held");
    assert(other.items[0].rhs_plan == unrelated.items[0].rhs_plan);
    assert(held_after.items[0].rhs_plan == held.items[0].rhs_plan);

    /* New immutable views publish the same current plan. */
    PettaProgramRevisionView *view = petta_program_revision_view_capture(program, &space);
    assert(view);
    PettaProgramRevisionProjection projection = {0};
    assert(petta_program_revision_view_bind(view, &space, &projection));
    PettaCandidateSnapshotLease projected = {0};
    assert(petta_program_revision_view_source_candidate_lease(&projection, &space,
        symbol_intern_cstr(g_symbols, "caller"), &projected, NULL));
    assert(projected.len == 1u);
    assert(projected.items[0].rhs_plan == current.items[0].rhs_plan);
    petta_program_candidate_snapshot_lease_release(&projected);
    petta_program_revision_view_free(view);
    petta_program_candidate_snapshot_lease_release(&held_after);
    petta_program_candidate_snapshot_lease_release(&other);
    petta_program_candidate_snapshot_lease_release(&current);
    petta_program_candidate_snapshot_lease_release(&held);
    petta_program_candidate_snapshot_lease_release(&unrelated);
    petta_program_candidate_snapshot_lease_release(&old);
    petta_program_free(program);
    space_free(&space);
    arena_free(&arena);
}

static void check_mutations(void) {
    Arena arena;
    Space space, replacement;
    arena_init(&arena);
    space_init(&space);
    space_init(&replacement);
    PettaProgram *program = petta_program_new();
    assert(petta_program_enable_callability_refresh(program));
    assert(petta_program_synchronize_space(program, &space));
    Atom *prefix = parse(&arena, "(= (prefix) 0)");
    Atom *future = parse(&arena, "(= (future $x) (+ $x 1))");
    add_atom(program, &space, prefix);
    add(program, &space, &arena, "(= (caller) (future 1))");
    add_atom(program, &space, future);
    PettaCandidateSnapshotLease saved = take_lease(program, &space, &arena, "caller");
    assert(saved.items[0].rhs_plan->role == PETTA_PLAN_STATIC_CALL);

    /* Removing a lower catalog position moves its neighbour's stamp too. */
    assert(space_remove(&space, prefix));
    petta_program_note_remove_one(program, &space, prefix);
    assert(space_remove(&space, future));
    petta_program_note_remove_one(program, &space, future);
    PettaCandidateSnapshotLease removed = take_lease(program, &space, &arena, "caller");
    assert(removed.items[0].rhs_plan->role == PETTA_PLAN_DATA);
    assert(saved.items[0].rhs_plan->role == PETTA_PLAN_STATIC_CALL);

    /* Compact stamped and newly appended rows together. */
    add_atom(program, &space, prefix);
    add_atom(program, &space, prefix);
    add_atom(program, &space, future);
    while (space_remove(&space, prefix)) {}
    petta_program_note_remove_all(program, &space, prefix);
    PettaCandidateSnapshotLease restored = take_lease(program, &space, &arena, "caller");
    assert(restored.items[0].rhs_plan->role == PETTA_PLAN_STATIC_CALL);

    /* Replacement must not reuse the previous catalog's validation stamps. */
    assert(petta_program_synchronize_space(program, &replacement));
    add(program, &replacement, &arena, "(= (caller) (second 2))");
    while (space_length64(&space))
        assert(space_remove(&space, space_get_at64(&space, 0u)));
    for (CettaIndex i = 0u; i < space_length64(&replacement); i++)
        space_add(&space, space_get_at64(&replacement, i));
    assert(petta_program_replace_space(program, &space, &replacement));
    PettaCandidateSnapshotLease replaced = take_lease(program, &space, &arena, "caller");
    assert(replaced.items[0].rhs_plan->role == PETTA_PLAN_DATA);
    assert(restored.items[0].rhs_plan->role == PETTA_PLAN_STATIC_CALL);
    add(program, &space, &arena, "(= (second $x) (+ $x 2))");
    PettaCandidateSnapshotLease latest = take_lease(program, &space, &arena, "caller");
    assert(latest.items[0].rhs_plan->role == PETTA_PLAN_STATIC_CALL);
    assert(replaced.items[0].rhs_plan->role == PETTA_PLAN_DATA);

    petta_program_candidate_snapshot_lease_release(&latest);
    petta_program_candidate_snapshot_lease_release(&replaced);
    petta_program_candidate_snapshot_lease_release(&restored);
    petta_program_candidate_snapshot_lease_release(&removed);
    petta_program_candidate_snapshot_lease_release(&saved);
    petta_program_free(program);
    space_free(&replacement);
    space_free(&space);
    arena_free(&arena);
}

int main(void) {
    SymbolTable symbols;
    VarInternTable variables;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols;
    g_var_intern = &variables;
    check(false);
    check(true);
    check_mutations();
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    puts("PASS: translation refresh preserves old leases, unrelated plans, held data and frame slots");
    return 0;
}
