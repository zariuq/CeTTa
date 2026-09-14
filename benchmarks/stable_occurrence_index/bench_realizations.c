#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * Cost tournament for three exact realizations of one ordered occurrence
 * family.  The benchmark is deliberately independent of Atom, Space, and any
 * workload name.  It measures the representation seam itself:
 *
 *   dense transport  -- index leaves store current ordered coordinates;
 *   stable identity  -- leaves remain unchanged and are projected through
 *                       the current identity-to-coordinate card;
 *   rebuild          -- every contraction reconstructs all leaf families;
 *   lazy rebuild     -- contraction invalidates the derived view and its
 *                       next observer reconstructs it.
 *
 * The stable-identity realization retains a dense authoritative row array.
 * It therefore claims zero index-leaf rewrites, not constant-time mutation.
 */

typedef uint64_t OccurrenceId;
typedef uint64_t Coordinate;

static const Coordinate COORDINATE_REMOVED = UINT64_MAX;

typedef struct {
    OccurrenceId id;
    uint64_t payload;
} Occurrence;

typedef struct {
    uint64_t *items;
    size_t len;
    size_t cap;
} WordVector;

typedef enum {
    REALIZATION_DENSE_TRANSPORT = 0,
    REALIZATION_STABLE_IDENTITY = 1,
    REALIZATION_REBUILD = 2,
    REALIZATION_LAZY_REBUILD = 3,
    REALIZATION_COUNT = 4,
} RealizationKind;

typedef struct {
    uint64_t source_rows;
    uint64_t row_writes;
    uint64_t mutation_leaf_reads;
    uint64_t mutation_leaf_writes;
    uint64_t query_leaf_reads;
    uint64_t projection_reads;
    uint64_t stale_filtered;
    uint64_t rebuilds;
} Work;

typedef struct {
    RealizationKind kind;
    Occurrence *rows;
    size_t row_len;
    size_t row_cap;
    Coordinate *coordinates;
    size_t coordinate_len;
    size_t coordinate_cap;
    WordVector *leaf_families;
    size_t family_count;
    size_t bucket_count;
    size_t index_count;
    size_t stale_occurrences;
    OccurrenceId last_assigned_id;
    bool has_assigned_id;
    bool leaves_dirty;
    Work work;
    uint64_t mutation_ns;
    uint64_t query_ns;
    uint64_t peak_payload_bytes;
    uint64_t checksum;
} Realization;

typedef enum {
    REMOVE_PREFIX,
    REMOVE_SUFFIX,
    REMOVE_MIDDLE,
    REMOVE_STRIDED,
    REMOVE_SCATTERED,
} RemovalPattern;

typedef struct {
    const char *name;
    size_t live_occurrences;
    size_t bucket_count;
    size_t index_count;
    size_t cycles;
    size_t removals_per_cycle;
    size_t queries_per_cycle;
    size_t query_period;
    uint64_t payload_modulus;
    RemovalPattern removal_pattern;
} Workload;

typedef struct {
    uint64_t mutation_ns;
    uint64_t query_ns;
    uint64_t peak_payload_bytes;
    uint64_t checksum;
    Work work;
} Result;

static volatile uint64_t benchmark_sink = 0u;

static void fail(const char *message) {
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
}

static void *checked_realloc(void *pointer, size_t count, size_t width) {
    if (width != 0u && count > SIZE_MAX / width)
        fail("stable-occurrence tournament: allocation size overflow");
    void *result = realloc(pointer, count * width);
    if (!result && count != 0u)
        fail("stable-occurrence tournament: allocation failed");
    return result;
}

static uint64_t monotonic_ns(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        fail("stable-occurrence tournament: clock_gettime failed");
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) +
           (uint64_t)now.tv_nsec;
}

static uint64_t mix64(uint64_t value) {
    value ^= value >> 30u;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27u;
    value *= UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31u;
    return value;
}

static void vector_reserve(WordVector *vector, size_t needed) {
    if (needed <= vector->cap)
        return;
    size_t capacity = vector->cap ? vector->cap : 4u;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2u)
            fail("stable-occurrence tournament: vector capacity overflow");
        capacity *= 2u;
    }
    vector->items = checked_realloc(
        vector->items, capacity, sizeof(*vector->items));
    vector->cap = capacity;
}

static void vector_push(WordVector *vector, uint64_t value) {
    vector_reserve(vector, vector->len + 1u);
    vector->items[vector->len++] = value;
}

static void vector_free(WordVector *vector) {
    free(vector->items);
    memset(vector, 0, sizeof(*vector));
}

static size_t checked_family_count(size_t bucket_count, size_t index_count) {
    if (bucket_count == 0u || index_count == 0u ||
        bucket_count > SIZE_MAX / index_count) {
        fail("stable-occurrence tournament: invalid family dimensions");
    }
    return bucket_count * index_count;
}

static size_t leaf_family_for(uint64_t payload, size_t index,
                              size_t bucket_count) {
    uint64_t salted = mix64(payload ^
        (UINT64_C(0x9e3779b97f4a7c15) * (uint64_t)(index + 1u)));
    return index * bucket_count + (size_t)(salted % bucket_count);
}

static const char *realization_name(RealizationKind kind) {
    switch (kind) {
    case REALIZATION_DENSE_TRANSPORT:
        return "dense-transport";
    case REALIZATION_STABLE_IDENTITY:
        return "stable-identity";
    case REALIZATION_REBUILD:
        return "rebuild";
    case REALIZATION_LAZY_REBUILD:
        return "lazy-rebuild";
    default:
        return "unknown";
    }
}

static void realization_reserve_rows(Realization *realization, size_t needed) {
    if (needed <= realization->row_cap)
        return;
    size_t capacity = realization->row_cap ? realization->row_cap : 16u;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2u)
            fail("stable-occurrence tournament: row capacity overflow");
        capacity *= 2u;
    }
    realization->rows = checked_realloc(
        realization->rows, capacity, sizeof(*realization->rows));
    realization->row_cap = capacity;
}

static void realization_reserve_coordinates(
        Realization *realization, size_t needed) {
    if (needed <= realization->coordinate_cap)
        return;
    size_t capacity = realization->coordinate_cap
        ? realization->coordinate_cap
        : 16u;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2u)
            fail("stable-occurrence tournament: coordinate capacity overflow");
        capacity *= 2u;
    }
    realization->coordinates = checked_realloc(
        realization->coordinates, capacity,
        sizeof(*realization->coordinates));
    if (realization->kind == REALIZATION_STABLE_IDENTITY) {
        for (size_t i = realization->coordinate_cap; i < capacity; i++)
            realization->coordinates[i] = COORDINATE_REMOVED;
    }
    realization->coordinate_cap = capacity;
}

static uint64_t realization_payload_bytes(const Realization *realization) {
    uint64_t bytes = (uint64_t)realization->row_cap * sizeof(Occurrence) +
        (uint64_t)realization->coordinate_cap * sizeof(Coordinate) +
        (uint64_t)realization->family_count * sizeof(WordVector);
    for (size_t family = 0u; family < realization->family_count; family++) {
        bytes += (uint64_t)realization->leaf_families[family].cap *
            sizeof(uint64_t);
    }
    return bytes;
}

static void realization_note_peak(Realization *realization) {
    uint64_t bytes = realization_payload_bytes(realization);
    if (bytes > realization->peak_payload_bytes)
        realization->peak_payload_bytes = bytes;
}

static void realization_init(Realization *realization, RealizationKind kind,
                             size_t bucket_count, size_t index_count) {
    memset(realization, 0, sizeof(*realization));
    realization->kind = kind;
    realization->bucket_count = bucket_count;
    realization->index_count = index_count;
    realization->family_count = checked_family_count(
        bucket_count, index_count);
    realization->leaf_families = calloc(
        realization->family_count, sizeof(*realization->leaf_families));
    if (!realization->leaf_families)
        fail("stable-occurrence tournament: leaf-family allocation failed");
    realization_note_peak(realization);
}

static void realization_free(Realization *realization) {
    for (size_t family = 0u; family < realization->family_count; family++)
        vector_free(&realization->leaf_families[family]);
    free(realization->leaf_families);
    free(realization->coordinates);
    free(realization->rows);
    memset(realization, 0, sizeof(*realization));
}

static bool realization_identity_is_fresh(
        const Realization *realization, OccurrenceId id) {
    return !realization->has_assigned_id ||
        id > realization->last_assigned_id;
}

static void realization_append(
        Realization *realization, Occurrence occurrence) {
    assert(realization_identity_is_fresh(realization, occurrence.id));
    realization->last_assigned_id = occurrence.id;
    realization->has_assigned_id = true;
    realization_reserve_rows(realization, realization->row_len + 1u);
    Coordinate coordinate = (Coordinate)realization->row_len;
    realization->rows[realization->row_len++] = occurrence;

    if (realization->kind == REALIZATION_STABLE_IDENTITY) {
        if (occurrence.id == UINT64_MAX ||
            occurrence.id > (OccurrenceId)(SIZE_MAX - 1u)) {
            fail("stable-occurrence tournament: identity range exhausted");
        }
        size_t identity_index = (size_t)occurrence.id;
        realization_reserve_coordinates(realization, identity_index + 1u);
        if (identity_index >= realization->coordinate_len)
            realization->coordinate_len = identity_index + 1u;
        assert(realization->coordinates[identity_index] == COORDINATE_REMOVED);
        realization->coordinates[identity_index] = coordinate;
    }

    if (!realization->leaves_dirty) {
        for (size_t index = 0u; index < realization->index_count; index++) {
            size_t family = leaf_family_for(
                occurrence.payload, index, realization->bucket_count);
            vector_push(&realization->leaf_families[family],
                realization->kind == REALIZATION_STABLE_IDENTITY
                    ? occurrence.id
                    : coordinate);
        }
    }
}

static void dense_transport_contract(
        Realization *realization, const uint8_t *remove_mask) {
    size_t source_len = realization->row_len;
    realization_reserve_coordinates(realization, source_len);
    realization->coordinate_len = source_len;
    Coordinate target = 0u;

    for (size_t source = 0u; source < source_len; source++) {
        realization->work.source_rows++;
        if (remove_mask[source]) {
            realization->coordinates[source] = COORDINATE_REMOVED;
            continue;
        }
        realization->coordinates[source] = target;
        if ((Coordinate)source != target) {
            realization->rows[(size_t)target] = realization->rows[source];
            realization->work.row_writes++;
        }
        target++;
    }
    realization->row_len = (size_t)target;

    for (size_t family = 0u; family < realization->family_count; family++) {
        WordVector *leaves = &realization->leaf_families[family];
        size_t write = 0u;
        for (size_t read = 0u; read < leaves->len; read++) {
            Coordinate source = leaves->items[read];
            assert(source < source_len);
            realization->work.mutation_leaf_reads++;
            Coordinate mapped = realization->coordinates[(size_t)source];
            if (mapped == COORDINATE_REMOVED)
                continue;
            leaves->items[write++] = mapped;
            realization->work.mutation_leaf_writes++;
        }
        leaves->len = write;
    }
}

static void stable_identity_prune(Realization *realization) {
    for (size_t family = 0u; family < realization->family_count; family++) {
        WordVector *leaves = &realization->leaf_families[family];
        size_t write = 0u;
        for (size_t read = 0u; read < leaves->len; read++) {
            OccurrenceId id = leaves->items[read];
            assert(id < realization->coordinate_len);
            realization->work.mutation_leaf_reads++;
            if (realization->coordinates[(size_t)id] == COORDINATE_REMOVED)
                continue;
            leaves->items[write++] = id;
            realization->work.mutation_leaf_writes++;
        }
        leaves->len = write;
    }
    realization->stale_occurrences = 0u;
    realization->work.rebuilds++;
}

static void stable_identity_contract(
        Realization *realization, const uint8_t *remove_mask) {
    size_t source_len = realization->row_len;
    size_t target = 0u;
    size_t removed = 0u;
    for (size_t source = 0u; source < source_len; source++) {
        Occurrence occurrence = realization->rows[source];
        assert(occurrence.id < realization->coordinate_len);
        realization->work.source_rows++;
        if (remove_mask[source]) {
            realization->coordinates[(size_t)occurrence.id] =
                COORDINATE_REMOVED;
            removed++;
            continue;
        }
        realization->coordinates[(size_t)occurrence.id] = (Coordinate)target;
        if (source != target) {
            realization->rows[target] = occurrence;
            realization->work.row_writes++;
        }
        target++;
    }
    realization->row_len = target;

    if (removed > SIZE_MAX - realization->stale_occurrences)
        fail("stable-occurrence tournament: stale count overflow");
    realization->stale_occurrences += removed;
    if (realization->stale_occurrences >= realization->row_len &&
        realization->stale_occurrences != 0u) {
        stable_identity_prune(realization);
    }
}

static void rebuild_leaf_families(Realization *realization) {
    for (size_t family = 0u; family < realization->family_count; family++)
        realization->leaf_families[family].len = 0u;
    for (size_t coordinate = 0u; coordinate < realization->row_len;
         coordinate++) {
        Occurrence occurrence = realization->rows[coordinate];
        for (size_t index = 0u; index < realization->index_count; index++) {
            size_t family = leaf_family_for(
                occurrence.payload, index, realization->bucket_count);
            realization->work.mutation_leaf_reads++;
            vector_push(&realization->leaf_families[family], coordinate);
            realization->work.mutation_leaf_writes++;
        }
    }
    realization->work.rebuilds++;
    realization->leaves_dirty = false;
}

static void rebuild_contract(
        Realization *realization, const uint8_t *remove_mask) {
    size_t source_len = realization->row_len;
    size_t target = 0u;
    for (size_t source = 0u; source < source_len; source++) {
        realization->work.source_rows++;
        if (remove_mask[source])
            continue;
        if (source != target) {
            realization->rows[target] = realization->rows[source];
            realization->work.row_writes++;
        }
        target++;
    }
    realization->row_len = target;
    rebuild_leaf_families(realization);
}

static void lazy_rebuild_contract(
        Realization *realization, const uint8_t *remove_mask) {
    size_t source_len = realization->row_len;
    size_t target = 0u;
    for (size_t source = 0u; source < source_len; source++) {
        realization->work.source_rows++;
        if (remove_mask[source])
            continue;
        if (source != target) {
            realization->rows[target] = realization->rows[source];
            realization->work.row_writes++;
        }
        target++;
    }
    realization->row_len = target;
    realization->leaves_dirty = true;
}

static void realization_contract(
        Realization *realization, const uint8_t *remove_mask) {
    switch (realization->kind) {
    case REALIZATION_DENSE_TRANSPORT:
        dense_transport_contract(realization, remove_mask);
        break;
    case REALIZATION_STABLE_IDENTITY:
        stable_identity_contract(realization, remove_mask);
        break;
    case REALIZATION_REBUILD:
        rebuild_contract(realization, remove_mask);
        break;
    case REALIZATION_LAZY_REBUILD:
        lazy_rebuild_contract(realization, remove_mask);
        break;
    default:
        fail("stable-occurrence tournament: unknown realization");
    }
}

static uint64_t digest_step(uint64_t digest, Coordinate coordinate) {
    return digest * UINT64_C(0x100000001b3) + coordinate + 1u;
}

static uint64_t realization_query(
        Realization *realization, size_t family) {
    if (realization->leaves_dirty)
        rebuild_leaf_families(realization);
    assert(family < realization->family_count);
    WordVector *leaves = &realization->leaf_families[family];
    uint64_t digest = UINT64_C(0x6a09e667f3bcc909);
    uint64_t count = 0u;
    for (size_t i = 0u; i < leaves->len; i++) {
        realization->work.query_leaf_reads++;
        Coordinate coordinate;
        if (realization->kind == REALIZATION_STABLE_IDENTITY) {
            OccurrenceId id = leaves->items[i];
            assert(id < realization->coordinate_len);
            realization->work.projection_reads++;
            coordinate = realization->coordinates[(size_t)id];
            if (coordinate == COORDINATE_REMOVED) {
                realization->work.stale_filtered++;
                continue;
            }
        } else {
            coordinate = leaves->items[i];
        }
        assert(coordinate < realization->row_len);
        digest = digest_step(digest, coordinate);
        count++;
    }
    return digest ^ mix64(count);
}

static void assert_rows_equal(
        const Realization *left, const Realization *right) {
    assert(left->row_len == right->row_len);
    for (size_t i = 0u; i < left->row_len; i++) {
        assert(left->rows[i].id == right->rows[i].id);
        assert(left->rows[i].payload == right->rows[i].payload);
    }
}

static void assert_all_queries_equal(
        const Realization *dense, const Realization *stable,
        const Realization *rebuilt, const Realization *lazy) {
    assert(dense->kind == REALIZATION_DENSE_TRANSPORT);
    assert(stable->kind == REALIZATION_STABLE_IDENTITY);
    assert(rebuilt->kind == REALIZATION_REBUILD);
    assert(lazy->kind == REALIZATION_LAZY_REBUILD);
    assert(!lazy->leaves_dirty);
    assert_rows_equal(dense, stable);
    assert_rows_equal(dense, rebuilt);
    assert_rows_equal(dense, lazy);
    assert(dense->family_count == stable->family_count);
    assert(dense->family_count == rebuilt->family_count);
    assert(dense->family_count == lazy->family_count);

    for (size_t family = 0u; family < dense->family_count; family++) {
        const WordVector *expected = &dense->leaf_families[family];
        const WordVector *rebuilt_leaves = &rebuilt->leaf_families[family];
        const WordVector *lazy_leaves = &lazy->leaf_families[family];
        assert(expected->len == rebuilt_leaves->len);
        assert(expected->len == lazy_leaves->len);
        for (size_t i = 0u; i < expected->len; i++)
            assert(expected->items[i] == rebuilt_leaves->items[i]);
        for (size_t i = 0u; i < expected->len; i++)
            assert(expected->items[i] == lazy_leaves->items[i]);

        const WordVector *stable_leaves = &stable->leaf_families[family];
        size_t expected_index = 0u;
        for (size_t i = 0u; i < stable_leaves->len; i++) {
            OccurrenceId id = stable_leaves->items[i];
            assert(id < stable->coordinate_len);
            Coordinate coordinate = stable->coordinates[(size_t)id];
            if (coordinate == COORDINATE_REMOVED)
                continue;
            assert(expected_index < expected->len);
            assert(expected->items[expected_index] == coordinate);
            expected_index++;
        }
        assert(expected_index == expected->len);
    }
}

static void set_removal_mask(const Workload *workload, size_t cycle,
                             uint8_t *mask) {
    size_t length = workload->live_occurrences;
    size_t removals = workload->removals_per_cycle;
    assert(removals <= length);
    memset(mask, 0, length);

    switch (workload->removal_pattern) {
    case REMOVE_PREFIX:
        memset(mask, 1, removals);
        break;
    case REMOVE_SUFFIX:
        memset(mask + (length - removals), 1, removals);
        break;
    case REMOVE_MIDDLE: {
        size_t start = (length - removals) / 2u;
        memset(mask + start, 1, removals);
        break;
    }
    case REMOVE_STRIDED: {
        size_t stride = length / removals;
        if (stride == 0u)
            stride = 1u;
        size_t cursor = cycle % stride;
        for (size_t selected = 0u; selected < removals; selected++) {
            while (mask[cursor])
                cursor = (cursor + 1u) % length;
            mask[cursor] = 1u;
            cursor = (cursor + stride) % length;
        }
        break;
    }
    case REMOVE_SCATTERED: {
        uint64_t state = mix64((uint64_t)cycle + UINT64_C(0x243f6a8885a308d3));
        size_t selected = 0u;
        while (selected < removals) {
            state = mix64(state + UINT64_C(0x9e3779b97f4a7c15));
            size_t coordinate = (size_t)(state % length);
            if (mask[coordinate])
                continue;
            mask[coordinate] = 1u;
            selected++;
        }
        break;
    }
    default:
        fail("stable-occurrence tournament: unknown removal pattern");
    }
}

static uint64_t payload_for(OccurrenceId id, uint64_t modulus) {
    if (modulus == 0u)
        return mix64(id);
    return mix64(id % modulus);
}

static void initialize_realizations(
        const Workload *workload, Realization realizations[REALIZATION_COUNT],
        OccurrenceId *next_id) {
    for (size_t kind = 0u; kind < REALIZATION_COUNT; kind++) {
        realization_init(&realizations[kind], (RealizationKind)kind,
            workload->bucket_count, workload->index_count);
    }
    for (OccurrenceId id = 0u;
         id < (OccurrenceId)workload->live_occurrences; id++) {
        Occurrence occurrence = {
            .id = id,
            .payload = payload_for(id, workload->payload_modulus),
        };
        for (size_t kind = 0u; kind < REALIZATION_COUNT; kind++)
            realization_append(&realizations[kind], occurrence);
    }
    for (size_t kind = 0u; kind < REALIZATION_COUNT; kind++)
        realization_note_peak(&realizations[kind]);
    *next_id = (OccurrenceId)workload->live_occurrences;
    assert_all_queries_equal(
        &realizations[REALIZATION_DENSE_TRANSPORT],
        &realizations[REALIZATION_STABLE_IDENTITY],
        &realizations[REALIZATION_REBUILD],
        &realizations[REALIZATION_LAZY_REBUILD]);
}

static void timed_contract_and_append(
        Realization *realization, const Workload *workload,
        const uint8_t *remove_mask, OccurrenceId first_new_id) {
    uint64_t start = monotonic_ns();
    realization_contract(realization, remove_mask);
    for (size_t offset = 0u; offset < workload->removals_per_cycle; offset++) {
        OccurrenceId id = first_new_id + (OccurrenceId)offset;
        realization_append(realization, (Occurrence){
            .id = id,
            .payload = payload_for(id, workload->payload_modulus),
        });
    }
    realization->mutation_ns += monotonic_ns() - start;
    realization_note_peak(realization);
    assert(realization->row_len == workload->live_occurrences);
}

static void timed_queries(Realization *realization,
                          const Workload *workload, size_t cycle) {
    uint64_t start = monotonic_ns();
    uint64_t checksum = 0u;
    for (size_t query = 0u; query < workload->queries_per_cycle; query++) {
        size_t family = (size_t)(mix64(
            (uint64_t)cycle * UINT64_C(0x9e3779b97f4a7c15) +
            (uint64_t)query) % realization->family_count);
        checksum ^= realization_query(realization, family);
    }
    realization->query_ns += monotonic_ns() - start;
    realization->checksum = mix64(realization->checksum ^ checksum);
    benchmark_sink += checksum + (uint64_t)realization->kind + 1u;
}

static Result result_from_realization(const Realization *realization) {
    return (Result){
        .mutation_ns = realization->mutation_ns,
        .query_ns = realization->query_ns,
        .peak_payload_bytes = realization->peak_payload_bytes,
        .checksum = realization->checksum,
        .work = realization->work,
    };
}

static void run_workload_once(
        const Workload *workload, Result out[REALIZATION_COUNT]) {
    Realization realizations[REALIZATION_COUNT];
    OccurrenceId next_id = 0u;
    uint8_t *remove_mask = calloc(workload->live_occurrences, 1u);
    if (!remove_mask)
        fail("stable-occurrence tournament: mask allocation failed");
    initialize_realizations(workload, realizations, &next_id);
    assert(workload->query_period != 0u);
    assert(workload->cycles % workload->query_period == 0u);

    for (size_t cycle = 0u; cycle < workload->cycles; cycle++) {
        set_removal_mask(workload, cycle, remove_mask);
        size_t mutation_first = cycle % REALIZATION_COUNT;
        for (size_t offset = 0u; offset < REALIZATION_COUNT; offset++) {
            size_t kind = (mutation_first + offset) % REALIZATION_COUNT;
            timed_contract_and_append(
                &realizations[kind], workload, remove_mask, next_id);
        }
        next_id += (OccurrenceId)workload->removals_per_cycle;
        for (size_t kind = 1u; kind < REALIZATION_COUNT; kind++)
            assert_rows_equal(&realizations[0], &realizations[kind]);

        if ((cycle + 1u) % workload->query_period == 0u) {
            size_t query_first = (cycle + 1u) % REALIZATION_COUNT;
            for (size_t offset = 0u; offset < REALIZATION_COUNT; offset++) {
                size_t kind = (query_first + offset) % REALIZATION_COUNT;
                timed_queries(&realizations[kind], workload, cycle);
            }
            assert_all_queries_equal(
                &realizations[REALIZATION_DENSE_TRANSPORT],
                &realizations[REALIZATION_STABLE_IDENTITY],
                &realizations[REALIZATION_REBUILD],
                &realizations[REALIZATION_LAZY_REBUILD]);
        }
    }

    uint64_t expected_checksum =
        realizations[REALIZATION_DENSE_TRANSPORT].checksum;
    for (size_t kind = 0u; kind < REALIZATION_COUNT; kind++) {
        assert(realizations[kind].checksum == expected_checksum);
        out[kind] = result_from_realization(&realizations[kind]);
        realization_free(&realizations[kind]);
    }
    free(remove_mask);
}

static int compare_u64(const void *left, const void *right) {
    uint64_t a = *(const uint64_t *)left;
    uint64_t b = *(const uint64_t *)right;
    return (a > b) - (a < b);
}

static uint64_t median_u64(const uint64_t *values, size_t count) {
    uint64_t *copy = checked_realloc(NULL, count, sizeof(*copy));
    memcpy(copy, values, count * sizeof(*copy));
    qsort(copy, count, sizeof(*copy), compare_u64);
    uint64_t median = copy[count / 2u];
    free(copy);
    return median;
}

static bool work_equal(const Work *left, const Work *right) {
    return left->source_rows == right->source_rows &&
        left->row_writes == right->row_writes &&
        left->mutation_leaf_reads == right->mutation_leaf_reads &&
        left->mutation_leaf_writes == right->mutation_leaf_writes &&
        left->query_leaf_reads == right->query_leaf_reads &&
        left->projection_reads == right->projection_reads &&
        left->stale_filtered == right->stale_filtered &&
        left->rebuilds == right->rebuilds;
}

static Result median_result(const Result *results, size_t count) {
    uint64_t *mutation = checked_realloc(NULL, count, sizeof(*mutation));
    uint64_t *query = checked_realloc(NULL, count, sizeof(*query));
    uint64_t *bytes = checked_realloc(NULL, count, sizeof(*bytes));
    for (size_t i = 0u; i < count; i++) {
        mutation[i] = results[i].mutation_ns;
        query[i] = results[i].query_ns;
        bytes[i] = results[i].peak_payload_bytes;
        assert(results[i].checksum == results[0].checksum);
        assert(work_equal(&results[i].work, &results[0].work));
    }
    Result median = results[0];
    median.mutation_ns = median_u64(mutation, count);
    median.query_ns = median_u64(query, count);
    median.peak_payload_bytes = median_u64(bytes, count);
    free(bytes);
    free(query);
    free(mutation);
    return median;
}

static void run_small_canaries(void) {
    Workload canary = {
        .name = "canary",
        .live_occurrences = 4u,
        .bucket_count = 2u,
        .index_count = 2u,
        .cycles = 1u,
        .removals_per_cycle = 1u,
        .queries_per_cycle = 8u,
        .query_period = 1u,
        .payload_modulus = 1u,
        .removal_pattern = REMOVE_PREFIX,
    };
    Result result[REALIZATION_COUNT];
    run_workload_once(&canary, result);
    assert(result[REALIZATION_STABLE_IDENTITY].work.mutation_leaf_reads == 0u);
    assert(result[REALIZATION_DENSE_TRANSPORT].work.mutation_leaf_reads > 0u);

    Realization duplicate_ids;
    realization_init(&duplicate_ids, REALIZATION_STABLE_IDENTITY, 2u, 1u);
    realization_append(&duplicate_ids, (Occurrence){.id = 7u, .payload = 1u});
    assert(!realization_identity_is_fresh(&duplicate_ids, 7u));
    assert(realization_identity_is_fresh(&duplicate_ids, 8u));
    realization_free(&duplicate_ids);
}

static void print_result(const Workload *workload, RealizationKind kind,
                         const Result *result) {
    printf("%s\t%s\t%.3f\t%.3f\t%.3f\t%" PRIu64
           "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
           "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
           "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\n",
        workload->name, realization_name(kind),
        (double)result->mutation_ns / 1000000.0,
        (double)result->query_ns / 1000000.0,
        (double)(result->mutation_ns + result->query_ns) / 1000000.0,
        result->peak_payload_bytes,
        result->work.source_rows,
        result->work.row_writes,
        result->work.mutation_leaf_reads,
        result->work.mutation_leaf_writes,
        result->work.query_leaf_reads,
        result->work.projection_reads,
        result->work.stale_filtered,
        result->work.rebuilds,
        result->checksum);
}

int main(int argc, char **argv) {
    size_t trials = 5u;
    if (argc > 2) {
        fputs("usage: bench_realizations [odd-trial-count]\n", stderr);
        return EXIT_FAILURE;
    }
    if (argc == 2) {
        errno = 0;
        char *end = NULL;
        unsigned long parsed = strtoul(argv[1], &end, 10);
        if (errno != 0 || !end || *end != '\0' || parsed == 0u ||
            parsed > 31u || parsed % 2u == 0u) {
            fputs("trial count must be an odd integer from 1 through 31\n",
                  stderr);
            return EXIT_FAILURE;
        }
        trials = (size_t)parsed;
    }

    const Workload workloads[] = {
        {"read-heavy-selective", 32768u, 4096u, 2u, 32u, 8u, 4096u, 1u,
         8192u, REMOVE_SCATTERED},
        {"read-heavy-broad", 32768u, 4u, 2u, 32u, 8u, 256u, 1u,
         8192u, REMOVE_SCATTERED},
        {"prefix-churn", 16384u, 128u, 4u, 256u, 64u, 32u, 1u,
         1024u, REMOVE_PREFIX},
        {"suffix-churn", 16384u, 128u, 4u, 256u, 64u, 32u, 1u,
         1024u, REMOVE_SUFFIX},
        {"duplicate-payloads", 32768u, 64u, 2u, 128u, 32u, 64u, 1u,
         8u, REMOVE_STRIDED},
        {"many-derived-indexes", 16384u, 256u, 8u, 128u, 32u, 64u, 1u,
         4096u, REMOVE_MIDDLE},
        {"mixed-batch", 65536u, 512u, 2u, 64u, 256u, 256u, 1u,
         16384u, REMOVE_SCATTERED},
        {"reclamation-pressure", 8192u, 64u, 2u, 32u, 512u, 128u, 1u,
         512u, REMOVE_PREFIX},
        {"mutation-bursts", 32768u, 256u, 2u, 128u, 32u, 128u, 16u,
         8192u, REMOVE_SCATTERED},
        {"query-dominant", 32768u, 64u, 2u, 1u, 1u, 65536u, 1u,
         8192u, REMOVE_SCATTERED},
        {"unobserved-mutation-burst", 32768u, 256u, 2u, 128u, 32u, 128u,
         128u, 8192u, REMOVE_SCATTERED},
    };
    const size_t workload_count = sizeof(workloads) / sizeof(workloads[0]);

    run_small_canaries();
    puts("workload\trealization\tmutation_ms\tquery_ms\ttotal_ms\t"
         "peak_payload_bytes\tsource_rows\trow_writes\t"
         "mutation_leaf_reads\tmutation_leaf_writes\tquery_leaf_reads\t"
         "projection_reads\tstale_filtered\trebuilds\tchecksum");

    Result *samples = checked_realloc(
        NULL, trials * REALIZATION_COUNT, sizeof(*samples));
    for (size_t workload_index = 0u; workload_index < workload_count;
         workload_index++) {
        const Workload *workload = &workloads[workload_index];
        for (size_t trial = 0u; trial < trials; trial++) {
            Result one[REALIZATION_COUNT];
            run_workload_once(workload, one);
            for (size_t kind = 0u; kind < REALIZATION_COUNT; kind++)
                samples[kind * trials + trial] = one[kind];
        }
        for (size_t kind = 0u; kind < REALIZATION_COUNT; kind++) {
            Result median = median_result(&samples[kind * trials], trials);
            print_result(workload, (RealizationKind)kind, &median);
        }
    }
    free(samples);
    printf("PASS\tstable-occurrence-realization-tournament\t%" PRIu64 "\n",
           benchmark_sink);
    return EXIT_SUCCESS;
}
