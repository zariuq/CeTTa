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
        execution->has_pending = false;
        memset(&execution->pending, 0, sizeof(execution->pending));
        execution->accepted++;
        allowance--;
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
