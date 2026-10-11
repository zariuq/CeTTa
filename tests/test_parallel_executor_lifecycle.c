#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "parallel_executor.h"
#include "stats.h"

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    bool started;
    bool release;
    bool block;
} EpisodeGate;

typedef struct {
    EpisodeGate gate;
    atomic_uint entered;
    atomic_uint executed;
    atomic_uint left;
} Episode;

typedef struct {
    CettaParallelExecutor *executor;
    bool ok;
} RunRequest;

static pthread_key_t worker_lifetime;
static atomic_uint live_thread_states;
static unsigned shutdown_gate_calls;

static bool refuse_shutdown(void *context) {
    assert(context == &shutdown_gate_calls);
    shutdown_gate_calls++;
    return false;
}

static void worker_lifetime_end(void *value) {
    assert(value == &live_thread_states);
    atomic_fetch_sub_explicit(&live_thread_states, 1u, memory_order_relaxed);
}

static void episode_init(Episode *episode, bool block) {
    pthread_mutex_init(&episode->gate.mutex, NULL);
    pthread_cond_init(&episode->gate.condition, NULL);
    episode->gate.started = false;
    episode->gate.release = false;
    episode->gate.block = block;
    atomic_init(&episode->entered, 0u);
    atomic_init(&episode->executed, 0u);
    atomic_init(&episode->left, 0u);
}

static void episode_free(Episode *episode) {
    pthread_cond_destroy(&episode->gate.condition);
    pthread_mutex_destroy(&episode->gate.mutex);
}

static void episode_worker_enter(CettaParallelWorker *worker, void *user) {
    Episode *episode = user;
    (void)worker;
    if (!pthread_getspecific(worker_lifetime)) {
        assert(pthread_setspecific(worker_lifetime, &live_thread_states) == 0);
        atomic_fetch_add_explicit(&live_thread_states, 1u, memory_order_relaxed);
    }
    atomic_fetch_add_explicit(&episode->entered, 1u,
                              memory_order_relaxed);
}

static void episode_worker_leave(CettaParallelWorker *worker, void *user) {
    Episode *episode = user;
    (void)worker;
    atomic_fetch_add_explicit(&episode->left, 1u,
                              memory_order_relaxed);
}

static bool episode_task(CettaParallelWorker *worker, void *task,
                         void *user) {
    Episode *episode = user;
    (void)worker;
    assert(task == episode);
    assert(!cetta_parallel_executor_shutdown());
    atomic_fetch_add_explicit(&episode->executed, 1u,
                              memory_order_relaxed);

    pthread_mutex_lock(&episode->gate.mutex);
    episode->gate.started = true;
    pthread_cond_broadcast(&episode->gate.condition);
    while (episode->gate.block && !episode->gate.release) {
        pthread_cond_wait(&episode->gate.condition,
                          &episode->gate.mutex);
    }
    pthread_mutex_unlock(&episode->gate.mutex);
    return true;
}

static CettaParallelExecutorConfig episode_config(Episode *episode,
                                                   uint32_t thread_count) {
    return (CettaParallelExecutorConfig){
        .thread_count = thread_count,
        .prefer_persistent_workers = true,
        .user = episode,
        .task_fn = episode_task,
        .worker_enter = episode_worker_enter,
        .worker_leave = episode_worker_leave,
        .worker_failure_message = "lifecycle task failed",
    };
}

static void *run_request_main(void *opaque) {
    RunRequest *request = opaque;
    request->ok = cetta_parallel_executor_run(request->executor);
    return NULL;
}

static bool episode_wait_started(Episode *episode, long seconds) {
    struct timespec deadline;
    int wait_result = 0;

    assert(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
    deadline.tv_sec += seconds;
    pthread_mutex_lock(&episode->gate.mutex);
    while (!episode->gate.started && wait_result == 0) {
        wait_result = pthread_cond_timedwait(&episode->gate.condition,
                                             &episode->gate.mutex,
                                             &deadline);
    }
    bool started = episode->gate.started;
    pthread_mutex_unlock(&episode->gate.mutex);
    assert(wait_result == 0 || wait_result == ETIMEDOUT);
    return started;
}

static void episode_release(Episode *episode) {
    pthread_mutex_lock(&episode->gate.mutex);
    episode->gate.release = true;
    pthread_cond_broadcast(&episode->gate.condition);
    pthread_mutex_unlock(&episode->gate.mutex);
}

static void assert_episode_completed(const Episode *episode,
                                     unsigned expected_tasks) {
    unsigned entered = atomic_load_explicit(
        &episode->entered, memory_order_relaxed);
    unsigned executed = atomic_load_explicit(
        &episode->executed, memory_order_relaxed);
    unsigned left = atomic_load_explicit(
        &episode->left, memory_order_relaxed);
    assert(executed == expected_tasks);
    /* Worker admission is a realization choice, but every admitted worker
       must leave before the episode completes. */
    assert(entered == left);
}

static bool worker_exit_task(CettaParallelWorker *worker, void *task,
                             void *user) {
    (void)worker;
    (void)task;
    (void)user;
    exit(17);
}

static int run_worker_exit_child(void) {
    CettaParallelExecutor executor;
    CettaParallelExecutorConfig config = {
        .thread_count = 1u,
        .prefer_persistent_workers = true,
        .task_fn = worker_exit_task,
        .worker_failure_message = "worker exit task returned",
    };

    assert(cetta_parallel_executor_init(&executor, &config));
    assert(cetta_parallel_executor_push(&executor, &executor));
    (void)cetta_parallel_executor_run(&executor);
    return 99;
}

static void assert_worker_exit_does_not_deadlock(const char *self) {
    pid_t child = fork();
    int status = 0;

    assert(child >= 0);
    if (child == 0) {
        execl(self, self, "--worker-exit", (char *)NULL);
        _exit(127);
    }

    for (unsigned attempt = 0u; attempt < 500u; attempt++) {
        pid_t waited = waitpid(child, &status, WNOHANG);
        assert(waited >= 0);
        if (waited == child) {
            assert(WIFEXITED(status));
            assert(WEXITSTATUS(status) == 17);
            return;
        }
        struct timespec pause = {.tv_nsec = 10000000L};
        (void)nanosleep(&pause, NULL);
    }

    (void)kill(child, SIGKILL);
    (void)waitpid(child, &status, 0);
    assert(false && "worker-triggered process exit deadlocked");
}

typedef struct {
    Atom *root;
    Atom *child;
    Atom *leaf;
    uint32_t expected;
} SharedHash;

static void assert_hash_unpublished(const SharedHash *hash) {
    assert((hash->root->flags & ATOM_FLAG_HASH_VALID) == 0u);
    assert((hash->child->flags & ATOM_FLAG_HASH_VALID) == 0u);
    assert((hash->leaf->flags & ATOM_FLAG_HASH_VALID) == 0u);
}

static void hash_worker_hook(CettaParallelWorker *worker, void *user) {
    (void)worker;
    SharedHash *hash = user;
    assert(cetta_parallel_worker_active());
    assert(atom_hash(hash->root) == hash->expected);
    assert_hash_unpublished(hash);
}

static bool hash_worker_task(CettaParallelWorker *worker, void *task,
                              void *user) {
    assert(task == user);
    hash_worker_hook(worker, user);
    /* An inner scope must not restore publication while the episode's outer
     * scope still owns these concurrent reads. */
    atom_hash_read_scope_enter();
    hash_worker_hook(worker, user);
    atom_hash_read_scope_leave();
    hash_worker_hook(worker, user);
    return true;
}

static void test_shared_hash_cache(void) {
    for (unsigned persistent = 0u; persistent < 2u; persistent++) {
        Arena arena;
        arena_init(&arena);
        SharedHash hash = {0};
        hash.leaf = atom_int(&arena, 173);
        hash.child = atom_expr(&arena, &hash.leaf, 1u);
        hash.root = atom_expr2(&arena, hash.child, hash.leaf);
        assert_hash_unpublished(&hash);
        atom_hash_read_scope_enter();
        atom_hash_read_scope_enter();
        hash.expected = atom_hash(hash.root);
        atom_hash_read_scope_leave();
        assert(atom_hash(hash.root) == hash.expected);
        assert_hash_unpublished(&hash);
        atom_hash_read_scope_leave();

        CettaParallelExecutor executor;
        CettaParallelExecutorConfig config = {
            .thread_count = 3u,
            .prefer_persistent_workers = persistent != 0u,
            .user = &hash,
            .task_fn = hash_worker_task,
            .worker_enter = hash_worker_hook,
            .worker_leave = hash_worker_hook,
        };
        assert(cetta_parallel_executor_init(&executor, &config));
        for (unsigned task = 0u; task < 32u; task++)
            assert(cetta_parallel_executor_push(&executor, &hash));
        assert(cetta_parallel_executor_run(&executor));
        assert_hash_unpublished(&hash);
        cetta_parallel_executor_free(&executor);
        /* The joined coordinator retains its ordinary miss-cache behavior. */
        assert(atom_hash(hash.root) == hash.expected);
        assert((hash.root->flags & ATOM_FLAG_HASH_VALID) != 0u);
        assert((hash.child->flags & ATOM_FLAG_HASH_VALID) != 0u);
        assert((hash.leaf->flags & ATOM_FLAG_HASH_VALID) != 0u);
        atom_hash_read_scope_enter();
        assert(atom_hash(hash.root) == hash.expected);
        atom_hash_read_scope_leave();
        arena_free(&arena);
    }
    puts("PASS: worker hash reads preserve recursive shared metadata and nested scopes");
}

static void *compare_owned_state_cells(void *unused) {
    (void)unused;
    Arena arena;
    arena_init_detached(&arena);
    StateCell left = {.value = atom_int(&arena, 7)};
    StateCell right = {.value = atom_int(&arena, 7)};
    Atom *a = atom_state(&arena, &left);
    Atom *b = atom_state(&arena, &right);
    assert(atom_eq(a, b));
    right.value = atom_int(&arena, 8);
    assert(!atom_eq(a, b));
    right.value = left.value;
    assert(atom_eq(a, b));
    arena_free(&arena);
    return NULL;
}

static void test_state_comparison_thread_retirement(void) {
    for (unsigned i = 0u; i < 3u; i++) {
        pthread_t thread;
        assert(pthread_create(&thread, NULL,
                              compare_owned_state_cells, NULL) == 0);
        assert(pthread_join(thread, NULL) == 0);
    }
    puts("PASS: state comparisons reuse scratch and release it at thread retirement");
}

int main(int argc, char **argv) {
    CettaParallelExecutor empty_executor;
    CettaParallelExecutor first_executor;
    CettaParallelExecutor second_executor;
    CettaParallelExecutor growth_executor;
    CettaParallelExecutor shrink_executor;
    Episode empty_episode;
    Episode first_episode;
    Episode second_episode;
    Episode growth_episode;
    Episode shrink_episode;
    CettaParallelExecutorConfig config;
    RunRequest first_request = {0};
    RunRequest second_request = {0};
    pthread_t first_thread;
    pthread_t second_thread;

    if (argc == 2 && strcmp(argv[1], "--worker-exit") == 0)
        return run_worker_exit_child();
    atomic_init(&live_thread_states, 0u);
    assert(pthread_key_create(&worker_lifetime, worker_lifetime_end) == 0);
    assert_worker_exit_does_not_deadlock(argv[0]);
    test_shared_hash_cache();
    test_state_comparison_thread_retirement();

    cetta_runtime_stats_reset();
    cetta_runtime_stats_enable();

    episode_init(&empty_episode, false);
    config = episode_config(&empty_episode, 1u);
    assert(cetta_parallel_executor_init(&empty_executor, &config));
    assert(cetta_parallel_executor_run(&empty_executor));
    assert(!cetta_parallel_executor_run(&empty_executor));
    assert_episode_completed(&empty_episode, 0u);
    cetta_parallel_executor_free(&empty_executor);
    episode_free(&empty_episode);

    episode_init(&first_episode, true);
    config = episode_config(&first_episode, 1u);
    assert(cetta_parallel_executor_init(&first_executor, &config));
    assert(cetta_parallel_executor_push(&first_executor, &first_episode));
    first_request.executor = &first_executor;
    assert(pthread_create(&first_thread, NULL, run_request_main,
                          &first_request) == 0);
    assert(episode_wait_started(&first_episode, 5));
    assert(!cetta_parallel_executor_shutdown());
    assert(!cetta_parallel_executor_shutdown_if(refuse_shutdown,
                                                &shutdown_gate_calls));
    assert(shutdown_gate_calls == 0u);

    episode_init(&second_episode, true);
    config = episode_config(&second_episode, 1u);
    assert(cetta_parallel_executor_init(&second_executor, &config));
    assert(cetta_parallel_executor_push(&second_executor, &second_episode));
    second_request.executor = &second_executor;
    assert(pthread_create(&second_thread, NULL, run_request_main,
                          &second_request) == 0);
    assert(episode_wait_started(&second_episode, 5));
    /* The busy pool sends this independent episode to one-shot workers.
     * Refusal neither closes their admission nor retires either context. */
    assert(!cetta_parallel_executor_shutdown());
    cetta_parallel_executor_cancel(&second_executor);
    episode_release(&second_episode);
    episode_release(&first_episode);
    assert(pthread_join(first_thread, NULL) == 0);
    assert(pthread_join(second_thread, NULL) == 0);
    assert(first_request.ok);
    assert(second_request.ok);
    assert_episode_completed(&first_episode, 1u);
    assert_episode_completed(&second_episode, 1u);
    assert(!cetta_parallel_executor_shutdown_if(refuse_shutdown,
                                                &shutdown_gate_calls));
    assert(shutdown_gate_calls == 1u);

    episode_init(&growth_episode, false);
    config = episode_config(&growth_episode, 3u);
    assert(cetta_parallel_executor_init(&growth_executor, &config));
    for (uint32_t i = 0u; i < 3u; i++)
        assert(cetta_parallel_executor_push(&growth_executor,
                                            &growth_episode));
    assert(cetta_parallel_executor_run(&growth_executor));
    assert_episode_completed(&growth_episode, 3u);

    episode_init(&shrink_episode, false);
    config = episode_config(&shrink_episode, 2u);
    assert(cetta_parallel_executor_init(&shrink_executor, &config));
    for (uint32_t i = 0u; i < 2u; i++)
        assert(cetta_parallel_executor_push(&shrink_executor,
                                            &shrink_episode));
    assert(cetta_parallel_executor_run(&shrink_executor));
    assert_episode_completed(&shrink_episode, 2u);

#if CETTA_BUILD_WITH_RUNTIME_STATS
    {
        CettaRuntimeStats stats;
        cetta_runtime_stats_snapshot(&stats);
        uint64_t attempts = stats.counters[
            CETTA_RUNTIME_COUNTER_PARALLEL_PERSISTENT_EPISODE_ATTEMPT];
        uint64_t commits = stats.counters[
            CETTA_RUNTIME_COUNTER_PARALLEL_PERSISTENT_EPISODE_COMMIT];
        uint64_t completes = stats.counters[
            CETTA_RUNTIME_COUNTER_PARALLEL_PERSISTENT_EPISODE_COMPLETE];
        uint64_t reuses = stats.counters[
            CETTA_RUNTIME_COUNTER_PARALLEL_PERSISTENT_EPISODE_REUSE];
        assert(attempts >= commits);
        assert(commits == completes);
        assert(reuses <= commits);
    }
#endif

    cetta_parallel_executor_free(&first_executor);
    cetta_parallel_executor_free(&second_executor);
    cetta_parallel_executor_free(&growth_executor);
    cetta_parallel_executor_free(&shrink_executor);
    episode_free(&first_episode);
    episode_free(&second_episode);
    episode_free(&growth_episode);
    episode_free(&shrink_episode);
    cetta_runtime_stats_disable();
    assert(atomic_load_explicit(&live_thread_states, memory_order_relaxed) > 0u);
    assert(cetta_parallel_executor_shutdown());
    assert(atomic_load_explicit(&live_thread_states, memory_order_relaxed) == 0u);
    assert(cetta_parallel_executor_shutdown());
    Episode refused;
    CettaParallelExecutor refused_executor;
    episode_init(&refused, false);
    config = episode_config(&refused, 1u);
    assert(cetta_parallel_executor_init(&refused_executor, &config));
    assert(cetta_parallel_executor_push(&refused_executor, &refused));
    assert(!cetta_parallel_executor_run(&refused_executor));
    assert(!refused_executor.run_started);
    assert_episode_completed(&refused, 0u);
    cetta_parallel_executor_free(&refused_executor);
    episode_free(&refused);
    assert(pthread_key_delete(worker_lifetime) == 0);
    puts("PASS: final shutdown refuses live and cancelled work, joins thread state,"
         " and seals admission once");
    puts("PASS: parallel executor preserves completion, lifecycle balance,"
         " and worker-exit liveness");
    return 0;
}
