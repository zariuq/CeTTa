#ifndef CETTA_NIK_DIRECT_AUTHORITY_H
#define CETTA_NIK_DIRECT_AUTHORITY_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Identity and revision data shared by directly computed NIK realizations.
 * The authority fields identify the semantic judgment being decided; the
 * realization fields identify the concrete native implementation.  Keeping
 * them separate prevents an unqualified replacement implementation from
 * inheriting facts admitted by its predecessor.
 */
typedef struct {
    const char *alias;
    const char *system_id;
    uint64_t authority_identity;
    uint64_t realization_identity;
    uint32_t authority_revision;
    uint32_t realization_abi;
} CettaNikDirectAuthorityV1;

/* Semantic result of one authority demand.  Positive derivations, checked
 * obstructions, fragment boundaries, and bounded search are distinct
 * constructors: there is no representable "established outside fragment"
 * or "refuted because the budget ended" state.  The constructor payload is
 * authority-specific and lives in the enclosing typed receipt. */
typedef enum {
    CETTA_NIK_OUTCOME_ESTABLISHED = 0,
    CETTA_NIK_OUTCOME_REFUTED,
    CETTA_NIK_OUTCOME_OUTSIDE_FRAGMENT,
    CETTA_NIK_OUTCOME_INCOMPLETE,
} CettaNikOutcomeV1;

/* Infrastructure failure is not a semantic outcome.  It travels in the
 * outer result channel and therefore cannot be read as Undetermined or enter
 * either semantic refinement order. */
typedef enum {
    CETTA_NIK_ENGINE_FAULT_UNAVAILABLE = 1,
} CettaNikEngineFaultV1;

typedef enum {
    CETTA_NIK_RESULT_OUTCOME = 0,
    CETTA_NIK_RESULT_ENGINE_FAULT,
} CettaNikResultKindV1;

typedef struct {
    CettaNikResultKindV1 kind;
    union {
        CettaNikOutcomeV1 outcome;
        CettaNikEngineFaultV1 fault;
    } value;
} CettaNikResultV1;

/* Human/tool compatibility readout of semantic outcomes only.  Engine
 * faults have no status readout. */
typedef enum {
    CETTA_NIK_STATUS_ESTABLISHED = 0,
    CETTA_NIK_STATUS_REFUTED,
    CETTA_NIK_STATUS_UNDETERMINED,
    CETTA_NIK_STATUS_INCOMPLETE,
} CettaNikStatusV1;

bool cetta_nik_outcome_v1_is_valid(CettaNikOutcomeV1 outcome);

bool cetta_nik_result_v1_is_valid(CettaNikResultV1 result);

CettaNikResultV1 cetta_nik_result_v1_outcome(CettaNikOutcomeV1 outcome);

CettaNikResultV1 cetta_nik_result_v1_engine_fault(
    CettaNikEngineFaultV1 fault);

bool cetta_nik_outcome_v1_status(
    CettaNikOutcomeV1 outcome, CettaNikStatusV1 *status_out);

/* How much of a direct authority is described by one authored source.
 * Fragment bindings are qualification inputs only: they must never be used
 * to claim complete source/native correspondence or to license a fast path
 * whose missing judgments matter. */
typedef enum {
    CETTA_NIK_DIRECT_SOURCE_AUTHORED_FRAGMENT = 1,
    CETTA_NIK_DIRECT_SOURCE_COMPLETE_PRESENTATION = 2,
} CettaNikDirectSourceCoverageV1;

/* Build/admission metadata connecting an independently authored language
 * presentation to a native direct authority.  The binding retains only
 * identity, provenance, and coverage.  Mode, outcome, and realization policy
 * belong to the authority/admission layer and cannot be inferred from parser
 * data or read from this receipt. */
typedef struct {
    const CettaNikDirectAuthorityV1 *authority;
    const char *schema_id;
    const char *presentation_id;
    const char *semantic_scope;
    const char *source_sha256;
    const char *package_sha256;
    CettaNikDirectSourceCoverageV1 coverage;
} CettaNikDirectSourceBindingV1;

/* Static provenance for a fact computed under one authority realization and
 * one authored policy.  Mutable stores are pinned separately by their native
 * revision tokens. */
typedef struct {
    uint64_t authority_identity;
    uint64_t realization_identity;
    uint32_t authority_revision;
    uint32_t realization_abi;
    uint32_t policy_identity;
} CettaNikDirectAuthorityStampV1;

/* Exact process-local authority snapshot used by hot-path caches.  Revisions
 * and realization ABIs are bounded so they, together with the full policy
 * identity, occupy one word.  This preserves the previous five-word budget
 * for language-owned mutable authority without enlarging hot cache entries. */
#define CETTA_NIK_DIRECT_AUTHORITY_REVISION_MAX UINT16_MAX
#define CETTA_NIK_DIRECT_AUTHORITY_REALIZATION_ABI_MAX UINT16_MAX
#define CETTA_NIK_DIRECT_AUTHORITY_TOKEN_WORD_CAPACITY 8u
#define CETTA_NIK_DIRECT_AUTHORITY_TOKEN_BASE_WORDS 3u
typedef struct {
    uint64_t words[CETTA_NIK_DIRECT_AUTHORITY_TOKEN_WORD_CAPACITY];
    uint8_t length;
} CettaNikDirectAuthorityTokenV1;

bool cetta_nik_direct_authority_v1_is_valid(
    const CettaNikDirectAuthorityV1 *authority);

bool cetta_nik_direct_source_binding_v1_is_valid(
    const CettaNikDirectSourceBindingV1 *binding);

bool cetta_nik_direct_authority_v1_stamp(
    const CettaNikDirectAuthorityV1 *authority,
    uint32_t policy_identity,
    CettaNikDirectAuthorityStampV1 *stamp);

bool cetta_nik_direct_authority_stamp_v1_equal(
    const CettaNikDirectAuthorityStampV1 *left,
    const CettaNikDirectAuthorityStampV1 *right);

/* Build a cache key without allocating an auxiliary evidence object.  The
 * mutable suffix belongs to the host language and is copied opaquely. */
bool cetta_nik_direct_authority_v1_token(
    const CettaNikDirectAuthorityV1 *authority,
    uint32_t policy_identity,
    const CettaNikDirectAuthorityTokenV1 *mutable_suffix,
    CettaNikDirectAuthorityTokenV1 *token);

/* Use one exact content digest as the language-owned mutable suffix of a
 * direct-authority token.  This is only identity/currentness data: parsing a
 * digest does not admit a realization or confer a semantic verdict. */
bool cetta_nik_direct_authority_v1_token_from_sha256(
    const CettaNikDirectAuthorityV1 *authority,
    const char digest[65],
    uint32_t policy_identity,
    CettaNikDirectAuthorityTokenV1 *token);

bool cetta_nik_direct_authority_token_v1_equal(
    const CettaNikDirectAuthorityTokenV1 *left,
    const CettaNikDirectAuthorityTokenV1 *right);

/* Production defaults to the direct typed-applicability license.  The shared
 * opt-out exists so qualification tests can run the exact historical search
 * path in a separate process and compare answers and work. */
bool cetta_nik_typed_applicability_pruning_enabled(void);

#endif /* CETTA_NIK_DIRECT_AUTHORITY_H */
