#define _GNU_SOURCE
#include "native/process_scope_v1.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;
static volatile sig_atomic_t stopped;
static void stop(int number) { (void)number; stopped = 1; }

static bool owner_gone(void) {
    struct pollfd owner = {3, POLLIN, 0};
    int result;
    do result = poll(&owner, 1, 0); while (result < 0 && errno == EINTR);
    return result < 0 || (owner.revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL));
}

/* A single-threaded subreaper never reaps during this enumeration. Every
 * listed PID is still its child (live or zombie), so it cannot be reused.
 * Killing each generation adopts escaped grandchildren for the next turn. */
static int kill_children(int descriptor) {
    if (lseek(descriptor, 0, SEEK_SET) < 0) return errno;
    char bytes[4096];
    unsigned long number = 0;
    bool digits = false;
    for (;;) {
        ssize_t count = read(descriptor, bytes, sizeof bytes);
        if (count < 0) { if (errno == EINTR) continue; return errno; }
        for (ssize_t i = 0; i <= count; ++i) {
            char c = i < count ? bytes[i] : ' ';
            if (c >= '0' && c <= '9') {
                if (number > ((unsigned long)INT_MAX - (unsigned long)(c - '0')) / 10)
                    return EOVERFLOW;
                number = number * 10 + (unsigned long)(c - '0'); digits = true;
            } else if (digits && (i < count || !count)) {
                if (number && kill((pid_t)number, SIGKILL) && errno != ESRCH)
                    return errno;
                number = 0; digits = false;
            }
        }
        if (!count) return 0;
    }
}

int main(int argc, char **argv) {
    CettaProcessScopeReceiptV1 receipt = {.magic = CETTA_PROCESS_SCOPE_MAGIC_V1};
    int children = -1, error_pipe[2] = {-1, -1};
    pid_t child = -1;
    bool cleaning = false;
    if (argc < 3 || fcntl(3, F_GETFD) < 0 || fcntl(4, F_GETFD) < 0) return 125;
    if (fcntl(3, F_SETFD, FD_CLOEXEC) < 0 || fcntl(4, F_SETFD, FD_CLOEXEC) < 0 ||
        prctl(PR_SET_CHILD_SUBREAPER, 1) < 0) { receipt.launch_error = errno; goto finish; }
    char path[96];
    snprintf(path, sizeof path, "/proc/self/task/%ld/children", (long)getpid());
    children = open(path, O_RDONLY | O_CLOEXEC);
    if (children < 0 || pipe2(error_pipe, O_CLOEXEC | O_NONBLOCK) < 0) {
        receipt.launch_error = errno; goto finish;
    }
    struct sigaction action = {.sa_handler = stop};
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGTERM, &action, NULL) < 0 || sigaction(SIGINT, &action, NULL) < 0 ||
        sigaction(SIGHUP, &action, NULL) < 0) { receipt.launch_error = errno; goto finish; }
    if (owner_gone()) goto finish;
    child = fork();
    if (child < 0) { receipt.launch_error = errno; goto finish; }
    if (!child) {
        /* Only async-signal-safe operations run between fork and exec. */
        close(error_pipe[0]); close(children); close(3); close(4);
        struct sigaction defaults = {.sa_handler = SIG_DFL};
        sigemptyset(&defaults.sa_mask);
        sigaction(SIGTERM, &defaults, NULL); sigaction(SIGINT, &defaults, NULL);
        sigaction(SIGHUP, &defaults, NULL);
        execve(argv[1], argv + 2, environ);
        int saved = errno;
        ssize_t reported;
        do reported = write(error_pipe[1], &saved, sizeof saved); while (reported < 0 && errno == EINTR);
        if (reported != sizeof saved) _exit(126);
        _exit(127);
    }
    close(error_pipe[1]); error_pipe[1] = -1;
    /* The supervisor must not keep the command's input/output alive. */
    close(STDIN_FILENO); close(STDOUT_FILENO); close(STDERR_FILENO);
    for (;;) {
        int status;
        pid_t waited;
        while ((waited = waitpid(-1, &status, WNOHANG)) > 0) {
            if (waited == child) { receipt.wait_status = status; cleaning = true; }
        }
        if (waited < 0 && errno == ECHILD) break;
        if (waited < 0 && errno != EINTR) { receipt.host_error = errno; cleaning = true; }
        cleaning = cleaning || stopped || owner_gone();
        if (cleaning) {
            int error = kill_children(children);
            if (error && !receipt.host_error) receipt.host_error = error;
        }
        struct pollfd owner = {cleaning ? -1 : 3, POLLIN, 0};
        if (poll(&owner, 1, 10) < 0 && errno != EINTR) {
            receipt.host_error = errno; cleaning = true;
        }
    }
    if (error_pipe[0] >= 0) {
        int error;
        ssize_t bytes;
        do bytes = read(error_pipe[0], &error, sizeof error); while (bytes < 0 && errno == EINTR);
        if (bytes == sizeof error) receipt.launch_error = error;
        else if (bytes > 0) receipt.host_error = EPROTO;
    }
finish:
    if (children >= 0) close(children);
    for (int i = 0; i < 2; ++i) if (error_pipe[i] >= 0) close(error_pipe[i]);
    ssize_t sent;
    do sent = write(4, &receipt, sizeof receipt); while (sent < 0 && errno == EINTR);
    return sent == sizeof receipt ? 0 : 125;
}
