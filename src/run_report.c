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
    RunReportOutcome settled_outcome;
};

struct RunReportDoc {
    struct DocRow {
        uint64_t query_id;
        RunReport *report;
        bool return_settled;
        uint32_t settled_exit;
    } *rows;
    size_t len, cap;
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
    uint64_t *bigger = realloc(*slots, next * sizeof *bigger);
    if (!bigger)
        return false;
    *slots = bigger;
    *cap = next;
    return true;
}

static void u64_insert(uint64_t **slots, size_t *len, size_t *cap,
                       uint64_t value) {
    (*slots)[(*len)++] = value;
    (void)0;
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
    free(report->settled_outcome.diagnostics.details);
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
        report->cleanup = action_note_work(report->_cleanup_freshness_hack = 0, report->cleanup);
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
    return false;
}
