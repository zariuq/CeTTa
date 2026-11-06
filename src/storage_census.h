#ifndef CETTA_STORAGE_CENSUS_H
#define CETTA_STORAGE_CENSUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/* A read-only ownership census. Identity is an allocation address together
 * with its domain: an embedded Arena and its containing malloc block are
 * different allocations. Shared allocations are added once. The owner must
 * remain quiescent for the entire traversal. This is requested/accounted
 * storage, not an allocator's usable size or process RSS. */
typedef struct {
    const void *identity;
    size_t bytes;
    size_t retained;
    unsigned domain;
} CettaStorageObject;

struct CettaStorageCensus;
typedef void (*CettaStorageVisitor)(const void *owner, struct CettaStorageCensus *census);
typedef struct { const void *owner; CettaStorageVisitor visitor; } CettaStorageJob;

typedef struct CettaStorageCensus {
    CettaStorageObject *slots;
    size_t capacity;
    size_t length;
    size_t bytes;
    size_t retained;
    size_t repeated;
    /* The census's own work, as a function of the ownership graph alone: one
     * probe per identity lookup, and on each table growth one per old slot
     * scanned and one per entry moved.  Steps past an occupied home slot
     * depend on where the allocator placed the identities, so they are host
     * placement work and are not counted. */
    uint64_t probes;
    bool overflow;
    bool invalidated;
    const char *incomplete;
    CettaStorageJob *jobs;
    size_t job_length, job_capacity;
} CettaStorageCensus;


enum { CETTA_STORAGE_BLOCK, CETTA_STORAGE_ARENA, CETTA_STORAGE_VISITED };

static inline void cetta_storage_refuse(CettaStorageCensus *census, const char *reason) {
    if (!census->incomplete)
        census->incomplete = reason;
}

static inline void cetta_storage_invalidate(CettaStorageCensus *census, const char *reason) {
    census->invalidated = true;
    cetta_storage_refuse(census, reason);
}

static inline void cetta_storage_probe(CettaStorageCensus *census) {
    if (census->probes != UINT64_MAX) census->probes++;
    else cetta_storage_refuse(census, "ProbeOverflow");
}

static inline size_t cetta_storage_hash(const void *identity, unsigned domain) {
    uintptr_t value = (uintptr_t)identity;
    value ^= value >> 16u;
    value *= (uintptr_t)UINT32_C(0x7feb352d);
    value ^= value >> 15u;
    return (size_t)value ^ ((size_t)domain * (size_t)UINT32_C(0x9e3779b9));
}

/* True means this identity is newly recorded and its children need visiting.
 * False means either an already visited identity or explicit refusal. */
static inline bool cetta_storage_object_dual(CettaStorageCensus *census,
        const void *identity, size_t bytes, size_t retained, unsigned domain) {
    if (!identity)
        return false;
    if (census->length >= census->capacity / 2u) {
        size_t capacity = census->capacity ? census->capacity * 2u : 64u;
        if (capacity <= census->capacity || capacity > SIZE_MAX / sizeof(*census->slots)) {
            cetta_storage_refuse(census, "CensusCapacity");
            return false;
        }
        CettaStorageObject *slots = calloc(capacity, sizeof(*slots));
        if (!slots) {
            cetta_storage_refuse(census, "CensusAllocation");
            return false;
        }
        for (size_t i = 0u; i < census->capacity; i++) {
            cetta_storage_probe(census);
            CettaStorageObject item = census->slots[i];
            if (!item.identity)
                continue;
            size_t slot = cetta_storage_hash(item.identity, item.domain) & (capacity - 1u);
            cetta_storage_probe(census);
            while (slots[slot].identity)
                slot = (slot + 1u) & (capacity - 1u);
            slots[slot] = item;
        }
        free(census->slots);
        census->slots = slots;
        census->capacity = capacity;
    }
    size_t slot = cetta_storage_hash(identity, domain) & (census->capacity - 1u);
    cetta_storage_probe(census);
    for (;;) {
        CettaStorageObject *item = &census->slots[slot];
        if (!item->identity) {
            *item = (CettaStorageObject){identity, bytes, retained, domain};
            census->length++;
            if (bytes > SIZE_MAX - census->bytes) {
                census->bytes = SIZE_MAX;
                census->overflow = true;
            } else {
                census->bytes += bytes;
            }
            if (retained > SIZE_MAX - census->retained) {
                census->retained = SIZE_MAX;
                census->overflow = true;
            } else {
                census->retained += retained;
            }
            return true;
        }
        if (item->identity == identity && item->domain == domain) {
            if (item->bytes != bytes || item->retained != retained)
                cetta_storage_invalidate(census, "OwnerChangedDuringInspection");
            if (census->repeated != SIZE_MAX)
                census->repeated++;
            return false;
        }
        slot = (slot + 1u) & (census->capacity - 1u);
    }
}

static inline bool cetta_storage_object(CettaStorageCensus *census,
        const void *identity, size_t bytes, unsigned domain) {
    return cetta_storage_object_dual(census, identity, bytes, bytes, domain);
}

static inline bool cetta_storage_array(CettaStorageCensus *census,
        const void *identity, size_t count, size_t width, size_t header) {
    if (!identity)
        return false;
    if (width && count > (SIZE_MAX - header) / width) {
        census->overflow = true;
        cetta_storage_refuse(census, "ObjectSizeOverflow");
        return false;
    }
    return cetta_storage_object(census, identity, header + count * width, CETTA_STORAGE_BLOCK);
}

static inline void cetta_storage_schedule(CettaStorageCensus *census,
        const void *owner, CettaStorageVisitor visitor) {
    if (!owner) return;
    if (census->job_length == census->job_capacity) {
        size_t capacity = census->job_capacity ? census->job_capacity * 2u : 32u;
        if (capacity <= census->job_capacity || capacity > SIZE_MAX / sizeof(*census->jobs)) {
            cetta_storage_refuse(census, "CensusWorkCapacity");
            return;
        }
        CettaStorageJob *jobs = realloc(census->jobs, capacity * sizeof(*jobs));
        if (!jobs) { cetta_storage_refuse(census, "CensusWorkAllocation"); return; }
        census->jobs = jobs; census->job_capacity = capacity;
    }
    census->jobs[census->job_length++] = (CettaStorageJob){owner, visitor};
}

static inline void cetta_storage_finish(CettaStorageCensus *census) {
    while (census->job_length) {
        CettaStorageJob job = census->jobs[--census->job_length];
        job.visitor(job.owner, census);
    }
}

static inline void cetta_storage_census_free(CettaStorageCensus *census) {
    free(census->slots);
    free(census->jobs);
    *census = (CettaStorageCensus){0};
}

#endif
