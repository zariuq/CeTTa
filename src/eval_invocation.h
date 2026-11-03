#ifndef CETTA_EVAL_INVOCATION_H
#define CETTA_EVAL_INVOCATION_H
#include "eval.h"
#include "owned_execution.h"

/* Direct host invocation: operands are values, and language/source controls
 * remain owned by the ordinary evaluator. Publication has the same ownership
 * and completion contract as eval_top_with_registry_outcome. */
void eval_top_with_registry_invocation_outcome(
    Space *, Arena *, Arena *, Registry *, const CettaInvocation *, EvalOutcome *);
#endif
