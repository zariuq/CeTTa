#include "relational_plan_internal.h"
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

/* A memoized, explicit-stack check of the coordinate equality fragment.
 * No term decoding is needed and shared children are inspected once. States
 * 1/2/3 mean discovered, active and checked; a cycle cannot pass as a tree. */
static bool relational_exact_key(
    RelationalKeyCheck *check, const TermUniverse *universe, AtomId root) {
    size_t depth = 0u;
    AtomId next = root;
    for (;;) {
        RelationalKeyState *state = relational_key_state(check, next);
        const CettaTermHdr *header = tu_hdr(universe, next);
        if (!state || !header || state->state == 2u)
            return false;
        if (state->state != 3u) {
            if (header->tag == ATOM_SYMBOL ||
                (header->tag == ATOM_GROUNDED &&
                 (header->subtag == GV_INT || header->subtag == GV_BOOL ||
                  header->subtag == GV_STRING))) {
                state->state = 3u;
            } else if (header->tag == ATOM_EXPR && !tu_has_vars(universe, next)) {
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

static size_t relational_variable(const CettaRelationalPlan *plan, VarId id) {
    for (size_t i = 0u; i < plan->info.variable_count; i++)
        if (plan->variables[i] == id)
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
                if (item->kind == ATOM_VAR && item->var_id == plan->variables[v]) {
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

static bool relational_factor_push(RelationalFactor *factor, const uint64_t *tuple) {
    if (factor->length == factor->capacity) {
        size_t capacity = factor->capacity ? factor->capacity * 2u : 16u;
        if (capacity < factor->capacity ||
            factor->width > SIZE_MAX / sizeof(uint64_t) ||
            capacity > SIZE_MAX / (factor->width * sizeof(uint64_t)))
            return false;
        uint64_t *tuples = realloc(factor->tuples, capacity * factor->width * sizeof(*tuples));
        if (!tuples)
            return false;
        factor->tuples = tuples;
        factor->capacity = capacity;
    }
    memcpy(factor->tuples + factor->length * factor->width, tuple,
           factor->width * sizeof(*tuple));
    factor->length++;
    return true;
}

static int relational_tuple_compare(const uint64_t *left, const uint64_t *right, size_t width) {
    for (size_t i = 0u; i < width; i++) {
        if (left[i] != right[i])
            return left[i] < right[i] ? -1 : 1;
    }
    return 0;
}

/* Sort each factor once by the common variable order and then by its
 * physical occurrence. Duplicate values remain distinct physical rows. */
static bool relational_factor_sort(RelationalFactor *factor) {
    if (factor->length < 2u)
        return true;
    size_t row_bytes = factor->width * sizeof(uint64_t);
    uint64_t *scratch = relational_array(factor->length, row_bytes);
    if (!scratch)
        return false;
    for (size_t run = 1u; run < factor->length;) {
        for (size_t begin = 0u; begin < factor->length;) {
            size_t middle = begin + (factor->length - begin < run ? factor->length - begin : run);
            size_t end = middle + (factor->length - middle < run ? factor->length - middle : run);
            size_t left = begin, right = middle;
            for (size_t dst = begin; dst < end; dst++) {
                size_t src = right == end ||
                    (left < middle && relational_tuple_compare(factor->tuples + left * factor->width,
                        factor->tuples + right * factor->width, factor->width) <= 0)
                    ? left++ : right++;
                memcpy(scratch + dst * factor->width, factor->tuples + src * factor->width, row_bytes);
            }
            begin = end;
        }
        memcpy(factor->tuples, scratch, factor->length * row_bytes);
        if (run > factor->length / 2u)
            break;
        run *= 2u;
    }
    free(scratch);
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
    free(plan);
}

bool cetta_relational_plan_prepare(
    Space *space, Arena *scratch, Atom *const *patterns, size_t pattern_count,
    const Bindings *seed, CettaRelationalAdmission admission, CettaRelationalPlan **out) {
    CETTA_SCOPED_SHARED_TRANSITION(shared_read);
    if (!space || !scratch || !patterns || !out || !pattern_count ||
        !space->native.universe || space->overlay_base || space->dep_count ||
        (space->match_backend.kind != SPACE_ENGINE_NATIVE &&
         space->match_backend.kind != SPACE_ENGINE_NATIVE_CANDIDATE_EXACT))
        return false;
    ArenaMark mark = arena_mark(scratch);
    CettaRelationalPlan *plan = calloc(1u, sizeof(*plan));
    if (plan)
        atomic_init(&plan->references, 1u);
    Atom **resolved = relational_array(pattern_count, sizeof(*resolved));
    RelationalKeyCheck keys = {0};
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
         * enumeration that the count consumer cannot observe. */
        size_t kept = 0u;
        plan->info.source_variable_count = plan->info.variable_count;
        for (size_t v = 0u; v < plan->info.source_variable_count; v++) {
            VarId id = plan->variables[v];
            size_t factors = 0u;
            for (size_t f = 0u; f < pattern_count; f++) {
                for (CettaExprIndex c = 0u; c < resolved[f]->expr.len; c++) {
                    Atom *item = resolved[f]->expr.elems[c];
                    if (item->kind == ATOM_VAR && item->var_id == id) {
                        factors++;
                        break;
                    }
                }
            }
            if (factors > 1u)
                plan->variables[kept++] = id;
        }
        plan->info.variable_count = kept;
        plan->info.covering_factor = relational_covering_factor(plan, resolved);
        ready = admission == CETTA_RELATIONAL_ADMIT_FULL_QUERY ||
            (admission == CETTA_RELATIONAL_ADMIT_CYCLIC_QUERY &&
             plan->info.query_class == CETTA_RELATIONAL_CYCLIC_QUERY) ||
            (admission == CETTA_RELATIONAL_ADMIT_FACTORIZED_COUNT &&
             (plan->info.query_class == CETTA_RELATIONAL_CYCLIC_QUERY ||
              plan->info.covering_factor != SIZE_MAX));
    }
    CettaCount rows = space->native.len;
    for (size_t i = 0u; ready && i < pattern_count; i++) {
        RelationalFactor *factor = &plan->factors[i];
        Atom *pattern = resolved[i];
        size_t columns = 0u;
        for (size_t v = 0u; v < plan->info.variable_count; v++) {
            for (CettaExprIndex c = 0u; c < pattern->expr.len; c++) {
                if (pattern->expr.elems[c]->kind == ATOM_VAR &&
                    pattern->expr.elems[c]->var_id == plan->variables[v]) {
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
                    pattern->expr.elems[c]->var_id == plan->variables[v]) {
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
            if (!relational_exact_key(&keys, space->native.universe, id)) {
                ready = false;
                break;
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
                                item->var_id == pattern->expr.elems[c]->var_id &&
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
                ready = relational_factor_push(factor, tuple);
                plan->info.input_occurrences++;
            }
        }
        size_t temporary = 0u;
        ready = ready && relational_add_bytes(&temporary, keys.capacity + keys.capacity / 2u, sizeof(*keys.slots)) &&
            relational_add_bytes(&temporary, keys.stack_capacity + keys.stack_capacity / 2u, sizeof(*keys.stack)) &&
            relational_add_bytes(&temporary, pattern_count, sizeof(*resolved)) &&
            relational_add_bytes(&temporary, pattern->expr.len ? (size_t)pattern->expr.len : 1u, sizeof(*constants) + sizeof(*positions)) &&
            relational_add_bytes(&temporary, factor->width, sizeof(*tuple)) &&
            relational_add_bytes(&temporary, factor->length + factor->capacity / 2u, factor->width * sizeof(*tuple));
        if (ready)
            ready = relational_account(plan, (variable_capacity ? variable_capacity : 1u) * sizeof(*plan->variables), temporary) &&
                relational_factor_sort(factor);
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

bool space_native_relational_count(Space *space, Arena *scratch,
    Atom *const *patterns, size_t pattern_count, const Bindings *seed, uint64_t *count_out) {
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
    CettaRelationalPlan *plan = NULL;
    if (!cetta_relational_plan_prepare(space, scratch, patterns, pattern_count, seed,
            CETTA_RELATIONAL_ADMIT_FACTORIZED_COUNT, &plan))
        return false;
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
