/* Execution profiles share a retained physical slice, not an expression
 * interpreter. The first profile lends an actual acquired nested operation
 * to a worker while its parent and return frames stay quiescent through join. */
typedef enum {
    PRIME_NATIVE_EXECUTION_NONE = 0,
    PRIME_NATIVE_EXECUTION_OWNED_NESTED,
} PrimeNativeExecutionProfile;

typedef enum {
    PRIME_NATIVE_EXECUTION_RESERVED = 0,
    PRIME_NATIVE_EXECUTION_PAID,
    PRIME_NATIVE_EXECUTION_RETURNING,
} PrimeNativeExecutionPhase;

typedef struct PrimeNativeExecutionSlice {
    struct PrimeNativeExecutionSlice *self;
    PrimeNativeExecutionProfile profile;
    PrimeNativeExecutionPhase phase;
    PrimeNativeServiceReservation reservation;
    PrimeNativeControl *parent;
    PrimeNativeWork *work;
    PrimeEvalStackFrame *sender;
    PrimeEvalStackFrame *target_owner;
    OutcomeSet *target;
    Arena *receiver;
    uint64_t sender_sequence, target_sequence, occurrence;
    PrimeNativeAcquiredOperation *operation;
    CettaNativeHandleUse *authority;
    PrimeNativeControl *child;
    PrimeNativeTopEpisode *episode;
    PrimeNativeOwnedRegion *region;
    PrimeNativeSourceServices *services;
    /* These are the actual retained dependency owners, not reconstructed
     * syntax. Their mutable contents are read only after synchronous join. */
    SpaceReadToken source_read;
    const Bindings *environment;
    uint64_t grant, spent, before, after;
    uint64_t remaining_before, remaining_after;
    uint64_t ledger_identity, cost_before, cost_after;
    size_t accepted_before, accepted_after, output_before, output_after;
    bool scope_entered, producer_entered, scope_left, joined;
} PrimeNativeExecutionSlice;

static bool prime_native_acquired_prepare_row(PrimeNativeAcquiredOperation *operation);
static bool prime_native_acquired_accept(Arena *arena, Atom *value,
    const Outcome *occurrence, void *payload);
static uint64_t prime_native_acquired_produce(PrimeNativeAcquiredOperation *operation,
    PrimeNativeControl *control, uint64_t granted);
static bool prime_native_owned_cost_policy(const CettaSearchPolicySpec *policy);
static bool prime_native_owned_observation_policy(const CettaSearchPolicySpec *policy);
static void prime_native_execution_detach(PrimeNativeParallel *parallel);
