#include "nik_direct_authority.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

static bool direct_digest_is_sha256(const char *digest) {
    if (!digest || strlen(digest) != 64u)
        return false;
    for (size_t index = 0u; index < 64u; index++) {
        char character = digest[index];
        if (!((character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'f')))
            return false;
    }
    return true;
}

bool cetta_nik_outcome_v1_is_valid(CettaNikOutcomeV1 outcome) {
    return outcome >= CETTA_NIK_OUTCOME_ESTABLISHED &&
           outcome <= CETTA_NIK_OUTCOME_INCOMPLETE;
}

bool cetta_nik_result_v1_is_valid(CettaNikResultV1 result) {
    if (result.kind == CETTA_NIK_RESULT_OUTCOME)
        return cetta_nik_outcome_v1_is_valid(result.value.outcome);
    return result.kind == CETTA_NIK_RESULT_ENGINE_FAULT &&
           result.value.fault == CETTA_NIK_ENGINE_FAULT_UNAVAILABLE;
}

CettaNikResultV1 cetta_nik_result_v1_outcome(CettaNikOutcomeV1 outcome) {
    return (CettaNikResultV1){
        .kind = CETTA_NIK_RESULT_OUTCOME,
        .value.outcome = outcome,
    };
}

CettaNikResultV1 cetta_nik_result_v1_engine_fault(
    CettaNikEngineFaultV1 fault) {
    return (CettaNikResultV1){
        .kind = CETTA_NIK_RESULT_ENGINE_FAULT,
        .value.fault = fault,
    };
}

bool cetta_nik_outcome_v1_status(
    CettaNikOutcomeV1 outcome, CettaNikStatusV1 *status_out) {
    if (!status_out || !cetta_nik_outcome_v1_is_valid(outcome))
        return false;
    if (outcome == CETTA_NIK_OUTCOME_ESTABLISHED)
        *status_out = CETTA_NIK_STATUS_ESTABLISHED;
    else if (outcome == CETTA_NIK_OUTCOME_REFUTED)
        *status_out = CETTA_NIK_STATUS_REFUTED;
    else if (outcome == CETTA_NIK_OUTCOME_INCOMPLETE)
        *status_out = CETTA_NIK_STATUS_INCOMPLETE;
    else
        *status_out = CETTA_NIK_STATUS_UNDETERMINED;
    return true;
}

bool cetta_nik_direct_authority_v1_is_valid(
    const CettaNikDirectAuthorityV1 *authority) {
    return authority && authority->alias && authority->alias[0] != '\0' &&
           authority->system_id && authority->system_id[0] != '\0' &&
           authority->authority_identity != 0u &&
           authority->realization_identity != 0u &&
           authority->authority_revision != 0u &&
           authority->authority_revision <=
               CETTA_NIK_DIRECT_AUTHORITY_REVISION_MAX &&
           authority->realization_abi != 0u &&
           authority->realization_abi <=
               CETTA_NIK_DIRECT_AUTHORITY_REALIZATION_ABI_MAX;
}

bool cetta_nik_direct_source_binding_v1_is_valid(
    const CettaNikDirectSourceBindingV1 *binding) {
    return binding &&
           cetta_nik_direct_authority_v1_is_valid(binding->authority) &&
           binding->schema_id && binding->schema_id[0] != '\0' &&
           binding->presentation_id && binding->presentation_id[0] != '\0' &&
           binding->semantic_scope && binding->semantic_scope[0] != '\0' &&
           direct_digest_is_sha256(binding->source_sha256) &&
           direct_digest_is_sha256(binding->package_sha256) &&
           (binding->coverage ==
                CETTA_NIK_DIRECT_SOURCE_AUTHORED_FRAGMENT ||
            binding->coverage ==
                CETTA_NIK_DIRECT_SOURCE_COMPLETE_PRESENTATION);
}

bool cetta_nik_direct_authority_v1_stamp(
    const CettaNikDirectAuthorityV1 *authority,
    uint32_t policy_identity,
    CettaNikDirectAuthorityStampV1 *stamp) {
    if (stamp)
        *stamp = (CettaNikDirectAuthorityStampV1){0};
    if (!stamp || !cetta_nik_direct_authority_v1_is_valid(authority))
        return false;
    *stamp = (CettaNikDirectAuthorityStampV1){
        .authority_identity = authority->authority_identity,
        .realization_identity = authority->realization_identity,
        .authority_revision = authority->authority_revision,
        .realization_abi = authority->realization_abi,
        .policy_identity = policy_identity,
    };
    return true;
}

bool cetta_nik_direct_authority_stamp_v1_equal(
    const CettaNikDirectAuthorityStampV1 *left,
    const CettaNikDirectAuthorityStampV1 *right) {
    return left && right &&
           left->authority_identity == right->authority_identity &&
           left->realization_identity == right->realization_identity &&
           left->authority_revision == right->authority_revision &&
           left->realization_abi == right->realization_abi &&
           left->policy_identity == right->policy_identity;
}

bool cetta_nik_direct_authority_v1_token(
    const CettaNikDirectAuthorityV1 *authority,
    uint32_t policy_identity,
    const CettaNikDirectAuthorityTokenV1 *mutable_suffix,
    CettaNikDirectAuthorityTokenV1 *token) {
    if (token)
        *token = (CettaNikDirectAuthorityTokenV1){0};
    if (!token || !cetta_nik_direct_authority_v1_is_valid(authority))
        return false;
    uint8_t mutable_length = mutable_suffix ? mutable_suffix->length : 0u;
    if (mutable_length >
        CETTA_NIK_DIRECT_AUTHORITY_TOKEN_WORD_CAPACITY -
            CETTA_NIK_DIRECT_AUTHORITY_TOKEN_BASE_WORDS) {
        return false;
    }
    token->words[0] = authority->authority_identity;
    token->words[1] = authority->realization_identity;
    token->words[2] =
        ((uint64_t)authority->authority_revision << 48u) |
        ((uint64_t)authority->realization_abi << 32u) |
        (uint64_t)policy_identity;
    if (mutable_length > 0u) {
        memcpy(
            &token->words[CETTA_NIK_DIRECT_AUTHORITY_TOKEN_BASE_WORDS],
            mutable_suffix->words,
            (size_t)mutable_length * sizeof(mutable_suffix->words[0]));
    }
    token->length = (uint8_t)(
        CETTA_NIK_DIRECT_AUTHORITY_TOKEN_BASE_WORDS + mutable_length);
    return true;
}

bool cetta_nik_direct_authority_v1_token_from_sha256(
    const CettaNikDirectAuthorityV1 *authority,
    const char digest[65],
    uint32_t policy_identity,
    CettaNikDirectAuthorityTokenV1 *token) {
    CettaNikDirectAuthorityTokenV1 suffix = {.length = 4u};
    if (!direct_digest_is_sha256(digest) || !token)
        return false;
    for (size_t word_index = 0u; word_index < 4u; word_index++) {
        uint64_t word = 0u;
        for (size_t digit_index = 0u; digit_index < 16u; digit_index++) {
            char digit = digest[word_index * 16u + digit_index];
            uint8_t value = digit >= '0' && digit <= '9'
                ? (uint8_t)(digit - '0')
                : (uint8_t)(digit - 'a' + 10);
            word = (word << 4u) | value;
        }
        suffix.words[word_index] = word;
    }
    return cetta_nik_direct_authority_v1_token(
        authority, policy_identity, &suffix, token);
}

bool cetta_nik_direct_authority_token_v1_equal(
    const CettaNikDirectAuthorityTokenV1 *left,
    const CettaNikDirectAuthorityTokenV1 *right) {
    if (!left || !right ||
        left->length < CETTA_NIK_DIRECT_AUTHORITY_TOKEN_BASE_WORDS ||
        left->length != right->length ||
        left->length > CETTA_NIK_DIRECT_AUTHORITY_TOKEN_WORD_CAPACITY) {
        return false;
    }
    return memcmp(
        left->words, right->words,
        (size_t)left->length * sizeof(left->words[0])) == 0;
}

bool cetta_nik_typed_applicability_pruning_enabled(void) {
    static __thread bool configured = false;
    static __thread bool enabled = true;
    if (!configured) {
        const char *setting = getenv("CETTA_NIK_TYPED_APPLICABILITY");
        enabled =
            !setting || !(strcmp(setting, "0") == 0 ||
                          strcasecmp(setting, "false") == 0 ||
                          strcasecmp(setting, "off") == 0 ||
                          strcasecmp(setting, "no") == 0);
        configured = true;
    }
    return enabled;
}
