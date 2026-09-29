/* The stages after a TPTP read, as tptp:read and tptp:print run them, for
 * the corpus tools: the compact morphism under the mixed leaf policy, the
 * printer of its records, and the printed text read and compacted again,
 * which must give the same records.  The morphism of a file maps its inputs
 * and the printer prints one input per line, so the tools run the stages on
 * each input as the streaming read yields it, as a one-input file; the
 * records of a file are those of its inputs in order, and memory stays that
 * of one input. */
#ifndef CETTA_TPTP_COMPACT_STAGES_V1_H
#define CETTA_TPTP_COMPACT_STAGES_V1_H

#include "native/langdef_module.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    CettaDeterministicEquationPlanV1 *compact;
    CettaDeterministicEquationPlanV1 *print;
    SymbolId *wrappers;
    uint32_t wrapper_len;
} TptpCompactStagesV1;

typedef enum {
    TPTP_STAGES_OK_V1 = 0,
    TPTP_STAGES_COMPACT_FAILED_V1,
    TPTP_STAGES_PRINT_FAILED_V1,
    TPTP_STAGES_REREAD_FAILED_V1,
    TPTP_STAGES_ROUND_TRIP_DIFFERS_V1
} TptpStageResultV1;

/* Totals over the inputs of one file; the first failure stops the stages. */
typedef struct {
    TptpStageResultV1 result;
    double compact_s;
    double print_s;
    double reread_s;
    uint64_t compact_work;
    size_t records;
    size_t printed_bytes;
    char error[512];
} TptpStageCostV1;

typedef struct {
    const CettaTptpPreparedReaderV1 *reader;
    const TptpCompactStagesV1 *stages;
    Arena *arena;
    TptpStageCostV1 cost;
} TptpStageRunV1;

static inline const char *tptp_stage_result_name_v1(TptpStageResultV1 result) {
    switch (result) {
    case TPTP_STAGES_OK_V1:
        return "ok";
    case TPTP_STAGES_COMPACT_FAILED_V1:
        return "compact-failed";
    case TPTP_STAGES_PRINT_FAILED_V1:
        return "print-failed";
    case TPTP_STAGES_REREAD_FAILED_V1:
        return "reread-failed";
    case TPTP_STAGES_ROUND_TRIP_DIFFERS_V1:
        return "round-trip-differs";
    }
    return "unknown";
}

static inline double tptp_stages_clock_v1(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0.0;
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static inline void tptp_compact_stages_free_v1(TptpCompactStagesV1 *stages) {
    free(stages->wrappers);
    if (stages->print)
        cetta_deterministic_equation_plan_v1_free(stages->print);
    if (stages->compact)
        cetta_deterministic_equation_plan_v1_free(stages->compact);
    memset(stages, 0, sizeof(*stages));
}

/* The morphism and printer installed beside the snapshot, as tptp:read and
 * tptp:print load them. */
static inline bool tptp_compact_stages_load_v1(TptpCompactStagesV1 *stages,
                                               const char *snapshot,
                                               char *error,
                                               size_t error_size) {
    memset(stages, 0, sizeof(*stages));
    stages->compact = cetta_tptp_compact_manifest_plan_v1(
        snapshot, "manifest", error, error_size);
    if (!stages->compact) {
        tptp_compact_stages_free_v1(stages);
        return false;
    }
    stages->print = cetta_tptp_compact_manifest_plan_v1(
        snapshot, "print-manifest", error, error_size);
    if (!stages->print ||
        !cetta_tptp_compact_wrappers_v1(stages->compact, &stages->wrappers,
                                        &stages->wrapper_len)) {
        if (stages->print && error && error_size)
            snprintf(error, error_size, "compact wrappers unavailable");
        tptp_compact_stages_free_v1(stages);
        return false;
    }
    return true;
}

static inline void tptp_stage_run_init_v1(TptpStageRunV1 *run,
                                          const CettaTptpPreparedReaderV1 *reader,
                                          const TptpCompactStagesV1 *stages,
                                          Arena *arena) {
    memset(run, 0, sizeof(*run));
    run->reader = reader;
    run->stages = stages;
    run->arena = arena;
    run->cost.result = TPTP_STAGES_OK_V1;
}

static inline double tptp_stage_run_seconds_v1(const TptpStageRunV1 *run) {
    return run->cost.compact_s + run->cost.print_s + run->cost.reread_s;
}

/* One input of the file being read.  False once a stage has failed; the
 * cost then names the stage and its error. */
static inline bool tptp_stage_run_input_once_v1(TptpStageRunV1 *run, Atom *input) {
    TptpStageCostV1 *cost = &run->cost;
    Atom *file;
    Atom *records = NULL;
    Atom *again = NULL;
    Atom *again_records = NULL;
    Atom *const *items = NULL;
    CettaExprLen count = 0u;
    CettaTptpReadOutcomeV1 outcome;
    uint64_t work = 0u;
    char *text = NULL;
    size_t text_len = 0u;
    double t0;
    if (cost->result != TPTP_STAGES_OK_V1)
        return false;
    file = atom_expr(run->arena, &input, 1u);
    t0 = tptp_stages_clock_v1();
    if (!cetta_tptp_compact_records_v1(
            run->stages->compact, "mixed", file, run->arena, &records, &work,
            NULL, cost->error, sizeof(cost->error))) {
        cost->compact_s += tptp_stages_clock_v1() - t0;
        cost->result = TPTP_STAGES_COMPACT_FAILED_V1;
        return false;
    }
    cost->compact_s += tptp_stages_clock_v1() - t0;
    cost->compact_work += work;
    if (atom_sequence_view(records, &items, &count))
        cost->records += (size_t)count;
    t0 = tptp_stages_clock_v1();
    if (!cetta_tptp_compact_print_v1(
            run->reader, NULL, run->stages->print,
            run->stages->wrappers,
            run->stages->wrapper_len, records, run->arena, &text, &text_len,
            NULL, NULL, NULL, NULL, NULL, cost->error, sizeof(cost->error))) {
        cost->print_s += tptp_stages_clock_v1() - t0;
        cost->result = TPTP_STAGES_PRINT_FAILED_V1;
        return false;
    }
    cost->print_s += tptp_stages_clock_v1() - t0;
    cost->printed_bytes += text_len;
    t0 = tptp_stages_clock_v1();
    if (!cetta_tptp_prepared_reader_read_text_outcome_v1(
            run->reader, text, text_len, run->arena, &again, &outcome,
            cost->error, sizeof(cost->error)) ||
        !cetta_tptp_compact_records_v1(
            run->stages->compact, "mixed", again, run->arena, &again_records,
            NULL, NULL, cost->error, sizeof(cost->error))) {
        cost->reread_s += tptp_stages_clock_v1() - t0;
        cost->result = TPTP_STAGES_REREAD_FAILED_V1;
        free(text);
        return false;
    }
    cost->reread_s += tptp_stages_clock_v1() - t0;
    free(text);
    if (!atom_eq(records, again_records)) {
        snprintf(cost->error, sizeof(cost->error),
                 "printed input %zu reads back to other records",
                 cost->records);
        cost->result = TPTP_STAGES_ROUND_TRIP_DIFFERS_V1;
        return false;
    }
    return true;
}

/* The stages of one input allocate above a mark that is released when they
 * finish, so a file of many inputs needs the memory of its largest input,
 * not of all of them. */
static inline bool tptp_stage_run_input_v1(TptpStageRunV1 *run, Atom *input) {
    ArenaMark mark = arena_mark(run->arena);
    bool ok = tptp_stage_run_input_once_v1(run, input);
    arena_reset(run->arena, mark);
    return ok;
}

#endif /* CETTA_TPTP_COMPACT_STAGES_V1_H */
