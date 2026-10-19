#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "mg_worker.h"

#include "mg_core.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * A small pool of big-stack workers.  A caller takes an idle worker (or
 * starts one), hands it one call and waits; finished workers return to the
 * idle list, at most MG_WORKER_IDLE_MAX of them.  Calls from several
 * evaluator threads run on distinct workers.
 */

#define MG_WORKER_STACK_BYTES ((size_t)1u << 30)       /* 1 GiB, reserved */
#define MG_WORKER_STACK_FALLBACK ((size_t)256u << 20)  /* 256 MiB */
#define MG_WORKER_IDLE_MAX 4u
/* Stack bytes allowed per kernel recursion level, sanitizer frames
 * included. */
#define MG_WORKER_BYTES_PER_LEVEL 4096u

typedef struct Worker Worker;
struct Worker {
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t wake;
    pthread_cond_t done;
    MgDeepCall *job;
    bool finished;
    bool stop;
    size_t stack_bytes;
    Worker *next_idle;
};

static pthread_mutex_t g_pool_lock = PTHREAD_MUTEX_INITIALIZER;
static Worker *g_idle;
static unsigned g_idle_count;
static size_t g_stack_bytes;

static void *worker_main(void *arg) {
    Worker *w = arg;
    uint64_t levels = w->stack_bytes / MG_WORKER_BYTES_PER_LEVEL;
    mg_set_thread_depth_limit(levels > UINT32_MAX ? UINT32_MAX : (uint32_t)levels);
    pthread_mutex_lock(&w->lock);
    for (;;) {
        while (!w->job && !w->stop)
            pthread_cond_wait(&w->wake, &w->lock);
        if (!w->job)
            break;
        MgDeepCall *job = w->job;
        pthread_mutex_unlock(&w->lock);
        job->run(job);
        pthread_mutex_lock(&w->lock);
        w->job = NULL;
        w->finished = true;
        pthread_cond_signal(&w->done);
    }
    pthread_mutex_unlock(&w->lock);
    return NULL;
}

static void worker_destroy(Worker *w) {
    pthread_cond_destroy(&w->done);
    pthread_cond_destroy(&w->wake);
    pthread_mutex_destroy(&w->lock);
    free(w);
}

static Worker *worker_start(void) {
    Worker *w = calloc(1u, sizeof(*w));
    if (!w)
        return NULL;
    if (pthread_mutex_init(&w->lock, NULL) != 0) {
        free(w);
        return NULL;
    }
    if (pthread_cond_init(&w->wake, NULL) != 0) {
        pthread_mutex_destroy(&w->lock);
        free(w);
        return NULL;
    }
    if (pthread_cond_init(&w->done, NULL) != 0) {
        pthread_cond_destroy(&w->wake);
        pthread_mutex_destroy(&w->lock);
        free(w);
        return NULL;
    }
    const size_t sizes[] = {MG_WORKER_STACK_BYTES, MG_WORKER_STACK_FALLBACK};
    bool started = false;
    for (size_t i = 0; !started && i < sizeof sizes / sizeof sizes[0]; ++i) {
        pthread_attr_t attr;
        if (pthread_attr_init(&attr) != 0)
            break;
        w->stack_bytes = sizes[i];
        if (pthread_attr_setstacksize(&attr, sizes[i]) == 0 &&
            pthread_create(&w->thread, &attr, worker_main, w) == 0)
            started = true;
        pthread_attr_destroy(&attr);
    }
    if (!started) {
        worker_destroy(w);
        return NULL;
    }
    pthread_mutex_lock(&g_pool_lock);
    g_stack_bytes = w->stack_bytes;
    pthread_mutex_unlock(&g_pool_lock);
    return w;
}

static void worker_stop(Worker *w) {
    pthread_mutex_lock(&w->lock);
    w->stop = true;
    pthread_cond_signal(&w->wake);
    pthread_mutex_unlock(&w->lock);
    pthread_join(w->thread, NULL);
    worker_destroy(w);
}

static Worker *worker_take(void) {
    pthread_mutex_lock(&g_pool_lock);
    Worker *w = g_idle;
    if (w) {
        g_idle = w->next_idle;
        --g_idle_count;
        w->next_idle = NULL;
    }
    pthread_mutex_unlock(&g_pool_lock);
    return w ? w : worker_start();
}

static void worker_return(Worker *w) {
    pthread_mutex_lock(&g_pool_lock);
    if (g_idle_count < MG_WORKER_IDLE_MAX) {
        w->next_idle = g_idle;
        g_idle = w;
        ++g_idle_count;
        w = NULL;
    }
    pthread_mutex_unlock(&g_pool_lock);
    if (w)
        worker_stop(w);
}

bool mg_deep_call(MgDeepCall *call, bool (*poll)(void *context),
                  void *poll_context) {
    Worker *w = worker_take();
    if (!w)
        return false;
    pthread_mutex_lock(&w->lock);
    w->finished = false;
    w->job = call;
    pthread_cond_signal(&w->wake);
    while (!w->finished) {
        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_nsec += 1000000L;
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_nsec -= 1000000000L;
            deadline.tv_sec += 1;
        }
        pthread_cond_timedwait(&w->done, &w->lock, &deadline);
        if (w->finished || !poll || atomic_load(&call->interrupt))
            continue;
        pthread_mutex_unlock(&w->lock);
        bool stop = poll(poll_context);
        pthread_mutex_lock(&w->lock);
        if (stop)
            atomic_store(&call->interrupt, true);
    }
    pthread_mutex_unlock(&w->lock);
    worker_return(w);
    return true;
}

bool mg_deep_call_interrupted(void *call) {
    return call && atomic_load(&((MgDeepCall *)call)->interrupt);
}

size_t mg_deep_call_stack_bytes(void) {
    pthread_mutex_lock(&g_pool_lock);
    size_t bytes = g_stack_bytes;
    pthread_mutex_unlock(&g_pool_lock);
    return bytes;
}

void mg_deep_call_shutdown(void) {
    pthread_mutex_lock(&g_pool_lock);
    Worker *w = g_idle;
    g_idle = NULL;
    g_idle_count = 0;
    pthread_mutex_unlock(&g_pool_lock);
    while (w) {
        Worker *next = w->next_idle;
        worker_stop(w);
        w = next;
    }
}
