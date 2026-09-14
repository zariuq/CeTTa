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
    const char *expected[4];
} Fixture;

static Atom *parse(Arena *arena, const char *text) {
    size_t position = 0u;
    Atom *atom = parse_sexpr(arena, text, &position);
    assert(atom);
    return atom;
}

static void init(Fixture *fixture, const char *const *equations,
                 size_t count, const char *call) {
    memset(fixture, 0, sizeof(*fixture));
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
        NULL, NULL, NULL, NULL, NULL, true, true,
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
            fixture->program, &fixture->scratch, &fixture->limits, visit, fixture,
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
static SymbolId entry_view_head;

static CettaPreparedPureExpressionViewState entry_view(
    const Atom *expression, CettaPreparedPureExpressionView *view) {
    if (expression->kind != ATOM_EXPR || expression->expr.len != 2u ||
        !atom_is_symbol_id(expression->expr.elems[0], entry_view_head))
        return CETTA_PREPARED_PURE_EXPRESSION_DEFAULT;
    view->projected = expression->expr.elems[1];
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
    };
    for (size_t i = 0u; i < sizeof(refusals) / sizeof(refusals[0]); i++) {
        entry_interpretation = refusals[i];
        CettaPreparedPureProgram *program =
            cetta_prepared_pure_program_compile_closed_answers(
                &fixture.space, call, CETTA_GSLT_PURE_CALL_EAGER,
                atom_bool, atom_expr, NULL, NULL, entry_view,
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
                    atom_bool, atom_expr, NULL, NULL, entry_view,
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
    destroy(&fixture);
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
    symbol_table_free(&symbols);
    var_intern_free(&variables);
    g_symbols = NULL;
    g_var_intern = NULL;
    puts("prepared pure answer producer: nine boundary cases passed");
    return 0;
}
