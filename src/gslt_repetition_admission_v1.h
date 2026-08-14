#ifndef CETTA_GSLT_REPETITION_ADMISSION_V1_H
#define CETTA_GSLT_REPETITION_ADMISSION_V1_H

#include "gslt_u32_index_v1.h"

typedef enum {
    CETTA_GSLT_REPETITION_FIRST_V1 = 0,
    CETTA_GSLT_REPETITION_PROMOTE_V1 = 1,
    CETTA_GSLT_REPETITION_HIT_V1 = 2
} CettaGsltRepetitionDecisionV1;

typedef enum {
    CETTA_GSLT_REPETITION_OK_V1 = 0,
    CETTA_GSLT_REPETITION_RESOURCE_V1 = 1,
    CETTA_GSLT_REPETITION_INVALID_V1 = 2
} CettaGsltRepetitionResultV1;

/* Closed generic policies selectable by a generated plan. */
typedef enum {
    CETTA_GSLT_REPETITION_POLICY_NONE_V1 = 0,
    CETTA_GSLT_REPETITION_POLICY_SECOND_OCCURRENCE_V1 = 1
} CettaGsltRepetitionPolicyV1;

/* Platform-calibrated symbolic costs for the generic repetition policy.  The
 * weights are supplied by a generated or measured plan; this module does not
 * choose them. */
typedef struct {
    uint64_t classification;
    uint64_t source_lookup;
    uint64_t cache_hit;
    uint64_t promotion;
    uint64_t retained_value;
} CettaGsltRepetitionCostModelV1;

typedef enum {
    CETTA_GSLT_REPETITION_COST_INVALID_V1 = 0,
    CETTA_GSLT_REPETITION_COST_REJECTED_V1 = 1,
    CETTA_GSLT_REPETITION_COST_NONREGRESSING_V1 = 2,
    CETTA_GSLT_REPETITION_COST_PROFITABLE_V1 = 3
} CettaGsltRepetitionCostQualificationV1;

/* A vocabulary-neutral repetition automaton.  First occurrence records a
 * key, second occurrence requests promotion, and later occurrences return
 * the caller-supplied promoted slot.  Values, lifetimes, and exact source
 * snapshot validation remain the caller's responsibility; append-only source
 * storage alone does not license a hit. */
typedef struct {
    CettaGsltU32IndexV1 seen;
    CettaGsltU32IndexV1 promoted;
} CettaGsltRepetitionAdmissionV1;

void cetta_gslt_repetition_admission_init_v1(
    CettaGsltRepetitionAdmissionV1 *admission);
void cetta_gslt_repetition_admission_free_v1(
    CettaGsltRepetitionAdmissionV1 *admission);
void cetta_gslt_repetition_admission_reset_v1(
    CettaGsltRepetitionAdmissionV1 *admission);

CettaGsltRepetitionResultV1 cetta_gslt_repetition_admission_classify_v1(
    CettaGsltRepetitionAdmissionV1 *admission,
    uint32_t key,
    CettaGsltRepetitionDecisionV1 *decision_out,
    uint32_t *slot_out);

CettaGsltRepetitionResultV1 cetta_gslt_repetition_admission_promote_v1(
    CettaGsltRepetitionAdmissionV1 *admission,
    uint32_t key,
    uint32_t slot);

bool cetta_gslt_repetition_admission_validate_v1(
    const CettaGsltRepetitionAdmissionV1 *admission);

/* Decide the same complete symbolic inequality as the formal cost qualifier.
 * Counter inconsistencies and arithmetic overflow fail closed as INVALID.
 * NONREGRESSING means exact equality; only PROFITABLE reports strict symbolic
 * improvement, and this function still does not enable an optimization by
 * itself. */
CettaGsltRepetitionCostQualificationV1
cetta_gslt_repetition_cost_qualify_v1(
    const CettaGsltRepetitionCostModelV1 *model,
    uint64_t occurrences,
    uint64_t source_lookups,
    uint64_t cache_hits,
    uint64_t promotions,
    uint64_t *cached_cost_out,
    uint64_t *fresh_cost_out);

#endif
