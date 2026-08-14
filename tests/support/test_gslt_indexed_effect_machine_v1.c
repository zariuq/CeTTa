#include "gslt_indexed_effect_machine_v1.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t prepared[3];
    uint32_t saved[8];
    uint32_t saved_len;
    uint32_t stack[16];
    uint32_t stack_len;
    bool reject_unknown;
} TestAlgebra;

static uint32_t failures;
static uint32_t checks;

static void expect(bool condition, const char *message) {
    checks++;
    if (!condition) {
        failures++;
        fprintf(stderr, "FAIL: %s\n", message);
    }
}

static CettaGsltIndexedEffectResultV1 test_view(
    void *context, CettaGsltSplitIndexedTableV1 *table_out) {
    TestAlgebra *state = context;
    *table_out = (CettaGsltSplitIndexedTableV1){
        .prepared = state->prepared,
        .prepared_len = 3u,
        .prepared_stride = sizeof(state->prepared[0]),
        .saved = state->saved,
        .saved_len = state->saved_len,
        .saved_stride = sizeof(state->saved[0]),
    };
    return CETTA_GSLT_INDEXED_EFFECT_OK_V1;
}

static CettaGsltIndexedEffectResultV1 test_push(
    void *context, const void *value) {
    TestAlgebra *state = context;
    if (state->stack_len == 16u)
        return CETTA_GSLT_INDEXED_EFFECT_RESOURCE_V1;
    state->stack[state->stack_len++] = *(const uint32_t *)value;
    return CETTA_GSLT_INDEXED_EFFECT_OK_V1;
}

static CettaGsltIndexedEffectResultV1 test_save(void *context) {
    TestAlgebra *state = context;
    if (state->stack_len == 0u)
        return CETTA_GSLT_INDEXED_EFFECT_REJECTED_V1;
    if (state->saved_len == 8u)
        return CETTA_GSLT_INDEXED_EFFECT_RESOURCE_V1;
    state->saved[state->saved_len++] = state->stack[state->stack_len - 1u];
    return CETTA_GSLT_INDEXED_EFFECT_OK_V1;
}

static CettaGsltIndexedEffectResultV1 test_unknown(void *context) {
    TestAlgebra *state = context;
    uint32_t value = UINT32_MAX;
    if (state->reject_unknown)
        return CETTA_GSLT_INDEXED_EFFECT_REJECTED_V1;
    return test_push(context, &value);
}

static CettaGsltIndexedEffectAlgebraV1 algebra(TestAlgebra *state) {
    return (CettaGsltIndexedEffectAlgebraV1){
        .context = state,
        .table_view = test_view,
        .use_prepared = test_push,
        .use_saved = test_push,
        .save_top = test_save,
        .use_unknown = test_unknown,
    };
}

int main(void) {
    const CettaGsltIndexedInstructionPlanV1 first_plan = {
        .terminal_low = 65u, .terminal_high = 84u,
        .continuation_low = 85u, .continuation_high = 89u,
        .save_byte = 90u, .unknown_byte = 63u,
        .terminal_radix = 20u, .terminal_digit_bias = 0u,
        .continuation_radix = 5u, .continuation_digit_bias = 1u,
        .save_placement =
            CETTA_GSLT_INDEXED_SAVE_IMMEDIATELY_AFTER_USE_V1,
    };
    const CettaGsltIndexedInstructionPlanV1 second_plan = {
        .terminal_low = 0u, .terminal_high = 9u,
        .continuation_low = 10u, .continuation_high = 19u,
        .save_byte = 20u, .unknown_byte = 21u,
        .terminal_radix = 10u, .terminal_digit_bias = 0u,
        .continuation_radix = 10u, .continuation_digit_bias = 0u,
        .save_placement =
            CETTA_GSLT_INDEXED_SAVE_IMMEDIATELY_AFTER_USE_V1,
    };
    TestAlgebra state = {.prepared = {11u, 22u, 33u}};
    CettaGsltIndexedEffectAlgebraV1 effects = algebra(&state);
    CettaGsltIndexedEffectMachineV1 machine;
    const uint8_t first[] = {65u, 90u, 68u, 63u};
    const uint8_t second[] = {0u, 20u, 3u};

    expect(cetta_gslt_indexed_effect_machine_init_v1(
               &machine, &first_plan, &effects) ==
               CETTA_GSLT_INDEXED_EFFECT_MACHINE_OK_V1,
           "first algebra is admitted");
    expect(cetta_gslt_indexed_effect_machine_execute_v1(
               &machine, first, sizeof(first)) ==
               CETTA_GSLT_INDEXED_EFFECT_MACHINE_OK_V1,
           "first instruction family executes");
    expect(cetta_gslt_indexed_effect_machine_finish_v1(&machine) ==
               CETTA_GSLT_INDEXED_EFFECT_MACHINE_OK_V1,
           "first instruction family finishes");
    expect(state.stack_len == 3u && state.stack[0] == 11u &&
               state.stack[1] == 11u && state.stack[2] == UINT32_MAX,
           "prepared, saved, and unknown effects retain order");
    expect(machine.value_instruction_len == 3u && state.saved_len == 1u,
           "value and saved counts are distinct");

    memset(&state, 0, sizeof(state));
    state.prepared[0] = 7u;
    state.prepared[1] = 8u;
    state.prepared[2] = 9u;
    effects = algebra(&state);
    expect(cetta_gslt_indexed_effect_machine_init_v1(
               &machine, &second_plan, &effects) ==
               CETTA_GSLT_INDEXED_EFFECT_MACHINE_OK_V1,
           "structurally different alphabet is admitted");
    expect(cetta_gslt_indexed_effect_machine_execute_v1(
               &machine, second, sizeof(second)) ==
               CETTA_GSLT_INDEXED_EFFECT_MACHINE_OK_V1 &&
               state.stack_len == 2u && state.stack[1] == 7u,
           "second alphabet uses its dynamically grown suffix");

    memset(&state, 0, sizeof(state));
    state.reject_unknown = true;
    effects = algebra(&state);
    expect(cetta_gslt_indexed_effect_machine_init_v1(
               &machine, &second_plan, &effects) ==
               CETTA_GSLT_INDEXED_EFFECT_MACHINE_OK_V1,
           "rejecting algebra is admitted");
    expect(cetta_gslt_indexed_effect_machine_execute_v1(
               &machine, &second_plan.unknown_byte, 1u) ==
               CETTA_GSLT_INDEXED_EFFECT_MACHINE_REJECTED_V1,
           "unknown policy fails closed through the algebra");

    memset(&state, 0, sizeof(state));
    effects = algebra(&state);
    expect(cetta_gslt_indexed_effect_machine_init_v1(
               &machine, &second_plan, &effects) ==
               CETTA_GSLT_INDEXED_EFFECT_MACHINE_OK_V1,
           "empty-save negative machine initializes");
    expect(cetta_gslt_indexed_effect_machine_execute_v1(
               &machine, &second_plan.save_byte, 1u) ==
               CETTA_GSLT_INDEXED_EFFECT_MACHINE_DECODE_V1 &&
               machine.decode_failure ==
                   CETTA_GSLT_INDEXED_DECODE_SAVE_WITHOUT_USE_V1,
           "saving without a completed use is rejected by the grammar");

    memset(&state, 0, sizeof(state));
    state.prepared[0] = 7u;
    effects = algebra(&state);
    expect(cetta_gslt_indexed_effect_machine_init_v1(
               &machine, &second_plan, &effects) ==
               CETTA_GSLT_INDEXED_EFFECT_MACHINE_OK_V1,
           "resource-boundary machine initializes");
    machine.value_instruction_len = UINT64_MAX;
    expect(cetta_gslt_indexed_effect_machine_execute_v1(
               &machine, &second_plan.terminal_low, 1u) ==
                   CETTA_GSLT_INDEXED_EFFECT_MACHINE_RESOURCE_V1 &&
               state.stack_len == 0u,
           "instruction-count exhaustion occurs before its effect");

    printf("(GsltIndexedEffectMachineV1Summary %u %u %u)\n",
           checks, checks - failures, failures);
    return failures == 0u ? 0 : 1;
}
