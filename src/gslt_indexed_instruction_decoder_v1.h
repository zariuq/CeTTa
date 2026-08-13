#ifndef CETTA_GSLT_INDEXED_INSTRUCTION_DECODER_V1_H
#define CETTA_GSLT_INDEXED_INSTRUCTION_DECODER_V1_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t terminal_low;
    uint8_t terminal_high;
    uint8_t continuation_low;
    uint8_t continuation_high;
    uint8_t save_byte;
    uint8_t unknown_byte;
    uint32_t terminal_radix;
    uint32_t terminal_digit_bias;
    uint32_t continuation_radix;
    uint32_t continuation_digit_bias;
} CettaGsltIndexedInstructionPlanV1;

typedef enum {
    CETTA_GSLT_INDEXED_INSTRUCTION_NONE_V1 = 0,
    CETTA_GSLT_INDEXED_INSTRUCTION_USE_V1 = 1,
    CETTA_GSLT_INDEXED_INSTRUCTION_SAVE_V1 = 2,
    CETTA_GSLT_INDEXED_INSTRUCTION_UNKNOWN_V1 = 3
} CettaGsltIndexedInstructionKindV1;

typedef struct {
    CettaGsltIndexedInstructionKindV1 kind;
    uint64_t index;
} CettaGsltIndexedInstructionEventV1;

typedef enum {
    CETTA_GSLT_INDEXED_DECODE_OK_V1 = 0,
    CETTA_GSLT_INDEXED_DECODE_INVALID_PLAN_V1 = 1,
    CETTA_GSLT_INDEXED_DECODE_INVALID_BYTE_V1 = 2,
    CETTA_GSLT_INDEXED_DECODE_OVERFLOW_V1 = 3,
    CETTA_GSLT_INDEXED_DECODE_SAVE_INSIDE_INDEX_V1 = 4,
    CETTA_GSLT_INDEXED_DECODE_OPEN_INDEX_AT_END_V1 = 5,
    CETTA_GSLT_INDEXED_DECODE_INVALID_ARGUMENT_V1 = 6
} CettaGsltIndexedDecodeResultV1;

typedef struct {
    CettaGsltIndexedInstructionPlanV1 plan;
    uint64_t accumulator;
    uint64_t consumed_byte_len;
    uint64_t emitted_instruction_len;
    bool open_index;
    bool ready;
} CettaGsltIndexedInstructionDecoderV1;

bool cetta_gslt_indexed_instruction_plan_validate_v1(
    const CettaGsltIndexedInstructionPlanV1 *plan);

CettaGsltIndexedDecodeResultV1 cetta_gslt_indexed_instruction_decoder_init_v1(
    CettaGsltIndexedInstructionDecoderV1 *decoder,
    const CettaGsltIndexedInstructionPlanV1 *plan);

CettaGsltIndexedDecodeResultV1 cetta_gslt_indexed_instruction_feed_v1(
    CettaGsltIndexedInstructionDecoderV1 *decoder,
    uint8_t byte,
    CettaGsltIndexedInstructionEventV1 *event);

CettaGsltIndexedDecodeResultV1 cetta_gslt_indexed_instruction_finish_v1(
    const CettaGsltIndexedInstructionDecoderV1 *decoder);

#endif /* CETTA_GSLT_INDEXED_INSTRUCTION_DECODER_V1_H */
