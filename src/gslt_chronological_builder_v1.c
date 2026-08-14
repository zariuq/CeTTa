#include "gslt_chronological_builder_v1.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static bool cetta_gslt_chronological_reserve_value_v1(
    uintptr_t **values, uint32_t *cap, uint32_t required) {
    uint32_t next_cap;
    uintptr_t *grown;

    if (!values || !cap)
        return false;
    if (required <= *cap)
        return true;
    next_cap = *cap == 0u ? 8u : *cap;
    while (next_cap < required) {
        if (next_cap > UINT32_MAX / 2u)
            return false;
        next_cap *= 2u;
    }
    if ((size_t)next_cap > SIZE_MAX / sizeof(*grown))
        return false;
    grown = realloc(*values, (size_t)next_cap * sizeof(*grown));
    if (!grown)
        return false;
    *values = grown;
    *cap = next_cap;
    return true;
}

static bool cetta_gslt_chronological_reserve_u32_v1(
    uint32_t **values, uint32_t *cap, uint32_t required) {
    uint32_t next_cap;
    uint32_t *grown;

    if (!values || !cap)
        return false;
    if (required <= *cap)
        return true;
    next_cap = *cap == 0u ? 8u : *cap;
    while (next_cap < required) {
        if (next_cap > UINT32_MAX / 2u)
            return false;
        next_cap *= 2u;
    }
    if ((size_t)next_cap > SIZE_MAX / sizeof(*grown))
        return false;
    grown = realloc(*values, (size_t)next_cap * sizeof(*grown));
    if (!grown)
        return false;
    *values = grown;
    *cap = next_cap;
    return true;
}

void cetta_gslt_chronological_builder_init_v1(
    CettaGsltChronologicalBuilderV1 *builder) {
    if (!builder)
        return;
    memset(builder, 0, sizeof(*builder));
    cetta_gslt_u32_index_init_v1(&builder->node_index);
}

void cetta_gslt_chronological_builder_free_v1(
    CettaGsltChronologicalBuilderV1 *builder) {
    uint32_t index;

    if (!builder)
        return;
    if (builder->discard) {
        for (index = 0u; index < builder->node_len; index++)
            builder->discard(
                builder->apply_context, builder->node_values[index]);
    }
    cetta_gslt_u32_index_free_v1(&builder->node_index);
    free(builder->premise_values);
    free(builder->node_ids);
    free(builder->node_values);
    free(builder->input_scratch);
    cetta_gslt_chronological_builder_init_v1(builder);
}

bool cetta_gslt_chronological_builder_begin_v1(
    CettaGsltChronologicalBuilderV1 *builder,
    const uintptr_t *premise_values,
    uint32_t premise_len,
    CettaGsltChronologicalApplyV1 apply,
    CettaGsltChronologicalEqualV1 equal,
    CettaGsltChronologicalDiscardV1 discard,
    void *apply_context) {
    uint32_t index;

    if (!builder || !apply || !equal || !discard ||
        (premise_len > 0u && !premise_values) ||
        !cetta_gslt_u32_index_shape_valid_v1(&builder->node_index) ||
        !cetta_gslt_chronological_reserve_value_v1(
            &builder->premise_values, &builder->premise_cap, premise_len))
        return false;
    if (builder->discard) {
        for (index = 0u; index < builder->node_len; index++)
            builder->discard(
                builder->apply_context, builder->node_values[index]);
    }
    if (premise_len > 0u)
        memcpy(builder->premise_values, premise_values,
               (size_t)premise_len * sizeof(*premise_values));
    cetta_gslt_u32_index_reset_v1(&builder->node_index);
    builder->premise_len = premise_len;
    builder->node_len = 0u;
    builder->apply = apply;
    builder->equal = equal;
    builder->discard = discard;
    builder->apply_context = apply_context;
    builder->begun = true;
    return true;
}

bool cetta_gslt_chronological_builder_append_premise_v1(
    CettaGsltChronologicalBuilderV1 *builder,
    uintptr_t premise_value,
    uint32_t *premise_index_out) {
    uint32_t next_len;

    if (!builder || !builder->begun || !premise_index_out ||
        builder->premise_len == UINT32_MAX)
        return false;
    next_len = builder->premise_len + 1u;
    if (!cetta_gslt_chronological_reserve_value_v1(
            &builder->premise_values, &builder->premise_cap, next_len))
        return false;
    *premise_index_out = builder->premise_len;
    builder->premise_values[builder->premise_len] = premise_value;
    builder->premise_len = next_len;
    return true;
}

static bool cetta_gslt_chronological_resolve_v1(
    const CettaGsltChronologicalBuilderV1 *builder,
    const CettaGsltChronologicalRefV1 *ref,
    uintptr_t *value_out) {
    uint32_t entry;

    if (!builder || !ref || !value_out)
        return false;
    switch (ref->kind) {
    case CETTA_GSLT_CHRONOLOGICAL_PREMISE_REF_V1:
        if (ref->value >= builder->premise_len)
            return false;
        *value_out = builder->premise_values[ref->value];
        return true;
    case CETTA_GSLT_CHRONOLOGICAL_NODE_REF_V1:
        if (!cetta_gslt_u32_index_find_v1(
                &builder->node_index, ref->value, &entry) ||
            entry >= builder->node_len)
            return false;
        *value_out = builder->node_values[entry];
        return true;
    default:
        return false;
    }
}

CettaGsltChronologicalAppendResultV1
cetta_gslt_chronological_builder_append_v1(
    CettaGsltChronologicalBuilderV1 *builder,
    uint32_t node_id,
    uint32_t action,
    const CettaGsltChronologicalRefV1 *inputs,
    uint32_t input_len,
    uintptr_t *value_out) {
    uint32_t existing;
    uint32_t i;
    uintptr_t next_value;
    CettaGsltU32IndexInsertResultV1 inserted;

    if (!builder || !builder->begun || !builder->apply || !value_out ||
        (input_len > 0u && !inputs) || builder->node_len == UINT32_MAX ||
        !cetta_gslt_u32_index_shape_valid_v1(&builder->node_index))
        return CETTA_GSLT_CHRONOLOGICAL_INVALID_V1;
    if (cetta_gslt_u32_index_find_v1(
            &builder->node_index, node_id, &existing))
        return CETTA_GSLT_CHRONOLOGICAL_DUPLICATE_V1;
    if (!cetta_gslt_chronological_reserve_u32_v1(
            &builder->node_ids, &builder->node_id_cap,
            builder->node_len + 1u))
        return CETTA_GSLT_CHRONOLOGICAL_RESOURCE_V1;
    if (!cetta_gslt_chronological_reserve_value_v1(
            &builder->node_values, &builder->node_value_cap,
            builder->node_len + 1u))
        return CETTA_GSLT_CHRONOLOGICAL_RESOURCE_V1;
    if (!cetta_gslt_chronological_reserve_value_v1(
            &builder->input_scratch, &builder->input_cap, input_len))
        return CETTA_GSLT_CHRONOLOGICAL_RESOURCE_V1;
    for (i = 0u; i < input_len; i++) {
        if (!cetta_gslt_chronological_resolve_v1(
                builder, &inputs[i], &builder->input_scratch[i]))
            return CETTA_GSLT_CHRONOLOGICAL_UNKNOWN_REFERENCE_V1;
    }
    if (!builder->apply(builder->apply_context, action,
                        builder->input_scratch, input_len, &next_value))
        return CETTA_GSLT_CHRONOLOGICAL_ACTION_REJECTED_V1;
    inserted = cetta_gslt_u32_index_insert_unique_v1(
        &builder->node_index, node_id, builder->node_len);
    if (inserted == CETTA_GSLT_U32_INDEX_DUPLICATE_V1) {
        builder->discard(builder->apply_context, next_value);
        return CETTA_GSLT_CHRONOLOGICAL_DUPLICATE_V1;
    }
    if (inserted == CETTA_GSLT_U32_INDEX_RESOURCE_V1) {
        builder->discard(builder->apply_context, next_value);
        return CETTA_GSLT_CHRONOLOGICAL_RESOURCE_V1;
    }
    if (inserted != CETTA_GSLT_U32_INDEX_INSERTED_V1) {
        builder->discard(builder->apply_context, next_value);
        return CETTA_GSLT_CHRONOLOGICAL_INVALID_V1;
    }
    builder->node_ids[builder->node_len] = node_id;
    builder->node_values[builder->node_len] = next_value;
    builder->node_len++;
    *value_out = next_value;
    return CETTA_GSLT_CHRONOLOGICAL_APPENDED_V1;
}

CettaGsltChronologicalAppendResultV1
cetta_gslt_chronological_builder_append_selected_v1(
    CettaGsltChronologicalBuilderV1 *builder,
    const CettaGsltU32IndexV1 *selector,
    uint32_t selector_key,
    uint32_t node_id,
    const CettaGsltChronologicalRefV1 *inputs,
    uint32_t input_len,
    uintptr_t *value_out) {
    uint32_t action;

    if (!cetta_gslt_u32_index_find_v1(selector, selector_key, &action))
        return CETTA_GSLT_CHRONOLOGICAL_UNKNOWN_ACTION_V1;
    return cetta_gslt_chronological_builder_append_v1(
        builder, node_id, action, inputs, input_len, value_out);
}

bool cetta_gslt_chronological_builder_finish_v1(
    const CettaGsltChronologicalBuilderV1 *builder,
    uint32_t root_id,
    uintptr_t target_value,
    CettaGsltChronologicalReceiptV1 *receipt_out) {
    uint32_t entry;

    if (!builder || !builder->begun || !builder->equal || !receipt_out ||
        !cetta_gslt_u32_index_find_v1(
            &builder->node_index, root_id, &entry) ||
        entry >= builder->node_len ||
        !builder->equal(
            builder->apply_context,
            builder->node_values[entry], target_value))
        return false;
    receipt_out->node_len = builder->node_len;
    receipt_out->root_id = root_id;
    receipt_out->root_value = target_value;
    return true;
}

bool cetta_gslt_chronological_builder_validate_v1(
    const CettaGsltChronologicalBuilderV1 *builder) {
    uint32_t i;
    uint32_t entry;

    if (!builder || !cetta_gslt_u32_index_validate_v1(&builder->node_index) ||
        builder->node_len != builder->node_index.len ||
        builder->node_len > builder->node_id_cap ||
        builder->node_len > builder->node_value_cap ||
        builder->premise_len > builder->premise_cap ||
        (builder->node_id_cap > 0u && !builder->node_ids) ||
        (builder->node_value_cap > 0u && !builder->node_values) ||
        (builder->premise_cap > 0u && !builder->premise_values) ||
        (builder->input_cap > 0u && !builder->input_scratch))
        return false;
    for (i = 0u; i < builder->node_len; i++) {
        if (!cetta_gslt_u32_index_find_v1(
                &builder->node_index, builder->node_ids[i], &entry) ||
            entry != i)
            return false;
    }
    return true;
}
