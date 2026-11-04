/* The ordinary HE driver, retained between host slices. Each activation
 * installs stable execution-owned roots and restores the enclosing runtime. */
typedef struct {
    HeCanonicalDriver driver;
    Arena heap;
    EvalGc collector;
    HeValueIndex values;
    EvalGcRegionGuard region;
    OutcomeSet output;
    CettaCount consumed;
    Space *space;
    Atom *source;
    int fuel;
    uint32_t depth;
    bool demand_present;
    CettaObservationDemand demand;
    CettaEvaluatorOptions captured_options;
    bool options_suspended;
    bool started;
} HeRetainedExecution;

typedef struct {
    EvalGc *collector;
    HeValueIndex *values;
    HeCanonicalDriver *driver;
    uint32_t depth;
    bool demand_present;
    CettaObservationDemand demand;
} HeRetainedScope;

static CettaEvaluatorOptions *he_retained_outer_options(HeRetainedExecution *work) {
    CettaEvaluatorOptions *outer = NULL;
    for (CettaOwnedFrameLink *link = work->driver.stack.top; link; link = link->next) {
        HeCanonicalFrame *frame = (HeCanonicalFrame *)link;
        if (frame->saved_options) outer = frame->saved_options;
        if (frame->saved_context) outer = &frame->saved_context->options;
    }
    return outer;
}

static void he_retained_enter(HeRetainedExecution *work, HeRetainedScope *scope) {
    *scope = (HeRetainedScope){.collector = g_eval_gc_current,
        .values = g_he_value_index_current, .driver = g_he_canonical_driver,
        .depth = g_metta_eval_depth, .demand_present = g_eval_observation_demand_present,
        .demand = g_eval_observation_demand};
    g_eval_gc_current = &work->collector;
    g_he_value_index_current = &work->values;
    g_he_canonical_driver = &work->driver;
    g_metta_eval_depth = work->depth;
    g_eval_observation_demand_present = work->demand_present;
    g_eval_observation_demand = work->demand;
    if (work->options_suspended) {
        CettaEvaluatorOptions *outer = he_retained_outer_options(work);
        assert(outer);
        /* Re-enter the captured dynamic extent. Its final restoration
         * resumes the current outside state, including intervening changes. */
        *outer = *active_eval_options_const();
        *active_eval_options() = work->captured_options;
        work->options_suspended = false;
    }
}

static void he_retained_leave(HeRetainedExecution *work, HeRetainedScope *scope) {
    CettaEvaluatorOptions *outer = he_retained_outer_options(work);
    if (outer) {
        work->captured_options = *active_eval_options_const();
        *active_eval_options() = *outer;
        work->options_suspended = true;
    }
    work->depth = g_metta_eval_depth;
    work->demand_present = g_eval_observation_demand_present;
    work->demand = g_eval_observation_demand;
    g_eval_gc_current = scope->collector;
    g_he_value_index_current = scope->values;
    g_he_canonical_driver = scope->driver;
    g_metta_eval_depth = scope->depth;
    g_eval_observation_demand_present = scope->demand_present;
    g_eval_observation_demand = scope->demand;
}

static HeRetainedExecution *he_retained_new(Space *space, Atom *source, int fuel) {
    HeRetainedExecution *work = calloc(1u, sizeof(*work));
    if (!work) return NULL;
    arena_init_detached(&work->heap);
    outcome_set_init_with_owner(&work->output, &work->heap);
    work->driver = (HeCanonicalDriver){.root_target = &work->output,
        .arena = &work->heap, .retained = true};
    work->space = space;
    work->source = source;
    work->fuel = fuel;
    return work;
}

static CettaOwnedExecutionStep he_retained_next(HeRetainedExecution *work,
    Arena *arena, uint64_t allowance, EvalOutcome *outcome,
    CettaEvalQueryAnswer *packet, uint64_t *transitions) {
    __attribute__((cleanup(eval_c_stack_guard_leave)))
    EvalCStackGuard boundary = {0};
    if (!eval_c_stack_guard_enter(work->fuel, &boundary)) {
        eval_mark_interrupted(CETTA_EVAL_INCOMPLETE_STACK);
        return CETTA_OWNED_EXECUTION_INTERRUPTED;
    }
    HeRetainedScope scope;
    he_retained_enter(work, &scope);
    uint64_t count = 0u;
    if (!work->started && allowance) {
        work->driver.gc_anchor = eval_gc_region_enter(&work->region, &work->heap);
        he_value_index_open(&work->heap);
        he_canonical_push(HE_CANONICAL_EVALUATE, work->space, &work->heap,
            work->source, NULL, work->fuel, NULL, true, &work->output);
        work->started = true;
    }
    while (work->driver.stack.top && count < allowance &&
        !eval_unwinding() && !eval_cancel_check() && work->consumed == work->output.len) {
        he_canonical_advance(&work->driver);
        count++;
    }
    CettaOwnedExecutionStep step;
    Atom *raised = petta_eval_take_raise();
    if (raised) {
        Atom *copy = atom_deep_copy(arena, raised);
        if (copy) {
            result_set_add(&outcome->results, copy);
            outcome->petta_raised_error = true;
            outcome->petta_raise_known = true;
        } else eval_mark_interrupted(CETTA_EVAL_INCOMPLETE_CAPACITY);
        step = CETTA_OWNED_EXECUTION_INTERRUPTED;
    } else if (eval_unwinding() || eval_cancel_check() ||
        (g_eval_completion_tracker && g_eval_completion_tracker->completion != CETTA_EVAL_COMPLETE)) {
        step = CETTA_OWNED_EXECUTION_INTERRUPTED;
    } else if (work->consumed < work->output.len) {
        Outcome *item = &work->output.items[work->consumed++];
        Atom *value = outcome_atom_materialize(&work->heap, item);
        value = value ? atom_deep_copy(arena, value) : NULL;
        if (!value) {
            eval_mark_interrupted(CETTA_EVAL_INCOMPLETE_CAPACITY);
            step = CETTA_OWNED_EXECUTION_INTERRUPTED;
        } else {
            result_set_add(&outcome->results, value);
            if (packet) {
                packet->bindings = bindings_capture_value(arena, &item->env);
                packet->delayed = item->delayed ? atom_deep_copy(arena, item->delayed) : NULL;
                packet->kind = item->delayed ? CETTA_CALL_SUSPENDED : CETTA_CALL_VALUE;
                if (!packet->bindings || (item->delayed && !packet->delayed))
                    eval_mark_interrupted(CETTA_EVAL_INCOMPLETE_CAPACITY);
            }
            step = CETTA_OWNED_EXECUTION_RETURN;
        }
        outcome_free_fields(item);
        if (work->consumed == work->output.len) {
            outcome_set_clear(&work->output);
            work->consumed = 0u;
        }
    } else step = work->driver.stack.top ? CETTA_OWNED_EXECUTION_PAUSED
                                         : CETTA_OWNED_EXECUTION_COMPLETE;
    if (transitions) *transitions = count;
    he_retained_leave(work, &scope);
    return step;
}

static void he_retained_free(HeRetainedExecution *work) {
    if (!work) return;
    HeRetainedScope scope;
    he_retained_enter(work, &scope);
    he_canonical_dispose(&work->driver);
    if (work->started) {
        he_value_index_close();
        eval_gc_region_leave(&work->region);
    }
    outcome_set_free(&work->output);
    assert(!work->collector.roots && !work->collector.region_depth && !g_metta_eval_depth);
    if (work->collector.ready) arena_free(&work->collector.survivor);
    he_retained_leave(work, &scope);
    arena_free(&work->heap);
    free(work);
}
