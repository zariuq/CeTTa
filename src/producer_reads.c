#include "producer_reads.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void cetta_producer_reads_init(CettaProducerReads *reads) {
    if (reads)
        *reads = (CettaProducerReads){0};
}

void cetta_producer_reads_free(CettaProducerReads *reads) {
    if (reads) {
        if (reads->hashed)
            free(reads->storage.hash.slots);
        cetta_producer_reads_init(reads);
    }
}

void cetta_producer_reads_begin(CettaProducerReads *reads) {
    if (!reads)
        return;
    if (reads->hashed)
        memset(reads->storage.hash.slots, 0, cetta_producer_reads_bytes(reads));
    else
        reads->storage.single = (CettaProducerReadSlot){0};
    reads->length = 0u;
    reads->coherent = true;
}

static size_t producer_hash(size_t producer) {
    uint64_t value = (uint64_t)producer;
    value ^= value >> 30;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27;
    value *= UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31;
    return (size_t)value;
}

static CettaProducerReadSlot *producer_slot(
    CettaProducerReadSlot *slots, size_t capacity, size_t producer) {
    size_t position = producer_hash(producer) & (capacity - 1u);
    while (slots[position].producer_plus_one &&
           slots[position].producer_plus_one != producer + 1u)
        position = (position + 1u) & (capacity - 1u);
    return slots + position;
}

static bool producer_reads_grow(CettaProducerReads *reads) {
    size_t old_capacity = reads->hashed ? reads->storage.hash.capacity : 0u;
    if (old_capacity > SIZE_MAX / 2u)
        return false;
    size_t capacity = old_capacity ? old_capacity * 2u : 16u;
    if (capacity > SIZE_MAX / sizeof(CettaProducerReadSlot))
        return false;
    CettaProducerReadSlot *slots = calloc(capacity, sizeof(*slots));
    if (!slots)
        return false;
    if (reads->hashed) {
        for (size_t i = 0u; i < old_capacity; i++) {
            CettaProducerReadSlot old = reads->storage.hash.slots[i];
            if (old.producer_plus_one)
                *producer_slot(slots, capacity, old.producer_plus_one - 1u) = old;
        }
        free(reads->storage.hash.slots);
    } else if (reads->length) {
        CettaProducerReadSlot old = reads->storage.single;
        *producer_slot(slots, capacity, old.producer_plus_one - 1u) = old;
    }
    reads->storage.hash.slots = slots;
    reads->storage.hash.capacity = capacity;
    reads->hashed = true;
    return true;
}

void cetta_producer_reads_observe(
    CettaProducerReads *reads, size_t producer, size_t revision) {
    if (!reads || !reads->coherent)
        return;
    if (producer == SIZE_MAX) {
        reads->coherent = false;
        return;
    }
    if (!reads->hashed) {
        if (!reads->length) {
            reads->storage.single = (CettaProducerReadSlot){producer + 1u, revision};
            reads->length = 1u;
            return;
        }
        if (reads->storage.single.producer_plus_one == producer + 1u) {
            reads->coherent = reads->storage.single.revision == revision;
            return;
        }
        if (!producer_reads_grow(reads)) {
            reads->coherent = false;
            return;
        }
    }
    CettaProducerReadSlot *slot = producer_slot(
        reads->storage.hash.slots, reads->storage.hash.capacity, producer);
    if (slot->producer_plus_one) {
        reads->coherent = slot->revision == revision;
        return;
    }
    if (reads->length == reads->storage.hash.capacity / 2u) {
        if (!producer_reads_grow(reads)) {
            reads->coherent = false;
            return;
        }
        slot = producer_slot(reads->storage.hash.slots,
            reads->storage.hash.capacity, producer);
    }
    *slot = (CettaProducerReadSlot){producer + 1u, revision};
    reads->length++;
}

static bool producer_read_valid(const CettaProducerReadSlot *slot,
    CettaProducerRevisionFn revision, const void *context) {
    size_t current = 0u;
    if (!revision(context, slot->producer_plus_one - 1u, &current) ||
        current != slot->revision)
        return false;
    return true;
}

bool cetta_producer_reads_valid(
    const CettaProducerReads *reads, CettaProducerRevisionFn revision,
    const void *context) {
    if (!reads || !reads->coherent || !revision)
        return false;
    if (!reads->hashed)
        return !reads->length ||
            producer_read_valid(&reads->storage.single, revision, context);
    for (size_t i = 0u; i < reads->storage.hash.capacity; i++) {
        const CettaProducerReadSlot *slot = reads->storage.hash.slots + i;
        if (!slot->producer_plus_one)
            continue;
        if (!producer_read_valid(slot, revision, context))
            return false;
    }
    return true;
}

bool cetta_producer_reads_visit(const CettaProducerReads *reads,
    CettaProducerReadFn visit, void *context) {
    if (!reads || !reads->coherent || !visit)
        return false;
    if (!reads->hashed)
        return !reads->length || visit(context,
            reads->storage.single.producer_plus_one - 1u, reads->storage.single.revision);
    for (size_t i = 0u; i < reads->storage.hash.capacity; i++) {
        const CettaProducerReadSlot *slot = reads->storage.hash.slots + i;
        if (slot->producer_plus_one &&
            !visit(context, slot->producer_plus_one - 1u, slot->revision))
            return false;
    }
    return true;
}

size_t cetta_producer_reads_bytes(const CettaProducerReads *reads) {
    return reads && reads->hashed
        ? reads->storage.hash.capacity * sizeof(CettaProducerReadSlot) : 0u;
}
