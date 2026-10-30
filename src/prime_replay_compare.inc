/* Read the admitted quiescent provider, without changing its caches, choosing
 * work, projecting an answer or evaluating a code-shaped field. */
#define RC_S(n,x,y) do { if(!read->scalar(read->context,n,(uint64_t)(x),(uint64_t)(y)))return false; } while(0)
#define RC_F(n,x,y) do { if(!read->physical(read->context,n,(uint64_t)(x),(uint64_t)(y)))return false; } while(0)
#define RC_P(n,x,y) do { if(!read->address(read->context,n,x,y))return false; } while(0)
#define RC_I(n,r,x,y) do { if(!read->identity(read->context,n,r,(uint64_t)(x),(uint64_t)(y)))return false; } while(0)
#define RC_A(n,x,y) do { if(!read->atom(read->context,n,x,y))return false; } while(0)
#define RC_FIELDS(prefix,a,b,field) RC_S(prefix "." #field,(a)->field,(b)->field)

static bool prime_replay_compare_text(const char *a,const char *b,
    const CettaCheckpointCompare *read) {
    RC_S("text.present",a!=NULL,b!=NULL);
    return (!a&&!b)||(a&&b&&read->bytes(read->context,"text",a,SIZE_MAX,b,SIZE_MAX));
}
static bool prime_replay_compare_atoms(Atom *const *a,size_t an,Atom *const *b,
    size_t bn,const CettaCheckpointCompare *read) {
    if(an!=bn||(an&&(!a||!b)))return false;
    RC_S("atoms.length",an,bn);RC_P("atoms.storage",a,b);
    for(size_t i=0u;i<an;i++){RC_S("atoms.index",i,i);RC_A("atoms.value",a[i],b[i]);}
    return true;
}
static bool prime_replay_compare_indices(const size_t *a,size_t an,size_t ac,
    const size_t *b,size_t bn,size_t bc,const CettaCheckpointCompare *read) {
    if(!cetta_checkpoint_extent(a,an,ac,b,bn,bc))return false;
    RC_S("indices.length",an,bn);RC_F("indices.capacity",ac,bc);RC_F("indices.storage",(uintptr_t)a,(uintptr_t)b);
    for(size_t i=0u;i<an;i++){RC_S("indices.index",i,i);RC_S("indices.value",a[i],b[i]);}
    return true;
}
static bool prime_replay_compare_read(SpaceReadToken a,SpaceReadToken b,
    const CettaCheckpointCompare *read) {
    if(a.space!=b.space)return false;
    RC_P("source.space",a.space,b.space);
#define RR(f) RC_S("source."#f,a.f,b.f)
    RR(instance_id);RR(revision);RR(prefix_epoch);RR(base_prefix_epoch);RR(base_dependency_epoch);
#undef RR
    return true;
}
static bool prime_replay_compare_history_spec(const PrimeNativeHistorySpec *a,
    const PrimeNativeHistorySpec *b,const CettaCheckpointCompare *read) {
#define RH(f) RC_FIELDS("history.spec",a,b,f)
    RH(enabled);RH(selections);RH(nodes);RH(bytes);RH(owners);
#undef RH
    return true;
}
static bool prime_replay_compare_policy(const CettaSearchPolicySpec *a,
    const CettaSearchPolicySpec *b,const CettaCheckpointCompare *read) {
#define RP(f) RC_FIELDS("policy",a,b,f)
    RP(present);RP(lane);RP(order);RP(native_control);RP(implicit_first_value);
    RP(priority);RP(age_share);RP(work_budget);RP(workers);RP(worker_quantum);
    RP(resource_profile);RP(resource_worlds);RP(retain);RP(receipts);RP(costs);RP(cost_compact);RP(cost_equations);RP(cost_source_only);
    RP(source_share);RP(prepare_meter);
    RP(breakpoint_specified);RP(breakpoint_budget);
    RP(additional);RP(all);RP(descending);RP(by_weight);RP(weight_law);RP(selection);
#undef RP
#define RA(f) RC_A("policy."#f,a->f,b->f)
    RA(policy_atom);RA(key_pattern);RA(key_expression);RA(goal_pattern);RA(goal_expression);
    RA(bound_pattern);RA(bound_expression);
    RA(breakpoint_pattern);
#undef RA
    return prime_replay_compare_history_spec(&a->history,&b->history,read);
}
static bool prime_replay_compare_outcomes(const OutcomeSet *a,const OutcomeSet *b,
    const CettaCheckpointCompare *read) {
    if(!a||!b||!cetta_checkpoint_extent(a->items,a->len,a->cap,b->items,b->len,b->cap))return false;
    RC_P("outcomes",a,b);if(!read->arena(read->context,"outcomes.owner",a->payload_owner,b->payload_owner))return false;
    RC_S("outcomes.length",a->len,b->len);RC_F("outcomes.capacity",a->cap,b->cap);
    RC_F("outcomes.item_storage",(uintptr_t)a->items,(uintptr_t)b->items);
    for(CettaCount i=0u;i<a->len;i++) {
        const Outcome *x=&a->items[i],*y=&b->items[i];
        RC_S("outcomes.index",i,i);RC_S("outcome.kind",x->kind,y->kind);
        RC_S("outcome.result_form",x->result_form,y->result_form);
        if ((x->result_form != CETTA_CALL_RESULT_EXPRESSION &&
             x->result_form != CETTA_CALL_RESULT_COMPLETED_VALUE) ||
            (y->result_form != CETTA_CALL_RESULT_EXPRESSION &&
             y->result_form != CETTA_CALL_RESULT_COMPLETED_VALUE)) return false;
        RC_A("outcome.atom",x->atom,y->atom);RC_A("outcome.materialized",x->materialized_atom,y->materialized_atom);
        RC_A("outcome.delayed",x->delayed,y->delayed);
        /* Answer-bank references have a separate mutable provider. The pure
         * caller profile excludes that provider, rather than treating its
         * printed projection as a complete owned answer. */
        if(x->kind!=CETTA_OUTCOME_INLINE||y->kind!=CETTA_OUTCOME_INLINE||
            x->answer_ref.bank||y->answer_ref.bank)return false;
        RC_P("outcome.answer_bank",x->answer_ref.bank,y->answer_ref.bank);
        if(!bindings_checkpoint_compare(&x->env,&y->env,read)||
            !variant_instance_checkpoint_compare(&x->variant,&y->variant,read))return false;
    }
    return true;
}
static bool prime_replay_compare_task(const PrimeEvalStackTask *a,const PrimeEvalStackTask *b,
    const CettaCheckpointCompare *read) {
    if(a->context || b->context) return false;
    if(a->space!=b->space)return false;
#define RT(f) RC_FIELDS("task",a,b,f)
    RT(kind);RT(pure_authority);RT(fuel);RT(preserve_bindings);RT(strict_ready_argument);
    RT(dynamic_env_initialized);RT(dynamic_env_is_seed);RT(seed_env_initialized);RT(control_scoring);
#undef RT
    RC_P("task.space",a->space,b->space);if(!read->arena(read->context,"task.arena",a->arena,b->arena))return false;
    RC_P("task.target",a->target,b->target);
    RC_A("task.atom",a->atom,b->atom);RC_A("task.etype",a->etype,b->etype);
    RC_A("task.key",a->control_key,b->control_key);
    RC_I("task.evaluator",CETTA_CHECKPOINT_NEED_EVALUATOR,a->evaluator_id,b->evaluator_id);
    if(a->dynamic_env_initialized!=b->dynamic_env_initialized||a->seed_env_initialized!=b->seed_env_initialized)return false;
    return (!a->dynamic_env_initialized||bindings_checkpoint_compare(&a->dynamic_env,&b->dynamic_env,read))&&
        (!a->seed_env_initialized||bindings_checkpoint_compare(&a->seed_env,&b->seed_env,read));
}
static bool prime_replay_compare_cell(const PrimeNeedCellView *a,const PrimeNeedCellView *b,
    const CettaCheckpointCompare *read) {
    RC_S("cell.state",a->cache_state,b->cache_state);RC_A("cell.origin",a->origin,b->origin);
    RC_S("cell.value_role",a->value_role,b->value_role);
    RC_I("cell.value_producer",CETTA_CHECKPOINT_NEED_THUNK,a->value_producer_thunk,b->value_producer_thunk);
    RC_I("cell.value_producer_update",CETTA_CHECKPOINT_NEED_SERIAL,a->value_producer_update,b->value_producer_update);
    RC_I("cell.update",CETTA_CHECKPOINT_NEED_SERIAL,a->update_id,b->update_id);
    RC_A("cell.cached",a->cached,b->cached);
    RC_I("cell.authority",CETTA_CHECKPOINT_NEED_AUTHORITY,a->authority_id,b->authority_id);
    RC_I("cell.evaluator",CETTA_CHECKPOINT_NEED_EVALUATOR,a->evaluator_id,b->evaluator_id);
    RC_I("cell.storage",CETTA_CHECKPOINT_NEED_STORAGE,a->storage_key,b->storage_key);
    RC_I("cell.import",CETTA_CHECKPOINT_NEED_IMPORT,a->import_key,b->import_key);
    RC_I("cell.occurrence",CETTA_CHECKPOINT_SOURCE_OCCURRENCE,a->source_occurrence_id,b->source_occurrence_id);
    RC_S("cell.argument",a->source_argument_index,b->source_argument_index);
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
    RC_S("cell.capture_known",a->capture_known,b->capture_known);
    RC_S("cell.capture_count",a->capture_var_count,b->capture_var_count);
    RC_P("cell.capture_storage",a->capture_var_ids,b->capture_var_ids);
    if(a->capture_var_count!=b->capture_var_count||(a->capture_var_count&&(!a->capture_var_ids||!b->capture_var_ids)))return false;
    for(size_t i=0u;i<a->capture_var_count;i++)
        if(!cetta_checkpoint_var(read,"cell.variable",a->capture_var_ids[i],b->capture_var_ids[i]))return false;
#endif
    return true;
}
static bool prime_replay_compare_semantics(const CettaMatchDecisionSemanticIdentity *a,
    const CettaMatchDecisionSemanticIdentity *b,const CettaCheckpointCompare *read) {
#define RS(f) RC_FIELDS("semantic",a,b,f)
    RS(language_id);RS(profile_id);RS(match_policy_id);RS(demand_policy_id);RS(presentation_identity);RS(compiler_identity);
#undef RS
    return true;
}
static bool prime_replay_compare_equation_metadata(const PrimeNeedEquationMetadata *a,
    size_t an, const PrimeNeedEquationMetadata *b, size_t bn,
    const CettaCheckpointCompare *read) {
    if(!cetta_checkpoint_extent(a->items,an,a->capacity,b->items,bn,b->capacity))return false;
    RC_P("equation.packet_metadata",a->items,b->items);
    RC_F("equation.packet_capacity",a->capacity,b->capacity);
    RC_S("equation.packet_observations",a->has_observations,b->has_observations);
    for(size_t i=0u;i<an;i++) {
        RC_S("equation.packet_index",i,i);
        RC_S("equation.packet_plan",a->items[i].plan_index,b->items[i].plan_index);
        RC_S("equation.packet_kind",a->items[i].kind,b->items[i].kind);
        RC_S("equation.packet_world",a->items[i].world_order,b->items[i].world_order);
    }
    return true;
}
static bool prime_replay_compare_pattern_request(const PrimeNeedPatternRequest *a,
    const PrimeNeedPatternRequest *b,const CettaCheckpointCompare *read) {
    if(!prime_need_pattern_request_valid(a)||!prime_need_pattern_request_valid(b)||
        a->length!=b->length)return false;
    RC_S("observation.demand",a->demand,b->demand);
    RC_S("observation.length",a->length,b->length);
    RC_F("observation.capacity",a->capacity,b->capacity);
    RC_P("observation.path",a->path,b->path);
    for(size_t i=0u;i<a->length;i++) {
        RC_S("observation.index",i,i);
        RC_S("observation.coordinate",a->path[i],b->path[i]);
    }
    return true;
}
static bool prime_replay_compare_pattern_readiness(const PrimeNeedPatternReadiness *a,
    const PrimeNeedPatternReadiness *b,const CettaCheckpointCompare *read) {
    RC_P("observation.readiness",a,b);if(!a||!b)return !a&&!b;
    if(!cetta_checkpoint_extent(a->paths,a->length,a->capacity,b->paths,b->length,b->capacity))return false;
    RC_S("observation.ready_length",a->length,b->length);
    RC_F("observation.ready_capacity",a->capacity,b->capacity);
    RC_P("observation.ready_paths",a->paths,b->paths);
    for(size_t i=0u;i<a->length;i++) {
        RC_S("observation.ready_index",i,i);
        if(a->paths[i].demand!=PRIME_NEED_PATTERN_FULL_VALUE ||
           b->paths[i].demand!=PRIME_NEED_PATTERN_FULL_VALUE ||
           !prime_replay_compare_pattern_request(&a->paths[i],&b->paths[i],read))return false;
    }
    return true;
}
static bool prime_replay_compare_equation(const PrimeNeedEquationContinuation *a,
    const PrimeNeedEquationContinuation *b,const CettaCheckpointCompare *read) {
    RC_P("equation",a,b);if(!a||!b)return !a&&!b;
    if(a->cohort_owner||b->cohort_owner)return false;
    if(a->space!=b->space||a->plan_count!=b->plan_count||(a->plan_count&&(!a->plans||!b->plans)))return false;
#define RE(f) RC_FIELDS("equation",a,b,f)
    RE(state);RE(fuel);RE(preserve_bindings);RE(frontier);RE(plan_count);RE(current_covered_by_match);
    RE(current_observation_fault);
    RE(current_work_slot);
    RE(current_world_order);RE(world_order_extent);
    RE(inspect_incrementally);RE(publication_returns_to_search);
    RE(force_argument);RE(publication_index);RE(publication_raw_with_ref);RE(publication_pair_probes);
    RE(publication_descendants);RE(publication_fallbacks);
#undef RE
    if(!prime_replay_compare_pattern_request(&a->inspection,&b->inspection,read)||
        !prime_replay_compare_pattern_readiness(a->observed,b->observed,read))return false;
    RC_P("equation.space",a->space,b->space);if(!read->arena(read->context,"equation.arena",a->arena,b->arena))return false;
    RC_A("equation.call",a->original_call,b->original_call);
    RC_I("equation.occurrence",CETTA_CHECKPOINT_SOURCE_OCCURRENCE,a->source_occurrence_id,b->source_occurrence_id);
    RC_P("equation.plans",a->plans,b->plans);RC_P("equation.decision",a->match_decision,b->match_decision);
    if(!prime_replay_compare_semantics(&a->match_decision_semantics,&b->match_decision_semantics,read))return false;
    size_t an=a->original_call&&a->original_call->kind==ATOM_EXPR&&a->original_call->expr.len?a->original_call->expr.len-1u:0u;
    size_t bn=b->original_call&&b->original_call->kind==ATOM_EXPR&&b->original_call->expr.len?b->original_call->expr.len-1u:0u;
    if(an!=bn)return false;
    for(size_t i=0u;i<a->plan_count;i++) {
        const PrimeNeedEquationPlan *x=&a->plans[i],*y=&b->plans[i];
        RC_S("equation.plan_index",i,i);RC_A("equation.plan_row",x->equation,y->equation);
        RC_I("equation.rule",CETTA_CHECKPOINT_RULE_OCCURRENCE,x->rule_occurrence_id,y->rule_occurrence_id);
        RC_S("equation.source_ref",x->match_source_ref,y->match_source_ref);
        RC_P("equation.demand_storage",x->demand,y->demand);
        if(an&&(!x->demand||!y->demand))return false;
        for(size_t j=0u;j<an;j++)RC_S("equation.demand",x->demand[j],y->demand[j]);
        if(!prime_replay_compare_outcomes(&x->results,&y->results,read))return false;
        if(!prime_replay_compare_equation_metadata(&x->result_metadata,x->results.len,
            &y->result_metadata,y->results.len,read))return false;
    }
    const OutcomeSet *as[]={&a->work,&a->current,&a->raw_results,&a->residuals,&a->publication,&a->forced_worlds};
    const OutcomeSet *bs[]={&b->work,&b->current,&b->raw_results,&b->residuals,&b->publication,&b->forced_worlds};
    for(size_t i=0u;i<6u;i++){RC_S("equation.outcome_role",i,i);if(!prime_replay_compare_outcomes(as[i],bs[i],read))return false;}
    if(!prime_replay_compare_equation_metadata(&a->raw_metadata,a->raw_results.len,
        &b->raw_metadata,b->raw_results.len,read)||
        !prime_replay_compare_equation_metadata(&a->publication_metadata,a->publication.len,
        &b->publication_metadata,b->publication.len,read)||
        !prime_replay_compare_equation_metadata(&a->residual_metadata,a->residuals.len,
        &b->residual_metadata,b->residuals.len,read)||
        !prime_replay_compare_equation_metadata(&a->forced_world_metadata,a->forced_worlds.len,
        &b->forced_world_metadata,b->forced_worlds.len,read)||
        !prime_replay_compare_indices(a->current_plan_indices,a->current_plan_count,a->current_plan_count,
        b->current_plan_indices,b->current_plan_count,b->current_plan_count,read)||
        !prime_replay_compare_indices(a->pending_plan_indices,a->pending_plan_count,a->pending_plan_count,
        b->pending_plan_indices,b->pending_plan_count,b->pending_plan_count,read))return false;
    if(!cetta_checkpoint_extent(a->work_meta,a->work.len,a->work_meta_cap,b->work_meta,b->work.len,b->work_meta_cap))return false;
    RC_F("equation.work_meta_capacity",a->work_meta_cap,b->work_meta_cap);
    for(size_t i=0u;i<a->work.len;i++) {
        const PrimeNeedEquationWorkMeta *x=&a->work_meta[i],*y=&b->work_meta[i];
        RC_S("equation.work_meta_index",i,i);RC_S("equation.covered",x->covered_by_match,y->covered_by_match);
        if(!prime_replay_compare_pattern_readiness(x->observed,y->observed,read))return false;
        RC_S("equation.observation_fault",x->observation_fault,y->observation_fault);
        RC_S("equation.work_world",x->world_order,y->world_order);
        if(!prime_replay_compare_indices(x->plan_indices,x->plan_count,x->plan_count,y->plan_indices,y->plan_count,y->plan_count,read))return false;
    }
    return prime_replay_compare_atoms(a->root_atoms,a->root_atom_count,b->root_atoms,b->root_atom_count,read)&&
        prime_replay_compare_atoms(a->result_contracts,a->result_contract_count,b->result_contracts,b->result_contract_count,read);
}

static bool prime_replay_compare_frame(const PrimeEvalStackFrame *a,const PrimeEvalStackFrame *b,
    const CettaCheckpointCompare *read) {
    RC_P("frame",a,b);if(!a||!b)return !a&&!b;
    if(a->context || b->context) return false;
#define RF(f) RC_FIELDS("frame",a,b,f)
    RF(control_pending);RF(control_sequence);RF(control_queued);RF(control_closed);RF(control_notified);
    RF(control_changed);RF(control_retired);RF(control_advisory);RF(control_force_stream);RF(control_force_detached);
    RF(control_nested_budget);RF(control_nested_retained);RF(control_nested_parked);RF(control_scoring);
    RF(control_generation);RF(dialect_source_success);RF(dialect_source_is_call);RF(dialect_error_index);
    RF(kind);RF(pure_authority);RF(state);RF(fuel);RF(preserve_bindings);RF(env_initialized);RF(child_initialized);
    RF(force_pending_initialized);RF(force_pending_observed);RF(force_published);RF(force_retry_pending);
    RF(index);RF(branch_index);RF(variable_branches_active);RF(normalization_child_index);RF(tuple_bind);
    RF(normalization_value_ready);RF(normalization_return_value_ready);RF(normalization_result_form);
    RF(strict_argument);RF(strict_mode);RF(strict_error_is_data);RF(weight_index);RF(weight_ownership_parked);
    RF(source_argument_universal_demand);RF(stream_forwarded);RF(control_shell_refs);
    RF(shared_need_wait);RF(force_empty_lexical_env);RF(ordinary_ordered);
#undef RF
    _Static_assert(offsetof(PrimeEvalStackFrame,continuation)==0u,
        "frame continuation identifies its enclosing frame");
    RC_P("frame.next",(const PrimeEvalStackFrame *)a->continuation.next,
        (const PrimeEvalStackFrame *)b->continuation.next);
    RC_P("frame.control_next",a->control_next,b->control_next);
    RC_P("frame.previous",a->control_previous,b->control_previous);RC_P("frame.parent",a->control_parent,b->control_parent);
    RC_P("frame.stream_consumer",a->stream_consumer,b->stream_consumer);
    RC_P("frame.force_producer",a->force_producer,b->force_producer);
    RC_P("frame.force_waiters",a->force_waiters,b->force_waiters);
    RC_P("frame.force_waiter_next",a->force_waiter_next,b->force_waiter_next);
    RC_P("frame.open_force_next",a->open_force_next,b->open_force_next);
    RC_I("frame.nested_handle",CETTA_CHECKPOINT_NATIVE_HANDLE,a->control_nested_handle,b->control_nested_handle);
    RC_I("frame.weight_dependency",CETTA_CHECKPOINT_SOURCE_OCCURRENCE,a->weight_dependency,b->weight_dependency);
    RC_I("frame.thunk",CETTA_CHECKPOINT_NEED_THUNK,a->thunk_id,b->thunk_id);
    RC_I("frame.value_producer",CETTA_CHECKPOINT_NEED_THUNK,a->value_producer_thunk,b->value_producer_thunk);
    RC_I("frame.previous_evaluator",CETTA_CHECKPOINT_NEED_EVALUATOR,a->previous_evaluator_id,b->previous_evaluator_id);
    RC_I("frame.evaluator",CETTA_CHECKPOINT_NEED_EVALUATOR,a->evaluator_id,b->evaluator_id);
    if(a->control_retired!=b->control_retired)return false;
    if(a->control_retired) {
        /* Read only the retained shell. Released Atom and target bits can
         * point into already retired storage and are never dereferenced. */
        return a->control_closed&&b->control_closed&&a->control_notified&&b->control_notified&&
            !a->control_queued&&!b->control_queued&&!a->control_pending&&!b->control_pending&&
            !a->env_initialized&&!b->env_initialized&&!a->child_initialized&&!b->child_initialized&&
            !a->force_pending_initialized&&!b->force_pending_initialized&&
            !a->equation_search&&!b->equation_search&&!a->match_rows&&!b->match_rows&&
            !a->control_nested&&!b->control_nested&&!a->dialect_source&&!b->dialect_source&&
            !a->normalization_children&&!b->normalization_children;
    }
    if(a->space!=b->space||a->control_nested||b->control_nested||a->match_rows||b->match_rows||
        a->dialect_source||b->dialect_source||a->control_return_advice_length||b->control_return_advice_length)
        return false;
    if(a->control_child_bound_length>a->control_child_bound_capacity||
        b->control_child_bound_length>b->control_child_bound_capacity||
        a->normalization_child_index>a->normalization_child_count||
        b->normalization_child_index>b->normalization_child_count)return false;
    RC_P("frame.space",a->space,b->space);if(!read->arena(read->context,"frame.arena",a->arena,b->arena))return false;
    RC_P("frame.target",a->target,b->target);if(!read->arena(read->context,"frame.heap_owner",a->heap_owner,b->heap_owner))return false;
    RC_P("frame.advice",a->control_advice,b->control_advice);
    RC_F("frame.dialect_advice_capacity",a->control_return_advice_capacity,b->control_return_advice_capacity);
    RC_F("frame.return_pending",a->control_return_pending,b->control_return_pending);
    RC_F("frame.pending_advice_begin",a->control_return_pending_begin,b->control_return_pending_begin);
    RC_F("frame.pending_advice_end",a->control_return_pending_end,b->control_return_pending_end);
    RC_A("frame.pending_advice_key",a->control_return_pending_advice.key,b->control_return_pending_advice.key);
    RC_A("frame.pending_advice_bound",a->control_return_pending_advice.bound,b->control_return_pending_advice.bound);
    RC_P("frame.pending_advice",a->control_return_pending_advice.advice,b->control_return_pending_advice.advice);
#define FA(f) RC_A("frame."#f,a->f,b->f)
    FA(control_key);FA(control_bound);FA(atom);FA(etype);FA(strict_source);FA(then_branch);FA(else_branch);
    FA(weight_subject);FA(weight_algebra);FA(weight_trace);FA(ref);
#undef FA
    if(a->env_initialized!=b->env_initialized||a->child_initialized!=b->child_initialized||
        a->force_pending_initialized!=b->force_pending_initialized)return false;
    if((a->env_initialized&&!bindings_checkpoint_compare(&a->env,&b->env,read))||
        (a->child_initialized&&!prime_replay_compare_outcomes(&a->child,&b->child,read))||
        (a->force_pending_initialized&&
            !prime_replay_compare_outcomes(&a->force_pending,&b->force_pending,read))||
        !prime_replay_compare_equation(a->equation_search,b->equation_search,read)||
        !prime_replay_compare_policy(&a->control_nested_policy,&b->control_nested_policy,read)||
        !prime_replay_compare_atoms(a->normalization_children,a->normalization_child_count,b->normalization_children,b->normalization_child_count,read)||
        !prime_replay_compare_atoms(a->control_child_bounds,a->control_child_bound_length,b->control_child_bounds,b->control_child_bound_length,read)||
        !prime_replay_compare_indices(a->control_selection,a->control_selection_length,a->control_selection_length,
        b->control_selection,b->control_selection_length,b->control_selection_length,read)||
        !prime_replay_compare_read(a->force_source_read,b->force_source_read,read)||
        !prime_replay_compare_cell(&a->cell,&b->cell,read)||
        !prime_need_snapshot_compare(&a->before,&b->before,read)||
        !prime_need_snapshot_compare(&a->evaluating,&b->evaluating,read))return false;
    RC_F("frame.child_bound_capacity",a->control_child_bound_capacity,b->control_child_bound_capacity);
    return prime_need_receipt_compare(&a->before_receipt,&b->before_receipt,read);
}

static bool prime_replay_compare_program(SpaceProgramToken a,SpaceProgramToken b,
    const CettaCheckpointCompare *read) {
    if(a.space!=b.space)return false;
    RC_P("program.space",a.space,b.space);
#define RP(f) RC_S("program."#f,a.f,b.f)
    RP(instance_id);RP(equation_revision);RP(declaration_revision);RP(base_dependency_epoch);
#undef RP
    return true;
}
static bool prime_replay_compare_registry(const Registry *a,const Registry *b,
    const CettaCheckpointCompare *read) {
    if(a->states||b->states||name_key_count(a->name_keys)||name_key_count(b->name_keys)||
        !cetta_checkpoint_extent(a->entries,a->len,a->cap,b->entries,b->len,b->cap)||
        a->index_cap!=b->index_cap||(a->index_cap&&(!a->index_slots||!b->index_slots)))return false;
    RC_S("registry.length",a->len,b->len);RC_F("registry.capacity",a->cap,b->cap);
    RC_F("registry.entries",(uintptr_t)a->entries,(uintptr_t)b->entries);
    RC_S("registry.index_capacity",a->index_cap,b->index_cap);
    RC_P("registry.states",a->states,b->states);
    for(size_t i=0u;i<a->len;i++) {
        RC_S("registry.index",i,i);RC_S("registry.key",a->entries[i].key,b->entries[i].key);
        RC_S("registry.name",a->entries[i].name_id,b->entries[i].name_id);
        if(read->retained_atom) {
            if(!read->retained_atom(read->context,"registry.value",
                CETTA_CHECKPOINT_REGISTRY_ATTACHMENT,
                a->entries[i].value,b->entries[i].value))return false;
        } else { RC_A("registry.value",a->entries[i].value,b->entries[i].value); }
    }
    /* Symbols are rigid under the held symbol-instance contract, so the
     * initialized hash table is compared without rehashing or a lookup. */
    for(size_t i=0u;i<a->index_cap;i++) {
        if(a->index_slots[i]>a->len||b->index_slots[i]>b->len)return false;
        RC_S("registry.slot",a->index_slots[i],b->index_slots[i]);
    }
    return true;
}
static bool prime_replay_compare_registry_view(const RegistryReadView *a,
    const RegistryReadView *b,const CettaCheckpointCompare *read) {
    RC_S("registry_view.initialized",a->initialized,b->initialized);
    if(a->initialized!=b->initialized)return false;
    if(!a->initialized)return true;
    /* leave restores TLS without clearing these inactive bits; enter writes
     * both before use. Quiescent/TLS-disjoint admission is supplied before
     * this reader. Do not follow an inactive borrowed caller pointer. */
    RC_F("registry_view.inactive_source",(uintptr_t)a->active_source,(uintptr_t)b->active_source);
    RC_F("registry_view.inactive_parent",(uintptr_t)a->active_parent,(uintptr_t)b->active_parent);
    if(!read->arena(read->context,"registry_view.arena",a->arena,b->arena))return false;
    RC_S("registry_view.passive",a->passive,b->passive);
    RC_S("registry_view.stale",a->stale,b->stale);RC_S("registry_view.incomplete",a->incomplete,b->incomplete);
    RC_A("registry_view.stale_name",a->stale_name,b->stale_name);
    RC_A("registry_view.stale_expected",a->stale_expected,b->stale_expected);
    RC_A("registry_view.stale_actual",a->stale_actual,b->stale_actual);
    return prime_replay_compare_text(a->refusal,b->refusal,read)&&
        prime_replay_compare_registry(&a->initial,&b->initial,read)&&
        prime_replay_compare_registry(&a->consulted,&b->consulted,read);
}
static bool prime_replay_compare_read_view(const SpaceReadView *a,const SpaceReadView *b,
    const CettaCheckpointCompare *read) {
    if(a->base_count!=b->base_count ||
        (a->base_count&&(!a->bases||!b->bases||!a->base_watches||!b->base_watches))||
        !prime_replay_compare_read(a->root,b->root,read))return false;
    RC_P("read_view.root_stamp",a->root_watch.stamp,b->root_watch.stamp);
    RC_S("read_view.root_expected",a->root_watch.expected,b->root_watch.expected);
    RC_S("read_view.count",a->base_count,b->base_count);
    RC_F("read_view.storage",(uintptr_t)a->bases,(uintptr_t)b->bases);
    RC_F("read_view.watches",(uintptr_t)a->base_watches,(uintptr_t)b->base_watches);
    for(size_t i=0u;i<a->base_count;i++) {
        RC_S("read_view.index",i,i);
        if(!prime_replay_compare_read(a->bases[i],b->bases[i],read))return false;
        RC_P("read_view.base_stamp",a->base_watches[i].stamp,b->base_watches[i].stamp);
        RC_S("read_view.base_expected",a->base_watches[i].expected,b->base_watches[i].expected);
    }
    return true;
}
static bool prime_replay_compare_cost_snapshot(const CettaNativeCostSnapshot *a,
    const CettaNativeCostSnapshot *b,const CettaCheckpointCompare *read) {
    for(size_t i=0u;i<CETTA_COST_KIND_COUNT;i++) {
        RC_S("cost.kind",i,i);RC_S("cost.count",a->counts[i],b->counts[i]);
    }
#define RS(f) RC_FIELDS("cost",a,b,f)
    RS(work);RS(span);RS(parallel);RS(overflow);RS(trace_incomplete);
#undef RS
    return true;
}
static bool prime_replay_compare_cost(const CettaNativeCostLedger *a,
    const CettaNativeCostLedger *b,const CettaCheckpointCompare *read) {
    if(a->length>a->capacity||b->length>b->capacity||
        (a->length&&!a->events)||(b->length&&!b->events))return false;
    RC_I("cost.ledger",CETTA_CHECKPOINT_COST_LEDGER,a->identity,b->identity);
    RC_S("cost.imported",a->imported,b->imported);
    RC_S("cost.compact",a->compact,b->compact);
    RC_S("cost.native_enabled",a->native_enabled,b->native_enabled);
    RC_S("cost.used",a->used,b->used);
#define RE(f) RC_FIELDS("cost.equations",(&a->equations),(&b->equations),f)
    RE(enabled);RE(work);RE(span);RE(work_overflow);RE(span_overflow);RE(incomplete);RE(parallel);
#undef RE
    RC_F("cost.rows",a->length,b->length);RC_F("cost.capacity",a->capacity,b->capacity);
    RC_F("cost.storage",(uintptr_t)a->events,(uintptr_t)b->events);
    if(!prime_replay_compare_cost_snapshot(&a->total,&b->total,read))return false;
    size_t i=0u,j=0u;uint64_t ai=0u,bj=0u;
    while(i<a->length&&j<b->length) {
        const CettaNativeCostEvent *x=&a->events[i],*y=&b->events[j];
        if(!x->repetitions||!y->repetitions||!x->identity||!y->identity||
            x->identity==UINT64_MAX||y->identity==UINT64_MAX||
            x->repetitions>UINT64_MAX-x->identity||y->repetitions>UINT64_MAX-y->identity||
            (unsigned)x->kind>=CETTA_COST_KIND_COUNT||(unsigned)y->kind>=CETTA_COST_KIND_COUNT)
            return false;
        RC_I("cost.event",CETTA_CHECKPOINT_COST_EVENT,x->identity+ai,y->identity+bj);
        RC_I("cost.origin",CETTA_CHECKPOINT_COST_LEDGER,x->origin,y->origin);
        RC_I("cost.occurrence",CETTA_CHECKPOINT_ISSUED_OCCURRENCE,x->occurrence,y->occurrence);
        RC_S("cost.operation",x->kind,y->kind);RC_S("cost.units",x->units,y->units);
        if(++ai==x->repetitions){i++;ai=0u;}
        if(++bj==y->repetitions){j++;bj=0u;}
    }
    return i==a->length&&j==b->length;
}
static bool prime_replay_compare_arena(const Arena *a,const Arena *b,
    const CettaCheckpointCompare *read) {
    if(!read->arena(read->context,"arena",a,b))return false;
    if(!a||!b)return !a&&!b;
    /* The concrete visitor separately authenticates held block spans and
     * retained-owner edges. Addresses and allocator quantities alone cannot
     * certify ownership, and copied allocation layouts need not coincide. */
    if(a->hashcons!=b->hashcons)return false;
    RC_P("arena.hashcons",a->hashcons,b->hashcons);
    RC_I("arena.identity",CETTA_CHECKPOINT_ARENA,a->identity,b->identity);
    RC_I("arena.older",CETTA_CHECKPOINT_ARENA,a->older_identity,b->older_identity);
    RC_S("arena.runtime_kind",a->runtime_kind,b->runtime_kind);
    RC_S("arena.scalar_cache",a->scalar_cache,b->scalar_cache);
#define AP(f) RC_F("arena."#f,a->f,b->f)
    AP(persistent_bytes);AP(live_bytes);AP(external_bytes);AP(reserved_bytes);AP(spare_bytes);
    AP(block_count);AP(spare_block_count);AP(block_capacity);AP(block_growth);AP(reset_epoch);
#undef AP
    RC_F("arena.blocks",(uintptr_t)a->head,(uintptr_t)b->head);
    RC_F("arena.spares",(uintptr_t)a->spare,(uintptr_t)b->spare);
    RC_F("arena.scalar_storage",(uintptr_t)a->symbol_cache,(uintptr_t)b->symbol_cache);
    RC_F("arena.finalizers",(uintptr_t)a->finalizers,(uintptr_t)b->finalizers);
    RC_F("arena.retained_owners",(uintptr_t)a->retained_owners,(uintptr_t)b->retained_owners);
    RC_F("arena.frame_holds",(uintptr_t)a->frame_identities,(uintptr_t)b->frame_identities);
    return true;
}
static bool prime_replay_compare_mark(const ArenaMark *a,const ArenaMark *b,
    const CettaCheckpointCompare *read) {
    RC_F("mark.head",(uintptr_t)a->head,(uintptr_t)b->head);
    RC_F("mark.finalizers",(uintptr_t)a->finalizers,(uintptr_t)b->finalizers);
#define AM(f) RC_F("mark."#f,a->f,b->f)
    AM(used);AM(live_bytes);AM(external_bytes);AM(persistent_bytes);AM(reserved_bytes);
    AM(block_count);AM(frame_identity_len);
#undef AM
    return true;
}
static bool prime_replay_compare_episode(const PrimeNativeControl *ac,
    const PrimeNativeControl *bc,const CettaCheckpointCompare *read) {
    const PrimeNativeTopEpisode *a=ac->owned_top_episode,*b=bc->owned_top_episode;
    RC_P("episode",a,b);if(!a||!b)return !a&&!b;
    /* Check physical owner links before dereferencing the episode selector. */
    if(!arena_retains_owner(&ac->storage,a,prime_native_top_episode_release)||
        !arena_retains_owner(&bc->storage,b,prime_native_top_episode_release))return false;
    RC_S("episode.owner_edge",1u,1u);
    if(!a->references||!b->references||!a->ready||!b->ready||
        !a->request_storage_ready||!b->request_storage_ready||a->observer||b->observer||
        a->observer_context||b->observer_context||a->space!=b->space||
        a->registry!=b->registry||a->persistent!=b->persistent||a->library!=b->library)return false;
    RC_F("episode.references",a->references,b->references);
    RC_S("episode.ready",a->ready,b->ready);RC_S("episode.request_ready",a->request_storage_ready,b->request_storage_ready);
    RC_P("episode.space",a->space,b->space);RC_P("episode.registry",a->registry,b->registry);
    RC_P("episode.persistent",a->persistent,b->persistent);RC_P("episode.library",a->library,b->library);
    RC_A("episode.request",a->request_expression,b->request_expression);
    if(!prime_replay_compare_arena(&a->arena,&b->arena,read)||
        !prime_replay_compare_arena(&a->request_storage,&b->request_storage,read)||
        !prime_need_snapshot_compare(&a->context.value.previous,&b->context.value.previous,read)||
        !prime_need_receipt_compare(&a->context.value.previous_branch_state,&b->context.value.previous_branch_state,read)||
        !prime_need_receipt_compare(prime_need_saved_context_receipt(&a->context),prime_need_saved_context_receipt(&b->context),read)||
        !cetta_weight_ledger_compare(&a->context.value.previous_weights,&b->context.value.previous_weights,read))return false;
    const PrimeNeedRegionPool *x=&a->regions,*y=&b->regions;
    if(x->depth||y->depth||!cetta_checkpoint_extent(x->levels,x->level_count,x->level_capacity,
        y->levels,y->level_count,y->level_capacity))return false;
    RC_S("regions.depth",x->depth,y->depth);RC_S("regions.count",x->level_count,y->level_count);
    RC_F("regions.capacity",x->level_capacity,y->level_capacity);
    RC_F("regions.storage",(uintptr_t)x->levels,(uintptr_t)y->levels);
    if(!read->arena(read->context,"regions.stable_owner",x->stable_owner,y->stable_owner))return false;
    for(size_t i=0u;i<x->level_count;i++) {
        const PrimeNeedRegionLevel *p=x->levels[i],*q=y->levels[i];
        RC_S("regions.index",i,i);RC_P("regions.level",p,q);
        if((!p)!=(!q))return false;
        if(p&&(!prime_replay_compare_arena(&p->arena,&q->arena,read)||
            !prime_replay_compare_mark(&p->origin,&q->origin,read)))return false;
    }
    return true;
}

static bool prime_replay_compare_work(const PrimeNativeWork *a,const PrimeNativeWork *b,
    const CettaCheckpointCompare *read) {
    RC_P("work",a,b);if(!a||!b)return !a&&!b;
    if(a->licence||b->licence||a->dialect||b->dialect||a->parallel||b->parallel)return false;
    RC_S("work.resource_step",a->resource_step,b->resource_step);
    RC_P("work.resource_provider",a->resource_provider,b->resource_provider);
    if(a->resource_step!=b->resource_step ||
        (a->resource_step ? (!a->owner || !b->owner ||
            a->resource_provider!=a->owner->space_agenda || b->resource_provider!=b->owner->space_agenda)
            : (a->resource_provider || b->resource_provider)))return false;
    RC_P("work.owner",a->owner,b->owner);RC_P("work.parent",a->parent,b->parent);
    RC_P("work.frame",a->frame,b->frame);RC_P("work.advice",a->advice,b->advice);
    RC_S("work.task_initialized",a->task_initialized,b->task_initialized);
    RC_S("work.parallel_refused",a->parallel_refused,b->parallel_refused);
    RC_S("work.prepared_publication",a->prepared_publication,b->prepared_publication);
    RC_S("work.scoring",a->scoring,b->scoring);
    RC_A("work.key",a->key,b->key);RC_A("work.bound",a->bound,b->bound);
    if(a->task_initialized!=b->task_initialized)return false;
    return prime_replay_compare_text(a->key_text,b->key_text,read)&&
        (!a->task_initialized||prime_replay_compare_task(&a->task,&b->task,read));
}
static bool prime_replay_compare_continuation(const CettaOwnedContinuation *a,
    const CettaOwnedContinuation *b,const CettaCheckpointCompare *read) {
    if(a->provider!=&prime_native_work_provider||b->provider!=a->provider||!a->payload||!b->payload)return false;
    RC_P("continuation.provider",a->provider,b->provider);
    RC_I("continuation.occurrence",CETTA_CHECKPOINT_ISSUED_OCCURRENCE,a->occurrence_id,b->occurrence_id);
    RC_I("continuation.parent",CETTA_CHECKPOINT_ISSUED_OCCURRENCE,a->parent_occurrence_id,b->parent_occurrence_id);
    RC_I("continuation.store",CETTA_CHECKPOINT_ISSUED_STORE,a->resume_store,b->resume_store);
    RC_I("continuation.lease",CETTA_CHECKPOINT_ISSUED_LEASE,a->resume_lease,b->resume_lease);
    return prime_replay_compare_work(a->payload,b->payload,read);
}
static bool prime_replay_compare_force_retry(const PrimeNativeForceRetry *a,
    const PrimeNativeForceRetry *b,const CettaCheckpointCompare *read) {
    bool present_a=a->continuation.payload!=NULL, present_b=b->continuation.payload!=NULL;
    RC_S("control.force_retry_admitted",a->admitted,b->admitted);
    RC_S("control.force_retry_present",present_a,present_b);
    if(a->admitted!=b->admitted || present_a!=present_b) return false;
    return !present_a || prime_replay_compare_continuation(
        &a->continuation,&b->continuation,read);
}
static bool prime_replay_compare_hub(const CettaContinuationHub *a,const CettaContinuationHub *b,
    const CettaCheckpointCompare *read) {
    RC_S("hub.initialized",a->initialized,b->initialized);
    if(a->initialized!=b->initialized)return false;
    if(!a->initialized)return true;
    RC_S("hub.duty",a->selection_duty,b->selection_duty);
    RC_S("hub.fair_descent",a->fair_descent,b->fair_descent);
    RC_S("hub.last_lane",a->last_lane,b->last_lane);
    RC_S("hub.oldest_from_old_region",a->oldest_from_old_region,b->oldest_from_old_region);
    RC_S("hub.observed",a->observed_occurrences,b->observed_occurrences);
    RC_S("hub.readout",a->plan.readout,b->plan.readout);
    RC_S("hub.activation",a->plan.activation,b->plan.activation);
    RC_S("hub.prefix_limit",a->plan.prefix_limit,b->plan.prefix_limit);
    const CettaSelectionAutomaton *x=&a->schedule,*y=&b->schedule;
    if(!cetta_selection_automaton_valid(x)||!cetta_selection_automaton_valid(y)||x->state_count!=y->state_count)return false;
    RC_S("schedule.count",x->state_count,y->state_count);RC_S("schedule.state",x->state,y->state);
    for(size_t i=0u;i<x->state_count;i++) {
        RC_S("schedule.index",i,i);RC_S("schedule.lane",x->lane[i],y->lane[i]);
        RC_S("schedule.next",x->next[i],y->next[i]);
    }
    const CettaContinuationStore *s=&a->store,*t=&b->store;
    if(s->begin>s->end||t->begin>t->end||s->end>s->capacity||t->end>t->capacity||
        s->end-s->begin!=t->end-t->begin||(s->end&&!s->items)||(t->end&&!t->items)||
        !cetta_checkpoint_extent(s->leases,s->lease_length,s->lease_capacity,
            t->leases,t->lease_length,t->lease_capacity))return false;
    RC_S("store.length",s->end-s->begin,t->end-t->begin);
    RC_I("store.identity",CETTA_CHECKPOINT_ISSUED_STORE,s->identity,t->identity);
    /* These are local monotone counters, including exhaustion; their values
     * determine the next admitted store operation rather than a public ID. */
    RC_S("store.next_occurrence",s->next_occurrence_id,t->next_occurrence_id);
    RC_S("store.next_lease",s->next_lease,t->next_lease);
    RC_F("store.begin",s->begin,t->begin);RC_F("store.end",s->end,t->end);
    RC_F("store.mid",s->mid,t->mid);
    RC_F("store.capacity",s->capacity,t->capacity);
    RC_F("store.storage",(uintptr_t)s->items,(uintptr_t)t->items);
    for(size_t i=0u;i<s->end-s->begin;i++) {
        RC_S("store.index",i,i);
        if(!prime_replay_compare_continuation(&s->items[s->begin+i],&t->items[t->begin+i],read))return false;
    }
    RC_S("store.lease_length",s->lease_length,t->lease_length);
    RC_F("store.lease_capacity",s->lease_capacity,t->lease_capacity);
    for(size_t i=0u;i<s->lease_length;i++) {
        const CettaContinuationLease *p=&s->leases[i],*q=&t->leases[i];
        if(p->provider!=&prime_native_work_provider||q->provider!=p->provider)return false;
        RC_S("store.lease_index",i,i);RC_P("store.lease_provider",p->provider,q->provider);
        RC_I("store.lease_occurrence",CETTA_CHECKPOINT_ISSUED_OCCURRENCE,p->occurrence,q->occurrence);
        RC_I("store.lease",CETTA_CHECKPOINT_ISSUED_LEASE,p->lease,q->lease);
        RC_P("store.lease_payload",p->payload,q->payload);
    }
    return true;
}
static bool prime_replay_compare_prepared_cache(const PreparedPureProgramCache *a,
    const PreparedPureProgramCache *b,const CettaCheckpointCompare *read) {
    if(a->entries||b->entries||a->execution_scratch_ready||b->execution_scratch_ready||
        a->root_space!=b->root_space)return false;
    RC_P("prepared.entries",a->entries,b->entries);RC_P("prepared.space",a->root_space,b->root_space);
    RC_S("prepared.generation",a->entry_generation,b->entry_generation);
    RC_S("prepared.capability_revision",a->capability_revision,b->capability_revision);
    RC_S("prepared.scratch_ready",a->execution_scratch_ready,b->execution_scratch_ready);
    RC_S("prepared.budget_declined",a->answer_collection_budget_declined,b->answer_collection_budget_declined);
    if(!prime_replay_compare_program(a->program,b->program,read))return false;
    for(size_t i=0u;i<PREPARED_PURE_DECLINE_SLOTS;i++) {
        const PreparedPureDeclineSlot *p=&a->decline_slots[i],*q=&b->decline_slots[i];
        RC_S("prepared.decline_index",i,i);RC_S("prepared.stamp",p->stamp,q->stamp);
        bool pa=p->stamp!=0u&&p->stamp-1u==a->entry_generation;
        bool qa=q->stamp!=0u&&q->stamp-1u==b->entry_generation;
        if(pa!=qa)return false;
        if(!pa)continue;
#define PD(f) RC_FIELDS("prepared.decline",p,q,f)
        PD(head);PD(arity);PD(language_id);PD(call_mode);PD(kind);
        PD(entry_arguments_are_values);PD(total_structural_equality);PD(declined);
#undef PD
    }
    return true;
}
static bool prime_replay_compare_driver(const PrimeEvalStackDriver *a,
    const PrimeEvalStackDriver *b,const CettaCheckpointCompare *read) {
    if(a->active_task||b->active_task||a->running_task||b->running_task||
        a->prepared_opaque_frames||b->prepared_opaque_frames||
        a->root_call_running||b->root_call_running||a->continuations.top||b->continuations.top||
        a->continuations.depth||b->continuations.depth||
        a->observe_context_read||b->observe_context_read||
        a->context_read_context||b->context_read_context)return false;
    RC_P("driver.top",a->top,b->top);RC_P("driver.control",a->control,b->control);
    RC_P("driver.continuations.top",a->continuations.top,b->continuations.top);
    RC_S("driver.continuations.depth",a->continuations.depth,b->continuations.depth);
    if(!read->arena(read->context,"driver.arena",a->arena,b->arena))return false;
    RC_P("driver.root_target",a->root_target,b->root_target);
    RC_P("driver.stream_pending",a->stream_pending,b->stream_pending);
#define DD(f) RC_FIELDS("driver",a,b,f)
    DD(task_ready);DD(raw_call_depth);DD(bind_depth);DD(poison_depth);DD(running_bind_task);
    DD(pure_authority);DD(running_task);DD(heap_phase);DD(collection_admitted);DD(failed);DD(capacity_blocked);
    DD(ownership_parked);DD(streaming);DD(answer_limit);DD(nested);DD(root_call_running);DD(frame_depth);
    DD(gc_survivor_floor_bytes);DD(continuation_generation);
    DD(root_weak_head);DD(ordinary_ordered);DD(ordinary_shared_source);DD(ordinary_ready_queue);
    DD(ordinary_refill);DD(ordinary_descendants);DD(capsule_stack);DD(capsule_frontier);
    DD(prepared_opaque_frames);
#undef DD
    if(a->task_ready!=b->task_ready)return false;
    return (!a->task_ready||prime_replay_compare_task(&a->task,&b->task,read))&&
        prime_replay_compare_mark(&a->gc_anchor,&b->gc_anchor,read)&&
        prime_replay_compare_prepared_cache(&a->prepared_pure_cache,&b->prepared_pure_cache,read);
}
static bool prime_replay_compare_history(const PrimeNativeHistory *a,const PrimeNativeHistory *b,
    const CettaCheckpointCompare *read) {
    PrimeNativeHistory absent = {0};
    /* Retired carrier metadata is not an observer or a replay diary. */
    if(a->forgotten)a=&absent;
    if(b->forgotten)b=&absent;
    if(a->active||b->active||a->count!=b->count||a->count>a->spec.selections||b->count>b->spec.selections||
        a->reservations>a->spec.selections||b->reservations>b->spec.selections||
        (a->count&&(!a->entries||!b->entries)))return false;
    if(!prime_replay_compare_history_spec(&a->spec,&b->spec,read)||
        !prime_need_recording_compare(a->group,b->group,read))return false;
#define HH(f) RC_FIELDS("history",a,b,f)
    HH(count);HH(reservations);HH(omitted);HH(overflow);HH(stopped);HH(stop);
#undef HH
    RC_S("history.start",a->start,b->start);
    RC_F("history.entries",(uintptr_t)a->entries,(uintptr_t)b->entries);
    for(size_t i=0u;i<a->count;i++) {
        const PrimeNativeHistorySelection *p=&a->entries[i],*q=&b->entries[i];
        RC_S("history.index",i,i);
        RC_I("history.occurrence",CETTA_CHECKPOINT_ISSUED_OCCURRENCE,p->occurrence,q->occurrence);
        RC_S("history.spent",p->spent,q->spent);RC_S("history.paid",p->paid,q->paid);
        RC_S("history.preparation",p->preparation,q->preparation);
        if(!prime_replay_compare_text(p->protocol,q->protocol,read)||
            !prime_need_receipt_compare(&p->operation,&q->operation,read)||
            !prime_need_receipt_compare(&p->prepared,&q->prepared,read))return false;
    }
    return true;
}

static bool prime_replay_compare_selector(const EquationMatchDecisionCacheSlot *x,
    const EquationMatchDecisionCacheSlot *y,const CettaCheckpointCompare *read) {
    RC_P("selector_owner",x,y);
    if(!x||!y)return !x&&!y;
    if(!x->references||!y->references||!x->valid||!y->valid||
        !x->pattern_arena_initialized||!y->pattern_arena_initialized||
        !x->decision||!y->decision||x->equation_count!=y->equation_count||
        (x->equation_count&&(!x->plans||!y->plans)))return false;
    RC_F("selector_owner.references",x->references,y->references);
    RC_S("selector_owner.valid",x->valid,y->valid);
    RC_S("selector_owner.pattern_ready",x->pattern_arena_initialized,y->pattern_arena_initialized);
    RC_S("selector_owner.head",x->head,y->head);RC_S("selector_owner.mode",x->mode,y->mode);
    RC_S("selector_owner.rows",x->equation_count,y->equation_count);
    RC_S("selector_owner.refinements",x->guard_refinement_count,y->guard_refinement_count);
    const SpaceEquationToken *p=&x->equation_projection,*q=&y->equation_projection;
    if(p->space!=q->space||p->projection_dependency!=q->projection_dependency)return false;
    RC_P("selector_owner.source",p->space,q->space);
    RC_P("selector_owner.dependency",p->projection_dependency,q->projection_dependency);
    RC_S("selector_owner.instance",p->instance_id,q->instance_id);
    RC_S("selector_owner.revision",p->equation_revision,q->equation_revision);
    RC_S("selector_owner.dependency_epoch",p->projection_dependency_epoch,q->projection_dependency_epoch);
    if(!prime_replay_compare_semantics(&x->semantics,&y->semantics,read)||
        !prime_replay_compare_arena(&x->pattern_arena,&y->pattern_arena,read)||
        !cetta_match_decision_checkpoint_compare(x->decision,y->decision,read))return false;
    RC_P("selector_owner.plans",x->plans,y->plans);
    for(size_t j=0u;j<x->equation_count;j++) {
        const HeEquationMatchPlan *s=&x->plans[j],*t=&y->plans[j];
        RC_S("selector_owner.plan_index",j,j);
#define SP(f) RC_A("selector_plan."#f,s->f,t->f)
        SP(equation);SP(match_pattern);SP(result_template);
        SP(refinement_fallback_pattern);SP(refinement_failure_template);
#undef SP
        RC_S("selector_plan.guards",s->consumed_guard_count,t->consumed_guard_count);
        if(!bindings_frame_schema_compare(s->frame_schema,t->frame_schema,read))return false;
    }
    return true;
}

static bool prime_replay_compare_selectors(const PrimeNativeControl *a,
    const PrimeNativeControl *b,const CettaCheckpointCompare *read) {
    for(size_t i=0u;;i++) {
        const EquationMatchDecisionCacheSlot *x=parent_copy_selector_at(a,i),*y=parent_copy_selector_at(b,i);
        RC_S("selector_owner.index",i,i);
        if(!prime_replay_compare_selector(x,y,read))return false;
        if(!x||!y)return !x&&!y;
        if(i==SIZE_MAX)return false;
    }
}

static bool prime_replay_compare_service_stores_in(const EquationMatchDecisionCache *a,
    const SpaceExecutionAnalysisCache *ae,const EquationMatchDecisionCache *b,
    const SpaceExecutionAnalysisCache *be,const CettaCheckpointCompare *read,
    const SpaceReadToken *at,const SpaceReadToken *bt) {
    if(!a||!ae||!b||!be)return false;
    RC_P("source_services.selectors",a,b);
    RC_S("source_services.clock",a->clock,b->clock);
    for(size_t i=0u;i<EQUATION_MATCH_DECISION_CACHE_SLOTS;i++) {
        RC_S("source_services.slot",i,i);
        RC_S("source_services.used",a->used[i],b->used[i]);
        if(!prime_replay_compare_selector(a->slots[i],b->slots[i],read))return false;
    }
    return at && bt ? space_execution_analysis_cache_compare_targets(ae,be,*at,*bt,read)
        : space_execution_analysis_cache_compare(ae,be,read);
}
static __attribute__((unused)) bool prime_replay_compare_service_stores(const EquationMatchDecisionCache *a,
    const SpaceExecutionAnalysisCache *ae,const EquationMatchDecisionCache *b,
    const SpaceExecutionAnalysisCache *be,const CettaCheckpointCompare *read) {
    return prime_replay_compare_service_stores_in(a,ae,b,be,read,NULL,NULL);
}
static bool prime_replay_compare_services_in(const PrimeNativeSourceServices *a,
    const PrimeNativeSourceServices *b,const CettaCheckpointCompare *read,
    const SpaceReadToken *at,const SpaceReadToken *bt) {
    RC_S("source_services.present",a!=NULL,b!=NULL);
    if(!a||!b)return !a&&!b;
    if(a->active||b->active||!a->effects||!b->effects)return false;
    RC_P("source_services",a,b);RC_S("source_services.active",a->active,b->active);
    return prime_replay_compare_service_stores_in(&a->selectors,a->effects,
        &b->selectors,b->effects,read,at,bt);
}
static bool prime_replay_compare_services(const PrimeNativeSourceServices *a,
    const PrimeNativeSourceServices *b,const CettaCheckpointCompare *read) {
    return prime_replay_compare_services_in(a,b,read,NULL,NULL);
}

/* Compare a relocated frame index by its initialized lookup relation, not
 * physical hash buckets. Every nonempty slot must name exactly one live
 * registered frame; probing from its actual hash must reach that slot. */
static bool prime_replay_frame_index_valid(const PrimeNativeControl *c,
    const CettaCheckpointCompare *read,bool right) {
    size_t cap=c->frame_index_capacity,found=0u;
    if(c->frame_index_count>cap||(cap&&(!c->frame_index||(cap&(cap-1u)))))return false;
    for(size_t i=0u;i<cap;i++) {
        if(!read->physical(read->context,"frame_index.slot",right?0u:i,right?i:0u))return false;
        const PrimeEvalStackFrame *f=c->frame_index[i];
        if(!f)continue;
        bool member=false;
        for(const PrimeEvalStackFrame *p=c->frames;p;p=p->control_next) {
            if(!read->physical(read->context,"frame_index.member_probe",right?0u:1u,right?1u:0u))return false;
            if(p==f){member=true;break;}
        }
        if(!member)return false;
        for(size_t j=0u;j<i;j++) {
            if(!read->physical(read->context,"frame_index.unique_probe",right?0u:1u,right?1u:0u))return false;
            if(c->frame_index[j]==f)return false;
        }
        size_t at=prime_native_frame_index_slot(f,cap),steps=0u;
        while(at!=i) {
            if(!read->physical(read->context,"frame_index.lookup_probe",right?0u:1u,right?1u:0u))return false;
            if(!c->frame_index[at]||++steps>=cap)return false;
            at=(at+1u)&(cap-1u);
        }
        found++;
    }
    size_t live=0u;
    for(const PrimeEvalStackFrame *p=c->frames;p;p=p->control_next) {
        if(!read->physical(read->context,"frame_index.live_frame",right?0u:1u,right?1u:0u))return false;
        if(live==SIZE_MAX)return false;
        live++;
    }
    return found==c->frame_index_count&&found==live;
}
static bool prime_replay_compare_advice(const PrimeNativeAdvice *a,
    const PrimeNativeAdvice *b,const CettaCheckpointCompare *read) {
    for(;a&&b;a=a->next,b=b->next) {
        RC_P("advice",a,b);RC_P("advice.next",a->next,b->next);
        RC_P("advice.inherited",a->inherited,b->inherited);
        RC_P("advice.follows",a->follows,b->follows);
        RC_S("advice.scoring",a->scoring,b->scoring);
        if(a->scoring||b->scoring||a->key||b->key||a->key_text||b->key_text||
            a->coefficient||b->coefficient)return false;
        RC_A("advice.subject",a->subject,b->subject);
        RC_A("advice.factor",a->factor,b->factor);
        RC_A("advice.coefficient",a->coefficient,b->coefficient);
        RC_A("advice.key",a->key,b->key);
        if(!prime_replay_compare_text(a->key_text,b->key_text,read)||
            !bindings_checkpoint_compare(&a->environment,&b->environment,read))return false;
    }
    RC_S("advice.end",a==NULL,b==NULL);
    return !a&&!b;
}

static bool prime_replay_compare_control_fields_in(const PrimeNativeControl *a,
    const PrimeNativeControl *b,const CettaCheckpointCompare *read,
    SpaceCheckpointBudget *transaction,const AtomDeepCopySession *origin) {
    if(!a||!b||!cetta_checkpoint_compare_valid(read)||!a->owned_arena||!b->owned_arena||
        a->driver.control!=a||b->driver.control!=b||a->driver.arena!=&a->storage||b->driver.arena!=&b->storage||
        !parent_copy_quiescent_in(a,a->leased,transaction!=NULL,NULL)||
        !parent_copy_quiescent_in(b,b->leased,transaction!=NULL,NULL)||
        a->space!=b->space||a->worker_execution||b->worker_execution||
        a->active_advice||b->active_advice||
        a->weight_analysis||b->weight_analysis||a->weight_analyses||b->weight_analyses)return false;
    RC_P("control",a,b);RC_P("control.space",a->space,b->space);
    RC_P("control.active_work",a->active_work,b->active_work);
    RC_P("control.active_advice",a->active_advice,b->active_advice);
    RC_P("control.advice_cells",a->advice_cells,b->advice_cells);
    RC_P("control.parallel_owner",a->parallel_owner,b->parallel_owner);
    RC_P("control.agenda",a->space_agenda,b->space_agenda);
    if(transaction && (!parent_copy_transaction_admitted(a,a->leased,transaction,NULL) ||
        !parent_copy_transaction_admitted(b,b->leased,transaction,NULL) ||
        !cetta_space_agenda_checkpoint_compare(a->space_agenda,b->space_agenda,origin,read,transaction)))return false;
    RC_P("control.weight_analysis",a->weight_analysis,b->weight_analysis);
    RC_P("control.weight_analyses",a->weight_analyses,b->weight_analyses);
    RC_F("control.inflight_storage",(uintptr_t)a->parallel_inflight,(uintptr_t)b->parallel_inflight);
    /* Public enrollment and the capsule's exclusive lease are checked by
     * the caller. A held unpublished image is not the same public handle. */
    RC_F("control.handle",a->handle_id,b->handle_id);RC_F("control.leased",a->leased,b->leased);
#define CC(f) RC_FIELDS("control",a,b,f)
    CC(frame_sequence);CC(open_source_frames);CC(active_allowance);CC(active_spent);
    CC(key_generation);CC(active_scoring);CC(pure_authority);CC(worker_execution);
    CC(effect_pending);CC(need_pending);CC(open_frames);CC(failed);CC(invalidated);
    CC(reclaims_frames);
    CC(completion);CC(owned_arena);CC(resource_pending);CC(cost_enabled);
    CC(breakpoint_hit);CC(breakpoint_incomplete);CC(breakpoint_operation);CC(breakpoint_checks);
    CC(breakpoint_resource_commit);
    CC(breakpoint_resource_activation);
    CC(published);CC(delivered_count);CC(steps);CC(base_environment_initialized);
    CC(goal_generation);CC(goal_size);CC(next_last);CC(next_pick_valid);CC(empty_pick_generated);
    CC(goal_found);CC(goal_reported);CC(selected_length);CC(selected_delivered);CC(costs_completed);
    CC(consumer_revision_count);CC(live_bound);CC(parallel_wave_count);CC(effect_barrier_count);
    CC(need_barrier_count);CC(receipt_interval_count);CC(parallel_inflight_count);CC(parked_count);
    CC(parallel_wave_pending);CC(parallel_wave_workers);CC(parallel_wave_deferred);CC(parallel_wave_cursor);
    CC(bounded_head);CC(weight_environment_initialized);CC(weight_generation);CC(weight_scope_conflict);
    CC(weight_keys);CC(weight_licensed);CC(weight_stopped_early);
#undef CC
    RC_I("control.active_occurrence",CETTA_CHECKPOINT_ISSUED_OCCURRENCE,a->active_occurrence,b->active_occurrence);
    RC_I("control.breakpoint_occurrence",CETTA_CHECKPOINT_ISSUED_OCCURRENCE,a->breakpoint_occurrence,b->breakpoint_occurrence);
#define CA(f) RC_A("control."#f,a->f,b->f)
    CA(active_key);CA(active_bound);CA(pending_effect);CA(source_expression);CA(weight_algebra);CA(weight_subject);
    CA(weight_coefficient_variable);CA(weight_factor_variable);CA(weight_refusal);
    CA(weight_frontier_bound);CA(weight_selected_bound);
#undef CA
    RC_P("control.weight_read_frame",a->weight_read_frame,b->weight_read_frame);
    RC_P("control.weight_descriptor_frame",a->weight_descriptor_frame,b->weight_descriptor_frame);
    if(a->weight_space!=b->weight_space)return false;
    RC_P("control.weight_space",a->weight_space,b->weight_space);
    if(!prime_replay_compare_advice(a->advice_cells,b->advice_cells,read)||
        !prime_replay_compare_driver(&a->driver,&b->driver,read)||
        !prime_replay_compare_hub(&a->hub,&b->hub,read)||
        !prime_replay_compare_arena(&a->storage,&b->storage,read)||
        !prime_replay_compare_episode(a,b,read)||
        !prime_replay_compare_read(a->source_read,b->source_read,read)||
        !prime_replay_compare_program(a->resource_program_read,b->resource_program_read,read)||
        !prime_replay_compare_registry_view(&a->registry_read,&b->registry_read,read)||
        !prime_replay_compare_cost(&a->event_cost,&b->event_cost,read)||
        !prime_replay_compare_outcomes(&a->output,&b->output,read)||
        !prime_replay_compare_policy(&a->policy,&b->policy,read)||
        !prime_replay_compare_history(&a->history,&b->history,read)||
        !prime_replay_compare_read_view(&a->weight_source,&b->weight_source,read)||
        !cetta_weight_ledger_compare(&a->weight_entry,&b->weight_entry,read))return false;
    if(a->base_environment_initialized!=b->base_environment_initialized||
        a->weight_environment_initialized!=b->weight_environment_initialized)return false;
    if((a->base_environment_initialized&&!bindings_checkpoint_compare(&a->base_environment,&b->base_environment,read))||
        (a->weight_environment_initialized&&!bindings_checkpoint_compare(&a->weight_environment,&b->weight_environment,read)))return false;
    const PrimeEvalStackFrame *p=a->frames,*q=b->frames;
    for(;p&&q;p=p->control_next,q=q->control_next)
        if(p->control_retired||q->control_retired||!prime_replay_compare_frame(p,q,read))return false;
    if(p||q)return false;
    p=a->retired_frames;q=b->retired_frames;
    for(;p&&q;p=p->control_next,q=q->control_next)
        if(!p->control_retired||!q->control_retired||!prime_replay_compare_frame(p,q,read))return false;
    if(p||q||!prime_replay_frame_index_valid(a,read,false)||!prime_replay_frame_index_valid(b,read,true))return false;
    if(a->reclaimed_frames||b->reclaimed_frames)return false;
    for(size_t i=0u;i<sizeof(a->open_force_buckets)/sizeof(*a->open_force_buckets);i++) {
        RC_S("force_bucket.index",i,i);
        RC_P("force_bucket.frame",a->open_force_buckets[i],b->open_force_buckets[i]);
    }
    RC_S("frame_index.count",a->frame_index_count,b->frame_index_count);
    RC_F("frame_index.capacity",a->frame_index_capacity,b->frame_index_capacity);
    RC_F("frame_index.storage",(uintptr_t)a->frame_index,(uintptr_t)b->frame_index);
    if(!cetta_checkpoint_extent(a->changed_frames,a->changed_count,a->changed_capacity,
        b->changed_frames,b->changed_count,b->changed_capacity)||
        !cetta_checkpoint_extent(a->query_reads,a->query_read_count,a->query_read_capacity,
        b->query_reads,b->query_read_count,b->query_read_capacity))return false;
    RC_S("control.changed_count",a->changed_count,b->changed_count);
    RC_F("control.changed_capacity",a->changed_capacity,b->changed_capacity);
    for(size_t i=0u;i<a->changed_count;i++)RC_P("control.changed_frame",a->changed_frames[i],b->changed_frames[i]);
    RC_S("control.query_count",a->query_read_count,b->query_read_count);
    RC_F("control.query_capacity",a->query_read_capacity,b->query_read_capacity);
    for(size_t i=0u;i<a->query_read_count;i++)
        if(!prime_replay_compare_read(a->query_reads[i],b->query_reads[i],read))return false;
    if(a->output.len!=b->output.len||a->published>a->output.len||b->published>b->output.len||
        a->delivered_count>a->output.len||b->delivered_count>b->output.len||
        (a->delivered_capacity&&!a->delivered)||(b->delivered_capacity&&!b->delivered))return false;
    RC_F("control.delivered_capacity",a->delivered_capacity,b->delivered_capacity);
    size_t delivered_a=0u,delivered_b=0u;
    for(CettaCount i=0u;i<a->output.len;i++) {
        bool x=i<a->delivered_capacity&&a->delivered[i],y=i<b->delivered_capacity&&b->delivered[i];
        RC_S("control.delivered_index",i,i);RC_S("control.delivered",x,y);
        delivered_a+=x;delivered_b+=y;
    }
    if(delivered_a!=a->delivered_count||delivered_b!=b->delivered_count)return false;
    for(size_t i=(size_t)a->output.len;i<a->delivered_capacity;i++) {
        RC_F("control.left_unused_delivery",i,0u);if(a->delivered[i])return false;
    }
    for(size_t i=(size_t)b->output.len;i<b->delivered_capacity;i++) {
        RC_F("control.right_unused_delivery",0u,i);if(b->delivered[i])return false;
    }
    RC_S("control.pick_present",a->next_pick!=NULL,b->next_pick!=NULL);
    if((!a->next_pick)!=(!b->next_pick))return false;
    if(a->next_pick&&!prime_replay_compare_indices(a->next_pick,a->goal_size,a->goal_size,
        b->next_pick,b->goal_size,b->goal_size,read))return false;
    if(a->selected_delivered>a->selected_length||b->selected_delivered>b->selected_length||
        !prime_replay_compare_indices(a->selected,a->selected_length,a->selected_length,
            b->selected,b->selected_length,b->selected_length,read))return false;
    /* Costs are calloc/zero-extended and may be pending at an initialized
     * null slot. Keep presence independently of the numerical coefficient. */
    if(a->costs_capacity!=b->costs_capacity||
        !prime_replay_compare_atoms(a->costs,a->costs_capacity,b->costs,b->costs_capacity,read)||
        !prime_replay_compare_atoms(a->consumer_revisions,a->consumer_revision_count,b->consumer_revisions,b->consumer_revision_count,read)||
        !prime_replay_compare_atoms(a->parallel_waves,a->parallel_wave_count,b->parallel_waves,b->parallel_wave_count,read)||
        !prime_replay_compare_atoms(a->effect_barriers,a->effect_barrier_count,b->effect_barriers,b->effect_barrier_count,read)||
        !prime_replay_compare_atoms(a->need_barriers,a->need_barrier_count,b->need_barriers,b->need_barrier_count,read))return false;
    if(a->receipt_interval_count!=b->receipt_interval_count||
        (a->receipt_interval_count&&(!a->receipt_intervals||!b->receipt_intervals)))return false;
    for(size_t i=0u;i<a->receipt_interval_count;i++) {
        RC_S("control.receipt_begin_work",a->receipt_intervals[i].begin,b->receipt_intervals[i].begin);
        RC_S("control.receipt_end_work",a->receipt_intervals[i].end,b->receipt_intervals[i].end);
    }
    if(!cetta_checkpoint_extent(a->parked,a->parked_count,a->parked_capacity,
        b->parked,b->parked_count,b->parked_capacity))return false;
    RC_F("control.parked_capacity",a->parked_capacity,b->parked_capacity);
    for(size_t i=0u;i<a->parked_count;i++)
        if(!prime_replay_compare_continuation(&a->parked[i],&b->parked[i],read))return false;
    if(!prime_replay_compare_force_retry(&a->force_retry,&b->force_retry,read))return false;
    return prime_replay_compare_selectors(a,b,read);
}

static bool prime_replay_compare_control_fields(const PrimeNativeControl *a,
    const PrimeNativeControl *b,const CettaCheckpointCompare *read) {
    return prime_replay_compare_control_fields_in(a,b,read,NULL,NULL);
}

static bool prime_replay_compare_control(const PrimeNativeControl *a,
    const PrimeNativeControl *b,const CettaCheckpointCompare *read) {
    return prime_replay_compare_control_fields(a,b,read) &&
        prime_replay_compare_services(a->source_services,b->source_services,read);
}

#undef RC_S
#undef RC_F
#undef RC_P
#undef RC_I
#undef RC_A
#undef RC_FIELDS
