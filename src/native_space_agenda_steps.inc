/* Resource-provider operations share one implementation. The including
 * owner supplies AGENDA_SUFFIX and a literal AGENDA_RECORDING policy. */
#define AGENDA_JOIN_(name, suffix) name##suffix
#define AGENDA_JOIN(name, suffix) AGENDA_JOIN_(name, suffix)
#define AGENDA_OP(name) AGENDA_JOIN(name, AGENDA_SUFFIX)
#if AGENDA_RECORDING
#define AGENDA_SCOPE agenda_observation
#define AGENDA_OBSERVE(...) agenda_observe(__VA_ARGS__)
#define AGENDA_OBSERVE_EVENT(...) agenda_observe_event(__VA_ARGS__)
#define AGENDA_CHARGE(...) agenda_charge(__VA_ARGS__)
#define AGENDA_PERMIT(a) agenda_observation_permit(a)
#define AGENDA_COMMITTED(a) agenda_observation_committed(a)
#else
#define AGENDA_SCOPE ((CettaSpaceAgendaObservationScope *)NULL)
#define AGENDA_OBSERVE(...) true
#define AGENDA_OBSERVE_EVENT(...) true
#define AGENDA_CHARGE(a, site, kind, units) cetta_native_cost_charge(kind, units)
#define AGENDA_PERMIT(a) true
#define AGENDA_COMMITTED(a) ((void)0)
#endif

static void AGENDA_OP(transaction_clear)(CettaSpaceAgenda *a);
static void AGENDA_OP(agenda_free_one)(CettaSpaceAgenda *a);
static void AGENDA_OP(cetta_space_agenda_free)(CettaSpaceAgenda *a);
static bool AGENDA_OP(capture_begin)(CettaSpaceAgenda *a, bool validating);
static CettaSpaceAgendaStep AGENDA_OP(select_step)(CettaSpaceAgenda *a);
static CettaSpaceAgendaStep AGENDA_OP(rank_step)(CettaSpaceAgenda *a);
static CettaSpaceAgendaStep AGENDA_OP(select_publish)(CettaSpaceAgenda *a,SpaceReadToken selection_read);
static CettaSpaceAgendaStep AGENDA_OP(cetta_space_agenda_prepare_source)(
    CettaSpaceAgenda *, SpaceReadToken, CettaIndex);
static CettaSpaceAgendaStep AGENDA_OP(revalidate_choice)(CettaSpaceAgenda *);
static CettaSpaceAgendaStep AGENDA_OP(capture_step)(CettaSpaceAgenda *a);
static CettaSpaceAgendaMatch *AGENDA_OP(match_begin)(CettaSpaceAgenda *a, Atom *pattern,
        Atom *row, const Bindings *environment);
static CettaSpaceAgendaMatch *AGENDA_OP(cetta_space_agenda_match_capture)(CettaSpaceAgenda *a);
static bool AGENDA_OP(cetta_space_agenda_match_run)(CettaSpaceAgendaMatch *match, uint64_t node_budget);
static CettaMatchCursorStep AGENDA_OP(finish_match)(CettaSpaceAgenda *a, Bindings *result);
static CettaMatchCursorStep AGENDA_OP(match_step)(CettaSpaceAgenda *a, Atom *pattern,
        Atom *row, const Bindings *base, Bindings *result);
static bool AGENDA_OP(row_add)(CettaSpaceAgenda *a);
static bool AGENDA_OP(rows_agree)(CettaSpaceAgenda *a);
static CettaSpaceAgendaStep AGENDA_OP(join_finished)(CettaSpaceAgenda *a);
static bool AGENDA_OP(world_child_new)(CettaSpaceAgenda *a, const JoinedRow *row);
static CettaSpaceAgendaStep AGENDA_OP(join_step)(CettaSpaceAgenda *a);
static CettaSpaceAgendaStep AGENDA_OP(build_step)(CettaSpaceAgenda *a);
static CettaSpaceAgendaStep AGENDA_OP(commit_step)(CettaSpaceAgenda *a);
static CettaSpaceAgendaStep AGENDA_OP(agenda_step_core)(CettaSpaceAgenda *a);
static CettaSpaceAgendaStep AGENDA_OP(cetta_space_agenda_step)(CettaSpaceAgenda *a);

static void AGENDA_OP(transaction_clear)(CettaSpaceAgenda *a) {
    (void)AGENDA_OBSERVE(a,CETTA_AGENDA_EVENT_PRECLEAR);
    match_free(a->matching);a->matching=NULL;
    join_free(a);
    free(a->inputs); a->inputs = NULL; a->input_count = 0u;
    free(a->rows); a->rows = NULL; a->row_count = a->row_capacity = 0u;
    free(a->validation); a->validation = NULL;
    a->validation_count = a->validation_capacity = 0u;
    for(size_t i=0u;i<2u;i++) {
        free(a->match_epochs[i]);a->match_epochs[i]=NULL;
        a->match_epoch_count[i]=a->match_epoch_capacity[i]=0u;
    }
    if (a->snapshot_ready) space_free(&a->snapshot);
    if (a->current_ready) space_free(&a->current);
    if (a->replacement_ready) space_free(&a->replacement);
    a->snapshot_ready = a->current_ready = a->replacement_ready = false;
    a->borrowed_preparation = false;
    a->validating = false;
}

static void AGENDA_OP(agenda_free_one)(CettaSpaceAgenda *a) {
    if (!a) return;
    if (a->selection_ready) space_occurrence_cursor_release(&a->selection);
    AGENDA_OP(transaction_clear)(a);
    bindings_free(&a->captured);
    free(a->selected_key);
    free(a->waiting);
    free(a->candidates);
    free(a->reported_atoms);
    if (a->world_ready) space_free(&a->world);
    if (a->owned_arena) {arena_free(a->owned_arena);free(a->owned_arena);}
    free(a);
}

static void AGENDA_OP(cetta_space_agenda_free)(CettaSpaceAgenda *a) {
    while (a) {
        CettaSpaceAgenda *next = a->owned_next;
        AGENDA_OP(agenda_free_one)(a);
        a = next;
    }
}

static bool AGENDA_OP(capture_begin)(CettaSpaceAgenda *a, bool validating) {
    (void)AGENDA_OBSERVE(a,CETTA_AGENDA_EVENT_CAPTURE_RESET);
    Space *destination = validating ? &a->current : &a->snapshot;
    bool *ready = validating ? &a->current_ready : &a->snapshot_ready;
    if (*ready) space_free(destination);
    *ready = false;
    if (!a->borrowed_preparation) {
        memset(destination,0,sizeof(*destination));
        space_init_with_universe(destination,a->space->universe);
        *ready = true;
    }
    a->capture_read = space_read_token(a->space);
    a->capture_length = space_length64(a->space);
    a->capture_index = 0u;
    a->validating = validating;
    a->match_epoch_count[validating?1u:0u]=0u;
    if(validating)a->current_exec_appended=false;
    else a->snapshot_exec_appended=false;
    a->phase = validating ? AGENDA_VALIDATE_CAPTURE : AGENDA_CAPTURE;
    (void)AGENDA_OBSERVE(a,CETTA_AGENDA_EVENT_CAPTURE_BEGIN);
    return true;
}

static CettaSpaceAgendaStep AGENDA_OP(select_step)(CettaSpaceAgenda *a) {
    if (!a->selection_ready) {
        a->selected = NULL;
        a->candidate_count=0u;
        size_t retained=0u;
        for(size_t i=0u;i<a->waiting_count;i++)
            if(space_read_token_is_current(a->waiting[i].read))a->waiting[retained++]=a->waiting[i];
        a->waiting_count=retained;
        if (!space_occurrence_cursor_init(a->space,a->wildcard,&a->selection))
            return pending(a,"SelectionCursor");
        a->selection_ready = true;
    }
    CettaIndex index;
    SpaceOccurrenceCursorStep step = space_occurrence_cursor_next(&a->selection,&index);
    AGENDA_CHARGE(a,CETTA_AGENDA_CHARGE_SELECTION,CETTA_COST_QUEUE_VISIT,1u);
    if (step == SPACE_OCCURRENCE_CURSOR_ITEM) {
        Atom *row = space_occurrence_cursor_atom(&a->selection,index);
        if (!row) return pending(a,"SelectionProjection");
        bool executable=shaped(row,a->exec,3u);
        bool waiting=executable && waiting_contains(a,index);
        (void)AGENDA_OBSERVE_EVENT(a,CETTA_AGENDA_EVENT_SELECTION,index,
            waiting?3u:executable?2u:1u,0u,a->selection.read,NULL,row,NULL);
        if (!executable || waiting) return CETTA_SPACE_AGENDA_PROGRESS;
        if(a->advise) {
            if(!candidate_add(a,index,row))return pending(a,"CandidateAdvice");
            return CETTA_SPACE_AGENDA_PROGRESS;
        }
        uint8_t *key = NULL;
        size_t length = 0u;
        const char *error = NULL;
        if (a->contract.raw_mm2_order &&
            !cetta_mm2_atom_to_bridge_expr_bytes(a->owner,row,&key,&length,&error))
            return pending(a,"CompactKey");
        bool first = !a->selected;
        bool less = first;
        if (!first && a->contract.raw_mm2_order) {
            size_t common = length < a->selected_key_length ? length : a->selected_key_length;
            int cmp = memcmp(key,a->selected_key,common);
            less = cmp < 0 || (!cmp && length < a->selected_key_length);
        }
        if (less) {
            free(a->selected_key); a->selected_key = key; a->selected_key_length = length;
            key=NULL;
            a->selected = atom_deep_copy(a->owner,row);
            a->selected_index = index;
        }
        free(key);
        return CETTA_SPACE_AGENDA_PROGRESS;
    }
    SpaceReadToken selection_read = a->selection.read;
    bool current = step == SPACE_OCCURRENCE_CURSOR_END &&
        space_read_token_is_current(selection_read);
    (void)AGENDA_OBSERVE_EVENT(a,CETTA_AGENDA_EVENT_SELECTION,UINT64_MAX,
        (uint64_t)step,0u,selection_read,NULL,NULL,NULL);
    space_occurrence_cursor_release(&a->selection); a->selection_ready = false;
    if (!current) { a->selected = NULL; return CETTA_SPACE_AGENDA_RETRY; }
    if(a->candidate_count) {
        size_t index=0u;
        a->oldest=a->schedule.lane[a->schedule.state]==CETTA_SELECTION_LANE_OLDEST;
        if(!cetta_selection_automaton_select(&a->schedule,a->candidate_count,&index))
            return pending(a,"SelectionDiscipline");
        a->selection_read=selection_read;a->ranking=1u;a->best=0u;a->phase=AGENDA_RANK;
        return CETTA_SPACE_AGENDA_PROGRESS;
    }
    return AGENDA_OP(select_publish)(a,selection_read);
}

static CettaSpaceAgendaStep AGENDA_OP(rank_step)(CettaSpaceAgenda *a) {
    if(!space_read_token_is_current(a->selection_read)) {
        a->selected=NULL;a->phase=AGENDA_SELECT;return CETTA_SPACE_AGENDA_RETRY;
    }
    if(a->ranking<a->candidate_count && !a->oldest) {
        size_t i=a->ranking++;
        cetta_native_cost_charge(CETTA_COST_QUEUE_VISIT,1u);
        if(a->before(a->advice_context,a->candidates[i].advice,a->candidates[a->best].advice))a->best=i;
        return CETTA_SPACE_AGENDA_PROGRESS;
    }
    a->selected=a->candidates[a->best].exec;a->selected_index=a->candidates[a->best].index;
    a->chosen_advice=a->candidates[a->best].advice;
    return AGENDA_OP(select_publish)(a,a->selection_read);
}

static CettaSpaceAgendaStep AGENDA_OP(select_publish)(CettaSpaceAgenda *a,SpaceReadToken selection_read) {
    if (!a->selected) {
        if (a->contract.snapshot_worlds) {
            if (a->uninterpreted_choice) {
                a->phase=AGENDA_WAIT;return pending(a,"UninterpretedExec");
            }
            if (a->yielded_child) {
                a->phase=AGENDA_DONE;return CETTA_SPACE_AGENDA_CLOSED;
            }
            a->phase=AGENDA_WORLD_REPORT;
            return CETTA_SPACE_AGENDA_PROGRESS;
        }
        return a->waiting_count ? pending(a,"WaitingForMatch") : CETTA_SPACE_AGENDA_CLOSED;
    }
    CETTA_SCOPED_SHARED_TRANSITION(publication);
    if (!space_read_token_is_current(selection_read)) {
        a->selected = NULL;a->phase=AGENDA_SELECT;return CETTA_SPACE_AGENDA_RETRY;
    }
    a->selected_read=selection_read;
#if AGENDA_CONTROLLED
    if (!a->contract.explicit_inputs) {
        a->selection_read=selection_read;a->phase=AGENDA_ACTIVATE;
        if (!agenda_activation_permit(a)) return pending(a,"ProtectedActivation");
        if (!space_read_token_is_current(selection_read))
            return pending(a,"ActivationSourceChanged");
    }
#endif
    /* The selected physical occurrence leaves live storage before its input
     * is interpreted. It remains owned here across pauses and refusals. */
    CettaCount n = space_length64(a->space);
    if (n > SIZE_MAX) return pending(a,"OccurrenceMask");
    if (!a->contract.explicit_inputs) {
        uint8_t *mask = calloc(n ? (size_t)n : 1u,1u);
        if (!mask) return pending(a,"OccurrenceMask");
        if (a->contract.set_support) {
            for (CettaIndex i=0u;i<n;i++) {
                bool equal=false;
                if(!carrier_equal(a,space_get_at64(a->space,i),a->selected,&equal)) {
                    free(mask);return pending(a,"CompactSetCarrier");
                }
                mask[i]=equal;
            }
        } else mask[a->selected_index] = 1u;
        CettaCount removed = 0u;
        bool taken = space_remove_occurrence_mask_stable(a->space,mask,n,&removed);
        free(mask);
        if (!taken || !removed) return pending(a,"ExecTake");
    }
    a->last_selected = a->selected;
    if (!parse_selected(a)) {
        if (a->contract.snapshot_worlds) {
            a->capture_read=selection_read;
            if (!waiting_add(a)) return pending(a,"WaitingCapacity");
            a->uninterpreted_choice=true;
            AGENDA_OP(transaction_clear)(a);a->selected=NULL;a->phase=AGENDA_SELECT;
            return CETTA_SPACE_AGENDA_PROGRESS;
        }
        a->phase = AGENDA_WAIT; return pending(a,"UninterpretedExec");
    }
    (void)AGENDA_OBSERVE(a,CETTA_AGENDA_EVENT_SELECTED);
    AGENDA_OP(capture_begin)(a,false);
    return CETTA_SPACE_AGENDA_PROGRESS;
}

static CettaSpaceAgendaStep AGENDA_OP(cetta_space_agenda_prepare_source)(
        CettaSpaceAgenda *a, SpaceReadToken source, CettaIndex index) {
    if (!a || !a->contract.explicit_inputs || a->contract.snapshot_worlds ||
        a->contract.set_support || a->contract.raw_mm2_order) return CETTA_SPACE_AGENDA_PENDING;
    if (a->phase!=AGENDA_SELECT || a->selection_ready || a->selected ||
        a->advise || a->owned_next || a->pending_child)
        return pending(a,"PreparationAlreadyStarted");
    CETTA_SCOPED_SHARED_TRANSITION(preparation);
    if (source.space!=a->space || a->space->instance_id!=a->space_identity ||
        !space_read_token_is_current(source))
        return pending(a,"StalePreparationSource");
    Atom *row=space_get_at64(a->space,index);
    if (!shaped(row,a->exec,3u)) return pending(a,"UninterpretedExec");
    a->selected=atom_deep_copy(a->owner,row);
    if (!a->selected) return pending(a,"PreparationCapacity");
    a->selected_index=index;
    return AGENDA_OP(select_publish)(a,source);
}

static CettaSpaceAgendaStep AGENDA_OP(revalidate_choice)(CettaSpaceAgenda *a) {
    if (!a || !a->contract.explicit_inputs || a->contract.snapshot_worlds ||
        a->phase!=AGENDA_BUILD || !a->validating || a->row_count!=1u)
        return CETTA_SPACE_AGENDA_PENDING;
    if (!selected_occurrence_current(a)) return pending(a,"StaleSelectedOccurrence");
    a->validation_count=0u;
    if (!AGENDA_OP(capture_begin)(a,true)) return pending(a,"RevalidationCapacity");
    return CETTA_SPACE_AGENDA_PROGRESS;
}

static CettaSpaceAgendaStep AGENDA_OP(capture_step)(CettaSpaceAgenda *a) {
    if(!selected_occurrence_current(a))return pending(a,"StaleSelectedOccurrence");
    if (!space_read_token_is_current(a->capture_read)) {
        AGENDA_OP(capture_begin)(a,a->validating); a->retries++;
        return CETTA_SPACE_AGENDA_RETRY;
    }
    Space *snapshot = a->borrowed_preparation ? a->space :
        a->validating ? &a->current : &a->snapshot;
    if (!a->borrowed_preparation && a->capture_index < a->capture_length) {
        Atom *row = space_get_at64(a->space,a->capture_index);
        if (!row) return pending(a,"SnapshotProjection");
        AGENDA_CHARGE(a,CETTA_AGENDA_CHARGE_CAPTURE,CETTA_COST_CANDIDATE_ROW,1u);
        bool found=false;
        if(a->contract.set_support && !carrier_contains(a,snapshot,row,&found))
            return pending(a,"CompactSetCarrier");
        bool admitted=(a->contract.set_support && found) || space_admit_atom(snapshot,a->owner,row);
        (void)AGENDA_OBSERVE_EVENT(a,CETTA_AGENDA_EVENT_CAPTURE_ROW,a->capture_index,
            admitted?1u:0u,0u,a->capture_read,NULL,row,NULL);
        if (!admitted) return pending(a,"SnapshotAdmission");
        a->capture_index++;
        return CETTA_SPACE_AGENDA_PROGRESS;
    }
    /* Upstream MM2 matches against live-after-pop with its selected exec
     * restored in the read copy. The live selected occurrence stays absent. */
    if (!a->contract.explicit_inputs) {
        bool found=false;
        if(a->contract.set_support && !carrier_contains(a,snapshot,a->selected,&found))
            return pending(a,"CompactSetCarrier");
        if(!a->contract.set_support || !found) {
            if(!space_admit_atom(snapshot,a->owner,a->selected))return pending(a,"ReadCopyExec");
            if(a->validating)a->current_exec_appended=true;
            else a->snapshot_exec_appended=true;
        }
    }
    (void)AGENDA_OBSERVE(a,CETTA_AGENDA_EVENT_CAPTURE_END);
    if (!join_begin(a)) return pending(a,"JoinCapacity");
    return CETTA_SPACE_AGENDA_PROGRESS;
}

static CettaSpaceAgendaMatch *AGENDA_OP(match_begin)(CettaSpaceAgenda *a, Atom *pattern,
        Atom *row, const Bindings *environment) {
    if (!AGENDA_OBSERVE_EVENT(a,CETTA_AGENDA_EVENT_MATCH_BEGIN,a->depth,0u,0u,
            a->capture_read,pattern,row,environment)) return NULL;
    CettaSpaceAgendaMatch *match=calloc(1u,sizeof(*match));
    if(!match)return NULL;
    bindings_init(&match->captured);bindings_init(&match->result);
    match->isolated=!atom_has_thread_local_resource(pattern) &&
        !atom_has_thread_local_resource(row) && matching_environment_admitted(environment);
    match->execution_arena=a->owner;
    match->pattern=pattern;match->row=row;
    const Bindings *seed=environment;
    if(match->isolated) {
        arena_init(&match->arena);hashcons_init_compact(&match->hashcons);
        arena_set_hashcons(&match->arena,&match->hashcons);
        match->execution_arena=&match->arena;
        match->pattern=atom_deep_copy(&match->arena,pattern);
        match->row=atom_deep_copy(&match->arena,row);
        if(!match->pattern || !match->row ||
           !bindings_clone(&match->captured,environment) ||
           !bindings_promote_atoms_to_arena(&match->captured,&match->arena)) {
            match_free(match);return NULL;
        }
        seed=&match->captured;
        AGENDA_CHARGE(a,CETTA_AGENDA_CHARGE_MATCH_CAPTURE,CETTA_COST_CAPTURE,1u);
    }
    match->cursor=match_atoms_cursor_new(match->pattern,match->row,
        seed,match->execution_arena);
    if(!match->cursor) {
        match_free(match);return NULL;
    }
    return match;
}

static CettaSpaceAgendaMatch *AGENDA_OP(cetta_space_agenda_match_capture)(CettaSpaceAgenda *a) {
    if(!a || (a->phase!=AGENDA_MATCH && a->phase!=AGENDA_VALIDATE_MATCH) ||
       a->depth>=a->input_count || a->pending_child || a->pending)return NULL;
    if(a->matching)
        return a->matching->isolated && !a->matching->prepared ? a->matching : NULL;
    JoinLevel *level=&a->levels[a->depth];
    Atom *pattern=a->inputs[a->depth].pattern,*row=level->pending_row;
    if(!row || atom_has_thread_local_resource(pattern) ||
       atom_has_thread_local_resource(row) || !matching_environment_admitted(&level->environment))return NULL;
    a->matching=AGENDA_OP(match_begin)(a,pattern,row,&level->environment);
    return a->matching;
}

static bool AGENDA_OP(cetta_space_agenda_match_run)(CettaSpaceAgendaMatch *match, uint64_t node_budget) {
    if(!match || match->prepared || !node_budget)return false;
    CettaSpaceAgenda *observed=AGENDA_SCOPE?AGENDA_SCOPE->agenda:NULL;
    if (observed && (observed->matching!=match || !agenda_observation_ready(observed))) {
        agenda_observation_refuse(AGENDA_SCOPE,"ForeignMatchGrant");
        return false;
    }
    HashConsTable *previous=g_hashcons;
    if(match->isolated)g_hashcons=&match->hashcons;
    match->decision=match_atoms_cursor_step(match->cursor,node_budget);
    match->epoch=match_atoms_cursor_epoch(match->cursor);
    g_hashcons=previous;match->prepared=true;
    if (observed) (void)AGENDA_OBSERVE_EVENT(observed,CETTA_AGENDA_EVENT_MATCH_GRANT,
        observed->depth,(uint64_t)match->decision,node_budget,observed->capture_read,NULL,NULL,NULL);
    return true;
}

static CettaMatchCursorStep AGENDA_OP(finish_match)(CettaSpaceAgenda *a, Bindings *result) {
    CettaSpaceAgendaMatch *match=a->matching;
    if(!match->prepared && !AGENDA_OP(cetta_space_agenda_match_run)(match,CETTA_SPACE_AGENDA_MATCH_NODES))
        return CETTA_MATCH_CURSOR_CAPACITY;
    CettaMatchCursorStep decision=match->decision;
    if(decision==CETTA_MATCH_CURSOR_PROGRESS) {
        /* A private quantum may have stopped inside this same candidate.
         * Installing that paid progress cannot execute another node. */
        match->prepared=false;
        return decision;
    }
    if(decision==CETTA_MATCH_CURSOR_INCOMPLETE || decision==CETTA_MATCH_CURSOR_CAPACITY)
        return decision;
    if(decision==CETTA_MATCH_CURSOR_MATCHED) {
        if(!match->result_ready) {
            if(!match_atoms_cursor_take(match->cursor,&match->result))
                return CETTA_MATCH_CURSOR_CAPACITY;
            match->result_ready=true;
        }
        if((match->isolated && !bindings_promote_atoms_to_arena(&match->result,a->owner)) ||
           !match_epoch_record(a,match->epoch))return CETTA_MATCH_CURSOR_CAPACITY;
        (void)AGENDA_OBSERVE_EVENT(a,CETTA_AGENDA_EVENT_MATCH_RESULT,a->depth,
            (uint64_t)decision,0u,a->capture_read,NULL,NULL,&match->result);
        bindings_move(result,&match->result);
    }
    if (decision!=CETTA_MATCH_CURSOR_MATCHED)
        (void)AGENDA_OBSERVE_EVENT(a,CETTA_AGENDA_EVENT_MATCH_RESULT,a->depth,
            (uint64_t)decision,0u,a->capture_read,NULL,NULL,NULL);
    match_free(match);a->matching=NULL;
    return decision;
}

static CettaMatchCursorStep AGENDA_OP(match_step)(CettaSpaceAgenda *a, Atom *pattern,
        Atom *row, const Bindings *base, Bindings *result) {
    if(!a->matching)a->matching=AGENDA_OP(match_begin)(a,pattern,row,base);
    return a->matching ? AGENDA_OP(finish_match)(a,result) : CETTA_MATCH_CURSOR_CAPACITY;
}

static bool AGENDA_OP(row_add)(CettaSpaceAgenda *a) {
    JoinedRow **rows = a->validating ? &a->validation : &a->rows;
    size_t *length = a->validating ? &a->validation_count : &a->row_count;
    size_t *capacity = a->validating ? &a->validation_capacity : &a->row_capacity;
    if (*length == *capacity) {
        size_t next = *capacity ? *capacity * 2u : 8u;
        if (next <= *capacity || next > SIZE_MAX / sizeof(**rows)) return false;
        JoinedRow *grown = realloc(*rows,next*sizeof(**rows));
        if (!grown) return false;
        *rows = grown; *capacity = next;
    }
    Atom **witnesses = a->input_count ? malloc(a->input_count*sizeof(*witnesses)) : NULL;
    Atom **dependencies = a->input_count ? malloc(a->input_count*sizeof(*dependencies)) : NULL;
    if (a->input_count && (!witnesses || !dependencies)) { free(witnesses); free(dependencies); return false; }
    for (size_t i=0u;i<a->input_count;i++) {
        JoinLevel *level=&a->levels[i];
        const char *name = a->inputs[i].kind == INPUT_TAKE ? "take" :
            a->inputs[i].kind == INPUT_ALL ? "all" : a->inputs[i].kind == INPUT_ABSENT ? "absent" : "read";
        witnesses[i]=level->witness_atom ? level->witness_atom : atom_expr(a->owner,NULL,0u);
        dependencies[i]=atom_expr(a->owner,(Atom *[]){atom_symbol(a->owner,name),
            bindings_apply_if_vars(&level->environment,a->owner,a->inputs[i].pattern),
            natural(a->owner,level->witness),witnesses[i]},4u);
    }
    Atom *outputs=bindings_apply_if_vars(&a->levels[a->input_count].environment,a->owner,a->sinks);
    if(!outputs) {free(witnesses);free(dependencies);return false;}
    (*rows)[(*length)++]=(JoinedRow){outputs,
        atom_expr(a->owner,witnesses,(CettaExprLen)a->input_count),
        atom_expr(a->owner,dependencies,(CettaExprLen)a->input_count)};
    free(witnesses); free(dependencies);
    (void)AGENDA_OBSERVE(a,CETTA_AGENDA_EVENT_ROW);
    return outputs != NULL;
}

static bool AGENDA_OP(rows_agree)(CettaSpaceAgenda *a) {
    if(a->row_count!=a->validation_count)return false;
    if(a->row_count>(SIZE_MAX/sizeof(Atom*)-1u)/3u)return false;
    size_t count=1u+3u*a->row_count;
    Atom **left=malloc(count*sizeof(*left)),**right=malloc(count*sizeof(*right));
    if(!left || !right){free(left);free(right);return false;}
    /* The source exec fixes its free variable identities. Every row then
     * participates in one bijective renaming check: fresh matcher epochs
     * may differ, while aliases within and between writes stay correlated.
     * Physical occurrence indices remain rigid entries of dependencies. */
    left[0]=right[0]=bindings_apply_if_vars(&a->captured,a->owner,a->selected);
    for(size_t i=0u;i<a->row_count;i++) {
        AGENDA_CHARGE(a,CETTA_AGENDA_CHARGE_ROW_CHECK,CETTA_COST_COMMIT_CHECK,1u);
        left[3u*i+1u]=a->rows[i].outputs;right[3u*i+1u]=a->validation[i].outputs;
        left[3u*i+2u]=a->rows[i].witnesses;right[3u*i+2u]=a->validation[i].witnesses;
        left[3u*i+3u]=a->rows[i].dependencies;right[3u*i+3u]=a->validation[i].dependencies;
    }
    bool same=left[0] && atom_alpha_eq_scoped(atom_expr(a->owner,left,(CettaExprLen)count),
        atom_expr(a->owner,right,(CettaExprLen)count),
        a->match_epochs[0],a->match_epoch_count[0],a->match_epochs[1],a->match_epoch_count[1]);
    (void)AGENDA_OBSERVE_EVENT(a,CETTA_AGENDA_EVENT_REVALIDATE,0u,same?1u:0u,
        0u,a->capture_read,NULL,NULL,NULL);
    free(left);free(right);return same;
}

static CettaSpaceAgendaStep AGENDA_OP(join_finished)(CettaSpaceAgenda *a) {
    (void)AGENDA_OBSERVE(a,CETTA_AGENDA_EVENT_JOIN_END);
    join_free(a);
    if (a->contract.snapshot_worlds) {
        /* Every binding of this exec was issued separately. The immutable
         * world remains available while the remaining execs are scanned. */
        if (!waiting_add(a)) return pending(a,"WaitingCapacity");
        AGENDA_OP(transaction_clear)(a);a->selected=NULL;a->phase=AGENDA_SELECT;
        return CETTA_SPACE_AGENDA_PROGRESS;
    }
    if(a->contract.explicit_inputs && !a->validating && !a->row_count) {
        if(!space_read_token_is_current(a->capture_read)) {
            AGENDA_OP(capture_begin)(a,false);a->retries++;return CETTA_SPACE_AGENDA_RETRY;
        }
        if(!waiting_add(a))return pending(a,"WaitingCapacity");
        AGENDA_OP(transaction_clear)(a);a->selected=NULL;a->phase=AGENDA_SELECT;
        return CETTA_SPACE_AGENDA_PROGRESS;
    }
    if(!a->validating) {AGENDA_OP(capture_begin)(a,true);return CETTA_SPACE_AGENDA_PROGRESS;}
    if(!space_read_token_is_current(a->capture_read)) {
        a->validation_count=0u;AGENDA_OP(capture_begin)(a,true);a->retries++;
        return CETTA_SPACE_AGENDA_RETRY;
    }
    if(!AGENDA_OP(rows_agree)(a)) {
        /* New matching facts, missing witnesses and competing consumes do
         * not authorize the old proposed writes. Keep the selected exec and
         * rebuild only its transaction against the new source. */
        a->row_count=a->validation_count=0u;a->retries++;
        AGENDA_OP(capture_begin)(a,false);return CETTA_SPACE_AGENDA_RETRY;
    }
    if(a->replacement_ready)space_free(&a->replacement);
    memset(&a->replacement,0,sizeof(a->replacement));
    space_init_with_universe(&a->replacement,a->space->universe);a->replacement_ready=true;
    a->build_index=0u;a->phase=AGENDA_BUILD;
    return CETTA_SPACE_AGENDA_PROGRESS;
}

static bool AGENDA_OP(world_child_new)(CettaSpaceAgenda *a, const JoinedRow *row) {
    CettaSpaceAgenda *root=a->root;
    if (a->pending_child || !root->next_world_id || root->next_world_id==UINT64_MAX)
        return false;
    CettaSpaceAgenda *child=cetta_space_agenda_new(a->space,a->owner,&a->captured,a->contract);
    if (!child) return false;
    child->rows=malloc(sizeof(*child->rows));
    child->validation=malloc(sizeof(*child->validation));
    if (!child->rows || !child->validation) {AGENDA_OP(agenda_free_one)(child);return false;}
    child->rows[0]=*row;child->validation[0]=*row;
    child->row_count=child->row_capacity=child->validation_count=child->validation_capacity=1u;
    child->selected=a->selected;child->last_selected=a->selected;
    child->selected_index=a->selected_index;child->selected_read=a->selected_read;
    child->capture_read=space_read_token(a->space);
    child->schedule=a->schedule;child->advise=a->advise;child->before=a->before;
    child->advice_context=a->advice_context;child->chosen_advice=a->chosen_advice;
    child->root=root;child->origin_read=root->origin_read;
    child->world_id=root->next_world_id++;
    child->parent_world_id=a->world_id;
    child->trace=a->trace;child->commits=a->commits;
    space_init_with_universe(&child->replacement,a->space->universe);
    child->replacement_ready=true;child->build_index=0u;child->phase=AGENDA_BUILD;
    child->owned_next=root->owned_next;root->owned_next=child;
    a->pending_child=child;a->yielded_child=true;
    cetta_native_cost_charge(CETTA_COST_CAPTURE,1u);
    return true;
}

static CettaSpaceAgendaStep AGENDA_OP(join_step)(CettaSpaceAgenda *a) {
    if(a->depth==a->input_count) {
        if(!AGENDA_OP(row_add)(a))return pending(a,"JoinedRowCapacity");
        if (a->contract.snapshot_worlds) {
            if (!AGENDA_OP(world_child_new)(a,&a->rows[a->row_count-1u])) {
                a->row_count--;
                return pending(a,"WorldContinuationCapacity");
            }
            /* The row has moved into its own store/commit continuation. The
             * matcher keeps the next physical binding, without deduplication. */
            a->row_count--;
            if (a->depth) {level_free(&a->levels[a->depth]);a->depth--;}
            else {AGENDA_OP(join_finished)(a);}
            return CETTA_SPACE_AGENDA_FORK;
        }
        /* Annotated transactions choose one complete environment. MM2
         * applies its sinks to every environment in the frozen read copy. */
        if(a->contract.explicit_inputs || !a->depth)return AGENDA_OP(join_finished)(a);
        level_free(&a->levels[a->depth]);a->depth--;return CETTA_SPACE_AGENDA_PROGRESS;
    }
    JoinLevel *level=&a->levels[a->depth];
    AgendaInput *input=&a->inputs[a->depth];
    Space *snapshot=a->borrowed_preparation?a->space:
        a->validating?&a->current:&a->snapshot;
    if(level->aggregate_done) {
        if(!a->depth)return AGENDA_OP(join_finished)(a);
        level_free(level);a->depth--;return CETTA_SPACE_AGENDA_PROGRESS;
    }
    if(!level->cursor_ready && !level->exhausted) {
        Atom *pattern=bindings_apply_if_vars(&level->environment,a->owner,input->pattern);
        if(!pattern || !space_occurrence_cursor_init(snapshot,pattern,&level->cursor))
            return pending(a,"MatchCursor");
        level->cursor_ready=true;
        (void)AGENDA_OBSERVE_EVENT(a,CETTA_AGENDA_EVENT_QUERY_BEGIN,a->depth,
            (uint64_t)input->kind,0u,level->cursor.read,pattern,NULL,&level->environment);
    }
    CettaIndex index=level->pending_index;
    SpaceOccurrenceCursorStep step=level->exhausted ? SPACE_OCCURRENCE_CURSOR_END :
        level->pending_row ? SPACE_OCCURRENCE_CURSOR_ITEM :
        space_occurrence_cursor_next(&level->cursor,&index);
    if(step==SPACE_OCCURRENCE_CURSOR_INVALIDATED)return pending(a,"CapturedCursor");
    if(step==SPACE_OCCURRENCE_CURSOR_END) {
        (void)AGENDA_OBSERVE_EVENT(a,CETTA_AGENDA_EVENT_QUERY_END,a->depth,
            (uint64_t)input->kind,0u,level->cursor_ready?level->cursor.read:(SpaceReadToken){0},
            input->pattern,NULL,&level->environment);
        if(level->cursor_ready)space_occurrence_cursor_release(&level->cursor);
        level->cursor_ready=false;level->exhausted=true;
        if(input->kind==INPUT_ALL || input->kind==INPUT_ABSENT) {
            if(input->kind==INPUT_ABSENT && level->aggregate_length) {
                level->aggregate_done=true;return CETTA_SPACE_AGENDA_PROGRESS;
            }
            if(!level->aggregate_value)
                level->aggregate_value=atom_expr(a->owner,level->aggregate,
                    (CettaExprLen)level->aggregate_length);
            Atom *all=level->aggregate_value;
            if(!all)return pending(a,"AggregateValue");
            level->witness=UINT64_MAX;level->witness_atom=all;
            Bindings next;
            if(input->kind==INPUT_ALL) {
                CettaMatchCursorStep match=AGENDA_OP(match_step)(a,input->collection,all,
                    &level->environment,&next);
                if(match==CETTA_MATCH_CURSOR_PROGRESS)return CETTA_SPACE_AGENDA_PROGRESS;
                if(match==CETTA_MATCH_CURSOR_INCOMPLETE)return pending(a,"AggregateBinding");
                if(match==CETTA_MATCH_CURSOR_CAPACITY)return pending(a,"AggregateBindingCapacity");
                if(match==CETTA_MATCH_CURSOR_REFUTED) {
                    level->aggregate_done=true;return CETTA_SPACE_AGENDA_PROGRESS;
                }
            } else if(!bindings_clone(&next,&level->environment))return pending(a,"AggregateBindings");
            level->aggregate_done=true;
            advance_level(a,&next);return CETTA_SPACE_AGENDA_PROGRESS;
        }
        if(!a->depth)return AGENDA_OP(join_finished)(a);
        level_free(level);a->depth--;return CETTA_SPACE_AGENDA_PROGRESS;
    }
    Atom *row=level->pending_row ? level->pending_row :
        space_occurrence_cursor_atom(&level->cursor,index);
    if(!row)return pending(a,"MatchProjection");
    if(!level->pending_row) {
        AGENDA_CHARGE(a,CETTA_AGENDA_CHARGE_QUERY,CETTA_COST_CANDIDATE_ROW,1u);
        (void)AGENDA_OBSERVE_EVENT(a,CETTA_AGENDA_EVENT_QUERY_ROW,index,
            (uint64_t)input->kind,0u,level->cursor.read,input->pattern,row,&level->environment);
    }
    if(input->kind==INPUT_TAKE) {
        /* The selected exec is in the private read copy, not live supply. */
        if(a->contract.explicit_inputs ? index==a->selected_index :
            index+1u==space_length64(snapshot))return CETTA_SPACE_AGENDA_PROGRESS;
        for(size_t i=0u;i<a->depth;i++)
            if(a->inputs[i].kind==INPUT_TAKE && a->levels[i].witness==index)
                return CETTA_SPACE_AGENDA_PROGRESS;
    }
    if(!level->pending_row) {
        /* Discovery and matching are separately retained native operations.
         * The row's physical cursor position and all captured bindings stay
         * with this continuation across a worker episode or a pause. */
        level->pending_row=row;level->pending_index=index;
        return CETTA_SPACE_AGENDA_PROGRESS;
    }
    Bindings next;
    CettaMatchCursorStep matched=AGENDA_OP(match_step)(a,input->pattern,row,&level->environment,&next);
    if(matched==CETTA_MATCH_CURSOR_PROGRESS)return CETTA_SPACE_AGENDA_PROGRESS;
    if(matched==CETTA_MATCH_CURSOR_INCOMPLETE || matched==CETTA_MATCH_CURSOR_CAPACITY) {
        level->pending_row=row;level->pending_index=index;
        return pending(a,matched==CETTA_MATCH_CURSOR_CAPACITY ? "MatcherCapacity" : "MatcherIncomplete");
    }
    level->pending_row=NULL;
    if(matched==CETTA_MATCH_CURSOR_REFUTED)return CETTA_SPACE_AGENDA_PROGRESS;
    if(input->kind==INPUT_ALL || input->kind==INPUT_ABSENT) {
        Atom *value=bindings_apply_if_vars(&next,a->owner,row);
        bindings_free(&next);
        if(!value || !aggregate_add(level,value))return pending(a,"AggregateCapacity");
        (void)AGENDA_OBSERVE_EVENT(a,CETTA_AGENDA_EVENT_AGGREGATE,index,
            level->aggregate_length,0u,a->capture_read,input->pattern,value,&level->environment);
        return CETTA_SPACE_AGENDA_PROGRESS;
    }
    level->witness=index;level->witness_atom=row;
    advance_level(a,&next);return CETTA_SPACE_AGENDA_PROGRESS;
}

static CettaSpaceAgendaStep AGENDA_OP(build_step)(CettaSpaceAgenda *a) {
    if(!selected_occurrence_current(a))return pending(a,"StaleSelectedOccurrence");
    if(!space_read_token_is_current(a->capture_read)) {
        a->validation_count=0u;AGENDA_OP(capture_begin)(a,true);a->retries++;
        return CETTA_SPACE_AGENDA_RETRY;
    }
    /* Reinsertion is a set union under MM2. A concurrently introduced equal
     * exec is already live and must not be removed as a private read copy. */
    Space *current=a->contract.snapshot_worlds?a->space:&a->current;
    size_t n=(size_t)space_length64(current)-(a->current_exec_appended?1u:0u);
    if(a->build_index<n) {
        size_t index=a->build_index;
        Atom *row=space_get_at64(current,index);
        if(a->contract.explicit_inputs && index==a->selected_index) {
            if(!atom_eq(row,a->selected))return pending(a,"SelectedOccurrenceChanged");
            a->build_index++;return CETTA_SPACE_AGENDA_PROGRESS;
        }
        if(a->contract.explicit_inputs && a->validation_count) {
            Atom *deps=a->validation[0].dependencies;
            for(CettaExprIndex i=0u;i<deps->expr.len;i++) {
                Atom *dep=deps->expr.elems[i];
                if(atom_is_symbol_id(dep->expr.elems[0],a->take) &&
                   dep->expr.elems[2]->kind==ATOM_GROUNDED &&
                   dep->expr.elems[2]->ground.gkind==GV_INT &&
                   dep->expr.elems[2]->ground.ival>=0 &&
                   (uint64_t)dep->expr.elems[2]->ground.ival==index) {
                    a->build_index++;return CETTA_SPACE_AGENDA_PROGRESS;
                }
            }
        }
        if(!row || !space_admit_atom(&a->replacement,a->owner,row))return pending(a,"CommitPreparation");
        a->build_index++;
        return CETTA_SPACE_AGENDA_PROGRESS;
    }
    a->sink_row=0u;a->sink_index=1u;a->phase=AGENDA_SINK;
    return CETTA_SPACE_AGENDA_PROGRESS;
}

static CettaSpaceAgendaStep AGENDA_OP(commit_step)(CettaSpaceAgenda *a) {
    Atom **deps=a->row_count?malloc(a->row_count*sizeof(*deps)):NULL;
    if(a->row_count && !deps)return pending(a,"CommitReceiptCapacity");
    for(size_t i=0u;i<a->row_count;i++)deps[i]=a->rows[i].dependencies;
    CETTA_SCOPED_SHARED_TRANSITION(publication);
    AGENDA_CHARGE(a,CETTA_AGENDA_CHARGE_COMMIT,CETTA_COST_COMMIT_CHECK,1u);
    if(!selected_occurrence_current(a)) {
        free(deps);return pending(a,"StaleSelectedOccurrence");
    }
    if(!space_read_token_is_current(a->capture_read)) {
        free(deps);
        a->validation_count=0u;AGENDA_OP(capture_begin)(a,true);a->retries++;
        return CETTA_SPACE_AGENDA_RETRY;
    }
    if (!AGENDA_PERMIT(a)) {
        free(deps); return pending(a,"ProtectedPrecommit");
    }
    /* A normally returning observer does not replace the provider's actual
     * current-match authority. Check again immediately before publication. */
    if (AGENDA_SCOPE && (!selected_occurrence_current(a) ||
            !space_read_token_is_current(a->capture_read))) {
        agenda_observation_refuse(AGENDA_SCOPE,"PrecommitAuthorityChanged");
        free(deps); return pending(a,"ProtectedPrecommitChanged");
    }
    if (a->contract.snapshot_worlds) {
        space_replace_contents(&a->world,&a->replacement);
        a->space=&a->world;a->space_identity=a->world.instance_id;
    } else space_replace_contents(a->space,&a->replacement);
    a->commits++;
    AGENDA_COMMITTED(a);
    Atom *fields[10]={atom_symbol(a->owner,a->contract.snapshot_worlds?"WorldStep":"Committed"),code_value(a,a->selected),
        atom_expr2(a->owner,atom_symbol(a->owner,"environments"),natural(a->owner,a->row_count)),
        atom_expr2(a->owner,atom_symbol(a->owner,"dependencies"),code_value(a,atom_expr(a->owner,deps,deps?(CettaExprLen)a->row_count:0u))),
        atom_expr2(a->owner,atom_symbol(a->owner,"retries"),natural(a->owner,a->retries)),
        atom_expr2(a->owner,atom_symbol(a->owner,"store"),atom_symbol(a->owner,a->contract.set_support?"set":"bag")),
        atom_expr2(a->owner,atom_symbol(a->owner,"order"),atom_symbol(a->owner,
            a->advise?"program-advice":a->contract.raw_mm2_order?"compact-byte":"source-order"))};
    if (a->contract.snapshot_worlds) {
        fields[7]=atom_expr2(a->owner,atom_symbol(a->owner,"from"),natural(a->owner,a->parent_world_id));
        fields[8]=atom_expr2(a->owner,atom_symbol(a->owner,"to"),natural(a->owner,a->world_id));
        fields[9]=atom_expr2(a->owner,atom_symbol(a->owner,"selected-occurrence"),natural(a->owner,a->selected_index));
    }
    a->receipt=atom_expr(a->owner,fields,a->contract.snapshot_worlds?10u:7u);
    if (a->contract.snapshot_worlds) {
        /* The ordered persistent spine is a trace, not a commutative sum.
         * Its ancestor is shared once among descendants. */
        a->trace=atom_expr3(a->owner,atom_symbol(a->owner,"then"),a->trace,a->receipt);
        cetta_native_cost_charge(CETTA_COST_RECEIPT_APPEND,1u);
    }
    (void)AGENDA_OBSERVE(a,CETTA_AGENDA_EVENT_COMMITTED);
    free(deps);
    AGENDA_OP(transaction_clear)(a);a->selected=NULL;a->phase=AGENDA_SELECT;
    return a->contract.snapshot_worlds?CETTA_SPACE_AGENDA_PROGRESS:CETTA_SPACE_AGENDA_COMMITTED;
}

static CettaSpaceAgendaStep AGENDA_OP(agenda_step_core)(CettaSpaceAgenda *a) {
    if(!a || a->space->instance_id!=a->space_identity)return CETTA_SPACE_AGENDA_PENDING;
    CETTA_SCOPED_SHARED_TRANSITION(observation);
    if (!cetta_space_agenda_current(a))return pending(a,"StaleWorldSource");
    a->pending=NULL;
    if (a->pending_child) return CETTA_SPACE_AGENDA_FORK;
    switch(a->phase) {
    case AGENDA_SELECT:return AGENDA_OP(select_step)(a);
    case AGENDA_RANK:return AGENDA_OP(rank_step)(a);
    case AGENDA_ACTIVATE:
        if (!space_read_token_is_current(a->selection_read))
            return pending(a,"StaleActivationSource");
        return AGENDA_OP(select_publish)(a,a->selection_read);
    case AGENDA_CAPTURE:case AGENDA_VALIDATE_CAPTURE:return AGENDA_OP(capture_step)(a);
    case AGENDA_MATCH:case AGENDA_VALIDATE_MATCH:return AGENDA_OP(join_step)(a);
    case AGENDA_BUILD:return AGENDA_OP(build_step)(a);
    case AGENDA_SINK:return sink_step(a);
    case AGENDA_COMMIT:return AGENDA_OP(commit_step)(a);
    case AGENDA_WAIT:return pending(a,"UninterpretedExec");
    case AGENDA_WORLD_CAPTURE:return world_capture_step(a);
    case AGENDA_WORLD_REPORT:return world_report_step(a);
    case AGENDA_DONE:return CETTA_SPACE_AGENDA_CLOSED;
    }
    return pending(a,"ProviderState");
}

static CettaSpaceAgendaStep AGENDA_OP(cetta_space_agenda_step)(CettaSpaceAgenda *a) {
    if (!AGENDA_SCOPE) return AGENDA_OP(agenda_step_core)(a);
    if (!a || !agenda_observation_ready(a)) return CETTA_SPACE_AGENDA_PENDING;
    if (!AGENDA_OBSERVE(a,CETTA_AGENDA_EVENT_STEP_BEGIN))
        return pending(a,"ObservationAdmission");
    CettaSpaceAgendaStep result=AGENDA_OP(agenda_step_core)(a);
    (void)AGENDA_OBSERVE_EVENT(a,CETTA_AGENDA_EVENT_STEP_RESULT,0u,
        (uint64_t)result,0u,(SpaceReadToken){0},NULL,NULL,NULL);
    /* COMMITTED remains a fact after a failed postpublication read. The
     * scope retains that failure and denies every later producer entry. */
    return result;
}

#undef AGENDA_COMMITTED
#undef AGENDA_PERMIT
#undef AGENDA_CHARGE
#undef AGENDA_OBSERVE_EVENT
#undef AGENDA_OBSERVE
#undef AGENDA_SCOPE
#undef AGENDA_OP
#undef AGENDA_JOIN
#undef AGENDA_JOIN_
