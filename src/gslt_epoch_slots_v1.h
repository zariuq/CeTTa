#ifndef CETTA_GSLT_EPOCH_SLOTS_V1_H
#define CETTA_GSLT_EPOCH_SLOTS_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    void *values;
    uint64_t *epochs;
    uint32_t capacity;
    uint32_t width;
    size_t value_size;
    uint64_t epoch;
} CettaGsltEpochSlotsV1;

bool cetta_gslt_epoch_slots_prepare_v1(
    CettaGsltEpochSlotsV1 *slots, uint32_t capacity, size_t value_size);

void cetta_gslt_epoch_slots_free_v1(CettaGsltEpochSlotsV1 *slots);

void *cetta_gslt_epoch_slots_get_v1(
    CettaGsltEpochSlotsV1 *slots, uint32_t slot);

const void *cetta_gslt_epoch_slots_get_const_v1(
    const CettaGsltEpochSlotsV1 *slots, uint32_t slot);

void *cetta_gslt_epoch_slots_set_v1(
    CettaGsltEpochSlotsV1 *slots, uint32_t slot);

#endif /* CETTA_GSLT_EPOCH_SLOTS_V1_H */
