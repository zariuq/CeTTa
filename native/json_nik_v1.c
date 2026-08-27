#include "json_nik_v1.h"

#include "native_sha256.h"

#include <stdlib.h>
#include <string.h>

#define JSON_NIK_V1_POLICY_ID UINT32_C(1)

typedef struct {
    const uint8_t *bytes;
    size_t length;
} JsonNikSourceV1;

struct CettaJsonNikV1 {
    JsonNikSourceV1 language;
    JsonNikSourceV1 profile;
    JsonNikSourceV1 target;
    CettaJsonRuntimeV1 *runtime;
    CettaJsonKernelV1 production_kernel;
    CettaNikNativeImplementationLicenseV1 license;
    CettaNikHostedNativeInstanceV1 *instance;
};

static const CettaNikCalculusCapabilityIdV1 json_capabilities[] = {
    CETTA_JSON_NIK_V1_CAPABILITY_EXACT_FOREST,
    CETTA_JSON_NIK_V1_CAPABILITY_EXACT_NUMBER_LEXEME,
    CETTA_JSON_NIK_V1_CAPABILITY_UNICODE_SCALARS,
    CETTA_JSON_NIK_V1_CAPABILITY_OCCURRENCE_IDENTITY,
    CETTA_JSON_NIK_V1_CAPABILITY_SOURCE_SPANS,
};

static const CettaNikNativeOperationV1 json_operations[] = {
    {CETTA_JSON_NIK_V1_OPERATION_PARSE},
};

static const CettaNikNativeCalculusV1 json_calculus = {
    .system_id = "cetta.rfc8259-json",
    .theory_identity = UINT64_C(0x4a534f4e54485231),
    .theory_revision = 1u,
    .calculus_identity = UINT64_C(0x4a534f4e43414c31),
    .calculus_revision = 1u,
    .judgment_family_identity = UINT64_C(0x4a534f4e4a554431),
    .evidence_family_identity = UINT64_C(0x4a534f4e45564931),
    .computation_identity = UINT64_C(0x4a534f4e434f4d31),
    .capabilities = json_capabilities,
    .capability_count = sizeof(json_capabilities) /
        sizeof(json_capabilities[0]),
    .operations = json_operations,
    .operation_count = sizeof(json_operations) / sizeof(json_operations[0]),
};

static const CettaNikDirectAuthorityV1 json_authority = {
    .alias = "rfc8259-json-via-parserpack-dual",
    .system_id = "cetta.rfc8259-json",
    .authority_identity = UINT64_C(0x4a534f4e43414c31),
    .realization_identity = UINT64_C(0x4a534f4e4455414c),
    .authority_revision = 1u,
    .realization_abi = 1u,
};

static void sha_u64(CettaNativeSha256 *sha, uint64_t value) {
    uint8_t encoded[8];
    size_t index;
    for (index = 0u; index < sizeof(encoded); index++) {
        encoded[sizeof(encoded) - index - 1u] = (uint8_t)(value & 0xffu);
        value >>= 8u;
    }
    cetta_native_sha256_update(sha, encoded, sizeof(encoded));
}

static bool sha_source(CettaNativeSha256 *sha, const char *label,
                       JsonNikSourceV1 source) {
    size_t label_length;
    if (!sha || !label || (!source.bytes && source.length != 0u))
        return false;
    label_length = strlen(label);
    sha_u64(sha, (uint64_t)label_length);
    cetta_native_sha256_update(
        sha, (const uint8_t *)label, label_length);
    sha_u64(sha, (uint64_t)source.length);
    cetta_native_sha256_update(sha, source.bytes, source.length);
    return true;
}

static bool json_scope_digest(
    const CettaJsonNikV1 *host, char digest_out[65]) {
    static const uint8_t domain[] = "cetta.json-nik.scope.v1";
    CettaNativeSha256 sha;
    if (!host || !digest_out)
        return false;
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(&sha, domain, sizeof(domain));
    if (!sha_source(&sha, "language", host->language) ||
        !sha_source(&sha, "profile", host->profile) ||
        !sha_source(&sha, "target", host->target)) {
        return false;
    }
    cetta_native_sha256_finish_hex(&sha, digest_out);
    return true;
}

static bool json_snapshot(
    const void *scope_state,
    CettaNikDirectAuthorityTokenV1 *token_out) {
    const CettaJsonNikV1 *host = scope_state;
    char digest[65];
    return json_scope_digest(host, digest) &&
        cetta_nik_direct_authority_v1_token_from_sha256(
            &json_authority, digest, JSON_NIK_V1_POLICY_ID, token_out);
}

static bool json_native_run(
    const void *implementation_context,
    CettaNikNativeOperationIdV1 operation_identity,
    const void *request_value,
    void *language_receipt_value,
    CettaNikResultV1 *result_out) {
    const CettaJsonNikV1 *host = implementation_context;
    const CettaJsonNikV1Request *request = request_value;
    CettaJsonNikV1LanguageReceipt *receipt = language_receipt_value;
    CettaJsonRuntimeV1Limits limits;
    CettaJsonRuntimeV1Status status = CETTA_JSON_RUNTIME_V1_BAD_ARGUMENT;
    Atom *value = NULL;
    char error[256] = {0};
    bool accepted;

    if (!host || !host->runtime || !request || !request->arena ||
        (!request->json_bytes && request->json_byte_len != 0u) ||
        !receipt || !result_out ||
        operation_identity != CETTA_JSON_NIK_V1_OPERATION_PARSE) {
        return false;
    }
    if (request->limits)
        limits = *request->limits;
    else
        cetta_json_runtime_v1_default_limits(&limits);
    /* NIK exposes the strongest qualified implementation here. The direct
     * runtime API remains the explicit lower-level GLL-only fallback. */
    limits.kernel = CETTA_JSON_KERNEL_V1_PACKED_GLL_GLR_DUAL;
    accepted = cetta_json_runtime_v1_parse(
        host->runtime, request->arena,
        request->json_bytes, request->json_byte_len,
        &limits, &value, &status, error, sizeof(error));
    receipt->status = status;
    receipt->kernel = limits.kernel;
    receipt->value = accepted ? value : NULL;

    if (accepted && status == CETTA_JSON_RUNTIME_V1_OK) {
        *result_out = cetta_nik_result_v1_outcome(
            CETTA_NIK_OUTCOME_ESTABLISHED);
        return true;
    }
    switch (status) {
    case CETTA_JSON_RUNTIME_V1_INVALID_UTF8:
    case CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED:
    case CETTA_JSON_RUNTIME_V1_INVALID_UNICODE_ESCAPE:
        *result_out = cetta_nik_result_v1_outcome(
            CETTA_NIK_OUTCOME_REFUTED);
        return true;
    case CETTA_JSON_RUNTIME_V1_RESOURCE_LIMIT:
        *result_out = cetta_nik_result_v1_outcome(
            CETTA_NIK_OUTCOME_INCOMPLETE);
        return true;
    case CETTA_JSON_RUNTIME_V1_OK:
    case CETTA_JSON_RUNTIME_V1_BAD_ARGUMENT:
    case CETTA_JSON_RUNTIME_V1_INVALID_LANGUAGE_SOURCE:
    case CETTA_JSON_RUNTIME_V1_INVALID_TARGET_SOURCE:
    case CETTA_JSON_RUNTIME_V1_OUTSIDE_LANGUAGE_FRAGMENT:
    case CETTA_JSON_RUNTIME_V1_OUTSIDE_ELABORATION_PROFILE:
    case CETTA_JSON_RUNTIME_V1_PREPARATION_FAILURE:
    case CETTA_JSON_RUNTIME_V1_AMBIGUOUS:
    case CETTA_JSON_RUNTIME_V1_BACKEND_DISAGREEMENT:
    case CETTA_JSON_RUNTIME_V1_MALFORMED_VALUE:
    case CETTA_JSON_RUNTIME_V1_ALLOCATION_FAILURE:
    case CETTA_JSON_RUNTIME_V1_INTERNAL_FAILURE:
        *result_out = cetta_nik_result_v1_engine_fault(
            CETTA_NIK_ENGINE_FAULT_UNAVAILABLE);
        return true;
    }
    return false;
}

void cetta_json_nik_v1_language_receipt_init(
    CettaJsonNikV1LanguageReceipt *receipt) {
    if (!receipt)
        return;
    receipt->status = CETTA_JSON_RUNTIME_V1_BAD_ARGUMENT;
    receipt->kernel = CETTA_JSON_KERNEL_V1_PACKED_GLL_GLR_DUAL;
    receipt->value = NULL;
}

CettaJsonNikV1Admission cetta_json_nik_v1_admit(
    const uint8_t *language_source,
    size_t language_source_len,
    const uint8_t *profile_source,
    size_t profile_source_len,
    const uint8_t *target_source,
    size_t target_source_len,
    char *error_buf,
    size_t error_buf_size) {
    CettaJsonNikV1Admission result = {
        .kind = CETTA_NIK_HOST_ADMISSION_INVALID_V1,
        .host = NULL,
    };
    CettaJsonNikV1 *host;
    CettaNikHostedNativeAdmissionV1 hosted;

    if (!language_source || language_source_len == 0u ||
        !profile_source || profile_source_len == 0u ||
        !target_source || target_source_len == 0u) {
        return result;
    }
    host = (CettaJsonNikV1 *)calloc(1u, sizeof(*host));
    if (!host) {
        result.kind = CETTA_NIK_HOST_ADMISSION_RESOURCE_FAULT_V1;
        return result;
    }
    host->language = (JsonNikSourceV1){language_source, language_source_len};
    host->profile = (JsonNikSourceV1){profile_source, profile_source_len};
    host->target = (JsonNikSourceV1){target_source, target_source_len};
    /* Both prepared backends have exact forest qualification.  GLL is the
     * measured single-backend production realization; the audited NIK call
     * retains dual GLL/GLR comparison. */
    host->production_kernel = CETTA_JSON_KERNEL_V1_PACKED_GLL;
    host->runtime = cetta_json_runtime_v1_new(
        language_source, language_source_len,
        profile_source, profile_source_len,
        target_source, target_source_len,
        error_buf, error_buf_size);
    if (!host->runtime) {
        if (error_buf && strstr(error_buf, "out of memory"))
            result.kind = CETTA_NIK_HOST_ADMISSION_RESOURCE_FAULT_V1;
        free(host);
        return result;
    }
    host->license = (CettaNikNativeImplementationLicenseV1){
        .calculus = &json_calculus,
        .authority = &json_authority,
        .correspondence_identity = UINT64_C(0x4a534f4e434f5252),
        .correspondence_revision = 1u,
        .policy_identity = JSON_NIK_V1_POLICY_ID,
        .snapshot = json_snapshot,
        .run = json_native_run,
    };
    hosted = cetta_nik_hosted_native_admit_v1(
        &host->license, host, host);
    result.kind = hosted.kind;
    if (hosted.kind != CETTA_NIK_HOST_ADMISSION_ADMITTED_V1) {
        cetta_json_runtime_v1_free(host->runtime);
        free(host);
        return result;
    }
    host->instance = hosted.instance;
    result.host = host;
    return result;
}

void cetta_json_nik_v1_destroy(CettaJsonNikV1 *host) {
    if (!host)
        return;
    cetta_nik_hosted_native_destroy_v1(host->instance);
    cetta_json_runtime_v1_free(host->runtime);
    free(host);
}

bool cetta_json_nik_v1_is_current(const CettaJsonNikV1 *host) {
    return host && cetta_nik_hosted_native_is_current_v1(host->instance);
}

bool cetta_json_nik_v1_parse_prepared(
    const CettaJsonNikV1 *host,
    Arena *arena,
    const uint8_t *json_bytes,
    size_t json_byte_len,
    const CettaJsonRuntimeV1Limits *limits,
    Atom **out,
    CettaJsonRuntimeV1Status *status,
    char *error_buf,
    size_t error_buf_size) {
    CettaJsonRuntimeV1Limits selected_limits;
    if (!host || !host->instance || !host->runtime) {
        if (error_buf && error_buf_size > 0u)
            error_buf[0] = '\0';
        if (status)
            *status = CETTA_JSON_RUNTIME_V1_BAD_ARGUMENT;
        return false;
    }
    if (limits)
        selected_limits = *limits;
    else
        cetta_json_runtime_v1_default_limits(&selected_limits);
    selected_limits.kernel = host->production_kernel;
    return cetta_json_runtime_v1_parse(
        host->runtime, arena, json_bytes, json_byte_len, &selected_limits,
        out, status, error_buf, error_buf_size);
}

const CettaJsonRuntimeV1 *cetta_json_nik_v1_borrow_selected_runtime(
    const CettaJsonNikV1 *host) {
    return host && host->instance ? host->runtime : NULL;
}

CettaJsonKernelV1 cetta_json_nik_v1_production_kernel(
    const CettaJsonNikV1 *host) {
    return host && host->instance
        ? host->production_kernel
        : CETTA_JSON_KERNEL_V1_PACKED_GLL;
}

CettaNikHostedNativeCallKindV1 cetta_json_nik_v1_run(
    CettaJsonNikV1 *host,
    CettaNikNativeOperationIdV1 operation_identity,
    const CettaJsonNikV1Request *request,
    CettaJsonNikV1LanguageReceipt *language_receipt,
    CettaNikHostedNativeReceiptV1 *hosted_receipt) {
    return cetta_nik_hosted_native_run_v1(
        host ? host->instance : NULL, operation_identity,
        request, language_receipt, hosted_receipt);
}

const CettaNikNativeCalculusV1 *cetta_json_nik_v1_calculus(void) {
    return &json_calculus;
}
