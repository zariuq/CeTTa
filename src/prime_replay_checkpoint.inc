/* The public replay owner uses the existing quiescent parent copier. These
 * helpers stage its actual arena and recording providers; they do not execute
 * an evaluator operation or turn a state copy into replay authentication. */
typedef struct PrimeReplayComparison PrimeReplayComparison;
typedef struct {
    SpaceReadToken source,image;
    SpaceCheckpointIssuance before,after;
    bool complete;
} PrimeReplayTargetOrigin;
typedef struct {
    Arena storage;
    Atom *holder;
    CettaPrimeCheckpointCopy *preparation;
    CettaContinuationStatus status;
    bool exported;
    PrimeReplayComparison *ambient_read;
    bool ambient_services_captured;
    uint64_t ambient_source_handle;
    PrimeReplayTargetOrigin target;
    bool service_only;
    const char *refusal;
} PrimeReplayCheckpoint;

typedef struct {
    const Space **spaces;
    SpaceReadView *reads;
    size_t count,capacity,limit;
    CettaLibraryContext *library;
    Arena *persistent;
    HashConsTable *globals;
    uint32_t persistent_identity;
    uint64_t persistent_epoch,global_instance,symbol_instance;
    /* A separately held mutable registry target, never an immutable source.
     * Its full revision is checked by the recording owner, not renewed here. */
    Space *external_target;
    uint64_t target_instance;
} PrimeReplaySources;

static void prime_replay_checkpoint_origin_free(PrimeReplayCheckpoint *);
static bool prime_replay_checkpoint_origin_check(const PrimeNativeControl *,
    const PrimeReplaySources *,size_t,size_t,const AtomDeepCopyArenaPair *,size_t,
    const AtomDeepCopySession *,PrimeReplayCheckpoint *);

static bool prime_replay_sources_add(PrimeReplaySources *sources,const Space *space) {
    if(!space)return true;
    if(space->universe!=&sources->library->term_universe)return false;
    for(size_t i=0u;i<sources->count;i++)if(sources->spaces[i]==space)return true;
    if(sources->count==sources->capacity) {
        if(sources->count==sources->limit)return false;
        size_t capacity=sources->capacity?sources->capacity*2u:8u;
        if(capacity<sources->capacity||capacity>sources->limit)capacity=sources->limit;
        if(capacity>SIZE_MAX/sizeof(*sources->spaces)||capacity>SIZE_MAX/sizeof(*sources->reads))return false;
        const Space **spaces=calloc(capacity,sizeof(*spaces));
        SpaceReadView *reads=calloc(capacity,sizeof(*reads));
        if(!spaces||!reads){free(spaces);free(reads);return false;}
        if(sources->count) {
            memcpy(spaces,sources->spaces,sources->count*sizeof(*spaces));
            memcpy(reads,sources->reads,sources->count*sizeof(*reads));
        }
        free(sources->spaces);free(sources->reads);
        sources->spaces=spaces;sources->reads=reads;sources->capacity=capacity;
    }
    size_t index=sources->count++;
    sources->spaces[index]=space;
    return space_read_view_capture(space,&sources->reads[index]);
}

static bool prime_replay_sources_capture(PrimeReplaySources *sources,
    const Space *root,size_t capacity) {
    if(capacity>SIZE_MAX/sizeof(*sources->spaces)||
        capacity>SIZE_MAX/sizeof(*sources->reads))return false;
    /* This public profile borrows the actual current runtime, whose library,
     * source universe, persistent arena, hashcons table and symbol service
     * outlive its native handles. Revision capture does not acquire those
     * lifetimes. Private library/universe/global domains refuse. */
    if(!g_library_context||!g_hashcons||!g_symbols)return false;
    Arena *persistent=g_library_context->term_universe.persistent_arena;
    if(!persistent||persistent!=g_eval_fallback_universe.persistent_arena||
        persistent->hashcons!=g_hashcons)return false;
    sources->library=g_library_context;sources->persistent=persistent;
    sources->globals=g_hashcons;sources->persistent_identity=persistent->identity;
    sources->persistent_epoch=persistent->reset_epoch;
    sources->global_instance=hashcons_instance_id(sources->globals);
    sources->symbol_instance=symbol_table_instance_id(g_symbols);
    if(!sources->symbol_instance||!sources->global_instance||sources->global_instance==UINT64_MAX)return false;
    sources->limit=capacity;
    if(!prime_replay_sources_add(sources,root))return false;
    for(size_t i=0u;i<sources->count;i++) {
        const Space *space=sources->spaces[i];
        if(!prime_replay_sources_add(sources,space->overlay_base))return false;
        uint32_t count=space_dependency_count(space);
        for(uint32_t j=0u;j<count;j++)
            if(!prime_replay_sources_add(sources,space_dependency_at(space,j)))return false;
    }
    return true;
}

/* The admitted runtime headers remain held by the enclosing library.
 * Authenticate their current identities before following captured pointers;
 * source revisions are checked separately and do not renew these loans. */
static bool prime_replay_source_owners_current(const PrimeReplaySources *sources) {
    if(!sources||!g_library_context||!g_hashcons||!g_symbols||!sources->persistent)return false;
    if(sources->library!=g_library_context||sources->globals!=g_hashcons||
        sources->global_instance!=hashcons_instance_id(sources->globals)||
        sources->symbol_instance!=symbol_table_instance_id(g_symbols)||
        sources->persistent!=g_library_context->term_universe.persistent_arena||
        sources->persistent->identity!=sources->persistent_identity||
        sources->persistent->reset_epoch!=sources->persistent_epoch||
        sources->persistent->hashcons!=sources->globals)return false;
    return true;
}

static bool prime_replay_sources_current(const PrimeReplaySources *sources) {
    if(!sources->count||!sources->spaces||!sources->reads)return false;
    if(!prime_replay_source_owners_current(sources))return false;
    if(sources->external_target &&
        (sources->external_target->instance_id!=sources->target_instance ||
         sources->external_target->universe!=&sources->library->term_universe))return false;
    for(size_t i=0u;i<sources->count;i++)
        if(!space_read_view_is_current(&sources->reads[i]) ||
            sources->spaces[i]==sources->external_target)return false;
    return true;
}

static void prime_replay_sources_free(PrimeReplaySources *sources) {
    for(size_t i=0u;i<sources->count;i++)space_read_view_release(&sources->reads[i]);
    free(sources->spaces);free(sources->reads);*sources=(PrimeReplaySources){0};
}

/* Release this observer without copying its computation. Real ownership of
 * the handle and quiescent writer state authorize the transition. Incidental
 * receipt handles keep only their control blocks; independent readers keep
 * their own payload leases. The positive limit admits one observer transition,
 * not a traversal of executable worlds or a replay check. */
static CettaContinuationStatus prime_checkpoint_forget_recording(
    Atom *holder, size_t limit, const char **refusal) {
    *refusal = "InspectionLimit";
    if (!limit) return CETTA_CONTINUATION_UNSUPPORTED;
    CettaNativeHandleUse use = {0};
    *refusal = "SourceOwner";
    if (!cetta_native_handle_acquire_owned(g_library_context, "prime-search", holder,
            CETTA_NATIVE_HANDLE_USE_EXCLUSIVE, &use))
        return CETTA_CONTINUATION_DEFERRED;
    PrimeNativeControl *source = cetta_native_handle_use_resource(&use);
    CettaContinuationStatus status = CETTA_CONTINUATION_UNSUPPORTED;
    if (!source || !source->owned_arena) {
        *refusal = "SourceOwner";
    } else if (source->leased || source->active_work || source->driver.running_task ||
            source->history.active || g_prime_eval_stack_driver == &source->driver ||
            g_prime_eval_top_authority == &source->driver) {
        *refusal = "ActiveWriter";
        status = CETTA_CONTINUATION_DEFERRED;
    } else {
        PrimeNativeHistory *history = &source->history;
        PrimeNeedRecording *carrier = history->group;
        if (!history->forgotten) prime_need_recording_demote(carrier);
        free(history->entries);
        *history = (PrimeNativeHistory){.group=carrier, .forgotten=true};
        source->policy.history = (PrimeNativeHistorySpec){0};
        *refusal = NULL;
        status = CETTA_CONTINUATION_READY;
    }
    bool released = cetta_native_handle_use_release(&use);
    assert(released); (void)released;
    return status;
}

static bool prime_replay_checkpoint_current(const PrimeNativeControl *control,Arena *inspection) {
    if(!control||!control->owned_top_episode||control->invalidated||control->failed||
        !space_read_token_is_current(control->source_read)||
        !prime_native_control_query_reads_current(control))return false;
    /* Validation changes only this temporary view's refusal fields. Any
     * diagnostic Atom it allocates belongs to the attempt, never the held
     * accepted checkpoint. The original registry tables stay borrowed here. */
    RegistryReadView view=control->registry_read;
    view.arena=inspection;
    return registry_read_view_validate(&view,control->owned_top_episode->registry);
}

static bool prime_replay_checkpoint_complete(const PrimeNativeControl *control,
    const PrimeReplaySourceScan *scan) {
    if(!control->cost_enabled||control->event_cost.equations.enabled||!control->event_cost.native_enabled||
        !control->event_cost.identity||
        control->event_cost.identity==UINT64_MAX||control->event_cost.compact||control->event_cost.total.overflow||
        control->event_cost.total.trace_incomplete||!control->history.spec.enabled||
        !control->history.group||control->history.omitted||control->history.overflow)
        return false;
    for(size_t i=0u;i<scan->recording_count;i++) {
        PrimeNeedRecordingStatus status=prime_need_recording_status(scan->recordings[i]);
        if(status.incomplete||status.overflow||status.omitted)return false;
    }
    for(size_t i=0u;i<control->history.count;i++)
        if(strcmp(control->history.entries[i].protocol,scan->transaction_target?"Resource":"Need"))return false;
    return true;
}

static void prime_replay_checkpoint_init(PrimeReplayCheckpoint *checkpoint) {
    *checkpoint=(PrimeReplayCheckpoint){.status=CETTA_CONTINUATION_UNSUPPORTED};
    arena_init_detached(&checkpoint->storage);
    arena_set_hashcons(&checkpoint->storage,NULL);
}

static PrimeNativeControl *prime_replay_checkpoint_control(
    CettaLibraryContext *context,const PrimeReplayCheckpoint *checkpoint) {
    return context&&checkpoint->holder?cetta_native_handle_get_owned(
        context,"prime-search",checkpoint->holder):NULL;
}

static void prime_replay_checkpoint_free(CettaLibraryContext *context,
    PrimeReplayCheckpoint *checkpoint) {
    prime_replay_checkpoint_origin_free(checkpoint);
    if(checkpoint->preparation)cetta_prime_checkpoint_copy_free(checkpoint->preparation);
    checkpoint->preparation=NULL;
    uint64_t id=0u;
    if(!checkpoint->exported&&context&&prime_replay_checkpoint_control(context,checkpoint)&&
        cetta_native_handle_arg(checkpoint->holder,"prime-search",&id))
        (void)cetta_native_handle_close(context,"prime-search",id);
    arena_free(&checkpoint->storage);
    *checkpoint=(PrimeReplayCheckpoint){0};
}

static bool prime_replay_pair_add(AtomDeepCopyArenaPair *pairs,size_t *count,
    size_t capacity,const Arena *source,Arena *image) {
    if(!source||!image)return false;
    for(size_t i=0u;i<*count;i++)
        if(pairs[i].source==source)return pairs[i].destination==image;
    if(*count==capacity)return false;
    pairs[(*count)++]=(AtomDeepCopyArenaPair){source,image};return true;
}

/* The immutable selector program may name its source equations. Authenticate
 * the live source domain before following any plan Atom; current query scratch
 * additionally uses the copier's existing held-span check. Effect-cache tokens
 * are scalar historical keys and are never followed by this admission. */
static bool prime_replay_service_domain(const PrimeNativeControl *source,
    const PrimeReplaySources *sources,size_t nodes) {
    const EquationMatchDecisionCache *cache=source->source_services
        ? &source->source_services->selectors : equation_match_cache_current();
    const SpaceExecutionAnalysisCache *effects=source->source_services
        ? source->source_services->effects : space_execution_analysis_cache_current();
    if(source->source_services&&source->source_services->active)return false;
    if(!space_execution_analysis_cache_admitted(effects,nodes))return false;
    for(size_t i=0u;i<EQUATION_MATCH_DECISION_CACHE_SLOTS;i++) {
        cetta_native_cost_charge(CETTA_COST_STORAGE_PROBE,1u);
        const EquationMatchDecisionCacheSlot *slot=cache->slots[i];
        if(!slot)continue;
        const Space *owner=NULL;
        for(size_t j=0u;j<sources->count;j++)
            if(slot->equation_projection.space==sources->spaces[j]){owner=sources->spaces[j];break;}
        if(!owner||!space_equation_token_matches_live_space(slot->equation_projection,owner))return false;
    }
    return true;
}

static bool prime_replay_checkpoint_prepare_profile(CettaLibraryContext *context,Atom *holder,
    const PrimeReplaySources *sources,size_t nodes,size_t bytes,PrimeReplayCheckpoint *out,
    bool transaction,bool source_reserved,bool service_only) {
    if(out->holder||out->preparation){out->refusal="AlreadyPrepared";return false;}
    /* One named checkpoint-preparation invocation. Callee charges remain
     * separate; this is not a count of allocator work or copied bytes. */
    cetta_native_cost_charge(CETTA_COST_CAPTURE,1u);
    PrimeNativeControl *source=cetta_native_handle_get_owned(context,"prime-search",holder);
    PrimeReplaySourceScan scan={0};
    AtomDeepCopyArenaPair *pairs=NULL;PrimeNeedRecordingCopy **records=NULL;
    AtomDeepCopySession *atoms=NULL;
    bool ok=false;
    out->refusal="SourceAdmission";
    if(!source||!prime_replay_sources_current(sources)||
        !prime_replay_checkpoint_current(source,&out->storage)||
        !prime_replay_source_admitted_profile(source,&out->storage,nodes,&scan,&out->refusal,
            transaction,source_reserved,bytes,sources->external_target))goto done;
    if(!prime_replay_checkpoint_complete(source,&scan)) {
        out->refusal="IncompleteRecording";goto done;
    }
    if(!prime_replay_service_domain(source,sources,nodes)) {
        out->refusal="SourceServiceDomain";goto done;
    }
    out->preparation=prime_checkpoint_copy_new_profile(context,holder,&out->status,true,nodes,
        transaction,source_reserved,service_only,bytes);
    if(!out->preparation){out->refusal="CheckpointAdmission";goto done;}
    if(nodes>SIZE_MAX/sizeof(*pairs)||scan.recording_count>SIZE_MAX/sizeof(*records)) {
        out->refusal="ProviderExtent";goto done;
    }
    pairs=nodes?calloc(nodes,sizeof(*pairs)):NULL;
    records=scan.recording_count?calloc(scan.recording_count,sizeof(*records)):NULL;
    if((nodes&&!pairs)||(scan.recording_count&&!records)) {
        out->refusal="Allocation";out->status=CETTA_CONTINUATION_CAPACITY;goto done;
    }
    size_t count=cetta_prime_checkpoint_copy_arena_pair_count(out->preparation);
    if(!count||count>nodes){out->refusal="ProviderExtent";goto done;}
    for(size_t i=0u;i<count;i++)
        if(!cetta_prime_checkpoint_copy_arena_pair_at(out->preparation,i,&pairs[i]))goto done;
    Arena *persistent=source->owned_top_episode->persistent;
    cetta_native_cost_charge(CETTA_COST_STORAGE_PROBE,1u);
    if(persistent!=sources->persistent||
        !prime_replay_pair_add(pairs,&count,nodes,persistent,persistent)) {
        out->refusal="PersistentOwner";goto done;
    }
    /* The source service is independently held, including its private
     * arena nodes. The existing restricted runtime map retains closed
     * nodes by identity and leaves this owner's caches untouched. Global
     * nodes still require the separate transitive table-domain check.
     * The complete replay reader checks retained descendants before
     * authentication. */
    for(size_t i=0u;i<sources->count;i++) {
        const Space *space=sources->spaces[i];
        Arena *owner=space->universe?space->universe->persistent_arena:NULL;
        cetta_native_cost_charge(CETTA_COST_STORAGE_PROBE,1u);
        if(owner!=persistent||
            !prime_replay_pair_add(pairs,&count,nodes,owner,owner)) {
            out->refusal="SourceUniverseOwner";goto done;
        }
    }
    for(size_t i=0u;i<scan.recording_count;i++) {
        records[i]=prime_need_recording_copy_new(scan.recordings[i]);
        if(!records[i]){out->refusal="RecordingOwner";goto done;}
        size_t length=prime_need_recording_copy_pair_count(records[i]);
        for(size_t j=0u;j<length;j++) {
            AtomDeepCopyArenaPair pair;
            if(!prime_need_recording_copy_pair(records[i],j,&pair)||
                !prime_replay_pair_add(pairs,&count,nodes,pair.source,pair.destination)) {
                out->refusal="RecordingMap";goto done;
            }
        }
    }
    atoms=atom_deep_copy_session_new(&out->preparation->image->storage);
    AtomDeepCopyGlobalInspection inspection={0};
    bool globals_ready=atoms&&atom_deep_copy_session_restrict_globals(
        atoms,sources->globals,nodes,&inspection);
    cetta_native_cost_charge(CETTA_COST_INDEX_VISIT,inspection.index_visits);
    cetta_native_cost_charge(CETTA_COST_ANALYSIS_STEP,inspection.analysis_steps);
    if(!globals_ready) {
        out->refusal="GlobalOwnerDomain";goto done;
    }
    /* Map setup itself copies live canonical-cache Atoms. Its first payload
     * read must already be subject to the restrictive global-owner domain. */
    if(!atom_deep_copy_session_set_runtime_arena_map(atoms,pairs,count)) {
        out->refusal="ArenaMap";goto done;
    }
    /* The recording policy uses the admitted arena map. Choose duplication
     * before preparing recording frames or copying parent receipt carriers. */
    if(scan.recording_count && !prime_need_recording_copy_enable(atoms)) {
        out->refusal="RecordingMode";goto done;
    }
    for(size_t i=0u;i<scan.recording_count;i++)
        if(!prime_need_recording_copy_prepare(records[i],atoms)) {
            out->refusal="RecordingCopy";goto done;
        }
    out->status=cetta_prime_checkpoint_copy_prepare(out->preparation,atoms);
    out->refusal=cetta_prime_checkpoint_copy_refusal(out->preparation);
    if(out->status!=CETTA_CONTINUATION_READY)goto done;
    if(!prime_replay_sources_current(sources)||
        !prime_replay_checkpoint_current(source,&out->storage)) {
        out->status=CETTA_CONTINUATION_INVALIDATED;out->refusal="ChangedSource";goto done;
    }
    if(transaction) {
        CettaPrimeCheckpointCopy *copy=out->preparation;
        out->target=(PrimeReplayTargetOrigin){.source=copy->source_target,.image=copy->image_target,
            .before=copy->target_before,.after=copy->target_after,.complete=copy->target_complete};
        out->service_only=service_only;
    }
    bool ambient=out->preparation->ambient_services;
    if((ambient||transaction)&&!prime_replay_checkpoint_origin_check(source,sources,nodes,bytes,
        pairs,count,atoms,out)) {
        out->status=CETTA_CONTINUATION_UNSUPPORTED;
        out->refusal="AmbientSourceMismatch";goto done;
    }
    uint64_t origin=out->preparation->source_handle;
    out->status=cetta_prime_checkpoint_copy_publish(out->preparation,&out->storage,&out->holder);
    if(out->status!=CETTA_CONTINUATION_READY){out->refusal="HolderPublication";goto done;}
    out->ambient_services_captured=ambient;
    out->ambient_source_handle=ambient?origin:0u;
    out->refusal=NULL;ok=true;
done:
    atom_deep_copy_session_free(atoms);
    for(size_t i=0u;records&&i<scan.recording_count;i++)prime_need_recording_copy_free(records[i]);
    free(records);free(pairs);prime_replay_source_free(&scan);
    if(out->preparation) {
        prime_checkpoint_unreserve(out->preparation);
        if(ok) {cetta_prime_checkpoint_copy_free(out->preparation);out->preparation=NULL;}
    }
    if(!ok&&out->status==CETTA_CONTINUATION_READY)out->status=CETTA_CONTINUATION_UNSUPPORTED;
    return ok;
}

static bool prime_replay_checkpoint_prepare(CettaLibraryContext *context,Atom *holder,
    const PrimeReplaySources *sources,size_t nodes,size_t bytes,PrimeReplayCheckpoint *out) {
    return prime_replay_checkpoint_prepare_profile(context,holder,sources,nodes,bytes,out,
        false,false,false);
}
