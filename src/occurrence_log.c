#include "occurrence_log.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    uint64_t position;
    bool active;
    bool pin;
} CettaOccurrenceConsumer;

struct CettaOccurrenceLog {
    CettaOccurrenceRow *rows;
    CettaOccurrenceConsumer *positions;
    uint64_t base;
    size_t head;
    size_t length;
    size_t capacity;
    size_t limit;
    size_t consumers;
    size_t consumer_capacity;
    bool completed;
};

static size_t row_index(const CettaOccurrenceLog *log, size_t offset) {
    size_t index = log->head + offset;
    return index < log->capacity ? index : index - log->capacity;
}

void cetta_occurrence_log_storage(const CettaOccurrenceLog *log,
    CettaStorageCensus *census,CettaStorageVisitor row_storage) {
    if(!log || !cetta_storage_object(census,log,sizeof(*log),CETTA_STORAGE_BLOCK)) return;
    cetta_storage_array(census,log->rows,log->capacity,sizeof(*log->rows),0u);
    cetta_storage_array(census,log->positions,log->consumer_capacity,sizeof(*log->positions),0u);
    if(log->length>log->capacity || log->consumers>log->consumer_capacity ||
        (log->length && (!log->rows || log->head>=log->capacity)))
        {cetta_storage_refuse(census,"OccurrenceLogExtent");return;}
    for(size_t i=0u;i<log->length;i++) {
        const CettaOccurrenceRow *row=&log->rows[row_index(log,i)];
        if(!row->owner) continue;
        if(row_storage) cetta_storage_schedule(census,row->owner,row_storage);
        else cetta_storage_refuse(census,"OpaqueOccurrenceRowOwner");
    }
}

static bool reserve_row(CettaOccurrenceLog *log) {
    if (log->length < log->capacity)
        return true;
    size_t next = log->capacity;
    if (!next)
        next = log->limit < 8u ? log->limit : 8u;
    else
        next = next > log->limit / 2u ? log->limit : next * 2u;
    if (next <= log->length || next > SIZE_MAX / sizeof(*log->rows))
        return false;
    CettaOccurrenceRow *rows = malloc(next * sizeof(*rows));
    if (!rows)
        return false;
    for (size_t i = 0u; i < log->length; ++i)
        rows[i] = log->rows[row_index(log, i)];
    free(log->rows);
    log->rows = rows;
    log->head = 0u;
    log->capacity = next;
    return true;
}

CettaOccurrenceLog *cetta_occurrence_log_new(size_t consumers, size_t limit) {
    if (consumers > SIZE_MAX / sizeof(CettaOccurrenceConsumer))
        return NULL;
    CettaOccurrenceLog *log = calloc(1u, sizeof(*log));
    if (!log)
        return NULL;
    if (consumers) {
        log->positions = calloc(consumers, sizeof(*log->positions));
        if (!log->positions) {
            free(log);
            return NULL;
        }
        for (size_t i = 0u; i < consumers; ++i)
            log->positions[i].active = true;
    }
    log->consumers = consumers;
    log->consumer_capacity = consumers;
    log->limit = limit;
    return log;
}

static bool reserve_consumer(CettaOccurrenceLog *log) {
    if (log->consumers == log->consumer_capacity) {
        size_t maximum = SIZE_MAX / sizeof(*log->positions);
        size_t next = !log->consumer_capacity ? 8u : log->consumer_capacity > maximum / 2u
            ? maximum : log->consumer_capacity * 2u;
        if (next <= log->consumers) return false;
        CettaOccurrenceConsumer *positions = malloc(next * sizeof(*positions));
        if (!positions) return false;
        for (size_t i = 0u; i < log->consumers; i++) positions[i] = log->positions[i];
        free(log->positions);
        log->positions = positions;
        log->consumer_capacity = next;
    }
    return true;
}

bool cetta_occurrence_log_fork(CettaOccurrenceLog *log, size_t parent,
                             size_t *out_consumer) {
    if (!log || !out_consumer || parent >= log->consumers ||
        !log->positions[parent].active || log->positions[parent].pin ||
        log->positions[parent].position < log->base ||
        log->positions[parent].position - log->base > log->length || !reserve_consumer(log))
        return false;
    size_t child = log->consumers;
    log->positions[child] = log->positions[parent];
    log->consumers++;
    *out_consumer = child;
    return true;
}

bool cetta_occurrence_log_pin(CettaOccurrenceLog *log, uint64_t position, size_t *out_pin) {
    if (!log || !out_pin || position < log->base || position - log->base > log->length ||
        !reserve_consumer(log)) return false;
    size_t index = log->consumers++;
    log->positions[index] = (CettaOccurrenceConsumer){.position=position,.active=true,.pin=true};
    *out_pin = index;
    return true;
}

bool cetta_occurrence_log_pin_move(CettaOccurrenceLog *log, size_t pin, uint64_t position) {
    if (!log || pin >= log->consumers || !log->positions[pin].active || !log->positions[pin].pin ||
        position < log->positions[pin].position || position < log->base ||
        position - log->base > log->length) return false;
    log->positions[pin].position = position;
    return true;
}

bool cetta_occurrence_log_pin_release(CettaOccurrenceLog *log, size_t pin) {
    if (!log || pin >= log->consumers || !log->positions[pin].active || !log->positions[pin].pin)
        return false;
    log->positions[pin].active = false;
    return true;
}

static void release_row(CettaOccurrenceRow row) {
    if (row.release)
        row.release(row.owner);
}

void cetta_occurrence_log_free(CettaOccurrenceLog *log) {
    if (!log)
        return;
    log->completed = true;
    for (size_t i = 0u; i < log->consumers; ++i)
        log->positions[i].active = false;
    (void)cetta_occurrence_log_trim(log);
    free(log->rows);
    free(log->positions);
    free(log);
}

CettaOccurrenceLog *cetta_occurrence_log_copy(const CettaOccurrenceLog *source,
    CettaOccurrenceCopyRow copy_row, void *context) {
    if (!cetta_occurrence_log_valid(source) || (source->length && !copy_row))
        return NULL;
    CettaOccurrenceLog *image = calloc(1u, sizeof(*image));
    if (!image)
        return NULL;
    image->base = source->base;
    image->head = source->head;
    image->capacity = source->capacity;
    image->limit = source->limit;
    image->consumers = source->consumers;
    image->consumer_capacity = source->consumer_capacity;
    image->completed = source->completed;
    if (image->capacity) {
        image->rows = malloc(image->capacity * sizeof(*image->rows));
        if (!image->rows)
            goto fail;
    }
    if (image->consumer_capacity) {
        image->positions = malloc(image->consumer_capacity * sizeof(*image->positions));
        if (!image->positions)
            goto fail;
        memcpy(image->positions, source->positions,
            image->consumers * sizeof(*image->positions));
    }
    for (size_t i = 0u; i < source->length; i++) {
        CettaOccurrenceRow copied = {0};
        if (!copy_row(context, source->base + (uint64_t)i,
                &source->rows[row_index(source, i)], &copied))
            goto fail;
        image->rows[row_index(image, i)] = copied;
        image->length++;
    }
    return image;
fail:
    /* Consumers may not have been allocated yet. Only the accepted row
     * prefix owns payload shares; no source pin or callback is touched. */
    for (size_t i = 0u; i < image->length; i++)
        release_row(image->rows[row_index(image, i)]);
    free(image->rows);
    free(image->positions);
    free(image);
    return NULL;
}

CettaOccurrenceAdmission cetta_occurrence_log_append(
    CettaOccurrenceLog *log, CettaOccurrenceRow row) {
    if (!log)
        return CETTA_OCCURRENCE_APPEND_INVALID;
    if (log->completed)
        return CETTA_OCCURRENCE_APPEND_COMPLETED;
    if (log->length == log->limit)
        return CETTA_OCCURRENCE_BACKPRESSURE;
    if (log->length == UINT64_MAX - log->base || !reserve_row(log))
        return CETTA_OCCURRENCE_CAPACITY;
    log->rows[row_index(log, log->length)] = row;
    ++log->length;
    return CETTA_OCCURRENCE_APPENDED;
}

CettaOccurrenceReply cetta_occurrence_log_peek(
    const CettaOccurrenceLog *log, size_t consumer) {
    CettaOccurrenceReply result = {.kind = CETTA_OCCURRENCE_INVALID};
    if (!log || consumer >= log->consumers)
        return result;
    CettaOccurrenceConsumer cursor = log->positions[consumer];
    if (cursor.pin) return result;
    if (!cursor.active) {
        result.kind = CETTA_OCCURRENCE_CANCELLED;
        return result;
    }
    if (cursor.position < log->base)
        return result;
    uint64_t offset = cursor.position - log->base;
    if (offset < log->length) {
        result.kind = CETTA_OCCURRENCE_ANSWER;
        result.position = cursor.position;
        result.value = log->rows[row_index(log, (size_t)offset)].value;
    } else if (offset == log->length) {
        result.kind = log->completed
            ? CETTA_OCCURRENCE_COMPLETED : CETTA_OCCURRENCE_PAUSED;
    }
    return result;
}

CettaOccurrenceReply cetta_occurrence_log_take(
    CettaOccurrenceLog *log, size_t consumer,
    CettaOccurrencePublish publish, void *context) {
    CettaOccurrenceReply result = cetta_occurrence_log_peek(log, consumer);
    if (result.kind == CETTA_OCCURRENCE_ANSWER) {
        if (!publish || !publish(context, result.position, result.value)) {
            result.kind = CETTA_OCCURRENCE_PUBLICATION_REFUSED;
            result.value = NULL;
            result.position = 0u;
            return result;
        }
        ++log->positions[consumer].position;
    }
    return result;
}

bool cetta_occurrence_log_cancel(CettaOccurrenceLog *log, size_t consumer) {
    if (!log || consumer >= log->consumers || log->positions[consumer].pin)
        return false;
    log->positions[consumer].active = false;
    return true;
}

size_t cetta_occurrence_log_trim(CettaOccurrenceLog *log) {
    if (!log)
        return 0u;
    uint64_t cutoff = log->base + (uint64_t)log->length;
    for (size_t i = 0u; i < log->consumers; ++i) {
        if (log->positions[i].active && log->positions[i].position < cutoff)
            cutoff = log->positions[i].position;
    }
    size_t count = (size_t)(cutoff - log->base);
    if (count) {
        size_t old_head = log->head;
        log->head = row_index(log, count);
        log->base = cutoff;
        log->length -= count;
        /* Expose the retained residual before invoking host release code. */
        for (size_t i = 0u; i < count; ++i) {
            size_t index = old_head + i;
            if (index >= log->capacity)
                index -= log->capacity;
            CettaOccurrenceRow released = log->rows[index];
            log->rows[index] = (CettaOccurrenceRow){0};
            release_row(released);
        }
    }
    return count;
}

void cetta_occurrence_log_complete(CettaOccurrenceLog *log) {
    if (log)
        log->completed = true;
}

bool cetta_occurrence_log_info(
    const CettaOccurrenceLog *log, CettaOccurrenceLogInfo *out) {
    if (!log || !out)
        return false;
    *out = (CettaOccurrenceLogInfo){
        .base = log->base, .past_end = log->base + (uint64_t)log->length,
        .rows = log->length, .limit = log->limit, .consumers = log->consumers,
        .reserved_rows = log->capacity, .completed = log->completed,
    };
    return true;
}

bool cetta_occurrence_log_position(
    const CettaOccurrenceLog *log, size_t consumer, uint64_t *out) {
    if (!log || !out || consumer >= log->consumers || !log->positions[consumer].active)
        return false;
    *out = log->positions[consumer].position;
    return true;
}

bool cetta_occurrence_log_row(
    const CettaOccurrenceLog *log, size_t offset, CettaOccurrenceRow *out) {
    if (!log || !out || offset >= log->length)
        return false;
    *out = log->rows[row_index(log, offset)];
    return true;
}

bool cetta_occurrence_log_valid(const CettaOccurrenceLog *log) {
    if (!log || log->length > log->limit || log->length > log->capacity ||
        log->length > UINT64_MAX - log->base ||
        log->capacity > SIZE_MAX / sizeof(*log->rows) ||
        (log->capacity && (!log->rows || log->head >= log->capacity)) ||
        log->consumers > log->consumer_capacity ||
        log->consumer_capacity > SIZE_MAX / sizeof(*log->positions) ||
        (log->consumer_capacity && !log->positions))
        return false;
    uint64_t end = log->base + (uint64_t)log->length;
    for (size_t i = 0u; i < log->consumers; ++i) {
        if (log->positions[i].active &&
            (log->positions[i].position < log->base || log->positions[i].position > end))
            return false;
    }
    return true;
}
