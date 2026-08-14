#include "gslt_two_phase_frame_machine_v1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
static unsigned failures;

static void expect(bool condition, const char *message) {
    checks++;
    if (!condition) {
        failures++;
        fprintf(stderr, "FAIL: %s\n", message);
    }
}

static bool append_literal(
    CettaGsltLiteralHeadProgramV1 *program,
    uint32_t literal, bool head) {
    return head
               ? cetta_gslt_literal_head_program_set_head_v1(
                     program, literal)
               : cetta_gslt_literal_head_program_append_literal_v1(
                     program, literal, true);
}

static CettaGsltTwoPhaseFrameResultV1 check_disjoint(
    void *context,
    uint32_t left_slot, const uint32_t *left, uint32_t left_len,
    uint32_t right_slot, const uint32_t *right, uint32_t right_len) {
    uint32_t left_index;
    uint32_t right_index;

    (void)context;
    if (left_slot == right_slot ||
        (left_len != 0u && !left) ||
        (right_len != 0u && !right))
        return CETTA_GSLT_TWO_PHASE_FRAME_INVALID_V1;
    for (left_index = 0u; left_index < left_len; left_index++) {
        for (right_index = 0u; right_index < right_len; right_index++) {
            if (left[left_index] == right[right_index])
                return CETTA_GSLT_TWO_PHASE_FRAME_REJECTED_V1;
        }
    }
    return CETTA_GSLT_TWO_PHASE_FRAME_OK_V1;
}

static bool append_formula(
    CettaGsltU32SliceArenaV1 *arena,
    const uint32_t *items, uint32_t len,
    CettaGsltU32SliceV1 *slice) {
    return cetta_gslt_u32_slice_arena_append_v1(
        arena, items, len, slice);
}

int main(void) {
    CettaGsltU32SliceArenaV1 arena = {0};
    CettaGsltEpochSlotsV1 slots = {0};
    CettaGsltTwoPhaseBindV1 binds[] = {
        {0u, 0u, 10u},
        {2u, 1u, 11u},
    };
    CettaGsltTwoPhaseMatchV1 matches[1] = {{0}};
    CettaGsltTwoPhaseApartV1 apart[] = {{0u, 1u}};
    CettaGsltLiteralHeadProgramV1 conclusion = {0};
    CettaGsltTwoPhaseFrameProgramV1 program;
    CettaGsltTwoPhaseFrameAlgebraV1 algebra = {
        .check_apart = check_disjoint,
    };
    CettaGsltU32SliceV1 stack[3];
    CettaGsltU32SliceV1 result = {0};
    const uint32_t *result_items;
    uint32_t base = UINT32_MAX;
    const uint32_t binder_a[] = {10u, 7u, 8u};
    const uint32_t matching[] = {20u, 7u, 8u, 21u, 9u};
    const uint32_t binder_b[] = {11u, 9u};
    const uint32_t expected[] = {30u, 9u, 7u, 8u, 31u};

    expect(append_literal(&matches[0].pattern, 20u, true) &&
               cetta_gslt_literal_head_program_append_hole_v1(
                   &matches[0].pattern, 0u) &&
               append_literal(&matches[0].pattern, 21u, false) &&
               cetta_gslt_literal_head_program_append_hole_v1(
                   &matches[0].pattern, 1u),
           "interleaved matching program constructs");
    matches[0].stack_offset = 1u;
    expect(append_literal(&conclusion, 30u, true) &&
               cetta_gslt_literal_head_program_append_hole_v1(
                   &conclusion, 1u) &&
               cetta_gslt_literal_head_program_append_hole_v1(
                   &conclusion, 0u) &&
               append_literal(&conclusion, 31u, false),
           "conclusion program constructs");
    program = (CettaGsltTwoPhaseFrameProgramV1){
        .binds = binds,
        .bind_len = 2u,
        .matches = matches,
        .match_len = 1u,
        .apart = apart,
        .apart_len = 1u,
        .conclusion = &conclusion,
        .stack_arity = 3u,
        .slot_len = 2u,
    };
    expect(cetta_gslt_two_phase_frame_program_validate_v1(&program),
           "interleaved frame validates");
    expect(append_formula(&arena, binder_a, 3u, &stack[0]) &&
               append_formula(&arena, matching, 5u, &stack[1]) &&
               append_formula(&arena, binder_b, 2u, &stack[2]),
           "interleaved input formulas append");
    expect(cetta_gslt_epoch_slots_prepare_v1(
               &slots, 2u, sizeof(CettaGsltU32SliceV1)),
           "dense slots prepare");
    expect(cetta_gslt_two_phase_frame_machine_execute_prepared_v1(
               &program, &algebra, &slots, &arena, stack, 3u,
               &base, &result) == CETTA_GSLT_TWO_PHASE_FRAME_OK_V1,
           "interleaved frame executes");
    result_items = cetta_gslt_u32_slice_arena_items_v1(&arena, result);
    expect(base == 0u && result.len == 5u && result_items &&
               memcmp(result_items, expected, sizeof(expected)) == 0,
           "interleaved frame returns exact conclusion");

    {
        CettaGsltTwoPhaseFrameAdmissionV1 admitted = {0};
        CettaGsltTwoPhaseFrameAdmissionV1 moved;

        expect(cetta_gslt_two_phase_frame_program_admit_v1(
                   &program, &admitted),
               "interleaved frame admits once");
        moved = admitted;
        memset(&admitted, 0, sizeof(admitted));
        expect(cetta_gslt_epoch_slots_prepare_v1(
                   &slots, 2u, sizeof(CettaGsltU32SliceV1)),
               "dense slots advance for moved admission");
        expect(cetta_gslt_two_phase_frame_machine_execute_admitted_v1(
                   &moved, &algebra, &slots, &arena, stack, 3u,
                   &base, &result) == CETTA_GSLT_TWO_PHASE_FRAME_OK_V1,
               "relocated admission executes without self pointers");
        result_items = cetta_gslt_u32_slice_arena_items_v1(&arena, result);
        expect(base == 0u && result.len == 5u && result_items &&
                   memcmp(result_items, expected, sizeof(expected)) == 0,
               "relocated admission preserves exact conclusion");
    }

    {
        CettaGsltU32SliceV1 extended[4] = {
            stack[0], stack[0], stack[1], stack[2],
        };
        expect(cetta_gslt_epoch_slots_prepare_v1(
                   &slots, 2u, sizeof(CettaGsltU32SliceV1)),
               "dense slots advance for a prefixed stack");
        expect(cetta_gslt_two_phase_frame_machine_execute_prepared_v1(
                   &program, &algebra, &slots, &arena, extended, 4u,
                   &base, &result) == CETTA_GSLT_TWO_PHASE_FRAME_OK_V1 &&
                   base == 1u,
               "frame consumes only its declared stack suffix");
    }

    {
        CettaGsltTwoPhaseFrameAlgebraV1 absent = {0};
        expect(cetta_gslt_epoch_slots_prepare_v1(
                   &slots, 2u, sizeof(CettaGsltU32SliceV1)),
               "dense slots advance for missing apartness callback");
        expect(cetta_gslt_two_phase_frame_machine_execute_prepared_v1(
                   &program, &absent, &slots, &arena, stack, 3u,
                   &base, &result) == CETTA_GSLT_TWO_PHASE_FRAME_INVALID_V1,
               "required apartness callback fails closed");
    }

    {
        CettaGsltTwoPhaseBindV1 bad_binds[] = {
            {0u, 0u, 10u}, {2u, 0u, 11u},
        };
        CettaGsltTwoPhaseFrameProgramV1 malformed = program;
        malformed.binds = bad_binds;
        expect(!cetta_gslt_two_phase_frame_program_validate_v1(&malformed),
               "duplicate dense slot is rejected");
        bad_binds[1].slot = 1u;
        bad_binds[1].stack_offset = 1u;
        expect(!cetta_gslt_two_phase_frame_program_validate_v1(&malformed),
               "duplicate stack coordinate is rejected");
    }

    {
        CettaGsltTwoPhaseApartV1 repeated[] = {
            {0u, 1u}, {0u, 1u},
        };
        CettaGsltTwoPhaseApartV1 reflexive[] = {{0u, 0u}};
        CettaGsltTwoPhaseFrameProgramV1 repeated_program = program;
        CettaGsltTwoPhaseFrameProgramV1 malformed = program;

        repeated_program.apart = repeated;
        repeated_program.apart_len = 2u;
        expect(cetta_gslt_two_phase_frame_program_validate_v1(
                   &repeated_program),
               "repeated apartness obligations preserve source order");
        malformed.apart = reflexive;
        expect(!cetta_gslt_two_phase_frame_program_validate_v1(&malformed),
               "reflexive apartness obligation is rejected");
    }

    {
        CettaGsltTwoPhaseBindV1 bind = {0u, 0u, 41u};
        CettaGsltTwoPhaseMatchV1 match = {0};
        CettaGsltLiteralHeadProgramV1 output = {0};
        CettaGsltTwoPhaseFrameProgramV1 second;
        CettaGsltU32SliceV1 second_stack[2];
        const uint32_t first[] = {41u, 5u};
        const uint32_t second_input[] = {50u, 5u};
        const uint32_t second_expected[] = {60u, 5u};

        match.stack_offset = 1u;
        expect(append_literal(&match.pattern, 50u, true) &&
                   cetta_gslt_literal_head_program_append_hole_v1(
                       &match.pattern, 0u) &&
                   append_literal(&output, 60u, true) &&
                   cetta_gslt_literal_head_program_append_hole_v1(
                       &output, 0u),
               "second frame shape constructs");
        second = (CettaGsltTwoPhaseFrameProgramV1){
            .binds = &bind,
            .bind_len = 1u,
            .matches = &match,
            .match_len = 1u,
            .conclusion = &output,
            .stack_arity = 2u,
            .slot_len = 1u,
        };
        expect(cetta_gslt_two_phase_frame_program_validate_v1(&second),
               "different no-apartness frame validates");
        expect(append_formula(&arena, first, 2u, &second_stack[0]) &&
                   append_formula(
                       &arena, second_input, 2u, &second_stack[1]) &&
                   cetta_gslt_epoch_slots_prepare_v1(
                       &slots, 1u, sizeof(CettaGsltU32SliceV1)),
               "second frame input and slots prepare");
        expect(cetta_gslt_two_phase_frame_machine_execute_prepared_v1(
                   &second, NULL, &slots, &arena, second_stack, 2u,
                   &base, &result) == CETTA_GSLT_TWO_PHASE_FRAME_OK_V1,
               "different no-apartness frame executes");
        result_items = cetta_gslt_u32_slice_arena_items_v1(&arena, result);
        expect(result.len == 2u && result_items &&
                   memcmp(result_items, second_expected,
                          sizeof(second_expected)) == 0,
               "different frame returns exact conclusion");
        second_stack[1] = second_stack[0];
        expect(cetta_gslt_epoch_slots_prepare_v1(
                   &slots, 1u, sizeof(CettaGsltU32SliceV1)),
               "dense slots advance for mismatch");
        expect(cetta_gslt_two_phase_frame_machine_execute_prepared_v1(
                   &second, NULL, &slots, &arena, second_stack, 2u,
                   &base, &result) == CETTA_GSLT_TWO_PHASE_FRAME_REJECTED_V1,
               "mismatching input is rejected");
        cetta_gslt_literal_head_program_free_v1(&match.pattern);
        cetta_gslt_literal_head_program_free_v1(&output);
    }

    expect(cetta_gslt_epoch_slots_prepare_v1(
               &slots, 2u, sizeof(CettaGsltU32SliceV1)),
           "dense slots advance for underflow");
    expect(cetta_gslt_two_phase_frame_machine_execute_prepared_v1(
               &program, &algebra, &slots, &arena, stack, 2u,
               &base, &result) == CETTA_GSLT_TWO_PHASE_FRAME_REJECTED_V1,
           "stack underflow is rejected");

    cetta_gslt_literal_head_program_free_v1(&matches[0].pattern);
    cetta_gslt_literal_head_program_free_v1(&conclusion);
    cetta_gslt_epoch_slots_free_v1(&slots);
    cetta_gslt_u32_slice_arena_free_v1(&arena);
    printf("(GsltTwoPhaseFrameMachineV1Summary %u %u %u)\n",
           checks, checks - failures, failures);
    return failures == 0u ? EXIT_SUCCESS : EXIT_FAILURE;
}
