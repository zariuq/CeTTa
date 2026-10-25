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

static inline CettaOwnedExecutionStep owned_execution_pull(
        CettaOwnedExecution *execution) {
    if (!execution || !execution->computation || !execution->next)
        return CETTA_OWNED_EXECUTION_HANDOFF;
    if (execution->terminal)
        return execution->stop;
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
    return CETTA_OWNED_EXECUTION_RETURN;
}

static inline void owned_execution_accept(CettaOwnedExecution *execution) {
    bool final = execution->pending.final &&
        execution->completion == CETTA_EVAL_COMPLETE &&
        (!execution->final_is_current ||
         execution->final_is_current(execution->computation));
    execution->has_pending = false;
    memset(&execution->pending, 0, sizeof(execution->pending));
    execution->accepted++;
    if (final) {
        execution->stop = CETTA_OWNED_EXECUTION_COMPLETE;
        execution->terminal = true;
    }
}

CettaOwnedExecutionStep cetta_owned_execution_loan(
        CettaOwnedExecution *execution, const CettaOwnedReturn **returned) {
    if (!returned)
        return CETTA_OWNED_EXECUTION_HANDOFF;
    *returned = NULL;
    CettaOwnedExecutionStep step = owned_execution_pull(execution);
    if (step == CETTA_OWNED_EXECUTION_RETURN)
        *returned = &execution->pending;
    return step;
}

bool cetta_owned_execution_accept(CettaOwnedExecution *execution) {
    if (!execution || execution->terminal || !execution->has_pending)
        return false;
    owned_execution_accept(execution);
    return true;
}

CettaOwnedExecutionStep cetta_owned_execution_sequence(
    CettaOwnedExecution *execution, uint64_t allowance,
    CettaOwnedReturnConsumer consume, void *consumer) {
    if (!execution || !execution->computation || !execution->next || !consume)
        return CETTA_OWNED_EXECUTION_HANDOFF;
    if (execution->terminal)
        return execution->stop;
    while (allowance > 0u) {
        CettaOwnedExecutionStep step = owned_execution_pull(execution);
        if (step != CETTA_OWNED_EXECUTION_RETURN)
            return step;
        if (!consume(consumer, &execution->pending)) {
            execution->stop = CETTA_OWNED_EXECUTION_PAUSED;
            return execution->stop;
        }
        owned_execution_accept(execution);
        allowance--;
        if (execution->terminal)
            return execution->stop;
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

static void owned_bind_release(void *context) {
    CettaOwnedBind *bind = context;
    /* The body's captures may borrow the producer's pending packet. */
    if (bind->has_body)
        cetta_owned_execution_cancel(&bind->body);
    cetta_owned_execution_cancel(&bind->producer);
    bind->has_body = bind->body_returned = bind->forwarded = false;
    bind->awaiting_body = false;
}

static CettaOwnedExecutionStep owned_bind_next(void *context,
        CettaOwnedReturn *returned, CettaEvalCompletion *completion) {
    CettaOwnedBind *bind = context;
    if (bind->awaiting_body)
        return CETTA_OWNED_EXECUTION_HANDOFF;
    if (bind->forwarded) {
        owned_execution_accept(&bind->producer);
        bind->forwarded = false;
    }
    if (bind->has_body) {
        /* Reaching next means the outer owner accepted the previous body
         * return. Refusal never advances this boundary. */
        if (bind->body_returned) {
            owned_execution_accept(&bind->body);
            bind->body_returned = false;
        }
    } else {
        CettaOwnedExecutionStep step = owned_execution_pull(&bind->producer);
        if (step != CETTA_OWNED_EXECUTION_RETURN) {
            cetta_eval_completion_record(completion, bind->producer.completion);
            return step;
        }
        CettaCallOutcome outcome = bind->producer.pending.outcome;
        if (!cetta_call_outcome_shape_valid(outcome)) {
            cetta_eval_completion_record(completion, CETTA_EVAL_INCOMPLETE_HOST_FAILURE);
            return CETTA_OWNED_EXECUTION_INTERRUPTED;
        }
        if (outcome.kind == CETTA_CALL_RAISED) {
            *returned = bind->producer.pending;
            returned->final = false;
            bind->forwarded = true;
            return CETTA_OWNED_EXECUTION_RETURN;
        }
        if (outcome.kind == CETTA_CALL_INTERRUPTED) {
            cetta_eval_completion_record(completion, CETTA_EVAL_INCOMPLETE_CANCELLED);
            return CETTA_OWNED_EXECUTION_INTERRUPTED;
        }
        if (outcome.kind == CETTA_CALL_FAILURE) {
            owned_execution_accept(&bind->producer);
            return bind->producer.terminal ? bind->producer.stop
                                          : CETTA_OWNED_EXECUTION_PAUSED;
        }
        if (!bind->continuation(bind->context, &bind->producer.pending, &bind->body)) {
            bind->awaiting_body = true;
            return CETTA_OWNED_EXECUTION_HANDOFF;
        }
        bind->has_body = true;
        if (!bind->body.computation || !bind->body.next) {
            cetta_eval_completion_record(completion, CETTA_EVAL_INCOMPLETE_HOST_FAILURE);
            return CETTA_OWNED_EXECUTION_INTERRUPTED;
        }
    }
    CettaOwnedExecutionStep step = owned_execution_pull(&bind->body);
    if (step == CETTA_OWNED_EXECUTION_RETURN) {
        *returned = bind->body.pending;
        /* Completion needs the body's and producer's current certificates,
         * checked after consumption. A later pull makes that observation. */
        returned->final = false;
        bind->body_returned = true;
        return step;
    }
    if (step != CETTA_OWNED_EXECUTION_COMPLETE ||
        bind->body.completion != CETTA_EVAL_COMPLETE) {
        cetta_eval_completion_record(completion, bind->body.completion);
        return step;
    }
    cetta_owned_execution_cancel(&bind->body);
    bind->has_body = false;
    owned_execution_accept(&bind->producer);
    return bind->producer.terminal ? bind->producer.stop
                                  : CETTA_OWNED_EXECUTION_PAUSED;
}

bool cetta_owned_bind_open(CettaOwnedExecution *execution,
        CettaOwnedBind *bind, CettaOwnedExecution *producer,
        CettaOwnedBindBody continuation, void *context) {
    if (!execution || !bind || !producer || execution == producer ||
        producer == &bind->producer || producer == &bind->body ||
        execution == &bind->producer || execution == &bind->body ||
        !producer->computation || !producer->next || !continuation)
        return false;
    *bind = (CettaOwnedBind){.producer = *producer,
        .continuation = continuation, .context = context};
    *producer = (CettaOwnedExecution){.terminal = true,
        .stop = CETTA_OWNED_EXECUTION_COMPLETE};
    cetta_owned_execution_init(execution, bind, owned_bind_next, owned_bind_release);
    return true;
}

const CettaOwnedReturn *cetta_owned_bind_pending(const CettaOwnedBind *bind) {
    return bind && bind->awaiting_body && bind->producer.has_pending
        ? &bind->producer.pending : NULL;
}

bool cetta_owned_bind_resume(CettaOwnedExecution *execution,
        CettaOwnedBind *bind, CettaOwnedExecution *body) {
    if (!execution || !bind || !body || body == execution ||
        body == &bind->body || body == &bind->producer ||
        execution->computation != bind || !bind->awaiting_body ||
        !execution->terminal || execution->stop != CETTA_OWNED_EXECUTION_HANDOFF ||
        !body->computation || !body->next)
        return false;
    bind->body = *body;
    *body = (CettaOwnedExecution){.terminal = true,
        .stop = CETTA_OWNED_EXECUTION_COMPLETE};
    bind->has_body = true;
    bind->awaiting_body = false;
    execution->terminal = false;
    execution->stop = CETTA_OWNED_EXECUTION_PAUSED;
    return true;
}
