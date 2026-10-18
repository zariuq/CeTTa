static bool prime_native_owned_region_receiver(PrimeNativeControl *control,
    Arena *arena, OutcomeSet *outcomes) {
    PrimeEvalStackDriver *driver=g_prime_eval_stack_driver;
    PrimeNativeControl *parent=driver ? driver->control : NULL;
    return parent && parent!=control && parent->owned_arena && driver->running_task &&
        !parent->worker_execution && !parent->active_scoring &&
        !(parent->owned_top_episode && parent->owned_top_episode->owned_region) &&
        arena==parent->driver.arena && prime_eval_stack_target_is_owned(outcomes);
}

/* The original selector supplies a newly acquired operation. Resume can
 * instead transfer its existing issued use and lease guard directly. The
 * external ordinary parent owns the suspended frame and receiver arenas. */
/* Failure leaves the exact acquired operation with its caller. Successful
 * frame enrollment clears it; only that registered frame then owns the use. */
static bool prime_native_owned_region_select(PrimeNativeAcquiredOperation **unpublished,
    Space *space, Arena *arena, uint64_t limit,
    const CettaSearchPolicySpec *policy, OutcomeSet *outcomes) {
    PrimeNativeAcquiredOperation *operation=unpublished ? *unpublished : NULL;
    if (!operation) return false;
    PrimeNativeControl *control=cetta_native_handle_use_resource(&operation->use);
    PrimeEvalStackDriver *driver=g_prime_eval_stack_driver;
    PrimeNativeControl *parent=driver ? driver->control : NULL;
    if (!control || !control->owned_top_episode || !control->owned_top_episode->owned_region ||
        control->leased!=operation->owns_lease || !prime_native_owned_observation_policy(policy) ||
        !prime_native_cost_policy_accepts(control, policy) ||
        !prime_native_owned_region_receiver(control,arena,outcomes) ||
        !prime_native_acquired_prepare_row(operation)) return false;
    PrimeNativeRegionScope scope={0};
    bool admitted=prime_native_region_scope_enter(control->owned_top_episode,&scope);
    CettaControlPlan plan;
    CettaObservationDemand demand=prime_native_control_demand(control,limit,policy);
    admitted=admitted && cetta_control_plan_derive(demand,CETTA_CONTROL_BRANCH_GENERAL,
        CETTA_CONTROL_BATCH_SINGLETON_ONLY,&plan);
    if (admitted && policy->cost_compact && !control->event_cost.compact) {
        admitted=cetta_native_cost_forget_events(&control->event_cost);
        if (admitted) control->policy.cost_compact=true;
    }
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    admitted=admitted && prime_native_history_configure(control,&policy->history);
#endif
    admitted=admitted && prime_native_control_receipts(control,policy->receipts);
    if (admitted) {
        control->hub.plan=plan;
        control->policy.all=policy->all;
        control->leased=true;
        operation->owns_lease=true;
    }
    prime_native_region_scope_leave(&scope);
    if (!admitted) return false;
    PrimeEvalStackFrame *frame=prime_eval_stack_frame_new(PRIME_EVAL_STACK_FRAME_CONTROL_NESTED);
    frame->space=space; frame->arena=arena; frame->target=outcomes;
    frame->control_nested=control;
    frame->control_nested_operation=operation;
    frame->control_nested_policy=*policy;
    frame->control_nested_budget=policy->work_budget;
    frame->control_nested_retained=true;
    frame->control_nested_handle=control->handle_id;
    /* The carrier already owns this immutable complete origin. Copying the
     * frame ref retains the same tuple; no registry lookup or issuance. */
    frame->ref=atom_deep_copy(arena,operation->carrier->origin);
    operation->parent_frame=frame;
    operation->parent_control=parent;
    prime_eval_stack_push(frame);
    *unpublished=NULL;
    return true;
}
