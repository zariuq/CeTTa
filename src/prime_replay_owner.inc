typedef struct {
    size_t command,phase_count,site_count;
    SpaceReadToken replacement;
    CettaSpaceAgendaEventKind kind;
} PrimeReplayResourceBoundary;
typedef struct {
    CettaSpaceAgendaTranscript *word;
    /* The holder belongs to capsule.storage (original) or frame.entry.storage
     * (attempt). Both enclose this word; resource_free precedes their release. */
    CettaLibraryContext *source_context;
    Atom *source_holder;
    Arena *owner;
    bool borrow_retired;
    SpaceCheckpointBudget budget;
    PrimeReplayComparison origin;
    PrimeReplayPhases sites;
    PrimeReplayResourceBoundary *boundaries;
    size_t event_count,event_capacity,global_reserved;
    uint64_t live_validation_work;
    bool ready,live_validated,site_open;
    CettaSpaceAgendaObservationScope scope;
    const char *refusal;
} PrimeReplayResourceWord;
typedef struct {
    Arena storage;
    PrimeNativeOwnedTopRequest top;
    OutcomeSet pending;
    ResultSet results;
    CettaNativeCostReceipts commands;
    CettaNativeCostLedger physical;
    PrimeReplayCheckpoint entry,post;
    PrimeReplayPhases phases;
    PrimeReplayResourceWord resource;
    CettaCheckpointIssuance begin,end;
    bool owns_commands,accounted,post_ready;
    uint64_t allowance;
    CettaContinuationStatus status;
    bool ready, entered, completed;
} PrimeReplayRequestFrame;

typedef struct {
    PrimeReplayRequestFrame frame;
    PrimeReplayComparison source_read,entry_read,copy_read,post_read,resource_read;
    size_t frame_index;
    uint64_t inspection_requested,inspection_remaining;
    const char *refusal;
    bool accepted;
} PrimeReplayAttempt;

typedef struct {
    CettaLibraryContext *context;
    uint64_t handle;
    bool busy,source_reserved,created_ready,capture_complete,transaction;
    SpaceReadToken target_expected;
    SpaceContentsMovedHook subscriber;
    Arena storage;
    Atom *source_holder;
    EvalOutcome created;
    PrimeReplaySources sources;
    PrimeReplayCheckpoint initial,recording;
    PrimeReplayAttempt *accepted_attempt,*failed_attempt;
    size_t replayed_count;
    bool replay_started;
    CettaNativeCostLedger expense;
    PrimeReplayRequestFrame *frames;
    CettaNativeCostReceipts reservation;
    size_t frame_count,command_used,destination_used;
    size_t frame_limit,command_limit,byte_limit,node_limit;
    uint64_t retained_bytes,guarded_bytes;
    bool storage_within_limit;
    const char *disposition;
    const char *refusal;
} PrimeReplayCapsule;

/* Explicit handle close can destroy a control despite surviving owning
 * Atom aliases. Reauthenticate that resource before reading its Arena header.
 * The current library separately holds the persistent/global service headers.
 * This is the existing serial, quiescent close/use protocol, not a concurrent
 * check/use lifetime acquisition. */
static bool prime_replay_resource_borrow_current(const PrimeReplayResourceWord *word) {
    if(!word->ready||!word->word||word->borrow_retired||!word->source_holder||!word->owner||
        !word->source_context||word->source_context!=g_library_context||
        !word->origin.sources||word->origin.sources->library!=word->source_context||
        !prime_replay_source_owners_current(word->origin.sources))return false;
    PrimeNativeControl *source=cetta_native_handle_get_owned(word->source_context,
        "prime-search",word->source_holder);
    if(!source||source!=word->origin.left)return false;
    const Arena *persistent=word->origin.sources->persistent;
    return cetta_space_agenda_transcript_maps_arena(word->word,&source->storage,word->owner)&&
        cetta_space_agenda_transcript_maps_arena(word->word,persistent,persistent);
}
static bool prime_replay_resource_correspondence_live(const PrimeReplayResourceWord *word) {
    return prime_replay_resource_borrow_current(word)&&
        cetta_space_agenda_transcript_correspondence_live(word->word);
}
/* A raw storage callback may run without the public inspection preflight.
 * It must refuse before scheduling any borrowed source header or pin. */
static bool prime_replay_resource_storage_safe(const PrimeReplayResourceWord *word,
    CettaStorageCensus *census) {
    if(!word->word||word->borrow_retired)return true;
    cetta_storage_probe(census);
    if(prime_replay_resource_borrow_current(word))return true;
    cetta_storage_invalidate(census,"RetiredTransactionSource");return false;
}
static void prime_replay_resource_word_storage(const PrimeReplayResourceWord *word,
    CettaStorageCensus *census) {
    if(prime_replay_resource_storage_safe(word,census))
        cetta_space_agenda_transcript_storage(word->word,census);
}
/* Retirement is a paid mutation before census, never inside its read-only
 * visitor. Finish drops memo keys and source pins, retaining the complete
 * image word, owned Bindings and frame holds until ordinary word disposal. */
static void prime_replay_resource_retire_borrow(PrimeReplayResourceWord *word) {
    if(!word->word||word->borrow_retired)return;
    cetta_native_cost_charge(CETTA_COST_STORAGE_PROBE,1u);
    if(prime_replay_resource_borrow_current(word))return;
    size_t cells=0u,capacity=0u;
    cetta_native_cost_charge(CETTA_COST_STORAGE_PROBE,1u);
    if(!cetta_space_agenda_transcript_counts(word->word,&cells,&capacity)||cells>capacity)return;
    cetta_native_cost_charge(CETTA_COST_ANALYSIS_STEP,(uint64_t)cells);
    cetta_space_agenda_transcript_finish(word->word);
    word->borrow_retired=true;
}
static void prime_replay_capsule_retire_borrows(PrimeReplayCapsule *capsule) {
    if(!capsule->transaction)return;
    for(size_t i=0u;i<capsule->frame_count;i++)
        prime_replay_resource_retire_borrow(&capsule->frames[i].resource);
    if(capsule->accepted_attempt)
        prime_replay_resource_retire_borrow(&capsule->accepted_attempt->frame.resource);
    if(capsule->failed_attempt)
        prime_replay_resource_retire_borrow(&capsule->failed_attempt->frame.resource);
}
static bool prime_replay_capsule_borrows_storage_safe(const PrimeReplayCapsule *capsule,
    CettaStorageCensus *census) {
    if(!capsule->transaction)return true;
    for(size_t i=0u;i<capsule->frame_count;i++)
        if(!prime_replay_resource_storage_safe(&capsule->frames[i].resource,census))return false;
    return (!capsule->accepted_attempt||
        prime_replay_resource_storage_safe(&capsule->accepted_attempt->frame.resource,census))&&
        (!capsule->failed_attempt||
        prime_replay_resource_storage_safe(&capsule->failed_attempt->frame.resource,census));
}

static void prime_replay_resource_free(PrimeReplayResourceWord *word) {
    if(!word->ready)return;
    assert(!word->scope.entered);
    cetta_space_agenda_transcript_free(word->word);
    prime_replay_comparison_free(&word->origin);
    prime_replay_phases_free(&word->sites);
    free(word->boundaries);*word=(PrimeReplayResourceWord){0};
}
static void prime_replay_frame_free(CettaLibraryContext *context,PrimeReplayRequestFrame *frame) {
    if(!frame->ready)return;
    prime_replay_resource_free(&frame->resource);
    outcome_set_free(&frame->pending);
    (void)prime_native_owned_top_dispose(&frame->top);
    result_set_free(&frame->results);
    cetta_native_cost_free(&frame->physical);
    prime_replay_phases_free(&frame->phases);
    if(frame->owns_commands)cetta_native_cost_receipts_free(&frame->commands);
    prime_replay_checkpoint_free(context,&frame->post);
    prime_replay_checkpoint_free(context,&frame->entry);
    arena_free(&frame->storage);
    *frame=(PrimeReplayRequestFrame){0};
}

static PrimeNativeControl *prime_replay_original(PrimeReplayCapsule *capsule) {
    return capsule->context&&capsule->source_holder?cetta_native_handle_get_owned(
        capsule->context,"prime-search",capsule->source_holder):NULL;
}

static void prime_replay_attempt_free(CettaLibraryContext *,PrimeReplayAttempt *);
static bool prime_replay_capsule_size(PrimeReplayCapsule *);
#include "prime_replay_join_storage.inc"

static void prime_replay_capsule_destroy(void *raw) {
    PrimeReplayCapsule *capsule=raw;
    if(!capsule)return;
    /* An owning Atom can have been detached by explicit close. Reauthenticate
     * before releasing a reservation; never use a cached control pointer. */
    PrimeNativeControl *source=prime_replay_original(capsule);
    if(source&&capsule->source_reserved)source->leased=false;
    capsule->source_reserved=false;
    for(size_t i=0u;i<capsule->frame_count;i++)prime_replay_frame_free(capsule->context,&capsule->frames[i]);
    prime_replay_attempt_free(capsule->context,capsule->failed_attempt);
    prime_replay_attempt_free(capsule->context,capsule->accepted_attempt);
    free(capsule->frames);
    cetta_native_cost_receipts_free(&capsule->reservation);
    prime_replay_checkpoint_free(capsule->context,&capsule->recording);
    prime_replay_checkpoint_free(capsule->context,&capsule->initial);
    if(capsule->created_ready)eval_outcome_free(&capsule->created);
    prime_replay_sources_free(&capsule->sources);
    cetta_native_cost_free(&capsule->expense);
    arena_free(&capsule->storage);
    free(capsule);
}

static void prime_replay_capsule_storage(const void *raw,CettaStorageCensus *census) {
    const PrimeReplayCapsule *capsule=raw;
    if(!cetta_storage_object(census,capsule,sizeof(*capsule),CETTA_STORAGE_BLOCK))return;
    if(!prime_replay_capsule_borrows_storage_safe(capsule,census))return;
    arena_storage_census(&capsule->storage,census);
    arena_storage_census(&capsule->initial.storage,census);
    arena_storage_census(&capsule->recording.storage,census);
    prime_replay_checkpoint_origin_storage(&capsule->initial,census);
    prime_replay_checkpoint_origin_storage(&capsule->recording,census);
    if(capsule->initial.preparation)prime_checkpoint_storage(capsule->initial.preparation,census);
    if(capsule->recording.preparation)prime_checkpoint_storage(capsule->recording.preparation,census);
    cetta_storage_array(census,capsule->expense.events,capsule->expense.capacity,
        sizeof(*capsule->expense.events),0u);
    cetta_storage_array(census,capsule->frames,capsule->frame_limit,sizeof(*capsule->frames),0u);
    cetta_storage_array(census,capsule->reservation.commands,
        capsule->reservation.command_capacity,sizeof(*capsule->reservation.commands),0u);
    cetta_storage_array(census,capsule->reservation.destinations,
        capsule->reservation.destination_capacity,sizeof(*capsule->reservation.destinations),0u);
    for(size_t i=0u;i<capsule->frame_count;i++)prime_replay_frame_storage(&capsule->frames[i],census);
    prime_replay_attempt_storage(capsule->accepted_attempt,census);
    prime_replay_attempt_storage(capsule->failed_attempt,census);
    cetta_storage_array(census,capsule->sources.spaces,capsule->sources.capacity,
        sizeof(*capsule->sources.spaces),0u);
    cetta_storage_array(census,capsule->sources.reads,capsule->sources.capacity,
        sizeof(*capsule->sources.reads),0u);
    for(size_t i=0u;i<capsule->sources.count;i++)
        space_read_view_storage_census(&capsule->sources.reads[i],census);
    if(capsule->sources.count) {
        if(!prime_replay_sources_current(&capsule->sources))
            cetta_storage_invalidate(census,"ChangedSourceOwner");
        else
            arena_storage_census(capsule->sources.persistent,census);
    }
    if(capsule->created_ready&&capsule->created.prime_observation) {
        const CettaPrimeObservationReturn *created=capsule->created.prime_observation;
        cetta_storage_object(census,created,sizeof(*created),CETTA_STORAGE_BLOCK);
        arena_storage_census(&created->holders,census);
        outcome_set_storage_census(&created->pending,census);
        prime_native_top_episode_storage(created->top.episode,census);
        if(created->top.episode&&!created->top.episode->ready&&
            created->top.episode->request_storage_ready)
            arena_storage_census(&created->top.episode->request_storage,census);
        if(created->top.owned.unpublished)
            prime_native_control_storage_census(created->top.owned.unpublished,census);
    }
}

static bool prime_replay_uint(Atom *atom,uint64_t *out) {
    if(!atom||atom->kind!=ATOM_GROUNDED||atom->ground.gkind!=GV_INT||atom->ground.ival<0)
        return false;
    *out=(uint64_t)atom->ground.ival;return true;
}

static bool prime_replay_limits(Atom *atom,size_t *frames,size_t *commands,size_t *bytes) {
    uint64_t f,c,b;
    if(!atom||atom->kind!=ATOM_EXPR||atom->expr.len!=4u||
        !atom_is_symbol(atom->expr.elems[0],"ReplayLimits")||
        !prime_replay_uint(expr_arg(atom,0u),&f)||!prime_replay_uint(expr_arg(atom,1u),&c)||
        !prime_replay_uint(expr_arg(atom,2u),&b)||f>SIZE_MAX||c>SIZE_MAX||b>SIZE_MAX)
        return false;
    *frames=(size_t)f;*commands=(size_t)c;*bytes=(size_t)b;return true;
}

static bool prime_replay_request_policy(Space *space,Arena *arena,Atom *request,bool *transaction) {
    *transaction=false;
    if(!request||request->kind!=ATOM_EXPR||request->expr.len!=3u||
        !atom_is_symbol_id(request->expr.elems[0],prime_need_symbols()->cost_run)||
        eval_equations_may_match_known_head(space,prime_need_symbols()->cost_run))return false;
    CettaSearchPolicySpec policy={.native_control=true,.age_share=8u,
        .work_budget=UINT64_MAX,.retain=true,.costs=true,.all=true};
    if(parse_search_policy_atom(arena,expr_arg(request,0u),&policy,NULL)!=CETTA_SEARCH_POLICY_PARSE_OK||
        !policy.native_control||policy.work_budget!=0u||policy.cost_compact||policy.cost_equations)return false;
    policy.costs=policy.retain=policy.all=true;
    if(prime_native_transaction_policy(&policy)) {
        *transaction=true;
        return resolve_space(g_registry,expr_arg(request,1u))!=NULL;
    }
    return prime_native_owned_cost_policy(&policy)&&policy.history.enabled&&
        expr_arg(request,1u)&&expr_arg(request,1u)->kind==ATOM_EXPR&&
        expr_arg(request,1u)->expr.len;
}

static bool prime_replay_request_source(PrimeReplayCapsule *capsule,Space *space,Atom *request) {
    PrimeReplaySourceScan scan={.space=space,.arena=&capsule->storage,.capacity=capsule->node_limit,
        .transaction_target=capsule->transaction?capsule->sources.external_target:NULL};
    if(scan.capacity>SIZE_MAX/sizeof(*scan.roots))return false;
    scan.roots=scan.capacity?calloc(scan.capacity,sizeof(*scan.roots)):NULL;
    if(scan.capacity&&!scan.roots)return false;
    bool ok=prime_replay_source_push(&scan,expr_arg(request,1u),capsule->transaction);
    while(ok&&scan.at<scan.length)ok=prime_replay_source_atom(&scan,scan.roots[scan.at++]);
    if(!ok)capsule->refusal=scan.refusal?scan.refusal:"RequestSource";
    prime_replay_source_free(&scan);return ok;
}

static bool prime_replay_capsule_size(PrimeReplayCapsule *capsule) {
    prime_replay_capsule_retire_borrows(capsule);
    CettaStorageCensus census={0};
    prime_replay_capsule_storage(capsule,&census);
    cetta_storage_finish(&census);
    uint64_t probes=census.probes;
    /* The query reports its preceding storage, as the existing storage
     * readout does. Reserve the maximum event-array growth for this probe
     * command and the following read; they are real retained work too. */
    size_t capacity=capsule->expense.capacity;
    bool extent=capsule->expense.length<=SIZE_MAX-2u;
    size_t wanted=extent?capsule->expense.length+2u:0u;
    while(extent&&capacity<wanted) {
        size_t next=capacity?capacity*2u:32u;
        if(next<capacity||next>SIZE_MAX/sizeof(*capsule->expense.events))extent=false;
        else capacity=next;
    }
    uint64_t extra=extent?(uint64_t)(capacity-capsule->expense.capacity)*sizeof(*capsule->expense.events):UINT64_MAX;
    bool sum=extra<=UINT64_MAX-census.bytes;
    capsule->retained_bytes=census.bytes;
    capsule->guarded_bytes=sum?census.bytes+extra:UINT64_MAX;
    bool ok=extent&&sum&&!census.invalidated&&!census.incomplete&&!census.overflow&&
        capsule->guarded_bytes<=capsule->byte_limit;
    cetta_storage_census_free(&census);
    cetta_native_cost_charge(CETTA_COST_STORAGE_PROBE,probes);
    ok=ok&&!capsule->expense.total.overflow&&!capsule->expense.total.trace_incomplete;
    capsule->storage_within_limit=ok;
    if(!ok)capsule->refusal="RetainedStorageLimit";
    return ok;
}

#include "prime_replay_owner_join.inc"

static bool prime_replay_capsule_capture(PrimeReplayCapsule *capsule,Space *space,
    Atom *input) {
    if(!capsule->expense.identity||capsule->expense.total.trace_incomplete) {
        capsule->refusal="ExpenseIdentity";return false;
    }
    if(!capsule->frame_limit||!capsule->command_limit||
        capsule->frame_limit>SIZE_MAX/sizeof(*capsule->frames)||
        capsule->command_limit>SIZE_MAX/sizeof(*capsule->reservation.commands)) {
        capsule->refusal="CapsuleCapacity";return false;
    }
    size_t frame_bytes=capsule->frame_limit*sizeof(*capsule->frames);
    size_t command_bytes=capsule->command_limit*sizeof(*capsule->reservation.commands);
    if(frame_bytes>capsule->byte_limit||command_bytes>capsule->byte_limit-frame_bytes) {
        capsule->refusal="CapsuleCapacity";return false;
    }
    size_t destinations=(capsule->byte_limit-frame_bytes-command_bytes)/
        (4u*sizeof(*capsule->reservation.destinations));
    if(capsule->command_limit<=SIZE_MAX/8u&&destinations>capsule->command_limit*8u)
        destinations=capsule->command_limit*8u;
    /* The same fixed buffers hold every recorded request. Truncation never
     * manufactures a complete frame or permits another producer request. */
    capsule->frames=calloc(capsule->frame_limit,sizeof(*capsule->frames));
    if(!capsule->frames||!destinations||!cetta_native_cost_receipts_init(
        &capsule->reservation,capsule->command_limit,destinations)) {
        capsule->refusal="CapsuleCapacity";return false;
    }
    Atom *holder=input;
    if(input&&input->kind==ATOM_EXPR&&input->expr.len==2u&&
        atom_is_symbol_id(input->expr.elems[0],g_builtin_syms.quote)) {
        Atom *request=expr_arg(input,0u);
        capsule->disposition="RequestPreparation";
        if(!prime_replay_request_policy(space,&capsule->storage,request,&capsule->transaction)) {
            capsule->refusal="ZeroInitialOwnedPolicy";return false;
        }
        if(!prime_replay_sources_capture(&capsule->sources,space,capsule->node_limit)) {
            capsule->refusal="SourceView";return false;
        }
        if(capsule->transaction) {
            Space *target=resolve_space(g_registry,expr_arg(request,1u));
            if(!target||target==space){capsule->refusal="TargetAliasesImmutableSource";return false;}
            capsule->sources.external_target=target;
            capsule->sources.target_instance=target->instance_id;
            capsule->target_expected=space_read_token(target);
            capsule->subscriber=space_contents_moved_hook();
            SpaceCheckpointBudget budget={.work_limit=capsule->node_limit,.byte_limit=capsule->byte_limit};
            if(!prime_replay_sources_current(&capsule->sources)||
                !space_checkpoint_native_admitted(target,&budget)||
                !prime_replay_transaction_authority(capsule,capsule->target_expected,(SpaceReadToken){0})) {
                capsule->refusal=budget.refusal?budget.refusal:"TransactionTargetAuthority";return false;
            }
        }
        if(!prime_replay_request_source(capsule,space,request)||
            !prime_replay_sources_current(&capsule->sources))return false;
        eval_outcome_init(&capsule->created);capsule->created_ready=true;
        capsule->created.depth_fuel=true;
        CettaPrimeObservationReturn *created=prime_native_registry_observation_prepare_profile(
            space,&capsule->storage,g_eval_fallback_universe.persistent_arena,g_registry,
            request,&capsule->created,NULL,NULL,true,capsule->transaction);
        if(!created) {capsule->refusal="OwnedRequestAdmission";return false;}
        if(created->top.owned.status!=CETTA_CONTINUATION_READY) {
            capsule->refusal="PartialInitialization";return false;
        }
        /* No caller collection is allowed to evacuate this independent
         * owned episode. The original registry/top prelude still executes. */
        eval_gc_external_owner_enter();
        prime_native_registry_observation_enter(&capsule->created);
        eval_gc_external_owner_leave();
        if(!created->top.committed||!created->top.owned.initialized||
            created->top.owned.pending->len||!created->top.owned.holder) {
            capsule->refusal="PendingInitialization";return false;
        }
        holder=created->top.owned.holder;
    }
    PrimeNativeControl *source=cetta_native_handle_get_owned(capsule->context,"prime-search",holder);
    if(!source||!source->owned_top_episode||source->space!=space||
        (source->space_agenda!=NULL)!=capsule->transaction||
        (capsule->transaction&&(source->source_services||
            cetta_space_agenda_checkpoint_target(source->space_agenda)!=capsule->sources.external_target||
            cetta_space_agenda_checkpoint_owns_target(source->space_agenda)))) {
        capsule->refusal="OwnedSource";return false;
    }
    uint64_t id=0u;
    if(!cetta_native_handle_arg(holder,"prime-search",&id))return false;
    capsule->source_holder=cetta_native_handle_owned_atom(capsule->context,&capsule->storage,"prime-search",id);
    if(!capsule->source_holder){capsule->refusal="SourceHolder";return false;}
    if(!capsule->sources.count&&
        !prime_replay_sources_capture(&capsule->sources,space,capsule->node_limit)) {
        capsule->refusal="SourceView";return false;
    }
    capsule->disposition="CheckpointPreparation";
    if(!prime_replay_checkpoint_prepare_profile(capsule->context,capsule->source_holder,
        &capsule->sources,capsule->node_limit,capsule->byte_limit,&capsule->initial,
        capsule->transaction,false,false)) {
        capsule->refusal=capsule->initial.refusal;return false;
    }
    if(!prime_replay_checkpoint_prepare_profile(capsule->context,
        capsule->transaction?capsule->source_holder:capsule->initial.holder,
        &capsule->sources,capsule->node_limit,capsule->byte_limit,&capsule->recording,
        capsule->transaction,false,capsule->transaction)) {
        capsule->refusal=capsule->recording.refusal;return false;
    }
    /* The original is reserved only after both independent owners exist.
     * Explicit close can still detach it; every later access reauthenticates. */
    source=prime_replay_original(capsule);
    if(!source||source->leased){capsule->refusal="SourceChanged";return false;}
    source->leased=true;capsule->source_reserved=true;
    capsule->disposition="Captured";
    capsule->capture_complete=prime_replay_capsule_size(capsule);
    return capsule->capture_complete;
}

static Atom *prime_replay_resource_readout(const PrimeReplayRequestFrame *frame,
    const PrimeReplayAttempt *attempt,Arena *arena) {
    const PrimeReplayResourceWord *w=&frame->resource;
    if(!w->ready)return atom_symbol(arena,"NotStarted");
    CettaStorageCensus census={0};
    prime_replay_resource_word_storage(w,&census);
    cetta_storage_finish(&census);
    size_t bytes=census.bytes,cells=0u,capacity=0u;
    bool measured=!census.invalidated&&!census.incomplete&&!census.overflow;
    uint64_t probes=census.probes;
    cetta_storage_census_free(&census);
    cetta_native_cost_charge(CETTA_COST_STORAGE_PROBE,probes);
    bool counts=cetta_space_agenda_transcript_counts(w->word,&cells,&capacity);
    cetta_native_cost_charge(CETTA_COST_STORAGE_PROBE,1u);
    bool correspondence=prime_replay_resource_correspondence_live(w);
    const char *refusal=w->refusal?w->refusal:w->scope.refusal?w->scope.refusal:
        w->origin.refusal?w->origin.refusal:cetta_space_agenda_transcript_refusal(w->word);
#define TR_N(name,value) prime_native_history_field(arena,name,prime_native_occurrence_atom(arena,(value)))
#define TR_B(name,value) prime_native_history_field(arena,name,atom_symbol(arena,(value)?"True":"False"))
    Atom *fields[]={atom_symbol(arena,"TransactionWord"),
        prime_native_history_field(arena,"profile",atom_symbol(arena,"TransactionBoundaryRequestsV1")),
        TR_N("allowance",frame->allowance),TR_B("entered",frame->entered),
        TR_N("events",cetta_space_agenda_transcript_events(w->word)),
        TR_N("validated-events",w->event_count),TR_N("observed-events",w->scope.events),
        TR_N("commits",cetta_space_agenda_transcript_commits(w->word)),
        TR_N("observed-commits",w->scope.commits),
        TR_B("complete",cetta_space_agenda_transcript_complete(w->word)),
        TR_B("live-validated",w->live_validated&&w->origin.matched),
        TR_B("correspondence-live",correspondence),
        TR_B("return-committed",frame->top.committed),TR_B("request-complete",frame->completed),
        TR_B("recorded-post-checkpoint",frame->post_ready),
        TR_B("attempt-accepted",attempt&&attempt->accepted),
        TR_N("commands",frame->commands.command_count),TR_N("required-sites",w->sites.count),
        TR_B("cell-counts-available",counts),TR_N("cells",cells),TR_N("cell-capacity",capacity),
        TR_B("storage-complete",measured),TR_N("retained-word-bytes",bytes),
        TR_N("direct-retained-bytes",w->budget.retained_bytes),
        TR_N("allocation-attempts",w->budget.allocation_attempts),
        TR_N("attempted-allocation-bytes",w->budget.attempted_bytes),
        TR_N("word-work",w->budget.work),TR_N("live-validation-work",w->live_validation_work),
        TR_N("origin-reads",w->origin.reads),TR_N("origin-probes",w->origin.probes),
        TR_N("origin-services",w->origin.services),TR_N("physical-reads",w->origin.physical_count),
        TR_N("encoded-physical-runs",w->origin.physical_length),
        prime_native_history_field(arena,"inspection-scope",atom_symbol(arena,
            attempt?"ComparisonsAndLiveWordV1":"RecordingPreparationV1")),
        prime_native_history_field(arena,"preparation-scope",atom_symbol(arena,
            "SeparatelyBoundedPaidPreparation")),
        TR_N("inspection-requested",attempt?attempt->inspection_requested:0u),
        TR_N("inspection-remaining",attempt?attempt->inspection_remaining:0u),
        prime_native_history_field(arena,"refusal",atom_symbol(arena,refusal?refusal:"None"))};
#undef TR_N
#undef TR_B
    return atom_expr(arena,fields,sizeof(fields)/sizeof(*fields));
}

static Atom *prime_replay_capsule_read(PrimeReplayCapsule *capsule,Arena *arena) {
    if(capsule->capture_complete&&!prime_replay_capsule_size(capsule))
        capsule->disposition="StorageRefused";
    PrimeNativeControl *source=prime_replay_original(capsule);
    if(!source&&capsule->source_holder) {
        capsule->disposition="Invalidated";capsule->refusal="SourceClosed";
    } else if(capsule->sources.count&&!prime_replay_sources_current(&capsule->sources)) {
        capsule->disposition="Invalidated";capsule->refusal="ChangedSource";
    } else if(capsule->transaction&&!space_read_token_is_current(capsule->target_expected)) {
        capsule->disposition="Invalidated";capsule->refusal="ChangedExternalTarget";
    } else if(source&&!prime_replay_checkpoint_current(source,arena)) {
        capsule->disposition="Invalidated";capsule->refusal="ChangedRegistryOrQuery";
    }
    PrimeReplayCheckpoint *accepted_checkpoint=prime_replay_accepted_checkpoint(capsule);
    PrimeNativeControl *accepted=prime_replay_checkpoint_control(capsule->context,accepted_checkpoint);
    PrimeNativeControl *recording=prime_replay_checkpoint_control(capsule->context,&capsule->recording);
    Atom *holder=cetta_native_handle_owned_atom(capsule->context,arena,"prime-replay",capsule->handle);
    if(!holder)return NULL;
    Atom *accepted_holder=prime_replay_checkpoint_export(capsule->context,accepted_checkpoint,arena);
    if(!accepted_holder)return NULL;
    Atom *created=atom_symbol(arena,"None");
    Atom *initial_state=atom_symbol(arena,"NotStarted");
    if(capsule->created_ready&&capsule->created.results.len==1u)
        created=prime_native_history_quote(arena,capsule->created.results.items[0]);
    if(capsule->created_ready) {
        const CettaPrimeObservationReturn *owned=capsule->created.prime_observation;
        const PrimeNativeOwnedTopRequest *top=owned?&owned->top:NULL;
        Atom *state[]={atom_symbol(arena,"InitialRequest"),
            prime_native_history_field(arena,"entered",atom_symbol(arena,owned&&owned->entered?"True":"False")),
            prime_native_history_field(arena,"started",atom_symbol(arena,top&&top->owned.started?"True":"False")),
            prime_native_history_field(arena,"initialized",atom_symbol(arena,top&&top->owned.initialized?"True":"False")),
            prime_native_history_field(arena,"unpublished",atom_symbol(arena,top&&top->owned.unpublished?"True":"False")),
            prime_native_history_field(arena,"committed",atom_symbol(arena,top&&top->committed?"True":"False")),
            prime_native_history_field(arena,"pending",prime_native_occurrence_atom(arena,top&&top->owned.pending?top->owned.pending->len:0u)),
            prime_native_history_field(arena,"status",atom_int(arena,top?(int)top->owned.status:(int)CETTA_CONTINUATION_UNSUPPORTED)),
            prime_native_history_field(arena,"completion",atom_int(arena,(int)capsule->created.completion)),
            prime_native_history_field(arena,"steps",prime_native_occurrence_atom(arena,capsule->created.steps_spent))};
        initial_state=atom_expr(arena,state,sizeof(state)/sizeof(*state));
    }
    Atom *last=atom_symbol(arena,"None");
    if(capsule->frame_count) {
        const PrimeReplayRequestFrame *frame=&capsule->frames[capsule->frame_count-1u];
        Atom *value=frame->results.len==1u?prime_native_history_quote(arena,frame->results.items[0]):
            atom_symbol(arena,"Pending");
        Atom *parts[]={atom_symbol(arena,"RecordedRequest"),
            prime_native_history_field(arena,"allowance",prime_native_occurrence_atom(arena,frame->allowance)),
            prime_native_history_field(arena,"entered",atom_symbol(arena,frame->entered?"True":"False")),
            prime_native_history_field(arena,"committed",atom_symbol(arena,frame->top.committed?"True":"False")),
            prime_native_history_field(arena,"pending",prime_native_occurrence_atom(arena,frame->pending.len)),
            prime_native_history_field(arena,"commands",prime_native_occurrence_atom(arena,frame->commands.command_count)),
            prime_native_history_field(arena,"commands-complete",atom_symbol(arena,
                cetta_native_cost_receipts_complete(&frame->commands)?"True":"False")),
            prime_native_history_field(arena,"observation",value),NULL};
        size_t length=sizeof(parts)/sizeof(*parts)-1u;
        if(capsule->transaction)parts[length++]=prime_native_history_field(arena,
            "transaction",prime_replay_resource_readout(frame,NULL,arena));
        last=atom_expr(arena,parts,length);
    }
    Atom *failed=atom_symbol(arena,"None");
    if(capsule->failed_attempt) {
        PrimeReplayAttempt *attempt=capsule->failed_attempt;
        const PrimeReplayRequestFrame *f=&attempt->frame;
        const PrimeReplayComparison *reads[]={&attempt->source_read,&attempt->entry_read,
            &attempt->copy_read,&attempt->post_read,&attempt->resource_read,&f->resource.origin};
        const char *field="None",*reason=attempt->refusal?attempt->refusal:"None";
        for(size_t i=0u;i<sizeof(reads)/sizeof(*reads);i++)if(reads[i]->refusal) {
            field=reads[i]->field?reads[i]->field:"None";reason=reads[i]->refusal;break;
        }
        if(capsule->transaction&&!strcmp(field,"None")&&f->resource.refusal) {
            field="transaction.live";reason=f->resource.refusal;
        }
        Atom *parts[]={atom_symbol(arena,"ReplayAttempt"),
            prime_native_history_field(arena,"frame",prime_native_occurrence_atom(arena,attempt->frame_index)),
            prime_native_history_field(arena,"allowance",prime_native_occurrence_atom(arena,f->allowance)),
            prime_native_history_field(arena,"entered",atom_symbol(arena,f->entered?"True":"False")),
            prime_native_history_field(arena,"committed",atom_symbol(arena,f->top.committed?"True":"False")),
            prime_native_history_field(arena,"pending",prime_native_occurrence_atom(arena,f->pending.len)),
            prime_native_history_field(arena,"status",atom_int(arena,f->status)),
            prime_native_history_field(arena,"observation",f->results.len==1u?
                prime_native_history_quote(arena,f->results.items[0]):atom_symbol(arena,"Pending")),
            prime_native_history_field(arena,"inspection-requested",prime_native_occurrence_atom(arena,attempt->inspection_requested)),
            prime_native_history_field(arena,"inspection-remaining",prime_native_occurrence_atom(arena,attempt->inspection_remaining)),
            prime_native_history_field(arena,"field",atom_string(arena,field)),
            prime_native_history_field(arena,"reason",atom_symbol(arena,reason)),
            prime_native_history_field(arena,"provider-owner",atom_symbol(arena,
                prime_replay_checkpoint_control(capsule->context,&f->entry)?"Held":"Absent")),NULL};
        size_t length=sizeof(parts)/sizeof(*parts)-1u;
        if(capsule->transaction)parts[length++]=prime_native_history_field(arena,
            "transaction",prime_replay_resource_readout(f,attempt,arena));
        failed=atom_expr(arena,parts,length);
    }
    Atom *accepted_transaction=atom_symbol(arena,"None");
    if(capsule->transaction&&capsule->accepted_attempt)
        accepted_transaction=prime_replay_resource_readout(&capsule->accepted_attempt->frame,
            capsule->accepted_attempt,arena);
    /* Transaction projections incurred additional paid storage inspection.
     * Recheck the retained expense before publishing authentication. */
    if(capsule->transaction&&capsule->capture_complete&&!prime_replay_capsule_size(capsule))
        capsule->disposition="StorageRefused";
    bool authenticated=capsule->replayed_count&&capsule->replayed_count==capsule->frame_count&&
        !capsule->failed_attempt&&!capsule->refusal&&capsule->storage_within_limit&&source&&accepted;
    Atom *fields[]={atom_symbol(arena,"ReplayCheckpoint"),
        prime_native_history_field(arena,"capsule",holder),
        prime_native_history_field(arena,"profile",atom_symbol(arena,capsule->transaction?
            "TransactionBoundaryRequestsV1":"NativePureCheckpointV1")),
        prime_native_history_field(arena,"status",atom_symbol(arena,capsule->disposition)),
        prime_native_history_field(arena,"refusal",atom_symbol(arena,capsule->refusal?capsule->refusal:"None")),
        prime_native_history_field(arena,"source-correspondence",atom_symbol(arena,"Unchecked")),
        prime_native_history_field(arena,"replay-authenticated",atom_symbol(arena,authenticated?"True":"False")),
        prime_native_history_field(arena,"accepted-owner",atom_symbol(arena,accepted?"Held":"Absent")),
        prime_native_history_field(arena,"accepted-control",accepted_holder),
        prime_native_history_field(arena,"accepted-requests",prime_native_occurrence_atom(arena,capsule->replayed_count)),
        prime_native_history_field(arena,"unconsumed-requests",prime_native_occurrence_atom(arena,capsule->frame_count-capsule->replayed_count)),
        prime_native_history_field(arena,"failed-attempt",failed),
        prime_native_history_field(arena,"recording-owner",atom_symbol(arena,recording?"Held":"Absent")),
        prime_native_history_field(arena,"source-reserved",atom_symbol(arena,source&&capsule->source_reserved?"True":"False")),
        prime_native_history_field(arena,"initial-observation",created),
        prime_native_history_field(arena,"initial-state",initial_state),
        prime_native_history_field(arena,"recorded-requests",prime_native_occurrence_atom(arena,capsule->frame_count)),
        prime_native_history_field(arena,"last-request",last),
        prime_native_history_field(arena,"retained-bytes-before-inspection",prime_native_occurrence_atom(arena,capsule->retained_bytes)),
        prime_native_history_field(arena,"guarded-bytes",prime_native_occurrence_atom(arena,capsule->guarded_bytes)),
        prime_native_history_field(arena,"byte-limit",prime_native_occurrence_atom(arena,capsule->byte_limit)),
        prime_native_history_field(arena,"storage-within-limit",atom_symbol(arena,capsule->storage_within_limit?"True":"False")),
        prime_native_history_field(arena,"expense",prime_cost_ledger_read_atom(&capsule->expense,0u,arena,false)),
        NULL,NULL};
    size_t length=sizeof(fields)/sizeof(*fields)-2u;
    if(capsule->transaction) {
        fields[length++]=prime_native_history_field(arena,"accepted-transaction",accepted_transaction);
        fields[length++]=prime_native_history_field(arena,"recording-service",atom_symbol(arena,
            recording&&capsule->recording.service_only?"HeldUnpolled":"Absent"));
    }
    Atom *result=atom_expr(arena,fields,length);
    if(!result)return NULL;
    /* Only a complete returned record publishes these receiving-arena edges.
     * A refused readout keeps the pre-admitted capsule holder in the caller's
     * output occurrence and leaves each internal owner available for retry. */
    if(accepted)accepted_checkpoint->exported=true;
    if(!capsule->transaction&&capsule->frame_count&&capsule->frames[capsule->frame_count-1u].results.len==1u)
        capsule->recording.exported=true;
    if(capsule->failed_attempt&&capsule->failed_attempt->frame.results.len==1u)
        capsule->failed_attempt->frame.entry.exported=true;
    return result;
}

static bool prime_replay_capsule_record(PrimeReplayCapsule *capsule,uint64_t allowance) {
    if(!capsule->capture_complete){capsule->refusal="UnadmittedCapture";return false;}
    if(capsule->replay_started){capsule->refusal="ReplayAlreadyStarted";return false;}
    if(!prime_replay_capsule_size(capsule)){capsule->disposition="RecordRefused";return false;}
    PrimeNativeControl *original=prime_replay_original(capsule);
    if(!original||!capsule->source_reserved||!original->leased) {
        capsule->refusal="SourceClosedOrUnreserved";return false;
    }
    PrimeNativeControl *auxiliary=prime_replay_checkpoint_control(capsule->context,&capsule->recording);
    PrimeNativeControl *control=capsule->transaction?original:auxiliary;
    if(capsule->transaction&&(!space_read_token_is_current(capsule->target_expected)||
        !auxiliary||!capsule->recording.service_only||original->source_services||
        !auxiliary->source_services||auxiliary->source_services->active)) {
        capsule->refusal="ChangedRecordingTargetOrService";return false;
    }
    if(!control||!prime_replay_sources_current(&capsule->sources)||
        !prime_replay_checkpoint_current(control,&capsule->storage)) {
        capsule->refusal="ChangedSource";return false;
    }
    if(capsule->frame_count&&(!capsule->frames[capsule->frame_count-1u].completed||
        !capsule->frames[capsule->frame_count-1u].post_ready)) {
        capsule->refusal="UncommittedRequest";return false;
    }
    if(capsule->frame_count==capsule->frame_limit||
        capsule->command_used==capsule->reservation.command_capacity||
        capsule->destination_used==capsule->reservation.destination_capacity) {
        capsule->refusal="RecordCapacity";return false;
    }
    PrimeNativeServiceScope services={0};
    if(capsule->transaction&&!prime_native_service_enter(auxiliary->source_services,&services)) {
        capsule->refusal="RecordingServiceAdmission";return false;
    }
    Atom *holder=capsule->transaction?capsule->source_holder:capsule->recording.holder;
    PrimeReplayRequestFrame *frame=&capsule->frames[capsule->frame_count++];
    CettaNativeCostReceipts commands={
        .commands=capsule->reservation.commands+capsule->command_used,
        .command_capacity=capsule->reservation.command_capacity-capsule->command_used,
        .destinations=capsule->reservation.destinations+capsule->destination_used,
        .destination_capacity=capsule->reservation.destination_capacity-capsule->destination_used};
    if(!prime_replay_frame_begin(frame,allowance,commands,false)) {
        capsule->refusal="RecordInitialization";goto incomplete;
    }
    if(!prime_replay_checkpoint_prepare_profile(capsule->context,holder,
        &capsule->sources,capsule->node_limit,capsule->byte_limit,&frame->entry,
        capsule->transaction,capsule->transaction,false)) {
        capsule->refusal="RecordEntryPreparation";goto incomplete;
    }
    if(!prime_replay_capsule_size(capsule))goto incomplete;
    bool completed=prime_replay_frame_execute(capsule,frame,holder);
    if(capsule->transaction&&frame->entered)
        capsule->target_expected=space_read_token(capsule->sources.external_target);
    capsule->command_used+=frame->commands.command_count;
    capsule->destination_used+=frame->commands.destination_count;
    if(!completed){capsule->refusal=capsule->transaction?"RecordedRequestIncomplete":"OwnedUncommittedRequest";goto incomplete;}
    if(!prime_replay_capsule_size(capsule))goto incomplete;
    frame->post_ready=prime_replay_checkpoint_prepare_profile(capsule->context,holder,
        &capsule->sources,capsule->node_limit,capsule->byte_limit,&frame->post,
        capsule->transaction,capsule->transaction,false);
    if(!frame->post_ready){capsule->refusal="UnrestorableRecordedPost";goto incomplete;}
    if(!prime_replay_capsule_size(capsule))goto incomplete;
    prime_native_service_leave(&services);
    capsule->disposition="Recorded";capsule->refusal=NULL;return true;
incomplete:
    prime_native_service_leave(&services);
    if(capsule->transaction&&frame->post.refusal&&
        !strcmp(frame->post.refusal,"UnrestorableCut"))
        capsule->refusal="UnrestorableCut";
    capsule->disposition="RecordIncomplete";
    /* The live recording control, exact allowance and pending return remain
     * held. An incomplete frame cannot be followed by another producer call. */
    frame->completed=false;return false;
}

/* The result record can be stored by bind!. Its labelled field is data;
 * only the existing owning native-handle token supplies authority. */
static Atom *prime_native_history_record_handle(Atom *input,
    const char *record,const char *label,CettaExprIndex first_field) {
    if(!input||input->kind!=ATOM_EXPR||!input->expr.len||
        !atom_is_symbol(input->expr.elems[0],record))return input;
    Atom *holder=NULL;
    for(CettaExprIndex i=first_field;i<input->expr.len;i++) {
        Atom *field=input->expr.elems[i];
        if(field&&field->kind==ATOM_EXPR&&field->expr.len==2u&&
            atom_is_symbol(field->expr.elems[0],label)) {
            if(holder)return NULL;
            holder=field->expr.elems[1];
        }
    }
    return holder;
}

static Atom *prime_replay_capsule_handle(Atom *input) {
    return prime_native_history_record_handle(input,"ReplayCheckpoint","capsule",1u);
}

static bool prime_native_history_forget(Space *space,Arena *arena,Atom *call,
    const Bindings *environment,OutcomeSet *target) {
    (void)space;
    Atom *argument=bindings_apply_if_vars(environment,arena,expr_arg(call,1u));
    uint64_t limit=0u;
    if(!prime_replay_uint(argument,&limit)||limit>SIZE_MAX)return false;
    Atom *input=bindings_apply_if_vars(environment,arena,expr_arg(call,0u));
    Atom *registered=registry_lookup_atom(input);if(registered)input=registered;
    /* An Observation's first payload is positional values, not a field. */
    input=prime_native_history_record_handle(input,"Observation","residual",2u);
    input=prime_native_history_record_handle(input,"HistoryPolicy","residual",1u);
    /* Reserve the return world and its real holder before ownership changes. */
    Outcome *slot=outcome_set_push_slot(target);
    if(!slot){eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);return true;}
    outcome_init(slot);
    if(!bindings_clone(&slot->env,environment)) {
        outcome_free_fields(slot);target->len--;eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);return true;
    }
    PrimeNativeControl *source=cetta_native_handle_get_owned(g_library_context,"prime-search",input);
    Atom *holder=source?atom_deep_copy(arena,input):atom_symbol(arena,"None");
    if(!holder){slot->atom=call;eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);return true;}
    slot->atom=holder;
    uint64_t work=source?source->steps:0u;
    const char *refusal="SourceOwner";
    CettaContinuationStatus status=source
        ?prime_checkpoint_forget_recording(input,(size_t)limit,&refusal)
        :CETTA_CONTINUATION_INVALIDATED;
    Atom *fields[]={atom_symbol(arena,"HistoryPolicy"),
        prime_native_history_field(arena,"status",atom_symbol(arena,
            status==CETTA_CONTINUATION_READY?"Unrecorded":
            status==CETTA_CONTINUATION_DEFERRED?"Deferred":"Refused")),
        prime_native_history_field(arena,"reason",refusal?atom_string(arena,refusal):atom_symbol(arena,"None")),
        prime_native_history_field(arena,"residual",holder),
        prime_native_history_field(arena,"work",prime_native_occurrence_atom(arena,work)),
        prime_native_history_field(arena,"earlier",atom_symbol(arena,
            status==CETTA_CONTINUATION_READY?"Unrecorded":"Unchanged")),
        prime_native_history_field(arena,"transport-expense",atom_symbol(arena,"Unmetered"))};
    slot->atom=atom_expr(arena,fields,sizeof(fields)/sizeof(*fields));
    return true;
}

static bool prime_native_replay_dispatch(Space *space,Arena *arena,Atom *call,
    const Bindings *environment,bool preserve_bindings,OutcomeSet *target) {
    (void)preserve_bindings;
    const PrimeNeedSymbolIds *ids=prime_need_symbols();SymbolId head=atom_head_symbol_id(call);
    CettaExprLen nargs=expr_nargs(call);
    bool capture=head==ids->history_capture&&nargs==2u;
    bool inspect=head==ids->history_checkpoint&&nargs==1u;
    bool dispose=head==ids->history_dispose&&nargs==1u;
    bool record=head==ids->history_record&&nargs==2u;
    bool replay=head==ids->history_replay&&nargs==2u;
    bool forget=head==ids->history_forget&&nargs==2u;
    if(!capture&&!inspect&&!dispose&&!record&&!replay&&!forget)return false;
    if(!prime_native_replay_caller_admitted())return false;
    if(forget)return prime_native_history_forget(space,arena,call,environment,target);
    Atom *input=bindings_apply_if_vars(environment,arena,expr_arg(call,0u));
    Atom *registered=registry_lookup_atom(input);if(registered)input=registered;
    PrimeReplayCapsule *capsule=capture?NULL:cetta_native_handle_get_owned(g_library_context,
        "prime-replay",prime_replay_capsule_handle(input));
    size_t frames=0u,commands=0u,bytes=0u;
    uint64_t allowance=0u;
    if(capture&&!prime_replay_limits(expr_arg(call,1u),&frames,&commands,&bytes))return false;
    if((record||replay)&&!prime_replay_uint(expr_arg(call,1u),&allowance))return false;
    if(!capture&&(!capsule||capsule->busy))return false;
    /* Reserve the actual return occurrence and its captured world before
     * allocating a capsule, running preparation or disposing any owner. */
    Outcome *slot=outcome_set_push_slot(target);
    if(!slot){eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);return true;}
    outcome_init(slot);
    if(!bindings_clone(&slot->env,environment)) {
        outcome_free_fields(slot);target->len--;eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);return true;
    }
    if(dispose) {
        uint64_t id=capsule->handle;
        bool closed=cetta_native_handle_close(g_library_context,"prime-replay",id);
        slot->atom=atom_expr2(arena,atom_symbol(arena,"ReplayDisposed"),atom_symbol(arena,closed?"True":"False"));
        return true;
    }
    if(capture) {
        capsule=calloc(1u,sizeof(*capsule));
        if(!capsule) {slot->atom=call;eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);return true;}
        capsule->context=g_library_context;capsule->frame_limit=frames;
        capsule->command_limit=commands;capsule->byte_limit=bytes;
        capsule->node_limit=bytes/(sizeof(PrimeReplaySyntaxRoot)+sizeof(PrimeNeedSnapshot *)+
            sizeof(PrimeNeedRecording *)+sizeof(AtomDeepCopyArenaPair));
        capsule->disposition="Admission";
        arena_init_detached(&capsule->storage);arena_set_hashcons(&capsule->storage,NULL);
        prime_replay_checkpoint_init(&capsule->initial);prime_replay_checkpoint_init(&capsule->recording);
        if(!cetta_native_handle_alloc(g_library_context,"prime-replay",capsule,
            prime_replay_capsule_destroy,&capsule->handle)) {
            prime_replay_capsule_destroy(capsule);slot->atom=call;
            eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);return true;
        }
        bool installed=cetta_native_handle_set_storage(g_library_context,"prime-replay",capsule->handle,prime_replay_capsule_storage);
        assert(installed);(void)installed;
        /* The pre-reserved output owns the holder before any request work. */
        slot->atom=cetta_native_handle_owned_atom(g_library_context,arena,"prime-replay",capsule->handle);
        if(!slot->atom||cetta_native_handle_get_owned(g_library_context,"prime-replay",slot->atom)!=capsule) {
            uint64_t handle=capsule->handle;
            (void)cetta_native_handle_close(g_library_context,"prime-replay",handle);
            slot->atom=call;eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);return true;
        }
        /* No source request, scan, copy or expense identity precedes the
         * receiving holder. A failed holder admission disposes only an empty
         * capsule. Later readout refusal retains this already owned token. */
        cetta_native_cost_init(&capsule->expense);
    } else {
        slot->atom=cetta_native_handle_owned_atom(g_library_context,arena,"prime-replay",capsule->handle);
        if(!slot->atom||cetta_native_handle_get_owned(g_library_context,"prime-replay",slot->atom)!=capsule) {
            slot->atom=call;eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);return true;
        }
    }
    capsule->busy=true;
    CettaNativeCostScope scope;cetta_native_cost_scope_enter(&scope,&capsule->expense,0u);
    if(capture&&!prime_replay_capsule_capture(capsule,space,input))
        capsule->disposition="CaptureRefused";
    if(record)(void)prime_replay_capsule_record(capsule,allowance);
    if(replay)(void)prime_replay_capsule_replay(capsule,allowance);
    Atom *readout=prime_replay_capsule_read(capsule,arena);
    if(readout)slot->atom=readout;
    else {
        capsule->disposition="ReadoutRefused";
        capsule->refusal="ReadoutCapacity";
        eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);
    }
    cetta_native_cost_scope_leave(&scope);
    capsule->busy=false;
    return true;
}
