#include "identity_counter.h"

/* One retained cohort, with fixed owner allowances and private resumptions.
 * Parent frames, hub, services and public cursors remain quiescent through
 * the executor join. A queue node is never permission to mutate that parent. */
typedef enum {
    PRIME_REFILL_RETAINED,
    PRIME_REFILL_QUEUED,
    PRIME_REFILL_RUNNING,
} PrimeNativeRefillState;

typedef struct {
    CettaNativeCostLedger cost;
    CettaNativeCostSegment schedule;
    uint64_t identity, grant, spent, before_steps, after_steps;
    uint64_t before_output, after_output, forwarded;
    uint64_t started_ns, finished_ns;
    bool completed, requeued, push_refused;
} PrimeNativeRefillSegment;

typedef struct {
    uintptr_t address;
    uint64_t identity, occurrence;
} PrimeNativeRefillDestination;

struct PrimeNativeRefillHistory {
    PrimeNativeRefillHistory *next;
    PrimeNativeRefillSegment *segments;
    CettaNativeCostSegment *joined;
    PrimeNativeRefillDestination *destinations;
    size_t destination_count;
    size_t capacity, length;
    uint32_t workers, overlap;
    uint64_t reserved, spent;
    bool metered, imported, executor_ok, push_refused, cancelled;
};

struct PrimeNativeRefillItem {
    PrimeNativeParallel *parallel;
    CettaOwnedContinuation owned;
    uint64_t reserved, remaining, spent, last_identity;
    size_t offset, capacity, length;
    _Atomic unsigned state;
    bool push_refused, invalid;
};

typedef struct {
    PrimeNativeParallelRun run;
    PrimeNativeRefillItem *items;
    size_t count;
    uint64_t quantum;
    PrimeNativeRefillHistory *history;
    uint64_t *worker_previous;
    _Atomic uint64_t next_order;
    uint32_t workers;
    bool metered;
} PrimeNativeRefillRun;

static _Atomic uint64_t prime_native_refill_next_identity = 1u;

static void prime_native_refill_history_free(PrimeNativeRefillHistory *history) {
    while (history) {
        PrimeNativeRefillHistory *next = history->next;
        for (size_t i = 0u; i < history->capacity; i++)
            cetta_native_cost_free(&history->segments[i].cost);
        free(history->destinations);
        free(history->joined);
        free(history->segments);
        free(history);
        history = next;
    }
}

static void prime_native_refill_history_storage(const PrimeNativeRefillHistory *history,
        CettaStorageCensus *census) {
    for (; history; history = history->next) {
        if (!cetta_storage_object(census, history, sizeof(*history), CETTA_STORAGE_BLOCK)) return;
        cetta_storage_array(census, history->destinations, history->destination_count,
            sizeof(*history->destinations), 0u);
        cetta_storage_array(census, history->segments, history->capacity,
            sizeof(*history->segments), 0u);
        cetta_storage_array(census, history->joined, history->capacity,
            sizeof(*history->joined), 0u);
        for (size_t i = 0u; i < history->capacity; i++)
            cetta_storage_array(census, history->segments[i].cost.events,
                history->segments[i].cost.capacity, sizeof(*history->segments[i].cost.events), 0u);
    }
}

/* These scalar addresses/issuance identities do not retain or dereference
 * ledgers. A resumed caller must select the same still-live account chain.
 * Closing that external account leaves an explicit unimported obligation. */
static bool prime_native_refill_destinations_match(const PrimeNativeRefillHistory *history) {
    size_t i=0u;
    for (CettaNativeCostScope *scope=cetta_native_cost_current_scope;scope;scope=scope->previous) {
        if (i>=history->destination_count) return false;
        const PrimeNativeRefillDestination *expected=&history->destinations[i++];
        if ((uintptr_t)scope->ledger!=expected->address ||
            (scope->ledger?scope->ledger->identity:0u)!=expected->identity ||
            scope->occurrence!=expected->occurrence) return false;
    }
    return i==history->destination_count;
}

static bool prime_native_refill_join_pending(PrimeNativeControl *owner) {
    PrimeNativeRefillHistory *history=owner->refill_pending;
    if (!history) return true;
    if (!history->metered || history->imported ||
        !prime_native_refill_destinations_match(history) ||
        !cetta_native_cost_join_segments(history->joined,history->length,history->workers))
        return false;
    history->imported=true;
    owner->refill_pending=NULL;
    return true;
}

static void prime_native_refill_receipt(PrimeNativeControl *owner,
        const PrimeNativeRefillHistory *history) {
    if (!owner->policy.receipts) return;
    Arena *arena=owner->driver.arena;
    Atom **nodes=cetta_malloc(history->length*sizeof(*nodes));
    size_t n=0u;
    for (size_t i=0u;i<history->capacity;i++) {
        const PrimeNativeRefillSegment *segment=&history->segments[i];
        if (!segment->schedule.order) continue;
        /* The graph identity is present in recording-only mode too. Its
         * cost origin remains Untracked rather than a forged ledger ID. */
        nodes[n++]=atom_expr(arena,(Atom *[]){atom_symbol(arena,"NativeRefillSegment"),
            prime_native_occurrence_atom(arena,segment->identity),
            prime_native_occurrence_atom(arena,segment->schedule.occurrence),
            prime_native_occurrence_atom(arena,segment->schedule.order),
            prime_native_occurrence_atom(arena,segment->schedule.owner_predecessor),
            prime_native_occurrence_atom(arena,segment->schedule.worker_predecessor),
            prime_native_occurrence_atom(arena,segment->schedule.worker),
            prime_native_occurrence_atom(arena,segment->grant),
            prime_native_occurrence_atom(arena,segment->spent),
            history->metered?prime_native_occurrence_atom(arena,segment->cost.identity):atom_symbol(arena,"Untracked"),
            history->metered?prime_native_occurrence_atom(arena,segment->cost.total.work):atom_symbol(arena,"Untracked"),
            history->metered?prime_native_occurrence_atom(arena,segment->cost.total.span):atom_symbol(arena,"Untracked"),
            prime_native_occurrence_atom(arena,segment->started_ns),
            prime_native_occurrence_atom(arena,segment->finished_ns)},14u);
    }
    assert(n==history->length);
    Atom *receipt=atom_expr(arena,(Atom *[]){atom_symbol(arena,"NativeParallelRefill"),
        prime_native_occurrence_atom(arena,history->overlap),
        prime_native_occurrence_atom(arena,history->reserved),
        prime_native_occurrence_atom(arena,history->spent),
        atom_expr(arena,nodes,(CettaExprLen)n)},5u);
    owner->parallel_waves=cetta_realloc(owner->parallel_waves,
        (owner->parallel_wave_count+1u)*sizeof(*owner->parallel_waves));
    owner->parallel_waves[owner->parallel_wave_count++]=receipt;
    free(nodes);
}

static bool prime_native_refill_can_continue(const PrimeNativeParallel *parallel) {
    const PrimeNativeControl *child = parallel->child;
    /* A paid split is a retained coordinator handoff, not another
     * private slice. Leave the exact frontier and unused grant intact. */
    return child && !child->driver.capsule_frontier &&
        !parallel->completed && !parallel->coordinator_only &&
        !child->failed && !child->invalidated && !child->effect_pending &&
        !child->need_pending && !child->driver.capacity_blocked &&
        !child->registry_read.incomplete && !prime_native_control_waiting_verdict(child) &&
        child->completion != CETTA_EVAL_INCOMPLETE_CANCELLED &&
        !prime_native_control_closed(child);
}

static bool prime_native_refill_task(CettaParallelWorker *worker, void *raw, void *context) {
    PrimeNativeRefillRun *refill = context;
    PrimeNativeParallel *parallel = raw;
    PrimeNativeRefillItem *item = parallel->refill_item;
    unsigned queued = PRIME_REFILL_QUEUED;
    if (!item || item->parallel != parallel || item->owned.occurrence_id != parallel->occurrence ||
        !atomic_compare_exchange_strong_explicit(&item->state, &queued, PRIME_REFILL_RUNNING,
            memory_order_acquire, memory_order_relaxed)) return false;
    uint32_t index = cetta_parallel_worker_index(worker);
    if (index >= refill->workers || !item->remaining) {
        item->invalid = true;
        atomic_store_explicit(&item->state, PRIME_REFILL_RETAINED, memory_order_release);
        return false;
    }
    uint64_t grant = item->remaining < refill->quantum ? item->remaining : refill->quantum;
    parallel->grant = grant;
    parallel->spent = 0u;
    parallel->phase = 0u;
    PrimeNativeRefillSegment *segment = NULL;
    if (refill->history) {
        if (item->length >= item->capacity) {
            item->invalid = true;
            atomic_store_explicit(&item->state, PRIME_REFILL_RETAINED, memory_order_release);
            return false;
        }
        if (refill->metered && !parallel->slice_cost.identity)
            cetta_native_cost_init(&parallel->slice_cost);
        segment = &refill->history->segments[item->offset + item->length];
        segment->identity = refill->metered ? parallel->slice_cost.identity :
            cetta_identity_try_take(&prime_native_refill_next_identity);
        segment->schedule = (CettaNativeCostSegment){.ledger=&segment->cost,
            .occurrence=item->owned.occurrence_id,
            .order=atomic_fetch_add_explicit(&refill->next_order, 1u, memory_order_relaxed) + 1u,
            .worker=index, .owner_predecessor=item->last_identity,
            .worker_predecessor=refill->worker_previous[index]};
        segment->grant=grant;
        segment->before_steps=parallel->child->steps;
        segment->before_output=parallel->child->output.len;
        segment->forwarded=parallel->forwarded;
    }
    /* This call returns only after all actual per-task TLS/service/cost and
     * recording scopes have left. The executor's worker scope stays active. */
    bool ok = prime_native_parallel_task(worker, parallel, &refill->run);
    uint64_t spent = parallel->spent;
    bool bounded = spent <= grant && spent <= item->remaining &&
        spent <= UINT64_MAX - item->spent;
    if (bounded) { item->spent += spent; item->remaining -= spent; }
    else item->invalid = true;
    bool again = ok && bounded && spent == grant && item->remaining &&
        prime_native_refill_can_continue(parallel) &&
        !atomic_load_explicit(&refill->run.cancelled, memory_order_acquire);
    /* Dispatch is an actual attempted queue operation, separate from child
     * producer quanta. Charge before publishing the capsule to another worker. */
    if (again && refill->metered)
        cetta_native_cost_append(&parallel->slice_cost, CETTA_COST_WORKER_DISPATCH,
            1u, item->owned.occurrence_id);
    if (segment) {
        segment->cost=parallel->slice_cost;
        memset(&parallel->slice_cost, 0, sizeof(parallel->slice_cost));
        segment->spent=spent;
        segment->after_steps=parallel->child->steps;
        segment->after_output=parallel->child->output.len;
        segment->completed=parallel->completed;
        segment->started_ns=parallel->started_ns;
        segment->finished_ns=parallel->finished_ns;
        segment->requeued=again;
        item->last_identity=segment->identity;
        refill->worker_previous[index]=segment->identity;
        item->length++;
    }
    if (!again) {
        atomic_store_explicit(&item->state, PRIME_REFILL_RETAINED, memory_order_release);
        return ok && bounded;
    }
    /* Every write to the mutable capsule and its segment precedes the push.
     * Success transfers access; there is deliberately no subsequent read of
     * item, parallel or segment on that branch. */
    atomic_store_explicit(&item->state, PRIME_REFILL_QUEUED, memory_order_release);
    if (cetta_parallel_executor_push(cetta_parallel_worker_executor(worker), parallel))
        return true;
    item->push_refused=true;
    if (segment) { segment->requeued=false; segment->push_refused=true; }
    atomic_store_explicit(&item->state, PRIME_REFILL_RETAINED, memory_order_release);
    return true;
}

/* False means preparation never entered the executor: the original complete
 * capsules remain available to the ordinary existing execute path. */
static bool prime_native_refill_execute(PrimeNativeControl *owner,
        PrimeNativeWork **works, const CettaOwnedContinuation *owned,
        size_t count, uint32_t workers, uint64_t *spent_out) {
    if (owner->refill_pending || !owner->driver.ordinary_refill || !owner->driver.ordinary_ordered ||
        !owner->driver.ordinary_ready_queue || !count || workers < 2u ||
        count > SIZE_MAX / sizeof(PrimeNativeRefillItem)) return false;
    uint64_t quantum=owner->policy.worker_quantum ? owner->policy.worker_quantum : 1u;
    bool metered=cetta_native_cost_active(), recording=metered || owner->policy.receipts;
    size_t capacity=0u;
    uint64_t reserved=0u;
    for (size_t i=0u;i<count;i++) {
        PrimeNativeParallel *parallel=works[i]->parallel;
        if (!works[i]->task_initialized || works[i]->resource_step || works[i]->dialect ||
            !parallel || parallel->execution.profile != PRIME_NATIVE_EXECUTION_NONE ||
            !parallel->child || parallel->coordinator_only || parallel->refill_item ||
            !parallel->grant || parallel->occurrence != owned[i].occurrence_id ||
            owned[i].payload != works[i] || works[i]->owner != owner ||
            !prime_native_parallel_validate(owner, parallel)) return false;
        for (size_t j=0u;j<i;j++)
            if (works[j]==works[i] || works[j]->parallel==parallel ||
                works[j]->parallel->child==parallel->child ||
                owned[j].occurrence_id==owned[i].occurrence_id) return false;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
        recording=recording || parallel->history_enabled;
#endif
        if (parallel->grant>UINT64_MAX-reserved) return false;
        reserved+=parallel->grant;
    }
    if (recording) {
        for (size_t i=0u;i<count;i++) {
            uint64_t grant=works[i]->parallel->grant;
            uint64_t chunks=grant/quantum + (grant%quantum != 0u);
            if (chunks > SIZE_MAX-capacity) return false;
            capacity+=(size_t)chunks;
        }
        if (capacity > SIZE_MAX/sizeof(PrimeNativeRefillSegment) ||
            capacity > SIZE_MAX/sizeof(CettaNativeCostSegment)) return false;
    }
    PrimeNativeRefillItem *items=calloc(count,sizeof(*items));
    PrimeNativeRefillHistory *history=recording ? calloc(1u,sizeof(*history)) : NULL;
    uint64_t *previous=recording ? calloc(workers,sizeof(*previous)) : NULL;
    if (!items || (recording && (!history || !previous))) {
        free(items);free(history);free(previous);return false;
    }
    if (history) {
        history->capacity=capacity;history->workers=workers;history->metered=metered;
        history->segments=calloc(capacity,sizeof(*history->segments));
        history->joined=calloc(capacity,sizeof(*history->joined));
        if (!history->segments || !history->joined) {
            free(history->segments);free(history->joined);free(history);
            free(items);free(previous);return false;
        }
    }
    if (metered) {
        size_t n=0u;
        for (CettaNativeCostScope *s=cetta_native_cost_current_scope;s;s=s->previous) {
            if (n==SIZE_MAX/sizeof(*history->destinations)) {
                prime_native_refill_history_free(history);free(items);free(previous);return false;
            }
            n++;
        }
        history->destinations=calloc(n,sizeof(*history->destinations));
        if (!history->destinations) {
            prime_native_refill_history_free(history);free(items);free(previous);return false;
        }
        history->destination_count=n;n=0u;
        for (CettaNativeCostScope *s=cetta_native_cost_current_scope;s;s=s->previous)
            history->destinations[n++]=(PrimeNativeRefillDestination){
                .address=(uintptr_t)s->ledger,.identity=s->ledger?s->ledger->identity:0u,
                .occurrence=s->occurrence};
    }
    PrimeNativeRefillRun refill={.run={.library=g_library_context,
        .receipts=owner->policy.receipts},.items=items,.count=count,.quantum=quantum,
        .history=history,.worker_previous=previous,.workers=workers,.metered=metered};
    atomic_init(&refill.next_order,0u);
    size_t offset=0u;
    for (size_t i=0u;i<count;i++) {
        PrimeNativeParallel *parallel=works[i]->parallel;
        uint64_t grant=parallel->grant;
        items[i]=(PrimeNativeRefillItem){.parallel=parallel,.owned=owned[i],
            .reserved=grant,.remaining=grant};
        atomic_init(&items[i].state,PRIME_REFILL_QUEUED);
        if (history) {
            size_t chunks=(size_t)(grant/quantum+(grant%quantum!=0u));
            items[i].offset=offset;items[i].capacity=chunks;
            offset+=chunks;history->reserved+=grant;
        }
    }
    CettaParallelExecutor executor;
    CettaParallelExecutorConfig config={.thread_count=workers,.prefer_persistent_workers=true,
        .user=&refill,.task_fn=prime_native_refill_task,
        .worker_enter=prime_native_parallel_enter,.worker_leave=prime_native_parallel_leave,
        .worker_failure_message="native retained refill episode"};
    if (!cetta_parallel_executor_init(&executor,&config)) {
        prime_native_refill_history_free(history);free(items);free(previous);return false;
    }
    bool queued=true;
    for (size_t i=0u;queued && i<count;i++)
        queued=cetta_parallel_executor_push(&executor,items[i].parallel);
    if (!queued) {
        cetta_parallel_executor_free(&executor);prime_native_refill_history_free(history);
        free(items);free(previous);return false;
    }
    for (size_t i=0u;i<count;i++) {
        items[i].parallel->refill_item=&items[i];
        cetta_native_cost_charge(CETTA_COST_WORKER_DISPATCH,1u);
    }
    bool ran=cetta_parallel_executor_run(&executor);
    /* run has synchronously joined every worker; this ordinary bool is now
     * quiescent. While workers run, cancellation is handled by push/pop. */
    bool cancelled=executor.queue.cancelled;
    cetta_parallel_executor_free(&executor);
    uint64_t spent=0u;
    bool refused=!ran;
    for (size_t i=0u;i<count;i++) {
        PrimeNativeRefillItem *item=&items[i];PrimeNativeParallel *parallel=item->parallel;
        assert(atomic_load_explicit(&item->state,memory_order_acquire)!=PRIME_REFILL_RUNNING);
        assert(item->spent<=item->reserved && item->remaining==item->reserved-item->spent);
        assert(item->spent<=UINT64_MAX-spent);spent+=item->spent;
        parallel->refill_item=NULL;
        parallel->grant=item->reserved;
        parallel->spent=item->spent;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
        prime_native_parallel_history_publish(parallel);
#endif
        refused=refused || item->push_refused || item->invalid;
        if (history) {
            for (size_t j=0u;j<item->length;j++)
                history->joined[history->length++]=history->segments[item->offset+j].schedule;
            history->push_refused=history->push_refused || item->push_refused;
        }
    }
    /* Executor join is paid once even when account validation refuses. */
    for (size_t i=0u;i<count;i++) cetta_native_cost_charge(CETTA_COST_WORKER_JOIN,1u);
    if (history) {
        history->overlap=atomic_load_explicit(&refill.run.overlap,memory_order_relaxed);
        history->spent=spent;history->executor_ok=ran;history->cancelled=cancelled;
        /* Keep failed joins and successful imports alike. This immutable
         * recorded history owns its ledgers until parent disposal; it holds
         * no borrowed task, frame, service or arena pointers. */
        history->next=owner->refill_history;owner->refill_history=history;
        history->imported=!metered;
        if (metered) owner->refill_pending=history;
        bool imported=prime_native_refill_join_pending(owner);
        refused=refused || !imported;
        prime_native_refill_receipt(owner,history);
    }
    if (cancelled || atomic_load_explicit(&refill.run.cancelled,memory_order_acquire))
        owner->completion=CETTA_EVAL_INCOMPLETE_CANCELLED;
    else if (refused) owner->driver.capacity_blocked=true;
    free(previous);free(items);*spent_out=spent;
    return true;
}
