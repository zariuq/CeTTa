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
        .check_term = he_typing_check_term_outcome_budgeted,
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

_Static_assert(CETTA_NIK_OUTCOME_ESTABLISHED == 0,
               "HE check outcome order must match the Lean inventory");
_Static_assert(CETTA_NIK_OUTCOME_INCOMPLETE == 3,
               "HE check outcome count must match the Lean inventory");
_Static_assert(CETTA_HE_NORMALIZE_COMPLETE == 0,
               "HE normalization order must match the Lean inventory");
_Static_assert(CETTA_HE_NORMALIZE_PROVISIONAL == 6,
               "HE normalization count must match the Lean inventory");

const CettaHeCollectionContractV1
    cetta_he_inference_contracts_v1[CETTA_HE_INFERENCE_API_V1_COUNT] = {
        {
            .api = CETTA_HE_INFERENCE_PROFILED_V1,
            .name = "eval_get_atom_types_profiled",
            .order_semantic = true,
            .multiplicity_semantic = true,
            .unbounded_complete = true,
            .memo_publishes = true,
        },
        {
            .api = CETTA_HE_INFERENCE_PROFILED_TRANSIENT_V1,
            .name = "eval_get_atom_types_profiled_transient",
            .order_semantic = true,
            .multiplicity_semantic = true,
            .unbounded_complete = true,
            .memo_publishes = false,
        },
        {
            .api = CETTA_HE_INFERENCE_PROFILED_BUDGETED_V1,
            .name = "eval_get_atom_types_profiled_budgeted",
            .order_semantic = true,
            .multiplicity_semantic = true,
            .unbounded_complete = false,
            .memo_publishes = false,
        },
        {
            .api = CETTA_HE_INFERENCE_STRUCTURAL_PROFILED_V1,
            .name = "eval_get_atom_types_structural_profiled",
            .order_semantic = true,
            .multiplicity_semantic = true,
            .unbounded_complete = true,
            .memo_publishes = false,
        },
        {
            .api = CETTA_HE_INFERENCE_STRUCTURAL_PROFILED_BUDGETED_V1,
            .name = "eval_get_atom_types_structural_profiled_budgeted",
            .order_semantic = true,
            .multiplicity_semantic = true,
            .unbounded_complete = false,
            .memo_publishes = false,
        },
    };

const CettaHeCollectionContractV1 *cetta_he_inference_contract_v1(
    CettaHeInferenceApiV1 api) {
    if ((unsigned)api >= CETTA_HE_INFERENCE_API_V1_COUNT)
        return NULL;
    return &cetta_he_inference_contracts_v1[(unsigned)api];
}

bool cetta_he_inference_contracts_v1_are_valid(void) {
    for (unsigned index = 0u;
         index < CETTA_HE_INFERENCE_API_V1_COUNT; index++) {
        const CettaHeCollectionContractV1 *contract =
            &cetta_he_inference_contracts_v1[index];
        bool budgeted = index == CETTA_HE_INFERENCE_PROFILED_BUDGETED_V1 ||
                        index ==
                            CETTA_HE_INFERENCE_STRUCTURAL_PROFILED_BUDGETED_V1;
        if ((unsigned)contract->api != index || !contract->name ||
            contract->name[0] == '\0' || !contract->order_semantic ||
            !contract->multiplicity_semantic ||
            contract->unbounded_complete == budgeted ||
            contract->memo_publishes !=
                (index == CETTA_HE_INFERENCE_PROFILED_V1)) {
            return false;
        }
    }
    return true;
}

bool cetta_he_outcome_is_budget_sensitive(CettaNikOutcomeV1 status) {
    return status == CETTA_NIK_OUTCOME_INCOMPLETE;
}

bool cetta_he_normalize_status_is_exhaustion(
    CettaHeNormalizeStatus status) {
    return status == CETTA_HE_NORMALIZE_RESOURCE ||
           status == CETTA_HE_NORMALIZE_DEPTH;
}

const CettaHeSearchStrategyContractV1
    cetta_he_search_strategy_contracts_v1[
        CETTA_HE_SEARCH_STRATEGY_V1_COUNT] = {
        {
            .api = CETTA_HE_SEARCH_INHABITANTS_V1,
            .name = "search-inhabitants",
            .exhaustive_empty_may_reject = false,
            .exhaustion_may_reject = false,
        },
        {
            .api = CETTA_HE_SEARCH_FIRST_INHABITANT_V1,
            .name = "search-first-inhabitant",
            .exhaustive_empty_may_reject = true,
            .exhaustion_may_reject = false,
        },
        {
            .api = CETTA_HE_SEARCH_FORWARD_STEP_V1,
            .name = "type-forward-step",
            .exhaustive_empty_may_reject = false,
            .exhaustion_may_reject = false,
        },
        {
            .api = CETTA_HE_SEARCH_FORWARD_CLOSURE_V1,
            .name = "type-forward-closure",
            .exhaustive_empty_may_reject = false,
            .exhaustion_may_reject = false,
        },
    };

const CettaHeSearchStrategyContractV1 *
cetta_he_search_strategy_contract_v1(CettaHeSearchStrategyApiV1 api) {
    if ((unsigned)api >= CETTA_HE_SEARCH_STRATEGY_V1_COUNT)
        return NULL;
    return &cetta_he_search_strategy_contracts_v1[(unsigned)api];
}

bool cetta_he_search_strategy_contracts_v1_are_valid(void) {
    for (unsigned index = 0u;
         index < CETTA_HE_SEARCH_STRATEGY_V1_COUNT; index++) {
        const CettaHeSearchStrategyContractV1 *contract =
            &cetta_he_search_strategy_contracts_v1[index];
        if ((unsigned)contract->api != index || !contract->name ||
            contract->name[0] == '\0' || contract->exhaustion_may_reject ||
            contract->exhaustive_empty_may_reject !=
                (index == CETTA_HE_SEARCH_FIRST_INHABITANT_V1)) {
            return false;
        }
    }
    return true;
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
