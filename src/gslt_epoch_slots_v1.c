#include "gslt_epoch_slots_v1.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

bool cetta_gslt_epoch_slots_prepare_v1(
    CettaGsltEpochSlotsV1 *slots, uint32_t capacity, size_t value_size) {
    void *values;
    uint64_t *epochs;

    if (!slots || value_size == 0u ||
        (capacity > 0u &&
         ((size_t)capacity > SIZE_MAX / value_size ||
          (size_t)capacity > SIZE_MAX / sizeof(*epochs))))
        return false;
    if (slots->value_size != 0u && slots->value_size != value_size)
        return false;
    if (capacity > slots->capacity) {
        values = calloc(capacity, value_size);
        epochs = calloc(capacity, sizeof(*epochs));
        if (!values || !epochs) {
            free(values);
            free(epochs);
            return false;
        }
        free(slots->values);
        free(slots->epochs);
        slots->values = values;
        slots->epochs = epochs;
        slots->capacity = capacity;
        slots->value_size = value_size;
        slots->epoch = 0u;
    } else if (slots->value_size == 0u) {
        slots->value_size = value_size;
    }
    slots->width = capacity;
    if (slots->epoch == UINT64_MAX) {
        if (slots->capacity > 0u)
            memset(slots->epochs, 0,
                   (size_t)slots->capacity * sizeof(*slots->epochs));
        slots->epoch = 1u;
    } else {
        slots->epoch++;
        if (slots->epoch == 0u)
            slots->epoch = 1u;
    }
    return true;
}

void cetta_gslt_epoch_slots_free_v1(CettaGsltEpochSlotsV1 *slots) {
    if (!slots)
        return;
    free(slots->values);
    free(slots->epochs);
    memset(slots, 0, sizeof(*slots));
}

static void *epoch_slot_address(const CettaGsltEpochSlotsV1 *slots,
                                uint32_t slot) {
    if (!slots || !slots->values || slots->value_size == 0u ||
        slot >= slots->width)
        return NULL;
    return (uint8_t *)slots->values + (size_t)slot * slots->value_size;
}

void *cetta_gslt_epoch_slots_get_v1(
    CettaGsltEpochSlotsV1 *slots, uint32_t slot) {
    if (!slots || !slots->epochs || slot >= slots->width ||
        slots->epochs[slot] != slots->epoch)
        return NULL;
    return epoch_slot_address(slots, slot);
}

const void *cetta_gslt_epoch_slots_get_const_v1(
    const CettaGsltEpochSlotsV1 *slots, uint32_t slot) {
    if (!slots || !slots->epochs || slot >= slots->width ||
        slots->epochs[slot] != slots->epoch)
        return NULL;
    return epoch_slot_address(slots, slot);
}

void *cetta_gslt_epoch_slots_set_v1(
    CettaGsltEpochSlotsV1 *slots, uint32_t slot) {
    void *value = epoch_slot_address(slots, slot);
    if (!value || !slots->epochs || slots->epoch == 0u)
        return NULL;
    slots->epochs[slot] = slots->epoch;
    return value;
}
