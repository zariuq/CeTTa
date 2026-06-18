#include "parallel_executor.h"
#include "stats.h"

#include <pthread.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PARALLEL_WORKER_ARENA_RESERVE (4u * ARENA_BLOCK_SIZE)

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
    pthread_mutex_destroy(&executor->error_mutex);
    executor->workers = NULL;
    executor->threads = NULL;
    executor->thread_count = 0;
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

static void *parallel_worker_main(void *arg) {
    CettaParallelWorker *worker = arg;
    CettaParallelExecutor *executor = worker->executor;

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
    return NULL;
}

bool cetta_parallel_executor_run(CettaParallelExecutor *executor) {
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
                           parallel_worker_main,
                           &executor->workers[i]) != 0) {
            cetta_parallel_executor_fail(executor,
                                         "could not start parallel worker thread");
            ok = false;
            break;
        }
        started++;
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
