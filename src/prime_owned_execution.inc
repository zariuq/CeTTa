static _Thread_local PrimeNativeExecutionSlice *g_prime_execution_apply;

/* The parent, sender, target-owning frame and receiving arena are held by
 * the synchronous caller. This authenticates those live registrations; it
 * does not acquire a reclamation pin or permit concurrent parent mutation. */
static bool prime_native_execution_target(const PrimeNativeExecutionSlice *slice,
    PrimeNativeControl *parent, PrimeNativeWork *work) {
    if (!slice || slice->self != slice ||
        slice->profile != PRIME_NATIVE_EXECUTION_OWNED_NESTED ||
        slice->parent != parent || slice->work != work || !parent || !work ||
        work->owner != parent || work->task_initialized || work->frame != slice->sender ||
        !prime_native_frame_index_find(parent, slice->sender)) return false;
    PrimeEvalStackFrame *sender = slice->sender;
    if (sender->control_retired || sender->control_closed ||
        sender->control_sequence != slice->sender_sequence ||
        sender->control_nested_operation != slice->operation ||
        sender->control_nested != slice->child || sender->target != slice->target ||
        sender->arena != slice->receiver || parent->driver.arena != slice->receiver)
        return false;
    if (slice->target_owner) {
        if (!prime_native_frame_index_find(parent, slice->target_owner)) return false;
        PrimeEvalStackFrame *target = slice->target_owner;
        if (target->control_retired || target->control_closed || !target->child_initialized ||
            target->control_sequence != slice->target_sequence ||
            target->arena != slice->receiver || &target->child != slice->target)
            return false;
    } else if (slice->target != &parent->output) return false;
    PrimeNativeAcquiredOperation *operation = slice->operation;
    if (!operation || slice->authority != &operation->use ||
        operation->use.mode != CETTA_NATIVE_HANDLE_USE_EXCLUSIVE ||
        cetta_native_handle_use_resource(&operation->use) != slice->child ||
        !prime_native_acquired_receiver_admitted(operation, slice->child)) return false;
    return true;
}

static void prime_native_execution_detach(PrimeNativeParallel *parallel) {
    PrimeNativeExecutionSlice *slice = &parallel->execution;
    if (slice->profile == PRIME_NATIVE_EXECUTION_NONE) return;
    assert(slice->self == slice && (!parallel->attempted || slice->joined));
    if (slice->reservation.self) {
        bool released = prime_native_service_unreserve(&slice->reservation);
        assert(released); (void)released;
    }
    /* The registered frame owns the actual Use and child. Retirement of
     * this physical capsule never destroys or reissues that operation. */
    parallel->child = NULL;
    slice->operation = NULL;
    slice->authority = NULL;
    slice->child = NULL;
    slice->episode = NULL;
    slice->region = NULL;
    slice->services = NULL;
}

static bool prime_native_execution_prepare(PrimeNativeControl *parent,
    PrimeNativeWork *work) {
    if (!parent || !work || work->owner != parent || work->task_initialized ||
        !work->frame || !prime_native_frame_index_find(parent, work->frame)) return false;
    PrimeEvalStackFrame *frame = work->frame;
    if (frame->kind != PRIME_EVAL_STACK_FRAME_CONTROL_NESTED ||
        !prime_native_control_frame_ready(frame) || frame->control_pending ||
        parent->worker_execution || parent->policy.selection || parent->policy.by_weight ||
        !parent->owned_arena || parent->active_scoring ||
        !prime_native_owned_cost_policy(&frame->control_nested_policy) ||
        g_context_home_depth || g_context_here_depth || g_owned_outcome_variant_bank ||
        g_prime_need_answer_observer || g_prime_need_answer_observer_context ||
        cetta_native_cost_observer_active() ||
        (parent->owned_top_episode && parent->owned_top_episode->owned_region)) return false;
    PrimeNativeAcquiredOperation *operation = frame->control_nested_operation;
    if (!operation || operation->use.mode != CETTA_NATIVE_HANDLE_USE_EXCLUSIVE ||
        operation->producer_complete || operation->materialized || operation->published ||
        operation->return_refusal || !operation->remaining || !operation->owns_lease) return false;
    PrimeNativeControl *child = cetta_native_handle_use_resource(&operation->use);
    if (!child || child != frame->control_nested || !child->leased ||
        !child->owned_top_episode || !child->owned_top_episode->owned_region ||
        !prime_native_acquired_receiver_admitted(operation, child)) return false;
    PrimeNativeOwnedRegion *region = child->owned_top_episode->owned_region;
    if (!region->ready || !region->services || !region->library_ready ||
        child->space != &region->world || child->source_services ||
        child->failed || child->invalidated || child->effect_pending || child->need_pending ||
        child->registry_read.incomplete || child->steps == UINT64_MAX ||
        prime_native_control_closed(child) || prime_native_control_waiting_verdict(child)) return false;
    PrimeEvalStackFrame *target_owner = frame->target == &parent->output ? NULL
        : prime_native_control_target_owner(parent, frame->target);
    if (frame->target != &parent->output && !target_owner) return false;
    uint64_t occurrence = 0u;
    for (size_t i = 0u; i < cetta_continuation_hub_length(&parent->hub); i++) {
        const CettaOwnedContinuation *owned = cetta_continuation_hub_at(&parent->hub, i);
        cetta_native_cost_charge(CETTA_COST_QUEUE_VISIT, 1u);
        if (owned->payload == work) { occurrence = owned->occurrence_id; break; }
    }
    if (!occurrence) return false;
    if (work->parallel) {
        PrimeNativeExecutionSlice *slice = &work->parallel->execution;
        return !work->parallel->attempted && slice->phase == PRIME_NATIVE_EXECUTION_RESERVED &&
            slice->occurrence == occurrence && prime_native_execution_target(slice, parent, work);
    }
    /* Preparation, reservation and scope selection are separate named
     * analysis operations. Instruction counts are not native-events-v1. */
    cetta_native_cost_charge(CETTA_COST_ANALYSIS_STEP, 1u);
    PrimeNativeParallel *parallel = calloc(1u, sizeof(*parallel));
    if (!parallel) return false;
    atomic_init(&parallel->references, 1u);
    PrimeNativeExecutionSlice *slice = &parallel->execution;
    *slice = (PrimeNativeExecutionSlice){.self=slice,
        .profile=PRIME_NATIVE_EXECUTION_OWNED_NESTED,
        .phase=PRIME_NATIVE_EXECUTION_RESERVED,
        .parent=parent,.work=work,.sender=frame,.target_owner=target_owner,
        .target=frame->target,.receiver=operation->receiver,
        .sender_sequence=frame->control_sequence,
        .target_sequence=target_owner ? target_owner->control_sequence : 0u,
        .occurrence=occurrence,.operation=operation,.authority=&operation->use,
        .child=child,.episode=child->owned_top_episode,.region=region,.services=region->services,
        .source_read=child->source_read,.environment=&child->base_environment};
    parallel->child = child;
    parallel->context = eval_context_capture();
    parallel->attachment = PRIME_NATIVE_ATTACHMENT_RETURN;
    parallel->activation_occurrence = occurrence;
    parallel->captured_need = *bindings_need_view(&child->base_environment);
    cetta_native_cost_charge(CETTA_COST_ANALYSIS_STEP, 1u);
    bool reserved = prime_native_execution_target(slice, parent, work) &&
        prime_native_service_reserve(region->services, &slice->reservation);
    if (!reserved) { prime_native_parallel_destroy(parallel); return false; }
    PrimeNativeServiceReservation *previous = g_prime_service_reservation;
    g_prime_service_reservation = &slice->reservation;
    PrimeNativeRegionScope scope = {0};
    cetta_native_cost_charge(CETTA_COST_ANALYSIS_STEP, 1u);
    bool selected = prime_native_region_scope_enter(slice->episode, &scope);
    bool profile = selected && active_search_table_mode() == CETTA_TABLE_MODE_NONE &&
        !outcome_variant_sharing_enabled() &&
        space_read_token_is_current(child->source_read) &&
        prime_native_control_query_reads_current(child) &&
        registry_read_view_validate(&child->registry_read, &region->registry);
    if (profile) parallel->context = eval_context_capture();
    prime_native_region_scope_leave(&scope);
    g_prime_service_reservation = previous;
    if (!profile) { prime_native_parallel_destroy(parallel); return false; }
    /* Allocate the actual external pending row before lending execution.
     * No worker is allowed to mutate the receiving arena or target set. */
    if (!prime_native_acquired_prepare_row(operation)) {
        prime_native_parallel_destroy(parallel);
        return false;
    }
    work->parallel = parallel;
    return true;
}

static bool prime_native_execution_task(CettaParallelWorker *worker,
    PrimeNativeParallel *parallel, PrimeNativeParallelRun *run) {
    PrimeNativeExecutionSlice *slice = &parallel->execution;
    assert(slice->self == slice && slice->phase == PRIME_NATIVE_EXECUTION_RESERVED &&
        slice->profile == PRIME_NATIVE_EXECUTION_OWNED_NESTED &&
        slice->reservation.self == &slice->reservation && !slice->reservation.entered &&
        slice->reservation.services == slice->services);
    PrimeNativeControl *child = slice->child;
    PrimeNativeAcquiredOperation *operation = slice->operation;
    /* The worker borrows selection only from this exact address-stable
     * capsule and acquired receipt. It never resolves a printed identifier
     * or reads the parent's mutable frontier while executing the child. */
    assert(child && operation && slice->authority == &operation->use &&
        operation->use.mode == CETTA_NATIVE_HANDLE_USE_EXCLUSIVE &&
        cetta_native_handle_use_resource(&operation->use) == child &&
        child == parallel->child && child->owned_top_episode == slice->episode &&
        slice->episode->owned_region == slice->region &&
        slice->region->services == slice->services &&
        slice->environment == &child->base_environment);
    parallel->worker = cetta_parallel_worker_index(worker);
    parallel->attempted = true;
    slice->grant = parallel->grant < operation->remaining ? parallel->grant : operation->remaining;
    if (slice->grant > UINT64_MAX - child->steps) slice->grant = UINT64_MAX - child->steps;
    slice->before = child->steps;
    slice->remaining_before = operation->remaining;
    slice->accepted_before = operation->accepted_count;
    slice->output_before = child->published;
    slice->ledger_identity = child->event_cost.identity;
    slice->cost_before = child->event_cost.total.work;
    if (run->receipts) {
        parallel->started_ns = prime_native_monotonic_ns();
        uint32_t active = atomic_fetch_add_explicit(&run->active, 1u, memory_order_acq_rel) + 1u;
        uint32_t previous = atomic_load_explicit(&run->overlap, memory_order_relaxed);
        while (active > previous && !atomic_compare_exchange_weak_explicit(
            &run->overlap, &previous, active, memory_order_relaxed, memory_order_relaxed)) {}
    }
    PrimeNativeParallel *previous_parallel = g_prime_native_parallel_current;
    PrimeNativeServiceReservation *previous_reservation = g_prime_service_reservation;
    EvalContextState previous_context = eval_context_capture();
    VariantBank *previous_variants = g_owned_outcome_variant_bank;
    const PettaProgramRevisionProjection *previous_projection = g_petta_program_revision_projection;
    EvalCompletionTracker *previous_completion = g_eval_completion_tracker;
    EvalCompletionTracker completion = {.completion=CETTA_EVAL_COMPLETE};
    _Atomic bool *previous_cancel = eval_set_cancel_token(&run->cancelled);
    g_prime_native_parallel_current = parallel;
    g_prime_service_reservation = &slice->reservation;
    g_owned_outcome_variant_bank = NULL;
    g_petta_program_revision_projection = NULL;
    g_eval_completion_tracker = &completion;
    CettaNativeCostScope *containing = cetta_native_cost_detach_begin();
    CettaNativeCostScope cost;
    if (parallel->metered) cetta_native_cost_scope_enter(&cost, &parallel->slice_cost, parallel->occurrence);
    cetta_native_cost_charge(CETTA_COST_ANALYSIS_STEP, 1u);
    PrimeNativeRegionScope scope = {0};
    slice->scope_entered = prime_native_region_scope_enter(slice->episode, &scope);
    if (slice->scope_entered) {
        eval_context_restore(&parallel->context);
        child->worker_execution = !run->coordinator;
        slice->producer_entered = true;
        parallel->spent = prime_native_acquired_produce(operation, child, slice->grant);
        child->worker_execution = false;
        prime_native_region_scope_leave(&scope);
    } else child->completion = CETTA_EVAL_INCOMPLETE_HOST_FAILURE;
    slice->scope_left = !slice->reservation.entered;
    assert(slice->scope_left);
    slice->after = child->steps;
    slice->spent = parallel->spent;
    slice->remaining_after = operation->remaining;
    slice->accepted_after = operation->accepted_count;
    slice->output_after = child->published;
    slice->cost_after = child->event_cost.total.work;
    assert(slice->after >= slice->before && slice->after - slice->before == slice->spent &&
        slice->spent <= slice->grant);
    slice->phase = PRIME_NATIVE_EXECUTION_PAID;
    /* One actual invocation completes this physical slice. An unfinished
     * producer returns to the original frame before receiving another grant. */
    parallel->completed = true;
    if (parallel->metered) cetta_native_cost_scope_leave(&cost);
    cetta_native_cost_detach_end(containing);
    g_eval_completion_tracker = previous_completion;
    g_owned_outcome_variant_bank = previous_variants;
    g_petta_program_revision_projection = previous_projection;
    eval_context_restore(&previous_context);
    g_prime_service_reservation = previous_reservation;
    g_prime_native_parallel_current = previous_parallel;
    eval_set_cancel_token(previous_cancel);
    if (run->receipts) {
        parallel->finished_ns = prime_native_monotonic_ns();
        atomic_fetch_sub_explicit(&run->active, 1u, memory_order_acq_rel);
    }
    return true;
}

/* Called only after the existing executor's synchronous join, including
 * completion of its unattempted fallback tasks. Scope exit happens-before
 * join; join happens-before releasing this exact service reservation. */
static void prime_native_execution_joined(PrimeNativeParallel *parallel) {
    PrimeNativeExecutionSlice *slice = &parallel->execution;
    if (slice->profile == PRIME_NATIVE_EXECUTION_NONE) return;
    assert(slice->self == slice && parallel->attempted && slice->scope_left &&
        slice->phase == PRIME_NATIVE_EXECUTION_PAID && !slice->joined);
    slice->joined = true;
    bool released = prime_native_service_unreserve(&slice->reservation);
    assert(released); (void)released;
}

/* A paid interval is consumed by its exact original coordinator frame.
 * Retrying an already completed return uses this same marker; it cannot
 * reenter the producer or issue a replacement residual. */
static bool prime_native_execution_consume(PrimeNativeAcquiredOperation *operation,
    PrimeNativeControl *parent, PrimeEvalStackFrame *frame) {
    PrimeNativeExecutionSlice *slice = g_prime_execution_apply;
    if (!slice) return false;
    assert(slice->self == slice && slice->joined && slice->operation == operation &&
        slice->parent == parent && slice->sender == frame &&
        (slice->phase == PRIME_NATIVE_EXECUTION_PAID ||
         slice->phase == PRIME_NATIVE_EXECUTION_RETURNING));
    slice->phase = PRIME_NATIVE_EXECUTION_RETURNING;
    frame->control_nested_budget = operation->remaining;
    parent->active_spent = 0u;
    return true;
}

static void prime_native_execution_requeue(PrimeNativeControl *parent,
    CettaOwnedContinuation *owned) {
    if (cetta_continuation_hub_requeue(&parent->hub, owned)) return;
    size_t count = parent->parallel_inflight_count;
    parent->parallel_inflight = cetta_realloc(parent->parallel_inflight,
        (count + 1u) * sizeof(*parent->parallel_inflight));
    parent->parallel_inflight[count] = *owned;
    parent->parallel_inflight_count = count + 1u;
    *owned = (CettaOwnedContinuation){0};
    parent->driver.capacity_blocked = true;
}

static void prime_native_execution_single(PrimeNativeControl *parent,
    CettaOwnedContinuation *owned) {
    PrimeNativeWork *work = owned->payload;
    PrimeNativeParallel *parallel = work->parallel;
    PrimeNativeExecutionSlice *slice = &parallel->execution;
    if (!prime_native_execution_target(slice, parent, work) ||
        slice->occurrence != owned->occurrence_id) {
        parent->active_spent = 0u;
        parent->invalidated = true;
        prime_native_execution_requeue(parent, owned);
        return;
    }
    if (!parallel->attempted) {
        /* Fewer than two admitted owners, exhausted wave credits or a
         * withdrawn wave keep the unchanged serial frame operation. */
        work->parallel = NULL;
        prime_native_parallel_retire(parallel);
        bool released = cetta_continuation_hub_release_lease(&parent->hub, owned);
        assert(released); (void)released;
        prime_native_control_frame_step(parent, work);
        return;
    }
    assert(slice->joined && !slice->reservation.self && work->prepared_publication);
    PrimeNativeAcquiredOperation *operation = slice->operation;
    PrimeEvalStackFrame *frame = slice->sender;
    parent->active_spent = 0u;
    if (!slice->scope_entered || !slice->producer_entered) {
        parent->completion = CETTA_EVAL_INCOMPLETE_HOST_FAILURE;
        parent->invalidated = true;
        prime_native_execution_requeue(parent, owned);
        return;
    }
    /* These are captured private dependencies. Original-slot currency is
     * deliberately not rechecked: the issued EXCLUSIVE Use survives close. */
    PrimeNativeRegionScope scope = {0};
    cetta_native_cost_charge(CETTA_COST_ANALYSIS_STEP, 1u);
    bool selected = prime_native_region_scope_enter(slice->episode, &scope);
    bool current = selected &&
        space_read_token_is_current(slice->source_read) &&
        space_read_token_is_current(slice->child->source_read) &&
        prime_native_control_query_reads_current(slice->child) &&
        registry_read_view_validate(&slice->child->registry_read, &slice->region->registry);
    prime_native_region_scope_leave(&scope);
    if (!current) {
        parent->invalidated = true;
        prime_native_execution_requeue(parent, owned);
        return;
    }
    PrimeNativeExecutionSlice *previous = g_prime_execution_apply;
    g_prime_execution_apply = slice;
    prime_native_control_frame_step(parent, work);
    g_prime_execution_apply = previous;
    /* The frame remains allocated until the following ordinary scan. An
     * incomplete append retains its original queued occurrence and capsule. */
    if (prime_native_frame_index_find(parent, frame) &&
        !frame->control_closed && frame->control_nested_operation == operation &&
        operation->producer_complete && !operation->published) {
        frame->control_queued = true;
        work->prepared_publication = true;
        prime_native_execution_requeue(parent, owned);
        return;
    }
    work->parallel = NULL;
    work->prepared_publication = false;
    prime_native_parallel_retire(parallel);
    bool released = cetta_continuation_hub_release_lease(&parent->hub, owned);
    assert(released); (void)released;
}
