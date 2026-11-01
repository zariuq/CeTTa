/* Exercise the real tail-dispatch collector boundary with explicit ownership.
 * The unit replaces eval.o; it does not copy the collector or dispatch code. */
#define _GNU_SOURCE
#include <stdlib.h>
static long cohort_calloc_refusal=-1,cohort_realloc_refusal=-1;
static void *cohort_test_calloc(size_t count,size_t bytes) {
    if(cohort_calloc_refusal==0) {cohort_calloc_refusal=-1;return NULL;}
    if(cohort_calloc_refusal>0) cohort_calloc_refusal--;
    return calloc(count,bytes);
}
static void *cohort_test_realloc(void *pointer,size_t bytes) {
    if(cohort_realloc_refusal==0) {cohort_realloc_refusal=-1;return NULL;}
    if(cohort_realloc_refusal>0) cohort_realloc_refusal--;
    return realloc(pointer,bytes);
}
#define calloc cohort_test_calloc
#define realloc cohort_test_realloc
#include "../src/eval.c"
#undef realloc
#undef calloc
#include "../src/binding/slot_store_internal.h"
#include <assert.h>

static void gc_guard_case(unsigned mode) {
    Arena arena, owner;
    arena_init(&arena);
    arena_init(&owner);
    arena_set_hashcons(&arena, NULL);
    arena_set_hashcons(&owner, NULL);
    Space space;
    space_init(&space);
    Bindings env;
    bindings_init(&env);
    OutcomeSet results;
    outcome_set_init(&results);
    PrimeEvalStackDriver driver = {0};
    driver.arena = &arena;
    driver.gc_anchor = arena_mark(&arena);
    driver.root_target = &results;
    driver.root_call_running = true;
    driver.collection_admitted = mode != 3u;
    g_prime_eval_stack_driver = &driver;
    g_prime_need_owner = &owner;
    g_prime_need_episode_owner = &owner;
    prime_need_snapshot_init(&g_prime_need_active);
    prime_need_branch_state_init(&g_prime_need_branch_state_active);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    prime_need_active_receipt_set((PrimeNeedReceipt){0});
#endif
    assert(arena_alloc(&arena, 4096u));
    Atom *leaf = atom_int(&arena, 7);
    Atom *held = atom_expr3(&arena, atom_symbol(&arena, "Pair"), leaf, leaf);
    Atom *type = NULL;
    Atom *before = held;
    EvalGcRootFrame root = {0};
    eval_gc_root_frame_enter(&root, &held, &env, &type);
    if (mode != 0u) eval_gc_root_frame_set_precise(&root, true);
    eval_gc_external_owner_enter();
    if (mode == 1u) eval_gc_external_owner_enter();
    if (mode == 2u) outcome_set_add(&results, atom_int(&arena, 42), &env);
    size_t old_collections = g_eval_gc.collections;
    size_t old_reclaimed = g_eval_gc.reclaimed_bytes;
    cetta_runtime_stats_reset();
    metta_call_impl(&space, &arena, atom_int(&arena, 42), NULL,
                   -1, true, &env, -1, &results);
    assert(!eval_unwinding());
    assert(results.len == (mode == 2u ? 2u : 1u));
    for (CettaCount i = 0u; i < results.len; i++) {
        Atom *value = outcome_atom_materialize(&arena, &results.items[i]);
        assert(value && value->kind == ATOM_GROUNDED &&
               value->ground.gkind == GV_INT && value->ground.ival == 42);
    }
    assert(held && held->kind == ATOM_EXPR && held->expr.len == 3u &&
           held->expr.elems[1] == held->expr.elems[2] &&
           held->expr.elems[1]->ground.ival == 7);
    if (mode == 4u) {
        assert(g_eval_gc.collections == old_collections + 1u);
        assert(g_eval_gc.reclaimed_bytes > old_reclaimed);
        assert(held != before);
    } else {
        assert(g_eval_gc.collections == old_collections);
        assert(g_eval_gc.reclaimed_bytes == old_reclaimed);
        assert(held == before);
    }
#if CETTA_BUILD_WITH_RUNTIME_STATS
    CettaRuntimeStats stats;
    cetta_runtime_stats_snapshot(&stats);
    assert(stats.counters[CETTA_RUNTIME_COUNTER_EVAL_TAIL_COLLECTION_CANDIDATE] == 1u);
    assert(stats.counters[CETTA_RUNTIME_COUNTER_EVAL_TAIL_BLOCKED_IMPRECISE_ROOT] ==
           (mode == 0u || mode == 3u));
    assert(stats.counters[CETTA_RUNTIME_COUNTER_EVAL_TAIL_BLOCKED_EXTERNAL_OWNER] == (mode == 1u));
    assert(stats.counters[CETTA_RUNTIME_COUNTER_EVAL_TAIL_BLOCKED_LIVE_OUTCOME] == (mode == 2u));
    assert(stats.counters[CETTA_RUNTIME_COUNTER_EVAL_TAIL_COLLECTED] == (mode == 4u));
    assert(stats.counters[CETTA_RUNTIME_COUNTER_PRIME_EVAL_STACK_GC_FRAME_SAFE_POINT] == (mode == 4u));
#endif
    printf("GCGuard %u answers %zu collected %u alias 1\n", mode,
           (size_t)results.len, mode == 4u);
    if (mode == 1u) eval_gc_external_owner_leave();
    eval_gc_external_owner_leave();
    eval_gc_root_frame_leave(&root);
    assert(!g_eval_gc.roots && !g_eval_gc.imprecise_lexical_roots &&
           !g_eval_gc.external_owner_depth && !g_eval_gc.region_depth);
    assert(!driver.top && !driver.task_ready && !driver.raw_call_depth);
    outcome_set_free(&results);
    bindings_free(&env);
    g_prime_eval_stack_driver = NULL;
    g_prime_need_owner = NULL;
    g_prime_need_episode_owner = NULL;
    space_free(&space);
    arena_free(&arena);
    arena_free(&owner);
    eval_gc_survivor_reset();
}

/* Pending delivery is an independent root. A transient state carrier first
 * declines collection. Its caller-owned cell then permits explicit world
 * preparation; both collectors preserve the pending payload and world. */
static void pending_force_gc_case(bool queued) {
    Arena arena, owner; arena_init(&arena); arena_init(&owner);
    arena_set_hashcons(&arena, NULL); arena_set_hashcons(&owner, NULL);
    Space space; space_init(&space);
    PrimeNativeControl control = {0};
    outcome_set_init(&control.output);
    PrimeEvalStackDriver *driver = &control.driver;
    driver->arena = &arena; driver->root_target = &control.output;
    driver->gc_anchor = arena_mark(&arena);
    if (queued) {driver->control = &control; control.collects = true;}
    g_prime_eval_stack_driver = driver;
    g_prime_need_owner = g_prime_need_episode_owner = &owner;
    prime_need_snapshot_init(&g_prime_need_active);
    prime_need_branch_state_init(&g_prime_need_branch_state_active);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    prime_need_active_receipt_set((PrimeNeedReceipt){0});
#endif
    PrimeEvalStackFrame *frame = prime_eval_stack_frame_new(PRIME_EVAL_STACK_FRAME_FORCE);
    frame->space = &space; frame->arena = &arena; frame->heap_owner = &owner;
    frame->target = &control.output; frame->fuel = -1;
    bindings_init(&frame->env); frame->env_initialized = true;
    outcome_set_init(&frame->child); frame->child_initialized = true;
    outcome_set_init(&frame->force_pending); frame->force_pending_initialized = true;
    PrimeNeedSnapshot root = {0}, cached;
    assert(prime_need_snapshot_begin(&root));
    assert(prime_need_snapshot_allocate(&owner, &root, atom_symbol(&owner, "pending-gc-source"),
        &frame->before, &frame->thunk_id));
    assert(prime_need_snapshot_lookup(&frame->before, frame->thunk_id, &frame->cell));
    frame->evaluator_id = prime_need_fresh_evaluator_id();
    assert(prime_need_snapshot_start_evaluation(&owner, &frame->before, frame->thunk_id,
        frame->evaluator_id, &frame->evaluating));
    *bindings_need_mut(&frame->env) = frame->evaluating;
    outcome_set_add(&frame->child, atom_int(&arena, 0), &frame->env);
    Atom *leaf = atom_int(&arena, 7);
    Atom *payload = atom_expr3(&arena, atom_symbol(&arena, "Pair"), leaf, leaf);
    assert(prime_need_snapshot_resolve_ready_value(&owner, &frame->evaluating,
        frame->thunk_id, frame->evaluator_id, atom_deep_copy(&owner, payload), &cached));
    Bindings pending; assert(bindings_clone(&pending, &frame->env));
    *bindings_need_mut(&pending) = cached;
    StateCell *state = arena_alloc(&owner, sizeof(*state)); assert(state);
    *state = (StateCell){.value=atom_int(&owner,0)};
    PrimeNeedBranchState branch = {0};
    assert(prime_need_branch_state_begin(&arena, &branch));
    assert(prime_need_branch_state_write(&arena, &branch, state, state->value,
        atom_int(&arena, 9), bindings_branch_state_mut(&pending)));
    CettaWeightLedger factor = {0};
    assert(cetta_weight_ledger_append(&arena, &factor, prime_need_fresh_source_occurrence(),
        prime_need_fresh_source_occurrence(), atom_int(&arena, 3), &factor));
    *bindings_weight_mut(&pending) = factor;
    assert(bindings_refresh_occurrence_token(&pending));
    uint64_t occurrence = bindings_occurrence_token(&pending);
    outcome_set_add(&frame->force_pending, payload, &pending); bindings_free(&pending);
    frame->force_pending_observed = frame->force_retry_pending = frame->force_published = true;
    if (queued) control.frames = frame;
    else {driver->top = frame; driver->frame_depth = 1u;}
    size_t collections = g_eval_gc.collections;
    eval_gc_external_owner_enter();
    assert(!(queued ? prime_native_control_collect(&control) : prime_eval_stack_collect(driver)));
    assert(g_eval_gc.collections == collections && frame->force_pending.items[0].atom == payload);
    assert(bindings_branch_state_view(&frame->force_pending.items[0].env)->owner == &arena);
    assert(bindings_promote_atoms_to_arena(&frame->force_pending.items[0].env, &owner));
    assert(queued ? prime_native_control_collect(&control) : prime_eval_stack_collect(driver));
    eval_gc_external_owner_leave();
    assert(g_eval_gc.collections == collections + 1u);
    Outcome *row = &frame->force_pending.items[0];
    assert(row->atom != payload && row->atom->kind == ATOM_EXPR && row->atom->expr.len == 3u &&
        row->atom->expr.elems[1] == row->atom->expr.elems[2] &&
        row->atom->expr.elems[1]->ground.ival == 7);
    assert(bindings_occurrence_token(&row->env) == occurrence &&
        !arena_owns_ptr(&arena, bindings_branch_state_view(&row->env)->top) &&
        !arena_owns_ptr(&arena, bindings_weight_view(&row->env)->last));
    Atom *read = NULL;
    assert(prime_need_branch_state_value(bindings_branch_state_view(&row->env), state, &read) &&
        read->ground.ival == 9);
    PrimeNeedCellView kept;
    assert(prime_need_snapshot_lookup(bindings_need_view(&row->env), frame->thunk_id, &kept) &&
        kept.cache_state == PRIME_NEED_CACHE_VALUE);
    prime_eval_stack_publish_force_plain(frame);
    assert(control.output.len == 1u && frame->index == 1u && !frame->force_pending.len &&
        !frame->force_retry_pending && bindings_occurrence_token(&control.output.items[0].env) == occurrence);
    driver->top = NULL; control.frames = NULL;
    prime_eval_stack_frame_free(frame); outcome_set_free(&control.output);
    g_prime_eval_stack_driver = NULL; g_prime_need_owner = g_prime_need_episode_owner = NULL;
    space_free(&space); arena_free(&arena); arena_free(&owner); eval_gc_survivor_reset();
    printf("PendingForceGC queued %u collected 1 delivered 1\n", queued);
}

static void delayed_outcome_gc_case(bool queued) {
    Arena arena, owner; arena_init_detached(&arena); arena_init_detached(&owner);
    PrimeNativeControl control = {0};
    outcome_set_init(&control.output);
    PrimeEvalStackDriver *driver = &control.driver;
    driver->arena = &arena; driver->root_target = &control.output;
    driver->gc_anchor = arena_mark(&arena);
    if (queued) {driver->control = &control; control.collects = true;}
    g_prime_eval_stack_driver = driver;
    g_prime_need_owner = g_prime_need_episode_owner = &owner;
    prime_need_snapshot_init(&g_prime_need_active);
    prime_need_branch_state_init(&g_prime_need_branch_state_active);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    prime_need_active_receipt_set((PrimeNeedReceipt){0});
#endif
    Bindings env; bindings_init(&env);
    Atom *leaf = atom_int(&arena, 19);
    Atom *value = atom_expr3(&arena, atom_symbol(&arena, "Answer"), leaf, leaf);
    Atom *condition = atom_expr3(&arena, atom_symbol(&arena, "Waiting"), leaf, leaf);
    outcome_set_add(&control.output, value, &env);
    outcome_set_delay_last(&control.output, condition);
    size_t collections = g_eval_gc.collections;
    eval_gc_external_owner_enter();
    assert(queued ? prime_native_control_collect(&control) : prime_eval_stack_collect(driver));
    eval_gc_external_owner_leave();
    assert(g_eval_gc.collections == collections + 1u);
    Outcome *row = &control.output.items[0];
    assert(row->delayed && row->delayed != condition);
    assert(row->atom && row->atom != value && row->delayed->kind == ATOM_EXPR &&
        row->delayed->expr.len == 3u && row->atom->expr.len == 3u &&
        row->delayed->expr.elems[1] == row->atom->expr.elems[1] &&
        row->delayed->expr.elems[1] == row->delayed->expr.elems[2] &&
        row->atom->expr.elems[1] == row->atom->expr.elems[2] &&
        row->delayed->expr.elems[1]->ground.ival == 19);
    bindings_free(&env); outcome_set_free(&control.output);
    g_prime_eval_stack_driver = NULL; g_prime_need_owner = g_prime_need_episode_owner = NULL;
    arena_free(&arena); arena_free(&owner); eval_gc_survivor_reset();
    printf("DelayedOutcomeGC queued %u collected 1 alias 1\n", queued);
}

static void call_source_birth_case(void) {
    Arena input;arena_init_detached(&input);
    PrimeNativeTopEpisode episode={0};arena_init_detached(&episode.arena);
    PrimeNeedCallLink link={0};
    PrimeNeedCallSources hub={.episode=&episode,.link=&link};
    PrimeNeedSnapshot heap={0},next;
    assert(prime_need_snapshot_begin(&heap));
    uint64_t ids[3];
    for(size_t i=0u;i<3u;i++) {
        assert(prime_need_snapshot_allocate(&episode.arena,&heap,
            atom_symbol(&episode.arena,"birth-source"),&next,&ids[i]));
        heap=next;
    }
    Atom *key=atom_expr2(&input,atom_symbol(&input,"birth-key"),atom_int(&input,2));
    CettaFrameIdentity epoch;assert(cetta_frame_identity_acquire(&epoch));
    VarId base=var_id_from_base(1u);
    Atom *presented=atom_var_with_name_key(&input,key,base);
    Atom *variable=atom_var_with_name_key(&input,key,var_epoch_id(base,epoch));
    Atom *leaf=atom_int(&input,23);
    Atom *payload=atom_expr3(&input,atom_symbol(&input,"Payload"),leaf,leaf);
    Bindings common;bindings_init(&common);
    BindingsFrameSchema *schema=bindings_frame_schema_new_presented(&base,&presented,1u);
    assert(schema && bindings_register_complete_frame_schema(&common,schema,epoch));
    bindings_frame_schema_release(schema);
    assert(variable && bindings_add_var(&common,variable,payload));
    assert(common.len==0u && bindings_has_frame_authority(&common));
    *bindings_need_mut(&common)=heap;
    assert(prime_need_call_birth_atom_safe(variable,NULL));
    AtomDeepCopySession *session=NULL;
    for(size_t i=0u;i<2u;i++) {
        Atom *reference=prime_need_ref(&input,&heap,ids[i]);
        if(i==0u) {
            cohort_calloc_refusal=0;
            assert(prime_need_call_register_cell(&hub,reference,&common,&session)==CETTA_MATCH_INCOMPLETE);
            assert(!hub.cells && !link.identity_count && !session);
        } else {
            PrimeNeedCallSourceCell *before=hub.cells;size_t identities=link.identity_count;
            cohort_realloc_refusal=0;
            assert(prime_need_call_register_cell(&hub,reference,&common,&session)==CETTA_MATCH_INCOMPLETE);
            assert(hub.cells==before && link.identity_count==identities && session &&
                bindings_lookup_value_id(&common,variable->var_id).skeleton==payload);
        }
        assert(reference && prime_need_call_register_cell(&hub,reference,&common,&session)==CETTA_MATCH_MATCHED);
    }
    assert(session && hub.cells && hub.cells->next);
    BindingValue first=bindings_lookup_value_id(&hub.cells->common,variable->var_id);
    BindingValue second=bindings_lookup_value_id(&hub.cells->next->common,variable->var_id);
    assert(first.skeleton && first.skeleton!=payload && first.skeleton==second.skeleton);
    assert(first.skeleton->expr.elems[1]==first.skeleton->expr.elems[2]);
    assert(bindings_logical_atoms_closed_for_arena(&hub.cells->common,&episode.arena));
    assert(bindings_logical_atoms_closed_for_arena(&hub.cells->next->common,&episode.arena));
    assert(bindings_lookup_value_id(&common,variable->var_id).skeleton==payload);

    Space space;space_init(&space);
    Atom *opaque=atom_space(&input,&space);
    Atom *unsafe_key=atom_expr2(&input,atom_symbol(&input,"unsafe-key"),opaque);
    Atom *unsafe_variable=atom_var_with_name_key(&input,unsafe_key,fresh_var_id());
    assert(opaque && unsafe_variable && !prime_need_call_birth_atom_safe(opaque,NULL));
    assert(!prime_need_call_birth_atom_safe(unsafe_variable,NULL));
    Bindings borrowed;bindings_init(&borrowed);
    assert(bindings_add_var(&borrowed,unsafe_variable,opaque));
    *bindings_need_mut(&borrowed)=heap;
    Atom *reference=prime_need_ref(&input,&heap,ids[2]);
    assert(reference && prime_need_call_register_cell(&hub,reference,&borrowed,&session)==CETTA_MATCH_MATCHED);
    assert(bindings_lookup_value_id(&hub.cells->common,unsafe_variable->var_id).skeleton==opaque);
    atom_deep_copy_session_free(session);
    bindings_free(&borrowed);bindings_free(&common);
    while(hub.cells) {
        PrimeNeedCallSourceCell *cell=hub.cells;hub.cells=cell->next;
        bindings_free(&cell->common);free(cell);
    }
    free(link.identities);
    arena_free(&input);arena_free(&episode.arena);space_free(&space);
    cetta_frame_identity_release(epoch);
    puts("CallSourceBirth framed 1 shared-copy 1 named-keys 1 opaque-borrow 1");
}

typedef struct {
    const CettaSealHold *hold;
    PrimeNativeTopEpisode *episode;
    unsigned references;
} BirthEpisodeSeal;

static void birth_episode_seal_retain(void *raw) {
    BirthEpisodeSeal *seal = raw;
    seal->references++;
    prime_native_top_episode_retain(seal->episode);
}

static void birth_episode_seal_release(void *raw) {
    BirthEpisodeSeal *seal = raw;
    assert(seal->references);
    seal->references--;
    prime_native_top_episode_release(seal->episode);
}

static const CettaSealHold birth_episode_seal_hold = {
    .retain = birth_episode_seal_retain,
    .release = birth_episode_seal_release,
};

static void call_source_entrance_owner_case(void) {
    Arena input; arena_init_detached(&input);
    Space space; space_init(&space);
    PrimeNativeTopEpisode *episode = calloc(1u, sizeof(*episode));
    assert(episode);
    atomic_init(&episode->references, 1u);
    arena_init_detached(&episode->arena);
    episode->ready = true; episode->space = &space; episode->library = g_library_context;
    episode->execution = calloc(1u, sizeof(*episode->execution));
    assert(episode->execution);
    eval_execution_context_init(episode->execution, &space, true);
    BirthEpisodeSeal seal = {.hold = &birth_episode_seal_hold, .episode = episode};
    Atom *opaque = atom_seal(&input, &seal);
    Atom *variable = atom_var_with_id(&input, "off-query-owner", fresh_var_id());
    Bindings common, entrance; bindings_init(&common); bindings_init(&entrance);
    assert(opaque && variable && bindings_add_var(&entrance, variable, opaque));
    PrimeNeedSnapshot heap = {0}, next; uint64_t thunk = 0u;
    assert(prime_need_snapshot_begin(&heap) && prime_need_snapshot_allocate(&episode->arena,
        &heap, atom_symbol(&episode->arena, "entrance-origin"), &next, &thunk));
    *bindings_need_mut(&common) = next; *bindings_need_mut(&entrance) = next;
    Atom *reference = prime_need_ref(&input, &next, thunk);
    PrimeNativeTopEpisode *previous_episode = g_prime_top_storage_owner;
    EvalExecutionContext *previous_execution = g_eval_execution;
    Arena *previous_owner = g_prime_need_owner, *previous_episode_owner = g_prime_need_episode_owner;
    g_prime_top_storage_owner = episode; g_eval_execution = episode->execution;
    g_prime_need_owner = g_prime_need_episode_owner = &episode->arena;
    size_t references = atomic_load_explicit(&episode->references, memory_order_relaxed);
    CettaOwnedExecution execution = {0};
    PrimeNeedForceProduction *source = prime_need_force_production_new_in(
        &space, reference, -1, &common, &entrance, &execution, false);
    assert(source && !source->input_owner_held && seal.references == 1u &&
        atomic_load_explicit(&episode->references, memory_order_relaxed) == references &&
        bindings_lookup_value_id(&source->entrance, variable->var_id).skeleton == opaque &&
        !arena_retains_owner(&source->storage, &seal, birth_episode_seal_release));
    cetta_owned_execution_cancel(&execution);
    assert(seal.references == 1u);
    g_prime_top_storage_owner = previous_episode; g_eval_execution = previous_execution;
    g_prime_need_owner = previous_owner; g_prime_need_episode_owner = previous_episode_owner;
    bindings_free(&entrance); bindings_free(&common);
    arena_free(&input);
    assert(!seal.references && atomic_load_explicit(&episode->references, memory_order_relaxed) == 1u);
    prime_native_top_episode_release(episode);
    space_free(&space);
    puts("CallSourceEntrance off-query-retain 0 borrowed-value 1 balanced-release 1");
}

static void cohort_unknown_retain(void *raw) {(*(unsigned *)raw)++;}
static void cohort_unknown_release(void *raw) {
    unsigned *holds=raw;assert(*holds);(*holds)--;
}
static bool cohort_accept_row(void *raw,uint64_t position,
        const CettaOwnedReturn *row,CettaSharedReturnCheckpoint *next) {
    (void)position;
    assert(row && row->outcome.term && next);
    *(const CettaOwnedReturn **)raw=row;return true;
}
static void resident_cohort_gc_case(void) {
    Arena arena,input;arena_init_detached(&arena);arena_init_detached(&input);
    Space space;space_init(&space);
    Registry registry;registry_init(&registry);Registry *previous_registry=g_registry;g_registry=&registry;
    PrimeNativeTopEpisode *episode=calloc(1u,sizeof(*episode));assert(episode);
    atomic_init(&episode->references,1u);arena_init_detached(&episode->arena);
    episode->ready=true;episode->space=&space;episode->library=g_library_context;episode->registry=&registry;
    episode->request_expression=atom_int(&input,0);
    episode->execution=calloc(1u,sizeof(*episode->execution));assert(episode->execution);
    eval_execution_context_init(episode->execution,&space,true);
    OutcomeSet results;outcome_set_init(&results);
    PrimeEvalStackDriver driver={.arena=&arena,.gc_anchor=arena_mark(&arena),
        .root_target=&results,.root_call_running=true,.collection_admitted=true};
    g_prime_eval_stack_driver=&driver;g_prime_top_storage_owner=episode;
    g_eval_execution=episode->execution;g_prime_need_owner=g_prime_need_episode_owner=&episode->arena;
    Bindings environment;bindings_init(&environment);
    PrimeNeedSnapshot heap={0},next;assert(prime_need_snapshot_begin(&heap));
    Atom *references[2];
    for(size_t i=0u;i<2u;i++) {
        uint64_t thunk=0u;
        assert(prime_need_snapshot_allocate(&episode->arena,&heap,
            atom_int(&episode->arena,23+(int64_t)i),&next,&thunk));
        heap=next;references[i]=prime_need_ref(&input,&heap,thunk);assert(references[i]);
    }
    *bindings_need_mut(&environment)=heap;
    g_prime_need_active=heap;g_prime_need_branch_state_active=(PrimeNeedBranchState){0};
    g_prime_weight_active=(CettaWeightLedger){0};g_prime_need_logical_env=NULL;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    prime_need_active_receipt_set((PrimeNeedReceipt){0});
#endif
    bool installed=false;
    assert(prime_need_call_sources_prepare(&space,&arena,references,2u,
        prime_need_fresh_source_occurrence(),&environment,&installed)==CETTA_MATCH_MATCHED && installed);
    PrimeNeedCallSources *hub=episode->call_sources;
    assert(hub && hub->cells && hub->cells->next);
    PrimeNeedCallForceReader *readers[2];size_t index=0u;
    for(PrimeNeedCallSourceCell *cell=hub->cells;cell;cell=cell->next,index++) {
        assert(index<2u);
        PrimeNeedCallLookup lookup={cell->session,cell->thunk,cell->authority,hub->link,cell};
        CettaMatchCompletion status=CETTA_MATCH_INCOMPLETE;CettaEvalCompletion refusal=CETTA_EVAL_COMPLETE;
        readers[index]=prime_need_call_reader_select(&space,&arena,cell->reference,-1,
            &lookup,&environment,&status,&refusal);
        assert(readers[index] && status==CETTA_MATCH_MATCHED && refusal==CETTA_EVAL_COMPLETE);
        CettaSharedReturnStep step=cetta_shared_return_checkpoint_advance(readers[index]->position,64u);
        for(size_t turn=0u;step==CETTA_SHARED_RETURN_PAUSED && turn<16u;turn++)
            step=cetta_shared_return_checkpoint_advance(readers[index]->position,64u);
        if(step!=CETTA_SHARED_RETURN_COMPLETE) fprintf(stderr,"cohort source step %d\n",(int)step);
        assert(step==CETTA_SHARED_RETURN_COMPLETE);
        assert(cetta_shared_return_producer_is_complete_for(readers[index]->slot->producer,
            prime_need_force_row_capture,prime_need_force_production_current,readers[index]->slot->source));
    }
    assert(index==2u);
    PrimeNeedCallGcAudit *audit=prime_need_call_gc_prepare(&driver);
    assert(audit && !audit->refusal);
    PrimeNativeControl queued={.collects=true};
    queued.driver.arena=&arena;
    g_prime_eval_stack_driver=&queued.driver;eval_gc_external_owner_enter();
    assert(!prime_native_control_collectable(&queued));
    episode->call_sources=NULL;
    assert(prime_native_control_collectable(&queued));
    g_prime_need_call_gc_audit=audit;
    assert(!prime_native_control_collectable(&queued));
    g_prime_need_call_gc_audit=NULL;episode->call_sources=hub;
    eval_gc_external_owner_leave();g_prime_eval_stack_driver=&driver;
    /* Registering an episode in itself is forbidden before any root moves. */
    assert(!prime_need_call_gc_hold_in(audit,&episode->arena));
    assert(prime_need_call_gc_frame_reader(audit,readers[0]));
    CettaSharedReturnCheckpoint *original=readers[0]->position;
    readers[0]->position=readers[1]->position;
    assert(!prime_need_call_gc_frame_reader(audit,readers[0]));
    readers[0]->position=original;
    CettaSharedReturnCheckpoint *released=cetta_shared_return_checkpoint_new_scoped(readers[0]->slot->producer,0u);
    assert(released);cetta_shared_return_checkpoint_release(released);
    readers[0]->position=released;
    assert(!prime_need_call_gc_frame_reader(audit,readers[0]));
    readers[0]->position=original;
    prime_need_call_gc_free(audit);

    PrimeNeedForceProduction *source=readers[0]->slot->source;
    ArenaMark before_owner=arena_mark(&source->storage);unsigned unknown_holds=0u;
    assert(arena_retain_owner(&source->storage,&unknown_holds,cohort_unknown_retain,cohort_unknown_release));
    assert(unknown_holds==1u && !prime_need_call_gc_prepare(&driver));
    arena_reset(&source->storage,before_owner);assert(!unknown_holds);
    assert((audit=prime_need_call_gc_prepare(&driver)));prime_need_call_gc_free(audit);
    Bindings saved;bindings_init(&saved);assert(bindings_clone(&saved,&source->common));
    Atom *bad_variable=atom_var_with_id(&input,"cohort-borrow",fresh_var_id());
    assert(bindings_add_var(&source->common,bad_variable,atom_int(&arena,91)));
    assert(!prime_need_call_gc_prepare(&driver));bindings_replace(&source->common,&saved);

    /* A saved provider can hold this same episode even after its off-query
     * logical entry is projected away. Its strong ledger remains an explicit
     * unsupported lifetime edge; neither birth nor collection drops it. */
    BirthEpisodeSeal seal={.hold=&birth_episode_seal_hold,.episode=episode};
    Arena provider;arena_init_detached(&provider);
    Bindings hidden,restored,projected;bindings_init(&hidden);bindings_init(&restored);bindings_init(&projected);
    Atom *hidden_variable=atom_var_with_id(&provider,"hidden-provider",fresh_var_id());
    assert(bindings_add_var(&hidden,hidden_variable,atom_seal(&provider,&seal)));
    Atom *captured=bindings_capture_value(&provider,&hidden);assert(captured);
    assert(bindings_restore_captured_value(captured,&restored));
    Atom *no_support=atom_int(&provider,1);
    assert(bindings_project_reachable(&restored,&no_support,1u,&projected));
    assert(!projected.len && projected.owners && !prime_need_call_birth_bindings_safe(&projected));
    assert(bindings_clone(&saved,&source->common));bindings_replace(&source->common,&projected);
    assert(!prime_need_call_gc_prepare(&driver));
    bindings_replace(&source->common,&saved);
    bindings_free(&restored);bindings_free(&hidden);arena_free(&provider);assert(!seal.references);

    CettaSharedDelivery before=cetta_shared_return_checkpoint_peek(readers[0]->position);
    assert(before.kind==CETTA_SHARED_DELIVERY_ANSWER && before.returned);
    Atom *leaf=atom_int(&arena,7);
    outcome_set_add(&results,atom_expr3(&arena,atom_symbol(&arena,"Pair"),leaf,leaf),&environment);
    Atom *old=results.items[0].atom;
    assert(prime_eval_stack_collect(&driver));
    assert(results.items[0].atom!=old && results.items[0].atom->expr.elems[1]==results.items[0].atom->expr.elems[2]);
    assert(arena_retains_owner(&g_eval_gc.survivor,episode,prime_native_top_episode_release));
    CettaSharedDelivery after=cetta_shared_return_checkpoint_peek(readers[0]->position);
    assert(after.kind==CETTA_SHARED_DELIVERY_ANSWER && after.returned==before.returned);
    const CettaOwnedReturn *replayed=NULL;CettaSharedReturnCheckpoint *accepted=NULL;
    assert(cetta_shared_return_checkpoint_take(readers[0]->position,cohort_accept_row,&replayed,&accepted).kind==CETTA_SHARED_DELIVERY_ANSWER);
    assert(replayed==before.returned && accepted && readers[0]->position==original);
    cetta_shared_return_checkpoint_release(accepted);
    size_t episode_references=atomic_load_explicit(&episode->references,memory_order_relaxed);
    old=results.items[0].atom;
    assert(arena_alloc(&arena,4096u) && prime_eval_stack_collect(&driver));
    assert(results.items[0].atom!=old &&
        results.items[0].atom->expr.elems[1]==results.items[0].atom->expr.elems[2] &&
        atomic_load_explicit(&episode->references,memory_order_relaxed)==episode_references);
    after=cetta_shared_return_checkpoint_peek(readers[0]->position);
    assert(after.kind==CETTA_SHARED_DELIVERY_ANSWER && after.returned==before.returned);
    /* Both scoped bookmarks remain readable through collection, then refuse
     * under the original retired lease without a producer dispatch. */
    uint64_t dispatches=source->dispatches;
    eval_execution_invalidate_borrows(episode->execution);
    assert(cetta_shared_return_checkpoint_advance(readers[0]->position,1u)==CETTA_SHARED_RETURN_CONTEXT_REFUSED);
    assert(source->dispatches==dispatches);
    for(size_t i=0u;i<2u;i++) prime_need_call_force_reader_free(readers[i]);
    outcome_set_free(&results);bindings_free(&environment);
    g_prime_need_active=(PrimeNeedSnapshot){0};g_prime_need_logical_env=NULL;
    g_prime_need_owner=g_prime_need_episode_owner=NULL;g_prime_top_storage_owner=NULL;
    g_prime_eval_stack_driver=NULL;g_eval_execution=NULL;
    prime_native_top_episode_release(episode);eval_gc_survivor_reset();
    arena_free(&input);arena_free(&arena);space_free(&space);
    g_registry=previous_registry;registry_free(&registry);
    puts("ResidentCohort complete 2 foreign/released-checkpoint-refusal 1 owner/borrow-refusal 1 replay-alias 1 retired-refusal 1");
}

int main(void) {
    SymbolTable symbols;
    VarInternTable variables;
    CettaLibraryContext context = {0};
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols;
    g_var_intern = &variables;
    g_hashcons = NULL;
    cetta_library_context_init_for_language_profile(&context, CETTA_LANGUAGE_PRIME,
                           cetta_profile_prime_default());
    eval_set_default_fuel(-1);
    eval_set_library_context(&context);
    eval_gc_init_once();
    g_eval_gc.enabled = true;
    g_eval_gc.budget_bytes = 1024u;
    cetta_runtime_stats_enable();
    for (unsigned mode = 0u; mode < 5u; mode++) gc_guard_case(mode);
    pending_force_gc_case(false);
    pending_force_gc_case(true);
    delayed_outcome_gc_case(false);
    delayed_outcome_gc_case(true);
    call_source_birth_case();
    call_source_entrance_owner_case();
    resident_cohort_gc_case();
    eval_set_library_context(NULL);
    cetta_library_context_free(&context);
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    g_var_intern = NULL;
    puts("PASS: tail collection retains unsafe owners and preserves rooted aliases");
    return 0;
}
