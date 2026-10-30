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
    size_t references;
    size_t checkpoints;
    CettaSharedReturnCheckpoint *checkpoint_head;
    size_t checkpoint_pin;
    bool checkpoint_pin_active;
    bool detached;
    bool copy_pending;
};

struct CettaSharedReturnCheckpoint {
    CettaSharedReturnProducer *producer;
    uint64_t position;
    size_t references;
    CettaSharedReturnCheckpoint *previous, *next;
    bool retains_producer;
};

void cetta_shared_return_producer_storage(const CettaSharedReturnProducer *producer,
    CettaStorageCensus *census,CettaStorageVisitor computation_storage,CettaStorageVisitor row_storage) {
    if(!producer || !cetta_storage_object(census,producer,sizeof(*producer),CETTA_STORAGE_BLOCK)) return;
    if (producer->copy_pending) {
        cetta_storage_refuse(census, "UnpublishedReturnCopy");
        return;
    }
    cetta_occurrence_log_storage(producer->log,census,row_storage);
    for(const CettaSharedReturnCheckpoint *checkpoint=producer->checkpoint_head;checkpoint;checkpoint=checkpoint->next)
        cetta_storage_object(census,checkpoint,sizeof(*checkpoint),CETTA_STORAGE_BLOCK);
    if(producer->execution.computation) {
        if(computation_storage) cetta_storage_schedule(census,producer->execution.computation,computation_storage);
        else cetta_storage_refuse(census,"OpaqueSharedComputationOwner");
    }
}
void cetta_shared_return_checkpoint_storage(const CettaSharedReturnCheckpoint *checkpoint,
    CettaStorageCensus *census,CettaStorageVisitor computation_storage,CettaStorageVisitor row_storage) {
    if(!checkpoint || !cetta_storage_object(census,checkpoint,sizeof(*checkpoint),CETTA_STORAGE_BLOCK)) return;
    if(checkpoint->retains_producer)
        cetta_shared_return_producer_storage(checkpoint->producer,census,computation_storage,row_storage);
}

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
        .references = 1u,
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
    assert(producer->references);
    if (--producer->references) return;
    producer->detached = true;
    /* Strong bookmarks prevent this last release. Scoped bookmarks do not:
     * invalidate all of them before cancellation/row finalizers can release
     * another context or inspect a sibling bookmark. */
    CettaSharedReturnCheckpoint *checkpoint=producer->checkpoint_head;
    producer->checkpoint_head=NULL;
    producer->checkpoints=0u;
    while (checkpoint) {
        CettaSharedReturnCheckpoint *next=checkpoint->next;
        assert(!checkpoint->retains_producer);
        checkpoint->producer=NULL;
        checkpoint->previous=checkpoint->next=NULL;
        checkpoint=next;
    }
    cetta_owned_execution_cancel(&producer->execution);
    cetta_occurrence_log_free(producer->log);
    free(producer);
}

CettaSharedReturnStep cetta_shared_return_capture_append(
    CettaOccurrenceLog *log, const CettaOwnedReturn *returned,
    CettaSharedReturnCapture capture, CettaSharedReturnCompatible compatible,
    void *context) {
    if (!log || !returned || !capture || !compatible)
        return CETTA_SHARED_RETURN_INVALID;
    if (!compatible(context))
        return CETTA_SHARED_RETURN_CONTEXT_REFUSED;
    CettaOccurrenceLogInfo info;
    if (!cetta_occurrence_log_info(log, &info))
        return CETTA_SHARED_RETURN_INVALID;
    if (info.completed)
        return CETTA_SHARED_RETURN_COMPLETE;
    if (info.rows == info.limit)
        return CETTA_SHARED_RETURN_BACKPRESSURE;
    if (info.past_end == UINT64_MAX)
        return CETTA_SHARED_RETURN_CAPACITY;
    CettaSharedReturnRow row = {0};
    if (!capture(context, returned, &row))
        return CETTA_SHARED_RETURN_CAPTURE_REFUSED;
    if (!compatible(context)) {
        if (row.release)
            row.release(row.owner);
        return CETTA_SHARED_RETURN_CONTEXT_REFUSED;
    }
    CettaOccurrenceAdmission admission = row.returned
        ? cetta_occurrence_log_append(log, (CettaOccurrenceRow){
            .value = row.returned, .owner = row.owner, .release = row.release,
        }) : CETTA_OCCURRENCE_APPEND_INVALID;
    if (admission == CETTA_OCCURRENCE_APPENDED)
        return CETTA_SHARED_RETURN_APPENDED;
    if (row.release)
        row.release(row.owner);
    return admission == CETTA_OCCURRENCE_BACKPRESSURE
        ? CETTA_SHARED_RETURN_BACKPRESSURE
        : admission == CETTA_OCCURRENCE_CAPACITY
            ? CETTA_SHARED_RETURN_CAPACITY : CETTA_SHARED_RETURN_CAPTURE_REFUSED;
}

static bool capture_return(void *context, const CettaOwnedReturn *returned) {
    CettaSharedReturnProducer *producer = context;
    CettaSharedReturnStep step = cetta_shared_return_capture_append(
        producer->log, returned, producer->capture, producer->compatible,
        producer->context);
    if (step == CETTA_SHARED_RETURN_APPENDED)
        return true;
    producer->refusal = step;
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
    if (!producer || producer->copy_pending)
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
    if (producer && producer->copy_pending)
        return (CettaSharedDelivery){.kind = CETTA_SHARED_DELIVERY_INVALID};
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
    if (producer && producer->checkpoint_pin_active && consumer == producer->checkpoint_pin)
        return (CettaSharedDelivery){.kind = CETTA_SHARED_DELIVERY_INVALID};
    CettaSharedDelivery out = cetta_shared_return_producer_peek(producer, consumer);
    if (out.kind != CETTA_SHARED_DELIVERY_ANSWER)
        return out;
    Publication publication = {.publish = publish, .context = context};
    return delivery(cetta_occurrence_log_take(producer->log, consumer,
                                            publish_return, &publication));
}

bool cetta_shared_return_producer_cancel(
    CettaSharedReturnProducer *producer, size_t consumer) {
    return producer && !producer->copy_pending &&
        (!producer->checkpoint_pin_active || consumer != producer->checkpoint_pin) &&
        cetta_occurrence_log_cancel(producer->log, consumer);
}

size_t cetta_shared_return_producer_trim(CettaSharedReturnProducer *producer) {
    return producer && !producer->copy_pending ? cetta_occurrence_log_trim(producer->log) : 0u;
}

bool cetta_shared_return_producer_detach_execution(
    CettaSharedReturnProducer *producer, CettaOwnedExecution *destination) {
    if (!producer || producer->copy_pending || producer->detached || producer->checkpoints ||
        !cetta_owned_execution_move(destination, &producer->execution))
        return false;
    producer->detached = true;
    return true;
}

const CettaOwnedExecution *cetta_shared_return_producer_execution(
    const CettaSharedReturnProducer *producer) {
    return producer && !producer->copy_pending && !producer->detached ? &producer->execution : NULL;
}

const CettaOccurrenceLog *cetta_shared_return_producer_log(
    const CettaSharedReturnProducer *producer) {
    return producer && !producer->copy_pending ? producer->log : NULL;
}

static CettaSharedReturnCheckpoint *checkpoint_allocate(
    CettaSharedReturnProducer *producer, uint64_t position,bool retains_producer) {
    if (!producer || (retains_producer && producer->references == SIZE_MAX) ||
        producer->checkpoints == SIZE_MAX)
        return NULL;
    CettaSharedReturnCheckpoint *checkpoint = malloc(sizeof(*checkpoint));
    if (!checkpoint) return NULL;
    *checkpoint = (CettaSharedReturnCheckpoint){
        .producer = producer, .position = position, .references = 1u,
        .retains_producer=retains_producer,
    };
    return checkpoint;
}

static void checkpoint_adopt(CettaSharedReturnCheckpoint *checkpoint) {
    CettaSharedReturnProducer *producer = checkpoint->producer;
    if (checkpoint->retains_producer) producer->references++;
    producer->checkpoints++;
    checkpoint->next = producer->checkpoint_head;
    if (checkpoint->next) checkpoint->next->previous = checkpoint;
    producer->checkpoint_head = checkpoint;
}

static CettaSharedReturnCheckpoint *checkpoint_new(
    CettaSharedReturnProducer *producer, size_t consumer,bool retains_producer) {
    uint64_t position = 0u, pin = 0u;
    if (!producer || producer->copy_pending ||
        (producer->checkpoint_pin_active && consumer == producer->checkpoint_pin) ||
        !cetta_occurrence_log_position(producer->log, consumer, &position))
        return NULL;
    CettaSharedReturnCheckpoint *checkpoint = checkpoint_allocate(producer, position,retains_producer);
    if (!checkpoint) return NULL;
    bool earlier = !producer->checkpoint_pin_active;
    if (!earlier) {
        if (!cetta_occurrence_log_position(producer->log, producer->checkpoint_pin, &pin)) {
            free(checkpoint); return NULL;
        }
        earlier = position < pin;
    }
    if (earlier) {
        size_t next_pin;
        if (!cetta_occurrence_log_pin(producer->log, position, &next_pin)) {
            free(checkpoint); return NULL;
        }
        if (producer->checkpoint_pin_active)
            (void)cetta_occurrence_log_pin_release(producer->log, producer->checkpoint_pin);
        producer->checkpoint_pin = next_pin;
        producer->checkpoint_pin_active = true;
    }
    checkpoint_adopt(checkpoint);
    return checkpoint;
}

CettaSharedReturnCheckpoint *cetta_shared_return_checkpoint_new(
    CettaSharedReturnProducer *producer,size_t consumer) {
    return checkpoint_new(producer,consumer,true);
}

CettaSharedReturnCheckpoint *cetta_shared_return_checkpoint_new_scoped(
    CettaSharedReturnProducer *producer,size_t consumer) {
    return checkpoint_new(producer,consumer,false);
}

bool cetta_shared_return_checkpoint_retain(CettaSharedReturnCheckpoint *checkpoint) {
    if (!checkpoint || checkpoint->references == SIZE_MAX) return false;
    checkpoint->references++;
    return true;
}

void cetta_shared_return_checkpoint_release(CettaSharedReturnCheckpoint *checkpoint) {
    if (!checkpoint) return;
    assert(checkpoint->references);
    if (--checkpoint->references) return;
    CettaSharedReturnProducer *producer = checkpoint->producer;
    if (!producer) {free(checkpoint);return;}
    bool retained=checkpoint->retains_producer;
    assert(producer->checkpoints);
    if (checkpoint->previous) checkpoint->previous->next = checkpoint->next;
    else producer->checkpoint_head = checkpoint->next;
    if (checkpoint->next) checkpoint->next->previous = checkpoint->previous;
    if (!--producer->checkpoints) {
        if (!producer->copy_pending)
            (void)cetta_occurrence_log_pin_release(producer->log, producer->checkpoint_pin);
        producer->checkpoint_pin_active = false;
    } else if (!producer->copy_pending) {
        uint64_t oldest = UINT64_MAX;
        for (CettaSharedReturnCheckpoint *held=producer->checkpoint_head; held; held=held->next)
            if (held->position < oldest) oldest = held->position;
        bool moved = cetta_occurrence_log_pin_move(producer->log, producer->checkpoint_pin, oldest);
        assert(moved); (void)moved;
    }
    free(checkpoint);
    if (retained) cetta_shared_return_producer_free(producer);
}

bool cetta_shared_return_checkpoint_position(
    const CettaSharedReturnCheckpoint *checkpoint, uint64_t *out_position) {
    if (!checkpoint || !out_position) return false;
    *out_position = checkpoint->position;
    return true;
}

CettaSharedDelivery cetta_shared_return_checkpoint_peek(
    const CettaSharedReturnCheckpoint *checkpoint) {
    CettaSharedDelivery result = {.kind = CETTA_SHARED_DELIVERY_INVALID};
    if (!checkpoint || !checkpoint->producer || checkpoint->producer->copy_pending) return result;
    CettaSharedReturnProducer *producer = checkpoint->producer;
    if (!producer->compatible(producer->context)) {
        result.kind = CETTA_SHARED_DELIVERY_CONTEXT_REFUSED;
        return result;
    }
    CettaOccurrenceLogInfo info;
    if (!producer->checkpoint_pin_active || !cetta_occurrence_log_info(producer->log, &info) ||
        checkpoint->position < info.base || checkpoint->position > info.past_end)
        return result;
    if (checkpoint->position == info.past_end) {
        result.kind = info.completed ? CETTA_SHARED_DELIVERY_COMPLETED : CETTA_SHARED_DELIVERY_PAUSED;
        return result;
    }
    CettaOccurrenceRow row;
    if (!cetta_occurrence_log_row(producer->log,
            (size_t)(checkpoint->position - info.base), &row)) return result;
    result.kind = CETTA_SHARED_DELIVERY_ANSWER;
    result.position = checkpoint->position;
    result.returned = row.value;
    return result;
}

CettaSharedReturnStep cetta_shared_return_checkpoint_advance(
    CettaSharedReturnCheckpoint *checkpoint, uint64_t allowance) {
    if (checkpoint && checkpoint->producer && checkpoint->producer->copy_pending)
        return CETTA_SHARED_RETURN_INVALID;
    if (checkpoint && checkpoint->producer && allowance && !checkpoint->producer->detached) {
        if (!checkpoint->producer->compatible(checkpoint->producer->context))
            return CETTA_SHARED_RETURN_CONTEXT_REFUSED;
        (void)cetta_shared_return_producer_trim(checkpoint->producer);
    }
    return cetta_shared_return_producer_advance(checkpoint ? checkpoint->producer : NULL, allowance);
}

CettaSharedDelivery cetta_shared_return_checkpoint_take(
    const CettaSharedReturnCheckpoint *checkpoint,
    CettaSharedCheckpointPublish publish, void *context,
    CettaSharedReturnCheckpoint **out_next) {
    if (!out_next || *out_next)
        return (CettaSharedDelivery){.kind = CETTA_SHARED_DELIVERY_INVALID};
    CettaSharedDelivery result = cetta_shared_return_checkpoint_peek(checkpoint);
    if (result.kind != CETTA_SHARED_DELIVERY_ANSWER) return result;
    CettaSharedReturnCheckpoint *next = checkpoint_allocate(
        checkpoint->producer, checkpoint->position + 1u,checkpoint->retains_producer);
    if (!next) return (CettaSharedDelivery){.kind = CETTA_SHARED_DELIVERY_PUBLICATION_REFUSED};
    checkpoint_adopt(next);
    /* Publication can replace the caller's context, releasing its old input
     * share. Keep that borrowed input live until the callback returns. */
    if (!cetta_shared_return_checkpoint_retain((CettaSharedReturnCheckpoint *)checkpoint)) {
        cetta_shared_return_checkpoint_release(next);
        return (CettaSharedDelivery){.kind = CETTA_SHARED_DELIVERY_PUBLICATION_REFUSED};
    }
    if (!publish || !publish(context, result.position, result.returned, next)) {
        cetta_shared_return_checkpoint_release(next);
        cetta_shared_return_checkpoint_release((CettaSharedReturnCheckpoint *)checkpoint);
        return (CettaSharedDelivery){.kind = CETTA_SHARED_DELIVERY_PUBLICATION_REFUSED};
    }
    *out_next = next;
    cetta_shared_return_checkpoint_release((CettaSharedReturnCheckpoint *)checkpoint);
    return result;
}

static bool copied_execution_shape(const CettaOwnedExecution *source,
    const CettaOwnedExecution *image) {
    if (!image->computation || image->computation == source->computation ||
        image->next != source->next || image->release != source->release ||
        image->release_poll != source->release_poll ||
        image->final_is_current != source->final_is_current ||
        image->completion != source->completion || image->stop != source->stop ||
        image->accepted != source->accepted || image->has_pending != source->has_pending ||
        image->terminal != source->terminal || image->retiring != source->retiring ||
        image->release_ready != source->release_ready ||
        !image->relocatable || image->relocatable != source->relocatable)
        return false;
    if (!source->has_pending)
        return true;
    return source->pending.final == image->pending.final &&
        source->pending.outcome.kind == image->pending.outcome.kind &&
        source->pending.outcome.result_form == image->pending.outcome.result_form &&
        (source->pending.outcome.term != NULL) == (image->pending.outcome.term != NULL) &&
        (source->pending.outcome.delayed != NULL) == (image->pending.outcome.delayed != NULL) &&
        (source->pending.environment != NULL) == (image->pending.environment != NULL);
}

typedef struct {
    CettaSharedReturnCheckpoint *source, *image;
} CopiedCheckpoint;

struct CettaSharedReturnCopyPlan {
    CettaSharedReturnProducer *source,*image;
    CettaSharedReturnCopy copy;
    CopiedCheckpoint *checkpoints;
    size_t held;
};

void cetta_shared_return_producer_copy_plan_free(
        CettaSharedReturnCopyPlan *plan) {
    if(!plan)return;
    CettaSharedReturnProducer *image=plan->image;
    if(image) {
        cetta_owned_execution_cancel(&image->execution);
        cetta_occurrence_log_free(image->log);
        image->log=NULL;
    }
    for(size_t i=0u;i<plan->held;i++) {
        cetta_shared_return_checkpoint_release(plan->checkpoints[i].image);
        cetta_shared_return_checkpoint_release(plan->checkpoints[i].source);
    }
    free(plan->checkpoints);
    cetta_shared_return_producer_free(plan->source);
    cetta_shared_return_producer_free(image);
    free(plan);
}

CettaSharedReturnCopyPlan *cetta_shared_return_producer_copy_prepare(
        CettaSharedReturnProducer *source,const CettaSharedReturnCopy *copy) {
    if (!source || source->copy_pending || !source->references ||
        source->references == SIZE_MAX || !copy || !copy->producer ||
        !copy->capture || !copy->compatible ||
        !cetta_occurrence_log_valid(source->log) ||
        source->checkpoints > SIZE_MAX / sizeof(CopiedCheckpoint) ||
        source->checkpoint_pin_active != (source->checkpoints != 0u) ||
        (source->checkpoints && !copy->checkpoint) ||
        (!source->detached && (!copy->execution || !source->execution.computation ||
            !source->execution.relocatable || source->execution.retiring)))
        return NULL;
    CettaSharedReturnCopyPlan *plan=calloc(1u,sizeof(*plan));
    if(!plan)return NULL;
    source->references++;
    plan->source=source;plan->copy=*copy;
    CettaSharedReturnProducer *image=calloc(1u,sizeof(*image));
    plan->image=image;
    if(!image)goto fail;
    *image = (CettaSharedReturnProducer){.references = 1u, .copy_pending = true,
        .capture = copy->capture, .compatible = copy->compatible,
        .context = copy->image_context, .refusal = source->refusal,
        .detached = source->detached, .checkpoint_pin = source->checkpoint_pin,
        .checkpoint_pin_active = source->checkpoint_pin_active};
    if (source->checkpoints) {
        plan->checkpoints=calloc(source->checkpoints,sizeof(*plan->checkpoints));
        if(!plan->checkpoints)goto fail;
    }
    CettaSharedReturnCheckpoint *previous = NULL, *checkpoint = source->checkpoint_head;
    uint64_t oldest = UINT64_MAX;
    while (checkpoint && plan->held < source->checkpoints) {
        if (checkpoint->producer != source || checkpoint->previous != previous ||
            !checkpoint->references || !cetta_shared_return_checkpoint_retain(checkpoint))
            goto fail;
        plan->checkpoints[plan->held++].source=checkpoint;
        if (checkpoint->position < oldest)
            oldest = checkpoint->position;
        previous = checkpoint;
        checkpoint = checkpoint->next;
    }
    if(checkpoint||plan->held!=source->checkpoints)goto fail;
    if(plan->held) {
        uint64_t pin;
        if (!cetta_occurrence_log_position(source->log, source->checkpoint_pin, &pin) ||
            pin != oldest)
            goto fail;
    }
    for(size_t i=plan->held;i>0u;i--) {
        CopiedCheckpoint *pair=&plan->checkpoints[i-1u];
        pair->image = checkpoint_allocate(image, pair->source->position,
            pair->source->retains_producer);
        if(!pair->image)goto fail;
        checkpoint_adopt(pair->image);
    }
    if(!copy->producer(copy->context,source,image))goto fail;
    for(size_t i=0u;i<plan->held;i++)
        if(!copy->checkpoint(copy->context,plan->checkpoints[i].source,
                plan->checkpoints[i].image))goto fail;
    return plan;
fail:
    cetta_shared_return_producer_copy_plan_free(plan);
    return NULL;
}

CettaSharedReturnProducer *cetta_shared_return_producer_copy_finish(
        CettaSharedReturnCopyPlan *plan) {
    if(!plan||!plan->source||!plan->image)return NULL;
    CettaSharedReturnProducer *source=plan->source,*image=plan->image;
    const CettaSharedReturnCopy *copy=&plan->copy;
    bool ready=false;
    image->log=cetta_occurrence_log_copy(source->log,copy->row,copy->context);
    if(!image->log)goto done;
    if (!source->detached) {
        CettaOwnedExecution execution = {0};
        if (!copy->execution(copy->context, &source->execution, &execution))
            goto done;
        /* A borrowed source computation is not an independently owned
         * image. In particular, do not cancel the source on this refusal. */
        if (execution.computation == source->execution.computation)
            goto done;
        if (!copied_execution_shape(&source->execution, &execution) ||
            !cetta_owned_execution_move(&image->execution, &execution)) {
            cetta_owned_execution_cancel(&execution);
            goto done;
        }
    } else image->execution = source->execution;
    for(size_t i=0u;i<plan->held;i++)
        if(plan->checkpoints[i].image->references<=1u)
            goto done;
    image->copy_pending = false;
    ready=true;
done:
    if(!ready) {
        cetta_owned_execution_cancel(&image->execution);
        cetta_occurrence_log_free(image->log);
        image->log = NULL;
    }
    for(size_t i=0u;i<plan->held;i++) {
        cetta_shared_return_checkpoint_release(plan->checkpoints[i].image);
        cetta_shared_return_checkpoint_release(plan->checkpoints[i].source);
    }
    free(plan->checkpoints);plan->checkpoints=NULL;plan->held=0u;
    cetta_shared_return_producer_free(source);plan->source=NULL;
    plan->image=NULL;free(plan);
    if(!ready) {cetta_shared_return_producer_free(image);return NULL;}
    return image;
}

CettaSharedReturnProducer *cetta_shared_return_producer_copy(
        CettaSharedReturnProducer *source,const CettaSharedReturnCopy *copy) {
    CettaSharedReturnCopyPlan *plan=
        cetta_shared_return_producer_copy_prepare(source,copy);
    return plan?cetta_shared_return_producer_copy_finish(plan):NULL;
}
