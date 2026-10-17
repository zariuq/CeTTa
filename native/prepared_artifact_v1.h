#ifndef CETTA_PREPARED_ARTIFACT_V1_H
#define CETTA_PREPARED_ARTIFACT_V1_H
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Same-directory atomic publication shared by parser and equation artifacts.
 * A failed write never replaces the previous artifact. */
static inline bool cetta_prepared_artifact_v1_publish(const char *path,
    const uint8_t *bytes, size_t length, char *error, size_t error_size) {
    if (!path || (!bytes && length) || strlen(path) > SIZE_MAX - 24u) return false;
    size_t capacity = strlen(path) + 24u;
    char *temporary = malloc(capacity);
    if (!temporary) return false;
    snprintf(temporary, capacity, "%s.partial.XXXXXX", path);
    int fd = mkstemp(temporary), saved = fd < 0 ? errno : 0;
    size_t written = 0u;
    while (!saved && written < length) {
        size_t remaining = length - written;
        if (remaining > (size_t)SSIZE_MAX) remaining = SSIZE_MAX;
        ssize_t count = write(fd, bytes + written, remaining);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { saved = count < 0 ? errno : EIO; break; }
        written += (size_t)count;
    }
    if (!saved && fsync(fd)) saved = errno;
    if (fd >= 0 && close(fd) && !saved) saved = errno;
    if (!saved && rename(temporary, path)) saved = errno;
    if (saved) {
        if (error && error_size) snprintf(error, error_size, "artifact publication failed: %s", strerror(saved));
        if (fd >= 0) (void)unlink(temporary);
    }
    free(temporary); return saved == 0;
}
#endif
