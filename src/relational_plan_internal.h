#ifndef CETTA_RELATIONAL_PLAN_INTERNAL_H
#define CETTA_RELATIONAL_PLAN_INTERNAL_H

#include "relational_plan.h"

#include <stdatomic.h>
#include <stdlib.h>

typedef struct {
    size_t width;
    size_t *variables;
    uint64_t *tuples;
    size_t length;
    size_t capacity;
} RelationalFactor;

typedef struct {
    size_t factor;
    size_t column;
} RelationalIncidence;

/* Published buffers are immutable. Each plan handle or consumer owns one
 * reference; borrowed universe coordinates still require live validation. */
struct CettaRelationalPlan {
    CettaRelationalPlanInfo info;
    VarId *variables;
    RelationalFactor *factors;
    size_t *variable_offsets;
    RelationalIncidence *incidences;
    size_t incidence_count;
    atomic_size_t references;
};

static inline void *relational_array(size_t count, size_t width) {
    if (width && count > SIZE_MAX / width)
        return NULL;
    return calloc(count ? count : 1u, width);
}

static inline bool relational_add_bytes(size_t *bytes, size_t count, size_t width) {
    size_t part = 0u;
    return !__builtin_mul_overflow(count, width, &part) &&
        !__builtin_add_overflow(*bytes, part, bytes);
}

#endif
