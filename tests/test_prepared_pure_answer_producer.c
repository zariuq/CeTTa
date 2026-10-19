#include "eval.h"
#include "parser.h"
#include "petta_semantics.h"
#include "prepared_pure_machine.h"
#include "stats.h"
#include "symbol.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Arena source;
    Arena scratch;
    TermUniverse universe;
    Space space;
    CettaPreparedPureProgram *program;
    CettaPreparedPureAnswerLimits limits;
    size_t answers;
    size_t stop_after;
    size_t polls;
    size_t poll_limit;
    CettaPreparedPureUnmatchedCall unmatched_call;
    const char *expected[4];
} Fixture;

static Atom *parse(Arena *arena, const char *text) {
    size_t position = 0u;
    Atom *atom = parse_sexpr(arena, text, &position);
    assert(atom);
    return atom;
}

static void init_with_view(Fixture *fixture, const char *const *equations,
                           size_t count, const char *call,
                           CettaPreparedPureExpressionViewFn expression_view) {
    memset(fixture, 0, sizeof(*fixture));
    fixture->unmatched_call = CETTA_PREPARED_PURE_UNMATCHED_DECLINES;
    fixture->limits = (CettaPreparedPureAnswerLimits){
        .max_transitions = 1024u,
        .max_scratch_bytes = 1024u * 1024u,
        .construct_allocation_bound = atom_expr_allocation_bound,
    };
    arena_init(&fixture->source);
    arena_init(&fixture->scratch);
    arena_set_hashcons(&fixture->source, NULL);
    arena_set_hashcons(&fixture->scratch, NULL);
    term_universe_init(&fixture->universe);
    term_universe_set_persistent_arena(&fixture->universe, &fixture->source);
    space_init_with_universe(&fixture->space, &fixture->universe);
    for (size_t index = 0u; index < count; index++)
        assert(space_admit_atom(&fixture->space, &fixture->source,
                               parse(&fixture->source, equations[index])));
    fixture->program = cetta_prepared_pure_program_compile_closed_answers(
        &fixture->space, parse(&fixture->source, call),
        CETTA_GSLT_PURE_CALL_EAGER, atom_bool, atom_expr,
        cetta_prepared_pure_constructs_expression_always,
        NULL, NULL, expression_view, NULL, NULL, true, true,
        (CettaMatchDecisionSemanticIdentity){
            .compiler_identity = cetta_match_decision_compiler_identity(),
        });
    if (!fixture->program) {
        fprintf(stderr, "answer producer compilation declined: %s\n", call);
        Atom *entry = parse(&fixture->source, call);
        bool defined = false;
        CettaGsltQueryEffect effect = space_query_effect_for_head(
            &fixture->space, atom_symbol_current_id(entry->expr.elems[0]), &defined);
        fprintf(stderr, "defined=%d effect=%u vars=%d arrow=%d\n",
                defined, (unsigned)effect, atom_has_vars(entry),
                space_head_has_arrow_signature(&fixture->space,
                    atom_symbol_current_id(entry->expr.elems[0]), entry->expr.len - 1u));
        abort();
    }
}

static void init(Fixture *fixture, const char *const *equations,
                 size_t count, const char *call) {
    init_with_view(fixture, equations, count, call, NULL);
}

static void destroy(Fixture *fixture) {
    cetta_prepared_pure_program_free(fixture->program);
    space_free(&fixture->space);
    term_universe_free(&fixture->universe);
    arena_free(&fixture->scratch);
    arena_free(&fixture->source);
}

static bool visit(Atom *answer, void *context) {
    Fixture *fixture = context;
    assert(answer);
    if (fixture->answers < 4u && fixture->expected[fixture->answers]) {
        char *text = atom_to_string(&fixture->source, answer);
        assert(text && strcmp(text, fixture->expected[fixture->answers]) == 0);
    }
    fixture->answers++;
    return fixture->stop_after == 0u || fixture->answers < fixture->stop_after;
}

static bool interrupt(void *context) {
    Fixture *fixture = context;
    fixture->polls++;
    return fixture->poll_limit > 0u && fixture->polls >= fixture->poll_limit;
}

static CettaPreparedPureAnswersResult run(Fixture *fixture,
                                         uint64_t *tail_calls) {
    uint64_t answers = UINT64_MAX;
    CettaPreparedPureAnswersResult result =
        cetta_prepared_pure_program_visit_closed_answers(
            fixture->program, &fixture->scratch, &fixture->limits,
            fixture->unmatched_call, visit, fixture,
            interrupt, fixture, &answers, tail_calls);
    assert(answers == fixture->answers);
    return result;
}

static void test_completed_occurrences(void) {
    const char *equations[] = {
        "(= (pick (Cons $x $xs)) $x)",
        "(= (pick (Cons $x (Cons $y $ys))) (pick (Cons $y $ys)))",
    };
    Fixture fixture;
    init(&fixture, equations, 2u, "(pick (Cons same (Cons same (Cons last Nil))))");
    fixture.expected[0] = "same";
    fixture.expected[1] = "same";
    fixture.expected[2] = "last";
    uint64_t tails = 0u;
    assert(run(&fixture, &tails) == CETTA_PREPARED_PURE_ANSWERS_COMPLETE);
    assert(fixture.answers == 3u && tails == 2u);
    destroy(&fixture);
}

static void test_late_decline_is_not_completion(void) {
    const char *equations[] = {
        "(= (pick (Cons $x $xs)) $x)",
        "(= (pick (Cons $x $xs)) $x)",
        "(= (pick (Cons $x $xs)) (pick $xs))",
    };
    Fixture fixture;
    init(&fixture, equations, 3u, "(pick (Cons same Nil))");
    fixture.expected[0] = fixture.expected[1] = "same";
    uint64_t tails = 0u;
    assert(run(&fixture, &tails) == CETTA_PREPARED_PURE_ANSWERS_DECLINED);
    assert(fixture.answers == 2u && tails == 1u);
    destroy(&fixture);
}

static void test_visitor_stop_and_reentry(void) {
    const char *equations[] = {
        "(= (pick $x) first)", "(= (pick $x) second)",
    };
    Fixture fixture;
    init(&fixture, equations, 2u, "(pick seed)");
    fixture.expected[0] = "first";
    fixture.stop_after = 1u;
    uint64_t tails = 0u;
    assert(run(&fixture, &tails) == CETTA_PREPARED_PURE_ANSWERS_STOPPED);
    assert(fixture.answers == 1u && tails == 0u);
    fixture.answers = fixture.stop_after = 0u;
    fixture.expected[1] = "second";
    assert(run(&fixture, &tails) == CETTA_PREPARED_PURE_ANSWERS_COMPLETE);
    assert(fixture.answers == 2u);
    destroy(&fixture);
}

static void test_interrupt_without_answers(void) {
    const char *equations[] = {"(= (spin $x) (spin $x))"};
    Fixture fixture;
    init(&fixture, equations, 1u, "(spin seed)");
    fixture.poll_limit = 31u;
    uint64_t tails = 0u;
    assert(run(&fixture, &tails) == CETTA_PREPARED_PURE_ANSWERS_STOPPED);
    assert(fixture.answers == 0u && tails == 30u);
    assert(fixture.polls == fixture.poll_limit);
    destroy(&fixture);
}

static void test_scratch_ownership_after_interrupted_growth(void) {
    const char *equations[] = {
        "(= (grow $x) (Box $x))", "(= (grow $x) (grow (Box $x)))",
    };
    Fixture fixture;
    init(&fixture, equations, 2u, "(grow seed)");
    ArenaMark anchor = arena_mark(&fixture.scratch);
    fixture.poll_limit = 129u;
    uint64_t tails = 0u;
    assert(run(&fixture, &tails) == CETTA_PREPARED_PURE_ANSWERS_STOPPED);
    assert(fixture.answers == 64u && tails == 64u);
    assert(arena_accounted_live_bytes(&fixture.scratch) >
           arena_mark_accounted_live_bytes(anchor));
    /* The visitor borrows its arena.  The caller owns reclamation after
     * either completion or interruption, even when it retains no answers. */
    arena_reset(&fixture.scratch, anchor);
    assert(arena_accounted_live_bytes(&fixture.scratch) ==
           arena_mark_accounted_live_bytes(anchor));
    fixture.answers = fixture.polls = 0u;
    fixture.poll_limit = 3u;
    assert(run(&fixture, &tails) == CETTA_PREPARED_PURE_ANSWERS_STOPPED);
    assert(fixture.answers == 1u && tails == 1u);
    destroy(&fixture);
}

static void test_transition_limit_without_answers(void) {
    const char *equations[] = {"(= (spin $x) (spin $x))"};
    Fixture fixture;
    init(&fixture, equations, 1u, "(spin seed)");
    fixture.limits.max_transitions = 31u;
    uint64_t tails = 0u;
    assert(run(&fixture, &tails) == CETTA_PREPARED_PURE_ANSWERS_LIMIT);
    assert(fixture.answers == 0u && tails == 31u);
    fixture.limits.max_transitions = 0u;
    assert(run(&fixture, &tails) == CETTA_PREPARED_PURE_ANSWERS_LIMIT);
    assert(fixture.answers == 0u && tails == 0u);
    destroy(&fixture);
}

static void test_allocation_limit_before_construction(void) {
    const char *equations[] = {
        "(= (grow $x) (Box $x))", "(= (grow $x) (grow (Box $x)))",
    };
    Fixture fixture;
    init(&fixture, equations, 2u, "(grow seed)");
    size_t one_constructor = 0u;
    assert(atom_expr_allocation_bound(2u, &one_constructor));
    fixture.limits.max_scratch_bytes = one_constructor;
    uint64_t tails = 0u;
    assert(run(&fixture, &tails) == CETTA_PREPARED_PURE_ANSWERS_LIMIT);
    assert(fixture.answers == 1u && tails == 0u);
    assert(arena_accounted_live_bytes(&fixture.scratch) == one_constructor);
    destroy(&fixture);
}

static void test_constructor_cost_adapters(void) {
    Arena arena;
    arena_init(&arena);
    arena_set_hashcons(&arena, NULL);
    Atom *children[3] = {
        atom_symbol(&arena, "cons"), atom_symbol(&arena, "head"),
        atom_symbol(&arena, "Nil"),
    };
    size_t before = arena_accounted_live_bytes(&arena);
    size_t native_bound = 0u;
    assert(atom_expr_allocation_bound(3u, &native_bound));
    assert(atom_expr(&arena, children, 3u));
    assert(arena_accounted_live_bytes(&arena) - before == native_bound);
    size_t petta_bound = 0u;
    assert(petta_semantics_construct_value_allocation_bound(3u, &petta_bound));
    before = arena_accounted_live_bytes(&arena);
    Atom *value = petta_semantics_construct_value(&arena, children, 3u);
    assert(value && petta_semantics_is_open_cons_value(value));
    assert(arena_accounted_live_bytes(&arena) - before <= petta_bound);
    assert(!atom_expr_allocation_bound(UINT64_MAX, &native_bound));
    arena_free(&arena);
}

static CettaPreparedPureExpressionViewState entry_interpretation;
static CettaPreparedPureProjectionMode entry_projection_mode;
static SymbolId entry_view_head;

static CettaPreparedPureExpressionViewState entry_view(
    Space *space, const Atom *expression, CettaPreparedPureExpressionView *view) {
    (void)space;
    if (expression->kind != ATOM_EXPR || expression->expr.len != 2u ||
        !atom_is_symbol_id(expression->expr.elems[0], entry_view_head))
        return CETTA_PREPARED_PURE_EXPRESSION_DEFAULT;
    view->projected = expression->expr.elems[1];
    view->projection_mode = entry_projection_mode;
    return entry_interpretation;
}

static void test_entry_interpretation_precedes_equations(void) {
    const char *equations[] = {
        "(= (entry $x) wrong)", "(= (payload $x) evaluated)",
    };
    Fixture fixture;
    init(&fixture, equations, 2u, "(entry seed)");
    Atom *call = parse(&fixture.source, "(entry (payload seed))");
    entry_view_head = atom_symbol_current_id(call->expr.elems[0]);
    const CettaPreparedPureExpressionViewState refusals[] = {
        CETTA_PREPARED_PURE_EXPRESSION_DECLINE,
        CETTA_PREPARED_PURE_EXPRESSION_CANONICAL_ONLY,
        CETTA_PREPARED_PURE_EXPRESSION_ZERO,
        CETTA_PREPARED_PURE_EXPRESSION_PROJECT_SINGLE_RESULT,
    };
    for (size_t i = 0u; i < sizeof(refusals) / sizeof(refusals[0]); i++) {
        entry_interpretation = refusals[i];
        CettaPreparedPureProgram *program =
            cetta_prepared_pure_program_compile_closed_answers(
                &fixture.space, call, CETTA_GSLT_PURE_CALL_EAGER,
                atom_bool, atom_expr,
                cetta_prepared_pure_constructs_expression_always,
                NULL, NULL, entry_view,
                NULL, NULL, true, true,
                (CettaMatchDecisionSemanticIdentity){0});
        assert(!program);
    }
    entry_interpretation = CETTA_PREPARED_PURE_EXPRESSION_PROJECT;
    const CettaGsltPureCallMode modes[] = {
        CETTA_GSLT_PURE_CALL_EAGER, CETTA_GSLT_PURE_CALL_CALL_BY_NEED,
    };
    for (size_t mode = 0u; mode < 2u; mode++) {
        for (unsigned ready = 0u; ready < 2u; ready++) {
            CettaPreparedPureProgram *program =
                cetta_prepared_pure_program_compile_closed(
                    &fixture.space, call, modes[mode],
                    atom_bool, atom_expr,
                    cetta_prepared_pure_constructs_expression_always,
                    NULL, NULL, entry_view,
                    NULL, NULL, ready != 0u, true,
                    (CettaMatchDecisionSemanticIdentity){0});
            assert(program);
            assert(!cetta_prepared_pure_closed_open_optional(
                program, &fixture.scratch, &fixture));
            Atom *answer = NULL;
            assert(cetta_prepared_pure_program_execute_closed(
                program, &fixture.scratch, 0u, &answer));
            Atom *expected = ready
                ? call->expr.elems[1] : parse(&fixture.source, "evaluated");
            assert(answer && atom_eq(answer, expected));
            if (ready) {
                Atom *next = parse(&fixture.source, "(entry (payload next))");
                assert(cetta_prepared_pure_program_rebind_closed_entry_call(
                    program, next));
                assert(cetta_prepared_pure_program_execute_closed(
                    program, &fixture.scratch, 0u, &answer));
                assert(answer && atom_eq(answer, next->expr.elems[1]));
            }
            cetta_prepared_pure_program_free(program);
        }
    }
    /* An explicitly evaluated entry is not an identity on a ready argument.
     * The outer payload call still runs, even with ready entry registers. */
    const CettaPreparedPureProjectionMode projections[] = {
        CETTA_PREPARED_PURE_PROJECT_APPLICATION,
        CETTA_PREPARED_PURE_PROJECT_CODE,
    };
    for (size_t i = 0u; i < sizeof(projections) / sizeof(projections[0]); i++) {
        entry_projection_mode = projections[i];
        CettaPreparedPureProgram *program =
            cetta_prepared_pure_program_compile_closed(
                &fixture.space, call, CETTA_GSLT_PURE_CALL_EAGER,
                atom_bool, atom_expr,
                cetta_prepared_pure_constructs_expression_always,
                NULL, NULL, entry_view,
                NULL, NULL, true, true,
                (CettaMatchDecisionSemanticIdentity){0});
        assert(program);
        Atom *answer = NULL;
        assert(cetta_prepared_pure_program_execute_closed(
            program, &fixture.scratch, 0u, &answer));
        assert(answer && atom_eq(answer, parse(&fixture.source, "evaluated")));
        cetta_prepared_pure_program_free(program);
    }
    entry_projection_mode = CETTA_PREPARED_PURE_PROJECT_SOURCE;
    destroy(&fixture);
}

/* A completed graph may share inert nodes, but two occurrences of one
 * suspended call need a virtual cell at publication. Pointer sharing alone
 * is not that cell; an inert shared parent cannot hide its occurrences. */
static void test_shared_suspension_publication(void) {
    const char *equations[] = {
        "(= (single $x) (Pair $x))",
        "(= (duplicate $x) (Pair $x $x))",
        "(= (duplicate-box $x) (let $box (Box $x) (Pair $box $box)))",
    };
    Fixture fixture;
    init(&fixture, equations, 3u, "(single seed)");
    const char *calls[] = {
        "(single (+ 1 2))",
        "(duplicate (+ 1 2))",
        "(duplicate-box (+ 1 2))",
    };
    for (size_t i = 0u; i < sizeof(calls) / sizeof(calls[0]); i++) {
        CettaPreparedPureProgram *program =
            cetta_prepared_pure_program_compile_closed(
                &fixture.space, parse(&fixture.source, calls[i]),
                CETTA_GSLT_PURE_CALL_CALL_BY_NEED,
                atom_bool, atom_expr,
                cetta_prepared_pure_constructs_expression_always,
                NULL, NULL, NULL, NULL, NULL, false, true,
                (CettaMatchDecisionSemanticIdentity){
                    .compiler_identity = cetta_match_decision_compiler_identity(),
                });
        assert(program);
        Atom *answer = NULL;
        bool completed = cetta_prepared_pure_program_execute_closed(
            program, &fixture.scratch, 0u, &answer);
        if (i == 0u) {
            assert(completed && answer);
            assert(atom_eq(answer, parse(&fixture.source, "(Pair (+ 1 2))")));
        } else {
            assert(!completed);
        }
        cetta_prepared_pure_program_free(program);
    }
    destroy(&fixture);
    puts("PASS: shared suspended occurrences require a virtual publication cell");
}

/* Private single-result execution has its own admission and nursery contract.
 * A frontend that transports an invocation need not enter the Atom-only ABI
 * to exercise that compiler or its Need update cells. */
static void test_closed_need_admission_and_collection(void) {
    const char *equations[] = {
        "(= (entry) seed)",
        "(= (vm-pick (Cons $x $xs)) $x)",
        "(= (vm-pick (Cons $x (Cons $y $ys))) $y)",
        "(= (vm-select (Box A)) first)",
        "(= (vm-select (Box B)) second)",
        "(= (vm-inc $x) (+ $x 1))",
        "(= (vm-add $x $y) (+ $x $y))",
        "(= (vm-twice $x) (vm-add $x $x))",
        "(= (vm-delay-loop $n $value)"
        "   (if (== $n 0) $value"
        "       (vm-delay-loop (- $n 1) (vm-inc $value))))",
    };
    Fixture fixture;
    init(&fixture, equations, sizeof(equations) / sizeof(equations[0]),
         "(entry)");
    const char *calls[] = {
        "(vm-pick (Cons 2 (Cons 1 Nil)))",
        "(vm-select (Box B))",
        "(vm-twice (vm-inc 10))",
        "(vm-delay-loop 4000 0)",
    };
    for (size_t i = 0u; i < sizeof(calls) / sizeof(calls[0]); i++) {
        CettaPreparedPureProgram *program =
            cetta_prepared_pure_program_compile_closed(
                &fixture.space, parse(&fixture.source, calls[i]),
                CETTA_GSLT_PURE_CALL_CALL_BY_NEED,
                atom_bool, atom_expr,
                cetta_prepared_pure_constructs_expression_always,
                NULL, NULL, NULL, NULL, NULL, false, true,
                (CettaMatchDecisionSemanticIdentity){
                    .compiler_identity = cetta_match_decision_compiler_identity(),
                });
        if (i < 2u) {
            assert(!program);
            continue;
        }
        assert(program);
        Atom *expected = parse(&fixture.source, i == 2u ? "22" : "4000");
        for (unsigned collect = 0u; collect < 2u; collect++) {
            Arena scratch;
            arena_init_detached(&scratch);
#if CETTA_BUILD_WITH_RUNTIME_STATS
            cetta_runtime_stats_reset();
            cetta_runtime_stats_enable();
#endif
            Atom *answer = NULL;
            assert(cetta_prepared_pure_program_execute_closed(
                program, &scratch, collect ? 16u * 1024u : 0u, &answer));
            assert(answer && atom_eq(answer, expected));
#if CETTA_BUILD_WITH_RUNTIME_STATS
            CettaRuntimeStats stats;
            cetta_runtime_stats_snapshot(&stats);
            cetta_runtime_stats_disable();
            assert(stats.counters[
                CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_THUNK_MEMO_STORE] > 0u);
            if (i == 2u)
                assert(stats.counters[
                    CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_THUNK_MEMO_HIT] > 0u);
            if (i == 3u) {
                assert(stats.counters[
                    CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_TAIL_REENTRY] > 0u);
                if (collect) {
                    assert(stats.counters[
                        CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_THUNK_PATH_COMPRESSION] > 0u);
                    assert(stats.counters[
                        CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_GC_COLLECTION] > 0u);
                    assert(stats.counters[
                        CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_EPHEMERON_RECLAIMED] > 0u);
                }
            }
            assert(stats.counters[
                CETTA_RUNTIME_COUNTER_PREPARED_PURE_CALL_THUNK_BLACKHOLE] == 0u);
#endif
            arena_free(&scratch);
        }
        cetta_prepared_pure_program_free(program);
    }
    destroy(&fixture);
    puts("PASS: private Need compilation refuses ambiguous heads and preserves shared updates across collection");
}

/* Enabling retained services adds origin tracking, but it must not discard
 * the embedding evaluator's checked fact that the closed entry arguments are
 * already values.  In particular, a structured value remains available to a
 * structural let instead of becoming a fresh demand with no projected
 * origin. */
static void test_entry_value_certificate_survives_service_roles(void) {
    const char *equations[] = {
        "(= (prepared-rigid $x $pair)"
        "   (let (Pair $x $y) $pair (Rigid $x $y)))",
    };
    Fixture fixture;
    init(&fixture, equations, 1u,
         "(prepared-rigid same (Pair same tail))");
    cetta_prepared_pure_program_free(fixture.program);
    fixture.program = cetta_prepared_pure_program_compile_closed(
        &fixture.space,
        parse(&fixture.source, "(prepared-rigid same (Pair same tail))"),
        CETTA_GSLT_PURE_CALL_CALL_BY_NEED,
        atom_bool, atom_expr,
        cetta_prepared_pure_constructs_expression_always,
        NULL, NULL, NULL, NULL, NULL,
        true, true,
        (CettaMatchDecisionSemanticIdentity){
            .compiler_identity = cetta_match_decision_compiler_identity(),
        });
    assert(fixture.program);
    CettaPreparedPureClosedExecution *execution =
        cetta_prepared_pure_closed_open(
            fixture.program, &fixture.scratch, &fixture);
    assert(execution);
    assert(cetta_prepared_pure_closed_enable_service(execution, &fixture));
    CettaPreparedClosedState before, after;
    assert(cetta_prepared_pure_closed_read(execution, &before));
    CettaOwnedExecution plain_owner={0};
    assert(!cetta_prepared_pure_closed_own_execution(&plain_owner,execution,&fixture,1u));
    assert(!plain_owner.computation);
    Atom *replacement = parse(
        &fixture.source, "(prepared-rigid next (Pair next later))");
    assert(!cetta_prepared_pure_program_rebind_closed_entry_call(
        fixture.program, replacement));
    assert(cetta_prepared_pure_closed_read(execution, &after));
    assert(after.status == before.status && after.performed == before.performed &&
           after.frames == before.frames && after.values == before.values &&
           after.frame_storage == before.frame_storage);
    uint64_t spent = 0u, total = 0u;
    Atom *answer = NULL;
    CettaPreparedClosedStep status = CETTA_PREPARED_CLOSED_SUSPENDED;
    for (size_t slice = 0u;
         slice < 1024u && status == CETTA_PREPARED_CLOSED_SUSPENDED;
         slice++) {
        status = cetta_prepared_pure_closed_step(
            execution, &fixture.scratch, &fixture, 1u, &spent, &answer);
        assert(spent <= 1u);
        total += spent;
    }
    assert(status == CETTA_PREPARED_CLOSED_ANSWER && total > 0u && answer);
    assert(atom_eq(answer, parse(&fixture.source, "(Rigid same tail)")));
    assert(cetta_prepared_pure_closed_close(execution, &fixture));
    assert(cetta_prepared_pure_program_rebind_closed_entry_call(
        fixture.program, replacement));
    execution = cetta_prepared_pure_closed_open(
        fixture.program, &fixture.scratch, &fixture);
    assert(execution);
    assert(cetta_prepared_pure_closed_enable_service(execution, &fixture));
    answer = NULL;
    status = CETTA_PREPARED_CLOSED_SUSPENDED;
    for (size_t slice = 0u;
         slice < 1024u && status == CETTA_PREPARED_CLOSED_SUSPENDED;
         slice++)
        status = cetta_prepared_pure_closed_step(
            execution, &fixture.scratch, &fixture, 1u, &spent, &answer);
    assert(status == CETTA_PREPARED_CLOSED_ANSWER && answer);
    assert(atom_eq(answer, parse(&fixture.source, "(Rigid next later)")));
    assert(cetta_prepared_pure_closed_close(execution, &fixture));
    destroy(&fixture);

    /* A false certificate retains demand: the callable structured argument
     * is evaluated before the entry projection observes it. */
    const char *projected_equations[] = {
        "(= (entry $x) (Box $x))", "(= (payload $x) evaluated)",
    };
    init(&fixture, projected_equations, 2u, "(entry seed)");
    cetta_prepared_pure_program_free(fixture.program);
    Atom *projected_call = parse(
        &fixture.source, "(entry (payload source))");
    fixture.program = cetta_prepared_pure_program_compile_closed(
        &fixture.space, projected_call,
        CETTA_GSLT_PURE_CALL_CALL_BY_NEED,
        atom_bool, atom_expr,
        cetta_prepared_pure_constructs_expression_always,
        NULL, NULL, NULL, NULL, NULL,
        false, true,
        (CettaMatchDecisionSemanticIdentity){
            .compiler_identity = cetta_match_decision_compiler_identity(),
        });
    assert(fixture.program);
    execution = cetta_prepared_pure_closed_open(
        fixture.program, &fixture.scratch, &fixture);
    assert(execution);
    assert(cetta_prepared_pure_closed_enable_service(execution, &fixture));
    answer = NULL;
    status = CETTA_PREPARED_CLOSED_SUSPENDED;
    for (size_t slice = 0u;
         slice < 1024u && status == CETTA_PREPARED_CLOSED_SUSPENDED;
         slice++)
        status = cetta_prepared_pure_closed_step(
            execution, &fixture.scratch, &fixture, 1u, &spent, &answer);
    assert(status == CETTA_PREPARED_CLOSED_SERVICE && !answer);
    CettaPreparedServiceRequest request;
    assert(cetta_prepared_pure_closed_service_request(
        execution, &fixture, &request));
    assert(request.call && request.operand_count == 0u &&
           request.ready_arguments == 0u);
    assert(atom_eq(request.call,
                   parse(&fixture.source, "(Box (payload source))")));
    assert(cetta_prepared_pure_closed_close(execution, &fixture));
    destroy(&fixture);
}

/* A handler can be projected only while the entire pure attempt remains
 * private. A streamed producer must not lose it when exporting alternatives. */
static void test_single_result_projection(void) {
    const char *equations[] = {
        "(= (calc $x) (+ $x 1))",
        "(= (guarded $x) (shield (calc $x)))",
    };
    Fixture fixture;
    init(&fixture, equations, 2u, "(calc 7)");
    entry_view_head = symbol_intern_cstr(g_symbols, "shield");
    entry_interpretation =
        CETTA_PREPARED_PURE_EXPRESSION_PROJECT_SINGLE_RESULT;
    Atom *call = parse(&fixture.source, "(guarded 7)");
    CettaPreparedPureProgram *program =
        cetta_prepared_pure_program_compile_closed(
            &fixture.space, call, CETTA_GSLT_PURE_CALL_EAGER,
            atom_bool, atom_expr,
            cetta_prepared_pure_constructs_expression_always,
            NULL, NULL, entry_view,
            NULL, NULL, true, true,
            (CettaMatchDecisionSemanticIdentity){0});
    assert(program);
    Atom *answer = NULL;
    assert(cetta_prepared_pure_program_execute_closed(
        program, &fixture.scratch, 0u, &answer));
    assert(answer && atom_eq(answer, parse(&fixture.source, "8")));
    CettaPreparedPureProgram *producer =
        cetta_prepared_pure_program_compile_closed_answers(
            &fixture.space, call, CETTA_GSLT_PURE_CALL_EAGER,
            atom_bool, atom_expr,
            cetta_prepared_pure_constructs_expression_always,
            NULL, NULL, entry_view,
            NULL, NULL, true, true,
            (CettaMatchDecisionSemanticIdentity){0});
    assert(!producer);
    /* An invalid operand aborts the private attempt; the original source
     * and its handler remain the fallback authority. */
    assert(cetta_prepared_pure_program_rebind_closed_entry_call(
        program, parse(&fixture.source, "(guarded not-a-number)")));
    answer = NULL;
    assert(!cetta_prepared_pure_program_execute_closed(
        program, &fixture.scratch, 0u, &answer));
    assert(!answer);
    cetta_prepared_pure_program_free(program);
    destroy(&fixture);

    /* The single-result executor cannot silently choose one of several
     * answers, including when the other alternative would raise. */
    const char *ambiguous[] = {
        "(= (calc $x) (+ $x 1))",
        "(= (calc $x) (/ $x 0))",
        "(= (guarded $x) (shield (calc $x)))",
    };
    init(&fixture, ambiguous, 3u, "(calc 7)");
    program = cetta_prepared_pure_program_compile_closed(
        &fixture.space, parse(&fixture.source, "(guarded 7)"),
        CETTA_GSLT_PURE_CALL_EAGER, atom_bool, atom_expr,
        cetta_prepared_pure_constructs_expression_always,
        NULL, NULL, entry_view, NULL, NULL, true, true,
        (CettaMatchDecisionSemanticIdentity){0});
    assert(!program);
    destroy(&fixture);
}

static void inspect_cursor_storage(Fixture *fixture, CettaPreparedPureAnswerCursor *cursor) {
    size_t polls=fixture->polls;
    uint64_t answers=cetta_prepared_pure_answer_cursor_answer_count(cursor);
    uint64_t tails=cetta_prepared_pure_answer_cursor_tail_call_count(cursor);
    size_t frames=cetta_prepared_pure_answer_cursor_frame_count(cursor);
    CettaPreparedPureCursorStorage storage=cetta_prepared_pure_answer_cursor_storage(cursor);
    CettaStorageCensus census={0};
    cetta_prepared_pure_answer_cursor_storage_census(cursor,&census);cetta_storage_finish(&census);
    assert(!census.incomplete && !census.invalidated && !census.overflow &&
        census.retained>=storage.metadata_bytes+storage.reserved_bytes);
    CettaStorageCensus program={0};
    cetta_prepared_pure_program_storage_census(fixture->program,&program);cetta_storage_finish(&program);
    assert(!program.incomplete && census.bytes==program.bytes+storage.metadata_bytes+storage.live_bytes);
    cetta_storage_census_free(&program);
    size_t retained=census.retained,objects=census.length;
    cetta_prepared_pure_answer_cursor_storage_census(cursor,&census);
    cetta_prepared_pure_program_storage_census(fixture->program,&census);
    cetta_storage_finish(&census);
    assert(!census.incomplete && census.retained==retained && census.length==objects &&
        census.repeated>0u && fixture->polls==polls &&
        cetta_prepared_pure_answer_cursor_answer_count(cursor)==answers &&
        cetta_prepared_pure_answer_cursor_tail_call_count(cursor)==tails &&
        cetta_prepared_pure_answer_cursor_frame_count(cursor)==frames);
    cetta_storage_census_free(&census);
}

static CettaPreparedPureAnswerCursor *open_cursor(
    Fixture *fixture, bool no_match_declines) {
    CettaPreparedPureAnswerCursorOptions options = {
        .limits = fixture->limits,
        .unmatched_call = no_match_declines
            ? CETTA_PREPARED_PURE_UNMATCHED_DECLINES
            : CETTA_PREPARED_PURE_UNMATCHED_FAILS,
        .interrupt_poll = interrupt,
        .interrupt_context = fixture,
        .interrupt_poll_interval = 1u,
        .allow_continuations = true,
    };
    options.limits.max_transitions = UINT64_MAX;
    CettaPreparedPureAnswerCursor *cursor =
        cetta_prepared_pure_answer_cursor_open(fixture->program, &options);
    assert(cursor);
    inspect_cursor_storage(fixture,cursor);
    return cursor;
}

static void expect_answer(Fixture *fixture,
                          CettaPreparedPureAnswerCursor *cursor,
                          const char *expected) {
    Atom *answer = NULL;
    assert(cetta_prepared_pure_answer_cursor_next(cursor, &answer) ==
           CETTA_PREPARED_PURE_CURSOR_ANSWER);
    char *text = atom_to_string(&fixture->source, answer);
    assert(text && strcmp(text, expected) == 0);
    inspect_cursor_storage(fixture,cursor);
}

static void expect_frame(Fixture *fixture,
                         const CettaPreparedPureAnswerCursor *cursor,
                         size_t index, const char *call,
                         uint32_t next_ordinal) {
    CettaPreparedPureAnswerFrame frame;
    assert(cetta_prepared_pure_answer_cursor_frame(cursor, index, &frame));
    Atom *expected = parse(&fixture->source, call);
    assert(atom_is_symbol_id(expected->expr.elems[0], frame.head));
    assert(frame.arity + 1u == expected->expr.len);
    for (uint32_t i = 0u; i < frame.arity; i++)
        assert(atom_eq(frame.arguments[i], expected->expr.elems[i + 1u]));
    assert(frame.next_ordinal == next_ordinal);
    assert((frame.next_ordinal < frame.equation_count) ==
           (frame.next_equation != NULL));
}

static void expect_zero_advance(Fixture *fixture,
                                CettaPreparedPureAnswerCursor *cursor) {
    size_t polls = fixture->polls;
    size_t frames = cetta_prepared_pure_answer_cursor_frame_count(cursor);
    uint64_t answers = cetta_prepared_pure_answer_cursor_answer_count(cursor);
    uint64_t tails = cetta_prepared_pure_answer_cursor_tail_call_count(cursor);
    uint64_t spent = UINT64_MAX;
    Atom *answer = (Atom *)cursor;
    CettaPreparedPureCursorStep step = cetta_prepared_pure_answer_cursor_advance(
        cursor, 0u, &spent, &answer);
    assert(step == (frames == 0u ? CETTA_PREPARED_PURE_CURSOR_EXHAUSTED
                                : CETTA_PREPARED_PURE_CURSOR_PAUSED));
    assert(spent == 0u && !answer && fixture->polls == polls &&
           cetta_prepared_pure_answer_cursor_frame_count(cursor) == frames &&
           cetta_prepared_pure_answer_cursor_answer_count(cursor) == answers &&
           cetta_prepared_pure_answer_cursor_tail_call_count(cursor) == tails);
}

static void test_cursor_bounded_continuations(void) {
    const char *equations[] = {
        "(= (coin) h)", "(= (coin) t)",
        "(= (toss) (Toss (coin) (coin)))",
    };
    const uint64_t quanta[] = {1u, 2u, 7u};
    for (size_t i = 0u; i < sizeof(quanta) / sizeof(quanta[0]); ++i) {
        Fixture fixture;
        init(&fixture, equations, 3u, "(toss)");
        fixture.expected[0] = "(Toss h h)";
        fixture.expected[1] = "(Toss h t)";
        fixture.expected[2] = "(Toss t h)";
        fixture.expected[3] = "(Toss t t)";
        CettaPreparedPureAnswerCursor *cursor = open_cursor(&fixture, false);
        expect_zero_advance(&fixture, cursor);
        bool exhausted = false;
        for (unsigned slice = 0u; slice < 128u && !exhausted; ++slice) {
            uint64_t spent = UINT64_MAX;
            Atom *answer = NULL;
            CettaPreparedPureCursorStep step = cetta_prepared_pure_answer_cursor_advance(
                cursor, quanta[i], &spent, &answer);
            assert(spent <= quanta[i]);
            if (step == CETTA_PREPARED_PURE_CURSOR_ANSWER) {
                assert(visit(answer, &fixture));
                /* A zero grant must not release the borrowed row. */
                expect_zero_advance(&fixture, cursor);
                char *text = atom_to_string(&fixture.source, answer);
                assert(strcmp(text, fixture.expected[fixture.answers - 1u]) == 0);
            } else if (step == CETTA_PREPARED_PURE_CURSOR_PAUSED) {
                assert(spent == quanta[i] && !answer);
                expect_zero_advance(&fixture, cursor);
            } else {
                assert(step == CETTA_PREPARED_PURE_CURSOR_EXHAUSTED && !answer);
                exhausted = true;
            }
        }
        assert(exhausted && fixture.answers == 4u);
        expect_zero_advance(&fixture, cursor);
        inspect_cursor_storage(&fixture, cursor);
        cetta_prepared_pure_answer_cursor_close(cursor);
        destroy(&fixture);
    }

    const char *single[] = {"(= (one) (Box kept))"};
    Fixture fixture;
    init(&fixture, single, 1u, "(one)");
    CettaPreparedPureAnswerCursor *cursor = open_cursor(&fixture, false);
    Atom *answer = NULL;
    uint64_t spent;
    assert(cetta_prepared_pure_answer_cursor_advance(cursor, 1u, &spent, &answer) ==
           CETTA_PREPARED_PURE_CURSOR_ANSWER);
    assert(spent == 1u);
    expect_zero_advance(&fixture, cursor);
    /* Zero advancement preserves the receiver's right to withdraw a row. */
    assert(cetta_prepared_pure_answer_cursor_unyield(cursor));
    expect_answer(&fixture, cursor, "(Box kept)");
    cetta_prepared_pure_answer_cursor_close(cursor);
    destroy(&fixture);
}

static void test_cursor_bounded_silent_loop_and_authority(void) {
    const char *equations[] = {"(= (spin $x) (spin $x))"};
    Fixture fixture;
    init(&fixture, equations, 1u, "(spin seed)");
    CettaPreparedPureAnswerCursor *cursor = open_cursor(&fixture, false);
    expect_zero_advance(&fixture, cursor);
    for (unsigned slice = 0u; slice < 32u; ++slice) {
        Atom *answer = NULL;
        uint64_t spent = UINT64_MAX;
        assert(cetta_prepared_pure_answer_cursor_advance(cursor, 3u, &spent, &answer) ==
               CETTA_PREPARED_PURE_CURSOR_PAUSED);
        assert(spent == 3u && !answer &&
               cetta_prepared_pure_answer_cursor_frame_count(cursor) == 1u &&
               cetta_prepared_pure_answer_cursor_handoff_reason(cursor) ==
                   CETTA_PREPARED_PURE_HANDOFF_NONE);
        expect_zero_advance(&fixture, cursor);
    }
    assert(cetta_prepared_pure_answer_cursor_answer_count(cursor) == 0u &&
           cetta_prepared_pure_answer_cursor_tail_call_count(cursor) == 96u);
    assert(space_admit_atom(&fixture.space, &fixture.source,
                           parse(&fixture.source, "(= (spin $x) done)")));
    size_t polls = fixture.polls;
    Atom *answer = NULL;
    uint64_t spent = UINT64_MAX;
    assert(cetta_prepared_pure_answer_cursor_advance(cursor, 3u, &spent, &answer) ==
           CETTA_PREPARED_PURE_CURSOR_HANDOFF);
    assert(spent == 0u && !answer && fixture.polls == polls &&
           cetta_prepared_pure_answer_cursor_handoff_reason(cursor) ==
               CETTA_PREPARED_PURE_HANDOFF_STALE);
    assert(cetta_prepared_pure_answer_cursor_advance(cursor, 0u, &spent, &answer) ==
           CETTA_PREPARED_PURE_CURSOR_HANDOFF);
    assert(spent == 0u && fixture.polls == polls);
    cetta_prepared_pure_answer_cursor_close(cursor);
    destroy(&fixture);
}

static void test_cursor_quantum_does_not_recharge_limits(void) {
    const char *equations[] = {"(= (spin $x) (spin $x))"};
    Fixture fixture;
    init(&fixture, equations, 1u, "(spin seed)");
    CettaPreparedPureAnswerCursorOptions options = {
        .limits = fixture.limits,
        .unmatched_call = CETTA_PREPARED_PURE_UNMATCHED_FAILS,
        .allow_continuations = true,
    };
    options.limits.max_transitions = 5u;
    CettaPreparedPureAnswerCursor *cursor =
        cetta_prepared_pure_answer_cursor_open(fixture.program, &options);
    assert(cursor);
    Atom *answer = NULL;
    uint64_t spent = UINT64_MAX;
    assert(cetta_prepared_pure_answer_cursor_advance(cursor, 3u, &spent, &answer) ==
           CETTA_PREPARED_PURE_CURSOR_PAUSED);
    assert(spent == 3u && !answer);
    assert(cetta_prepared_pure_answer_cursor_advance(cursor, 3u, &spent, &answer) ==
           CETTA_PREPARED_PURE_CURSOR_HANDOFF);
    assert(spent == 2u && !answer &&
           cetta_prepared_pure_answer_cursor_handoff_reason(cursor) ==
               CETTA_PREPARED_PURE_HANDOFF_LIMIT);
    assert(cetta_prepared_pure_answer_cursor_advance(cursor, 100u, &spent, &answer) ==
           CETTA_PREPARED_PURE_CURSOR_HANDOFF);
    assert(spent == 0u && cetta_prepared_pure_answer_cursor_tail_call_count(cursor) == 5u);
    cetta_prepared_pure_answer_cursor_close(cursor);
    destroy(&fixture);
}

/* Suspension yields the enumeration order, and the frontier after each
 * answer is the stack of unfinished calls with their next occurrences. */
static void test_cursor_frontier_between_answers(void) {
    const char *equations[] = {
        "(= (g (S $n)) (g $n))",
        "(= (g $n) $n)",
    };
    Fixture fixture;
    init(&fixture, equations, 2u, "(g (S (S Z)))");
    CettaPreparedPureAnswerCursor *cursor = open_cursor(&fixture, false);
    expect_answer(&fixture, cursor, "Z");
    /* (g Z) has tried both equations; both enclosing calls still owe the
     * second one. */
    assert(cetta_prepared_pure_answer_cursor_frame_count(cursor) == 3u);
    expect_frame(&fixture, cursor, 0u, "(g (S (S Z)))", 1u);
    expect_frame(&fixture, cursor, 1u, "(g (S Z))", 1u);
    expect_frame(&fixture, cursor, 2u, "(g Z)", 2u);
    expect_answer(&fixture, cursor, "(S Z)");
    assert(cetta_prepared_pure_answer_cursor_frame_count(cursor) == 2u);
    expect_answer(&fixture, cursor, "(S (S Z))");
    Atom *answer = NULL;
    assert(cetta_prepared_pure_answer_cursor_next(cursor, &answer) ==
           CETTA_PREPARED_PURE_CURSOR_EXHAUSTED);
    assert(cetta_prepared_pure_answer_cursor_frame_count(cursor) == 0u);
    assert(cetta_prepared_pure_answer_cursor_head_count(cursor) == 1u);
    cetta_prepared_pure_answer_cursor_close(cursor);
    destroy(&fixture);
}

/* A last call replaces its caller, so the frontier stays one call deep. */
static void test_cursor_last_call_replaces_caller(void) {
    const char *equations[] = {
        "(= (pick (Cons $x $xs)) $x)",
        "(= (pick (Cons $x (Cons $y $ys))) (pick (Cons $y $ys)))",
    };
    Fixture fixture;
    init(&fixture, equations, 2u, "(pick (Cons a (Cons b (Cons c Nil))))");
    CettaPreparedPureAnswerCursor *cursor = open_cursor(&fixture, false);
    expect_answer(&fixture, cursor, "a");
    expect_frame(&fixture, cursor, 0u,
                 "(pick (Cons a (Cons b (Cons c Nil))))", 1u);
    expect_answer(&fixture, cursor, "b");
    assert(cetta_prepared_pure_answer_cursor_frame_count(cursor) == 1u);
    expect_frame(&fixture, cursor, 0u, "(pick (Cons b (Cons c Nil)))", 1u);
    expect_answer(&fixture, cursor, "c");
    Atom *answer = NULL;
    assert(cetta_prepared_pure_answer_cursor_next(cursor, &answer) ==
           CETTA_PREPARED_PURE_CURSOR_EXHAUSTED);
    assert(cetta_prepared_pure_answer_cursor_answer_count(cursor) == 3u);
    assert(cetta_prepared_pure_answer_cursor_tail_call_count(cursor) == 2u);
    cetta_prepared_pure_answer_cursor_close(cursor);
    destroy(&fixture);
}

/* Withdrawing an answer restores the frontier it was produced from. */
static void test_cursor_unyield(void) {
    const char *equations[] = {
        "(= (pick (Cons $x $xs)) $x)",
        "(= (pick (Cons $x (Cons $y $ys))) (pick (Cons $y $ys)))",
    };
    Fixture fixture;
    init(&fixture, equations, 2u, "(pick (Cons a (Cons b Nil)))");
    CettaPreparedPureAnswerCursor *cursor = open_cursor(&fixture, false);
    expect_answer(&fixture, cursor, "a");
    expect_answer(&fixture, cursor, "b");
    assert(cetta_prepared_pure_answer_cursor_unyield(cursor));
    assert(!cetta_prepared_pure_answer_cursor_unyield(cursor));
    expect_frame(&fixture, cursor, 0u, "(pick (Cons b Nil))", 0u);
    expect_answer(&fixture, cursor, "b");
    cetta_prepared_pure_answer_cursor_close(cursor);
    destroy(&fixture);
}

/* A call no equation matches is zero answers, or a decline on request. */
static void test_cursor_unmatched_call(void) {
    const char *equations[] = {
        "(= (pick (Cons $x $xs)) $x)",
        "(= (pick (Cons $x (Cons $y $ys))) (pick (Cons $y $ys)))",
    };
    Fixture fixture;
    init(&fixture, equations, 2u, "(pick Nil)");
    Atom *answer = NULL;
    CettaPreparedPureAnswerCursor *cursor = open_cursor(&fixture, false);
    assert(cetta_prepared_pure_answer_cursor_next(cursor, &answer) ==
           CETTA_PREPARED_PURE_CURSOR_EXHAUSTED);
    cetta_prepared_pure_answer_cursor_close(cursor);
    cursor = open_cursor(&fixture, true);
    assert(cetta_prepared_pure_answer_cursor_next(cursor, &answer) ==
           CETTA_PREPARED_PURE_CURSOR_HANDOFF);
    assert(cetta_prepared_pure_answer_cursor_handoff_reason(cursor) ==
           CETTA_PREPARED_PURE_HANDOFF_NO_MATCH);
    cetta_prepared_pure_answer_cursor_close(cursor);
    destroy(&fixture);
}

/* A program change between answers stops the cursor before any step, with
 * the frontier intact; the cursor keeps its program alive meanwhile. */
static void test_cursor_stale_program_and_retention(void) {
    const char *equations[] = {
        "(= (pick (Cons $x $xs)) $x)",
        "(= (pick (Cons $x (Cons $y $ys))) (pick (Cons $y $ys)))",
    };
    Fixture fixture;
    init(&fixture, equations, 2u, "(pick (Cons a (Cons b Nil)))");
    CettaPreparedPureAnswerCursor *cursor = open_cursor(&fixture, false);
    expect_answer(&fixture, cursor, "a");
    /* The cache's owner releases the program while the cursor reads it. */
    cetta_prepared_pure_program_free(fixture.program);
    fixture.program = NULL;
    assert(space_admit_atom(&fixture.space, &fixture.source,
                           parse(&fixture.source, "(= (pick $l) other)")));
    Atom *answer = NULL;
    assert(cetta_prepared_pure_answer_cursor_next(cursor, &answer) ==
           CETTA_PREPARED_PURE_CURSOR_HANDOFF);
    assert(cetta_prepared_pure_answer_cursor_handoff_reason(cursor) ==
           CETTA_PREPARED_PURE_HANDOFF_STALE);
    assert(cetta_prepared_pure_answer_cursor_frame_count(cursor) == 1u);
    expect_frame(&fixture, cursor, 0u, "(pick (Cons a (Cons b Nil)))", 1u);
    /* Stopped cursors stay stopped. */
    assert(cetta_prepared_pure_answer_cursor_next(cursor, &answer) ==
           CETTA_PREPARED_PURE_CURSOR_HANDOFF);
    cetta_prepared_pure_answer_cursor_close(cursor);
    destroy(&fixture);
}

/* After detaching, the cursor no longer reads the storage of its call. */
static void test_cursor_detach(void) {
    const char *equations[] = {
        "(= (pick (Cons $x $xs)) $x)",
        "(= (pick (Cons $x (Cons $y $ys))) (pick (Cons $y $ys)))",
    };
    Fixture fixture;
    init(&fixture, equations, 2u, "(pick (Cons a Nil))");
    Arena call_arena;
    arena_init(&call_arena);
    arena_set_hashcons(&call_arena, NULL);
    ArenaMark empty = arena_mark(&call_arena);
    Atom *call = parse(&call_arena, "(pick (Cons a (Cons b (Cons c Nil))))");
    assert(cetta_prepared_pure_program_rebind_closed_entry_call(
        fixture.program, call));
    CettaPreparedPureAnswerCursor *cursor = open_cursor(&fixture, false);
    cetta_prepared_pure_program_clear_closed_entry_call(fixture.program);
    expect_answer(&fixture, cursor, "a");
    assert(!cetta_prepared_pure_answer_cursor_can_detach(NULL));
    assert(cetta_prepared_pure_answer_cursor_can_detach(cursor));
    assert(cetta_prepared_pure_answer_cursor_detach(cursor));
    assert(cetta_prepared_pure_answer_cursor_can_detach(cursor));
    assert(cetta_prepared_pure_answer_cursor_detach(cursor));
    /* Reuse the released storage for unrelated values. */
    arena_reset(&call_arena, empty);
    for (int i = 0; i < 64; i++)
        (void)parse(&call_arena, "(noise (noise (noise noise)))");
    expect_answer(&fixture, cursor, "b");
    expect_answer(&fixture, cursor, "c");
    Atom *answer = NULL;
    assert(cetta_prepared_pure_answer_cursor_next(cursor, &answer) ==
           CETTA_PREPARED_PURE_CURSOR_EXHAUSTED);
    cetta_prepared_pure_answer_cursor_close(cursor);
    arena_free(&call_arena);
    destroy(&fixture);
}

/* Pending non-tail calls retain locals as well as frontier arguments.  Move
 * both before the consumer reuses the storage of its original call. */
static void test_cursor_detach_continuations(void) {
    const char *equations[] = {
        "(= (pick (Cons $x $xs)) $x)",
        "(= (pick (Cons $x (Cons $y $ys))) (pick (Cons $y $ys)))",
        "(= (pair $xs) (let $x (pick $xs) (Pair $xs $x)))",
        "(= (outer $xs) (Box $xs (pair $xs)))",
    };
    /* Cover detachment before stepping and with nested continuations live. */
    for (int yielded = 0; yielded <= 1; yielded++) {
        Fixture fixture;
        init(&fixture, equations, 4u, "(outer (Cons a Nil))");
        Arena call_arena;
        arena_init(&call_arena);
        arena_set_hashcons(&call_arena, NULL);
        ArenaMark empty = arena_mark(&call_arena);
        Atom *call = parse(&call_arena,
            "(outer (Cons a (Cons b (Cons c Nil))))");
        assert(cetta_prepared_pure_program_rebind_closed_entry_call(
            fixture.program, call));
        CettaPreparedPureAnswerCursor *cursor = open_cursor(&fixture, false);
        cetta_prepared_pure_program_clear_closed_entry_call(fixture.program);
        const char *answers[] = {
            "(Box (Cons a (Cons b (Cons c Nil))) (Pair (Cons a (Cons b (Cons c Nil))) a))",
            "(Box (Cons a (Cons b (Cons c Nil))) (Pair (Cons a (Cons b (Cons c Nil))) b))",
            "(Box (Cons a (Cons b (Cons c Nil))) (Pair (Cons a (Cons b (Cons c Nil))) c))",
        };
        if (yielded)
            expect_answer(&fixture, cursor, answers[0]);
        assert(cetta_prepared_pure_answer_cursor_can_detach(cursor));
        assert(cetta_prepared_pure_answer_cursor_detach(cursor));
        assert(cetta_prepared_pure_answer_cursor_detach(cursor));
        arena_reset(&call_arena, empty);
        for (int i = 0; i < 64; i++)
            (void)parse(&call_arena, "(noise (noise (noise noise)))");
        for (int i = yielded; i < 3; i++)
            expect_answer(&fixture, cursor, answers[i]);
        Atom *answer = NULL;
        assert(cetta_prepared_pure_answer_cursor_next(cursor, &answer) ==
               CETTA_PREPARED_PURE_CURSOR_EXHAUSTED);
        cetta_prepared_pure_answer_cursor_close(cursor);
        arena_free(&call_arena);
        destroy(&fixture);
    }
}

/* A detach no-op on a caller arena does not authorize freeing that arena. */
static void test_cursor_caller_arena_not_evacuatable(void) {
    const char *equations[] = {
        "(= (pick $x) first)", "(= (pick $x) second)",
    };
    Fixture fixture;
    init(&fixture, equations, 2u, "(pick seed)");
    CettaPreparedPureAnswerCursorOptions options = {
        .arena = &fixture.scratch,
        .limits = fixture.limits,
    };
    CettaPreparedPureAnswerCursor *cursor =
        cetta_prepared_pure_answer_cursor_open(fixture.program, &options);
    assert(cursor);
    assert(!cetta_prepared_pure_answer_cursor_can_detach(cursor));
    assert(cetta_prepared_pure_answer_cursor_detach(cursor));
    assert(!cetta_prepared_pure_answer_cursor_can_detach(cursor));
    expect_answer(&fixture, cursor, "first");
    expect_answer(&fixture, cursor, "second");
    Atom *answer = NULL;
    assert(cetta_prepared_pure_answer_cursor_next(cursor, &answer) ==
           CETTA_PREPARED_PURE_CURSOR_EXHAUSTED);
    cetta_prepared_pure_answer_cursor_close(cursor);
    destroy(&fixture);
}

/* A chain of last calls keeps the frontier one call deep while it grows
 * its argument; the scratch purse, not the frontier, ends the stream. */
static void test_cursor_last_call_chain_is_bounded(void) {
    const char *equations[] = {
        "(= (count (S (S (S (S (S (S (S (S $n))))))))) done)",
        "(= (count $n) (count (S $n)))",
    };
    Fixture fixture;
    init(&fixture, equations, 2u, "(count Z)");
    fixture.limits.max_scratch_bytes = 64u * 1024u;
    CettaPreparedPureAnswerCursor *cursor = open_cursor(&fixture, false);
    Atom *answer = NULL;
    size_t answers = 0u;
    CettaPreparedPureCursorStep step;
    while ((step = cetta_prepared_pure_answer_cursor_next(cursor, &answer)) ==
           CETTA_PREPARED_PURE_CURSOR_ANSWER) {
        char *text = atom_to_string(&fixture.source, answer);
        assert(text && strcmp(text, "done") == 0);
        assert(cetta_prepared_pure_answer_cursor_frame_count(cursor) == 1u);
        answers++;
    }
    assert(step == CETTA_PREPARED_PURE_CURSOR_HANDOFF);
    assert(cetta_prepared_pure_answer_cursor_handoff_reason(cursor) ==
           CETTA_PREPARED_PURE_HANDOFF_LIMIT);
    assert(answers > 64u);
    assert(cetta_prepared_pure_answer_cursor_frame_count(cursor) == 1u);
    cetta_prepared_pure_answer_cursor_close(cursor);
    destroy(&fixture);
}

/* A scalar generator has a fixed live frontier despite an unbounded number
 * of answers. Consuming an answer does not retain its abandoned arithmetic
 * temporaries, and closing the producer does not evaluate another step. */
static void test_cursor_scalar_frontier_reclamation(void) {
    const char *equations[] = {
        "(= (ticks $n) $n)",
        "(= (ticks $n) (ticks (+ $n 1)))",
    };
    Fixture fixture;
    init(&fixture, equations, 2u, "(ticks 0)");
    fixture.limits.max_scratch_bytes = 128u * 1024u;
    CettaPreparedPureAnswerCursor *cursor = open_cursor(&fixture, false);
    Arena selected;
    arena_init_detached(&selected);
    Atom *kept = NULL;
    size_t peak_live = 0u, peak_reserved = 0u, peak_metadata = 0u;
    size_t warmed_metadata = 0u;
    for (unsigned i = 0u; i < 4000u; i++) {
        Atom *answer = NULL;
        CettaPreparedPureCursorStep step =
            cetta_prepared_pure_answer_cursor_next(cursor, &answer);
        if (step != CETTA_PREPARED_PURE_CURSOR_ANSWER)
            fprintf(stderr, "scalar frontier stopped at %u: reason %u\n", i,
                    cetta_prepared_pure_answer_cursor_handoff_reason(cursor));
        assert(step == CETTA_PREPARED_PURE_CURSOR_ANSWER);
        assert(answer && answer->kind == ATOM_GROUNDED &&
               answer->ground.gkind == GV_INT && answer->ground.ival == i);
        assert(cetta_prepared_pure_answer_cursor_frame_count(cursor) == 1u);
        CettaPreparedPureCursorStorage storage =
            cetta_prepared_pure_answer_cursor_storage(cursor);
        if (storage.live_bytes > peak_live)
            peak_live = storage.live_bytes;
        if (storage.reserved_bytes > peak_reserved)
            peak_reserved = storage.reserved_bytes;
        if (storage.metadata_bytes > peak_metadata)
            peak_metadata = storage.metadata_bytes;
        if (i == 32u)
            warmed_metadata = storage.metadata_bytes;
        if (i > 32u)
            assert(storage.metadata_bytes == warmed_metadata);
        if (i == 7u)
            kept = atom_deep_copy(&selected, answer);
    }
    assert(cetta_prepared_pure_answer_cursor_answer_count(cursor) == 4000u);
    assert(cetta_prepared_pure_answer_cursor_tail_call_count(cursor) == 3999u);
    CettaPreparedPureCursorStorage storage =
        cetta_prepared_pure_answer_cursor_storage(cursor);
    assert(storage.collections > 0u);
    assert(peak_live < fixture.limits.max_scratch_bytes);
    assert(peak_reserved <= fixture.limits.max_scratch_bytes);
    assert(peak_metadata == warmed_metadata);
    printf("owned scalar frontier: 4000 answers, peak %zu live / %zu reserved / "
           "%zu metadata bytes, %llu collections\n", peak_live, peak_reserved,
           peak_metadata, (unsigned long long)storage.collections);
    cetta_prepared_pure_answer_cursor_close(cursor);
    assert(kept && kept->ground.ival == 7);
    arena_free(&selected);
    destroy(&fixture);
}

/* Repeated argument roots remain aliases after relocation, while two equal
 * equation occurrences still produce two answers. The caller's input can be
 * reset after detachment and a published answer outlives the whole cursor. */
static void test_cursor_nested_frontier_reclamation(void) {
    const char *equations[] = {
        "(= (ticks $n) $n)",
        "(= (ticks $n) (ticks (+ $n 1)))",
        "(= (wrapped $n $payload) (let $value (ticks $n) (Pair $payload $payload $value)))",
        "(= (outer $n $payload) (Box $payload (wrapped $n $payload)))",
        "(= (outer $n $payload) (Fallback $payload))",
    };
    Fixture fixture;
    init(&fixture, equations, 5u, "(outer 0 (Cons kept Nil))");
    fixture.limits.max_scratch_bytes = 128u * 1024u;
    CettaPreparedPureAnswerCursor *cursor = open_cursor(&fixture, false);
    assert(cetta_prepared_pure_answer_cursor_detach(cursor));
    size_t peak_live = 0u, peak_reserved = 0u, metadata = 0u;
    Arena publication;
    arena_init_detached(&publication);
    Atom *kept = NULL;
    for (unsigned i = 0u; i < 4000u; i++) {
        Atom *answer = NULL;
        CettaPreparedPureCursorStep step =
            cetta_prepared_pure_answer_cursor_next(cursor, &answer);
        if (step != CETTA_PREPARED_PURE_CURSOR_ANSWER)
            fprintf(stderr, "nested frontier stopped at %u: reason %u\n", i,
                    cetta_prepared_pure_answer_cursor_handoff_reason(cursor));
        assert(step == CETTA_PREPARED_PURE_CURSOR_ANSWER);
        assert(answer->kind == ATOM_EXPR && answer->expr.len == 3u);
        Atom *pair = answer->expr.elems[2];
        assert(pair->kind == ATOM_EXPR && pair->expr.len == 4u);
        assert(pair->expr.elems[1] == pair->expr.elems[2]);
        assert(answer->expr.elems[1] == pair->expr.elems[1]);
        assert(pair->expr.elems[3]->ground.ival == i);
        assert(cetta_prepared_pure_answer_cursor_frame_count(cursor) == 2u);
        expect_frame(&fixture, cursor, 0u, "(outer 0 (Cons kept Nil))", 1u);
        CettaPreparedPureCursorStorage storage =
            cetta_prepared_pure_answer_cursor_storage(cursor);
        if (storage.live_bytes > peak_live) peak_live = storage.live_bytes;
        if (storage.reserved_bytes > peak_reserved) peak_reserved = storage.reserved_bytes;
        if (i == 32u) metadata = storage.metadata_bytes;
        if (i > 32u) assert(storage.metadata_bytes == metadata);
        if (i == 7u) kept = atom_deep_copy(&publication, answer);
    }
    CettaPreparedPureCursorStorage storage =
        cetta_prepared_pure_answer_cursor_storage(cursor);
    assert(storage.collections > 0u);
    assert(storage.copied_bytes > 0u && storage.copied_bytes < 64u * 1024u);
    assert(peak_live < 96u * 1024u && peak_reserved <= 128u * 1024u);
    printf("owned nested frontier: 4000 answers, peak %zu live / %zu reserved / "
           "%zu metadata bytes, %llu collections, %llu copied bytes\n",
           peak_live, peak_reserved, metadata,
           (unsigned long long)storage.collections,
           (unsigned long long)storage.copied_bytes);
    cetta_prepared_pure_answer_cursor_close(cursor);
    assert(kept->expr.elems[2]->expr.elems[3]->ground.ival == 7);
    assert(kept->expr.elems[1] == kept->expr.elems[2]->expr.elems[1]);
    arena_free(&publication);
    destroy(&fixture);
}

static void test_cursor_collection_preserves_shared_frontier(void) {
    const char *equations[] = {
        "(= (ticks-pair $n $x $y) (Pair $n $x $y))",
        "(= (ticks-pair $n $x $y) (Pair $n $x $y))",
        "(= (ticks-pair $n $x $y) (ticks-pair (+ $n 1) $x $y))",
    };
    Fixture fixture;
    init(&fixture, equations, 3u, "(ticks-pair 0 a a)");
    fixture.limits.max_scratch_bytes = 128u * 1024u;
    Arena source, selected;
    arena_init_detached(&source);
    arena_init_detached(&selected);
    ArenaMark empty = arena_mark(&source);
    Atom *payload = parse(&source, "(nested (bytes abc) (more xyz))");
    Atom *parts[] = {atom_symbol(&source, "ticks-pair"), atom_int(&source, 0),
                     payload, payload};
    assert(cetta_prepared_pure_program_rebind_closed_entry_call(
        fixture.program, atom_expr(&source, parts, 4u)));
    CettaPreparedPureAnswerCursor *cursor = open_cursor(&fixture, false);
    cetta_prepared_pure_program_clear_closed_entry_call(fixture.program);
    assert(cetta_prepared_pure_answer_cursor_detach(cursor));
    arena_reset(&source, empty);
    Atom *kept = NULL;
    for (unsigned i = 0u; i < 4000u; i++) {
        Atom *answer = NULL;
        assert(cetta_prepared_pure_answer_cursor_next(cursor, &answer) ==
               CETTA_PREPARED_PURE_CURSOR_ANSWER);
        assert(answer && answer->kind == ATOM_EXPR && answer->expr.len == 4u);
        assert(answer->expr.elems[1]->ground.ival == i / 2u);
        assert(answer->expr.elems[2] == answer->expr.elems[3]);
        if (i == 1234u) {
            assert(cetta_prepared_pure_answer_cursor_unyield(cursor));
            assert(cetta_prepared_pure_answer_cursor_next(cursor, &answer) ==
                   CETTA_PREPARED_PURE_CURSOR_ANSWER);
            assert(answer->expr.elems[1]->ground.ival == i / 2u);
        }
        if (i == 3999u)
            kept = atom_deep_copy(&selected, answer);
    }
    assert(cetta_prepared_pure_answer_cursor_answer_count(cursor) == 4000u);
    assert(cetta_prepared_pure_answer_cursor_tail_call_count(cursor) == 1999u);
    assert(cetta_prepared_pure_answer_cursor_storage(cursor).collections > 0u);
    cetta_prepared_pure_answer_cursor_close(cursor);
    assert(kept && kept->expr.elems[1]->ground.ival == 1999);
    assert(kept->expr.elems[2] == kept->expr.elems[3]);
    assert(atom_eq(kept->expr.elems[2],
                   parse(&source, "(nested (bytes abc) (more xyz))")));
    arena_free(&source);
    arena_free(&selected);
    destroy(&fixture);
}

/* Reclaiming dead values does not refill a finite execution allowance. */
static void test_cursor_collection_keeps_transition_limit(void) {
    const char *equations[] = {
        "(= (ticks $n) $n)",
        "(= (ticks $n) (ticks (+ $n 1)))",
    };
    Fixture fixture;
    init(&fixture, equations, 2u, "(ticks 0)");
    fixture.limits.max_transitions = 20000u;
    fixture.limits.max_scratch_bytes = 128u * 1024u;
    CettaPreparedPureAnswerCursorOptions options = {
        .limits = fixture.limits,
        .unmatched_call = CETTA_PREPARED_PURE_UNMATCHED_FAILS,
        .allow_continuations = true,
    };
    CettaPreparedPureAnswerCursor *cursor =
        cetta_prepared_pure_answer_cursor_open(fixture.program, &options);
    assert(cursor);
    uint64_t answers = 0u;
    Atom *answer = NULL;
    CettaPreparedPureCursorStep step;
    while ((step = cetta_prepared_pure_answer_cursor_next(cursor, &answer)) ==
           CETTA_PREPARED_PURE_CURSOR_ANSWER) {
        assert(answer && answer->kind == ATOM_GROUNDED &&
               answer->ground.gkind == GV_INT && answer->ground.ival == answers);
        assert(++answers < fixture.limits.max_transitions);
    }
    assert(step == CETTA_PREPARED_PURE_CURSOR_HANDOFF);
    assert(cetta_prepared_pure_answer_cursor_handoff_reason(cursor) ==
           CETTA_PREPARED_PURE_HANDOFF_LIMIT);
    assert(cetta_prepared_pure_answer_cursor_storage(cursor).collections > 0u);
    assert(cetta_prepared_pure_answer_cursor_answer_count(cursor) == answers);
    assert(cetta_prepared_pure_answer_cursor_frame_count(cursor) == 1u);
    cetta_prepared_pure_answer_cursor_close(cursor);
    destroy(&fixture);
}

/* A spent purse stops before the step that would exceed it. */
static void test_cursor_limit_keeps_frontier(void) {
    const char *equations[] = {
        "(= (grow $n) $n)",
        "(= (grow $n) (grow (S $n)))",
    };
    Fixture fixture;
    init(&fixture, equations, 2u, "(grow Z)");
    size_t one_constructor = 0u;
    assert(atom_expr_allocation_bound(2u, &one_constructor));
    fixture.limits.max_scratch_bytes = 2u * one_constructor;
    CettaPreparedPureAnswerCursor *cursor = open_cursor(&fixture, false);
    expect_answer(&fixture, cursor, "Z");
    expect_answer(&fixture, cursor, "(S Z)");
    expect_answer(&fixture, cursor, "(S (S Z))");
    Atom *answer = NULL;
    assert(cetta_prepared_pure_answer_cursor_next(cursor, &answer) ==
           CETTA_PREPARED_PURE_CURSOR_HANDOFF);
    assert(cetta_prepared_pure_answer_cursor_handoff_reason(cursor) ==
           CETTA_PREPARED_PURE_HANDOFF_LIMIT);
    assert(cetta_prepared_pure_answer_cursor_frame_count(cursor) == 1u);
    expect_frame(&fixture, cursor, 0u, "(grow (S (S Z)))", 1u);
    cetta_prepared_pure_answer_cursor_close(cursor);
    destroy(&fixture);
}

static CettaPreparedPureExpressionViewState zero_view(
    Space *space, const Atom *expression, CettaPreparedPureExpressionView *view) {
    (void)space;
    (void)view;
    return expression && expression->kind == ATOM_EXPR &&
           expression->expr.len == 1u &&
           atom_is_symbol_id(expression->expr.elems[0],
                             symbol_intern_cstr(g_symbols, "empty"))
        ? CETTA_PREPARED_PURE_EXPRESSION_ZERO
        : CETTA_PREPARED_PURE_EXPRESSION_DEFAULT;
}

/* Each answer of a call resumes its caller's body from the same state. */
static void test_continuation_permutations(void) {
    const char *equations[] = {
        "(= (select (Cons $x $xs)) (Pair $x $xs))",
        "(= (select (Cons $x $xs)) (let (Pair $y $ys) (select $xs) "
        "(Pair $y (Cons $x $ys))))",
        "(= (perm Nil) Nil)",
        "(= (perm (Cons $h $t)) (let (Pair $y $ys) (select (Cons $h $t)) "
        "(Cons $y (perm $ys))))",
    };
    Fixture fixture;
    init(&fixture, equations, 4u, "(perm (Cons a (Cons b (Cons c Nil))))");
    /* Selecting from Nil matches no equation and fails. */
    fixture.unmatched_call = CETTA_PREPARED_PURE_UNMATCHED_FAILS;
    fixture.expected[0] = "(Cons a (Cons b (Cons c Nil)))";
    fixture.expected[1] = "(Cons a (Cons c (Cons b Nil)))";
    fixture.expected[2] = "(Cons b (Cons a (Cons c Nil)))";
    fixture.expected[3] = "(Cons b (Cons c (Cons a Nil)))";
    uint64_t tails = 0u;
    assert(run(&fixture, &tails) == CETTA_PREPARED_PURE_ANSWERS_COMPLETE);
    assert(fixture.answers == 6u);
    /* Continuations have no equation-choice reading: a consumer that hands
     * a frontier to equation search cannot open this program. */
    CettaPreparedPureAnswerCursorOptions lazy = {
        .limits = fixture.limits,
    };
    assert(!cetta_prepared_pure_answer_cursor_open(fixture.program, &lazy));
    destroy(&fixture);
}

/* Saved locals, not just pending call arguments, must survive detachment.
 * Equal inputs still produce separate ordered occurrences after the caller
 * arena has been freed.  Exercise fresh and partially consumed frontiers. */
static void test_continuation_detach(void) {
    const char *equations[] = {
        "(= (select (Cons $x $xs)) (Pair $x $xs))",
        "(= (select (Cons $x $xs)) (let (Pair $y $ys) (select $xs) "
        "(Pair $y (Cons $x $ys))))",
        "(= (perm Nil) Nil)",
        "(= (perm (Cons $h $t)) (let (Pair $y $ys) (select (Cons $h $t)) "
        "(Cons $y (perm $ys))))",
    };
    const char *expected[] = {
        "(Cons a (Cons a (Cons b Nil)))",
        "(Cons a (Cons b (Cons a Nil)))",
        "(Cons a (Cons a (Cons b Nil)))",
        "(Cons a (Cons b (Cons a Nil)))",
        "(Cons b (Cons a (Cons a Nil)))",
        "(Cons b (Cons a (Cons a Nil)))",
    };
    const size_t detach_after[] = {0u, 1u, 3u};
    for (size_t run = 0u; run < 3u; run++) {
        Fixture fixture;
        init(&fixture, equations, 4u, "(perm (Cons a Nil))");
        Arena call_arena;
        arena_init(&call_arena);
        arena_set_hashcons(&call_arena, NULL);
        Atom *call = parse(&call_arena, "(perm (Cons a (Cons a (Cons b Nil))))");
        assert(cetta_prepared_pure_program_rebind_closed_entry_call(
            fixture.program, call));
        CettaPreparedPureAnswerCursor *cursor = open_cursor(&fixture, false);
        cetta_prepared_pure_program_clear_closed_entry_call(fixture.program);
        for (size_t index = 0u; index < 6u; index++) {
            if (index == detach_after[run]) {
                assert(cetta_prepared_pure_answer_cursor_detach(cursor));
                assert(cetta_prepared_pure_answer_cursor_detach(cursor));
                assert(!cetta_prepared_pure_answer_cursor_unyield(cursor));
                arena_free(&call_arena);
                arena_init(&call_arena);
                arena_set_hashcons(&call_arena, NULL);
                for (size_t noise = 0u; noise < 64u; noise++)
                    (void)parse(&call_arena, "(noise (noise (noise noise)))");
            }
            expect_answer(&fixture, cursor, expected[index]);
        }
        Atom *answer = NULL;
        assert(cetta_prepared_pure_answer_cursor_next(cursor, &answer) ==
               CETTA_PREPARED_PURE_CURSOR_EXHAUSTED);
        cetta_prepared_pure_answer_cursor_close(cursor);
        arena_free(&call_arena);
        destroy(&fixture);
    }
}

/* A call that matches no equation answers by dialect: nothing, a decline,
 * or itself as data. */
static void test_continuation_unmatched_call_rules(void) {
    const char *equations[] = {
        "(= (f a) b)", "(= (f a) c)",
        "(= (g $x) (Box (f $x)))",
    };
    Fixture fixture;
    init(&fixture, equations, 3u, "(g z)");
    uint64_t tails = 0u;
    fixture.unmatched_call = CETTA_PREPARED_PURE_UNMATCHED_FAILS;
    assert(run(&fixture, &tails) == CETTA_PREPARED_PURE_ANSWERS_COMPLETE);
    assert(fixture.answers == 0u);
    fixture.unmatched_call = CETTA_PREPARED_PURE_UNMATCHED_DECLINES;
    assert(run(&fixture, &tails) == CETTA_PREPARED_PURE_ANSWERS_DECLINED);
    assert(fixture.answers == 0u);
    fixture.unmatched_call = CETTA_PREPARED_PURE_UNMATCHED_REDUCES_TO_ITSELF;
    fixture.expected[0] = "(Box (f z))";
    assert(run(&fixture, &tails) == CETTA_PREPARED_PURE_ANSWERS_COMPLETE);
    assert(fixture.answers == 1u);
    destroy(&fixture);
}

/* A guard selects a branch; choice zero leaves no answer on its path. */
static void test_continuation_guard_and_zero(void) {
    const char *equations[] = {
        "(= (letter) a)", "(= (letter) b)", "(= (letter) c)",
        "(= (keep a) True)", "(= (keep b) False)", "(= (keep c) True)",
        "(= (kept) (let $x (letter) (if (keep $x) (Kept $x) (empty))))",
    };
    Fixture fixture;
    init_with_view(&fixture, equations, 7u, "(kept)", zero_view);
    fixture.expected[0] = "(Kept a)";
    fixture.expected[1] = "(Kept c)";
    uint64_t tails = 0u;
    assert(run(&fixture, &tails) == CETTA_PREPARED_PURE_ANSWERS_COMPLETE);
    assert(fixture.answers == 2u);
    destroy(&fixture);
}

/* Operands run left to right, so every combination appears in order. */
static void test_continuation_operand_order(void) {
    const char *equations[] = {
        "(= (coin) h)", "(= (coin) t)",
        "(= (toss) (Toss (coin) (coin)))",
    };
    Fixture fixture;
    init(&fixture, equations, 3u, "(toss)");
    fixture.expected[0] = "(Toss h h)";
    fixture.expected[1] = "(Toss h t)";
    fixture.expected[2] = "(Toss t h)";
    fixture.expected[3] = "(Toss t t)";
    uint64_t tails = 0u;
    assert(run(&fixture, &tails) == CETTA_PREPARED_PURE_ANSWERS_COMPLETE);
    assert(fixture.answers == 4u);
    destroy(&fixture);
}

/* A value the let pattern refuses leaves no answer; the others remain, with
 * their multiplicity. */
static void test_continuation_let_mismatch(void) {
    const char *equations[] = {
        "(= (perhaps) (Some 1))", "(= (perhaps) None)",
        "(= (perhaps) (Some 2))", "(= (perhaps) (Some 1))",
        "(= (somes) (let (Some $x) (perhaps) (Got $x)))",
    };
    Fixture fixture;
    init(&fixture, equations, 5u, "(somes)");
    fixture.expected[0] = "(Got 1)";
    fixture.expected[1] = "(Got 2)";
    fixture.expected[2] = "(Got 1)";
    uint64_t tails = 0u;
    assert(run(&fixture, &tails) == CETTA_PREPARED_PURE_ANSWERS_COMPLETE);
    assert(fixture.answers == 3u);
    destroy(&fixture);
}

/* Choice zero as an operand empties the whole value. */
static void test_continuation_zero_operand(void) {
    const char *equations[] = {
        "(= (gen) one)", "(= (gen) two)",
        "(= (bad) (Pair (gen) (empty)))",
    };
    Fixture fixture;
    init_with_view(&fixture, equations, 3u, "(bad)", zero_view);
    uint64_t tails = 0u;
    assert(run(&fixture, &tails) == CETTA_PREPARED_PURE_ANSWERS_COMPLETE);
    assert(fixture.answers == 0u);
    destroy(&fixture);
}

/* A single-equation head whose body chooses is not deterministic: its calls
 * are entered, not evaluated inline. */
static void test_continuation_choice_under_determinate_head(void) {
    const char *equations[] = {
        "(= (coin) h)", "(= (coin) t)",
        "(= (wrap $x) (Box $x (coin)))",
        "(= (both) (Pair (wrap 1) (wrap 2)))",
    };
    Fixture fixture;
    init(&fixture, equations, 4u, "(both)");
    fixture.expected[0] = "(Pair (Box 1 h) (Box 2 h))";
    fixture.expected[1] = "(Pair (Box 1 h) (Box 2 t))";
    fixture.expected[2] = "(Pair (Box 1 t) (Box 2 h))";
    fixture.expected[3] = "(Pair (Box 1 t) (Box 2 t))";
    uint64_t tails = 0u;
    assert(run(&fixture, &tails) == CETTA_PREPARED_PURE_ANSWERS_COMPLETE);
    assert(fixture.answers == 4u);
    destroy(&fixture);
}

/* A step the machine cannot take after earlier answers declines the whole
 * enumeration; it is never read as completion. */
static void test_continuation_late_decline(void) {
    const char *equations[] = {
        "(= (val) True)", "(= (val) False)", "(= (val) neither)",
        "(= (test) (let $v (val) (if $v yes no)))",
    };
    Fixture fixture;
    init(&fixture, equations, 4u, "(test)");
    fixture.expected[0] = "yes";
    fixture.expected[1] = "no";
    uint64_t tails = 0u;
    assert(run(&fixture, &tails) == CETTA_PREPARED_PURE_ANSWERS_DECLINED);
    assert(fixture.answers == 2u);
    destroy(&fixture);
}

/* A last call from a step program replaces its caller, so a recursion
 * keeps the frontier one call deep. */
static void test_continuation_last_call_depth(void) {
    const char *equations[] = {
        "(= (walk Z) done)", "(= (walk Z) also)",
        "(= (walk (S $n)) (let $m $n (walk $m)))",
    };
    Fixture fixture;
    init(&fixture, equations, 3u, "(walk (S (S (S (S Z)))))");
    CettaPreparedPureAnswerCursor *cursor = open_cursor(&fixture, true);
    assert(cetta_prepared_pure_answer_cursor_can_detach(cursor));
    assert(cetta_prepared_pure_answer_cursor_detach(cursor));
    expect_answer(&fixture, cursor, "done");
    assert(cetta_prepared_pure_answer_cursor_frame_count(cursor) == 1u);
    expect_answer(&fixture, cursor, "also");
    Atom *answer = NULL;
    assert(cetta_prepared_pure_answer_cursor_next(cursor, &answer) ==
           CETTA_PREPARED_PURE_CURSOR_EXHAUSTED);
    assert(cetta_prepared_pure_answer_cursor_tail_call_count(cursor) == 4u);
    cetta_prepared_pure_answer_cursor_close(cursor);
    destroy(&fixture);
}

/* An unbounded generator with a continuation stops at the transition limit,
 * never at completion. */
static void test_continuation_unbounded_generator_limit(void) {
    const char *equations[] = {
        "(= (nat) Z)", "(= (nat) (S (nat)))",
    };
    Fixture fixture;
    init(&fixture, equations, 2u, "(nat)");
    fixture.expected[0] = "Z";
    fixture.expected[1] = "(S Z)";
    fixture.expected[2] = "(S (S Z))";
    fixture.limits.max_transitions = 64u;
    uint64_t tails = 0u;
    assert(run(&fixture, &tails) == CETTA_PREPARED_PURE_ANSWERS_LIMIT);
    assert(fixture.answers >= 3u);
    destroy(&fixture);
}

/* A call whose equation builds its value around a further call records the
 * constructor as a context, and builds the value into its contexts one per
 * executor step.  An interrupt requested while the value is being built is
 * therefore observed within one poll interval, as between frames, and the
 * execution reports the interruption rather than success. */
static SymbolId context_out_symbol;
static bool context_stop_on_out;
static bool context_stop_requested;
static size_t context_out_constructions;

static Atom *construct_counting_out(Arena *arena, Atom **elements,
                                    CettaExprLen length) {
    if (length == 2u && elements[0]->kind == ATOM_SYMBOL &&
        elements[0]->symbol_number == context_out_symbol) {
        context_out_constructions++;
        if (context_stop_on_out)
            context_stop_requested = true;
    }
    return atom_expr(arena, elements, length);
}

static bool poll_context_stop(void *context) {
    size_t *observed = context;
    if (!context_stop_requested)
        return false;
    (*observed)++;
    return true;
}

static void test_context_reconstruction_observes_interrupt(void) {
    const char *equations[] = {
        "(= (f Nil) Nil)",
        "(= (f (Cons $x $xs)) (Out (f $xs)))",
        "(= (upto $n $a) (if (== $n 0) $a (upto (- $n 1) (Cons $n $a))))",
    };
    const char *call_text = "(f (upto 10000 Nil))";
    Fixture fixture;
    init(&fixture, equations, 3u, call_text);
    context_out_symbol = symbol_intern_cstr(g_symbols, "Out");
    Atom *call = parse(&fixture.source, call_text);
    for (int interrupted = 0; interrupted <= 1; interrupted++) {
        CettaPreparedPureProgram *program =
            cetta_prepared_pure_program_compile_closed(
                &fixture.space, call, CETTA_GSLT_PURE_CALL_EAGER,
                atom_bool, construct_counting_out, NULL, NULL, NULL, NULL,
                NULL, NULL, false, true,
                (CettaMatchDecisionSemanticIdentity){
                    .compiler_identity =
                        cetta_match_decision_compiler_identity(),
                });
        assert(program);
        context_stop_on_out = interrupted;
        context_stop_requested = false;
        context_out_constructions = 0u;
        size_t observed = 0u;
        Atom *answer = NULL;
        bool completed = cetta_prepared_pure_program_execute_closed_controlled(
            program, &fixture.scratch, 65536u, poll_context_stop, &observed,
            &answer);
        if (!interrupted) {
            assert(completed && answer);
            assert(context_out_constructions == 10000u);
            size_t depth = 0u;
            for (Atom *cursor = answer;
                 cursor->kind == ATOM_EXPR && cursor->expr.len == 2u;
                 cursor = cursor->expr.elems[1])
                depth++;
            assert(depth == 10000u);
        } else {
            assert(!completed);
            assert(observed == 1u);
            /* The executor polls every 256 steps and builds at most 8
             * contexts per step. */
            assert(context_out_constructions >= 1u &&
                   context_out_constructions <= 257u * 8u);
        }
        cetta_prepared_pure_program_free(program);
    }
    destroy(&fixture);
}

typedef struct {
    CettaOwnedReturn values[4];
    unsigned next, releases, accepted, attempts, calls;
    unsigned length;
    CettaEvalCompletion completion;
    bool reject, warmed;
    Arena *observation_arena;
    Atom *context_variable;
    int64_t context_values[4];
} OwnedSequenceFixture;

static CettaOwnedExecutionStep owned_sequence_next(void *context,
    CettaOwnedReturn *returned, CettaEvalCompletion *completion) {
    OwnedSequenceFixture *fixture = context;
    assert(fixture->releases == 0u);
    fixture->calls++;
    *completion = fixture->completion;
    if (fixture->next == (fixture->length ? fixture->length : 4u))
        return CETTA_OWNED_EXECUTION_COMPLETE;
    *returned = fixture->values[fixture->next++];
    return CETTA_OWNED_EXECUTION_RETURN;
}

static void owned_sequence_release(void *context) {
    OwnedSequenceFixture *fixture = context;
    fixture->releases++;
}

static bool owned_sequence_accept(void *context, const CettaOwnedReturn *returned) {
    OwnedSequenceFixture *fixture = context;
    fixture->attempts++;
    assert(fixture->releases == 0u && fixture->accepted < 4u);
    CettaOwnedReturn expected = fixture->values[fixture->accepted];
    assert(returned->outcome.kind == expected.outcome.kind);
    assert(returned->outcome.result_form == expected.outcome.result_form);
    assert(returned->outcome.term == expected.outcome.term);
    assert(returned->outcome.delayed == expected.outcome.delayed);
    assert(returned->environment == expected.environment);
    assert(returned->final == expected.final);
    if (fixture->reject)
        return false;
    if (fixture->context_variable) {
        Atom *value = bindings_apply_if_vars(returned->environment,
            fixture->observation_arena, fixture->context_variable);
        assert(value && value->kind == ATOM_GROUNDED && value->ground.gkind == GV_INT);
        assert(value->ground.ival == fixture->context_values[fixture->accepted]);
    }
    fixture->accepted++;
    return true;
}

typedef struct {
    OwnedSequenceFixture sequence;
    unsigned compatibility, finals;
    bool permitted, revoke_after_produce, current;
    CettaOwnedExecutionStep stop;
} OwnedTaskFixture;

static CettaOwnedExecutionStep owned_task_next(void *raw,
    CettaOwnedReturn *returned, CettaEvalCompletion *completion) {
    OwnedTaskFixture *fixture = raw;
    if (fixture->stop != CETTA_OWNED_EXECUTION_RETURN) {
        fixture->sequence.calls++;
        *completion = fixture->sequence.completion;
        return fixture->stop;
    }
    CettaOwnedExecutionStep step = owned_sequence_next(&fixture->sequence, returned, completion);
    if (fixture->revoke_after_produce) fixture->permitted = false;
    return step;
}
static void owned_task_release(void *raw) {
    OwnedTaskFixture *fixture = raw;
    owned_sequence_release(&fixture->sequence);
}
static bool owned_task_compatible(void *raw) {
    OwnedTaskFixture *fixture = raw;
    fixture->compatibility++;
    return fixture->permitted;
}
static bool owned_task_accept(void *raw, const CettaOwnedReturn *returned) {
    OwnedTaskFixture *fixture = raw;
    return owned_sequence_accept(&fixture->sequence, returned);
}
static bool owned_task_final_current(void *raw) {
    OwnedTaskFixture *fixture = raw;
    fixture->finals++;
    return fixture->current;
}

static void test_owned_task_account(void) {
    Arena arena;
    arena_init_detached(&arena);
    for (unsigned accounted = 0u; accounted < 2u; accounted++) {
        OwnedTaskFixture fixture = {
            .sequence = {.length = 1u, .values = {{
                .outcome = cetta_call_completed_value(parse(&arena, "same")), .final = true}}},
            .current = true,
        };
        CettaOwnedExecution execution = {0};
        cetta_owned_execution_init(&execution, &fixture, owned_task_next, owned_task_release);
        execution.relocatable = true;
        execution.final_is_current = owned_task_final_current;
        CettaOwnedTask task = {0};
        assert(cetta_owned_task_init(&task, &execution, owned_task_compatible,
                                    owned_task_accept, &fixture));
        CettaReadyQueue queue = {.capacity = 1u};
        assert(cetta_ready_admit(&queue, &task.ready) == READY_ADMITTED);
        CettaOwnedTaskAccount account;
        for (unsigned phase = 0u; phase < 7u; phase++) {
            if (phase == 2u) { fixture.permitted = true; fixture.revoke_after_produce = true; }
            if (phase == 4u) { fixture.permitted = true; fixture.sequence.reject = true; }
            if (phase == 5u) fixture.sequence.reject = false;
            unsigned compatibility = fixture.compatibility, calls = fixture.sequence.calls;
            unsigned attempts = fixture.sequence.attempts, finals = fixture.finals;
            memset(&account, 0xff, sizeof(account));
            CettaOwnedVisit visit = accounted
                ? cetta_owned_task_visit_accounted(&queue, phase != 0u, &account)
                : cetta_owned_task_visit(&queue, phase != 0u);
            assert(visit.task == (phase == 6u ? NULL : &task));
            assert(fixture.sequence.calls == (phase < 2u ? 0u : 1u));
            assert(fixture.sequence.accepted == (phase < 5u ? 0u : 1u));
            assert(task.execution.has_pending == (phase >= 2u && phase < 5u));
            assert(task.execution.terminal == (phase >= 5u));
            assert(queue.count == (phase < 5u ? 1u : 0u));
            assert(visit.context_refused == (phase >= 1u && phase <= 3u));
            if (accounted) {
                assert(account.compatibility == fixture.compatibility - compatibility);
                assert(account.producer == fixture.sequence.calls - calls);
                assert(account.consumer == fixture.sequence.attempts - attempts);
                assert(account.final_is_current == fixture.finals - finals);
                assert(account.rotations == (phase < 6u ? 1u : 0u));
                assert(account.retirements == (phase == 5u ? 1u : 0u));
                assert(account.compatibility <= 2u && account.producer <= 1u &&
                       account.consumer <= 1u && account.final_is_current <= 1u);
                if (phase == 3u || phase == 4u || phase == 5u) assert(!account.producer);
            }
        }
        assert(fixture.sequence.calls == 1u && fixture.sequence.attempts == 2u && fixture.finals == 1u);
        /* Completed membership is separate from execution ownership. */
        assert(cetta_ready_admit(&queue, &task.ready) == READY_ADMITTED);
        CettaOwnedVisit retired = cetta_owned_task_poll_accounted(&task, false, &account);
        assert(retired.step == CETTA_OWNED_EXECUTION_COMPLETE && !queue.count);
        assert(!account.compatibility && !account.producer && !account.consumer &&
               !account.final_is_current && !account.rotations && account.retirements == 1u);
        assert(!fixture.sequence.releases);
        cetta_owned_task_cancel(&task);
        assert(fixture.sequence.releases == 1u);
    }
    /* A final marker can finish immediately, need a successful read-only
     * check, or become stale. A stale marker never replays its accepted item. */
    for (unsigned final = 0u; final < 3u; final++) {
        OwnedTaskFixture fixture = {
            .sequence = {.length = 1u, .values = {{
                .outcome = cetta_call_completed_value(parse(&arena, "final")), .final = true}}},
            .permitted = true, .current = final == 2u,
        };
        CettaOwnedExecution execution = {0};
        cetta_owned_execution_init(&execution, &fixture, owned_task_next, owned_task_release);
        execution.relocatable = true;
        if (final) execution.final_is_current = owned_task_final_current;
        CettaOwnedTask task = {0};
        assert(cetta_owned_task_init(&task, &execution, owned_task_compatible,
                                    owned_task_accept, &fixture));
        CettaReadyQueue queue = {.capacity = 1u};
        assert(cetta_ready_admit(&queue, &task.ready) == READY_ADMITTED);
        CettaOwnedTaskAccount account;
        CettaOwnedVisit visit = cetta_owned_task_visit_accounted(&queue, true, &account);
        bool stale = final == 1u;
        assert(visit.step == (stale ? CETTA_OWNED_EXECUTION_PAUSED : CETTA_OWNED_EXECUTION_COMPLETE));
        assert(fixture.sequence.calls == 1u && fixture.sequence.accepted == 1u &&
               fixture.sequence.attempts == 1u && fixture.finals == (final ? 1u : 0u));
        assert(account.compatibility == 2u && account.producer == 1u && account.consumer == 1u &&
               account.final_is_current == (final ? 1u : 0u) && account.rotations == 1u &&
               account.retirements == (stale ? 0u : 1u));
        assert(!task.execution.has_pending && task.execution.terminal == !stale);
        if (stale) {
            visit = cetta_owned_task_visit_accounted(&queue, true, &account);
            assert(visit.step == CETTA_OWNED_EXECUTION_COMPLETE && !queue.count);
            assert(fixture.sequence.calls == 2u && fixture.sequence.accepted == 1u &&
                   fixture.sequence.attempts == 1u && fixture.finals == 1u);
            assert(account.producer == 1u && account.compatibility == 1u &&
                   !account.consumer && !account.final_is_current && account.rotations == 1u &&
                   account.retirements == 1u);
        }
        cetta_owned_task_cancel(&task);
        assert(fixture.sequence.releases == 1u);
    }
    /* Work which returns no item is still a paid producer call. Only PAUSED
     * retains ready membership; terminal stops keep their distinct reason. */
    for (unsigned stop = CETTA_OWNED_EXECUTION_COMPLETE;
         stop <= CETTA_OWNED_EXECUTION_INTERRUPTED; stop++) {
        OwnedTaskFixture fixture = {.permitted = true, .stop = (CettaOwnedExecutionStep)stop};
        CettaOwnedExecution execution = {0};
        cetta_owned_execution_init(&execution, &fixture, owned_task_next, owned_task_release);
        execution.relocatable = true;
        execution.final_is_current = owned_task_final_current;
        CettaOwnedTask task = {0};
        assert(cetta_owned_task_init(&task, &execution, owned_task_compatible,
                                    owned_task_accept, &fixture));
        CettaReadyQueue queue = {.capacity = 1u};
        assert(cetta_ready_admit(&queue, &task.ready) == READY_ADMITTED);
        CettaOwnedTaskAccount account;
        CettaOwnedVisit visit = cetta_owned_task_visit_accounted(&queue, true, &account);
        bool paused = stop == CETTA_OWNED_EXECUTION_PAUSED;
        assert(visit.step == (CettaOwnedExecutionStep)stop && !visit.context_refused);
        assert(fixture.sequence.calls == 1u && !fixture.sequence.accepted &&
               !fixture.sequence.attempts && !fixture.finals);
        assert(account.compatibility == 1u && account.producer == 1u &&
               !account.consumer && !account.final_is_current && account.rotations == 1u &&
               account.retirements == (paused ? 0u : 1u));
        assert(task.execution.terminal == !paused && queue.count == (paused ? 1u : 0u));
        visit = cetta_owned_task_poll_accounted(&task, true, &account);
        assert(visit.step == (CettaOwnedExecutionStep)stop && !account.rotations);
        assert(account.producer == (paused ? 1u : 0u) &&
               account.compatibility == (paused ? 1u : 0u) && !account.consumer &&
               !account.final_is_current && !account.retirements);
        cetta_owned_task_cancel(&task);
        assert(!queue.count && fixture.sequence.releases == 1u);
    }
    arena_free(&arena);
}

static void test_owned_return_sequencing(void) {
    Arena arena;
    arena_init_detached(&arena);
    Bindings environment;
    bindings_init(&environment);
    Atom *held = parse(&arena, "(println! held-data)");
    Atom *delay = parse(&arena, "(pending-goal $x)");
    OwnedSequenceFixture fixture = {
        .values = {
            {.outcome = cetta_call_completed_value(held), .environment = &environment},
            {.outcome = cetta_call_completed_value(held), .environment = &environment},
            {.outcome = cetta_call_value(held), .environment = &environment},
            {.outcome = cetta_call_suspended(held, delay), .environment = &environment},
        },
    };
    CettaOwnedExecution execution = {0};
    cetta_owned_execution_init(&execution, &fixture, owned_sequence_next,
                              owned_sequence_release);
    assert(cetta_owned_execution_sequence(&execution, 0u,
        owned_sequence_accept, &fixture) == CETTA_OWNED_EXECUTION_PAUSED);
    assert(fixture.next == 0u && execution.accepted == 0u);
    assert(cetta_owned_execution_sequence(&execution, 1u,
        owned_sequence_accept, &fixture) == CETTA_OWNED_EXECUTION_PAUSED);
    assert(fixture.next == 1u && fixture.accepted == 1u);
    fixture.reject = true;
    assert(cetta_owned_execution_sequence(&execution, 8u,
        owned_sequence_accept, &fixture) == CETTA_OWNED_EXECUTION_PAUSED);
    assert(execution.has_pending && fixture.next == 2u && fixture.accepted == 1u);
    fixture.reject = false;
    assert(cetta_owned_execution_sequence(&execution, 2u,
        owned_sequence_accept, &fixture) == CETTA_OWNED_EXECUTION_PAUSED);
    assert(!execution.has_pending && fixture.next == 3u && fixture.accepted == 3u);
    /* Consuming the last answer is not itself a proof of exhaustion. */
    assert(cetta_owned_execution_sequence(&execution, 1u,
        owned_sequence_accept, &fixture) == CETTA_OWNED_EXECUTION_PAUSED);
    assert(fixture.next == 4u && fixture.accepted == 4u && !execution.terminal);
    assert(cetta_owned_execution_sequence(&execution, 1u,
        owned_sequence_accept, &fixture) == CETTA_OWNED_EXECUTION_COMPLETE);
    assert(execution.accepted == 4u && fixture.attempts == 5u);
    cetta_owned_execution_cancel(&execution);
    cetta_owned_execution_cancel(&execution);
    assert(fixture.releases == 1u && fixture.accepted == 4u);
    /* A sibling is independent, and cancelling a pending return commits
     * neither that occurrence nor a rollback of the accepted prefix. */
    OwnedSequenceFixture sibling = {.values = {fixture.values[0]}, .reject = true};
    cetta_owned_execution_init(&execution, &sibling, owned_sequence_next,
                              owned_sequence_release);
    assert(cetta_owned_execution_sequence(&execution, 1u,
        owned_sequence_accept, &sibling) == CETTA_OWNED_EXECUTION_PAUSED);
    assert(execution.has_pending && sibling.accepted == 0u && fixture.accepted == 4u);
    cetta_owned_execution_cancel(&execution);
    assert(sibling.releases == 1u && sibling.accepted == 0u && fixture.releases == 1u);
    bindings_free(&environment);
    arena_free(&arena);
}

/* A refusal may prepare private consumer state without publishing the
 * occurrence. The next attempt must see that state and the original loan. */
static bool owned_sequence_warming_accept(void *context,
    const CettaOwnedReturn *returned) {
    OwnedSequenceFixture *fixture = context;
    fixture->reject = !fixture->warmed;
    fixture->warmed = true;
    return owned_sequence_accept(context, returned);
}

static void test_owned_stateful_return_boundary(void) {
    Arena arena;
    arena_init_detached(&arena);
    Bindings left, right;
    bindings_init(&left);
    bindings_init(&right);
    Atom *variable = parse(&arena, "$x");
    assert(bindings_add_var(&left, variable, atom_int(&arena, 7)));
    assert(bindings_add_var(&right, variable, atom_int(&arena, 11)));
    Atom *written = parse(&arena, "(println! $x)");
    Atom *condition = parse(&arena, "(pending-goal $x)");
    OwnedSequenceFixture fixture = {
        .values = {
            {.outcome = cetta_call_completed_value(written), .environment = &left},
            {.outcome = cetta_call_completed_value(written), .environment = &right},
            {.outcome = cetta_call_value(written), .environment = &left},
            {.outcome = cetta_call_suspended(written, condition), .environment = &right},
        },
        .observation_arena = &arena, .context_variable = variable,
        .context_values = {7, 11, 7, 11},
    };
    CettaOwnedExecution execution = {0};
    cetta_owned_execution_init(&execution, &fixture, owned_sequence_next,
                              owned_sequence_release);
    assert(cetta_owned_execution_sequence(&execution, 1u,
        owned_sequence_warming_accept, &fixture) == CETTA_OWNED_EXECUTION_PAUSED);
    assert(fixture.warmed && fixture.calls == 1u && fixture.attempts == 1u);
    assert(execution.has_pending && fixture.accepted == 0u && execution.accepted == 0u);
    assert(execution.pending.outcome.term == written && execution.pending.environment == &left);
    assert(cetta_owned_execution_sequence(&execution, 1u,
        owned_sequence_warming_accept, &fixture) == CETTA_OWNED_EXECUTION_PAUSED);
    assert(fixture.calls == 1u && fixture.attempts == 2u && fixture.accepted == 1u);
    assert(!execution.has_pending && execution.accepted == 1u);
    assert(cetta_owned_execution_sequence(&execution, 3u,
        owned_sequence_warming_accept, &fixture) == CETTA_OWNED_EXECUTION_PAUSED);
    assert(fixture.calls == 4u && fixture.attempts == 5u && fixture.accepted == 4u);
    assert(execution.accepted == 4u && !execution.has_pending);
    assert(cetta_owned_execution_sequence(&execution, 1u,
        owned_sequence_warming_accept, &fixture) == CETTA_OWNED_EXECUTION_COMPLETE);
    assert(fixture.calls == 5u && fixture.attempts == 5u);
    cetta_owned_execution_cancel(&execution);
    assert(fixture.releases == 1u);

    /* Combining two allowances is not equivalent to retrying a stateful
     * refusal: the larger single call must still stop at its first refusal. */
    OwnedSequenceFixture unsliced = {.values = {fixture.values[0], fixture.values[1]},
                                    .length = 2u};
    cetta_owned_execution_init(&execution, &unsliced, owned_sequence_next,
                              owned_sequence_release);
    assert(cetta_owned_execution_sequence(&execution, 2u,
        owned_sequence_warming_accept, &unsliced) == CETTA_OWNED_EXECUTION_PAUSED);
    assert(unsliced.warmed && unsliced.calls == 1u && unsliced.attempts == 1u &&
           unsliced.accepted == 0u && execution.has_pending);
    cetta_owned_execution_cancel(&execution);
    assert(unsliced.releases == 1u && unsliced.accepted == 0u);
    bindings_free(&right);
    bindings_free(&left);
    arena_free(&arena);
}


static bool owned_integer_accept(void *context, const CettaOwnedReturn *returned) {
    unsigned *count = context;
    assert(returned->outcome.kind == CETTA_CALL_VALUE);
    assert(returned->outcome.result_form == CETTA_CALL_RESULT_COMPLETED_VALUE);
    assert(returned->environment == NULL && returned->outcome.delayed == NULL);
    assert(returned->outcome.term->ground.ival == *count);
    (*count)++;
    return true;
}

static void test_owned_final_delivery(void) {
    Arena arena;
    arena_init_detached(&arena);
    Atom *value = parse(&arena, "same");
    OwnedSequenceFixture fixture = {
        .length = 2u,
        .values = {
            {.outcome = cetta_call_completed_value(value)},
            {.outcome = cetta_call_completed_value(value), .final = true},
        },
    };
    CettaOwnedExecution execution = {0};
    cetta_owned_execution_init(&execution, &fixture, owned_sequence_next,
                              owned_sequence_release);
    assert(cetta_owned_execution_sequence(&execution, 1u,
        owned_sequence_accept, &fixture) == CETTA_OWNED_EXECUTION_PAUSED);
    assert(fixture.next == 1u && fixture.accepted == 1u && !execution.terminal);
    fixture.reject = true;
    assert(cetta_owned_execution_sequence(&execution, 1u,
        owned_sequence_accept, &fixture) == CETTA_OWNED_EXECUTION_PAUSED);
    assert(fixture.next == 2u && fixture.calls == 2u && fixture.accepted == 1u &&
           execution.has_pending && execution.pending.final);
    unsigned attempts = fixture.attempts;
    assert(cetta_owned_execution_sequence(&execution, 0u,
        owned_sequence_accept, &fixture) == CETTA_OWNED_EXECUTION_PAUSED);
    assert(fixture.next == 2u && fixture.attempts == attempts && execution.has_pending);
    fixture.reject = false;
    assert(cetta_owned_execution_sequence(&execution, 1u,
        owned_sequence_accept, &fixture) == CETTA_OWNED_EXECUTION_COMPLETE);
    assert(fixture.next == 2u && fixture.calls == 2u && fixture.accepted == 2u && execution.accepted == 2u &&
           execution.terminal && !execution.has_pending);
    assert(cetta_owned_execution_sequence(&execution, 1u,
        owned_sequence_accept, &fixture) == CETTA_OWNED_EXECUTION_COMPLETE);
    assert(fixture.next == 2u && fixture.calls == 2u && fixture.attempts == attempts + 1u);
    cetta_owned_execution_cancel(&execution);
    cetta_owned_execution_cancel(&execution);
    assert(fixture.releases == 1u);

    /* A final-return proposal cannot erase an incomplete operation account. */
    OwnedSequenceFixture incomplete = {
        .length = 1u, .completion = CETTA_EVAL_INCOMPLETE_FUEL,
        .values = {{.outcome = cetta_call_completed_value(value), .final = true}},
    };
    cetta_owned_execution_init(&execution, &incomplete, owned_sequence_next,
                              owned_sequence_release);
    assert(cetta_owned_execution_sequence(&execution, 1u,
        owned_sequence_accept, &incomplete) == CETTA_OWNED_EXECUTION_PAUSED);
    assert(incomplete.next == 1u && incomplete.accepted == 1u &&
           !execution.terminal && execution.completion == CETTA_EVAL_INCOMPLETE_FUEL);
    cetta_owned_execution_cancel(&execution);
    assert(incomplete.releases == 1u);
    /* Exhausting the producer does not discharge a returned condition or
     * activate the conditional payload. Its unchanged packet is accepted
     * once, with no extra poll to discover a second answer. */
    Atom *condition = parse(&arena, "(pending-goal $x)");
    OwnedSequenceFixture conditional = {
        .length = 1u,
        .values = {{.outcome = cetta_call_suspended(value, condition), .final = true}},
    };
    cetta_owned_execution_init(&execution, &conditional, owned_sequence_next,
                              owned_sequence_release);
    assert(cetta_owned_execution_sequence(&execution, 1u,
        owned_sequence_accept, &conditional) == CETTA_OWNED_EXECUTION_COMPLETE);
    assert(conditional.calls == 1u && conditional.attempts == 1u && conditional.accepted == 1u);
    assert(conditional.values[0].outcome.kind == CETTA_CALL_SUSPENDED &&
           conditional.values[0].outcome.delayed == condition);
    cetta_owned_execution_cancel(&execution);
    assert(conditional.releases == 1u);
    arena_free(&arena);
}

static void test_cursor_final_frontier(void) {
    const char *equal[] = {"(= (final-pick 0) 0)", "(= (final-pick 0) 0)"};
    Fixture fixture;
    init(&fixture, equal, 2u, "(final-pick 0)");
    CettaPreparedPureAnswerCursor *cursor = open_cursor(&fixture, true);
    assert(!cetta_prepared_pure_answer_cursor_frontier_is_complete(cursor));
    expect_answer(&fixture, cursor, "0");
    assert(!cetta_prepared_pure_answer_cursor_frontier_is_complete(cursor));
    expect_answer(&fixture, cursor, "0");
    size_t polls = fixture.polls;
    assert(cetta_prepared_pure_answer_cursor_frontier_is_complete(cursor));
    assert(cetta_prepared_pure_answer_cursor_frontier_is_complete(cursor));
    assert(fixture.polls == polls);
    /* Withdrawing the last return restores its unconsumed equation. */
    assert(cetta_prepared_pure_answer_cursor_unyield(cursor));
    assert(!cetta_prepared_pure_answer_cursor_frontier_is_complete(cursor));
    expect_answer(&fixture, cursor, "0");
    assert(cetta_prepared_pure_answer_cursor_frontier_is_complete(cursor));
    cetta_prepared_pure_answer_cursor_close(cursor);
    destroy(&fixture);

    const char *pending[] = {"(= (final-pick 0) 0)", "(= (final-pick 0) (empty))"};
    init_with_view(&fixture, pending, 2u, "(final-pick 0)", zero_view);
    cursor = open_cursor(&fixture, true);
    expect_answer(&fixture, cursor, "0");
    /* It would fail, but establishing that requires executing the next body. */
    assert(!cetta_prepared_pure_answer_cursor_frontier_is_complete(cursor));
    Atom *answer = NULL;
    assert(cetta_prepared_pure_answer_cursor_next(cursor, &answer) ==
           CETTA_PREPARED_PURE_CURSOR_EXHAUSTED);
    cetta_prepared_pure_answer_cursor_close(cursor);
    destroy(&fixture);

    const char *continuations[] = {
        "(= (final-leaf $x) $x)", "(= (final-leaf $x) $x)",
        "(= (final-wrap $x) (+ (final-leaf $x) 1))",
    };
    init(&fixture, continuations, 3u, "(final-wrap 0)");
    cursor = open_cursor(&fixture, true);
    expect_answer(&fixture, cursor, "1");
    assert(!cetta_prepared_pure_answer_cursor_frontier_is_complete(cursor));
    expect_answer(&fixture, cursor, "1");
    assert(cetta_prepared_pure_answer_cursor_frontier_is_complete(cursor));
    assert(space_admit_atom(&fixture.space, &fixture.source,
                           parse(&fixture.source, "(= (final-leaf $x) 99)")));
    assert(!cetta_prepared_pure_answer_cursor_frontier_is_complete(cursor));
    cetta_prepared_pure_answer_cursor_close(cursor);
    destroy(&fixture);

    const char *known[] = {"(= (final-pick 0) 0)"};
    init(&fixture, known, 1u, "(final-pick 1)");
    cursor = open_cursor(&fixture, true);
    assert(!cetta_prepared_pure_answer_cursor_frontier_is_complete(cursor));
    assert(cetta_prepared_pure_answer_cursor_next(cursor, &answer) ==
           CETTA_PREPARED_PURE_CURSOR_HANDOFF);
    assert(!cetta_prepared_pure_answer_cursor_frontier_is_complete(cursor));
    cetta_prepared_pure_answer_cursor_close(cursor);
    destroy(&fixture);
}

static void test_owned_cursor_final_answer(void) {
    const char *equations[] = {"(= (owned-final 0) 0)"};
    Fixture fixture;
    init(&fixture, equations, 1u, "(owned-final 0)");
    CettaPreparedPureAnswerCursor *cursor = open_cursor(&fixture, true);
    CettaOwnedExecution execution = {0};
    assert(cetta_prepared_pure_answer_cursor_own_execution(&execution, cursor));
    unsigned count = 0u;
    assert(cetta_owned_execution_sequence(&execution, 1u, owned_integer_accept, &count) ==
           CETTA_OWNED_EXECUTION_COMPLETE);
    assert(count == 1u && fixture.polls == 1u && execution.accepted == 1u && execution.terminal);
    size_t polls = fixture.polls;
    assert(cetta_owned_execution_sequence(&execution, 1u, owned_integer_accept, &count) ==
           CETTA_OWNED_EXECUTION_COMPLETE);
    assert(fixture.polls == polls && count == 1u);
    cetta_owned_execution_cancel(&execution);
    destroy(&fixture);
}

static void test_owned_cursor_transfer_is_exclusive(void) {
    const char *equations[] = {"(= (owned-final 0) 0)"};
    for (unsigned bounded = 0u; bounded < 2u; ++bounded) {
        Fixture fixture;
        init(&fixture, equations, 1u, "(owned-final 0)");
        CettaPreparedPureAnswerCursor *cursor = open_cursor(&fixture, true);
        CettaPreparedPureAnswerCursor *other = open_cursor(&fixture, true);
        CettaOwnedExecution execution = {0}, rejected = {0};
        assert(!cetta_prepared_pure_answer_cursor_own_execution(NULL, cursor));
        assert(!cetta_prepared_pure_answer_cursor_own_execution(&execution, NULL));
        if (bounded)
            assert(cetta_prepared_pure_answer_cursor_own_execution_bounded(
                &execution, cursor, 1u));
        else
            assert(cetta_prepared_pure_answer_cursor_own_execution(&execution, cursor));
        CettaOwnedExecution before = execution;
        assert(!cetta_prepared_pure_answer_cursor_own_execution(&rejected, cursor));
        assert(!cetta_prepared_pure_answer_cursor_own_execution_bounded(
            &rejected, cursor, 0u));
        assert(!cetta_prepared_pure_answer_cursor_own_execution(&execution, other));
        assert(!cetta_prepared_pure_answer_cursor_own_execution_bounded(
            &execution, other, 0u));
        assert(memcmp(&before, &execution, sizeof(execution)) == 0);
        assert(!rejected.computation && !rejected.has_pending && fixture.polls == 0u);
        /* Refusing the occupied recipient must not acquire the second cursor. */
        rejected.has_pending = true;
        assert(!cetta_prepared_pure_answer_cursor_own_execution(&rejected, other));
        rejected.has_pending = false;
        assert(cetta_prepared_pure_answer_cursor_own_execution(&rejected, other));
        unsigned count = 0u;
        for (unsigned slice = 0u; !execution.terminal && slice < 8u; ++slice) {
            CettaOwnedExecutionStep step = cetta_owned_execution_sequence(
                &execution, 1u, owned_integer_accept, &count);
            assert(step == CETTA_OWNED_EXECUTION_PAUSED ||
                   step == CETTA_OWNED_EXECUTION_COMPLETE);
        }
        assert(execution.terminal && count == 1u);
        cetta_owned_execution_cancel(&execution);
        cetta_owned_execution_cancel(&rejected);
        /* A retired receiving driver can acquire a different live cursor. */
        assert(cetta_prepared_pure_answer_cursor_own_execution(
            &execution, open_cursor(&fixture, true)));
        cetta_owned_execution_cancel(&execution);
        destroy(&fixture);
    }
}

typedef struct {
    Fixture *fixture;
    unsigned attempts, effects;
    bool reject, change_on_accept;
} FinalReplyConsumer;

static bool final_reply_accept(void *context, const CettaOwnedReturn *returned) {
    FinalReplyConsumer *consumer = context;
    consumer->attempts++;
    assert(returned->outcome.kind == CETTA_CALL_VALUE &&
           returned->outcome.result_form == CETTA_CALL_RESULT_COMPLETED_VALUE &&
           returned->outcome.term->ground.ival == 0 &&
           returned->environment == NULL);
    if (consumer->reject)
        return false;
    if (consumer->change_on_accept) {
        Fixture *fixture = consumer->fixture;
        assert(space_admit_atom(&fixture->space, &fixture->source,
            parse(&fixture->source, "(= (owned-final 0) 1)")));
    }
    consumer->effects++;
    return true;
}

static void test_owned_final_reply_pause_and_effects(void) {
    const char *equations[] = {
        "(= (owned-final 0) 0)", "(= (owned-final 0) 0)",
    };
    Fixture fixture;
    init(&fixture, equations, 2u, "(owned-final 0)");
    CettaPreparedPureAnswerCursor *cursor = open_cursor(&fixture, true);
    CettaOwnedExecution execution = {0};
    assert(cetta_prepared_pure_answer_cursor_own_execution(&execution, cursor));
    FinalReplyConsumer consumer = {.fixture = &fixture};
    assert(cetta_owned_execution_sequence(&execution, 1u,
        final_reply_accept, &consumer) == CETTA_OWNED_EXECUTION_PAUSED);
    assert(consumer.effects == 1u && execution.accepted == 1u);
    consumer.reject = true;
    assert(cetta_owned_execution_sequence(&execution, 1u,
        final_reply_accept, &consumer) == CETTA_OWNED_EXECUTION_PAUSED);
    assert(execution.has_pending && execution.pending.final && consumer.effects == 1u);
    unsigned attempts = consumer.attempts;
    size_t polls = fixture.polls;
    assert(cetta_owned_execution_sequence(&execution, 0u,
        final_reply_accept, &consumer) == CETTA_OWNED_EXECUTION_PAUSED);
    assert(consumer.attempts == attempts && fixture.polls == polls);
    assert(space_admit_atom(&fixture.space, &fixture.source,
        parse(&fixture.source, "(unrelated fact)")));
    assert(cetta_prepared_pure_answer_cursor_frontier_is_complete(cursor));
    consumer.reject = false;
    assert(cetta_owned_execution_sequence(&execution, 1u,
        final_reply_accept, &consumer) == CETTA_OWNED_EXECUTION_COMPLETE);
    assert(consumer.effects == 2u && execution.accepted == 2u &&
           fixture.polls == polls && !execution.has_pending);
    cetta_owned_execution_cancel(&execution);
    cetta_owned_execution_cancel(&execution);
    assert(consumer.effects == 2u);
    destroy(&fixture);
}

static void test_owned_bounded_cursor_pending_reply(void) {
    const char *equations[] = {
        "(= (descend (S $n)) (descend $n))", "(= (descend Z) 0)",
        "(= (owned-final 0) (descend (S (S Z))))",
        "(= (owned-final 0) (descend (S (S Z))))",
    };
    Fixture fixture;
    init(&fixture, equations, 4u, "(owned-final 0)");
    CettaPreparedPureAnswerCursor *cursor = open_cursor(&fixture, true);
    CettaOwnedExecution execution = {0};
    assert(cetta_prepared_pure_answer_cursor_own_execution_bounded(&execution, cursor, 1u));
    FinalReplyConsumer consumer = {.fixture = &fixture, .reject = true};
    assert(cetta_owned_execution_sequence(&execution, 0u,
        final_reply_accept, &consumer) == CETTA_OWNED_EXECUTION_PAUSED);
    assert(fixture.polls == 0u && consumer.attempts == 0u && !execution.has_pending);

    for (unsigned slice = 0u; slice < 64u && !execution.has_pending; ++slice) {
        size_t polls = fixture.polls;
        assert(cetta_owned_execution_sequence(&execution, 1u,
            final_reply_accept, &consumer) == CETTA_OWNED_EXECUTION_PAUSED);
        assert(!execution.terminal && fixture.polls - polls <= 1u &&
               execution.accepted == 0u && consumer.effects == 0u);
    }
    assert(execution.has_pending && consumer.attempts == 1u);
    size_t polls = fixture.polls;
    assert(cetta_owned_execution_sequence(&execution, 0u,
        final_reply_accept, &consumer) == CETTA_OWNED_EXECUTION_PAUSED);
    assert(fixture.polls == polls && consumer.attempts == 1u);
    consumer.reject = false;
    assert(cetta_owned_execution_sequence(&execution, 1u,
        final_reply_accept, &consumer) == CETTA_OWNED_EXECUTION_PAUSED);
    assert(fixture.polls == polls && execution.accepted == 1u &&
           consumer.effects == 1u && !execution.has_pending);

    for (unsigned slice = 0u; slice < 128u && !execution.terminal; ++slice) {
        polls = fixture.polls;
        CettaOwnedExecutionStep step = cetta_owned_execution_sequence(&execution, 1u,
            final_reply_accept, &consumer);
        assert(step == CETTA_OWNED_EXECUTION_PAUSED ||
               step == CETTA_OWNED_EXECUTION_COMPLETE);
        assert(fixture.polls - polls <= 1u && execution.accepted <= 2u);
    }
    assert(execution.terminal && execution.stop == CETTA_OWNED_EXECUTION_COMPLETE &&
           execution.accepted == 2u && consumer.effects == 2u && consumer.attempts == 3u);
    polls = fixture.polls;
    assert(cetta_owned_execution_sequence(&execution, 1u,
        final_reply_accept, &consumer) == CETTA_OWNED_EXECUTION_COMPLETE);
    assert(fixture.polls == polls && consumer.effects == 2u);
    cetta_owned_execution_cancel(&execution);
    cetta_owned_execution_cancel(&execution);
    destroy(&fixture);
}

static void test_owned_bounded_cursor_silent_loop(void) {
    const char *equations[] = {"(= (spin $x) (spin $x))"};
    for (uint64_t quantum = 0u; quantum <= 1u; ++quantum) {
        Fixture fixture;
        init(&fixture, equations, 1u, "(spin seed)");
        CettaPreparedPureAnswerCursor *cursor = open_cursor(&fixture, false);
        CettaOwnedExecution execution = {0};
        assert(cetta_prepared_pure_answer_cursor_own_execution_bounded(&execution, cursor, quantum));
        unsigned count = 0u;
        for (unsigned slice = 0u; slice < 8u; ++slice) {
            assert(cetta_owned_execution_sequence(&execution, 1u,
                owned_integer_accept, &count) == CETTA_OWNED_EXECUTION_PAUSED);
            assert(!execution.terminal && !execution.has_pending &&
                   execution.accepted == 0u && count == 0u &&
                   fixture.polls == (slice + 1u) * quantum);
        }
        assert(cetta_prepared_pure_answer_cursor_frame_count(cursor) == 1u);
        cetta_owned_execution_cancel(&execution);
        assert(execution.terminal && !execution.computation &&
               execution.completion == CETTA_EVAL_INCOMPLETE_CANCELLED);
        destroy(&fixture);
    }
}

static void test_owned_bounded_cursor_peer_service(void) {
    const char *busy_equations[] = {"(= (spin $x) (spin $x))"};
    const char *service_equations[] = {
        "(= (respond (S $n)) (respond $n))", "(= (respond Z) 0)",
    };
    Fixture busy, service;
    init(&busy, busy_equations, 1u, "(spin seed)");
    init(&service, service_equations, 2u, "(respond (S (S Z)))");
    CettaOwnedExecution busy_owner = {0}, service_owner = {0};
    assert(cetta_prepared_pure_answer_cursor_own_execution_bounded(
        &busy_owner, open_cursor(&busy, false), 1u));
    assert(cetta_prepared_pure_answer_cursor_own_execution_bounded(
        &service_owner, open_cursor(&service, false), 1u));
    unsigned busy_count = 0u, service_count = 0u;
    /* A yielding operation cannot by itself service an unselected peer. */
    for (unsigned round = 0u; round < 16u; ++round)
        assert(cetta_owned_execution_sequence(&busy_owner, 1u,
            owned_integer_accept, &busy_count) == CETTA_OWNED_EXECUTION_PAUSED);
    assert(service.polls == 0u && service_count == 0u && !service_owner.terminal);
    /* Alternating the same owners lets the finite request finish while
     * the silent computation keeps its unfinished call. */
    for (unsigned round = 0u; round < 32u && !service_owner.terminal; ++round) {
        assert(cetta_owned_execution_sequence(&busy_owner, 1u,
            owned_integer_accept, &busy_count) == CETTA_OWNED_EXECUTION_PAUSED);
        CettaOwnedExecutionStep step = cetta_owned_execution_sequence(
            &service_owner, 1u, owned_integer_accept, &service_count);
        assert(step == CETTA_OWNED_EXECUTION_PAUSED ||
               step == CETTA_OWNED_EXECUTION_COMPLETE);
    }
    assert(service_owner.terminal && service_owner.stop == CETTA_OWNED_EXECUTION_COMPLETE &&
           service_count == 1u && service_owner.accepted == 1u &&
           !busy_owner.terminal && busy_count == 0u);
    cetta_owned_execution_cancel(&busy_owner);
    cetta_owned_execution_cancel(&service_owner);
    destroy(&busy);
    destroy(&service);
}

static void test_owned_final_authority_revalidation(void) {
    const char *equations[] = {"(= (owned-final 0) 0)"};
    for (unsigned during_accept = 0u; during_accept < 2u; ++during_accept) {
        Fixture fixture;
        init(&fixture, equations, 1u, "(owned-final 0)");
        CettaPreparedPureAnswerCursor *cursor = open_cursor(&fixture, true);
        CettaOwnedExecution execution = {0};
        assert(cetta_prepared_pure_answer_cursor_own_execution(&execution, cursor));
        FinalReplyConsumer consumer = {
            .fixture = &fixture, .reject = !during_accept,
            .change_on_accept = during_accept,
        };
        if (!during_accept) {
            assert(cetta_owned_execution_sequence(&execution, 1u,
                final_reply_accept, &consumer) == CETTA_OWNED_EXECUTION_PAUSED);
            assert(execution.has_pending && execution.pending.final && consumer.effects == 0u);
            assert(space_admit_atom(&fixture.space, &fixture.source,
                parse(&fixture.source, "(= (owned-final 0) 1)")));
            assert(!cetta_prepared_pure_answer_cursor_frontier_is_complete(cursor));
            consumer.reject = false;
        }
        assert(cetta_owned_execution_sequence(&execution, 1u,
            final_reply_accept, &consumer) == CETTA_OWNED_EXECUTION_PAUSED);
        assert(consumer.effects == 1u && execution.accepted == 1u &&
               !execution.has_pending && !execution.terminal);
        assert(cetta_owned_execution_sequence(&execution, 1u,
            final_reply_accept, &consumer) == CETTA_OWNED_EXECUTION_HANDOFF);
        assert(execution.completion == CETTA_EVAL_INCOMPLETE_INVALIDATED &&
               consumer.effects == 1u && execution.accepted == 1u);
        cetta_owned_execution_cancel(&execution);
        assert(consumer.effects == 1u);
        destroy(&fixture);
    }
}

static void test_owned_cursor_authority(void) {
    const char *equations[] = {
        "(= (owned-ticks $n) $n)",
        "(= (owned-ticks $n) (owned-ticks (+ $n 1)))",
    };
    Fixture fixture;
    init(&fixture, equations, 2u, "(owned-ticks 0)");
    CettaPreparedPureAnswerCursor *cursor = open_cursor(&fixture, false);
    CettaOwnedExecution execution = {0};
    assert(cetta_prepared_pure_answer_cursor_own_execution(&execution, cursor));
    unsigned count = 0u;
    assert(cetta_owned_execution_sequence(&execution, 1u, owned_integer_accept, &count) ==
           CETTA_OWNED_EXECUTION_PAUSED);
    assert(count == 1u && execution.accepted == 1u);
    /* Retained code remains alive but has lost its source read authority. */
    cetta_prepared_pure_program_free(fixture.program);
    fixture.program = NULL;
    assert(space_admit_atom(&fixture.space, &fixture.source,
                           parse(&fixture.source, "(= (owned-ticks $n) 999)")));
    assert(cetta_owned_execution_sequence(&execution, 1u, owned_integer_accept, &count) ==
           CETTA_OWNED_EXECUTION_HANDOFF);
    assert(execution.completion == CETTA_EVAL_INCOMPLETE_INVALIDATED);
    assert(count == 1u && execution.accepted == 1u && execution.terminal);
    cetta_owned_execution_cancel(&execution);
    cetta_owned_execution_cancel(&execution);
    destroy(&fixture);
}

typedef struct {
    unsigned references, releases;
    unsigned char *bytes;
} OwnedBytes;

static void owned_bytes_retain(void *context) {
    OwnedBytes *bytes = context;
    assert(bytes->bytes && bytes->releases == 0u);
    bytes->references++;
}

static void owned_bytes_release(void *context) {
    OwnedBytes *bytes = context;
    assert(bytes->references > 0u);
    if (--bytes->references == 0u) {
        free(bytes->bytes);
        bytes->bytes = NULL;
        bytes->releases++;
    }
}

typedef struct {
    Arena arena;
    Atom *views[2];
    unsigned position, releases;
} OwnedViewProducer;

static CettaOwnedExecutionStep owned_view_next(void *context,
    CettaOwnedReturn *returned, CettaEvalCompletion *completion) {
    OwnedViewProducer *producer = context;
    assert(producer->releases == 0u);
    *completion = CETTA_EVAL_COMPLETE;
    if (producer->position == 2u)
        return CETTA_OWNED_EXECUTION_COMPLETE;
    *returned = (CettaOwnedReturn){
        .outcome = cetta_call_completed_value(producer->views[producer->position++]),
    };
    return CETTA_OWNED_EXECUTION_RETURN;
}

static void owned_view_release(void *context) {
    OwnedViewProducer *producer = context;
    assert(producer->releases++ == 0u);
    arena_free(&producer->arena);
}

static bool owned_view_pause(void *context, const CettaOwnedReturn *returned) {
    unsigned *attempts = context;
    assert(returned->outcome.result_form == CETTA_CALL_RESULT_COMPLETED_VALUE);
    assert(returned->outcome.term->expr.len == 4u);
    (*attempts)++;
    return false;
}

static Atom *owned_payload_map_leaf(Arena *destination, Atom *source, void *context) {
    (void)context;
    return atom_deep_copy(destination, source);
}

/* Large immutable storage is retained by its actual native owner. Distinct
 * capture spans share the handle; publishing a pending view precedes cancel.
 * No tensor mutation or implicit exclusivity is inferred from these views. */
static void test_owned_shared_payload(void) {
    Arena source, publication;
    arena_init_detached(&source);
    arena_init_detached(&publication);
    ArenaMark empty = arena_mark(&source);
    OwnedBytes bytes = {.bytes = malloc(65536u)};
    assert(bytes.bytes);
    memset(bytes.bytes, 'x', 65536u);
    const unsigned char expected[] = {'a', 0, 0xc3, 0xb1, 'z'};
    memcpy(bytes.bytes + 17u, expected, sizeof(expected));
    Atom *handle = atom_native_handle_identifier(&source, 42, &bytes,
                                                owned_bytes_retain, owned_bytes_release);
    Atom *view = atom_expr(&source, (Atom *[]){atom_symbol(&source, "ByteSpan"),
        handle, atom_int(&source, 17), atom_int(&source, 5)}, 4u);
    Atom *shifted = atom_expr(&source, (Atom *[]){view->expr.elems[0],
        handle, atom_int(&source, 18), atom_int(&source, 4)}, 4u);
    OwnedViewProducer producer = {0};
    arena_init_detached(&producer.arena);
    AtomDeepCopySession *copy = atom_deep_copy_session_new_mapped(
        &producer.arena, owned_payload_map_leaf, NULL);
    assert(copy);
    producer.views[0] = atom_deep_copy_session_copy(copy, view);
    producer.views[1] = atom_deep_copy_session_copy(copy, shifted);
    atom_deep_copy_session_free(copy);
    assert(producer.views[0]->expr.elems[1] == producer.views[1]->expr.elems[1]);
    CettaOwnedExecution execution = {0};
    cetta_owned_execution_init(&execution, &producer, owned_view_next, owned_view_release);
    unsigned attempts = 0u;
    assert(cetta_owned_execution_sequence(&execution, 1u, owned_view_pause, &attempts) ==
           CETTA_OWNED_EXECUTION_PAUSED);
    assert(execution.has_pending && producer.position == 1u && attempts == 1u);
    arena_reset(&source, empty);
    assert(bytes.releases == 0u && memcmp(bytes.bytes + 17u, expected, sizeof(expected)) == 0);
    Atom *published = atom_deep_copy(&publication, execution.pending.outcome.term);
    assert(published && published->expr.elems[2]->ground.ival == 17);
    assert(cetta_owned_execution_sequence(&execution, 1u, owned_view_pause, &attempts) ==
           CETTA_OWNED_EXECUTION_PAUSED);
    assert(producer.position == 1u && attempts == 2u && execution.accepted == 0u);
    cetta_owned_execution_cancel(&execution);
    assert(producer.releases == 1u && bytes.releases == 0u && bytes.references > 0u);
    assert(memcmp(bytes.bytes + published->expr.elems[2]->ground.ival,
                  expected, sizeof(expected)) == 0);
    arena_free(&publication);
    assert(bytes.references == 0u && bytes.releases == 1u && bytes.bytes == NULL);
    arena_free(&source);
    printf("owned shared payload: 65536 bytes, aliased spans, paused return, source reset, "
           "publication and exactly one final release\n");
}

#if CETTA_MANAGED_SYMBOLS
static void test_spelled_value_across_prepared_choices(void) {
    const char *equations[] = {"(= (pick $x) $x)", "(= (pick $x) $x)"};
    Fixture fixture;
    init(&fixture, equations, 2u, "(pick seed)");
    cetta_prepared_pure_program_free(fixture.program);
    Arena temporary, output;
    arena_init(&temporary);
    arena_init(&output);
    const char payload[] = {'p', 0, (char)0xff, 'q'};
    Atom *birth = atom_symbol_data_n(&temporary, payload, sizeof(payload));
    Atom *retained = atom_symbol_copy(&fixture.source, birth);
    arena_free(&temporary);
    Atom *call = atom_expr2(&fixture.source,
        atom_symbol(&fixture.source, "pick"), retained);
    fixture.program = cetta_prepared_pure_program_compile_closed_answers(
        &fixture.space, call, CETTA_GSLT_PURE_CALL_EAGER, atom_bool, atom_expr,
        cetta_prepared_pure_constructs_expression_always,
        NULL, NULL, NULL, NULL, NULL, true, true,
        (CettaMatchDecisionSemanticIdentity){
            .compiler_identity = cetta_match_decision_compiler_identity()});
    assert(fixture.program);
    CettaPreparedPureAnswerCursor *cursor = open_cursor(&fixture, false);
    Atom *saved = NULL;
    for (unsigned i = 0u; i < 2u; i++) {
        Atom *answer = NULL;
        assert(cetta_prepared_pure_answer_cursor_next(cursor, &answer) ==
               CETTA_PREPARED_PURE_CURSOR_ANSWER);
        assert(atom_symbol_is_spelled(answer));
        assert(atom_symbol_length(answer) == sizeof(payload));
        assert(memcmp(atom_symbol_bytes(answer), payload, sizeof(payload)) == 0);
        if (!saved) saved = atom_symbol_copy(&output, answer);
    }
    Atom *answer = NULL;
    assert(cetta_prepared_pure_answer_cursor_next(cursor, &answer) ==
           CETTA_PREPARED_PURE_CURSOR_EXHAUSTED);
    assert(atom_symbol_current_id(retained) == SYMBOL_ID_NONE);
    cetta_prepared_pure_answer_cursor_close(cursor);
    destroy(&fixture);
    assert(atom_symbol_length(saved) == sizeof(payload));
    assert(memcmp(atom_symbol_bytes(saved), payload, sizeof(payload)) == 0);
    arena_free(&output);
}
#endif

static CettaPreparedPureProgram *compile_optional(Fixture *fixture, const char *call,
                                                 bool values) {
    return cetta_prepared_pure_program_compile_closed_optional(
        &fixture->space, parse(&fixture->source, call),
        CETTA_GSLT_PURE_CALL_EAGER, atom_bool, atom_expr,
        cetta_prepared_pure_constructs_expression_always,
        NULL, NULL, zero_view, NULL, NULL, values, true,
        (CettaMatchDecisionSemanticIdentity){
            .compiler_identity = cetta_match_decision_compiler_identity(),
        });
}

static void test_optional_register_worker(void) {
    const char *equations[] = {
        "(= (pick Nil) (empty))",
        "(= (pick (Cons $x $xs)) (if (== $x 0) (empty) (Pair $x $xs)))",
        "(= (wrap $xs) (Pair result (pick $xs)))",
        "(= (walk Nil) (empty))",
        "(= (walk (Cons $x $xs)) (if (== $x 0) found (walk $xs)))",
        "(= (spin $x) (spin $x))",
    };
    Fixture fixture;
    init_with_view(&fixture, equations, 6u, "(pick Nil)", zero_view);
    assert(run(&fixture, NULL) == CETTA_PREPARED_PURE_ANSWERS_COMPLETE);
    assert(fixture.answers == 0u);
    CettaPreparedPureProgram *optional = compile_optional(&fixture, "(pick Nil)", true);
    assert(optional);
    Atom *answer = parse(&fixture.source, "stale-output");
    fixture.polls=0u;
    assert(cetta_prepared_pure_program_execute_closed_optional(
        optional, &fixture.scratch, 0u, interrupt, &fixture, &answer) ==
        CETTA_PREPARED_PURE_OPTIONAL_EMPTY);
    assert(!answer && fixture.polls == 1u);
    assert(!cetta_prepared_pure_program_execute_closed(
        optional, &fixture.scratch, 0u, &answer));
    assert(!answer);
    assert(!cetta_prepared_pure_closed_open(optional, &fixture.scratch, &fixture));
    assert(cetta_prepared_pure_program_rebind_closed_entry_call(
        optional, parse(&fixture.source, "(pick (Cons 7 Nil))")));
    assert(cetta_prepared_pure_program_execute_closed_optional(
        optional, &fixture.scratch, 0u, NULL, NULL, &answer) ==
        CETTA_PREPARED_PURE_OPTIONAL_VALUE);
    assert(strcmp(atom_to_string(&fixture.source, answer), "(Pair 7 Nil)") == 0);
    assert(cetta_prepared_pure_program_rebind_closed_entry_call(
        optional, parse(&fixture.source, "(pick (Cons 0 Nil))")));
    assert(cetta_prepared_pure_program_execute_closed_optional(
        optional, &fixture.scratch, 0u, NULL, NULL, &answer) ==
        CETTA_PREPARED_PURE_OPTIONAL_EMPTY);
    assert(!answer);
    /* No-rule observation is a separate dialect policy, not zero. */
    assert(cetta_prepared_pure_program_rebind_closed_entry_call(
        optional, parse(&fixture.source, "(pick Other)")));
    assert(cetta_prepared_pure_program_execute_closed_optional(
        optional, &fixture.scratch, 0u, NULL, NULL, &answer) ==
        CETTA_PREPARED_PURE_OPTIONAL_DECLINED);
    assert(!answer);
    assert(!compile_optional(&fixture, "(pick $free)", true));
    assert(!compile_optional(&fixture, "(pick Nil)", false));
    assert(space_admit_atom(&fixture.space, &fixture.source,
        parse(&fixture.source, "(= (pick Nil) later)")));
    assert(cetta_prepared_pure_program_execute_closed_optional(
        optional, &fixture.scratch, 0u, NULL, NULL, &answer) ==
        CETTA_PREPARED_PURE_OPTIONAL_INVALIDATED);
    assert(!answer && !compile_optional(&fixture, "(pick Nil)", true));
    cetta_prepared_pure_program_free(optional);
    destroy(&fixture);
    init_with_view(&fixture, equations, 6u, "(wrap Nil)", zero_view);
    assert(run(&fixture, NULL) == CETTA_PREPARED_PURE_ANSWERS_COMPLETE);
    assert(!fixture.answers);
    optional = compile_optional(&fixture, "(wrap Nil)", true);
    assert(optional);
    assert(cetta_prepared_pure_program_execute_closed_optional(
        optional, &fixture.scratch, 0u, NULL, NULL, &answer) ==
        CETTA_PREPARED_PURE_OPTIONAL_EMPTY);
    assert(!answer);
    cetta_prepared_pure_program_free(optional);
    optional = compile_optional(&fixture, "(walk (Cons 9 (Cons 0 Nil)))", true);
    assert(optional);
    assert(cetta_prepared_pure_program_execute_closed_optional(
        optional, &fixture.scratch, 0u, NULL, NULL, &answer) ==
        CETTA_PREPARED_PURE_OPTIONAL_VALUE);
    assert(strcmp(atom_to_string(&fixture.source, answer), "found") == 0);
    assert(cetta_prepared_pure_program_rebind_closed_entry_call(
        optional, parse(&fixture.source, "(walk (Cons 9 Nil))")));
    assert(cetta_prepared_pure_program_execute_closed_optional(
        optional, &fixture.scratch, 0u, NULL, NULL, &answer) ==
        CETTA_PREPARED_PURE_OPTIONAL_EMPTY);
    assert(!answer);
    cetta_prepared_pure_program_free(optional);
    optional = compile_optional(&fixture, "(spin seed)", true);
    assert(optional);
    fixture.polls=0u; fixture.poll_limit=1u;
    assert(cetta_prepared_pure_program_execute_closed_optional(
        optional, &fixture.scratch, 0u, interrupt, &fixture, &answer) ==
        CETTA_PREPARED_PURE_OPTIONAL_INTERRUPTED);
    assert(!answer && fixture.polls == 1u);
    cetta_prepared_pure_program_free(optional);
    destroy(&fixture);
}

static void test_optional_declines_alternatives_and_faults(void) {
    const char *equations[] = {
        "(= (pick $x) same)", "(= (pick $x) same)",
        "(= (bad $x) (/ 1 $x))",
        "(= (none) (empty))",
    };
    Fixture fixture;
    init(&fixture, equations, 4u, "(pick seed)");
    fixture.expected[0] = fixture.expected[1] = "same";
    assert(run(&fixture, NULL) == CETTA_PREPARED_PURE_ANSWERS_COMPLETE);
    assert(fixture.answers == 2u);
    assert(!compile_optional(&fixture, "(pick seed)", true));
    CettaPreparedPureProgram *optional = compile_optional(&fixture, "(bad 0)", true);
    assert(optional);
    Atom *answer = NULL;
    assert(cetta_prepared_pure_program_execute_closed_optional(
        optional, &fixture.scratch, 0u, NULL, NULL, &answer) ==
        CETTA_PREPARED_PURE_OPTIONAL_DECLINED);
    assert(!answer);
    cetta_prepared_pure_program_free(optional);
    optional = compile_optional(&fixture, "(none)", true);
    assert(optional);
    assert(cetta_prepared_pure_program_execute_closed_optional(
        optional, &fixture.scratch, 0u, NULL, NULL, &answer) ==
        CETTA_PREPARED_PURE_OPTIONAL_EMPTY);
    assert(!answer);
    cetta_prepared_pure_program_free(optional);
    destroy(&fixture);
}

static void expect_same_optional_cut(CettaPreparedClosedState before,
                                      CettaPreparedClosedState after) {
    assert(before.performed == after.performed);
    assert(before.terminal_dispatches == after.terminal_dispatches);
    assert(before.frames == after.frames && before.slots == after.slots);
    assert(before.pc == after.pc && before.base == after.base);
    assert(before.memo_entries == after.memo_entries);
    assert(before.frame_storage == after.frame_storage);
    assert(before.slot_storage == after.slot_storage);
    assert(before.status == after.status);
    assert(before.pending_answer == after.pending_answer);
}

/* Splitting execution retains the instruction frontier and its paid work.
 * Delivery acknowledgement is separate from observing a completed value. */
static void test_optional_retained_prefix_and_delivery(void) {
    const char *equations[] = {
        "(= (walk Nil) (empty))",
        "(= (walk (Cons $x $xs)) (if (== $x 0) (Box $x $xs) (walk $xs)))",
    };
    const char *calls[] = {
        "(walk (Cons 8 (Cons 7 (Cons 6 (Cons 5 (Cons 4 (Cons 3 (Cons 2 (Cons 1 (Cons 0 Nil))))))))))",
        "(walk (Cons 8 (Cons 7 (Cons 6 (Cons 5 (Cons 4 (Cons 3 (Cons 2 (Cons 1 Nil)))))))))",
    };
    for (size_t case_index = 0u; case_index < 2u; case_index++) {
        Fixture fixture;
        init_with_view(&fixture, equations, 2u, calls[case_index], zero_view);
        CettaPreparedPureProgram *optional = compile_optional(&fixture, calls[case_index], true);
        assert(optional);
        CettaPreparedPureClosedExecution *whole = cetta_prepared_pure_closed_open_optional(
            optional, &fixture.scratch, &fixture);
        assert(whole && !cetta_prepared_pure_closed_open_optional(
            optional, &fixture.scratch, &fixture));
        uint64_t spent = 0u;
        Atom *answer = NULL;
        CettaPreparedClosedStep terminal = case_index == 0u
            ? CETTA_PREPARED_CLOSED_ANSWER : CETTA_PREPARED_CLOSED_EXHAUSTED;
        assert(cetta_prepared_pure_closed_step(whole, &fixture.scratch, &fixture,
            100000u, &spent, &answer) == terminal);
        assert(spent > 8u);
        uint64_t whole_spent = spent;
        assert((answer != NULL) == (case_index == 0u));
        if (answer) assert(atom_eq(answer, parse(&fixture.source, "(Box 0 Nil)")));
        assert(cetta_prepared_pure_closed_close(whole, &fixture));

        CettaPreparedPureClosedExecution *sliced = cetta_prepared_pure_closed_open_optional(
            optional, &fixture.scratch, &fixture);
        assert(sliced);
        assert(cetta_prepared_pure_closed_owned_by(sliced, optional,
            &fixture.scratch, &fixture));
        assert(!cetta_prepared_pure_program_rebind_closed_entry_call(
            optional, parse(&fixture.source, "(walk Nil)")));
        CettaPreparedClosedState before, after;
        assert(cetta_prepared_pure_closed_read(sliced, &before));
        assert(before.optional_result && before.register_code && !before.pending_answer);
        assert(cetta_prepared_pure_closed_step(sliced, &fixture.scratch, &fixture,
            0u, &spent, &answer) == CETTA_PREPARED_CLOSED_SUSPENDED);
        assert(spent == 0u && !answer);
        assert(cetta_prepared_pure_closed_read(sliced, &after));
        expect_same_optional_cut(before, after);
        assert(cetta_prepared_pure_closed_step(sliced, &fixture.scratch, optional,
            1u, &spent, &answer) == CETTA_PREPARED_CLOSED_REFUSED);
        assert(spent == 0u && !answer);
        assert(cetta_prepared_pure_closed_read(sliced, &after));
        expect_same_optional_cut(before, after);
        uint64_t total = 0u;
        CettaPreparedClosedStep status;
        do {
            status = cetta_prepared_pure_closed_step(sliced, &fixture.scratch, &fixture,
                1u, &spent, &answer);
            assert(spent == 1u && ++total <= whole_spent);
            assert(cetta_prepared_pure_closed_read(sliced, &after));
            assert(after.performed == total && after.terminal_dispatches == 0u);
        } while (status == CETTA_PREPARED_CLOSED_SUSPENDED);
        assert(status == terminal && total == whole_spent);
        Atom *pending = answer;
        before = after;
        for (size_t observer = 0u; observer < 3u; observer++) {
            assert(cetta_prepared_pure_closed_step(sliced, &fixture.scratch, &fixture,
                observer ? 100u : 0u, &spent, &answer) == terminal);
            assert(spent == 0u && answer == pending);
            assert(cetta_prepared_pure_closed_read(sliced, &after));
            expect_same_optional_cut(before, after);
        }
        if (pending) {
            assert(!cetta_prepared_pure_closed_accept_optional(sliced, optional, pending));
            assert(!cetta_prepared_pure_closed_accept_optional(
                sliced, &fixture, parse(&fixture.source, "(Box 0 Nil)")));
            assert(cetta_prepared_pure_closed_read(sliced, &after));
            expect_same_optional_cut(before, after);
        }
        /* An earned empty result or pending value is not reopened by new equations. */
        assert(space_admit_atom(&fixture.space, &fixture.source,
            parse(&fixture.source, "(= (walk Nil) later)")));
        assert(cetta_prepared_pure_closed_step(sliced, &fixture.scratch, &fixture,
            1u, &spent, &answer) == terminal);
        assert(spent == 0u && answer == pending);
        assert(cetta_prepared_pure_closed_owned_by(sliced, optional,
            &fixture.scratch, &fixture));
        assert(cetta_prepared_pure_closed_read(sliced, &after));
        expect_same_optional_cut(before, after);
        if (pending) {
            assert(cetta_prepared_pure_closed_accept_optional(sliced, &fixture, pending));
            assert(!cetta_prepared_pure_closed_accept_optional(sliced, &fixture, pending));
        } else {
            assert(!cetta_prepared_pure_closed_accept_optional(sliced, &fixture,
                parse(&fixture.source, "counterfeit")));
        }
        assert(cetta_prepared_pure_closed_step(sliced, &fixture.scratch, &fixture,
            1u, &spent, &answer) == CETTA_PREPARED_CLOSED_EXHAUSTED);
        assert(spent == 0u && !answer);
        assert(cetta_prepared_pure_closed_close(sliced, &fixture));
        assert(!cetta_prepared_pure_closed_open_optional(optional, &fixture.scratch, &fixture));
        cetta_prepared_pure_program_free(optional);
        destroy(&fixture);
    }
}

/* A spent prefix cannot be restarted when its future becomes unavailable. */
static void test_optional_retained_refusals(void) {
    const char *equations[] = {
        "(= (walk Nil) (empty))",
        "(= (walk (Cons $x $xs)) (walk $xs))",
        "(= (bad $x) (/ (+ $x 1) (- $x $x)))",
        "(= (spin $x) (spin $x))",
    };
    const char *calls[] = {"(walk (Cons 1 Nil))", "(bad 0)", "(spin seed)"};
    for (size_t case_index = 0u; case_index < 3u; case_index++) {
        Fixture fixture;
        init_with_view(&fixture, equations, 4u, calls[case_index], zero_view);
        CettaPreparedPureProgram *optional = compile_optional(&fixture, calls[case_index], true);
        assert(optional);
        CettaPreparedPureClosedExecution *execution = cetta_prepared_pure_closed_open_optional(
            optional, &fixture.scratch, &fixture);
        assert(execution);
        uint64_t spent = 0u;
        Atom *answer = NULL;
        assert(cetta_prepared_pure_closed_step(execution, &fixture.scratch, &fixture,
            1u, &spent, &answer) == CETTA_PREPARED_CLOSED_SUSPENDED);
        assert(spent == 1u && !answer);
        CettaPreparedClosedState before, after;
        assert(cetta_prepared_pure_closed_read(execution, &before));
        if (case_index == 0u) {
            assert(space_admit_atom(&fixture.space, &fixture.source,
                parse(&fixture.source, "(= (walk Nil) later)")));
            assert(cetta_prepared_pure_closed_step(execution, &fixture.scratch, &fixture,
                100u, &spent, &answer) == CETTA_PREPARED_CLOSED_INVALIDATED);
            assert(spent == 0u && !answer);
            assert(!cetta_prepared_pure_closed_owned_by(execution, optional,
                &fixture.scratch, &fixture));
            assert(cetta_prepared_pure_closed_read(execution, &after));
            assert(after.performed == before.performed && after.frames == before.frames);
        } else if (case_index == 1u) {
            assert(cetta_prepared_pure_closed_step(execution, &fixture.scratch, &fixture,
                100u, &spent, &answer) == CETTA_PREPARED_CLOSED_DECLINED);
            assert(spent > 0u && !answer);
            assert(cetta_prepared_pure_closed_read(execution, &before));
            assert(before.decline && before.frames > 0u && before.performed > 1u);
            assert(cetta_prepared_pure_closed_step(execution, &fixture.scratch, &fixture,
                100u, &spent, &answer) == CETTA_PREPARED_CLOSED_DECLINED);
            assert(spent == 0u && !answer);
            assert(cetta_prepared_pure_closed_read(execution, &after));
            expect_same_optional_cut(before, after);
        } else {
            for (size_t slice = 0u; slice < 7u; slice++) {
                assert(cetta_prepared_pure_closed_step(execution, &fixture.scratch, &fixture,
                    3u, &spent, &answer) == CETTA_PREPARED_CLOSED_SUSPENDED);
                assert(spent == 3u && !answer);
            }
            assert(cetta_prepared_pure_closed_read(execution, &after));
            assert(after.performed == 22u && after.frames == 1u);
        }
        assert(!cetta_prepared_pure_closed_close(execution, optional));
        assert(cetta_prepared_pure_closed_close(execution, &fixture));
        cetta_prepared_pure_program_free(optional);
        destroy(&fixture);
    }
}

static CettaPreparedPureProgram *compile_scalar(Fixture *fixture, const char *call) {
    return cetta_prepared_pure_program_compile_closed(
        &fixture->space,parse(&fixture->source,call),CETTA_GSLT_PURE_CALL_EAGER,
        atom_bool,atom_expr,cetta_prepared_pure_constructs_expression_always,
        NULL,NULL,zero_view,NULL,NULL,true,true,
        (CettaMatchDecisionSemanticIdentity){
            .compiler_identity=cetta_match_decision_compiler_identity(),
        });
}

static size_t result_preparation_views;
static CettaPreparedPureExpressionViewState counted_zero_view(
    Space *space, const Atom *expression, CettaPreparedPureExpressionView *view) {
    result_preparation_views++;
    return zero_view(space, expression, view);
}

static CettaPreparedPureProgram *compile_result(Fixture *fixture,
        const char *call, bool values) {
    return cetta_prepared_pure_program_compile_closed_result(
        &fixture->space, parse(&fixture->source, call),
        CETTA_GSLT_PURE_CALL_EAGER, atom_bool, atom_expr,
        cetta_prepared_pure_constructs_expression_always,
        NULL, NULL, counted_zero_view, NULL, NULL, values, true,
        (CettaMatchDecisionSemanticIdentity){
            .compiler_identity = cetta_match_decision_compiler_identity(),
        });
}

static void test_shared_result_preparation(void) {
    const char *equations[] = {
        "(= (scalar $x) (Box $x))",
        "(= (datum) ())",
        "(= (maybe Nil) (empty))",
        "(= (maybe (Cons $x $xs)) (Box $x))",
        "(= (ambiguous $x) same)", "(= (ambiguous $x) same)",
        "(= (bad $x) (/ 1 $x))",
    };
    Fixture fixture;
    init_with_view(&fixture, equations, 7u, "(scalar 7)", zero_view);
    CettaPreparedPureProgram *program = compile_result(&fixture, "(scalar 7)", true);
    Atom *answer = NULL;
    assert(program && !cetta_prepared_pure_program_returns_optional(program));
    assert(cetta_prepared_pure_program_execute_closed(
        program, &fixture.scratch, 0u, &answer));
    assert(strcmp(atom_to_string(&fixture.source, answer), "(Box 7)") == 0);
    cetta_prepared_pure_program_free(program);
    program = compile_result(&fixture, "(datum)", true);
    assert(program && !cetta_prepared_pure_program_returns_optional(program));
    assert(cetta_prepared_pure_program_execute_closed(
        program, &fixture.scratch, 0u, &answer));
    assert(answer && answer->kind == ATOM_EXPR && answer->expr.len == 0u);
    cetta_prepared_pure_program_free(program);

    /* The old direct attempt stops at choice-zero. Shared preparation
     * interprets the source only once, just as the explicit optional path. */
    assert(!compile_scalar(&fixture, "(maybe Nil)"));
    result_preparation_views = 0u;
    program = compile_result(&fixture, "(maybe Nil)", true);
    size_t shared_views = result_preparation_views;
    assert(program && cetta_prepared_pure_program_returns_optional(program));
    assert(cetta_prepared_pure_program_execute_closed_optional(
        program, &fixture.scratch, 0u, NULL, NULL, &answer) ==
        CETTA_PREPARED_PURE_OPTIONAL_EMPTY);
    assert(!answer);
    assert(cetta_prepared_pure_program_rebind_closed_entry_call(
        program, parse(&fixture.source, "(maybe (Cons 9 Nil))")));
    assert(cetta_prepared_pure_program_execute_closed_optional(
        program, &fixture.scratch, 0u, NULL, NULL, &answer) ==
        CETTA_PREPARED_PURE_OPTIONAL_VALUE);
    assert(strcmp(atom_to_string(&fixture.source, answer), "(Box 9)") == 0);
    assert(cetta_prepared_pure_program_rebind_closed_entry_call(
        program, parse(&fixture.source, "(maybe Other)")));
    assert(cetta_prepared_pure_program_execute_closed_optional(
        program, &fixture.scratch, 0u, NULL, NULL, &answer) ==
        CETTA_PREPARED_PURE_OPTIONAL_DECLINED);
    assert(!answer);
    result_preparation_views = 0u;
    CettaPreparedPureProgram *explicit_optional =
        cetta_prepared_pure_program_compile_closed_optional(
            &fixture.space, parse(&fixture.source, "(maybe Nil)"),
            CETTA_GSLT_PURE_CALL_EAGER, atom_bool, atom_expr,
            cetta_prepared_pure_constructs_expression_always,
            NULL, NULL, counted_zero_view, NULL, NULL, true, true,
            (CettaMatchDecisionSemanticIdentity){
                .compiler_identity = cetta_match_decision_compiler_identity(),
            });
    assert(explicit_optional && shared_views > 0u &&
        result_preparation_views == shared_views);
    cetta_prepared_pure_program_free(explicit_optional);
    assert(!compile_result(&fixture, "(maybe Nil)", false));
    assert(!compile_result(&fixture, "(maybe $free)", true));
    assert(!compile_result(&fixture, "(ambiguous seed)", true));
    CettaPreparedPureProgram *fault = compile_result(&fixture, "(bad 0)", true);
    assert(fault && !cetta_prepared_pure_program_returns_optional(fault));
    assert(!cetta_prepared_pure_program_execute_closed(
        fault, &fixture.scratch, 0u, &answer) && !answer);
    cetta_prepared_pure_program_free(fault);
    space_add(&fixture.space, parse(&fixture.source, "(= (maybe Nil) forged)"));
    assert(cetta_prepared_pure_program_execute_closed_optional(
        program, &fixture.scratch, 0u, NULL, NULL, &answer) ==
        CETTA_PREPARED_PURE_OPTIONAL_INVALIDATED);
    assert(!answer && !compile_result(&fixture, "(maybe Nil)", true));
    cetta_prepared_pure_program_free(program);
    destroy(&fixture);
}

typedef struct {
    Atom *expected, *borrowed;
    unsigned attempts, accepted;
    bool refuse;
} ClosedReturnConsumer;

static bool closed_return_consume(void *raw,const CettaOwnedReturn *returned) {
    ClosedReturnConsumer *consumer=raw;
    assert(returned->outcome.kind==CETTA_CALL_VALUE &&
        returned->outcome.result_form==CETTA_CALL_RESULT_COMPLETED_VALUE &&
        !returned->outcome.delayed && !returned->environment && returned->final);
    assert(atom_eq(returned->outcome.term,consumer->expected));
    if (consumer->borrowed) assert(returned->outcome.term==consumer->borrowed);
    consumer->borrowed=returned->outcome.term;
    consumer->attempts++;
    if (consumer->refuse) return false;
    consumer->accepted++;
    return true;
}

/* The host uses one return protocol for actual scalar, optional and general
 * workers. A source edit cannot execute a paid final value a second time. */
static void test_closed_owned_return_modes(void) {
    const char *equations[]={
        "(= (scalar $n) (if (== $n 0) (Box 7) (scalar (- $n 1))))",
        "(= (optional Nil) (empty))",
        "(= (optional (Cons $x $xs)) (Box $x))",
        "(= (empty-datum) ())",
    };
    const char *calls[]={"(scalar 5)","(optional (Cons 7 Nil))","(optional Nil)","(empty-datum)"};
    for(unsigned which=0u;which<4u;which++) {
        Fixture fixture;init_with_view(&fixture,equations,4u,calls[which],zero_view);
        CettaPreparedPureProgram *program=(which==1u || which==2u)
            ? compile_optional(&fixture,calls[which],true):compile_scalar(&fixture,calls[which]);
        assert(program);
        CettaPreparedPureClosedExecution *closed=(which==1u || which==2u)
            ? cetta_prepared_pure_closed_open_optional(program,&fixture.scratch,&fixture)
            : cetta_prepared_pure_closed_open(program,&fixture.scratch,&fixture);
        assert(closed);
        CettaOwnedExecution driver={0},counterfeit={0};
        assert(!cetta_prepared_pure_closed_own_execution(&driver,closed,program,1u));
        assert(!driver.computation);
        assert(cetta_prepared_pure_closed_own_execution(&driver,closed,&fixture,1u));
        assert(!cetta_prepared_pure_closed_own_execution(&counterfeit,closed,&fixture,1u));
        assert(!counterfeit.computation);
        CettaPreparedClosedState before,after;
        assert(cetta_prepared_pure_closed_read(closed,&before) && before.performed==0u);
        ClosedReturnConsumer consumer={.expected=parse(&fixture.source,which==3u?"()":"(Box 7)"),.refuse=true};
        assert(cetta_owned_execution_sequence(&driver,0u,closed_return_consume,&consumer)
            ==CETTA_OWNED_EXECUTION_PAUSED);
        assert(cetta_prepared_pure_closed_read(closed,&after));
        expect_same_optional_cut(before,after);
        CettaOwnedExecutionStep step=CETTA_OWNED_EXECUTION_PAUSED;
        for(unsigned slice=0u;slice<1000u && !driver.has_pending && !driver.terminal;slice++) {
            before=after;
            step=cetta_owned_execution_sequence(&driver,1u,closed_return_consume,&consumer);
            assert(cetta_prepared_pure_closed_read(closed,&after));
            assert(after.performed-before.performed<=1u);
        }
        assert(driver.accepted==0u && driver.has_pending==(which!=2u));
        assert(consumer.attempts==(which==2u?0u:1u));
        assert(space_admit_atom(&fixture.space,&fixture.source,
            parse(&fixture.source,"(= (later-edit) changed)")));
        before=after;
        for(unsigned retry=0u;retry<3u;retry++) {
            step=cetta_owned_execution_sequence(&driver,1u,closed_return_consume,&consumer);
            assert(step==(which==2u?CETTA_OWNED_EXECUTION_COMPLETE:CETTA_OWNED_EXECUTION_PAUSED));
            assert(cetta_prepared_pure_closed_read(closed,&after));
            expect_same_optional_cut(before,after);
        }
        consumer.refuse=false;
        assert(cetta_owned_execution_sequence(&driver,1u,closed_return_consume,&consumer)
            ==CETTA_OWNED_EXECUTION_COMPLETE);
        assert(driver.accepted==(which==2u?0u:1u) && consumer.accepted==driver.accepted);
        assert(cetta_prepared_pure_closed_read(closed,&after));
        expect_same_optional_cut(before,after);
        unsigned attempts=consumer.attempts;
        assert(cetta_owned_execution_sequence(&driver,100u,closed_return_consume,&consumer)
            ==CETTA_OWNED_EXECUTION_COMPLETE);
        assert(consumer.attempts==attempts);
        cetta_owned_execution_cancel(&driver);cetta_owned_execution_cancel(&driver);
        cetta_prepared_pure_program_free(program);destroy(&fixture);
    }
}

static void test_closed_owned_prefix_and_stale_boundary(void) {
    const char *equations[]={
        "(= (scalar $n) (if (== $n 0) (Box 7) (scalar (- $n 1))))",
        "(= (bad $x) (/ (+ $x 1) (- $x $x)))",
        "(= (spin $x) (spin $x))",
    };
    for(unsigned which=0u;which<7u;which++) {
        const char *call=which==3u?"(bad 0)":(which==4u || which==5u)?"(spin seed)":"(scalar 5)";
        Fixture fixture;init_with_view(&fixture,equations,3u,call,zero_view);
        CettaPreparedPureProgram *program=compile_scalar(&fixture,call);assert(program);
        CettaPreparedPureClosedExecution *closed=
            cetta_prepared_pure_closed_open(program,&fixture.scratch,&fixture);
        assert(closed);
        Atom *answer=NULL;uint64_t spent=0u;
        assert(cetta_prepared_pure_closed_step(closed,&fixture.scratch,&fixture,
            (which==0u || which==6u)?1000u:1u,&spent,&answer)==
            ((which==0u || which==6u)?CETTA_PREPARED_CLOSED_ANSWER:CETTA_PREPARED_CLOSED_SUSPENDED));
        if(which==6u)assert(cetta_prepared_pure_closed_step(closed,&fixture.scratch,&fixture,
            1u,&spent,&answer)==CETTA_PREPARED_CLOSED_EXHAUSTED && !spent && !answer);
        CettaPreparedClosedState before,after;
        assert(cetta_prepared_pure_closed_read(closed,&before) && before.performed>0u);
        if(which<2u)assert(space_admit_atom(&fixture.space,&fixture.source,
            parse(&fixture.source,"(= (later-edit) changed)")));
        CettaOwnedExecution driver={0};
        bool admitted=cetta_prepared_pure_closed_own_execution(&driver,closed,&fixture,
            (which==0u || which==5u)?0u:1u);
        if(which==1u || which==6u) {
            assert(!admitted && !driver.computation);
            assert(cetta_prepared_pure_closed_read(closed,&after));expect_same_optional_cut(before,after);
            assert(cetta_prepared_pure_closed_close(closed,&fixture));
        } else {
            assert(admitted);
            ClosedReturnConsumer consumer={.expected=parse(&fixture.source,"(Box 7)"),.refuse=which==0u};
            if(which==2u)assert(space_admit_atom(&fixture.space,&fixture.source,
                parse(&fixture.source,"(= (later-edit) changed)")));
            CettaOwnedExecutionStep step=CETTA_OWNED_EXECUTION_PAUSED;
            for(unsigned slice=0u;slice<100u && !driver.terminal && !driver.has_pending;slice++)
                step=cetta_owned_execution_sequence(&driver,1u,closed_return_consume,&consumer);
            assert(cetta_prepared_pure_closed_read(closed,&after));
            if(which==0u) {
                assert(step==CETTA_OWNED_EXECUTION_PAUSED && driver.has_pending && consumer.borrowed==answer);
                expect_same_optional_cut(before,after);
                consumer.refuse=false;
                assert(cetta_owned_execution_sequence(&driver,1u,closed_return_consume,&consumer)
                    ==CETTA_OWNED_EXECUTION_COMPLETE);
                assert(driver.accepted==1u);
            } else if(which==2u) {
                assert(step==CETTA_OWNED_EXECUTION_HANDOFF &&
                    driver.completion==CETTA_EVAL_INCOMPLETE_INVALIDATED && after.performed==before.performed);
            } else if(which==3u) {
                assert(step==CETTA_OWNED_EXECUTION_HANDOFF &&
                    driver.completion==CETTA_EVAL_INCOMPLETE_HOST_FAILURE && after.frames>0u);
            } else assert(step==CETTA_OWNED_EXECUTION_PAUSED && !driver.terminal &&
                !driver.has_pending && after.performed==before.performed+(which==5u?0u:100u));
            cetta_owned_execution_cancel(&driver);
        }
        cetta_prepared_pure_program_free(program);destroy(&fixture);
    }
}

int main(void) {
    SymbolTable symbols;
    VarInternTable variables;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    var_intern_init(&variables);
    g_var_intern = &variables;
    eval_set_library_context(NULL);
    test_optional_register_worker();
    test_optional_declines_alternatives_and_faults();
    test_optional_retained_prefix_and_delivery();
    test_optional_retained_refusals();
    test_shared_result_preparation();
    test_closed_owned_return_modes();
    test_closed_owned_prefix_and_stale_boundary();
#if CETTA_MANAGED_SYMBOLS
    test_spelled_value_across_prepared_choices();
#endif
    test_completed_occurrences();
    test_late_decline_is_not_completion();
    test_visitor_stop_and_reentry();
    test_interrupt_without_answers();
    test_scratch_ownership_after_interrupted_growth();
    test_transition_limit_without_answers();
    test_allocation_limit_before_construction();
    test_constructor_cost_adapters();
    test_entry_interpretation_precedes_equations();
    test_shared_suspension_publication();
    test_closed_need_admission_and_collection();
    test_entry_value_certificate_survives_service_roles();
    test_single_result_projection();
    test_cursor_bounded_continuations();
    test_cursor_bounded_silent_loop_and_authority();
    test_cursor_quantum_does_not_recharge_limits();
    test_cursor_frontier_between_answers();
    test_cursor_last_call_replaces_caller();
    test_cursor_unyield();
    test_cursor_unmatched_call();
    test_cursor_stale_program_and_retention();
    test_cursor_detach();
    test_cursor_detach_continuations();
    test_cursor_caller_arena_not_evacuatable();
    test_cursor_last_call_chain_is_bounded();
    test_cursor_scalar_frontier_reclamation();
    test_cursor_nested_frontier_reclamation();
    test_cursor_collection_preserves_shared_frontier();
    test_cursor_collection_keeps_transition_limit();
    test_cursor_limit_keeps_frontier();
    test_continuation_permutations();
    test_continuation_detach();
    test_continuation_unmatched_call_rules();
    test_continuation_guard_and_zero();
    test_continuation_operand_order();
    test_continuation_let_mismatch();
    test_continuation_zero_operand();
    test_continuation_choice_under_determinate_head();
    test_continuation_late_decline();
    test_continuation_last_call_depth();
    test_continuation_unbounded_generator_limit();
    test_context_reconstruction_observes_interrupt();
    test_owned_return_sequencing();
    test_owned_task_account();
    test_owned_stateful_return_boundary();
    test_owned_final_delivery();
    test_cursor_final_frontier();
    test_owned_cursor_final_answer();
    test_owned_cursor_transfer_is_exclusive();
    test_owned_final_reply_pause_and_effects();
    test_owned_bounded_cursor_pending_reply();
    test_owned_bounded_cursor_silent_loop();
    test_owned_bounded_cursor_peer_service();
    test_owned_final_authority_revalidation();
    test_owned_cursor_authority();
    test_owned_shared_payload();
    symbol_table_free(&symbols);
    var_intern_free(&variables);
    g_symbols = NULL;
    g_var_intern = NULL;
    puts("prepared pure answer producer: boundary controls passed");
    return 0;
}
