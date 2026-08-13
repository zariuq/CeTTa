/* he_typing.h — he-prime extensions over the shared HE type engine.
 *
 * Frame (implemented to, deviations named in the census):
 *  - HE typing is SUCCESS TYPING: a rejection is proven, an acceptance is not a
 *    promise.  Multiple declared types are INTERSECTION types.
 *  - %Undefined% is the GRADUAL DYNAMIC type ?: consistent with everything, and
 *    that consistency is deliberately NOT transitive — a value may not be
 *    laundered from one base type to another by passing through ?.
 *  - Atom is the value-lattice TOP with one-directional subsumption, kept
 *    distinct from ?; in parameter position it is a STAGING modality (the
 *    argument arrives unreduced).  The other meta-types (Symbol, Expression,
 *    Grounded, Variable) are staging modalities, not value-lattice members.
 *  - Consistency has exactly one named reason per edge (exact,
 *    dynamic-?, top, meta-staging, structural); naming them apart is what makes
 *    the dynamic edge non-composing.
 *
 * Constraint: every op is inert outside dependent-telescope profiles, so HE and
 * he-compat default typing behavior is untouched.
 * Constraint: verdicts are three-valued (accept / reject / unknown); unknown
 * propagates and is never conflated with reject or with a proven-empty type set.
 */
#ifndef CETTA_HE_TYPING_H
#define CETTA_HE_TYPING_H

#include <stdint.h>
#include "atom.h"

/* Returns NULL when head is not a typing op or the active profile does not
 * enable dependent telescopes; otherwise returns a verdict atom. */
Atom *he_typing_dispatch(Arena *a, Atom *head, Atom **args, uint32_t nargs)
    __attribute__((weak));

/* True for the he-prime typing op names only; result constructors such as
 * he-accept / he-reject / he-unknown are ordinary inert atoms.
 * Weak: standalone unit-test binaries link grounded.c without he_typing.c, so
 * the symbol resolves to NULL there and the grounded.c hook is skipped. */
bool he_typing_is_op(const char *name) __attribute__((weak));
/* Interned-symbol counterpart for evaluator hot paths.  Classification is
 * identical to he_typing_is_op; it only avoids reparsing a stable symbol name
 * at every gradual-typing capability probe. */
bool he_typing_is_op_id(SymbolId id) __attribute__((weak));

/* True when arg_index of the named typing op is a DATA argument that must
 * reach the op unevaluated.  Lives beside the op table so the surface and its
 * argument policy stay in one place.  Weak for the same standalone-binary
 * reason as above. */
bool he_typing_op_data_arg(const char *name, uint32_t arg_index)
    __attribute__((weak));

/* Optional he-prime normalization hook used by the shared native HE type
 * engine. On success `complete` is true and the returned atom is the checked
 * normal form. On ambiguity, exhaustion, or an inadmissible effect it is false
 * and callers preserve the original residual type. */
Atom *he_typing_normalize_shared_type(Arena *a, Space *space, Atom *type,
                                      uint64_t fuel, bool *complete)
    __attribute__((weak));

/* Language-neutral classifications over the shared HE inference engine.
   Presentation remains the responsibility of the calling language package. */
typedef enum {
    CETTA_HE_EDGE_NONE = 0,
    CETTA_HE_EDGE_EXACT,
    CETTA_HE_EDGE_STRUCTURAL,
    CETTA_HE_EDGE_DYNAMIC,
    CETTA_HE_EDGE_TOP,
    CETTA_HE_EDGE_META_STAGING,
    CETTA_HE_EDGE_UNKNOWN
} CettaHeTypingEdge;

typedef enum {
    CETTA_HE_CHECK_ESTABLISHED = 0,
    CETTA_HE_CHECK_REFUTED,
    CETTA_HE_CHECK_UNDETERMINED,
    CETTA_HE_CHECK_INCOMPLETE
} CettaHeCheckStatus;

typedef enum {
    CETTA_HE_NORMALIZE_COMPLETE = 0,
    CETTA_HE_NORMALIZE_RESOURCE,
    CETTA_HE_NORMALIZE_DEPTH,
    CETTA_HE_NORMALIZE_AMBIGUOUS,
    CETTA_HE_NORMALIZE_NO_RESULT,
    CETTA_HE_NORMALIZE_INADMISSIBLE,
    CETTA_HE_NORMALIZE_PROVISIONAL
} CettaHeNormalizeStatus;

typedef enum {
    CETTA_HE_REFINEMENT_VALID = 0,
    CETTA_HE_REFINEMENT_INVALID,
    CETTA_HE_REFINEMENT_UNDETERMINED,
    CETTA_HE_REFINEMENT_INCOMPLETE
} CettaHeRefinementStatus;

typedef enum {
    CETTA_HE_VAR_RIGID = 0,
    CETTA_HE_VAR_SCHEME,
    CETTA_HE_VAR_ELABORATION
} CettaHeVariableClass;

/* One caller-owned ledger for the resource-aware HE-typing operations used by
   Prime. `steps_*` describe an optional producer bound for evaluation, search,
   and type-level computation. Finite structural checking is measured in
   `work_steps_observed` but never failed by that producer bound. */
typedef struct {
    bool steps_limited;
    uint64_t steps_initial;
    uint64_t steps_remaining;
    uint64_t steps_spent;
    uint64_t work_steps_observed;
    uint32_t max_depth_observed;
    uint32_t type_capacity;
    bool type_capacity_exhausted;
    bool evaluator_stack_exhausted;
    bool evaluator_capacity_exhausted;
    bool allow_marked_user_type_functions;
} CettaHeTypingBudget;

void he_typing_budget_init(CettaHeTypingBudget *budget, uint64_t steps);
void he_typing_budget_init_unbounded(CettaHeTypingBudget *budget);

const char *he_typing_edge_name(CettaHeTypingEdge edge);
bool he_typing_edge_is_exact_or_structural(CettaHeTypingEdge edge);
const char *he_typing_variable_class_name(CettaHeVariableClass var_class);

CettaHeTypingEdge he_typing_classify_consistency(Atom *actual, Atom *expected,
                                                 uint64_t fuel);
CettaHeNormalizeStatus he_typing_normalize_type_status(
    Arena *a, Space *space, Atom *type, uint64_t fuel, Atom **normalized_out);
CettaHeNormalizeStatus he_typing_normalize_type_status_budgeted(
    Arena *a, Space *space, Atom *type, CettaHeTypingBudget *budget,
    Atom **normalized_out) __attribute__((weak));
CettaHeRefinementStatus he_typing_check_refinement_status(
    Arena *a, Space *space, Atom *type, uint64_t fuel, Atom **detail_out);
CettaHeRefinementStatus he_typing_check_refinement_status_budgeted(
    Arena *a, Space *space, Atom *type, CettaHeTypingBudget *budget,
    Atom **detail_out);
CettaHeCheckStatus he_typing_check_term_status(
    Arena *a, Space *space, Atom *term, Atom *expected, uint64_t fuel,
    bool require_exact_or_structural, CettaHeTypingEdge *edge_out,
    Atom **detail_out);
CettaHeCheckStatus he_typing_check_term_status_budgeted(
    Arena *a, Space *space, Atom *term, Atom *expected,
    CettaHeTypingBudget *budget, bool require_exact_or_structural,
    CettaHeTypingEdge *edge_out, Atom **detail_out);

#endif
