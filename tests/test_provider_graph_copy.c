#include "petta_search_machine.c"
#include "parser.h"

typedef struct {
    size_t references, retains, releases;
    uint64_t *invalidate_revision;
    size_t invalidate_at;
} ProbeLease;

typedef struct ProbeAuthority {
    uint64_t revision;
    size_t remaining;
    unsigned domain;
    struct ProbeAuthority *invalidate_source;
    unsigned transfer_calls, invalidation_call;
    const void *resource_scope;
    const void *resource_identity;
    int resource_kind;
    ProbeLease *lease;
} ProbeAuthority;
static CettaBranchAdmission probe_admission(void *raw, Space *space) {
    (void)space;ProbeAuthority *authority=raw;
    return (CettaBranchAdmission){.status=CETTA_BRANCH_ADMISSION_AVAILABLE,
        .capacity=CETTA_BRANCH_CAPTURE_MULTI_SHOT,
        .authority={.words={0x4f574e4544u,authority->revision},.length=2u}};
}
static bool probe_permit(void *raw) {
    ProbeAuthority *authority=raw;
    if(!authority->remaining)return false;
    authority->remaining--;return true;
}
static Atom *probe_parse(Arena *arena,const char *text) {
    Atom **forms=NULL;int count=parse_metta_text(text,arena,&forms);
    assert(count==1);Atom *out=forms[0];free(forms);return out;
}
static bool provider_graph_case(size_t cut,bool distinct,bool provider_first,bool mapped) {
    Arena external,answers,seed_arena,parent_source,parent_destination,separate,separate_destination;
    Arena *arenas[]={&external,&answers,&seed_arena,&parent_source,&parent_destination,&separate,&separate_destination};
    for(size_t i=0u;i<sizeof(arenas)/sizeof(*arenas);i++)arena_init_detached(arenas[i]);
    TermUniverse universe;term_universe_init(&universe);term_universe_set_persistent_arena(&universe,&external);
    Space space;space_init_with_universe(&space,&universe);
    space_add(&space,probe_parse(&external,"(= (provider-graph-choice) 1)"));
    space_add(&space,probe_parse(&external,"(= (provider-graph-choice) 1)"));
    space_add(&space,probe_parse(&external,"(= (provider-graph-choice) 2)"));
    Atom *query=probe_parse(&answers,"(let $x (provider-graph-choice) (Pair $x $x))");
    Atom *value=atom_expr2(&seed_arena,atom_symbol(&seed_arena,"ProviderValue"),atom_int(&seed_arena,7));
    PrimeNeedSnapshot root={0},allocated,running,completed;uint64_t cell=0u;
    assert(prime_need_snapshot_begin(&root) &&
        prime_need_snapshot_allocate(&seed_arena,&root,value,&allocated,&cell) &&
        prime_need_snapshot_start_evaluation(&seed_arena,&allocated,cell,19u,&running) &&
        prime_need_snapshot_resolve_value(&seed_arena,&running,cell,19u,value,&completed));
    Bindings seed;bindings_init(&seed);VarId visible=fresh_var_id();
    assert(bindings_add_id(&seed,visible,SYMBOL_ID_NONE,value));*bindings_need_mut(&seed)=completed;
    ProbeAuthority authority={.revision=61u,.remaining=cut};
    PettaMachineHost host={.context=&authority,.admit_branch_capture=probe_admission,.permit_transition=probe_permit};
    PettaMachine machine={0};assert(petta_machine_init(&machine,&space,&answers,query,&seed,&host));
    Atom *answer=NULL;Bindings environment;
    PettaMachineStep step=petta_machine_next(&machine,&answer,&environment);
    assert(step==PETTA_MACHINE_STEP_SUSPENDED || step==PETTA_MACHINE_STEP_ANSWER);bindings_free(&environment);
    CettaStorageCensus live_census={0};
    PettaMachineStats before_inspection=machine.impl->stats;
    petta_machine_storage_census(&machine,&live_census);cetta_storage_finish(&live_census);
    if(live_census.incomplete)fprintf(stderr,"live census cut %zu: %s\n",cut,live_census.incomplete);
    assert(!live_census.incomplete && !live_census.invalidated && !live_census.overflow &&
        live_census.retained>=sizeof(*machine.impl));
    size_t live_retained=live_census.retained,live_objects=live_census.length;
    petta_machine_storage_census(&machine,&live_census);cetta_storage_finish(&live_census);
    assert(live_census.retained==live_retained && live_census.length==live_objects &&
        !memcmp(&machine.impl->stats,&before_inspection,sizeof(before_inspection)));
    cetta_storage_census_free(&live_census);
    PettaPullFrame active_pull={0};machine.impl->pull_frame=&active_pull;
    petta_machine_storage_census(&machine,&live_census);cetta_storage_finish(&live_census);
    assert(live_census.incomplete && !strcmp(live_census.incomplete,"ActiveMachinePull"));
    machine.impl->pull_frame=NULL;cetta_storage_census_free(&live_census);
    CettaOwnedContinuation image={0};
    assert(cetta_continuation_capture(petta_machine_continuation_machine(&machine),&image)==CETTA_CONTINUATION_READY);
    const PettaOwnedContinuationImpl *source=image.payload;
    const Bindings *source_bindings=&source->bindings.bindings.current;
    CettaStorageCensus census={0};
    size_t references=atomic_load_explicit(&source->terms.pool->references,memory_order_relaxed);
    PettaContinuationAuthorityComponent saved_authority=source->authority;
    PettaMachineStats saved_stats=((PettaMachineImpl *)machine.impl)->stats;
    cetta_owned_continuation_storage_census(&image,&census);cetta_storage_finish(&census);
    assert(!census.incomplete && !census.invalidated && !census.overflow &&
        census.retained>=sizeof(*source)+sizeof(*source->terms.pool));
    size_t retained=census.retained,objects=census.length;
    cetta_owned_continuation_storage_census(&image,&census);cetta_storage_finish(&census);
    assert(census.retained==retained && census.length==objects && census.repeated>0u &&
        atomic_load_explicit(&source->terms.pool->references,memory_order_relaxed)==references &&
        memcmp(&source->authority,&saved_authority,sizeof(saved_authority))==0 &&
        memcmp(&((PettaMachineImpl *)machine.impl)->stats,&saved_stats,sizeof(saved_stats))==0);
    cetta_storage_census_free(&census);
    PrimeNeedCellView source_cell;assert(prime_need_snapshot_lookup(bindings_need_view(source_bindings),cell,&source_cell));
    Bindings outside;bindings_init(&outside);assert(bindings_clone(&outside,source_bindings));
    assert(bindings_lookup_value_id(&outside,visible).skeleton==source_cell.origin && source_cell.origin==source_cell.cached);
    if(distinct)assert(prime_need_snapshot_promote(&separate,bindings_need_mut(&outside)));
    PrimeNeedCellView outside_cell;assert(prime_need_snapshot_lookup(bindings_need_view(&outside),cell,&outside_cell));
    bool source_shared=bindings_need_view(&outside)->top==bindings_need_view(source_bindings)->top;
    assert(source_shared==!distinct && atom_eq(outside_cell.cached,source_cell.cached));
    PettaContinuationCopyGroup *group=NULL;
    PettaContinuationTermPool *pool=NULL;
    AtomDeepCopySession *atoms=NULL;
    if(mapped) {
        assert(petta_continuation_copy_group_new(&image,NULL,&group)==CETTA_CONTINUATION_READY);
        AtomDeepCopyArenaPair provider_pair;
        assert(petta_continuation_copy_group_arena_pair(group,&provider_pair));
        assert(provider_pair.source==&source->terms.pool->owner);
        pool=group->destination;
        atoms=atom_deep_copy_session_new(&parent_destination);
        AtomDeepCopyArenaPair pairs[]={{&parent_source,&parent_destination},provider_pair,
            {&separate,&separate_destination},{&external,&external},{&answers,&answers}};
        assert(atoms && atom_deep_copy_session_set_arena_map(atoms,pairs,5u));
    } else {
        pool=petta_continuation_term_pool_new();assert(pool);
        atoms=atom_deep_copy_session_new(&pool->owner);assert(atoms);
    }
    Bindings outside_copy;bindings_init(&outside_copy);assert(bindings_clone(&outside_copy,&outside));
    PettaOwnedContinuationImpl *copied=NULL;
    CettaOwnedContinuation prepared={0};
    Arena *selected=mapped?&parent_destination:&pool->owner;
    if(provider_first) {
        if(mapped) {
            assert(petta_continuation_copy_group_prepare(group,&image,atoms,&prepared)==CETTA_CONTINUATION_READY);
            copied=prepared.payload;
        } else copied=petta_owned_continuation_clone_to_term_pool(source,pool,atoms);
    }
    assert(bindings_copy_owned_atoms_with_session(&outside_copy,selected,atoms));
    if(!provider_first) {
        if(mapped) {
            assert(petta_continuation_copy_group_prepare(group,&image,atoms,&prepared)==CETTA_CONTINUATION_READY);
            copied=prepared.payload;
        } else copied=petta_owned_continuation_clone_to_term_pool(source,pool,atoms);
    }
    assert(copied && atom_deep_copy_session_targets(atoms,selected));
    if(mapped) assert(prepared.provider==image.provider && !prepared.occurrence_id &&
        !prepared.parent_occurrence_id && !prepared.resume_store && !prepared.resume_lease &&
        memcmp(&copied->authority,&source->authority,sizeof(copied->authority))==0);
    bool copied_shared=bindings_need_view(&outside_copy)->top==bindings_need_view(&copied->bindings.bindings.current)->top;
    PrimeNeedCellView copied_cell;
    assert(prime_need_snapshot_lookup(bindings_need_view(&copied->bindings.bindings.current),cell,&copied_cell));
    bool payload_shared=bindings_lookup_value_id(&outside_copy,visible).skeleton==copied_cell.origin &&
        copied_cell.origin==copied_cell.cached;
    /* Inject an unlicensed external operand into an unpublished sibling. The
     * actual provider copy must reject it without changing the original. */
    PettaOwnedContinuationImpl unlicensed=*source;
    assert(bindings_builder_clone(&unlicensed.bindings.bindings,&source->bindings.bindings));
    assert(bindings_builder_add_id_fresh(&unlicensed.bindings.bindings,fresh_var_id(),SYMBOL_ID_NONE,
        atom_space(&source->terms.pool->owner,&space)));
    PettaContinuationTermPool *refused_pool=petta_continuation_term_pool_new();
    AtomDeepCopySession *refused_atoms=atom_deep_copy_session_new(&refused_pool->owner);
    const PrimeNeedFrame *original_top=bindings_need_view(source_bindings)->top;
    PettaOwnedContinuationImpl *refused=petta_owned_continuation_clone_to_term_pool(&unlicensed,refused_pool,refused_atoms);
    bool refused_ok=!refused && bindings_need_view(source_bindings)->top==original_top;
    petta_owned_continuation_impl_free(refused);atom_deep_copy_session_free(refused_atoms);
    petta_continuation_term_pool_release(refused_pool);bindings_builder_free(&unlicensed.bindings.bindings);
    atom_deep_copy_session_free(atoms);
    if(mapped)petta_continuation_copy_group_free(group);else petta_continuation_term_pool_release(pool);
    cetta_owned_continuation_destroy(&image);petta_machine_destroy(&machine);
    bindings_free(&seed);bindings_free(&outside);arena_free(&seed_arena);arena_free(&parent_source);arena_free(&separate);
    assert(prime_need_snapshot_lookup(bindings_need_view(&copied->bindings.bindings.current),cell,&copied_cell) &&
        copied_cell.cache_state==PRIME_NEED_CACHE_VALUE && atom_is_symbol(copied_cell.cached->expr.elems[0],"ProviderValue"));
    bool passed=source_shared==copied_shared && payload_shared && refused_ok;
    printf("{\"mapped\":%s,\"cut\":%zu,\"distinct\":%s,\"provider_first\":%s,\"source_shared_top\":%s,\"copied_shared_top\":%s,\"payload_alias\":%s,\"unlicensed_refused\":%s,\"passed\":%s}\n",mapped?"true":"false",cut,distinct?"true":"false",provider_first?"true":"false",source_shared?"true":"false",copied_shared?"true":"false",payload_shared?"true":"false",refused_ok?"true":"false",passed?"true":"false");
    petta_owned_continuation_impl_free(copied);bindings_free(&outside_copy);
    arena_free(&separate_destination);arena_free(&parent_destination);space_free(&space);term_universe_free(&universe);
    arena_free(&answers);arena_free(&external);
    return passed;
}
static void probe_lease_retain(void *raw) {
    ProbeLease *lease=raw;
    assert(lease->references && lease->references<SIZE_MAX);
    lease->references++;lease->retains++;
    if(lease->invalidate_revision && lease->retains==lease->invalidate_at)
        (*lease->invalidate_revision)++;
}
static void probe_lease_release(void *raw) {
    ProbeLease *lease=raw;assert(lease->references);lease->references--;lease->releases++;
}

static bool probe_capture_resource(void *raw,const Atom *atom,PettaMachineResourceLease *out) {
    ProbeAuthority *authority=raw;
    if(!authority->resource_scope || !atom || atom->kind!=ATOM_GROUNDED ||
        atom->ground.gkind!=authority->resource_kind || atom->ground.ptr!=authority->resource_identity)return false;
    *out=(PettaMachineResourceLease){.scope=authority->resource_scope,.owner=authority->lease,
        .retain=probe_lease_retain,.release=probe_lease_release};
    return true;
}

static void provider_resource_case(unsigned carrier) {
    Arena external,answers,seed_arena,parent_source,parent_destination;
    Arena *arenas[]={&external,&answers,&seed_arena,&parent_source,&parent_destination};
    for(size_t i=0u;i<sizeof(arenas)/sizeof(*arenas);i++)arena_init_detached(arenas[i]);
    TermUniverse universe;term_universe_init(&universe);term_universe_set_persistent_arena(&universe,&external);
    Space space,other;space_init_with_universe(&space,&universe);space_init_with_universe(&other,&universe);
    unsigned scope_id=1u,wrong_scope=2u;StateCell state={0};ProbeLease lease={.references=1u};
    const void *identity=carrier==4u?(const void *)&state:(const void *)&space;
    Atom *resource=carrier==4u?atom_state(&seed_arena,&state):atom_space(&seed_arena,&space);
    Atom *value=atom_expr2(&seed_arena,atom_symbol(&seed_arena,"HeldResource"),resource);
    Bindings seed;bindings_init(&seed);uint64_t cell=0u;
    if(carrier==0u || carrier==4u)assert(bindings_add_id(&seed,fresh_var_id(),SYMBOL_ID_NONE,value));
    if(carrier==1u || carrier==3u) {
        PrimeNeedSnapshot root={0},allocated,running,completed;
        assert(prime_need_snapshot_begin(&root) &&
            prime_need_snapshot_allocate(&seed_arena,&root,value,&allocated,&cell) &&
            prime_need_snapshot_start_evaluation(&seed_arena,&allocated,cell,91u,&running) &&
            prime_need_snapshot_resolve_value(&seed_arena,&running,cell,91u,value,&completed));
        *bindings_need_mut(&seed)=completed;
    }
    if(carrier==2u) {
        CettaWeightLedger empty={0},weighted;
        assert(cetta_weight_ledger_append(&seed_arena,&empty,71u,81u,value,&weighted));
        *bindings_weight_mut(&seed)=weighted;
    }
    if(carrier==4u) {
        PrimeNeedBranchState empty={0},written;Atom *seven=atom_int(&seed_arena,7);
        assert(prime_need_branch_state_begin(&seed_arena,&empty) &&
            prime_need_branch_state_write(&seed_arena,&empty,&state,seven,seven,&written));
        *bindings_branch_state_mut(&seed)=written;
    }
    ProbeAuthority authority={.revision=61u,.resource_scope=&scope_id,.resource_identity=identity,
        .resource_kind=carrier==4u?GV_STATE:GV_SPACE,.lease=&lease};
    PettaMachineHost host={.context=&authority,.admit_branch_capture=probe_admission,
        .permit_transition=probe_permit,.resource_scope=&scope_id,.capture_resource=probe_capture_resource};
    PettaMachine machine={0};
    assert(petta_machine_init(&machine,&space,&answers,atom_symbol(&answers,"ResourceIdle"),&seed,&host));
    if(carrier==3u) {
        BindingsBuilder *builder=&machine.impl->search.bindings;
        (void)bindings_builder_save(builder);
        prime_need_snapshot_init(bindings_need_mut(&builder->current));
        assert(builder->prime_trail_len && builder->prime_trail[0].prime_need.top);
    }
    CettaOwnedContinuation image={0};
    assert(cetta_continuation_capture(petta_machine_continuation_machine(&machine),&image)==CETTA_CONTINUATION_READY);
    PettaOwnedContinuationImpl *source=image.payload;
    assert(source->terms.pool->resource_len==1u);
    PettaContinuationCopyGroup *group=NULL;
    assert(petta_continuation_copy_group_new(&image,&wrong_scope,&group)==CETTA_CONTINUATION_UNSUPPORTED && !group);
    assert(petta_continuation_copy_group_new(&image,&scope_id,&group)==CETTA_CONTINUATION_READY);
    AtomDeepCopyArenaPair pair;
    assert(petta_continuation_copy_group_arena_pair(group,&pair));
    AtomDeepCopySession *atoms=atom_deep_copy_session_new(&parent_destination);
    AtomDeepCopyArenaPair pairs[]={{&parent_source,&parent_destination},pair,
        {&external,&external},{&answers,&answers}};
    assert(atoms && atom_deep_copy_session_set_arena_map(atoms,pairs,4u));
    BindingsBuilder outside;
    assert(bindings_builder_clone(&outside,&source->bindings.bindings));
    AtomDeepCopyDestinationScope selected={0};
    assert(atom_deep_copy_destination_enter(atoms,pair.destination,&selected));
    assert(bindings_builder_copy_owned_atoms_with_session(&outside,pair.destination,atoms));
    assert(atom_deep_copy_destination_leave(&selected));
    /* The complete retained Need path is already in the memo. Losing resource
     * authority must still refuse this provider's later publication. */
    PettaContinuationResource original=source->terms.pool->resources[0];
    CettaOwnedContinuation copy={0};
    for(unsigned fault=0u;fault<4u;fault++) {
        if(fault==0u)source->terms.pool->resources[0].kind=original.kind==GV_STATE?GV_SPACE:GV_STATE;
        if(fault==1u)source->terms.pool->resources[0].identity=&other;
        if(fault==2u)source->terms.pool->resources[0].lease.scope=&wrong_scope;
        if(fault==3u)source->terms.pool->resource_len=0u;
        assert(petta_continuation_copy_group_prepare(group,&image,atoms,&copy)==CETTA_CONTINUATION_UNSUPPORTED);
        assert(!copy.payload && !copy.provider && image.payload==source &&
            atom_deep_copy_session_targets(atoms,&parent_destination));
        source->terms.pool->resources[0]=original;source->terms.pool->resource_len=1u;
    }
    assert(petta_continuation_copy_group_prepare(group,&image,atoms,&copy)==CETTA_CONTINUATION_READY);
    PettaOwnedContinuationImpl *moved=copy.payload;
    assert(moved && moved->terms.pool==group->destination && moved->terms.pool->resource_len==1u &&
        moved->terms.pool->resources[0].identity==identity &&
        moved->terms.pool->resources[0].lease.scope==&scope_id);
    assert(bindings_need_view(&moved->bindings.bindings.current)->top==bindings_need_view(&outside.current)->top);
    if(carrier==3u)assert(moved->bindings.bindings.prime_trail[0].prime_need.top==outside.prime_trail[0].prime_need.top);
    void *accepted=copy.payload;
    assert(petta_continuation_copy_group_prepare(group,&image,atoms,&copy)==CETTA_CONTINUATION_UNSUPPORTED && copy.payload==accepted);
    atom_deep_copy_session_free(atoms);petta_continuation_copy_group_free(group);
    cetta_owned_continuation_destroy(&image);petta_machine_destroy(&machine);
    bindings_free(&seed);arena_free(&seed_arena);arena_free(&parent_source);arena_free(&parent_destination);
    PettaResourceCapture capture={moved->terms.pool,NULL};
    assert(bindings_builder_payloads_satisfy(&moved->bindings.bindings,
        petta_continuation_resource_graph,petta_continuation_state_resource,&capture));
    assert(lease.references==2u);
    bindings_builder_free(&outside);cetta_owned_continuation_destroy(&copy);
    assert(lease.references==1u && lease.retains==lease.releases);
    probe_lease_release(&lease);assert(lease.references==0u);
    space_free(&other);space_free(&space);term_universe_free(&universe);
    arena_free(&answers);arena_free(&external);
}

static bool probe_any_atom(const Atom *atom,void *raw) {(void)raw;return atom!=NULL;}
static bool probe_expected_state(const StateCell *cell,void *raw) {return cell==raw;}
static void provider_state_identity_control(void) {
    Arena owner;arena_init_detached(&owner);StateCell state={0},other={0};
    Atom *seven=atom_int(&owner,7);PrimeNeedBranchState empty={0},written;
    assert(prime_need_branch_state_begin(&owner,&empty) &&
        prime_need_branch_state_write(&owner,&empty,&state,seven,seven,&written));
    Bindings seed;bindings_init(&seed);*bindings_branch_state_mut(&seed)=written;
    BindingsBuilder builder;assert(bindings_builder_init(&builder,&seed));
    assert(bindings_builder_payloads_satisfy(&builder,probe_any_atom,probe_expected_state,&state));
    assert(!bindings_builder_payloads_satisfy(&builder,probe_any_atom,probe_expected_state,&other));
    (void)bindings_builder_save(&builder);
    assert(bindings_builder_add_id_fresh(&builder,fresh_var_id(),SYMBOL_ID_NONE,seven));
    assert(builder.prime_trail_len && builder.prime_trail[builder.prime_trail_len-1u].branch_state.top);
    prime_need_branch_state_init(bindings_branch_state_mut(&builder.current));
    assert(bindings_builder_payloads_satisfy(&builder,probe_any_atom,probe_expected_state,&state));
    assert(!bindings_builder_payloads_satisfy(&builder,probe_any_atom,probe_expected_state,&other));
    bindings_builder_free(&builder);bindings_free(&seed);arena_free(&owner);
}

static bool probe_transfer(void *raw,const void *old,Space *space) {
    (void)space;
    ProbeAuthority *to=raw;const ProbeAuthority *from=old;
    to->transfer_calls++;
    if(to->invalidate_source && (!to->invalidation_call || to->transfer_calls==to->invalidation_call))
        to->invalidate_source->revision++;
    return to->domain==from->domain;
}

static void provider_mapped_transfer_case(size_t cut) {
    Arena external,answers,seed_owner,parent_source,parent_destination;
    Arena *owners[]={&external,&answers,&seed_owner,&parent_source,&parent_destination};
    for(size_t i=0u;i<sizeof(owners)/sizeof(*owners);i++)arena_init_detached(owners[i]);
    TermUniverse universe;term_universe_init(&universe);term_universe_set_persistent_arena(&universe,&external);
    Space space;space_init_with_universe(&space,&universe);
    space_add(&space,probe_parse(&external,"(= (mapped-provider-choice) 1)"));
    space_add(&space,probe_parse(&external,"(= (mapped-provider-choice) 1)"));
    space_add(&space,probe_parse(&external,"(= (mapped-provider-choice) 2)"));
    Atom *query=probe_parse(&answers,"(let $x (mapped-provider-choice) (Pair $x $x))");
    Atom *expected[]={probe_parse(&answers,"(Pair 1 1)"),probe_parse(&answers,"(Pair 1 1)"),probe_parse(&answers,"(Pair 2 2)")};
    Atom *value=atom_expr2(&seed_owner,atom_symbol(&seed_owner,"OwnedPayload"),atom_int(&seed_owner,17));
    PrimeNeedSnapshot root={0},allocated,running,completed;uint64_t cell=0u;
    assert(prime_need_snapshot_begin(&root) &&
        prime_need_snapshot_allocate_persisted(&seed_owner,&root,value,41u,&allocated,&cell) &&
        prime_need_snapshot_start_evaluation(&seed_owner,&allocated,cell,83u,&running) &&
        prime_need_snapshot_resolve_value(&seed_owner,&running,cell,83u,value,&completed));
    completed.max_storage_key=911u;
    CettaWeightLedger empty={0},factors,handled,selected;
    assert(cetta_weight_ledger_append(&seed_owner,&empty,711u,811u,value,&factors) &&
        cetta_weight_ledger_handle(&seed_owner,&factors,&empty,911u,&handled,&selected));
    Bindings seed;bindings_init(&seed);VarId visible=fresh_var_id(),coefficient=fresh_var_id();
    assert(bindings_add_id(&seed,visible,SYMBOL_ID_NONE,value) &&
        bindings_add_id(&seed,coefficient,SYMBOL_ID_NONE,handled.last->value) &&
        bindings_refresh_occurrence_token(&seed));
    *bindings_need_mut(&seed)=completed;*bindings_weight_mut(&seed)=handled;
    uint64_t occurrence=bindings_occurrence_token(&seed);
    ProbeAuthority from={.revision=61u,.remaining=cut,.domain=7u};
    ProbeAuthority to={.revision=61u,.remaining=1000u,.domain=7u};
    PettaMachineHost host={.context=&from,.admit_branch_capture=probe_admission,
        .admit_branch_transfer=probe_transfer,.permit_transition=probe_permit};
    PettaMachineHost target_host=host;target_host.context=&to;
    PettaMachine source={0},destination={0};
    assert(petta_machine_init(&source,&space,&answers,query,&seed,&host) &&
        petta_machine_init(&destination,&space,&answers,query,NULL,&target_host));
    Atom *answer=NULL;Bindings environment;
    PettaMachineStep step=petta_machine_next(&source,&answer,&environment);
    size_t already=step==PETTA_MACHINE_STEP_ANSWER?1u:0u;
    if(already)assert(atom_alpha_eq(answer,expected[0]));
    bindings_free(&environment);
    assert(step==PETTA_MACHINE_STEP_ANSWER || step==PETTA_MACHINE_STEP_SUSPENDED);
    PettaMachineStats before=source.impl->stats;
    CettaOwnedContinuation image={0};
    assert(cetta_continuation_capture(petta_machine_continuation_machine(&source),&image)==CETTA_CONTINUATION_READY);
    PettaOwnedContinuationImpl *saved=image.payload;
    Bindings outside;assert(bindings_clone(&outside,&saved->bindings.bindings.current));
    assert(bindings_need_view(&outside)->top==bindings_need_view(&saved->bindings.bindings.current)->top);
    PettaContinuationCopyGroup *group=NULL;
    assert(petta_continuation_copy_group_new(&image,NULL,&group)==CETTA_CONTINUATION_READY);
    AtomDeepCopyArenaPair provider_pair;
    assert(petta_continuation_copy_group_arena_pair(group,&provider_pair));
    AtomDeepCopySession *atoms=atom_deep_copy_session_new(&parent_destination);
    assert(petta_machine_transfer_continuation_with_session(&destination,&source,&image,group,atoms)==CETTA_CONTINUATION_UNSUPPORTED);
    assert(image.payload==saved && destination.impl->stats.transitions==0u && source.impl->stats.transitions==before.transitions);
    AtomDeepCopyArenaPair pairs[]={{&parent_source,&parent_destination},provider_pair,
        {&external,&external},{&answers,&answers}};
    assert(atom_deep_copy_session_set_arena_map(atoms,pairs,4u));
    AtomDeepCopyDestinationScope scope={0};
    assert(atom_deep_copy_destination_enter(atoms,provider_pair.destination,&scope) &&
        bindings_copy_owned_atoms_with_session(&outside,provider_pair.destination,atoms) &&
        atom_deep_copy_destination_leave(&scope));
    to.domain++;
    assert(petta_machine_transfer_continuation_with_session(&destination,&source,&image,group,atoms)==CETTA_CONTINUATION_UNSUPPORTED);
    to.domain--;to.revision++;
    assert(petta_machine_transfer_continuation_with_session(&destination,&source,&image,group,atoms)==CETTA_CONTINUATION_INVALIDATED);
    to.revision--;
    assert(image.payload==saved && destination.impl->stats.transitions==0u &&
        source.impl->stats.transitions==before.transitions && atom_deep_copy_session_targets(atoms,&parent_destination));
    /* Independent future observation of the original residual. Capturing did
     * not install the relocated representation into this reference machine. */
    from.remaining=1000u;size_t reference_seen=already;
    while((step=petta_machine_next(&source,&answer,&environment))==PETTA_MACHINE_STEP_ANSWER) {
        assert(reference_seen<3u && atom_alpha_eq(answer,expected[reference_seen++]));
        bindings_free(&environment);
    }
    bindings_free(&environment);assert(step==PETTA_MACHINE_STEP_EXHAUSTED && reference_seen==3u);
    PettaMachineStats after=source.impl->stats;
    assert(petta_machine_transfer_continuation_with_session(&destination,&source,&image,group,atoms)==CETTA_CONTINUATION_READY);
    assert(!image.payload && !image.provider && destination.impl->stats.transitions==0u &&
        atom_deep_copy_session_targets(atoms,&parent_destination));
    const Bindings *pending=petta_machine_pending_environment(&destination);
    assert(bindings_need_view(pending)->top==bindings_need_view(&outside)->top);
    assert(bindings_need_view(pending)->max_storage_key==911u && bindings_occurrence_token(pending)==occurrence);
    PrimeNeedCellView view;assert(prime_need_snapshot_lookup(bindings_need_view(pending),cell,&view));
    assert(view.origin==view.cached && bindings_lookup_value_id(&outside,visible).skeleton==view.origin);
    const CettaWeightLedger *ledger=bindings_weight_view(pending);
    assert(ledger->length==1u && ledger->last->occurrence==711u && ledger->last->dependency==811u &&
        cetta_weight_ledger_handler(ledger,711u)==911u &&
        bindings_lookup_value_id(&outside,coefficient).skeleton==ledger->last->value &&
        ledger->last->value!=view.origin && atom_eq(ledger->last->value,view.origin));
    atom_deep_copy_session_free(atoms);petta_continuation_copy_group_free(group);
    petta_machine_destroy(&source);bindings_free(&seed);arena_free(&seed_owner);arena_free(&parent_source);
    size_t resumed_seen=already;
    while((step=petta_machine_next(&destination,&answer,&environment))==PETTA_MACHINE_STEP_ANSWER) {
        assert(resumed_seen<3u && atom_alpha_eq(answer,expected[resumed_seen++]));
        assert(bindings_need_view(&environment)->max_storage_key==911u &&
            cetta_weight_ledger_handler(bindings_weight_view(&environment),711u)==911u);
        bindings_free(&environment);
    }
    bindings_free(&environment);assert(step==PETTA_MACHINE_STEP_EXHAUSTED && resumed_seen==3u);
    const PettaMachineStats *resumed=&destination.impl->stats;
#define SAME_FUTURE(field) assert(resumed->field==after.field-before.field)
    SAME_FUTURE(transitions);SAME_FUTURE(solve_goal_transitions);SAME_FUTURE(call_goal_transitions);
    SAME_FUTURE(unify_goal_transitions);SAME_FUTURE(collection_goal_transitions);SAME_FUTURE(control_goal_transitions);
    SAME_FUTURE(host_goal_transitions);SAME_FUTURE(other_goal_transitions);
    SAME_FUTURE(equation_match_attempts);SAME_FUTURE(equation_branches_scheduled);SAME_FUTURE(unification_calls);
#undef SAME_FUTURE
    printf("{\"mapped_transfer_cut\":%zu,\"remaining_transitions\":%" PRIu64 ",\"remaining_answers\":%zu,\"passed\":true}\n",cut,resumed->transitions,resumed_seen-already);
    bindings_free(&outside);petta_machine_destroy(&destination);arena_free(&parent_destination);
    space_free(&space);term_universe_free(&universe);arena_free(&answers);arena_free(&external);
}

static Atom *probe_identity_resolver(void *context,Atom *atom) {(void)context;return atom;}

static void provider_transfer_late_refusal(void) {
    Arena external,answers,parent_source,parent_destination;
    Arena *owners[]={&external,&answers,&parent_source,&parent_destination};
    for(size_t i=0u;i<sizeof(owners)/sizeof(*owners);i++)arena_init_detached(owners[i]);
    TermUniverse universe;term_universe_init(&universe);term_universe_set_persistent_arena(&universe,&external);
    Space space;space_init_with_universe(&space,&universe);
    Atom *query=probe_parse(&answers,"(LateReturn 9)");
    ProbeAuthority from={.revision=61u,.domain=7u};
    ProbeAuthority to={.revision=61u,.remaining=1000u,.domain=7u,.invalidate_source=&from};
    PettaMachineHost host={.context=&from,.admit_branch_capture=probe_admission,
        .admit_branch_transfer=probe_transfer,.permit_transition=probe_permit};
    PettaMachineHost target_host=host;target_host.context=&to;
    PettaMachine source={0},destination={0};
    assert(petta_machine_init(&source,&space,&answers,query,NULL,&host) &&
        petta_machine_init(&destination,&space,&answers,query,NULL,&target_host));
    CettaOwnedContinuation image={0};
    assert(cetta_continuation_capture(petta_machine_continuation_machine(&source),&image)==CETTA_CONTINUATION_READY);
    PettaOwnedContinuationImpl *saved=image.payload;
    PettaContinuationCopyGroup *group=NULL;
    assert(petta_continuation_copy_group_new(&image,NULL,&group)==CETTA_CONTINUATION_READY);
    AtomDeepCopyArenaPair provider_pair;
    assert(petta_continuation_copy_group_arena_pair(group,&provider_pair));
    AtomDeepCopySession *atoms=atom_deep_copy_session_new(&parent_destination);
    AtomDeepCopyArenaPair pairs[]={{&parent_source,&parent_destination},provider_pair,
        {&external,&external},{&answers,&answers}};
    assert(atom_deep_copy_session_set_arena_map(atoms,pairs,4u));
    CettaOwnedContinuation prepared={0};
    atom_deep_copy_session_set_resolver(atoms,probe_identity_resolver,NULL);
    assert(petta_continuation_copy_group_prepare(group,&image,atoms,&prepared)==CETTA_CONTINUATION_UNSUPPORTED && !prepared.payload);
    atom_deep_copy_session_set_resolver(atoms,NULL,NULL);
    PettaOwnedContinuationImpl malformed=*saved;
    malformed.control.goal_initialized_len=1u;malformed.control.goals=NULL;
    CettaOwnedContinuation bad=image;bad.payload=&malformed;
    PettaContinuationCopyGroup *bad_group=NULL;
    assert(petta_continuation_copy_group_new(&bad,NULL,&bad_group)==CETTA_CONTINUATION_UNSUPPORTED && !bad_group);
    assert(petta_continuation_copy_group_prepare(group,&bad,atoms,&prepared)==CETTA_CONTINUATION_UNSUPPORTED && !prepared.payload);
    PettaContinuationControlComponent control=saved->control;
    PettaContinuationReadoutComponent readout=saved->readout;
    assert(petta_machine_transfer_continuation_with_session(&destination,&source,&image,group,atoms)==CETTA_CONTINUATION_INVALIDATED);
    assert(image.payload==saved && source.impl->stats.transitions==0u && destination.impl->stats.transitions==0u &&
        memcmp(&control,&saved->control,sizeof(control))==0 && memcmp(&readout,&saved->readout,sizeof(readout))==0 &&
        atom_deep_copy_session_targets(atoms,&parent_destination));
    atom_deep_copy_session_free(atoms);petta_continuation_copy_group_free(group);
    from.revision--;to.invalidate_source=NULL;
    assert(petta_machine_transfer_continuation(&destination,&source,&image)==CETTA_CONTINUATION_READY);
    petta_machine_destroy(&source);arena_free(&parent_source);arena_free(&parent_destination);
    Atom *answer=NULL;Bindings environment;
    assert(petta_machine_next(&destination,&answer,&environment)==PETTA_MACHINE_STEP_ANSWER && atom_alpha_eq(answer,query));
    bindings_free(&environment);
    assert(petta_machine_next(&destination,&answer,&environment)==PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);petta_machine_destroy(&destination);
    space_free(&space);term_universe_free(&universe);arena_free(&answers);arena_free(&external);
    puts("PASS: post-preparation source invalidation retains the original residual and zero evaluator work");
}

static void provider_transfer_callback_boundaries(bool mapped,unsigned phase) {
    Arena external,answers,parent_source,parent_destination;
    Arena *owners[]={&external,&answers,&parent_source,&parent_destination};
    for(size_t i=0u;i<4u;i++)arena_init_detached(owners[i]);
    TermUniverse universe;term_universe_init(&universe);term_universe_set_persistent_arena(&universe,&external);
    Space space;space_init_with_universe(&space,&universe);
    Atom *query=probe_parse(&answers,"(LateReturn 9)");
    unsigned scope=1u;ProbeLease lease={.references=1u};
    ProbeAuthority from={.revision=61u,.domain=7u,.resource_scope=&scope,
        .resource_identity=&space,.resource_kind=GV_SPACE,.lease=&lease};
    ProbeAuthority to=from;to.remaining=1000u;
    if(phase==1u || phase==2u || phase==5u) {
        to.invalidate_source=phase==5u?&to:&from;
        to.invalidation_call=phase==2u?2u:1u;
    }
    Bindings seed;bindings_init(&seed);
    if(phase==3u || phase==4u)
        assert(bindings_add_id(&seed,fresh_var_id(),SYMBOL_ID_NONE,atom_space(&answers,&space)));
    PettaMachineHost host={.context=&from,.admit_branch_capture=probe_admission,
        .admit_branch_transfer=probe_transfer,.permit_transition=probe_permit,
        .resource_scope=&scope,.capture_resource=probe_capture_resource};
    PettaMachineHost target=host;target.context=&to;
    PettaMachine source={0},destination={0};
    assert(petta_machine_init(&source,&space,&answers,query,&seed,&host) &&
        petta_machine_init(&destination,&space,&answers,query,NULL,&target));
    CettaOwnedContinuation image={0};
    assert(cetta_continuation_capture(petta_machine_continuation_machine(&source),&image)==CETTA_CONTINUATION_READY);
    PettaOwnedContinuationImpl *saved=image.payload;
    PettaContinuationControlComponent control=saved->control;
    PettaContinuationReadoutComponent readout=saved->readout;
    PettaContinuationCopyGroup *group=NULL;AtomDeepCopySession *atoms=NULL;
    if(mapped) {
        assert(petta_continuation_copy_group_new(&image,&scope,&group)==CETTA_CONTINUATION_READY);
        AtomDeepCopyArenaPair provider_pair;
        assert(petta_continuation_copy_group_arena_pair(group,&provider_pair));
        AtomDeepCopyArenaPair pairs[]={{&parent_source,&parent_destination},provider_pair,
            {&external,&external},{&answers,&answers}};
        atoms=atom_deep_copy_session_new(&parent_destination);
        assert(atoms && atom_deep_copy_session_set_arena_map(atoms,pairs,4u));
    }
    if(phase==3u || phase==4u) {
        assert(saved->terms.pool->resource_len==1u);
        lease.invalidate_revision=phase==3u?&from.revision:&to.revision;
        lease.invalidate_at=lease.retains+1u;
    }
    if(phase==6u)from.revision++;
    size_t held_before=lease.references;
    CettaContinuationStatus status=mapped
        ? petta_machine_transfer_continuation_with_session(&destination,&source,&image,group,atoms)
        : petta_machine_transfer_continuation(&destination,&source,&image);
    bool refused=phase!=0u && phase!=2u;
    assert(status==(refused?CETTA_CONTINUATION_INVALIDATED:CETTA_CONTINUATION_READY));
    assert(to.transfer_calls==(phase==6u?0u:1u));
    assert(source.impl->stats.transitions==0u && destination.impl->stats.transitions==0u);
    if(refused)assert(image.payload==saved &&
        memcmp(&control,&saved->control,sizeof(control))==0 &&
        memcmp(&readout,&saved->readout,sizeof(readout))==0);
    else assert(!image.payload && from.revision==61u && to.revision==61u);
    if(mapped)assert(atom_deep_copy_session_targets(atoms,&parent_destination));
    atom_deep_copy_session_free(atoms);petta_continuation_copy_group_free(group);
    if(refused) {
        assert(lease.references==held_before);
        from.revision=61u;to.revision=61u;to.invalidate_source=NULL;lease.invalidate_revision=NULL;
        assert(petta_machine_transfer_continuation(&destination,&source,&image)==CETTA_CONTINUATION_READY);
    }
    petta_machine_destroy(&source);bindings_free(&seed);
    Atom *answer=NULL;Bindings environment;
    assert(petta_machine_next(&destination,&answer,&environment)==PETTA_MACHINE_STEP_ANSWER && atom_alpha_eq(answer,query));
    bindings_free(&environment);
    assert(petta_machine_next(&destination,&answer,&environment)==PETTA_MACHINE_STEP_EXHAUSTED);
    bindings_free(&environment);petta_machine_destroy(&destination);
    space_free(&space);term_universe_free(&universe);
    /* Returned bindings can extend the resource's lifetime into the answer
     * arena even after the provider machines have retired. */
    for(size_t i=4u;i>0u;i--)arena_free(owners[i-1u]);
    assert(lease.references==1u && lease.retains==lease.releases);
}

static void provider_match_copy_control(bool match_first) {
    Arena source,destination;arena_init_detached(&source);arena_init_detached(&destination);
    Atom *value=atom_expr2(&source,atom_symbol(&source,"MatchPayload"),atom_int(&source,7));
    PrimeNeedSnapshot root={0},allocated,running,completed;uint64_t cell=0u;
    assert(prime_need_snapshot_begin(&root) &&
        prime_need_snapshot_allocate(&source,&root,value,&allocated,&cell) &&
        prime_need_snapshot_start_evaluation(&source,&allocated,cell,91u,&running) &&
        prime_need_snapshot_resolve_value(&source,&running,cell,91u,value,&completed));
    Bindings seed,outside;bindings_init(&seed);*bindings_need_mut(&seed)=completed;
    assert(bindings_clone(&outside,&seed));
    PettaChoice choice={.kind=PETTA_CHOICE_MATCH};binding_set_init(&choice.as.match.binding_snapshot);
    assert(binding_set_push(&choice.as.match.binding_snapshot,&seed));
    assert(bindings_need_view(&choice.as.match.binding_snapshot.items[0])->top==bindings_need_view(&outside)->top);
    AtomDeepCopySession *atoms=atom_deep_copy_session_new(&destination);
    AtomDeepCopyArenaPair pair={&source,&destination};assert(atom_deep_copy_session_set_arena_map(atoms,&pair,1u));
    if(match_first)assert(petta_copy_supported_choice_atoms(atoms,&destination,&choice,NULL,true));
    assert(bindings_copy_owned_atoms_with_session(&outside,&destination,atoms));
    if(!match_first)assert(petta_copy_supported_choice_atoms(atoms,&destination,&choice,NULL,true));
    assert(bindings_need_view(&choice.as.match.binding_snapshot.items[0])->top==bindings_need_view(&outside)->top);
    atom_deep_copy_session_free(atoms);bindings_free(&seed);arena_free(&source);
    PrimeNeedCellView view;assert(prime_need_snapshot_lookup(
        bindings_need_view(&choice.as.match.binding_snapshot.items[0]),cell,&view) &&
        view.origin==view.cached && atom_is_symbol(view.cached->expr.elems[0],"MatchPayload"));
    binding_set_free(&choice.as.match.binding_snapshot);bindings_free(&outside);arena_free(&destination);
}

int main(void) {
    SymbolTable symbols;symbol_table_init(&symbols);symbol_table_init_builtins(&symbols,&g_builtin_syms);g_symbols=&symbols;
    VarInternTable variables;var_intern_init(&variables);g_var_intern=&variables;g_hashcons=NULL;
    bool passed=true;size_t cuts[]={0u,3u,7u,11u,17u,23u};
    for(size_t i=0u;i<sizeof(cuts)/sizeof(*cuts);i++)
        for(unsigned distinct=0u;distinct<2u;distinct++)
            for(unsigned first=0u;first<2u;first++)
                for(unsigned mapped=0u;mapped<2u;mapped++)
                    passed=provider_graph_case(cuts[i],distinct!=0u,first!=0u,mapped!=0u)&&passed;
    for(unsigned carrier=0u;carrier<5u;carrier++)provider_resource_case(carrier);
    puts("PASS: mapped provider admission checks captured resource identity and scope independently of memo hits");
    provider_state_identity_control();
    puts("PASS: current and rollback branch payloads retain naked StateCell authority");
    for(size_t cut=0u;cut<24u;cut++)provider_mapped_transfer_case(cut);
    puts("PASS: checked mapped transfer preserves residual order, sharing, claims and remaining transitions after private retirement");
    provider_transfer_late_refusal();
    for(unsigned mapped=0u;mapped<2u;mapped++)for(unsigned phase=0u;phase<7u;phase++)
        provider_transfer_callback_boundaries(mapped!=0u,phase);
    puts("PASS: one transfer-policy call and post-copy endpoint validation preserve refusal, resource leases and retry");
    provider_match_copy_control(false);provider_match_copy_control(true);
    puts("PASS: MATCH binding snapshots share the enclosing owned Need graph in both copy orders");
    bindings_thread_cache_free();g_var_intern=NULL;var_intern_free(&variables);g_symbols=NULL;symbol_table_free(&symbols);
    if(passed)puts("PASS: provider graph copy preserves shared and distinct Need paths in both copy orders");
    return passed?0:1;
}
