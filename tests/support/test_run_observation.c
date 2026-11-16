#include "run_observation.h"
#include "term_domain.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>

static Atom *unary(Arena *arena, Atom *item) {
    return atom_expr(arena, &item, 1);
}

int main(int argc, char **argv) {
    assert(argc == 3);
    CettaTermDomain *domain = cetta_term_domain_new_default(false);
    CettaTermDomainScope scope = {0};
    assert(domain && cetta_term_domain_enter(domain, &scope));
    Arena arena;
    arena_init_detached(&arena);
    CETTA_FRAME_IDENTITY_SCOPE(frames);
    CettaFrameIdentity first_frame, second_frame;
    assert(cetta_frame_identity_scope_try(&frames, &first_frame));
    assert(cetta_frame_identity_scope_try(&frames, &second_frame));
    VarId first_id = var_epoch_id(var_id_from_base(500), first_frame);
    VarId second_id = var_epoch_id(var_id_from_base(500), second_frame);
    Atom *first = atom_var_with_id(&arena, "same-spelling", first_id);
    Atom *second = atom_var_with_id(&arena, "same-spelling", second_id);
    Atom *renamed_first = atom_var_with_id(&arena, "other-spelling", first_id);
    Atom *alias = atom_expr3(&arena, first, second, first);
    Atom *wide[257];
    for (size_t i = 0; i < 256; i++)
        wide[i] = atom_var_with_id(&arena, "same-spelling",
            var_epoch_id(var_id_from_base(1000 + i), first_frame));
    wide[256] = wide[0];
    Atom *deep = atom_symbol(&arena, "deep-leaf");
    for (size_t i = 0; i < 10000; i++) deep = unary(&arena, deep);
    Atom *values[] = {
        atom_int(&arena, 1), atom_float(&arena, 1.0),
        atom_float(&arena, -0.0), atom_float(&arena, 0.0),
        atom_bool(&arena, true), atom_symbol(&arena, "True"),
        atom_string_n(&arena, "a\0\377b", 4),
        atom_expr(&arena, NULL, 0), unary(&arena, atom_symbol(&arena, "nullary")),
        atom_expr2(&arena, atom_internal_tag(&arena, CETTA_INTERNAL_TAG_PETTA_PROLOG_COMPOUND),
                   atom_symbol(&arena, "nullary")),
        alias, atom_expr(&arena, wide, 257), deep,
    };
    RunObservation *o = run_observation_open(argv[1]);
    assert(o && run_observation_query_begin(o, 1));
    run_observation_answers(o, values, sizeof(values) / sizeof(values[0]));
    run_observation_query_end(o, RUN_REPORT_STOP_OBSERVED_COMPLETE, true, true);
    assert(run_observation_query_begin(o, 2));
    run_observation_answers(o, &renamed_first, 1);
    run_observation_query_end(o, RUN_REPORT_STOP_OBSERVED_COMPLETE, true, true);
    RunReportDocStatus complete = {.exit_code = 0, .queries_declared = 2,
                                   .queries_settled_ok = 2};
    assert(run_observation_finish(o, &complete, 0));
    run_observation_free(o);

    /* Unknown grounded values stay explicitly unobserved. The value's kind
     * is not treated as its identity or an exported authority payload. */
    Atom unknown = {0};
    unknown.kind = ATOM_GROUNDED;
    unknown.ground.gkind = GV_FOREIGN;
    Atom *opaque = &unknown;
    o = run_observation_open(argv[2]);
    assert(o && run_observation_query_begin(o, 1));
    run_observation_answers(o, &opaque, 1);
    run_observation_query_end(o, RUN_REPORT_STOP_OBSERVED_COMPLETE, true, true);
    complete.queries_declared = complete.queries_settled_ok = 1;
    assert(run_observation_finish(o, &complete, 0));
    run_observation_free(o);

    arena_free(&arena);
    cetta_term_domain_leave(&scope);
    cetta_term_domain_release(domain);
    puts("PASS: iterative structural observations, value kinds, full variable identities and unobserved values");
    return 0;
}
