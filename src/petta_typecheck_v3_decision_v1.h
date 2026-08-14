#ifndef CETTA_PETTA_TYPECHECK_V3_DECISION_V1_H
#define CETTA_PETTA_TYPECHECK_V3_DECISION_V1_H

#include "gslt_horn_runtime.h"
#include "gslt_provider_runtime.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    CETTA_PETTA_V3_EVIDENCE_ESTABLISHED = 0,
    CETTA_PETTA_V3_EVIDENCE_REFUTED,
    CETTA_PETTA_V3_EVIDENCE_UNDETERMINED,
    CETTA_PETTA_V3_EVIDENCE_INCOMPLETE,
} CettaPettaV3EvidenceOutcomeV1;

typedef enum {
    CETTA_PETTA_V3_BOUNDARY_NONE = 0,
    CETTA_PETTA_V3_BOUNDARY_SHAPE,
    CETTA_PETTA_V3_BOUNDARY_CARDINALITY,
    CETTA_PETTA_V3_BOUNDARY_STAGE,
} CettaPettaV3EvidenceBoundaryV1;

typedef enum {
    CETTA_PETTA_V3_SEAM_UNCLASSIFIED = 0,
    CETTA_PETTA_V3_SEAM_EXACT,
    CETTA_PETTA_V3_SEAM_GRADUAL,
    CETTA_PETTA_V3_SEAM_CONFLICT,
} CettaPettaV3SeamKindV1;

/* Owned scalar witness that an evidence occurrence was established with
 * unknown-free actual and expected types.  Structural hashes retain the
 * licensed judgment after the decision scratch arena is released. */
typedef struct {
    bool issued;
    uint32_t actual_hash;
    uint32_t expected_hash;
    uint32_t cardinality_hash;
    uint32_t demand_hash;
} CettaPettaV3EvidenceOptLicenseV1;

typedef struct {
    CettaPettaV3EvidenceOutcomeV1 outcome;
    CettaPettaV3EvidenceBoundaryV1 boundary;
    const char *relation;
    const Atom *actual;
    const Atom *required;
    CettaGsltHornOutcome search_outcome;
    CettaPettaV3SeamKindV1 seam_kind;
    CettaPettaV3EvidenceOptLicenseV1 optimization_license;
} CettaPettaV3EvidenceDecisionV1;

/* Decide one ground v3 RuntimeEvidence value.  A completed failed relation
 * is a refutation only when both sides are positive ground evidence.  Search
 * limits map to INCOMPLETE, and missing result evidence maps to UNDETERMINED. */
bool cetta_petta_typecheck_v3_decide_evidence_v1(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    const Atom *evidence,
    const Atom *expected,
    const Atom *demand,
    CettaGsltHornLimits limits,
    CettaPettaV3EvidenceDecisionV1 *decision,
    char *error,
    size_t error_size);

/* End-to-end vertical slice for one declared definition.
 * The signature comes from EnvDeclaredList, is elaborated by the v3 langdef,
 * and the source body is checked through V3ExpressionEvidence. */
bool cetta_petta_typecheck_v3_decide_definition_v1(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    const Atom *subject,
    const Atom *body,
    CettaGsltHornLimits limits,
    CettaPettaV3EvidenceDecisionV1 *decision,
    char *error,
    size_t error_size);

/* Definition judgment with its source left-hand side retained.  Direct
 * pattern variables are paired with the elaborated arrow domains before the
 * body is checked, so higher-order and value parameters carry positive local
 * evidence without becoming global provider facts. */
bool cetta_petta_typecheck_v3_decide_equation_v1(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    const Atom *lhs,
    const Atom *body,
    CettaGsltHornLimits limits,
    CettaPettaV3EvidenceDecisionV1 *decision,
    char *error,
    size_t error_size);

#endif /* CETTA_PETTA_TYPECHECK_V3_DECISION_V1_H */
