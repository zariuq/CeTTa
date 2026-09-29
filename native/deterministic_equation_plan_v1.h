#ifndef CETTA_DETERMINISTIC_EQUATION_PLAN_V1_H
#define CETTA_DETERMINISTIC_EQUATION_PLAN_V1_H

#include "src/atom.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct CettaDeterministicEquationPlanV1
    CettaDeterministicEquationPlanV1;

typedef struct {
    const uint8_t *bytes;
    size_t length;
    const char *source;
} CettaDeterministicEquationInputV1;

typedef enum {
    CETTA_DETERMINISTIC_EQUATION_V1_OK = 0,
    CETTA_DETERMINISTIC_EQUATION_V1_BAD_ARGUMENT,
    CETTA_DETERMINISTIC_EQUATION_V1_INVALID_PRESENTATION,
    CETTA_DETERMINISTIC_EQUATION_V1_UNSUPPORTED_RULE,
    CETTA_DETERMINISTIC_EQUATION_V1_NO_RULE,
    CETTA_DETERMINISTIC_EQUATION_V1_AMBIGUOUS_RULE,
    CETTA_DETERMINISTIC_EQUATION_V1_NON_GROUND_TERM,
    CETTA_DETERMINISTIC_EQUATION_V1_PRIMITIVE_FAULT,
    CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT,
    CETTA_DETERMINISTIC_EQUATION_V1_NON_DESCENDING
} CettaDeterministicEquationStatusV1;

typedef enum {
    CETTA_DETERMINISTIC_PRIMITIVE_V1_NOT_HANDLED = 0,
    CETTA_DETERMINISTIC_PRIMITIVE_V1_HANDLED,
    CETTA_DETERMINISTIC_PRIMITIVE_V1_FAULT
} CettaDeterministicPrimitiveResultV1;

/* Evaluated arguments are borrowed.  A handled result must be allocated in
 * arena or be a globally shared immutable atom. */
typedef CettaDeterministicPrimitiveResultV1
(*CettaDeterministicPrimitiveFnV1)(
    void *context, const char *head, Atom *const *arguments,
    uint32_t argument_count, Arena *arena, Atom **out,
    char *error, size_t error_size);

/* What the value of a primitive call is known to be, for the loader's
 * descent check.  The norm of a value is 1 for an atom; 1 + |h| + |t| for a
 * cell (LCons h t); |x| for a value view (W x) of langdef:value-view; and
 * 1 + n + the norms of the elements for any other expression or list value
 * of n elements.  A primitive the vocabulary classifies must answer every
 * call with a value or a fault, never leave it unhandled. */
typedef enum {
    /* Not a primitive: the call is a constructor of its values. */
    CETTA_DETERMINISTIC_PRIMITIVE_CLASS_V1_NONE = 0,
    /* Its value is an atom, of norm 1. */
    CETTA_DETERMINISTIC_PRIMITIVE_CLASS_V1_ATOM,
    /* One argument, and a value of norm at most the argument's. */
    CETTA_DETERMINISTIC_PRIMITIVE_CLASS_V1_PRESERVING,
    /* A value no argument bounds. */
    CETTA_DETERMINISTIC_PRIMITIVE_CLASS_V1_STRUCTURE
} CettaDeterministicPrimitiveClassV1;

typedef CettaDeterministicPrimitiveClassV1
(*CettaDeterministicPrimitiveClassifyFnV1)(
    void *context, const char *head, uint32_t argument_count);

/* The primitives a plan runs with: the handler, and the class of each head
 * it handles.  A plan is admitted, and runs, with one vocabulary; a NULL
 * vocabulary has no primitives. */
typedef struct {
    CettaDeterministicPrimitiveFnV1 primitive;
    CettaDeterministicPrimitiveClassifyFnV1 classify;
    void *context;
} CettaDeterministicVocabularyV1;

/* Compile one deterministic equation family from the metta-equation rows of
 * authored GSLT presentations.  Every such row must be unconditional and
 * left-linear, left sides must be pairwise exclusive, and the calls of every
 * cycle of equations must descend structurally under the vocabulary's
 * primitives (NON_DESCENDING names a rule that does not).  The presentations
 * remain owned by the plan so the executing patterns and right-hand sides are
 * exactly the admitted source. */
bool cetta_deterministic_equation_plan_v1_load(
    const char *const *presentation_paths, size_t presentation_count,
    const CettaDeterministicVocabularyV1 *vocabulary,
    CettaDeterministicEquationPlanV1 **out,
    CettaDeterministicEquationStatusV1 *status,
    char *error, size_t error_size);

/* The same source validation and execution plan, loaded from length-aware
 * source buffers.  The plan owns its parsed source; input buffers need only
 * remain valid until this call returns. */
bool cetta_deterministic_equation_plan_v1_load_inputs(
    const CettaDeterministicEquationInputV1 *inputs, size_t input_count,
    const CettaDeterministicVocabularyV1 *vocabulary,
    CettaDeterministicEquationPlanV1 **out,
    CettaDeterministicEquationStatusV1 *status,
    char *error, size_t error_size);

void cetta_deterministic_equation_plan_v1_free(
    CettaDeterministicEquationPlanV1 *plan);

/* Evaluate one ground call by exact first-order matching.  The loader
 * admits only pairwise exclusive left sides, so at most one rule matches a
 * call; a call to a defined head that no rule matches fails closed.  The
 * primitive handler must be the one of the vocabulary the plan was admitted
 * with.
 * Undefined heads are constructors after their children are evaluated.
 * `let` is the sole built-in binder; other effects enter only through the
 * explicit primitive handler. */
bool cetta_deterministic_equation_plan_v1_run(
    const CettaDeterministicEquationPlanV1 *plan, const Atom *call,
    CettaDeterministicPrimitiveFnV1 primitive, void *primitive_context,
    Arena *arena, uint32_t depth_limit, uint64_t work_limit,
    Atom **out, CettaDeterministicEquationStatusV1 *status,
    char *error, size_t error_size);

/* Apply a ground symbol-headed call to already computed argument values.
 * Only the selected equation body is evaluated; argument subtrees are never
 * interpreted as equations, binders or primitive calls on entry. */
bool cetta_deterministic_equation_plan_v1_apply(
    const CettaDeterministicEquationPlanV1 *plan, const Atom *call,
    CettaDeterministicPrimitiveFnV1 primitive, void *primitive_context,
    Arena *arena, uint32_t depth_limit, uint64_t work_limit,
    Atom **out, CettaDeterministicEquationStatusV1 *status,
    char *error, size_t error_size);

/* The same execution with consumed evaluator/matcher work exposed on every
 * return, including failure.  work_used may be NULL.  These are the existing
 * budget units, not a measure of primitive running time or allocation. */
bool cetta_deterministic_equation_plan_v1_run_counted(
    const CettaDeterministicEquationPlanV1 *plan, const Atom *call,
    CettaDeterministicPrimitiveFnV1 primitive, void *primitive_context,
    Arena *arena, uint32_t depth_limit, uint64_t work_limit,
    uint64_t *work_used,
    Atom **out, CettaDeterministicEquationStatusV1 *status,
    char *error, size_t error_size);

/* The data application with consumed work exposed, as for run_counted. */
bool cetta_deterministic_equation_plan_v1_apply_counted(
    const CettaDeterministicEquationPlanV1 *plan, const Atom *call,
    CettaDeterministicPrimitiveFnV1 primitive, void *primitive_context,
    Arena *arena, uint32_t depth_limit, uint64_t work_limit,
    uint64_t *work_used,
    Atom **out, CettaDeterministicEquationStatusV1 *status,
    char *error, size_t error_size);

const char *cetta_deterministic_equation_status_name_v1(
    CettaDeterministicEquationStatusV1 status);

/* Audit view of a loaded plan: its admitted equations in execution order,
 * with source variables bound, and the operators its presentations declare. */
uint32_t cetta_deterministic_equation_plan_v1_rule_count(
    const CettaDeterministicEquationPlanV1 *plan);

bool cetta_deterministic_equation_plan_v1_rule_view(
    const CettaDeterministicEquationPlanV1 *plan, uint32_t index,
    const char **name, const Atom **left, const Atom **right);

bool cetta_deterministic_equation_plan_v1_declares(
    const CettaDeterministicEquationPlanV1 *plan, const char *name,
    size_t arity);

#endif /* CETTA_DETERMINISTIC_EQUATION_PLAN_V1_H */
