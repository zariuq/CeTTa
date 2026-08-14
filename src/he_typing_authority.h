#ifndef CETTA_HE_TYPING_AUTHORITY_H
#define CETTA_HE_TYPING_AUTHORITY_H

#include "he_typing.h"
#include "nik_direct_authority.h"
#include "space.h"

/* Certificate-free NIK service over the consistency/normalization helper used
 * by Prime and by selected profile-aware inference operations.  This is an
 * authored core fragment, not the complete --lang he typing interface. */
typedef struct {
    const CettaNikDirectAuthorityV1 *authority;
    CettaHeTypingEdge (*classify_consistency)(
        Atom *actual, Atom *expected, uint64_t fuel);
    CettaHeNormalizeStatus (*normalize_type)(
        Arena *arena, Space *space, Atom *type,
        CettaHeTypingBudget *budget, Atom **normalized_out);
    CettaHeRefinementStatus (*check_refinement)(
        Arena *arena, Space *space, Atom *type,
        CettaHeTypingBudget *budget, Atom **detail_out);
    CettaHeCheckStatus (*check_term)(
        Arena *arena, Space *space, Atom *term, Atom *expected,
        CettaHeTypingBudget *budget, bool require_exact_or_structural,
        CettaHeTypingEdge *edge_out, Atom **detail_out);
} CettaHeTypingCoreDirectServiceV1;

extern const CettaNikDirectAuthorityV1
    cetta_he_typing_core_direct_authority_v1;
extern const CettaHeTypingCoreDirectServiceV1
    cetta_he_typing_core_direct_service_v1;

bool cetta_he_typing_core_direct_service_v1_is_valid(
    const CettaHeTypingCoreDirectServiceV1 *service);

/* The actual profile-aware inference entry points used by --lang he.  The
 * service retains the native list-valued result and the native resource
 * ledger; it does not introduce a proof/certificate carrier. */
typedef struct {
    const CettaNikDirectAuthorityV1 *authority;
    uint32_t (*infer)(
        Space *space, Arena *arena, Atom *atom, Atom ***types_out);
    uint32_t (*infer_transient)(
        Space *space, Arena *arena, Atom *atom, Atom ***types_out);
    uint32_t (*infer_budgeted)(
        Space *space, Arena *arena, Atom *atom, Atom ***types_out,
        CettaTypeInferenceBudget *budget);
    uint32_t (*infer_structural)(
        Space *space, Arena *arena, Atom *atom, Atom ***types_out);
    uint32_t (*infer_structural_budgeted)(
        Space *space, Arena *arena, Atom *atom, Atom ***types_out,
        CettaTypeInferenceBudget *budget);
} CettaHeProfiledTypeInferenceDirectServiceV1;

extern const CettaNikDirectAuthorityV1
    cetta_he_profiled_type_inference_direct_authority_v1;
extern const CettaHeProfiledTypeInferenceDirectServiceV1
    cetta_he_profiled_type_inference_direct_service_v1;

bool cetta_he_profiled_type_inference_direct_service_v1_is_valid(
    const CettaHeProfiledTypeInferenceDirectServiceV1 *service);

/* Ordered mirror of OutcomeListContracts.InferenceAPI.  The collection bits
 * are executable ABI facts: each result is a logical-order sequence, repeated
 * type occurrences remain observable, only the three unbounded operations
 * claim complete enumeration, and only the ordinary profiled operation writes
 * the persistent memo. */
typedef enum {
    CETTA_HE_INFERENCE_PROFILED_V1 = 0,
    CETTA_HE_INFERENCE_PROFILED_TRANSIENT_V1,
    CETTA_HE_INFERENCE_PROFILED_BUDGETED_V1,
    CETTA_HE_INFERENCE_STRUCTURAL_PROFILED_V1,
    CETTA_HE_INFERENCE_STRUCTURAL_PROFILED_BUDGETED_V1,
    CETTA_HE_INFERENCE_API_V1_COUNT
} CettaHeInferenceApiV1;

typedef struct {
    CettaHeInferenceApiV1 api;
    const char *name;
    bool order_semantic;
    bool multiplicity_semantic;
    bool unbounded_complete;
    bool memo_publishes;
} CettaHeCollectionContractV1;

extern const CettaHeCollectionContractV1
    cetta_he_inference_contracts_v1[CETTA_HE_INFERENCE_API_V1_COUNT];

bool cetta_he_inference_contracts_v1_are_valid(void);
const CettaHeCollectionContractV1 *cetta_he_inference_contract_v1(
    CettaHeInferenceApiV1 api);

/* The budget-stability partition mirrored from OutcomeListContracts.  Only an
 * incomplete check and resource/depth normalization are budget-sensitive. */
bool cetta_he_check_status_is_budget_sensitive(CettaHeCheckStatus status);
bool cetta_he_normalize_status_is_exhaustion(CettaHeNormalizeStatus status);

/* Ordered mirror of the four live HE search operations.  This inventory keeps
 * bounded exhaustion separate from a completed empty search: no operation may
 * present exhaustion as rejection; first-inhabitant may reject only after its
 * requested finite depth has been exhaustively searched. */
typedef enum {
    CETTA_HE_SEARCH_INHABITANTS_V1 = 0,
    CETTA_HE_SEARCH_FIRST_INHABITANT_V1,
    CETTA_HE_SEARCH_FORWARD_STEP_V1,
    CETTA_HE_SEARCH_FORWARD_CLOSURE_V1,
    CETTA_HE_SEARCH_STRATEGY_V1_COUNT
} CettaHeSearchStrategyApiV1;

typedef struct {
    CettaHeSearchStrategyApiV1 api;
    const char *name;
    bool exhaustive_empty_may_reject;
    bool exhaustion_may_reject;
} CettaHeSearchStrategyContractV1;

extern const CettaHeSearchStrategyContractV1
    cetta_he_search_strategy_contracts_v1[
        CETTA_HE_SEARCH_STRATEGY_V1_COUNT];

bool cetta_he_search_strategy_contracts_v1_are_valid(void);
const CettaHeSearchStrategyContractV1 *cetta_he_search_strategy_contract_v1(
    CettaHeSearchStrategyApiV1 api);

/* Capture authority and mutable-space provenance for caches.  The claim,
 * budget, and result remain separate cache-key/value fields; this token alone
 * never identifies a judgment.  NULL space is allowed for pure operations. */
bool cetta_he_typing_core_direct_authority_token_v1(
    const Space *space, uint32_t policy_identity,
    CettaNikDirectAuthorityTokenV1 *token);

bool cetta_he_typing_core_direct_authority_token_v1_is_current(
    const CettaNikDirectAuthorityTokenV1 *token,
    const Space *space, uint32_t policy_identity);

bool cetta_he_profiled_type_inference_direct_authority_token_v1(
    const Space *space, uint32_t policy_identity,
    CettaNikDirectAuthorityTokenV1 *token);

bool cetta_he_profiled_type_inference_direct_authority_token_v1_is_current(
    const CettaNikDirectAuthorityTokenV1 *token,
    const Space *space, uint32_t policy_identity);

#endif /* CETTA_HE_TYPING_AUTHORITY_H */
