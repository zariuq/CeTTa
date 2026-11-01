/* Independent rooted relation: enumerate each retained carrier and compare
 * logical fields plus physical alias classes, without consulting the copier. */
typedef struct { unsigned kind; const void *source, *image; } CopyIdentity;
typedef struct { CopyIdentity keys[8192]; size_t count, shared; bool valid; } CopyRelation;
static bool copy_identity(CopyRelation *r,unsigned kind,const void *s,const void *d) {
    if(!s||!d)return r->valid&=(s==NULL)==(d==NULL);
    if(s==d)return r->valid=false;
    for(size_t i=0;i<r->count;i++)if(r->keys[i].kind==kind){
        if((r->keys[i].source==s)!=(r->keys[i].image==d))return r->valid=false;
        if(r->keys[i].source==s){r->shared++;return true;}
    }
    if(r->count>=sizeof(r->keys)/sizeof(*r->keys))return r->valid=false;
    r->keys[r->count++]=(CopyIdentity){kind,s,d};return true;
}
static bool copy_need(CopyRelation *r,const PrimeNeedSnapshot *s,const PrimeNeedSnapshot *d) {
    if(s->session_id!=d->session_id||s->max_storage_key!=d->max_storage_key)return r->valid=false;
    bool good=copy_identity(r,1,s->top,d->top);
#if CETTA_PRIME_NEED_HEAP_INDEX
    good=copy_identity(r,2,s->heap_index,d->heap_index)&&good;
    good=copy_identity(r,2,s->lineage_index,d->lineage_index)&&good;
#endif
    return good;
}
static bool copy_receipt(CopyRelation *r,const PrimeNeedReceipt *s,const PrimeNeedReceipt *d,unsigned kind) {
    return (s->session_id==d->session_id&&copy_identity(r,kind,s->top,d->top))||(r->valid=false);
}
static bool copy_factors(CopyRelation *r,const CettaWeightLedger *s,const CettaWeightLedger *d) {
    if(s->length!=d->length||s->claim_count!=d->claim_count)return r->valid=false;
    const CettaWeightFactor *a=s->last,*b=d->last;
    for(size_t i=0;i<s->length;i++){
        if(!a||!b||a->occurrence!=b->occurrence||a->dependency!=b->dependency||!atom_eq(a->value,b->value))return r->valid=false;
        if(!copy_identity(r,5,a,b))return false;
        a=a->previous;b=b->previous;
    }
    if(a||b)return r->valid=false;
    const CettaWeightClaim *x=s->claims,*y=d->claims;
    for(size_t i=0;i<s->claim_count;i++){
        if(!x||!y||x->occurrence!=y->occurrence||x->handler!=y->handler)return r->valid=false;
        if(!copy_identity(r,6,x,y))return false;
        x=x->previous;y=y->previous;
    }
    return (!x&&!y)||(r->valid=false);
}
typedef struct {
    CopyRelation *relation;
    const PrimeNeedReaderContext *other;
    size_t count;
} CopyReaders;
static CettaMatchCompletion copy_reader_entry(uint64_t key,
        const PrimeNeedReaderValue *source,void *raw) {
    CopyReaders *copy=raw;PrimeNeedReaderValue image={0};
    if(!prime_need_reader_context_lookup(copy->other,key,&image)||
        source->operations!=image.operations)return CETTA_MATCH_REFUTED;
    bool good=false;
    if(source->operations==&prime_need_call_link_ops) {
        const PrimeNeedCallLink *a=source->value,*b=image.value;
        good=copy_identity(copy->relation,17,a,b)&&
            copy_identity(copy->relation,21,a->sources,b->sources)&&
            copy_identity(copy->relation,22,a->sources->episode,b->sources->episode)&&
            a->identity_count==b->identity_count;
        for(size_t i=0u;good&&i<a->identity_count;i++)
            good=!memcmp(&a->identities[i],&b->identities[i],sizeof(a->identities[i]));
    } else if(source->operations==&prime_need_call_reader_ops) {
        const PrimeNeedCallForceReader *a=source->value,*b=image.value;
        uint64_t ap=0u,bp=0u;
        good=copy_identity(copy->relation,18,a,b)&&
            copy_identity(copy->relation,17,a->link,b->link)&&
            copy_identity(copy->relation,19,a->cell,b->cell)&&
            copy_identity(copy->relation,20,a->slot,b->slot)&&
            copy_identity(copy->relation,23,a->position,b->position)&&
            cetta_shared_return_checkpoint_position(a->position,&ap)&&
            cetta_shared_return_checkpoint_position(b->position,&bp)&&ap==bp;
    }
    copy->count++;
    return good?CETTA_MATCH_MATCHED:CETTA_MATCH_REFUTED;
}
static CettaMatchCompletion count_reader_entry(uint64_t key,
        const PrimeNeedReaderValue *value,void *raw) {
    (void)key;(void)value;(*(size_t *)raw)++;return CETTA_MATCH_MATCHED;
}
static bool copy_readers(CopyRelation *r,const PrimeNeedReaderContext *a,
        const PrimeNeedReaderContext *b) {
    if(!copy_identity(r,24,a,b))return false;
    if(!a||!b)return a==b;
    CopyReaders copy={.relation=r,.other=b};size_t other=0u;
    return prime_need_reader_context_visit(a,copy_reader_entry,&copy)==CETTA_MATCH_MATCHED&&
        prime_need_reader_context_visit(b,count_reader_entry,&other)==CETTA_MATCH_MATCHED&&
        copy.count==other;
}
static bool copy_bindings(CopyRelation *r,const Bindings *s,const Bindings *d) {
    if(!copy_readers(r,bindings_reader_context_view(s),bindings_reader_context_view(d)))
        return r->valid=false;
    if((s->prime_ext!=NULL)!=(d->prime_ext!=NULL)||bindings_occurrence_token(s)!=bindings_occurrence_token(d))return r->valid=false;
    bool good=copy_need(r,bindings_need_view(s),bindings_need_view(d));
    good=copy_receipt(r,bindings_branch_state_view(s),bindings_branch_state_view(d),3)&&good;
    good=copy_factors(r,bindings_weight_view(s),bindings_weight_view(d))&&good;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    good=copy_receipt(r,bindings_receipt_view(s),bindings_receipt_view(d),3)&&good;
#endif
    return good;
}
static bool copy_outcomes(CopyRelation *r,const OutcomeSet *s,const OutcomeSet *d) {
    if(s->len!=d->len||s->cap!=d->cap)return r->valid=false;
    for(size_t i=0;i<s->len;i++){
        if(s->items[i].kind!=d->items[i].kind||
           s->items[i].result_form!=d->items[i].result_form||
           (s->items[i].materialized_atom!=NULL)!=(d->items[i].materialized_atom!=NULL)||
           (s->items[i].delayed!=NULL)!=(d->items[i].delayed!=NULL)||
           !copy_bindings(r,&s->items[i].env,&d->items[i].env))return r->valid=false;
    }
    return true;
}
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
static bool copy_recorded_receipt(CopyRelation *r,const PrimeNeedReceipt *s,const PrimeNeedReceipt *d) {
    bool good=copy_receipt(r,s,d,3);
    good=copy_identity(r,7,s->recording,d->recording)&&good;
    good=copy_identity(r,8,s->owner,d->owner)&&good;
    return good;
}
#endif
static bool copy_pattern_request(CopyRelation *r,const PrimeNeedPatternRequest *a,
    const PrimeNeedPatternRequest *b) {
    if(a->length!=b->length || a->capacity!=b->capacity || a->demand!=b->demand ||
       a->length>a->capacity || (a->capacity&&(!a->path||!b->path)))return r->valid=false;
    if(!copy_identity(r,12,a->path,b->path))return false;
    for(size_t i=0u;i<a->length;i++)if(a->path[i]!=b->path[i])return r->valid=false;
    return true;
}
static bool copy_pattern_readiness(CopyRelation *r,const PrimeNeedPatternReadiness *a,
    const PrimeNeedPatternReadiness *b) {
    if(!copy_identity(r,13,a,b))return false;
    if(!a||!b)return !a&&!b;
    if(a->length!=b->length || a->capacity!=b->capacity || a->length>a->capacity ||
       (a->capacity&&(!a->paths||!b->paths)))return r->valid=false;
    if(!copy_identity(r,14,a->paths,b->paths))return false;
    for(size_t i=0u;i<a->length;i++)if(!copy_pattern_request(r,&a->paths[i],&b->paths[i]))return false;
    return true;
}
static bool copy_equation_metadata(CopyRelation *r,const PrimeNeedEquationMetadata *a,
    const PrimeNeedEquationMetadata *b,size_t count) {
    if(a->capacity!=b->capacity || a->has_observations!=b->has_observations ||
       count>a->capacity || !copy_identity(r,16,a->items,b->items))return r->valid=false;
    for(size_t i=0u;i<count;i++)if(a->items[i].plan_index!=b->items[i].plan_index ||
        a->items[i].kind!=b->items[i].kind || a->items[i].world_order!=b->items[i].world_order)
        return r->valid=false;
    return true;
}
/* Enumerate service ownership independently of the copier's identity table.
 * In particular, one production and cursor may serve several physical readers. */
static bool copy_service_owners(CopyRelation *r,const PrimeNativeControl *source,
        const PrimeNativeControl *image) {
    copy_identity(r,40,source,image);
    for(size_t i=0u;r->valid&&i<r->count;i++) {
        CopyIdentity item=r->keys[i];
        if(item.kind==17u) {
            const PrimeNeedCallLink *s=item.source,*d=item.image;
            if(s->generation!=d->generation)return r->valid=false;
            copy_identity(r,46,s->lease,d->lease);
        } else if(item.kind==40u) {
            const PrimeNativeControl *s=item.source,*d=item.image;
            if(s->force_service_park_reservations!=d->force_service_park_reservations||
                s->parked_count!=d->parked_count) return r->valid=false;
            copy_identity(r,41,s->force_services,d->force_services);
            copy_identity(r,41,s->force_service_focus,d->force_service_focus);
            copy_identity(r,47,s->owned_top_episode?s->owned_top_episode->execution:NULL,
                d->owned_top_episode?d->owned_top_episode->execution:NULL);
            size_t queued=cetta_continuation_hub_length(&s->hub);
            if(queued!=cetta_continuation_hub_length(&d->hub))return r->valid=false;
            size_t count=queued+s->parked_count+(s->force_retry.continuation.payload?1u:0u);
            for(size_t j=0u;j<count;j++) {
                const CettaOwnedContinuation *a=j<queued?cetta_continuation_hub_at(&s->hub,j):
                    j<queued+s->parked_count?&s->parked[j-queued]:&s->force_retry.continuation;
                const CettaOwnedContinuation *b=j<queued?cetta_continuation_hub_at(&d->hub,j):
                    j<queued+d->parked_count?&d->parked[j-queued]:&d->force_retry.continuation;
                const PrimeNativeWork *x=a->payload,*y=b->payload;
                if(!x||!y||x->service_resume!=y->service_resume||
                    x->service_park_reserved!=y->service_park_reserved)return r->valid=false;
                copy_identity(r,43,x,y);copy_identity(r,40,x->owner,y->owner);
                copy_identity(r,41,x->force_service,y->force_service);
                copy_identity(r,41,x->service_request,y->service_request);
                copy_identity(r,15,x->parent,y->parent);copy_identity(r,15,x->frame,y->frame);
            }
        } else if(item.kind==41u) {
            const PrimeNeedForceService *s=item.source,*d=item.image;
            if(s->parked!=d->parked||s->cancelled!=d->cancelled||
                s->waiting_source!=d->waiting_source||s->finished!=d->finished||
                s->library!=d->library||s->registry!=d->registry||
                s->context.self_policy!=d->context.self_policy||
                s->context.identity_policy!=d->context.identity_policy||
                s->context.symbol_scope!=d->context.symbol_scope||
                !prime_need_call_symbol_context_equal(&s->context.symbol_source,&d->context.symbol_source))
                return r->valid=false;
            copy_identity(r,40,s->coordinator,d->coordinator);
            copy_identity(r,40,s->requester_control,d->requester_control);
            copy_identity(r,43,s->work,d->work);copy_identity(r,43,s->requester_work,d->requester_work);
            copy_identity(r,15,s->frame,d->frame);copy_identity(r,15,s->cancel_root,d->cancel_root);
            if(s->frame)copy_identity(r,41,s->frame->call_force_service,d->frame->call_force_service);
            copy_identity(r,42,s->source,d->source);copy_identity(r,44,s->activation,d->activation);
            copy_identity(r,41,s->previous,d->previous);copy_identity(r,41,s->next,d->next);
            copy_identity(r,41,s->parent,d->parent);copy_identity(r,41,s->dependency,d->dependency);
            copy_identity(r,41,s->wait_previous,d->wait_previous);copy_identity(r,41,s->wait_next,d->wait_next);
            copy_identity(r,23,s->position,d->position);
            uint64_t a=UINT64_MAX,b=UINT64_MAX;
            if(!cetta_shared_return_checkpoint_position(s->position,&a)||
                !cetta_shared_return_checkpoint_position(d->position,&b)||a!=b)return r->valid=false;
            copy_bindings(r,&s->environment,&d->environment);
        } else if(item.kind==42u) {
            const PrimeNeedForceProduction *s=item.source,*d=item.image;
            if(s->dispatches!=d->dispatches||s->call_refusals!=d->call_refusals||
                s->world_effects!=d->world_effects||s->thunk!=d->thunk||
                s->input_generation!=d->input_generation)return r->valid=false;
            const PrimeNeedSessionPolicy *a=&s->session.policy,*b=&d->session.policy;
            if(s->session.language_id!=d->session.language_id||s->session.speculative!=d->session.speculative||
                a->module_policy.provider_flags!=b->module_policy.provider_flags||
                a->module_policy.relative_module_policy!=b->module_policy.relative_module_policy||
                a->module_policy.transactional_imports!=b->module_policy.transactional_imports||
                a->options.type_check_auto!=b->options.type_check_auto||a->options.he_typing!=b->options.he_typing||
                a->options.interpreter_mode!=b->options.interpreter_mode||
                a->options.max_stack_depth!=b->options.max_stack_depth||a->options.fuel_limit!=b->options.fuel_limit||
                a->options.entry_len!=b->options.entry_len)return r->valid=false;
            if(a->options.entry_len&&(a->options.entries==b->options.entries||
                !arena_has_live_span(&d->storage,b->options.entries,
                    (size_t)b->options.entry_len*sizeof(*b->options.entries))))return r->valid=false;
            for(uint32_t j=0u;j<a->options.entry_len;j++) {
                const CettaEvalOptionEntry *x=&a->options.entries[j],*y=&b->options.entries[j];
                if(x->kind!=y->kind||strncmp(x->key,y->key,sizeof(x->key))||
                    strncmp(x->repr,y->repr,sizeof(x->repr))||
                    (x->kind==CETTA_EVAL_OPTION_VALUE_INT&&x->int_value!=y->int_value))return r->valid=false;
            }
            copy_identity(r,47,s->execution,d->execution);
            copy_identity(r,46,s->input_lease,d->input_lease);
            copy_identity(r,40,s->agenda,d->agenda);
            copy_identity(r,41,s->service_active,d->service_active);
            copy_identity(r,41,s->service_waiters,d->service_waiters);
        } else if(item.kind==46u) {
            const EvalExecutionLease *s=item.source,*d=item.image;
            if(s->generation!=d->generation)return r->valid=false;
            copy_identity(r,47,s->context,d->context);
        } else if(item.kind==47u) {
            const EvalExecutionContext *s=item.source,*d=item.image;
            if(s->root_space!=d->root_space||s->scoped!=d->scoped||
                s->table_ready!=d->table_ready||s->answers_ready!=d->answers_ready||
                s->variants_ready!=d->variants_ready||s->survivor_ready!=d->survivor_ready)
                return r->valid=false;
            copy_identity(r,46,s->lease,d->lease);
        } else if(item.kind==44u) {
            const PrimeEvalScopeNode *s=item.source,*d=item.image;
            if(!d->references||s->root!=d->root||d->validation_cached||d->release_next||
                s->owned.is_current!=d->owned.is_current||s->owned.completion!=d->owned.completion||
                s->projection.pure_authority!=d->projection.pure_authority||
                s->projection.scope_control_pure!=d->projection.scope_control_pure||
                s->projection.scope_scoring!=d->projection.scope_scoring||
                s->projection.scope_invocation_incomplete!=d->projection.scope_invocation_incomplete||
                s->projection.ordinary_ordered!=d->projection.ordinary_ordered||
                s->projection.retain_bind_calls!=d->projection.retain_bind_calls)return r->valid=false;
            copy_identity(r,40,s->coordinator,d->coordinator);
            copy_identity(r,44,s->resource,d->resource);copy_identity(r,44,s->invocation,d->invocation);
            copy_identity(r,44,s->root_node,d->root_node);
            copy_identity(r,45,s->owned.driver,d->owned.driver);
            copy_identity(r,42,s->owned.owner,d->owned.owner);
            copy_identity(r,45,s->projection.scope_failure_owner,d->projection.scope_failure_owner);
            copy_identity(r,42,s->projection.call_permit_context,d->projection.call_permit_context);
            copy_identity(r,42,s->projection.call_refusal_context,d->projection.call_refusal_context);
            copy_identity(r,42,s->projection.own_data_context,d->projection.own_data_context);
            copy_identity(r,42,s->projection.space_read_context,d->projection.space_read_context);
            copy_identity(r,42,s->projection.context_read_context,d->projection.context_read_context);
            if(s->root&&(d->captured_root_authority_parent||d->captured_root_call_permit_parent))
                return r->valid=false;
            if(s->owned.owner&&d->owned.driver!=&d->coordinator->driver&&
                (!arena_retains_owner(&d->holds,d->owned.owner,prime_need_force_production_release)||
                 arena_retains_owner(&d->holds,s->owned.owner,prime_need_force_production_release)))
                return r->valid=false;
        }
    }
    return r->valid;
}

static bool copy_relation(const PrimeNativeControl *s,const PrimeNativeControl *d,CopyRelation *r) {
    *r=(CopyRelation){.valid=true};
    if(s->selected_length!=d->selected_length||s->selected_delivered!=d->selected_delivered||
        s->goal_found!=d->goal_found||s->goal_reported!=d->goal_reported)return false;
    for(size_t i=0u;i<s->selected_length;i++)if(s->selected[i]!=d->selected[i])return false;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if(s->history.count!=d->history.count)return false;
    copy_identity(r,9,s->history.group,d->history.group);
    for(size_t i=0u;i<s->history.count;i++) {
        const PrimeNativeHistorySelection *a=&s->history.entries[i],*b=&d->history.entries[i];
        if(a->occurrence!=b->occurrence||a->spent!=b->spent||a->paid!=b->paid||
            a->preparation!=b->preparation||strcmp(a->protocol,b->protocol))r->valid=false;
        copy_recorded_receipt(r,&a->operation,&b->operation);
        copy_recorded_receipt(r,&a->prepared,&b->prepared);
    }
#endif
    copy_factors(r,&s->weight_entry,&d->weight_entry);
    if(s->base_environment_initialized)copy_bindings(r,&s->base_environment,&d->base_environment);
    if(s->weight_environment_initialized)copy_bindings(r,&s->weight_environment,&d->weight_environment);
    copy_outcomes(r,&s->output,&d->output);
    const PrimeNativeAdvice *sa=s->advice_cells,*da=d->advice_cells;
    for(;sa&&da;sa=sa->next,da=da->next) {
        copy_identity(r,10,sa,da);
        copy_identity(r,10,sa->inherited,da->inherited);
        copy_identity(r,10,sa->follows,da->follows);
        copy_bindings(r,&sa->environment,&da->environment);
    }
    if(sa||da)r->valid=false;
    const PrimeEvalStackFrame *sf=s->frames,*df=d->frames;
    for(;sf&&df;sf=sf->control_next,df=df->control_next){
        copy_identity(r,15,sf,df);
        copy_identity(r,15,sf->force_producer,df->force_producer);
        copy_identity(r,15,sf->force_waiters,df->force_waiters);
        copy_identity(r,15,sf->force_waiter_next,df->force_waiter_next);
        copy_identity(r,15,sf->open_force_next,df->open_force_next);
        /* Allocation capabilities are relocated, independently of shared
         * immutable payloads. A copied FORCE cannot publish into its source. */
        if(!sf->control_retired&&
           (sf->arena!=s->driver.arena||df->arena!=d->driver.arena||
            ((sf->heap_owner==NULL)!=(df->heap_owner==NULL))||
            (sf->heap_owner&&(sf->heap_owner!=s->driver.arena||
                             df->heap_owner!=d->driver.arena))))r->valid=false;
        if(sf->kind!=df->kind||sf->state!=df->state||sf->control_sequence!=df->control_sequence||sf->index!=df->index||
           sf->force_pending_initialized!=df->force_pending_initialized||
           sf->force_pending_observed!=df->force_pending_observed||sf->force_published!=df->force_published||
           sf->force_retry_pending!=df->force_retry_pending||
           sf->return_retry_pending!=df->return_retry_pending||
           sf->call_force_attempt.ready!=df->call_force_attempt.ready||
           sf->call_force_attempt.step!=df->call_force_attempt.step||
           sf->call_force_attempt.completion!=df->call_force_attempt.completion||
           sf->call_force_attempt.source_completion!=df->call_force_attempt.source_completion||
           sf->call_force_attempt.refusal_reason!=df->call_force_attempt.refusal_reason||
           sf->call_force_attempt.steps_spent!=df->call_force_attempt.steps_spent||
           sf->call_force_attempt.refused!=df->call_force_attempt.refused||
           sf->call_force_attempt.preparation_incomplete!=df->call_force_attempt.preparation_incomplete||
           sf->call_force_attempt.interrupted!=df->call_force_attempt.interrupted||
           sf->normalization_value_ready!=df->normalization_value_ready||
           sf->normalization_return_value_ready!=df->normalization_return_value_ready||
           sf->normalization_result_form!=df->normalization_result_form)r->valid=false;
        if(sf->force_pending_initialized)copy_outcomes(r,&sf->force_pending,&df->force_pending);
        if(sf->control_return_advice_length!=df->control_return_advice_length ||
           sf->control_return_advice_capacity!=df->control_return_advice_capacity ||
           sf->control_return_advice_length>sf->control_return_advice_capacity ||
           df->control_return_advice_length>df->control_return_advice_capacity ||
           sf->control_return_advice_length>sf->child.len ||
           df->control_return_advice_length>df->child.len) return r->valid=false;
        copy_identity(r,11,sf->control_return_advice,df->control_return_advice);
        copy_identity(r,10,sf->control_advice,df->control_advice);
        if(sf->control_return_pending!=df->control_return_pending ||
           sf->control_return_pending_begin!=df->control_return_pending_begin ||
           sf->control_return_pending_end!=df->control_return_pending_end ||
           (sf->control_return_pending_advice.key==NULL)!=(df->control_return_pending_advice.key==NULL) ||
           (sf->control_return_pending_advice.bound==NULL)!=(df->control_return_pending_advice.bound==NULL) ||
           (sf->control_return_pending_advice.key &&
            !atom_eq(sf->control_return_pending_advice.key,df->control_return_pending_advice.key)) ||
           (sf->control_return_pending_advice.bound &&
            !atom_eq(sf->control_return_pending_advice.bound,df->control_return_pending_advice.bound)))
            return r->valid=false;
        copy_identity(r,10,sf->control_return_pending_advice.advice,
            df->control_return_pending_advice.advice);
        for(size_t i=0u;i<sf->control_return_advice_length;i++) {
            const PrimeNativeReturnAdvice *a=&sf->control_return_advice[i];
            const PrimeNativeReturnAdvice *b=&df->control_return_advice[i];
            if((a->key==NULL)!=(b->key==NULL)||(a->bound==NULL)!=(b->bound==NULL)||
               (a->key&&!atom_eq(a->key,b->key))||(a->bound&&!atom_eq(a->bound,b->bound)))r->valid=false;
            copy_identity(r,10,a->advice,b->advice);
        }
        if(sf->env_initialized)copy_bindings(r,&sf->env,&df->env);
        if(sf->child_initialized)copy_outcomes(r,&sf->child,&df->child);
        copy_need(r,&sf->before,&df->before);copy_need(r,&sf->evaluating,&df->evaluating);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
        copy_receipt(r,&sf->before_receipt,&df->before_receipt,3);
#endif
        if(sf->match_rows)copy_bindings(r,&sf->match_rows->pending,&df->match_rows->pending);
        if(sf->equation_search){
            const PrimeNeedEquationContinuation *a=sf->equation_search,*b=df->equation_search;
            const OutcomeSet *x[]={&a->work,&a->current,&a->raw_results,&a->residuals,&a->publication,&a->forced_worlds};
            const OutcomeSet *y[]={&b->work,&b->current,&b->raw_results,&b->residuals,&b->publication,&b->forced_worlds};
            if(a->state!=b->state||a->plan_count!=b->plan_count||a->frontier!=b->frontier)r->valid=false;
            if(a->current_work_slot!=b->current_work_slot)r->valid=false;
            if(a->current_world_order!=b->current_world_order ||
               a->world_order_extent!=b->world_order_extent)r->valid=false;
            if(a->inspect_incrementally!=b->inspect_incrementally ||
               a->publication_returns_to_search!=b->publication_returns_to_search)r->valid=false;
            copy_pattern_request(r,&a->inspection,&b->inspection);
            copy_pattern_readiness(r,a->observed,b->observed);
            if(a->work.len!=b->work.len)return r->valid=false;
            for(size_t i=0u;i<a->work.len;i++) {
                if(a->work_meta[i].world_order!=b->work_meta[i].world_order)r->valid=false;
                copy_pattern_readiness(r,a->work_meta[i].observed,b->work_meta[i].observed);
            }
            copy_equation_metadata(r,&a->raw_metadata,&b->raw_metadata,a->raw_results.len);
            copy_equation_metadata(r,&a->publication_metadata,&b->publication_metadata,a->publication.len);
            copy_equation_metadata(r,&a->residual_metadata,&b->residual_metadata,a->residuals.len);
            copy_equation_metadata(r,&a->forced_world_metadata,&b->forced_world_metadata,a->forced_worlds.len);
            for(size_t i=0;i<6;i++)copy_outcomes(r,x[i],y[i]);
            for(size_t i=0;i<a->plan_count;i++) {
                copy_outcomes(r,&a->plans[i].results,&b->plans[i].results);
                copy_equation_metadata(r,&a->plans[i].result_metadata,&b->plans[i].result_metadata,
                    a->plans[i].results.len);
            }
        }
    }
    if(sf||df)r->valid=false;
    for(size_t i=0u;i<sizeof(s->open_force_buckets)/sizeof(*s->open_force_buckets);i++)
        copy_identity(r,15,s->open_force_buckets[i],d->open_force_buckets[i]);
    size_t queued=cetta_continuation_hub_length(&s->hub);
    if(queued!=cetta_continuation_hub_length(&d->hub)||s->parked_count!=d->parked_count)return false;
    bool retry=s->force_retry.continuation.payload!=NULL;
    if(retry!=(d->force_retry.continuation.payload!=NULL)||
       s->force_retry.admitted!=d->force_retry.admitted ||
       s->force_retry.prepared!=d->force_retry.prepared ||
       s->force_retry.call_refusal!=d->force_retry.call_refusal)return false;
    for(size_t i=0;i<queued+s->parked_count+(retry?1u:0u);i++){
        const CettaOwnedContinuation *a=i<queued?cetta_continuation_hub_at(&s->hub,i):
            i<queued+s->parked_count?&s->parked[i-queued]:&s->force_retry.continuation;
        const CettaOwnedContinuation *b=i<queued?cetta_continuation_hub_at(&d->hub,i):
            i<queued+d->parked_count?&d->parked[i-queued]:&d->force_retry.continuation;
        const PrimeNativeWork *x=a->payload,*y=b->payload;
        if(a->occurrence_id!=b->occurrence_id||a->parent_occurrence_id!=b->parent_occurrence_id||
           ((a->resume_store!=0)!=(b->resume_store!=0))||
           (a->resume_store&&(a->resume_store!=s->hub.store.identity||b->resume_store!=d->hub.store.identity))||
           (a->resume_lease==0u)!=(b->resume_lease==0u)||
           x->owner!=s||y->owner!=d||x->task_initialized!=y->task_initialized||
           x->task_preparation_pending!=y->task_preparation_pending)r->valid=false;
        copy_identity(r,15,x->frame,y->frame);copy_identity(r,15,x->parent,y->parent);
        copy_identity(r,10,x->advice,y->advice);
        if(x->task_initialized){
            if(x->task.kind!=y->task.kind||x->task.pure_authority!=y->task.pure_authority)r->valid=false;
            if(x->task.dynamic_env_initialized)copy_bindings(r,&x->task.dynamic_env,&y->task.dynamic_env);
            if(x->task.seed_env_initialized)copy_bindings(r,&x->task.seed_env,&y->task.seed_env);
        }
    }
    return copy_service_owners(r,s,d);
}
