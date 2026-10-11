/* Native workers execute the evaluator's retained task, including its task
 * kind and captured environment. The coordinator alone publishes into the
 * original return frame. Every unfinished child remains that frame's owned
 * producer; finishing an executor episode does not mean source closure. */
typedef enum {
    PRIME_NATIVE_ATTACHMENT_NONE = 0,
    PRIME_NATIVE_ATTACHMENT_FORCE,
    PRIME_NATIVE_ATTACHMENT_EQUATION,
    PRIME_NATIVE_ATTACHMENT_RETURN,
} PrimeNativeAttachment;

#include "prime_owned_execution_types.inc"

typedef struct PrimeNativeRefillItem PrimeNativeRefillItem;

struct PrimeNativeParallel {
    _Atomic size_t references;
    PrimeNativeExecutionSlice execution;
    Arena *resource_owner;
    TermUniverse *resource_universe;
    bool resource_owner_pinned;
    bool executor_retired;
    bool coordinator_only;
    /* Placement restriction without original-world resource authority. */
    bool coordinator_private;
    /* Issued from an actual native return graph. An equation activation is
     * an alternative world; a FORCE attachment owns its one producer. The
     * capsule never retains a pointer to a frame after executor retirement. */
    PrimeNativeAttachment attachment;
    uint64_t force_thunk_id;
    uint64_t force_evaluator_id;
    uint64_t activation_occurrence;
    PrimeNeedSnapshot captured_need;
    EvalContextState context;
    HyperposeThreadBranch world;
    SpaceReadView borrowed_source;
    CettaLibraryContext *borrowed_library;
    uint64_t borrowed_library_revision;
    uint64_t borrowed_symbol_instance;
    PrimeNativeControl *child;
    PrimeNativeWork *dialect_operation;
    CettaSpaceAgenda *resource_provider;
    CettaSpaceAgendaMatch *resource_match;
    CettaNativeCostLedger slice_cost;
    PrimeNativeRefillItem *refill_item;
    VariantBank variants;
    bool variants_ready;
    TempSpaceSet temporary_spaces;
    TempSpaceSet created_spaces;
    size_t forwarded;
    size_t barriers_forwarded;
    size_t need_barriers_forwarded;
    uint64_t grant;
    uint64_t wave_remaining;
    bool wave_settled;
    bool descendant_pending;
    bool descendant_refused;
    uint64_t spent;
    uint64_t occurrence;
    uint64_t started_ns;
    uint64_t finished_ns;
    uint32_t worker;
    uint64_t phase;
    bool attempted;
    bool completed;
    bool metered;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    bool history_enabled;
    PrimeNeedReceipt history;
#endif
};

#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
static PrimeNeedReceipt prime_native_parallel_history(const PrimeNativeWork *work) {
    return work->parallel && work->parallel->history_enabled
        ? work->parallel->history : (PrimeNeedReceipt){0};
}
#endif

typedef struct {
    CettaLibraryContext *library;
    bool coordinator;
    bool descendants;
    uint32_t descendant_workers;
    uint64_t descendant_quantum;
    bool receipts;
    _Atomic bool cancelled;
    _Atomic uint32_t active;
    _Atomic uint32_t overlap;
} PrimeNativeParallelRun;

static _Thread_local PrimeNativeParallel *g_prime_native_parallel_current;

static void prime_native_parallel_destroy(PrimeNativeParallel *parallel) {
    if (!parallel) return;
    size_t previous = atomic_fetch_sub_explicit(&parallel->references, 1u, memory_order_acq_rel);
    assert(previous > 0u);
    if (previous != 1u) return;
    /* A partial adoption leaves these entries owned by the suspended slice.
     * Release their heap internals while every containing arena is live. */
    TempSpaceSet *pending[] = {&parallel->temporary_spaces, &parallel->created_spaces};
    for (size_t k = 0u; k < 2u; k++) {
        for (CettaCount i = 0u; i < pending[k]->len; i++) {
            EvalTrackedSpace tracked = pending[k]->items[i];
            if (!tracked.space) continue;
            if (tracked.petta_program) petta_program_forget_space(tracked.petta_program, tracked.space);
            space_free(tracked.space);
        }
        free(pending[k]->items);
    }
    prime_native_execution_detach(parallel);
    prime_native_control_destroy(parallel->child);
    if (parallel->variants_ready) variant_bank_free(&parallel->variants);
    cetta_native_cost_free(&parallel->slice_cost);
    hyperpose_thread_branch_free(&parallel->world);
    space_read_view_release(&parallel->borrowed_source);
    free(parallel);
}

/* A persistent resource may retain syntax after the executor is retired.
 * Program catalogs belong to the library, whose teardown precedes the final
 * persistent arena release. End those borrowed cache associations while the
 * executor still has library authority; keep the actual resource world. */
static void prime_native_parallel_retire(PrimeNativeParallel *parallel) {
    if (!parallel || parallel->executor_retired) return;
    parallel->executor_retired = true;
    __attribute__((cleanup(eval_shared_transition_scope_leave)))
    bool retirement_scope = !cetta_shared_transition_scope_active();
    if (retirement_scope) cetta_shared_transition_scope_enter();
    CETTA_SCOPED_SHARED_TRANSITION(retirement_transition);
    for (CettaCount i = 0u; i < parallel->world.owned_spaces.len; i++) {
        HyperposeOwnedSpace *owned = &parallel->world.owned_spaces.items[i];
        if (owned->petta_program) {
            petta_program_forget_space(owned->petta_program, owned->space);
            owned->petta_program = NULL;
        }
    }
    TempSpaceSet *pending[] = {&parallel->temporary_spaces, &parallel->created_spaces};
    for (size_t k = 0u; k < 2u; k++) {
        for (CettaCount i = 0u; i < pending[k]->len; i++) {
            EvalTrackedSpace *tracked = &pending[k]->items[i];
            if (tracked->petta_program) {
                petta_program_forget_space(tracked->petta_program, tracked->space);
                tracked->petta_program = NULL;
            }
        }
    }
    prime_native_parallel_destroy(parallel);
}

static void prime_native_parallel_storage(const PrimeNativeParallel *parallel,
        CettaStorageCensus *census) {
    if (!parallel || !cetta_storage_object(census, parallel, sizeof(*parallel),
                                           CETTA_STORAGE_BLOCK)) return;
    cetta_storage_schedule(census, parallel->child, prime_native_control_storage_census);
    arena_storage_census(&parallel->world.persistent_arena, census);
    arena_storage_census(&parallel->world.eval_arena, census);
    hashcons_storage_census(&parallel->world.hashcons, census);
    term_universe_storage_census(&parallel->world.term_universe, census);
    registry_storage_census(&parallel->world.registry, census);
    if(parallel->borrowed_source.root.space)
        space_read_view_storage_census(&parallel->borrowed_source,census);
    if (parallel->variants_ready) {
        arena_storage_census(&parallel->variants.arena, census);
        hashcons_storage_census(&parallel->variants.hashcons, census);
    }
    bindings_storage_census(&parallel->world.capture_env, census);
    prime_need_snapshot_storage_census(&parallel->captured_need, census);
    cetta_storage_array(census, parallel->world.owned_spaces.items,
        parallel->world.owned_spaces.cap, sizeof(*parallel->world.owned_spaces.items), 0u);
    cetta_storage_array(census, parallel->slice_cost.events,
        parallel->slice_cost.capacity, sizeof(*parallel->slice_cost.events), 0u);
    for (CettaCount i = 0u; i < parallel->world.owned_spaces.len; i++) {
        const Space *space = parallel->world.owned_spaces.items[i].space;
        if (!arena_owns_ptr(&parallel->world.persistent_arena, space) &&
            !arena_owns_ptr(&parallel->world.eval_arena, space) &&
            (!parallel->child || !arena_owns_ptr(&parallel->child->storage, space)))
            cetta_storage_object(census, space, sizeof(*space), CETTA_STORAGE_BLOCK);
        space_storage_census(space, census);
    }
    const TempSpaceSet *pending[] = {&parallel->temporary_spaces, &parallel->created_spaces};
    for (size_t k = 0u; k < 2u; k++) {
        cetta_storage_array(census, pending[k]->items, pending[k]->cap, sizeof(*pending[k]->items), 0u);
        for (CettaCount i = 0u; i < pending[k]->len; i++)
            space_storage_census(pending[k]->items[i].space, census);
    }
}

/* Captured unresolved support is transported only with an issued native
 * attachment. Actual demands for it rendezvous on the coordinator;
 * this traversal does not force a lazy subterm to make it eligible. */
static bool prime_native_parallel_support_admitted(const PrimeNeedSnapshot *snapshot,
        Atom *expression, PrimeNativeAttachment attachment, uint64_t evaluator_id) {
    PrimeEvalStackSupportBuffer scan;
    prime_eval_stack_support_buffer_init(&scan);
    OutcomePreviewSeen seen;
    outcome_preview_seen_init(&seen);
    bool safe = prime_eval_stack_support_buffer_push(&scan, expression);
    for (size_t i = 0u; safe && i < scan.len; i++) {
        Atom *atom = scan.items[i];
        bool known = false;
        for (size_t j = 0u; j < seen.len; j++)
            known = known || seen.items[j].skeleton == atom;
        if (known) continue;
        if (!atom || !outcome_preview_seen_add(&seen, atom)) { safe = false; break; }
        if (atom->structural_facts & ATOM_STRUCTURAL_HAS_NATIVE_HANDLE_ID) {
            safe = false; break;
        }
        if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_PRIME_NEED_CAPABILITY) {
            uint64_t cell;
            PrimeNeedCellView value;
            safe = prime_need_ref_belongs_to(atom, snapshot, &cell) &&
                prime_need_snapshot_lookup(snapshot, cell, &value);
            if (safe && (value.cache_state == PRIME_NEED_CACHE_VALUE ||
                         value.cache_state == PRIME_NEED_CACHE_STABLE_FAULT))
                safe = prime_eval_stack_support_buffer_push(&scan, value.cached);
            else if (safe && value.cache_state == PRIME_NEED_CACHE_EMPTY)
                safe = attachment != PRIME_NATIVE_ATTACHMENT_NONE;
            else if (safe)
                safe = attachment == PRIME_NATIVE_ATTACHMENT_FORCE &&
                    value.evaluator_id == evaluator_id;
        } else if (atom->kind == ATOM_GROUNDED) {
            GroundedKind kind = atom->ground.gkind;
            safe = kind == GV_INT || kind == GV_FLOAT || kind == GV_BOOL ||
                kind == GV_STRING || kind == GV_BIGINT || kind == GV_RATIONAL;
        } else if (atom->kind == ATOM_VAR) {
            safe = false;
        } else if (atom->kind == ATOM_EXPR) {
            for (CettaExprIndex j = 0u; safe && j < atom->expr.len; j++)
                safe = prime_eval_stack_support_buffer_push(&scan, atom->expr.elems[j]);
        }
    }
    prime_eval_stack_support_buffer_free(&scan);
    outcome_preview_seen_free(&seen);
    return safe;
}

static void prime_native_restore_hashcons(HashConsTable **previous) {
    g_hashcons = *previous;
}

/* Only the initial BIND of this actual FORCE receives the attachment. The
 * finish frame relays its whole returned world into the original producer.
 * Neither equal values nor equal cell IDs establish this ownership. */
static PrimeEvalStackFrame *prime_native_force_owner(const PrimeNativeWork *work) {
    const PrimeEvalStackTask *task = &work->task;
    PrimeEvalStackFrame *finish = work->parent;
    PrimeEvalStackFrame *force = finish ? finish->control_parent : NULL;
    if (!finish || finish->kind != PRIME_EVAL_STACK_FRAME_BIND_FINISH ||
        task->kind != PRIME_EVAL_STACK_TASK_BIND ||
        task->target != &finish->child || !force ||
        force->kind != PRIME_EVAL_STACK_FRAME_FORCE ||
        finish->target != &force->child || force->control_force_detached ||
        force->control_closed || finish->control_closed ||
        force->control_pending != 1u || finish->control_pending != 1u ||
        task->evaluator_id != force->evaluator_id)
        return NULL;
    const PrimeNeedSnapshot *world = bindings_need_view(&task->dynamic_env);
    if (!prime_need_snapshot_is_ancestor(world, &force->evaluating) ||
        !prime_need_snapshot_is_ancestor(&force->evaluating, world))
        return NULL;
    return force;
}

/* The source matcher has already authorized this physical RHS occurrence.
 * Sibling RHSs are alternative derivations, each with its own world and
 * return. This does not issue independent consumers of one FORCE. */
static uint64_t prime_native_equation_attachment(const PrimeNativeWork *work) {
    const PrimeEvalStackTask *task = &work->task;
    const PrimeEvalStackFrame *parent = work->parent;
    if (!parent || parent->kind != PRIME_EVAL_STACK_FRAME_EQUATION_SEARCH ||
        !parent->equation_search || task->kind != PRIME_EVAL_STACK_TASK_CALL ||
        !task->seed_env_initialized || task->target != parent->target ||
        task->evaluator_id != parent->evaluator_id)
        return 0u;
    const PrimeNeedSnapshot *world = bindings_need_view(&task->dynamic_env);
    const PrimeNeedSnapshot *seed = bindings_need_view(&task->seed_env);
    if (!prime_need_snapshot_is_ancestor(world, seed) ||
        !prime_need_snapshot_is_ancestor(seed, world))
        return 0u;
    return bindings_occurrence_token(&task->seed_env);
}

/* Detachment moves this existing task's one counted edge. It cannot create
 * a second caller, invent a new target, or copy a shared return frame. */
static bool prime_native_return_attachment(
        const PrimeNativeControl *owner, const PrimeNativeWork *work) {
    if (work->owner != owner || !work->task.dynamic_env_initialized)
        return false;
    if (work->task.target == &owner->output)
        return true;
    for (const PrimeEvalStackFrame *frame = work->parent; frame;
         frame = frame->control_parent) {
        if (frame->control_notified)
            return false;
        if (frame->child_initialized && work->task.target == &frame->child)
            return true;
    }
    return false;
}

static uint64_t prime_native_monotonic_ns(void);

#include "prime_owned_execution.inc"

/* A private source remains owned by the enclosing parallel packet until
 * child destruction and join. This permits a scheduling representation
 * change; it does not grant immutable borrowing or new call capabilities. */
static bool prime_native_private_capsule_source(PrimeNativeParallel *parallel) {
    if(!parallel || parallel->borrowed_source.root.space || !parallel->world.prepared ||
        !parallel->world.space || !hyperpose_branch_owns_space(&parallel->world,parallel->world.space))return false;
    const PrimeNativeControl *child=parallel->child;
    if(!child || child->parallel_owner!=parallel || child->driver.control!=child ||
        !child->owned_arena || child->driver.arena!=&child->storage ||
        child->source_read.space!=parallel->world.space || !space_read_token_is_current(child->source_read) ||
        child->query_read_count || child->registry_read.incomplete || child->registry_read.stale)return false;
    RegistryReadView registry=child->registry_read;
    return registry_read_view_validate(&registry,&parallel->world.registry);
}

/* The caller or enclosing owned episode retains these services. Tokens
 * detect a changed interpretation; they do not keep any service alive. */
static bool prime_native_parallel_borrowed_current(
        const PrimeNativeParallel *parallel,CettaLibraryContext *library) {
    return parallel && library && parallel->borrowed_source.root.space &&
        parallel->borrowed_library==library &&
        parallel->borrowed_library_revision==cetta_library_petta_admission_revision(library) &&
        parallel->borrowed_symbol_instance==symbol_table_instance_id(g_symbols) &&
        space_read_view_is_current(&parallel->borrowed_source);
}

/* A checked read-only source closure may contain pure calls whose stack
 * service belongs on the coordinator. Keep the private world and receiver;
 * this does not pin or publish persistent effects in the caller's world. */
static bool prime_native_parallel_read_service_admitted(PrimeNativeControl *child,
        Space *space,Arena *arena,Atom *call,const OutcomeSet *target) {
    PrimeNativeParallel *parallel=child?child->parallel_owner:NULL;
    if(!parallel || parallel->child!=child || parallel->resource_owner_pinned ||
        !parallel->world.prepared || g_prime_native_parallel_current!=parallel ||
        g_prime_eval_stack_driver!=&child->driver || child->driver.control!=child ||
        !child->driver.ordinary_ordered || !child->driver.ordinary_shared_source ||
        child->driver.arena!=arena || space!=parallel->world.space ||
        child->source_read.space!=space || g_registry!=&parallel->world.registry ||
        child->failed || child->invalidated || child->driver.capacity_blocked ||
        !prime_native_parallel_borrowed_current(parallel,g_library_context) ||
        !space_read_token_is_current(child->source_read) ||
        !prime_native_control_query_reads_current(child) ||
        !prime_eval_stack_target_is_owned(target) ||
        (target->payload_owner && target->payload_owner!=arena) ||
        !cetta_shared_transition_scope_active() ||
        (!child->worker_execution && !cetta_shared_transition_guard_held_by_current_thread()) ||
        !prime_native_actual_call_allowed(space,arena,call))return false;
    RegistryReadView registry=child->registry_read;
    return registry_read_view_validate(&registry,g_registry) &&
        petta_program_classify_resolved_call(g_library_context->petta_program,
            space,call)==PETTA_RESOLVED_CALL_MACHINE_LOCAL;
}

static bool prime_native_parallel_validate(PrimeNativeControl *,PrimeNativeParallel *);

static bool prime_native_parallel_prepare(PrimeNativeControl *owner,
                                          PrimeNativeWork *work) {
    if(work->licence)return false;
    if (work->parallel && work->parallel->child &&
        work->parallel->child->driver.capsule_stack &&
        (work->parallel->child->driver.capsule_frontier ||
         cetta_native_cost_active() || owner->policy.receipts ||
         cetta_native_cost_observer_active()
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
         || owner->history.spec.enabled || g_prime_need_recording_scope
#endif
         )) return false;
    if(work->prepared_publication) {
        PrimeNativeParallel *paid=work->parallel;
        /* A full ordinary DFS run may extend an unfinished private sibling
         * while its earlier output awaits logical selection. Its rows and
         * forwarding cursor remain intact; each new slice has a fresh paid
         * interval. No selected return or completed child is run again. */
        if(!owner->driver.ordinary_ordered || !paid ||
            paid->execution.profile!=PRIME_NATIVE_EXECUTION_NONE ||
            paid->resource_provider || work->dialect || paid->coordinator_only ||
            paid->completed || !paid->child || paid->child->failed ||
            paid->child->invalidated || paid->child->effect_pending ||
            paid->child->need_pending || paid->child->driver.capacity_blocked ||
            prime_native_control_closed(paid->child) ||
            prime_native_control_waiting_verdict(paid->child) ||
            !space_read_token_is_current(paid->child->source_read) ||
            !registry_read_view_validate(&paid->child->registry_read,&paid->world.registry) ||
            !prime_native_parallel_validate(owner,paid))return false;
    }
    if(work->resource_step) {
        CettaSpaceAgendaMatch *match=cetta_space_agenda_match_capture(work->resource_provider);
        if(!match)return false;
        if(!work->parallel) {
            work->parallel=calloc(1u,sizeof(*work->parallel));
            if(!work->parallel)return false;
            atomic_init(&work->parallel->references,1u);
            work->parallel->context=eval_context_capture();
            work->parallel->resource_provider=work->resource_provider;
        }
        work->parallel->resource_match=match;
        return true;
    }
    if (work->dialect)
        return native_dialect_parallel_prepare(owner, work);
    /* Extended host calls here construct return frames on the shared
     * parent frontier. Their coefficient/body/fold evaluations are the
     * dialect operations above. Moving the constructor into a private
     * child agenda would change the host's order at allowance boundaries. */
    if (eval_current_language_id() != CETTA_LANGUAGE_PRIME) return false;
    if (!work->task_initialized && work->frame)
        return prime_native_execution_prepare(owner, work);
    if (work->parallel)
        return !work->parallel->coordinator_only && !work->parallel->child->need_pending;
    if (!work->task_initialized || !work->task.target || work->parallel_refused ||
        active_search_table_mode() != CETTA_TABLE_MODE_NONE) return false;
    PrimeEvalStackTask *task = &work->task;
    Arena *arena = owner->driver.arena;
    Atom *expression = bindings_apply_if_vars(&task->dynamic_env, arena, task->atom);
    const PrimeNeedSnapshot *need = bindings_need_view(&task->dynamic_env);
    PrimeEvalStackFrame *force_owner = prime_native_force_owner(work);
    uint64_t activation = prime_native_equation_attachment(work);
    PrimeNativeAttachment attachment = force_owner ? PRIME_NATIVE_ATTACHMENT_FORCE
        : activation ? PRIME_NATIVE_ATTACHMENT_EQUATION
        : prime_native_return_attachment(owner, work) ? PRIME_NATIVE_ATTACHMENT_RETURN
        : PRIME_NATIVE_ATTACHMENT_NONE;
    if (!expression || !prime_native_parallel_support_admitted(
            need, expression, attachment, task->evaluator_id)) return false;
    /* The expression and captured world cross together. An observation of
     * a suspension reconstructs its source syntax and is not a transport of
     * the cell's memoized choice. */
    PrimeNativeParallel *parallel = calloc(1u, sizeof(*parallel));
    if (!parallel) return false;
    atomic_init(&parallel->references, 1u);
    parallel->resource_owner = eval_persistent_arena();
    parallel->resource_universe = eval_current_term_universe();
    parallel->context = eval_context_capture();
    variant_bank_init(&parallel->variants, (CettaVariantShapeOptions){
        .slot_policy = CETTA_VARIANT_SLOT_FIXED_SPELLING, .slot_name = "$_slot",
        .share_immutable = true,
    });
    parallel->variants_ready = true;
    bool shared_source=owner->driver.ordinary_shared_source &&
        task->space==owner->source_read.space &&
        prime_ordinary_shared_task_admitted(task->space,arena,expression);
    if(shared_source) {
        shared_source=space_read_view_capture(task->space,&parallel->borrowed_source) &&
            space_read_view_has_native_occurrences(&parallel->borrowed_source) &&
            space_read_view_is_current(&parallel->borrowed_source);
        if(!shared_source)space_read_view_release(&parallel->borrowed_source);
        else {
            parallel->borrowed_library=g_library_context;
            parallel->borrowed_library_revision=cetta_library_petta_admission_revision(g_library_context);
            parallel->borrowed_symbol_instance=symbol_table_instance_id(g_symbols);
        }
    }
    __attribute__((cleanup(prime_native_restore_hashcons)))
    HashConsTable *previous_hashcons = g_hashcons;
    g_hashcons = &parallel->world.hashcons;
    /* Only preparation/ownership is reused here. Hyperpose's eager branch
     * execution and result observer are not involved. */
    bool prepared = hyperpose_prepare_thread_branch(&parallel->world, 0u,
        task->space, &owner->registry_read.initial, expression, NULL,
        NULL, shared_source, true, need, NULL);
    if (!prepared) { prime_native_parallel_destroy(parallel); return false; }
    for (CettaCount i = 0u; i < parallel->world.owned_spaces.len; i++)
        parallel->world.owned_spaces.items[i].petta_program = g_library_context->petta_program;
    Bindings captured;
    bindings_init(&captured);
    if (!bindings_clone(&captured, &task->dynamic_env) ||
        !bindings_promote_atoms_to_arena(&captured, &parallel->world.persistent_arena)) {
        bindings_free(&captured);
        prime_native_parallel_destroy(parallel);
        return false;
    }
    parallel->captured_need = *bindings_need_view(&captured);
    PrimeNeedActiveGuard active = prime_need_active_enter(&captured);
    Registry *previous_registry = g_registry;
    g_registry = &parallel->world.registry;
    PrimeNativeControl *child = calloc(1u, sizeof(*child));
    parallel->child = child;
    CettaSearchPolicySpec policy = {
        .native_control = true, .all = true, .age_share = 8u,
        .work_budget = UINT64_MAX, .costs = owner->cost_enabled, .workers = 1u,
        .receipts = owner->policy.receipts,
        .prepared_node_quantum = owner->policy.prepared_node_quantum,
    };
    bool ready = child && prime_native_control_init(child, parallel->world.space,
        &parallel->world.eval_arena, parallel->world.branch, task->fuel, true,
        (CettaObservationDemand){.completion = CETTA_OBSERVATION_COMPLETE_BAG}, &policy,
        &captured, NULL, NULL);
    g_registry = previous_registry;
    prime_need_active_leave(&active);
    bindings_free(&captured);
    if (!ready || cetta_continuation_hub_length(&child->hub) != 1u) {
        prime_native_parallel_destroy(parallel); return false;
    }
    if (owner->driver.ordinary_ordered) {
        CettaSelectionAutomaton order;
        child->driver.ordinary_ordered=true;
        child->driver.ordinary_shared_source=shared_source;
        child->driver.ordinary_ready_queue=owner->driver.ordinary_ready_queue;
        child->driver.ordinary_refill=owner->driver.ordinary_refill;
        child->driver.ordinary_descendants=owner->driver.ordinary_descendants;
        child->hub.selection_duty=CETTA_SELECTION_DUTY_NONE;
        if (!cetta_selection_automaton_lifo(&order) ||
            !cetta_continuation_hub_switch_schedule(&child->hub,&order)) {
            prime_native_parallel_destroy(parallel);return false;
        }
    }
    child->pure_authority = owner->pure_authority || work->scoring || task->pure_authority;
    child->parallel_owner = parallel;
    PrimeNativeWork *root = cetta_continuation_hub_at(&child->hub, 0u)->payload;
    root->task.kind = task->kind;
    root->task.pure_authority = task->pure_authority;
    root->task.etype = task->etype ? atom_deep_copy(child->driver.arena, task->etype) : NULL;
    root->task.preserve_bindings = task->preserve_bindings;
    root->task.strict_ready_argument = task->strict_ready_argument;
    root->task.evaluator_id = task->evaluator_id;
    /* The child evaluates this one captured task. An advisory result returns
     * only to the original score frame, whose generation check remains on
     * the coordinator. Fuel is the task's semantic path allowance, separate
     * from the observation budget granted to each worker episode. */
    if (task->seed_env_initialized) {
        ready = bindings_clone(&root->task.seed_env, &task->seed_env);
        root->task.seed_env_initialized = ready;
        ready = ready && bindings_promote_atoms_to_arena(&root->task.seed_env, child->driver.arena);
    }
    if (!ready) { prime_native_parallel_destroy(parallel); return false; }
    if (owner->driver.ordinary_ordered && (shared_source || prime_native_private_capsule_source(parallel)) &&
        owner->driver.ordinary_descendants && task->fuel < 0 &&
        !owner->cost_enabled && !owner->policy.receipts &&
        !cetta_native_cost_active() && !cetta_native_cost_observer_active()
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
        && !owner->history.spec.enabled && !g_prime_need_recording_scope
#endif
        ) {
        (void)prime_ordinary_capsule_stack_start(child);
    }
    if (force_owner) {
        parallel->force_thunk_id = force_owner->thunk_id;
        parallel->force_evaluator_id = force_owner->evaluator_id;
        force_owner->control_force_detached = true;
    }
    parallel->attachment = attachment;
    parallel->activation_occurrence = activation;
    work->parallel = parallel;
    return true;
}

static void prime_native_parallel_retain(void *raw) {
    PrimeNativeParallel *parallel = raw;
    size_t previous = atomic_fetch_add_explicit(&parallel->references, 1u, memory_order_relaxed);
    assert(previous > 0u && previous < SIZE_MAX);
}
static void prime_native_parallel_release(void *raw) {
    prime_native_parallel_destroy(raw);
}
static void prime_native_parallel_owner_storage(const void *raw, CettaStorageCensus *census) {
    prime_native_parallel_storage(raw, census);
}

/* Effectful publication can put an identity into a Space or StateCell after
 * the answer Bindings have gone away. Pin the borrowed continuation world at
 * that actual persistent root, using the existing arena owner mechanism. */
static bool prime_native_parallel_pin_effect(PrimeNativeParallel *parallel) {
    if (!parallel || parallel->resource_owner_pinned) return true;
    /* This profile has no retained source/service owner beyond the ordinary
     * synchronous extent. A persistent effect cannot pin a borrowed view. */
    if(parallel->borrowed_source.root.space || parallel->coordinator_private)return false;
    if (!parallel->resource_owner || !arena_retain_owner_with_storage(
            parallel->resource_owner, parallel, prime_native_parallel_retain,
            prime_native_parallel_release, prime_native_parallel_owner_storage)) return false;
    parallel->resource_owner_pinned = true;
    parallel->coordinator_only = true;
    return true;
}

static void prime_native_effect_receipt(PrimeNativeControl *control,
        const char *status, Atom *call) {
    if (!control->policy.receipts) return;
    Arena *arena = control->driver.arena;
    uint64_t producer = control->parallel_owner ? control->parallel_owner->occurrence : 0u;
    Atom *parts[] = {atom_symbol(arena, "NativeEffectBarrier"),
        prime_native_occurrence_atom(arena, producer),
        prime_native_occurrence_atom(arena, control->active_occurrence),
        atom_symbol(arena, status), atom_deep_copy(arena, call)};
    control->effect_barriers = cetta_realloc(control->effect_barriers,
        (control->effect_barrier_count + 1u) * sizeof(*control->effect_barriers));
    control->effect_barriers[control->effect_barrier_count++] = atom_expr(arena, parts, 5u);
}

static void prime_native_need_receipt(PrimeNativeControl *control,
        const PrimeNativeParallel *parallel, const char *status, uint64_t thunk_id) {
    if (!control->policy.receipts) return;
    Arena *arena = control->driver.arena;
    Atom *parts[] = {atom_symbol(arena, "NativeNeedBarrier"),
        prime_native_occurrence_atom(arena, parallel->occurrence),
        prime_native_occurrence_atom(arena, parallel->captured_need.session_id),
        prime_native_occurrence_atom(arena, thunk_id), atom_symbol(arena, status),
        atom_symbol(arena, parallel->attachment == PRIME_NATIVE_ATTACHMENT_FORCE
            ? "Force" : parallel->attachment == PRIME_NATIVE_ATTACHMENT_EQUATION
            ? "Equation" : "Return"),
        prime_native_occurrence_atom(arena, parallel->activation_occurrence
            ? parallel->activation_occurrence : parallel->force_thunk_id
            ? parallel->force_thunk_id : parallel->occurrence)};
    control->need_barriers = cetta_realloc(control->need_barriers,
        (control->need_barrier_count + 1u) * sizeof(*control->need_barriers));
    control->need_barriers[control->need_barrier_count++] = atom_expr(arena, parts, 7u);
}

static bool prime_native_parallel_force_barrier(
        Space *space, Arena *arena, Atom *ref, uint64_t thunk_id, int fuel,
        OutcomeSet *target) {
    PrimeNativeParallel *parallel = g_prime_native_parallel_current;
    PrimeEvalStackDriver *driver = g_prime_eval_stack_driver;
    PrimeNativeControl *control = driver ? driver->control : NULL;
    if (!parallel || parallel->attachment == PRIME_NATIVE_ATTACHMENT_NONE || !control ||
        parallel->captured_need.session_id != g_prime_need_active.session_id)
        return false;
    PrimeNeedCellView captured, current;
    if (!prime_need_snapshot_lookup(&parallel->captured_need, thunk_id, &captured) ||
        !prime_need_snapshot_lookup(&g_prime_need_active, thunk_id, &current) ||
        captured.authority_id != current.authority_id ||
        current.cache_state == PRIME_NEED_CACHE_VALUE ||
        current.cache_state == PRIME_NEED_CACHE_STABLE_FAULT)
        return false;
    /* Self-demand remains the native blackhole. A private producer created
     * after capture has no entry in captured_need and proceeds locally. */
    if (current.cache_state == PRIME_NEED_CACHE_EVALUATING &&
        current.evaluator_id == g_prime_need_evaluator_id)
        return false;
    if (!control->worker_execution) {
        prime_native_need_receipt(control, parallel, "Coordinator", thunk_id);
        return false;
    }
    if (!prime_eval_stack_schedule_call(space, arena, ref, NULL, fuel, true, -1,
            g_prime_need_logical_env, g_prime_need_logical_env,
            g_prime_need_evaluator_id, target)) {
        eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_CAPACITY);
        return true;
    }
    prime_native_need_receipt(control, parallel, "Suspended", thunk_id);
    control->need_pending = true;
    return true;
}

static bool prime_native_parallel_ignore(Arena *arena, Atom *value,
        const Bindings *environment, void *context) {
    (void)arena; (void)value; (void)environment; (void)context;
    return true;
}

static uint64_t prime_native_monotonic_ns(void) {
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) != 0) return 0u;
    return (uint64_t)time.tv_sec * UINT64_C(1000000000) + (uint64_t)time.tv_nsec;
}

static bool prime_native_resource_match_task(CettaParallelWorker *worker,
        PrimeNativeParallel *parallel, PrimeNativeParallelRun *run) {
    parallel->attempted=true;parallel->worker=cetta_parallel_worker_index(worker);
    if(run->receipts) {
        parallel->started_ns=prime_native_monotonic_ns();
        uint32_t active=atomic_fetch_add_explicit(&run->active,1u,memory_order_acq_rel)+1u;
        uint32_t previous=atomic_load_explicit(&run->overlap,memory_order_relaxed);
        while(active>previous && !atomic_compare_exchange_weak_explicit(
            &run->overlap,&previous,active,memory_order_relaxed,memory_order_relaxed)) {}
    }
    CettaLibraryContext *library=g_library_context;
    EvalContextState previous_context=eval_context_capture();
    eval_swap_library_context(run->library);eval_context_restore(&parallel->context);
    CettaNativeCostScope *containing=cetta_native_cost_detach_begin(),cost;
    if(parallel->metered)cetta_native_cost_scope_enter(&cost,&parallel->slice_cost,parallel->occurrence);
    cetta_native_cost_charge(CETTA_COST_QUANTUM,1u);
    parallel->spent=cetta_space_agenda_match_run(parallel->resource_match,
        CETTA_SPACE_AGENDA_MATCH_NODES)?1u:0u;
    if(parallel->metered)cetta_native_cost_scope_leave(&cost);
    cetta_native_cost_detach_end(containing);
    eval_swap_library_context(library);eval_context_restore(&previous_context);
    if(run->receipts) {
        parallel->finished_ns=prime_native_monotonic_ns();
        atomic_fetch_sub_explicit(&run->active,1u,memory_order_acq_rel);
    }
    return parallel->spent==1u;
}


static bool prime_native_parallel_task(CettaParallelWorker *worker, void *raw,
                                       void *context) {
    PrimeNativeParallel *parallel = raw;
    PrimeNativeParallelRun *run = context;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    __attribute__((cleanup(prime_need_recording_leave)))
    PrimeNeedRecordingScope history_scope = {0};
    if (parallel->history_enabled)
        prime_need_recording_enter(&history_scope, &parallel->history);
#endif
    if(parallel->resource_provider)return prime_native_resource_match_task(worker,parallel,run);
    if (parallel->execution.profile != PRIME_NATIVE_EXECUTION_NONE)
        return prime_native_execution_task(worker, parallel, run);
    PrimeNativeControl *child = parallel->child;
    if(parallel->borrowed_source.root.space &&
        !prime_native_parallel_borrowed_current(parallel,run->library)) {
        parallel->attempted=true;parallel->spent=0u;child->invalidated=true;return false;
    }
    PrimeNativeParallel *previous_parallel = g_prime_native_parallel_current;
    g_prime_native_parallel_current = parallel;
    bool resource_execution = run->coordinator && parallel->coordinator_only &&
        !parallel->coordinator_private;
    parallel->worker = cetta_parallel_worker_index(worker);
    parallel->attempted = true;
    if (run->receipts) {
        parallel->started_ns = prime_native_monotonic_ns();
        uint32_t active = atomic_fetch_add_explicit(&run->active, 1u, memory_order_acq_rel) + 1u;
        uint32_t previous = atomic_load_explicit(&run->overlap, memory_order_relaxed);
        while (active > previous && !atomic_compare_exchange_weak_explicit(
            &run->overlap, &previous, active, memory_order_relaxed, memory_order_relaxed)) {}
    }
    Registry *registry = g_registry;
    Space *root = g_eval_root_space;
    CettaLibraryContext *library = g_library_context;
    EvalContextState previous_context = eval_context_capture();
    HashConsTable *hashcons = g_hashcons;
    EvalCompletionTracker *tracker = g_eval_completion_tracker;
    VariantBank *variants = g_owned_outcome_variant_bank;
    TempSpaceSet temporary = g_temp_spaces, created = g_new_space_track;
    const PettaProgramRevisionProjection *projection = g_petta_program_revision_projection;
    Arena *persistent = g_eval_fallback_universe.persistent_arena;
    if (!resource_execution) {
        g_temp_spaces = parallel->temporary_spaces;
        g_new_space_track = parallel->created_spaces;
        parallel->temporary_spaces = (TempSpaceSet){0};
        parallel->created_spaces = (TempSpaceSet){0};
    }
    g_owned_outcome_variant_bank = &parallel->variants;
    EvalCompletionTracker completion = {.completion = CETTA_EVAL_COMPLETE};
    _Atomic bool *cancel = eval_set_cancel_token(&run->cancelled);
    EvalTermUniverseScope universe = {0};
    eval_term_universe_scope_enter(&universe, resource_execution
        ? parallel->resource_universe : &parallel->world.term_universe);
    g_registry = resource_execution ? registry : &parallel->world.registry;
    g_eval_root_space = resource_execution ? root : parallel->world.space;
    g_hashcons = &parallel->world.hashcons;
    g_eval_completion_tracker = &completion;
    eval_swap_library_context(run->library);
    eval_context_restore(&parallel->context);
    term_universe_set_persistent_arena(&g_eval_fallback_universe, resource_execution
        ? parallel->resource_owner : &parallel->world.persistent_arena);
    g_petta_program_revision_projection = parallel->world.program_projection.view
        ? &parallel->world.program_projection : NULL;
    CettaNativeCostScope *containing_cost = cetta_native_cost_detach_begin();
    CettaNativeCostScope cost;
    bool metered = parallel->metered;
    if (metered) cetta_native_cost_scope_enter(&cost, &parallel->slice_cost, parallel->occurrence);
    child->worker_execution = !run->coordinator;
    uint64_t before = child->steps;
    if (run->descendants) {
        /* No parent publication or interpreter transition runs concurrently
         * with this issued child frontier. The ordinary wave owns each
         * descendant interval and joins it before this service scope exits. */
        assert(run->coordinator && !cetta_parallel_worker_active() &&
            !cetta_shared_transition_guard_held_by_current_thread() && !metered);
        PrimeNativeRunGuard guard;
        prime_native_run_enter(child,&guard);
        uint32_t workers=child->policy.workers;
        uint64_t quantum=child->policy.worker_quantum;
        child->policy.workers=run->descendant_workers;
        child->policy.worker_quantum=run->descendant_quantum;
        if(parallel->descendant_pending)child->driver.capacity_blocked=false;
        uint64_t spent=0u;
        bool waved=prime_native_control_parallel_wave(child,parallel->grant,&spent);
        if(!waved || (!spent && !child->parallel_wave_pending))parallel->descendant_refused=true;
        assert(spent<=parallel->grant);
        child->steps+=spent;
        parallel->descendant_pending=child->parallel_wave_pending;
        child->policy.workers=workers;child->policy.worker_quantum=quantum;
        if(child->driver.capacity_blocked)child->completion=CETTA_EVAL_INCOMPLETE_CAPACITY;
        prime_native_run_leave(&guard);
        parallel->spent=child->steps-before;
        parallel->completed=prime_native_control_closed(child);
    } else if (parallel->dialect_operation) {
        PrimeNativeRunGuard guard;
        prime_native_run_enter(child, &guard);
        __attribute__((cleanup(prime_eval_authority_leave)))
        PrimeEvalAuthorityGuard authority = prime_eval_authority_enter(
            child->pure_authority || parallel->dialect_operation->scoring ||
            native_dialect_work_pure(parallel->dialect_operation->dialect));
        child->active_occurrence = parallel->occurrence;
        child->active_scoring = parallel->dialect_operation->scoring;
        native_dialect_parallel_operation(parallel->dialect_operation);
        cetta_native_cost_charge(CETTA_COST_QUANTUM, 1u);
        child->steps++;
        child->active_occurrence = 0u;
        child->active_scoring = false;
        prime_native_run_leave(&guard);
        parallel->spent = 1u;
        parallel->completed = false;
    } else if (child->driver.capsule_stack) {
        prime_ordinary_capsule_stack_run(child,parallel->grant);
        parallel->spent=child->steps-before;
        parallel->completed=prime_native_control_closed(child);
    } else {
        prime_native_control_run(child, parallel->grant, prime_native_parallel_ignore, NULL);
        parallel->spent = child->steps - before;
        parallel->completed = prime_native_control_closed(child);
    }
    if (child->effect_pending) parallel->coordinator_only = true;
    if (metered) cetta_native_cost_scope_leave(&cost);
    cetta_native_cost_detach_end(containing_cost);
    if (!resource_execution) {
        if (!hyperpose_adopt_tracked_spaces(&parallel->world))
            child->driver.capacity_blocked = true;
        parallel->temporary_spaces = g_temp_spaces;
        parallel->created_spaces = g_new_space_track;
        g_temp_spaces = temporary;
        g_new_space_track = created;
    }
    g_owned_outcome_variant_bank = variants;
    g_petta_program_revision_projection = projection;
    term_universe_set_persistent_arena(&g_eval_fallback_universe, persistent);
    eval_swap_library_context(library);
    eval_context_restore(&previous_context);
    g_registry = registry;
    g_eval_root_space = root;
    g_hashcons = hashcons;
    g_eval_completion_tracker = tracker;
    eval_set_cancel_token(cancel);
    eval_term_universe_scope_leave(&universe);
    g_prime_native_parallel_current = previous_parallel;
    if (run->receipts) {
        parallel->finished_ns = prime_native_monotonic_ns();
        atomic_fetch_sub_explicit(&run->active, 1u, memory_order_acq_rel);
    }
    return true;
}

static void prime_native_parallel_enter(CettaParallelWorker *worker, void *context) {
    (void)worker; (void)context;
    cetta_shared_transition_scope_enter();
}
static void prime_native_parallel_leave(CettaParallelWorker *worker, void *context) {
    (void)worker; (void)context;
    eval_cleanup_owned_new_spaces_for_current_thread();
    eval_match_decision_cache_free_for_current_thread();
    eval_profiled_type_cache_free_for_current_thread();
    space_execution_analysis_cache_free_for_current_thread();
    bindings_thread_cache_free();
    cetta_shared_transition_scope_leave();
}

static bool prime_native_parallel_validate(PrimeNativeControl *owner,
                                           PrimeNativeParallel *parallel) {
    if (!space_read_token_is_current(owner->source_read)) return false;
    if(parallel->borrowed_source.root.space &&
        !prime_native_parallel_borrowed_current(parallel,g_library_context))return false;
    if (!prime_native_control_query_reads_current(owner) ||
        !prime_native_control_query_reads_current(parallel->child)) return false;
    Registry *consulted = &parallel->child->registry_read.consulted;
    for (uint32_t i = 0u; i < consulted->len; i++) {
        RegistryEntry entry = consulted->entries[i];
        if (entry.key != SYMBOL_ID_NONE) (void)registry_lookup_id(g_registry, entry.key);
        else (void)registry_lookup_name(g_registry,
            (Atom *)registry_entry_name_key(consulted, i));
    }
    return !owner->registry_read.stale && !owner->registry_read.incomplete;
}

static bool prime_native_parallel_publish(PrimeNativeControl *owner,
                                          PrimeNativeWork *work) {
    PrimeNativeParallel *parallel = work->parallel;
    OutcomeSet *target = work->dialect ? native_dialect_work_target(work->dialect)
        : work->task.target;
    bool preserve_bindings = work->dialect || work->task.preserve_bindings;
    for (; parallel->need_barriers_forwarded < parallel->child->need_barrier_count;
         parallel->need_barriers_forwarded++) {
        if (!owner->policy.receipts) continue;
        owner->need_barriers = cetta_realloc(owner->need_barriers,
            (owner->need_barrier_count + 1u) * sizeof(*owner->need_barriers));
        owner->need_barriers[owner->need_barrier_count++] = atom_deep_copy(
            owner->driver.arena, parallel->child->need_barriers[parallel->need_barriers_forwarded]);
    }
    for (; parallel->barriers_forwarded < parallel->child->effect_barrier_count;
         parallel->barriers_forwarded++) {
        if (!owner->policy.receipts) continue;
        owner->effect_barriers = cetta_realloc(owner->effect_barriers,
            (owner->effect_barrier_count + 1u) * sizeof(*owner->effect_barriers));
        owner->effect_barriers[owner->effect_barrier_count++] = atom_deep_copy(
            owner->driver.arena, parallel->child->effect_barriers[parallel->barriers_forwarded]);
    }
    if (!prime_native_parallel_validate(owner, parallel)) {
        owner->invalidated = true;
        return false;
    }
    if (parallel->child->failed || parallel->child->invalidated ||
        parallel->child->registry_read.incomplete) {
        owner->completion = parallel->child->completion;
        if (owner->completion != CETTA_EVAL_COMPLETE)
            eval_mark_incomplete(owner->completion);
        owner->failed = true;
        return false;
    }
    /* The initial profile has one non-overlay source and no external query
     * owner. Unexpected dependencies remain a refused pending operation;
     * they never retain a capsule with a dangling borrowed Space. */
    if(parallel->borrowed_source.root.space && parallel->child->query_read_count) {
        owner->driver.capacity_blocked=true;return false;
    }
    if (parallel->child->query_read_count) {
        /* A read may refer to a private resource created in this executor.
         * The containing residual retains that resource owner alongside
         * the imported source token. */
        if (!arena_retain_owner_with_storage(owner->driver.arena,parallel,
                prime_native_parallel_retain,prime_native_parallel_release,
                prime_native_parallel_owner_storage)) {
            owner->driver.capacity_blocked=true;return false;
        }
        for (size_t i=0u;i<parallel->child->query_read_count;i++)
            if (!prime_native_control_record_query_read(owner,parallel->child->query_reads[i])) {
                if (!owner->invalidated) owner->driver.capacity_blocked=true;
                return false;
            }
    }
    for (; parallel->forwarded < parallel->child->published; parallel->forwarded++) {
        Outcome *item = &parallel->child->output.items[parallel->forwarded];
        if (!outcome_inflate_answer_ref(parallel->child->driver.arena, item)) {
            owner->driver.capacity_blocked = true;
            return false;
        }
        CettaCount before = target->len;
        if (!outcome_set_add_promoted_existing(owner->driver.arena,
                target, item, preserve_bindings)) {
            owner->driver.capacity_blocked = true;
            return false;
        }
        /* An ordinary outcome promotion shares the enclosing Need episode.
         * A detached episode instead transfers its complete world before
         * the private owner may be retired. */
        Outcome *promoted = &target->items[before];
        Arena *receiving=outcome_set_payload_arena(target,owner->driver.arena);
        if (!bindings_promote_atoms_to_arena(&promoted->env,receiving) ||
            (parallel->borrowed_source.root.space &&
             !prime_ordinary_received_outcome(receiving,promoted))) {
            outcome_free_fields(promoted);
            target->len = before;
            owner->driver.capacity_blocked = true;
            return false;
        }
    }
    prime_eval_stack_stream_note(target);
    return true;
}

/* Read dependencies are part of the paid packet's publication authority.
 * Moving application to parent selection must not discard empty query reads. */
static bool prime_native_parallel_apply_ready(PrimeNativeControl *owner,PrimeNativeWork *work) {
    return !work->parallel || !work->prepared_publication ||
        prime_native_parallel_publish(owner,work);
}

static void prime_native_parallel_slice_begin(PrimeNativeControl *owner,
        PrimeNativeParallel *parallel, uint64_t grant, uint64_t occurrence) {
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    parallel->history_enabled = owner->history.spec.enabled ||
        g_prime_need_recording_scope != NULL;
    parallel->history = owner->history.spec.enabled
        ? prime_native_history_reserve(owner) : (PrimeNeedReceipt){0};
    /* An inherited private slice is never writable by nested workers.
     * Their unrepresented detail remains explicitly missing in the outer
     * graph; the evaluator's nested dispatch is unchanged. */
    if (!owner->history.spec.enabled && parallel->history_enabled)
        prime_need_recording_omit_scope();
    if (owner->history.active && owner->history.active->occurrence == occurrence) {
        owner->history.active->prepared = parallel->history;
        owner->history.active->preparation = true;
    }
#endif
    cetta_native_cost_free(&parallel->slice_cost);
    /* An inner agenda may have no public meter of its own while executing
     * inside an enclosing observation. Its physical worker slice still
     * belongs to that observation, with the original event identities. */
    parallel->metered = cetta_native_cost_active();
    if (parallel->metered) cetta_native_cost_init(&parallel->slice_cost);
    if (parallel->child && parallel->execution.profile == PRIME_NATIVE_EXECUTION_NONE)
        parallel->child->policy.receipts = owner->policy.receipts;
    parallel->grant = grant;
    parallel->spent = 0u;
    parallel->occurrence = occurrence;
    parallel->attempted = false;
    parallel->completed = false;
    parallel->phase = 0u;
}

static bool prime_native_parallel_recover(PrimeNativeControl *owner) {
    if (owner->parallel_wave_pending) return true;
    for (size_t i = 0u; i < owner->parallel_inflight_count; i++) {
        CettaOwnedContinuation *owned = &owner->parallel_inflight[i];
        if (!owned->payload) continue;
        if (!owned->resume_lease) {
            /* Borrowed entries remain in their original parent frontier.
             * A revision withdraws only future preparation, not paid state. */
            PrimeNativeWork *work=owned->payload;
            if (work->parallel) {
                if (work->parallel->attempted) work->prepared_publication=true;
                work->parallel->dialect_operation=NULL;
            }
            continue;
        }
        if (owned->payload && !cetta_continuation_hub_requeue(&owner->hub, owned)) {
            owner->driver.capacity_blocked = true;
            return false;
        }
    }
    free(owner->parallel_inflight);
    owner->parallel_inflight = NULL;
    owner->parallel_inflight_count = 0u;
    return true;
}

/* An explicit controller or consumer revision withdraws the old wave grant,
 * retaining each occurrence's exact child world and unpublished returns.
 * Allowance-only resumption never takes this path. */
static bool prime_native_parallel_interrupt_wave(PrimeNativeControl *owner) {
    owner->parallel_wave_pending = false;
    owner->parallel_wave_cursor = 0u;
    return prime_native_parallel_recover(owner);
}

static bool prime_native_parallel_join_owned(PrimeNativeControl *owner,
                                             CettaOwnedContinuation *owned) {
    PrimeNativeWork *work = owned->payload;
    PrimeNativeParallel *parallel = work->parallel;
    assert(!parallel->resource_provider);
    bool completed = parallel->completed;
    bool park = !completed && prime_native_control_waiting_verdict(parallel->child);
    /* Requeue before publication can introduce new producers. The payload's
     * original occurrence identity and its return obligation remain live. */
    if (!completed && !park && !cetta_continuation_hub_requeue(&owner->hub, owned)) {
        owner->driver.capacity_blocked = true;
        return false;
    }
    bool published = prime_native_parallel_publish(owner, work);
    if (published) work->prepared_publication=false;
    if (park && published) {
        size_t count = owner->parked_count;
        if (count == SIZE_MAX / sizeof(*owner->parked)) {
            owner->driver.capacity_blocked = true;
            return false;
        }
        owner->parked = cetta_realloc(owner->parked, (count + 1u) * sizeof(*owner->parked));
        owner->parked_capacity = count + 1u;
        owner->parked[count] = *owned;
        owner->parked_count++;
        *owned = (CettaOwnedContinuation){0};
        return true;
    }
    if (completed && published) {
        PrimeEvalStackFrame *parent = work->parent;
        if (parent) {
            assert(parent->control_pending > 0u);
            parent->control_pending--;
            prime_native_control_notify_closed(owner, parent);
        }
        bool released = cetta_continuation_hub_release_lease(&owner->hub, owned);
        assert(released);
        cetta_owned_continuation_destroy(owned);
    } else if (completed) {
        if (!cetta_continuation_hub_requeue(&owner->hub, owned)) owner->driver.capacity_blocked = true;
    }
    return published;
}

/* The selected outer lease holds this complete private driver through a
 * synchronous descendant wave. Only already-issued equation alternatives
 * may be lent; return frames, Need state and source lifetime stay here. */
static uint64_t prime_native_descendant_grant(PrimeNativeControl *owner,
        const CettaOwnedContinuation *owned) {
    if (!owner || !owned || !owner->driver.ordinary_descendants ||
        !owner->driver.ordinary_ordered || owner->parallel_inflight ||
        owner->parallel_wave_pending || owner->active_allowance < 2u ||
        cetta_parallel_worker_active() || g_prime_native_parallel_current ||
        cetta_shared_transition_guard_held_by_current_thread() ||
        cetta_native_cost_active() || cetta_native_cost_observer_active() ||
        owner->policy.receipts || owned->provider != &prime_native_work_provider ||
        !owned->payload || !owned->resume_lease ||
        owned->resume_store != owner->hub.store.identity ||
        owned->occurrence_id != owner->active_occurrence) return 0u;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if (owner->history.spec.enabled || g_prime_need_recording_scope) return 0u;
#endif
    uint32_t workers=owner->policy.workers?owner->policy.workers:eval_current_num_threads();
    if (workers<2u) return 0u;
    bool leased=false;
    for(size_t i=0u;i<owner->hub.store.lease_length;i++) {
        const CettaContinuationLease *lease=&owner->hub.store.leases[i];
        if(lease->occurrence==owned->occurrence_id && lease->lease==owned->resume_lease &&
           lease->payload==owned->payload && lease->provider==owned->provider) leased=true;
    }
    if(!leased)return 0u;
    PrimeNativeWork *work=owned->payload;
    PrimeNativeParallel *parallel=work->parallel;
    if(work->owner!=owner || !work->task_initialized || work->prepared_publication ||
       work->resource_step || work->dialect || work->licence || !parallel ||
       parallel->execution.profile!=PRIME_NATIVE_EXECUTION_NONE ||
       parallel->resource_provider || parallel->coordinator_only || parallel->refill_item ||
       parallel->descendant_refused || parallel->completed || !parallel->child) return 0u;
    if(work->parent && (!prime_native_frame_index_find(owner,work->parent) ||
       work->parent->control_retired || work->parent->arena!=owner->driver.arena ||
       !work->parent->control_pending))return 0u;
    PrimeEvalStackFrame *target=work->task.target==&owner->output?NULL:
        prime_native_control_target_owner(owner,work->task.target);
    if(work->task.target!=&owner->output && (!target || target->control_retired ||
       target->arena!=owner->driver.arena))return 0u;
    if(!work->task.target || (work->task.target->payload_owner &&
       work->task.target->payload_owner!=owner->driver.arena))return 0u;
    PrimeNativeControl *child=parallel->child;
    if(child->parallel_owner!=parallel || child->driver.control!=child ||
       !child->driver.ordinary_ordered || child->failed || child->invalidated ||
       child->effect_pending || child->need_pending || child->registry_read.incomplete ||
       child->registry_read.stale || child->cost_enabled || child->policy.receipts ||
       child->refill_pending || child->query_read_count ||
       !space_read_token_is_current(owner->source_read) ||
       !space_read_token_is_current(child->source_read))return 0u;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if(child->history.spec.enabled)return 0u;
#endif
    if(parallel->borrowed_source.root.space &&
       !prime_native_parallel_borrowed_current(parallel,g_library_context))return 0u;
    RegistryReadView registry=child->registry_read;
    if(!registry_read_view_validate(&registry,&parallel->world.registry))return 0u;
    if(parallel->descendant_pending) {
        if(!child->parallel_wave_pending)return 0u;
        return owner->active_allowance;
    }
    if(child->driver.capacity_blocked || child->parallel_wave_pending ||
       child->parallel_inflight || prime_native_control_closed(child) ||
       prime_native_control_waiting_verdict(child))return 0u;
    for(size_t i=0u;i<cetta_continuation_hub_length(&child->hub);i++) {
        const CettaOwnedContinuation *entry=cetta_continuation_hub_at(&child->hub,i);
        if(entry->provider!=&prime_native_work_provider || !entry->payload)return 0u;
    }
    PrimeNativeWork *first=prime_native_control_peek(child);
    if(!first || first->prepared_publication || !first->task_initialized || !first->parent ||
       !prime_native_frame_index_find(child,first->parent) ||
       first->parent->control_retired ||
       first->parent->kind!=PRIME_EVAL_STACK_FRAME_EQUATION_SEARCH)return 0u;
    PrimeEvalStackFrame *parent=first->parent;
    if(parent->arena!=child->driver.arena || !parent->target)return 0u;
    PrimeEvalStackFrame *receiver=parent->target==&child->output?NULL:
        prime_native_control_target_owner(child,parent->target);
    if(parent->target!=&child->output && (!receiver || receiver->control_retired ||
       receiver->arena!=child->driver.arena))return 0u;
    if(parent->target->payload_owner && parent->target->payload_owner!=child->driver.arena)return 0u;
    size_t count=0u;
    for(size_t i=0u;i<cetta_continuation_hub_length(&child->hub);i++) {
        const CettaOwnedContinuation *entry=cetta_continuation_hub_at(&child->hub,i);
        if(entry->provider!=&prime_native_work_provider || !entry->payload)return 0u;
        PrimeNativeWork *candidate=entry->payload;
        if(!candidate->task_initialized)continue;
        if(entry->resume_lease ||
           candidate->owner!=child || candidate->parent!=parent || candidate->resource_step ||
           candidate->dialect || candidate->licence ||
           !prime_native_equation_attachment(candidate))return 0u;
        count++;
    }
    if(count<2u || parent->control_pending<count)return 0u;
    size_t capacity=(size_t)workers;
    if(child->driver.ordinary_ready_queue) {
        if(capacity>SIZE_MAX/2u)return 0u;
        capacity*=2u;
    }
    if(count>capacity)count=capacity;
    uint64_t quantum=owner->policy.worker_quantum?owner->policy.worker_quantum:1u;
    uint64_t factor=child->driver.ordinary_refill?4u:1u;
    if(quantum>UINT64_MAX/factor || quantum*factor>UINT64_MAX/count)return 0u;
    uint64_t grant=quantum*factor*(uint64_t)count;
    return grant<=owner->active_allowance?grant:0u;
}

static void prime_native_parallel_single(PrimeNativeControl *owner,
                                         CettaOwnedContinuation *owned) {
    PrimeNativeWork *candidate = owned->payload;
    if (candidate->parallel->execution.profile != PRIME_NATIVE_EXECUTION_NONE) {
        prime_native_execution_single(owner, owned);
        return;
    }
    __attribute__((cleanup(eval_shared_transition_scope_leave)))
    bool coordinator_scope = !cetta_shared_transition_scope_active();
    if (coordinator_scope) cetta_shared_transition_scope_enter();
    PrimeNativeControl *stack_child=candidate->parallel->child;
    bool needs_adoption=stack_child && stack_child->driver.capsule_stack &&
        (stack_child->driver.capsule_frontier || stack_child->need_pending ||
         stack_child->effect_pending || cetta_native_cost_active() ||
         cetta_native_cost_observer_active() || owner->policy.receipts
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
         || owner->history.spec.enabled || g_prime_need_recording_scope
#endif
         );
    if (needs_adoption &&
        (!prime_native_parallel_validate(owner,candidate->parallel) ||
         !registry_read_view_validate(&stack_child->registry_read,
             &candidate->parallel->world.registry) ||
         !prime_ordinary_capsule_stack_adopt(stack_child))) {
        owner->active_spent=0u;
        if (!cetta_continuation_hub_requeue(&owner->hub,owned)) {
            assert(!owner->parallel_inflight);
            owner->parallel_inflight=cetta_malloc(sizeof(*owned));
            owner->parallel_inflight_count=1u;owner->parallel_inflight[0]=*owned;
            *owned=(CettaOwnedContinuation){0};
        }
        owner->driver.capacity_blocked=true;
        return;
    }
    uint64_t descendant_grant=prime_native_descendant_grant(owner,owned);
    if(candidate->parallel->descendant_pending && !descendant_grant) {
        /* An issued frontier cannot be reinterpreted as a generic guarded
         * single. Keep its paid state and lease until its exact profile can
         * resume; changed authority never licenses a fallback execution. */
        owner->active_spent=0u;
        if(!cetta_continuation_hub_requeue(&owner->hub,owned)) {
            assert(!owner->parallel_inflight);
            owner->parallel_inflight=cetta_malloc(sizeof(*owned));
            owner->parallel_inflight_count=1u;owner->parallel_inflight[0]=*owned;
            *owned=(CettaOwnedContinuation){0};
        }
        owner->driver.capacity_blocked=true;
        return;
    }
    __attribute__((cleanup(cetta_shared_transition_guard_leave)))
    CettaSharedTransitionGuard coordinator_transition={0};
    if(!descendant_grant)cetta_shared_transition_guard_enter(&coordinator_transition);
    assert(!owner->parallel_inflight);
    owner->parallel_inflight = cetta_malloc(sizeof(*owned));
    owner->parallel_inflight_count = 1u;
    owner->parallel_inflight[0] = *owned;
    *owned = (CettaOwnedContinuation){0};
    owned = &owner->parallel_inflight[0];
    PrimeNativeWork *work = owned->payload;
    owner->active_spent=0u;
    if (!work->prepared_publication) {
        PrimeNativeParallelRun run={.library=g_library_context,.coordinator=true,
            .receipts=owner->policy.receipts,.descendants=descendant_grant!=0u,
            .descendant_workers=owner->policy.workers?owner->policy.workers:eval_current_num_threads(),
            .descendant_quantum=owner->policy.worker_quantum};
        uint64_t grant=descendant_grant?descendant_grant:1u;
        if (work->parallel->child->driver.capsule_stack) {
            uint64_t quantum=owner->policy.worker_quantum?owner->policy.worker_quantum:64u;
            grant=owner->active_allowance<quantum?owner->active_allowance:quantum;
        }
        prime_native_parallel_slice_begin(owner,work->parallel,grant,owned->occurrence_id);
        CettaParallelWorker worker={.index=0u};
        (void)prime_native_parallel_task(&worker,work->parallel,&run);
        owner->active_spent=work->parallel->spent;
        assert(owner->active_spent<=owner->active_allowance);
        CettaNativeCostLedger *slice=&work->parallel->slice_cost;
        if(work->parallel->metered && !cetta_native_cost_join(&slice,1u))owner->failed=true;
        work->prepared_publication=true;
        if(work->parallel->child->driver.capacity_blocked)
            owner->driver.capacity_blocked=true;
    }
    (void)prime_native_parallel_join_owned(owner, owned);
    if (!owned->payload) (void)prime_native_parallel_recover(owner);
}

static int prime_native_supply_compare(const void *left, const void *right) {
    return cetta_resource_identity_compare(((const CettaResourceSupply *)left)->identity,
        ((const CettaResourceSupply *)right)->identity);
}

#include "prime_native_refill.inc"

static uint64_t prime_native_parallel_execute(PrimeNativeControl *owner,
        PrimeNativeWork **works, const CettaOwnedContinuation *owned,
        size_t count, uint32_t workers, size_t deferred) {
    uint64_t refill_spent=0u;
    if (prime_native_refill_execute(owner,works,owned,count,workers,&refill_spent))
        return refill_spent;
    bool metered = cetta_native_cost_active();
    CettaNativeCostLedger **slices = metered ? cetta_malloc(count*sizeof(*slices)) : NULL;
    PrimeNativeParallelRun run = {.library = g_library_context, .receipts = owner->policy.receipts};
    CettaParallelExecutor executor;
    CettaParallelExecutorConfig config = {
        .thread_count = workers, .prefer_persistent_workers = true,
        .user = &run, .task_fn = prime_native_parallel_task,
        .worker_enter = prime_native_parallel_enter, .worker_leave = prime_native_parallel_leave,
        .worker_failure_message = "native worker episode",
    };
    bool initialized = cetta_parallel_executor_init(&executor,&config);
    bool queued = initialized;
    for (size_t i = 0u; i < count; i++) {
        PrimeNativeParallel *parallel = works[i]->parallel;
        if (slices) slices[i] = &parallel->slice_cost;
        if (queued) queued = cetta_parallel_executor_push(&executor,parallel);
        cetta_native_cost_charge(CETTA_COST_WORKER_DISPATCH,1u);
    }
    bool ran = queued && cetta_parallel_executor_run(&executor);
    if (initialized) cetta_parallel_executor_free(&executor);
    if (!ran) {
        /* No owned work is lost if a pool cannot run the episode. Complete
         * each still-unattempted slice through the same native driver. */
        for (size_t i = 0u; i < count; i++) if (!works[i]->parallel->attempted) {
            CettaParallelWorker worker = {.index = 0u};
            works[i]->parallel->phase = 1u;
            (void)prime_native_parallel_task(&worker,works[i]->parallel,&run);
        }
    }
    for (size_t i = 0u; i < count; i++)
        prime_native_execution_joined(works[i]->parallel);
    uint64_t spent = 0u;
    Atom **branches = run.receipts ? cetta_malloc(count*sizeof(*branches)) : NULL;
    CettaNativeCostPlacement *placement = metered ? cetta_malloc(count*sizeof(*placement)) : NULL;
    for (size_t i = 0u; i < count; i++) {
        PrimeNativeParallel *parallel = works[i]->parallel;
        spent += parallel->spent;
        if (placement) placement[i] = (CettaNativeCostPlacement){parallel->phase, parallel->worker};
        if (!branches) continue;
        Arena *arena = owner->driver.arena;
        branches[i] = atom_expr(arena,(Atom *[]){atom_symbol(arena,"NativeBranchSlice"),
            prime_native_occurrence_atom(arena,owned[i].occurrence_id),
            parallel->metered ? prime_native_occurrence_atom(arena,parallel->slice_cost.identity)
                              : atom_symbol(arena,"Untracked"),
            parallel->metered ? prime_native_occurrence_atom(arena,parallel->slice_cost.total.work)
                              : atom_symbol(arena,"Untracked"),
            parallel->metered ? prime_native_occurrence_atom(arena,parallel->slice_cost.total.span)
                              : atom_symbol(arena,"Untracked"),
            prime_native_occurrence_atom(arena,parallel->worker),
            prime_native_occurrence_atom(arena,parallel->started_ns),
            prime_native_occurrence_atom(arena,parallel->finished_ns),
            prime_native_occurrence_atom(arena,parallel->phase)},9u);
    }
    if (metered && !cetta_native_cost_join_scheduled(slices,placement,count)) owner->failed = true;
    free(placement);
    if (branches) {
        Arena *arena = owner->driver.arena;
        Atom *receipt = atom_expr(arena,(Atom *[]){atom_symbol(arena,"NativeParallelWave"),
            prime_native_occurrence_atom(arena,atomic_load_explicit(&run.overlap,memory_order_relaxed)),
            atom_expr(arena,branches,(CettaExprLen)count),
            prime_native_occurrence_atom(arena,deferred)},4u);
        owner->parallel_waves = cetta_realloc(owner->parallel_waves,
            (owner->parallel_wave_count+1u)*sizeof(*owner->parallel_waves));
        owner->parallel_waves[owner->parallel_wave_count++] = receipt;
    }
    free(branches);
    for (size_t i=0u;i<count;i++) cetta_native_cost_charge(CETTA_COST_WORKER_JOIN,1u);
    free(slices);
    return spent;
}

/* The selected wave and its grant belong to the continuation, not the
 * observation allowance. A smaller allowance may pause a private episode,
 * but no partial return wakes the parent before that same grant ends. */
static uint64_t prime_native_parallel_resume_wave(PrimeNativeControl *owner,
                                                 uint64_t budget) {
    size_t count=owner->parallel_inflight_count;
    CettaOwnedContinuation *owned=owner->parallel_inflight;
    assert(owner->parallel_wave_pending && count);
    size_t active=0u;
    for (size_t i=0u;i<count;i++) {
        PrimeNativeWork *work=owned[i].payload;
        if (!work->parallel->wave_settled) active++;
    }
    uint64_t spent=0u;
    if (active && budget) {
        PrimeNativeWork **works=cetta_malloc(active*sizeof(*works));
        CettaOwnedContinuation *selected=cetta_malloc(active*sizeof(*selected));
        uint64_t *grants=calloc(count,sizeof(*grants));
        if (!grants) {
            free(works);free(selected);owner->driver.capacity_blocked=true;return 0u;
        }
        uint64_t share=budget/active, extra=budget%active;
        size_t last=owner->parallel_wave_cursor;
        for (size_t j=0u;j<count;j++) {
            size_t i=(owner->parallel_wave_cursor+j)%count;
            PrimeNativeParallel *parallel=((PrimeNativeWork *)owned[i].payload)->parallel;
            if (parallel->wave_settled) continue;
            uint64_t grant=share;
            if (extra) {grant++;extra--;last=(i+1u)%count;}
            if (grant>parallel->wave_remaining) grant=parallel->wave_remaining;
            grants[i]=grant;
        }
        owner->parallel_wave_cursor=last;
        size_t n=0u;
        for (size_t i=0u;i<count;i++) if (grants[i]) {
            PrimeNativeWork *work=owned[i].payload;
            prime_native_parallel_slice_begin(owner,work->parallel,grants[i],owned[i].occurrence_id);
            selected[n]=owned[i];works[n++]=work;
        }
        free(grants);
        assert(n);
        prime_native_control_accepted_progress(owner);
        spent=prime_native_parallel_execute(owner,works,selected,n,
            owner->parallel_wave_workers,owner->parallel_wave_deferred);
        for (size_t i=0u;i<n;i++) {
            PrimeNativeParallel *parallel=works[i]->parallel;
            assert(parallel->spent<=parallel->wave_remaining);
            parallel->wave_remaining-=parallel->spent;
            PrimeNativeControl *child=parallel->child;
            parallel->wave_settled=!parallel->wave_remaining || parallel->completed ||
                parallel->resource_provider || (child &&
                (child->failed || child->invalidated || child->effect_pending || child->need_pending ||
                 child->driver.capsule_frontier ||
                 prime_native_control_waiting_verdict(child)));
            if (child && parallel->execution.profile == PRIME_NATIVE_EXECUTION_NONE &&
                (child->driver.capacity_blocked || child->registry_read.incomplete))
                owner->driver.capacity_blocked=true;
            if (child && child->completion==CETTA_EVAL_INCOMPLETE_CANCELLED)
                owner->completion=child->completion;
        }
        free(works);free(selected);
    }
    /* A failed account join retains this complete wave, including paid
     * outputs and grants. Retry the account before queue publication. */
    if (owner->refill_pending) return spent;
    bool settled=true;
    for (size_t i=0u;i<count;i++)
        settled=settled && ((PrimeNativeWork *)owned[i].payload)->parallel->wave_settled;
    if (settled) {
        owner->parallel_wave_pending=false;
        /* No provider is applied here. Its full paid result remains on
         * the original occurrence until the parent agenda selects it. */
        for (size_t i=0u;i<count;i++) {
            PrimeNativeWork *work=owned[i].payload;
            if(work->parallel->attempted)work->prepared_publication=true;
        }
        prime_native_control_scan(owner);
        (void)prime_native_parallel_recover(owner);
    }
    return spent;
}

static bool prime_native_control_parallel_wave(PrimeNativeControl *owner,
                                               uint64_t budget, uint64_t *spent_out) {
    *spent_out=0u;
    if (owner->parallel_wave_pending) {
        *spent_out=prime_native_parallel_resume_wave(owner,budget);
        return true;
    }
    size_t ready = cetta_continuation_hub_length(&owner->hub);
    uint32_t workers = owner->policy.workers ? owner->policy.workers : eval_current_num_threads();
    if (workers < 2u || ready < 2u || budget == 0u ||
        cetta_parallel_worker_active() || cetta_shared_transition_guard_held_by_current_thread()) return 0u;
    if (workers > ready) workers = (uint32_t)ready;
    CettaSelectionLane lane;
    size_t first;
    CettaSelectionAutomaton selection = owner->hub.schedule;
    if (selection.state >= selection.state_count) return 0u;
    lane = (CettaSelectionLane)selection.lane[selection.state];
    if (!cetta_selection_automaton_select(&selection, ready, &first)) return 0u;
    if (lane != CETTA_SELECTION_LANE_OLDEST && owner->policy.priority)
        first = prime_native_control_priority_index(owner);
    PrimeNativeWork *first_work = cetta_continuation_hub_at(&owner->hub, first)->payload;
    if (!prime_native_parallel_prepare(owner, first_work)) return 0u;
    bool acquired_wave = first_work->parallel->execution.profile != PRIME_NATIVE_EXECUTION_NONE;
    /* Independent private continuations may wait in the existing executor
     * queue without holding a physical worker. This bounded ordinary
     * profile still joins the whole cohort before replenishing it. It does
     * not transfer a descendant frontier or share mutable Need producers. */
    bool ready_queue = owner->driver.ordinary_ordered &&
        owner->driver.ordinary_ready_queue && !acquired_wave &&
        first_work->task_initialized && !first_work->resource_step && !first_work->dialect;
    size_t preparation_limit = workers;
    if (ready_queue) {
        size_t extra = ready - preparation_limit;
        if (extra > workers) extra = workers;
        preparation_limit += extra;
    }
    if (ready > (SIZE_MAX / sizeof(CettaResourceClaim)) / 3u ||
        ready > SIZE_MAX / sizeof(CettaResourceFootprint) ||
        ready > SIZE_MAX / sizeof(size_t) || ready > SIZE_MAX / sizeof(CettaResourceSupply) - 2u)
        return 0u;
    size_t *order = cetta_malloc(ready * sizeof(*order));
    for (size_t i = 0u; i < ready; i++) order[i] = i;
    order[0] = first;
    for (size_t i = 1u; i <= first; i++) order[i] = i - 1u;
    {
        /* The physical wave is a snapshot of the declared agenda. Only
         * logical selection below commits an operation, so a new better key
         * may still precede an already prepared later occurrence. */
        CettaSelectionAutomaton projected = owner->hub.schedule;
        for (size_t i = 0u; i < ready; i++) order[i] = i;
        for (size_t at = 0u; at < ready; at++) {
            size_t selected;
            CettaSelectionLane next = (CettaSelectionLane)projected.lane[projected.state];
            bool valid = cetta_selection_automaton_select(&projected, ready-at, &selected);
            assert(valid);
            selected += at;
            if (next != CETTA_SELECTION_LANE_OLDEST && owner->policy.priority) {
                selected = at;
                for (size_t i = at+1u; i < ready; i++)
                    if (prime_native_work_key_less(owner,
                            cetta_continuation_hub_at(&owner->hub, order[i])->payload,
                            cetta_continuation_hub_at(&owner->hub, order[selected])->payload))
                        selected = i;
            }
            size_t chosen = order[selected];
            memmove(order+at+1u, order+at, (selected-at)*sizeof(*order));
            order[at] = chosen;
        }
    }
    CettaResourceSupply *supply = cetta_malloc((ready + 2u) * sizeof(*supply));
    CettaResourceClaim *claims = cetta_malloc(ready * 3u * sizeof(*claims));
    CettaResourceFootprint *footprints = calloc(ready, sizeof(*footprints));
    CettaResourceIdentity source_id = {1u, owner->source_read.instance_id, owner->source_read.revision};
    CettaResourceIdentity slots_id = {2u, (uint64_t)(uintptr_t)owner, 0u};
    supply[ready] = (CettaResourceSupply){source_id,1u};
    supply[ready + 1u] = (CettaResourceSupply){slots_id,workers};
    size_t prepared_count = 0u;
    for (size_t i = 0u; i < ready; i++) {
        const CettaOwnedContinuation *owned = cetta_continuation_hub_at(&owner->hub, order[i]);
        CettaResourceIdentity task_id = {0u,(uint64_t)(uintptr_t)owner,owned->occurrence_id};
        supply[i] = (CettaResourceSupply){task_id,1u};
        claims[3u*i] = (CettaResourceClaim){task_id,1u,0u};
        claims[3u*i+1u] = (CettaResourceClaim){source_id,0u,1u};
        claims[3u*i+2u] = (CettaResourceClaim){slots_id,1u,0u};
        /* Preparation remains bounded independently of source size. Queue
         * entries own private execution, not an occupied physical worker;
         * the executor enforces the active worker bound at dispatch. */
        PrimeNativeWork *candidate = owned->payload;
        /* Protocol stays with the occurrence: child episode, dialect
         * operation or private matcher packet. Their states are never cast
         * to one common execution mode. */
        bool acquired_candidate = !candidate->task_initialized && candidate->frame &&
            prime_native_frame_index_find(owner, candidate->frame) &&
            candidate->frame->kind == PRIME_EVAL_STACK_FRAME_CONTROL_NESTED &&
            candidate->frame->control_nested_operation;
        bool queue_candidate = !ready_queue || (candidate->task_initialized &&
            !candidate->resource_step && !candidate->dialect);
        bool prepared = prepared_count < preparation_limit && queue_candidate &&
            acquired_candidate == acquired_wave && prime_native_parallel_prepare(owner, candidate);
        if (prepared) prepared_count++;
        /* The ordinary queue checks task consumption and source reads.
         * Worker credits constrain simultaneous execution in the executor,
         * rather than the number of authenticated waiting capsules. */
        footprints[i] = (CettaResourceFootprint){claims + 3u*i,ready_queue ? 2u : 3u,prepared};
    }
    qsort(supply,ready+2u,sizeof(*supply),prime_native_supply_compare);
    CettaResourceWave wave = {0};
    bool selected = cetta_resource_wave_select(supply,ready+2u,footprints,ready,&wave);
    free(supply); free(claims); free(footprints);
    if (!selected || wave.admitted < 2u) {
        cetta_resource_wave_free(&wave); free(order); return 0u;
    }
    size_t count = wave.admitted;
    CettaOwnedContinuation *owned = calloc(count,sizeof(*owned));
    if (!owned) { cetta_resource_wave_free(&wave); free(order); return 0u; }
    assert(!owner->parallel_inflight);
    owner->parallel_inflight = owned;
    owner->parallel_inflight_count = count;
    PrimeNativeWork **works = cetta_malloc(count*sizeof(*works));
    size_t n=0u;
    for(size_t i=0u;i<ready;i++)if(wave.decisions[i]==CETTA_WAVE_ADMITTED) {
        owned[n]=*cetta_continuation_hub_at(&owner->hub,order[i]);
        assert(!owned[n].resume_lease);
        works[n]=owned[n].payload;n++;
    }
    /* A wave only prepares. It neither removes an occurrence nor advances
     * the parent selection phase; publication below uses that same agenda. */
    uint64_t quantum=owner->policy.worker_quantum?owner->policy.worker_quantum:1u;
    /* Total per-owner authority is reserved before dispatch. A refill uses
     * four ordinary quanta at most; observation budgets still cap each grant. */
    uint64_t reservation=quantum;
    if (ready_queue && owner->driver.ordinary_refill)
        reservation=quantum>UINT64_MAX/4u?UINT64_MAX:4u*quantum;
    owner->parallel_wave_pending=true;
    owner->parallel_wave_workers=workers;
    owner->parallel_wave_deferred=wave.deferred;
    owner->parallel_wave_cursor=0u;
    for (size_t i=0u;i<count;i++) {
        works[i]->parallel->wave_remaining=
            (works[i]->resource_step || works[i]->dialect)?1u:reservation;
        works[i]->parallel->wave_settled=false;
        works[i]->parallel->attempted=false;
        if(works[i]->dialect)works[i]->parallel->dialect_operation=works[i];
    }
    free(works);free(order);
    cetta_resource_wave_free(&wave);
    *spent_out=prime_native_parallel_resume_wave(owner,budget);
    return true;
}
