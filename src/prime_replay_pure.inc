/* Admission for the explicit serial replay fragment. This walks actual
 * source views and captured executable roots without evaluating them. */
typedef struct { Atom *atom; bool data; } PrimeReplaySyntaxRoot;
typedef struct {
    Space *space;
    Arena *arena;
    PrimeReplaySyntaxRoot *roots;
    const PrimeNeedSnapshot **snapshots;
    PrimeNeedRecording **recordings;
    size_t length, at, capacity;
    size_t snapshot_count, recording_count;
    const Space *transaction_target;
    const char *refusal;
} PrimeReplaySourceScan;

static bool prime_replay_source_recording(PrimeReplaySourceScan *scan,PrimeNeedRecording *group) {
    if(!group)return true;
    for(size_t i=0u;i<scan->recording_count;i++)if(scan->recordings[i]==group)return true;
    if(scan->recording_count==scan->capacity){scan->refusal="RecordingInspectionLimit";return false;}
    scan->recordings[scan->recording_count++]=group;return true;
}

static bool prime_replay_source_receipt(PrimeReplaySourceScan *scan,const PrimeNeedReceipt *receipt) {
    return prime_replay_source_recording(scan,prime_need_receipt_recording_owner(receipt));
}

static bool prime_replay_source_push(PrimeReplaySourceScan *scan,Atom *atom,bool data) {
    if (!atom) return true;
    for(size_t i=0u;i<scan->length;i++) {
        cetta_native_cost_charge(CETTA_COST_INDEX_VISIT,1u);
        if(scan->roots[i].atom==atom&&scan->roots[i].data==data)return true;
    }
    if(scan->length==scan->capacity) {scan->refusal="SourceInspectionLimit";return false;}
    scan->roots[scan->length++]=(PrimeReplaySyntaxRoot){atom,data};return true;
}

static bool prime_replay_source_need_atom(const Atom *atom,void *raw) {
    return prime_replay_source_push(raw,(Atom *)atom,false);
}

static bool prime_replay_source_snapshot(PrimeReplaySourceScan *scan,const PrimeNeedSnapshot *s) {
    for(size_t i=0u;i<scan->snapshot_count;i++)if(scan->snapshots[i]==s)return true;
    if(scan->snapshot_count==scan->capacity){scan->refusal="CaptureInspectionLimit";return false;}
    scan->snapshots[scan->snapshot_count++]=s;
    return prime_need_snapshot_atoms_satisfy(s,prime_replay_source_need_atom,scan);
}

static bool prime_replay_source_environment(PrimeReplaySourceScan *scan,const Bindings *b) {
    if(b->owners){scan->refusal="BindingsValueOwner";return false;}
    if(!prime_replay_source_snapshot(scan,bindings_need_view(b))||
        !prime_replay_source_receipt(scan,bindings_branch_state_view(b)))return false;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if(!prime_replay_source_receipt(scan,bindings_receipt_view(b)))return false;
#endif
    BindingsIterator it={.bindings=b};Binding item;
    while(bindings_iterator_next(&it,&item))
        if(!prime_replay_source_push(scan,item.name_key,true)||
           !prime_replay_source_push(scan,item.value.skeleton,false))return false;
    return true;
}

static bool prime_replay_source_outcomes(PrimeReplaySourceScan *scan,const OutcomeSet *set) {
    for(CettaCount i=0u;i<set->len;i++) {
        const Outcome *o=&set->items[i];
        if(o->kind!=CETTA_OUTCOME_INLINE||o->answer_ref.bank) {
            scan->refusal="AnswerBankOwner";return false;
        }
        if(o->result_form!=CETTA_CALL_RESULT_EXPRESSION &&
           o->result_form!=CETTA_CALL_RESULT_COMPLETED_VALUE) {
            scan->refusal="OutcomeResultForm";return false;
        }
        if(!variant_instance_checkpoint_atoms(&o->variant,prime_replay_source_need_atom,scan)||
           !prime_replay_source_push(scan,o->atom,scan->transaction_target!=NULL)||
           !prime_replay_source_push(scan,o->delayed,false)||
           !prime_replay_source_push(scan,o->materialized_atom,true)||
           !prime_replay_source_environment(scan,&o->env))return false;
    }
    return true;
}

static bool prime_replay_source_task(PrimeReplaySourceScan *scan,const PrimeEvalStackTask *t) {
    if(t->space!=scan->space) {scan->refusal="CapturedSpace";return false;}
    if((unsigned)t->kind>PRIME_EVAL_STACK_TASK_NORMALIZE||t->control_key||t->control_scoring||
        (t->etype&&!atom_is_symbol_id(t->etype,g_builtin_syms.undefined_type))) {
        scan->refusal="TaskService";return false;
    }
    return prime_replay_source_push(scan,t->atom,false)&&
        prime_replay_source_push(scan,t->etype,true)&&
        (!t->dynamic_env_initialized||prime_replay_source_environment(scan,&t->dynamic_env))&&
        (!t->seed_env_initialized||prime_replay_source_environment(scan,&t->seed_env));
}

static bool prime_replay_source_equation(PrimeReplaySourceScan *scan,Atom *row) {
    Atom *lhs=NULL,*rhs=NULL,*grade=NULL;
    if(!space_equation_view(row,false,&lhs,&rhs,&grade)||
        !lhs||lhs->kind!=ATOM_EXPR||!lhs->expr.len||
        !lhs->expr.elems[0]||lhs->expr.elems[0]->kind!=ATOM_SYMBOL) {
        scan->refusal="CapturedEquation";return false;
    }
    /* Matching reads the complete structural pattern; only the RHS runs.
     * Typed result contracts and graded/admission programs are excluded. */
    return prime_replay_source_push(scan,lhs,true)&&
        prime_replay_source_push(scan,rhs,false);
}

static bool prime_replay_source_frame_kind(PrimeEvalStackFrameKind kind) {
    switch(kind) {
    case PRIME_EVAL_STACK_FRAME_BIND_FINISH:
    case PRIME_EVAL_STACK_FRAME_STRICT:
    case PRIME_EVAL_STACK_FRAME_IF:
    case PRIME_EVAL_STACK_FRAME_LET:
    case PRIME_EVAL_STACK_FRAME_EQUATION_SEARCH:
    case PRIME_EVAL_STACK_FRAME_FORCE:
    case PRIME_EVAL_STACK_FRAME_NORMALIZE_ATOM:
    case PRIME_EVAL_STACK_FRAME_NORMALIZE_CHILDREN:
    case PRIME_EVAL_STACK_FRAME_ALTERNATIVES:
    case PRIME_EVAL_STACK_FRAME_WEIGHT_WHERE:return true;
    default:return false;
    }
}

/* Publication advice exists even when no scoring policy is installed.
 * Only owned, unscored cells are admitted here; scoring and weighted policy
 * services keep their separate contracts. Membership checks compare addresses
 * before following a work/frame edge into this complete owned list. */
static bool prime_replay_source_advice_member(PrimeReplaySourceScan *scan,
    const PrimeNativeAdvice *head,const PrimeNativeAdvice *wanted) {
    if(!wanted)return true;
    size_t count=0u;
    for(const PrimeNativeAdvice *p=head;p;p=p->next) {
        if(count++==scan->capacity){scan->refusal="AdviceInspectionLimit";return false;}
        cetta_native_cost_charge(CETTA_COST_INDEX_VISIT,1u);
        if(p==wanted)return true;
    }
    scan->refusal="AdviceOwner";return false;
}

static bool prime_replay_source_advice(PrimeReplaySourceScan *scan,
    const PrimeNativeControl *control) {
    size_t count=0u;
    for(const PrimeNativeAdvice *p=control->advice_cells;p;p=p->next) {
        if(count++==scan->capacity){scan->refusal="AdviceInspectionLimit";return false;}
        cetta_native_cost_charge(CETTA_COST_ANALYSIS_STEP,1u);
        if(p->scoring||p->key||p->key_text||p->coefficient) {
            scan->refusal="ScoredAdviceService";return false;
        }
        /* Inheritance refers to an older publication. The list is in reverse
         * creation order, so this also excludes cycles in those links. */
        if(!prime_replay_source_advice_member(scan,p->next,p->inherited)||
            !prime_replay_source_advice_member(scan,p->next,p->follows)||
            !prime_replay_source_environment(scan,&p->environment)||
            !prime_replay_source_push(scan,p->subject,false)||
            !prime_replay_source_push(scan,p->factor,false)||
            !prime_replay_source_push(scan,p->coefficient,true)||
            !prime_replay_source_push(scan,p->key,true))return false;
    }
    return true;
}

static bool prime_replay_source_roots(PrimeReplaySourceScan *scan,const PrimeNativeControl *c) {
    if(!prime_replay_source_advice(scan,c)||
       !prime_replay_source_push(scan,c->source_expression,scan->transaction_target!=NULL)||
       (c->base_environment_initialized&&!prime_replay_source_environment(scan,&c->base_environment))||
       !prime_replay_source_outcomes(scan,&c->output))return false;
    const PrimeNativeTopEpisode *episode=c->owned_top_episode;
    if(!episode||!prime_replay_source_snapshot(scan,&episode->need)||
        !prime_replay_source_receipt(scan,&episode->branch))return false;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if(!prime_replay_source_receipt(scan,&episode->receipt)||
        !prime_replay_source_recording(scan,c->history.group))return false;
    for(size_t i=0u;i<c->history.count;i++)
        if(!prime_replay_source_receipt(scan,&c->history.entries[i].operation)||
            !prime_replay_source_receipt(scan,&c->history.entries[i].prepared))return false;
#endif
    size_t ready=cetta_continuation_hub_length(&c->hub);
    if(c->parked_count>SIZE_MAX-ready){scan->refusal="OccurrenceExtent";return false;}
    for(size_t i=0u;i<ready+c->parked_count;i++) {
        const CettaOwnedContinuation *o=i<ready?cetta_continuation_hub_at(&c->hub,i):&c->parked[i-ready];
        const PrimeNativeWork *w=o->payload;
        if(scan->transaction_target && parent_copy_transaction_work(c,o))continue;
        if(o->provider!=&prime_native_work_provider||!w||w->licence||w->resource_step||
            w->resource_provider||w->dialect||w->parallel||w->scoring) {
            scan->refusal="SelectedService";return false;
        }
        if(!prime_replay_source_advice_member(scan,c->advice_cells,w->advice)||
            (w->task_initialized&&!prime_replay_source_task(scan,&w->task)))return false;
    }
    for(const PrimeEvalStackFrame *f=c->frames;f;f=f->control_next) {
        if(f->control_retired) {
            /* Only released shells are projected. Their stale payload bits
             * must never be traversed as live roots. */
            if(!f->control_closed||!f->control_notified||f->control_queued||
                f->control_pending||f->env_initialized||f->child_initialized||
                f->equation_search||f->match_rows||f->control_nested||f->dialect_source) {
                scan->refusal="RetiredPayload";return false;
            }
            continue;
        }
        if(f->space!=scan->space||f->control_nested||f->match_rows||f->control_advisory||
            !prime_replay_source_frame_kind(f->kind)||f->control_key||f->control_bound||
            f->control_scoring||f->control_selection_length||
            f->control_child_bound_length||f->dialect_source||f->control_return_advice_length||
            f->weight_algebra||f->weight_trace||
            (f->etype&&!atom_is_symbol_id(f->etype,g_builtin_syms.undefined_type))) {
            scan->refusal="FrameService";return false;
        }
        if(!prime_replay_source_advice_member(scan,c->advice_cells,f->control_advice))return false;
        Atom *roots[]={f->atom,f->strict_source,f->then_branch,f->else_branch,
            f->weight_subject,f->ref,f->cell.origin,f->cell.cached};
        for(size_t i=0u;i<sizeof(roots)/sizeof(*roots);i++)
            if(!prime_replay_source_push(scan,roots[i],false))return false;
        if(!prime_replay_source_push(scan,f->etype,true))return false;
        if(f->normalization_child_index>f->normalization_child_count||
            (f->normalization_child_count&&!f->normalization_children)) {
            scan->refusal="NormalizationRoots";return false;
        }
        /* The constructor initializes the whole array from the source or
         * supplied children; both completed and remaining slots are roots. */
        for(size_t i=0u;i<f->normalization_child_count;i++)
            if(!prime_replay_source_push(scan,f->normalization_children[i],false))return false;
        if((f->env_initialized&&!prime_replay_source_environment(scan,&f->env))||
           (f->child_initialized&&!prime_replay_source_outcomes(scan,&f->child))||
            !prime_replay_source_snapshot(scan,&f->before)||
           !prime_replay_source_snapshot(scan,&f->evaluating))return false;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
        if(!prime_replay_source_receipt(scan,&f->before_receipt)||
            !prime_replay_source_receipt(scan,&f->producer_receipt))return false;
#endif
        if(f->equation_search) {
            const PrimeNeedEquationContinuation *e=f->equation_search;
            if(e->space!=scan->space){scan->refusal="EquationSpace";return false;}
            if(e->result_contract_count||e->root_atom_count!=e->plan_count||
                (e->plan_count&&(!e->plans||!e->root_atoms))) {
                scan->refusal="EquationContract";return false;
            }
            if(!prime_replay_source_push(scan,e->original_call,false))return false;
            const OutcomeSet *sets[]={&e->work,&e->current,&e->raw_results,&e->residuals,&e->publication,&e->forced_worlds};
            for(size_t i=0u;i<6u;i++)if(!prime_replay_source_outcomes(scan,sets[i]))return false;
            for(size_t i=0u;i<e->plan_count;i++) {
                if(e->plans[i].equation!=e->root_atoms[i]||
                    !prime_replay_source_equation(scan,e->plans[i].equation)||
                    !prime_replay_source_outcomes(scan,&e->plans[i].results))return false;
            }
        }
    }
    return true;
}

static bool prime_replay_source_atom(PrimeReplaySourceScan *scan,PrimeReplaySyntaxRoot root) {
    Atom *atom=root.atom;
    if(!atom)return true;
    cetta_native_cost_charge(CETTA_COST_ANALYSIS_STEP,1u);
    if(atom->structural_facts&ATOM_STRUCTURAL_HAS_NATIVE_HANDLE_ID) {
        scan->refusal="CapturedCapability";return false;
    }
    if(atom->kind==ATOM_VAR)return prime_replay_source_push(scan,atom->name_key,true);
    if(atom->kind==ATOM_SYMBOL) {
        if(!root.data&&(registry_lookup_atom(atom)||prime_symbol_is_defined(scan->space,atom))) {
            scan->refusal="SymbolProducer";return false;
        }
        return true;
    }
    if(atom->kind==ATOM_GROUNDED) {
        switch(atom->ground.gkind) {
        case GV_INT:case GV_FLOAT:case GV_BOOL:case GV_STRING:case GV_BIGINT:case GV_RATIONAL:return true;
        case GV_SPACE:
            if(root.data && scan->transaction_target &&
                atom->ground.ptr==scan->transaction_target)return true;
            scan->refusal="TargetReferenceRole";return false;
        case GV_INTERNAL_TAG:
            if(root.data&&atom_checkpoint_internal_tag_admitted(atom->ground.ival))return true;
            scan->refusal="PrivateTagOperation";return false;
        case GV_PRIME_NEED_CAPABILITY:
            /* Origins are scanned through the actual captured ancestry;
             * an unrelated capability is not admitted by its printed value. */
            for(size_t i=0u;i<scan->snapshot_count;i++)
                if(prime_need_ref_belongs_to(atom,scan->snapshots[i],NULL))return true;
            scan->refusal="UncapturedNeed";return false;
        default:scan->refusal="GroundedService";return false;
        }
    }
    if(atom->kind!=ATOM_EXPR){scan->refusal="Syntax";return false;}
    if(root.data) {
        for(CettaExprIndex i=0u;i<atom->expr.len;i++)
            if(!prime_replay_source_push(scan,atom->expr.elems[i],true))return false;
        return true;
    }
    if(!atom->expr.len)return true;
    Atom *head=atom->expr.elems[0];
    if(!head||head->kind!=ATOM_SYMBOL){scan->refusal="ComputedHead";return false;}
    /* Executable arity follows the authored form. The complete private
     * suffix is still admitted as data and compared by the Atom reader. */
    CettaExprLen authored=atom_authored_len(atom);
    if(!authored||authored>atom->expr.len){scan->refusal="AuthoredExtent";return false;}
    for(CettaExprIndex i=authored;i<atom->expr.len;i++)
        if(!prime_replay_source_push(scan,atom->expr.elems[i],true))return false;
    SymbolId h=atom_symbol_current_id(head);CettaExprLen n=authored-1u;
    const PrimeNeedSymbolIds *ids=prime_need_symbols();
    if(h==g_builtin_syms.quote&&n==1u)
        return prime_replay_source_push(scan,expr_arg(atom,0u),true);
    if(h==g_builtin_syms.superpose&&n==1u) {
        Atom *choices=expr_arg(atom,0u);
        if(!choices||choices->kind!=ATOM_EXPR){scan->refusal="DynamicChoices";return false;}
        for(CettaExprIndex i=0u;i<choices->expr.len;i++)
            if(!prime_replay_source_push(scan,choices->expr.elems[i],false))return false;
        return true;
    }
    bool explicit=(h==ids->delay&&n==1u)||(h==ids->force&&n==1u)||
        (h==g_builtin_syms.if_text&&n==3u);
    if(h==g_builtin_syms.let&&n==3u) {
        if(!expr_arg(atom,0u)||expr_arg(atom,0u)->kind!=ATOM_VAR) {
            scan->refusal="BindingPattern";return false;
        }
        return prime_replay_source_push(scan,expr_arg(atom,1u),false)&&
            prime_replay_source_push(scan,expr_arg(atom,2u),false);
    }
    if(h==ids->weight_where&&n==2u) {
        Atom *factor=expr_arg(atom,1u);
        if(!factor||factor->kind!=ATOM_GROUNDED||
           (factor->ground.gkind!=GV_INT&&factor->ground.gkind!=GV_RATIONAL)) {
            scan->refusal="CoefficientService";return false;
        }
        return prime_replay_source_push(scan,expr_arg(atom,0u),false);
    }
    if(!explicit) {
        if(registry_lookup_atom(head)||symbol_id_is_builtin(h)||
           prime_need_is_explicit_control_form(atom)||prime_scoped_judgment_is_head_id(h)||
           is_grounded_op(h)||h==ids->cost_run||h==ids->cost_inspect||h==ids->cost_trace||
           h==ids->cost_storage||h==ids->cost_analyze||
           h==ids->history_capture||h==ids->history_record||h==ids->history_replay||
           h==ids->history_checkpoint||h==ids->history_dispose||
           (g_library_context->foreign_runtime&&cetta_foreign_is_callable_atom(head))) {
            scan->refusal="ProducerOperation";return false;
        }
        Atom **types=NULL;uint32_t count=0u;
        bool complete=space_get_declared_types_checked(scan->space,scan->arena,head,&types,&count);
        free(types);
        if(!complete||count){scan->refusal="TypedProducer";return false;}
        SpaceEquationCursor cursor;
        if(!space_equation_cursor_init_view(scan->space,h,true,&cursor)) {
            scan->refusal="SourceCursor";return false;
        }
        for(;;) {
            SpaceEquationOccurrenceId id;
            SpaceEquationCursorStep step=space_equation_cursor_next(&cursor,&id);
            if(step==SPACE_EQUATION_CURSOR_END)break;
            SpaceEquationOccurrence row;Atom *lhs=NULL,*rhs=NULL,*grade=NULL;
            if(step!=SPACE_EQUATION_CURSOR_ITEM||
               !space_equation_occurrence_resolve_view(id,true,&row)||
               !space_equation_view(row.equation,false,&lhs,&rhs,&grade)||
               atom_head_symbol_id(lhs)!=h) {
                scan->refusal="UngradedNamedRows";return false;
            }
            if(!prime_replay_source_equation(scan,row.equation))return false;
        }
    }
    for(CettaExprIndex i=1u;i<authored;i++)
        if(!prime_replay_source_push(scan,atom->expr.elems[i],false))return false;
    return true;
}

static void prime_replay_source_free(PrimeReplaySourceScan *scan) {
    free(scan->roots);free(scan->snapshots);free(scan->recordings);
    *scan=(PrimeReplaySourceScan){0};
}

static bool prime_replay_source_admitted_profile(const PrimeNativeControl *control,
    Arena *inspection,size_t nodes,PrimeReplaySourceScan *scan,const char **refusal,
    bool transaction,bool source_reserved,size_t bytes,const Space *external_target) {
    /* The existing complete preflight excludes issued/active Task, driver
     * stack, active worker, in-flight parallel wave and prepared scratch.
     * The graph walk below is not a replacement for that ownership gate. */
    SpaceCheckpointBudget budget={.work_limit=nodes,.byte_limit=bytes};
    if((transaction ? !parent_copy_transaction_admitted(control,source_reserved,&budget,refusal)
                    : !parent_copy_quiescent(control,source_reserved,refusal)) ||
        !parent_copy_episode_admitted(control))return false;
    if(!control||!control->owned_top_episode||!control->cost_enabled||
       control->policy.selection||control->policy.priority||control->policy.by_weight||
       control->policy.workers>1u||(!transaction&&control->space_agenda)||control->parallel_owner||
       control->weight_analysis||control->active_advice||control->active_scoring||
       nodes>SIZE_MAX/sizeof(PrimeReplaySyntaxRoot)||nodes>SIZE_MAX/sizeof(PrimeNeedSnapshot *)||
       nodes>SIZE_MAX/sizeof(PrimeNeedRecording *)) {
        *refusal="SerialPureProfile";return false;
    }
    *scan=(PrimeReplaySourceScan){.space=control->space,.arena=inspection,.capacity=nodes,
        .transaction_target=transaction?external_target:NULL};
    if(transaction&&!external_target){*refusal="MissingExternalTarget";return false;}
    scan->roots=nodes?calloc(nodes,sizeof(*scan->roots)):NULL;
    scan->snapshots=nodes?calloc(nodes,sizeof(*scan->snapshots)):NULL;
    scan->recordings=nodes?calloc(nodes,sizeof(*scan->recordings)):NULL;
    if(nodes&&(!scan->roots||!scan->snapshots||!scan->recordings)){
        *refusal="Allocation";return false;
    }
    bool ok=prime_replay_source_roots(scan,control);
    while(ok&&scan->at<scan->length)ok=prime_replay_source_atom(scan,scan->roots[scan->at++]);
    *refusal=ok?NULL:(scan->refusal?scan->refusal:"CapturedSource");
    return ok;
}

static __attribute__((unused)) bool prime_replay_source_admitted(const PrimeNativeControl *control,
    Arena *inspection,size_t nodes,PrimeReplaySourceScan *scan,const char **refusal) {
    return prime_replay_source_admitted_profile(control,inspection,nodes,scan,refusal,
        false,false,SIZE_MAX,NULL);
}
