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
        !source->computation || !source->next || !source->relocatable ||
        source->retiring)
        return false;
    *destination = *source;
    *source = (CettaOwnedExecution){
        .terminal = true, .stop = CETTA_OWNED_EXECUTION_HANDOFF,
    };
    return true;
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
    return cetta_owned_execution_sequence_with(execution,allowance,
        execution ? execution->next : NULL,consume,consumer);
}

bool cetta_owned_execution_set_release_poll(CettaOwnedExecution *execution,
    CettaOwnedReleasePoll poll) {
    if (!execution || !execution->computation || !execution->release ||
        !poll || execution->retiring || execution->terminal ||
        execution->has_pending || execution->accepted || execution->release_poll)
        return false;
    execution->release_poll = poll;
    /* An earlier move certificate did not cover this callback's captures. */
    execution->relocatable = false;
    return true;
}

static void owned_execution_begin_retirement(CettaOwnedExecution *execution) {
    execution->has_pending = false;
    memset(&execution->pending, 0, sizeof(execution->pending));
    execution->terminal = true;
    execution->stop = CETTA_OWNED_EXECUTION_INTERRUPTED;
    execution->completion = CETTA_EVAL_INCOMPLETE_CANCELLED;
    execution->retiring = true;
}

CettaOwnedReleaseStep cetta_owned_execution_cancel_poll(
    CettaOwnedExecution *execution, uint64_t allowance, bool allow_atomic,
    uint64_t *used) {
    uint64_t spent = 0u;
    if (used) *used = 0u;
    if (!execution || !execution->computation)
        return CETTA_OWNED_RELEASE_COMPLETE;
    if (!allowance) return CETTA_OWNED_RELEASE_PENDING;
    if (!execution->retiring) {
        owned_execution_begin_retirement(execution);
        spent++;
    }
    while (spent < allowance) {
        if (execution->release_ready) {
            if (execution->release)
                execution->release(execution->computation);
            execution->computation = NULL;
            spent++;
            if (used) *used = spent;
            return CETTA_OWNED_RELEASE_COMPLETE;
        }
        if (!execution->release_poll) {
            if (!allow_atomic) {
                if (used) *used = spent;
                return CETTA_OWNED_RELEASE_ATOMIC;
            }
            execution->release_ready = true;
            continue;
        }
        uint64_t used_cleanup = 0u;
        CettaOwnedReleaseStep status = execution->release_poll(
            execution->computation, allowance - spent, allow_atomic,
            &used_cleanup);
        assert(used_cleanup <= allowance - spent);
        assert(status >= CETTA_OWNED_RELEASE_PENDING &&
            status <= CETTA_OWNED_RELEASE_ATOMIC);
        spent += used_cleanup;
        if (status == CETTA_OWNED_RELEASE_COMPLETE)
            execution->release_ready = true;
        else {
            if (used) *used = spent;
            return status;
        }
    }
    if (used) *used = spent;
    return CETTA_OWNED_RELEASE_PENDING;
}

void cetta_owned_execution_cancel(CettaOwnedExecution *execution) {
    if (!execution) return;
    while (cetta_owned_execution_cancel_poll(execution, UINT64_MAX, true,
            NULL) != CETTA_OWNED_RELEASE_COMPLETE) {}
    /* Empty/uninitialized owners also acquire the existing terminal verdict. */
    owned_execution_begin_retirement(execution);
}

bool cetta_owned_task_init(CettaOwnedTask *task, CettaOwnedExecution *execution,
    CettaOwnedTaskAdmit admit, CettaOwnedReturnConsumer consume,
    void *context) {
    if (!task || task->ready.owner || !admit || !consume ||
        !cetta_owned_execution_move(&task->execution, execution))
        return false;
    task->admit = admit;
    task->consume = consume;
    task->context = context;
    return true;
}

static inline __attribute__((always_inline))
bool owned_task_admit(CettaOwnedVisit *visit, CettaOwnedTaskAccount *account,
    CettaOwnedTaskAccountObserver observer,void *observer_context) {
    CettaOwnedTask *task = visit->task;
    CettaOwnedTaskAdmission admission=task->admit(task->context);
    if (admission<CETTA_OWNED_TASK_ADMISSION_READY ||
        admission>CETTA_OWNED_TASK_ADMISSION_INVALIDATED)
        admission=CETTA_OWNED_TASK_ADMISSION_INVALIDATED;
    if (account) account->admission++;
    cetta_owned_task_account_record(account,observer,observer_context,
        CETTA_OWNED_TASK_PHASE_ADMISSION,(uint64_t)admission);
    visit->admission=admission;
    return admission==CETTA_OWNED_TASK_ADMISSION_READY;
}

static inline __attribute__((always_inline))
bool owned_task_deliver_to(CettaOwnedVisit *visit,
    const CettaOwnedReturn *returned, CettaOwnedTaskAccount *account,
    CettaOwnedTaskAccountObserver observer,void *observer_context) {
    CettaOwnedTask *task = visit->task;
    if (!owned_task_admit(visit,account,observer,observer_context)) return false;
    bool accepted=task->consume(task->context, returned);
    if (account) account->consumer++;
    cetta_owned_task_account_record(account,observer,observer_context,
        CETTA_OWNED_TASK_PHASE_CONSUMER,accepted?1u:0u);
    return accepted;
}

static bool owned_task_deliver(void *raw, const CettaOwnedReturn *returned) {
    return owned_task_deliver_to(raw, returned, NULL, NULL, NULL);
}

typedef struct {
    CettaOwnedVisit *visit;
    CettaOwnedTaskAccount *account;
    CettaOwnedTaskAccountObserver observer;
    void *observer_context;
} OwnedTaskDelivery;

static bool owned_task_deliver_accounted(void *raw, const CettaOwnedReturn *returned) {
    OwnedTaskDelivery *delivery = raw;
    return owned_task_deliver_to(delivery->visit, returned, delivery->account,
        delivery->observer,delivery->observer_context);
}

CettaOwnedVisit cetta_owned_task_visit(CettaReadyQueue *queue, bool poll) {
    CettaReadyLink *link = cetta_ready_rotate(queue);
    return cetta_owned_task_poll((CettaOwnedTask *)link,poll);
}

static inline __attribute__((always_inline))
CettaOwnedVisit owned_task_poll(CettaOwnedTask *task, bool poll,
    CettaOwnedTaskAccount *account,CettaOwnedTaskAccountObserver observer,
    void *observer_context) {
    CettaOwnedVisit visit = {.task = task, .step = CETTA_OWNED_EXECUTION_PAUSED};
    if (!task) return visit;
    if (!task->execution.terminal && poll) {
        bool admitted = true;
        if (!task->execution.has_pending) {
            admitted=owned_task_admit(&visit,account,observer,observer_context);
        }
        if (admitted && account) {
            OwnedTaskDelivery delivery = {&visit,account,observer,observer_context};
            visit.step = cetta_owned_execution_sequence_accounted_with(
                &task->execution, 1u, task->execution.next,
                owned_task_deliver_accounted, &delivery, account,
                observer,observer_context);
        } else if (admitted)
            visit.step = cetta_owned_execution_sequence(
                &task->execution, 1u, owned_task_deliver, &visit);
    }
    if (task->execution.terminal) {
        visit.step = task->execution.stop;
        if (task->ready.owner) {
            cetta_ready_remove(task->ready.owner, &task->ready);
            if (account) account->retirements++;
            cetta_owned_task_account_record(account,observer,observer_context,
                CETTA_OWNED_TASK_PHASE_RETIREMENT,0u);
        }
    }
    return visit;
}

CettaOwnedVisit cetta_owned_task_poll(CettaOwnedTask *task, bool poll) {
    return owned_task_poll(task, poll, NULL, NULL, NULL);
}

CettaOwnedVisit cetta_owned_task_poll_accounted(
    CettaOwnedTask *task, bool poll, CettaOwnedTaskAccount *account) {
    if (account) *account = (CettaOwnedTaskAccount){0};
    return owned_task_poll(task, poll, account, NULL, NULL);
}

CettaOwnedVisit cetta_owned_task_poll_observed(
    CettaOwnedTask *task,bool poll,CettaOwnedTaskAccount *account,
    CettaOwnedTaskAccountObserver observer,void *observer_context) {
    if (account) *account=(CettaOwnedTaskAccount){0};
    return owned_task_poll(task,poll,account,observer,observer_context);
}

CettaOwnedVisit cetta_owned_task_visit_accounted(
    CettaReadyQueue *queue, bool poll, CettaOwnedTaskAccount *account) {
    if (account) *account = (CettaOwnedTaskAccount){0};
    CettaReadyLink *link = cetta_ready_rotate(queue);
    if (account && link) {
        account->rotations++;
        cetta_owned_task_account_record(account,NULL,NULL,
            CETTA_OWNED_TASK_PHASE_ROTATION,0u);
    }
    return owned_task_poll((CettaOwnedTask *)link,poll,account,NULL,NULL);
}

CettaOwnedReleaseStep cetta_owned_task_cancel_poll(CettaOwnedTask *task,
    uint64_t allowance, bool allow_atomic, uint64_t *used,
    CettaOwnedTaskAccount *account, CettaOwnedTaskAccountObserver observer,
    void *observer_context) {
    if (used) *used=0u;
    if (account) *account=(CettaOwnedTaskAccount){0};
    if (!task) return CETTA_OWNED_RELEASE_COMPLETE;
    if (!allowance) return task->ready.owner || task->execution.computation
        ? CETTA_OWNED_RELEASE_PENDING : CETTA_OWNED_RELEASE_COMPLETE;
    if (task->ready.owner) {
        cetta_ready_remove(task->ready.owner, &task->ready);
        if (account) account->retirements++;
        cetta_owned_task_account_record(account,observer,observer_context,
            CETTA_OWNED_TASK_PHASE_RETIREMENT,0u);
    }
    if (!task->execution.computation) return CETTA_OWNED_RELEASE_COMPLETE;
    uint64_t spent=0u;
    CettaOwnedReleaseStep status=cetta_owned_execution_cancel_poll(
        &task->execution,allowance,allow_atomic,&spent);
    if (account) { account->cleanup_polls++;account->cleanup_work=spent; }
    cetta_owned_task_account_record(account,observer,observer_context,
        CETTA_OWNED_TASK_PHASE_CLEANUP,spent);
    if (used) *used=spent;
    return status;
}

void cetta_owned_task_cancel(CettaOwnedTask *task) {
    if (!task) return;
    while (cetta_owned_task_cancel_poll(task,UINT64_MAX,true,NULL,NULL,NULL,NULL)!=
            CETTA_OWNED_RELEASE_COMPLETE) {}
    /* Preserve the synchronous cancellation verdict for an empty/moved task. */
    owned_execution_begin_retirement(&task->execution);
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
