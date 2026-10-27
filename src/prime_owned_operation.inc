/* These witnesses are installed only by the explicit external top request
 * or by a registered ordinary parent frame. An arena address by itself is
 * neither a root receipt nor evidence that it is outside the child graph. */
static bool prime_native_acquired_receiver_admitted(
    const PrimeNativeAcquiredOperation *operation, const PrimeNativeControl *control) {
    if (!operation || !control || !operation->receiver ||
        !control->owned_top_episode || !control->owned_top_episode->owned_region)
        return false;
    const PrimeNativeOwnedRequest *request = operation->top_request;
    if (request)
        return !operation->parent_frame && !operation->parent_control &&
            request->acquired == operation && request->top_episode == control->owned_top_episode &&
            request->pending && request->pending->payload_owner == operation->receiver;
    const PrimeEvalStackFrame *frame = operation->parent_frame;
    PrimeNativeControl *parent = (PrimeNativeControl *)operation->parent_control;
    if (!frame || !parent || parent == control || !parent->owned_arena ||
        (parent->owned_top_episode && parent->owned_top_episode->owned_region) ||
        !prime_native_frame_index_find(parent, frame))
        return false;
    if (frame->kind != PRIME_EVAL_STACK_FRAME_CONTROL_NESTED || frame->control_retired ||
        frame->control_nested_operation != operation || frame->control_nested != control ||
        frame->arena != operation->receiver || frame->arena != parent->driver.arena ||
        !frame->target)
        return false;
    /* The index authenticates this exact embedded child set. Its owning
     * frame must share the still-held parent arena and remain unretired.
     * These reads require the caller's quiescent parent lifetime; they do
     * not acquire a concurrent use or prevent independent reclamation. */
    if (frame->target == parent->driver.root_target) {
        if (frame->target != &parent->output) return false;
    } else {
        const PrimeEvalStackFrame *target_owner =
            prime_native_control_target_owner(parent, frame->target);
        if (!target_owner || target_owner->control_retired ||
            target_owner->arena != operation->receiver)
            return false;
    }
    return !frame->target->payload_owner ||
        frame->target->payload_owner == operation->receiver;
}

static bool prime_native_acquired_prepare_row(PrimeNativeAcquiredOperation *operation) {
    if (!operation || !operation->pending_ready) return false;
    if (operation->pending.len) return operation->pending.len == 1u;
    Outcome *row = outcome_set_push_slot(&operation->pending);
    if (!row) return false;
    outcome_init(row);
    if (!bindings_capture_active_need(&row->env)) {
        outcome_free_fields(row);
        operation->pending.len--;
        return false;
    }
    return true;
}

/* Publication already precedes this callback in the ordinary controller.
 * The pure operation accepts every correctly invoked row without an
 * allocation. The exact row/value/environment and ordered cursor witness
 * actual callbacks; marked-but-unvisited rows cannot be repaired later.
 * Copying is a separate phase. False retains legacy accept-and-stop; a
 * malformed callback also leaves an explicit incomplete return. */
static bool prime_native_acquired_accept(
    Arena *arena, Atom *value, const Outcome *occurrence, void *payload) {
    const Bindings *environment = &occurrence->env;
    PrimeNativeAcquiredOperation *operation = payload;
    PrimeNativeControl *control = cetta_native_handle_use_resource(&operation->use);
    cetta_native_cost_charge(CETTA_COST_ANALYSIS_STEP, 1u);
    if (!control || control->policy.selection || !control->published ||
        control->published > control->output.len || arena != control->driver.arena) {
        operation->return_refusal = "ReturnCallbackOwner";
        return false;
    }
    size_t index = control->published - 1u;
    const Outcome *row = &control->output.items[index];
    if (index < operation->accept_cursor || index >= control->delivered_capacity ||
        !control->delivered[index] || !value || row->materialized_atom != value ||
        row != occurrence || &row->env != environment || operation->accepted_count == SIZE_MAX) {
        operation->return_refusal = "ReturnCallbackRow";
        return false;
    }
    while (operation->accept_cursor < index) {
        size_t prior = operation->accept_cursor++;
        cetta_native_cost_charge(CETTA_COST_ANALYSIS_STEP, 1u);
        if (prior < control->delivered_capacity && control->delivered[prior])
            operation->return_refusal = "ReturnMissingCallback";
    }
    operation->accept_cursor = index + 1u;
    operation->accepted_count++;
    return operation->return_refusal == NULL;
}

static bool prime_native_acquired_interval(PrimeNativeAcquiredOperation *operation,
    PrimeNativeControl *control) {
    if (operation->return_refusal || !operation->producer_complete ||
        operation->output_first > operation->value_cursor ||
        operation->value_cursor > operation->output_limit ||
        operation->output_limit != control->published ||
        operation->output_limit > control->output.len || control->policy.selection)
        return false;
    if (!operation->acceptance_checked) {
        cetta_native_cost_charge(CETTA_COST_ANALYSIS_STEP, 1u);
        if (operation->accept_cursor > operation->output_limit ||
            control->delivered_count < operation->delivered_first ||
            operation->accepted_count != control->delivered_count - operation->delivered_first) {
            operation->return_refusal = "ReturnAcceptanceCount";
            return false;
        }
        while (operation->accept_cursor < operation->output_limit) {
            size_t index = operation->accept_cursor++;
            cetta_native_cost_charge(CETTA_COST_ANALYSIS_STEP, 1u);
            if (index < control->delivered_capacity && control->delivered[index]) {
                operation->return_refusal = "ReturnMissingCallback";
                return false;
            }
        }
        operation->acceptance_checked = true;
    }
    return true;
}

static bool prime_native_acquired_values(PrimeNativeAcquiredOperation *operation,
    PrimeNativeControl *control) {
    if (operation->values) return true;
    if (!prime_native_acquired_interval(operation,control)) return false;
    PrimeNativeReturnCarrier *carrier = operation->carrier;
    if (!operation->value_copy) {
        operation->value_copy = atom_deep_copy_session_new(&carrier->value_storage);
        if (!operation->value_copy) return false;
        if (!atom_deep_copy_session_set_snapshot(operation->value_copy)) {
            operation->return_refusal = "ReturnSnapshotProfile";
            return false;
        }
    }
    while (operation->value_cursor < operation->output_limit) {
        size_t index = operation->value_cursor;
        cetta_native_cost_charge(CETTA_COST_ANALYSIS_STEP, 1u);
        /* A published cursor alone is not an acceptance receipt: a null
         * materialization or an already delivered row never enters here. */
        if (index >= control->delivered_capacity || !control->delivered[index]) {
            operation->value_cursor++;
            continue;
        }
        if (operation->value_count == operation->value_capacity) {
            size_t capacity = operation->value_capacity ? operation->value_capacity * 2u : 8u;
            if (capacity <= operation->value_capacity ||
                capacity > SIZE_MAX / sizeof(*operation->value_items)) return false;
            Atom **items = realloc(operation->value_items, capacity * sizeof(*items));
            if (!items) return false;
            operation->value_items = items;
            operation->value_capacity = capacity;
        }
        if (!operation->reified) {
            Outcome *row = &control->output.items[index];
            __attribute__((cleanup(prime_need_active_leave)))
            PrimeNeedActiveGuard selected;
            prime_need_active_enter_captured(&selected,&row->env);
            CettaEvalEpisode preparation={.completion=CETTA_EVAL_COMPLETE};
            CettaEvalEpisode *caller=g_eval_completion_tracker;
            g_eval_completion_tracker=&preparation;
            Atom *value = outcome_atom_materialize(control->driver.arena, row);
            operation->reified = value ? prime_need_reify_suspended(
                &carrier->value_storage, value, bindings_need_view(&row->env)) : NULL;
            g_eval_completion_tracker=caller;
            if (preparation.interrupted) operation->reified=NULL;
            if (!operation->reified) return false;
        }
        /* One actual map covers the complete batch and variable name keys.
         * It refuses raw Need/native/context/resource leaves. It never
         * falls back to the unreified source after a failed conversion. */
        Atom *copy = atom_deep_copy_session_copy(operation->value_copy, operation->reified);
        if (!copy) {
            if (atom_deep_copy_session_snapshot_unsupported(operation->value_copy))
                operation->return_refusal = "UnsupportedReturnGraph";
            else if (atom_deep_copy_session_failed(operation->value_copy))
                operation->return_refusal = "ReturnCopyRefused";
            return false;
        }
        operation->value_items[operation->value_count++] = copy;
        operation->reified = NULL;
        operation->value_cursor++;
    }
    operation->values = atom_expr(&carrier->value_storage, operation->value_items,
        (CettaExprLen)operation->value_count);
    if (!operation->values) return false;
    /* Memo keys were backed by the held controller or this same retained
     * arena. Retire the correspondence before releasing EXCLUSIVE. */
    atom_deep_copy_session_free(operation->value_copy);
    operation->value_copy = NULL;
    free(operation->value_items);
    operation->value_items = NULL;
    operation->value_capacity = 0u;
    return true;
}

static bool prime_native_acquired_materialize(PrimeNativeAcquiredOperation *operation,
    const CettaSearchPolicySpec *policy) {
    if (!operation || !operation->producer_complete ||
        operation->pending.len != 1u) return false;
    if (operation->materialized) return true;
    PrimeNativeControl *control = cetta_native_handle_use_resource(&operation->use);
    if (!prime_native_acquired_receiver_admitted(operation, control)) return false;
    PrimeNativeRegionScope scope = {0};
    if (!prime_native_region_scope_enter(control->owned_top_episode, &scope)) return false;
    Atom *holder = prime_native_acquired_values(operation, control)
        ? prime_native_acquired_return_holder(operation) : NULL;
    Atom *record = holder ? prime_native_control_observation_with_residual(
        control, operation->receiver, operation->values, policy, holder) : NULL;
    if (record) {
        operation->pending.items[0].atom = record;
        operation->materialized = true;
    }
    prime_native_region_scope_leave(&scope);
    return record != NULL;
}

static CettaContinuationStatus prime_native_acquired_observe(
    PrimeNativeOwnedRequest *request, uint64_t allowance) {
    PrimeNativeAcquiredOperation *operation = request ? request->acquired : NULL;
    PrimeNativeControl *control = operation
        ? cetta_native_handle_use_resource(&operation->use) : NULL;
    if (!control || !prime_native_acquired_receiver_admitted(operation, control))
        return CETTA_CONTINUATION_INVALIDATED;
    if (request->pending->len) return CETTA_CONTINUATION_DEFERRED;
    if (!prime_native_acquired_prepare_row(operation)) return CETTA_CONTINUATION_CAPACITY;
    if (!operation->producer_complete) {
        uint64_t granted = allowance < operation->remaining ? allowance : operation->remaining;
        uint64_t before = control->steps;
        control->leased = true;
        operation->owns_lease = true;
        prime_native_control_run(control, granted, prime_native_acquired_accept, operation);
        uint64_t spent = control->steps - before;
        operation->remaining = spent < operation->remaining ? operation->remaining - spent : 0u;
        operation->output_limit = control->published;
        operation->producer_complete = true;
    }
    if (!prime_native_acquired_materialize(operation, &control->policy))
        return CETTA_CONTINUATION_CAPACITY;
    return prime_native_control_deliver_owned(operation, &operation->pending, request->pending)
        == OUTCOME_APPEND_COMMITTED ? CETTA_CONTINUATION_READY : CETTA_CONTINUATION_CAPACITY;
}

/* A materialized row is the same obligation on every retry. No child poll,
 * observation read, residual constructor, or allowance grant occurs here. */
static uint64_t prime_native_acquired_produce(PrimeNativeAcquiredOperation *operation,
    PrimeNativeControl *control, uint64_t granted) {
    uint64_t before = control->steps;
    prime_native_control_run(control, granted, prime_native_acquired_accept, operation);
    uint64_t spent = control->steps - before;
    operation->remaining = spent < operation->remaining ? operation->remaining - spent : 0u;
    operation->output_limit = control->published;
    operation->producer_complete = prime_native_control_closed(control) ||
        cetta_control_plan_observation_satisfied(&control->hub.plan,control->delivered_count) ||
        prime_native_control_waiting_verdict(control) || control->failed ||
        control->invalidated || control->registry_read.incomplete ||
        control->driver.capacity_blocked || !operation->remaining;
    return spent;
}

/* The policy owns the source endpoint; a caller grant only authorizes a
 * slice. Once that endpoint is reached the exact accepted interval stays
 * immutable until every closed Outcome, or one open Observation, commits. */
static CettaContinuationStatus prime_native_acquired_weighted_return(
    PrimeNativeOwnedRequest *request, uint64_t allowance) {
    PrimeNativeAcquiredOperation *operation=request ? request->acquired : NULL;
    PrimeNativeControl *control=operation
        ? cetta_native_handle_use_resource(&operation->use) : NULL;
    if (!control || !prime_native_acquired_receiver_admitted(operation,control))
        return CETTA_CONTINUATION_INVALIDATED;
    if (request->pending->len) return CETTA_CONTINUATION_DEFERRED;
    if (!operation->producer_complete) {
        uint64_t grant=allowance < operation->remaining ? allowance : operation->remaining;
        control->leased=operation->owns_lease=true;
        (void)prime_native_acquired_produce(operation,control,grant);
        if (!operation->producer_complete) return CETTA_CONTINUATION_DEFERRED;
        operation->closed_return=prime_native_control_closed(control);
    }
    if (!operation->closed_return) {
        if (!prime_native_acquired_prepare_row(operation) ||
            !prime_native_acquired_materialize(operation,&control->policy))
            return CETTA_CONTINUATION_CAPACITY;
        return prime_native_control_deliver_owned(operation,&operation->pending,request->pending)
            == OUTCOME_APPEND_COMMITTED ? CETTA_CONTINUATION_READY : CETTA_CONTINUATION_CAPACITY;
    }
    if (!prime_native_acquired_interval(operation,control)) return CETTA_CONTINUATION_CAPACITY;
    while (operation->value_cursor < operation->output_limit &&
        (operation->value_cursor >= control->delivered_capacity ||
         !control->delivered[operation->value_cursor])) operation->value_cursor++;
    if (operation->value_cursor == operation->output_limit) {
        operation->published=true;
        return CETTA_CONTINUATION_READY;
    }
    if (!operation->pending.len) {
        const Outcome *source=&control->output.items[operation->value_cursor];
        __attribute__((cleanup(prime_need_active_leave))) PrimeNeedActiveGuard selected;
        prime_need_active_enter_captured(&selected,&source->env);
        CettaEvalEpisode preparation={.completion=CETTA_EVAL_COMPLETE};
        CettaEvalEpisode *caller=g_eval_completion_tracker;
        g_eval_completion_tracker=&preparation;
        bool ready=outcome_set_add_promoted_existing(operation->receiver,
            &operation->pending,source,true) &&
            bindings_promote_atoms_to_arena(&operation->pending.items[0].env,operation->receiver);
        g_eval_completion_tracker=caller;
        if (!ready || preparation.interrupted) {
            if (operation->pending.len) {
                outcome_free_fields(&operation->pending.items[0]);
                operation->pending.len=0u;
            }
            return CETTA_CONTINUATION_CAPACITY;
        }
        operation->materialized=true;
        operation->value_count++;
    }
    __attribute__((cleanup(prime_need_active_leave))) PrimeNeedActiveGuard selected;
    prime_need_active_enter_captured(&selected,&operation->pending.items[0].env);
    if (prime_native_control_deliver_owned(operation,&operation->pending,request->pending)
            != OUTCOME_APPEND_COMMITTED)
        return CETTA_CONTINUATION_CAPACITY;
    operation->value_cursor++;
    while (operation->value_cursor < operation->output_limit &&
        (operation->value_cursor >= control->delivered_capacity ||
         !control->delivered[operation->value_cursor])) operation->value_cursor++;
    operation->published=operation->value_cursor==operation->output_limit;
    return CETTA_CONTINUATION_READY;
}

static bool prime_native_acquired_resume_nested(PrimeEvalStackFrame *frame) {
    PrimeNativeAcquiredOperation *operation = frame->control_nested_operation;
    if (!operation) return false;
    PrimeNativeControl *control = cetta_native_handle_use_resource(&operation->use);
    PrimeNativeControl *parent = g_prime_eval_stack_driver
        ? g_prime_eval_stack_driver->control : NULL;
    if (parent != operation->parent_control || frame != operation->parent_frame ||
        control != frame->control_nested ||
        !prime_native_acquired_receiver_admitted(operation, control)) {
        if (parent) parent->invalidated = true;
        return true;
    }
    bool paid = prime_native_execution_consume(operation, parent, frame);
    if (!prime_native_acquired_prepare_row(operation)) {
        parent->active_spent = paid ? 0u : 1u;
        parent->driver.capacity_blocked = true;
        return true;
    }
    if (paid) {
        if (!operation->producer_complete) return true;
    } else if (!operation->producer_complete) {
        uint64_t granted = operation->remaining ? 1u : 0u;
        if (granted > parent->active_allowance) granted = parent->active_allowance;
        uint64_t spent = prime_native_acquired_produce(operation, control, granted);
        parent->active_spent = spent ? spent : 1u;
        frame->control_nested_budget = operation->remaining;
        if (!operation->producer_complete) return true;
    } else parent->active_spent = 1u;
    if (!prime_native_acquired_materialize(operation, &frame->control_nested_policy) ||
        prime_native_control_deliver_owned(operation, &operation->pending, frame->target)
            != OUTCOME_APPEND_COMMITTED) {
        /* Retain frame, child, carrier, exact pending row and remaining
         * allowance. The same frame is serviced after capacity recovery. */
        parent->driver.capacity_blocked = true;
        return true;
    }
    operation->published = true;
    prime_eval_stack_stream_note(frame->target);
    prime_eval_stack_pop();
    return true;
}
