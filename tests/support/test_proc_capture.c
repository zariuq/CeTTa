#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static bool interrupt_read;
static ssize_t capture_test_read(int fd, void *data, size_t length) {
    if (interrupt_read) {
        interrupt_read = false;
        errno = EINTR;
        return -1;
    }
    return read(fd, data, length);
}
#define read capture_test_read
#include "proc_capture.h"
#undef read

static void capture_case(size_t length, size_t capacity, bool interrupt) {
    int channel[2];
    assert(pipe(channel) == 0);
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        close(channel[0]);
        signal(SIGPIPE, SIG_DFL);
        char chunk[4096];
        size_t sent = 0u;
        while (sent < length) {
            size_t count = length - sent;
            if (count > sizeof(chunk)) count = sizeof(chunk);
            for (size_t i = 0; i < count; ++i)
                chunk[i] = (char)('a' + (sent + i) % 26u);
            ssize_t got = write(channel[1], chunk, count);
            if (got < 0 && errno == EINTR) continue;
            if (got <= 0) _exit(91);
            sent += (size_t)got;
        }
        close(channel[1]);
        _exit(0);
    }
    close(channel[1]);
    char prefix[4097];
    memset(prefix, 0x5a, sizeof(prefix));
    interrupt_read = interrupt;
    assert(cetta_proc_capture_prefix_and_drain(channel[0], prefix, capacity));
    assert(!interrupt_read);
    close(channel[0]);
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    size_t kept = capacity == 0u ? 0u : capacity - 1u;
    if (kept > length) kept = length;
    for (size_t i = 0; i < kept; ++i)
        assert(prefix[i] == (char)('a' + i % 26u));
    if (capacity != 0u) assert(prefix[kept] == '\0');
    assert(prefix[capacity] == 0x5a);
}

int main(void) {
    const size_t lengths[] = {0u, 17u, 1048576u};
    const size_t capacities[] = {0u, 1u, 2u, 256u, 4096u};
    unsigned controls = 0u;
    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i)
        for (size_t j = 0; j < sizeof(capacities) / sizeof(capacities[0]); ++j)
            for (unsigned interrupted = 0u; interrupted < 2u; ++interrupted) {
                capture_case(lengths[i], capacities[j], interrupted != 0u);
                ++controls;
            }
    char failed[2] = {'x', 'y'};
    assert(!cetta_proc_capture_prefix_and_drain(-1, failed, sizeof(failed)));
    assert(failed[0] == '\0' && failed[1] == 'y');
    assert(!cetta_proc_capture_prefix_and_drain(-1, NULL, 0u));
    printf("PASS: %u process capture controls, bounded prefixes, complete draining and read failures\n", controls + 2u);
    return 0;
}
