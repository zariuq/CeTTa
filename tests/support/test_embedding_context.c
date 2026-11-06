#define _POSIX_C_SOURCE 200809L
#include "cetta_embed.h"
#include "grounded.h"
#include "eval.h"
#include "stats.h"
#include "native_handle.h"
#include "parallel_executor.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static atomic_uint hook_mode;
static uint64_t marker;
static pthread_mutex_t handshake = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static bool entered, release_holder, other_finished;
static _Thread_local bool hold_named_lock;
static atomic_uint native_closes;
static cetta_engine_t *closing_engine;
typedef struct { cetta_engine_t *engine; } NativeWork;

static void close_native_work(void *resource) {
    NativeWork *work = resource;
    atomic_fetch_add(&native_closes, 1u);
    cetta_embed_engine_close(work->engine);
    cetta_atom_t *refused = NULL;
    assert(cetta_embed_atom_integer(work->engine, 1, &refused) == CETTA_ENGINE_CLOSED);
    assert(!refused);
    free(resource);
}

static cetta_engine_t *new_engine(void) {
    cetta_engine_t *engine = NULL;
    cetta_engine_config_t config = {sizeof(config), CETTA_EMBED_ABI_VERSION,
                                    "petta", "extended", NULL};
    assert(cetta_embed_engine_new(&config, &engine) == CETTA_OK);
    return engine;
}
static cetta_result_t *run(cetta_engine_t *engine, const char *text) {
    cetta_result_t *result = NULL;
    cetta_status_t status = cetta_embed_run(engine, text, strlen(text), NULL, &result);
    if (status) fprintf(stderr, "%s: %s\n", text, cetta_embed_status_name(status));
    assert(status == CETTA_OK);
    return result;
}
static void run_discard(cetta_engine_t *engine, const char *text) {
    cetta_embed_result_release(run(engine, text));
}
static int64_t integer_result(cetta_result_t *result) {
    assert(cetta_embed_result_count(result) == 1);
    cetta_atom_t *answer = NULL;
    int64_t number;
    assert(cetta_embed_result_get(result, 0, &answer) == CETTA_OK);
    if (cetta_embed_atom_get_integer(answer, &number) != CETTA_OK) {
        size_t size;
        assert(cetta_embed_atom_format(answer, NULL, 0, &size) == CETTA_OK);
        char *text = malloc(size);
        assert(text);
        assert(cetta_embed_atom_format(answer, text, size, &size) == CETTA_OK);
        fprintf(stderr, "expected integer, got %s\n", text);
        free(text);
        abort();
    }
    cetta_embed_atom_release(answer);
    cetta_embed_result_release(result);
    return number;
}
static int64_t statistics_marker(cetta_engine_t *engine) {
    return integer_result(run(engine,
        "!(match (runtime-stats!) (runtime-counter runtime-statistics-recorded $n) $n)"));
}

bool __real_grounded_call(Arena *, Atom *, Atom **, uint32_t, CettaCallOutcome *);
bool __wrap_grounded_call(Arena *arena, Atom *head, Atom **arguments,
                           uint32_t count, CettaCallOutcome *outcome) {
    unsigned mode = atomic_load(&hook_mode);
    if (!mode || !atom_is_symbol_id(head, g_builtin_syms.readln_bang))
        return __real_grounded_call(arena, head, arguments, count, outcome);
    if (mode == 1) {
        (cetta_runtime_stats_set)(CETTA_RUNTIME_COUNTER_STATISTICS_RECORDED, marker);
    } else if (mode == 2) {
        Atom *key = atom_symbol(arena, "shared-lock");
        void *first = NULL, *recursive = NULL;
        assert(eval_named_mutex_acquire(eval_current_library_context(), arena, key, &first));
        assert(eval_named_mutex_acquire(eval_current_library_context(), arena, key, &recursive));
        assert(first == recursive);
        eval_named_mutex_release(recursive);
        if (hold_named_lock) {
            pthread_mutex_lock(&handshake);
            entered = true;
            pthread_cond_broadcast(&changed);
            while (!release_holder) pthread_cond_wait(&changed, &handshake);
            pthread_mutex_unlock(&handshake);
        }
        eval_named_mutex_release(first);
    } else if (mode == 3) {
        CettaRuntimeStatsOwner *outer = cetta_runtime_stats_owner_current();
        cetta_engine_t *nested = new_engine();
        run_discard(nested, "!(reset-runtime-stats!)");
        assert(integer_result(run(nested, "!(+ 20 22)")) == 42);
        cetta_embed_engine_release(nested);
        assert(cetta_runtime_stats_owner_current() == outer);
    } else if (mode == 5 || mode == 6 || mode == 7) {
        CettaLibraryContext *library = eval_current_library_context();
        uint64_t id;
        NativeWork *resource = malloc(sizeof(*resource));
        assert(resource);
        resource->engine = closing_engine;
        assert(cetta_native_handle_alloc(library, "embedding-test-work", resource,
                                         close_native_work, &id));
        if (mode == 7) {
            *outcome = cetta_call_completed_value(atom_int(arena, 37));
            return true;
        }
        Atom *value = cetta_native_handle_owned_atom(library, arena, "embedding-test-work", id);
        assert(value);
        if (mode == 6) {
            cetta_embed_engine_close(closing_engine);
            assert(atomic_load(&native_closes) == 0);
            assert(cetta_native_handle_get_owned(library, value, "embedding-test-work") == resource);
        }
        *outcome = cetta_call_completed_value(value);
        return true;
    }
    *outcome = cetta_call_completed_value(atom_int(arena, 37));
    return true;
}

static void statistics_are_separate(void) {
    cetta_engine_t *left = new_engine(), *right = new_engine();
    atomic_store(&hook_mode, 1);
    run_discard(left, "!(reset-runtime-stats!)");
    run_discard(right, "!(reset-runtime-stats!)");
    marker = 17;
    run_discard(left, "!(readln!)");
    marker = 29;
    run_discard(right, "!(readln!)");
    assert(statistics_marker(left) == 17);
    assert(statistics_marker(right) == 29);
    run_discard(right, "!(reset-runtime-stats!)");
    assert(statistics_marker(left) == 17);
    atomic_store(&hook_mode, 3);
    assert(integer_result(run(left, "!(readln!)")) == 37);
    assert(statistics_marker(left) == 17);
    atomic_store(&hook_mode, 0);
    cetta_embed_engine_release(left);
    cetta_embed_engine_release(right);
}

static struct timespec deadline(void) {
    struct timespec until;
    assert(!clock_gettime(CLOCK_REALTIME, &until));
    until.tv_sec += 5;
    return until;
}
static void *holder(void *engine) {
    hold_named_lock = true;
    assert(integer_result(run(engine, "!(readln!)")) == 37);
    return NULL;
}
static void *independent_lock(void *engine) {
    assert(integer_result(run(engine, "!(readln!)")) == 37);
    pthread_mutex_lock(&handshake);
    other_finished = true;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&handshake);
    return NULL;
}
static void locks_are_separate(void) {
    cetta_engine_t *left = new_engine(), *right = new_engine();
    atomic_store(&hook_mode, 2);
    pthread_t first, second;
    assert(!pthread_create(&first, NULL, holder, left));
    pthread_mutex_lock(&handshake);
    struct timespec until = deadline();
    while (!entered) {
        int status = pthread_cond_timedwait(&changed, &handshake, &until);
        assert(status != ETIMEDOUT);
        assert(!status);
    }
    pthread_mutex_unlock(&handshake);
    assert(!pthread_create(&second, NULL, independent_lock, right));
    pthread_mutex_lock(&handshake);
    until = deadline();
    while (!other_finished) {
        int status = pthread_cond_timedwait(&changed, &handshake, &until);
        if (status == ETIMEDOUT) break;
        assert(!status);
    }
    bool overlapped = other_finished;
    release_holder = true;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&handshake);
    assert(!pthread_join(first, NULL));
    assert(!pthread_join(second, NULL));
    assert(overlapped);
    atomic_store(&hook_mode, 0);
    /* Mutex execution is native PeTTa, including builds without Prolog.
     * Host services still must not turn unrelated names into operations. */
    assert(integer_result(run(left, "!(with_mutex shared-lock 42)")) == 42);
    cetta_result_t *unknown = run(left, "!(embedding-unknown-lock shared-lock 42)");
    cetta_atom_t *answer = NULL;
    assert(cetta_embed_result_get(unknown, 0, &answer) == CETTA_OK);
    assert(cetta_embed_atom_kind(answer) == CETTA_ATOM_EXPRESSION);
    cetta_embed_atom_release(answer);
    cetta_embed_result_release(unknown);
    cetta_embed_engine_release(left);
    cetta_embed_engine_release(right);
}

static void close_cancels_native_work(void) {
    atomic_store(&native_closes, 0);
    atomic_store(&hook_mode, 5);
    cetta_engine_t *engine = new_engine();
    closing_engine = engine;
    cetta_result_t *result = run(engine, "!(readln!)");
    cetta_atom_t *answer = NULL;
    assert(cetta_embed_result_get(result, 0, &answer) == CETTA_OK);
    cetta_embed_result_release(result);
    assert(atomic_load(&native_closes) == 0);
    cetta_embed_engine_close(engine);
    assert(atomic_load(&native_closes) == 1);
    cetta_embed_engine_close(engine);
    assert(atomic_load(&native_closes) == 1);
    cetta_embed_engine_release(engine);
    size_t size;
    assert(cetta_embed_atom_format(answer, NULL, 0, &size) == CETTA_OK);
    cetta_embed_atom_release(answer);
    assert(atomic_load(&native_closes) == 1);

    atomic_store(&native_closes, 0);
    atomic_store(&hook_mode, 6);
    closing_engine = new_engine();
    result = NULL;
    const char *text = "!(readln!)";
    cetta_status_t status = cetta_embed_run(closing_engine, text, strlen(text), NULL, &result);
    assert(status == CETTA_OK || status == CETTA_CANCELLED);
    assert(atomic_load(&native_closes) == 1);
    cetta_embed_result_release(result);
    cetta_embed_engine_release(closing_engine);
    closing_engine = NULL;
    assert(atomic_load(&native_closes) == 1);

    atomic_store(&native_closes, 0);
    atomic_store(&hook_mode, 7);
    closing_engine = new_engine();
    assert(integer_result(run(closing_engine, "!(readln!)")) == 37);
    assert(atomic_load(&native_closes) == 0);
    cetta_embed_engine_release(closing_engine);
    closing_engine = NULL;
    assert(atomic_load(&native_closes) == 1);
    atomic_store(&hook_mode, 0);
}

static bool count_task(CettaParallelWorker *worker, void *task, void *owner) {
    (void)worker;
    (void)task;
    assert(cetta_runtime_stats_owner_current() == owner);
    (cetta_runtime_stats_add)(CETTA_RUNTIME_COUNTER_STATISTICS_RECORDED, 1);
    return true;
}
static void worker_statistics_follow_the_submitter(void) {
    CettaRuntimeStatsOwner *first = cetta_runtime_stats_owner_new();
    CettaRuntimeStatsOwner *second = cetta_runtime_stats_owner_new();
    assert(first && second);
    CettaRuntimeStatsOwner *previous = cetta_runtime_stats_owner_exchange(first);
    cetta_runtime_stats_enable();
    (cetta_runtime_stats_set)(CETTA_RUNTIME_COUNTER_STATISTICS_RECORDED, 11);
    CettaParallelExecutor executor;
    CettaParallelExecutorConfig config = {.thread_count = 2, .task_fn = count_task,
                                         .user = first, .prefer_persistent_workers = true};
    assert(cetta_parallel_executor_init(&executor, &config));
    int tasks[2];
    assert(cetta_parallel_executor_push(&executor, &tasks[0]));
    assert(cetta_parallel_executor_push(&executor, &tasks[1]));
    cetta_runtime_stats_owner_exchange(second);
    cetta_runtime_stats_enable();
    (cetta_runtime_stats_set)(CETTA_RUNTIME_COUNTER_STATISTICS_RECORDED, 23);
    assert(cetta_parallel_executor_run(&executor));
    assert(cetta_runtime_stats_owner_current() == second);
    cetta_parallel_executor_free(&executor);
    assert(cetta_runtime_stats_owner_current() == second);
    CettaRuntimeStats snapshot;
    cetta_runtime_stats_snapshot(&snapshot);
    assert(snapshot.counters[CETTA_RUNTIME_COUNTER_STATISTICS_RECORDED] == 23);
    cetta_runtime_stats_owner_exchange(first);
    cetta_runtime_stats_snapshot(&snapshot);
    assert(snapshot.counters[CETTA_RUNTIME_COUNTER_STATISTICS_RECORDED] == 13);
    cetta_runtime_stats_owner_exchange(previous);
    cetta_runtime_stats_owner_free(first);
    cetta_runtime_stats_owner_free(second);
}

int main(void) {
    statistics_are_separate();
    locks_are_separate();
    close_cancels_native_work();
    worker_statistics_follow_the_submitter();
    puts("embedding context: isolated counters/locks, nested entry and exactly-once native closure pass");
    return 0;
}
