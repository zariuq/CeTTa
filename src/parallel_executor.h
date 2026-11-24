#ifndef CETTA_PARALLEL_EXECUTOR_H
#define CETTA_PARALLEL_EXECUTOR_H

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

#include "atom.h"

typedef struct CettaParallelTaskNode CettaParallelTaskNode;
struct CettaParallelTaskNode {
    void *task;
    /* The task's name supply: the pusher's next push when it was pushed,
       or the supply the pusher placed it at, so its names depend on the
       tasks and the program, never on which worker runs it
       (name_supply.h).  The node holds its block until a worker takes
       it. */
    CettaNameSupply names;
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
    /* The queue takes and gives no more tasks: it was closed once idle,
       cancelled, or failed. */
    bool stopped;
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
    /* Called, when the executor is freed, for each task still queued, which
       will never run (a run that failed or was cancelled); optional. */
    void (*task_discard)(void *task, void *user);
    const char *worker_failure_message;
} CettaParallelExecutorConfig;

struct CettaParallelWorker {
    CettaParallelExecutor *executor;
    Arena arena;
    uint32_t index;
    /* The latest clocks of the supplies that ended on this worker. */
    uint64_t ending_clocks[CETTA_NAME_KIND_COUNT];
};

struct CettaParallelExecutor {
    struct CettaNativeMemory *memory;
    CettaNameContext names;
    /* The name supply of the thread that made the executor, its place then
       (its block, held, its share and its pushes), and the latest clocks of
       the supplies that ended on that thread since.  Its pushes hand on
       parts of it; when the executor is freed every task's supply has
       ended, so the place comes back whole and the clocks go on from the
       latest any task reached: a fork and join in balance
       (SharedSupplies.join_pieces). */
    CettaNameSupply *coordinator;
    CettaNameSupply coordinator_place;
    uint64_t ending_clocks[CETTA_NAME_KIND_COUNT];
    uint64_t *coordinator_ending_clocks;
    bool coordinator_held;
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

/* Make an executor on the calling thread, its coordinator, whose name
   supply it forks for the tasks the thread pushes.  Executors one thread
   makes are freed in the reverse order. */
bool cetta_parallel_executor_init(CettaParallelExecutor *executor,
                                  const CettaParallelExecutorConfig *config);
/* Free an executor on the thread that made it: queued tasks that never ran
   are discarded, and the coordinator's supply gets its place back, at the
   latest clocks its tasks reached. */
void cetta_parallel_executor_free(CettaParallelExecutor *executor);
/* Queue TASK as the next push of the calling thread's name supply: the
   task draws from the part of the thread's pool its push count gives
   (name_supply.h), and the thread draws from the half of its share it
   kept. */
bool cetta_parallel_executor_push(CettaParallelExecutor *executor, void *task);
/* Queue TASK to draw from *NAMES, which the queue takes over, ending it
   when it refuses the task: for a client that places its tasks itself (the
   rho executor places a spawned component and a COMM's continuation by the
   supplies of the tasks they come from). */
bool cetta_parallel_executor_push_supply(CettaParallelExecutor *executor,
                                         void *task, CettaNameSupply *names);
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
