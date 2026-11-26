/*
 * Generated names through the parallel executor (parallel_executor.h,
 * name_supply.h).  A pushed task draws from its pusher's next push, a part
 * of the pusher's pool its push count gives, or from the supply its pusher
 * placed it at, so its names depend on the tasks and the seed, never on the
 * workers.  A chain of a million tasks, each pushing the next from its own
 * supply, gives the same names on 1, 2 and 4 workers, keeps at most a few
 * blocks alive at once and gives every block back, with a flat heap; a
 * coordinator's million pushes, one at a time, give the same names on any
 * number of workers, all in the root block; no two names are alike.  A
 * coordinator that spawns a known number of tasks hands its share over to
 * them: task i draws from its balanced piece, within ceil(log2 k) halvings,
 * on any number of workers, and a name the coordinator draws meanwhile is
 * one of a fresh block.  When the executor ends, the coordinator's supply
 * has its place back, at the latest clocks its tasks reached, so that its
 * next names are fresh.  Run once per mode, as test_name_supply is.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#if defined(__GLIBC__)
#include <malloc.h>
#endif

#include "name_supply.h"
#include "parallel_executor.h"

static int g_failures;

#define CHECK(condition, ...)                                         \
    do {                                                              \
        if (!(condition)) {                                           \
            g_failures++;                                             \
            fprintf(stderr, "FAIL: " __VA_ARGS__);                    \
            fputc('\n', stderr);                                      \
        }                                                             \
    } while (0)

typedef struct {
    uint64_t hi, lo;
} Fingerprint;

static Fingerprint spelling_fingerprint(CettaNameKind kind,
                                        CettaGeneratedName name) {
    static const uint8_t key[16] = {2, 7, 1, 8, 2, 8, 1, 8,
                                    2, 8, 4, 5, 9, 0, 4, 5};
    char text[CETTA_GENERATED_NAME_SPELLING_MAX];
    size_t length = cetta_generated_name_spell(kind, name, text, sizeof(text));
    uint8_t digest[16];
    cetta_siphash24_128(key, text, length, digest);
    Fingerprint f = {0u, 0u};
    for (unsigned i = 0u; i < 8u; i++) {
        f.hi = (f.hi << 8) | digest[i];
        f.lo = (f.lo << 8) | digest[8u + i];
    }
    return f;
}

static int compare_fingerprints(const void *left, const void *right) {
    const Fingerprint *a = left, *b = right;
    if (a->hi != b->hi) return a->hi < b->hi ? -1 : 1;
    if (a->lo != b->lo) return a->lo < b->lo ? -1 : 1;
    return 0;
}

static void note_peak(_Atomic uint64_t *peak) {
    uint64_t live = cetta_name_supply_live_blocks();
    uint64_t seen = atomic_load_explicit(peak, memory_order_relaxed);
    while (live > seen &&
           !atomic_compare_exchange_weak_explicit(peak, &seen, live,
                                                  memory_order_relaxed,
                                                  memory_order_relaxed)) {
    }
}

/* ── A chain of tasks ────────────────────────────────────────────────── */

enum { CHAIN_STRIDE = 997u };

typedef struct {
    unsigned levels;
    Fingerprint *prints;  /* one per CHAIN_STRIDE levels, then three */
    _Atomic uint64_t peak_blocks;
} Chain;

/* Task LEVEL + 1 of the chain: draws every CHAIN_STRIDE levels, pushes the
   next level from its own supply, and at the bottom draws three names. */
static bool chain_task(CettaParallelWorker *worker, void *task, void *user) {
    Chain *chain = user;
    unsigned level = (unsigned)((uintptr_t)task - 1u);
    if (level % CHAIN_STRIDE == 0u) {
        CettaNameKind kind = (level / CHAIN_STRIDE) % 2u
            ? CETTA_NAME_KIND_CALLABLE : CETTA_NAME_KIND_SPACE;
        chain->prints[level / CHAIN_STRIDE] =
            spelling_fingerprint(kind, cetta_name_fresh(kind));
    }
    note_peak(&chain->peak_blocks);
    if (level < chain->levels)
        return cetta_parallel_executor_push(
            cetta_parallel_worker_executor(worker),
            (void *)(uintptr_t)(level + 2u));
    unsigned bottom = chain->levels / CHAIN_STRIDE + 1u;
    for (unsigned i = 0u; i < 3u; i++) {
        CettaNameKind kind = i % 2u ? CETTA_NAME_KIND_CALLABLE
                                    : CETTA_NAME_KIND_SPACE;
        chain->prints[bottom + i] =
            spelling_fingerprint(kind, cetta_name_fresh(kind));
    }
    return true;
}

static size_t chain_slots(unsigned levels) {
    return levels / CHAIN_STRIDE + 4u;
}

/* The clocks the chain's bottom task reaches: each task's draws, on top of
   those of the tasks above it. */
static void chain_clocks(unsigned levels, uint64_t *clocks) {
    clocks[CETTA_NAME_KIND_SPACE] = clocks[CETTA_NAME_KIND_CALLABLE] = 0u;
    for (unsigned level = 0u; level <= levels; level++)
        if (level % CHAIN_STRIDE == 0u)
            clocks[(level / CHAIN_STRIDE) % 2u ? CETTA_NAME_KIND_CALLABLE
                                               : CETTA_NAME_KIND_SPACE]++;
    clocks[CETTA_NAME_KIND_SPACE] += 2u;
    clocks[CETTA_NAME_KIND_CALLABLE] += 1u;
}

/* The coordinator ROOT after its executor ended: its whole share in the
   root block with nothing pushed, at the clocks EXPECTED, and its next name
   a root's. */
static bool coordinator_back(CettaNameSupply *root, const uint64_t *expected) {
    bool placed = root->share == CETTA_NAME_SHARE_WHOLE && root->block == NULL &&
                  root->pushes == 0u && root->pushed == 0u &&
                  root->drawn[CETTA_NAME_KIND_SPACE] == expected[CETTA_NAME_KIND_SPACE] &&
                  root->drawn[CETTA_NAME_KIND_CALLABLE] == expected[CETTA_NAME_KIND_CALLABLE];
    CettaGeneratedName next = cetta_name_supply_fresh(root, CETTA_NAME_KIND_SPACE);
    bool root_name = cetta_generated_name_form(next) == CETTA_NAME_FORM_HASHED ||
                     (next.hi == CETTA_NAME_SHARE_WHOLE && next.tag == CETTA_NAME_FORM_COUNTED &&
                      next.lo == expected[CETTA_NAME_KIND_SPACE] + 1u);
    return placed && root_name;
}

static bool g_coordinator_back = true;

/* Each run starts from a root of its own, as a run of the program does. */
static bool run_chain(unsigned levels, uint32_t workers, Fingerprint *prints,
                      uint64_t *peak) {
    CettaNameSupply root;
    cetta_name_supply_init_root(&root);
    CettaNameSupply *previous = cetta_name_supply_install(&root);
    Chain chain = {.levels = levels, .prints = prints};
    atomic_init(&chain.peak_blocks, 0u);
    memset(prints, 0, chain_slots(levels) * sizeof(*prints));
    CettaParallelExecutor executor;
    CettaParallelExecutorConfig config = {
        .thread_count = workers,
        .prefer_persistent_workers = true,
        .user = &chain,
        .task_fn = chain_task,
        .worker_failure_message = "chain task failed",
    };
    if (!cetta_parallel_executor_init(&executor, &config))
        return false;
    bool ok = cetta_parallel_executor_push(&executor, (void *)(uintptr_t)1u) &&
              cetta_parallel_executor_run(&executor);
    cetta_parallel_executor_free(&executor);
    *peak = atomic_load_explicit(&chain.peak_blocks, memory_order_relaxed);
    uint64_t expected[CETTA_NAME_KIND_COUNT];
    chain_clocks(levels, expected);
    g_coordinator_back = g_coordinator_back && coordinator_back(&root, expected);
    cetta_name_supply_install(previous);
    cetta_name_supply_dispose(&root);
    return ok;
}

/* ── Siblings pushed by one coordinator ──────────────────────────────── */

typedef struct {
    Fingerprint *prints;
    /* Siblings that drew beyond the root block. */
    _Atomic uint64_t blocked;
} Siblings;

static bool sibling_task(CettaParallelWorker *worker, void *task, void *user) {
    (void)worker;
    Siblings *siblings = user;
    size_t index = (size_t)((uintptr_t)task - 1u);
    CettaGeneratedName name = cetta_name_fresh(CETTA_NAME_KIND_SPACE);
    if (cetta_name_supply_current()->block)
        atomic_fetch_add_explicit(&siblings->blocked, 1u, memory_order_relaxed);
    siblings->prints[index] = spelling_fingerprint(CETTA_NAME_KIND_SPACE, name);
    return true;
}

static uint64_t g_siblings_blocked;

static bool run_siblings(size_t count, uint32_t workers, Fingerprint *prints) {
    CettaNameSupply root;
    cetta_name_supply_init_root(&root);
    CettaNameSupply *previous = cetta_name_supply_install(&root);
    Siblings siblings = {.prints = prints};
    atomic_init(&siblings.blocked, 0u);
    CettaParallelExecutor executor;
    CettaParallelExecutorConfig config = {
        .thread_count = workers,
        .prefer_persistent_workers = true,
        .user = &siblings,
        .task_fn = sibling_task,
        .worker_failure_message = "sibling task failed",
    };
    if (!cetta_parallel_executor_init(&executor, &config))
        return false;
    bool ok = true;
    for (size_t i = 0u; ok && i < count; i++)
        ok = cetta_parallel_executor_push(&executor, (void *)(uintptr_t)(i + 1u));
    ok = ok && cetta_parallel_executor_run(&executor);
    cetta_parallel_executor_free(&executor);
    g_siblings_blocked += atomic_load_explicit(&siblings.blocked,
                                               memory_order_relaxed);
    /* Every sibling drew one space name from the root's clocks. */
    static const uint64_t expected[CETTA_NAME_KIND_COUNT] = {1u, 0u};
    g_coordinator_back = g_coordinator_back && coordinator_back(&root, expected);
    cetta_name_supply_install(previous);
    cetta_name_supply_dispose(&root);
    return ok;
}

/* ── A spawn of a known number of tasks ──────────────────────────────── */

typedef struct {
    uint64_t *shares;      /* the share each task drew from */
    Fingerprint *prints;   /* the name each task drew */
    _Atomic uint64_t blocked;
} Spawned;

static bool spawned_task(CettaParallelWorker *worker, void *task, void *user) {
    (void)worker;
    Spawned *spawned = user;
    size_t index = (size_t)((uintptr_t)task - 1u);
    const CettaNameSupply *supply = cetta_name_supply_current();
    spawned->shares[index] = supply->share;
    if (supply->block)
        atomic_fetch_add_explicit(&spawned->blocked, 1u, memory_order_relaxed);
    spawned->prints[index] = spelling_fingerprint(
        CETTA_NAME_KIND_SPACE, cetta_name_fresh(CETTA_NAME_KIND_SPACE));
    return true;
}

static bool g_spawn_back = true;
static uint64_t g_spawn_blocked;

/* COUNT tasks spawned on WORKERS workers by a root that drew one name
   first; the root draws a name meanwhile, after the spawn and before the
   run: whether it lies in a block of its own into *MEANWHILE_FRESH, its
   fingerprint into *MEANWHILE_PRINT.  A name is spelled while its block
   lives, and only the root's supply holds that block, which it gives up
   when the executor gives the root its place back. */
static bool run_spawn(uint32_t count, uint32_t workers, uint64_t *shares,
                      Fingerprint *prints, Fingerprint *meanwhile_print,
                      bool *meanwhile_fresh) {
    CettaNameSupply root;
    cetta_name_supply_init_root(&root);
    CettaNameSupply *previous = cetta_name_supply_install(&root);
    (void)cetta_name_fresh(CETTA_NAME_KIND_SPACE);
    Spawned spawned = {.shares = shares, .prints = prints};
    atomic_init(&spawned.blocked, 0u);
    CettaParallelExecutor executor;
    CettaParallelExecutorConfig config = {
        .thread_count = workers,
        .prefer_persistent_workers = true,
        .user = &spawned,
        .task_fn = spawned_task,
        .worker_failure_message = "spawned task failed",
    };
    if (!cetta_parallel_executor_init(&executor, &config))
        return false;
    CettaParallelSpawn spawn;
    cetta_parallel_executor_spawn_begin(&executor, &spawn, count);
    bool ok = true;
    for (uint32_t i = 0u; ok && i < count; i++)
        ok = cetta_parallel_executor_spawn_push(&executor, &spawn,
                                                (void *)(uintptr_t)(i + 1u));
    cetta_parallel_executor_spawn_end(&spawn);
    CettaGeneratedName meanwhile = cetta_name_fresh(CETTA_NAME_KIND_SPACE);
    *meanwhile_fresh = cetta_generated_name_block(meanwhile) != NULL;
    *meanwhile_print = spelling_fingerprint(CETTA_NAME_KIND_SPACE, meanwhile);
    ok = ok && cetta_parallel_executor_run(&executor);
    cetta_parallel_executor_free(&executor);
    g_spawn_blocked += atomic_load_explicit(&spawned.blocked,
                                            memory_order_relaxed);
    /* The root's first name, then every task's, from its clock 1. */
    static const uint64_t expected[CETTA_NAME_KIND_COUNT] = {2u, 0u};
    g_spawn_back = g_spawn_back && coordinator_back(&root, expected);
    cetta_name_supply_install(previous);
    cetta_name_supply_dispose(&root);
    return ok;
}

/* How many halvings an interval's word lies below the whole interval. */
static unsigned interval_depth(uint64_t share) {
    return 63u - (unsigned)__builtin_clzll(share);
}

static unsigned ceil_log2(uint32_t k) {
    unsigned bits = 0u;
    while ((UINT64_C(1) << bits) < k)
        bits++;
    return bits;
}

/* Spawns of 1, 2, 3, 640 and 100000 tasks on 1, 2 and 4 workers: task i
   draws from the balanced piece i of the root's whole share, the same on
   every number of workers, within ceil(log2 k) halvings; no name twice,
   the coordinator's meanwhile name among them; the root back whole. */
static void test_spawns(bool unforgeable, unsigned *deepest_out) {
    static const uint32_t counts[5] = {1u, 2u, 3u, 640u, 100000u};
    static const uint32_t workers[3] = {1u, 2u, 4u};
    bool pieces_ok = true, depth_ok = true, same_ok = true, distinct_ok = true;
    bool meanwhile_ok = true;
    unsigned deepest = 0u;
    for (unsigned c = 0u; c < 5u; c++) {
        uint32_t count = counts[c];
        uint64_t *expected = malloc(count * sizeof(*expected));
        CettaNameSupply model;
        cetta_name_supply_init_root(&model);
        model.drawn[CETTA_NAME_KIND_SPACE] = 1u;
        CettaNameSupplyPieces pieces;
        cetta_name_supply_pieces_begin(&pieces, &model, count);
        for (uint32_t i = 0u; i < count; i++) {
            CettaNameSupply piece = cetta_name_supply_pieces_next(&pieces);
            expected[i] = piece.share;
            depth_ok = depth_ok && piece.block == NULL &&
                       interval_depth(piece.share) <= ceil_log2(count);
            if (interval_depth(piece.share) > deepest)
                deepest = interval_depth(piece.share);
            cetta_name_supply_dispose(&piece);
        }
        cetta_name_supply_dispose(&model);
        Fingerprint *prints[3];
        for (unsigned w = 0u; w < 3u; w++) {
            uint64_t *shares = calloc(count, sizeof(*shares));
            prints[w] = malloc((count + 1u) * sizeof(*prints[w]));
            Fingerprint meanwhile_print;
            bool meanwhile_fresh = false;
            CHECK(run_spawn(count, workers[w], shares, prints[w],
                            &meanwhile_print, &meanwhile_fresh),
                  "a spawn of %u tasks runs on %u workers", count, workers[w]);
            pieces_ok = pieces_ok &&
                        memcmp(shares, expected, count * sizeof(*shares)) == 0;
            /* Handed over, the coordinator draws in a fresh block. */
            meanwhile_ok = meanwhile_ok && (unforgeable || meanwhile_fresh);
            prints[w][count] = meanwhile_print;
            free(shares);
        }
        for (unsigned w = 1u; w < 3u; w++)
            same_ok = same_ok &&
                      memcmp(prints[0], prints[w], count * sizeof(Fingerprint)) == 0;
        qsort(prints[0], count + 1u, sizeof(Fingerprint), compare_fingerprints);
        for (uint32_t i = 1u; i <= count; i++)
            distinct_ok = distinct_ok &&
                          compare_fingerprints(&prints[0][i - 1u], &prints[0][i]) != 0;
        for (unsigned w = 0u; w < 3u; w++)
            free(prints[w]);
        free(expected);
    }
    CHECK(pieces_ok, "task i of a spawn of k draws from the balanced piece i "
                     "of the coordinator's share, on 1, 2 and 4 workers");
    CHECK(depth_ok, "every piece lies in the root block within ceil(log2 k) "
                    "halvings");
    CHECK(same_ok, "a spawn's names on 2 and 4 workers are those on one");
    CHECK(distinct_ok, "a spawn's names and the coordinator's meanwhile name "
                       "are all distinct");
    CHECK(meanwhile_ok, "a coordinator that spawned draws meanwhile in a "
                        "fresh block, outside the pieces it handed over");
    CHECK(g_spawn_blocked == 0u, "no spawned task draws beyond the root block "
                                 "(%llu did)", (unsigned long long)g_spawn_blocked);
    *deepest_out = deepest;
}

static size_t heap_in_use(void) {
#if defined(__GLIBC__)
    return mallinfo2().uordblks;
#else
    return 0u;
#endif
}

static long peak_rss_kib(void) {
    struct rusage usage;
    return getrusage(RUSAGE_SELF, &usage) == 0 ? usage.ru_maxrss : 0;
}

int main(void) {
    char error[200];
    if (!cetta_name_supply_configure(NULL, NULL, error, sizeof(error))) {
        fprintf(stderr, "FAIL: the environment's name mode reads: %s\n", error);
        return 1;
    }
    bool unforgeable = cetta_name_supply_unforgeable();

    /* A short chain warms the workers and the allocator. */
    enum { SHORT = 100000u, LONG = 1000000u, SIBLINGS = 1000000u };
    Fingerprint *warm = malloc(chain_slots(SHORT) * sizeof(*warm));
    uint64_t peak = 0u;
    CHECK(run_chain(SHORT, 4u, warm, &peak), "a short chain runs");
    size_t slots = chain_slots(LONG);
    Fingerprint *by_workers[3];
    static const uint32_t workers[3] = {1u, 2u, 4u};
    uint64_t peaks[3] = {0u, 0u, 0u};
    for (unsigned i = 0u; i < 3u; i++)
        by_workers[i] = malloc(slots * sizeof(*by_workers[i]));
    long rss_short = peak_rss_kib();
    size_t heap_before = heap_in_use();
    for (unsigned i = 0u; i < 3u; i++) {
        CHECK(run_chain(LONG, workers[i], by_workers[i], &peaks[i]),
              "a chain of %u tasks runs on %u workers", (unsigned)LONG,
              workers[i]);
        CHECK(cetta_name_supply_live_blocks() == 0u,
              "a chain on %u workers gives back every block (%llu left)",
              workers[i], (unsigned long long)cetta_name_supply_live_blocks());
        /* What a chain keeps alive is set by its concurrency, never by
           its depth (about 16000 blocks a million levels make): a block per
           running task's supply, the queued child's, and the one a supply
           leaves while it moves on. */
        CHECK(peaks[i] <= workers[i] + 2u,
              "at most %u blocks of a chain are alive at once (%llu on %u workers)",
              workers[i] + 2u, (unsigned long long)peaks[i], workers[i]);
    }
    size_t heap_after = heap_in_use();
    long rss_long = peak_rss_kib();
    for (unsigned i = 1u; i < 3u; i++)
        CHECK(memcmp(by_workers[0], by_workers[i], slots * sizeof(Fingerprint)) == 0,
              "a chain's names on %u workers are those on one", workers[i]);
    /* Freed blocks and queue nodes leave the heap as it was: a block kept
       per filled word would add about 16000 blocks per chain. */
    CHECK(heap_after <= heap_before + 4u * 1024u,
          "three chains of a million tasks leave the heap flat (%zu -> %zu bytes)",
          heap_before, heap_after);

    Fingerprint *sibling_prints[3];
    for (unsigned i = 0u; i < 3u; i++) {
        sibling_prints[i] = malloc(SIBLINGS * sizeof(*sibling_prints[i]));
        CHECK(run_siblings(SIBLINGS, workers[i], sibling_prints[i]),
              "%u pushes run on %u workers", (unsigned)SIBLINGS, workers[i]);
    }
    for (unsigned i = 1u; i < 3u; i++)
        CHECK(memcmp(sibling_prints[0], sibling_prints[i],
                     SIBLINGS * sizeof(Fingerprint)) == 0,
              "siblings' names on %u workers are those on one", workers[i]);
    /* Counted pushes keep a million siblings in the root block. */
    CHECK(g_siblings_blocked == 0u && cetta_name_supply_live_blocks() == 0u,
          "a million siblings pushed one at a time draw in the root block "
          "(%llu beyond it)", (unsigned long long)g_siblings_blocked);

    /* Within a run no two names are alike: a chain's, and the siblings'. */
    size_t repeats = 0u, total = slots + SIBLINGS;
    Fingerprint *all = malloc(total * sizeof(*all));
    memcpy(all, by_workers[0], slots * sizeof(*all));
    qsort(all, slots, sizeof(*all), compare_fingerprints);
    for (size_t i = 1u; i < slots; i++)
        repeats += compare_fingerprints(&all[i - 1u], &all[i]) == 0;
    memcpy(all, sibling_prints[0], SIBLINGS * sizeof(*all));
    qsort(all, SIBLINGS, sizeof(*all), compare_fingerprints);
    for (size_t i = 1u; i < SIBLINGS; i++)
        repeats += compare_fingerprints(&all[i - 1u], &all[i]) == 0;
    CHECK(repeats == 0u, "%zu of %zu names repeat", repeats, total);
    unsigned spawn_deepest = 0u;
    test_spawns(unforgeable, &spawn_deepest);
    CHECK(g_coordinator_back && g_spawn_back,
          "every executor gives its coordinator's supply its place back, at "
          "the latest clocks its tasks reached");

    printf("PASS: names through the parallel executor (%s mode): a chain of "
           "%u tasks gives the same names on 1, 2 and 4 workers with at most "
           "%llu blocks alive at once, all given back (heap %zu -> %zu bytes, "
           "peak RSS %ld -> %ld KiB from a chain of %u); %u pushes, one at a "
           "time, give the same names on 1, 2 and 4 workers, all in the root "
           "block; %zu names distinct; spawns of up to 100000 tasks take "
           "balanced pieces, the deepest %u halvings down; the coordinator's "
           "supply comes back whole at its tasks' latest clocks\n",
           unforgeable ? "unforgeable" : "counter", (unsigned)LONG,
           (unsigned long long)(peaks[0] > peaks[2] ? peaks[0] : peaks[2]),
           heap_before, heap_after, rss_short, rss_long, (unsigned)SHORT,
           (unsigned)SIBLINGS, total, spawn_deepest);
    for (unsigned i = 0u; i < 3u; i++) {
        free(by_workers[i]);
        free(sibling_prints[i]);
    }
    free(all);
    free(warm);
    return g_failures ? 1 : 0;
}
