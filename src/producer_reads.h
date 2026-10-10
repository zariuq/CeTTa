#ifndef CETTA_PRODUCER_READS_H
#define CETTA_PRODUCER_READS_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    size_t producer_plus_one;
    size_t revision;
} CettaProducerReadSlot;

/* One generator's observed producer revisions. Repeated reads retain the
 * first revision; observing another revision makes the round ineligible for
 * reuse. Missing answers are reads too. The owner supplies the meaning of a
 * revision and the separate purity/region license for skipping a round. */
typedef struct {
    union {
        CettaProducerReadSlot single;
        struct {
            CettaProducerReadSlot *slots;
            size_t capacity;
        } hash;
    } storage;
    size_t length;
    bool coherent;
    bool hashed;
} CettaProducerReads;

typedef bool (*CettaProducerRevisionFn)(
    const void *context, size_t producer, size_t *revision);
typedef bool (*CettaProducerReadFn)(void *context, size_t producer, size_t revision);

void cetta_producer_reads_init(CettaProducerReads *reads);
void cetta_producer_reads_free(CettaProducerReads *reads);
void cetta_producer_reads_begin(CettaProducerReads *reads);
/* Producer identifiers range from zero through SIZE_MAX - 1. Allocation or
 * representation failure disables reuse of this round, preserving the
 * ordinary generator route. BEGIN starts a new, independently checked round. */
void cetta_producer_reads_observe(
    CettaProducerReads *reads, size_t producer, size_t revision);
bool cetta_producer_reads_valid(
    const CettaProducerReads *reads, CettaProducerRevisionFn revision,
    const void *context);
/* Visit distinct captured reads in unspecified order. An incomplete capture
 * or a refused visit returns false. The callback must leave capture storage
 * alive and unchanged. This exposes dependencies without expanding answers. */
bool cetta_producer_reads_visit(const CettaProducerReads *reads,
    CettaProducerReadFn visit, void *context);
/* Owned buffer payload, including spare hash slots. A single dependency lives
 * in the header; excludes that header and allocator metadata. FREE retires
 * allocated storage when the producer completes or dies. */
size_t cetta_producer_reads_bytes(const CettaProducerReads *reads);

#endif
