#define _GNU_SOURCE
#include "library_proc.h"
#include "library.h"
#include "eval.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

typedef struct {
    unsigned char *bytes;
    size_t kept, capacity;
    uint64_t observed;
    bool eof;
} ProcCapture;

typedef struct {
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t changed;
    bool started, done, consumed, cancel;
    int wake[2];
    char **argv, **env;
    char *cwd, *program;
    unsigned char *input;
    size_t input_len, input_written, limit;
    uint64_t timeout_ms;
    int launch_error, host_error, wait_status;
    const char *launch_stage;
    bool waited, timed_out, cancelled;
    ProcCapture output[2];
} ProcJob;

static uint64_t monotonic_ms(void) {
    struct timespec value;
    clock_gettime(CLOCK_MONOTONIC, &value);
    return (uint64_t)value.tv_sec * 1000u + (uint64_t)value.tv_nsec / 1000000u;
}

static bool bytes_arg(Atom *value, const char **bytes, size_t *length) {
    if (!value || value->kind != ATOM_GROUNDED || value->ground.gkind != GV_STRING)
        return false;
    *bytes = value->ground.sval;
    *length = atom_string_len(value);
    return true;
}

static char *text_copy(Atom *value) {
    const char *bytes;
    size_t length;
    if (!bytes_arg(value, &bytes, &length) || memchr(bytes, 0, length)) {
        errno = EINVAL; return NULL;
    }
    return strndup(bytes, length);
}

static Atom *failed(Arena *arena, const char *kind, const char *message) {
    return atom_expr3(arena, atom_symbol(arena, "proc:failed"),
        atom_symbol(arena, kind), atom_string(arena, message));
}

static void free_vector(char **values) {
    if (!values) return;
    for (size_t i = 0; values[i]; i++) free(values[i]);
    free(values);
}

static bool cancelled(ProcJob *job) {
    pthread_mutex_lock(&job->lock);
    bool value = job->cancel;
    pthread_mutex_unlock(&job->lock);
    return value;
}

static void wake_job(ProcJob *job) {
    unsigned char wake = 1;
    ssize_t count;
    do count = write(job->wake[1], &wake, 1); while (count < 0 && errno == EINTR);
    /* A full nonblocking pipe is already a wakeup; polling also checks the
     * cancellation flag. No byte delivery is claimed by this notification. */
}

static int owned_pipe(int descriptors[2], int flags) {
    /* A closed standard descriptor may be reused transiently by pipe2.
     * Standard FILE writes take these same stream locks, so they cannot
     * accidentally publish into an internal pipe before it is relocated. */
    flockfile(stdout);
    flockfile(stderr);
    int status = pipe2(descriptors, flags);
    if (status) goto done;
    for (int i = 0; i < 2; i++) {
        if (descriptors[i] >= 3) continue;
        int moved = fcntl(descriptors[i], F_DUPFD_CLOEXEC, 3);
        if (moved < 0) { status = -1; goto failed; }
        close(descriptors[i]); descriptors[i] = moved;
    }
    goto done;
failed: {
    int saved_error = errno;
    for (int i = 0; i < 2; i++) {
        close(descriptors[i]); descriptors[i] = -1;
    }
    errno = saved_error;
}
done: {
    int saved_error = errno;
    funlockfile(stderr);
    funlockfile(stdout);
    errno = saved_error;
    return status;
}
}

static void stop_job(void *resource) {
    ProcJob *job = resource;
    if (!job) return;
    if (job->started) {
        pthread_mutex_lock(&job->lock);
        job->cancel = true;
        pthread_mutex_unlock(&job->lock);
        wake_job(job);
        pthread_join(job->thread, NULL);
    }
    for (int i = 0; i < 2; i++) {
        if (job->wake[i] >= 0) close(job->wake[i]);
        free(job->output[i].bytes);
    }
    free_vector(job->argv);
    free_vector(job->env);
    free(job->cwd); free(job->program); free(job->input);
    pthread_cond_destroy(&job->changed);
    pthread_mutex_destroy(&job->lock);
    free(job);
}

static bool capture(ProcJob *job, ProcCapture *out, const unsigned char *bytes, size_t size) {
    if (size > INT64_MAX - out->observed) return false;
    out->observed += size;
    size_t keep = size;
    if (job->limit && keep > job->limit - out->kept) keep = job->limit - out->kept;
    if (keep > SIZE_MAX - out->kept) return false;
    size_t needed = out->kept + keep;
    if (needed > UINT32_MAX) return false; /* Atom strings have a 32-bit length. */
    if (needed > out->capacity) {
        size_t capacity = out->capacity ? out->capacity : 4096u;
        while (capacity < needed && capacity <= SIZE_MAX / 2u) capacity *= 2u;
        if (capacity < needed) capacity = needed;
        unsigned char *next = realloc(out->bytes, capacity);
        if (!next) return false;
        out->bytes = next; out->capacity = capacity;
    }
    if (keep) memcpy(out->bytes + out->kept, bytes, keep);
    out->kept = needed;
    return true;
}

static void drain(ProcJob *job, int *fd, ProcCapture *out) {
    unsigned char bytes[16384];
    /* Bound each turn so an unending writer cannot starve deadline checks. */
    for (unsigned turn = 0; turn < 16u && *fd >= 0; turn++) {
        ssize_t size = read(*fd, bytes, sizeof bytes);
        if (size > 0) {
            if (!capture(job, out, bytes, (size_t)size)) { job->host_error = ENOMEM; return; }
        } else if (!size) {
            out->eof = true; close(*fd); *fd = -1;
        } else if (errno == EINTR) continue;
        else if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        else { job->host_error = errno; close(*fd); *fd = -1; return; }
    }
}

static void kill_owned(pid_t pid) {
    /* Process-group membership is the guarantee; descendants may leave it. */
    (void)kill(-pid, SIGKILL);
    (void)kill(pid, SIGKILL);
}

static void *execute(void *resource) {
    ProcJob *job = resource;
    int input[2] = {-1,-1}, output[2][2] = {{-1,-1},{-1,-1}};
    pid_t pid = -1;
    if (cancelled(job)) { job->cancelled = true; goto finish; }
    if (owned_pipe(input, O_CLOEXEC) || owned_pipe(output[0], O_CLOEXEC) || owned_pipe(output[1], O_CLOEXEC)) {
        job->launch_error = errno; job->launch_stage = "pipe"; goto finish;
    }
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    int error = posix_spawn_file_actions_init(&actions);
    if (error) { job->launch_error = error; job->launch_stage = "spawn-actions"; goto finish; }
    error = posix_spawnattr_init(&attributes);
    if (error) {
        posix_spawn_file_actions_destroy(&actions);
        job->launch_error = error; job->launch_stage = "spawn-attributes"; goto finish;
    }
    sigset_t empty, defaults;
    sigemptyset(&empty); sigfillset(&defaults);
#define SPAWN_STEP(operation) do { if (!error) error = (operation); } while (0)
    SPAWN_STEP(posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF));
    SPAWN_STEP(posix_spawnattr_setpgroup(&attributes, 0));
    SPAWN_STEP(posix_spawnattr_setsigmask(&attributes, &empty));
    SPAWN_STEP(posix_spawnattr_setsigdefault(&attributes, &defaults));
    SPAWN_STEP(posix_spawn_file_actions_addchdir_np(&actions, job->cwd));
    SPAWN_STEP(posix_spawn_file_actions_adddup2(&actions, input[0], STDIN_FILENO));
    SPAWN_STEP(posix_spawn_file_actions_adddup2(&actions, output[0][1], STDOUT_FILENO));
    SPAWN_STEP(posix_spawn_file_actions_adddup2(&actions, output[1][1], STDERR_FILENO));
    SPAWN_STEP(posix_spawn_file_actions_addclosefrom_np(&actions, 3));
    SPAWN_STEP(posix_spawn(&pid, job->program, &actions, &attributes, job->argv, job->env));
#undef SPAWN_STEP
    posix_spawnattr_destroy(&attributes);
    posix_spawn_file_actions_destroy(&actions);
    if (error) { job->launch_error = error; job->launch_stage = "spawn"; goto finish; }
    close(input[0]); input[0] = -1;
    for (int i = 0; i < 2; i++) { close(output[i][1]); output[i][1] = -1; }
    if (fcntl(input[1], F_SETFL, O_NONBLOCK) < 0) job->host_error = errno;
    for (int i = 0; i < 2; i++)
        if (fcntl(output[i][0], F_SETFL, O_NONBLOCK) < 0) job->host_error = errno;
    /* SIGPIPE is blocked only in this owner; child defaults were restored. */
    sigset_t broken;
    sigemptyset(&broken); sigaddset(&broken, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &broken, NULL);
    size_t sent = 0;
    bool child_exited = false;
    uint64_t started = monotonic_ms();
    while (!child_exited || output[0][0] >= 0 || output[1][0] >= 0) {
        if (cancelled(job)) { job->cancelled = true; break; }
        if (job->timeout_ms && monotonic_ms() - started >= job->timeout_ms) { job->timed_out = true; break; }
        if (job->host_error) break;
        if (input[1] >= 0 && sent == job->input_len) { close(input[1]); input[1] = -1; }
        struct pollfd fds[4] = {{job->wake[0], POLLIN, 0},
            {input[1], POLLOUT, 0}, {output[0][0], POLLIN, 0}, {output[1][0], POLLIN, 0}};
        int ready = poll(fds, 4, 20);
        if (ready < 0 && errno != EINTR) { job->host_error = errno; break; }
        if (input[1] >= 0 && (fds[1].revents & (POLLOUT | POLLHUP | POLLERR))) {
            size_t remaining = job->input_len - sent;
            if (remaining > 16384u) remaining = 16384u;
            ssize_t count = write(input[1], job->input + sent, remaining);
            if (count > 0) { sent += (size_t)count; job->input_written = sent; }
            else if (count < 0 && errno != EINTR && errno != EAGAIN) {
                if (errno != EPIPE) job->host_error = errno;
                close(input[1]); input[1] = -1;
            }
        }
        for (int i = 0; i < 2; i++) if (output[i][0] >= 0) drain(job, &output[i][0], &job->output[i]);
        if (!child_exited) {
            /* Observe without reaping: the child's PID remains reserved
             * while its group may still own capture writers. A deadline
             * must never signal a numerically reused, unrelated PID. */
            siginfo_t observation = {0};
            int observed = waitid(P_PID, (id_t)pid, &observation, WEXITED | WNOHANG | WNOWAIT);
            if (!observed && observation.si_pid == pid) child_exited = true;
            else if (observed < 0 && errno != EINTR) { job->host_error = errno; break; }
        }
    }
    if (job->cancelled || job->timed_out || job->host_error) kill_owned(pid);
    if (!job->waited) {
        pid_t waited;
        do waited = waitpid(pid, &job->wait_status, 0); while (waited < 0 && errno == EINTR);
        job->waited = waited == pid;
        if (!job->waited && !job->host_error) job->host_error = errno;
    }
    for (int i = 0; i < 2; i++) if (output[i][0] >= 0) drain(job, &output[i][0], &job->output[i]);
finish:
    for (int i = 0; i < 2; i++) {
        if (input[i] >= 0) close(input[i]);
        for (int j = 0; j < 2; j++) if (output[i][j] >= 0) close(output[i][j]);
    }
    pthread_mutex_lock(&job->lock);
    job->done = true;
    pthread_cond_broadcast(&job->changed);
    pthread_mutex_unlock(&job->lock);
    return NULL;
}

static char *join_path(const char *base, const char *part) {
    if (*part == '/') return strdup(part);
    size_t length = strlen(base) + strlen(part) + 2u;
    char *result = malloc(length);
    if (result) snprintf(result, length, "%s/%s", base, part);
    return result;
}

static char *resolve_program(ProcJob *job) {
    if (strchr(job->argv[0], '/')) return join_path(job->cwd, job->argv[0]);
    const char *path = "/bin:/usr/bin";
    for (size_t i = 0; job->env[i]; i++) if (!strncmp(job->env[i], "PATH=", 5)) path = job->env[i] + 5;
    char *first = NULL;
    while (true) {
        const char *colon = strchr(path, ':');
        char *directory = strndup(path, colon ? (size_t)(colon-path) : strlen(path));
        char *absolute = directory ? join_path(job->cwd, directory) : NULL;
        char *candidate = absolute ? join_path(absolute, job->argv[0]) : NULL;
        free(directory); free(absolute);
        if (!candidate) { free(first); return NULL; }
        if (!first) first = strdup(candidate);
        if (!access(candidate, X_OK)) { free(first); return candidate; }
        free(candidate);
        if (!colon) return first;
        path = colon + 1;
    }
}

static ProcJob *prepare(Atom *argv, Atom *options, const char **kind, const char **reason) {
    if (!argv || argv->kind != ATOM_EXPR || !argv->expr.len || !options || options->kind != ATOM_EXPR) {
        *reason = "expected a nonempty argv expression and an option expression"; return NULL;
    }
    ProcJob *job = calloc(1, sizeof(*job));
    if (!job) { *kind = "proc:capacity"; *reason = "cannot allocate process owner"; return NULL; }
    job->wake[0] = job->wake[1] = -1;
    if (pthread_mutex_init(&job->lock, NULL)) {
        free(job); *kind = "proc:host-failure"; *reason = "cannot initialize process lock"; return NULL;
    }
    pthread_condattr_t attr;
    int condition_error = pthread_condattr_init(&attr);
    if (!condition_error) {
        condition_error = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
        if (!condition_error) condition_error = pthread_cond_init(&job->changed, &attr);
        pthread_condattr_destroy(&attr);
    }
    if (condition_error) {
        pthread_mutex_destroy(&job->lock); free(job);
        *kind = "proc:host-failure"; *reason = "cannot initialize process clock"; return NULL;
    }
    char *cwd = NULL;
    job->argv = calloc((size_t)argv->expr.len + 1u, sizeof(char *));
    if (!job->argv) goto allocation;
    for (size_t i = 0; i < argv->expr.len; i++) {
        job->argv[i] = text_copy(argv->expr.elems[i]);
        if (!job->argv[i]) {
            if (errno == ENOMEM) goto allocation;
            *reason = "argv must contain strings without embedded NUL"; goto refuse;
        }
    }
    if (!*job->argv[0]) { *reason = "program name is empty"; goto refuse; }
    Atom *env = NULL;
    unsigned seen = 0;
    for (size_t i = 0; i < options->expr.len; i++) {
        Atom *entry = options->expr.elems[i];
        if (!entry || entry->kind != ATOM_EXPR || entry->expr.len != 2u) { *reason = "options are (name value) pairs"; goto refuse; }
        Atom *key = entry->expr.elems[0], *value = entry->expr.elems[1];
        unsigned bit = atom_is_symbol(key, "cwd") ? 1u : atom_is_symbol(key, "env") ? 2u :
            atom_is_symbol(key, "stdin") ? 4u : atom_is_symbol(key, "timeout-ms") ? 8u :
            atom_is_symbol(key, "max-bytes") ? 16u : 0u;
        if (!bit || (seen & bit)) { *reason = "unknown or duplicate process option"; goto refuse; }
        seen |= bit;
        if (bit == 1u) {
            cwd = text_copy(value);
            if (!cwd) {
                if (errno == ENOMEM) goto allocation;
                *reason = "cwd must be a string without NUL"; goto refuse;
            }
        }
        else if (bit == 2u) env = value;
        else if (bit == 4u) {
            const char *bytes;
            if (!bytes_arg(value, &bytes, &job->input_len)) { *reason = "stdin must be a byte string"; goto refuse; }
            job->input = malloc(job->input_len ? job->input_len : 1u);
            if (!job->input) goto allocation;
            memcpy(job->input, bytes, job->input_len);
        } else {
            if (value->kind != ATOM_GROUNDED || value->ground.gkind != GV_INT || value->ground.ival < 0) { *reason = "timeout-ms and max-bytes require nonnegative integers"; goto refuse; }
            if (bit == 8u) job->timeout_ms = (uint64_t)value->ground.ival;
            else job->limit = (size_t)value->ground.ival;
        }
    }
    char *current = getcwd(NULL, 0);
    if (!current) goto host_failure;
    job->cwd = join_path(current, cwd ? cwd : ".");
    free(current); free(cwd); cwd = NULL;
    if (!job->cwd) goto allocation;
    if (!env || atom_is_symbol(env, "inherit")) {
        size_t count = 0; while (environ[count]) count++;
        job->env = calloc(count + 1u, sizeof(char *));
        if (!job->env) goto allocation;
        for (size_t i = 0; i < count; i++) if (!(job->env[i] = strdup(environ[i]))) goto allocation;
    } else {
        if (env->kind != ATOM_EXPR) { *reason = "env must be inherit or a complete environment of string pairs"; goto refuse; }
        job->env = calloc((size_t)env->expr.len + 1u, sizeof(char *));
        if (!job->env) goto allocation;
        for (size_t i = 0; i < env->expr.len; i++) {
            Atom *pair = env->expr.elems[i];
            if (pair->kind != ATOM_EXPR || pair->expr.len != 2u) { *reason = "environment entries are string pairs"; goto refuse; }
            char *name = text_copy(pair->expr.elems[0]);
            int name_error = name ? 0 : errno;
            char *value = text_copy(pair->expr.elems[1]);
            int value_error = value ? 0 : errno;
            if (!name || !value || !*name || strchr(name, '=')) {
                free(name); free(value);
                if (name_error == ENOMEM || value_error == ENOMEM) goto allocation;
                *reason = "invalid environment string"; goto refuse;
            }
            for (size_t j = 0; j < i; j++) if (!strncmp(job->env[j], name, strlen(name)) && job->env[j][strlen(name)] == '=') { free(name); free(value); *reason = "duplicate environment name"; goto refuse; }
            size_t size = strlen(name) + strlen(value) + 2u;
            job->env[i] = malloc(size);
            if (job->env[i]) snprintf(job->env[i], size, "%s=%s", name, value);
            free(name); free(value);
            if (!job->env[i]) goto allocation;
        }
    }
    job->program = resolve_program(job);
    if (!job->program) goto allocation;
    if (owned_pipe(job->wake, O_CLOEXEC | O_NONBLOCK)) goto host_failure;
    return job;
host_failure:
    *kind = "proc:host-failure";
    *reason = strerror(errno);
    goto refuse;
allocation:
    *kind = "proc:capacity";
    *reason = "cannot prepare process resources";
refuse:
    free(cwd);
    stop_job(job);
    return NULL;
}

static Atom *result(Arena *arena, ProcJob *job) {
    Atom *termination = job->launch_error
        ? atom_expr3(arena, atom_symbol(arena, "proc:launch-failed"), atom_int(arena, job->launch_error), atom_symbol(arena, job->launch_stage))
        : job->waited && WIFEXITED(job->wait_status)
        ? atom_expr2(arena, atom_symbol(arena, "proc:exited"), atom_int(arena, WEXITSTATUS(job->wait_status)))
        : job->waited && WIFSIGNALED(job->wait_status)
        ? atom_expr2(arena, atom_symbol(arena, "proc:signaled"), atom_int(arena, WTERMSIG(job->wait_status)))
        : atom_symbol(arena, "proc:not-started");
    Atom *cause = job->cancelled ? atom_symbol(arena, "proc:cancelled") : job->timed_out ? atom_symbol(arena, "proc:timed-out") : job->host_error
        ? atom_expr2(arena, atom_symbol(arena, "proc:host-failed"), atom_int(arena, job->host_error)) : atom_symbol(arena, "proc:normal");
    Atom *captures = atom_expr(arena, (Atom *[]){atom_symbol(arena, "proc:capture"),
        atom_int(arena, (int64_t)job->output[0].kept), atom_int(arena, (int64_t)job->output[0].observed),
        atom_int(arena, (int64_t)job->output[1].kept), atom_int(arena, (int64_t)job->output[1].observed),
        atom_symbol(arena, job->output[0].eof ? "proc:eof" : "proc:open"),
        atom_symbol(arena, job->output[1].eof ? "proc:eof" : "proc:open"),
        atom_expr3(arena, atom_symbol(arena, "proc:input"),
            atom_int(arena, (int64_t)job->input_written),
            atom_int(arena, (int64_t)job->input_len))}, 8u);
    return atom_expr(arena, (Atom *[]){atom_symbol(arena, "proc:result"), termination, cause,
        atom_string_n(arena, (char *)(job->output[0].bytes ? job->output[0].bytes : (unsigned char *)""), job->output[0].kept),
        atom_string_n(arena, (char *)(job->output[1].bytes ? job->output[1].bytes : (unsigned char *)""), job->output[1].kept), captures}, 6u);
}

static Atom *proc_dispatch(CettaLibraryContext *ctx, Arena *arena, Atom *head, Atom **args, uint32_t nargs, bool *interrupted) {
    if (atom_is_symbol(head, "__cetta_lib_proc_abort_signal"))
        return !nargs || (nargs == 1u && args[0]->kind == ATOM_EXPR && !args[0]->expr.len)
            ? atom_int(arena, SIGABRT)
            : failed(arena, "proc:invalid-argument", "abort-signal takes no arguments");
    if (!atom_is_symbol(head, "__cetta_lib_proc_spawn") && !atom_is_symbol(head, "__cetta_lib_proc_poll") &&
        !atom_is_symbol(head, "__cetta_lib_proc_wait") && !atom_is_symbol(head, "__cetta_lib_proc_status") &&
        !atom_is_symbol(head, "__cetta_lib_proc_cancel") && !atom_is_symbol(head, "__cetta_lib_proc_close") &&
        !atom_is_symbol(head, "__cetta_lib_proc_run") && !atom_is_symbol(head, "__cetta_lib_proc_await")) return NULL;
    if (atom_is_symbol(head, "__cetta_lib_proc_run")) {
        Atom *owner = proc_dispatch(ctx, arena, atom_symbol(arena, "__cetta_lib_proc_spawn"), args, nargs, interrupted);
        uint64_t id;
        if (!cetta_native_handle_arg(owner, "process", &id)) return owner;
        Atom *observed = proc_dispatch(ctx, arena, atom_symbol(arena, "__cetta_lib_proc_await"), &owner, 1u, interrupted);
        cetta_native_handle_close(ctx, "process", id);
        return observed;
    }
    if (atom_is_symbol(head, "__cetta_lib_proc_spawn")) {
        const char *reason = "spawn expects argv and options";
        const char *kind = "proc:invalid-argument";
        ProcJob *job = nargs == 2u ? prepare(args[0], args[1], &kind, &reason) : NULL;
        if (!job) return failed(arena, kind, reason);
        uint64_t id;
        if (!cetta_native_handle_alloc(ctx, "process", job, stop_job, &id)) { stop_job(job); return failed(arena, "proc:capacity", "native handle allocation failed"); }
        Atom *owner = cetta_native_handle_owned_atom(ctx, arena, "process", id);
        if (!owner) { cetta_native_handle_close(ctx, "process", id); return failed(arena, "proc:capacity", "native handle publication allocation failed"); }
        int error = pthread_create(&job->thread, NULL, execute, job);
        if (error) { cetta_native_handle_close(ctx, "process", id); return failed(arena, "proc:host-failure", strerror(error)); }
        job->started = true;
        return owner;
    }
    uint64_t id;
    bool wait = atom_is_symbol(head, "__cetta_lib_proc_wait");
    bool await = atom_is_symbol(head, "__cetta_lib_proc_await");
    if (nargs != (wait ? 2u : 1u) || !cetta_native_handle_arg(args[0], "process", &id)) return failed(arena, "proc:invalid-handle", "expected an owned process handle");
    ProcJob *job = cetta_native_handle_get(ctx, "process", id);
    if (!job) return failed(arena, "proc:invalid-handle", "process owner is closed or belongs to another context");
    if (atom_is_symbol(head, "__cetta_lib_proc_close")) { cetta_native_handle_close(ctx, "process", id); return atom_unit(arena); }
    if (atom_is_symbol(head, "__cetta_lib_proc_cancel")) {
        pthread_mutex_lock(&job->lock); job->cancel = true; pthread_mutex_unlock(&job->lock);
        wake_job(job);
        return atom_unit(arena);
    }
    if (wait && (args[1]->kind != ATOM_GROUNDED || args[1]->ground.gkind != GV_INT || args[1]->ground.ival < 0)) return failed(arena, "proc:invalid-argument", "wait timeout must be a nonnegative integer");
    pthread_mutex_lock(&job->lock);
    while (await && !job->done) {
        pthread_mutex_unlock(&job->lock);
        bool cancelled_execution = eval_native_interrupted();
        pthread_mutex_lock(&job->lock);
        if (cancelled_execution) {
            job->cancel = true;
            wake_job(job);
            *interrupted = true;
            break;
        }
        struct timespec turn;
        clock_gettime(CLOCK_MONOTONIC, &turn);
        turn.tv_nsec += 100000000L;
        if (turn.tv_nsec >= 1000000000L) { turn.tv_sec++; turn.tv_nsec -= 1000000000L; }
        int error = pthread_cond_timedwait(&job->changed, &job->lock, &turn);
        if (error && error != ETIMEDOUT) {
            pthread_mutex_unlock(&job->lock);
            return failed(arena, "proc:host-failure", "process wait failed");
        }
    }
    if (wait && !job->done) {
        uint64_t wait_started = monotonic_ms();
        uint64_t duration = (uint64_t)args[1]->ground.ival;
        while (!job->done) {
            pthread_mutex_unlock(&job->lock);
            bool cancelled_execution = eval_native_interrupted();
            pthread_mutex_lock(&job->lock);
            if (cancelled_execution) {
                job->cancel = true; wake_job(job); *interrupted = true; break;
            }
            /* Compare elapsed durations instead of adding an arbitrary
             * timeout to time_t. Large waits need no artificial INT cap. */
            uint64_t elapsed = monotonic_ms() - wait_started;
            if (elapsed >= duration) break;
            uint64_t remaining = duration - elapsed;
            if (remaining > 100u) remaining = 100u;
            struct timespec turn;
            clock_gettime(CLOCK_MONOTONIC, &turn);
            turn.tv_nsec += (long)remaining * 1000000L;
            if (turn.tv_nsec >= 1000000000L) { turn.tv_sec++; turn.tv_nsec -= 1000000000L; }
            int error = pthread_cond_timedwait(&job->changed, &job->lock, &turn);
            if (error && error != ETIMEDOUT) {
                pthread_mutex_unlock(&job->lock);
                return failed(arena, "proc:host-failure", "process wait failed");
            }
        }
    }
    Atom *value;
    if (atom_is_symbol(head, "__cetta_lib_proc_status")) value = atom_symbol(arena, job->consumed ? "proc:consumed" : job->done ? "proc:ready" : "proc:pending");
    else if (job->consumed) value = failed(arena, "proc:already-consumed", "process completion was already taken");
    else if (!job->done) value = atom_symbol(arena, "proc:pending");
    else { value = result(arena, job); job->consumed = true; }
    pthread_mutex_unlock(&job->lock);
    return value;
}

bool cetta_proc_call(CettaLibraryContext *ctx, Arena *arena, Atom *head, Atom **args, uint32_t nargs, CettaCallOutcome *out) {
    bool interrupted = false;
    Atom *value = proc_dispatch(ctx, arena, head, args, nargs, &interrupted);
    if (!value) return false;
    *out = interrupted ? cetta_call_interrupted() : cetta_call_completed_value(value);
    return true;
}
