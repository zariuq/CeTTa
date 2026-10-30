/* Transport one owned parent through the enclosing graph-copy session:
 * one quiescent owned Prime parent, no registered child, live worker capsule,
 * foreign provider, resource agenda or compiled scratch. A recording owner
 * needs a separately prepared capability in this same session, or an explicit
 * erasing session policy. The public entry selects neither automatically.
 * Declines preserve the complete source. External Space/program/universe and
 * registry resources remain borrowed under the stable episode contract.
 * This copies state; it does not authenticate a replay or execute an edge. */
static const char parent_copy_frame_kind, parent_copy_outcome_kind;
static const char parent_copy_advice_kind, parent_copy_equation_kind, parent_copy_licence_kind;
static CettaMatchCompletion prime_need_call_reader_context_copy_with_session(
    const PrimeNeedReaderContext *,AtomDeepCopySession *,PrimeNeedReaderContext **);
static PrimeNeedCallForceReader *prime_need_call_reader_copy_with_session(
    const PrimeNeedCallForceReader *,AtomDeepCopySession *);
static bool prime_need_production_driver_owned(
    const PrimeEvalStackDriver *,const void *);
typedef struct PrimeNeedEpisodeCopyPlan PrimeNeedEpisodeCopyPlan;
static PrimeNeedEpisodeCopyPlan *prime_need_episode_copy_plan_new(
    const PrimeNativeControl *,PrimeNativeControl *,
    const PrimeNativeTopEpisode *,PrimeNativeTopEpisode *);
static void prime_need_episode_copy_plan_free(PrimeNeedEpisodeCopyPlan *);
static size_t prime_need_episode_copy_pair_count(const PrimeNeedEpisodeCopyPlan *);
static bool prime_need_episode_copy_pair_at(const PrimeNeedEpisodeCopyPlan *,
    size_t,AtomDeepCopyArenaPair *);
static bool prime_need_episode_copy_capture(PrimeNeedEpisodeCopyPlan *,
    AtomDeepCopySession *,const char **);
static void prime_need_episode_copy_plan_storage(
    const PrimeNeedEpisodeCopyPlan *,CettaStorageCensus *);

typedef struct {
    const PrimeNativeControl *source;
    PrimeNativeControl *image;
    AtomDeepCopySession *atoms;
    const char *refusal;
    /* Frame/task transport is shared by the native-control checkpoint and
     * ordinary retained computations.  The explicit continuation profile
     * keeps that common copier independent of either enclosing owner. */
    const PrimeEvalStackDriver *source_driver;
    PrimeEvalStackDriver *image_driver;
    Space *source_space;
    Arena *image_storage;
    bool retain_reader_contexts;
} PrimeParentCopy;

static const PrimeEvalStackDriver *parent_copy_source_driver(
        const PrimeParentCopy *copy) {
    return copy->source_driver ? copy->source_driver
        : copy->source ? &copy->source->driver : NULL;
}
static Space *parent_copy_source_space(const PrimeParentCopy *copy) {
    return copy->source_space ? copy->source_space
        : copy->source ? copy->source->space : NULL;
}
static Arena *parent_copy_image_storage(const PrimeParentCopy *copy) {
    return copy->image_storage ? copy->image_storage
        : copy->image ? &copy->image->storage : NULL;
}

static bool parent_copy_refuse(PrimeParentCopy *copy, const char *why) {
    if (!copy->refusal) copy->refusal = why;
    return false;
}
static bool parent_copy_atom(PrimeParentCopy *copy, Atom **slot) {
    if (!*slot) return true;
    /* An episode-relative continuation keeps immutable syntax and grounded
     * identities under its retained source owner.  Independent checkpoints
     * still require an owned graph image. */
    if(copy->retain_reader_contexts)return true;
    Atom *value = atom_deep_copy_session_copy(copy->atoms, *slot);
    if (!value) return parent_copy_refuse(copy, "AtomOwner");
    *slot = value;
    return true;
}
static bool parent_copy_array(PrimeParentCopy *copy, void **out,
    const void *source, size_t count, size_t width) {
    *out = NULL;
    if (!source) return !count || parent_copy_refuse(copy, "ArrayExtent");
    if (!count || !width || count > SIZE_MAX / width)
        return parent_copy_refuse(copy, "ArrayExtent");
    void *image = malloc(count * width);
    if (!image) return parent_copy_refuse(copy, "Allocation");
    memcpy(image, source, count * width);
    *out = image;
    return true;
}
static bool parent_copy_edge(PrimeParentCopy *copy, const void *kind,
    const void *source, size_t bytes, void **out) {
    *out = NULL;
    if (!source) return true;
    void *image = atom_deep_copy_session_object_forwarded(
        copy->atoms, kind, source, bytes);
    if (!image) return parent_copy_refuse(copy, "UnregisteredEdge");
    *out = image;
    return true;
}
static bool parent_copy_register(PrimeParentCopy *copy, const void *kind,
    const void *source, void *image, size_t bytes) {
    if (atom_deep_copy_session_object_forwarded(copy->atoms, kind, source, bytes) ||
        !atom_deep_copy_session_object_record(copy->atoms, kind, source, image, bytes))
        return parent_copy_refuse(copy, "RepeatedOwner");
    return true;
}
static bool parent_copy_environment(PrimeParentCopy *copy,
    Bindings *out, const Bindings *source) {
    Arena *storage=parent_copy_image_storage(copy);
    bindings_init(out);
    const PrimeEvalStackDriver *driver=parent_copy_source_driver(copy);
    if(!storage||!driver||!bindings_clone(out,source))
        return parent_copy_refuse(copy,"BindingsOwner");
    if(copy->retain_reader_contexts)return true;
    PrimeNeedReaderContext *readers=bindings_reader_context_view(source),*image=NULL;
    if(readers && prime_need_call_reader_context_copy_with_session(
            readers,copy->atoms,&image)!=CETTA_MATCH_MATCHED) {
        bindings_free(out);
        return parent_copy_refuse(copy,"ReaderContextOwner");
    }
    if(readers)bindings_reader_context_set(out,NULL);
    bool ready=bindings_copy_owned_atoms_with_session(out,storage,copy->atoms);
    if(ready&&image)bindings_reader_context_set(out,image);
    prime_need_reader_context_release(image);
    if(!ready) {
        bindings_free(out);
        return parent_copy_refuse(copy,"BindingsOwner");
    }
    return true;
}
static bool parent_copy_outcomes(PrimeParentCopy *copy, const OutcomeSet *source,
    OutcomeSet *image) {
    Arena *storage=parent_copy_image_storage(copy);
    return (storage && (copy->retain_reader_contexts
        ? outcome_set_copy_to_episode(source,image,storage,copy->atoms)
        : outcome_set_copy_to_arena_mapping_readers(source,image,storage,
            copy->atoms,prime_need_call_reader_context_copy_with_session))) ||
        parent_copy_refuse(copy, "OutcomeOwner");
}
static bool parent_copy_owner(PrimeParentCopy *copy, Arena **owner) {
    if (!*owner) return true;
    Arena *storage=parent_copy_image_storage(copy);
    Arena *mapped = NULL;
    if (!atom_deep_copy_session_destination_for_owner(copy->atoms,*owner,&mapped))
        return copy->retain_reader_contexts;
    if(!storage || mapped!=storage)
        return parent_copy_refuse(copy, "PrivateOwner");
    *owner = mapped;
    return true;
}
#define PC_ATOM(o,f) do { if (!parent_copy_atom(copy,&(o)->f)) return false; } while (0)
#define PC_ARRAY(d,s,f,n) do { if (!parent_copy_array(copy,(void **)&(d)->f,(s)->f,(n),sizeof(*(s)->f))) return false; } while (0)
#define PC_EDGE(d,s,f,k) do { if (!parent_copy_edge(copy,&(k),(s)->f,sizeof(*(s)->f),(void **)&(d)->f)) return false; } while (0)

/* Initial and consulted symbol tables retain order, capacity and absent reads.
 * Structural-key tables need their own owner graph; this first profile refuses
 * them rather than re-interning equal keys and claiming identity preservation. */
static bool parent_copy_registry(PrimeParentCopy *copy, const Registry *source,
    Registry *image) {
    if (source->states || name_key_count(source->name_keys) ||
        source->len > source->cap || !source->entries || !source->index_slots)
        return parent_copy_refuse(copy, "RegistryOwner");
    registry_init(image);
    if (source->entries == source->inline_entries) {
        if (source->cap != 16u) return parent_copy_refuse(copy,"RegistryExtent");
        memcpy(image->inline_entries,source->inline_entries,sizeof(source->inline_entries));
    } else {
        PC_ARRAY(image,source,entries,source->cap);
    }
    if (source->index_slots == source->inline_index_slots) {
        if (source->index_cap != 32u) return parent_copy_refuse(copy,"RegistryExtent");
        memcpy(image->inline_index_slots,source->inline_index_slots,sizeof(source->inline_index_slots));
    } else {
        PC_ARRAY(image,source,index_slots,source->index_cap);
    }
    image->len=source->len; image->cap=source->cap; image->index_cap=source->index_cap;
    for (uint32_t i=0u;i<image->len;i++) {
        if (image->entries[i].key==SYMBOL_ID_NONE)
            return parent_copy_refuse(copy,"StructuralRegistryKey");
        PC_ATOM(&image->entries[i],value);
    }
    return true;
}

static bool parent_copy_registry_read_view(PrimeParentCopy *copy,
    const RegistryReadView *source,RegistryReadView *image) {
    Arena *storage=parent_copy_image_storage(copy);
    if(!source->initialized || !storage)
        return parent_copy_refuse(copy,"RegistryCapture");
    *image=(RegistryReadView){.arena=storage,.initialized=true,
        .passive=source->passive,.stale=source->stale,
        .incomplete=source->incomplete,.refusal=source->refusal,
        .stale_name=source->stale_name,.stale_expected=source->stale_expected,
        .stale_actual=source->stale_actual};
    return parent_copy_registry(copy,&source->initial,&image->initial) &&
        parent_copy_registry(copy,&source->consulted,&image->consulted) &&
        parent_copy_atom(copy,&image->stale_name) &&
        parent_copy_atom(copy,&image->stale_expected) &&
        parent_copy_atom(copy,&image->stale_actual);
}

/* A local fork can retain a receiver selected by an entered scope. This
 * copies no new authority; independent world transport keeps its boundary. */
static bool parent_copy_admits_space(const PrimeParentCopy *copy,
        const PrimeEvalContext *context,const Space *space) {
    if (space==parent_copy_source_space(copy)) return true;
    if (!copy->retain_reader_contexts || !context) return false;
    const EvalContextState *state=&context->value;
    if (state->here_depth>64u || state->home_depth>64u) return false;
    for (size_t i=0u;i<state->here_depth;i++) if(state->here[i]==space) return true;
    for (size_t i=0u;i<state->home_depth;i++) if(state->home[i]==space) return true;
    return false;
}

static bool parent_copy_equation_shell(PrimeParentCopy *copy,
    const PrimeNeedEquationContinuation *source, PrimeNeedEquationContinuation **out,
    const PrimeEvalContext *context) {
    *out = NULL;
    if (!source) return true;
    if (source->work.len > source->work_meta_cap ||
        (source->work.len && !source->work_meta))
        return parent_copy_refuse(copy,"WorkMetaExtent");
    const PrimeEvalStackDriver *driver=parent_copy_source_driver(copy);
    if (!driver || source->arena!=driver->arena ||
        !parent_copy_admits_space(copy,context,source->space))
        return parent_copy_refuse(copy,"EquationDecisionOwner");
    PrimeNeedEquationContinuation *image=calloc(1u,sizeof(*image));
    if (!image) return parent_copy_refuse(copy,"Allocation");
    *out=image;
    if (!parent_copy_register(copy,&parent_copy_equation_kind,source,image,sizeof(*source))) return false;
    const OutcomeSet *from[]={&source->work,&source->current,&source->raw_results,
        &source->residuals,&source->publication,&source->forced_worlds};
    OutcomeSet *to[]={&image->work,&image->current,&image->raw_results,
        &image->residuals,&image->publication,&image->forced_worlds};
    for (size_t i=0u;i<6u;i++) {
        outcome_set_init(to[i]);
        if (!parent_copy_register(copy,&parent_copy_outcome_kind,from[i],to[i],sizeof(*from[i]))) return false;
    }
    if (source->plan_count>SIZE_MAX/sizeof(*image->plans))return parent_copy_refuse(copy,"PlanExtent");
    image->plans=calloc(source->plan_count,sizeof(*image->plans));
    if (source->plan_count&&!image->plans)return parent_copy_refuse(copy,"Allocation");
    image->plan_count=source->plan_count;
    for(size_t i=0u;i<source->plan_count;i++) {
        outcome_set_init(&image->plans[i].results);
        if(!parent_copy_register(copy,&parent_copy_outcome_kind,
                &source->plans[i].results,&image->plans[i].results,sizeof(OutcomeSet)))return false;
    }
    return true;
}
static bool parent_copy_frame_shell(PrimeParentCopy *copy,
    const PrimeEvalStackFrame *source,PrimeEvalStackFrame **out,bool retired) {
    *out=NULL;
    if(source->context && !copy->retain_reader_contexts)
        return parent_copy_refuse(copy,"FrameContextOwner");
    if(source->control_retired!=retired)
        return parent_copy_refuse(copy,"FrameMembership");
    if(!retired && (source->native_service || source->capsule_search))
        return parent_copy_refuse(copy,"FrameExternalOwner");
    PrimeEvalStackFrame *image=calloc(1u,sizeof(*image));
    if(!image)return parent_copy_refuse(copy,"Allocation");
    *out=image;
    if(!parent_copy_register(copy,&parent_copy_frame_kind,source,image,sizeof(*source)))
        return false;
    image->control_retired=retired;
    if(retired)return true;
    const PrimeEvalStackDriver *driver=parent_copy_source_driver(copy);
    if(!driver || source->control_nested || source->control_nested_operation ||
        source->control_nested_retained || source->dialect_source ||
        !parent_copy_admits_space(copy,source->context,source->space) ||
        source->arena!=driver->arena)
        return parent_copy_refuse(copy,"FrameExternalOwner");
    if(source->child_initialized) {
        outcome_set_init(&image->child);image->child_initialized=true;
        if(!parent_copy_register(copy,&parent_copy_outcome_kind,&source->child,
                &image->child,sizeof(OutcomeSet)))return false;
    }
    if(source->force_pending_initialized) {
        if(source->kind!=PRIME_EVAL_STACK_FRAME_FORCE || source->force_pending.len>1u ||
            (source->force_pending.len && source->index>=source->child.len) ||
            (source->force_pending_observed && !source->force_pending.len))
            return parent_copy_refuse(copy,"ForcePendingExtent");
        outcome_set_init(&image->force_pending);image->force_pending_initialized=true;
        if(!parent_copy_register(copy,&parent_copy_outcome_kind,&source->force_pending,
                &image->force_pending,sizeof(OutcomeSet)))return false;
    } else if(source->force_pending_observed)
        return parent_copy_refuse(copy,"ForcePendingState");
    return parent_copy_equation_shell(
        copy,source->equation_search,&image->equation_search,source->context);
}
static bool parent_copy_frame_shells(PrimeParentCopy *copy,
    const PrimeEvalStackFrame *source, PrimeEvalStackFrame **out, bool retired) {
    PrimeEvalStackFrame **tail=out;
    for(;source;source=source->control_next) {
        PrimeEvalStackFrame *image=NULL;
        if(!parent_copy_frame_shell(copy,source,&image,retired))return false;
        *tail=image;tail=&image->control_next;
    }
    return true;
}
static bool parent_copy_visible(PrimeParentCopy *copy, const MatchVisibleVarSet *source,
    MatchVisibleVarSet *image) {
    *image=*source;image->items=NULL;
    if(source->len>source->cap)return parent_copy_refuse(copy,"VisibleExtent");
    PC_ARRAY(image,source,items,source->cap);
    for(size_t i=0u;i<image->len;i++) {
        PC_ATOM(&image->items[i],name_key);PC_ATOM(&image->items[i],presentation_cache);
        if(!parent_copy_owner(copy,&image->items[i].presentation_arena))return false;
    }
    return true;
}
static bool parent_copy_read_view(PrimeParentCopy *copy,const SpaceReadView *source,SpaceReadView *image) {
    if(source->root.space)
        return space_read_view_clone(source,image)||parent_copy_refuse(copy,"ReadView");
    if(source->bases||source->base_watches||source->base_count||
        source->root_watch.stamp||source->root_watch.expected||
        source->root.instance_id||source->root.revision||
        source->root.prefix_epoch||source->root.base_prefix_epoch||source->root.base_dependency_epoch)
        return parent_copy_refuse(copy,"AbsentReadView");
    *image=(SpaceReadView){0};return true;
}
static bool parent_copy_licence_shells(PrimeParentCopy *copy) {
    PrimeWeightLicence **tail=&copy->image->weight_analyses;
    for(const PrimeWeightLicence *s=copy->source->weight_analyses;s;s=s->next) {
        if(s->control!=copy->source||s->arena!=copy->source->driver.arena)
            return parent_copy_refuse(copy,"LicenceOwner");
        PrimeWeightLicence *d=calloc(1u,sizeof(*d));
        if(!d)return parent_copy_refuse(copy,"Allocation");
        *tail=d;tail=&d->next;
        if(!parent_copy_register(copy,&parent_copy_licence_kind,s,d,sizeof(*s)))return false;
        bindings_init(&d->environment);bindings_init(&d->candidate);
        match_visible_var_set_init(&d->pattern_vars);match_visible_var_set_init(&d->body_vars);
    }
    return true;
}
static bool parent_copy_licence(PrimeParentCopy *copy,const PrimeWeightLicence *s,PrimeWeightLicence *d) {
    PrimeWeightLicence *next=d->next;*d=*s;d->next=next;d->control=copy->image;d->arena=&copy->image->storage;
    d->source=(SpaceReadView){0};d->heads=NULL;d->work=NULL;d->query=NULL;
    memset(&d->environment,0,sizeof(d->environment));memset(&d->candidate,0,sizeof(d->candidate));
    memset(&d->pattern_vars,0,sizeof(d->pattern_vars));memset(&d->body_vars,0,sizeof(d->body_vars));
    if(s->head_count>s->head_capacity||s->work_count>s->work_capacity)return parent_copy_refuse(copy,"LicenceExtent");
    PC_EDGE(d,s,read_owner,parent_copy_frame_kind);
    PC_ATOM(d,algebra);PC_ATOM(d,subject);PC_ATOM(d,times);PC_ATOM(d,refusal);
    PC_ATOM(d,current);PC_ATOM(d,row);PC_ATOM(d,match_call);PC_ATOM(d,grade);
    PC_ARRAY(d,s,heads,s->head_capacity);PC_ARRAY(d,s,work,s->work_capacity);
    for(size_t i=0u;i<s->work_count;i++)if(!parent_copy_atom(copy,&d->work[i].atom))return false;
    if(!parent_copy_environment(copy,&d->environment,&s->environment)||
       !parent_copy_environment(copy,&d->candidate,&s->candidate)||
       !parent_copy_read_view(copy,&s->source,&d->source)||
       !parent_copy_visible(copy,&s->pattern_vars,&d->pattern_vars)||
       !parent_copy_visible(copy,&s->body_vars,&d->body_vars))return false;
    if(s->query && !(d->query=space_query_cursor_copy_to_arena(s->query,&copy->image->storage,copy->atoms)))
        return parent_copy_refuse(copy,"LicenceQueryOwner");
    return true;
}
static bool parent_copy_task(PrimeParentCopy *copy, const PrimeEvalStackTask *source,
    PrimeEvalStackTask *destination) {
    if(source->context && !copy->retain_reader_contexts)
        return parent_copy_refuse(copy,"TaskContextOwner");
    if(source==destination)return parent_copy_refuse(copy,"TaskIdentityOwner");
    __attribute__((cleanup(prime_eval_stack_task_free))) PrimeEvalStackTask staged=*source;
    PrimeEvalStackTask *image=&staged;
    prime_eval_context_retain(image->context);
    memset(&image->dynamic_env,0,sizeof(image->dynamic_env));
    memset(&image->seed_env,0,sizeof(image->seed_env));
    image->dynamic_env_initialized=false;image->seed_env_initialized=false;
    if(!parent_copy_admits_space(copy,source->context,source->space) ||
        !parent_copy_owner(copy,&image->arena))
        return parent_copy_refuse(copy,"TaskSpace");
    PC_EDGE(image,source,target,parent_copy_outcome_kind);
    PC_ATOM(image,atom);PC_ATOM(image,etype);PC_ATOM(image,control_key);
    if(source->dynamic_env_initialized) {
        image->dynamic_env_initialized=true;
        if(!parent_copy_environment(copy,&image->dynamic_env,&source->dynamic_env))return false;
    }
    if(source->seed_env_initialized) {
        image->seed_env_initialized=true;
        if(!parent_copy_environment(copy,&image->seed_env,&source->seed_env))return false;
    }
    *destination=staged;memset(&staged,0,sizeof(staged));
    return true;
}
static bool parent_copy_equation_metadata(PrimeParentCopy *copy,
    const PrimeNeedEquationMetadata *source, PrimeNeedEquationMetadata *image,
    CettaCount length) {
    if (length>source->capacity || (source->capacity && !source->items) ||
        source->capacity>SIZE_MAX/sizeof(*source->items))
        return parent_copy_refuse(copy,"EquationPacketExtent");
    image->items=source->capacity?calloc(source->capacity,sizeof(*image->items)):NULL;
    if (source->capacity&&!image->items)return parent_copy_refuse(copy,"Allocation");
    image->capacity=source->capacity;image->has_observations=source->has_observations;
    for (CettaCount i=0u;i<length;i++) {
        if (source->items[i].kind!=PRIME_NEED_PACKET_BODY &&
            source->items[i].kind!=PRIME_NEED_PACKET_OBSERVATION)
            return parent_copy_refuse(copy,"EquationPacketKind");
        image->items[i]=source->items[i];
    }
    return true;
}

/* These paths contain only coordinates into the separately copied query.
 * Spare slots are uninitialized in the source; preserve their capacity, not
 * their bytes. Install each owner before a fallible descendant allocation. */
static bool parent_copy_pattern_request(PrimeParentCopy *copy,
    const PrimeNeedPatternRequest *source, PrimeNeedPatternRequest *image) {
    if (!prime_need_pattern_request_valid(source))
        return parent_copy_refuse(copy,"ObservationRequestExtent");
    image->path = source->capacity ? calloc(source->capacity,sizeof(*image->path)) : NULL;
    if (source->capacity && !image->path) return parent_copy_refuse(copy,"Allocation");
    image->length=source->length;image->capacity=source->capacity;image->demand=source->demand;
    if (source->length) memcpy(image->path,source->path,source->length*sizeof(*source->path));
    return true;
}
static bool parent_copy_pattern_readiness(PrimeParentCopy *copy,
    const PrimeNeedPatternReadiness *source, PrimeNeedPatternReadiness **out,
    Atom *query) {
    *out=NULL;
    if (!source) return true;
    if (source->length>source->capacity ||
        source->capacity>SIZE_MAX/sizeof(*source->paths) ||
        (source->paths!=NULL)!=(source->capacity!=0u))
        return parent_copy_refuse(copy,"ObservationReadinessExtent");
    PrimeNeedPatternReadiness *image=calloc(1u,sizeof(*image));
    if (!image) return parent_copy_refuse(copy,"Allocation");
    *out=image;
    image->paths=source->capacity?calloc(source->capacity,sizeof(*image->paths)):NULL;
    if (source->capacity&&!image->paths) return parent_copy_refuse(copy,"Allocation");
    image->capacity=source->capacity;
    for (size_t i=0u;i<source->length;i++) {
        const PrimeNeedPatternRequest *path=&source->paths[i];
        if (!prime_need_pattern_request_valid(path) ||
            path->demand!=PRIME_NEED_PATTERN_FULL_VALUE ||
            !prime_need_pattern_request_at(query,path))
            return parent_copy_refuse(copy,"ObservationReadinessPath");
        image->length=i+1u;
        if (!parent_copy_pattern_request(copy,path,&image->paths[i])) return false;
    }
    return true;
}

static bool parent_copy_equation(PrimeParentCopy *copy,
    const PrimeNeedEquationContinuation *source, PrimeNeedEquationContinuation *image) {
    if(!source)return image==NULL;
    Arena *storage=parent_copy_image_storage(copy);
    if(!storage)return parent_copy_refuse(copy,"EquationOwner");
    image->state=source->state;image->space=source->space;image->arena=storage;
    image->original_call=source->original_call;PC_ATOM(image,original_call);
    image->source_occurrence_id=source->source_occurrence_id;
    image->fuel=source->fuel;image->preserve_bindings=source->preserve_bindings;
    image->frontier=source->frontier;image->match_decision_semantics=source->match_decision_semantics;
    if(source->match_decision) {
        CettaMatchDecision *decision=cetta_match_decision_copy_lookup(source->match_decision,copy->atoms);
        if(!decision || !(image->match_decision=cetta_match_decision_retain(decision)))
            return parent_copy_refuse(copy,"EquationDecisionImage");
    }
    const OutcomeSet *from[]={&source->work,&source->current,&source->raw_results,&source->residuals,&source->publication,&source->forced_worlds};
    OutcomeSet *to[]={&image->work,&image->current,&image->raw_results,&image->residuals,&image->publication,&image->forced_worlds};
    /* work_meta is installed before work, so partial cleanup never dereferences
     * an absent table for an already-copied work occurrence. */
    if(source->work.len>source->work_meta_cap)return parent_copy_refuse(copy,"WorkMetaExtent");
    image->work_meta_cap=source->work_meta_cap;
    if(source->work_meta_cap) {
        image->work_meta=calloc(source->work_meta_cap,sizeof(*image->work_meta));
        if(!image->work_meta)return parent_copy_refuse(copy,"Allocation");
    }
    for(size_t i=0u;i<6u;i++)if(!parent_copy_outcomes(copy,from[i],to[i]))return false;
    for(size_t i=0u;i<source->work.len;i++) {
        image->work_meta[i]=source->work_meta[i];
        image->work_meta[i].plan_indices=NULL;image->work_meta[i].observed=NULL;
        PC_ARRAY(&image->work_meta[i],&source->work_meta[i],plan_indices,source->work_meta[i].plan_count);
        if(!parent_copy_pattern_readiness(copy,source->work_meta[i].observed,
            &image->work_meta[i].observed,source->work.items[i].atom))return false;
    }
    image->inspect_incrementally=source->inspect_incrementally;
    image->publication_returns_to_search=source->publication_returns_to_search;
    if(!parent_copy_pattern_request(copy,&source->inspection,&image->inspection) ||
       !parent_copy_pattern_readiness(copy,source->observed,&image->observed,
            source->current.len==1u?source->current.items[0].atom:NULL))return false;
    if(source->inspect_incrementally && source->state==PRIME_NEED_EQUATION_SEARCH_WAIT_FORCE &&
       (source->current.len!=1u ||
        source->inspection.demand==PRIME_NEED_PATTERN_READY ||
        !source->inspection.length ||
        !source->original_call || source->original_call->kind!=ATOM_EXPR ||
        source->original_call->expr.len<=1u ||
        source->force_argument>=source->original_call->expr.len-1u ||
        source->inspection.path[0]!=source->force_argument+1u ||
        !prime_need_pattern_request_at(source->current.items[0].atom,&source->inspection)))
        return parent_copy_refuse(copy,"ObservationRequestPath");
    size_t nargs=source->original_call&&source->original_call->kind==ATOM_EXPR&&source->original_call->expr.len?source->original_call->expr.len-1u:0u;
    bool *demands=NULL;
    if(nargs && source->plan_count) {
        if(source->plan_count>SIZE_MAX/nargs ||
            !parent_copy_array(copy,(void **)&demands,source->plans[0].demand,
                nargs*source->plan_count,sizeof(bool)))return false;
    }
    for(size_t i=0u;i<source->plan_count;i++) {
        PrimeNeedEquationPlan *p=&image->plans[i];const PrimeNeedEquationPlan *s=&source->plans[i];
        p->demand=demands?demands+i*nargs:NULL;
        if(s->demand!=(demands?source->plans[0].demand+i*nargs:NULL))return parent_copy_refuse(copy,"PlanDemandExtent");
        p->equation=s->equation;PC_ATOM(p,equation);
        p->rule_occurrence_id=s->rule_occurrence_id;p->match_source_ref=s->match_source_ref;
        if(!parent_copy_outcomes(copy,&s->results,&p->results))return false;
        if(!parent_copy_equation_metadata(copy,&s->result_metadata,&p->result_metadata,s->results.len))return false;
    }
    if(!parent_copy_equation_metadata(copy,&source->raw_metadata,&image->raw_metadata,source->raw_results.len) ||
       !parent_copy_equation_metadata(copy,&source->publication_metadata,&image->publication_metadata,source->publication.len) ||
       !parent_copy_equation_metadata(copy,&source->residual_metadata,&image->residual_metadata,source->residuals.len) ||
       !parent_copy_equation_metadata(copy,&source->forced_world_metadata,&image->forced_world_metadata,source->forced_worlds.len))return false;
    image->current_plan_count=source->current_plan_count;PC_ARRAY(image,source,current_plan_indices,source->current_plan_count);
    image->current_work_slot=source->current_work_slot;
    image->current_world_order=source->current_world_order;
    image->world_order_extent=source->world_order_extent;
    if(source->current.len && source->current_work_slot>source->work.len)
        return parent_copy_refuse(copy,"CurrentWorkSlot");
    image->pending_plan_count=source->pending_plan_count;PC_ARRAY(image,source,pending_plan_indices,source->pending_plan_count);
    image->current_covered_by_match=source->current_covered_by_match;image->force_argument=source->force_argument;
    image->current_observation_fault=source->current_observation_fault;
    image->current_fault=source->current_fault;
    image->current_coverage_only=source->current_coverage_only;
    image->independent_readers=source->independent_readers;
    PC_EDGE(image,source,cohort_owner,parent_copy_equation_kind);
    image->root_atom_count=source->root_atom_count;PC_ARRAY(image,source,root_atoms,source->root_atom_count);
    for(size_t i=0u;i<image->root_atom_count;i++)if(!parent_copy_atom(copy,&image->root_atoms[i]))return false;
    image->result_contract_count=source->result_contract_count;
    if(source->result_contract_count) {
        if(source->root_atom_count<source->plan_count ||
            source->result_contract_count>source->root_atom_count-source->plan_count ||
            source->result_contracts!=source->root_atoms+source->plan_count)return parent_copy_refuse(copy,"ResultContractExtent");
        image->result_contracts=image->root_atoms+source->plan_count;
    } else if(source->result_contracts)return parent_copy_refuse(copy,"ResultContractExtent");
    image->publication_index=source->publication_index;
    image->publication_raw_with_ref=source->publication_raw_with_ref;
    image->publication_pair_probes=source->publication_pair_probes;
    image->publication_descendants=source->publication_descendants;
    image->publication_fallbacks=source->publication_fallbacks;
    return true;
}
static bool parent_copy_frame(PrimeParentCopy *copy, const PrimeEvalStackFrame *source,
    PrimeEvalStackFrame *image) {
    if(source->context && !copy->retain_reader_contexts)
        return parent_copy_refuse(copy,"FrameContextOwner");
    PrimeEvalStackFrame *next=image->control_next;
    PrimeNeedEquationContinuation *equation=image->equation_search;
    OutcomeSet child=image->child;
    *image=*source;
    /* A local fork shares immutable scope metadata with its retained source.
     * Independent world transport still requires its own context mapping. */
    prime_eval_context_retain(image->context);
    image->control_next=next;
    memset(&image->env,0,sizeof(image->env));image->env_initialized=false;
    memset(&image->child,0,sizeof(image->child));image->child_initialized=false;
    memset(&image->force_pending,0,sizeof(image->force_pending));image->force_pending_initialized=false;
    image->equation_search=equation;image->match_rows=NULL;
    image->normalization_children=NULL;image->control_selection=NULL;image->control_child_bounds=NULL;
    image->dialect_source=NULL;image->control_return_advice=NULL;image->control_nested=NULL;
    image->control_nested_operation=NULL;
    image->native_service=NULL;image->capsule_search=NULL;
    image->call_force_reader=NULL;
    image->force_producer=NULL;image->force_waiters=NULL;
    image->force_waiter_next=NULL;image->open_force_next=NULL;
    if(source->child_initialized&&!source->control_retired) {
        /* The registered embedded object's address stays fixed; an empty
         * OutcomeSet cannot be moved by copying its inline storage pointer. */
        (void)child;outcome_set_init(&image->child);image->child_initialized=true;
    }
    if(source->force_pending_initialized&&!source->control_retired) {
        outcome_set_init(&image->force_pending);image->force_pending_initialized=true;
    }
    PrimeEvalStackFrame *continuation = NULL;
    /* continuation is the first member of a frame, as used by the ordinary
     * driver. Transport the enclosing registered frame, not a link-sized
     * allocation or a borrowed source address. */
    _Static_assert(offsetof(PrimeEvalStackFrame, continuation) == 0u,
        "frame continuation must be the first member");
    if (!parent_copy_edge(copy, &parent_copy_frame_kind,
            (const PrimeEvalStackFrame *)source->continuation.next,
            sizeof(*source), (void **)&continuation)) return false;
    image->continuation.next = continuation ? &continuation->continuation : NULL;
    PC_EDGE(image,source,control_previous,parent_copy_frame_kind);
    PC_EDGE(image,source,control_parent,parent_copy_frame_kind);
    PC_EDGE(image,source,stream_consumer,parent_copy_frame_kind);
    PC_EDGE(image,source,control_advice,parent_copy_advice_kind);
    if(source->call_force_reader&&!(image->call_force_reader=
            prime_need_call_reader_copy_with_session(
                source->call_force_reader,copy->atoms)))
        return parent_copy_refuse(copy,"CallReaderOwner");
    if(source->control_retired) {
        /* Retirement has already released children; their leftover bits are
         * not roots. Never inspect or reconstruct them. */
        image->target=NULL;image->arena=NULL;image->space=NULL;
        image->atom=NULL;image->etype=NULL;image->control_key=NULL;image->control_bound=NULL;
        image->strict_source=NULL;image->then_branch=NULL;image->else_branch=NULL;
        image->weight_subject=NULL;image->weight_algebra=NULL;image->weight_trace=NULL;
        image->ref=NULL;image->heap_owner=NULL;
        memset(&image->cell,0,sizeof(image->cell));
        prime_need_snapshot_init(&image->before);prime_need_snapshot_init(&image->evaluating);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
        image->before_receipt=(PrimeNeedReceipt){0};
#endif
        return true;
    }
    PC_EDGE(image,source,force_producer,parent_copy_frame_kind);
    PC_EDGE(image,source,force_waiters,parent_copy_frame_kind);
    PC_EDGE(image,source,force_waiter_next,parent_copy_frame_kind);
    PC_EDGE(image,source,open_force_next,parent_copy_frame_kind);
    PC_EDGE(image,source,target,parent_copy_outcome_kind);
    if(!parent_copy_owner(copy,&image->arena)||!parent_copy_owner(copy,&image->heap_owner))return false;
    PC_ATOM(image,atom);PC_ATOM(image,etype);PC_ATOM(image,control_key);PC_ATOM(image,control_bound);
    PC_ATOM(image,strict_source);PC_ATOM(image,then_branch);PC_ATOM(image,else_branch);
    PC_ATOM(image,weight_subject);PC_ATOM(image,weight_algebra);PC_ATOM(image,weight_trace);PC_ATOM(image,ref);
    PC_ATOM(&image->control_nested_policy,policy_atom);
    PC_ATOM(&image->control_nested_policy,key_pattern);PC_ATOM(&image->control_nested_policy,key_expression);
    PC_ATOM(&image->control_nested_policy,goal_pattern);PC_ATOM(&image->control_nested_policy,goal_expression);
    PC_ATOM(&image->control_nested_policy,bound_pattern);PC_ATOM(&image->control_nested_policy,bound_expression);
    if(source->env_initialized){image->env_initialized=true;if(!parent_copy_environment(copy,&image->env,&source->env))return false;}
    if(source->child_initialized&&!parent_copy_outcomes(copy,&source->child,&image->child))return false;
    if(source->force_pending_initialized&&
        !parent_copy_outcomes(copy,&source->force_pending,&image->force_pending))return false;
    /* Return scheduling belongs to each physical child occurrence. Preserve
     * the optional array and its links to this parent's existing advice graph,
     * including shared holders whose keys can change after the copy. */
    if(source->control_return_advice_length>source->control_return_advice_capacity ||
        source->control_return_advice_length>source->child.len ||
        (source->control_return_advice_length&&!source->child_initialized))
        return parent_copy_refuse(copy,"ReturnAdviceExtent");
    PC_ARRAY(image,source,control_return_advice,source->control_return_advice_capacity);
    for(size_t i=0u;i<source->control_return_advice_length;i++) {
        PrimeNativeReturnAdvice *d=&image->control_return_advice[i];
        const PrimeNativeReturnAdvice *s=&source->control_return_advice[i];
        PC_ATOM(d,key);PC_ATOM(d,bound);
        PC_EDGE(d,s,advice,parent_copy_advice_kind);
    }
    if(source->control_return_pending) {
        if(source->control_return_pending_begin!=source->control_return_advice_length ||
            source->control_return_pending_end<=source->control_return_pending_begin ||
            source->control_return_pending_end>source->child.len)
            return parent_copy_refuse(copy,"ReturnAdvicePendingExtent");
        PrimeNativeReturnAdvice *d=&image->control_return_pending_advice;
        const PrimeNativeReturnAdvice *s=&source->control_return_pending_advice;
        PC_ATOM(d,key);PC_ATOM(d,bound);
        PC_EDGE(d,s,advice,parent_copy_advice_kind);
    } else if(source->control_return_pending_begin || source->control_return_pending_end ||
        source->control_return_pending_advice.key ||
        source->control_return_pending_advice.bound ||
        source->control_return_pending_advice.advice)
        return parent_copy_refuse(copy,"ReturnAdvicePendingShape");
    PC_ARRAY(image,source,normalization_children,source->normalization_child_count);
    for(size_t i=0u;i<source->normalization_child_count;i++)if(!parent_copy_atom(copy,&image->normalization_children[i]))return false;
    PC_ARRAY(image,source,control_selection,source->control_selection_length);
    PC_ARRAY(image,source,control_child_bounds,source->control_child_bound_capacity);
    if(source->control_child_bound_length>source->control_child_bound_capacity)return parent_copy_refuse(copy,"BoundExtent");
    for(size_t i=0u;i<source->control_child_bound_length;i++)if(!parent_copy_atom(copy,&image->control_child_bounds[i]))return false;
    Arena *storage=parent_copy_image_storage(copy);
    bool before=copy->retain_reader_contexts ||
        prime_need_snapshot_copy_with_session(storage,copy->atoms,&image->before);
    bool evaluating=before && (copy->retain_reader_contexts ||
        prime_need_snapshot_copy_with_session(storage,copy->atoms,&image->evaluating));
    if(!storage || !before || !evaluating)
        return parent_copy_refuse(copy,"FrameNeedOwner");
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if(!copy->retain_reader_contexts &&
        !prime_need_receipt_copy_with_session(storage,copy->atoms,&image->before_receipt))
        return parent_copy_refuse(copy,"FrameReceiptOwner");
#endif
    if(source->kind==PRIME_EVAL_STACK_FRAME_FORCE) {
        PrimeNeedCellView expected;
        const PrimeNeedSnapshot *source_world=source->call_force_reader
            ? bindings_need_view(&source->env):&source->before;
        PrimeNeedSnapshot *image_world=source->call_force_reader
            ? bindings_need_mut(&image->env):&image->before;
        if(!prime_need_snapshot_lookup(source_world,source->thunk_id,&expected)||
            expected.cache_state!=source->cell.cache_state||expected.value_role!=source->cell.value_role||
            expected.value_producer_thunk!=source->cell.value_producer_thunk||
            expected.value_producer_update!=source->cell.value_producer_update||
            expected.update_id!=source->cell.update_id||
            expected.origin!=source->cell.origin||expected.cached!=source->cell.cached||
            expected.authority_id!=source->cell.authority_id||expected.evaluator_id!=source->cell.evaluator_id||expected.storage_key!=source->cell.storage_key||
            expected.import_key!=source->cell.import_key||expected.source_occurrence_id!=source->cell.source_occurrence_id||
            expected.source_argument_index!=source->cell.source_argument_index
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
            ||expected.capture_known!=source->cell.capture_known||expected.capture_var_ids!=source->cell.capture_var_ids||expected.capture_var_count!=source->cell.capture_var_count
#endif
            )return parent_copy_refuse(copy,"ForceCellSource");
        if(!prime_need_snapshot_lookup(image_world,image->thunk_id,&image->cell))
            return parent_copy_refuse(copy,"ForceCellImage");
    } else if(source->cell.origin||source->cell.cached)return parent_copy_refuse(copy,"UnexpectedCellCapture");
    if(source->match_rows) {
        const PrimeMatchRows *s=source->match_rows;
        image->match_rows=calloc(1u,sizeof(*image->match_rows));
        if(!image->match_rows)return parent_copy_refuse(copy,"Allocation");
        PrimeMatchRows *d=image->match_rows;
        d->read=s->read;d->pending_ready=s->pending_ready;d->blocked=s->blocked;d->matched=s->matched;
        if(!parent_copy_visible(copy,&s->visible,&d->visible)||!parent_copy_environment(copy,&d->pending,&s->pending))return false;
        if(s->cursor && !(d->cursor=space_query_cursor_copy_to_arena(s->cursor,storage,copy->atoms)))return parent_copy_refuse(copy,"QueryOwner");
    }
    return parent_copy_equation(copy,source->equation_search,equation);
}
static bool parent_copy_transaction_work(const PrimeNativeControl *,const CettaOwnedContinuation *);
static bool parent_copy_work(PrimeParentCopy *copy,const CettaOwnedContinuation *source,
    CettaOwnedContinuation *out) {
    if(source->provider!=&prime_native_work_provider||!source->payload)return parent_copy_refuse(copy,"WorkProvider");
    const PrimeNativeWork *s=source->payload;
    if(s->owner!=copy->source||s->dialect||s->parallel ||
        (s->resource_step ?
            (!parent_copy_transaction_work(copy->source,source) ||
             cetta_space_agenda_checkpoint_lookup(copy->atoms,s->resource_provider)!=copy->image->space_agenda)
            : s->resource_provider!=NULL))
        return parent_copy_refuse(copy,"WorkOwner");
    PrimeNativeWork *d=calloc(1u,sizeof(*d));if(!d)return parent_copy_refuse(copy,"Allocation");
    *out=(CettaOwnedContinuation){.payload=d,.provider=source->provider};
    *d=*s;d->owner=copy->image;d->task_initialized=false;memset(&d->task,0,sizeof(d->task));d->key_text=NULL;
    if(s->resource_step)d->resource_provider=copy->image->space_agenda;
    PC_EDGE(d,s,parent,parent_copy_frame_kind);PC_EDGE(d,s,frame,parent_copy_frame_kind);PC_EDGE(d,s,advice,parent_copy_advice_kind);PC_EDGE(d,s,licence,parent_copy_licence_kind);
    PC_ATOM(d,key);PC_ATOM(d,bound);
    if(s->key_text&&!parent_copy_array(copy,(void **)&d->key_text,s->key_text,strlen(s->key_text)+1u,1u))return false;
    if(s->task_initialized){d->task_initialized=true;if(!parent_copy_task(copy,&s->task,&d->task))return false;}
    return true;
}

/* Caller prepares IMAGE.storage and an injective map covering all private
 * roots plus explicit borrowed external regions. It keeps the session and
 * source quiescent under one stable library/registry episode. The caller owns
 * SOURCE_RESERVED when it is true; copying never consumes that reservation.
 * IMAGE is empty
 * except for its arena. On failure destroy the unpublished image and session;
 * no source queue, lease, capture or evaluator tick was consumed. */
/* An episode image uses the parent copier's arena and object transport.
 * Its staging owner exists until the image control has an actual holder. */
static const char parent_copy_episode_shell_kind, parent_copy_episode_kind;

static CettaContinuationStatus parent_copy_refusal_status(const char *refusal) {
    if(refusal && (!strcmp(refusal,"ActiveAuthorityScope") ||
        !strcmp(refusal,"ActiveSourceServices")))return CETTA_CONTINUATION_DEFERRED;
    if(refusal && !strcmp(refusal,"AuthorityScopeCapacity"))return CETTA_CONTINUATION_CAPACITY;
    if(refusal && !strcmp(refusal,"AuthorityScopeCycle"))return CETTA_CONTINUATION_INVALIDATED;
    return CETTA_CONTINUATION_UNSUPPORTED;
}

/* The reservation excludes another admitted resume. It does not establish
 * worker or callback quiescence; check those same source conditions before
 * allocating an episode shell and again before the first payload copy. */
static bool parent_copy_quiescent_in_profile(const PrimeNativeControl *source,
    bool source_reserved,bool transaction,const void *driver_owner,
    const char **refusal) {
    if (!source || (!driver_owner&&(!source->owned_arena||
            source->driver.arena!=&source->storage)) ||
        (driver_owner&&(source->owned_arena||!source->driver.arena))) {
        if (refusal) *refusal = "QuiescentOwnerProfile";
        return false;
    }
    CettaContinuationStatus active=prime_native_driver_active_scope_status(&source->driver);
    if(active!=CETTA_CONTINUATION_READY) {
        if(refusal)*refusal=active==CETTA_CONTINUATION_DEFERRED ? "ActiveAuthorityScope" :
            active==CETTA_CONTINUATION_CAPACITY ? "AuthorityScopeCapacity" : "AuthorityScopeCycle";
        return false;
    }
    if(source->source_services && atomic_load_explicit(
            &source->source_services->active,memory_order_acquire)) {
        if(refusal)*refusal="ActiveSourceServices";
        return false;
    }
    if (source->interaction_selection || source->resume_choice) {
        if (refusal) *refusal = "SelectedInteractionOwner";
        return false;
    }
    /* Invocation and authority parents are borrowed drivers. Their stack or
     * service lifetime is not mapped by this checkpoint. Do not discard an
     * unknown grant or silently retain a callback's original context. */
    bool owned_driver=driver_owner&&
        prime_need_production_driver_owned(&source->driver,driver_owner);
    if (source->driver.authority_parent || source->driver.call_permit_parent ||
        (!owned_driver&&(source->driver.call_permit || source->driver.call_permit_context ||
        source->driver.call_refusal_generation ||
        source->driver.call_refusal_retained || source->driver.call_refusal_context ||
        source->driver.observe_space_read || source->driver.space_read_context ||
        source->driver.observe_context_read || source->driver.context_read_context ||
        source->driver.own_data_read || source->driver.own_data_published ||
        source->driver.own_data_context || source->driver.normalization_value_role ||
        source->driver.normalization_role_context))) {
        if (refusal) *refusal = "BorrowedAuthorityScope";
        return false;
    }
    /* These retained protocols have not supplied an injective transport
     * of their producer owners. A shallow copy would alias arrays
     * and copy a live mutex. Refuse before creating an image. */
    if (source->need_claim_count) {
        if (refusal) *refusal = "RetainedProducerClaims";
        return false;
    }
    if (source->need_share_waiting) {
        if (refusal) *refusal = "RetainedProducerShareWait";
        return false;
    }
    if(g_prime_eval_stack_driver==&source->driver||g_prime_eval_top_authority==&source->driver||
        !source->driver.nested||source->driver.running_task||source->driver.active_task||source->driver.root_call_running||source->active_work||
        source->active_allowance||source->active_spent||
        (!driver_owner&&(source->driver.task_ready||source->driver.top||
            source->driver.continuations.top||source->driver.continuations.depth))||
        source->leased!=source_reserved||
        source->parallel_owner||(!transaction&&source->space_agenda)||source->parallel_inflight_count||source->parallel_wave_pending||
        source->reclaimed_frames || source->driver.prepared_opaque_frames ||
        source->driver.prepared_pure_cache.entries||source->driver.prepared_pure_cache.execution_scratch_ready)
        {if(refusal)*refusal="QuiescentOwnerProfile";return false;}
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if(source->history.active || source->history.count > source->history.spec.selections ||
        source->history.reservations > source->history.spec.selections ||
        (!source->history.entries && source->history.count) ||
        (!source->history.spec.enabled && !source->history.stopped && !source->history.forgotten &&
         (source->history.entries || source->history.reservations || source->history.group)))
        {if(refusal)*refusal="RecordingOwner";return false;}
#endif
    return true;
}

static bool parent_copy_quiescent_in(const PrimeNativeControl *source,
    bool source_reserved,bool transaction,const char **refusal) {
    return parent_copy_quiescent_in_profile(source,source_reserved,transaction,
        NULL,refusal);
}

static bool parent_copy_transaction_work(const PrimeNativeControl *source,
    const CettaOwnedContinuation *owned) {
    if (!owned || owned->provider!=&prime_native_work_provider || !owned->payload) return false;
    const PrimeNativeWork *work=owned->payload;
    return work->owner==source && work->resource_step &&
        work->resource_provider==source->space_agenda && !work->parent && !work->frame &&
        !work->task_initialized && !work->licence && !work->dialect && !work->parallel &&
        !work->prepared_publication && !work->scoring && !work->advice &&
        !work->key && !work->bound && !work->key_text;
}

static bool parent_copy_transaction_admitted(const PrimeNativeControl *source,
    bool source_reserved, SpaceCheckpointBudget *budget, const char **refusal) {
    if (!parent_copy_quiescent_in(source,source_reserved,true,refusal)) return false;
    if (!prime_native_transaction_policy(&source->policy) || !source->space_agenda ||
        source->frames || source->retired_frames || source->advice_cells ||
        source->weight_analyses || source->weight_analysis || source->weight_environment_initialized ||
        source->active_advice || source->active_scoring || source->parked_count ||
        source->force_retry.continuation.payload ||
        cetta_continuation_hub_length(&source->hub)>1u) {
        if (refusal) *refusal="TransactionBoundaryProfile";
        return false;
    }
    size_t count=cetta_continuation_hub_length(&source->hub);
    for (size_t i=0u;i<count;i++)
        if (!parent_copy_transaction_work(source,cetta_continuation_hub_at(&source->hub,i))) {
            if (refusal) *refusal="TransactionWorkOwner";
            return false;
        }
    if (!cetta_space_agenda_checkpoint_admitted(source->space_agenda,budget)) {
        if (refusal) *refusal=budget->refusal?budget->refusal:"UnrestorableCut";
        return false;
    }
    return true;
}

static bool parent_copy_quiescent(const PrimeNativeControl *source,
    bool source_reserved, const char **refusal) {
    return parent_copy_quiescent_in(source,source_reserved,false,refusal);
}

static bool parent_copy_episode_admitted(const PrimeNativeControl *control) {
    if (!control) return false;
    const PrimeNativeTopEpisode *source = control->owned_top_episode;
    if (!source) return true;
    if (!arena_retains_owner(&control->storage, source,
            prime_native_top_episode_release)) return false;
    if (source->owned_region || !atomic_load_explicit(&source->references, memory_order_acquire) ||
        !source->ready ||
        !source->request_expression || source->space != control->space ||
        source->library != g_library_context || source->observer || source->observer_context ||
        source->arena.hashcons ||
        (source->request_storage_ready&&source->request_storage.hashcons) ||
        source->regions.depth || source->regions.level_count > source->regions.level_capacity ||
        source->regions.level_capacity > SIZE_MAX / sizeof(*source->regions.levels) ||
        (source->regions.level_capacity && !source->regions.levels) ||
        (source->regions.stable_owner && source->regions.stable_owner != &source->arena))
        return false;
    for (size_t i = 0u; i < source->regions.level_count; i++)
        if (source->regions.levels[i] && source->regions.levels[i]->arena.hashcons)
            return false;
    return true;
}

static void parent_copy_episode_arena(Arena *image, const Arena *source) {
    arena_init_detached(image);
    arena_set_block_capacity(image, source->block_capacity);
    image->block_growth = source->block_growth;
    arena_set_runtime_kind(image, source->runtime_kind);
    arena_set_scalar_cache(image, source->scalar_cache);
}

static PrimeNativeTopEpisode *parent_copy_episode_new(
    const PrimeNativeTopEpisode *source) {
    PrimeNativeTopEpisode *image = calloc(1u, sizeof(*image));
    if (!image) return NULL;
    atomic_init(&image->references, 1u);
    parent_copy_episode_arena(&image->arena, &source->arena);
    image->ready = true;
    if(source->request_storage_ready) {
        parent_copy_episode_arena(&image->request_storage,&source->request_storage);
        image->request_storage_ready=true;
    }
    image->regions.stable_owner = source->regions.stable_owner ? &image->arena : NULL;
    image->regions.level_capacity = source->regions.level_capacity;
    image->regions.level_count = source->regions.level_count;
    if (image->regions.level_capacity) {
        image->regions.levels = calloc(image->regions.level_capacity,
            sizeof(*image->regions.levels));
        if (!image->regions.levels) {
            image->regions.level_count = 0u;
            prime_native_top_episode_release(image);
            return NULL;
        }
    }
    for (size_t i = 0u; i < source->regions.level_count; i++) {
        if (!source->regions.levels[i]) continue;
        PrimeNeedRegionLevel *level = calloc(1u, sizeof(*level));
        if (!level) {
            prime_native_top_episode_release(image);
            return NULL;
        }
        parent_copy_episode_arena(&level->arena, &source->regions.levels[i]->arena);
        level->origin = arena_mark(&level->arena);
        image->regions.levels[i] = level;
    }
    image->space = source->space;
    image->registry = source->registry;
    image->persistent = source->persistent;
    image->library = source->library;
    /* Admission above requires no external observer. */
    return image;
}

static bool parent_copy_episode_capture(const PrimeNativeTopEpisode *source,
    PrimeNativeTopEpisode *image, AtomDeepCopySession *atoms) {
    if (!source || !image || source == image || !image->ready ||
        source->request_storage_ready!=image->request_storage_ready ||
        !atom_deep_copy_session_maps_arena(atoms, &source->arena, &image->arena) ||
        (source->request_storage_ready&&!atom_deep_copy_session_maps_arena(
            atoms,&source->request_storage,&image->request_storage)))
        return false;
    for (size_t i = 0u; i < source->regions.level_count; i++) {
        if (!source->regions.levels[i]) continue;
        if (!image->regions.levels[i] || !atom_deep_copy_session_maps_arena(atoms,
                &source->regions.levels[i]->arena, &image->regions.levels[i]->arena))
            return false;
    }
    /* An incomplete shell cannot be retried or mistaken for a completed
     * episode. The source remains held while these session keys are live. */
    if (atom_deep_copy_session_object_forwarded(atoms,
            &parent_copy_episode_shell_kind, source, sizeof(*source)) ||
        atom_deep_copy_session_object_forwarded(atoms,
            &parent_copy_episode_kind, source, sizeof(*source)) ||
        !atom_deep_copy_session_object_record(atoms,
            &parent_copy_episode_shell_kind, source, image, sizeof(*source)))
        return false;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    const PrimeNeedReceipt *source_receipt = prime_need_saved_context_receipt(&source->context);
    bool has_recording = source->context.value.previous_branch_state.recording ||
        source_receipt->recording;
    bool erase_recording = has_recording && prime_need_recording_copy_erases(atoms);
    if (has_recording && !erase_recording && !prime_need_recording_copy_enable(atoms))
        return false;
    const PrimeNeedReceipt *receipts[] = {
        &source->context.value.previous_branch_state, source_receipt};
    for (size_t i = 0u; i < sizeof(receipts) / sizeof(*receipts); i++) {
        PrimeNeedRecording *group = prime_need_receipt_recording_owner(receipts[i]);
        if (!group || erase_recording) continue;
        if (prime_need_recording_erased(receipts[i])) continue;
        PrimeNeedRecordingCopy *copy = prime_need_recording_copy_lookup(atoms, group);
        /* This episode owns groups through real arena links, not a direct
         * control-style ownership field. Never manufacture that premise. */
        if (!copy || !prime_need_recording_copy_restore_holder(copy,
                &source->arena, &image->arena)) return false;
    }
#endif
    AtomDeepCopyDestinationScope scope = {0};
    Arena *request_destination=image->request_storage_ready
        ? &image->request_storage:&image->arena;
    if (!atom_deep_copy_destination_enter(atoms,request_destination,&scope))
        return false;
    image->request_expression = atom_deep_copy_session_copy(atoms, source->request_expression);
    bool ok = image->request_expression != NULL;
    if (!atom_deep_copy_destination_leave(&scope)) return false;
    if (!ok || !atom_deep_copy_destination_enter(atoms, &image->arena, &scope)) return false;
    image->context.value.previous = source->context.value.previous;
    image->context.value.previous_branch_state = source->context.value.previous_branch_state;
    image->context.value.previous_weights = source->context.value.previous_weights;
    assert(!source->context.value.previous_logical_env);
    image->context.value.previous_logical_env = NULL;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    image->context.value.operations = &g_prime_need_saved_plain_ops;
    if (source_receipt->session_id) {
        image->context.value.previous_receipt = *source_receipt;
        image->context.value.operations = &g_prime_need_carried_origin_ops;
    }
#endif
    ok = prime_need_snapshot_copy_with_session(&image->arena, atoms,
            &image->context.value.previous) &&
        cetta_weight_ledger_copy_with_session(&image->arena, atoms,
            &image->context.value.previous_weights);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    ok = ok && prime_need_receipt_copy_with_session(&image->arena, atoms,
        &image->context.value.previous_branch_state);
    if (ok && source_receipt->session_id) {
        ok = prime_need_receipt_copy_with_session(&image->arena, atoms,
            &image->context.value.previous_receipt);
        if (ok && !image->context.value.previous_receipt.session_id)
            image->context.value.operations = &g_prime_need_saved_plain_ops;
    }
#else
    ok = ok && prime_need_branch_state_copy_with_session(&image->arena, atoms,
        &image->context.value.previous_branch_state);
#endif
    if (!atom_deep_copy_destination_leave(&scope)) return false;
    return ok && atom_deep_copy_session_object_record(atoms,
        &parent_copy_episode_kind, source, image, sizeof(*source));
}

static const char parent_copy_selector_shell_kind, parent_copy_selector_kind;

typedef struct {
    const EquationMatchDecisionCacheSlot *source;
    EquationMatchDecisionCacheSlot *image;
} PrimeParentSelectorImage;

typedef struct {
    PrimeParentSelectorImage *items;
    size_t count;
} PrimeParentSelectorImages;

static const EquationMatchDecisionCacheSlot *parent_copy_selector_at(
    const PrimeNativeControl *source, size_t index) {
    return arena_retained_owner_at(&source->storage,
        equation_match_decision_owner_release, index);
}

/* The union preserves real arena-owner order, then cache position order.
 * A cache slot and a live continuation sharing one selector have one image. */
static const EquationMatchDecisionCacheSlot *parent_copy_selector_union_at(
    const PrimeNativeControl *source, const EquationMatchDecisionCache *services,
    size_t index) {
    size_t owned=0u;
    for (;;) {
        const EquationMatchDecisionCacheSlot *slot=parent_copy_selector_at(source,owned);
        if(!slot)break;
        if(index==owned)return slot;
        if(owned==SIZE_MAX)return NULL;
        owned++;
    }
    index-=owned;
    if(!services)return NULL;
    for(size_t i=0u;i<EQUATION_MATCH_DECISION_CACHE_SLOTS;i++) {
        const EquationMatchDecisionCacheSlot *slot=services->slots[i];
        if(!slot)continue;
        bool seen=arena_retains_owner(&source->storage,slot,equation_match_decision_owner_release);
        for(size_t j=0u;j<i;j++)seen=seen||services->slots[j]==slot;
        if(seen)continue;
        if(!index)return slot;
        index--;
    }
    return NULL;
}

static bool parent_copy_selectors_admitted_in(const PrimeNativeControl *source,
    const EquationMatchDecisionCache *services) {
    for (size_t i = 0u;; i++) {
        const EquationMatchDecisionCacheSlot *slot = parent_copy_selector_union_at(source, services, i);
        if (!slot) break;
        if (!slot->references || !slot->valid || !slot->decision ||
            !slot->pattern_arena_initialized || slot->pattern_arena.hashcons ||
            !slot->equation_count || !slot->plans ||
            slot->equation_count > SIZE_MAX / sizeof(*slot->plans))
            return false;
        if (i == SIZE_MAX) return false;
    }
    for (const PrimeEvalStackFrame *frame = source->frames; frame; frame = frame->control_next) {
        const PrimeNeedEquationContinuation *equation = frame->equation_search;
        if (!equation || !equation->match_decision) continue;
        bool found = false;
        for (size_t i = 0u;; i++) {
            const EquationMatchDecisionCacheSlot *slot = parent_copy_selector_at(source, i);
            if (!slot) break;
            if (slot->decision == equation->match_decision &&
                equation_match_decision_semantics_equal(slot->semantics,
                    equation->match_decision_semantics)) {
                found = true;
                break;
            }
            if (i == SIZE_MAX) return false;
        }
        if (!found) return false;
    }
    return true;
}

static bool parent_copy_selectors_admitted(const PrimeNativeControl *source) {
    return source && (!source->source_services || !atomic_load_explicit(&source->source_services->active, memory_order_acquire)) &&
        parent_copy_selectors_admitted_in(source,source->source_services
            ? &source->source_services->selectors : NULL);
}

static void parent_copy_selectors_free(PrimeParentSelectorImages *images) {
    for (size_t i = 0u; i < images->count; i++)
        equation_match_decision_owner_release(images->items[i].image);
    free(images->items);
    *images = (PrimeParentSelectorImages){0};
}

static bool parent_copy_selectors_new(const PrimeNativeControl *source,
    const EquationMatchDecisionCache *services, size_t limit,
    PrimeParentSelectorImages *images) {
    size_t count = 0u;
    while (parent_copy_selector_union_at(source,services,count)) {
        if (count == limit || count == SIZE_MAX / sizeof(*images->items)) return false;
        count++;
    }
    if (!count) return true;
    images->items = calloc(count, sizeof(*images->items));
    if (!images->items) return false;
    images->count = count;
    for (size_t i = 0u; i < count; i++) {
        const EquationMatchDecisionCacheSlot *s = parent_copy_selector_union_at(source,services,i);
        EquationMatchDecisionCacheSlot *d = calloc(1u, sizeof(*d));
        if (!d) return false;
        images->items[i] = (PrimeParentSelectorImage){s,d};
        d->references = 1u;
        parent_copy_episode_arena(&d->pattern_arena, &s->pattern_arena);
        d->pattern_arena_initialized = true;
    }
    return true;
}

static bool parent_copy_selectors_live(const PrimeNativeControl *source,
    const EquationMatchDecisionCache *services, const PrimeParentSelectorImages *images) {
    if (!parent_copy_selectors_admitted_in(source,services)) return false;
    for (size_t i = 0u; i < images->count; i++)
        if (images->items[i].source != parent_copy_selector_union_at(source,services,i) ||
            !images->items[i].image || !images->items[i].image->pattern_arena_initialized)
            return false;
    return parent_copy_selector_union_at(source,services,images->count) == NULL;
}

static bool parent_copy_selector_capture(const EquationMatchDecisionCacheSlot *s,
    EquationMatchDecisionCacheSlot *d, AtomDeepCopySession *atoms) {
    if (!s || !d || s == d || d->valid || d->decision || d->plans ||
        !atom_deep_copy_session_maps_arena(atoms, &s->pattern_arena, &d->pattern_arena) ||
        atom_deep_copy_session_object_forwarded(atoms,
            &parent_copy_selector_shell_kind,s,sizeof(*s)) ||
        !atom_deep_copy_session_object_record(atoms,
            &parent_copy_selector_shell_kind,s,d,sizeof(*s)))
        return false;
    d->equation_projection=s->equation_projection;
    d->semantics=s->semantics;d->head=s->head;d->mode=s->mode;
    d->guard_refinement_count=s->guard_refinement_count;
    d->plans=calloc(s->equation_count,sizeof(*d->plans));
    if (!d->plans) return false;
    d->equation_count=s->equation_count;
    for(size_t i=0u;i<s->equation_count;i++) {
        const HeEquationMatchPlan *p=&s->plans[i];
        HeEquationMatchPlan *q=&d->plans[i];
        q->consumed_guard_count=p->consumed_guard_count;
#define PC_SELECTOR_ATOM(f) do { q->f=p->f?atom_deep_copy_session_copy(atoms,p->f):NULL; if(p->f&&!q->f)return false; } while(0)
        PC_SELECTOR_ATOM(equation);
        PC_SELECTOR_ATOM(match_pattern);
        PC_SELECTOR_ATOM(result_template);
        PC_SELECTOR_ATOM(refinement_fallback_pattern);
        PC_SELECTOR_ATOM(refinement_failure_template);
#undef PC_SELECTOR_ATOM
        if(p->frame_schema) {
            q->frame_schema=bindings_frame_schema_copy_with_session(p->frame_schema,atoms);
            if(!q->frame_schema)return false;
        }
    }
    d->decision=cetta_match_decision_copy_with_session(s->decision,atoms);
    if(!d->decision)return false;
    d->valid=s->valid;
    return atom_deep_copy_session_object_record(atoms,
        &parent_copy_selector_kind,s,d,sizeof(*s));
}

static bool parent_copy_selectors_capture(const PrimeNativeControl *source,
    PrimeNativeControl *image, const EquationMatchDecisionCache *services,
    PrimeParentSelectorImages *images, AtomDeepCopySession *atoms) {
    if(!parent_copy_selectors_live(source,services,images))return false;
    /* Retains prepend to the actual owner chain; reverse capture keeps
     * the source chain and normal destruction order. */
    for(size_t remaining=images->count;remaining>0u;remaining--) {
        size_t i=remaining-1u;
        const EquationMatchDecisionCacheSlot *s=images->items[i].source;
        EquationMatchDecisionCacheSlot *d=images->items[i].image;
        if(!parent_copy_selector_capture(s,d,atoms))return false;
        if(arena_retains_owner(&source->storage,s,equation_match_decision_owner_release) &&
            !arena_retain_owner_with_storage(&image->storage,d,
                equation_match_decision_owner_retain,
                equation_match_decision_owner_release,
                equation_match_decision_owner_storage))return false;
    }
    if(services) {
        if(!image->source_services)return false;
        EquationMatchDecisionCache *target=&image->source_services->selectors;
        target->clock=services->clock;
        for(size_t i=0u;i<EQUATION_MATCH_DECISION_CACHE_SLOTS;i++) {
            target->used[i]=services->used[i];
            if(!services->slots[i])continue;
            EquationMatchDecisionCacheSlot *mapped=atom_deep_copy_session_object_forwarded(
                atoms,&parent_copy_selector_kind,services->slots[i],sizeof(*mapped));
            if(!mapped||target->slots[i])return false;
            equation_match_decision_owner_retain(mapped);
            target->slots[i]=mapped;
        }
    }
    return true;
}

static bool parent_copy_selectors_transported(const PrimeNativeControl *source,
    const PrimeNativeControl *image,const Arena *image_storage,
    const AtomDeepCopySession *atoms) {
    for(size_t i=0u;;i++) {
        const EquationMatchDecisionCacheSlot *s=parent_copy_selector_at(source,i);
        if(!s)return true;
        const EquationMatchDecisionCacheSlot *d=atom_deep_copy_session_object_forwarded(
            atoms,&parent_copy_selector_kind,s,sizeof(*s));
        if(!d || d==s || !d->valid ||
            !arena_retains_owner(image_storage,d,equation_match_decision_owner_release) ||
            cetta_match_decision_copy_lookup(s->decision,atoms)!=d->decision)
            return false;
        if(i==SIZE_MAX)return false;
    }
}

static bool prime_native_parent_copy_with_session_profile(const PrimeNativeControl *source,
    PrimeNativeControl *image, AtomDeepCopySession *atoms, bool source_reserved,
    SpaceCheckpointBudget *transaction,const void *driver_owner,
    Arena *external_image_storage,
    const char **refusal) {
    const Arena *source_storage=driver_owner?source->driver.arena:&source->storage;
    Arena *image_storage=external_image_storage?external_image_storage:&image->storage;
    PrimeParentCopy state={.source=source,.image=image,.atoms=atoms,
        .source_driver=&source->driver,.image_driver=&image->driver,
        .source_space=source->space,.image_storage=image_storage},*copy=&state;
    if(refusal)*refusal=NULL;
    if(!source||!image||source==image||!source_storage||!image_storage||
        !atom_deep_copy_session_targets(atoms,image_storage)||
        !atom_deep_copy_session_maps_arena(atoms,source_storage,image_storage))return false;
    if (transaction ? !parent_copy_transaction_admitted(source,source_reserved,transaction,refusal)
                    : !parent_copy_quiescent_in_profile(source,source_reserved,
                        false,driver_owner,refusal)) return false;
    if ((source->source_services && !image->source_services) ||
        !parent_copy_selectors_admitted(source) ||
        !parent_copy_selectors_transported(source,image,image_storage,atoms)) {
        if(refusal)*refusal="EquationSelectorOwner";
        return false;
    }
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    bool erase_recording = (source->history.spec.enabled || source->history.stopped) &&
        prime_need_recording_copy_erases(atoms);
    PrimeNeedRecordingCopy *recording = !erase_recording && !source->history.forgotten && source->history.group
        ? prime_need_recording_copy_lookup(atoms, source->history.group) : NULL;
    if (source->history.group && !source->history.forgotten && !erase_recording && !recording)
        {if(refusal)*refusal="RecordingOwner";return false;}
#endif
    PrimeNativeTopEpisode *episode_image = NULL;
    if (source->owned_top_episode) {
        episode_image = atom_deep_copy_session_object_forwarded(atoms,
            &parent_copy_episode_kind, source->owned_top_episode, sizeof(*episode_image));
        if (!episode_image || !parent_copy_episode_admitted(source)) {
            if (refusal) *refusal = "OwnedTopEpisodeImage";
            return false;
        }
    }
    Arena destination=image->storage;
    PrimeNativeSourceServices *services=image->source_services;
    *image=*source;image->storage=destination;image->source_services=services;
    image->force_retry=(PrimeNativeForceRetry){0};
    image->need_claims=NULL;image->need_claim_count=0u;image->need_claim_cap=0u;
    image->need_claim_mu_ready=false;
    memset(&image->need_claim_mu,0,sizeof(image->need_claim_mu));
    image->owned_arena=external_image_storage==NULL;
    image->handle_id=0u;image->leased=false;
    image->owned_top_episode=NULL;image->owns_execution_episode=false;
    image->space_agenda=NULL;
    image->interaction_selection=NULL;
    image->resume_choice=NULL;
    memset(&image->hub,0,sizeof(image->hub));image->frames=NULL;image->retired_frames=NULL;
    image->reclaimed_frames=NULL;
    memset(image->open_force_buckets,0,sizeof(image->open_force_buckets));
    image->changed_frames=NULL;image->frame_index=NULL;image->advice_cells=NULL;image->weight_analyses=NULL;image->weight_analysis=NULL;
    image->parked=NULL;image->parked_count=0;image->parallel_inflight=NULL;
    image->next_pick=NULL;image->selected=NULL;image->costs=NULL;image->delivered=NULL;
    image->consumer_revisions=NULL;image->parallel_waves=NULL;image->effect_barriers=NULL;image->need_barriers=NULL;
    image->receipt_intervals=NULL;image->query_reads=NULL;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    image->history.group=NULL;image->history.entries=NULL;image->history.active=NULL;
    if (erase_recording || source->history.forgotten) {
        image->history=(PrimeNativeHistory){0};
        image->policy.history=(PrimeNativeHistorySpec){0};
    }
#endif
    image->event_cost.events=NULL;image->event_cost.length=0;image->event_cost.capacity=0;
    image->base_environment_initialized=false;memset(&image->base_environment,0,sizeof(image->base_environment));
    image->weight_environment_initialized=false;memset(&image->weight_environment,0,sizeof(image->weight_environment));
    memset(&image->weight_source,0,sizeof(image->weight_source));memset(&image->registry_read,0,sizeof(image->registry_read));
    outcome_set_init(&image->output);
    image->driver.control=image;image->driver.arena=image_storage;
    image->driver.authority_parent=NULL;image->driver.call_permit_parent=NULL;
    image->driver.call_permit=NULL;image->driver.call_permit_context=NULL;
    image->driver.call_refusal_generation=NULL;
    image->driver.call_refusal_retained=NULL;
    image->driver.call_refusal_context=NULL;
    image->driver.observe_space_read=NULL;image->driver.space_read_context=NULL;
    image->driver.observe_context_read=NULL;image->driver.context_read_context=NULL;
    image->driver.own_data_read=NULL;image->driver.own_data_published=NULL;
    image->driver.own_data_context=NULL;
    image->driver.normalization_value_role=NULL;
    image->driver.normalization_role_context=NULL;
    image->driver.root_target=NULL;image->driver.stream_pending=NULL;
    image->driver.top=NULL;image->driver.continuations=(CettaOwnedFrameStack){0};
    image->driver.task_ready=false;image->driver.running_task=false;
    image->driver.active_task=NULL;image->driver.root_call_running=false;
    memset(&image->driver.task,0,sizeof(image->driver.task));
    image->driver.prepared_pure_cache=source->driver.prepared_pure_cache;
    image->driver.gc_anchor=arena_mark(image_storage);
    bool ok=parent_copy_register(copy,&parent_copy_outcome_kind,&source->output,&image->output,sizeof(OutcomeSet));
    if(ok) {
        image->need_claim_mu_ready=pthread_mutex_init(&image->need_claim_mu,NULL)==0;
        if(!image->need_claim_mu_ready)ok=parent_copy_refuse(copy,"NeedClaimMutex");
    }
    if(ok && source->space_agenda && (!transaction ||
        !cetta_space_agenda_checkpoint_copy(source->space_agenda,image_storage,atoms,
            &image->space_agenda,transaction)))
        ok=parent_copy_refuse(copy,transaction&&transaction->refusal?transaction->refusal:"TransactionImage");
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if(ok && !erase_recording && (source->history.spec.enabled || source->history.stopped) &&
        !prime_need_recording_copy_enable(atoms))
        ok=parent_copy_refuse(copy,"RecordingMode");
    /* The actual control owns history.group directly. Its carrier borrows
     * last until that control retires; no extra paid arena hold is implied.
     * A real additional source arena hold is restored separately. */
    if(ok && recording) {
        ok=prime_need_recording_copy_enclosing_owner(recording,
            source_storage,image_storage);
        if(ok && arena_retains_owner(source_storage,source->history.group,
                prime_need_recording_release))
            ok=prime_need_recording_copy_restore_holder(recording,
                source_storage,image_storage);
        if(!ok)parent_copy_refuse(copy,"RecordingLifetime");
    }
    if(ok && !erase_recording && source->history.entries)
        ok=parent_copy_array(copy,(void **)&image->history.entries,source->history.entries,
            source->history.spec.selections,sizeof(*source->history.entries));
    for(size_t i=0u;ok && i<image->history.count;i++) {
        PrimeNativeHistorySelection *entry=&image->history.entries[i];
        ok=prime_need_receipt_copy_with_session(image_storage,atoms,&entry->operation) &&
            prime_need_receipt_copy_with_session(image_storage,atoms,&entry->prepared);
        if(!ok)parent_copy_refuse(copy,"RecordingSelection");
    }
#endif
    PrimeNativeAdvice **tail=&image->advice_cells;
    for(const PrimeNativeAdvice *s=source->advice_cells;ok&&s;s=s->next) {
        PrimeNativeAdvice *d=calloc(1u,sizeof(*d));if(!d){ok=parent_copy_refuse(copy,"Allocation");break;}
        *tail=d;tail=&d->next;
        ok=parent_copy_register(copy,&parent_copy_advice_kind,s,d,sizeof(*s));
    }
    ok=ok&&parent_copy_frame_shells(copy,source->frames,&image->frames,false)&&
        parent_copy_frame_shells(copy,source->retired_frames,&image->retired_frames,true)&&parent_copy_licence_shells(copy);
    const PrimeNativeAdvice *sa=source->advice_cells;
    for(PrimeNativeAdvice *d=image->advice_cells;ok&&d;d=d->next,sa=sa->next) {
        PrimeNativeAdvice *next=d->next;*d=*sa;d->next=next;d->key_text=NULL;memset(&d->environment,0,sizeof(d->environment));
        ok=parent_copy_environment(copy,&d->environment,&sa->environment)&&
            parent_copy_edge(copy,&parent_copy_advice_kind,sa->inherited,sizeof(*sa),(void **)&d->inherited)&&
            parent_copy_edge(copy,&parent_copy_advice_kind,sa->follows,sizeof(*sa),(void **)&d->follows)&&
            parent_copy_atom(copy,&d->subject)&&parent_copy_atom(copy,&d->factor)&&parent_copy_atom(copy,&d->coefficient)&&parent_copy_atom(copy,&d->key);
        if(ok&&sa->key_text)ok=parent_copy_array(copy,(void **)&d->key_text,sa->key_text,strlen(sa->key_text)+1u,1u);
    }
    const PrimeEvalStackFrame *sf=source->frames;
    for(PrimeEvalStackFrame *d=image->frames;ok&&d;d=d->control_next,sf=sf->control_next)ok=parent_copy_frame(copy,sf,d);
    sf=source->retired_frames;
    for(PrimeEvalStackFrame *d=image->retired_frames;ok&&d;d=d->control_next,sf=sf->control_next)ok=parent_copy_frame(copy,sf,d);
    if(ok&&source->driver.task_ready) {
        ok=parent_copy_task(copy,&source->driver.task,&image->driver.task);
        image->driver.task_ready=ok;
    }
    if(ok&&source->driver.top)
        ok=parent_copy_edge(copy,&parent_copy_frame_kind,source->driver.top,
            sizeof(*source->driver.top),(void **)&image->driver.top);
    if(ok&&source->driver.continuations.top) {
        PrimeEvalStackFrame *top=NULL;
        ok=parent_copy_edge(copy,&parent_copy_frame_kind,
            (const PrimeEvalStackFrame *)source->driver.continuations.top,
            sizeof(PrimeEvalStackFrame),(void **)&top);
        if(ok)image->driver.continuations.top=&top->continuation;
    }
    if(ok) {
        image->driver.continuations.depth=source->driver.continuations.depth;
    }
    /* The lookup and producer/waiter links name this parent's registered
     * frames. Reusing a source bucket would outlive its retired owner. */
    for(size_t i=0u;ok&&i<sizeof(source->open_force_buckets)/sizeof(*source->open_force_buckets);i++)
        ok=parent_copy_edge(copy,&parent_copy_frame_kind,source->open_force_buckets[i],
            sizeof(PrimeEvalStackFrame),(void **)&image->open_force_buckets[i]);
    const PrimeWeightLicence *sl=source->weight_analyses;
    for(PrimeWeightLicence *d=image->weight_analyses;ok&&d;d=d->next,sl=sl->next)ok=parent_copy_licence(copy,sl,d);
    if(ok&&source->base_environment_initialized){image->base_environment_initialized=true;ok=parent_copy_environment(copy,&image->base_environment,&source->base_environment);}
    if(ok&&source->weight_environment_initialized){image->weight_environment_initialized=true;ok=parent_copy_environment(copy,&image->weight_environment,&source->weight_environment);}
    if(ok)ok=parent_copy_outcomes(copy,&source->output,&image->output);
    if(ok&&!cetta_weight_ledger_copy_with_session(image_storage,atoms,&image->weight_entry))
        ok=parent_copy_refuse(copy,"WeightEntryOwner");
    if(ok)ok=parent_copy_read_view(copy,&source->weight_source,&image->weight_source);
    if(ok)ok=parent_copy_registry_read_view(
        copy,&source->registry_read,&image->registry_read);
#define PA(f) if(ok)ok=parent_copy_atom(copy,&image->f)
    PA(active_key);PA(active_bound);PA(pending_effect);PA(source_expression);PA(weight_algebra);PA(weight_subject);
    PA(weight_coefficient_variable);PA(weight_factor_variable);PA(weight_refusal);PA(weight_frontier_bound);PA(weight_selected_bound);
    PA(policy.policy_atom);PA(policy.key_pattern);PA(policy.key_expression);PA(policy.goal_pattern);PA(policy.goal_expression);
    PA(policy.bound_pattern);PA(policy.bound_expression);
    PA(policy.breakpoint_pattern);
#undef PA
#define PE(f,k) if(ok)ok=parent_copy_edge(copy,&k,source->f,sizeof(*source->f),(void **)&image->f)
    PE(active_advice,parent_copy_advice_kind);PE(weight_analysis,parent_copy_licence_kind);PE(weight_read_frame,parent_copy_frame_kind);PE(weight_descriptor_frame,parent_copy_frame_kind);
    PE(driver.root_target,parent_copy_outcome_kind);PE(driver.stream_pending,parent_copy_outcome_kind);
#undef PE
#define PAR(f,n) if(ok)ok=parent_copy_array(copy,(void **)&image->f,source->f,n,sizeof(*source->f))
    PAR(changed_frames,source->changed_capacity);
    if(ok)for(size_t i=0;i<source->changed_count;i++)if(!parent_copy_edge(copy,&parent_copy_frame_kind,source->changed_frames[i],sizeof(PrimeEvalStackFrame),(void **)&image->changed_frames[i])){ok=false;break;}
    PAR(frame_index,source->frame_index_capacity);
    if(ok&&image->frame_index){memset(image->frame_index,0,source->frame_index_capacity*sizeof(*image->frame_index));for(size_t i=0;i<source->frame_index_capacity;i++)if(source->frame_index[i]){
        PrimeEvalStackFrame *f=NULL;if(!parent_copy_edge(copy,&parent_copy_frame_kind,source->frame_index[i],sizeof(*f),(void **)&f)){ok=false;break;}prime_native_frame_index_place(image->frame_index,source->frame_index_capacity,f);}}
    PAR(query_reads,source->query_read_capacity);PAR(delivered,source->delivered_capacity);
    if(source->next_pick) {PAR(next_pick,source->goal_size);}
    PAR(selected,source->selected_length);PAR(costs,source->costs_capacity);PAR(consumer_revisions,source->consumer_revision_count);
    PAR(parallel_waves,source->parallel_wave_count);PAR(effect_barriers,source->effect_barrier_count);PAR(need_barriers,source->need_barrier_count);
    PAR(receipt_intervals,source->receipt_interval_count);
    PAR(event_cost.events,source->event_cost.capacity);
#undef PAR
    if(ok){image->event_cost.length=source->event_cost.length;image->event_cost.capacity=source->event_cost.capacity;}
    struct {Atom **items;size_t count;} vectors[]={
        {image->costs,source->costs_capacity},{image->consumer_revisions,source->consumer_revision_count},
        {image->parallel_waves,source->parallel_wave_count},{image->effect_barriers,source->effect_barrier_count},{image->need_barriers,source->need_barrier_count}};
    for(size_t k=0u;ok&&k<sizeof(vectors)/sizeof(*vectors);k++)for(size_t i=0u;i<vectors[k].count;i++)if(!parent_copy_atom(copy,&vectors[k].items[i])){ok=false;break;}
    size_t queued=cetta_continuation_hub_length(&source->hub);
    size_t retry_count=source->force_retry.continuation.payload ? 1u : 0u;
    size_t count=queued+source->parked_count;
    CettaContinuationTransport *transport=NULL;
    if(ok&&(count<queued||retry_count>SIZE_MAX-count))ok=parent_copy_refuse(copy,"FrontierExtent");
    if(ok)count+=retry_count;
    if(ok&&count>SIZE_MAX/sizeof(*transport))ok=parent_copy_refuse(copy,"FrontierExtent");
    if(ok&&count){transport=calloc(count,sizeof(*transport));if(!transport)ok=parent_copy_refuse(copy,"Allocation");}
    for(size_t i=0;ok&&i<count;i++) {
        transport[i].source=i<queued ? cetta_continuation_hub_at(&source->hub,i)
            : i<queued+source->parked_count ? &source->parked[i-queued]
            : &source->force_retry.continuation;
        ok=parent_copy_work(copy,transport[i].source,&transport[i].prepared);
    }
    if(ok&&source->parked_capacity){image->parked=calloc(source->parked_capacity,sizeof(*image->parked));if(!image->parked)ok=parent_copy_refuse(copy,"Allocation");}
    if(ok&&cetta_continuation_hub_copy_prepared(&source->hub,transport,count,&image->hub)!=CETTA_CONTINUATION_READY)
        ok=parent_copy_refuse(copy,"HubTransport");
    if(ok){image->parked_count=source->parked_count;for(size_t i=0;i<source->parked_count;i++){image->parked[i]=transport[queued+i].prepared;memset(&transport[queued+i].prepared,0,sizeof(transport[queued+i].prepared));}}
    if(ok) {
        image->force_retry.admitted=source->force_retry.admitted;
        if(retry_count) {
            size_t index=queued+source->parked_count;
            image->force_retry.continuation=transport[index].prepared;
            memset(&transport[index].prepared,0,sizeof(transport[index].prepared));
        }
    }
    for(size_t i=0;i<count&&transport;i++)cetta_owned_continuation_destroy(&transport[i].prepared);
    free(transport);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    /* Publish the direct ownership reference only after every parent edge
     * and carrier has been copied. Refusal leaves it in the staged owner. */
    if(ok && recording) {
        ok=prime_need_recording_copy_lookup(atoms,source->history.group)==recording;
        if(ok)image->history.group=prime_need_recording_copy_take(recording);
        if(!ok || !image->history.group)ok=parent_copy_refuse(copy,"RecordingCapture");
    }
#endif
    if (ok && episode_image) {
        ok = arena_retain_owner_with_storage(image_storage, episode_image,
            prime_native_top_episode_retain, prime_native_top_episode_release,
            prime_native_top_episode_storage);
        if (ok) {
            image->owned_top_episode=episode_image;
            image->owns_execution_episode=source->owns_execution_episode;
        }
        else parent_copy_refuse(copy, "EpisodeOwnership");
    }
    if(refusal)*refusal=state.refusal;
    return ok;
}

static bool prime_native_parent_copy_with_session(const PrimeNativeControl *source,
    PrimeNativeControl *image,AtomDeepCopySession *atoms,bool source_reserved,
    SpaceCheckpointBudget *transaction,const char **refusal) {
    return prime_native_parent_copy_with_session_profile(source,image,atoms,
        source_reserved,transaction,NULL,NULL,refusal);
}
#undef PC_ATOM
#undef PC_ARRAY
#undef PC_EDGE


struct CettaPrimeCheckpointCopy {
    CettaLibraryContext *context;
    PrimeNativeControl *source;
    PrimeNativeControl *image;
    uint64_t source_handle;
    uint64_t context_watch;
    Arena holder;
    Atom *source_holder;
    AtomDeepCopyArenaPair pair;
    PrimeNativeTopEpisode *episode_image;
    PrimeNeedEpisodeCopyPlan *episode_calls;
    PrimeParentSelectorImages selectors;
    const EquationMatchDecisionCache *service_selectors;
    EquationMatchDecisionCache service_selection_at_capture;
    bool ambient_services;
    bool transaction_boundary,source_was_reserved,keep_target_keys;
    SpaceCheckpointBudget transaction_budget;
    SpaceReadToken source_target,image_target;
    SpaceCheckpointIssuance target_before,target_after;
    bool target_complete;
    bool reserved;
    bool keep_source_reserved;
    bool attempted;
    bool prepared;
    CettaContinuationStatus status;
    const char *refusal;
};

static bool prime_checkpoint_source_live(const CettaPrimeCheckpointCopy *copy) {
    return copy && copy->context && copy->source &&
        cetta_native_handle_get(copy->context, "prime-search", copy->source_handle) == copy->source;
}

static void prime_checkpoint_unreserve(CettaPrimeCheckpointCopy *copy) {
    if (copy->reserved && prime_checkpoint_source_live(copy))
        copy->source->leased = copy->source_was_reserved;
    copy->reserved = false;
}

/* The registry detaches before calling this hook, while library/Space/program
 * dependencies are still live. The caller owns only the now-invalid shell. */
static void prime_checkpoint_context_retire(void *raw) {
    CettaPrimeCheckpointCopy *copy = raw;
    prime_checkpoint_unreserve(copy);
    prime_native_control_destroy(copy->image);
    copy->image = NULL;
    parent_copy_selectors_free(&copy->selectors);
    prime_need_episode_copy_plan_free(copy->episode_calls);
    copy->episode_calls = NULL;
    if (copy->episode_image) prime_native_top_episode_release(copy->episode_image);
    copy->episode_image = NULL;
    copy->source = NULL;
    copy->context = NULL;
    copy->context_watch = 0u;
    copy->prepared = false;
    copy->attempted = true;
    copy->status = CETTA_CONTINUATION_INVALIDATED;
    copy->refusal = "ContextRetired";
}

static void prime_checkpoint_storage(const void *raw, CettaStorageCensus *census) {
    const CettaPrimeCheckpointCopy *copy = raw;
    if (!cetta_storage_object(census, copy, sizeof(*copy), CETTA_STORAGE_BLOCK)) return;
    arena_storage_census(&copy->holder, census);
    prime_native_control_storage_census(copy->image, census);
    cetta_storage_array(census,copy->selectors.items,copy->selectors.count,
        sizeof(*copy->selectors.items),0u);
    for(size_t i=0u;i<copy->selectors.count;i++)
        if(copy->selectors.items[i].image)
            equation_match_decision_owner_storage(copy->selectors.items[i].image,census);
    if (copy->episode_image) prime_native_top_episode_storage(copy->episode_image, census);
    prime_need_episode_copy_plan_storage(copy->episode_calls,census);
}

static CettaPrimeCheckpointCopy *prime_checkpoint_copy_new_profile(
    CettaLibraryContext *context, Atom *residual, CettaContinuationStatus *status,
    bool capture_services, size_t service_limit, bool transaction,
    bool source_reserved, bool keep_target_keys, size_t bytes) {
    if (status) *status = CETTA_CONTINUATION_UNSUPPORTED;
    if (!context || context != g_library_context ||
        eval_current_language_id() != CETTA_LANGUAGE_PRIME)
        return NULL;
    uint64_t id = 0u;
    if (!cetta_native_handle_arg(residual, "prime-search", &id)) return NULL;
    PrimeNativeControl *source = cetta_native_handle_get_owned(context, "prime-search", residual);
    if (!source) { if (status) *status = CETTA_CONTINUATION_INVALIDATED; return NULL; }
    if (source->leased!=source_reserved || g_prime_eval_stack_driver == &source->driver ||
        g_prime_eval_top_authority == &source->driver) {
        if (status) *status = CETTA_CONTINUATION_DEFERRED;
        return NULL;
    }
    const EquationMatchDecisionCache *services=source->source_services
        ? &source->source_services->selectors
        : capture_services ? equation_match_cache_current() : NULL;
    const SpaceExecutionAnalysisCache *effects=source->source_services
        ? source->source_services->effects
        : capture_services ? space_execution_analysis_cache_current() : NULL;
    SpaceCheckpointBudget transaction_budget={.work_limit=service_limit,.byte_limit=bytes};
    const char *admission_refusal=NULL;
    bool admitted=transaction
        ? parent_copy_transaction_admitted(source,source_reserved,&transaction_budget,&admission_refusal)
        : parent_copy_quiescent(source,source_reserved,&admission_refusal);
    if(!admitted) {
        if(status)*status=parent_copy_refusal_status(admission_refusal);
        return NULL;
    }
    if (!parent_copy_episode_admitted(source) ||
        !parent_copy_selectors_admitted_in(source,services) ||
        (services && !space_execution_analysis_cache_admitted(effects,service_limit))) return NULL;
    SpaceReadToken source_target={0};
    SpaceCheckpointIssuance target_before={0};
    if(transaction) {
        source_target=space_read_token(cetta_space_agenda_checkpoint_target(source->space_agenda));
        target_before=space_checkpoint_issuance();
        if(!source_target.instance_id || source_target.instance_id>=target_before.instance ||
            !source_target.prefix_epoch || source_target.prefix_epoch>=target_before.prefix ||
            target_before.instance==UINT64_MAX || target_before.prefix==UINT64_MAX)
            return NULL;
    }
    CettaPrimeCheckpointCopy *copy = calloc(1u, sizeof(*copy));
    PrimeNativeControl *image = calloc(1u, sizeof(*image));
    if (!copy || !image) {
        free(copy); free(image);
        if (status) *status = CETTA_CONTINUATION_CAPACITY;
        return NULL;
    }
    arena_init(&copy->holder);
    arena_init(&image->storage);
    image->owned_arena = true;
    outcome_set_init(&image->output);
    copy->context = context;
    copy->source = source;
    copy->source_handle = id;
    copy->image = image;
    copy->pair = (AtomDeepCopyArenaPair){&source->storage, &image->storage};
    copy->transaction_boundary=transaction;copy->source_was_reserved=source_reserved;
    copy->keep_target_keys=keep_target_keys;copy->transaction_budget=transaction_budget;
    if(transaction) {
        copy->source_target=source_target;
        copy->target_before=target_before;
    }
    copy->service_selectors=services;
    copy->ambient_services=services&&!source->source_services;
    if(services) {
        copy->service_selection_at_capture=*services;
        image->source_services=calloc(1u,sizeof(*image->source_services));
        if (image->source_services) atomic_init(&image->source_services->active, false);
        if(!image->source_services || !space_execution_analysis_cache_copy(
                effects,service_limit,&image->source_services->effects)) {
            cetta_prime_checkpoint_copy_free(copy);
            if(status)*status=CETTA_CONTINUATION_CAPACITY;
            return NULL;
        }
    }
    if (!parent_copy_selectors_new(source,services,service_limit,&copy->selectors)) {
        cetta_prime_checkpoint_copy_free(copy);
        if(status)*status=CETTA_CONTINUATION_CAPACITY;
        return NULL;
    }
    if (source->owned_top_episode) {
        copy->episode_image = parent_copy_episode_new(source->owned_top_episode);
        if (!copy->episode_image) {
            cetta_prime_checkpoint_copy_free(copy);
            if (status) *status = CETTA_CONTINUATION_CAPACITY;
            return NULL;
        }
        copy->episode_calls=prime_need_episode_copy_plan_new(source,image,
            source->owned_top_episode,copy->episode_image);
        if(!copy->episode_calls) {
            cetta_prime_checkpoint_copy_free(copy);
            if(status)*status=CETTA_CONTINUATION_CAPACITY;
            return NULL;
        }
    }
    copy->source_holder = cetta_native_handle_owned_atom(context, &copy->holder, "prime-search", id);
    if (!copy->source_holder) {
        cetta_prime_checkpoint_copy_free(copy);
        if (status) *status = CETTA_CONTINUATION_INVALIDATED;
        return NULL;
    }
    if (!cetta_native_handle_alloc(context, "prime-checkpoint-copy", copy,
            prime_checkpoint_context_retire, &copy->context_watch)) {
        cetta_prime_checkpoint_copy_free(copy);
        if (status) *status = CETTA_CONTINUATION_CAPACITY;
        return NULL;
    }
    bool storage = cetta_native_handle_set_storage(context, "prime-checkpoint-copy",
        copy->context_watch, prime_checkpoint_storage);
    assert(storage);
    (void)storage;
    source->leased = true;
    copy->reserved = true;
    copy->status = CETTA_CONTINUATION_READY;
    if (status) *status = CETTA_CONTINUATION_READY;
    return copy;
}

static CettaPrimeCheckpointCopy *prime_checkpoint_copy_new_in(
    CettaLibraryContext *context,Atom *residual,CettaContinuationStatus *status,
    bool capture_services,size_t service_limit) {
    return prime_checkpoint_copy_new_profile(context,residual,status,capture_services,
        service_limit,false,false,false,SIZE_MAX);
}

CettaPrimeCheckpointCopy *cetta_prime_checkpoint_copy_new(
    CettaLibraryContext *context, Atom *residual, CettaContinuationStatus *status) {
    return prime_checkpoint_copy_new_in(context,residual,status,false,SIZE_MAX);
}

CettaPrimeCheckpointCopy *cetta_prime_checkpoint_copy_new_reserved(
    CettaLibraryContext *context, Atom *residual, CettaContinuationStatus *status) {
    CettaPrimeCheckpointCopy *copy = cetta_prime_checkpoint_copy_new(context,residual,status);
    if (copy) copy->keep_source_reserved = true;
    return copy;
}

static bool prime_checkpoint_service_selection_live(const CettaPrimeCheckpointCopy *copy) {
    if(!copy->service_selectors)return true;
    const EquationMatchDecisionCache *s=copy->service_selectors;
    const EquationMatchDecisionCache *saved=&copy->service_selection_at_capture;
    if(s->clock!=saved->clock)return false;
    for(size_t i=0u;i<EQUATION_MATCH_DECISION_CACHE_SLOTS;i++)
        if(s->slots[i]!=saved->slots[i]||s->used[i]!=saved->used[i])return false;
    return true;
}

static bool prime_checkpoint_pairs_live(const CettaPrimeCheckpointCopy *copy) {
    return copy && !copy->attempted && copy->reserved &&
        copy->context == g_library_context && prime_checkpoint_source_live(copy) &&
        parent_copy_quiescent_in(copy->source,true,copy->transaction_boundary,NULL) &&
        prime_checkpoint_service_selection_live(copy) &&
        parent_copy_selectors_live(copy->source,copy->service_selectors,&copy->selectors);
}

size_t cetta_prime_checkpoint_copy_arena_pair_count(const CettaPrimeCheckpointCopy *copy) {
    if (!prime_checkpoint_pairs_live(copy)) return 0u;
    size_t count = 1u;
    const PrimeNativeTopEpisode *source = copy->source->owned_top_episode;
    if (source) {
        if (!copy->episode_image || !parent_copy_episode_admitted(copy->source) ||
            source->regions.level_count > SIZE_MAX - 3u) return 0u;
        count += 1u+source->request_storage_ready;
        for (size_t i = 0u; i < source->regions.level_count; i++)
            if (source->regions.levels[i]) count++;
        size_t episode_pairs=prime_need_episode_copy_pair_count(copy->episode_calls);
        if(episode_pairs>SIZE_MAX-count)return 0u;
        count+=episode_pairs;
    }
    if(copy->selectors.count>SIZE_MAX-count)return 0u;
    return count+copy->selectors.count;
}

bool cetta_prime_checkpoint_copy_arena_pair_at(const CettaPrimeCheckpointCopy *copy,
    size_t index, AtomDeepCopyArenaPair *pair) {
    if (!pair || index >= cetta_prime_checkpoint_copy_arena_pair_count(copy)) return false;
    if (!index) { *pair = copy->pair; return true; }
    index--;
    const PrimeNativeTopEpisode *source = copy->source->owned_top_episode;
    PrimeNativeTopEpisode *image = copy->episode_image;
    if(source) {
        if (!index) { *pair = (AtomDeepCopyArenaPair){&source->arena, &image->arena}; return true; }
        index--;
        if(source->request_storage_ready) {
            if (!index) { *pair = (AtomDeepCopyArenaPair){&source->request_storage, &image->request_storage}; return true; }
            index--;
        }
        for (size_t i = 0u; i < source->regions.level_count; i++) {
            if (!source->regions.levels[i]) continue;
            if (!index) {
                *pair = (AtomDeepCopyArenaPair){&source->regions.levels[i]->arena, &image->regions.levels[i]->arena};
                return true;
            }
            index--;
        }
        size_t episode_pairs=prime_need_episode_copy_pair_count(copy->episode_calls);
        if(index<episode_pairs)
            return prime_need_episode_copy_pair_at(copy->episode_calls,index,pair);
        index-=episode_pairs;
    }
    if(index>=copy->selectors.count)return false;
    *pair=(AtomDeepCopyArenaPair){&copy->selectors.items[index].source->pattern_arena,
        &copy->selectors.items[index].image->pattern_arena};
    return true;
}

bool cetta_prime_checkpoint_copy_arena_pair(
    const CettaPrimeCheckpointCopy *copy, AtomDeepCopyArenaPair *pair) {
    return cetta_prime_checkpoint_copy_arena_pair_at(copy, 0u, pair);
}

static bool prime_checkpoint_episode_capture(
        const PrimeNativeTopEpisode *source,PrimeNativeTopEpisode *image,
        AtomDeepCopySession *atoms,bool *capacity) {
    errno=0;
    bool copied=parent_copy_episode_capture(source,image,atoms);
    *capacity=!copied&&errno==ENOMEM;
    return copied;
}

static bool prime_checkpoint_producer_capture(PrimeNeedEpisodeCopyPlan *plan,
        AtomDeepCopySession *atoms,const char **refusal,bool *capacity) {
    errno=0;
    bool copied=prime_need_episode_copy_capture(plan,atoms,refusal);
    *capacity=!copied&&(errno==ENOMEM||
        (*refusal&&!strcmp(*refusal,"Allocation")));
    return copied;
}

static bool prime_checkpoint_control_capture(CettaPrimeCheckpointCopy *copy,
        AtomDeepCopySession *atoms,bool *capacity) {
    errno=0;
    bool copied=prime_native_parent_copy_with_session(copy->source,copy->image,
        atoms,true,copy->transaction_boundary?&copy->transaction_budget:NULL,
        &copy->refusal);
    *capacity=!copied&&(errno==ENOMEM||
        (copy->refusal&&!strcmp(copy->refusal,"Allocation")));
    return copied;
}

CettaContinuationStatus cetta_prime_checkpoint_copy_prepare(
    CettaPrimeCheckpointCopy *copy, AtomDeepCopySession *atoms) {
    if (!copy) return CETTA_CONTINUATION_UNSUPPORTED;
    if (copy->attempted) return copy->status;
    copy->attempted = true;
    bool episode_capacity=false,producer_capacity=false,control_capacity=false;
    if (copy->context != g_library_context || !copy->reserved ||
        !prime_checkpoint_source_live(copy) || !copy->source->leased) {
        copy->status = CETTA_CONTINUATION_INVALIDATED;
        copy->refusal = "SourceReservation";
    } else if (!parent_copy_quiescent_in(copy->source,true,copy->transaction_boundary,&copy->refusal)) {
        copy->status = parent_copy_refusal_status(copy->refusal);
    } else if (!parent_copy_episode_admitted(copy->source)) {
        copy->status = CETTA_CONTINUATION_UNSUPPORTED;
        copy->refusal = "OwnedTopEpisodeImage";
    } else if (!prime_checkpoint_service_selection_live(copy) ||
        !parent_copy_selectors_capture(copy->source,copy->image,copy->service_selectors,&copy->selectors,atoms)) {
        copy->status=CETTA_CONTINUATION_UNSUPPORTED;
        copy->refusal="EquationSelectorCapture";
    } else if (copy->episode_image && !prime_checkpoint_episode_capture(
            copy->source->owned_top_episode, copy->episode_image, atoms,
            &episode_capacity)) {
        copy->status = episode_capacity ? CETTA_CONTINUATION_CAPACITY
            : CETTA_CONTINUATION_UNSUPPORTED;
        copy->refusal = "EpisodeCapture";
    } else if(copy->episode_calls&&!prime_checkpoint_producer_capture(
            copy->episode_calls,atoms,&copy->refusal,&producer_capacity)) {
        copy->status=producer_capacity ? CETTA_CONTINUATION_CAPACITY
            : CETTA_CONTINUATION_UNSUPPORTED;
        if(!copy->refusal)copy->refusal="EpisodeProducerCapture";
    } else if (!prime_checkpoint_control_capture(copy,atoms,&control_capacity)) {
        copy->status = control_capacity ? CETTA_CONTINUATION_CAPACITY
            : parent_copy_refusal_status(copy->refusal);
    } else {
        bool target=true;
        if(copy->transaction_boundary) {
            copy->image_target=space_read_token(cetta_space_agenda_checkpoint_target(copy->image->space_agenda));
            copy->target_after=space_checkpoint_issuance();
            target=space_read_token_is_current(copy->source_target) &&
                copy->image_target.instance_id>=copy->target_before.instance &&
                copy->image_target.instance_id<copy->target_after.instance &&
                copy->image_target.prefix_epoch>=copy->target_before.prefix &&
                copy->image_target.prefix_epoch<copy->target_after.prefix &&
                copy->target_after.instance!=UINT64_MAX && copy->target_after.prefix!=UINT64_MAX &&
                cetta_space_agenda_checkpoint_lookup(atoms,copy->source->space_agenda)==copy->image->space_agenda;
            if(target && !copy->keep_target_keys && copy->image->source_services)
                target=space_execution_analysis_cache_transport_target(copy->image->source_services->effects,
                    copy->source_target,copy->image_target,copy->transaction_budget.work_limit);
            copy->target_complete=target;
        }
        copy->prepared=target;
        copy->status=target?CETTA_CONTINUATION_READY:CETTA_CONTINUATION_UNSUPPORTED;
        if(!target)copy->refusal="TransactionTargetImage";
    }
    if (!copy->keep_source_reserved || !copy->prepared)
        prime_checkpoint_unreserve(copy);
    return copy->status;
}

CettaContinuationStatus cetta_prime_checkpoint_copy_publish(
    CettaPrimeCheckpointCopy *copy, Arena *arena, Atom **residual) {
    if (!copy || !arena || !residual)
        return CETTA_CONTINUATION_UNSUPPORTED;
    if (!copy->context || copy->context != g_library_context)
        return CETTA_CONTINUATION_INVALIDATED;
    if (copy->keep_source_reserved) return CETTA_CONTINUATION_UNSUPPORTED;
    if (!copy->prepared || !copy->image) return CETTA_CONTINUATION_UNSUPPORTED;
    uint64_t id = 0u;
    if (!cetta_native_handle_alloc(copy->context, "prime-search", copy->image,
            prime_native_control_destroy, &id))
        return CETTA_CONTINUATION_CAPACITY;
    /* No user callback runs between registration and holder publication. */
    bool storage = cetta_native_handle_set_storage(copy->context, "prime-search", id,
        prime_native_control_storage_census);
    assert(storage);
    (void)storage;
    copy->image->handle_id = id;
    Atom *handle = cetta_native_handle_owned_atom(copy->context, arena, "prime-search", id);
    assert(handle);
    copy->image = NULL;
    copy->prepared = false;
    copy->status = CETTA_CONTINUATION_UNSUPPORTED;
    copy->refusal = "Published";
    *residual = handle;
    return CETTA_CONTINUATION_READY;
}

CettaContinuationStatus cetta_prime_checkpoint_copy_replace(
    CettaPrimeCheckpointCopy *copy, CettaNativeHandleUse *source_use) {
    if (!copy) return CETTA_CONTINUATION_UNSUPPORTED;
    if (!copy->context || copy->context != g_library_context)
        return CETTA_CONTINUATION_INVALIDATED;
    if (!copy->keep_source_reserved || !copy->prepared || !copy->image || !copy->reserved)
        return CETTA_CONTINUATION_UNSUPPORTED;
    if (!prime_checkpoint_source_live(copy) || !copy->source->leased ||
        cetta_native_handle_use_resource(source_use) != copy->source)
        return CETTA_CONTINUATION_INVALIDATED;
    const char *refusal=NULL;
    if(!parent_copy_quiescent_in(copy->source,true,copy->transaction_boundary,&refusal))
        return parent_copy_refusal_status(refusal);
    PrimeNativeControl *image = copy->image;
    uint64_t former_id = image->handle_id;
    image->handle_id = copy->source_handle;
    void *previous = NULL;
    if (!cetta_native_handle_use_exchange(copy->context,"prime-search",
            copy->source_holder,source_use,image,&previous)) {
        image->handle_id = former_id;
        return CETTA_CONTINUATION_DEFERRED;
    }
    /* There are no graph-copy pointer keys live at this boundary. The old
     * control is no longer the registry payload. Detach all staging roots
     * before its finalizer can reenter the native services. */
    copy->source = NULL;
    copy->image = NULL;
    copy->reserved = false;
    copy->prepared = false;
    copy->status = CETTA_CONTINUATION_UNSUPPORTED;
    copy->refusal = "Replaced";
    prime_native_control_destroy(previous);
    return CETTA_CONTINUATION_READY;
}

const char *cetta_prime_checkpoint_copy_refusal(const CettaPrimeCheckpointCopy *copy) {
    return copy ? copy->refusal : "NoCheckpointCopy";
}

void cetta_prime_checkpoint_copy_free(CettaPrimeCheckpointCopy *copy) {
    if (!copy) return;
    if (!copy->context || !copy->context_watch ||
        !cetta_native_handle_close(copy->context, "prime-checkpoint-copy", copy->context_watch))
        prime_checkpoint_context_retire(copy);
    arena_free(&copy->holder);
    free(copy);
}
