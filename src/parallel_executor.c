/*
 * parallel_executor.c — the shared worker-pool substrate for threaded
 * reduction.  One episode owns its ready queue, task payloads, scratch
 * arenas, cancellation state, and completion receipt.  Operating-system
 * workers may persist across completed episodes; their entry and leave hooks
 * still bracket every episode independently.  Nested execution uses the
 * one-shot realization so a bounded persistent pool can never wait on itself.
 *
 * Workers may push follow-up tasks while running, and an episode queue closes
 * itself when it is empty with no task in flight.  The first failure is
 * recorded and wakes every episode worker.  run() waits for all participating
 * workers before the coordinator reads any task result.  Both rho executors
 * and Hyperpose are clients.
 */

#include "parallel_executor.h"
#include "stats.h"
#include "native_memory.h"

#include <pthread.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PARALLEL_WORKER_ARENA_RESERVE (4u * ARENA_BLOCK_SIZE)
#define PERSISTENT_WORKER_STACK_BYTES (16u * 1024u * 1024u)

typedef struct { CettaRuntimeStatsOwner *previous; } ParallelStatsScope;
static void parallel_stats_restore(ParallelStatsScope *scope) {
    cetta_runtime_stats_owner_exchange(scope->previous);
}

typedef struct CettaPersistentPoolWorker CettaPersistentPoolWorker;

typedef enum {
    CETTA_PERSISTENT_POOL_RUN_COMPLETE = 0,
    CETTA_PERSISTENT_POOL_RUN_BUSY,
    CETTA_PERSISTENT_POOL_RUN_UNAVAILABLE,
} CettaPersistentPoolRunResult;

struct CettaPersistentPoolWorker {
    pthread_t thread;
    uint32_t index;
    uint64_t observed_generation;
};

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    pthread_mutex_t episode_mutex;
    CettaPersistentPoolWorker **workers;
    uint32_t worker_count;
    uint32_t worker_capacity;
    uint64_t generation;
    CettaParallelExecutor *active_executor;
    bool stopping;
} CettaPersistentWorkerPool;

static CettaPersistentWorkerPool g_persistent_worker_pool = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .condition = PTHREAD_COND_INITIALIZER,
    .episode_mutex = PTHREAD_MUTEX_INITIALIZER,
};
static pthread_once_t g_persistent_worker_pool_atexit_once =
    PTHREAD_ONCE_INIT;
static _Thread_local uint32_t g_parallel_worker_depth = 0u;

static void parallel_ready_queue_init(CettaParallelReadyQueue *queue) {
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->cond, NULL);
    queue->head = NULL;
    queue->tail = NULL;
    queue->spare = NULL;
    queue->queued = 0;
    queue->active = 0;
    queue->stopped = false;
}

static void parallel_ready_queue_free(CettaParallelReadyQueue *queue,
                                      void (*discard)(void *, void *),
                                      void *user) {
    CettaParallelTaskNode *node = queue->head;
    while (node) {
        CettaParallelTaskNode *next = node->next;
        /* A task that never ran ends here, and so does its supply. */
        if (discard)
            discard(node->task, user);
        cetta_name_supply_dispose(&node->names);
        free(node);
        node = next;
    }
    for (node = queue->spare; node;) {
        CettaParallelTaskNode *next = node->next;
        free(node);
        node = next;
    }
    pthread_cond_destroy(&queue->cond);
    pthread_mutex_destroy(&queue->mutex);
    queue->head = NULL;
    queue->tail = NULL;
    queue->spare = NULL;
}

/* Queue TASK with a supply: NAMES, which the queue takes over, or, when
   NAMES is NULL, the next push of the calling thread's supply, made here
   under the lock so that it goes straight into the node and the thread
   pushes nothing when the queue refuses the task. */
static inline __attribute__((always_inline)) bool parallel_ready_queue_push(
    CettaParallelReadyQueue *queue, void *task, const CettaNameSupply *names) {
    pthread_mutex_lock(&queue->mutex);
    if (queue->stopped) {
        pthread_mutex_unlock(&queue->mutex);
        return false;
    }
    CettaParallelTaskNode *node = queue->spare;
    if (node) {
        queue->spare = node->next;
    } else {
        node = cetta_malloc(sizeof(CettaParallelTaskNode));
        if (!node) {
            pthread_mutex_unlock(&queue->mutex);
            return false;
        }
    }
    node->task = task;
    if (names)
        node->names = *names;
    else
        cetta_name_supply_push_into(cetta_name_supply_current(), &node->names);
    node->next = NULL;
    if (queue->tail) {
        queue->tail->next = node;
    } else {
        queue->head = node;
    }
    queue->tail = node;
    queue->queued++;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_PARALLEL_QUEUE_PUSH);
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_PARALLEL_QUEUE_DEPTH_PEAK, queue->queued);
    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
    return true;
}

static void *parallel_ready_queue_pop(CettaParallelReadyQueue *queue,
                                      CettaNameSupply *names) {
    CettaParallelTaskNode *node;
    void *task;

    pthread_mutex_lock(&queue->mutex);
    while (!queue->head && !queue->stopped) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_PARALLEL_QUEUE_WAIT);
        pthread_cond_wait(&queue->cond, &queue->mutex);
    }
    if (queue->stopped) {
        pthread_mutex_unlock(&queue->mutex);
        return NULL;
    }
    node = queue->head;
    queue->head = node->next;
    if (!queue->head) queue->tail = NULL;
    if (queue->queued > 0) queue->queued--;
    queue->active++;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_PARALLEL_QUEUE_POP);
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_PARALLEL_QUEUE_ACTIVE_PEAK, queue->active);
    task = node->task;
    *names = node->names;
    node->next = queue->spare;
    queue->spare = node;
    pthread_mutex_unlock(&queue->mutex);
    return task;
}

static void parallel_ready_queue_task_done(CettaParallelReadyQueue *queue) {
    pthread_mutex_lock(&queue->mutex);
    if (queue->active > 0) queue->active--;
    if (!queue->head && queue->active == 0) {
        queue->stopped = true;
        pthread_cond_broadcast(&queue->cond);
    }
    pthread_mutex_unlock(&queue->mutex);
}

static void parallel_ready_queue_cancel(CettaParallelReadyQueue *queue) {
    pthread_mutex_lock(&queue->mutex);
    queue->stopped = true;
    pthread_cond_broadcast(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
}

static void parallel_ready_queue_fail(CettaParallelReadyQueue *queue) {
    pthread_mutex_lock(&queue->mutex);
    queue->stopped = true;
    pthread_cond_broadcast(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
}

static void parallel_ready_queue_close_if_idle(
        CettaParallelReadyQueue *queue) {
    pthread_mutex_lock(&queue->mutex);
    if (!queue->head && queue->active == 0u) {
        queue->stopped = true;
        pthread_cond_broadcast(&queue->cond);
    }
    pthread_mutex_unlock(&queue->mutex);
}

/* The coordinator is the thread that makes the executor.  Its supply's
   place -- its block, its share and its pushes -- is kept, and the clocks of
   the supplies that end on its thread go to the executor, until the
   executor is freed. */
static void parallel_executor_coordinator_hold(CettaParallelExecutor *executor) {
    CettaNameSupply *coordinator = cetta_name_supply_current();
    executor->coordinator = coordinator;
    executor->coordinator_place = cetta_name_supply_copy(coordinator);
    executor->coordinator_ending_clocks =
        cetta_name_supply_ending_clocks_install(executor->ending_clocks);
    executor->coordinator_held = true;
}

/* Every task's supply has ended: the coordinator's pushes handed on parts
   of its share, and the parts are all back, so it has its place again (the
   join of the pieces of a share is the share), at the latest clocks any of
   its tasks reached, so that nothing drawn meanwhile is drawn again. */
static void parallel_executor_coordinator_give_back(
    CettaParallelExecutor *executor) {
    if (!executor->coordinator_held)
        return;
    if (executor->workers)
        for (uint32_t i = 0; i < executor->thread_count; i++)
            for (unsigned kind = 0u; kind < CETTA_NAME_KIND_COUNT; kind++)
                if (executor->workers[i].ending_clocks[kind] >
                    executor->ending_clocks[kind])
                    executor->ending_clocks[kind] =
                        executor->workers[i].ending_clocks[kind];
    CettaNameSupply *coordinator = executor->coordinator;
    const CettaNameSupply *place = &executor->coordinator_place;
    CettaNameBlock *left = coordinator->block;
    if (place->block == &cetta_name_block_unplaced) {
        cetta_name_supply_init_unplaced(coordinator);
    } else {
        coordinator->block = place->block;
        coordinator->share = place->share;
        coordinator->pushed = place->pushed;
        coordinator->pushes = place->pushes;
        /* A coordinator that spawned drew nothing from its share since,
           and goes on from the clocks it spawned at. */
        if (executor->coordinator_spawned)
            for (unsigned kind = 0u; kind < CETTA_NAME_KIND_COUNT; kind++)
                coordinator->drawn[kind] = executor->spawned_clocks[kind];
        for (unsigned kind = 0u; kind < CETTA_NAME_KIND_COUNT; kind++)
            if (executor->ending_clocks[kind] > coordinator->drawn[kind])
                coordinator->drawn[kind] = executor->ending_clocks[kind];
    }
    if (left)
        cetta_name_block_release(left);
    cetta_name_supply_ending_clocks_install(executor->coordinator_ending_clocks);
    executor->coordinator_held = false;
}

bool cetta_parallel_executor_init(CettaParallelExecutor *executor,
                                  const CettaParallelExecutorConfig *config) {
    if (!executor || !config || !config->task_fn ||
        config->thread_count == 0u) {
        return false;
    }
    memset(executor, 0, sizeof(*executor));
    parallel_executor_coordinator_hold(executor);
    executor->memory = cetta_native_memory_current ? cetta_native_memory_current() : NULL;
    executor->names = cetta_name_context_current();
    executor->stats = cetta_runtime_stats_owner_current();
    executor->config = *config;
    executor->thread_count = config->thread_count;
    parallel_ready_queue_init(&executor->queue);
    pthread_mutex_init(&executor->error_mutex, NULL);
    pthread_mutex_init(&executor->completion_mutex, NULL);
    pthread_cond_init(&executor->completion_cond, NULL);
    executor->threads =
        cetta_malloc(sizeof(pthread_t) * executor->thread_count);
    executor->workers =
        calloc(executor->thread_count, sizeof(CettaParallelWorker));
    if (!executor->threads || !executor->workers) {
        cetta_parallel_executor_free(executor); return false;
    }
    for (uint32_t i = 0; i < executor->thread_count; i++) {
        executor->workers[i].executor = executor;
        executor->workers[i].index = i;
        arena_init(&executor->workers[i].arena);
        arena_reserve(&executor->workers[i].arena,
                      PARALLEL_WORKER_ARENA_RESERVE);
        if (cetta_native_memory_failed && cetta_native_memory_failed()) {
            cetta_parallel_executor_free(executor); return false;
        }
        arena_set_hashcons(&executor->workers[i].arena, NULL);
        arena_set_runtime_kind(&executor->workers[i].arena,
                               CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    }
    return true;
}

void cetta_parallel_executor_free(CettaParallelExecutor *executor) {
    if (!executor) return;
    __attribute__((cleanup(parallel_stats_restore))) ParallelStatsScope stats = {
        cetta_runtime_stats_owner_exchange(executor->stats)};
    parallel_ready_queue_free(&executor->queue, executor->config.task_discard,
                              executor->config.user);
    parallel_executor_coordinator_give_back(executor);
    if (executor->workers) {
        for (uint32_t i = 0; i < executor->thread_count; i++) {
            arena_free(&executor->workers[i].arena);
        }
    }
    free(executor->workers);
    free(executor->threads);
    pthread_cond_destroy(&executor->completion_cond);
    pthread_mutex_destroy(&executor->completion_mutex);
    pthread_mutex_destroy(&executor->error_mutex);
    executor->workers = NULL;
    executor->threads = NULL;
    executor->thread_count = 0;
    executor->completed_workers = 0;
    executor->run_started = false;
}

bool cetta_parallel_executor_push(CettaParallelExecutor *executor, void *task) {
    if (!executor || !task) return false;
    /* The pusher owns its supply: the queue's lock only orders the node. */
    return parallel_ready_queue_push(&executor->queue, task, NULL);
}

bool cetta_parallel_executor_push_supply(CettaParallelExecutor *executor,
                                         void *task, CettaNameSupply *names) {
    if (executor && task &&
        parallel_ready_queue_push(&executor->queue, task, names))
        return true;
    cetta_name_supply_dispose(names);
    return false;
}

void cetta_parallel_executor_spawn_begin(CettaParallelExecutor *executor,
                                         CettaParallelSpawn *spawn,
                                         uint32_t count) {
    CettaNameSupply *coordinator = executor->coordinator;
    cetta_name_supply_pieces_begin(&spawn->pieces, coordinator, count);
    spawn->left = count;
    /* The share is the pieces' now: the coordinator holds none until the
       executor gives its place back. */
    if (!executor->coordinator_spawned) {
        for (unsigned kind = 0u; kind < CETTA_NAME_KIND_COUNT; kind++)
            executor->spawned_clocks[kind] = coordinator->drawn[kind];
        executor->coordinator_spawned = true;
    }
    CettaNameBlock *block = coordinator->block;
    cetta_name_supply_init_unplaced(coordinator);
    if (block)
        cetta_name_block_release(block);
}

bool cetta_parallel_executor_spawn_push(CettaParallelExecutor *executor,
                                        CettaParallelSpawn *spawn, void *task) {
    if (spawn->left == 0u)
        return false;
    spawn->left--;
    CettaNameSupply piece = cetta_name_supply_pieces_next(&spawn->pieces);
    return cetta_parallel_executor_push_supply(executor, task, &piece);
}

void cetta_parallel_executor_spawn_end(CettaParallelSpawn *spawn) {
    cetta_name_supply_pieces_end(&spawn->pieces);
    spawn->left = 0u;
}

void cetta_parallel_executor_cancel(CettaParallelExecutor *executor) {
    if (!executor) return;
    parallel_ready_queue_cancel(&executor->queue);
}

void cetta_parallel_executor_fail(CettaParallelExecutor *executor,
                                  const char *fmt, ...) {
    va_list ap;
    if (!executor) return;
    pthread_mutex_lock(&executor->error_mutex);
    if (!executor->error[0]) {
        va_start(ap, fmt);
        vsnprintf(executor->error, sizeof(executor->error), fmt, ap);
        va_end(ap);
    }
    pthread_mutex_unlock(&executor->error_mutex);
    parallel_ready_queue_fail(&executor->queue);
}

const char *cetta_parallel_executor_error(const CettaParallelExecutor *executor) {
    if (!executor || !executor->error[0]) return NULL;
    return executor->error;
}

bool cetta_parallel_executor_failed(const CettaParallelExecutor *executor) {
    if (!executor) return true;
    return executor->error[0] != '\0';
}

static void parallel_worker_run_episode(CettaParallelWorker *worker) {
    CettaParallelExecutor *executor = worker->executor;
    CettaNativeMemoryScope memory = {0};
    if (cetta_native_memory_enter) cetta_native_memory_enter(&memory, executor->memory);
    CettaNameContext previous_names = cetta_name_context_current();
    cetta_name_context_install(&executor->names);
    CettaRuntimeStatsOwner *previous_stats = cetta_runtime_stats_owner_exchange(executor->stats);

    g_parallel_worker_depth++;
    /* Each name a task draws comes from the supply of the task.  Between
       tasks the worker has a supply of its own, outside the split tree, in
       which nothing is drawn.  The clocks of the supplies that end here are
       the worker's, handed to the coordinator when the executor ends. */
    CettaNameSupply between_tasks;
    cetta_name_supply_init_unplaced(&between_tasks);
    CettaNameSupply *previous_supply =
        cetta_name_supply_install(&between_tasks);
    uint64_t *previous_clocks =
        cetta_name_supply_ending_clocks_install(worker->ending_clocks);
    if (executor->config.worker_enter) {
        executor->config.worker_enter(worker, executor->config.user);
    }
    for (;;) {
        CettaNameSupply task_names = {0};
        void *task = parallel_ready_queue_pop(&executor->queue, &task_names);
        bool ok;
        if (!task) break;
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_PARALLEL_WORKER_TASK);
        cetta_name_supply_install(&task_names);
        ok = executor->config.task_fn(worker, task, executor->config.user);
        cetta_name_supply_install(&between_tasks);
        cetta_name_supply_dispose(&task_names);
        if (!ok) {
            const char *message = executor->config.worker_failure_message
                ? executor->config.worker_failure_message
                : "parallel worker failed";
            cetta_parallel_executor_fail(executor, "%s", message);
        }
        parallel_ready_queue_task_done(&executor->queue);
    }
    if (executor->config.worker_leave) {
        executor->config.worker_leave(worker, executor->config.user);
    }
    cetta_name_supply_install(previous_supply);
    cetta_name_supply_dispose(&between_tasks);
    cetta_name_supply_ending_clocks_install(previous_clocks);
    g_parallel_worker_depth--;
    cetta_runtime_stats_owner_exchange(previous_stats);
    cetta_name_context_install(&previous_names);
    if (cetta_native_memory_leave) cetta_native_memory_leave(&memory);
    pthread_mutex_lock(&executor->completion_mutex);
    executor->completed_workers++;
    pthread_cond_broadcast(&executor->completion_cond);
    pthread_mutex_unlock(&executor->completion_mutex);
}

static void parallel_wait_for_workers(CettaParallelExecutor *executor, uint32_t count) {
    pthread_mutex_lock(&executor->completion_mutex);
    while (executor->completed_workers < count) {
        if (!executor->config.interrupt) {
            pthread_cond_wait(&executor->completion_cond, &executor->completion_mutex);
            continue;
        }
        struct timespec deadline;
        timespec_get(&deadline, TIME_UTC);
        deadline.tv_nsec += 20000000L;
        if (deadline.tv_nsec >= 1000000000L) { deadline.tv_nsec -= 1000000000L; deadline.tv_sec++; }
        pthread_cond_timedwait(&executor->completion_cond, &executor->completion_mutex, &deadline);
        /* Host callbacks run without executor locks. */
        pthread_mutex_unlock(&executor->completion_mutex);
        if (executor->config.interrupt(executor->config.user))
            cetta_parallel_executor_cancel(executor);
        pthread_mutex_lock(&executor->completion_mutex);
    }
    pthread_mutex_unlock(&executor->completion_mutex);
}

static void *parallel_one_shot_worker_main(void *arg) {
    parallel_worker_run_episode((CettaParallelWorker *)arg);
    return NULL;
}

static void persistent_worker_pool_shutdown(void);

static void persistent_worker_pool_register_shutdown(void) {
    (void)atexit(persistent_worker_pool_shutdown);
}

static void *persistent_worker_pool_main(void *raw_worker) {
    CettaPersistentPoolWorker *pool_worker = raw_worker;
    CettaPersistentWorkerPool *pool = &g_persistent_worker_pool;

    for (;;) {
        CettaParallelExecutor *executor = NULL;
        pthread_mutex_lock(&pool->mutex);
        while (!pool->stopping &&
               pool_worker->observed_generation == pool->generation) {
            pthread_cond_wait(&pool->condition, &pool->mutex);
        }
        if (pool->stopping) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }
        pool_worker->observed_generation = pool->generation;
        if (pool->active_executor &&
            pool_worker->index < pool->active_executor->thread_count) {
            executor = pool->active_executor;
        }
        pthread_mutex_unlock(&pool->mutex);

        if (!executor)
            continue;
        parallel_worker_run_episode(
            &executor->workers[pool_worker->index]);
    }
    return NULL;
}

static bool persistent_worker_pool_ensure(
        uint32_t requested, bool *out_reused) {
    CettaPersistentWorkerPool *pool = &g_persistent_worker_pool;
    pthread_attr_t attr;
    bool attr_initialized = false;
    bool ok = true;

    if (!out_reused || requested == 0u)
        return false;
    pthread_once(&g_persistent_worker_pool_atexit_once,
                 persistent_worker_pool_register_shutdown);
    if (pthread_attr_init(&attr) != 0)
        return false;
    attr_initialized = true;
    if (pthread_attr_setstacksize(
            &attr, (size_t)PERSISTENT_WORKER_STACK_BYTES) != 0) {
        pthread_attr_destroy(&attr);
        return false;
    }

    pthread_mutex_lock(&pool->mutex);
    if (pool->stopping) {
        ok = false;
        goto done;
    }
    *out_reused = pool->worker_count >= requested;
    if (requested > pool->worker_capacity) {
        uint32_t next_capacity = pool->worker_capacity
            ? pool->worker_capacity : 4u;
        while (next_capacity < requested) {
            if (next_capacity > UINT32_MAX / 2u) {
                next_capacity = requested;
                break;
            }
            next_capacity *= 2u;
        }
        CettaPersistentPoolWorker **next = cetta_realloc(
            pool->workers,
            sizeof(CettaPersistentPoolWorker *) *
                (size_t)next_capacity);
        if (!next) {
            ok = false;
            goto done;
        }
        pool->workers = next;
        pool->worker_capacity = next_capacity;
    }
    while (pool->worker_count < requested) {
        CettaPersistentPoolWorker *worker =
            cetta_malloc(sizeof(*worker));
        if (!worker) { ok = false; break; }
        worker->index = pool->worker_count;
        worker->observed_generation = pool->generation;
        if (pthread_create(&worker->thread, &attr,
                           persistent_worker_pool_main, worker) != 0) {
            free(worker);
            ok = false;
            break;
        }
        pool->workers[pool->worker_count++] = worker;
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PARALLEL_PERSISTENT_THREAD_START);
    }
    if (pool->worker_count < requested)
        ok = false;

done:
    pthread_mutex_unlock(&pool->mutex);
    if (attr_initialized)
        pthread_attr_destroy(&attr);
    return ok;
}

static CettaPersistentPoolRunResult persistent_worker_pool_run(
        CettaParallelExecutor *executor) {
    CettaPersistentWorkerPool *pool = &g_persistent_worker_pool;
    bool reused = false;

    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PARALLEL_PERSISTENT_EPISODE_ATTEMPT);
    if (pthread_mutex_trylock(&pool->episode_mutex) != 0) {
        return CETTA_PERSISTENT_POOL_RUN_BUSY;
    }
    if (!persistent_worker_pool_ensure(executor->thread_count, &reused)) {
        pthread_mutex_unlock(&pool->episode_mutex);
        return CETTA_PERSISTENT_POOL_RUN_UNAVAILABLE;
    }

    pthread_mutex_lock(&executor->completion_mutex);
    executor->completed_workers = 0u;
    pthread_mutex_unlock(&executor->completion_mutex);

    pthread_mutex_lock(&pool->mutex);
    pool->active_executor = executor;
    if (pool->generation == UINT64_MAX) {
        pool->generation = 1u;
        for (uint32_t i = 0; i < pool->worker_count; i++)
            pool->workers[i]->observed_generation = 0u;
    } else {
        pool->generation++;
    }
    pthread_cond_broadcast(&pool->condition);
    pthread_mutex_unlock(&pool->mutex);

    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PARALLEL_PERSISTENT_EPISODE_COMMIT);
    if (reused) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PARALLEL_PERSISTENT_EPISODE_REUSE);
    }

    parallel_wait_for_workers(executor, executor->thread_count);

    pthread_mutex_lock(&pool->mutex);
    if (pool->active_executor == executor)
        pool->active_executor = NULL;
    pthread_mutex_unlock(&pool->mutex);
    pthread_mutex_unlock(&pool->episode_mutex);
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PARALLEL_PERSISTENT_EPISODE_COMPLETE);
    return CETTA_PERSISTENT_POOL_RUN_COMPLETE;
}

static void persistent_worker_pool_shutdown(void) {
    CettaPersistentWorkerPool *pool = &g_persistent_worker_pool;

    /* system:exit may run inside a participating worker.  Process-exit
     * cleanup must not wait for that worker's coordinator (or try to join
     * the calling worker).  An idle pool is joined normally; an active
     * episode is reclaimed by process termination. */
    if (pthread_mutex_trylock(&pool->episode_mutex) != 0)
        return;
    pthread_mutex_lock(&pool->mutex);
    pool->stopping = true;
    pthread_cond_broadcast(&pool->condition);
    pthread_mutex_unlock(&pool->mutex);
    for (uint32_t i = 0; i < pool->worker_count; i++) {
        (void)pthread_join(pool->workers[i]->thread, NULL);
        free(pool->workers[i]);
    }
    free(pool->workers);
    pool->workers = NULL;
    pool->worker_count = 0u;
    pool->worker_capacity = 0u;
    pool->active_executor = NULL;
    pthread_mutex_unlock(&pool->episode_mutex);
}

static bool parallel_executor_run_one_shot(
        CettaParallelExecutor *executor) {
    uint32_t started = 0;
    bool ok = true;
    pthread_attr_t attr;
    pthread_attr_t *attrp = NULL;
    bool attr_initialized = false;

    if (!executor || !executor->threads || !executor->workers ||
        executor->thread_count == 0u) {
        return false;
    }
    if (executor->config.stack_size_bytes > 0) {
        size_t stack_size = executor->config.stack_size_bytes;
#ifdef PTHREAD_STACK_MIN
        if (stack_size < (size_t)PTHREAD_STACK_MIN)
            stack_size = (size_t)PTHREAD_STACK_MIN;
#endif
        if (pthread_attr_init(&attr) != 0) {
            cetta_parallel_executor_fail(
                executor, "could not configure parallel worker stack");
            ok = false;
        } else {
            attr_initialized = true;
        }
        if (ok && pthread_attr_setstacksize(&attr, stack_size) != 0) {
            cetta_parallel_executor_fail(
                executor, "could not configure parallel worker stack");
            ok = false;
        }
        if (ok) {
            attrp = &attr;
        }
    }
    for (uint32_t i = 0; ok && i < executor->thread_count; i++) {
        if (pthread_create(&executor->threads[i], attrp,
                           parallel_one_shot_worker_main,
                           &executor->workers[i]) != 0) {
            cetta_parallel_executor_fail(executor,
                                         "could not start parallel worker thread");
            ok = false;
            break;
        }
        started++;
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PARALLEL_ONE_SHOT_THREAD_START);
    }

    if (!ok) parallel_ready_queue_fail(&executor->queue);

    parallel_wait_for_workers(executor, started);
    for (uint32_t i = 0; i < started; i++) {
        if (pthread_join(executor->threads[i], NULL) != 0) {
            cetta_parallel_executor_fail(executor,
                                         "could not join parallel worker thread");
            ok = false;
        }
    }
    if (attr_initialized)
        pthread_attr_destroy(&attr);
    return ok && !cetta_parallel_executor_failed(executor);
}

bool cetta_parallel_executor_run(CettaParallelExecutor *executor) {
    CettaPersistentPoolRunResult persistent_result =
        CETTA_PERSISTENT_POOL_RUN_UNAVAILABLE;

    if (!executor || !executor->threads || !executor->workers ||
        executor->thread_count == 0u || executor->run_started) {
        return false;
    }
    __attribute__((cleanup(parallel_stats_restore))) ParallelStatsScope stats = {
        cetta_runtime_stats_owner_exchange(executor->stats)};
    executor->run_started = true;
    parallel_ready_queue_close_if_idle(&executor->queue);

    if (executor->config.prefer_persistent_workers) {
        if (g_parallel_worker_depth != 0u) {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_PARALLEL_PERSISTENT_NESTED_DECLINE);
        } else if (executor->config.stack_size_bytes <=
                   (size_t)PERSISTENT_WORKER_STACK_BYTES) {
            persistent_result = persistent_worker_pool_run(executor);
            if (persistent_result == CETTA_PERSISTENT_POOL_RUN_COMPLETE)
                return !cetta_parallel_executor_failed(executor);
            if (persistent_result == CETTA_PERSISTENT_POOL_RUN_BUSY) {
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_PARALLEL_PERSISTENT_BUSY_DECLINE);
            } else {
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_PARALLEL_PERSISTENT_CAPACITY_DECLINE);
            }
        } else {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_PARALLEL_PERSISTENT_CAPACITY_DECLINE);
        }
    }
    return parallel_executor_run_one_shot(executor);
}

Arena *cetta_parallel_worker_arena(CettaParallelWorker *worker) {
    return worker ? &worker->arena : NULL;
}

uint32_t cetta_parallel_worker_index(const CettaParallelWorker *worker) {
    return worker ? worker->index : 0u;
}

bool cetta_parallel_worker_active(void) {
    return g_parallel_worker_depth != 0u;
}

CettaParallelExecutor *cetta_parallel_worker_executor(
    const CettaParallelWorker *worker) {
    return worker ? worker->executor : NULL;
}
