#ifndef CETTA_ACT_TRIE_H
#define CETTA_ACT_TRIE_H

/* ACTree03 interoperability with Adam Vandervorst's MIT-licensed PathMap.
 * Byte keys and unsigned values have no built-in atom semantics. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  CETTA_ACT_OK,
  CETTA_ACT_END,
  CETTA_ACT_NOT_FOUND,
  CETTA_ACT_IO,
  CETTA_ACT_CORRUPT,
  CETTA_ACT_NOMEM,
  CETTA_ACT_LIMIT,
  CETTA_ACT_UNCERTAIN,
  CETTA_ACT_UNSUPPORTED,
  CETTA_ACT_READ_ONLY,
} CettaActStatus;
typedef struct CettaActTree CettaActTree;
typedef struct CettaActCursor CettaActCursor;
struct CettaStorageCensus;
typedef struct {
  const unsigned char *data;
  size_t size;
} CettaActBytes;
typedef struct {
  CettaActBytes key;
  uint64_t value;
} CettaActItem;

bool cetta_act_varint(const unsigned char *, size_t, uint64_t *, size_t *);
size_t cetta_act_put_varint(unsigned char out[9], uint64_t);
CettaActStatus cetta_act_open(const char *path, CettaActTree **);
/* Raw immutable mapping and validated embedded ACT image; the view retains
 * its parent mapping, so neither path replacement nor parent close recaptures.
 */
CettaActStatus cetta_act_map(const char *path, CettaActTree **);
CettaActStatus cetta_act_view(CettaActTree *, size_t offset, size_t size,
                              CettaActTree **);
/* Copy the image; no borrowed memory survives this call. */
CettaActStatus cetta_act_from_bytes(CettaActBytes, CettaActTree **);
void cetta_act_retain(CettaActTree *);
void cetta_act_release(CettaActTree *);
size_t cetta_act_mapped_bytes(const CettaActTree *);
CettaActBytes cetta_act_image(const CettaActTree *);
CettaActStatus cetta_act_get(const CettaActTree *, CettaActBytes, uint64_t *);
CettaActStatus cetta_act_cursor_open(CettaActTree *, const CettaActBytes *lower,
                                     const CettaActBytes *upper,
                                     CettaActCursor **);
CettaActStatus cetta_act_cursor_prefix(CettaActTree *, CettaActBytes,
                                       CettaActCursor **);
CettaActStatus cetta_act_cursor_clone(const CettaActCursor *, bool rewind,
                                      CettaActCursor **);
/* Key bytes are borrowed until next/close; values are copied. */
CettaActStatus cetta_act_cursor_next(CettaActCursor *, CettaActBytes *,
                                     uint64_t *);
void cetta_act_cursor_close(CettaActCursor *);
void cetta_act_storage(const CettaActTree *, struct CettaStorageCensus *);
void cetta_act_cursor_storage(const CettaActCursor *,
                              struct CettaStorageCensus *);
CettaActStatus cetta_act_validate(CettaActTree *, uint64_t *count);
/* Items must be strictly increasing. Output is deterministic ACTree03 bytes.
 * The caller frees the image; file publication syncs data and its directory. */
CettaActStatus cetta_act_build(const CettaActItem *, size_t,
                               unsigned char **image, size_t *size);
CettaActStatus cetta_act_publish(const char *path, CettaActBytes);
const char *cetta_act_status_name(CettaActStatus);
#endif
