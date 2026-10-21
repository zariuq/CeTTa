void eval_prime_owned_region_storage_census(const EvalOutcome *outcome,
    CettaStorageCensus *census) {
    const CettaPrimeObservationReturn *owned = outcome ? outcome->prime_observation : NULL;
    if (!owned || !owned->top.episode || !owned->top.episode->owned_region) {
        cetta_storage_refuse(census, "NoOwnedRegionObservation");
        return;
    }
    if (!cetta_storage_object(census, owned, sizeof(*owned), CETTA_STORAGE_BLOCK)) return;
    if (outcome->results.len > outcome->results.cap ||
        (outcome->results.cap && !outcome->results.items)) {
        cetta_storage_refuse(census, "ObservationResultShape");
        return;
    }
    if (outcome->results.items != outcome->results.inline_items)
        cetta_storage_array(census, outcome->results.items, outcome->results.cap,
            sizeof(*outcome->results.items), 0u);
    arena_storage_census(&owned->holders, census);
    prime_native_top_episode_storage(owned->top.episode, census);
    outcome_set_storage_census(&owned->pending, census);
    prime_native_control_storage_census(owned->top.owned.unpublished, census);
    prime_native_acquired_operation_storage(owned->top.owned.acquired, census);
}

CettaContinuationStatus eval_prime_owned_region_outcome(
    Space *source, Arena *output, Arena *persistent, Registry *registry,
    Atom *expression, CettaPrimeOwnedRegionProfile profile,
    size_t inspection_limit, size_t byte_limit, EvalOutcome *outcome) {
    bool weighted = source && output && prime_native_owned_top_context_admitted() &&
        native_weight_form(expression) &&
        prime_native_control_admits(source,output,expression,true);
    if (profile!=CETTA_PRIME_OWNED_REGION_READ_ONLY_SNAPSHOT_V1 ||
        !outcome || outcome->prime_observation || !output || output->hashcons ||
        output->older_identity || !persistent ||
        (!weighted && !prime_native_registry_observation_admits(source,output,expression)))
        return CETTA_CONTINUATION_UNSUPPORTED;
    CettaLibraryContext *authority=g_library_context;
    CettaContinuationStatus status=CETTA_CONTINUATION_UNSUPPORTED;
    PrimeNativeOwnedRegion *region=prime_native_owned_region_capture(source,registry,
        expression,weighted ? expression : expr_arg(expression,expr_nargs(expression)-1u),
        inspection_limit,byte_limit,false,&status);
    if (!region) return status;
    CettaPrimeObservationReturn *owned=calloc(1u,sizeof(*owned));
    PrimeNativeTopEpisode *episode=calloc(1u,sizeof(*episode));
    if (!owned || !episode) {
        free(owned); free(episode); prime_native_owned_region_free(region);
        return CETTA_CONTINUATION_CAPACITY;
    }
    atomic_init(&episode->references,1u);
    episode->owned_region=region;
    episode->space=&region->world; episode->registry=&region->registry;
    episode->persistent=&region->persistent; episode->library=region->library;
    /* The single capture session already transported this root together
     * with world/registry roots and name keys. Do not copy it separately. */
    episode->request_expression=region->expression;
    arena_init_detached(&owned->holders);
    outcome_set_init_with_owner(&owned->pending,output);
    owned->output=output; owned->library=region->library;
    PrimeNativeOwnedTopRequest *top=&owned->top;
    top->episode=top->owned.top_episode=episode;
    top->source_owner=&region->snapshot;
    top->owned.holder_arena=&owned->holders;
    top->owned.pending=&owned->pending;
    top->owned.return_mode = weighted ? PRIME_NATIVE_RETURN_WEIGHTED_CALL
        : PRIME_NATIVE_RETURN_OBSERVATION;
    top->owned.origin_authority=authority;
    top->owned.status=CETTA_CONTINUATION_CAPACITY;
    if (!prime_native_owned_top_reserve(top,output,&outcome->results)) {
        prime_native_registry_observation_free(owned);
        return CETTA_CONTINUATION_CAPACITY;
    }
    prime_native_top_episode_attach(output,episode);
    outcome->prime_observation=owned;
    top->owned.status=CETTA_CONTINUATION_READY;
    /* The original dispatcher initializes the child in this owned world,
     * then the explicit authority parameter issues the original public slot,
     * copies its token and acquires EXCLUSIVE before the first producer poll.
     * The private registry is never an enrollment or residual authority. */
    assert(region->library->native_handle_registry==NULL);
    prime_native_registry_observation_enter(outcome);
    assert(!top->owned.origin_authority && region->library->native_handle_registry==NULL);
    return top->owned.status;
}
