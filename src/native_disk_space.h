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

/* Optional candidate indexes over one expression arity/position (head is
 * position zero). A posting key is PREFIX + "/exact/" + CAV1(value) + "/" +
 * full fact key, or PREFIX + "/any/" + full fact key. Its value is empty;
 * the occurrence reference is already the key suffix. Symbol/string fields
 * use exact postings. Every other field, and
 * every whole-variable fact, uses any; nonmatching expression arities and
 * nonvariable scalar facts have no posting for this index. Maintain all
 * postings atomically with facts/metadata in the same captured commit domain.
 * Numeric/compound probes keep the complete scan until their equality/index
 * normalization is independently qualified. Indexes only select candidates;
 * the ordinary matcher still owns all bindings and answer multiplicity. */
typedef struct { uint32_t arity, position; CettaDiskBytes prefix; } CettaDiskSpaceIndex;
CettaDiskStoreStatus cetta_disk_space_capture_indexed(Space *target,
    const CettaDiskStoreView *view, CettaDiskBytes prefix, uint64_t occurrence_count,
    const CettaDiskSpaceIndex *indexes, size_t index_count);
typedef struct {
    uint64_t scans, indexed_selections, candidates, decoded_bytes;
    bool overflow;
} CettaDiskSpaceStatistics;
/* Diagnostic totals across cursors of this source. They do not authorize a
 * result, describe internal index-node visits, or change its captured view. */
bool cetta_disk_space_statistics(const Space *source, CettaDiskSpaceStatistics *out);

#endif
