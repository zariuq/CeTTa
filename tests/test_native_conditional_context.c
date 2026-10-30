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
static bool observe_ordered_pattern_plans;
static unsigned ordered_pattern_plan_calls;
bool __real_match_atoms_epoch_builder_planned(Atom *, Atom *,
    const CettaOpenPatternPlan *, BindingsBuilder *, Arena *, CettaFrameIdentity);
bool __wrap_match_atoms_epoch_builder_planned(Atom *left, Atom *right,
        const CettaOpenPatternPlan *plan, BindingsBuilder *builder,
        Arena *arena, CettaFrameIdentity epoch) {
    if (observe_ordered_pattern_plans && plan && plan->source == right)
        ordered_pattern_plan_calls++;
    return __real_match_atoms_epoch_builder_planned(left, right, plan, builder, arena, epoch);
}
Atom *__real_cetta_invocation_materialize(Arena *, const CettaInvocation *);
Atom *__wrap_cetta_invocation_materialize(Arena *arena, const CettaInvocation *call) {
    invocation_syntax_observations++;
    return __real_cetta_invocation_materialize(arena, call);
}

static bool refusal_cache_allocation_fail;
static unsigned refusal_cache_allocation_failures;
static SymbolId refusal_compile_head = SYMBOL_ID_NONE;
static unsigned refusal_compile_calls;
static bool refusal_compile_once;
static bool refusal_compile_resource;
static _Thread_local bool refusal_program_allocation_fail;
static CettaPreparedPureCompileReport refusal_last_report;
void *__real_calloc(size_t, size_t);
void *__wrap_calloc(size_t count, size_t bytes) {
    if (refusal_program_allocation_fail && count == 1u) {
        refusal_program_allocation_fail = false;
        return NULL;
    }
    return __real_calloc(count, bytes);
}

CettaPreparedPureProgram *__real_cetta_prepared_pure_program_compile_closed(
    Space *, Atom *, CettaGsltPureCallMode, CettaPreparedPureBooleanValue,
    CettaPreparedPureConstructValue, CettaPreparedPureConstructsExpression,
    CettaPreparedPureOpaqueValue, CettaPreparedPureRegisterViewFn,
    CettaPreparedPureExpressionViewFn, CettaPreparedPurePatternViewFn,
    const CettaPreparedPureSourceView *, bool, bool,
    CettaMatchDecisionSemanticIdentity, CettaPreparedPureCompileReport *);
CettaPreparedPureProgram *__wrap_cetta_prepared_pure_program_compile_closed(
        Space *space, Atom *expression, CettaGsltPureCallMode mode,
        CettaPreparedPureBooleanValue boolean, CettaPreparedPureConstructValue construct,
        CettaPreparedPureConstructsExpression constructs,
        CettaPreparedPureOpaqueValue opaque, CettaPreparedPureRegisterViewFn registers,
        CettaPreparedPureExpressionViewFn view, CettaPreparedPurePatternViewFn pattern,
        const CettaPreparedPureSourceView *source, bool values, bool equality,
        CettaMatchDecisionSemanticIdentity semantics, CettaPreparedPureCompileReport *report) {
    if (refusal_compile_head != SYMBOL_ID_NONE && expression &&
        expression->kind == ATOM_EXPR && expression->expr.len &&
        atom_is_symbol_id(expression->expr.elems[0], refusal_compile_head)) {
        refusal_compile_calls++;
        if (refusal_compile_once) {
            refusal_compile_once = false;
            if (refusal_compile_resource)
                refusal_program_allocation_fail = true;
            else {
                refusal_last_report = (CettaPreparedPureCompileReport){
                    .source = space_program_token(space),
                    .status = CETTA_PREPARED_PURE_COMPILE_UNCERTIFIED,
                };
                if (report)
                    *report = refusal_last_report;
                return NULL;
            }
        }
    }
    CettaPreparedPureProgram *program = __real_cetta_prepared_pure_program_compile_closed(space, expression, mode,
        boolean, construct, constructs, opaque, registers, view, pattern, source,
        values, equality, semantics, report);
    if (report && refusal_compile_head != SYMBOL_ID_NONE && expression &&
        expression->kind == ATOM_EXPR && expression->expr.len &&
        atom_is_symbol_id(expression->expr.elems[0], refusal_compile_head))
        refusal_last_report = *report;
    return program;
}

void *__real_malloc(size_t);
void *__wrap_malloc(size_t bytes) {
    if (refusal_cache_allocation_fail && bytes == sizeof(PreparedPureCacheEntry)) {
        refusal_cache_allocation_fail = false;
        refusal_cache_allocation_failures++;
        return NULL;
    }
    return __real_malloc(bytes);
}

static SymbolId refusal_effect_head = SYMBOL_ID_NONE;
static unsigned refusal_effect_calls, refusal_effect_mode;
static Arena *refusal_effect_owner;
static Atom *refusal_effect_equation;
static Atom *refusal_add_equation(Space *space, Arena *owner, const char *text) {
    size_t position = 0u;
    Atom *equation = parse_sexpr(owner, text, &position);
    PettaProgram *program = g_library_context->petta_program;
    const PettaPlanNode *plan = petta_program_plan_dynamic_add(program, equation);
    assert(plan);
    CettaCount row = space_length64(space);
    space_add(space, equation);
    Atom *stored = space_get_at64(space, row);
    assert(petta_program_note_add(program, space, stored, plan));
    return stored;
}

static void refusal_remove_equation(Space *space, Atom *equation) {
    assert(space_remove(space, equation));
    petta_program_note_remove_one(g_library_context->petta_program, space, equation);
}

CettaGsltQueryEffect __real_space_query_effect_for_head(Space *, SymbolId, bool *);
CettaGsltQueryEffect __wrap_space_query_effect_for_head(Space *space, SymbolId head,
        bool *defined) {
    if (head != refusal_effect_head)
        return __real_space_query_effect_for_head(space, head, defined);
    refusal_effect_calls++;
    if (refusal_effect_mode == 1u) {
        *defined = false;
        return CETTA_GSLT_QUERY_EFFECT_UNCERTAIN_HEAD;
    }
    CettaGsltQueryEffect result = __real_space_query_effect_for_head(space, head, defined);
    if (refusal_effect_mode == 2u) {
        refusal_effect_mode = 0u;
        assert(*defined && result != CETTA_GSLT_QUERY_EFFECT_PURE);
        refusal_remove_equation(space, refusal_effect_equation);
        refusal_add_equation(space, refusal_effect_owner,
            "(= (refusal-source $x) (+ $x 2))");
    }
    return result;
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
static bool prepared_caller_collect;
static Atom *prepared_borrowed_value;
static uint32_t prepared_borrowed_parent_depth;
static Arena *prepared_mutation_arena;
static Space *prepared_staged_space;
static const char *prepared_staged_definition;

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
    case 5u: case 13u: case 41u: *outcome = cetta_call_expression_frontier(
        atom_expr(arena, (Atom *[]){prepared_fixture_atom(arena, "(readln!)")}, 1u)); break;
    case 50u: *outcome = cetta_call_expression_frontier(atom_expr(arena,
        (Atom *[]){prepared_fixture_atom(arena, "(let $_ (readln!) (readln!))")}, 1u)); break;
    case 42u: *outcome = cetta_call_expression_frontier(atom_expr(arena,
        (Atom *[]){prepared_fixture_atom(arena, "(let $_ (cut) (readln!))")}, 1u)); break;
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
    case 29u: *outcome = cetta_call_completed_value(prepared_fixture_atom(arena, "(readln!)")); break;
    case 32u:
        *outcome = prepared_calls == 1u ? cetta_call_completed_value(value)
            : cetta_call_raised(atom_error(arena, value, atom_symbol(arena, "fixture")));
        break;
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
    case 40u: {
        Atom *first = atom_var_with_id(arena, "$native-first", fresh_var_id());
        Atom *second = atom_var_with_id(arena, "$native-second", fresh_var_id());
        *outcome = cetta_call_completed_frontier(atom_expr3(arena, first, second, atom_int(arena, 9)));
        break;
    }
    case 39u: {
        Atom *shared = atom_var_with_id(arena, "$native-code-slot", fresh_var_id());
        *outcome = cetta_call_completed_value(atom_expr3(arena, atom_symbol(arena, "Pair"),
            shared, prepared_fixture_atom(arena, "(function (return (readln!)))")));
        break;
    }
    case 33u: case 34u: case 35u: case 36u: case 37u: {
        Atom *shared = atom_var_with_id(arena, "$native-open", fresh_var_id());
        Atom *answer = prepared_mode == 35u || prepared_mode == 37u
            ? atom_expr3(arena, atom_symbol(arena, "Pair"), shared, shared) : shared;
        if (prepared_mode == 33u || prepared_mode == 37u)
            *outcome = cetta_call_completed_value(answer);
        else {
            Atom *goal = atom_expr3(arena, atom_symbol(arena, "dif"), shared, atom_int(arena, 1));
            Atom *component = atom_expr2(arena, goal, shared);
            Atom *components = prepared_mode == 36u ? atom_unit(arena)
                : atom_expr(arena, &component, 1u);
            *outcome = cetta_call_suspended(answer,
                atom_expr2(arena, atom_unit(arena), components));
        }
        break;
    }
    case 23u: {
        Atom *shared = atom_var_with_id(arena, "$native-shared", fresh_var_id());
        *outcome = cetta_call_suspended(shared, atom_expr2(arena, shared, shared));
        break;
    }
    default: abort();
    }
}

static bool prepared_fixture_caller_effect(Arena *arena, Atom **arguments,
        uint32_t count, CettaCallOutcome *outcome) {
    if ((prepared_mode == 33u || prepared_mode == 34u || prepared_mode == 40u || prepared_mode == 50u) && count == 1u &&
        arguments[0]->kind == ATOM_GROUNDED && arguments[0]->ground.gkind == GV_INT &&
        arguments[0]->ground.ival == 99) {
        prepared_effects++;
        if (prepared_caller_collect) {
            if (prepared_mode == 50u) {
                /* Pressure belongs to the parent collection control. A
                 * compiled cursor has its own independent scratch limit. */
                if (arena->runtime_kind == CETTA_ARENA_RUNTIME_KIND_SURVIVOR)
                    for (unsigned block = 0u; block < 512u; block++) {
                        void *bytes = arena_alloc(arena, 4096u);
                        assert(bytes);
                        memset(bytes, 0xa5, 4096u);
                    }
            } else {
                assert(arena_alloc(arena, 2u * 1024u * 1024u));
            }
        }
        *outcome = cetta_call_completed_value(atom_int(arena, 5));
        return true;
    }
    return false;
}

static bool prepared_fixture_mutating_result(CettaLibraryContext *library,
        Space *space, Arena *arena, CettaCallOutcome *outcome) {
    if (prepared_mode == 51u || prepared_mode == 52u) {
        prepared_calls++;
        assert(prepared_mutation_arena && prepared_staged_definition);
        Atom *equation = prepared_fixture_atom(prepared_mutation_arena, prepared_staged_definition);
        const PettaPlanNode *plan = petta_program_plan_dynamic_add(library->petta_program, equation);
        assert(plan);
        space_add(space, equation);
        assert(petta_program_note_add(library->petta_program, space,
            space_get_at64(space, space_length64(space) - 1u), plan));
        *outcome = cetta_call_completed_value(prepared_mode == 51u ? atom_int(arena, 17)
            : prepared_fixture_atom(arena, "(readln!)"));
        return true;
    }
    if (prepared_mode == 27u) {
        prepared_calls++;
        SymbolId unrelated = symbol_intern_cstr(g_symbols, "body-authority-unrelated");
        bool before = cetta_library_petta_translator_rule_contains(library, unrelated);
        assert(cetta_library_petta_translator_rule_set(library, unrelated, !before));
        *outcome = cetta_call_completed_value(atom_int(arena, 17));
        return true;
    }
    if (prepared_mode == 25u || prepared_mode == 31u ||
        (prepared_mode >= 43u && prepared_mode <= 49u)) {
        prepared_calls++;
        assert(prepared_mutation_arena);
        space_add(space, prepared_fixture_atom(prepared_mutation_arena, prepared_mode == 31u
            ? "(= (resume-choice $x) 99)" : "(= (mutated-by-native) 19)"));
        switch (prepared_mode) {
        case 43u:
            *outcome = cetta_call_completed_value(prepared_fixture_atom(arena, "(readln!)"));
            break;
        case 44u: *outcome = cetta_call_failure(); break;
        case 45u:
            *outcome = cetta_call_raised(atom_error(arena, atom_int(arena, 17),
                atom_symbol(arena, "fixture")));
            break;
        case 46u:
            *outcome = cetta_call_completed_frontier(atom_expr3(arena,
                atom_int(arena, 7), atom_int(arena, 7), atom_int(arena, 9)));
            break;
        case 47u:
            *outcome = cetta_call_expression_frontier(atom_expr2(arena,
                prepared_fixture_atom(arena, "(readln!)"),
                prepared_fixture_atom(arena, "(readln!)")));
            break;
        case 48u: *outcome = cetta_call_interrupted(); break;
        case 49u: {
            Atom *shared = atom_var_with_id(arena, "$stale-native", fresh_var_id());
            Atom *goal = atom_expr3(arena, atom_symbol(arena, "dif"), shared, atom_int(arena, 1));
            Atom *component = atom_expr2(arena, goal, shared);
            *outcome = cetta_call_suspended(shared, atom_expr2(arena, atom_unit(arena),
                atom_expr(arena, &component, 1u)));
            break;
        }
        default: *outcome = cetta_call_completed_value(atom_int(arena, 17)); break;
        }
        return true;
    }
    return false;
}

bool __real_cetta_library_call_native_prepared_view(CettaLibraryContext *, Space *,
    Arena *, Atom *, Atom **, uint32_t, CettaCallOutcome *);
bool __wrap_cetta_library_call_native_prepared_view(CettaLibraryContext *library, Space *space,
    Arena *arena, Atom *head, Atom **arguments, uint32_t count, CettaCallOutcome *outcome) {
    if (!prepared_fixture || !atom_is_symbol_id(head, prepared_head))
        return __real_cetta_library_call_native_prepared_view(library, space, arena,
            head, arguments, count, outcome);
    prepared_attempts++;
    if (prepared_fixture_caller_effect(arena, arguments, count, outcome))
        return true;
    if (prepared_mode == 24u || prepared_mode == 26u || prepared_mode == 28u) {
        prepared_calls++;
        assert(count == 1u);
        if (prepared_mode == 28u) {
            /* Force parent collection while the callee retains its input. */
            assert(arena_alloc(arena, 24u * 1024u));
            prepared_effects++;
        }
        *outcome = prepared_mode != 26u ? cetta_call_completed_value(arguments[0])
            : cetta_call_completed_frontier(atom_expr2(arena, arguments[0], arguments[0]));
        return true;
    }
    if (prepared_fixture_mutating_result(library, space, arena, outcome))
        return true;
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
    if (prepared_fixture_mutating_result(library, space, arena, outcome))
        return true;
    if (!prepared_fixture_caller_effect(arena, arguments, count, outcome))
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
        if ((prepared_mode == 41u || prepared_mode == 50u) && prepared_staged_definition) {
            assert(prepared_mutation_arena && prepared_staged_space);
            Atom *equation = prepared_fixture_atom(prepared_mutation_arena,
                prepared_staged_definition);
            const PettaPlanNode *plan = petta_program_plan_dynamic_add(
                g_library_context->petta_program, equation);
            assert(plan);
            space_add(prepared_staged_space, equation);
            assert(petta_program_note_add(g_library_context->petta_program,
                prepared_staged_space, space_get_at64(prepared_staged_space,
                    space_length64(prepared_staged_space) - 1u), plan));
        }
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
    Atom *observed = bindings_apply_if_vars(returned->environment,
        recipient->arena, returned->outcome.term);
    assert(atom_eq(observed,
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
         ordinary < (mode == 8u || mode == 10u || mode == 13u || mode == 14u || mode == 15u ||
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
                    assert(atom_eq(value, atom_int(arena, mode == 5u || mode == 13u ? 5
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
        if (mode == 8u || mode == 10u || mode == 12u || mode == 16u || mode == 20u) {
            if (count != (mode == 12u ? 1u : 0u))
                fprintf(stderr, "prepared stop observation mode=%u tail=%u ordinary=%u count=%u stop=%u effects=%u\n", mode, tail, ordinary, count, stop, prepared_effects);
            assert(count == (mode == 12u ? 1u : 0u));
            PettaMachineStep expected_stop = mode == 8u || mode == 20u ? PETTA_MACHINE_STEP_SUSPENDED
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
            plan.total_structural_equality, prepared_pure_match_decision_semantics(plan.call_mode), NULL);
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
            plan.total_structural_equality, prepared_pure_match_decision_semantics(plan.call_mode), NULL);
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
            plan.total_structural_equality, prepared_pure_match_decision_semantics(plan.call_mode), NULL);
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
        prepared_head = fixture_atom(&arena, "__cetta_lib_json_parse_v1")->sym_id;
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
                &space, &arena, call, -1, NULL, NULL, false, &outcomes);
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
                call->expr.elems[0]->sym_id, 1u, plan.source_view.root,
                plan.language_id, plan.call_mode, false,
                plan.cache_entry_arguments_are_values, plan.total_structural_equality,
                PREPARED_PURE_COMPILE_INQUIRY));
            OutcomeSet result;
            outcome_set_init(&result);
            assert(prepared_pure_closed_answers_try(&space, &arena, call, -1,
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
                        ? "(native-bind-miss 1)" : "(native-tail-miss 1)"),
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
                fixture_atom(&arena, "(native-before-miss 1)"), -1, NULL, NULL,
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
                &space, &arena, call, -1, NULL, NULL, false, &result);
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
                fixture_atom(&arena, remaining_calls[k]), -1, NULL, NULL, false,
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
            fixture_atom(&arena, "(native-box 1)"), -1, NULL, NULL, false, &bounded) ==
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
            fixture_atom(&arena, "(native-box 1)"), -1, NULL, NULL, false, &declined) ==
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

static unsigned ready_invocation_controls(void) {
    CettaLibraryContext *library = calloc(1u, sizeof(*library));
    assert(library);
    cetta_library_context_init_for_language_profile(library, CETTA_LANGUAGE_PETTA, NULL);
    eval_set_library_context(library);
    Arena source, results;
    arena_init_detached(&source);
    arena_init_detached(&results);
    Space space;
    space_init(&space);
    Atom *equation = fixture_atom(&source,
        "(= (span-join $a $b $c) (Tuple $a $b $c $a))");
    const PettaPlanNode *authored = petta_program_plan_dynamic_add(library->petta_program, equation);
    assert(authored);
    space_add(&space, equation);
    assert(petta_program_note_add(library->petta_program, &space,
        space_get_at64(&space, 0u), authored));
    PettaEvalMachineContext context = {.library_context = library, .fuel = -1,
        .prepared_pure_cache = {.root_space = &space}};
    SymbolId head = symbol_intern_cstr(g_symbols, "span-join");
    Atom *args[] = {fixture_atom(&source, "(Data 7)"),
        atom_string(&source, "literal"), fixture_atom(&source, "(+ (readln!) 1)")};
    unsigned checks = 0u;
    for (unsigned round = 0u; round < 4u; round++) {
        args[0] = atom_expr2(&source, atom_symbol(&source, "Data"), atom_int(&source, round));
        CettaCallOutcome returned;
        invocation_syntax_observations = 0u;
        assert(petta_eval_open_execute_pure_relation(&context, &space,
            &results, head, args, 3u, &returned));
        assert(invocation_syntax_observations == (round == 0u ? 1u : 0u));
        assert(returned.kind == CETTA_CALL_VALUE && returned.result_form == CETTA_CALL_RESULT_COMPLETED_VALUE &&
            returned.term->kind == ATOM_EXPR && returned.term->expr.len == 5u &&
            atom_eq(returned.term->expr.elems[1], args[0]) &&
            atom_eq(returned.term->expr.elems[3], args[2]) &&
            returned.term->expr.elems[1] == returned.term->expr.elems[4]);
        checks++;
    }
    Atom *call = atom_expr(&source,
        (Atom *[]){atom_symbol_id(&source, head), args[0], args[1], args[2]}, 4u);
    PreparedPureClosedCallPlan plan;
    assert(prepared_pure_closed_call_plan(&space, &source, call, -1, true,
        &context.prepared_pure_cache, &plan));
    bool declined = false;
    CettaPreparedPureProgram *program = prepared_pure_program_cache_lookup(
        &context.prepared_pure_cache, &space, &plan.invocation, plan.source_view.root,
        plan.language_id, plan.call_mode, false, true, plan.total_structural_equality, &declined);
    assert(program && !declined);
    BindingValue values[] = {binding_value_from_atom(args[0]), binding_value_from_atom(args[1]),
        binding_value_from_atom(args[2])};
    Atom *expected = atom_expr(&source, (Atom *[]){atom_symbol(&source, "Tuple"),
        args[0], args[1], args[2], args[0]}, 5u);
    for (unsigned split = 0u; split <= 3u; split++)
    for (unsigned representation = 0u; representation < 2u; representation++) {
        CettaInvocation invocation = {.head = plan.invocation.head,
            .captured = {.atoms = representation ? NULL : args,
                .values = representation ? values : NULL, .count = split},
            .supplied = {.atoms = representation ? args + split : NULL,
                .values = representation ? NULL : values + split, .count = 3u - split}};
        assert(cetta_prepared_pure_program_rebind_closed_invocation(program, &invocation));
        CettaCallOutcome returned;
        assert(cetta_prepared_pure_program_execute_closed_controlled(program, &results,
            0u, NULL, NULL, &returned) && returned.kind == CETTA_CALL_VALUE &&
            atom_eq(returned.term, expected) &&
            returned.term->expr.elems[1] == returned.term->expr.elems[4]);
        cetta_prepared_pure_program_release_closed_execution(program);
        checks++;
    }
    for (unsigned fault = 0u; fault < 7u; fault++) {
        Atom *bad_args[] = {atom_int(&source, 999), args[1], args[2]};
        BindingValue bad_values[] = {binding_value_from_atom(bad_args[0]),
            binding_value_from_atom(bad_args[1]), binding_value_from_atom(bad_args[2])};
        CettaInvocation invocation = {.head = plan.invocation.head,
            .supplied = {.atoms = bad_args, .count = 3u}};
        if (fault == 0u) {
            invocation.supplied.count = 2u;
            invocation.supplied.atoms = (Atom *const *)(uintptr_t)1u;
        } else if (fault == 1u) {
            invocation.captured.count = UINT64_MAX;
            invocation.captured.atoms = (Atom *const *)(uintptr_t)1u;
        } else if (fault == 2u) {
            invocation.supplied.values = bad_values;
        } else if (fault == 3u) {
            bad_args[2] = NULL;
        } else if (fault == 4u) {
            bad_args[2] = atom_var(&source, "$open-operand");
        } else if (fault == 5u) {
            bad_values[2] = binding_value_from_context(args[2], var_epoch_suffix(fresh_var_id()));
            invocation.supplied.atoms = NULL;
            invocation.supplied.values = bad_values;
        } else {
            invocation.head = binding_value_from_context(plan.invocation.head.skeleton,
                var_epoch_suffix(fresh_var_id()));
        }
        assert(!cetta_prepared_pure_program_rebind_closed_invocation(program, &invocation));
        CettaCallOutcome returned;
        assert(cetta_prepared_pure_program_execute_closed_controlled(program, &results,
            0u, NULL, NULL, &returned) && returned.kind == CETTA_CALL_VALUE &&
            atom_eq(returned.term, expected));
        cetta_prepared_pure_program_release_closed_execution(program);
        checks++;
    }
    assert(cetta_prepared_pure_program_is_current(program));
    equation = fixture_atom(&source, "(= (span-join $a $b $c) (Other $c))");
    authored = petta_program_plan_dynamic_add(library->petta_program, equation);
    assert(authored);
    space_add(&space, equation);
    assert(petta_program_note_add(library->petta_program, &space,
        space_get_at64(&space, 1u), authored));
    assert(!cetta_prepared_pure_program_is_current(program));
    CettaCallOutcome returned;
    invocation_syntax_observations = 0u;
    assert(!petta_eval_open_execute_pure_relation(&context, &space, &results, head, args, 3u, &returned));
    assert(invocation_syntax_observations == 1u);
    checks++;
    prepared_pure_program_cache_free(&context.prepared_pure_cache);
    space_free(&space);
    arena_free(&source);
    arena_free(&results);
    eval_set_library_context(NULL);
    cetta_library_context_free(library);
    free(library);
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
                &cache, &space, &plan.invocation, plan.source_view.root, plan.language_id,
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

static bool lexical_completion_interrupt(void *context) {
    unsigned *polls = context;
    (*polls)++;
    return true;
}

/* Both lowerings receive the same fresh parameter sequence. The retained
 * variant owns unchanged source nodes that ABT opening can still borrow. */
static unsigned lexical_source_controls(void) {
    const uint32_t arities[] = {1u, 2u, 4u, 64u};
    CettaLibraryContext *library = calloc(1u, sizeof(*library));
    assert(library);
    cetta_library_context_init_for_language_profile(library, CETTA_LANGUAGE_PETTA, NULL);
    eval_set_library_context(library);
    unsigned checks = 0u;
    for (size_t shape = 0u; shape < 4u; shape++) {
        uint32_t arity = arities[shape];
        Arena input, retained, temporary;
        arena_init_detached(&input);
        arena_init_detached(&retained);
        arena_init_detached(&temporary);
        Atom *parameters[64], *fields[69], *arguments[64], *expected_fields[69];
        fields[0] = atom_symbol(&input, "LexicalFields");
        expected_fields[0] = atom_symbol(&retained, "LexicalFields");
        for (uint32_t index = 0u; index < arity; index++) {
            parameters[index] = atom_var_with_id(&input, "$source-parameter", fresh_var_id());
            fields[index + 1u] = parameters[index];
            arguments[index] = index ? atom_int(&retained, index)
                : fixture_atom(&retained, "(readln!)");
            expected_fields[index + 1u] = arguments[index];
        }
        Atom *capture = atom_var_with_id(&input, "$outside", fresh_var_id());
        Atom *expected_capture = atom_var_with_id(&retained, "$outside", capture->var_id);
        fields[arity + 1u] = fields[arity + 2u] = capture;
        expected_fields[arity + 1u] = expected_fields[arity + 2u] = expected_capture;
        fields[arity + 3u] = fixture_atom(&input, "(Frozen \"payload\")");
        expected_fields[arity + 3u] = fixture_atom(&retained, "(Frozen \"payload\")");
        fields[arity + 4u] = parameters[0];
        expected_fields[arity + 4u] = arguments[0];
        Atom *expected = atom_expr(&retained, expected_fields, arity + 5u);
        Atom *raw = atom_expr3(&input, atom_symbol(&input, "|->"),
            atom_expr(&input, parameters, arity), atom_expr(&input, fields, arity + 5u));
        Atom *callable = petta_capture_lambda(&input, raw, NULL);
        assert(callable);
        PettaLexicalSource owned, borrowed;
        assert(prepared_pure_open_lexical_source(&retained, callable, arity, &owned));
        assert(petta_open_lexical_source(&temporary, callable, arity, "__lexical_arg", &borrowed));
        assert(owned.open_captures && borrowed.open_captures);
        Bindings owned_values, borrowed_values;
        bindings_init(&owned_values);
        bindings_init(&borrowed_values);
        for (uint32_t index = 0u; index < arity; index++) {
            assert(owned.parameters[index]->kind == ATOM_VAR &&
                borrowed.parameters[index]->kind == ATOM_VAR &&
                !var_id_eq(owned.parameters[index]->var_id, capture->var_id));
            for (uint32_t prior = 0u; prior < index; prior++)
                assert(!var_id_eq(owned.parameters[index]->var_id, owned.parameters[prior]->var_id) &&
                    !var_id_eq(borrowed.parameters[index]->var_id, borrowed.parameters[prior]->var_id));
            assert(bindings_add_var(&owned_values, owned.parameters[index], arguments[index]));
            assert(bindings_add_var(&borrowed_values, borrowed.parameters[index], arguments[index]));
        }
        Atom *observed = bindings_apply_if_vars(&borrowed_values, &temporary, borrowed.body);
        assert(atom_eq(observed, expected));
        PettaLexicalSource refused;
        assert(!petta_open_lexical_source(&temporary, callable, 0u, "__lexical_arg", &refused));
        assert(!petta_open_lexical_source(&temporary, callable, 65u, "__lexical_arg", &refused));
        assert(!petta_open_lexical_source(&temporary, callable,
            arity == 64u ? 63u : arity + 1u, "__lexical_arg", &refused));
        bindings_free(&borrowed_values);
        Bindings close_capture;
        bindings_init(&close_capture);
        assert(bindings_add_var(&close_capture, capture, atom_string(&input, "capture")));
        Atom *closed_raw = bindings_apply_if_vars(&close_capture, &input, raw);
        Atom *closed_callable = petta_capture_lambda(&input, closed_raw, NULL);
        bindings_free(&close_capture);
        assert(closed_callable && !atom_has_vars(closed_callable));
        TermUniverse universe;
        term_universe_init(&universe);
        term_universe_set_persistent_arena(&universe, &input);
        Space space;
        space_init_with_universe(&space, &universe);
        PettaEvalMachineContext context = {.library_context = library, .fuel = -1};
        void *compiled = petta_eval_open_prepare_pure_callable(
            &context, &space, &retained, closed_callable, arity);
        assert(compiled);
        /* Mutation makes a missing ownership copy fail even without a
         * sanitizer; freeing the loan also checks every retained leaf. */
        assert(borrowed.body->kind == ATOM_EXPR && borrowed.body->expr.len == arity + 5u);
        Atom *borrowed_frozen = borrowed.body->expr.elems[arity + 3u];
        assert(borrowed_frozen->kind == ATOM_EXPR && borrowed_frozen->expr.len == 2u);
        borrowed_frozen->expr.elems[1] = atom_string(&input, "changed input");
        observed = bindings_apply_if_vars(&owned_values, &retained, owned.body);
        assert(atom_eq(observed, expected));
        arena_free(&temporary);
        arena_free(&input);
        observed = bindings_apply_if_vars(&owned_values, &retained, owned.body);
        assert(atom_eq(observed, expected));
        bindings_free(&owned_values);
        bindings_init(&close_capture);
        assert(bindings_add_var(&close_capture, expected_capture, atom_string(&retained, "capture")));
        Atom *closed_expected = bindings_apply_if_vars(&close_capture, &retained, expected);
        CettaCallOutcome result = {0};
        if (arity <= 2u) {
            assert(petta_eval_open_execute_pure_callable(&context, compiled, &retained,
                arguments, arity, &result));
            assert(result.kind == CETTA_CALL_VALUE &&
                result.result_form == CETTA_CALL_RESULT_COMPLETED_VALUE &&
                atom_eq(result.term, closed_expected));
        } else {
            CettaPreparedPureAnswerCursor *cursor = petta_eval_open_callable_answers(
                &context, compiled, &retained, arguments, arity);
            Atom *answer = NULL;
            assert(cursor && cetta_prepared_pure_answer_cursor_next(cursor, &answer) ==
                CETTA_PREPARED_PURE_CURSOR_ANSWER && atom_eq(answer, closed_expected));
            assert(cetta_prepared_pure_answer_cursor_next(cursor, &answer) ==
                CETTA_PREPARED_PURE_CURSOR_EXHAUSTED);
            cetta_prepared_pure_answer_cursor_close(cursor);
        }
        bindings_free(&close_capture);
        petta_eval_open_release_pure_callable(&context, compiled);
        space_free(&space);
        term_universe_free(&universe);
        arena_free(&retained);
        checks += 11u;
    }
    Arena input, owner;
    arena_init_detached(&input);
    arena_init_detached(&owner);
    Atom *nested = petta_capture_lambda(&input,
        fixture_atom(&input, "(|-> ($x) (|-> ($y) (Pair $x $y)))"), NULL);
    PettaLexicalSource source;
    assert(nested && petta_open_lexical_source(&owner, nested, 1u, "__lexical_arg", &source));
    assert(petta_semantics_lambda_body(source.body, NULL));
    assert(!prepared_pure_open_lexical_source(&owner, nested, 1u, &source));
    size_t before = arena_accounted_live_bytes(&owner);
    Atom *not_callable = fixture_atom(&input, "(not-a-callable (payload 7))");
    assert(!prepared_pure_open_lexical_source(&owner, not_callable, 1u, &source));
    assert(arena_accounted_live_bytes(&owner) == before);
    Atom *cyclic = petta_semantics_lambda_value(&input,
        petta_semantics_new_callable_identity(&input), atom_int(&input, 0));
    assert(cyclic && cyclic->kind == ATOM_EXPR && cyclic->expr.len == 3u);
    cyclic->expr.elems[2] = cyclic;
    assert(!petta_open_lexical_source(&owner, cyclic, 1u, "__lexical_arg", &source));
    assert(arena_accounted_live_bytes(&owner) == before);
    arena_free(&owner);
    arena_free(&input);
    eval_set_library_context(NULL);
    cetta_library_context_free(library);
    free(library);
    return checks + 6u;
}

static SymbolId caller_owner_head, caller_extension_head;
static unsigned caller_entries, caller_extension_calls, caller_result_probes, caller_packet_mode;
static bool caller_declines, caller_scalar_refusal;
static CettaEvalCompletion caller_collection_completion;

static PeTTaNamedArity caller_result_arity(void *context, SymbolId head, CettaExprLen supplied) {
    if (head == caller_extension_head) {
        if (caller_entries)
            caller_result_probes++;
        return (PeTTaNamedArity){.known = true, .exact = supplied == 1u};
    }
    return petta_eval_machine_foreign_named_arity(context, head, supplied);
}

static Atom *caller_result_value(Arena *arena) {
    Atom *payload = atom_int(arena, 7);
    if (caller_packet_mode == 6u) {
        Atom *shared = atom_var_with_id(arena, "$result-shared", fresh_var_id());
        Atom *distinct = atom_var_with_id(arena, "$result-distinct", fresh_var_id());
        payload = atom_expr3(arena, shared, shared, distinct);
    }
    if (caller_packet_mode == 5u)
        return atom_error(arena, payload, atom_symbol(arena, "returned-data"));
    if (caller_packet_mode == 9u)
        return atom_symbol(arena, "Empty");
    if (caller_packet_mode == 10u)
        return atom_int(arena, 37);
    if (caller_packet_mode == 11u || caller_packet_mode == 12u)
        return petta_semantics_boolean_value(arena, caller_packet_mode == 11u);
    return atom_expr2(arena, atom_symbol_id(arena, caller_extension_head), payload);
}

static bool caller_prepared_result(void *context, Space *space, Arena *arena,
        Atom *call, CettaCallOutcome *returned) {
    (void)context;
    (void)space;
    if (atom_head_symbol_id(call) != caller_owner_head || caller_declines || caller_scalar_refusal)
        return false;
    caller_entries++;
    Atom *value = caller_result_value(arena);
    *returned = caller_packet_mode == 1u ? cetta_call_value(value)
        : caller_packet_mode == 2u ? cetta_call_failure()
        : caller_packet_mode == 3u ? cetta_call_interrupted()
        : caller_packet_mode == 4u ? cetta_call_raised(atom_error(arena,
            atom_int(arena, 7), atom_symbol(arena, "raised-control")))
        : caller_packet_mode == 7u ? cetta_call_completed_frontier(atom_expr2(arena, value, value))
        : caller_packet_mode == 8u ? cetta_call_completed_value(NULL)
        : cetta_call_completed_value(value);
    return true;
}

static bool caller_result_extension(void *context, Arena *arena, Atom *expression,
        Atom *expected, const Bindings *environment, OutcomeSet *outcomes,
        bool *recognized, CettaCallOutcome *end, const CettaDelayView *delay) {
    if (atom_head_symbol_id(expression) != caller_extension_head)
        return petta_eval_machine_extension_call(context, arena, expression, expected,
            environment, outcomes, recognized, end, delay);
    caller_extension_calls++;
    *recognized = true;
    *end = cetta_call_failure();
    outcome_set_add(outcomes, atom_int(arena, 17), environment);
    return true;
}

static bool caller_collected_result(void *context, Space *space, Arena *arena,
        Atom *call, OutcomeSet *outcomes, CettaEvalCompletion *completion) {
    (void)context;
    (void)space;
    if (atom_head_symbol_id(call) != caller_owner_head)
        return false;
    *completion = caller_collection_completion;
    if (caller_declines) {
        Bindings empty;
        bindings_init(&empty);
        outcome_set_add(outcomes, atom_symbol(arena, "discarded-admission-temporary"), &empty);
        return false;
    }
    caller_entries++;
    if (caller_packet_mode == 2u)
        return true;
    Bindings empty;
    bindings_init(&empty);
    Atom *value = caller_result_value(arena);
    outcome_set_add(outcomes, value, &empty);
    outcome_set_add(outcomes, value, &empty);
    return true;
}

static void caller_check_data(Arena *arena, Atom *value) {
    if (caller_packet_mode == 6u) {
        assert(value->kind == ATOM_EXPR && value->expr.len == 2u &&
            atom_is_symbol_id(value->expr.elems[0], caller_extension_head));
        Atom *fields = value->expr.elems[1];
        assert(fields->kind == ATOM_EXPR && fields->expr.len == 3u);
        Atom *first = fields->expr.elems[0], *again = fields->expr.elems[1], *other = fields->expr.elems[2];
        assert(first->kind == ATOM_VAR && again->kind == ATOM_VAR && other->kind == ATOM_VAR &&
            var_id_eq(first->var_id, again->var_id) && !var_id_eq(first->var_id, other->var_id));
    } else {
        const char *text = caller_packet_mode == 5u ? "(Error 7 returned-data)"
            : caller_packet_mode == 9u ? "Empty" : caller_packet_mode == 10u ? "37"
            : caller_packet_mode == 11u ? "true" : caller_packet_mode == 12u ? "false"
            : "(caller-result-extension 7)";
        assert(atom_eq(value, fixture_atom(arena, text)));
    }
}

static unsigned caller_result_controls(void) {
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
    caller_owner_head = symbol_intern_cstr(g_symbols, "caller-result-owner");
    caller_extension_head = symbol_intern_cstr(g_symbols, "caller-result-extension");
    refusal_add_equation(&space, &source,
        "(= (caller-result-owner $x) (caller-result-extension 7))");
    refusal_add_equation(&space, &source,
        "(= (caller-result-condition $x $open) (if (caller-result-owner $x) selected-true selected-false))");
    PettaEvalMachineContext context = {.library_context = library, .fuel = -1,
        .semantic_authority_epoch = 1u, .prepared_pure_cache = {.root_space = &space}};
    PettaMachineHost host = petta_eval_machine_host(&context, -1,
        false, false, false, false, false, petta_eval_machine_match_semantics(library, false));
    host.prepare_call = NULL;
    host.open_answer_cursor = NULL;
    host.open_relation_cursor = NULL;
    host.measure_stats = true;
    host.execute_prepared_pure_call = caller_prepared_result;
    host.foreign_named_arity = caller_result_arity;
    host.foreign_named_arity_resolved = caller_result_arity;
    host.extension_call = caller_result_extension;
    unsigned checks = 0u;
    for (unsigned condition = 0u; condition < 2u; condition++)
    for (unsigned mode = 0u; mode <= 12u; mode++) {
        caller_entries = caller_extension_calls = caller_result_probes = 0u;
        caller_packet_mode = mode;
        caller_declines = false;
        caller_scalar_refusal = false;
        PettaMachine machine;
        assert(petta_machine_init(&machine, &space, &source, fixture_atom(&source,
            condition ? "(caller-result-condition 7 $outside)" : "(caller-result-owner 7)"), NULL, &host));
        Atom *answer = NULL;
        Bindings environment;
        PettaMachineStep step = petta_machine_next(&machine, &answer, &environment);
        bool bad = mode == 1u || mode == 7u || mode == 8u;
        PettaMachineStep expected_stop = bad ? PETTA_MACHINE_STEP_HOST_ERROR
            : mode == 3u ? PETTA_MACHINE_STEP_SUSPENDED : PETTA_MACHINE_STEP_EXHAUSTED;
        if (mode == 2u || mode == 3u || bad)
            assert(step == expected_stop);
        else {
            assert(step == PETTA_MACHINE_STEP_ANSWER);
            Atom *value = bindings_apply_if_vars(&environment, &source, answer);
            if (mode == 4u) {
                assert(atom_is_error(value) && petta_machine_last_answer_raised(&machine));
            } else if (condition) {
                assert(atom_eq(value, fixture_atom(&source,
                    mode == 11u ? "selected-true" : "selected-false")));
            } else
                caller_check_data(&source, value);
        }
        bindings_free(&environment);
        if (step == PETTA_MACHINE_STEP_ANSWER) {
            assert(petta_machine_next(&machine, &answer, &environment) == expected_stop);
            bindings_free(&environment);
        }
        if (mode == 3u || bad) {
            assert(petta_machine_next(&machine, &answer, &environment) == expected_stop);
            bindings_free(&environment);
        }
        PettaMachineStats stats;
        assert(petta_machine_stats(&machine, &stats));
        if (caller_entries != 1u || caller_extension_calls || caller_result_probes)
            fprintf(stderr, "caller condition=%u mode=%u entries=%u extension=%u probes=%u\n",
                condition, mode, caller_entries, caller_extension_calls, caller_result_probes);
        assert(caller_entries == 1u && !caller_extension_calls && !caller_result_probes);
        assert(stats.equation_branches_scheduled == condition);
        if (condition && (mode == 11u || mode == 12u))
            assert(stats.pure_grounded_slot_frame_entries > 0u);
        petta_machine_destroy(&machine);
        checks++;
    }
    caller_packet_mode = 0u;
    for (unsigned collected = 0u; collected < 2u; collected++)
    for (unsigned decline = 0u; decline < 2u; decline++) {
        PettaMachineHost selected = host;
        caller_declines = decline;
        caller_scalar_refusal = collected;
        if (collected) {
            selected.collect_closed_answers = caller_collected_result;
        }
        caller_collection_completion = CETTA_EVAL_COMPLETE;
        caller_entries = caller_extension_calls = caller_result_probes = 0u;
        PettaMachine machine;
        assert(petta_machine_init(&machine, &space, &source,
            fixture_atom(&source, "(caller-result-owner 7)"), NULL, &selected));
        unsigned count = 0u;
        for (;;) {
            Atom *answer = NULL;
            Bindings environment;
            PettaMachineStep step = petta_machine_next(&machine, &answer, &environment);
            if (step == PETTA_MACHINE_STEP_ANSWER) {
                assert(++count <= 2u);
                caller_check_data(&source, bindings_apply_if_vars(&environment, &source, answer));
            }
            bindings_free(&environment);
            if (step != PETTA_MACHINE_STEP_ANSWER) {
                assert(step == PETTA_MACHINE_STEP_EXHAUSTED);
                break;
            }
        }
        if (count != (collected && !decline ? 2u : 1u))
            fprintf(stderr, "caller collected=%u decline=%u count=%u entries=%u\n",
                collected, decline, count, caller_entries);
        assert(count == (collected && !decline ? 2u : 1u));
        assert(caller_entries == !decline && !caller_extension_calls);
        petta_machine_destroy(&machine);
        checks++;
    }
    for (unsigned extra = 0u; extra < 3u; extra++) {
        PettaMachineHost selected = host;
        caller_packet_mode = extra == 2u ? 2u : 0u;
        caller_scalar_refusal = extra == 2u;
        caller_collection_completion = CETTA_EVAL_COMPLETE;
        caller_declines = false;
        if (extra == 1u)
            selected.unlimited_transition_budget = false;
        if (extra == 2u)
            selected.collect_closed_answers = caller_collected_result;
        if (extra == 0u)
            refusal_add_equation(&space, &source, "(= (caller-result-extension $x) 17)");
        caller_entries = caller_extension_calls = caller_result_probes = 0u;
        PettaMachine machine;
        assert(petta_machine_init(&machine, &space, &source, fixture_atom(&source,
            extra == 0u ? "(let $value (caller-result-owner 7) (eval $value))"
                        : "(caller-result-owner 7)"), NULL, &selected));
        Atom *answer = NULL;
        Bindings environment;
        PettaMachineStep step = petta_machine_next(&machine, &answer, &environment);
        assert(step == (extra == 2u ? PETTA_MACHINE_STEP_EXHAUSTED : PETTA_MACHINE_STEP_ANSWER));
        if (extra == 0u)
            assert(atom_eq(bindings_apply_if_vars(&environment, &source, answer), atom_int(&source, 17)));
        if (extra == 1u)
            caller_check_data(&source, bindings_apply_if_vars(&environment, &source, answer));
        bindings_free(&environment);
        if (step == PETTA_MACHINE_STEP_ANSWER) {
            assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_EXHAUSTED);
            bindings_free(&environment);
        }
        assert(caller_entries == (extra == 1u ? 0u : 1u));
        assert(extra == 0u ? caller_extension_calls <= 1u : !caller_extension_calls);
        petta_machine_destroy(&machine);
        checks++;
    }
    caller_packet_mode = 0u;
    const CettaEvalCompletion stops[] = {CETTA_EVAL_INCOMPLETE_FUEL, CETTA_EVAL_INCOMPLETE_CANCELLED,
        CETTA_EVAL_INCOMPLETE_STACK, CETTA_EVAL_INCOMPLETE_CAPACITY,
        CETTA_EVAL_INCOMPLETE_INVALIDATED, CETTA_EVAL_INCOMPLETE_HOST_FAILURE};
    const PettaMachineStep reasons[] = {PETTA_MACHINE_STEP_SUSPENDED, PETTA_MACHINE_STEP_SUSPENDED,
        PETTA_MACHINE_STEP_STACK, PETTA_MACHINE_STEP_CAPACITY,
        PETTA_MACHINE_STEP_INVALIDATED, PETTA_MACHINE_STEP_HOST_ERROR};
    for (unsigned cause = 0u; cause < sizeof(stops)/sizeof(*stops); cause++) {
        PettaMachineHost selected = host;
        caller_scalar_refusal = true;
        selected.collect_closed_answers = caller_collected_result;
        caller_collection_completion = stops[cause];
        caller_declines = false;
        caller_entries = caller_extension_calls = caller_result_probes = 0u;
        PettaMachine machine;
        assert(petta_machine_init(&machine, &space, &source,
            fixture_atom(&source, "(caller-result-owner 7)"), NULL, &selected));
        for (unsigned retry = 0u; retry < 2u; retry++) {
            Atom *answer = NULL;
            Bindings environment;
            assert(petta_machine_next(&machine, &answer, &environment) == reasons[cause]);
            bindings_free(&environment);
        }
        assert(caller_entries == 1u && !caller_extension_calls && !caller_result_probes);
        petta_machine_destroy(&machine);
        checks++;
    }
    prepared_pure_program_cache_free(&context.prepared_pure_cache);
    space_free(&space);
    term_universe_free(&universe);
    arena_free(&source);
    eval_set_library_context(NULL);
    cetta_library_context_free(library);
    free(library);
    return checks;
}

static unsigned caller_real_collection_controls(void) {
    CettaLibraryContext *library = calloc(1u, sizeof(*library));
    assert(library);
    cetta_library_context_init_for_language_profile(library, CETTA_LANGUAGE_HE, NULL);
    eval_set_library_context(library);
    Arena source;
    arena_init(&source);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &source);
    Space space;
    space_init_with_universe(&space, &universe);
    assert(space_admit_atom(&space, &source, fixture_atom(&source,
        "(= (collector-entered $x) (__cetta_lib_json_parse_v1 $x))")));
    Atom *call = fixture_atom(&source, "(collector-entered 1)");
    PettaEvalMachineContext context = {.library_context = library, .fuel = -1,
        .semantic_authority_epoch = 1u, .prepared_pure_cache = {.root_space = &space}};
    prepared_head = symbol_intern_cstr(g_symbols, "__cetta_lib_json_parse_v1");
    prepared_fixture = true;
    const unsigned modes[] = {0u, 2u, 8u, 10u, 12u};
    unsigned checks = 0u;
    for (unsigned parent_kind = 0u; parent_kind < 3u; parent_kind++)
    for (unsigned m = 0u; m < sizeof(modes)/sizeof(*modes); m++) {
        prepared_mode = modes[m];
        prepared_calls = prepared_attempts = prepared_effects = 0u;
        CettaEvalEpisode parent = {0};
        if (parent_kind == 2u)
            parent.completion = CETTA_EVAL_INCOMPLETE_FUEL;
        CettaEvalEpisode *previous = g_eval_completion_tracker;
        g_eval_completion_tracker = parent_kind ? &parent : NULL;
        OutcomeSet values;
        outcome_set_init_with_owner(&values, &source);
        CettaEvalCompletion completion = CETTA_EVAL_COMPLETE;
        bool handled = petta_eval_machine_collect_closed_answers(&context, &space, &source,
            call, &values, &completion);
        assert(g_eval_completion_tracker == (parent_kind ? &parent : NULL));
        CettaEvalCompletion expected = prepared_mode == 8u ? CETTA_EVAL_INCOMPLETE_CANCELLED
            : prepared_mode == 10u || prepared_mode == 12u ? CETTA_EVAL_INCOMPLETE_HOST_FAILURE
            : CETTA_EVAL_COMPLETE;
        if (!handled || completion != expected || prepared_calls != 1u)
            fprintf(stderr, "collector parent=%u mode=%u handled=%u completion=%u calls=%u answers=%llu\n",
                parent_kind, prepared_mode, handled, completion, prepared_calls,
                (unsigned long long)values.len);
        assert(handled && completion == expected && prepared_calls == 1u && prepared_attempts == 1u);
        if (parent_kind)
            assert(parent.completion == (parent_kind == 2u ? CETTA_EVAL_INCOMPLETE_FUEL : expected));
        if (prepared_mode == 0u || prepared_mode == 2u) {
            assert(values.len == (prepared_mode == 0u ? 1u : 3u));
            for (CettaCount i = 0u; i < values.len; i++)
                assert(atom_eq(outcome_atom_materialize(&source, &values.items[i]),
                    atom_int(&source, i == 2u ? 9 : 7)));
        }
        outcome_set_free(&values);
        g_eval_completion_tracker = previous;
        checks++;
    }
    const unsigned credits[] = {0u, 1u, 2u, 17u, 1024u};
    for (unsigned i = 0u; i < sizeof(credits)/sizeof(*credits); i++) {
        CettaEvalEpisode parent = cetta_eval_episode_begin(NULL, (int)credits[i]);
        CettaEvalEpisode *previous = g_eval_completion_tracker;
        g_eval_completion_tracker = &parent;
        OutcomeSet values;
        outcome_set_init_with_owner(&values, &source);
        prepared_mode = prepared_calls = prepared_attempts = 0u;
        CettaEvalCompletion completion = CETTA_EVAL_COMPLETE;
        assert(!petta_eval_machine_collect_closed_answers(&context, &space, &source,
            call, &values, &completion));
        assert(completion == CETTA_EVAL_COMPLETE && !values.len && !prepared_calls &&
            !prepared_attempts && parent.budget_remaining == credits[i] && !parent.steps_spent);
        assert(g_eval_completion_tracker == &parent);
        outcome_set_free(&values);
        g_eval_completion_tracker = previous;
        checks++;
    }
    prepared_fixture = false;
    prepared_pure_program_cache_free(&context.prepared_pure_cache);
    space_free(&space);
    term_universe_free(&universe);
    arena_free(&source);
    eval_set_library_context(NULL);
    cetta_library_context_free(library);
    free(library);
    return checks;
}

static unsigned authority_observation_controls(void) {
    CettaLibraryContext *library = calloc(1u, sizeof(*library));
    assert(library);
    cetta_library_context_init_for_language_profile(library, CETTA_LANGUAGE_PETTA, NULL);
    CettaLibraryContext *previous = g_library_context;
    eval_set_library_context(library);
    PettaEvalMachineContext context = {
        .library_context = library, .semantic_authority_epoch = 37u};
    CettaLibPrologReadToken foreign = library->lib_prolog
        ? cetta_lib_prolog_read_token(library->lib_prolog)
        : (CettaLibPrologReadToken){0};
    const uint64_t expected[] = {
        foreign.instance_id, foreign.revision, 37u, 0u,
        cetta_library_petta_translator_rule_revision(library),
        cetta_library_petta_admission_revision(library),
        library->lib_prolog ? petta_libpl_capability_revision(library->lib_prolog) : 0u,
        library->active_mask};
    PettaMachineAuthorityToken semantic, snapshot;
    assert(petta_eval_machine_semantic_authority_token(&context, &semantic));
    assert(semantic.length == 5u && !memcmp(semantic.words, expected, 5u * sizeof(uint64_t)));
    for (unsigned index = 5u; index < PETTA_MACHINE_AUTHORITY_WORD_CAPACITY; index++)
        assert(semantic.words[index] == 0u);
    assert(petta_eval_machine_answer_authority_token(&context, &snapshot));
    assert(snapshot.length == 8u && !memcmp(snapshot.words, expected, sizeof(expected)));
    assert(petta_eval_machine_answer_authority_current(&context, &snapshot));
    unsigned checks = 2u;
    for (unsigned index = 0u; index < 8u; index++) {
        PettaMachineAuthorityToken changed = snapshot;
        changed.words[index] ^= UINT64_C(1);
        assert(!petta_eval_machine_answer_authority_current(&context, &changed));
        checks++;
    }
    const uint8_t lengths[] = {0u, 1u, 3u, 5u, 7u, 9u, UINT8_MAX};
    for (size_t index = 0u; index < sizeof(lengths) / sizeof(*lengths); index++) {
        PettaMachineAuthorityToken changed = snapshot;
        changed.length = lengths[index];
        assert(!petta_eval_machine_answer_authority_current(&context, &changed));
        checks++;
    }
    context.semantic_authority_epoch++;
    assert(!petta_eval_machine_answer_authority_current(&context, &snapshot));
    context.semantic_authority_epoch--;
    PettaEvalTransaction transaction = {0};
    context.transaction = &transaction;
    assert(!petta_eval_machine_answer_authority_current(&context, &snapshot));
    context.transaction = NULL;
    library->active_mask ^= UINT64_C(1);
    assert(!petta_eval_machine_answer_authority_current(&context, &snapshot));
    library->active_mask ^= UINT64_C(1);
    checks += 3u;
    PettaCallableAuthority retained = {.context = &context, .library = library, .token = snapshot};
    assert(petta_eval_callable_authority_current(&retained));
    eval_set_library_context(NULL);
    assert(!petta_eval_callable_authority_current(&retained));
    eval_set_library_context(library);
    context.library_context = NULL;
    assert(!petta_eval_callable_authority_current(&retained));
    context.library_context = library;
    assert(petta_eval_callable_authority_current(&retained));
    assert(!petta_eval_machine_answer_authority_current(NULL, &snapshot));
    assert(!petta_eval_machine_answer_authority_current(&context, NULL));
    checks += 4u;
    eval_set_library_context(previous);
    cetta_library_context_free(library);
    free(library);
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
    prepared_head = fixture_atom(&source, "__cetta_lib_json_parse_v1")->sym_id;
    prepared_fixture = true;
    prepared_mode = prepared_calls = prepared_attempts = 0u;
    Atom *native = petta_capture_lambda(&source,
        fixture_atom(&source, "(|-> ($x) (__cetta_lib_json_parse_v1 $x))"), NULL);
    Atom *pure = petta_capture_lambda(&source,
        fixture_atom(&source, "(|-> ($x) (+ $x 1))"), NULL);
    assert(native && pure);
    void *native_plan = petta_eval_open_prepare_pure_callable(
        &context, &space, &owner, native, 1u);
    assert(native_plan);
    assert(prepared_calls == 0u && prepared_attempts == 0u);
    Atom *native_argument = atom_int(&source, 5), *untyped = NULL;
    assert(!cetta_prepared_pure_program_execute(((PettaPreparedCallable *)native_plan)->program, &owner,
        native_argument, native_argument, &untyped));
    assert(prepared_calls == 0u && prepared_attempts == 0u);
    petta_eval_open_release_pure_callable(&context, native_plan);
    void *compiled = petta_eval_open_prepare_pure_callable(&context, &space, &owner, pure, 1u);
    assert(compiled);
    Atom *arguments[] = {atom_int(&source, 5)};
    CettaCallOutcome result = {0};
    assert(petta_eval_open_execute_pure_callable(&context, compiled, &owner,
        arguments, 1u, &result));
    assert(result.kind == CETTA_CALL_VALUE &&
        result.result_form == CETTA_CALL_RESULT_COMPLETED_VALUE &&
        atom_eq(result.term, atom_int(&source, 6)));
    assert(space_admit_atom(&space, &source,
        fixture_atom(&source, "(= (ScalarEntryRevision) changed)")));
    assert(!cetta_prepared_pure_program_is_current(
        ((PettaPreparedCallable *)compiled)->program));
    size_t stale_live = arena_accounted_live_bytes(&owner);
    result = (CettaCallOutcome){0};
    assert(!petta_eval_open_execute_pure_callable(&context, compiled, &owner,
        arguments, 1u, &result));
    assert(!result.term && prepared_calls == 0u && prepared_attempts == 0u);
    assert(!cetta_prepared_pure_program_execute_controlled(
        ((PettaPreparedCallable *)compiled)->program, &owner,
        arguments[0], arguments[0], NULL, NULL, &result));
    assert(arena_accounted_live_bytes(&owner) == stale_live);
    petta_eval_open_release_pure_callable(&context, compiled);
    unsigned checks = 6u;
    Atom *identity = petta_capture_lambda(&source,
        fixture_atom(&source, "(|-> ($x) $x)"), NULL);
    compiled = petta_eval_open_prepare_pure_callable(&context, &space, &owner, identity, 1u);
    assert(compiled);
    const char *values[] = {"7", "(Error payload marker)", "Empty", "(readln!)", "(return 17)"};
    for (size_t index = 0u; index < 5u; index++) {
        arguments[0] = fixture_atom(&source, values[index]);
        result = (CettaCallOutcome){0};
        assert(petta_eval_open_execute_pure_callable(&context, compiled, &owner,
            arguments, 1u, &result));
        assert(result.kind == CETTA_CALL_VALUE &&
            result.result_form == CETTA_CALL_RESULT_COMPLETED_VALUE &&
            result.term == arguments[0]);
        checks += 2u;
    }
    unsigned polls = 0u;
    assert(cetta_prepared_pure_program_execute_controlled(((PettaPreparedCallable *)compiled)->program, &owner,
        arguments[0], arguments[0], lexical_completion_interrupt, &polls, &result));
    assert(result.kind == CETTA_CALL_INTERRUPTED && polls == 1u);
    checks += 2u;
    petta_eval_open_release_pure_callable(&context, compiled);
    Atom *matcher_equation = fixture_atom(&source,
        "(= (scalar-pair-first $x) (let (Pair $a $b) $x $a))");
    const PettaPlanNode *matcher_plan = petta_program_plan_dynamic_add(
        library->petta_program, matcher_equation);
    assert(matcher_plan);
    CettaCount matcher_index = space_length64(&space);
    space_add(&space, matcher_equation);
    assert(petta_program_note_add(library->petta_program, &space,
        space_get_at64(&space, matcher_index), matcher_plan));
    Atom *matcher = petta_capture_lambda(&source,
        fixture_atom(&source, "(|-> ($x) (scalar-pair-first $x))"), NULL);
    compiled = petta_eval_open_prepare_pure_callable(&context, &space, &owner, matcher, 1u);
    assert(compiled);
    arguments[0] = atom_int(&source, 5);
    assert(petta_eval_open_execute_pure_callable(&context, compiled, &owner,
        arguments, 1u, &result));
    assert(result.kind == CETTA_CALL_FAILURE && !result.term);
    arguments[0] = fixture_atom(&source, "(Pair 11 12)");
    assert(petta_eval_open_execute_pure_callable(&context, compiled, &owner,
        arguments, 1u, &result));
    assert(result.kind == CETTA_CALL_VALUE &&
        result.result_form == CETTA_CALL_RESULT_COMPLETED_VALUE &&
        atom_eq(result.term, atom_int(&source, 11)));
    checks += 4u;
    petta_eval_open_release_pure_callable(&context, compiled);
    prepared_fixture = false;
    space_free(&space);
    term_universe_free(&universe);
    arena_free(&owner);
    arena_free(&source);
    eval_set_library_context(NULL);
    cetta_library_context_free(library);
    free(library);
    return checks;
}

static Space *refusal_test_space;
static Arena *refusal_test_owner;
static bool refusal_test_mutate;
static CettaPreparedPureExpressionViewState refusal_test_view(const Atom *atom,
        const CettaComputationPlan *computation, CettaPreparedPureExpressionView *view) {
    if (atom && atom->kind == ATOM_EXPR && atom->expr.len &&
        atom_is_symbol(atom->expr.elems[0], "uncertified-projection")) {
        view->projection_mode = CETTA_PREPARED_PURE_PROJECT_CODE;
        view->projected = NULL;
        return CETTA_PREPARED_PURE_EXPRESSION_PROJECT;
    }
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
    return petta_prepared_pure_expression_view(atom, computation, view);
}

static bool source_refusal_hit(PreparedPureProgramCache *cache, Space *space,
        SymbolId head, PreparedPureRefusalInquiry inquiry) {
    return prepared_pure_program_cache_has_decline_head(cache, space, head, 1u,
        NULL, CETTA_LANGUAGE_PETTA, CETTA_GSLT_PURE_CALL_EAGER, false, true,
        active_composition_uses_total_structural_eq(), inquiry);
}

static unsigned source_admission_refusal_controls(void) {
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
    Atom *effectful = refusal_add_equation(&space, &source,
        "(= (refusal-source $x) (readln!))");
    refusal_add_equation(&space, &source, "(= (refusal-pure $x) (+ $x 1))");
    Atom *pure = fixture_atom(&source, "(refusal-pure 7)");
    Atom *call = fixture_atom(&source, "(refusal-source 7)");
    refusal_effect_head = call->expr.elems[0]->sym_id;
    refusal_effect_calls = refusal_effect_mode = 0u;
    refusal_cache_allocation_failures = 0u;
    PreparedPureProgramCache cache = {.root_space = &space};
    PreparedPureClosedCallPlan plan;
    unsigned checks = 0u;

    /* Input and episode refusals do not classify the callee. */
    assert(!prepared_pure_closed_call_plan(&space, &source,
        fixture_atom(&source, "(refusal-pure $x)"), -1, true, &cache, &plan));
    assert(!cache.entries);
    assert(!prepared_pure_closed_call_plan(&space, &source, pure, 0, true, &cache, &plan));
    assert(!cache.entries);
    assert(prepared_pure_closed_call_plan(&space, &source, pure, -1, true, &cache, &plan));
    assert(!prepared_pure_closed_call_plan(&space, &source,
        fixture_atom(&source, "(refusal-undefined 7)"), -1, true, &cache, &plan));
    assert(!cache.entries);
    checks += 4u;

    /* Missing/incomplete classification cannot become a program refusal. */
    refusal_effect_mode = 1u;
    for (unsigned retry = 0u; retry < 2u; retry++) {
        assert(!prepared_pure_closed_call_plan(&space, &source, call, -1, true, &cache, &plan));
        assert(!cache.entries);
        checks++;
    }
    assert(refusal_effect_calls == 2u);
    refusal_effect_mode = 0u;
    refusal_cache_allocation_fail = true;
    assert(!prepared_pure_closed_call_plan(&space, &source, call, -1, true, &cache, &plan));
    assert(!refusal_cache_allocation_fail && refusal_cache_allocation_failures == 1u &&
        !cache.entries);
    checks++;

    assert(!prepared_pure_closed_call_plan(&space, &source, call, -1, false, &cache, &plan));
    assert(cache.entries && !cache.entries->next && !cache.entries->program &&
        cache.entries->admission_refused && !cache.entries->source_identity &&
        cache.entries->entry_arguments_are_values && refusal_effect_calls == 4u);
    uint64_t generation = cache.entry_generation;
    for (unsigned retry = 0u; retry < 128u; retry++) {
        Atom *fresh = atom_expr2(&source, call->expr.elems[0], atom_int(&source, retry));
        assert(!prepared_pure_closed_call_plan(&space, &source, fresh, -1,
            retry % 2u == 0u, &cache, &plan));
        assert(refusal_effect_calls == 4u && cache.entry_generation == generation &&
            !cache.entries->next);
        /* Alternating inquiries share the same direct-mapped slot. */
        assert(source_refusal_hit(&cache, &space, refusal_effect_head,
            PREPARED_PURE_ADMISSION_INQUIRY));
        assert(!source_refusal_hit(&cache, &space, refusal_effect_head,
            PREPARED_PURE_COMPILE_INQUIRY));
        assert(source_refusal_hit(&cache, &space, refusal_effect_head,
            PREPARED_PURE_ENTRY_INQUIRY));
        checks++;
    }
    for (unsigned field = 0u; field < 7u; field++) {
        assert(!prepared_pure_program_cache_has_decline_head(&cache, &space,
            field == 0u ? pure->expr.elems[0]->sym_id : refusal_effect_head,
            field == 1u ? 2u : 1u, field == 2u ? call : NULL,
            CETTA_LANGUAGE_PETTA,
            field == 3u ? CETTA_GSLT_PURE_CALL_CALL_BY_NEED : CETTA_GSLT_PURE_CALL_EAGER,
            field == 4u, field != 5u,
            field == 6u ? !active_composition_uses_total_structural_eq()
                        : active_composition_uses_total_structural_eq(),
            PREPARED_PURE_ENTRY_INQUIRY));
        checks++;
    }

    /* A true frontend refusal must never masquerade as a source assessment. */
    prepared_pure_program_cache_clear_entries(&cache);
    assert(prepared_pure_program_cache_insert_decline(&cache, &space, call,
        NULL, CETTA_LANGUAGE_PETTA, CETTA_GSLT_PURE_CALL_EAGER, false, true,
        active_composition_uses_total_structural_eq()));
    assert(!source_refusal_hit(&cache, &space, refusal_effect_head,
        PREPARED_PURE_ADMISSION_INQUIRY));
    assert(source_refusal_hit(&cache, &space, refusal_effect_head,
        PREPARED_PURE_COMPILE_INQUIRY));
    assert(source_refusal_hit(&cache, &space, refusal_effect_head,
        PREPARED_PURE_ENTRY_INQUIRY));
    checks++;

    /* The existing authority guard applies before either kind of hit. */
    uint32_t mask = library->active_mask;
    library->active_mask ^= 1u;
    assert(!source_refusal_hit(&cache, &space, refusal_effect_head,
        PREPARED_PURE_ENTRY_INQUIRY) && !cache.entries);
    library->active_mask = mask;
    assert(!prepared_pure_closed_call_plan(&space, &source, call, -1, true, &cache, &plan));
    assert(cache.entries && cache.entries->admission_refused);
    refusal_remove_equation(&space, effectful);
    Atom *replacement = refusal_add_equation(&space, &source,
        "(= (refusal-source $x) (+ $x 2))");
    assert(prepared_pure_closed_call_plan(&space, &source, call, -1, true, &cache, &plan));
    assert(!cache.entries);
    CettaCallOutcome result;
    assert(prepared_pure_closed_call_try(&space, &source, call, -1, true, true,
        &cache, &result));
    assert(result.kind == CETTA_CALL_VALUE && atom_eq(result.term, atom_int(&source, 9)));
    checks += 3u;

    /* A source change during classification cannot be stamped as current. */
    prepared_pure_program_cache_free(&cache);
    cache = (PreparedPureProgramCache){.root_space = &space};
    refusal_remove_equation(&space, replacement);
    effectful = refusal_add_equation(&space, &source,
        "(= (refusal-source $x) (readln!))");
    refusal_effect_owner = &source;
    refusal_effect_equation = effectful;
    refusal_effect_mode = 2u;
    assert(!prepared_pure_closed_call_plan(&space, &source, call, -1, true, &cache, &plan));
    assert(!refusal_effect_mode && !cache.entries);
    assert(prepared_pure_closed_call_try(&space, &source, call, -1, true, true,
        &cache, &result));
    assert(result.kind == CETTA_CALL_VALUE && atom_eq(result.term, atom_int(&source, 9)));
    checks += 2u;
    prepared_pure_program_cache_free(&cache);
    refusal_effect_head = SYMBOL_ID_NONE;
    refusal_effect_owner = NULL;
    refusal_effect_equation = NULL;
    space_free(&space);
    term_universe_free(&universe);
    arena_free(&source);
    eval_set_library_context(NULL);
    cetta_library_context_free(library);
    free(library);
    return checks;
}

static unsigned uncertified_compile_refusal_controls(void) {
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
    refusal_add_equation(&space, &source, "(= (compile-retry $x) (+ $x 1))");
    refusal_compile_head = symbol_intern_cstr(g_symbols, "compile-retry");
    for (unsigned resource = 0u; resource < 2u; resource++) {
        refusal_compile_calls = 0u;
        refusal_compile_once = true;
        refusal_compile_resource = resource != 0u;
        PreparedPureProgramCache cache = {.root_space = &space};
        CettaCallOutcome result;
        assert(!prepared_pure_closed_call_try(&space, &source,
            fixture_atom(&source, "(compile-retry 7)"), -1, true, true, &cache, &result));
        assert(refusal_compile_calls == 1u && !cache.entries &&
            !refusal_program_allocation_fail && !refusal_last_report.refusals &&
            refusal_last_report.status == (resource
                ? CETTA_PREPARED_PURE_COMPILE_RESOURCE_REFUSED
                : CETTA_PREPARED_PURE_COMPILE_UNCERTIFIED));
        assert(prepared_pure_closed_call_try(&space, &source,
            fixture_atom(&source, "(compile-retry 9)"), -1, true, true, &cache, &result));
        assert(refusal_compile_calls == 2u && result.kind == CETTA_CALL_VALUE &&
            atom_eq(result.term, atom_int(&source, 10)) &&
            refusal_last_report.status == CETTA_PREPARED_PURE_COMPILE_READY);
        prepared_pure_program_cache_free(&cache);
    }
    refusal_compile_head = SYMBOL_ID_NONE;
    space_free(&space);
    term_universe_free(&universe);
    arena_free(&source);
    eval_set_library_context(NULL);
    cetta_library_context_free(library);
    free(library);
    return 4u;
}

static unsigned he_source_refusal_controls(void) {
    CettaLibraryContext *library = calloc(1u, sizeof(*library));
    assert(library);
    cetta_library_context_init_for_language_profile(library, CETTA_LANGUAGE_HE, NULL);
    eval_set_library_context(library);
    Arena source;
    arena_init(&source);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &source);
    Space space;
    space_init_with_universe(&space, &universe);
    Atom *equation = fixture_atom(&source, "(= (refusal-he $x) (readln!))");
    assert(space_admit_atom(&space, &source, equation));
    Atom *call = fixture_atom(&source, "(refusal-he 7)");
    refusal_effect_head = call->expr.elems[0]->sym_id;
    refusal_effect_mode = 1u;
    refusal_effect_calls = 0u;
    PettaEvalMachineContext context = {.library_context = library, .fuel = -1,
        .prepared_pure_cache = {.root_space = &space}};
    OutcomeSet result;
    outcome_set_init(&result);
    unsigned checks = 0u;
    CettaEvalCompletion completion = CETTA_EVAL_COMPLETE;
    for (unsigned retry = 0u; retry < 2u; retry++) {
        assert(!petta_eval_machine_collect_closed_answers(&context, &space, &source, call, &result, &completion));
        assert(!context.prepared_pure_cache.entries && !result.len);
        checks++;
    }
    assert(refusal_effect_calls == 2u);
    refusal_effect_mode = 0u;
    assert(!petta_eval_machine_collect_closed_answers(&context, &space, &source, call, &result, &completion));
    assert(context.prepared_pure_cache.entries &&
        context.prepared_pure_cache.entries->admission_refused && !result.len);
    assert(!petta_eval_machine_collect_closed_answers(&context, &space, &source, call, &result, &completion));
    assert(refusal_effect_calls == 3u && !result.len);
    checks += 2u;
    assert(space_remove(&space, equation));
    assert(space_admit_atom(&space, &source,
        fixture_atom(&source, "(= (refusal-he $x) (+ $x 2))")));
    assert(petta_eval_machine_collect_closed_answers(&context, &space, &source, call, &result, &completion));
    assert(result.len == 1u && atom_eq(outcome_atom_materialize(&source, &result.items[0]),
        atom_int(&source, 9)));
    checks++;
    outcome_set_free(&result);
    prepared_pure_program_cache_free(&context.prepared_pure_cache);
    refusal_effect_head = SYMBOL_ID_NONE;
    space_free(&space);
    term_universe_free(&universe);
    arena_free(&source);
    eval_set_library_context(NULL);
    cetta_library_context_free(library);
    free(library);
    return checks;
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
        "(= (observed-earlier $x) (refuse-frontend $x))",
        "(= (answer-only-gate $x) (observe-demand (uncertified-projection $x)))"};
    for (unsigned i = 0u; i < sizeof(equations) / sizeof(equations[0]); i++)
        assert(space_admit_atom(&space, &source, fixture_atom(&source, equations[i])));
    const char *calls[] = {"(shared-refusal 5)", "(one-answer 5)",
        "(many-answers 5)", "(zero-answers 5)", "(observed-refusal 5)",
        "(observed-earlier 5)"};
    unsigned checks = 0u;
    for (unsigned i = 0u; i < sizeof(calls) / sizeof(calls[0]); i++) {
        CettaPreparedPureCompileReport report = {0};
        Atom *call = fixture_atom(&source, calls[i]);
        CettaPreparedPureProgram *scalar = cetta_prepared_pure_program_compile_closed(
            &space, call, CETTA_GSLT_PURE_CALL_EAGER, atom_bool, atom_expr,
            cetta_prepared_pure_constructs_expression_always, NULL, NULL,
            refusal_test_view, NULL, NULL, true, true,
            (CettaMatchDecisionSemanticIdentity){0}, &report);
        assert(cetta_prepared_pure_compile_refused(&report, &space, true) ==
            (i == 0u || i == 4u));
        assert(!!scalar == (i == 1u));
        CettaPreparedPureProgram *answers = cetta_prepared_pure_program_compile_closed_answers(
            &space, call, CETTA_GSLT_PURE_CALL_EAGER, atom_bool, atom_expr,
            cetta_prepared_pure_constructs_expression_always, NULL, NULL,
            refusal_test_view, NULL, NULL, true, true,
            (CettaMatchDecisionSemanticIdentity){0}, NULL);
        assert(!!answers == (i >= 1u && i <= 3u));
        cetta_prepared_pure_program_free(scalar);
        cetta_prepared_pure_program_free(answers);
        checks++;
    }
    {
        Atom *call = fixture_atom(&source, "(answer-only-gate 5)");
        CettaPreparedPureCompileReport report = {0};
        CettaPreparedPureProgram *program = cetta_prepared_pure_program_compile_closed(
            &space, call, CETTA_GSLT_PURE_CALL_EAGER, atom_bool, atom_expr,
            cetta_prepared_pure_constructs_expression_always, NULL, NULL,
            refusal_test_view, NULL, NULL, true, true,
            (CettaMatchDecisionSemanticIdentity){0}, &report);
        assert(!program && report.status == CETTA_PREPARED_PURE_COMPILE_UNCERTIFIED &&
            !cetta_prepared_pure_compile_refused(&report, &space, false) &&
            cetta_prepared_pure_compile_refused(&report, &space, true));
        PreparedPureProgramCache cache = {.root_space = &space};
        assert(prepared_pure_program_cache_prepare_revision(&cache, &space,
            petta_libpl_capability_revision(library->lib_prolog)));
        uint64_t generation = cache.entry_generation;
        prepared_pure_program_cache_publish_report(&cache, &space, call, NULL,
            CETTA_LANGUAGE_PETTA, CETTA_GSLT_PURE_CALL_EAGER, true, true, &report, generation);
        SymbolId head = call->expr.elems[0]->sym_id;
        assert(!prepared_pure_program_cache_has_decline_head(&cache, &space, head, 1u,
            NULL, CETTA_LANGUAGE_PETTA, CETTA_GSLT_PURE_CALL_EAGER, false, true, true,
            PREPARED_PURE_COMPILE_INQUIRY));
        assert(prepared_pure_program_cache_has_decline_head(&cache, &space, head, 1u,
            NULL, CETTA_LANGUAGE_PETTA, CETTA_GSLT_PURE_CALL_EAGER, true, true, true,
            PREPARED_PURE_COMPILE_INQUIRY));
        prepared_pure_program_cache_free(&cache);
        cache = (PreparedPureProgramCache){.root_space = &space};
        assert(prepared_pure_program_cache_prepare_revision(&cache, &space,
            petta_libpl_capability_revision(library->lib_prolog)));
        generation = cache.entry_generation;
        uint32_t mask = library->active_mask;
        library->active_mask ^= 1u;
        prepared_pure_program_cache_publish_report(&cache, &space, call, NULL,
            CETTA_LANGUAGE_PETTA, CETTA_GSLT_PURE_CALL_EAGER, true, true, &report, generation);
        assert(!cache.entries);
        library->active_mask = mask;
        prepared_pure_program_cache_free(&cache);
        program = cetta_prepared_pure_program_compile_closed_answers(
            &space, call, CETTA_GSLT_PURE_CALL_EAGER, atom_bool, atom_expr,
            cetta_prepared_pure_constructs_expression_always, NULL, NULL,
            refusal_test_view, NULL, NULL, true, true,
            (CettaMatchDecisionSemanticIdentity){0}, &report);
        assert(!program && report.status == CETTA_PREPARED_PURE_COMPILE_STABLE_REFUSED &&
            !cetta_prepared_pure_compile_refused(&report, &space, false) &&
            cetta_prepared_pure_compile_refused(&report, &space, true));
        program = cetta_prepared_pure_program_compile_closed(
            &space, fixture_atom(&source, "(answer-only-gate $open)"),
            CETTA_GSLT_PURE_CALL_EAGER, atom_bool, atom_expr,
            cetta_prepared_pure_constructs_expression_always, NULL, NULL,
            refusal_test_view, NULL, NULL, true, true,
            (CettaMatchDecisionSemanticIdentity){0}, &report);
        assert(!program && report.status == CETTA_PREPARED_PURE_COMPILE_INPUT_REFUSED &&
            !report.refusals);
        checks += 5u;
    }
    /* A source change during the read invalidates an otherwise shared
     * refusal. The callback is adversarial; no evidence may outlive it. */
    refusal_test_space = &space;
    refusal_test_owner = &source;
    refusal_test_mutate = true;
    CettaPreparedPureCompileReport report = {0};
    CettaPreparedPureProgram *changed = cetta_prepared_pure_program_compile_closed(
        &space, fixture_atom(&source, calls[0]), CETTA_GSLT_PURE_CALL_EAGER,
        atom_bool, atom_expr, cetta_prepared_pure_constructs_expression_always,
        NULL, NULL, refusal_test_view, NULL, NULL, true, true,
        (CettaMatchDecisionSemanticIdentity){0}, &report);
    assert(!changed && !report.refusals && !refusal_test_mutate &&
        report.status == CETTA_PREPARED_PURE_COMPILE_INVALIDATED);
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
    unsigned snapshots;
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
    if (head == admission->head)
        admission->snapshots++;
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
        {"(reduce $c)", PETTA_PLAN_STATIC_CALL, CETTA_CONTROL_DISPATCH},
        {"(once $c)", PETTA_PLAN_STATIC_CALL, CETTA_CONTROL_FIRST_SUCCESS},
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
    const PettaPlanNode *dispatch = petta_program_plan_current(program,
        fixture_atom(&source, "(reduce (source-probe 7))"));
    assert(dispatch && dispatch->computation.control.operation == CETTA_CONTROL_DISPATCH &&
        dispatch->computation.control.source == 0u && dispatch->child_count == 2u &&
        dispatch->children[1].role == PETTA_PLAN_STATIC_CALL &&
        dispatch->children[1].dispatch_handler);
    const PettaPlanNode *dispatch_children[] = {&dispatch->children[0], &dispatch->children[1]};
    const PettaPlanNode *projected_dispatch = petta_program_project_computation(program,
        dispatch, dispatch->computation, false, dispatch->relation_head_admitted,
        dispatch_children, 2u);
    assert(projected_dispatch && projected_dispatch->computation.control.operation == CETTA_CONTROL_DISPATCH &&
        projected_dispatch->source_position == dispatch->source_position &&
        projected_dispatch->source_debit == dispatch->source_debit &&
        projected_dispatch->children[1].dispatch_handler);
    const PettaPlanNode *held_dispatch = petta_program_plan_current(program,
        fixture_atom(&source, "(reduce $held)"));
    assert(held_dispatch && held_dispatch->computation.control.operation == CETTA_CONTROL_DISPATCH &&
        held_dispatch->children[1].role == PETTA_PLAN_VALUE);
    PettaTranslationEnvironment translation = {.host_identity = program,
        .admission_revision = 1u};
    const PettaPlanNode *quoted_dispatch = petta_program_plan_transient(program,
        fixture_atom(&source, "(quote (reduce (source-probe 7)))"), &translation);
    assert(quoted_dispatch && quoted_dispatch->output == PETTA_PLAN_OUTPUT_QUOTED_CHILD &&
        quoted_dispatch->children[1].role == PETTA_PLAN_VALUE &&
        quoted_dispatch->children[1].computation.control.operation == CETTA_CONTROL_RETURN_VALUE);
    assert(petta_program_plan_current(program, fixture_atom(&source,
        "(reduce (source-probe 7) extra)"))->computation.control.operation != CETTA_CONTROL_DISPATCH);
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
        CettaComputationPlan dispatch = petta_semantics_elaborate_computation(
            SYMBOL_ID_NONE, PETTA_FORM_REDUCE, arity);
        assert(dispatch.control.operation == (arity == 1u ? CETTA_CONTROL_DISPATCH : CETTA_CONTROL_NONE));
        assert(dispatch.invocation.operation == CETTA_INVOCATION_NONE);
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
    return 45u;
}

static unsigned source_role_elaboration_controls(void) {
    Arena arena;
    arena_init(&arena);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &arena);
    Space space;
    space_init_with_universe(&space, &universe);
    PettaProgram *program = petta_program_new();
    assert(program);
    const struct { const char *text; PettaPlanRole role; unsigned operation; } cases[] = {
        {"source-role-name", PETTA_PLAN_VALUE, CETTA_CONTROL_RETURN_VALUE},
        {"$source-role-variable", PETTA_PLAN_VALUE, CETTA_CONTROL_RETURN_VALUE},
        {"17", PETTA_PLAN_VALUE, CETTA_CONTROL_RETURN_VALUE},
        {"\"(+ 1 2)\"", PETTA_PLAN_VALUE, CETTA_CONTROL_RETURN_VALUE},
        {"()", PETTA_PLAN_DATA, CETTA_CONTROL_CONSTRUCT},
        {"(source-role-future 7)", PETTA_PLAN_DATA, CETTA_CONTROL_CONSTRUCT},
        {"($source-role-head 7)", PETTA_PLAN_DYNAMIC_CALL, CETTA_CONTROL_INVOKE},
        {"((quote source-role-head) 7)", PETTA_PLAN_DYNAMIC_CALL, CETTA_CONTROL_INVOKE},
        {"(7 8)", PETTA_PLAN_DYNAMIC_CALL, CETTA_CONTROL_INVOKE},
        {"(+ 1 2)", PETTA_PLAN_STATIC_CALL, CETTA_CONTROL_INVOKE},
        {"(: a Type)", PETTA_PLAN_DATA, CETTA_CONTROL_CONSTRUCT},
        {"(-> a Type)", PETTA_PLAN_DATA, CETTA_CONTROL_CONSTRUCT},
    };
    unsigned checks = 0u;
    for (size_t index = 0u; index < sizeof(cases) / sizeof(*cases); index++) {
        const PettaPlanNode *plan = petta_program_plan_current(
            program, fixture_atom(&arena, cases[index].text));
        assert(plan && plan->role == cases[index].role &&
            plan->computation.control.operation == cases[index].operation);
        checks++;
    }
    Atom *future = fixture_atom(&arena, "(source-role-future 7)");
    PettaTranslationEnvironment environment = {
        .host_identity = program, .admission_revision = 1u};
    const PettaPlanNode *captured = petta_program_plan_transient(program, future, &environment);
    assert(captured && captured->role == PETTA_PLAN_VALUE);
    const char *equations[] = {
        "(= (source-role-future $x) $x)",
        "(= (: $x $y) changed)",
        "(= (-> $x $y) changed)",
    };
    for (size_t index = 0u; index < 3u; index++) {
        Atom *equation = fixture_atom(&arena, equations[index]);
        const PettaPlanNode *plan = petta_program_plan_dynamic_add(program, equation);
        assert(plan);
        CettaCount at = space_length64(&space);
        space_add(&space, equation);
        assert(petta_program_note_add(program, &space, space_get_at64(&space, at), plan));
    }
    environment.admission_revision++;
    const PettaPlanNode *fresh = petta_program_plan_transient(program, future, &environment);
    assert(fresh && fresh->role == PETTA_PLAN_STATIC_CALL &&
        fresh->computation.control.operation == CETTA_CONTROL_INVOKE);
    assert(captured->role == PETTA_PLAN_VALUE &&
        captured->computation.control.operation == CETTA_CONTROL_RETURN_VALUE);
    checks += 2u;
    for (size_t index = 10u; index < 12u; index++) {
        const PettaPlanNode *plan = petta_program_plan_current(
            program, fixture_atom(&arena, cases[index].text));
        assert(plan && plan->relation_head_admitted && plan->role == PETTA_PLAN_DATA &&
            plan->computation.control.operation == CETTA_CONTROL_CONSTRUCT);
        checks++;
    }
    const PettaPlanNode *direct = petta_program_plan_current(program,
        fixture_atom(&arena, "(call (source-role-later 7))"));
    assert(direct && direct->child_count == 2u &&
        direct->children[1].role == PETTA_PLAN_STATIC_CALL &&
        !direct->children[1].relation_head_admitted);
    checks++;
    Atom *active = fixture_atom(&arena, "(+ 1 2)");
    const PettaPlanNode *list = petta_program_plan_current(program, atom_list(&arena, &active, 1u));
    assert(list && list->role == PETTA_PLAN_VALUE && list->child_count == 2u &&
        list->children[1].role == PETTA_PLAN_VALUE &&
        list->children[1].computation.control.operation == CETTA_CONTROL_RETURN_VALUE);
    const PettaPlanNode *quoted = petta_program_plan_transient(program,
        fixture_atom(&arena, "(quote (source-role-future 7))"), &environment);
    assert(quoted && quoted->child_count == 2u &&
        quoted->children[1].role == PETTA_PLAN_VALUE && !quoted->children[1].child_count);
    checks += 2u;
    Atom *value_binding = petta_semantics_lower_shared_atom(&arena,
        fixture_atom(&arena, "(let $x 7 $x)"));
    assert(value_binding && petta_semantics_is_value_let(
        value_binding->expr.elems[0]->sym_id));
    const PettaPlanNode *binding = petta_program_plan_current(program, value_binding);
    assert(binding && binding->computation.control.operation == CETTA_CONTROL_SEQUENCE &&
        binding->output == PETTA_PLAN_OUTPUT_CHILD && binding->output_child == 3u);
    checks++;
    petta_program_free(program);
    space_free(&space);
    term_universe_free(&universe);
    arena_free(&arena);
    return checks;
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
                call->expr.elems[0]->sym_id, PETTA_FORM_NONE, cases[i].arity);
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
    CettaLibraryContext *prior_library = g_library_context;
    CettaLibraryContext *dispatch_library = calloc(1u, sizeof(*dispatch_library));
    assert(dispatch_library);
    cetta_library_context_init_for_language_profile(dispatch_library, CETTA_LANGUAGE_PETTA, NULL);
    eval_set_library_context(dispatch_library);
    refusal_add_equation(&space, &source, "(= (dispatch-plan-identity $x) $x)");
    PettaEvalMachineContext dispatch_context = {.library_context = dispatch_library, .fuel = -1,
        .prepared_pure_cache = {.root_space = &space}};
    PettaMachineHost dispatch_host = petta_eval_machine_host(&dispatch_context, -1,
        false, false, false, false, false, petta_eval_machine_match_semantics(dispatch_library, false));
    dispatch_host.execute_prepared_pure_call = NULL;
    dispatch_host.open_answer_cursor = NULL;
    dispatch_host.open_relation_cursor = NULL;
    /* A retained stage owns the operation even when its physical wrapper
     * still spells the other one. The held argument contains executable data. */
    for (unsigned wrapper = 0u; wrapper < 2u; wrapper++)
    for (unsigned force = 0u; force < 2u; force++) {
        Atom *application = fixture_atom(&source, wrapper ? "(eval $held)" : "(reduce $held)");
        const PettaPlanNode *original = petta_program_plan_current(program, application);
        PettaPlanNode selected = *original;
        selected.computation.control.operation = force ? CETTA_CONTROL_FORCE : CETTA_CONTROL_DISPATCH;
        Atom *written = fixture_atom(&source, wrapper
            ? "(eval (dispatch-plan-identity 7))" : "(reduce (dispatch-plan-identity 7))");
        CettaPreparedPureExpressionView view = {0};
        CettaPreparedPureExpressionViewState projected = petta_prepared_pure_expression_view(
            written, &selected.computation, &view);
        assert(projected == (force ? CETTA_PREPARED_PURE_EXPRESSION_PROJECT
            : CETTA_PREPARED_PURE_EXPRESSION_PROJECT_SINGLE_RESULT));
        assert(view.projection_mode == (force ? CETTA_PREPARED_PURE_PROJECT_CODE
            : CETTA_PREPARED_PURE_PROJECT_APPLICATION));
        Bindings input;
        bindings_init(&input);
        assert(bindings_add_var(&input, application->expr.elems[1],
            fixture_atom(&source, "(dispatch-plan-identity (+ 1 2))")));
        assert(petta_machine_init_with_plan(&machine, &space, &source, application,
            &selected, &input, &dispatch_host));
        assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_ANSWER);
        if (!atom_eq(answer, fixture_atom(&source, force ? "3" : "(+ 1 2)"))) {
            fprintf(stderr, "dispatch plan wrapper=%u force=%u answer=", wrapper, force);
            atom_print(answer, stderr);
            fputc('\n', stderr);
        }
        assert(atom_eq(answer, fixture_atom(&source, force ? "3" : "(+ 1 2)")));
        bindings_free(&environment);
        assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_EXHAUSTED);
        bindings_free(&environment);
        petta_machine_destroy(&machine);
        bindings_free(&input);
        checks += 2u;
    }
    prepared_pure_program_cache_free(&dispatch_context.prepared_pure_cache);
    eval_set_library_context(prior_library);
    cetta_library_context_free(dispatch_library);
    free(dispatch_library);
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
        context.head = head->sym_id;
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
        "(reduce (car-atom (quote ((+ 1 2)))))",
        /* id is intrinsic to this standalone host; arbitrary held natives
         * also need their language-owned callability provider. */
        "(let $held (quote (id (+ 1 2))) (reduce $held))",
        "(let $held (quote (id (+ 1 2))) (eval $held))",
        "(reduce 7)",
        "(reduce ())",
        "(eval 7)",
        "(reduce (car-atom (superpose ((7) (7) (9)))))",
        "(once (superpose (7 8 9)))",
        "(once (empty))",
        "(once 7)",
        "(superpose ((once (superpose (7 8))) (once (superpose (9 10)))))",
        "(once (superpose ((empty) 9)))",
    };
    const char *answers[][3] = {{"(1)", "(2)", "(1)"}, {"((+ 1 2))"}, {"()"}, {NULL},
        {"(+ 1 2)"}, {"(+ 1 2)"}, {"3"}, {NULL}, {NULL}, {"7"}, {"7", "7", "9"},
        {"7"}, {NULL}, {"7"}, {"7", "9"}, {"9"}};
    const size_t counts[] = {3u, 1u, 1u, 0u, 1u, 1u, 1u, 0u, 0u, 1u, 3u,
        1u, 0u, 1u, 2u, 1u};
    const uint64_t budgets[] = {0u, 1u, 2u, 3u, 64u, 1024u};
    const size_t budget_count = sizeof(budgets) / sizeof(*budgets);
    unsigned checks = 0u;
    for (size_t query = 0u; query < sizeof(queries) / sizeof(*queries); query++) {
        CettaSourcePhase reference[128];
        size_t reference_len = 0u;
        for (unsigned generic = 0u; generic < 2u; generic++)
        for (size_t purse_index = budget_count; purse_index-- > 0u;)
        for (size_t quantum_index = budget_count; quantum_index-- > 0u;) {
            Arena arena; arena_init(&arena);
            TermUniverse universe; term_universe_init(&universe);
            term_universe_set_persistent_arena(&universe, &arena);
            Space space; space_init_with_universe(&space, &universe);
            PettaProgram *program = petta_program_new(); assert(program);
            Atom *call = fixture_atom(&arena, queries[query]);
            const PettaPlanNode *authored = petta_program_plan_current(program, call);
            assert(authored && authored->role == PETTA_PLAN_STATIC_CALL);
            PettaPlanNode plan = *authored;
            if (generic) plan.computation = (CettaComputationPlan){0};
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
                if (!atom_eq(answer, fixture_atom(&arena, answers[query][count]))) {
                    fprintf(stderr, "budget answer query=%s generic=%u purse=%" PRIu64
                        " quantum=%" PRIu64 " actual=", queries[query], generic,
                        budgets[purse_index], budgets[quantum_index]);
                    atom_print(answer, stderr);
                    fprintf(stderr, " expected=%s\n", answers[query][count]);
                }
                assert(atom_eq(answer, fixture_atom(&arena, answers[query][count])));
                count++; bindings_free(&environment);
            }
            if (purse_index + 1u == budget_count && quantum_index + 1u == budget_count && !generic) {
                assert(complete && count == counts[query]);
                reference_len = probe.length;
                memcpy(reference, probe.phases, sizeof(reference));
            }
            if (probe.length > reference_len ||
                memcmp(reference, probe.phases, probe.length * sizeof(*reference)) != 0) {
                fprintf(stderr, "source prefix query=%s generic=%u purse=%" PRIu64
                    " quantum=%" PRIu64 " actual=%zu reference=%zu phases=",
                    queries[query], generic, budgets[purse_index], budgets[quantum_index],
                    probe.length, reference_len);
                for (size_t i = 0u; i < probe.length; i++)
                    fprintf(stderr, "%u,", (unsigned)probe.phases[i]);
                fputs(" reference=", stderr);
                for (size_t i = 0u; i < reference_len; i++)
                    fprintf(stderr, "%u,", (unsigned)reference[i]);
                fputc('\n', stderr);
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
            owner, &space, lhs->expr.elems[0]->sym_id, fixture ? 1u : 0u, NULL, &reason);
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


static unsigned source_first_success_plans(void) {
    Arena arena;
    arena_init(&arena);
    Space space;
    space_init(&space);
    PettaProgram *owner = petta_program_new();
    assert(owner);
    CettaLibraryContext *prior_library = g_library_context;
    CettaLibraryContext *library = calloc(1u, sizeof(*library));
    assert(library);
    cetta_library_context_init_for_language_profile(library, CETTA_LANGUAGE_PETTA, NULL);
    eval_set_library_context(library);
    PettaEvalMachineContext context = {.library_context = library, .fuel = -1,
        .prepared_pure_cache = {.root_space = &space}};
    PettaMachineHost host = petta_eval_machine_host(&context, -1, false, false,
        false, false, false, petta_eval_machine_match_semantics(library, false));
    unsigned checks = 0u;
    for (unsigned wrapper = 0u; wrapper < 3u; wrapper++)
    for (unsigned first = 0u; first < 2u; first++) {
        Atom *written = fixture_atom(&arena, "(once (superpose (7 7 9)))");
        const PettaPlanNode *authored = petta_program_plan_current(owner, written);
        assert(authored && authored->computation.control.operation == CETTA_CONTROL_FIRST_SUCCESS);
        Atom *body = written->expr.elems[1];
        Atom *carrier = wrapper == 2u
            ? atom_expr3(&arena, atom_symbol(&arena, "scope-carrier"),
                fixture_atom(&arena, "ignored"), body)
            : atom_expr2(&arena, atom_symbol(&arena, wrapper ? "collapse" : "once"), body);
        PettaPlanNode ignored = *petta_program_plan_current(owner, fixture_atom(&arena, "ignored"));
        PettaPlanNode children[] = {*petta_plan_child(authored, 0u),
            wrapper == 2u ? ignored : *petta_plan_child(authored, 1u),
            *petta_plan_child(authored, 1u)};
        PettaPlanNode selected = *authored;
        selected.children = children;
        selected.child_count = carrier->expr.len;
        selected.computation.control.operation = first ? CETTA_CONTROL_FIRST_SUCCESS : CETTA_CONTROL_COLLECT;
        selected.computation.control.source = wrapper == 2u ? 1u : 0u;
        selected.output = first ? PETTA_PLAN_OUTPUT_CHILD : PETTA_PLAN_OUTPUT_VALUE;
        selected.output_child = selected.computation.control.source + 1u;
        Atom *expected = fixture_atom(&arena, first ? "7" : "(7 7 9)");
        const PettaPlanNode *child_views[] = {&children[0], &children[1], &children[2]};
        const PettaPlanNode *projected = petta_program_project_computation(owner,
            &selected, selected.computation, false, selected.relation_head_admitted,
            child_views, selected.child_count);
        if (first)
            assert(projected && projected->computation.control.operation == selected.computation.control.operation &&
                projected->computation.control.source == selected.computation.control.source &&
                projected->source_position == selected.source_position &&
                projected->source_debit == selected.source_debit);
        else
            assert(!projected);
        PettaMachineHost standalone = {.forms_are_written_syntax = true};
        PettaMachine machine;
        assert(petta_machine_init_with_plan(&machine, &space, &arena, carrier, &selected, NULL, &standalone));
        Atom *answer = NULL;
        Bindings environment;
        assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_ANSWER);
        assert(atom_eq(answer, expected));
        bindings_free(&environment);
        assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_EXHAUSTED);
        bindings_free(&environment);
        petta_machine_destroy(&machine);
        checks += 2u;
        OutcomeSet outcomes;
        outcome_set_init_with_owner(&outcomes, &arena);
        CettaCallOutcome end = {0};
        assert(host.evaluate_planned(host.context, &space, &arena, carrier, &selected,
            NULL, &outcomes, &end, NULL));
        if (outcomes.len != 1u || !atom_eq(outcomes.items[0].atom, expected)) {
            fprintf(stderr, "first host wrapper=%u first=%u count=%" PRIu64 "\n", wrapper, first, (uint64_t)outcomes.len);
            for (CettaCount j = 0u; j < outcomes.len; j++) atom_print(outcomes.items[j].atom, stderr);
        }
        assert(outcomes.len == 1u && atom_eq(outcomes.items[0].atom, expected));
        outcome_set_free(&outcomes);
        checks++;
        char name[48];
        snprintf(name, sizeof(name), "scope-word-%u-%u", wrapper, first);
        Atom *lhs = atom_expr(&arena, (Atom *[]){atom_symbol(&arena, name)}, 1u);
        Atom *equation = atom_expr3(&arena, atom_symbol(&arena, "="), lhs, written);
        const PettaPlanNode *definition = petta_program_plan_dynamic_add(owner, equation);
        PettaPlanNode equation_children[] = {*petta_plan_child(definition, 0u),
            *petta_plan_child(definition, 1u), selected};
        PettaPlanNode lowered = *definition;
        lowered.children = equation_children;
        equation = atom_expr3(&arena, atom_symbol(&arena, "="), lhs, carrier);
        CettaCount index = space_length64(&space);
        space_add(&space, equation);
        bool noted = petta_program_note_add(owner, &space, space_get_at64(&space, index), &lowered);
        if (!noted) fprintf(stderr, "scope note wrapper=%u first=%u\n", wrapper, first);
        assert(noted);
        const char *reason = NULL;
        CettaOpenEquationProgram *program = cetta_open_equation_program_compile(
            owner, &space, lhs->expr.elems[0]->sym_id, 0u, NULL, &reason);
        if (!program) fprintf(stderr, "scope word OEM: %s\n", reason);
        assert(program);
        CettaOpenEquationCursor *cursor = cetta_open_equation_cursor_open(
            program, &arena, NULL, 0u, NULL, NULL, 0u, NULL);
        assert(cursor);
        CettaCallOutcome returned = {0};
        Atom **values = NULL;
        assert(cetta_open_equation_cursor_next(cursor, NULL, &returned, &values) == CETTA_OPEN_EQUATION_ANSWER);
        assert(returned.kind == CETTA_CALL_VALUE && returned.result_form == CETTA_CALL_RESULT_COMPLETED_VALUE &&
            atom_eq(returned.term, expected));
        assert(cetta_open_equation_cursor_next(cursor, NULL, &returned, &values) == CETTA_OPEN_EQUATION_EXHAUSTED);
        cetta_open_equation_cursor_close(cursor);
        cetta_open_equation_program_release(program);
        checks++;
    }
    prepared_pure_program_cache_free(&context.prepared_pure_cache);
    eval_set_library_context(prior_library);
    cetta_library_context_free(library);
    free(library);
    petta_program_free(owner);
    space_free(&space);
    arena_free(&arena);
    return checks;
}

static unsigned source_case_plans(void) {
    Arena arena; arena_init(&arena);
    Space space; space_init(&space);
    PettaProgram *owner = petta_program_new(); assert(owner);
    const struct {
        const char *source;
        const char *answers[3];
        unsigned count;
    } fixtures[] = {
        {"(case 7 ((8 wrong) (7 right) ($later wrong)))", {"right"}, 1u},
        {"(case (Pair 7 7) (((Pair $a 8) wrong) ((Pair 7 $b) (Result $a $b))))",
            {"(Result $a 7)"}, 1u},
        {"(case (superpose (7 7 8)) ((7 dup) (8 end)))", {"dup", "dup", "end"}, 3u},
        {"(case 7 ((7 (quote (empty))) ($later wrong)))", {"(empty)"}, 1u},
        {"(case 7 ((7 (empty)) ($later wrong)))", {0}, 0u},
        {"(case 7 ())", {0}, 0u},
    };
    const uint64_t budgets[] = {0u, 1u, 4u, 1024u};
    unsigned checks = 0u;
    for (unsigned fixture = 0u; fixture < sizeof(fixtures) / sizeof(*fixtures); fixture++) {
        Atom *source = fixture_atom(&arena, fixtures[fixture].source);
        const PettaPlanNode *authored = petta_program_plan_current(owner, source);
        assert(authored && authored->computation.control.operation == CETTA_CONTROL_MATCH_BRANCHES);
        PettaPlanNode children[] = {*petta_plan_child(authored, 0u),
            *petta_plan_child(authored, 2u), *petta_plan_child(authored, 1u)};
        PettaPlanNode normalized = *authored;
        normalized.children = children;
        normalized.computation.control.source = 1u;
        normalized.computation.control.first = 0u;
        Atom *carrier = atom_expr3(&arena, atom_symbol(&arena, "case-carrier"),
            source->expr.elems[2], source->expr.elems[1]);
        CettaSourcePhase reference[128]; size_t reference_len = 0u;
        for (unsigned route = 0u; route < 2u; route++)
        for (unsigned purse = 4u; purse-- > 0u;)
        for (unsigned quantum = 4u; quantum-- > 0u;) {
            GenericProbe probe = {.remaining = budgets[purse], .quantum = budgets[quantum]};
            PettaMachineHost host = {.context = &probe, .permit_transition = generic_permit,
                .forms_are_written_syntax = true};
            PettaMachine machine;
            assert(petta_machine_init_with_plan(&machine, &space, &arena,
                route ? carrier : source, route ? &normalized : authored, NULL, &host));
            unsigned count = 0u; bool complete = false;
            for (unsigned attempt = 0u;; attempt++) {
                assert(attempt < 128u);
                Atom *answer = NULL; Bindings environment;
                PettaMachineStep step = petta_machine_next(&machine, &answer, &environment);
                bindings_free(&environment);
                if (step == PETTA_MACHINE_STEP_SUSPENDED) {
                    if (!probe.remaining) break;
                    probe.quantum = budgets[quantum] ? budgets[quantum] : 1u;
                    continue;
                }
                if (step == PETTA_MACHINE_STEP_EXHAUSTED) { complete = true; break; }
                if (step != PETTA_MACHINE_STEP_ANSWER)
                    fprintf(stderr, "case plan: fixture=%u route=%u step=%u\n", fixture, route, step);
                assert(step == PETTA_MACHINE_STEP_ANSWER && count < fixtures[fixture].count);
                assert(atom_alpha_eq(answer, fixture_atom(&arena, fixtures[fixture].answers[count++])));
            }
            if (!route && purse == 3u && quantum == 3u) {
                assert(complete && count == fixtures[fixture].count);
                reference_len = probe.length;
                memcpy(reference, probe.phases, reference_len * sizeof(*reference));
            }
            assert(probe.length <= reference_len);
            assert(memcmp(reference, probe.phases, probe.length * sizeof(*reference)) == 0);
            if (complete) assert(count == fixtures[fixture].count && probe.length == reference_len);
            assert(probe.spent + probe.remaining == budgets[purse]);
            petta_machine_destroy(&machine);
            checks++;
        }
        if (fixture) continue;
        Atom *lhs = fixture_atom(&arena, "(planned-case)");
        Atom *equation = atom_expr3(&arena, atom_symbol(&arena, "="), lhs, source);
        const PettaPlanNode *original = petta_program_plan_dynamic_add(owner, equation);
        assert(original);
        PettaPlanNode rhs = *petta_plan_child(original, 2u);
        PettaPlanNode rhs_children[] = {*petta_plan_child(&rhs, 0u),
            *petta_plan_child(&rhs, 2u), *petta_plan_child(&rhs, 1u)};
        rhs.children = rhs_children;
        rhs.computation.control.source = 1u;
        rhs.computation.control.first = 0u;
        PettaPlanNode equation_plan = *original;
        PettaPlanNode equation_children[] = {*petta_plan_child(original, 0u),
            *petta_plan_child(original, 1u), rhs};
        equation_plan.children = equation_children;
        CettaCount position = space_length64(&space);
        space_add(&space, atom_expr3(&arena, atom_symbol(&arena, "="), lhs, carrier));
        assert(petta_program_note_add(owner, &space, space_get_at64(&space, position), &equation_plan));
        const char *reason = NULL;
        CettaOpenEquationProgram *program = cetta_open_equation_program_compile(
            owner, &space, lhs->expr.elems[0]->sym_id, 0u, NULL, &reason);
        if (!program) fprintf(stderr, "case plan OEM: %s\n", reason);
        assert(program);
        CettaOpenEquationCursor *cursor = cetta_open_equation_cursor_open(
            program, &arena, NULL, 0u, NULL, NULL, 0u, NULL);
        assert(cursor);
        CettaCallOutcome returned = {0}; Atom **values = NULL;
        assert(cetta_open_equation_cursor_next(cursor, NULL, &returned, &values) == CETTA_OPEN_EQUATION_ANSWER);
        assert(returned.kind == CETTA_CALL_VALUE && returned.result_form == CETTA_CALL_RESULT_COMPLETED_VALUE &&
            atom_eq(returned.term, fixture_atom(&arena, "right")));
        assert(cetta_open_equation_cursor_next(cursor, NULL, &returned, &values) == CETTA_OPEN_EQUATION_EXHAUSTED);
        cetta_open_equation_cursor_close(cursor);
        cetta_open_equation_program_release(program);
        checks++;
    }
    petta_program_free(owner); space_free(&space); arena_free(&arena);
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

        Atom *selection = fixture_atom(&source, "(case 7 ((8 wrong) (7 right)))");
        const PettaPlanNode *selection_source = petta_program_plan_current(owner, selection);
        assert(selection_source->computation.control.operation == CETTA_CONTROL_MATCH_BRANCHES);
        PettaPlanNode selection_children[] = {*petta_plan_child(selection_source, 0u),
            *petta_plan_child(selection_source, 2u), *petta_plan_child(selection_source, 1u)};
        plan = *selection_source;
        plan.children = selection_children;
        plan.computation.control.source = 1u;
        plan.computation.control.first = 0u;
        carrier = atom_expr3(&source, atom_symbol(&source, "normalized-case"),
            selection->expr.elems[2], selection->expr.elems[1]);
        assert(prepared_plan_observation(&space, &source, carrier, &plan, true,
            mode, atom_symbol(&source, "right"), false));
        /* Without the word, the carrier has no selection authority. A
         * worker may refuse it or retain it as ordinary constructor data. */
        assert(prepared_plan_observation(&space, &source, carrier, &plan, false,
            mode, NULL, false) ||
            prepared_plan_observation(&space, &source, carrier, &plan, false,
                mode, carrier, false));
        plan.computation.control.first = CETTA_CONTROL_OPERAND_ABSENT;
        assert(prepared_plan_observation(&space, &source, carrier, &plan, true,
            mode, NULL, false));
        checks += 3u;

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
        InvocationAdmission admission = {.head = head->sym_id, .admit = admit, .program = program};
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

typedef struct {
    InvocationAdmission admission;
    GenericProbe budget;
} MeteredInvocation;

static bool metered_invocation_permit(void *opaque, const CettaSourceStep *step) {
    MeteredInvocation *context = opaque;
    return generic_permit(&context->budget, step);
}

static unsigned metered_invocation_controls(void) {
    const unsigned quantums[] = {0u, 1u, 2u, 128u};
    unsigned checks = 0u;
    for (unsigned captured = 0u; captured <= 3u; captured++)
    for (unsigned admit = 0u; admit < 2u; admit++)
    for (unsigned credit = 0u; credit <= 15u; credit++)
    for (unsigned physical = 0u; physical < 4u; physical++) {
        Arena source;
        arena_init(&source);
        Space space;
        space_init(&space);
        PettaProgram *program = petta_program_new();
        assert(program);
        Atom *equation = fixture_atom(&source,
            "(= (metered-bundle $a $b $c) (Bundle $a $b $c))");
        const PettaPlanNode *equation_plan = petta_program_plan_dynamic_add(program, equation);
        space_add(&space, equation);
        assert(petta_program_note_add(program, &space, space_get_at64(&space, 0u), equation_plan));
        Atom *head = atom_symbol(&source, "metered-bundle");
        Atom *values[] = {fixture_atom(&source, "(+ 1 2)"),
            atom_int(&source, 7), atom_int(&source, 9)};
        Atom *partial = petta_semantics_partial_value(&source, head, values, captured);
        assert(partial);
        Atom *fields[4] = {partial};
        for (unsigned index = captured; index < 3u; index++)
            fields[index - captured + 1u] = !index
                ? atom_expr2(&source, atom_symbol_id(&source, g_builtin_syms.quote), values[index])
                : values[index];
        Atom *call = atom_expr(&source, fields, 4u - captured);
        MeteredInvocation context = {.admission = {.head = head->sym_id,
            .admit = admit, .program = program}, .budget = {.remaining = credit,
            .quantum = quantums[physical]}};
        PettaMachineHost host = {.context = &context, .forms_are_written_syntax = true,
            .source_output_constraints = true, .dispatch_recovers_branch_locally = true,
            .admit_space_query_values = invocation_admit_values,
            .candidate_snapshot_lease = invocation_candidate_snapshot,
            .permit_transition = metered_invocation_permit};
        PettaMachine machine;
        assert(petta_machine_init_with_plan(&machine, &space, &source, call,
            petta_program_plan_current(program, call), NULL, &host));
        unsigned answers = 0u, suspensions = 0u;
        PettaMachineStep step;
        invocation_syntax_observations = 0u;
        for (;;) {
            Atom *answer = NULL;
            Bindings environment;
            step = petta_machine_next(&machine, &answer, &environment);
            if (step == PETTA_MACHINE_STEP_SUSPENDED) {
                assert(context.budget.is_blocked && suspensions++ < 128u);
                if (!context.budget.remaining)
                    break;
                context.budget.quantum = quantums[physical] ? quantums[physical] : 1u;
                continue;
            }
            if (step == PETTA_MACHINE_STEP_EXHAUSTED) {
                bindings_free(&environment);
                break;
            }
            assert(step == PETTA_MACHINE_STEP_ANSWER && !answers);
            assert(atom_eq(answer, fixture_atom(&source, "(Bundle (+ 1 2) 7 9)")));
            answers++;
            bindings_free(&environment);
        }
        /* The public invocation executes once. Its two metered entrance
         * visits precede the RHS's constructor and three value reads.
         * A supplied quote contributes one additional written occurrence. */
        unsigned publication = captured ? 11u : 12u;
        unsigned exhausted = publication + 1u;
        assert(answers == (credit >= publication ? 1u : 0u));
        assert(step == (credit >= exhausted ? PETTA_MACHINE_STEP_EXHAUSTED
            : PETTA_MACHINE_STEP_SUSPENDED));
        assert(context.budget.spent == (credit < exhausted ? credit : exhausted) &&
            context.budget.spent + context.budget.remaining == credit);
        if (credit >= exhausted) {
            assert(context.admission.inquiries == 1u && invocation_syntax_observations == 1u);
            assert(context.budget.phases[context.budget.length - 2u] == CETTA_SOURCE_PUBLISH &&
                context.budget.phases[context.budget.length - 1u] == CETTA_SOURCE_ADVANCE);
        }
        if (step == PETTA_MACHINE_STEP_SUSPENDED) {
            context.budget.remaining = 64u;
            context.budget.quantum = quantums[physical] ? quantums[physical] : 1u;
            for (;;) {
                Atom *answer = NULL;
                Bindings environment;
                step = petta_machine_next(&machine, &answer, &environment);
                if (step == PETTA_MACHINE_STEP_SUSPENDED) {
                    assert(context.budget.remaining && suspensions++ < 128u);
                    context.budget.quantum = quantums[physical] ? quantums[physical] : 1u;
                    continue;
                }
                if (step == PETTA_MACHINE_STEP_EXHAUSTED) {
                    bindings_free(&environment);
                    break;
                }
                assert(step == PETTA_MACHINE_STEP_ANSWER && !answers &&
                    atom_eq(answer, fixture_atom(&source, "(Bundle (+ 1 2) 7 9)")));
                answers++;
                bindings_free(&environment);
            }
        }
        assert(answers == 1u && context.budget.spent == exhausted &&
            context.admission.snapshots == 1u && context.admission.inquiries == 1u &&
            invocation_syntax_observations == 1u);
        petta_machine_destroy(&machine);
        space_free(&space);
        petta_program_free(program);
        arena_free(&source);
        checks++;
    }
    printf("PASS: %u metered invocation controls retain entry visits, source purses, refuelling and physical resumption\n", checks);
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

static unsigned captured_binding_projection_controls(void) {
    unsigned checks = 0u;
    const unsigned padding[] = {0u, 32u, 128u};
    for (unsigned size = 0u; size < 3u; size++) {
        Arena arena;
        arena_init_detached(&arena);
        Atom *port = atom_var_with_id(&arena, "port", fresh_var_id());
        Atom *other = atom_var_with_id(&arena, "port", fresh_var_id());
        Atom *alias = atom_var_with_id(&arena, "alias", fresh_var_id());
        Atom *box = atom_var_with_id(&arena, "box", fresh_var_id());
        assert(!var_id_eq(port->var_id, other->var_id));
        Bindings image, empty;
        bindings_init(&image);
        bindings_init(&empty);
        assert(bindings_add_var(&image, port, alias));
        assert(bindings_add_var(&image, alias, atom_int(&arena, 7)));
        Atom *capsule = bindings_capture_value(&arena, &image);
        Atom *empty_capsule = bindings_capture_value(&arena, &empty);
        assert(capsule && empty_capsule && !atom_has_vars(capsule) &&
            bindings_value_has_captured_support(capsule) &&
            !bindings_value_has_captured_support(empty_capsule) &&
            !bindings_value_has_captured_support(atom_string(&arena, "$port")));
        Bindings full;
        bindings_init(&full);
        assert(bindings_add_var(&full, port, alias));
        assert(bindings_add_var(&full, alias, atom_int(&arena, 7)));
        assert(bindings_add_var(&full, other, atom_int(&arena, 9)));
        assert(bindings_add_var(&full, box, capsule));
        for (unsigned i = 0u; i < padding[size]; i++)
            assert(bindings_add_var(&full,
                atom_var_with_id(&arena, "unused", fresh_var_id()), atom_int(&arena, i)));
        Atom *renaming = bindings_capture_renaming(&arena,
            (Atom *[]){port, alias}, (Atom *[]){alias, port}, 2u);
        Atom *shared = capsule, *empty_shared = empty_capsule;
        for (unsigned depth = 0u; depth < 40u; depth++) {
            shared = atom_expr2(&arena, shared, shared);
            empty_shared = atom_expr2(&arena, empty_shared, empty_shared);
        }
        assert(bindings_value_has_captured_support(shared) &&
            !bindings_value_has_captured_support(empty_shared));
        Atom *roots[] = {capsule, shared, box, renaming};
        for (unsigned root = 0u; root < 4u; root++) {
            Bindings projected;
            assert(bindings_project_reachable(&full, &roots[root], 1u, &projected));
            assert(atom_eq(bindings_apply_if_vars(&projected, &arena, port), atom_int(&arena, 7)));
            assert(atom_eq(bindings_apply_if_vars(&projected, &arena, alias), atom_int(&arena, 7)));
            assert(!bindings_lookup_value_id(&projected, other->var_id).skeleton);
            assert((bindings_lookup_value_id(&projected, box->var_id).skeleton != NULL) == (root == 2u));
            uint32_t marks[] = {2u, full.len, 0u, 2u};
            Bindings selected;
            assert(bindings_project_reachable_with_entry_marks(&full, &roots[root], 1u,
                marks, 4u, &selected));
            assert(marks[0] == 2u && marks[1] == (root == 2u ? 3u : 2u) &&
                marks[2] == 0u && marks[3] == 2u);
            assert(atom_eq(bindings_apply_if_vars(&selected, &arena, port), atom_int(&arena, 7)) &&
                !bindings_lookup_value_id(&selected, other->var_id).skeleton);
            bindings_free(&selected);
            bindings_free(&projected);
            checks++;
        }
        CettaFrameIdentity epoch;
        assert(cetta_frame_identity_acquire(&epoch));
        BindingsEpochRoot retained = {.atom = shared, .epoch = epoch};
        Bindings projected;
        assert(bindings_project_reachable_with_epoch_roots(&full, NULL, 0u,
            &retained, 1u, &projected));
        assert(atom_eq(bindings_apply_if_vars(&projected, &arena, port), atom_int(&arena, 7)) &&
            !bindings_lookup_value_id(&projected, other->var_id).skeleton);
        bindings_free(&projected);
        cetta_frame_identity_release(epoch);
        Bindings conflict;
        bindings_init(&conflict);
        assert(bindings_add_var(&conflict, port, atom_int(&arena, 99)));
        Atom *opaque = bindings_apply_if_vars(&conflict, &arena, capsule);
        assert(opaque->ground.ptr == capsule->ground.ptr);
        Bindings restored;
        assert(bindings_restore_captured_value(opaque, &restored));
        assert(atom_eq(bindings_apply_if_vars(&restored, &arena, port), atom_int(&arena, 7)));
        bindings_free(&restored);
        bindings_free(&conflict);
        Atom *plain = atom_symbol(&arena, "closed");
        for (unsigned position = 0u; position < 3u; position++) {
            HeCanonicalFrame frame = {.atom = position ? plain : shared,
                .type = position == 1u ? shared : plain};
            frame.live[2] = position == 2u ? shared : plain;
            assert(!he_frame_call_closed(&frame));
            frame.atom = frame.type = frame.live[2] = empty_shared;
            assert(he_frame_call_closed(&frame));
            checks++;
        }
        bindings_free(&full);
        bindings_free(&image);
        arena_free(&arena);
        checks += 3u;
    }
    return checks;
}

static Atom *he_plan_source_variable(Atom *source, const char *name) {
    if (source->kind == ATOM_VAR && strcmp(symbol_bytes(g_symbols, source->sym_id), name) == 0)
        return source;
    if (source->kind == ATOM_EXPR)
        for (CettaExprIndex i = 0u; i < source->expr.len; i++) {
            Atom *found = he_plan_source_variable(source->expr.elems[i], name);
            if (found) return found;
        }
    return NULL;
}

static void he_plan_same_results(Arena *arena, OutcomeSet *left, OutcomeSet *right,
        Atom *key, bool collection) {
    assert(left->len == right->len);
    for (CettaCount i = 0u; i < left->len; i++) {
        Atom *a = outcome_atom_materialize(arena, &left->items[i]);
        Atom *b = outcome_atom_materialize(arena, &right->items[i]);
        if (!collection) {
            assert(atom_eq(a, b));
            continue;
        }
        assert(a->kind == ATOM_EXPR && b->kind == ATOM_EXPR &&
            a->expr.len == b->expr.len);
        for (CettaExprIndex j = 0u; j < a->expr.len; j++) {
            Atom *ap = a->expr.elems[j], *bp = b->expr.elems[j];
            assert(ap->kind == ATOM_EXPR && ap->expr.len == 2u &&
                bp->kind == ATOM_EXPR && bp->expr.len == 2u &&
                atom_eq(ap->expr.elems[0], bp->expr.elems[0]));
            assert(ap->expr.elems[1]->kind == ATOM_GROUNDED &&
                ap->expr.elems[1]->ground.gkind == GV_BINDINGS &&
                bp->expr.elems[1]->kind == ATOM_GROUNDED &&
                bp->expr.elems[1]->ground.gkind == GV_BINDINGS);
        }
        for (unsigned image = 0u; image < 2u; image++) {
            Outcome *parent = image ? &right->items[i] : &left->items[i];
            Atom *pairs = image ? b : a;
            OutcomeSet restored;
            outcome_set_init_with_owner(&restored, arena);
            he_minimal_run(NULL, arena, atom_expr2(arena,
                atom_symbol(arena, "superpose-bind"), pairs), -1, &parent->env, &restored);
            assert(restored.len == pairs->expr.len);
            for (CettaCount j = 0u; j < restored.len; j++) {
                assert(key);
                if (!atom_eq(bindings_apply_if_vars(&restored.items[j].env, arena, key),
                        atom_int(arena, j < 2u ? 1 : 2)))
                    fprintf(stderr, "HE restored image=%u row=%" PRIu64 " env=%s parent=%s key=%s\n",
                        image, (uint64_t)j,
                        atom_to_string(arena, bindings_to_atom(arena, &restored.items[j].env)),
                        atom_to_string(arena, bindings_to_atom(arena, &parent->env)),
                        atom_to_string(arena, bindings_apply_if_vars(&restored.items[j].env, arena, key)));
                assert(atom_eq(bindings_apply_if_vars(&restored.items[j].env, arena, key),
                    atom_int(arena, j < 2u ? 1 : 2)));
            }
            outcome_set_free(&restored);
        }
    }
}

static unsigned he_prepared_completion_controls(void) {
    const CettaProfile *profiles[] = {cetta_profile_he_formal(),
        cetta_profile_he_compat(), cetta_profile_he_extended()};
    const char *values[] = {"(completed-box 7)", "(completed-held (readln!))"};
    const char *demands[] = {"CompletedBox", "Number", "%Undefined%", "$completed-type"};
    unsigned checks = 0u;
    for (unsigned profile = 0u; profile < 3u; profile++) {
        CettaLibraryContext *library = calloc(1u, sizeof(*library));
        assert(library);
        cetta_library_context_init_for_language_profile(library, CETTA_LANGUAGE_HE, profiles[profile]);
        eval_set_library_context(library);
        Arena arena;
        arena_init(&arena);
        TermUniverse universe;
        term_universe_init(&universe);
        term_universe_set_persistent_arena(&universe, &arena);
        Space space;
        space_init_with_universe(&space, &universe);
        EvalExecutionScope execution = {0};
        eval_execution_scope_enter(&execution, &space);
        const char *definitions[] = {"(: CompletedBox Type)",
            "(: completed-box (-> Number CompletedBox))",
            "(: completed-held (-> Atom CompletedBox))",
            "(= (completed-id $x) $x)",
            "(= (completed-many $x) $x)", "(= (completed-many $x) $x)"};
        for (unsigned index = 0u; index < sizeof(definitions) / sizeof(*definitions); index++)
            space_add(&space, fixture_atom(&arena, definitions[index]));
        prepared_fixture = true;
        prepared_mode = 0u;
        for (unsigned value_index = 0u; value_index < 2u; value_index++)
        for (unsigned marked = 0u; marked < 2u; marked++)
        for (unsigned demand = 0u; demand < 4u; demand++) {
            Atom *value = fixture_atom(&arena, values[value_index]);
            if (marked)
                value = he_answer_as_value(&arena, value);
            Atom *expected_type = fixture_atom(&arena, demands[demand]);
            Atom *expected = value;
            if (demand == 1u && profile != 1u)
                expected = atom_error(&arena, value, fixture_atom(&arena,
                    "(BadType Number CompletedBox)"));
            Bindings outer;
            bindings_init(&outer);
            Atom *shared = fixture_atom(&arena, "$completed-outer");
            assert(bindings_add_var_owned(&outer, shared, atom_int(&arena, 13)));
            prepared_calls = prepared_attempts = prepared_effects = 0u;
            for (unsigned multiple = 0u; multiple < 2u; multiple++) {
                Atom *call = atom_expr2(&arena, atom_symbol(&arena,
                    multiple ? "completed-many" : "completed-id"), value);
                OutcomeSet outputs;
                outcome_set_init_with_owner(&outputs, &arena);
                bool admitted = he_evaluated_call_answers_try(&space, &arena, call,
                    expected_type, -1, &outer, true, &outputs);
                if (admitted != !!marked)
                    fprintf(stderr, "HE prepared profile=%u value=%u marked=%u demand=%u multiple=%u admitted=%u provenance=%u\n",
                        profile, value_index, marked, demand, multiple, admitted, atom_is_he_evaluated(value));
                assert(admitted == !!marked);
                assert(outputs.len == (marked ? (multiple ? 2u : 1u) : 0u));
                for (CettaCount index = 0u; index < outputs.len; index++) {
                    assert(atom_eq(outcome_atom_materialize(&arena, &outputs.items[index]), expected));
                    assert(atom_eq(bindings_apply_if_vars(&outputs.items[index].env, &arena, shared),
                        atom_int(&arena, 13)));
                }
                assert(!prepared_calls && !prepared_attempts && !prepared_effects);
                outcome_set_free(&outputs);
                checks++;
            }
            Atom *head = atom_symbol(&arena, "completed-id");
            CettaInvocation invocation = {.head = binding_value_from_atom(head),
                .supplied = {.atoms = &value, .count = 1u}};
            CettaCallOutcome returned;
            assert(prepared_pure_closed_invocation_try(&space, &arena, &invocation,
                NULL, -1, true, true, NULL, &returned));
            assert(returned.kind == CETTA_CALL_VALUE &&
                returned.result_form == CETTA_CALL_RESULT_COMPLETED_VALUE && atom_eq(returned.term, value));
            OutcomeSet scalar;
            outcome_set_init_with_owner(&scalar, &arena);
            he_complete_prepared_answer(&space, &arena, returned.term,
                expected_type, &outer, &scalar);
            assert(scalar.len == 1u &&
                atom_eq(outcome_atom_materialize(&arena, &scalar.items[0]), expected));
            assert(!prepared_calls && !prepared_attempts && !prepared_effects);
            outcome_set_free(&scalar);
            bindings_free(&outer);
            checks++;
        }
        /* An authored typed constructor is source, not a ready argument.
         * Its held/computed policy must be admitted before any body enters. */
        OutcomeSet source;
        outcome_set_init_with_owner(&source, &arena);
        prepared_calls = prepared_attempts = 0u;
        assert(prepared_pure_closed_answers_try(&space, &arena,
            fixture_atom(&arena, "(completed-id (completed-box (readln!)))"), -1,
            NULL, NULL, true, &source) == CETTA_PREPARED_PURE_ANSWERS_DECLINED);
        assert(!source.len && !prepared_calls && !prepared_attempts);
        outcome_set_free(&source);
        checks++;
        CettaEvalEpisode episode = {0};
        CettaEvalEpisode *previous_tracker = g_eval_completion_tracker;
        g_eval_completion_tracker = &episode;
        OutcomeSet invalid, retained;
        outcome_set_init_with_owner(&invalid, &arena);
        outcome_set_init_with_owner(&retained, &arena);
        Bindings empty;
        bindings_init(&empty);
        he_complete_prepared_answer(&space, &arena, NULL,
            fixture_atom(&arena, "CompletedBox"), &empty, &invalid);
        assert(episode.interrupted && episode.completion == CETTA_EVAL_INCOMPLETE_HOST_FAILURE &&
            !invalid.len);
        outcome_set_add(&retained, fixture_atom(&arena, "(completed-box 7)"), &empty);
        he_complete_prepared_answers(&space, &arena, &retained,
            fixture_atom(&arena, "CompletedBox"), &invalid);
        assert(!invalid.len && episode.completion == CETTA_EVAL_INCOMPLETE_HOST_FAILURE);
        outcome_set_free(&retained);
        outcome_set_free(&invalid);
        g_eval_completion_tracker = previous_tracker;
        checks++;
        prepared_fixture = false;
        eval_execution_scope_leave(&execution);
        space_free(&space);
        term_universe_free(&universe);
        arena_free(&arena);
        eval_set_library_context(NULL);
        cetta_library_context_free(library);
        free(library);
    }
    return checks;
}

static unsigned he_instruction_plan_controls(void) {
    const struct {
        const char *source, *carrier, *answers[3];
        unsigned count;
        CettaControlOperation operation;
        bool collection;
    } cases[] = {
        {"(chain (eval (he-add-one $outer)) $dst (return $dst))",
         "(chain-carrier $dst (return $dst) (eval (he-add-one $outer)))",
         {"(return 100)"}, 1u, CETTA_CONTROL_SEQUENCE, false},
        {"(chain (eval (superpose (7 7 8))) $dst (return $dst))",
         "(chain-carrier $dst (return $dst) (eval (superpose (7 7 8))))",
         {"(return 7)", "(return 7)", "(return 8)"}, 3u, CETTA_CONTROL_SEQUENCE, false},
        {"(eval (he-three))", "(evalc (he-three))", {"3"},
         1u, CETTA_CONTROL_FORCE, false},
        {"(function (return 37))", "(eval (return 37))", {"37"},
         1u, CETTA_CONTROL_RETURN_SCOPE, false},
        {"(function 37)", "(scope-carrier 37)",
         {"(Error (function 37) IncorrectNumberOfArguments)"},
         1u, CETTA_CONTROL_RETURN_SCOPE, false},
        {"(chain (eval (he-three)) 7 (return 7))",
         "(chain-carrier 7 (return 7) (eval (he-three)))",
         {"(Error (chain (eval (he-three)) 7 (return 7)) IncorrectNumberOfArguments)"},
         1u, CETTA_CONTROL_SEQUENCE, false},
        {"(function (chain (eval (he-three)) $dst $dst))",
         "(eval (chain (eval (he-three)) $dst $dst))",
         {"(Error (function (chain (eval (he-three)) $dst $dst)) NoReturn)"},
         1u, CETTA_CONTROL_RETURN_SCOPE, false},
        {"(function (return (readln!)))", "(eval (return (readln!)))",
         {"(readln!)"}, 1u, CETTA_CONTROL_RETURN_SCOPE, false},
        {"(collapse-bind (eval (he-row $key)))",
         "(collect-carrier (eval (he-row $key)))", {NULL},
         1u, CETTA_CONTROL_COLLECT, true},
    };
    CettaLibraryContext *library = calloc(1u, sizeof(*library));
    assert(library);
    cetta_library_context_init_for_language_profile(library, CETTA_LANGUAGE_HE, NULL);
    eval_set_library_context(library);
    Arena arena;
    arena_init(&arena);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &arena);
    Space space;
    space_init_with_universe(&space, &universe);
    space_add(&space, fixture_atom(&arena, "(= (he-add-one 99) 100)"));
    space_add(&space, fixture_atom(&arena, "(= (he-three) 3)"));
    space_add(&space, fixture_atom(&arena, "(= (he-row 1) 7)"));
    space_add(&space, fixture_atom(&arena, "(= (he-row 1) 7)"));
    space_add(&space, fixture_atom(&arena, "(= (he-row 2) 8)"));
    unsigned checks = 0u;
    const unsigned budgets[] = {0u, 1u, 4u, 16u, 512u};
    prepared_fixture = true;
    prepared_mode = 0u;
    prepared_effects = 0u;
    for (size_t index = 0u; index < sizeof(cases) / sizeof(*cases); index++)
    for (unsigned raw = 0u; raw < 2u; raw++)
    for (size_t purse = 0u; purse < sizeof(budgets) / sizeof(*budgets); purse++) {
        Atom *source = fixture_atom(&arena, cases[index].source);
        Atom *wrapper = fixture_atom(&arena, cases[index].carrier)->expr.elems[0];
        CettaControlOperationPlan word = he_instruction_control_plan(source);
        assert(word.operation == cases[index].operation);
        Atom *carrier;
        if (word.operation == CETTA_CONTROL_SEQUENCE) {
            word.source = 2u;
            word.first = 0u;
            word.last = 1u;
            carrier = atom_expr(&arena, (Atom *[]){wrapper, expr_arg(source, 1u),
                expr_arg(source, 2u), expr_arg(source, 0u)}, 4u);
        } else
            carrier = atom_expr2(&arena, wrapper, expr_arg(source, 0u));
        Bindings prefix;
        bindings_init(&prefix);
        Atom *outer = he_plan_source_variable(source, "outer");
        Atom *destination = he_plan_source_variable(source, "dst");
        Atom *key = he_plan_source_variable(source, "key");
        if (!outer) outer = atom_var(&arena, "outer");
        if (!destination) destination = atom_var(&arena, "dst");
        assert(bindings_add_var(&prefix, outer, atom_int(&arena, 99)));
        if (!raw)
            assert(bindings_add_var(&prefix, destination, atom_int(&arena, 700)));
        OutcomeSet observed[2];
        CettaEvalEpisode episodes[2];
        for (unsigned relocated = 0u; relocated < 2u; relocated++) {
            outcome_set_init_with_owner(&observed[relocated], &arena);
            episodes[relocated] = cetta_eval_episode_begin(NULL, (int)budgets[purse]);
            g_eval_completion_tracker = &episodes[relocated];
            he_instruction_run(raw ? HE_INSTRUCTION_RAW : HE_INSTRUCTION_RUN,
                &space, &arena, relocated ? carrier : source, -1, &prefix,
                &observed[relocated], relocated ? &word : NULL, source);
            g_eval_completion_tracker = NULL;
            assert(episodes[relocated].budget_remaining + episodes[relocated].steps_spent ==
                budgets[purse]);
            assert(!episodes[relocated].interrupted && !prepared_effects);
        }
        assert(episodes[0].steps_spent == episodes[1].steps_spent &&
            episodes[0].completion == episodes[1].completion);
        he_plan_same_results(&arena, &observed[0], &observed[1], key, cases[index].collection);
        if (budgets[purse] == 512u) {
            assert(episodes[0].completion == CETTA_EVAL_COMPLETE &&
                observed[0].len == cases[index].count);
            for (CettaCount i = 0u; i < observed[0].len; i++) {
                Atom *value = outcome_atom_materialize(&arena, &observed[0].items[i]);
                if (!cases[index].collection) {
                    Atom *expected = fixture_atom(&arena, cases[index].answers[i]);
                    if (atom_is_error(expected))
                        expected = bindings_apply_if_vars(&prefix, &arena,
                            atom_error(&arena, source, expr_arg(expected, 1u)));
                    if (!atom_eq(value, expected))
                        fprintf(stderr, "HE instruction source=%s raw=%u returned=%s expected=%s\n",
                            cases[index].source, raw, atom_to_string(&arena, value),
                            cases[index].answers[i]);
                    assert(atom_eq(value, expected));
                } else {
                    assert(value->kind == ATOM_EXPR && value->expr.len == 3u);
                    for (CettaExprIndex j = 0u; j < 3u; j++)
                        assert(atom_eq(value->expr.elems[j]->expr.elems[0],
                            atom_int(&arena, j < 2u ? 7 : 8)));
                }
            }
        }
        assert(atom_eq(bindings_apply_if_vars(&prefix, &arena, outer), atom_int(&arena, 99)));
        if (!raw)
            assert(atom_eq(bindings_apply_if_vars(&prefix, &arena, destination),
                atom_int(&arena, 700)));
        bindings_free(&prefix);
        outcome_set_free(&observed[0]);
        outcome_set_free(&observed[1]);
        checks++;
    }
    for (unsigned raw = 0u; raw < 2u; raw++) {
        Atom *dynamic = fixture_atom(&arena, "($operator (he-three))");
        Bindings prefix;
        bindings_init(&prefix);
        assert(bindings_add_var(&prefix, dynamic->expr.elems[0], atom_symbol(&arena, "eval")));
        OutcomeSet observed;
        outcome_set_init_with_owner(&observed, &arena);
        he_instruction_run(raw ? HE_INSTRUCTION_RAW : HE_INSTRUCTION_RUN,
            &space, &arena, dynamic, -1, &prefix, &observed, NULL, NULL);
        assert(observed.len == 1u &&
            atom_eq(outcome_atom_materialize(&arena, &observed.items[0]), atom_int(&arena, 3)));
        outcome_set_free(&observed);
        bindings_free(&prefix);
        checks++;
    }
    for (unsigned invalid = 0u; invalid < 3u; invalid++) {
        Atom *source = fixture_atom(&arena, cases[0].source);
        Atom *carrier = fixture_atom(&arena, cases[0].carrier);
        CettaControlOperationPlan word = {CETTA_CONTROL_SEQUENCE, 2u, 0u, 1u};
        if (!invalid) word.first = word.source;
        if (invalid == 1u) word.operation = CETTA_CONTROL_MATCH_BRANCHES;
        if (invalid == 2u) word.last = CETTA_CONTROL_OPERAND_ABSENT;
        CettaEvalEpisode episode = cetta_eval_episode_begin(NULL, 32);
        OutcomeSet observed;
        outcome_set_init_with_owner(&observed, &arena);
        g_eval_completion_tracker = &episode;
        he_instruction_run(HE_INSTRUCTION_RUN, &space, &arena, carrier, -1, NULL,
            &observed, &word, source);
        g_eval_completion_tracker = NULL;
        assert(episode.interrupted && episode.completion == CETTA_EVAL_INCOMPLETE_HOST_FAILURE &&
            !observed.len && episode.steps_spent == 1u && !prepared_effects);
        outcome_set_free(&observed);
        checks++;
    }
    Atom *carrier = fixture_atom(&arena, cases[1].carrier);
    OutcomeSet data;
    outcome_set_init_with_owner(&data, &arena);
    he_minimal_run(&space, &arena, carrier, -1, NULL, &data);
    assert(data.len == 1u && atom_eq(outcome_atom_materialize(&arena, &data.items[0]), carrier));
    assert(!he_minimal_instruction(carrier) &&
        he_minimal_instruction(fixture_atom(&arena, "(chain)")) &&
        he_minimal_instruction(fixture_atom(&arena, "(function)")));
    outcome_set_free(&data);
    checks++;
    prepared_fixture = false;
    space_free(&space);
    term_universe_free(&universe);
    arena_free(&arena);
    eval_set_library_context(NULL);
    cetta_library_context_free(library);
    free(library);
    return checks;
}

static void he_canonical_plan_run(Space *space, Arena *arena, Atom *carrier,
        Atom *syntax, CettaControlOperationPlan control, const Bindings *prefix,
        OutcomeSet *target) {
    assert(!g_he_canonical_driver);
    HeCanonicalDriver driver = {.root_target = target, .arena = arena};
    g_he_canonical_driver = &driver;
    HeCanonicalFrame parent = {.space = space, .arena = arena, .fuel = -1,
        .preserve_bindings = true};
    he_canonical_instruction_push(&parent, carrier, atom_symbol(arena, "Atom"),
        prefix, true, false, target);
    HeCanonicalFrame *entry = (HeCanonicalFrame *)driver.stack.top;
    assert(entry);
    entry->control = control;
    entry->live[0] = syntax;
    while (driver.stack.top && !eval_unwinding())
        he_canonical_resume((HeCanonicalFrame *)driver.stack.top);
    while (driver.stack.top)
        he_canonical_pop();
    while (driver.free_frames) {
        void *next = *(void **)driver.free_frames;
        free(driver.free_frames);
        driver.free_frames = next;
    }
    g_he_canonical_driver = NULL;
}

/* Exercise both real completion consumers. The expected observations below
 * follow the raw instruction protocol and the declared Atom codomain of
 * function, independently of the completion decision's implementation. */
static void he_completion_consume(Space *space, Arena *arena, Atom *source,
    Atom *expected, const Bindings *prefix, bool canonical, bool allow_tail,
    OutcomeSet *outcomes) {
    if (!canonical) {
        Atom *next = NULL, *type = NULL;
        Bindings env;
        bindings_init(&env);
        if (he_eval_typed_instruction(space, arena, source, expected, -1,
                prefix, true, allow_tail, &next, &type, &env, outcomes))
            metta_call_impl(space, arena, next, type, -1, true, &env, -1, outcomes);
        bindings_free(&env);
        return;
    }
    assert(!g_he_canonical_driver);
    HeCanonicalDriver driver = {.root_target = outcomes, .arena = arena};
    g_he_canonical_driver = &driver;
    HeCanonicalFrame parent = {.space = space, .arena = arena, .fuel = -1,
        .preserve_bindings = true};
    he_canonical_instruction_push(&parent, source, expected, prefix, true,
        allow_tail, outcomes);
    while (driver.stack.top && !eval_unwinding())
        he_canonical_resume((HeCanonicalFrame *)driver.stack.top);
    while (driver.stack.top)
        he_canonical_pop();
    while (driver.free_frames) {
        void *next = *(void **)driver.free_frames;
        free(driver.free_frames);
        driver.free_frames = next;
    }
    g_he_canonical_driver = NULL;
}

static unsigned he_instruction_completion_controls(bool receipts) {
    const struct {
        const char *source;
        const char *answers[4];
        unsigned count;
        bool effect;
    } cases[] = {
        {"(eval (completion-row))", {"7", "7", "7",
            "(Error 7 (BadType Expression Number))"}, 3u, false},
        {"(eval (completion-empty))", {NULL}, 0u, false},
        {"(eval (completion-missing))", {"(completion-missing)"}, 1u, false},
        {"(function (return (completion-tick)))", {"(completion-tick)", "()",
            "()", "(completion-tick)"}, 1u, true},
        {"(function (return (function (return (+ 1 2)))))",
            {"(function (return (+ 1 2)))", "(+ 1 2)", "(+ 1 2)",
             "(function (return (+ 1 2)))"}, 1u, false},
        {"(function (return NotReducible))",
            {"(function (return NotReducible))"}, 1u, false},
        {"(function (return (Error datum fault)))", {"(Error datum fault)"}, 1u, false},
        {"(function (return (return (completion-tick))))",
            {"(return (completion-tick))"}, 1u, false},
        {"(eval (completion-open $key))", {"(Box 5)"}, 1u, false},
        {"(function (return (Box $outer $outer)))", {"(Box 99 99)"}, 1u, false},
    };
    const char *demands[] = {"Atom", "%Undefined%", "Number", "Expression"};
    const unsigned credits[] = {0u, 1u, 2u, 4u, 16u, 512u, UINT_MAX};
    const CettaProfile *profiles[] = {cetta_profile_he_formal(),
        cetta_profile_he_compat(), cetta_profile_he_extended()};
    unsigned checks = 0u;
    for (unsigned profile = 0u; profile < 3u; profile++) {
        CettaLibraryContext *library = calloc(1u, sizeof(*library));
        assert(library);
        cetta_library_context_init_for_language_profile(library, CETTA_LANGUAGE_HE,
            profiles[profile]);
        eval_set_library_context(library);
        Arena storage;
        arena_init(&storage);
        Space space;
        space_init(&space);
        const char *declarations[] = {
            "(: function (-> Atom Atom))", "(: return (-> $t $t))",
            "(= (completion-row) 7)", "(= (completion-row) 7)",
            "(= (completion-row) 9)", "(= (completion-empty) Empty)",
            "(= (completion-open 5) (Box 5))",
            "(= (completion-tick) (add-atom &self completion-effect))",
        };
        for (unsigned i = 0u; i < sizeof(declarations) / sizeof(*declarations); i++)
            space_add(&space, fixture_atom(&storage, declarations[i]));
        Registry registry;
        registry_init(&registry);
        registry_bind_id(&registry, g_builtin_syms.self, atom_space(&storage, &space));
        g_registry = &registry;
        for (unsigned shape = 0u; shape < 2u; shape++)
        for (unsigned demand = 0u; demand < 2u; demand++)
        for (unsigned frontier = 0u; frontier < 2u; frontier++) {
            Arena arena;
            arena_init(&arena);
            Atom *value = fixture_atom(&arena, shape
                ? "(function (return (completion-tick)))" : "NotReducible");
            CettaCallOutcome packet = frontier
                ? cetta_call_completed_frontier(atom_expr2(&arena, value, value))
                : cetta_call_completed_value(value);
            OutcomeSet outcomes;
            outcome_set_init_with_owner(&outcomes, &arena);
            Bindings empty;
            bindings_init(&empty);
            Atom *expression = NULL;
            CettaCount before = space_length64(&space);
            CettaEvalEpisode episode = {.budget_limited = true};
            g_eval_completion_tracker = &episode;
            assert(sequence_native_call(&space, &arena, packet, false,
                fixture_atom(&arena, demands[demand]), &empty, &outcomes, &expression));
            g_eval_completion_tracker = NULL;
            assert(!expression && outcomes.len == (frontier ? 2u : 1u));
            assert(episode.completion == CETTA_EVAL_COMPLETE && !episode.steps_spent);
            assert(space_length64(&space) == before);
            for (CettaCount i = 0u; i < outcomes.len; i++)
                assert(atom_eq(outcome_atom_materialize(&arena, &outcomes.items[i]), value));
            outcome_set_free(&outcomes);
            bindings_free(&empty);
            arena_free(&arena);
            checks++;
        }
        for (unsigned sample = 0u; sample < sizeof(cases) / sizeof(*cases); sample++)
        for (unsigned demand = 0u; demand < 4u; demand++)
        for (unsigned canonical = 0u; canonical < 2u; canonical++)
        for (unsigned tail = 0u; tail < 2u; tail++)
        for (unsigned purse = 0u; purse < sizeof(credits) / sizeof(*credits); purse++) {
            Arena arena;
            arena_init(&arena);
            Atom *source = fixture_atom(&arena, cases[sample].source);
            Bindings prefix;
            bindings_init(&prefix);
            Atom *outer = he_plan_source_variable(source, "outer");
            if (outer)
                assert(bindings_add_var(&prefix, outer, atom_int(&arena, 99)));
            OutcomeSet outcomes;
            outcome_set_init_with_owner(&outcomes, &arena);
            CettaCount before = space_length64(&space);
            bool limited = credits[purse] != UINT_MAX;
            CettaEvalEpisode episode = {.budget_limited = limited,
                .budget_initial = limited ? credits[purse] : 0u,
                .budget_remaining = limited ? credits[purse] : 0u};
            g_eval_completion_tracker = &episode;
            he_completion_consume(&space, &arena, source,
                fixture_atom(&arena, demands[demand]), &prefix, canonical, tail, &outcomes);
            g_eval_completion_tracker = NULL;
            CettaCount effects = space_length64(&space) - before;
            bool expected_effect = cases[sample].effect && (demand == 1u || demand == 2u);
            assert(effects <= (expected_effect ? 1u : 0u));
            if (episode.completion == CETTA_EVAL_COMPLETE) {
                assert(outcomes.len == cases[sample].count);
                assert(effects == (expected_effect ? 1u : 0u));
                for (CettaCount i = 0u; i < outcomes.len; i++) {
                    const char *text = cases[sample].answers[demand]
                        ? cases[sample].answers[demand] : cases[sample].answers[0];
                    if (sample == 0u && i == 2u)
                        text = demand == 3u ? "(Error 9 (BadType Expression Number))" : "9";
                    if (sample == 2u && profile == 1u)
                        text = cases[sample].source;
                    Atom *actual = outcome_atom_materialize(&arena, &outcomes.items[i]);
                    if (!atom_eq(actual, fixture_atom(&arena, text)))
                        fprintf(stderr, "HE completion profile=%u sample=%u demand=%u canonical=%u tail=%u: %s != %s\n",
                            profile, sample, demand, canonical, tail,
                            atom_to_string(&arena, actual), text);
                    assert(atom_eq(actual, fixture_atom(&arena, text)));
                    if (outer)
                        assert(atom_eq(bindings_apply_if_vars(&outcomes.items[i].env,
                            &arena, outer), atom_int(&arena, 99)));
                }
            } else
                assert(episode.completion == CETTA_EVAL_INCOMPLETE_FUEL);
            if (limited)
                assert(episode.steps_spent + episode.budget_remaining == credits[purse]);
            if (receipts) {
                printf("he-completion %u %u %u %u %u %u: status=%u spent=%" PRIu64
                    " effects=%" PRIu64 " answers=", profile, sample, demand,
                    canonical, tail, credits[purse], (unsigned)episode.completion,
                    episode.steps_spent, (uint64_t)effects);
                for (CettaCount i = 0u; i < outcomes.len; i++)
                    printf("%s%s", i ? "|" : "", atom_to_string(&arena,
                        outcome_atom_materialize(&arena, &outcomes.items[i])));
                putchar('\n');
            }
            assert(space_truncate64(&space, before));
            outcome_set_free(&outcomes);
            bindings_free(&prefix);
            arena_free(&arena);
            checks++;
        }
        g_registry = NULL;
        registry_free(&registry);
        space_free(&space);
        arena_free(&storage);
        eval_set_library_context(NULL);
        cetta_library_context_free(library);
        free(library);
    }
    return checks;
}

static unsigned he_canonical_plan_controls(void) {
    const struct { const char *source, *head, *expected; } cases[] = {
        {"(chain (he-three) $dst (return $dst))", "chain-carrier", "(return 3)"},
        {"(chain (eval (he-three)) $dst (return $dst))", "chain-carrier", "(return 3)"},
        {"(function 37)", "scope-carrier", "(Error (function 37) IncorrectNumberOfArguments)"},
        {"(eval (he-three))", "evalc", "3"},
        {"(eval (he-missing))", "evalc", "(he-missing)"},
        {"(function (return (readln!)))", "eval", "(readln!)"},
    };
    const unsigned budgets[] = {0u, 1u, 4u, 16u, 512u};
    CettaLibraryContext *library = calloc(1u, sizeof(*library));
    assert(library);
    cetta_library_context_init_for_language_profile(library, CETTA_LANGUAGE_HE, NULL);
    eval_set_library_context(library);
    Arena arena;
    arena_init(&arena);
    Space space;
    space_init(&space);
    space_add(&space, fixture_atom(&arena, "(= (he-three) 3)"));
    unsigned checks = 0u;
    prepared_fixture = true;
    prepared_mode = 0u;
    prepared_effects = 0u;
    for (unsigned index = 0u; index < 6u; index++)
    for (unsigned purse = 0u; purse < 5u; purse++) {
        Atom *source = fixture_atom(&arena, cases[index].source);
        CettaControlOperationPlan control = he_instruction_control_plan(source);
        Atom *head = atom_symbol(&arena, cases[index].head);
        Atom *carrier;
        if (control.operation == CETTA_CONTROL_SEQUENCE) {
            carrier = atom_expr(&arena, (Atom *[]){head, expr_arg(source, 1u),
                expr_arg(source, 2u), expr_arg(source, 0u)}, 4u);
            control = (CettaControlOperationPlan){CETTA_CONTROL_SEQUENCE, 2u, 0u, 1u};
        } else
            carrier = atom_expr2(&arena, head, expr_arg(source, 0u));
        Bindings prefix;
        bindings_init(&prefix);
        Atom *destination = he_plan_source_variable(source, "dst");
        if (destination)
            assert(bindings_add_var(&prefix, destination, atom_int(&arena, 700)));
        CettaEvalEpisode episodes[2];
        OutcomeSet observed[2];
        for (unsigned relocated = 0u; relocated < 2u; relocated++) {
            outcome_set_init_with_owner(&observed[relocated], &arena);
            episodes[relocated] = cetta_eval_episode_begin(NULL, (int)budgets[purse]);
            g_eval_completion_tracker = &episodes[relocated];
            he_canonical_plan_run(&space, &arena, relocated ? carrier : source, source,
                relocated ? control : he_instruction_control_plan(source), &prefix,
                &observed[relocated]);
            g_eval_completion_tracker = NULL;
            assert(!episodes[relocated].interrupted && !prepared_effects &&
                episodes[relocated].steps_spent + episodes[relocated].budget_remaining == budgets[purse]);
        }
        assert(episodes[0].steps_spent == episodes[1].steps_spent &&
            episodes[0].completion == episodes[1].completion);
        he_plan_same_results(&arena, &observed[0], &observed[1], NULL, false);
        if (budgets[purse] == 512u) {
            assert(episodes[0].completion == CETTA_EVAL_COMPLETE && observed[0].len == 1u);
            assert(atom_eq(outcome_atom_materialize(&arena, &observed[0].items[0]),
                fixture_atom(&arena, cases[index].expected)));
        }
        outcome_set_free(&observed[0]);
        outcome_set_free(&observed[1]);
        bindings_free(&prefix);
        checks++;
    }
    prepared_fixture = false;
    space_free(&space);
    arena_free(&arena);
    eval_set_library_context(NULL);
    cetta_library_context_free(library);
    free(library);
    return checks;
}

static unsigned he_instruction_source_gc_controls(void) {
    CettaLibraryContext *library = calloc(1u, sizeof(*library));
    assert(library);
    cetta_library_context_init_for_language_profile(library, CETTA_LANGUAGE_HE, NULL);
    eval_set_library_context(library);
    Arena definitions;
    arena_init_detached(&definitions);
    Space space;
    space_init(&space);
    space_add(&space, fixture_atom(&definitions, "(= (he-three) 3)"));
    unsigned checks = 0u;
    for (unsigned phase = 0u; phase < 2u; phase++) {
        assert(!g_eval_gc.roots && !g_eval_gc.imprecise_lexical_roots);
        Arena arena;
        arena_init_detached(&arena);
        ArenaMark empty = arena_mark(&arena);
        Atom *source = fixture_atom(&arena, phase ? "(function (chain (eval (he-three)) $x $x))"
                                                 : "(function 37)");
        Atom *carrier = atom_expr2(&arena, atom_symbol(&arena, "scope-carrier"),
            expr_arg(source, 0u));
        Atom *expected = atom_error(&definitions, atom_deep_copy(&definitions, source),
            atom_symbol(&definitions, phase ? "NoReturn" : "IncorrectNumberOfArguments"));
        OutcomeSet observed;
        outcome_set_init_with_owner(&observed, &arena);
        CettaOwnedFrameStack stack = {0};
        he_instruction_push(&stack, HE_INSTRUCTION_RUN, &space, &arena, carrier,
            -1, NULL, &observed, NULL, NULL);
        HeInstructionFrame *root = (HeInstructionFrame *)stack.top;
        root->control = he_instruction_control_plan(source);
        root->syntax = source;
        uint64_t before = g_eval_gc.collections;
        for (unsigned step = 0u; stack.top; step++) {
            assert(step < 64u);
            Atom *old_syntax = root->syntax;
            eval_gc_collect(&arena, empty, NULL, NULL, NULL, NULL, NULL, NULL
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
                , NULL
#endif
                );
            assert(root->syntax != old_syntax);
            he_instruction_resume(&stack);
        }
        assert(g_eval_gc.collections > before && !g_eval_gc.roots &&
            !g_eval_gc.imprecise_lexical_roots && observed.len == 1u &&
            atom_eq(outcome_atom_materialize(&arena, &observed.items[0]), expected));
        outcome_set_free(&observed);
        eval_gc_survivor_reset();
        arena_free(&arena);
        checks++;
    }
    space_free(&space);
    arena_free(&definitions);
    eval_set_library_context(NULL);
    cetta_library_context_free(library);
    free(library);
    return checks;
}


typedef struct {
    PettaEvalMachineContext owner;
    unsigned attempts;
} OemCalleeOwnerProbe;

static bool oem_callee_owner_execute(void *context, Space *space, Arena *arena,
        SymbolId head, Atom *const *arguments, uint32_t arity,
        CettaCallOutcome *outcome) {
    OemCalleeOwnerProbe *probe = context;
    probe->attempts++;
    return petta_eval_open_execute_pure_relation(&probe->owner, space, arena,
        head, arguments, arity, outcome);
}

static unsigned oem_callee_owner_controls(void) {
    CettaLibraryContext *library = calloc(1u, sizeof(*library));
    assert(library);
    cetta_library_context_init_for_language_profile(library, CETTA_LANGUAGE_PETTA, NULL);
    eval_set_library_context(library);
    Arena source;
    arena_init(&source);
    Space space;
    space_init(&space);
    const char *equations[] = {
        "(= (owner-entry-loop $n $x) (if (== $n 0) $x (owner-entry-loop (- $n 1) $x)))",
        "(= (owner-entry-bridge $x) (progn (owner-entry-loop 0 $x) (owner-entry-loop 32 7)))",
    };
    for (size_t index = 0u; index < 2u; index++) {
        Atom *equation = fixture_atom(&source, equations[index]);
        const PettaPlanNode *plan = petta_program_plan_dynamic_add(library->petta_program, equation);
        assert(plan);
        CettaCount position = space_length64(&space);
        space_add(&space, equation);
        assert(petta_program_note_add(library->petta_program, &space,
            space_get_at64(&space, position), plan));
    }
    OemCalleeOwnerProbe probe = {.owner = {.library_context = library, .fuel = -1,
        .prepared_pure_cache = {.root_space = &space}}};
    CettaOpenEquationHost host = petta_eval_open_compile_host(&probe.owner, &space, false);
    const char *reason = NULL;
    CettaOpenEquationProgram *program = cetta_open_equation_program_compile(
        library->petta_program, &space,
        symbol_intern_cstr(g_symbols, "owner-entry-bridge"), 1u, &host, &reason);
    if (!program) fprintf(stderr, "owner entry compile: %s\n", reason);
    assert(program);
    CettaOpenEquationRuntime runtime = {.context = &probe,
        .execute_pure_relation = oem_callee_owner_execute,
        .pure_relation_declined = petta_eval_machine_prepared_pure_call_declined};
    unsigned checks = 0u;
    for (unsigned repeat = 0u; repeat < 2u; repeat++) {
        Arena answers;
        arena_init(&answers);
        Atom *argument = atom_var_with_id(&source, "$owner-open", fresh_var_id());
        Atom *arguments[] = {argument};
        CettaOpenEquationCursor *cursor = cetta_open_equation_cursor_open(program,
            &answers, arguments, 1u, NULL, arguments, 1u, &runtime);
        assert(cursor);
        CettaCallOutcome returned = cetta_call_failure();
        assert(cetta_open_equation_cursor_next(cursor, arguments, &returned, NULL) ==
            CETTA_OPEN_EQUATION_ANSWER);
        assert(returned.kind == CETTA_CALL_VALUE &&
            returned.result_form == CETTA_CALL_RESULT_COMPLETED_VALUE &&
            atom_eq(returned.term, atom_int(&answers, 7)) && probe.attempts == repeat + 1u);
        assert(cetta_open_equation_cursor_next(cursor, arguments, &returned, NULL) ==
            CETTA_OPEN_EQUATION_EXHAUSTED);
        checks += 3u;
        cetta_open_equation_cursor_close(cursor);
        arena_free(&answers);
    }
    cetta_open_equation_program_release(program);
    prepared_pure_program_cache_free(&probe.owner.prepared_pure_cache);
    space_free(&space);
    arena_free(&source);
    eval_set_library_context(NULL);
    cetta_library_context_free(library);
    free(library);
    return checks;
}

typedef struct {
    unsigned mode, prepares, calls, releases;
} LexicalPacketProbe;

static void *lexical_packet_prepare(void *context, Space *space, Arena *owner,
        Atom *callable, uint32_t arity) {
    (void)space;
    (void)owner;
    assert(callable && arity == 1u);
    LexicalPacketProbe *probe = context;
    probe->prepares++;
    return probe;
}

static bool lexical_packet_execute(void *context, void *prepared, Arena *arena,
        Atom *const *arguments, uint32_t arity, CettaCallOutcome *outcome) {
    LexicalPacketProbe *probe = context;
    assert(prepared == probe && arity == 1u && arguments[0]);
    probe->calls++;
    switch (probe->mode) {
    case 0u: *outcome = cetta_call_completed_value(fixture_atom(arena, "(Error payload marker)")); break;
    case 1u: *outcome = cetta_call_completed_value(fixture_atom(arena, "Empty")); break;
    case 2u: *outcome = cetta_call_completed_value(fixture_atom(arena, "(readln!)")); break;
    case 3u: *outcome = cetta_call_failure(); break;
    case 4u: *outcome = cetta_call_interrupted(); break;
    case 5u: *outcome = cetta_call_raised(fixture_atom(arena, "(Error payload marker)")); break;
    case 6u: *outcome = cetta_call_value(fixture_atom(arena, "(readln!)")); break;
    case 7u: {
        Atom *shared = atom_var_with_id(arena, "$pending-shared", fresh_var_id());
        *outcome = cetta_call_suspended(shared, atom_expr2(arena, shared, shared));
        break;
    }
    case 8u: *outcome = cetta_call_completed_value(NULL); break;
    default: return false;
    }
    return true;
}

static void lexical_packet_release(void *context, void *prepared) {
    LexicalPacketProbe *probe = context;
    assert(prepared == probe);
    probe->releases++;
}

/* The admitted test host supplies each return protocol independently of
 * the scalar compiler. Real executor admission is checked separately. */
static unsigned lexical_packet_controls(void) {
    CettaLibraryContext *library = calloc(1u, sizeof(*library));
    assert(library);
    cetta_library_context_init_for_language_profile(library, CETTA_LANGUAGE_PETTA, NULL);
    eval_set_library_context(library);
    Arena source;
    arena_init(&source);
    Space space;
    space_init(&space);
    Atom *equation = fixture_atom(&source, "(= (lexical-packet-driver $f) ($f 1))");
    const PettaPlanNode *plan = petta_program_plan_dynamic_add(library->petta_program, equation);
    assert(plan);
    space_add(&space, equation);
    assert(petta_program_note_add(library->petta_program, &space,
        space_get_at64(&space, 0u), plan));
    const char *reason = NULL;
    CettaOpenEquationProgram *program = cetta_open_equation_program_compile(
        library->petta_program, &space,
        symbol_intern_cstr(g_symbols, "lexical-packet-driver"), 1u, NULL, &reason);
    if (!program) fprintf(stderr, "lexical packet compile: %s\n", reason);
    assert(program);
    Atom *callable = petta_capture_lambda(&source,
        fixture_atom(&source, "(|-> ($x) $x)"), NULL);
    assert(callable);
    unsigned checks = 0u;
    for (unsigned mode = 0u; mode < 10u; mode++) {
        Arena answers;
        arena_init(&answers);
        LexicalPacketProbe probe = {.mode = mode};
        CettaOpenEquationRuntime runtime = {.context = &probe,
            .prepare_pure_callable = lexical_packet_prepare,
            .execute_pure_callable = lexical_packet_execute,
            .release_pure_callable = lexical_packet_release};
        Atom *arguments[] = {callable};
        CettaOpenEquationCursor *cursor = cetta_open_equation_cursor_open(program,
            &answers, arguments, 1u, NULL, NULL, 0u, &runtime);
        assert(cursor);
        CettaCallOutcome returned = cetta_call_failure();
        CettaOpenEquationStep step = cetta_open_equation_cursor_next(cursor, NULL, &returned, NULL);
        assert(probe.prepares == 1u && probe.calls == 1u);
        if (mode < 3u) {
            const char *expected[] = {"(Error payload marker)", "Empty", "(readln!)"};
            assert(step == CETTA_OPEN_EQUATION_ANSWER && returned.kind == CETTA_CALL_VALUE &&
                returned.result_form == CETTA_CALL_RESULT_COMPLETED_VALUE &&
                atom_eq(returned.term, fixture_atom(&answers, expected[mode])));
            assert(cetta_open_equation_cursor_next(cursor, NULL, &returned, NULL) ==
                CETTA_OPEN_EQUATION_EXHAUSTED && probe.calls == 1u);
        } else if (mode == 3u) {
            assert(step == CETTA_OPEN_EQUATION_EXHAUSTED);
        } else if (mode == 5u) {
            assert(step == CETTA_OPEN_EQUATION_RAISE && returned.kind == CETTA_CALL_RAISED &&
                atom_eq(returned.term, fixture_atom(&answers, "(Error payload marker)")));
        } else if (mode == 4u || mode == 6u || mode == 7u || mode == 9u) {
            assert(step == CETTA_OPEN_EQUATION_HOST);
            if (mode != 9u) {
                CettaCallOutcome packet;
                assert(cetta_open_equation_cursor_host_return(cursor, &packet));
                assert(packet.kind == (mode == 4u ? CETTA_CALL_INTERRUPTED
                    : mode == 6u ? CETTA_CALL_VALUE : CETTA_CALL_SUSPENDED));
                if (mode == 7u)
                    assert(packet.delayed && packet.delayed->expr.len == 2u &&
                        packet.term->kind == ATOM_VAR &&
                        atom_eq(packet.term, packet.delayed->expr.elems[0]) &&
                        atom_eq(packet.term, packet.delayed->expr.elems[1]));
            }
        } else {
            CettaOpenEquationStep expected = CETTA_OPEN_EQUATION_FAULT;
            if (step != expected)
                fprintf(stderr, "lexical packet mode=%u step=%u handoff=%u\n", mode,
                    step, cetta_open_equation_cursor_handoff(cursor));
            assert(step == expected);
            assert(cetta_open_equation_cursor_handoff(cursor) ==
                CETTA_OPEN_EQUATION_HANDOFF_SERVICE_ERROR);
        }
        cetta_open_equation_cursor_close(cursor);
        assert(probe.releases == 1u && probe.calls == 1u);
        checks += 3u;
        arena_free(&answers);
    }
    cetta_open_equation_program_release(program);
    space_free(&space);
    arena_free(&source);
    eval_set_library_context(NULL);
    cetta_library_context_free(library);
    free(library);
    return checks;
}

typedef struct {
    uint32_t arity;
    Atom *first, *second;
    unsigned prepares, calls, releases, inquiries;
} ProgramOperandProbe;

static void program_operand_check(ProgramOperandProbe *probe,
        Atom *const *operands, uint32_t arity) {
    assert(arity == probe->arity);
    bool open = probe->first->kind == ATOM_VAR;
    Atom *shared = open ? operands[0] : NULL;
    if (open)
        assert(shared && shared->kind == ATOM_VAR);
    for (uint32_t index = 0u; index < arity; index++) {
        Atom *value = operands[index];
        if (index % 3u == 1u) {
            assert(value && value->kind == ATOM_EXPR && value->expr.len == 3u &&
                atom_is_symbol(value->expr.elems[0], "Box"));
            if (open) {
                Atom *first = value->expr.elems[2], *second = value->expr.elems[1];
                assert(first->kind == ATOM_VAR && second->kind == ATOM_VAR &&
                    var_id_eq(first->var_id, shared->var_id) &&
                    var_id_eq(second->var_id, shared->var_id) ==
                        var_id_eq(probe->second->var_id, probe->first->var_id));
            } else {
                assert(atom_eq(value->expr.elems[1], probe->second) &&
                    atom_eq(value->expr.elems[2], probe->first));
            }
        } else {
            assert(open ? value->kind == ATOM_VAR && var_id_eq(value->var_id, shared->var_id)
                : atom_eq(value, probe->first));
        }
    }
}

static void *program_operand_prepare(void *context, Space *space, Arena *arena,
        Atom *callable, uint32_t arity) {
    (void)space; (void)arena;
    ProgramOperandProbe *probe = context;
    CettaExprLen expected = 0u;
    assert(petta_semantics_lambda_arity(callable, &expected) && expected == arity);
    assert(arity == probe->arity);
    probe->prepares++;
    return probe;
}

static bool program_operand_execute(void *context, void *prepared, Arena *arena,
        Atom *const *operands, uint32_t arity, CettaCallOutcome *outcome) {
    ProgramOperandProbe *probe = context;
    assert(prepared == probe);
    program_operand_check(probe, operands, arity);
    probe->calls++;
    *outcome = cetta_call_completed_value(fixture_atom(arena, "(readln!)"));
    return true;
}

static void program_operand_release(void *context, void *prepared) {
    ProgramOperandProbe *probe = context;
    assert(prepared == probe);
    probe->releases++;
}

static bool program_operand_callable(void *context, Space *space, Atom *call) {
    (void)space;
    ProgramOperandProbe *probe = context;
    assert(call && call->kind == ATOM_EXPR && call->expr.len == probe->arity + 1u &&
        atom_is_symbol(call->expr.elems[0], "operand-inert"));
    program_operand_check(probe, call->expr.elems + 1u, probe->arity);
    probe->inquiries++;
    return false;
}

static unsigned program_operand_controls(void) {
    CettaLibraryContext *library = calloc(1u, sizeof(*library));
    assert(library);
    cetta_library_context_init_for_language_profile(library, CETTA_LANGUAGE_PETTA, NULL);
    eval_set_library_context(library);
    Arena source;
    arena_init(&source);
    unsigned checks = 0u;
    const uint32_t arities[] = {0u, 1u, 3u, 64u, 65u};
    for (size_t shape = 0u; shape < sizeof(arities) / sizeof(arities[0]); shape++) {
        uint32_t arity = arities[shape];
        char equation[4096] = "(= (operand-driver $f $p $q) ($f";
        char lexical[4096] = "(|-> (";
        for (uint32_t index = 0u; index < arity; index++) {
            strcat(equation, index % 3u == 1u ? " (Box $q $p)" : " $p");
            char name[32];
            snprintf(name, sizeof(name), "%s$a%u", index ? " " : "", index);
            strcat(lexical, name);
        }
        strcat(equation, "))");
        strcat(lexical, ") ignored)");
        Space space;
        space_init(&space);
        Atom *definition = fixture_atom(&source, equation);
        const PettaPlanNode *plan = petta_program_plan_dynamic_add(library->petta_program, definition);
        assert(plan);
        space_add(&space, definition);
        assert(petta_program_note_add(library->petta_program, &space,
            space_get_at64(&space, 0u), plan));
        const char *reason = NULL;
        CettaOpenEquationProgram *program = cetta_open_equation_program_compile(
            library->petta_program, &space, symbol_intern_cstr(g_symbols, "operand-driver"),
            3u, NULL, &reason);
        if (!program) fprintf(stderr, "operand driver arity=%u: %s\n", arity, reason);
        assert(program);
        unsigned modes = arity == 3u || arity == 65u ? 7u : arity ? 3u : 2u;
        for (unsigned mode = 0u; mode < modes; mode++) {
            unsigned kind = mode < 3u ? mode : (mode - 3u) % 2u;
            Arena answers;
            arena_init(&answers);
            ProgramOperandProbe probe = {.arity = arity,
                .first = fixture_atom(&source, "(Item first)"),
                .second = fixture_atom(&source, "(Item second)")};
            if (mode >= 3u) {
                probe.first = atom_var_with_id(&source, "$operand-shared", fresh_var_id());
                probe.second = mode >= 5u ? probe.first
                    : atom_var_with_id(&source, "$operand-distinct", fresh_var_id());
            }
            Atom *head = kind == 0u ? atom_int(&source, 41)
                : kind == 1u ? atom_symbol(&source, "operand-inert")
                : petta_capture_lambda(&source, fixture_atom(&source, lexical), NULL);
            assert(head);
            CettaOpenEquationRuntime runtime = {.context = &probe,
                .prepare_pure_callable = program_operand_prepare,
                .execute_pure_callable = program_operand_execute,
                .release_pure_callable = program_operand_release,
                .head_callable = program_operand_callable};
            Atom *arguments[] = {head, probe.first, probe.second};
            Atom *query_variables[] = {probe.first, probe.second};
            uint32_t query_count = mode >= 3u ? mode >= 5u ? 1u : 2u : 0u;
            CettaOpenEquationCursor *cursor = cetta_open_equation_cursor_open(
                program, &answers, arguments, 3u, NULL,
                query_count ? query_variables : NULL, query_count, &runtime);
            assert(cursor);
            CettaCallOutcome returned = cetta_call_failure();
            CettaOpenEquationStep step = cetta_open_equation_cursor_next(cursor,
                query_count ? query_variables : NULL, &returned, NULL);
            if (kind == 2u && arity > 64u) {
                assert(step == CETTA_OPEN_EQUATION_HOST && !probe.calls && !probe.prepares);
            } else {
                assert(step == CETTA_OPEN_EQUATION_ANSWER && returned.kind == CETTA_CALL_VALUE &&
                    returned.result_form == CETTA_CALL_RESULT_COMPLETED_VALUE);
                if (kind == 2u) {
                    assert(atom_eq(returned.term, fixture_atom(&answers, "(readln!)")) &&
                        probe.calls == 1u && probe.prepares == 1u && !probe.inquiries);
                } else {
                    assert(returned.term->kind == ATOM_EXPR &&
                        returned.term->expr.len == arity + 1u &&
                        atom_eq(returned.term->expr.elems[0], head));
                    program_operand_check(&probe, returned.term->expr.elems + 1u, arity);
                    assert(probe.inquiries == (kind == 1u) && !probe.calls && !probe.prepares);
                }
                assert(cetta_open_equation_cursor_next(cursor,
                    query_count ? query_variables : NULL, &returned, NULL) ==
                    CETTA_OPEN_EQUATION_EXHAUSTED && probe.calls == (kind == 2u));
            }
            cetta_open_equation_cursor_close(cursor);
            assert(probe.releases == probe.prepares);
            arena_free(&answers);
            checks += 4u;
        }
        cetta_open_equation_program_release(program);
        space_free(&space);
    }
    arena_free(&source);
    eval_set_library_context(NULL);
    cetta_library_context_free(library);
    free(library);
    printf("PASS: %u common-plan operand controls preserve ordered fields, sharing, data readback and wide refusal\n", checks);
    return checks;
}

/* Real lexical compilation and native entry, followed by the search
 * machine's ordinary host completion of the retained OEM packet. */
static unsigned lexical_native_tail_controls(void) {
    CettaLibraryContext *library = calloc(1u, sizeof(*library));
    assert(library);
    cetta_library_context_init_for_language_profile(library, CETTA_LANGUAGE_PETTA, NULL);
    eval_set_library_context(library);
    Arena source, owner;
    arena_init(&source);
    arena_init(&owner);
    Space space;
    space_init(&space);
    const char *equations[] = {
        "(= (lexical-native-driver) (let $f (|-> ($x) (__cetta_lib_json_parse_v1 $x)) (let $v ($f 1) (pair $v $v))))",
        "(= (lexical-native-body) (let $f (|-> ($x) (native-body-inner $x (__cetta_lib_json_parse_v1 $x))) (let $v ($f 1) (pair $v $v))))",
        "(= (native-body-inner $x $n) (Inner $x $n $n))",
        "(= (lexical-native-siblings) (let $f (|-> ($x) (native-body-inner $x (__cetta_lib_json_parse_v1 $x))) (let $left ($f 1) (let $right ($f 2) (Siblings $left $right)))))",
    };
    for (size_t i = 0u; i < 4u; i++) {
        Atom *equation = fixture_atom(&source, equations[i]);
        const PettaPlanNode *plan = petta_program_plan_dynamic_add(library->petta_program, equation);
        assert(plan);
        space_add(&space, equation);
        assert(petta_program_note_add(library->petta_program, &space,
            space_get_at64(&space, i), plan));
    }
    prepared_head = symbol_intern_cstr(g_symbols, "__cetta_lib_json_parse_v1");
    prepared_fixture = true;
    unsigned checks = 0u;
    PettaEvalMachineContext compile_context = {.library_context = library, .fuel = -1};
    const char *refusals[] = {
        "(|-> ($x) (+ (__cetta_lib_json_parse_v1 $x) 1))",
        "(|-> ($x) (let $v (__cetta_lib_json_parse_v1 $x) (+ $v 1)))",
        "(|-> ($x) (if (__cetta_lib_json_parse_v1 $x) 1 2))",
        "(|-> ($x) (__cetta_lib_json_parse_v1 (__cetta_lib_json_parse_v1 $x)))",
    };
    prepared_calls = prepared_attempts = 0u;
    for (size_t index = 0u; index < sizeof(refusals) / sizeof(*refusals); index++) {
        Atom *callable = petta_capture_lambda(&source, fixture_atom(&source, refusals[index]), NULL);
        PettaPreparedCallable *plan = petta_eval_open_prepare_pure_callable(
            &compile_context, &space, &owner, callable, 1u);
        if (index == 1u) {
            assert(callable && !plan && !prepared_calls && !prepared_attempts);
            checks += 2u;
            continue;
        }
        assert(callable && plan && plan->body_authority);
        Atom *args[] = {atom_int(&source, 1)};
        CettaCallOutcome refused;
        assert(!petta_eval_open_execute_pure_callable(&compile_context, plan,
            &owner, args, 1u, &refused));
        assert(!prepared_calls && !prepared_attempts);
        petta_eval_open_release_pure_callable(&compile_context, plan);
        checks += 2u;
    }
    Atom *callable = petta_capture_lambda(&source, fixture_atom(&source,
        "(|-> ($x) (if (== (+ $x 1) 2) (__cetta_lib_json_parse_v1 (+ $x 1)) 99))"), NULL);
    assert(callable);
    void *compiled = petta_eval_open_prepare_pure_callable(
        &compile_context, &space, &owner, callable, 1u);
    assert(compiled && !prepared_calls && !prepared_attempts);
    const int budgets[] = {0, 1, 64};
    for (size_t index = 0u; index < 3u; index++) {
        compile_context.fuel = budgets[index];
        assert(!petta_eval_open_prepare_pure_callable(
            &compile_context, &space, &owner, callable, 1u));
        assert(!prepared_calls && !prepared_attempts);
        checks += 2u;
    }
    compile_context.fuel = -1;
    Atom *arguments[] = {atom_int(&source, 1)};
    CettaCallOutcome packet;
    for (unsigned mode = 0u; mode <= 23u; mode++) {
        if (mode == 11u || mode == 21u || mode == 22u)
            continue;
        prepared_mode = mode;
        prepared_calls = prepared_attempts = prepared_effects = 0u;
        assert(petta_eval_open_execute_pure_callable(&compile_context, compiled,
            &owner, arguments, 1u, &packet));
        assert(prepared_calls == 1u && prepared_attempts == 1u && !prepared_effects);
        if (mode == 2u || mode == 17u)
            assert(packet.kind == CETTA_CALL_VALUE &&
                packet.result_form == CETTA_CALL_RESULT_COMPLETED_FRONTIER);
        if (mode == 23u)
            assert(packet.kind == CETTA_CALL_SUSPENDED && packet.delayed &&
                packet.term == packet.delayed->expr.elems[0] &&
                packet.term == packet.delayed->expr.elems[1]);
        checks += 2u;
    }
    arguments[0] = atom_int(&source, 2);
    prepared_calls = prepared_attempts = 0u;
    assert(petta_eval_open_execute_pure_callable(&compile_context, compiled,
        &owner, arguments, 1u, &packet));
    assert(packet.kind == CETTA_CALL_VALUE && atom_eq(packet.term, atom_int(&owner, 99)) &&
        !prepared_calls && !prepared_attempts);
    checks += 2u;
    petta_eval_open_release_pure_callable(&compile_context, compiled);
    Atom *matcher = petta_capture_lambda(&source, fixture_atom(&source,
        "(|-> ($x) (let (Pair $a $b) $x (__cetta_lib_json_parse_v1 $a)))"), NULL);
    prepared_calls = prepared_attempts = 0u;
    assert(!petta_eval_open_prepare_pure_callable(
        &compile_context, &space, &owner, matcher, 1u));
    assert(!prepared_calls && !prepared_attempts);
    checks += 2u;

    const unsigned modes[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 10u,
        14u, 15u, 17u, 18u, 19u, 20u, 25u, 27u};
    for (size_t index = 0u; index < sizeof(modes) / sizeof(*modes); index++)
    for (unsigned body_route = 0u; body_route < 2u; body_route++)
    for (unsigned stop_early = 0u; stop_early < 2u; stop_early++) {
        if ((modes[index] == 25u || modes[index] == 27u) && !body_route)
            continue;
        prepared_mutation_arena = &source;
        assert(cetta_library_petta_translator_rule_set(library, prepared_head, false));
        prepared_mode = modes[index];
        prepared_calls = prepared_attempts = prepared_effects = 0u;
        Arena execution;
        arena_init_detached(&execution);
        PettaEvalMachineContext context = {.fuel = -1, .library_context = library,
            .semantic_authority_epoch = 1u, .prepared_pure_cache = {.root_space = &space}};
        PettaMachineHost host = petta_eval_machine_host(&context, -1,
            false, false, false, false, false,
            petta_eval_machine_match_semantics(library, false));
        Atom *call = fixture_atom(&execution, body_route ? "(lexical-native-body)" : "(lexical-native-driver)");
        PettaMachine machine;
        assert(petta_machine_init(&machine, &space, &execution, call, NULL, &host));
        unsigned count = 0u;
        PettaMachineStep step;
        for (;;) {
            Atom *answer = NULL;
            Bindings bindings;
            step = petta_machine_next(&machine, &answer, &bindings);
            if (step == PETTA_MACHINE_STEP_ANSWER) {
                assert(++count <= 3u);
                Atom *value = bindings_apply_if_vars(&bindings, &execution, answer);
                if (prepared_mode == 4u)
                    assert(petta_machine_last_answer_raised(&machine) && atom_is_error(value));
                else {
                    assert(value && value->kind == ATOM_EXPR && value->expr.len == 3u &&
                        atom_is_symbol(value->expr.elems[0], "pair") &&
                        atom_eq(value->expr.elems[1], value->expr.elems[2]));
                    Atom *payload = value->expr.elems[1];
                    if (body_route) {
                        assert(payload->kind == ATOM_EXPR && payload->expr.len == 4u &&
                            atom_is_symbol(payload->expr.elems[0], "Inner") &&
                            atom_eq(payload->expr.elems[1], atom_int(&execution, 1)) &&
                            atom_eq(payload->expr.elems[2], payload->expr.elems[3]));
                        payload = payload->expr.elems[2];
                    }
                    if (prepared_mode == 1u || prepared_mode == 17u || prepared_mode == 18u)
                        assert(atom_eq(payload, fixture_atom(&execution,
                            prepared_mode == 1u ? "(function (return (readln!)))"
                            : prepared_mode == 17u ? "(+ (readln!) 1)" : "(readln!)")));
                    if (prepared_mode == 2u)
                        assert(atom_eq(payload, atom_int(&execution, count == 3u ? 9 : 7)));
                    if (prepared_mode == 6u)
                        assert(atom_is_error(payload));
                    if (prepared_mode == 7u)
                        assert(atom_is_symbol(payload, "Empty"));
                }
            }
            bindings_free(&bindings);
            if (step != PETTA_MACHINE_STEP_ANSWER || stop_early)
                break;
        }
        if (prepared_calls != 1u || prepared_attempts != 1u)
            fprintf(stderr, "lexical native mode=%u body=%u calls=%u attempts=%u count=%u step=%u\n",
                prepared_mode, body_route, prepared_calls, prepared_attempts, count, step);
        assert(prepared_calls == 1u && prepared_attempts == 1u);
        if (!(stop_early && count)) {
            unsigned expected = prepared_mode == 2u ? 3u
                : prepared_mode == 3u || prepared_mode == 8u || prepared_mode == 10u ||
                  prepared_mode == 15u || prepared_mode == 20u || prepared_mode == 25u || prepared_mode == 27u ? 0u
                : prepared_mode == 14u || prepared_mode == 17u || prepared_mode == 19u ? 2u : 1u;
            assert(count == expected);
            assert(step == (prepared_mode == 8u || prepared_mode == 20u ? PETTA_MACHINE_STEP_SUSPENDED
                : prepared_mode == 10u ? PETTA_MACHINE_STEP_HOST_ERROR
                : prepared_mode == 25u || prepared_mode == 27u ? PETTA_MACHINE_STEP_INVALIDATED : PETTA_MACHINE_STEP_EXHAUSTED));
        }
        if (step != PETTA_MACHINE_STEP_ANSWER) {
            Atom *again = NULL;
            Bindings again_bindings;
            assert(petta_machine_next(&machine, &again, &again_bindings) == step);
            bindings_free(&again_bindings);
            assert(prepared_calls == 1u);
        }
        unsigned effects = prepared_effects;
        petta_machine_destroy(&machine);
        assert(prepared_calls == 1u && prepared_effects == effects);
        assert(prepared_effects == (prepared_mode == 14u || prepared_mode == 20u ? 2u
            : prepared_mode == 5u ? 1u : 0u));
        prepared_pure_program_cache_free(&context.prepared_pure_cache);
        arena_free(&execution);
        assert(!petta_eval_take_raise());
        checks += 4u;
    }
    for (unsigned early = 0u; early < 2u; early++) {
        prepared_mode = 2u;
        prepared_calls = prepared_attempts = prepared_effects = 0u;
        Arena execution;
        arena_init_detached(&execution);
        PettaEvalMachineContext context = {.fuel = -1, .library_context = library,
            .semantic_authority_epoch = 1u, .prepared_pure_cache = {.root_space = &space}};
        PettaMachineHost host = petta_eval_machine_host(&context, -1,
            false, false, false, false, false,
            petta_eval_machine_match_semantics(library, false));
        PettaMachine machine;
        assert(petta_machine_init(&machine, &space, &execution,
            fixture_atom(&execution, "(lexical-native-siblings)"), NULL, &host));
        unsigned counts[2][2] = {{0}}, count = 0u;
        PettaMachineStep step;
        for (;;) {
            Atom *answer = NULL;
            Bindings bindings;
            step = petta_machine_next(&machine, &answer, &bindings);
            if (step != PETTA_MACHINE_STEP_ANSWER) {
                bindings_free(&bindings);
                break;
            }
            Atom *value = bindings_apply_if_vars(&bindings, &execution, answer);
            assert(value && value->kind == ATOM_EXPR && value->expr.len == 3u &&
                atom_is_symbol(value->expr.elems[0], "Siblings"));
            unsigned cell[2];
            for (unsigned i = 0u; i < 2u; i++) {
                Atom *inner = value->expr.elems[i + 1u];
                assert(inner && inner->kind == ATOM_EXPR && inner->expr.len == 4u &&
                    atom_is_symbol(inner->expr.elems[0], "Inner") &&
                    atom_eq(inner->expr.elems[1], atom_int(&execution, i + 1u)) &&
                    atom_eq(inner->expr.elems[2], inner->expr.elems[3]));
                assert(atom_eq(inner->expr.elems[2], atom_int(&execution, 7)) ||
                    atom_eq(inner->expr.elems[2], atom_int(&execution, 9)));
                cell[i] = atom_eq(inner->expr.elems[2], atom_int(&execution, 9));
            }
            counts[cell[0]][cell[1]]++;
            assert(++count <= 9u);
            bindings_free(&bindings);
            if (early)
                break;
        }
        if (!early)
            assert(step == PETTA_MACHINE_STEP_EXHAUSTED && count == 9u &&
                counts[0][0] == 4u && counts[0][1] == 2u && counts[1][0] == 2u && counts[1][1] == 1u);
        assert(prepared_calls == (early ? 2u : 4u) && prepared_attempts == prepared_calls);
        unsigned calls = prepared_calls;
        petta_machine_destroy(&machine);
        assert(prepared_calls == calls);
        prepared_pure_program_cache_free(&context.prepared_pure_cache);
        arena_free(&execution);
        checks += 4u;
    }
    prepared_mutation_arena = NULL;
    prepared_fixture = false;
    space_free(&space);
    arena_free(&owner);
    arena_free(&source);
    eval_set_library_context(NULL);
    cetta_library_context_free(library);
    free(library);
    return checks;
}

typedef struct {
    PettaEvalMachineContext context;
    size_t peak_live;
    unsigned pulls;
    Arena *caller_arena;
    unsigned borrowed_returns;
    unsigned owned_returns;
} OemBodyRetentionProbe;

static CettaPreparedPureAnswerCursor *oem_body_probe_open(void *context, void *prepared,
        Arena *arena, Atom *const *arguments, uint32_t arity) {
    OemBodyRetentionProbe *probe = context;
    probe->caller_arena = arena;
    return petta_eval_open_callable_answers(&probe->context, prepared, arena, arguments, arity);
}

static CettaOwnedExecutionStep oem_body_retention_next(void *context, Space *space,
        CettaPreparedPureAnswerCursor *cursor, CettaOwnedReturn *returned,
        CettaEvalCompletion *completion) {
    OemBodyRetentionProbe *probe = context;
    probe->pulls++;
    CettaOwnedExecutionStep step = petta_eval_machine_next_answer_cursor(
        &probe->context, space, cursor, returned, completion);
    CettaPreparedPureCursorStorage storage = cetta_prepared_pure_answer_cursor_storage(cursor);
    if (storage.live_bytes > probe->peak_live)
        probe->peak_live = storage.live_bytes;
    if (step == CETTA_OWNED_EXECUTION_RETURN && probe->caller_arena) {
        if (cetta_prepared_pure_answer_cursor_uses_arena(cursor, probe->caller_arena))
            probe->borrowed_returns++;
        else
            probe->owned_returns++;
    }
    return step;
}

/* Rejected partial matches restore the caller while the same callee
 * retains its remaining alternatives and committed effects. */
static unsigned relational_body_destination_controls(void) {
    CettaLibraryContext *library = calloc(1u, sizeof(*library));
    assert(library);
    cetta_library_context_init_for_language_profile(library, CETTA_LANGUAGE_PETTA, NULL);
    eval_set_library_context(library);
    Arena source;
    arena_init_detached(&source);
    Space space;
    space_init(&space);
    const char *equations[] = {
        "(= (rel-inner $x $n) (Inner $x $n $n))",
        "(= (rel-wrap $value) (Wrapped $value))",
        "(= (rel-wrapped $f) (let (Wrapped (Inner 1 $z 9)) ($f 1) (Result $z)))",
        "(= (rel-mixed-inner $x $n) (Inner $x 7 9))",
        "(= (rel-mixed-inner $x $n) (Inner $x 9 9))",
        "(= (rel-nonlinear $f) (let (Inner 1 $z $z) ($f 1) (Result $z)))",
        "(= (rel-choice-inner $x $n) (Inner $x 7 7))",
        "(= (rel-choice-inner $x $n) (Inner $x 7 7))",
        "(= (rel-choice-inner $x $n) (Inner $x 9 9))",
        "(= (rel-prefix $f) (let (Inner 1 $z 9) ($f 1) (Result $z)))",
        "(= (rel-bound $f) (let (Inner 1 7 7) ($f 1) Accepted))",
        "(= (rel-alias $f) (let (Inner 1 $z $z) ($f 1) (Result $z)))",
        "(= (rel-missing $f) (let (Inner 2 $z $z) ($f 1) (Result $z)))",
        "(= (rel-slot $f $expected) (let $expected ($f 1) Accepted))",
        "(= (rel-caller-fails $f) (let (Inner 1 $z $z) ($f 1) (if (== $z 9) (Result $z) (empty))))",
        "(= (rel-once $f) (once (let (Inner 1 $z 9) ($f 1) (Result $z))))",
        "(= (rel-cut $f) (let (Inner 1 $z $z) ($f 1) (let $u (cut) (Result $z))))",
        "(= (rel-shared $f) (let (Inner 1 $left $right) ($f 1) (let $left $right (Pair $left $right))))",
    };
    for (size_t index = 0u; index < sizeof(equations) / sizeof(*equations); index++) {
        Atom *equation = fixture_atom(&source, equations[index]);
        const PettaPlanNode *plan = petta_program_plan_dynamic_add(library->petta_program, equation);
        assert(plan);
        space_add(&space, equation);
        assert(petta_program_note_add(library->petta_program, &space,
            space_get_at64(&space, index), plan));
    }
    const struct {
        const char *head;
        unsigned count;
        const char *answers[3];
        unsigned effect_count;
        const char *effect_answer;
    } cases[] = {
        {"rel-prefix", 1u, {"(Result 9)"}, 0u, NULL},
        {"rel-bound", 2u, {"Accepted", "Accepted"}, 0u, NULL},
        {"rel-alias", 3u, {"(Result 7)", "(Result 7)", "(Result 9)"}, 2u, "(Result 6)"},
        {"rel-missing", 0u, {NULL}, 0u, NULL},
        {"rel-slot", 2u, {"Accepted", "Accepted"}, 0u, NULL},
        {"rel-caller-fails", 1u, {"(Result 9)"}, 0u, NULL},
        {"rel-once", 1u, {"(Result 9)"}, 0u, NULL},
        {"rel-cut", 1u, {"(Result 7)"}, 1u, "(Result 6)"},
        {"rel-shared", 3u, {"(Pair 7 7)", "(Pair 7 7)", "(Pair 9 9)"}, 2u, "(Pair 6 6)"},
        {"rel-nonlinear", 3u, {"(Result 9)", "(Result 9)", "(Result 9)"}, 2u, "(Result 9)"},
        {"rel-wrapped", 3u, {"(Result 9)", "(Result 9)", "(Result 9)"}, 2u, "(Result 9)"},
    };
    Atom *callable = petta_capture_lambda(&source, fixture_atom(&source,
        "(|-> ($x) (rel-inner $x (__cetta_lib_json_parse_v1 $x)))"), NULL);
    Atom *choices_callable = petta_capture_lambda(&source, fixture_atom(&source,
        "(|-> ($x) (rel-choice-inner $x (__cetta_lib_json_parse_v1 $x)))"), NULL);
    Atom *mixed_callable = petta_capture_lambda(&source, fixture_atom(&source,
        "(|-> ($x) (rel-mixed-inner $x (__cetta_lib_json_parse_v1 $x)))"), NULL);
    Atom *wrapped_callable = petta_capture_lambda(&source, fixture_atom(&source,
        "(|-> ($x) (rel-wrap (rel-choice-inner $x (__cetta_lib_json_parse_v1 $x))))"), NULL);
    assert(callable && choices_callable && mixed_callable && wrapped_callable);
    prepared_head = symbol_intern_cstr(g_symbols, "__cetta_lib_json_parse_v1");
    prepared_fixture = true;
    unsigned checks = 0u;
    for (size_t item = 0u; item < sizeof(cases) / sizeof(*cases); item++)
    for (unsigned variant = 0u; variant < 3u; variant++)
    for (unsigned early = 0u; early < 2u; early++) {
        bool effects = variant == 1u;
        Arena answers;
        arena_init_detached(&answers);
        OemBodyRetentionProbe probe = {.context = {.library_context = library,
            .fuel = -1, .semantic_authority_epoch = 1u}};
        CettaOpenEquationHost host = petta_eval_open_compile_host(&probe.context, &space, false);
        uint32_t arity = item == 4u ? 2u : 1u;
        const char *reason = NULL;
        CettaOpenEquationProgram *program = cetta_open_equation_program_compile(
            library->petta_program, &space, symbol_intern_cstr(g_symbols, cases[item].head),
            arity, &host, &reason);
        if (!program) fprintf(stderr, "relational body compile %s: %s\n", cases[item].head, reason);
        assert(program);
        CettaOpenEquationRuntime runtime = {.context = &probe,
            .prepare_pure_callable = petta_eval_open_prepare_pure_callable,
            .execute_pure_callable = petta_eval_open_execute_pure_callable,
            .open_callable_answers = oem_body_probe_open,
            .next_callable_answer = oem_body_retention_next,
            .release_pure_callable = petta_eval_open_release_pure_callable,
            .head_callable = petta_eval_open_head_callable,
            .builtin_allowed = petta_eval_machine_builtin_allowed,
            .function_registered = petta_eval_open_function_registered,
            .boolean_value = petta_eval_machine_boolean_value, .open_head_is_data = true};
        Atom *arguments[] = {item == 10u ? wrapped_callable : item == 9u ? mixed_callable : variant == 2u ? choices_callable : callable, fixture_atom(&source, "(Inner 1 7 7)")};
        CettaOpenEquationCursor *cursor = cetta_open_equation_cursor_open(
            program, &answers, arguments, arity, NULL, NULL, 0u, &runtime);
        assert(cursor);
        prepared_mode = effects ? 14u : variant == 2u ? 0u : 2u;
        prepared_calls = prepared_attempts = prepared_effects = 0u;
        unsigned count = 0u;
        unsigned expected = item >= 9u && variant == 2u ? 1u
            : effects ? cases[item].effect_count : cases[item].count;
        CettaOpenEquationStep step;
        CettaCallOutcome returned;
        while ((step = cetta_open_equation_cursor_next(cursor, NULL, &returned, NULL)) ==
                CETTA_OPEN_EQUATION_ANSWER) {
            assert(count < expected && returned.kind == CETTA_CALL_VALUE &&
                returned.result_form == CETTA_CALL_RESULT_COMPLETED_VALUE);
            const char *observation = effects ? cases[item].effect_answer : cases[item].answers[count];
            assert(observation && atom_eq(returned.term, fixture_atom(&answers, observation)));
            count++;
            if (early)
                break;
            assert(cetta_open_equation_cursor_detach(cursor, &answers));
        }
        if (step != CETTA_OPEN_EQUATION_ANSWER && step != CETTA_OPEN_EQUATION_EXHAUSTED)
            fprintf(stderr, "relational body %s effects=%u early=%u count=%u step=%u reason=%u calls=%u attempts=%u\n",
                cases[item].head, effects, early, count, step,
                cetta_open_equation_cursor_handoff(cursor), prepared_calls, prepared_attempts);
        assert(count == (early && expected ? 1u : expected));
        if (!early || !count)
            assert(step == CETTA_OPEN_EQUATION_EXHAUSTED);
        assert(prepared_calls == 1u && prepared_attempts == 1u && probe.pulls);
        if (variant == 2u)
            assert(probe.borrowed_returns > 0u);
        else
            assert(probe.owned_returns > 0u && !probe.borrowed_returns);
        unsigned committed = prepared_effects;
        assert(committed == (effects ? 2u : 0u));
        cetta_open_equation_cursor_close(cursor);
        assert(prepared_calls == 1u && prepared_attempts == 1u && prepared_effects == committed);
        cetta_open_equation_program_release(program);
        prepared_pure_program_cache_free(&probe.context.prepared_pure_cache);
        arena_free(&answers);
        checks++;
    }
    prepared_fixture = false;
    space_free(&space);
    arena_free(&source);
    eval_set_library_context(NULL);
    cetta_library_context_free(library);
    free(library);
    return checks;
}

/* A failed evacuation after native entry is terminal. Re-entering source
 * would duplicate the effect even though no answer has been published. */
static unsigned borrowed_collection_failure_control(void) {
    CettaLibraryContext *library = calloc(1u, sizeof(*library));
    assert(library);
    cetta_library_context_init_for_language_profile(library, CETTA_LANGUAGE_PETTA, NULL);
    eval_set_library_context(library);
    Arena source;
    arena_init_detached(&source);
    Space space;
    space_init(&space);
    const char *equations[] = {
        "(= (capacity-inner $x $n) (Inner $x 7 9))",
        "(= (capacity-inner $x $n) (Inner $x 9 9))",
        "(= (capacity-driver $f $x) (let (Inner $payload $z $z) ($f $x) (Result $payload $z)))",
    };
    for (size_t index = 0u; index < sizeof(equations) / sizeof(*equations); index++) {
        Atom *equation = fixture_atom(&source, equations[index]);
        const PettaPlanNode *plan = petta_program_plan_dynamic_add(library->petta_program, equation);
        assert(plan);
        space_add(&space, equation);
        assert(petta_program_note_add(library->petta_program, &space,
            space_get_at64(&space, index), plan));
    }
    (void)eval_gc_enabled();
    bool was_enabled = g_eval_gc.enabled;
    size_t old_budget = g_eval_gc.budget_bytes;
    g_eval_gc.enabled = true;
    g_eval_gc.budget_bytes = ARENA_BLOCK_SIZE;
    enum { PAYLOAD_FIELDS = 2048u };
    Atom **fields = malloc(sizeof(*fields) * (PAYLOAD_FIELDS + 1u));
    assert(fields);
    fields[0] = atom_symbol(&source, "Payload");
    for (unsigned index = 1u; index <= PAYLOAD_FIELDS; index++)
        fields[index] = atom_int(&source, index);
    Atom *payload = atom_expr(&source, fields, PAYLOAD_FIELDS + 1u);
    free(fields);
    assert(payload);
    Arena execution;
    arena_init_detached(&execution);
    OemBodyRetentionProbe probe = {.context = {.fuel = -1,
        .library_context = library, .semantic_authority_epoch = 1u}};
    CettaOpenEquationHost host = petta_eval_open_compile_host(&probe.context, &space, false);
    const char *reason = NULL;
    CettaOpenEquationProgram *program = cetta_open_equation_program_compile(
        library->petta_program, &space, symbol_intern_cstr(g_symbols, "capacity-driver"),
        2u, &host, &reason);
    assert(program);
    CettaOpenEquationRuntime runtime = {.context = &probe,
        .prepare_pure_callable = petta_eval_open_prepare_pure_callable,
        .execute_pure_callable = petta_eval_open_execute_pure_callable,
        .open_callable_answers = oem_body_probe_open,
        .next_callable_answer = oem_body_retention_next,
        .release_pure_callable = petta_eval_open_release_pure_callable,
        .head_callable = petta_eval_open_head_callable,
        .builtin_allowed = petta_eval_machine_builtin_allowed,
        .function_registered = petta_eval_open_function_registered,
        .boolean_value = petta_eval_machine_boolean_value, .open_head_is_data = true};
    Atom *callable = petta_capture_lambda(&source, fixture_atom(&source,
        "(|-> ($p) (capacity-inner $p (__cetta_lib_json_parse_v1 $p)))"), NULL);
    assert(callable);
    Atom *arguments[] = {callable, payload};
    CettaOpenEquationCursor *cursor = cetta_open_equation_cursor_open(
        program, &execution, arguments, 2u, NULL, NULL, 0u, &runtime);
    assert(cursor);
    prepared_head = symbol_intern_cstr(g_symbols, "__cetta_lib_json_parse_v1");
    prepared_fixture = true;
    prepared_mode = 28u;
    prepared_calls = prepared_attempts = prepared_effects = 0u;
    CettaCallOutcome returned = cetta_call_failure();
    CettaOpenEquationStep step = cetta_open_equation_cursor_next(cursor, NULL, &returned, NULL);
    if (step != CETTA_OPEN_EQUATION_FAULT)
        fprintf(stderr, "borrowed collection failure: step=%u calls=%u attempts=%u effects=%u\n",
            step, prepared_calls, prepared_attempts, prepared_effects);
    assert(step == CETTA_OPEN_EQUATION_FAULT &&
        cetta_open_equation_cursor_handoff(cursor) == CETTA_OPEN_EQUATION_HANDOFF_CAPACITY);
    assert(prepared_calls == 1u && prepared_attempts == 1u && prepared_effects == 1u &&
        probe.borrowed_returns == 1u && !probe.owned_returns);
    assert(cetta_open_equation_cursor_next(cursor, NULL, &returned, NULL) ==
        CETTA_OPEN_EQUATION_HANDOFF);
    assert(prepared_calls == 1u && prepared_effects == 1u);
    cetta_open_equation_cursor_close(cursor);
    assert(prepared_calls == 1u && prepared_effects == 1u);
    cetta_open_equation_program_release(program);
    prepared_pure_program_cache_free(&probe.context.prepared_pure_cache);
    arena_free(&execution);
    prepared_fixture = false;
    g_eval_gc.enabled = was_enabled;
    g_eval_gc.budget_bytes = old_budget;
    space_free(&space);
    arena_free(&source);
    eval_set_library_context(NULL);
    cetta_library_context_free(library);
    free(library);
    return 5u;
}

/* A callee can loan one final result, or retain a single shared graph for
 * repeated results, while the OEM caller collects and rolls back. */
static unsigned oem_body_retention_controls(void) {
    CettaLibraryContext *library = calloc(1u, sizeof(*library));
    assert(library);
    cetta_library_context_init_for_language_profile(library, CETTA_LANGUAGE_PETTA, NULL);
    eval_set_library_context(library);
    Arena source;
    arena_init_detached(&source);
    Space space;
    space_init(&space);
    const char *equations[] = {
        "(= (body-retained-inner $x $n) (Retained $x $n $n))",
        "(= (body-retained-driver $f $x) (let $n ($f $x) (Outer $n $n)))",
    };
    for (size_t i = 0u; i < 2u; i++) {
        Atom *equation = fixture_atom(&source, equations[i]);
        const PettaPlanNode *plan = petta_program_plan_dynamic_add(library->petta_program, equation);
        assert(plan);
        space_add(&space, equation);
        assert(petta_program_note_add(library->petta_program, &space,
            space_get_at64(&space, i), plan));
    }
    enum { PAYLOAD_BYTES = 65536u };
    char *text = malloc(PAYLOAD_BYTES + 1u);
    assert(text);
    memset(text, 'x', PAYLOAD_BYTES);
    text[PAYLOAD_BYTES] = '\0';
    Atom *payload = atom_string(&source, text);
    free(text);
    assert(payload);
    prepared_head = symbol_intern_cstr(g_symbols, "__cetta_lib_json_parse_v1");
    prepared_fixture = true;
    unsigned checks = 0u;
    for (unsigned frontier = 0u; frontier < 2u; frontier++)
    for (unsigned early = 0u; early < 2u; early++) {
        Arena answers;
        arena_init_detached(&answers);
        OemBodyRetentionProbe probe = {.context = {.library_context = library,
            .fuel = -1, .semantic_authority_epoch = 1u}};
        CettaOpenEquationHost host = petta_eval_open_compile_host(&probe.context, &space, false);
        const char *reason = NULL;
        CettaOpenEquationProgram *program = cetta_open_equation_program_compile(
            library->petta_program, &space, symbol_intern_cstr(g_symbols, "body-retained-driver"),
            2u, &host, &reason);
        if (!program) fprintf(stderr, "retained body compile: %s\n", reason);
        assert(program);
        CettaOpenEquationRuntime runtime = {.context = &probe,
            .prepare_pure_callable = petta_eval_open_prepare_pure_callable,
            .execute_pure_callable = petta_eval_open_execute_pure_callable,
            .open_callable_answers = petta_eval_open_callable_answers,
            .next_callable_answer = oem_body_retention_next,
            .release_pure_callable = petta_eval_open_release_pure_callable,
            .head_callable = petta_eval_open_head_callable,
            .builtin_allowed = petta_eval_machine_builtin_allowed,
            .function_registered = petta_eval_open_function_registered,
            .boolean_value = petta_eval_machine_boolean_value,
            .open_head_is_data = true};
        Atom *callable = petta_capture_lambda(&source, fixture_atom(&source,
            "(|-> ($p) (body-retained-inner $p (__cetta_lib_json_parse_v1 $p)))"), NULL);
        assert(callable);
        Atom *args[] = {callable, payload};
        CettaOpenEquationCursor *cursor = cetta_open_equation_cursor_open(
            program, &answers, args, 2u, NULL, NULL, 0u, &runtime);
        assert(cursor);
        prepared_mode = frontier ? 26u : 24u;
        prepared_calls = prepared_attempts = 0u;
        unsigned count = 0u;
        CettaCallOutcome returned;
        CettaOpenEquationStep stop;
        while ((stop = cetta_open_equation_cursor_next(cursor, NULL, &returned, NULL)) ==
                CETTA_OPEN_EQUATION_ANSWER) {
            Atom *value = returned.term;
            assert(++count <= 2u && returned.kind == CETTA_CALL_VALUE &&
                value->kind == ATOM_EXPR && value->expr.len == 3u &&
                atom_eq(value->expr.elems[1], value->expr.elems[2]));
            Atom *inner = value->expr.elems[1];
            assert(inner->kind == ATOM_EXPR && inner->expr.len == 4u);
            for (unsigned i = 1u; i < 4u; i++)
                assert(atom_eq(inner->expr.elems[i], payload));
            if (early)
                break;
        }
        if (count != (frontier && !early ? 2u : 1u)) {
            Atom *host_goal = NULL, *host_destination = NULL;
            Atom *const *host_vars = NULL;
            uint32_t host_count = 0u;
            const PettaPlanNode *host_plan = NULL;
            CettaOpenEquationHostMode mode;
            bool host_recovers = false;
            if (cetta_open_equation_cursor_host_goal(cursor, &host_goal, &host_destination,
                    &host_vars, &host_count, &host_plan, &mode, &host_recovers)) {
                fprintf(stderr, "retention host mode=%u goal=", mode);
                atom_print(host_goal, stderr);
                fputc(10, stderr);
            }
            fprintf(stderr, "retention frontier=%u early=%u count=%u stop=%u reason=%u pulls=%u calls=%u attempts=%u peak=%zu\n",
                frontier, early, count, stop, cetta_open_equation_cursor_handoff(cursor),
                probe.pulls, prepared_calls, prepared_attempts, probe.peak_live);
        }
        assert(count == (frontier && !early ? 2u : 1u));
        assert(prepared_calls == 1u && prepared_attempts == 1u && probe.pulls);
        /* One escaping shared payload, with room for cursor/continuation
         * metadata, is below the size of two complete payload copies. */
        assert(probe.peak_live < 2u * PAYLOAD_BYTES);
        printf("owned body payload: %u bytes, frontier %u, early %u, peak %zu callee bytes, %u pulls\n",
            PAYLOAD_BYTES, frontier, early, probe.peak_live, probe.pulls);
        cetta_open_equation_cursor_close(cursor);
        assert(prepared_calls == 1u);
        cetta_open_equation_program_release(program);
        arena_free(&answers);
        checks += 6u;
    }
    prepared_fixture = false;
    space_free(&space);
    arena_free(&source);
    eval_set_library_context(NULL);
    cetta_library_context_free(library);
    free(library);
    return checks;
}

static CettaPreparedPureProgram *test_body_program(Space *space, Atom *body,
        const VarId *parameters, uint32_t arity) {
    CettaPreparedPureSourceView view = prepared_pure_source_view_for_language(
        CETTA_LANGUAGE_PETTA, body);
    return cetta_prepared_pure_program_compile_body_answers(space, body, parameters, arity,
        petta_semantics_boolean_value, petta_semantics_construct_value,
        petta_semantics_construct_value_is_expression, petta_semantics_is_opaque_runtime_value,
        petta_prepared_pure_register_view, petta_prepared_pure_expression_view,
        petta_prepared_pure_pattern_view, &view,
        active_composition_uses_total_structural_eq(),
        prepared_pure_match_decision_semantics(CETTA_GSLT_PURE_CALL_EAGER));
}

/* A completed native loan is interpreted in its carried bindings before
 * closed continuation admission. Conditional/expression packets stay pending. */
static unsigned bound_native_completion_controls(void) {
    CettaLibraryContext *library = calloc(1u, sizeof(*library));
    assert(library);
    cetta_library_context_init_for_language_profile(library, CETTA_LANGUAGE_PETTA, NULL);
    eval_set_library_context(library);
    Arena source;
    arena_init_detached(&source);
    Space space;
    space_init(&space);
    prepared_head = symbol_intern_cstr(g_symbols, "__cetta_lib_json_parse_v1");
    prepared_fixture = true;
    Atom *callable = petta_capture_lambda(&source, fixture_atom(&source,
        "(|-> ($x) (let $y (__cetta_lib_json_parse_v1 $x) (Pair $x $y $y)))"), NULL);
    Atom *parameter = atom_var_with_id(&source, "$bound-input", fresh_var_id());
    Atom *body = cetta_petta_open_callable(&source, callable, parameter);
    CettaPreparedPureProgram *program = test_body_program(&space, body, &parameter->var_id, 1u);
    assert(program);
    const uint64_t allowances[] = {0u, 1u, 64u};
    unsigned checks = 0u;
    for (unsigned item = 0u; item < 9u; item++)
    for (unsigned budget = 0u; budget < 3u; budget++)
    for (unsigned early = 0u; early < 2u; early++) {
        CettaPreparedPureAnswerCursorOptions options = {
            .limits = {.max_transitions = allowances[budget],
                .max_scratch_bytes = item == 4u ? ARENA_BLOCK_SIZE : 1024u * 1024u,
                .construct_allocation_bound = petta_semantics_construct_value_allocation_bound},
            .allow_continuations = true};
        Atom *arguments[] = {atom_int(&source, 11)};
        prepared_mode = 23u;
        prepared_calls = prepared_attempts = prepared_effects = 0u;
        CettaPreparedPureAnswerCursor *cursor = cetta_prepared_pure_answer_cursor_open_body(
            program, arguments, 1u, &options);
        assert(cursor);
        CettaEvalCompletion completion = CETTA_EVAL_COMPLETE;
        CettaOwnedReturn returned;
        CettaOwnedExecutionStep step;
        for (;;) {
            step = cetta_prepared_pure_answer_cursor_return(cursor, &returned, &completion);
            if (step == CETTA_OWNED_EXECUTION_HANDOFF &&
                cetta_prepared_pure_answer_cursor_refill_transitions(cursor, 32u, &completion))
                continue;
            break;
        }
        CettaInvocation invocation;
        bool entered = false;
        CettaOwnedReturn pending;
        assert(step == CETTA_OWNED_EXECUTION_HANDOFF && completion == CETTA_EVAL_COMPLETE &&
            cetta_prepared_pure_answer_cursor_native_boundary(cursor, &invocation, &entered, &pending));
        assert(entered && pending.outcome.kind == CETTA_CALL_SUSPENDED &&
            pending.outcome.term == pending.outcome.delayed->expr.elems[0] &&
            pending.outcome.term == pending.outcome.delayed->expr.elems[1]);
        Atom *held = pending.outcome.term;
        Atom *alias = atom_var_with_id(&source, "$bound-alias", fresh_var_id());
        Atom *other = atom_var_with_id(&source, "$bound-other", fresh_var_id());
        Bindings environment;
        bindings_init(&environment);
        assert(bindings_add_var(&environment, held, alias));
        Atom *resolved = item == 1u || item == 8u ? fixture_atom(&source, "(readln!)")
            : item == 2u ? atom_symbol(&source, "Empty")
            : item == 3u ? fixture_atom(&source, "(Error 7 datum)")
            : item == 4u || item == 6u ? atom_expr3(&source,
                atom_symbol(&source, "Nested"), other, other) : atom_int(&source, 7);
        if (item != 5u)
            assert(bindings_add_var(&environment, alias, resolved));
        if (item != 6u)
            assert(bindings_add_var(&environment, other, atom_int(&source, item ? 7 : 9)));
        enum { RETAINED_ANSWERS = 4096u };
        Atom *frontier = NULL;
        if (item == 4u) {
            Atom **items = arena_alloc(&source, sizeof(*items) * RETAINED_ANSWERS);
            assert(items);
            for (unsigned index = 0u; index < RETAINED_ANSWERS; index++)
                items[index] = held;
            frontier = atom_expr(&source, items, RETAINED_ANSWERS);
            assert(frontier);
        }
        CettaCallReturnCursor values = {.outcome = item == 0u
            ? cetta_call_completed_frontier(atom_expr3(&source, held, held, other))
            : item == 4u ? cetta_call_completed_frontier(frontier)
            : item == 7u ? cetta_call_suspended(held, pending.outcome.delayed)
            : item == 8u ? cetta_call_value(held) : cetta_call_completed_value(held),
            .environment = &environment};
        CettaOwnedExecution replacement;
        cetta_owned_execution_init(&replacement, &values, cetta_call_return_next, NULL);
        assert(cetta_prepared_pure_answer_cursor_complete_native(cursor, &replacement, false) &&
            !replacement.computation);
        unsigned count = 0u;
        for (;;) {
            step = cetta_prepared_pure_answer_cursor_return(cursor, &returned, &completion);
            if (step == CETTA_OWNED_EXECUTION_HANDOFF &&
                cetta_prepared_pure_answer_cursor_refill_transitions(cursor, 32u, &completion))
                continue;
            if (step != CETTA_OWNED_EXECUTION_RETURN)
                break;
            Atom *answer = returned.outcome.term;
            assert(item < 5u && returned.outcome.kind == CETTA_CALL_VALUE &&
                returned.outcome.result_form == CETTA_CALL_RESULT_COMPLETED_VALUE &&
                answer && answer->kind == ATOM_EXPR && answer->expr.len == 4u &&
                atom_is_symbol(answer->expr.elems[0], "Pair") &&
                atom_eq(answer->expr.elems[1], atom_int(&source, 11)) &&
                answer->expr.elems[2] == answer->expr.elems[3]);
            Atom *expected = item == 0u ? atom_int(&source, count == 2u ? 9 : 7)
                : item == 4u ? fixture_atom(&source, "(Nested 7 7)") : resolved;
            assert(atom_eq(answer->expr.elems[2], expected) && !atom_has_vars(answer));
            assert(++count <= (item == 4u ? RETAINED_ANSWERS : item ? 1u : 3u));
            if (item == 4u)
                assert(cetta_prepared_pure_answer_cursor_storage(cursor).live_bytes < ARENA_BLOCK_SIZE);
            if (early)
                break;
        }
        if (completion != CETTA_EVAL_COMPLETE)
            fprintf(stderr, "bound completion item=%u budget=%u early=%u count=%u step=%u completion=%u live=%zu\n",
                item, budget, early, count, step, completion,
                cetta_prepared_pure_answer_cursor_storage(cursor).live_bytes);
        assert(completion == CETTA_EVAL_COMPLETE);
        if (item >= 5u) {
            assert(!count && step == CETTA_OWNED_EXECUTION_HANDOFF &&
                cetta_prepared_pure_answer_cursor_native_boundary(cursor, &invocation, &entered, &returned));
            assert(entered && returned.environment == &environment && returned.outcome.term == held &&
                returned.outcome.kind == (item == 7u ? CETTA_CALL_SUSPENDED : CETTA_CALL_VALUE));
            if (item == 7u)
                assert(returned.outcome.delayed == pending.outcome.delayed);
            if (item == 8u)
                assert(returned.outcome.result_form == CETTA_CALL_RESULT_EXPRESSION);
        } else {
            assert(count == (early ? 1u : item == 4u ? RETAINED_ANSWERS : item ? 1u : 3u));
            if (!early)
                assert(step == CETTA_OWNED_EXECUTION_COMPLETE);
        }
        assert(prepared_calls == 1u && prepared_attempts == 1u && !prepared_effects);
        cetta_prepared_pure_answer_cursor_close(cursor);
        assert(prepared_calls == 1u && !prepared_effects);
        bindings_free(&environment);
        checks++;
    }
    cetta_prepared_pure_program_free(program);
    prepared_fixture = false;
    space_free(&space);
    arena_free(&source);
    eval_set_library_context(NULL);
    cetta_library_context_free(library);
    free(library);
    return checks;
}

static uint64_t resumption_allowance;
static SymbolId resumption_root;
static unsigned resumption_opened, resumption_entered_handoffs, resumption_pulls;
static unsigned resumption_native_transfers;
static uint64_t resumption_parent_credit;
static bool resumption_missing_source_root;
static bool resumption_missing_caller_plans;
static bool resumption_cancel_armed, resumption_cancelled;
static CettaSourceStep resumption_refused_step;

static bool resumption_cancel_permit(void *context, const CettaSourceStep *step) {
    (void)context;
    if (resumption_cancel_armed && prepared_calls) {
        resumption_cancel_armed = false;
        resumption_cancelled = true;
    }
    if (resumption_cancelled) {
        resumption_refused_step = *step;
        return false;
    }
    return true;
}

static bool resumption_parent_permit(void *context, const CettaSourceStep *step) {
    (void)context;
    if (!step || !step->source.root)
        resumption_missing_source_root = true;
    if (!resumption_parent_credit)
        return false;
    resumption_parent_credit--;
    return true;
}

static CettaPreparedPureAnswerCursor *resumption_open_cursor(void *context,
        Space *space, Arena *scratch, Atom *call,
        CettaPreparedPureHeadAdmissionFn admit, void *admission,
        bool (*current)(const void *)) {
    (void)context; (void)scratch;
    if (atom_head_symbol_id(call) != resumption_root)
        return NULL;
    CettaPreparedPureSourceView view = prepared_pure_source_view_for_language(
        CETTA_LANGUAGE_PETTA, call);
    if (resumption_missing_caller_plans)
        view.project_source = NULL;
    CettaPreparedPureProgram *program = cetta_prepared_pure_program_compile_closed_answers(
        space, call, CETTA_GSLT_PURE_CALL_EAGER, petta_semantics_boolean_value,
        petta_semantics_construct_value, petta_semantics_construct_value_is_expression,
        petta_semantics_is_opaque_runtime_value, petta_prepared_pure_register_view,
        petta_prepared_pure_expression_view, petta_prepared_pure_pattern_view, &view,
        true, active_composition_uses_total_structural_eq(),
        prepared_pure_match_decision_semantics(CETTA_GSLT_PURE_CALL_EAGER), NULL);
    assert(program);
    CettaPreparedPureAnswerCursorOptions options = {
        .limits = {.max_transitions = resumption_allowance,
            .max_scratch_bytes = 1024u * 1024u,
            .construct_allocation_bound = petta_semantics_construct_value_allocation_bound},
        .head_admission = admit, .head_admission_context = admission,
        .allow_continuations = true,
        .continuation_current = current, .continuation_context = admission};
    CettaPreparedPureAnswerCursor *cursor = cetta_prepared_pure_answer_cursor_open(program, &options);
    cetta_prepared_pure_program_clear_closed_entry_call(program);
    cetta_prepared_pure_program_free(program);
    if (cursor)
        resumption_opened++;
    return cursor;
}

static CettaOwnedExecutionStep resumption_next_cursor(void *context, Space *space,
        CettaPreparedPureAnswerCursor *cursor, CettaOwnedReturn *returned,
        CettaEvalCompletion *completion) {
    (void)context;
    resumption_pulls++;
    CettaOwnedExecutionStep step = prepared_pure_next_return(space, cursor, 0u, returned, completion);
    if (cetta_prepared_pure_answer_cursor_native_transfer_ready(cursor))
        resumption_native_transfers++;
    if (step == CETTA_OWNED_EXECUTION_HANDOFF &&
        cetta_prepared_pure_answer_cursor_native_started(cursor))
        resumption_entered_handoffs++;
    return step;
}

/* Executable readback preserves captured data and the pending computation
 * across both ordinary consumers, including rollback and source alternatives. */
static unsigned contextual_resumption_controls(void) {
    CettaLibraryContext *library = calloc(1u, sizeof(*library));
    assert(library);
    cetta_library_context_init_for_language_profile(library, CETTA_LANGUAGE_PETTA, NULL);
    eval_set_library_context(library);
    Arena source;
    arena_init_detached(&source);
    Space space;
    space_init(&space);
    const char *equations[] = {
        "(= (resume-choice $x) 7)", "(= (resume-choice $x) 7)", "(= (resume-choice $x) 9)",
        "(= (resume-inner $x) (let $n (resume-choice $x) (Inner $n $n)))",
        "(= (resume-data $x) (let $a (__cetta_lib_json_parse_v1 $x) (let $n (resume-choice $x) (Pair $x $a $n))))",
        "(= (resume-nested $x) (let $a (__cetta_lib_json_parse_v1 $x) (let $n (resume-inner $x) (Outer $a $n))))",
        "(= (resume-code $x) (let $a (__cetta_lib_json_parse_v1 $x) (let $n (resume-choice $x) (Pair $a $n))))",
        "(= (resume-number $x) (let $a (+ $x 29) (let $n (resume-choice $x) (+ $a $n))))",
        "(= (resume-error $x) (let $a (__cetta_lib_json_parse_v1 $x) (let $n (resume-choice $x) (Pair $a $n))))",
        "(= (resume-empty $x) (let $a (__cetta_lib_json_parse_v1 $x) (let $n (resume-choice $x) (Pair $a $n))))",
        "(= (resume-error-root $x) (let $a (__cetta_lib_json_parse_v1 $x) (let $n (resume-choice $x) $a)))",
        "(= (resume-empty-root $x) (let $a (__cetta_lib_json_parse_v1 $x) (let $n (resume-choice $x) $a)))",
        "(= (resume-raise-root $x) (let $a (__cetta_lib_json_parse_v1 $x) (let $n (resume-choice $x) (__cetta_lib_json_parse_v1 $n))))",
    };
    for (size_t index = 0u; index < sizeof(equations) / sizeof(*equations); index++) {
        Atom *equation = fixture_atom(&source, equations[index]);
        const PettaPlanNode *plan = petta_program_plan_dynamic_add(library->petta_program, equation);
        assert(plan);
        space_add(&space, equation);
        assert(petta_program_note_add(library->petta_program, &space,
            space_get_at64(&space, index), plan));
    }
    const struct { const char *head, *body; const char *answers[3]; bool native; unsigned mode; } cases[] = {
        {"resume-data", "(|-> ($x) (let $a (__cetta_lib_json_parse_v1 $x) (let $n (resume-choice $x) (Pair $x $a $n))))",
            {"(Pair 11 (function (return (readln!))) 7)", "(Pair 11 (function (return (readln!))) 7)", "(Pair 11 (function (return (readln!))) 9)"}, true, 1u},
        {"resume-nested", "(|-> ($x) (let $a (__cetta_lib_json_parse_v1 $x) (let $n (resume-inner $x) (Outer $a $n))))",
            {"(Outer (function (return (readln!))) (Inner 7 7))", "(Outer (function (return (readln!))) (Inner 7 7))", "(Outer (function (return (readln!))) (Inner 9 9))"}, true, 1u},
        {"resume-code", "(|-> ($x) (let $a (__cetta_lib_json_parse_v1 $x) (let $n (resume-choice $x) (Pair $a $n))))",
            {"(Pair (readln!) 7)", "(Pair (readln!) 7)", "(Pair (readln!) 9)"}, true, 29u},
        {"resume-number", "(|-> ($x) (let $a (+ $x 29) (let $n (resume-choice $x) (+ $a $n))))",
            {"47", "47", "49"}, false, 0u},
        {"resume-error", "(|-> ($x) (let $a (__cetta_lib_json_parse_v1 $x) (let $n (resume-choice $x) (Pair $a $n))))",
            {"(Pair (Error 7 datum) 7)", "(Pair (Error 7 datum) 7)", "(Pair (Error 7 datum) 9)"}, true, 6u},
        {"resume-empty", "(|-> ($x) (let $a (__cetta_lib_json_parse_v1 $x) (let $n (resume-choice $x) (Pair $a $n))))",
            {"(Pair Empty 7)", "(Pair Empty 7)", "(Pair Empty 9)"}, true, 7u},
    };
    const uint64_t allowances[] = {1u, 2u, 64u};
    prepared_fixture = true;
    prepared_head = symbol_intern_cstr(g_symbols, "__cetta_lib_json_parse_v1");
    unsigned checks = 0u;
    for (unsigned item = 0u; item < sizeof(cases) / sizeof(*cases); item++) {
        Atom *callable = petta_capture_lambda(&source, fixture_atom(&source, cases[item].body), NULL);
        Atom *parameter = atom_var_with_id(&source, "$resume-input", fresh_var_id());
        Atom *body = cetta_petta_open_callable(&source, callable, parameter);
        CettaPreparedPureProgram *program = test_body_program(&space, body, &parameter->var_id, 1u);
        assert(program);
        for (unsigned budget = 0u; budget < 3u; budget++)
        for (unsigned refuse = 0u; refuse < 2u; refuse++)
        for (unsigned nested_host = 0u; nested_host < 2u; nested_host++) {
            Arena publication;
            arena_init_detached(&publication);
            ResidualRecipient recipient = {.arena = &publication,
                .expected = cases[item].answers, .refuse = refuse, .case_index = item};
            EvalOwnedAnswers owner = {.program = cetta_prepared_pure_program_retain(program), .space = &space};
            CettaPreparedPureAnswerCursorOptions options = {
                .limits = {.max_transitions = allowances[budget], .max_scratch_bytes = 1024u * 1024u,
                    .construct_allocation_bound = petta_semantics_construct_value_allocation_bound},
                .allow_continuations = true};
            Atom *arguments[] = {atom_int(&source, 11)};
            owner.cursor = cetta_prepared_pure_answer_cursor_open_body(program, arguments, 1u, &options);
            assert(owner.cursor);
            eval_owned_answers_capture_authority(&owner);
            cetta_owned_execution_init(&owner.execution, &owner,
                eval_owned_answers_next, eval_owned_answers_release);
            prepared_mode = cases[item].mode;
            prepared_calls = prepared_attempts = prepared_effects = 0u;
            uint32_t old_depth = g_petta_machine_host_depth;
            g_petta_machine_host_depth += nested_host;
            bool complete = false;
            for (;;) {
                const CettaOwnedReturn *returned = NULL;
                CettaOwnedExecutionStep step = cetta_owned_execution_loan(&owner.execution, &returned);
                if (step == CETTA_OWNED_EXECUTION_RETURN) {
                    if (!residual_publish(&recipient, returned))
                        break;
                    assert(cetta_owned_execution_accept(&owner.execution));
                    continue;
                }
                complete = step == CETTA_OWNED_EXECUTION_COMPLETE ||
                    (step == CETTA_OWNED_EXECUTION_HANDOFF &&
                     eval_owned_answers_finish_residual(&owner, residual_publish, &recipient));
                break;
            }
            assert(g_petta_machine_host_depth == old_depth + nested_host);
            g_petta_machine_host_depth = old_depth;
            assert(complete == !refuse && recipient.count == (refuse ? 0u : 3u));
            assert(prepared_calls == (cases[item].native ? 1u : 0u) && !prepared_effects);
            eval_owned_answers_close(&owner);
            assert(prepared_calls == (cases[item].native ? 1u : 0u) && !prepared_effects);
            arena_free(&publication);
            checks++;
        }
        cetta_prepared_pure_program_free(program);
    }
    const uint64_t machine_allowances[] = {0u, 1u, 2u, 64u};
    for (unsigned item = 0u; item < sizeof(cases) / sizeof(*cases); item++)
    for (unsigned budget = 0u; budget < 4u; budget++)
    for (unsigned early = 0u; early < 2u; early++) {
        Arena execution;
        arena_init_detached(&execution);
        PettaEvalMachineContext context = {.fuel = -1, .library_context = library,
            .semantic_authority_epoch = 1u, .prepared_pure_cache = {.root_space = &space}};
        PettaMachineHost host = petta_eval_machine_host(&context, -1, false, false, false, false,
            false, petta_eval_machine_match_semantics(library, false));
        host.execute_prepared_pure_call = NULL;
        host.open_relation_cursor = NULL;
        host.open_answer_cursor = resumption_open_cursor;
        host.next_answer_cursor = resumption_next_cursor;
        resumption_allowance = machine_allowances[budget];
        resumption_root = symbol_intern_cstr(g_symbols, cases[item].head);
        resumption_opened = resumption_entered_handoffs = 0u;
        prepared_mode = cases[item].mode;
        prepared_calls = prepared_attempts = prepared_effects = 0u;
        Atom *call = atom_expr2(&execution, atom_symbol_id(&execution, resumption_root), atom_int(&execution, 11));
        PettaMachine machine;
        assert(petta_machine_init(&machine, &space, &execution, call, NULL, &host));
        unsigned count = 0u;
        PettaMachineStep step;
        for (;;) {
            Atom *answer = NULL;
            Bindings environment;
            step = petta_machine_next(&machine, &answer, &environment);
            if (step == PETTA_MACHINE_STEP_ANSWER) {
                assert(count < 3u);
                Atom *observed = bindings_apply_if_vars(&environment, &execution, answer);
                assert(atom_eq(observed, fixture_atom(&execution, cases[item].answers[count++])));
                assert(bindings_logically_empty(&environment));
            }
            bindings_free(&environment);
            if (step != PETTA_MACHINE_STEP_ANSWER || early)
                break;
        }
        if (count != (early ? 1u : 3u) || (!early && step != PETTA_MACHINE_STEP_EXHAUSTED) || !resumption_opened)
            fprintf(stderr, "source handoff item=%u budget=%u early=%u count=%u step=%u opened=%u entered=%u native=%u attempts=%u\n",
                item, budget, early, count, step, resumption_opened, resumption_entered_handoffs, prepared_calls, prepared_attempts);
        assert(count == (early ? 1u : 3u) &&
            (early || step == PETTA_MACHINE_STEP_EXHAUSTED) && resumption_opened > 0u);
        if (cases[item].native && budget == 1u)
            assert(resumption_entered_handoffs > 0u);
        assert(prepared_calls == (cases[item].native ? 1u : 0u) && !prepared_effects);
        petta_machine_destroy(&machine);
        assert(prepared_calls == (cases[item].native ? 1u : 0u) && !prepared_effects);
        prepared_pure_program_cache_free(&context.prepared_pure_cache);
        arena_free(&execution);
        checks++;
    }
    {
        Arena execution;
        arena_init_detached(&execution);
        PettaEvalMachineContext context = {.fuel = -1, .library_context = library,
            .semantic_authority_epoch = 1u, .prepared_pure_cache = {.root_space = &space}};
        PettaMachineHost host = petta_eval_machine_host(&context, -1, false, false, false, false,
            false, petta_eval_machine_match_semantics(library, false));
        host.execute_prepared_pure_call = NULL;
        host.open_relation_cursor = NULL;
        host.open_answer_cursor = resumption_open_cursor;
        host.next_answer_cursor = resumption_next_cursor;
        host.permit_transition = resumption_parent_permit;
        host.unlimited_transition_budget = false;
        resumption_allowance = 1u;
        resumption_root = symbol_intern_cstr(g_symbols, "resume-data");
        resumption_pulls = resumption_entered_handoffs = 0u;
        prepared_mode = 1u;
        prepared_calls = prepared_attempts = prepared_effects = 0u;
        PettaMachine machine;
        assert(petta_machine_init(&machine, &space, &execution,
            fixture_atom(&execution, "(resume-data 11)"), NULL, &host));
        unsigned count = 0u, turns = 0u;
        PettaMachineStep step;
        for (;;) {
            for (unsigned refused = 0u; refused < 2u; refused++) {
                resumption_parent_credit = 0u;
                unsigned calls = prepared_calls, pulls = resumption_pulls;
                Atom *answer = NULL;
                Bindings environment;
                step = petta_machine_next(&machine, &answer, &environment);
                bindings_free(&environment);
                if (step == PETTA_MACHINE_STEP_EXHAUSTED)
                    break;
                assert(step == PETTA_MACHINE_STEP_SUSPENDED &&
                    prepared_calls == calls && resumption_pulls == pulls);
            }
            if (step == PETTA_MACHINE_STEP_EXHAUSTED)
                break;
            resumption_parent_credit = 1u;
            Atom *answer = NULL;
            Bindings environment;
            step = petta_machine_next(&machine, &answer, &environment);
            if (step == PETTA_MACHINE_STEP_ANSWER) {
                assert(count < 3u && atom_eq(bindings_apply_if_vars(&environment,
                    &execution, answer), fixture_atom(&execution, cases[0].answers[count])));
                count++;
            } else
                assert(step == PETTA_MACHINE_STEP_SUSPENDED || step == PETTA_MACHINE_STEP_EXHAUSTED);
            bindings_free(&environment);
            assert(++turns < 1024u);
            if (step == PETTA_MACHINE_STEP_EXHAUSTED)
                break;
        }
        /* A finite parent purse keeps canonical selection before entry. */
        assert(count == 3u && prepared_calls == 1u && !prepared_effects && !resumption_pulls);
        petta_machine_destroy(&machine);
        prepared_pure_program_cache_free(&context.prepared_pure_cache);
        arena_free(&execution);
        checks++;
    }
    {
        Arena execution;
        arena_init_detached(&execution);
        PettaEvalMachineContext context = {.fuel = -1, .library_context = library,
            .semantic_authority_epoch = 1u, .prepared_pure_cache = {.root_space = &space}};
        PettaMachineHost host = petta_eval_machine_host(&context, -1, false, false, false, false,
            false, petta_eval_machine_match_semantics(library, false));
        host.execute_prepared_pure_call = NULL;
        host.open_relation_cursor = NULL;
        host.open_answer_cursor = resumption_open_cursor;
        host.next_answer_cursor = resumption_next_cursor;
        host.permit_transition = resumption_cancel_permit;
        resumption_allowance = 1u;
        resumption_root = symbol_intern_cstr(g_symbols, "resume-data");
        resumption_pulls = resumption_entered_handoffs = 0u;
        resumption_cancel_armed = true;
        resumption_cancelled = false;
        prepared_mode = 1u;
        prepared_calls = prepared_attempts = prepared_effects = 0u;
        PettaMachine machine;
        assert(petta_machine_init(&machine, &space, &execution,
            fixture_atom(&execution, "(resume-data 11)"), NULL, &host));
        uint64_t occurrence = 0u;
        for (unsigned repeat = 0u; repeat < 2u; repeat++) {
            Atom *answer = NULL;
            Bindings environment;
            assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_SUSPENDED);
            bindings_free(&environment);
            assert(prepared_calls == 1u && resumption_pulls == 1u && !prepared_effects);
            if (repeat)
                assert(resumption_refused_step.occurrence_instance == occurrence);
            occurrence = resumption_refused_step.occurrence_instance;
            assert(occurrence);
        }
        resumption_cancelled = false;
        unsigned count = 0u;
        PettaMachineStep step;
        for (;;) {
            Atom *answer = NULL;
            Bindings environment;
            step = petta_machine_next(&machine, &answer, &environment);
            if (step == PETTA_MACHINE_STEP_ANSWER) {
                assert(count < 3u && atom_eq(bindings_apply_if_vars(&environment,
                    &execution, answer), fixture_atom(&execution, cases[0].answers[count])));
                count++;
            }
            bindings_free(&environment);
            if (step != PETTA_MACHINE_STEP_ANSWER)
                break;
        }
        assert(step == PETTA_MACHINE_STEP_EXHAUSTED && count == 3u &&
            prepared_calls == 1u && !prepared_effects && resumption_entered_handoffs > 0u);
        petta_machine_destroy(&machine);
        prepared_pure_program_cache_free(&context.prepared_pure_cache);
        arena_free(&execution);
        checks++;
    }
    const struct { const char *head, *value; unsigned mode, count; bool raised; } controls[] = {
        {"resume-error-root", "(Error 7 datum)", 6u, 3u, false},
        {"resume-empty-root", "Empty", 7u, 3u, false},
        {"resume-raise-root", "(Error 7 fixture)", 32u, 1u, true},
    };
    for (unsigned item = 0u; item < sizeof(controls) / sizeof(*controls); item++)
    for (unsigned budget = 0u; budget < 4u; budget++) {
        Arena execution;
        arena_init_detached(&execution);
        PettaEvalMachineContext context = {.fuel = -1, .library_context = library,
            .semantic_authority_epoch = 1u, .prepared_pure_cache = {.root_space = &space}};
        PettaMachineHost host = petta_eval_machine_host(&context, -1, false, false, false, false,
            false, petta_eval_machine_match_semantics(library, false));
        host.execute_prepared_pure_call = NULL;
        host.open_relation_cursor = NULL;
        host.open_answer_cursor = resumption_open_cursor;
        host.next_answer_cursor = resumption_next_cursor;
        resumption_allowance = machine_allowances[budget];
        resumption_root = symbol_intern_cstr(g_symbols, controls[item].head);
        prepared_mode = controls[item].mode;
        prepared_calls = prepared_attempts = prepared_effects = 0u;
        PettaMachine machine;
        Atom *call = atom_expr2(&execution, atom_symbol_id(&execution, resumption_root), atom_int(&execution, 11));
        assert(petta_machine_init(&machine, &space, &execution, call, NULL, &host));
        unsigned count = 0u;
        PettaMachineStep step;
        for (;;) {
            Atom *answer = NULL;
            Bindings environment;
            step = petta_machine_next(&machine, &answer, &environment);
            if (step == PETTA_MACHINE_STEP_ANSWER) {
                Atom *observed = bindings_apply_if_vars(&environment, &execution, answer);
                assert(atom_eq(observed, fixture_atom(&execution, controls[item].value)));
                assert(petta_machine_last_answer_raised(&machine) == controls[item].raised);
                assert(++count <= controls[item].count);
            }
            bindings_free(&environment);
            /* A raised receipt transfers control to this caller, as the
             * evaluator driver does; it is not another normal answer pull. */
            if (step != PETTA_MACHINE_STEP_ANSWER || controls[item].raised)
                break;
        }
        PettaMachineStep expected_step = controls[item].raised
            ? PETTA_MACHINE_STEP_ANSWER : PETTA_MACHINE_STEP_EXHAUSTED;
        if (count != controls[item].count || step != expected_step)
            fprintf(stderr, "source control item=%u budget=%u count=%u step=%u native=%u\n",
                item, budget, count, step, prepared_calls);
        assert(count == controls[item].count && step == expected_step &&
            prepared_calls == (controls[item].raised ? 2u : 1u) && !prepared_effects);
        petta_machine_destroy(&machine);
        prepared_pure_program_cache_free(&context.prepared_pure_cache);
        assert(prepared_calls == (controls[item].raised ? 2u : 1u) && !prepared_effects);
        arena_free(&execution);
        checks++;
    }
    for (unsigned reason = 0u; reason < 2u; reason++) {
        bool revoked = reason == 0u;
        Arena execution;
        arena_init_detached(&execution);
        PettaEvalMachineContext context = {.fuel = -1, .library_context = library,
            .semantic_authority_epoch = 1u, .prepared_pure_cache = {.root_space = &space}};
        PettaMachineHost host = petta_eval_machine_host(&context, -1, false, false, false, false,
            false, petta_eval_machine_match_semantics(library, false));
        host.execute_prepared_pure_call = NULL;
        host.open_relation_cursor = NULL;
        host.open_answer_cursor = resumption_open_cursor;
        host.next_answer_cursor = resumption_next_cursor;
        resumption_allowance = 64u;
        resumption_root = symbol_intern_cstr(g_symbols, "resume-data");
        prepared_mode = revoked ? 27u : 31u;
        prepared_mutation_arena = &source;
        prepared_calls = prepared_attempts = prepared_effects = 0u;
        PettaMachine machine;
        Atom *call = fixture_atom(&execution, "(resume-data 11)");
        resumption_missing_caller_plans = !revoked;
        assert(petta_machine_init(&machine, &space, &execution, call, NULL, &host));
        for (unsigned repeat = 0u; repeat < 2u; repeat++) {
            Atom *answer = NULL;
            Bindings environment;
            PettaMachineStep step = petta_machine_next(&machine, &answer, &environment);
            assert(step == PETTA_MACHINE_STEP_INVALIDATED);
            bindings_free(&environment);
            assert(prepared_calls == 1u && !prepared_effects);
        }
        petta_machine_destroy(&machine);
        assert(prepared_calls == 1u && !prepared_effects);
        prepared_pure_program_cache_free(&context.prepared_pure_cache);
        resumption_missing_caller_plans = false;
        prepared_mutation_arena = NULL;
        arena_free(&execution);
        checks++;
    }
    for (unsigned tail = 0u; tail < 2u; tail++)
    for (unsigned datum = 0u; datum < 3u; datum++) {
        Atom *callable = petta_capture_lambda(&source, fixture_atom(&source, tail
            ? "(|-> ($x) (__cetta_lib_json_parse_v1 $x))"
            : "(|-> ($x) (let $a (__cetta_lib_json_parse_v1 $x) (Pair $x $a $a)))"), NULL);
        Atom *parameter = atom_var_with_id(&source, "$native-resume", fresh_var_id());
        Atom *body = cetta_petta_open_callable(&source, callable, parameter);
        CettaPreparedPureProgram *program = test_body_program(&space, body, &parameter->var_id, 1u);
        assert(program);
        CettaPreparedPureAnswerCursorOptions options = {
            .limits = {.max_transitions = 64u, .max_scratch_bytes = 1024u * 1024u,
                .construct_allocation_bound = petta_semantics_construct_value_allocation_bound},
            .allow_continuations = true};
        Atom *arguments[] = {atom_int(&source, 11)};
        prepared_mode = 23u;
        prepared_calls = prepared_attempts = prepared_effects = 0u;
        CettaPreparedPureAnswerCursor *cursor = cetta_prepared_pure_answer_cursor_open_body(program, arguments, 1u, &options);
        assert(cursor);
        CettaOwnedReturn pending;
        CettaEvalCompletion completion = CETTA_EVAL_COMPLETE;
        assert(cetta_prepared_pure_answer_cursor_return(cursor, &pending, &completion) == CETTA_OWNED_EXECUTION_HANDOFF);
        CettaInvocation invocation;
        bool entered = false;
        assert(cetta_prepared_pure_answer_cursor_native_boundary(cursor, &invocation, &entered, &pending) && entered);
        assert(pending.outcome.kind == CETTA_CALL_SUSPENDED);
        Arena output;
        arena_init_detached(&output);
        Bindings captures;
        bindings_init(&captures);
        Atom *sentinel = atom_var_with_id(&output, "$unchanged", fresh_var_id());
        assert(bindings_add_var(&captures, sentinel, atom_int(&output, 99)));
        Atom *hole = atom_var_with_id(&output, "$result", fresh_var_id());
        AtomDeepCopySession *copy = atom_deep_copy_session_new(&output);
        assert(copy);
        Atom *resumption = sentinel;
        assert(!cetta_prepared_pure_answer_cursor_frame_resumption(cursor, SIZE_MAX, &output,
            hole, eval_owned_residual_import, copy, &captures, &resumption) && !resumption);
        assert(atom_eq(bindings_lookup_value_id(&captures, sentinel->var_id).skeleton, atom_int(&output, 99)));
        assert(cetta_prepared_pure_answer_cursor_frame_resumption(cursor,
            cetta_prepared_pure_answer_cursor_frame_count(cursor) - 1u, &output,
            hole, eval_owned_residual_import, copy, &captures, &resumption));
        atom_deep_copy_session_free(copy);
        assert(!bindings_lookup_value_id(&captures, sentinel->var_id).skeleton);
        Atom *value = fixture_atom(&output, datum == 0u ? "(function (return (readln!)))"
            : datum == 1u ? "(readln!)" : "(Shared 7 7)");
        assert(bindings_add_var(&captures, hole, value));
        OutcomeSet values;
        outcome_set_init(&values);
        metta_call_with_env(&space, &output, resumption, NULL, -1, &captures, &values);
        assert(values.len == 1u);
        Atom *observed = bindings_apply_if_vars(&values.items[0].env, &output,
            outcome_atom_materialize(&output, &values.items[0]));
        Atom *expected = tail ? value : atom_expr(&output, (Atom *[]){
            atom_symbol(&output, "Pair"), atom_int(&output, 11), value, value}, 4u);
        assert(atom_eq(observed, expected) && prepared_calls == 1u && !prepared_effects);
        outcome_set_free(&values);
        checks++;
        CettaOwnedReturn after;
        assert(cetta_prepared_pure_answer_cursor_native_boundary(cursor, &invocation, &entered, &after) &&
            after.outcome.term == pending.outcome.term && after.outcome.delayed == pending.outcome.delayed);
        bindings_free(&captures);
        cetta_prepared_pure_answer_cursor_close(cursor);
        cetta_prepared_pure_program_free(program);
        arena_free(&output);
        assert(prepared_calls == 1u && !prepared_effects);
    }
    prepared_fixture = false;
    space_free(&space);
    arena_free(&source);
    eval_set_library_context(NULL);
    cetta_library_context_free(library);
    free(library);
    return checks;
}

static bool body_storage_authority(const void *context) {
    return *(const bool *)context;
}

static bool body_storage_interrupt(void *context) {
    return *(const bool *)context;
}

/* Opening borrows storage; the first pull admits the actual authority.
 * Refused and successful invocations repeatedly reuse the same body plan. */
static unsigned prepared_body_open_admission_controls(void) {
    Arena source;
    arena_init_detached(&source);
    Space space;
    space_init(&space);
    Atom *parameter = atom_var_with_id(&source, "$open-authority-input", fresh_var_id());
    Atom *body = atom_expr2(&source,
        atom_symbol(&source, "__cetta_lib_json_parse_v1"), parameter);
    CettaPreparedPureProgram *program = test_body_program(&space, body, &parameter->var_id, 1u);
    assert(program);
    PettaEvalMachineContext context = {.library_context = g_library_context,
        .fuel = -1, .semantic_authority_epoch = 1u};
    PettaCallableAuthority authority = {.context = &context, .library = g_library_context};
    assert(petta_eval_machine_answer_authority_token(&context, &authority.token));
    PettaPreparedCallable plan = {.program = program, .body_authority = &authority};
    prepared_head = symbol_intern_cstr(g_symbols, "__cetta_lib_json_parse_v1");
    bool saved_fixture = prepared_fixture;
    prepared_fixture = true;
    unsigned checks = 0u;
    for (unsigned round = 0u; round < 24u; round++) {
        Arena loan;
        arena_init_detached(&loan);
        Atom *arguments[] = {atom_int(&loan, round)};
        unsigned mode = round % 3u;
        context.semantic_authority_epoch = mode == 0u ? 2u : 1u;
        prepared_mode = 0u;
        prepared_calls = prepared_attempts = 0u;
        CettaPreparedPureAnswerCursor *cursor = petta_eval_open_callable_answers(
            &context, &plan, &loan, arguments, 1u);
        assert(cursor && !prepared_calls);
        if (mode == 1u)
            context.semantic_authority_epoch = 2u;
        Atom *answer = NULL;
        CettaPreparedPureCursorStep step = cetta_prepared_pure_answer_cursor_next(cursor, &answer);
        if (mode < 2u) {
            assert(step == CETTA_PREPARED_PURE_CURSOR_HANDOFF && !answer &&
                !prepared_calls && !prepared_attempts &&
                cetta_prepared_pure_answer_cursor_handoff_reason(cursor) ==
                    CETTA_PREPARED_PURE_HANDOFF_STALE);
            assert(cetta_prepared_pure_answer_cursor_next(cursor, &answer) == step &&
                !answer && !prepared_calls);
        } else {
            assert(step == CETTA_PREPARED_PURE_CURSOR_ANSWER &&
                atom_eq(answer, atom_int(&loan, 7)) && prepared_calls == 1u);
            assert(cetta_prepared_pure_answer_cursor_next(cursor, &answer) ==
                CETTA_PREPARED_PURE_CURSOR_EXHAUSTED && !answer && prepared_calls == 1u);
        }
        cetta_prepared_pure_answer_cursor_close(cursor);
        arena_free(&loan);
        checks++;
    }
    prepared_fixture = saved_fixture;
    cetta_prepared_pure_program_free(program);
    space_free(&space);
    arena_free(&source);
    return checks;
}

/* Alternating terminal states must leave no input, callback or allowance in
 * the next invocation. Promoted siblings survive retirement of their loan. */
static unsigned prepared_body_storage_controls(void) {
    Arena source, published;
    arena_init_detached(&source);
    arena_init_detached(&published);
    Space space;
    space_init(&space);
    for (unsigned index = 0u; index < 2u; index++) {
        Atom *equation = fixture_atom(&source, "(= (storage-choice $x) (Box $x $x))");
        const PettaPlanNode *plan = petta_program_plan_dynamic_add(
            g_library_context->petta_program, equation);
        assert(plan);
        space_add(&space, equation);
        assert(petta_program_note_add(g_library_context->petta_program, &space,
            space_get_at64(&space, index), plan));
    }
    Atom *parameter = atom_var_with_id(&source, "$storage-argument", fresh_var_id());
    Atom *body = atom_expr2(&source, atom_symbol(&source, "storage-choice"), parameter);
    CettaPreparedPureProgram *program = test_body_program(&space, body, &parameter->var_id, 1u);
    assert(program);
    unsigned checks = 1u;
    for (unsigned round = 0u; round < 24u; round++) {
        Arena loan;
        arena_init_detached(&loan);
        Atom *args[] = {atom_expr2(&loan, atom_symbol(&loan, "Input"), atom_int(&loan, round))};
        bool current = round % 6u != 1u;
        bool interrupted = round % 6u == 2u;
        CettaPreparedPureAnswerCursorOptions options = {
            .arena = &loan, .borrowed_until_escape = true,
            .limits = {.max_transitions = round % 6u == 0u ? 0u : UINT64_MAX,
                .max_scratch_bytes = 1024u * 1024u,
                .construct_allocation_bound = petta_semantics_construct_value_allocation_bound},
            .interrupt_poll = body_storage_interrupt, .interrupt_context = &interrupted,
            .interrupt_poll_interval = 1u, .continuation_current = body_storage_authority,
            .continuation_context = &current, .allow_continuations = true,
            .unmatched_call = CETTA_PREPARED_PURE_UNMATCHED_FAILS};
        CettaPreparedPureAnswerCursor *cursor = cetta_prepared_pure_answer_cursor_open_body(
            program, args, 1u, &options);
        assert(cursor);
        Atom *value = NULL;
        CettaPreparedPureCursorStep step = cetta_prepared_pure_answer_cursor_next(cursor, &value);
        if (round % 6u < 3u) {
            assert(step == CETTA_PREPARED_PURE_CURSOR_HANDOFF && !value);
            assert(cetta_prepared_pure_answer_cursor_handoff_reason(cursor) ==
                (round % 6u == 0u ? CETTA_PREPARED_PURE_HANDOFF_LIMIT
                 : !current ? CETTA_PREPARED_PURE_HANDOFF_STALE
                 : CETTA_PREPARED_PURE_HANDOFF_INTERRUPT));
        } else {
            assert(step == CETTA_PREPARED_PURE_CURSOR_ANSWER &&
                value->kind == ATOM_EXPR && value->expr.len == 3u &&
                atom_eq(value->expr.elems[1], args[0]) &&
                atom_eq(value->expr.elems[1], value->expr.elems[2]));
            Atom *saved = cetta_prepared_pure_answer_cursor_copy_value(cursor, &published, value);
            assert(saved);
            if (round % 6u != 3u) {
                assert(cetta_prepared_pure_answer_cursor_detach(cursor));
                cetta_prepared_pure_answer_cursor_suspend(cursor);
                arena_free(&loan);
                arena_init_detached(&loan);
                assert(cetta_prepared_pure_answer_cursor_next(cursor, &value) ==
                    CETTA_PREPARED_PURE_CURSOR_ANSWER && atom_eq(value, saved));
                assert(cetta_prepared_pure_answer_cursor_next(cursor, &value) ==
                    CETTA_PREPARED_PURE_CURSOR_EXHAUSTED);
            }
            cetta_prepared_pure_answer_cursor_close(cursor);
            cursor = NULL;
            arena_reset(&loan, (ArenaMark){0});
            assert(saved->kind == ATOM_EXPR &&
                atom_eq(saved->expr.elems[1]->expr.elems[1], atom_int(&published, round)));
        }
        cetta_prepared_pure_answer_cursor_close(cursor);
        arena_free(&loan);
        checks += 4u;
    }
    cetta_prepared_pure_program_free(program);
    space_free(&space);
    arena_free(&source);
    arena_free(&published);
    return checks;
}

static unsigned prepared_body_entry_controls(void) {
    CettaLibraryContext *library = calloc(1u, sizeof(*library));
    assert(library);
    cetta_library_context_init_for_language_profile(library, CETTA_LANGUAGE_PETTA, NULL);
    eval_set_library_context(library);
    Arena source;
    arena_init_detached(&source);
    Space space;
    space_init(&space);
    prepared_head = symbol_intern_cstr(g_symbols, "__cetta_lib_json_parse_v1");
    prepared_fixture = true;
    Atom *callable = petta_capture_lambda(&source, fixture_atom(&source,
        "(|-> ($x) (let $y (__cetta_lib_json_parse_v1 $x) (Pair $x $y $y)))"), NULL);
    Atom *parameter = atom_var_with_id(&source, "$body-argument", fresh_var_id());
    Atom *body = cetta_petta_open_callable(&source, callable, parameter);
    assert(body);
    CettaPreparedPureProgram *program = test_body_program(&space, body, &parameter->var_id, 1u);
    assert(program && cetta_prepared_pure_program_equation_count(program) == 0u);
    unsigned checks = 1u + prepared_body_storage_controls() + prepared_body_open_admission_controls();
    VarId duplicates[] = {parameter->var_id, parameter->var_id};
    assert(!test_body_program(&space, body, duplicates, 2u));
    assert(!test_body_program(&space, body, NULL, 1u));
    assert(!test_body_program(&space, body, &parameter->var_id, 65u));
    VarId absent = VAR_ID_NONE;
    assert(!test_body_program(&space, body, &absent, 1u));
    checks += 4u;
    {
        enum { WIDTH = 64u };
        VarId parameters[WIDTH];
        Atom *elements[WIDTH + 1u], *arguments[WIDTH];
        elements[0] = atom_symbol(&source, "Record");
        for (unsigned index = 0u; index < WIDTH; index++) {
            parameters[index] = fresh_var_id();
            elements[index + 1u] = atom_var_with_id(&source, "$position", parameters[index]);
            arguments[index] = atom_int(&source, index % 2u ? (int64_t)index : 7);
        }
        Atom *record = atom_expr(&source, elements, WIDTH + 1u);
        CettaPreparedPureProgram *wide = test_body_program(&space, record, parameters, WIDTH);
        assert(wide && cetta_prepared_pure_program_equation_count(wide) == 0u);
        CettaPreparedPureAnswerCursorOptions options = {
            .limits = {.max_transitions = UINT64_MAX, .max_scratch_bytes = 1024u * 1024u,
                .construct_allocation_bound = petta_semantics_construct_value_allocation_bound},
            .allow_continuations = true};
        CettaPreparedPureAnswerCursor *cursor = cetta_prepared_pure_answer_cursor_open_body(
            wide, arguments, WIDTH, &options);
        assert(cursor);
        CettaOwnedReturn returned;
        CettaEvalCompletion completion = CETTA_EVAL_COMPLETE;
        assert(prepared_pure_next_return(&space, cursor, 64u, &returned, &completion) ==
            CETTA_OWNED_EXECUTION_RETURN && returned.final &&
            returned.outcome.result_form == CETTA_CALL_RESULT_COMPLETED_VALUE &&
            returned.outcome.term->expr.len == WIDTH + 1u);
        for (unsigned index = 0u; index < WIDTH; index++) {
            assert(atom_eq(returned.outcome.term->expr.elems[index + 1u], arguments[index]));
            checks++;
        }
        cetta_prepared_pure_answer_cursor_close(cursor);
        cetta_prepared_pure_program_free(wide);
        checks += 3u;
    }
    const unsigned modes[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 10u, 14u, 15u, 17u, 18u, 19u, 20u};
    const uint64_t allowances[] = {0u, 1u, 64u, UINT64_MAX};
    for (size_t m = 0u; m < sizeof(modes) / sizeof(*modes); m++)
    for (size_t a = 0u; a < sizeof(allowances) / sizeof(*allowances); a++) {
        prepared_mode = modes[m];
        prepared_calls = prepared_attempts = prepared_effects = 0u;
        Arena arguments;
        arena_init_detached(&arguments);
        Atom *input = atom_int(&arguments, 11);
        Atom *args[] = {input};
        CettaPreparedPureAnswerCursorOptions options = {
            .limits = {.max_transitions = allowances[a],
                .max_scratch_bytes = 1024u * 1024u,
                .construct_allocation_bound = petta_semantics_construct_value_allocation_bound},
            .allow_continuations = true,
            .unmatched_call = CETTA_PREPARED_PURE_UNMATCHED_FAILS};
        assert(!cetta_prepared_pure_answer_cursor_open(program, &options));
        CettaPreparedPureAnswerCursor *cursor = cetta_prepared_pure_answer_cursor_open_body(
            program, args, 1u, &options);
        assert(cursor && !prepared_calls && !prepared_attempts &&
            cetta_prepared_pure_answer_cursor_head_count(cursor) == 0u);
        assert(cetta_prepared_pure_answer_cursor_detach(cursor));
        arena_free(&arguments);
        unsigned count = 0u;
        CettaOwnedExecutionStep step;
        CettaEvalCompletion completion = CETTA_EVAL_COMPLETE;
        for (;;) {
            CettaOwnedReturn returned;
            step = prepared_pure_next_return(&space, cursor, 64u, &returned, &completion);
            if (step != CETTA_OWNED_EXECUTION_RETURN)
                break;
            assert(++count <= 3u);
            if (prepared_mode == 4u)
                assert(returned.outcome.kind == CETTA_CALL_RAISED && atom_is_error(returned.outcome.term));
            else {
                Atom *value = returned.outcome.term;
                assert(returned.outcome.kind == CETTA_CALL_VALUE &&
                    returned.outcome.result_form == CETTA_CALL_RESULT_COMPLETED_VALUE &&
                    value && value->kind == ATOM_EXPR && value->expr.len == 4u &&
                    atom_is_symbol(value->expr.elems[0], "Pair") &&
                    atom_eq(value->expr.elems[1], atom_int(&source, 11)) &&
                    atom_eq(value->expr.elems[2], value->expr.elems[3]));
                if (prepared_mode == 2u)
                    assert(atom_eq(value->expr.elems[2], atom_int(&source, count == 3u ? 9 : 7)));
                if (prepared_mode == 17u || prepared_mode == 18u)
                    assert(atom_eq(value->expr.elems[2], fixture_atom(&source,
                        prepared_mode == 17u ? "(+ (readln!) 1)" : "(readln!)")));
                if (prepared_mode == 6u)
                    assert(atom_is_error(value->expr.elems[2]));
                if (prepared_mode == 7u)
                    assert(atom_is_symbol(value->expr.elems[2], "Empty"));
            }
        }
        if (prepared_calls != 1u || prepared_attempts != 1u)
            fprintf(stderr, "body entry mode=%u allowance=%llu calls=%u attempts=%u count=%u step=%u\n",
                prepared_mode, (unsigned long long)allowances[a], prepared_calls,
                prepared_attempts, count, step);
        assert(prepared_calls == 1u && prepared_attempts == 1u);
        unsigned expected = prepared_mode == 2u ? 3u
            : prepared_mode == 3u || prepared_mode == 8u || prepared_mode == 10u ||
              prepared_mode == 15u || prepared_mode == 20u ? 0u
            : prepared_mode == 14u || prepared_mode == 17u || prepared_mode == 19u ? 2u : 1u;
        assert(count == expected);
        if (prepared_mode == 8u || prepared_mode == 20u)
            assert(step == CETTA_OWNED_EXECUTION_INTERRUPTED &&
                completion == CETTA_EVAL_INCOMPLETE_CANCELLED);
        else if (prepared_mode == 10u)
            assert(step == CETTA_OWNED_EXECUTION_HANDOFF &&
                completion == CETTA_EVAL_INCOMPLETE_HOST_FAILURE);
        else
            assert(step == CETTA_OWNED_EXECUTION_COMPLETE && completion == CETTA_EVAL_COMPLETE);
        assert(prepared_effects == (prepared_mode == 14u || prepared_mode == 20u ? 2u
            : prepared_mode == 5u ? 1u : 0u));
        cetta_prepared_pure_answer_cursor_close(cursor);
        assert(prepared_calls == 1u);
        checks += 6u;
    }
    CettaCallOutcome raw;
    prepared_calls = prepared_attempts = 0u;
    assert(!cetta_prepared_pure_program_execute_closed_controlled(program, &source,
        0u, NULL, NULL, &raw) && !prepared_calls && !prepared_attempts);
    checks++;
    {
        Arena arguments, published;
        arena_init_detached(&arguments);
        arena_init_detached(&published);
        Atom *args[] = {fixture_atom(&arguments, "(shared payload)")};
        CettaPreparedPureAnswerCursorOptions options = {
            .limits = {.max_transitions = UINT64_MAX, .max_scratch_bytes = 1024u * 1024u,
                .construct_allocation_bound = petta_semantics_construct_value_allocation_bound},
            .allow_continuations = true};
        prepared_mode = 24u;
        prepared_calls = prepared_attempts = 0u;
        CettaPreparedPureAnswerCursor *cursor = cetta_prepared_pure_answer_cursor_open_body(
            program, args, 1u, &options);
        assert(cursor && cetta_prepared_pure_answer_cursor_detach(cursor));
        arena_free(&arguments);
        CettaOwnedReturn returned;
        CettaEvalCompletion completion = CETTA_EVAL_COMPLETE;
        assert(prepared_pure_next_return(&space, cursor, 64u, &returned, &completion) ==
            CETTA_OWNED_EXECUTION_RETURN);
        Atom *value = atom_deep_copy(&published, returned.outcome.term);
        cetta_prepared_pure_answer_cursor_close(cursor);
        assert(value && atom_eq(value->expr.elems[1], value->expr.elems[2]) &&
            atom_eq(value->expr.elems[2], value->expr.elems[3]) &&
            atom_eq(value->expr.elems[1], fixture_atom(&published, "(shared payload)")) &&
            prepared_calls == 1u && prepared_attempts == 1u);
        checks += 3u;
        arena_free(&published);
    }
    {
        Atom *args[] = {atom_int(&source, 11)};
        CettaPreparedPureAnswerCursorOptions options = {
            .limits = {.max_transitions = UINT64_MAX, .max_scratch_bytes = 1024u * 1024u,
                .construct_allocation_bound = petta_semantics_construct_value_allocation_bound},
            .allow_continuations = true};
        prepared_mode = 25u;
        prepared_mutation_arena = &source;
        prepared_calls = prepared_attempts = 0u;
        CettaPreparedPureAnswerCursor *cursor = cetta_prepared_pure_answer_cursor_open_body(
            program, args, 1u, &options);
        assert(cursor);
        CettaOwnedReturn returned;
        CettaEvalCompletion completion = CETTA_EVAL_COMPLETE;
        assert(prepared_pure_next_return(&space, cursor, 64u, &returned, &completion) ==
            CETTA_OWNED_EXECUTION_HANDOFF && completion == CETTA_EVAL_INCOMPLETE_INVALIDATED);
        CettaInvocation invocation;
        bool entered = false;
        CettaOwnedReturn snapshot;
        assert(cetta_prepared_pure_answer_cursor_native_boundary(cursor, &invocation, &entered, &snapshot));
        assert(entered && snapshot.outcome.kind == CETTA_CALL_VALUE &&
            snapshot.outcome.result_form == CETTA_CALL_RESULT_COMPLETED_VALUE &&
            atom_eq(snapshot.outcome.term, atom_int(&source, 17)) &&
            prepared_calls == 1u && prepared_attempts == 1u);
        CettaCallReturnCursor replacement_value = {.outcome = cetta_call_completed_value(atom_int(&source, 99))};
        CettaOwnedExecution replacement;
        cetta_owned_execution_init(&replacement, &replacement_value, cetta_call_return_next, NULL);
        assert(!cetta_prepared_pure_answer_cursor_complete_native(cursor, &replacement, false) &&
            replacement.computation == &replacement_value);
        cetta_owned_execution_cancel(&replacement);
        cetta_prepared_pure_answer_cursor_close(cursor);
        assert(prepared_calls == 1u);
        prepared_mutation_arena = NULL;
        checks += 5u;
    }
    cetta_prepared_pure_program_free(program);
    prepared_fixture = false;
    space_free(&space);
    arena_free(&source);
    eval_set_library_context(NULL);
    cetta_library_context_free(library);
    free(library);
    return checks;
}

static unsigned fold_completion_controls(void) {
    const CettaLanguageId languages[] = {
        CETTA_LANGUAGE_PETTA, CETTA_LANGUAGE_HE, CETTA_LANGUAGE_PRIME,
    };
    const char *data[] = {"42", "(return 17)", "(Error payload marker)",
        "(empty)", "(+ 1 2)"};
    unsigned checks = 0u;
    for (size_t language = 0u; language < 3u; language++) {
        CettaLibraryContext *library = calloc(1u, sizeof(*library));
        assert(library);
        cetta_library_context_init_for_language_profile(library, languages[language], NULL);
        eval_set_library_context(library);
        Arena arena;
        arena_init(&arena);
        Space space;
        space_init(&space);
        Atom *head = atom_symbol_id(&arena, g_builtin_syms.minimal_foldl_llist);
        Atom *accumulator = atom_var(&arena, "$fold_accumulator");
        Atom *item = atom_var(&arena, "$fold_item");
        Atom *arguments[] = {
            atom_symbol_id(&arena, g_builtin_syms.llist_nil), NULL,
            accumulator, item, accumulator, atom_space(&arena, &space),
        };
        for (size_t datum = 0u; datum < 5u; datum++) {
            Atom *value = fixture_atom(&arena, data[datum]);
            arguments[1] = value;
            CettaCallOutcome core = eval_minimal_foldl_llist(&arena, head, arguments, 6u);
            assert(core.kind == CETTA_CALL_VALUE &&
                core.result_form == CETTA_CALL_RESULT_COMPLETED_VALUE && core.term == value);
            Atom *wrapped = atom_expr2(&arena,
                atom_symbol_id(&arena, g_builtin_syms.return_text), value);
            assert(atom_eq(minimal_foldl_llist_expression(&arena, core), wrapped));
            CettaCallOutcome returned = cetta_call_failure();
            assert(dispatch_native_op(&space, &arena, head, arguments, 6u, &returned));
            assert(returned.kind == CETTA_CALL_VALUE &&
                returned.result_form == (languages[language] == CETTA_LANGUAGE_PETTA
                    ? CETTA_CALL_RESULT_COMPLETED_VALUE : CETTA_CALL_RESULT_EXPRESSION) &&
                atom_eq(returned.term, languages[language] == CETTA_LANGUAGE_PETTA
                    ? value : wrapped));
            assert(atom_eq(dispatch_named_native(&space, &arena,
                g_builtin_syms.minimal_foldl_llist, arguments, 6u), wrapped));
            /* An expression packet with the very same data has no completion
             * authority, including when the data spells return or Error. */
            assert(minimal_foldl_llist_expression(&arena, cetta_call_value(value)) == value);
            checks += 6u;
        }
        arguments[0] = atom_symbol(&arena, "malformed-list");
        arguments[1] = fixture_atom(&arena, "(return 17)");
        CettaCallOutcome bad = eval_minimal_foldl_llist(&arena, head, arguments, 6u);
        assert(bad.kind == CETTA_CALL_VALUE &&
            bad.result_form == CETTA_CALL_RESULT_EXPRESSION && atom_is_error(bad.term));
        assert(minimal_foldl_llist_expression(&arena, bad) == bad.term);
        bad = eval_minimal_foldl_llist(&arena, head, NULL, 0u);
        assert(bad.kind == CETTA_CALL_VALUE &&
            bad.result_form == CETTA_CALL_RESULT_EXPRESSION && atom_is_error(bad.term));
        checks += 3u;
        space_free(&space);
        arena_free(&arena);
        eval_set_library_context(NULL);
        cetta_library_context_free(library);
        free(library);
    }
    return checks;
}

static CettaPreparedPureExpressionViewState conflicting_data_view(
    const Atom *expression, const CettaComputationPlan *computation,
    CettaPreparedPureExpressionView *view) {
    (void)expression;
    (void)computation;
    (void)view;
    return CETTA_PREPARED_PURE_EXPRESSION_DATA;
}

static unsigned prime_prepared_data_controls(void) {
    CettaLibraryContext *library = calloc(1u, sizeof(*library));
    assert(library);
    cetta_library_context_init_for_language_profile(library, CETTA_LANGUAGE_PRIME, NULL);
    eval_set_library_context(library);
    Arena arena;
    arena_init(&arena);
    Space space;
    space_init(&space);
    const char *equations[] = {
        "(= (data-pending $x) (+ $x 1))",
        "(= (data-quoted $x) (quote (Pair $x $x)))",
        "(= (data-two-quotes $x) (Pair (quote $x) (quote $x)))",
        "(= (data-two-pending $x) (Pair $x $x))",
        "(= (data-countdown $n $x) (if (== $n 0) (quote (Pair $x $x)) (data-countdown (- $n 1) $x)))",
    };
    for (size_t i = 0u; i < sizeof(equations) / sizeof(*equations); i++)
        space_add(&space, fixture_atom(&arena, equations[i]));
    const struct { const char *source, *expected; } cases[] = {
        {"(quote (Pair (data-pending 7) (data-pending 7)))",
         "(quote (Pair (data-pending 7) (data-pending 7)))"},
        {"(data-quoted (data-pending 7))",
         "(quote (Pair (data-pending 7) (data-pending 7)))"},
        {"(data-two-quotes (data-pending 7))",
         "(Pair (quote (data-pending 7)) (quote (data-pending 7)))"},
        {"(quote (+ 1 2))", "(quote (+ 1 2))"},
        {"(data-countdown 4096 (data-pending 7))",
         "(quote (Pair (data-pending 7) (data-pending 7)))"},
    };
    unsigned checks = 0u;
    for (size_t i = 0u; i < sizeof(cases) / sizeof(*cases); i++) {
        Atom *source = fixture_atom(&arena, cases[i].source);
        CettaPreparedPureProgram *program = cetta_prepared_pure_program_compile_closed(
            &space, source, CETTA_GSLT_PURE_CALL_CALL_BY_NEED, atom_bool, atom_expr,
            cetta_prepared_pure_constructs_expression_always, NULL,
            cetta_prepared_pure_register_view, prime_prepared_pure_expression_view,
            NULL, NULL, false, true,
            (CettaMatchDecisionSemanticIdentity){
                .compiler_identity = cetta_match_decision_compiler_identity()}, NULL);
        assert(program);
        CettaCallOutcome returned = cetta_call_failure();
        bool ran = cetta_prepared_pure_program_execute_closed_controlled(
            program, &arena, 65536u, NULL, NULL, &returned);
        assert(ran && returned.kind == CETTA_CALL_VALUE && returned.term &&
            returned.result_form == CETTA_CALL_RESULT_COMPLETED_VALUE &&
            atom_eq(returned.term, fixture_atom(&arena, cases[i].expected)));
        assert(cetta_prepared_pure_program_value_is_fully_evaluated(program, returned.term));
        if (i + 1u == sizeof(cases) / sizeof(*cases)) {
            assert(cetta_prepared_pure_program_closed_survivor_arena(program));
            Atom *pair = returned.term->expr.elems[1];
            assert(pair->kind == ATOM_EXPR && pair->expr.len == 3u &&
                pair->expr.elems[1] == pair->expr.elems[2]);
            checks += 2u;
        }
        cetta_prepared_pure_program_free(program);
        checks += 3u;
    }
    CettaPreparedPureProgram *pending = cetta_prepared_pure_program_compile_closed(
        &space, fixture_atom(&arena, "(data-two-pending (data-pending 7))"),
        CETTA_GSLT_PURE_CALL_CALL_BY_NEED, atom_bool, atom_expr,
        cetta_prepared_pure_constructs_expression_always, NULL,
        cetta_prepared_pure_register_view, prime_prepared_pure_expression_view,
        NULL, NULL, false, true,
        (CettaMatchDecisionSemanticIdentity){
            .compiler_identity = cetta_match_decision_compiler_identity()}, NULL);
    assert(pending);
    CettaCallOutcome refused = cetta_call_failure();
    assert(!cetta_prepared_pure_program_execute_closed_controlled(
        pending, &arena, 65536u, NULL, NULL, &refused));
    cetta_prepared_pure_program_free(pending);
    CettaPreparedPureExpressionView view = {0};
    Atom *quoted = fixture_atom(&arena, "(quote (+ 1 2))");
    CettaComputationPlan force = {
        .control = {CETTA_CONTROL_FORCE, 0u,
            CETTA_CONTROL_OPERAND_ABSENT, CETTA_CONTROL_OPERAND_ABSENT}};
    assert(prime_prepared_pure_expression_view(quoted, &force, &view) ==
        CETTA_PREPARED_PURE_EXPRESSION_DEFAULT);
    assert(he_prepared_pure_expression_view(quoted, NULL, &view) ==
        CETTA_PREPARED_PURE_EXPRESSION_DEFAULT);
    PettaPlanNode forced = {.role = PETTA_PLAN_STATIC_CALL, .computation = force};
    CettaPreparedPureSourceView forced_view = {
        .root = &forced,
        .role = petta_prepared_pure_source_role,
        .computation = petta_prepared_pure_source_computation,
    };
    CettaPreparedPureProgram *conflict = cetta_prepared_pure_program_compile_closed(
        &space, quoted, CETTA_GSLT_PURE_CALL_CALL_BY_NEED, atom_bool, atom_expr,
        cetta_prepared_pure_constructs_expression_always, NULL,
        cetta_prepared_pure_register_view, conflicting_data_view,
        NULL, &forced_view, false, true,
        (CettaMatchDecisionSemanticIdentity){
            .compiler_identity = cetta_match_decision_compiler_identity()}, NULL);
    assert(!conflict);
    space_free(&space);
    arena_free(&arena);
    eval_set_library_context(NULL);
    cetta_library_context_free(library);
    free(library);
    return checks + 5u;
}

/* Actual native packets enter the ordinary relational continuation without
 * native replay. Conditional goals stay attached until binding or publication. */
static unsigned conditional_native_continuation_controls(void) {
    CettaLibraryContext *library = calloc(1u, sizeof(*library));
    assert(library);
    cetta_library_context_init_for_language_profile(library, CETTA_LANGUAGE_PETTA, NULL);
    eval_set_library_context(library);
    Arena source;
    arena_init_detached(&source);
    Space space;
    space_init(&space);
    prepared_head = symbol_intern_cstr(g_symbols, "__cetta_lib_json_parse_v1");
    prepared_fixture = true;
    bool prolog = cetta_lib_prolog_runtime_available(library->lib_prolog);
    const struct {
        const char *body, *answer;
        unsigned mode, count;
        bool suspended, open, shared, needs_prolog, effectful;
    } cases[] = {
        {"(__cetta_lib_json_parse_v1 $input)", NULL, 33u, 1u, false, true, false, false, false},
        {"(let $v (__cetta_lib_json_parse_v1 $input) (Pair $v $v))", NULL, 33u, 1u, false, true, true, false, false},
        {"(let $v (__cetta_lib_json_parse_v1 $input) (let 2 $v (Pair $v $v)))", "(Pair 2 2)", 33u, 1u, false, false, true, false, false},
        {"(__cetta_lib_json_parse_v1 $input)", NULL, 36u, 1u, false, true, false, false, false},
        {"(__cetta_lib_json_parse_v1 $input)", NULL, 34u, 1u, true, true, false, true, false},
        {"(let $v (__cetta_lib_json_parse_v1 $input) (let 1 $v rejected))", NULL, 34u, 0u, false, false, false, true, false},
        {"(let $v (__cetta_lib_json_parse_v1 $input) (let 2 $v (Pair $v $v)))", "(Pair 2 2)", 34u, 1u, false, false, true, true, false},
        {"(__cetta_lib_json_parse_v1 $input)", NULL, 37u, 1u, false, true, true, false, false},
        {"(let $v (__cetta_lib_json_parse_v1 $input) (Outer $v $v))", NULL, 35u, 1u, true, true, true, true, false},
        {"(let $v (__cetta_lib_json_parse_v1 $input) (let $n (conditional-choice $input) (let $_ (__cetta_lib_json_parse_v1 99) (Pair $v $v))))", NULL, 33u, 3u, false, true, true, false, true},
        {"(let $v (__cetta_lib_json_parse_v1 $input) (let $n (conditional-bind $input) (let $n $v (let $_ (__cetta_lib_json_parse_v1 99) (Pair $v $v)))))", "(Pair 2 2)", 34u, 2u, false, false, true, true, true},
        {"(__cetta_lib_json_parse_v1 $input)", NULL, 39u, 1u, false, true, false, false, false},
        {"(let $v (__cetta_lib_json_parse_v1 $input) (Outer $v $v))", NULL, 39u, 1u, false, true, true, false, false},
        {"(let $v (__cetta_lib_json_parse_v1 $input) (let $_ (__cetta_lib_json_parse_v1 99) (Pair $v $v)))", NULL, 40u, 3u, false, true, true, false, true},
    };
    const char *equations[] = {
        "(= (conditional-choice $x) 7)", "(= (conditional-choice $x) 7)", "(= (conditional-choice $x) 9)",
        "(= (conditional-bind $x) 1)", "(= (conditional-bind $x) 2)", "(= (conditional-bind $x) 2)"};
    for (unsigned index = 0u; index < sizeof(equations) / sizeof(*equations); index++) {
        Atom *equation = fixture_atom(&source, equations[index]);
        const PettaPlanNode *plan = petta_program_plan_dynamic_add(library->petta_program, equation);
        assert(plan);
        space_add(&space, equation);
        assert(petta_program_note_add(library->petta_program, &space,
            space_get_at64(&space, space_length64(&space) - 1u), plan));
    }
    const uint64_t allowances[] = {0u, 1u, 2u, 64u};
    unsigned checks = 0u;
    for (unsigned item = 0u; item < sizeof(cases) / sizeof(*cases); item++) {
        if (cases[item].needs_prolog && !prolog)
            continue;
        Atom *parameter = atom_var_with_id(&source, "$input", fresh_var_id());
        char text[512];
        assert(snprintf(text, sizeof(text), "(|-> ($input) %s)", cases[item].body) < (int)sizeof(text));
        Atom *callable = petta_capture_lambda(&source, fixture_atom(&source, text), NULL);
        Atom *body = callable ? cetta_petta_open_callable(&source, callable, parameter) : NULL;
        CettaPreparedPureProgram *program = body ? test_body_program(&space, body, &parameter->var_id, 1u) : NULL;
        if (!program) fprintf(stderr, "conditional compile item=%u\n", item);
        assert(program);
        for (unsigned budget = 0u; budget < 4u; budget++)
        for (unsigned early = 0u; early < 2u; early++) {
            CettaPreparedPureAnswerCursorOptions options = {
                .limits = {.max_transitions = allowances[budget],
                    .max_scratch_bytes = 1024u * 1024u,
                    .construct_allocation_bound = petta_semantics_construct_value_allocation_bound},
                .allow_continuations = true};
            Atom *arguments[] = {atom_int(&source, 11)};
            CettaPreparedPureAnswerCursor *cursor = cetta_prepared_pure_answer_cursor_open_body(
                program, arguments, 1u, &options);
            assert(cursor);
            prepared_mode = cases[item].mode;
            prepared_calls = prepared_attempts = prepared_effects = 0u;
            unsigned count = 0u;
            CettaEvalCompletion completion = CETTA_EVAL_COMPLETE;
            CettaOwnedReturn returned;
            CettaOwnedExecutionStep step;
            for (;;) {
                step = prepared_pure_next_return(&space, cursor, 32u, &returned, &completion);
                if (step != CETTA_OWNED_EXECUTION_RETURN)
                    break;
                assert(count < cases[item].count);
                Atom *value = bindings_apply_if_vars(returned.environment, &source, returned.outcome.term);
                if (!value || returned.outcome.kind != (cases[item].suspended
                        ? CETTA_CALL_SUSPENDED : CETTA_CALL_VALUE)) {
                    fprintf(stderr, "conditional value item=%u budget=%u kind=%u value=", item, budget, returned.outcome.kind);
                    atom_print(value, stderr); fputc('\n', stderr);
                }
                assert(cetta_prepared_pure_answer_cursor_owns_native_frontier(cursor));
                assert(value && returned.outcome.kind == (cases[item].suspended
                    ? CETTA_CALL_SUSPENDED : CETTA_CALL_VALUE));
                assert(atom_has_vars(value) == (cases[item].open && !(cases[item].mode == 40u && count == 2u)));
                if (cases[item].answer)
                    assert(atom_eq(value, fixture_atom(&source, cases[item].answer)));
                if (cases[item].shared)
                    assert(value->kind == ATOM_EXPR && value->expr.len == 3u &&
                        atom_eq(value->expr.elems[1], value->expr.elems[2]));
                if (cases[item].mode == 39u) {
                    Atom *payload = atom_is_symbol(value->expr.elems[0], "Outer") ? value->expr.elems[1] : value;
                    assert(payload->kind == ATOM_EXPR && payload->expr.len == 3u &&
                        atom_eq(payload->expr.elems[2], fixture_atom(&source, "(function (return (readln!)))")));
                }
                if (cases[item].suspended)
                    assert(returned.outcome.delayed);
                count++;
                assert(prepared_effects == (cases[item].effectful ? count : 0u));
                if (early)
                    break;
            }
            if (step == CETTA_OWNED_EXECUTION_HANDOFF || completion != CETTA_EVAL_COMPLETE)
                fprintf(stderr, "conditional continuation item=%u budget=%u early=%u count=%u step=%u completion=%u reason=%u\n",
                    item, budget, early, count, step, completion,
                    cetta_prepared_pure_answer_cursor_handoff_reason(cursor));
            assert(completion == CETTA_EVAL_COMPLETE && count == (early && cases[item].count ? 1u : cases[item].count) &&
                (early && count ? step == CETTA_OWNED_EXECUTION_RETURN : step == CETTA_OWNED_EXECUTION_COMPLETE));
            assert(prepared_calls == 1u && prepared_attempts >= 1u &&
                prepared_effects == (cases[item].effectful ? count : 0u));
            cetta_prepared_pure_answer_cursor_close(cursor);
            checks++;
        }
        cetta_prepared_pure_program_free(program);
        char definition[768], name[64];
        snprintf(name, sizeof(name), "conditional-entry-%u", item);
        assert(snprintf(definition, sizeof(definition), "(= (%s $input) %s)",
            name, cases[item].body) < (int)sizeof(definition));
        Atom *equation = fixture_atom(&source, definition);
        const PettaPlanNode *plan = petta_program_plan_dynamic_add(library->petta_program, equation);
        assert(plan);
        space_add(&space, equation);
        assert(petta_program_note_add(library->petta_program, &space,
            space_get_at64(&space, space_length64(&space) - 1u), plan));
        for (unsigned budget = 0u; budget < 4u; budget++)
        for (unsigned early = 0u; early < 2u; early++) {
            Arena execution;
            arena_init_detached(&execution);
            PettaEvalMachineContext context = {.fuel = -1, .library_context = library,
                .semantic_authority_epoch = 1u, .prepared_pure_cache = {.root_space = &space}};
            PettaMachineHost host = petta_eval_machine_host(&context, -1, false, false,
                false, true, false, petta_eval_machine_match_semantics(library, false));
            host.execute_prepared_pure_call = NULL;
            host.open_relation_cursor = NULL;
            host.open_answer_cursor = resumption_open_cursor;
            host.next_answer_cursor = resumption_next_cursor;
            resumption_root = symbol_intern_cstr(g_symbols, name);
            resumption_allowance = allowances[budget];
            resumption_pulls = resumption_native_transfers = 0u;
            prepared_caller_collect = cases[item].effectful;
            prepared_mode = cases[item].mode;
            prepared_calls = prepared_attempts = prepared_effects = 0u;
            Atom *query = atom_expr2(&execution, atom_symbol_id(&execution, resumption_root),
                atom_int(&execution, 11));
            PettaMachine machine;
            assert(petta_machine_init(&machine, &space, &execution, query, NULL, &host));
            unsigned count = 0u;
            PettaMachineStep step;
            for (;;) {
                Atom *answer = NULL;
                Bindings environment;
                step = petta_machine_next(&machine, &answer, &environment);
                if (step == PETTA_MACHINE_STEP_ANSWER) {
                    assert(count < cases[item].count && !petta_machine_last_answer_raised(&machine));
                    Atom *value = bindings_apply_if_vars(&environment, &execution, answer);
                    assert(value && atom_has_vars(value) == (cases[item].open && !(cases[item].mode == 40u && count == 2u)));
                    if (cases[item].answer)
                        assert(atom_eq(value, fixture_atom(&execution, cases[item].answer)));
                    if (cases[item].shared)
                        assert(value->kind == ATOM_EXPR && value->expr.len == 3u &&
                            atom_eq(value->expr.elems[1], value->expr.elems[2]));
                    if (cases[item].mode == 39u) {
                        Atom *payload = atom_is_symbol(value->expr.elems[0], "Outer") ? value->expr.elems[1] : value;
                        assert(payload->kind == ATOM_EXPR && payload->expr.len == 3u &&
                            atom_eq(payload->expr.elems[2], fixture_atom(&execution, "(function (return (readln!)))")));
                    }
                    Atom *delayed = NULL;
                    assert(petta_machine_answer_delayed(&machine, answer, &environment,
                        &execution, &delayed) && (delayed != NULL) == cases[item].suspended);
                    count++;
                    assert(prepared_effects == (cases[item].effectful ? count : 0u));
                }
                bindings_free(&environment);
                if (step != PETTA_MACHINE_STEP_ANSWER || early)
                    break;
            }
            if (step != PETTA_MACHINE_STEP_ANSWER && step != PETTA_MACHINE_STEP_EXHAUSTED)
                fprintf(stderr, "conditional parent item=%u budget=%u step=%u count=%u pulls=%u\n",
                    item, budget, step, count, resumption_pulls);
            assert(count == (early && cases[item].count ? 1u : cases[item].count) &&
                (early && count ? step == PETTA_MACHINE_STEP_ANSWER : step == PETTA_MACHINE_STEP_EXHAUSTED));
            assert(resumption_pulls && prepared_calls == 1u &&
                prepared_effects == (cases[item].effectful ? count : 0u));
            if (budget == 3u)
                assert(resumption_native_transfers == 1u);
            if (cases[item].effectful && resumption_native_transfers) {
                PettaMachineStats stats;
                assert(petta_machine_stats(&machine, &stats) &&
                    stats.choice_nursery_evacuations + stats.deterministic_heap_collections > 0u);
            }
            prepared_caller_collect = false;
            petta_machine_destroy(&machine);
            prepared_pure_program_cache_free(&context.prepared_pure_cache);
            arena_free(&execution);
            checks++;
        }
    }
    /* Moving a raw producer preserves its loan and every remaining occurrence.
     * A refused destination must leave the original frontier intact. */
    for (unsigned early = 0u; early < 2u; early++) {
        Atom *parameter = atom_var_with_id(&source, "$transfer-input", fresh_var_id());
        Atom *callable = petta_capture_lambda(&source, fixture_atom(&source,
            "(|-> ($x) (__cetta_lib_json_parse_v1 $x))"), NULL);
        Atom *body = cetta_petta_open_callable(&source, callable, parameter);
        CettaPreparedPureProgram *program = test_body_program(&space, body, &parameter->var_id, 1u);
        assert(program);
        CettaPreparedPureAnswerCursorOptions options = {
            .limits = {.max_transitions = 64u, .max_scratch_bytes = 1024u * 1024u,
                .construct_allocation_bound = petta_semantics_construct_value_allocation_bound},
            .allow_continuations = true};
        Atom *arguments[] = {atom_int(&source, 11)};
        CettaPreparedPureAnswerCursor *cursor = cetta_prepared_pure_answer_cursor_open_body(
            program, arguments, 1u, &options);
        assert(cursor);
        cetta_prepared_pure_answer_cursor_defer_native_completion(cursor);
        prepared_mode = 40u;
        prepared_calls = prepared_attempts = prepared_effects = 0u;
        CettaOwnedReturn pending;
        CettaEvalCompletion completion = CETTA_EVAL_COMPLETE;
        assert(prepared_pure_next_return(&space, cursor, 0u, &pending, &completion) ==
            CETTA_OWNED_EXECUTION_HANDOFF && completion == CETTA_EVAL_COMPLETE);
        CettaInvocation invocation;
        bool entered = false;
        assert(cetta_prepared_pure_answer_cursor_native_boundary(cursor, &invocation,
            &entered, &pending) && entered &&
            cetta_prepared_pure_answer_cursor_native_transfer_ready(cursor));
        CettaOwnedExecution refused = {.computation = &completion};
        assert(!cetta_prepared_pure_answer_cursor_take_native(cursor, &refused) &&
            refused.computation == &completion &&
            cetta_prepared_pure_answer_cursor_native_transfer_ready(cursor));
        CettaOwnedExecution producer = {0}, duplicate = {0};
        assert(cetta_prepared_pure_answer_cursor_take_native(cursor, &producer) &&
            !cetta_prepared_pure_answer_cursor_take_native(cursor, &duplicate));
        const CettaOwnedReturn *loan = NULL;
        assert(cetta_owned_execution_loan(&producer, &loan) == CETTA_OWNED_EXECUTION_RETURN &&
            loan->outcome.term == pending.outcome.term && loan->outcome.term->kind == ATOM_VAR);
        VarId first = loan->outcome.term->var_id;
        if (!early) {
            assert(cetta_owned_execution_accept(&producer) &&
                cetta_owned_execution_loan(&producer, &loan) == CETTA_OWNED_EXECUTION_RETURN &&
                loan->outcome.term->kind == ATOM_VAR &&
                !var_id_eq(loan->outcome.term->var_id, first));
            assert(cetta_owned_execution_accept(&producer) &&
                cetta_owned_execution_loan(&producer, &loan) == CETTA_OWNED_EXECUTION_RETURN &&
                atom_eq(loan->outcome.term, atom_int(&source, 9)));
            assert(cetta_owned_execution_accept(&producer) &&
                cetta_owned_execution_loan(&producer, &loan) == CETTA_OWNED_EXECUTION_COMPLETE);
        }
        cetta_owned_execution_cancel(&producer);
        cetta_prepared_pure_answer_cursor_close(cursor);
        cetta_prepared_pure_program_free(program);
        assert(prepared_calls == 1u && !prepared_effects);
        checks++;
    }
    {
        Atom *parameter = atom_var_with_id(&source, "$quantum-input", fresh_var_id());
        Atom *callable = petta_capture_lambda(&source, fixture_atom(&source,
            "(|-> ($x) (let $v (__cetta_lib_json_parse_v1 $x) (let 2 $v (Pair $v $v))))"), NULL);
        Atom *body = cetta_petta_open_callable(&source, callable, parameter);
        CettaPreparedPureProgram *program = test_body_program(&space, body, &parameter->var_id, 1u);
        assert(program);
        CettaPreparedPureAnswerCursorOptions options = {
            .limits = {.max_transitions = 64u, .max_scratch_bytes = 1024u * 1024u,
                .construct_allocation_bound = petta_semantics_construct_value_allocation_bound},
            .allow_continuations = true};
        Atom *arguments[] = {atom_int(&source, 11)};
        CettaPreparedPureAnswerCursor *cursor = cetta_prepared_pure_answer_cursor_open_body(program,
            arguments, 1u, &options);
        prepared_mode = 33u;
        prepared_calls = prepared_attempts = prepared_effects = 0u;
        CettaOwnedReturn pending;
        CettaEvalCompletion completion = CETTA_EVAL_COMPLETE;
        assert(cetta_prepared_pure_answer_cursor_return(cursor, &pending, &completion) ==
            CETTA_OWNED_EXECUTION_HANDOFF);
        CettaInvocation invocation;
        bool entered = false;
        assert(cetta_prepared_pure_answer_cursor_native_boundary(cursor, &invocation, &entered, &pending) && entered);
        Arena answers;
        arena_init_detached(&answers);
        PreparedNativeContinuation *worker = prepared_native_continuation_new(&space, &answers, cursor, &pending);
        assert(worker);
        unsigned pauses = 0u, count = 0u;
        CettaOwnedExecutionStep step;
        do {
            CettaOwnedReturn returned;
            step = prepared_native_continuation_next(worker, &returned, &completion);
            if (step != CETTA_OWNED_EXECUTION_COMPLETE)
                assert(worker->context.controller_quantum_transitions == 1u);
            if (step == CETTA_OWNED_EXECUTION_PAUSED)
                pauses++;
            else if (step == CETTA_OWNED_EXECUTION_RETURN) {
                assert(atom_eq(returned.outcome.term, fixture_atom(&answers, "(Pair 2 2)")));
                count++;
            }
            assert(pauses < 128u && count <= 1u);
        } while (step == CETTA_OWNED_EXECUTION_PAUSED || step == CETTA_OWNED_EXECUTION_RETURN);
        assert(step == CETTA_OWNED_EXECUTION_COMPLETE && completion == CETTA_EVAL_COMPLETE &&
            count == 1u && pauses > 1u && prepared_calls == 1u && !prepared_effects);
        prepared_native_continuation_free(worker);
        arena_free(&answers);
        cetta_prepared_pure_answer_cursor_close(cursor);
        cetta_prepared_pure_program_free(program);
        checks++;
    }
    /* Entry carries a positional translation event across a declaration
     * change, independently of retaining the packet's variable coordinates. */
    for (unsigned known = 0u; known < 2u; known++) {
        Arena execution;
        arena_init_detached(&execution);
        Atom *hole = atom_var_with_id(&execution, "$staged-return", fresh_var_id());
        Atom *head = atom_symbol(&execution, known ? "conditional-staged-known" : "conditional-staged-data");
        Atom *query = atom_expr2(&execution, head, hole);
        char declaration[160];
        snprintf(declaration, sizeof(declaration), "(= (%s $x) changed)", atom_name_cstr(head));
        if (known) {
            Atom *equation = fixture_atom(&source, declaration);
            const PettaPlanNode *plan = petta_program_plan_dynamic_add(library->petta_program, equation);
            assert(plan); space_add(&space, equation);
            assert(petta_program_note_add(library->petta_program, &space,
                space_get_at64(&space, space_length64(&space) - 1u), plan));
        }
        PettaEvalMachineContext context = {.fuel = -1, .library_context = library,
            .semantic_authority_epoch = 1u, .prepared_pure_cache = {.root_space = &space}};
        PettaMachineHost host = petta_eval_machine_host(&context, -1, false, false,
            false, true, false, petta_eval_machine_match_semantics(library, false));
        const Arena *owners[] = {&execution};
        const PettaPlanNode *plan = host.translate_source(host.context, &space, query, owners, 1u);
        assert(plan);
        PettaMachine machine;
        assert(petta_machine_init_with_return(&machine, &space, &execution, query, plan,
            hole, cetta_call_completed_value(atom_int(&execution, 5)), NULL, &host));
        if (!known) {
            Atom *equation = fixture_atom(&source, declaration);
            const PettaPlanNode *added = petta_program_plan_dynamic_add(library->petta_program, equation);
            assert(added); space_add(&space, equation);
            assert(petta_program_note_add(library->petta_program, &space,
                space_get_at64(&space, space_length64(&space) - 1u), added));
        }
        Atom *answer = NULL;
        Bindings environment;
        assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_ANSWER);
        Atom *expected = known ? atom_symbol(&execution, "changed")
            : atom_expr2(&execution, head, atom_int(&execution, 5));
        assert(atom_eq(bindings_apply_if_vars(&environment, &execution, answer), expected));
        bindings_free(&environment);
        assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_EXHAUSTED);
        bindings_free(&environment);
        petta_machine_destroy(&machine);
        prepared_pure_program_cache_free(&context.prepared_pure_cache);
        arena_free(&execution);
        checks++;
    }
    /* A native expression may introduce a callable between transfer and
     * caller execution. The saved source occurrence keeps its entry role. */
    for (unsigned known = 0u; known < 2u; known++) {
        const char *head = known ? "conditional-parent-known" : "conditional-parent-data";
        char declaration[192], entry[256], entry_name[64];
        assert(snprintf(declaration, sizeof(declaration), "(= (%s $x) changed)", head) < (int)sizeof(declaration));
        if (known) {
            Atom *equation = fixture_atom(&source, declaration);
            const PettaPlanNode *plan = petta_program_plan_dynamic_add(library->petta_program, equation);
            assert(plan); space_add(&space, equation);
            assert(petta_program_note_add(library->petta_program, &space,
                space_get_at64(&space, space_length64(&space) - 1u), plan));
        }
        snprintf(entry_name, sizeof(entry_name), "conditional-parent-entry-%u", known);
        assert(snprintf(entry, sizeof(entry),
            "(= (%s $input) (let $v (__cetta_lib_json_parse_v1 $input) (%s $v)))",
            entry_name, head) < (int)sizeof(entry));
        Atom *equation = fixture_atom(&source, entry);
        const PettaPlanNode *plan = petta_program_plan_dynamic_add(library->petta_program, equation);
        assert(plan); space_add(&space, equation);
        assert(petta_program_note_add(library->petta_program, &space,
            space_get_at64(&space, space_length64(&space) - 1u), plan));
        Arena execution;
        arena_init_detached(&execution);
        PettaEvalMachineContext context = {.fuel = -1, .library_context = library,
            .semantic_authority_epoch = 1u, .prepared_pure_cache = {.root_space = &space}};
        PettaMachineHost host = petta_eval_machine_host(&context, -1, false, false,
            false, true, false, petta_eval_machine_match_semantics(library, false));
        host.execute_prepared_pure_call = NULL;
        host.open_relation_cursor = NULL;
        host.open_answer_cursor = resumption_open_cursor;
        host.next_answer_cursor = resumption_next_cursor;
        resumption_root = symbol_intern_cstr(g_symbols, entry_name);
        resumption_allowance = 64u;
        resumption_pulls = resumption_native_transfers = 0u;
        prepared_mode = 41u;
        prepared_calls = prepared_attempts = prepared_effects = 0u;
        prepared_mutation_arena = &source;
        prepared_staged_space = &space;
        prepared_staged_definition = known ? NULL : declaration;
        PettaMachine machine;
        Atom *query = atom_expr2(&execution, atom_symbol_id(&execution, resumption_root),
            atom_int(&execution, 11));
        assert(petta_machine_init(&machine, &space, &execution, query, NULL, &host));
        Atom *answer = NULL;
        Bindings environment;
        assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_ANSWER);
        Atom *expected = known ? atom_symbol(&execution, "changed") : atom_expr2(&execution,
            atom_symbol(&execution, head), atom_int(&execution, 5));
        assert(atom_eq(bindings_apply_if_vars(&environment, &execution, answer), expected));
        bindings_free(&environment);
        assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_EXHAUSTED);
        bindings_free(&environment);
        assert(resumption_native_transfers == 1u && prepared_calls == 1u && prepared_effects == 1u);
        prepared_staged_definition = NULL;
        prepared_staged_space = NULL;
        prepared_mutation_arena = NULL;
        petta_machine_destroy(&machine);
        prepared_pure_program_cache_free(&context.prepared_pure_cache);
        arena_free(&execution);
        checks++;
    }
    /* Untried source equations prevent a whole-frontier ownership grant. */
    for (unsigned index = 0u; index < 2u; index++) {
        Atom *equation = fixture_atom(&source,
            "(= (conditional-remaining $input) (let $v (__cetta_lib_json_parse_v1 $input) (Pair $v $v)))");
        const PettaPlanNode *plan = petta_program_plan_dynamic_add(library->petta_program, equation);
        assert(plan);
        space_add(&space, equation);
        assert(petta_program_note_add(library->petta_program, &space,
            space_get_at64(&space, space_length64(&space) - 1u), plan));
    }
    prepared_mode = 33u;
    prepared_calls = prepared_attempts = prepared_effects = 0u;
    resumption_root = symbol_intern_cstr(g_symbols, "conditional-remaining");
    resumption_allowance = 1u;
    CettaPreparedPureAnswerCursor *remaining = resumption_open_cursor(NULL, &space, &source,
        fixture_atom(&source, "(conditional-remaining 11)"), NULL, NULL, NULL);
    assert(remaining);
    CettaEvalCompletion completion = CETTA_EVAL_COMPLETE;
    CettaOwnedReturn returned;
    unsigned count = 0u;
    CettaOwnedExecutionStep step;
    while ((step = prepared_pure_next_return(&space, remaining, 32u, &returned, &completion)) ==
            CETTA_OWNED_EXECUTION_RETURN) {
        assert(cetta_prepared_pure_answer_cursor_owns_native_frontier(remaining) == (count != 0u));
        assert(++count <= 2u && returned.outcome.kind == CETTA_CALL_VALUE &&
            atom_has_vars(returned.outcome.term));
    }
    assert(count == 2u && step == CETTA_OWNED_EXECUTION_COMPLETE &&
        completion == CETTA_EVAL_COMPLETE && prepared_calls == 2u && !prepared_effects);
    cetta_prepared_pure_answer_cursor_close(remaining);
    checks++;
    prepared_fixture = false;
    space_free(&space);
    arena_free(&source);
    eval_set_library_context(NULL);
    cetta_library_context_free(library);
    free(library);
    return checks;
}


static bool rejoin_decline_head(void *context, SymbolId head) {
    *(SymbolId *)context = head;
    return false;
}

/* Native caller execution can retire independently of the prepared equations
 * still held by its cursor. No published prefix or entered effect is replayed. */
static unsigned native_frontier_rejoin_controls(void) {
    CettaLibraryContext *library = calloc(1u, sizeof(*library));
    assert(library);
    cetta_library_context_init_for_language_profile(library, CETTA_LANGUAGE_PETTA, NULL);
    eval_set_library_context(library);
    Arena source;
    arena_init_detached(&source);
    Space space;
    space_init(&space);
    prepared_head = symbol_intern_cstr(g_symbols, "__cetta_lib_json_parse_v1");
    prepared_fixture = true;
    const char *equations[] = {
        "(= (rejoin-leaf $input) (__cetta_lib_json_parse_v1 $input))",
        "(= (rejoin-leaf $input) (__cetta_lib_json_parse_v1 $input))",
        "(= (rejoin-mid $input) (rejoin-leaf $input))",
        "(= (rejoin-mid $input) (rejoin-leaf $input))",
        "(= (rejoin-pair $input) (let $v (rejoin-leaf $input) (let $_ (__cetta_lib_json_parse_v1 99) (Pair $input $v $v))))",
        "(= (rejoin-nested $input) (let $v (rejoin-mid $input) (let $_ (__cetta_lib_json_parse_v1 99) (Pair $input $v $v))))",
        "(= (rejoin-fail $input) (let $v (rejoin-leaf $input) (let 1 $v (let $_ (__cetta_lib_json_parse_v1 99) (Pair $input $v $v)))))",
        "(= (rejoin-bound $input) (let $v (rejoin-leaf $input) (let 2 $v (let $_ (__cetta_lib_json_parse_v1 99) (Pair $input $v $v)))))",
        "(= (rejoin-expression $input) (let $v (rejoin-leaf $input) (Pair $input $v $v)))",
        "(= (rejoin-long-3 $input) (let $v (rejoin-leaf $input) (Pair $v $v)))",
        "(= (rejoin-long-2 $input) (let $v (rejoin-long-3 $input) (Pair $v $v)))",
        "(= (rejoin-long-1 $input) (let $v (rejoin-long-2 $input) (Pair $v $v)))",
        "(= (rejoin-long-0 $input) (let $v (rejoin-long-1 $input) (Pair $v $v)))",
        "(= (rejoin-short-1 $input) (let $v (rejoin-leaf $input) (Pair $v $v)))",
        "(= (rejoin-short-0 $input) (let $v (rejoin-short-1 $input) (Pair $v $v)))",
        "(= (rejoin-many $input) (__cetta_lib_json_parse_v1 $input))",
    };
    for (unsigned i = 0u; i < sizeof(equations) / sizeof(*equations); i++) {
        Atom *equation = fixture_atom(&source, equations[i]);
        const PettaPlanNode *plan = petta_program_plan_dynamic_add(library->petta_program, equation);
        assert(plan); space_add(&space, equation);
        assert(petta_program_note_add(library->petta_program, &space,
            space_get_at64(&space, space_length64(&space) - 1u), plan));
    }
    for (unsigned index = 0u; index < 20u; index++) {
        char definition[96];
        snprintf(definition, sizeof(definition), "(= (rejoin-many $input) %u)", index);
        Atom *equation = fixture_atom(&source, definition);
        const PettaPlanNode *plan = petta_program_plan_dynamic_add(library->petta_program, equation);
        assert(plan); space_add(&space, equation);
        assert(petta_program_note_add(library->petta_program, &space,
            space_get_at64(&space, space_length64(&space) - 1u), plan));
    }
    const struct {
        const char *head;
        unsigned mode, answers, calls;
        bool prolog, allocates;
    } cases[] = {
        {"rejoin-pair", 33u, 2u, 2u, false, true},
        {"rejoin-pair", 40u, 6u, 2u, false, true},
        {"rejoin-nested", 33u, 4u, 4u, false, true},
        {"rejoin-fail", 34u, 0u, 2u, true, false},
        {"rejoin-bound", 34u, 2u, 2u, true, true},
        {"rejoin-expression", 5u, 2u, 2u, false, false},
        {"rejoin-expression", 42u, 2u, 2u, false, false},
    };
    const uint64_t allowances[] = {0u, 1u, 2u, 64u};
    unsigned checks = 0u;
    for (unsigned item = 0u; item < sizeof(cases) / sizeof(*cases); item++) {
        if (cases[item].prolog && !cetta_lib_prolog_runtime_available(library->lib_prolog))
            continue;
        for (unsigned ordinary = 0u; ordinary < 2u; ordinary++)
        for (unsigned budget = 0u; budget < 4u; budget++)
        for (unsigned early = 0u; early < 2u; early++) {
            Arena execution;
            arena_init_detached(&execution);
            PettaEvalMachineContext context = {.fuel = -1, .library_context = library,
                .semantic_authority_epoch = 1u, .prepared_pure_cache = {.root_space = &space}};
            PettaMachineHost host = petta_eval_machine_host(&context, -1, false, false,
                false, true, false, petta_eval_machine_match_semantics(library, false));
            host.execute_prepared_pure_call = NULL;
            host.open_relation_cursor = NULL;
            host.open_answer_cursor = ordinary ? NULL : resumption_open_cursor;
            host.next_answer_cursor = resumption_next_cursor;
            resumption_root = symbol_intern_cstr(g_symbols, cases[item].head);
            resumption_allowance = allowances[budget];
            resumption_pulls = resumption_native_transfers = 0u;
            prepared_mode = cases[item].mode;
            prepared_calls = prepared_attempts = prepared_effects = 0u;
            prepared_caller_collect = cases[item].allocates;
            PettaMachine machine;
            Atom *query = atom_expr2(&execution, atom_symbol_id(&execution, resumption_root),
                atom_int(&execution, 11));
            assert(petta_machine_init(&machine, &space, &execution, query, NULL, &host));
            unsigned count = 0u;
            PettaMachineStep step;
            for (;;) {
                Atom *answer = NULL;
                Bindings environment;
                step = petta_machine_next(&machine, &answer, &environment);
                if (step == PETTA_MACHINE_STEP_ANSWER) {
                    Atom *value = bindings_apply_if_vars(&environment, &execution, answer);
                    assert(count < cases[item].answers && value && value->kind == ATOM_EXPR &&
                        value->expr.len == 4u && atom_is_symbol(value->expr.elems[0], "Pair") &&
                        atom_eq(value->expr.elems[1], atom_int(&execution, 11)) &&
                        atom_eq(value->expr.elems[2], value->expr.elems[3]));
                    bool open = prepared_mode == 33u || (prepared_mode == 40u && count % 3u != 2u);
                    assert(atom_has_vars(value) == open);
                    if (!open)
                        assert(atom_eq(value->expr.elems[2], atom_int(&execution,
                            prepared_mode == 40u ? 9 : prepared_mode == 34u ? 2 : 5)));
                    count++;
                    assert(prepared_effects == count);
                }
                bindings_free(&environment);
                if (step != PETTA_MACHINE_STEP_ANSWER || early)
                    break;
            }
            unsigned expected = early && cases[item].answers ? 1u : cases[item].answers;
            if (count != expected || (step != PETTA_MACHINE_STEP_ANSWER && step != PETTA_MACHINE_STEP_EXHAUSTED))
                fprintf(stderr, "native rejoin item=%u ordinary=%u budget=%u early=%u count=%u step=%u calls=%u transfers=%u\n",
                    item, ordinary, budget, early, count, step, prepared_calls, resumption_native_transfers);
            assert(count == expected && (early && count ? step == PETTA_MACHINE_STEP_ANSWER
                : step == PETTA_MACHINE_STEP_EXHAUSTED));
            assert(prepared_calls == (early && count ? 1u : cases[item].calls) &&
                prepared_effects == count);
            if (!ordinary && budget == 3u)
                assert(resumption_native_transfers >= 1u &&
                    resumption_native_transfers <= prepared_calls);
            if (!ordinary && budget == 3u && cases[item].allocates && resumption_native_transfers) {
                PettaMachineStats stats;
                assert(petta_machine_stats(&machine, &stats) && stats.choice_nursery_evacuations > 0u);
            }
            prepared_caller_collect = false;
            petta_machine_destroy(&machine);
            prepared_pure_program_cache_free(&context.prepared_pure_cache);
            arena_free(&execution);
            checks++;
        }
    }
    /* Native expression completion owns its cut scope. Neither the saved
     * equation alternatives nor an external caller's choice is committed. */
    for (unsigned ordinary = 0u; ordinary < 2u; ordinary++) {
        Arena execution;
        arena_init_detached(&execution);
        PettaEvalMachineContext context = {.fuel = -1, .library_context = library,
            .semantic_authority_epoch = 1u, .prepared_pure_cache = {.root_space = &space}};
        PettaMachineHost host = petta_eval_machine_host(&context, -1, false, false,
            false, true, false, petta_eval_machine_match_semantics(library, false));
        host.execute_prepared_pure_call = NULL; host.open_relation_cursor = NULL;
        host.open_answer_cursor = ordinary ? NULL : resumption_open_cursor;
        host.next_answer_cursor = resumption_next_cursor;
        resumption_root = symbol_intern_cstr(g_symbols, "rejoin-expression");
        resumption_allowance = 64u; resumption_pulls = resumption_native_transfers = 0u;
        prepared_mode = 42u; prepared_calls = prepared_attempts = prepared_effects = 0u;
        PettaMachine machine;
        Atom *query = fixture_atom(&execution,
            "(let $input (superpose (11 12)) (rejoin-expression $input))");
        assert(petta_machine_init(&machine, &space, &execution, query, NULL, &host));
        for (unsigned count = 0u; count < 5u; count++) {
            Atom *answer = NULL; Bindings environment;
            PettaMachineStep step = petta_machine_next(&machine, &answer, &environment);
            assert(step == (count < 4u ? PETTA_MACHINE_STEP_ANSWER : PETTA_MACHINE_STEP_EXHAUSTED));
            if (count < 4u) {
                char expected[64];
                snprintf(expected, sizeof(expected), "(Pair %u 5 5)", 11u + count / 2u);
                Atom *observed = bindings_apply_if_vars(&environment, &execution, answer);
                if (!atom_eq(observed, fixture_atom(&execution, expected))) {
                    fprintf(stderr, "external cut ordinary=%u count=%u observed=", ordinary, count);
                    atom_print(observed, stderr); fprintf(stderr, " expected=%s\n", expected);
                }
                assert(atom_eq(observed, fixture_atom(&execution, expected)));
            }
            bindings_free(&environment);
        }
        assert(prepared_calls == 4u && prepared_effects == 4u &&
            (ordinary ? !resumption_native_transfers :
                resumption_native_transfers >= 2u && resumption_native_transfers <= 4u));
        petta_machine_destroy(&machine); prepared_pure_program_cache_free(&context.prepared_pure_cache);
        arena_free(&execution); checks++;
    }
    /* An unentered body stays opaque. After entering its callee, the cursor
     * keeps the actual body as that callee's pending caller, without replay. */
    {
        Atom *parameter = atom_var_with_id(&source, "$opaque-root-input", fresh_var_id());
        Atom *callable = petta_capture_lambda(&source, fixture_atom(&source,
            "(|-> ($x) (let $v (rejoin-leaf $x) (Pair $v $v)))"), NULL);
        Atom *body = cetta_petta_open_callable(&source, callable, parameter);
        CettaPreparedPureProgram *program = test_body_program(&space, body, &parameter->var_id, 1u);
        assert(program);
        SymbolId declined = SYMBOL_ID_NONE;
        CettaPreparedPureAnswerCursorOptions options = {
            .limits = {.max_transitions = 0u, .max_scratch_bytes = 1024u * 1024u,
                .construct_allocation_bound = petta_semantics_construct_value_allocation_bound},
            .allow_continuations = true,
            .head_admission = rejoin_decline_head, .head_admission_context = &declined};
        Atom *arguments[] = {atom_int(&source, 11)};
        CettaPreparedPureAnswerCursor *opaque = cetta_prepared_pure_answer_cursor_open_body(
            program, arguments, 1u, &options);
        assert(opaque);
        prepared_mode = 33u; prepared_calls = prepared_attempts = prepared_effects = 0u;
        CettaOwnedReturn pending;
        CettaEvalCompletion completion = CETTA_EVAL_COMPLETE;
        CettaOwnedExecutionStep opaque_step = cetta_prepared_pure_answer_cursor_return(opaque, &pending, &completion);
        CettaPreparedPureAnswerFrame frame;
        assert(opaque_step == CETTA_OWNED_EXECUTION_HANDOFF &&
            cetta_prepared_pure_answer_cursor_handoff_reason(opaque) == CETTA_PREPARED_PURE_HANDOFF_LIMIT &&
            cetta_prepared_pure_answer_cursor_frame_count(opaque) == 1u &&
            !cetta_prepared_pure_answer_cursor_frame(opaque, 0u, &frame) && !prepared_calls);
        assert(cetta_prepared_pure_answer_cursor_refill_transitions(opaque, 64u, &completion));
        assert(cetta_prepared_pure_answer_cursor_return(opaque, &pending, &completion) == CETTA_OWNED_EXECUTION_HANDOFF &&
            cetta_prepared_pure_answer_cursor_handoff_reason(opaque) == CETTA_PREPARED_PURE_HANDOFF_UNSUPPORTED &&
            declined == symbol_intern_cstr(g_symbols, "rejoin-leaf") &&
            cetta_prepared_pure_answer_cursor_frame_count(opaque) == 1u && !prepared_calls);
        assert(cetta_prepared_pure_answer_cursor_frame(opaque, 0u, &frame) &&
            frame.head == declined && frame.next_ordinal == 0u && frame.equation_count == 2u &&
            frame.resumes_continuation);
        cetta_prepared_pure_answer_cursor_close(opaque);
        cetta_prepared_pure_program_free(program); checks++;
    }
    /* Multiple unfinished frames and long caller chains share their saved
     * ancestors. Taking the packet does not replay the native entry. */
    for (unsigned long_chain = 0u; long_chain < 3u; long_chain++) {
        prepared_mode = 33u;
        prepared_calls = prepared_attempts = prepared_effects = 0u;
        const char *head = long_chain == 0u ? "rejoin-nested"
            : long_chain == 1u ? "rejoin-long-1" : "rejoin-long-0";
        const char *call = long_chain == 0u ? "(rejoin-nested 11)"
            : long_chain == 1u ? "(rejoin-long-1 11)" : "(rejoin-long-0 11)";
        resumption_root = symbol_intern_cstr(g_symbols, head);
        resumption_allowance = 64u;
        CettaPreparedPureAnswerCursor *cursor = resumption_open_cursor(NULL, &space, &source,
            fixture_atom(&source, call), NULL, NULL, NULL);
        assert(cursor);
        cetta_prepared_pure_answer_cursor_defer_native_completion(cursor);
        CettaOwnedReturn returned;
        CettaEvalCompletion completion = CETTA_EVAL_COMPLETE;
        assert(cetta_prepared_pure_answer_cursor_return(cursor, &returned, &completion) ==
            CETTA_OWNED_EXECUTION_HANDOFF && completion == CETTA_EVAL_COMPLETE &&
            cetta_prepared_pure_answer_cursor_handoff_reason(cursor) == CETTA_PREPARED_PURE_HANDOFF_NATIVE_RETURN);
        unsigned unfinished = 0u;
        size_t frames = cetta_prepared_pure_answer_cursor_frame_count(cursor);
        for (size_t index = 0u; index + 1u < frames; index++) {
            CettaPreparedPureAnswerFrame frame;
            assert(cetta_prepared_pure_answer_cursor_frame(cursor, index, &frame));
            unfinished += frame.next_ordinal < frame.equation_count;
        }
        CettaInvocation invocation; bool entered = false;
        assert(unfinished == (long_chain ? 1u : 2u) && cetta_prepared_pure_answer_cursor_native_boundary(
            cursor, &invocation, &entered, &returned) && entered && returned.outcome.term &&
            returned.outcome.term->kind == ATOM_VAR && prepared_calls == 1u);
        Atom *pending = returned.outcome.term;
        CettaOwnedExecution moved = {0};
        assert(cetta_prepared_pure_answer_cursor_native_transfer_ready(cursor) &&
            cetta_prepared_pure_answer_cursor_take_native(cursor, &moved));
        const CettaOwnedReturn *loan = NULL;
        assert(cetta_owned_execution_loan(&moved, &loan) == CETTA_OWNED_EXECUTION_RETURN &&
            loan->outcome.term == pending && prepared_calls == 1u);
        cetta_owned_execution_cancel(&moved);
        cetta_prepared_pure_answer_cursor_close(cursor);
        assert(prepared_calls == 1u && !prepared_effects); checks++;
    }
    /* A short caller chain and many equations in one frame remain eligible.
     * Each accepted transfer moves the entered packet exactly once. */
    for (unsigned many = 0u; many < 2u; many++) {
        prepared_mode = 33u;
        prepared_calls = prepared_attempts = prepared_effects = 0u;
        resumption_root = symbol_intern_cstr(g_symbols, many ? "rejoin-many" : "rejoin-short-0");
        resumption_allowance = 64u;
        CettaPreparedPureAnswerCursor *cursor = resumption_open_cursor(NULL, &space, &source,
            fixture_atom(&source, many ? "(rejoin-many 11)" : "(rejoin-short-0 11)"), NULL, NULL, NULL);
        assert(cursor);
        cetta_prepared_pure_answer_cursor_defer_native_completion(cursor);
        CettaOwnedReturn returned; CettaEvalCompletion completion = CETTA_EVAL_COMPLETE;
        assert(cetta_prepared_pure_answer_cursor_return(cursor, &returned, &completion) ==
            CETTA_OWNED_EXECUTION_HANDOFF && completion == CETTA_EVAL_COMPLETE &&
            cetta_prepared_pure_answer_cursor_native_transfer_ready(cursor));
        CettaPreparedPureAnswerFrame frame;
        size_t frames = cetta_prepared_pure_answer_cursor_frame_count(cursor);
        assert(frames >= 2u && cetta_prepared_pure_answer_cursor_frame(cursor, frames - 2u, &frame) &&
            frame.next_ordinal == 1u && frame.equation_count == (many ? 21u : 2u));
        CettaOwnedExecution producer = {0};
        assert(cetta_prepared_pure_answer_cursor_take_native(cursor, &producer) &&
            !cetta_prepared_pure_answer_cursor_owns_native_frontier(cursor));
        const CettaOwnedReturn *loan = NULL;
        assert(cetta_owned_execution_loan(&producer, &loan) == CETTA_OWNED_EXECUTION_RETURN &&
            loan->outcome.term && loan->outcome.term->kind == ATOM_VAR && prepared_calls == 1u);
        cetta_owned_execution_cancel(&producer); cetta_prepared_pure_answer_cursor_close(cursor);
        assert(prepared_calls == 1u && !prepared_effects); checks++;
    }
    /* The entered loan is independent of each untried equation. The public
     * frame image retains that next occurrence until its parent takes it. */
    prepared_mode = 40u;
    prepared_calls = prepared_attempts = prepared_effects = 0u;
    resumption_root = symbol_intern_cstr(g_symbols, "rejoin-leaf");
    resumption_allowance = 64u;
    CettaPreparedPureAnswerCursor *cursor = resumption_open_cursor(NULL, &space, &source,
        fixture_atom(&source, "(rejoin-leaf 11)"), NULL, NULL, NULL);
    assert(cursor);
    cetta_prepared_pure_answer_cursor_defer_native_completion(cursor);
    CettaOwnedReturn pending;
    CettaEvalCompletion completion = CETTA_EVAL_COMPLETE;
    assert(prepared_pure_next_return(&space, cursor, 32u, &pending, &completion) ==
        CETTA_OWNED_EXECUTION_HANDOFF && completion == CETTA_EVAL_COMPLETE);
    CettaPreparedPureAnswerFrame frame;
    assert(cetta_prepared_pure_answer_cursor_frame(cursor, 0u, &frame) &&
        frame.next_ordinal == 1u && frame.equation_count == 2u && frame.next_equation);
    const Atom *next = frame.next_equation;
    CettaOwnedExecution producer = {0};
    assert(cetta_prepared_pure_answer_cursor_take_native(cursor, &producer) &&
        !cetta_prepared_pure_answer_cursor_owns_native_frontier(cursor));
    unsigned count = 0u;
    const CettaOwnedReturn *loan = NULL;
    while (cetta_owned_execution_loan(&producer, &loan) == CETTA_OWNED_EXECUTION_RETURN) {
        assert(++count <= 3u && cetta_owned_execution_accept(&producer));
    }
    assert(count == 3u && cetta_prepared_pure_answer_cursor_frame(cursor, 0u, &frame) &&
        frame.next_ordinal == 1u && frame.next_equation == next && prepared_calls == 1u);
    cetta_owned_execution_cancel(&producer);
    cetta_prepared_pure_answer_cursor_close(cursor);
    checks++;
    /* Retirement schedules the remaining cursor. A refused parent permit
     * cannot advance the next equation or enter its native effect. */
    {
        Arena execution;
        arena_init_detached(&execution);
        PettaEvalMachineContext context = {.fuel = -1, .library_context = library,
            .semantic_authority_epoch = 1u, .prepared_pure_cache = {.root_space = &space}};
        PettaMachineHost host = petta_eval_machine_host(&context, -1, false, false,
            false, true, false, petta_eval_machine_match_semantics(library, false));
        host.execute_prepared_pure_call = NULL; host.open_relation_cursor = NULL;
        host.open_answer_cursor = resumption_open_cursor; host.next_answer_cursor = resumption_next_cursor;
        host.permit_transition = resumption_parent_permit;
        resumption_root = symbol_intern_cstr(g_symbols, "rejoin-pair");
        resumption_allowance = 64u; resumption_pulls = resumption_native_transfers = 0u;
        resumption_parent_credit = UINT64_MAX;
        resumption_missing_source_root = false;
        prepared_mode = 33u; prepared_calls = prepared_attempts = prepared_effects = 0u;
        PettaMachine machine;
        Atom *query = atom_expr2(&execution, atom_symbol_id(&execution, resumption_root), atom_int(&execution, 11));
        assert(petta_machine_init(&machine, &space, &execution, query, NULL, &host));
        Atom *answer = NULL; Bindings environment;
        assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_ANSWER);
        bindings_free(&environment);
        unsigned pulls = resumption_pulls;
        resumption_parent_credit = 0u;
        assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_SUSPENDED &&
            resumption_pulls == pulls && prepared_calls == 1u && prepared_effects == 1u);
        bindings_free(&environment);
        resumption_parent_credit = UINT64_MAX;
        assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_ANSWER &&
            prepared_calls == 2u && prepared_effects == 2u &&
            resumption_native_transfers >= 1u && resumption_native_transfers <= 2u);
        bindings_free(&environment);
        assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_EXHAUSTED);
        bindings_free(&environment);
        assert(!resumption_missing_source_root);
        petta_machine_destroy(&machine); prepared_pure_program_cache_free(&context.prepared_pure_cache);
        arena_free(&execution); checks++;
    }
    /* All transferred callers retain their original data/call roles,
     * including a continuation not entered until after native mutation. */
    for (unsigned known = 0u; known < 2u; known++) {
        char declaration[160], definition[256], entry_name[64], head[64];
        snprintf(head, sizeof(head), "rejoin-born-%u", known);
        snprintf(declaration, sizeof(declaration), "(= (%s $x) changed)", head);
        if (known) {
            Atom *equation = fixture_atom(&source, declaration);
            const PettaPlanNode *plan = petta_program_plan_dynamic_add(library->petta_program, equation);
            assert(plan); space_add(&space, equation);
            assert(petta_program_note_add(library->petta_program, &space,
                space_get_at64(&space, space_length64(&space) - 1u), plan));
        }
        snprintf(entry_name, sizeof(entry_name), "rejoin-mutation-entry-%u", known);
        snprintf(definition, sizeof(definition),
            "(= (%s $input) (let $v (rejoin-leaf $input) (%s $v)))", entry_name, head);
        Atom *equation = fixture_atom(&source, definition);
        const PettaPlanNode *plan = petta_program_plan_dynamic_add(library->petta_program, equation);
        assert(plan); space_add(&space, equation);
        assert(petta_program_note_add(library->petta_program, &space,
            space_get_at64(&space, space_length64(&space) - 1u), plan));
        Arena execution;
        arena_init_detached(&execution);
        PettaEvalMachineContext context = {.fuel = -1, .library_context = library,
            .semantic_authority_epoch = 1u, .prepared_pure_cache = {.root_space = &space}};
        PettaMachineHost host = petta_eval_machine_host(&context, -1, false, false,
            false, true, false, petta_eval_machine_match_semantics(library, false));
        host.execute_prepared_pure_call = NULL; host.open_relation_cursor = NULL;
        host.open_answer_cursor = resumption_open_cursor; host.next_answer_cursor = resumption_next_cursor;
        resumption_root = symbol_intern_cstr(g_symbols, entry_name);
        resumption_allowance = 64u; resumption_pulls = resumption_native_transfers = 0u;
        prepared_mode = 41u; prepared_calls = prepared_attempts = prepared_effects = 0u;
        prepared_mutation_arena = &source; prepared_staged_space = &space;
        prepared_staged_definition = known ? NULL : declaration;
        PettaMachine machine;
        Atom *query = atom_expr2(&execution, atom_symbol_id(&execution, resumption_root), atom_int(&execution, 11));
        assert(petta_machine_init(&machine, &space, &execution, query, NULL, &host));
        Atom *expected = known ? atom_symbol(&execution, "changed") : atom_expr2(&execution,
            atom_symbol(&execution, head), atom_int(&execution, 5));
        for (unsigned count = 0u; count < 3u; count++) {
            Atom *answer = NULL; Bindings environment;
            PettaMachineStep step = petta_machine_next(&machine, &answer, &environment);
            assert(step == (count < 2u ? PETTA_MACHINE_STEP_ANSWER : PETTA_MACHINE_STEP_EXHAUSTED));
            if (count < 2u)
                assert(atom_eq(bindings_apply_if_vars(&environment, &execution, answer), expected));
            bindings_free(&environment);
        }
        assert(prepared_calls == 2u && prepared_effects == 2u &&
            resumption_native_transfers >= 1u && resumption_native_transfers <= 2u);
        prepared_staged_definition = NULL; prepared_staged_space = NULL; prepared_mutation_arena = NULL;
        petta_machine_destroy(&machine); prepared_pure_program_cache_free(&context.prepared_pure_cache);
        arena_free(&execution); checks++;
    }
    /* Native mutation retires compiled authority. Transferred alternatives
     * have the same observations as the independent ordinary equation path. */
    Atom *observations[8] = {0};
    unsigned expected_count = 0u, expected_effects = 0u, expected_calls = 0u;
    for (unsigned compiled = 0u; compiled < 2u; compiled++) {
        assert(cetta_library_petta_translator_rule_set(library, prepared_head, false));
        Arena execution;
        arena_init_detached(&execution);
        PettaEvalMachineContext context = {.fuel = -1, .library_context = library,
            .semantic_authority_epoch = 1u, .prepared_pure_cache = {.root_space = &space}};
        PettaMachineHost host = petta_eval_machine_host(&context, -1, false, false,
            false, true, false, petta_eval_machine_match_semantics(library, false));
        host.execute_prepared_pure_call = NULL; host.open_relation_cursor = NULL;
        host.open_answer_cursor = compiled ? resumption_open_cursor : NULL;
        host.next_answer_cursor = resumption_next_cursor;
        resumption_root = symbol_intern_cstr(g_symbols, "rejoin-expression");
        resumption_allowance = 64u; resumption_pulls = resumption_native_transfers = 0u;
        prepared_mode = 13u; prepared_calls = prepared_attempts = prepared_effects = 0u;
        PettaMachine machine;
        Atom *query = atom_expr2(&execution, atom_symbol_id(&execution, resumption_root), atom_int(&execution, 11));
        assert(petta_machine_init(&machine, &space, &execution, query, NULL, &host));
        unsigned count = 0u;
        for (;;) {
            Atom *answer = NULL; Bindings environment;
            PettaMachineStep step = petta_machine_next(&machine, &answer, &environment);
            if (step == PETTA_MACHINE_STEP_ANSWER) {
                Atom *value = bindings_apply_if_vars(&environment, &execution, answer);
                assert(value && count < 8u);
                if (compiled) {
                    if (!atom_eq(value, observations[count])) {
                        fputs("native mutation actual=", stderr); atom_print(value, stderr);
                        fputs(" expected=", stderr); atom_print(observations[count], stderr); fputc('\n', stderr);
                    }
                    assert(count < expected_count && atom_eq(value, observations[count]));
                } else observations[count] = atom_deep_copy(&source, value);
                count++;
            }
            bindings_free(&environment);
            if (step == PETTA_MACHINE_STEP_ANSWER) continue;
            assert(step == PETTA_MACHINE_STEP_EXHAUSTED); break;
        }
        if (compiled) assert(count == expected_count && prepared_calls == expected_calls &&
            prepared_effects == expected_effects && resumption_native_transfers == 1u);
        else { expected_count = count; expected_calls = prepared_calls; expected_effects = prepared_effects; }
        petta_machine_destroy(&machine); prepared_pure_program_cache_free(&context.prepared_pure_cache);
        arena_free(&execution);
        assert(cetta_library_petta_translator_rule_set(library, prepared_head, false));
        checks++;
    }
    prepared_fixture = false;
    space_free(&space); arena_free(&source);
    eval_set_library_context(NULL); cetta_library_context_free(library); free(library);
    return checks;
}

/* Returning from one nondeterministic call can leave its old caller pending
 * while a second call installs a sibling caller over the same outer one. */
typedef struct {
    AtomDeepCopySession *copy;
    unsigned input_reads;
    unsigned reads;
    unsigned refuse_at;
} CallerReadObservation;

static Atom *observe_caller_read(void *context, Atom *value) {
    CallerReadObservation *observation = context;
    if (++observation->reads == observation->refuse_at)
        return NULL;
    if (value && value->kind == ATOM_GROUNDED && value->ground.gkind == GV_INT &&
        value->ground.ival == 11)
        observation->input_reads++;
    return atom_deep_copy_session_copy(observation->copy, value);
}

/* Entry changes which names are callable. A saved constructor stays data;
 * an already known call still admits its callee at the time it is entered. */
static const void *decline_constructor_projection(void *context,
        const CettaPreparedPureSourceProjection *projection) {
    return projection->computation.control.operation == CETTA_CONTROL_CONSTRUCT
        ? NULL : petta_prepared_pure_project_source(context, projection);
}

static unsigned frozen_source_caller_controls(void) {
    CettaLibraryContext *library = calloc(1u, sizeof(*library));
    assert(library);
    cetta_library_context_init_for_language_profile(library, CETTA_LANGUAGE_PETTA, NULL);
    eval_set_library_context(library);
    const PettaPlanNode origin = {.role = PETTA_PLAN_STATIC_CALL,
        .computation.control = CETTA_CONTROL_INVOKE_PLAN,
        .source_position = 7u, .source_debit = 3u,
        .contains_cardinality_call = true, .contains_deferred_occurrence_transport = true};
    const PettaPlanNode *value_plan = petta_program_project_computation(library->petta_program,
        &origin, (CettaComputationPlan){.control = CETTA_CONTROL_RETURN_VALUE_PLAN},
        false, false, NULL, 0u);
    const PettaPlanNode *call_plan = petta_program_project_computation(library->petta_program,
        &origin, origin.computation, false, false, NULL, 0u);
    assert(value_plan && call_plan && value_plan->role == PETTA_PLAN_VALUE &&
        !value_plan->contains_cardinality_call && !value_plan->contains_deferred_occurrence_transport &&
        call_plan->contains_cardinality_call && call_plan->contains_deferred_occurrence_transport &&
        call_plan->source_position == 7u && call_plan->source_debit == 3u);
    Arena source;
    arena_init_detached(&source);
    Space space;
    space_init(&space);
    prepared_head = symbol_intern_cstr(g_symbols, "__cetta_lib_json_parse_v1");
    prepared_fixture = true;
    prepared_mutation_arena = &source;
    const uint64_t allowances[] = {0u, 1u, 2u, 64u};
    unsigned checks = 0u;
    for (unsigned known = 0u; known < 2u; known++)
    for (unsigned data = 0u; data < 2u; data++)
    for (unsigned ordinary = 0u; ordinary < 2u; ordinary++)
    for (size_t budget = 0u; budget < sizeof(allowances) / sizeof(*allowances); budget++) {
        char name[96], root[96], rows[2][384], declaration[192], query[128], expected[192];
        snprintf(name, sizeof(name), "caller-born-%u-%u-%u-%zu", known, data, ordinary, budget);
        snprintf(root, sizeof(root), "caller-owner-%u-%u-%u-%zu", known, data, ordinary, budget);
        snprintf(rows[0], sizeof(rows[0]), "(= (%s $x) (let $v (__cetta_lib_json_parse_v1 $x) (%s $v (Pair $v $v))))", root, name);
        snprintf(rows[1], sizeof(rows[1]), "(= (%s $a $b) 42)", name);
        /* Callability is established when the caller is installed. */
        for (unsigned remaining = 1u + known; remaining > 0u; remaining--) {
            unsigned row = remaining - 1u;
            Atom *equation = fixture_atom(&source, rows[row]);
            const PettaPlanNode *plan = petta_program_plan_dynamic_add(library->petta_program, equation);
            assert(plan);
            space_add(&space, equation);
            assert(petta_program_note_add(library->petta_program, &space,
                space_get_at64(&space, space_length64(&space) - 1u), plan));
        }
        snprintf(declaration, sizeof(declaration), "(= (%s $a $b) 99)", name);
        snprintf(query, sizeof(query), "(%s 11)", root);
        const char *value = data ? "(readln!)" : "17";
        snprintf(expected, sizeof(expected), "(%s %s (Pair %s %s))", name, value, value, value);
        if (!known && !data && !ordinary && !budget) {
            /* A source adapter's refused caller projection must decline
             * compilation before the native effect can be entered. */
            Atom *call = fixture_atom(&source, query);
            CettaPreparedPureSourceView view = prepared_pure_source_view_for_language(
                CETTA_LANGUAGE_PETTA, call);
            view.project_source = decline_constructor_projection;
            prepared_calls = prepared_attempts = 0u;
            CettaPreparedPureProgram *refused = cetta_prepared_pure_program_compile_closed_answers(
                &space, call, CETTA_GSLT_PURE_CALL_EAGER, petta_semantics_boolean_value,
                petta_semantics_construct_value, petta_semantics_construct_value_is_expression,
                petta_semantics_is_opaque_runtime_value, petta_prepared_pure_register_view,
                petta_prepared_pure_expression_view, petta_prepared_pure_pattern_view, &view,
                true, active_composition_uses_total_structural_eq(),
                prepared_pure_match_decision_semantics(CETTA_GSLT_PURE_CALL_EAGER), NULL);
            assert(!refused && !prepared_calls && !prepared_attempts);
            checks++;
        }
        prepared_staged_definition = declaration;
        prepared_mode = data ? 52u : 51u;
        prepared_calls = prepared_attempts = prepared_effects = 0u;
        resumption_root = symbol_intern_cstr(g_symbols, root);
        resumption_allowance = allowances[budget];
        resumption_opened = resumption_pulls = resumption_native_transfers = 0u;
        uint64_t before = space_length64(&space);
        Arena output;
        arena_init_detached(&output);
        PettaEvalMachineContext context = {.fuel = -1, .library_context = library,
            .semantic_authority_epoch = 1u, .prepared_pure_cache = {.root_space = &space}};
        PettaMachineHost host = petta_eval_machine_host(&context, -1, false, false,
            false, true, false, petta_eval_machine_match_semantics(library, false));
        host.execute_prepared_pure_call = NULL;
        host.open_relation_cursor = NULL;
        host.open_answer_cursor = ordinary ? NULL : resumption_open_cursor;
        host.next_answer_cursor = resumption_next_cursor;
        PettaMachine machine;
        assert(petta_machine_init(&machine, &space, &output, fixture_atom(&output, query), NULL, &host));
        unsigned answers = 0u, old = 0u, added = 0u;
        for (;;) {
            Atom *answer = NULL;
            Bindings environment;
            PettaMachineStep step = petta_machine_next(&machine, &answer, &environment);
            if (step != PETTA_MACHINE_STEP_ANSWER) {
                bindings_free(&environment);
                assert(step == PETTA_MACHINE_STEP_EXHAUSTED);
                break;
            }
            Atom *actual = bindings_apply_if_vars(&environment, &output, answer);
            assert(actual);
            if (known) {
                old += atom_eq(actual, atom_int(&output, 42));
                added += atom_eq(actual, atom_int(&output, 99));
            } else {
                assert(atom_eq(actual, fixture_atom(&output, expected)));
            }
            answers++;
            bindings_free(&environment);
        }
        if (answers != (known ? 2u : 1u) || old != known || added != known ||
            prepared_calls != 1u || prepared_effects || space_length64(&space) != before + 1u)
            fprintf(stderr, "frozen caller known=%u data=%u ordinary=%u budget=%zu answers=%u old=%u added=%u calls=%u effects=%u\n",
                known, data, ordinary, budget, answers, old, added, prepared_calls, prepared_effects);
        assert(answers == (known ? 2u : 1u) && old == known && added == known &&
            prepared_calls == 1u && !prepared_effects && space_length64(&space) == before + 1u);
        if (!ordinary && budget == 3u)
            assert(resumption_opened && resumption_native_transfers == 1u);
        petta_machine_destroy(&machine);
        prepared_pure_program_cache_free(&context.prepared_pure_cache);
        arena_free(&output);
        checks++;
    }
    prepared_staged_definition = NULL;
    prepared_mutation_arena = NULL;
    prepared_fixture = false;
    space_free(&space);
    arena_free(&source);
    eval_set_library_context(NULL);
    cetta_library_context_free(library);
    free(library);
    return checks;
}

static void check_retained_caller_plan(Atom *term, const PettaPlanNode *plan) {
    assert(term && plan);
    if (plan->role == PETTA_PLAN_VALUE) {
        assert(plan->computation.control.operation == CETTA_CONTROL_RETURN_VALUE && !plan->child_count);
        return;
    }
    assert(term->kind == ATOM_EXPR && term->expr.len == plan->child_count);
    for (CettaExprIndex index = 0u; index < plan->child_count; index++)
        check_retained_caller_plan(term->expr.elems[index], petta_plan_child(plan, index));
}

static unsigned branching_native_frontier_controls(void) {
    CettaLibraryContext *library = calloc(1u, sizeof(*library));
    assert(library);
    cetta_library_context_init_for_language_profile(library, CETTA_LANGUAGE_PETTA, NULL);
    eval_set_library_context(library);
    Arena source;
    arena_init_detached(&source);
    Space space;
    space_init(&space);
    const char *equations[] = {
        "(= (branch-leaf $x) 7)",
        "(= (branch-leaf $x) 8)",
        "(= (branch-side $x) (__cetta_lib_json_parse_v1 $x))",
        "(= (branch-side $x) 9)",
        "(= (branch-root $x) (let $v (branch-leaf $x) (let $w (branch-side $v) (Pair $x $w $w))))",
        "(= (branch-root $x) 10)",
        "(= (branch-out $x) (let $v (branch-root $x) (Outer $v $v)))",
        "(= (branch-out $x) 11)",
        "(= (branch-stale-only $x) (let $v (__cetta_lib_json_parse_v1 $x) (Pair $x $v)))",
        "(= (branch-pressure-root $x) (let $v (branch-leaf $x) (let $w (branch-side $v) (let $_first (__cetta_lib_json_parse_v1 99) (let $_second (__cetta_lib_json_parse_v1 99) (Pair $x $w $w))))))",
        "(= (branch-pressure-root $x) 10)",
        "(= (branch-pressure-out $x) (let $v (branch-pressure-root $x) (Outer $v $v)))",
        "(= (branch-pressure-out $x) 11)",
    };
    for (size_t index = 0u; index < sizeof(equations) / sizeof(*equations); index++) {
        Atom *equation = fixture_atom(&source, equations[index]);
        const PettaPlanNode *plan = petta_program_plan_dynamic_add(library->petta_program, equation);
        assert(plan);
        space_add(&space, equation);
        assert(petta_program_note_add(library->petta_program, &space,
            space_get_at64(&space, space_length64(&space) - 1u), plan));
    }
    prepared_head = symbol_intern_cstr(g_symbols, "__cetta_lib_json_parse_v1");
    prepared_fixture = true;
    resumption_root = symbol_intern_cstr(g_symbols, "branch-out");
    resumption_allowance = 256u;
    prepared_mode = 33u;
    prepared_calls = prepared_attempts = prepared_effects = 0u;
    CettaPreparedPureAnswerCursor *cursor = resumption_open_cursor(NULL, &space, &source,
        fixture_atom(&source, "(branch-out 11)"), NULL, NULL, NULL);
    assert(cursor);
    cetta_prepared_pure_answer_cursor_defer_native_completion(cursor);
    CettaOwnedReturn packet;
    CettaEvalCompletion completion = CETTA_EVAL_COMPLETE;
    assert(cetta_prepared_pure_answer_cursor_return(cursor, &packet, &completion) ==
        CETTA_OWNED_EXECUTION_HANDOFF && completion == CETTA_EVAL_COMPLETE &&
        cetta_prepared_pure_answer_cursor_handoff_reason(cursor) ==
            CETTA_PREPARED_PURE_HANDOFF_NATIVE_RETURN && prepared_calls == 1u);
    size_t count = cetta_prepared_pure_answer_cursor_frame_count(cursor);
    size_t leaf = SIZE_MAX, side = SIZE_MAX;
    for (size_t index = 0u; index + 1u < count; index++) {
        CettaPreparedPureAnswerFrame frame;
        assert(cetta_prepared_pure_answer_cursor_frame(cursor, index, &frame));
        if (frame.head == symbol_intern_cstr(g_symbols, "branch-leaf")) {
            leaf = index;
            assert(frame.resumes_continuation && frame.next_ordinal == 1u && frame.equation_count == 2u);
        }
        if (frame.head == symbol_intern_cstr(g_symbols, "branch-side")) {
            side = index;
            assert(frame.resumes_continuation && frame.next_ordinal == 1u && frame.equation_count == 2u);
        }
    }
    assert(leaf != SIZE_MAX && side != SIZE_MAX && leaf < side);
    Arena reading;
    arena_init_detached(&reading);
    AtomDeepCopySession *copy = atom_deep_copy_session_new(&reading);
    assert(copy);
    Atom *hole = atom_var_with_id(&reading, "$branch-result", fresh_var_id());
    Atom *leaf_caller = NULL, *side_caller = NULL;
    Bindings leaf_captures, side_captures;
    bindings_init(&leaf_captures);
    bindings_init(&side_captures);
    assert(cetta_prepared_pure_answer_cursor_frame_resumption(cursor, leaf, &reading, hole,
        eval_owned_residual_import, copy, &leaf_captures, &leaf_caller));
    assert(cetta_prepared_pure_answer_cursor_frame_resumption(cursor, side, &reading, hole,
        eval_owned_residual_import, copy, &side_captures, &side_caller));
    assert(leaf_caller && side_caller && !atom_alpha_eq(leaf_caller, side_caller));
    const size_t selected[] = {leaf, side, count - 1u, side};
    CettaPreparedPureCallerGraph callers = {0};
    CallerReadObservation observed = {.copy = copy};
    assert(cetta_prepared_pure_answer_cursor_read_callers(cursor, selected,
        sizeof(selected) / sizeof(*selected), &reading, observe_caller_read, &observed, &callers));
    assert(observed.input_reads > 0u);
    assert(callers.root_count == 4u && callers.length &&
        callers.roots[0] && callers.roots[1] && callers.roots[2] &&
        callers.roots[0] != callers.roots[1] && callers.roots[1] == callers.roots[3]);
    size_t total = 0u, shared = 0u;
    for (size_t root = 0u; root < 3u; root++)
        for (size_t node = callers.roots[root]; node; node = callers.nodes[node - 1u].parent) {
            assert(node <= callers.length && callers.nodes[node - 1u].parent < node &&
                callers.nodes[node - 1u].input->kind == ATOM_VAR && callers.nodes[node - 1u].term &&
                callers.nodes[node - 1u].plan);
            total++;
        }
    for (size_t a = callers.roots[0]; a; a = callers.nodes[a - 1u].parent)
        for (size_t b = callers.roots[1]; b; b = callers.nodes[b - 1u].parent)
            shared += a == b;
    assert(shared && callers.length < total);
    CettaPreparedPureCallerGraph invalid = {0};
    const size_t bad[] = {leaf, count};
    assert(!cetta_prepared_pure_answer_cursor_read_callers(cursor, bad, 2u,
        &reading, eval_owned_residual_import, copy, &invalid) &&
        !invalid.nodes && !invalid.roots && !invalid.length && !invalid.root_count);
    CettaInvocation invocation;
    bool entered = false;
    assert(cetta_prepared_pure_answer_cursor_native_boundary(cursor, &invocation, &entered, &packet) &&
        entered && packet.outcome.term && atom_has_vars(packet.outcome.term));
    Atom *retained = packet.outcome.term;
    for (unsigned at = 1u; at <= observed.reads && at <= 3u; at++) {
        CettaPreparedPureCallerGraph refused_graph = {0};
        CallerReadObservation refusing = {.copy = copy, .refuse_at = at};
        assert(!cetta_prepared_pure_answer_cursor_read_callers(cursor, selected,
            sizeof(selected) / sizeof(*selected), &reading, observe_caller_read, &refusing,
            &refused_graph) && !refused_graph.nodes && !refused_graph.roots &&
            !refused_graph.length && !refused_graph.root_count);
        assert(cetta_prepared_pure_answer_cursor_native_boundary(cursor, &invocation, &entered, &packet) &&
            entered && packet.outcome.term == retained && prepared_calls == 1u);
    }
    assert(cetta_prepared_pure_answer_cursor_native_transfer_ready(cursor));
    CettaOwnedExecution refused = {.computation = &callers};
    assert(!cetta_prepared_pure_answer_cursor_take_native(cursor, &refused) &&
        refused.computation == &callers && prepared_calls == 1u);
    CettaOwnedExecution moved = {0};
    assert(cetta_prepared_pure_answer_cursor_take_native(cursor, &moved) &&
        !cetta_prepared_pure_answer_cursor_owns_native_frontier(cursor));
    const CettaOwnedReturn *loan = NULL;
    assert(cetta_owned_execution_loan(&moved, &loan) == CETTA_OWNED_EXECUTION_RETURN &&
        loan->outcome.term == retained && prepared_calls == 1u);
    assert(!cetta_prepared_pure_answer_cursor_resume_native_frontier(cursor, &moved) &&
        moved.has_pending && cetta_prepared_pure_answer_cursor_frame_count(cursor) == count);
    CettaOwnedExecution unrelated = {.computation = &callers, .terminal = true,
        .stop = CETTA_OWNED_EXECUTION_COMPLETE};
    assert(!cetta_prepared_pure_answer_cursor_resume_native_frontier(cursor, &unrelated) &&
        unrelated.computation == &callers && moved.has_pending);
    cetta_owned_execution_cancel(&moved);
    cetta_prepared_pure_answer_cursor_close(cursor);
    /* The readout owns its values independently of the compiled cursor. */
    for (size_t index = 0u; index < callers.length; index++) {
        assert(atom_to_string(&reading, callers.nodes[index].term));
        check_retained_caller_plan(callers.nodes[index].term, callers.nodes[index].plan);
    }
    cetta_prepared_pure_caller_graph_free(&callers);
    atom_deep_copy_session_free(copy);
    bindings_free(&leaf_captures);
    bindings_free(&side_captures);
    arena_free(&reading);
    unsigned checks = 1u;

    const uint64_t allowances[] = {0u, 1u, 2u, 64u};
    for (unsigned scenario = 0u; scenario < 3u; scenario++)
    for (unsigned ordinary = 0u; ordinary < 2u; ordinary++)
    for (size_t budget = 0u; budget < sizeof(allowances) / sizeof(*allowances); budget++) {
        bool pressure = scenario != 0u;
        bool mutates = scenario == 1u;
        Arena output;
        arena_init_detached(&output);
        PettaEvalMachineContext context = {.fuel = -1, .library_context = library,
            .semantic_authority_epoch = 1u, .prepared_pure_cache = {.root_space = &space}};
        PettaMachineHost host = petta_eval_machine_host(&context, -1, false, false,
            false, true, false, petta_eval_machine_match_semantics(library, false));
        host.execute_prepared_pure_call = NULL;
        host.open_relation_cursor = NULL;
        host.open_answer_cursor = ordinary ? NULL : resumption_open_cursor;
        host.next_answer_cursor = resumption_next_cursor;
        resumption_allowance = allowances[budget];
        resumption_root = symbol_intern_cstr(g_symbols,
            pressure ? "branch-pressure-out" : "branch-out");
        resumption_pulls = resumption_native_transfers = 0u;
        prepared_calls = prepared_attempts = prepared_effects = 0u;
        prepared_mode = pressure ? 50u : 33u;
        char added[128];
        snprintf(added, sizeof(added), "(= (branch-added-%u-%zu $x) added)", ordinary, budget);
        prepared_staged_definition = mutates ? added : NULL;
        prepared_staged_space = mutates ? &space : NULL;
        prepared_mutation_arena = mutates ? &source : NULL;
        prepared_caller_collect = pressure;
        uint64_t before_run = space_length64(&space);
        PettaMachine machine;
        /* Keep a caller-produced value live across entry and collection.
         * Its old nursery mark is nonempty, so reusing that mark after a
         * move is distinguishable from safely retiring the caller. */
        const char *expression = scenario == 2u
            ? "(let $live-input (+ 10 1) (branch-pressure-out $live-input))"
            : pressure ? "(branch-pressure-out 11)" : "(branch-out 11)";
        assert(petta_machine_init(&machine, &space, &output,
            fixture_atom(&output, expression), NULL, &host));
        unsigned opened = 0u, closed = 0u, outer_alt = 0u, root_alt = 0u;
        for (;;) {
            Atom *answer = NULL;
            Bindings environment;
            PettaMachineStep step = petta_machine_next(&machine, &answer, &environment);
            if (step != PETTA_MACHINE_STEP_ANSWER) {
                bindings_free(&environment);
                if (step != PETTA_MACHINE_STEP_EXHAUSTED)
                    fprintf(stderr, "branching frontier ordinary=%u budget=%zu step=%u calls=%u\n",
                        ordinary, budget, (unsigned)step, prepared_calls);
                assert(step == PETTA_MACHINE_STEP_EXHAUSTED);
                break;
            }
            Atom *value = bindings_apply_if_vars(&environment, &output, answer);
            assert(value);
            if (atom_eq(value, atom_int(&output, 11))) {
                outer_alt++;
            } else {
                assert(value->kind == ATOM_EXPR && value->expr.len == 3u &&
                    atom_is_symbol(value->expr.elems[0], "Outer") &&
                    atom_eq(value->expr.elems[1], value->expr.elems[2]));
                Atom *inner = value->expr.elems[1];
                if (atom_eq(inner, atom_int(&output, 10))) {
                    root_alt++;
                } else {
                    assert(inner->kind == ATOM_EXPR && inner->expr.len == 4u &&
                        atom_is_symbol(inner->expr.elems[0], "Pair") &&
                        atom_eq(inner->expr.elems[1], atom_int(&output, 11)) &&
                        atom_eq(inner->expr.elems[2], inner->expr.elems[3]));
                    if (pressure && atom_eq(inner->expr.elems[2], atom_int(&output, 5)))
                        opened++;
                    else if (!pressure && atom_has_vars(inner->expr.elems[2]))
                        opened++;
                    else {
                        assert(atom_eq(inner->expr.elems[2], atom_int(&output, 9)));
                        closed++;
                    }
                }
            }
            bindings_free(&environment);
        }
        assert(opened == 2u && closed == 2u && outer_alt == 1u && root_alt == 1u &&
            prepared_calls == 2u && prepared_effects == (pressure ? 12u : 0u) &&
            space_length64(&space) == before_run + (mutates ? 4u : 0u));
        if (!ordinary && budget == 3u) {
            assert(resumption_native_transfers >= 1u);
            if (mutates)
                assert(resumption_native_transfers == 1u);
            if (pressure) {
                PettaMachineStats stats;
                assert(petta_machine_stats(&machine, &stats) && stats.choice_nursery_evacuations >= 2u);
            }
        }
        prepared_staged_definition = NULL;
        prepared_staged_space = NULL;
        prepared_mutation_arena = NULL;
        prepared_caller_collect = false;
        petta_machine_destroy(&machine);
        prepared_pure_program_cache_free(&context.prepared_pure_cache);
        arena_free(&output);
        checks++;
    }
    /* Invalidation during native entry retains both sibling callers and
     * the actual entered packet. Lowering has already saved their plans. */
    resumption_allowance = 256u;
    resumption_root = symbol_intern_cstr(g_symbols, "branch-out");
    prepared_mode = 31u;
    prepared_mutation_arena = &source;
    prepared_calls = prepared_attempts = prepared_effects = 0u;
    uint64_t before_stale = space_length64(&space);
    cursor = resumption_open_cursor(NULL, &space, &source,
        fixture_atom(&source, "(branch-out 11)"), NULL, NULL, NULL);
    assert(cursor);
    cetta_prepared_pure_answer_cursor_defer_native_completion(cursor);
    completion = CETTA_EVAL_COMPLETE;
    assert(cetta_prepared_pure_answer_cursor_return(cursor, &packet, &completion) ==
        CETTA_OWNED_EXECUTION_HANDOFF && completion == CETTA_EVAL_INCOMPLETE_INVALIDATED &&
        cetta_prepared_pure_answer_cursor_handoff_reason(cursor) == CETTA_PREPARED_PURE_HANDOFF_STALE);
    assert(cetta_prepared_pure_answer_cursor_native_boundary(cursor, &invocation, &entered, &packet) &&
        entered && packet.outcome.term && atom_eq(packet.outcome.term, atom_int(&source, 17)) &&
        prepared_calls == 1u && space_length64(&space) == before_stale + 1u);
    retained = packet.outcome.term;
    assert(cetta_prepared_pure_answer_cursor_native_transfer_ready(cursor));
    refused = (CettaOwnedExecution){0};
    const CettaOwnedReturn *stale_loan = NULL;
    assert(cetta_prepared_pure_answer_cursor_take_native(cursor, &refused) && refused.computation &&
        cetta_owned_execution_loan(&refused, &stale_loan) == CETTA_OWNED_EXECUTION_RETURN &&
        stale_loan->outcome.term == retained && prepared_calls == 1u);
    cetta_owned_execution_cancel(&refused);
    cetta_prepared_pure_answer_cursor_close(cursor);
    checks++;
    /* An exhausted frontier still needs its saved caller's plan. */
    resumption_root = symbol_intern_cstr(g_symbols, "branch-stale-only");
    prepared_calls = prepared_attempts = prepared_effects = 0u;
    cursor = resumption_open_cursor(NULL, &space, &source,
        fixture_atom(&source, "(branch-stale-only 11)"), NULL, NULL, NULL);
    assert(cursor);
    cetta_prepared_pure_answer_cursor_defer_native_completion(cursor);
    completion = CETTA_EVAL_COMPLETE;
    assert(cetta_prepared_pure_answer_cursor_return(cursor, &packet, &completion) ==
        CETTA_OWNED_EXECUTION_HANDOFF && completion == CETTA_EVAL_INCOMPLETE_INVALIDATED &&
        cetta_prepared_pure_answer_cursor_native_frontier_complete(cursor) && prepared_calls == 1u);
    assert(cetta_prepared_pure_answer_cursor_native_transfer_ready(cursor));
    refused = (CettaOwnedExecution){0};
    assert(cetta_prepared_pure_answer_cursor_take_native(cursor, &refused) && refused.computation &&
        cetta_owned_execution_loan(&refused, &stale_loan) == CETTA_OWNED_EXECUTION_RETURN &&
        atom_eq(stale_loan->outcome.term, atom_int(&source, 17)) &&
        prepared_calls == 1u);
    cetta_owned_execution_cancel(&refused);
    cetta_prepared_pure_answer_cursor_close(cursor);
    checks++;
    resumption_root = symbol_intern_cstr(g_symbols, "branch-out");
    /* Ordinary and prepared execution retain six answers, the two actual
     * native calls and their two mutations, at every physical allowance. */
    for (unsigned ordinary = 0u; ordinary < 2u; ordinary++)
    for (unsigned quantum = 0u; quantum < 4u; quantum++) {
        resumption_allowance = allowances[quantum];
        Arena output;
        arena_init_detached(&output);
        PettaEvalMachineContext context = {.fuel = -1, .library_context = library,
            .semantic_authority_epoch = 1u, .prepared_pure_cache = {.root_space = &space}};
        PettaMachineHost host = petta_eval_machine_host(&context, -1, false, false,
            false, true, false, petta_eval_machine_match_semantics(library, false));
        host.execute_prepared_pure_call = NULL;
        host.open_relation_cursor = NULL;
        host.open_answer_cursor = ordinary ? NULL : resumption_open_cursor;
        host.next_answer_cursor = resumption_next_cursor;
        resumption_pulls = resumption_native_transfers = 0u;
        prepared_calls = prepared_attempts = prepared_effects = 0u;
        uint64_t before_ordinary = space_length64(&space);
        PettaMachine machine;
        assert(petta_machine_init(&machine, &space, &output,
            fixture_atom(&output, "(branch-out 11)"), NULL, &host));
        unsigned native_values = 0u, alternative_values = 0u, root_alt = 0u, outer_alt = 0u;
        for (;;) {
            Atom *answer = NULL;
            Bindings environment;
            PettaMachineStep step = petta_machine_next(&machine, &answer, &environment);
            if (step != PETTA_MACHINE_STEP_ANSWER) {
                bindings_free(&environment);
                assert(step == PETTA_MACHINE_STEP_EXHAUSTED);
                break;
            }
            Atom *value = bindings_apply_if_vars(&environment, &output, answer);
            assert(value);
            if (atom_eq(value, atom_int(&output, 11))) {
                outer_alt++;
            } else {
                assert(value->kind == ATOM_EXPR && value->expr.len == 3u &&
                    atom_is_symbol(value->expr.elems[0], "Outer") &&
                    atom_eq(value->expr.elems[1], value->expr.elems[2]));
                Atom *inner = value->expr.elems[1];
                if (atom_eq(inner, atom_int(&output, 10))) {
                    root_alt++;
                } else {
                    assert(inner->kind == ATOM_EXPR && inner->expr.len == 4u &&
                        atom_is_symbol(inner->expr.elems[0], "Pair") &&
                        atom_eq(inner->expr.elems[1], atom_int(&output, 11)) &&
                        atom_eq(inner->expr.elems[2], inner->expr.elems[3]));
                    if (atom_eq(inner->expr.elems[2], atom_int(&output, 17)))
                        native_values++;
                    else {
                        assert(atom_eq(inner->expr.elems[2], atom_int(&output, 9)));
                        alternative_values++;
                    }
                }
            }
            bindings_free(&environment);
        }
        assert(native_values == 2u && alternative_values == 2u && root_alt == 1u &&
            outer_alt == 1u && prepared_calls == 2u && !prepared_effects &&
            space_length64(&space) == before_ordinary + 2u);
        if (!ordinary && quantum == 3u)
            assert(resumption_native_transfers == 1u);
        petta_machine_destroy(&machine);
        prepared_pure_program_cache_free(&context.prepared_pure_cache);
        arena_free(&output);
        checks++;
    }
    prepared_mutation_arena = NULL;
    prepared_fixture = false;
    space_free(&space);
    arena_free(&source);
    eval_set_library_context(NULL);
    cetta_library_context_free(library);
    free(library);
    return checks;
}

/* Once a tail native packet is the cursor's only remaining work, a source
 * change during entry must not discard it or invoke the native a second time. */
/* A geometric depth ladder exercises shared ancestors with an unfinished
 * equation at every level. Values have linear printed size; repeated leaf
 * variables still expose broken aliasing without an exponential tree. */
static unsigned caller_dag_scaling_controls(void) {
    CettaLibraryContext *library = calloc(1u, sizeof(*library));
    assert(library);
    cetta_library_context_init_for_language_profile(library, CETTA_LANGUAGE_PETTA, NULL);
    eval_set_library_context(library);
    Arena source;
    arena_init_detached(&source);
    Space space;
    space_init(&space);
    const unsigned depths[] = {8u, 24u, 48u};
    unsigned checks = 0u;
    prepared_head = symbol_intern_cstr(g_symbols, "__cetta_lib_json_parse_v1");
    prepared_fixture = true;
    for (size_t rung = 0u; rung < sizeof(depths) / sizeof(*depths); rung++) {
        unsigned depth = depths[rung];
        for (unsigned level = 0u; level <= depth; level++) {
            for (unsigned alternative = 0u; alternative < 2u; alternative++) {
                char definition[256];
                if (alternative)
                    snprintf(definition, sizeof(definition), "(= (dag-%u-%u $x) %u)", depth, level, level);
                else if (!level)
                    snprintf(definition, sizeof(definition),
                        "(= (dag-%u-0 $x) (let $v (__cetta_lib_json_parse_v1 $x) (Leaf $v $v)))", depth);
                else
                    snprintf(definition, sizeof(definition),
                        "(= (dag-%u-%u $x) (let $v (dag-%u-%u $x) (Wrap $v)))",
                        depth, level, depth, level - 1u);
                Atom *equation = fixture_atom(&source, definition);
                const PettaPlanNode *plan = petta_program_plan_dynamic_add(library->petta_program, equation);
                assert(plan);
                space_add(&space, equation);
                assert(petta_program_note_add(library->petta_program, &space,
                    space_get_at64(&space, space_length64(&space) - 1u), plan));
            }
        }
        char call[96], head[64];
        snprintf(head, sizeof(head), "dag-%u-%u", depth, depth);
        snprintf(call, sizeof(call), "(%s 11)", head);
        resumption_root = symbol_intern_cstr(g_symbols, head);
        resumption_allowance = 65536u;
        prepared_mode = 33u;
        prepared_calls = prepared_attempts = prepared_effects = 0u;
        CettaPreparedPureAnswerCursor *cursor = resumption_open_cursor(NULL, &space, &source,
            fixture_atom(&source, call), NULL, NULL, NULL);
        assert(cursor);
        cetta_prepared_pure_answer_cursor_defer_native_completion(cursor);
        CettaOwnedReturn packet;
        CettaEvalCompletion completion = CETTA_EVAL_COMPLETE;
        assert(cetta_prepared_pure_answer_cursor_return(cursor, &packet, &completion) ==
            CETTA_OWNED_EXECUTION_HANDOFF && completion == CETTA_EVAL_COMPLETE &&
            cetta_prepared_pure_answer_cursor_native_transfer_ready(cursor));
        size_t count = cetta_prepared_pure_answer_cursor_frame_count(cursor);
        size_t *frames = calloc(count, sizeof(*frames));
        assert(frames);
        for (size_t index = 0u; index < count; index++) frames[index] = index;
        Arena reading;
        arena_init_detached(&reading);
        AtomDeepCopySession *copy = atom_deep_copy_session_new(&reading);
        CettaPreparedPureCallerGraph graph = {0};
        CallerReadObservation observed = {.copy = copy};
        assert(copy && cetta_prepared_pure_answer_cursor_read_callers(cursor, frames, count,
            &reading, observe_caller_read, &observed, &graph));
        assert(!observed.input_reads);
        size_t expanded = 0u;
        for (size_t index = 0u; index < count; index++)
            for (size_t root = graph.roots[index]; root; root = graph.nodes[root - 1u].parent)
                expanded++;
        if (graph.length != (size_t)depth + 1u ||
            2u * expanded != (size_t)(depth + 1u) * (depth + 2u))
            fprintf(stderr, "caller DAG depth=%u frames=%zu nodes=%zu expanded=%zu\n",
                depth, count, graph.length, expanded);
        /* Owned ancestors are actual shared records, not copied suffixes.
         * The readout cannot manufacture a second node for the same caller. */
        assert(graph.length == (size_t)depth + 1u && graph.length < expanded / 2u &&
            2u * expanded == (size_t)(depth + 1u) * (depth + 2u) && prepared_calls == 1u);
        cetta_prepared_pure_caller_graph_free(&graph);
        atom_deep_copy_session_free(copy);
        arena_free(&reading);
        free(frames);
        cetta_prepared_pure_answer_cursor_close(cursor);
        checks++;
        for (unsigned ordinary = 0u; ordinary < 2u; ordinary++) {
            Arena output;
            arena_init_detached(&output);
            PettaEvalMachineContext context = {.fuel = -1, .library_context = library,
                .semantic_authority_epoch = 1u, .prepared_pure_cache = {.root_space = &space}};
            PettaMachineHost host = petta_eval_machine_host(&context, -1, false, false,
                false, true, false, petta_eval_machine_match_semantics(library, false));
            host.execute_prepared_pure_call = NULL;
            host.open_relation_cursor = NULL;
            host.open_answer_cursor = ordinary ? NULL : resumption_open_cursor;
            host.next_answer_cursor = resumption_next_cursor;
            resumption_pulls = resumption_native_transfers = 0u;
            prepared_calls = prepared_attempts = prepared_effects = 0u;
            PettaMachine machine;
            assert(petta_machine_init(&machine, &space, &output, fixture_atom(&output, call), NULL, &host));
            unsigned answers = 0u;
            for (;;) {
                Atom *answer = NULL;
                Bindings environment;
                PettaMachineStep step = petta_machine_next(&machine, &answer, &environment);
                if (step != PETTA_MACHINE_STEP_ANSWER) {
                    bindings_free(&environment);
                    assert(step == PETTA_MACHINE_STEP_EXHAUSTED);
                    break;
                }
                Atom *value = bindings_apply_if_vars(&environment, &output, answer);
                assert(value && answers <= depth + 1u);
                unsigned wraps = answers ? depth + 1u - answers : depth;
                for (unsigned index = 0u; index < wraps; index++) {
                    assert(value->kind == ATOM_EXPR && value->expr.len == 2u &&
                        atom_is_symbol(value->expr.elems[0], "Wrap"));
                    value = value->expr.elems[1];
                }
                if (!answers)
                    assert(value->kind == ATOM_EXPR && value->expr.len == 3u &&
                        atom_is_symbol(value->expr.elems[0], "Leaf") &&
                        value->expr.elems[1]->kind == ATOM_VAR &&
                        atom_eq(value->expr.elems[1], value->expr.elems[2]));
                else
                    assert(atom_eq(value, atom_int(&output, answers - 1u)));
                bindings_free(&environment);
                answers++;
            }
            assert(answers == depth + 2u && prepared_calls == 1u && !prepared_effects &&
                resumption_native_transfers == (ordinary ? 0u : 1u));
            petta_machine_destroy(&machine);
            prepared_pure_program_cache_free(&context.prepared_pure_cache);
            arena_free(&output);
            checks++;
        }
    }
    prepared_fixture = false;
    space_free(&space);
    arena_free(&source);
    eval_set_library_context(NULL);
    cetta_library_context_free(library);
    free(library);
    return checks;
}

static unsigned stale_native_tail_controls(void) {
    CettaLibraryContext *library = calloc(1u, sizeof(*library));
    assert(library);
    cetta_library_context_init_for_language_profile(library, CETTA_LANGUAGE_PETTA, NULL);
    eval_set_library_context(library);
    Arena source;
    arena_init_detached(&source);
    Space space;
    space_init(&space);
    const char *equations[] = {
        "(= (stale-tail $x) (__cetta_lib_json_parse_v1 $x))",
        "(= (stale-caller $x) (let $v (__cetta_lib_json_parse_v1 $x) (Pair $v $v)))",
        "(= (stale-choice $x) (__cetta_lib_json_parse_v1 $x))",
        "(= (stale-choice $x) 99)",
    };
    for (size_t index = 0u; index < sizeof(equations) / sizeof(*equations); index++) {
        Atom *equation = fixture_atom(&source, equations[index]);
        const PettaPlanNode *plan = petta_program_plan_dynamic_add(library->petta_program, equation);
        assert(plan);
        space_add(&space, equation);
        assert(petta_program_note_add(library->petta_program, &space,
            space_get_at64(&space, index), plan));
    }
    prepared_fixture = true;
    prepared_head = symbol_intern_cstr(g_symbols, "__cetta_lib_json_parse_v1");
    prepared_mutation_arena = &source;
    SymbolId unrelated = symbol_intern_cstr(g_symbols, "body-authority-unrelated");
    const uint64_t allowances[] = {0u, 1u, 2u, 64u};
    unsigned checks = 0u, transfers = 0u;
    for (unsigned mutation = 0u; mutation < 2u; mutation++)
    for (unsigned compiled = 0u; compiled < 2u; compiled++)
    for (unsigned budget = 0u; budget < 4u; budget++)
    for (unsigned early = 0u; early < 2u; early++) {
        assert(cetta_library_petta_translator_rule_set(library, unrelated, false));
        Arena execution;
        arena_init_detached(&execution);
        PettaEvalMachineContext context = {.fuel = -1, .library_context = library,
            .semantic_authority_epoch = 1u, .prepared_pure_cache = {.root_space = &space}};
        PettaMachineHost host = petta_eval_machine_host(&context, -1, false, false,
            false, false, false, petta_eval_machine_match_semantics(library, false));
        host.execute_prepared_pure_call = NULL;
        host.open_relation_cursor = NULL;
        host.open_answer_cursor = compiled ? resumption_open_cursor : NULL;
        host.next_answer_cursor = resumption_next_cursor;
        resumption_root = symbol_intern_cstr(g_symbols, "stale-tail");
        resumption_allowance = allowances[budget];
        resumption_opened = resumption_pulls = resumption_native_transfers = 0u;
        prepared_mode = mutation ? 27u : 25u;
        prepared_calls = prepared_attempts = prepared_effects = 0u;
        uint64_t before = space_length64(&space);
        PettaMachine machine;
        assert(petta_machine_init(&machine, &space, &execution,
            fixture_atom(&execution, "(stale-tail 11)"), NULL, &host));
        Atom *answer = NULL;
        Bindings environment;
        PettaMachineStep step = petta_machine_next(&machine, &answer, &environment);
        if (step != PETTA_MACHINE_STEP_ANSWER)
            fprintf(stderr, "stale tail mutation=%u compiled=%u budget=%u early=%u step=%u native=%u\n",
                mutation, compiled, budget, early, step, prepared_calls);
        assert(step == PETTA_MACHINE_STEP_ANSWER &&
            atom_eq(bindings_apply_if_vars(&environment, &execution, answer), atom_int(&execution, 17)));
        bindings_free(&environment);
        if (!early) {
            answer = NULL;
            assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_EXHAUSTED);
            bindings_free(&environment);
        }
        assert(prepared_calls == 1u && !prepared_effects &&
            space_length64(&space) == before + (mutation ? 0u : 1u));
        if (mutation)
            assert(cetta_library_petta_translator_rule_contains(library, unrelated));
        if (compiled && budget != 0u)
            assert(resumption_opened && resumption_native_transfers == 1u);
        if (!compiled)
            assert(!resumption_opened && !resumption_native_transfers);
        transfers += resumption_native_transfers;
        petta_machine_destroy(&machine);
        assert(prepared_calls == 1u);
        prepared_pure_program_cache_free(&context.prepared_pure_cache);
        arena_free(&execution);
        checks++;
    }
    assert(transfers > 0u);
    /* The stale tail transfers provenance and control, not just integers. */
    bool delayed_provider = cetta_lib_prolog_runtime_available(library->lib_prolog);
    for (unsigned scoped = 0u; scoped < 2u; scoped++)
    for (unsigned mode = 43u; mode <= 49u; mode++)
    for (unsigned compiled = 0u; compiled < 2u; compiled++)
    for (unsigned early = 0u; early < 2u; early++) {
        Arena execution;
        arena_init_detached(&execution);
        PettaEvalMachineContext context = {.fuel = -1, .library_context = library,
            .semantic_authority_epoch = 1u, .prepared_pure_cache = {.root_space = &space}};
        PettaMachineHost host = petta_eval_machine_host(&context, -1, false, false,
            false, false, false, petta_eval_machine_match_semantics(library, false));
        host.execute_prepared_pure_call = NULL;
        host.open_relation_cursor = NULL;
        host.open_answer_cursor = compiled ? resumption_open_cursor : NULL;
        host.next_answer_cursor = resumption_next_cursor;
        resumption_root = symbol_intern_cstr(g_symbols, "stale-tail");
        resumption_allowance = 64u;
        resumption_opened = resumption_pulls = resumption_native_transfers = 0u;
        prepared_mode = mode;
        prepared_calls = prepared_attempts = prepared_effects = 0u;
        uint64_t before = space_length64(&space);
        PettaMachine machine;
        assert(petta_machine_init(&machine, &space, &execution,
            fixture_atom(&execution, scoped ? "(once (stale-tail 11))" :
                "(stale-tail 11)"), NULL, &host));
        unsigned count = 0u, sevens = 0u, nines = 0u;
        PettaMachineStep step;
        for (;;) {
            Atom *answer = NULL;
            Bindings environment;
            step = petta_machine_next(&machine, &answer, &environment);
            if (step == PETTA_MACHINE_STEP_ANSWER) {
                Atom *value = bindings_apply_if_vars(&environment, &execution, answer);
                assert(value);
                if (mode == 43u)
                    assert(atom_eq(value, fixture_atom(&execution, "(readln!)")) && !prepared_effects);
                else if (mode == 45u)
                    assert(atom_eq(value, fixture_atom(&execution, "(Error 17 fixture)")) &&
                        petta_machine_last_answer_raised(&machine));
                else if (mode == 46u) {
                    assert(value->kind == ATOM_GROUNDED && value->ground.gkind == GV_INT);
                    sevens += value->ground.ival == 7;
                    nines += value->ground.ival == 9;
                    assert(value->ground.ival == 7 || value->ground.ival == 9);
                } else if (mode == 47u)
                    assert(atom_eq(value, atom_int(&execution, 5)));
                else if (mode == 49u) {
                    Atom *delayed = NULL;
                    assert(atom_has_vars(value) && petta_machine_answer_delayed(
                        &machine, answer, &environment, &execution, &delayed) && delayed);
                    Atom *observation = atom_expr2(&execution, value, delayed);
                    Atom *variable = atom_var_with_id(&execution, "$expected", fresh_var_id());
                    Atom *goal = atom_expr3(&execution, atom_symbol(&execution, "dif"),
                        variable, atom_int(&execution, 1));
                    Atom *component = atom_expr2(&execution,
                        atom_petta_prolog_compound(&execution, goal), variable);
                    Atom *carried = atom_expr(&execution, &variable, 1u);
                    Atom *components = atom_expr(&execution, &component, 1u);
                    Atom *expected = atom_expr2(&execution, variable,
                        atom_expr2(&execution, carried, components));
                    if (!atom_alpha_eq(observation, expected)) {
                        fputs("stale delayed actual=", stderr); atom_print(observation, stderr);
                        fputc('\n', stderr);
                    }
                    assert(atom_alpha_eq(observation, expected));
                } else
                    assert(false);
                count++;
            }
            bindings_free(&environment);
            if (step != PETTA_MACHINE_STEP_ANSWER || early || mode == 45u)
                break;
        }
        unsigned expected = mode == 44u || mode == 48u || (mode == 49u && !delayed_provider) ? 0u
            : early || scoped || mode == 43u || mode == 45u || mode == 49u ? 1u
            : mode == 46u ? 3u : 2u;
        if (count != expected || (mode != 48u && !count && step != PETTA_MACHINE_STEP_EXHAUSTED))
            fprintf(stderr, "stale packet mode=%u scoped=%u compiled=%u early=%u count=%u step=%u native=%u\n",
                mode, scoped, compiled, early, count, step, prepared_calls);
        assert(count == expected && prepared_calls == 1u &&
            space_length64(&space) == before + 1u);
        if (mode == 48u)
            assert(step == PETTA_MACHINE_STEP_SUSPENDED);
        else if (!early && mode != 45u)
            assert(step == PETTA_MACHINE_STEP_EXHAUSTED);
        if (mode == 46u && !early && !scoped)
            assert(sevens == 2u && nines == 1u);
        if (mode == 47u)
            assert(prepared_effects >= count && prepared_effects <= 2u &&
                (early || scoped || prepared_effects == 2u));
        else
            assert(!prepared_effects);
        if (compiled)
            assert(resumption_opened && resumption_native_transfers == 1u);
        petta_machine_destroy(&machine);
        assert(prepared_calls == 1u);
        prepared_pure_program_cache_free(&context.prepared_pure_cache);
        arena_free(&execution);
        checks++;
    }
    /* Without saved source plans, stale source still blocks a caller or
     * an untried equation. Refusal must leave the entered packet intact. */
    const char *blocked[] = {"(stale-caller 11)", "(stale-choice 11)"};
    const char *blocked_heads[] = {"stale-caller", "stale-choice"};
    resumption_missing_caller_plans = true;
    for (unsigned item = 0u; item < 2u; item++) {
        resumption_root = symbol_intern_cstr(g_symbols, blocked_heads[item]);
        resumption_allowance = 64u;
        prepared_mode = 25u;
        prepared_calls = prepared_attempts = prepared_effects = 0u;
        CettaPreparedPureAnswerCursor *cursor = resumption_open_cursor(NULL, &space, &source,
            fixture_atom(&source, blocked[item]), NULL, NULL, NULL);
        assert(cursor);
        cetta_prepared_pure_answer_cursor_defer_native_completion(cursor);
        CettaOwnedReturn returned;
        CettaEvalCompletion completion = CETTA_EVAL_COMPLETE;
        assert(prepared_pure_next_return(&space, cursor, 64u, &returned, &completion) ==
            CETTA_OWNED_EXECUTION_HANDOFF && completion == CETTA_EVAL_INCOMPLETE_INVALIDATED);
        CettaInvocation invocation;
        bool entered = false;
        assert(cetta_prepared_pure_answer_cursor_native_boundary(cursor, &invocation, &entered, &returned) &&
            entered && atom_eq(returned.outcome.term, atom_int(&source, 17)));
        Atom *packet = returned.outcome.term;
        CettaOwnedExecution refused = {0};
        assert(!cetta_prepared_pure_answer_cursor_native_transfer_ready(cursor) &&
            !cetta_prepared_pure_answer_cursor_take_native(cursor, &refused) &&
            !refused.computation && cetta_prepared_pure_answer_cursor_native_boundary(
                cursor, &invocation, &entered, &returned) && returned.outcome.term == packet);
        cetta_prepared_pure_answer_cursor_close(cursor);
        assert(prepared_calls == 1u && !prepared_effects);
        checks++;
    }
    resumption_missing_caller_plans = false;
    prepared_fixture = false;
    prepared_mutation_arena = NULL;
    space_free(&space);
    arena_free(&source);
    eval_set_library_context(NULL);
    cetta_library_context_free(library);
    free(library);
    return checks;
}

typedef struct {
    Space *nested;
    unsigned seen;
    bool early;
} OrderedPatternVisitor;

static bool ordered_pattern_nested_result(Atom *result, const Bindings *bindings, void *opaque) {
    (void)bindings;
    unsigned *seen = opaque;
    assert(atom_is_symbol(result, "Nested"));
    (*seen)++;
    return true;
}

static bool ordered_pattern_evicting_result(Atom *result, const Bindings *bindings, void *opaque) {
    (void)bindings;
    OrderedPatternVisitor *visitor = opaque;
    Arena nested;
    arena_init(&nested);
    assert(atom_eq(result, fixture_atom(&nested, "(Result 7 9)")));
    if (!visitor->seen) {
        unsigned seen = 0u;
        /* The same head in another space evicts this cache slot while the
         * outer call still owns an untried duplicate equation. */
        assert(eval_query_equations_visit(visitor->nested,
            fixture_atom(&nested, "(ordered-live (Pair 7 9))"), &nested,
            ordered_pattern_nested_result, &seen) == 1u && seen == 1u);
    }
    visitor->seen++;
    arena_free(&nested);
    return !visitor->early;
}

static unsigned ordered_pattern_plan_controls(void) {
    const struct { const char *pattern, *query; bool matches; } cases[] = {
        {"(ordered $x $x)", "(ordered (+ 1 2) (+ 1 2))", true},
        {"(ordered $x $x)", "(ordered 7 9)", false},
        {"(ordered $x)", "(ordered $query)", true},
        {"(ordered 2 3)", "(ordered $probe 1)", false},
        {"(ordered $x (Wrap $x))", "(ordered $query $query)", false},
        {"(ordered (Pair $x $y) $x)", "(ordered (Pair 7 9) 7)", true},
        {"(ordered $x)", "(different 7)", false},
        {"(ordered $x 3)", "(ordered 2 1)", false},
    };
    CETTA_FRAME_IDENTITY_SCOPE(identities);
    unsigned checks = 0u;
    for (unsigned test = 0u; test < sizeof(cases) / sizeof(*cases); test++) {
        Arena arena;
        arena_init(&arena);
        CettaVarMap inventory;
        cetta_var_map_init(&inventory);
        Atom *pattern = cetta_compile_frame_syntax(&arena,
            fixture_atom(&arena, cases[test].pattern), &inventory);
        VarId ids[8];
        Atom *variables[8];
        assert(inventory.len <= 8u);
        for (uint32_t i = 0u; i < inventory.len; i++) {
            variables[i] = inventory.items[i].mapped_var;
            ids[i] = variables[i]->var_id;
        }
        BindingsFrameSchema *schema = bindings_frame_schema_new_presented(
            ids, variables, inventory.len);
        assert(schema && pattern);
        CettaOpenPatternPlan *plan = cetta_open_pattern_plan_compile(&arena, pattern, schema);
        assert(plan && !plan->linear_program);
        Atom *query = fixture_atom(&arena, cases[test].query);
        CettaFrameIdentity epoch;
        assert(cetta_frame_identity_scope_try(&identities, &epoch));
        for (unsigned fallback = 0u; fallback < 3u; fallback++) {
            BindingsBuilder reference, planned;
            assert(bindings_builder_init(&reference, NULL) &&
                bindings_builder_init(&planned, NULL));
            assert(bindings_builder_register_complete_frame_schema(&reference, schema, epoch) &&
                bindings_builder_register_complete_frame_schema(&planned, schema, epoch));
            Atom *pending = fixture_atom(&arena, "$pending");
            Atom *held = fixture_atom(&arena, "(+ 4 5)");
            assert(bindings_add_constraint(&reference.current, pending, held) &&
                bindings_add_constraint(&planned.current, pending, held));
            CettaOpenPatternPlan mismatch = *plan;
            mismatch.source = query;
            const CettaOpenPatternPlan *selected = fallback == 0u ? plan
                : fallback == 1u ? NULL : &mismatch;
            bool expected = match_atoms_epoch_builder(query, pattern, &reference, &arena, epoch);
            bool actual = match_atoms_epoch_builder_planned(query, pattern, selected, &planned, &arena, epoch);
            assert(expected == cases[test].matches && actual == expected);
            assert(bindings_eq(&reference.current, &planned.current));
            if (test == 2u) {
                BindingValue value = bindings_lookup_value_id(&planned.current,
                    query->expr.elems[1]->var_id);
                assert(value.skeleton == pattern->expr.elems[1] &&
                    cetta_frame_identity_eq(binding_value_epoch(value), epoch));
            }
            if (test == 3u) {
                BindingValue value = bindings_lookup_value_id(&planned.current,
                    query->expr.elems[1]->var_id);
                assert(value.skeleton && atom_eq(value.skeleton, atom_int(&arena, 2)));
            }
            if (test == 7u) {
                BindingValue value = bindings_lookup_context_id(&planned.current,
                    binding_value_from_context(pattern, epoch), pattern->expr.elems[1]->var_id);
                assert(value.skeleton && atom_eq(value.skeleton, atom_int(&arena, 2)));
            }
            bindings_builder_free(&reference);
            bindings_builder_free(&planned);
            checks++;
        }
        bindings_frame_schema_release(schema);
        cetta_var_map_free(&inventory);
        arena_free(&arena);
    }
    {
        Arena arena;
        arena_init(&arena);
        VarId ids[65];
        Atom *variables[65], *values[65];
        for (unsigned i = 0u; i < 65u; i++) {
            char spelling[24];
            snprintf(spelling, sizeof(spelling), "wide-%u", i);
            ids[i] = var_id_from_base(i + 1u);
            variables[i] = atom_var_with_id(&arena, spelling, ids[i]);
            values[i] = atom_int(&arena, i);
        }
        BindingsFrameSchema *schema = bindings_frame_schema_new_presented(ids, variables, 65u);
        assert(schema);
        Atom *pattern = atom_expr(&arena, variables, 65u);
        Atom *query = atom_expr(&arena, values, 65u);
        CettaOpenPatternPlan *plan = cetta_open_pattern_plan_compile(&arena, pattern, schema);
        assert(plan && !plan->variable_mask && !plan->variable_ids);
        CettaFrameIdentity epoch;
        assert(cetta_frame_identity_scope_try(&identities, &epoch));
        BindingsBuilder reference, planned;
        assert(bindings_builder_init(&reference, NULL) && bindings_builder_init(&planned, NULL));
        assert(match_atoms_epoch_builder(query, pattern, &reference, &arena, epoch) &&
            match_atoms_epoch_builder_planned(query, pattern, plan, &planned, &arena, epoch));
        assert(bindings_eq(&reference.current, &planned.current));
        bindings_builder_free(&reference);
        bindings_builder_free(&planned);
        checks++;
        BindingsFrameSchema *incomplete = bindings_frame_schema_new(NULL, 0u);
        assert(incomplete && !cetta_open_pattern_plan_compile(&arena, pattern, incomplete));
        assert(!cetta_open_pattern_plan_compile(&arena, atom_list(&arena, values, 65u), schema));
        bindings_frame_schema_release(incomplete);
        bindings_frame_schema_release(schema);
        arena_free(&arena);
        checks++;
    }
    CettaLibraryContext *library = calloc(1u, sizeof(*library));
    assert(library);
    cetta_library_context_init_for_language_profile(library, CETTA_LANGUAGE_HE,
        cetta_profile_he_extended());
    eval_set_library_context(library);
    Arena arena;
    arena_init(&arena);
    Space space;
    space_init(&space);
    assert(space_admit_atom(&space, &arena, fixture_atom(&arena,
        "(= (ordered-live (Pair $x $y)) (Result $x $y))")));
    assert(space_admit_atom(&space, &arena, fixture_atom(&arena,
        "(= (ordered-live (Pair $x $y)) (Result $x $y))")));
    const char *setting = getenv("CETTA_HE_MATCH_DECISION");
    char *saved_setting = setting ? strdup(setting) : NULL;
    const char *modes[] = {"off", "linear", "1"};
    for (unsigned mode = 0u; mode < 3u; mode++) {
    assert(setenv("CETTA_HE_MATCH_DECISION", modes[mode], 1) == 0);
    for (unsigned pass = 0u; pass < 2u; pass++) {
        ordered_pattern_plan_calls = 0u;
        observe_ordered_pattern_plans = true;
        EvalOutcome outcome;
        eval_outcome_init(&outcome);
        metta_eval_outcome(&space, &arena, NULL,
            fixture_atom(&arena, "(ordered-live (Pair 7 9))"), 10000, &outcome);
        observe_ordered_pattern_plans = false;
        assert(outcome.completion == CETTA_EVAL_COMPLETE && outcome.results.len == 2u &&
            ordered_pattern_plan_calls == (mode ? 2u : 0u));
        for (unsigned result = 0u; result < 2u; result++)
            assert(atom_eq(outcome.results.items[result], fixture_atom(&arena, "(Result 7 9)")));
        eval_outcome_free(&outcome);
        checks++;
    }
    }
    Space nested;
    space_init(&nested);
    assert(space_admit_atom(&nested, &arena, fixture_atom(&arena,
        "(= (ordered-live (Pair $x $y)) Nested)")));
    for (unsigned early = 0u; early < 2u; early++) {
        OrderedPatternVisitor visitor = {.nested = &nested, .early = early};
        ordered_pattern_plan_calls = 0u;
        observe_ordered_pattern_plans = true;
        assert(eval_query_equations_visit(&space,
            fixture_atom(&arena, "(ordered-live (Pair 7 9))"), &arena,
            ordered_pattern_evicting_result, &visitor) == (early ? 1u : 2u));
        observe_ordered_pattern_plans = false;
        assert(visitor.seen == (early ? 1u : 2u) &&
            ordered_pattern_plan_calls == (early ? 2u : 3u));
        checks++;
    }
    space_free(&nested);
    assert(space_admit_atom(&space, &arena, fixture_atom(&arena,
        "(= (ordered-live (Pair 7 9)) (quote (+ 1 2)))")));
    ordered_pattern_plan_calls = 0u;
    observe_ordered_pattern_plans = true;
    EvalOutcome changed;
    eval_outcome_init(&changed);
    metta_eval_outcome(&space, &arena, NULL, fixture_atom(&arena,
        "(ordered-live (Pair 7 9))"), 10000, &changed);
    observe_ordered_pattern_plans = false;
    assert(changed.completion == CETTA_EVAL_COMPLETE && changed.results.len == 3u &&
        ordered_pattern_plan_calls == 3u);
    unsigned code_data = 0u;
    for (unsigned i = 0u; i < changed.results.len; i++) {
        if (atom_eq(changed.results.items[i], fixture_atom(&arena, "(quote (+ 1 2))")))
            code_data++;
        else
            assert(atom_eq(changed.results.items[i], fixture_atom(&arena, "(Result 7 9)")));
    }
    assert(code_data == 1u);
    eval_outcome_free(&changed);
    checks++;
    assert(space_admit_atom(&space, &arena, fixture_atom(&arena,
        "(: unify (-> Atom Atom Atom Atom %Undefined%))")));
    assert(space_admit_atom(&space, &arena, fixture_atom(&arena,
        "(= (ordered-guard $value) (unify $value (Pair $x $y) (Result $x $y) Empty))")));
    for (unsigned route = 0u; route < 2u; route++) {
    assert(setenv("CETTA_HE_MATCH_DECISION", route ? "1" : "off", 1) == 0);
    for (unsigned pass = 0u; pass < 2u; pass++) {
        ordered_pattern_plan_calls = 0u;
        observe_ordered_pattern_plans = true;
        EvalOutcome guarded;
        eval_outcome_init(&guarded);
        metta_eval_outcome(&space, &arena, NULL, fixture_atom(&arena, pass
            ? "(ordered-guard (Other 7 9))" : "(ordered-guard (Pair 7 9))"), 10000, &guarded);
        observe_ordered_pattern_plans = false;
        /* The standard declaration holds all operands and completes its
         * Undefined result, so Empty publishes no answer. */
        if (guarded.completion != CETTA_EVAL_COMPLETE || guarded.results.len != (pass ? 0u : 1u) ||
            ordered_pattern_plan_calls != route)
            fprintf(stderr, "ordered guard route=%u pass=%u completion=%u answers=%" PRIu64 " plans=%u\n",
                route, pass, (unsigned)guarded.completion, (uint64_t)guarded.results.len,
                ordered_pattern_plan_calls);
        assert(guarded.completion == CETTA_EVAL_COMPLETE &&
            guarded.results.len == (pass ? 0u : 1u) && ordered_pattern_plan_calls == route);
        if (!pass)
            assert(atom_eq(guarded.results.items[0], fixture_atom(&arena, "(Result 7 9)")));
        eval_outcome_free(&guarded);
        checks++;
    }
    }
    const int allowances[] = {0, 1, 2, 8, 128};
    for (unsigned branch = 0u; branch < 2u; branch++)
    for (unsigned slice = 0u; slice < sizeof(allowances) / sizeof(*allowances); slice++) {
        EvalOutcome reference, planned;
        eval_outcome_init(&reference);
        eval_outcome_init(&planned);
        Atom *call = fixture_atom(&arena, branch ? "(ordered-guard (Other 7 9))"
            : "(ordered-guard (Pair 7 9))");
        assert(setenv("CETTA_HE_MATCH_DECISION", "off", 1) == 0);
        metta_eval_outcome(&space, &arena, NULL, call, allowances[slice], &reference);
        assert(setenv("CETTA_HE_MATCH_DECISION", "1", 1) == 0);
        metta_eval_outcome(&space, &arena, NULL, call, allowances[slice], &planned);
        assert(reference.completion == planned.completion &&
            reference.results.len == planned.results.len &&
            reference.steps_spent == planned.steps_spent &&
            reference.budget_remaining == planned.budget_remaining);
        for (unsigned i = 0u; i < reference.results.len; i++)
            assert(atom_alpha_eq(reference.results.items[i], planned.results.items[i]));
        if (!slice)
            assert(planned.completion == CETTA_EVAL_INCOMPLETE_FUEL && !planned.results.len);
        if (slice == 4u)
            assert(planned.completion == CETTA_EVAL_COMPLETE &&
                planned.results.len == (branch ? 0u : 1u));
        eval_outcome_free(&reference);
        eval_outcome_free(&planned);
        checks++;
    }
    assert(space_admit_atom(&space, &arena, fixture_atom(&arena,
        "(: ordered-code (-> Atom Atom))")));
    assert(space_admit_atom(&space, &arena, fixture_atom(&arena,
        "(= (ordered-code (Pair $x $y)) (+ $x $y))")));
    for (unsigned mode = 0u; mode < 3u; mode++) {
        assert(setenv("CETTA_HE_MATCH_DECISION", modes[mode], 1) == 0);
        EvalOutcome data;
        eval_outcome_init(&data);
        metta_eval_outcome(&space, &arena, NULL, fixture_atom(&arena,
            "(ordered-code (Pair 7 9))"), 10000, &data);
        assert(data.completion == CETTA_EVAL_COMPLETE && data.results.len == 1u &&
            atom_eq(data.results.items[0], fixture_atom(&arena, "(+ 7 9)")));
        eval_outcome_free(&data);
        checks++;
    }
    if (saved_setting) {
        assert(setenv("CETTA_HE_MATCH_DECISION", saved_setting, 1) == 0);
        free(saved_setting);
    } else
        assert(unsetenv("CETTA_HE_MATCH_DECISION") == 0);
    space_free(&space);
    arena_free(&arena);
    eval_set_library_context(NULL);
    cetta_library_context_free(library);
    free(library);
    printf("PASS: %u ordered pattern-plan controls preserve binding direction, failed prefixes, residuals and duplicate live calls\n", checks);
    return checks;
}

static unsigned execution_semantic_ledger_controls(void) {
    const CettaLanguageId languages[] = {CETTA_LANGUAGE_PETTA, CETTA_LANGUAGE_HE, CETTA_LANGUAGE_PRIME};
    const struct { const char *source, *petta, *held; unsigned count; } cases[] = {
        {"(+ 1 2)", "3", "3", 1u},
        {"(quote (+ 1 2))", "(+ 1 2)", "(quote (+ 1 2))", 1u},
        {"(let $v (quote (+ 1 2)) $v)", "(+ 1 2)", "(quote (+ 1 2))", 1u},
        {"(let $v (quote (+ 1 2)) (Pair $v $v))", "(Pair (+ 1 2) (+ 1 2))",
            "(Pair (quote (+ 1 2)) (quote (+ 1 2)))", 1u},
        {"(superpose (7 7 9))", NULL, NULL, 3u},
    };
    unsigned checks = 0u;
    for (unsigned dialect = 0u; dialect < 3u; dialect++) {
        CettaLibraryContext *library = calloc(1u, sizeof(*library));
        assert(library);
        cetta_library_context_init_for_language_profile(library, languages[dialect], NULL);
        eval_set_library_context(library);
        Arena arena;
        arena_init(&arena);
        Space space;
        space_init(&space);
        for (unsigned test = 0u; test < sizeof(cases) / sizeof(cases[0]); test++) {
            Atom *source = fixture_atom(&arena, cases[test].source);
            ResultSet plain;
            result_set_init(&plain);
            metta_eval(&space, &arena, NULL, source, 10000, &plain);
            EvalOutcome observed;
            eval_outcome_init(&observed);
            metta_eval_outcome(&space, &arena, NULL, source, 10000, &observed);
            if (plain.len != cases[test].count || observed.results.len != cases[test].count)
                fprintf(stderr, "semantic ledger language=%u source=%s counts=%" PRIu64 "/%" PRIu64 "\n",
                    languages[dialect], cases[test].source, (uint64_t)plain.len, (uint64_t)observed.results.len);
            assert(plain.len == cases[test].count && observed.results.len == cases[test].count &&
                observed.completion == CETTA_EVAL_COMPLETE);
            for (unsigned index = 0u; index < cases[test].count; index++) {
                Atom *expected = cases[test].count == 3u ? atom_int(&arena, index < 2u ? 7 : 9)
                    : fixture_atom(&arena, dialect == 0u ? cases[test].petta : cases[test].held);
                assert(atom_eq(plain.items[index], expected) &&
                    atom_eq(observed.results.items[index], expected));
            }
            result_set_free(&plain);
            eval_outcome_free(&observed);
            checks++;
        }
        const char *partial_source;
        const char *partial_values[2];
        unsigned partial_count;
        if (dialect == 0u) {
            refusal_add_equation(&space, &arena, "(= (ledger-keep $x $y) (Pair $x $y))");
            partial_source = "(let $v (quote (+ 1 2)) ((ledger-keep $v) 9))";
            partial_values[0] = "(Pair (+ 1 2) 9)";
            partial_count = 1u;
        } else if (dialect == 1u) {
            refusal_add_equation(&space, &arena, "(= (ledger-keep $x $y) (Pair $x $y))");
            partial_source = "(ledger-keep 9)";
            partial_values[0] = "(ledger-keep 9)";
            partial_count = 1u;
        } else {
            refusal_add_equation(&space, &arena, "(= (ledger-partial A) selected)");
            partial_source = "(ledger-partial (superpose (A B)))";
            partial_values[0] = "selected";
            partial_values[1] = "(ledger-partial B)";
            partial_count = 2u;
        }
        EvalOutcome partial;
        eval_outcome_init(&partial);
        metta_eval_outcome(&space, &arena, NULL, fixture_atom(&arena, partial_source), 10000, &partial);
        assert(partial.completion == CETTA_EVAL_COMPLETE && partial.results.len == partial_count);
        for (unsigned index = 0u; index < partial_count; index++)
            assert(atom_eq(partial.results.items[index], fixture_atom(&arena, partial_values[index])));
        eval_outcome_free(&partial);
        checks++;
        if (dialect < 2u) {
            refusal_add_equation(&space, &arena, "(: ledger-raw (-> Atom Atom))");
            refusal_add_equation(&space, &arena, "(= (ledger-raw $x) $x)");
            refusal_add_equation(&space, &arena, "(: ledger-expression (-> Expression Atom))");
            refusal_add_equation(&space, &arena, "(= (ledger-expression $x) $x)");
            for (unsigned demand = 0u; demand < 2u; demand++) {
                EvalOutcome held;
                eval_outcome_init(&held);
                metta_eval_outcome(&space, &arena, NULL, fixture_atom(&arena, demand
                    ? "(ledger-expression (+ 1 2))" : "(ledger-raw (+ 1 2))"), 10000, &held);
                unsigned expected_count = demand && dialect == 0u ? 0u : 1u;
                assert(held.completion == CETTA_EVAL_COMPLETE && held.results.len == expected_count);
                if (expected_count)
                    assert(atom_eq(held.results.items[0], fixture_atom(&arena, "(+ 1 2)")));
                eval_outcome_free(&held);
                checks++;
            }
        }
        ResultSet limited;
        result_set_init(&limited);
        metta_eval(&space, &arena, NULL, fixture_atom(&arena, "(+ 1 2)"), 0, &limited);
        EvalOutcome pending;
        eval_outcome_init(&pending);
        metta_eval_outcome(&space, &arena, NULL, fixture_atom(&arena, "(+ 1 2)"), 0, &pending);
        assert(!limited.len && !pending.results.len &&
            pending.completion == CETTA_EVAL_INCOMPLETE_FUEL &&
            eval_outcome_zero_status(&pending) == CETTA_EVAL_ZERO_PENDING);
        result_set_free(&limited);
        eval_outcome_free(&pending);
        checks++;
        Atom *shape = fixture_atom(&arena, "(+ 1 2)");
        Atom *shared = atom_var_with_id(&arena, "$ledger-shared", fresh_var_id());
        Atom *delayed = atom_expr2(&arena, shared, shared);
        Atom *duplicates = atom_expr3(&arena, shape, shape, atom_int(&arena, 9));
        CettaCallOutcome packets[] = {cetta_call_value(shape), cetta_call_completed_value(shape),
            cetta_call_suspended(shared, delayed), cetta_call_completed_frontier(duplicates),
            cetta_call_expression_frontier(duplicates), cetta_call_interrupted(), cetta_call_failure()};
        for (unsigned packet = 0u; packet < sizeof(packets) / sizeof(packets[0]); packet++) {
            CettaCallReturnCursor cursor = {.outcome = packets[packet]};
            CettaEvalCompletion completion = CETTA_EVAL_COMPLETE;
            CettaOwnedReturn returned = {0};
            CettaOwnedExecutionStep step = cetta_call_return_next(&cursor, &returned, &completion);
            if (packet == 5u) {
                assert(step == CETTA_OWNED_EXECUTION_INTERRUPTED &&
                    completion == CETTA_EVAL_INCOMPLETE_CANCELLED);
            } else if (packet == 6u) {
                assert(step == CETTA_OWNED_EXECUTION_COMPLETE && completion == CETTA_EVAL_COMPLETE);
            } else {
                assert(step == CETTA_OWNED_EXECUTION_RETURN && completion == CETTA_EVAL_COMPLETE);
                if (packet == 3u) {
                    assert(returned.outcome.kind == CETTA_CALL_VALUE &&
                        returned.outcome.result_form == CETTA_CALL_RESULT_COMPLETED_VALUE &&
                        returned.outcome.term == shape && !returned.final);
                    assert(cetta_call_return_next(&cursor, &returned, &completion) == CETTA_OWNED_EXECUTION_RETURN &&
                        returned.outcome.term == shape && !returned.final);
                    assert(cetta_call_return_next(&cursor, &returned, &completion) == CETTA_OWNED_EXECUTION_RETURN &&
                        atom_eq(returned.outcome.term, atom_int(&arena, 9)) && returned.final);
                } else {
                    CettaCallResultForm expected_form = packet == 1u ? CETTA_CALL_RESULT_COMPLETED_VALUE
                        : packet == 4u ? CETTA_CALL_RESULT_EXPRESSION_FRONTIER : CETTA_CALL_RESULT_EXPRESSION;
                    assert(returned.outcome.kind == (packet == 2u ? CETTA_CALL_SUSPENDED : CETTA_CALL_VALUE) &&
                        returned.outcome.result_form == expected_form &&
                        returned.outcome.term == packets[packet].term && returned.final);
                    if (packet == 2u)
                        assert(returned.outcome.delayed == delayed &&
                            var_id_eq(returned.outcome.term->var_id, delayed->expr.elems[0]->var_id) &&
                            var_id_eq(returned.outcome.term->var_id, delayed->expr.elems[1]->var_id));
                }
                assert(cetta_call_return_next(&cursor, &returned, &completion) == CETTA_OWNED_EXECUTION_COMPLETE);
            }
            checks++;
        }
        space_free(&space);
        arena_free(&arena);
        eval_set_library_context(NULL);
        cetta_library_context_free(library);
        free(library);
    }
    printf("PASS: %u semantic-ledger observations distinguish source, held data, residuals, duplicates and optional results\n", checks);
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
    bool refusal_only = argc == 2 && strcmp(argv[1], "--source-refusal") == 0;
    bool lexical_source_only = argc == 2 && strcmp(argv[1], "--lexical-source") == 0;
    bool source_role_only = argc == 2 && strcmp(argv[1], "--source-role") == 0;
    bool caller_only = argc == 2 && strcmp(argv[1], "--caller-result") == 0;
    bool authority_only = argc == 2 && strcmp(argv[1], "--authority-current") == 0;
    bool compiler_refusal_only = argc == 2 && strcmp(argv[1], "--compiler-refusal") == 0;
    bool episode_only = argc == 2 && strcmp(argv[1], "--episode-contract") == 0;
    bool prime_data_only = argc == 2 && strcmp(argv[1], "--prime-data") == 0;
    bool fold_only = argc == 2 && strcmp(argv[1], "--fold-completion") == 0;
    bool callee_only = argc == 2 && strcmp(argv[1], "--callee-entry") == 0;
    bool body_only = argc == 2 && strcmp(argv[1], "--body-entry") == 0;
    bool lexical_only = argc == 2 && strcmp(argv[1], "--lexical-completion") == 0;
    bool collection_failure_only = argc == 2 && strcmp(argv[1], "--borrowed-collection-failure") == 0;
    bool bound_completion_only = argc == 2 && strcmp(argv[1], "--bound-completion") == 0;
    bool caller_dag_only = argc == 2 && strcmp(argv[1], "--caller-dag") == 0;
    bool he_prepared_only = argc == 2 && strcmp(argv[1], "--he-prepared-completion") == 0;
    bool he_completion_only = argc == 2 && strcmp(argv[1], "--he-completion") == 0;
    bool conditional_only = argc == 2 && strcmp(argv[1], "--conditional-continuation") == 0;
    bool resumption_only = argc == 2 && strcmp(argv[1], "--contextual-resumption") == 0;
    bool stale_tail_only = argc == 2 && strcmp(argv[1], "--stale-native-tail") == 0;
    bool ledger_only = argc == 2 && strcmp(argv[1], "--semantic-ledger") == 0;
    bool ordered_only = argc == 2 && strcmp(argv[1], "--ordered-pattern-plans") == 0;
    bool metered_invocation_only = argc == 2 && strcmp(argv[1], "--metered-invocation") == 0;
    assert(argc == 1 || metered_invocation_only || ordered_only || ledger_only || caller_only || he_prepared_only || authority_only || source_role_only || lexical_source_only || compiler_refusal_only || refusal_only || availability_only || episode_only || prime_data_only || fold_only || callee_only || lexical_only || body_only || collection_failure_only || bound_completion_only || resumption_only || conditional_only || stale_tail_only || caller_dag_only || he_completion_only);
    unsigned checks = prepared_worker_availability();
    if (ordered_only || ledger_only || argc == 1)
        checks += ordered_pattern_plan_controls();
    if (metered_invocation_only || ledger_only || argc == 1)
        checks += metered_invocation_controls();
    if (ledger_only || argc == 1)
        checks += execution_semantic_ledger_controls();
    if (ledger_only)
        checks += source_computation_plans() + source_invocation_plans() + generic_budget_checks() + source_first_success_plans();
    if (caller_only)
        checks += caller_result_controls() + caller_real_collection_controls();
    else if (he_prepared_only)
        checks += he_prepared_completion_controls();
    else if (authority_only)
        checks += authority_observation_controls();
    else if (source_role_only)
        checks += source_role_elaboration_controls();
    else if (lexical_source_only)
        checks += lexical_source_controls();
    else if (compiler_refusal_only)
        checks += uncertified_compile_refusal_controls() + shared_compilation_refusals();
    else if (refusal_only)
        checks += source_admission_refusal_controls() + he_source_refusal_controls();
    else if (he_completion_only)
        checks += he_instruction_completion_controls(true);
    else if (stale_tail_only)
        checks += stale_native_tail_controls();
    else if (caller_dag_only)
        checks += frozen_source_caller_controls() + branching_native_frontier_controls() + caller_dag_scaling_controls();
    else if (conditional_only)
        checks += conditional_native_continuation_controls() + native_frontier_rejoin_controls() +
            branching_native_frontier_controls();
    else if (resumption_only)
        checks += contextual_resumption_controls();
    else if (bound_completion_only)
        checks += bound_native_completion_controls();
    else if (collection_failure_only)
        checks += borrowed_collection_failure_control();
    else if (body_only)
        checks += prepared_body_entry_controls();
    else if (lexical_only)
        checks += scalar_callable_admission() + lexical_packet_controls() + program_operand_controls() + lexical_native_tail_controls() + oem_body_retention_controls() + relational_body_destination_controls();
    else if (callee_only)
        checks += oem_callee_owner_controls() + ready_invocation_controls();
    else if (fold_only)
        checks += fold_completion_controls();
    else if (prime_data_only)
        checks += prime_prepared_data_controls();
    else if (episode_only)
        checks += episode_accounting() + evaluator_episode_consumers();
    else if (!availability_only && !ledger_only && !ordered_only && !metered_invocation_only)
        checks += caller_result_controls() + caller_real_collection_controls() + he_prepared_completion_controls() + authority_observation_controls() + lexical_source_controls() + uncertified_compile_refusal_controls() + source_admission_refusal_controls() + he_source_refusal_controls() + he_instruction_completion_controls(false) + frozen_source_caller_controls() + stale_native_tail_controls() + conditional_native_continuation_controls() + native_frontier_rejoin_controls() + branching_native_frontier_controls() + caller_dag_scaling_controls() + contextual_resumption_controls() + bound_native_completion_controls() + relational_body_destination_controls() + oem_body_retention_controls() + prepared_body_entry_controls() + lexical_native_tail_controls() + lexical_packet_controls() + program_operand_controls() + oem_callee_owner_controls() + fold_completion_controls() + prime_prepared_data_controls() + episode_accounting() + evaluator_episode_consumers() + source_computation_plans() + source_role_elaboration_controls() + source_invocation_plans() + prepared_computation_plans() + generic_budget_checks() + source_first_success_plans() + source_collection_plans() + source_case_plans() + captured_binding_projection_controls() + he_instruction_plan_controls() + he_canonical_plan_controls() + he_instruction_source_gc_controls() + ordinary_invocation_consumers() + shared_compilation_refusals() + closed_failure_cache() + ready_invocation_controls() + evaluator_consumers() + projected_roots() + residual_consumers() + prepared_native_consumers() + scalar_callable_admission();
    cetta_lib_prolog_global_shutdown();
    symbol_table_free(&symbols);
    var_intern_free(&variables);
    g_symbols = NULL;
    g_var_intern = NULL;
    printf("PASS: %u conditional/context return controls, including ordinary residual bind\n", checks);
    return 0;
}
