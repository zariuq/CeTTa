/* HE's interpretation policy over owned call, argument and return frames.
 * The source interpreter supplies control and native leaves; an authored
 * argument or equation body is a saved task, rather than a nested C call. */
typedef enum {
    HE_CANONICAL_BIND,
    HE_CANONICAL_EVALUATE,
    HE_CANONICAL_CALL,
    HE_CANONICAL_CALLER,
    HE_CANONICAL_DISPATCH,
    HE_CANONICAL_ARGUMENTS,
    HE_CANONICAL_TUPLE,
    HE_CANONICAL_RESULT,
    HE_CANONICAL_CONTROL,
    HE_CANONICAL_INSTRUCTION,
    HE_CANONICAL_MATCH,
    HE_CANONICAL_CAPTURE,
    HE_CANONICAL_SPACE,
    HE_CANONICAL_STATE,
} HeCanonicalKind;

typedef struct {
    Atom *op;
    Atom **source;
    Atom **types;
    Atom **prefix;
    CettaExprLen len;
    EvalGcRootFrame root;
} HeCanonicalRegisters;

typedef struct {
    CettaOwnedFrameLink link;
    HeCanonicalKind kind;
    unsigned phase;
    Space *space;
    Arena *arena;
    Atom *atom;
    Atom *type;
    Atom *live[4];
    Bindings env;
    OutcomeSet *target;
    OutcomeSet child;
    OutcomeSet work;
    OutcomeSet paths;
    int fuel;
    bool preserve_bindings;
    bool typed;
    bool selected;
    bool has_functions;
    bool has_non_functions;
    bool query_closed;
    CettaCount index;
    CettaCount head_index;
    CettaCount path_index;
    Atom **types;
    uint32_t type_count;
    EvalGcRootFrame lexical_root;
    EvalGcRootFrame child_root;
    EvalGcRootFrame work_root;
    EvalGcRootFrame type_root;
    HeCanonicalRegisters *registers;
    bool owns_registers;
    InterpretFunctionArgsFrame argument;
    InterpretTupleFrame tuple;
    bool argument_initialized;
    bool tuple_initialized;
    MettaEvalDepthGuard evaluation_depth;
    EvalObservationDemandGuard observation;
    Space **match_spaces;
    bool sourced_match;
    OutcomeSet saved;
    EvalGcRootFrame saved_root;
    DeferredSpaceSet deferred_spaces;
    CettaOwnedFrameStack instructions;
    CettaEvaluatorOptions *saved_options;
} HeCanonicalFrame;

typedef struct {
    CettaOwnedFrameStack stack;
    OutcomeSet *root_target;
    Arena *arena;
    ArenaMark gc_anchor;
    size_t gc_survivor_floor_bytes;
} HeCanonicalDriver;

static __thread HeCanonicalDriver *g_he_canonical_driver;

static bool he_canonical_query_closed(bool source_closed) {
    HeCanonicalFrame *frame = g_he_canonical_driver
        ? (HeCanonicalFrame *)g_he_canonical_driver->stack.top : NULL;
    return frame && frame->kind == HE_CANONICAL_CALL
        ? frame->query_closed : source_closed;
}

static void metta_call_impl(Space *s, Arena *a, Atom *atom, Atom *etype,
    int fuel, bool preserve_bindings, const Bindings *initial_env,
#if CETTA_PRIME_EVAL_STACK
    int strict_ready_argument,
#endif
    OutcomeSet *os);

static bool he_canonical_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        const char *setting = getenv("CETTA_HE_CANONICAL_EXECUTION");
        enabled = !setting || strcmp(setting, "1") == 0;
    }
    return enabled && eval_current_language_id() == CETTA_LANGUAGE_HE;
}

static HeCanonicalFrame *he_canonical_push(HeCanonicalKind kind,
    Space *s, Arena *a, Atom *atom, Atom *type, int fuel,
    const Bindings *env, bool preserve, OutcomeSet *target) {
    HeCanonicalFrame *frame = cetta_malloc(sizeof(*frame));
    memset(frame, 0, sizeof(*frame));
    frame->kind = kind;
    frame->space = s;
    frame->arena = a;
    frame->atom = atom;
    frame->type = type;
    frame->fuel = fuel;
    frame->target = target;
    frame->preserve_bindings = preserve;
    if (kind == HE_CANONICAL_EVALUATE) {
        frame->evaluation_depth.entered = true;
        g_metta_eval_depth++;
    }
    frame->query_closed = eval_gc_enabled() && atom && !atom_contains_vars(atom);
    bindings_init(&frame->env);
    outcome_set_init(&frame->child);
    outcome_set_init(&frame->work);
    outcome_set_init(&frame->paths);
    outcome_set_init(&frame->saved);
    if (env && !bindings_copy(&frame->env, env)) {
        metta_eval_depth_guard_leave(&frame->evaluation_depth);
        free(frame);
        eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);
        return NULL;
    }
    eval_gc_root_frame_enter(&frame->lexical_root,
        &frame->atom, &frame->env, &frame->type);
    /* All remaining atom-bearing fields are registered below or by the
     * argument/register owner. There is no suspended C-local state in this
     * lexical frame; collection occurs between continuation operations. */
    eval_gc_root_frame_suspend_precisely(&frame->lexical_root);
    eval_gc_root_frame_enter_outcome_continuation(&frame->child_root,
        frame->live, 4u, &frame->child, target);
    eval_gc_root_frame_enter_outcome_continuation(&frame->work_root,
        NULL, 0u, &frame->work, &frame->paths);
    eval_gc_root_frame_enter_outcome_continuation(&frame->saved_root,
        NULL, 0u, &frame->saved, NULL);
    cetta_owned_frame_push(&g_he_canonical_driver->stack, &frame->link);
    return frame;
}

static void he_canonical_pop(void) {
    HeCanonicalFrame *frame = (HeCanonicalFrame *)cetta_owned_frame_pop(
        &g_he_canonical_driver->stack);
    if (frame->saved_options) {
        *active_eval_options() = *frame->saved_options;
        free(frame->saved_options);
    }
    while (frame->instructions.top)
        he_instruction_pop(&frame->instructions);
    if (frame->argument_initialized)
        interpret_function_args_frame_cleanup(&frame->argument);
    if (frame->tuple_initialized)
        interpret_tuple_frame_cleanup(&frame->tuple);
    if (frame->owns_registers) {
        eval_gc_root_frame_leave(&frame->registers->root);
        free(frame->registers->source);
        free(frame->registers->types);
        free(frame->registers->prefix);
        free(frame->registers);
    }
    if (frame->types) {
        eval_gc_root_frame_leave(&frame->type_root);
        free(frame->types);
    }
    eval_observation_demand_leave(&frame->observation);
    metta_eval_depth_guard_leave(&frame->evaluation_depth);
    eval_gc_root_frame_leave(&frame->saved_root);
    eval_gc_root_frame_leave(&frame->work_root);
    eval_gc_root_frame_leave(&frame->child_root);
    eval_gc_root_frame_leave(&frame->lexical_root);
    outcome_set_free(&frame->paths);
    outcome_set_free(&frame->saved);
    outcome_set_free(&frame->work);
    outcome_set_free(&frame->child);
    bindings_free(&frame->env);
    free(frame->match_spaces);
    deferred_space_set_free(&frame->deferred_spaces);
    free(frame);
}

static void he_canonical_publish(HeCanonicalFrame *frame,
    OutcomeSet *source, bool invocation) {
    for (CettaCount i = 0u; i < source->len; i++) {
        Outcome *out = &source->items[i];
        Atom *value = outcome_preview_atom(out);
        if (invocation && he_not_reducible(value))
            outcome_set_add_prefixed(frame->arena, frame->target,
                frame->atom, &out->env, &frame->env,
                frame->preserve_bindings);
        else
            outcome_set_add_prefixed_outcome(frame->arena, frame->target,
                out, invocation ? &frame->env : NULL,
                frame->preserve_bindings);
    }
}

static bool he_canonical_argument_schedule(Space *s, Arena *a, Atom *type,
    Atom *atom, int fuel, const Bindings *env, OutcomeSet *os) {
    if (!g_he_canonical_driver)
        return false;
    HeCanonicalFrame *frame = he_canonical_push(HE_CANONICAL_BIND,
        s, a, atom, type, fuel, env, true, os);
    if (frame)
        frame->typed = true;
    return true;
}

static bool he_canonical_caller_schedule(Space *s, Arena *a, Atom *type,
    Atom *atom, int fuel, const Bindings *prefix, bool preserve,
    OutcomeSet *os) {
    HeCanonicalFrame *frame = g_he_canonical_driver
        ? (HeCanonicalFrame *)g_he_canonical_driver->stack.top : NULL;
    if (!frame || frame->kind != HE_CANONICAL_INSTRUCTION ||
        !frame->instructions.top)
        return false;
    HeInstructionFrame *instruction =
        (HeInstructionFrame *)frame->instructions.top;
    /* Stack-local foreign result sets cannot become suspended targets. */
    if (os != &instruction->inner && os != instruction->target)
        return false;
    he_canonical_push(HE_CANONICAL_CALLER, s, a, atom, type, fuel,
        prefix, preserve, os);
    return true;
}

static void he_canonical_instruction_push(HeCanonicalFrame *parent,
    Atom *atom, Atom *type, const Bindings *env, bool public_instruction,
    bool allow_tail, OutcomeSet *target) {
    HeCanonicalFrame *frame = he_canonical_push(HE_CANONICAL_INSTRUCTION,
        parent->space, parent->arena, atom, type, parent->fuel,
        env, parent->preserve_bindings, target);
    if (frame) {
        frame->typed = public_instruction;
        frame->selected = allow_tail;
        frame->query_closed = parent->query_closed;
    }
}

static void he_canonical_arguments_push(HeCanonicalFrame *parent,
    Atom *op, Atom **source, Atom **types, CettaExprLen len,
    const Bindings *env, OutcomeSet *target, bool typed) {
    HeCanonicalFrame *frame = he_canonical_push(
        typed ? HE_CANONICAL_ARGUMENTS : HE_CANONICAL_TUPLE,
        parent->space, parent->arena, NULL, NULL, parent->fuel,
        env, true, target);
    if (!frame)
        return;
    HeCanonicalRegisters *registers = cetta_malloc(sizeof(*registers));
    memset(registers, 0, sizeof(*registers));
    registers->op = op;
    registers->len = len;
    if (len) {
        registers->source = cetta_malloc(sizeof(Atom *) * (size_t)len);
        registers->prefix = cetta_malloc(sizeof(Atom *) * (size_t)len);
        memset(registers->prefix, 0, sizeof(Atom *) * (size_t)len);
        memcpy(registers->source, source, sizeof(Atom *) * (size_t)len);
        if (typed) {
            registers->types = cetta_malloc(sizeof(Atom *) * (size_t)len);
            memcpy(registers->types, types, sizeof(Atom *) * (size_t)len);
        }
    }
    frame->registers = registers;
    frame->owns_registers = true;
    if (typed) {
        eval_gc_root_frame_enter_function_args_machine(&registers->root,
            &registers->op, registers->source, (size_t)len,
            registers->types, (size_t)len, registers->prefix,
            (size_t)len, target);
        interpret_function_args_frame_init(&frame->argument, 0u, &frame->env);
        frame->argument_initialized = true;
    } else {
        eval_gc_root_frame_enter_tuple_machine(&registers->root,
            NULL, 0u, registers->source, (size_t)len,
            registers->prefix, (size_t)len, &frame->env, target, NULL);
        interpret_tuple_frame_init(&frame->tuple, 0u, &frame->env, NULL);
        frame->tuple_initialized = true;
    }
}

static void he_canonical_argument_next(HeCanonicalFrame *frame,
    CettaExprIndex index, const Bindings *env,
    const VariantInstance *variant) {
    HeCanonicalFrame *child = he_canonical_push(frame->kind,
        frame->space, frame->arena, NULL, NULL, frame->fuel,
        env, true, frame->target);
    if (!child)
        return;
    child->registers = frame->registers;
    if (frame->kind == HE_CANONICAL_ARGUMENTS) {
        interpret_function_args_frame_init(&child->argument, index, &child->env);
        child->argument_initialized = true;
    } else {
        interpret_tuple_frame_init(&child->tuple, (uint32_t)index,
            &child->env, variant);
        child->tuple_initialized = true;
    }
}

static void he_canonical_resume_arguments(HeCanonicalFrame *frame) {
    InterpretFunctionArgsFrame *argument = &frame->argument;
    if (argument->state == INTERPRET_FUNCTION_ARGS_FRAME_AFTER_ITER_CHILD) {
        interpret_function_args_frame_finish_child(argument);
        argument->state = INTERPRET_FUNCTION_ARGS_FRAME_ITERATE;
    } else if (argument->state == INTERPRET_FUNCTION_ARGS_FRAME_AFTER_ONLY_CHILD) {
        he_canonical_pop();
        return;
    }
    const Bindings *child_env = NULL;
    InterpretFunctionArgsAction action = interpret_function_args_frame_step(
        frame->space, frame->arena, &frame->registers->op,
        frame->registers->source, frame->registers->types,
        frame->registers->len, frame->registers->prefix, frame->fuel,
        frame->target, argument, &child_env, NULL, true);
    if (action == INTERPRET_FUNCTION_ARGS_POP)
        he_canonical_pop();
    else if (action == INTERPRET_FUNCTION_ARGS_PUSH)
        he_canonical_argument_next(frame, argument->idx + 1u, child_env, NULL);
}

static void he_canonical_resume_tuple(HeCanonicalFrame *frame) {
    InterpretTupleFrame *tuple = &frame->tuple;
    HeCanonicalRegisters *registers = frame->registers;
    Arena *a = frame->arena;
    if (tuple->state == INTERPRET_TUPLE_FRAME_AFTER_CHILD) {
        interpret_tuple_frame_finish_child(tuple);
    }
    if (tuple->idx == registers->len) {
        Atom *value = atom_expr(a, registers->prefix, registers->len);
        if (tuple->prefix_variant && variant_instance_present(tuple->prefix_variant))
            outcome_set_add_with_variant(frame->target, value,
                tuple->ctx, tuple->prefix_variant);
        else
            outcome_set_add(frame->target, value, tuple->ctx);
        he_canonical_pop();
        return;
    }
    if (tuple->state == INTERPRET_TUPLE_FRAME_ENTER) {
        outcome_set_init(&tuple->sub);
        tuple->sub_initialized = true;
        frame->phase = 1u;
        tuple->state = INTERPRET_TUPLE_FRAME_ITERATE;
        Atom *elem = bindings_apply_if_vars(tuple->ctx, a,
            registers->source[tuple->idx]);
        he_canonical_argument_schedule(frame->space, a, NULL, elem,
            frame->fuel, tuple->ctx, &tuple->sub);
        return;
    }
    if (frame->phase == 1u) {
        frame->phase = 2u;
        if (!tuple->sub.len || !bindings_builder_init(
                &tuple->merged_builder, tuple->ctx)) {
            he_canonical_pop();
            return;
        }
        tuple->builder_initialized = true;
    }
    while (tuple->sub_index < tuple->sub.len) {
        Outcome *item = &tuple->sub.items[tuple->sub_index++];
        Atom *value = item->atom;
        if (atom_is_legacy_empty_sentinel(value) || outcome_atom_is_error(a, item)) {
            Bindings empty;
            bindings_init(&empty);
            outcome_set_add(frame->target, outcome_atom_materialize(a, item), &empty);
            continue;
        }
        const VariantInstance *variant = tuple->prefix_variant;
        if (variant_instance_present(&item->variant)) {
            if (variant && variant_instance_present(variant) &&
                !variant_instance_clone(&tuple->active_variant, variant))
                continue;
            if (!variant_instance_append_rebased(a, &tuple->active_variant,
                    &value, item->atom, &item->variant)) {
                variant_instance_free(&tuple->active_variant);
                continue;
            }
            tuple->active_variant_owned = true;
            variant = &tuple->active_variant;
        }
        BindingsMergeAttempt attempt;
        if (!bindings_builder_merge_or_clone(&tuple->merged_builder,
                tuple->ctx, &item->env, &attempt)) {
            if (tuple->active_variant_owned) {
                variant_instance_free(&tuple->active_variant);
                tuple->active_variant_owned = false;
            }
            continue;
        }
        tuple->active_attempt = attempt;
        if (!tuple->active_attempt.used_builder)
            tuple->active_attempt.env = &tuple->active_attempt.owned;
        tuple->active_attempt_initialized = true;
        registers->prefix[tuple->idx] = value;
        tuple->state = INTERPRET_TUPLE_FRAME_AFTER_CHILD;
        he_canonical_argument_next(frame, tuple->idx + 1u,
            tuple->active_attempt.env, variant);
        return;
    }
    he_canonical_pop();
}

static void he_canonical_result_push(HeCanonicalFrame *parent,
    Atom *type, Outcome *seed, OutcomeSet *target) {
    HeCanonicalFrame *frame = he_canonical_push(HE_CANONICAL_RESULT,
        parent->space, parent->arena, NULL, type, parent->fuel,
        NULL, parent->preserve_bindings, target);
    if (frame)
        outcome_set_add_existing(&frame->work, seed);
}

static void he_canonical_resume_result(HeCanonicalFrame *frame) {
    Arena *a = frame->arena;
    if (frame->phase) {
        he_canonical_publish(frame, &frame->child, false);
        he_canonical_pop();
        return;
    }
    Outcome *seed = &frame->work.items[0];
    Atom *preview = outcome_preview_atom(seed);
    if (he_not_reducible(preview) || atom_is_legacy_empty_sentinel(preview) ||
        outcome_atom_is_error(a, seed)) {
        he_canonical_publish(frame, &frame->work, false);
        he_canonical_pop();
        return;
    }
    if (atom_head_symbol_id(preview) == g_builtin_syms.function) {
        frame->phase = 1u;
        he_canonical_instruction_push(frame, outcome_atom_materialize(a, seed),
            frame->type, &seed->env, false, false, &frame->child);
        return;
    }
    Atom *candidate = outcome_atom_materialize(a, seed);
    if (preview->kind != ATOM_EXPR || !preview->expr.len ||
        (!prime_need_atom_has_observable_ref(candidate) &&
         atom_is_constructor_normal_form(frame->space, a, candidate, frame->fuel))) {
        bool defer = false;
        if (he_inert_result_cast(frame->space, a, frame->type, seed,
                frame->preserve_bindings, &frame->child, &defer)) {
            he_canonical_publish(frame, &frame->child, false);
            he_canonical_pop();
            return;
        }
        if (!defer) {
            he_canonical_publish(frame, &frame->work, false);
            he_canonical_pop();
            return;
        }
    }
    frame->phase = 1u;
    he_canonical_push(HE_CANONICAL_CALLER, frame->space, a,
        outcome_atom_materialize_variant_only(a, seed), frame->type,
        frame->fuel, &seed->env, frame->preserve_bindings, &frame->child);
}

static void he_canonical_resume_bind(HeCanonicalFrame *frame) {
    Arena *a = frame->arena;
    if (frame->phase) {
        outcome_set_normalize_visible_frontier(a, &frame->child);
        he_canonical_publish(frame, &frame->child, false);
        he_canonical_pop();
        return;
    }
    if (!eval_completion_step()) {
        he_canonical_pop();
        return;
    }
    if (frame->typed && !frame->type && bindings_logically_empty(&frame->env)) {
        /* The public typed wrapper delegates to the ordinary bind operation. */
        frame->typed = false;
        return;
    }
    if (frame->typed) {
        frame->atom = bindings_apply_if_vars(&frame->env, a, frame->atom);
        if (frame->type)
            frame->type = bindings_apply_if_vars(&frame->env, a, frame->type);
        if (frame->fuel == 0) {
            eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_FUEL);
            he_canonical_pop();
            return;
        }
    } else if (frame->fuel == 0 && frame->atom->kind == ATOM_EXPR &&
               frame->atom->expr.len)
        eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_FUEL);
    Atom *registered = registry_lookup_atom(frame->atom);
    if (registered) {
        outcome_set_add(frame->target, registered, &frame->env);
        he_canonical_pop();
        return;
    }
    frame->atom = materialize_runtime_token(frame->space, a, frame->atom);
    Atom *type = frame->type ? frame->type : atom_undefined_type(a);
    HeTypeDemand demand = frame->typed
        ? eval_atom_type_demand(a, frame->atom, type) : HE_TYPE_INTERPRET;
    if (atom_is_legacy_empty_or_error(frame->atom) || demand == HE_TYPE_KEEP ||
        frame->atom->kind == ATOM_VAR ||
        (!frame->typed && atom_eval_is_immediate_value(frame->atom, frame->fuel)) ||
        (atom_is_symbol_id(type, g_builtin_syms.undefined_type) &&
         eval_has_normal_constructor_value(frame->space, a, frame->atom, frame->fuel))) {
        outcome_set_add(frame->target, frame->atom, &frame->env);
        he_canonical_pop();
    } else if (demand == HE_TYPE_CAST) {
        type_cast_outcomes(frame->space, a, frame->atom, type,
            &frame->env, frame->target);
        he_canonical_pop();
    } else {
        frame->phase = 1u;
        HeCanonicalFrame *call = he_canonical_push(HE_CANONICAL_CALL, frame->space, a,
            frame->atom, frame->typed ? type : NULL,
            frame->fuel > 0 ? frame->fuel - 1 : frame->fuel,
            &frame->env, true, &frame->child);
        if (call)
            call->query_closed = frame->query_closed;
    }
}

/* Atom publication delimits the logical frontier. This is the ResultSet
 * observation of evaluation, rather than the binding-producing operation. */
static void he_canonical_resume_evaluate(HeCanonicalFrame *frame) {
    Arena *a = frame->arena;
    Bindings empty;
    bindings_init(&empty);
    if (frame->phase) {
        outcome_set_normalize_visible_frontier(a, &frame->child);
        for (CettaCount i = 0u; i < frame->child.len; i++)
            outcome_set_add(frame->target,
                outcome_atom_materialize_traced(a, &frame->child.items[i],
                    CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_MATERIALIZE_TOP_LEVEL),
                &empty);
        he_canonical_pop();
        return;
    }
    if (!eval_completion_step()) {
        he_canonical_pop();
        return;
    }
    if (frame->fuel == 0) {
        eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_FUEL);
        he_canonical_pop();
        return;
    }
    Atom *registered = registry_lookup_atom(frame->atom);
    if (registered) {
        outcome_set_add(frame->target, registered, &empty);
        he_canonical_pop();
        return;
    }
    frame->atom = materialize_runtime_token(frame->space, a, frame->atom);
    Atom *type = frame->type ? frame->type : atom_undefined_type(a);
    HeTypeDemand demand = eval_atom_type_demand(a, frame->atom, type);
    if (atom_is_legacy_empty_or_error(frame->atom) || demand == HE_TYPE_KEEP ||
        frame->atom->kind == ATOM_VAR ||
        (atom_is_symbol_id(type, g_builtin_syms.undefined_type) &&
         eval_has_normal_constructor_value(frame->space, a, frame->atom, frame->fuel))) {
        outcome_set_add(frame->target, frame->atom, &empty);
        he_canonical_pop();
    } else if (demand == HE_TYPE_CAST) {
        ResultSet cast;
        result_set_init(&cast);
        type_cast_fn(frame->space, a, frame->atom, type, frame->fuel, &cast);
        for (CettaCount i = 0u; i < cast.len; i++)
            outcome_set_add(frame->target, cast.items[i], &empty);
        result_set_free(&cast);
        he_canonical_pop();
    } else {
        frame->phase = 1u;
        he_canonical_push(HE_CANONICAL_CALL, frame->space, a,
            frame->atom, type, frame->fuel > 0 ? frame->fuel - 1 : frame->fuel,
            NULL, false, &frame->child);
    }
}

static void he_canonical_resume_caller(HeCanonicalFrame *frame) {
    Arena *a = frame->arena;
    if (frame->phase) {
        outcome_set_normalize_visible_frontier(a, &frame->child);
        he_canonical_publish(frame, &frame->child, false);
        he_canonical_pop();
        return;
    }
    frame->phase = 1u;
    Atom *applied = bindings_apply_if_vars(&frame->env, a, frame->atom);
    bool direct = !frame->type || atom_is_symbol_id(frame->type, g_builtin_syms.undefined_type);
    if (!frame->preserve_bindings && bindings_logically_empty(&frame->env) && direct) {
        CettaCallOutcome producer = cetta_call_value(NULL);
        Atom *value = prime_need_atom_has_observable_ref(applied) ? NULL
            : eval_grounded_application_outcome_on(frame->space, a, applied,
                &frame->env, frame->fuel, false, &producer);
        if (producer.kind == CETTA_CALL_INTERRUPTED)
            return;
        if (value && producer.result_form != CETTA_CALL_RESULT_EXPRESSION) {
            emit_completed_native_call(frame->space, a, producer, value,
                frame->type, &frame->env, &frame->child);
            return;
        }
        if (value && (value->kind != ATOM_EXPR || atom_eq(value, applied))) {
            if (!atom_is_legacy_empty_sentinel(value))
                outcome_set_add(&frame->child, value, &frame->env);
            return;
        }
        if (value)
            applied = value;
        if (atom_eval_is_immediate_value(applied, frame->fuel)) {
            outcome_set_add(&frame->child, applied, &frame->env);
            return;
        }
    }
    HeCanonicalFrame *bind = he_canonical_push(HE_CANONICAL_BIND,
        frame->space, a, applied, frame->type, frame->fuel,
        &frame->env, true, &frame->child);
    if (bind) {
        bind->typed = true;
        bind->query_closed = frame->query_closed;
    }
}

static bool he_canonical_control_schedule(Space *s, Arena *a, Atom *atom,
    Atom *type, Atom *source, int fuel, const Bindings *env,
    bool preserve, bool query_closed, bool direct_source, OutcomeSet *os) {
    if (!g_he_canonical_driver || !he_canonical_enabled())
        return false;
    HeCanonicalFrame *frame = he_canonical_push(HE_CANONICAL_CONTROL,
        s, a, atom, type, fuel, env, preserve, os);
    if (frame) {
        frame->live[0] = source;
        frame->query_closed = query_closed;
        frame->selected = direct_source;
    }
    return true;
}

static bool he_canonical_state_schedule(Space *s, Arena *a, Atom *atom,
    int fuel, const Bindings *env, bool preserve, OutcomeSet *os) {
    if (!g_he_canonical_driver || !he_canonical_enabled())
        return false;
    he_canonical_push(HE_CANONICAL_STATE, s, a, atom, NULL,
        fuel, env, preserve, os);
    return true;
}

static void he_canonical_resume_state(HeCanonicalFrame *frame) {
    Arena *a = frame->arena;
    if (!frame->phase) {
        frame->phase = 1u;
        he_canonical_push(HE_CANONICAL_BIND, frame->space, a,
            expr_arg(frame->atom, 0u), NULL, frame->fuel,
            NULL, true, &frame->child);
        return;
    }
    if (frame->phase == 2u) {
        Outcome *reference = &frame->child.items[frame->index];
        if (frame->path_index < frame->work.len) {
            Outcome *value = &frame->work.items[frame->path_index++];
            BindingsBuilder builder;
            if (!bindings_builder_init(&builder, &reference->env))
                return;
            BindingsMergeAttempt attempt;
            if (bindings_builder_merge_or_clone(&builder, &reference->env,
                    &value->env, &attempt)) {
                state_change_publish_value(frame->space, a, frame->atom,
                    frame->live[0], outcome_atom_materialize(a, value),
                    attempt.env, &frame->env, frame->preserve_bindings, frame->target);
                bindings_merge_attempt_finish(&builder, &attempt);
            }
            bindings_builder_free(&builder);
            return;
        }
        outcome_set_free(&frame->work);
        outcome_set_init(&frame->work);
        frame->live[0] = NULL;
        frame->index++;
        frame->phase = 1u;
    }
    if (frame->index >= frame->child.len) {
        he_canonical_pop();
        return;
    }
    Outcome *reference = &frame->child.items[frame->index];
    Atom *state_ref = resolve_registry_refs(a,
        outcome_atom_materialize(a, reference));
    if (state_ref->kind != ATOM_GROUNDED || state_ref->ground.gkind != GV_STATE) {
        outcome_set_add_prefixed(a, frame->target, state_bad_arg_type_error(
            frame->space, a, frame->atom, 1, state_ref), &reference->env,
            &frame->env, frame->preserve_bindings);
        frame->index++;
        return;
    }
    if (atom_head_symbol_id(frame->atom) == g_builtin_syms.get_state) {
        StateCell *cell = payload_resolve_state_read(state_ref->ground.ptr);
        outcome_set_add_prefixed(a, frame->target, payload_rebind_resources(a, cell->value),
            &reference->env, &frame->env, frame->preserve_bindings);
        frame->index++;
        return;
    }
    StateCell *cell = payload_resolve_state_write(a, state_ref->ground.ptr);
    if (!cell) {
        outcome_set_add_prefixed(a, frame->target, atom_error(a, frame->atom,
            atom_symbol(a, "PayloadIsolationFailed")), &reference->env,
            &frame->env, frame->preserve_bindings);
        frame->index++;
        return;
    }
    frame->live[0] = atom_state(a, cell);
    Atom *value_source = bindings_apply_if_vars(&reference->env, a,
        expr_arg(frame->atom, 1u));
    frame->path_index = 0u;
    frame->phase = 2u;
    he_canonical_push(HE_CANONICAL_BIND, frame->space, a,
        value_source, NULL, frame->fuel, NULL, true, &frame->work);
}

static void he_canonical_control_body(HeCanonicalFrame *frame,
    Atom *body, const Bindings *env, bool tail, Atom *type) {
    if (tail) {
        body = resolve_registry_refs(frame->arena, body);
        if (frame->fuel == 0) {
            eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_FUEL);
            return;
        }
    }
    Bindings merged;
    bindings_init(&merged);
    if (bindings_try_merge_live(&merged, &frame->env) &&
        bindings_try_merge_live(&merged, env)) {
        HeCanonicalFrame *body_frame = he_canonical_push(tail ? HE_CANONICAL_CALL : HE_CANONICAL_CALLER,
            frame->space, frame->arena, body, type,
            tail && frame->fuel > 0 ? frame->fuel - 1 : frame->fuel,
            &merged, frame->preserve_bindings, &frame->paths);
        if (body_frame && tail)
            body_frame->query_closed = frame->query_closed;
    }
    bindings_free(&merged);
}

static void he_canonical_resume_control(HeCanonicalFrame *frame) {
    Arena *a = frame->arena;
    SymbolId head = atom_head_symbol_id(frame->atom);
    if (!frame->phase) {
        frame->phase = 1u;
        bool observation = eval_is_reify_head(head);
        if (observation)
            frame->observation = eval_observation_demand_enter(
                (CettaObservationDemand){ .completion = CETTA_OBSERVATION_COMPLETE_BAG },
                frame->live[0]);
        bool atoms_only = observation || head == g_builtin_syms.case_text ||
            head == g_builtin_syms.switch_text || head == g_builtin_syms.assert_text ||
            head == g_builtin_syms.nop || head == g_builtin_syms.bind_bang;
        he_canonical_push(atoms_only ? HE_CANONICAL_EVALUATE : HE_CANONICAL_BIND,
            frame->space, a,
            frame->live[0], NULL, frame->fuel, NULL, true, &frame->child);
        return;
    }
    if (eval_is_reify_head(head)) {
        Atom **items = frame->child.len
            ? arena_alloc(a, sizeof(Atom *) * (size_t)frame->child.len) : NULL;
        CettaExprLen len = 0u;
        for (CettaCount i = 0u; i < frame->child.len; i++) {
            Atom *item = outcome_atom_materialize(a, &frame->child.items[i]);
            if (!atom_is_legacy_empty_sentinel(item))
                items[len++] = item;
        }
        Bindings empty;
        bindings_init(&empty);
        outcome_set_add(frame->target, atom_expr(a, items, len), &empty);
        he_canonical_pop();
        return;
    }
    if (head == g_builtin_syms.bind_bang) {
        Atom *first = frame->child.len
            ? outcome_atom_materialize(a, &frame->child.items[0]) : NULL;
        bind_publish_value(a, frame->atom, first,
            &frame->env, frame->preserve_bindings, frame->target);
        he_canonical_pop();
        return;
    }
    if (head == g_builtin_syms.nop || head == g_builtin_syms.collapse_bind) {
        Bindings empty;
        bindings_init(&empty);
        Atom *result = atom_unit(a);
        if (head == g_builtin_syms.collapse_bind) {
            Atom **pairs = frame->child.len
                ? arena_alloc(a, sizeof(Atom *) * (size_t)frame->child.len) : NULL;
            CettaExprLen count = 0u;
            for (CettaCount i = 0u; i < frame->child.len; i++) {
                Outcome *out = &frame->child.items[i];
                Atom *value = outcome_atom_materialize(a, out);
                if (!atom_is_legacy_empty_sentinel(value))
                    pairs[count++] = atom_expr2(a, value, bindings_capture_value(a, &out->env));
            }
            result = atom_expr(a, pairs, count);
        }
        outcome_set_add_prefixed(a, frame->target, result, &empty,
            &frame->env, frame->preserve_bindings);
        he_canonical_pop();
        return;
    }
    if (head == g_builtin_syms.case_text && !frame->child.len) {
        Bindings empty;
        bindings_init(&empty);
        outcome_set_add(&frame->child, atom_empty(a), &empty);
    }
    if (frame->index >= frame->child.len) {
        he_canonical_publish(frame, &frame->paths, true);
        he_canonical_pop();
        return;
    }
    Outcome *source = &frame->child.items[frame->index];
    Atom *value = outcome_atom_materialize(a, source);
    bool single = frame->child.len == 1u;
    if (head == g_builtin_syms.let && frame->phase == 1u) {
        frame->has_functions = frame->child.len > 0u;
        for (CettaCount i = 0u; i < frame->child.len; i++) {
            Atom *candidate = outcome_atom_materialize(a, &frame->child.items[i]);
            bool error = atom_is_error(candidate);
            frame->has_functions &= error;
            frame->has_non_functions |= !error && !atom_is_legacy_empty_sentinel(candidate);
        }
        frame->phase = 2u;
    }
    if (head == g_builtin_syms.case_text || head == g_builtin_syms.switch_text) {
        frame->index++;
        Atom *branches = expr_arg(frame->atom, 1);
        for (CettaExprIndex i = 0u; branches->kind == ATOM_EXPR &&
                i < branches->expr.len; i++) {
            Atom *branch = branches->expr.elems[i];
            if (branch->kind != ATOM_EXPR || branch->expr.len != 2u)
                continue;
            BindingsBuilder matched;
            if (!bindings_builder_init(&matched, NULL))
                return;
            bool match = eval_case_match_builder(branch->expr.elems[0], value, &matched, a);
            if (match) {
                const Bindings *env = bindings_builder_bindings(&matched);
                Atom *body = bindings_apply_if_vars(env, a, branch->expr.elems[1]);
                Bindings empty;
                bindings_init(&empty);
                he_canonical_control_body(frame, body,
                    single && !frame->preserve_bindings ? &empty : env,
                    single, single ? frame->type : NULL);
            }
            bindings_builder_free(&matched);
            if (match)
                break;
        }
        return;
    }
    if (head == g_builtin_syms.assert_text) {
        frame->index++;
        Bindings empty;
        bindings_init(&empty);
        outcome_set_add(&frame->paths, is_true_atom(value) ? atom_unit(a)
            : atom_error(a, frame->atom, atom_expr3(a, frame->live[0],
                atom_symbol(a, "not"), atom_symbol(a, "True"))), &empty);
        return;
    }
    if (head == g_builtin_syms.if_text) {
        bool truth = is_true_atom(value);
        bool falsity = is_false_atom(value);
        if (truth || falsity) {
            frame->index++;
            Atom *body = truth ? expr_arg(frame->atom, 1)
                : expr_nargs(frame->atom) == 3u ? expr_arg(frame->atom, 2)
                : legacy_empty_sentinel(a);
            body = bindings_apply_if_vars(&source->env, a, body);
            he_canonical_control_body(frame, body, &source->env,
                single, frame->type);
        } else if (atom_is_legacy_empty_sentinel(value)) {
            frame->index++;
        } else if (atom_is_error(value)) {
            frame->index++;
            outcome_set_add_existing(&frame->paths, source);
        } else if (value->kind == ATOM_VAR) {
            BindingsBuilder refined;
            bool initialized = bindings_builder_init(&refined, &source->env);
            unsigned branch = (unsigned)frame->head_index++;
            if (frame->head_index == 2u) {
                frame->index++;
                frame->head_index = 0u;
            }
            if (initialized && bindings_builder_add_var_fresh(&refined, value,
                    atom_symbol_id(a, branch ? g_builtin_syms.false_text
                        : g_builtin_syms.true_text))) {
                Atom *body = !branch ? expr_arg(frame->atom, 1)
                    : expr_nargs(frame->atom) == 3u ? expr_arg(frame->atom, 2)
                    : legacy_empty_sentinel(a);
                he_canonical_control_body(frame, body,
                    bindings_builder_bindings(&refined), false, frame->type);
            }
            if (initialized)
                bindings_builder_free(&refined);
        } else {
            frame->index++;
            Atom **types = NULL;
            uint32_t count = eval_get_atom_types_profiled(frame->space, a, value, &types);
            Atom *boolean = atom_symbol(a, "Bool");
            bool concrete = false, may_bool = false;
            for (uint32_t i = 0u; i < count; i++) {
                if (atom_is_symbol_id(types[i], g_builtin_syms.undefined_type) ||
                    atom_is_meta_type(types[i]))
                    continue;
                if (atom_eq_fast(types[i], boolean))
                    may_bool = true;
                else
                    concrete = true;
            }
            free(types);
            if ((concrete || value->kind == ATOM_GROUNDED) && !may_bool)
                outcome_set_add(&frame->paths, bad_arg_type_error(frame->space, a,
                    frame->atom, 1, boolean, value), &source->env);
            else {
                Atom *elems[] = { frame->atom->expr.elems[0], value,
                    expr_arg(frame->atom, 1), expr_nargs(frame->atom) == 3u
                        ? expr_arg(frame->atom, 2) : NULL };
                outcome_set_add(&frame->paths,
                    atom_expr(a, elems, frame->atom->expr.len), &source->env);
            }
        }
        return;
    }
    assert(head == g_builtin_syms.let);
    frame->index++;
    if (atom_is_legacy_empty_sentinel(value))
        return;
    if ((frame->selected && atom_is_error(value)) || frame->has_functions) {
        if (frame->selected && frame->has_non_functions)
            return;
        Bindings empty;
        bindings_init(&empty);
        outcome_set_add(&frame->paths, value, &empty);
        return;
    }
    Atom *pattern = expr_arg(frame->atom, 0);
    Atom *body = expr_arg(frame->atom, 2);
    BindingsBuilder matched;
    if (!bindings_builder_init(&matched, &source->env))
        return;
    bool admitted = pattern->kind == ATOM_VAR
        ? bindings_builder_add_var_fresh(&matched, pattern, value)
        : match_atoms_builder(value, pattern, &matched, a);
    if (admitted) {
        Bindings visible;
        if (bindings_project_body_visible_env(a, body,
                bindings_builder_bindings(&matched), &visible)) {
            body = bindings_apply_projected_body_visible(&visible, a, body);
            Bindings empty;
            bindings_init(&empty);
            bool retained = frame->selected
                ? !(pattern->kind == ATOM_VAR &&
                    !atom_contains_vars(expr_arg(frame->atom, 2u)))
                : !single || (frame->preserve_bindings && !frame->query_closed);
            he_canonical_control_body(frame, body, retained
                ? bindings_builder_bindings(&matched) : &empty,
                single && !frame->selected,
                single && !frame->selected ? frame->type : NULL);
            bindings_free(&visible);
        }
    }
    bindings_builder_free(&matched);
}

static bool he_canonical_dispatch_schedule(Space *s, Arena *a, Atom *atom,
    Atom *type, int fuel, const Bindings *env, bool preserve, OutcomeSet *os) {
    if (!g_he_canonical_driver || !he_canonical_enabled())
        return false;
    bool closed = he_canonical_query_closed(eval_gc_enabled() &&
        !atom_contains_vars(atom));
    HeCanonicalFrame *frame = he_canonical_push(HE_CANONICAL_DISPATCH,
        s, a, atom, type, fuel, env, preserve, os);
    if (frame)
        frame->query_closed = closed;
    return true;
}

static bool he_canonical_project_contract(HeCanonicalFrame *frame,
    Outcome *head, Outcome *path, Bindings *out) {
    MatchVisibleVarSet visible;
    match_visible_var_set_init(&visible);
    collect_match_visible_vars_rec(frame->atom, &visible);
    collect_match_visible_vars_rec(frame->type, &visible);
    collect_match_visible_vars_rec(outcome_atom_materialize(frame->arena, head), &visible);
    BindingsBuilder acquired;
    bool initialized = bindings_builder_init(&acquired, &frame->env);
    bool ok = initialized && bindings_builder_merge_commit(&acquired, &head->env) &&
        bindings_builder_merge_commit(&acquired, &path->env);
    Bindings projected;
    bindings_init(&projected);
    ok = ok && project_match_visible_bindings(frame->arena, &visible,
        bindings_builder_bindings(&acquired), &projected);
    if (initialized)
        bindings_builder_free(&acquired);
    match_visible_var_set_free(&visible);
    BindingsBuilder merged;
    initialized = ok && bindings_builder_init(&merged, &frame->env);
    ok = initialized && bindings_builder_merge_commit(&merged, &projected);
    bindings_free(&projected);
    if (ok)
        bindings_builder_take(&merged, out);
    if (initialized)
        bindings_builder_free(&merged);
    return ok;
}

static void he_canonical_tail_push(HeCanonicalFrame *frame,
    Atom *body, Atom *type, const Bindings *env, OutcomeSet *target) {
    body = resolve_registry_refs(frame->arena, body);
    if (he_not_reducible(body)) {
        outcome_set_add(target, body, env);
    } else if (atom_head_symbol_id(body) == g_builtin_syms.function) {
        he_canonical_instruction_push(frame, body, type,
            env, false, false, target);
    } else if (frame->fuel == 0) {
        eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_FUEL);
    } else {
        HeCanonicalFrame *tail = he_canonical_push(HE_CANONICAL_CALL, frame->space, frame->arena,
            body, type ? type : frame->type,
            frame->fuel > 0 ? frame->fuel - 1 : frame->fuel,
            env, frame->preserve_bindings, target);
        if (tail)
            tail->query_closed = frame->query_closed;
    }
}

static void he_canonical_dispatch_call(HeCanonicalFrame *frame, Outcome *call) {
    Arena *a = frame->arena;
    Atom *source = outcome_atom_materialize(a, call);
    Bindings *env = &call->env;
    if (atom_is_legacy_empty_or_error(source)) {
        outcome_set_add_existing(&frame->paths, call);
        return;
    }
    source = resolve_registry_refs(a, source);
    Atom *return_type = frame->selected ? frame->live[1] : frame->type;
    if (return_type)
        return_type = bindings_apply_if_vars(env, a, return_type);
    if (frame->selected && (he_minimal_instruction(source) ||
        atom_head_symbol_id(source) == g_builtin_syms.superpose) &&
        atom_head_symbol_id(source) != g_builtin_syms.call_native) {
        he_canonical_instruction_push(frame, source, return_type, env,
            true, frame->typed, &frame->paths);
        return;
    }
    if (source->kind == ATOM_EXPR && source->expr.len) {
        Atom *head = source->expr.elems[0];
        CettaExprLen nargs = source->expr.len - 1u;
        Atom *tail = NULL, *tail_type = NULL;
        Bindings tail_env;
        bindings_init(&tail_env);
        bool foreign = dispatch_foreign_outcomes(frame->space, a, head,
            source->expr.elems + 1u, (uint32_t)nargs, return_type, frame->fuel,
            env, true, frame->preserve_bindings, &tail, &tail_type,
            &tail_env, &frame->paths);
        if (foreign) {
            if (tail)
                he_canonical_push(HE_CANONICAL_CALLER, frame->space, a,
                    tail, tail_type, frame->fuel, &tail_env,
                    frame->preserve_bindings, &frame->paths);
            bindings_free(&tail_env);
            return;
        }
        bindings_free(&tail_env);
        if (is_capture_closure(head)) {
            if (g_hyperpose_thread_unsafe_requested &&
                eval_mark_hyperpose_thread_unsafe()) {
                outcome_set_add(&frame->paths,
                    legacy_empty_sentinel(a), env);
            } else if (nargs != 1u) {
                outcome_set_add(&frame->paths, atom_error(a, source,
                    atom_symbol(a, "IncorrectNumberOfArguments")), env);
            } else {
                CaptureClosure *closure = head->ground.ptr;
                he_canonical_push(HE_CANONICAL_CAPTURE,
                    closure->space_ptr, a, source, NULL, frame->fuel,
                    env, frame->preserve_bindings, &frame->paths);
            }
            return;
        }
        if (is_petta_runtime_callable(head)) {
            (void)dispatch_petta_callable_outcomes(frame->space, a, head,
                source->expr.elems + 1u, (uint32_t)nargs, frame->fuel,
                env, frame->preserve_bindings, &frame->paths);
            return;
        }
        if (head->kind == ATOM_SYMBOL && is_grounded_op(head->sym_id)) {
            CettaCallOutcome produced;
            Atom *value = dispatch_native_call(frame->space, a, head,
                source->expr.elems + 1u, nargs, &produced)
                ? eval_call_outcome_atom(a, produced) : NULL;
            if (value) {
                if (produced.result_form != CETTA_CALL_RESULT_EXPRESSION ||
                    eval_grounded_result_is_final())
                    emit_completed_native_call(frame->space, a, produced,
                        value, return_type, env, &frame->paths);
                else
                    if (frame->typed)
                        he_canonical_tail_push(frame, value, return_type,
                            env, &frame->paths);
                    else
                        he_canonical_push(HE_CANONICAL_CALLER, frame->space, a,
                            value, return_type, frame->fuel, env,
                            frame->preserve_bindings, &frame->paths);
                return;
            }
        }
    }
    /* Matching completes before bodies run. Every pending occurrence owns
     * its fresh bindings; later calls acquire the source's current revision. */
    OutcomeSet pending;
    outcome_set_init(&pending);
    HeRawQuery query = { .arena = a, .prefix = env, .outcomes = &pending };
    CettaCount matched = query_equations_visit(frame->space, source, a,
        he_raw_query_result, &query);
    if (!matched) {
        outcome_set_add_existing(&frame->paths, call);
        outcome_set_free(&pending);
        return;
    }
    if (frame->typed && pending.len == 1u) {
        SearchContext context;
        if (search_context_init(&context, env, NULL)) {
            Atom *body = NULL, *type = NULL;
            Bindings continuation;
            bindings_init(&continuation);
            Outcome *seed = &pending.items[0];
            if (query_delayed_result_apply_single_tail(NULL, &context,
                    env, a, return_type, frame->preserve_bindings,
                    seed->atom, &seed->env, &seed->variant,
                    &body, &type, &continuation))
                he_canonical_tail_push(frame, body, type,
                    &continuation, &frame->paths);
            bindings_free(&continuation);
            search_context_free(&context);
        }
        outcome_set_free(&pending);
        return;
    }
    /* Save this complete producer frontier in a child return sequencer. */
    HeCanonicalFrame *result = he_canonical_push(HE_CANONICAL_RESULT,
        frame->space, a, NULL, return_type, frame->fuel, NULL,
        frame->preserve_bindings, &frame->paths);
    if (result) {
        for (CettaCount i = 0u; i < pending.len; i++)
            outcome_set_add_existing_move(&result->work, &pending.items[i]);
        result->phase = 2u;
    }
    outcome_set_free(&pending);
}

static void he_canonical_resume_dispatch(HeCanonicalFrame *frame) {
    Arena *a = frame->arena;
    if (frame->phase == 0u) {
        Atom *op = frame->atom->expr.elems[0];
        uint32_t nargs = 0u;
        if (!expr_nargs_u32(frame->atom, &nargs)) {
            outcome_set_add(frame->target,
                expr_arity_too_large_error(a, frame->atom), &frame->env);
            he_canonical_pop();
            return;
        }
        bool total_structural_eq = atom_is_symbol_id(op, g_builtin_syms.op_eq) &&
            nargs == 2u && active_profile_uses_total_structural_eq();
        frame->type_count = eval_get_atom_types_profiled(frame->space, a, op, &frame->types);
        if (frame->types)
            eval_gc_root_frame_enter_outcome_continuation(&frame->type_root,
                frame->types, frame->type_count, NULL, NULL);
        ApplicabilityErrors all_errors;
        applicability_errors_init(&all_errors);
        for (uint32_t i = 0u; i < frame->type_count; i++) {
            if (!is_function_type(frame->types[i])) {
                frame->has_non_functions = true;
                continue;
            }
            frame->has_functions = true;
            Atom *fresh = cetta_instantiate_frame_syntax(a, frame->types[i]);
            ApplicabilityErrors errors;
            applicability_errors_init(&errors);
            ApplicabilityTypes contracts;
            applicability_types_init(&contracts);
            bool ok = check_function_applicable(frame->atom, fresh,
                frame->type ? frame->type : atom_undefined_type(a),
                frame->space, a, frame->fuel, frame->type_count > 1u,
                &errors, &contracts, &frame->work);
            applicability_types_free(&contracts);
            if (ok) {
                frame->selected = true;
                frame->live[0] = fresh;
                applicability_errors_free(&errors);
                break;
            }
            for (uint32_t j = 0u; j < errors.len; j++)
                applicability_errors_push(&all_errors, errors.items[j]);
            applicability_errors_free(&errors);
        }
        if (!frame->selected && frame->has_functions && !total_structural_eq) {
            if (!frame->has_non_functions || eval_type_check_auto_enabled())
                for (uint32_t i = 0u; i < all_errors.len; i++)
                    outcome_set_add(frame->target, all_errors.items[i], &frame->env);
            if (!frame->has_non_functions) {
                applicability_errors_free(&all_errors);
                he_canonical_pop();
                return;
            }
        }
        applicability_errors_free(&all_errors);
        if (frame->selected) {
            frame->phase = 1u;
        } else {
            frame->phase = 5u;
            he_canonical_arguments_push(frame, NULL, frame->atom->expr.elems,
                NULL, frame->atom->expr.len, &frame->env, &frame->child, false);
            return;
        }
    }
    if (frame->phase == 1u) {
        if (frame->path_index >= frame->work.len) {
            he_canonical_pop();
            return;
        }
        frame->live[1] = he_type_result_demand(a,
            outcome_atom_materialize(a, &frame->work.items[frame->path_index]));
        frame->head_index = 0u;
        outcome_set_free(&frame->child);
        outcome_set_init(&frame->child);
        frame->phase = 2u;
        he_canonical_argument_schedule(frame->space, a, frame->live[0],
            frame->atom->expr.elems[0], frame->fuel, &frame->env, &frame->child);
        return;
    }
    if (frame->phase == 2u) {
        if (frame->head_index >= frame->child.len) {
            frame->path_index++;
            frame->phase = 1u;
            return;
        }
        Outcome *head = &frame->child.items[frame->head_index];
        Atom *op = outcome_atom_materialize(a, head);
        if (atom_is_legacy_empty_or_error(op) && !atom_eq(op, frame->atom->expr.elems[0])) {
            outcome_set_add_prefixed_outcome(a, frame->target, head,
                &frame->env, frame->preserve_bindings);
            frame->head_index++;
            return;
        }
        Bindings applicable;
        bindings_init(&applicable);
        if (!he_canonical_project_contract(frame, head,
                &frame->work.items[frame->path_index], &applicable)) {
            bindings_free(&applicable);
            frame->head_index++;
            return;
        }
        CettaExprLen nargs = frame->atom->expr.len - 1u;
        Atom **domains = nargs ? cetta_malloc(sizeof(Atom *) * (size_t)nargs) : NULL;
        for (CettaExprIndex i = 0u; i < nargs; i++)
            domains[i] = function_domain_refine(
                &frame->work.items[frame->path_index].env, a,
                frame->live[0]->expr.elems[i + 1u]);
        outcome_set_free(&frame->paths);
        outcome_set_init(&frame->paths);
        frame->index = 0u;
        frame->phase = 3u;
        he_canonical_arguments_push(frame, op, frame->atom->expr.elems + 1u,
            domains, nargs, &applicable, &frame->paths, true);
        free(domains);
        bindings_free(&applicable);
        return;
    }
    if (frame->phase == 3u) {
        if (frame->index >= frame->paths.len) {
            /* Completed results are in the target, not an applicability
             * accumulator: a later empty branch cannot erase earlier ones. */
            frame->head_index++;
            frame->phase = 2u;
            return;
        }
        Outcome *call = &frame->paths.items[frame->index++];
        /* The call frontier and return frontier must be different owners. */
        HeCanonicalFrame *delivery = he_canonical_push(HE_CANONICAL_DISPATCH,
            frame->space, a, frame->atom, frame->live[1], frame->fuel,
            &frame->env, frame->preserve_bindings, frame->target);
        if (delivery) {
            delivery->selected = true;
            delivery->query_closed = frame->query_closed;
            delivery->typed = frame->type_count == 1u &&
                frame->work.len == 1u && frame->child.len == 1u &&
                frame->paths.len == 1u;
            delivery->live[1] = frame->live[1];
            outcome_set_add_existing(&delivery->child, call);
            delivery->phase = 6u;
        }
        return;
    }
    if (frame->phase == 5u) {
        if (frame->index >= frame->child.len) {
            he_canonical_publish(frame, &frame->paths, true);
            he_canonical_pop();
            return;
        }
        frame->typed = frame->child.len == 1u;
        he_canonical_dispatch_call(frame, &frame->child.items[frame->index++]);
        return;
    }
    if (frame->phase == 6u) {
        frame->phase = 7u;
        he_canonical_dispatch_call(frame, &frame->child.items[0]);
        return;
    }
    if (frame->phase == 7u) {
        he_canonical_publish(frame, &frame->paths, true);
        he_canonical_pop();
    }
}

static bool he_canonical_match_schedule(Space *s, Arena *a, Atom *atom,
    Atom *type, int fuel, const Bindings *env, bool preserve,
    OutcomeSet *os) {
    if (!g_he_canonical_driver || !he_canonical_enabled() ||
        atom_head_symbol_id(atom) != g_builtin_syms.match)
        return false;
    CettaExprLen nargs = expr_nargs(atom);
    if (nargs == 2u) {
        Atom *conjunction = resolve_registry_refs(a, expr_arg(atom, 0));
        if (conjunction->kind != ATOM_EXPR || conjunction->expr.len < 2u ||
            !atom_is_symbol_id(conjunction->expr.elems[0], g_builtin_syms.comma))
            return false;
    } else if (nargs == 3u) {
        CettaMorkSpaceHandle *bridge = NULL;
        Atom *space_ref = resolve_registry_refs(a, expr_arg(atom, 0));
        if (g_library_context && cetta_library_lookup_explicit_mork_bridge(
                g_library_context, space_ref, &bridge) && bridge)
            return false;
    } else
        return false;
    bool closed = he_canonical_query_closed(eval_gc_enabled() &&
        !atom_contains_vars(atom));
    HeCanonicalFrame *frame = he_canonical_push(HE_CANONICAL_MATCH,
        s, a, atom, type, fuel, env, preserve, os);
    if (frame) {
        frame->sourced_match = nargs == 2u;
        frame->query_closed = closed;
    }
    return true;
}

static void he_canonical_match_error(HeCanonicalFrame *frame, const char *reason) {
    outcome_set_add(frame->target, space_arg_error(frame->arena,
        frame->atom, reason), &frame->env);
    he_canonical_pop();
}

static void he_canonical_match_initialize(HeCanonicalFrame *frame) {
    Arena *a = frame->arena;
    Atom *conjunction = frame->sourced_match
        ? resolve_registry_refs(a, expr_arg(frame->atom, 0)) : NULL;
    CettaExprLen capacity = frame->sourced_match
        ? conjunction->expr.len - 1u : 16u;
    HeCanonicalRegisters *registers = cetta_malloc(sizeof(*registers));
    memset(registers, 0, sizeof(*registers));
    registers->len = frame->sourced_match ? capacity : 1u;
    registers->source = cetta_malloc(sizeof(Atom *) * (size_t)capacity);
    registers->types = cetta_malloc(sizeof(Atom *) * (size_t)capacity);
    registers->prefix = cetta_malloc(sizeof(Atom *) * (size_t)capacity);
    memset(registers->source, 0, sizeof(Atom *) * (size_t)capacity);
    memset(registers->types, 0, sizeof(Atom *) * (size_t)capacity);
    memset(registers->prefix, 0, sizeof(Atom *) * (size_t)capacity);
    frame->match_spaces = cetta_malloc(sizeof(Space *) * (size_t)capacity);
    memset(frame->match_spaces, 0, sizeof(Space *) * (size_t)capacity);
    frame->registers = registers;
    frame->owns_registers = true;
    eval_gc_root_frame_enter_function_args_machine(&registers->root,
        &registers->op, registers->source, (size_t)capacity,
        registers->types, (size_t)capacity, registers->prefix,
        (size_t)capacity, &frame->work);
    if (frame->sourced_match) {
        for (CettaExprIndex i = 0u; i < capacity; i++) {
            Atom *leg = conjunction->expr.elems[i + 1u];
            if (leg->kind != ATOM_EXPR || leg->expr.len != 2u) {
                he_canonical_match_error(frame,
                    "each sourced conjunct must be (space pattern)");
                return;
            }
            registers->source[i] = resolve_registry_refs(a, leg->expr.elems[1]);
            registers->types[i] = leg->expr.elems[0];
        }
        frame->live[0] = resolve_registry_refs(a, expr_arg(frame->atom, 1));
    } else {
        registers->source[0] = resolve_registry_refs(a, expr_arg(frame->atom, 1));
        registers->types[0] = expr_arg(frame->atom, 0);
        frame->live[0] = resolve_registry_refs(a, expr_arg(frame->atom, 2));
    }
    frame->phase = 1u;
}

static void he_canonical_match_plan(HeCanonicalFrame *frame) {
    HeCanonicalRegisters *registers = frame->registers;
    Arena *a = frame->arena;
    if (!frame->sourced_match) {
        Atom *error = guard_mork_space_syntax(a, frame->atom,
            frame->match_spaces[0], "match", "mork:match");
        if (error) {
            outcome_set_add(frame->target, error, &frame->env);
            he_canonical_pop();
            return;
        }
    }
    if (!frame->sourced_match && !match_query_is_connective(registers->source[0])) {
        MatchStep steps[16];
        uint32_t count = 1u;
        steps[0] = (MatchStep){frame->match_spaces[0], registers->source[0]};
        Atom *body = frame->live[0];
        while (count < 16u && body->kind == ATOM_EXPR && body->expr.len == 4u &&
                atom_is_symbol_id(body->expr.elems[0], g_builtin_syms.match)) {
            Atom *pattern = resolve_registry_refs(a, body->expr.elems[2]);
            if (match_query_is_connective(pattern))
                break;
            Space *space = g_registry ? resolve_registry_space_payload(g_registry,
                resolve_registry_refs(a, body->expr.elems[1])) : NULL;
            steps[count++] = (MatchStep){space ? space : frame->space, pattern};
            body = body->expr.elems[3];
        }
        match_chain_order_steps(steps, &count);
        registers->len = count;
        frame->live[0] = body;
        for (uint32_t i = 0u; i < count; i++) {
            registers->source[i] = steps[i].pattern;
            frame->match_spaces[i] = steps[i].space;
            registers->prefix[i] = atom_space(a, steps[i].space);
        }
    }
    Bindings empty;
    bindings_init(&empty);
    outcome_set_add(&frame->work, atom_unit(a), &empty);
    CettaExprLen count = registers->len;
    /* A fused chain completes its prefix first. Its final read window is
     * acquired separately for each prefix, exactly where the source adapter
     * acquires it; a completed window is never silently reopened. */
    CettaExprLen prefix = !frame->sourced_match && count > 1u ? count - 1u : count;
    for (CettaExprIndex leg = 0u; leg < prefix; leg++) {
        OutcomeSet next;
        outcome_set_init(&next);
        for (CettaCount i = 0u; i < frame->work.len; i++) {
            BindingSet rows;
            if (!match_query_bindings(frame->match_spaces[leg], a,
                    registers->source[leg], &frame->work.items[i].env, &rows)) {
                eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);
                outcome_set_free(&next);
                return;
            }
            for (CettaIndex j = 0u; j < rows.len; j++)
                outcome_set_add(&next, atom_unit(a), &rows.items[j]);
            binding_set_free(&rows);
        }
        outcome_set_free(&frame->work);
        outcome_set_init(&frame->work);
        for (CettaCount i = 0u; i < next.len; i++)
            outcome_set_add_existing_move(&frame->work, &next.items[i]);
        outcome_set_free(&next);
    }
    frame->phase = 3u;
    frame->index = 0u;
}

/* Resolve an authored space producer through the same continuation driver.
 * Algebra operands are sequenced without nesting the native resolver in C. */
static void he_canonical_resume_space(HeCanonicalFrame *frame) {
    Arena *a = frame->arena;
    if (!g_registry) {
        he_canonical_pop();
        return;
    }
    if (!frame->phase) {
        if (petta_space_expression_is_algebra(frame->atom)) {
            frame->phase = 2u;
            frame->index = 1u;
            he_canonical_push(HE_CANONICAL_SPACE, frame->space, a,
                frame->atom->expr.elems[frame->index++], NULL,
                frame->fuel, NULL, false, &frame->child);
            return;
        }
        Atom *direct = resolve_registry_refs(a, frame->atom);
        Space *space = resolve_registry_space_payload(g_registry, direct);
        if (space) {
            outcome_set_add(frame->target, atom_space(a, space), &frame->env);
            he_canonical_pop();
            return;
        }
        frame->phase = 1u;
        he_canonical_push(HE_CANONICAL_EVALUATE, frame->space, a,
            frame->atom, NULL, frame->fuel, NULL, false, &frame->child);
        return;
    }
    Space *space = NULL;
    for (CettaCount i = 0u; !space && i < frame->child.len; i++)
        space = resolve_registry_space_payload(g_registry,
            outcome_atom_materialize(a, &frame->child.items[i]));
    if (!space) {
        he_canonical_pop();
        return;
    }
    if (frame->phase == 2u) {
        if (frame->live[0]) {
            Space *previous = resolve_registry_space_payload(g_registry, frame->live[0]);
            Space *folded = space_algebra_fresh_result(a);
            if (!folded) {
                eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);
                return;
            }
            if (atom_is_symbol_id(frame->atom->expr.elems[0], g_builtin_syms.comma))
                space_algebra_add_meet(folded, previous, space);
            else
                space_algebra_add_union(folded, previous, space);
            space = folded;
        }
        frame->live[0] = atom_space(a, space);
        outcome_set_free(&frame->child);
        outcome_set_init(&frame->child);
        if (frame->index < frame->atom->expr.len) {
            he_canonical_push(HE_CANONICAL_SPACE, frame->space, a,
                frame->atom->expr.elems[frame->index++], NULL,
                frame->fuel, NULL, false, &frame->child);
            return;
        }
    }
    outcome_set_add(frame->target, atom_space(a, space), &frame->env);
    he_canonical_pop();
}

static void he_canonical_resume_match(HeCanonicalFrame *frame) {
    Arena *a = frame->arena;
    if (!frame->phase) {
        he_canonical_match_initialize(frame);
        return;
    }
    HeCanonicalRegisters *registers = frame->registers;
    if (frame->phase == 1u) {
        if (frame->index == registers->len) {
            he_canonical_match_plan(frame);
            return;
        }
        Atom *ref = resolve_registry_refs(a, registers->types[frame->index]);
        Space *space = g_registry ? resolve_registry_space_payload(g_registry, ref) : NULL;
        if (!g_registry && !frame->sourced_match)
            space = frame->space;
        if (!space) {
            frame->phase = 2u;
            he_canonical_push(HE_CANONICAL_SPACE, frame->space, a,
                ref, NULL, frame->fuel, NULL, false, &frame->child);
            return;
        }
        frame->match_spaces[frame->index] = space;
        registers->prefix[frame->index++] = atom_space(a, space);
        return;
    }
    if (frame->phase == 2u) {
        Space *space = NULL;
        for (CettaCount i = 0u; g_registry && !space && i < frame->child.len; i++)
            space = resolve_registry_space_payload(g_registry,
                outcome_atom_materialize(a, &frame->child.items[i]));
        if (!space) {
            he_canonical_match_error(frame, frame->sourced_match
                ? "sourced conjunct space did not resolve"
                : "match expects a space as the first argument");
            return;
        }
        frame->match_spaces[frame->index] = space;
        registers->prefix[frame->index++] = atom_space(a, space);
        outcome_set_free(&frame->child);
        outcome_set_init(&frame->child);
        frame->phase = 1u;
        return;
    }
    if (frame->phase == 3u) {
        if (frame->index == frame->work.len) {
            frame->phase = 5u;
            frame->index = 0u;
            return;
        }
        Outcome *prefix = &frame->work.items[frame->index++];
        if (!frame->sourced_match && registers->len > 1u) {
            CettaExprIndex last = registers->len - 1u;
            BindingSet rows;
            if (!match_query_bindings(frame->match_spaces[last], a,
                    registers->source[last], &prefix->env, &rows)) {
                eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);
                return;
            }
            for (CettaIndex i = 0u; i < rows.len; i++)
                outcome_set_add(&frame->child, atom_unit(a), &rows.items[i]);
            binding_set_free(&rows);
        } else
            outcome_set_add_existing(&frame->child, prefix);
        frame->head_index = 0u;
        frame->phase = 4u;
    }
    if (frame->phase == 4u) {
        if (frame->head_index == frame->child.len) {
            outcome_set_free(&frame->child);
            outcome_set_init(&frame->child);
            frame->phase = 3u;
            return;
        }
        Outcome *row = &frame->child.items[frame->head_index++];
        MatchVisibleVarSet visible;
        match_visible_var_set_init(&visible);
        bool valid = true;
        for (CettaExprIndex i = 0u; valid && i < registers->len; i++)
            valid = collect_match_visible_vars_rec(registers->source[i], &visible);
        Bindings projected;
        bindings_init(&projected);
        valid = valid && project_match_visible_bindings(a, &visible, &row->env, &projected);
        match_visible_var_set_free(&visible);
        if (!valid) {
            bindings_free(&projected);
            return;
        }
        Atom *body = bindings_apply_if_vars(&projected, a, frame->live[0]);
        Space *target = match_result_target_space(body, a);
        bool snapshot = !frame->sourced_match && !frame->preserve_bindings &&
            match_result_targets_any_query_space(target, frame->match_spaces,
                (uint32_t)registers->len);
        if (snapshot) {
            SymbolId head = atom_head_symbol_id(body);
            if (head == g_builtin_syms.remove_atom || head == g_builtin_syms.mork_remove_atom) {
                Atom *restored = space_match_backend_restore_opening_provenance(a, body);
                if (restored)
                    body = restored;
            }
            Bindings empty;
            bindings_init(&empty);
            outcome_set_add(&frame->saved, body, &empty);
        } else {
            if (!frame->preserve_bindings)
                (void)deferred_space_set_add(&frame->deferred_spaces, target);
            HeCanonicalFrame *body_frame = he_canonical_push(HE_CANONICAL_CALLER, frame->space, a,
                body, NULL, frame->fuel, &projected,
                frame->preserve_bindings, &frame->paths);
            /* Template substitution continues the established invocation.
             * A local binder in its syntax is not a new caller variable.
             * Keep the incoming matcher refinements, while carrying the
             * original scope through interpretation and return. */
            if (body_frame)
                body_frame->query_closed = frame->query_closed;
        }
        bindings_free(&projected);
        return;
    }
    if (frame->phase == 5u) {
        if (frame->index == frame->saved.len) {
            he_canonical_publish(frame, &frame->paths, false);
            he_canonical_pop();
            return;
        }
        Outcome *saved = &frame->saved.items[frame->index++];
        he_canonical_push(HE_CANONICAL_CALLER, frame->space, a, saved->atom,
            NULL, frame->fuel, &saved->env, false, &frame->paths);
    }
}

static void he_canonical_resume_instruction(HeCanonicalFrame *frame) {
    Arena *a = frame->arena;
    SymbolId head = atom_head_symbol_id(frame->atom);
    bool source_chain = frame->typed && head == g_builtin_syms.chain &&
        expr_nargs(frame->atom) == 3u && expr_arg(frame->atom, 1)->kind == ATOM_VAR &&
        !he_minimal_instruction(expr_arg(frame->atom, 0)) &&
        !active_profile_uses_rust_he_compat_semantics();
    if (!frame->phase) {
        if (source_chain) {
            frame->phase = 1u;
            he_canonical_push(HE_CANONICAL_CALLER, frame->space, a,
                expr_arg(frame->atom, 0), NULL, frame->fuel,
                &frame->env, true, &frame->child);
        } else {
            frame->phase = 2u;
            he_instruction_push(&frame->instructions,
                frame->typed && head == g_builtin_syms.superpose
                    ? HE_INSTRUCTION_RAW : HE_INSTRUCTION_RUN,
                frame->space, a, frame->atom, frame->fuel,
                &frame->env, &frame->work);
        }
        return;
    }
    if (frame->phase == 1u) {
        if (frame->index >= frame->child.len) {
            frame->phase = 3u;
            frame->index = 0u;
        } else {
            Outcome *source = &frame->child.items[frame->index++];
            Atom *value = outcome_atom_materialize(a, source);
            Atom *body = bindings_apply_if_vars(&source->env, a,
                expr_arg(frame->atom, 2));
            body = instantiate_template_variable_once(a, body,
                expr_arg(frame->atom, 1)->var_id, value);
            frame->phase = 2u;
            he_instruction_push(&frame->instructions, HE_INSTRUCTION_RUN,
                frame->space, a, body, frame->fuel, &source->env, &frame->work);
        }
        return;
    }
    if (frame->phase == 2u) {
        if (frame->instructions.top) {
            he_instruction_resume(&frame->instructions);
            return;
        }
        frame->phase = source_chain ? 1u : 3u;
        return;
    }
    if (frame->phase == 3u) {
        if (frame->typed && frame->selected && frame->work.len == 1u &&
            !eval_unwinding()) {
            Outcome *source = &frame->work.items[0];
            Atom *value = outcome_atom_materialize(a, source);
            if (value && !atom_is_legacy_empty_sentinel(value) &&
                !he_not_reducible(value) &&
                atom_head_symbol_id(value) != g_builtin_syms.function &&
                eval_atom_type_demand(a, value, frame->type) == HE_TYPE_INTERPRET) {
                frame->phase = 5u;
                he_canonical_tail_push(frame, value, frame->type,
                    &source->env, &frame->paths);
                return;
            }
        }
        frame->phase = 4u;
        frame->index = 0u;
    }
    if (frame->phase == 4u) {
        if (frame->index >= frame->work.len) {
            frame->phase = 5u;
        } else {
            Outcome *source = &frame->work.items[frame->index++];
            Atom *value = outcome_atom_materialize(a, source);
            if (!value || (frame->typed && atom_is_legacy_empty_sentinel(value)))
                return;
            if (he_not_reducible(value)) {
                if (frame->typed)
                    value = active_profile_uses_rust_he_compat_semantics() ||
                        (head != g_builtin_syms.eval && head != g_builtin_syms.evalc)
                        ? frame->atom : expr_arg(frame->atom, 0);
                outcome_set_add(&frame->paths, value, &source->env);
            } else {
                he_canonical_push(HE_CANONICAL_CALLER, frame->space, a,
                    value, frame->type, frame->fuel, &source->env,
                    frame->preserve_bindings, &frame->paths);
            }
            return;
        }
    }
    if (frame->phase == 5u) {
        he_canonical_publish(frame, &frame->paths, false);
        he_canonical_pop();
    }
}

static void he_canonical_resume(HeCanonicalFrame *frame) {
    switch (frame->kind) {
    case HE_CANONICAL_BIND: he_canonical_resume_bind(frame); break;
    case HE_CANONICAL_EVALUATE: he_canonical_resume_evaluate(frame); break;
    case HE_CANONICAL_CALLER: he_canonical_resume_caller(frame); break;
    case HE_CANONICAL_DISPATCH: he_canonical_resume_dispatch(frame); break;
    case HE_CANONICAL_ARGUMENTS: he_canonical_resume_arguments(frame); break;
    case HE_CANONICAL_TUPLE: he_canonical_resume_tuple(frame); break;
    case HE_CANONICAL_RESULT:
        if (frame->phase == 2u) {
            if (frame->index >= frame->work.len)
                he_canonical_pop();
            else {
                Outcome *seed = &frame->work.items[frame->index++];
                he_canonical_result_push(frame,
                    result_eval_type_hint(frame->type, outcome_preview_atom(seed)),
                    seed, frame->target);
            }
        } else
            he_canonical_resume_result(frame);
        break;
    case HE_CANONICAL_CONTROL: he_canonical_resume_control(frame); break;
    case HE_CANONICAL_INSTRUCTION: he_canonical_resume_instruction(frame); break;
    case HE_CANONICAL_MATCH: he_canonical_resume_match(frame); break;
    case HE_CANONICAL_SPACE: he_canonical_resume_space(frame); break;
    case HE_CANONICAL_STATE: he_canonical_resume_state(frame); break;
    case HE_CANONICAL_CAPTURE:
        if (!frame->phase) {
            CaptureClosure *closure = frame->atom->expr.elems[0]->ground.ptr;
            frame->saved_options = cetta_malloc(sizeof(*frame->saved_options));
            *frame->saved_options = *active_eval_options_const();
            *active_eval_options() = closure->options;
            frame->phase = 1u;
            he_canonical_push(HE_CANONICAL_CALLER,
                frame->space, frame->arena, expr_arg(frame->atom, 0u),
                NULL, frame->fuel, &frame->env,
                frame->preserve_bindings, &frame->child);
        } else {
            he_canonical_publish(frame, &frame->child, false);
            he_canonical_pop();
        }
        break;
    case HE_CANONICAL_CALL:
        if (frame->phase) {
            he_canonical_publish(frame, &frame->child, false);
            he_canonical_pop();
        } else {
            frame->phase = 1u;
            if (he_shared_execution_try(frame->space, frame->arena,
                    frame->atom, frame->type, frame->fuel, &frame->env,
                    frame->preserve_bindings, &frame->child))
                break;
            metta_call_impl(frame->space, frame->arena, frame->atom,
                frame->type, frame->fuel, frame->preserve_bindings,
                &frame->env,
#if CETTA_PRIME_EVAL_STACK
                -1,
#endif
                &frame->child);
        }
        break;
    }
}

static void he_canonical_run(HeCanonicalKind kind, Space *s, Arena *a,
    Atom *atom, Atom *type, int fuel, const Bindings *env,
    bool preserve, bool typed, OutcomeSet *target) {
    /* Authored calls stay on the owned stack. A synchronous external callback
     * can enter another driver while its native caller is still live; retain
     * the physical stack guard at that service boundary. */
    __attribute__((cleanup(eval_c_stack_guard_leave)))
    EvalCStackGuard boundary = {0};
    if (!eval_c_stack_guard_enter(fuel, &boundary)) {
        Bindings empty;
        bindings_init(&empty);
        outcome_set_add(target, atom_error(a, atom, atom_symbol(a, "StackOverflow")),
            env ? env : &empty);
        return;
    }
    EvalGcRegionGuard region = {0};
    HeCanonicalDriver driver = {
        .root_target = target,
        .arena = a,
        .gc_anchor = eval_gc_region_enter(&region, a),
    };
    HeCanonicalDriver *enclosing = g_he_canonical_driver;
    g_he_canonical_driver = &driver;
    HeCanonicalFrame *frame = he_canonical_push(kind, s, a, atom, type,
        fuel, env, preserve, target);
    if (frame)
        frame->typed = typed;
    while (driver.stack.top && !eval_unwinding() && !eval_cancel_check()) {
        if (g_eval_gc.region_arena == a &&
            g_eval_gc.external_owner_depth == 0u &&
            eval_gc_budget_reached_with_floor(a, driver.gc_anchor,
                driver.gc_survivor_floor_bytes) &&
            eval_gc_root_chain_is_precise(g_eval_gc.roots)) {
            uint64_t before = g_eval_gc.collections;
            eval_gc_collect(a, driver.gc_anchor, NULL, NULL, NULL,
                g_prime_need_episode_owner, &g_prime_need_active,
                &g_prime_need_branch_state_active
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
                , &g_prime_need_receipt_active
#endif
                );
            if (g_eval_gc.collections != before)
                driver.gc_survivor_floor_bytes =
                    arena_accounted_live_bytes(&g_eval_gc.survivor);
        }
        he_canonical_resume((HeCanonicalFrame *)driver.stack.top);
    }
    while (driver.stack.top)
        he_canonical_pop();
    g_he_canonical_driver = enclosing;
    eval_gc_region_leave(&region);
}

static bool he_canonical_bind_try(Space *s, Arena *a, Atom *type,
    Atom *atom, int fuel, const Bindings *env, bool typed, OutcomeSet *os) {
    if (!he_canonical_enabled())
        return false;
    he_canonical_run(HE_CANONICAL_BIND, s, a, atom, type, fuel, env,
        true, typed, os);
    return true;
}

static bool he_canonical_call_try(Space *s, Arena *a, Atom *atom, Atom *type,
    int fuel, const Bindings *env, bool preserve, OutcomeSet *os) {
    if (!he_canonical_enabled())
        return false;
    he_canonical_run(HE_CANONICAL_CALL, s, a, atom, type, fuel, env,
        preserve, false, os);
    return true;
}
