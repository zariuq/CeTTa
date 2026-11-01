/* Exercise the actual checker entry points with an unrelated host registry.
 * This translation unit replaces eval.o. */
#include "../src/eval.c"
#include <assert.h>

typedef bool (*GuestEvaluate)(Arena *, Atom *, Atom **, Atom **);

static Atom *guest_read(Arena *arena, const char *text) {
    size_t position = 0u;
    Atom *atom = parse_sexpr(arena, text, &position);
    assert(atom);
    return atom;
}

static Atom *guest_ask(GuestEvaluate evaluate, Arena *arena, const char *text) {
    Atom *answer = NULL, *reason = NULL;
    bool ok = evaluate(arena, guest_read(arena, text), &answer, &reason);
    if (!ok) {
        fputs("guest request refused: ", stderr);
        if (reason) atom_print(reason, stderr);
        fputc('\n', stderr);
    }
    assert(ok && answer);
    return answer;
}

static bool refuse_guest_call(void *raw, Space *space,
    PettaMachineOperation operation, const PettaMachineOperationView *view) {
    (void)space;
    assert(operation == PETTA_MACHINE_OPERATION_CALL && view && view->expression);
    (*(size_t *)raw)++;
    return false;
}

typedef struct {
    EvalExecutionContext *host;
    size_t calls;
} GuestQueryOwnerCheck;

static bool check_guest_query_owner(void *raw, Space *space,
    PettaMachineOperation operation, const PettaMachineOperationView *view) {
    GuestQueryOwnerCheck *check = raw;
    (void)space;
    assert(operation == PETTA_MACHINE_OPERATION_CALL && view && view->expression);
    assert(g_eval_execution && g_eval_execution != check->host && g_eval_execution->scoped);
    check->calls++;
    return true;
}

static bool refuse_later_guest_call(void *raw, Space *space,
    PettaMachineOperation operation, const PettaMachineOperationView *view) {
    (void)space;
    assert(operation == PETTA_MACHINE_OPERATION_CALL && view && view->expression);
    Atom *call = view->expression;
    if (call->kind == ATOM_EXPR && call->expr.len == 3u &&
        atom_is_symbol(call->expr.elems[0], "+")) {
        (*(size_t *)raw)++;
        return false;
    }
    return true;
}

static void check_scalar_type_read_profile(PrimeHolGuest *guest, Arena *arena,
    Space *host_root) {
    PrimeHostedGuestScope scope;
    prime_hosted_guest_scope_enter(guest, &scope);
    Space isolated;
    space_init_with_universe(&isolated, &guest->universe);
    Atom *head = atom_symbol_id(arena, g_builtin_syms.get_type);
    Atom *scalars[] = {atom_int(arena, 7), atom_float(arena, 1.5),
        atom_bigint(arena, "999999999999999999999999"), atom_rational(arena, "1/3"),
        atom_bool(arena, true), atom_string(arena, "text")};
    for (size_t i = 0u; i < sizeof(scalars) / sizeof(*scalars); i++) {
        Atom *call = atom_expr2(arena, head, scalars[i]);
        assert(prime_native_scalar_type_read(&isolated, call));
        assert(prime_native_actual_call_allowed(&isolated, arena, call));
        assert(prime_native_expression_safe(&isolated, arena, call, &g_prime_need_active));
        call = atom_expr2(arena, head, atom_prime_held_wrap(arena, scalars[i]));
        assert(prime_native_scalar_type_read(&isolated, call));
    }
    const char *outside[] = {"(get-type $unknown)", "(get-type unknown)",
        "(get-type (foo 7))", "(get-type 7 &self)", "(get-type)", NULL};
    for (size_t i = 0u; outside[i]; i++)
        assert(!prime_native_scalar_type_read(&isolated, guest_read(arena, outside[i])));
    assert(!prime_native_scalar_type_read(&isolated,
        atom_expr2(arena, head, atom_space(arena, host_root))));
    assert(!prime_native_scalar_type_read(&isolated, atom_expr2(arena, head,
        atom_prime_held_wrap(arena, guest_read(arena, "(println! forbidden)")))));
    Registry registered;
    Registry *prior_registry = g_registry;
    registry_init(&registered);
    registry_bind(&registered, "&checker-getter", head);
    g_registry = &registered;
    Atom *reference = guest_read(arena, "&checker-getter");
    assert(registry_lookup_atom(reference) == head);
    assert(!prime_native_scalar_type_read(&isolated, atom_expr2(arena, reference, scalars[0])));
    g_registry = prior_registry;
    registry_free(&registered);
    /* An authored extension invalidates this intrinsic classification even
     * for a scalar. Its body must keep the ordinary call/effect boundary. */
    space_add(&isolated, guest_read(arena,
        "(= (get-type $value) (println! forbidden))"));
    assert(!prime_native_scalar_type_read(&isolated, atom_expr2(arena, head, scalars[0])));
    space_free(&isolated);
    prime_hosted_guest_scope_leave(&scope);
}

static void check_guest(GuestEvaluate evaluate, CettaLibraryContext *first,
    CettaLibraryContext *second, Registry *host, Arena *arena) {
    Space *host_root = g_eval_root_space;
    TermUniverse *host_universe = g_eval_term_universe_override;
    eval_set_library_context(first);
    assert(!g_prime_eval_context);
    PrimeEvalContext caller = {.entrance = eval_context_capture(),
        .value = eval_context_capture()};
    atomic_init(&caller.references, 1u);
    g_prime_eval_context = &caller;
    Atom *answer = guest_ask(evaluate, arena, "&checker-host-secret");
    assert(atom_is_symbol(answer, "&checker-host-secret"));
    assert(eval_current_registry() == host);
    answer = guest_ask(evaluate, arena,
        "(collapse (match &here (checker-host-row) leaked))");
    assert(answer->kind == ATOM_EXPR && answer->expr.len == 0u);
    answer = guest_ask(evaluate, arena,
        "(collapse (match &home (checker-host-row) leaked))");
    assert(answer->kind == ATOM_EXPR && answer->expr.len == 0u);

    (void)guest_ask(evaluate, arena, "(bind! &checker-private-value 7)");
    assert(!registry_lookup(host, "&checker-private-value"));
    answer = guest_ask(evaluate, arena, "&checker-private-value");
    assert(answer->kind == ATOM_GROUNDED && answer->ground.gkind == GV_INT &&
        answer->ground.ival == 7);

    eval_set_library_context(second);
    answer = guest_ask(evaluate, arena, "&checker-private-value");
    assert(atom_is_symbol(answer, "&checker-private-value"));
    answer = guest_ask(evaluate, arena, "&checker-host-secret");
    assert(atom_is_symbol(answer, "&checker-host-secret"));

    eval_set_library_context(first);
    answer = guest_ask(evaluate, arena, "&checker-private-value");
    assert(answer->kind == ATOM_GROUNDED && answer->ground.gkind == GV_INT &&
        answer->ground.ival == 7);
    assert(eval_current_registry() == host);
    answer = registry_lookup(host, "&checker-host-secret");
    assert(answer && answer->kind == ATOM_GROUNDED && answer->ground.gkind == GV_INT &&
        answer->ground.ival == 91);

    /* Persistence belongs to the checker, even under a host universe and a
     * temporary request arena. The next request reads after that arena dies. */
    Arena temporary;
    arena_init(&temporary);
    (void)guest_ask(evaluate, &temporary,
        "(bind! &checker-persistent-value (private-packet 17 23))");
    arena_free(&temporary);
    answer = guest_ask(evaluate, arena, "&checker-persistent-value");
    assert(atom_alpha_eq(answer, guest_read(arena, "(private-packet 17 23)")));
    assert(!registry_lookup(host, "&checker-persistent-value"));

    Atom *reason = NULL;
    answer = NULL;
    assert(!evaluate(arena, guest_read(arena, "(Error private refused)"), &answer, &reason));
    assert(!answer && reason);
    assert(eval_current_registry() == host && g_eval_root_space == host_root &&
        g_eval_term_universe_override == host_universe);
    assert(g_context_home_depth == 1u && g_context_home_stack[0] == host_root &&
        g_context_here_depth == 1u && g_context_here_stack[0] == host_root);

    /* Private storage never exempts nested calls from the actual caller's
     * permit. A refusal is unavailable evidence, not a checker verdict. */
    size_t refused = 0u;
    PrimeEvalStackDriver parent = {.call_permit = refuse_guest_call,
        .call_permit_context = &refused};
    assert(!g_prime_eval_stack_driver);
    g_prime_eval_stack_driver = &parent;
    answer = reason = NULL;
    bool accepted = evaluate(arena, guest_read(arena, "(== 1 1)"), &answer, &reason);
    g_prime_eval_stack_driver = NULL;
    assert(!accepted && !answer && reason && refused);
    assert(eval_current_registry() == host && g_eval_root_space == host_root &&
        g_eval_term_universe_override == host_universe);

    /* Query tables and variant banks belong to the request, rather than to
     * the typing client which happens to invoke it. */
    EvalExecutionScope host_queries = {0};
    eval_execution_scope_enter(&host_queries, host_root);
    GuestQueryOwnerCheck check = {.host = host_queries.owner};
    parent = (PrimeEvalStackDriver){.call_permit = check_guest_query_owner,
        .call_permit_context = &check};
    g_prime_eval_stack_driver = &parent;
    answer = reason = NULL;
    accepted = evaluate(arena, guest_read(arena, "(== 1 1)"), &answer, &reason);
    g_prime_eval_stack_driver = NULL;
    assert(accepted && answer && check.calls && g_eval_execution == host_queries.owner);
    eval_execution_scope_leave(&host_queries);

    /* Checker requests have their explicit unlimited interpretation; an
     * enclosing program's evaluation fuel is not a checker verdict. */
    PrimeHolGuest *private = evaluate == eval_prime_hol_guest_evaluate
        ? first->prime_hol_guest : first->prime_lf_guest;
    int prior_fuel = private->context.session.options.fuel_limit;
    cetta_eval_session_set_fuel_limit(&private->context.session, 1);
    answer = guest_ask(evaluate, arena, "(if (== 1 1) (+ 1 1) (+ 7 7))");
    assert(answer->kind == ATOM_GROUNDED && answer->ground.gkind == GV_INT && answer->ground.ival == 2);
    assert(private->context.session.options.fuel_limit == 1);
    cetta_eval_session_set_fuel_limit(&private->context.session, prior_fuel);

    /* The intrinsic type of an immutable scalar is a local data read even
     * when the enclosing computation has pure authority. */
    parent = (PrimeEvalStackDriver){.pure_authority = true};
    g_prime_eval_stack_driver = &parent;
    answer = reason = NULL;
    accepted = evaluate(arena, guest_read(arena, "(get-type 7)"), &answer, &reason);
    g_prime_eval_stack_driver = NULL;
    assert(accepted && answer && atom_is_symbol(answer, "Number"));
    refused = 0u;
    parent = (PrimeEvalStackDriver){.pure_authority = true,
        .call_permit = refuse_guest_call, .call_permit_context = &refused};
    g_prime_eval_stack_driver = &parent;
    answer = reason = NULL;
    accepted = evaluate(arena, guest_read(arena, "(get-type 7)"), &answer, &reason);
    g_prime_eval_stack_driver = NULL;
    assert(!accepted && !answer && reason && refused);
    check_scalar_type_read_profile(private, arena, host_root);

    answer = reason = NULL;
    assert(!evaluate(arena, guest_read(arena, "(superpose (alpha beta))"), &answer, &reason));
    assert(!answer && reason);
    size_t later_refusals = 0u;
    parent = (PrimeEvalStackDriver){.call_permit = refuse_later_guest_call,
        .call_permit_context = &later_refusals};
    g_prime_eval_stack_driver = &parent;
    answer = reason = NULL;
    accepted = evaluate(arena, guest_read(arena, "(superpose (alpha (+ 7 7)))"), &answer, &reason);
    g_prime_eval_stack_driver = NULL;
    assert(!accepted && !answer && reason && later_refusals);

    /* Refusal after an earned prefix never turns that prefix into evidence,
     * and retiring the request leaves the same checker usable. */
    answer = guest_ask(evaluate, arena, "(== 1 1)");
    assert(answer->kind == ATOM_GROUNDED && answer->ground.gkind == GV_BOOL && answer->ground.bval);
    assert(g_prime_eval_context == &caller && atomic_load(&caller.references) == 1u);
    g_prime_eval_context = NULL;
}

int main(int argc, char **argv) {
    (void)argc;
    SymbolTable symbols;
    VarInternTable variables;
    Arena arena;
    TermUniverse universe;
    Space space;
    Registry host;
    CettaLibraryContext first, second;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols;
    g_var_intern = &variables;
    arena_init(&arena);
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &arena);
    space_init_with_universe(&space, &universe);
    g_eval_root_space = &space;
    g_eval_term_universe_override = &universe;
    space_add(&space, guest_read(&arena, "(checker-host-row)"));
    g_context_home_stack[0] = g_context_here_stack[0] = &space;
    g_context_home_depth = g_context_here_depth = 1u;
    registry_init(&host);
    registry_bind(&host, "&checker-host-secret", atom_int(&arena, 91));
    g_registry = &host;
    cetta_library_context_init_for_language_profile(
        &first, CETTA_LANGUAGE_PRIME, cetta_profile_prime_default());
    cetta_library_context_init_for_language_profile(
        &second, CETTA_LANGUAGE_PRIME, cetta_profile_prime_default());
    cetta_library_context_set_exec_path(&first, argv[0]);
    cetta_library_context_set_exec_path(&second, argv[0]);

    check_guest(eval_prime_hol_guest_evaluate, &first, &second, &host, &arena);
    check_guest(eval_prime_lf_guest_evaluate, &first, &second, &host, &arena);
    assert(first.prime_hol_guest != second.prime_hol_guest);
    assert(first.prime_lf_guest != second.prime_lf_guest);
    assert(first.prime_hol_guest != first.prime_lf_guest);

    eval_set_library_context(NULL);
    g_registry = NULL;
    g_eval_root_space = NULL;
    g_eval_term_universe_override = NULL;
    g_context_home_depth = g_context_here_depth = 0u;
    cetta_library_context_free(&second);
    cetta_library_context_free(&first);
    registry_free(&host);
    space_free(&space);
    term_universe_free(&universe);
    arena_free(&arena);
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    g_var_intern = NULL;
    puts("PASS: hosted checkers own registries independently of hosts and siblings");
    return 0;
}
