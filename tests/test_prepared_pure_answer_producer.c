#include "eval.h"
#include "parser.h"
#include "petta_semantics.h"
#include "prepared_pure_machine.h"
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
            &fixture->space, entry->expr.elems[0]->sym_id, &defined);
        fprintf(stderr, "defined=%d effect=%u vars=%d arrow=%d\n",
                defined, (unsigned)effect, atom_has_vars(entry),
                space_head_has_arrow_signature(&fixture->space,
                    entry->expr.elems[0]->sym_id, entry->expr.len - 1u));
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
    const Atom *expression, CettaPreparedPureExpressionView *view) {
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
    entry_view_head = call->expr.elems[0]->sym_id;
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
    assert(cetta_prepared_pure_answer_cursor_detach(cursor));
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
    const Atom *expression, CettaPreparedPureExpressionView *view) {
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
        elements[0]->sym_id == context_out_symbol) {
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
    unsigned next, releases, accepted, attempts;
    bool reject;
} OwnedSequenceFixture;

static CettaOwnedExecutionStep owned_sequence_next(void *context,
    CettaOwnedReturn *returned, CettaEvalCompletion *completion) {
    OwnedSequenceFixture *fixture = context;
    assert(fixture->releases == 0u);
    *completion = CETTA_EVAL_COMPLETE;
    if (fixture->next == 4u)
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
    if (fixture->reject)
        return false;
    fixture->accepted++;
    return true;
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
    CettaOwnedExecution execution;
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


static bool owned_integer_accept(void *context, const CettaOwnedReturn *returned) {
    unsigned *count = context;
    assert(returned->outcome.kind == CETTA_CALL_VALUE);
    assert(returned->outcome.result_form == CETTA_CALL_RESULT_COMPLETED_VALUE);
    assert(returned->environment == NULL && returned->outcome.delayed == NULL);
    assert(returned->outcome.term->ground.ival == *count);
    (*count)++;
    return true;
}

static void test_owned_cursor_authority(void) {
    const char *equations[] = {
        "(= (owned-ticks $n) $n)",
        "(= (owned-ticks $n) (owned-ticks (+ $n 1)))",
    };
    Fixture fixture;
    init(&fixture, equations, 2u, "(owned-ticks 0)");
    CettaPreparedPureAnswerCursor *cursor = open_cursor(&fixture, false);
    CettaOwnedExecution execution;
    cetta_prepared_pure_answer_cursor_own_execution(&execution, cursor);
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
    CettaOwnedExecution execution;
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
    test_completed_occurrences();
    test_late_decline_is_not_completion();
    test_visitor_stop_and_reentry();
    test_interrupt_without_answers();
    test_scratch_ownership_after_interrupted_growth();
    test_transition_limit_without_answers();
    test_allocation_limit_before_construction();
    test_constructor_cost_adapters();
    test_entry_interpretation_precedes_equations();
    test_single_result_projection();
    test_cursor_frontier_between_answers();
    test_cursor_last_call_replaces_caller();
    test_cursor_unyield();
    test_cursor_unmatched_call();
    test_cursor_stale_program_and_retention();
    test_cursor_detach();
    test_cursor_detach_continuations();
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
    test_owned_cursor_authority();
    test_owned_shared_payload();
    symbol_table_free(&symbols);
    var_intern_free(&variables);
    g_symbols = NULL;
    g_var_intern = NULL;
    puts("prepared pure answer producer: thirty-eight boundary cases passed");
    return 0;
}
