#include "gslt_classified_value_v1.h"

#include <stdio.h>

static unsigned checks;
static unsigned failures;

#define CHECK(CONDITION)                                                       \
    do {                                                                       \
        checks++;                                                              \
        if (!(CONDITION)) {                                                    \
            failures++;                                                        \
            fprintf(stderr, "check failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #CONDITION);                           \
        }                                                                      \
    } while (0)

static uint32_t parity_classification(uint32_t value) {
    return value % 2u == 0u ? 1u : 2u;
}

static uint32_t bounded_classification(uint32_t value) {
    if (value < 10u)
        return 1u;
    return value == 10u ? 2u : 3u;
}

int main(void) {
    static const CettaGsltClassificationCaseV1 parity_cases[] = {
        {0u, 1u},
        {1u, 2u},
    };
    static const CettaGsltClassificationCaseV1 bounded_cases[] = {
        {7u, 1u},
        {10u, 2u},
        {14u, 3u},
    };
    static const CettaGsltClassificationCaseV1 ambiguous_cases[] = {
        {7u, 1u},
        {7u, 2u},
    };
    CettaGsltClassifiedValueV1 parity = {0};
    CettaGsltClassifiedValueV1 bounded = {0};
    CettaGsltClassifiedValueV1 rejected = {91u, 77u};

    CHECK(cetta_gslt_classified_value_init_v1(
        &parity, 8u, parity_classification(8u), 2u));
    CHECK(parity.value == 8u && parity.classification == 1u);
    CHECK(cetta_gslt_classified_value_validate_v1(&parity, 2u));

    CHECK(cetta_gslt_classified_value_init_v1(
        &bounded, 14u, bounded_classification(14u), 3u));
    CHECK(bounded.value == 14u && bounded.classification == 3u);
    CHECK(cetta_gslt_classified_value_validate_v1(&bounded, 3u));

    CHECK(!cetta_gslt_classified_value_init_v1(
        NULL, 5u, parity_classification(5u), 2u));
    CHECK(!cetta_gslt_classified_value_init_v1(
        &rejected, 5u, 0u, 2u));
    CHECK(rejected.value == 91u && rejected.classification == 77u);
    CHECK(!cetta_gslt_classified_value_init_v1(
        &rejected, 5u, 3u, 2u));
    CHECK(rejected.value == 91u && rejected.classification == 77u);
    CHECK(!cetta_gslt_classified_value_validate_v1(NULL, 3u));
    CHECK(!cetta_gslt_classified_value_validate_v1(&rejected, 3u));

    CHECK(cetta_gslt_classified_value_from_cases_v1(
        &parity, 8u, 8u % 2u, parity_cases, 2u, 2u));
    CHECK(parity.value == 8u && parity.classification == 1u);
    CHECK(cetta_gslt_classified_value_from_cases_v1(
        &bounded, 99u, 14u, bounded_cases, 3u, 3u));
    CHECK(bounded.value == 99u && bounded.classification == 3u);
    CHECK(!cetta_gslt_classified_value_from_cases_v1(
        &rejected, 8u, 8u, parity_cases, 2u, 2u));
    CHECK(!cetta_gslt_classified_value_from_cases_v1(
        &rejected, 8u, 7u, ambiguous_cases, 2u, 2u));
    CHECK(!cetta_gslt_classified_value_from_cases_v1(
        &rejected, 8u, 0u, NULL, 1u, 2u));

    printf("(GsltClassifiedValueV1Summary %u %u %u)\n",
           checks, checks - failures, failures);
    return failures == 0u ? 0 : 1;
}
