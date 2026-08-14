#ifndef CETTA_PETTA_TYPECHECK_V3_H
#define CETTA_PETTA_TYPECHECK_V3_H

#include "petta_analysis.h"
#include "petta_program.h"
#include "petta_typecheck.h"
#include "petta_typecheck_v3_decision_v1.h"
#include "space.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct CettaPettaTypecheckV3 CettaPettaTypecheckV3;

typedef enum {
    CETTA_PETTA_TYPECHECK_V3_POLICY_DEFAULT = 0,
    CETTA_PETTA_TYPECHECK_V3_POLICY_STRICT,
    CETTA_PETTA_TYPECHECK_V3_POLICY_STRICT_DET,
} CettaPettaTypecheckV3Policy;

typedef struct {
    bool issued;
    uint64_t space_instance;
    uint64_t admitted_revision;
    uint32_t block_hash;
    uint32_t policy;
    uint32_t equation_count;
} CettaPettaTypecheckV3OptLicense;

typedef struct {
    PettaAnalysisVerdict verdict;
    CettaPettaV3EvidenceBoundaryV1 boundary;
    CettaGsltHornOutcome search_outcome;
    uint32_t declarations_seen;
    uint32_t equations_checked;
    uint32_t established_equations;
    uint32_t undetermined_equations;
    uint32_t incomplete_equations;
    uint32_t exact_equations;
    uint32_t gradual_equations;
    uint32_t conflict_equations;
    CettaPettaTypecheckV3OptLicense optimization_license;
    char relation[96];
    char subject[128];
    char diagnostic[512];
} CettaPettaTypecheckV3BlockResult;

typedef enum {
    CETTA_PETTA_TYPECHECK_V3_ROUTE_NATIVE_AGREEMENT = 0,
    CETTA_PETTA_TYPECHECK_V3_ROUTE_NAMED_REFINEMENT,
    CETTA_PETTA_TYPECHECK_V3_ROUTE_LEGACY_V2,
} CettaPettaTypecheckV3Route;

typedef struct {
    CettaPettaTypecheckV3Route route;
    PettaTypecheckVerdict verdict;
    PettaTypecheckFault fault;
    bool native_optimization_authorized;
    CettaPettaTypecheckV3BlockResult native;
    PettaTypecheckBlockResult legacy;
    char diagnostic[512];
} CettaPettaTypecheckV3CompatibilityResult;

/* Load the generated native v3 calculus once.  The returned service owns only
 * immutable rule state and can be reused across source revisions. */
CettaPettaTypecheckV3 *cetta_petta_typecheck_v3_create(
    char *error, size_t error_size);

void cetta_petta_typecheck_v3_free(CettaPettaTypecheckV3 *checker);

size_t cetta_petta_typecheck_v3_rule_count(
    const CettaPettaTypecheckV3 *checker);

/* Check a mutually visible source block against one pinned live revision.
 * The block is exposed to the five fact providers as a read-only overlay and
 * is never inserted into the program or Space by this function. */
bool cetta_petta_typecheck_v3_declaration_block(
    CettaPettaTypecheckV3 *checker,
    PettaProgram *program,
    Space *space,
    Atom *const *forms,
    size_t form_count,
    CettaPettaTypecheckV3Policy policy,
    CettaPettaTypecheckV3BlockResult *result);

/* Validate a previously issued exact-block authority against the source block
 * and the live Space revision it was admitted against. */
bool cetta_petta_typecheck_v3_opt_license_is_current(
    const CettaPettaTypecheckV3OptLicense *license,
    const Space *space,
    Atom *const *forms,
    size_t form_count,
    CettaPettaTypecheckV3Policy policy);

/* Product admission waist.  The native calculus and Roman-compatible v2
 * independently judge the complete block.  Decisive agreement uses the
 * native route; every unpromoted difference delegates to v2 without turning
 * v2's verdict into native evidence. */
bool cetta_petta_typecheck_v3_compatibility_block(
    CettaPettaTypecheckV3 *checker,
    PettaProgram *program,
    Space *space,
    Registry *registry,
    Atom *const *forms,
    size_t form_count,
    PettaTypecheckPolicy policy,
    bool declaration_admission,
    CettaPettaTypecheckV3CompatibilityResult *result);

const char *cetta_petta_typecheck_v3_route_name(
    CettaPettaTypecheckV3Route route);

const char *cetta_petta_typecheck_v3_verdict_name(
    PettaAnalysisVerdict verdict);

const char *cetta_petta_typecheck_v3_boundary_name(
    CettaPettaV3EvidenceBoundaryV1 boundary);

#endif /* CETTA_PETTA_TYPECHECK_V3_H */
