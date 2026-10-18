#include "shared_return_producer.h"
#include "owned_return_image.h"

#include <stdlib.h>

bool cetta_shared_return_capture_image(
    void *context, const CettaOwnedReturn *borrowed, CettaSharedReturnRow *out) {
    if (!out)
        return false;
    CettaOwnedReturnImage *image = cetta_owned_return_image_capture(borrowed, context);
    if (!image)
        return false;
    *out = (CettaSharedReturnRow){
        .returned = cetta_owned_return_image_view(image),
        .owner = image, .release = cetta_owned_return_image_release,
    };
    return true;
}

struct CettaSharedReturnProducer {
    CettaOwnedExecution execution;
    CettaOccurrenceLog *log;
    CettaSharedReturnCapture capture;
    CettaSharedReturnCompatible compatible;
    void *context;
    CettaSharedReturnStep refusal;
    bool detached;
};

CettaSharedReturnProducer *cetta_shared_return_producer_new(
    CettaOwnedExecution *execution, size_t consumers, size_t row_limit,
    CettaSharedReturnCapture capture, CettaSharedReturnCompatible compatible,
    void *context) {
    if (!execution || !execution->computation || !execution->next || !execution->relocatable ||
        !capture || !compatible)
        return NULL;
    CettaSharedReturnProducer *producer = malloc(sizeof(*producer));
    if (!producer)
        return NULL;
    CettaOccurrenceLog *log = cetta_occurrence_log_new(consumers, row_limit);
    if (!log) {
        free(producer);
        return NULL;
    }
    *producer = (CettaSharedReturnProducer){
        .log = log,
        .capture = capture, .compatible = compatible, .context = context,
    };
    if (!cetta_owned_execution_move(&producer->execution, execution)) {
        cetta_occurrence_log_free(log);
        free(producer);
        return NULL;
    }
    return producer;
}

void cetta_shared_return_producer_free(CettaSharedReturnProducer *producer) {
    if (!producer)
        return;
    producer->detached = true;
    cetta_owned_execution_cancel(&producer->execution);
    cetta_occurrence_log_free(producer->log);
    free(producer);
}

static bool capture_return(void *context, const CettaOwnedReturn *returned) {
    CettaSharedReturnProducer *producer = context;
    if (!producer->compatible(producer->context)) {
        producer->refusal = CETTA_SHARED_RETURN_CONTEXT_REFUSED;
        return false;
    }
    CettaSharedReturnRow row = {0};
    if (!producer->capture(producer->context, returned, &row)) {
        producer->refusal = CETTA_SHARED_RETURN_CAPTURE_REFUSED;
        return false;
    }
    if (!producer->compatible(producer->context)) {
        if (row.release)
            row.release(row.owner);
        producer->refusal = CETTA_SHARED_RETURN_CONTEXT_REFUSED;
        return false;
    }
    CettaOccurrenceAdmission admission = row.returned
        ? cetta_occurrence_log_append(producer->log, (CettaOccurrenceRow){
            .value = row.returned, .owner = row.owner, .release = row.release,
        }) : CETTA_OCCURRENCE_APPEND_INVALID;
    if (admission == CETTA_OCCURRENCE_APPENDED)
        return true;
    if (row.release)
        row.release(row.owner);
    producer->refusal = admission == CETTA_OCCURRENCE_BACKPRESSURE
        ? CETTA_SHARED_RETURN_BACKPRESSURE
        : admission == CETTA_OCCURRENCE_CAPACITY
            ? CETTA_SHARED_RETURN_CAPACITY : CETTA_SHARED_RETURN_CAPTURE_REFUSED;
    return false;
}

static CettaSharedReturnStep execution_stop(CettaSharedReturnProducer *producer) {
    switch (producer->execution.stop) {
    case CETTA_OWNED_EXECUTION_COMPLETE:
        if (producer->execution.completion == CETTA_EVAL_COMPLETE) {
            cetta_occurrence_log_complete(producer->log);
            return CETTA_SHARED_RETURN_COMPLETE;
        }
        return CETTA_SHARED_RETURN_HANDOFF;
    case CETTA_OWNED_EXECUTION_HANDOFF:
        return CETTA_SHARED_RETURN_HANDOFF;
    case CETTA_OWNED_EXECUTION_INTERRUPTED:
        return CETTA_SHARED_RETURN_INTERRUPTED;
    default:
        return CETTA_SHARED_RETURN_PAUSED;
    }
}

CettaSharedReturnStep cetta_shared_return_producer_advance(
    CettaSharedReturnProducer *producer, uint64_t allowance) {
    if (!producer)
        return CETTA_SHARED_RETURN_INVALID;
    if (producer->detached)
        return CETTA_SHARED_RETURN_DETACHED;
    if (!producer->compatible(producer->context))
        return CETTA_SHARED_RETURN_CONTEXT_REFUSED;
    if (producer->execution.terminal)
        return execution_stop(producer);
    if (!allowance)
        return CETTA_SHARED_RETURN_PAUSED;
    CettaOccurrenceLogInfo info;
    if (!cetta_occurrence_log_info(producer->log, &info))
        return CETTA_SHARED_RETURN_INVALID;
    bool active = false;
    for (size_t i = 0u; i < info.consumers; ++i) {
        uint64_t position;
        if (cetta_occurrence_log_position(producer->log, i, &position)) {
            active = true;
            break;
        }
    }
    if (!active)
        return CETTA_SHARED_RETURN_NO_CONSUMERS;
    uint64_t room = (uint64_t)(info.limit - info.rows);
    if (!room)
        return CETTA_SHARED_RETURN_BACKPRESSURE;
    uint64_t remaining = UINT64_MAX - producer->execution.accepted;
    if (!remaining || info.past_end == UINT64_MAX)
        return CETTA_SHARED_RETURN_CAPACITY;
    if (allowance > room)
        allowance = room;
    if (allowance > remaining)
        allowance = remaining;
    if (allowance > UINT64_MAX - info.past_end)
        allowance = UINT64_MAX - info.past_end;
    producer->refusal = CETTA_SHARED_RETURN_PAUSED;
    CettaOwnedExecutionStep step = cetta_owned_execution_sequence(
        &producer->execution, allowance, capture_return, producer);
    if (step == CETTA_OWNED_EXECUTION_PAUSED)
        return producer->refusal;
    return execution_stop(producer);
}

static CettaSharedDelivery delivery(CettaOccurrenceReply reply) {
    CettaSharedDelivery out = {.kind = CETTA_SHARED_DELIVERY_INVALID};
    switch (reply.kind) {
    case CETTA_OCCURRENCE_ANSWER:
        out.kind = CETTA_SHARED_DELIVERY_ANSWER;
        out.position = reply.position;
        out.returned = reply.value;
        break;
    case CETTA_OCCURRENCE_PAUSED:
        out.kind = CETTA_SHARED_DELIVERY_PAUSED;
        break;
    case CETTA_OCCURRENCE_COMPLETED:
        out.kind = CETTA_SHARED_DELIVERY_COMPLETED;
        break;
    case CETTA_OCCURRENCE_CANCELLED:
        out.kind = CETTA_SHARED_DELIVERY_CANCELLED;
        break;
    case CETTA_OCCURRENCE_PUBLICATION_REFUSED:
        out.kind = CETTA_SHARED_DELIVERY_PUBLICATION_REFUSED;
        break;
    default:
        break;
    }
    return out;
}

CettaSharedDelivery cetta_shared_return_producer_peek(
    const CettaSharedReturnProducer *producer, size_t consumer) {
    CettaSharedDelivery out = delivery(cetta_occurrence_log_peek(
        producer ? producer->log : NULL, consumer));
    if (producer && out.kind != CETTA_SHARED_DELIVERY_INVALID &&
        out.kind != CETTA_SHARED_DELIVERY_CANCELLED &&
        !producer->compatible(producer->context))
        out = (CettaSharedDelivery){.kind = CETTA_SHARED_DELIVERY_CONTEXT_REFUSED};
    return out;
}

typedef struct {
    CettaSharedReturnPublish publish;
    void *context;
} Publication;

static bool publish_return(void *context, uint64_t position, const void *value) {
    Publication *publication = context;
    return publication->publish && publication->publish(
        publication->context, position, value);
}

CettaSharedDelivery cetta_shared_return_producer_take(
    CettaSharedReturnProducer *producer, size_t consumer,
    CettaSharedReturnPublish publish, void *context) {
    CettaSharedDelivery out = cetta_shared_return_producer_peek(producer, consumer);
    if (out.kind != CETTA_SHARED_DELIVERY_ANSWER)
        return out;
    Publication publication = {.publish = publish, .context = context};
    return delivery(cetta_occurrence_log_take(producer->log, consumer,
                                            publish_return, &publication));
}

bool cetta_shared_return_producer_cancel(
    CettaSharedReturnProducer *producer, size_t consumer) {
    return producer && cetta_occurrence_log_cancel(producer->log, consumer);
}

size_t cetta_shared_return_producer_trim(CettaSharedReturnProducer *producer) {
    return producer ? cetta_occurrence_log_trim(producer->log) : 0u;
}

bool cetta_shared_return_producer_detach_execution(
    CettaSharedReturnProducer *producer, CettaOwnedExecution *destination) {
    if (!producer || producer->detached ||
        !cetta_owned_execution_move(destination, &producer->execution))
        return false;
    producer->detached = true;
    return true;
}

const CettaOwnedExecution *cetta_shared_return_producer_execution(
    const CettaSharedReturnProducer *producer) {
    return producer && !producer->detached ? &producer->execution : NULL;
}

const CettaOccurrenceLog *cetta_shared_return_producer_log(
    const CettaSharedReturnProducer *producer) {
    return producer ? producer->log : NULL;
}
