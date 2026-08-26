#ifndef CETTA_GDL_POSITIVE_HORN_NATIVE_H
#define CETTA_GDL_POSITIVE_HORN_NATIVE_H

#include "gdl_type_of_native.h"
#include "gdl_stratification.h"
#include "rule_machine.h"
#include "space.h"

#include <stddef.h>
#include <stdint.h>

/* Native proof construction for the exact type-validated positive-Horn GDL
 * image.  This is a language-owned realization hosted by NIK, not a global
 * GDL opcode and not a parser-side authority mode. */
typedef struct CettaGdlPositiveHornNativeV1 CettaGdlPositiveHornNativeV1;
typedef struct CettaGdlPositiveHornEpisodeV1 CettaGdlPositiveHornEpisodeV1;

typedef struct {
    CettaGdlTypeOfNativeLimitsV1 typing;
    size_t max_source_blocks;
} CettaGdlPositiveHornLimitsV1;

typedef enum {
    CETTA_GDL_POSITIVE_HORN_ADMITTED_V1 = 1,
    CETTA_GDL_POSITIVE_HORN_OUTSIDE_FRAGMENT_V1,
    CETTA_GDL_POSITIVE_HORN_INCOMPLETE_V1,
    CETTA_GDL_POSITIVE_HORN_ENGINE_FAULT_V1,
} CettaGdlPositiveHornAdmissionKindV1;

typedef struct {
    CettaGdlPositiveHornAdmissionKindV1 kind;
    CettaGdlPositiveHornNativeV1 *native;
} CettaGdlPositiveHornAdmissionV1;

CettaGdlPositiveHornAdmissionV1
cetta_gdl_positive_horn_native_admit_v1(
    Atom *source_program,
    CettaGdlPositiveHornLimitsV1 limits);

/* Admit the exact range-restricted finite-state extension.  Source-declared
 * ground `base` members define the complete domain of episode `true` facts;
 * `not (true ...)` is compiled only as a demand for explicit absence
 * evidence constructed from a complete Space snapshot. */
CettaGdlPositiveHornAdmissionV1
cetta_gdl_finite_view_native_admit_v1(
    Atom *source_program,
    CettaGdlPositiveHornLimitsV1 limits);

void cetta_gdl_positive_horn_native_destroy_v1(
    CettaGdlPositiveHornNativeV1 *native);

const CettaNikDirectAuthorityV1 *
cetta_gdl_positive_horn_native_authority_v1(void);

const CettaNikDirectAuthorityV1 *
cetta_gdl_finite_view_native_authority_v1(void);

bool cetta_gdl_positive_horn_native_token_v1(
    const CettaGdlPositiveHornNativeV1 *native,
    CettaNikDirectAuthorityTokenV1 *token_out);

bool cetta_gdl_positive_horn_native_token_is_current_v1(
    const CettaGdlPositiveHornNativeV1 *native,
    const CettaNikDirectAuthorityTokenV1 *token);

bool cetta_gdl_positive_horn_native_identity_v1(
    const CettaGdlPositiveHornNativeV1 *native,
    const char **source_sha256_out,
    const char **profile_sha256_out,
    const char **revision_out);

/* Borrow the language-owned dependency/stratification witness constructed
 * during source admission.  It is evidence carried by the native calculus,
 * not an authority marker supplied by the parser. */
const CettaGdlStratificationV1 *
cetta_gdl_positive_horn_native_stratification_v1(
    const CettaGdlPositiveHornNativeV1 *native);

typedef struct {
    size_t max_facts;
    size_t max_typing_proofs_per_fact;
    size_t max_delta_blocks;
} CettaGdlPositiveHornEpisodeLimitsV1;

typedef enum {
    CETTA_GDL_POSITIVE_HORN_EPISODE_ADMITTED_V1 = 1,
    CETTA_GDL_POSITIVE_HORN_EPISODE_REFUTED_V1,
    CETTA_GDL_POSITIVE_HORN_EPISODE_OUTSIDE_FRAGMENT_V1,
    CETTA_GDL_POSITIVE_HORN_EPISODE_INCOMPLETE_V1,
    CETTA_GDL_POSITIVE_HORN_EPISODE_STALE_V1,
    CETTA_GDL_POSITIVE_HORN_EPISODE_ENGINE_FAULT_V1,
} CettaGdlPositiveHornEpisodeAdmissionKindV1;

typedef struct {
    CettaGdlPositiveHornEpisodeAdmissionKindV1 kind;
    CettaGdlPositiveHornEpisodeV1 *episode;
} CettaGdlPositiveHornEpisodeAdmissionV1;

/* Construct one immutable state/episode extension transactionally.  Each
 * authored fact must be ground and must first receive a native Bool typing
 * proof from the source presentation.  Every distinct typing proof becomes
 * a distinct RuleMachine proof occurrence; no raw fact bypass exists.  The
 * source-native object must outlive the returned episode. */
CettaGdlPositiveHornEpisodeAdmissionV1
cetta_gdl_positive_horn_native_admit_episode_v1(
    CettaGdlPositiveHornNativeV1 *native,
    const CettaNikDirectAuthorityTokenV1 *source_token,
    Atom *episode_identity,
    Atom *const *facts,
    size_t fact_count,
    CettaGdlPositiveHornEpisodeLimitsV1 limits);

/* Construct a finite-view episode from every occurrence in one immutable
 * live Space revision.  Unlike the ordinary typed-delta API, callers cannot
 * provide a selective fact subset and thereby manufacture absence. */
CettaGdlPositiveHornEpisodeAdmissionV1
cetta_gdl_finite_view_native_admit_space_episode_v1(
    CettaGdlPositiveHornNativeV1 *native,
    const CettaNikDirectAuthorityTokenV1 *source_token,
    Space *episode_space,
    Atom *episode_identity,
    CettaGdlPositiveHornEpisodeLimitsV1 limits);

void cetta_gdl_positive_horn_episode_destroy_v1(
    CettaGdlPositiveHornEpisodeV1 *episode);

bool cetta_gdl_positive_horn_episode_token_v1(
    const CettaGdlPositiveHornEpisodeV1 *episode,
    CettaNikDirectAuthorityTokenV1 *token_out);

bool cetta_gdl_positive_horn_episode_token_is_current_v1(
    const CettaGdlPositiveHornEpisodeV1 *episode,
    const CettaNikDirectAuthorityTokenV1 *token);

bool cetta_gdl_positive_horn_episode_identity_v1(
    const CettaGdlPositiveHornEpisodeV1 *episode,
    const char **digest_out,
    const char **revision_out);

typedef enum {
    CETTA_GDL_POSITIVE_HORN_RUN_COMPLETE_V1 = 1,
    CETTA_GDL_POSITIVE_HORN_RUN_INCOMPLETE_V1,
    CETTA_GDL_POSITIVE_HORN_RUN_STALE_V1,
    CETTA_GDL_POSITIVE_HORN_RUN_ENGINE_FAULT_V1,
} CettaGdlPositiveHornRunKindV1;

typedef struct {
    CettaGdlPositiveHornRunKindV1 kind;
    /* The implementation actually used for this run.  Zero means that no
     * implementation ran (for example, because the admission was stale).
     * This is an observation, not a claim that RuleMachine is preferred to
     * every other implementation of the positive-Horn calculus. */
    uint64_t implementation_identity;
    /* Borrowed from the caller-provided result arena. */
    Atom *result;
} CettaGdlPositiveHornRunV1;

/* Run a source-only query.  Complete empty proof search remains an empty
 * proof bag: it is never converted to Refuted without a checked obstruction.
 * Finite episode extension enters through the typed-delta operation added by
 * the next tranche. */
CettaGdlPositiveHornRunV1 cetta_gdl_positive_horn_native_run_v1(
    CettaGdlPositiveHornNativeV1 *native,
    const CettaNikDirectAuthorityTokenV1 *token,
    Arena *result_arena,
    Atom *query,
    uint32_t depth,
    uint64_t max_states,
    uint32_t max_occurrences);

/* Run against one typed immutable episode extension.  Search performs no
 * interior type checks: typing evidence was constructed once at episode
 * admission and is retained inside every fact proof occurrence. */
CettaGdlPositiveHornRunV1 cetta_gdl_positive_horn_episode_run_v1(
    CettaGdlPositiveHornEpisodeV1 *episode,
    const CettaNikDirectAuthorityTokenV1 *token,
    Arena *result_arena,
    Atom *query,
    uint32_t depth,
    uint64_t max_states,
    uint32_t max_occurrences);

typedef struct {
    size_t authored_facts;
    size_t typing_proof_occurrences;
    size_t finite_state_absence_proof_occurrences;
    size_t compiled_delta_blocks;
} CettaGdlPositiveHornEpisodeStatsV1;

bool cetta_gdl_positive_horn_episode_stats_v1(
    const CettaGdlPositiveHornEpisodeV1 *episode,
    CettaGdlPositiveHornEpisodeStatsV1 *stats_out);

typedef struct {
    size_t source_forms;
    size_t source_rules;
    size_t source_facts;
    size_t distinct_premises;
    size_t distinct_evidence_blocks;
    size_t finite_state_domain_members;
    size_t finite_state_negative_premises;
    size_t dependency_relations;
    size_t dependency_edges;
    size_t dependency_negative_edges;
    size_t dependency_strata;
    size_t compiled_blocks;
} CettaGdlPositiveHornStatsV1;

bool cetta_gdl_positive_horn_native_stats_v1(
    const CettaGdlPositiveHornNativeV1 *native,
    CettaGdlPositiveHornStatsV1 *stats_out);

#endif /* CETTA_GDL_POSITIVE_HORN_NATIVE_H */
