#ifndef CETTA_NATIVE_ACT_BASE_H
#define CETTA_NATIVE_ACT_BASE_H
#include "act_trie.h"

/* Immutable generic byte map. ACT values address packed payload records;
 * neither this layer nor its wire format interprets MeTTa application keys. */
typedef struct CettaActBase CettaActBase;
typedef struct CettaActBaseBuilder CettaActBaseBuilder;
typedef struct CettaActBaseValidation CettaActBaseValidation;
typedef struct { size_t key_bytes, value_bytes; } CettaActBaseLimits;
typedef struct {
    unsigned char generation[16], digest[32];
    uint64_t bytes, records;
    CettaActBaseLimits limits;
} CettaActBaseInfo;
CettaActStatus cetta_act_base_open_fd(int, CettaActBase **);
CettaActStatus cetta_act_base_validation_open(int,CettaActBaseValidation **);
CettaActStatus cetta_act_base_validation_step(CettaActBaseValidation *,
    uint64_t work,CettaActBase **);
void cetta_act_base_validation_close(CettaActBaseValidation *);
void cetta_act_base_validation_storage(const CettaActBaseValidation *,struct CettaStorageCensus *);
void cetta_act_base_retain(CettaActBase *);
void cetta_act_base_release(CettaActBase *);
bool cetta_act_base_info(const CettaActBase *, CettaActBaseInfo *);
CettaActStatus cetta_act_base_lookup(const CettaActBase *, CettaActBytes,
    uint64_t *reference, uint64_t *size);
CettaActStatus cetta_act_base_value(const CettaActBase *, uint64_t reference,
    CettaActBytes *);
CettaActStatus cetta_act_base_cursor(CettaActBase *, const CettaActBytes *lower,
    const CettaActBytes *upper, bool reverse, CettaActCursor **);
void cetta_act_base_storage(const CettaActBase *, struct CettaStorageCensus *);

/* All descriptors name distinct empty private files. Construction and
 * publication are separate. A value can be supplied in bounded chunks, and
 * then associated with its next sorted key. Reference zero is the shared
 * empty value; nonempty payloads occur once in key order. */
CettaActStatus cetta_act_base_builder_open(int output, int trie, int payload,
    int frontier, int key, size_t memory_bytes, const unsigned char generation[16],
    CettaActBaseLimits, CettaActBaseBuilder **);
size_t cetta_act_base_builder_minimum_memory(void);
CettaActStatus cetta_act_base_value_begin(CettaActBaseBuilder *, uint64_t size);
CettaActStatus cetta_act_base_value_write(CettaActBaseBuilder *, CettaActBytes);
CettaActStatus cetta_act_base_key(CettaActBaseBuilder *, CettaActBytes);
CettaActStatus cetta_act_base_builder_finish(CettaActBaseBuilder *, CettaActBaseInfo *);
CettaActStatus cetta_act_base_builder_finish_step(CettaActBaseBuilder *,
    uint64_t byte_work, CettaActBaseInfo *);
void cetta_act_base_builder_close(CettaActBaseBuilder *);
typedef struct {
    unsigned phase;
    uint64_t records, encoded_bytes, assembled_bytes;
} CettaActBaseBuildInfo;
bool cetta_act_base_builder_info(const CettaActBaseBuilder *,CettaActBaseBuildInfo *);
void cetta_act_base_builder_storage(const CettaActBaseBuilder *,struct CettaStorageCensus *);
#endif
