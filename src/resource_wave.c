#include "resource_wave.h"

#include <stdlib.h>
#include <string.h>

int cetta_resource_identity_compare(CettaResourceIdentity left,
                                    CettaResourceIdentity right) {
    if (left.kind != right.kind)
        return left.kind < right.kind ? -1 : 1;
    if (left.owner != right.owner)
        return left.owner < right.owner ? -1 : 1;
    if (left.item != right.item)
        return left.item < right.item ? -1 : 1;
    return 0;
}

/* Check collective natural-number demand without unsigned addition. */
static inline bool resource_demand_fits(uint64_t available, uint64_t read,
                                      uint64_t consumed, uint64_t added) {
    if (read > available)
        return false;
    if (consumed > available - read)
        return false;
    return added <= available - read - consumed;
}

static size_t resource_index(const CettaResourceSupply *supply,
    size_t count, CettaResourceIdentity identity) {
    size_t low = 0u, high = count;
    while (low < high) {
        size_t mid = low + (high - low) / 2u;
        int comparison = cetta_resource_identity_compare(
            supply[mid].identity, identity);
        if (comparison < 0)
            low = mid + 1u;
        else
            high = mid;
    }
    return low < count && cetta_resource_identity_compare(
        supply[low].identity, identity) == 0 ? low : count;
}

void cetta_resource_wave_free(CettaResourceWave *wave) {
    if (!wave)
        return;
    free(wave->decisions);
    free(wave->consumed);
    free(wave->read);
    memset(wave, 0, sizeof(*wave));
}

bool cetta_resource_wave_select(const CettaResourceSupply *supply,
    size_t resource_count, const CettaResourceFootprint *work, size_t length,
    CettaResourceWave *result) {
    if (!result || result->decisions || result->consumed || result->read ||
        result->length || result->resource_count || (resource_count && !supply) || (length && !work) ||
        resource_count > SIZE_MAX / sizeof(uint64_t) ||
        length > SIZE_MAX / sizeof(CettaResourceWaveDecision))
        return false;
    for (size_t i = 1u; i < resource_count; i++)
        if (cetta_resource_identity_compare(supply[i - 1u].identity,
                                            supply[i].identity) >= 0)
            return false;
    for (size_t i = 0u; i < length; i++) {
        if (work[i].length && !work[i].claims)
            return false;
        for (size_t j = 1u; j < work[i].length; j++)
            if (cetta_resource_identity_compare(work[i].claims[j - 1u].identity,
                                                work[i].claims[j].identity) >= 0)
                return false;
    }
    CettaResourceWave fresh = {0};
    fresh.length = length;
    fresh.resource_count = resource_count;
    fresh.decisions = calloc(length ? length : 1u, sizeof(*fresh.decisions));
    fresh.consumed = calloc(resource_count ? resource_count : 1u,
                            sizeof(*fresh.consumed));
    fresh.read = calloc(resource_count ? resource_count : 1u, sizeof(*fresh.read));
    if (!fresh.decisions || !fresh.consumed || !fresh.read) {
        cetta_resource_wave_free(&fresh);
        return false;
    }
    for (size_t i = 0u; i < length; i++) {
        CettaResourceWaveDecision decision = work[i].complete
            ? CETTA_WAVE_ADMITTED : CETTA_WAVE_DEFERRED_UNKNOWN;
        for (size_t j = 0u; decision == CETTA_WAVE_ADMITTED &&
             j < work[i].length; j++) {
            const CettaResourceClaim *claim = &work[i].claims[j];
            size_t index = resource_index(supply, resource_count, claim->identity);
            if (index == resource_count) {
                decision = CETTA_WAVE_DEFERRED_UNKNOWN;
                break;
            }
            uint64_t available = supply[index].available;
            uint64_t read = claim->read > fresh.read[index]
                ? claim->read : fresh.read[index];
            /* Subtraction gives the natural-number inequality without
             * permitting unsigned wraparound to authorize a wave. */
            if (!resource_demand_fits(available, read,
                                      fresh.consumed[index], claim->consume))
                decision = CETTA_WAVE_DEFERRED_CONFLICT;
        }
        fresh.decisions[i] = decision;
        if (decision == CETTA_WAVE_ADMITTED) {
            for (size_t j = 0u; j < work[i].length; j++) {
                const CettaResourceClaim *claim = &work[i].claims[j];
                size_t index = resource_index(supply, resource_count, claim->identity);
                fresh.consumed[index] += claim->consume;
                if (claim->read > fresh.read[index])
                    fresh.read[index] = claim->read;
            }
            fresh.admitted++;
        } else {
            fresh.deferred++;
        }
    }
    *result = fresh;
    return true;
}
