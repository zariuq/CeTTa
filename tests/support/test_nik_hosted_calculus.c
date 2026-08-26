#include "nik_hosted_calculus.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

enum {
    CANARY_OPERATION_OBSERVE = 100u,
    CANARY_OPERATION_ADVANCE = 200u,
};

typedef struct {
    const CettaNikDirectAuthorityV1 *authority;
    char digest[65];
    uint32_t policy_identity;
} CanaryScope;

typedef struct {
    uint32_t maximum_value;
} CanaryNativeContext;

typedef struct {
    uint32_t before;
    uint32_t delta;
    CanaryScope *scope_to_invalidate;
} CanaryAdvanceRequest;

typedef struct {
    uint32_t before;
    uint32_t delta;
    uint32_t after;
} CanaryNativeEvidence;

typedef struct {
    unsigned checks;
} CanaryCertificateState;

typedef struct {
    uint32_t expected_after;
} CanaryClaim;

typedef struct {
    uint32_t before;
    uint32_t delta;
    uint32_t after;
} CanaryCertificate;

typedef struct {
    bool checked;
    bool arithmetic_agrees;
    bool claim_agrees;
} CanaryCertificateEvidence;

static const char canary_digest[] =
    "0123456789abcdef0123456789abcdef"
    "0123456789abcdef0123456789abcdef";

static const CettaNikDirectAuthorityV1 canary_native_authority = {
    .alias = "canary-native",
    .system_id = "canary.counter",
    .authority_identity = 0x102u,
    .realization_identity = 0x201u,
    .authority_revision = 1u,
    .realization_abi = 1u,
};

static const CettaNikDirectAuthorityV1 canary_certificate_authority = {
    .alias = "canary-certificate",
    .system_id = "canary.counter",
    .authority_identity = 0x102u,
    .realization_identity = 0x301u,
    .authority_revision = 1u,
    .realization_abi = 1u,
};

static bool canary_snapshot(
    const void *scope_state,
    CettaNikDirectAuthorityTokenV1 *token_out) {
    const CanaryScope *scope = scope_state;
    return scope && scope->authority &&
        cetta_nik_direct_authority_v1_token_from_sha256(
            scope->authority, scope->digest, scope->policy_identity,
            token_out);
}

static bool canary_native_run(
    const void *implementation_context,
    CettaNikNativeOperationIdV1 operation_identity,
    const void *request,
    void *language_receipt_out,
    CettaNikResultV1 *result_out) {
    const CanaryNativeContext *context = implementation_context;
    const CanaryAdvanceRequest *advance = request;
    CanaryNativeEvidence *evidence = language_receipt_out;
    if (!context || !advance || !evidence || !result_out)
        return false;
    if (operation_identity != CANARY_OPERATION_OBSERVE &&
        operation_identity != CANARY_OPERATION_ADVANCE) {
        return false;
    }
    uint32_t delta = operation_identity == CANARY_OPERATION_ADVANCE
        ? advance->delta : 0u;
    if (context->maximum_value - advance->before < delta)
        return false;
    *evidence = (CanaryNativeEvidence){
        .before = advance->before,
        .delta = delta,
        .after = advance->before + delta,
    };
    if (advance->scope_to_invalidate) {
        CanaryScope *scope = advance->scope_to_invalidate;
        scope->digest[0] = scope->digest[0] == '0' ? '1' : '0';
    }
    *result_out = cetta_nik_result_v1_outcome(
        CETTA_NIK_OUTCOME_ESTABLISHED);
    return true;
}

static bool canary_invalid_native_run(
    const void *implementation_context,
    CettaNikNativeOperationIdV1 operation_identity,
    const void *request,
    void *language_receipt_out,
    CettaNikResultV1 *result_out) {
    (void)implementation_context;
    (void)operation_identity;
    (void)request;
    (void)language_receipt_out;
    if (!result_out)
        return false;
    *result_out = (CettaNikResultV1){.kind = (CettaNikResultKindV1)99};
    return true;
}

static bool canary_certificate_check(
    void *checker_state,
    const void *claim_value,
    const void *certificate_value,
    void *language_receipt_out,
    CettaNikResultV1 *result_out) {
    CanaryCertificateState *state = checker_state;
    const CanaryClaim *claim = claim_value;
    const CanaryCertificate *certificate = certificate_value;
    CanaryCertificateEvidence *evidence = language_receipt_out;
    if (!state || !claim || !certificate || !evidence || !result_out)
        return false;
    state->checks++;
    bool arithmetic_agrees =
        UINT32_MAX - certificate->before >= certificate->delta &&
        certificate->before + certificate->delta == certificate->after;
    bool claim_agrees = certificate->after == claim->expected_after;
    *evidence = (CanaryCertificateEvidence){
        .checked = true,
        .arithmetic_agrees = arithmetic_agrees,
        .claim_agrees = claim_agrees,
    };
    *result_out = cetta_nik_result_v1_outcome(
        arithmetic_agrees && claim_agrees
            ? CETTA_NIK_OUTCOME_ESTABLISHED
            : CETTA_NIK_OUTCOME_REFUTED);
    return true;
}

static CettaNikNativeCalculusSelectionV1 select_calculus(
    const CettaNikNativeCalculusV1 *calculi,
    size_t calculus_count,
    const CettaNikNativeCalculusUpgradeV1 *upgrades,
    size_t upgrade_count,
    const CettaNikCalculusCapabilityIdV1 *required,
    size_t required_count,
    size_t *frontier,
    size_t frontier_capacity) {
    CettaNikNativeCalculusFamilyV1 family = {
        .calculi = calculi,
        .calculus_count = calculus_count,
        .upgrades = upgrades,
        .upgrade_count = upgrade_count,
    };
    CettaNikNativeCalculusRequestV1 request = {
        .required_capabilities = required,
        .required_capability_count = required_count,
    };
    return cetta_nik_native_calculus_select_v1(
        &family, &request, frontier, frontier_capacity);
}

int main(void) {
    static const CettaNikCalculusCapabilityIdV1 observe_caps[] = {10u};
    static const CettaNikCalculusCapabilityIdV1 constructive_caps[] = {
        10u, 20u,
    };
    static const CettaNikNativeOperationV1 observe_operations[] = {
        {CANARY_OPERATION_OBSERVE},
    };
    static const CettaNikNativeOperationV1 constructive_operations[] = {
        {CANARY_OPERATION_OBSERVE},
        {CANARY_OPERATION_ADVANCE},
    };
    static const CettaNikNativeCalculusV1 calculi[] = {
        {
            .system_id = "canary.counter",
            .theory_identity = 0x11u,
            .theory_revision = 1u,
            .calculus_identity = 0x101u,
            .calculus_revision = 1u,
            .judgment_family_identity = 0x501u,
            .evidence_family_identity = 0x601u,
            .computation_identity = 0x701u,
            .capabilities = observe_caps,
            .capability_count = 1u,
            .operations = observe_operations,
            .operation_count = 1u,
        },
        {
            .system_id = "canary.counter",
            .theory_identity = 0x11u,
            .theory_revision = 1u,
            .calculus_identity = 0x102u,
            .calculus_revision = 1u,
            .judgment_family_identity = 0x502u,
            .evidence_family_identity = 0x602u,
            .computation_identity = 0x702u,
            .capabilities = constructive_caps,
            .capability_count = 2u,
            .operations = constructive_operations,
            .operation_count = 2u,
        },
    };
    static const CettaNikNativeCalculusUpgradeV1 upgrade[] = {{0u, 1u}};
    size_t frontier[2] = {SIZE_MAX, SIZE_MAX};
    CettaNikNativeCalculusSelectionV1 selected = select_calculus(
        calculi, 2u, upgrade, 1u, observe_caps, 1u, frontier, 2u);
    CHECK(
        selected.status == CETTA_NIK_CALCULUS_SELECTION_STATUS_OK_V1 &&
            selected.kind ==
                CETTA_NIK_CALCULUS_SELECTION_UNIQUE_GREATEST_V1 &&
            selected.eligible_count == 2u &&
            selected.frontier_count == 1u &&
            selected.greatest_index == 1u && frontier[0] == 1u,
        "an admitted semantic extension selects the strongest native calculus");

    selected = select_calculus(
        calculi, 2u, NULL, 0u, observe_caps, 1u, frontier, 2u);
    CHECK(
        selected.status == CETTA_NIK_CALCULUS_SELECTION_STATUS_OK_V1 &&
            selected.kind ==
                CETTA_NIK_CALCULUS_SELECTION_MAXIMAL_FRONTIER_V1 &&
            selected.frontier_count == 2u,
        "without a calculus-upgrade law NIK retains the incomparable frontier");

    CettaNikNativeCalculusV1 trace_only = calculi[1];
    trace_only.calculus_identity = 0x103u;
    trace_only.computation_identity = 0u;
    CHECK(
        !cetta_nik_native_calculus_v1_is_valid(&trace_only),
        "a trace boundary without native computation is not a native calculus");

    CettaNikNativeCalculusV1 foreign = calculi[1];
    foreign.system_id = "foreign.system";
    foreign.theory_identity = 0x22u;
    foreign.calculus_identity = 0x104u;
    CettaNikNativeCalculusV1 mixed[] = {calculi[0], foreign};
    selected = select_calculus(
        mixed, 2u, NULL, 0u, NULL, 0u, frontier, 2u);
    CHECK(
        selected.status ==
            CETTA_NIK_CALCULUS_SELECTION_STATUS_INVALID_FAMILY_V1,
        "maximality is local to one hosted theory revision");

    CanaryScope native_scope = {
        .authority = &canary_native_authority,
        .policy_identity = 17u,
    };
    memcpy(native_scope.digest, canary_digest, sizeof(canary_digest));
    const CanaryNativeContext native_context = {
        .maximum_value = UINT32_MAX,
    };
    const CettaNikNativeImplementationLicenseV1 native_license = {
        .calculus = &calculi[1],
        .authority = &canary_native_authority,
        .correspondence_identity = 0x801u,
        .correspondence_revision = 1u,
        .policy_identity = 17u,
        .snapshot = canary_snapshot,
        .run = canary_native_run,
    };
    CHECK(
        cetta_nik_native_implementation_license_v1_is_valid(
            &native_license),
        "a faithful implementation license is tied to the selected calculus");
    CettaNikHostedNativeAdmissionV1 admitted =
        cetta_nik_hosted_native_admit_v1(
            &native_license, &native_scope, &native_context);
    CHECK(
        admitted.kind == CETTA_NIK_HOST_ADMISSION_ADMITTED_V1 &&
            admitted.instance &&
            cetta_nik_hosted_native_is_current_v1(admitted.instance),
        "a native implementation is hosted only at an exact current source scope");

    CanaryAdvanceRequest advance = {.before = 4u, .delta = 3u};
    CanaryNativeEvidence native_evidence = {0};
    CettaNikHostedNativeReceiptV1 native_receipt;
    CettaNikHostedNativeCallKindV1 native_call =
        cetta_nik_hosted_native_run_v1(
            admitted.instance, CANARY_OPERATION_ADVANCE, &advance,
            &native_evidence, &native_receipt);
    CHECK(
        native_call == CETTA_NIK_HOSTED_NATIVE_CALL_RESULT_V1 &&
            native_receipt.result.kind == CETTA_NIK_RESULT_OUTCOME &&
            native_receipt.result.value.outcome ==
                CETTA_NIK_OUTCOME_ESTABLISHED &&
            native_evidence.before == 4u &&
            native_evidence.delta == 3u && native_evidence.after == 7u &&
            native_receipt.calculus_identity == 0x102u &&
            native_receipt.implementation_identity == 0x201u &&
            native_receipt.correspondence_identity == 0x801u,
        "native hosting executes the calculus and retains construction evidence");

    CanaryNativeEvidence evidence_before = native_evidence;
    native_call = cetta_nik_hosted_native_run_v1(
        admitted.instance, 999u, &advance,
        &native_evidence, &native_receipt);
    CHECK(
        native_call == CETTA_NIK_HOSTED_NATIVE_CALL_OUTSIDE_CALCULUS_V1 &&
            memcmp(&native_evidence, &evidence_before,
                sizeof(native_evidence)) == 0,
        "an unknown operation cannot enter the hosted native calculus");

    native_scope.digest[0] = '1';
    native_call = cetta_nik_hosted_native_run_v1(
        admitted.instance, CANARY_OPERATION_ADVANCE, &advance,
        &native_evidence, &native_receipt);
    CHECK(
        native_call == CETTA_NIK_HOSTED_NATIVE_CALL_STALE_V1 &&
            memcmp(&native_evidence, &evidence_before,
                sizeof(native_evidence)) == 0,
        "a stale source scope cannot execute an admitted native operation");
    native_scope.digest[0] = '0';

    CanaryAdvanceRequest invalidating_advance = {
        .before = 7u,
        .delta = 2u,
        .scope_to_invalidate = &native_scope,
    };
    native_call = cetta_nik_hosted_native_run_v1(
        admitted.instance, CANARY_OPERATION_ADVANCE, &invalidating_advance,
        &native_evidence, &native_receipt);
    CHECK(
        native_call == CETTA_NIK_HOSTED_NATIVE_CALL_STALE_V1 &&
            native_evidence.before == 7u && native_evidence.delta == 2u &&
            native_evidence.after == 9u,
        "a scope invalidated during execution cannot publish a hosted result");
    native_scope.digest[0] = '0';

    const CettaNikNativeImplementationLicenseV1 invalid_result_license = {
        .calculus = &calculi[1],
        .authority = &canary_native_authority,
        .correspondence_identity = 0x802u,
        .correspondence_revision = 1u,
        .policy_identity = 17u,
        .snapshot = canary_snapshot,
        .run = canary_invalid_native_run,
    };
    CettaNikHostedNativeAdmissionV1 invalid_result_admission =
        cetta_nik_hosted_native_admit_v1(
            &invalid_result_license, &native_scope, &native_context);
    native_call = cetta_nik_hosted_native_run_v1(
        invalid_result_admission.instance, CANARY_OPERATION_OBSERVE, &advance,
        &native_evidence, &native_receipt);
    CHECK(
        native_call == CETTA_NIK_HOSTED_NATIVE_CALL_ENGINE_FAULT_V1,
        "an invalid implementation result cannot masquerade as a semantic outcome");

    CanaryScope certificate_scope = {
        .authority = &canary_certificate_authority,
        .policy_identity = 19u,
    };
    memcpy(
        certificate_scope.digest, canary_digest, sizeof(canary_digest));
    CanaryCertificateState certificate_state = {0};
    const CettaNikCertificateBoundaryV1 certificate_boundary = {
        .calculus = &calculi[1],
        .authority = &canary_certificate_authority,
        .adequacy_identity = 0x901u,
        .adequacy_revision = 1u,
        .policy_identity = 19u,
        .snapshot = canary_snapshot,
        .check = canary_certificate_check,
    };
    CHECK(
        cetta_nik_certificate_boundary_v1_is_valid(&certificate_boundary),
        "a certificate checker is admitted as an explicit boundary");
    CettaNikHostedCertificateAdmissionV1 certificate_admission =
        cetta_nik_hosted_certificate_admit_v1(
            &certificate_boundary, &certificate_scope, &certificate_state);
    CHECK(
        certificate_admission.kind ==
            CETTA_NIK_HOST_ADMISSION_ADMITTED_V1 &&
            certificate_admission.boundary &&
            cetta_nik_hosted_certificate_is_current_v1(
                certificate_admission.boundary),
        "the certificate boundary retains independent currentness");

    CanaryClaim claim = {.expected_after = 7u};
    CanaryCertificate certificate = {
        .before = 4u,
        .delta = 3u,
        .after = 7u,
    };
    CanaryCertificateEvidence certificate_evidence = {0};
    CettaNikHostedCertificateReceiptV1 certificate_receipt;
    CettaNikHostedCertificateCheckKindV1 certificate_call =
        cetta_nik_hosted_certificate_check_v1(
            certificate_admission.boundary, &claim, &certificate,
            &certificate_evidence, &certificate_receipt);
    CHECK(
        certificate_call ==
                CETTA_NIK_HOSTED_CERTIFICATE_CHECK_RESULT_V1 &&
            certificate_receipt.result.kind == CETTA_NIK_RESULT_OUTCOME &&
            certificate_receipt.result.value.outcome ==
                CETTA_NIK_OUTCOME_ESTABLISHED &&
            certificate_evidence.checked &&
            certificate_evidence.arithmetic_agrees &&
            certificate_evidence.claim_agrees &&
            certificate_state.checks == 1u &&
            native_evidence.after == 9u &&
            certificate_receipt.boundary_identity == 0x301u &&
            certificate_receipt.adequacy_identity == 0x901u,
        "certificate checking validates supplied evidence without executing the calculus");

    certificate.after = 8u;
    certificate_call = cetta_nik_hosted_certificate_check_v1(
        certificate_admission.boundary, &claim, &certificate,
        &certificate_evidence, &certificate_receipt);
    CHECK(
        certificate_call ==
                CETTA_NIK_HOSTED_CERTIFICATE_CHECK_RESULT_V1 &&
            certificate_receipt.result.value.outcome ==
                CETTA_NIK_OUTCOME_REFUTED &&
            certificate_evidence.checked &&
            !certificate_evidence.arithmetic_agrees,
        "certificate refutation carries a checked boundary obstruction");

    certificate_scope.digest[0] = '1';
    unsigned checks_before = certificate_state.checks;
    certificate_call = cetta_nik_hosted_certificate_check_v1(
        certificate_admission.boundary, &claim, &certificate,
        &certificate_evidence, &certificate_receipt);
    CHECK(
        certificate_call ==
                CETTA_NIK_HOSTED_CERTIFICATE_CHECK_STALE_V1 &&
            certificate_state.checks == checks_before,
        "a stale certificate boundary abstains without invoking the checker");

    CettaNikDirectAuthorityV1 mismatched_authority =
        canary_native_authority;
    mismatched_authority.authority_identity = 0x999u;
    CettaNikNativeImplementationLicenseV1 mismatched_license = native_license;
    mismatched_license.authority = &mismatched_authority;
    CHECK(
        !cetta_nik_native_implementation_license_v1_is_valid(
            &mismatched_license),
        "an implementation cannot inherit authority from another calculus");

    cetta_nik_hosted_certificate_destroy_v1(
        certificate_admission.boundary);
    cetta_nik_hosted_native_destroy_v1(invalid_result_admission.instance);
    cetta_nik_hosted_native_destroy_v1(admitted.instance);

    printf(
        "NikHostedCalculusSummary checks=%u failures=%u\n",
        checks, failures);
    return failures == 0u ? 0 : 1;
}
