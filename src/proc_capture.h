#ifndef CETTA_PROC_CAPTURE_H
#define CETTA_PROC_CAPTURE_H

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>

/* Retain a diagnostic prefix while consuming the complete stream. Closing
 * a full prefix buffer's pipe can interrupt a still-writing child. */
static inline bool cetta_proc_capture_prefix_and_drain(
        int output, char *buffer, size_t capacity) {
    char chunk[4096];
    size_t used = 0u;
    bool ok;
    for (;;) {
        ssize_t got = read(output, chunk, sizeof(chunk));
        if (got < 0 && errno == EINTR)
            continue;
        if (got <= 0) {
            ok = got == 0;
            break;
        }
        size_t remaining = capacity > used ? capacity - used - 1u : 0u;
        size_t keep = (size_t)got < remaining ? (size_t)got : remaining;
        if (keep != 0u) {
            memcpy(buffer + used, chunk, keep);
            used += keep;
        }
    }
    if (capacity != 0u)
        buffer[used] = '\0';
    return ok;
}

#endif
