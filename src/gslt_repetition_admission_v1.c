#include "gslt_repetition_admission_v1.h"

#include <string.h>

static bool cetta_gslt_repetition_u64_add_v1(
    uint64_t left, uint64_t right, uint64_t *result_out) {
    if (!result_out || left > UINT64_MAX - right)
        return false;
    *result_out = left + right;
    return true;
}

static bool cetta_gslt_repetition_u64_mul_v1(
    uint64_t left, uint64_t right, uint64_t *result_out) {
    if (!result_out || (left != 0u && right > UINT64_MAX / left))
        return false;
    *result_out = left * right;
    return true;
}

void cetta_gslt_repetition_admission_init_v1(
    CettaGsltRepetitionAdmissionV1 *admission) {
    if (!admission)
        return;
    memset(admission, 0, sizeof(*admission));
    cetta_gslt_u32_index_init_v1(&admission->seen);
    cetta_gslt_u32_index_init_v1(&admission->promoted);
}

void cetta_gslt_repetition_admission_free_v1(
    CettaGsltRepetitionAdmissionV1 *admission) {
    if (!admission)
        return;
    cetta_gslt_u32_index_free_v1(&admission->promoted);
    cetta_gslt_u32_index_free_v1(&admission->seen);
    memset(admission, 0, sizeof(*admission));
}

void cetta_gslt_repetition_admission_reset_v1(
    CettaGsltRepetitionAdmissionV1 *admission) {
    if (!admission)
        return;
    cetta_gslt_u32_index_reset_v1(&admission->promoted);
    cetta_gslt_u32_index_reset_v1(&admission->seen);
}

static CettaGsltRepetitionResultV1
cetta_gslt_repetition_from_insert_v1(
    CettaGsltU32IndexInsertResultV1 inserted) {
    if (inserted == CETTA_GSLT_U32_INDEX_INSERTED_V1)
        return CETTA_GSLT_REPETITION_OK_V1;
    if (inserted == CETTA_GSLT_U32_INDEX_RESOURCE_V1)
        return CETTA_GSLT_REPETITION_RESOURCE_V1;
    return CETTA_GSLT_REPETITION_INVALID_V1;
}

CettaGsltRepetitionResultV1 cetta_gslt_repetition_admission_classify_v1(
    CettaGsltRepetitionAdmissionV1 *admission,
    uint32_t key,
    CettaGsltRepetitionDecisionV1 *decision_out,
    uint32_t *slot_out) {
    uint32_t ignored;

    if (!admission || !decision_out || !slot_out ||
        !cetta_gslt_u32_index_shape_valid_v1(&admission->seen) ||
        !cetta_gslt_u32_index_shape_valid_v1(&admission->promoted))
        return CETTA_GSLT_REPETITION_INVALID_V1;
    if (cetta_gslt_u32_index_find_v1(
            &admission->promoted, key, slot_out)) {
        *decision_out = CETTA_GSLT_REPETITION_HIT_V1;
        return CETTA_GSLT_REPETITION_OK_V1;
    }
    *slot_out = 0u;
    if (cetta_gslt_u32_index_find_v1(&admission->seen, key, &ignored)) {
        *decision_out = CETTA_GSLT_REPETITION_PROMOTE_V1;
        return CETTA_GSLT_REPETITION_OK_V1;
    }
    {
        CettaGsltRepetitionResultV1 inserted =
            cetta_gslt_repetition_from_insert_v1(
                cetta_gslt_u32_index_insert_unique_v1(
                    &admission->seen, key, 1u));
        if (inserted != CETTA_GSLT_REPETITION_OK_V1)
            return inserted;
    }
    *decision_out = CETTA_GSLT_REPETITION_FIRST_V1;
    return CETTA_GSLT_REPETITION_OK_V1;
}

CettaGsltRepetitionResultV1 cetta_gslt_repetition_admission_promote_v1(
    CettaGsltRepetitionAdmissionV1 *admission,
    uint32_t key,
    uint32_t slot) {
    uint32_t ignored;

    if (!admission ||
        !cetta_gslt_u32_index_shape_valid_v1(&admission->seen) ||
        !cetta_gslt_u32_index_shape_valid_v1(&admission->promoted) ||
        !cetta_gslt_u32_index_find_v1(&admission->seen, key, &ignored) ||
        cetta_gslt_u32_index_find_v1(
            &admission->promoted, key, &ignored))
        return CETTA_GSLT_REPETITION_INVALID_V1;
    return cetta_gslt_repetition_from_insert_v1(
        cetta_gslt_u32_index_insert_unique_v1(
            &admission->promoted, key, slot));
}

bool cetta_gslt_repetition_admission_validate_v1(
    const CettaGsltRepetitionAdmissionV1 *admission) {
    uint32_t slot;

    if (!admission ||
        !cetta_gslt_u32_index_validate_v1(&admission->seen) ||
        !cetta_gslt_u32_index_validate_v1(&admission->promoted))
        return false;
    for (slot = 0u; slot < admission->promoted.cap; slot++) {
        uint32_t ignored;
        if (!admission->promoted.occupied[slot])
            continue;
        if (!cetta_gslt_u32_index_find_v1(
                &admission->seen,
                admission->promoted.keys[slot], &ignored))
            return false;
    }
    return true;
}

CettaGsltRepetitionCostQualificationV1
cetta_gslt_repetition_cost_qualify_v1(
    const CettaGsltRepetitionCostModelV1 *model,
    uint64_t occurrences,
    uint64_t source_lookups,
    uint64_t cache_hits,
    uint64_t promotions,
    uint64_t *cached_cost_out,
    uint64_t *fresh_cost_out) {
    uint64_t accounted;
    uint64_t retained;
    uint64_t classification_cost;
    uint64_t source_cost;
    uint64_t hit_cost;
    uint64_t promotion_cost;
    uint64_t cached_cost;
    uint64_t fresh_cost;

    if (!model || !cached_cost_out || !fresh_cost_out ||
        promotions > source_lookups ||
        !cetta_gslt_repetition_u64_add_v1(
            source_lookups, cache_hits, &accounted) ||
        accounted != occurrences ||
        !cetta_gslt_repetition_u64_add_v1(
            model->promotion, model->retained_value, &retained) ||
        !cetta_gslt_repetition_u64_mul_v1(
            occurrences, model->classification, &classification_cost) ||
        !cetta_gslt_repetition_u64_mul_v1(
            source_lookups, model->source_lookup, &source_cost) ||
        !cetta_gslt_repetition_u64_mul_v1(
            cache_hits, model->cache_hit, &hit_cost) ||
        !cetta_gslt_repetition_u64_mul_v1(
            promotions, retained, &promotion_cost) ||
        !cetta_gslt_repetition_u64_add_v1(
            classification_cost, source_cost, &cached_cost) ||
        !cetta_gslt_repetition_u64_add_v1(
            cached_cost, hit_cost, &cached_cost) ||
        !cetta_gslt_repetition_u64_add_v1(
            cached_cost, promotion_cost, &cached_cost) ||
        !cetta_gslt_repetition_u64_mul_v1(
            occurrences, model->source_lookup, &fresh_cost))
        return CETTA_GSLT_REPETITION_COST_INVALID_V1;

    *cached_cost_out = cached_cost;
    *fresh_cost_out = fresh_cost;
    if (cached_cost < fresh_cost && cache_hits != 0u)
        return CETTA_GSLT_REPETITION_COST_PROFITABLE_V1;
    if (cached_cost == fresh_cost)
        return CETTA_GSLT_REPETITION_COST_NONREGRESSING_V1;
    return CETTA_GSLT_REPETITION_COST_REJECTED_V1;
}
