#ifndef CETTA_STATE_CELL_H
#define CETTA_STATE_CELL_H

#include "atom.h"

typedef struct StateCellImage StateCellImage;

typedef enum {
    STATE_CELL_UNMANAGED,
    STATE_CELL_HELD,
    STATE_CELL_CAPACITY
} StateCellLease;

typedef struct {
    Atom *value;
    Atom *content_type;
    StateCellImage *image;
} StateCellSnapshot;

/* Images have independent storage. Canonical preparation uses a temporary
 * universe; otherwise one copy session preserves sharing across both roots. */
StateCellImage *state_cell_image_prepare(Atom *value, Atom *content_type,
                                         bool canonical);
void state_cell_image_retain(StateCellImage *image);
void state_cell_image_release(StateCellImage *image);
size_t state_cell_image_bytes(const StateCellImage *image);
/* Only while the caller owns an image snapshot/lease. This does not retain
 * the image or turn its Arena's name into a storage ownership reference. */
const Arena *state_cell_image_syntax(const StateCellImage *image);

/* Success consumes IMAGE and returns one creator reference. NULL permits
 * a private cyclic clone to register its identity before filling its image.
 * Failure leaves IMAGE with its caller. */
StateCell *state_cell_managed_new(StateCellImage *image);
void state_cell_creator_release(StateCell *cell);
bool state_cell_is_managed(const StateCell *cell);

/* Unknown stack/transaction/Rho descriptors are not dereferenced. Native
 * identity pins and arena leases preserve the original mutable identity. */
StateCellLease state_cell_lease_into(Arena *arena, StateCell *cell);
void *state_cell_identity_pin(StateCell *cell);
void state_cell_identity_unpin(void *pin);

/* A snapshot retains an immutable image independently of its mutable node.
 * False means an unmanaged descriptor or an unpublished empty managed node. */
bool state_cell_snapshot_acquire(StateCell *cell, StateCellSnapshot *snapshot);
/* IMAGE is already owned by the caller (for example an Arena retained
 * owner). Pin that exact historical image, without rereading any cell. */
bool state_cell_image_snapshot_acquire(StateCellImage *image,
                                       StateCellSnapshot *snapshot);
void state_cell_snapshot_release(StateCellSnapshot *snapshot);
bool state_cell_snapshot_retain_into(Arena *arena,
                                      const StateCellSnapshot *snapshot);
StateCellLease state_cell_snapshot_into(Arena *arena, StateCell *cell,
                                        StateCellSnapshot *snapshot);
/* snapshot_into returns arena-held value/type pointers with image == NULL;
 * it creates no additional caller-owned snapshot reference. */

/* The pointer/value/type swap is serialized by the registry mutex. Success
 * consumes IMAGE; failure leaves both the cell and image ownership unchanged.
 * The previous image is released after leaving the mutex. */
bool state_cell_publish_prepared(StateCell *cell, StateCellImage *image);

/* Retire at most LIMIT queued records, including revived records skipped.
 * Image finalizers run outside the registry mutex and can enqueue children. */
size_t state_cell_reclaim_bounded(size_t limit);
bool state_cell_reclamation_due(void);

typedef void (*StateCellVisit)(void *context, StateCell *cell,
                               const StateCellImage *image,
                               uint64_t references, bool saturated);
/* Visitor callbacks execute under the registry mutex and must not reenter
 * this API. Arena/image owner inspection itself does not acquire that mutex. */
void state_cell_visit_managed(StateCellVisit visit, void *context);
typedef void (*StateCellImageVisit)(void *context, const StateCellImage *image,
                                    const Arena *syntax, uint64_t references,
                                    bool saturated);
void state_cell_visit_images(StateCellImageVisit visit, void *context);
void state_cell_image_visit_owners(const StateCellImage *image,
                                   ArenaOwnerVisit visit, void *context);
/* Classify only this module's known finalizer callbacks. Unrecognized owner
 * pointers are never inspected. Outputs are NULL for the other owner kind. */
bool state_cell_classify_owner(void *owner, void (*release)(void *),
                                StateCell **cell, StateCellImage **image);

typedef struct StateCellGraphSnapshot StateCellGraphSnapshot;
typedef struct {
    StateCell *cell;
    StateCellImage *current_image;
    uint64_t references;
    uint64_t publication_epoch;
    bool saturated;
} StateCellGraphNode;
typedef struct {
    StateCellImage *image;
    const Arena *syntax;
    Atom *value;
    Atom *content_type;
    uint64_t references;
    bool saturated;
} StateCellGraphImage;

/* Pins protect every captured record. Counts precede this snapshot's own
 * pins; other observers and other snapshots remain included. Dying zero-ref
 * images are skipped rather than resurrected. NULL means allocation refusal.
 * The caller supplies the broader quiescent/physical transition admission. */
StateCellGraphSnapshot *state_cell_graph_snapshot_acquire(void);
void state_cell_graph_snapshot_release(StateCellGraphSnapshot *snapshot);
size_t state_cell_graph_node_count(const StateCellGraphSnapshot *snapshot);
size_t state_cell_graph_image_count(const StateCellGraphSnapshot *snapshot);
const StateCellGraphNode *state_cell_graph_nodes(const StateCellGraphSnapshot *snapshot);
const StateCellGraphImage *state_cell_graph_images(const StateCellGraphSnapshot *snapshot);

/* Arrays have the corresponding captured lengths. A NULL victim array
 * selects none. For every victim, incoming must equal its original counted
 * references; the caller establishes complete edges and closed reachability.
 * All selected identities/epochs/counts are checked before any edge changes.
 * A mismatch returns false without State mutation. Caller-protected doomed
 * foreign containers must survive all released finalizers.
 *
 * Success detaches all selected current edges and image payloads first, then
 * releases their arenas/edges outside the registry mutex. Captured controls
 * stay pinned until snapshot_release; image syntax is no longer readable
 * after successful retirement. Saturated records cannot be victims. */
bool state_cell_graph_retire(StateCellGraphSnapshot *snapshot,
                              const bool *dead_nodes, const bool *dead_images,
                              const uint64_t *incoming_nodes,
                              const uint64_t *incoming_images);

typedef struct StateCellPressureObserver StateCellPressureObserver;
StateCellPressureObserver *state_cell_pressure_observer_new(void);
void state_cell_pressure_observer_free(StateCellPressureObserver *observer);
/* UINT64_MAX is conservatively always dirty and cannot be acknowledged. */
uint64_t state_cell_pressure_observer_epoch(const StateCellPressureObserver *observer);
/* Monotone import obligations, independent of latest-version discovery.
 * A publication with an uncertified mutable child increments this epoch.
 * Census, settlement, overwrites and cancellation never reset it. The
 * consumer acknowledges a captured value only after a full root trace and
 * completed reclamation, with stable epochs. UINT64_MAX is always dirty. */
uint64_t state_cell_pressure_observer_uncovered_epoch(
    const StateCellPressureObserver *observer);
typedef struct {
    uint64_t epoch;
    uint64_t topology_epoch;
    size_t exposure_bytes;
    bool saturated;
} StateCellPressureNotice;
/* Read a coherent cumulative notice only if EXPECTED_EPOCH is still current.
 * Refusal leaves NOTICE unchanged. These counters do not acknowledge a
 * survivor census or successful host reclamation. */
bool state_cell_pressure_observer_notice(const StateCellPressureObserver *observer,
    uint64_t expected_epoch, StateCellPressureNotice *notice);
void state_cell_pressure_observer_begin(StateCellPressureObserver *observer);
/* A weak subscription never roots the node. The returned snapshot image pin
 * belongs to the caller and must be released after traversing that image.
 * Empty private nodes are valid node-only observations (snapshot.image=NULL). */
bool state_cell_pressure_observer_watch(StateCellPressureObserver *observer,
                                         StateCell *cell,
                                         StateCellSnapshot *snapshot,
                                         size_t *node_control_bytes);
/* Success atomically replaces the accepted watch set and returns its epoch.
 * A publication/retirement since a pending observation refuses the census. */
bool state_cell_pressure_observer_finish(StateCellPressureObserver *observer,
                                          uint64_t *accepted_epoch);
void state_cell_pressure_observer_cancel(StateCellPressureObserver *observer);
size_t state_cell_pressure_observer_bytes(const StateCellPressureObserver *observer);

typedef struct {
    StateCell *cell;
    uint64_t watch_id;
    uint64_t capture_id;
    uint64_t publication_epoch;
    StateCellSnapshot snapshot;
    size_t node_control_bytes;
    bool fresh_node;
} StateCellPressureDelta;
/* Accepted weak membership is monotone between successful full censuses.
 * A new member starts dirty. Certified Node owners supply CELL; false means
 * an unknown/retiring descriptor or allocation/counter exhaustion. */
bool state_cell_pressure_observer_ensure(StateCellPressureObserver *observer,
    StateCell *cell, bool *fresh_member);
/* Capture one queued version without allocation; false means no work (or
 * exhausted observer counters, whose notice remains saturated/dirty).
 * The returned image pin belongs to the caller, independent of the watch. */
bool state_cell_pressure_observer_next(StateCellPressureObserver *observer,
    StateCellPressureDelta *delta);
/* Pass the unchanged ticket exactly once after discovering its image edges.
 * Only the same live watch/capture/version can become covered. False work
 * or a changed version requeues; dead/replaced/replayed tickets do nothing.
 * This never releases DELTA.snapshot: the caller releases that pin separately. */
bool state_cell_pressure_observer_settle(StateCellPressureObserver *observer,
    const StateCellPressureDelta *delta, bool edges_discovered);

typedef struct {
    size_t nodes;
    size_t images;
    size_t image_bytes;
    size_t queued;
    size_t registry_height;
} StateCellDiagnostics;
StateCellDiagnostics state_cell_diagnostics(void);

typedef struct {
    size_t live_image_bytes;
    size_t dying_image_bytes;
    size_t peak_image_bytes;
    size_t live_node_bytes;
    bool saturated;
} StateCellStorageDiagnostics;
/* Direct image storage excludes outgoing leases. Dying bytes remain charged
 * until their Arena finalizers and control destruction have completed. */
StateCellStorageDiagnostics state_cell_storage_diagnostics(void);

#endif
