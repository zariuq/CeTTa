struct CettaPrimeSourceScope {
    PrimeNativeTopEpisode *owner;
    PrimeNativeRegionScope region;
    struct CettaPrimeSourceScope *previous;
    uint64_t allowance;
};

static bool prime_ordinary_source_current(PrimeNativeTopEpisode *owner) {
    if (!owner || !owner->ordinary_source || !owner->owned_region) return false;
    PrimeNativeOwnedRegion *region=owner->owned_region;
    if (!region->ready || owner->space!=&region->world ||
        owner->registry!=&region->registry || owner->persistent!=&region->persistent ||
        owner->library!=region->library || !space_read_view_is_current(&owner->source_view) ||
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
    if (!result || *result) return CETTA_CONTINUATION_UNSUPPORTED;
    CettaContinuationStatus status=CETTA_CONTINUATION_UNSUPPORTED;
    PrimeNativeOwnedRegion *region=prime_native_owned_region_capture(
        space,registry,expression,inspections,bytes,&status);
    if (!region) return status;
    PrimeNativeTopEpisode *owner=calloc(1u,sizeof(*owner));
    if (!owner) {prime_native_owned_region_free(region);return CETTA_CONTINUATION_CAPACITY;}
    atomic_init(&owner->references,1u);
    owner->owned_region=region;owner->ordinary_source=true;
    owner->space=&region->world;owner->registry=&region->registry;
    owner->persistent=&region->persistent;owner->library=region->library;
    owner->request_expression=region->expression;
    owner->source_session=region->library->session;
    owner->source_symbols=g_symbols;owner->source_variables=g_var_intern;
    arena_init_detached(&owner->request_storage);owner->request_storage_ready=true;
    PrimeNativeRegionScope selected={0};
    bool entered=prime_native_region_scope_enter(owner,&selected);
    bool admitted=entered &&
        prime_native_owned_region_call_admitted(owner->space,&owner->request_storage,
            owner->request_expression) &&
        space_read_view_capture(owner->space,&owner->source_view) &&
        registry_read_view_init(&owner->source_registry,owner->registry,&owner->request_storage);
    prime_native_region_scope_leave(&selected);
    if (!admitted) {prime_native_top_episode_release(owner);return CETTA_CONTINUATION_UNSUPPORTED;}
    *result=owner;return CETTA_CONTINUATION_READY;
}

void eval_prime_source_owner_retain(CettaPrimeSourceOwner *owner) {
    prime_native_top_episode_retain(owner);
}
void eval_prime_source_owner_release(CettaPrimeSourceOwner *owner) {
    if (owner) prime_native_top_episode_release(owner);
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
    scope->need=prime_need_active_enter(NULL);
    scope->execution=g_eval_execution;g_eval_execution=episode->execution;
    scope->owner=g_prime_need_owner;scope->episode_owner=g_prime_need_episode_owner;
    scope->branch_owner=g_prime_need_region_branch_owner;scope->pool=g_prime_need_region_pool;
    scope->observer=g_prime_need_answer_observer;scope->observer_context=g_prime_need_answer_observer_context;
    scope->previous=g_prime_ordinary_return;g_prime_ordinary_return=owned;
    g_prime_need_active=episode->need;g_prime_need_branch_state_active=episode->branch;
    g_prime_weight_active=episode->weights;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    g_prime_need_receipt_active=episode->receipt;
#endif
    g_prime_need_logical_env=NULL;
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
static bool prime_ordinary_return_project(CettaPrimeObservationReturn *owned) {
    PrimeNativeControl *control=owned->ordinary_control;
    ResultSet *results=owned->top.destination;
    if (!control || owned->ordinary_cursor>=control->published) return true;
    if (owned->ordinary_cursor>=control->output.len ||
        owned->ordinary_cursor>=control->delivered_capacity ||
        !control->delivered[owned->ordinary_cursor] ||
        !results || results->len!=owned->ordinary_result_length) {
        owned->ordinary_refusal="OrdinaryPublicationIdentity";return false;
    }
    Outcome *row=&control->output.items[owned->ordinary_cursor];
    if (!owned->ordinary_reified) {
        Atom *value=outcome_atom_materialize_traced(control->driver.arena,row,
            CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_MATERIALIZE_TOP_LEVEL);
        owned->ordinary_reified=prime_need_observe_data(control->driver.arena,value,&row->env);
        if (!owned->ordinary_reified)
            owned->ordinary_reified=atom_error(control->driver.arena,
                control->source_expression,atom_symbol(control->driver.arena,"PrimeNeedReificationCycle"));
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
    prime_need_observe_top_answer(owned->ordinary_projected,&row->env);
    owned->ordinary_result_length=results->len;
    owned->ordinary_cursor++;
    owned->ordinary_reified=owned->ordinary_projected=NULL;
    return true;
}
static bool prime_ordinary_stop_after_row(Arena *arena,Atom *value,const Outcome *occurrence,void *raw) {
    const Bindings *env = &occurrence->env;
    (void)arena;(void)value;(void)env;(void)raw;
    return false; /* Existing visitor contract: accept this row, then stop. */
}
static void prime_ordinary_return_pump(CettaPrimeObservationReturn *owned,uint64_t allowance) {
    PrimeNativeControl *control=owned->ordinary_control;
    assert(control && control->owned_top_episode==owned->top.episode);
    if (owned->ordinary_allowance!=UINT64_MAX) {
        if (allowance==UINT64_MAX) owned->ordinary_allowance=UINT64_MAX;
        else if (allowance>UINT64_MAX-owned->ordinary_allowance) {
            owned->top.owned.status=CETTA_CONTINUATION_CAPACITY;
            eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);return;
        } else owned->ordinary_allowance+=allowance;
    }
    while (true) {
        /* Refusal retains this exact row and cached materialization. No
         * producer or continuation selection precedes its accepted append. */
        while (owned->ordinary_cursor<control->published)
            if (!prime_ordinary_return_project(owned)) {
                owned->top.owned.status=owned->ordinary_refusal?
                    CETTA_CONTINUATION_UNSUPPORTED:CETTA_CONTINUATION_CAPACITY;
                eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);return;
            }
        if (control->invalidated) {
            owned->top.owned.status=CETTA_CONTINUATION_INVALIDATED;
            eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_INVALIDATED);return;
        }
        if (control->failed) {
            owned->top.owned.status=CETTA_CONTINUATION_DEFERRED;
            eval_mark_incomplete(control->completion);return;
        }
        if (prime_native_control_closed(control)) {
            owned->top.committed=true;owned->top.owned.status=CETTA_CONTINUATION_READY;return;
        }
        uint64_t before=control->steps;size_t published=control->published;
        prime_native_control_run(control,owned->ordinary_allowance,prime_ordinary_stop_after_row,NULL);
        uint64_t spent=control->steps-before;
        assert(spent<=owned->ordinary_allowance);
        owned->ordinary_allowance-=spent;
        if (control->published==published && control->steps==before) {
            owned->top.owned.status=control->invalidated?CETTA_CONTINUATION_INVALIDATED:
                control->driver.capacity_blocked?CETTA_CONTINUATION_CAPACITY:CETTA_CONTINUATION_DEFERRED;
            eval_mark_incomplete(control->invalidated?CETTA_EVAL_INCOMPLETE_INVALIDATED:
                control->completion==CETTA_EVAL_COMPLETE?CETTA_EVAL_INCOMPLETE_CAPACITY:control->completion);
            return;
        }
    }
}

static void prime_ordinary_return_free(CettaPrimeObservationReturn *owned) {
    assert(owned && owned->ordinary && !owned->ordinary_running);
    PrimeNativeTopEpisode *episode=owned->top.episode;
    PrimeNativeRegionScope region={0};
    bool entered=prime_native_region_scope_enter(episode,&region);
    assert(entered);(void)entered;
    EvalExecutionContext *previous=g_eval_execution;
    g_eval_execution=episode->execution;
    prime_native_control_destroy(owned->ordinary_control);owned->ordinary_control=NULL;
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
    if (eval_current_num_threads()<2u || current_eval_fuel_limit()>=0 ||
        outcome->depth_fuel || observer || observer_context ||
        output->hashcons || output->older_identity ||
        !prime_native_owned_top_context_admitted()) return false;
    /* The admitted source already owns this entry root. Other caller roots
     * retain literal synchronous evaluation until a root-owner transport is
     * supplied; no borrowed root is silently retained across return. */
    if (expression!=source->request_expression || !expression) return false;
    CettaPrimeObservationReturn *owned=calloc(1u,sizeof(*owned));
    PrimeNativeTopEpisode *episode=calloc(1u,sizeof(*episode));
    if (!owned || !episode) {free(owned);free(episode);return false;}
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
