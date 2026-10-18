#include "owned_execution.h"

#include <string.h>

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

static bool owned_task_deliver(void *raw, const CettaOwnedReturn *returned) {
    CettaOwnedVisit *visit = raw;
    CettaOwnedTask *task = visit->task;
    if (!task->compatible(task->context)) {
        visit->context_refused = true;
        return false;
    }
    return task->consume(task->context, returned);
}

CettaOwnedVisit cetta_owned_task_visit(CettaReadyQueue *queue, bool poll) {
    CettaReadyLink *link = cetta_ready_rotate(queue);
    return cetta_owned_task_poll((CettaOwnedTask *)link,poll);
}

CettaOwnedVisit cetta_owned_task_poll(CettaOwnedTask *task, bool poll) {
    CettaOwnedVisit visit = {.task = task, .step = CETTA_OWNED_EXECUTION_PAUSED};
    if (!task) return visit;
    if (!task->execution.terminal && poll) {
        if (!task->execution.has_pending && !task->compatible(task->context))
            visit.context_refused = true;
        else
            visit.step = cetta_owned_execution_sequence(
                &task->execution, 1u, owned_task_deliver, &visit);
    }
    if (task->execution.terminal) {
        visit.step = task->execution.stop;
        if (task->ready.owner)
            cetta_ready_remove(task->ready.owner, &task->ready);
    }
    return visit;
}

void cetta_owned_task_cancel(CettaOwnedTask *task) {
    if (!task) return;
    if (task->ready.owner)
        cetta_ready_remove(task->ready.owner, &task->ready);
    cetta_owned_execution_cancel(&task->execution);
}
