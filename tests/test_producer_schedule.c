#include "producer_schedule.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t allocation, fail_at, allocated_bytes;
void *__real_calloc(size_t count, size_t width);
void *__wrap_calloc(size_t count, size_t width) {
    allocation++;
    if (allocation == fail_at)
        return NULL;
    void *memory = __real_calloc(count, width);
    if (memory)
        allocated_bytes += count * width;
    return memory;
}

enum { LIMIT = 262145 };
typedef struct {
    bool current[LIMIT], later[LIMIT];
    size_t cursor, size;
} Reference;

/* Independent fixed-order pass interpretation. It uses two arrays and a
 * linear search, with no bitmap, phase parity or summary metadata. */
static void wake(Reference *reference, size_t rank) {
    if (rank < reference->cursor)
        reference->later[rank] = true;
    else
        reference->current[rank] = true;
}

static bool next(Reference *reference, size_t *rank, bool *advanced) {
    *advanced = false;
    for (;;) {
        for (size_t i = reference->cursor; i < reference->size; i++) {
            if (!reference->current[i])
                continue;
            reference->current[i] = false;
            reference->cursor = i + 1u;
            *rank = i;
            return true;
        }
        bool later = false;
        for (size_t i = 0u; i < reference->size; i++)
            later |= reference->later[i];
        if (!later)
            return false;
        memcpy(reference->current, reference->later, sizeof(reference->current));
        memset(reference->later, 0, sizeof(reference->later));
        reference->cursor = 0u;
        *advanced = true;
    }
}

static bool compare_next(CettaProducerSchedule *schedule, Reference *reference) {
    size_t actual = SIZE_MAX, expected = SIZE_MAX;
    bool actual_pass = false, expected_pass = false;
    bool found = next(reference, &expected, &expected_pass);
    assert(cetta_producer_schedule_next(schedule, &actual, &actual_pass) == found);
    if (found) {
        assert(actual == expected && actual_pass == expected_pass);
        assert(schedule->cursor == reference->cursor);
    }
    assert(schedule->length <= schedule->size);
    return found;
}

static void compare_representation(const CettaProducerSchedule *schedule,
                                   const Reference *reference) {
    size_t count = 0u;
    for (size_t rank = 0u; rank < reference->size; rank++) {
        assert(!(reference->current[rank] && reference->later[rank]));
        count += reference->current[rank] + reference->later[rank];
    }
    assert(count == schedule->length);
    if (!schedule->indexed) {
        assert(count <= 1u);
        if (count) {
            size_t rank = schedule->storage.one.rank;
            assert(rank < reference->size);
            assert(schedule->storage.one.phase == schedule->phase ?
                reference->current[rank] : reference->later[rank]);
        }
        return;
    }
    size_t words = (size_t)schedule->offsets[schedule->levels - 1u] + 1u;
    uint64_t *expected = malloc(words * 2u * sizeof(*expected));
    assert(expected);
    memset(expected, 0, words * 2u * sizeof(*expected));
    for (size_t rank = 0u; rank < reference->size; rank++) {
        if (!reference->current[rank] && !reference->later[rank])
            continue;
        bool phase = reference->current[rank] ? schedule->phase : !schedule->phase;
        size_t span = 1u;
        for (size_t level = 0u; level < schedule->levels; level++) {
            size_t node = rank / (64u * span);
            size_t bit = rank / span % 64u;
            expected[2u * ((size_t)schedule->offsets[level] + node) + phase] |=
                UINT64_C(1) << bit;
            span *= 64u;
        }
    }
    const uint64_t *actual = schedule->levels == 1u ? schedule->storage.single : schedule->words;
    for (size_t index = 0u; index < 2u * words; index++)
        assert(actual[index] == expected[index]);
    free(expected);
}

static void run(size_t size) {
    CettaProducerSchedule schedule;
    Reference reference = {.size = size};
    size_t payload_before = allocated_bytes;
    assert(cetta_producer_schedule_init(&schedule, size));
    assert(!cetta_producer_schedule_wake(&schedule, size));
    assert(!cetta_producer_schedule_next(&schedule, NULL, NULL));

    uint32_t random = 731u;
    for (size_t event = 0u; event < 20000u; event++) {
        if (event < 16u || event % 257u == 0u)
            compare_representation(&schedule, &reference);
        random = random * UINT32_C(1664525) + UINT32_C(1013904223);
        if (size && (event % 7u < 4u)) {
            size_t rank = (random >> 8u) % size;
            assert(cetta_producer_schedule_wake(&schedule, rank));
            wake(&reference, rank);
            assert(cetta_producer_schedule_wake(&schedule, rank));
            wake(&reference, rank);
        } else {
            compare_next(&schedule, &reference);
        }
    }
    while (compare_next(&schedule, &reference)) { }

    assert(cetta_producer_schedule_bytes(&schedule) == allocated_bytes - payload_before);
    cetta_producer_schedule_free(&schedule);
    assert(!schedule.length && !schedule.size && !schedule.cursor &&
        cetta_producer_schedule_bytes(&schedule) == 0u);
}

int main(void) {
    CettaProducerSchedule schedule;
    allocation = 0u;
    fail_at = 1u;
    assert(cetta_producer_schedule_init(&schedule, 4096u));
    assert(cetta_producer_schedule_wake(&schedule, 3u));
    assert(!cetta_producer_schedule_wake(&schedule, 4095u));
    assert(schedule.length == 1u && cetta_producer_schedule_bytes(&schedule) == 0u);
    fail_at = 0u;
    assert(cetta_producer_schedule_wake(&schedule, 4095u));
    Reference promoted = {.size = 4096u};
    wake(&promoted, 3u);
    wake(&promoted, 4095u);
    compare_representation(&schedule, &promoted);
    size_t kept = SIZE_MAX;
    assert(cetta_producer_schedule_next(&schedule, &kept, NULL) && kept == 3u);
    assert(cetta_producer_schedule_next(&schedule, &kept, NULL) && kept == 4095u);
    assert(schedule.cursor == 4096u);
    size_t allocations = allocation;
    assert(cetta_producer_schedule_wake(&schedule, 3u));
    assert(cetta_producer_schedule_wake(&schedule, 4095u));
    assert(allocation == allocations);
    assert(cetta_producer_schedule_next(&schedule, &kept, NULL) && kept == 3u);
    assert(cetta_producer_schedule_next(&schedule, &kept, NULL) && kept == 4095u);
    cetta_producer_schedule_free(&schedule);
    fail_at = 0u;
    CettaProducerSchedule duplicate;
    assert(cetta_producer_schedule_init(&duplicate, 17u));
    assert(cetta_producer_schedule_wake(&duplicate, 0u));
    assert(cetta_producer_schedule_wake(&duplicate, 0u));
    assert(duplicate.length == 1u);
    cetta_producer_schedule_free(&duplicate);
    CettaProducerSchedule refused;
    assert(!cetta_producer_schedule_init(&refused, UINT32_MAX));
    cetta_producer_schedule_free(&refused);
    const size_t sizes[] = {0u, 1u, 2u, 17u, 63u, 64u, 65u, 256u, 4095u, 4096u, 4097u, 262144u, LIMIT};
    for (size_t i = 0u; i < sizeof(sizes) / sizeof(*sizes); i++)
        run(sizes[i]);
    puts("PASS: ordered producer queue matches independent current/next passes across 260000 mixed events");
    puts("PASS: duplicate wakes coalesce; visited readers wait; passes advance without allocation");
    puts("PASS: promotion failures, representation refusal and complete release preserve ownership");
    return 0;
}
