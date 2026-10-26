/* The region owns execution services. It is not an identifier registry
 * authority and never replaces the original public slot's currency. */
typedef struct PrimeNativeSourceServices PrimeNativeSourceServices;
struct PrimeNativeOwnedRegion {
    CettaLibraryContext *library;
    PrimeNativeSourceServices *services;
    Space world;
    Registry registry;
    Arena snapshot;
    Arena persistent;
    Atom *expression;
    bool library_ready, world_ready, registry_ready, arenas_ready, ready;
    size_t inspection_limit, inspection_spent, byte_limit;
    size_t captured_nodes, captured_edges, captured_occurrences;
    size_t prepared_bytes;
    uint64_t symbols;
    CettaNativeCostLedger preparation;
    bool private_data;
    Space *program;
    SpaceReadToken data_read;
    SpaceProgramToken program_read;
};

static __thread PrimeNativeOwnedRegion *g_prime_owned_region;

static Space *prime_native_region_program(PrimeNativeOwnedRegion *region) {
    return region->program ? region->program : &region->world;
}

/* Equation lookup keeps the frozen program. The execution context denotes
 * its separate private data world, just as the captured &self binding does.
 * Explicit references to any other space retain their original role. */
static bool prime_native_context_overlay_of(const Space *view,const Space *base) {
    if (!base) return false;
    while (view && view!=base && view->overlay_base && !view->module_own_view)
        view=view->overlay_base;
    return view==base;
}

static Space *prime_native_region_context_space(Space *space) {
    PrimeNativeOwnedRegion *region = g_prime_owned_region;
    if (!region || !region->private_data ||
        !prime_native_context_overlay_of(space,region->program)) return space;
    Space *world=&region->world;
    Atom *self=g_registry ? registry_lookup_id(g_registry,g_builtin_syms.self) : NULL;
    /* A nested payload's registry already names its innermost data overlay.
     * The active redirect map alone need not contain the original world. */
    if (self && self->kind==ATOM_GROUNDED && self->ground.gkind==GV_SPACE &&
        prime_native_context_overlay_of(self->ground.ptr,world))
        world=self->ground.ptr;
    return eval_resolve_space_read(world);
}

static PrimeNativeOwnedRegion *prime_native_episode_region(PrimeNativeTopEpisode *episode) {
    if (episode && episode->source_episode) episode=episode->source_episode;
    return episode ? episode->owned_region : NULL;
}

typedef struct {
    atomic_size_t references;
    Arena origin_storage;
    Arena value_storage;
    Atom *origin;
    CettaNativeHandleUse lifetime;
    bool lifetime_ready;
} PrimeNativeReturnCarrier;

/* Address-stable acquired receipts live here, never in a movable Outcome.
 * Only this operation may mutate the child. Carrier references grant memory
 * retention alone, and an immutable pending row forbids another child run. */
struct PrimeNativeAcquiredOperation {
    CettaNativeHandleUse use;
    PrimeNativeReturnCarrier *carrier;
    Arena *receiver;
    Atom *identifier, *holder;
    const struct PrimeNativeOwnedRequest *top_request;
    const struct PrimeEvalStackFrame *parent_frame;
    const struct PrimeNativeControl *parent_control;
    OutcomeSet pending;
    bool pending_ready;
    bool owns_lease;
    bool producer_complete;
    /* Only the explicit weighted-call entry chooses this contract. A closed
     * call transfers whole Outcomes; an open cut retains one Observation. */
    bool closed_return;
    bool materialized;
    bool published;
    Atom *values;
    /* The paid accepted interval is filtered by the controller's actual
     * delivered flags. EXCLUSIVE keeps those rows/environments immutable
     * while this separate return phase completes or remains refused. */
    size_t output_first, output_limit, value_cursor;
    size_t accept_cursor, accepted_count, delivered_first;
    bool acceptance_checked;
    Atom **value_items;
    size_t value_count, value_capacity;
    Atom *reified;
    AtomDeepCopySession *value_copy;
    const char *return_refusal;
    uint64_t remaining;
};

static void prime_native_acquired_operation_free(PrimeNativeAcquiredOperation *operation);
static void prime_native_acquired_operation_storage(const PrimeNativeAcquiredOperation *operation,
    CettaStorageCensus *census);
static bool prime_native_acquired_receiver_admitted(const PrimeNativeAcquiredOperation *operation,
    const struct PrimeNativeControl *control);
static bool prime_native_acquired_resume_nested(struct PrimeEvalStackFrame *frame);
static bool prime_native_region_primitive(SymbolId head);
static bool prime_native_owned_region_call_admitted(Space *space, Arena *arena,
    Atom *call, bool private_data);
