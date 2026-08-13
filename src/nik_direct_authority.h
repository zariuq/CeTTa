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

/* How much of a direct authority is described by one authored source.
 * Fragment bindings are qualification inputs only: they must never be used
 * to claim complete source/native correspondence or to license a fast path
 * whose missing judgments matter. */
typedef enum {
    CETTA_NIK_DIRECT_SOURCE_AUTHORED_FRAGMENT = 1,
    CETTA_NIK_DIRECT_SOURCE_COMPLETE_PRESENTATION = 2,
} CettaNikDirectSourceCoverageV1;

/* Build/admission metadata connecting an independently authored language
 * presentation to a native direct authority.  The runtime does not execute
 * this source and hot-path stamps/tokens do not carry these strings.  A valid
 * binding states provenance and coverage, not an adequacy proof. */
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

bool cetta_nik_direct_authority_token_v1_equal(
    const CettaNikDirectAuthorityTokenV1 *left,
    const CettaNikDirectAuthorityTokenV1 *right);

/* Production defaults to the direct typed-applicability license.  The shared
 * opt-out exists so qualification tests can run the exact historical search
 * path in a separate process and compare answers and work. */
bool cetta_nik_typed_applicability_pruning_enabled(void);

#endif /* CETTA_NIK_DIRECT_AUTHORITY_H */
