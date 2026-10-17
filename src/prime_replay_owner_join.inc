static void prime_replay_issuance(CettaCheckpointIssuance *out) {
    *out=(CettaCheckpointIssuance){0};
    prime_need_checkpoint_issuance(out);cetta_native_cost_checkpoint_issuance(out);
    SpaceCheckpointIssuance spaces=space_checkpoint_issuance();
    out->next[CETTA_CHECKPOINT_TARGET_SPACE_INSTANCE]=spaces.instance;
    out->next[CETTA_CHECKPOINT_TARGET_SPACE_PREFIX]=spaces.prefix;
}
static PrimeReplayCheckpoint *prime_replay_accepted_checkpoint(PrimeReplayCapsule *capsule) {
    return capsule->accepted_attempt?&capsule->accepted_attempt->frame.entry:&capsule->initial;
}
static Atom *prime_replay_checkpoint_export(CettaLibraryContext *context,
    PrimeReplayCheckpoint *checkpoint,Arena *arena) {
    PrimeNativeControl *control=prime_replay_checkpoint_control(context,checkpoint);
    if(!control)return atom_symbol(arena,"Absent");
    uint64_t id=0u;
    if(!cetta_native_handle_arg(checkpoint->holder,"prime-search",&id))return NULL;
    Atom *holder=cetta_native_handle_owned_atom(context,arena,"prime-search",id);
    return holder&&cetta_native_handle_get_owned(context,"prime-search",holder)==control?holder:NULL;
}
static bool prime_replay_identity_prefix(PrimeReplayComparison *target,
    const PrimeReplayIdentityRelation *source) {
    if(!source)return true;
    for(size_t i=0u;i<source->length;i++) {
        PrimeReplayIdentityPair p=source->items[i];
        /* These coordinates describe this particular physical image. The
         * next image obtains its authority from its actual owner graph. */
        if(p.role==CETTA_CHECKPOINT_ARENA||p.role==CETTA_CHECKPOINT_ISSUED_STORE||
            p.role==CETTA_CHECKPOINT_NATIVE_HANDLE)continue;
        if(!prime_replay_read_identity(target,"accepted.identity_prefix",p.role,p.left,p.right))return false;
    }
    return true;
}
/* These witnesses belong to completed concrete copies, not supplied maps.
 * Historical token numbers are never interpreted as live Space pointers. */
static bool prime_replay_target_witness(const PrimeReplayCheckpoint *checkpoint) {
    const PrimeReplayTargetOrigin *w=&checkpoint->target;
    return w->complete&&checkpoint->ambient_read&&checkpoint->ambient_read->matched&&
        checkpoint->ambient_read->target_copy==w&&
        w->source.space&&w->image.space&&w->source.space!=w->image.space&&
        w->source.instance_id&&w->source.instance_id<w->before.instance&&
        w->before.instance<=w->image.instance_id&&w->image.instance_id<w->after.instance&&
        w->source.prefix_epoch&&w->source.prefix_epoch<w->before.prefix&&
        w->before.prefix<=w->image.prefix_epoch&&w->image.prefix_epoch<w->after.prefix&&
        w->after.instance!=UINT64_MAX&&w->after.prefix!=UINT64_MAX;
}
static bool prime_replay_target_role(CettaCheckpointIdentityRole role) {
    return role==CETTA_CHECKPOINT_TARGET_SPACE_INSTANCE||role==CETTA_CHECKPOINT_TARGET_SPACE_PREFIX;
}
static bool prime_replay_target_relocate(PrimeReplayComparison *read,
    CettaCheckpointIdentityRole role,uint64_t value,const PrimeReplayCheckpoint *copy,uint64_t *image) {
    *image=value;
    if(!copy||!prime_replay_target_role(role))return true;
    if(!prime_replay_compare_tick(read,"target.copy_witness",1u,false)||
        !prime_replay_target_witness(copy))return false;
    const PrimeReplayTargetOrigin *w=&copy->target;
    bool instance=role==CETTA_CHECKPOINT_TARGET_SPACE_INSTANCE;
    uint64_t current=instance?w->source.instance_id:w->source.prefix_epoch;
    uint64_t before=instance?w->before.instance:w->before.prefix;
    uint64_t fresh=instance?w->image.instance_id:w->image.prefix_epoch;
    if(value>=before)return prime_replay_compare_refuse(read,"target.copy_domain","UnheldPreIssuanceIdentity");
    if(value==current)*image=fresh;
    return true;
}
static bool prime_replay_transaction_prefix(PrimeReplayComparison *read,
    const PrimeReplayIdentityRelation *prior,const PrimeReplayCheckpoint *left_copy,
    const PrimeReplayCheckpoint *right_copy) {
    if(!prior)return true;
    for(size_t i=0u;i<prior->length;i++) {
        PrimeReplayIdentityPair p=prior->items[i];
        if(p.role==CETTA_CHECKPOINT_ARENA||p.role==CETTA_CHECKPOINT_ISSUED_STORE||
            p.role==CETTA_CHECKPOINT_NATIVE_HANDLE)continue;
        uint64_t a,b;
        if(!prime_replay_target_relocate(read,p.role,p.left,left_copy,&a)||
            !prime_replay_target_relocate(read,p.role,p.right,right_copy,&b)||
            !prime_replay_read_identity(read,"transaction.accepted_prefix",p.role,a,b))return false;
    }
    return true;
}
static bool prime_replay_transaction_initial(PrimeReplayComparison *read,
    const PrimeReplayCheckpoint *left,const PrimeReplayCheckpoint *right) {
    if(!prime_replay_target_witness(left)||!prime_replay_target_witness(right))return false;
    const PrimeReplayTargetOrigin *a=&left->target,*b=&right->target;
    if(a->source.space!=b->source.space||a->source.instance_id!=b->source.instance_id||
        a->source.prefix_epoch!=b->source.prefix_epoch||a->source.revision!=b->source.revision||
        a->source.base_prefix_epoch!=b->source.base_prefix_epoch||
        a->source.base_dependency_epoch!=b->source.base_dependency_epoch)return false;
    const PrimeReplayIdentityRelation *x=&left->ambient_read->identities;
    const PrimeReplayIdentityRelation *y=&right->ambient_read->identities;
    for(size_t i=0u;i<x->length;i++) {
        PrimeReplayIdentityPair p=x->items[i];
        if(!prime_replay_target_role(p.role))continue;
        bool found=false;
        for(size_t j=0u;j<y->length;j++) {
            if(!prime_replay_compare_tick(read,"target.common_origin",1u,true))return false;
            PrimeReplayIdentityPair q=y->items[j];
            if(p.role==q.role&&p.left==q.left) {
                if(!prime_replay_read_identity(read,"target.initial_images",p.role,p.right,q.right))return false;
                found=true;break;
            }
        }
        if(!found)return prime_replay_compare_refuse(read,"target.common_origin","MissingCopyIdentity");
    }
    return true;
}
static bool prime_replay_transaction_identity_commutes(PrimeReplayComparison *post,
    const PrimeReplayComparison *word,const PrimeReplayCheckpoint *recorded_post) {
    if(!prime_replay_target_witness(recorded_post))return false;
    for(size_t i=0u;i<word->identities.length;i++) {
        PrimeReplayIdentityPair p=word->identities.items[i];
        if(p.role==CETTA_CHECKPOINT_ARENA||p.role==CETTA_CHECKPOINT_ISSUED_STORE||
            p.role==CETTA_CHECKPOINT_NATIVE_HANDLE)continue;
        uint64_t left;
        if(!prime_replay_target_relocate(post,p.role,p.left,recorded_post,&left)||
            !prime_replay_read_identity(post,"transaction.commuting_identity",p.role,left,p.right))return false;
    }
    return true;
}
static bool prime_replay_transaction_atoms_commute(PrimeReplayComparison *word,
    const PrimeReplayComparison *post,const PrimeReplayCheckpoint *recorded_post,
    const PrimeReplayResourceWord *recorded,const PrimeReplayResourceWord *attempted) {
    if(!prime_replay_target_witness(recorded_post)||!recorded->live_validated||
        !attempted->live_validated||!recorded->origin.matched||!attempted->origin.matched||
        !prime_replay_compare_tick(word,"transaction.recorded_borrow",1u,true)||
        !prime_replay_resource_correspondence_live(recorded)||
        !prime_replay_compare_tick(word,"transaction.attempted_borrow",1u,true)||
        !prime_replay_resource_correspondence_live(attempted))
        return prime_replay_compare_refuse(word,"transaction.word_map","UnavailableWordCorrespondence");
    const PrimeReplayComparison *origin=recorded_post->ambient_read;
    for(size_t i=0u;i<origin->atom_count;i++) {
        if(!prime_replay_compare_tick(word,"transaction.copy_atom",1u,true))return false;
        const Atom *source=origin->atoms[i].left,*image=origin->atoms[i].right;
        const Atom *recorded_atom=NULL;
        for(size_t j=0u;j<recorded->origin.atom_count;j++) {
            if(!prime_replay_compare_tick(word,"transaction.word_member",1u,true))return false;
            if(recorded->origin.atoms[j].left==source){recorded_atom=recorded->origin.atoms[j].right;break;}
        }
        /* Both complete, still-live graph readers distinguish an absent
         * root from an unavailable map, including identity-retained globals. */
        if(!recorded_atom)continue;
        const Atom *native=NULL;
        for(size_t j=0u;j<post->atom_count;j++) {
            if(!prime_replay_compare_tick(word,"transaction.post_atom",1u,true))return false;
            if(post->atoms[j].left==image){native=post->atoms[j].right;break;}
        }
        if(!native)return prime_replay_compare_refuse(word,"transaction.copy_atom","MissingPostAtom");
        const Atom *attempted_atom=NULL;
        for(size_t j=0u;j<attempted->origin.atom_count;j++) {
            if(!prime_replay_compare_tick(word,"transaction.attempt_member",1u,true))return false;
            if(attempted->origin.atoms[j].left==native){attempted_atom=attempted->origin.atoms[j].right;break;}
        }
        if(!attempted_atom||!prime_replay_read_atom(word,"transaction.commuting_atom",recorded_atom,attempted_atom))
            return prime_replay_compare_refuse(word,"transaction.copy_atom","MissingWordAtom");
    }
    return true;
}

static bool prime_replay_comparison_open(PrimeReplayCapsule *capsule,PrimeReplayAttempt *attempt,
    PrimeReplayComparison *read,const PrimeReplayIdentityRelation *prefix,
    CettaCheckpointIssuance lb,CettaCheckpointIssuance le,
    CettaCheckpointIssuance rb,CettaCheckpointIssuance re) {
    /* An invocation is paid even if storage or domain admission refuses. */
    cetta_native_cost_charge(CETTA_COST_CAPTURE,1u);
    if(!prime_replay_capsule_size(capsule)||capsule->guarded_bytes>=capsule->byte_limit)return false;
    size_t bytes=(capsule->byte_limit-(size_t)capsule->guarded_bytes)/4u;
    size_t width=sizeof(PrimeReplayAddressPair)*2u+sizeof(PrimeReplayArenaPair)+
        sizeof(PrimeReplayPhysicalField)+sizeof(PrimeReplayIdentityPair)+sizeof(PrimeReplayProjectionNode);
    size_t capacity=bytes/(2u*width);
    if(capacity>capsule->node_limit)capacity=capsule->node_limit;
    bool ok=prime_replay_comparison_init(read,capacity,attempt->inspection_remaining,
        bytes,&capsule->sources,NULL);
    read->left_begin=lb;read->left_end=le;read->right_begin=rb;read->right_end=re;
    if(ok)ok=prime_replay_identity_prefix(read,prefix);
    return ok;
}
static void prime_replay_comparison_close(PrimeReplayAttempt *attempt,PrimeReplayComparison *read) {
    if(read->ready)attempt->inspection_remaining=read->fuel;
    prime_replay_comparison_account(read);
}
static bool prime_replay_receipt_map(void *raw,CettaNativeCostIdentityRole role,
    uint64_t source,uint64_t *image) {
    PrimeReplayComparison *c=raw;
    CettaCheckpointIdentityRole native=role==CETTA_COST_ID_EVENT?CETTA_CHECKPOINT_COST_EVENT:
        role==CETTA_COST_ID_OCCURRENCE?CETTA_CHECKPOINT_ISSUED_OCCURRENCE:CETTA_CHECKPOINT_COST_LEDGER;
    if(!source){*image=0u;return true;}
    for(size_t i=0u;i<c->identities.length;i++) {
        if(!prime_replay_compare_tick(c,"commands.map_lookup",1u,true))return false;
        PrimeReplayIdentityPair p=c->identities.items[i];
        if(p.role==native&&p.left==source){*image=p.right;return true;}
    }
    return false;
}
static bool prime_replay_compare_commands_in(PrimeReplayComparison *c,
    const CettaNativeCostReceipts *a,const CettaNativeCostReceipts *b,bool prefix) {
    if((prefix ? a->command_count<b->command_count :
        (a->command_count!=b->command_count||a->destination_count!=b->destination_count))||
        a->command_count>a->command_capacity||b->command_count>b->command_capacity||
        a->destination_count>a->destination_capacity||b->destination_count>b->destination_capacity||
        a->command_count>SIZE_MAX-a->destination_count)return false;
    size_t n=a->command_count+a->destination_count;
    if(n>SIZE_MAX-b->command_count||n+b->command_count>SIZE_MAX-b->destination_count)return false;
    n+=b->command_count+b->destination_count;
    if(n>SIZE_MAX/(2u*CETTA_COST_KIND_COUNT+32u)||
        !prime_replay_read_service(c,"commands.full_receipt",n*(2u*CETTA_COST_KIND_COUNT+32u),0u)||
        !cetta_native_cost_receipts_complete(a)||!cetta_native_cost_receipts_complete(b))return false;
    for(size_t i=0u;i<b->command_count;i++) {
        const CettaNativeCostEvent *x=&a->commands[i].event,*y=&b->commands[i].event;
        if(!x->identity||!y->identity||x->identity==UINT64_MAX||y->identity==UINT64_MAX||
            !prime_replay_read_identity(c,"commands.event",CETTA_CHECKPOINT_COST_EVENT,x->identity,y->identity)||
            !prime_replay_read_identity(c,"commands.origin",CETTA_CHECKPOINT_COST_LEDGER,x->origin,y->origin)||
            !prime_replay_read_identity(c,"commands.occurrence",CETTA_CHECKPOINT_ISSUED_OCCURRENCE,x->occurrence,y->occurrence))return false;
    }
    if(a->destination_count<b->destination_count)return false;
    for(size_t i=0u;i<b->destination_count;i++) {
        const CettaNativeCostDestinationReceipt *x=&a->destinations[i],*y=&b->destinations[i];
        if(!prime_replay_read_identity(c,"commands.destination",CETTA_CHECKPOINT_COST_LEDGER,x->ledger_identity,y->ledger_identity)||
            !prime_replay_read_identity(c,"commands.scope_occurrence",CETTA_CHECKPOINT_ISSUED_OCCURRENCE,x->scope_occurrence,y->scope_occurrence))return false;
    }
    size_t mismatch=0u;
    bool ok=prefix?cetta_native_cost_receipts_prefix_related(a,b,prime_replay_receipt_map,c,&mismatch):
        cetta_native_cost_receipts_related(a,b,prime_replay_receipt_map,c,&mismatch);
    return ok||prime_replay_compare_refuse(c,"commands.receipts","CommandMismatch");
}
static bool prime_replay_compare_commands(PrimeReplayComparison *c,
    const CettaNativeCostReceipts *a,const CettaNativeCostReceipts *b) {
    return prime_replay_compare_commands_in(c,a,b,false);
}
static bool prime_replay_compare_phases(PrimeReplayComparison *c,
    const PrimeReplayRequestFrame *a,const PrimeReplayRequestFrame *b) {
    if(!prime_replay_phases_complete(&a->phases,&a->commands)||
        !prime_replay_phases_complete(&b->phases,&b->commands)||
        !prime_replay_read_scalar(c,"phase.count",a->phases.count,b->phases.count))return false;
    for(size_t i=0u;i<a->phases.count;i++) {
        const PrimeReplayPhase *x=&a->phases.items[i],*y=&b->phases.items[i];
        if(!prime_replay_read_scalar(c,"phase.kind",x->kind,y->kind)||
            !prime_replay_read_scalar(c,"phase.command",x->command,y->command)||
            !prime_replay_read_identity(c,"phase.origin",CETTA_CHECKPOINT_COST_LEDGER,x->origin,y->origin)||
            !prime_replay_read_identity(c,"phase.occurrence",CETTA_CHECKPOINT_ISSUED_OCCURRENCE,x->occurrence,y->occurrence))return false;
        if(x->kind==CETTA_COST_RESTORE) {
            if(!prime_replay_read_identity(c,"phase.selected",CETTA_CHECKPOINT_ISSUED_OCCURRENCE,x->trigger,y->trigger))return false;
        } else if(!prime_replay_read_scalar(c,"phase.trigger",x->trigger,y->trigger))return false;
    }
    return true;
}
static bool prime_replay_compare_frame_product(PrimeReplayCapsule *capsule,
    PrimeReplayComparison *c,const PrimeReplayRequestFrame *a,const PrimeReplayRequestFrame *b,
    const PrimeNativeControl *recording,const PrimeNativeControl *attempt) {
#define FP(f) do { if(!prime_replay_read_scalar(c,"request."#f,a->f,b->f))return false; } while(0)
    FP(allowance);FP(status);FP(ready);FP(entered);FP(completed);FP(accounted);
    FP(top.owned.allowance);FP(top.owned.started);FP(top.owned.initialized);FP(top.owned.status);
    FP(top.owned.transaction_entry);
    FP(top.reserved_length);FP(top.normalized);FP(top.committed);
    FP(top.snapshot_request);FP(top.snapshot_unsupported);FP(results.len);
#undef FP
    /* The recording request alone borrows the capsule's reserved original.
     * A private attempt has its own independently owned, unreserved control. */
    bool reservation=capsule->transaction ?
        recording==prime_replay_original(capsule)&&capsule->source_reserved&&recording->leased&&
            a->top.owned.reservation==recording&&!b->top.owned.reservation&&!attempt->leased :
        !a->top.owned.reservation&&!b->top.owned.reservation;
    if(!prime_replay_read_scalar(c,"request.reservation_authority",reservation,true))return false;
    if(a->top.owned.unpublished||b->top.owned.unpublished||
        a->top.destination!=&a->results||b->top.destination!=&b->results||
        a->top.owned.pending!=&a->pending||b->top.owned.pending!=&b->pending||
        a->results.len!=1u||b->results.len!=1u||
        a->top.projected!=a->results.items[0]||b->top.projected!=b->results.items[0])return false;
    if(!prime_replay_read_arena(c,"request.storage",&a->storage,&b->storage)||
        !prime_replay_read_physical(c,"request.result_capacity",a->results.cap,b->results.cap)||
        !prime_replay_read_physical(c,"request.reserved_capacity",a->top.reserved_capacity,b->top.reserved_capacity)||
        !prime_replay_compare_outcomes(&a->pending,&b->pending,&c->read)||
        !prime_replay_compare_cost(&a->physical,&b->physical,&c->read)||
        !prime_replay_compare_commands(c,&a->commands,&b->commands)||
        !prime_replay_compare_phases(c,a,b))return false;
    /* These two actual reply arenas own their episode and native-holder
     * edges. Metadata handle IDs are authenticated at the live targets. */
    if(!a->top.episode||!b->top.episode||
        !arena_retains_owner(&a->storage,a->top.episode,prime_native_top_episode_release)||
        !arena_retains_owner(&b->storage,b->top.episode,prime_native_top_episode_release)||
        cetta_native_handle_get_owned(capsule->context,"prime-search",a->top.owned.holder)!=recording||
        cetta_native_handle_get_owned(capsule->context,"prime-search",b->top.owned.holder)!=attempt)
        return prime_replay_compare_refuse(c,"request.owner","ReplyOwnerMismatch");
    return prime_replay_read_arena_inventory(c)&&prime_replay_compare_projection(c,capsule->context,
        a->results.items[0],b->results.items[0],recording,attempt);
}

static bool prime_replay_frame_begin(PrimeReplayRequestFrame *frame,uint64_t allowance,
    CettaNativeCostReceipts commands,bool owns_commands) {
    /* The actual auxiliary ledger is allocated after this request frontier. */
    prime_replay_issuance(&frame->begin);
    arena_init_detached(&frame->storage);arena_set_hashcons(&frame->storage,NULL);
    frame->storage_initial_epoch=frame->storage.reset_epoch;
    prime_replay_checkpoint_init(&frame->entry);prime_replay_checkpoint_init(&frame->post);
    outcome_set_init_with_owner(&frame->pending,&frame->storage);
    result_set_init(&frame->results);cetta_native_cost_init(&frame->physical);
    frame->ready=true;frame->allowance=allowance;frame->commands=commands;
    frame->owns_commands=owns_commands;
    if(!frame->physical.identity||frame->physical.total.trace_incomplete)return false;
    return prime_replay_phases_init(&frame->phases,commands.command_capacity,
        commands.destination_capacity,&frame->commands);
}
typedef struct {
    CettaNativeCostObserverSuspension observers;
    CettaNativeCostScope *caller;
    CettaNativeCostScope expense;
    PrimeReplayPhases *phases;
    bool entered;
} PrimeReplayResourceInspection;
typedef struct {
    PrimeReplayCapsule *capsule;
    PrimeReplayRequestFrame *frame;
    const PrimeReplayRequestFrame *recorded;
    PrimeReplayAttempt *attempt;
    PrimeNativeControl *control;
} PrimeReplayResourceRun;

static bool prime_replay_resource_inspection_enter(PrimeReplayCapsule *capsule,
    PrimeReplayResourceInspection *scope) {
    if(!cetta_native_cost_observer_suspend(&scope->observers))return false;
    scope->phases=g_prime_replay_phases;g_prime_replay_phases=NULL;
    scope->caller=cetta_native_cost_detach_begin();
    cetta_native_cost_scope_enter(&scope->expense,&capsule->expense,0u);
    scope->entered=true;return true;
}
static void prime_replay_resource_inspection_leave(PrimeReplayResourceInspection *scope) {
    if(!scope->entered)return;
    cetta_native_cost_scope_leave(&scope->expense);
    cetta_native_cost_detach_end(scope->caller);
    g_prime_replay_phases=scope->phases;
    cetta_native_cost_observer_restore(&scope->observers);
    *scope=(PrimeReplayResourceInspection){0};
}
static bool prime_replay_transaction_authority(PrimeReplayCapsule *capsule,
    SpaceReadToken target,SpaceReadToken replacement) {
    cetta_native_cost_charge(CETTA_COST_STORAGE_PROBE,1u);
    SpaceContentsMovedHook actual=space_contents_moved_hook();
    if(actual!=capsule->subscriber||
        (actual&&actual!=prime_scoped_judgment_contents_subscriber())||
        !target.space||!space_read_token_is_current(target)||
        (replacement.space&&!space_read_token_is_current(replacement)))return false;
    uint64_t instances[]={target.instance_id,replacement.instance_id};
    for(size_t i=0u;i<2u;i++) {
        if(!instances[i])continue;
        size_t used=0u;bool empty=false;
        bool ok=prime_scoped_judgment_instance_empty(instances[i],capsule->node_limit,&used,&empty);
        cetta_native_cost_charge(CETTA_COST_INDEX_VISIT,used);
        if(!ok||!empty)return false;
        used=0u;empty=false;
        ok=prime_semantics_theorem_instance_empty(instances[i],capsule->node_limit,&used,&empty);
        cetta_native_cost_charge(CETTA_COST_INDEX_VISIT,used);
        if(!ok||!empty)return false;
    }
    return true;
}
/* These bounded header queries plan allocation only. The caller holds the
 * complete recorded owner; actual replay still traverses and compares its
 * whole word. Each attempted metadata query is one expense storage probe. */
static size_t prime_replay_resource_replay_cells(const CettaSpaceAgendaTranscript *word,
    size_t expected_events) {
    cetta_native_cost_charge(CETTA_COST_STORAGE_PROBE,1u);
    if(!cetta_space_agenda_transcript_complete(word))return 0u;
    size_t cells=0u,capacity=0u;
    cetta_native_cost_charge(CETTA_COST_STORAGE_PROBE,1u);
    if(!cetta_space_agenda_transcript_counts(word,&cells,&capacity)||cells>capacity)return 0u;
    cetta_native_cost_charge(CETTA_COST_STORAGE_PROBE,1u);
    size_t events=cetta_space_agenda_transcript_events(word);
    if(events!=expected_events||cells<events||(!events&&cells))return 0u;
    /* The constructor requires positive capacity, including an empty word. */
    return cells?cells:1u;
}

static bool prime_replay_resource_word_open(PrimeReplayCapsule *capsule,
    PrimeReplayRequestFrame *frame,PrimeNativeControl *control,Atom *holder,const PrimeReplayRequestFrame *recorded) {
    PrimeReplayResourceWord *w=&frame->resource;
    if(w->ready||!control||!control->space_agenda||
        !prime_replay_capsule_size(capsule)||capsule->guarded_bytes>=capsule->byte_limit)return false;
    w->ready=true;w->source_context=capsule->context;
    w->source_holder=holder;w->owner=&frame->storage;
    size_t bytes=(capsule->byte_limit-(size_t)capsule->guarded_bytes)/4u;
    size_t events=recorded?recorded->resource.event_count:frame->commands.command_capacity;
    if(!events)events=1u;
    if(!events||events>SIZE_MAX/sizeof(*w->boundaries)||events*sizeof(*w->boundaries)>bytes/8u)
        return false;
    w->boundaries=calloc(events,sizeof(*w->boundaries));
    if(!w->boundaries)return false;
    w->event_capacity=events;
    if(!prime_replay_phases_init(&w->sites,frame->commands.command_capacity,
        frame->commands.destination_capacity,&frame->commands))return false;
    w->budget=(SpaceCheckpointBudget){.work_limit=capsule->byte_limit,
        .byte_limit=bytes/2u};
    size_t cells=recorded?prime_replay_resource_replay_cells(recorded->resource.word,
        recorded->resource.event_count):cetta_space_agenda_transcript_cell_capacity(bytes/2u,events);
    if(cells<events)return false;
    /* This frame owner has not yet received a reply or transcript payload.
     * Match the held source's allocator policy before map setup copies its
     * canonical caches. Preserve the identity issued at frame_begin. */
    Arena *image=&frame->storage;
    const Arena *source=&control->storage;
    const Arena *persistent=capsule->sources.persistent;
    if(!prime_replay_sources_current(&capsule->sources)||!source->identity||
        !image->identity||image->reset_epoch!=frame->storage_initial_epoch||
        image->head||image->spare||
        image->hashcons||image->symbol_cache||image->persistent_bytes||image->live_bytes||
        image->external_bytes||image->reserved_bytes||image->spare_bytes||
        image->block_count||image->spare_block_count||image->older_identity||
        image->finalizers||image->retained_owners||image->frame_identities||
        (source->hashcons&&source->hashcons!=capsule->sources.globals)||
        (source->older_identity&&(!persistent||
            source->older_identity!=persistent->identity||persistent->older_identity)))return false;
    arena_set_block_capacity(image,source->block_capacity);
    arena_set_runtime_kind(image,source->runtime_kind);
    arena_set_scalar_cache(image,source->scalar_cache);
    arena_set_hashcons(image,source->hashcons);
    if(source->older_identity)arena_set_older_generation(image,persistent);
    /* Both membership carriers have a checked inspection/storage reservation
     * before their allocation. The source/image policy is already fixed. */
    size_t width=sizeof(PrimeReplayAddressPair)*2u+sizeof(PrimeReplayArenaPair)+
        sizeof(PrimeReplayPhysicalField)+sizeof(PrimeReplayIdentityPair)+sizeof(unsigned char);
    size_t capacity=(bytes/4u)/(2u*width);
    if(capacity>capsule->node_limit)capacity=capsule->node_limit;
    bool ok=prime_replay_comparison_init(&w->origin,capacity,(uint64_t)capsule->byte_limit,
        bytes/4u,&capsule->sources,NULL);
    w->origin.left=w->origin.right=control;
    size_t copy_bytes=0u,copy_work=0u,copy_granted=0u;
    if(ok)ok=atom_checkpoint_global_budget(capsule->sources.globals,capsule->node_limit,&copy_bytes,&copy_work);
    if(copy_work>capsule->node_limit)copy_work=capsule->node_limit;
    if(!ok||!prime_replay_global_reserve(&w->origin,"transaction.copy_global_preparation",copy_work,copy_bytes,&copy_granted))
        return false;
    w->global_reserved=copy_bytes;
    AtomDeepCopySession *atoms=atom_deep_copy_session_new(image);
    AtomDeepCopyGlobalInspection global={0};
    bool globals=atoms&&atom_deep_copy_session_restrict_globals(atoms,capsule->sources.globals,
        copy_granted,&global);
    prime_replay_global_settle(&w->origin,copy_granted,&global);
    AtomDeepCopyArenaPair pairs[]={{&control->storage,&frame->storage},
        {capsule->sources.persistent,capsule->sources.persistent}};
    if(!globals||!atom_deep_copy_session_set_runtime_arena_map(atoms,pairs,2u)) {
        atom_deep_copy_session_free(atoms);return false;
    }
    w->word=cetta_space_agenda_transcript_new(&frame->storage,atoms,cells,events,&w->budget);
    if(!w->word){atom_deep_copy_session_free(atoms);return false;}
    for(size_t i=0u;ok&&i<2u;i++)
        ok=prime_replay_read_arena(&w->origin,"transaction.word_owner",pairs[i].source,pairs[i].destination);
    ok=ok&&cetta_space_agenda_transcript_complete(w->word)&&prime_replay_capsule_size(capsule);
    w->live_validated=w->origin.matched=ok;
    return ok;
}
/* Growth is inspection work, not a primitive producer command. The caller
 * has suspended its command observer/required phases and selected expense. */
static bool prime_replay_resource_globals(PrimeReplayResourceRun *run,bool word) {
    PrimeReplayResourceWord *w=&run->frame->resource;
    PrimeReplayComparison *c=&w->origin;
    size_t bytes=0u,work=0u;
    const HashConsTable *owner=run->capsule->sources.globals;
    size_t nodes=run->capsule->node_limit;
    if(!prime_replay_compare_tick(c,"transaction.global_current",1u,false))return false;
    bool ready=word?cetta_space_agenda_transcript_global_extension_budget(w->word,owner,nodes,&bytes,&work):
        atom_deep_copy_session_global_extension_budget(c->globals,owner,nodes,&bytes,&work);
    if(!ready)return prime_replay_compare_refuse(c,"transaction.global_domain","GlobalOwnerDomain");
    if(!bytes)return true;
    /* Old metadata is already reserved. Admit coexistence before allocation;
     * after the atomic swap, retire exactly that old reservation. */
    size_t granted=0u;
    if(!prime_replay_global_reserve(c,"transaction.global_extension",work,bytes,&granted))return false;
    cetta_native_cost_charge(CETTA_COST_STORAGE_PROBE,1u);
    AtomDeepCopyGlobalInspection observed={0};
    bool ok=word?cetta_space_agenda_transcript_extend_globals(w->word,owner,nodes,granted,bytes,&observed):
        atom_deep_copy_session_extend_globals(c->globals,owner,nodes,granted,bytes,&observed);
    prime_replay_global_settle(c,granted,&observed);
    size_t *reserved=word?&w->global_reserved:&c->global_reserved;
    assert(c->scratch_reserved>=(ok?*reserved:bytes));
    c->scratch_reserved-=ok?*reserved:bytes;
    if(ok)*reserved=bytes;
    return ok||prime_replay_compare_refuse(c,"transaction.global_extension","GlobalOwnerDomain");
}

static bool prime_replay_resource_observe(void *raw,const CettaSpaceAgendaEvent *event) {
    PrimeReplayResourceRun *run=raw;
    PrimeReplayResourceWord *w=&run->frame->resource;
    if(!w->ready||event->agenda!=run->control->space_agenda||w->refusal)return false;
    PrimeReplayResourceInspection inspection={0};
    if(!prime_replay_resource_inspection_enter(run->capsule,&inspection))return false;
    size_t work_before=w->budget.work;
    uint64_t fuel_before=w->origin.fuel;
    if(run->attempt) {
        uint64_t available=run->attempt->inspection_remaining;
        size_t capture=(size_t)(available/2u);
        if(capture>SIZE_MAX-w->budget.work)capture=SIZE_MAX-w->budget.work;
        w->budget.work_limit=w->budget.work+capture;
        w->origin.fuel=available-capture;
        fuel_before=w->origin.fuel;
    }
    bool ok=w->event_count<w->event_capacity&&w->event_count<SIZE_MAX&&
        w->scope.events==(uint64_t)w->event_count+1u&&
        cetta_space_agenda_transcript_events(w->word)==w->event_count&&
        prime_replay_sources_current(&run->capsule->sources);
    PrimeReplayResourceBoundary *boundary=ok?&w->boundaries[w->event_count]:NULL;
    if(ok)*boundary=(PrimeReplayResourceBoundary){.command=run->frame->commands.command_count,
        .phase_count=run->frame->phases.count,.site_count=w->sites.count,.kind=event->kind};
    w->live_validated=false;
    if(ok&&!(prime_replay_resource_globals(run,true)&&prime_replay_resource_globals(run,false))) {
        ok=false;
        if(!w->budget.refusal)w->budget.refusal="TranscriptGlobalDomain";
    }
    /* Append first retains an actual committed fact even if inspection
     * fails after publication. No later callback may clear that refusal. */
    bool appended=cetta_space_agenda_transcript_append(w->word,event,&w->budget);
    if(ok&&appended) {
        ok=cetta_space_agenda_transcript_events(w->word)==w->event_count+1u&&
            cetta_space_agenda_transcript_validate_live(w->word,event,&w->origin.read,
            &w->budget,&boundary->replacement)&&prime_replay_read_atom_graph(&w->origin)&&
            prime_replay_read_identity_authority(&w->origin,0u)&&
            prime_replay_sources_current(&run->capsule->sources);
        if(ok) {w->event_count++;w->live_validated=true;w->origin.matched=true;}
    } else ok=false;
    if(ok&&event->kind==CETTA_AGENDA_EVENT_COMMITTED)
        ok=prime_replay_transaction_authority(run->capsule,
            space_read_token(cetta_space_agenda_checkpoint_target(event->agenda)),(SpaceReadToken){0});
    if(ok)ok=prime_replay_capsule_size(run->capsule);
    uint64_t word_work=(uint64_t)(w->budget.work-work_before);
    uint64_t origin_work=fuel_before-w->origin.fuel;
    bool sum=word_work<=UINT64_MAX-origin_work;
    uint64_t spent=sum?word_work+origin_work:UINT64_MAX;
    if(!sum||spent>UINT64_MAX-w->live_validation_work)ok=false;
    else w->live_validation_work+=spent;
    if(run->attempt) {
        assert(sum&&spent<=run->attempt->inspection_remaining);
        run->attempt->inspection_remaining-=spent;
    }
    if(!ok)w->refusal="TransactionLiveInspection";
    prime_replay_resource_inspection_leave(&inspection);return ok;
}
static const CettaNativeCostKind prime_replay_resource_kinds[]={
    CETTA_COST_QUEUE_VISIT,CETTA_COST_CANDIDATE_ROW,CETTA_COST_CANDIDATE_ROW,
    CETTA_COST_CAPTURE,CETTA_COST_COMMIT_CHECK,CETTA_COST_COMMIT_CHECK};
static bool prime_replay_resource_site_matches(const PrimeReplayResourceWord *w,size_t index) {
    const PrimeReplayPhases *sites=&w->sites;
    cetta_native_cost_charge(CETTA_COST_ANALYSIS_STEP,1u);
    if(index>=sites->count||sites->incomplete||!sites->commands||sites->commands->open)return false;
    const PrimeReplayPhase *p=&sites->items[index];
    const CettaNativeCostReceipts *commands=sites->commands;
    if(p->command>=commands->command_count||p->trigger>=6u||
        p->kind!=prime_replay_resource_kinds[p->trigger])return false;
    const CettaNativeCostCommandReceipt *c=&commands->commands[p->command];
    if(!c->scoped||c->event.kind!=p->kind||c->event.units!=1u||c->event.repetitions!=1u||
        !c->event.identity||c->event.identity==UINT64_MAX||c->event.origin!=p->origin||
        c->event.occurrence!=p->occurrence||p->destination_count!=c->destination_count||
        p->first_destination>sites->destination_count||
        p->destination_count>sites->destination_count-p->first_destination||
        c->first_destination>commands->destination_count||
        c->destination_count>commands->destination_count-c->first_destination)return false;
    for(size_t j=0u;j<p->destination_count;j++) {
        cetta_native_cost_charge(CETTA_COST_ANALYSIS_STEP,1u);
        const CettaNativeCostDestinationReceipt *a=&sites->destinations[p->first_destination+j];
        const CettaNativeCostDestinationReceipt *b=&commands->destinations[c->first_destination+j];
        if(a->ledger_identity!=b->ledger_identity||a->scope_occurrence!=b->scope_occurrence||
            a->present!=b->present||a->duplicate!=b->duplicate||a->retained!=b->retained||
            !prime_replay_phase_snapshot(&a->before,&b->before)||
            !prime_replay_phase_snapshot(&a->after,&b->after))return false;
    }
    return true;
}
static bool prime_replay_resource_charge(void *raw,const CettaSpaceAgenda *agenda,
    CettaSpaceAgendaChargeSite site,unsigned kind,uint64_t units,bool after) {
    PrimeReplayResourceRun *run=raw;PrimeReplayResourceWord *w=&run->frame->resource;
    if(agenda!=run->control->space_agenda||w->refusal||(unsigned)site>=6u||
        kind!=(unsigned)prime_replay_resource_kinds[site]||units!=1u)return false;
    if(!after) {
        if(w->site_open)return false;
        PrimeReplayPhases *saved=g_prime_replay_phases;
        g_prime_replay_phases=&w->sites;
        prime_replay_phase_required((CettaNativeCostKind)kind,(uint64_t)site);
        g_prime_replay_phases=saved;
        w->site_open=!w->sites.incomplete;
        PrimeReplayResourceInspection inspection={0};
        if(!prime_replay_resource_inspection_enter(run->capsule,&inspection))return false;
        cetta_native_cost_charge(CETTA_COST_CAPTURE,1u);
        if(w->sites.count)cetta_native_cost_charge(CETTA_COST_STORAGE_PROBE,
            w->sites.items[w->sites.count-1u].destination_count);
        prime_replay_resource_inspection_leave(&inspection);
        return w->site_open;
    }
    PrimeReplayResourceInspection inspection={0};
    if(!prime_replay_resource_inspection_enter(run->capsule,&inspection))return false;
    bool ok=w->site_open&&w->sites.count&&
        w->sites.items[w->sites.count-1u].trigger==(uint64_t)site&&
        run->frame->commands.command_count==w->sites.items[w->sites.count-1u].command+1u&&
        prime_replay_resource_site_matches(w,w->sites.count-1u);
    w->site_open=false;
    if(!ok)w->refusal="TransactionRequiredCharge";
    prime_replay_resource_inspection_leave(&inspection);
    return ok;
}

/* Reserve the complete existing phase-validator traversal before it reads
 * the retained command word. Future recorded commands remain part of this
 * bound even when only a precommit prefix is compared. */
static bool prime_replay_resource_phase_budget(PrimeReplayComparison *read,
    const PrimeReplayRequestFrame *a,const PrimeReplayRequestFrame *b) {
    const PrimeReplayRequestFrame *frames[]={a,b};
    size_t extent=0u;
    for(size_t i=0u;i<2u;i++) {
        const PrimeReplayRequestFrame *f=frames[i];
        if(f->commands.command_count>f->commands.command_capacity||
            f->commands.destination_count>f->commands.destination_capacity||
            f->phases.count>f->phases.capacity||
            f->phases.destination_count>f->phases.destination_capacity||
            f->resource.sites.count>f->resource.sites.capacity||
            f->resource.sites.destination_count>f->resource.sites.destination_capacity)return false;
        size_t lengths[]={1u,f->commands.command_count,f->commands.destination_count,
            f->phases.count,f->phases.destination_count,f->resource.sites.count,
            f->resource.sites.destination_count};
        for(size_t j=0u;j<sizeof(lengths)/sizeof(*lengths);j++) {
            if(lengths[j]>SIZE_MAX-extent)return false;
            extent+=lengths[j];
        }
    }
    size_t width=4u*CETTA_COST_KIND_COUNT+64u;
    return extent<=SIZE_MAX/width&&
        prime_replay_read_service(read,"transaction.phase_validation",extent*width,0u);
}
static bool prime_replay_resource_phase_prefix(PrimeReplayComparison *read,
    const PrimeReplayRequestFrame *record,const PrimeReplayRequestFrame *attempt,
    const PrimeReplayResourceBoundary *boundary) {
    if(!prime_replay_resource_phase_budget(read,record,attempt)||
        boundary->phase_count!=attempt->phases.count||boundary->site_count!=attempt->resource.sites.count||
        !prime_replay_phases_complete(&record->phases,&record->commands)||
        !prime_replay_phases_complete(&attempt->phases,&attempt->commands))return false;
    const PrimeReplayPhases *left[]={&record->phases,&record->resource.sites};
    const PrimeReplayPhases *right[]={&attempt->phases,&attempt->resource.sites};
    for(size_t side=0u;side<2u;side++) {
        const PrimeReplayPhases *a=left[side],*b=right[side];
        if(b->count>a->count||a->incomplete||b->incomplete)return false;
        for(size_t i=0u;i<b->count;i++) {
            const PrimeReplayPhase *x=&a->items[i],*y=&b->items[i];
            if(!prime_replay_read_scalar(read,"transaction.phase.command",x->command,y->command)||
                !prime_replay_read_scalar(read,"transaction.phase.kind",x->kind,y->kind)||
                !(x->kind==CETTA_COST_RESTORE&&!side
                    ? prime_replay_read_identity(read,"transaction.phase.trigger",CETTA_CHECKPOINT_ISSUED_OCCURRENCE,x->trigger,y->trigger)
                    : prime_replay_read_scalar(read,"transaction.phase.trigger",x->trigger,y->trigger))||
                !prime_replay_read_identity(read,"transaction.phase.origin",CETTA_CHECKPOINT_COST_LEDGER,x->origin,y->origin)||
                !prime_replay_read_identity(read,"transaction.phase.occurrence",CETTA_CHECKPOINT_ISSUED_OCCURRENCE,x->occurrence,y->occurrence))return false;
            if(side&&(!prime_replay_resource_site_matches(&record->resource,i)||
                      !prime_replay_resource_site_matches(&attempt->resource,i)))return false;
        }
    }
    return true;
}
static bool prime_replay_resource_permit(void *raw,const CettaSpaceAgendaEvent *event) {
    PrimeReplayResourceRun *run=raw;PrimeReplayResourceWord *w=&run->frame->resource;
    if(event->agenda!=run->control->space_agenda||!w->live_validated||w->refusal||
        !w->event_count||event->kind!=CETTA_AGENDA_EVENT_PRECOMMIT||w->site_open||
        w->scope.events!=w->event_count||
        cetta_space_agenda_transcript_events(w->word)!=w->event_count)return false;
    PrimeReplayResourceInspection inspection={0};
    if(!prime_replay_resource_inspection_enter(run->capsule,&inspection))return false;
    const PrimeReplayResourceBoundary *at=&w->boundaries[w->event_count-1u];
    bool ok=at->kind==event->kind&&at->command==run->frame->commands.command_count&&
        at->site_count==w->sites.count&&at->phase_count==run->frame->phases.count&&
        at->replacement.space&&prime_replay_sources_current(&run->capsule->sources)&&
        prime_replay_transaction_authority(run->capsule,
            space_read_token(cetta_space_agenda_checkpoint_target(event->agenda)),at->replacement);
    if(ok&&run->recorded) {
        PrimeReplayComparison *read=&run->attempt->resource_read;
        const PrimeReplayResourceWord *expected=&run->recorded->resource;
        if(w->event_count>expected->event_count)ok=false;
        const PrimeReplayResourceBoundary *bound=ok?&expected->boundaries[w->event_count-1u]:NULL;
        if(ok)ok=bound->kind==CETTA_AGENDA_EVENT_PRECOMMIT&&bound->command==at->command&&
            bound->phase_count==at->phase_count&&bound->site_count==at->site_count;
        CettaCheckpointIssuance now;prime_replay_issuance(&now);read->right_end=now;
        read->fuel=run->attempt->inspection_remaining;
        size_t prefix=read->identities.length;
        if(ok)ok=cetta_space_agenda_transcript_compare_prefix(expected->word,w->word,&read->read)&&
            prime_replay_compare_commands_in(read,&run->recorded->commands,&run->frame->commands,true)&&
            prime_replay_resource_phase_prefix(read,run->recorded,run->frame,bound)&&
            prime_replay_read_atom_graph(read)&&prime_replay_read_identity_authority(read,prefix);
        run->attempt->inspection_remaining=read->fuel;
    }
    if(ok)ok=prime_replay_capsule_size(run->capsule);
    if(!ok)w->refusal="TransactionProtectedPrecommit";
    prime_replay_resource_inspection_leave(&inspection);return ok;
}

static bool prime_replay_frame_execute_in(PrimeReplayCapsule *capsule,
    PrimeReplayRequestFrame *frame,Atom *holder,const PrimeReplayRequestFrame *recorded,
    PrimeReplayAttempt *attempt) {
    if(frame->entered||g_prime_replay_phases)return false;
    PrimeNativeControl *control=cetta_native_handle_get_owned(capsule->context,"prime-search",holder);
    PrimeNativeControl *reserved=capsule->transaction&&control==prime_replay_original(capsule)?control:NULL;
    PrimeReplayResourceRun resource={capsule,frame,recorded,attempt,control};
    bool observing=false;
    if(capsule->transaction&&(!control||!control->space_agenda||
        !prime_replay_resource_word_open(capsule,frame,control,holder,recorded)))goto finished;
    frame->status=prime_native_owned_top_recover_profile(&frame->top,holder,
        &frame->storage,&frame->pending,&frame->results,true,reserved);
    if(frame->status!=CETTA_CONTINUATION_READY)goto finished;
    if(capsule->transaction&&!cetta_space_agenda_observation_enter(&frame->resource.scope,
        control->space_agenda,prime_replay_resource_observe,prime_replay_resource_permit,
        prime_replay_resource_charge,&resource))goto finished;
    CettaNativeCostObserver observer;
    observing=cetta_native_cost_observer_enter(&observer,cetta_native_cost_receipts_observe,&frame->commands);
    if(observing) {
        CettaNativeCostScope *enclosing=cetta_native_cost_detach_begin();
        CettaNativeCostScope physical;cetta_native_cost_scope_enter(&physical,&frame->physical,0u);
        frame->entered=true;g_prime_replay_phases=&frame->phases;
        eval_gc_external_owner_enter();
        frame->status=prime_native_owned_top_resume_in_context(&frame->top,&frame->storage,frame->allowance,true);
        eval_gc_external_owner_leave();
        g_prime_replay_phases=NULL;
        cetta_native_cost_scope_leave(&physical);cetta_native_cost_detach_end(enclosing);
        cetta_native_cost_observer_leave(&observer);
    }
finished:
    if(frame->resource.scope.entered)cetta_space_agenda_observation_leave(&frame->resource.scope);
    /* The stored disposition is owned; the normally returning stack context
     * and its borrowed live-provider pointer are never retained. */
    frame->resource.scope.context=NULL;frame->resource.scope.agenda=NULL;
    prime_replay_comparison_account(&frame->resource.origin);
    prime_replay_issuance(&frame->end);
    if(!observing)return false;
    CettaNativeCostLedger *slices[]={&frame->physical};
    frame->accounted=cetta_native_cost_join(slices,1u);
    frame->completed=frame->top.committed&&!frame->pending.len&&frame->results.len==1u&&
        frame->accounted&&cetta_native_cost_receipts_complete(&frame->commands)&&
        prime_replay_phases_complete(&frame->phases,&frame->commands)&&
        !frame->physical.total.overflow&&!frame->physical.total.trace_incomplete;
    if(capsule->transaction) {
        PrimeReplayResourceWord *w=&frame->resource;
        bool complete=!w->refusal&&!w->site_open&&w->live_validated&&w->origin.matched&&
            !w->scope.refusal&&w->event_count==w->scope.events&&
            w->event_count==cetta_space_agenda_transcript_events(w->word)&&
            w->scope.commits==cetta_space_agenda_transcript_commits(w->word)&&
            cetta_space_agenda_transcript_complete(w->word);
        for(size_t i=0u;complete&&i<w->sites.count;i++) {
            cetta_native_cost_charge(CETTA_COST_ANALYSIS_STEP,1u);
            complete=prime_replay_resource_site_matches(w,i);
        }
        frame->completed=frame->completed&&complete;
    }
    return frame->completed;
}
static bool prime_replay_frame_execute(PrimeReplayCapsule *capsule,
    PrimeReplayRequestFrame *frame,Atom *holder) {
    return prime_replay_frame_execute_in(capsule,frame,holder,NULL,NULL);
}
static void prime_replay_attempt_free(CettaLibraryContext *context,PrimeReplayAttempt *attempt) {
    if(!attempt)return;
    prime_replay_comparison_free(&attempt->source_read);prime_replay_comparison_free(&attempt->entry_read);
    prime_replay_comparison_free(&attempt->copy_read);prime_replay_comparison_free(&attempt->post_read);
    prime_replay_comparison_free(&attempt->resource_read);
    prime_replay_frame_free(context,&attempt->frame);free(attempt);
}
static bool prime_replay_resource_full(PrimeReplayComparison *read,
    const PrimeReplayRequestFrame *record,const PrimeReplayRequestFrame *attempt) {
    const PrimeReplayResourceWord *a=&record->resource,*b=&attempt->resource;
    if(!prime_replay_resource_phase_budget(read,record,attempt)||
        !a->live_validated||!b->live_validated||a->refusal||b->refusal||
        a->event_count!=b->event_count||a->sites.count!=b->sites.count||
        !cetta_space_agenda_transcript_compare(a->word,b->word,&read->read)||
        !prime_replay_compare_commands(read,&record->commands,&attempt->commands)||
        !prime_replay_compare_phases(read,record,attempt))return false;
    for(size_t i=0u;i<a->event_count;i++) {
        const PrimeReplayResourceBoundary *x=&a->boundaries[i],*y=&b->boundaries[i];
        if(!prime_replay_read_scalar(read,"transaction.event.command",x->command,y->command)||
            !prime_replay_read_scalar(read,"transaction.event.phases",x->phase_count,y->phase_count)||
            !prime_replay_read_scalar(read,"transaction.event.sites",x->site_count,y->site_count)||
            !prime_replay_read_scalar(read,"transaction.event.kind",x->kind,y->kind)||
            !cetta_space_agenda_checkpoint_read_token("transaction.event.replacement",x->replacement,y->replacement,&read->read))return false;
    }
    for(size_t i=0u;i<a->sites.count;i++) {
        const PrimeReplayPhase *x=&a->sites.items[i],*y=&b->sites.items[i];
        if(!prime_replay_read_scalar(read,"transaction.site",x->trigger,y->trigger)||
            !prime_replay_read_scalar(read,"transaction.site.command",x->command,y->command)||
            !prime_replay_read_scalar(read,"transaction.site.kind",x->kind,y->kind)||
            !prime_replay_read_identity(read,"transaction.site.origin",CETTA_CHECKPOINT_COST_LEDGER,x->origin,y->origin)||
            !prime_replay_read_identity(read,"transaction.site.occurrence",CETTA_CHECKPOINT_ISSUED_OCCURRENCE,x->occurrence,y->occurrence)||
            !prime_replay_resource_site_matches(a,i)||!prime_replay_resource_site_matches(b,i))return false;
    }
    return prime_replay_read_atom_graph(read);
}

static bool prime_replay_transaction_replay(PrimeReplayCapsule *capsule,uint64_t inspection) {
    capsule->disposition="ReplayRefused";
    if(!capsule->capture_complete){capsule->refusal="UnadmittedCapture";return false;}
    if(capsule->failed_attempt){capsule->refusal="HeldFailedAttempt";return false;}
    if(capsule->replayed_count==capsule->frame_count){capsule->refusal="NoRecordedRequest";return false;}
    PrimeNativeControl *original=prime_replay_original(capsule);
    PrimeReplayCheckpoint *accepted=prime_replay_accepted_checkpoint(capsule);
    PrimeNativeControl *prior=prime_replay_checkpoint_control(capsule->context,accepted);
    PrimeReplayRequestFrame *record=&capsule->frames[capsule->replayed_count];
    PrimeNativeControl *entry=prime_replay_checkpoint_control(capsule->context,&record->entry);
    PrimeNativeControl *post=prime_replay_checkpoint_control(capsule->context,&record->post);
    if(!original||!capsule->source_reserved||!original->leased||!prior||!entry||!post||
        !record->completed||!record->post_ready||!prime_replay_target_witness(&record->entry)||
        !prime_replay_target_witness(&record->post)||
        !space_read_token_is_current(capsule->target_expected)||
        !prime_replay_sources_current(&capsule->sources)||
        !prime_replay_checkpoint_current(prior,&capsule->storage)) {
        capsule->refusal="UnavailableRecordedOrAcceptedOwner";return false;
    }
    if(!prime_replay_capsule_size(capsule))return false;
    PrimeReplayAttempt *attempt=calloc(1u,sizeof(*attempt));
    if(!attempt){capsule->refusal="AttemptCapacity";return false;}
    capsule->failed_attempt=attempt;capsule->replay_started=true;
    attempt->inspection_requested=attempt->inspection_remaining=inspection;
    attempt->frame_index=capsule->replayed_count;attempt->frame.allowance=record->allowance;
    const PrimeReplayIdentityRelation *prefix=capsule->accepted_attempt?
        &capsule->accepted_attempt->resource_read.identities:NULL;
    CettaCheckpointIssuance current;prime_replay_issuance(&current);
    /* Original recording legitimately advanced its mutable world. Its
     * initial correspondence is the held completed construction witness;
     * immutable source/current external target authority is checked above. */
    if(!prime_replay_target_witness(&capsule->initial)) {
        attempt->refusal="CapturedTargetWitness";goto refused;
    }
    {
        PrimeReplayComparison *read=&attempt->entry_read;
        bool ok=prime_replay_comparison_open(capsule,attempt,read,NULL,
            record->begin,record->begin,current,current);
        read->transaction=true;
        if(ok)ok=prefix ? prime_replay_transaction_prefix(read,prefix,&record->entry,NULL)
                        : prime_replay_transaction_initial(read,&record->entry,&capsule->initial);
        if(ok)ok=prime_replay_comparison_run(read,entry,prior);
        prime_replay_comparison_close(attempt,read);
        if(!ok){attempt->refusal="AcceptedEntryMismatch";goto refused;}
    }
    CettaNativeCostReceipts commands={0};
    if(!cetta_native_cost_receipts_init(&commands,record->commands.command_count,
        record->commands.destination_count)) {attempt->refusal="AttemptCommandCapacity";goto refused;}
    if(!prime_replay_frame_begin(&attempt->frame,record->allowance,commands,true)) {
        attempt->refusal="AttemptInitialization";goto refused;
    }
    if(!prime_replay_checkpoint_prepare_profile(capsule->context,accepted->holder,&capsule->sources,
        capsule->node_limit,capsule->byte_limit,&attempt->frame.entry,true,false,false)) {
        attempt->refusal="AttemptPreparation";goto refused;
    }
    PrimeNativeControl *control=prime_replay_checkpoint_control(capsule->context,&attempt->frame.entry);
    {
        CettaCheckpointIssuance after;prime_replay_issuance(&after);
        PrimeReplayComparison *read=&attempt->copy_read;
        bool ok=prime_replay_comparison_open(capsule,attempt,read,NULL,
            current,current,attempt->frame.begin,after);
        read->transaction=true;read->target_copy=&attempt->frame.entry.target;
        if(ok)ok=prime_replay_comparison_run(read,prior,control);
        prime_replay_comparison_close(attempt,read);
        if(!ok){attempt->refusal="PreparedCopyMismatch";goto refused;}
    }
    {
        PrimeReplayComparison *read=&attempt->resource_read;
        bool ok=prime_replay_comparison_open(capsule,attempt,read,NULL,
            record->begin,record->end,attempt->frame.begin,attempt->frame.begin);
        read->transaction=true;read->left=original;read->right=control;
        const PrimeReplayIdentityRelation *base=prefix?prefix:&capsule->initial.ambient_read->identities;
        if(ok)ok=prime_replay_transaction_prefix(read,base,NULL,&attempt->frame.entry)&&
            prime_replay_targets(read,original,control)&&
            prime_replay_read_arena(read,"transaction.word_owner",&record->storage,&attempt->frame.storage);
        /* Physical region addresses remain live; the retained original
         * current token is the actual request-entry authority. */
        read->left_target=record->entry.target.source;
        attempt->inspection_remaining=read->fuel;
        if(!ok){attempt->refusal="TransactionWordPreparation";goto refused;}
    }
    if(!prime_replay_capsule_size(capsule)){attempt->refusal="AttemptStorageBeforeRun";goto refused;}
    if(!prime_replay_frame_execute_in(capsule,&attempt->frame,attempt->frame.entry.holder,record,attempt)) {
        attempt->refusal="AttemptRequestIncomplete";goto refused;
    }
    if(!prime_replay_capsule_size(capsule)){attempt->refusal="AttemptStorageAfterRun";goto refused;}
    {
        PrimeReplayComparison *word=&attempt->resource_read;
        word->fuel=attempt->inspection_remaining;word->right_end=attempt->frame.end;
        size_t fixed=word->identities.length;
        bool ok=prime_replay_resource_full(word,record,&attempt->frame)&&
            prime_replay_read_identity_authority(word,fixed);
        attempt->inspection_remaining=word->fuel;
        if(!ok){attempt->refusal="TransactionWordMismatch";goto refused;}
        PrimeReplayComparison *read=&attempt->post_read;
        ok=prime_replay_comparison_open(capsule,attempt,read,NULL,
            record->begin,record->end,attempt->frame.begin,attempt->frame.end);
        read->transaction=true;
        if(ok)ok=prime_replay_transaction_identity_commutes(read,word,&record->post);
        fixed=read->identities.length;
        if(ok)ok=prime_replay_comparison_run(read,post,control)&&
            prime_replay_compare_frame_product(capsule,read,record,&attempt->frame,original,control)&&
            prime_replay_read_atom_graph(read)&&prime_replay_read_identity_authority(read,fixed);
        word->fuel=read->fuel;
        if(ok)ok=prime_replay_transaction_atoms_commute(word,read,&record->post,
            &record->resource,&attempt->frame.resource)&&prime_replay_read_atom_graph(word)&&
            prime_replay_sources_current(&capsule->sources);
        read->fuel=word->fuel;read->matched=word->matched=ok;
        prime_replay_comparison_close(attempt,read);
        if(!ok){attempt->refusal="ReplayPostMismatch";goto refused;}
    }
    prime_replay_comparison_account(&attempt->resource_read);
    if(!prime_replay_capsule_size(capsule)){attempt->refusal="AcceptedStorageLimit";goto refused;}
    {
        PrimeReplayAttempt *previous=capsule->accepted_attempt;
        capsule->accepted_attempt=attempt;capsule->failed_attempt=NULL;capsule->replayed_count++;
        attempt->accepted=true;capsule->disposition="Replayed";capsule->refusal=NULL;
        prime_replay_attempt_free(capsule->context,previous);
    }
    return true;
refused:
    prime_replay_comparison_account(&attempt->resource_read);
    capsule->refusal=attempt->refusal?attempt->refusal:"TransactionReplayComparison";
    return false;
}

static bool prime_replay_capsule_replay(PrimeReplayCapsule *capsule,uint64_t inspection) {
    if(capsule->transaction)return prime_replay_transaction_replay(capsule,inspection);
    capsule->disposition="ReplayRefused";
    if(!capsule->capture_complete){capsule->refusal="UnadmittedCapture";return false;}
    if(capsule->failed_attempt){capsule->refusal="HeldFailedAttempt";return false;}
    if(capsule->replayed_count==capsule->frame_count){capsule->refusal="NoRecordedRequest";return false;}
    PrimeNativeControl *original=prime_replay_original(capsule);
    PrimeReplayCheckpoint *accepted=prime_replay_accepted_checkpoint(capsule);
    PrimeNativeControl *prior=prime_replay_checkpoint_control(capsule->context,accepted);
    PrimeReplayRequestFrame *record=&capsule->frames[capsule->replayed_count];
    PrimeNativeControl *entry=prime_replay_checkpoint_control(capsule->context,&record->entry);
    PrimeNativeControl *post=prime_replay_checkpoint_control(capsule->context,&record->post);
    if(!original||!capsule->source_reserved||!original->leased||!prior||!entry||!post||
        !record->completed||!record->post_ready||!prime_replay_sources_current(&capsule->sources)||
        !prime_replay_checkpoint_current(prior,&capsule->storage)) {
        capsule->refusal="UnavailableRecordedOrAcceptedOwner";return false;
    }
    if(!prime_replay_capsule_size(capsule))return false;
    PrimeReplayAttempt *attempt=calloc(1u,sizeof(*attempt));
    if(!attempt){capsule->refusal="AttemptCapacity";return false;}
    capsule->failed_attempt=attempt;capsule->replay_started=true;
    attempt->inspection_requested=attempt->inspection_remaining=inspection;
    attempt->frame_index=capsule->replayed_count;
    attempt->frame.allowance=record->allowance;
    const PrimeReplayIdentityRelation *prefix=capsule->accepted_attempt?
        &capsule->accepted_attempt->post_read.identities:NULL;
    CettaCheckpointIssuance current;prime_replay_issuance(&current);
    if(!capsule->replayed_count) {
        bool ok=prime_replay_comparison_open(capsule,attempt,&attempt->source_read,NULL,
            record->begin,record->begin,current,current)&&
            (capsule->initial.ambient_services_captured
                ? prime_replay_comparison_run_origin(&attempt->source_read,original,entry,
                    &capsule->initial,prime_replay_checkpoint_control(capsule->context,&capsule->initial))
                : prime_replay_comparison_run(&attempt->source_read,original,entry));
        prime_replay_comparison_close(attempt,&attempt->source_read);
        if(!ok){attempt->refusal="CapturedSourceMismatch";goto refused;}
    }
    {
        bool ok=prime_replay_comparison_open(capsule,attempt,&attempt->entry_read,prefix,
            record->begin,record->begin,current,current)&&
            prime_replay_comparison_run(&attempt->entry_read,entry,prior);
        prime_replay_comparison_close(attempt,&attempt->entry_read);
        if(!ok){attempt->refusal="AcceptedEntryMismatch";goto refused;}
    }
    CettaNativeCostReceipts commands={0};
    if(!cetta_native_cost_receipts_init(&commands,record->commands.command_count,
        record->commands.destination_count)) {attempt->refusal="AttemptCommandCapacity";goto refused;}
    if(!prime_replay_frame_begin(&attempt->frame,record->allowance,commands,true)) {
        attempt->refusal="AttemptInitialization";goto refused;
    }
    if(!prime_replay_checkpoint_prepare(capsule->context,accepted->holder,&capsule->sources,
        capsule->node_limit,capsule->byte_limit,&attempt->frame.entry)) {attempt->refusal="AttemptPreparation";goto refused;}
    PrimeNativeControl *control=prime_replay_checkpoint_control(capsule->context,&attempt->frame.entry);
    {
        CettaCheckpointIssuance after;prime_replay_issuance(&after);
        bool ok=prime_replay_comparison_open(capsule,attempt,&attempt->copy_read,NULL,
            current,current,attempt->frame.begin,after)&&
            prime_replay_comparison_run(&attempt->copy_read,prior,control);
        prime_replay_comparison_close(attempt,&attempt->copy_read);
        if(!ok){attempt->refusal="PreparedCopyMismatch";goto refused;}
    }
    if(!prime_replay_capsule_size(capsule)){attempt->refusal="AttemptStorageBeforeRun";goto refused;}
    if(!prime_replay_frame_execute(capsule,&attempt->frame,attempt->frame.entry.holder)) {
        attempt->refusal="AttemptUncommittedRequest";goto refused;
    }
    if(!prime_replay_capsule_size(capsule)){attempt->refusal="AttemptStorageAfterRun";goto refused;}
    {
        bool ok=prime_replay_comparison_open(capsule,attempt,&attempt->post_read,prefix,
            record->begin,record->end,attempt->frame.begin,attempt->frame.end);
        size_t fixed=attempt->post_read.identities.length;
        if(ok)ok=prime_replay_comparison_run(&attempt->post_read,post,control);
        PrimeNativeControl *recording=prime_replay_checkpoint_control(capsule->context,&capsule->recording);
        if(ok)ok=prime_replay_compare_frame_product(capsule,&attempt->post_read,record,
            &attempt->frame,recording,control)&&prime_replay_read_atom_graph(&attempt->post_read)&&
            prime_replay_read_identity_authority(&attempt->post_read,fixed)&&
            prime_replay_sources_current(&capsule->sources);
        attempt->post_read.matched=ok;
        prime_replay_comparison_close(attempt,&attempt->post_read);
        if(!ok){attempt->refusal="ReplayPostMismatch";goto refused;}
    }
    if(!prime_replay_capsule_size(capsule)){attempt->refusal="AcceptedStorageLimit";goto refused;}
    {
        PrimeReplayAttempt *previous=capsule->accepted_attempt;
        capsule->accepted_attempt=attempt;capsule->failed_attempt=NULL;capsule->replayed_count++;
        attempt->accepted=true;capsule->disposition="Replayed";capsule->refusal=NULL;
        prime_replay_attempt_free(capsule->context,previous);
    }
    return true;
refused:
    capsule->refusal=attempt->refusal?attempt->refusal:"ReplayComparison";
    return false;
}
