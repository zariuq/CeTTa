#ifndef CETTA_DETERMINISTIC_EQUATION_PLAN_V1_H
#define CETTA_DETERMINISTIC_EQUATION_PLAN_V1_H

#include "src/atom.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct CettaDeterministicEquationPlanV1
    CettaDeterministicEquationPlanV1;

typedef enum {
    CETTA_DETERMINISTIC_EQUATION_V1_OK = 0,
    CETTA_DETERMINISTIC_EQUATION_V1_BAD_ARGUMENT,
    CETTA_DETERMINISTIC_EQUATION_V1_INVALID_PRESENTATION,
    CETTA_DETERMINISTIC_EQUATION_V1_UNSUPPORTED_RULE,
    CETTA_DETERMINISTIC_EQUATION_V1_NO_RULE,
    CETTA_DETERMINISTIC_EQUATION_V1_AMBIGUOUS_RULE,
    CETTA_DETERMINISTIC_EQUATION_V1_NON_GROUND_TERM,
    CETTA_DETERMINISTIC_EQUATION_V1_PRIMITIVE_FAULT,
    CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT
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

/* Compile one deterministic equation family from the metta-equation rows of
 * authored GSLT presentations.  Every such row must be unconditional and
 * left-linear.  The presentations remain owned by the plan so the executing
 * patterns and right-hand sides are exactly the admitted source. */
bool cetta_deterministic_equation_plan_v1_load(
    const char *const *presentation_paths, size_t presentation_count,
    CettaDeterministicEquationPlanV1 **out,
    CettaDeterministicEquationStatusV1 *status,
    char *error, size_t error_size);

void cetta_deterministic_equation_plan_v1_free(
    CettaDeterministicEquationPlanV1 *plan);

/* Evaluate one ground call by exact first-order matching.  A defined head
 * must match exactly one rule; zero and multiple matches fail closed.
 * Undefined heads are constructors after their children are evaluated.
 * `let` is the sole built-in binder; other effects enter only through the
 * explicit primitive handler. */
bool cetta_deterministic_equation_plan_v1_run(
    const CettaDeterministicEquationPlanV1 *plan, const Atom *call,
    CettaDeterministicPrimitiveFnV1 primitive, void *primitive_context,
    Arena *arena, uint32_t depth_limit, uint64_t work_limit,
    Atom **out, CettaDeterministicEquationStatusV1 *status,
    char *error, size_t error_size);

const char *cetta_deterministic_equation_status_name_v1(
    CettaDeterministicEquationStatusV1 status);

#endif /* CETTA_DETERMINISTIC_EQUATION_PLAN_V1_H */
