#include "relational_cursor.h"
#include "relational_plan_internal.h"
#include "shared_transition.h"

#include <string.h>

typedef struct {
    size_t begin;
    size_t end;
} RelationalRange;

typedef struct {
    size_t factor;
    size_t column;
    size_t position;
    /* Parent range, retained even while this iterator advances. */
    RelationalRange before;
} RelationalIterator;

typedef struct {
    size_t count;
    size_t current;
} RelationalLevel;

typedef enum {
    RELATIONAL_CURSOR_START = 0,
    RELATIONAL_CURSOR_INTERSECT,
    RELATIONAL_CURSOR_NARROW,
    RELATIONAL_CURSOR_LEAF,
    RELATIONAL_CURSOR_ADVANCE,
    RELATIONAL_CURSOR_BACKTRACK,
} RelationalCursorPhase;

struct CettaRelationalCountCursor {
    const CettaRelationalPlan *plan;
    RelationalRange *ranges;
    RelationalIterator *iters;
    RelationalLevel *levels;
    size_t range_cells;
    size_t iterator_cells;
    size_t exclusive_bytes;
    size_t depth;
    size_t narrow_index;
    uint64_t maximum;
    uint64_t key;
    uint64_t total;
    CettaRelationalWork work;
    RelationalCursorPhase phase;
    CettaRelationalCountStatus status;
    /* An enumeration hands each leaf to `visit`, with the key each level
     * chose in `values`; a count has neither. */
    CettaRelationalVisit visit;
    void *visit_context;
    AtomId *values;
    _Alignas(max_align_t) unsigned char state[];
};

_Static_assert(sizeof(RelationalRange) % _Alignof(RelationalIterator) == 0u,
    "range payload must align iterators");
_Static_assert(sizeof(RelationalIterator) % _Alignof(RelationalLevel) == 0u,
    "iterator payload must align levels");

static void relational_cursor_locate_state(CettaRelationalCountCursor *cursor) {
    cursor->ranges = (RelationalRange *)cursor->state;
    cursor->iters = (RelationalIterator *)(cursor->ranges + cursor->range_cells);
    cursor->levels = (RelationalLevel *)(cursor->iters + cursor->iterator_cells);
}

void cetta_relational_count_cursor_free(CettaRelationalCountCursor *cursor) {
    if (!cursor)
        return;
    cetta_relational_plan_free((CettaRelationalPlan *)cursor->plan);
    free(cursor);
}

static CettaRelationalCountCursor *relational_cursor_allocate(
    const CettaRelationalPlan *plan) {
    size_t factors = plan->info.factor_count, variables = plan->info.variable_count;
    /* One current range per factor and one saved range per incidence.
     * Depths share untouched ranges rather than copying a factor matrix. */
    size_t range_cells = factors;
    size_t bytes = sizeof(CettaRelationalCountCursor), peak = 0u;
    size_t iterator_cells = plan->incidence_count ? plan->incidence_count : 1u;
    if (!relational_add_bytes(&bytes, range_cells, sizeof(RelationalRange)) ||
        !relational_add_bytes(&bytes, iterator_cells, sizeof(RelationalIterator)) ||
        !relational_add_bytes(&bytes, variables ? variables : 1u, sizeof(RelationalLevel)) ||
        __builtin_add_overflow(plan->info.retained_bytes, bytes, &peak))
        return NULL;
    CettaRelationalCountCursor *cursor = calloc(1u, bytes);
    if (!cursor || !cetta_relational_plan_retain(plan)) {
        free(cursor);
        return NULL;
    }
    cursor->range_cells = range_cells;
    cursor->iterator_cells = iterator_cells;
    cursor->exclusive_bytes = bytes;
    cursor->work.peak_bytes = peak;
    cursor->plan = plan;
    relational_cursor_locate_state(cursor);
    return cursor;
}

static CettaRelationalCountCursor *relational_cursor_initialize(
    const CettaRelationalPlan *plan) {
    CettaRelationalCountCursor *cursor = relational_cursor_allocate(plan);
    if (!cursor)
        return NULL;
    for (size_t f = 0u; f < plan->info.factor_count; f++) {
        cursor->ranges[f] = (RelationalRange){0u, plan->factors[f].length};
        if (!plan->factors[f].length)
            cursor->status = CETTA_RELATIONAL_COUNT_COMPLETE;
    }
    cursor->phase = plan->info.variable_count ? RELATIONAL_CURSOR_START : RELATIONAL_CURSOR_LEAF;
    return cursor;
}

bool cetta_relational_count_cursor_open(const CettaRelationalPlan *plan,
    const Space *space, CettaRelationalCountCursor **out) {
    CETTA_SCOPED_SHARED_TRANSITION(shared_read);
    if (!out || !cetta_relational_plan_valid_for(plan, space) ||
        plan->rows_unsorted || plan->open_part_count)
        return false;
    CettaRelationalCountCursor *cursor = relational_cursor_initialize(plan);
    if (!cursor)
        return false;
    if (!cetta_relational_plan_valid_for(plan, space)) {
        cetta_relational_count_cursor_free(cursor);
        return false;
    }
    *out = cursor;
    return true;
}

bool cetta_relational_count_cursor_clone(const CettaRelationalCountCursor *source,
    const Space *space, CettaRelationalCountCursor **out) {
    CETTA_SCOPED_SHARED_TRANSITION(shared_read);
    if (!source || !out || source->status == CETTA_RELATIONAL_COUNT_INVALIDATED ||
        !cetta_relational_plan_valid_for(source->plan, space))
        return false;
    CettaRelationalCountCursor *copy = relational_cursor_allocate(source->plan);
    if (!copy)
        return false;
    memcpy(copy, source, source->exclusive_bytes);
    relational_cursor_locate_state(copy);
    if (!cetta_relational_plan_valid_for(copy->plan, space)) {
        cetta_relational_count_cursor_free(copy);
        return false;
    }
    *out = copy;
    return true;
}

static uint64_t relational_iterator_key(const CettaRelationalPlan *plan, const RelationalIterator *it) {
    const RelationalFactor *factor = &plan->factors[it->factor];
    return factor->tuples[it->position * factor->width + it->column];
}

static size_t relational_bound(const RelationalFactor *factor, size_t column,
    size_t begin, size_t end, uint64_t key, bool upper, CettaRelationalWork *work) {
    work->key_seeks++;
    while (begin < end) {
        work->key_comparisons++;
        size_t middle = begin + (end - begin) / 2u;
        uint64_t value = factor->tuples[middle * factor->width + column];
        if (value < key || (upper && value == key))
            begin = middle + 1u;
        else
            end = middle;
    }
    return begin;
}

static inline bool relational_level_start(const CettaRelationalPlan *plan, size_t variable,
    const RelationalRange *ranges, RelationalIterator *iters, RelationalLevel *level) {
    level->count = 0u;
    level->current = 0u;
    for (size_t slot = plan->variable_offsets[variable];
         slot < plan->variable_offsets[variable + 1u]; slot++) {
        RelationalIncidence incidence = plan->incidences[slot];
        if (ranges[incidence.factor].begin == ranges[incidence.factor].end)
            return false;
        RelationalIterator it = {incidence.factor, incidence.column,
            ranges[incidence.factor].begin, ranges[incidence.factor]};
        size_t p = level->count++;
        while (p && relational_iterator_key(plan, &iters[p - 1u]) >
                      relational_iterator_key(plan, &it)) {
            iters[p] = iters[p - 1u];
            p--;
        }
        iters[p] = it;
    }
    return level->count != 0u;
}

static bool relational_leaf_count(const CettaRelationalPlan *plan,
    const RelationalRange *ranges, uint64_t *total, uint64_t *leaf_weight) {
    uint64_t weight = 1u;
    for (size_t f = 0u; f < plan->info.factor_count; f++) {
        if (__builtin_mul_overflow(weight, (uint64_t)(ranges[f].end - ranges[f].begin), &weight))
            return false;
    }
    uint64_t next = 0u;
    if (__builtin_add_overflow(*total, weight, &next))
        return false;
    *total = next;
    *leaf_weight = weight;
    return true;
}

/* One navigation algorithm serves both bounded resumption and direct drain.
 * Inlining specializes the NULL-budget drain: it keeps the same transitions
 * without checking or recording a suspension allowance in its inner loop. */
static inline __attribute__((always_inline)) void relational_cursor_run(
    CettaRelationalCountCursor *cursor, uint64_t *remaining) {
    if (cursor->status != CETTA_RELATIONAL_COUNT_PAUSED)
        return;
    const CettaRelationalPlan *plan = cursor->plan;
    size_t variables = plan->info.variable_count;
    size_t depth = cursor->depth, narrow = cursor->narrow_index;
    uint64_t maximum = cursor->maximum, key = cursor->key;
    uint64_t total = cursor->total;
    CettaRelationalWork work = cursor->work;
    RelationalRange *ranges = cursor->ranges;
    RelationalIterator *iters = cursor->iters;
    RelationalLevel *levels = cursor->levels;
    RelationalLevel local_level = {0};
    RelationalLevel *level = &local_level;
    RelationalIterator *current;

#define RELATIONAL_TICK(next_phase) do {                          \
    if (remaining) {                                             \
        if (!*remaining) {                                      \
            cursor->phase = (next_phase);                        \
            goto stopped;                                       \
        }                                                       \
        --*remaining;                                           \
    }                                                           \
} while (0)
#define RELATIONAL_AT_DEPTH() do {                               \
    local_level = levels[depth];                                 \
    current = iters + (variables ? plan->variable_offsets[depth] : 0u); \
} while (0)

    /* The active level stays local until depth changes or a slice stops.
     * Incidence offsets locate its private iterators in the packed trail. */
    RELATIONAL_AT_DEPTH();
    switch (cursor->phase) {
    case RELATIONAL_CURSOR_START: goto start;
    case RELATIONAL_CURSOR_INTERSECT: goto intersect;
    case RELATIONAL_CURSOR_NARROW: goto narrow_range;
    case RELATIONAL_CURSOR_LEAF: goto leaf;
    case RELATIONAL_CURSOR_ADVANCE: goto advance;
    case RELATIONAL_CURSOR_BACKTRACK: goto backtrack;
    }
start:
    RELATIONAL_TICK(RELATIONAL_CURSOR_START);
    RELATIONAL_AT_DEPTH();
    if (!relational_level_start(plan, depth, ranges, current, level))
        goto backtrack;
    maximum = relational_iterator_key(plan, &current[level->count - 1u]);
intersect:
    RELATIONAL_TICK(RELATIONAL_CURSOR_INTERSECT);
    RelationalIterator *it = &current[level->current];
    uint64_t value = relational_iterator_key(plan, it);
    if (value == maximum) {
        work.candidate_prefixes++;
        key = value;
        narrow = 0u;
        goto narrow_range;
    }
    it->position = relational_bound(&plan->factors[it->factor], it->column,
        it->position, it->before.end, maximum, false, &work);
    if (it->position == it->before.end)
        goto backtrack;
    maximum = relational_iterator_key(plan, it);
    level->current = (level->current + 1u) % level->count;
    goto intersect;
narrow_range:
    RELATIONAL_TICK(RELATIONAL_CURSOR_NARROW);
    it = &current[narrow];
    ranges[it->factor].begin = it->position;
    ranges[it->factor].end = relational_bound(&plan->factors[it->factor], it->column,
        it->position, it->before.end, key, true, &work);
    if (++narrow < level->count)
        goto narrow_range;
    if (cursor->values)
        cursor->values[depth] = key;
    if (depth + 1u < variables) {
        levels[depth] = local_level;
        depth++;
        goto start;
    }
leaf:
    RELATIONAL_TICK(RELATIONAL_CURSOR_LEAF);
    work.leaf_assignments++;
    uint64_t leaf_weight = 0u;
    if (!relational_leaf_count(plan, ranges, &total, &leaf_weight)) {
        cursor->status = CETTA_RELATIONAL_COUNT_OVERFLOW;
        goto stopped;
    }
    if (cursor->visit &&
        !cursor->visit(cursor->visit_context, cursor->values, leaf_weight)) {
        cursor->status = CETTA_RELATIONAL_COUNT_STOPPED;
        goto stopped;
    }
    if (!variables)
        goto complete;
advance:
    RELATIONAL_TICK(RELATIONAL_CURSOR_ADVANCE);
    it = &current[level->current];
    it->position = ranges[it->factor].end;
    if (it->position == it->before.end)
        goto backtrack;
    maximum = relational_iterator_key(plan, it);
    level->current = (level->current + 1u) % level->count;
    goto intersect;
backtrack:
    RELATIONAL_TICK(RELATIONAL_CURSOR_BACKTRACK);
    /* Descendants have restored their ranges before reaching this level.
     * Restore this level's touched factors; all others still belong to its
     * parent. The next sibling must seek within that original parent view. */
    for (size_t i = 0u; i < level->count; i++)
        ranges[current[i].factor] = current[i].before;
    if (!depth)
        goto complete;
    levels[depth] = local_level;
    depth--;
    RELATIONAL_AT_DEPTH();
    goto advance;
complete:
    cursor->status = CETTA_RELATIONAL_COUNT_COMPLETE;
stopped:
    if (remaining) {
        levels[depth] = local_level;
        cursor->depth = depth;
        cursor->narrow_index = narrow;
        cursor->maximum = maximum;
        cursor->key = key;
    }
    cursor->total = total;
    cursor->work = work;
#undef RELATIONAL_AT_DEPTH
#undef RELATIONAL_TICK
}

CettaRelationalCountStatus cetta_relational_count_cursor_status(
    const CettaRelationalCountCursor *cursor) {
    return cursor ? cursor->status : CETTA_RELATIONAL_COUNT_INVALIDATED;
}

CettaRelationalCountStatus cetta_relational_count_cursor_advance(
    CettaRelationalCountCursor *cursor, const Space *space,
    uint64_t quantum, uint64_t *used) {
    CETTA_SCOPED_SHARED_TRANSITION(shared_read);
    uint64_t remaining = quantum;
    if (cursor) {
        if (!cetta_relational_plan_valid_for(cursor->plan, space))
            cursor->status = CETTA_RELATIONAL_COUNT_INVALIDATED;
        relational_cursor_run(cursor, &remaining);
        if (!cetta_relational_plan_valid_for(cursor->plan, space))
            cursor->status = CETTA_RELATIONAL_COUNT_INVALIDATED;
    }
    if (used)
        *used = quantum - remaining;
    return cetta_relational_count_cursor_status(cursor);
}

static bool relational_cursor_publish(const CettaRelationalCountCursor *cursor,
    uint64_t *count, CettaRelationalWork *work) {
    if (!count || cursor->status != CETTA_RELATIONAL_COUNT_COMPLETE)
        return false;
    *count = cursor->total;
    if (work)
        *work = cursor->work;
    return true;
}

bool cetta_relational_count_cursor_result(const CettaRelationalCountCursor *cursor,
    const Space *space, uint64_t *count, CettaRelationalWork *work) {
    CETTA_SCOPED_SHARED_TRANSITION(shared_read);
    return cursor && cetta_relational_plan_valid_for(cursor->plan, space) &&
        relational_cursor_publish(cursor, count, work);
}

bool cetta_relational_count_cursor_storage(const CettaRelationalCountCursor *cursor,
    CettaContinuationStorage *storage) {
    if (!cursor || !storage)
        return false;
    *storage = (CettaContinuationStorage){cursor->plan,
        cursor->plan->info.retained_bytes, cursor->exclusive_bytes};
    return true;
}

bool cetta_relational_plan_count(const CettaRelationalPlan *plan, const Space *space,
    uint64_t *count, CettaRelationalWork *work) {
    CETTA_SCOPED_SHARED_TRANSITION(shared_read);
    if (!count || !cetta_relational_plan_valid_for(plan, space) ||
        plan->rows_unsorted)
        return false;
    CettaRelationalCountCursor *cursor = relational_cursor_initialize(plan);
    if (!cursor)
        return false;
    relational_cursor_run(cursor, NULL);
    bool counted = cetta_relational_plan_valid_for(plan, space) &&
        relational_cursor_publish(cursor, count, work);
    cetta_relational_count_cursor_free(cursor);
    /* Each open row's part counts the combinations holding that row. */
    for (size_t i = 0u; counted && i < plan->open_part_count; i++) {
        const RelationalOpenPart *part = &plan->open_parts[i];
        uint64_t part_count = 1u;
        CettaRelationalWork part_work = {0};
        counted = (!part->plan ||
                   cetta_relational_plan_count(part->plan, space, &part_count,
                                               &part_work)) &&
            !__builtin_add_overflow(*count, part_count, count);
        if (counted && work) {
            work->key_seeks += part_work.key_seeks;
            work->key_comparisons += part_work.key_comparisons;
            work->candidate_prefixes += part_work.candidate_prefixes;
            work->leaf_assignments += part_work.leaf_assignments;
        }
    }
    return counted;
}

typedef struct {
    const RelationalOpenPart *part;
    CettaRelationalVisit visit;
    void *context;
    AtomId *values;
    size_t variables;
} RelationalPartVisit;

/* A part's solution as one of the enclosing plan's. */
static bool relational_part_visit(void *context, const AtomId *values,
                                  uint64_t weight) {
    RelationalPartVisit *run = context;
    for (size_t v = 0u; v < run->variables; v++)
        run->values[v] = run->part->sources[v] == SIZE_MAX
            ? run->part->constants[v] : values[run->part->sources[v]];
    return run->visit(run->context, run->values, weight);
}

/* The count's navigation, with a visitor at its leaves.  The factors hold
 * every row the plan was prepared from, so the run reads no live state and
 * needs no validity check once it has started. */
bool cetta_relational_plan_enumerate(const CettaRelationalPlan *plan,
    const Space *space, CettaRelationalVisit visit, void *context,
    uint64_t *count, CettaRelationalWork *work) {
    CETTA_SCOPED_SHARED_TRANSITION(shared_read);
    if (!visit || !count || !cetta_relational_plan_valid_for(plan, space))
        return false;
    if (plan->rows_unsorted) {
        /* One pattern: each row below scan_rows that matches it is a
         * solution of weight one, matched again in turn. */
        const RelationalFactor *factor = &plan->factors[0];
        const TermUniverse *universe = space->native.universe;
        size_t variables = plan->info.variable_count;
        AtomId *values = relational_array(variables, sizeof(*values));
        bool *assigned = relational_array(variables, sizeof(*assigned));
        if (!values || !assigned) {
            free(values);
            free(assigned);
            return false;
        }
        bool stopped = false;
        uint64_t total = 0u;
        for (CettaIndex row = 0u; !stopped && row < plan->scan_rows; row++) {
            AtomId id = space_get_atom_id_at64(space, row);
            if (tu_kind(universe, id) != ATOM_EXPR ||
                tu_arity(universe, id) != plan->scan_len)
                continue;
            for (size_t v = 0u; v < variables; v++)
                assigned[v] = false;
            bool matched = true;
            for (CettaExprIndex c = 0u; matched && c < plan->scan_len; c++) {
                AtomId value = tu_child(universe, id, c);
                size_t position = plan->scan_positions[c];
                if (position == SIZE_MAX) {
                    matched = plan->scan_constants[c] == value;
                    continue;
                }
                size_t slot = factor->variables[position];
                if (!assigned[slot]) {
                    values[slot] = value;
                    assigned[slot] = true;
                } else {
                    matched = values[slot] == value;
                }
            }
            if (!matched)
                continue;
            total++;
            stopped = !visit(context, values, 1u);
        }
        free(values);
        free(assigned);
        if (stopped)
            return false;
        *count = total;
        if (work)
            *work = (CettaRelationalWork){.leaf_assignments = total};
        return true;
    }
    CettaRelationalCountCursor *cursor = relational_cursor_initialize(plan);
    if (!cursor)
        return false;
    size_t variables = plan->info.variable_count;
    cursor->values = relational_array(variables, sizeof(*cursor->values));
    if (!cursor->values) {
        cetta_relational_count_cursor_free(cursor);
        return false;
    }
    cursor->visit = visit;
    cursor->visit_context = context;
    relational_cursor_run(cursor, NULL);
    bool complete = relational_cursor_publish(cursor, count, work);
    free(cursor->values);
    cetta_relational_count_cursor_free(cursor);
    if (!complete || !plan->open_part_count)
        return complete;
    /* Each open row's part gives the solutions holding that row. */
    AtomId *values = relational_array(variables, sizeof(*values));
    RelationalPartVisit run = {
        .visit = visit, .context = context, .values = values,
        .variables = variables};
    for (size_t i = 0u; complete && values && i < plan->open_part_count; i++) {
        run.part = &plan->open_parts[i];
        uint64_t part_count = 0u;
        CettaRelationalWork part_work = {0};
        if (run.part->plan) {
            complete = cetta_relational_plan_enumerate(
                run.part->plan, space, relational_part_visit, &run,
                &part_count, &part_work);
        } else {
            part_count = 1u;
            complete = relational_part_visit(&run, NULL, 1u);
        }
        complete = complete &&
            !__builtin_add_overflow(*count, part_count, count);
        if (complete && work) {
            work->key_seeks += part_work.key_seeks;
            work->key_comparisons += part_work.key_comparisons;
            work->candidate_prefixes += part_work.candidate_prefixes;
            work->leaf_assignments += part_work.leaf_assignments;
        }
    }
    free(values);
    return complete && values;
}

static CettaContinuationStatus relational_count_capture(void *machine, void **payload) {
    CettaRelationalCountEndpoint *endpoint = machine;
    if (!endpoint || !endpoint->cursor || !*endpoint->cursor || !payload)
        return CETTA_CONTINUATION_UNSUPPORTED;
    CettaRelationalCountCursor *cursor = *endpoint->cursor, *copy = NULL;
    CETTA_SCOPED_SHARED_TRANSITION(shared_read);
    if (cursor->status == CETTA_RELATIONAL_COUNT_INVALIDATED ||
        !cetta_relational_plan_valid_for(cursor->plan, endpoint->space))
        return CETTA_CONTINUATION_INVALIDATED;
    if (!cetta_relational_count_cursor_clone(cursor, endpoint->space, &copy))
        return CETTA_CONTINUATION_CAPACITY;
    *payload = copy;
    return CETTA_CONTINUATION_READY;
}

static CettaContinuationStatus relational_count_restore(void *machine, void **payload) {
    CettaRelationalCountEndpoint *endpoint = machine;
    if (!endpoint || !endpoint->cursor || !payload || !*payload)
        return CETTA_CONTINUATION_UNSUPPORTED;
    CettaRelationalCountCursor *cursor = *payload;
    CETTA_SCOPED_SHARED_TRANSITION(shared_read);
    if (cursor->status == CETTA_RELATIONAL_COUNT_INVALIDATED ||
        !cetta_relational_plan_valid_for(cursor->plan, endpoint->space))
        return CETTA_CONTINUATION_INVALIDATED;
    cetta_relational_count_cursor_free(*endpoint->cursor);
    *endpoint->cursor = cursor;
    *payload = NULL;
    return CETTA_CONTINUATION_READY;
}

static void relational_count_destroy(void *payload) {
    cetta_relational_count_cursor_free(payload);
}

static bool relational_count_storage(const void *payload, CettaContinuationStorage *storage) {
    return cetta_relational_count_cursor_storage(payload, storage);
}

static const CettaContinuationProvider relational_count_provider = {
    .representation_name = "owned-relational-count-cursor",
    .ownership = {.capture=relational_count_capture, .restore=relational_count_restore,
                  .destroy=relational_count_destroy, .storage=relational_count_storage},
};

CettaContinuationMachine cetta_relational_count_continuation_machine(
    CettaRelationalCountEndpoint *endpoint) {
    return (CettaContinuationMachine){endpoint, &relational_count_provider};
}
