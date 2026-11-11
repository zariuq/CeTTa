#ifndef CETTA_BYTE_RUN_SORT_H
#define CETTA_BYTE_RUN_SORT_H
#include "act_trie.h"
struct CettaStorageCensus;
typedef struct CettaByteRunSort CettaByteRunSort;
typedef struct {uint64_t offset,size;} CettaByteSpan;
typedef struct {CettaByteSpan key;uint64_t value;} CettaByteSortItem;
/* The three distinct empty files are private readable/writable scratch
 * without append mode. Key bytes are immutable
 * after append. Sorted runs use fixed-size private descriptors; no run catalog
 * grows with the corpus. All descriptors are duplicated by the sorter. */
CettaActStatus cetta_byte_sort_open(int keys,int runs_a,int runs_b,
    size_t memory_bytes,CettaByteRunSort **);
CettaActStatus cetta_byte_sort_key(CettaByteRunSort *,CettaActBytes,CettaByteSpan *);
CettaActStatus cetta_byte_sort_add(CettaByteRunSort *,CettaByteSpan,uint64_t value);
CettaActStatus cetta_byte_sort_put(CettaByteRunSort *,CettaActBytes,uint64_t value);
/* Seal input, then drive step until OK. PROGRESS never means exhaustion. */
CettaActStatus cetta_byte_sort_seal(CettaByteRunSort *);
CettaActStatus cetta_byte_sort_step(CettaByteRunSort *,uint64_t work);
CettaActStatus cetta_byte_sort_next(CettaByteRunSort *,CettaByteSortItem *);
CettaActStatus cetta_byte_sort_read(CettaByteRunSort *,CettaByteSpan,
    uint64_t offset,void *,size_t capacity,size_t *written);
CettaActStatus cetta_byte_sort_compare(CettaByteRunSort *,CettaByteSpan,
    CettaByteSpan,uint64_t work,int *order);
int cetta_byte_sort_key_fd(const CettaByteRunSort *);
typedef struct {
    uint64_t records,key_bytes,run_bytes,read_records;
    size_t heap_bytes,capacity;
    unsigned phase;
} CettaByteSortInfo;
bool cetta_byte_sort_info(const CettaByteRunSort *,CettaByteSortInfo *);
void cetta_byte_sort_storage(const CettaByteRunSort *,struct CettaStorageCensus *);
void cetta_byte_sort_close(CettaByteRunSort *);
#endif
