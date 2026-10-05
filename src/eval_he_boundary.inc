/* Minimal instructions consume raw invocation results, not the ordinary
 * interpreter's return-type continuation.  The instruction protocol remains
 * local: a raw RHS is data unless it is a function delimiter, and a function
 * consumes a return-shaped value without interpreting the payload. */
static bool he_minimal_instruction(Atom *atom) {
    SymbolId head = atom_head_symbol_id(atom);
    return head == g_builtin_syms.eval || head == g_builtin_syms.evalc ||
           head == g_builtin_syms.chain || head == g_builtin_syms.function ||
           head == g_builtin_syms.unify || head == g_builtin_syms.metta ||
           head == g_builtin_syms.context_space || head == g_builtin_syms.call_native ||
           head == g_builtin_syms.cons_atom || head == g_builtin_syms.decons_atom ||
           head == g_builtin_syms.collapse_bind || head == g_builtin_syms.superpose_bind;
}

static bool he_not_reducible(Atom *atom) {
    return atom_is_symbol_id(atom, g_builtin_syms.not_reducible);
}

static void he_minimal_run(Space *s, Arena *a, Atom *atom, int fuel,
                           const Bindings *prefix, OutcomeSet *os);

typedef struct {
    Arena *arena;
    const Bindings *prefix;
    OutcomeSet *outcomes;
} HeRawQuery;

static bool he_raw_query_result(Atom *result, const Bindings *bindings,
                                void *context) {
    HeRawQuery *query = context;
    outcome_set_add_prefixed(query->arena, query->outcomes, result,
                            bindings, query->prefix, true);
    return !eval_unwinding();
}

static void he_raw_collect(Space *s, Arena *a, Atom *atom, int fuel,
                          const Bindings *prefix, OutcomeSet *os) {
    __attribute__((cleanup(bindings_free))) Bindings env;
    bindings_init(&env);
    if (prefix && !bindings_copy(&env, prefix)) {
        eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);
        return;
    }
    atom = resolve_registry_refs(a, bindings_apply_if_vars(&env, a, atom));
    __attribute__((cleanup(eval_gc_root_frame_leave)))
    EvalGcRootFrame lexical = {0};
    Atom *unused_type = NULL;
    eval_gc_root_frame_enter(&lexical, &atom, &env, &unused_type);
    OutcomeSet raw;
    outcome_set_init(&raw);
    __attribute__((cleanup(eval_gc_outcome_suspension_end)))
    EvalGcOutcomeSuspension suspension = {0};
    Atom *live[] = {atom};
    eval_gc_outcome_suspension_begin_with_atoms(
        &suspension, &lexical, os, &raw, live, 1u);


    SymbolId head = atom_head_symbol_id(atom);
    CettaExprLen nargs = atom->kind == ATOM_EXPR && atom->expr.len
        ? expr_nargs(atom) : 0u;
    bool invoked = false;
    Atom *prepared = NULL;
    PreparedMinimalWorkerResult worker =
        prepared_minimal_worker_call(s, a, atom, &env, fuel, &prepared);
    if (worker == PREPARED_MINIMAL_WORKER_INTERRUPTED) {
        outcome_set_free(&raw);
        return;
    }
    if (worker == PREPARED_MINIMAL_WORKER_EMIT ||
        worker == PREPARED_MINIMAL_WORKER_REENTER) {
        outcome_set_add(&raw, prepared, &env);
        invoked = true;
    } else if (head == g_builtin_syms.get_type && nargs == 1u) {
        Atom **types = NULL;
        uint32_t count = 0u;
        CettaEvalCompletion completion = CETTA_EVAL_INCOMPLETE_HOST_FAILURE;
        if (!eval_get_type_intrinsic_answers(
                s, a, expr_arg(atom, 0), NULL, fuel, &types, &count,
                &completion)) {
            free(types);
            outcome_set_free(&raw);
            eval_mark_incomplete(completion);
            return;
        }
        for (uint32_t i = 0u; i < count; i++)
            outcome_set_add(&raw, types[i], &env);
        free(types);
        invoked = true;
    } else if (head == g_builtin_syms.superpose && nargs == 1u) {
        Atom *list = expr_arg(atom, 0);
        Atom *const *elements;
        CettaExprLen count;
        if (!atom_sequence_view(list, &elements, &count)) {
            outcome_set_add(&raw, call_signature_error(
                a, atom, "(superpose (: <alternatives> Expression))"), &env);
        } else if (count == 0u) {
            outcome_set_add(&raw, legacy_empty_sentinel(a), &env);
        } else {
            for (CettaExprIndex i = 0u; i < count; i++)
                outcome_set_add(&raw, elements[i], &env);
        }
        invoked = true;
    } else if (head == g_builtin_syms.match && nargs == 3u && g_registry) {
        Space *target = resolve_single_space_arg(
            s, a, expr_arg(atom, 0), fuel);
        if (!target) {
            outcome_set_add(&raw, call_signature_error(
                a, atom, "(match <space> <pattern> <template>)"), &env);
        } else {
            BindingSet rows;
            binding_set_init(&rows);
            match_query_bindings(target, a, expr_arg(atom, 1), &env, &rows);
            MatchVisibleVarSet visible;
            match_visible_var_set_init(&visible);
            collect_match_visible_vars_rec(atom, &visible);
            for (CettaIndex i = 0u; i < rows.len; i++) {
                Bindings projected;
                bindings_init(&projected);
                if (project_match_visible_bindings(a, &visible,
                                                   &rows.items[i], &projected)) {
                    Atom *result = bindings_apply_if_vars(
                        &projected, a, expr_arg(atom, 2));
                    outcome_set_add_prefixed(a, &raw, result,
                                            &projected, &env, true);
                }
                bindings_free(&projected);
            }
            match_visible_var_set_free(&visible);
            binding_set_free(&rows);
            if (raw.len == 0u)
                outcome_set_add(&raw, legacy_empty_sentinel(a), &env);
        }
        invoked = true;
    } else if (atom->kind == ATOM_EXPR && atom->expr.len > 0u) {
        Atom *op = materialize_runtime_token(s, a, atom->expr.elems[0]);
        invoked = dispatch_foreign_outcomes(
            s, a, op, atom->expr.elems + 1u, nargs, atom_symbol(a, "Atom"),
            fuel, &env, false, true, NULL, NULL, NULL, &raw);
        if (!invoked) {
            CettaCallOutcome outcome;
            invoked = dispatch_native_call(
                s, a, op, atom->expr.elems + 1u, nargs, &outcome);
            if (invoked) {
                if (outcome.kind == CETTA_CALL_FAILURE) {
                    outcome_set_add(&raw, legacy_empty_sentinel(a), &env);
                } else if (outcome.kind != CETTA_CALL_INTERRUPTED) {
                    Atom *result = eval_call_outcome_atom(a, outcome);
                    if (result && outcome.result_form != CETTA_CALL_RESULT_EXPRESSION) {
                        emit_completed_native_call(
                            s, a, outcome, result, atom_symbol(a, "Atom"), &env, os);
                        outcome_set_free(&raw);
                        return;
                    } else if (result)
                        outcome_set_add(&raw, result, &env);
                }
            }
        }
    }
    if (!invoked) {
        HeRawQuery query = {a, &env, &raw};
        query_equations_visit(s, atom, a, he_raw_query_result, &query);
        if (raw.len == 0u)
            outcome_set_add(&raw,
                atom_symbol_id(a, g_builtin_syms.not_reducible), &env);
    }
    for (CettaCount i = 0u; i < raw.len && !eval_unwinding(); i++)
        outcome_set_add_existing_move(os, &raw.items[i]);
    outcome_set_free(&raw);
}

/* Raw instructions and delimiter resumptions use the same intrusive owned
 * stack as the need evaluator. Each frame completes its producer before
 * entering a consumer branch. Saved environments and pending occurrences are
 * registered for moving collection for the entire frame lifetime. */
typedef enum {
    HE_INSTRUCTION_RUN,
    HE_INSTRUCTION_RAW,
} HeInstructionKind;

typedef enum {
    HE_INSTRUCTION_ENTER,
    HE_INSTRUCTION_DONE,
    HE_INSTRUCTION_CHAIN,
    HE_INSTRUCTION_FUNCTION,
    HE_INSTRUCTION_COLLAPSE,
    HE_INSTRUCTION_RAW_RETURNS,
} HeInstructionPhase;

typedef struct {
    CettaOwnedFrameLink continuation;
    HeInstructionKind kind;
    HeInstructionPhase phase;
    Space *space;
    Arena *arena;
    Atom *atom;
    Atom *unused_type;
    Bindings env;
    OutcomeSet inner;
    OutcomeSet *target;
    CettaCount index;
    int fuel;
    EvalGcRootFrame lexical;
    EvalGcRootFrame pending;
} HeInstructionFrame;

static void he_instruction_push(CettaOwnedFrameStack *stack,
    HeInstructionKind kind, Space *space, Arena *arena, Atom *source,
    int fuel, const Bindings *prefix, OutcomeSet *target) {
    HeInstructionFrame *frame = cetta_malloc(sizeof(*frame));
    memset(frame, 0, sizeof(*frame));
    frame->kind = kind;
    frame->space = space;
    frame->arena = arena;
    frame->atom = source;
    frame->fuel = fuel;
    frame->target = target;
    bindings_init(&frame->env);
    outcome_set_init(&frame->inner);
    if (prefix && !bindings_copy(&frame->env, prefix)) {
        bindings_free(&frame->env);
        free(frame);
        eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);
        return;
    }
    eval_gc_root_frame_enter(&frame->lexical, &frame->atom,
                            &frame->env, &frame->unused_type);
    eval_gc_root_frame_suspend_precisely(&frame->lexical);
    eval_gc_root_frame_enter_outcome_continuation(
        &frame->pending, NULL, 0u, &frame->inner, target);
    cetta_owned_frame_push(stack, &frame->continuation);
}

static void he_instruction_pop(CettaOwnedFrameStack *stack) {
    HeInstructionFrame *frame = (HeInstructionFrame *)
        cetta_owned_frame_pop(stack);
    eval_gc_root_frame_leave(&frame->pending);
    eval_gc_root_frame_leave(&frame->lexical);
    outcome_set_free(&frame->inner);
    bindings_free(&frame->env);
    free(frame);
}

static void he_instruction_resume(CettaOwnedFrameStack *stack) {
    HeInstructionFrame *frame = (HeInstructionFrame *)stack->top;
    Space *s = frame->space;
    Arena *a = frame->arena;
    OutcomeSet *os = frame->target;
    int child_fuel = frame->fuel > 0 ? frame->fuel - 1 : frame->fuel;
    if (frame->phase == HE_INSTRUCTION_DONE) {
        he_instruction_pop(stack);
        return;
    }
    if (frame->phase == HE_INSTRUCTION_RAW_RETURNS ||
        frame->phase == HE_INSTRUCTION_CHAIN ||
        frame->phase == HE_INSTRUCTION_FUNCTION) {
        while (frame->index < frame->inner.len && !eval_unwinding()) {
            Outcome *item = &frame->inner.items[frame->index++];
            Atom *result = outcome_atom_materialize(a, item);
            if (!result)
                continue;
            if (frame->phase == HE_INSTRUCTION_RAW_RETURNS) {
                if (atom_head_symbol_id(result) == g_builtin_syms.function) {
                    he_instruction_push(stack, HE_INSTRUCTION_RUN, s, a,
                        result, frame->fuel, &item->env, os);
                    return;
                }
                outcome_set_add(os, result, &item->env);
            } else if (frame->phase == HE_INSTRUCTION_CHAIN) {
                Atom *body = bindings_apply_if_vars(
                    &item->env, a, expr_arg(frame->atom, 2));
                body = instantiate_template_variable_once(a, body,
                    expr_arg(frame->atom, 1)->var_id, result);
                he_instruction_push(stack, HE_INSTRUCTION_RUN, s, a,
                    body, child_fuel, &item->env, os);
                return;
            } else if (atom_head_symbol_id(result) == g_builtin_syms.return_text &&
                       result->expr.len == 2u) {
                outcome_set_add(os, result->expr.elems[1], &item->env);
            } else if (he_minimal_instruction(result)) {
                Atom *body = atom_expr2(a, atom_symbol(a, "function"), result);
                he_instruction_push(stack, HE_INSTRUCTION_RUN, s, a,
                    body, child_fuel, &item->env, os);
                return;
            } else if (active_profile_uses_rust_he_compat_semantics()) {
                outcome_set_add(os, atom_var_with_id(
                    a, "result", var_epoch_id(1, 17)), &item->env);
            } else {
                outcome_set_add(os, atom_error(a, frame->atom,
                    atom_symbol(a, "NoReturn")), &item->env);
            }
        }
        he_instruction_pop(stack);
        return;
    }
    if (frame->phase == HE_INSTRUCTION_COLLAPSE) {
        Atom **pairs = arena_alloc(a, sizeof(*pairs) *
            (frame->inner.len ? frame->inner.len : 1u));
        CettaExprLen count = 0u;
        for (CettaCount i = 0u; i < frame->inner.len; i++) {
            Outcome *item = &frame->inner.items[i];
            Atom *result = outcome_atom_materialize(a, item);
            if (!result || atom_is_legacy_empty_sentinel(result))
                continue;
            pairs[count++] = atom_expr2(a, result,
                bindings_capture_value(a, &item->env));
        }
        outcome_set_add(os, atom_expr(a, pairs, count), &frame->env);
        he_instruction_pop(stack);
        return;
    }
    if (frame->kind == HE_INSTRUCTION_RUN) {
        if (!eval_completion_step()) {
            he_instruction_pop(stack);
            return;
        }
        if (frame->fuel == 0) {
            eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_FUEL);
            he_instruction_pop(stack);
            return;
        }
    }
    frame->atom = resolve_registry_refs(a,
        bindings_apply_if_vars(&frame->env, a, frame->atom));
    Atom *atom = frame->atom;
    SymbolId head = atom_head_symbol_id(atom);
    CettaExprLen nargs = atom->kind == ATOM_EXPR && atom->expr.len
        ? expr_nargs(atom) : 0u;
    if (frame->kind == HE_INSTRUCTION_RAW) {
        if (he_minimal_instruction(atom)) {
            frame->phase = HE_INSTRUCTION_DONE;
            he_instruction_push(stack, HE_INSTRUCTION_RUN, s, a,
                atom, frame->fuel, &frame->env, os);
        } else {
            he_raw_collect(s, a, atom, frame->fuel, &frame->env, &frame->inner);
            frame->phase = HE_INSTRUCTION_RAW_RETURNS;
        }
        return;
    }
    if ((head == g_builtin_syms.eval && nargs == 1u) ||
        (head == g_builtin_syms.evalc && nargs == 2u)) {
        Space *target = s;
        if (head == g_builtin_syms.evalc)
            target = g_registry ? resolve_single_space_arg(
                s, a, expr_arg(atom, 1), child_fuel) : NULL;
        frame->phase = HE_INSTRUCTION_DONE;
        if (target)
            he_instruction_push(stack, HE_INSTRUCTION_RAW, target, a,
                expr_arg(frame->atom, 0), child_fuel, &frame->env, os);
        else
            outcome_set_add(os, call_signature_error(a, frame->atom,
                "(evalc <atom> <space>)"), &frame->env);
    } else if (head == g_builtin_syms.metta && nargs == 3u && g_registry) {
        Space *target = resolve_single_space_arg(
            s, a, expr_arg(atom, 2), child_fuel);
        if (target)
            eval_for_caller(target, a, expr_arg(frame->atom, 1),
                expr_arg(frame->atom, 0), child_fuel, &frame->env, true, os);
        else
            outcome_set_add(os, call_signature_error(a, frame->atom,
                "(metta <atom> <type> <space>)"), &frame->env);
        frame->phase = HE_INSTRUCTION_DONE;
    } else if (head == g_builtin_syms.context_space && nargs == 0u) {
        outcome_set_add(os, atom_space(a, s), &frame->env);
        frame->phase = HE_INSTRUCTION_DONE;
    } else if (head == g_builtin_syms.call_native) {
        outcome_set_add(os, call_signature_error(a, atom,
            "(call-native func args)"), &frame->env);
        frame->phase = HE_INSTRUCTION_DONE;
    } else if (head == g_builtin_syms.chain && nargs == 3u &&
               expr_arg(atom, 1)->kind == ATOM_VAR) {
        frame->phase = HE_INSTRUCTION_CHAIN;
        he_instruction_push(stack, HE_INSTRUCTION_RUN, s, a,
            expr_arg(atom, 0), child_fuel, &frame->env, &frame->inner);
    } else if (head == g_builtin_syms.function && nargs == 1u &&
               expr_arg(atom, 0)->kind == ATOM_EXPR) {
        frame->phase = HE_INSTRUCTION_FUNCTION;
        Atom *body = expr_arg(atom, 0);
        if (!he_minimal_instruction(body) &&
            atom_head_symbol_id(body) != g_builtin_syms.return_text)
            eval_for_caller(s, a, NULL, body, child_fuel,
                &frame->env, true, &frame->inner);
        else
            he_instruction_push(stack, HE_INSTRUCTION_RUN, s, a,
                body, child_fuel, &frame->env, &frame->inner);
    } else if (head == g_builtin_syms.collapse_bind && nargs == 1u) {
        frame->phase = HE_INSTRUCTION_COLLAPSE;
        he_instruction_push(stack, HE_INSTRUCTION_RUN, s, a,
            expr_arg(atom, 0), child_fuel, &frame->env, &frame->inner);
    } else if (head == g_builtin_syms.unify && nargs == 4u) {
        Bindings matched;
        bindings_init(&matched);
        bool success = match_atoms(expr_arg(atom, 0), expr_arg(atom, 1), &matched, a);
        if (success) {
            Bindings merged;
            const Bindings *effective = NULL;
            if (bindings_effective_merge(&merged, &effective, &frame->env, &matched, true)) {
                Atom *result = bindings_apply_if_vars(effective, a, expr_arg(atom, 2));
                outcome_set_add(os, result, effective ? effective : &frame->env);
                if (effective == &merged) bindings_free(&merged);
            }
        } else {
            outcome_set_add(os, expr_arg(atom, 3), &frame->env);
        }
        bindings_free(&matched);
    } else if (head == g_builtin_syms.cons_atom && nargs == 2u) {
        Atom *tail = expr_arg(atom, 1);
        Atom *const *elems;
        CettaExprLen len;
        if (atom_sequence_view(tail, &elems, &len)) {
            Atom **result = arena_alloc(a, sizeof(*result) * (len + 1u));
            result[0] = expr_arg(atom, 0);
            for (CettaExprIndex i = 0u; i < len; i++) result[i + 1u] = elems[i];
            outcome_set_add(os, atom_sequence_like(a, tail, result, len + 1u), &frame->env);
        } else {
            outcome_set_add(os, call_signature_error(
                a, atom, "(cons-atom <head> (: <tail> Expression))"), &frame->env);
        }
    } else if (head == g_builtin_syms.decons_atom && nargs == 1u) {
        Atom *input = expr_arg(atom, 0);
        Atom *const *elems;
        CettaExprLen len;
        if (atom_sequence_view(input, &elems, &len) && len > 0u) {
            Atom *tail = atom_sequence_like(a, input, elems + 1u, len - 1u);
            outcome_set_add(os, atom_expr2(a, elems[0], tail), &frame->env);
        } else {
            outcome_set_add(os, call_signature_error(
                a, atom, "(decons-atom (: <expr> Expression))"), &frame->env);
        }
    } else if (head == g_builtin_syms.superpose_bind && nargs == 1u) {
        Atom *list = expr_arg(atom, 0);
        if (list->kind == ATOM_EXPR) {
            for (CettaExprIndex i = 0u; i < list->expr.len; i++) {
                Atom *pair = list->expr.elems[i];
                if (pair->kind != ATOM_EXPR || pair->expr.len != 2u) continue;
                Bindings restored;
                if (!bindings_from_atom_scoped(pair->expr.elems[1], pair->expr.elems[0],
                                               &frame->env, &restored)) continue;
                outcome_set_add_prefixed(a, os, pair->expr.elems[0],
                                        &restored, &frame->env, true);
                bindings_free(&restored);
            }
        } else {
            outcome_set_add(os, call_signature_error(
                a, atom, "(superpose-bind (: <alternatives> Expression))"), &frame->env);
        }
    } else if (he_minimal_instruction(atom) && head != g_builtin_syms.return_text) {
        outcome_set_add(os, atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")), &frame->env);
    } else {
        outcome_set_add(os, atom, &frame->env);
    }

    if (stack->top == &frame->continuation &&
        frame->phase == HE_INSTRUCTION_ENTER)
        he_instruction_pop(stack);
}

static void he_instruction_run(HeInstructionKind kind, Space *s, Arena *a,
    Atom *atom, int fuel, const Bindings *prefix, OutcomeSet *os) {
    CettaOwnedFrameStack stack = {0};
    he_instruction_push(&stack, kind, s, a, atom, fuel, prefix, os);
    while (stack.top && !eval_unwinding() && !eval_cancel_check())
        he_instruction_resume(&stack);
    while (stack.top)
        he_instruction_pop(&stack);
}

static void he_minimal_run(Space *s, Arena *a, Atom *atom, int fuel,
    const Bindings *prefix, OutcomeSet *os) {
    he_instruction_run(HE_INSTRUCTION_RUN, s, a, atom, fuel, prefix, os);
}

static void he_raw_invoke(Space *s, Arena *a, Atom *atom, int fuel,
    const Bindings *prefix, OutcomeSet *os) {
    he_instruction_run(HE_INSTRUCTION_RAW, s, a, atom, fuel, prefix, os);
}

static bool he_eval_typed_instruction(Space *s, Arena *a, Atom *atom, Atom *codomain,
                                   int fuel, const Bindings *prefix,
                                   bool preserve_bindings, bool allow_tail,
                                   Atom **tail_next, Atom **tail_type,
                                   Bindings *tail_env, OutcomeSet *os) {
    __attribute__((cleanup(bindings_free))) Bindings env;
    bindings_init(&env);
    if (prefix && !bindings_copy(&env, prefix)) {
        eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);
        return false;
    }
    __attribute__((cleanup(eval_gc_root_frame_leave)))
    EvalGcRootFrame lexical = {0};
    eval_gc_root_frame_enter(&lexical, &atom, &env, &codomain);
    OutcomeSet minimal;
    outcome_set_init(&minimal);
    __attribute__((cleanup(eval_gc_outcome_suspension_end)))
    EvalGcOutcomeSuspension suspension = {0};
    Atom *live[] = {atom, codomain};
    eval_gc_outcome_suspension_begin_with_atoms(
        &suspension, &lexical, os, &minimal, live, 2u);
    SymbolId head = atom_head_symbol_id(atom);
    if (head == g_builtin_syms.chain && expr_nargs(atom) == 3u &&
        expr_arg(atom, 1)->kind == ATOM_VAR &&
        !he_minimal_instruction(expr_arg(atom, 0)) &&
        !active_profile_uses_rust_he_compat_semantics()) {
        /* The public extended-HE chain also accepts an ordinary source
         * computation.  Minimal instructions inside a delimiter retain
         * their separate raw-source contract. */
        OutcomeSet sources;
        outcome_set_init(&sources);
        __attribute__((cleanup(eval_gc_root_frame_leave)))
        EvalGcRootFrame sources_root = {0};
        eval_gc_root_frame_enter_outcome_continuation(
            &sources_root, NULL, 0u, &sources, &minimal);
        eval_for_caller(s, a, NULL, expr_arg(live[0], 0), fuel,
                        &env, true, &sources);
        for (CettaCount i = 0u; i < sources.len && !eval_unwinding(); i++) {
            Atom *result = outcome_atom_materialize(a, &sources.items[i]);
            if (!result) continue;
            Atom *body = bindings_apply_if_vars(
                &sources.items[i].env, a, expr_arg(live[0], 2));
            body = instantiate_template_variable_once(
                a, body, expr_arg(live[0], 1)->var_id, result);
            he_minimal_run(s, a, body, fuel, &sources.items[i].env, &minimal);
        }
        outcome_set_free(&sources);
    } else if (head == g_builtin_syms.superpose) {
        he_raw_invoke(s, a, atom, fuel, &env, &minimal);
    } else {
        he_minimal_run(s, a, atom, fuel, &env, &minimal);
    }
    /* A single continuation uses the existing evaluator tail carrier after
     * applicability and raw invocation. It keeps deep public instructions
     * off the C stack without giving them a separate execution path. */
    if (allow_tail && minimal.len == 1u && !eval_unwinding()) {
        Atom *result = outcome_atom_materialize(a, &minimal.items[0]);
        if (result && !atom_is_legacy_empty_sentinel(result) &&
            !he_not_reducible(result) &&
            /* A returned function is a fresh public invocation. Its own
             * Atom codomain must be acquired before interpreting its body;
             * the generic tail carrier instead treats it as a delimiter. */
            atom_head_symbol_id(result) != g_builtin_syms.function &&
            eval_atom_type_demand(a, result, live[1]) == HE_TYPE_INTERPRET) {
            bool copied = bindings_copy(tail_env, &minimal.items[0].env);
            if (copied) {
                *tail_next = result;
                *tail_type = live[1];
            } else {
                eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);
            }
            outcome_set_free(&minimal);
            return copied;
        }
    }
    for (CettaCount i = 0u; i < minimal.len && !eval_unwinding(); i++) {
        Atom *result = outcome_atom_materialize(a, &minimal.items[i]);
        if (!result || atom_is_legacy_empty_sentinel(result)) continue;
        if (he_not_reducible(result)) {
            if (active_profile_uses_rust_he_compat_semantics() ||
                (head != g_builtin_syms.eval && head != g_builtin_syms.evalc))
                result = live[0];
            else
                result = expr_arg(live[0], 0);
            outcome_set_add(os, result, &minimal.items[i].env);
        } else {
            eval_for_caller(s, a, live[1],
                            result, fuel, &minimal.items[i].env,
                            preserve_bindings, os);
        }
    }
    /* This is the full interpreter's completed frontier. Zero remains zero;
     * raw minimal instructions retain their own internal Empty events. */
    outcome_set_free(&minimal);
    return false;
}

static void he_eval_invocation_delimiter(Space *s, Arena *a, Atom *atom,
                                        Atom *type, int fuel,
                                        const Bindings *prefix,
                                        bool preserve_bindings, OutcomeSet *os) {
    __attribute__((cleanup(bindings_free))) Bindings env;
    bindings_init(&env);
    if (prefix && !bindings_copy(&env, prefix)) {
        eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);
        return;
    }
    __attribute__((cleanup(eval_gc_root_frame_leave)))
    EvalGcRootFrame lexical = {0};
    eval_gc_root_frame_enter(&lexical, &atom, &env, &type);
    OutcomeSet returned;
    outcome_set_init(&returned);
    __attribute__((cleanup(eval_gc_outcome_suspension_end)))
    EvalGcOutcomeSuspension suspension = {0};
    Atom *live[] = {atom, type};
    eval_gc_outcome_suspension_begin_with_atoms(
        &suspension, &lexical, os, &returned, live, 2u);
    he_minimal_run(s, a, atom, fuel, &env, &returned);
    for (CettaCount i = 0u; i < returned.len && !eval_unwinding(); i++) {
        Atom *result = outcome_atom_materialize(a, &returned.items[i]);
        if (he_not_reducible(result))
            outcome_set_add(os, result, &returned.items[i].env);
        else if (result)
            eval_for_caller(s, a, live[1], result, fuel,
                            &returned.items[i].env, preserve_bindings, os);
    }
    outcome_set_free(&returned);
}
