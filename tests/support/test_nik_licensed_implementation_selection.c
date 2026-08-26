#include "nik_licensed_implementation_selection.h"

#include <stdio.h>

static unsigned checks;
static unsigned failures;

#define CHECK(condition, label)                                              \
    do {                                                                     \
        checks++;                                                            \
        if (!(condition)) {                                                  \
            fprintf(stderr, "FAIL: %s\n", (label));                         \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static CettaNikImplementationSelectionV1 select_family(
    const CettaNikLicensedImplementationV1 *implementations,
    size_t implementation_count,
    const CettaNikLicensedImplementationUpgradeV1 *upgrades,
    size_t upgrade_count,
    const CettaNikImplementationCapabilityIdV1 *required,
    size_t required_count,
    size_t *frontier,
    size_t frontier_capacity) {
    CettaNikLicensedImplementationFamilyV1 family = {
        .implementations = implementations,
        .implementation_count = implementation_count,
        .upgrades = upgrades,
        .upgrade_count = upgrade_count,
    };
    CettaNikImplementationCapabilityRequestV1 request = {
        .required_capabilities = required,
        .required_capability_count = required_count,
    };
    return cetta_nik_licensed_implementation_select_v1(
        &family, &request, frontier, frontier_capacity);
}

int main(void) {
    static const CettaNikImplementationCapabilityIdV1 check_caps[] = {1u};
    static const CettaNikImplementationCapabilityIdV1 proof_caps[] = {1u, 2u};
    static const CettaNikImplementationCapabilityIdV1 construct_caps[] = {
        1u, 2u, 3u,
    };
    static const CettaNikLicensedImplementationV1 linear[] = {
        {7u, 11u, check_caps, 1u},
        {7u, 12u, proof_caps, 2u},
        {7u, 13u, construct_caps, 3u},
    };
    static const CettaNikLicensedImplementationUpgradeV1 linear_upgrades[] = {
        {0u, 1u},
        {1u, 2u},
    };
    size_t frontier[3] = {SIZE_MAX, SIZE_MAX, SIZE_MAX};
    CettaNikImplementationSelectionV1 selected = select_family(
        linear, 3u, linear_upgrades, 2u,
        check_caps, 1u, frontier, 3u);
    CHECK(selected.status == CETTA_NIK_IMPLEMENTATION_SELECTION_STATUS_OK_V1 &&
              selected.kind ==
                  CETTA_NIK_IMPLEMENTATION_SELECTION_UNIQUE_GREATEST_V1 &&
              selected.eligible_count == 3u &&
              selected.frontier_count == 1u &&
              selected.greatest_index == 2u && frontier[0] == 2u,
          "a directed licensed family exposes its unique greatest implementation");

    static const CettaNikImplementationCapabilityIdV1 construct_request[] = {3u};
    selected = select_family(
        linear, 3u, linear_upgrades, 2u,
        construct_request, 1u, frontier, 3u);
    CHECK(selected.status == CETTA_NIK_IMPLEMENTATION_SELECTION_STATUS_OK_V1 &&
              selected.kind ==
                  CETTA_NIK_IMPLEMENTATION_SELECTION_UNIQUE_GREATEST_V1 &&
              selected.eligible_count == 1u &&
              selected.greatest_index == 2u,
          "the exact request fibre excludes implementations missing a required capability");

    static const CettaNikImplementationCapabilityIdV1 left_caps[] = {10u};
    static const CettaNikImplementationCapabilityIdV1 right_caps[] = {20u};
    static const CettaNikLicensedImplementationV1 incomparable[] = {
        {7u, 21u, left_caps, 1u},
        {7u, 22u, right_caps, 1u},
    };
    frontier[0] = frontier[1] = SIZE_MAX;
    selected = select_family(
        incomparable, 2u, NULL, 0u,
        NULL, 0u, frontier, 2u);
    CHECK(selected.status == CETTA_NIK_IMPLEMENTATION_SELECTION_STATUS_OK_V1 &&
              selected.kind ==
                  CETTA_NIK_IMPLEMENTATION_SELECTION_MAXIMAL_FRONTIER_V1 &&
              selected.eligible_count == 2u &&
              selected.frontier_count == 2u &&
              selected.greatest_index == SIZE_MAX &&
              frontier[0] == 0u && frontier[1] == 1u,
          "incomparable licensed implementations remain an explicit maximal frontier");

    static const CettaNikImplementationCapabilityIdV1 join_caps[] = {
        10u, 20u, 30u,
    };
    static const CettaNikLicensedImplementationV1 joined[] = {
        {7u, 21u, left_caps, 1u},
        {7u, 22u, right_caps, 1u},
        {7u, 23u, join_caps, 3u},
    };
    static const CettaNikLicensedImplementationUpgradeV1 joined_upgrades[] = {
        {0u, 2u},
        {1u, 2u},
    };
    selected = select_family(
        joined, 3u, joined_upgrades, 2u,
        NULL, 0u, frontier, 3u);
    CHECK(selected.status == CETTA_NIK_IMPLEMENTATION_SELECTION_STATUS_OK_V1 &&
              selected.kind ==
                  CETTA_NIK_IMPLEMENTATION_SELECTION_UNIQUE_GREATEST_V1 &&
              selected.greatest_index == 2u,
          "an explicitly admitted common upgrade dissolves the maximal frontier");

    static const CettaNikLicensedImplementationV1 superset_only[] = {
        {7u, 31u, check_caps, 1u},
        {7u, 32u, proof_caps, 2u},
    };
    selected = select_family(
        superset_only, 2u, NULL, 0u,
        NULL, 0u, frontier, 2u);
    CHECK(selected.status == CETTA_NIK_IMPLEMENTATION_SELECTION_STATUS_OK_V1 &&
              selected.kind ==
                  CETTA_NIK_IMPLEMENTATION_SELECTION_MAXIMAL_FRONTIER_V1 &&
              selected.frontier_count == 2u,
          "capability inclusion alone never invents semantic dominance");

    static const CettaNikLicensedImplementationV1 equal_support[] = {
        {7u, 41u, proof_caps, 2u},
        {7u, 42u, proof_caps, 2u},
    };
    static const CettaNikLicensedImplementationUpgradeV1 false_upgrade[] = {
        {0u, 1u},
    };
    selected = select_family(
        equal_support, 2u, false_upgrade, 1u,
        NULL, 0u, frontier, 2u);
    CHECK(selected.status ==
              CETTA_NIK_IMPLEMENTATION_SELECTION_STATUS_INVALID_FAMILY_V1,
          "a strict upgrade without a real capability gain is not representable as a licensed order");

    static const CettaNikLicensedImplementationV1 mixed_calculi[] = {
        {7u, 51u, check_caps, 1u},
        {8u, 52u, proof_caps, 2u},
    };
    selected = select_family(
        mixed_calculi, 2u, NULL, 0u,
        NULL, 0u, frontier, 2u);
    CHECK(selected.status ==
              CETTA_NIK_IMPLEMENTATION_SELECTION_STATUS_INVALID_FAMILY_V1,
          "implementation policy cannot compare different native calculi");

    static const CettaNikLicensedImplementationUpgradeV1 cycle[] = {
        {0u, 1u},
        {1u, 0u},
    };
    selected = select_family(
        superset_only, 2u, cycle, 2u,
        NULL, 0u, frontier, 2u);
    CHECK(selected.status ==
              CETTA_NIK_IMPLEMENTATION_SELECTION_STATUS_INVALID_FAMILY_V1,
          "a cyclic or capability-losing upgrade family is rejected");

    static const CettaNikImplementationCapabilityIdV1 unavailable[] = {99u};
    selected = select_family(
        linear, 3u, linear_upgrades, 2u,
        unavailable, 1u, frontier, 3u);
    CHECK(selected.status == CETTA_NIK_IMPLEMENTATION_SELECTION_STATUS_OK_V1 &&
              selected.kind == CETTA_NIK_IMPLEMENTATION_SELECTION_NONE_V1 &&
              selected.eligible_count == 0u &&
              selected.frontier_count == 0u,
          "a capability miss yields ordinary fallback rather than a semantic verdict");

    size_t sentinel = 777u;
    selected = select_family(
        incomparable, 2u, NULL, 0u,
        NULL, 0u, &sentinel, 1u);
    CHECK(selected.status ==
              CETTA_NIK_IMPLEMENTATION_SELECTION_STATUS_OUTPUT_TOO_SMALL_V1 &&
              selected.kind ==
                  CETTA_NIK_IMPLEMENTATION_SELECTION_MAXIMAL_FRONTIER_V1 &&
              selected.frontier_count == 2u && sentinel == 777u,
          "an undersized frontier buffer reports the exact need without a partial policy choice");

    printf(
        "NikLicensedImplementationSelectionSummary checks=%u failures=%u\n",
        checks, failures);
    return failures == 0u ? 0 : 1;
}
