#ifndef CETTA_SPACE_FILE_H
#define CETTA_SPACE_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A space file, format v0: a space's flat facts in a read-only file that is
 * mapped, validated and then read in place.
 *
 * The file holds a dictionary of the symbols its facts use (sorted, each
 * once), each fact as its arity and its symbols' positions in the dictionary
 * (LEB128 varints), and a side: the facts' numbers sorted by their first
 * symbol's position, with those positions beside them, so a lookup on a
 * bound first symbol finds its run by binary search and reads only the facts
 * in it.  All integers are little-endian.
 *
 *   header     magic "CETTASF0", version, counts and section offsets
 *   dict index (symbols + 1) u64 offsets into the dictionary bytes
 *   dict bytes the symbols' bytes, back to back
 *   rec index  (facts + 1) u64 offsets into the record bytes
 *   rec bytes  per fact: arity, then one position per field (varints)
 *   side keys  per side entry: 0 for a fact with no fields, else 1 + the
 *              position of its first symbol (u64), ascending
 *   side facts per side entry: the fact's number (u64)
 *
 * Its laws (Mettapedia.Machines.SpaceFileLaws):
 * - reading the file back gives the facts it was written from, in their
 *   order and multiplicity;
 * - the facts a lookup on a first symbol reads through the side are the
 *   facts a scan whose first symbol is that symbol selects, as a bag;
 * - a symbol the dictionary does not hold answers no fact, and no record
 *   is read to say so.
 *
 * Opening validates every offset, every record and the side (a permutation
 * of the facts, in key order, agreeing with the records), so a malformed
 * file is refused rather than read out of bounds. */

typedef struct {
    const char *bytes;
    uint32_t length;
} CettaSpaceFileField;

typedef struct {
    const CettaSpaceFileField *fields;
    uint32_t arity;
} CettaSpaceFileFact;

typedef struct CettaSpaceFile CettaSpaceFile;

typedef struct {
    const CettaSpaceFile *file;
    uint64_t next;
    uint64_t end;
} CettaSpaceFileCursor;

/* Writes `count` facts to `path`.  False on an I/O or allocation failure. */
bool cetta_space_file_write(const char *path, const CettaSpaceFileFact *facts,
                            size_t count);

/* Maps and validates `path`.  NULL, with `*error` naming the reason when
 * `error` is given, for an unreadable or malformed file. */
CettaSpaceFile *cetta_space_file_open(const char *path, const char **error);
void cetta_space_file_close(CettaSpaceFile *file);

uint64_t cetta_space_file_fact_count(const CettaSpaceFile *file);
uint64_t cetta_space_file_symbol_count(const CettaSpaceFile *file);

/* The fields of fact `index`, pointing into the mapping, into `fields`
 * (room for `capacity`); `*arity_out` is the fact's arity.  False when the
 * index is out of range or the fact has more fields than `capacity`. */
bool cetta_space_file_fact(const CettaSpaceFile *file, uint64_t index,
                           CettaSpaceFileField *fields, uint32_t capacity,
                           uint32_t *arity_out);

/* Positions a cursor on the facts whose first symbol is `key`.  False, with
 * an empty cursor, when the dictionary does not hold `key`; no record is
 * read either way until the cursor is advanced. */
bool cetta_space_file_seek(const CettaSpaceFile *file,
                           const char *key, uint32_t key_length,
                           CettaSpaceFileCursor *cursor);

/* The next fact's number, or false when the run is over. */
bool cetta_space_file_cursor_next(CettaSpaceFileCursor *cursor,
                                  uint64_t *fact_index);

/* How many facts have been decoded from this file since it was opened:
 * a lookup's cost in records read. */
uint64_t cetta_space_file_records_read(const CettaSpaceFile *file);

/* Compaction (Mettapedia.Machines.SpaceFileOverlay): writes to `path` a new
 * file holding what a space over `file` holds: the file's facts that no
 * tombstone hides, in their order (a tombstone hides every copy of its
 * fact), then the `added` facts.  The new file numbers its facts afresh,
 * 0, 1, 2, ..., so the survivors keep their order.  False on an I/O or
 * allocation failure. */
bool cetta_space_file_compact(const CettaSpaceFile *file,
                              const CettaSpaceFileFact *added,
                              size_t added_count,
                              const CettaSpaceFileFact *tombstones,
                              size_t tombstone_count, const char *path);

#endif
