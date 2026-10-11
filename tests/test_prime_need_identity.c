#define _POSIX_C_SOURCE 200809L
/* Need identity namespaces never wrap.
 *
 * This harness compiles src/prime_need.c with
 * CETTA_PRIME_NEED_IDENTITY_TEST_HOOKS, which lets it place a namespace's
 * next identity near its end.  It checks the shared helper, every Need
 * namespace at its last identity, eight workers exhausting a namespace
 * together, the reserve-through edges, and the runtime functions that draw
 * identities: each refuses without leaving a partial object behind and
 * without changing what its caller holds. */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "atom.h"
#include "identity_counter.h"
#include "prime_need.h"
#include "symbol.h"

#if !CETTA_PRIME_NEED_IDENTITY_TEST_HOOKS
#error "build with -DCETTA_PRIME_NEED_IDENTITY_TEST_HOOKS=1"
#endif

static unsigned checks = 0u;
static unsigned failures = 0u;

#define CHECK(condition, label)                                                \
    do {                                                                       \
        checks++;                                                              \
        if (!(condition)) {                                                    \
            fprintf(stderr, "FAIL: %s\n", (label));                          \
            failures++;                                                       \
        }                                                                      \
    } while (0)

static _Atomic unsigned g_refusals = 0u;

static void count_refusal(void) {
    atomic_fetch_add_explicit(&g_refusals, 1u, memory_order_relaxed);
}

static unsigned refusals(void) {
    return atomic_load_explicit(&g_refusals, memory_order_relaxed);
}

static bool snapshot_same(const PrimeNeedSnapshot *left,
                          const PrimeNeedSnapshot *right) {
    return left->top == right->top &&
           left->session_id == right->session_id &&
           left->max_storage_key == right->max_storage_key &&
           left->owner == right->owner &&
           left->closure_owner == right->closure_owner
#if CETTA_PRIME_NEED_HEAP_INDEX
           && left->heap_index == right->heap_index &&
           left->lineage_index == right->lineage_index
#endif
        ;
}

static bool branch_same(const PrimeNeedBranchState *left,
                        const PrimeNeedBranchState *right) {
    return left->top == right->top &&
           left->session_id == right->session_id &&
           left->owner == right->owner;
}

/* A value no runtime path writes, so an untouched output is recognisable. */
static void snapshot_poison(PrimeNeedSnapshot *snapshot) {
    memset(snapshot, 0xA5, sizeof(*snapshot));
}

static bool snapshot_poisoned(const PrimeNeedSnapshot *snapshot) {
    PrimeNeedSnapshot poisoned;
    snapshot_poison(&poisoned);
    return memcmp(snapshot, &poisoned, sizeof(poisoned)) == 0;
}

static void branch_poison(PrimeNeedBranchState *state) {
    memset(state, 0xA5, sizeof(*state));
}

static bool branch_poisoned(const PrimeNeedBranchState *state) {
    PrimeNeedBranchState poisoned;
    branch_poison(&poisoned);
    return memcmp(state, &poisoned, sizeof(poisoned)) == 0;
}

#define POISON_ID UINT64_C(0xA5A5A5A5A5A5A5A5)

/* ---- The helper itself ------------------------------------------------- */

static void check_helper(void) {
    _Atomic uint64_t counter = 1u;
    CHECK(cetta_identity_try_take(&counter) == 1u &&
              cetta_identity_try_take(&counter) == 2u &&
              atomic_load(&counter) == 3u,
          "helper issues consecutive identities from one");
    atomic_store(&counter, UINT64_MAX - 1u);
    CHECK(cetta_identity_try_take(&counter) == UINT64_MAX - 1u,
          "helper issues UINT64_MAX-1 as its last identity");
    CHECK(atomic_load(&counter) == UINT64_MAX,
          "issuing the last identity leaves the exhausted state");
    CHECK(cetta_identity_try_take(&counter) == 0u &&
              atomic_load(&counter) == UINT64_MAX,
          "exhausted helper refuses and stays exhausted");
    CHECK(cetta_identity_try_take(&counter) == 0u &&
              atomic_load(&counter) == UINT64_MAX,
          "exhausted helper refuses again without wrapping");
    atomic_store(&counter, 0u);
    CHECK(cetta_identity_try_take(&counter) == 0u &&
              atomic_load(&counter) == 0u,
          "helper never issues zero and leaves a zero counter unchanged");
}

/* ---- Every Need namespace at its end ------------------------------------ */

static void check_namespace_end(PrimeNeedIdentityNamespace ns) {
    char label[256];
    const char *name = prime_need_identity_test_name(ns);
    uint64_t last = prime_need_identity_test_last(ns);
    unsigned before = refusals();

    CHECK(prime_need_identity_test_seed(ns, last - 1u), name);
    snprintf(label, sizeof(label), "%s issues its second-to-last identity",
             name);
    CHECK(prime_need_identity_test_take(ns) == last - 1u, label);
    snprintf(label, sizeof(label), "%s issues its last identity %s", name,
             last == UINT64_MAX - 1u ? "UINT64_MAX-1" : "below the named half");
    CHECK(prime_need_identity_test_take(ns) == last, label);
    snprintf(label, sizeof(label), "%s refuses after its last identity",
             name);
    CHECK(prime_need_identity_test_take(ns) == 0u &&
              prime_need_identity_test_next(ns) == UINT64_MAX,
          label);
    snprintf(label, sizeof(label), "%s stays exhausted", name);
    CHECK(prime_need_identity_test_take(ns) == 0u &&
              prime_need_identity_test_take(ns) == 0u &&
              prime_need_identity_test_next(ns) == UINT64_MAX,
          label);
    snprintf(label, sizeof(label), "%s reports each refusal once", name);
    CHECK(refusals() == before + 3u, label);
    snprintf(label, sizeof(label), "%s cannot be seeded with zero", name);
    CHECK(!prime_need_identity_test_seed(ns, 0u) &&
              prime_need_identity_test_next(ns) == UINT64_MAX,
          label);
    CHECK(prime_need_identity_test_seed(ns, 1u), name);
}

static void check_thunk_named_half(void) {
    uint64_t last = prime_need_identity_test_last(PRIME_NEED_IDENTITY_THUNK);
    CHECK(last == PRIME_NEED_NAMED_CELL_BIT - 1u,
          "fresh thunk identities end below the named half");
    unsigned before = refusals();
    CHECK(prime_need_identity_test_seed(
              PRIME_NEED_IDENTITY_THUNK, PRIME_NEED_NAMED_CELL_BIT),
          "thunk counter seeded at the named half");
    CHECK(prime_need_identity_test_take(PRIME_NEED_IDENTITY_THUNK) == 0u &&
              prime_need_identity_test_next(PRIME_NEED_IDENTITY_THUNK) ==
                  UINT64_MAX,
          "a thunk identity in the named half is refused and exhausts");
    CHECK(prime_need_identity_test_seed(
              PRIME_NEED_IDENTITY_THUNK, UINT64_MAX - 1u) &&
              prime_need_identity_test_take(PRIME_NEED_IDENTITY_THUNK) == 0u &&
              prime_need_identity_test_next(PRIME_NEED_IDENTITY_THUNK) ==
                  UINT64_MAX,
          "UINT64_MAX-1 is never a fresh thunk identity");
    CHECK(refusals() == before + 2u,
          "named-half thunk refusals are each reported once");
    CHECK(prime_need_identity_test_seed(PRIME_NEED_IDENTITY_THUNK, 1u),
          "thunk counter reset");
}

/* ---- Eight workers exhaust a namespace together ------------------------ */

#define WORKERS 8u
#define WINDOW 20000u

typedef struct {
    pthread_barrier_t *start;
    PrimeNeedIdentityNamespace ns;
    _Atomic uint64_t *raw;
    uint64_t *issued;
    size_t issued_len;
    size_t issued_cap;
    unsigned refused;
} TakeWorker;

static void *take_until_refused(void *raw_worker) {
    TakeWorker *worker = raw_worker;
    pthread_barrier_wait(worker->start);
    /* Keep drawing well past exhaustion: no draw may wrap. */
    while (worker->refused < 64u) {
        uint64_t id = worker->raw
            ? cetta_identity_try_take(worker->raw)
            : prime_need_identity_test_take(worker->ns);
        if (id == 0u) {
            worker->refused++;
            continue;
        }
        if (worker->issued_len == worker->issued_cap)
            return NULL;
        worker->issued[worker->issued_len++] = id;
    }
    return NULL;
}

static int compare_u64(const void *left, const void *right) {
    uint64_t l = *(const uint64_t *)left;
    uint64_t r = *(const uint64_t *)right;
    return l < r ? -1 : (l > r ? 1 : 0);
}

/* Run WORKERS threads over the last WINDOW identities of a namespace (or of
 * a raw helper counter when `raw` is given). */
static void check_concurrent_exhaustion(const char *name,
                                        PrimeNeedIdentityNamespace ns,
                                        _Atomic uint64_t *raw,
                                        uint64_t last) {
    uint64_t first = last - WINDOW + 1u;
    if (raw)
        atomic_store(raw, first);
    else
        CHECK(prime_need_identity_test_seed(ns, first), name);
    pthread_barrier_t start;
    pthread_barrier_init(&start, NULL, WORKERS);
    TakeWorker workers[WORKERS];
    pthread_t threads[WORKERS];
    for (unsigned i = 0u; i < WORKERS; i++) {
        workers[i] = (TakeWorker){
            .start = &start,
            .ns = ns,
            .raw = raw,
            .issued = calloc(WINDOW, sizeof(uint64_t)),
            .issued_cap = WINDOW,
        };
        pthread_create(&threads[i], NULL, take_until_refused, &workers[i]);
    }
    uint64_t *all = calloc((size_t)WORKERS * WINDOW, sizeof(uint64_t));
    size_t total = 0u;
    bool every_worker_refused = true;
    for (unsigned i = 0u; i < WORKERS; i++) {
        pthread_join(threads[i], NULL);
        every_worker_refused =
            every_worker_refused && workers[i].refused == 64u;
        for (size_t j = 0u; j < workers[i].issued_len; j++)
            all[total++] = workers[i].issued[j];
        free(workers[i].issued);
    }
    pthread_barrier_destroy(&start);
    qsort(all, total, sizeof(*all), compare_u64);
    bool exact = total == WINDOW;
    for (size_t i = 0u; exact && i < total; i++)
        exact = all[i] == first + i;
    free(all);
    char label[256];
    snprintf(label, sizeof(label),
             "%s: eight workers issue each of its last %u identities once",
             name, WINDOW);
    CHECK(exact, label);
    snprintf(label, sizeof(label),
             "%s: every worker is refused after exhaustion", name);
    CHECK(every_worker_refused, label);
    snprintf(label, sizeof(label),
             "%s: the namespace stays exhausted after the race", name);
    CHECK(raw ? atomic_load(raw) == UINT64_MAX &&
                    cetta_identity_try_take(raw) == 0u
              : prime_need_identity_test_next(ns) == UINT64_MAX &&
                    prime_need_identity_test_take(ns) == 0u,
          label);
    if (!raw)
        CHECK(prime_need_identity_test_seed(ns, 1u), name);
}

/* ---- Reserve-through ----------------------------------------------------- */

typedef struct {
    pthread_barrier_t *start;
    _Atomic uint64_t *next_key;
    uint64_t *issued;
    size_t issued_len;
    bool ordered;
} ReserveWorker;

#define RESERVE_ROUNDS 4000u

static void *reserve_and_take(void *raw_worker) {
    ReserveWorker *worker = raw_worker;
    pthread_barrier_wait(worker->start);
    for (unsigned i = 0u; i < RESERVE_ROUNDS; i++) {
        uint64_t key = atomic_fetch_add_explicit(
            worker->next_key, 3u, memory_order_relaxed);
        prime_need_identity_test_reserve_storage_keys_through(key);
        uint64_t id = prime_need_identity_test_take(
            PRIME_NEED_IDENTITY_STORAGE_KEY);
        /* Once a reservation has returned, no later draw issues a key at
         * or below it, on any worker. */
        if (id <= key)
            worker->ordered = false;
        worker->issued[worker->issued_len++] = id;
    }
    return NULL;
}

static void check_reserve_through(void) {
    const PrimeNeedIdentityNamespace ns = PRIME_NEED_IDENTITY_STORAGE_KEY;
    CHECK(prime_need_identity_test_seed(ns, 10u), "storage keys seeded");
    prime_need_identity_test_reserve_storage_keys_through(5u);
    CHECK(prime_need_identity_test_next(ns) == 10u,
          "reserving a key already passed never moves the counter back");
    prime_need_identity_test_reserve_storage_keys_through(10u);
    CHECK(prime_need_identity_test_next(ns) == 11u,
          "reserving the next key moves the counter strictly beyond it");
    prime_need_identity_test_reserve_storage_keys_through(UINT64_MAX - 2u);
    CHECK(prime_need_identity_test_next(ns) == UINT64_MAX - 1u &&
              prime_need_identity_test_take(ns) == UINT64_MAX - 1u &&
              prime_need_identity_test_take(ns) == 0u,
          "reserving through MAX-2 leaves exactly MAX-1 to issue");
    CHECK(prime_need_identity_test_seed(ns, 100u), "storage keys reseeded");
    prime_need_identity_test_reserve_storage_keys_through(UINT64_MAX - 1u);
    CHECK(prime_need_identity_test_next(ns) == UINT64_MAX &&
              prime_need_identity_test_take(ns) == 0u,
          "reserving the last issuable key exhausts the namespace");
    CHECK(prime_need_identity_test_seed(ns, 100u), "storage keys reseeded");
    prime_need_identity_test_reserve_storage_keys_through(UINT64_MAX);
    CHECK(prime_need_identity_test_next(ns) == UINT64_MAX &&
              prime_need_identity_test_take(ns) == 0u,
          "reserving UINT64_MAX exhausts the namespace");
    prime_need_identity_test_reserve_storage_keys_through(5u);
    prime_need_identity_test_reserve_storage_keys_through(UINT64_MAX);
    CHECK(prime_need_identity_test_next(ns) == UINT64_MAX &&
              prime_need_identity_test_take(ns) == 0u,
          "no later reservation revives an exhausted namespace");

    /* Reservations race eight drawing workers. */
    CHECK(prime_need_identity_test_seed(ns, 1u), "storage keys reseeded");
    _Atomic uint64_t next_key = 1u;
    pthread_barrier_t start;
    pthread_barrier_init(&start, NULL, WORKERS);
    ReserveWorker workers[WORKERS];
    pthread_t threads[WORKERS];
    for (unsigned i = 0u; i < WORKERS; i++) {
        workers[i] = (ReserveWorker){
            .start = &start,
            .next_key = &next_key,
            .issued = calloc(RESERVE_ROUNDS, sizeof(uint64_t)),
            .ordered = true,
        };
        pthread_create(&threads[i], NULL, reserve_and_take, &workers[i]);
    }
    uint64_t *all = calloc((size_t)WORKERS * RESERVE_ROUNDS,
                           sizeof(uint64_t));
    size_t total = 0u;
    bool ordered = true;
    for (unsigned i = 0u; i < WORKERS; i++) {
        pthread_join(threads[i], NULL);
        ordered = ordered && workers[i].ordered;
        for (size_t j = 0u; j < workers[i].issued_len; j++)
            all[total++] = workers[i].issued[j];
        free(workers[i].issued);
    }
    pthread_barrier_destroy(&start);
    qsort(all, total, sizeof(*all), compare_u64);
    bool distinct = total == (size_t)WORKERS * RESERVE_ROUNDS;
    for (size_t i = 0u; distinct && i < total; i++)
        distinct = all[i] != 0u && (i == 0u || all[i - 1u] < all[i]);
    free(all);
    CHECK(ordered,
          "racing reservations: a draw after a reservation is beyond its key");
    CHECK(distinct,
          "racing reservations: no storage key is issued twice or as zero");
    CHECK(prime_need_identity_test_seed(ns, 1u), "storage keys reset");
}

/* ---- Runtime callers driven into refusal --------------------------------- */

static void check_snapshot_callers(Arena *arena) {
    Atom *term = atom_symbol(arena, "delayed");
    PrimeNeedSnapshot base;
    prime_need_snapshot_init(&base);
    CHECK(prime_need_snapshot_begin(&base), "a session begins");

    /* Session. */
    unsigned before = refusals();
    CHECK(prime_need_identity_test_seed(PRIME_NEED_IDENTITY_SESSION,
                                        UINT64_MAX),
          "session namespace exhausted");
    /* A present snapshot needs no session, so begin from the empty one. */
    PrimeNeedSnapshot refused;
    prime_need_snapshot_init(&refused);
    PrimeNeedSnapshot empty;
    prime_need_snapshot_init(&empty);
    CHECK(!prime_need_snapshot_begin(&refused) &&
              !prime_need_snapshot_present(&refused) &&
              snapshot_same(&refused, &empty),
          "snapshot_begin refuses and leaves the empty snapshot");
    CHECK(refusals() == before + 1u, "session refusal is reported");
    CHECK(prime_need_identity_test_seed(PRIME_NEED_IDENTITY_SESSION, 1u),
          "session namespace reset");

    /* Storage key: refused before any other identity is drawn. */
    PrimeNeedSnapshot keep = base;
    PrimeNeedSnapshot out;
    uint64_t thunk_id = POISON_ID;
    uint64_t thunk_next =
        prime_need_identity_test_next(PRIME_NEED_IDENTITY_THUNK);
    uint64_t authority_next =
        prime_need_identity_test_next(PRIME_NEED_IDENTITY_AUTHORITY);
    uint64_t serial_next =
        prime_need_identity_test_next(PRIME_NEED_IDENTITY_SERIAL);
    size_t live = arena->live_bytes;
    before = refusals();
    CHECK(prime_need_identity_test_seed(PRIME_NEED_IDENTITY_STORAGE_KEY,
                                        UINT64_MAX),
          "storage-key namespace exhausted");
    snapshot_poison(&out);
    CHECK(!prime_need_snapshot_allocate(arena, &base, term, &out,
                                        &thunk_id) &&
              snapshot_poisoned(&out) && thunk_id == POISON_ID &&
              snapshot_same(&base, &keep),
          "allocate refuses a fresh storage key and changes nothing held");
    CHECK(prime_need_identity_test_next(PRIME_NEED_IDENTITY_THUNK) ==
                  thunk_next &&
              prime_need_identity_test_next(
                  PRIME_NEED_IDENTITY_AUTHORITY) == authority_next &&
              prime_need_identity_test_next(PRIME_NEED_IDENTITY_SERIAL) ==
                  serial_next &&
              arena->live_bytes == live,
          "a storage-key refusal draws no further identity and allocates nothing");
    CHECK(refusals() == before + 1u, "storage-key refusal is reported");
    CHECK(prime_need_identity_test_seed(PRIME_NEED_IDENTITY_STORAGE_KEY, 1u),
          "storage-key namespace reset");

    /* Thunk: the last fresh identity is issued, the next refused. */
    uint64_t last_thunk =
        prime_need_identity_test_last(PRIME_NEED_IDENTITY_THUNK);
    CHECK(prime_need_identity_test_seed(PRIME_NEED_IDENTITY_THUNK,
                                        last_thunk),
          "thunk namespace at its last identity");
    PrimeNeedSnapshot with_last;
    uint64_t last_id = 0u;
    CHECK(prime_need_snapshot_allocate(arena, &base, term, &with_last,
                                       &last_id) &&
              last_id == last_thunk,
          "allocate issues the last fresh thunk identity");
    keep = with_last;
    authority_next =
        prime_need_identity_test_next(PRIME_NEED_IDENTITY_AUTHORITY);
    serial_next = prime_need_identity_test_next(PRIME_NEED_IDENTITY_SERIAL);
    live = arena->live_bytes;
    before = refusals();
    snapshot_poison(&out);
    thunk_id = POISON_ID;
    CHECK(!prime_need_snapshot_allocate(arena, &with_last, term, &out,
                                        &thunk_id) &&
              snapshot_poisoned(&out) && thunk_id == POISON_ID &&
              snapshot_same(&with_last, &keep),
          "allocate refuses past the last thunk identity");
    CHECK(prime_need_identity_test_next(PRIME_NEED_IDENTITY_AUTHORITY) ==
                  authority_next &&
              prime_need_identity_test_next(PRIME_NEED_IDENTITY_SERIAL) ==
                  serial_next &&
              arena->live_bytes == live,
          "a thunk refusal draws no authority or serial and allocates nothing");
    CHECK(refusals() == before + 1u, "thunk refusal is reported");
    PrimeNeedCellView last_cell;
    CHECK(prime_need_snapshot_lookup(&with_last, last_thunk, &last_cell) &&
              last_cell.cache_state == PRIME_NEED_CACHE_EMPTY,
          "the cell issued before exhaustion is intact");
    /* Named cells own the upper half and keep working. */
    PrimeNeedSnapshot named;
    uint64_t named_id = PRIME_NEED_NAMED_CELL_BIT | 7u;
    CHECK(prime_need_snapshot_allocate_named(
              arena, &with_last, atom_symbol(arena, "named"), named_id,
              &named) &&
              prime_need_snapshot_lookup(&named, named_id, &last_cell) &&
              prime_need_snapshot_lookup(&named, last_thunk, &last_cell),
          "an exhausted fresh half leaves named cells and old cells usable");
    CHECK(prime_need_identity_test_seed(PRIME_NEED_IDENTITY_THUNK, 1u),
          "thunk namespace reset");

    /* Authority, for a fresh cell and for a named cell. */
    CHECK(prime_need_identity_test_seed(PRIME_NEED_IDENTITY_AUTHORITY,
                                        UINT64_MAX),
          "authority namespace exhausted");
    keep = base;
    serial_next = prime_need_identity_test_next(PRIME_NEED_IDENTITY_SERIAL);
    live = arena->live_bytes;
    before = refusals();
    snapshot_poison(&out);
    thunk_id = POISON_ID;
    CHECK(!prime_need_snapshot_allocate(arena, &base, term, &out,
                                        &thunk_id) &&
              snapshot_poisoned(&out) && thunk_id == POISON_ID &&
              snapshot_same(&base, &keep),
          "allocate refuses without an authority");
    snapshot_poison(&out);
    CHECK(!prime_need_snapshot_allocate_named(
              arena, &base, term, PRIME_NEED_NAMED_CELL_BIT | 9u, &out) &&
              snapshot_poisoned(&out) && snapshot_same(&base, &keep),
          "allocate_named refuses without an authority");
    CHECK(prime_need_identity_test_next(PRIME_NEED_IDENTITY_SERIAL) ==
                  serial_next &&
              arena->live_bytes == live,
          "authority refusals push no frame");
    CHECK(refusals() == before + 2u, "authority refusals are reported");
    CHECK(prime_need_identity_test_seed(PRIME_NEED_IDENTITY_AUTHORITY, 1u),
          "authority namespace reset");

    /* Frame serial, for an allocation and for a cache transition. */
    PrimeNeedSnapshot cell_snapshot;
    uint64_t cell_id = 0u;
    CHECK(prime_need_snapshot_allocate(arena, &base, term, &cell_snapshot,
                                       &cell_id),
          "a cell is allocated before the serial namespace ends");
    CHECK(prime_need_identity_test_seed(PRIME_NEED_IDENTITY_SERIAL,
                                        UINT64_MAX),
          "serial namespace exhausted");
    keep = cell_snapshot;
    live = arena->live_bytes;
    before = refusals();
    snapshot_poison(&out);
    thunk_id = POISON_ID;
    CHECK(!prime_need_snapshot_allocate(arena, &cell_snapshot, term, &out,
                                        &thunk_id) &&
              snapshot_poisoned(&out) && thunk_id == POISON_ID &&
              snapshot_same(&cell_snapshot, &keep) &&
              arena->live_bytes == live,
          "allocate refuses without a frame serial and leaves no frame");
    uint64_t evaluator = prime_need_fresh_evaluator_id();
    snapshot_poison(&out);
    CHECK(evaluator != 0u &&
              !prime_need_snapshot_start_evaluation(
                  arena, &cell_snapshot, cell_id, evaluator, &out) &&
              snapshot_poisoned(&out) &&
              snapshot_same(&cell_snapshot, &keep) &&
              arena->live_bytes == live,
          "start_evaluation refuses without a frame serial");
    PrimeNeedCellView cell;
    CHECK(prime_need_snapshot_lookup(&cell_snapshot, cell_id, &cell) &&
              cell.cache_state == PRIME_NEED_CACHE_EMPTY,
          "the refused transition leaves the cell empty");
    CHECK(refusals() == before + 2u, "serial refusals are reported");
    CHECK(prime_need_identity_test_seed(PRIME_NEED_IDENTITY_SERIAL, 1u),
          "serial namespace reset");
    CHECK(prime_need_snapshot_start_evaluation(
              arena, &cell_snapshot, cell_id, evaluator, &out),
          "the same transition succeeds once serials are available");

    /* A persisted key reserves the storage namespace through itself. */
    CHECK(prime_need_identity_test_seed(PRIME_NEED_IDENTITY_STORAGE_KEY, 1u),
          "storage-key namespace reset");
    PrimeNeedSnapshot persisted;
    CHECK(prime_need_snapshot_allocate_persisted(
              arena, &base, atom_symbol(arena, "persisted"), 41u,
              &persisted, &thunk_id) &&
              prime_need_identity_test_next(
                  PRIME_NEED_IDENTITY_STORAGE_KEY) == 42u,
          "a persisted key moves fresh storage keys beyond it");
    CHECK(prime_need_snapshot_allocate_persisted(
              arena, &base, atom_symbol(arena, "persisted-last"),
              UINT64_MAX, &persisted, &thunk_id) &&
              prime_need_identity_test_next(
                  PRIME_NEED_IDENTITY_STORAGE_KEY) == UINT64_MAX,
          "the cell persisted under UINT64_MAX keeps its key and exhausts "
          "fresh storage keys");
    keep = persisted;
    before = refusals();
    snapshot_poison(&out);
    CHECK(!prime_need_snapshot_allocate(arena, &persisted, term, &out,
                                        &thunk_id) &&
              snapshot_poisoned(&out) && snapshot_same(&persisted, &keep) &&
              refusals() == before + 1u,
          "after it, a fresh cell is refused rather than given a reused key");
    CHECK(prime_need_identity_test_seed(PRIME_NEED_IDENTITY_STORAGE_KEY, 1u),
          "storage-key namespace reset");
}

static void check_branch_state_callers(Arena *arena) {
    StateCell cell = {
        .value = atom_int(arena, 0),
        .content_type = atom_symbol(arena, "Number"),
    };

    /* Receipt session at begin. */
    CHECK(prime_need_identity_test_seed(PRIME_NEED_IDENTITY_RECEIPT_SESSION,
                                        UINT64_MAX),
          "receipt-session namespace exhausted");
    unsigned before = refusals();
    PrimeNeedBranchState state;
    prime_need_branch_state_init(&state);
    PrimeNeedBranchState empty;
    prime_need_branch_state_init(&empty);
    CHECK(!prime_need_branch_state_begin(arena, &state) &&
              !prime_need_branch_state_present(&state) &&
              branch_same(&state, &empty),
          "branch_state_begin refuses and leaves the empty state");
    /* Receipt session for an event without a begun base. */
    PrimeNeedBranchState out;
    branch_poison(&out);
    CHECK(!prime_need_branch_state_write(arena, &empty, &cell, cell.value,
                                         atom_int(arena, 1), &out) &&
              branch_poisoned(&out),
          "a first event without a session is refused");
    CHECK(refusals() == before + 2u, "receipt-session refusals are reported");
    CHECK(prime_need_identity_test_seed(PRIME_NEED_IDENTITY_RECEIPT_SESSION,
                                        1u),
          "receipt-session namespace reset");

    /* Receipt node for an event and for a join. */
    PrimeNeedBranchState base;
    prime_need_branch_state_init(&base);
    CHECK(prime_need_branch_state_begin(arena, &base), "branch state begins");
    StateCell left_cell = {
        .value = atom_int(arena, 0),
        .content_type = atom_symbol(arena, "Number"),
    };
    StateCell right_cell = {
        .value = atom_int(arena, 0),
        .content_type = atom_symbol(arena, "Number"),
    };
    PrimeNeedBranchState left, right;
    CHECK(prime_need_branch_state_write(arena, &base, &left_cell,
                                        left_cell.value, atom_int(arena, 1),
                                        &left) &&
              prime_need_branch_state_write(arena, &base, &right_cell,
                                            right_cell.value,
                                            atom_int(arena, 2), &right),
          "two sibling branch states with events");
    CHECK(prime_need_identity_test_seed(PRIME_NEED_IDENTITY_RECEIPT_NODE,
                                        UINT64_MAX),
          "receipt-node namespace exhausted");
    before = refusals();
    PrimeNeedBranchState keep = base;
    branch_poison(&out);
    Atom *written = atom_int(arena, 3);
    size_t live = arena->live_bytes;
    CHECK(!prime_need_branch_state_write(arena, &base, &cell, cell.value,
                                         written, &out) &&
              branch_poisoned(&out) && branch_same(&base, &keep) &&
              arena->live_bytes == live,
          "an event is refused without a node identity and copies nothing");
    PrimeNeedBranchState joined = left;
    PrimeNeedBranchState keep_left = left;
    CHECK(!prime_need_branch_state_merge_nonempty(&joined, &right) &&
              branch_same(&joined, &keep_left) &&
              arena->live_bytes == live,
          "a join is refused without a node identity and leaves its target");
    CHECK(refusals() == before + 2u, "receipt-node refusals are reported");
    CHECK(prime_need_identity_test_seed(PRIME_NEED_IDENTITY_RECEIPT_NODE, 1u),
          "receipt-node namespace reset");
    CHECK(prime_need_branch_state_merge_nonempty(&joined, &right),
          "the same join succeeds once node identities are available");
}

static void check_exported_draws(void) {
    struct {
        PrimeNeedIdentityNamespace ns;
        uint64_t (*draw)(void);
        const char *label;
    } draws[] = {
        {PRIME_NEED_IDENTITY_SOURCE_OCCURRENCE,
         prime_need_fresh_source_occurrence,
         "fresh_source_occurrence issues the last identity, then zero"},
        {PRIME_NEED_IDENTITY_EVALUATOR, prime_need_fresh_evaluator_id,
         "fresh_evaluator_id issues the last identity, then zero"},
        {PRIME_NEED_IDENTITY_RESAMPLE_SCOPE, prime_need_fresh_resample_scope,
         "fresh_resample_scope issues the last identity, then zero"},
    };
    for (size_t i = 0u; i < sizeof(draws) / sizeof(draws[0]); i++) {
        uint64_t last = prime_need_identity_test_last(draws[i].ns);
        unsigned before = refusals();
        CHECK(prime_need_identity_test_seed(draws[i].ns, last) &&
                  draws[i].draw() == last && draws[i].draw() == 0u &&
                  draws[i].draw() == 0u && refusals() == before + 2u,
              draws[i].label);
        CHECK(prime_need_identity_test_seed(draws[i].ns, 1u),
              draws[i].label);
    }
}

int main(void) {
    SymbolTable symbols;
    Arena arena;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);
    prime_need_identity_set_refusal_observer(count_refusal);

    check_helper();
    for (int ns = 0; ns < PRIME_NEED_IDENTITY_NAMESPACE_COUNT; ns++)
        check_namespace_end((PrimeNeedIdentityNamespace)ns);
    check_thunk_named_half();

    _Atomic uint64_t raw = 1u;
    check_concurrent_exhaustion("helper", PRIME_NEED_IDENTITY_SESSION, &raw,
                                UINT64_MAX - 1u);
    for (int ns = 0; ns < PRIME_NEED_IDENTITY_NAMESPACE_COUNT; ns++)
        check_concurrent_exhaustion(
            prime_need_identity_test_name((PrimeNeedIdentityNamespace)ns),
            (PrimeNeedIdentityNamespace)ns, NULL,
            prime_need_identity_test_last((PrimeNeedIdentityNamespace)ns));

    check_reserve_through();
    check_snapshot_callers(&arena);
    check_branch_state_callers(&arena);
    check_exported_draws();

    prime_need_identity_set_refusal_observer(NULL);
    arena_free(&arena);
    symbol_table_free(&symbols);
    printf("(PrimeNeedIdentitySummary %u %u %u)\n", checks,
           checks - failures, failures);
    return failures == 0u ? 0 : 1;
}
