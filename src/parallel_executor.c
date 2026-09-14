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

#include <pthread.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PARALLEL_WORKER_ARENA_RESERVE (4u * ARENA_BLOCK_SIZE)
#define PERSISTENT_WORKER_STACK_BYTES (16u * 1024u * 1024u)

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
    queue->queued = 0;
    queue->active = 0;
    queue->closed = false;
    queue->cancelled = false;
    queue->failed = false;
}

static void parallel_ready_queue_free(CettaParallelReadyQueue *queue) {
    CettaParallelTaskNode *node = queue->head;
    while (node) {
        CettaParallelTaskNode *next = node->next;
        free(node);
        node = next;
    }
    pthread_cond_destroy(&queue->cond);
    pthread_mutex_destroy(&queue->mutex);
    queue->head = NULL;
    queue->tail = NULL;
}

static bool parallel_ready_queue_push(CettaParallelReadyQueue *queue,
                                      void *task) {
    CettaParallelTaskNode *node = cetta_malloc(sizeof(CettaParallelTaskNode));
    node->task = task;
    node->next = NULL;

    pthread_mutex_lock(&queue->mutex);
    if (queue->closed || queue->cancelled || queue->failed) {
        pthread_mutex_unlock(&queue->mutex);
        free(node);
        return false;
    }
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

static void *parallel_ready_queue_pop(CettaParallelReadyQueue *queue) {
    CettaParallelTaskNode *node;
    void *task;

    pthread_mutex_lock(&queue->mutex);
    while (!queue->head && !queue->closed && !queue->cancelled &&
           !queue->failed) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_PARALLEL_QUEUE_WAIT);
        pthread_cond_wait(&queue->cond, &queue->mutex);
    }
    if (queue->failed || queue->cancelled || queue->closed) {
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
    pthread_mutex_unlock(&queue->mutex);

    task = node->task;
    free(node);
    return task;
}

static void parallel_ready_queue_task_done(CettaParallelReadyQueue *queue) {
    pthread_mutex_lock(&queue->mutex);
    if (queue->active > 0) queue->active--;
    if (!queue->head && queue->active == 0) {
        queue->closed = true;
        pthread_cond_broadcast(&queue->cond);
    }
    pthread_mutex_unlock(&queue->mutex);
}

static void parallel_ready_queue_cancel(CettaParallelReadyQueue *queue) {
    pthread_mutex_lock(&queue->mutex);
    queue->cancelled = true;
    pthread_cond_broadcast(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
}

static void parallel_ready_queue_fail(CettaParallelReadyQueue *queue) {
    pthread_mutex_lock(&queue->mutex);
    queue->failed = true;
    pthread_cond_broadcast(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
}

static void parallel_ready_queue_close_if_idle(
        CettaParallelReadyQueue *queue) {
    pthread_mutex_lock(&queue->mutex);
    if (!queue->head && queue->active == 0u) {
        queue->closed = true;
        pthread_cond_broadcast(&queue->cond);
    }
    pthread_mutex_unlock(&queue->mutex);
}

bool cetta_parallel_executor_init(CettaParallelExecutor *executor,
                                  const CettaParallelExecutorConfig *config) {
    if (!executor || !config || !config->task_fn ||
        config->thread_count == 0u) {
        return false;
    }
    memset(executor, 0, sizeof(*executor));
    executor->config = *config;
    executor->thread_count = config->thread_count;
    parallel_ready_queue_init(&executor->queue);
    pthread_mutex_init(&executor->error_mutex, NULL);
    pthread_mutex_init(&executor->completion_mutex, NULL);
    pthread_cond_init(&executor->completion_cond, NULL);
    executor->threads =
        cetta_malloc(sizeof(pthread_t) * executor->thread_count);
    executor->workers =
        cetta_malloc(sizeof(CettaParallelWorker) * executor->thread_count);
    for (uint32_t i = 0; i < executor->thread_count; i++) {
        executor->workers[i].executor = executor;
        executor->workers[i].index = i;
        arena_init(&executor->workers[i].arena);
        arena_reserve(&executor->workers[i].arena,
                      PARALLEL_WORKER_ARENA_RESERVE);
        arena_set_hashcons(&executor->workers[i].arena, NULL);
        arena_set_runtime_kind(&executor->workers[i].arena,
                               CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    }
    return true;
}

void cetta_parallel_executor_free(CettaParallelExecutor *executor) {
    if (!executor) return;
    if (executor->workers) {
        for (uint32_t i = 0; i < executor->thread_count; i++) {
            arena_free(&executor->workers[i].arena);
        }
    }
    free(executor->workers);
    free(executor->threads);
    parallel_ready_queue_free(&executor->queue);
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
    return parallel_ready_queue_push(&executor->queue, task);
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

    g_parallel_worker_depth++;
    if (executor->config.worker_enter) {
        executor->config.worker_enter(worker, executor->config.user);
    }
    for (;;) {
        void *task = parallel_ready_queue_pop(&executor->queue);
        bool ok;
        if (!task) break;
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_PARALLEL_WORKER_TASK);
        ok = executor->config.task_fn(worker, task, executor->config.user);
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
    g_parallel_worker_depth--;
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
        pthread_mutex_lock(&executor->completion_mutex);
        executor->completed_workers++;
        pthread_cond_broadcast(&executor->completion_cond);
        pthread_mutex_unlock(&executor->completion_mutex);
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

    pthread_mutex_lock(&executor->completion_mutex);
    while (executor->completed_workers < executor->thread_count) {
        pthread_cond_wait(&executor->completion_cond,
                          &executor->completion_mutex);
    }
    pthread_mutex_unlock(&executor->completion_mutex);

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

CettaParallelExecutor *cetta_parallel_worker_executor(
    const CettaParallelWorker *worker) {
    return worker ? worker->executor : NULL;
}
