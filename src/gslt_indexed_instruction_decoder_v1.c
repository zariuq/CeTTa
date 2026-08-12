#include "gslt_indexed_instruction_decoder_v1.h"

#include <limits.h>
#include <string.h>

static bool indexed_in_range(uint8_t byte, uint8_t low, uint8_t high) {
    return byte >= low && byte <= high;
}

static bool indexed_ranges_overlap(uint8_t left_low, uint8_t left_high,
                                   uint8_t right_low, uint8_t right_high) {
    return !(left_high < right_low || right_high < left_low);
}

bool cetta_gslt_indexed_instruction_plan_validate_v1(
    const CettaGsltIndexedInstructionPlanV1 *plan) {
    return plan && plan->terminal_low <= plan->terminal_high &&
           plan->continuation_low <= plan->continuation_high &&
           !indexed_ranges_overlap(
               plan->terminal_low, plan->terminal_high,
               plan->continuation_low, plan->continuation_high) &&
           !indexed_in_range(
               plan->save_byte, plan->terminal_low, plan->terminal_high) &&
           !indexed_in_range(plan->save_byte, plan->continuation_low,
                             plan->continuation_high) &&
           !indexed_in_range(plan->unknown_byte, plan->terminal_low,
                             plan->terminal_high) &&
           !indexed_in_range(plan->unknown_byte, plan->continuation_low,
                             plan->continuation_high) &&
           plan->save_byte != plan->unknown_byte &&
           plan->terminal_radix != 0u &&
           plan->continuation_radix != 0u;
}

CettaGsltIndexedDecodeResultV1 cetta_gslt_indexed_instruction_decoder_init_v1(
    CettaGsltIndexedInstructionDecoderV1 *decoder,
    const CettaGsltIndexedInstructionPlanV1 *plan) {
    if (!decoder)
        return CETTA_GSLT_INDEXED_DECODE_INVALID_ARGUMENT_V1;
    memset(decoder, 0, sizeof(*decoder));
    if (!cetta_gslt_indexed_instruction_plan_validate_v1(plan))
        return CETTA_GSLT_INDEXED_DECODE_INVALID_PLAN_V1;
    decoder->plan = *plan;
    decoder->ready = true;
    return CETTA_GSLT_INDEXED_DECODE_OK_V1;
}

static CettaGsltIndexedDecodeResultV1 indexed_checked_update(
    uint32_t radix, uint64_t accumulator, uint64_t digit,
    uint64_t *value_out) {
    if (!value_out || radix == 0u)
        return CETTA_GSLT_INDEXED_DECODE_INVALID_ARGUMENT_V1;
    if (accumulator > (UINT64_MAX - digit) / (uint64_t)radix)
        return CETTA_GSLT_INDEXED_DECODE_OVERFLOW_V1;
    *value_out = (uint64_t)radix * accumulator + digit;
    return CETTA_GSLT_INDEXED_DECODE_OK_V1;
}

CettaGsltIndexedDecodeResultV1 cetta_gslt_indexed_instruction_feed_v1(
    CettaGsltIndexedInstructionDecoderV1 *decoder,
    uint8_t byte,
    CettaGsltIndexedInstructionEventV1 *event) {
    const CettaGsltIndexedInstructionPlanV1 *plan;
    uint64_t digit;
    uint64_t value;
    CettaGsltIndexedDecodeResultV1 result;

    if (!decoder || !decoder->ready || !event)
        return CETTA_GSLT_INDEXED_DECODE_INVALID_ARGUMENT_V1;
    plan = &decoder->plan;
    event->kind = CETTA_GSLT_INDEXED_INSTRUCTION_NONE_V1;
    event->index = 0u;

    if (indexed_in_range(byte, plan->terminal_low, plan->terminal_high)) {
        digit = (uint64_t)(byte - plan->terminal_low) +
                (uint64_t)plan->terminal_digit_bias;
        result = indexed_checked_update(
            plan->terminal_radix, decoder->accumulator, digit, &value);
        if (result != CETTA_GSLT_INDEXED_DECODE_OK_V1)
            return result;
        decoder->accumulator = 0u;
        decoder->open_index = false;
        event->kind = CETTA_GSLT_INDEXED_INSTRUCTION_USE_V1;
        event->index = value;
        return CETTA_GSLT_INDEXED_DECODE_OK_V1;
    }
    if (indexed_in_range(
            byte, plan->continuation_low, plan->continuation_high)) {
        digit = (uint64_t)(byte - plan->continuation_low) +
                (uint64_t)plan->continuation_digit_bias;
        result = indexed_checked_update(
            plan->continuation_radix, decoder->accumulator, digit, &value);
        if (result != CETTA_GSLT_INDEXED_DECODE_OK_V1)
            return result;
        decoder->accumulator = value;
        decoder->open_index = true;
        return CETTA_GSLT_INDEXED_DECODE_OK_V1;
    }
    if (byte == plan->save_byte) {
        if (decoder->open_index)
            return CETTA_GSLT_INDEXED_DECODE_SAVE_INSIDE_INDEX_V1;
        event->kind = CETTA_GSLT_INDEXED_INSTRUCTION_SAVE_V1;
        return CETTA_GSLT_INDEXED_DECODE_OK_V1;
    }
    if (byte == plan->unknown_byte) {
        decoder->accumulator = 0u;
        decoder->open_index = false;
        event->kind = CETTA_GSLT_INDEXED_INSTRUCTION_UNKNOWN_V1;
        return CETTA_GSLT_INDEXED_DECODE_OK_V1;
    }
    return CETTA_GSLT_INDEXED_DECODE_INVALID_BYTE_V1;
}

CettaGsltIndexedDecodeResultV1 cetta_gslt_indexed_instruction_finish_v1(
    const CettaGsltIndexedInstructionDecoderV1 *decoder) {
    if (!decoder || !decoder->ready)
        return CETTA_GSLT_INDEXED_DECODE_INVALID_ARGUMENT_V1;
    return decoder->open_index
        ? CETTA_GSLT_INDEXED_DECODE_OPEN_INDEX_AT_END_V1
        : CETTA_GSLT_INDEXED_DECODE_OK_V1;
}
