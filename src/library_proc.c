#define _GNU_SOURCE
#include "library_proc.h"
#include "eval.h"
#include "symbol.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef CLOSE_RANGE_CLOEXEC
#define CLOSE_RANGE_CLOEXEC (1U << 2)
#endif

enum {
    PROC_ARGV_MAX = 4096,
    PROC_ENV_MAX = 4096,
    PROC_TIMEOUT_MAX_MS = 3600000,
    PROC_OUTPUT_MAX = 64 << 20,
    PROC_FD_SWEEP = 65536,
};

/* Where a child that could not become the program stopped. */
enum { START_STDIO = 1, START_CWD = 2, START_EXEC = 3 };

static const char *text(Atom *a) {
    if (a && a->kind == ATOM_GROUNDED && a->ground.gkind == GV_STRING)
        return a->ground.sval;
    /* PeTTa's Python/Prolog text is an atom, including strings returned by
     * py-call and read by sread. HE retains its grounded-string contract. */
    if (a && a->kind == ATOM_SYMBOL && eval_current_language_id &&
        eval_current_language_id() == CETTA_LANGUAGE_PETTA)
        return atom_name_cstr(a);
    return NULL;
}

static bool integer(Atom *a, int64_t lo, int64_t hi, int64_t *out) {
    if (!a || a->kind != ATOM_GROUNDED || a->ground.gkind != GV_INT ||
        a->ground.ival < lo || a->ground.ival > hi) return false;
    *out = a->ground.ival;
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
    char *out = arena_alloc(arena, len * 3 + 1);
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
    return atom_string(arena, out);
}

/* One of the child's output streams: kept up to its limit, drained past it
 * so that the child never blocks on a full pipe. */
typedef struct {
    int fd;
    bool open;
    char *data;
    size_t len, cap, limit;
} Stream;

static void stream_read(Stream *s) {
    char chunk[65536];
    while (s->open) {
        ssize_t n = read(s->fd, chunk, sizeof(chunk));
        if (n > 0) {
            size_t room = s->limit > s->len ? s->limit - s->len : 0;
            size_t take = (size_t)n < room ? (size_t)n : room;
            if (take && s->len + take > s->cap) {
                size_t cap = s->cap ? s->cap : 4096;
                while (cap < s->len + take) cap *= 2;
                if (cap > s->limit) cap = s->limit;
                char *grown = realloc(s->data, cap);
                if (!grown) take = 0;
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
        close(s->fd);
        s->fd = -1;
        s->open = false;
    }
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
    ssize_t ignored = write(fd, report, sizeof(report));
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

static Atom *proc_run(Arena *arena, Atom **args, uint32_t nargs) {
    if (nargs != 5u)
        return invalid(arena, "expected (proc:run argv cwd env timeout-ms max-bytes)");
    Atom *argv_atom = args[0], *env_atom = args[2];
    const char *cwd = text(args[1]);
    int64_t timeout, max_bytes;
    if (!argv_atom || argv_atom->kind != ATOM_EXPR || argv_atom->expr.len < 1u ||
        argv_atom->expr.len > PROC_ARGV_MAX)
        return invalid(arena, "argv must be a list of one or more strings");
    if (!env_atom || env_atom->kind != ATOM_EXPR || env_atom->expr.len > PROC_ENV_MAX)
        return invalid(arena, "env must be a list of (\"NAME\" \"value\") pairs");
    if (!cwd || !cwd[0])
        return invalid(arena, "cwd must be a directory path");
    if (!integer(args[3], 1, PROC_TIMEOUT_MAX_MS, &timeout))
        return invalid(arena, "timeout-ms must be an integer from 1 to 3600000");
    if (!integer(args[4], 0, PROC_OUTPUT_MAX, &max_bytes))
        return invalid(arena, "max-bytes must be an integer from 0 to 67108864");

    size_t argc = (size_t)argv_atom->expr.len, envc = (size_t)env_atom->expr.len;
    char **argv = calloc(argc + 1, sizeof(*argv));
    char **envp = calloc(envc + 1, sizeof(*envp));
    char *program = NULL;
    Atom *result = NULL;
    Stream streams[2] = {{.fd = -1}, {.fd = -1}};
    int out[2] = {-1, -1}, err[2] = {-1, -1}, report[2] = {-1, -1}, pidfd = -1;
    if (!argv || !envp) { result = failed(arena, "out of memory"); goto done; }
    for (size_t i = 0; i < argc; ++i) {
        const char *arg = text(argv_atom->expr.elems[i]);
        if (!arg) { result = invalid(arena, "argv must be a list of one or more strings"); goto done; }
        argv[i] = (char *)arg;
    }
    if (!argv[0][0]) { result = invalid(arena, "the program name is empty"); goto done; }
    const char *path_env = NULL;
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
    if (pipe2(out, O_CLOEXEC) || pipe2(err, O_CLOEXEC) || pipe2(report, O_CLOEXEC)) {
        result = failed_errno(arena, "pipe", errno);
        goto done;
    }
    pid_t pid = fork();
    if (pid < 0) { result = failed_errno(arena, "fork", errno); goto done; }
    if (pid == 0) child_start(program, argv, envp, cwd, out[1], err[1], report[1]);
    setpgid(pid, pid);
    close(out[1]); out[1] = -1;
    close(err[1]); err[1] = -1;
    close(report[1]); report[1] = -1;
    uint64_t deadline = now_ms() + (uint64_t)timeout;

    /* The report pipe closes at exec; a report instead says where the child
     * stopped. */
    int stopped[2];
    ssize_t got = 0;
    for (;;) {
        uint64_t now = now_ms();
        struct pollfd p = {report[0], POLLIN, 0};
        int n = now < deadline ? poll(&p, 1, (int)(deadline - now)) : 0;
        if (n < 0 && errno == EINTR) continue;
        if (n > 0) got = read(report[0], stopped, sizeof(stopped));
        if (n > 0 && got < 0 && errno == EINTR) continue;
        break;
    }
    if (got == (ssize_t)sizeof(stopped)) {
        waitpid(pid, NULL, 0);
        const char *stage = stopped[0] == START_CWD ? "cwd"
            : stopped[0] == START_EXEC ? "exec" : "stdio";
        result = failed_errno(arena, stage, stopped[1]);
        goto done;
    }

    streams[0] = (Stream){.fd = out[0], .open = true, .limit = (size_t)max_bytes};
    streams[1] = (Stream){.fd = err[0], .open = true, .limit = (size_t)max_bytes};
    out[0] = err[0] = -1;
    for (int i = 0; i < 2; ++i)
        fcntl(streams[i].fd, F_SETFL, fcntl(streams[i].fd, F_GETFL) | O_NONBLOCK);
    pidfd = (int)syscall(SYS_pidfd_open, pid, 0);
    bool exited = false, timed_out = false;
    int status = 0;
    while (streams[0].open || streams[1].open || !exited) {
        uint64_t now = now_ms();
        if (now >= deadline) { timed_out = true; break; }
        struct pollfd fds[3];
        int n = 0;
        for (int i = 0; i < 2; ++i)
            if (streams[i].open) fds[n++] = (struct pollfd){streams[i].fd, POLLIN, 0};
        if (!exited && pidfd >= 0) fds[n++] = (struct pollfd){pidfd, POLLIN, 0};
        uint64_t wait = deadline - now;
        if (!exited && pidfd < 0 && wait > 20) wait = 20;
        if (poll(fds, (nfds_t)n, (int)wait) < 0 && errno != EINTR) {
            timed_out = true;
            break;
        }
        for (int i = 0; i < 2; ++i) stream_read(&streams[i]);
        if (!exited && waitpid(pid, &status, WNOHANG) == pid) exited = true;
    }
    if (timed_out) {
        /* The whole process group goes, whatever it started. */
        kill(-pid, SIGKILL);
        kill(pid, SIGKILL);
        if (!exited) waitpid(pid, &status, 0);
        for (int i = 0; i < 2; ++i) stream_read(&streams[i]);
    }
    Atom *out_text = as_text(arena, streams[0].data, streams[0].len);
    Atom *err_text = as_text(arena, streams[1].data, streams[1].len);
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
