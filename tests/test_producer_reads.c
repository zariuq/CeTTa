#include "producer_reads.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static size_t allocation, fail_at;
void *__real_calloc(size_t count, size_t width);
void *__wrap_calloc(size_t count, size_t width) {
    allocation++;
    return fail_at && allocation == fail_at ? NULL : __real_calloc(count, width);
}

static bool revision(const void *context, size_t producer, size_t *out) {
    const size_t *values = context;
    if (producer >= 512u)
        return false;
    *out = values[producer];
    return true;
}

static void test_inline_reads(void) {
    size_t values[512] = {0};
    CettaProducerReads reads;
    cetta_producer_reads_init(&reads);
    cetta_producer_reads_begin(&reads);
    size_t before = allocation;
    for (size_t i = 0u; i < 100u; i++)
        cetta_producer_reads_observe(&reads, 0u, 0u);
    assert(allocation == before && reads.length == 1u);
    assert(cetta_producer_reads_bytes(&reads) == 0u);
    assert(cetta_producer_reads_valid(&reads, revision, values));
    values[0] = 1u;
    assert(!cetta_producer_reads_valid(&reads, revision, values));
    cetta_producer_reads_observe(&reads, 0u, 1u);
    assert(!reads.coherent);
    values[0] = 0u;
    assert(!cetta_producer_reads_valid(&reads, revision, values));
    cetta_producer_reads_begin(&reads);
    cetta_producer_reads_observe(&reads, 512u, 0u);
    assert(!cetta_producer_reads_valid(&reads, revision, values));
    cetta_producer_reads_begin(&reads);
    assert(cetta_producer_reads_valid(&reads, revision, values));
    cetta_producer_reads_observe(&reads, 0u, 0u);
    values[1] = 7u;
    cetta_producer_reads_observe(&reads, 1u, 7u);
    assert(reads.length == 2u && allocation == before + 1u);
    assert(cetta_producer_reads_valid(&reads, revision, values));
    size_t bytes = cetta_producer_reads_bytes(&reads);
    assert(bytes > 0u);
    values[0] = 1u;
    assert(!cetta_producer_reads_valid(&reads, revision, values));
    values[0] = 0u;
    assert(cetta_producer_reads_valid(&reads, revision, values));
    cetta_producer_reads_begin(&reads);
    assert(reads.length == 0u && cetta_producer_reads_bytes(&reads) == bytes);
    assert(cetta_producer_reads_valid(&reads, revision, values));
    cetta_producer_reads_free(&reads);
    assert(cetta_producer_reads_bytes(&reads) == 0u && !reads.coherent);

    cetta_producer_reads_begin(&reads);
    allocation = 0u;
    fail_at = 1u;
    cetta_producer_reads_observe(&reads, 0u, 0u);
    assert(reads.coherent && allocation == 0u);
    cetta_producer_reads_observe(&reads, 1u, 7u);
    assert(!reads.coherent && reads.length == 1u);
    assert(cetta_producer_reads_bytes(&reads) == 0u);
    assert(!cetta_producer_reads_valid(&reads, revision, values));
    fail_at = 0u;
    cetta_producer_reads_begin(&reads);
    cetta_producer_reads_observe(&reads, 0u, 0u);
    cetta_producer_reads_observe(&reads, 1u, 7u);
    assert(cetta_producer_reads_valid(&reads, revision, values));
    cetta_producer_reads_free(&reads);
    puts("PASS: inline reads preserve empty and conflicting observations, spill contents, failure fallback and retirement");
}

static void test_trace_reference(void) {
    size_t values[512] = {0}, first[512];
    bool seen[512];
    CettaProducerReads reads;
    cetta_producer_reads_init(&reads);
    uint32_t random = 17u;
    for (size_t round = 0u; round < 100u; round++) {
        for (size_t key = 0u; key < 512u; key++) {
            seen[key] = false;
            values[key] = key % 7u;
        }
        bool coherent = true;
        cetta_producer_reads_begin(&reads);
        for (size_t event = 0u; event < 127u; event++) {
            random = random * UINT32_C(1664525) + UINT32_C(1013904223);
            size_t key = (random >> 8u) % (round % 33u + 1u);
            if (coherent) {
                if (seen[key])
                    coherent = first[key] == values[key];
                else {
                    seen[key] = true;
                    first[key] = values[key];
                }
            }
            cetta_producer_reads_observe(&reads, key, values[key]);
            if (event % 13u == 0u)
                values[key]++;
            bool accepted = coherent;
            for (size_t check = 0u; check < 512u; check++)
                if (seen[check] && values[check] != first[check])
                    accepted = false;
            assert(cetta_producer_reads_valid(&reads, revision, values) == accepted);
        }
    }
    cetta_producer_reads_free(&reads);
    puts("PASS: inline and hashed certificates match independent first-read traces across 12700 read and mutation events");
}

typedef struct {
    bool seen[512];
    size_t length;
    bool cancel;
} Visit;

static bool visit(void *context, size_t producer, size_t value) {
    Visit *capture = context;
    assert(producer < 512u && !capture->seen[producer]);
    assert(value == producer % 7u);
    capture->seen[producer] = true;
    capture->length++;
    return !capture->cancel;
}

static void test_read_visits(void) {
    CettaProducerReads reads;
    cetta_producer_reads_init(&reads);
    Visit capture = {0};
    assert(!cetta_producer_reads_visit(NULL, visit, &capture));
    assert(!cetta_producer_reads_visit(&reads, visit, &capture));
    cetta_producer_reads_begin(&reads);
    assert(!cetta_producer_reads_visit(&reads, NULL, &capture));
    assert(cetta_producer_reads_visit(&reads, visit, &capture) && capture.length == 0u);
    cetta_producer_reads_observe(&reads, 7u, 0u);
    cetta_producer_reads_observe(&reads, 7u, 0u);
    assert(cetta_producer_reads_visit(&reads, visit, &capture) && capture.length == 1u);
    cetta_producer_reads_observe(&reads, 7u, 1u);
    assert(!cetta_producer_reads_visit(&reads, visit, &capture) && capture.length == 1u);
    cetta_producer_reads_begin(&reads);
    capture = (Visit){0};
    for (size_t i = 0u; i < 512u; i++)
        cetta_producer_reads_observe(&reads, i, i % 7u);
    assert(cetta_producer_reads_visit(&reads, visit, &capture) && capture.length == 512u);
    for (size_t i = 0u; i < 512u; i++)
        assert(capture.seen[i]);
    capture = (Visit){.cancel = true};
    assert(!cetta_producer_reads_visit(&reads, visit, &capture) && capture.length == 1u);
    cetta_producer_reads_free(&reads);
    puts("PASS: captured-read visitors preserve inline and hash contents, duplicates, empty reads and cancellation");
}

int main(void) {
    test_inline_reads();
    test_trace_reference();
    size_t values[512] = {0};
    CettaProducerReads reads;
    cetta_producer_reads_init(&reads);
    assert(!cetta_producer_reads_valid(&reads, revision, values));
    cetta_producer_reads_begin(&reads);
    assert(cetta_producer_reads_valid(&reads, revision, values));
    for (size_t i = 0u; i < 512u; i++) {
        values[i] = i % 7u;
        cetta_producer_reads_observe(&reads, i, values[i]);
        cetta_producer_reads_observe(&reads, i, values[i]);
    }
    assert(reads.length == 512u);
    assert(cetta_producer_reads_valid(&reads, revision, values));
    size_t bytes = cetta_producer_reads_bytes(&reads);
    assert(bytes > 0u && reads.hashed && reads.storage.hash.capacity == 1024u);
    values[511]++;
    assert(!cetta_producer_reads_valid(&reads, revision, values));
    values[511]--;
    assert(cetta_producer_reads_valid(&reads, revision, values));
    cetta_producer_reads_observe(&reads, 511u, values[511] + 1u);
    assert(!reads.coherent);
    /* Returning to the old revision cannot repair a mixed observation. */
    assert(!cetta_producer_reads_valid(&reads, revision, values));
    cetta_producer_reads_begin(&reads);
    cetta_producer_reads_observe(&reads, 1u, 0u);
    values[1] = 0u;
    assert(cetta_producer_reads_valid(&reads, revision, values));
    values[1] = 1u;
    assert(!cetta_producer_reads_valid(&reads, revision, values));
    values[511]++;
    values[1] = 0u;
    assert(cetta_producer_reads_valid(&reads, revision, values));
    assert(cetta_producer_reads_bytes(&reads) == bytes);
    cetta_producer_reads_observe(&reads, SIZE_MAX - 1u, 0u);
    assert(!cetta_producer_reads_valid(&reads, revision, values));
    cetta_producer_reads_observe(&reads, SIZE_MAX, 0u);
    assert(!reads.coherent);
    cetta_producer_reads_free(&reads);
    assert(cetta_producer_reads_bytes(&reads) == 0u && !reads.coherent);
    puts("PASS: producer revisions retain first reads, negative dependencies and unrelated-change isolation");

    for (size_t failure = 1u; failure <= 7u; failure++) {
        cetta_producer_reads_init(&reads);
        cetta_producer_reads_begin(&reads);
        allocation = 0u;
        fail_at = failure;
        for (size_t i = 0u; i < 512u; i++)
            cetta_producer_reads_observe(&reads, i, values[i]);
        assert(!reads.coherent);
        assert(!cetta_producer_reads_valid(&reads, revision, values));
        fail_at = 0u;
        cetta_producer_reads_begin(&reads);
        for (size_t i = 0u; i < 512u; i++)
            cetta_producer_reads_observe(&reads, i, values[i]);
        assert(cetta_producer_reads_valid(&reads, revision, values));
        cetta_producer_reads_free(&reads);
    }
    puts("PASS: seven producer-read allocation failures select ordinary rounds and permit fresh capture");
    test_read_visits();
    return 0;
}
