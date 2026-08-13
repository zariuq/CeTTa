#ifndef CETTA_PETTA_TYPECHECK_H
#define CETTA_PETTA_TYPECHECK_H

#include "atom.h"
#include "nik_direct_authority.h"
#include "petta_analysis.h"
#include "petta_program.h"
#include "space.h"

typedef PettaAnalysisVerdict PettaTypecheckVerdict;
typedef PettaAnalysisReason PettaTypecheckReason;
typedef PettaAnalysisFault PettaTypecheckFault;
typedef PettaAnalysisCallable PettaTypecheckCallable;

#define PETTA_TYPECHECK_ESTABLISHED PETTA_ANALYSIS_ESTABLISHED
#define PETTA_TYPECHECK_REFUTED PETTA_ANALYSIS_REFUTED
#define PETTA_TYPECHECK_UNDETERMINED PETTA_ANALYSIS_UNDETERMINED
#define PETTA_TYPECHECK_INCOMPLETE PETTA_ANALYSIS_INCOMPLETE
#define PETTA_TYPECHECK_REASON_NONE PETTA_ANALYSIS_REASON_NONE
#define PETTA_TYPECHECK_REASON_EXACT PETTA_ANALYSIS_REASON_EXACT
#define PETTA_TYPECHECK_REASON_WILDCARD PETTA_ANALYSIS_REASON_WILDCARD
#define PETTA_TYPECHECK_REASON_STRUCTURAL PETTA_ANALYSIS_REASON_STRUCTURAL
#define PETTA_TYPECHECK_REASON_DECLARED PETTA_ANALYSIS_REASON_DECLARED
#define PETTA_TYPECHECK_REASON_OPEN_VALUE PETTA_ANALYSIS_REASON_OPEN_VALUE
#define PETTA_TYPECHECK_REASON_CYCLE PETTA_ANALYSIS_REASON_CYCLE
#define PETTA_TYPECHECK_REASON_MISMATCH PETTA_ANALYSIS_REASON_MISMATCH
#define PETTA_TYPECHECK_REASON_NONCALLABLE PETTA_ANALYSIS_REASON_NONCALLABLE
#define PETTA_TYPECHECK_FAULT_NONE PETTA_ANALYSIS_FAULT_NONE
#define PETTA_TYPECHECK_FAULT_INVALID_ARGUMENT PETTA_ANALYSIS_FAULT_INVALID_ARGUMENT
#define PETTA_TYPECHECK_FAULT_ALLOCATION PETTA_ANALYSIS_FAULT_ALLOCATION
#define PETTA_TYPECHECK_FAULT_MALFORMED_TYPE PETTA_ANALYSIS_FAULT_MALFORMED_REQUIREMENT
#define PETTA_TYPECHECK_CALLABLE_NO PETTA_ANALYSIS_CALLABLE_NO
#define PETTA_TYPECHECK_CALLABLE_YES PETTA_ANALYSIS_CALLABLE_YES
#define PETTA_TYPECHECK_CALLABLE_UNKNOWN PETTA_ANALYSIS_CALLABLE_UNKNOWN

typedef struct {
    void *context;
    PettaTypecheckCallable (*callable)(
        void *context, Atom *value, CettaExprLen arity);
} PettaTypecheckHooks;

typedef PettaAnalysisResult PettaTypecheckResult;

typedef enum {
    PETTA_TYPECHECK_POLICY_DEFAULT = 0,
    PETTA_TYPECHECK_POLICY_STRICT,
    PETTA_TYPECHECK_POLICY_STRICT_DET,
} PettaTypecheckPolicy;

/* The authored PeTTa typecheck-v2 judgment and its current native computing
 * realization.  Ordinary decisions consume only the claim and live state. */
extern const CettaNikDirectAuthorityV1
    petta_typecheck_v2_direct_authority_v1;

typedef struct {
    PettaTypecheckVerdict verdict;
    PettaTypecheckFault fault;
    uint32_t declarations_seen;
    uint32_t equations_checked;
    char diagnostic[512];
} PettaTypecheckBlockResult;

/* A committed cardinality proof may consume a direct parameter's runtime
 * shape.  These obligations are deliberately separate from value typing:
 * (List T) classifies elements but does not prove that the list spine is
 * closed, and Bool does not prove that a relational argument is bound. */
typedef PettaAnalysisBoundaryRequirement PettaTypecheckBoundaryRequirement;
#define PETTA_TYPECHECK_BOUNDARY_NONE PETTA_ANALYSIS_BOUNDARY_NONE
#define PETTA_TYPECHECK_BOUNDARY_NONVAR PETTA_ANALYSIS_BOUNDARY_NONVAR
#define PETTA_TYPECHECK_BOUNDARY_PROPER_LIST PETTA_ANALYSIS_BOUNDARY_PROPER_LIST
#define PETTA_TYPECHECK_BOUNDARY_NONEMPTY_EXPRESSION PETTA_ANALYSIS_BOUNDARY_NONEMPTY_EXPRESSION

/*
 * Decide the residual judgment "value has required type".  `true` means the
 * judgment produced a semantic verdict.  `false` means an infrastructure or
 * malformed-input fault recorded in `result`; faults are never converted to
 * UNDETERMINED.  Tranche 1 has no resource limiter, so INCOMPLETE is reserved
 * for a future real producer and is not returned here.
 */
bool petta_typecheck_value(
    Space *space, Arena *arena, Atom *value, Atom *required,
    const PettaTypecheckHooks *hooks, PettaTypecheckResult *result);

/* The type-to-type judgment is exposed for load-time clients and focused
 * law tests.  It implements Roman's asymmetric union compatibility without
 * pretending that a type expression is a runtime value. */
bool petta_typecheck_types(
    Space *space, Arena *arena, Atom *actual, Atom *required,
    PettaTypecheckResult *result);

/* True when a live `(get-type ...)` extension explicitly names `required`
 * as a possible result.  Callers then preserve relational type selection
 * instead of treating the native declaration lookup as a closed world. */
bool petta_typecheck_type_has_runtime_classifier(
    Space *space, Atom *required);

/*
 * Check one source-ordered PeTTa declaration block before brand/ascription
 * erasure.  The live Space supplies declarations installed by earlier source
 * blocks and imports; `forms` supplies the mutually visible declarations and
 * equations in the current block.  The function performs no Space mutation.
 */
bool petta_typecheck_declaration_block(
    PettaProgram *program, Space *space, Registry *registry,
    Atom *const *forms, size_t form_count,
    PettaTypecheckPolicy policy, PettaTypecheckBlockResult *result);

/* Qualified replacement path: compute exactly the same declaration judgment
 * while pinning every retained inferred signature to the named direct NIK
 * authority realization and policy. */
bool petta_typecheck_declaration_block_under_authority(
    const CettaNikDirectAuthorityV1 *authority,
    PettaProgram *program, Space *space, Registry *registry,
    Atom *const *forms, size_t form_count,
    PettaTypecheckPolicy policy, PettaTypecheckBlockResult *result);

/* Production route.  The explicit unqualified entry point above remains an
 * independent differential oracle, while selected source admission retains
 * every inferred fact under the exact authority realization and policy. */
bool petta_typecheck_declaration_block_selected(
    PettaProgram *program, Space *space, Registry *registry,
    Atom *const *forms, size_t form_count,
    PettaTypecheckPolicy policy, PettaTypecheckBlockResult *result);

/* Mark the same direct declaration judgment as an admission boundary.  This
 * wrapper is reserved for declarations entering a program revision; checking
 * a runnable source form must use the judgment entry point above. */
bool petta_typecheck_declaration_admission_under_authority(
    const CettaNikDirectAuthorityV1 *authority,
    PettaProgram *program, Space *space, Registry *registry,
    Atom *const *forms, size_t form_count,
    PettaTypecheckPolicy policy, PettaTypecheckBlockResult *result);

/* Production declaration-admission route under the selected authority. */
bool petta_typecheck_declaration_admission_selected(
    PettaProgram *program, Space *space, Registry *registry,
    Atom *const *forms, size_t form_count,
    PettaTypecheckPolicy policy, PettaTypecheckBlockResult *result);

typedef enum {
    PETTA_TYPECHECK_MUTATION_ADD = 0,
    PETTA_TYPECHECK_MUTATION_REMOVE,
} PettaTypecheckMutation;

/* Validate a proposed runtime declaration/equation transition against the
 * complete live PeTTa program before the Space publishes the mutation. */
bool petta_typecheck_program_mutation(
    PettaProgram *program, Space *program_space, Registry *registry,
    Space *target_space, Atom *proposed,
    PettaTypecheckMutation mutation,
    PettaTypecheckPolicy policy, PettaTypecheckBlockResult *result);
bool petta_typecheck_program_mutation_under_authority(
    const CettaNikDirectAuthorityV1 *authority,
    PettaProgram *program, Space *program_space, Registry *registry,
    Space *target_space, Atom *proposed,
    PettaTypecheckMutation mutation,
    PettaTypecheckPolicy policy, PettaTypecheckBlockResult *result);
bool petta_typecheck_program_mutation_selected(
    PettaProgram *program, Space *program_space, Registry *registry,
    Space *target_space, Atom *proposed,
    PettaTypecheckMutation mutation,
    PettaTypecheckPolicy policy, PettaTypecheckBlockResult *result);

void petta_typecheck_inferred_signatures_rebase_selected(
    PettaProgram *program, Space *space, PettaTypecheckPolicy policy);

/* Derive the strongest runtime proviso consumed by any live clause at one
 * direct argument position.  The caller caches against the supplied Space's
 * instance and revision; false denotes an infrastructure failure, never a
 * semantic rejection. */
bool petta_typecheck_call_boundary_requirement(
    PettaProgram *program, Space *space,
    SymbolId head, CettaExprLen arity, CettaExprIndex position,
    PettaTypecheckBoundaryRequirement *requirement);

/* Derive every direct-argument proviso from one declared-type lookup and one
 * source-ordered clause snapshot.  This is the machine-facing form: callers
 * must supply exactly `arity` result slots (or NULL for arity zero). */
bool petta_typecheck_call_boundary_plan(
    PettaProgram *program, Space *space,
    SymbolId head, CettaExprLen arity,
    PettaTypecheckBoundaryRequirement *requirements,
    size_t requirement_count);

/* Runtime type failures remain ordinary catchable PeTTa Error values until
 * they reach the top-level observation boundary. */
Atom *petta_typecheck_error_atom(
    Arena *arena, Atom *source, int exit_code, const char *diagnostic);
bool petta_typecheck_error_view(
    Atom *atom, int *exit_code, const char **diagnostic);

const char *petta_typecheck_verdict_name(PettaTypecheckVerdict verdict);
const char *petta_typecheck_reason_name(PettaTypecheckReason reason);

#endif /* CETTA_PETTA_TYPECHECK_H */
