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
enum { CETTA_DISK_ABI_VERSION=2, CETTA_DISK_SEGMENT_HEADER_BYTES=92 };
uint32_t cetta_disk_abi_version(void);

/* Zero root means the empty map. Other references are frame offset + 1.
 * fd is borrowed. Keep it open until the snapshot's reads have ended.
 * Limits bound individual items and traversal depth, never the archive size.
 */
typedef struct {
    int fd;
    uint64_t root, end;
    size_t key_limit, value_limit;
    unsigned char segment[16];
} CettaDiskRoot;

/* Initialize an empty descriptor with one checksummed generation header.
 * The caller supplies a nonzero generation; no synchronization is implied.
 * Root metadata carries this identity through publication and reopen. */
CettaDiskStatus cetta_disk_segment_create(int fd,const unsigned char identity[16]);
CettaDiskStatus cetta_disk_root_check(const CettaDiskRoot *root);

typedef struct { bool remove; CettaDiskBytes key, value; } CettaDiskOp;
typedef struct { uint64_t reference, size; } CettaDiskBlob;
typedef struct CettaDiskCursor CettaDiskCursor;
typedef struct CettaDiskBlobReader CettaDiskBlobReader;

/* All operations are interpreted in order in one candidate root. No operation
 * count or corpus-sized in-memory index is retained between calls. */
CettaDiskStatus cetta_disk_build(const CettaDiskRoot *base,
    const CettaDiskOp *operations, size_t count, CettaDiskRoot *candidate);
/* On any sync failure the candidate is invalidated (fd becomes -1, without
 * closing the borrowed descriptor). Do not re-sync it or derive new roots
 * from any unsynchronized candidate sharing those writes. A successful later
 * fsync does not repair lost writeback. Rebuild from the last acknowledged
 * root, rewriting discarded bytes, or use a fresh segment. */
CettaDiskStatus cetta_disk_sync(CettaDiskRoot *candidate);
CettaDiskStatus cetta_disk_lookup(const CettaDiskRoot *root,
    CettaDiskBytes key, CettaDiskBlob *value);

/* Verify the whole value through bounded scratch space, then report the
 * requested range. On failure written is zero; destination is unspecified.
 * A caller needing the entire body allocates that body, not the whole store.
 */
CettaDiskStatus cetta_disk_blob_read(const CettaDiskRoot *root, CettaDiskBlob value,
    uint64_t offset, void *destination, size_t capacity, size_t *written);

/* Verify once, then read duplicate-safe ranges from an owned immutable fd.
 * This avoids rehashing the whole body for every chunk. The owner must keep
 * published bytes immutable, including while old readers remain open. */
CettaDiskStatus cetta_disk_blob_reader_open(const CettaDiskRoot *root, CettaDiskBlob value,
    CettaDiskBlobReader **out);
CettaDiskStatus cetta_disk_blob_reader_read(CettaDiskBlobReader *reader,
    uint64_t offset, void *destination, size_t capacity, size_t *written);
uint64_t cetta_disk_blob_reader_size(const CettaDiskBlobReader *reader);
void cetta_disk_blob_reader_close(CettaDiskBlobReader *reader);

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
/* The same interval, returned in decreasing byte-key order. Upper remains
 * exclusive; exclusive still controls the lower bound. Seeking starts at the
 * upper edge without enumerating the earlier keys. */
CettaDiskStatus cetta_disk_cursor_open_reverse_range(const CettaDiskRoot *root,
    const CettaDiskBytes *lower, bool exclusive, const CettaDiskBytes *upper,
    CettaDiskCursor **out);
CettaDiskStatus cetta_disk_cursor_next(CettaDiskCursor *cursor,
    CettaDiskBuffer *key, CettaDiskBlob *value);
const CettaDiskRoot *cetta_disk_cursor_root(const CettaDiskCursor *cursor);
/* Requested heap storage retained by a quiescent cursor, counted through the
 * common identity census. The borrowed disk contents and kernel fd state are
 * not heap allocations and are not included. */
struct CettaStorageCensus;
void cetta_disk_cursor_storage_census(const CettaDiskCursor *cursor,
    struct CettaStorageCensus *census);
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
