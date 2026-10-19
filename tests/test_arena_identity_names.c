/*
 * Arena identities are reclaimable names.
 *
 * Properties under test:
 *   - an arena name is reused only after its arena is freed, so a process
 *     may create arenas past the size of the name space (the run below
 *     starts minting near its end and keeps going on reused names);
 *   - a name's next incarnation starts its reset epochs above every epoch
 *     the previous one used, so an (identity, reset epoch) pair never
 *     repeats;
 *   - a younger arena linked to an older generation owns the older name: it
 *     is not reused while the younger arena may take its atoms as settled;
 *   - allocating from a freed arena before arena_init is fatal, a second
 *     arena_free changes nothing, and arena_init opens the struct again;
 *   - concurrent creation and release from several threads never hands one
 *     name to two live arenas.
 */
#define _GNU_SOURCE
#include "atom.h"
#include "symbol.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

static int g_failures;
static int g_checks;

static void check(bool condition, const char *what) {
    g_checks++;
    if (!condition) {
        g_failures++;
        fprintf(stderr, "FAIL: %s\n", what);
    }
}

enum { NAME_DEAD = UINT32_MAX - 1u, NEAR_END = UINT32_MAX - 1u - 1000u };

/* Epochs each name's incarnations used, kept by name: LIFO reuse moves a
 * name between arena structs, so history by position would miss repeats. */
enum { HISTORY_SLOTS = 1u << 14 };
static uint32_t g_history_name[HISTORY_SLOTS];
static uint64_t g_history_epoch[HISTORY_SLOTS];

static uint64_t *history_last_epoch(uint32_t name) {
    uint32_t slot = (name * 2654435761u) & (HISTORY_SLOTS - 1u);
    while (g_history_name[slot] && g_history_name[slot] != name)
        slot = (slot + 1u) & (HISTORY_SLOTS - 1u);
    g_history_name[slot] = name;
    return &g_history_epoch[slot];
}

static void test_churn_past_the_end(void) {
    arena_identity_test_start_fresh_at(NEAR_END);
    /* 400 rounds of 500 live arenas: 200,000 arenas from the last 1,000
     * fresh names, which run out in the second round, so every later arena
     * takes a name an earlier one released. */
    enum { HELD = 500, ROUNDS = 400 };
    static Arena held[HELD];
    bool distinct = true, in_range = true, epochs_rise = true;
    uint64_t incarnations = 0;
    for (uint32_t round = 0; round < ROUNDS; round++) {
        for (uint32_t i = 0; i < HELD; i++) {
            arena_init(&held[i]);
            uint32_t name = held[i].identity;
            in_range = in_range && name != 0u && name < NAME_DEAD;
            /* A new incarnation starts above every epoch its name used. */
            uint64_t *last = history_last_epoch(name);
            epochs_rise = epochs_rise && held[i].reset_epoch > *last;
            Atom *atom = atom_symbol(&held[i], "churn");
            in_range = in_range && atom && atom->arena_id == name;
            /* A reset inside the incarnation advances its epoch too. */
            ArenaMark mark = arena_mark(&held[i]);
            (void)atom_symbol(&held[i], "reset-me");
            arena_reset(&held[i], mark);
            incarnations++;
        }
        for (uint32_t i = 0; i < HELD && distinct; i++)
            for (uint32_t j = i + 1u; j < HELD && distinct; j++)
                distinct = held[i].identity != held[j].identity;
        for (uint32_t i = 0; i < HELD; i++) {
            /* arena_free advances the epoch once more as it ends. */
            uint32_t name = held[i].identity;
            arena_free(&held[i]);
            *history_last_epoch(name) = held[i].reset_epoch;
        }
    }
    check(incarnations == (uint64_t)HELD * ROUNDS, "every round ran");
    check(in_range, "every name lies in the name space and stamps its atoms");
    check(distinct, "live arenas never share a name");
    check(epochs_rise, "a reused name starts above every epoch it used before");
}

static void test_older_generation_owns_its_name(void) {
    Arena older, young, other;
    arena_init(&older);
    arena_init(&young);
    (void)atom_symbol(&older, "old-atom");
    arena_set_older_generation(&young, &older);
    uint32_t older_name = older.identity;
    arena_free(&older);
    bool reused = false;
    for (int i = 0; i < 300; i++) {
        arena_init(&other);
        reused = reused || other.identity == older_name;
        arena_free(&other);
    }
    check(!reused, "a linked older name is not reused while the younger arena lives");
    check(young.older_identity == older_name,
          "the younger arena keeps naming its older generation");
    arena_free(&young);
    /* Both names are free now; this thread takes them back last-in first,
     * so two arenas live at once receive the younger and the older name. */
    Arena first, second;
    arena_init(&first);
    arena_init(&second);
    check(first.identity == older_name || second.identity == older_name,
          "the older name returns once the younger arena is freed");
    arena_free(&second);
    arena_free(&first);
}

/* Allocating from a freed arena before arena_init opens it again is fatal. */
static void allocate_from_freed_arena(void) {
    Arena a;
    arena_init(&a);
    arena_free(&a);
    (void)atom_symbol(&a, "after-free");
}

static void test_reuse_after_free(void) {
    fflush(stderr);
    pid_t pid = fork();
    if (pid == 0) {
        if (!freopen("/dev/null", "w", stderr))
            _exit(3);
        allocate_from_freed_arena();
        _exit(0);
    }
    int status = 0;
    check(pid > 0 && waitpid(pid, &status, 0) == pid && WIFSIGNALED(status) &&
              WTERMSIG(status) == SIGABRT,
          "allocating from a freed arena is fatal");
    /* arena_init opens the freed struct again: under the same name, its
     * epochs start above every epoch the ended incarnation used. */
    Arena a;
    arena_init(&a);
    (void)atom_symbol(&a, "first");
    uint32_t first = a.identity;
    uint64_t first_epoch = a.reset_epoch;
    arena_free(&a);
    check(a.identity == NAME_DEAD, "a freed arena is dead");
    arena_free(&a);
    check(arena_identity_test_stale_releases() == 0u,
          "freeing a dead arena releases nothing");
    arena_init(&a);
    Atom *atom = atom_symbol(&a, "second");
    check(a.identity != NAME_DEAD && a.identity != 0u && atom &&
              atom->arena_id == a.identity,
          "a reopened arena is named and stamps its atoms");
    check(a.identity != first || a.reset_epoch > first_epoch + 1u,
          "a reopened arena never repeats an earlier (identity, epoch)");
    arena_free(&a);
}

enum { THREADS = 4, THREAD_HELD = 64, THREAD_ROUNDS = 1500 };
static pthread_barrier_t g_barrier;
static uint32_t g_live[THREADS][THREAD_HELD];
static _Atomic int g_thread_bad;
static _Atomic int g_cross_overlap;

static int compare_names(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

static void *churn_thread(void *raw) {
    int t = (int)(intptr_t)raw;
    Arena held[THREAD_HELD];
    for (uint32_t round = 0; round < THREAD_ROUNDS; round++) {
        for (uint32_t i = 0; i < THREAD_HELD; i++) {
            arena_init(&held[i]);
            Atom *atom = atom_symbol(&held[i], "thread");
            if (!atom || atom->arena_id != held[i].identity ||
                held[i].identity == 0u || held[i].identity >= NAME_DEAD)
                atomic_fetch_add(&g_thread_bad, 1);
            g_live[t][i] = held[i].identity;
        }
        pthread_barrier_wait(&g_barrier);
        if (t == 0) {
            static uint32_t all[THREADS * THREAD_HELD];
            memcpy(all, g_live, sizeof(all));
            qsort(all, THREADS * THREAD_HELD, sizeof(all[0]), compare_names);
            for (uint32_t i = 1; i < THREADS * THREAD_HELD; i++)
                if (all[i] == all[i - 1u])
                    atomic_fetch_add(&g_cross_overlap, 1);
        }
        pthread_barrier_wait(&g_barrier);
        for (uint32_t i = 0; i < THREAD_HELD; i++)
            arena_free(&held[i]);
        pthread_barrier_wait(&g_barrier);
    }
    return NULL;
}

static void test_concurrent_churn(void) {
    pthread_t threads[THREADS];
    pthread_barrier_init(&g_barrier, NULL, THREADS);
    for (int t = 0; t < THREADS; t++) {
        if (pthread_create(&threads[t], NULL, churn_thread,
                           (void *)(intptr_t)t) != 0) {
            check(false, "thread creation");
            return;
        }
    }
    for (int t = 0; t < THREADS; t++)
        pthread_join(threads[t], NULL);
    pthread_barrier_destroy(&g_barrier);
    check(atomic_load(&g_thread_bad) == 0,
          "every thread's arenas are named and stamp their atoms");
    check(atomic_load(&g_cross_overlap) == 0,
          "arenas live at once in different threads never share a name");
    check(arena_identity_test_stale_releases() == 0u,
          "no name is released twice");
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    test_churn_past_the_end();
    test_older_generation_owns_its_name();
    test_reuse_after_free();
    test_concurrent_churn();
    if (g_failures) {
        fprintf(stderr, "%d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    printf("PASS: arena identities are reclaimable names (%d checks)\n",
           g_checks);
    return 0;
}
