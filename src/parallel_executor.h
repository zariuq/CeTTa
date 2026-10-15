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
    size_t stack_size_bytes;
    bool prefer_persistent_workers;
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
bool cetta_parallel_executor_push(CettaParallelExecutor *executor, void *task);
bool cetta_parallel_executor_run(CettaParallelExecutor *executor);
void cetta_parallel_executor_cancel(CettaParallelExecutor *executor);
void cetta_parallel_executor_fail(CettaParallelExecutor *executor,
                                  const char *fmt, ...);
const char *cetta_parallel_executor_error(const CettaParallelExecutor *executor);
bool cetta_parallel_executor_failed(const CettaParallelExecutor *executor);

/* Final process-level shutdown. A busy refusal leaves admission unchanged.
 * Success closes admission and joins all idle persistent workers, including
 * their thread-local destructors. One-shot and nested episodes count as live
 * until their workers have joined. Repeated successful shutdown is harmless;
 * no new episode is admitted afterward. The caller still owns its contexts. */
bool cetta_parallel_executor_shutdown(void);
/* As above, with an additional final-admission gate. The gate runs only once
 * no episode is live, under the admission mutex, before worker admission is
 * closed. It must not call the executor or run finalizers: it may atomically
 * seal another owner's admission, or refuse without changing that owner.
 * Joining and thread-exit callbacks run after this mutex is released. A join
 * failure retains Closing; another call retries the remaining joins. */
typedef bool (*CettaParallelShutdownGate)(void *context);
bool cetta_parallel_executor_shutdown_if(CettaParallelShutdownGate gate,
                                         void *context);

Arena *cetta_parallel_worker_arena(CettaParallelWorker *worker);
uint32_t cetta_parallel_worker_index(const CettaParallelWorker *worker);
/* Whether the calling thread is running a parallel worker's episode. */
bool cetta_parallel_worker_active(void);
CettaParallelExecutor *cetta_parallel_worker_executor(
    const CettaParallelWorker *worker);

#endif /* CETTA_PARALLEL_EXECUTOR_H */
