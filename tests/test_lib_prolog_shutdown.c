#define _POSIX_C_SOURCE 200809L

#include "lib_prolog.h"
#include "parallel_executor.h"
#include "symbol.h"

#include <SWI-Prolog.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct {
    CettaLibPrologRuntime *runtime;
    Atom *goal;
    Atom *truth;
    Atom *projection;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    bool started;
    bool release;
    bool install;
    bool returned;
} ForeignEpisode;

static _Thread_local ForeignEpisode *active_episode;
static _Thread_local Arena *active_arena;
static pthread_key_t lifetime_key;
static atomic_uint live_threads;
static unsigned finalizers;

static void query_one(ForeignEpisode *episode, Arena *arena, Atom *goal) {
    Atom *answers = NULL;
    assert(cetta_lib_prolog_query(episode->runtime, arena, goal,
        episode->projection, &answers) == CETTA_LIB_PROLOG_QUERY_OK);
    assert(answers && answers->kind == ATOM_EXPR && answers->expr.len == 1u);
    assert(atom_eq(answers->expr.elems[0], episode->projection));
}

static foreign_t shutdown_probe(void) {
    ForeignEpisode *episode = active_episode;
    assert(episode && active_arena);
    assert(!cetta_lib_prolog_global_shutdown());
    query_one(episode, active_arena, episode->truth);
    assert(!cetta_lib_prolog_global_shutdown());
    pthread_mutex_lock(&episode->mutex);
    episode->started = true;
    pthread_cond_broadcast(&episode->condition);
    while (!episode->release)
        pthread_cond_wait(&episode->condition, &episode->mutex);
    pthread_mutex_unlock(&episode->mutex);
    return true;
}

static int engine_finalizer(int status, void *unused) {
    (void)status;
    (void)unused;
    assert(atomic_load_explicit(&live_threads, memory_order_relaxed) == 0u);
    assert(cetta_lib_prolog_runtime_new() == NULL);
    /* Cleanup's foreign finalizer may ask again: it must not deadlock or
     * recursively clean the same engine. */
    assert(!cetta_lib_prolog_global_shutdown());
    finalizers++;
    return 0;
}

static void install_probe(void) {
    bool claimed = PL_current_engine() == NULL;
    if (claimed) assert(PL_set_engine(PL_ENGINE_MAIN, NULL) == PL_ENGINE_SET);
    assert(PL_register_foreign_in_module("user", "shutdown_probe", 0,
                                        (pl_function_t)shutdown_probe, 0));
    PL_on_halt(engine_finalizer, NULL);
    if (claimed) assert(PL_set_engine(NULL, NULL) == PL_ENGINE_SET);
}

static void thread_retired(void *value) {
    assert(value == &live_threads);
    /* Both the final join and a one-shot episode retain their admission
     * ownership while destructors run. */
    assert(!cetta_parallel_executor_shutdown());
    assert(!cetta_lib_prolog_global_shutdown());
    atomic_fetch_sub_explicit(&live_threads, 1u, memory_order_relaxed);
}

static bool foreign_task(CettaParallelWorker *worker, void *task, void *user) {
    ForeignEpisode *episode = user;
    assert(task == episode);
    assert(pthread_setspecific(lifetime_key, &live_threads) == 0);
    atomic_fetch_add_explicit(&live_threads, 1u, memory_order_relaxed);
    assert(cetta_lib_prolog_runtime_available(episode->runtime));
    if (episode->install) install_probe();
    active_episode = episode;
    active_arena = cetta_parallel_worker_arena(worker);
    query_one(episode, active_arena, episode->goal);
    query_one(episode, active_arena, episode->truth);
    active_episode = NULL;
    active_arena = NULL;
    episode->returned = true;
    return true;
}

typedef struct {
    CettaParallelExecutor *executor;
    bool result;
} Run;

static void *run_episode(void *argument) {
    Run *run = argument;
    run->result = cetta_parallel_executor_run(run->executor);
    return NULL;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    bool worker_first = strcmp(argv[1], "main-first") != 0;
    bool persistent = strcmp(argv[1], "one-shot") != 0;
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    Arena arena;
    arena_init(&arena);
    assert(pthread_key_create(&lifetime_key, thread_retired) == 0);
    atomic_init(&live_threads, 0u);

    ForeignEpisode episode = {
        .runtime = cetta_lib_prolog_runtime_new(),
        .goal = atom_symbol(&arena, "shutdown_probe"),
        .truth = atom_symbol(&arena, "true"),
        .projection = atom_int(&arena, 41),
        .mutex = PTHREAD_MUTEX_INITIALIZER,
        .condition = PTHREAD_COND_INITIALIZER,
        .install = worker_first,
    };
    assert(episode.runtime);
    assert(!cetta_lib_prolog_global_shutdown());
    CettaLibPrologRuntime *second = cetta_lib_prolog_runtime_new();
    assert(second);
    cetta_lib_prolog_runtime_free(second);
    if (!worker_first) {
        assert(cetta_lib_prolog_runtime_available(episode.runtime));
        install_probe();
    }
    CettaParallelExecutor executor;
    CettaParallelExecutorConfig config = {
        .thread_count = 1u,
        .prefer_persistent_workers = persistent,
        .task_fn = foreign_task,
        .user = &episode,
    };
    assert(cetta_parallel_executor_init(&executor, &config));
    assert(cetta_parallel_executor_push(&executor, &episode));
    Run run = {.executor = &executor};
    pthread_t coordinator;
    assert(pthread_create(&coordinator, NULL, run_episode, &run) == 0);
    struct timespec deadline;
    assert(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
    deadline.tv_sec += 30;
    pthread_mutex_lock(&episode.mutex);
    int waited = 0;
    while (!episode.started && waited == 0)
        waited = pthread_cond_timedwait(&episode.condition, &episode.mutex,
                                        &deadline);
    assert(waited == 0 && episode.started);
    assert(!cetta_parallel_executor_shutdown());
    assert(!cetta_lib_prolog_global_shutdown());
    /* Refusal must leave context admission and the nested query usable. */
    second = cetta_lib_prolog_runtime_new();
    assert(second);
    cetta_lib_prolog_runtime_free(second);
    cetta_parallel_executor_cancel(&executor);
    episode.release = true;
    pthread_cond_broadcast(&episode.condition);
    pthread_mutex_unlock(&episode.mutex);
    assert(pthread_join(coordinator, NULL) == 0);
    assert(run.result && episode.returned);
    assert(!cetta_lib_prolog_global_shutdown());
    query_one(&episode, &arena, episode.truth);
    cetta_parallel_executor_free(&executor);
    cetta_lib_prolog_runtime_free(episode.runtime);
    arena_free(&arena);
    assert(cetta_lib_prolog_global_shutdown());
    assert(atomic_load_explicit(&live_threads, memory_order_relaxed) == 0u);
    assert(finalizers == 1u);
    assert(cetta_lib_prolog_global_shutdown());
    assert(finalizers == 1u && cetta_lib_prolog_runtime_new() == NULL);
    assert(pthread_key_delete(lifetime_key) == 0);
    pthread_cond_destroy(&episode.condition);
    pthread_mutex_destroy(&episode.mutex);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    printf("PASS: libpl shutdown %s preserves busy/nested/cancelled work and reclaims after thread retirement\n", argv[1]);
    return 0;
}
