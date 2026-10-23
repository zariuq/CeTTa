#include "owned_execution.h"

#include <string.h>

bool cetta_invocation_resolve_arguments(
        Arena *arena, const Bindings *environment, const CettaInvocation *call,
        bool closed_only, CettaInvocation *resolved) {
    CettaExprLen arity = 0u;
    if (!arena || !resolved || !cetta_invocation_arity(call, &arity) ||
        !cetta_expr_len_mul_fits_size(arity, sizeof(Atom *)))
        return false;
    Atom *const *borrowed = cetta_invocation_borrow_arguments(call);
    Atom **arguments = borrowed ? (Atom **)borrowed
        : arena_alloc(arena, sizeof(*arguments) * (arity ? (size_t)arity : 1u));
    for (CettaExprIndex index = 0u; index < arity; index++) {
        BindingValue operand = cetta_invocation_argument(call, index);
        Atom *value = binding_value_kind(operand) == BINDING_VALUE_MATERIALIZED
            ? bindings_apply_if_vars(environment, arena, operand.skeleton)
            : bindings_apply_value(environment, arena, operand);
        if (!value || (closed_only && atom_has_vars(value)))
            return false;
        if (arguments == borrowed && value != borrowed[index]) {
            arguments = arena_alloc(arena, sizeof(*arguments) * (size_t)arity);
            memcpy(arguments, borrowed, sizeof(*arguments) * (size_t)index);
        }
        if (arguments != borrowed)
            arguments[index] = value;
    }
    *resolved = (CettaInvocation){.head = call->head,
        .supplied = {.atoms = arguments, .count = arity}};
    return true;
}

Atom **cetta_invocation_materialize_arguments(
        Arena *arena, const CettaInvocation *call) {
    CettaExprLen arity = 0u;
    if (!arena || !cetta_invocation_arity(call, &arity) ||
        !cetta_expr_len_mul_fits_size(arity, sizeof(Atom *)))
        return NULL;
    Atom *const *borrowed = cetta_invocation_borrow_arguments(call);
    if (borrowed)
        return (Atom **)borrowed;
    Atom **arguments = arena_alloc(
        arena, sizeof(*arguments) * (arity ? (size_t)arity : 1u));
    for (CettaExprIndex index = 0u; index < arity; index++) {
        arguments[index] = binding_value_materialize(
            arena, cetta_invocation_argument(call, index));
        if (!arguments[index])
            return NULL;
    }
    return arguments;
}

Atom *cetta_invocation_materialize(Arena *arena, const CettaInvocation *call) {
    CettaExprLen arity = 0u;
    if (!arena || !cetta_invocation_arity(call, &arity) ||
        arity == UINT64_MAX ||
        !cetta_expr_len_mul_fits_size(arity + 1u, sizeof(Atom *)))
        return NULL;
    Atom *head = binding_value_materialize(arena, call->head);
    if (!head)
        return NULL;
    Atom *draft = atom_expr_builder_begin(arena, arity + 1u);
    if (!draft)
        return NULL;
    draft->expr.elems[0] = head;
    for (CettaExprIndex index = 0u; index < arity; index++) {
        Atom *argument = binding_value_materialize(
            arena, cetta_invocation_argument(call, index));
        if (!argument)
            return NULL;
        draft->expr.elems[index + 1u] = argument;
    }
    return atom_expr_builder_finish(arena, draft);
}

void cetta_owned_execution_init(CettaOwnedExecution *execution,
    void *computation, CettaOwnedExecutionNext next,
    CettaOwnedExecutionRelease release) {
    *execution = (CettaOwnedExecution){
        .computation = computation,
        .next = next,
        .release = release,
        .stop = CETTA_OWNED_EXECUTION_PAUSED,
    };
}

CettaOwnedExecutionStep cetta_owned_execution_sequence(
    CettaOwnedExecution *execution, uint64_t allowance,
    CettaOwnedReturnConsumer consume, void *consumer) {
    if (!execution || !execution->computation || !execution->next || !consume)
        return CETTA_OWNED_EXECUTION_HANDOFF;
    if (execution->terminal)
        return execution->stop;
    while (allowance > 0u) {
        if (!execution->has_pending) {
            CettaOwnedReturn returned = {0};
            CettaOwnedExecutionStep step = execution->next(
                execution->computation, &returned, &execution->completion);
            if (step != CETTA_OWNED_EXECUTION_RETURN) {
                execution->stop = step;
                execution->terminal = step != CETTA_OWNED_EXECUTION_PAUSED;
                return step;
            }
            execution->pending = returned;
            execution->has_pending = true;
        }
        if (!consume(consumer, &execution->pending)) {
            execution->stop = CETTA_OWNED_EXECUTION_PAUSED;
            return execution->stop;
        }
        bool final = execution->pending.final &&
            execution->completion == CETTA_EVAL_COMPLETE &&
            (!execution->final_is_current ||
             execution->final_is_current(execution->computation));
        execution->has_pending = false;
        memset(&execution->pending, 0, sizeof(execution->pending));
        execution->accepted++;
        allowance--;
        if (final) {
            execution->stop = CETTA_OWNED_EXECUTION_COMPLETE;
            execution->terminal = true;
            return execution->stop;
        }
    }
    execution->stop = CETTA_OWNED_EXECUTION_PAUSED;
    return execution->stop;
}

void cetta_owned_execution_cancel(CettaOwnedExecution *execution) {
    if (!execution)
        return;
    if (execution->computation && execution->release)
        execution->release(execution->computation);
    execution->computation = NULL;
    execution->has_pending = false;
    memset(&execution->pending, 0, sizeof(execution->pending));
    execution->terminal = true;
    execution->stop = CETTA_OWNED_EXECUTION_INTERRUPTED;
    execution->completion = CETTA_EVAL_INCOMPLETE_CANCELLED;
}
