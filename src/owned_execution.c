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

bool cetta_owned_execution_move(CettaOwnedExecution *destination,
    CettaOwnedExecution *source) {
    if (!destination || !source || destination == source ||
        destination->computation || destination->has_pending ||
        !source->computation || !source->next || !source->relocatable)
        return false;
    *destination = *source;
    *source = (CettaOwnedExecution){
        .terminal = true, .stop = CETTA_OWNED_EXECUTION_HANDOFF,
    };
    return true;
}

CettaOwnedExecutionStep cetta_owned_execution_sequence(
    CettaOwnedExecution *execution, uint64_t allowance,
    CettaOwnedReturnConsumer consume, void *consumer) {
    return cetta_owned_execution_sequence_with(execution,allowance,
        execution ? execution->next : NULL,consume,consumer);
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

bool cetta_owned_task_init(CettaOwnedTask *task, CettaOwnedExecution *execution,
    CettaOwnedTaskCompatible compatible, CettaOwnedReturnConsumer consume,
    void *context) {
    if (!task || task->ready.owner || !compatible || !consume ||
        !cetta_owned_execution_move(&task->execution, execution))
        return false;
    task->compatible = compatible;
    task->consume = consume;
    task->context = context;
    return true;
}

static inline __attribute__((always_inline))
bool owned_task_deliver_to(CettaOwnedVisit *visit,
    const CettaOwnedReturn *returned, CettaOwnedTaskAccount *account) {
    CettaOwnedTask *task = visit->task;
    if (account) account->compatibility++;
    if (!task->compatible(task->context)) {
        visit->context_refused = true;
        return false;
    }
    if (account) account->consumer++;
    return task->consume(task->context, returned);
}

static bool owned_task_deliver(void *raw, const CettaOwnedReturn *returned) {
    return owned_task_deliver_to(raw, returned, NULL);
}

typedef struct {
    CettaOwnedVisit *visit;
    CettaOwnedTaskAccount *account;
} OwnedTaskDelivery;

static bool owned_task_deliver_accounted(void *raw, const CettaOwnedReturn *returned) {
    OwnedTaskDelivery *delivery = raw;
    return owned_task_deliver_to(delivery->visit, returned, delivery->account);
}

CettaOwnedVisit cetta_owned_task_visit(CettaReadyQueue *queue, bool poll) {
    CettaReadyLink *link = cetta_ready_rotate(queue);
    return cetta_owned_task_poll((CettaOwnedTask *)link,poll);
}

static inline __attribute__((always_inline))
CettaOwnedVisit owned_task_poll(CettaOwnedTask *task, bool poll,
                               CettaOwnedTaskAccount *account) {
    CettaOwnedVisit visit = {.task = task, .step = CETTA_OWNED_EXECUTION_PAUSED};
    if (!task) return visit;
    if (!task->execution.terminal && poll) {
        bool compatible = true;
        if (!task->execution.has_pending) {
            if (account) account->compatibility++;
            compatible = task->compatible(task->context);
        }
        if (!compatible) visit.context_refused = true;
        else if (account) {
            OwnedTaskDelivery delivery = {&visit, account};
            visit.step = cetta_owned_execution_sequence_accounted_with(
                &task->execution, 1u, task->execution.next,
                owned_task_deliver_accounted, &delivery, account);
        } else
            visit.step = cetta_owned_execution_sequence(
                &task->execution, 1u, owned_task_deliver, &visit);
    }
    if (task->execution.terminal) {
        visit.step = task->execution.stop;
        if (task->ready.owner) {
            if (account) account->retirements++;
            cetta_ready_remove(task->ready.owner, &task->ready);
        }
    }
    return visit;
}

CettaOwnedVisit cetta_owned_task_poll(CettaOwnedTask *task, bool poll) {
    return owned_task_poll(task, poll, NULL);
}

CettaOwnedVisit cetta_owned_task_poll_accounted(
    CettaOwnedTask *task, bool poll, CettaOwnedTaskAccount *account) {
    if (account) *account = (CettaOwnedTaskAccount){0};
    return owned_task_poll(task, poll, account);
}

CettaOwnedVisit cetta_owned_task_visit_accounted(
    CettaReadyQueue *queue, bool poll, CettaOwnedTaskAccount *account) {
    if (account) *account = (CettaOwnedTaskAccount){0};
    CettaReadyLink *link = cetta_ready_rotate(queue);
    if (account && link) account->rotations++;
    return owned_task_poll((CettaOwnedTask *)link, poll, account);
}

void cetta_owned_task_cancel(CettaOwnedTask *task) {
    if (!task) return;
    if (task->ready.owner)
        cetta_ready_remove(task->ready.owner, &task->ready);
    cetta_owned_execution_cancel(&task->execution);
}
