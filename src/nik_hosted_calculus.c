#include "nik_hosted_calculus.h"

#include <stdlib.h>
#include <string.h>

struct CettaNikHostedNativeInstanceV1 {
    const CettaNikNativeImplementationLicenseV1 *license;
    const void *scope_state;
    const void *implementation_context;
    CettaNikDirectAuthorityTokenV1 admitted_token;
};

struct CettaNikHostedCertificateBoundaryV1 {
    const CettaNikCertificateBoundaryV1 *descriptor;
    const void *scope_state;
    void *checker_state;
    CettaNikDirectAuthorityTokenV1 admitted_token;
};

static bool identities_valid(
    const uint64_t *identities, size_t identity_count) {
    if ((identity_count != 0u) != (identities != NULL))
        return false;
    for (size_t index = 0u; index < identity_count; index++) {
        if (identities[index] == 0u ||
            (index != 0u && identities[index - 1u] >= identities[index])) {
            return false;
        }
    }
    return true;
}

static bool capabilities_include(
    const CettaNikCalculusCapabilityIdV1 *superset,
    size_t superset_count,
    const CettaNikCalculusCapabilityIdV1 *subset,
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

static bool operations_include(
    const CettaNikNativeOperationV1 *superset,
    size_t superset_count,
    const CettaNikNativeOperationV1 *subset,
    size_t subset_count) {
    size_t superset_index = 0u;
    size_t subset_index = 0u;
    while (superset_index < superset_count && subset_index < subset_count) {
        uint64_t superset_id = superset[superset_index].operation_identity;
        uint64_t subset_id = subset[subset_index].operation_identity;
        if (superset_id < subset_id) {
            superset_index++;
        } else if (superset_id == subset_id) {
            superset_index++;
            subset_index++;
        } else {
            return false;
        }
    }
    return subset_index == subset_count;
}

bool cetta_nik_native_calculus_v1_is_valid(
    const CettaNikNativeCalculusV1 *calculus) {
    if (!calculus || !calculus->system_id || !calculus->system_id[0] ||
        calculus->theory_identity == 0u || calculus->theory_revision == 0u ||
        calculus->calculus_identity == 0u ||
        calculus->calculus_revision == 0u ||
        calculus->judgment_family_identity == 0u ||
        calculus->evidence_family_identity == 0u ||
        calculus->computation_identity == 0u ||
        calculus->operation_count == 0u || !calculus->operations ||
        !identities_valid(
            calculus->capabilities, calculus->capability_count)) {
        return false;
    }
    for (size_t index = 0u; index < calculus->operation_count; index++) {
        if (calculus->operations[index].operation_identity == 0u ||
            (index != 0u &&
             calculus->operations[index - 1u].operation_identity >=
                 calculus->operations[index].operation_identity)) {
            return false;
        }
    }
    return true;
}

static CettaNikNativeCalculusSelectionV1 calculus_selection(
    CettaNikNativeCalculusSelectionStatusV1 status,
    CettaNikNativeCalculusSelectionKindV1 kind,
    size_t eligible_count,
    size_t frontier_count,
    size_t greatest_index) {
    return (CettaNikNativeCalculusSelectionV1){
        .status = status,
        .kind = kind,
        .eligible_count = eligible_count,
        .frontier_count = frontier_count,
        .greatest_index = greatest_index,
    };
}

static bool calculus_family_shape_valid(
    const CettaNikNativeCalculusFamilyV1 *family) {
    if (!family || family->calculus_count == 0u || !family->calculi ||
        ((family->upgrade_count != 0u) != (family->upgrades != NULL))) {
        return false;
    }
    for (size_t index = 0u; index < family->calculus_count; index++) {
        const CettaNikNativeCalculusV1 *calculus = &family->calculi[index];
        if (!cetta_nik_native_calculus_v1_is_valid(calculus))
            return false;
        if (index != 0u &&
            (strcmp(family->calculi[0].system_id, calculus->system_id) != 0 ||
             family->calculi[0].theory_identity != calculus->theory_identity ||
             family->calculi[0].theory_revision != calculus->theory_revision)) {
            return false;
        }
        for (size_t earlier = 0u; earlier < index; earlier++) {
            if (family->calculi[earlier].calculus_identity ==
                calculus->calculus_identity) {
                return false;
            }
        }
    }
    return true;
}

static bool calculus_build_strict_order(
    const CettaNikNativeCalculusFamilyV1 *family,
    bool *strict_order) {
    size_t count = family->calculus_count;
    for (size_t edge_index = 0u;
         edge_index < family->upgrade_count; edge_index++) {
        const CettaNikNativeCalculusUpgradeV1 *edge =
            &family->upgrades[edge_index];
        if (edge->weaker_index >= count || edge->stronger_index >= count ||
            edge->weaker_index == edge->stronger_index)
            return false;
        const CettaNikNativeCalculusV1 *weaker =
            &family->calculi[edge->weaker_index];
        const CettaNikNativeCalculusV1 *stronger =
            &family->calculi[edge->stronger_index];
        if (!capabilities_include(
                stronger->capabilities, stronger->capability_count,
                weaker->capabilities, weaker->capability_count) ||
            !operations_include(
                stronger->operations, stronger->operation_count,
                weaker->operations, weaker->operation_count) ||
            (stronger->capability_count == weaker->capability_count &&
             stronger->operation_count == weaker->operation_count)) {
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

CettaNikNativeCalculusSelectionV1 cetta_nik_native_calculus_select_v1(
    const CettaNikNativeCalculusFamilyV1 *family,
    const CettaNikNativeCalculusRequestV1 *request,
    size_t *frontier_indices,
    size_t frontier_capacity) {
    bool *strict_order = NULL;
    bool *eligible = NULL;
    size_t eligible_count = 0u;
    size_t frontier_count = 0u;
    size_t greatest_index = SIZE_MAX;
    CettaNikNativeCalculusSelectionKindV1 kind =
        CETTA_NIK_CALCULUS_SELECTION_NONE_V1;
    CettaNikNativeCalculusSelectionStatusV1 status =
        CETTA_NIK_CALCULUS_SELECTION_STATUS_OK_V1;

    if (!calculus_family_shape_valid(family)) {
        return calculus_selection(
            CETTA_NIK_CALCULUS_SELECTION_STATUS_INVALID_FAMILY_V1,
            CETTA_NIK_CALCULUS_SELECTION_NONE_V1, 0u, 0u, SIZE_MAX);
    }
    if (!request || !identities_valid(
            request->required_capabilities,
            request->required_capability_count)) {
        return calculus_selection(
            CETTA_NIK_CALCULUS_SELECTION_STATUS_INVALID_REQUEST_V1,
            CETTA_NIK_CALCULUS_SELECTION_NONE_V1, 0u, 0u, SIZE_MAX);
    }
    if (family->calculus_count > SIZE_MAX / family->calculus_count) {
        return calculus_selection(
            CETTA_NIK_CALCULUS_SELECTION_STATUS_RESOURCE_FAULT_V1,
            CETTA_NIK_CALCULUS_SELECTION_NONE_V1, 0u, 0u, SIZE_MAX);
    }
    size_t matrix_count = family->calculus_count * family->calculus_count;
    strict_order = calloc(matrix_count, sizeof(*strict_order));
    eligible = calloc(family->calculus_count, sizeof(*eligible));
    if (!strict_order || !eligible) {
        status = CETTA_NIK_CALCULUS_SELECTION_STATUS_RESOURCE_FAULT_V1;
        goto finish;
    }
    if (!calculus_build_strict_order(family, strict_order)) {
        status = CETTA_NIK_CALCULUS_SELECTION_STATUS_INVALID_FAMILY_V1;
        goto finish;
    }

    for (size_t index = 0u; index < family->calculus_count; index++) {
        eligible[index] = capabilities_include(
            family->calculi[index].capabilities,
            family->calculi[index].capability_count,
            request->required_capabilities,
            request->required_capability_count);
        eligible_count += eligible[index] ? 1u : 0u;
    }
    if (eligible_count == 0u)
        goto finish;

    for (size_t candidate = 0u;
         candidate < family->calculus_count; candidate++) {
        bool maximal = eligible[candidate];
        for (size_t stronger = 0u;
             maximal && stronger < family->calculus_count; stronger++) {
            if (eligible[stronger] &&
                strict_order[candidate * family->calculus_count + stronger]) {
                maximal = false;
            }
        }
        frontier_count += maximal ? 1u : 0u;
    }
    if (frontier_count == 1u) {
        kind = CETTA_NIK_CALCULUS_SELECTION_UNIQUE_GREATEST_V1;
        for (size_t candidate = 0u;
             candidate < family->calculus_count; candidate++) {
            if (!eligible[candidate])
                continue;
            bool maximal = true;
            for (size_t stronger = 0u;
                 maximal && stronger < family->calculus_count; stronger++) {
                if (eligible[stronger] &&
                    strict_order[candidate * family->calculus_count +
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
             candidate < family->calculus_count; candidate++) {
            if (eligible[candidate] && candidate != greatest_index &&
                !strict_order[candidate * family->calculus_count +
                              greatest_index]) {
                status = CETTA_NIK_CALCULUS_SELECTION_STATUS_INVALID_FAMILY_V1;
                goto finish;
            }
        }
    } else {
        kind = CETTA_NIK_CALCULUS_SELECTION_MAXIMAL_FRONTIER_V1;
    }

    if (frontier_capacity < frontier_count ||
        (frontier_count != 0u && !frontier_indices)) {
        status = CETTA_NIK_CALCULUS_SELECTION_STATUS_OUTPUT_TOO_SMALL_V1;
        goto finish;
    }
    size_t output_index = 0u;
    for (size_t candidate = 0u;
         candidate < family->calculus_count; candidate++) {
        if (!eligible[candidate])
            continue;
        bool maximal = true;
        for (size_t stronger = 0u;
             maximal && stronger < family->calculus_count; stronger++) {
            if (eligible[stronger] &&
                strict_order[candidate * family->calculus_count + stronger]) {
                maximal = false;
            }
        }
        if (maximal)
            frontier_indices[output_index++] = candidate;
    }

finish:
    free(eligible);
    free(strict_order);
    return calculus_selection(
        status, kind, eligible_count, frontier_count, greatest_index);
}

static bool authority_matches_calculus(
    const CettaNikDirectAuthorityV1 *authority,
    const CettaNikNativeCalculusV1 *calculus) {
    return cetta_nik_direct_authority_v1_is_valid(authority) &&
        cetta_nik_native_calculus_v1_is_valid(calculus) &&
        strcmp(authority->system_id, calculus->system_id) == 0 &&
        authority->authority_identity == calculus->calculus_identity &&
        authority->authority_revision == calculus->calculus_revision;
}

static bool token_matches_authority(
    const CettaNikDirectAuthorityTokenV1 *token,
    const CettaNikDirectAuthorityV1 *authority,
    uint32_t policy_identity) {
    if (!token || !authority ||
        token->length < CETTA_NIK_DIRECT_AUTHORITY_TOKEN_BASE_WORDS ||
        token->length > CETTA_NIK_DIRECT_AUTHORITY_TOKEN_WORD_CAPACITY) {
        return false;
    }
    uint64_t revision_word =
        ((uint64_t)authority->authority_revision << 48u) |
        ((uint64_t)authority->realization_abi << 32u) |
        (uint64_t)policy_identity;
    return token->words[0] == authority->authority_identity &&
        token->words[1] == authority->realization_identity &&
        token->words[2] == revision_word;
}

bool cetta_nik_native_implementation_license_v1_is_valid(
    const CettaNikNativeImplementationLicenseV1 *license) {
    return license &&
        authority_matches_calculus(license->authority, license->calculus) &&
        license->correspondence_identity != 0u &&
        license->correspondence_revision != 0u &&
        license->snapshot && license->run;
}

static bool take_matching_snapshot(
    CettaNikScopeSnapshotV1 snapshot,
    const void *scope_state,
    const CettaNikDirectAuthorityV1 *authority,
    uint32_t policy_identity,
    CettaNikDirectAuthorityTokenV1 *token_out) {
    if (token_out)
        *token_out = (CettaNikDirectAuthorityTokenV1){0};
    return snapshot && token_out && snapshot(scope_state, token_out) &&
        token_matches_authority(token_out, authority, policy_identity);
}

CettaNikHostedNativeAdmissionV1 cetta_nik_hosted_native_admit_v1(
    const CettaNikNativeImplementationLicenseV1 *license,
    const void *scope_state,
    const void *implementation_context) {
    CettaNikHostedNativeAdmissionV1 admission = {
        .kind = CETTA_NIK_HOST_ADMISSION_INVALID_V1,
    };
    CettaNikDirectAuthorityTokenV1 first;
    CettaNikDirectAuthorityTokenV1 second;
    if (!cetta_nik_native_implementation_license_v1_is_valid(license))
        return admission;
    if (!take_matching_snapshot(
            license->snapshot, scope_state, license->authority,
            license->policy_identity, &first) ||
        !take_matching_snapshot(
            license->snapshot, scope_state, license->authority,
            license->policy_identity, &second) ||
        !cetta_nik_direct_authority_token_v1_equal(&first, &second)) {
        admission.kind = CETTA_NIK_HOST_ADMISSION_STALE_V1;
        return admission;
    }
    CettaNikHostedNativeInstanceV1 *instance = malloc(sizeof(*instance));
    if (!instance) {
        admission.kind = CETTA_NIK_HOST_ADMISSION_RESOURCE_FAULT_V1;
        return admission;
    }
    *instance = (CettaNikHostedNativeInstanceV1){
        .license = license,
        .scope_state = scope_state,
        .implementation_context = implementation_context,
        .admitted_token = first,
    };
    admission.kind = CETTA_NIK_HOST_ADMISSION_ADMITTED_V1;
    admission.instance = instance;
    return admission;
}

void cetta_nik_hosted_native_destroy_v1(
    CettaNikHostedNativeInstanceV1 *instance) {
    free(instance);
}

bool cetta_nik_hosted_native_is_current_v1(
    const CettaNikHostedNativeInstanceV1 *instance) {
    CettaNikDirectAuthorityTokenV1 current;
    return instance && take_matching_snapshot(
            instance->license->snapshot, instance->scope_state,
            instance->license->authority,
            instance->license->policy_identity, &current) &&
        cetta_nik_direct_authority_token_v1_equal(
            &instance->admitted_token, &current);
}

static bool calculus_has_operation(
    const CettaNikNativeCalculusV1 *calculus,
    CettaNikNativeOperationIdV1 operation_identity) {
    size_t low = 0u;
    size_t high = calculus->operation_count;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        uint64_t current = calculus->operations[middle].operation_identity;
        if (current < operation_identity)
            low = middle + 1u;
        else
            high = middle;
    }
    return low < calculus->operation_count &&
        calculus->operations[low].operation_identity == operation_identity;
}

static CettaNikResultV1 engine_fault_result(void) {
    return cetta_nik_result_v1_engine_fault(
        CETTA_NIK_ENGINE_FAULT_UNAVAILABLE);
}

static void native_receipt_reset(CettaNikHostedNativeReceiptV1 *receipt) {
    if (!receipt)
        return;
    *receipt = (CettaNikHostedNativeReceiptV1){
        .kind = CETTA_NIK_HOSTED_NATIVE_CALL_INVALID_V1,
        .result = engine_fault_result(),
    };
}

CettaNikHostedNativeCallKindV1 cetta_nik_hosted_native_run_v1(
    CettaNikHostedNativeInstanceV1 *instance,
    CettaNikNativeOperationIdV1 operation_identity,
    const void *request,
    void *language_receipt_out,
    CettaNikHostedNativeReceiptV1 *receipt_out) {
    native_receipt_reset(receipt_out);
    if (!instance || !receipt_out || operation_identity == 0u) {
        return CETTA_NIK_HOSTED_NATIVE_CALL_INVALID_V1;
    }
    const CettaNikNativeImplementationLicenseV1 *license = instance->license;
    receipt_out->theory_identity = license->calculus->theory_identity;
    receipt_out->theory_revision = license->calculus->theory_revision;
    receipt_out->calculus_identity = license->calculus->calculus_identity;
    receipt_out->calculus_revision = license->calculus->calculus_revision;
    receipt_out->implementation_identity =
        license->authority->realization_identity;
    receipt_out->implementation_abi =
        license->authority->realization_abi;
    receipt_out->correspondence_identity = license->correspondence_identity;
    receipt_out->correspondence_revision =
        license->correspondence_revision;
    receipt_out->policy_identity = license->policy_identity;
    receipt_out->operation_identity = operation_identity;
    receipt_out->authority = instance->admitted_token;
    if (!calculus_has_operation(license->calculus, operation_identity)) {
        receipt_out->kind =
            CETTA_NIK_HOSTED_NATIVE_CALL_OUTSIDE_CALCULUS_V1;
        return receipt_out->kind;
    }
    if (!cetta_nik_hosted_native_is_current_v1(instance)) {
        receipt_out->kind = CETTA_NIK_HOSTED_NATIVE_CALL_STALE_V1;
        return receipt_out->kind;
    }
    CettaNikResultV1 result = engine_fault_result();
    if (!license->run(
            instance->implementation_context, operation_identity, request,
            language_receipt_out, &result) ||
        !cetta_nik_result_v1_is_valid(result)) {
        receipt_out->kind = CETTA_NIK_HOSTED_NATIVE_CALL_ENGINE_FAULT_V1;
        return receipt_out->kind;
    }
    if (!cetta_nik_hosted_native_is_current_v1(instance)) {
        receipt_out->kind = CETTA_NIK_HOSTED_NATIVE_CALL_STALE_V1;
        return receipt_out->kind;
    }
    receipt_out->kind = CETTA_NIK_HOSTED_NATIVE_CALL_RESULT_V1;
    receipt_out->result = result;
    return receipt_out->kind;
}

bool cetta_nik_certificate_boundary_v1_is_valid(
    const CettaNikCertificateBoundaryV1 *boundary) {
    return boundary &&
        authority_matches_calculus(boundary->authority, boundary->calculus) &&
        boundary->adequacy_identity != 0u &&
        boundary->adequacy_revision != 0u &&
        boundary->snapshot && boundary->check;
}

CettaNikHostedCertificateAdmissionV1
cetta_nik_hosted_certificate_admit_v1(
    const CettaNikCertificateBoundaryV1 *boundary,
    const void *scope_state,
    void *checker_state) {
    CettaNikHostedCertificateAdmissionV1 admission = {
        .kind = CETTA_NIK_HOST_ADMISSION_INVALID_V1,
    };
    CettaNikDirectAuthorityTokenV1 first;
    CettaNikDirectAuthorityTokenV1 second;
    if (!cetta_nik_certificate_boundary_v1_is_valid(boundary))
        return admission;
    if (!take_matching_snapshot(
            boundary->snapshot, scope_state, boundary->authority,
            boundary->policy_identity, &first) ||
        !take_matching_snapshot(
            boundary->snapshot, scope_state, boundary->authority,
            boundary->policy_identity, &second) ||
        !cetta_nik_direct_authority_token_v1_equal(&first, &second)) {
        admission.kind = CETTA_NIK_HOST_ADMISSION_STALE_V1;
        return admission;
    }
    CettaNikHostedCertificateBoundaryV1 *hosted = malloc(sizeof(*hosted));
    if (!hosted) {
        admission.kind = CETTA_NIK_HOST_ADMISSION_RESOURCE_FAULT_V1;
        return admission;
    }
    *hosted = (CettaNikHostedCertificateBoundaryV1){
        .descriptor = boundary,
        .scope_state = scope_state,
        .checker_state = checker_state,
        .admitted_token = first,
    };
    admission.kind = CETTA_NIK_HOST_ADMISSION_ADMITTED_V1;
    admission.boundary = hosted;
    return admission;
}

void cetta_nik_hosted_certificate_destroy_v1(
    CettaNikHostedCertificateBoundaryV1 *boundary) {
    free(boundary);
}

bool cetta_nik_hosted_certificate_is_current_v1(
    const CettaNikHostedCertificateBoundaryV1 *boundary) {
    CettaNikDirectAuthorityTokenV1 current;
    return boundary && take_matching_snapshot(
            boundary->descriptor->snapshot, boundary->scope_state,
            boundary->descriptor->authority,
            boundary->descriptor->policy_identity, &current) &&
        cetta_nik_direct_authority_token_v1_equal(
            &boundary->admitted_token, &current);
}

static void certificate_receipt_reset(
    CettaNikHostedCertificateReceiptV1 *receipt) {
    if (!receipt)
        return;
    *receipt = (CettaNikHostedCertificateReceiptV1){
        .kind = CETTA_NIK_HOSTED_CERTIFICATE_CHECK_INVALID_V1,
        .result = engine_fault_result(),
    };
}

CettaNikHostedCertificateCheckKindV1
cetta_nik_hosted_certificate_check_v1(
    CettaNikHostedCertificateBoundaryV1 *boundary,
    const void *claim,
    const void *certificate,
    void *language_receipt_out,
    CettaNikHostedCertificateReceiptV1 *receipt_out) {
    certificate_receipt_reset(receipt_out);
    if (!boundary || !receipt_out) {
        return CETTA_NIK_HOSTED_CERTIFICATE_CHECK_INVALID_V1;
    }
    const CettaNikCertificateBoundaryV1 *descriptor = boundary->descriptor;
    receipt_out->theory_identity = descriptor->calculus->theory_identity;
    receipt_out->theory_revision = descriptor->calculus->theory_revision;
    receipt_out->calculus_identity = descriptor->calculus->calculus_identity;
    receipt_out->calculus_revision =
        descriptor->calculus->calculus_revision;
    receipt_out->boundary_identity = descriptor->authority->realization_identity;
    receipt_out->boundary_abi = descriptor->authority->realization_abi;
    receipt_out->adequacy_identity = descriptor->adequacy_identity;
    receipt_out->adequacy_revision = descriptor->adequacy_revision;
    receipt_out->policy_identity = descriptor->policy_identity;
    receipt_out->authority = boundary->admitted_token;
    if (!cetta_nik_hosted_certificate_is_current_v1(boundary)) {
        receipt_out->kind = CETTA_NIK_HOSTED_CERTIFICATE_CHECK_STALE_V1;
        return receipt_out->kind;
    }
    CettaNikResultV1 result = engine_fault_result();
    if (!descriptor->check(
            boundary->checker_state, claim, certificate,
            language_receipt_out, &result) ||
        !cetta_nik_result_v1_is_valid(result)) {
        receipt_out->kind =
            CETTA_NIK_HOSTED_CERTIFICATE_CHECK_ENGINE_FAULT_V1;
        return receipt_out->kind;
    }
    if (!cetta_nik_hosted_certificate_is_current_v1(boundary)) {
        receipt_out->kind = CETTA_NIK_HOSTED_CERTIFICATE_CHECK_STALE_V1;
        return receipt_out->kind;
    }
    receipt_out->kind = CETTA_NIK_HOSTED_CERTIFICATE_CHECK_RESULT_V1;
    receipt_out->result = result;
    return receipt_out->kind;
}
