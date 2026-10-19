struct CettaPrimeSourceScope {
    PrimeNativeTopEpisode *owner;
    PrimeNativeRegionScope region;
    struct CettaPrimeSourceScope *previous;
    uint64_t allowance;
};

static bool prime_ordinary_source_current(const PrimeNativeTopEpisode *owner) {
    if (!owner || !owner->ordinary_source || !owner->owned_region) return false;
    PrimeNativeOwnedRegion *region=owner->owned_region;
    if (!region->ready || owner->space!=prime_native_region_program(region) ||
        owner->registry!=&region->registry || owner->persistent!=&region->persistent ||
        owner->library!=region->library ||
        !space_read_view_is_current(&owner->source_view) ||
        (region->private_data &&
            (!space_read_token_is_current(region->data_read) ||
             !space_program_token_is_current(region->program_read))) ||
        memcmp(&owner->source_session,&region->library->session,sizeof(owner->source_session)))
        return false;
    const Registry *before=&owner->source_registry.initial,*now=owner->registry;
    if (before->len!=now->len || registry_state_count(now)) return false;
    for (uint32_t i=0u;i<before->len;i++) {
        const RegistryEntry *a=&before->entries[i],*b=&now->entries[i];
        if (a->key!=b->key || !atom_eq(a->value,b->value)) return false;
        if (a->key==SYMBOL_ID_NONE && !atom_eq(
            (Atom *)registry_entry_name_key(before,i),
            (Atom *)registry_entry_name_key(now,i))) return false;
    }
    return true;
}

/* Selection authenticates the actual thread services, independently of
 * source currency. A selected region pointer alone is not a service lease. */
static bool prime_ordinary_source_selected(PrimeNativeTopEpisode *owner) {
    if (!owner || !owner->owned_region) return false;
    PrimeNativeOwnedRegion *region=owner->owned_region;
    PrimeNativeSourceServices *services=region->services;
    return services && atomic_load_explicit(&services->active,memory_order_acquire) &&
        g_prime_owned_region==region && g_library_context==owner->library &&
        g_registry==owner->registry && g_eval_root_space==owner->space &&
        !g_hashcons && g_eval_term_universe_override==&owner->library->term_universe &&
        g_eval_fallback_universe.persistent_arena==owner->persistent &&
        g_equation_match_cache_selected==&services->selectors &&
        space_execution_analysis_cache_selected()==services->effects &&
        g_symbols==owner->source_symbols && g_var_intern==owner->source_variables;
}

CettaContinuationStatus eval_prime_source_owner_capture(Space *space,Registry *registry,
    Atom *expression,size_t inspections,size_t bytes,CettaPrimeSourceOwner **result) {
    return eval_prime_source_owner_capture_profile(space,registry,expression,
        CETTA_PRIME_SOURCE_IMMUTABLE,inspections,bytes,result);
}
CettaContinuationStatus eval_prime_source_owner_capture_profile(Space *space,Registry *registry,
    Atom *expression,CettaPrimeSourceProfile profile,size_t inspections,size_t bytes,
    CettaPrimeSourceOwner **result) {
    if (!result || *result) return CETTA_CONTINUATION_UNSUPPORTED;
    if (profile!=CETTA_PRIME_SOURCE_IMMUTABLE &&
        profile!=CETTA_PRIME_SOURCE_PRIVATE_DATA_WORLD) return CETTA_CONTINUATION_UNSUPPORTED;
    CettaContinuationStatus status=CETTA_CONTINUATION_UNSUPPORTED;
    PrimeNativeOwnedRegion *region=prime_native_owned_region_capture(
        space,registry,expression,expression,inspections,bytes,
        profile==CETTA_PRIME_SOURCE_PRIVATE_DATA_WORLD,&status);
    if (!region) return status;
    PrimeNativeTopEpisode *owner=calloc(1u,sizeof(*owner));
    if (!owner) {prime_native_owned_region_free(region);return CETTA_CONTINUATION_CAPACITY;}
    if (region->private_data) {
        PrimeNativeRegionScope selected={0};
        bool entered=prime_native_region_scope_select(region,&selected);
        CettaNativeCostScope cost;
        cetta_native_cost_scope_enter(&cost,&region->preparation,0u);
        CettaCount rows=space_length64(&region->world);
        bool room=region->inspection_spent<region->inspection_limit &&
            rows<region->inspection_limit-region->inspection_spent;
        if (entered && room) {
            region->inspection_spent+=(size_t)rows;
            cetta_native_cost_charge(CETTA_COST_ANALYSIS_STEP,(uint64_t)rows);
            region->program=space_heap_clone_shallow(&region->world);
        }
        PrimeNativeRegionCapture capture={.region=region,.status=CETTA_CONTINUATION_CAPACITY};
        bool okay=region->program && space_length64(region->program)==rows &&
            prime_native_region_measure(&capture,NULL);
        cetta_native_cost_scope_leave(&cost);
        prime_native_region_scope_leave(&selected);
        if (!okay) {
            free(owner);prime_native_owned_region_free(region);
            return CETTA_CONTINUATION_CAPACITY;
        }
    }
    atomic_init(&owner->references,1u);
    owner->owned_region=region;owner->ordinary_source=true;
    owner->space=prime_native_region_program(region);owner->registry=&region->registry;
    owner->persistent=&region->persistent;owner->library=region->library;
    owner->request_expression=region->expression;
    owner->source_session=region->library->session;
    owner->source_symbols=g_symbols;owner->source_variables=g_var_intern;
    arena_init_detached(&owner->request_storage);owner->request_storage_ready=true;
    PrimeNativeRegionScope selected={0};
    bool entered=prime_native_region_scope_enter(owner,&selected);
    bool admitted=entered &&
        prime_native_owned_region_call_admitted(owner->space,&owner->request_storage,
            owner->request_expression,region->private_data) &&
        space_read_view_capture(owner->space,&owner->source_view) &&
        registry_read_view_init(&owner->source_registry,owner->registry,&owner->request_storage);
    prime_native_region_scope_leave(&selected);
    if (!admitted) {prime_native_top_episode_release(owner);return CETTA_CONTINUATION_UNSUPPORTED;}
    region->data_read=space_read_token(&region->world);
    region->program_read=space_program_token(owner->space);
    *result=owner;return CETTA_CONTINUATION_READY;
}

void eval_prime_source_owner_retain(CettaPrimeSourceOwner *owner) {
    prime_native_top_episode_retain(owner);
}
CettaContinuationStatus eval_prime_source_owner_capture_context(CettaLibraryContext *context,
    Space *space,Registry *registry,Atom *expression,CettaPrimeSourceProfile profile,
    size_t inspections,size_t bytes,CettaPrimeSourceOwner **result) {
    if (!context || context->session.language_id!=CETTA_LANGUAGE_PRIME ||
        !context->session.profile || context->session.profile->id!=CETTA_PROFILE_PRIME_DEFAULT ||
        cetta_parallel_worker_active() || g_prime_ordinary_return)
        return CETTA_CONTINUATION_UNSUPPORTED;
    int fuel=context->session.options.fuel_limit;
    Registry *old_registry=g_registry;
    CettaLibraryContext *previous=eval_swap_library_context(context);
    context->session.options.fuel_limit=-1;g_registry=registry;
    CettaContinuationStatus status=eval_prime_source_owner_capture_profile(space,registry,
        expression,profile,inspections,bytes,result);
    context->session.options.fuel_limit=fuel;g_registry=old_registry;
    eval_swap_library_context(previous);
    return status;
}
void eval_prime_source_owner_release(CettaPrimeSourceOwner *owner) {
    if (owner) prime_native_top_episode_release(owner);
}
bool eval_prime_source_owner_is_current(const CettaPrimeSourceOwner *owner) {
    return owner && owner->owned_region && owner->owned_region->services &&
        !cetta_parallel_worker_active() &&
        g_symbols==owner->source_symbols && g_var_intern==owner->source_variables &&
        (!atomic_load_explicit(&owner->owned_region->services->active,memory_order_acquire) ||
         g_prime_owned_region==owner->owned_region) &&
        prime_ordinary_source_current(owner);
}

CettaContinuationStatus eval_prime_source_scope_open(CettaPrimeSourceOwner *owner,
    uint64_t allowance,CettaPrimeSourceScope **result) {
    if (!owner || !result || *result || cetta_parallel_worker_active())
        return CETTA_CONTINUATION_UNSUPPORTED;
    /* The caller's live reference is the admission premise. Retain before
     * selecting or inspecting mutable service currency. */
    prime_native_top_episode_retain(owner);
    CettaPrimeSourceScope *scope=calloc(1u,sizeof(*scope));
    if (!scope) {prime_native_top_episode_release(owner);return CETTA_CONTINUATION_CAPACITY;}
    scope->owner=owner;scope->allowance=allowance;
    if (!prime_native_region_scope_enter(owner,&scope->region) ||
        !prime_ordinary_source_selected(owner) || !prime_ordinary_source_current(owner)) {
        prime_native_region_scope_leave(&scope->region);
        free(scope);prime_native_top_episode_release(owner);
        return CETTA_CONTINUATION_INVALIDATED;
    }
    scope->previous=g_prime_ordinary_source_scope;
    g_prime_ordinary_source_scope=scope;*result=scope;
    return CETTA_CONTINUATION_READY;
}
void eval_prime_source_scope_close(CettaPrimeSourceScope *scope) {
    if (!scope) return;
    assert(g_prime_ordinary_source_scope==scope);
    g_prime_ordinary_source_scope=scope->previous;
    PrimeNativeTopEpisode *owner=scope->owner;
    prime_native_region_scope_leave(&scope->region);
    free(scope);prime_native_top_episode_release(owner);
}
bool eval_prime_source_scope_inputs(CettaPrimeSourceScope *scope,Space **space,
    Registry **registry,Arena **persistent,Atom **expression) {
    if (!scope || scope!=g_prime_ordinary_source_scope ||
        !prime_ordinary_source_selected(scope->owner) ||
        !prime_ordinary_source_current(scope->owner)) return false;
    if (space) *space=scope->owner->space;
    if (registry) *registry=scope->owner->registry;
    if (persistent) *persistent=scope->owner->persistent;
    if (expression) *expression=scope->owner->request_expression;
    return true;
}

/* Every ordinary execution still enters through the existing top evaluator.
 * This scope only selects a retained representation of its original episode. */
typedef struct {
    PrimeNativeRegionScope region;
    PrimeNeedActiveGuard need;
    EvalExecutionContext *execution;
    Arena *owner,*episode_owner,*branch_owner;
    PrimeNeedRegionPool *pool;
    CettaPrimeNeedAnswerObserver observer;
    void *observer_context;
    CettaPrimeObservationReturn *previous;
    bool active;
} PrimeOrdinaryReturnScope;

static bool prime_ordinary_return_enter(CettaPrimeObservationReturn *owned,
    PrimeOrdinaryReturnScope *scope) {
    PrimeNativeTopEpisode *episode=owned->top.episode;
    if (owned->ordinary_running || !episode || !episode->source_episode ||
        cetta_parallel_worker_active() ||
        !prime_native_region_scope_enter(episode,&scope->region)) return false;
    if (!prime_native_owned_top_context_admitted() ||
        !prime_ordinary_source_selected(episode->source_episode) ||
        !prime_ordinary_source_current(episode->source_episode)) {
        prime_native_region_scope_leave(&scope->region);return false;
    }
    owned->ordinary_running=true;scope->active=true;
    prime_need_active_enter(&scope->need,NULL);
    scope->execution=g_eval_execution;g_eval_execution=episode->execution;
    scope->owner=g_prime_need_owner;scope->episode_owner=g_prime_need_episode_owner;
    scope->branch_owner=g_prime_need_region_branch_owner;scope->pool=g_prime_need_region_pool;
    scope->observer=g_prime_need_answer_observer;scope->observer_context=g_prime_need_answer_observer_context;
    scope->previous=g_prime_ordinary_return;g_prime_ordinary_return=owned;
    prime_need_saved_context_install(&episode->context);
    g_prime_need_owner=g_prime_need_episode_owner=&episode->arena;
    g_prime_need_region_pool=episode->regions.stable_owner?&episode->regions:NULL;
    g_prime_need_region_branch_owner=NULL;
    g_prime_need_answer_observer=episode->observer;
    g_prime_need_answer_observer_context=episode->observer_context;
    return true;
}
static void prime_ordinary_return_leave(CettaPrimeObservationReturn *owned,
    PrimeOrdinaryReturnScope *scope) {
    if (!scope->active) return;
    prime_native_top_snapshot(owned->top.episode);
    g_eval_execution=scope->execution;
    g_prime_need_owner=scope->owner;g_prime_need_episode_owner=scope->episode_owner;
    g_prime_need_region_branch_owner=scope->branch_owner;g_prime_need_region_pool=scope->pool;
    g_prime_need_answer_observer=scope->observer;g_prime_need_answer_observer_context=scope->observer_context;
    g_prime_ordinary_return=scope->previous;
    prime_need_active_leave(&scope->need);
    owned->ordinary_running=false;
    prime_native_region_scope_leave(&scope->region);scope->active=false;
}

static bool prime_ordinary_result_slot(ResultSet *results) {
#ifdef CETTA_PRIME_ORDINARY_RETURN_TEST
    if (!prime_ordinary_test_slot(results)) return false;
#endif
    return result_set_ensure_one(results);
}
static bool prime_ordinary_return_project(void *raw,const CettaOwnedReturn *returned) {
    CettaPrimeObservationReturn *owned=raw;
    PrimeNativeControl *control=owned->ordinary_control;
    ResultSet *results=owned->top.destination;
    uint64_t index=owned->ordinary_returns.accepted;
    if (!control || index>=control->published || index>=control->output.len ||
        returned->environment!=&control->output.items[index].env ||
        returned->outcome.term!=owned->ordinary_reified ||
        !results || results->len!=owned->ordinary_result_length) {
        owned->ordinary_refusal="OrdinaryPublicationIdentity";return false;
    }
    if (!owned->ordinary_projected) {
        if (!owned->ordinary_copy) owned->ordinary_copy=atom_deep_copy_session_new(owned->output);
        if (owned->ordinary_copy)
            owned->ordinary_projected=atom_deep_copy_session_copy(
                owned->ordinary_copy,owned->ordinary_reified);
        if (!owned->ordinary_projected) return false;
        PrimeEvalStackSupportBuffer graph;prime_eval_stack_support_buffer_init(&graph);
        CettaStorageCensus seen={0};
        bool closed=prime_eval_stack_support_buffer_push(&graph,owned->ordinary_projected) &&
            prime_ordinary_immutable_graph(&graph,&seen) &&
            atom_graph_is_closed_for_arena(owned->output,owned->ordinary_projected);
        cetta_storage_census_free(&seen);prime_eval_stack_support_buffer_free(&graph);
        if (!closed) {owned->ordinary_refusal="OrdinaryEscapingPayload";return false;}
    }
    if (owned->ordinary_refusal || !prime_ordinary_result_slot(results)) return false;
    result_set_add(results,owned->ordinary_projected);
    owned->ordinary_result_length=results->len;
    owned->ordinary_reified=owned->ordinary_projected=NULL;
    return true;
}
static bool prime_ordinary_stop_after_row(Arena *arena,Atom *value,const Outcome *occurrence,void *raw) {
    const Bindings *env = &occurrence->env;
    (void)arena;(void)value;(void)env;(void)raw;
    return false; /* Existing visitor contract: accept this row, then stop. */
}
/* This provider advances the retained Prime controller, not a reconstructed
 * answer list. Its work allowance is independent of accepted-return count. */
static CettaOwnedExecutionStep prime_ordinary_return_next(void *raw,
    CettaOwnedReturn *returned,CettaEvalCompletion *completion) {
    CettaPrimeObservationReturn *owned=raw;
    PrimeNativeControl *control=owned->ordinary_control;
    assert(control && control->owned_top_episode==owned->top.episode);
    while (owned->ordinary_next>=control->published) {
        if (control->invalidated) {
            owned->top.owned.status=CETTA_CONTINUATION_INVALIDATED;
            *completion=CETTA_EVAL_INCOMPLETE_INVALIDATED;
            return CETTA_OWNED_EXECUTION_HANDOFF;
        }
        if (control->failed) {
            owned->top.owned.status=CETTA_CONTINUATION_DEFERRED;
            /* Inner scopes can finish while their authority owner refuses
             * an operation. The exported producer must still report that
             * failure, rather than certify complete answer exhaustion. An
             * unclassified host failure supplies no permission verdict. */
            *completion=control->completion==CETTA_EVAL_COMPLETE?
                CETTA_EVAL_INCOMPLETE_HOST_FAILURE:control->completion;
            return CETTA_OWNED_EXECUTION_HANDOFF;
        }
        if (prime_native_control_closed(control)) {
            *completion=CETTA_EVAL_COMPLETE;
            return CETTA_OWNED_EXECUTION_COMPLETE;
        }
        uint64_t before=control->steps;size_t published=control->published;
        prime_native_control_run(control,owned->ordinary_allowance,prime_ordinary_stop_after_row,NULL);
        uint64_t spent=control->steps-before;
        assert(spent<=owned->ordinary_allowance);
        owned->ordinary_allowance-=spent;
        if (control->published==published && control->steps==before) {
            owned->top.owned.status=control->invalidated?CETTA_CONTINUATION_INVALIDATED:
                control->driver.capacity_blocked?CETTA_CONTINUATION_CAPACITY:CETTA_CONTINUATION_DEFERRED;
            *completion=control->invalidated?CETTA_EVAL_INCOMPLETE_INVALIDATED:
                control->completion==CETTA_EVAL_COMPLETE?CETTA_EVAL_INCOMPLETE_CAPACITY:control->completion;
            return CETTA_OWNED_EXECUTION_PAUSED;
        }
    }
    uint64_t index=owned->ordinary_next;
    if (index>=control->output.len || index>=control->delivered_capacity ||
        !control->delivered[index]) {
        owned->ordinary_refusal="OrdinaryPublicationIdentity";
        owned->top.owned.status=CETTA_CONTINUATION_UNSUPPORTED;
        *completion=CETTA_EVAL_INCOMPLETE_CAPACITY;
        return CETTA_OWNED_EXECUTION_PAUSED;
    }
    Outcome *row=&control->output.items[index];
    Atom *value=outcome_atom_materialize_traced(control->driver.arena,row,
        CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_MATERIALIZE_TOP_LEVEL);
    owned->ordinary_reified=prime_need_observe_data(control->driver.arena,value,&row->env);
    if (!owned->ordinary_reified)
        owned->ordinary_reified=atom_error(control->driver.arena,
            control->source_expression,atom_symbol(control->driver.arena,"PrimeNeedReificationCycle"));
    *returned=(CettaOwnedReturn){
        .outcome=row->delayed?cetta_call_suspended(owned->ordinary_reified,row->delayed):
            cetta_call_value(owned->ordinary_reified),
        .environment=&row->env,
        .final=index+1u==control->published && !control->invalidated &&
            !control->failed && prime_native_control_closed(control),
    };
    returned->outcome.result_form=row->result_form;
    *completion=control->completion;
    owned->ordinary_next++;
    return CETTA_OWNED_EXECUTION_RETURN;
}
static bool prime_ordinary_return_final_current(void *raw) {
    CettaPrimeObservationReturn *owned=raw;
    PrimeNativeControl *control=owned->ordinary_control;
    return control && !control->invalidated && !control->failed &&
        prime_native_control_closed(control) &&
        prime_ordinary_source_selected(owned->top.episode->source_episode) &&
        prime_ordinary_source_current(owned->top.episode->source_episode);
}
static void prime_ordinary_return_release(void *raw) {
    CettaPrimeObservationReturn *owned=raw;
    prime_native_control_destroy(owned->ordinary_control);
    owned->ordinary_control=NULL;
}
static void prime_ordinary_return_take_control(CettaPrimeObservationReturn *owned,
    PrimeNativeControl *control) {
    assert(owned && !owned->ordinary_control && control);
    control->owned_top_episode=owned->top.episode;
    owned->ordinary_control=control;
    owned->top.owned.initialized=true;
    cetta_owned_execution_init(&owned->ordinary_returns,owned,
        prime_ordinary_return_next,prime_ordinary_return_release);
    owned->ordinary_returns.final_is_current=prime_ordinary_return_final_current;
}
static void prime_ordinary_return_pump(CettaPrimeObservationReturn *owned,uint64_t allowance) {
    assert(owned->ordinary_control);
    if (owned->ordinary_allowance!=UINT64_MAX) {
        if (allowance==UINT64_MAX) owned->ordinary_allowance=UINT64_MAX;
        else if (allowance>UINT64_MAX-owned->ordinary_allowance) {
            owned->top.owned.status=CETTA_CONTINUATION_CAPACITY;
            eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);return;
        } else owned->ordinary_allowance+=allowance;
    }
    /* A zero work grant can still retry an already-paid pending publication.
     * The shared driver's allowance counts accepted returns, not that work. */
    CettaOwnedExecutionStep step=cetta_owned_execution_sequence(
        &owned->ordinary_returns,UINT64_MAX,prime_ordinary_return_project,owned);
    if (step==CETTA_OWNED_EXECUTION_COMPLETE) {
        owned->top.committed=true;
        owned->top.owned.status=CETTA_CONTINUATION_READY;
    } else if (owned->ordinary_returns.has_pending) {
        owned->top.owned.status=owned->ordinary_refusal?
            CETTA_CONTINUATION_UNSUPPORTED:CETTA_CONTINUATION_CAPACITY;
        eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);
    } else eval_mark_incomplete(owned->ordinary_returns.completion);
}

static void prime_ordinary_return_free(CettaPrimeObservationReturn *owned) {
    assert(owned && owned->ordinary && !owned->ordinary_running);
    PrimeNativeTopEpisode *episode=owned->top.episode;
    PrimeNativeRegionScope region={0};
    bool entered=prime_native_region_scope_enter(episode,&region);
    assert(entered);(void)entered;
    EvalExecutionContext *previous=g_eval_execution;
    g_eval_execution=episode->execution;
    cetta_owned_execution_cancel(&owned->ordinary_returns);
    /* An exported driver no longer occupies the embedded consumer slot.
     * The address-stable source owner still owns this same controller. */
    if (owned->ordinary_control) prime_ordinary_return_release(owned);
    atom_deep_copy_session_free(owned->ordinary_copy);
    outcome_set_free(&owned->pending);arena_free(&owned->holders);
    g_eval_execution=previous;
    prime_native_region_scope_leave(&region);
    prime_native_top_episode_release(episode);free(owned);
}
static CettaContinuationStatus prime_ordinary_return_invalidate(EvalOutcome *outcome,
    const char *reason) {
    CettaPrimeObservationReturn *owned=outcome->prime_observation;
    owned->top.owned.status=CETTA_CONTINUATION_INVALIDATED;
    owned->ordinary_refusal=reason;
    outcome->completion=CETTA_EVAL_INCOMPLETE_INVALIDATED;
    return CETTA_CONTINUATION_INVALIDATED;
}
static CettaContinuationStatus prime_ordinary_return_resume(EvalOutcome *outcome,uint64_t allowance) {
    CettaPrimeObservationReturn *owned=outcome->prime_observation;
    if (owned->top.owned.status==CETTA_CONTINUATION_INVALIDATED)
        return CETTA_CONTINUATION_INVALIDATED;
    if (owned->top.destination!=&outcome->results || !owned->ordinary_control)
        return prime_ordinary_return_invalidate(outcome,"OrdinaryReturnIdentity");
    if (owned->top.committed) return CETTA_CONTINUATION_READY;
    PrimeOrdinaryReturnScope scope={0};
    if (!prime_ordinary_return_enter(owned,&scope))
        return prime_ordinary_return_invalidate(outcome,"OrdinarySourceAuthority");
    EvalCompletionTracker tracker={.completion=CETTA_EVAL_COMPLETE,.parent=g_eval_completion_tracker};
    g_eval_completion_tracker=&tracker;
    __attribute__((cleanup(prime_eval_top_leave))) PrimeEvalTopGuard top_guard;
    prime_eval_top_enter(&top_guard);
    g_metta_eval_depth++;
    prime_ordinary_return_pump(owned,allowance);
    g_metta_eval_depth--;
    g_eval_completion_tracker=tracker.parent;
    outcome->completion=tracker.completion;
    prime_ordinary_return_leave(owned,&scope);
    CettaContinuationStatus status=owned->top.owned.status;
    if (owned->top.committed) {
        outcome->prime_observation=NULL;prime_ordinary_return_free(owned);
    }
    return status;
}

/* An explicitly owned root starts on the same controller before
 * source evaluation. A zero allowance retains its first task; it is not an
 * empty answer and cannot run an effect through the synchronous fallback. */
static bool prime_ordinary_retained_root(Space *space, Arena *arena,
    Atom *type, Atom *expression, int fuel, EvalOutcome *outcome) {
    CettaPrimeObservationReturn *owned = g_prime_ordinary_return;
    if (!owned || !owned->ordinary || owned->ordinary_control ||
        fuel >= 0 ||
        owned->top.destination != &outcome->results ||
        space != owned->top.episode->space ||
        arena != &owned->top.episode->arena ||
        expression != owned->top.episode->request_expression)
        return false;
    PrimeNativeControl *control = calloc(1u, sizeof(*control));
    if (!control) {
        owned->top.owned.status = CETTA_CONTINUATION_CAPACITY;
        eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);
        return true;
    }
    int64_t prepared = eval_option_int_or_default("prepared-node-quantum", 1);
    CettaSearchPolicySpec policy = {
        .native_control = true, .all = true, .workers = eval_current_num_threads(),
        .work_budget = UINT64_MAX, .worker_quantum = 64u,
        .prepared_node_quantum = prepared > 0 ? (uint64_t)prepared : 1u,
    };
    CettaSelectionAutomaton schedule;
    bool initialized = prime_native_control_init(control, space, arena,
        expression, fuel, false,
        (CettaObservationDemand){.completion = CETTA_OBSERVATION_COMPLETE_BAG},
        &policy, NULL, NULL, type);
    if (initialized)
        control->hub.selection_duty = CETTA_SELECTION_DUTY_NONE;
    if (!initialized || control->registry_read.incomplete ||
        !cetta_selection_automaton_lifo(&schedule) ||
        !cetta_continuation_hub_switch_schedule(&control->hub, &schedule)) {
        prime_native_control_destroy(control);
        owned->top.owned.status = CETTA_CONTINUATION_CAPACITY;
        eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);
        return true;
    }
    control->driver.ordinary_ordered = true;
    control->driver.ordinary_shared_source = prime_ordinary_shared_source_admitted(space);
    control->driver.ordinary_ready_queue = true;
    control->driver.ordinary_refill = true;
    control->driver.ordinary_descendants = true;
    prime_ordinary_return_take_control(owned,control);
    prime_ordinary_return_pump(owned, 0u);
    return true;
}

static bool prime_ordinary_owned_entry(Space *space,Arena *output,Arena *persistent,
    Registry *registry,Atom *expression,EvalOutcome *outcome,
    CettaPrimeNeedAnswerObserver observer,void *observer_context) {
    CettaPrimeSourceScope *scope=g_prime_ordinary_source_scope;
    if (!scope || g_prime_ordinary_return) return false;
    PrimeNativeTopEpisode *source=scope->owner;
    /* A caller that selected an owner cannot silently execute under a
     * different source or service tuple. Refusal precedes producer entry. */
    if (!prime_ordinary_source_selected(source) || !prime_ordinary_source_current(source) ||
        source->space!=space || source->registry!=registry || source->persistent!=persistent) {
        outcome->completion=CETTA_EVAL_INCOMPLETE_INVALIDATED;
        return true;
    }
    if (current_eval_fuel_limit()>=0 ||
        outcome->depth_fuel || observer || observer_context ||
        cetta_native_cost_observer_active() ||
        cetta_shared_transition_guard_held_by_current_thread() ||
        active_search_table_mode()!=CETTA_TABLE_MODE_NONE ||
        output->hashcons || output->older_identity ||
        !prime_native_owned_top_context_admitted()) return false;
    /* The admitted source already owns this entry root. Other caller roots
     * retain literal synchronous evaluation until a root-owner transport is
     * supplied; no borrowed root is silently retained across return. */
    if (expression!=source->request_expression || !expression) return false;
    CettaPrimeObservationReturn *owned=calloc(1u,sizeof(*owned));
    PrimeNativeTopEpisode *episode=calloc(1u,sizeof(*episode));
    if (!owned || !episode) {
        free(owned);free(episode);
        outcome->completion=CETTA_EVAL_INCOMPLETE_CAPACITY;
        return true;
    }
    atomic_init(&episode->references,1u);
    prime_native_top_episode_retain(source);episode->source_episode=source;
    episode->space=space;episode->registry=registry;episode->persistent=persistent;
    episode->library=source->library;episode->request_expression=expression;
    owned->ordinary=true;owned->output=output;owned->library=source->library;
    owned->top.episode=owned->top.owned.top_episode=episode;
    owned->top.destination=&outcome->results;owned->ordinary_result_length=outcome->results.len;
    owned->top.owned.status=CETTA_CONTINUATION_READY;owned->ordinary_allowance=scope->allowance;
    arena_init_detached(&owned->holders);outcome_set_init(&owned->pending);
    outcome->prime_observation=owned;owned->entered=true;
    CettaPrimeObservationReturn *previous=g_prime_ordinary_return;g_prime_ordinary_return=owned;
    eval_top_with_registry_core(space,output,persistent,registry,expression,NULL,
        &outcome->results,outcome,NULL,NULL,&owned->top);
    g_prime_ordinary_return=previous;
    if (!owned->ordinary_control || owned->top.committed) {
        /* A declined promotion completed through the original serial body. */
        outcome->prime_observation=NULL;prime_ordinary_return_free(owned);
    }
    return true;
}

static CettaOwnedExecutionStep prime_ordinary_source_next(void *raw,
    CettaOwnedReturn *returned,CettaEvalCompletion *completion) {
    CettaPrimeObservationReturn *owned=raw;
    PrimeOrdinaryReturnScope scope={0};
    if (!prime_ordinary_return_enter(owned,&scope)) {
        owned->top.owned.status=CETTA_CONTINUATION_INVALIDATED;
        *completion=CETTA_EVAL_INCOMPLETE_INVALIDATED;
        return CETTA_OWNED_EXECUTION_HANDOFF;
    }
    EvalCompletionTracker tracker={.completion=CETTA_EVAL_COMPLETE,.parent=g_eval_completion_tracker};
    g_eval_completion_tracker=&tracker;
    __attribute__((cleanup(prime_eval_top_leave))) PrimeEvalTopGuard top_guard;
    prime_eval_top_enter(&top_guard);
    g_metta_eval_depth++;
    owned->ordinary_allowance=owned->ordinary_quantum;
    CettaOwnedExecutionStep step=prime_ordinary_return_next(owned,returned,completion);
    g_metta_eval_depth--;
    g_eval_completion_tracker=tracker.parent;
    prime_ordinary_return_leave(owned,&scope);
    return step;
}
static bool prime_ordinary_source_final_current(void *raw) {
    CettaPrimeObservationReturn *owned=raw;
    PrimeNativeControl *control=owned->ordinary_control;
    return control && !control->invalidated && !control->failed &&
        prime_native_control_closed(control) &&
        eval_prime_source_owner_is_current(owned->top.episode->source_episode);
}
static void prime_ordinary_source_release(void *raw) {
    prime_ordinary_return_free(raw);
}

static CettaOwnedExecutionStep prime_ordinary_source_funded_next(void *raw,
    CettaOwnedReturn *returned,CettaEvalCompletion *completion) {
    CettaPrimeObservationReturn *owned=raw;
    uint64_t quantum=owned->ordinary_quantum;
    uint64_t available=cetta_work_credit_available(owned->ordinary_credit);
    if (available<quantum) owned->ordinary_quantum=available;
    uint64_t before=owned->ordinary_control->steps;
    CettaOwnedExecutionStep step=prime_ordinary_source_next(owned,returned,completion);
    uint64_t spent=owned->ordinary_control->steps-before;
    owned->ordinary_quantum=quantum;
    /* Controller allowance is enforced before execution. A violation is an
     * internal accounting fault, never permission to publish unpaid work. */
    if (spent>available || !cetta_work_credit_spend(owned->ordinary_credit,spent))
        abort();
    return step;
}
static void prime_ordinary_source_funded_release(void *raw) {
    CettaPrimeObservationReturn *owned=raw;
    CettaWorkCredit *credit=owned->ordinary_credit;
    prime_ordinary_return_free(owned);
    cetta_work_credit_release(credit);
}

static CettaPrimeObservationReturn *prime_ordinary_source_driver(
    const CettaOwnedExecution *execution) {
    if (!execution || !execution->computation ||
        !((execution->next==prime_ordinary_source_next &&
           execution->release==prime_ordinary_source_release) ||
          (execution->next==prime_ordinary_source_funded_next &&
           execution->release==prime_ordinary_source_funded_release)))
        return NULL;
    CettaPrimeObservationReturn *owned=execution->computation;
    PrimeNativeTopEpisode *source=owned->top.episode->source_episode;
    if (owned->ordinary_running || !source ||
        g_symbols!=source->source_symbols || g_var_intern!=source->source_variables ||
        !owned->ordinary_control) return NULL;
    return owned;
}

CettaContinuationStatus eval_prime_source_execution_fund(CettaOwnedExecution *execution,
    CettaWorkCredit *payer,uint64_t amount) {
    CettaPrimeObservationReturn *owned=prime_ordinary_source_driver(execution);
    if (!owned || owned->ordinary_credit || execution->terminal ||
        execution->has_pending || execution->accepted || owned->ordinary_next ||
        owned->ordinary_control->steps)
        return CETTA_CONTINUATION_UNSUPPORTED;
    if (!payer) return CETTA_CONTINUATION_READY;
    CettaWorkCredit *credit=cetta_work_credit_reserve(payer,amount);
    if (!credit) return CETTA_CONTINUATION_CAPACITY;
    owned->ordinary_credit=credit;
    execution->next=prime_ordinary_source_funded_next;
    execution->release=prime_ordinary_source_funded_release;
    return CETTA_CONTINUATION_READY;
}

bool eval_prime_source_execution_add_credit(const CettaOwnedExecution *execution,
    CettaWorkCredit *payer,uint64_t amount) {
    CettaPrimeObservationReturn *owned=prime_ordinary_source_driver(execution);
    return owned && owned->ordinary_credit && !execution->terminal &&
        cetta_work_credit_transfer(payer,owned->ordinary_credit,amount);
}

bool eval_prime_source_execution_is_current(const CettaOwnedExecution *execution) {
    CettaPrimeObservationReturn *owned=prime_ordinary_source_driver(execution);
    return owned && eval_prime_source_owner_is_current(owned->top.episode->source_episode);
}
bool eval_prime_source_execution_work(const CettaOwnedExecution *execution,uint64_t *work) {
    CettaPrimeObservationReturn *owned=prime_ordinary_source_driver(execution);
    if (!owned || !work) return false;
    *work=owned->ordinary_control->steps;
    return true;
}
bool eval_prime_source_execution_set_quantum(const CettaOwnedExecution *execution,uint64_t quantum) {
    CettaPrimeObservationReturn *owned=prime_ordinary_source_driver(execution);
    if (!owned || quantum==UINT64_MAX) return false;
    owned->ordinary_quantum=quantum;
    return true;
}

#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
static CettaPrimeObservationReturn *prime_ordinary_source_history_owner(
    const CettaOwnedExecution *execution) {
    CettaPrimeObservationReturn *owned=prime_ordinary_source_driver(execution);
    return owned && prime_native_history_quiescent(owned->ordinary_control)?owned:NULL;
}
#endif
CettaContinuationStatus eval_prime_source_execution_record(const CettaOwnedExecution *execution,
    const PrimeNativeHistorySpec *spec) {
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    CettaPrimeObservationReturn *owned=prime_ordinary_source_history_owner(execution);
    if (!owned || !spec) return CETTA_CONTINUATION_UNSUPPORTED;
    PrimeNativeRegionScope scope={0};
    if (!prime_native_region_scope_enter(owned->top.episode,&scope))
        return CETTA_CONTINUATION_DEFERRED;
    bool ready=prime_native_history_configure(owned->ordinary_control,spec);
    prime_native_region_scope_leave(&scope);
    return ready?CETTA_CONTINUATION_READY:CETTA_CONTINUATION_UNSUPPORTED;
#else
    (void)execution;(void)spec;
    return CETTA_CONTINUATION_UNSUPPORTED;
#endif
}
CettaContinuationStatus eval_prime_source_execution_forget(const CettaOwnedExecution *execution) {
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    CettaPrimeObservationReturn *owned=prime_ordinary_source_history_owner(execution);
    if (!owned) return CETTA_CONTINUATION_UNSUPPORTED;
    PrimeNativeRegionScope scope={0};
    if (!prime_native_region_scope_enter(owned->top.episode,&scope))
        return CETTA_CONTINUATION_DEFERRED;
    bool ready=prime_native_history_forget_control(owned->ordinary_control);
    prime_native_region_scope_leave(&scope);
    return ready?CETTA_CONTINUATION_READY:CETTA_CONTINUATION_DEFERRED;
#else
    (void)execution;
    return CETTA_CONTINUATION_UNSUPPORTED;
#endif
}
CettaContinuationStatus eval_prime_source_execution_history(const CettaOwnedExecution *execution,
    Arena *arena,Atom **result) {
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    CettaPrimeObservationReturn *owned=prime_ordinary_source_history_owner(execution);
    if (!owned || !arena || !result || *result) return CETTA_CONTINUATION_UNSUPPORTED;
    PrimeNativeRegionScope scope={0};
    if (!prime_native_region_scope_enter(owned->top.episode,&scope))
        return CETTA_CONTINUATION_DEFERRED;
    PrimeNativeHistory *history=&owned->ordinary_control->history;
    *result=history->forgotten?atom_symbol(arena,"Forgotten"):
        !history->spec.enabled && !history->stopped?atom_symbol(arena,"Unrecorded"):
        prime_native_history_atom(owned->ordinary_control,arena);
    prime_native_region_scope_leave(&scope);
    return CETTA_CONTINUATION_READY;
#else
    (void)execution;(void)arena;(void)result;
    return CETTA_CONTINUATION_UNSUPPORTED;
#endif
}
CettaContinuationStatus eval_prime_source_execution_open(CettaPrimeSourceOwner *source,
    uint64_t quantum,CettaOwnedExecution *execution) {
    if (!execution || execution->computation || execution->has_pending ||
        !quantum || quantum==UINT64_MAX)
        return CETTA_CONTINUATION_UNSUPPORTED;
    CettaPrimeSourceScope *scope=NULL;
    CettaContinuationStatus status=eval_prime_source_scope_open(source,0u,&scope);
    if (status!=CETTA_CONTINUATION_READY) return status;
    Arena receiver;arena_init_detached(&receiver);
    EvalOutcome outcome;eval_outcome_init(&outcome);
    bool entered=prime_ordinary_owned_entry(source->space,&receiver,source->persistent,
        source->registry,source->request_expression,&outcome,NULL,NULL);
    CettaPrimeObservationReturn *owned=outcome.prime_observation;
    status=CETTA_CONTINUATION_UNSUPPORTED;
    if (entered && owned && owned->ordinary_control &&
        !owned->ordinary_control->steps && !outcome.results.len &&
        !owned->ordinary_returns.accepted && !owned->ordinary_returns.has_pending &&
        !owned->ordinary_copy && !owned->ordinary_projected) {
        owned->ordinary_returns.relocatable=true;
        if (cetta_owned_execution_move(execution,&owned->ordinary_returns)) {
            execution->next=prime_ordinary_source_next;
            execution->release=prime_ordinary_source_release;
            execution->final_is_current=prime_ordinary_source_final_current;
            owned->ordinary_quantum=quantum;
            /* No pointer to either temporary publication receiver escapes. */
            owned->output=NULL;owned->top.destination=NULL;
            outcome.prime_observation=NULL;
            status=CETTA_CONTINUATION_READY;
        }
    } else if (entered && outcome.completion==CETTA_EVAL_INCOMPLETE_CAPACITY)
        status=CETTA_CONTINUATION_CAPACITY;
    else if (entered && outcome.completion==CETTA_EVAL_INCOMPLETE_INVALIDATED)
        status=CETTA_CONTINUATION_INVALIDATED;
    eval_outcome_free(&outcome);arena_free(&receiver);
    eval_prime_source_scope_close(scope);
    return status;
}
