/* Inspect structured results of real evaluator calls. Printed atoms cannot
 * reveal a dropped pending check. This unit replaces eval.o at link time. */
#include "../src/eval.c"
#include "parser.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>

static SymbolId conditional_head = SYMBOL_ID_NONE;
static unsigned conditional_calls;
static bool opaque_check;
static unsigned conditional_shape;

bool __real_grounded_call(Arena *arena, Atom *head, Atom **arguments,
                         uint32_t count, CettaCallOutcome *outcome);
bool __wrap_grounded_call(Arena *arena, Atom *head, Atom **arguments,
                         uint32_t count, CettaCallOutcome *outcome) {
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

int main(void) {
    SymbolTable symbols;
    VarInternTable variables;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols;
    g_var_intern = &variables;
    unsigned checks = evaluator_consumers() + projected_roots();
    symbol_table_free(&symbols);
    var_intern_free(&variables);
    g_symbols = NULL;
    g_var_intern = NULL;
    printf("PASS: %u conditional-context consumers and ownership projections\n", checks);
    return 0;
}
