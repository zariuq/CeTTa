#ifndef CETTA_HE_TYPING_AUTHORITY_H
#define CETTA_HE_TYPING_AUTHORITY_H

#include "he_typing.h"
#include "nik_direct_authority.h"
#include "space.h"

/* Certificate-free NIK service over the consistency/normalization helper used
 * by Prime and by selected profile-aware inference operations.  This is an
 * authored core fragment, not the complete --lang he typing surface. */
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
