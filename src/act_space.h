#ifndef CETTA_ACT_SPACE_H
#define CETTA_ACT_SPACE_H
#include "act_trie.h"
#include "space.h"

/* Capture an immutable upstream ACTree03 set or typed CACT0001 occurrence bag.
 * The initialized empty target becomes an ordinary source-backed Space. */
CettaActStatus cetta_act_space_capture(Space *, const char *path,
                                       size_t maximum_bytes);
typedef struct {
    uint64_t occurrences, bytes;
    size_t maximum_bytes;
    bool typed;
    char digest[65];
} CettaActSpaceInfo;
/* Content identity of an owned, fully validated immutable file source.
 * Hashing retains its backing and performs no whole-image heap copy. */
CettaActStatus cetta_act_space_info(const Space *, CettaActSpaceInfo *);
CettaActStatus cetta_act_space_dump(Space *, const char *path, bool mork,
                                    size_t maximum_bytes);
typedef struct CettaActSpaceExport CettaActSpaceExport;
/* In-process jobs own their captured scan and private files. Cancel by close;
 * no destination is installed until validation and durable publication. */
CettaActStatus cetta_act_space_export_open(Space *,const char *path,bool mork,
    size_t maximum_bytes,size_t memory_bytes,CettaActSpaceExport **);
CettaActStatus cetta_act_space_export_step(CettaActSpaceExport *,uint64_t work);
typedef struct {
    /* Logical temporary bytes include pending file buffers; filesystem block
     * allocation and mapped residency are separate operating-system measures. */
    uint64_t occurrences,payload_bytes,trie_bytes,temporary_bytes;
    /* Construction allocations, excluding retained input ownership and mapped
     * file residency. The storage visitor also walks retained input owners. */
    size_t heap_bytes;
    unsigned phase;
    bool complete;
} CettaActSpaceExportInfo;
bool cetta_act_space_export_info(const CettaActSpaceExport *,CettaActSpaceExportInfo *);
void cetta_act_space_export_storage(const CettaActSpaceExport *,struct CettaStorageCensus *);
void cetta_act_space_export_close(CettaActSpaceExport *);
/* Validate all incoming rows before publishing one ordinary admission batch.
 * Copying allocates target-domain occurrences; act:open retains saved
 * identities. */
CettaActStatus cetta_act_space_load(Space *, Arena *, const char *path,
                                    size_t maximum_bytes);
#endif
