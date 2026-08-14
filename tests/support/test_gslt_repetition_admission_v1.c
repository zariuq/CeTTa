#include "gslt_repetition_admission_v1.h"

#include <stdio.h>

static unsigned checks;
static unsigned failures;

static void check(bool condition, const char *name) {
    checks++;
    if (!condition) {
        failures++;
        fprintf(stderr, "FAIL: %s\n", name);
    }
}

int main(void) {
    CettaGsltRepetitionAdmissionV1 admission;
    CettaGsltRepetitionDecisionV1 decision;
    const CettaGsltRepetitionCostModelV1 profitable_model = {
        1u, 100u, 2u, 20u, 10u
    };
    const CettaGsltRepetitionCostModelV1 losing_model = {
        4u, 10u, 5u, 20u, 10u
    };
    const CettaGsltRepetitionCostModelV1 overflow_model = {
        UINT64_MAX, 1u, 1u, 1u, 1u
    };
    uint32_t slot;
    uint64_t cached_cost;
    uint64_t fresh_cost;

    cetta_gslt_repetition_admission_init_v1(&admission);
    check(cetta_gslt_repetition_admission_validate_v1(&admission),
          "empty admission validates");
    check(cetta_gslt_repetition_admission_classify_v1(
              &admission, 7u, &decision, &slot) ==
              CETTA_GSLT_REPETITION_OK_V1 &&
              decision == CETTA_GSLT_REPETITION_FIRST_V1,
          "first occurrence stays transient");
    check(cetta_gslt_repetition_admission_classify_v1(
              &admission, 11u, &decision, &slot) ==
              CETTA_GSLT_REPETITION_OK_V1 &&
              decision == CETTA_GSLT_REPETITION_FIRST_V1,
          "independent singleton stays transient");
    check(cetta_gslt_repetition_admission_classify_v1(
              &admission, 7u, &decision, &slot) ==
              CETTA_GSLT_REPETITION_OK_V1 &&
              decision == CETTA_GSLT_REPETITION_PROMOTE_V1,
          "second occurrence requests promotion");
    check(cetta_gslt_repetition_admission_promote_v1(
              &admission, 7u, 23u) == CETTA_GSLT_REPETITION_OK_V1,
          "caller records a promoted slot");
    check(cetta_gslt_repetition_admission_classify_v1(
              &admission, 7u, &decision, &slot) ==
              CETTA_GSLT_REPETITION_OK_V1 &&
              decision == CETTA_GSLT_REPETITION_HIT_V1 && slot == 23u,
          "third occurrence resolves the promoted slot");
    check(cetta_gslt_repetition_admission_promote_v1(
              &admission, 13u, 1u) == CETTA_GSLT_REPETITION_INVALID_V1,
          "unseen key cannot be promoted");
    check(cetta_gslt_repetition_admission_promote_v1(
              &admission, 7u, 24u) == CETTA_GSLT_REPETITION_INVALID_V1,
          "promoted key cannot be rebound");
    check(cetta_gslt_repetition_admission_validate_v1(&admission),
          "admitted repetition state remains valid");
    cetta_gslt_repetition_admission_reset_v1(&admission);
    check(cetta_gslt_repetition_admission_classify_v1(
              &admission, 7u, &decision, &slot) ==
              CETTA_GSLT_REPETITION_OK_V1 &&
              decision == CETTA_GSLT_REPETITION_FIRST_V1,
          "execution boundary clears logical admission state");
    cetta_gslt_repetition_admission_free_v1(&admission);

    check(cetta_gslt_repetition_cost_qualify_v1(
              &profitable_model, 3u, 2u, 1u, 1u,
              &cached_cost, &fresh_cost) ==
              CETTA_GSLT_REPETITION_COST_PROFITABLE_V1 &&
              cached_cost == 235u && fresh_cost == 300u,
          "complete repeated workload cost qualifies");
    check(cetta_gslt_repetition_cost_qualify_v1(
              &losing_model, 3u, 2u, 1u, 1u,
              &cached_cost, &fresh_cost) ==
              CETTA_GSLT_REPETITION_COST_REJECTED_V1 &&
              cached_cost == 67u && fresh_cost == 30u,
          "unprofitable cost is rejected");
    check(cetta_gslt_repetition_cost_qualify_v1(
              &profitable_model, 1u, 1u, 0u, 0u,
              &cached_cost, &fresh_cost) ==
              CETTA_GSLT_REPETITION_COST_REJECTED_V1 &&
              cached_cost == 101u && fresh_cost == 100u,
          "singleton cost is rejected");
    check(cetta_gslt_repetition_cost_qualify_v1(
              &profitable_model, 0u, 0u, 0u, 0u,
              &cached_cost, &fresh_cost) ==
              CETTA_GSLT_REPETITION_COST_NONREGRESSING_V1 &&
              cached_cost == 0u && fresh_cost == 0u,
          "empty workload is nonregressing but not profitable");
    check(cetta_gslt_repetition_cost_qualify_v1(
              &profitable_model, 3u, 1u, 1u, 1u,
              &cached_cost, &fresh_cost) ==
              CETTA_GSLT_REPETITION_COST_INVALID_V1,
          "counter mismatch fails closed");
    check(cetta_gslt_repetition_cost_qualify_v1(
              &profitable_model, 1u, 0u, 1u, 1u,
              &cached_cost, &fresh_cost) ==
              CETTA_GSLT_REPETITION_COST_INVALID_V1,
          "promotion without source lookup fails closed");
    check(cetta_gslt_repetition_cost_qualify_v1(
              &overflow_model, 2u, 2u, 0u, 0u,
              &cached_cost, &fresh_cost) ==
              CETTA_GSLT_REPETITION_COST_INVALID_V1,
          "cost overflow fails closed");
    check(cetta_gslt_repetition_cost_qualify_v1(
              NULL, 0u, 0u, 0u, 0u,
              &cached_cost, &fresh_cost) ==
              CETTA_GSLT_REPETITION_COST_INVALID_V1,
          "missing cost model fails closed");

    printf("(GsltRepetitionAdmissionV1Summary %u %u %u)\n",
           checks, checks - failures, failures);
    return failures ? 1 : 0;
}
