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
  CETTA_ACT_PROGRESS,
} CettaActStatus;
typedef struct CettaActTree CettaActTree;
typedef struct CettaActCursor CettaActCursor;
typedef struct CettaActBuilder CettaActBuilder;
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
CettaActStatus cetta_act_map_fd(int fd, CettaActTree **);
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
CettaActStatus cetta_act_cursor_open_reverse(CettaActTree *,
    const CettaActBytes *lower, const CettaActBytes *upper, CettaActCursor **);
CettaActStatus cetta_act_cursor_prefix(CettaActTree *, CettaActBytes,
                                       CettaActCursor **);
CettaActStatus cetta_act_cursor_clone(const CettaActCursor *, bool rewind,
                                      CettaActCursor **);
/* Key bytes are borrowed until next/close; values are copied. */
CettaActStatus cetta_act_cursor_next(CettaActCursor *, CettaActBytes *,
                                     uint64_t *);
/* Node-work allowance; PROGRESS keeps the traversal live. The key allowance
 * bounds a path before allocating it, independently of the corpus size. */
CettaActStatus cetta_act_cursor_next_bounded(CettaActCursor *,uint64_t node_work,
    CettaActBytes *,uint64_t *);
bool cetta_act_cursor_key_limit(CettaActCursor *,size_t maximum_bytes);
void cetta_act_cursor_close(CettaActCursor *);
void cetta_act_storage(const CettaActTree *, struct CettaStorageCensus *);
void cetta_act_cursor_storage(const CettaActCursor *,
                              struct CettaStorageCensus *);
CettaActStatus cetta_act_validate(CettaActTree *, uint64_t *count);
/* Items must be strictly increasing. Output is deterministic ACTree03 bytes.
 * The caller frees the image; file publication syncs data and its directory. */
CettaActStatus cetta_act_build(const CettaActItem *, size_t,
                               unsigned char **image, size_t *size);
/* Incremental sorted construction into an empty private file. The three
 * descriptors must name distinct readable/writable regular files without
 * append mode. Scratch files are private
 * working state, not restart tokens. The builder duplicates descriptors;
 * closing it never publishes or removes the caller's files. Keys are copied
 * or spooled before put returns. A failed builder cannot be reused. */
CettaActStatus cetta_act_builder_open(int output_fd, int frontier_fd,
    int key_fd, size_t memory_bytes, CettaActBuilder **);
size_t cetta_act_builder_minimum_memory(void);
CettaActStatus cetta_act_builder_put(CettaActBuilder *, CettaActBytes,
    uint64_t value);
/* The key is an immutable range in a separate file. No complete large key
 * is loaded into heap to pass it between external sorting and construction. */
CettaActStatus cetta_act_builder_put_fd(CettaActBuilder *,int key_fd,
    uint64_t offset,uint64_t size,uint64_t value);
CettaActStatus cetta_act_builder_finish(CettaActBuilder *, uint64_t *size);
CettaActStatus cetta_act_builder_finish_step(CettaActBuilder *, uint64_t work,
    uint64_t *size);
typedef struct {
    uint64_t records, output_bytes, scratch_bytes;
    size_t heap_bytes, peak_heap_bytes;
} CettaActBuildInfo;
bool cetta_act_builder_info(const CettaActBuilder *, CettaActBuildInfo *);
void cetta_act_builder_storage(const CettaActBuilder *,struct CettaStorageCensus *);
void cetta_act_builder_close(CettaActBuilder *);
CettaActStatus cetta_act_publish(const char *path, CettaActBytes);
typedef struct CettaActPublication CettaActPublication;
CettaActStatus cetta_act_publication_open(const char *path,CettaActPublication **);
/* Private output and scratch files are descriptor-relative and unlinked.
 * Commit synchronizes the output without creating a public pathname. */
CettaActStatus cetta_act_publication_open_private(int directory_fd,CettaActPublication **);
int cetta_act_publication_fd(const CettaActPublication *);
CettaActStatus cetta_act_publication_scratch(const CettaActPublication *,int *fd);
CettaActStatus cetta_act_publication_commit(CettaActPublication *);
CettaActStatus cetta_act_publication_sidecar(CettaActPublication *,bool retire);
void cetta_act_publication_close(CettaActPublication *);
const char *cetta_act_status_name(CettaActStatus);
#endif
