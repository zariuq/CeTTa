#define _POSIX_C_SOURCE 200809L
#include "native_journal.h"
#include "native_sha256.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { HEADER_BYTES = 72 };

static ssize_t read_at(int fd, unsigned char *bytes, size_t size, off_t position) {
    size_t used = 0;
    while (used < size) {
        ssize_t n = pread(fd, bytes + used, size - used, position + (off_t)used);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) return -1;
        if (!n) break;
        used += (size_t)n;
    }
    return (ssize_t)used;
}

static bool write_all(int fd, const unsigned char *bytes, size_t size) {
    while (size) {
        ssize_t n = write(fd, bytes, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        bytes += n; size -= (size_t)n;
    }
    return true;
}

CettaNativeJournalStatus cetta_native_journal_append(int fd,
        const unsigned char *bytes, size_t size, bool synchronize, uint64_t *written) {
    if ((!bytes && size) || size > UINT32_MAX) return CETTA_JOURNAL_LIMIT;
    if (written && *written > UINT64_MAX - HEADER_BYTES - size) return CETTA_JOURNAL_LIMIT;
    unsigned char header[HEADER_BYTES] = {'C', 'S', 'L', '1'};
    for (size_t i = 0; i < 4; ++i) header[4+i] = (unsigned char)(size >> (24-8*i));
    char digest[65]; cetta_native_sha256_hex(bytes, size, digest);
    memcpy(header + 8, digest, 64);
    if (!write_all(fd, header, sizeof(header)) || !write_all(fd, bytes, size) ||
        (synchronize && fsync(fd))) return CETTA_JOURNAL_IO;
    if (written) *written += HEADER_BYTES + size;
    return CETTA_JOURNAL_OK;
}

CettaNativeJournalStatus cetta_native_journal_read(int fd, off_t position,
        size_t max_bytes, unsigned char **bytes, size_t *size, off_t *next) {
    if (!bytes || !size || !next || position < 0) return CETTA_JOURNAL_CORRUPT;
    *bytes = NULL; *size = 0; *next = position;
    unsigned char header[HEADER_BYTES];
    ssize_t n = read_at(fd, header, sizeof(header), position);
    if (n < 0) return CETTA_JOURNAL_IO;
    if (!n) return CETTA_JOURNAL_END;
    if ((size_t)n < sizeof(header)) return CETTA_JOURNAL_TAIL;
    if (memcmp(header, "CSL1", 4)) return CETTA_JOURNAL_CORRUPT;
    uint32_t length = 0;
    for (size_t i = 4; i < 8; ++i) length = (length << 8) | header[i];
    if ((max_bytes && length > max_bytes) || (uint64_t)position >
        (uint64_t)INT64_MAX - HEADER_BYTES - length) return CETTA_JOURNAL_LIMIT;
    struct stat st;
    if (fstat(fd, &st) || st.st_size < position) return CETTA_JOURNAL_IO;
    if ((uint64_t)(st.st_size - position) < (uint64_t)HEADER_BYTES + length)
        return CETTA_JOURNAL_TAIL;
    unsigned char *payload = malloc(length ? length : 1u);
    if (!payload) return CETTA_JOURNAL_NOMEM;
    n = read_at(fd, payload, length, position + HEADER_BYTES);
    if (n < 0) { free(payload); return CETTA_JOURNAL_IO; }
    if ((uint64_t)n != length) { free(payload); return CETTA_JOURNAL_TAIL; }
    char digest[65]; cetta_native_sha256_hex(payload, length, digest);
    if (memcmp(header + 8, digest, 64)) { free(payload); return CETTA_JOURNAL_CORRUPT; }
    *bytes = payload; *size = length; *next = position + HEADER_BYTES + length;
    return CETTA_JOURNAL_OK;
}
