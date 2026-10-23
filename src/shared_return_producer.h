#ifndef CETTA_SHARED_RETURN_PRODUCER_H
#define CETTA_SHARED_RETURN_PRODUCER_H

#include "occurrence_log.h"
#include "owned_execution.h"

/* One explicitly selected, qualified computation, with independent delivery
 * to a fixed consumer cohort. This does not identify separate calls or choose
 * their alternatives. Calls and callbacks are serialized; callbacks may read
 * diagnostics but must not mutate the producer or its log. */
typedef struct CettaSharedReturnProducer CettaSharedReturnProducer;
typedef struct CettaOwnedReturnImagePolicy CettaOwnedReturnImagePolicy;

typedef struct {
    const CettaOwnedReturn *returned;
    void *owner;
    void (*release)(void *owner);
} CettaSharedReturnRow;

/* Capture the complete return, including provenance, environment, constraints,
 * delayed work and resources it can reach, independently of the computation.
 * True transfers one ownership share to out; false retains all attempted
 * allocations in the callback. Neither result advances the computation. */
typedef bool (*CettaSharedReturnCapture)(
    void *context, const CettaOwnedReturn *borrowed, CettaSharedReturnRow *out);
/* The host checks captured dependencies and authority at resumption and
 * buffered delivery. Refusal keeps the complete execution and unread rows.
 * A successful check does not grant context transport or shared-choice laws. */
typedef bool (*CettaSharedReturnCompatible)(void *context);

/* Buffered publication is validated at entry. The callback's successful
 * acceptance precedes acknowledgment under that entrance context. Any
 * external context change is checked at the next delivery/resumption; this
 * interface provides neither post-publication validation nor rollback of a
 * caller's accepted answer. A fixed-context microstep requires a publication
 * callback that preserves that context throughout acceptance. */

/* Capture with the common owned logical-image adapter. Context points to an
 * optional CettaOwnedReturnImagePolicy. Publication may retain the returned
 * image through owned_return_image.h before acknowledging its occurrence.
 * Compatibility and permission to share this computation remain host checks. */
bool cetta_shared_return_capture_image(
    void *context, const CettaOwnedReturn *borrowed, CettaSharedReturnRow *out);

typedef enum {
    CETTA_SHARED_RETURN_PAUSED,
    CETTA_SHARED_RETURN_BACKPRESSURE,
    CETTA_SHARED_RETURN_CAPACITY,
    CETTA_SHARED_RETURN_CAPTURE_REFUSED,
    CETTA_SHARED_RETURN_CONTEXT_REFUSED,
    CETTA_SHARED_RETURN_COMPLETE,
    CETTA_SHARED_RETURN_HANDOFF,
    CETTA_SHARED_RETURN_INTERRUPTED,
    CETTA_SHARED_RETURN_NO_CONSUMERS,
    CETTA_SHARED_RETURN_DETACHED,
    CETTA_SHARED_RETURN_INVALID,
    CETTA_SHARED_RETURN_APPENDED,
} CettaSharedReturnStep;

/* Admit one already-earned return from a resident computation to the same
 * occurrence log used by an owned producer. This does not step, move, cancel
 * or complete that computation. APPENDED alone permits its publication cursor
 * to advance. Every refusal leaves the borrowed return with its caller; retry
 * must present that return, not rerun the source. Capture protects the complete
 * graph, and compatibility is checked both before and after capture.
 *
 * The caller owns the log, cohort and actual source completion. Empty or full
 * storage is never evidence of exhaustion. A completed or full log refuses
 * before capture, so it does not allocate another retained image. */
CettaSharedReturnStep cetta_shared_return_capture_append(
    CettaOccurrenceLog *log, const CettaOwnedReturn *returned,
    CettaSharedReturnCapture capture, CettaSharedReturnCompatible compatible,
    void *context);

typedef enum {
    CETTA_SHARED_DELIVERY_ANSWER,
    CETTA_SHARED_DELIVERY_PAUSED,
    CETTA_SHARED_DELIVERY_COMPLETED,
    CETTA_SHARED_DELIVERY_CANCELLED,
    CETTA_SHARED_DELIVERY_INVALID,
    CETTA_SHARED_DELIVERY_PUBLICATION_REFUSED,
    CETTA_SHARED_DELIVERY_CONTEXT_REFUSED,
} CettaSharedDeliveryKind;

typedef struct {
    CettaSharedDeliveryKind kind;
    uint64_t position;
    const CettaOwnedReturn *returned;
} CettaSharedDelivery;

/* Success moves execution, including any pending return, into the producer
 * and clears the source handle. Failure leaves it unchanged. The callback
 * context and the execution's source authority must outlive the producer.
 * The native adapter must have qualified relocation of this driver. */
CettaSharedReturnProducer *cetta_shared_return_producer_new(
    CettaOwnedExecution *execution, size_t consumers, size_t row_limit,
    CettaSharedReturnCapture capture, CettaSharedReturnCompatible compatible,
    void *context);
void cetta_shared_return_producer_free(CettaSharedReturnProducer *producer);

/* Allowance counts admitted return occurrences, not semantic transitions,
 * internal work or callback latency. A full log prevents producer advancement.
 * Capture/allocation refusal retains its pending borrowed return for retry.
 * HANDOFF and INTERRUPTED retain the residual, and never close the answer log.
 * Only an authoritative complete frontier marks the log complete. */
CettaSharedReturnStep cetta_shared_return_producer_advance(
    CettaSharedReturnProducer *producer, uint64_t allowance);

/* A peek borrows a return until its occurrence is trimmed or the producer is
 * freed. Publication must independently protect the complete return graph;
 * only a successful publication advances that consumer's position. */
typedef bool (*CettaSharedReturnPublish)(
    void *context, uint64_t position, const CettaOwnedReturn *returned);
CettaSharedDelivery cetta_shared_return_producer_peek(
    const CettaSharedReturnProducer *producer, size_t consumer);
CettaSharedDelivery cetta_shared_return_producer_take(
    CettaSharedReturnProducer *producer, size_t consumer,
    CettaSharedReturnPublish publish, void *context);
bool cetta_shared_return_producer_cancel(
    CettaSharedReturnProducer *producer, size_t consumer);
size_t cetta_shared_return_producer_trim(CettaSharedReturnProducer *producer);

/* Move the complete execution, including its pending return and stop reason,
 * back to the host for continuation/handoff. Destination must be initialized
 * and empty. Buffered returns retain their own owners and remain deliverable.
 * Detachment is not exhaustion and does not close the log. */
bool cetta_shared_return_producer_detach_execution(
    CettaSharedReturnProducer *producer, CettaOwnedExecution *destination);
const CettaOwnedExecution *cetta_shared_return_producer_execution(
    const CettaSharedReturnProducer *producer);
const CettaOccurrenceLog *cetta_shared_return_producer_log(
    const CettaSharedReturnProducer *producer);

#endif
