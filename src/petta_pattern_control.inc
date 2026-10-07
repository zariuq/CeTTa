/* Binding policy changes the match transition, not the interpretation of a
 * body. Original body occurrences and their plans return to ordinary SOLVE.
 * Matching protects the subject during this transition only. */
static bool petta_pattern_control_status(PettaMachineImpl *machine,
    CettaTermMatchStatus status, Atom *source, PettaMachineStep *failure) {
    if (status == CETTA_TERM_MATCH_INVALID)
        return petta_machine_pattern_query_fault(machine, source, "InvalidTermGraph");
    if (status == CETTA_TERM_MATCH_NO_MEMORY)
        *failure = PETTA_MACHINE_STEP_CAPACITY;
    return status == CETTA_TERM_MATCH_OK;
}

static bool petta_pattern_control_wait(PettaMachineImpl *machine,
    const PettaGoal *continuation, Atom *source, const PettaPlanNode *plan) {
    PettaGoal ready = *continuation;
    ready.third = petta_fresh_variable(machine);
    return ready.third && petta_goal_push(machine, &ready) &&
        petta_push_solve_planned(machine, source, ready.third, ready.barrier, plan);
}

static bool petta_pattern_control_ready(PettaMachineImpl *machine,
    const PettaGoal *goal, PettaMachineStep *failure) {
    const Bindings *env = search_context_bindings(&machine->search);
    BindingsBuilder *builder = search_context_builder(&machine->search);
    Atom *call = goal->first;
    SymbolId head = atom_head_symbol_id(call);
    if (!cetta_pattern_adapter_arity(head)) head = eval_current_pattern_head(head);
    Atom *value = bindings_apply_graph((Bindings *)env, &machine->heap, goal->third);
    if (!value) { *failure = PETTA_MACHINE_STEP_CAPACITY; return false; }
    if (head == g_builtin_syms.pat_case_forward) {
        Atom *branches = petta_machine_resolve_root_or_reference(env, call->expr.elems[2]);
        if (!branches || branches->kind != ATOM_EXPR)
            return petta_machine_pattern_query_fault(machine, call, "ExpectedCaseBranches");
        for (CettaExprIndex i = 0u; i < branches->expr.len; ++i) {
            Atom *branch = branches->expr.elems[i];
            if (branch->kind != ATOM_EXPR || branch->expr.len != 2u)
                return petta_machine_pattern_query_fault(machine, call, "ExpectedCaseBranches");
            CettaTermMatchPair pair = {
                bindings_apply_graph((Bindings *)env, &machine->heap, branch->expr.elems[0]), value};
            CettaTermMatchStatus status = cetta_pattern_bind_many(&machine->heap, &pair, 1u, builder);
            if (status == CETTA_TERM_MATCH_MISMATCH) continue;
            if (!petta_pattern_control_status(machine, status, call, failure)) return false;
            return petta_push_solve_planned(machine, branch->expr.elems[1], goal->second,
                goal->barrier, petta_plan_child(petta_plan_child(
                    petta_plan_child(goal->plan, 2u), i), 1u));
        }
        return false;
    }
    CettaTermMatchPair pair = {
        bindings_apply_graph((Bindings *)env, &machine->heap, call->expr.elems[1]), value};
    CettaTermMatchStatus status = cetta_pattern_bind_many(&machine->heap, &pair, 1u, builder);
    CettaExprIndex selected = 3u;
    if (head == g_builtin_syms.pat_unify_forward && status == CETTA_TERM_MATCH_MISMATCH)
        selected = 4u;
    else if (!petta_pattern_control_status(machine, status, call, failure)) return false;
    return petta_push_solve_planned(machine, call->expr.elems[selected], goal->second,
        goal->barrier, petta_plan_child(goal->plan, selected));
}

static bool petta_pattern_control_start(PettaMachineImpl *machine,
    Atom *call, Atom *expected, const PettaGoal *source, PettaMachineStep *failure) {
    SymbolId head = atom_head_symbol_id(call);
    if (!cetta_pattern_adapter_arity(head)) head = eval_current_pattern_head(head);
    uint32_t arity = cetta_pattern_adapter_arity(head);
    if (call->expr.len != arity + 1u)
        return petta_machine_pattern_query_fault(machine, call, "IncorrectNumberOfArguments");
    PettaGoal ready = {.kind = PETTA_GOAL_PATTERN_CONTROL_READY,
        .first = call, .second = expected, .barrier = source->barrier, .plan = source->plan};
    if (head == g_builtin_syms.pat_unify_forward) {
        ready.third = call->expr.elems[2];
        return petta_pattern_control_ready(machine, &ready, failure);
    }
    CettaExprIndex index = head == g_builtin_syms.pat_let_forward ? 2u : 1u;
    return petta_pattern_control_wait(machine, &ready, call->expr.elems[index],
        petta_plan_child(source->plan, index));
}
