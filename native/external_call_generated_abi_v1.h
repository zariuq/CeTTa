#ifndef CETTA_EXTERNAL_CALL_GENERATED_ABI_V1_H
#define CETTA_EXTERNAL_CALL_GENERATED_ABI_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Opaque exact-integer carrier selected by a separately qualified provider. */
typedef struct CettaExternalCallExactIntegerV1 CettaExternalCallExactIntegerV1;

typedef enum {
    CETTA_EXTERNAL_CALL_GENERATED_EXTERNAL_VALUE_V1 = 0,
    CETTA_EXTERNAL_CALL_GENERATED_EXTERNAL_LANGUAGE_FAULT_V1,
    CETTA_EXTERNAL_CALL_GENERATED_EXTERNAL_ENGINE_FAULT_V1,
    CETTA_EXTERNAL_CALL_GENERATED_EXTERNAL_RESOURCE_FAULT_V1
} CettaExternalCallGeneratedExternalV1;

typedef enum {
    CETTA_EXTERNAL_CALL_GENERATED_VALUE_V1 = 0,
    CETTA_EXTERNAL_CALL_GENERATED_DECLINED_V1,
    CETTA_EXTERNAL_CALL_GENERATED_LANGUAGE_FAULT_V1,
    CETTA_EXTERNAL_CALL_GENERATED_ENGINE_FAULT_V1,
    CETTA_EXTERNAL_CALL_GENERATED_RESOURCE_FAULT_V1
} CettaExternalCallGeneratedOutcomeV1;

typedef enum {
    CETTA_EXTERNAL_CALL_GENERATED_EVENT_STEP_V1 = 0,
    CETTA_EXTERNAL_CALL_GENERATED_EVENT_EXTERNAL_V1
} CettaExternalCallGeneratedEventKindV1;

typedef struct {
    CettaExternalCallGeneratedEventKindV1 kind;
    uint32_t instruction;
    uint32_t external;
    CettaExternalCallGeneratedExternalV1 external_outcome;
} CettaExternalCallGeneratedEventV1;

/*
 * Events are written in chronological order.  event_count is the required
 * length even when the caller's buffer is too small; complete is true exactly
 * when every event was retained.  Receipt storage never changes the semantic
 * outcome.
 */
typedef struct {
    CettaExternalCallGeneratedEventV1 *events;
    uint32_t event_capacity;
    uint32_t event_count;
    bool complete;
    CettaExternalCallGeneratedOutcomeV1 outcome;
} CettaExternalCallGeneratedReceiptV1;

static inline void cetta_external_call_generated_receipt_init_v1(
    CettaExternalCallGeneratedReceiptV1 *receipt,
    CettaExternalCallGeneratedEventV1 *events,
    uint32_t event_capacity) {
    if (!receipt)
        return;
    receipt->events = events;
    receipt->event_capacity = event_capacity;
    receipt->event_count = 0u;
    receipt->complete = false;
    receipt->outcome = CETTA_EXTERNAL_CALL_GENERATED_ENGINE_FAULT_V1;
}

static inline bool cetta_external_call_generated_receipt_missing_v1(
    const CettaExternalCallGeneratedReceiptV1 *receipt) {
    return receipt == NULL;
}

static inline void cetta_external_call_generated_begin_v1(
    CettaExternalCallGeneratedReceiptV1 *receipt) {
    receipt->event_count = 0u;
    receipt->complete = true;
    receipt->outcome = CETTA_EXTERNAL_CALL_GENERATED_ENGINE_FAULT_V1;
}

static inline void cetta_external_call_generated_record_event_v1(
    CettaExternalCallGeneratedReceiptV1 *receipt,
    CettaExternalCallGeneratedEventKindV1 kind,
    uint32_t instruction,
    uint32_t external,
    CettaExternalCallGeneratedExternalV1 external_outcome) {
    uint32_t index = receipt->event_count++;

    if (!receipt->events || index >= receipt->event_capacity) {
        receipt->complete = false;
        return;
    }
    receipt->events[index].kind = kind;
    receipt->events[index].instruction = instruction;
    receipt->events[index].external = external;
    receipt->events[index].external_outcome = external_outcome;
}

static inline void cetta_external_call_generated_record_step_v1(
    CettaExternalCallGeneratedReceiptV1 *receipt,
    uint32_t instruction) {
    cetta_external_call_generated_record_event_v1(
        receipt, CETTA_EXTERNAL_CALL_GENERATED_EVENT_STEP_V1,
        instruction, 0u, CETTA_EXTERNAL_CALL_GENERATED_EXTERNAL_VALUE_V1);
}

static inline void cetta_external_call_generated_record_external_v1(
    CettaExternalCallGeneratedReceiptV1 *receipt,
    uint32_t instruction,
    uint32_t external,
    CettaExternalCallGeneratedExternalV1 external_outcome) {
    cetta_external_call_generated_record_event_v1(
        receipt, CETTA_EXTERNAL_CALL_GENERATED_EVENT_EXTERNAL_V1,
        instruction, external, external_outcome);
}

static inline void cetta_external_call_generated_mark_incomplete_v1(
    CettaExternalCallGeneratedReceiptV1 *receipt) {
    receipt->complete = false;
}

static inline CettaExternalCallGeneratedOutcomeV1
cetta_external_call_generated_finish_v1(
    CettaExternalCallGeneratedReceiptV1 *receipt,
    CettaExternalCallGeneratedOutcomeV1 outcome) {
    receipt->outcome = outcome;
    return outcome;
}

bool cetta_external_call_exact_integer_is_zero_v1(
    const CettaExternalCallExactIntegerV1 *value);

#endif /* CETTA_EXTERNAL_CALL_GENERATED_ABI_V1_H */
