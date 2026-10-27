#ifndef CETTA_NATIVE_DISK_INDEX_H
#define CETTA_NATIVE_DISK_INDEX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Immutable, disk-backed byte keys and out-of-line byte values. Application
 * schemas, multiplicity, order keys, secondary indexes and receipts belong
 * to MeTTa. This kernel supplies no SQL or agent-specific policy.
 *
 * One external owner holds the writer lock. A build only appends candidates;
 * it does not publish a transaction. Synchronize the segment, then publish its
 * root/end with the existing native journal/manifest owner. A failed build may
 * leave orphan bytes, but cannot change an already captured root. After an
 * uncertain publication, recover/read that owner's journal before proceeding.
 */
typedef enum {
    CETTA_DISK_OK, CETTA_DISK_END, CETTA_DISK_ABSENT, CETTA_DISK_INVALID,
    CETTA_DISK_IO, CETTA_DISK_CORRUPT, CETTA_DISK_NOMEM, CETTA_DISK_LIMIT
} CettaDiskStatus;

typedef struct { const unsigned char *data; size_t size; } CettaDiskBytes;
typedef struct { unsigned char *data; size_t size; } CettaDiskBuffer;

/* Zero root means the empty map. Other references are frame offset + 1.
 * fd is borrowed. Keep it open until the snapshot's reads have ended.
 * Limits bound individual items and traversal depth, never the archive size.
 */
typedef struct {
    int fd;
    uint64_t root, end;
    size_t key_limit, value_limit;
} CettaDiskRoot;

typedef struct { bool remove; CettaDiskBytes key, value; } CettaDiskOp;
typedef struct { uint64_t reference, size; } CettaDiskBlob;
typedef struct CettaDiskCursor CettaDiskCursor;

/* All operations are interpreted in order in one candidate root. No operation
 * count or corpus-sized in-memory index is retained between calls. */
CettaDiskStatus cetta_disk_build(const CettaDiskRoot *base,
    const CettaDiskOp *operations, size_t count, CettaDiskRoot *candidate);
CettaDiskStatus cetta_disk_sync(const CettaDiskRoot *candidate);
CettaDiskStatus cetta_disk_lookup(const CettaDiskRoot *root,
    CettaDiskBytes key, CettaDiskBlob *value);

/* Verify the whole value through bounded scratch space, then report the
 * requested range. On failure written is zero; destination is unspecified.
 * A caller needing the entire body allocates that body, not the whole store.
 */
CettaDiskStatus cetta_disk_blob_read(const CettaDiskRoot *root, CettaDiskBlob value,
    uint64_t offset, void *destination, size_t capacity, size_t *written);

/* Captures root/end and owns a duplicate fd. Appends, deletion, compaction and
 * unlinking the old segment do not change this view. Increasing byte-key order,
 * one key at a time; key ownership transfers to the caller. Close on every exit.
 */
CettaDiskStatus cetta_disk_cursor_open(const CettaDiskRoot *root, CettaDiskCursor **out);
/* Optional lower bound (inclusive unless exclusive), optional exclusive upper
 * bound. Seeking visits a key path rather than scanning all earlier keys. */
CettaDiskStatus cetta_disk_cursor_open_range(const CettaDiskRoot *root,
    const CettaDiskBytes *lower, bool exclusive, const CettaDiskBytes *upper,
    CettaDiskCursor **out);
CettaDiskStatus cetta_disk_cursor_next(CettaDiskCursor *cursor,
    CettaDiskBuffer *key, CettaDiskBlob *value);
const CettaDiskRoot *cetta_disk_cursor_root(const CettaDiskCursor *cursor);
void cetta_disk_cursor_close(CettaDiskCursor *cursor);
void cetta_disk_buffer_free(CettaDiskBuffer *buffer);

/* Copy only the live tree and values into an empty, distinct segment. Memory
 * is bounded by key depth plus one key and a fixed copy buffer. Synchronize
 * and publish the new segment/root through the owner before retiring the old
 * name. Open reader fds keep old segments alive until readers close them.
 */
CettaDiskStatus cetta_disk_compact(const CettaDiskRoot *source, int destination,
    CettaDiskRoot *candidate);

#endif
