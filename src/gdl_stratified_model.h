#ifndef CETTA_GDL_STRATIFIED_MODEL_H
#define CETTA_GDL_STRATIFIED_MODEL_H

#include "gdl_finite_herbrand.h"
#include "gdl_stratification.h"
#include "gdl_type_of_native.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A completed finite support model with an occurrence-bearing packed proof
 * graph.  Finite support is the quotient used to establish completion;
 * source proofs, typed substitutions, branches, premise order, and cycles
 * remain in the graph and are not deduplicated into Boolean truth. */
typedef struct CettaGdlStratifiedModelV1 CettaGdlStratifiedModelV1;
typedef struct CettaGdlStratifiedEpisodeV1 CettaGdlStratifiedEpisodeV1;

typedef struct {
    size_t max_variables_per_form;
    size_t max_branch_expansions;
    size_t max_assignments;
    size_t max_ground_instances;
    size_t max_supports;
    size_t max_proof_edges;
    size_t max_rounds;
    size_t max_logical_depth;
} CettaGdlStratifiedModelLimitsV1;

typedef enum {
    CETTA_GDL_STRATIFIED_MODEL_ESTABLISHED_V1 = 1,
    CETTA_GDL_STRATIFIED_MODEL_REFUTED_NEGATIVE_CYCLE_V1,
    CETTA_GDL_STRATIFIED_MODEL_OUTSIDE_FRAGMENT_V1,
    CETTA_GDL_STRATIFIED_MODEL_INCOMPLETE_V1,
    CETTA_GDL_STRATIFIED_MODEL_ENGINE_FAULT_V1,
} CettaGdlStratifiedModelKindV1;

typedef struct {
    CettaGdlStratifiedModelKindV1 kind;
    /* Established carries a completed model.  Incomplete retains the exact
     * partial support/graph and completed lower strata, but it does not
     * license absence in the unfinished stratum. */
    CettaGdlStratifiedModelV1 *model;
    /* Refuted alone carries the checked authored negative-cycle witness.
     * Destroy it with `cetta_gdl_stratification_destroy_v1`. */
    CettaGdlStratificationV1 *negative_cycle_obstruction;
} CettaGdlStratifiedModelResultV1;

typedef struct {
    CettaGdlTypeOfNativeLimitsV1 typing;
    CettaGdlFiniteHerbrandLimitsV1 herbrand;
    CettaGdlStratificationLimitsV1 stratification;
    CettaGdlStratifiedModelLimitsV1 evaluation;
} CettaGdlStratifiedModelAdmissionLimitsV1;

typedef struct {
    const char *name;
    size_t term_index;
    Atom *term;
    const char *exact_type;
} CettaGdlStratifiedSubstitutionViewV1;

typedef struct {
    const char *or_path;
    size_t alternative;
    Atom *const *source_literal_proofs;
    size_t source_literal_proof_count;
} CettaGdlStratifiedBranchChoiceViewV1;

typedef enum {
    CETTA_GDL_STRATIFIED_PROOF_AUTHORED_SOURCE_V1 = 1,
    CETTA_GDL_STRATIFIED_PROOF_TYPED_EPISODE_FACT_V1,
} CettaGdlStratifiedProofOriginKindV1;

typedef struct {
    const char *path;
    size_t support_index;
    Atom *literal;
    Atom *const *source_literal_proofs;
    size_t source_literal_proof_count;
} CettaGdlStratifiedPositivePremiseViewV1;

typedef struct {
    const char *not_path;
    Atom *literal;
    size_t relation_index;
    size_t completed_stratum;
    size_t completed_relation_support_count;
    Atom *const *source_not_proofs;
    size_t source_not_proof_count;
    Atom *const *source_operand_proofs;
    size_t source_operand_proof_count;
} CettaGdlStratifiedAbsenceViewV1;

typedef struct {
    const char *path;
    Atom *left;
    Atom *right;
    Atom *const *source_literal_proofs;
    size_t source_literal_proof_count;
} CettaGdlStratifiedDistinctViewV1;

typedef struct {
    CettaGdlStratifiedProofOriginKindV1 origin;
    /* Present only for a typed episode fact.  Authored source proofs use the
     * structural source coordinates below. */
    Atom *episode_occurrence;
    size_t source_form_ordinal;
    size_t source_start_line;
    size_t source_end_line;
    size_t head_support_index;
    Atom *source_head_proof;
    const CettaGdlStratifiedSubstitutionViewV1 *substitution;
    size_t substitution_count;
    const CettaGdlStratifiedBranchChoiceViewV1 *branch_choices;
    size_t branch_choice_count;
    const CettaGdlStratifiedPositivePremiseViewV1 *positive_premises;
    size_t positive_premise_count;
    const CettaGdlStratifiedAbsenceViewV1 *absence_receipts;
    size_t absence_receipt_count;
    const CettaGdlStratifiedDistinctViewV1 *distinct_evidence;
    size_t distinct_evidence_count;
} CettaGdlStratifiedProofEdgeViewV1;

typedef struct {
    Atom *literal;
    size_t relation_index;
    size_t stratum;
    const size_t *proof_edge_indices;
    size_t proof_edge_count;
} CettaGdlStratifiedSupportViewV1;

typedef struct {
    size_t source_forms;
    size_t source_rules;
    size_t source_facts;
    size_t assignments;
    size_t branch_expansions;
    size_t ground_instances;
    size_t distinct_checks;
    size_t support_nodes;
    size_t proof_edges;
    size_t positive_premise_references;
    size_t absence_receipts;
    size_t rounds;
    size_t completed_strata;
} CettaGdlStratifiedModelStatsV1;

/* Admit one exact authored package and construct its least stratified support
 * model.  Typing, finite carrier, source syntax, and stratification are
 * constructed and owned as one basis, so components from different source
 * revisions cannot be mixed.  Open typing derivations are composed with
 * exact typed Herbrand substitutions; no per-grounding checker replay is
 * retained by the model. */
CettaGdlStratifiedModelResultV1
cetta_gdl_stratified_model_admit_authored_source_v1(
    Atom *source_program,
    CettaGdlStratifiedModelAdmissionLimitsV1 limits);

/* Construct the strongest currently admitted model for one exact authored
 * target fibre.  Dependency closure is derived over the original source
 * occurrences; unrelated forms are not renamed, copied, or interpreted as
 * rejected. */
CettaGdlStratifiedModelResultV1
cetta_gdl_stratified_model_admit_authored_target_v1(
    Atom *source_program,
    const char *target_name,
    size_t target_arity,
    CettaGdlStratifiedModelAdmissionLimitsV1 limits);

const CettaNikDirectAuthorityV1 *
cetta_gdl_stratified_model_authority_v1(void);

bool cetta_gdl_stratified_model_token_v1(
    const CettaGdlStratifiedModelV1 *model,
    CettaNikDirectAuthorityTokenV1 *token_out);

bool cetta_gdl_stratified_model_token_is_current_v1(
    const CettaGdlStratifiedModelV1 *model,
    const CettaNikDirectAuthorityTokenV1 *token);

bool cetta_gdl_stratified_model_identity_v1(
    const CettaGdlStratifiedModelV1 *model,
    const char **source_sha256_out,
    const char **profile_sha256_out,
    const char **revision_out);

bool cetta_gdl_stratified_model_target_slice_v1(
    const CettaGdlStratifiedModelV1 *model,
    const char **target_name_out,
    size_t *target_arity_out,
    size_t *source_forms_out,
    size_t *selected_forms_out,
    size_t *reachable_relations_out,
    size_t *external_relations_out);

bool cetta_gdl_stratified_model_selection_v1(
    const CettaGdlStratifiedModelV1 *model,
    CettaNikNativeSelectionV1 *selection_out,
    uint64_t *realization_identity_out);

typedef struct {
    size_t max_facts;
    size_t max_typing_proofs_per_fact;
    CettaGdlStratifiedModelLimitsV1 evaluation;
} CettaGdlStratifiedEpisodeLimitsV1;

typedef enum {
    CETTA_GDL_STRATIFIED_EPISODE_ESTABLISHED_V1 = 1,
    CETTA_GDL_STRATIFIED_EPISODE_OUTSIDE_FRAGMENT_V1,
    CETTA_GDL_STRATIFIED_EPISODE_INCOMPLETE_V1,
    CETTA_GDL_STRATIFIED_EPISODE_STALE_V1,
    CETTA_GDL_STRATIFIED_EPISODE_ENGINE_FAULT_V1,
} CettaGdlStratifiedEpisodeKindV1;

typedef struct {
    CettaGdlStratifiedEpisodeKindV1 kind;
    /* Established carries a completed episode.  Incomplete retains the
     * exact partial support/proof graph but never licenses absence in an
     * unfinished stratum. */
    CettaGdlStratifiedEpisodeV1 *episode;
} CettaGdlStratifiedEpisodeResultV1;

/* Extend one immutable source calculus by an ordered bag of external ground
 * facts.  Raw facts cross the native typing boundary exactly once; their
 * proof occurrences seed the same structural fixed-point engine used by the
 * source model.  The source model must outlive the episode. */
CettaGdlStratifiedEpisodeResultV1
cetta_gdl_stratified_model_admit_episode_v1(
    CettaGdlStratifiedModelV1 *source_model,
    const CettaNikDirectAuthorityTokenV1 *source_token,
    Atom *episode_identity,
    Atom *const *facts,
    size_t fact_count,
    CettaGdlStratifiedEpisodeLimitsV1 limits);

void cetta_gdl_stratified_episode_destroy_v1(
    CettaGdlStratifiedEpisodeV1 *episode);

bool cetta_gdl_stratified_episode_token_v1(
    const CettaGdlStratifiedEpisodeV1 *episode,
    CettaNikDirectAuthorityTokenV1 *token_out);

bool cetta_gdl_stratified_episode_token_is_current_v1(
    const CettaGdlStratifiedEpisodeV1 *episode,
    const CettaNikDirectAuthorityTokenV1 *token);

bool cetta_gdl_stratified_episode_identity_v1(
    const CettaGdlStratifiedEpisodeV1 *episode,
    const char **digest_out,
    const char **revision_out);

const CettaGdlStratifiedModelV1 *
cetta_gdl_stratified_episode_model_v1(
    const CettaGdlStratifiedEpisodeV1 *episode);

typedef struct {
    size_t authored_facts;
    size_t typing_proof_occurrences;
    size_t seeded_support_nodes;
    size_t seeded_proof_edges;
} CettaGdlStratifiedEpisodeStatsV1;

bool cetta_gdl_stratified_episode_stats_v1(
    const CettaGdlStratifiedEpisodeV1 *episode,
    CettaGdlStratifiedEpisodeStatsV1 *stats_out);

void cetta_gdl_stratified_model_destroy_v1(
    CettaGdlStratifiedModelV1 *model);

size_t cetta_gdl_stratified_model_support_count_v1(
    const CettaGdlStratifiedModelV1 *model);

size_t cetta_gdl_stratified_model_proof_edge_count_v1(
    const CettaGdlStratifiedModelV1 *model);

bool cetta_gdl_stratified_model_support_view_v1(
    const CettaGdlStratifiedModelV1 *model,
    size_t index,
    CettaGdlStratifiedSupportViewV1 *view_out);

bool cetta_gdl_stratified_model_proof_edge_view_v1(
    const CettaGdlStratifiedModelV1 *model,
    size_t index,
    CettaGdlStratifiedProofEdgeViewV1 *view_out);

bool cetta_gdl_stratified_model_find_support_v1(
    const CettaGdlStratifiedModelV1 *model,
    Atom *literal,
    size_t *index_out);

bool cetta_gdl_stratified_model_stats_v1(
    const CettaGdlStratifiedModelV1 *model,
    CettaGdlStratifiedModelStatsV1 *stats_out);

#endif /* CETTA_GDL_STRATIFIED_MODEL_H */
