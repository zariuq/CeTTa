#include "petta_libpl.h"
#include "parser.h"
#include "symbol.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static Arena arena;
static unsigned checks;
static Atom *parse(const char *text) {
    Atom **forms = NULL;
    int count = parse_metta_text(text, &arena, &forms);
    assert(count == 1 && forms);
    Atom *value = forms[0];
    free(forms);
    return value;
}

static CettaCount invoke_atom_kind(CettaLibPrologRuntime *runtime, Atom *expr,
                             const CettaDelayView *delay, bool same,
                             CettaCallOutcomeKind expected_kind) {
    Bindings env;
    bindings_init(&env);
    OutcomeSet outcomes;
    outcome_set_init(&outcomes);
    Atom *expected = atom_var_with_id(&arena, "$result", fresh_var_id());
    CettaCallOutcome end = cetta_call_failure();
    bool recognized = false;
    CettaLibPrologReadToken before = cetta_lib_prolog_read_token(runtime);
    assert(petta_libpl_call(runtime, &arena, expr, expected, &env,
                           &outcomes, &recognized, &end, delay));
    assert(recognized && end.kind == expected_kind);
    CettaLibPrologReadToken after = cetta_lib_prolog_read_token(runtime);
    if ((before.revision == after.revision) != same) {
        fprintf(stderr, "revision expectation %u: before=%llu after=%llu same=%d\n",
            checks, (unsigned long long)before.revision,
            (unsigned long long)after.revision, same);
        abort();
    }
    assert(before.instance_id == after.instance_id);
    CettaCount count = outcomes.len;
    outcome_set_free(&outcomes);
    bindings_free(&env);
    checks++;
    return count;
}
static CettaCount invoke_atom(CettaLibPrologRuntime *runtime, Atom *expr,
                             const CettaDelayView *delay, bool same) {
    return invoke_atom_kind(runtime, expr, delay, same, CETTA_CALL_FAILURE);
}
static CettaCount invoke(CettaLibPrologRuntime *runtime, const char *text, bool same) {
    return invoke_atom(runtime, parse(text), NULL, same);
}
static CettaLibPrologRuntime *fresh(void) {
    CettaLibPrologRuntime *runtime = cetta_lib_prolog_runtime_new();
    assert(runtime && cetta_lib_prolog_runtime_available(runtime));
    return runtime;
}
static void load(CettaLibPrologRuntime *runtime, const char *path) {
    char source[2048];
    int len = snprintf(source, sizeof(source),
        "(callPredicate (Predicate (consult \"%s\")))", path);
    assert(len > 0 && (size_t)len < sizeof(source));
    assert(invoke(runtime, source, false) == 1);
}
int main(int argc, char **argv) {
    assert(argc == 3);
    SymbolTable symbols;
    VarInternTable vars;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&vars);
    g_symbols = &symbols;
    g_var_intern = &vars;
    arena_init(&arena);
    CettaLibPrologRuntime *runtime = fresh();
    for (unsigned i = 0; i < 32; i++)
        assert(invoke(runtime, "(callPredicate (Predicate (cyclic_term (a b c))))", true) == 0);
    assert(invoke(runtime, "(callPredicate (Predicate true))", false) == 1);
    assert(invoke(runtime, "(callPredicate (Predicate (, "
        "(Predicate (assertz (Predicate (observer_write witness)))) fail)))", false) == 0);
    assert(invoke(runtime, "(callPredicate (Predicate (observer_write witness)))", false) == 1);
    assert(invoke_atom_kind(runtime,
        parse("(callPredicate (Predicate (throw observer_fault)))"), NULL,
        false, CETTA_CALL_RAISED) == 0);
    assert(invoke(runtime, "(callPredicate (Predicate (cyclic_term $x)))", true) == 0);
    load(runtime, argv[1]);
    assert(invoke(runtime, "(callPredicate (Predicate (cyclic_term a)))", false) == 0);
    assert(invoke(runtime, "(callPredicate (Predicate observer_seen))", false) == 1);
    assert(invoke(runtime, "(callPredicate (Predicate (abolish (Predicate (/ cyclic_term 1)))))", false) == 1);
    assert(invoke(runtime, "(callPredicate (Predicate (cyclic_term a)))", true) == 0);
    cetta_lib_prolog_runtime_free(runtime);

    runtime = fresh();
    assert(invoke(runtime, "(callPredicate (Predicate (cyclic_term a)))", true) == 0);
    load(runtime, argv[2]);
    assert(invoke(runtime, "(callPredicate (Predicate (cyclic_term a)))", false) == 0);
    /* The witness query itself crosses the replaced wrapper once more. */
    assert(invoke(runtime, "(callPredicate (Predicate observer_wrapper_seen))", false) == 2);
    cetta_lib_prolog_runtime_free(runtime);

    runtime = fresh();
    assert(invoke(runtime, "(callPredicate (Predicate (cyclic_term a)))", true) == 0);
    Atom *shared = parse("(test (callPredicate (Predicate (cyclic_term $x))) "
        "(Predicate (assertz (Predicate (observer_delayed $x)))))");
    Atom *expr = shared->expr.elems[1];
    Atom *delayed = shared->expr.elems[2];
    Atom *var = expr->expr.elems[1]->expr.elems[1]->expr.elems[1];
    assert(var->kind == ATOM_VAR);
    CettaDelayService *service = cetta_delay_service_new();
    assert(service && cetta_delay_suspend(service, 0u, CETTA_DELAY_CLIENT_PROLOG,
        delayed, &var->var_id, 1u, false));
    CettaDelayView view = {.service = service};
    assert(invoke_atom(runtime, expr, &view, false) == 0);
    assert(invoke(runtime, "(callPredicate (Predicate (observer_delayed a)))", false) == 1);
    cetta_delay_service_free(service);
    cetta_lib_prolog_runtime_free(runtime);
    cetta_lib_prolog_global_shutdown();
    arena_free(&arena);
    g_symbols = NULL;
    g_var_intern = NULL;
    var_intern_free(&vars);
    symbol_table_free(&symbols);
    printf("PASS: %u observer calls preserve revisions, overrides, restoration and carried effects\n", checks);
    return 0;
}
