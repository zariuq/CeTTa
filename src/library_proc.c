#define _GNU_SOURCE
#include "library_proc.h"
#include "eval.h"
#include "symbol.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef CLOSE_RANGE_CLOEXEC
#define CLOSE_RANGE_CLOEXEC (1U << 2)
#endif

enum { PROC_FD_SWEEP = 65536 };
extern char **environ;

/* Where a child that could not become the program stopped. */
enum { START_STDIO = 1, START_CWD = 2, START_EXEC = 3 };

static const char *text(Atom *a) {
    if (a && a->kind == ATOM_GROUNDED && a->ground.gkind == GV_STRING)
        return memchr(a->ground.sval, '\0', a->ground.slen) ? NULL : a->ground.sval;
    /* PeTTa's Python/Prolog text is an atom, including strings returned by
     * py-call and read by sread. HE retains its grounded-string contract. */
    if (a && a->kind == ATOM_SYMBOL && eval_current_language_id &&
        eval_current_language_id() == CETTA_LANGUAGE_PETTA)
        return atom_name_cstr(a);
    return NULL;
}

static bool integer(Atom *a, int64_t lo, int64_t hi, int64_t *out) {
    if (!a || a->kind != ATOM_GROUNDED) return false;
    if (a->ground.gkind == GV_INT) {
        if (a->ground.ival < lo || a->ground.ival > hi) return false;
        *out = a->ground.ival;
    } else if (a->ground.gkind == GV_FLOAT) {
        double value = a->ground.fval;
        /* HE's ceil returns an integral float. Exclude the rounded 2^63
         * boundary before converting it to a signed integer. */
        if (!isfinite(value) || value < lo || value >= 0x1p63 ||
            trunc(value) != value) return false;
        *out = (int64_t)value;
        if (*out > hi) return false;
    } else return false;
    return true;
}

static Atom *invalid(Arena *arena, const char *message) {
    Atom *operation = atom_symbol(arena, "proc:run");
    return atom_error(arena, atom_expr(arena, &operation, 1u), atom_string(arena, message));
}

static Atom *failed(Arena *arena, const char *reason) {
    return atom_expr2(arena, atom_symbol(arena, "proc:failed"), atom_string(arena, reason));
}

static Atom *failed_errno(Arena *arena, const char *stage, int error) {
    char reason[256];
    snprintf(reason, sizeof(reason), "%s: %s", stage, strerror(error));
    return failed(arena, reason);
}

static uint64_t now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
}

/* The length of the well-formed UTF-8 sequence at p, or 0 if there is none
 * (a NUL, a stray or overlong byte, a surrogate, a truncated sequence). */
static size_t utf8_sequence(const unsigned char *p, size_t left) {
    unsigned char c = p[0], lo = 0x80, hi = 0xBF;
    size_t n;
    if (c == 0) return 0;
    if (c < 0x80) return 1;
    if (c >= 0xC2 && c <= 0xDF) n = 2;
    else if (c >= 0xE0 && c <= 0xEF) {
        n = 3;
        if (c == 0xE0) lo = 0xA0;
        else if (c == 0xED) hi = 0x9F;
    } else if (c >= 0xF0 && c <= 0xF4) {
        n = 4;
        if (c == 0xF0) lo = 0x90;
        else if (c == 0xF4) hi = 0x8F;
    } else return 0;
    if (left < n || p[1] < lo || p[1] > hi) return 0;
    for (size_t k = 2; k < n; ++k)
        if ((p[k] & 0xC0) != 0x80) return 0;
    return n;
}

/* Captured bytes as text: anything that is not well-formed UTF-8 reads as
 * U+FFFD, one per byte. */
static Atom *as_text(Arena *arena, const char *bytes, size_t len) {
    size_t bad = 0;
    for (size_t i = 0; i < len;) {
        /* Most tool output is ASCII. Validate eight bytes together without
         * allocating a three-times-sized copy of an already valid string. */
        if (len - i >= sizeof(uint64_t)) {
            uint64_t word;
            memcpy(&word, bytes + i, sizeof(word));
            uint64_t high = UINT64_C(0x8080808080808080);
            if (!(word & high) &&
                !((word - UINT64_C(0x0101010101010101)) & ~word & high)) {
                i += sizeof(word);
                continue;
            }
        }
        size_t n = utf8_sequence((const unsigned char *)bytes + i, len - i);
        if (!n) ++bad;
        i += n ? n : 1;
    }
    if (!bad) return atom_string_n(arena, bytes, len);
    if (bad > (SIZE_MAX - len - 1) / 2)
        return atom_string(arena, "[output unavailable: text is too large]");
    char *out = malloc(len + bad * 2 + 1);
    if (!out) return atom_string(arena, "[output unavailable: out of memory]");
    size_t o = 0;
    for (size_t i = 0; i < len;) {
        size_t n = utf8_sequence((const unsigned char *)bytes + i, len - i);
        if (n) {
            memcpy(out + o, bytes + i, n);
            o += n;
            i += n;
        } else {
            memcpy(out + o, "\xEF\xBF\xBD", 3);
            o += 3;
            i += 1;
        }
    }
    out[o] = 0;
    Atom *value = atom_string_n(arena, out, o);
    free(out);
    return value;
}

/* One of the child's output streams: kept up to its limit, drained past it
 * so that the child never blocks on a full pipe. */
typedef struct {
    int fd;
    bool open;
    char *data;
    size_t len, cap, limit, total;
    int error;
} Stream;

static void stream_read(Stream *s) {
    char chunk[65536];
    /* Give the other stream, deadline and observer a turn even when a child
     * writes continuously. Nothing is discarded by this read quantum. */
    for (unsigned reads = 0; s->open && reads < 16; ++reads) {
        ssize_t n = read(s->fd, chunk, sizeof(chunk));
        if (n > 0) {
            s->total += (size_t)n;
            size_t room = s->limit > s->len ? s->limit - s->len : 0;
            size_t take = (size_t)n < room ? (size_t)n : room;
            if (take && s->len + take > s->cap) {
                size_t cap = s->cap ? s->cap : 4096;
                while (cap < s->len + take) {
                    if (cap > SIZE_MAX / 2) { cap = s->len + take; break; }
                    cap *= 2;
                }
                if (cap > s->limit) cap = s->limit;
                char *grown = realloc(s->data, cap);
                if (!grown) { s->error = ENOMEM; take = 0; }
                else { s->data = grown; s->cap = cap; }
            }
            if (take) {
                memcpy(s->data + s->len, chunk, take);
                s->len += take;
            }
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        if (n < 0) s->error = errno;
        close(s->fd);
        s->fd = -1;
        s->open = false;
    }
}

static void file_capture(Stream *s, FILE *file) {
    struct stat st;
    int fd = fileno(file);
    if (fstat(fd, &st) || st.st_size < 0) { s->error = errno ? errno : EIO; return; }
    s->total = (size_t)st.st_size;
    size_t keep = s->total < s->limit ? s->total : s->limit;
    if (!keep) return;
    s->data = malloc(keep);
    if (!s->data) { s->error = ENOMEM; return; }
    s->cap = keep;
    while (s->len < keep) {
        size_t count = keep - s->len;
        if (count > 65536) count = 65536;
        ssize_t got = pread(fd, s->data + s->len, count, (off_t)s->len);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) { s->error = got < 0 ? errno : EIO; return; }
        s->len += (size_t)got;
    }
}

static Atom *stream_text(Arena *arena, const Stream *s) {
    if (s->total == s->len) return as_text(arena, s->data, s->len);
    char note[160];
    int count = snprintf(note, sizeof(note),
        "\n[output truncated: kept %zu of %zu bytes]\n", s->len, s->total);
    size_t length = s->len + (size_t)count;
    char *joined = malloc(length);
    if (!joined) return atom_string(arena, "[output unavailable: out of memory]");
    if (s->len) memcpy(joined, s->data, s->len);
    memcpy(joined + s->len, note, (size_t)count);
    Atom *value = as_text(arena, joined, length);
    free(joined);
    return value;
}

/* Optional observation of a blocking call. This never admits, refuses or
 * interrupts work: it only refreshes a progress file and the service watchdog.
 * Like libuv's exit callback, process identity belongs to the parent. */
void cetta_proc_observe_wait(const char *path, uint64_t sequence) {
    const char *address = getenv("NOTIFY_SOCKET");
    if (address && address[0]) {
        struct sockaddr_un peer = {.sun_family = AF_UNIX};
        size_t length = strlen(address);
        if (length < sizeof(peer.sun_path)) {
            memcpy(peer.sun_path, address, length + 1);
            if (address[0] == '@') peer.sun_path[0] = 0;
            int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
            if (fd >= 0) {
                socklen_t size = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + length +
                                             (address[0] != '@'));
                (void)sendto(fd, "WATCHDOG=1", 10, MSG_NOSIGNAL, (struct sockaddr *)&peer, size);
                close(fd);
            }
        }
    }
    if (!path || !path[0]) return;
    char owner[96], stat[4096];
    snprintf(owner, sizeof(owner), "%ld:portable", (long)getpid());
    FILE *file = fopen("/proc/self/stat", "r");
    if (file) {
        if (fgets(stat, sizeof(stat), file)) {
            char *p = strrchr(stat, ')');
            if (p) {
                p += 2;
                for (int field = 3; field < 22 && p; ++field) {
                    p = strchr(p, ' ');
                    if (p) ++p;
                }
                if (p) snprintf(owner, sizeof(owner), "%ld:%llu", (long)getpid(), strtoull(p, NULL, 10));
            }
        }
        fclose(file);
    }
    struct timespec wall, clock;
    clock_gettime(CLOCK_REALTIME, &wall);
    clock_gettime(CLOCK_MONOTONIC, &clock);
    char body[512];
    int length = snprintf(body, sizeof(body),
        "{\"pid\":%ld,\"owner\":\"%s\",\"sequence\":%llu,\"phase\":\"process-wait\","
        "\"progress_at\":%llu.%09lu,\"progress_clock\":%llu.%09lu,"
        "\"wait_until\":%llu,\"memory_bytes\":null}\n",
        (long)getpid(), owner, (unsigned long long)sequence,
        (unsigned long long)wall.tv_sec, (unsigned long)wall.tv_nsec,
        (unsigned long long)clock.tv_sec, (unsigned long)clock.tv_nsec,
        (unsigned long long)clock.tv_sec + 60);
    char *temporary = malloc(strlen(path) + 20);
    if (!temporary) return;
    sprintf(temporary, "%s.proc-XXXXXX", path);
    int fd = mkstemp(temporary);
    if (fd >= 0) {
        ssize_t written = write(fd, body, (size_t)length);
        close(fd);
        if (written == length) (void)rename(temporary, path);
        (void)unlink(temporary);
    }
    free(temporary);
}

/* The program's path: as given when it names a path (a relative one then
 * resolves in the child's working directory), otherwise the first
 * executable regular file of that name on the PATH of the child's own
 * environment. Without a PATH there, only a path runs. */
static char *resolve_program(const char *program, const char *path_env, const char *cwd) {
    if (strchr(program, '/')) return strdup(program);
    if (!path_env) return NULL;
    for (const char *p = path_env;;) {
        const char *colon = strchr(p, ':');
        size_t dir_len = colon ? (size_t)(colon - p) : strlen(p);
        size_t size = strlen(cwd) + dir_len + strlen(program) + 3;
        char *candidate = malloc(size);
        if (!candidate) return NULL;
        if (dir_len == 0)
            snprintf(candidate, size, "%s/%s", cwd, program);
        else if (p[0] == '/')
            snprintf(candidate, size, "%.*s/%s", (int)dir_len, p, program);
        else
            snprintf(candidate, size, "%s/%.*s/%s", cwd, (int)dir_len, p, program);
        struct stat st;
        if (stat(candidate, &st) == 0 && S_ISREG(st.st_mode) && access(candidate, X_OK) == 0)
            return candidate;
        free(candidate);
        if (!colon) return NULL;
        p = colon + 1;
    }
}

/* In the child, between fork and exec, only async-signal-safe calls. */
static void child_stop(int fd, int stage, int error) {
    int report[2] = {stage, error};
    ssize_t ignored;
    do { ignored = write(fd, report, sizeof(report)); } while (ignored < 0 && errno == EINTR);
    (void)ignored;
    _exit(127);
}

static void child_start(const char *program, char **argv, char **envp, const char *cwd,
                        int out, int err, int report) {
    setpgid(0, 0);
    struct sigaction standard;
    memset(&standard, 0, sizeof(standard));
    standard.sa_handler = SIG_DFL;
    sigemptyset(&standard.sa_mask);
    for (int sig = 1; sig < NSIG; ++sig)
        if (sig != SIGKILL && sig != SIGSTOP) sigaction(sig, &standard, NULL);
    sigset_t none;
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, NULL);
    int in = open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (in < 0 || dup2(in, 0) < 0 || dup2(out, 1) < 0 || dup2(err, 2) < 0)
        child_stop(report, START_STDIO, errno);
    /* Nothing of this process but the three streams reaches the program. */
    if (syscall(SYS_close_range, 3u, ~0u, CLOSE_RANGE_CLOEXEC) != 0)
        for (int fd = 3; fd < PROC_FD_SWEEP; ++fd) fcntl(fd, F_SETFD, FD_CLOEXEC);
    if (chdir(cwd) != 0) child_stop(report, START_CWD, errno);
    execve(program, argv, envp);
    child_stop(report, START_EXEC, errno);
}

struct CettaProcChild {
    pid_t pid;
    int pidfd, report, status, error;
    bool finished, timed_out, killed;
    uint64_t started, deadline, finished_at;
    char *out, *err;
};

CettaProcChild *cetta_proc_start(Atom *arguments, const char *cwd, Atom *environment,
    uint64_t timeout_ms, const char *out, const char *err, char *error, size_t error_size) {
    struct sigaction child_exit;
    if (sigaction(SIGCHLD, NULL, &child_exit) || child_exit.sa_handler == SIG_IGN ||
        (child_exit.sa_flags & SA_NOCLDWAIT)) {
        snprintf(error, error_size, "process start: host discards child exit status (SIGCHLD)");
        return NULL;
    }
    bool inherit = environment && environment->kind == ATOM_SYMBOL &&
                   !strcmp(atom_name_cstr(environment), "inherit");
    if (!arguments || arguments->kind != ATOM_EXPR || !arguments->expr.len ||
        (!inherit && (!environment || environment->kind != ATOM_EXPR)) ||
        !cwd || !*cwd || !out || !*out || !err || !*err) {
        snprintf(error, error_size, "expected argv, cwd, environment and two output paths");
        return NULL;
    }
    size_t argc = arguments->expr.len, envc = inherit ? 0 : environment->expr.len;
    char **argv = calloc(argc + 1, sizeof(*argv)), **envp = calloc(envc + 1, sizeof(*envp));
    char *program = NULL;
    int output = -1, errors = -1, report[2] = {-1, -1};
    CettaProcChild *child = calloc(1, sizeof(*child));
    if (child) { child->pidfd = -1; child->report = -1; }
    if (!argv || !envp || !child) { errno = ENOMEM; goto failed; }
    for (size_t i = 0; i < argc; ++i) {
        argv[i] = (char *)text(arguments->expr.elems[i]);
        if (!argv[i]) { errno = EINVAL; goto failed; }
    }
    if (!argv[0][0]) { errno = EINVAL; goto failed; }
    const char *path = inherit ? getenv("PATH") : NULL;
    for (size_t i = 0; i < envc; ++i) {
        Atom *pair = environment->expr.elems[i];
        const char *name = pair && pair->kind == ATOM_EXPR && pair->expr.len == 2
                         ? text(pair->expr.elems[0]) : NULL;
        const char *value = name ? text(pair->expr.elems[1]) : NULL;
        if (!name || !value || !*name || strchr(name, '=')) { errno = EINVAL; goto failed; }
        size_t length = strlen(name) + strlen(value) + 2;
        envp[i] = malloc(length);
        if (!envp[i]) { errno = ENOMEM; goto failed; }
        snprintf(envp[i], length, "%s=%s", name, value);
        if (!strcmp(name, "PATH")) path = value;
    }
    program = resolve_program(argv[0], path, cwd);
    if (!program) { errno = ENOENT; goto failed; }
    child->out = strdup(out); child->err = strdup(err);
    if (!child->out || !child->err) { errno = ENOMEM; goto failed; }
    output = open(out, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (output < 0) goto failed;
    errors = !strcmp(out, err) ? fcntl(output, F_DUPFD_CLOEXEC, 3)
           : open(err, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (errors < 0 || pipe2(report, O_CLOEXEC | O_NONBLOCK)) goto failed;
    struct stat a, b;
    if (!fstat(output, &a) && !fstat(errors, &b) && a.st_dev == b.st_dev && a.st_ino == b.st_ino) {
        close(errors); errors = fcntl(output, F_DUPFD_CLOEXEC, 3);
        if (errors < 0) goto failed;
    }
    child->started = now_ms();
    child->deadline = timeout_ms ? child->started + timeout_ms : UINT64_MAX;
    /* The child resets inherited handlers before unblocking. An immediate
     * io:signal must never run the embedding host's handlers before exec. */
    sigset_t blocked, previous;
    sigfillset(&blocked);
    int mask_error = pthread_sigmask(SIG_SETMASK, &blocked, &previous);
    if (mask_error) { errno = mask_error; goto failed; }
    child->pid = fork();
    if (!child->pid) child_start(program, argv, inherit ? environ : envp, cwd, output, errors, report[1]);
    int fork_error = errno;
    (void)pthread_sigmask(SIG_SETMASK, &previous, NULL);
    if (child->pid < 0) { child->pid = 0; errno = fork_error; goto failed; }
    setpgid(child->pid, child->pid);
    close(report[1]); report[1] = -1;
#ifndef CETTA_PROC_NO_PIDFD
    child->pidfd = (int)syscall(SYS_pidfd_open, child->pid, 0);
#endif
    /* Only inspect the atomic child report after reaping. Another subsystem's
     * fork can inherit the pipe, so waiting for EOF here can hang indefinitely. */
    child->report = report[0]; report[0] = -1;
    goto done;
failed:
    snprintf(error, error_size, "process start: %s", strerror(errno));
    cetta_proc_free(child); child = NULL;
done:
    if (output >= 0) close(output);
    if (errors >= 0) close(errors);
    if (report[0] >= 0) close(report[0]);
    if (report[1] >= 0) close(report[1]);
    if (envp) for (size_t i = 0; i < envc; ++i) free(envp[i]);
    free(envp); free(argv); free(program);
    return child;
}

bool cetta_proc_poll(CettaProcChild *child) {
    if (!child || child->finished) return true;
    pid_t observed;
    do { observed = waitpid(child->pid, &child->status, WNOHANG); } while (observed < 0 && errno == EINTR);
    if (observed == child->pid) {
        int stopped[2];
        ssize_t got;
        do { got = read(child->report, stopped, sizeof(stopped)); } while (got < 0 && errno == EINTR);
        if (got == (ssize_t)sizeof(stopped)) child->error = stopped[1];
        else if (got > 0) child->error = EIO;
        child->finished = true;
    }
    else if (observed < 0) { child->error = errno; child->finished = true; }
    else if (!child->killed && now_ms() >= child->deadline) {
        child->timed_out = true;
        kill(-child->pid, SIGKILL); kill(child->pid, SIGKILL);
        child->killed = true;
    }
    if (child->finished) {
        child->finished_at = now_ms();
        if (child->report >= 0) { close(child->report); child->report = -1; }
    }
    return child->finished;
}
int cetta_proc_descriptor(const CettaProcChild *child) { return child ? child->pidfd : -1; }
uint64_t cetta_proc_deadline(const CettaProcChild *child) { return child && !child->killed ? child->deadline : UINT64_MAX; }
bool cetta_proc_signal(CettaProcChild *child, int number) {
    if (!child || number < 0 || number >= NSIG || child->finished) return false;
    bool sent = kill(-child->pid, number) == 0;
    /* The owned leader can move to another group. Its unreaped PID is still
     * ours, so cancellation must reach it as well as its original group. */
    if (number == SIGKILL) {
        bool leader_sent = kill(child->pid, number) == 0;
        sent = sent || leader_sent;
        if (sent) child->killed = true;
    }
    return sent;
}
void cetta_proc_snapshot(const CettaProcChild *child, CettaProcSnapshot *out) {
    *out = (CettaProcSnapshot){ .pid = child->pid,
        .elapsed_ms = (child->finished ? child->finished_at : now_ms()) - child->started,
        .status = child->status, .error = child->error,
        .finished = child->finished, .timed_out = child->timed_out };
}
Atom *cetta_proc_information(Arena *arena, CettaProcChild *child) {
    Atom *parts[] = {atom_symbol(arena, "proc:running"), atom_int(arena, child->pid),
                    atom_int(arena, (int64_t)(now_ms() - child->started)),
                    atom_string(arena, child->out), atom_string(arena, child->err)};
    return atom_expr(arena, parts, 5);
}
Atom *cetta_proc_outcome(Arena *arena, CettaProcChild *child) {
    const char *kind = child->error ? "failed" : child->timed_out ? "timed-out"
                     : WIFEXITED(child->status) ? "exited" : "signaled";
    int status = child->error ? child->error : child->timed_out ? 0
               : WIFEXITED(child->status) ? WEXITSTATUS(child->status) : WTERMSIG(child->status);
    Atom *parts[] = {atom_symbol(arena, "proc:finished"), atom_int(arena, child->pid),
        atom_symbol(arena, kind), atom_int(arena, status),
        atom_string(arena, child->out), atom_string(arena, child->err)};
    return atom_expr(arena, parts, 6);
}
void cetta_proc_free(CettaProcChild *child) {
    if (!child) return;
    if (child->pid > 0 && !cetta_proc_poll(child)) {
        kill(-child->pid, SIGKILL); kill(child->pid, SIGKILL);
        while (waitpid(child->pid, &child->status, 0) < 0 && errno == EINTR) {}
    }
    if (child->pidfd >= 0) close(child->pidfd);
    if (child->report >= 0) close(child->report);
    free(child->out); free(child->err); free(child);
}

static Atom *proc_run(Arena *arena, Atom **args, uint32_t nargs) {
    if (nargs != 5u && nargs != 6u)
        return invalid(arena, "expected (proc:run argv cwd env timeout-ms max-bytes [options])");
    Atom *argv_atom = args[0], *env_atom = args[2];
    const char *cwd = text(args[1]);
    bool inherit = env_atom && env_atom->kind == ATOM_SYMBOL &&
                   strcmp(atom_name_cstr(env_atom), "inherit") == 0;
    bool merged = false;
    bool file_output = false;
    const char *progress = "";
    if (nargs == 6u) {
        Atom *options = args[5];
        const char *mode = text(options);
        if (options && options->kind == ATOM_EXPR && options->expr.len == 3u &&
            options->expr.elems[0]->kind == ATOM_SYMBOL &&
            strcmp(atom_name_cstr(options->expr.elems[0]), "proc:options") == 0) {
            mode = text(options->expr.elems[1]);
            progress = text(options->expr.elems[2]);
        }
        if (!mode || !progress || (strcmp(mode, "merged") && strcmp(mode, "merged-file") && strcmp(mode, "separate")))
            return invalid(arena, "options mode must be merged, merged-file or separate");
        merged = strcmp(mode, "separate") != 0;
        file_output = strcmp(mode, "merged-file") == 0;
    }
    int64_t timeout, max_bytes;
    if (!argv_atom || argv_atom->kind != ATOM_EXPR || argv_atom->expr.len < 1u)
        return invalid(arena, "argv must be a list of one or more strings");
    if (!inherit && (!env_atom || env_atom->kind != ATOM_EXPR))
        return invalid(arena, "env must be a list of (\"NAME\" \"value\") pairs");
    if (!cwd || !cwd[0])
        return invalid(arena, "cwd must be a directory path");
    if (!integer(args[3], 0, INT64_MAX, &timeout))
        return invalid(arena, "timeout-ms must be a nonnegative integer (0 means no deadline)");
    if (!integer(args[4], 0, INT64_MAX, &max_bytes))
        return invalid(arena, "max-bytes must be a nonnegative integer (0 means unlimited)");

    size_t argc = (size_t)argv_atom->expr.len, envc = inherit ? 0 : (size_t)env_atom->expr.len;
    char **argv = calloc(argc + 1, sizeof(*argv));
    char **envp = calloc(envc + 1, sizeof(*envp));
    char *program = NULL;
    Atom *result = NULL;
    FILE *capture = NULL;
    Stream streams[2] = {{.fd = -1}, {.fd = -1}};
    int out[2] = {-1, -1}, err[2] = {-1, -1}, report[2] = {-1, -1}, pidfd = -1;
    if (!argv || !envp) { result = failed(arena, "out of memory"); goto done; }
    for (size_t i = 0; i < argc; ++i) {
        const char *arg = text(argv_atom->expr.elems[i]);
        if (!arg) { result = invalid(arena, "argv must be a list of one or more strings"); goto done; }
        argv[i] = (char *)arg;
    }
    if (!argv[0][0]) { result = invalid(arena, "the program name is empty"); goto done; }
    const char *path_env = inherit ? getenv("PATH") : NULL;
    for (size_t i = 0; i < envc; ++i) {
        Atom *pair = env_atom->expr.elems[i];
        const char *name = pair && pair->kind == ATOM_EXPR && pair->expr.len == 2u
            ? text(pair->expr.elems[0]) : NULL;
        const char *value = name ? text(pair->expr.elems[1]) : NULL;
        if (!name || !value || !name[0] || strchr(name, '=')) {
            result = invalid(arena, "env must be a list of (\"NAME\" \"value\") pairs");
            goto done;
        }
        size_t size = strlen(name) + strlen(value) + 2;
        if (!(envp[i] = malloc(size))) { result = failed(arena, "out of memory"); goto done; }
        snprintf(envp[i], size, "%s=%s", name, value);
        if (strcmp(name, "PATH") == 0) path_env = value;
    }
    if (!(program = resolve_program(argv[0], path_env, cwd))) {
        result = failed(arena, "program not found");
        goto done;
    }
    if (file_output) {
        capture = tmpfile();
        if (!capture || (out[1] = dup(fileno(capture))) < 0) {
            result = failed_errno(arena, "output file", errno);
            goto done;
        }
    }
    if ((!file_output && pipe2(out, O_CLOEXEC)) || (!merged && pipe2(err, O_CLOEXEC)) || pipe2(report, O_CLOEXEC)) {
        result = failed_errno(arena, "pipe", errno);
        goto done;
    }
    pid_t pid = fork();
    if (pid < 0) { result = failed_errno(arena, "fork", errno); goto done; }
    if (pid == 0) child_start(program, argv, inherit ? environ : envp, cwd,
                              out[1], merged ? out[1] : err[1], report[1]);
    setpgid(pid, pid);
    close(out[1]); out[1] = -1;
    if (err[1] >= 0) close(err[1]);
    err[1] = -1;
    close(report[1]); report[1] = -1;
    uint64_t deadline = timeout ? now_ms() + (uint64_t)timeout : UINT64_MAX;
    uint64_t observation = 0, sequence = 0;
    bool observed = progress[0] || getenv("NOTIFY_SOCKET");

    /* The report pipe closes at exec; a report instead says where the child
     * stopped. */
    int stopped[2];
    ssize_t got = 0;
    for (;;) {
        uint64_t now = now_ms();
        if (observed && now >= observation) {
            cetta_proc_observe_wait(progress, ++sequence);
            observation = now + 30000;
        }
        struct pollfd p = {report[0], POLLIN, 0};
        uint64_t left = now < deadline ? deadline - now : 0;
        if (observed && left > observation - now) left = observation - now;
        int n = left ? poll(&p, 1, left > INT_MAX ? INT_MAX : (int)left) : 0;
        if (n < 0 && errno == EINTR) continue;
        if (n > 0) got = read(report[0], stopped, sizeof(stopped));
        if (n > 0 && got < 0 && errno == EINTR) continue;
        if (n == 0 && now_ms() < deadline) continue;
        break;
    }
    if (got == (ssize_t)sizeof(stopped)) {
        waitpid(pid, NULL, 0);
        const char *stage = stopped[0] == START_CWD ? "cwd"
            : stopped[0] == START_EXEC ? "exec" : "stdio";
        result = failed_errno(arena, stage, stopped[1]);
        goto done;
    }

    size_t limit = max_bytes ? (size_t)max_bytes : SIZE_MAX;
    streams[0] = (Stream){.fd = out[0], .open = !file_output, .limit = limit};
    streams[1] = (Stream){.fd = err[0], .open = !merged, .limit = limit};
    out[0] = err[0] = -1;
    for (int i = 0; i < 2; ++i)
        if (streams[i].open) fcntl(streams[i].fd, F_SETFL, fcntl(streams[i].fd, F_GETFL) | O_NONBLOCK);
    pidfd = (int)syscall(SYS_pidfd_open, pid, 0);
    bool exited = false, timed_out = false;
    int status = 0, wait_error = 0;
    while (streams[0].open || streams[1].open || !exited) {
        uint64_t now = now_ms();
        if (now >= deadline) { timed_out = true; break; }
        if (observed && now >= observation) {
            cetta_proc_observe_wait(progress, ++sequence);
            observation = now + 30000;
        }
        struct pollfd fds[3];
        int n = 0;
        for (int i = 0; i < 2; ++i)
            if (streams[i].open) fds[n++] = (struct pollfd){streams[i].fd, POLLIN, 0};
        if (!exited && pidfd >= 0) fds[n++] = (struct pollfd){pidfd, POLLIN, 0};
        uint64_t wait = deadline - now;
        if (observed && wait > observation - now) wait = observation - now;
        if (!exited && pidfd < 0 && wait > 20) wait = 20;
        if (poll(fds, (nfds_t)n, wait > INT_MAX ? INT_MAX : (int)wait) < 0 && errno != EINTR) {
            wait_error = errno;
            break;
        }
        for (int i = 0; i < 2; ++i) stream_read(&streams[i]);
        if (!exited && waitpid(pid, &status, WNOHANG) == pid) exited = true;
    }
    if (timed_out || wait_error) {
        /* The whole process group goes, whatever it started. */
        kill(-pid, SIGKILL);
        kill(pid, SIGKILL);
        if (!exited) waitpid(pid, &status, 0);
        for (int i = 0; i < 2; ++i) stream_read(&streams[i]);
    }
    if (capture) file_capture(&streams[0], capture);
    if (wait_error) {
        result = failed_errno(arena, "wait", wait_error);
        goto done;
    }
    if (streams[0].error || streams[1].error) {
        result = failed_errno(arena, "capture", streams[0].error ? streams[0].error : streams[1].error);
        goto done;
    }
    Atom *out_text = stream_text(arena, &streams[0]);
    Atom *err_text = stream_text(arena, &streams[1]);
    if (timed_out) {
        Atom *parts[] = {atom_symbol(arena, "proc:timed-out"), out_text, err_text};
        result = atom_expr(arena, parts, 3u);
    } else if (WIFEXITED(status)) {
        Atom *parts[] = {atom_symbol(arena, "proc:exited"),
                         atom_int(arena, WEXITSTATUS(status)), out_text, err_text};
        result = atom_expr(arena, parts, 4u);
    } else if (WIFSIGNALED(status)) {
        Atom *parts[] = {atom_symbol(arena, "proc:signaled"),
                         atom_int(arena, WTERMSIG(status)), out_text, err_text};
        result = atom_expr(arena, parts, 4u);
    } else {
        result = failed(arena, "the child ended in an unknown state");
    }
done:
    if (capture) fclose(capture);
    for (int i = 0; i < 2; ++i) {
        if (streams[i].open) close(streams[i].fd);
        free(streams[i].data);
    }
    int fds[] = {out[0], out[1], err[0], err[1], report[0], report[1], pidfd};
    for (size_t i = 0; i < sizeof(fds) / sizeof(*fds); ++i)
        if (fds[i] >= 0) close(fds[i]);
    if (envp)
        for (size_t i = 0; i < envc; ++i) free(envp[i]);
    free(envp);
    free(argv);
    free(program);
    return result;
}

Atom *cetta_proc_dispatch(Arena *arena, Atom *head, Atom **args, uint32_t nargs) {
    if (!arena || !head || head->kind != ATOM_SYMBOL || atom_symbol_current_id(head) != g_builtin_syms.lib_proc_run)
        return NULL;
    return proc_run(arena, args, nargs);
}
