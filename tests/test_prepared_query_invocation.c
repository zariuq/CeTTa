/* Exercise the private query entrance with borrowed, still-scoped operands.
 * Public ready-call routes often resolve those operands before this boundary. */
#include "../src/petta_search_machine.c"
#include "parser.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>

typedef struct {
    Atom *expected;
    CettaExprLen arity;
    unsigned mode, entries;
} QueryProbe;

static Atom *query_probe_parse(Arena *arena, const char *text) {
    size_t position = 0u;
    Atom *value = parse_sexpr(arena, text, &position);
    assert(value);
    return value;
}

static bool query_probe_enter(void *context, Space *space, Arena *arena,
        const CettaInvocation *invocation, Atom *source,
        CettaCallOutcome *outcome) {
    (void)space;
    QueryProbe *probe = context;
    CettaExprLen arity = 0u;
    assert(!source && cetta_invocation_arity(invocation, &arity) && arity == probe->arity);
    for (CettaExprIndex index = 0u; index < arity; index++) {
        BindingValue value = cetta_invocation_argument(invocation, index);
        assert(!binding_value_is_contextual(value) &&
            atom_eq(value.skeleton, probe->expected));
    }
    probe->entries++;
    if (probe->mode == 1u)
        return false;
    *outcome = probe->mode == 2u ? cetta_call_failure()
        : probe->mode == 3u ? cetta_call_interrupted()
        : cetta_call_completed_value(arity ? cetta_invocation_argument(invocation, 0u).skeleton
                                         : atom_unit(arena));
    return true;
}

int main(void) {
    SymbolTable symbols;
    VarInternTable variables;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols;
    g_var_intern = &variables;
    const CettaExprLen arities[] = {0u, 1u, 8u, 9u, 65u};
    unsigned checks = 0u;
    for (unsigned width = 0u; width < 5u; width++)
    for (unsigned views = 0u; views < 2u; views++)
    for (unsigned mode = 0u; mode < 4u; mode++) {
        Arena source, output;
        arena_init(&source);
        arena_init(&output);
        Space space;
        space_init(&space);
        Bindings base;
        bindings_init(&base);
        Atom *shared = atom_var_with_id(&source, "$shared", fresh_var_id());
        Atom *alias = atom_var_with_id(&source, "$alias", fresh_var_id());
        Atom *other = atom_var_with_id(&source, "$other", fresh_var_id());
        assert(bindings_add_var(&base, shared, atom_int(&source, 7)) &&
            bindings_add_var(&base, alias, shared) &&
            bindings_add_var(&base, other, atom_int(&source, 9)));
        Atom *payload = atom_expr(&source,
            (Atom *[]){atom_symbol(&source, "Payload"), shared, alias, other}, 4u);
        Atom *arguments[65];
        CettaGsltTermCursorV1 fields[65];
        for (unsigned index = 0u; index < 65u; index++) {
            arguments[index] = payload;
            fields[index] = (CettaGsltTermCursorV1){.source = payload};
        }
        QueryProbe probe = {.expected = query_probe_parse(&source, "(Payload 7 7 9)"),
            .arity = arities[width], .mode = mode};
        PettaMachineHost host = {.context = &probe,
            .unlimited_transition_budget = true,
            .execute_prepared_pure_call = query_probe_enter};
        PettaMachine machine;
        assert(petta_machine_init(&machine, &space, &output,
            atom_int(&source, 0), &base, &host));
        PettaSpaceQueryView query = {
            .head = atom_symbol(&source, "prepared-query-probe"),
            .operands = {.atoms = arguments, .count = probe.arity},
            .argument_views = views ? fields : NULL,
        };
        assert(petta_machine_space_query_closed(machine.impl, &query));
        size_t bindings_before = search_context_bindings(&machine.impl->search)->len;
        CettaCallOutcome outcome = {0};
        bool entered = petta_machine_execute_prepared_query(
            machine.impl, &query, NULL, &outcome);
        assert(entered == (mode != 1u) && probe.entries == 1u &&
            search_context_bindings(&machine.impl->search)->len == bindings_before &&
            atom_has_vars(payload) && payload->expr.elems[1] == shared &&
            payload->expr.elems[2] == alias && payload->expr.elems[3] == other);
        if (entered) {
            assert(outcome.kind == (mode == 2u ? CETTA_CALL_FAILURE
                : mode == 3u ? CETTA_CALL_INTERRUPTED : CETTA_CALL_VALUE));
            if (!mode)
                assert(atom_eq(outcome.term, probe.arity ? probe.expected : atom_unit(&source)));
        }
        petta_machine_destroy(&machine);
        bindings_free(&base);
        space_free(&space);
        arena_free(&output);
        arena_free(&source);
        checks++;
    }
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    g_var_intern = NULL;
    assert(checks == 40u);
    printf("PASS: %u prepared query entries preserve borrowed fields, aliases and outcomes\n", checks);
    return 0;
}
