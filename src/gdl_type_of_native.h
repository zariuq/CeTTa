#ifndef CETTA_GDL_TYPE_OF_NATIVE_H
#define CETTA_GDL_TYPE_OF_NATIVE_H

#include "atom.h"
#include "nik_direct_authority.h"
#include "nik_licensed_implementation_selection.h"

#include <stddef.h>

/* A structure-licensed native realization of the lowercase GDL `type:of`
 * calculus.  The admitted object owns its source/profile facts and constructs
 * proof occurrences from their native typing structure; it does not retain a
 * generic checker or replay certificates while serving queries. */
typedef struct CettaGdlTypeOfNativeV1 CettaGdlTypeOfNativeV1;

typedef struct {
    size_t max_source_nodes;
    size_t max_proof_nodes;
    size_t max_derivation_depth;
} CettaGdlTypeOfNativeLimitsV1;

typedef enum {
    CETTA_GDL_TYPE_OF_NATIVE_ADMITTED_V1 = 1,
    CETTA_GDL_TYPE_OF_NATIVE_OUTSIDE_FRAGMENT_V1,
    CETTA_GDL_TYPE_OF_NATIVE_INCOMPLETE_V1,
    CETTA_GDL_TYPE_OF_NATIVE_ENGINE_FAULT_V1,
} CettaGdlTypeOfNativeAdmissionKindV1;

typedef struct {
    CettaGdlTypeOfNativeAdmissionKindV1 kind;
    CettaGdlTypeOfNativeV1 *native;
} CettaGdlTypeOfNativeAdmissionV1;

/* Trusted admission pins arrive from the package/catalog boundary.  Parser
 * data never supplies authority merely by naming itself. */
CettaGdlTypeOfNativeAdmissionV1 cetta_gdl_type_of_native_admit_v1(
    Atom *program,
    const char *expected_source_sha256,
    const char *expected_profile_sha256,
    const char *expected_revision,
    CettaGdlTypeOfNativeLimitsV1 limits);

/* Admit the same native calculus directly from authored GDL and profile
 * text.  The carrier contains no inferred types, rules, proofs, verdicts, or
 * authority claims: this operation parses the two language-owned inputs,
 * solves their finite covered typing problem, and constructs proof fibres in
 * C. */
CettaGdlTypeOfNativeAdmissionV1
cetta_gdl_type_of_native_admit_source_v1(
    Atom *source_program,
    const char *expected_source_sha256,
    const char *expected_profile_sha256,
    const char *expected_revision,
    CettaGdlTypeOfNativeLimitsV1 limits);

/* Admit an authored, content-addressed GDL typing source without requiring a
 * pre-existing catalog entry.  The digest and revision fields are recomputed
 * from the exact source/profile bytes; they identify the resulting hosted
 * calculus but do not authorize it.  Admission comes only from parsing and
 * validating the complete covered GDL typing structure below. */
CettaGdlTypeOfNativeAdmissionV1
cetta_gdl_type_of_native_admit_authored_source_v1(
    Atom *source_program, CettaGdlTypeOfNativeLimitsV1 limits);

/* Admit the dependency-closed source fibre for one authored relation.  The
 * full source/profile package remains the presentation identity; the target
 * and retained original source ordinals refine the admitted calculus
 * identity. */
CettaGdlTypeOfNativeAdmissionV1
cetta_gdl_type_of_native_admit_authored_target_v1(
    Atom *source_program,
    const char *target_name,
    size_t target_arity,
    CettaGdlTypeOfNativeLimitsV1 limits);

void cetta_gdl_type_of_native_destroy_v1(CettaGdlTypeOfNativeV1 *native);

const CettaNikDirectAuthorityV1 *
cetta_gdl_type_of_native_authority_v1(void);

bool cetta_gdl_type_of_native_token_v1(
    const CettaGdlTypeOfNativeV1 *native,
    CettaNikDirectAuthorityTokenV1 *token_out);

bool cetta_gdl_type_of_native_token_is_current_v1(
    const CettaGdlTypeOfNativeV1 *native,
    const CettaNikDirectAuthorityTokenV1 *token);

typedef enum {
    CETTA_GDL_TYPE_OF_NATIVE_QUERY_OUTCOME_V1 = 1,
    CETTA_GDL_TYPE_OF_NATIVE_QUERY_STALE_V1,
    CETTA_GDL_TYPE_OF_NATIVE_QUERY_ENGINE_FAULT_V1,
} CettaGdlTypeOfNativeQueryKindV1;

typedef struct {
    CettaGdlTypeOfNativeQueryKindV1 kind;
    /* Selection is request-local to this exact admitted source/profile/
     * revision fibre.  A served outcome always names the selected native
     * realization; stale or faulted requests select nothing. */
    CettaNikImplementationSelectionV1 selection;
    uint64_t selected_realization_identity;
    union {
        CettaNikOutcomeV1 outcome;
        CettaNikEngineFaultV1 fault;
    } value;
    /* Borrowed immutable proof occurrences.  They remain valid until the
     * admitted native calculus is destroyed.  Only Established carries a
     * nonempty bag. */
    Atom *const *proofs;
    size_t proof_count;
    /* Borrowed exact type code for a successful type synthesis.  Exact
     * checking queries also expose the checked endpoint here. */
    Atom *type;
} CettaGdlTypeOfNativeQueryV1;

/* Serve the exact proof-occurrence fibre for a closed `type:of` or
 * `gdl:literal` judgment through request-local maximal-native selection.
 * The current admitted family contains the source-derived proof constructor;
 * the selector, rather than syntax order or a tier number, establishes that
 * it is the unique greatest realization for exact ordered proof construction
 * without certificate replay.  A nonzero proof limit bounds publication; a
 * smaller limit yields Incomplete and publishes no partial bag. */
CettaGdlTypeOfNativeQueryV1 cetta_gdl_type_of_native_serve_v1(
    const CettaGdlTypeOfNativeV1 *native,
    const CettaNikDirectAuthorityTokenV1 *token,
    Atom *judgment,
    size_t max_proofs);

/* Synthesize the exact type/proof fibre of one source occurrence.  Both the
 * occurrence and its term are required, so equal endpoint terms at distinct
 * source positions remain distinct. */
CettaGdlTypeOfNativeQueryV1 cetta_gdl_type_of_native_synthesize_v1(
    const CettaGdlTypeOfNativeV1 *native,
    const CettaNikDirectAuthorityTokenV1 *token,
    Atom *occurrence, Atom *term, size_t max_proofs);

/* Borrowed content identity of the admitted hosted calculus. */
bool cetta_gdl_type_of_native_identity_v1(
    const CettaGdlTypeOfNativeV1 *native,
    const char **source_sha256_out,
    const char **profile_sha256_out,
    const char **revision_out);

/* A target view exists only for a target-indexed admitted calculus.  Its
 * target name is borrowed until the calculus is destroyed. */
bool cetta_gdl_type_of_native_target_slice_v1(
    const CettaGdlTypeOfNativeV1 *native,
    const char **target_name_out,
    size_t *target_arity_out,
    size_t *source_forms_out,
    size_t *selected_forms_out,
    size_t *reachable_relations_out,
    size_t *external_relations_out);

typedef enum {
    CETTA_GDL_RULE_VARIABLE_UNIQUE_GREATEST_V1 = 1,
    CETTA_GDL_RULE_VARIABLE_NO_COMMON_GREATEST_V1,
    CETTA_GDL_RULE_VARIABLE_SELECTION_INCOMPLETE_V1,
} CettaGdlRuleVariableSelectionKindV1;

typedef struct {
    CettaGdlRuleVariableSelectionKindV1 kind;
    size_t component_count;
    size_t candidate_count;
    size_t ambiguous_component_count;
    size_t greatest_component_count;
} CettaGdlRuleVariableSelectionV1;

/* Inspect the request-local type choice made for rule variables.  A unique
 * greatest result is returned only when every locally supported component
 * has a greatest candidate and all such candidates coexist in one complete
 * replayed source assignment.  Candidate order never breaks a frontier. */
bool cetta_gdl_type_of_native_rule_variable_selection_v1(
    const CettaGdlTypeOfNativeV1 *native,
    CettaGdlRuleVariableSelectionV1 *selection_out);

/* Borrow the checked source type assigned to one exact authored occurrence.
 * Form ordinals and paths use the language presentation's structural
 * coordinates (`root`, or one-based child paths such as `2.1`). */
bool cetta_gdl_type_of_native_source_type_name_v1(
    const CettaGdlTypeOfNativeV1 *native,
    size_t form_ordinal,
    const char *path,
    const char **type_name_out);

typedef struct {
    Atom *occurrence;
    Atom *term;
    Atom *type;
    const char *type_name;
    Atom *const *type_proofs;
    size_t type_proof_count;
    Atom *const *literal_proofs;
    size_t literal_proof_count;
} CettaGdlTypeOfNativeSourceJudgmentV1;

/* Borrow the complete native judgment fibre assigned to one authored source
 * occurrence.  This lets later language-owned calculi compose the checked
 * open derivation with typed substitutions rather than replaying a boundary
 * checker for every ground instance. */
bool cetta_gdl_type_of_native_source_judgment_v1(
    const CettaGdlTypeOfNativeV1 *native,
    size_t form_ordinal,
    const char *path,
    CettaGdlTypeOfNativeSourceJudgmentV1 *view_out);

typedef enum {
    CETTA_GDL_TYPE_OF_NATIVE_GROUND_OUTCOME_V1 = 1,
    CETTA_GDL_TYPE_OF_NATIVE_GROUND_STALE_V1,
    CETTA_GDL_TYPE_OF_NATIVE_GROUND_ENGINE_FAULT_V1,
} CettaGdlTypeOfNativeGroundKindV1;

typedef struct {
    CettaGdlTypeOfNativeGroundKindV1 kind;
    CettaNikImplementationSelectionV1 selection;
    uint64_t selected_realization_identity;
    union {
        CettaNikOutcomeV1 outcome;
        CettaNikEngineFaultV1 fault;
    } value;
    /* Owned by the caller-provided result arena.  Established alone carries
     * a nonempty proof bag and its unique inferred type. */
    Atom **proofs;
    size_t proof_count;
    Atom *type;
} CettaGdlTypeOfNativeGroundV1;

/* Construct the proof fibre for one exact external ground-literal occurrence
 * from the already admitted signature and subtype evidence.  This is a
 * native proof-kernel operation at the raw-to-typed boundary, not generic
 * certificate replay.  Unsupported syntax or an unproved Bool landing stays
 * OutsideFragment; a publication or construction bound stays Incomplete. */
CettaGdlTypeOfNativeGroundV1
cetta_gdl_type_of_native_construct_ground_literal_v1(
    const CettaGdlTypeOfNativeV1 *native,
    const CettaNikDirectAuthorityTokenV1 *token,
    Arena *result_arena,
    Atom *occurrence,
    Atom *term,
    size_t max_proofs);

typedef struct {
    size_t source_forms;
    size_t foreign_source_lines;
    size_t profile_statements;
    size_t typing_components;
    size_t typing_acceptance_constraints;
    size_t source_nodes;
    size_t signatures;
    size_t variable_bindings;
    size_t subtype_edges;
    size_t type_proof_occurrences;
    size_t literal_proof_occurrences;
    size_t constructed_proof_nodes;
} CettaGdlTypeOfNativeStatsV1;

bool cetta_gdl_type_of_native_stats_v1(
    const CettaGdlTypeOfNativeV1 *native,
    CettaGdlTypeOfNativeStatsV1 *stats_out);

#endif /* CETTA_GDL_TYPE_OF_NATIVE_H */
