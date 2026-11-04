#ifndef CETTA_INVOCATION_PLAN_H
#define CETTA_INVOCATION_PLAN_H
#include "petta_program.h"

static inline bool cetta_invocation_plan_has_value_operands(
        const PettaPlanNode *plan, CettaExprLen length) {
    if (!plan || !length || plan->child_count != length || !plan->children ||
        plan->computation.control.operation != CETTA_CONTROL_INVOKE)
        return false;
    for (CettaExprIndex i = 1; i < length; ++i)
        if (plan->children[i].computation.control.operation != CETTA_CONTROL_RETURN_VALUE)
            return false;
    return true;
}

/* A host or a completed caller supplies values, rather than source operands.
 * The existing invocation instruction still owns callee admission and dispatch.
 * The caller chooses its existing direct/dynamic error-handler discipline. */
static inline PettaPlanNode *cetta_value_invocation_plan(
        Arena *arena, CettaExprLen length, PettaPlanRole role, bool handler) {
    if (!arena || !length || !cetta_expr_len_mul_fits_size(length, sizeof(PettaPlanNode)))
        return NULL;
    PettaPlanNode *root = arena_alloc(arena, sizeof(*root));
    PettaPlanNode *places = arena_alloc(arena, sizeof(*places) * (size_t)length);
    if (!root || !places) return NULL;
    for (CettaExprIndex i = 0; i < length; ++i)
        places[i] = (PettaPlanNode){.computation.control = CETTA_CONTROL_RETURN_VALUE_PLAN};
    *root = (PettaPlanNode){.role = role,
        .computation.control = CETTA_CONTROL_INVOKE_PLAN,
        .dispatch_handler = handler, .child_count = length, .children = places};
    return root;
}
#endif
