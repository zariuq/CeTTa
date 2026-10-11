#ifndef CETTA_OCCURRENCE_LOG_H
#define CETTA_OCCURRENCE_LOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Delivery from an already qualified producer, with a fixed consumer cohort.
 * Occurrence positions are local to this log. This does not choose a producer,
 * merge independent choices, or validate the producer's source context.
 * Calls are serialized. Publication and release callbacks must not mutate the
 * log. Their work is outside the constant-cost cursor operation. */
typedef struct CettaOccurrenceLog CettaOccurrenceLog;

typedef struct {
    const void *value;
    void *owner;
    void (*release)(void *owner);
} CettaOccurrenceRow;

typedef enum {
    CETTA_OCCURRENCE_ANSWER,
    CETTA_OCCURRENCE_PAUSED,
    CETTA_OCCURRENCE_COMPLETED,
    CETTA_OCCURRENCE_CANCELLED,
    CETTA_OCCURRENCE_INVALID,
    CETTA_OCCURRENCE_PUBLICATION_REFUSED,
} CettaOccurrenceReplyKind;

typedef struct {
    CettaOccurrenceReplyKind kind;
    uint64_t position;
    const void *value;
} CettaOccurrenceReply;

typedef enum {
    CETTA_OCCURRENCE_APPENDED,
    CETTA_OCCURRENCE_BACKPRESSURE,
    CETTA_OCCURRENCE_APPEND_COMPLETED,
    CETTA_OCCURRENCE_CAPACITY,
    CETTA_OCCURRENCE_APPEND_INVALID,
} CettaOccurrenceAdmission;

typedef struct {
    uint64_t base;
    uint64_t past_end;
    size_t rows;
    size_t limit;
    size_t consumers;
    size_t reserved_rows;
    bool completed;
} CettaOccurrenceLogInfo;

/* Every consumer initially waits at position zero. The explicit row limit
 * bounds retained rows, not payload bytes. Zero consumers and zero limits are
 * permitted. NULL reports an unrepresentable or unallocated control store. */
CettaOccurrenceLog *cetta_occurrence_log_new(size_t consumers, size_t limit);
void cetta_occurrence_log_free(CettaOccurrenceLog *log);

/* APPENDED transfers this row's owner to the log. Every refusal leaves that
 * owner with the caller. Each admitted occurrence has its own ownership share,
 * including when multiple occurrences have the same value or owner. */
CettaOccurrenceAdmission cetta_occurrence_log_append(
    CettaOccurrenceLog *log, CettaOccurrenceRow row);

/* A peeked value is borrowed until its row is trimmed or the log is freed. */
CettaOccurrenceReply cetta_occurrence_log_peek(
    const CettaOccurrenceLog *log, size_t consumer);

/* Success means the recipient now protects the complete retained answer graph
 * independently of the log. Only then does take advance this consumer. False
 * leaves its occurrence unread, even if the callback allocated caller storage.
 * Payload publication, allocation failures and finalizers are host obligations. */
typedef bool (*CettaOccurrencePublish)(
    void *context, uint64_t position, const void *value);
CettaOccurrenceReply cetta_occurrence_log_take(
    CettaOccurrenceLog *log, size_t consumer,
    CettaOccurrencePublish publish, void *context);

bool cetta_occurrence_log_cancel(CettaOccurrenceLog *log, size_t consumer);
/* Drop exactly the prefix preceding every active position. A slow consumer
 * pins its unread rows. With no active consumers the entire prefix is dropped.
 * The scan costs O(consumers), plus one release per dropped occurrence. */
size_t cetta_occurrence_log_trim(CettaOccurrenceLog *log);
void cetta_occurrence_log_complete(CettaOccurrenceLog *log);

/* Read-only diagnostics expose the logical residual and allocated row slots. */
bool cetta_occurrence_log_info(
    const CettaOccurrenceLog *log, CettaOccurrenceLogInfo *out);
bool cetta_occurrence_log_position(
    const CettaOccurrenceLog *log, size_t consumer, uint64_t *out);
bool cetta_occurrence_log_row(
    const CettaOccurrenceLog *log, size_t offset, CettaOccurrenceRow *out);
bool cetta_occurrence_log_valid(const CettaOccurrenceLog *log);

#endif
