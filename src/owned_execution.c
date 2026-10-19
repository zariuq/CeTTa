#include "owned_execution.h"

#include <stdlib.h>
#include <string.h>

struct CettaWorkCredit {
    CettaWorkCredit *parent, *root;
    size_t references, children;
    uint64_t balance, spent;
    uint64_t initial, remaining, total_spent;
    bool closed;
};

CettaWorkCredit *cetta_work_credit_new(uint64_t amount) {
    CettaWorkCredit *credit = calloc(1u, sizeof(*credit));
    if (!credit) return NULL;
    credit->root = credit;
    credit->references = 1u;
    credit->balance = credit->initial = credit->remaining = amount;
    return credit;
}

bool cetta_work_credit_retain(CettaWorkCredit *credit) {
    if (!credit || credit->references == SIZE_MAX) return false;
    credit->references++;
    return true;
}

CettaWorkCredit *cetta_work_credit_reserve(CettaWorkCredit *parent, uint64_t amount) {
    if (!parent || parent->closed || amount > parent->balance ||
        parent->children == SIZE_MAX || parent->references == SIZE_MAX)
        return NULL;
    CettaWorkCredit *credit = calloc(1u, sizeof(*credit));
    if (!credit) return NULL;
    credit->parent = parent;
    credit->root = parent->root;
    credit->references = 1u;
    credit->balance = amount;
    parent->references++;
    parent->children++;
    parent->balance -= amount;
    return credit;
}

bool cetta_work_credit_transfer(CettaWorkCredit *source, CettaWorkCredit *target,
    uint64_t amount) {
    if (!source || !target || source == target || source->closed || target->closed ||
        source->root != target->root || amount > source->balance ||
        amount > UINT64_MAX - target->balance) return false;
    source->balance -= amount;
    target->balance += amount;
    return true;
}

uint64_t cetta_work_credit_available(const CettaWorkCredit *credit) {
    return credit && !credit->closed ? credit->balance : 0u;
}

bool cetta_work_credit_spend(CettaWorkCredit *credit, uint64_t amount) {
    if (!credit || credit->closed || amount > credit->balance) return false;
    CettaWorkCredit *root = credit->root;
    /* The bank accounts for all descendants in constant time. Reserve and
     * refund merely move balances; they do not increase remaining work. */
    if (amount > root->remaining || amount > UINT64_MAX - root->total_spent ||
        amount > UINT64_MAX - credit->spent) return false;
    credit->balance -= amount;
    credit->spent += amount;
    root->remaining -= amount;
    root->total_spent += amount;
    return true;
}

bool cetta_work_credit_inspect(const CettaWorkCredit *credit, CettaWorkCreditView *view) {
    if (!credit || !view) return false;
    const CettaWorkCredit *root = credit->root;
    *view = (CettaWorkCreditView){
        .balance = credit->balance, .spent = credit->spent,
        .root_initial = root->initial, .root_remaining = root->remaining,
        .root_spent = root->total_spent, .children = credit->children,
        .closed = credit->closed,
    };
    return true;
}

bool cetta_work_credit_close(CettaWorkCredit *credit) {
    if (!credit || credit->closed || credit->children) return false;
    if (credit->parent) {
        CettaWorkCredit *parent = credit->parent;
        assert(!parent->closed && parent->children &&
            credit->balance <= UINT64_MAX - parent->balance);
        parent->balance += credit->balance;
        parent->children--;
        credit->balance = 0u;
    }
    credit->closed = true;
    return true;
}

void cetta_work_credit_release(CettaWorkCredit *credit) {
    /* Iterative parent release also handles arbitrarily deep lease nesting. */
    while (credit) {
        assert(credit->references);
        if (--credit->references) return;
        assert(!credit->children);
        if (!credit->closed) {
            bool closed = cetta_work_credit_close(credit);
            assert(closed); (void)closed;
        }
        CettaWorkCredit *parent = credit->parent;
        free(credit);
        credit = parent;
    }
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
