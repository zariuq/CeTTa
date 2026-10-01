/* run_report.c — native run-report state machine.
 *
 * Correspondence (stated, not proved):
 *   Runner.decode        → terminal framing latch (exactly one terminal;
 *                          nothing after it; missing terminal is unsuccessful)
 *   Completion.scan      → per-event consume below (answers, worker sets,
 *                          action statuses); sticky failures; acknowledgement
 *                          invalidation; protocolOK for worker lifetimes
 *   completeB/commandSucceeded → observation_complete / finalization_ready
 *                        / exit participation
 *   TestPlan.consume     → pending-id ledger with per-verdict rejection
 *   Diagnostics.collect  → exact total + bounded details + omitted/overflow
 *   Runner.report        → RunReportOutcome and exit code 0/1/2
 *   Document aggregate   → per-query settle accounting; no pooling
 *
 * Native finitization notes: worker/test/diagnostic sets are bounded only by
 * the heap; allocation failure latches api_error rather than wrapping into
 * success.  Worker ids and test ids are caller-stable u64s.  Answer payloads
 * never enter this module.
 */
#include "run_report.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ state */

typedef enum {
    ACTION_PENDING = 0,
    ACTION_SUCCESS,
    ACTION_FAILURE,
} ActionStatus;

struct RunReport {
    RunReportDemand demand;
    uint64_t detail_budget;

    /* streaming observation accounts (Completion.Summary) */
    uint64_t answer_count;
    uint64_t *started_workers;
    size_t started_len, started_cap;
    uint64_t *pending_workers;
    size_t pending_len, pending_cap;
    ActionStatus output, cleanup;
    bool protocol_ok;

    /* test plan */
    bool plan_declared;
    bool plan_allow_empty;
    size_t plan_count;
    uint64_t test_count;
    uint64_t *pending_tests;
    size_t pending_tests_len, pending_tests_cap;
    bool tests_rejected;

    /* diagnostics */
    uint64_t fault_total;
    RunReportDiagDetail *details;
    size_t details_len, details_cap;

    /* framing */
    bool settled;
    bool framing_ok;
    RunReportStopKind stop;

    bool api_error;
};

struct RunReportDoc {
    struct DocRow {
        uint64_t query_id;
        RunReport *report;
    } *rows;
    size_t len, cap;
    RunReport *boundary;
    bool rejected;
};

static bool grow_u64(uint64_t **slots, size_t *cap, size_t need) {
    if (need <= *cap)
        return true;
    size_t next = *cap ? *cap : 8u;
    while (next < need) {
        if (next > SIZE_MAX / 2u)
            return false;
        next *= 2u;
    }
    if (next > SIZE_MAX / sizeof(uint64_t))
        return false;
    uint64_t *bigger = realloc(*slots, next * sizeof *bigger);
    if (!bigger)
        return false;
    *slots = bigger;
    *cap = next;
    return true;
}

static bool u64_has(const uint64_t *slots, size_t len, uint64_t value) {
    for (size_t i = 0; i < len; i++)
        if (slots[i] == value)
            return true;
    return false;
}

static void u64_erase(uint64_t *slots, size_t *len, uint64_t value) {
    for (size_t i = 0; i < *len; i++) {
        if (slots[i] == value) {
            slots[i] = slots[*len - 1];
            (*len)--;
            return;
        }
    }
}

/* Action transitions: sticky failure; acknowledgement resolves pending
 * success; new work invalidates an earlier acknowledgement. */
static ActionStatus action_acknowledge(ActionStatus status) {
    return status == ACTION_FAILURE ? ACTION_FAILURE : ACTION_SUCCESS;
}

static ActionStatus action_note_work(ActionStatus status) {
    return status == ACTION_FAILURE ? ACTION_FAILURE : ACTION_PENDING;
}

/* ------------------------------------------------------------------ init */

RunReport *run_report_begin(RunReportDemand demand, uint64_t detail_budget) {
    RunReport *report = calloc(1, sizeof *report);
    if (!report)
        return NULL;
    report->demand = demand;
    report->detail_budget = detail_budget;
    report->output = ACTION_PENDING;
    report->cleanup = ACTION_PENDING;
    report->protocol_ok = true;
    report->framing_ok = true;
    report->stop = RUN_REPORT_STOP_OBSERVED_COMPLETE;
    return report;
}

void run_report_free(RunReport *report) {
    if (!report)
        return;
    free(report->started_workers);
    free(report->pending_workers);
    free(report->pending_tests);
    free(report->details);
    free(report);
}

void run_report_outcome_details_free(RunReportOutcome *outcome) {
    if (!outcome)
        return;
    free((void *)outcome->diagnostics.details);
    outcome->diagnostics.details = NULL;
}

static bool feeds_allowed(RunReport *report) {
    if (!report || report->settled) {
        if (report)
            report->framing_ok = false;
        return false;
    }
    if (!report->framing_ok)
        return false;
    return true;
}

/* ------------------------------------------------------------------ feeds */

bool run_report_answer(RunReport *report) {
    if (!feeds_allowed(report))
        return false;
    if (report->answer_count == UINT64_MAX) {
        report->api_error = true;
        return false;
    }
    report->answer_count++;
    report->output = action_note_work(report->output);
    return true;
}

bool run_report_activity(RunReport *report, RunReportActivityKind kind,
                         uint64_t worker_id) {
    if (!feeds_allowed(report))
        return false;
    switch (kind) {
    case RUN_REPORT_ACTIVITY_ANSWER:
        return run_report_answer(report);
    case RUN_REPORT_ACTIVITY_WORKER_STARTED: {
        bool fresh = !u64_has(report->started_workers,
                              report->started_len, worker_id);
        if (!grow_u64(&report->started_workers, &report->started_cap,
                      report->started_len + 1u) ||
            !grow_u64(&report->pending_workers, &report->pending_cap,
                      report->pending_len + 1u)) {
            report->api_error = true;
            return false;
        }
        report->started_workers[report->started_len++] = worker_id;
        report->pending_workers[report->pending_len++] = worker_id;
        report->cleanup = action_note_work(report->cleanup);
        report->protocol_ok = report->protocol_ok && fresh;
        return true;
    }
    case RUN_REPORT_ACTIVITY_WORKER_SETTLED: {
        bool outstanding = u64_has(report->pending_workers,
                                   report->pending_len, worker_id);
        u64_erase(report->pending_workers, &report->pending_len, worker_id);
        report->cleanup = action_note_work(report->cleanup);
        report->protocol_ok = report->protocol_ok && outstanding;
        return true;
    }
    case RUN_REPORT_ACTIVITY_OUTPUT_COMPLETE:
        report->output = action_acknowledge(report->output);
        return true;
    case RUN_REPORT_ACTIVITY_OUTPUT_FAILED:
        report->output = ACTION_FAILURE;
        return true;
    case RUN_REPORT_ACTIVITY_CLEANUP_COMPLETE:
        report->cleanup = action_acknowledge(report->cleanup);
        return true;
    case RUN_REPORT_ACTIVITY_CLEANUP_FAILED:
        report->cleanup = ACTION_FAILURE;
        return true;
    }
    report->api_error = true;
    return false;
}


bool run_report_test_verdict(RunReport *r, uint64_t id, bool passed) {
    if (!feeds_allowed(r)) return false;
    if (r->test_count == UINT64_MAX) { r->api_error = true; return false; }
    r->test_count++;
    bool known = !r->plan_declared || u64_has(r->pending_tests, r->pending_tests_len, id);
    if (r->plan_declared && known) u64_erase(r->pending_tests, &r->pending_tests_len, id);
    r->tests_rejected |= !passed || !known;
    return true;
}

RunReportPlanStatus run_report_test_plan_declare(RunReport *r,
        const uint64_t *ids, size_t count, bool allow_empty) {
    if (!r || !feeds_allowed(r) || r->plan_declared || r->test_count) {
        if (r) r->api_error = true;
        return RUN_REPORT_PLAN_ALREADY_DECLARED;
    }
    if (count && !ids) { r->api_error = true; return RUN_REPORT_PLAN_CAPACITY_EXCEEDED; }
    for (size_t i = 0; i < count; i++)
        if (u64_has(ids, i, ids[i])) {
            r->tests_rejected = true;
            return RUN_REPORT_PLAN_DUPLICATE_DECLARED_ID;
        }
    if (!grow_u64(&r->pending_tests, &r->pending_tests_cap, count)) {
        r->api_error = true;
        return RUN_REPORT_PLAN_CAPACITY_EXCEEDED;
    }
    if (count) memcpy(r->pending_tests, ids, count * sizeof(*ids));
    r->plan_declared = true;
    r->plan_allow_empty = allow_empty;
    r->plan_count = r->pending_tests_len = count;
    return RUN_REPORT_PLAN_OK;
}

bool run_report_unhandled_fault(RunReport *r, uint64_t code,
                                uint64_t origin, uint64_t payload_id) {
    if (!feeds_allowed(r)) return false;
    if (r->fault_total == UINT64_MAX) { r->api_error = true; return false; }
    r->fault_total++;
    if (r->details_len >= r->detail_budget) return true;
    if (r->details_len == r->details_cap) {
        size_t cap = r->details_cap ? r->details_cap * 2u : 4u;
        if (cap < r->details_cap || cap > SIZE_MAX / sizeof(*r->details)) {
            r->api_error = true; return false;
        }
        if ((uint64_t)cap > r->detail_budget) cap = (size_t)r->detail_budget;
        RunReportDiagDetail *v = realloc(r->details, cap * sizeof(*v));
        if (!v) { r->api_error = true; return false; }
        r->details = v; r->details_cap = cap;
    }
    r->details[r->details_len++] = (RunReportDiagDetail){r->fault_total,code,origin,payload_id};
    return true;
}

bool run_report_terminal(RunReport *r, RunReportStopKind stop,
                         uint64_t code, uint64_t origin, uint64_t payload) {
    if (!feeds_allowed(r)) return false;
    if (stop > RUN_REPORT_STOP_FAULT) { r->api_error = true; return false; }
    bool ok = stop != RUN_REPORT_STOP_FAULT || run_report_unhandled_fault(r,code,origin,payload);
    r->stop = stop;
    r->settled = true;
    return ok;
}

/* Derive every projection from the current registers. In particular a feed
 * after finalize invalidates framing; no cached success can hide it. */
static RunReportOutcome outcome(const RunReport *r) {
    RunReportOutcome o = {0};
    o.framing_ok = r->settled && r->framing_ok;
    o.observation_complete = (r->stop == RUN_REPORT_STOP_OBSERVED_COMPLETE &&
        (r->demand.kind == RUN_REPORT_DEMAND_EXHAUSTIVE || r->answer_count <= r->demand.count)) ||
        (r->stop == RUN_REPORT_STOP_DEMAND_SATISFIED &&
         r->demand.kind == RUN_REPORT_DEMAND_PREFIX && r->answer_count == r->demand.count);
    o.observation_complete &= r->settled;
    o.tests_passed = !r->tests_rejected && (!r->plan_declared ||
        (!r->pending_tests_len && (r->plan_count || r->plan_allow_empty)));
    o.finalization_ready = !r->api_error && r->protocol_ok && !r->pending_len &&
        r->output == ACTION_SUCCESS && r->cleanup == ACTION_SUCCESS;
    o.diagnostics = (RunReportCollected){r->fault_total,r->details_len,
        r->fault_total-r->details_len,r->fault_total>r->details_len,r->details};
    o.worker_count_started = r->started_len;
    o.worker_count_pending = r->pending_len;
    o.answer_count = r->answer_count;
    o.test_count = r->test_count;
    o.exit_code = !o.framing_ok ? 1 : r->fault_total ? 2 :
        o.observation_complete && o.tests_passed && o.finalization_ready ? 0 : 1;
    return o;
}

bool run_report_finalize(RunReport *r, RunReportOutcome *out) {
    if (!r || !out) return false;
    *out = outcome(r);
    out->diagnostics.details = NULL;
    if (r->details_len) {
        RunReportDiagDetail *copy = malloc(r->details_len * sizeof(*copy));
        if (!copy) {
            r->api_error = true; out->finalization_ready = false;
            if (!out->exit_code) out->exit_code = 1;
            return false;
        }
        memcpy(copy, r->details, r->details_len * sizeof(*copy));
        out->diagnostics.details = copy;
    }
    return true;
}

RunReportDoc *run_report_doc_begin(void) {
    RunReportDoc *d = calloc(1,sizeof(*d));
    if (!d) return NULL;
    d->boundary = run_report_begin((RunReportDemand){RUN_REPORT_DEMAND_EXHAUSTIVE,0},0);
    if (!d->boundary) { free(d); return NULL; }
    return d;
}
void run_report_doc_free(RunReportDoc *d) {
    if (!d) return;
    for (size_t i=0;i<d->len;i++) run_report_free(d->rows[i].report);
    run_report_free(d->boundary); free(d->rows); free(d);
}
RunReport *run_report_doc_boundary(RunReportDoc *d) { return d ? d->boundary : NULL; }

bool run_report_doc_declare_query(RunReportDoc *d,uint64_t id) {
    if (!d || d->boundary->settled) { if (d) d->rejected=true; return false; }
    for (size_t i=0;i<d->len;i++)
        if (d->rows[i].query_id==id) { d->rejected=true; return false; }
    if (d->len==d->cap) {
        size_t cap=d->cap ? d->cap*2u : 8u;
        if (cap<d->cap || cap>SIZE_MAX/sizeof(*d->rows)) { d->rejected=true; return false; }
        struct DocRow *v=realloc(d->rows,cap*sizeof(*v));
        if (!v) { d->rejected=true; return false; }
        d->rows=v; d->cap=cap;
    }
    d->rows[d->len++]=(struct DocRow){.query_id=id};
    return true;
}
RunReport *run_report_doc_open_query(RunReportDoc *d,uint64_t id,
                                     RunReportDemand demand,uint64_t budget) {
    if (!d || d->boundary->settled) { if (d) d->rejected=true; return NULL; }
    for (size_t i=0;i<d->len;i++) if (d->rows[i].query_id==id) {
        if (d->rows[i].report) { d->rejected=true; return NULL; }
        d->rows[i].report=run_report_begin(demand,budget);
        if (!d->rows[i].report) d->rejected=true;
        return d->rows[i].report;
    }
    d->rejected=true;
    return NULL;
}

bool run_report_doc_verify(const RunReportDoc *d,RunReportDocStatus *out) {
    if (!d || !out) return false;
    *out=(RunReportDocStatus){.queries_declared=d->len};
    RunReportOutcome b=outcome(d->boundary);
    bool failed=d->rejected || b.exit_code;
    bool fault=b.exit_code==2;
    for (size_t i=0;i<d->len;i++) {
        const RunReport *r=d->rows[i].report;
        if (!r || !r->settled) { out->queries_unsettled++; failed=true; continue; }
        RunReportOutcome o=outcome(r);
        if (o.exit_code==2) { out->queries_settled_exit2++; fault=true; }
        else if (o.exit_code) { out->queries_settled_exit1++; failed=true; }
        else out->queries_settled_ok++;
    }
    out->exit_code=!b.framing_ok ? 1 : fault ? 2 : failed ? 1 : 0;
    return true;
}

bool run_report_doc_query_at(const RunReportDoc *d, size_t index,
                            uint64_t *id, RunReportOutcome *out) {
    if (!d || index >= d->len || !out) return false;
    if (id) *id = d->rows[index].query_id;
    if (!d->rows[index].report) {
        *out = (RunReportOutcome){.exit_code=1};
        return true;
    }
    return run_report_finalize(d->rows[index].report, out);
}
