#ifndef CETTA_RUN_OBSERVATION_H
#define CETTA_RUN_OBSERVATION_H

#include "atom.h"
#include "run_report.h"

/* Optional, out-of-band document observations. Answers are the evaluator's
 * ordered query frontier, not printed terms or a claim about yield timing.
 * Effects are bytes delivered through the current thread's host services.
 * Values without a structural codec remain explicitly unobserved; their
 * display or process-local address is never substituted for their meaning. */
typedef struct RunObservation RunObservation;
RunObservation *run_observation_open(const char *path);
void run_observation_free(RunObservation *observation);
bool run_observation_query_begin(RunObservation *, uint64_t query);
void run_observation_answers(RunObservation *, Atom *const *, size_t count);
void run_observation_query_end(RunObservation *, RunReportStopKind,
                               bool output_ok, bool cleanup_ok);
bool run_observation_finish(RunObservation *, const RunReportDocStatus *,
                            int dialect_exit);

#endif
