#include "relational_plan_internal.h"

#include "dense_relation.h"
#include "native_query.h"
#include "shared_transition.h"
#include "stats.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    AtomId id;
    uint8_t state;
} RelationalKeyState;

typedef struct {
    AtomId id;
    CettaExprIndex child;
} RelationalKeyFrame;

typedef struct {
    RelationalKeyState *slots;
    size_t capacity;
    size_t used;
    RelationalKeyFrame *stack;
    size_t stack_capacity;
} RelationalKeyCheck;

static size_t relational_key_hash(AtomId id) {
    id ^= id >> 30u;
    id *= UINT64_C(0xbf58476d1ce4e5b9);
    id ^= id >> 27u;
    id *= UINT64_C(0x94d049bb133111eb);
    return (size_t)(id ^ (id >> 31u));
}

static RelationalKeyState *relational_key_state(
    RelationalKeyCheck *check, AtomId id) {
    if (check->used >= check->capacity / 2u) {
        size_t capacity = check->capacity ? check->capacity * 2u : 64u;
        if (capacity < check->capacity)
            return NULL;
        RelationalKeyState *slots = relational_array(capacity, sizeof(*slots));
        if (!slots)
            return NULL;
        for (size_t i = 0u; i < check->capacity; i++) {
            RelationalKeyState old = check->slots[i];
            if (!old.state)
                continue;
            size_t p = relational_key_hash(old.id) & (capacity - 1u);
            while (slots[p].state)
                p = (p + 1u) & (capacity - 1u);
            slots[p] = old;
        }
        free(check->slots);
        check->slots = slots;
        check->capacity = capacity;
    }
    size_t p = relational_key_hash(id) & (check->capacity - 1u);
    while (check->slots[p].state && check->slots[p].id != id)
        p = (p + 1u) & (check->capacity - 1u);
    if (!check->slots[p].state) {
        check->slots[p] = (RelationalKeyState){id, 1u};
        check->used++;
    }
    return &check->slots[p];
}

/* A symbol, an integer, a Boolean or a string is a key by its header. */
static inline bool relational_leaf_key(const CettaTermHdr *header) {
    return header->tag == ATOM_SYMBOL ||
        (header->tag == ATOM_GROUNDED &&
         (header->subtag == GV_INT || header->subtag == GV_BOOL ||
          header->subtag == GV_STRING));
}

/* A memoized, explicit-stack check of the coordinate equality fragment.
 * No term decoding is needed and shared children are inspected once; a
 * leaf is read from its header and needs no memo. States 1/2/3 mean
 * discovered, active and checked; a cycle cannot pass as a tree. */
static bool relational_exact_key(
    RelationalKeyCheck *check, const TermUniverse *universe, AtomId root) {
    size_t depth = 0u;
    AtomId next = root;
    for (;;) {
        const CettaTermHdr *header = tu_hdr(universe, next);
        if (!header)
            return false;
        bool leaf = relational_leaf_key(header);
        RelationalKeyState *state = leaf ? NULL : relational_key_state(check, next);
        if (!leaf && (!state || state->state == 2u))
            return false;
        if (state && state->state != 3u) {
            if (header->tag == ATOM_EXPR && !tu_has_vars(universe, next)) {
                state->state = 2u;
                if (depth == check->stack_capacity) {
                    size_t capacity = check->stack_capacity ? check->stack_capacity * 2u : 32u;
                    if (capacity < check->stack_capacity ||
                        capacity > SIZE_MAX / sizeof(*check->stack))
                        return false;
                    RelationalKeyFrame *stack = realloc(check->stack, capacity * sizeof(*stack));
                    if (!stack)
                        return false;
                    check->stack = stack;
                    check->stack_capacity = capacity;
                }
                check->stack[depth++] = (RelationalKeyFrame){next, 0u};
            } else {
                return false;
            }
        }
        for (;;) {
            if (!depth)
                return true;
            RelationalKeyFrame *frame = &check->stack[depth - 1u];
            if (frame->child < tu_arity(universe, frame->id)) {
                next = tu_child(universe, frame->id, frame->child++);
                break;
            }
            /* A hash-table resize may have moved the parent's state. */
            state = relational_key_state(check, frame->id);
            if (!state)
                return false;
            state->state = 3u;
            depth--;
        }
    }
}

/* A stored row is a key when each of its children is.  A factor reads each
 * row once, so rows are not memoized themselves: the memo holds only the
 * atoms rows share. */
static bool relational_row_exact_key(
    RelationalKeyCheck *check, const TermUniverse *universe, AtomId row) {
    const CettaTermHdr *header = tu_hdr(universe, row);
    if (!header || header->tag != ATOM_EXPR || tu_has_vars(universe, row))
        return false;
    for (CettaExprIndex c = 0u; c < tu_arity(universe, row); c++) {
        if (!relational_exact_key(check, universe, tu_child(universe, row, c)))
            return false;
    }
    return true;
}

/* A row the factors cannot key that is joined on its own: each of its
 * children a key, or a stored variable standing for the whole column. */
static bool relational_row_open_key(
    RelationalKeyCheck *check, const TermUniverse *universe, AtomId row) {
    const CettaTermHdr *header = tu_hdr(universe, row);
    if (!header || header->tag != ATOM_EXPR)
        return false;
    for (CettaExprIndex c = 0u; c < tu_arity(universe, row); c++) {
        AtomId child = tu_child(universe, row, c);
        if (tu_kind(universe, child) != ATOM_VAR &&
            !relational_exact_key(check, universe, child))
            return false;
    }
    return true;
}

/* Parts a query may have, its own and its parts' together: each is a plan
 * prepared from the space. */
enum { RELATIONAL_OPEN_PARTS = 64u };

typedef struct {
    size_t factor;
    AtomId row;
} RelationalOpenRow;

typedef struct {
    RelationalOpenRow *rows;
    size_t len, cap;
} RelationalOpenRows;

static bool relational_open_push(RelationalOpenRows *open, size_t factor,
                                 AtomId row) {
    if (open->len == open->cap) {
        size_t cap = open->cap ? open->cap * 2u : 8u;
        RelationalOpenRow *rows = realloc(open->rows, cap * sizeof(*rows));
        if (!rows)
            return false;
        open->rows = rows;
        open->cap = cap;
    }
    open->rows[open->len++] = (RelationalOpenRow){factor, row};
    return true;
}

static size_t relational_variable(const CettaRelationalPlan *plan, VarId id) {
    for (size_t i = 0u; i < plan->info.variable_count; i++)
        if (var_id_eq(plan->variables[i], id))
            return i;
    return SIZE_MAX;
}

static size_t relational_covering_factor(const CettaRelationalPlan *plan,
    Atom *const *patterns) {
    for (size_t f = 0u; f < plan->info.factor_count; f++) {
        bool covers = true;
        for (size_t v = 0u; covers && v < plan->info.variable_count; v++) {
            bool found = false;
            for (CettaExprIndex c = 0u; c < patterns[f]->expr.len; c++) {
                const Atom *item = patterns[f]->expr.elems[c];
                if (item->kind == ATOM_VAR && var_id_eq(item->var_id, plan->variables[v])) {
                    found = true;
                    break;
                }
            }
            covers = found;
        }
        if (covers)
            return f;
    }
    return SIZE_MAX;
}

/* GYO reduction of the finite query hypergraph. A contained edge and a
 * variable occurring in at most one remaining edge can be removed. Counts
 * and occurrence identities are not changed by this structural analysis. */
static bool relational_query_class(const CettaRelationalPlan *plan,
    Atom *const *patterns, CettaRelationalQueryClass *out, size_t *workspace) {
    size_t factors = plan->info.factor_count, variables = plan->info.variable_count;
    size_t cells = 0u;
    *workspace = 0u;
    if (__builtin_mul_overflow(factors, variables, &cells) ||
        !relational_add_bytes(workspace, cells ? cells : 1u, sizeof(uint8_t)) ||
        !relational_add_bytes(workspace, factors, sizeof(uint8_t)))
        return false;
    uint8_t *edges = relational_array(cells, sizeof(*edges));
    uint8_t *active = relational_array(factors, sizeof(*active));
    if (!edges || !active) {
        free(edges);
        free(active);
        return false;
    }
    for (size_t f = 0u; f < factors; f++) {
        active[f] = 1u;
        for (CettaExprIndex c = 0u; c < patterns[f]->expr.len; c++) {
            Atom *item = patterns[f]->expr.elems[c];
            if (item->kind == ATOM_VAR)
                edges[f * variables + relational_variable(plan, item->var_id)] = 1u;
        }
    }
    bool changed;
    do {
        changed = false;
        for (size_t v = 0u; v < variables; v++) {
            size_t uses = 0u;
            for (size_t f = 0u; f < factors; f++)
                uses += active[f] && edges[f * variables + v];
            if (uses <= 1u) {
                for (size_t f = 0u; f < factors; f++) {
                    if (active[f] && edges[f * variables + v]) {
                        edges[f * variables + v] = 0u;
                        changed = true;
                    }
                }
            }
        }
        for (size_t f = 0u; f < factors; f++) {
            if (!active[f])
                continue;
            bool empty = true;
            for (size_t v = 0u; v < variables; v++)
                empty = empty && !edges[f * variables + v];
            if (empty) {
                active[f] = 0u;
                changed = true;
                continue;
            }
            for (size_t other = 0u; other < factors; other++) {
                if (f == other || !active[other])
                    continue;
                bool contained = true;
                for (size_t v = 0u; v < variables; v++)
                    contained = contained && (!edges[f * variables + v] || edges[other * variables + v]);
                if (contained) {
                    active[f] = 0u;
                    changed = true;
                    break;
                }
            }
        }
    } while (changed);
    *out = CETTA_RELATIONAL_ACYCLIC_QUERY;
    for (size_t f = 0u; f < factors; f++)
        if (active[f])
            *out = CETTA_RELATIONAL_CYCLIC_QUERY;
    free(edges);
    free(active);
    return true;
}

/* Append a tuple; false when a value does not fit a cell, which declines
 * the plan. */
static bool relational_factor_push(RelationalFactor *factor, const uint64_t *tuple) {
    for (size_t i = 0u; i < factor->width; i++) {
        if (tuple[i] > UINT32_MAX)
            return false;
    }
    if (factor->length == factor->capacity) {
        size_t capacity = factor->capacity ? factor->capacity * 2u : 16u;
        if (capacity < factor->capacity ||
            factor->width > SIZE_MAX / sizeof(*factor->tuples) ||
            capacity > SIZE_MAX / (factor->width * sizeof(*factor->tuples)))
            return false;
        uint32_t *tuples = realloc(factor->tuples, capacity * factor->width * sizeof(*tuples));
        if (!tuples)
            return false;
        factor->tuples = tuples;
        factor->capacity = capacity;
    }
    uint32_t *cells = factor->tuples + factor->length * factor->width;
    for (size_t i = 0u; i < factor->width; i++)
        cells[i] = (uint32_t)tuple[i];
    factor->length++;
    return true;
}

static int relational_tuple_compare(const uint32_t *left, const uint32_t *right, size_t width) {
    for (size_t i = 0u; i < width; i++) {
        if (left[i] != right[i])
            return left[i] < right[i] ? -1 : 1;
    }
    return 0;
}

/* The width of the tuples the sort compares, set for the sort's duration. */
static _Thread_local size_t g_relational_sort_width;

static int relational_tuple_order(const void *left, const void *right) {
    return relational_tuple_compare(left, right, g_relational_sort_width);
}

/* Sort each factor once by the common variable order and then by its
 * physical occurrence. Duplicate values remain distinct physical rows.  A
 * tuple ends with its row, so no two compare equal and any sort will do:
 * the C library's, which merges through one buffer instead of copying the
 * factor back at every pass. */
static bool relational_factor_sort(RelationalFactor *factor) {
    if (factor->length < 2u)
        return true;
    size_t saved = g_relational_sort_width;
    g_relational_sort_width = factor->width;
    qsort(factor->tuples, factor->length, factor->width * sizeof(*factor->tuples),
          relational_tuple_order);
    g_relational_sort_width = saved;
    return true;
}

static bool relational_account(CettaRelationalPlan *plan, size_t key_bytes, size_t temporary) {
    size_t bytes = sizeof(*plan);
    size_t factors = plan->info.factor_count;
    if (!relational_add_bytes(&bytes, factors, sizeof(*plan->factors)) ||
        __builtin_add_overflow(bytes, key_bytes, &bytes))
        return false;
    if (plan->variable_offsets &&
        (!relational_add_bytes(&bytes, plan->info.variable_count + 1u, sizeof(*plan->variable_offsets)) ||
         !relational_add_bytes(&bytes, plan->incidence_count, sizeof(*plan->incidences))))
        return false;
    for (size_t i = 0u; i < factors; i++) {
        const RelationalFactor *factor = &plan->factors[i];
        if (!factor->width)
            continue;
        if (!relational_add_bytes(&bytes, factor->capacity, factor->width * sizeof(uint64_t)) ||
            !relational_add_bytes(&bytes, factor->width > 1u ? factor->width - 1u : 1u, sizeof(size_t)))
            return false;
    }
    plan->info.retained_bytes = bytes;
    if (__builtin_add_overflow(bytes, temporary, &bytes))
        return false;
    if (bytes > plan->info.preparation_peak_bytes)
        plan->info.preparation_peak_bytes = bytes;
    return true;
}

/* Compile the variable-to-column incidence once. Histogram prefix offsets
 * serve as insertion positions, then shift back to immutable range starts.
 * Factor order matches schema order, preserving stable equal-key iteration. */
static bool relational_compile_incidence(CettaRelationalPlan *plan) {
    size_t variables = plan->info.variable_count;
    if (!variables)
        return true;
    if (variables == SIZE_MAX)
        return false;
    for (size_t f = 0u; f < plan->info.factor_count; f++) {
        if (__builtin_add_overflow(plan->incidence_count,
                plan->factors[f].width - 1u, &plan->incidence_count))
            return false;
    }
    plan->variable_offsets = relational_array(variables + 1u, sizeof(*plan->variable_offsets));
    plan->incidences = relational_array(plan->incidence_count, sizeof(*plan->incidences));
    if (!plan->variable_offsets || !plan->incidences)
        return false;
    for (size_t f = 0u; f < plan->info.factor_count; f++) {
        const RelationalFactor *factor = &plan->factors[f];
        for (size_t c = 0u; c + 1u < factor->width; c++)
            plan->variable_offsets[factor->variables[c] + 1u]++;
    }
    for (size_t v = 0u; v < variables; v++)
        plan->variable_offsets[v + 1u] += plan->variable_offsets[v];
    for (size_t f = 0u; f < plan->info.factor_count; f++) {
        const RelationalFactor *factor = &plan->factors[f];
        for (size_t c = 0u; c + 1u < factor->width; c++) {
            size_t position = plan->variable_offsets[factor->variables[c]]++;
            plan->incidences[position] = (RelationalIncidence){f, c};
        }
    }
    for (size_t v = variables; v > 0u; v--)
        plan->variable_offsets[v] = plan->variable_offsets[v - 1u];
    plan->variable_offsets[0] = 0u;
    return true;
}

bool cetta_relational_plan_valid_for(const CettaRelationalPlan *plan, const Space *space) {
    return plan && space && !space->overlay_base && !space->dep_count &&
        (space->match_backend.kind == SPACE_ENGINE_NATIVE ||
         space->match_backend.kind == SPACE_ENGINE_NATIVE_CANDIDATE_EXACT) &&
        space->native.universe && plan->info.space_instance == space->instance_id &&
        plan->info.space_revision == space_revision(space) &&
        plan->info.universe_instance == space->native.universe->instance_id &&
        plan->info.universe_epoch == space->native.universe->storage_epoch;
}

const CettaRelationalPlanInfo *cetta_relational_plan_info(const CettaRelationalPlan *plan) {
    return plan ? &plan->info : NULL;
}

size_t cetta_relational_plan_variable_index(const CettaRelationalPlan *plan, VarId id) {
    return plan ? relational_variable(plan, id) : SIZE_MAX;
}

size_t cetta_relational_plan_variable_count(const CettaRelationalPlan *plan) {
    return plan ? plan->info.variable_count : 0u;
}

VarId cetta_relational_plan_variable_at(const CettaRelationalPlan *plan,
                                        size_t index) {
    return plan->variables[index];
}

bool cetta_relational_patterns_cyclic(Atom *const *patterns, size_t count,
                                      bool *cyclic) {
    CettaRelationalPlan shape = {0};
    size_t capacity = 0u;
    for (size_t i = 0u; i < count; i++) {
        if (!patterns[i] || patterns[i]->kind != ATOM_EXPR)
            return false;
        capacity += patterns[i]->expr.len;
    }
    shape.variables = relational_array(capacity, sizeof(*shape.variables));
    if (!shape.variables)
        return false;
    shape.info.factor_count = count;
    for (size_t i = 0u; i < count; i++) {
        for (CettaExprIndex c = 0u; c < patterns[i]->expr.len; c++) {
            Atom *item = patterns[i]->expr.elems[c];
            if (item->kind == ATOM_VAR &&
                relational_variable(&shape, item->var_id) == SIZE_MAX)
                shape.variables[shape.info.variable_count++] = item->var_id;
        }
    }
    CettaRelationalQueryClass query_class = CETTA_RELATIONAL_ACYCLIC_QUERY;
    size_t workspace = 0u;
    bool classified = relational_query_class(&shape, patterns, &query_class,
                                             &workspace);
    free(shape.variables);
    if (classified)
        *cyclic = query_class == CETTA_RELATIONAL_CYCLIC_QUERY;
    return classified;
}

bool cetta_relational_plan_retain(const CettaRelationalPlan *plan) {
    if (!plan)
        return false;
    atomic_size_t *references = &((CettaRelationalPlan *)plan)->references;
    size_t previous = atomic_load_explicit(references, memory_order_relaxed);
    while (previous && previous != SIZE_MAX) {
        if (atomic_compare_exchange_weak_explicit(references, &previous,
                previous + 1u, memory_order_relaxed, memory_order_relaxed))
            return true;
    }
    return false;
}

void cetta_relational_plan_free(CettaRelationalPlan *plan) {
    if (!plan)
        return;
    if (atomic_fetch_sub_explicit(&plan->references, 1u, memory_order_acq_rel) != 1u)
        return;
    if (plan->factors) {
        for (size_t i = 0u; i < plan->info.factor_count; i++) {
            free(plan->factors[i].variables);
            free(plan->factors[i].tuples);
        }
    }
    free(plan->factors);
    free(plan->variables);
    free(plan->variable_offsets);
    free(plan->incidences);
    free(plan->scan_positions);
    free(plan->scan_constants);
    for (size_t i = 0u; i < plan->open_part_count; i++) {
        cetta_relational_plan_free(plan->open_parts[i].plan);
        free(plan->open_parts[i].sources);
        free(plan->open_parts[i].constants);
    }
    free(plan->open_parts);
    free(plan);
}

static bool relational_open_parts(
    CettaRelationalPlan *plan, Space *space, Arena *scratch, Atom **resolved,
    size_t pattern_count, CettaRelationalAdmission admission,
    const bool *ground_only, size_t *budget, const RelationalOpenRows *open);

/* Prepare a plan whose factors listed in `ground_only` (NULL for none)
 * read only the rows without stored variables; `budget` counts the parts
 * the whole query may still have. */
static bool relational_plan_prepare_with(
    Space *space, Arena *scratch, Atom *const *patterns, size_t pattern_count,
    const Bindings *seed, CettaRelationalAdmission admission,
    const bool *ground_only, size_t *budget, CettaRelationalPlan **out) {
    if (!space || !scratch || !patterns || !out || !pattern_count ||
        !space->native.universe || space->overlay_base || space->dep_count ||
        (space->match_backend.kind != SPACE_ENGINE_NATIVE &&
         space->match_backend.kind != SPACE_ENGINE_NATIVE_CANDIDATE_EXACT))
        return false;
    ArenaMark mark = arena_mark(scratch);
    CettaRelationalPlan *plan = calloc(1u, sizeof(*plan));
    if (plan) {
        atomic_init(&plan->references, 1u);
        /* A part, which has factors reading only some rows, keeps its
         * rows like any plan. */
        plan->rows_unsorted = admission == CETTA_RELATIONAL_ADMIT_ENUMERATION &&
            pattern_count == 1u && !ground_only;
    }
    Atom **resolved = relational_array(pattern_count, sizeof(*resolved));
    RelationalKeyCheck keys = {0};
    RelationalOpenRows open = {0};
    bool ready = plan && resolved;
    size_t variable_capacity = 0u;
    if (ready) {
        plan->info = (CettaRelationalPlanInfo){
            .space_instance = space->instance_id, .space_revision = space_revision(space),
            .universe_instance = space->native.universe->instance_id,
            .universe_epoch = space->native.universe->storage_epoch,
            .encoding = CETTA_RELATIONAL_FINITE_EXACT_TERM_KEYS,
            .algorithm = CETTA_RELATIONAL_VARIABLE_INTERSECTION,
            .observation = {{CETTA_OBSERVATION_COMPLETE_BAG, 0u},
                CETTA_OBSERVATION_ALGEBRA_PREFERRED_FALLBACK_COUNT},
            .factor_count = pattern_count,
        };
        plan->factors = relational_array(pattern_count, sizeof(*plan->factors));
        ready = plan->factors != NULL;
    }
    for (size_t i = 0u; ready && i < pattern_count; i++) {
        resolved[i] = bindings_apply_if_vars(seed, scratch, patterns[i]);
        Atom *pattern = resolved[i];
        ready = pattern && pattern->kind == ATOM_EXPR &&
            !atom_structural_may_have_list_carrier(pattern) &&
            !__builtin_add_overflow(variable_capacity, (size_t)pattern->expr.len, &variable_capacity);
    }
    if (ready) {
        plan->variables = relational_array(variable_capacity, sizeof(*plan->variables));
        ready = plan->variables != NULL;
    }
    for (size_t i = 0u; ready && i < pattern_count; i++) {
        for (CettaExprIndex c = 0u; ready && c < resolved[i]->expr.len; c++) {
            Atom *item = resolved[i]->expr.elems[c];
            if (!item) {
                ready = false;
                break;
            }
            if (item->kind == ATOM_VAR) {
                if (relational_variable(plan, item->var_id) == SIZE_MAX)
                    plan->variables[plan->info.variable_count++] = item->var_id;
            } else {
                AtomId id = term_universe_lookup_atom_id(space->native.universe, item);
                /* An absent exact rigid key makes this factor empty; its
                 * representation still needs checking independently. */
                if (id == CETTA_ATOM_ID_NONE) {
                    ready = term_universe_atom_is_stable(item) && !atom_has_vars(item) &&
                        (item->kind == ATOM_SYMBOL ||
                         (item->kind == ATOM_GROUNDED && (item->ground.gkind == GV_INT ||
                          item->ground.gkind == GV_BOOL || item->ground.gkind == GV_STRING)));
                } else {
                    ready = relational_exact_key(&keys, space->native.universe, id);
                }
            }
        }
    }
    if (ready) {
        size_t temporary = 0u;
        ready = relational_query_class(plan, resolved, &plan->info.query_class, &temporary) &&
            relational_add_bytes(&temporary, pattern_count, sizeof(*resolved)) &&
            relational_add_bytes(&temporary, keys.capacity + keys.capacity / 2u, sizeof(*keys.slots)) &&
            relational_add_bytes(&temporary, keys.stack_capacity + keys.stack_capacity / 2u, sizeof(*keys.stack)) &&
            relational_account(plan, (variable_capacity ? variable_capacity : 1u) * sizeof(*plan->variables), temporary);
    }
    if (ready) {
        /* A complete count cannot observe a factor-private binding. Keep its
         * row constraint and multiplicity, but project it out of the common
         * key. In particular, occurrence labels never force a Cartesian
         * enumeration that the count consumer cannot observe.  An
         * enumeration observes every binding and keeps them all. */
        bool enumeration = admission == CETTA_RELATIONAL_ADMIT_ENUMERATION;
        size_t kept = 0u;
        plan->info.source_variable_count = plan->info.variable_count;
        for (size_t v = 0u; v < plan->info.source_variable_count; v++) {
            VarId id = plan->variables[v];
            size_t factors = 0u;
            for (size_t f = 0u; f < pattern_count; f++) {
                for (CettaExprIndex c = 0u; c < resolved[f]->expr.len; c++) {
                    Atom *item = resolved[f]->expr.elems[c];
                    if (item->kind == ATOM_VAR && var_id_eq(item->var_id, id)) {
                        factors++;
                        break;
                    }
                }
            }
            if (factors > 1u)
                plan->variables[kept++] = id;
            else if (enumeration)
                plan->variables[kept++] = id;
        }
        plan->info.variable_count = kept;
        plan->info.covering_factor = relational_covering_factor(plan, resolved);
        ready = enumeration ||
            admission == CETTA_RELATIONAL_ADMIT_FULL_QUERY ||
            (admission == CETTA_RELATIONAL_ADMIT_CYCLIC_QUERY &&
             plan->info.query_class == CETTA_RELATIONAL_CYCLIC_QUERY) ||
            (admission == CETTA_RELATIONAL_ADMIT_FACTORIZED_COUNT &&
             (plan->info.query_class == CETTA_RELATIONAL_CYCLIC_QUERY ||
              plan->info.covering_factor != SIZE_MAX));
    }
    CettaCount rows = space_length64(space);
    for (size_t i = 0u; ready && i < pattern_count; i++) {
        RelationalFactor *factor = &plan->factors[i];
        Atom *pattern = resolved[i];
        size_t columns = 0u;
        for (size_t v = 0u; v < plan->info.variable_count; v++) {
            for (CettaExprIndex c = 0u; c < pattern->expr.len; c++) {
                if (pattern->expr.elems[c]->kind == ATOM_VAR &&
                    var_id_eq(pattern->expr.elems[c]->var_id, plan->variables[v])) {
                    columns++;
                    break;
                }
            }
        }
        factor->width = columns + 1u;
        factor->variables = relational_array(columns, sizeof(*factor->variables));
        AtomId *constants = relational_array((size_t)pattern->expr.len, sizeof(*constants));
        size_t *positions = relational_array((size_t)pattern->expr.len, sizeof(*positions));
        uint64_t *tuple = relational_array(factor->width, sizeof(*tuple));
        ready = factor->variables && constants && positions && tuple;
        size_t local = 0u;
        for (CettaExprIndex c = 0u; ready && c < pattern->expr.len; c++)
            positions[c] = SIZE_MAX;
        for (size_t v = 0u; ready && v < plan->info.variable_count; v++) {
            bool found = false;
            for (CettaExprIndex c = 0u; c < pattern->expr.len; c++) {
                if (pattern->expr.elems[c]->kind == ATOM_VAR &&
                    var_id_eq(pattern->expr.elems[c]->var_id, plan->variables[v])) {
                    positions[c] = local;
                    found = true;
                }
            }
            if (found)
                factor->variables[local++] = v;
        }
        for (CettaExprIndex c = 0u; ready && c < pattern->expr.len; c++) {
            constants[c] = pattern->expr.elems[c]->kind == ATOM_VAR ? CETTA_ATOM_ID_NONE :
                term_universe_lookup_atom_id(space->native.universe, pattern->expr.elems[c]);
        }
        for (CettaIndex row = 0u; ready && row < rows; row++) {
            plan->info.scanned_rows++;
            AtomId id = space_get_atom_id_at64(space, row);
            AtomKind kind = tu_kind(space->native.universe, id);
            /* A whole-atom variable can match every expression factor. Its
             * bindings are not a finite ground tuple in this encoding. */
            if (kind == ATOM_VAR) {
                ready = false;
                break;
            }
            /* A row stored without a record holds an object compared by
             * identity, which no factor can key: one the pattern could match
             * declines the plan.  Its head and length are read from the row
             * itself, since its children have no ids. */
            if (kind == ATOM_EXPR && !tu_hdr(space->native.universe, id)) {
                Atom *stored = term_universe_get_atom(space->native.universe, id);
                if (stored && stored->kind == ATOM_EXPR &&
                    (stored->expr.len != pattern->expr.len ||
                     (pattern->expr.len &&
                      pattern->expr.elems[0]->kind == ATOM_SYMBOL &&
                      stored->expr.elems[0]->kind == ATOM_SYMBOL &&
                      atom_symbol_current_id(stored->expr.elems[0]) !=
                          atom_symbol_current_id(pattern->expr.elems[0]))))
                    continue;
                ready = false;
                break;
            }
            if (kind != ATOM_EXPR ||
                tu_arity(space->native.universe, id) != pattern->expr.len)
                continue;
            if (pattern->expr.len && pattern->expr.elems[0]->kind == ATOM_SYMBOL) {
                AtomId head = tu_child(space->native.universe, id, 0u);
                if (tu_kind(space->native.universe, head) == ATOM_SYMBOL &&
                    tu_sym(space->native.universe, head) !=
                        atom_symbol_current_id(pattern->expr.elems[0]))
                    continue;
            }
            if (!relational_row_exact_key(&keys, space->native.universe, id)) {
                /* A row holding a stored variable for a whole column is
                 * joined on its own; any other row the factor cannot key
                 * declines the plan. */
                ready = !plan->rows_unsorted &&
                    relational_row_open_key(&keys, space->native.universe, id);
                if (ready && !(ground_only && ground_only[i]))
                    ready = relational_open_push(&open, i, id);
                continue;
            }
            for (size_t c = 0u; c < columns; c++)
                tuple[c] = CETTA_ATOM_ID_NONE;
            bool matched = true;
            for (CettaExprIndex c = 0u; matched && c < pattern->expr.len; c++) {
                AtomId value = tu_child(space->native.universe, id, c);
                if (pattern->expr.elems[c]->kind == ATOM_VAR) {
                    size_t position = positions[c];
                    if (position == SIZE_MAX) {
                        for (CettaExprIndex previous = 0u; previous < c; previous++) {
                            Atom *item = pattern->expr.elems[previous];
                            if (item->kind == ATOM_VAR &&
                                var_id_eq(item->var_id, pattern->expr.elems[c]->var_id) &&
                                tu_child(space->native.universe, id, previous) != value) {
                                matched = false;
                                break;
                            }
                        }
                    } else if (tuple[position] == CETTA_ATOM_ID_NONE)
                        tuple[position] = value;
                    else
                        matched = tuple[position] == value;
                } else {
                    matched = constants[c] == value;
                }
            }
            if (matched) {
                tuple[columns] = row;
                ready = plan->rows_unsorted ||
                    relational_factor_push(factor, tuple);
                plan->info.input_occurrences++;
            }
        }
        if (ready && plan->rows_unsorted) {
            plan->scan_len = pattern->expr.len;
            plan->scan_rows = rows;
            plan->scan_positions = relational_array((size_t)pattern->expr.len,
                                                    sizeof(*positions));
            plan->scan_constants = relational_array((size_t)pattern->expr.len,
                                                    sizeof(*constants));
            ready = plan->scan_positions && plan->scan_constants;
            for (CettaExprIndex c = 0u; ready && c < pattern->expr.len; c++) {
                plan->scan_positions[c] = positions[c];
                plan->scan_constants[c] = constants[c];
            }
        }
        size_t temporary = 0u;
        ready = ready && relational_add_bytes(&temporary, keys.capacity + keys.capacity / 2u, sizeof(*keys.slots)) &&
            relational_add_bytes(&temporary, keys.stack_capacity + keys.stack_capacity / 2u, sizeof(*keys.stack)) &&
            relational_add_bytes(&temporary, pattern_count, sizeof(*resolved)) &&
            relational_add_bytes(&temporary, pattern->expr.len ? (size_t)pattern->expr.len : 1u, sizeof(*constants) + sizeof(*positions)) &&
            relational_add_bytes(&temporary, factor->width, sizeof(*tuple)) &&
            relational_add_bytes(&temporary, factor->length + factor->capacity / 2u, factor->width * sizeof(*factor->tuples));
        if (ready)
            ready = relational_account(plan, (variable_capacity ? variable_capacity : 1u) * sizeof(*plan->variables), temporary) &&
                (plan->rows_unsorted || relational_factor_sort(factor));
        free(constants);
        free(positions);
        free(tuple);
    }
    if (ready) {
        size_t temporary = 0u;
        ready = relational_compile_incidence(plan) &&
            relational_add_bytes(&temporary, keys.capacity, sizeof(*keys.slots)) &&
            relational_add_bytes(&temporary, keys.stack_capacity, sizeof(*keys.stack)) &&
            relational_add_bytes(&temporary, pattern_count, sizeof(*resolved)) &&
            relational_account(plan, (variable_capacity ? variable_capacity : 1u) * sizeof(*plan->variables), temporary);
    }
    if (ready && open.len)
        ready = relational_open_parts(plan, space, scratch, resolved,
                                      pattern_count, admission, ground_only,
                                      budget, &open);
    free(open.rows);
    free(keys.slots);
    free(keys.stack);
    free(resolved);
    arena_reset(scratch, mark);
    if (!ready || !cetta_relational_plan_valid_for(plan, space)) {
        cetta_relational_plan_free(plan);
        return false;
    }
    *out = plan;
    return true;
}

/* The atom of plan variable `id` as a pattern holds it. */
static Atom *relational_variable_atom(Atom **resolved, size_t pattern_count,
                                      VarId id) {
    for (size_t i = 0u; i < pattern_count; i++) {
        for (CettaExprIndex c = 0u; c < resolved[i]->expr.len; c++) {
            Atom *item = resolved[i]->expr.elems[c];
            if (item->kind == ATOM_VAR && var_id_eq(item->var_id, id))
                return item;
        }
    }
    return NULL;
}

/* Each open row's part: the row, renamed apart, unified with its factor's
 * pattern, and the other patterns with those bindings joined by a plan of
 * their own.  An enumeration needs every variable's value from a part, so
 * a part leaving one unbound declines it. */
static bool relational_open_parts(
    CettaRelationalPlan *plan, Space *space, Arena *scratch, Atom **resolved,
    size_t pattern_count, CettaRelationalAdmission admission,
    const bool *ground_only, size_t *budget, const RelationalOpenRows *open) {
    /* More open rows than the query may still have parts decline before
     * any part is prepared. */
    if (open->len > *budget)
        return false;
    CETTA_FRAME_IDENTITY_SCOPE(frame_identity_scope);
    bool enumeration = admission == CETTA_RELATIONAL_ADMIT_ENUMERATION;
    /* A part joins fewer patterns, whatever their shape. */
    CettaRelationalAdmission part_admission = enumeration
        ? admission : CETTA_RELATIONAL_ADMIT_FULL_QUERY;
    size_t variables = plan->info.variable_count;
    plan->open_parts = relational_array(open->len, sizeof(*plan->open_parts));
    Atom **others = relational_array(pattern_count, sizeof(*others));
    bool *before = relational_array(pattern_count, sizeof(*before));
    bool ready = plan->open_parts && others && before;
    for (size_t k = 0u; ready && k < open->len; k++) {
        if (*budget == 0u) {
            ready = false;
            break;
        }
        (*budget)--;
        size_t i = open->rows[k].factor;
        Atom *row = term_universe_get_atom(space->native.universe,
                                           open->rows[k].row);
        Atom *fresh = row ? rename_vars(scratch, row,
            cetta_frame_identity_scope_fresh(&frame_identity_scope)) : NULL;
        if (!fresh) {
            ready = false;
            break;
        }
        Bindings bindings;
        bindings_init(&bindings);
        if (!match_atoms(resolved[i], fresh, &bindings, scratch)) {
            /* The row meets no row combination of this query. */
            bindings_free(&bindings);
            continue;
        }
        size_t others_len = 0u;
        for (size_t j = 0u; ready && j < pattern_count; j++) {
            if (j == i)
                continue;
            others[others_len] = bindings_apply(&bindings, scratch, resolved[j]);
            before[others_len] = j < i || (ground_only && ground_only[j]);
            ready = others[others_len++] != NULL;
        }
        RelationalOpenPart *part = &plan->open_parts[plan->open_part_count];
        *part = (RelationalOpenPart){0};
        if (ready && others_len)
            ready = relational_plan_prepare_with(
                space, scratch, others, others_len, NULL, part_admission,
                before, budget, &part->plan);
        if (ready)
            plan->open_part_count++;
        if (ready && enumeration) {
            part->sources = relational_array(variables, sizeof(*part->sources));
            part->constants = relational_array(variables, sizeof(*part->constants));
            ready = part->sources && part->constants;
            for (size_t v = 0u; ready && v < variables; v++) {
                Atom *variable = relational_variable_atom(
                    resolved, pattern_count, plan->variables[v]);
                Atom *value = variable
                    ? bindings_apply(&bindings, scratch, variable) : NULL;
                if (!value) {
                    ready = false;
                } else if (!atom_has_vars(value)) {
                    part->sources[v] = SIZE_MAX;
                    part->constants[v] = term_universe_lookup_atom_id(
                        space->native.universe, value);
                    ready = part->constants[v] != CETTA_ATOM_ID_NONE;
                } else {
                    part->sources[v] = value->kind == ATOM_VAR && part->plan
                        ? cetta_relational_plan_variable_index(part->plan,
                                                               value->var_id)
                        : SIZE_MAX;
                    ready = part->sources[v] != SIZE_MAX;
                }
            }
        }
        bindings_free(&bindings);
    }
    free(others);
    free(before);
    return ready;
}

bool cetta_relational_plan_prepare(
    Space *space, Arena *scratch, Atom *const *patterns, size_t pattern_count,
    const Bindings *seed, CettaRelationalAdmission admission, CettaRelationalPlan **out) {
    CETTA_SCOPED_SHARED_TRANSITION(shared_read);
    size_t budget = RELATIONAL_OPEN_PARTS;
    return relational_plan_prepare_with(space, scratch, patterns, pattern_count,
                                        seed, admission, NULL, &budget, out);
}

/* A conjunction over dense binary relations counts by popcounts. */
static bool relational_dense_count(Space *space, Arena *scratch,
    Atom *const *patterns, size_t pattern_count, const Bindings *seed,
    uint64_t *count_out) {
    enum { PATTERNS = 32u, VARIABLES = 64u };
    if (pattern_count < 2u || pattern_count > PATTERNS)
        return false;
    Atom *bound[PATTERNS];
    for (size_t p = 0u; p < pattern_count; p++) {
        bound[p] = bindings_apply_if_vars(seed, scratch, patterns[p]);
        if (!bound[p])
            return false;
    }
    CettaDenseQuery *query = NULL;
    if (!cetta_dense_query_prepare(space, scratch, bound, pattern_count,
                                   &query))
        return false;
    bool kept[VARIABLES] = {false};
    CettaDenseWork work = {0};
    uint64_t count = 0u;
    bool counted = cetta_dense_query_run(query, kept, NULL, NULL, &count,
                                         &work);
    cetta_dense_query_free(query);
    if (!counted)
        return false;
    *count_out = count;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_DENSE_RELATION_ADMISSION);
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_DENSE_RELATION_WORD,
                            work.words);
    return true;
}

bool space_native_relational_count(Space *space, Arena *scratch,
    Atom *const *patterns, size_t pattern_count, const Bindings *seed, uint64_t *count_out) {
    /* A relation kept as bits is counted by the native query's loops. */
    if (count_out && space_dense_meets_patterns(space, patterns, pattern_count))
        return cetta_native_query_count(space, scratch, patterns,
                                        pattern_count, seed, count_out);
    if (space_native_flat_chain_count(space, scratch, patterns, pattern_count, seed, count_out))
        return true;
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        const char *reference = getenv("CETTA_RELATIONAL_COUNT_REFERENCE");
        enabled = !reference || !*reference || strcmp(reference, "0") == 0 ||
            strcmp(reference, "false") == 0 || strcmp(reference, "off") == 0;
    }
    if (!enabled || !count_out)
        return false;
    if (relational_dense_count(space, scratch, patterns, pattern_count, seed,
                               count_out))
        return true;
    CettaRelationalPlan *plan = NULL;
    /* Rows the plan cannot key, such as rows holding variables inside a
     * column, may still meet the native path's index loops. */
    if (!cetta_relational_plan_prepare(space, scratch, patterns, pattern_count, seed,
            CETTA_RELATIONAL_ADMIT_FACTORIZED_COUNT, &plan))
        return cetta_native_query_count(space, scratch, patterns,
                                        pattern_count, seed, count_out);
    CettaRelationalWork work;
    bool counted = cetta_relational_plan_count(plan, space, count_out, &work);
    if (counted) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_MATCH_RELATIONAL_COUNT_ADMISSION);
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MATCH_RELATIONAL_PREPARE_ROWS, plan->info.scanned_rows);
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MATCH_RELATIONAL_KEY_SEEKS, work.key_seeks);
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MATCH_RELATIONAL_PREFIXES, work.candidate_prefixes);
    }
    cetta_relational_plan_free(plan);
    return counted;
}
