#include "gslt_indexed_instruction_decoder_v1.h"

#include <limits.h>
#include <stdio.h>

static unsigned checks_run;
static unsigned checks_failed;

static void expect(bool condition, const char *message) {
    checks_run++;
    if (!condition) {
        checks_failed++;
        fprintf(stderr, "FAIL: %s\n", message);
    }
}

static CettaGsltIndexedInstructionPlanV1 proof_dag_plan(void) {
    return (CettaGsltIndexedInstructionPlanV1){
        .terminal_low = 65u,
        .terminal_high = 84u,
        .continuation_low = 85u,
        .continuation_high = 89u,
        .save_byte = 90u,
        .unknown_byte = 63u,
        .terminal_radix = 20u,
        .terminal_digit_bias = 0u,
        .continuation_radix = 5u,
        .continuation_digit_bias = 1u,
    };
}

static CettaGsltIndexedInstructionPlanV1 parser_index_plan(void) {
    return (CettaGsltIndexedInstructionPlanV1){
        .terminal_low = 0u,
        .terminal_high = 9u,
        .continuation_low = 10u,
        .continuation_high = 19u,
        .save_byte = 20u,
        .unknown_byte = 21u,
        .terminal_radix = 10u,
        .terminal_digit_bias = 0u,
        .continuation_radix = 10u,
        .continuation_digit_bias = 0u,
    };
}

int main(void) {
    CettaGsltIndexedInstructionDecoderV1 decoder;
    CettaGsltIndexedInstructionEventV1 event;
    CettaGsltIndexedInstructionPlanV1 proof = proof_dag_plan();
    CettaGsltIndexedInstructionPlanV1 parser = parser_index_plan();
    CettaGsltIndexedInstructionPlanV1 invalid = parser;
    CettaGsltIndexedDecodeResultV1 result;

    expect(cetta_gslt_indexed_instruction_decoder_init_v1(
               &decoder, &proof) == CETTA_GSLT_INDEXED_DECODE_OK_V1,
           "proof DAG plan is admitted");
    expect(cetta_gslt_indexed_instruction_feed_v1(
               &decoder, 85u, &event) == CETTA_GSLT_INDEXED_DECODE_OK_V1 &&
               event.kind == CETTA_GSLT_INDEXED_INSTRUCTION_NONE_V1,
           "proof continuation retains an open index");
    expect(cetta_gslt_indexed_instruction_feed_v1(
               &decoder, 65u, &event) == CETTA_GSLT_INDEXED_DECODE_OK_V1 &&
               event.kind == CETTA_GSLT_INDEXED_INSTRUCTION_USE_V1 &&
               event.index == 20u,
           "proof terminal emits the accumulated index");
    expect(cetta_gslt_indexed_instruction_feed_v1(
               &decoder, 90u, &event) == CETTA_GSLT_INDEXED_DECODE_OK_V1 &&
               event.kind == CETTA_GSLT_INDEXED_INSTRUCTION_SAVE_V1,
           "proof save is decoded structurally");
    expect(cetta_gslt_indexed_instruction_feed_v1(
               &decoder, 63u, &event) == CETTA_GSLT_INDEXED_DECODE_OK_V1 &&
               event.kind == CETTA_GSLT_INDEXED_INSTRUCTION_UNKNOWN_V1,
           "proof unknown is decoded structurally");
    expect(cetta_gslt_indexed_instruction_finish_v1(&decoder) ==
               CETTA_GSLT_INDEXED_DECODE_OK_V1,
           "complete proof stream finishes");

    expect(cetta_gslt_indexed_instruction_decoder_init_v1(
               &decoder, &parser) == CETTA_GSLT_INDEXED_DECODE_OK_V1,
           "structurally different parser plan is admitted");
    expect(cetta_gslt_indexed_instruction_feed_v1(
               &decoder, 11u, &event) == CETTA_GSLT_INDEXED_DECODE_OK_V1 &&
               event.kind == CETTA_GSLT_INDEXED_INSTRUCTION_NONE_V1,
           "parser continuation uses the generated range");
    expect(cetta_gslt_indexed_instruction_feed_v1(
               &decoder, 2u, &event) == CETTA_GSLT_INDEXED_DECODE_OK_V1 &&
               event.kind == CETTA_GSLT_INDEXED_INSTRUCTION_USE_V1 &&
               event.index == 12u,
           "parser terminal uses the generated radix");

    expect(cetta_gslt_indexed_instruction_decoder_init_v1(
               &decoder, &parser) == CETTA_GSLT_INDEXED_DECODE_OK_V1 &&
               cetta_gslt_indexed_instruction_feed_v1(
                   &decoder, 10u, &event) ==
                   CETTA_GSLT_INDEXED_DECODE_OK_V1 &&
               cetta_gslt_indexed_instruction_finish_v1(&decoder) ==
                   CETTA_GSLT_INDEXED_DECODE_OPEN_INDEX_AT_END_V1,
           "zero continuation digit still leaves an open index");
    expect(cetta_gslt_indexed_instruction_feed_v1(
               &decoder, 20u, &event) ==
               CETTA_GSLT_INDEXED_DECODE_SAVE_INSIDE_INDEX_V1,
           "save cannot interrupt an open parser index");

    invalid.continuation_low = 9u;
    expect(cetta_gslt_indexed_instruction_decoder_init_v1(
               &decoder, &invalid) ==
               CETTA_GSLT_INDEXED_DECODE_INVALID_PLAN_V1,
           "overlapping generated classes fail closed");

    expect(cetta_gslt_indexed_instruction_decoder_init_v1(
               &decoder, &parser) == CETTA_GSLT_INDEXED_DECODE_OK_V1,
           "parser plan reinitializes after rejection");
    decoder.accumulator = UINT64_MAX;
    decoder.open_index = true;
    result = cetta_gslt_indexed_instruction_feed_v1(
        &decoder, 11u, &event);
    expect(result == CETTA_GSLT_INDEXED_DECODE_OVERFLOW_V1,
           "numeric carrier rejects modular overflow");
    expect(cetta_gslt_indexed_instruction_feed_v1(
               &decoder, 255u, &event) ==
               CETTA_GSLT_INDEXED_DECODE_INVALID_BYTE_V1,
           "undeclared bytes fail closed");

    printf("GsltIndexedInstructionDecoderV1Summary checks=%u failures=%u\n",
           checks_run, checks_failed);
    return checks_failed == 0u ? 0 : 1;
}
