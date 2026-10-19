#ifndef CETTA_PROC_CHILD_H
#define CETTA_PROC_CHILD_H

/* Shared fork/exec child setup. Call with signals blocked. No allocation or
 * inherited host callbacks here; descriptors may alias standard streams. */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <unistd.h>

enum {
    CETTA_PROC_CHILD_STDIO = 1, CETTA_PROC_CHILD_CWD = 2,
    CETTA_PROC_CHILD_EXEC = 3, CETTA_PROC_CHILD_FDS = 4,
    CETTA_PROC_CHILD_SIGNALS = 5, CETTA_PROC_CHILD_GROUP = 6
};

static inline int cetta_proc_child_cloexec(void) {
#if defined(SYS_close_range) && !defined(CETTA_PROC_NO_CLOSE_RANGE)
    if (!syscall(SYS_close_range, 3u, ~0u, 4u /* CLOSE_RANGE_CLOEXEC */)) return 0;
#endif
    /* Older kernels or syscall filters: enumerate the child's own, stable
     * descriptor table, not the concurrently changing parent's. getdents64
     * avoids libc directory allocation after fork. Refuse exec on failure;
     * a guessed upper bound can leak descriptors above a lowered rlimit. */
    int directory = open("/proc/self/fd", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory < 0) return -1;
    struct Entry {
        uint64_t ino;
        int64_t offset;
        unsigned short length;
        unsigned char type;
        char name[];
    };
    union { uint64_t align; unsigned char bytes[4096]; } buffer;
    int error = 0;
    for (;;) {
        long count = syscall(SYS_getdents64, directory, buffer.bytes, sizeof buffer.bytes);
        if (count < 0) { if (errno == EINTR) continue; error = errno; break; }
        if (!count) break;
        for (size_t at = 0; at < (size_t)count;) {
            struct Entry *entry = (struct Entry *)(void *)(buffer.bytes + at);
            size_t prefix = offsetof(struct Entry, name);
            if ((size_t)count - at <= prefix || entry->length <= prefix ||
                entry->length > (size_t)count - at) { error = EIO; break; }
            size_t i = 0, room = entry->length - prefix;
            unsigned fd = 0;
            while (i < room && entry->name[i] >= '0' && entry->name[i] <= '9') {
                unsigned digit = (unsigned)(entry->name[i++] - '0');
                if (fd > ((unsigned)INT_MAX - digit) / 10u) { error = EIO; break; }
                fd = fd * 10u + digit;
            }
            if (error) break;
            if (i && i < room && !entry->name[i] && fd >= 3u &&
                fcntl((int)fd, F_SETFD, FD_CLOEXEC) < 0) { error = errno; break; }
            at += entry->length;
        }
        if (error) break;
    }
    close(directory);
    if (error) { errno = error; return -1; }
    return 0;
}

static inline _Noreturn void cetta_proc_child_stop(int fd, int stage, int error) {
    int report[2] = {stage, error};
    ssize_t count;
    do count = write(fd, report, sizeof report); while (count < 0 && errno == EINTR);
    _exit(127);
}

static inline _Noreturn void cetta_proc_child_exec(const char *program,
        char *const argv[], char *const env[], const char *cwd,
        int in, int out, int err, int report) {
    if (report < 3) {
        int moved = fcntl(report, F_DUPFD_CLOEXEC, 3);
        if (moved < 0) cetta_proc_child_stop(report, CETTA_PROC_CHILD_STDIO, errno);
        report = moved;
    }
    if (setpgid(0, 0)) cetta_proc_child_stop(report, CETTA_PROC_CHILD_GROUP, errno);
    struct sigaction standard = {.sa_handler = SIG_DFL};
    sigemptyset(&standard.sa_mask);
    for (int sig = 1; sig < NSIG; ++sig) {
        if (sig == SIGKILL || sig == SIGSTOP) continue;
        if (sigaction(sig, &standard, NULL) && errno != EINVAL)
            cetta_proc_child_stop(report, CETTA_PROC_CHILD_SIGNALS, errno);
    }
    /* Preserve every source before dup2 can replace a reused standard FD. */
    if (out < 3) out = fcntl(out, F_DUPFD_CLOEXEC, 3);
    if (err < 3) err = fcntl(err, F_DUPFD_CLOEXEC, 3);
    if (in < 0) in = open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (in >= 0 && in < 3) in = fcntl(in, F_DUPFD_CLOEXEC, 3);
    if (in < 0 || out < 0 || err < 0 || dup2(in, 0) < 0 || dup2(out, 1) < 0 || dup2(err, 2) < 0 ||
        fcntl(0, F_SETFD, 0) < 0 || fcntl(1, F_SETFD, 0) < 0 || fcntl(2, F_SETFD, 0) < 0)
        cetta_proc_child_stop(report, CETTA_PROC_CHILD_STDIO, errno);
    if (cetta_proc_child_cloexec()) cetta_proc_child_stop(report, CETTA_PROC_CHILD_FDS, errno);
    if (chdir(cwd)) cetta_proc_child_stop(report, CETTA_PROC_CHILD_CWD, errno);
    sigset_t empty;
    sigemptyset(&empty);
    if (sigprocmask(SIG_SETMASK, &empty, NULL))
        cetta_proc_child_stop(report, CETTA_PROC_CHILD_SIGNALS, errno);
    execve(program, argv, env);
    cetta_proc_child_stop(report, CETTA_PROC_CHILD_EXEC, errno);
}

#endif
