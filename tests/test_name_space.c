/*
 * Reclaimable names (src/binding/name_space.h).
 *
 * Properties under test:
 *   - a name's next incarnation starts at or above the watermark of every
 *     owner, whichever owner releases last;
 *   - a weak upgrade succeeds only on the incarnation it names, and a failed
 *     one that ends up releasing the last owner recycles the name;
 *   - owner counts never wrap, and a name with no generation left is
 *     retired instead of reissued;
 *   - quarantine never reuses a name, and forced reuse never lets a stale
 *     weak handle reach the next incarnation;
 *   - names live at the same moment in different threads are distinct, and
 *     each name's generations only grow;
 *   - racing upgrades and releases leave no name owned or lost.
 */
#define _GNU_SOURCE
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void test_after_weak_pin(void);
#define CETTA_NAME_SPACE_TEST_AFTER_WEAK_PIN test_after_weak_pin
#include "binding/name_space.h"

static int g_failures;
static int g_checks;

static void check(bool condition, const char *what) {
    g_checks++;
    if (!condition) {
        g_failures++;
        fprintf(stderr, "FAIL: %s\n", what);
    }
}

/* One name, so every reuse is forced. */
static CettaNameSpace g_one = CETTA_NAME_SPACE_INITIALIZER("one name", 1u, 2u);
static _Thread_local CettaNameLocal g_one_local;
CETTA_NAME_SPACE_ONCE_FUNCTION(one_once, g_one)

static uint32_t g_hook_name;
static uint64_t g_hook_watermark;
static bool g_hook_release;

static void test_after_weak_pin(void) {
    if (!g_hook_release)
        return;
    g_hook_release = false;
    cetta_name_release(&g_one, &g_one_local, one_once, g_hook_name,
                       g_hook_watermark);
}

static void test_watermark_of_a_non_final_release(void) {
    uint32_t name = 0u;
    uint64_t generation = 0u;
    check(cetta_name_acquire(&g_one, &g_one_local, one_once, &name, &generation),
          "acquire the only name");
    cetta_name_retain(&g_one, name);
    cetta_name_release(&g_one, &g_one_local, one_once, name, 100u);
    cetta_name_release(&g_one, &g_one_local, one_once, name, 0u);
    uint64_t next = 0u;
    check(cetta_name_acquire(&g_one, &g_one_local, one_once, &name, &next),
          "reacquire after both owners released");
    check(next >= 100u,
          "a watermark recorded by a non-final release survives to the next incarnation");
    cetta_name_release(&g_one, &g_one_local, one_once, name, 0u);
}

static void test_failed_weak_upgrade_as_last_release(void) {
    uint32_t name = 0u;
    uint64_t stale = 0u, live = 0u;
    check(cetta_name_acquire(&g_one, &g_one_local, one_once, &name, &stale),
          "first incarnation");
    cetta_name_release(&g_one, &g_one_local, one_once, name, 0u);
    check(cetta_name_acquire(&g_one, &g_one_local, one_once, &name, &live),
          "second incarnation");
    check(live > stale, "the second incarnation has a larger generation");
    /* The owner releases, with a watermark, between the weak pin and its
     * generation check: the failed upgrade's release is then the last. */
    g_hook_name = name;
    g_hook_watermark = 200u;
    g_hook_release = true;
    bool upgraded = cetta_name_retain_weak(&g_one, &g_one_local, one_once, name,
                                           stale);
    check(!upgraded, "a stale weak handle does not upgrade");
    check(!cetta_name_is_owned(&g_one, name), "nobody owns the name afterwards");
    uint64_t next = 0u;
    check(cetta_name_acquire(&g_one, &g_one_local, one_once, &name, &next),
          "the name is recycled by the failed upgrade's release");
    check(next >= 200u && next > live,
          "the recycled name starts above the actual owner's watermark");
    /* A current weak handle upgrades and becomes an owner. */
    check(cetta_name_retain_weak(&g_one, &g_one_local, one_once, name, next),
          "a current weak handle upgrades");
    cetta_name_release(&g_one, &g_one_local, one_once, name, 0u);
    check(cetta_name_is_owned(&g_one, name), "the original owner remains");
    cetta_name_release(&g_one, &g_one_local, one_once, name, 0u);
    check(!cetta_name_retain_weak(&g_one, &g_one_local, one_once, name, next),
          "a weak handle to a free name does not upgrade");
    uint64_t before = atomic_load(&g_one.stale_releases);
    cetta_name_release(&g_one, &g_one_local, one_once, name, 0u);
    check(atomic_load(&g_one.stale_releases) == before + 1u,
          "a release of a free name is counted and ignored");
    uint64_t after = 0u;
    check(cetta_name_acquire(&g_one, &g_one_local, one_once, &name, &after) &&
              after > next,
          "the name is still usable after the ignored release");
    cetta_name_release(&g_one, &g_one_local, one_once, name, 0u);
}

/* Run `fn` in a child; true when it ended by SIGABRT. */
static bool aborts(void (*fn)(void)) {
    fflush(stderr);
    pid_t pid = fork();
    if (pid == 0) {
        if (!freopen("/dev/null", "w", stderr))
            _exit(3);
        fn();
        _exit(0);
    }
    int status = 0;
    if (pid < 0 || waitpid(pid, &status, 0) != pid)
        return false;
    return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}

static CettaNameSpace g_edge = CETTA_NAME_SPACE_INITIALIZER("edge", 1u, 2u);
static _Thread_local CettaNameLocal g_edge_local;
CETTA_NAME_SPACE_ONCE_FUNCTION(edge_once, g_edge)

static void retain_at_owner_limit(void) {
    uint32_t name = 0u;
    if (!cetta_name_acquire(&g_edge, &g_edge_local, edge_once, &name, NULL))
        _exit(2);
    atomic_store(&cetta_name_cell(&g_edge, name)->owners, UINT32_MAX);
    cetta_name_retain(&g_edge, name);
}

static void weak_upgrade_at_owner_limit(void) {
    uint32_t name = 0u;
    uint64_t generation = 0u;
    if (!cetta_name_acquire(&g_edge, &g_edge_local, edge_once, &name,
                            &generation))
        _exit(2);
    atomic_store(&cetta_name_cell(&g_edge, name)->owners, UINT32_MAX);
    (void)cetta_name_retain_weak(&g_edge, &g_edge_local, edge_once, name,
                                 generation);
}

static void test_limits(void) {
    check(aborts(retain_at_owner_limit), "a strong retain never wraps the owner count");
    check(aborts(weak_upgrade_at_owner_limit), "a weak upgrade never wraps the owner count");
    uint32_t name = 0u;
    uint64_t generation = 0u;
    check(cetta_name_acquire(&g_edge, &g_edge_local, edge_once, &name,
                             &generation),
          "acquire the edge name");
    atomic_store(&cetta_name_cell(&g_edge, name)->generation, UINT64_MAX);
    cetta_name_release(&g_edge, &g_edge_local, edge_once, name, 0u);
    check(atomic_load(&g_edge.retired) == 1u,
          "a name with no generation left is retired");
    check(!cetta_name_acquire(&g_edge, &g_edge_local, edge_once, &name, NULL),
          "a retired name is never reissued (explicit exhaustion)");
    check(atomic_load(&g_edge.exhaustions) == 1u, "the refusal is counted");
}

static CettaNameSpace g_quarantine =
    CETTA_NAME_SPACE_INITIALIZER("quarantine", 1u, 64u);
static _Thread_local CettaNameLocal g_quarantine_local;
CETTA_NAME_SPACE_ONCE_FUNCTION(quarantine_once, g_quarantine)

static void double_release_in_quarantine(void) {
    uint32_t name = 0u;
    if (!cetta_name_acquire(&g_quarantine, &g_quarantine_local,
                            quarantine_once, &name, NULL))
        _exit(2);
    cetta_name_release(&g_quarantine, &g_quarantine_local, quarantine_once,
                       name, 0u);
    cetta_name_release(&g_quarantine, &g_quarantine_local, quarantine_once,
                       name, 0u);
}

static void test_quarantine_and_forced_reuse(void) {
    setenv("CETTA_NAME_QUARANTINE", "1", 1);
    uint32_t first = 0u, second = 0u;
    uint64_t first_generation = 0u;
    check(cetta_name_acquire(&g_quarantine, &g_quarantine_local, quarantine_once,
                             &first, &first_generation),
          "quarantine acquire");
    unsetenv("CETTA_NAME_QUARANTINE");
    check(g_quarantine.quarantine, "quarantine mode is read at first use");
    cetta_name_release(&g_quarantine, &g_quarantine_local, quarantine_once,
                       first, 0u);
    check(cetta_name_acquire(&g_quarantine, &g_quarantine_local,
                             quarantine_once, &second, NULL) &&
              second != first,
          "quarantine never hands a released name out again");
    check(!cetta_name_retain_weak(&g_quarantine, &g_quarantine_local,
                                  quarantine_once, first, first_generation),
          "a released name stays dead in quarantine");
    check(aborts(double_release_in_quarantine),
          "a double release is fatal in quarantine");
    /* Forced reuse: the one-name space hands the same name back at once,
     * and a stale weak handle meets the new incarnation's generation. */
    uint32_t name = 0u, again = 0u;
    uint64_t generation = 0u, later = 0u;
    check(cetta_name_acquire(&g_one, &g_one_local, one_once, &name, &generation),
          "forced reuse: first");
    cetta_name_release(&g_one, &g_one_local, one_once, name, 0u);
    check(cetta_name_acquire(&g_one, &g_one_local, one_once, &again, &later) &&
              again == name && later > generation,
          "forced reuse hands the same name with a larger generation");
    check(!cetta_name_retain_weak(&g_one, &g_one_local, one_once, name,
                                  generation),
          "a stale handle never upgrades to the reused name");
    check(atomic_load(&cetta_name_cell(&g_one, name)->owners) == 1u,
          "the refused upgrade leaves the owner count as it was");
    cetta_name_release(&g_one, &g_one_local, one_once, name, 0u);
}

/* Threads share a small space; at each barrier every live name in every
 * thread is distinct, and every acquisition of a name has a larger
 * generation than any earlier one. */
enum { SMALL_NAMES = 48, THREADS = 4, PER_THREAD = 10, ROUNDS = 2000 };
static CettaNameSpace g_small =
    CETTA_NAME_SPACE_INITIALIZER("small", 1u, SMALL_NAMES + 1u);
static _Thread_local CettaNameLocal g_small_local;
CETTA_NAME_SPACE_ONCE_FUNCTION(small_once, g_small)
static pthread_barrier_t g_barrier;
static uint32_t g_live[THREADS][PER_THREAD];
static _Atomic uint64_t g_last_generation[SMALL_NAMES + 1u];
static _Atomic int g_bad_history;
static _Atomic int g_bad_overlap;
static _Atomic int g_refused;

static void *uniqueness_thread(void *raw) {
    int t = (int)(intptr_t)raw;
    for (int round = 0; round < ROUNDS; round++) {
        for (int i = 0; i < PER_THREAD; i++) {
            uint32_t name = 0u;
            uint64_t generation = 0u;
            if (!cetta_name_acquire(&g_small, &g_small_local, small_once, &name,
                                    &generation)) {
                atomic_fetch_add(&g_refused, 1);
                g_live[t][i] = 0u;
                continue;
            }
            g_live[t][i] = name;
            uint64_t last = atomic_load(&g_last_generation[name]);
            if (generation <= last)
                atomic_fetch_add(&g_bad_history, 1);
            atomic_store(&g_last_generation[name], generation);
        }
        pthread_barrier_wait(&g_barrier);
        if (t == 0) {
            bool seen[SMALL_NAMES + 1u];
            memset(seen, 0, sizeof(seen));
            for (int u = 0; u < THREADS; u++)
                for (int i = 0; i < PER_THREAD; i++) {
                    uint32_t name = g_live[u][i];
                    if (!name)
                        continue;
                    if (seen[name])
                        atomic_fetch_add(&g_bad_overlap, 1);
                    seen[name] = true;
                }
        }
        pthread_barrier_wait(&g_barrier);
        for (int i = 0; i < PER_THREAD; i++)
            if (g_live[t][i])
                cetta_name_release(&g_small, &g_small_local, small_once,
                                   g_live[t][i], 0u);
        pthread_barrier_wait(&g_barrier);
    }
    return NULL;
}

static void test_cross_thread_uniqueness(void) {
    pthread_t threads[THREADS];
    pthread_barrier_init(&g_barrier, NULL, THREADS);
    for (int t = 0; t < THREADS; t++)
        pthread_create(&threads[t], NULL, uniqueness_thread,
                       (void *)(intptr_t)t);
    for (int t = 0; t < THREADS; t++)
        pthread_join(threads[t], NULL);
    pthread_barrier_destroy(&g_barrier);
    check(atomic_load(&g_bad_overlap) == 0,
          "names live at once in different threads are distinct");
    check(atomic_load(&g_bad_history) == 0,
          "each name's generations only grow across all threads");
    check(atomic_load(&g_refused) == 0,
          "40 live names fit the 48-name space without refusal");
}

/* Racing weak upgrades against release and reacquisition: at the end every
 * name is free again and can be taken once more. */
enum { RACE_NAMES = 8, RACE_ROUNDS = 200000 };
static CettaNameSpace g_race =
    CETTA_NAME_SPACE_INITIALIZER("race", 1u, RACE_NAMES + 1u);
static _Thread_local CettaNameLocal g_race_local;
CETTA_NAME_SPACE_ONCE_FUNCTION(race_once, g_race)
static _Atomic uint64_t g_published[RACE_NAMES + 1u];
static _Atomic int g_race_bad;

static void *race_owner(void *raw) {
    (void)raw;
    for (int round = 0; round < RACE_ROUNDS; round++) {
        uint32_t name = 0u;
        uint64_t generation = 0u;
        if (!cetta_name_acquire(&g_race, &g_race_local, race_once, &name,
                                &generation))
            continue;
        atomic_store(&g_published[name], generation);
        cetta_name_release(&g_race, &g_race_local, race_once, name,
                           generation + (uint64_t)(round & 7));
    }
    return NULL;
}

static void *race_upgrader(void *raw) {
    (void)raw;
    for (int round = 0; round < RACE_ROUNDS; round++) {
        uint32_t name = 1u + (uint32_t)(round % RACE_NAMES);
        uint64_t generation = atomic_load(&g_published[name]);
        if (!generation)
            continue;
        if (cetta_name_retain_weak(&g_race, &g_race_local, race_once, name,
                                   generation)) {
            if (cetta_name_generation(&g_race, name) != generation)
                atomic_fetch_add(&g_race_bad, 1);
            cetta_name_release(&g_race, &g_race_local, race_once, name, 0u);
        }
    }
    return NULL;
}

static void test_racing_upgrades(void) {
    pthread_t threads[4];
    pthread_create(&threads[0], NULL, race_owner, NULL);
    pthread_create(&threads[1], NULL, race_owner, NULL);
    pthread_create(&threads[2], NULL, race_upgrader, NULL);
    pthread_create(&threads[3], NULL, race_upgrader, NULL);
    for (int t = 0; t < 4; t++)
        pthread_join(threads[t], NULL);
    check(atomic_load(&g_race_bad) == 0,
          "a successful upgrade pins exactly the incarnation it named");
    bool none_owned = true;
    for (uint32_t name = 1u; name < g_race.next_fresh; name++)
        none_owned = none_owned && !cetta_name_is_owned(&g_race, name);
    check(none_owned, "no name is left owned after the race");
    uint32_t taken[RACE_NAMES];
    int count = 0;
    for (; count < RACE_NAMES; count++)
        if (!cetta_name_acquire(&g_race, &g_race_local, race_once,
                                &taken[count], NULL))
            break;
    check(count == RACE_NAMES, "every name can be taken again: none was lost");
    for (int i = 0; i < count; i++)
        cetta_name_release(&g_race, &g_race_local, race_once, taken[i], 0u);
    check(atomic_load(&g_race.stale_releases) == 0u,
          "the race released no name twice");
}

int main(void) {
    test_watermark_of_a_non_final_release();
    test_failed_weak_upgrade_as_last_release();
    test_limits();
    test_quarantine_and_forced_reuse();
    test_cross_thread_uniqueness();
    test_racing_upgrades();
    if (g_failures) {
        fprintf(stderr, "%d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    printf("PASS: reclaimable names (%d checks)\n", g_checks);
    return 0;
}
