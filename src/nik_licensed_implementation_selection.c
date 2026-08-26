#include "nik_licensed_implementation_selection.h"

#include <stdbool.h>
#include <stdlib.h>

static CettaNikImplementationSelectionV1 implementation_selection(
    CettaNikImplementationSelectionStatusV1 status,
    CettaNikImplementationSelectionKindV1 kind,
    size_t eligible_count,
    size_t frontier_count,
    size_t greatest_index) {
    return (CettaNikImplementationSelectionV1){
        .status = status,
        .kind = kind,
        .eligible_count = eligible_count,
        .frontier_count = frontier_count,
        .greatest_index = greatest_index,
    };
}

static bool implementation_capabilities_valid(
    const CettaNikImplementationCapabilityIdV1 *capabilities,
    size_t capability_count) {
    if ((capability_count != 0u) != (capabilities != NULL))
        return false;
    for (size_t index = 0u; index < capability_count; index++) {
        if (capabilities[index] == 0u ||
            (index != 0u &&
             capabilities[index - 1u] >= capabilities[index])) {
            return false;
        }
    }
    return true;
}

static bool implementation_capabilities_include(
    const CettaNikImplementationCapabilityIdV1 *superset,
    size_t superset_count,
    const CettaNikImplementationCapabilityIdV1 *subset,
    size_t subset_count) {
    size_t superset_index = 0u;
    size_t subset_index = 0u;
    while (superset_index < superset_count && subset_index < subset_count) {
        if (superset[superset_index] < subset[subset_index]) {
            superset_index++;
        } else if (superset[superset_index] == subset[subset_index]) {
            superset_index++;
            subset_index++;
        } else {
            return false;
        }
    }
    return subset_index == subset_count;
}

static bool implementation_family_shape_valid(
    const CettaNikLicensedImplementationFamilyV1 *family) {
    if (!family || family->implementation_count == 0u ||
        !family->implementations ||
        ((family->upgrade_count != 0u) != (family->upgrades != NULL))) {
        return false;
    }
    for (size_t index = 0u; index < family->implementation_count; index++) {
        const CettaNikLicensedImplementationV1 *implementation =
            &family->implementations[index];
        if (implementation->calculus_identity == 0u ||
            implementation->implementation_identity == 0u ||
            !implementation_capabilities_valid(
                implementation->capabilities,
                implementation->capability_count)) {
            return false;
        }
        if (index != 0u &&
            family->implementations[0].calculus_identity !=
                implementation->calculus_identity) {
            return false;
        }
        for (size_t earlier = 0u; earlier < index; earlier++) {
            if (family->implementations[earlier].implementation_identity ==
                implementation->implementation_identity) {
                return false;
            }
        }
    }
    return true;
}

static bool implementation_build_strict_order(
    const CettaNikLicensedImplementationFamilyV1 *family,
    bool *strict_order) {
    const size_t count = family->implementation_count;
    for (size_t edge_index = 0u;
         edge_index < family->upgrade_count; edge_index++) {
        const CettaNikLicensedImplementationUpgradeV1 *edge =
            &family->upgrades[edge_index];
        if (edge->weaker_index >= count || edge->stronger_index >= count ||
            edge->weaker_index == edge->stronger_index) {
            return false;
        }
        const CettaNikLicensedImplementationV1 *weaker =
            &family->implementations[edge->weaker_index];
        const CettaNikLicensedImplementationV1 *stronger =
            &family->implementations[edge->stronger_index];
        if (!implementation_capabilities_include(
                stronger->capabilities, stronger->capability_count,
                weaker->capabilities, weaker->capability_count) ||
            stronger->capability_count == weaker->capability_count) {
            return false;
        }
        size_t offset = edge->weaker_index * count + edge->stronger_index;
        if (strict_order[offset])
            return false;
        strict_order[offset] = true;
    }

    for (size_t middle = 0u; middle < count; middle++) {
        for (size_t weaker = 0u; weaker < count; weaker++) {
            if (!strict_order[weaker * count + middle])
                continue;
            for (size_t stronger = 0u; stronger < count; stronger++) {
                if (strict_order[middle * count + stronger])
                    strict_order[weaker * count + stronger] = true;
            }
        }
    }
    for (size_t index = 0u; index < count; index++) {
        if (strict_order[index * count + index])
            return false;
    }
    return true;
}

CettaNikImplementationSelectionV1 cetta_nik_licensed_implementation_select_v1(
    const CettaNikLicensedImplementationFamilyV1 *family,
    const CettaNikImplementationCapabilityRequestV1 *request,
    size_t *frontier_indices,
    size_t frontier_capacity) {
    bool *strict_order = NULL;
    bool *eligible = NULL;
    size_t matrix_count;
    size_t eligible_count = 0u;
    size_t frontier_count = 0u;
    size_t greatest_index = SIZE_MAX;
    CettaNikImplementationSelectionKindV1 kind = CETTA_NIK_IMPLEMENTATION_SELECTION_NONE_V1;
    CettaNikImplementationSelectionStatusV1 status =
        CETTA_NIK_IMPLEMENTATION_SELECTION_STATUS_OK_V1;

    if (!implementation_family_shape_valid(family)) {
        return implementation_selection(
            CETTA_NIK_IMPLEMENTATION_SELECTION_STATUS_INVALID_FAMILY_V1,
            CETTA_NIK_IMPLEMENTATION_SELECTION_NONE_V1, 0u, 0u, SIZE_MAX);
    }
    if (!request || !implementation_capabilities_valid(
            request->required_capabilities,
            request->required_capability_count)) {
        return implementation_selection(
            CETTA_NIK_IMPLEMENTATION_SELECTION_STATUS_INVALID_REQUEST_V1,
            CETTA_NIK_IMPLEMENTATION_SELECTION_NONE_V1, 0u, 0u, SIZE_MAX);
    }
    if (family->implementation_count >
        SIZE_MAX / family->implementation_count) {
        return implementation_selection(
            CETTA_NIK_IMPLEMENTATION_SELECTION_STATUS_RESOURCE_FAULT_V1,
            CETTA_NIK_IMPLEMENTATION_SELECTION_NONE_V1, 0u, 0u, SIZE_MAX);
    }
    matrix_count = family->implementation_count * family->implementation_count;
    strict_order = calloc(matrix_count, sizeof(*strict_order));
    eligible = calloc(family->implementation_count, sizeof(*eligible));
    if (!strict_order || !eligible) {
        status = CETTA_NIK_IMPLEMENTATION_SELECTION_STATUS_RESOURCE_FAULT_V1;
        goto finish;
    }
    if (!implementation_build_strict_order(family, strict_order)) {
        status = CETTA_NIK_IMPLEMENTATION_SELECTION_STATUS_INVALID_FAMILY_V1;
        goto finish;
    }

    for (size_t index = 0u; index < family->implementation_count; index++) {
        const CettaNikLicensedImplementationV1 *implementation =
            &family->implementations[index];
        eligible[index] = implementation_capabilities_include(
            implementation->capabilities, implementation->capability_count,
            request->required_capabilities,
            request->required_capability_count);
        eligible_count += eligible[index] ? 1u : 0u;
    }
    if (eligible_count == 0u)
        goto finish;

    for (size_t candidate = 0u;
         candidate < family->implementation_count; candidate++) {
        bool maximal = eligible[candidate];
        for (size_t stronger = 0u;
             maximal && stronger < family->implementation_count; stronger++) {
            if (eligible[stronger] &&
                strict_order[candidate * family->implementation_count +
                             stronger]) {
                maximal = false;
            }
        }
        frontier_count += maximal ? 1u : 0u;
    }

    if (frontier_count == 1u) {
        kind = CETTA_NIK_IMPLEMENTATION_SELECTION_UNIQUE_GREATEST_V1;
        for (size_t candidate = 0u;
             candidate < family->implementation_count; candidate++) {
            if (!eligible[candidate])
                continue;
            bool maximal = true;
            for (size_t stronger = 0u;
                 maximal && stronger < family->implementation_count; stronger++) {
                if (eligible[stronger] &&
                    strict_order[candidate * family->implementation_count +
                                 stronger]) {
                    maximal = false;
                }
            }
            if (maximal) {
                greatest_index = candidate;
                break;
            }
        }
        for (size_t candidate = 0u;
             candidate < family->implementation_count; candidate++) {
            if (eligible[candidate] && candidate != greatest_index &&
                !strict_order[candidate * family->implementation_count +
                              greatest_index]) {
                status =
                    CETTA_NIK_IMPLEMENTATION_SELECTION_STATUS_INVALID_FAMILY_V1;
                goto finish;
            }
        }
    } else {
        kind = CETTA_NIK_IMPLEMENTATION_SELECTION_MAXIMAL_FRONTIER_V1;
    }

    if (frontier_capacity < frontier_count ||
        (frontier_count != 0u && !frontier_indices)) {
        status = CETTA_NIK_IMPLEMENTATION_SELECTION_STATUS_OUTPUT_TOO_SMALL_V1;
        goto finish;
    }
    size_t output_index = 0u;
    for (size_t candidate = 0u;
         candidate < family->implementation_count; candidate++) {
        if (!eligible[candidate])
            continue;
        bool maximal = true;
        for (size_t stronger = 0u;
             maximal && stronger < family->implementation_count; stronger++) {
            if (eligible[stronger] &&
                strict_order[candidate * family->implementation_count +
                             stronger]) {
                maximal = false;
            }
        }
        if (maximal)
            frontier_indices[output_index++] = candidate;
    }

finish:
    free(eligible);
    free(strict_order);
    return implementation_selection(
        status, kind, eligible_count, frontier_count, greatest_index);
}
