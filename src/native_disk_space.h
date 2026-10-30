#ifndef CETTA_NATIVE_DISK_SPACE_H
#define CETTA_NATIVE_DISK_SPACE_H

#include "native_disk_store.h"
#include "space.h"

/* Capture an encoded atom collection as a read-only ordinary Space source.
 * Each key under prefix is one occurrence; its value is one CAV1 atom.
 * occurrence_count must come from the collection's authoritative metadata in
 * the same captured view, not a guest estimate. A complete scan verifies it.
 * This low-level adapter does not define a collection mutation/metadata schema.
 *
 * target must be an initialized, empty, native atom Space without dependencies.
 * Success owns its view and prefix; the original view/store may immediately
 * close. Failure leaves target unchanged. Patterns select a complete scan of
 * the captured prefix until an independently qualified index is supplied.
 * Normal Space/matcher lifetimes apply: retain target while queries use it.
 * This is storage access, not an executable equation catalog or write grant.
 */
CettaDiskStoreStatus cetta_disk_space_capture(Space *target,
    const CettaDiskStoreView *view, CettaDiskBytes prefix, uint64_t occurrence_count);

#endif
