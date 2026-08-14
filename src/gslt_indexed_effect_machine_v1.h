#ifndef CETTA_GSLT_INDEXED_EFFECT_MACHINE_V1_H
#define CETTA_GSLT_INDEXED_EFFECT_MACHINE_V1_H

#include "gslt_indexed_instruction_decoder_v1.h"
#include "gslt_split_indexed_table_v1.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CETTA_GSLT_INDEXED_EFFECT_OK_V1 = 0,
    CETTA_GSLT_INDEXED_EFFECT_REJECTED_V1 = 1,
    CETTA_GSLT_INDEXED_EFFECT_RESOURCE_V1 = 2,
    CETTA_GSLT_INDEXED_EFFECT_INVALID_V1 = 3,
    CETTA_GSLT_INDEXED_EFFECT_UNSUPPORTED_V1 = 4
} CettaGsltIndexedEffectResultV1;

typedef CettaGsltIndexedEffectResultV1
(*CettaGsltIndexedEffectTableViewV1)(
    void *context, CettaGsltSplitIndexedTableV1 *table_out);
typedef CettaGsltIndexedEffectResultV1
(*CettaGsltIndexedEffectValueV1)(void *context, const void *value);
typedef CettaGsltIndexedEffectResultV1
(*CettaGsltIndexedEffectControlV1)(void *context);

typedef struct {
    void *context;
    CettaGsltIndexedEffectTableViewV1 table_view;
    CettaGsltIndexedEffectValueV1 use_prepared;
    CettaGsltIndexedEffectValueV1 use_saved;
    CettaGsltIndexedEffectControlV1 save_top;
    CettaGsltIndexedEffectControlV1 use_unknown;
} CettaGsltIndexedEffectAlgebraV1;

typedef enum {
    CETTA_GSLT_INDEXED_EFFECT_MACHINE_OK_V1 = 0,
    CETTA_GSLT_INDEXED_EFFECT_MACHINE_REJECTED_V1 = 1,
    CETTA_GSLT_INDEXED_EFFECT_MACHINE_RESOURCE_V1 = 2,
    CETTA_GSLT_INDEXED_EFFECT_MACHINE_INVALID_V1 = 3,
    CETTA_GSLT_INDEXED_EFFECT_MACHINE_DECODE_V1 = 4,
    CETTA_GSLT_INDEXED_EFFECT_MACHINE_INDEX_V1 = 5,
    CETTA_GSLT_INDEXED_EFFECT_MACHINE_UNSUPPORTED_V1 = 6
} CettaGsltIndexedEffectMachineResultV1;

typedef struct {
    CettaGsltIndexedInstructionDecoderV1 decoder;
    CettaGsltIndexedEffectAlgebraV1 algebra;
    CettaGsltIndexedDecodeResultV1 decode_failure;
    CettaGsltIndexedInstructionKindV1 effect_failure_kind;
    uint64_t effect_failure_index;
    uint64_t value_instruction_len;
    bool ready;
} CettaGsltIndexedEffectMachineV1;

CettaGsltIndexedEffectMachineResultV1
cetta_gslt_indexed_effect_machine_init_v1(
    CettaGsltIndexedEffectMachineV1 *machine,
    const CettaGsltIndexedInstructionPlanV1 *plan,
    const CettaGsltIndexedEffectAlgebraV1 *algebra);

CettaGsltIndexedEffectMachineResultV1
cetta_gslt_indexed_effect_machine_execute_v1(
    CettaGsltIndexedEffectMachineV1 *machine,
    const uint8_t *bytes,
    uint32_t byte_len);

CettaGsltIndexedEffectMachineResultV1
cetta_gslt_indexed_effect_machine_finish_v1(
    CettaGsltIndexedEffectMachineV1 *machine);

#endif
