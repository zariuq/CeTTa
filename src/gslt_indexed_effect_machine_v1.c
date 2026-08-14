#include "gslt_indexed_effect_machine_v1.h"

#include <limits.h>
#include <string.h>

static CettaGsltIndexedEffectMachineResultV1 indexed_effect_result(
    CettaGsltIndexedEffectResultV1 result) {
    switch (result) {
    case CETTA_GSLT_INDEXED_EFFECT_OK_V1:
        return CETTA_GSLT_INDEXED_EFFECT_MACHINE_OK_V1;
    case CETTA_GSLT_INDEXED_EFFECT_REJECTED_V1:
        return CETTA_GSLT_INDEXED_EFFECT_MACHINE_REJECTED_V1;
    case CETTA_GSLT_INDEXED_EFFECT_RESOURCE_V1:
        return CETTA_GSLT_INDEXED_EFFECT_MACHINE_RESOURCE_V1;
    case CETTA_GSLT_INDEXED_EFFECT_UNSUPPORTED_V1:
        return CETTA_GSLT_INDEXED_EFFECT_MACHINE_UNSUPPORTED_V1;
    case CETTA_GSLT_INDEXED_EFFECT_INVALID_V1:
    default:
        return CETTA_GSLT_INDEXED_EFFECT_MACHINE_INVALID_V1;
    }
}

CettaGsltIndexedEffectMachineResultV1
cetta_gslt_indexed_effect_machine_init_v1(
    CettaGsltIndexedEffectMachineV1 *machine,
    const CettaGsltIndexedInstructionPlanV1 *plan,
    const CettaGsltIndexedEffectAlgebraV1 *algebra) {
    CettaGsltIndexedDecodeResultV1 decode_result;

    if (!machine)
        return CETTA_GSLT_INDEXED_EFFECT_MACHINE_INVALID_V1;
    memset(machine, 0, sizeof(*machine));
    if (!algebra || !algebra->table_view || !algebra->use_prepared ||
        !algebra->use_saved || !algebra->save_top ||
        !algebra->use_unknown)
        return CETTA_GSLT_INDEXED_EFFECT_MACHINE_INVALID_V1;
    decode_result = cetta_gslt_indexed_instruction_decoder_init_v1(
        &machine->decoder, plan);
    if (decode_result != CETTA_GSLT_INDEXED_DECODE_OK_V1) {
        machine->decode_failure = decode_result;
        return CETTA_GSLT_INDEXED_EFFECT_MACHINE_DECODE_V1;
    }
    machine->algebra = *algebra;
    machine->ready = true;
    return CETTA_GSLT_INDEXED_EFFECT_MACHINE_OK_V1;
}

static CettaGsltIndexedEffectMachineResultV1 indexed_execute_event(
    CettaGsltIndexedEffectMachineV1 *machine,
    CettaGsltIndexedInstructionEventV1 event) {
    CettaGsltIndexedEffectResultV1 effect_result;

    machine->effect_failure_kind = event.kind;
    machine->effect_failure_index = event.index;
    if (event.kind == CETTA_GSLT_INDEXED_INSTRUCTION_NONE_V1)
        return CETTA_GSLT_INDEXED_EFFECT_MACHINE_OK_V1;
    if ((event.kind == CETTA_GSLT_INDEXED_INSTRUCTION_USE_V1 ||
         event.kind == CETTA_GSLT_INDEXED_INSTRUCTION_UNKNOWN_V1) &&
        machine->value_instruction_len == UINT64_MAX)
        return CETTA_GSLT_INDEXED_EFFECT_MACHINE_RESOURCE_V1;
    if (event.kind == CETTA_GSLT_INDEXED_INSTRUCTION_USE_V1) {
        CettaGsltSplitIndexedTableV1 table;
        CettaGsltSplitIndexedValueV1 value;

        effect_result = machine->algebra.table_view(
            machine->algebra.context, &table);
        if (effect_result != CETTA_GSLT_INDEXED_EFFECT_OK_V1)
            return indexed_effect_result(effect_result);
        if (!cetta_gslt_split_indexed_table_validate_v1(&table))
            return CETTA_GSLT_INDEXED_EFFECT_MACHINE_INVALID_V1;
        if (!cetta_gslt_split_indexed_table_get_v1(
                &table, event.index, &value))
            return CETTA_GSLT_INDEXED_EFFECT_MACHINE_INDEX_V1;
        if (value.kind == CETTA_GSLT_SPLIT_INDEXED_VALUE_V1_PREPARED)
            effect_result = machine->algebra.use_prepared(
                machine->algebra.context, value.value);
        else if (value.kind == CETTA_GSLT_SPLIT_INDEXED_VALUE_V1_SAVED)
            effect_result = machine->algebra.use_saved(
                machine->algebra.context, value.value);
        else
            return CETTA_GSLT_INDEXED_EFFECT_MACHINE_INVALID_V1;
    } else if (event.kind == CETTA_GSLT_INDEXED_INSTRUCTION_SAVE_V1) {
        effect_result = machine->algebra.save_top(machine->algebra.context);
    } else if (event.kind == CETTA_GSLT_INDEXED_INSTRUCTION_UNKNOWN_V1) {
        effect_result = machine->algebra.use_unknown(
            machine->algebra.context);
    } else {
        return CETTA_GSLT_INDEXED_EFFECT_MACHINE_INVALID_V1;
    }
    if (effect_result != CETTA_GSLT_INDEXED_EFFECT_OK_V1)
        return indexed_effect_result(effect_result);
    if ((event.kind == CETTA_GSLT_INDEXED_INSTRUCTION_USE_V1 ||
         event.kind == CETTA_GSLT_INDEXED_INSTRUCTION_UNKNOWN_V1))
        machine->value_instruction_len++;
    return CETTA_GSLT_INDEXED_EFFECT_MACHINE_OK_V1;
}

CettaGsltIndexedEffectMachineResultV1
cetta_gslt_indexed_effect_machine_execute_v1(
    CettaGsltIndexedEffectMachineV1 *machine,
    const uint8_t *bytes,
    uint32_t byte_len) {
    uint32_t index;

    if (!machine || !machine->ready || (byte_len != 0u && !bytes))
        return CETTA_GSLT_INDEXED_EFFECT_MACHINE_INVALID_V1;
    for (index = 0u; index < byte_len; index++) {
        CettaGsltIndexedInstructionEventV1 event;
        CettaGsltIndexedDecodeResultV1 decode_result =
            cetta_gslt_indexed_instruction_feed_v1(
                &machine->decoder, bytes[index], &event);
        CettaGsltIndexedEffectMachineResultV1 effect_machine_result;

        if (decode_result != CETTA_GSLT_INDEXED_DECODE_OK_V1) {
            machine->decode_failure = decode_result;
            return CETTA_GSLT_INDEXED_EFFECT_MACHINE_DECODE_V1;
        }
        effect_machine_result = indexed_execute_event(machine, event);
        if (effect_machine_result !=
            CETTA_GSLT_INDEXED_EFFECT_MACHINE_OK_V1)
            return effect_machine_result;
    }
    return CETTA_GSLT_INDEXED_EFFECT_MACHINE_OK_V1;
}

CettaGsltIndexedEffectMachineResultV1
cetta_gslt_indexed_effect_machine_finish_v1(
    CettaGsltIndexedEffectMachineV1 *machine) {
    CettaGsltIndexedDecodeResultV1 decode_result;

    if (!machine || !machine->ready)
        return CETTA_GSLT_INDEXED_EFFECT_MACHINE_INVALID_V1;
    decode_result = cetta_gslt_indexed_instruction_finish_v1(
        &machine->decoder);
    if (decode_result != CETTA_GSLT_INDEXED_DECODE_OK_V1) {
        machine->decode_failure = decode_result;
        return CETTA_GSLT_INDEXED_EFFECT_MACHINE_DECODE_V1;
    }
    return CETTA_GSLT_INDEXED_EFFECT_MACHINE_OK_V1;
}
