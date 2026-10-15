/* The existing relational machine supplies its actual owned images. The
 * agenda supplies selection and demand, without changing their restore
 * authority or reconstructing a continuation from its displayed value. */
typedef struct NativeDialectOwner NativeDialectOwner;
typedef struct NativeDialectCandidate {
    struct NativeDialectCandidate *next;
    uint64_t occurrence;
    Atom *body;
    Bindings environment;
    PrimeNativeAdvice *advice;
} NativeDialectCandidate;

struct NativeDialectWork {
    NativeDialectOwner *machine;
    CettaOwnedContinuation image;
    CettaContinuationBatch pending;
    OutcomeSet *target;
    bool pure;
    bool operation_pending;
    bool operation_raised;
    PettaMachineStep operation_step;
    Atom *operation_answer;
    Bindings operation_environment;
    CettaBranchAuthorityToken operation_authority;
    SpaceReadToken operation_source;
    uint64_t operation_candidate;
    Atom *source_value;
    Bindings source_environment;
    bool source_return_pending;
    bool source_return_is_call;
};

struct NativeDialectOwner {
    PettaEvalMachineContext context;
    PettaMachine machine;
    PrimeNativeControl *control;
    NativeDialectWork *live;
    size_t references;
    bool portable;
    Atom *source_expression;
    NativeDialectCandidate *candidates;
    PettaProgramRevisionProjection projection;
    PettaProgramRevisionView *projection_lease;
    PrimeNativeParallel *storage_owner;
};

static void native_dialect_work_free(NativeDialectWork *work) {
    if (!work) return;
    NativeDialectOwner *owner = work->machine;
    cetta_continuation_batch_destroy(&work->pending);
    cetta_owned_continuation_destroy(&work->image);
    if (work->operation_pending)
        bindings_free(&work->operation_environment);
    if (work->source_return_pending)
        bindings_free(&work->source_environment);
    if (owner) {
        if (owner->live == work) owner->live = NULL;
        assert(owner->references > 0u);
        if (--owner->references == 0u) {
            petta_machine_destroy(&owner->machine);
            petta_program_revision_view_free(owner->projection_lease);
            NativeDialectCandidate *candidate = owner->candidates;
            while (candidate) {
                NativeDialectCandidate *next = candidate->next;
                bindings_free(&candidate->environment);
                free(candidate);
                candidate = next;
            }
            prepared_pure_program_cache_free(&owner->context.prepared_pure_cache);
#if CETTA_BUILD_WITH_PETTA_TYPECHECK_V2
            free(owner->context.analysis_state);
#endif
            PrimeNativeParallel *storage_owner = owner->storage_owner;
            free(owner);
            prime_native_parallel_retire(storage_owner);
        }
    }
    free(work);
}

static bool native_dialect_work_pure(const NativeDialectWork *work) {
    return work && work->pure;
}

static bool native_dialect_operation_pending(const NativeDialectWork *work) {
    return work && work->operation_pending;
}

static OutcomeSet *native_dialect_work_target(const NativeDialectWork *work) {
    return work ? work->target : NULL;
}

static bool native_dialect_admit_transfer(void *destination, const void *source,
                                         Space *space) {
    const NativeDialectOwner *to = destination, *from = source;
    const PettaEvalMachineContext *a = &to->context, *b = &from->context;
    return to != from && space && to->portable == from->portable &&
        a->library_context == b->library_context && a->fuel == b->fuel &&
        a->data_ops == b->data_ops && a->typecheck_ops == b->typecheck_ops &&
        a->semantic_authority_epoch == b->semantic_authority_epoch &&
        !a->transaction && !b->transaction &&
        a->controller_admitted && b->controller_admitted &&
        a->diagnostic_transition_limit == b->diagnostic_transition_limit &&
        a->program_projection == NULL && b->program_projection == NULL;
}

static bool native_dialect_permit_operation(void *raw, Space *space,
    PettaMachineOperation operation, const PettaMachineOperationView *view) {
    NativeDialectOwner *owner = raw;
    PrimeNativeControl *control = owner->control;
    Atom *expression = view ? view->expression : NULL;
    bool root_ready = true;
    if (control->worker_execution && operation == PETTA_MACHINE_OPERATION_CALL)
        root_ready = petta_machine_operation_view_root(view, &expression) ==
            CETTA_GSLT_TERM_VIEW_OK_V1;
    if (operation == PETTA_MACHINE_OPERATION_CALL &&
        root_ready &&
        !prime_eval_check_call_authority(space, control->driver.arena, expression))
        return false;
    if (!control->worker_execution) return true;
    bool allowed = operation == PETTA_MACHINE_OPERATION_LOCAL;
    if (operation == PETTA_MACHINE_OPERATION_CALL && root_ready && expression) {
        bool fixed_head = expression->kind != ATOM_VAR &&
            (expression->kind != ATOM_EXPR || !expression->expr.len ||
             expression->expr.elems[0]->kind == ATOM_SYMBOL);
        allowed = fixed_head && prime_native_worker_call_local(
            space, control->driver.arena, expression);
    }
    if (allowed) return true;
    if (!control->effect_pending)
        prime_native_effect_receipt(control, "PendingDialectOperation",
            expression ? expression : view && view->expression
                ? view->expression : owner->source_expression);
    control->effect_pending = true;
    return false;
}

static void native_dialect_observe_space_read(void *raw, SpaceReadToken read) {
    NativeDialectOwner *owner = raw;
    prime_native_observe_enclosing_space_read((Space *)read.space);
    if (!prime_native_control_record_query_read(owner->control, read) &&
        !owner->control->invalidated) owner->control->driver.capacity_blocked = true;
}

static CettaBranchAdmission native_dialect_admit_capture(void *raw, Space *space) {
    NativeDialectOwner *owner = raw;
    CettaBranchAdmission admission = petta_eval_machine_admit_branch_capture(
        &owner->context, space);
    if (admission.status != CETTA_BRANCH_ADMISSION_AVAILABLE) return admission;
    if (owner->control->invalidated ||
        !prime_native_control_query_reads_current(owner->control)) {
        admission.status = CETTA_BRANCH_ADMISSION_INVALIDATED;
        return admission;
    }
    /* Native queries retain their actual Space read tokens, including a
     * completed match whose bindings outlive its cursor. A mutation of a
     * different resource is not a change of this image's read authority.
     * Root, foreign service, registry and translator authority stay pinned. */
    admission.authority.words[6] = UINT64_C(0x4e41544956455244);
    return admission;
}

static bool native_dialect_capture_resource(void *raw, const Atom *atom,
                                           PettaMachineResourceLease *lease) {
    NativeDialectOwner *owner = raw;
    if (!owner || !atom || atom->kind != ATOM_GROUNDED || !atom->ground.ptr ||
        (atom->ground.gkind != GV_SPACE && atom->ground.gkind != GV_STATE &&
         atom->ground.gkind != GV_CAPTURE)) return false;
    PrimeNativeParallel *parallel = owner->storage_owner
        ? owner->storage_owner : owner->control->parallel_owner;
    if (parallel && (arena_owns_ptr(&parallel->world.persistent_arena, atom->ground.ptr) ||
            arena_owns_ptr(&parallel->world.eval_arena, atom->ground.ptr) ||
            (atom->ground.gkind == GV_SPACE &&
             hyperpose_branch_owns_space(&parallel->world, atom->ground.ptr)))) {
        *lease = (PettaMachineResourceLease){
            .scope=owner->context.library_context, .owner=parallel,
            .retain=prime_native_parallel_retain,
            .release=prime_native_parallel_release,
            .storage=prime_native_parallel_owner_storage};
        return true;
    }
    /* Session resources are borrowed, never placed in a worker's destruction
     * list. Native handle shutdown ends every image before session resources
     * and their persistent arena are destroyed. */
    const CettaLibraryContext *library = owner->context.library_context;
    Arena *persistent = library ? library->term_universe.persistent_arena : NULL;
    if ((persistent && arena_owns_ptr(persistent, atom->ground.ptr)) ||
        (atom->ground.gkind == GV_SPACE && atom->ground.ptr == owner->control->space)) {
        *lease = (PettaMachineResourceLease){.scope=library};
        return library != NULL;
    }
    return false;
}

static void native_dialect_owner_storage(const void *raw, CettaStorageCensus *census) {
    const NativeDialectOwner *owner = raw;
    if (!owner || !cetta_storage_object(census, owner, sizeof(*owner), CETTA_STORAGE_BLOCK)) return;
    petta_eval_machine_context_storage(&owner->context, census);
    petta_machine_storage_census(&owner->machine, census);
    petta_program_revision_view_storage_census(owner->projection_lease, census);
    prime_native_parallel_owner_storage(owner->storage_owner, census);
    for (const NativeDialectCandidate *candidate = owner->candidates; candidate; candidate = candidate->next) {
        if (!cetta_storage_object(census, candidate, sizeof(*candidate), CETTA_STORAGE_BLOCK)) break;
        bindings_storage_census(&candidate->environment, census);
    }
}

static void native_dialect_work_storage(const NativeDialectWork *work,
                                        CettaStorageCensus *census) {
    if (!work || !cetta_storage_object(census,work,sizeof(*work),CETTA_STORAGE_BLOCK))
        return;
    cetta_storage_schedule(census, work->machine, native_dialect_owner_storage);
    cetta_owned_continuation_storage_census(&work->image, census);
    cetta_storage_array(census, work->pending.items, work->pending.length,
        sizeof(*work->pending.items), 0u);
    for (size_t i = 0u; i < work->pending.length; i++)
        cetta_owned_continuation_storage_census(&work->pending.items[i], census);
    if (work->operation_pending) bindings_storage_census(&work->operation_environment, census);
    if (work->source_return_pending) bindings_storage_census(&work->source_environment, census);
}

static bool native_dialect_admits(Space *space, Atom *expression) {
    if (!space || !expression || !g_library_context)
        return false;
    CettaLanguageId language = eval_current_language_id();
    if (language != CETTA_LANGUAGE_HE && language != CETTA_LANGUAGE_PETTA)
        return false;
    /* Ordinary HE evaluation does not need an occurrence program. Allocate
     * its existing relational view only at this explicit control boundary;
     * the session owns and retires it in the usual way. */
    if (!g_library_context->petta_program) {
        g_library_context->petta_program =
            petta_program_new_with_host_intrinsics(
                native_control_profile_active() ? eval_native_weight_head : NULL);
        if (!g_library_context->petta_program) return false;
        if (native_control_profile_active())
            petta_program_enable_graded_equations(g_library_context->petta_program);
        if (cetta_profile_uses_petta_typing(g_library_context->session.profile))
            (void)petta_program_enable_analysis(g_library_context->petta_program);
    }
    if (native_weight_form(expression) ||
        (native_control_profile_active() && space_match_where_grade(expression))) return true;
    /* Match already has a held-pattern/template continuation in this same
     * machine. Its return is interpreted by the active host, including HE's
     * expected result contract. */
    if (atom_head_symbol_id(expression)==g_builtin_syms.match &&
        expression->expr.len==4u) return true;
    SymbolId head=atom_head_symbol_id(expression);
    if (language==CETTA_LANGUAGE_HE &&
        ((head==g_builtin_syms.let && expression->expr.len==4u) ||
         (head==g_builtin_syms.if_text &&
          (expression->expr.len==3u || expression->expr.len==4u)))) return true;
    /* The retained HE interpreter owns argument demand and correlated type
     * alternatives. Actual operations rendezvous through permit_operation;
     * whole-future pure-call admission is not its semantic boundary. */
    if (language == CETTA_LANGUAGE_HE)
        return petta_program_synchronize_space(g_library_context->petta_program,space);
    return true;
}

static bool native_dialect_commit_batch(PrimeNativeWork *work) {
    NativeDialectWork *job = work->dialect;
    size_t length = job->pending.length;
    if (!cetta_continuation_hub_append_batch(&work->owner->hub,&job->pending))
        return false;
    if (work->parent) work->parent->control_pending += length;
    job->machine->live = NULL;
    return true;
}

static bool native_dialect_branch_images(PrimeNativeWork *work,
                                        CettaContinuationBatch *images) {
    NativeDialectWork *job = work->dialect;
    CettaContinuationBatch wrappers = {0};
    if (images->length > SIZE_MAX / sizeof(*wrappers.items)) return false;
    if (images->length > SIZE_MAX - job->machine->references) return false;
    if (work->parent && images->length > SIZE_MAX - work->parent->control_pending)
        return false;
    wrappers.items = calloc(images->length ? images->length : 1u,sizeof(*wrappers.items));
    if (!wrappers.items) return false;
    for (size_t i = 0u; i < images->length; ++i) {
        PrimeNativeWork *branch = calloc(1u,sizeof(*branch));
        NativeDialectWork *child = calloc(1u,sizeof(*child));
        if (!branch || !child) {
            free(branch);free(child);
            cetta_continuation_batch_destroy(&wrappers);
            return false;
        }
        *branch = (PrimeNativeWork){.owner=work->owner,.parent=work->parent,
            .key=work->key,.bound=work->bound,.advice=work->advice,
            .scoring=work->scoring,.dialect=child};
        *child = (NativeDialectWork){.machine=job->machine,
            .target=job->target,.pure=job->pure};
        job->machine->references++;
        child->image = images->items[i];
        memset(&images->items[i],0,sizeof(images->items[i]));
        wrappers.items[wrappers.length++] = (CettaOwnedContinuation){
            .payload=branch,.provider=&prime_native_work_provider,
            .parent_occurrence_id=work->owner->active_occurrence};
    }
    cetta_continuation_batch_destroy(images);
    job->pending = wrappers;
    return true;
}

static uint64_t native_dialect_begin_call(void *raw, SpaceReadToken read, Atom *query) {
    (void)raw; (void)read; (void)query;
    return prime_need_fresh_source_occurrence();
}

static bool native_dialect_observes_body(void *raw) {
    (void)raw;
    return true;
}

static bool native_dialect_record_candidate(void *raw, Arena *arena, uint64_t call,
    const PettaEquationCandidate *row, Atom *result,
    const Bindings *environment, Bindings *delta) {
    (void)arena;
    NativeDialectOwner *owner = raw;
    if (!call || !row || !row->equation || !result || !environment || !delta)
        return false;
    if (!bindings_refresh_occurrence_token(delta)) return false;
    NativeDialectCandidate *candidate = calloc(1u,sizeof(*candidate));
    if (!candidate) return false;
    Arena *stable = owner->control->driver.arena;
    if (!bindings_clone(&candidate->environment,environment)) {free(candidate);return false;}
    candidate->body = bindings_apply_if_vars(environment,stable,result);
    if (!candidate->body || !bindings_promote_atoms_to_arena(&candidate->environment,stable)) {
        bindings_free(&candidate->environment);free(candidate);return false;
    }
    candidate->body = atom_deep_copy(stable,candidate->body);
    if (!candidate->body) {bindings_free(&candidate->environment);free(candidate);return false;}
    candidate->occurrence = bindings_occurrence_token(delta);
    candidate->next = owner->candidates;
    owner->candidates = candidate;
    cetta_native_cost_charge(CETTA_COST_EQUATION_ACTIVATION,1u);
    return true;
}

/* The shared binding merge may join two distinct occurrence authorities
 * under a fresh identity. Advice follows that resulting branch occurrence,
 * while its candidate still retains the associated body and bindings. */
static void native_dialect_equation_use_merged(void *raw,
    const Bindings *delta, const Bindings *merged) {
    NativeDialectOwner *owner=raw;
    uint64_t before=bindings_occurrence_token(delta);
    uint64_t after=bindings_occurrence_token(merged);
    assert(before && after);
    for (NativeDialectCandidate *candidate=owner->candidates;candidate;candidate=candidate->next)
        if (candidate->occurrence == before) {
            candidate->occurrence=after;
            return;
        }
    assert(false);
}

/* Capturing an equation image only records its immutable candidate. Pure
 * advice is started when that actual image is selected, before its RHS runs. */
static void native_dialect_attach_candidate_at(PrimeNativeWork *work, uint64_t occurrence) {
    NativeDialectOwner *owner = work->dialect->machine;
    if (!occurrence || work->scoring) return;
    for (NativeDialectCandidate *candidate=owner->candidates;candidate;candidate=candidate->next) {
        if (candidate->occurrence != occurrence) continue;
        PrimeNativeControl *control=work->owner;
        /* A return may revisit an older activation's environment. Its
         * advice was already inherited by the caller; do not overwrite
         * the advice carried back by a more recent source occurrence. */
        if (candidate->advice && work->advice) break;
        if (!candidate->advice) {
            PrimeEvalStackFrame publication={.space=control->space,
                .arena=control->driver.arena,.target=work->dialect->target};
            Bindings captured;
            if (!bindings_clone(&captured, &candidate->environment) ||
                !bindings_promote_atoms_to_arena(&captured, control->driver.arena)) {
                bindings_free(&captured);
                control->driver.capacity_blocked = true;
                return;
            }
            Atom *body = atom_deep_copy(control->driver.arena, candidate->body);
            if (!body) {
                bindings_free(&captured);
                control->driver.capacity_blocked = true;
                return;
            }
            (void)prime_native_control_advice_candidate(&publication,body,NULL,
                &captured,owner->context.fuel);
            bindings_free(&captured);
            candidate->advice=control->active_advice;
        }
        if (candidate->advice) {
            work->advice=candidate->advice;
            control->active_advice=candidate->advice;
            control->active_key=prime_native_advice_holder(candidate->advice)->key;
        }
        break;
    }
}

static void native_dialect_attach_candidate(PrimeNativeWork *work) {
    native_dialect_attach_candidate_at(work, bindings_occurrence_token(
        petta_machine_pending_environment(&work->dialect->machine->machine)));
}

/* An executor prepares one machine operation, never a private search agenda.
 * Its result owns the post-operation world until the parent selects this
 * occurrence. Candidate advice, answers and branch images are committed on
 * that coordinator selection, independently of physical completion order. */
static void native_dialect_parallel_operation(PrimeNativeWork *work) {
    NativeDialectWork *job = work->dialect;
    NativeDialectOwner *owner = job->machine;
    assert(owner->references == 1u && owner->live == job && !job->operation_pending);
    CettaBranchAdmission admission = native_dialect_admit_capture(
        owner, owner->control->space);
    assert(admission.status == CETTA_BRANCH_ADMISSION_AVAILABLE);
    job->operation_authority = admission.authority;
    job->operation_source = space_read_token(owner->control->space);
    job->operation_candidate = bindings_occurrence_token(
        petta_machine_pending_environment(&owner->machine));
    owner->context.controller_quantum_active = true;
    owner->context.controller_quantum_expired = false;
    owner->context.controller_quantum_limit = 1u;
    owner->context.controller_quantum_transitions = 0u;
    job->operation_step = petta_machine_next(&owner->machine,
        &job->operation_answer, &job->operation_environment);
    job->operation_raised = petta_machine_last_answer_raised(&owner->machine);
    job->operation_pending = true;
}

static bool native_dialect_source_start(PrimeNativeWork *work, Atom *source, Atom *type,
                                        bool owned_call) {
    NativeDialectWork *job=work->dialect;
    NativeDialectOwner *owner=job->machine;
    PrimeNativeControl *control=work->owner;
    if (control->open_frames == SIZE_MAX ||
        (work->parent && work->parent->control_pending == SIZE_MAX)) {
        control->driver.capacity_blocked=true;return false;
    }
    PrimeEvalStackFrame *frame=prime_eval_stack_frame_new(PRIME_EVAL_STACK_FRAME_DIALECT_SOURCE);
    frame->space=control->space;frame->arena=control->driver.arena;
    frame->target=job->target;frame->fuel=owner->context.fuel;
    const Bindings *environment=petta_machine_pending_environment(&owner->machine);
    frame->atom=bindings_apply_if_vars(environment,frame->arena,source);
    if (frame->atom) frame->atom=atom_deep_copy(frame->arena,frame->atom);
    frame->dialect_source_is_call=owned_call;
    frame->etype=type ? bindings_apply_if_vars(environment,frame->arena,type) : NULL;
    if (frame->etype) frame->etype=atom_deep_copy(frame->arena,frame->etype);
    if (!frame->atom || (type && !frame->etype) || !bindings_clone(&frame->env,environment)) {
        prime_eval_stack_frame_free(frame);control->driver.capacity_blocked=true;return false;
    }
    frame->env_initialized=true;
    if (!bindings_promote_atoms_to_arena(&frame->env,frame->arena)) {
        prime_eval_stack_frame_free(frame);control->driver.capacity_blocked=true;return false;
    }
    CettaContinuationStatus status=cetta_continuation_capture(
        petta_machine_continuation_machine(&owner->machine),&job->image);
    if (status != CETTA_CONTINUATION_READY) {
        if (getenv("CETTA_PRIME_NATIVE_CONTROL_TRACE"))
            fprintf(stderr,"[native-dialect] source capture status=%d owned=%d\n",status,owned_call);
        prime_eval_stack_frame_free(frame);
        if (status == CETTA_CONTINUATION_INVALIDATED) control->invalidated=true;
        else control->driver.capacity_blocked=true;
        return false;
    }
    owner->live=NULL;
    /* The return image still owns this executor. Retire the work's extra
     * reference without ending the executor's borrowed cache authority. */
    prime_native_parallel_destroy(work->parallel);work->parallel=NULL;
    frame->dialect_source=job;work->dialect=NULL;
    outcome_set_init(&frame->child);frame->child_initialized=true;
    prime_eval_stack_push(frame);
    if (native_dialect_schedule_type(control,frame->space,frame->atom,frame->etype,
            frame->fuel,&frame->env,NULL,&frame->child))
        frame->state=PRIME_EVAL_STACK_FRAME_WAIT_CALL;
    else {
        if (getenv("CETTA_PRIME_NATIVE_CONTROL_TRACE"))
            fprintf(stderr,"[native-dialect] source schedule refused owned=%d\n",owned_call);
        control->driver.capacity_blocked=true;
    }
    return true;
}

/* The source's physical answer occurrence owns its scheduling inheritance.
 * Several equal values may have different advice, and a slow scorer retains
 * the same mutable advice cell across the source-to-caller return. */
static PrimeEvalStackFrame *native_dialect_source_sink(const PrimeNativeWork *work) {
    PrimeEvalStackFrame *frame=work->parent;
    return frame && frame->kind == PRIME_EVAL_STACK_FRAME_DIALECT_SOURCE &&
        work->dialect->target == &frame->child ? frame : NULL;
}

static bool prime_native_return_reserve_advice(PrimeEvalStackFrame *frame) {
    if (!frame || frame->child.len < frame->control_return_advice_capacity) return true;
    size_t capacity=frame->control_return_advice_capacity;
    if (capacity > SIZE_MAX / 2u) return false;
    capacity=capacity ? capacity * 2u : 4u;
    if (frame->child.len==SIZE_MAX) return false;
    if (capacity<=frame->child.len) capacity=(size_t)frame->child.len+1u;
    if (capacity > SIZE_MAX / sizeof(*frame->control_return_advice)) return false;
    void *items=realloc(frame->control_return_advice,
        capacity * sizeof(*frame->control_return_advice));
    if (!items) return false;
    frame->control_return_advice=items;
    frame->control_return_advice_capacity=capacity;
    return true;
}

/* These receivers append physical rows without splicing or reordering
 * their child sets. Other frame protocols keep their own inherited context. */
static bool prime_native_return_receiver(const PrimeEvalStackFrame *frame) {
    return frame && (frame->kind == PRIME_EVAL_STACK_FRAME_DIALECT_SOURCE ||
        (eval_current_language_id() == CETTA_LANGUAGE_PRIME &&
         (frame->kind == PRIME_EVAL_STACK_FRAME_NORMALIZE_ATOM ||
          frame->kind == PRIME_EVAL_STACK_FRAME_NORMALIZE_CHILDREN ||
          frame->kind == PRIME_EVAL_STACK_FRAME_FORCE ||
          frame->kind == PRIME_EVAL_STACK_FRAME_BIND_FINISH)));
}

static bool prime_native_note_returns(PrimeEvalStackFrame *frame) {
    if (!prime_native_return_receiver(frame) || !frame->child_initialized ||
        frame->control_return_advice_length >= frame->child.len) return true;
    PrimeNativeControl *control=g_prime_eval_stack_driver->control;
    /* Ordinary ungraded Prime evaluation neither allocates nor populates
     * this optional scheduling sidecar. */
    if (frame->kind != PRIME_EVAL_STACK_FRAME_DIALECT_SOURCE &&
        !control->policy.priority && !control->live_bound) return true;
    if (!prime_native_return_reserve_advice(frame)) return false;
    while (frame->control_return_advice_length < frame->child.len)
        frame->control_return_advice[frame->control_return_advice_length++]=
            (PrimeNativeReturnAdvice){.key=control->active_key,
                .bound=control->active_bound,.advice=control->active_advice};
    return true;
}

static const PrimeNativeReturnAdvice *prime_native_frame_next_return(
    const PrimeEvalStackFrame *frame) {
    if (!frame || ((frame->kind == PRIME_EVAL_STACK_FRAME_NORMALIZE_ATOM ||
                   frame->kind == PRIME_EVAL_STACK_FRAME_NORMALIZE_CHILDREN) &&
                  frame->state != PRIME_EVAL_STACK_FRAME_WAIT_CALL)) return NULL;
    CettaCount index = frame->kind == PRIME_EVAL_STACK_FRAME_BIND_FINISH
        ? frame->stream_forwarded : frame->index;
    if (frame->kind == PRIME_EVAL_STACK_FRAME_DIALECT_SOURCE &&
        index >= frame->child.len && !frame->control_pending &&
        !frame->dialect_source_success) index = frame->dialect_error_index;
    return index < frame->control_return_advice_length
        ? &frame->control_return_advice[index] : NULL;
}

static PrimeNativeReturnScope prime_native_return_enter(
    const PrimeEvalStackFrame *frame, CettaCount index) {
    PrimeNativeReturnScope scope = {0};
    if (!prime_eval_stack_queued(g_prime_eval_stack_driver) ||
        index >= frame->control_return_advice_length) return scope;
    PrimeNativeControl *control = g_prime_eval_stack_driver->control;
    scope.control = control;
    scope.saved = (PrimeNativeReturnAdvice){control->active_key,
        control->active_bound, control->active_advice};
    const PrimeNativeReturnAdvice *returned = &frame->control_return_advice[index];
    control->active_key = returned->key;
    control->active_bound = returned->bound;
    control->active_advice = returned->advice;
    return scope;
}

static void prime_native_return_leave(PrimeNativeReturnScope *scope) {
    if (!scope->control) return;
    scope->control->active_key = scope->saved.key;
    scope->control->active_bound = scope->saved.bound;
    scope->control->active_advice = scope->saved.advice;
}

static bool native_dialect_source_return(PrimeEvalStackFrame *frame, CettaCount index) {
    if (index >= frame->child.len || index >= frame->control_return_advice_length) return false;
    Outcome *value=&frame->child.items[index];
    /* The machine's supply API consumes a value without a delay service at
     * this return point. It cannot erase a conditional occurrence. */
    if (value->delayed ||
        (value->result_form != CETTA_CALL_RESULT_EXPRESSION &&
         value->result_form != CETTA_CALL_RESULT_COMPLETED_VALUE)) {
        g_prime_eval_stack_driver->control->failed = true;
        eval_mark_interrupted(CETTA_EVAL_INCOMPLETE_HOST_FAILURE);
        return false;
    }
    PrimeNativeReturnAdvice inherited=frame->control_return_advice[index];
    NativeDialectWork *template=frame->dialect_source;
    NativeDialectOwner *owner=template->machine;
    PrimeNativeControl *control=g_prime_eval_stack_driver->control;
    if (owner->references == SIZE_MAX ||
        (frame->control_parent && frame->control_parent->control_pending == SIZE_MAX))
        return false;
    if (owner->live && owner->live != template) return false;
    if (template->image.payload) {
        CettaContinuationStatus restored=cetta_continuation_restore(
            petta_machine_continuation_machine(&owner->machine),&template->image);
        if (restored != CETTA_CONTINUATION_READY) {
            if (getenv("CETTA_PRIME_NATIVE_CONTROL_TRACE"))
                fprintf(stderr,"[native-dialect] return restore status=%d owned=%d\n",restored,frame->dialect_source_is_call);
            if (restored == CETTA_CONTINUATION_INVALIDATED) control->invalidated=true;
            return false;
        }
    }
    owner->live=template;
    NativeDialectWork *child=calloc(1u,sizeof(*child));
    PrimeNativeWork *branch=calloc(1u,sizeof(*branch));
    if (!child || !branch) {free(child);free(branch);return false;}
    child->source_value=outcome_atom_materialize(frame->arena,value);
    if (!child->source_value || !bindings_clone(&child->source_environment,&value->env)) {
        free(child);free(branch);return false;
    }
    child->source_return_pending=true;
    child->source_return_is_call=frame->dialect_source_is_call;
    child->machine=owner;owner->references++;
    child->target=template->target;child->pure=template->pure;
    CettaContinuationStatus captured=cetta_continuation_capture(
        petta_machine_continuation_machine(&owner->machine),&child->image);
    if (captured == CETTA_CONTINUATION_READY)
        captured=cetta_continuation_capture(
            petta_machine_continuation_machine(&owner->machine),&template->image);
    if (captured != CETTA_CONTINUATION_READY) {
        native_dialect_work_free(child);free(branch);
        if (captured == CETTA_CONTINUATION_INVALIDATED) control->invalidated=true;
        return false;
    }
    owner->live=NULL;
    *branch=(PrimeNativeWork){.owner=control,.parent=frame->control_parent,
        .key=inherited.key,.bound=inherited.bound,.advice=inherited.advice,
        .scoring=frame->control_scoring,.dialect=child};
    if (branch->parent) branch->parent->control_pending++;
    if (prime_native_work_append(control,branch)) return true;
    if (frame->control_parent) frame->control_parent->control_pending--;
    return false;
}

/* The source and the waiting return image share the ordinary agenda. Good
 * values can resume immediately; fallback errors have no publication right
 * until every source alternative is closed and none produced a good value. */
static void native_dialect_source_resume(PrimeEvalStackFrame *frame) {
    PrimeNativeControl *control=g_prime_eval_stack_driver->control;
    if (frame->state == PRIME_EVAL_STACK_FRAME_DEMAND) {
        if (native_dialect_schedule_type(control,frame->space,frame->atom,frame->etype,
                frame->fuel,&frame->env,NULL,&frame->child))
            frame->state=PRIME_EVAL_STACK_FRAME_WAIT_CALL;
        else control->driver.capacity_blocked=true;
        return;
    }
    while (frame->index < frame->child.len) {
        Outcome *item=&frame->child.items[frame->index];
        Atom *value=outcome_atom_materialize(frame->arena,item);
        if (!value) {control->driver.capacity_blocked=true;return;}
        if (frame->dialect_source_is_call || !atom_is_error(value)) {
            if (!native_dialect_source_return(frame,frame->index)) {
                control->driver.capacity_blocked=true;return;
            }
            frame->dialect_source_success=true;
            frame->index++;
            return;
        }
        frame->index++;
    }
    if (frame->control_pending) return;
    if (!frame->dialect_source_success && frame->dialect_error_index < frame->child.len) {
        if (!native_dialect_source_return(frame,frame->dialect_error_index)) {
            control->driver.capacity_blocked=true;return;
        }
        frame->dialect_error_index++;
        return;
    }
    prime_eval_stack_pop();
}

/* True retires this occurrence. False retains its full machine/image and
 * return obligation. A provider refusal is never an exhaustion proof. */
static bool native_dialect_work_step(PrimeNativeWork *work) {
    NativeDialectWork *job = work->dialect;
    NativeDialectOwner *owner = job->machine;
    PrimeNativeControl *control = work->owner;
    bool prepared = job->operation_pending;
    if (job->pending.length) return native_dialect_commit_batch(work);
    if (owner->live && owner->live != job) return false;
    if (prepared) {
        CettaBranchAdmission current = native_dialect_admit_capture(owner, control->space);
        if (current.status != CETTA_BRANCH_ADMISSION_AVAILABLE ||
            !cetta_branch_authority_token_equal(&current.authority,
                &job->operation_authority) ||
            !space_read_token_matches_live_space(job->operation_source, control->space) ||
            !prime_native_parallel_validate(control, owner->storage_owner)) {
            control->invalidated = true;
            return false;
        }
    }
    if (job->image.payload) {
        CettaContinuationStatus status = cetta_continuation_restore(
            petta_machine_continuation_machine(&owner->machine),&job->image);
        if (status != CETTA_CONTINUATION_READY) {
            if (status == CETTA_CONTINUATION_INVALIDATED) control->invalidated = true;
            else control->driver.capacity_blocked = true;
            return false;
        }
    }
    owner->live = job;
    if (job->source_return_pending) {
        bool supplied = job->source_return_is_call
            ? petta_machine_supply_owned_call(&owner->machine,
                job->source_value,&job->source_environment)
            : petta_machine_supply_he_source(&owner->machine,
                job->source_value,&job->source_environment);
        if (!supplied) {
            if (getenv("CETTA_PRIME_NATIVE_CONTROL_TRACE"))
                fprintf(stderr,"[native-dialect] source supply refused owned=%d\n",job->source_return_is_call);
            control->driver.capacity_blocked=true;return false;
        }
        bindings_free(&job->source_environment);
        job->source_value=NULL;job->source_return_pending=false;
        goto capture_residual;
    }
    if (prepared) native_dialect_attach_candidate_at(work, job->operation_candidate);
    else native_dialect_attach_candidate(work);
    if (control->driver.capacity_blocked || control->failed) return false;
    PrimeEvalStackFrame *source_sink=native_dialect_source_sink(work);
    if (!prime_native_return_reserve_advice(source_sink)) {
        control->driver.capacity_blocked=true;return false;
    }
    owner->context.controller_quantum_active = true;
    owner->context.controller_quantum_expired = false;
    owner->context.controller_quantum_limit = 1u;
    owner->context.controller_quantum_transitions = 0u;
    Atom *answer = NULL;
    Bindings environment;
    PettaMachineStep step;
    bool raised;
    if (prepared) {
        step = job->operation_step;
        answer = job->operation_answer;
        bindings_move(&environment, &job->operation_environment);
        raised = job->operation_raised;
        job->operation_answer = NULL;
        job->operation_pending = false;
    } else {
        owner->control->worker_execution = false;
        owner->control->effect_pending = false;
        step = petta_machine_next(&owner->machine,&answer,&environment);
        raised = petta_machine_last_answer_raised(&owner->machine);
    }
    if (step == PETTA_MACHINE_STEP_ANSWER) {
        /* The destination arena owns any escaped value and binding roots;
         * retiring a transferred executor cannot retire a published answer. */
        if (owner->storage_owner) {
            Arena *payload = outcome_set_payload_arena(job->target, control->driver.arena);
            Atom *promoted = atom_deep_copy(payload, answer);
            if (!promoted || !bindings_promote_atoms_to_arena(&environment, payload)) {
                job->operation_answer = answer;
                bindings_move(&job->operation_environment, &environment);
                job->operation_step = step;
                job->operation_raised = raised;
                job->operation_pending = true;
                if (!prepared) {
                    CettaBranchAdmission current = native_dialect_admit_capture(owner, control->space);
                    job->operation_authority = current.authority;
                    job->operation_source = space_read_token(control->space);
                    job->operation_candidate = bindings_occurrence_token(
                        petta_machine_pending_environment(&owner->machine));
                }
                control->driver.capacity_blocked = true;
                return false;
            }
            answer = promoted;
        }
        if (raised && eval_current_language_id() == CETTA_LANGUAGE_PETTA) {
            petta_eval_raise(answer);
            control->failed = true;
        } else {
            if (!owner->portable)
                answer = petta_flatten_closed_open_cons(control->driver.arena,answer);
            outcome_set_add(job->target,answer,&environment);
            if (!petta_eval_machine_answer_delayed(&owner->machine,answer,&environment,
                    control->driver.arena,job->target))
                control->driver.capacity_blocked = true;
            prime_eval_stack_stream_note(job->target);
        }
    }
    bindings_free(&environment);
    native_dialect_attach_candidate(work);
    if (step == PETTA_MACHINE_STEP_EXHAUSTED) {
        owner->live = NULL;
        return true;
    }
    if (step == PETTA_MACHINE_STEP_INVALIDATED) {
        if (getenv("CETTA_PRIME_NATIVE_CONTROL_TRACE"))
            fprintf(stderr,"[native-dialect] machine invalidated\n");
        control->invalidated = true;
    }
    if (step == PETTA_MACHINE_STEP_CAPACITY) control->driver.capacity_blocked = true;
    if (step == PETTA_MACHINE_STEP_HOST_ERROR) control->failed = true;
    if (step == PETTA_MACHINE_STEP_DECLINED) control->driver.capacity_blocked = true;
    if (step != PETTA_MACHINE_STEP_ANSWER && step != PETTA_MACHINE_STEP_SUSPENDED)
        return false;
    Atom *source_type=NULL;
    Atom *source=petta_machine_pending_he_source(&owner->machine,&source_type);
    if (source) return native_dialect_source_start(work,source,source_type,false);
    source=petta_machine_pending_owned_call(&owner->machine,&source_type);
    if (source) return native_dialect_source_start(work,source,source_type,true);
    if (petta_machine_external_branch_ready(&owner->machine)) {
        CettaContinuationBatch images = {0};
        CettaContinuationStatus expanded = cetta_continuation_expand(
            petta_machine_continuation_machine(&owner->machine),&images);
        if (expanded == CETTA_CONTINUATION_READY) {
            bool wrapped = native_dialect_branch_images(work,&images);
            cetta_continuation_batch_destroy(&images);
            if (!wrapped) {control->driver.capacity_blocked=true;return false;}
            return native_dialect_commit_batch(work);
        }
        cetta_continuation_batch_destroy(&images);
        if (expanded == CETTA_CONTINUATION_INVALIDATED) control->invalidated = true;
        if (expanded == CETTA_CONTINUATION_CAPACITY) control->driver.capacity_blocked = true;
    }
capture_residual:
    /* A sole image's live machine already is its owned residual. Capturing
     * it is necessary only when a sibling can restore this same host. */
    if (owner->references > 1u) {
        CettaContinuationStatus captured = cetta_continuation_capture(
            petta_machine_continuation_machine(&owner->machine),&job->image);
        if (captured == CETTA_CONTINUATION_READY) owner->live = NULL;
        else if (captured == CETTA_CONTINUATION_INVALIDATED) control->invalidated = true;
        else if (captured == CETTA_CONTINUATION_CAPACITY) control->driver.capacity_blocked = true;
    }
    return false;
}

/* The canonical HE type services decide source demand and applicability;
 * the machine retains the ensuing argument, equation and return jobs. */
static bool native_dialect_prepare_he_type(
    void *raw, Space *space, Arena *arena, Atom *source, Atom *type,
    bool *handled, bool *untyped, PettaTypeCall **calls, uint32_t *count,
    OutcomeSet *values) {
    NativeDialectOwner *owner = raw;
    *handled = true; *untyped = false; *calls = NULL; *count = 0u;
    Bindings empty;
    bindings_init(&empty);
    Atom *bound = registry_lookup_atom(source);
    if (bound) {
        outcome_set_add(values, bound, &empty);
        return true;
    }
    source = materialize_runtime_token(space, arena, source);
    if (!source) return false;
    if (atom_is_empty(source)) return true;
    if (atom_is_error(source)) {
        outcome_set_add(values, source, &empty); return true;
    }
    HeTypeDemand demand = eval_atom_type_demand(arena, source, type);
    if (demand == HE_TYPE_KEEP) {
        outcome_set_add(values, source, &empty); return true;
    }
    if (demand == HE_TYPE_CAST) {
        ResultSet cast;
        result_set_init(&cast);
        type_cast_fn(space, arena, source, type, owner->context.fuel, &cast);
        for (CettaCount i = 0u; i < cast.len; ++i)
            outcome_set_add(values, cast.items[i], &empty);
        result_set_free(&cast); return true;
    }
    SymbolId head = atom_head_symbol_id(source);
    /* HE does not reduce (quote X): interpreting it gives it unchanged,
     * never PeTTa's reading of quote as its operand. */
    if (head == g_builtin_syms.quote && source->expr.len == 2u) {
        outcome_set_add(values, source, &empty); return true;
    }
    bool held_control = head == g_builtin_syms.if_text || head == g_builtin_syms.let;
    /* Host syntax keeps its declared operand policy. In particular a
     * resource constructor is an operation, not an unmatched equation call.
     * The ordinary dispatcher still owns its permission and result. */
    if (!held_control && symbol_id_is_builtin(head) &&
        !is_grounded_op(head) && petta_program_head_is_intrinsic(head)) {
        *handled = false; return true;
    }
    bool nested_primitive = false;
    if (is_grounded_op(head) && petta_semantics_form(head) == PETTA_FORM_NONE)
        for (CettaExprIndex i = 1u; i < source->expr.len; ++i)
            nested_primitive |= source->expr.elems[i]->kind == ATOM_EXPR;
    if (!held_control && (is_grounded_op(head) ? !nested_primitive :
             petta_semantics_form(head) != PETTA_FORM_NONE)) {
        *handled = false; return true;
    }
    if (head == g_builtin_syms.let && source->expr.len == 4u) {
        /* The binding intrinsic holds pattern/template and interprets only
         * the source. Its body retains the caller's result contract. */
        Atom *elements[] = {atom_symbol_id(arena, g_builtin_syms.arrow),
            atom_symbol_id(arena, g_builtin_syms.atom), atom_undefined_type(arena),
            atom_symbol_id(arena, g_builtin_syms.atom), type};
        Atom *signature = atom_expr(arena, elements, 5u);
        *calls = calloc(1u, sizeof(**calls));
        if (!signature || !*calls) return false;
        **calls = (PettaTypeCall){.signature=signature,.arity=3u,.result_type=type};
        *count=1u; return true;
    }
    Atom **types = NULL;
    uint32_t ntypes;
    if (head == g_builtin_syms.if_text &&
        (source->expr.len == 3u || source->expr.len == 4u)) {
        Atom *elements[] = {atom_symbol_id(arena,g_builtin_syms.arrow),
            atom_symbol(arena,"Bool"),atom_symbol_id(arena,g_builtin_syms.atom),
            atom_symbol_id(arena,g_builtin_syms.atom),type};
        elements[source->expr.len]=type;
        types=cetta_malloc(sizeof(*types));
        types[0]=atom_expr(arena,elements,source->expr.len+1u);
        ntypes=1u;
    } else ntypes = eval_get_atom_types_profiled(space, arena,
        source->expr.elems[0], &types);
    ApplicabilityErrors errors;
    applicability_errors_init(&errors);
    bool function = false;
    bool selected = false;
    bool ok = true;
    for (uint32_t i = 0u; ok && !selected && i < ntypes; ++i) {
        if (!is_function_type(types[i])) continue;
        function = true;
        Atom *signature = cetta_instantiate_frame_syntax(arena, types[i]);
        OutcomeSet contracts;
        outcome_set_init_with_owner(&contracts, arena);
        if (!signature) {ok = false; outcome_set_free(&contracts); break;}
        selected = check_function_applicable(source, signature, type,
            space, arena, owner->context.fuel, ntypes > 1u, &errors, NULL, &contracts);
        if (selected) {
            ok = contracts.len > 0u && contracts.len <= UINT32_MAX &&
                contracts.len <= SIZE_MAX / sizeof(**calls);
            if (ok) *calls = calloc((size_t)contracts.len, sizeof(**calls));
            ok = ok && *calls != NULL;
            MatchVisibleVarSet visible;
            match_visible_var_set_init(&visible);
            collect_match_visible_vars_rec(source, &visible);
            collect_match_visible_vars_rec(type, &visible);
            for (uint32_t j = 0u; ok && j < contracts.len; ++j) {
                Outcome *contract = &contracts.items[j];
                size_t length = signature->expr.len;
                Atom **elements = arena_alloc(arena, length * sizeof(*elements));
                if (!elements) {ok = false; break;}
                for (size_t k = 0u; ok && k < length; ++k) {
                    elements[k] = bindings_apply_if_vars(
                        &contract->env, arena, signature->expr.elems[k]);
                    ok = elements[k] != NULL;
                }
                Atom *result = he_type_result_demand(
                    arena, outcome_atom_materialize(arena, contract));
                elements[length - 1u] = result;
                Atom *instantiated = ok && result
                    ? atom_expr(arena, elements, length) : NULL;
                Bindings projected;
                bindings_init(&projected);
                ok = instantiated && project_match_visible_bindings(
                    arena, &visible, &contract->env, &projected);
                bool has_refinement = bindings_has_bound_values(&projected) ||
                    projected.eq_len != 0u;
                Atom *refinement = ok && has_refinement
                    ? bindings_capture_value(arena, &projected) : NULL;
                bindings_free(&projected);
                if (!ok || (has_refinement && !refinement)) {ok = false; break;}
                (*calls)[(*count)++] = (PettaTypeCall){
                    .signature = instantiated, .arity = length - 2u,
                    .result_type = result, .source_refinement = refinement};
            }
            match_visible_var_set_free(&visible);
        }
        outcome_set_free(&contracts);
    }
    free(types);
    if (ok && function && !selected)
        for (uint32_t i = 0u; i < errors.len; ++i)
            outcome_set_add(values, errors.items[i], &empty);
    applicability_errors_free(&errors);
    if (!ok) return false;
    if (function) return true;
    CettaExprLen length = source->expr.len + 1u;
    if (length <= source->expr.len ||
        !cetta_expr_len_mul_fits_size(length, sizeof(Atom *))) return false;
    Atom **elements = arena_alloc(arena, sizeof(*elements) * (size_t)length);
    if (!elements) return false;
    elements[0] = atom_symbol_id(arena, g_builtin_syms.arrow);
    for (CettaExprIndex i = 1u; i + 1u < length; ++i)
        elements[i] = atom_undefined_type(arena);
    elements[length - 1u] = type;
    Atom *signature = atom_expr(arena, elements, length);
    *calls = calloc(1u, sizeof(**calls));
    if (!signature || !*calls) return false;
    **calls = (PettaTypeCall){.signature = signature,
        .arity = source->expr.len - 1u, .result_type = type};
    *count = 1u; *untyped = true; return true;
}

static bool native_dialect_reduce_he_call(void *raw, Space *space, Arena *arena,
    Atom *call, const Bindings *environment, bool *handled, bool *interpret,
    OutcomeSet *values, CettaCallOutcome *end) {
    NativeDialectOwner *owner = raw;
    *handled = false; *interpret = false;
    if (!prime_eval_check_call_authority(space, arena, call)) {
        *handled = true;
        *end = cetta_call_interrupted();
        return true;
    }
    SymbolId head = atom_head_symbol_id(call);
    if (head == g_builtin_syms.if_text &&
        (call->expr.len == 3u || call->expr.len == 4u)) {
        *handled = true;
        Atom *condition = call->expr.elems[1];
        Atom *branches[] = {call->expr.elems[2], call->expr.len == 4u
            ? call->expr.elems[3] : legacy_empty_sentinel(arena)};
        if (atom_is_error(condition)) { *end=cetta_call_raised(condition); return true; }
        if (is_true_atom(condition) || is_false_atom(condition)) {
            *interpret = true;
            outcome_set_add(values, branches[is_true_atom(condition) ? 0u : 1u], environment);
        } else if (condition->kind == ATOM_VAR) {
            *interpret = true;
            for (size_t i=0u; i<2u; ++i) {
                BindingsBuilder builder;
                if (!bindings_builder_init(&builder, environment)) return false;
                bool bound = bindings_builder_add_var_fresh(&builder, condition,
                    atom_symbol_id(arena, i ? g_builtin_syms.false_text : g_builtin_syms.true_text));
                if (bound) outcome_set_add(values, branches[i], bindings_builder_bindings(&builder));
                bindings_builder_free(&builder);
                if (!bound) return false;
            }
        } else {
            /* HE leaves an unmatched, potentially Boolean condition as a
             * residual. It must not be interpreted again at this same call. */
            outcome_set_add(values, call, environment);
        }
        return true;
    }
    if (head == g_builtin_syms.let && call->expr.len == 4u) {
        *handled = true; *interpret = true;
        Atom *pattern=call->expr.elems[1], *value=call->expr.elems[2], *body=call->expr.elems[3];
        if (atom_is_error(value)) { *end=cetta_call_raised(value); return true; }
        if (atom_is_legacy_empty_sentinel(value)) return true;
        BindingsBuilder builder;
        if (!bindings_builder_init(&builder, environment)) return false;
        bool matched = pattern->kind == ATOM_VAR
            ? bindings_builder_add_var_fresh(&builder, pattern, value)
            : match_atoms_builder(value, pattern, &builder, arena);
        bool ok = true;
        if (matched) {
            const Bindings *bindings=bindings_builder_bindings(&builder);
            Bindings visible;
            ok = bindings_project_body_visible_env(arena,body,bindings,&visible);
            if (ok) {
                Atom *result=bindings_apply_projected_body_visible(&visible,arena,body);
                ok=result != NULL;
                if (ok) outcome_set_add(values,result,bindings);
                bindings_free(&visible);
            }
        }
        bindings_builder_free(&builder);
        return ok;
    }
    if (is_grounded_op(head) && petta_semantics_form(head) == PETTA_FORM_NONE) {
        *handled = true; *interpret = true;
        if (petta_eval_machine_apply_ready_values(&owner->context,space,arena,
                call,environment,values,end)) return true;
        /* Open or non-scalar ready values keep the host's own interpretation
         * and refusal behavior. The full host service returns completed
         * values, including delimited data; only the direct primitive above
         * returns an expression that still needs interpretation. */
        *interpret = false;
        return petta_eval_machine_evaluate_host(&owner->context,space,arena,
            call,environment,values,end,NULL);
    }
    return true;
}

static PettaMachineHostMode native_dialect_classify(
    void *raw, Space *space, Atom *expression) {
    if (g_prime_eval_stack_driver && g_prime_eval_stack_driver->pure_authority &&
        !prime_native_actual_call_allowed(space, g_prime_eval_stack_driver->arena, expression))
        return PETTA_MACHINE_HOST_READY_OVERRIDE;
    NativeDialectOwner *owner = raw;
    SymbolId head = atom_head_symbol_id(expression);
    if (owner->portable && head != SYMBOL_ID_NONE && is_grounded_op(head) &&
        petta_semantics_form(head) == PETTA_FORM_NONE)
        return PETTA_MACHINE_HOST_READY_OVERRIDE;
    PettaMachineHostMode mode =
        petta_eval_machine_classify_host(raw, space, expression);
    /* An HE application deferred by its type provider still belongs to
     * the HE host. PeTTa's uniform strict-argument modes cannot decide its
     * operand demand: deconstructing an Expression, for example, must not
     * first evaluate the code-shaped elements it holds. The canonical host
     * receives the captured source and publishes its completed outcomes. */
    if (owner->portable &&
        (mode == PETTA_MACHINE_HOST_STRICT_APPLICATION ||
         mode == PETTA_MACHINE_HOST_STRICT_FIRST_APPLICATION))
        return PETTA_MACHINE_HOST_READY_OVERRIDE;
    return mode;
}

static bool native_dialect_owns_call(void *raw, const Atom *expression) {
    (void)raw;
    /* An explicit policy is interpreted by the existing selection service.
     * Keep its source operand and the caller's return image held while that
     * service owns a nested observation. The ordinary HE unary declaration
     * of select is not the arity contract of this extended form. */
    if (native_control_profile_active() && expression &&
        expression->kind == ATOM_EXPR &&
        (expression->expr.len == 3u || expression->expr.len == 4u) &&
        atom_head_symbol_id(expression) == g_builtin_syms.select) {
        Atom *policy = expression->expr.elems[expression->expr.len - 2u];
        if (expr_head_is_id(policy, g_builtin_syms.search_policy) &&
            policy->expr.len == 2u &&
            expr_head_is_id(policy->expr.elems[1], prime_need_symbols()->control))
            return true;
    }
    return native_weight_form(expression) ||
        (native_control_profile_active() && space_match_where_grade(expression)) ||
        (expression && expression->kind==ATOM_EXPR && expression->expr.len==4u &&
         atom_head_symbol_id(expression)==g_builtin_syms.match);
}

static bool native_dialect_schedule_type(PrimeNativeControl *control, Space *space,
    Atom *expression, Atom *type, int fuel, const Bindings *environment,
    const PettaPlanNode *source_plan, OutcomeSet *target) {
    if (!control || !space || !expression || !environment || !target ||
        !native_dialect_admits(space, expression)) return false;
    if (native_dialect_owns_call(NULL,expression))
        return prime_eval_stack_schedule_call(space,control->driver.arena,
            expression,type,fuel,true,-1,environment,environment,
            g_prime_need_evaluator_id,target);
    if (control->driver.top && control->driver.top->control_pending == SIZE_MAX)
        return false;
    NativeDialectOwner *owner = calloc(1u, sizeof(*owner));
    NativeDialectWork *job = calloc(1u, sizeof(*job));
    PrimeNativeWork *work = calloc(1u, sizeof(*work));
    if (!owner || !job || !work) {free(owner);free(job);free(work);return false;}
    owner->control = control;
    owner->references = 1u;
    owner->portable = eval_current_language_id() == CETTA_LANGUAGE_HE;
    owner->source_expression = expression;
    *job = (NativeDialectWork){.machine=owner,.target=target,
        .pure=control->pure_authority || control->active_scoring || control->driver.pure_authority};
    *work = (PrimeNativeWork){.owner=control,.parent=control->driver.top,
        .key=control->active_key,.bound=control->active_bound,
        .advice=control->active_advice,.scoring=control->active_scoring,.dialect=job};
    owner->context = (PettaEvalMachineContext){
        .fuel=fuel,.data_ops=cetta_petta_profile_admits_typecheck_ops(),
        .typecheck_ops=cetta_petta_profile_admits_native_typecheck_v2(),
        .controller_policy=CETTA_SEARCH_CONTROLLER_FIFO,.controller_admitted=true,
        .controller_quantum_limit=1u,
        .observation_contract={.demand={.completion=CETTA_OBSERVATION_COMPLETE_BAG},
            .algebra=CETTA_OBSERVATION_ALGEBRA_EXACT_OCCURRENCES},
        .semantic_authority_epoch=1u,.library_context=g_library_context,
        .prepared_pure_cache={.root_space=space},
    };
    bool ready = cetta_control_plan_derive(owner->context.observation_contract.demand,
        CETTA_CONTROL_BRANCH_GENERAL,CETTA_CONTROL_BATCH_SINGLETON_ONLY,
        &owner->context.control_plan);
    if (ready && g_petta_program_revision_projection) {
        owner->projection = *g_petta_program_revision_projection;
        owner->projection_lease = (PettaProgramRevisionView *)owner->projection.view;
        ready = petta_program_revision_view_retain(owner->projection_lease);
        if (!ready) owner->projection_lease = NULL;
        else owner->context.program_projection = &owner->projection;
    }
    const PettaPlanNode *plan = source_plan
        ? petta_plan_copy_semantics(control->driver.arena,source_plan) : NULL;
    if (source_plan && !plan) ready = false;
#if CETTA_BUILD_WITH_PETTA_TYPECHECK_V2
    bool type_obligations = cetta_petta_profile_admits_native_typecheck_v2();
    if (type_obligations) {
        owner->context.analysis_state = calloc(1u,sizeof(*owner->context.analysis_state));
        if (!owner->context.analysis_state) ready = false;
    }
#else
    bool type_obligations = false;
#endif
    CettaMatchDecisionSemanticIdentity identity = owner->portable
        ? he_match_decision_semantics()
        : (CettaMatchDecisionSemanticIdentity){
            .language_id=(uint32_t)g_library_context->session.language_id,
            .profile_id=(uint32_t)g_library_context->session.profile->id,
            .match_policy_id=PETTA_MATCH_POLICY_RELATIONAL_STRUCTURAL_V1,
            .demand_policy_id=PETTA_DEMAND_POLICY_STRICT_CALL_ARGUMENTS_V1,
            .presentation_identity=PETTA_SEMANTIC_PRESENTATION_ID_V1,
            .compiler_identity=cetta_match_decision_compiler_identity()};
    PettaMachineHost host = petta_eval_machine_host(&owner->context,fuel,false,
        owner->portable,true,!owner->portable &&
        petta_eval_machine_source_output_constraints_required(space,expression),
        type_obligations,identity);
    host.externalize_equation_choices = true;
    host.admit_branch_capture = native_dialect_admit_capture;
    host.admit_branch_transfer = native_dialect_admit_transfer;
    host.observe_space_read = native_dialect_observe_space_read;
    host.resource_scope = owner->context.library_context;
    host.capture_resource = native_dialect_capture_resource;
    host.permit_operation = native_dialect_permit_operation;
    host.empty_symbol_is_no_result = owner->portable;
    host.prepare_he_type = owner->portable ? native_dialect_prepare_he_type : NULL;
    host.reduce_he_call = owner->portable ? native_dialect_reduce_he_call : NULL;
    host.external_he_sources = owner->portable;
    host.owns_call = native_dialect_owns_call;
    host.begin_relation_call = native_dialect_begin_call;
    host.record_equation_use = native_dialect_record_candidate;
    host.equation_use_merged = native_dialect_equation_use_merged;
    host.equation_result_payload_observed = native_dialect_observes_body;
    host.first_witness_portfolio = false;
    host.classify = native_dialect_classify;
    /* This optimizer materializes derived equations in the live source.
     * A captured query cannot revise its own source authority. Retain the
     * canonical equation provider for this owner instead. */
    host.prepare_call = NULL;
    /* A native owner may move to another executor. Mutable decision caches
     * therefore belong to this machine, while immutable equation catalogs
     * retain their ordinary source leases. */
    host.match_decisions = NULL;
    ready = ready && (type && owner->portable
        ? petta_machine_init_he_interpret(&owner->machine,space,
            control->driver.arena,expression,type,environment,&host)
        : petta_machine_init_with_plan(&owner->machine,space,
            control->driver.arena,expression,plan,environment,&host));
    if (ready && g_petta_eval_delay_service)
        ready = petta_machine_inherit_delayed_goals(&owner->machine,g_petta_eval_delay_service);
    if (!ready) {prime_native_work_destroy(work);return false;}
    owner->live = job;
    if (work->parent) work->parent->control_pending++;
    PrimeEvalStackFrame *parent = work->parent;
    if (prime_native_work_append(control,work)) return true;
    if (parent) parent->control_pending--;
    return false;
}

static bool native_dialect_schedule(PrimeNativeControl *control, Space *space,
    Atom *expression, int fuel, const Bindings *environment,
    const PettaPlanNode *plan, OutcomeSet *target) {
    return native_dialect_schedule_type(control,space,expression,NULL,fuel,
        environment,plan,target);
}

/* Transfer the actual image into a fresh executor, keeping the original
 * return edge on the coordinator. The disposable initial root only creates
 * the destination host; no source step is replayed to reach the cut. */
static bool native_dialect_parallel_prepare(PrimeNativeControl *control,
                                             PrimeNativeWork *work) {
    NativeDialectWork *job = work->dialect;
    NativeDialectOwner *source = job->machine;
    if (work->parallel_refused || job->pending.length || job->operation_pending ||
        job->source_return_pending ||
        active_search_table_mode() != CETTA_TABLE_MODE_NONE ||
        (source->live && source->live != job) || source->context.program_projection)
        return false;
    if (source->storage_owner && source->references == 1u && source->live == job &&
        !job->image.payload) {
        if (source->storage_owner->coordinator_only) return false;
        if (!work->parallel) {
            work->parallel = source->storage_owner;
            prime_native_parallel_retain(work->parallel);
        }
        return true;
    }
    if (!job->image.payload) {
        if (source->live != job || cetta_continuation_capture(
                petta_machine_continuation_machine(&source->machine),
                &job->image) != CETTA_CONTINUATION_READY)
            return false;
        source->live = NULL;
    }
    PrimeNativeParallel *parallel = calloc(1u, sizeof(*parallel));
    if (!parallel) return false;
    atomic_init(&parallel->references, 1u);
    parallel->resource_owner = eval_persistent_arena();
    parallel->resource_universe = eval_current_term_universe();
    parallel->context = eval_context_capture();
    variant_bank_init(&parallel->variants, (CettaVariantShapeOptions){
        .slot_policy=CETTA_VARIANT_SLOT_FIXED_SPELLING, .slot_name="$_slot",
        .share_immutable=true});
    parallel->variants_ready = true;
    __attribute__((cleanup(prime_native_restore_hashcons)))
    HashConsTable *previous_hashcons = g_hashcons;
    g_hashcons = &parallel->world.hashcons;
    if (!hyperpose_prepare_thread_branch(&parallel->world, 0u, control->space,
            &control->registry_read.initial, source->source_expression, NULL,
            NULL, true, true, NULL, NULL)) {
        prime_native_parallel_destroy(parallel); return false;
    }
    parallel->child = calloc(1u, sizeof(*parallel->child));
    PrimeNativeControl *child = parallel->child;
    CettaSearchPolicySpec policy = {.native_control=true, .all=true,
        .age_share=control->policy.age_share, .priority=control->policy.priority,
        .key_pattern=control->policy.key_pattern, .key_expression=control->policy.key_expression,
        .descending=control->policy.descending, .work_budget=UINT64_MAX, .costs=control->cost_enabled, .workers=1u,
        .receipts=control->policy.receipts};
    Registry *previous_registry = g_registry;
    g_registry = &parallel->world.registry;
    bool ready = child && prime_native_control_init(child, control->space,
        &parallel->world.eval_arena, parallel->world.branch, source->context.fuel,
        true, (CettaObservationDemand){.completion=CETTA_OBSERVATION_COMPLETE_BAG},
        &policy, &control->base_environment, NULL, NULL);
    g_registry = previous_registry;
    if (!ready || cetta_continuation_hub_length(&child->hub) != 1u) {
        prime_native_parallel_destroy(parallel); return false;
    }
    PrimeNativeWork *root = cetta_continuation_hub_at(&child->hub, 0u)->payload;
    if (!root->dialect) {prime_native_parallel_destroy(parallel); return false;}
    NativeDialectOwner *destination = root->dialect->machine;
    child->pure_authority = control->pure_authority || work->scoring || job->pure;
    child->parallel_owner = parallel;
    root->scoring = work->scoring;
    root->dialect->pure = job->pure;
    root->key = work->key ? atom_deep_copy(child->driver.arena, work->key) : NULL;
    root->bound = work->bound ? atom_deep_copy(child->driver.arena, work->bound) : NULL;
    for (NativeDialectCandidate *item=source->candidates; item; item=item->next) {
        NativeDialectCandidate *copy = calloc(1u, sizeof(*copy));
        if (!copy) {ready=false; break;}
        copy->occurrence = item->occurrence;
        copy->advice = item->advice;
        ready = bindings_clone(&copy->environment, &item->environment) &&
            bindings_promote_atoms_to_arena(&copy->environment, child->driver.arena);
        copy->body = atom_deep_copy(child->driver.arena, item->body);
        copy->next = destination->candidates;
        destination->candidates = copy;
        if (!ready || !copy->body) {ready=false; break;}
    }
    if (!ready || petta_machine_transfer_continuation(&destination->machine,
            &source->machine, &job->image) != CETTA_CONTINUATION_READY) {
        prime_native_parallel_destroy(parallel); return false;
    }
    /* The fresh control supplied the destination host and private storage.
     * Its initial agenda is removed without evaluation: this actual machine
     * joins the original parent frontier, including every future branch. */
    CettaOwnedContinuation destination_owned = {0};
    bool taken = cetta_continuation_hub_take(&child->hub, 0u, &destination_owned);
    assert(taken && destination_owned.payload == root);
    NativeDialectWork *transferred = root->dialect;
    root->dialect = NULL;
    transferred->target = job->target;
    transferred->pure = job->pure;
    destination->storage_owner = parallel;
    destination->live = transferred;
    work->dialect = transferred;
    native_dialect_work_free(job);
    cetta_owned_continuation_destroy(&destination_owned);
    parallel->attachment = PRIME_NATIVE_ATTACHMENT_RETURN;
    work->parallel = parallel;
    prime_native_parallel_retain(parallel);
    return true;
}
