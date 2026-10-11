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
};

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
static bool prime_native_owned_region_call_admitted(Space *space, Arena *arena,
    Atom *call);
