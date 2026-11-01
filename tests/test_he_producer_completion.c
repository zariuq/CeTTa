/* Exercise a completed producer's retained state independently of source
 * admission. The bank may be empty after successes have already streamed. */
#include "../src/petta_search_machine.c"

#include <assert.h>
#include <stdio.h>

static bool batch_retains_last(Atom *const *rows, unsigned length, bool prefer) {
    unsigned successes[8], errors[8], success_count = 0u, error_count = 0u;
    for (unsigned index = 0u; index < length; index++) {
        if (atom_is_error(rows[index]))
            errors[error_count++] = index;
        else
            successes[success_count++] = index;
    }
    if (!prefer)
        return length != 0u;
    unsigned *selected = success_count ? successes : errors;
    unsigned count = success_count ? success_count : error_count;
    for (unsigned index = 0u; index < count; index++)
        if (selected[index] + 1u == length)
            return true;
    return false;
}

int main(void) {
    SymbolTable symbols;
    VarInternTable variables;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols;
    g_var_intern = &variables;
    unsigned checks = 0u;
    for (unsigned preference = 0u; preference < 2u; preference++)
    for (unsigned prior = 0u; prior < (preference ? 3u : 1u); prior++)
    for (unsigned kind = 0u; kind < 4u; kind++) {
        Arena arena;
        arena_init_detached(&arena);
        Space space;
        space_init(&space);
        PettaMachineHost host = {.eager_argument_faults = true,
            .retain_query_variable_identity = true};
        PettaMachine machine;
        assert(petta_machine_init(&machine, &space, &arena,
            atom_int(&arena, 7), NULL, &host));
        PettaMachineImpl *impl = machine.impl;
        impl->goal_len = 0u;
        Atom *target = impl->answer_variable;
        assert(petta_machine_push_completion(impl, atom_int(&arena, 7),
            &target, 0u, preference != 0u));
        impl->choices[0].as.outcomes.prefer_success = preference != 0u;
        /* This is a valid empty-bank prefix: its preferred occurrences have
         * already been given to the taker, with their multiplicity intact. */
        impl->choices[0].as.outcomes.has_success = prior != 0u;
        PettaGoal last = impl->goals[--impl->goal_len];
        Atom *rows[4];
        for (unsigned index = 0u; index < prior; index++)
            rows[index] = atom_int(&arena, 7);
        Atom *error = atom_error(&arena, atom_int(&arena, 1), atom_symbol(&arena, "fault"));
        rows[prior] = kind == 0u ? error : kind == 1u ? atom_int(&arena, 9)
            : kind == 2u ? atom_var_with_id(&arena, "$authored", fresh_var_id())
            : atom_expr2(&arena, atom_symbol(&arena, "Box"), error);
        last.first = rows[prior];
        PettaMachineStep failure = PETTA_MACHINE_STEP_EXHAUSTED;
        bool retained = petta_machine_dispatch_goal(impl, last, &failure);
        assert(retained == batch_retains_last(rows, prior + 1u, preference != 0u));
        assert(impl->choice_len == 0u);
        if (retained && kind == 2u) {
            BindingValue resolved;
            assert(bindings_resolve_value_preview((Bindings *)search_context_bindings(&impl->search),
                binding_value_from_atom(impl->answer_variable), &resolved));
            assert(resolved.skeleton->kind == ATOM_VAR &&
                var_id_eq(resolved.skeleton->var_id, rows[prior]->var_id));
        }
        petta_machine_destroy(&machine);
        space_free(&space);
        arena_free(&arena);
        checks++;
    }
    printf("PASS: %u native completion states preserve batch visibility and authored returns\n", checks);
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    return 0;
}
