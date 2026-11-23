#ifndef CETTA_PARALLEL_EXECUTOR_H
#define CETTA_PARALLEL_EXECUTOR_H

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

#include "atom.h"

typedef struct CettaParallelTaskNode CettaParallelTaskNode;
struct CettaParallelTaskNode {
    void *task;
    /* The task's name supply, split off the pusher's when it was pushed, so
       its names depend on which task pushed it and when, never on which
       worker runs it (name_supply.h).  The node holds its block until a
       worker takes it. */
    CettaNameSupplyChild names;
    CettaParallelTaskNode *next;
};

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    CettaParallelTaskNode *head;
    CettaParallelTaskNode *tail;
    /* Nodes taken off the queue, kept for the next pushes under the queue's
       lock and freed with the queue, so a task costs no allocation once
       the queue has been as long, and every node is freed by the thread
       that frees the queue. */
    CettaParallelTaskNode *spare;
    uint32_t queued;
    uint32_t active;
    bool closed;
    bool cancelled;
    bool failed;
} CettaParallelReadyQueue;

#include "runtime_names.h"
#include "stats.h"

typedef struct CettaParallelWorker CettaParallelWorker;
typedef struct CettaParallelExecutor CettaParallelExecutor;

typedef bool (*CettaParallelTaskFn)(CettaParallelWorker *worker, void *task,
                                    void *user);
typedef void (*CettaParallelWorkerHookFn)(CettaParallelWorker *worker,
                                          void *user);

typedef struct {
    uint32_t thread_count;
    size_t stack_size_bytes;
    bool prefer_persistent_workers;
    void *user;
    CettaParallelTaskFn task_fn;
    CettaParallelWorkerHookFn worker_enter;
    CettaParallelWorkerHookFn worker_leave;
    bool (*interrupt)(void *user);
    const char *worker_failure_message;
} CettaParallelExecutorConfig;

struct CettaParallelWorker {
    CettaParallelExecutor *executor;
    Arena arena;
    uint32_t index;
};

struct CettaParallelExecutor {
    struct CettaNativeMemory *memory;
    CettaNameContext names;
    CettaRuntimeStatsOwner *stats;
    CettaParallelExecutorConfig config;
    CettaParallelReadyQueue queue;
    pthread_mutex_t error_mutex;
    pthread_mutex_t completion_mutex;
    pthread_cond_t completion_cond;
    char error[256];
    pthread_t *threads;
    CettaParallelWorker *workers;
    uint32_t thread_count;
    uint32_t completed_workers;
    bool run_started;
};

bool cetta_parallel_executor_init(CettaParallelExecutor *executor,
                                  const CettaParallelExecutorConfig *config);
void cetta_parallel_executor_free(CettaParallelExecutor *executor);
/* Queue TASK, giving it the next child of the calling thread's name supply;
   its names are drawn from that child while it runs. */
bool cetta_parallel_executor_push(CettaParallelExecutor *executor, void *task);
bool cetta_parallel_executor_run(CettaParallelExecutor *executor);
void cetta_parallel_executor_cancel(CettaParallelExecutor *executor);
void cetta_parallel_executor_fail(CettaParallelExecutor *executor,
                                  const char *fmt, ...);
const char *cetta_parallel_executor_error(const CettaParallelExecutor *executor);
bool cetta_parallel_executor_failed(const CettaParallelExecutor *executor);

Arena *cetta_parallel_worker_arena(CettaParallelWorker *worker);
uint32_t cetta_parallel_worker_index(const CettaParallelWorker *worker);
/* Whether the calling thread is running a parallel worker's episode. */
bool cetta_parallel_worker_active(void);
CettaParallelExecutor *cetta_parallel_worker_executor(
    const CettaParallelWorker *worker);

#endif /* CETTA_PARALLEL_EXECUTOR_H */
