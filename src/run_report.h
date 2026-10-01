/* run_report.h — native run-report state machine (2026-09-30, Kimi).
 *
 * One report covers one declared query demand.  The module is a streaming
 * state machine over framed packets: activity occurrences (answers, worker
 * lifetimes, output/cleanup acknowledgements and failures), resolved test
 * verdicts, unhandled faults, and exactly one terminal stop record.  It is
 * the native counterpart of the executable contract in
 * Mettapedia/Machines/RunContracts (Runner, Completion, TestPlan,
 * Diagnostics, ScopedOutcome), without claiming any proof correspondence.
 *
 * Contract, stated independently of this implementation:
 *  - A run ends with exactly one terminal record; a missing terminal,
 *    a duplicate terminal, or any event after a terminal makes the run
 *    unsuccessful, never successful.
 *  - Ordinary exhaustive evaluation succeeds with zero answers when
 *    observation and finalization complete.  A too-short deliberate stop,
 *    an interrupted or depth-cut stop, a failed output or cleanup, an
 *    illegal worker lifetime, or an unresolved failed assertion prevents
 *    success.
 *  - Invalid framing has status 1. Within valid framing, an unhandled
 *    execution fault, recorded or terminal, forces status 2. Other
 *    unsuccessful contracts give 1; successful ones 0.  A caught
 *    error or a passed expected-error assertion produces no unhandled-fault
 *    event at all.  Answer payloads are never inspected for classification.
 *  - Test mode checks exact declared identity multiplicities and resolved
 *    verdicts; the plan must be declared from distinct ids and a disallowed
 *    empty discovery plan fails.  No count of pass text substitutes.
 *  - Diagnostic detail limits never limit fault accounting: the total is
 *    always exact, details are a bounded view, and overflow is reported
 *    truthfully rather than treated as complete detail.
 *  - Worker ids identify fresh ownership lifetimes, not reusable thread
 *    identifiers; duplicate starts and unstarted or duplicate settlements
 *    are protocol violations.
 *  - Observation completion and output/cleanup finalization are separate:
 *    a failed latter never erases a complete former.
 *
 * The module is additive infrastructure.  It reads no files, writes no
 * output, and imposes no new user switches or call policies; the shared
 * CLI/session wiring is an integration obligation of the caller, offered
 * separately as a patch.
 */
#ifndef CETTA_RUN_REPORT_H
#define CETTA_RUN_REPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Demand (Completion.Demand): exhaustive, or a deliberate bounded prefix. */
typedef enum {
    RUN_REPORT_DEMAND_EXHAUSTIVE = 0,
    RUN_REPORT_DEMAND_PREFIX = 1,
} RunReportDemandKind;

typedef struct {
    RunReportDemandKind kind;
    uint64_t count; /* relevant only when kind == RUN_REPORT_DEMAND_PREFIX */
} RunReportDemand;

/* Completion.Event, payload-free: answer occurrences count only. */
typedef enum {
    RUN_REPORT_ACTIVITY_ANSWER = 0,
    RUN_REPORT_ACTIVITY_WORKER_STARTED,
    RUN_REPORT_ACTIVITY_WORKER_SETTLED,
    RUN_REPORT_ACTIVITY_OUTPUT_COMPLETE,
    RUN_REPORT_ACTIVITY_OUTPUT_FAILED,
    RUN_REPORT_ACTIVITY_CLEANUP_COMPLETE,
    RUN_REPORT_ACTIVITY_CLEANUP_FAILED,
} RunReportActivityKind;

/* ProducerStop over RunStop: observed complete/interrupted/depth-cut; the
 * deliberate demand satisfaction; a terminal fault. */
typedef enum {
    RUN_REPORT_STOP_OBSERVED_COMPLETE = 0,
    RUN_REPORT_STOP_OBSERVED_INTERRUPTED,
    RUN_REPORT_STOP_OBSERVED_DEPTH_CUT,
    RUN_REPORT_STOP_DEMAND_SATISFIED,
    RUN_REPORT_STOP_FAULT,
} RunReportStopKind;

/* One diagnostic detail record at most per unhandled fault occurrence, held
 * while the detail budget allows.  Accounting is exact even when this view
 * truncates.  code/origin/payload_id are caller-stable identities; the module
 * owns nothing behind them. */
typedef struct {
    uint64_t occurrence;  /* 1-based ordinal among this query's faults */
    uint64_t code;
    uint64_t origin;
    uint64_t payload_id;
} RunReportDiagDetail;

typedef struct {
    uint64_t total;         /* always exact */
    uint64_t shown;         /* details currently held */
    uint64_t omitted;       /* total - shown */
    bool details_overflow;  /* more unhandled faults than detail budget */
    const RunReportDiagDetail *details; /* valid for read until *_free */
} RunReportCollected;

typedef struct {
    uint32_t exit_code;             /* 0 success, 1 invalid/unsuccessful, 2 validly framed fault */
    bool observation_complete;      /* Completion: demand-relative observation */
    bool tests_passed;              /* TestContract: no-plan all-pass or exact bag */
    bool finalization_ready;        /* ResourcesReady: protocol + workers + outputs */
    bool framing_ok;                /* exactly one terminal, nothing after it */
    RunReportCollected diagnostics;
    uint64_t answer_count;
    uint64_t test_count;
    uint64_t worker_count_started;
    uint64_t worker_count_pending;
} RunReportOutcome;

typedef enum {
    RUN_REPORT_PLAN_OK = 0,
    RUN_REPORT_PLAN_DUPLICATE_DECLARED_ID, /* fromList rejects: no plan exists */
    RUN_REPORT_PLAN_CAPACITY_EXCEEDED,     /* facility exhaustion, truthfully */
    RUN_REPORT_PLAN_ALREADY_DECLARED,
} RunReportPlanStatus;

typedef struct RunReport RunReport;
typedef struct RunReportDoc RunReportDoc;

/* Lifecycle ------------------------------------------------------------------*/

RunReport *run_report_begin(RunReportDemand demand, uint64_t detail_budget);
void run_report_free(RunReport *report);
/* Memory copy of detail records; valid after _free until *_outcome_details_free */
void run_report_outcome_details_free(RunReportOutcome *outcome);

/* Packets ---------------------------------------------------------------------*/

/* One answer occurrence: its payload is the caller's concern. */
bool run_report_answer(RunReport *report);
/* Worker lifetime events: started/settled with the id; the remaining kinds
 * take id argument ignored (pass 0).  False means framing is already broken
 * (an event after the terminal) and was latched; the rejected feed cannot
 * make the run successful. */
bool run_report_activity(RunReport *report, RunReportActivityKind kind,
                         uint64_t worker_id);
/* A resolved verdict, already evaluated by an assertion host (an expected
 * error consumed by its assertion is presented as passed, never additionally
 * as an unhandled fault). */
bool run_report_test_verdict(RunReport *report, uint64_t test_id, bool passed);
/* An unhandled execution fault occurrence. */
bool run_report_unhandled_fault(RunReport *report, uint64_t code,
                                uint64_t origin, uint64_t payload_id);
/* Declared test plan.  Must be valid fromList-wise: distinct ids.
 * Empty plans need allow_empty true to ever succeed. */
RunReportPlanStatus run_report_test_plan_declare(RunReport *report,
                                                 const uint64_t *ids,
                                                 size_t count, bool allow_empty);

/* Terminal stop.  stop==RUN_REPORT_STOP_FAULT moves the terminal fault into
 * unhandled-fault accounting (a terminal fault is evidence without an earlier
 * fault event).  Any later packet is a framing violation. */
bool run_report_terminal(RunReport *report, RunReportStopKind stop,
                         uint64_t fault_code, uint64_t fault_origin,
                         uint64_t fault_payload_id);

/* Close the report.  A missing terminal makes the run unsuccessful.  The
 * outcome holds a heap copy of detail records; call
 * run_report_outcome_details_free on its details with matching _free. */
bool run_report_finalize(RunReport *report, RunReportOutcome *outcome);

/* Documents -------------------------------------------------------------------*/

/* A document aggregates per-query reports.  Answer counts, plans and worker
 * lifetimes must never pool: each query is its own RunReport.  Document
 * success requires every declared query to settle successfully. */
RunReportDoc *run_report_doc_begin(void);
/* Declare the entire catalogue before execution: unopened queries remain
 * outstanding, including those skipped after a failure. */
bool run_report_doc_declare_query(RunReportDoc *doc, uint64_t query_id);
RunReport *run_report_doc_boundary(RunReportDoc *doc);
void run_report_doc_free(RunReportDoc *doc);
/* Open one previously declared query, exactly once. Demand/plans stay
 * inside the query report. Settle the separate document boundary last. */
RunReport *run_report_doc_open_query(RunReportDoc *doc, uint64_t query_id,
                                     RunReportDemand demand,
                                     uint64_t detail_budget);
/* After queries: verify all settled.  The document's exit code: 2 if any
 * settled query reports exit 2, else 1 if any query is unsuccessful or
 * unsettled, else 0.  This aggregation is a CLI-level convention; the strict
 * contract is per-query. */
typedef struct {
    uint32_t exit_code;
    uint64_t queries_declared;
    uint64_t queries_settled_ok;
    uint64_t queries_settled_exit1;
    uint64_t queries_settled_exit2;
    uint64_t queries_unsettled;
} RunReportDocStatus;
bool run_report_doc_verify(const RunReportDoc *doc, RunReportDocStatus *out);
bool run_report_doc_query_at(const RunReportDoc *doc, size_t index,
                            uint64_t *query_id, RunReportOutcome *out);

#ifdef __cplusplus
}
#endif

#endif /* CETTA_RUN_REPORT_H */
