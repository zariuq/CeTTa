/* Minimal instructions consume raw invocation results, not the ordinary
 * interpreter's return-type continuation.  The instruction protocol remains
 * local: a raw RHS is data unless it is a function delimiter, and a function
 * consumes a return-shaped value without interpreting the payload. */
static bool he_minimal_instruction_head(SymbolId head) {
    return head == g_builtin_syms.eval || head == g_builtin_syms.evalc ||
           head == g_builtin_syms.chain || head == g_builtin_syms.function ||
           head == g_builtin_syms.unify || head == g_builtin_syms.metta ||
           head == g_builtin_syms.context_space || head == g_builtin_syms.call_native ||
           head == g_builtin_syms.cons_atom || head == g_builtin_syms.decons_atom ||
           head == g_builtin_syms.collapse_bind || head == g_builtin_syms.superpose_bind;
}

static bool he_minimal_instruction(Atom *atom) {
    return he_minimal_instruction_head(atom_head_symbol_id(atom));
}

static bool he_not_reducible(Atom *atom) {
    return atom_is_symbol_id(atom, g_builtin_syms.not_reducible);
}

/* Only raw invocation results cross this boundary. Completed native values
 * and held operands retain their own provenance and never call it. */
static Atom *he_call_return_atom(Atom *invocation, Atom *raw_result) {
    return invocation && he_not_reducible(raw_result) ? invocation : raw_result;
}

static bool he_call_result_needs_invocation(Atom *result) {
    return he_not_reducible(result) ||
        atom_head_symbol_id(result) == g_builtin_syms.function;
}

static bool he_source_has_return_protocol(const Atom *atom, void *context) {
    (void)context;
    return atom && atom->kind == ATOM_SYMBOL &&
        atom_symbol_current_id(atom) == g_builtin_syms.not_reducible;
}

/* The minimal protocol admits these source controls. Other HE operations
 * keep their own context, typing and raw-invocation policies. */
static CettaControlOperationPlan he_instruction_control_plan(Atom *atom) {
    SymbolId head = atom_head_symbol_id(atom);
    CettaExprLen arity = atom && atom->kind == ATOM_EXPR && atom->expr.len
        ? expr_nargs(atom) : 0u;
    if (head == g_builtin_syms.collapse_bind && arity == 1u)
        head = g_builtin_syms.collapse;
    else if (head != g_builtin_syms.eval && head != g_builtin_syms.chain &&
             head != g_builtin_syms.function)
        return (CettaControlOperationPlan){0};
    CettaControlOperationPlan plan = {0};
    cetta_control_operation_decode(head, arity, &plan, &g_builtin_syms);
    return plan;
}

static bool he_instruction_control_valid(CettaControlOperationPlan plan,
                                         CettaExprLen arity) {
    if (plan.operation == CETTA_CONTROL_NONE)
        return true;
    if (plan.operation == CETTA_CONTROL_SEQUENCE)
        return arity == 3u && plan.source < arity && plan.first < arity &&
            plan.last < arity && plan.source != plan.first &&
            plan.source != plan.last && plan.first != plan.last;
    return (plan.operation == CETTA_CONTROL_FORCE ||
            plan.operation == CETTA_CONTROL_RETURN_SCOPE ||
            plan.operation == CETTA_CONTROL_COLLECT) &&
        arity == 1u && plan.source == 0u &&
        plan.first == CETTA_CONTROL_OPERAND_ABSENT &&
        plan.last == CETTA_CONTROL_OPERAND_ABSENT;
}

static Atom *he_instruction_refusal_readback(Atom *syntax, Atom *carrier,
                                             CettaControlOperationPlan control) {
    if (active_profile_uses_rust_he_compat_semantics())
        return syntax;
    if (control.operation == CETTA_CONTROL_FORCE)
        return expr_arg(carrier, control.source);
    if (control.operation == CETTA_CONTROL_NONE &&
        atom_head_symbol_id(syntax) == g_builtin_syms.evalc && expr_nargs(syntax) == 2u)
        return expr_arg(syntax, 0u);
    return syntax;
}

/* The public instruction boundary removes raw Empty events; a delimiter
 * keeps them for ordinary completion. NotReducible restores the saved
 * source image. A returned function must acquire its own public codomain
 * before its body runs, so it cannot use the generic tail carrier. */
typedef enum {
    HE_INSTRUCTION_OMIT,
    HE_INSTRUCTION_REFUSAL,
    HE_INSTRUCTION_INTERPRET,
    HE_INSTRUCTION_TAIL,
} HeInstructionCompletion;

static HeInstructionCompletion he_instruction_completion(Arena *arena,
    Atom *value, Atom *expected, bool public_instruction, bool allow_tail) {
    if (!value || (public_instruction && atom_is_legacy_empty_sentinel(value)))
        return HE_INSTRUCTION_OMIT;
    if (he_not_reducible(value))
        return HE_INSTRUCTION_REFUSAL;
    if (allow_tail && atom_head_symbol_id(value) != g_builtin_syms.function &&
        eval_atom_type_demand(arena, value, expected) == HE_TYPE_INTERPRET)
        return HE_INSTRUCTION_TAIL;
    return HE_INSTRUCTION_INTERPRET;
}

/* (chain <nested> $v <template>) binds $v in <template>, and function and
 * collapse-bind carry code.  An environment reaches a chain's nested
 * computation at once and its template only after the selected result has
 * replaced $v, so an outer binding of the destination's variable never
 * reaches the destination's occurrences.  Every other atom takes the
 * environment as it stands. */
static Atom *he_instruction_apply_env(Arena *a, const Bindings *env,
                                      Atom *atom, CettaControlOperationPlan control);

static Atom *he_instruction_source_env(Arena *a, const Bindings *env, Atom *atom) {
    CettaControlOperationPlan control = atom_has_vars(atom)
        ? he_instruction_control_plan(atom) : (CettaControlOperationPlan){0};
    return he_instruction_apply_env(a, env, atom, control);
}

static Atom *he_instruction_apply_env(Arena *a, const Bindings *env,
                                      Atom *atom, CettaControlOperationPlan control) {
    CettaExprLen arity = atom && atom->kind == ATOM_EXPR && atom->expr.len
        ? expr_nargs(atom) : 0u;
    if (!he_instruction_control_valid(control, arity))
        return NULL;
    if (!atom || atom->kind != ATOM_EXPR || !atom_has_vars(atom))
        return bindings_apply_if_vars(env, a, atom);
    if (control.operation == CETTA_CONTROL_SEQUENCE &&
        expr_arg(atom, control.first)->kind == ATOM_VAR) {
        Atom *nested = he_instruction_source_env(a, env, expr_arg(atom, control.source));
        if (!nested)
            return NULL;
        if (nested == expr_arg(atom, control.source))
            return atom;
        Atom *elems[] = {atom->expr.elems[0], atom->expr.elems[1],
                         atom->expr.elems[2], atom->expr.elems[3]};
        elems[(CettaExprIndex)control.source + 1u] = nested;
        return atom_expr(a, elems, 4u);
    }
    if (control.operation == CETTA_CONTROL_RETURN_SCOPE ||
        control.operation == CETTA_CONTROL_COLLECT) {
        Atom *body = he_instruction_source_env(a, env, expr_arg(atom, control.source));
        if (!body)
            return NULL;
        return body == expr_arg(atom, control.source)
            ? atom : atom_expr2(a, atom->expr.elems[0], body);
    }
    return bindings_apply_if_vars(env, a, atom);
}

static void he_minimal_run(Space *s, Arena *a, Atom *atom, int fuel,
                           const Bindings *prefix, OutcomeSet *os);

typedef struct {
    Arena *arena;
    const Bindings *prefix;
    OutcomeSet *outcomes;
} HeRawQuery;

/* These host readers are grounded producers in the Rust compatibility
 * profile. Reading does not complete their ordinary invocation: a caller
 * owns that return stage. Minimal `eval` uses this same read without it. */
static bool he_compat_host_read_raw(Space *s, Arena *a, Atom *invocation,
    const Bindings *env, const Bindings *outer, OutcomeSet *raw,
    Atom **declared_type) {
    if (!active_profile_uses_rust_he_compat_semantics() ||
        !invocation || invocation->kind != ATOM_EXPR ||
        invocation->expr.len != 2u)
        return false;
    SymbolId head = atom_head_symbol_id(invocation);
    if (head != g_builtin_syms.get_state && head != g_builtin_syms.get_atoms)
        return false;
    Atom *argument = resolve_registry_refs(a, expr_arg(invocation, 0u));
    CettaCount first = raw->len;
    *declared_type = atom_undefined_type(a);
    if (head == g_builtin_syms.get_state) {
        if (argument->kind == ATOM_GROUNDED && argument->ground.gkind == GV_STATE) {
            StateCell *cell = eval_resolve_state_read(argument->ground.ptr);
            outcome_set_add_prefixed(a, raw, payload_rebind_resources(a, cell->value),
                env, outer, true);
        } else {
            outcome_set_add_prefixed(a, raw,
                space_arg_error(a, invocation,
                    "get-state expects single state atom as an argument"),
                env, outer, true);
        }
    } else {
        *declared_type = atom_symbol_id(a, g_builtin_syms.atom);
        if (argument->kind != ATOM_GROUNDED || argument->ground.gkind != GV_SPACE) {
            outcome_set_add_prefixed(a, raw, space_arg_error(a, invocation,
                "get-atoms expects a space as its argument"), env, outer, true);
        } else {
            PettaSpaceAtomSnapshot snapshot = {0};
            PettaSpaceAtomSnapshotStatus status = petta_space_atom_snapshot_capture(
                a, argument->ground.ptr, &snapshot);
            if (status != PETTA_SPACE_ATOM_SNAPSHOT_OK) {
                outcome_set_add_prefixed(a, raw,
                    status == PETTA_SPACE_ATOM_SNAPSHOT_MATERIALIZE_FAILED
                        ? space_backend_or_symbol_error(a, invocation,
                            "AttachedCompiledSpaceMaterializeFailed")
                        : atom_error(a, invocation, atom_symbol(a, "OutOfMemory")),
                    env, outer, true);
            } else if (!snapshot.len) {
                outcome_set_add_prefixed(a, raw, legacy_empty_sentinel(a), env, outer, true);
            } else {
                for (CettaCount i = 0u; i < snapshot.len; i++)
                    outcome_set_add_prefixed(a, raw,
                        cetta_instantiate_frame_syntax(a, snapshot.items[i]), env, outer, true);
            }
        }
    }
    for (CettaCount i = first; i < raw->len; i++)
        raw->items[i].result_form = CETTA_CALL_RESULT_EXPRESSION;
    return true;
}

/* Rust narrows a query match so the class name is the query variable.
 * The native match stores that variable on the first rule variable and
 * chains each later rule variable, so the instantiated term shows the
 * private terminal. One query variable resolved to that terminal is the
 * name to publish. Two query variables on one terminal stay as matched.
 * An eligible variable may be published; with no eligibility list, every
 * bound variable is. */
typedef struct {
    VarId query_id;
    SymbolId spelling;
    Atom *name_key;
    VarId terminal_id;
    Atom *value;
    bool eligible;
    bool ambiguous;
} HeQueryRepresentative;

static bool he_var_id_listed(const VarId *ids, uint32_t count, VarId id) {
    for (uint32_t i = 0u; i < count; i++)
        if (var_id_eq(ids[i], id))
            return true;
    return false;
}

static bool he_query_representative_collect(Arena *arena, const Bindings *bindings,
                                            const VarId *eligible_ids,
                                            uint32_t eligible_count,
                                            HeQueryRepresentative **out,
                                            uint32_t *count_out) {
    BindingsIterator iterator = {.bindings = bindings};
    Binding binding;
    HeQueryRepresentative *reps = NULL;
    uint32_t count = 0u;
    uint32_t cap = 0u;
    *out = NULL;
    *count_out = 0u;
    while (bindings_iterator_next(&iterator, &binding)) {
        uint32_t slot = count;
        Atom *value = binding_value_materialize(arena, binding.value);
        if (!value)
            goto fail;
        for (uint32_t i = 0u; i < count; i++) {
            if (var_id_eq(reps[i].query_id, binding.var_id)) {
                slot = i;
                break;
            }
        }
        if (slot == count) {
            if (count == cap) {
                uint32_t next_cap = cap ? cap * 2u : 8u;
                HeQueryRepresentative *grown = realloc(
                    reps, sizeof(HeQueryRepresentative) * (size_t)next_cap);
                if (!grown)
                    goto fail;
                reps = grown;
                cap = next_cap;
            }
            count++;
        }
        reps[slot].query_id = binding.var_id;
        reps[slot].spelling = binding.spelling;
        reps[slot].name_key = binding.name_key;
        reps[slot].value = value;
        reps[slot].terminal_id =
            value->kind == ATOM_VAR ? value->var_id : VAR_ID_NONE;
        reps[slot].eligible = !eligible_ids ||
            he_var_id_listed(eligible_ids, eligible_count, binding.var_id);
        reps[slot].ambiguous = false;
    }
    for (uint32_t i = 0u; i < count; i++) {
        if (var_id_is_none(reps[i].terminal_id) || !reps[i].eligible)
            continue;
        for (uint32_t j = 0u; j < count; j++) {
            if (i != j && reps[j].eligible &&
                var_id_eq(reps[j].terminal_id, reps[i].terminal_id))
                reps[i].ambiguous = true;
        }
    }
    *out = reps;
    *count_out = count;
    return true;
fail:
    free(reps);
    return false;
}

static Atom *he_rewrite_query_representative(Arena *arena, Atom *atom,
                                             const HeQueryRepresentative *reps,
                                             uint32_t count) {
    if (!atom || count == 0u || !atom_has_vars(atom))
        return atom;
    if (atom->kind == ATOM_VAR) {
        const HeQueryRepresentative *found = NULL;
        for (uint32_t i = 0u; i < count; i++) {
            if (!reps[i].eligible || reps[i].ambiguous ||
                var_id_is_none(reps[i].terminal_id) ||
                !var_id_eq(reps[i].terminal_id, atom->var_id))
                continue;
            if (found)
                return atom;
            found = &reps[i];
        }
        if (!found || var_id_eq(found->query_id, atom->var_id))
            return atom;
        return atom_var_with_presentation(
            arena, found->spelling, found->name_key, found->query_id);
    }
    if (atom->kind != ATOM_EXPR)
        return atom;
    Atom **rewritten = NULL;
    for (CettaExprIndex i = 0u; i < atom->expr.len; i++) {
        Atom *next = he_rewrite_query_representative(
            arena, atom->expr.elems[i], reps, count);
        if (!next)
            return NULL;
        if (!rewritten && next != atom->expr.elems[i]) {
            rewritten = arena_alloc(arena, sizeof(Atom *) * atom->expr.len);
            if (!rewritten)
                return NULL;
            for (CettaExprIndex j = 0u; j < i; j++)
                rewritten[j] = atom->expr.elems[j];
        }
        if (rewritten)
            rewritten[i] = next;
    }
    return rewritten ? atom_expr(arena, rewritten, atom->expr.len) : atom;
}

/* One query variable resolved to a terminal is the name to publish.
 * Two query variables on one terminal stay as matched. False is an
 * allocation failure; `atom` and `env` stay put when nothing moves. */
static bool he_orient_query_representative_among(
    Arena *arena, Atom **atom, Bindings *env, bool *changed_out,
    const VarId *eligible_ids, uint32_t eligible_count);

static bool he_orient_query_representative(Arena *arena, Atom **atom,
                                          Bindings *env, bool *changed_out) {
    return he_orient_query_representative_among(
        arena, atom, env, changed_out, NULL, 0u);
}

static bool he_orient_query_representative_among(
    Arena *arena, Atom **atom, Bindings *env, bool *changed_out,
    const VarId *eligible_ids, uint32_t eligible_count) {
    HeQueryRepresentative *reps = NULL;
    Atom **values = NULL;
    uint32_t count = 0u;
    Atom *shown;
    bool changed = false;
    if (changed_out)
        *changed_out = false;
    if (!active_profile_uses_rust_he_compat_semantics() ||
        !arena || !atom || !*atom || !env || !bindings_has_bound_values(env))
        return true;
    if (!he_query_representative_collect(arena, env, eligible_ids,
                                         eligible_count, &reps, &count))
        return false;
    shown = he_rewrite_query_representative(arena, *atom, reps, count);
    if (!shown)
        goto fail;
    changed = shown != *atom;
    if (count != 0u) {
        values = calloc((size_t)count, sizeof(Atom *));
        if (!values)
            goto fail;
        for (uint32_t i = 0u; i < count; i++) {
            values[i] = he_rewrite_query_representative(
                arena, reps[i].value, reps, count);
            if (!values[i])
                goto fail;
            if (values[i] != reps[i].value)
                changed = true;
        }
    }
    if (!changed) {
        free(values);
        free(reps);
        return true;
    }
    {
        Bindings published;
        bool ok = true;
        bindings_init(&published);
        for (uint32_t i = 0u; ok && i < count; i++) {
            if (values[i]->kind == ATOM_VAR &&
                var_id_eq(values[i]->var_id, reps[i].query_id))
                continue;
            Atom *key = atom_var_with_presentation(
                arena, reps[i].spelling, reps[i].name_key, reps[i].query_id);
            ok = key && bindings_add_var(&published, key, values[i]);
        }
        if (ok && env->constraints) {
            for (uint32_t i = 0u; ok && i < env->eq_len; i++) {
                Atom *lhs = binding_value_materialize(
                    arena, env->constraints[i].lhs);
                Atom *rhs = binding_value_materialize(
                    arena, env->constraints[i].rhs);
                lhs = lhs ? he_rewrite_query_representative(
                    arena, lhs, reps, count) : NULL;
                rhs = rhs ? he_rewrite_query_representative(
                    arena, rhs, reps, count) : NULL;
                ok = lhs && rhs && bindings_add_constraint(&published, lhs, rhs);
            }
        }
        if (!ok) {
            bindings_free(&published);
            goto fail;
        }
        *atom = shown;
        bindings_replace(env, &published);
        if (changed_out)
            *changed_out = true;
    }
    free(values);
    free(reps);
    return true;
fail:
    free(values);
    free(reps);
    return false;
}

typedef struct {
    VarId *ids;
    uint32_t count;
    uint32_t cap;
    bool failed;
} HeVarIdList;

static bool he_var_id_list_visit(const Atom *atom, void *context) {
    HeVarIdList *list = context;
    if (!atom || atom->kind != ATOM_VAR ||
        he_var_id_listed(list->ids, list->count, atom->var_id))
        return false;
    if (list->count == list->cap) {
        uint32_t next = list->cap ? list->cap * 2u : 8u;
        VarId *grown = realloc(list->ids, sizeof(*grown) * (size_t)next);
        if (!grown) {
            list->failed = true;
            return true;
        }
        list->ids = grown;
        list->cap = next;
    }
    list->ids[list->count++] = atom->var_id;
    return false;
}

/* Rust's call return keeps the caller's variables (apply_and_retain): a
 * variable the callee introduced that stands for a class holding exactly
 * one variable of the invocation is published as that variable, and the
 * invocation variable's binding to it is dropped. The invocation is read
 * as the caller holds it, through `caller_env` when one is given. Other
 * answers are unchanged. False is an allocation failure. */
static bool he_return_retain_invocation(Arena *arena, Atom *invocation,
                                        const Bindings *caller_env,
                                        Atom **atom, Bindings *env) {
    if (!active_profile_uses_rust_he_compat_semantics() || !arena ||
        !invocation || !atom || !*atom || !env ||
        !atom_has_vars(invocation) || !bindings_has_bound_values(env))
        return true;
    HeVarIdList list = {0};
    (void)atom_tree_any(invocation, he_var_id_list_visit, &list);
    if (!list.failed && caller_env && bindings_has_bound_values(caller_env)) {
        Atom *held = bindings_apply_if_vars(caller_env, arena, invocation);
        if (held && held != invocation)
            (void)atom_tree_any(held, he_var_id_list_visit, &list);
    }
    bool ok = !list.failed &&
        (list.count == 0u ||
         he_orient_query_representative_among(
             arena, atom, env, NULL, list.ids, list.count));
    free(list.ids);
    return ok;
}

/* The same return stage for an outcome a caller receives. */
static bool he_return_retain_invocation_outcome(Arena *arena,
                                                Atom *invocation,
                                                const Bindings *caller_env,
                                                Outcome *out) {
    if (!out || out->kind != CETTA_OUTCOME_INLINE || !out->atom ||
        variant_instance_present(&out->variant) ||
        !active_profile_uses_rust_he_compat_semantics())
        return true;
    Atom *shown = out->atom;
    if (!he_return_retain_invocation(arena, invocation, caller_env,
                                     &shown, &out->env))
        return false;
    if (shown != out->atom) {
        out->atom = shown;
        out->materialized_atom = NULL;
    }
    return true;
}

static bool he_raw_query_result(Atom *result, const Bindings *bindings,
                                void *context) {
    HeRawQuery *query = context;
    Atom *shown = result;
    Bindings published;
    bool changed = false;
    if (!active_profile_uses_rust_he_compat_semantics() ||
        !bindings || !bindings_has_bound_values(bindings)) {
        outcome_set_add_prefixed(query->arena, query->outcomes, result,
                                 bindings, query->prefix, true);
        return !eval_unwinding();
    }
    bindings_init(&published);
    if (!bindings_copy(&published, bindings) ||
        !he_orient_query_representative(query->arena, &shown, &published,
                                        &changed)) {
        bindings_free(&published);
        eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);
        return false;
    }
    outcome_set_add_prefixed(query->arena, query->outcomes,
                             changed ? shown : result,
                             changed ? &published : bindings,
                             query->prefix, true);
    bindings_free(&published);
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
    CettaControlOperationPlan control = {0};
    cetta_control_operation_decode(head, nargs, &control, &g_builtin_syms);
    bool invoked = false;
    CettaCallOutcome worker;
    if (prepared_minimal_worker_call(s, a, atom, &env, fuel, &worker)) {
        if (!cetta_call_outcome_shape_valid(worker)) {
            eval_mark_interrupted(CETTA_EVAL_INCOMPLETE_HOST_FAILURE);
            outcome_set_free(&raw);
            return;
        }
        if (worker.kind != CETTA_CALL_VALUE) {
            outcome_set_free(&raw);
            return;
        }
        Atom *prepared = prepared_minimal_worker_readback(a, worker);
        if (!prepared) {
            eval_mark_interrupted(CETTA_EVAL_INCOMPLETE_HOST_FAILURE);
            outcome_set_free(&raw);
            return;
        }
        outcome_set_add(&raw, prepared, &env);
        invoked = true;
    } else if (he_compat_host_read_raw(s, a, atom, &env, NULL, &raw,
                                      &unused_type)) {
        invoked = true;
    } else if (head == g_builtin_syms.get_type &&
               (nargs == 1u || nargs == 2u)) {
        Space *typed_space = s;
        Atom *error = NULL;
        if (nargs == 2u)
            typed_space = eval_get_type_space_argument(
                a, atom, expr_arg(atom, 1), &error);
        Atom **types = NULL;
        uint32_t count = 0u;
        CettaEvalCompletion completion = CETTA_EVAL_INCOMPLETE_HOST_FAILURE;
        if (!typed_space) {
            outcome_set_add(&raw, error, &env);
        } else if (!eval_get_type_intrinsic_answers(
                typed_space, a, expr_arg(atom, 0), NULL, fuel, &types, &count,
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
    } else if (control.operation == CETTA_CONTROL_ORDERED_CHOICE) {
        Atom *list = expr_arg(atom, control.source);
        Atom *const *elements;
        CettaExprLen count;
        if (!atom_sequence_view(list, &elements, &count)) {
            outcome_set_add(&raw, call_signature_error(
                a, atom, "(superpose (: <alternatives> Expression))"), &env);
        } else if (count == 0u) {
            outcome_set_add(&raw, legacy_empty_sentinel(a), &env);
        } else {
            Atom frontier = {.kind = ATOM_EXPR, .expr = {
                .elems = (Atom **)elements, .len = count}};
            CettaCallReturnCursor cursor = {
                .outcome = cetta_call_expression_frontier(&frontier),
                .environment = &env,
                .select_expressions = true,
            };
            CettaOwnedReturn returned;
            CettaEvalCompletion completion = CETTA_EVAL_COMPLETE;
            while (cetta_call_return_next(&cursor, &returned, &completion) ==
                    CETTA_OWNED_EXECUTION_RETURN)
                outcome_set_add(&raw, returned.outcome.term, returned.environment);
            if (completion != CETTA_EVAL_COMPLETE)
                eval_mark_incomplete(completion);
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
            prime_native_observe_space_read(target);
            if (!space_query_bindings(target, a, expr_arg(atom, 1), &env, &rows)) {
                binding_set_free(&rows);
                outcome_set_free(&raw);
                eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);
                return;
            }
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
    } else if (he_raw_case_collect(s, a, atom, fuel, &env, &raw)) {
        invoked = true;
    } else if (atom->kind == ATOM_EXPR && atom->expr.len > 0u) {
        Atom *op = materialize_runtime_token(s, a, atom->expr.elems[0]);
        invoked = dispatch_foreign_outcomes(
            s, a, op, atom->expr.elems + 1u, nargs, atom_symbol(a, "Atom"),
            fuel, &env, false, true, NULL, NULL, NULL, &raw);
        if (!invoked) {
            CettaCallOutcome outcome;
            invoked = dispatch_native_invocation(s, a,
                &(CettaInvocation){.head = binding_value_from_atom(op),
                    .supplied = {.atoms = atom->expr.elems + 1u, .count = nargs}},
                &outcome);
            if (invoked) {
                Atom *result = NULL;
                if (sequence_native_call(s, a, outcome, false,
                        atom_symbol(a, "Atom"), &env, os, &result)) {
                    outcome_set_free(&raw);
                    return;
                }
                if (result)
                    outcome_set_add(&raw, result, &env);
            }
        }
    }
    if (!invoked) {
        HeRawQuery query = {a, &env, &raw};
        query_equations_visit(s, atom, a, he_raw_query_result, &query);
        if (raw.len == 0u) {
            outcome_set_add(&raw,
                atom_symbol_id(a, g_builtin_syms.not_reducible), &env);
        }
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
    /* Named reduction that produced this function, when one is known. */
    Atom *function_caller;
    Atom *syntax;
    Bindings env;
    OutcomeSet inner;
    OutcomeSet *target;
    CettaCount index;
    int fuel;
    CettaControlOperationPlan control;
    EvalGcRootFrame lexical;
    EvalGcRootFrame pending;
} HeInstructionFrame;

static void he_instruction_push(CettaOwnedFrameStack *stack,
    HeInstructionKind kind, Space *space, Arena *arena, Atom *source,
    int fuel, const Bindings *prefix, OutcomeSet *target,
    const CettaControlOperationPlan *control, Atom *syntax) {
    HeInstructionFrame *frame = cetta_malloc(sizeof(*frame));
    memset(frame, 0, sizeof(*frame));
    frame->kind = kind;
    frame->space = space;
    frame->arena = arena;
    frame->atom = source;
    frame->syntax = syntax ? syntax : source;
    if (control)
        frame->control = *control;
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
                            &frame->env, &frame->syntax);
    eval_gc_root_frame_suspend_precisely(&frame->lexical);
    eval_gc_root_frame_enter_outcome_continuation(
        &frame->pending, &frame->function_caller, 1u, &frame->inner, target);
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

/* Minimal function_ret: a caller frame's atom is the witness. A bare
 * function finishes on the body atom. The type-directed $result frontier
 * is not minted here. */
static Atom *he_function_noreturn_witness(HeInstructionFrame *frame,
                                         Atom *finished) {
    if (frame->function_caller && frame->function_caller != frame->atom)
        return frame->function_caller;
    if (frame->continuation.next) {
        HeInstructionFrame *caller =
            (HeInstructionFrame *)frame->continuation.next;
        if (caller->atom && caller->atom != frame->atom)
            return caller->atom;
    }
    return finished;
}

/* One variable in the matched substitution. `left_order` is its first
 * appearance in the unify left operand, or UINT32_MAX when it does not
 * occur there. Class fields are meaningful on the union-find root. */
typedef struct {
    VarId id;
    SymbolId spelling;
    Atom *name_key;
    Atom *left_atom;
    uint32_t parent;
    uint32_t left_order;
    bool nonvar;
    bool class_frozen;
    bool class_rewrite;
    uint32_t class_rep;
    uint32_t class_best;
} HeOrientVar;

typedef struct {
    VarId key;
    SymbolId spelling;
    Atom *name_key;
    Atom *value;
    VarId value_var;
} HeOrientRow;

static uint32_t he_orient_find(HeOrientVar *vars, uint32_t index) {
    uint32_t parent = vars[index].parent;
    if (parent != index) {
        parent = he_orient_find(vars, parent);
        vars[index].parent = parent;
    }
    return parent;
}

static bool he_orient_node(HeOrientVar **vars, uint32_t *count, uint32_t *cap,
                           VarId id, SymbolId spelling, Atom *name_key,
                           uint32_t *index_out) {
    for (uint32_t i = 0; i < *count; i++) {
        if (var_id_eq((*vars)[i].id, id)) {
            *index_out = i;
            return true;
        }
    }
    if (*count == *cap) {
        uint32_t next = *cap ? *cap * 2u : 8u;
        HeOrientVar *grown = realloc(*vars, sizeof(*grown) * (size_t)next);
        if (!grown)
            return false;
        *vars = grown;
        *cap = next;
    }
    (*vars)[*count] = (HeOrientVar){
        .id = id,
        .spelling = spelling,
        .name_key = name_key,
        .parent = *count,
        .left_order = UINT32_MAX,
        .class_best = UINT32_MAX,
    };
    *index_out = (*count)++;
    return true;
}

static void he_orient_note_left(HeOrientVar *vars, uint32_t count,
                                Atom *atom, uint32_t *order) {
    if (!atom)
        return;
    if (atom->kind == ATOM_VAR) {
        for (uint32_t i = 0; i < count; i++) {
            if (!var_id_eq(vars[i].id, atom->var_id) || vars[i].left_order != UINT32_MAX)
                continue;
            vars[i].left_order = (*order)++;
            vars[i].left_atom = atom;
            return;
        }
        return;
    }
    if (atom->kind != ATOM_EXPR)
        return;
    for (CettaExprIndex i = 0; i < atom->expr.len; i++)
        he_orient_note_left(vars, count, atom->expr.elems[i], order);
}

/* Publish each open variable class under the earliest variable of the unify
 * left operand. A class that also meets a non-variable keeps the matcher's
 * edges. `matched` is unchanged when no class moves. False is allocation
 * failure; the caller marks the evaluation incomplete. */
static bool he_orient_unify_left(Arena *arena, Atom *left, Bindings *matched) {
    HeOrientRow *rows = NULL;
    HeOrientVar *vars = NULL;
    uint32_t row_count = 0u;
    uint32_t row_cap = 0u;
    uint32_t var_count = 0u;
    uint32_t var_cap = 0u;
    bool saw_var_edge = false;
    bool any_rewrite = false;
    bool ok = true;
    /* Framed epoch variables are ordinary open classes. Prime state stays
     * with the matcher that recorded it. */
    if (!matched || matched->prime_ext || !bindings_has_bound_values(matched))
        return true;
    {
        BindingsIterator iterator = {.bindings = matched};
        Binding binding;
        while (bindings_iterator_next(&iterator, &binding)) {
            Atom *value = binding_value_materialize(arena, binding.value);
            uint32_t slot = row_count;
            if (!value)
                goto fail;
            for (uint32_t i = 0; i < row_count; i++) {
                if (var_id_eq(rows[i].key, binding.var_id)) {
                    slot = i;
                    break;
                }
            }
            if (slot == row_count) {
                if (row_count == row_cap) {
                    uint32_t next = row_cap ? row_cap * 2u : 8u;
                    HeOrientRow *grown = realloc(
                        rows, sizeof(*grown) * (size_t)next);
                    if (!grown)
                        goto fail;
                    rows = grown;
                    row_cap = next;
                }
                row_count++;
            }
            rows[slot].key = binding.var_id;
            rows[slot].spelling = binding.spelling;
            rows[slot].name_key = binding.name_key;
            rows[slot].value = value;
            rows[slot].value_var =
                value->kind == ATOM_VAR ? value->var_id : VAR_ID_NONE;
            if (!var_id_is_none(rows[slot].value_var))
                saw_var_edge = true;
        }
    }
    if (!saw_var_edge)
        goto done;
    for (uint32_t i = 0; ok && i < row_count; i++) {
        uint32_t key_node = 0u;
        ok = he_orient_node(&vars, &var_count, &var_cap, rows[i].key,
                            rows[i].spelling, rows[i].name_key, &key_node);
        if (!ok)
            break;
        if (var_id_is_none(rows[i].value_var)) {
            vars[key_node].nonvar = true;
            continue;
        }
        uint32_t value_node = 0u;
        ok = he_orient_node(
            &vars, &var_count, &var_cap, rows[i].value_var,
            atom_symbol_current_id(rows[i].value), rows[i].value->name_key,
            &value_node);
        if (!ok)
            break;
        {
            uint32_t left_root = he_orient_find(vars, key_node);
            uint32_t right_root = he_orient_find(vars, value_node);
            if (left_root != right_root)
                vars[right_root].parent = left_root;
        }
    }
    if (!ok)
        goto fail;
    {
        uint32_t order = 0u;
        he_orient_note_left(vars, var_count, left, &order);
    }
    for (uint32_t i = 0; i < var_count; i++)
        (void)he_orient_find(vars, i);
    for (uint32_t i = 0; i < var_count; i++) {
        uint32_t root = vars[i].parent;
        if (vars[i].nonvar)
            vars[root].class_frozen = true;
        if (vars[i].left_order < vars[root].class_best) {
            vars[root].class_best = vars[i].left_order;
            vars[root].class_rep = i;
        }
    }
    for (uint32_t i = 0; i < var_count; i++) {
        if (vars[i].parent != i)
            continue;
        vars[i].class_rewrite =
            !vars[i].class_frozen && vars[i].class_best != UINT32_MAX;
        any_rewrite = any_rewrite || vars[i].class_rewrite;
    }
    if (!any_rewrite)
        goto done;
    {
        Bindings oriented;
        bindings_init(&oriented);
        for (uint32_t i = 0; ok && i < row_count; i++) {
            uint32_t key_node = 0u;
            Atom *key;
            if (!var_id_is_none(rows[i].value_var)) {
                for (uint32_t v = 0; v < var_count; v++) {
                    if (var_id_eq(vars[v].id, rows[i].key)) {
                        key_node = v;
                        break;
                    }
                }
                if (vars[vars[key_node].parent].class_rewrite)
                    continue;
            }
            key = atom_var_with_presentation(
                arena, rows[i].spelling, rows[i].name_key, rows[i].key);
            ok = key && bindings_add_var(&oriented, key, rows[i].value);
        }
        for (uint32_t i = 0; ok && i < var_count; i++) {
            uint32_t root = vars[i].parent;
            uint32_t rep;
            Atom *key;
            Atom *representative;
            if (!vars[root].class_rewrite || i == vars[root].class_rep)
                continue;
            rep = vars[root].class_rep;
            key = atom_var_with_presentation(
                arena, vars[i].spelling, vars[i].name_key, vars[i].id);
            representative = vars[rep].left_atom;
            ok = key && representative &&
                bindings_add_var(&oriented, key, representative);
        }
        if (ok && matched->eq_len) {
            for (uint32_t i = 0; ok && i < matched->eq_len; i++) {
                Atom *lhs = binding_value_materialize(
                    arena, matched->constraints[i].lhs);
                Atom *rhs = binding_value_materialize(
                    arena, matched->constraints[i].rhs);
                ok = lhs && rhs && bindings_add_constraint(&oriented, lhs, rhs);
            }
        }
        if (!ok) {
            bindings_free(&oriented);
            goto fail;
        }
        bindings_replace(matched, &oriented);
    }
done:
    free(rows);
    free(vars);
    return true;
fail:
    free(rows);
    free(vars);
    return false;
}

/* He-compat let is the library unify of the value against the pattern.
 * Open classes publish the earliest variable of that value. A match that
 * already does so keeps the builder, including its trail. */
static bool he_orient_let_value(Arena *arena, Atom *value,
                               BindingsBuilder *builder) {
    if (!active_profile_uses_rust_he_compat_semantics() || !builder)
        return true;
    const Bindings *current = bindings_builder_bindings(builder);
    if (!current || current->prime_ext || !bindings_has_bound_values(current))
        return true;
    Bindings preview;
    if (!bindings_clone(&preview, current))
        return false;
    if (!he_orient_unify_left(arena, value, &preview)) {
        bindings_free(&preview);
        return false;
    }
    if (bindings_eq(&preview, (Bindings *)current)) {
        bindings_free(&preview);
        return true;
    }
    BindingsBuilder fresh;
    if (!bindings_builder_init(&fresh, NULL) ||
        !bindings_builder_try_merge(&fresh, &preview)) {
        bindings_builder_free(&fresh);
        bindings_free(&preview);
        return false;
    }
    bindings_free(&preview);
    bindings_builder_free(builder);
    *builder = fresh;
    return true;
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
                        result, frame->fuel, &item->env, os, NULL, NULL);
                    return;
                }
                outcome_set_add(os, result, &item->env);
            } else if (frame->phase == HE_INSTRUCTION_CHAIN) {
                /* The result replaces the destination first; the template
                 * takes the result's environment when it runs. */
                Atom *body = instantiate_template_variable_once(a,
                    expr_arg(frame->atom, frame->control.last),
                    expr_arg(frame->atom, frame->control.first)->var_id, result);
                he_instruction_push(stack, HE_INSTRUCTION_RUN, s, a,
                    body, child_fuel, &item->env, os, NULL, NULL);
                return;
            } else if (atom_head_symbol_id(result) == g_builtin_syms.return_text &&
                       result->expr.len == 2u) {
                outcome_set_add(os, result->expr.elems[1], &item->env);
            } else if (he_minimal_instruction(result)) {
                if (active_profile_uses_rust_he_compat_semantics()) {
                    he_instruction_push(stack, HE_INSTRUCTION_RUN, s, a,
                        result, child_fuel, &item->env, &frame->inner, NULL, NULL);
                    return;
                }
                Atom *body = atom_expr2(a, atom_symbol(a, "function"), result);
                he_instruction_push(stack, HE_INSTRUCTION_RUN, s, a,
                    body, child_fuel, &item->env, os, NULL, NULL);
                return;
            } else if (active_profile_uses_rust_he_compat_semantics()) {
                outcome_set_add(os, atom_error(a,
                    he_function_noreturn_witness(frame, result),
                    atom_symbol(a, "NoReturn")), &item->env);
            } else {
                outcome_set_add(os, atom_error(a, frame->syntax,
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
            if (!result) {
                if (!eval_unwinding()) continue;
                he_instruction_pop(stack);return;
            }
            if (atom_is_legacy_empty_sentinel(result))
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
    Atom *before = frame->atom;
    if (frame->control.operation == CETTA_CONTROL_NONE)
        frame->control = he_instruction_control_plan(before);
    CettaControlOperationPlan control = frame->control;
    Atom *mapped = frame->kind == HE_INSTRUCTION_RUN
        ? he_instruction_apply_env(a, &frame->env, before, control)
        : bindings_apply_if_vars(&frame->env, a, before);
    frame->atom = mapped ? resolve_registry_refs(a, mapped) : NULL;
    if (frame->syntax == before) {
        frame->syntax = frame->atom;
    } else {
        Atom *syntax = frame->kind == HE_INSTRUCTION_RUN
            ? he_instruction_source_env(a, &frame->env, frame->syntax)
            : bindings_apply_if_vars(&frame->env, a, frame->syntax);
        frame->syntax = syntax ? resolve_registry_refs(a, syntax) : NULL;
    }
    if (!frame->atom || !frame->syntax) {
        eval_mark_interrupted(CETTA_EVAL_INCOMPLETE_HOST_FAILURE);
        he_instruction_pop(stack);
        return;
    }
    Atom *atom = frame->atom;
    SymbolId head = atom_head_symbol_id(atom);
    CettaExprLen nargs = atom->kind == ATOM_EXPR && atom->expr.len
        ? expr_nargs(atom) : 0u;
    if (frame->control.operation == CETTA_CONTROL_NONE && atom != before)
        frame->control = he_instruction_control_plan(atom);
    control = frame->control;
    if (!he_instruction_control_valid(control, nargs)) {
        eval_mark_interrupted(CETTA_EVAL_INCOMPLETE_HOST_FAILURE);
        he_instruction_pop(stack);
        return;
    }
    if (frame->kind == HE_INSTRUCTION_RAW) {
        if (control.operation != CETTA_CONTROL_NONE || he_minimal_instruction(atom)) {
            frame->phase = HE_INSTRUCTION_DONE;
            he_instruction_push(stack, HE_INSTRUCTION_RUN, s, a,
                atom, frame->fuel, &frame->env, os, &control, frame->syntax);
        } else {
            he_raw_collect(s, a, atom, frame->fuel, &frame->env, &frame->inner);
            frame->phase = HE_INSTRUCTION_RAW_RETURNS;
        }
        return;
    }
    bool contextual_force = control.operation == CETTA_CONTROL_NONE &&
        head == g_builtin_syms.evalc && nargs == 2u;
    if (control.operation == CETTA_CONTROL_FORCE || contextual_force) {
        Space *target = s;
        if (contextual_force)
            target = g_registry ? resolve_single_space_arg(
                s, a, expr_arg(atom, 1), child_fuel) : NULL;
        frame->phase = HE_INSTRUCTION_DONE;
        if (target)
            he_instruction_push(stack, HE_INSTRUCTION_RAW, target, a,
                expr_arg(frame->atom, control.operation == CETTA_CONTROL_FORCE
                    ? control.source : 0u), child_fuel, &frame->env, os, NULL, NULL);
        else
            outcome_set_add(os, call_signature_error(a, frame->syntax,
                "(evalc <atom> <space>)"), &frame->env);
    } else if (control.operation == CETTA_CONTROL_SEQUENCE) {
        if (expr_arg(atom, control.first)->kind != ATOM_VAR) {
            outcome_set_add(os, atom_error(a, frame->syntax,
                atom_symbol(a, "IncorrectNumberOfArguments")), &frame->env);
        } else {
            frame->phase = HE_INSTRUCTION_CHAIN;
            he_instruction_push(stack, HE_INSTRUCTION_RUN, s, a,
                expr_arg(atom, control.source), child_fuel, &frame->env, &frame->inner, NULL, NULL);
        }
    } else if (control.operation == CETTA_CONTROL_RETURN_SCOPE) {
        Atom *body = expr_arg(atom, control.source);
        if (body->kind != ATOM_EXPR) {
            Atom *error = active_profile_uses_rust_he_compat_semantics()
                ? call_signature_error(a,frame->syntax,"(function (: <body> Expression))")
                : atom_error(a,frame->syntax,atom_symbol(a,"IncorrectNumberOfArguments"));
            outcome_set_add(os,error,&frame->env);
        } else {
            frame->phase = HE_INSTRUCTION_FUNCTION;
            bool step = active_profile_uses_rust_he_compat_semantics() ||
                he_minimal_instruction(body) || atom_head_symbol_id(body)==g_builtin_syms.return_text;
            if (step)
                he_instruction_push(stack,HE_INSTRUCTION_RUN,s,a,body,child_fuel,
                    &frame->env,&frame->inner,NULL,NULL);
            else
                eval_for_caller(s,a,NULL,body,child_fuel,&frame->env,true,&frame->inner);
        }
    } else if (control.operation == CETTA_CONTROL_COLLECT) {
        frame->phase = HE_INSTRUCTION_COLLAPSE;
        he_instruction_push(stack, HE_INSTRUCTION_RUN, s, a,
            expr_arg(atom, control.source), child_fuel, &frame->env, &frame->inner, NULL, NULL);
    } else if (head == g_builtin_syms.metta && nargs == 3u && g_registry) {
        Space *target = resolve_single_space_arg(
            s, a, expr_arg(atom, 2), child_fuel);
        if (target)
            eval_for_caller(target, a, expr_arg(frame->atom, 1),
                expr_arg(frame->atom, 0), child_fuel, &frame->env, true, os);
        else
            outcome_set_add(os, call_signature_error(a, frame->syntax,
                "(metta <atom> <type> <space>)"), &frame->env);
        frame->phase = HE_INSTRUCTION_DONE;
    } else if (head == g_builtin_syms.context_space && nargs == 0u) {
        /* The interpreter's context, the space of the running evaluation:
         * evalc and metta choose the space that looks up equations. */
        outcome_set_add(os, atom_space(a,
            cetta_library_logical_module_space(g_library_context,
                g_eval_root_space ? g_eval_root_space : s)),
            &frame->env);
        frame->phase = HE_INSTRUCTION_DONE;
    } else if (head == g_builtin_syms.call_native) {
        outcome_set_add(os, call_signature_error(a, frame->syntax,
            "(call-native func args)"), &frame->env);
        frame->phase = HE_INSTRUCTION_DONE;
    } else if (head == g_builtin_syms.unify && nargs == 4u) {
        Bindings matched;
        bindings_init(&matched);
        Atom *left = expr_arg(atom, 0);
        Atom *right = expr_arg(atom, 1);
        bool success = match_atoms(left, right, &matched, a);
        bool orient_failed = false;
        /* Open classes publish a left-operand variable. The earliest
         * occurrence in this operand is the representative. */
        if (success && active_profile_uses_rust_he_compat_semantics() &&
            !he_orient_unify_left(a, left, &matched)) {
            eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);
            orient_failed = true;
            success = false;
        }
        if (success) {
            Bindings merged;
            const Bindings *effective = NULL;
            success = bindings_effective_merge(
                &merged, &effective, &frame->env, &matched, true);
            if (success) {
                Atom *result = bindings_apply_if_vars(effective, a, expr_arg(atom, 2));
                outcome_set_add(os, result, effective ? effective : &frame->env);
                if (effective == &merged) bindings_free(&merged);
            }
        }
        if (!success && !orient_failed) {
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
    } else if (atom->kind == ATOM_EXPR && head == g_builtin_syms.unify &&
               active_profile_uses_rust_he_compat_semantics()) {
        /* Every arity other than four. The symbol keeps the printer's
         * rendering of this call, fresh variable names included. */
        outcome_set_add(os, call_signature_error(a, atom,
            "(unify <atom> <pattern> <then> <else>)"), &frame->env);
    } else if (he_minimal_instruction(atom) && head != g_builtin_syms.return_text) {
        outcome_set_add(os, atom_error(a, frame->syntax, atom_symbol(a, "IncorrectNumberOfArguments")), &frame->env);
    } else {
        outcome_set_add(os, atom, &frame->env);
    }

    if (stack->top == &frame->continuation &&
        frame->phase == HE_INSTRUCTION_ENTER)
        he_instruction_pop(stack);
}

static void he_instruction_run_with_caller(HeInstructionKind kind, Space *s,
    Arena *a, Atom *atom, int fuel, const Bindings *prefix, OutcomeSet *os,
    const CettaControlOperationPlan *control, Atom *syntax, Atom *function_caller) {
    CettaOwnedFrameStack stack = {0};
    he_instruction_push(&stack, kind, s, a, atom, fuel, prefix, os, control, syntax);
    if (function_caller && stack.top)
        ((HeInstructionFrame *)stack.top)->function_caller = function_caller;
    while (stack.top && !eval_unwinding() && !eval_cancel_check())
        he_instruction_resume(&stack);
    while (stack.top)
        he_instruction_pop(&stack);
}

static void he_instruction_run(HeInstructionKind kind, Space *s, Arena *a,
    Atom *atom, int fuel, const Bindings *prefix, OutcomeSet *os,
    const CettaControlOperationPlan *control, Atom *syntax) {
    he_instruction_run_with_caller(kind,s,a,atom,fuel,prefix,os,control,syntax,NULL);
}

static void he_minimal_run(Space *s, Arena *a, Atom *atom, int fuel,
    const Bindings *prefix, OutcomeSet *os) {
    he_instruction_run(HE_INSTRUCTION_RUN, s, a, atom, fuel, prefix, os, NULL, NULL);
}

static void he_raw_invoke(Space *s, Arena *a, Atom *atom, int fuel,
    const Bindings *prefix, OutcomeSet *os) {
    he_instruction_run(HE_INSTRUCTION_RAW, s, a, atom, fuel, prefix, os, NULL, NULL);
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
    CettaControlOperationPlan control = he_instruction_control_plan(atom);
    if (control.operation == CETTA_CONTROL_SEQUENCE &&
        expr_arg(atom, control.first)->kind == ATOM_VAR &&
        !he_minimal_instruction(expr_arg(atom, control.source)) &&
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
        eval_for_caller(s, a, NULL, expr_arg(live[0], control.source), fuel,
                        &env, true, &sources);
        for (CettaCount i = 0u; i < sources.len && !eval_unwinding(); i++) {
            Atom *result = outcome_atom_materialize(a, &sources.items[i]);
            if (!result) continue;
            Atom *body = instantiate_template_variable_once(
                a, expr_arg(live[0], control.last),
                expr_arg(live[0], control.first)->var_id, result);
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
        if (he_instruction_completion(a, result, live[1], true, true) ==
                HE_INSTRUCTION_TAIL) {
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
        HeInstructionCompletion completion = he_instruction_completion(
            a, result, live[1], true, false);
        if (completion == HE_INSTRUCTION_OMIT) continue;
        if (completion == HE_INSTRUCTION_REFUSAL) {
            result = he_instruction_refusal_readback(live[0], live[0], control);
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
                                        Atom *invocation, Atom *type, int fuel,
                                        const Bindings *prefix,
                                        bool preserve_bindings, OutcomeSet *os) {
    __attribute__((cleanup(bindings_free))) Bindings env;
    bindings_init(&env);
    if (prefix && !bindings_copy(&env, prefix)) {
        eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);
        return;
    }
    /* The equation emitter has already applied the match. A chained rule
     * variable is still the name inside this function until the query
     * variable is put back, which is what the canonical raw result does. */
    if (!he_orient_query_representative(a, &atom, &env, NULL)) {
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
    Atom *live[] = {atom, type, invocation};
    eval_gc_outcome_suspension_begin_with_atoms(
        &suspension, &lexical, os, &returned, live, 3u);
    bool raw_function = active_profile_uses_rust_he_compat_semantics() &&
        atom_head_symbol_id(atom) == g_builtin_syms.function;
    if (raw_function)
        he_instruction_run_with_caller(HE_INSTRUCTION_RUN, s, a, atom, fuel,
            &env, &returned, NULL, NULL, invocation);
    else
        he_minimal_run(s, a, atom, fuel, &env, &returned);
    for (CettaCount i = 0u; i < returned.len && !eval_unwinding(); i++) {
        Atom *result = outcome_atom_materialize(a, &returned.items[i]);
        HeInstructionCompletion completion = he_instruction_completion(a,result,live[1],false,false);
        if (completion == HE_INSTRUCTION_OMIT) continue;
        if (completion == HE_INSTRUCTION_REFUSAL)
            outcome_set_add(os,he_call_return_atom(live[2],result),&returned.items[i].env);
        else if (raw_function && (atom_is_error(result) || atom_is_legacy_empty_sentinel(result)))
            outcome_set_add(os,result,&returned.items[i].env);
        else
            eval_for_caller(s, a, live[1], result, fuel,
                            &returned.items[i].env, preserve_bindings, os);
    }
    outcome_set_free(&returned);
}
