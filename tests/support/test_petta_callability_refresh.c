#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static const PettaPlanNode *body_plan(const PettaCandidateSnapshotLease *lease,
                                     bool graded) {
    const PettaPlanNode *rhs = lease->items[0].rhs_plan;
    return graded ? petta_plan_child(rhs, 1u) : rhs;
}

static void check(bool live, const char *declaration, bool graded) {
    Arena arena;
    Space space;
    arena_init(&arena);
    space_init(&space);
    PettaProgram *program = petta_program_new();
    assert(program);
    if (graded)
        petta_program_enable_graded_equations(program);
    if (live)
        assert(petta_program_enable_callability_refresh(program));
    assert(petta_program_synchronize_space(program, &space));
    add(program, &space, &arena, declaration);
    add(program, &space, &arena, "(= (unrelated) (inert 1))");
    Atom *element = parse(&arena, "(future 1)");
    add_atom(program, &space, atom_expr3(&arena, atom_symbol(&arena, "="),
        parse(&arena, "(held)"), atom_list(&arena, &element, 1u)));
    PettaCandidateSnapshotLease old = take_lease(program, &space, &arena, "caller");
    PettaCandidateSnapshotLease unrelated = take_lease(program, &space, &arena, "unrelated");
    PettaCandidateSnapshotLease held = take_lease(program, &space, &arena, "held");
    const PettaPlanNode *old_plan = body_plan(&old, graded);
    assert(old_plan->role == PETTA_PLAN_DATA);
    assert(held.items[0].rhs_plan->role == PETTA_PLAN_VALUE);
    add(program, &space, &arena, "(= (elsewhere) 9)");
    PettaCandidateSnapshotLease same = take_lease(program, &space, &arena, "caller");
    assert(body_plan(&same, graded) == old_plan);
    petta_program_candidate_snapshot_lease_release(&same);

    /* Publish a new callable name. The old invocation remains intact. */
    add(program, &space, &arena, "(= (future $x) (+ $x 1))");
    PettaCandidateSnapshotLease current = take_lease(program, &space, &arena, "caller");
    assert(body_plan(&current, graded)->role ==
           (live ? PETTA_PLAN_STATIC_CALL : PETTA_PLAN_DATA));
    assert((body_plan(&current, graded) == old_plan) == !live);
    assert(body_plan(&old, graded) == old_plan && old_plan->role == PETTA_PLAN_DATA);
    assert(current.items[0].occurrence.read.instance_id == old.items[0].occurrence.read.instance_id);
    assert(current.items[0].occurrence.logical_index == old.items[0].occurrence.logical_index);
    if (graded) {
        SpaceEquationOccurrence authored;
        Atom *grade = NULL, *admission = NULL;
        assert(space_rule_occurrence_resolve_view(current.items[0].occurrence,
            CETTA_RULE_SYNTAX_ORDINARY, true, &authored));
        assert(authored.equation->expr.len == parse(&arena, declaration)->expr.len);
        assert(space_equation_view(authored.equation, true, NULL, NULL, &grade));
        if (atom_is_symbol_id(authored.equation->expr.elems[0], g_builtin_syms.equals)) {
            Atom *where = authored.equation->expr.elems[3];
            assert(where->kind == ATOM_EXPR);
            if (where->expr.len == 3u)
                admission = where->expr.elems[2];
        } else if (authored.equation->expr.len == 5u) {
            admission = authored.equation->expr.elems[4];
        }
        assert(atom_eq(grade, atom_int(&arena, 2)));
        assert((admission != NULL) == (strstr(declaration, "admit") != NULL));
        if (admission)
            assert(atom_is_symbol(space_grade_admission_predicate(admission), "allow"));
    }
    const PettaPlanNode *argument = petta_plan_child(body_plan(&current, graded), 1u);
    assert(argument && (graded || argument->has_equation_variable_slot));

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

typedef struct {
    const char *refuse_site;
    bool allow_different_rows;
    size_t atom_reads, head_bytes, scratch;
} SnapshotRead;

static bool snapshot_scalar(void *context, const char *site,
                            uint64_t left, uint64_t right) {
    SnapshotRead *read = context;
    (void)left; (void)right;
    if (!strcmp(site, "program.plan.refresh.head.byte"))
        read->head_bytes++;
    return !read->refuse_site || strcmp(site, read->refuse_site);
}

static bool snapshot_atom(void *context, const char *site,
                          const Atom *left, const Atom *right) {
    SnapshotRead *read = context;
    (void)site;
    read->atom_reads++;
    return read->allow_different_rows || left == right;
}

static bool snapshot_service(void *context, const char *site,
                             size_t work, size_t scratch) {
    SnapshotRead *read = context;
    (void)site; (void)work;
    assert(scratch <= SIZE_MAX - read->scratch);
    read->scratch += scratch;
    return true;
}

static void snapshot_release(void *context, size_t scratch) {
    SnapshotRead *read = context;
    assert(scratch <= read->scratch);
    read->scratch -= scratch;
}

static int compare_programs(PettaProgram *left, Space *left_space,
                            PettaProgram *right, Space *right_space,
                            SnapshotRead *context) {
    CettaCheckpointCompare read = {.context=context, .scalar=snapshot_scalar,
        .atom=snapshot_atom, .service=snapshot_service,
        .service_release=snapshot_release};
    int result = petta_program_snapshot_semantics_compare(
        left, left_space, right, right_space, &read);
    assert(!context->scratch);
    return result;
}

static PettaProgram *snapshot_program(Space *space, bool live) {
    PettaProgram *program = petta_program_new();
    assert(program);
    petta_program_enable_graded_equations(program);
    if (live)
        assert(petta_program_enable_callability_refresh(program));
    assert(petta_program_synchronize_space(program, space));
    return program;
}

static void check_snapshot_observation(void) {
    Arena arena;
    Space space;
    arena_init(&arena);
    space_init(&space);
    PettaProgram *left = snapshot_program(&space, true);
    add(left, &space, &arena, "(= (plain $x) (future $x))");
    add(left, &space, &arena, "(=w (graded $x) (future $x) 2)");
    add(left, &space, &arena, "(=w (guarded $x) (future $x) 2 (admit allow))");
    add(left, &space, &arena, "(= (where-graded $x) (future $x) (where 2 (admit allow)))");
    assert(petta_program_synchronize_space(left, &space));
    PettaProgram *right = snapshot_program(&space, true);
    SnapshotRead read = {0};
    assert(compare_programs(left, &space, right, &space, &read) == 1);
    /* Only authored rows must be copy-map edges. Each program constructs its
     * own lowered syntax and canonical equation-variable templates. */
    assert(read.atom_reads == space_length64(&space));
    read = (SnapshotRead){.refuse_site="program.plan.refresh.expression"};
    assert(compare_programs(left, &space, right, &space, &read) == -1);
    read = (SnapshotRead){0};
    assert(compare_programs(left, &space, right, &space, &read) == 1);
    PettaProgram *frozen = snapshot_program(&space, false);
    read = (SnapshotRead){0};
    assert(compare_programs(left, &space, frozen, &space, &read) == 0);
    petta_program_free(frozen);
    petta_program_free(right);
    petta_program_free(left);
    space_free(&space);
    arena_free(&arena);
}

static void check_unregistered_heads(bool held) {
    Arena arena;
    Space spaces[2];
    PettaProgram *programs[2];
    Atom *heads[2];
    const char *names[] = {"late-unregistered-a", "late-unregistered-b"};
    arena_init(&arena);
    for (size_t i = 0u; i < 2u; i++) {
        space_init(&spaces[i]);
        programs[i] = snapshot_program(&spaces[i], true);
        heads[i] = atom_symbol_data(&arena, names[i]);
        Atom *rhs = atom_expr2(&arena, heads[i], atom_int(&arena, 1));
        if (held)
            rhs = atom_list(&arena, &rhs, 1u);
        Atom *fields[] = {atom_symbol(&arena, "=w"),
            parse(&arena, "(latent-caller)"), rhs, atom_int(&arena, 2)};
        add_atom(programs[i], &spaces[i], atom_expr(&arena, fields, 4u));
        assert(petta_program_synchronize_space(programs[i], &spaces[i]));
        assert((atom_symbol_current_id(heads[i]) == SYMBOL_ID_NONE)
               == (CETTA_MANAGED_SYMBOLS != 0));
    }
    PettaProgram *peer = snapshot_program(&spaces[0], true);
    SnapshotRead read = {0};
    assert(compare_programs(programs[0], &spaces[0], peer, &spaces[0], &read) == 1);
    assert((read.head_bytes != 0u) == (!held && CETTA_MANAGED_SYMBOLS));
    if (!held && CETTA_MANAGED_SYMBOLS) {
        read = (SnapshotRead){.refuse_site="program.plan.refresh.head.byte"};
        assert(compare_programs(programs[0], &spaces[0], peer, &spaces[0], &read) == -1);
        read = (SnapshotRead){0};
        assert(compare_programs(programs[0], &spaces[0], peer, &spaces[0], &read) == 1);
    }
    /* A deliberately permissive row visitor isolates the refresh footprint.
     * It is not an admitted source-image relation. Equal missing IDs cannot
     * hide distinct future callable names; held values expose neither name. */
    read = (SnapshotRead){.allow_different_rows=true};
    assert(compare_programs(programs[0], &spaces[0], programs[1], &spaces[1], &read)
           == (held ? 1 : 0));
    assert((read.head_bytes != 0u) == (!held && CETTA_MANAGED_SYMBOLS));
    petta_program_free(peer);
    for (size_t i = 0u; i < 2u; i++) {
        petta_program_free(programs[i]);
        space_free(&spaces[i]);
    }
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
    check(false, "(= (caller $x) (future $x))", false);
    check(true, "(= (caller $x) (future $x))", false);
    check(false, "(=w (caller $x) (future $x) 2)", true);
    check(true, "(=w (caller $x) (future $x) 2)", true);
    check(true, "(=w (caller $x) (future $x) 2 (admit allow))", true);
    check(true, "(= (caller $x) (future $x) (where 2 (admit allow)))", true);
    check_mutations();
    check_snapshot_observation();
    check_unregistered_heads(false);
    check_unregistered_heads(true);
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    puts("PASS: translation refresh preserves graded source identity, old leases and frame slots; snapshot reads preserve modes, latent names, opacity and refusal");
    return 0;
}
