#ifndef CETTA_STATS_H
#define CETTA_STATS_H

#include "atom.h"
#include <stdint.h>

typedef struct Space Space;

typedef enum {
    CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP = 0,
    CETTA_RUNTIME_COUNTER_BINDINGS_CLONE = 1,
    CETTA_RUNTIME_COUNTER_BINDINGS_ADD = 2,
    CETTA_RUNTIME_COUNTER_BINDINGS_CONSTRAINT_ADD = 3,
    CETTA_RUNTIME_COUNTER_BINDINGS_MERGE = 4,
    CETTA_RUNTIME_COUNTER_RENAME_VARS = 5,
    CETTA_RUNTIME_COUNTER_QUERY_EQUATIONS = 6,
    CETTA_RUNTIME_COUNTER_QUERY_EQUATION_CANDIDATES = 7,
    CETTA_RUNTIME_COUNTER_EQ_INDEX_REBUILD = 8,
    CETTA_RUNTIME_COUNTER_TY_INDEX_REBUILD = 9,
    CETTA_RUNTIME_COUNTER_COUNT = 10
} CettaRuntimeCounter;

typedef struct {
    uint64_t counters[CETTA_RUNTIME_COUNTER_COUNT];
} CettaRuntimeStats;

const char *cetta_runtime_counter_name(CettaRuntimeCounter counter);
void cetta_runtime_stats_reset(void);
void cetta_runtime_stats_add(CettaRuntimeCounter counter, uint64_t delta);
void cetta_runtime_stats_snapshot(CettaRuntimeStats *out);
void cetta_runtime_stats_populate_space(Space *space, Arena *a,
                                        const CettaRuntimeStats *stats);

static inline void cetta_runtime_stats_inc(CettaRuntimeCounter counter) {
    cetta_runtime_stats_add(counter, 1);
}

#endif
