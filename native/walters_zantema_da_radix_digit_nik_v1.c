#include "walters_zantema_da_radix_digit_nik_v1.h"

#include "native_sha256.h"

#include <stdlib.h>
#include <string.h>

#define WALTERS_ZANTEMA_DA_RADIX_DIGIT_NIK_V1_POLICY_ID UINT32_C(1)

struct CettaWaltersZantemaDaRadixDigitNikV1 {
    const CettaOperationalLanguageDefV1 *source;
    const CettaOperationalLanguageDefV1 *target;
    CettaWaltersZantemaDaRadixDigitV1Program program;
    CettaNikNativeImplementationLicenseV1 license;
    CettaNikHostedNativeInstanceV1 *instance;
};

static const CettaNikCalculusCapabilityIdV1 walters_zantema_da_radix_digit_capabilities[] = {
    CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_NIK_V1_CAPABILITY_CANONICAL_DIGITS,
    CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_NIK_V1_CAPABILITY_ORDERED_PROVENANCE,
    CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_NIK_V1_CAPABILITY_ADDITION,
    CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_NIK_V1_CAPABILITY_MULTIPLICATION,
};

static const CettaNikNativeOperationV1 walters_zantema_da_radix_digit_operations[] = {
    {CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_NIK_V1_OPERATION_ADD},
    {CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_NIK_V1_OPERATION_MULTIPLY},
};

static const CettaNikNativeCalculusV1 walters_zantema_da_radix_digit_calculus = {
    .system_id = "cetta.walters-zantema-da-natural",
    .theory_identity = UINT64_C(0x44414e4154523231),
    .theory_revision = 1u,
    .calculus_identity = UINT64_C(0x444143314e415431),
    .calculus_revision = 1u,
    .judgment_family_identity = UINT64_C(0x444143314a554431),
    .evidence_family_identity = UINT64_C(0x4441433145564931),
    .computation_identity = UINT64_C(0x44414331434f4d31),
    .capabilities = walters_zantema_da_radix_digit_capabilities,
    .capability_count = sizeof(walters_zantema_da_radix_digit_capabilities) /
        sizeof(walters_zantema_da_radix_digit_capabilities[0]),
    .operations = walters_zantema_da_radix_digit_operations,
    .operation_count = sizeof(walters_zantema_da_radix_digit_operations) /
        sizeof(walters_zantema_da_radix_digit_operations[0]),
};

static const CettaNikDirectAuthorityV1 walters_zantema_da_radix_digit_authority = {
    .alias = "walters-zantema-da-via-radix-digit",
    .system_id = "cetta.walters-zantema-da-natural",
    .authority_identity = UINT64_C(0x444143314e415431),
    .realization_identity = UINT64_C(0x44414331434f4d50),
    .authority_revision = 1u,
    .realization_abi = 1u,
};

static bool digest_valid(const char digest[65]) {
    size_t index;
    if (!digest || digest[64] != '\0')
        return false;
    for (index = 0u; index < 64u; ++index) {
        char character = digest[index];
        if (!((character >= '0' && character <= '9') ||
                (character >= 'a' && character <= 'f')))
            return false;
    }
    return true;
}

static bool combined_scope_digest(
    const CettaWaltersZantemaDaRadixDigitNikV1 *host,
    char digest_out[65]) {
    static const uint8_t domain[] = "cetta.walters-zantema-da-radix-digit.scope.v1";
    static const uint8_t source_label[] = "source";
    static const uint8_t target_label[] = "target";
    CettaNativeSha256 sha;

    if (!host || !host->source || !host->target || !digest_out ||
            !digest_valid(host->source->source_sha256) ||
            !digest_valid(host->target->source_sha256))
        return false;
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(&sha, domain, sizeof(domain));
    cetta_native_sha256_update(&sha, source_label, sizeof(source_label));
    cetta_native_sha256_update(&sha,
        (const uint8_t *)host->source->source_sha256, 64u);
    cetta_native_sha256_update(&sha, target_label, sizeof(target_label));
    cetta_native_sha256_update(&sha,
        (const uint8_t *)host->target->source_sha256, 64u);
    cetta_native_sha256_finish_hex(&sha, digest_out);
    return true;
}

static bool walters_zantema_da_radix_digit_snapshot(
    const void *scope_state,
    CettaNikDirectAuthorityTokenV1 *token_out) {
    const CettaWaltersZantemaDaRadixDigitNikV1 *host = scope_state;
    char digest[65];
    return combined_scope_digest(host, digest) &&
        cetta_nik_direct_authority_v1_token_from_sha256(
            &walters_zantema_da_radix_digit_authority, digest, WALTERS_ZANTEMA_DA_RADIX_DIGIT_NIK_V1_POLICY_ID, token_out);
}

static bool walters_zantema_da_radix_digit_run(
    const void *implementation_context,
    CettaNikNativeOperationIdV1 operation_identity,
    const void *request_value,
    void *language_receipt_value,
    CettaNikResultV1 *result_out) {
    const CettaWaltersZantemaDaRadixDigitNikV1 *host = implementation_context;
    const CettaWaltersZantemaDaRadixDigitNikV1Request *request = request_value;
    CettaWaltersZantemaDaRadixDigitNikV1LanguageReceipt *receipt = language_receipt_value;
    const CettaRadixDigitV1Program *target_program;

    if (!host || !request || !receipt || !result_out)
        return false;
    if (operation_identity == CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_NIK_V1_OPERATION_ADD)
        target_program = &host->program.addition_program;
    else if (operation_identity == CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_NIK_V1_OPERATION_MULTIPLY)
        target_program = &host->program.multiplication_program;
    else
        return false;
    if (!cetta_radix_digit_v1_execute(&receipt->execution,
            host->program.radix, &host->program.target_profile,
            target_program,
            request->first, request->first_len,
            request->second, request->second_len,
            request->output_limit, request->fuel))
        return false;

    switch (receipt->execution.kind) {
    case CETTA_RADIX_DIGIT_V1_OUTCOME_VALUE:
        *result_out = cetta_nik_result_v1_outcome(
            CETTA_NIK_OUTCOME_ESTABLISHED);
        return true;
    case CETTA_RADIX_DIGIT_V1_OUTCOME_LANGUAGE_FAULT:
        *result_out = cetta_nik_result_v1_outcome(
            CETTA_NIK_OUTCOME_REFUTED);
        return true;
    case CETTA_RADIX_DIGIT_V1_OUTCOME_RESOURCE_FAULT:
        *result_out = cetta_nik_result_v1_outcome(
            CETTA_NIK_OUTCOME_INCOMPLETE);
        return true;
    case CETTA_RADIX_DIGIT_V1_OUTCOME_ENGINE_FAULT:
        *result_out = cetta_nik_result_v1_engine_fault(
            CETTA_NIK_ENGINE_FAULT_UNAVAILABLE);
        return true;
    }
    return false;
}

void cetta_walters_zantema_da_radix_digit_nik_v1_language_receipt_init(
    CettaWaltersZantemaDaRadixDigitNikV1LanguageReceipt *receipt) {
    if (receipt)
        cetta_radix_digit_v1_run_result_init(&receipt->execution);
}

void cetta_walters_zantema_da_radix_digit_nik_v1_language_receipt_free(
    CettaWaltersZantemaDaRadixDigitNikV1LanguageReceipt *receipt) {
    if (receipt)
        cetta_radix_digit_v1_run_result_free(&receipt->execution);
}

CettaWaltersZantemaDaRadixDigitNikV1Admission cetta_walters_zantema_da_radix_digit_nik_v1_admit(
    const CettaOperationalLanguageDefV1 *source,
    const CettaOperationalLanguageDefV1 *target,
    char *error_buf,
    size_t error_buf_size) {
    CettaWaltersZantemaDaRadixDigitNikV1Admission result = {
        .kind = CETTA_NIK_HOST_ADMISSION_INVALID_V1,
        .transform_status = CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_BAD_ARGUMENT,
    };
    CettaWaltersZantemaDaRadixDigitNikV1 *host;
    CettaNikHostedNativeAdmissionV1 hosted;

    if (!source || !target)
        return result;
    host = calloc(1u, sizeof(*host));
    if (!host) {
        result.kind = CETTA_NIK_HOST_ADMISSION_RESOURCE_FAULT_V1;
        result.transform_status = CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_ALLOCATION_FAILURE;
        return result;
    }
    host->source = source;
    host->target = target;
    cetta_walters_zantema_da_radix_digit_v1_program_init(&host->program);
    if (!cetta_walters_zantema_da_radix_digit_v1_transform(&host->program, source, target,
            &result.transform_status, error_buf, error_buf_size)) {
        if (result.transform_status == CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_ALLOCATION_FAILURE)
            result.kind = CETTA_NIK_HOST_ADMISSION_RESOURCE_FAULT_V1;
        cetta_walters_zantema_da_radix_digit_v1_program_free(&host->program);
        free(host);
        return result;
    }
    host->license = (CettaNikNativeImplementationLicenseV1){
        .calculus = &walters_zantema_da_radix_digit_calculus,
        .authority = &walters_zantema_da_radix_digit_authority,
        .correspondence_identity = UINT64_C(0x44414331434f5252),
        .correspondence_revision = 1u,
        .policy_identity = WALTERS_ZANTEMA_DA_RADIX_DIGIT_NIK_V1_POLICY_ID,
        .snapshot = walters_zantema_da_radix_digit_snapshot,
        .run = walters_zantema_da_radix_digit_run,
    };
    hosted = cetta_nik_hosted_native_admit_v1(
        &host->license, host, host);
    result.kind = hosted.kind;
    if (hosted.kind != CETTA_NIK_HOST_ADMISSION_ADMITTED_V1) {
        cetta_walters_zantema_da_radix_digit_v1_program_free(&host->program);
        free(host);
        return result;
    }
    host->instance = hosted.instance;
    result.host = host;
    return result;
}

void cetta_walters_zantema_da_radix_digit_nik_v1_destroy(CettaWaltersZantemaDaRadixDigitNikV1 *host) {
    if (!host)
        return;
    cetta_nik_hosted_native_destroy_v1(host->instance);
    cetta_walters_zantema_da_radix_digit_v1_program_free(&host->program);
    free(host);
}

bool cetta_walters_zantema_da_radix_digit_nik_v1_is_current(const CettaWaltersZantemaDaRadixDigitNikV1 *host) {
    return host && cetta_nik_hosted_native_is_current_v1(host->instance);
}

CettaNikHostedNativeCallKindV1 cetta_walters_zantema_da_radix_digit_nik_v1_run(
    CettaWaltersZantemaDaRadixDigitNikV1 *host,
    CettaNikNativeOperationIdV1 operation_identity,
    const CettaWaltersZantemaDaRadixDigitNikV1Request *request,
    CettaWaltersZantemaDaRadixDigitNikV1LanguageReceipt *language_receipt,
    CettaNikHostedNativeReceiptV1 *hosted_receipt) {
    return cetta_nik_hosted_native_run_v1(host ? host->instance : NULL,
        operation_identity, request, language_receipt, hosted_receipt);
}

const CettaNikNativeCalculusV1 *cetta_walters_zantema_da_radix_digit_nik_v1_calculus(void) {
    return &walters_zantema_da_radix_digit_calculus;
}
