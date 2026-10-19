typedef struct {
    CettaLibraryContext *library;
    Registry *registry;
    Space *root;
    Arena *persistent;
    HashConsTable *hashcons;
    EvalTermUniverseScope universe;
    PrimeNativeOwnedRegion *previous;
    PrimeNativeServiceScope services;
    bool active;
} PrimeNativeRegionScope;

static void prime_native_region_context_select(PrimeNativeOwnedRegion *region,
    PrimeNativeRegionScope *scope) {
    assert(region && scope && !scope->active && region->library_ready && region->arenas_ready);
    scope->library = eval_swap_library_context(region->library);
    scope->registry = g_registry;
    scope->root = g_eval_root_space;
    scope->persistent = g_eval_fallback_universe.persistent_arena;
    scope->hashcons = g_hashcons;
    scope->previous = g_prime_owned_region;
    scope->active = true;
    g_prime_owned_region = region;
    g_registry = &region->registry;
    g_eval_root_space = prime_native_region_program(region);
    g_hashcons = NULL;
    eval_term_universe_scope_enter(&scope->universe, &region->library->term_universe);
    term_universe_set_persistent_arena(&g_eval_fallback_universe, &region->persistent);
}

static bool prime_native_region_scope_select(PrimeNativeOwnedRegion *region,
    PrimeNativeRegionScope *scope) {
    if (!scope || scope->active) return false;
    if (!region || region == g_prime_owned_region) return true;
    if (!region->library_ready || !region->arenas_ready || !region->services)
        return false;
    if (!prime_native_service_enter(region->services, &scope->services)) return false;
    prime_native_region_context_select(region,scope);
    return true;
}

static bool prime_native_region_scope_enter(PrimeNativeTopEpisode *episode,
    PrimeNativeRegionScope *scope) {
    return prime_native_region_scope_select(prime_native_episode_region(episode), scope);
}

static void prime_native_region_scope_leave(PrimeNativeRegionScope *scope) {
    if (!scope || !scope->active) return;
    g_registry = scope->registry;
    g_eval_root_space = scope->root;
    term_universe_set_persistent_arena(&g_eval_fallback_universe, scope->persistent);
    eval_term_universe_scope_leave(&scope->universe);
    g_hashcons = scope->hashcons;
    (void)eval_swap_library_context(scope->library);
    g_prime_owned_region = scope->previous;
    prime_native_service_leave(&scope->services);
    *scope = (PrimeNativeRegionScope){0};
}

static void prime_native_top_episode_destroy(PrimeNativeTopEpisode *episode) {
    PrimeNativeRegionScope scope = {0};
    bool selected = prime_native_region_scope_enter(episode, &scope);
    assert(selected); (void)selected;
    if (episode->execution) {
        EvalExecutionScope execution={.owner=episode->execution,.previous=g_eval_execution};
        g_eval_execution=episode->execution;
        eval_execution_scope_leave(&execution);
        episode->execution=NULL;
    }
    if (episode->ordinary_source) {
        registry_read_view_free(&episode->source_registry);
        space_read_view_release(&episode->source_view);
    }
    if (episode->ready) {
        prime_need_region_pool_free(&episode->regions);
        arena_free(&episode->arena);
    }
    if (episode->request_storage_ready) arena_free(&episode->request_storage);
    PrimeNativeOwnedRegion *region = episode->owned_region;
    PrimeNativeTopEpisode *source=episode->source_episode;
    prime_native_region_scope_leave(&scope);
    free(episode);
    prime_native_owned_region_free(region);
    if (source) prime_native_top_episode_release(source);
}

static void prime_native_owned_region_storage(const PrimeNativeOwnedRegion *region,
    CettaStorageCensus *census) {
    if (!region || !cetta_storage_object(census, region, sizeof(*region), CETTA_STORAGE_BLOCK))
        return;
    if (region->library_ready)
        cetta_library_context_prime_snapshot_storage(region->library, census);
    prime_native_source_services_storage(region->services, census);
    if (region->world_ready) space_storage_census(&region->world, census);
    if (region->program) space_storage_census(region->program, census);
    if (region->registry_ready) registry_storage_census(&region->registry, census);
    if (region->arenas_ready) {
        arena_storage_census(&region->snapshot, census);
        arena_storage_census(&region->persistent, census);
    }
    cetta_storage_array(census, region->preparation.events, region->preparation.capacity,
        sizeof(*region->preparation.events), 0u);
}

static void prime_native_owned_region_free(PrimeNativeOwnedRegion *region) {
    if (!region) return;
    assert(g_prime_owned_region != region);
    PrimeNativeRegionScope scope={0};
    if (region->library_ready) {
        assert(region->arenas_ready && !region->library->native_handle_registry);
        if (region->services && region->services->effects) {
            bool selected=prime_native_region_scope_select(region,&scope);
            assert(selected); (void)selected;
        } else prime_native_region_context_select(region,&scope);
    }
    /* Controller and pending owners retire first. World retirement runs
     * with its own cache selected. Then deselect the service stores before
     * destroying them, while their enclosing private context remains live. */
    if (region->library_ready && region->world_ready && region->library->petta_program)
        petta_program_forget_space(region->library->petta_program, &region->world);
    if (region->program) {
        if (region->library_ready && region->library->petta_program)
            petta_program_forget_space(region->library->petta_program,region->program);
        space_free(region->program);
        free(region->program);
        region->program=NULL;
    }
    if (region->registry_ready) registry_free(&region->registry);
    if (region->world_ready) space_free(&region->world);
    prime_native_service_leave(&scope.services);
    prime_native_source_services_free(region->services);
    region->services=NULL;
    if (region->library_ready) cetta_library_context_free(region->library);
    /* Admitted snapshot payloads have only structural/scalar and frame
     * finalizers; no native return, State or foreign callback is admitted. */
    if (region->arenas_ready) {
        arena_free(&region->snapshot);
        arena_free(&region->persistent);
    }
    prime_native_region_scope_leave(&scope);
    free(region->library);
    cetta_native_cost_free(&region->preparation);
    free(region);
}
