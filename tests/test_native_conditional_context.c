/* Inspect structured results of real evaluator calls. Printed atoms cannot
 * reveal a dropped pending check. This unit replaces eval.o at link time. */
#include "../src/eval.c"
#include "parser.h"
#include "petta_contract_core.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>

static unsigned invocation_syntax_observations;
Atom *__real_cetta_invocation_materialize(Arena *, const CettaInvocation *);
Atom *__wrap_cetta_invocation_materialize(Arena *arena, const CettaInvocation *call) {
    invocation_syntax_observations++;
    return __real_cetta_invocation_materialize(arena, call);
}

static SymbolId conditional_head = SYMBOL_ID_NONE;
static unsigned conditional_calls;
static bool opaque_check;
static unsigned conditional_shape;
static bool residual_fixture;
static unsigned residual_native_calls;

static bool prepared_fixture;
static SymbolId prepared_head = SYMBOL_ID_NONE;
static unsigned prepared_mode, prepared_attempts, prepared_calls, prepared_effects;
static Atom *prepared_borrowed_value;
static uint32_t prepared_borrowed_parent_depth;

static Atom *prepared_fixture_atom(Arena *arena, const char *text) {
    size_t position = 0u;
    return parse_sexpr(arena, text, &position);
}

static void prepared_fixture_result(Arena *arena, CettaCallOutcome *outcome) {
    prepared_calls++;
    Atom *value = atom_int(arena, 7);
    switch (prepared_mode) {
    case 0u: case 9u: case 11u: *outcome = cetta_call_completed_value(value); break;
    case 1u:
        *outcome = cetta_call_completed_value(prepared_fixture_atom(arena,
            "(function (return (readln!)))")); break;
    case 2u:
        *outcome = cetta_call_completed_frontier(atom_expr3(arena, value, value,
            atom_int(arena, 9))); break;
    case 3u: *outcome = cetta_call_failure(); break;
    case 4u: *outcome = cetta_call_raised(atom_error(arena, value, atom_symbol(arena, "fixture"))); break;
    case 5u: case 13u: *outcome = cetta_call_expression_frontier(
        atom_expr(arena, (Atom *[]){prepared_fixture_atom(arena, "(readln!)")}, 1u)); break;
    case 6u: *outcome = cetta_call_completed_value(atom_error(arena, value,
        atom_symbol(arena, "datum"))); break;
    case 7u: *outcome = cetta_call_completed_value(atom_symbol(arena, "Empty")); break;
    case 8u: *outcome = cetta_call_interrupted(); break;
    case 10u: *outcome = cetta_call_completed_value(NULL); break;
    case 12u: case 16u: {
        Atom *prototype = atom_expr2(arena, value, value);
        Atom *frontier = arena_alloc(arena, sizeof(*frontier));
        *frontier = *prototype;
        frontier->expr.elems = arena_alloc(arena, 2u * sizeof(Atom *));
        frontier->expr.elems[0] = value;
        frontier->expr.elems[1] = NULL;
        *outcome = prepared_mode == 16u ? cetta_call_expression_frontier(frontier)
                                      : cetta_call_completed_frontier(frontier);
        break;
    }
    case 14u: case 17u: {
        Atom *code = prepared_fixture_atom(arena, "(+ (readln!) 1)");
        Atom *frontier = atom_expr2(arena, code, code);
        *outcome = prepared_mode == 14u ? cetta_call_expression_frontier(frontier)
                                      : cetta_call_completed_frontier(frontier);
        break;
    }
    case 15u: *outcome = cetta_call_expression_frontier(atom_unit(arena)); break;
    case 18u: *outcome = cetta_call_value(prepared_fixture_atom(arena, "(readln!)")); break;
    case 19u:
        *outcome = cetta_call_expression_frontier(atom_expr2(arena,
            atom_error(arena, atom_symbol(arena, "native"), atom_symbol(arena, "datum")), value));
        break;
    case 20u: {
        Atom *code = prepared_fixture_atom(arena, "(+ (readln!) 1)");
        *outcome = cetta_call_expression_frontier(atom_expr3(arena, code, code, code));
        break;
    }
    case 21u: *outcome = cetta_call_completed_value(prepared_borrowed_value); break;
    case 22u: *outcome = cetta_call_completed_frontier(
        atom_expr2(arena, prepared_borrowed_value, prepared_borrowed_value)); break;
    default: abort();
    }
}

bool __real_cetta_library_call_native_prepared_view(CettaLibraryContext *, Space *,
    Arena *, Atom *, Atom **, uint32_t, CettaCallOutcome *);
bool __wrap_cetta_library_call_native_prepared_view(CettaLibraryContext *library, Space *space,
    Arena *arena, Atom *head, Atom **arguments, uint32_t count, CettaCallOutcome *outcome) {
    if (!prepared_fixture || !atom_is_symbol_id(head, prepared_head))
        return __real_cetta_library_call_native_prepared_view(library, space, arena,
            head, arguments, count, outcome);
    prepared_attempts++;
    if (prepared_mode == 11u)
        return false;
    if (prepared_mode == 21u || prepared_mode == 22u) {
        assert(g_eval_gc.external_owner_depth > prepared_borrowed_parent_depth);
        EvalOutcome nested;
        eval_outcome_init(&nested);
        metta_eval_outcome(space, arena, NULL,
            prepared_fixture_atom(arena, "(+ 1 2)"), -1, &nested);
        assert(nested.completion == CETTA_EVAL_COMPLETE && nested.results.len == 1u &&
            atom_eq(nested.results.items[0], atom_int(arena, 3)));
        eval_outcome_free(&nested);
        assert(g_eval_gc.external_owner_depth > prepared_borrowed_parent_depth);
    }
    prepared_fixture_result(arena, outcome);
    return true;
}

bool __real_cetta_library_call_native(CettaLibraryContext *, Space *, Arena *, Atom *,
    Atom **, uint32_t, CettaCallOutcome *);
bool __wrap_cetta_library_call_native(CettaLibraryContext *library, Space *space,
    Arena *arena, Atom *head, Atom **arguments, uint32_t count, CettaCallOutcome *outcome) {
    if (!prepared_fixture || !atom_is_symbol_id(head, prepared_head))
        return __real_cetta_library_call_native(library, space, arena, head, arguments, count, outcome);
    prepared_fixture_result(arena, outcome);
    return true;
}

bool __real_grounded_call(Arena *arena, Atom *head, Atom **arguments,
                         uint32_t count, CettaCallOutcome *outcome);
bool __wrap_grounded_call(Arena *arena, Atom *head, Atom **arguments,
                         uint32_t count, CettaCallOutcome *outcome) {
    if (prepared_fixture && atom_is_symbol_id(head, g_builtin_syms.readln_bang)) {
        prepared_effects++;
        if (prepared_mode == 20u && prepared_effects == 2u) {
            *outcome = cetta_call_interrupted();
            return true;
        }
        if (prepared_mode == 13u)
            assert(cetta_library_petta_translator_rule_set(g_library_context, prepared_head, true));
        *outcome = cetta_call_completed_value(atom_int(arena, 5));
        return true;
    }
    if (residual_fixture && atom_is_symbol_id(head, g_builtin_syms.readln_bang)) {
        residual_native_calls++;
        *outcome = cetta_call_completed_value(atom_int(arena, 1));
        return true;
    }
    if (!atom_is_symbol_id(head, conditional_head))
        return __real_grounded_call(arena, head, arguments, count, outcome);
    conditional_calls++;
    Atom *value = atom_int(arena, 9);
    if (conditional_shape) {
        Atom *row = atom_expr2(arena, atom_symbol_id(arena, g_builtin_syms.quote), value);
        Atom *rows = conditional_shape == 1u ? atom_expr2(arena, row, row) : atom_unit(arena);
        value = atom_expr2(arena, atom_symbol_id(arena, g_builtin_syms.quote), rows);
    }
    *outcome = cetta_call_suspended(value, opaque_check
        ? atom_symbol(arena, "opaque-client-check")
        : atom_expr2(arena, atom_unit(arena), atom_unit(arena)));
    return true;
}

static Atom *fixture_atom(Arena *arena, const char *text) {
    size_t position = 0u;
    Atom *atom = parse_sexpr(arena, text, &position);
    assert(atom);
    return atom;
}

static unsigned evaluator_consumers(void) {
    unsigned checks = 0u;
    for (unsigned language = 0u; language < 2u; language++) {
        CettaLibraryContext *context = calloc(1u, sizeof(*context));
        assert(context);
        cetta_library_context_init_for_language_profile(context,
            language ? CETTA_LANGUAGE_HE : CETTA_LANGUAGE_PETTA, NULL);
        eval_set_library_context(context);
        Arena arena;
        arena_init(&arena);
        TermUniverse universe;
        term_universe_init(&universe);
        term_universe_set_persistent_arena(&universe, &arena);
        Space space;
        space_init_with_universe(&space, &universe);
        Registry registry;
        registry_init(&registry);
        registry_bind_id(&registry, g_builtin_syms.self, atom_space(&arena, &space));
        g_registry = &registry;
        SymbolId heads[] = {g_builtin_syms.lib_mork_space_atoms,
            g_builtin_syms.lib_mork_space_match,
            g_builtin_syms.lib_mork_space_add_atoms, g_builtin_syms.lib_list_range};
        const char *sources[] = {"(mork:get-atoms opaque)",
            "(mork:match opaque pattern 9)",
            "(__cetta_lib_mork_space_add_atoms opaque ())",
            "(__cetta_lib_list_range (readln!) 1 2)"};
        for (unsigned client = 0u; client < 4u; client++) {
            /* HE rejects this arity before demanding operands. PeTTa's
             * eager/partial-application policy is a different observer. */
            if (client == 3u && !language)
                continue;
            conditional_head = heads[client];
            for (unsigned shape = 0u; shape < (client < 2u ? 3u : 1u); shape++)
            for (unsigned opaque = 0u; opaque < 2u; opaque++) {
                conditional_shape = shape;
                opaque_check = opaque;
                conditional_calls = 0u;
                OutcomeSet outcomes;
                outcome_set_init_with_owner(&outcomes, &arena);
                /* An ordinary nested PeTTa host call retains a pending
                 * check for its caller. The relational machine instead
                 * interprets/registers the check under its own protocol. */
                g_petta_machine_host_depth++;
                metta_call(&space, &arena, fixture_atom(&arena, sources[client]),
                           NULL, -1, false, &outcomes);
                g_petta_machine_host_depth--;
                CettaCount count = shape == 1u ? 2u : shape == 2u ? 0u : 1u;
                assert(conditional_calls == 1u && outcomes.len == count);
                Atom *expected = opaque_check
                    ? atom_symbol(&arena, "opaque-client-check")
                    : atom_expr2(&arena, atom_unit(&arena), atom_unit(&arena));
                for (CettaCount index = 0u; index < count; index++) {
                    assert(atom_eq(outcomes.items[index].atom, atom_int(&arena, 9)));
                    assert(outcomes.items[index].delayed &&
                           atom_eq(outcomes.items[index].delayed, expected));
                }
                outcome_set_free(&outcomes);
                checks++;
            }
        }
        conditional_head = SYMBOL_ID_NONE;
        g_registry = NULL;
        eval_release_temporary_spaces();
        registry_free(&registry);
        space_free(&space);
        term_universe_free(&universe);
        eval_set_library_context(NULL);
        cetta_library_context_free(context);
        free(context);
        arena_free(&arena);
    }
    return checks;
}

static unsigned projected_roots(void) {
    unsigned checks = 0u;
    for (unsigned preserve = 0u; preserve < 2u; preserve++)
    for (unsigned factored = 0u; factored < 2u; factored++) {
        Arena source, destination;
        arena_init_detached(&source);
        arena_init_detached(&destination);
        Outcome original;
        outcome_init(&original);
        original.atom = fixture_atom(&source, "(Pair $result $free)");
        Atom *result = original.atom->expr.elems[1];
        Atom *free_variable = original.atom->expr.elems[2];
        Atom *alias = atom_var(&source, "alias");
        Atom *pending = atom_var(&source, "pending");
        Atom *unreachable = atom_var(&source, "unreachable");
        original.delayed = atom_expr3(&source, atom_symbol(&source, "Check"),
                                      pending, free_variable);
        assert(bindings_add_var(&original.env, result, alias));
        assert(bindings_add_var(&original.env, pending, atom_int(&source, 11)));
        assert(bindings_add_var(&original.env, unreachable, atom_int(&source, 99)));
        /* Warm the value preview before introducing an outer binding. */
        assert(outcome_atom_materialize(&source, &original));
        VariantBank bank;
        variant_bank_init(&bank, (CettaVariantShapeOptions){
            .slot_policy = CETTA_VARIANT_SLOT_FIXED_SPELLING,
            .slot_name = "$_slot", .share_immutable = true});
        if (factored) {
            VariantShape shape;
            assert(variant_shape_from_bound_atom(&bank, &original.env,
                                                  original.atom, &shape));
            assert(variant_instance_from_shape(&original.variant, &shape));
            original.atom = shape.skeleton;
            original.materialized_atom = NULL;
            variant_shape_free(&shape);
        }
        Bindings prefix;
        bindings_init(&prefix);
        assert(bindings_add_var(&prefix, alias, atom_int(&source, 7)));
        OutcomeSet projected;
        outcome_set_init_with_owner(&projected, &destination);
        outcome_set_add_prefixed_outcome(&destination, &projected, &original,
                                         &prefix, preserve);
        assert(projected.len == 1u && projected.items[0].delayed);
        Outcome *answer = &projected.items[0];
        Atom *value = outcome_atom_materialize(&destination, answer);
        Atom *check = bindings_apply_if_vars(&answer->env, &destination, answer->delayed);
        Atom *free_witness = atom_var_with_id(&destination, "free", free_variable->var_id);
        Atom *expected_value = atom_expr3(&destination,
            atom_symbol(&destination, "Pair"), atom_int(&destination, 7), free_witness);
        Atom *expected_check = atom_expr3(&destination,
            atom_symbol(&destination, "Check"), atom_int(&destination, 11), free_witness);
        if (!atom_eq(value, expected_value))
            fprintf(stderr, "projection preserve=%u factored=%u value=%s\n",
                preserve, factored, atom_to_string(&destination, value));
        assert(atom_eq(value, expected_value));
        assert(atom_eq(check, expected_check));
        assert(var_id_eq(value->expr.elems[2]->var_id, check->expr.elems[2]->var_id));
        if (!preserve)
            assert(!bindings_lookup_value_id(&answer->env, unreachable->var_id).skeleton);
        assert(atom_graph_is_closed_for_arena(&destination, answer->atom));
        assert(atom_graph_is_closed_for_arena(&destination, answer->delayed));
        bindings_free(&prefix);
        outcome_free_fields(&original);
        variant_bank_free(&bank);
        arena_free(&source);
        /* Publication owns the surviving graph after the source retires. */
        assert(atom_eq(value, expected_value));
        assert(atom_eq(check, expected_check));
        outcome_set_free(&projected);
        arena_free(&destination);
        checks++;
    }
    return checks;
}

static void native_frontier_capture_control(Space *space, Arena *recipient) {
    Arena source;
    arena_init_detached(&source);
    Bindings environment;
    bindings_init(&environment);
    Atom *variable = atom_var(&source, "captured");
    Atom *alias = atom_var(&source, "alias");
    assert(bindings_add_var(&environment, variable, alias));
    assert(bindings_add_var(&environment, alias, atom_int(&source, 7)));
    Atom *sum = atom_expr3(&source, atom_symbol(&source, "+"), variable, atom_int(&source, 1));
    Atom *expressions = atom_expr3(&source, variable, sum, variable);
    OutcomeSet values;
    outcome_set_init(&values);
    CettaEvalCompletion completion = CETTA_EVAL_COMPLETE;
    CettaCallOutcome end = complete_native_expression_frontier(space, recipient,
        cetta_call_expression_frontier(expressions), NULL, &environment, -1,
        &values, &completion);
    assert(end.kind == CETTA_CALL_FAILURE && completion == CETTA_EVAL_COMPLETE);
    assert(values.payload_owner == recipient);
    assert(values.len == 3u);
    bindings_free(&environment);
    arena_free(&source);
    for (CettaCount index = 0u; index < values.len; index++) {
        Atom *value = outcome_atom_materialize(recipient, &values.items[index]);
        assert(atom_eq(value, atom_int(recipient, index == 1u ? 8 : 7)));
        assert(atom_graph_is_closed_for_arena(recipient, value));
    }
    outcome_set_free(&values);
}

typedef struct {
    Arena *arena;
    const char *const *expected;
    unsigned count, attempts;
    unsigned language, case_index;
    bool refuse;
} ResidualRecipient;

static bool residual_publish(void *context, const CettaOwnedReturn *returned) {
    ResidualRecipient *recipient = context;
    recipient->attempts++;
    if (recipient->refuse)
        return false;
    assert(recipient->count < 4u);
    assert(returned->outcome.kind == CETTA_CALL_VALUE);
    if (!recipient->expected[recipient->count])
        fprintf(stderr, "unexpected residual value: language=%u case=%u value=%s\n",
            recipient->language, recipient->case_index,
            atom_to_string(recipient->arena, returned->outcome.term));
    assert(recipient->expected[recipient->count]);
    assert(atom_eq(returned->outcome.term,
                   fixture_atom(recipient->arena, recipient->expected[recipient->count])));
    recipient->count++;
    return true;
}

static unsigned residual_consumers(void) {
    unsigned checks = 0u;
    for (unsigned language = 0u; language < 2u; language++) {
        CettaLibraryContext *context = calloc(1u, sizeof(*context));
        assert(context);
        cetta_library_context_init_for_language_profile(context,
            language ? CETTA_LANGUAGE_HE : CETTA_LANGUAGE_PETTA, NULL);
        eval_set_library_context(context);
        Arena arena;
        arena_init(&arena);
        TermUniverse universe;
        term_universe_init(&universe);
        term_universe_set_persistent_arena(&universe, &arena);
        Space space;
        space_init_with_universe(&space, &universe);
        Registry registry;
        registry_init(&registry);
        registry_bind_id(&registry, g_builtin_syms.self, atom_space(&arena, &space));
        g_registry = &registry;
        struct {
            const char *source, *continuation;
            const char *expected[4];
            unsigned count, calls;
            bool refuse;
        } cases[] = {
            {"(superpose (1 1 2))", "(Test $slot (pair $slot $slot))",
             {"(pair 1 1)", "(pair 1 1)", "(pair 2 2)"}, 3u, 0u, false},
            {"(superpose ())", "(Test $slot (readln!))", {NULL}, 0u, 0u, false},
            {"(superpose (1 2))", "(Test $slot (let $effect (readln!) (superpose ())))",
             {NULL}, 0u, 2u, false},
            {"(let $effect (readln!) (superpose (1 2)))",
             "(Test $slot (let $effect (readln!) (+ $slot 10)))",
             {"11", "12"}, 2u, 3u, false},
            {"(let $effect (readln!) (superpose (1 2)))",
             "(Test $slot (let $effect (readln!) (+ $slot 10)))",
             {NULL}, 0u, 2u, true},
            {"(superpose (1 2))", "(Test $slot (superpose ($slot $slot)))",
             {"1", "1", "2", "2"}, 4u, 0u, false},
        };
        residual_fixture = true;
        for (unsigned i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
            Atom *wrapper = fixture_atom(&arena, cases[i].continuation);
            ResidualRecipient recipient = {.arena = &arena,
                .expected = cases[i].expected, .refuse = cases[i].refuse,
                .language = language, .case_index = i};
            EvalOwnedAnswers producer = {.space = &space};
            EvalOwnedResidual residual = {.answers = &producer, .arena = &arena,
                .resumption = wrapper->expr.elems[2], .hole = wrapper->expr.elems[1],
                .consume = residual_publish, .consumer = &recipient, .ok = true};
            Bindings empty;
            bindings_init(&empty);
            residual_native_calls = 0u;
            bool complete = eval_owned_residual_body(
                fixture_atom(&arena, cases[i].source), &empty, &residual);
            assert(complete == !cases[i].refuse && residual.ok == complete);
            assert(recipient.count == cases[i].count && residual_native_calls == cases[i].calls);
            if (cases[i].refuse)
                assert(recipient.attempts == 1u);
            bindings_free(&empty);
            checks++;
        }
        residual_fixture = false;
        g_registry = NULL;
        eval_release_temporary_spaces();
        registry_free(&registry);
        space_free(&space);
        term_universe_free(&universe);
        eval_set_library_context(NULL);
        cetta_library_context_free(context);
        free(context);
        arena_free(&arena);
    }
    return checks;
}

static unsigned prepared_machine_pulls;
static Arena *prepared_machine_parent;
static bool prepared_machine_collect;
static CettaPreparedPureAnswerCursor *prepared_machine_observe_heap(void *context,
        Space *space, Arena *arena, Atom *call,
        CettaPreparedPureHeadAdmissionFn head_admission, void *admission_context,
        bool (*current)(const void *)) {
    prepared_machine_parent = arena;
    return petta_eval_machine_open_answer_cursor(context, space, arena, call,
        head_admission, admission_context, current);
}

static CettaOwnedExecutionStep counted_prepared_machine_return(void *context,
        Space *space, CettaPreparedPureAnswerCursor *cursor,
        CettaOwnedReturn *returned, CettaEvalCompletion *completion) {
    prepared_machine_pulls++;
    CettaOwnedExecutionStep step = petta_eval_machine_next_answer_cursor(
        context, space, cursor, returned, completion);
    if (prepared_machine_collect && step == CETTA_OWNED_EXECUTION_RETURN) {
        assert(prepared_machine_parent);
        for (unsigned i = 0u; i < 2048u; i++)
            assert(atom_int(prepared_machine_parent, i));
    }
    return step;
}

static unsigned prepared_machine_consumers(CettaLibraryContext *library,
        Space *space, Arena *arena) {
    unsigned checks = 0u;
    for (unsigned mode = 0u; mode < 21u; mode++)
    for (unsigned tail = 0u; tail < 2u; tail++)
    for (unsigned ordinary = 0u;
         ordinary < (mode == 8u || mode == 10u || mode == 14u || mode == 15u ||
                     mode == 16u || mode == 17u || mode == 18u || mode == 19u ||
                     mode == 20u ? 2u : 1u);
         ordinary++) {
        assert(cetta_library_petta_translator_rule_set(library, prepared_head, false));
        prepared_mode = mode;
        prepared_calls = prepared_attempts = prepared_effects = prepared_machine_pulls = 0u;
        PettaEvalMachineContext context = {.fuel = -1, .library_context = library,
            .semantic_authority_epoch = 1u, .prepared_pure_cache = {.root_space = space}};
        PettaMachineHost host = petta_eval_machine_host(&context, -1,
            false, false, false, false, false,
            petta_eval_machine_match_semantics(library, false));
        host.next_answer_cursor = counted_prepared_machine_return;
        if (ordinary) {
            host.execute_prepared_pure_call = NULL;
            host.open_answer_cursor = NULL;
            host.open_relation_cursor = NULL;
        }
        PettaMachine machine;
        Atom *call = fixture_atom(arena, mode == 9u ? "(native-fault 1)"
            : tail ? "(native-tail 1)" : "(native-box 1)");
        assert(petta_machine_init(&machine, space, arena, call, NULL, &host));
        unsigned count = 0u;
        bool raised = false;
        PettaMachineStep stop;
        for (;;) {
            Atom *answer = NULL;
            Bindings environment;
            stop = petta_machine_next(&machine, &answer, &environment);
            if (stop != PETTA_MACHINE_STEP_ANSWER) {
                bindings_free(&environment);
                break;
            }
            assert(++count <= 3u);
            if (mode == 14u)
                assert(prepared_effects == 2u);
            raised |= petta_machine_last_answer_raised(&machine);
            Atom *value = bindings_apply_if_vars(&environment, arena, answer);
            if (mode == 4u || mode == 9u)
                assert(atom_is_error(value) && raised);
            else if (tail) {
                assert(!raised && value);
                if (mode == 1u)
                    assert(atom_eq(value, fixture_atom(arena, "(function (return (readln!)))")));
                else if (mode == 6u)
                    assert(atom_is_error(value));
                else if (mode == 7u)
                    assert(atom_is_symbol(value, "Empty"));
                else if (mode == 17u)
                    assert(atom_eq(value, fixture_atom(arena, "(+ (readln!) 1)")));
                else if (mode == 18u)
                    assert(atom_eq(value, fixture_atom(arena, "(readln!)")));
                else if (mode == 19u && count == 1u)
                    assert(atom_eq(value, fixture_atom(arena, "(Error native datum)")));
                else
                    assert(atom_eq(value, atom_int(arena, mode == 5u ? 5
                        : mode == 14u ? 6
                        : mode == 2u && count == 3u ? 9 : 7)));
            } else {
                assert(!raised && value && value->kind == ATOM_EXPR && value->expr.len == 3u);
                assert(atom_eq(value->expr.elems[1], value->expr.elems[2]));
                if (mode == 1u)
                    assert(atom_eq(value->expr.elems[1], fixture_atom(arena,
                        "(function (return (readln!)))")));
                if (mode == 14u)
                    assert(atom_eq(value->expr.elems[1], atom_int(arena, 6)));
                if (mode == 17u)
                    assert(atom_eq(value->expr.elems[1], fixture_atom(arena, "(+ (readln!) 1)")));
                if (mode == 18u)
                    assert(atom_eq(value->expr.elems[1], fixture_atom(arena, "(readln!)")));
                if (mode == 19u)
                    assert(atom_eq(value->expr.elems[1], count == 1u
                        ? fixture_atom(arena, "(Error native datum)") : atom_int(arena, 7)));
            }
            bindings_free(&environment);
        }
        if ((!ordinary && !prepared_machine_pulls) || prepared_calls != 1u ||
            prepared_attempts != (ordinary ? 0u : 1u))
            fprintf(stderr, "PSM prepared worker: mode=%u pulls=%u calls=%u attempts=%u count=%u stop=%u\n",
                mode, prepared_machine_pulls, prepared_calls, prepared_attempts, count, stop);
        assert((ordinary ? !prepared_machine_pulls : !!prepared_machine_pulls) &&
            prepared_calls == 1u && prepared_attempts == (ordinary ? 0u : 1u));
        if (mode == 8u || mode == 10u || mode == 12u || mode == 13u || mode == 16u || mode == 20u) {
            assert(count == (mode == 12u ? 1u : 0u));
            PettaMachineStep expected_stop = mode == 8u || mode == 20u ? PETTA_MACHINE_STEP_SUSPENDED
                : mode == 13u ? PETTA_MACHINE_STEP_INVALIDATED
                : ordinary ? PETTA_MACHINE_STEP_SUSPENDED
                : PETTA_MACHINE_STEP_HOST_ERROR;
            if (stop != expected_stop)
                fprintf(stderr, "native stop mode=%u ordinary=%u tail=%u stop=%u expected=%u\n",
                    mode, ordinary, tail, stop, expected_stop);
            assert(stop == expected_stop);
        } else {
            assert(stop == PETTA_MACHINE_STEP_EXHAUSTED);
            assert(count == (mode == 2u ? 3u : mode == 3u || mode == 15u ? 0u
                : mode == 14u || mode == 17u || mode == 19u ? 2u : 1u));
        }
        assert(prepared_effects == (mode == 14u || mode == 20u ? 2u
            : mode == 5u || mode == 13u ? 1u : 0u));
        /* Entered interruption is a terminal native boundary. Asking again
         * cannot re-enter the operation or repeat a completed effect. */
        Atom *later = NULL;
        Bindings later_environment;
        assert(petta_machine_next(&machine, &later, &later_environment) == stop);
        bindings_free(&later_environment);
        assert(prepared_calls == 1u);
        petta_machine_destroy(&machine);
        prepared_pure_program_cache_free(&context.prepared_pure_cache);
        assert(!petta_eval_take_raise());
        checks++;
    }
    assert(cetta_library_petta_translator_rule_set(library, prepared_head, false));
    prepared_mode = 2u;
    prepared_calls = prepared_attempts = prepared_effects = prepared_machine_pulls = 0u;
    PettaEvalMachineContext context = {.fuel = -1, .library_context = library,
        .semantic_authority_epoch = 1u, .prepared_pure_cache = {.root_space = space}};
    PettaMachineHost host = petta_eval_machine_host(&context, -1,
        false, false, false, false, false,
        petta_eval_machine_match_semantics(library, false));
    host.next_answer_cursor = counted_prepared_machine_return;
    host.open_answer_cursor = prepared_machine_observe_heap;
    prepared_machine_collect = true;
    prepared_machine_parent = NULL;
    PettaMachine machine;
    Atom *call = fixture_atom(arena, "(native-box 1)");
    assert(petta_machine_init(&machine, space, arena, call, NULL, &host));
    for (unsigned row = 0u; row < 4u; row++) {
        Atom *answer = NULL;
        Bindings environment;
        PettaMachineStep step = petta_machine_next(&machine, &answer, &environment);
        assert(step == (row == 3u ? PETTA_MACHINE_STEP_EXHAUSTED : PETTA_MACHINE_STEP_ANSWER));
        bindings_free(&environment);
    }
    const char *stress = getenv("CETTA_PETTA_COLLECT_STRESS");
    if (stress && stress[0] && strcmp(stress, "0") != 0) {
        PettaMachineStats stats;
        assert(petta_machine_stats(&machine, &stats) &&
            (stats.deterministic_heap_collections || stats.choice_nursery_evacuations));
    }
    assert(prepared_calls == 1u && prepared_attempts == 1u);
    petta_machine_destroy(&machine);
    prepared_machine_collect = false;
    checks++;
    /* Two readers reuse one cached callee program; cancelling one leaves
     * the other's exact remaining duplicate occurrences and native count. */
    PettaMachine first, second;
    assert(petta_machine_init(&first, space, arena, call, NULL, &host));
    Atom *answer = NULL;
    Bindings environment;
    assert(petta_machine_next(&first, &answer, &environment) == PETTA_MACHINE_STEP_ANSWER);
    bindings_free(&environment);
    assert(petta_machine_init(&second, space, arena, call, NULL, &host));
    assert(petta_machine_next(&second, &answer, &environment) == PETTA_MACHINE_STEP_ANSWER);
    bindings_free(&environment);
    assert(prepared_calls == 3u && prepared_attempts == 3u);
    petta_machine_destroy(&second);
    for (unsigned row = 0u; row < 3u; row++) {
        PettaMachineStep step = petta_machine_next(&first, &answer, &environment);
        assert(step == (row == 2u ? PETTA_MACHINE_STEP_EXHAUSTED : PETTA_MACHINE_STEP_ANSWER));
        bindings_free(&environment);
    }
    assert(prepared_calls == 3u && prepared_attempts == 3u);
    petta_machine_destroy(&first);
    prepared_pure_program_cache_free(&context.prepared_pure_cache);
    checks++;
    return checks;
}

static unsigned failure_contract_controls(CettaLibraryContext *context,
        Space *space, Arena *arena) {
    const char *forms[] = {
        "(: identity-or-zero (-> $a Bool $a))",
        "(= (identity-or-zero $x $condition) (if $condition $x (empty)))",
        "(: identity-or-empty-data (-> $a Bool $a))",
        "(= (identity-or-empty-data $x $condition) (if $condition $x Empty))",
        "(: only-zero (-> $a $a))",
        "(= (only-zero $x) (empty))",
    };
    for (size_t index = 0u; index < sizeof(forms) / sizeof(*forms); index++) {
        Atom *form = fixture_atom(arena, forms[index]);
        const PettaPlanNode *plan = petta_program_plan_dynamic_add(context->petta_program, form);
        assert(plan);
        CettaCount position = space_length64(space);
        space_add(space, form);
        assert(petta_program_note_add(context->petta_program, space,
            space_get_at64(space, position), plan));
    }
    const char *heads[] = {"identity-or-zero", "identity-or-empty-data", "only-zero"};
    for (unsigned index = 0u; index < 3u; index++) {
        PettaContractVersion version;
        bool admitted = petta_contract_trusted_version(context->petta_program, space,
            symbol_intern_cstr(g_symbols, heads[index]), index == 2u ? 1u : 2u, &version);
        if (admitted != (index != 1u))
            fprintf(stderr, "failure contract head=%s admitted=%u\n", heads[index], admitted);
        assert(admitted == (index != 1u));
    }
    return 3u;
}

static unsigned result_policy_admissions(CettaLibraryContext *context,
        Space *space, Arena *arena, CettaLanguageId language) {
    const char *forms[] = {
        "(= (result-fresh $n) (let $v (+ $n 1) $v))",
        "(= (result-tree $n) (let (Pair $a $b) (Pair $n 7) (Pair $b $a)))",
        "(= (result-finite $n) (let 2 (+ 1 1) $n))",
        "(= (result-loop $n) (Right (result-loop (+ $n 1))))",
        "(= (result-demand $n) (let (Left $x) (result-loop $n) $x))",
        "(= (result-observation $x) (let (Pair False 7) (Pair (is-var $x) $x) accepted))",
    };
    for (size_t index = 0u; index < sizeof(forms) / sizeof(*forms); index++) {
        Atom *form = fixture_atom(arena, forms[index]);
        if (language == CETTA_LANGUAGE_PETTA) {
            const PettaPlanNode *source = petta_program_plan_dynamic_add(context->petta_program, form);
            CettaCount before = space_length64(space);
            assert(source);
            space_add(space, form);
            assert(petta_program_note_add(context->petta_program, space,
                space_get_at64(space, before), source));
        } else
            assert(space_admit_atom(space, arena, form));
    }
    const char *queries[] = {"(result-fresh 7)", "(result-tree 7)",
        "(result-finite 7)", "(result-demand 0)", "(result-observation 7)"};
    for (unsigned index = 0u; index < 5u; index++) {
        Atom *call = fixture_atom(arena, queries[index]);
        PreparedPureClosedCallPlan plan;
        assert(prepared_pure_closed_call_plan(space, arena, call, -1, true, NULL, &plan));
        assert(plan.source_view.result_policy == cetta_language_call_result_policy(language));
        CettaPreparedPureProgram *program = cetta_prepared_pure_program_compile_closed_answers(
            space, call, plan.call_mode, plan.boolean_value, plan.construct_value,
            plan.constructs_expression, plan.opaque_value, plan.register_view,
            plan.expression_view, plan.pattern_view, &plan.source_view, true,
            plan.total_structural_equality, prepared_pure_match_decision_semantics(plan.call_mode));
        bool admitted = language != CETTA_LANGUAGE_PETTA || index < 3u;
        if (!!program != admitted)
            fprintf(stderr, "result policy language=%u call=%s compiled=%u expected=%u\n",
                language, queries[index], !!program, admitted);
        assert(!!program == admitted);
        cetta_prepared_pure_program_free(program);
    }
    return 5u;
}

/* Exercise the ordinary entry adapter with a large caller-owned value.
 * A completed native result borrows it; a suspended native frontier owns it
 * before the caller reuses its storage. Both occurrences keep one image. */
static unsigned prepared_native_borrow_controls(CettaLibraryContext *library,
        Space *space) {
    unsigned checks = 0u;
    for (unsigned frontier = 0u; frontier < 2u; frontier++) {
        Arena source, publication;
        arena_init_detached(&source);
        arena_init_detached(&publication);
        ArenaMark empty = arena_mark(&source);
        Atom *data = atom_expr_builder_begin(&source, 2049u);
        assert(data);
        data->expr.elems[0] = atom_symbol(&source, "Payload");
        for (unsigned i = 1u; i < 2049u; i++)
            data->expr.elems[i] = atom_int(&source, i);
        data = atom_expr_builder_finish(&source, data);
        prepared_borrowed_value = data;
        prepared_mode = frontier ? 22u : 21u;
        prepared_calls = prepared_attempts = prepared_effects = 0u;
        PettaEvalMachineContext context = {.fuel = -1, .library_context = library,
            .semantic_authority_epoch = 1u, .prepared_pure_cache = {.root_space = space}};
        Atom *call = atom_expr2(&source, atom_symbol(&source, "native-tail"), data);
        CettaPreparedPureAnswerCursor *cursor = petta_eval_machine_open_answer_cursor(
            &context, space, &source, call, NULL, NULL, NULL);
        assert(cursor && cetta_prepared_pure_answer_cursor_storage(cursor).live_bytes == 0u);
        CettaOwnedReturn returned = {0};
        CettaEvalCompletion completion = CETTA_EVAL_COMPLETE;
        prepared_borrowed_parent_depth = g_eval_gc.external_owner_depth;
        assert(petta_eval_machine_next_answer_cursor(&context, space, cursor,
            &returned, &completion) == CETTA_OWNED_EXECUTION_RETURN);
        assert(g_eval_gc.external_owner_depth == prepared_borrowed_parent_depth);
        assert(returned.outcome.kind == CETTA_CALL_VALUE &&
            returned.outcome.result_form == CETTA_CALL_RESULT_COMPLETED_VALUE);
        Atom *answer = returned.outcome.term;
        assert(atom_eq(answer, data));
        if (!frontier) {
            assert(answer == data);
            assert(cetta_prepared_pure_answer_cursor_storage(cursor).live_bytes < 65536u);
            assert(cetta_prepared_pure_answer_cursor_detach(cursor));
        } else {
            assert(answer != data);
            assert((answer->flags & ATOM_FLAG_ARENA_CLOSED) != 0u);
            CettaPreparedPureAnswerFrame frame;
            assert(cetta_prepared_pure_answer_cursor_frame(cursor, 0u, &frame));
            assert(frame.arity == 1u && frame.arguments[0] == answer);
        }
        Atom *selected = atom_deep_copy(&publication, answer);
        assert(selected);
        arena_reset(&source, empty);
        for (unsigned i = 0u; i < 4096u; i++)
            assert(atom_int(&source, -(int64_t)i));
        if (frontier) {
            memset(&returned, 0, sizeof(returned));
            assert(petta_eval_machine_next_answer_cursor(&context, space, cursor,
                &returned, &completion) == CETTA_OWNED_EXECUTION_RETURN);
            assert(returned.outcome.term == answer && atom_eq(returned.outcome.term, selected));
        }
        assert(petta_eval_machine_next_answer_cursor(&context, space, cursor,
            &returned, &completion) == CETTA_OWNED_EXECUTION_COMPLETE);
        assert(prepared_calls == 1u && prepared_attempts == 1u &&
            completion == CETTA_EVAL_COMPLETE);
        cetta_prepared_pure_answer_cursor_close(cursor);
        prepared_pure_program_cache_free(&context.prepared_pure_cache);
        assert(selected->expr.len == 2049u && selected->expr.elems[2048]->ground.ival == 2048);
        prepared_borrowed_value = NULL;
        arena_free(&source);
        arena_free(&publication);
        checks++;
    }
    return checks;
}

static unsigned prepared_native_gc_borrow_controls(CettaLibraryContext *library,
        Space *space, Arena *definitions) {
    enum { ITERATIONS = 4096u };
    const char *equations[] = {
        "(= (native-gc-loan $n $payload) (if (== $n 0) $payload "
        "(let $effect (__cetta_lib_json_parse_v1 $n) (native-gc-loan (- $n 1) $payload))))",
        "(= (native-gc-wrapper $n $payload) (native-gc-loan $n $payload))",
        "(= (native-gc-wrapper $n $payload) 17)",
    };
    for (unsigned i = 0u; i < sizeof(equations) / sizeof(*equations); i++) {
        Atom *equation = fixture_atom(definitions, equations[i]);
        const PettaPlanNode *source_plan = petta_program_plan_dynamic_add(library->petta_program, equation);
        CettaCount before = space_length64(space);
        assert(source_plan);
        space_add(space, equation);
        assert(petta_program_note_add(library->petta_program, space,
            space_get_at64(space, before), source_plan));
    }
    for (unsigned variant = 0u; variant < 4u; variant++) {
        unsigned route = variant / 2u;
        unsigned pause = variant % 2u;
        Arena input, publication;
        arena_init_detached(&input);
        arena_init_detached(&publication);
        ArenaMark empty = arena_mark(&input);
        Atom *payload = atom_expr_builder_begin(&input, 4097u);
        assert(payload);
        payload->expr.elems[0] = atom_symbol(&input, "Payload");
        for (unsigned index = 1u; index < 4097u; index++)
            payload->expr.elems[index] = atom_int(&input, index);
        payload->expr.elems[2] = payload->expr.elems[1];
        payload = atom_expr_builder_finish(&input, payload);
        assert(payload && (payload->flags & ATOM_FLAG_ARENA_CLOSED));
        Atom *call = atom_expr3(&input, atom_symbol(&input,
            route ? "native-gc-wrapper" : "native-gc-loan"),
            atom_int(&input, ITERATIONS), payload);
        PreparedPureClosedCallPlan plan;
        assert(prepared_pure_closed_call_plan(space, &input, call, -1, true, NULL, &plan));
        CettaPreparedPureProgram *program = cetta_prepared_pure_program_compile_closed_answers(
            space, call, plan.call_mode, plan.boolean_value, plan.construct_value,
            plan.constructs_expression, plan.opaque_value, plan.register_view,
            plan.expression_view, plan.pattern_view, &plan.source_view, true,
            plan.total_structural_equality, prepared_pure_match_decision_semantics(plan.call_mode));
        assert(program);
        CettaPreparedPureAnswerCursorOptions options = {
            .limits = {.max_transitions = pause ? 2048u : UINT64_MAX,
                .max_scratch_bytes = 65536u,
                .construct_allocation_bound = petta_semantics_construct_value_allocation_bound},
            .unmatched_call = CETTA_PREPARED_PURE_UNMATCHED_FAILS,
            .allow_continuations = true,
        };
        CettaPreparedPureAnswerCursor *cursor = cetta_prepared_pure_answer_cursor_open(program, &options);
        assert(cursor);
        cetta_prepared_pure_program_clear_closed_entry_call(program);
        prepared_mode = 0u;
        prepared_calls = prepared_attempts = prepared_effects = 0u;
        CettaOwnedReturn returned = {0};
        CettaEvalCompletion completion = CETTA_EVAL_COMPLETE;
        CettaOwnedExecutionStep first = prepared_pure_next_return(
            space, cursor, 0u, &returned, &completion);
        Arena *survivor = cetta_prepared_pure_program_closed_survivor_arena(program);
        if (!survivor)
            fprintf(stderr, "GC loan: pause=%u step=%u handoff=%u calls=%u\n", pause,
                (unsigned)first, (unsigned)cetta_prepared_pure_answer_cursor_handoff_reason(cursor),
                prepared_calls);
        assert(survivor && arena_accounted_live_bytes(survivor) < 4096u);
        if (pause) {
            assert(first == CETTA_OWNED_EXECUTION_HANDOFF &&
                cetta_prepared_pure_answer_cursor_handoff_reason(cursor) == CETTA_PREPARED_PURE_HANDOFF_LIMIT);
            assert(prepared_calls > 0u && prepared_calls < ITERATIONS);
            assert(cetta_prepared_pure_answer_cursor_detach(cursor));
            arena_reset(&input, empty);
            for (unsigned index = 0u; index < 8192u; index++)
                (void)atom_int(&input, -(int64_t)index);
            assert(cetta_prepared_pure_answer_cursor_refill_transitions(cursor, ITERATIONS * 4u, &completion));
            assert(prepared_pure_next_return(space, cursor, 0u, &returned, &completion) ==
                CETTA_OWNED_EXECUTION_RETURN);
        } else {
            assert(first == CETTA_OWNED_EXECUTION_RETURN && returned.outcome.term == payload);
        }
        Atom *answer = returned.outcome.term;
        assert(answer && answer->expr.len == 4097u &&
            answer->expr.elems[1] == answer->expr.elems[2] &&
            answer->expr.elems[4096]->ground.ival == 4096);
        assert(prepared_calls == ITERATIONS && prepared_attempts == ITERATIONS);
        Atom *published = atom_deep_copy(&publication, answer);
        assert(published && published->expr.elems[1] == published->expr.elems[2]);
        if (route) {
            assert(prepared_pure_next_return(space, cursor, 0u, &returned, &completion) ==
                CETTA_OWNED_EXECUTION_RETURN);
            assert(atom_eq(returned.outcome.term, atom_int(&publication, 17)));
        }
        assert(prepared_pure_next_return(space, cursor, 0u, &returned, &completion) ==
            CETTA_OWNED_EXECUTION_COMPLETE);
        assert(prepared_calls == ITERATIONS && prepared_attempts == ITERATIONS);
        cetta_prepared_pure_answer_cursor_close(cursor);
        cetta_prepared_pure_program_free(program);
        arena_free(&input);
        assert(published->expr.elems[4096]->ground.ival == 4096);
        arena_free(&publication);
    }
    return 4u;
}

static unsigned prepared_native_callee_controls(CettaLibraryContext *library,
        Space *space, Arena *definitions) {
    const char *equations[] = {
        "(= (native-entry-alt 7) (Box 7))",
        "(= (native-entry-alt 7) (Box 7))",
        "(= (native-entry-alt 0) (Box 0))",
        "(= (native-entry-run $value) (let $ready (__cetta_lib_json_parse_v1 $value) "
        "(let $answer (native-entry-alt $value) (Pair $ready $answer))))",
    };
    for (unsigned i = 0u; i < sizeof(equations) / sizeof(*equations); i++) {
        Atom *equation = fixture_atom(definitions, equations[i]);
        const PettaPlanNode *plan = petta_program_plan_dynamic_add(library->petta_program, equation);
        CettaCount before = space_length64(space);
        assert(plan);
        space_add(space, equation);
        assert(petta_program_note_add(library->petta_program, space,
            space_get_at64(space, before), plan));
    }
    uint64_t allowances[] = {0u, 1u, 2u, 63u, 64u, 65u, 128u, UINT64_MAX};
    for (unsigned i = 0u; i < sizeof(allowances) / sizeof(*allowances); i++) {
        Atom *call = fixture_atom(definitions, "(native-entry-run 7)");
        PreparedPureClosedCallPlan plan;
        assert(prepared_pure_closed_call_plan(space, definitions, call, -1, true, NULL, &plan));
        CettaPreparedPureProgram *program = cetta_prepared_pure_program_compile_closed_answers(
            space, call, plan.call_mode, plan.boolean_value, plan.construct_value,
            plan.constructs_expression, plan.opaque_value, plan.register_view,
            plan.expression_view, plan.pattern_view, &plan.source_view, true,
            plan.total_structural_equality, prepared_pure_match_decision_semantics(plan.call_mode));
        assert(program);
        CettaPreparedPureAnswerCursorOptions options = {
            .limits = {.max_transitions = allowances[i], .max_scratch_bytes = 1024u * 1024u,
                .construct_allocation_bound = petta_semantics_construct_value_allocation_bound},
            .unmatched_call = CETTA_PREPARED_PURE_UNMATCHED_FAILS,
            .allow_continuations = true,
        };
        CettaPreparedPureAnswerCursor *cursor = cetta_prepared_pure_answer_cursor_open(program, &options);
        assert(cursor);
        cetta_prepared_pure_program_clear_closed_entry_call(program);
        prepared_mode = 0u;
        prepared_calls = prepared_attempts = prepared_effects = 0u;
        CettaEvalCompletion completion = CETTA_EVAL_COMPLETE;
        unsigned answers = 0u, resumptions = 0u;
        bool preentry = false;
        for (;;) {
            CettaOwnedReturn returned = {0};
            CettaOwnedExecutionStep next = prepared_pure_next_return(
                space, cursor, 0u, &returned, &completion);
            if (next == CETTA_OWNED_EXECUTION_COMPLETE)
                break;
            if (next == CETTA_OWNED_EXECUTION_HANDOFF) {
                assert(cetta_prepared_pure_answer_cursor_handoff_reason(cursor) ==
                    CETTA_PREPARED_PURE_HANDOFF_LIMIT);
                assert(++resumptions < 8u);
                if (!cetta_prepared_pure_answer_cursor_native_started(cursor)) {
                    assert(prepared_calls == 0u && prepared_attempts == 0u && answers == 0u);
                    assert(!cetta_prepared_pure_answer_cursor_refill_transitions(cursor, 128u, &completion));
                    preentry = true;
                    break;
                }
                assert(cetta_prepared_pure_answer_cursor_detach(cursor));
                assert(cetta_prepared_pure_answer_cursor_refill_transitions(cursor, 128u, &completion));
                continue;
            }
            assert(next == CETTA_OWNED_EXECUTION_RETURN);
            assert(atom_eq(returned.outcome.term, fixture_atom(definitions, "(Pair 7 (Box 7))")));
            assert(++answers <= 2u && prepared_calls == 1u && prepared_attempts == 1u);
        }
        CettaEvalCompletion expected = preentry
            ? CETTA_EVAL_INCOMPLETE_CAPACITY : CETTA_EVAL_COMPLETE;
        if (completion != expected || (!preentry && answers != 2u))
            fprintf(stderr, "callee allowance=%" PRIu64 " answers=%u resumptions=%u calls=%u completion=%u\n",
                allowances[i], answers, resumptions, prepared_calls, (unsigned)completion);
        assert(completion == expected && (preentry || answers == 2u));
        assert(!preentry || allowances[i] < 2u);
        cetta_prepared_pure_answer_cursor_close(cursor);
        if (allowances[i] == UINT64_MAX) {
            /* Ambiguity belongs to the actual call. It must not poison
             * this reusable program for a different set of entry values. */
            for (unsigned reuse = 0u; reuse < 2u; reuse++) {
                call = fixture_atom(definitions, reuse
                    ? "(native-entry-run 7)" : "(native-entry-run 0)");
                assert(cetta_prepared_pure_program_rebind_closed_entry_call(program, call));
                cursor = cetta_prepared_pure_answer_cursor_open(program, &options);
                assert(cursor);
                cetta_prepared_pure_program_clear_closed_entry_call(program);
                prepared_calls = prepared_attempts = prepared_effects = 0u;
                completion = CETTA_EVAL_COMPLETE;
                answers = 0u;
                for (;;) {
                    CettaOwnedReturn returned = {0};
                    CettaOwnedExecutionStep next = prepared_pure_next_return(
                        space, cursor, 0u, &returned, &completion);
                    if (next == CETTA_OWNED_EXECUTION_COMPLETE)
                        break;
                    assert(next == CETTA_OWNED_EXECUTION_RETURN);
                    assert(atom_eq(returned.outcome.term, fixture_atom(definitions,
                        reuse ? "(Pair 7 (Box 7))" : "(Pair 7 (Box 0))")));
                    assert(++answers <= (reuse ? 2u : 1u));
                }
                assert(answers == (reuse ? 2u : 1u) && completion == CETTA_EVAL_COMPLETE);
                assert(prepared_calls == 1u && prepared_attempts == 1u);
                cetta_prepared_pure_answer_cursor_close(cursor);
            }
        }
        cetta_prepared_pure_program_free(program);
    }
    return sizeof(allowances) / sizeof(*allowances) + 2u;
}

static unsigned prepared_native_consumers(void) {
    unsigned checks = 0u;
    for (unsigned language = 0u; language < 2u; language++) {
        CettaLibraryContext *context = calloc(1u, sizeof(*context));
        assert(context);
        cetta_library_context_init_for_language_profile(context,
            language ? CETTA_LANGUAGE_HE : CETTA_LANGUAGE_PETTA, NULL);
        eval_set_library_context(context);
        Arena arena;
        arena_init(&arena);
        TermUniverse universe;
        term_universe_init(&universe);
        term_universe_set_persistent_arena(&universe, &arena);
        Space space;
        space_init_with_universe(&space, &universe);
        Registry registry;
        registry_init(&registry);
        registry_bind_id(&registry, g_builtin_syms.self, atom_space(&arena, &space));
        g_registry = &registry;
        const char *equations[] = {
            "(= (native-box $x) (let $v (__cetta_lib_json_parse_v1 $x) (Box $v $v)))",
            "(= (native-tail $x) (__cetta_lib_json_parse_v1 $x))",
            "(= (native-fault $x) (let $v (__cetta_lib_json_parse_v1 $x) (+ $v bad)))",
            "(= (only-a (A $x)) $x)",
            "(= (native-before-miss $x) (let $v (only-a B) (let $w (__cetta_lib_json_parse_v1 $x) (Box $v $w))))",
            "(= (native-two $x $y) (Pair $x $y))",
            "(= (native-partial $x) (native-two (__cetta_lib_json_parse_v1 $x)))",
            "(= (native-over $x) (native-two (__cetta_lib_json_parse_v1 $x) 2 3))",
            "(= (native-fresh $x) (let $v (__cetta_lib_json_parse_v1 $x) (Pair $v $unbound)))",
            "(= (native-condition $x) (if (__cetta_lib_json_parse_v1 $x) then else))",
            "(= (native-dynamic-head $x) (let $head (__cetta_lib_json_parse_v1 $x) ($head 4 5)))",
            "(= (native-tail-miss $x) (only-a (__cetta_lib_json_parse_v1 $x)))",
            "(= (native-bind-miss $x) (let $v (only-a (__cetta_lib_json_parse_v1 $x)) (Box $v)))",
        };
        for (size_t i = 0u; i < sizeof(equations) / sizeof(equations[0]); i++) {
            Atom *equation = fixture_atom(&arena, equations[i]);
            if (!language) {
                const PettaPlanNode *plan = petta_program_plan_dynamic_add(context->petta_program, equation);
                assert(plan);
                CettaCount before = space_length64(&space);
                space_add(&space, equation);
                assert(petta_program_note_add(context->petta_program, &space,
                    space_get_at64(&space, before), plan));
            } else
                assert(space_admit_atom(&space, &arena, equation));
        }
        if (!language)
            checks += failure_contract_controls(context, &space, &arena);
        checks += result_policy_admissions(context, &space, &arena,
            language ? CETTA_LANGUAGE_HE : CETTA_LANGUAGE_PETTA);
        prepared_head = atom_symbol_current_id(fixture_atom(&arena, "__cetta_lib_json_parse_v1"));
        native_frontier_capture_control(&space, &arena);
        checks++;
        prepared_fixture = true;
        if (!language) {
            checks += prepared_native_borrow_controls(context, &space);
            checks += prepared_native_gc_borrow_controls(context, &space, &arena);
            checks += prepared_native_callee_controls(context, &space, &arena);
        }
        for (prepared_mode = 0u; prepared_mode < 21u; prepared_mode++) {
            assert(cetta_library_petta_translator_rule_set(context, prepared_head, false));
            CettaEvalEpisode tracker = {0};
            g_eval_completion_tracker = &tracker;
            OutcomeSet outcomes;
            outcome_set_init(&outcomes);
            prepared_calls = prepared_attempts = prepared_effects = 0u;
            Atom *call = fixture_atom(&arena, prepared_mode == 9u ? "(native-fault 1)" : "(native-box 1)");
            CettaPreparedPureAnswersResult result = prepared_pure_closed_answers_try(
                &space, &arena, call, NULL, -1, NULL, NULL, false, &outcomes);
            if (result == CETTA_PREPARED_PURE_ANSWERS_DECLINED || prepared_calls != 1u)
                fprintf(stderr, "prepared ordinary boundary: lang=%u mode=%u result=%u calls=%u attempts=%u count=%" PRIu64 " completion=%u\n",
                    language, prepared_mode, result, prepared_calls, prepared_attempts,
                    (uint64_t)outcomes.len, tracker.completion);
            assert(result != CETTA_PREPARED_PURE_ANSWERS_DECLINED && prepared_calls == 1u);
            assert(prepared_attempts == 1u);
            Atom *raised = petta_eval_take_raise();
            if (prepared_mode == 13u) {
                assert(result == CETTA_PREPARED_PURE_ANSWERS_STOPPED && outcomes.len == 0u);
                assert(tracker.completion == CETTA_EVAL_INCOMPLETE_INVALIDATED);
            } else if (prepared_mode == 8u || prepared_mode == 10u || prepared_mode == 12u ||
                       prepared_mode == 16u || prepared_mode == 20u) {
                assert(result == CETTA_PREPARED_PURE_ANSWERS_STOPPED &&
                    outcomes.len == (prepared_mode == 12u ? 1u : 0u));
                assert(tracker.completion == (prepared_mode == 8u || prepared_mode == 20u
                    ? CETTA_EVAL_INCOMPLETE_CANCELLED : CETTA_EVAL_INCOMPLETE_HOST_FAILURE));
            } else if (prepared_mode == 4u || prepared_mode == 9u) {
                if (!language)
                    assert(raised && outcomes.len == 0u);
                else {
                    assert(outcomes.len == 1u);
                    Atom *value = outcome_atom_materialize(&arena, &outcomes.items[0]);
                    /* HE keeps this ill-typed scalar application unreduced;
                     * the frozen implementation is the independent oracle. */
                    assert(prepared_mode == 9u
                        ? atom_eq(value, fixture_atom(&arena, "(+ 7 bad)"))
                        : atom_is_error(value));
                }
            } else {
                assert(!raised && tracker.completion == CETTA_EVAL_COMPLETE);
                unsigned count = prepared_mode == 2u ? 3u : prepared_mode == 3u || prepared_mode == 15u ? 0u
                    : prepared_mode == 14u || prepared_mode == 17u ? 2u
                    : prepared_mode == 19u ? (language ? 1u : 2u) : 1u;
                if (outcomes.len != count) {
                    fprintf(stderr, "native frontier lang=%u mode=%u count=%" PRIu64 " expected=%u\n",
                        language, prepared_mode, (uint64_t)outcomes.len, count);
                    for (CettaCount row = 0u; row < outcomes.len; row++)
                        fprintf(stderr, "%s\n", atom_to_string(&arena,
                            outcome_atom_materialize(&arena, &outcomes.items[row])));
                }
                assert(outcomes.len == count);
                for (unsigned i = 0u; i < count; i++) {
                    Atom *value = outcome_atom_materialize(&arena, &outcomes.items[i]);
                    assert(value && value->kind == ATOM_EXPR && value->expr.len == 3u);
                    assert(atom_eq(value->expr.elems[1], value->expr.elems[2]));
                    if (prepared_mode == 5u)
                        assert(atom_eq(value->expr.elems[1], atom_int(&arena, 5)));
                    if (prepared_mode == 14u)
                        assert(atom_eq(value->expr.elems[1], atom_int(&arena, 6)));
                    if (prepared_mode == 17u)
                        assert(atom_eq(value->expr.elems[1], fixture_atom(&arena, "(+ (readln!) 1)")));
                    if (prepared_mode == 18u)
                        assert(atom_eq(value->expr.elems[1], fixture_atom(&arena, "(readln!)")));
                    if (prepared_mode == 19u)
                        assert(atom_eq(value->expr.elems[1], !language && i == 0u
                            ? fixture_atom(&arena, "(Error native datum)") : atom_int(&arena, 7)));
                }
            }
            assert(prepared_effects == (prepared_mode == 14u || prepared_mode == 20u ? 2u
                : prepared_mode == 5u || prepared_mode == 13u ? 1u : 0u));
            outcome_set_free(&outcomes);
            g_eval_completion_tracker = NULL;
            checks++;
        }
        assert(cetta_library_petta_translator_rule_set(context, prepared_head, false));
        if (!language)
            checks += prepared_machine_consumers(context, &space, &arena);
        {
            PreparedPureProgramCache cache = {.root_space = &space};
            Atom *call = fixture_atom(&arena, "(native-box 1)");
            CettaCallOutcome returned;
            prepared_mode = prepared_calls = prepared_attempts = 0u;
            for (unsigned retry = 0u; retry < 2u; retry++)
                assert(!prepared_pure_closed_call_try(&space, &arena, call, -1,
                    true, true, &cache, &returned));
            assert(prepared_calls == 0u && prepared_attempts == 0u);
            PreparedPureClosedCallPlan plan;
            assert(prepared_pure_closed_call_plan(&space, &arena, call, -1, true,
                &cache, &plan));
            assert(prepared_pure_program_cache_has_decline_head(&cache, &space,
                atom_symbol_current_id(call->expr.elems[0]), 1u, plan.source_view.root,
                plan.language_id, plan.call_mode, false,
                plan.cache_entry_arguments_are_values, plan.total_structural_equality));
            OutcomeSet result;
            outcome_set_init(&result);
            assert(prepared_pure_closed_answers_try(&space, &arena, call, NULL, -1,
                &cache, NULL, false, &result) == CETTA_PREPARED_PURE_ANSWERS_COMPLETE);
            assert(result.len == 1u && prepared_calls == 1u && prepared_attempts == 1u);
            assert(atom_eq(outcome_atom_materialize(&arena, &result.items[0]),
                fixture_atom(&arena, "(Box 7 7)")));
            outcome_set_free(&result);
            prepared_pure_program_cache_free(&cache);
            checks++;
        }
        for (unsigned body = 0u; body < 2u; body++) {
            for (unsigned frontier = 0u; frontier < 2u; frontier++) {
                CettaEvalEpisode tracker = {0};
                g_eval_completion_tracker = &tracker;
                OutcomeSet result;
                outcome_set_init(&result);
                prepared_mode = frontier ? 2u : 0u;
                prepared_calls = prepared_attempts = 0u;
                CettaPreparedPureAnswersResult completed = prepared_pure_closed_answers_try(
                    &space, &arena, fixture_atom(&arena, body
                        ? "(native-bind-miss 1)" : "(native-tail-miss 1)"), NULL,
                    -1, NULL, NULL, false, &result);
                assert(completed == CETTA_PREPARED_PURE_ANSWERS_COMPLETE &&
                    tracker.completion == CETTA_EVAL_COMPLETE &&
                    prepared_calls == 1u && prepared_attempts == 1u && !petta_eval_take_raise());
                unsigned count = language ? (frontier ? 3u : 1u) : 0u;
                assert(result.len == count);
                for (unsigned k = 0u; k < count; k++) {
                    char expected[64];
                    snprintf(expected, sizeof(expected), body
                        ? "(Box (only-a %u))" : "(only-a %u)", k == 2u ? 9u : 7u);
                    assert(atom_eq(outcome_atom_materialize(&arena, &result.items[k]),
                        fixture_atom(&arena, expected)));
                }
                outcome_set_free(&result);
                g_eval_completion_tracker = NULL;
                checks++;
            }
        }
        {
            CettaEvalEpisode tracker = {0};
            g_eval_completion_tracker = &tracker;
            OutcomeSet result;
            outcome_set_init(&result);
            prepared_mode = prepared_calls = prepared_attempts = 0u;
            assert(prepared_pure_closed_answers_try(&space, &arena,
                fixture_atom(&arena, "(native-before-miss 1)"), NULL, -1, NULL, NULL,
                false, &result) == CETTA_PREPARED_PURE_ANSWERS_COMPLETE);
            assert(tracker.completion == CETTA_EVAL_COMPLETE &&
                prepared_calls == language && prepared_attempts == language &&
                result.len == language);
            if (language)
                assert(atom_eq(outcome_atom_materialize(&arena, &result.items[0]),
                    fixture_atom(&arena, "(Box (only-a B) 7)")));
            outcome_set_free(&result);
            g_eval_completion_tracker = NULL;
            checks++;
        }
        for (unsigned dynamic = 0u; dynamic < 2u; dynamic++) {
            CettaEvalEpisode tracker = {0};
            g_eval_completion_tracker = &tracker;
            OutcomeSet result;
            outcome_set_init(&result);
            prepared_mode = prepared_calls = prepared_attempts = 0u;
            Atom *call = fixture_atom(&arena, dynamic
                ? "(native-dynamic-head 1)" : "(native-condition 1)");
            CettaPreparedPureAnswersResult completed = prepared_pure_closed_answers_try(
                &space, &arena, call, NULL, -1, NULL, NULL, false, &result);
            if (dynamic || language) {
                assert(completed == CETTA_PREPARED_PURE_ANSWERS_DECLINED &&
                    prepared_calls == 0u && prepared_attempts == 0u && result.len == 0u);
                if (!dynamic) {
                    eval_for_caller(&space, &arena, NULL, call, -1, NULL, false, &result);
                    assert(prepared_calls == 1u && result.len == 1u);
                    Atom *error = outcome_atom_materialize(&arena, &result.items[0]);
                    assert(atom_is_error(error) && error->expr.len == 3u &&
                        atom_eq(error->expr.elems[2],
                            fixture_atom(&arena, "(BadArgType 1 Bool Number)")));
                }
            } else {
                assert(completed == CETTA_PREPARED_PURE_ANSWERS_COMPLETE &&
                    prepared_calls == 1u && prepared_attempts == 1u && result.len == 1u);
                assert(atom_eq(outcome_atom_materialize(&arena, &result.items[0]),
                    fixture_atom(&arena, "else")));
            }
            outcome_set_free(&result);
            g_eval_completion_tracker = NULL;
            checks++;
        }
        const char *remaining_calls[] = {
            "(native-partial 1)", "(native-over 1)", "(native-fresh 1)",
        };
        for (size_t k = 0u; k < sizeof(remaining_calls) / sizeof(remaining_calls[0]); k++) {
            OutcomeSet result;
            outcome_set_init(&result);
            prepared_mode = prepared_calls = prepared_attempts = 0u;
            assert(prepared_pure_closed_answers_try(&space, &arena,
                fixture_atom(&arena, remaining_calls[k]), NULL, -1, NULL, NULL, false,
                &result) == CETTA_PREPARED_PURE_ANSWERS_DECLINED);
            assert(prepared_calls == 0u && prepared_attempts == 0u && result.len == 0u);
            outcome_set_free(&result);
            checks++;
        }
        CettaEvalEpisode limited = {.budget_limited = true,
            .budget_initial = 1u, .budget_remaining = 1u};
        g_eval_completion_tracker = &limited;
        OutcomeSet bounded;
        outcome_set_init(&bounded);
        prepared_mode = prepared_calls = prepared_attempts = 0u;
        assert(prepared_pure_closed_answers_try(&space, &arena,
            fixture_atom(&arena, "(native-box 1)"), NULL, -1, NULL, NULL, false, &bounded) ==
            CETTA_PREPARED_PURE_ANSWERS_DECLINED);
        assert(prepared_calls == 0u && prepared_attempts == 0u && bounded.len == 0u);
        assert(limited.budget_remaining == 1u);
        outcome_set_free(&bounded);
        g_eval_completion_tracker = NULL;
        checks++;

        for (int fuel = 0; fuel <= 1; fuel++) {
            EvalOutcome observation;
            eval_outcome_init(&observation);
            prepared_mode = 14u;
            prepared_calls = prepared_attempts = prepared_effects = 0u;
            metta_eval_outcome(&space, &arena, NULL, fixture_atom(&arena,
                "(superpose ((native-box 1) (native-box 1)))"), fuel, &observation);
            assert(observation.completion == CETTA_EVAL_INCOMPLETE_FUEL);
            assert(observation.results.len == 0u && prepared_calls == 0u &&
                prepared_attempts == 0u && prepared_effects == 0u);
            assert(observation.steps_spent <= (uint64_t)fuel);
            eval_outcome_free(&observation);
            checks++;
        }

        assert(space_admit_atom(&space, &arena, fixture_atom(&arena,
            "(= (__cetta_lib_json_parse_v1 $x) authored)")));
        OutcomeSet declined;
        outcome_set_init(&declined);
        prepared_mode = prepared_calls = prepared_attempts = 0u;
        assert(prepared_pure_closed_answers_try(&space, &arena,
            fixture_atom(&arena, "(native-box 1)"), NULL, -1, NULL, NULL, false, &declined) ==
            CETTA_PREPARED_PURE_ANSWERS_DECLINED);
        assert(prepared_calls == 0u && prepared_attempts == 0u && declined.len == 0u);
        outcome_set_free(&declined);
        checks++;
        prepared_fixture = false;
        g_registry = NULL;
        eval_release_temporary_spaces();
        registry_free(&registry);
        space_free(&space);
        term_universe_free(&universe);
        eval_set_library_context(NULL);
        cetta_library_context_free(context);
        free(context);
        arena_free(&arena);
    }
    return checks;
}

static unsigned closed_failure_cache(void) {
    unsigned checks = 0u;
    for (unsigned language = 0u; language < 2u; language++) {
        CettaLibraryContext *library = calloc(1u, sizeof(*library));
        assert(library);
        cetta_library_context_init_for_language_profile(library,
            language ? CETTA_LANGUAGE_HE : CETTA_LANGUAGE_PETTA, NULL);
        eval_set_library_context(library);
        Arena source;
        arena_init(&source);
        TermUniverse universe;
        term_universe_init(&universe);
        term_universe_set_persistent_arena(&universe, &source);
        Space space;
        space_init_with_universe(&space, &universe);
        Atom *equation = fixture_atom(&source,
            "(= (failure-cache $p) (let (Pair $x $y) $p 7))");
        if (!language) {
            const PettaPlanNode *plan = petta_program_plan_dynamic_add(
                library->petta_program, equation);
            assert(plan);
            CettaCount position = space_length64(&space);
            space_add(&space, equation);
            assert(petta_program_note_add(library->petta_program, &space,
                space_get_at64(&space, position), plan));
        } else
            assert(space_admit_atom(&space, &source, equation));
        PreparedPureProgramCache cache = {.root_space = &space};
        CettaPreparedPureProgram *compiled = NULL;
        const char *calls[] = {"(failure-cache (Single 3))", "(failure-cache (Pair 4 5))",
            "(failure-cache (Single 8))", "(failure-cache (Pair 6 7))"};
        for (unsigned index = 0u; index < 4u; index++) {
            Atom *call = fixture_atom(&source, calls[index]);
            CettaCallOutcome returned;
            assert(prepared_pure_closed_call_try(&space, &source, call, -1,
                true, true, &cache, &returned));
            if (index % 2u == 0u)
                assert(returned.kind == CETTA_CALL_FAILURE && !returned.term);
            else
                assert(returned.kind == CETTA_CALL_VALUE &&
                    returned.result_form == CETTA_CALL_RESULT_COMPLETED_VALUE &&
                    atom_eq(returned.term, atom_int(&source, 7)));
            PreparedPureClosedCallPlan plan;
            assert(prepared_pure_closed_call_plan(&space, &source, call, -1, true,
                &cache, &plan));
            bool declined = false;
            CettaPreparedPureProgram *cached = prepared_pure_program_cache_lookup(
                &cache, &space, call, plan.source_view.root, plan.language_id,
                plan.call_mode, false, true, plan.total_structural_equality, &declined);
            assert(cached && !declined);
            if (index == 0u)
                compiled = cached;
            else
                assert(cached == compiled);
            checks++;
        }
        if (!language) {
            PettaEvalMachineContext context = {.library_context = library,
                .fuel = -1, .prepared_pure_cache = {.root_space = &space}};
            SymbolId head = symbol_intern_cstr(g_symbols, "failure-cache");
            for (unsigned index = 0u; index < 4u; index++) {
                Atom *call = fixture_atom(&source, calls[index]);
                CettaCallOutcome returned;
                assert(petta_eval_open_execute_pure_relation(&context, &space,
                    &source, head, call->expr.elems + 1u, 1u, &returned));
                assert(returned.kind == (index % 2u == 0u
                    ? CETTA_CALL_FAILURE : CETTA_CALL_VALUE));
                if (returned.kind == CETTA_CALL_VALUE)
                    assert(returned.result_form == CETTA_CALL_RESULT_COMPLETED_VALUE &&
                        atom_eq(returned.term, atom_int(&source, 7)));
                checks++;
            }
            prepared_pure_program_cache_free(&context.prepared_pure_cache);
        }
        equation = fixture_atom(&source, "(= (failure-cache $p) 8)");
        if (!language) {
            const PettaPlanNode *plan = petta_program_plan_dynamic_add(
                library->petta_program, equation);
            assert(plan);
            CettaCount position = space_length64(&space);
            space_add(&space, equation);
            assert(petta_program_note_add(library->petta_program, &space,
                space_get_at64(&space, position), plan));
        } else
            assert(space_admit_atom(&space, &source, equation));
        CettaCallOutcome returned;
        assert(!prepared_pure_closed_call_try(&space, &source,
            fixture_atom(&source, calls[0]), -1, true, true, &cache, &returned));
        assert(returned.kind != CETTA_CALL_FAILURE);
        checks++;
        prepared_pure_program_cache_free(&cache);
        space_free(&space);
        term_universe_free(&universe);
        arena_free(&source);
        eval_set_library_context(NULL);
        cetta_library_context_free(library);
        free(library);
    }
    return checks;
}

static unsigned scalar_callable_admission(void) {
    CettaLibraryContext *library = calloc(1u, sizeof(*library));
    assert(library);
    cetta_library_context_init_for_language_profile(library, CETTA_LANGUAGE_PETTA, NULL);
    eval_set_library_context(library);
    Arena source, owner;
    arena_init(&source);
    arena_init(&owner);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &source);
    Space space;
    space_init_with_universe(&space, &universe);
    PettaEvalMachineContext context = {.library_context = library, .fuel = -1};
    prepared_head = atom_symbol_current_id(fixture_atom(&source, "__cetta_lib_json_parse_v1"));
    prepared_fixture = true;
    prepared_mode = prepared_calls = prepared_attempts = 0u;
    Atom *native = petta_capture_lambda(&source,
        fixture_atom(&source, "(|-> ($x) (__cetta_lib_json_parse_v1 $x))"), NULL);
    Atom *pure = petta_capture_lambda(&source,
        fixture_atom(&source, "(|-> ($x) (+ $x 1))"), NULL);
    assert(native && pure);
    assert(!petta_eval_open_prepare_pure_callable(&context, &space, &owner, native, 1u));
    assert(prepared_calls == 0u && prepared_attempts == 0u);
    void *compiled = petta_eval_open_prepare_pure_callable(&context, &space, &owner, pure, 1u);
    assert(compiled);
    Atom *arguments[] = {atom_int(&source, 5)};
    Atom *result = NULL;
    assert(petta_eval_open_execute_pure_callable(&context, compiled, &owner,
        arguments, 1u, &result));
    assert(atom_eq(result, atom_int(&source, 6)));
    petta_eval_open_release_pure_callable(&context, compiled);
    prepared_fixture = false;
    space_free(&space);
    term_universe_free(&universe);
    arena_free(&owner);
    arena_free(&source);
    eval_set_library_context(NULL);
    cetta_library_context_free(library);
    free(library);
    return 2u;
}

static Space *refusal_test_space;
static Arena *refusal_test_owner;
static bool refusal_test_mutate;
static CettaPreparedPureExpressionViewState refusal_test_view(Space *space, const Atom *atom,
        const CettaComputationPlan *computation, CettaPreparedPureExpressionView *view) {
    if (atom && atom->kind == ATOM_EXPR && atom->expr.len &&
        atom_is_symbol(atom->expr.elems[0], "refuse-frontend")) {
        if (refusal_test_mutate) {
            refusal_test_mutate = false;
            Arena *owner = refusal_test_owner;
            assert(space_admit_atom(refusal_test_space, owner,
                fixture_atom(owner, "(= (new-dependency) 1)")));
        }
        return CETTA_PREPARED_PURE_EXPRESSION_DECLINE;
    }
    if (atom && atom->kind == ATOM_EXPR && atom->expr.len == 2u &&
        atom_is_symbol(atom->expr.elems[0], "observe-demand")) {
        view->projected = atom->expr.elems[1];
        view->projection_mode = CETTA_PREPARED_PURE_PROJECT_CODE;
        return CETTA_PREPARED_PURE_EXPRESSION_PROJECT_SINGLE_RESULT;
    }
    return petta_prepared_pure_expression_view(space, atom, computation, view);
}

static unsigned shared_compilation_refusals(void) {
    CettaLibraryContext *library = calloc(1u, sizeof(*library));
    assert(library);
    cetta_library_context_init_for_language_profile(library, CETTA_LANGUAGE_PETTA, NULL);
    eval_set_library_context(library);
    Arena source;
    arena_init(&source);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &source);
    Space space;
    space_init_with_universe(&space, &universe);
    const char *equations[] = {"(= (shared-refusal $x) (refuse-frontend $x))",
        "(= (one-answer $x) (+ $x 1))", "(= (many-answers $x) 1)",
        "(= (many-answers $x) 2)", "(= (zero-answers $x) (empty))",
        "(= (observed-refusal $x) (observe-demand (refuse-frontend $x)))",
        "(: observed-earlier (-> Number Number))",
        "(= (observed-earlier $x) (refuse-frontend $x))"};
    for (unsigned i = 0u; i < sizeof(equations) / sizeof(equations[0]); i++)
        assert(space_admit_atom(&space, &source, fixture_atom(&source, equations[i])));
    const char *calls[] = {"(shared-refusal 5)", "(one-answer 5)",
        "(many-answers 5)", "(zero-answers 5)", "(observed-refusal 5)",
        "(observed-earlier 5)"};
    unsigned checks = 0u;
    for (unsigned i = 0u; i < sizeof(calls) / sizeof(calls[0]); i++) {
        bool shared = true;
        Atom *call = fixture_atom(&source, calls[i]);
        CettaPreparedPureProgram *scalar = cetta_prepared_pure_program_compile_closed(
            &space, call, CETTA_GSLT_PURE_CALL_EAGER, atom_bool, atom_expr,
            cetta_prepared_pure_constructs_expression_always, NULL, NULL,
            refusal_test_view, NULL, NULL, true, true,
            (CettaMatchDecisionSemanticIdentity){0}, &shared);
        assert(shared == (i == 0u || i == 4u));
        assert(!!scalar == (i == 1u));
        CettaPreparedPureProgram *answers = cetta_prepared_pure_program_compile_closed_answers(
            &space, call, CETTA_GSLT_PURE_CALL_EAGER, atom_bool, atom_expr,
            cetta_prepared_pure_constructs_expression_always, NULL, NULL,
            refusal_test_view, NULL, NULL, true, true,
            (CettaMatchDecisionSemanticIdentity){0});
        assert(!!answers == (i >= 1u && i <= 3u));
        cetta_prepared_pure_program_free(scalar);
        cetta_prepared_pure_program_free(answers);
        checks++;
    }
    /* A source change during the read invalidates an otherwise shared
     * refusal. The callback is adversarial; no evidence may outlive it. */
    refusal_test_space = &space;
    refusal_test_owner = &source;
    refusal_test_mutate = true;
    bool shared = true;
    CettaPreparedPureProgram *changed = cetta_prepared_pure_program_compile_closed(
        &space, fixture_atom(&source, calls[0]), CETTA_GSLT_PURE_CALL_EAGER,
        atom_bool, atom_expr, cetta_prepared_pure_constructs_expression_always,
        NULL, NULL, refusal_test_view, NULL, NULL, true, true,
        (CettaMatchDecisionSemanticIdentity){0}, &shared);
    assert(!changed && !shared && !refusal_test_mutate);
    refusal_test_space = NULL;
    refusal_test_owner = NULL;
    space_free(&space);
    term_universe_free(&universe);
    arena_free(&source);
    eval_set_library_context(NULL);
    cetta_library_context_free(library);
    free(library);
    return checks + 1u;
}

typedef struct {
    SymbolId head;
    bool admit;
    unsigned inquiries;
    PettaProgram *program;
} InvocationAdmission;

static PettaMachineSpaceQueryAdmission invocation_admit_values(void *opaque,
        Space *space, SymbolId head, Bindings *environment,
        const BindingValue *arguments, CettaExprLen arity) {
    (void)space;
    (void)environment;
    InvocationAdmission *admission = opaque;
    if (head != admission->head)
        return PETTA_MACHINE_SPACE_QUERY_DEFER;
    admission->inquiries++;
    assert(arity == 3u);
    assert(arguments[0].skeleton && arguments[1].skeleton && arguments[2].skeleton);
    return admission->admit ? PETTA_MACHINE_SPACE_QUERY_ADMITTED
                            : PETTA_MACHINE_SPACE_QUERY_DEFER;
}

static bool invocation_candidate_snapshot(void *opaque, Space *space, SymbolId head,
        PettaCandidateSnapshotLease *lease, PettaCandidateSnapshotStats *stats) {
    InvocationAdmission *admission = opaque;
    return petta_program_candidate_snapshot_lease_profiled(
        admission->program, space, head, lease, stats);
}

static unsigned source_computation_plans(void) {
    Arena source;
    arena_init(&source);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &source);
    Space space;
    space_init_with_universe(&space, &universe);
    PettaProgram *program = petta_program_new();
    assert(program);
    Atom *equation = fixture_atom(&source, "(= (source-probe $x) $x)");
    const PettaPlanNode *definition = petta_program_plan_dynamic_add(program, equation);
    space_add(&space, equation);
    assert(petta_program_note_add(program, &space, space_get_at64(&space, 0u), definition));
    const struct { const char *source; PettaPlanRole authority; unsigned operation; } cases[] = {
        {"$v", PETTA_PLAN_VALUE, CETTA_CONTROL_RETURN_VALUE},
        {"()", PETTA_PLAN_DATA, CETTA_CONTROL_CONSTRUCT},
        {"(source-packet 7)", PETTA_PLAN_DATA, CETTA_CONTROL_CONSTRUCT},
        {"(source-probe 7)", PETTA_PLAN_STATIC_CALL, CETTA_CONTROL_INVOKE},
        {"($f 7)", PETTA_PLAN_DYNAMIC_CALL, CETTA_CONTROL_INVOKE},
        {"(eval $c)", PETTA_PLAN_STATIC_CALL, CETTA_CONTROL_FORCE},
        {"(progn 1 2)", PETTA_PLAN_STATIC_CALL, CETTA_CONTROL_SEQUENCE_LAST},
        {"(prog1 1 2)", PETTA_PLAN_STATIC_CALL, CETTA_CONTROL_SEQUENCE_FIRST},
        {"(progn)", PETTA_PLAN_STATIC_CALL, CETTA_CONTROL_FAILURE},
        {"(prog1)", PETTA_PLAN_STATIC_CALL, CETTA_CONTROL_FAILURE},
    };
    for (size_t index = 0u; index < sizeof(cases) / sizeof(*cases); index++) {
        const PettaPlanNode *plan = petta_program_plan_current(
            program, fixture_atom(&source, cases[index].source));
        assert(plan && plan->role == cases[index].authority);
        assert(plan->computation.control.operation == cases[index].operation);
        assert(petta_plan_computation(plan).control.operation == cases[index].operation);
        PettaPlanNode absent = *plan;
        absent.computation = (CettaComputationPlan){0};
        assert(petta_plan_computation(&absent).control.operation == CETTA_CONTROL_NONE);
        assert(petta_plan_computation(&absent).invocation.operation == CETTA_INVOCATION_NONE);
    }
    const PettaPlanNode *data = petta_program_plan_current(program,
        fixture_atom(&source, "(source-packet (source-probe 7))"));
    assert(data && data->computation.control.operation == CETTA_CONTROL_CONSTRUCT &&
        data->children[1].computation.control.operation == CETTA_CONTROL_INVOKE);
    PettaTranslationEnvironment translation = {.host_identity = program,
        .admission_revision = 1u};
    Atom *inert = fixture_atom(&source, "(source-packet 7)");
    const PettaPlanNode *translated = petta_program_plan_transient(
        program, inert, &translation);
    assert(translated && translated->role == PETTA_PLAN_VALUE &&
        translated->computation.control.operation == CETTA_CONTROL_RETURN_VALUE);
    const PettaPlanNode *cached = petta_program_plan_transient(
        program, inert, &translation);
    assert(cached && cached->role == PETTA_PLAN_VALUE &&
        cached->computation.control.operation == CETTA_CONTROL_RETURN_VALUE);
    const PettaPlanNode *variable = petta_program_plan_transient(
        program, fixture_atom(&source, "$v"), &translation);
    assert(variable && variable->role == PETTA_PLAN_VALUE &&
        variable->computation.control.operation == CETTA_CONTROL_RETURN_VALUE);
    const PettaPlanNode *mixed = petta_program_plan_transient(program,
        fixture_atom(&source, "(source-packet (source-probe 7) (plain 9))"),
        &translation);
    assert(mixed && mixed->computation.control.operation == CETTA_CONTROL_CONSTRUCT &&
        mixed->children[0].role == PETTA_PLAN_VALUE &&
        mixed->children[0].computation.control.operation == CETTA_CONTROL_RETURN_VALUE);
    assert(mixed->children[1].computation.control.operation == CETTA_CONTROL_INVOKE &&
        mixed->children[2].role == PETTA_PLAN_VALUE &&
        mixed->children[2].computation.control.operation == CETTA_CONTROL_RETURN_VALUE);
    for (unsigned arity = 0u; arity <= 5u; arity++) {
        CettaControlOperationPlan force = petta_semantics_elaborate_control(
            SYMBOL_ID_NONE, PETTA_FORM_EVAL, arity);
        assert(force.operation == (arity == 1u ? CETTA_CONTROL_FORCE : CETTA_CONTROL_NONE));
        assert(petta_semantics_elaborate_control(
            g_builtin_syms.eval, PETTA_FORM_NONE, arity).operation == CETTA_CONTROL_NONE);
        CettaComputationPlan selected = petta_semantics_elaborate_computation(
            g_builtin_syms.petta_member, PETTA_FORM_IF, arity);
        /* The admitted dialect form owns interpretation even when the
         * physical head also names a native operation of this arity. */
        assert(selected.control.operation ==
            (arity == 2u || arity == 3u ? CETTA_CONTROL_BRANCH : CETTA_CONTROL_NONE));
        assert(selected.invocation.operation == CETTA_INVOCATION_NONE);
    }
    CettaExprLen series_arities[] = {0u, 1u, 2u, 8u, 255u, 256u, 4096u, UINT64_MAX};
    for (size_t i = 0u; i < sizeof(series_arities) / sizeof(*series_arities); i++) {
        CettaExprLen arity = series_arities[i];
        for (unsigned first = 0u; first < 2u; first++) {
            CettaComputationPlan computation = petta_semantics_elaborate_computation(
                SYMBOL_ID_NONE, first ? PETTA_FORM_PROG1 : PETTA_FORM_PROGN, arity);
            assert(computation.control.operation == (!arity ? CETTA_CONTROL_FAILURE :
                first ? CETTA_CONTROL_SEQUENCE_FIRST : CETTA_CONTROL_SEQUENCE_LAST));
            assert(computation.invocation.operation == CETTA_INVOCATION_NONE);
            assert(cetta_control_operation_series_result(computation.control, arity) ==
                (!arity ? 0u : first ? 1u : arity));
        }
    }
    petta_program_free(program);
    space_free(&space);
    term_universe_free(&universe);
    arena_free(&source);
    return 38u;
}

static unsigned cold_function_type_cache(void) {
    Arena arena;
    arena_init_detached(&arena);
    Space space;
    space_init(&space);
    Atom *head = atom_symbol(&arena, "cold-type-cache-head");
    eval_profiled_type_cache_free_for_current_thread();
    assert(!g_profiled_type_cache);
    assert(!eval_atom_has_function_type_profiled(&space, &arena, head));
    space_add(&space, fixture_atom(&arena,
        "(: cold-type-cache-head (-> Number Number))"));
    assert(eval_atom_has_function_type_profiled(&space, &arena, head));
    eval_profiled_type_cache_free_for_current_thread();
    assert(!g_profiled_type_cache);
    assert(eval_atom_has_function_type_profiled(&space, &arena, head));
    eval_profiled_type_cache_free_for_current_thread();
    space_free(&space);
    arena_free(&arena);
    return 5u;
}

static bool invocation_rewrite(void *opaque, Space *space, Arena *arena,
        Atom *call, Atom **out) {
    (void)space;
    *out = call;
    if (opaque && call->kind == ATOM_EXPR && call->expr.len == 4u &&
        atom_is_symbol_id(call->expr.elems[0], g_builtin_syms.if_text)) {
        Atom *fields[] = {atom_symbol_id(arena, g_builtin_syms.let),
            call->expr.elems[1], call->expr.elems[2], call->expr.elems[3]};
        *out = atom_expr(arena, fields, 4u);
    }
    if (call->kind == ATOM_EXPR && call->expr.len == 2u &&
        atom_is_symbol_id(call->expr.elems[0], g_builtin_syms.repr)) {
        *out = atom_expr2(arena, atom_symbol(arena, "sread"), call->expr.elems[1]);
    }
    return *out != NULL;
}

static unsigned source_invocation_plans(void) {
    Arena source;
    arena_init(&source);
    Space space;
    space_init(&space);
    PettaProgram *program = petta_program_new();
    assert(program);
    const struct { const char *call; unsigned operation, arity; } cases[] = {
        {"(repr 7)", CETTA_INVOCATION_HOST, 1u},
        {"(member 7 (7 8))", CETTA_INVOCATION_RELATIONAL_MEMBER, 2u},
        {"(car-atom (7))", CETTA_INVOCATION_LIST_UNARY, 1u},
        {"(cdr-atom (7))", CETTA_INVOCATION_LIST_UNARY, 1u},
        {"(decons-atom (7))", CETTA_INVOCATION_LIST_UNARY, 1u},
        {"(decons (7))", CETTA_INVOCATION_LIST_UNARY, 1u},
        {"(last (7))", CETTA_INVOCATION_LIST_UNARY, 1u},
        {"(reverse (7))", CETTA_INVOCATION_LIST_UNARY, 1u},
        {"(msort (7))", CETTA_INVOCATION_LIST_UNARY, 1u},
        {"(sort-atom (7))", CETTA_INVOCATION_LIST_UNARY, 1u},
        {"(Predicate (p 7))", CETTA_INVOCATION_PREDICATE_VALUE, 1u},
        {"(list_to_set (7))", CETTA_INVOCATION_LIST_SET, 1u},
        {"(alpha-unique-atom (7))", CETTA_INVOCATION_LIST_SET, 1u},
        {"(exclude-item (7) 7)", CETTA_INVOCATION_LIST_SET, 2u},
        {"(sread \"(p 7)\")", CETTA_INVOCATION_READ_TERM, 1u},
        {"(import_prolog_function p)", CETTA_INVOCATION_EXTENSION, 1u},
        {"(callPredicate (Predicate (p 7)))", CETTA_INVOCATION_PREDICATE_CALL, 1u},
        {"(assertaPredicate (Predicate (p 7)))", CETTA_INVOCATION_PREDICATE_CALL, 1u},
        {"(assertzPredicate (Predicate (p 7)))", CETTA_INVOCATION_PREDICATE_CALL, 1u},
        {"(retractPredicate (Predicate (p 7)))", CETTA_INVOCATION_PREDICATE_CALL, 1u},
    };
    unsigned checks = 0u;
    for (unsigned i = 0u; i < sizeof(cases) / sizeof(*cases); i++) {
        Atom *call = fixture_atom(&source, cases[i].call);
        const PettaPlanNode *plan = petta_program_plan_current(program, call);
        /* This extension spelling needs source registration. A known body
         * layout must not itself establish that the source is a call. */
        if (i == 5u) {
            assert(plan && plan->role == PETTA_PLAN_DATA && !plan->computation.invocation.operation);
            CettaInvocationPlan layout = petta_semantics_elaborate_invocation(
                atom_symbol_current_id(call->expr.elems[0]), PETTA_FORM_NONE, cases[i].arity);
            assert(layout.operation == cases[i].operation);
            checks++;
            continue;
        }
        if (!plan || plan->role != PETTA_PLAN_STATIC_CALL)
            fprintf(stderr, "invocation authority: %s\n", cases[i].call);
        assert(plan && plan->role == PETTA_PLAN_STATIC_CALL);
        if (plan->computation.invocation.operation != cases[i].operation)
            fprintf(stderr, "invocation layout: %s operation=%u\n", cases[i].call, plan->computation.invocation.operation);
        assert(plan->computation.invocation.operation == cases[i].operation);
        assert(plan->computation.invocation.first == 0u && plan->computation.invocation.last == cases[i].arity);
        checks++;
    }
    const PettaPlanNode *dynamic = petta_program_plan_current(program,
        fixture_atom(&source, "($head (7))"));
    const PettaPlanNode *quoted = petta_program_plan_current(program,
        fixture_atom(&source, "(quote (car-atom (7)))"));
    assert(dynamic && dynamic->role == PETTA_PLAN_DYNAMIC_CALL && !dynamic->computation.invocation.operation);
    assert(quoted && quoted->output == PETTA_PLAN_OUTPUT_QUOTED_CHILD);
    Atom *call = fixture_atom(&source, "(repr \"(7 8)\")");
    const PettaPlanNode *plan = petta_program_plan_current(program, call);
    assert(plan && plan->computation.invocation.operation == CETTA_INVOCATION_HOST);
    PettaMachineHost host = {.prepare_resolved_call = invocation_rewrite,
        .forms_are_written_syntax = true};
    PettaMachine machine;
    assert(petta_machine_init_with_plan(&machine, &space, &source, call, plan, NULL, &host));
    Atom *answer = NULL;
    Bindings environment;
    assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_ANSWER);
    assert(atom_eq(answer, fixture_atom(&source, "(7 8)")));
    bindings_free(&environment);
    assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);
    /* Reordering physical operands changes their coordinates, not the
     * retained computation. Rereading the wrapper would select 1 here. */
    Atom *branch = fixture_atom(&source, "(if True 1 2)");
    const PettaPlanNode *branch_source = petta_program_plan_current(program, branch);
    assert(branch_source && branch_source->computation.control.operation == CETTA_CONTROL_BRANCH);
    PettaPlanNode normalized = *branch_source;
    normalized.computation.control.first = 2u;
    normalized.computation.control.last = 1u;
    PettaMachineHost ordinary = {.forms_are_written_syntax = true};
    assert(petta_machine_init_with_plan(&machine, &space, &source, branch,
        &normalized, NULL, &ordinary));
    assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_ANSWER);
    assert(atom_eq(answer, atom_int(&source, 2)));
    bindings_free(&environment);
    assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);
    /* An adapter with no elaborated operation must use current source
     * decoding, rather than treat a missing descriptor as failure. */
    normalized.computation.control = (CettaControlOperationPlan){0};
    assert(petta_machine_init_with_plan(&machine, &space, &source, branch,
        &normalized, NULL, &ordinary));
    assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_ANSWER);
    assert(atom_eq(answer, atom_int(&source, 1)));
    bindings_free(&environment);
    petta_machine_destroy(&machine);
    /* A lowered ordinary invocation owns its operation even when the
     * operand carrier still has a special-form spelling. It has no native
     * descriptor. Freshly decoding that spelling would choose a branch. */
    normalized.computation.control = CETTA_CONTROL_INVOKE_PLAN;
    assert(normalized.computation.invocation.operation == CETTA_INVOCATION_NONE);
    assert(petta_machine_init_with_plan(&machine, &space, &source, branch,
        &normalized, NULL, &ordinary));
    assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_ANSWER);
    assert(atom_eq(answer, branch));
    bindings_free(&environment);
    assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);
    /* Generated ordinary calls need not specialize their native body.
     * Callee entry still selects it without decoding a new control form. */
    const struct { const char *call; const char *answer; } generic_native[] = {
        {"(car-atom (7 8))", "7"},
        {"(list_to_set (7 7 8))", "(7 8)"},
    };
    for (size_t i = 0u; i < sizeof(generic_native) / sizeof(*generic_native); i++) {
        Atom *application = fixture_atom(&source, generic_native[i].call);
        const PettaPlanNode *application_source = petta_program_plan_current(
            program, application);
        assert(application_source && application_source->role == PETTA_PLAN_STATIC_CALL);
        PettaPlanNode generic = *application_source;
        generic.computation.invocation = (CettaInvocationPlan){0};
        assert(generic.computation.control.operation == CETTA_CONTROL_INVOKE);
        assert(petta_machine_init_with_plan(&machine, &space, &source, application,
            &generic, NULL, &ordinary));
        assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_ANSWER);
        assert(atom_eq(answer, fixture_atom(&source, generic_native[i].answer)));
        bindings_free(&environment);
        assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_EXHAUSTED);
        bindings_free(&environment);
        petta_machine_destroy(&machine);
    }
    Atom *rewritten = fixture_atom(&source, "(if False 7 9)");
    const PettaPlanNode *rewritten_plan = petta_program_plan_current(program, rewritten);
    host.context = program;
    assert(petta_machine_init_with_plan(&machine, &space, &source, rewritten,
        rewritten_plan, NULL, &host));
    assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);
    Atom *authored = fixture_atom(&source,
        "(if (superpose (True False True)) (quote (+ 1 2)) (empty))");
    Atom *reordered = fixture_atom(&source,
        "(if (superpose (True False True)) (empty) (quote (+ 1 2)))");
    const PettaPlanNode *authored_plan = petta_program_plan_current(program, authored);
    PettaPlanNode reordered_children[4];
    memcpy(reordered_children, authored_plan->children, sizeof(reordered_children));
    reordered_children[2] = authored_plan->children[3];
    reordered_children[3] = authored_plan->children[2];
    PettaPlanNode reordered_plan = *authored_plan;
    reordered_plan.computation.control.first = 2u;
    reordered_plan.computation.control.last = 1u;
    reordered_plan.children = reordered_children;
    assert(petta_machine_init_with_plan(&machine, &space, &source, reordered,
        &reordered_plan, NULL, &ordinary));
    for (unsigned i = 0u; i < 2u; i++) {
        assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_ANSWER);
        assert(atom_eq(answer, fixture_atom(&source, "(+ 1 2)")));
        bindings_free(&environment);
    }
    assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);
    Atom *series = fixture_atom(&source, "(progn (quote (+ 1 2)) (quote ignored))");
    const PettaPlanNode *series_source = petta_program_plan_current(program, series);
    PettaPlanNode series_plan = *series_source;
    series_plan.computation.control.operation = CETTA_CONTROL_SEQUENCE_FIRST;
    series_plan.output_child = 1u;
    assert(petta_machine_init_with_plan(&machine, &space, &source, series,
        &series_plan, NULL, &ordinary));
    assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_ANSWER);
    assert(atom_eq(answer, fixture_atom(&source, "(+ 1 2)")));
    bindings_free(&environment);
    assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);
    petta_machine_destroy(&machine);
    petta_program_free(program);
    space_free(&space);
    arena_free(&source);
    return checks + 11u;
}

typedef struct {
    Space *space;
    SymbolId head;
    const PettaPlanNode *body;
} PreparedPlanSource;

static bool prepared_plan_equation_source(void *opaque, Space *space,
        SymbolId head, size_t ordinal, SpaceEquationOccurrenceId occurrence,
        const Atom *equation, const void **body) {
    (void)occurrence;
    PreparedPlanSource *context = opaque;
    if (space != context->space || head != context->head)
        return false;
    assert(ordinal == 0u && equation && equation->kind == ATOM_EXPR &&
        equation->expr.len == 3u);
    *body = context->body;
    return true;
}

static bool prepared_plan_observation(Space *space, Arena *source,
        Atom *expression, const PettaPlanNode *plan, bool metadata,
        CettaGsltPureCallMode mode, Atom *expected, bool callee) {
    PreparedPlanSource context = {.space = space, .body = plan};
    CettaPreparedPureSourceView view = {
        .context = &context,
        .root = plan,
        .role = petta_prepared_pure_source_role,
        .computation = metadata ? petta_prepared_pure_source_computation : NULL,
        .child = petta_prepared_pure_source_child,
        .equation_rhs = prepared_plan_equation_source,
    };
    /* A closed caller enters the callee whose body contains the local
     * bindings. The closed-entry API deliberately refuses free variables. */
    if (callee || atom_has_vars(expression)) {
        static unsigned serial;
        char name[64];
        snprintf(name, sizeof(name), "prepared-plan-body-%u", ++serial);
        Atom *head = atom_symbol(source, name);
        context.head = atom_symbol_current_id(head);
        Atom *call = atom_expr(source, &head, 1u);
        Atom *fields[] = {atom_symbol(source, "="), call, expression};
        assert(space_admit_atom(space, source, atom_expr(source, fields, 3u)));
        expression = call;
        view.root = NULL;
    }
    CettaPreparedPureProgram *program = cetta_prepared_pure_program_compile_closed(
        space, expression, mode, atom_bool, atom_expr,
        cetta_prepared_pure_constructs_expression_always,
        NULL, NULL, petta_prepared_pure_expression_view, NULL, &view, false, true,
        (CettaMatchDecisionSemanticIdentity){
            .compiler_identity = cetta_match_decision_compiler_identity()}, NULL);
    if (!expected) {
        if (program) cetta_prepared_pure_program_free(program);
        return !program;
    }
    if (!program) {
        fprintf(stderr, "prepared computation refused %s\n",
            atom_to_string(source, expression));
        return false;
    }
    CettaCallOutcome returned = cetta_call_failure();
    bool ran = cetta_prepared_pure_program_execute_closed_controlled(
        program, source, 65536u, NULL, NULL, &returned);
    bool correct = ran && returned.kind == CETTA_CALL_VALUE &&
        returned.term && atom_eq(returned.term, expected);
    if (metadata && plan->computation.control.operation == CETTA_CONTROL_RETURN_VALUE)
        correct = correct && returned.result_form == CETTA_CALL_RESULT_COMPLETED_VALUE;
    if (!correct)
        fprintf(stderr, "prepared computation mode=%u source=%s expected=%s returned=%s kind=%u\n",
            mode, atom_to_string(source, expression), atom_to_string(source, expected),
            returned.term ? atom_to_string(source, returned.term) : "<none>", returned.kind);
    cetta_prepared_pure_program_free(program);
    return correct;
}

typedef struct {
    uint64_t remaining, quantum, spent;
    CettaSourceStep blocked;
    bool is_blocked;
    CettaSourcePhase phases[128];
    size_t length;
} GenericProbe;

static bool generic_permit(void *opaque, const CettaSourceStep *step) {
    GenericProbe *probe = opaque;
    assert(step && step->machine_instance && step->debit == 1u);
    if (probe->is_blocked)
        assert(probe->blocked.machine_instance == step->machine_instance &&
            probe->blocked.occurrence_instance == step->occurrence_instance &&
            probe->blocked.phase == step->phase && probe->blocked.debit == step->debit);
    if (!probe->quantum || !probe->remaining) {
        probe->blocked = *step;
        probe->is_blocked = true;
        return false;
    }
    probe->is_blocked = false;
    probe->remaining--;
    probe->quantum--;
    probe->spent++;
    assert(probe->length < 128u);
    probe->phases[probe->length++] = step->phase;
    return true;
}

static unsigned generic_budget_checks(void) {
    const char *queries[] = {
        "(list_to_set (superpose ((1 1) (2 2) (1 1))))",
        "(list_to_set (quote ((+ 1 2) (+ 1 2))))",
        "(car-atom ())",
        "(decons-atom ())",
    };
    const char *answers[4][3] = {{"(1)", "(2)", "(1)"}, {"((+ 1 2))"}, {"()"}, {NULL}};
    const size_t counts[] = {3u, 1u, 1u, 0u};
    const uint64_t budgets[] = {0u, 1u, 3u, 64u, 1024u};
    unsigned checks = 0u;
    for (size_t query = 0u; query < 4u; query++) {
        CettaSourcePhase reference[128];
        size_t reference_len = 0u;
        for (unsigned generic = 0u; generic < 2u; generic++)
        for (size_t purse_index = 5u; purse_index-- > 0u;)
        for (size_t quantum_index = 5u; quantum_index-- > 0u;) {
            Arena arena; arena_init(&arena);
            TermUniverse universe; term_universe_init(&universe);
            term_universe_set_persistent_arena(&universe, &arena);
            Space space; space_init_with_universe(&space, &universe);
            PettaProgram *program = petta_program_new(); assert(program);
            Atom *call = fixture_atom(&arena, queries[query]);
            const PettaPlanNode *authored = petta_program_plan_current(program, call);
            assert(authored && authored->role == PETTA_PLAN_STATIC_CALL);
            PettaPlanNode plan = *authored;
            if (generic) plan.computation.invocation = (CettaInvocationPlan){0};
            GenericProbe probe = {.remaining = budgets[purse_index],
                                  .quantum = budgets[quantum_index]};
            PettaMachineHost host = {.context = &probe, .permit_transition = generic_permit,
                                     .forms_are_written_syntax = true};
            PettaMachine machine;
            assert(petta_machine_init_with_plan(&machine, &space, &arena, call, &plan, NULL, &host));
            size_t count = 0u;
            bool complete = false;
            for (unsigned attempts = 0u; ; attempts++) {
                assert(attempts < 1024u);
                Atom *answer = NULL; Bindings environment; bindings_init(&environment);
                PettaMachineStep step = petta_machine_next(&machine, &answer, &environment);
                if (step == PETTA_MACHINE_STEP_SUSPENDED) {
                    bindings_free(&environment);
                    if (!probe.remaining) {
                        uint64_t spent = probe.spent;
                        assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_SUSPENDED);
                        bindings_free(&environment);
                        assert(probe.spent == spent);
                        break;
                    }
                    probe.quantum = budgets[quantum_index] ? budgets[quantum_index] : 1u;
                    continue;
                }
                if (step == PETTA_MACHINE_STEP_EXHAUSTED) {
                    bindings_free(&environment); complete = true; break;
                }
                assert(step == PETTA_MACHINE_STEP_ANSWER && count < counts[query]);
                assert(atom_eq(answer, fixture_atom(&arena, answers[query][count])));
                count++; bindings_free(&environment);
            }
            if (purse_index == 4u && quantum_index == 4u && !generic) {
                assert(complete && count == counts[query]);
                reference_len = probe.length;
                memcpy(reference, probe.phases, sizeof(reference));
            }
            assert(probe.length <= reference_len);
            assert(memcmp(reference, probe.phases, probe.length * sizeof(*reference)) == 0);
            if (complete) assert(count == counts[query] && probe.length == reference_len);
            assert(probe.spent + probe.remaining == budgets[purse_index]);
            petta_machine_destroy(&machine); petta_program_free(program);
            space_free(&space); term_universe_free(&universe); arena_free(&arena);
            checks++;
        }
    }
    return checks;
}


static unsigned source_collection_plans(void) {
    Arena arena;
    arena_init(&arena);
    Space space;
    space_init(&space);
    PettaProgram *owner = petta_program_new();
    assert(owner);
    const char *bodies[] = {"(superpose (7 7))",
        "(superpose ((quote (empty)) (quote (empty))))"};
    const char *expected[] = {"(7 7)", "((empty) (empty))"};
    const char *names[] = {"collection-word-scalars", "collection-word-data"};
    const uint64_t budgets[] = {0u, 1u, 4u, 1024u};
    unsigned checks = 0u;
    for (unsigned fixture = 0u; fixture < 2u; fixture++) {
        Atom *body = fixture_atom(&arena, bodies[fixture]);
        Atom *source = atom_expr2(&arena, atom_symbol(&arena, "collapse"), body);
        const PettaPlanNode *authored = petta_program_plan_current(owner, source);
        assert(authored && authored->computation.control.operation == CETTA_CONTROL_COLLECT);
        PettaPlanNode ignored = *petta_program_plan_current(owner, fixture_atom(&arena, "(empty)"));
        ignored.source_position = 0u;
        PettaPlanNode children[] = {*petta_plan_child(authored, 0u), ignored,
            *petta_plan_child(authored, 1u)};
        PettaPlanNode normalized = *authored;
        normalized.children = children;
        normalized.child_count = 3u;
        normalized.computation.control.source = 1u;
        Atom *carrier = atom_expr3(&arena, atom_symbol(&arena, "collection-carrier"),
            fixture_atom(&arena, "(empty)"), body);
        Atom *count = atom_expr2(&arena, atom_symbol(&arena, "size-atom"), carrier);
        Atom *original_count = atom_expr2(&arena, atom_symbol(&arena, "size-atom"), source);
        PettaPlanNode counted = *petta_program_plan_current(owner, original_count);
        PettaPlanNode count_children[] = {*petta_plan_child(&counted, 0u), normalized};
        counted.children = count_children;
        for (unsigned route = 0u; route < 2u; route++) {
            CettaSourcePhase reference[128];
            size_t reference_len = 0u;
            for (unsigned purse = 4u; purse-- > 0u;)
            for (unsigned quantum = 4u; quantum-- > 0u;) {
                GenericProbe probe = {.remaining = budgets[purse], .quantum = budgets[quantum]};
                PettaMachineHost host = {.context = &probe, .permit_transition = generic_permit,
                    .forms_are_written_syntax = true};
                PettaMachine machine;
                assert(petta_machine_init_with_plan(&machine, &space, &arena,
                    route ? count : carrier, route ? &counted : &normalized, NULL, &host));
                unsigned answers = 0u;
                bool complete = false;
                for (unsigned attempt = 0u;; attempt++) {
                    assert(attempt < 128u);
                    Atom *answer = NULL;
                    Bindings environment;
                    PettaMachineStep step = petta_machine_next(&machine, &answer, &environment);
                    bindings_free(&environment);
                    if (step == PETTA_MACHINE_STEP_SUSPENDED) {
                        if (!probe.remaining)
                            break;
                        probe.quantum = budgets[quantum] ? budgets[quantum] : 1u;
                        continue;
                    }
                    if (step == PETTA_MACHINE_STEP_EXHAUSTED) {
                        complete = true;
                        break;
                    }
                    if (step != PETTA_MACHINE_STEP_ANSWER || answers != 0u)
                        fprintf(stderr, "collection plan: fixture=%u route=%u purse=%u quantum=%u step=%u answers=%u\n",
                            fixture, route, purse, quantum, (unsigned)step, answers);
                    assert(step == PETTA_MACHINE_STEP_ANSWER && ++answers == 1u);
                    assert(atom_eq(answer, route ? atom_int(&arena, 2) : fixture_atom(&arena, expected[fixture])));
                }
                if (purse == 3u && quantum == 3u) {
                    assert(complete && answers == 1u);
                    reference_len = probe.length;
                    memcpy(reference, probe.phases, sizeof(reference));
                }
                assert(probe.length <= reference_len &&
                    memcmp(reference, probe.phases, probe.length * sizeof(*reference)) == 0);
                if (complete)
                    assert(answers == 1u && probe.length == reference_len);
                assert(probe.spent + probe.remaining == budgets[purse]);
                petta_machine_destroy(&machine);
                checks++;
            }
        }
        for (unsigned route = 0u; route < 2u; route++) {
            PettaMachineHost host = {.forms_are_written_syntax = true};
            PettaMachine machine;
            PettaPlanNode held = normalized;
            held.role = PETTA_PLAN_VALUE;
            held.computation.control = CETTA_CONTROL_RETURN_VALUE_PLAN;
            PettaPlanNode held_children[] = {count_children[0], held};
            PettaPlanNode held_count = counted;
            held_count.children = held_children;
            assert(petta_machine_init_with_plan(&machine, &space, &arena,
                route ? count : carrier, route ? &held_count : &held, NULL, &host));
            Atom *answer = NULL;
            Bindings environment;
            assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_ANSWER);
            assert(atom_eq(answer, route ? atom_int(&arena, 3) : carrier));
            bindings_free(&environment);
            petta_machine_destroy(&machine);
            PettaPlanNode invalid = normalized;
            invalid.computation.control.source = CETTA_CONTROL_OPERAND_ABSENT;
            PettaPlanNode invalid_children[] = {count_children[0], invalid};
            PettaPlanNode invalid_count = counted;
            invalid_count.children = invalid_children;
            assert(petta_machine_init_with_plan(&machine, &space, &arena,
                route ? count : carrier, route ? &invalid_count : &invalid, NULL, &host));
            assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_DECLINED);
            bindings_free(&environment);
            petta_machine_destroy(&machine);
            checks += 2u;
        }
        Atom *parameter = fixture_atom(&arena, "$completed");
        Atom *lhs = fixture
            ? atom_expr2(&arena, atom_symbol(&arena, names[fixture]), parameter)
            : atom_expr(&arena, (Atom *[]){atom_symbol(&arena, names[fixture])}, 1u);
        Atom *oem_body = fixture ? atom_expr2(&arena, atom_symbol(&arena, "superpose"),
            atom_expr2(&arena, parameter, parameter)) : body;
        Atom *oem_source = atom_expr2(&arena, atom_symbol(&arena, "collapse"), oem_body);
        Atom *oem_carrier = atom_expr3(&arena, atom_symbol(&arena, "collection-carrier"),
            fixture_atom(&arena, "(empty)"), oem_body);
        Atom *equation = atom_expr3(&arena, atom_symbol(&arena, "="), lhs, oem_source);
        const PettaPlanNode *equation_source = petta_program_plan_dynamic_add(owner, equation);
        assert(equation_source);
        PettaPlanNode rhs = *petta_plan_child(equation_source, 2u);
        PettaPlanNode rhs_children[] = {*petta_plan_child(&rhs, 0u), ignored,
            *petta_plan_child(&rhs, 1u)};
        rhs.children = rhs_children;
        rhs.child_count = 3u;
        rhs.computation.control.source = 1u;
        PettaPlanNode normalized_equation = *equation_source;
        PettaPlanNode equation_children[] = {*petta_plan_child(equation_source, 0u),
            *petta_plan_child(equation_source, 1u), rhs};
        normalized_equation.children = equation_children;
        equation = atom_expr3(&arena, atom_symbol(&arena, "="), lhs, oem_carrier);
        CettaCount index = space_length64(&space);
        space_add(&space, equation);
        assert(petta_program_note_add(owner, &space, space_get_at64(&space, index), &normalized_equation));
        const char *reason = NULL;
        CettaOpenEquationProgram *program = cetta_open_equation_program_compile(
            owner, &space, atom_symbol_current_id(lhs->expr.elems[0]), fixture ? 1u : 0u, NULL, &reason);
        if (!program)
            fprintf(stderr, "collection word OEM: %s\n", reason);
        assert(program);
        Atom *arguments[] = {fixture_atom(&arena, "(empty)")};
        CettaOpenEquationCursor *cursor = cetta_open_equation_cursor_open(
            program, &arena, fixture ? arguments : NULL, fixture ? 1u : 0u, NULL, NULL, 0u, NULL);
        assert(cursor);
        CettaCallOutcome returned = {0};
        Atom **values = NULL;
        CettaOpenEquationStep next = cetta_open_equation_cursor_next(cursor, NULL, &returned, &values);
        if (next != CETTA_OPEN_EQUATION_ANSWER)
            fprintf(stderr, "collection word OEM: fixture=%u step=%u handoff=%u\n", fixture,
                (unsigned)next, (unsigned)cetta_open_equation_cursor_handoff(cursor));
        assert(next == CETTA_OPEN_EQUATION_ANSWER);
        if (returned.kind != CETTA_CALL_VALUE || returned.result_form != CETTA_CALL_RESULT_COMPLETED_VALUE ||
            !atom_eq(returned.term, fixture_atom(&arena, expected[fixture]))) {
            fprintf(stderr, "collection word OEM: fixture=%u kind=%u form=%u value=", fixture,
                (unsigned)returned.kind, (unsigned)returned.result_form);
            atom_print(returned.term, stderr);
            fputc('\n', stderr);
        }
        assert(returned.kind == CETTA_CALL_VALUE && returned.result_form == CETTA_CALL_RESULT_COMPLETED_VALUE &&
            atom_eq(returned.term, fixture_atom(&arena, expected[fixture])));
        assert(cetta_open_equation_cursor_next(cursor, NULL, &returned, &values) == CETTA_OPEN_EQUATION_EXHAUSTED);
        cetta_open_equation_cursor_close(cursor);
        cetta_open_equation_program_release(program);
        checks++;
    }
    petta_program_free(owner);
    space_free(&space);
    arena_free(&arena);
    return checks;
}

static unsigned prepared_computation_plans(void) {
    Arena source; arena_init(&source);
    TermUniverse universe; term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &source);
    Space space; space_init_with_universe(&space, &universe);
    PettaProgram *owner = petta_program_new(); assert(owner);
    unsigned checks = 0u;
    for (unsigned need = 0u; need <= 1u; need++) {
        CettaGsltPureCallMode mode = need ? CETTA_GSLT_PURE_CALL_CALL_BY_NEED
                                        : CETTA_GSLT_PURE_CALL_EAGER;
        Atom *branch = fixture_atom(&source, "(if True 1 2)");
        PettaPlanNode plan = *petta_program_plan_current(owner, branch);
        plan.computation.control.first = 2u;
        plan.computation.control.last = 1u;
        assert(prepared_plan_observation(&space, &source, branch, &plan, true,
            mode, atom_int(&source, 2), false));
        assert(prepared_plan_observation(&space, &source, branch, &plan, false,
            mode, atom_int(&source, 1), false));
        plan.computation.control.source = 1u;
        plan.computation.control.first = 2u;
        plan.computation.control.last = 0u;
        Atom *carrier = fixture_atom(&source, "(normalized-if 9 True 7)");
        assert(prepared_plan_observation(&space, &source, carrier, &plan, true,
            mode, atom_int(&source, 7), false));
        assert(prepared_plan_observation(&space, &source, carrier, &plan, false,
            mode, carrier, false));
        plan.computation.control.source = CETTA_CONTROL_OPERAND_ABSENT;
        assert(prepared_plan_observation(&space, &source, branch, &plan, true,
            mode, NULL, false));

        Atom *binding = fixture_atom(&source, "(let $v 7 (+ $v 1))");
        const PettaPlanNode *authored = petta_program_plan_current(owner, binding);
        PettaPlanNode binding_children[4] = {authored->children[0],
            authored->children[3], authored->children[2], authored->children[1]};
        plan = *authored;
        plan.children = binding_children;
        plan.computation.control.first = 2u;
        plan.computation.control.last = 0u;
        carrier = fixture_atom(&source, "(normalized-bind (+ $v 1) 7 $v)");
        assert(prepared_plan_observation(&space, &source, carrier, &plan, true,
            mode, atom_int(&source, 8), false));

        binding = fixture_atom(&source, "(let* (($v 7) ($w (+ $v 1))) (+ $v $w))");
        authored = petta_program_plan_current(owner, binding);
        PettaPlanNode series_children[3] = {authored->children[0],
            authored->children[2], authored->children[1]};
        plan = *authored;
        plan.children = series_children;
        plan.computation.control.source = 1u;
        plan.computation.control.last = 0u;
        carrier = fixture_atom(&source,
            "(normalized-bindings (+ $v $w) (($v 7) ($w (+ $v 1))))");
        assert(prepared_plan_observation(&space, &source, carrier, &plan, true,
            mode, atom_int(&source, 15), false));

        Atom *code = fixture_atom(&source, "(+ 1 2)");
        plan = *petta_program_plan_current(owner, code);
        plan.computation.control = CETTA_CONTROL_RETURN_VALUE_PLAN;
        assert(prepared_plan_observation(&space, &source, code, &plan, true,
            mode, code, false));
        assert(prepared_plan_observation(&space, &source, code, &plan, false,
            mode, atom_int(&source, 3), false));
        Atom *guarded = fixture_atom(&source, "(if (< 1 2) 7 (empty))");
        authored = petta_program_plan_current(owner, guarded);
        PettaPlanNode guarded_children[4] = {authored->children[0],
            authored->children[3], authored->children[2], authored->children[1]};
        plan = *authored;
        plan.children = guarded_children;
        plan.computation.control.source = 2u;
        plan.computation.control.first = 1u;
        plan.computation.control.last = 0u;
        carrier = fixture_atom(&source, "(normalized-guard (empty) 7 (< 1 2))");
        assert(prepared_plan_observation(&space, &source, carrier, &plan, true,
            mode, atom_int(&source, 7), true));

        Atom *forced = fixture_atom(&source, "(eval (+ 1 2))");
        plan = *petta_program_plan_current(owner, forced);
        carrier = fixture_atom(&source, "(stage-carrier (+ 1 2))");
        assert(prepared_plan_observation(&space, &source, carrier, &plan, true,
            mode, atom_int(&source, 3), false));
        assert(prepared_plan_observation(&space, &source, carrier, &plan, false,
            mode, fixture_atom(&source, "(stage-carrier 3)"), false));
        plan.computation.control = CETTA_CONTROL_INVOKE_PLAN;
        assert(prepared_plan_observation(&space, &source, forced, &plan, true,
            mode, NULL, false));

        branch = fixture_atom(&source, "(if True (+ 1 2) wrong)");
        authored = petta_program_plan_current(owner, branch);
        PettaPlanNode held_children[4];
        memcpy(held_children, authored->children, sizeof(held_children));
        held_children[2].role = PETTA_PLAN_VALUE;
        held_children[2].computation.control = CETTA_CONTROL_RETURN_VALUE_PLAN;
        plan = *authored;
        plan.children = held_children;
        assert(prepared_plan_observation(&space, &source, branch, &plan, true,
            mode, fixture_atom(&source, "(+ 1 2)"), false));

        code = fixture_atom(&source, "(id (7 8))");
        plan = *petta_program_plan_current(owner, code);
        plan.computation.control = CETTA_CONTROL_RETURN_VALUE_PLAN;
        assert(prepared_plan_observation(&space, &source, code, &plan, true,
            mode, code, false));
        assert(prepared_plan_observation(&space, &source, code, &plan, false,
            mode, fixture_atom(&source, "(7 8)"), false));
        checks += 16u;
    }
    petta_program_free(owner); space_free(&space);
    term_universe_free(&universe); arena_free(&source);
    return checks;
}

static unsigned ordinary_invocation_consumers(void) {
    Arena source;
    arena_init(&source);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &source);
    Space space;
    space_init_with_universe(&space, &universe);
    PettaProgram *program = petta_program_new();
    assert(program);
    Atom *equation = fixture_atom(&source,
        "(= (invocation-bundle $a $b $c) (Bundle $a $b $c))");
    const PettaPlanNode *equation_plan = petta_program_plan_dynamic_add(program, equation);
    space_add(&space, equation);
    assert(petta_program_note_add(program, &space, space_get_at64(&space, 0u), equation_plan));
    Atom *head = atom_symbol(&source, "invocation-bundle");
    Atom *values[] = {fixture_atom(&source, "(+ 1 2)"),
                      atom_int(&source, 7), atom_int(&source, 9)};
    unsigned checks = 0u;
    for (unsigned captured = 0u; captured <= 3u; captured++)
    for (unsigned admit = 0u; admit < 2u; admit++)
    for (unsigned unlimited = 0u; unlimited < 2u; unlimited++) {
        Atom *partial = petta_semantics_partial_value(&source, head, values, captured);
        assert(partial);
        Atom *fields[4] = {partial};
        for (unsigned index = captured; index < 3u; index++)
            fields[index - captured + 1u] = index == 0u
                ? atom_expr2(&source, atom_symbol_id(&source, g_builtin_syms.quote), values[index])
                : values[index];
        Atom *call = atom_expr(&source, fields, 4u - captured);
        InvocationAdmission admission = {.head = atom_symbol_current_id(head), .admit = admit, .program = program};
        PettaMachineHost host = {
            .context = &admission,
            .forms_are_written_syntax = true,
            .source_output_constraints = true,
            .dispatch_recovers_branch_locally = true,
            .unlimited_transition_budget = unlimited,
            .admit_space_query_values = invocation_admit_values,
            .candidate_snapshot_lease = invocation_candidate_snapshot,
        };
        PettaMachine machine;
        const PettaPlanNode *plan = petta_program_plan_current(program, call);
        assert(plan);
        assert(petta_machine_init_with_plan(&machine, &space, &source, call, plan, NULL, &host));
        invocation_syntax_observations = 0u;
        Atom *answer = NULL;
        Bindings environment;
        PettaMachineStep step = petta_machine_next(&machine, &answer, &environment);
        if (step != PETTA_MACHINE_STEP_ANSWER)
            fprintf(stderr, "invocation capture=%u admit=%u unlimited=%u step=%u\n",
                captured, admit, unlimited, step);
        assert(step == PETTA_MACHINE_STEP_ANSWER);
        if (!atom_eq(answer, fixture_atom(&source, "(Bundle (+ 1 2) 7 9)")))
            fprintf(stderr, "invocation capture=%u admit=%u unlimited=%u answer=%s\n",
                captured, admit, unlimited, atom_to_string(&source, answer));
        assert(atom_eq(answer, fixture_atom(&source, "(Bundle (+ 1 2) 7 9)")));
        bindings_free(&environment);
        assert(admission.inquiries > 0u);
        unsigned expected = admit && unlimited ? 0u : 1u;
        if (invocation_syntax_observations != expected)
            fprintf(stderr, "invocation capture=%u admit=%u unlimited=%u syntax=%u expected=%u\n",
                captured, admit, unlimited, invocation_syntax_observations, expected);
        assert(invocation_syntax_observations == expected);
        assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_EXHAUSTED);
        bindings_free(&environment);
        petta_machine_destroy(&machine);
        checks++;
    }
    space_free(&space);
    petta_program_free(program);
    term_universe_free(&universe);
    arena_free(&source);
    return checks;
}

static unsigned prepared_worker_availability(void) {
    unsigned checks = 0u;
    PettaEvalMachineContext context = {.fuel = -1};
    for (int fuel = -1; fuel <= 1; fuel++) {
        PettaMachineHost host = petta_eval_machine_host(&context, fuel,
            false, false, false, false, false,
            petta_eval_machine_match_semantics(NULL, false));
        bool available = fuel < 0 && !prepared_pure_reference_enabled();
        assert(!!host.execute_prepared_pure_call == available);
        assert(!!host.open_answer_cursor == available);
        checks++;
    }
    CettaEvalEpisode tracker = {.budget_limited = true};
    g_eval_completion_tracker = &tracker;
    PettaMachineHost inherited = petta_eval_machine_host(&context, -1,
        false, false, false, false, false,
        petta_eval_machine_match_semantics(NULL, false));
    assert(!inherited.execute_prepared_pure_call && !inherited.open_answer_cursor);
    g_eval_completion_tracker = NULL;
    return checks + 1u;
}

static unsigned episode_accounting(void) {
    unsigned checks = 0u;
    for (unsigned budget = 0u; budget <= 8u; budget++)
    for (unsigned prefix = 0u; prefix <= budget; prefix++)
    for (int requested = -1; requested <= 10; requested++)
    for (unsigned attempts = 0u; attempts <= 12u; attempts++) {
        CettaEvalEpisode parent = cetta_eval_episode_begin(NULL, (int)budget);
        g_eval_completion_tracker = &parent;
        for (unsigned index = 0u; index < prefix; index++)
            assert(eval_completion_step());
        unsigned available = budget - prefix;
        unsigned granted = requested < 0 || (unsigned)requested > available
            ? available : (unsigned)requested;
        CettaEvalEpisode child = cetta_eval_episode_begin(&parent, requested);
        assert(child.budget_limited && child.budget_initial == granted);
        g_eval_completion_tracker = &child;
        unsigned accepted = 0u;
        for (unsigned index = 0u; index < attempts; index++)
            accepted += eval_completion_step();
        unsigned used = attempts < granted ? attempts : granted;
        assert(accepted == used && child.steps_spent == used &&
            child.budget_remaining == granted - used);
        CettaEvalCompletion completion = eval_completion_child_end(&child);
        assert(completion == (attempts > granted
            ? CETTA_EVAL_INCOMPLETE_FUEL : CETTA_EVAL_COMPLETE));
        assert(g_eval_completion_tracker == &parent && !child.parent);
        assert(parent.steps_spent == prefix + used &&
            parent.budget_remaining == budget - prefix - used);
        assert(parent.steps_spent + parent.budget_remaining == budget);
        assert(cetta_eval_episode_end(&child) == completion);
        assert(parent.steps_spent == prefix + used &&
            parent.budget_remaining == budget - prefix - used);
        g_eval_completion_tracker = NULL;
        checks++;
    }
    for (unsigned first = CETTA_EVAL_COMPLETE; first <= CETTA_EVAL_INCOMPLETE_HOST_FAILURE; first++)
    for (unsigned later = CETTA_EVAL_COMPLETE; later <= CETTA_EVAL_INCOMPLETE_HOST_FAILURE; later++) {
        CettaEvalEpisode parent = cetta_eval_episode_begin(NULL, 5);
        parent.completion = (CettaEvalCompletion)first;
        CettaEvalEpisode child = cetta_eval_episode_begin(&parent, 2);
        child.completion = (CettaEvalCompletion)later;
        child.interrupted = later == CETTA_EVAL_INCOMPLETE_CANCELLED;
        cetta_eval_episode_end(&child);
        assert(parent.completion == (CettaEvalCompletion)(first ? first : later));
        assert(parent.interrupted == (later == CETTA_EVAL_INCOMPLETE_CANCELLED));
        checks++;
    }
    CettaEvalEpisode parent = cetta_eval_episode_begin(NULL, 8);
    g_eval_completion_tracker = &parent;
    assert(eval_completion_step());
    CettaEvalEpisode child;
    eval_completion_child_begin(&child);
    assert(eval_completion_step());
    CettaEvalEpisode inner;
    eval_completion_child_begin(&inner);
    assert(eval_completion_step() && eval_completion_step());
    eval_completion_child_end(&inner);
    assert(g_eval_completion_tracker == &child && child.steps_spent == 3u);
    eval_completion_child_end(&child);
    assert(g_eval_completion_tracker == &parent && parent.steps_spent == 4u &&
        parent.budget_remaining == 4u);
    parent.interrupted = true;
    parent.completion = CETTA_EVAL_INCOMPLETE_CANCELLED;
    eval_completion_child_begin(&child);
    assert(child.interrupted && !eval_completion_step() && !child.steps_spent);
    eval_completion_child_end(&child);
    assert(parent.steps_spent == 4u && parent.budget_remaining == 4u);
    g_eval_completion_tracker = NULL;
    parent = (CettaEvalEpisode){.budget_limited = true,
        .budget_remaining = 5u, .steps_spent = UINT64_MAX - 2u};
    child = (CettaEvalEpisode){.parent = &parent, .steps_spent = 4u};
    cetta_eval_episode_end(&child);
    assert(parent.steps_spent == UINT64_MAX && parent.budget_remaining == 1u);
    assert(cetta_eval_episode_step(&parent, 1u) && parent.steps_spent == UINT64_MAX);
    assert(!cetta_eval_episode_step(&parent, 1u) && !parent.budget_remaining);
    CettaEvalEpisode unbounded = cetta_eval_episode_begin(NULL, -1);
    for (unsigned index = 0u; index < 64u; index++)
        assert(cetta_eval_episode_step(&unbounded, 1u));
    assert(!unbounded.budget_limited && !unbounded.steps_spent &&
        !unbounded.budget_remaining);
    for (unsigned credit = 0u; credit <= 8u; credit++)
    for (unsigned debit = 0u; debit <= 11u; debit++) {
        CettaEvalEpisode episode = cetta_eval_episode_begin(NULL, (int)credit);
        bool admitted = cetta_eval_episode_step(&episode, debit);
        assert(admitted == (debit <= credit));
        assert(episode.steps_spent == (admitted ? debit : 0u));
        assert(episode.budget_remaining == credit - (admitted ? debit : 0u));
        assert(episode.completion == (admitted ? CETTA_EVAL_COMPLETE : CETTA_EVAL_INCOMPLETE_FUEL));
        checks++;
    }
    CettaEvalEpisode episode = cetta_eval_episode_begin(NULL, 5);
    PettaEvalMachineContext physical = {.controller_quantum_active = true,
        .controller_quantum_limit = 0u};
    CettaSourceStep source = {.machine_instance = 1u, .occurrence_instance = 7u,
        .debit = 3u, .phase = CETTA_SOURCE_COMPUTE};
    g_eval_completion_tracker = &episode;
    assert(!petta_eval_machine_permit_controller_transition(&physical, &source));
    assert(episode.budget_remaining == 5u && !episode.steps_spent);
    physical.controller_quantum_limit = 1u;
    assert(petta_eval_machine_permit_controller_transition(&physical, &source));
    assert(episode.budget_remaining == 2u && episode.steps_spent == 3u);
    assert(physical.controller_quantum_transitions == 1u);
    assert(!petta_eval_machine_permit_controller_transition(&physical, &source));
    assert(episode.budget_remaining == 2u && episode.steps_spent == 3u);
    g_eval_completion_tracker = NULL;
    const unsigned credits[] = {8u, 1u, 0u};
    const unsigned debits[] = {0u, 1u, 3u};
    const unsigned limits[] = {0u, 1u, 4u};
    for (unsigned phase = CETTA_SOURCE_COMPUTE; phase <= CETTA_SOURCE_PUBLISH; phase++)
    for (unsigned controller = 0u; controller < 2u; controller++)
    for (unsigned limit = 0u; limit < 3u; limit++)
    for (unsigned used = 0u; used < 3u; used++)
    for (unsigned credit = 0u; credit < 3u; credit++)
    for (unsigned cost = 0u; cost < 3u; cost++) {
        uint64_t visited = used ? limits[limit] + used - 1u : 0u;
        bool physically_ready = !limits[limit] || visited < limits[limit];
        bool expected = physically_ready && debits[cost] <= credits[credit];
        CettaEvalEpisode observed = {.budget_limited = true,
            .budget_initial = credits[credit] + 5u, .budget_remaining = credits[credit],
            .steps_spent = 5u};
        PettaEvalMachineContext context = {.diagnostic_transition_limit = limits[limit],
            .diagnostic_transitions_permitted = visited,
            .controller_quantum_active = true, .controller_quantum_limit = UINT64_MAX};
        CettaSourceStep request = {.machine_instance = 1u, .occurrence_instance = 7u,
            .debit = debits[cost], .phase = (CettaSourcePhase)phase,
            .source = {.root = 11u, .position = 3u, .debit = debits[cost]}};
        g_eval_completion_tracker = &observed;
        bool permitted = controller
            ? petta_eval_machine_permit_controller_transition(&context, &request)
            : petta_eval_machine_permit_transition(&context, &request);
        assert(permitted == expected);
        assert(observed.steps_spent == 5u + (expected ? debits[cost] : 0u));
        assert(observed.budget_remaining == credits[credit] - (expected ? debits[cost] : 0u));
        assert(context.diagnostic_transitions_permitted == visited + (expected && limits[limit] ? 1u : 0u));
        assert(context.controller_quantum_transitions == (controller && expected ? 1u : 0u));
        assert(context.controller_stats.transitions == (controller && expected ? 1u : 0u));
        assert(context.diagnostic_transition_exhausted == !physically_ready);
        assert(observed.completion == (expected ? CETTA_EVAL_COMPLETE : CETTA_EVAL_INCOMPLETE_FUEL));
        g_eval_completion_tracker = NULL;
        checks++;
    }
    for (unsigned controller = 0u; controller < 2u; controller++) {
        CettaEvalEpisode cancelled = {.budget_limited = true, .budget_remaining = 8u,
            .completion = CETTA_EVAL_INCOMPLETE_CANCELLED, .interrupted = true};
        PettaEvalMachineContext context = {.diagnostic_transition_limit = 1u,
            .diagnostic_transitions_permitted = 1u};
        g_eval_completion_tracker = &cancelled;
        assert(!(controller
            ? petta_eval_machine_permit_controller_transition(&context, &source)
            : petta_eval_machine_permit_transition(&context, &source)));
        assert(cancelled.budget_remaining == 8u && !cancelled.steps_spent &&
            cancelled.completion == CETTA_EVAL_INCOMPLETE_CANCELLED &&
            !context.diagnostic_transition_exhausted);
        g_eval_completion_tracker = NULL;
        checks++;
    }
    for (unsigned mode = 0u; mode < 3u; mode++) {
        Arena arena;
        arena_init(&arena);
        Space space;
        space_init(&space);
        CettaEvalEpisode observed = cetta_eval_episode_begin(NULL, 8);
        PettaEvalMachineContext context = {
            .diagnostic_transition_limit = mode ? 0u : 1u,
            .controller_quantum_active = mode != 0u,
            .controller_quantum_limit = mode == 1u ? 1u : 64u};
        PettaMachineHost host = {.context = &context, .forms_are_written_syntax = true,
            .permit_transition = mode ? petta_eval_machine_permit_controller_transition
                                      : petta_eval_machine_permit_transition};
        PettaMachine machine;
        assert(petta_machine_init(&machine, &space, &arena, atom_int(&arena, 37), NULL, &host));
        g_eval_completion_tracker = &observed;
        unsigned answers = 0u, pauses = 0u;
        for (unsigned attempt = 0u;; attempt++) {
            assert(attempt < 16u);
            Atom *answer = NULL;
            Bindings environment;
            PettaMachineStep step = petta_machine_next(&machine, &answer, &environment);
            bindings_free(&environment);
            if (!mode) {
                assert(step == PETTA_MACHINE_STEP_SUSPENDED && !answer);
                assert(observed.steps_spent == 1u && observed.budget_remaining == 7u);
                assert(context.diagnostic_transition_exhausted);
                break;
            }
            if (step == PETTA_MACHINE_STEP_SUSPENDED) {
                assert(context.controller_quantum_expired && mode == 1u);
                assert(observed.steps_spent == context.controller_stats.transitions);
                context.controller_quantum_transitions = 0u;
                context.controller_quantum_expired = false;
                pauses++;
                continue;
            }
            if (step == PETTA_MACHINE_STEP_EXHAUSTED)
                break;
            assert(step == PETTA_MACHINE_STEP_ANSWER && answer &&
                answer->kind == ATOM_GROUNDED && answer->ground.ival == 37);
            answers++;
        }
        if (mode) {
            assert(answers == 1u && observed.completion == CETTA_EVAL_COMPLETE);
            assert(observed.steps_spent == 3u && observed.budget_remaining == 5u);
            assert(pauses == (mode == 1u ? 2u : 0u));
        }
        g_eval_completion_tracker = NULL;
        petta_machine_destroy(&machine);
        space_free(&space);
        arena_free(&arena);
        checks++;
    }
    for (unsigned inherited = 0u; inherited < 2u; inherited++)
    for (int request = -1; request <= 2; request++)
    for (unsigned diagnostic = 0u; diagnostic < 2u; diagnostic++) {
        CettaEvalEpisode parent = cetta_eval_episode_begin(NULL, inherited ? 5 : -1);
        PettaEvalMachineContext context = {.diagnostic_transition_limit = diagnostic};
        g_eval_completion_tracker = &parent;
        PettaMachineHost host = petta_eval_machine_host(&context, request,
            false, false, false, true, false, (CettaMatchDecisionSemanticIdentity){0});
        assert(cetta_eval_episode_allows_unmetered(&parent, request) ==
            (!inherited && request < 0));
        assert(host.unlimited_transition_budget ==
            (!inherited && request < 0 && !diagnostic));
        g_eval_completion_tracker = NULL;
        checks++;
    }
    CettaEvalEpisode empty = cetta_eval_episode_begin(NULL, 0);
    for (int request = -1; request <= 2; request++) {
        assert(cetta_eval_episode_allows_unmetered(NULL, request) == (request < 0));
        assert(!cetta_eval_episode_allows_unmetered(&empty, request));
        checks += 2u;
    }
    return checks + 7u;
}

static unsigned evaluator_episode_consumers(void) {
    unsigned checks = 0u;
    const CettaLanguageId languages[] = {
        CETTA_LANGUAGE_PETTA, CETTA_LANGUAGE_HE, CETTA_LANGUAGE_PRIME,
    };
    for (size_t language = 0u; language < sizeof(languages) / sizeof(*languages); language++) {
        CettaLibraryContext *library = calloc(1u, sizeof(*library));
        assert(library);
        cetta_library_context_init_for_language_profile(library, languages[language], NULL);
        eval_set_library_context(library);
        Arena arena;
        arena_init(&arena);
        TermUniverse universe;
        term_universe_init(&universe);
        term_universe_set_persistent_arena(&universe, &arena);
        Space space;
        space_init_with_universe(&space, &universe);
        for (unsigned budget = 0u; budget <= 12u; budget++)
        for (int requested = -1; requested <= 14; requested++) {
            CettaEvalEpisode parent = cetta_eval_episode_begin(NULL, (int)budget);
            g_eval_completion_tracker = &parent;
            EvalOutcome observed;
            eval_outcome_init(&observed);
            metta_eval_outcome(&space, &arena, NULL, atom_int(&arena, 7), requested, &observed);
            unsigned granted = requested < 0 || (unsigned)requested > budget
                ? budget : (unsigned)requested;
            assert(g_eval_completion_tracker == &parent && observed.budget_limited &&
                observed.budget_initial == granted && observed.steps_spent <= granted);
            assert(parent.steps_spent == observed.steps_spent &&
                parent.budget_remaining + parent.steps_spent == budget);
            if (!granted)
                assert(!observed.results.len && observed.completion == CETTA_EVAL_INCOMPLETE_FUEL);
            if (granted >= 8u)
                assert(observed.completion == CETTA_EVAL_COMPLETE && observed.results.len == 1u);
            for (CettaCount index = 0u; index < observed.results.len; index++)
                assert(atom_eq(observed.results.items[index], atom_int(&arena, 7)));
            eval_outcome_free(&observed);
            g_eval_completion_tracker = NULL;
            checks++;
        }
        if (languages[language] != CETTA_LANGUAGE_PRIME) {
            prepared_fixture = true;
            prepared_mode = 0u;
            for (unsigned budget = 0u; budget <= 64u; budget++) {
                CettaEvalEpisode parent = cetta_eval_episode_begin(NULL, (int)budget);
                g_eval_completion_tracker = &parent;
                prepared_effects = 0u;
                EvalOutcome observed;
                eval_outcome_init(&observed);
                metta_eval_outcome(&space, &arena, NULL,
                    fixture_atom(&arena, "(+ (readln!) (readln!))"), -1, &observed);
                assert(parent.steps_spent == observed.steps_spent &&
                    parent.steps_spent + parent.budget_remaining == budget);
                assert(prepared_effects <= 2u);
                if (!budget)
                    assert(!prepared_effects && !observed.results.len);
                if (budget == 64u)
                    assert(prepared_effects == 2u && observed.completion == CETTA_EVAL_COMPLETE &&
                        observed.results.len == 1u &&
                        atom_eq(observed.results.items[0], atom_int(&arena, 10)));
                uint64_t spent_before = parent.steps_spent;
                uint64_t remaining_before = parent.budget_remaining;
                unsigned effects_before = prepared_effects;
                eval_outcome_free(&observed);
                eval_outcome_init(&observed);
                metta_eval_outcome(&space, &arena, NULL,
                    fixture_atom(&arena, "(readln!)"), -1, &observed);
                assert(parent.steps_spent == spent_before + observed.steps_spent &&
                    parent.budget_remaining + parent.steps_spent == budget);
                if (!remaining_before)
                    assert(prepared_effects == effects_before && !observed.results.len);
                eval_outcome_free(&observed);
                g_eval_completion_tracker = NULL;
                checks++;
            }
            prepared_fixture = false;
        }
        space_free(&space);
        term_universe_free(&universe);
        eval_set_library_context(NULL);
        cetta_library_context_free(library);
        free(library);
        arena_free(&arena);
    }
    return checks;
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    VarInternTable variables;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols;
    g_var_intern = &variables;
    bool availability_only = argc == 2 && strcmp(argv[1], "--worker-availability") == 0;
    bool episode_only = argc == 2 && strcmp(argv[1], "--episode-contract") == 0;
    assert(argc == 1 || availability_only || episode_only);
    unsigned checks = prepared_worker_availability();
    if (episode_only)
        checks += episode_accounting() + evaluator_episode_consumers();
    else if (!availability_only)
        checks += episode_accounting() + evaluator_episode_consumers() + source_computation_plans() + cold_function_type_cache() + source_invocation_plans() + prepared_computation_plans() + generic_budget_checks() + source_collection_plans() + ordinary_invocation_consumers() + shared_compilation_refusals() + closed_failure_cache() + evaluator_consumers() + projected_roots() + residual_consumers() + prepared_native_consumers() + scalar_callable_admission();
    eval_match_decision_cache_free_for_current_thread();
    eval_profiled_type_cache_free_for_current_thread();
    space_execution_analysis_cache_free_for_current_thread();
    cetta_foreign_global_shutdown();
    assert(cetta_lib_prolog_global_shutdown());
    symbol_table_free(&symbols);
    var_intern_free(&variables);
    g_symbols = NULL;
    g_var_intern = NULL;
    printf("PASS: %u conditional/context return controls, including ordinary residual bind\n", checks);
    return 0;
}
