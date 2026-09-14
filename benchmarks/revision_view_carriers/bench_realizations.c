#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef uint64_t OccurrenceId;
typedef uint64_t Revision;
typedef uint32_t Head;

static const Head HEAD_ANY = UINT32_MAX;
static const Head HEAD_WILDCARD = UINT32_MAX - 1u;
static const Revision REVISION_LIVE = UINT64_MAX;
static volatile uint64_t benchmark_sink = 0u;

typedef struct {
    OccurrenceId id;
    uint64_t payload;
    uint64_t order;
    Head head;
} Occurrence;

typedef struct {
    Revision revision;
    Occurrence *items;
    size_t len;
} OracleSnapshot;

typedef enum {
    MUTATION_SCATTERED,
    MUTATION_HOT_HEAD,
    MUTATION_UNRELATED_HEAD,
    MUTATION_APPEND_ONLY,
} MutationPattern;

typedef struct {
    uint8_t *remove_mask;
    Occurrence *removed;
    size_t removed_len;
    Occurrence *appended;
    size_t appended_len;
} Mutation;

typedef enum {
    QUERY_MIXED,
    QUERY_HOT_HEAD,
    QUERY_UNRELATED_HEAD,
    QUERY_BROAD,
} QueryPattern;

typedef struct {
    const char *name;
    size_t initial_occurrences;
    size_t head_count;
    size_t query_head_count;
    size_t wildcard_stride;
    size_t cycles;
    size_t removals_per_cycle;
    size_t appends_per_cycle;
    size_t unchanged_republish_every;
    size_t acquisitions_per_cycle;
    size_t queries_per_lease;
    size_t lease_window;
    uint64_t payload_modulus;
    MutationPattern mutation_pattern;
    QueryPattern query_pattern;
} Workload;

typedef struct {
    uint64_t authority_builds;
    uint64_t exact_key_reuses;
    uint64_t occurrence_reads;
    uint64_t occurrence_writes;
    uint64_t carrier_copied_bytes;
    uint64_t observer_copied_bytes;
    uint64_t index_reads;
    uint64_t index_writes;
    uint64_t mutation_membership_tests;
    uint64_t visibility_tests;
    uint64_t merge_comparisons;
    uint64_t reference_operations;
    uint64_t reclamation_scans;
    uint64_t queries;
    uint64_t emitted_candidates;
} Work;

typedef struct {
    uint64_t allocation_calls;
    uint64_t allocated_bytes;
    uint64_t freed_bytes;
    uint64_t live_bytes;
    uint64_t peak_live_bytes;
    uint64_t carrier_allocation_calls;
    uint64_t carrier_allocated_bytes;
    uint64_t carrier_freed_bytes;
    uint64_t carrier_live_bytes;
    uint64_t carrier_peak_live_bytes;
    uint64_t observer_allocation_calls;
    uint64_t observer_allocated_bytes;
    uint64_t observer_freed_bytes;
    uint64_t observer_live_bytes;
    uint64_t observer_peak_live_bytes;
} AllocationTracker;

typedef enum {
    ALLOCATION_CARRIER,
    ALLOCATION_OBSERVER,
} AllocationRole;

typedef union {
    max_align_t alignment;
    struct {
        size_t bytes;
        AllocationRole role;
    } metadata;
} AllocationHeader;

typedef struct {
    size_t *items;
    size_t len;
    size_t cap;
} SizeVector;

typedef struct {
    Occurrence *items;
    size_t len;
    size_t cap;
} QueryResult;

typedef enum {
    REALIZATION_OWNED_FLAT_SCAN = 0,
    REALIZATION_OWNED_FLAT_INDEX = 1,
    REALIZATION_PERSISTENT_HEAD_BLOCKS = 2,
    REALIZATION_REVISION_LOG = 3,
    REALIZATION_COUNT = 4,
} RealizationKind;

typedef struct FlatRoot FlatRoot;
typedef struct PersistentRoot PersistentRoot;
typedef struct VersionRoot VersionRoot;

typedef struct {
    RealizationKind kind;
    Revision revision;
    void *object;
} Lease;

typedef struct {
    FlatRoot *current;
    bool indexed;
} FlatAuthority;

typedef struct HeadBlock HeadBlock;

typedef struct {
    PersistentRoot *current;
} PersistentAuthority;

typedef struct {
    Occurrence occurrence;
    Revision born;
    Revision dead;
} LogEntry;

typedef struct {
    LogEntry *entries;
    size_t entry_len;
    size_t entry_cap;
    SizeVector *heads;
    size_t head_count;
    size_t *id_to_index;
    size_t id_len;
    size_t id_cap;
    Revision current_revision;
    VersionRoot *roots;
    VersionRoot *current;
    uint64_t retired_bytes;
} RevisionLog;

typedef struct Realization {
    RealizationKind kind;
    size_t head_count;
    Work work;
    AllocationTracker allocations;
    uint64_t elapsed_ns;
    uint64_t digest;
    uint64_t peak_retired_bytes;
    union {
        FlatAuthority flat;
        PersistentAuthority persistent;
        RevisionLog log;
    } state;
} Realization;

struct FlatRoot {
    size_t references;
    Revision revision;
    Occurrence *rows;
    size_t row_len;
    SizeVector *heads;
    size_t head_count;
};

struct HeadBlock {
    size_t references;
    Occurrence *rows;
    size_t row_len;
};

struct PersistentRoot {
    size_t references;
    Revision revision;
    HeadBlock **heads;
    size_t head_count;
    size_t row_len;
};

struct VersionRoot {
    size_t references;
    Revision revision;
    VersionRoot *previous;
    VersionRoot *next;
};

typedef struct {
    Work work;
    AllocationTracker allocations;
    uint64_t elapsed_ns;
    uint64_t digest;
    uint64_t peak_retired_bytes;
} Result;

typedef struct {
    bool occupied;
    OracleSnapshot oracle;
    Lease leases[REALIZATION_COUNT];
} HeldRevision;

static void fail(const char *message) {
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
}

static size_t checked_multiply(size_t count, size_t width) {
    if (width != 0u && count > SIZE_MAX / width)
        fail("revision-view tournament: allocation size overflow");
    return count * width;
}

static void *system_allocate(size_t count, size_t width) {
    size_t bytes = checked_multiply(count, width);
    if (bytes == 0u)
        return NULL;
    void *result = calloc(1u, bytes);
    if (!result)
        fail("revision-view tournament: system allocation failed");
    return result;
}

static void *tracked_allocate(AllocationTracker *tracker, size_t bytes,
                              AllocationRole role) {
    if (bytes == 0u)
        return NULL;
    if (bytes > SIZE_MAX - sizeof(AllocationHeader))
        fail("revision-view tournament: tracked allocation overflow");
    AllocationHeader *header = calloc(1u, sizeof(*header) + bytes);
    if (!header)
        fail("revision-view tournament: tracked allocation failed");
    header->metadata.bytes = bytes;
    header->metadata.role = role;
    tracker->allocation_calls++;
    tracker->allocated_bytes += (uint64_t)bytes;
    tracker->live_bytes += (uint64_t)bytes;
    if (tracker->live_bytes > tracker->peak_live_bytes)
        tracker->peak_live_bytes = tracker->live_bytes;
    if (role == ALLOCATION_CARRIER) {
        tracker->carrier_allocation_calls++;
        tracker->carrier_allocated_bytes += (uint64_t)bytes;
        tracker->carrier_live_bytes += (uint64_t)bytes;
        if (tracker->carrier_live_bytes > tracker->carrier_peak_live_bytes)
            tracker->carrier_peak_live_bytes = tracker->carrier_live_bytes;
    } else {
        tracker->observer_allocation_calls++;
        tracker->observer_allocated_bytes += (uint64_t)bytes;
        tracker->observer_live_bytes += (uint64_t)bytes;
        if (tracker->observer_live_bytes > tracker->observer_peak_live_bytes)
            tracker->observer_peak_live_bytes = tracker->observer_live_bytes;
    }
    return header + 1;
}

static size_t tracked_size(const void *pointer) {
    if (!pointer)
        return 0u;
    const AllocationHeader *header =
        (const AllocationHeader *)pointer - 1;
    return header->metadata.bytes;
}

static void tracked_free(AllocationTracker *tracker, void *pointer) {
    if (!pointer)
        return;
    AllocationHeader *header = (AllocationHeader *)pointer - 1;
    size_t bytes = header->metadata.bytes;
    AllocationRole role = header->metadata.role;
    assert(tracker->live_bytes >= (uint64_t)bytes);
    tracker->live_bytes -= (uint64_t)bytes;
    tracker->freed_bytes += (uint64_t)bytes;
    if (role == ALLOCATION_CARRIER) {
        assert(tracker->carrier_live_bytes >= (uint64_t)bytes);
        tracker->carrier_live_bytes -= (uint64_t)bytes;
        tracker->carrier_freed_bytes += (uint64_t)bytes;
    } else {
        assert(tracker->observer_live_bytes >= (uint64_t)bytes);
        tracker->observer_live_bytes -= (uint64_t)bytes;
        tracker->observer_freed_bytes += (uint64_t)bytes;
    }
    free(header);
}

static void *tracked_grow(Realization *realization, void *old_pointer,
                          size_t old_count, size_t new_count, size_t width,
                          AllocationRole role) {
    assert(new_count >= old_count);
    size_t old_bytes = checked_multiply(old_count, width);
    size_t new_bytes = checked_multiply(new_count, width);
    assert(tracked_size(old_pointer) == old_bytes);
    void *new_pointer = tracked_allocate(
        &realization->allocations, new_bytes, role);
    if (old_bytes != 0u) {
        memcpy(new_pointer, old_pointer, old_bytes);
        if (role == ALLOCATION_CARRIER)
            realization->work.carrier_copied_bytes += (uint64_t)old_bytes;
        else
            realization->work.observer_copied_bytes += (uint64_t)old_bytes;
    }
    tracked_free(&realization->allocations, old_pointer);
    return new_pointer;
}

static uint64_t monotonic_ns(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        fail("revision-view tournament: clock_gettime failed");
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

static bool occurrence_equal(Occurrence left, Occurrence right) {
    return left.id == right.id && left.payload == right.payload &&
        left.order == right.order && left.head == right.head;
}

static uint64_t digest_occurrence(uint64_t digest, Occurrence occurrence) {
    digest ^= mix64(occurrence.id + UINT64_C(0x9e3779b97f4a7c15));
    digest = mix64(digest ^ occurrence.payload);
    digest = mix64(digest ^ occurrence.order);
    return mix64(digest ^ (uint64_t)occurrence.head);
}

static Occurrence make_occurrence(OccurrenceId id, size_t head_count,
                                  uint64_t payload_modulus,
                                  size_t wildcard_stride,
                                  Head forced_head) {
    uint64_t payload_key = payload_modulus == 0u
        ? id
        : id % payload_modulus;
    Head head;
    if (forced_head != HEAD_ANY) {
        head = forced_head;
    } else if (wildcard_stride != 0u && id % wildcard_stride == 0u) {
        head = HEAD_WILDCARD;
    } else {
        head = (Head)(mix64(id + UINT64_C(0x243f6a8885a308d3)) %
            head_count);
    }
    return (Occurrence){
        .id = id,
        .payload = mix64(payload_key),
        .order = id,
        .head = head,
    };
}

static OracleSnapshot oracle_initial(const Workload *workload) {
    OracleSnapshot snapshot = {
        .revision = 0u,
        .items = system_allocate(
            workload->initial_occurrences, sizeof(*snapshot.items)),
        .len = workload->initial_occurrences,
    };
    for (size_t i = 0u; i < snapshot.len; i++) {
        snapshot.items[i] = make_occurrence(
            (OccurrenceId)i, workload->head_count,
            workload->payload_modulus, workload->wildcard_stride,
            HEAD_ANY);
    }
    return snapshot;
}

static OracleSnapshot oracle_clone(const OracleSnapshot *source) {
    OracleSnapshot clone = {
        .revision = source->revision,
        .items = system_allocate(source->len, sizeof(*clone.items)),
        .len = source->len,
    };
    if (source->len != 0u)
        memcpy(clone.items, source->items,
               source->len * sizeof(*clone.items));
    return clone;
}

static void oracle_free(OracleSnapshot *snapshot) {
    free(snapshot->items);
    memset(snapshot, 0, sizeof(*snapshot));
}

static void mutation_free(Mutation *mutation) {
    free(mutation->remove_mask);
    free(mutation->removed);
    free(mutation->appended);
    memset(mutation, 0, sizeof(*mutation));
}

static Mutation mutation_generate(const Workload *workload,
                                  const OracleSnapshot *current,
                                  OccurrenceId next_id, size_t cycle) {
    if (workload->removals_per_cycle > current->len)
        fail("revision-view tournament: removal exceeds live family");
    Mutation mutation = {
        .remove_mask = system_allocate(current->len, 1u),
        .removed = system_allocate(
            workload->removals_per_cycle, sizeof(*mutation.removed)),
        .removed_len = workload->removals_per_cycle,
        .appended = system_allocate(
            workload->appends_per_cycle, sizeof(*mutation.appended)),
        .appended_len = workload->appends_per_cycle,
    };

    size_t selected = 0u;
    if (workload->mutation_pattern == MUTATION_HOT_HEAD ||
        workload->mutation_pattern == MUTATION_UNRELATED_HEAD) {
        for (size_t i = 0u; i < current->len &&
             selected < mutation.removed_len; i++) {
            if (current->items[i].head != 0u)
                continue;
            mutation.remove_mask[i] = 1u;
            mutation.removed[selected++] = current->items[i];
        }
    } else {
        uint64_t state = mix64((uint64_t)cycle +
            UINT64_C(0x13198a2e03707344));
        while (selected < mutation.removed_len) {
            state = mix64(state + UINT64_C(0x9e3779b97f4a7c15));
            size_t coordinate = (size_t)(state % current->len);
            if (mutation.remove_mask[coordinate])
                continue;
            mutation.remove_mask[coordinate] = 1u;
            mutation.removed[selected++] = current->items[coordinate];
        }
    }
    if (selected != mutation.removed_len)
        fail("revision-view tournament: insufficient hot-head occurrences");

    for (size_t i = 0u; i < mutation.appended_len; i++) {
        Head forced = (workload->mutation_pattern == MUTATION_HOT_HEAD ||
            workload->mutation_pattern == MUTATION_UNRELATED_HEAD)
            ? 0u
            : HEAD_ANY;
        mutation.appended[i] = make_occurrence(
            next_id + (OccurrenceId)i, workload->head_count,
            workload->payload_modulus, workload->wildcard_stride,
            forced);
    }
    return mutation;
}

static OracleSnapshot oracle_apply(const OracleSnapshot *current,
                                   const Mutation *mutation) {
    assert(mutation->removed_len <= current->len);
    size_t next_len = current->len - mutation->removed_len +
        mutation->appended_len;
    OracleSnapshot next = {
        .revision = current->revision + 1u,
        .items = system_allocate(next_len, sizeof(*next.items)),
        .len = next_len,
    };
    size_t write = 0u;
    for (size_t read = 0u; read < current->len; read++) {
        if (!mutation->remove_mask[read])
            next.items[write++] = current->items[read];
    }
    for (size_t i = 0u; i < mutation->appended_len; i++)
        next.items[write++] = mutation->appended[i];
    assert(write == next.len);
    return next;
}

static Occurrence *oracle_query(const OracleSnapshot *snapshot, Head head,
                                size_t *length) {
    Occurrence *result = system_allocate(snapshot->len, sizeof(*result));
    size_t write = 0u;
    for (size_t i = 0u; i < snapshot->len; i++) {
        if (head == HEAD_ANY || snapshot->items[i].head == head ||
            snapshot->items[i].head == HEAD_WILDCARD)
            result[write++] = snapshot->items[i];
    }
    *length = write;
    return result;
}

static size_t head_bucket(size_t concrete_head_count, Head head) {
    if (head == HEAD_WILDCARD)
        return concrete_head_count;
    assert(head < concrete_head_count);
    return (size_t)head;
}

static Head bucket_pattern(size_t concrete_head_count, size_t bucket) {
    assert(bucket <= concrete_head_count);
    return bucket == concrete_head_count
        ? HEAD_WILDCARD
        : (Head)bucket;
}

static void size_vector_reserve(Realization *realization, SizeVector *vector,
                                size_t needed) {
    if (needed <= vector->cap)
        return;
    size_t capacity = vector->cap ? vector->cap : 4u;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2u)
            fail("revision-view tournament: vector capacity overflow");
        capacity *= 2u;
    }
    vector->items = tracked_grow(realization, vector->items, vector->cap,
        capacity, sizeof(*vector->items), ALLOCATION_CARRIER);
    vector->cap = capacity;
}

static void size_vector_push(Realization *realization, SizeVector *vector,
                             size_t value) {
    size_vector_reserve(realization, vector, vector->len + 1u);
    vector->items[vector->len++] = value;
}

static void size_vector_release(Realization *realization,
                                SizeVector *vector) {
    tracked_free(&realization->allocations, vector->items);
    memset(vector, 0, sizeof(*vector));
}

static void query_result_push(Realization *realization, QueryResult *result,
                              Occurrence occurrence) {
    if (result->len == result->cap) {
        size_t capacity = result->cap ? result->cap * 2u : 8u;
        if (capacity < result->cap)
            fail("revision-view tournament: result capacity overflow");
        result->items = tracked_grow(realization, result->items, result->cap,
            capacity, sizeof(*result->items), ALLOCATION_OBSERVER);
        result->cap = capacity;
    }
    result->items[result->len++] = occurrence;
    realization->work.emitted_candidates++;
}

static void query_result_release(Realization *realization,
                                 QueryResult *result) {
    tracked_free(&realization->allocations, result->items);
    memset(result, 0, sizeof(*result));
}

static const char *realization_name(RealizationKind kind) {
    switch (kind) {
    case REALIZATION_OWNED_FLAT_SCAN:
        return "owned-flat-scan";
    case REALIZATION_OWNED_FLAT_INDEX:
        return "owned-flat-index";
    case REALIZATION_PERSISTENT_HEAD_BLOCKS:
        return "persistent-head-blocks";
    case REALIZATION_REVISION_LOG:
        return "revision-log";
    default:
        return "unknown";
    }
}

static const char *realization_layout(RealizationKind kind) {
    switch (kind) {
    case REALIZATION_OWNED_FLAT_SCAN:
    case REALIZATION_OWNED_FLAT_INDEX:
        return "owned-flat";
    case REALIZATION_PERSISTENT_HEAD_BLOCKS:
        return "persistent-head-blocks";
    case REALIZATION_REVISION_LOG:
        return "append-version-log";
    default:
        return "unknown";
    }
}

static const char *realization_index(RealizationKind kind) {
    switch (kind) {
    case REALIZATION_OWNED_FLAT_SCAN:
        return "scan";
    case REALIZATION_OWNED_FLAT_INDEX:
        return "head-coordinates";
    case REALIZATION_PERSISTENT_HEAD_BLOCKS:
        return "head-block";
    case REALIZATION_REVISION_LOG:
        return "head-log-indices";
    default:
        return "unknown";
    }
}

static const char *realization_lifetime(RealizationKind kind) {
    return kind == REALIZATION_REVISION_LOG
        ? "revision-lease"
        : "reference-counted-immutable";
}

static FlatRoot *flat_root_build(Realization *realization,
                                 const OracleSnapshot *snapshot,
                                 bool indexed) {
    FlatRoot *root = tracked_allocate(
        &realization->allocations, sizeof(*root), ALLOCATION_CARRIER);
    root->references = 1u;
    root->revision = snapshot->revision;
    root->row_len = snapshot->len;
    root->head_count = realization->head_count + 1u;
    root->rows = tracked_allocate(&realization->allocations,
        checked_multiply(snapshot->len, sizeof(*root->rows)),
        ALLOCATION_CARRIER);
    if (snapshot->len != 0u) {
        memcpy(root->rows, snapshot->items,
               snapshot->len * sizeof(*root->rows));
        realization->work.carrier_copied_bytes +=
            (uint64_t)(snapshot->len * sizeof(*root->rows));
        realization->work.occurrence_reads += snapshot->len;
        realization->work.occurrence_writes += snapshot->len;
    }
    if (!indexed)
        return root;

    root->heads = tracked_allocate(&realization->allocations,
        checked_multiply(root->head_count, sizeof(*root->heads)),
        ALLOCATION_CARRIER);
    size_t *counts = tracked_allocate(&realization->allocations,
        checked_multiply(root->head_count, sizeof(*counts)),
        ALLOCATION_CARRIER);
    for (size_t i = 0u; i < root->row_len; i++) {
        size_t bucket = head_bucket(
            realization->head_count, root->rows[i].head);
        counts[bucket]++;
        realization->work.occurrence_reads++;
        realization->work.index_writes++;
    }
    for (size_t head = 0u; head < root->head_count; head++) {
        root->heads[head].cap = counts[head];
        root->heads[head].items = tracked_allocate(
            &realization->allocations,
            checked_multiply(counts[head],
                             sizeof(*root->heads[head].items)),
            ALLOCATION_CARRIER);
    }
    memset(counts, 0, root->head_count * sizeof(*counts));
    for (size_t i = 0u; i < root->row_len; i++) {
        size_t bucket = head_bucket(
            realization->head_count, root->rows[i].head);
        size_t coordinate = counts[bucket]++;
        root->heads[bucket].items[coordinate] = i;
        root->heads[bucket].len++;
        realization->work.occurrence_reads++;
        realization->work.index_reads++;
        realization->work.index_writes++;
    }
    tracked_free(&realization->allocations, counts);
    return root;
}

static void flat_root_retain(Realization *realization, FlatRoot *root) {
    assert(root && root->references != 0u);
    root->references++;
    realization->work.reference_operations++;
}

static void flat_root_release(Realization *realization, FlatRoot *root) {
    if (!root)
        return;
    assert(root->references != 0u);
    root->references--;
    realization->work.reference_operations++;
    if (root->references != 0u)
        return;
    if (root->heads) {
        for (size_t head = 0u; head < root->head_count; head++)
            size_vector_release(realization, &root->heads[head]);
        tracked_free(&realization->allocations, root->heads);
    }
    tracked_free(&realization->allocations, root->rows);
    tracked_free(&realization->allocations, root);
}

static void flat_publish(Realization *realization,
                         const OracleSnapshot *snapshot) {
    FlatAuthority *authority = &realization->state.flat;
    FlatRoot *next = flat_root_build(
        realization, snapshot, authority->indexed);
    FlatRoot *previous = authority->current;
    authority->current = next;
    realization->work.authority_builds++;
    flat_root_release(realization, previous);
}

static Lease flat_acquire(Realization *realization) {
    FlatRoot *root = realization->state.flat.current;
    assert(root);
    flat_root_retain(realization, root);
    realization->work.exact_key_reuses++;
    return (Lease){
        .kind = realization->kind,
        .revision = root->revision,
        .object = root,
    };
}

static void flat_query(Realization *realization, const Lease *lease,
                       Head head, QueryResult *result) {
    FlatRoot *root = lease->object;
    assert(root && root->revision == lease->revision);
    if (realization->state.flat.indexed && head != HEAD_ANY) {
        assert(head < realization->head_count);
        SizeVector *exact = &root->heads[head];
        SizeVector *wildcard = &root->heads[root->head_count - 1u];
        size_t exact_position = 0u;
        size_t wildcard_position = 0u;
        while (exact_position < exact->len ||
               wildcard_position < wildcard->len) {
            size_t exact_coordinate = exact_position < exact->len
                ? exact->items[exact_position]
                : SIZE_MAX;
            size_t wildcard_coordinate = wildcard_position < wildcard->len
                ? wildcard->items[wildcard_position]
                : SIZE_MAX;
            realization->work.index_reads +=
                (exact_coordinate != SIZE_MAX) +
                (wildcard_coordinate != SIZE_MAX);
            size_t coordinate;
            if (exact_coordinate <= wildcard_coordinate) {
                coordinate = exact_coordinate;
                exact_position++;
            } else {
                coordinate = wildcard_coordinate;
                wildcard_position++;
            }
            if (exact_coordinate != SIZE_MAX &&
                wildcard_coordinate != SIZE_MAX)
                realization->work.merge_comparisons++;
            assert(coordinate < root->row_len);
            realization->work.occurrence_reads++;
            query_result_push(realization, result, root->rows[coordinate]);
        }
        return;
    }
    for (size_t i = 0u; i < root->row_len; i++) {
        realization->work.occurrence_reads++;
        if (head == HEAD_ANY || root->rows[i].head == head ||
            root->rows[i].head == HEAD_WILDCARD)
            query_result_push(realization, result, root->rows[i]);
    }
}

static HeadBlock *head_block_new(Realization *realization, size_t row_len) {
    HeadBlock *block = tracked_allocate(
        &realization->allocations, sizeof(*block), ALLOCATION_CARRIER);
    block->references = 1u;
    block->row_len = row_len;
    block->rows = tracked_allocate(&realization->allocations,
        checked_multiply(row_len, sizeof(*block->rows)), ALLOCATION_CARRIER);
    return block;
}

static void head_block_retain(Realization *realization, HeadBlock *block) {
    assert(block && block->references != 0u);
    block->references++;
    realization->work.reference_operations++;
}

static void head_block_release(Realization *realization, HeadBlock *block) {
    assert(block && block->references != 0u);
    block->references--;
    realization->work.reference_operations++;
    if (block->references != 0u)
        return;
    tracked_free(&realization->allocations, block->rows);
    tracked_free(&realization->allocations, block);
}

static PersistentRoot *persistent_root_initial(
        Realization *realization, const OracleSnapshot *snapshot) {
    PersistentRoot *root = tracked_allocate(
        &realization->allocations, sizeof(*root), ALLOCATION_CARRIER);
    root->references = 1u;
    root->revision = snapshot->revision;
    root->head_count = realization->head_count + 1u;
    root->row_len = snapshot->len;
    root->heads = tracked_allocate(&realization->allocations,
        checked_multiply(root->head_count, sizeof(*root->heads)),
        ALLOCATION_CARRIER);
    size_t *counts = tracked_allocate(&realization->allocations,
        checked_multiply(root->head_count, sizeof(*counts)),
        ALLOCATION_CARRIER);
    for (size_t i = 0u; i < snapshot->len; i++) {
        size_t bucket = head_bucket(
            realization->head_count, snapshot->items[i].head);
        counts[bucket]++;
        realization->work.occurrence_reads++;
        realization->work.index_writes++;
    }
    for (size_t head = 0u; head < root->head_count; head++)
        root->heads[head] = head_block_new(realization, counts[head]);
    memset(counts, 0, root->head_count * sizeof(*counts));
    for (size_t i = 0u; i < snapshot->len; i++) {
        Occurrence occurrence = snapshot->items[i];
        size_t bucket = head_bucket(
            realization->head_count, occurrence.head);
        HeadBlock *block = root->heads[bucket];
        block->rows[counts[bucket]++] = occurrence;
        realization->work.occurrence_reads++;
        realization->work.occurrence_writes++;
        realization->work.index_reads++;
        realization->work.index_writes++;
    }
    tracked_free(&realization->allocations, counts);
    return root;
}

static bool mutation_removes_from_head(Realization *realization,
                                       const Mutation *mutation,
                                       Head head, OccurrenceId id) {
    for (size_t i = 0u; i < mutation->removed_len; i++) {
        if (mutation->removed[i].head != head)
            continue;
        realization->work.mutation_membership_tests++;
        if (mutation->removed[i].id == id)
            return true;
    }
    return false;
}

static PersistentRoot *persistent_root_derive(
        Realization *realization, PersistentRoot *previous,
        const Mutation *mutation, Revision revision) {
    PersistentRoot *root = tracked_allocate(
        &realization->allocations, sizeof(*root), ALLOCATION_CARRIER);
    root->references = 1u;
    root->revision = revision;
    root->head_count = previous->head_count;
    root->row_len = previous->row_len - mutation->removed_len +
        mutation->appended_len;
    root->heads = tracked_allocate(&realization->allocations,
        checked_multiply(root->head_count, sizeof(*root->heads)),
        ALLOCATION_CARRIER);
    uint8_t *affected = tracked_allocate(&realization->allocations,
        checked_multiply(root->head_count, sizeof(*affected)),
        ALLOCATION_CARRIER);
    size_t *removed_counts = tracked_allocate(&realization->allocations,
        checked_multiply(root->head_count, sizeof(*removed_counts)),
        ALLOCATION_CARRIER);
    size_t *appended_counts = tracked_allocate(&realization->allocations,
        checked_multiply(root->head_count, sizeof(*appended_counts)),
        ALLOCATION_CARRIER);
    for (size_t i = 0u; i < mutation->removed_len; i++) {
        size_t bucket = head_bucket(
            realization->head_count, mutation->removed[i].head);
        affected[bucket] = 1u;
        removed_counts[bucket]++;
        realization->work.index_writes += 2u;
    }
    for (size_t i = 0u; i < mutation->appended_len; i++) {
        size_t bucket = head_bucket(
            realization->head_count, mutation->appended[i].head);
        affected[bucket] = 1u;
        appended_counts[bucket]++;
        realization->work.index_writes += 2u;
    }

    for (size_t head = 0u; head < root->head_count; head++) {
        HeadBlock *old_block = previous->heads[head];
        if (!affected[head]) {
            root->heads[head] = old_block;
            head_block_retain(realization, old_block);
            realization->work.index_reads++;
            realization->work.index_writes++;
            continue;
        }
        assert(removed_counts[head] <= old_block->row_len);
        size_t new_len = old_block->row_len - removed_counts[head] +
            appended_counts[head];
        HeadBlock *new_block = head_block_new(realization, new_len);
        Head pattern = bucket_pattern(realization->head_count, head);
        size_t write = 0u;
        for (size_t i = 0u; i < old_block->row_len; i++) {
            realization->work.occurrence_reads++;
            Occurrence occurrence = old_block->rows[i];
            if (mutation_removes_from_head(
                    realization, mutation, pattern, occurrence.id))
                continue;
            new_block->rows[write++] = occurrence;
            realization->work.occurrence_writes++;
            realization->work.carrier_copied_bytes += sizeof(occurrence);
        }
        for (size_t i = 0u; i < mutation->appended_len; i++) {
            if (mutation->appended[i].head != pattern)
                continue;
            new_block->rows[write++] = mutation->appended[i];
            realization->work.occurrence_writes++;
        }
        assert(write == new_len);
        root->heads[head] = new_block;
        realization->work.index_writes++;
    }
    tracked_free(&realization->allocations, appended_counts);
    tracked_free(&realization->allocations, removed_counts);
    tracked_free(&realization->allocations, affected);
    return root;
}

static void persistent_root_retain(Realization *realization,
                                   PersistentRoot *root) {
    assert(root && root->references != 0u);
    root->references++;
    realization->work.reference_operations++;
}

static void persistent_root_release(Realization *realization,
                                    PersistentRoot *root) {
    if (!root)
        return;
    assert(root->references != 0u);
    root->references--;
    realization->work.reference_operations++;
    if (root->references != 0u)
        return;
    for (size_t head = 0u; head < root->head_count; head++)
        head_block_release(realization, root->heads[head]);
    tracked_free(&realization->allocations, root->heads);
    tracked_free(&realization->allocations, root);
}

static void persistent_publish_initial(Realization *realization,
                                       const OracleSnapshot *snapshot) {
    realization->state.persistent.current =
        persistent_root_initial(realization, snapshot);
    realization->work.authority_builds++;
}

static void persistent_publish(Realization *realization,
                               const Mutation *mutation,
                               Revision revision) {
    PersistentRoot *previous = realization->state.persistent.current;
    PersistentRoot *next = persistent_root_derive(
        realization, previous, mutation, revision);
    realization->state.persistent.current = next;
    realization->work.authority_builds++;
    persistent_root_release(realization, previous);
}

static Lease persistent_acquire(Realization *realization) {
    PersistentRoot *root = realization->state.persistent.current;
    assert(root);
    persistent_root_retain(realization, root);
    realization->work.exact_key_reuses++;
    return (Lease){
        .kind = realization->kind,
        .revision = root->revision,
        .object = root,
    };
}

typedef struct {
    Head head;
    size_t coordinate;
} MergeCursor;

static bool merge_cursor_less(Realization *realization,
                              const PersistentRoot *root,
                              MergeCursor left, MergeCursor right) {
    realization->work.merge_comparisons++;
    realization->work.occurrence_reads += 2u;
    Occurrence left_occurrence =
        root->heads[left.head]->rows[left.coordinate];
    Occurrence right_occurrence =
        root->heads[right.head]->rows[right.coordinate];
    return left_occurrence.order < right_occurrence.order;
}

static void merge_heap_push(Realization *realization,
                            const PersistentRoot *root,
                            MergeCursor *heap, size_t *length,
                            MergeCursor cursor) {
    size_t index = (*length)++;
    heap[index] = cursor;
    while (index != 0u) {
        size_t parent = (index - 1u) / 2u;
        if (!merge_cursor_less(realization, root, heap[index], heap[parent]))
            break;
        MergeCursor temporary = heap[index];
        heap[index] = heap[parent];
        heap[parent] = temporary;
        index = parent;
    }
}

static MergeCursor merge_heap_pop(Realization *realization,
                                  const PersistentRoot *root,
                                  MergeCursor *heap, size_t *length) {
    assert(*length != 0u);
    MergeCursor result = heap[0];
    (*length)--;
    if (*length == 0u)
        return result;
    heap[0] = heap[*length];
    size_t index = 0u;
    while (true) {
        size_t left = index * 2u + 1u;
        size_t right = left + 1u;
        if (left >= *length)
            break;
        size_t smallest = left;
        if (right < *length &&
            merge_cursor_less(realization, root, heap[right], heap[left]))
            smallest = right;
        if (!merge_cursor_less(
                realization, root, heap[smallest], heap[index]))
            break;
        MergeCursor temporary = heap[index];
        heap[index] = heap[smallest];
        heap[smallest] = temporary;
        index = smallest;
    }
    return result;
}

static void persistent_query(Realization *realization, const Lease *lease,
                             Head head, QueryResult *result) {
    PersistentRoot *root = lease->object;
    assert(root && root->revision == lease->revision);
    if (head != HEAD_ANY) {
        assert(head < realization->head_count);
        HeadBlock *exact = root->heads[head];
        HeadBlock *wildcard = root->heads[root->head_count - 1u];
        realization->work.index_reads += 2u;
        size_t exact_position = 0u;
        size_t wildcard_position = 0u;
        while (exact_position < exact->row_len ||
               wildcard_position < wildcard->row_len) {
            bool take_exact;
            if (wildcard_position >= wildcard->row_len) {
                take_exact = true;
            } else if (exact_position >= exact->row_len) {
                take_exact = false;
            } else {
                realization->work.occurrence_reads += 2u;
                realization->work.merge_comparisons++;
                take_exact = exact->rows[exact_position].order <
                    wildcard->rows[wildcard_position].order;
            }
            Occurrence occurrence = take_exact
                ? exact->rows[exact_position++]
                : wildcard->rows[wildcard_position++];
            realization->work.occurrence_reads++;
            query_result_push(realization, result, occurrence);
        }
        return;
    }

    MergeCursor *heap = tracked_allocate(&realization->allocations,
        checked_multiply(root->head_count, sizeof(*heap)),
        ALLOCATION_OBSERVER);
    size_t heap_len = 0u;
    for (size_t candidate_head = 0u;
         candidate_head < root->head_count; candidate_head++) {
        realization->work.index_reads++;
        if (root->heads[candidate_head]->row_len == 0u)
            continue;
        merge_heap_push(realization, root, heap, &heap_len,
            (MergeCursor){.head = (Head)candidate_head, .coordinate = 0u});
    }
    while (heap_len != 0u) {
        MergeCursor cursor = merge_heap_pop(
            realization, root, heap, &heap_len);
        HeadBlock *block = root->heads[cursor.head];
        realization->work.occurrence_reads++;
        query_result_push(
            realization, result, block->rows[cursor.coordinate]);
        cursor.coordinate++;
        if (cursor.coordinate < block->row_len)
            merge_heap_push(realization, root, heap, &heap_len, cursor);
    }
    tracked_free(&realization->allocations, heap);
}

static void revision_log_reserve_entries(Realization *realization,
                                         size_t needed) {
    RevisionLog *log = &realization->state.log;
    if (needed <= log->entry_cap)
        return;
    size_t capacity = log->entry_cap ? log->entry_cap : 16u;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2u)
            fail("revision-view tournament: log capacity overflow");
        capacity *= 2u;
    }
    log->entries = tracked_grow(realization, log->entries, log->entry_cap,
        capacity, sizeof(*log->entries), ALLOCATION_CARRIER);
    log->entry_cap = capacity;
}

static void revision_log_reserve_ids(Realization *realization,
                                     size_t needed) {
    RevisionLog *log = &realization->state.log;
    if (needed <= log->id_cap)
        return;
    size_t old_capacity = log->id_cap;
    size_t capacity = old_capacity ? old_capacity : 16u;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2u)
            fail("revision-view tournament: identity map overflow");
        capacity *= 2u;
    }
    log->id_to_index = tracked_grow(realization, log->id_to_index,
        old_capacity, capacity, sizeof(*log->id_to_index),
        ALLOCATION_CARRIER);
    for (size_t i = old_capacity; i < capacity; i++)
        log->id_to_index[i] = SIZE_MAX;
    log->id_cap = capacity;
}

static void revision_log_append(Realization *realization,
                                Occurrence occurrence, Revision born) {
    RevisionLog *log = &realization->state.log;
    if (occurrence.id > (OccurrenceId)(SIZE_MAX - 1u))
        fail("revision-view tournament: identity range exhausted");
    size_t identity = (size_t)occurrence.id;
    revision_log_reserve_entries(realization, log->entry_len + 1u);
    revision_log_reserve_ids(realization, identity + 1u);
    if (identity >= log->id_len)
        log->id_len = identity + 1u;
    assert(log->id_to_index[identity] == SIZE_MAX);
    size_t coordinate = log->entry_len++;
    log->entries[coordinate] = (LogEntry){
        .occurrence = occurrence,
        .born = born,
        .dead = REVISION_LIVE,
    };
    log->id_to_index[identity] = coordinate;
    size_t bucket = head_bucket(realization->head_count, occurrence.head);
    assert(bucket < log->head_count);
    size_vector_push(realization, &log->heads[bucket], coordinate);
    realization->work.occurrence_writes++;
    realization->work.index_writes += 2u;
}

static VersionRoot *version_root_new(Realization *realization,
                                     Revision revision) {
    RevisionLog *log = &realization->state.log;
    VersionRoot *root = tracked_allocate(
        &realization->allocations, sizeof(*root), ALLOCATION_CARRIER);
    root->references = 1u;
    root->revision = revision;
    root->next = log->roots;
    if (log->roots)
        log->roots->previous = root;
    log->roots = root;
    return root;
}

static void version_root_retain(Realization *realization,
                                VersionRoot *root) {
    assert(root && root->references != 0u);
    root->references++;
    realization->work.reference_operations++;
}

static bool log_entry_visible(const LogEntry *entry, Revision revision) {
    return entry->born <= revision &&
        (entry->dead == REVISION_LIVE || revision < entry->dead);
}

static Revision revision_log_minimum_live_revision(
        Realization *realization) {
    RevisionLog *log = &realization->state.log;
    if (!log->roots)
        return log->current_revision;
    Revision minimum = UINT64_MAX;
    for (VersionRoot *root = log->roots; root; root = root->next) {
        realization->work.reclamation_scans++;
        if (root->revision < minimum)
            minimum = root->revision;
    }
    return minimum;
}

static void revision_log_reset_indexes(Realization *realization) {
    RevisionLog *log = &realization->state.log;
    for (size_t head = 0u; head < log->head_count; head++) {
        size_vector_release(realization, &log->heads[head]);
        realization->work.index_writes++;
    }
    for (size_t i = 0u; i < log->id_len; i++) {
        log->id_to_index[i] = SIZE_MAX;
        realization->work.index_writes++;
    }
}

static void revision_log_compact(Realization *realization) {
    RevisionLog *log = &realization->state.log;
    if (!log->roots || log->entry_len == 0u)
        return;
    Revision minimum = revision_log_minimum_live_revision(realization);
    size_t keep_count = 0u;
    size_t reclaim_count = 0u;
    for (size_t i = 0u; i < log->entry_len; i++) {
        realization->work.reclamation_scans++;
        LogEntry entry = log->entries[i];
        if (entry.dead != REVISION_LIVE && entry.dead <= minimum)
            reclaim_count++;
        else
            keep_count++;
    }
    if (reclaim_count == 0u)
        return;

    LogEntry *replacement = tracked_allocate(&realization->allocations,
        checked_multiply(keep_count, sizeof(*replacement)),
        ALLOCATION_CARRIER);
    size_t write = 0u;
    for (size_t i = 0u; i < log->entry_len; i++) {
        realization->work.reclamation_scans++;
        LogEntry entry = log->entries[i];
        if (entry.dead != REVISION_LIVE && entry.dead <= minimum)
            continue;
        replacement[write++] = entry;
        realization->work.occurrence_reads++;
        realization->work.occurrence_writes++;
        realization->work.carrier_copied_bytes += sizeof(entry);
    }
    assert(write == keep_count);
    tracked_free(&realization->allocations, log->entries);
    log->entries = replacement;
    log->entry_len = keep_count;
    log->entry_cap = keep_count;
    revision_log_reset_indexes(realization);
    for (size_t i = 0u; i < log->entry_len; i++) {
        Occurrence occurrence = log->entries[i].occurrence;
        size_t identity = (size_t)occurrence.id;
        assert(identity < log->id_len);
        log->id_to_index[identity] = i;
        size_t bucket = head_bucket(
            realization->head_count, occurrence.head);
        size_vector_push(realization, &log->heads[bucket], i);
        realization->work.occurrence_reads++;
        realization->work.index_writes += 2u;
    }
    uint64_t reclaimed_bytes =
        (uint64_t)reclaim_count * sizeof(LogEntry);
    assert(log->retired_bytes >= reclaimed_bytes);
    log->retired_bytes -= reclaimed_bytes;
}

static void version_root_release(Realization *realization,
                                 VersionRoot *root) {
    if (!root)
        return;
    assert(root->references != 0u);
    root->references--;
    realization->work.reference_operations++;
    if (root->references != 0u)
        return;
    RevisionLog *log = &realization->state.log;
    if (root->previous)
        root->previous->next = root->next;
    else
        log->roots = root->next;
    if (root->next)
        root->next->previous = root->previous;
    tracked_free(&realization->allocations, root);
    revision_log_compact(realization);
}

static void revision_log_publish_initial(
        Realization *realization, const OracleSnapshot *snapshot) {
    RevisionLog *log = &realization->state.log;
    log->head_count = realization->head_count + 1u;
    log->heads = tracked_allocate(&realization->allocations,
        checked_multiply(log->head_count, sizeof(*log->heads)),
        ALLOCATION_CARRIER);
    log->current_revision = snapshot->revision;
    for (size_t i = 0u; i < snapshot->len; i++) {
        realization->work.occurrence_reads++;
        revision_log_append(
            realization, snapshot->items[i], snapshot->revision);
    }
    log->current = version_root_new(realization, snapshot->revision);
    realization->work.authority_builds++;
}

static void revision_log_publish(Realization *realization,
                                 const Mutation *mutation,
                                 Revision revision) {
    RevisionLog *log = &realization->state.log;
    assert(revision == log->current_revision + 1u);
    for (size_t i = 0u; i < mutation->removed_len; i++) {
        OccurrenceId id = mutation->removed[i].id;
        assert(id < log->id_len);
        realization->work.index_reads++;
        size_t coordinate = log->id_to_index[(size_t)id];
        assert(coordinate < log->entry_len);
        LogEntry *entry = &log->entries[coordinate];
        assert(entry->dead == REVISION_LIVE);
        entry->dead = revision;
        realization->work.occurrence_writes++;
        log->retired_bytes += sizeof(*entry);
    }
    for (size_t i = 0u; i < mutation->appended_len; i++)
        revision_log_append(realization, mutation->appended[i], revision);
    log->current_revision = revision;
    VersionRoot *previous = log->current;
    log->current = version_root_new(realization, revision);
    realization->work.authority_builds++;
    if (log->retired_bytes > realization->peak_retired_bytes)
        realization->peak_retired_bytes = log->retired_bytes;
    version_root_release(realization, previous);
}

static Lease revision_log_acquire(Realization *realization) {
    VersionRoot *root = realization->state.log.current;
    assert(root);
    version_root_retain(realization, root);
    realization->work.exact_key_reuses++;
    return (Lease){
        .kind = realization->kind,
        .revision = root->revision,
        .object = root,
    };
}

static void revision_log_query(Realization *realization, const Lease *lease,
                               Head head, QueryResult *result) {
    RevisionLog *log = &realization->state.log;
    VersionRoot *root = lease->object;
    assert(root && root->revision == lease->revision);
    if (head != HEAD_ANY) {
        assert(head < realization->head_count);
        SizeVector *exact = &log->heads[head];
        SizeVector *wildcard = &log->heads[log->head_count - 1u];
        size_t exact_position = 0u;
        size_t wildcard_position = 0u;
        while (exact_position < exact->len ||
               wildcard_position < wildcard->len) {
            size_t exact_coordinate = exact_position < exact->len
                ? exact->items[exact_position]
                : SIZE_MAX;
            size_t wildcard_coordinate = wildcard_position < wildcard->len
                ? wildcard->items[wildcard_position]
                : SIZE_MAX;
            realization->work.index_reads +=
                (exact_coordinate != SIZE_MAX) +
                (wildcard_coordinate != SIZE_MAX);
            size_t coordinate;
            if (wildcard_coordinate == SIZE_MAX) {
                coordinate = exact_coordinate;
                exact_position++;
            } else if (exact_coordinate == SIZE_MAX) {
                coordinate = wildcard_coordinate;
                wildcard_position++;
            } else {
                assert(exact_coordinate < log->entry_len);
                assert(wildcard_coordinate < log->entry_len);
                realization->work.occurrence_reads += 2u;
                realization->work.merge_comparisons++;
                if (log->entries[exact_coordinate].occurrence.order <
                    log->entries[wildcard_coordinate].occurrence.order) {
                    coordinate = exact_coordinate;
                    exact_position++;
                } else {
                    coordinate = wildcard_coordinate;
                    wildcard_position++;
                }
            }
            assert(coordinate < log->entry_len);
            LogEntry *entry = &log->entries[coordinate];
            realization->work.occurrence_reads++;
            realization->work.visibility_tests++;
            if (log_entry_visible(entry, lease->revision))
                query_result_push(
                    realization, result, entry->occurrence);
        }
        return;
    }
    for (size_t i = 0u; i < log->entry_len; i++) {
        LogEntry *entry = &log->entries[i];
        realization->work.occurrence_reads++;
        realization->work.visibility_tests++;
        if (log_entry_visible(entry, lease->revision))
            query_result_push(realization, result, entry->occurrence);
    }
}

static void realization_initialize(Realization *realization,
                                   RealizationKind kind,
                                   const Workload *workload,
                                   const OracleSnapshot *initial) {
    memset(realization, 0, sizeof(*realization));
    realization->kind = kind;
    realization->head_count = workload->head_count;
    switch (kind) {
    case REALIZATION_OWNED_FLAT_SCAN:
    case REALIZATION_OWNED_FLAT_INDEX:
        realization->state.flat.indexed =
            kind == REALIZATION_OWNED_FLAT_INDEX;
        flat_publish(realization, initial);
        break;
    case REALIZATION_PERSISTENT_HEAD_BLOCKS:
        persistent_publish_initial(realization, initial);
        break;
    case REALIZATION_REVISION_LOG:
        revision_log_publish_initial(realization, initial);
        break;
    default:
        fail("revision-view tournament: unknown realization");
    }
}

static void realization_publish(Realization *realization,
                                const OracleSnapshot *snapshot,
                                const Mutation *mutation) {
    switch (realization->kind) {
    case REALIZATION_OWNED_FLAT_SCAN:
    case REALIZATION_OWNED_FLAT_INDEX:
        flat_publish(realization, snapshot);
        break;
    case REALIZATION_PERSISTENT_HEAD_BLOCKS:
        persistent_publish(realization, mutation, snapshot->revision);
        break;
    case REALIZATION_REVISION_LOG:
        revision_log_publish(realization, mutation, snapshot->revision);
        break;
    default:
        fail("revision-view tournament: unknown realization");
    }
}

static Lease realization_acquire(Realization *realization) {
    switch (realization->kind) {
    case REALIZATION_OWNED_FLAT_SCAN:
    case REALIZATION_OWNED_FLAT_INDEX:
        return flat_acquire(realization);
    case REALIZATION_PERSISTENT_HEAD_BLOCKS:
        return persistent_acquire(realization);
    case REALIZATION_REVISION_LOG:
        return revision_log_acquire(realization);
    default:
        fail("revision-view tournament: unknown realization");
        return (Lease){0};
    }
}

static void realization_release(Realization *realization, Lease *lease) {
    assert(lease->kind == realization->kind);
    switch (realization->kind) {
    case REALIZATION_OWNED_FLAT_SCAN:
    case REALIZATION_OWNED_FLAT_INDEX:
        flat_root_release(realization, lease->object);
        break;
    case REALIZATION_PERSISTENT_HEAD_BLOCKS:
        persistent_root_release(realization, lease->object);
        break;
    case REALIZATION_REVISION_LOG:
        version_root_release(realization, lease->object);
        break;
    default:
        fail("revision-view tournament: unknown realization");
    }
    memset(lease, 0, sizeof(*lease));
}

static void realization_query(Realization *realization, const Lease *lease,
                              Head head, QueryResult *result) {
    assert(lease->kind == realization->kind);
    realization->work.queries++;
    switch (realization->kind) {
    case REALIZATION_OWNED_FLAT_SCAN:
    case REALIZATION_OWNED_FLAT_INDEX:
        flat_query(realization, lease, head, result);
        break;
    case REALIZATION_PERSISTENT_HEAD_BLOCKS:
        persistent_query(realization, lease, head, result);
        break;
    case REALIZATION_REVISION_LOG:
        revision_log_query(realization, lease, head, result);
        break;
    default:
        fail("revision-view tournament: unknown realization");
    }
}

static void realization_destroy(Realization *realization) {
    switch (realization->kind) {
    case REALIZATION_OWNED_FLAT_SCAN:
    case REALIZATION_OWNED_FLAT_INDEX:
        flat_root_release(realization, realization->state.flat.current);
        realization->state.flat.current = NULL;
        break;
    case REALIZATION_PERSISTENT_HEAD_BLOCKS:
        persistent_root_release(
            realization, realization->state.persistent.current);
        realization->state.persistent.current = NULL;
        break;
    case REALIZATION_REVISION_LOG: {
        RevisionLog *log = &realization->state.log;
        version_root_release(realization, log->current);
        log->current = NULL;
        assert(log->roots == NULL);
        for (size_t head = 0u; head < log->head_count; head++)
            size_vector_release(realization, &log->heads[head]);
        tracked_free(&realization->allocations, log->heads);
        tracked_free(&realization->allocations, log->id_to_index);
        tracked_free(&realization->allocations, log->entries);
        memset(log, 0, sizeof(*log));
        break;
    }
    default:
        fail("revision-view tournament: unknown realization");
    }
    if (realization->allocations.live_bytes != 0u ||
        realization->allocations.carrier_live_bytes != 0u ||
        realization->allocations.observer_live_bytes != 0u ||
        realization->allocations.allocated_bytes !=
            realization->allocations.freed_bytes ||
        realization->allocations.carrier_allocated_bytes !=
            realization->allocations.carrier_freed_bytes ||
        realization->allocations.observer_allocated_bytes !=
            realization->allocations.observer_freed_bytes ||
        realization->allocations.allocation_calls !=
            realization->allocations.carrier_allocation_calls +
            realization->allocations.observer_allocation_calls ||
        realization->allocations.allocated_bytes !=
            realization->allocations.carrier_allocated_bytes +
            realization->allocations.observer_allocated_bytes) {
        fail("revision-view tournament: tracked allocation imbalance");
    }
}

static Head query_head_for(const Workload *workload, size_t cycle,
                           size_t held_index, size_t query) {
    switch (workload->query_pattern) {
    case QUERY_HOT_HEAD:
        return 0u;
    case QUERY_UNRELATED_HEAD:
        return workload->head_count > 1u ? 1u : 0u;
    case QUERY_BROAD:
        return HEAD_ANY;
    case QUERY_MIXED: {
        size_t query_head_count = workload->query_head_count != 0u
            ? workload->query_head_count
            : workload->head_count;
        return (Head)(mix64(
            (uint64_t)cycle * UINT64_C(0x9e3779b97f4a7c15) +
            (uint64_t)held_index * UINT64_C(0xbf58476d1ce4e5b9) +
            (uint64_t)query) % query_head_count);
    }
    default:
        fail("revision-view tournament: unknown query pattern");
        return 0u;
    }
}

static uint64_t query_digest(const QueryResult *result) {
    uint64_t digest = UINT64_C(0x6a09e667f3bcc909);
    for (size_t i = 0u; i < result->len; i++)
        digest = digest_occurrence(digest, result->items[i]);
    return mix64(digest ^ (uint64_t)result->len);
}

static void assert_query_equal(const char *workload_name,
                               Realization *realization,
                               const OracleSnapshot *oracle,
                               const Lease *lease, Head head) {
    size_t expected_len = 0u;
    Occurrence *expected = oracle_query(oracle, head, &expected_len);
    QueryResult actual = {0};
    uint64_t start = monotonic_ns();
    realization_query(realization, lease, head, &actual);
    realization->elapsed_ns += monotonic_ns() - start;
    if (actual.len != expected_len) {
        fprintf(stderr,
            "revision-view tournament: %s/%s length mismatch "
            "at revision=%" PRIu64 " head=%" PRIu32
            " expected=%zu actual=%zu\n",
            workload_name, realization_name(realization->kind),
            oracle->revision, head, expected_len, actual.len);
        exit(EXIT_FAILURE);
    }
    for (size_t i = 0u; i < expected_len; i++) {
        if (!occurrence_equal(expected[i], actual.items[i])) {
            fprintf(stderr,
                "revision-view tournament: %s/%s occurrence mismatch "
                "at revision=%" PRIu64 " head=%" PRIu32
                " coordinate=%zu\n",
                workload_name, realization_name(realization->kind),
                oracle->revision, head, i);
            exit(EXIT_FAILURE);
        }
    }
    uint64_t digest = query_digest(&actual);
    realization->digest = mix64(realization->digest ^ digest ^
        (uint64_t)head ^ oracle->revision);
    benchmark_sink ^= digest + (uint64_t)realization->kind + 1u;
    query_result_release(realization, &actual);
    free(expected);
}

static void held_revision_release(HeldRevision *held,
                                  Realization realizations[REALIZATION_COUNT]) {
    if (!held->occupied)
        return;
    for (size_t kind = 0u; kind < REALIZATION_COUNT; kind++) {
        uint64_t start = monotonic_ns();
        realization_release(&realizations[kind], &held->leases[kind]);
        realizations[kind].elapsed_ns += monotonic_ns() - start;
    }
    oracle_free(&held->oracle);
    held->occupied = false;
}

static void held_revision_acquire(
        HeldRevision *held, const OracleSnapshot *oracle,
        const Workload *workload,
        Realization realizations[REALIZATION_COUNT]) {
    assert(!held->occupied);
    held->oracle = oracle_clone(oracle);
    held->occupied = true;
    for (size_t kind = 0u; kind < REALIZATION_COUNT; kind++) {
        Realization *realization = &realizations[kind];
        uint64_t start = monotonic_ns();
        Lease primary = realization_acquire(realization);
        realization->elapsed_ns += monotonic_ns() - start;
        assert(primary.revision == oracle->revision);
        held->leases[kind] = primary;
        for (size_t acquisition = 1u;
             acquisition < workload->acquisitions_per_cycle;
             acquisition++) {
            start = monotonic_ns();
            Lease repeated = realization_acquire(realization);
            realization->elapsed_ns += monotonic_ns() - start;
            assert(repeated.revision == primary.revision);
            assert(repeated.object == primary.object);
            start = monotonic_ns();
            realization_release(realization, &repeated);
            realization->elapsed_ns += monotonic_ns() - start;
        }
    }
}

static void query_all_held(const Workload *workload, size_t cycle,
                           HeldRevision *held,
                           Realization realizations[REALIZATION_COUNT]) {
    for (size_t held_index = 0u;
         held_index < workload->lease_window; held_index++) {
        if (!held[held_index].occupied)
            continue;
        for (size_t query = 0u;
             query < workload->queries_per_lease; query++) {
            Head head = query_head_for(
                workload, cycle, held_index, query);
            size_t first = (cycle + held_index + query) % REALIZATION_COUNT;
            for (size_t offset = 0u; offset < REALIZATION_COUNT; offset++) {
                size_t kind = (first + offset) % REALIZATION_COUNT;
                assert_query_equal(workload->name, &realizations[kind],
                    &held[held_index].oracle,
                    &held[held_index].leases[kind], head);
            }
        }
    }
}

static bool work_equal(const Work *left, const Work *right) {
    return left->authority_builds == right->authority_builds &&
        left->exact_key_reuses == right->exact_key_reuses &&
        left->occurrence_reads == right->occurrence_reads &&
        left->occurrence_writes == right->occurrence_writes &&
        left->carrier_copied_bytes == right->carrier_copied_bytes &&
        left->observer_copied_bytes == right->observer_copied_bytes &&
        left->index_reads == right->index_reads &&
        left->index_writes == right->index_writes &&
        left->mutation_membership_tests ==
            right->mutation_membership_tests &&
        left->visibility_tests == right->visibility_tests &&
        left->merge_comparisons == right->merge_comparisons &&
        left->reference_operations == right->reference_operations &&
        left->reclamation_scans == right->reclamation_scans &&
        left->queries == right->queries &&
        left->emitted_candidates == right->emitted_candidates;
}

static bool allocations_equal(const AllocationTracker *left,
                              const AllocationTracker *right) {
    return left->allocation_calls == right->allocation_calls &&
        left->allocated_bytes == right->allocated_bytes &&
        left->freed_bytes == right->freed_bytes &&
        left->live_bytes == right->live_bytes &&
        left->peak_live_bytes == right->peak_live_bytes &&
        left->carrier_allocation_calls == right->carrier_allocation_calls &&
        left->carrier_allocated_bytes == right->carrier_allocated_bytes &&
        left->carrier_freed_bytes == right->carrier_freed_bytes &&
        left->carrier_live_bytes == right->carrier_live_bytes &&
        left->carrier_peak_live_bytes == right->carrier_peak_live_bytes &&
        left->observer_allocation_calls == right->observer_allocation_calls &&
        left->observer_allocated_bytes == right->observer_allocated_bytes &&
        left->observer_freed_bytes == right->observer_freed_bytes &&
        left->observer_live_bytes == right->observer_live_bytes &&
        left->observer_peak_live_bytes == right->observer_peak_live_bytes;
}

static void run_workload_once(const Workload *workload,
                              Result results[REALIZATION_COUNT]) {
    assert(workload->head_count != 0u);
    assert(workload->query_head_count == 0u ||
           workload->query_head_count <= workload->head_count);
    assert(workload->acquisitions_per_cycle != 0u);
    assert(workload->lease_window != 0u);
    OracleSnapshot current = oracle_initial(workload);
    OccurrenceId next_id = (OccurrenceId)current.len;
    Realization realizations[REALIZATION_COUNT];
    for (size_t kind = 0u; kind < REALIZATION_COUNT; kind++)
        realization_initialize(&realizations[kind], (RealizationKind)kind,
            workload, &current);
    HeldRevision *held = system_allocate(
        workload->lease_window, sizeof(*held));

    for (size_t cycle = 0u; cycle <= workload->cycles; cycle++) {
        size_t slot = cycle % workload->lease_window;
        held_revision_release(&held[slot], realizations);
        held_revision_acquire(
            &held[slot], &current, workload, realizations);
        query_all_held(workload, cycle, held, realizations);
        if (cycle == workload->cycles)
            break;
        bool equation_mutation = workload->removals_per_cycle != 0u ||
            workload->appends_per_cycle != 0u;
        bool unchanged_republish =
            workload->unchanged_republish_every != 0u &&
            cycle % workload->unchanged_republish_every == 0u;
        if (!equation_mutation && !unchanged_republish)
            continue;

        Mutation mutation = equation_mutation
            ? mutation_generate(workload, &current, next_id, cycle)
            : (Mutation){
                .remove_mask = system_allocate(current.len, 1u),
            };
        OracleSnapshot next = oracle_apply(&current, &mutation);
        size_t first = cycle % REALIZATION_COUNT;
        for (size_t offset = 0u; offset < REALIZATION_COUNT; offset++) {
            size_t kind = (first + offset) % REALIZATION_COUNT;
            uint64_t start = monotonic_ns();
            realization_publish(
                &realizations[kind], &next, &mutation);
            realizations[kind].elapsed_ns += monotonic_ns() - start;
        }
        next_id += (OccurrenceId)mutation.appended_len;
        oracle_free(&current);
        current = next;
        mutation_free(&mutation);
    }

    for (size_t slot = 0u; slot < workload->lease_window; slot++)
        held_revision_release(&held[slot], realizations);
    free(held);
    oracle_free(&current);

    uint64_t expected_digest = realizations[0].digest;
    for (size_t kind = 0u; kind < REALIZATION_COUNT; kind++) {
        if (realizations[kind].digest != expected_digest)
            fail("revision-view tournament: semantic digest mismatch");
        realization_destroy(&realizations[kind]);
        results[kind] = (Result){
            .work = realizations[kind].work,
            .allocations = realizations[kind].allocations,
            .elapsed_ns = realizations[kind].elapsed_ns,
            .digest = realizations[kind].digest,
            .peak_retired_bytes = realizations[kind].peak_retired_bytes,
        };
    }
}

static int compare_u64(const void *left, const void *right) {
    uint64_t a = *(const uint64_t *)left;
    uint64_t b = *(const uint64_t *)right;
    return (a > b) - (a < b);
}

static uint64_t median_u64(const uint64_t *values, size_t count) {
    uint64_t *copy = system_allocate(count, sizeof(*copy));
    memcpy(copy, values, count * sizeof(*copy));
    qsort(copy, count, sizeof(*copy), compare_u64);
    uint64_t median = copy[count / 2u];
    free(copy);
    return median;
}

static void print_result(const Workload *workload, RealizationKind kind,
                         const Result *result, uint64_t median_ns) {
    const Work *work = &result->work;
    const AllocationTracker *allocations = &result->allocations;
    printf("workload=%s realization=%s layout=%s index=%s lifetime=%s\n",
        workload->name, realization_name(kind), realization_layout(kind),
        realization_index(kind), realization_lifetime(kind));
    printf("  work builds=%" PRIu64 " reuses=%" PRIu64
           " occurrence_reads=%" PRIu64 " occurrence_writes=%" PRIu64
           " carrier_copied_bytes=%" PRIu64
           " observer_copied_bytes=%" PRIu64
           " index_reads=%" PRIu64
           " index_writes=%" PRIu64 " membership_tests=%" PRIu64
           " visibility_tests=%" PRIu64 " merge_comparisons=%" PRIu64
           " reference_ops=%" PRIu64 " reclamation_scans=%" PRIu64
           " queries=%" PRIu64 " emitted=%" PRIu64 "\n",
        work->authority_builds, work->exact_key_reuses,
        work->occurrence_reads, work->occurrence_writes,
        work->carrier_copied_bytes, work->observer_copied_bytes,
        work->index_reads, work->index_writes,
        work->mutation_membership_tests, work->visibility_tests,
        work->merge_comparisons, work->reference_operations,
        work->reclamation_scans, work->queries,
        work->emitted_candidates);
    printf("  storage allocation_calls=%" PRIu64
           " allocated_bytes=%" PRIu64 " freed_bytes=%" PRIu64
           " peak_live_bytes=%" PRIu64 " peak_retired_bytes=%" PRIu64
           "\n",
        allocations->allocation_calls, allocations->allocated_bytes,
        allocations->freed_bytes, allocations->peak_live_bytes,
        result->peak_retired_bytes);
    printf("  allocation_roles carrier_calls=%" PRIu64
           " carrier_bytes=%" PRIu64 " carrier_freed=%" PRIu64
           " carrier_peak_live=%" PRIu64
           " observer_calls=%" PRIu64 " observer_bytes=%" PRIu64
           " observer_freed=%" PRIu64
           " observer_peak_live=%" PRIu64 "\n",
        allocations->carrier_allocation_calls,
        allocations->carrier_allocated_bytes,
        allocations->carrier_freed_bytes,
        allocations->carrier_peak_live_bytes,
        allocations->observer_allocation_calls,
        allocations->observer_allocated_bytes,
        allocations->observer_freed_bytes,
        allocations->observer_peak_live_bytes);
    printf("  timing median_ms=%.3f digest=%016" PRIx64 "\n",
        (double)median_ns / 1000000.0, result->digest);
}

static void run_negative_canaries(void) {
    Occurrence duplicate_payloads[2] = {
        {.id = 10u, .payload = 7u, .order = 10u, .head = 0u},
        {.id = 11u, .payload = 7u, .order = 11u, .head = 0u},
    };
    assert(duplicate_payloads[0].payload == duplicate_payloads[1].payload);
    assert(duplicate_payloads[0].id != duplicate_payloads[1].id);

    LogEntry retired = {
        .occurrence = duplicate_payloads[0],
        .born = 0u,
        .dead = 1u,
    };
    assert(log_entry_visible(&retired, 0u));
    assert(!log_entry_visible(&retired, 1u));
    puts("canary duplicate-payload-distinct-identity=PASS");
    puts("canary old-lease-survives-later-retirement=PASS");
}

int main(int argc, char **argv) {
    size_t trial_count = 5u;
    if (argc > 2)
        fail("usage: bench_realizations [odd-trial-count]");
    if (argc == 2) {
        char *end = NULL;
        unsigned long parsed = strtoul(argv[1], &end, 10);
        if (!end || *end != '\0' || parsed == 0u || parsed > 31u ||
            parsed % 2u == 0u)
            fail("revision-view tournament: trial count must be odd, 1..31");
        trial_count = (size_t)parsed;
    }

    const Workload workloads[] = {
        {
            .name = "static-query-heavy",
            .initial_occurrences = 4096u,
            .head_count = 128u,
            .wildcard_stride = 31u,
            .cycles = 12u,
            .removals_per_cycle = 0u,
            .appends_per_cycle = 0u,
            .acquisitions_per_cycle = 4u,
            .queries_per_lease = 48u,
            .lease_window = 2u,
            .payload_modulus = 0u,
            .mutation_pattern = MUTATION_SCATTERED,
            .query_pattern = QUERY_MIXED,
        },
        {
            .name = "small-attention-churn",
            .initial_occurrences = 256u,
            .head_count = 32u,
            .wildcard_stride = 31u,
            .cycles = 64u,
            .removals_per_cycle = 8u,
            .appends_per_cycle = 8u,
            .acquisitions_per_cycle = 3u,
            .queries_per_lease = 12u,
            .lease_window = 4u,
            .payload_modulus = 16u,
            .mutation_pattern = MUTATION_SCATTERED,
            .query_pattern = QUERY_MIXED,
        },
        {
            .name = "medium-attention-churn",
            .initial_occurrences = 4096u,
            .head_count = 128u,
            .wildcard_stride = 31u,
            .cycles = 32u,
            .removals_per_cycle = 64u,
            .appends_per_cycle = 64u,
            .acquisitions_per_cycle = 2u,
            .queries_per_lease = 12u,
            .lease_window = 4u,
            .payload_modulus = 64u,
            .mutation_pattern = MUTATION_SCATTERED,
            .query_pattern = QUERY_MIXED,
        },
        {
            .name = "mostly-static-chaining",
            .initial_occurrences = 16384u,
            .head_count = 512u,
            .wildcard_stride = 31u,
            .cycles = 4u,
            .removals_per_cycle = 4u,
            .appends_per_cycle = 4u,
            .acquisitions_per_cycle = 3u,
            .queries_per_lease = 96u,
            .lease_window = 2u,
            .payload_modulus = 0u,
            .mutation_pattern = MUTATION_SCATTERED,
            .query_pattern = QUERY_MIXED,
        },
        {
            .name = "hot-head-churn",
            .initial_occurrences = 4096u,
            .head_count = 64u,
            .wildcard_stride = 31u,
            .cycles = 32u,
            .removals_per_cycle = 16u,
            .appends_per_cycle = 16u,
            .acquisitions_per_cycle = 2u,
            .queries_per_lease = 24u,
            .lease_window = 4u,
            .payload_modulus = 32u,
            .mutation_pattern = MUTATION_HOT_HEAD,
            .query_pattern = QUERY_HOT_HEAD,
        },
        {
            .name = "unrelated-head-churn",
            .initial_occurrences = 4096u,
            .head_count = 64u,
            .wildcard_stride = 31u,
            .cycles = 32u,
            .removals_per_cycle = 16u,
            .appends_per_cycle = 16u,
            .acquisitions_per_cycle = 2u,
            .queries_per_lease = 24u,
            .lease_window = 4u,
            .payload_modulus = 32u,
            .mutation_pattern = MUTATION_UNRELATED_HEAD,
            .query_pattern = QUERY_UNRELATED_HEAD,
        },
        {
            .name = "long-held-old-leases",
            .initial_occurrences = 4096u,
            .head_count = 128u,
            .wildcard_stride = 31u,
            .cycles = 24u,
            .removals_per_cycle = 64u,
            .appends_per_cycle = 64u,
            .acquisitions_per_cycle = 2u,
            .queries_per_lease = 4u,
            .lease_window = 12u,
            .payload_modulus = 64u,
            .mutation_pattern = MUTATION_SCATTERED,
            .query_pattern = QUERY_MIXED,
        },
        {
            .name = "append-bursts",
            .initial_occurrences = 512u,
            .head_count = 64u,
            .wildcard_stride = 31u,
            .cycles = 24u,
            .removals_per_cycle = 0u,
            .appends_per_cycle = 64u,
            .acquisitions_per_cycle = 2u,
            .queries_per_lease = 12u,
            .lease_window = 4u,
            .payload_modulus = 32u,
            .mutation_pattern = MUTATION_APPEND_ONLY,
            .query_pattern = QUERY_MIXED,
        },
        {
            .name = "broad-observation",
            .initial_occurrences = 4096u,
            .head_count = 128u,
            .wildcard_stride = 31u,
            .cycles = 8u,
            .removals_per_cycle = 32u,
            .appends_per_cycle = 32u,
            .acquisitions_per_cycle = 2u,
            .queries_per_lease = 3u,
            .lease_window = 4u,
            .payload_modulus = 0u,
            .mutation_pattern = MUTATION_SCATTERED,
            .query_pattern = QUERY_BROAD,
        },
        {
            .name = "duplicate-payload-identities",
            .initial_occurrences = 1024u,
            .head_count = 16u,
            .wildcard_stride = 31u,
            .cycles = 24u,
            .removals_per_cycle = 16u,
            .appends_per_cycle = 16u,
            .acquisitions_per_cycle = 2u,
            .queries_per_lease = 12u,
            .lease_window = 6u,
            .payload_modulus = 4u,
            .mutation_pattern = MUTATION_SCATTERED,
            .query_pattern = QUERY_MIXED,
        },
        {
            .name = "ecan-afrent-observed",
            .initial_occurrences = 152u,
            .head_count = 142u,
            .query_head_count = 98u,
            .cycles = 0u,
            .removals_per_cycle = 0u,
            .appends_per_cycle = 0u,
            .acquisitions_per_cycle = 1u,
            .queries_per_lease = 98u,
            .lease_window = 1u,
            .payload_modulus = 0u,
            .mutation_pattern = MUTATION_SCATTERED,
            .query_pattern = QUERY_MIXED,
        },
        {
            .name = "ecan-dynamicity-source-revision-key",
            .initial_occurrences = 242u,
            .head_count = 235u,
            .query_head_count = 89u,
            .cycles = 315u,
            .removals_per_cycle = 0u,
            .appends_per_cycle = 0u,
            .unchanged_republish_every = 53u,
            .acquisitions_per_cycle = 1u,
            .queries_per_lease = 620u,
            .lease_window = 1u,
            .payload_modulus = 0u,
            .mutation_pattern = MUTATION_SCATTERED,
            .query_pattern = QUERY_MIXED,
        },
        {
            .name = "ecan-dynamicity-equation-projection-key",
            .initial_occurrences = 242u,
            .head_count = 235u,
            .query_head_count = 89u,
            .cycles = 315u,
            .removals_per_cycle = 0u,
            .appends_per_cycle = 0u,
            .acquisitions_per_cycle = 1u,
            .queries_per_lease = 620u,
            .lease_window = 1u,
            .payload_modulus = 0u,
            .mutation_pattern = MUTATION_SCATTERED,
            .query_pattern = QUERY_MIXED,
        },
    };

    run_negative_canaries();
    printf("trials=%zu workloads=%zu\n", trial_count,
        sizeof(workloads) / sizeof(workloads[0]));
    for (size_t workload_index = 0u;
         workload_index < sizeof(workloads) / sizeof(workloads[0]);
         workload_index++) {
        const Workload *workload = &workloads[workload_index];
        Result *trial_results = system_allocate(
            trial_count * REALIZATION_COUNT, sizeof(*trial_results));
        for (size_t trial = 0u; trial < trial_count; trial++)
            run_workload_once(workload,
                &trial_results[trial * REALIZATION_COUNT]);
        for (size_t kind = 0u; kind < REALIZATION_COUNT; kind++) {
            const Result *reference = &trial_results[kind];
            uint64_t times[31];
            for (size_t trial = 0u; trial < trial_count; trial++) {
                const Result *candidate =
                    &trial_results[trial * REALIZATION_COUNT + kind];
                if (!work_equal(&reference->work, &candidate->work) ||
                    !allocations_equal(
                        &reference->allocations, &candidate->allocations) ||
                    reference->digest != candidate->digest ||
                    reference->peak_retired_bytes !=
                        candidate->peak_retired_bytes) {
                    fail("revision-view tournament: nondeterministic receipt");
                }
                times[trial] = candidate->elapsed_ns;
            }
            print_result(workload, (RealizationKind)kind, reference,
                median_u64(times, trial_count));
        }
        free(trial_results);
    }
    printf("revision-view realization tournament: PASS sink=%" PRIu64 "\n",
        benchmark_sink);
    return EXIT_SUCCESS;
}
