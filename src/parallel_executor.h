#ifndef CETTA_PARALLEL_EXECUTOR_H
#define CETTA_PARALLEL_EXECUTOR_H

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

#include "atom.h"

typedef struct CettaParallelTaskNode CettaParallelTaskNode;
struct CettaParallelTaskNode {
    void *task;
    CettaParallelTaskNode *next;
};

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    CettaParallelTaskNode *head;
    CettaParallelTaskNode *tail;
    uint32_t queued;
    uint32_t active;
    bool closed;
    bool cancelled;
    bool failed;
} CettaParallelReadyQueue;

typedef struct CettaParallelWorker CettaParallelWorker;
typedef struct CettaParallelExecutor CettaParallelExecutor;

typedef bool (*CettaParallelTaskFn)(CettaParallelWorker *worker, void *task,
                                    void *user);
typedef void (*CettaParallelWorkerHookFn)(CettaParallelWorker *worker,
                                          void *user);

typedef struct {
    uint32_t thread_count;
    void *user;
    CettaParallelTaskFn task_fn;
    CettaParallelWorkerHookFn worker_enter;
    CettaParallelWorkerHookFn worker_leave;
    const char *worker_failure_message;
} CettaParallelExecutorConfig;

struct CettaParallelWorker {
    CettaParallelExecutor *executor;
    Arena arena;
    uint32_t index;
};

struct CettaParallelExecutor {
    CettaParallelExecutorConfig config;
    CettaParallelReadyQueue queue;
    pthread_mutex_t error_mutex;
    char error[256];
    pthread_t *threads;
    CettaParallelWorker *workers;
    uint32_t thread_count;
};

bool cetta_parallel_executor_init(CettaParallelExecutor *executor,
                                  const CettaParallelExecutorConfig *config);
void cetta_parallel_executor_free(CettaParallelExecutor *executor);
bool cetta_parallel_executor_push(CettaParallelExecutor *executor, void *task);
bool cetta_parallel_executor_run(CettaParallelExecutor *executor);
void cetta_parallel_executor_cancel(CettaParallelExecutor *executor);
void cetta_parallel_executor_fail(CettaParallelExecutor *executor,
                                  const char *fmt, ...);
const char *cetta_parallel_executor_error(const CettaParallelExecutor *executor);
bool cetta_parallel_executor_failed(const CettaParallelExecutor *executor);

Arena *cetta_parallel_worker_arena(CettaParallelWorker *worker);
uint32_t cetta_parallel_worker_index(const CettaParallelWorker *worker);
CettaParallelExecutor *cetta_parallel_worker_executor(
    const CettaParallelWorker *worker);

#endif /* CETTA_PARALLEL_EXECUTOR_H */
