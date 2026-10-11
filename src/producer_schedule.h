#ifndef CETTA_PRODUCER_SCHEDULE_H
#define CETTA_PRODUCER_SCHEDULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Fixed-order producer passes. A reader already visited in the current pass
 * waits for the next pass; duplicate wakes coalesce. One pending reader uses
 * an inline rank. Wider frontiers use two hierarchical bitmaps, retaining
 * their allocated workspace until free. The caller owns this state.
 *
 * Wake may fail while promoting to a bitmap. Previously pending work remains
 * intact, but the caller must then use its ordinary execution route. */
typedef struct {
    union {
        uint64_t single[2];
        struct { size_t rank; bool phase; } one;
    } storage;
    uint64_t *words;
    size_t size, length, cursor;
    uint32_t offsets[6];
    unsigned char levels;
    bool phase;
    bool indexed;
} CettaProducerSchedule;

bool cetta_producer_schedule_init(CettaProducerSchedule *schedule, size_t size);
void cetta_producer_schedule_free(CettaProducerSchedule *schedule);
bool cetta_producer_schedule_wake(CettaProducerSchedule *schedule, size_t rank);
bool cetta_producer_schedule_next(CettaProducerSchedule *schedule,
    size_t *rank, bool *next_pass);
/* Owned bitmap payload; the enclosing owner accounts for the state header. */
size_t cetta_producer_schedule_bytes(const CettaProducerSchedule *schedule);

#endif
