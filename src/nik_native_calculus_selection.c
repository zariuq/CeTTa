#include "nik_native_calculus_selection.h"

#include <stdbool.h>
#include <stdlib.h>

static CettaNikNativeSelectionV1 native_selection(
    CettaNikNativeSelectionStatusV1 status,
    CettaNikNativeSelectionKindV1 kind,
    size_t eligible_count,
    size_t frontier_count,
    size_t greatest_index) {
    return (CettaNikNativeSelectionV1){
        .status = status,
        .kind = kind,
        .eligible_count = eligible_count,
        .frontier_count = frontier_count,
        .greatest_index = greatest_index,
    };
}

static bool native_capabilities_valid(
    const CettaNikNativeCapabilityIdV1 *capabilities,
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

static bool native_capabilities_include(
    const CettaNikNativeCapabilityIdV1 *superset,
    size_t superset_count,
    const CettaNikNativeCapabilityIdV1 *subset,
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

static bool native_family_shape_valid(
    const CettaNikLicensedNativeFamilyV1 *family) {
    if (!family || family->realization_count == 0u ||
        !family->realizations ||
        ((family->upgrade_count != 0u) != (family->upgrades != NULL))) {
        return false;
    }
    for (size_t index = 0u; index < family->realization_count; index++) {
        const CettaNikLicensedNativeRealizationV1 *realization =
            &family->realizations[index];
        if (realization->realization_identity == 0u ||
            !native_capabilities_valid(
                realization->capabilities,
                realization->capability_count)) {
            return false;
        }
        for (size_t earlier = 0u; earlier < index; earlier++) {
            if (family->realizations[earlier].realization_identity ==
                realization->realization_identity) {
                return false;
            }
        }
    }
    return true;
}

static bool native_build_strict_order(
    const CettaNikLicensedNativeFamilyV1 *family,
    bool *strict_order) {
    const size_t count = family->realization_count;
    for (size_t edge_index = 0u;
         edge_index < family->upgrade_count; edge_index++) {
        const CettaNikLicensedNativeUpgradeV1 *edge =
            &family->upgrades[edge_index];
        if (edge->weaker_index >= count || edge->stronger_index >= count ||
            edge->weaker_index == edge->stronger_index) {
            return false;
        }
        const CettaNikLicensedNativeRealizationV1 *weaker =
            &family->realizations[edge->weaker_index];
        const CettaNikLicensedNativeRealizationV1 *stronger =
            &family->realizations[edge->stronger_index];
        if (!native_capabilities_include(
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

CettaNikNativeSelectionV1 cetta_nik_native_calculus_select_v1(
    const CettaNikLicensedNativeFamilyV1 *family,
    const CettaNikNativeCapabilityRequestV1 *request,
    size_t *frontier_indices,
    size_t frontier_capacity) {
    bool *strict_order = NULL;
    bool *eligible = NULL;
    size_t matrix_count;
    size_t eligible_count = 0u;
    size_t frontier_count = 0u;
    size_t greatest_index = SIZE_MAX;
    CettaNikNativeSelectionKindV1 kind = CETTA_NIK_NATIVE_SELECTION_NONE_V1;
    CettaNikNativeSelectionStatusV1 status =
        CETTA_NIK_NATIVE_SELECTION_STATUS_OK_V1;

    if (!native_family_shape_valid(family)) {
        return native_selection(
            CETTA_NIK_NATIVE_SELECTION_STATUS_INVALID_FAMILY_V1,
            CETTA_NIK_NATIVE_SELECTION_NONE_V1, 0u, 0u, SIZE_MAX);
    }
    if (!request || !native_capabilities_valid(
            request->required_capabilities,
            request->required_capability_count)) {
        return native_selection(
            CETTA_NIK_NATIVE_SELECTION_STATUS_INVALID_REQUEST_V1,
            CETTA_NIK_NATIVE_SELECTION_NONE_V1, 0u, 0u, SIZE_MAX);
    }
    if (family->realization_count >
        SIZE_MAX / family->realization_count) {
        return native_selection(
            CETTA_NIK_NATIVE_SELECTION_STATUS_RESOURCE_FAULT_V1,
            CETTA_NIK_NATIVE_SELECTION_NONE_V1, 0u, 0u, SIZE_MAX);
    }
    matrix_count = family->realization_count * family->realization_count;
    strict_order = calloc(matrix_count, sizeof(*strict_order));
    eligible = calloc(family->realization_count, sizeof(*eligible));
    if (!strict_order || !eligible) {
        status = CETTA_NIK_NATIVE_SELECTION_STATUS_RESOURCE_FAULT_V1;
        goto finish;
    }
    if (!native_build_strict_order(family, strict_order)) {
        status = CETTA_NIK_NATIVE_SELECTION_STATUS_INVALID_FAMILY_V1;
        goto finish;
    }

    for (size_t index = 0u; index < family->realization_count; index++) {
        const CettaNikLicensedNativeRealizationV1 *realization =
            &family->realizations[index];
        eligible[index] = native_capabilities_include(
            realization->capabilities, realization->capability_count,
            request->required_capabilities,
            request->required_capability_count);
        eligible_count += eligible[index] ? 1u : 0u;
    }
    if (eligible_count == 0u)
        goto finish;

    for (size_t candidate = 0u;
         candidate < family->realization_count; candidate++) {
        bool maximal = eligible[candidate];
        for (size_t stronger = 0u;
             maximal && stronger < family->realization_count; stronger++) {
            if (eligible[stronger] &&
                strict_order[candidate * family->realization_count +
                             stronger]) {
                maximal = false;
            }
        }
        frontier_count += maximal ? 1u : 0u;
    }

    if (frontier_count == 1u) {
        kind = CETTA_NIK_NATIVE_SELECTION_UNIQUE_GREATEST_V1;
        for (size_t candidate = 0u;
             candidate < family->realization_count; candidate++) {
            if (!eligible[candidate])
                continue;
            bool maximal = true;
            for (size_t stronger = 0u;
                 maximal && stronger < family->realization_count; stronger++) {
                if (eligible[stronger] &&
                    strict_order[candidate * family->realization_count +
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
             candidate < family->realization_count; candidate++) {
            if (eligible[candidate] && candidate != greatest_index &&
                !strict_order[candidate * family->realization_count +
                              greatest_index]) {
                status =
                    CETTA_NIK_NATIVE_SELECTION_STATUS_INVALID_FAMILY_V1;
                goto finish;
            }
        }
    } else {
        kind = CETTA_NIK_NATIVE_SELECTION_MAXIMAL_FRONTIER_V1;
    }

    if (frontier_capacity < frontier_count ||
        (frontier_count != 0u && !frontier_indices)) {
        status = CETTA_NIK_NATIVE_SELECTION_STATUS_OUTPUT_TOO_SMALL_V1;
        goto finish;
    }
    size_t output_index = 0u;
    for (size_t candidate = 0u;
         candidate < family->realization_count; candidate++) {
        if (!eligible[candidate])
            continue;
        bool maximal = true;
        for (size_t stronger = 0u;
             maximal && stronger < family->realization_count; stronger++) {
            if (eligible[stronger] &&
                strict_order[candidate * family->realization_count +
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
    return native_selection(
        status, kind, eligible_count, frontier_count, greatest_index);
}
