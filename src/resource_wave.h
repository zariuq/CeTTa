#ifndef CETTA_RESOURCE_WAVE_H
#define CETTA_RESOURCE_WAVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Namespaces prevent an agenda occurrence, a cell and a stored fact with the
 * same numeric index from becoming the same resource. */
typedef struct {
    uint64_t kind;
    uint64_t owner;
    uint64_t item;
} CettaResourceIdentity;

typedef struct {
    CettaResourceIdentity identity;
    uint64_t available;
} CettaResourceSupply;

typedef struct {
    CettaResourceIdentity identity;
    uint64_t consume;
    uint64_t read;
} CettaResourceClaim;

typedef struct {
    const CettaResourceClaim *claims;
    size_t length;
    bool complete;
} CettaResourceFootprint;

typedef enum {
    CETTA_WAVE_ADMITTED = 0,
    CETTA_WAVE_DEFERRED_CONFLICT,
    CETTA_WAVE_DEFERRED_UNKNOWN,
} CettaResourceWaveDecision;

typedef struct {
    CettaResourceWaveDecision *decisions;
    size_t length;
    size_t admitted;
    size_t deferred;
    uint64_t *consumed;
    uint64_t *read;
    size_t resource_count;
} CettaResourceWave;

int cetta_resource_identity_compare(CettaResourceIdentity left,
                                    CettaResourceIdentity right);
/* Supplies and each complete footprint are strictly increasing by identity.
 * A malformed input refuses the selection; an incomplete footprint or unknown
 * resource is retained as deferred. The scan preserves physical task order.
 * Reads combine by maximum; consumes add. Admission checks the whole combined
 * consume-plus-read demand, including earlier readers of a later consume. */
bool cetta_resource_wave_select(const CettaResourceSupply *supply,
    size_t resource_count, const CettaResourceFootprint *work, size_t length,
    CettaResourceWave *result);
void cetta_resource_wave_free(CettaResourceWave *wave);

#endif
