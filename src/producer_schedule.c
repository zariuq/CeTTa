#include "producer_schedule.h"

#include <stdlib.h>

void cetta_producer_schedule_free(CettaProducerSchedule *schedule) {
    free(schedule->words);
    *schedule = (CettaProducerSchedule){0};
}

bool cetta_producer_schedule_init(CettaProducerSchedule *schedule, size_t size) {
    *schedule = (CettaProducerSchedule){0};
    if (!size)
        return true;
    if (size >= UINT32_MAX)
        return false;
    schedule->size = size;
    return true;
}

static void mark(CettaProducerSchedule *schedule, size_t rank, bool phase);

/* Each summary bit names a nonempty child word. A 32-bit rank needs at most
 * six base-64 levels. Zero words left after draining can be reused directly. */
static bool promote(CettaProducerSchedule *schedule) {
    if (!schedule->levels) {
        size_t words = schedule->size / 64u + (schedule->size % 64u != 0u);
        size_t total = 0u;
        do {
            schedule->offsets[schedule->levels++] = (uint32_t)total;
            total += words;
            if (words == 1u)
                break;
            words = words / 64u + (words % 64u != 0u);
        } while (true);
    }
    if (schedule->levels > 1u && !schedule->words) {
        size_t total = (size_t)schedule->offsets[schedule->levels - 1u] + 1u;
        if (total > SIZE_MAX / (2u * sizeof(uint64_t)))
            return false;
        uint64_t *allocated = calloc(total, 2u * sizeof(uint64_t));
        if (!allocated)
            return false;
        schedule->words = allocated;
    }
    size_t prior = schedule->storage.one.rank;
    bool phase = schedule->storage.one.phase;
    if (schedule->levels == 1u) {
        schedule->storage.single[0] = 0u;
        schedule->storage.single[1] = 0u;
    }
    schedule->indexed = true;
    mark(schedule, prior, phase);
    return true;
}

static uint64_t *word(CettaProducerSchedule *schedule, size_t level,
                      size_t node, bool phase) {
    if (schedule->levels == 1u)
        return schedule->storage.single + phase;
    return schedule->words + 2u * ((size_t)schedule->offsets[level] + node) + phase;
}

static void mark(CettaProducerSchedule *schedule, size_t rank, bool phase) {
    for (size_t level = 0u; level < schedule->levels; level++) {
        uint64_t *bits = word(schedule, level, rank / 64u, phase);
        uint64_t old = *bits;
        *bits |= UINT64_C(1) << (rank % 64u);
        if (old)
            return;
        rank /= 64u;
    }
}

static void unmark(CettaProducerSchedule *schedule, size_t rank, bool phase) {
    for (size_t level = 0u; level < schedule->levels; level++) {
        uint64_t *bits = word(schedule, level, rank / 64u, phase);
        *bits &= ~(UINT64_C(1) << (rank % 64u));
        if (*bits)
            return;
        rank /= 64u;
    }
}

bool cetta_producer_schedule_wake(CettaProducerSchedule *schedule, size_t rank) {
    if (rank >= schedule->size)
        return false;
    bool phase = rank < schedule->cursor ? !schedule->phase : schedule->phase;
    if (!schedule->indexed) {
        if (!schedule->length) {
            schedule->storage.one.rank = rank;
            schedule->storage.one.phase = phase;
            schedule->length = 1u;
            return true;
        }
        if (schedule->storage.one.rank == rank)
            return true;
        if (!promote(schedule))
            return false;
    }
    if (*word(schedule, 0u, rank / 64u, phase) & (UINT64_C(1) << (rank % 64u)))
        return true;
    mark(schedule, rank, phase);
    schedule->length++;
    return true;
}

bool cetta_producer_schedule_next(CettaProducerSchedule *schedule,
    size_t *rank, bool *next_pass) {
    if (!schedule->length || !rank)
        return false;
    size_t first;
    bool advance;
    if (!schedule->indexed) {
        first = schedule->storage.one.rank;
        advance = schedule->storage.one.phase != schedule->phase;
        schedule->phase = schedule->storage.one.phase;
        schedule->length = 0u;
    } else {
        advance = !*word(schedule, schedule->levels - 1u, 0u, schedule->phase);
        if (advance)
            schedule->phase = !schedule->phase;
        first = 0u;
        for (size_t level = schedule->levels; level-- > 0u;) {
            uint64_t bits = *word(schedule, level, first, schedule->phase);
            first = first * 64u + (unsigned)__builtin_ctzll(bits);
        }
        unmark(schedule, first, schedule->phase);
        schedule->length--;
        if (!schedule->length)
            schedule->indexed = false;
    }
    schedule->cursor = first + 1u;
    *rank = first;
    if (next_pass)
        *next_pass = advance;
    return true;
}

size_t cetta_producer_schedule_bytes(const CettaProducerSchedule *schedule) {
    if (!schedule->words)
        return 0u;
    return 2u * sizeof(uint64_t) * ((size_t)schedule->offsets[schedule->levels - 1u] + 1u);
}
