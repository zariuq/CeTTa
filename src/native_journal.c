#define _POSIX_C_SOURCE 200809L
#include "native_journal.h"
#include "native_sha256.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

enum { FRAME_HEADER = 72 };

static bool synchronize_file(int fd) {
    int result;
    do { result = fsync(fd); } while (result < 0 && errno == EINTR);
    return result == 0;
}

static bool write_all(int fd, const unsigned char *data, size_t size) {
    while (size) {
        size_t amount = size > (size_t)SSIZE_MAX ? (size_t)SSIZE_MAX : size;
        ssize_t n = write(fd, data, amount);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        data += n;
        size -= (size_t)n;
    }
    return true;
}

static CettaJournalStatus read_all(int fd, unsigned char *data, size_t size,
                                  uint64_t offset) {
    while (size) {
        size_t amount = size > (size_t)SSIZE_MAX ? (size_t)SSIZE_MAX : size;
        ssize_t n = pread(fd, data, amount, (off_t)offset);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) return CETTA_JOURNAL_IO;
        if (!n) return CETTA_JOURNAL_TORN;
        data += n;
        size -= (size_t)n;
        offset += (uint64_t)n;
    }
    return CETTA_JOURNAL_OK;
}

CettaJournalStatus cetta_journal_append(int fd, const unsigned char *data,
        size_t size, bool synchronize, uint64_t *written) {
    if (fd < 0 || (size && !data) || size > UINT32_MAX ||
        (written && *written > UINT64_MAX - FRAME_HEADER - size))
        return CETTA_JOURNAL_INVALID;
    unsigned char header[FRAME_HEADER] = {'C', 'S', 'L', '1'};
    for (size_t i = 0; i < 4; ++i)
        header[4 + i] = (unsigned char)(size >> (24 - 8 * i));
    char digest[65];
    cetta_native_sha256_hex(data, size, digest);
    memcpy(header + 8, digest, 64);
    if (!write_all(fd, header, sizeof(header)) || !write_all(fd, data, size) ||
        (synchronize && !synchronize_file(fd))) return CETTA_JOURNAL_UNKNOWN;
    if (written) *written += FRAME_HEADER + size;
    return CETTA_JOURNAL_OK;
}

void cetta_journal_record_free(CettaJournalRecord *record) {
    if (!record) return;
    free(record->data);
    *record = (CettaJournalRecord){0};
}

CettaJournalStatus cetta_journal_read(int fd, uint64_t offset,
        uint64_t file_size, size_t max_record_bytes, CettaJournalRecord *out) {
    if (!out) return CETTA_JOURNAL_INVALID;
    *out = (CettaJournalRecord){0};
    if (fd < 0 || offset > file_size || file_size > INT64_MAX)
        return CETTA_JOURNAL_INVALID;
    if (offset == file_size) return CETTA_JOURNAL_END;
    if (file_size - offset < FRAME_HEADER) return CETTA_JOURNAL_TORN;
    unsigned char header[FRAME_HEADER];
    CettaJournalStatus status = read_all(fd, header, sizeof(header), offset);
    if (status != CETTA_JOURNAL_OK) return status;
    if (memcmp(header, "CSL1", 4)) return CETTA_JOURNAL_CORRUPT;
    size_t size = 0;
    for (size_t i = 4; i < 8; ++i) size = (size << 8) | header[i];
    if (size > max_record_bytes) return CETTA_JOURNAL_CORRUPT;
    if ((uint64_t)size > file_size - offset - FRAME_HEADER)
        return CETTA_JOURNAL_TORN;
    unsigned char *data = malloc(size ? size : 1);
    if (!data) return CETTA_JOURNAL_NOMEM;
    status = read_all(fd, data, size, offset + FRAME_HEADER);
    if (status != CETTA_JOURNAL_OK) { free(data); return status; }
    char digest[65];
    cetta_native_sha256_hex(data, size, digest);
    if (memcmp(digest, header + 8, 64)) {
        free(data);
        return CETTA_JOURNAL_CORRUPT;
    }
    *out = (CettaJournalRecord){data, size, offset + FRAME_HEADER + size};
    return CETTA_JOURNAL_OK;
}

CettaJournalStatus cetta_journal_trim_tail(int fd, uint64_t valid_bytes) {
    if (fd < 0 || valid_bytes > INT64_MAX) return CETTA_JOURNAL_INVALID;
    int result;
    do { result = ftruncate(fd, (off_t)valid_bytes); } while (result < 0 && errno == EINTR);
    if (result < 0 || !synchronize_file(fd)) return CETTA_JOURNAL_UNKNOWN;
    return CETTA_JOURNAL_OK;
}

static bool component(const char *name) {
    return name && *name && strcmp(name, ".") && strcmp(name, "..") &&
           !strchr(name, '/');
}

CettaJournalStatus cetta_journal_checkpoint(int directory, const char *name,
        const char *temporary, CettaJournalCheckpointWriter writer, void *context,
        int *live_fd, uint64_t *written) {
    if (directory < 0 || !live_fd || *live_fd < 0 || !writer ||
        !component(name) || !component(temporary) || !strcmp(name, temporary))
        return CETTA_JOURNAL_INVALID;
    int fd;
    do { fd = openat(directory, temporary,
                    O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600); }
    while (fd < 0 && errno == EINTR);
    if (fd < 0) return CETTA_JOURNAL_IO;
    uint64_t size = 0;
    CettaJournalStatus status = CETTA_JOURNAL_IO;
    if (writer(fd, &size, context) && synchronize_file(fd)) {
        /* Once rename is attempted, a failure needs recovery/readback. */
        status = CETTA_JOURNAL_UNKNOWN;
        if (!renameat(directory, temporary, directory, name) && synchronize_file(directory)) {
            close(*live_fd);
            *live_fd = fd;
            if (written) *written = size;
            return CETTA_JOURNAL_OK;
        }
    }
    close(fd);
    unlinkat(directory, temporary, 0);
    return status;
}
