#include "gslt_chronological_builder_v1.h"

#include <stdio.h>

enum {
    ACTION_CONSTANT_V1 = 11u,
    ACTION_SUM_V1 = 12u,
    ACTION_DOUBLE_V1 = 13u
};

typedef struct {
    uint32_t constant;
    uint32_t calls;
} ActionTableV1;

static bool apply_action_v1(
    void *context,
    uint32_t action,
    const uintptr_t *inputs,
    uint32_t input_len,
    uintptr_t *value_out) {
    ActionTableV1 *table = context;

    if (!table || !value_out || (input_len > 0u && !inputs))
        return false;
    table->calls++;
    if (action == ACTION_CONSTANT_V1 && input_len == 0u) {
        *value_out = table->constant;
        return true;
    }
    if (action == ACTION_SUM_V1 && input_len == 2u) {
        *value_out = inputs[0] + inputs[1];
        return true;
    }
    if (action == ACTION_DOUBLE_V1 && input_len == 1u) {
        *value_out = inputs[0] * 2u;
        return true;
    }
    return false;
}

static bool equal_value_v1(
    void *context, uintptr_t left, uintptr_t right) {
    (void)context;
    return left == right;
}

static void discard_value_v1(void *context, uintptr_t value) {
    (void)context;
    (void)value;
}

static int expect_v1(bool condition, const char *name) {
    if (condition)
        return 0;
    fprintf(stderr, "failed: %s\n", name);
    return 1;
}

int main(void) {
    CettaGsltChronologicalBuilderV1 builder;
    CettaGsltChronologicalReceiptV1 receipt;
    CettaGsltU32IndexV1 selector;
    ActionTableV1 table = { .constant = 9u, .calls = 0u };
    const uintptr_t premises[] = { 5u, 7u };
    const CettaGsltChronologicalRefV1 sum_inputs[] = {
        { CETTA_GSLT_CHRONOLOGICAL_PREMISE_REF_V1, 0u },
        { CETTA_GSLT_CHRONOLOGICAL_PREMISE_REF_V1, 1u }
    };
    const CettaGsltChronologicalRefV1 double_input[] = {
        { CETTA_GSLT_CHRONOLOGICAL_NODE_REF_V1, 100u }
    };
    const CettaGsltChronologicalRefV1 future_input[] = {
        { CETTA_GSLT_CHRONOLOGICAL_NODE_REF_V1, 999u }
    };
    const CettaGsltChronologicalRefV1 bad_premise[] = {
        { CETTA_GSLT_CHRONOLOGICAL_PREMISE_REF_V1, 2u }
    };
    uintptr_t value = 0u;
    uint32_t premise_index = UINT32_MAX;
    uint32_t stable_len;
    uint32_t stable_calls;
    int failed = 0;

    cetta_gslt_chronological_builder_init_v1(&builder);
    cetta_gslt_u32_index_init_v1(&selector);
    failed += expect_v1(
        cetta_gslt_u32_index_insert_unique_v1(
            &selector, 41u, ACTION_SUM_V1) ==
            CETTA_GSLT_U32_INDEX_INSERTED_V1 &&
        cetta_gslt_u32_index_insert_unique_v1(
            &selector, 42u, ACTION_DOUBLE_V1) ==
            CETTA_GSLT_U32_INDEX_INSERTED_V1,
        "exact selector admitted");
    failed += expect_v1(
        cetta_gslt_chronological_builder_begin_v1(
            &builder, premises, 2u, apply_action_v1, equal_value_v1,
            discard_value_v1, &table),
        "begin");
    failed += expect_v1(
        cetta_gslt_chronological_builder_append_selected_v1(
            &builder, &selector, 41u, 100u, sum_inputs, 2u, &value) ==
            CETTA_GSLT_CHRONOLOGICAL_APPENDED_V1 && value == 12u,
        "append sum");
    failed += expect_v1(
        cetta_gslt_chronological_builder_append_selected_v1(
            &builder, &selector, 42u, 101u, double_input, 1u, &value) ==
            CETTA_GSLT_CHRONOLOGICAL_APPENDED_V1 && value == 24u,
        "append double");
    failed += expect_v1(
        cetta_gslt_chronological_builder_finish_v1(
            &builder, 101u, 24u, &receipt) &&
            receipt.node_len == 2u && receipt.root_id == 101u &&
            receipt.root_value == 24u,
        "finish accepted");
    failed += expect_v1(
        !cetta_gslt_chronological_builder_finish_v1(
            &builder, 101u, 25u, &receipt),
        "wrong target rejected");
    failed += expect_v1(
        !cetta_gslt_chronological_builder_finish_v1(
            &builder, 999u, 24u, &receipt),
        "missing root rejected");

    stable_len = builder.node_len;
    stable_calls = table.calls;
    failed += expect_v1(
        cetta_gslt_chronological_builder_append_v1(
            &builder, 102u, ACTION_DOUBLE_V1, bad_premise, 1u, &value) ==
            CETTA_GSLT_CHRONOLOGICAL_UNKNOWN_REFERENCE_V1 &&
            builder.node_len == stable_len && table.calls == stable_calls,
        "future premise rejected transactionally");
    failed += expect_v1(
        cetta_gslt_chronological_builder_append_premise_v1(
            &builder, 11u, &premise_index) && premise_index == 2u &&
            builder.premise_len == 3u,
        "append premise with stable coordinate");
    failed += expect_v1(
        cetta_gslt_chronological_builder_append_v1(
            &builder, 102u, ACTION_DOUBLE_V1, bad_premise, 1u, &value) ==
            CETTA_GSLT_CHRONOLOGICAL_APPENDED_V1 && value == 22u,
        "appended premise is available");

    stable_len = builder.node_len;
    stable_calls = table.calls;
    failed += expect_v1(
        cetta_gslt_chronological_builder_append_selected_v1(
            &builder, &selector, 99u, 102u, NULL, 0u, &value) ==
            CETTA_GSLT_CHRONOLOGICAL_UNKNOWN_ACTION_V1 &&
            builder.node_len == stable_len && table.calls == stable_calls,
        "missing selector rejected transactionally");
    failed += expect_v1(
        cetta_gslt_chronological_builder_append_v1(
            &builder, 100u, ACTION_CONSTANT_V1, NULL, 0u, &value) ==
            CETTA_GSLT_CHRONOLOGICAL_DUPLICATE_V1 &&
            builder.node_len == stable_len && table.calls == stable_calls,
        "duplicate rejected transactionally");
    failed += expect_v1(
        cetta_gslt_chronological_builder_append_v1(
            &builder, 103u, ACTION_DOUBLE_V1, future_input, 1u, &value) ==
            CETTA_GSLT_CHRONOLOGICAL_UNKNOWN_REFERENCE_V1 &&
            builder.node_len == stable_len && table.calls == stable_calls,
        "future reference rejected transactionally");
    failed += expect_v1(
        cetta_gslt_chronological_builder_append_v1(
            &builder, 104u, ACTION_SUM_V1, double_input, 1u, &value) ==
            CETTA_GSLT_CHRONOLOGICAL_ACTION_REJECTED_V1 &&
            builder.node_len == stable_len && table.calls == stable_calls + 1u,
        "invalid action rejected");
    failed += expect_v1(
        cetta_gslt_chronological_builder_validate_v1(&builder),
        "valid after rejection");

    table.constant = 31u;
    failed += expect_v1(
        cetta_gslt_chronological_builder_begin_v1(
            &builder, NULL, 0u, apply_action_v1, equal_value_v1,
            discard_value_v1, &table) &&
            builder.node_len == 0u,
        "reusable begin");
    failed += expect_v1(
        cetta_gslt_chronological_builder_append_v1(
            &builder, 7u, ACTION_CONSTANT_V1, NULL, 0u, &value) ==
            CETTA_GSLT_CHRONOLOGICAL_APPENDED_V1 && value == 31u &&
        cetta_gslt_chronological_builder_finish_v1(
            &builder, 7u, 31u, &receipt),
        "independent constant table");
    failed += expect_v1(
        cetta_gslt_chronological_builder_validate_v1(&builder),
        "final validation");

    cetta_gslt_chronological_builder_free_v1(&builder);
    cetta_gslt_u32_index_free_v1(&selector);
    printf("(GsltChronologicalBuilderV1Summary %u %u %d)\n",
           18u - (uint32_t)failed, 18u, failed);
    return failed == 0 ? 0 : 1;
}
