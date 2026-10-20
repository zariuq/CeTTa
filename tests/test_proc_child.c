#define _GNU_SOURCE
#include "proc_child.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>

static unsigned checks, failures;
#define CHECK(test, label) do { checks++; if (!(test)) { failures++; \
    fprintf(stderr, "FAIL: %s\n", label); } } while (0)

#ifdef CETTA_PROC_TEST_DENIAL
static int deny_descriptor_table;
extern int __real_open(const char *, int, ...);
int __wrap_open(const char *path, int flags, ...) {
    if (deny_descriptor_table && !strcmp(path, "/proc/self/fd")) {
        errno = EACCES; return -1;
    }
    return __real_open(path, flags, 0600);
}
#endif

static int child_probe(const char *text, const char *group) {
    int inherited = atoi(text);
    if (fcntl(inherited, F_GETFD) >= 0 || errno != EBADF) return 11;
    /* "new": a group of its own; otherwise the caller's group, by number. */
    if (strcmp(group, "new") ? getpgrp() != (pid_t)atol(group)
                             : getpgrp() != getpid()) return 12;
    sigset_t mask;
    if (sigprocmask(SIG_SETMASK, NULL, &mask) || sigismember(&mask, SIGUSR2)) return 13;
    struct sigaction action;
    if (sigaction(SIGUSR1, NULL, &action) || action.sa_handler != SIG_DFL) return 14;
    char input[5] = {0}, cwd[8];
    if (read(0, input, 4) != 4 || strcmp(input, "data")) return 15;
    if (!getcwd(cwd, sizeof cwd) || strcmp(cwd, "/")) return 16;
    const char *value = getenv("PROC_CHILD_FIXTURE");
    if (!value || strcmp(value, "isolated") || getenv("PROC_CHILD_PARENT")) return 17;
    if (write(1, "out", 3) != 3 || write(2, "err", 3) != 3) return 18;
    return 0;
}

static void launch(const char *self, int inherited, const char *program,
                   const char *cwd, int expected_stage, int expected_error, int aliases,
                   int new_group) {
    int in[2], out[2], err[2], report[2];
    if (pipe2(in, O_CLOEXEC) || pipe2(out, O_CLOEXEC) || pipe2(err, O_CLOEXEC) ||
        pipe2(report, O_CLOEXEC | O_NONBLOCK)) { perror("pipe2"); exit(2); }
    char number[32], group[32];
    snprintf(number, sizeof number, "%d", inherited);
    if (new_group) snprintf(group, sizeof group, "new");
    else snprintf(group, sizeof group, "%ld", (long)getpgrp());
    char *arguments[] = {(char *)self, "--child", number, group, NULL};
    char *environment[] = {"PROC_CHILD_FIXTURE=isolated", NULL};
    sigset_t all, previous;
    sigfillset(&all);
    if (sigprocmask(SIG_SETMASK, &all, &previous)) exit(2);
    pid_t pid = fork();
    if (!pid) {
        /* An open descriptor can outlive a lowered soft and hard limit. */
        struct rlimit lowered = {64, 64};
        if (setrlimit(RLIMIT_NOFILE, &lowered)) _exit(19);
        if (aliases) {
            if (dup2(report[1], 0) < 0 || dup2(out[1], 1) < 0 || dup2(err[1], 2) < 0) _exit(20);
            cetta_proc_child_exec_group(program, arguments, environment, cwd,
                in[0], 1, 2, 0, new_group);
        }
        cetta_proc_child_exec_group(program, arguments, environment, cwd,
            in[0], out[1], err[1], report[1], new_group);
    }
    if (sigprocmask(SIG_SETMASK, &previous, NULL) || pid < 0) exit(2);
    close(in[0]); close(out[1]); close(err[1]); close(report[1]);
    if (!expected_stage) CHECK(write(in[1], "data", 4) == 4, "stdin delivered");
    close(in[1]);
    int status = 0;
    CHECK(waitpid(pid, &status, 0) == pid, "child reaped");
    int stopped[2] = {0};
    ssize_t count = read(report[0], stopped, sizeof stopped);
    if (expected_stage) {
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 127, "setup failure exits distinctly");
        CHECK(count == sizeof stopped && stopped[0] == expected_stage && stopped[1] == expected_error,
              "setup reports actual stage and errno");
    } else {
        CHECK(WIFEXITED(status) && !WEXITSTATUS(status), "child isolation, signals, cwd and environment");
        CHECK(count == 0, "successful exec closes report");
        char bytes[4] = {0};
        CHECK(read(out[0], bytes, 4) == 3 && !strcmp(bytes, "out"), "stdout preserved");
        CHECK(read(err[0], bytes, 4) == 3 && !memcmp(bytes, "err", 3), "stderr preserved");
    }
    CHECK(fcntl(inherited, F_GETFD) >= 0, "parent descriptor remains open");
    close(out[0]); close(err[0]); close(report[0]);
}

int main(int argc, char **argv) {
    if (argc == 4 && !strcmp(argv[1], "--child")) return child_probe(argv[2], argv[3]);
    char self[4096];
    ssize_t length = readlink("/proc/self/exe", self, sizeof self - 1);
    if (length <= 0) return 2;
    self[length] = 0;
    if (setenv("PROC_CHILD_PARENT", "not inherited", 1)) return 2;
    struct sigaction ignored = {.sa_handler = SIG_IGN};
    sigemptyset(&ignored.sa_mask);
    if (sigaction(SIGUSR1, &ignored, NULL)) return 2;
    int file = open("/dev/null", O_RDONLY);
    if (file < 0) return 2;
    launch(self, file, self, "/", 0, 0, 0, 1);
    launch(self, file, self, "/", 0, 0, 1, 1);
    /* A helper launch stays in the caller's process group. */
    launch(self, file, self, "/", 0, 0, 0, 0);
    launch(self, file, self, "/no-such-cetta-proc-directory", CETTA_PROC_CHILD_CWD, ENOENT, 0, 1);
    launch(self, file, "/no-such-cetta-proc-program", "/", CETTA_PROC_CHILD_EXEC, ENOENT, 0, 1);
    /* Above the old 65536-entry sweep; retain it across a lowered limit. */
    int high = fcntl(file, F_DUPFD, 65536);
    if (high >= 0) { launch(self, high, self, "/", 0, 0, 0, 1); close(high); }
    else fprintf(stderr, "SKIP: host cannot allocate a descriptor above 65535\n");
#ifdef CETTA_PROC_TEST_DENIAL
    deny_descriptor_table = 1;
    launch(self, file, self, "/", CETTA_PROC_CHILD_FDS, EACCES, 0, 1);
#endif
    close(file);
    printf("proc child: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
