#include "he_typing_authority.h"

#include "eval.h"
#include "space.h"

const CettaNikDirectAuthorityV1
    cetta_he_typing_core_direct_authority_v1 = {
        .alias = "HE-TYPING-CORE",
        .system_id = "he.typing.consistency-core",
        .authority_identity = UINT64_C(0x68652e7479636f72),
        .realization_identity = UINT64_C(0x63657474612e6863),
        .authority_revision = 1u,
        .realization_abi = 1u,
    };

const CettaHeTypingCoreDirectServiceV1
    cetta_he_typing_core_direct_service_v1 = {
        .authority = &cetta_he_typing_core_direct_authority_v1,
        .classify_consistency = he_typing_classify_consistency,
        .normalize_type = he_typing_normalize_type_status_budgeted,
        .check_refinement = he_typing_check_refinement_status_budgeted,
        .check_term = he_typing_check_term_status_budgeted,
    };

bool cetta_he_typing_core_direct_service_v1_is_valid(
    const CettaHeTypingCoreDirectServiceV1 *service) {
    return service &&
           cetta_nik_direct_authority_v1_is_valid(service->authority) &&
           service->classify_consistency && service->normalize_type &&
           service->check_refinement && service->check_term;
}

const CettaNikDirectAuthorityV1
    cetta_he_profiled_type_inference_direct_authority_v1 = {
        .alias = "HE-PROFILED-TYPE-INFERENCE",
        .system_id = "he.profiled-type-inference",
        .authority_identity = UINT64_C(0x68652e70726f6669),
        .realization_identity = UINT64_C(0x63657474612e6870),
        .authority_revision = 1u,
        .realization_abi = 1u,
    };

const CettaHeProfiledTypeInferenceDirectServiceV1
    cetta_he_profiled_type_inference_direct_service_v1 = {
        .authority =
            &cetta_he_profiled_type_inference_direct_authority_v1,
        .infer = eval_get_atom_types_profiled,
        .infer_transient = eval_get_atom_types_profiled_transient,
        .infer_budgeted = eval_get_atom_types_profiled_budgeted,
        .infer_structural = eval_get_atom_types_structural_profiled,
        .infer_structural_budgeted =
            eval_get_atom_types_structural_profiled_budgeted,
    };

bool cetta_he_profiled_type_inference_direct_service_v1_is_valid(
    const CettaHeProfiledTypeInferenceDirectServiceV1 *service) {
    return service &&
           cetta_nik_direct_authority_v1_is_valid(service->authority) &&
           service->infer && service->infer_transient &&
           service->infer_budgeted && service->infer_structural &&
           service->infer_structural_budgeted;
}

static bool he_direct_authority_token(
    const CettaNikDirectAuthorityV1 *authority, const Space *space,
    uint32_t policy_identity, CettaNikDirectAuthorityTokenV1 *token) {
    if (!space) {
        return cetta_nik_direct_authority_v1_token(
            authority, policy_identity, NULL, token);
    }

    uint64_t epoch_before = space_global_mutation_epoch();
    SpaceReadToken read = space_read_token(space);
    uint64_t epoch_after = space_global_mutation_epoch();
    if (epoch_before != epoch_after ||
        !space_read_token_matches_live_space(read, space)) {
        if (token)
            *token = (CettaNikDirectAuthorityTokenV1){0};
        return false;
    }

    CettaNikDirectAuthorityTokenV1 mutable = {
        .words = {read.instance_id, read.revision, epoch_after},
        .length = 3u,
    };
    return cetta_nik_direct_authority_v1_token(
        authority, policy_identity, &mutable, token);
}

bool cetta_he_typing_core_direct_authority_token_v1(
    const Space *space, uint32_t policy_identity,
    CettaNikDirectAuthorityTokenV1 *token) {
    return he_direct_authority_token(
        &cetta_he_typing_core_direct_authority_v1,
        space, policy_identity, token);
}

bool cetta_he_typing_core_direct_authority_token_v1_is_current(
    const CettaNikDirectAuthorityTokenV1 *token,
    const Space *space, uint32_t policy_identity) {
    CettaNikDirectAuthorityTokenV1 current;
    return token &&
           cetta_he_typing_core_direct_authority_token_v1(
               space, policy_identity, &current) &&
           cetta_nik_direct_authority_token_v1_equal(token, &current);
}

bool cetta_he_profiled_type_inference_direct_authority_token_v1(
    const Space *space, uint32_t policy_identity,
    CettaNikDirectAuthorityTokenV1 *token) {
    return he_direct_authority_token(
        &cetta_he_profiled_type_inference_direct_authority_v1,
        space, policy_identity, token);
}

bool cetta_he_profiled_type_inference_direct_authority_token_v1_is_current(
    const CettaNikDirectAuthorityTokenV1 *token,
    const Space *space, uint32_t policy_identity) {
    CettaNikDirectAuthorityTokenV1 current;
    return token &&
           cetta_he_profiled_type_inference_direct_authority_token_v1(
               space, policy_identity, &current) &&
           cetta_nik_direct_authority_token_v1_equal(token, &current);
}
