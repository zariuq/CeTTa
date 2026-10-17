/* measure - run one command and report its exit code, wall time and peak
 * resident set, for tests/mm0/bench.py.
 *
 *   measure STDIN-FILE PROGRAM [ARG ...]
 *   prints: EXIT-CODE WALL-NANOSECONDS MAXRSS-KIB
 *
 * Linux charges a child the high-water RSS of the address space it execs
 * from; a process spawned from a large interpreter therefore reports at
 * least the interpreter's size.  This launcher is small, so the peak it
 * reports is the program's own. */
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

pid_t wait4(pid_t pid, int *status, int options, struct rusage *usage);

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: measure STDIN-FILE PROGRAM [ARG ...]\n");
        return 64;
    }
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 70; }
    if (pid == 0) {
        int in = open(argv[1], O_RDONLY), out = open("/dev/null", O_WRONLY);
        if (in < 0 || out < 0) _exit(127);
        dup2(in, 0);
        dup2(out, 1);
        dup2(out, 2);
        execv(argv[2], argv + 2);
        _exit(127);
    }
    int status;
    struct rusage usage;
    if (wait4(pid, &status, 0, &usage) < 0) { perror("wait4"); return 70; }
    clock_gettime(CLOCK_MONOTONIC, &end);
    long long ns = (long long)(end.tv_sec - start.tv_sec) * 1000000000LL + (end.tv_nsec - start.tv_nsec);
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    printf("%d %lld %ld\n", code, ns, usage.ru_maxrss);
    return 0;
}
