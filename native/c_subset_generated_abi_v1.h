#ifndef CETTA_C_SUBSET_GENERATED_ABI_V1_H
#define CETTA_C_SUBSET_GENERATED_ABI_V1_H

#include <stdbool.h>
#include <stdint.h>

/* Opaque exact-integer carrier selected by a separately qualified provider. */
typedef struct CettaCSubsetExactIntegerV1 CettaCSubsetExactIntegerV1;

typedef enum {
    CETTA_C_SUBSET_GENERATED_EXTERNAL_VALUE_V1 = 0,
    CETTA_C_SUBSET_GENERATED_EXTERNAL_LANGUAGE_FAULT_V1,
    CETTA_C_SUBSET_GENERATED_EXTERNAL_ENGINE_FAULT_V1,
    CETTA_C_SUBSET_GENERATED_EXTERNAL_RESOURCE_FAULT_V1
} CettaCSubsetGeneratedExternalV1;

typedef enum {
    CETTA_C_SUBSET_GENERATED_VALUE_V1 = 0,
    CETTA_C_SUBSET_GENERATED_DECLINED_V1,
    CETTA_C_SUBSET_GENERATED_LANGUAGE_FAULT_V1,
    CETTA_C_SUBSET_GENERATED_ENGINE_FAULT_V1,
    CETTA_C_SUBSET_GENERATED_RESOURCE_FAULT_V1
} CettaCSubsetGeneratedOutcomeV1;

typedef enum {
    CETTA_C_SUBSET_GENERATED_EVENT_STEP_V1 = 0,
    CETTA_C_SUBSET_GENERATED_EVENT_EXTERNAL_V1
} CettaCSubsetGeneratedEventKindV1;

typedef struct {
    CettaCSubsetGeneratedEventKindV1 kind;
    uint32_t instruction;
    uint32_t external;
    CettaCSubsetGeneratedExternalV1 external_outcome;
} CettaCSubsetGeneratedEventV1;

/*
 * Events are written in chronological order.  event_count is the required
 * length even when the caller's buffer is too small; complete is true exactly
 * when every event was retained.  Receipt storage never changes the semantic
 * outcome.
 */
typedef struct {
    CettaCSubsetGeneratedEventV1 *events;
    uint32_t event_capacity;
    uint32_t event_count;
    bool complete;
    CettaCSubsetGeneratedOutcomeV1 outcome;
} CettaCSubsetGeneratedReceiptV1;

static inline void cetta_c_subset_generated_receipt_init_v1(
    CettaCSubsetGeneratedReceiptV1 *receipt,
    CettaCSubsetGeneratedEventV1 *events,
    uint32_t event_capacity) {
    if (!receipt)
        return;
    receipt->events = events;
    receipt->event_capacity = event_capacity;
    receipt->event_count = 0u;
    receipt->complete = false;
    receipt->outcome = CETTA_C_SUBSET_GENERATED_ENGINE_FAULT_V1;
}

bool cetta_csubset_exact_integer_is_zero_v1(
    const CettaCSubsetExactIntegerV1 *value);

#endif /* CETTA_C_SUBSET_GENERATED_ABI_V1_H */
