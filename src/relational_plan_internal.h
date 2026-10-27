#ifndef CETTA_RELATIONAL_PLAN_INTERNAL_H
#define CETTA_RELATIONAL_PLAN_INTERNAL_H

#include "relational_plan.h"

#include <stdatomic.h>
#include <stdlib.h>

typedef struct {
    size_t width;
    size_t *variables;
    /* Each tuple's values and then its row, a cell of 32 bits each: a plan
     * meeting a value that does not fit declines. */
    uint32_t *tuples;
    size_t length;
    size_t capacity;
} RelationalFactor;

typedef struct {
    size_t factor;
    size_t column;
} RelationalIncidence;

/* A row holding a stored variable for a whole column, joined on its own:
 * by a plan of the other factors with the row's bindings applied, the
 * factors before the row's own reading only rows without stored variables,
 * so that each combination of rows is met in exactly one part. */
typedef struct {
    /* NULL when the row's factor is the only one: one solution. */
    CettaRelationalPlan *plan;
    /* For each variable of the enclosing plan, the part plan's variable
     * that gives its value, or SIZE_MAX and the value in `constants`.  An
     * enumeration's parts have them; a count needs none. */
    size_t *sources;
    AtomId *constants;
} RelationalOpenPart;

/* Published buffers are immutable. Each plan handle or consumer owns one
 * reference; borrowed universe coordinates still require live validation. */
struct CettaRelationalPlan {
    CettaRelationalPlanInfo info;
    VarId *variables;
    RelationalFactor *factors;
    size_t *variable_offsets;
    RelationalIncidence *incidences;
    size_t incidence_count;
    /* An enumeration of one pattern joins nothing: it keeps no rows, and
     * enumeration matches again, in occurrence order, the rows below
     * `scan_rows` that preparation checked.  Per element of the pattern,
     * `scan_positions` holds the factor column of a variable (SIZE_MAX for
     * an atom) and `scan_constants` the id of an atom. */
    bool rows_unsorted;
    CettaExprLen scan_len;
    size_t *scan_positions;
    AtomId *scan_constants;
    CettaIndex scan_rows;
    /* The rows holding stored variables, which the factors do not hold. */
    RelationalOpenPart *open_parts;
    size_t open_part_count;
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
