/* An ordinary synchronous call may lend its complete suspended frontier to
 * the common controller. Its caller retains the arenas, services and source
 * through the synchronous join. This is not a concurrent reclamation pin. */
static bool prime_ordinary_target(const PrimeEvalStackDriver *driver,
    PrimeEvalStackFrame *const *frames,size_t count,const OutcomeSet *target) {
    if(target==driver->root_target)return true;
    for(size_t i=0u;i<count;i++)
        if(frames[i]->child_initialized && target==&frames[i]->child)return true;
    return false;
}

/* Move the actual occurrence storage, including an inline first row. */
static void prime_ordinary_move_outcomes(OutcomeSet *destination,OutcomeSet *source) {
    assert(destination->len==0u);
    if(source->items!=source->inline_items) {
        *destination=*source;
    } else {
        assert(source->len<=1u && source->cap==1u);
        outcome_set_init_with_owner(destination,source->payload_owner);
        destination->len=source->len;
        if(source->len)outcome_move(&destination->inline_items[0],&source->inline_items[0]);
    }
    outcome_set_init(source);
}

static bool prime_ordinary_observe(Arena *arena,Atom *value,const Outcome *occurrence,void *raw) {
    const Bindings *env = &occurrence->env;
    (void)arena;(void)value;(void)env;(void)raw;
    return true;
}

typedef struct {
    Space *space;Arena *arena;const PrimeNeedSnapshot *snapshot;const Bindings *environment;
} PrimeOrdinarySupport;
static bool prime_ordinary_support_atom(const Atom *atom,void *raw) {
    PrimeOrdinarySupport *support=raw;
    if(!atom)return true;
    Atom *resolved=support->environment
        ? bindings_apply_if_vars(support->environment,support->arena,(Atom *)atom):(Atom *)atom;
    return resolved && prime_native_expression_safe(support->space,support->arena,resolved,support->snapshot);
}
static bool prime_ordinary_support_cell(const StateCell *cell,void *raw) {
    (void)cell;(void)raw;return false;
}
static bool prime_ordinary_support_bindings(PrimeOrdinarySupport *support,const Bindings *environment) {
    if(!environment)return true;
    PrimeOrdinarySupport bound=*support;
    bound.environment=environment;bound.snapshot=bindings_need_view(environment);
    return bindings_payloads_satisfy(environment,prime_ordinary_support_atom,
        prime_ordinary_support_cell,&bound);
}
static bool prime_ordinary_support_outcomes(PrimeOrdinarySupport *support,const OutcomeSet *outcomes) {
    if(!outcomes)return true;
    for(CettaCount i=0u;i<outcomes->len;i++) {
        const Outcome *row=&outcomes->items[i];
        if(row->kind!=CETTA_OUTCOME_INLINE || row->answer_ref.bank || row->delayed ||
           (row->result_form!=CETTA_CALL_RESULT_EXPRESSION &&
            row->result_form!=CETTA_CALL_RESULT_COMPLETED_VALUE))return false;
        PrimeOrdinarySupport bound=*support;
        bound.environment=&row->env;bound.snapshot=bindings_need_view(&row->env);
        if(!prime_ordinary_support_bindings(&bound,&row->env) ||
            !prime_ordinary_support_atom(row->atom,&bound) ||
            !prime_ordinary_support_atom(row->materialized_atom,&bound) ||
            !prime_ordinary_support_atom(row->delayed,&bound) ||
            !variant_instance_checkpoint_atoms(&row->variant,prime_ordinary_support_atom,&bound))return false;
        for(uint32_t j=0u;j<row->answer_ref.goal_instantiation.len;j++)
            if(!prime_ordinary_support_atom(row->answer_ref.goal_instantiation.items[j].mapped_var,&bound))return false;
    }
    return true;
}
/* The same payload visitors used by checkpoint admission cover contextual
 * slots, constraints and every retained Need/state/weight carrier. No copied
 * atom or lifetime reference serves as a substitute for this inspection. */
static bool prime_ordinary_support_frontier(PrimeEvalStackDriver *driver,Space *space,
    PrimeEvalStackFrame *const *frames,size_t count,const Bindings *environment) {
    PrimeOrdinarySupport support={space,driver->arena,&g_prime_need_active,environment};
    if(!prime_ordinary_support_bindings(&support,environment) ||
        !prime_ordinary_support_outcomes(&support,driver->root_target))return false;
    if(driver->task_ready) {
        const PrimeEvalStackTask *task=&driver->task;
        PrimeOrdinarySupport task_support={task->space,task->arena,
            task->dynamic_env_initialized?bindings_need_view(&task->dynamic_env):&g_prime_need_active,
            task->dynamic_env_initialized?&task->dynamic_env:NULL};
        if(!prime_ordinary_support_atom(task->atom,&task_support) ||
            !prime_ordinary_support_atom(task->etype,&task_support) ||
            !prime_ordinary_support_atom(task->control_key,&task_support) ||
            (task->dynamic_env_initialized && !prime_ordinary_support_bindings(&task_support,&task->dynamic_env)) ||
            (task->seed_env_initialized && !prime_ordinary_support_bindings(&task_support,&task->seed_env)))return false;
    }
    for(size_t i=0u;i<count;i++) {
        const PrimeEvalStackFrame *frame=frames[i];
        PrimeOrdinarySupport bound={frame->space,frame->arena,
            frame->env_initialized?bindings_need_view(&frame->env):&g_prime_need_active,
            frame->env_initialized?&frame->env:NULL};
        const Atom *roots[]={frame->atom,frame->etype,frame->strict_source,frame->then_branch,
            frame->else_branch,frame->weight_subject,frame->weight_algebra,frame->weight_trace,
            frame->ref,frame->cell.origin,frame->cell.cached,frame->control_key,frame->control_bound,
            frame->control_return_pending_advice.key,frame->control_return_pending_advice.bound};
        for(size_t j=0u;j<sizeof(roots)/sizeof(*roots);j++)
            if(!prime_ordinary_support_atom(roots[j],&bound))return false;
        for(size_t j=0u;j<frame->normalization_child_count;j++)
            if(!prime_ordinary_support_atom(frame->normalization_children[j],&bound))return false;
        if((frame->env_initialized && !prime_ordinary_support_bindings(&bound,&frame->env)) ||
            (frame->child_initialized && !prime_ordinary_support_outcomes(&bound,&frame->child)) ||
            (frame->force_pending_initialized &&
             !prime_ordinary_support_outcomes(&bound,&frame->force_pending)))return false;
        PrimeOrdinarySupport before=bound,evaluating=bound;
        before.snapshot=&frame->before;evaluating.snapshot=&frame->evaluating;
        if(!prime_need_snapshot_atoms_satisfy(&frame->before,prime_ordinary_support_atom,&before) ||
            !prime_need_snapshot_atoms_satisfy(&frame->evaluating,prime_ordinary_support_atom,&evaluating))return false;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
        if(!prime_need_branch_state_payloads_satisfy(&frame->before_receipt,
                prime_ordinary_support_atom,prime_ordinary_support_cell,&before) ||
            !prime_need_branch_state_payloads_satisfy(&frame->before_receipt,
                prime_ordinary_support_atom,prime_ordinary_support_cell,&evaluating))return false;
#endif
        const PrimeNeedEquationContinuation *equation=frame->equation_search;
        if(equation) {
            if(equation->space!=frame->space || equation->arena!=frame->arena)return false;
            for(size_t j=0u;j<equation->root_atom_count;j++) {
                Atom *root=equation->root_atoms[j];
                if(j<equation->plan_count) {
                    if(root!=equation->plans[j].equation || !root || root->kind!=ATOM_EXPR ||
                        root->expr.len!=3u || !atom_is_symbol_id(root->expr.elems[0],g_builtin_syms.equals) ||
                        !prime_ordinary_support_atom(root->expr.elems[1],&bound) ||
                        !prime_ordinary_support_atom(root->expr.elems[2],&bound))return false;
                } else if(!prime_ordinary_support_atom(root,&bound))return false;
            }
            const OutcomeSet *rows[]={&equation->work,&equation->current,&equation->raw_results,
                &equation->residuals,&equation->publication,&equation->forced_worlds};
            for(size_t j=0u;j<sizeof(rows)/sizeof(*rows);j++)
                if(!prime_ordinary_support_outcomes(&bound,rows[j]))return false;
        }
    }
    return true;
}

static _Thread_local const char *g_prime_share_refusal;

static void prime_share_refuse(const char *reason) {
    if (!g_prime_share_refusal)
        g_prime_share_refusal = reason;
}

static const char *prime_share_grounded_refusal(GroundedKind kind) {
    switch (kind) {
    case GV_INT: case GV_FLOAT: case GV_BOOL: case GV_STRING:
    case GV_BIGINT: case GV_RATIONAL:
        return "immutable-scalar";
    case GV_SPACE: return "immutable-space";
    case GV_STATE: return "immutable-state";
    case GV_CAPTURE: return "immutable-capture";
    case GV_FOREIGN: return "immutable-foreign";
    case GV_PRIME_NEED_CAPABILITY: return "immutable-need";
    case GV_PRIME_CONTEXT: return "immutable-context";
    case GV_INTERNAL_TAG: return "immutable-tag";
    case GV_BINDINGS: return "immutable-bindings";
    case GV_TERM_GRAPH: return "immutable-term-graph";
    case GV_TENSOR: return "immutable-tensor";
    case GV_STRUCTURAL: return "immutable-structural";
    case GV_SEAL: return "immutable-seal";
    }
    return "immutable-grounded";
}

/* The ordinary caller holds the source/universe, registry, symbols and
 * library until its synchronous frontier has joined and published. This
 * inspection admits immutable syntax payloads; it does not create a pin or
 * authorize an external writer. Index/cache accesses retain their existing
 * concurrent-scope guards. */
typedef struct {
    size_t position;
    CettaExprIndex child;
    bool checked;
} PrimeOrdinaryGraphCursor;

struct PrimeOrdinaryGraphInspection {
    PrimeEvalStackSupportBuffer scan;
    CettaStorageCensus seen;
    PrimeOrdinaryGraphCursor cursor;
    Arena *arena;
    uint32_t arena_identity, older_identity;
    uint64_t arena_epoch;
    HashConsTable *hashcons;
    bool scalar_cache;
};

/* Both admissions use the same node predicate and edge order. A finite
 * slice retains its census, node position and next edge. Native indexing,
 * allocation and pointer validity remain primitive service obligations. */
static inline __attribute__((always_inline)) CettaContinuationStatus
prime_ordinary_immutable_graph_run(PrimeEvalStackSupportBuffer *scan,
    CettaStorageCensus *seen,PrimeOrdinaryGraphCursor *cursor,
    uint64_t allowance,uint64_t *used) {
    uint64_t spent=0u;
    if (used) *used=0u;
#define INSPECTION_DEBIT() do { \
    if (used) { \
        if (spent==allowance) { *used=spent;return CETTA_CONTINUATION_DEFERRED; } \
        ++spent; *used=spent; \
    } \
} while (0)
    if (used && !allowance) return CETTA_CONTINUATION_DEFERRED;
    while (cursor->position<scan->len) {
        Atom *atom=scan->items[cursor->position];
        if (!cursor->checked) {
            INSPECTION_DEBIT();
            if (!atom) {
                prime_share_refuse("immutable-null");
                return CETTA_CONTINUATION_UNSUPPORTED;
            }
            if (!cetta_storage_object(seen,atom,0u,CETTA_STORAGE_VISITED)) {
                if (seen->incomplete || seen->overflow) {
                    prime_share_refuse("immutable-census");
                    return CETTA_CONTINUATION_UNSUPPORTED;
                }
                cursor->position++;
                continue;
            }
            if (atom->structural_facts&ATOM_STRUCTURAL_HAS_NATIVE_HANDLE_ID) {
                prime_share_refuse("immutable-handle");
                return CETTA_CONTINUATION_UNSUPPORTED;
            }
            if (atom->kind==ATOM_GROUNDED) {
                GroundedKind kind=atom->ground.gkind;
                if (kind!=GV_INT && kind!=GV_FLOAT && kind!=GV_BOOL && kind!=GV_STRING &&
                    kind!=GV_BIGINT && kind!=GV_RATIONAL) {
                    prime_share_refuse(prime_share_grounded_refusal(kind));
                    return CETTA_CONTINUATION_UNSUPPORTED;
                }
            } else if (atom->kind==ATOM_EXPR) {
                if (atom->expr.len && !atom->expr.elems) {
                    prime_share_refuse("immutable-expression");
                    return CETTA_CONTINUATION_UNSUPPORTED;
                }
            } else if (atom->kind!=ATOM_VAR && atom->kind!=ATOM_SYMBOL) {
                prime_share_refuse("immutable-kind");
                return CETTA_CONTINUATION_UNSUPPORTED;
            }
            cursor->checked=true;
        }
        CettaExprLen children=atom->kind==ATOM_EXPR?atom->expr.len:
            atom->kind==ATOM_VAR && atom->name_key?1u:0u;
        while (cursor->child<children) {
            INSPECTION_DEBIT();
            Atom *child=atom->kind==ATOM_VAR?atom->name_key:atom->expr.elems[cursor->child];
            if (!prime_eval_stack_support_buffer_push(scan,child)) {
                prime_share_refuse("immutable-push");
                return CETTA_CONTINUATION_UNSUPPORTED;
            }
            cursor->child++;
        }
        cursor->position++;cursor->child=0u;cursor->checked=false;
    }
    if (seen->incomplete || seen->overflow) {
        prime_share_refuse("immutable-census");
        return CETTA_CONTINUATION_UNSUPPORTED;
    }
    return CETTA_CONTINUATION_READY;
#undef INSPECTION_DEBIT
}

static bool prime_ordinary_immutable_graph(PrimeEvalStackSupportBuffer *scan,
    CettaStorageCensus *seen) {
    PrimeOrdinaryGraphCursor cursor={0};
    return prime_ordinary_immutable_graph_run(scan,seen,&cursor,0u,NULL)
        ==CETTA_CONTINUATION_READY;
}

static void prime_ordinary_graph_inspection_free(PrimeOrdinaryGraphInspection *inspection) {
    if (!inspection) return;
    cetta_storage_census_free(&inspection->seen);
    prime_eval_stack_support_buffer_free(&inspection->scan);
    free(inspection);
}

static bool prime_ordinary_shared_source_admitted(Space *space) {
    SpaceReadView view={0};
    if(!space_read_view_capture(space,&view)) {
        prime_share_refuse("view-capture");
        return false;
    }
    if (view.base_count)
        prime_share_refuse("overlay-base");
    if (!space_read_view_has_native_occurrences(&view))
        prime_share_refuse("no-native-occurrences");
    bool safe=!view.base_count && space_read_view_has_native_occurrences(&view);
    for(size_t i=0u;safe && i<g_context_home_depth;i++) {
        safe=g_context_home_stack[i]==space;
        if (!safe) prime_share_refuse("home-stack");
    }
    for(size_t i=0u;safe && i<g_context_here_depth;i++) {
        safe=g_context_here_stack[i]==space;
        if (!safe) prime_share_refuse("here-stack");
    }
    PrimeEvalStackSupportBuffer scan;prime_eval_stack_support_buffer_init(&scan);
    CettaStorageCensus seen={0};
    for(CettaCount i=0u;safe && i<space_length64(space);i++) {
        safe=prime_eval_stack_support_buffer_push(&scan,space_get_at64(space,i));
        if (!safe) prime_share_refuse("space-row");
    }
    if(g_registry)for(uint32_t i=0u;safe && i<g_registry->len;i++) {
        Atom *value=g_registry->entries[i].value;
        if(value && value->kind==ATOM_GROUNDED && value->ground.gkind==GV_SPACE) {
            safe=value->ground.ptr==space;
            if (!safe) prime_share_refuse("registry-space");
        } else if(value) {
            safe=prime_eval_stack_support_buffer_push(&scan,value);
            if (!safe) prime_share_refuse("registry-value");
        }
        if(safe && g_registry->entries[i].key==SYMBOL_ID_NONE) {
            const Atom *key=registry_entry_name_key(g_registry,i);
            safe=key && prime_eval_stack_support_buffer_push(&scan,(Atom *)key);
            if (!safe) prime_share_refuse("registry-key");
        }
    }
    /* The static relation classifier consumes compiled RHS plans. Use the
     * common live-Space ingress; it preserves existing declaration plans,
     * and its preparation/cache work remains part of this call. Synchronize
     * still runs only after the immutable-graph conjunct, as before. */
    if (safe && !prime_ordinary_immutable_graph(&scan,&seen)) {
        prime_share_refuse("immutable-graph");
        safe = false;
    }
    if (safe && !petta_program_synchronize_space(g_library_context->petta_program,space)) {
        prime_share_refuse("synchronize");
        safe = false;
    }
    if (safe) {
        PettaProgramRevisionView *program_view = petta_program_revision_view_capture(
            g_library_context->petta_program, space);
        if (!program_view || !petta_program_revision_view_source_plans_current(
                g_library_context->petta_program, space, program_view)) {
            prime_share_refuse("program-view");
            safe = false;
        }
        petta_program_revision_view_free(program_view);
    }
    if (safe && !space_read_view_is_current(&view)) {
        prime_share_refuse("view-stale");
        safe = false;
    }
    cetta_storage_census_free(&seen);prime_eval_stack_support_buffer_free(&scan);
    space_read_view_release(&view);return safe;
}
/* This is an additional sharing profile, not the evaluator's general call
 * classifier. Dynamic/opaque support keeps the established private-copy
 * route. The real program classification and complete native closure walk
 * both apply; a guarded plan alone never licenses borrowing. */
static bool prime_ordinary_shared_task_admitted(Space *space,Arena *arena,Atom *expression) {
    PrimeEvalStackSupportBuffer scan;prime_eval_stack_support_buffer_init(&scan);
    CettaStorageCensus seen={0},calls={0};
    bool safe=prime_eval_stack_support_buffer_push(&scan,expression) &&
        prime_ordinary_immutable_graph(&scan,&seen);
    if (!safe) prime_share_refuse("task-expression");
    for(size_t i=0u;safe && i<scan.len;i++) {
        Atom *atom=scan.items[i];
        if(!cetta_storage_object(&calls,atom,0u,CETTA_STORAGE_VISITED)) {
            if(calls.incomplete || calls.overflow)safe=false;
            continue;
        }
        if(atom->kind!=ATOM_EXPR || !atom->expr.len)continue;
        Atom *head=atom->expr.elems[0];
        /* A registry binding supplies a different callable/value region.
         * This read-closure profile does not transport that interpretation. */
        if((head->kind!=ATOM_SYMBOL && head->kind!=ATOM_GROUNDED) ||
            registry_lookup_atom(head)) {
            prime_share_refuse("task-head");
            safe=false;break;
        }
        if(atom_has_constructor_head(space,arena,atom))continue;
        /* Reading immutable syntax is independent of worker placement.
         * The complete closure and real call classifier still apply. */
        if(!prime_native_actual_call_allowed(space,arena,atom)) {
            prime_share_refuse("task-call");
            safe=false;break;
        }
        SymbolId id=atom_head_symbol_id(atom);
        if(id!=SYMBOL_ID_NONE && !symbol_id_is_builtin_for(id, eval_current_language_id()) &&
            !registry_lookup_atom(head) && eval_equations_may_match_known_head(space,id)) {
            /* PeTTa conservatively gives every non-symbol head a dynamic
             * plan, including an inert scalar-headed container. Keep its
             * classification unchanged. The complete native walk above
             * refuses variable/compound heads, authenticates constructors,
             * and still inspects every descendant and authored RHS. */
            PettaRelationSafety safety=petta_program_relation_safety(
                g_library_context->petta_program,space,id,expr_nargs(atom));
            safe=safety==PETTA_RELATION_SAFETY_STATIC ||
                safety==PETTA_RELATION_SAFETY_GUARDED_DYNAMIC;
            if (!safe) prime_share_refuse("task-relation");
            SpaceEquationCursor cursor;
            safe=safe && space_equation_cursor_init(space,id,&cursor);
            if (!safe) prime_share_refuse("task-equation");
            while(safe) {
                SpaceEquationOccurrenceId occurrence_id;
                SpaceEquationCursorStep step=space_equation_cursor_next(&cursor,&occurrence_id);
                if(step==SPACE_EQUATION_CURSOR_END)break;
                SpaceEquationOccurrence occurrence;
                safe=step==SPACE_EQUATION_CURSOR_ITEM &&
                    space_equation_occurrence_resolve(occurrence_id,&occurrence) &&
                    atom_head_symbol_id(occurrence.lhs)==id &&
                    prime_eval_stack_support_buffer_push(&scan,occurrence.rhs);
            }
            safe=safe && prime_ordinary_immutable_graph(&scan,&seen);
            if (!safe) prime_share_refuse("task-rhs");
        } else {
            safe=petta_program_classify_resolved_call(g_library_context->petta_program,
                space,atom)==PETTA_RESOLVED_CALL_MACHINE_LOCAL;
            if (!safe) prime_share_refuse("task-machine");
        }
    }
    cetta_storage_census_free(&calls);cetta_storage_census_free(&seen);
    prime_eval_stack_support_buffer_free(&scan);return safe;
}
static bool prime_ordinary_received_atom(const Atom *atom,void *arena) {
    return !atom || atom_graph_is_closed_for_arena(arena,atom);
}
/* Borrowed source syntax cannot become a delayed owner in the result. The
 * common promoter copies the full occurrence first; audit its actual atom,
 * logical/Need/receipt and variant roots in the original receiving arena. */
static bool prime_ordinary_received_outcome(Arena *arena,const Outcome *row) {
    if(!arena || !row || row->kind!=CETTA_OUTCOME_INLINE || row->answer_ref.bank || row->delayed ||
        (row->result_form!=CETTA_CALL_RESULT_EXPRESSION &&
         row->result_form!=CETTA_CALL_RESULT_COMPLETED_VALUE) ||
        !prime_ordinary_received_atom(row->atom,arena) ||
        !prime_ordinary_received_atom(row->materialized_atom,arena) ||
        !prime_ordinary_received_atom(row->delayed,arena) ||
        !bindings_payloads_satisfy(&row->env,prime_ordinary_received_atom,
            prime_ordinary_support_cell,arena) ||
        !variant_instance_checkpoint_atoms(&row->variant,prime_ordinary_received_atom,arena))return false;
    for(uint32_t i=0u;i<row->answer_ref.goal_instantiation.len;i++)
        if(!prime_ordinary_received_atom(row->answer_ref.goal_instantiation.items[i].mapped_var,arena))return false;
    return true;
}

/* Borrowed return and service contexts may name the original driver. Moving
 * its frontier needs a transport for those contexts; ordinary transfer keeps
 * their invocation in place until that transport is available. */
static bool prime_ordinary_scope_has_no_permit(PrimeEvalStackDriver *scope,void *context) {
    (void)context;
    return !scope->call_permit;
}

static bool prime_ordinary_driver_transfer_admitted(
    const PrimeEvalStackDriver *source) {
    if (!source || g_prime_ordinary_return)
        return false;
    /* A resource parent and an invocation parent can be different. Refuse
     * transfer if either graph branch carries an unmapped permit, or if its
     * finite traversal cannot establish that the whole graph was checked. */
    return prime_eval_scope_walk((PrimeEvalStackDriver *)source,true,
        prime_ordinary_scope_has_no_permit,NULL)==PRIME_EVAL_SCOPE_WALK_COMPLETE;
}

static bool prime_eval_stack_promote_ordinary(PrimeEvalStackDriver *source,
    Space *space,Atom *expression,int fuel,const Bindings *environment,uint32_t workers) {
    /* Explicit owned roots install their controller before source entry.
     * A nested synchronous root retains its caller's return receiver and
     * invocation checks; moving it cannot detach that borrowed ancestry. */
    if(!prime_ordinary_driver_transfer_admitted(source) || source!=g_prime_eval_stack_driver || source->control || workers<2u ||
        source->streaming || source->running_task || source->active_task ||
        source->root_call_running || source->raw_call_depth || source->bind_depth ||
        source->poison_depth || source->capacity_blocked || source->failed ||
        source->stream_pending || source->prepared_pure_cache.execution_scratch_ready ||
        !source->root_target || !source->arena ||
        (!source->task_ready && !source->top) || cetta_parallel_worker_active() ||
        cetta_native_cost_observer_active() || cetta_shared_transition_guard_held_by_current_thread() ||
        eval_current_language_id()!=CETTA_LANGUAGE_PRIME ||
        active_search_table_mode()!=CETTA_TABLE_MODE_NONE)return false;
    size_t count=source->frame_depth;
    if(count>SIZE_MAX/sizeof(PrimeEvalStackFrame *) ||
        source->continuations.depth!=count ||
        source->continuations.top!=(CettaOwnedFrameLink *)source->top)return false;
    PrimeEvalStackFrame **frames=count?calloc(count,sizeof(*frames)):NULL;
    if(count && !frames)return false;
    size_t found=0u;
    for(PrimeEvalStackFrame *frame=source->top;frame;frame=(PrimeEvalStackFrame *)frame->continuation.next) {
        if(found==count || frame->arena!=source->arena || frame->control_sequence ||
            frame->control_pending || frame->control_queued || frame->control_retired ||
            frame->control_closed || frame->control_nested || frame->control_nested_operation ||
            frame->kind>=PRIME_EVAL_STACK_FRAME_CONTROL_SCORE)goto decline;
        frames[found++]=frame;
    }
    if(found!=count)return free(frames),false;
    for(size_t i=0u;i<count;i++)
        if(!prime_ordinary_target(source,frames,count,frames[i]->target))goto decline;
    if(source->task_ready && (!prime_ordinary_target(source,frames,count,source->task.target) ||
        source->task.arena!=source->arena))goto decline;
    /* This admission inspects the complete captured support. A retained
     * reference alone is never permission to execute an omitted service. */
    if(!prime_native_expression_safe(space,source->arena,expression,&g_prime_need_active))goto decline;
    if(!prime_ordinary_support_frontier(source,space,frames,count,environment))goto decline;
    bool shared_source=prime_ordinary_shared_source_admitted(space);
#ifdef CETTA_PRIME_ORDINARY_TEST_OBSERVER
    shared_source=shared_source && prime_ordinary_test_share_source();
#endif
    PrimeNativeControl *control=calloc(1u,sizeof(*control));
    if(!control)goto decline;
    int64_t prepared_quantum=eval_option_int_or_default("prepared-node-quantum",1);
    /* Full-bag work amortizes worker handoff across a bounded batch.
     * Authored control policies retain their explicitly requested quantum. */
    CettaSearchPolicySpec policy={.native_control=true,.all=true,.workers=workers,
        .work_budget=UINT64_MAX,.worker_quantum=4096u,
        .prepared_node_quantum=prepared_quantum>0?(uint64_t)prepared_quantum:1u};
#ifdef CETTA_PRIME_ORDINARY_TEST_OBSERVER
    policy.worker_quantum=prime_ordinary_test_quantum();
    assert(policy.worker_quantum>0u);
#endif
    if(!prime_native_control_initialize(control,space,source->arena,expression,fuel,false,
            (CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},
            &policy,environment,NULL,NULL,false) || control->registry_read.incomplete)goto release;
    CettaSelectionAutomaton schedule;
    if(!cetta_selection_automaton_lifo(&schedule))goto release;
    control->hub.selection_duty=CETTA_SELECTION_DUTY_NONE;
    if(!cetta_continuation_hub_switch_schedule(&control->hub,&schedule))goto release;
    size_t capacity=16u;
    if(count>SIZE_MAX/2u)goto release;
    while(capacity<count*2u) {if(capacity>SIZE_MAX/2u)goto release;capacity*=2u;}
    control->frame_index=calloc(capacity,sizeof(*control->frame_index));
    control->frame_index_capacity=capacity;
    control->changed_frames=count?calloc(count,sizeof(*control->changed_frames)):NULL;
    control->changed_capacity=count;
    CettaCount prefix=source->root_target->len;
    if(prefix>SIZE_MAX/sizeof(*control->delivered))goto release;
    control->delivered=prefix?calloc((size_t)prefix,sizeof(*control->delivered)):NULL;
    control->delivered_capacity=(size_t)prefix;
    if(!control->frame_index || (count && !control->changed_frames) || (prefix && !control->delivered))goto release;
    PrimeNativeWork *ready=calloc(1u,sizeof(*ready));
    if(!ready)goto release;
    ready->owner=control;
    CettaOwnedContinuation initial={.payload=ready,.provider=&prime_native_work_provider};
    if(!cetta_continuation_hub_append(&control->hub,&initial)) {
        free(ready);goto release;
    }
#ifdef CETTA_PRIME_ORDINARY_TEST_OBSERVER
    prime_ordinary_test_promoting(source,control,frames,count);
#endif
    /* All fallible adoption preparation precedes this ownership transfer.
     * Atoms, environments, cell snapshots and frame addresses stay intact. */
    OutcomeSet *target=source->root_target;
    prime_ordinary_move_outcomes(&control->output,target);
    control->driver=*source;
    control->driver.control=control;control->driver.ordinary_ordered=true;
    control->driver.ordinary_shared_source=shared_source;
    control->driver.ordinary_ready_queue=true;
    control->driver.ordinary_refill=true;
    control->driver.ordinary_descendants=true;
#ifdef CETTA_PRIME_ORDINARY_DESCENDANTS_TEST
    control->driver.ordinary_descendants=prime_ordinary_test_descendants();
#endif
#ifdef CETTA_PRIME_ORDINARY_REFILL_TEST
    control->driver.ordinary_refill=prime_ordinary_test_refill();
#endif
#ifdef CETTA_PRIME_ORDINARY_READY_QUEUE_TEST
    control->driver.ordinary_ready_queue=prime_ordinary_test_ready_queue();
#endif
    control->driver.continuations=(CettaOwnedFrameStack){0};
    control->driver.root_target=&control->output;
    control->driver.collection_admitted=false;
    control->driver.top=NULL;
    for(size_t i=count;i>0u;i--) {
        PrimeEvalStackFrame *frame=frames[i-1u];
        if(frame->target==target)frame->target=&control->output;
        prime_native_control_register_frame(control,frame);
        control->driver.top=frame;
    }
    if(control->driver.task_ready) {
        ready->task=control->driver.task;ready->task_initialized=true;
        if(ready->task.target==target)ready->task.target=&control->output;
        ready->parent=control->driver.top;
        if(ready->parent)ready->parent->control_pending++;
        memset(&control->driver.task,0,sizeof(control->driver.task));
        control->driver.task_ready=false;
    } else {ready->frame=control->driver.top;ready->frame->control_queued=true;}
    control->driver.top=NULL;
    control->published=prefix;control->delivered_count=prefix;
    for(size_t i=0u;i<(size_t)prefix;i++)control->delivered[i]=true;
    memset(source,0,sizeof(*source));
    prime_native_control_run(control,UINT64_MAX,prime_ordinary_observe,NULL);
#ifdef CETTA_PRIME_ORDINARY_TEST_OBSERVER
    prime_ordinary_test_finished(control);
#endif
    prime_ordinary_move_outcomes(target,&control->output);
    if(control->failed || control->invalidated || control->driver.capacity_blocked ||
        !prime_native_source_drained(control))
        eval_mark_incomplete(control->completion==CETTA_EVAL_COMPLETE?
            CETTA_EVAL_INCOMPLETE_CAPACITY:control->completion);
    prime_native_control_destroy(control);free(frames);return true;
release:
    prime_native_control_destroy(control);
decline:
    free(frames);return false;
}

/* A fresh capsule may use the ordinary exclusive continuation stack. Its
 * control remains the authority and output owner throughout both forms. */
static bool prime_ordinary_capsule_stack_start(PrimeNativeControl *control) {
    PrimeEvalStackDriver *driver=&control->driver;
    if (!driver->ordinary_ordered || driver->capsule_stack || driver->top ||
        driver->task_ready || control->frames || control->retired_frames ||
        control->steps || control->output.len || control->cost_enabled ||
        control->policy.receipts || cetta_continuation_hub_length(&control->hub)!=1u)
        return false;
    const CettaOwnedContinuation *entry=cetta_continuation_hub_at(&control->hub,0u);
    if (!entry || entry->provider!=&prime_native_work_provider || !entry->payload)
        return false;
    PrimeNativeWork *work=entry->payload;
    if (work->owner!=control || !work->task_initialized || work->parent || work->frame ||
        work->parallel || work->dialect || work->licence || work->resource_step ||
        work->task.target!=&control->output || work->task.arena!=driver->arena ||
        work->task.fuel>=0) return false;
    CettaOwnedContinuation owned={0};
    if (!cetta_continuation_hub_take(&control->hub,0u,&owned)) return false;
    assert(owned.payload==work);
    driver->task=work->task;driver->task_ready=true;
    memset(&work->task,0,sizeof(work->task));work->task_initialized=false;
    driver->capsule_stack=true;driver->ordinary_ordered=false;
    driver->streaming=false;driver->collection_admitted=false;
    cetta_owned_continuation_destroy(&owned);
    return true;
}

/* This only changes the scheduling representation. Source, Need, frames,
 * task environments and every receiving OutcomeSet retain their addresses.
 * All allocations and the fallible queue insertion precede the transfer. */
static bool prime_ordinary_capsule_stack_adopt(PrimeNativeControl *control) {
    PrimeEvalStackDriver *source=&control->driver;
    if (!source->capsule_stack || source->control!=control ||
        source->root_target!=&control->output || source->running_task ||
        source->active_task || source->raw_call_depth || source->bind_depth ||
        source->poison_depth || source->stream_pending || control->frames ||
        control->retired_frames || control->open_frames || control->frame_index ||
        control->changed_frames || control->parallel_inflight ||
        cetta_continuation_hub_length(&control->hub) || control->failed ||
        control->invalidated || !space_read_token_is_current(control->source_read) ||
        control->registry_read.stale || control->registry_read.incomplete ||
        (!source->task_ready && !source->top)) return false;
    size_t count=source->frame_depth;
    if (source->continuations.depth!=count || count>SIZE_MAX/sizeof(PrimeEvalStackFrame *) ||
        source->continuations.top!=(CettaOwnedFrameLink *)source->top) return false;
    PrimeEvalStackFrame **frames=count?calloc(count,sizeof(*frames)):NULL;
    if (count && !frames) return false;
    size_t found=0u;
    for (PrimeEvalStackFrame *frame=source->top;frame;
         frame=(PrimeEvalStackFrame *)frame->continuation.next) {
        if (found==count || frame->arena!=source->arena || frame->control_sequence ||
            frame->control_pending || frame->control_queued || frame->control_retired ||
            frame->control_closed || frame->control_nested || frame->control_nested_operation ||
            (frame->kind>=PRIME_EVAL_STACK_FRAME_CONTROL_SCORE &&
             frame->kind!=PRIME_EVAL_STACK_FRAME_CAPSULE_SEARCH) ||
            (frame->kind==PRIME_EVAL_STACK_FRAME_CAPSULE_SEARCH &&
             !prime_capsule_search_adoptable(control,frame))) goto refuse;
        frames[found++]=frame;
    }
    if (found!=count) goto refuse;
    for (size_t i=0u;i<count;i++)
        if (!prime_ordinary_target(source,frames,count,frames[i]->target)) goto refuse;
    if (source->task_ready && (source->task.arena!=source->arena ||
        !prime_ordinary_target(source,frames,count,source->task.target))) goto refuse;
    size_t capacity=16u;
    if (count>SIZE_MAX/2u) goto refuse;
    while (capacity<count*2u) {if(capacity>SIZE_MAX/2u)goto refuse;capacity*=2u;}
    PrimeEvalStackFrame **index=calloc(capacity,sizeof(*index));
    PrimeEvalStackFrame **changed=count?calloc(count,sizeof(*changed)):NULL;
    PrimeNativeWork *ready=calloc(1u,sizeof(*ready));
    if (!index || (count && !changed) || !ready) {
        free(index);free(changed);free(ready);goto refuse;
    }
    ready->owner=control;
    CettaOwnedContinuation initial={.payload=ready,.provider=&prime_native_work_provider};
    if (!cetta_continuation_hub_append(&control->hub,&initial)) {
        free(index);free(changed);free(ready);goto refuse;
    }
    control->frame_index=index;control->frame_index_capacity=capacity;
    control->changed_frames=changed;control->changed_capacity=count;
    source->capsule_stack=false;source->capsule_frontier=false;
    source->ordinary_ordered=true;source->streaming=true;
    source->continuations=(CettaOwnedFrameStack){0};source->top=NULL;
    for (size_t i=count;i>0u;i--) {
        PrimeEvalStackFrame *frame=frames[i-1u];
        prime_native_control_register_frame(control,frame);source->top=frame;
    }
    if (source->task_ready) {
        ready->task=source->task;ready->task_initialized=true;ready->parent=source->top;
        if (ready->parent) ready->parent->control_pending++;
        memset(&source->task,0,sizeof(source->task));source->task_ready=false;
    } else {ready->frame=source->top;ready->frame->control_queued=true;}
    source->top=NULL;
    free(frames);return true;
refuse:
    free(frames);return false;
}

/* The borrowed observer policies stay fixed for this run. Unobserved adapter
 * transitions do not probe cost or replay services at every quantum/return. */
static inline __attribute__((always_inline)) void prime_ordinary_capsule_stack_loop(
    PrimeNativeControl *control,uint64_t grant,bool meter_cost,bool replay_phases) {
    PrimeEvalStackDriver *driver=&control->driver;
    while (grant && (driver->task_ready || driver->top) &&
           !driver->capsule_frontier && !control->need_pending &&
           !control->effect_pending && !control->failed && !control->invalidated) {
        if (driver->capacity_blocked) {
            control->completion=CETTA_EVAL_INCOMPLETE_CAPACITY;break;
        }
        if (eval_unwinding() || eval_cancel_check()) {
            control->completion=CETTA_EVAL_INCOMPLETE_CANCELLED;break;
        }
        if (!space_read_token_is_current(control->source_read) ||
            control->registry_read.stale) {control->invalidated=true;break;}
        CettaEvalEpisode completion;
        eval_completion_child_begin(&completion);
        if (driver->task_ready) prime_eval_stack_run_task();
        else prime_eval_stack_resume_top();
        CettaEvalCompletion result=eval_completion_child_end(&completion);
        /* An adapter dispatch remains a paid unit, even if it suspends.
         * It is deliberately not equated with an old agenda quantum. */
        if (meter_cost)
            cetta_native_cost_charge_observed(CETTA_COST_QUANTUM,1u);
        control->steps++;grant--;
        if (result!=CETTA_EVAL_COMPLETE) {
            control->completion=result;control->failed=true;
        }
    }
    while (!control->failed && !control->invalidated &&
           control->published<control->output.len) {
        size_t i=control->published;
        Outcome *item=&control->output.items[i];
        if (!outcome_atom_materialize(driver->arena,item)) {
            driver->capacity_blocked=true;
            control->completion=CETTA_EVAL_INCOMPLETE_CAPACITY;break;
        }
        (void)prime_native_mark_delivered(control,i,replay_phases);
        control->published++;
    }
    if (control->registry_read.stale) control->invalidated=true;
    if (control->registry_read.incomplete || driver->capacity_blocked)
        control->completion=CETTA_EVAL_INCOMPLETE_CAPACITY;
}

static PrimeNativeControl *prime_inline_quantum_adopt(
    PrimeEvalStackDriver *source, Space *space, Atom *expression, int fuel,
    const Bindings *environment, CettaObservationDemand demand) {
    if (!prime_ordinary_driver_transfer_admitted(source) ||
        source != g_prime_eval_stack_driver || source->control ||
        source->running_task || source->active_task ||
        source->root_call_running || source->raw_call_depth ||
        source->bind_depth || source->poison_depth ||
        source->capacity_blocked || source->failed ||
        source->stream_pending ||
        source->prepared_pure_cache.execution_scratch_ready ||
        !source->root_target || !source->arena ||
        (!source->task_ready && !source->top) ||
        cetta_parallel_worker_active() ||
        cetta_native_cost_observer_active() ||
        cetta_shared_transition_guard_held_by_current_thread() ||
        eval_current_language_id() != CETTA_LANGUAGE_PRIME ||
        active_search_table_mode() != CETTA_TABLE_MODE_NONE ||
        !prime_native_control_admits(space, source->arena, expression, false))
        return NULL;
    size_t count = source->frame_depth;
    if (count > SIZE_MAX / sizeof(PrimeEvalStackFrame *) ||
        source->continuations.depth != count ||
        source->continuations.top != (CettaOwnedFrameLink *)source->top)
        return NULL;
    PrimeEvalStackFrame **frames = count ? calloc(count, sizeof(*frames)) : NULL;
    if (count && !frames)
        return NULL;
    size_t found = 0u;
    size_t alternative_frames = 0u;
    for (PrimeEvalStackFrame *frame = source->top; frame;
         frame = (PrimeEvalStackFrame *)frame->continuation.next) {
        if (found == count || frame->arena != source->arena ||
            frame->control_sequence || frame->control_pending ||
            frame->control_queued || frame->control_retired ||
            frame->control_closed || frame->control_nested ||
            frame->control_nested_operation ||
            frame->kind == PRIME_EVAL_STACK_FRAME_STREAM_RESUME ||
            (frame->kind >= PRIME_EVAL_STACK_FRAME_CONTROL_SCORE &&
             frame->kind != PRIME_EVAL_STACK_FRAME_CHOICE))
            goto decline;
        if (prime_eval_stack_frame_holds_alternative(frame) &&
            ++alternative_frames > 1u)
            goto decline;
        if (frame->kind == PRIME_EVAL_STACK_FRAME_EQUATION_SEARCH) {
            const PrimeNeedEquationContinuation *eq = frame->equation_search;
            if (!eq ||
                eq->cohort_owner || eq->inspect_incrementally ||
                eq->publication_returns_to_search ||
                eq->state == PRIME_NEED_EQUATION_SEARCH_WAIT_FORCE ||
                (eq->state == PRIME_NEED_EQUATION_SEARCH_WAIT_PUBLICATION &&
                 (frame->child.len != 0u ||
                  (frame == source->top && !source->task_ready))))
                /* A synchronous body can stop at the inline budget without
                 * leaving a child task. Its empty wait is not a retained
                 * producer and cannot be advanced as a completed body. */
                goto decline;
        }
        frames[found++] = frame;
    }
    if (found != count)
        goto decline;
    for (size_t i = 0u; i < count; i++)
        if (!prime_ordinary_target(source, frames, count, frames[i]->target))
            goto decline;
    if (source->task_ready &&
        (!prime_ordinary_target(source, frames, count, source->task.target) ||
         source->task.arena != source->arena))
        goto decline;
    PrimeNativeControl *control = calloc(1u, sizeof(*control));
    if (!control)
        goto decline;
    if (!prime_native_control_initialize(
            control, space, source->arena, expression, fuel, false,
            demand, NULL, environment, NULL, NULL, false) ||
        control->registry_read.incomplete)
        goto release;
    control->reclaims_frames = !control->weight_analysis && !control->weight_keys;
    size_t capacity = 16u;
    if (count > SIZE_MAX / 2u)
        goto release;
    while (capacity < count * 2u) {
        if (capacity > SIZE_MAX / 2u)
            goto release;
        capacity *= 2u;
    }
    control->frame_index = calloc(capacity, sizeof(*control->frame_index));
    control->frame_index_capacity = capacity;
    control->changed_frames = count ? calloc(count, sizeof(*control->changed_frames)) : NULL;
    control->changed_capacity = count;
    if (!control->frame_index || (count && !control->changed_frames))
        goto release;
    PrimeNativeWork *ready = calloc(1u, sizeof(*ready));
    if (!ready)
        goto release;
    ready->owner = control;
    CettaOwnedContinuation initial = {
        .payload = ready, .provider = &prime_native_work_provider};
    if (!cetta_continuation_hub_append(&control->hub, &initial)) {
        free(ready);
        goto release;
    }
    OutcomeSet *target = source->root_target;
    prime_ordinary_move_outcomes(&control->output, target);
    for (size_t i = count; i > 0u; i--) {
        PrimeEvalStackFrame *frame = frames[i - 1u];
        if (frame->target == target)
            frame->target = &control->output;
        if (frame->kind == PRIME_EVAL_STACK_FRAME_CHOICE)
            frame->kind = PRIME_EVAL_STACK_FRAME_ALTERNATIVES;
        if (frame->kind == PRIME_EVAL_STACK_FRAME_EQUATION_SEARCH &&
            frame->equation_search->state ==
                PRIME_NEED_EQUATION_SEARCH_WAIT_PUBLICATION) {
            OutcomeSet *old_target = &frame->child;
            OutcomeSet *new_target = frame->target;
            for (size_t j = 0u; j < i - 1u; j++)
                if (frames[j]->target == old_target)
                    frames[j]->target = new_target;
            if (source->task_ready && source->task.target == old_target)
                source->task.target = new_target;
            frame->equation_search->state =
                PRIME_NEED_EQUATION_SEARCH_BUILD_PUBLICATION;
        }
    }
    if (source->task_ready && source->task.target == target)
        source->task.target = &control->output;
    control->driver = *source;
    control->driver.control = control;
    control->driver.ordinary_ordered = false;
    control->driver.answer_limit = 0u;
    control->driver.streaming = true;
    control->driver.nested = true;
    control->driver.continuations = (CettaOwnedFrameStack){0};
    control->driver.root_target = &control->output;
    control->driver.collection_admitted = false;
    control->driver.top = NULL;
    for (size_t i = count; i > 0u; i--) {
        PrimeEvalStackFrame *frame = frames[i - 1u];
        prime_native_control_register_frame(control, frame);
        control->driver.top = frame;
    }
    if (control->driver.task_ready) {
        ready->task = control->driver.task;
        ready->task_initialized = true;
        ready->parent = control->driver.top;
        if (ready->parent)
            ready->parent->control_pending++;
        memset(&control->driver.task, 0, sizeof(control->driver.task));
        control->driver.task_ready = false;
    } else {
        ready->frame = control->driver.top;
        ready->frame->control_queued = true;
    }
    control->driver.top = NULL;
    g_prime_eval_stack_driver = &control->driver;
    prime_native_control_scan(control);
    g_prime_eval_stack_driver = source;
    memset(source, 0, sizeof(*source));
    free(frames);
    return control;
release:
    prime_native_control_destroy(control);
decline:
    free(frames);
    return NULL;
}

static void prime_ordinary_capsule_stack_run(PrimeNativeControl *control,uint64_t grant) {
    PrimeEvalStackDriver *driver=&control->driver;
    assert(driver->capsule_stack && driver->control==control && !control->cost_enabled);
    if (!grant || control->failed || control->invalidated) return;
    if (!space_read_token_is_current(control->source_read) ||
        !prime_native_control_query_reads_current(control) ||
        !registry_read_view_validate(&control->registry_read,g_registry)) {
        control->invalidated=true;return;
    }
    PrimeNativeRunGuard guard;
    prime_native_run_enter(control,&guard);
    if (guard.cost_active) {
        if (prime_replay_phases_requested())
            prime_ordinary_capsule_stack_loop(control,grant,true,true);
        else
            prime_ordinary_capsule_stack_loop(control,grant,true,false);
    } else {
        if (prime_replay_phases_requested())
            prime_ordinary_capsule_stack_loop(control,grant,false,true);
        else
            prime_ordinary_capsule_stack_loop(control,grant,false,false);
    }
    prime_native_run_leave(&guard);
}
