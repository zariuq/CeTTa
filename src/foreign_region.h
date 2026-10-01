#ifndef CETTA_FOREIGN_REGION_H
#define CETTA_FOREIGN_REGION_H

#include <SWI-Prolog.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* An attached engine's branch-owned term store. No engine is created here.
 * Regions and checkpoints are nested resources; only the innermost region
 * may be used. An open region belongs to its engine and current thread lease.
 * After moving that same engine, adopt() explicitly transfers the region
 * stack's lease. A snapshot instead restores an independent graph on an engine.
 * The enclosing query and engine must outlive the region. Release or detach
 * the region before returning from a foreign callback to that enclosing query.
 *
 * Stable variable keys address slots in one rooted term graph. References
 * returned by variable() belong to the current operation and expire at
 * finish(). Committing an operation retains bindings and attributes, never
 * its temporary references. A checkpoint restores both the graph and the
 * key catalogue; effects performed by Prolog are not replayed or undone.
 *
 * The caller supplies and observes goals through the existing host bridge.
 * This module owns storage and query lifetime, not a language substitution,
 * answer policy, predicate database, or scheduler. */
typedef struct CettaForeignRegion CettaForeignRegion;
typedef struct CettaForeignSnapshot CettaForeignSnapshot;

typedef enum {
    CETTA_FOREIGN_OK,
    CETTA_FOREIGN_INVALID,
    CETTA_FOREIGN_CAPACITY,
    CETTA_FOREIGN_FAULT,
    CETTA_FOREIGN_NOT_INNER,
} CettaForeignStatus;

typedef struct {
    uint64_t operations;
    uint64_t answers;
    uint64_t slot_steps;
    uint64_t snapshots;
    uint64_t snapshot_slots;
} CettaForeignRegionStats;

CettaForeignRegion *cetta_foreign_region_open(void);
CettaForeignStatus cetta_foreign_region_free(CettaForeignRegion *region);
/* The caller has exclusively attached the region's engine on this thread.
 * Only a quiescent innermost region at its enclosing query may be adopted;
 * the region stack moves together. No graph is copied and no query is cut. */
CettaForeignStatus cetta_foreign_region_adopt(CettaForeignRegion *region);

CettaForeignStatus cetta_foreign_region_checkpoint(
    CettaForeignRegion *region, uint32_t mark);
CettaForeignStatus cetta_foreign_region_rollback(
    CettaForeignRegion *region, uint32_t mark);
CettaForeignStatus cetta_foreign_region_commit(CettaForeignRegion *region);
/* Binding-history compaction: kept marks must be strictly increasing. A mark
 * is renamed to the last kept checkpoint at or before it. Older frames become
 * non-rewindable anchors until their younger frames have been discharged. */
CettaForeignStatus cetta_foreign_region_rebase(
    CettaForeignRegion *region, const uint32_t *kept, size_t count);
/* At a quiescent guest-step boundary, retain only the search's live rollback
 * horizons. Round each operation down to its preceding horizon, preserving
 * the original numeric marks. Native bindings are not renumbered. */
CettaForeignStatus cetta_foreign_region_frontier(
    CettaForeignRegion *region, const uint32_t *kept, size_t count);

/* Begin before converting inputs, then start and enumerate a goal. Only an
 * observed solution can be committed. next() returns SWI's extended status;
 * a negative value means an invalid native protocol operation. */
CettaForeignStatus cetta_foreign_region_begin(CettaForeignRegion *region);
term_t cetta_foreign_region_variable(CettaForeignRegion *region, uint64_t key);
CettaForeignStatus cetta_foreign_region_start(
    CettaForeignRegion *region, module_t module,
    predicate_t predicate, term_t arguments);
int cetta_foreign_region_next(CettaForeignRegion *region);
/* On a fault, *exception owns a recorded copy of the exact thrown term.
 * The caller releases it with PL_erase. Body faults precede cleanup faults.
 * A refused non-innermost release leaves the operation open and retryable. */
CettaForeignStatus cetta_foreign_region_finish(
    CettaForeignRegion *region, bool commit, record_t *exception);

/* Copying happens at an explicit branch-transfer boundary, not on each call.
 * A selected query answer can be copied while its cursor remains open; the
 * copy neither advances nor cuts that cursor. Otherwise no operation is open.
 * A snapshot retains attributed-variable sharing and cyclic terms. Restoring
 * it creates an independent graph; mutable sibling variables never alias. */
CettaForeignSnapshot *cetta_foreign_region_snapshot(CettaForeignRegion *region);
CettaForeignRegion *cetta_foreign_region_restore(
    const CettaForeignSnapshot *snapshot);
/* Snapshot release, like restoration, requires an attached SWI engine. */
void cetta_foreign_snapshot_free(CettaForeignSnapshot *snapshot);

size_t cetta_foreign_region_variable_count(const CettaForeignRegion *region);
CettaForeignRegionStats cetta_foreign_region_stats(const CettaForeignRegion *region);

#endif
