#ifndef CETTA_NATIVE_JOURNAL_H
#define CETTA_NATIVE_JOURNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Shared framing and publication for native stores. Records are opaque bytes;
 * schemas, atom semantics and mutation policies belong to their callers.
 * CSL1 framing remains compatible with existing named-space journals. */
typedef enum {
    CETTA_JOURNAL_OK,
    CETTA_JOURNAL_END,
    CETTA_JOURNAL_TORN,
    CETTA_JOURNAL_INVALID,
    CETTA_JOURNAL_CORRUPT,
    CETTA_JOURNAL_NOMEM,
    CETTA_JOURNAL_IO,
    CETTA_JOURNAL_UNKNOWN
} CettaJournalStatus;

typedef struct {
    unsigned char *data;
    size_t size;
    uint64_t next;
} CettaJournalRecord;

/* The caller owns the file position and holds the store's writer lock.
 * Failure after entering the write boundary is UNKNOWN, never a rollback.
 * The byte counter advances only after the requested synchronization succeeds. */
CettaJournalStatus cetta_journal_append(int fd, const unsigned char *data,
    size_t size, bool synchronize, uint64_t *written);

typedef struct { const unsigned char *data; size_t size; } CettaJournalPart;
/* One frame from borrowed parts, without concatenating their payloads. */
CettaJournalStatus cetta_journal_append_parts(int fd, const CettaJournalPart *parts,
    size_t count, bool synchronize, uint64_t *written);

/* Read one frame within a captured file length. END is an exact boundary;
 * TORN is an incomplete final frame; CORRUPT is a complete invalid frame.
 * A successful record owns its data until cetta_journal_record_free. */
CettaJournalStatus cetta_journal_read(int fd, uint64_t offset,
    uint64_t file_size, size_t max_record_bytes, CettaJournalRecord *out);
void cetta_journal_record_free(CettaJournalRecord *record);

/* Copy one complete frame using constant scratch space, checking its digest.
 * This builds an unpublished destination: failure can leave partial or corrupt
 * bytes there, which must not be published. The source's captured length is
 * never extended. Destination position/counter ownership is as for append. */
CettaJournalStatus cetta_journal_copy(int source, uint64_t offset,
    uint64_t file_size, size_t max_record_bytes, int destination,
    uint64_t *written);

/* Only recovery of an otherwise valid journal may discard an incomplete tail. */
CettaJournalStatus cetta_journal_trim_tail(int fd, uint64_t valid_bytes);

typedef bool (*CettaJournalCheckpointWriter)(int fd, uint64_t *bytes, void *context);

/* Write a complete replacement, synchronize it, atomically rename it, then
 * synchronize the directory. The live descriptor changes only on success.
 * UNKNOWN requires reopening the store: the replacement may be durable.
 * Both names are single path components in the already-open private directory. */
CettaJournalStatus cetta_journal_checkpoint(int directory, const char *name,
    const char *temporary, CettaJournalCheckpointWriter writer, void *context,
    int *live_fd, uint64_t *written);

#endif
