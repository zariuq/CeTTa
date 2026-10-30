#ifndef CETTA_NATIVE_DISK_QUERY_H
#define CETTA_NATIVE_DISK_QUERY_H

#include "native_disk_space.h"

typedef struct CettaDiskQuery CettaDiskQuery;
typedef enum {
    CETTA_DISK_QUERY_PAUSED,
    CETTA_DISK_QUERY_ROW,
    CETTA_DISK_QUERY_END,
    CETTA_DISK_QUERY_INCOMPLETE,
    CETTA_DISK_QUERY_INVALIDATED,
    CETTA_DISK_QUERY_CAPACITY,
    CETTA_DISK_QUERY_TOO_LARGE,
    CETTA_DISK_QUERY_STALE,
    CETTA_DISK_QUERY_CANCELLED,
} CettaDiskQueryStep;

/* A serially owned captured Space/query lifetime for a service cursor. No
 * raw Space pointer escapes this owner. It uses the ordinary Space matcher;
 * pattern connectives and variable policy are unchanged. The template is
 * instantiated as data, never evaluated. Syntax must fit one joint CAV1
 * packet; shared variables in pattern/template are freshened together.
 * Inputs and the captured store view
 * may be released after open. Count has the metadata contract of capture().
 * A result must fit CAV1; failures are explicit, never empty success. */
CettaDiskStoreStatus cetta_disk_query_open(const CettaDiskStoreView *view,
    CettaDiskBytes prefix, uint64_t count, Atom *pattern, Atom *template,
    CettaDiskQuery **out);

/* Bounded matcher transitions; each grants at most 32 matcher node actions.
 * This is not a byte/CPU bound for individual primitives or serialization.
 * A row is CAV1 (DiskQueryRowV1 instantiated-template (occurrence-ids ...)).
 * bytes is borrowed until the next next()/cancel()/close(), not checkpoint().
 * Sequence starts at zero. An unchanged last sequence replays identical bytes
 * until the next sequence is requested; it does not advance or match again.
 * TOO_LARGE preserves its pending row and reports the required byte count.
 * PAUSED, INCOMPLETE, CAPACITY, INVALIDATED and CANCELLED are not END.
 * END/cancel release the cursor, snapshot, arenas and universe immediately. */
CettaDiskQueryStep cetta_disk_query_next(CettaDiskQuery *query,
    uint64_t sequence, uint64_t transitions, size_t maximum_bytes,
    CettaDiskBytes *bytes, size_t *required_bytes);

/* Relocate only live continuation/template state, then retire the old arena.
 * Call between pages/work quanta to avoid retaining all earlier answers.
 * Failure leaves the original resumable cursor and any replay row untouched.
 * No query/matcher transition occurs. */
bool cetta_disk_query_checkpoint(CettaDiskQuery *query);
CettaDiskQueryStep cetta_disk_query_cancel(CettaDiskQuery *query);
void cetta_disk_query_close(CettaDiskQuery *query);
void cetta_disk_query_storage_census(const CettaDiskQuery *query,
    CettaStorageCensus *census);
uint64_t cetta_disk_query_revision(const CettaDiskQuery *query);

#endif
