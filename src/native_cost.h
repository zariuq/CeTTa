#ifndef CETTA_NATIVE_COST_H
#define CETTA_NATIVE_COST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Operation counts in native-events-v1. A matching or arithmetic primitive is
 * one operation in this profile; its machine instructions and byte complexity
 * are not units of this valuation. */
typedef enum {
    CETTA_COST_QUANTUM,
    CETTA_COST_CANDIDATE_ROW,
    CETTA_COST_INDEX_VISIT,
    CETTA_COST_MATCH_ATTEMPT,
    CETTA_COST_MATCH_NODE,
    CETTA_COST_HEAP_LOOKUP,
    CETTA_COST_HEAP_INDEX_VISIT,
    CETTA_COST_HEAP_UPDATE,
    CETTA_COST_RECEIPT_APPEND,
    CETTA_COST_BINDING_CLONE,
    CETTA_COST_QUEUE_INSERT,
    CETTA_COST_QUEUE_TAKE,
    CETTA_COST_QUEUE_VISIT,
    CETTA_COST_QUEUE_MOVE,
    CETTA_COST_CAPTURE,
    CETTA_COST_RESTORE,
    CETTA_COST_FRAME_ALLOCATE,
    CETTA_COST_FRAME_RETIRE,
    CETTA_COST_PUBLISH,
    CETTA_COST_COEFFICIENT_APPEND,
    CETTA_COST_COEFFICIENT_MULTIPLY,
    CETTA_COST_READ,
    CETTA_COST_ANALYSIS_STEP,
    CETTA_COST_WORKER_DISPATCH,
    CETTA_COST_WORKER_JOIN,
    CETTA_COST_COMMIT_CHECK,
    CETTA_COST_EQUATION_ACTIVATION,
    CETTA_COST_STORAGE_PROBE,
    CETTA_COST_COEFFICIENT_HANDLE,
    CETTA_COST_KIND_COUNT
} CettaNativeCostKind;

typedef struct {
    uint64_t identity;
    uint64_t origin;
    uint64_t occurrence;
    uint64_t units;
    uint64_t repetitions;
    CettaNativeCostKind kind;
} CettaNativeCostEvent;

typedef struct {
    uint64_t counts[CETTA_COST_KIND_COUNT];
    uint64_t work;
    uint64_t span;
    bool parallel;
    bool overflow;
    bool trace_incomplete;
} CettaNativeCostSnapshot;

typedef struct {
    uint64_t identity;
    CettaNativeCostSnapshot total;
    CettaNativeCostEvent *events;
    size_t length;
    size_t capacity;
    bool imported;
} CettaNativeCostLedger;

typedef struct CettaNativeCostScope {
    struct CettaNativeCostScope *previous;
    CettaNativeCostLedger *ledger;
    uint64_t occurrence;
} CettaNativeCostScope;


/* A synchronous readout of one actual command and its ordered destination
 * visits. These borrowed views are valid only during the callback. The
 * callback must return normally, preserve every ledger/scope/observer, and
 * neither charge work nor reenter this service. Retention is separate from
 * numerical charging: a refused event still changes its account. */
typedef enum {
    CETTA_COST_OBSERVE_BEGIN,
    CETTA_COST_OBSERVE_DESTINATION,
    CETTA_COST_OBSERVE_END
} CettaNativeCostObservationPoint;
typedef struct {
    CettaNativeCostObservationPoint point;
    CettaNativeCostEvent event;
    const CettaNativeCostScope *chain;
    const CettaNativeCostScope *scope;
    const CettaNativeCostLedger *ledger;
    size_t ordinal;
    bool scoped, duplicate, retained;
    CettaNativeCostSnapshot before, after;
} CettaNativeCostChargeView;
typedef void (*CettaNativeCostObserverFn)(
    void *context, const CettaNativeCostChargeView *view);
typedef struct CettaNativeCostObserver {
    struct CettaNativeCostObserver *previous;
    CettaNativeCostObserverFn observe;
    void *context;
} CettaNativeCostObserver;
/* Enter/leave own no ledger or callback context. They allocate nothing.
 * Disabled observations do not build snapshots or invoke callbacks. */
bool cetta_native_cost_observer_enter(CettaNativeCostObserver *observer,
    CettaNativeCostObserverFn observe, void *context);
void cetta_native_cost_observer_leave(CettaNativeCostObserver *observer);
bool cetta_native_cost_observer_active(void);

/* A borrowed synchronous suspension of the complete observer chain. The
 * cost scope chain is independent and must be detached/restored separately.
 * Begin refuses during notification without changing either selection.
 * Restore after all inner observers leave, in reverse suspension order. */
typedef struct CettaNativeCostObserverSuspension {
    struct CettaNativeCostObserverSuspension *previous;
    CettaNativeCostObserver *observers;
    bool active;
} CettaNativeCostObserverSuspension;
bool cetta_native_cost_observer_suspend(CettaNativeCostObserverSuspension *);
void cetta_native_cost_observer_restore(CettaNativeCostObserverSuspension *);


void cetta_native_cost_init(CettaNativeCostLedger *ledger);
void cetta_native_cost_free(CettaNativeCostLedger *ledger);
const char *cetta_native_cost_name(CettaNativeCostKind kind);
/* Overflow preserves a lower bound and sets OVERFLOW. Trace allocation
 * failure sets TRACE_INCOMPLETE; neither condition is an exact receipt. */
void cetta_native_cost_append(CettaNativeCostLedger *ledger,
    CettaNativeCostKind kind, uint64_t units, uint64_t occurrence);
void cetta_native_cost_scope_enter(CettaNativeCostScope *scope,
    CettaNativeCostLedger *ledger, uint64_t occurrence);
void cetta_native_cost_scope_leave(CettaNativeCostScope *scope);
/* Detach a private slice from its coordinator observation until a checked
 * join imports that slice. Restore only after all private scopes have left. */
CettaNativeCostScope *cetta_native_cost_detach_begin(void);
void cetta_native_cost_detach_end(CettaNativeCostScope *previous);
/* A real operation belongs once to each distinct containing observation.
 * Re-entering the same ledger does not double-charge it. */
/* This is the active scope itself, not a second enable flag. Ordinary
 * cost-blind operations do not call into the event/identity machinery. */
extern _Thread_local CettaNativeCostScope *cetta_native_cost_current_scope;
static inline bool cetta_native_cost_active(void) {
    return cetta_native_cost_current_scope != NULL;
}
uint64_t cetta_native_cost_charge_observed(CettaNativeCostKind kind, uint64_t units);
static inline uint64_t cetta_native_cost_charge(CettaNativeCostKind kind, uint64_t units) {
    return cetta_native_cost_active()
        ? cetta_native_cost_charge_observed(kind, units) : 0u;
}
void cetta_native_cost_occurrence(uint64_t occurrence);
/* Transfer each completed private slice once into the current containing
 * observations. The slices are disjoint physical executions, ordered by
 * logical child index. The coordinator owns the fork/join boundary. */
bool cetta_native_cost_join(CettaNativeCostLedger *const *children, size_t count);
/* Phases run sequentially; workers within one phase run independently;
 * slices assigned to the same worker run sequentially. The caller retains
 * this actual dependency schedule. Event publication still uses child order. */
typedef struct { uint64_t phase, worker; } CettaNativeCostPlacement;
bool cetta_native_cost_join_scheduled(CettaNativeCostLedger *const *children,
    const CettaNativeCostPlacement *placement, size_t count);

/* A completed private execution segment. Array order is logical publication
 * order; order is the actual start order within this one joined episode.
 * The two predecessor slots retain their distinct meanings even when equal.
 * This service validates the complete finite schedule, not native authority:
 * the caller authenticates owner and physical-worker edges at execution. */
typedef struct {
    CettaNativeCostLedger *ledger;
    uint64_t occurrence;
    uint64_t order;
    uint64_t worker;
    uint64_t owner_predecessor;
    uint64_t worker_predecessor;
} CettaNativeCostSegment;

/* Refusal leaves every child and containing account unchanged. Successful
 * import preserves event identities/origins and logical array order, while
 * span follows both predecessor slots. The parent and all segments are
 * quiescent; no active destination may also be an imported segment. */
bool cetta_native_cost_join_segments(const CettaNativeCostSegment *segments,
    size_t count, uint64_t workers);

/* Return the preceding snapshot, then record the read as its own event. */
CettaNativeCostSnapshot cetta_native_cost_read(CettaNativeCostLedger *ledger,
    uint64_t occurrence, CettaNativeCostEvent *charge);


/* Owned copies of the synchronous command views. A direct append has one
 * destination, even though it has no dynamic C scope chain. Scope pointers
 * and borrowed ledger pointers are never retained in this buffer. */
typedef struct {
    uint64_t ledger_identity, scope_occurrence;
    bool present, duplicate, retained;
    CettaNativeCostSnapshot before, after;
} CettaNativeCostDestinationReceipt;
typedef struct {
    CettaNativeCostEvent event;
    size_t first_destination, destination_count;
    bool scoped;
} CettaNativeCostCommandReceipt;
typedef struct {
    CettaNativeCostCommandReceipt *commands;
    CettaNativeCostDestinationReceipt *destinations;
    size_t command_count, command_capacity, destination_count, destination_capacity;
    size_t active_command, active_destinations;
    CettaNativeCostEvent active_event;
    bool active_scoped, open, truncated, malformed;
} CettaNativeCostReceipts;

bool cetta_native_cost_receipts_init(CettaNativeCostReceipts *receipts,
    size_t commands, size_t destinations);
void cetta_native_cost_receipts_free(CettaNativeCostReceipts *receipts);
void cetta_native_cost_receipts_observe(void *receipts,
    const CettaNativeCostChargeView *view);
bool cetta_native_cost_receipts_complete(const CettaNativeCostReceipts *receipts);

typedef enum {
    CETTA_COST_ID_EVENT, CETTA_COST_ID_ORIGIN, CETTA_COST_ID_OCCURRENCE,
    CETTA_COST_ID_DESTINATION
} CettaNativeCostIdentityRole;
/* This comparison consumes an already authenticated finite map; it neither
 * invents fresh bindings nor certifies the map's issuer/source authority.
 * All native zero/sentinel coordinates remain fixed. */
typedef bool (*CettaNativeCostIdentityMap)(void *context,
    CettaNativeCostIdentityRole role, uint64_t source, uint64_t *image);
bool cetta_native_cost_receipts_related(const CettaNativeCostReceipts *source,
    const CettaNativeCostReceipts *image, CettaNativeCostIdentityMap map,
    void *context, size_t *first_mismatch);

/* Compare the complete attempted receipt owner against its exact command
 * prefix in a complete recorded owner. All later recorded destinations stay
 * held. The caller authenticates the map and the selected event boundary. */
bool cetta_native_cost_receipts_prefix_related(const CettaNativeCostReceipts *recorded,
    const CettaNativeCostReceipts *attempt, CettaNativeCostIdentityMap map,
    void *context, size_t *first_mismatch);

#include "checkpoint_compare.h"
void cetta_native_cost_checkpoint_issuance(CettaCheckpointIssuance *);
#endif
