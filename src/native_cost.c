#include "native_cost.h"
#include "identity_counter.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

_Thread_local CettaNativeCostScope *cetta_native_cost_current_scope;
#define current_scope cetta_native_cost_current_scope
_Thread_local CettaNativeCostScope *cetta_native_cost_current_account_scope;
#define account_scope cetta_native_cost_current_account_scope
static _Atomic uint64_t next_event_identity = 1u;
static _Atomic uint64_t next_ledger_identity = 1u;
static _Thread_local bool cost_notifying;

static uint64_t cost_charge_counts(CettaNativeCostKind, uint64_t);
static uint64_t cost_charge_events(CettaNativeCostKind, uint64_t);
static uint64_t cost_charge_receipts(CettaNativeCostKind, uint64_t);
static void cost_select_worker(void);
static _Thread_local uint64_t (*cost_charge_worker)(CettaNativeCostKind, uint64_t) =
    cost_charge_counts;

static const char *const names[CETTA_COST_KIND_COUNT] = {
    "quantum", "candidate-row", "index-visit", "match-attempt", "match-node",
    "heap-lookup", "heap-index-visit", "heap-update", "receipt-append",
    "binding-clone", "queue-insert", "queue-take", "queue-visit", "queue-move",
    "capture", "restore", "frame-allocate", "frame-retire", "publish",
    "coefficient-append", "coefficient-multiply", "cost-read", "analysis-step",
    "worker-dispatch", "worker-join", "commit-check", "equation-activation", "storage-probe",
    "coefficient-handle",
};

bool cetta_native_cost_event_source_init(CettaNativeCostEventSource *source) {
    if (!source) return false;
    *source=(CettaNativeCostEventSource){
        .origin=cetta_identity_try_take(&next_ledger_identity)};
    source->exhausted=!source->origin;
    return !source->exhausted;
}

bool cetta_native_cost_event_source_issue(CettaNativeCostEventSource *source,
    CettaNativeCostKind kind,uint64_t units,CettaNativeCostEvent *event) {
    if (!source || !event || source->exhausted || !source->origin || !units ||
        (unsigned)kind>=CETTA_COST_KIND_COUNT) return false;
    if (source->next_occurrence==UINT64_MAX) {
        source->exhausted=true;return false;
    }
    uint64_t identity=cetta_identity_try_take(&next_event_identity);
    if (!identity) { source->exhausted=true;return false; }
    source->next_occurrence++;
    *event=(CettaNativeCostEvent){.identity=identity,.origin=source->origin,
        .occurrence=source->next_occurrence,.units=units,.repetitions=1u,
        .kind=kind};
    return true;
}

void cetta_native_cost_init(CettaNativeCostLedger *ledger) {
    if (ledger) {
        memset(ledger, 0, sizeof(*ledger));
        ledger->native_enabled = true;
        ledger->identity = cetta_identity_try_take(&next_ledger_identity);
        ledger->total.trace_incomplete = !ledger->identity;
    }
}

void cetta_native_cost_init_compact(CettaNativeCostLedger *ledger) {
    cetta_native_cost_init(ledger);
    if (ledger) ledger->compact = true;
}

void cetta_native_cost_init_equations(CettaNativeCostLedger *ledger) {
    cetta_native_cost_init_compact(ledger);
    if (ledger) {
        ledger->native_enabled = false;
        ledger->equations.enabled = true;
    }
}

bool cetta_native_cost_enable_equations(CettaNativeCostLedger *ledger) {
    if (!ledger || cost_notifying) return false;
    if (!ledger->equations.enabled && (ledger->used || ledger->total.work)) return false;
    for (CettaNativeCostScope *scope = account_scope; scope; scope = scope->account_previous)
        if (scope->ledger == ledger) return false;
    ledger->equations.enabled = true;
    return true;
}

bool cetta_native_cost_equations_requested(void) {
    return account_scope && account_scope->equations_requested;
}

bool cetta_native_cost_forget_events(CettaNativeCostLedger *ledger) {
    if (!ledger || cost_notifying) return false;
    for (CettaNativeCostScope *scope = account_scope; scope; scope = scope->account_previous)
        if (scope->ledger == ledger) return false;
    free(ledger->events);
    ledger->events = NULL;
    ledger->length = ledger->capacity = 0u;
    ledger->compact = true;
    return true;
}

void cetta_native_cost_free(CettaNativeCostLedger *ledger) {
    if (!ledger)
        return;
    free(ledger->events);
    memset(ledger, 0, sizeof(*ledger));
}

const char *cetta_native_cost_name(CettaNativeCostKind kind) {
    return (unsigned)kind < CETTA_COST_KIND_COUNT ? names[kind] : "invalid";
}

static uint64_t add_lower_bound(uint64_t left, uint64_t right, bool *overflow) {
    if (right > UINT64_MAX - left) {
        *overflow = true;
        return UINT64_MAX;
    }
    return left + right;
}

void cetta_native_cost_equations_admitted(uint64_t units) {
    for (CettaNativeCostScope *scope = account_scope; scope; scope = scope->account_previous) {
        if (!scope->ledger || !scope->ledger->equations.enabled) continue;
        bool duplicate = false;
        for (CettaNativeCostScope *inner = account_scope; inner != scope;
             inner = inner->account_previous)
            duplicate |= inner->ledger == scope->ledger;
        if (duplicate) continue;
        CettaEquationCost *account = &scope->ledger->equations;
        account->work = add_lower_bound(account->work, units, &account->work_overflow);
        account->span = add_lower_bound(account->span, units, &account->span_overflow);
    }
}

static inline void append_account(CettaNativeCostLedger *ledger,
        CettaNativeCostKind kind, uint64_t units) {
    ledger->total.counts[kind] = add_lower_bound(
        ledger->total.counts[kind], units, &ledger->total.overflow);
    ledger->total.work = add_lower_bound(
        ledger->total.work, units, &ledger->total.overflow);
    ledger->total.span = add_lower_bound(
        ledger->total.span, units, &ledger->total.overflow);
}

static void append_event(CettaNativeCostLedger *ledger,
        CettaNativeCostKind kind, uint64_t units, uint64_t occurrence,
        uint64_t identity, uint64_t origin) {
    if (!ledger || (unsigned)kind >= CETTA_COST_KIND_COUNT || units == 0u)
        return;
    append_account(ledger, kind, units);
    if (ledger->compact)
        return;
    if (!identity)
        ledger->total.trace_incomplete = true;
    if (ledger->total.trace_incomplete)
        return;
    if (ledger->length) {
        CettaNativeCostEvent *last = &ledger->events[ledger->length - 1u];
        if (last->kind == kind && last->origin == origin &&
            last->occurrence == occurrence && last->units == units &&
            last->repetitions < UINT64_MAX &&
            last->identity <= UINT64_MAX - last->repetitions &&
            last->identity + last->repetitions == identity) {
            last->repetitions++;
            return;
        }
    }
    if (ledger->length == ledger->capacity) {
        size_t capacity = ledger->capacity ? ledger->capacity * 2u : 32u;
        if (capacity < ledger->capacity ||
            capacity > SIZE_MAX / sizeof(*ledger->events)) {
            ledger->total.trace_incomplete = true;
            return;
        }
        void *events = realloc(ledger->events, capacity * sizeof(*ledger->events));
        if (!events) {
            ledger->total.trace_incomplete = true;
            return;
        }
        ledger->events = events;
        ledger->capacity = capacity;
    }
    ledger->events[ledger->length++] = (CettaNativeCostEvent){
        .identity = identity, .origin = origin,
        .occurrence = occurrence, .units = units, .repetitions = 1u, .kind = kind,
    };
}


static _Thread_local CettaNativeCostObserver *cost_observer;
static _Thread_local bool equations_selected;
static bool cost_observer_suspended_contains(const CettaNativeCostObserver *);
extern void cetta_native_cost_observation_policy_changed(void) __attribute__((weak));

/* Scope and observer changes choose an operational worker. The numerical
 * worker does not inspect trace policy, event identities or observers. */
static void cost_select_worker(void) {
    cost_charge_worker = cost_observer ? cost_charge_receipts
        : current_scope && current_scope->events_requested ? cost_charge_events
        : cost_charge_counts;
    bool equations = cetta_native_cost_equations_requested();
    if (equations != equations_selected) {
        equations_selected = equations;
        if (cetta_native_cost_observation_policy_changed)
            cetta_native_cost_observation_policy_changed();
    }
}

bool cetta_native_cost_observer_active(void) { return cost_observer != NULL; }

bool cetta_native_cost_events_requested(void) {
    return cost_observer || (current_scope && current_scope->events_requested);
}

bool cetta_native_cost_observer_enter(CettaNativeCostObserver *observer,
        CettaNativeCostObserverFn observe, void *context) {
    if (!observer || !observe || cost_notifying ||
        cost_observer_suspended_contains(observer)) return false;
    for (CettaNativeCostObserver *p = cost_observer; p; p = p->previous)
        if (p == observer) return false;
    *observer = (CettaNativeCostObserver){cost_observer, observe, context};
    cost_observer = observer;
    cost_select_worker();
    return true;
}

void cetta_native_cost_observer_leave(CettaNativeCostObserver *observer) {
    assert(observer && cost_observer == observer && !cost_notifying);
    cost_observer = observer->previous;
    cost_select_worker();
}

static _Thread_local CettaNativeCostObserverSuspension *cost_suspension;

static bool cost_observer_suspended_contains(const CettaNativeCostObserver *observer) {
    for (const CettaNativeCostObserverSuspension *scope=cost_suspension;
         scope; scope=scope->previous)
        for (const CettaNativeCostObserver *p=scope->observers; p; p=p->previous)
            if (p==observer) return true;
    return false;
}

bool cetta_native_cost_observer_suspend(CettaNativeCostObserverSuspension *scope) {
    if (!scope || cost_notifying) return false;
    for (CettaNativeCostObserverSuspension *p=cost_suspension; p; p=p->previous)
        if (p==scope) return false;
    *scope=(CettaNativeCostObserverSuspension){.previous=cost_suspension,
        .observers=cost_observer,.active=true};
    cost_suspension=scope;
    cost_observer=NULL;
    cost_select_worker();
    return true;
}

void cetta_native_cost_observer_restore(CettaNativeCostObserverSuspension *scope) {
    assert(scope && scope->active && cost_suspension==scope &&
        !cost_observer && !cost_notifying);
    cost_observer=scope->observers;
    cost_suspension=scope->previous;
    *scope=(CettaNativeCostObserverSuspension){0};
    cost_select_worker();
}


static void cost_observe(const CettaNativeCostChargeView *view) {
    assert(!cost_notifying);
    cost_notifying = true;
    for (CettaNativeCostObserver *p = cost_observer; p; p = p->previous)
        p->observe(p->context, view);
    cost_notifying = false;
}

static void cost_observe_destination(CettaNativeCostChargeView *view,
        CettaNativeCostLedger *ledger, bool duplicate) {
    view->point = CETTA_COST_OBSERVE_DESTINATION;
    view->ledger = ledger;
    view->duplicate = duplicate;
    view->before = ledger ? ledger->total : (CettaNativeCostSnapshot){0};
    if (!duplicate)
        append_event(ledger, view->event.kind, view->event.units,
            view->event.occurrence, view->event.identity, view->event.origin);
    view->after = ledger ? ledger->total : (CettaNativeCostSnapshot){0};
    view->retained = ledger && !duplicate && !ledger->compact && !ledger->total.trace_incomplete;
    cost_observe(view);
}

static void cost_observe_end(CettaNativeCostChargeView *view) {
    view->point = CETTA_COST_OBSERVE_END;
    view->scope = NULL;
    view->ledger = NULL;
    view->duplicate = view->retained = false;
    view->before = view->after = (CettaNativeCostSnapshot){0};
    cost_observe(view);
}

void cetta_native_cost_append(CettaNativeCostLedger *ledger,
        CettaNativeCostKind kind, uint64_t units, uint64_t occurrence) {
    if (!ledger || !ledger->native_enabled || !units || (unsigned)kind >= CETTA_COST_KIND_COUNT)
        return;
    uint64_t identity = !ledger->compact || cost_observer
        ? cetta_identity_try_take(&next_event_identity) : 0u;
    if (!cost_observer) {
        append_event(ledger, kind, units, occurrence, identity, ledger->identity);
        return;
    }
    CettaNativeCostChargeView view = {
        .point=CETTA_COST_OBSERVE_BEGIN,
        .event={.identity=identity,.origin=ledger->identity,.occurrence=occurrence,
            .units=units,.repetitions=1u,.kind=kind},
    };
    cost_observe(&view);
    cost_observe_destination(&view,ledger,false);
    view.ordinal=1u;
    cost_observe_end(&view);
}

void cetta_native_cost_scope_enter(CettaNativeCostScope *scope,
        CettaNativeCostLedger *ledger, uint64_t occurrence) {
    *scope = (CettaNativeCostScope){
        .previous = current_scope, .account_previous = account_scope,
        .native_head = !ledger || ledger->native_enabled ? scope : current_scope,
        .ledger = ledger, .occurrence = occurrence,
        /* A NULL destination is an explicit legacy event-only scope. */
        .events_requested = (!ledger || (ledger->native_enabled && !ledger->compact)) ||
            (current_scope && current_scope->events_requested),
        .equations_requested = (ledger && ledger->equations.enabled) ||
            (account_scope && account_scope->equations_requested),
    };
    account_scope = scope;
    current_scope = scope->native_head;
    if (ledger) ledger->used = true;
    cost_select_worker();
}

void cetta_native_cost_scope_enter_source(CettaNativeCostScope *scope,
        CettaNativeCostLedger *ledger, const uint64_t *occurrence) {
    assert(occurrence);
    cetta_native_cost_scope_enter(scope, ledger, 0u);
    scope->occurrence_source = occurrence;
}

void cetta_native_cost_scope_leave(CettaNativeCostScope *scope) {
    assert(account_scope == scope);
    scope->occurrence = cetta_native_cost_scope_occurrence(scope);
    scope->occurrence_source = NULL;
    account_scope = scope->account_previous;
    current_scope = scope->previous;
    cost_select_worker();
}

CettaNativeCostScope *cetta_native_cost_detach_begin(void) {
    CettaNativeCostScope *previous = account_scope;
    account_scope = current_scope = NULL;
    cost_select_worker();
    return previous;
}

void cetta_native_cost_detach_end(CettaNativeCostScope *previous) {
    assert(!account_scope && !current_scope);
    account_scope = previous;
    current_scope = previous ? previous->native_head : NULL;
    cost_select_worker();
}

static uint64_t cost_charge_counts(CettaNativeCostKind kind, uint64_t units) {
    for (CettaNativeCostScope *scope = current_scope; scope; scope = scope->previous) {
        bool duplicate = false;
        for (CettaNativeCostScope *inner = current_scope; inner != scope;
             inner = inner->previous)
            duplicate = duplicate || inner->ledger == scope->ledger;
        if (!duplicate)
            append_account(scope->ledger, kind, units);
    }
    return 0u;
}

static uint64_t cost_charge_events(CettaNativeCostKind kind, uint64_t units) {
    uint64_t identity = cetta_identity_try_take(&next_event_identity);
    uint64_t origin = current_scope->ledger ? current_scope->ledger->identity : 0u;
    uint64_t occurrence = cetta_native_cost_scope_occurrence(current_scope);
    for (CettaNativeCostScope *scope = current_scope; scope; scope = scope->previous) {
        bool duplicate = false;
        for (CettaNativeCostScope *inner = current_scope; inner != scope;
             inner = inner->previous)
            duplicate = duplicate || inner->ledger == scope->ledger;
        if (!duplicate)
            append_event(scope->ledger, kind, units, occurrence, identity, origin);
    }
    return identity;
}

static uint64_t cost_charge_receipts(CettaNativeCostKind kind, uint64_t units) {
    uint64_t identity = cetta_identity_try_take(&next_event_identity);
    uint64_t origin = current_scope->ledger ? current_scope->ledger->identity : 0u;
    uint64_t occurrence = cetta_native_cost_scope_occurrence(current_scope);
    CettaNativeCostChargeView view = {
        .point=CETTA_COST_OBSERVE_BEGIN,
        .event={.identity=identity,.origin=origin,.occurrence=occurrence,
            .units=units,.repetitions=1u,.kind=kind},
        .chain=current_scope,.scoped=true,
    };
    cost_observe(&view);
    for (CettaNativeCostScope *scope = current_scope; scope; scope = scope->previous) {
        bool duplicate = false;
        for (CettaNativeCostScope *inner = current_scope; inner != scope;
             inner = inner->previous)
            duplicate = duplicate || inner->ledger == scope->ledger;
        view.scope=scope;
        cost_observe_destination(&view,scope->ledger,duplicate);
        view.ordinal++;
    }
    cost_observe_end(&view);
    return identity;
}

uint64_t cetta_native_cost_charge_observed(CettaNativeCostKind kind, uint64_t units) {
    if (!current_scope || !units || (unsigned)kind >= CETTA_COST_KIND_COUNT)
        return 0u;
    return cost_charge_worker(kind, units);
}

/* Import numerical accounts directly when chronology is not available or
 * not requested. Scheduling supplies span separately at the join boundary. */
static void cost_import_account(CettaNativeCostLedger *target,
        const CettaNativeCostLedger *source) {
    if (target->equations.enabled) {
        target->equations.work = add_lower_bound(target->equations.work,
            source->equations.work, &target->equations.work_overflow);
        target->equations.work_overflow |= source->equations.work_overflow;
        target->equations.span_overflow |= source->equations.span_overflow;
        target->equations.incomplete |= !source->equations.enabled || source->equations.incomplete;
        target->equations.parallel |= source->equations.parallel;
    }
    if (!target->native_enabled) return;
    if (!source->native_enabled) target->total.trace_incomplete = true;
    if (target->compact || source->compact || source->total.trace_incomplete) {
        for (unsigned k = 0u; k < CETTA_COST_KIND_COUNT; k++)
            target->total.counts[k] = add_lower_bound(target->total.counts[k],
                source->total.counts[k], &target->total.overflow);
        target->total.work = add_lower_bound(target->total.work,
            source->total.work, &target->total.overflow);
        if (!target->compact)
            target->total.trace_incomplete = true;
    } else {
        for (size_t j = 0u; j < source->length; j++) {
            const CettaNativeCostEvent *event = &source->events[j];
            for (uint64_t repetition = 0u; repetition < event->repetitions; repetition++)
                append_event(target, event->kind, event->units, event->occurrence,
                    event->identity + repetition, event->origin);
        }
    }
    target->total.overflow = target->total.overflow || source->total.overflow;
    target->total.parallel = target->total.parallel || source->total.parallel;
}

bool cetta_native_cost_join_scheduled(CettaNativeCostLedger *const *children,
        const CettaNativeCostPlacement *placement, size_t count) {
    if (count && !children) return false;
    for (size_t i = 0u; i < count; i++) {
        if (!children[i] || children[i]->imported) return false;
        for (size_t j = 0u; j < i; j++)
            if (children[j] == children[i]) return false;
        for (CettaNativeCostScope *scope = account_scope; scope; scope = scope->account_previous)
            if (scope->ledger == children[i]) return false;
    }
    for (CettaNativeCostScope *scope = account_scope; scope; scope = scope->account_previous) {
        bool duplicate = false;
        for (CettaNativeCostScope *inner = account_scope; inner != scope; inner = inner->account_previous)
            duplicate = duplicate || inner->ledger == scope->ledger;
        CettaNativeCostLedger *target = scope->ledger;
        if (duplicate || !target) continue;
        uint64_t before_span = target->total.span, schedule_span = 0u;
        uint64_t before_equations = target->equations.span, equation_span = 0u;
        bool parallel = false;
        for (size_t i = 0u; i < count; i++) {
            uint64_t phase = placement ? placement[i].phase : 0u;
            bool seen_phase = false;
            for (size_t j = 0u; j < i; j++)
                seen_phase |= (placement ? placement[j].phase : 0u) == phase;
            if (seen_phase) continue;
            uint64_t maximum = 0u;
            uint64_t equation_maximum = 0u;
            size_t workers = 0u;
            for (size_t j = 0u; j < count; j++) {
                if ((placement ? placement[j].phase : 0u) != phase) continue;
                uint64_t worker = placement ? placement[j].worker : j;
                bool seen_worker = false;
                for (size_t k = 0u; k < j; k++)
                    seen_worker |= (placement ? placement[k].phase : 0u) == phase &&
                        (placement ? placement[k].worker : k) == worker;
                if (seen_worker) continue;
                workers++;
                uint64_t load = 0u;
                uint64_t equation_load = 0u;
                for (size_t k = j; k < count; k++)
                    if ((placement ? placement[k].phase : 0u) == phase &&
                        (placement ? placement[k].worker : k) == worker) {
                        if (target->native_enabled)
                            load = add_lower_bound(load, children[k]->total.span, &target->total.overflow);
                        if (target->equations.enabled)
                            equation_load = add_lower_bound(equation_load, children[k]->equations.span,
                                &target->equations.span_overflow);
                    }
                if (load > maximum) maximum = load;
                if (equation_load > equation_maximum) equation_maximum = equation_load;
            }
            if (target->native_enabled)
                schedule_span = add_lower_bound(schedule_span, maximum, &target->total.overflow);
            if (target->equations.enabled)
                equation_span = add_lower_bound(equation_span, equation_maximum, &target->equations.span_overflow);
            parallel |= workers > 1u;
        }
        for (size_t i = 0u; i < count; i++) {
            CettaNativeCostLedger *source = children[i];
            cost_import_account(target, source);
        }
        if (target->native_enabled) {
            target->total.span = add_lower_bound(before_span, schedule_span, &target->total.overflow);
            target->total.parallel |= parallel;
        }
        if (target->equations.enabled) {
            target->equations.span = add_lower_bound(before_equations, equation_span,
                &target->equations.span_overflow);
            target->equations.parallel |= parallel;
        }
    }
    for (size_t i = 0u; i < count; i++) children[i]->imported = true;
    return true;
}

bool cetta_native_cost_join_segments(const CettaNativeCostSegment *segments,
        size_t count, uint64_t workers) {
    if (!count) return true;
    if (!segments || !workers || count > SIZE_MAX / sizeof(size_t) ||
        count > SIZE_MAX / sizeof(uint64_t)) return false;
    size_t *order = malloc(count * sizeof(*order));
    bool native = cetta_native_cost_active();
    uint64_t *finish = native ? calloc(count, sizeof(*finish)) : NULL;
    bool equations = cetta_native_cost_equations_requested();
    uint64_t *equation_finish = equations ? calloc(count, sizeof(*equation_finish)) : NULL;
    if (!order || (native && !finish) || (equations && !equation_finish)) {
        free(order); free(finish); free(equation_finish); return false;
    }
    for (size_t i = 0u; i < count; i++) order[i] = SIZE_MAX;
    bool valid = true, overflow = false, parallel = false;
    uint64_t span = 0u;
    uint64_t equation_span = 0u;
    bool equation_overflow = false;
    for (size_t i = 0u; valid && i < count; i++) {
        const CettaNativeCostSegment *node = &segments[i];
        CettaNativeCostLedger *ledger = node->ledger;
        valid = ledger && ledger->identity && !ledger->imported &&
            node->occurrence && node->order && node->order <= count &&
            node->worker < workers;
        if (!valid) break;
        size_t position = (size_t)(node->order - 1u);
        valid = order[position] == SIZE_MAX;
        order[position] = i;
        for (size_t j = 0u; j < i; j++) {
            valid = valid && segments[j].ledger != ledger &&
                segments[j].ledger->identity != ledger->identity;
            parallel = parallel || segments[j].worker != node->worker;
        }
        for (CettaNativeCostScope *scope = account_scope; scope; scope = scope->account_previous)
            valid = valid && scope->ledger != ledger;
    }
    for (size_t position = 0u; valid && position < count; position++) {
        if (order[position] == SIZE_MAX) { valid = false; break; }
        const CettaNativeCostSegment *node = &segments[order[position]];
        uint64_t owner_predecessor = 0u, worker_predecessor = 0u;
        uint64_t owner_finish = 0u, worker_finish = 0u;
        uint64_t equation_owner_finish = 0u, equation_worker_finish = 0u;
        for (size_t prior = 0u; prior < position; prior++) {
            const CettaNativeCostSegment *previous = &segments[order[prior]];
            if (previous->occurrence == node->occurrence) {
                owner_predecessor = previous->ledger->identity;
                if (native) owner_finish = finish[prior];
                if (equations) equation_owner_finish = equation_finish[prior];
            }
            if (previous->worker == node->worker) {
                worker_predecessor = previous->ledger->identity;
                if (native) worker_finish = finish[prior];
                if (equations) equation_worker_finish = equation_finish[prior];
            }
        }
        if (node->owner_predecessor != owner_predecessor ||
            node->worker_predecessor != worker_predecessor) { valid = false; break; }
        if (native) {
            uint64_t preceding = owner_finish > worker_finish ? owner_finish : worker_finish;
            finish[position] = add_lower_bound(preceding, node->ledger->total.span, &overflow);
            if (finish[position] > span) span = finish[position];
        }
        if (equations) {
            uint64_t preceding_equations = equation_owner_finish > equation_worker_finish
                ? equation_owner_finish : equation_worker_finish;
            equation_finish[position] = add_lower_bound(preceding_equations,
                node->ledger->equations.span, &equation_overflow);
            if (equation_finish[position] > equation_span)
                equation_span = equation_finish[position];
        }
    }
    free(order); free(finish); free(equation_finish);
    if (!valid) return false;
    /* Every input and edge is checked before the first destination changes.
     * Trace allocation failure retains numerical work and marks incomplete,
     * exactly as the existing scheduled join does. */
    for (CettaNativeCostScope *scope = account_scope; scope; scope = scope->account_previous) {
        bool duplicate = false;
        for (CettaNativeCostScope *inner = account_scope; inner != scope; inner = inner->account_previous)
            duplicate = duplicate || inner->ledger == scope->ledger;
        CettaNativeCostLedger *target = scope->ledger;
        if (duplicate || !target) continue;
        uint64_t before_span = target->total.span;
        uint64_t before_equations = target->equations.span;
        for (size_t i = 0u; i < count; i++) {
            CettaNativeCostLedger *source = segments[i].ledger;
            cost_import_account(target, source);
        }
        if (target->native_enabled) {
            target->total.overflow |= overflow;
            target->total.span = add_lower_bound(before_span, span, &target->total.overflow);
            target->total.parallel |= parallel;
        }
        if (target->equations.enabled) {
            target->equations.span_overflow |= equation_overflow;
            target->equations.span = add_lower_bound(before_equations, equation_span,
                &target->equations.span_overflow);
            target->equations.parallel |= parallel;
        }
    }
    for (size_t i = 0u; i < count; i++) segments[i].ledger->imported = true;
    return true;
}

bool cetta_native_cost_join(CettaNativeCostLedger *const *children, size_t count) {
    return cetta_native_cost_join_scheduled(children, NULL, count);
}

void cetta_native_cost_occurrence(uint64_t occurrence) {
    if (current_scope) {
        current_scope->occurrence_source = NULL;
        current_scope->occurrence = occurrence;
    }
}

CettaNativeCostSnapshot cetta_native_cost_read(CettaNativeCostLedger *ledger,
        uint64_t occurrence, CettaNativeCostEvent *charge) {
    CettaNativeCostSnapshot snapshot = ledger ? ledger->total
        : (CettaNativeCostSnapshot){.trace_incomplete = true};
    CettaNativeCostScope scope;
    cetta_native_cost_scope_enter(&scope, ledger, occurrence);
    uint64_t identity = cetta_native_cost_charge(CETTA_COST_READ, 1u);
    cetta_native_cost_scope_leave(&scope);
    if (charge)
        *charge = (CettaNativeCostEvent){
            .identity = identity, .origin = ledger ? ledger->identity : 0u,
            .occurrence = occurrence, .units = 1u, .repetitions = 1u,
            .kind = CETTA_COST_READ,
        };
    return snapshot;
}

static bool cost_event_equal(const CettaNativeCostEvent *a,
                             const CettaNativeCostEvent *b) {
    return a->identity==b->identity && a->origin==b->origin &&
        a->occurrence==b->occurrence && a->units==b->units &&
        a->repetitions==b->repetitions && a->kind==b->kind;
}

static bool cost_snapshot_equal(const CettaNativeCostSnapshot *a,
                                const CettaNativeCostSnapshot *b) {
    for (size_t i=0u;i<CETTA_COST_KIND_COUNT;i++)
        if (a->counts[i]!=b->counts[i]) return false;
    return a->work==b->work && a->span==b->span && a->parallel==b->parallel &&
        a->overflow==b->overflow && a->trace_incomplete==b->trace_incomplete;
}

bool cetta_native_cost_receipts_init(CettaNativeCostReceipts *out,
                                    size_t commands,size_t destinations) {
    if (!out || commands>SIZE_MAX/sizeof(*out->commands) ||
        destinations>SIZE_MAX/sizeof(*out->destinations)) return false;
    CettaNativeCostReceipts next={0};
    next.commands=commands?calloc(commands,sizeof(*next.commands)):NULL;
    next.destinations=destinations?calloc(destinations,sizeof(*next.destinations)):NULL;
    if ((commands&&!next.commands)||(destinations&&!next.destinations)) {
        free(next.commands);free(next.destinations);return false;
    }
    next.command_capacity=commands;next.destination_capacity=destinations;
    next.active_command=SIZE_MAX;
    *out=next;
    return true;
}

void cetta_native_cost_receipts_free(CettaNativeCostReceipts *out) {
    if (!out) return;
    free(out->commands);free(out->destinations);
    *out=(CettaNativeCostReceipts){0};
}

void cetta_native_cost_receipts_observe(void *raw,const CettaNativeCostChargeView *v) {
    CettaNativeCostReceipts *out=raw;
    if (!out || !v) return;
    if (v->point==CETTA_COST_OBSERVE_BEGIN) {
        if (out->open) out->malformed=true;
        out->open=true;out->active_event=v->event;out->active_scoped=v->scoped;
        out->active_destinations=0u;out->active_command=SIZE_MAX;
        if (!v->event.units || v->event.repetitions!=1u ||
            (unsigned)v->event.kind>=CETTA_COST_KIND_COUNT)
            out->malformed=true;
        if (out->command_count==out->command_capacity) out->truncated=true;
        else {
            out->active_command=out->command_count++;
            out->commands[out->active_command]=(CettaNativeCostCommandReceipt){
                .event=v->event,.first_destination=out->destination_count,
                .scoped=v->scoped};
        }
        return;
    }
    if (!out->open || !cost_event_equal(&out->active_event,&v->event) ||
        out->active_scoped!=v->scoped) {
        out->malformed=true;return;
    }
    if (v->point==CETTA_COST_OBSERVE_DESTINATION) {
        if (v->ordinal!=out->active_destinations ||
            (v->scoped ? v->scope==NULL : v->scope!=NULL)) out->malformed=true;
        if (out->active_destinations==SIZE_MAX) out->malformed=true;
        else out->active_destinations++;
        if (out->active_command==SIZE_MAX) return;
        if (out->destination_count==out->destination_capacity) {
            out->truncated=true;return;
        }
        CettaNativeCostDestinationReceipt *d=&out->destinations[out->destination_count++];
        *d=(CettaNativeCostDestinationReceipt){
            .ledger_identity=v->ledger?v->ledger->identity:0u,
            .scope_occurrence=v->scoped&&v->scope
                ?cetta_native_cost_scope_occurrence(v->scope):v->event.occurrence,
            .present=v->ledger!=NULL,.duplicate=v->duplicate,.retained=v->retained,
            .before=v->before,.after=v->after};
        out->commands[out->active_command].destination_count++;
        return;
    }
    if (v->point!=CETTA_COST_OBSERVE_END || v->ordinal!=out->active_destinations ||
        (!v->scoped && out->active_destinations!=1u)) out->malformed=true;
    out->open=false;out->active_command=SIZE_MAX;
}

bool cetta_native_cost_receipts_complete(const CettaNativeCostReceipts *out) {
    if (!out || out->open || out->truncated || out->malformed ||
        out->command_count>out->command_capacity ||
        out->destination_count>out->destination_capacity ||
        (out->command_count&&!out->commands) ||
        (out->destination_count&&!out->destinations)) return false;
    size_t at=0u;
    for (size_t i=0u;i<out->command_count;i++) {
        const CettaNativeCostCommandReceipt *c=&out->commands[i];
        if (c->first_destination!=at || c->destination_count>out->destination_count-at ||
            (!c->scoped&&c->destination_count!=1u)) return false;
        at+=c->destination_count;
    }
    return at==out->destination_count;
}

static bool cost_mapped_id(CettaNativeCostIdentityMap map,void *ctx,
    CettaNativeCostIdentityRole role,uint64_t a,uint64_t b) {
    if (!a || !b) return a==b;
    uint64_t got=0u;
    return map && map(ctx,role,a,&got) && got==b;
}

static bool cost_receipts_related(const CettaNativeCostReceipts *s,
    const CettaNativeCostReceipts *d,CettaNativeCostIdentityMap map,void *ctx,
    size_t *mismatch,bool prefix) {
    if (mismatch) *mismatch=0u;
    if (!cetta_native_cost_receipts_complete(s)||!cetta_native_cost_receipts_complete(d)||
        (prefix ? s->command_count<d->command_count : s->command_count!=d->command_count))
        return false;
    size_t end=d->command_count ? s->commands[d->command_count-1u].first_destination+
        s->commands[d->command_count-1u].destination_count : 0u;
    if(end!=d->destination_count || (!prefix&&s->destination_count!=d->destination_count))
        return false;
    for (size_t i=0u;i<d->command_count;i++) {
        if (mismatch) *mismatch=i;
        const CettaNativeCostCommandReceipt *a=&s->commands[i],*b=&d->commands[i];
        if (a->scoped!=b->scoped || a->destination_count!=b->destination_count ||
            a->event.kind!=b->event.kind || a->event.units!=b->event.units ||
            a->event.repetitions!=1u || b->event.repetitions!=1u ||
            !cost_mapped_id(map,ctx,CETTA_COST_ID_EVENT,a->event.identity,b->event.identity)||
            !cost_mapped_id(map,ctx,CETTA_COST_ID_ORIGIN,a->event.origin,b->event.origin)||
            !cost_mapped_id(map,ctx,CETTA_COST_ID_OCCURRENCE,a->event.occurrence,b->event.occurrence))
            return false;
        for (size_t j=0u;j<a->destination_count;j++) {
            const CettaNativeCostDestinationReceipt *x=&s->destinations[a->first_destination+j];
            const CettaNativeCostDestinationReceipt *y=&d->destinations[b->first_destination+j];
            if (x->present!=y->present || x->duplicate!=y->duplicate || x->retained!=y->retained ||
                !cost_mapped_id(map,ctx,CETTA_COST_ID_DESTINATION,x->ledger_identity,y->ledger_identity)||
                !cost_mapped_id(map,ctx,CETTA_COST_ID_OCCURRENCE,x->scope_occurrence,y->scope_occurrence)||
                !cost_snapshot_equal(&x->before,&y->before)||!cost_snapshot_equal(&x->after,&y->after))
                return false;
        }
    }
    if (mismatch) *mismatch=d->command_count;
    return true;
}

bool cetta_native_cost_receipts_related(const CettaNativeCostReceipts *s,
    const CettaNativeCostReceipts *d,CettaNativeCostIdentityMap map,void *ctx,
    size_t *mismatch) {
    return cost_receipts_related(s,d,map,ctx,mismatch,false);
}
bool cetta_native_cost_receipts_prefix_related(const CettaNativeCostReceipts *s,
    const CettaNativeCostReceipts *d,CettaNativeCostIdentityMap map,void *ctx,
    size_t *mismatch) {
    return cost_receipts_related(s,d,map,ctx,mismatch,true);
}

void cetta_native_cost_checkpoint_issuance(CettaCheckpointIssuance *out) {
    if(!out)return;
    out->next[CETTA_CHECKPOINT_COST_EVENT]=atomic_load_explicit(&next_event_identity,memory_order_relaxed);
    out->next[CETTA_CHECKPOINT_COST_LEDGER]=atomic_load_explicit(&next_ledger_identity,memory_order_relaxed);
}
