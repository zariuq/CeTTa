#ifndef CETTA_PETTA_TYPECHECK_H
#define CETTA_PETTA_TYPECHECK_H

#include "atom.h"
#include "petta_program.h"
#include "space.h"

typedef enum {
    PETTA_TYPECHECK_ESTABLISHED = 0,
    PETTA_TYPECHECK_REFUTED,
    PETTA_TYPECHECK_UNDETERMINED,
    PETTA_TYPECHECK_INCOMPLETE,
} PettaTypecheckVerdict;

typedef enum {
    PETTA_TYPECHECK_REASON_NONE = 0,
    PETTA_TYPECHECK_REASON_EXACT,
    PETTA_TYPECHECK_REASON_WILDCARD,
    PETTA_TYPECHECK_REASON_STRUCTURAL,
    PETTA_TYPECHECK_REASON_DECLARED,
    PETTA_TYPECHECK_REASON_OPEN_VALUE,
    PETTA_TYPECHECK_REASON_CYCLE,
    PETTA_TYPECHECK_REASON_MISMATCH,
    PETTA_TYPECHECK_REASON_NONCALLABLE,
} PettaTypecheckReason;

typedef enum {
    PETTA_TYPECHECK_FAULT_NONE = 0,
    PETTA_TYPECHECK_FAULT_INVALID_ARGUMENT,
    PETTA_TYPECHECK_FAULT_ALLOCATION,
    PETTA_TYPECHECK_FAULT_MALFORMED_TYPE,
} PettaTypecheckFault;

typedef enum {
    PETTA_TYPECHECK_CALLABLE_NO = 0,
    PETTA_TYPECHECK_CALLABLE_YES,
    PETTA_TYPECHECK_CALLABLE_UNKNOWN,
} PettaTypecheckCallable;

typedef struct {
    void *context;
    PettaTypecheckCallable (*callable)(
        void *context, Atom *value, CettaExprLen arity);
} PettaTypecheckHooks;

typedef struct {
    PettaTypecheckVerdict verdict;
    PettaTypecheckReason reason;
    PettaTypecheckFault fault;
    SpaceDeclaredTypeLookupCost declaration_lookup_cost;
} PettaTypecheckResult;

typedef enum {
    PETTA_TYPECHECK_POLICY_DEFAULT = 0,
    PETTA_TYPECHECK_POLICY_STRICT,
    PETTA_TYPECHECK_POLICY_STRICT_DET,
} PettaTypecheckPolicy;

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
typedef enum {
    PETTA_TYPECHECK_BOUNDARY_NONE = 0,
    PETTA_TYPECHECK_BOUNDARY_NONVAR,
    PETTA_TYPECHECK_BOUNDARY_PROPER_LIST,
    PETTA_TYPECHECK_BOUNDARY_NONEMPTY_EXPRESSION,
} PettaTypecheckBoundaryRequirement;

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

/* Derive the strongest runtime proviso consumed by any live clause at one
 * direct argument position.  The caller caches against the supplied Space's
 * instance and revision; false denotes an infrastructure failure, never a
 * semantic rejection. */
bool petta_typecheck_call_boundary_requirement(
    PettaProgram *program, Space *space,
    SymbolId head, CettaExprLen arity, CettaExprIndex position,
    PettaTypecheckBoundaryRequirement *requirement);

/* Runtime type failures remain ordinary catchable PeTTa Error values until
 * they reach the top-level observation boundary. */
Atom *petta_typecheck_error_atom(
    Arena *arena, Atom *source, int exit_code, const char *diagnostic);
bool petta_typecheck_error_view(
    Atom *atom, int *exit_code, const char **diagnostic);

const char *petta_typecheck_verdict_name(PettaTypecheckVerdict verdict);
const char *petta_typecheck_reason_name(PettaTypecheckReason reason);

#endif /* CETTA_PETTA_TYPECHECK_H */
