#ifndef CETTA_NATIVE_JOURNAL_H
#define CETTA_NATIVE_JOURNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef enum {
    CETTA_JOURNAL_OK, CETTA_JOURNAL_END, CETTA_JOURNAL_TAIL,
    CETTA_JOURNAL_IO, CETTA_JOURNAL_CORRUPT, CETTA_JOURNAL_LIMIT,
    CETTA_JOURNAL_NOMEM
} CettaNativeJournalStatus;

/* CSL1 is a length-delimited, SHA-256 checked byte frame. Interpretation of
 * its payload and atomic application belong to the owning store. A complete
 * invalid frame is corruption; an incomplete final frame is a recoverable
 * tail. A zero read allowance selects the representation's full range. */
CettaNativeJournalStatus cetta_native_journal_read(int fd, off_t position,
    size_t max_bytes, unsigned char **bytes, size_t *size, off_t *next);
CettaNativeJournalStatus cetta_native_journal_append(int fd,
    const unsigned char *bytes, size_t size, bool synchronize, uint64_t *written);

#endif
