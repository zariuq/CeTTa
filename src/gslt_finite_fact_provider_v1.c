#include "gslt_finite_fact_provider_v1.h"

#include "match.h"

#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    CETTA_GSLT_FACT_ROOT_SYMBOL_V1 = 1,
    CETTA_GSLT_FACT_ROOT_APPLICATION_V1 = 2,
};

typedef struct {
    uint8_t kind;
    SymbolId symbol;
    CettaExprLen arity;
} CettaGsltFiniteFactRootKeyV1;

typedef struct {
    CettaGsltFiniteFactRootKeyV1 key;
    Atom **rows;
    size_t row_count;
} CettaGsltFiniteFactGroupV1;

typedef struct {
    CettaGsltFiniteFactRootKeyV1 key;
    size_t group_plus_one;
} CettaGsltFiniteFactGroupSlotV1;

typedef struct {
    const CettaGsltProviderRequirementV1 *requirement;
    Atom **rows;
    size_t row_count;
    uint32_t index_argument;
    CettaGsltFiniteFactGroupV1 *groups;
    size_t group_count;
    Atom **indexed_rows;
    CettaGsltFiniteFactGroupSlotV1 *group_slots;
    size_t group_slot_count;
    _Atomic uint64_t queries;
    _Atomic uint64_t indexed_queries;
    _Atomic uint64_t rows_considered;
    _Atomic uint64_t rows_skipped;
} CettaGsltFiniteFactRelationV1;

struct CettaGsltFiniteFactProviderSetV1 {
    CettaGsltProviderRegistryV1 registry;
    CettaGsltProviderV1 *providers;
    CettaGsltFiniteFactRelationV1 *relations;
    Atom **rows;
    size_t row_count;
    size_t indexed_relations;
};

static void finite_fact_atomic_add_sat_v1(
    _Atomic uint64_t *counter, uint64_t amount) {
    uint64_t current = atomic_load_explicit(counter, memory_order_relaxed);
    while (current != UINT64_MAX) {
        uint64_t next = amount > UINT64_MAX - current
            ? UINT64_MAX : current + amount;
        if (atomic_compare_exchange_weak_explicit(
                counter, &current, next,
                memory_order_relaxed, memory_order_relaxed))
            return;
    }
}

static uint64_t finite_fact_add_sat_v1(uint64_t left, uint64_t right) {
    return right > UINT64_MAX - left ? UINT64_MAX : left + right;
}

static bool finite_fact_root_key_v1(
    const Atom *atom, CettaGsltFiniteFactRootKeyV1 *key) {
    if (!atom || !key)
        return false;
    if (atom->kind == ATOM_SYMBOL) {
        *key = (CettaGsltFiniteFactRootKeyV1){
            .kind = CETTA_GSLT_FACT_ROOT_SYMBOL_V1,
            .symbol = atom->sym_id,
            .arity = 0u,
        };
        return true;
    }
    if (atom->kind == ATOM_EXPR && atom->expr.len > 0u &&
        atom->expr.elems && atom->expr.elems[0] &&
        atom->expr.elems[0]->kind == ATOM_SYMBOL) {
        *key = (CettaGsltFiniteFactRootKeyV1){
            .kind = CETTA_GSLT_FACT_ROOT_APPLICATION_V1,
            .symbol = atom->expr.elems[0]->sym_id,
            .arity = atom->expr.len - 1u,
        };
        return true;
    }
    return false;
}

static bool finite_fact_root_key_equal_v1(
    const CettaGsltFiniteFactRootKeyV1 *left,
    const CettaGsltFiniteFactRootKeyV1 *right) {
    return left && right && left->kind == right->kind &&
        left->symbol == right->symbol && left->arity == right->arity;
}

static uint64_t finite_fact_root_key_hash_v1(
    const CettaGsltFiniteFactRootKeyV1 *key) {
    uint64_t value = ((uint64_t)key->symbol << 32u) ^
        (uint64_t)key->arity ^ ((uint64_t)key->kind << 56u);
    value ^= value >> 30u;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27u;
    value *= UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31u);
}

static size_t finite_fact_slot_capacity_v1(size_t row_count) {
    if (row_count == 0u || row_count > SIZE_MAX / 2u)
        return 0u;
    size_t required = row_count * 2u;
    size_t capacity = 1u;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2u)
            return 0u;
        capacity *= 2u;
    }
    return capacity;
}

static size_t finite_fact_group_slot_v1(
    const CettaGsltFiniteFactGroupSlotV1 *slots, size_t slot_count,
    const CettaGsltFiniteFactRootKeyV1 *key) {
    size_t slot = (size_t)finite_fact_root_key_hash_v1(key) &
        (slot_count - 1u);
    while (slots[slot].group_plus_one != 0u &&
           !finite_fact_root_key_equal_v1(&slots[slot].key, key))
        slot = (slot + 1u) & (slot_count - 1u);
    return slot;
}

static size_t finite_fact_distinct_roots_v1(
    Atom *const *rows, size_t row_count, uint32_t argument) {
    size_t slot_count = finite_fact_slot_capacity_v1(row_count);
    if (!slot_count)
        return 0u;
    CettaGsltFiniteFactGroupSlotV1 *slots =
        calloc(slot_count, sizeof(*slots));
    if (!slots)
        return 0u;
    size_t distinct = 0u;
    for (size_t row = 0u; row < row_count; row++) {
        CettaGsltFiniteFactRootKeyV1 key;
        if (!rows[row] || rows[row]->kind != ATOM_EXPR ||
            !rows[row]->expr.elems ||
            argument + 1u >= rows[row]->expr.len ||
            !finite_fact_root_key_v1(
                rows[row]->expr.elems[argument + 1u], &key)) {
            distinct = 0u;
            break;
        }
        size_t slot = finite_fact_group_slot_v1(slots, slot_count, &key);
        if (slots[slot].group_plus_one == 0u) {
            slots[slot].key = key;
            slots[slot].group_plus_one = ++distinct;
        }
    }
    free(slots);
    return distinct;
}

static bool finite_fact_build_index_v1(
    CettaGsltFiniteFactRelationV1 *relation) {
    if (!relation || !relation->requirement || relation->row_count < 2u ||
        relation->requirement->arity == 0u)
        return true;
    size_t best_distinct = 1u;
    uint32_t best_argument = 0u;
    for (uint32_t argument = 0u;
         argument < relation->requirement->arity; argument++) {
        size_t distinct = finite_fact_distinct_roots_v1(
            relation->rows, relation->row_count, argument);
        if (distinct > best_distinct) {
            best_distinct = distinct;
            best_argument = argument;
        }
    }
    if (best_distinct <= 1u)
        return true;
    size_t slot_count = finite_fact_slot_capacity_v1(relation->row_count);
    if (!slot_count || best_distinct > SIZE_MAX / sizeof(*relation->groups) ||
        relation->row_count > SIZE_MAX / sizeof(*relation->indexed_rows) ||
        slot_count > SIZE_MAX / sizeof(*relation->group_slots))
        return false;
    relation->groups = calloc(best_distinct, sizeof(*relation->groups));
    relation->indexed_rows =
        malloc(relation->row_count * sizeof(*relation->indexed_rows));
    relation->group_slots = calloc(
        slot_count, sizeof(*relation->group_slots));
    size_t *row_groups =
        malloc(relation->row_count * sizeof(*row_groups));
    if (!relation->groups || !relation->indexed_rows ||
        !relation->group_slots || !row_groups) {
        free(row_groups);
        return false;
    }
    relation->group_slot_count = slot_count;
    relation->index_argument = best_argument;
    for (size_t row = 0u; row < relation->row_count; row++) {
        CettaGsltFiniteFactRootKeyV1 key;
        if (!finite_fact_root_key_v1(
                relation->rows[row]->expr.elems[best_argument + 1u], &key)) {
            free(row_groups);
            return false;
        }
        size_t slot = finite_fact_group_slot_v1(
            relation->group_slots, slot_count, &key);
        size_t group;
        if (relation->group_slots[slot].group_plus_one == 0u) {
            group = relation->group_count++;
            if (group >= best_distinct) {
                free(row_groups);
                return false;
            }
            relation->group_slots[slot].key = key;
            relation->group_slots[slot].group_plus_one = group + 1u;
            relation->groups[group].key = key;
        } else {
            group = relation->group_slots[slot].group_plus_one - 1u;
        }
        row_groups[row] = group;
        relation->groups[group].row_count++;
    }
    size_t offset = 0u;
    for (size_t group = 0u; group < relation->group_count; group++) {
        size_t count = relation->groups[group].row_count;
        relation->groups[group].rows = relation->indexed_rows + offset;
        relation->groups[group].row_count = 0u;
        offset += count;
    }
    for (size_t row = 0u; row < relation->row_count; row++) {
        CettaGsltFiniteFactGroupV1 *group =
            &relation->groups[row_groups[row]];
        group->rows[group->row_count++] = relation->rows[row];
    }
    free(row_groups);
    return true;
}

static const CettaGsltFiniteFactGroupV1 *finite_fact_find_group_v1(
    const CettaGsltFiniteFactRelationV1 *relation,
    const CettaGsltFiniteFactRootKeyV1 *key) {
    if (!relation || !key || !relation->group_slots ||
        relation->group_slot_count == 0u)
        return NULL;
    size_t slot = finite_fact_group_slot_v1(
        relation->group_slots, relation->group_slot_count, key);
    size_t group_plus_one = relation->group_slots[slot].group_plus_one;
    return group_plus_one == 0u
        ? NULL : &relation->groups[group_plus_one - 1u];
}

static bool finite_fact_error_v1(
    char *error, size_t error_size, const char *format, ...) {
    if (error && error_size > 0u) {
        va_list arguments;
        va_start(arguments, format);
        (void)vsnprintf(error, error_size, format, arguments);
        va_end(arguments);
    }
    return false;
}

static bool finite_fact_shape_v1(
    const Atom *row, const CettaGsltProviderRequirementV1 *requirement) {
    return row && requirement && row->kind == ATOM_EXPR &&
        (uint64_t)row->expr.len == (uint64_t)requirement->arity + 1u &&
        row->expr.elems[0] && row->expr.elems[0]->kind == ATOM_SYMBOL &&
        strcmp(atom_name_cstr(row->expr.elems[0]),
               requirement->relation) == 0;
}

static size_t finite_fact_requirement_v1(
    const CettaGsltProviderRequirementV1 *requirements,
    size_t requirement_count,
    const Atom *row) {
    if (!row || row->kind != ATOM_EXPR || row->expr.len == 0u ||
        !row->expr.elems[0] ||
        row->expr.elems[0]->kind != ATOM_SYMBOL)
        return SIZE_MAX;
    for (size_t index = 0u; index < requirement_count; index++)
        if (finite_fact_shape_v1(row, &requirements[index]))
            return index;
    return SIZE_MAX;
}

static CettaGsltProviderOutcomeV1 finite_fact_query_v1(
    void *raw_context,
    Arena *answer_arena,
    const Atom *goal,
    uint64_t answer_limit,
    CettaGsltProviderAnswersV1 *answers,
    char *error,
    size_t error_size) {
    CettaGsltFiniteFactRelationV1 *relation = raw_context;
    if (!relation || !relation->requirement || !answer_arena || !answers ||
        !finite_fact_shape_v1(goal, relation->requirement)) {
        finite_fact_error_v1(
            error, error_size, "invalid finite fact-provider query");
        return CETTA_GSLT_PROVIDER_FAULT;
    }
    Atom *const *rows = relation->rows;
    size_t row_count = relation->row_count;
    bool indexed = false;
    if (relation->group_slots && goal->kind == ATOM_EXPR &&
        goal->expr.elems &&
        relation->index_argument + 1u < goal->expr.len) {
        CettaGsltFiniteFactRootKeyV1 key;
        if (finite_fact_root_key_v1(
                goal->expr.elems[relation->index_argument + 1u], &key)) {
            const CettaGsltFiniteFactGroupV1 *group =
                finite_fact_find_group_v1(relation, &key);
            indexed = true;
            rows = group ? group->rows : NULL;
            row_count = group ? group->row_count : 0u;
        }
    }
    finite_fact_atomic_add_sat_v1(&relation->queries, 1u);
    finite_fact_atomic_add_sat_v1(
        &relation->rows_considered, (uint64_t)row_count);
    if (indexed) {
        finite_fact_atomic_add_sat_v1(&relation->indexed_queries, 1u);
        finite_fact_atomic_add_sat_v1(
            &relation->rows_skipped,
            (uint64_t)(relation->row_count - row_count));
    }
    for (size_t index = 0u; index < row_count; index++) {
        Bindings bindings;
        bindings_init(&bindings);
        bool matched = match_atoms(
            (Atom *)goal, rows[index], &bindings);
        bindings_free(&bindings);
        if (!matched)
            continue;
        Atom *answer = atom_deep_copy(answer_arena, rows[index]);
        if (!answer ||
            !cetta_gslt_provider_answers_push_v1(answers, answer)) {
            cetta_gslt_provider_answers_free_v1(answers);
            finite_fact_error_v1(
                error, error_size,
                "finite fact-provider could not allocate its frontier");
            return CETTA_GSLT_PROVIDER_FAULT;
        }
        if ((uint64_t)answers->answer_count > answer_limit) {
            cetta_gslt_provider_answers_free_v1(answers);
            return CETTA_GSLT_PROVIDER_ANSWER_LIMIT;
        }
    }
    return CETTA_GSLT_PROVIDER_COMPLETED;
}

CettaGsltFiniteFactProviderSetV1 *
cetta_gslt_finite_fact_provider_set_create_borrowed_v1(
    const CettaGsltProviderRequirementV1 *requirements,
    size_t requirement_count,
    const CettaGsltFiniteFactSpanV1 *spans,
    size_t span_count,
    char *error,
    size_t error_size) {
    if ((requirement_count > 0u && !requirements) ||
        (span_count > 0u && !spans)) {
        finite_fact_error_v1(
            error, error_size, "finite fact-provider inventory is absent");
        return NULL;
    }
    for (size_t index = 0u; index < requirement_count; index++) {
        if (!requirements[index].relation ||
            requirements[index].relation[0] == '\0' ||
            !requirements[index].semantic_id ||
            requirements[index].semantic_id[0] == '\0') {
            finite_fact_error_v1(
                error, error_size,
                "finite fact-provider requirement %zu is incomplete", index);
            return NULL;
        }
        for (size_t prior = 0u; prior < index; prior++) {
            if ((requirements[prior].arity == requirements[index].arity &&
                 strcmp(requirements[prior].relation,
                        requirements[index].relation) == 0) ||
                strcmp(requirements[prior].semantic_id,
                       requirements[index].semantic_id) == 0) {
                finite_fact_error_v1(
                    error, error_size,
                    "finite fact-provider requirement %zu is duplicated",
                    index);
                return NULL;
            }
        }
    }
    size_t row_count = 0u;
    for (size_t span = 0u; span < span_count; span++) {
        if (spans[span].row_count > 0u && !spans[span].rows) {
            finite_fact_error_v1(
                error, error_size, "finite fact-provider span is absent");
            return NULL;
        }
        if (spans[span].row_count > SIZE_MAX - row_count) {
            finite_fact_error_v1(
                error, error_size, "finite fact-provider row bag is too large");
            return NULL;
        }
        row_count += spans[span].row_count;
    }
    if (requirement_count > SIZE_MAX / sizeof(CettaGsltProviderV1) ||
        requirement_count > SIZE_MAX /
            sizeof(CettaGsltFiniteFactRelationV1) ||
        row_count > SIZE_MAX / sizeof(Atom *)) {
        finite_fact_error_v1(
            error, error_size, "finite fact-provider index is too large");
        return NULL;
    }

    CettaGsltFiniteFactProviderSetV1 *set = calloc(1u, sizeof(*set));
    if (!set) {
        finite_fact_error_v1(
            error, error_size, "cannot allocate finite fact-provider set");
        return NULL;
    }
    set->providers = requirement_count
        ? calloc(requirement_count, sizeof(*set->providers)) : NULL;
    set->relations = requirement_count
        ? calloc(requirement_count, sizeof(*set->relations)) : NULL;
    set->rows = row_count ? calloc(row_count, sizeof(*set->rows)) : NULL;
    size_t *counts = requirement_count
        ? calloc(requirement_count, sizeof(*counts)) : NULL;
    size_t *cursors = requirement_count
        ? calloc(requirement_count, sizeof(*cursors)) : NULL;
    if ((requirement_count > 0u &&
         (!set->providers || !set->relations || !counts || !cursors)) ||
        (row_count > 0u && !set->rows)) {
        finite_fact_error_v1(
            error, error_size, "cannot allocate finite fact-provider index");
        free(cursors);
        free(counts);
        cetta_gslt_finite_fact_provider_set_free_v1(set);
        return NULL;
    }

    bool valid = true;
    for (size_t span = 0u; valid && span < span_count; span++) {
        for (size_t row = 0u; valid && row < spans[span].row_count; row++) {
            Atom *fact = spans[span].rows[row];
            size_t requirement = finite_fact_requirement_v1(
                requirements, requirement_count, fact);
            if (requirement == SIZE_MAX || atom_has_vars(fact)) {
                valid = finite_fact_error_v1(
                    error, error_size,
                    "finite fact-provider row %zu:%zu is open or undeclared",
                    span, row);
                break;
            }
            counts[requirement]++;
        }
    }
    size_t offset = 0u;
    for (size_t index = 0u; valid && index < requirement_count; index++) {
        const CettaGsltProviderRequirementV1 *requirement =
            &requirements[index];
        set->relations[index] = (CettaGsltFiniteFactRelationV1){
            .requirement = requirement,
            .rows = set->rows ? set->rows + offset : NULL,
            .row_count = counts[index],
        };
        set->providers[index] = (CettaGsltProviderV1){
            .relation = requirement->relation,
            .arity = requirement->arity,
            .semantic_id = requirement->semantic_id,
            .context = &set->relations[index],
            .query = finite_fact_query_v1,
        };
        cursors[index] = offset;
        offset += counts[index];
    }
    for (size_t span = 0u; valid && span < span_count; span++) {
        for (size_t row = 0u; row < spans[span].row_count; row++) {
            Atom *fact = spans[span].rows[row];
            size_t requirement = finite_fact_requirement_v1(
                requirements, requirement_count, fact);
            if (requirement == SIZE_MAX) {
                valid = false;
                break;
            }
            set->rows[cursors[requirement]++] = fact;
        }
    }
    set->row_count = row_count;
    set->registry = (CettaGsltProviderRegistryV1){
        .providers = set->providers,
        .provider_count = requirement_count,
    };
    if (valid)
        valid = cetta_gslt_provider_registry_validate_v1(
            &set->registry, error, error_size);
    for (size_t index = 0u; valid && index < requirement_count; index++) {
        if (!finite_fact_build_index_v1(&set->relations[index])) {
            valid = finite_fact_error_v1(
                error, error_size,
                "cannot build finite fact-provider relation index");
        } else if (set->relations[index].group_count > 0u) {
            set->indexed_relations++;
        }
    }
    free(cursors);
    free(counts);
    if (!valid) {
        cetta_gslt_finite_fact_provider_set_free_v1(set);
        return NULL;
    }
    return set;
}

void cetta_gslt_finite_fact_provider_set_free_v1(
    CettaGsltFiniteFactProviderSetV1 *set) {
    if (!set)
        return;
    for (size_t index = 0u;
         index < set->registry.provider_count; index++) {
        free(set->relations[index].group_slots);
        free(set->relations[index].indexed_rows);
        free(set->relations[index].groups);
    }
    free(set->rows);
    free(set->relations);
    free(set->providers);
    free(set);
}

const CettaGsltProviderRegistryV1 *
cetta_gslt_finite_fact_provider_set_registry_v1(
    const CettaGsltFiniteFactProviderSetV1 *set) {
    return set ? &set->registry : NULL;
}

size_t cetta_gslt_finite_fact_provider_set_row_count_v1(
    const CettaGsltFiniteFactProviderSetV1 *set) {
    return set ? set->row_count : 0u;
}

size_t cetta_gslt_finite_fact_provider_set_relation_count_v1(
    const CettaGsltFiniteFactProviderSetV1 *set) {
    return set ? set->registry.provider_count : 0u;
}

bool cetta_gslt_finite_fact_provider_set_relation_view_v1(
    const CettaGsltFiniteFactProviderSetV1 *set,
    size_t relation_index,
    CettaGsltFiniteFactRelationViewV1 *view) {
    if (!view)
        return false;
    *view = (CettaGsltFiniteFactRelationViewV1){0};
    if (!set || relation_index >= set->registry.provider_count)
        return false;
    const CettaGsltFiniteFactRelationV1 *relation =
        &set->relations[relation_index];
    *view = (CettaGsltFiniteFactRelationViewV1){
        .requirement = relation->requirement,
        .rows = relation->rows,
        .row_count = relation->row_count,
    };
    return true;
}

void cetta_gslt_finite_fact_provider_set_stats_v1(
    const CettaGsltFiniteFactProviderSetV1 *set,
    CettaGsltFiniteFactProviderStatsV1 *stats) {
    if (!stats)
        return;
    *stats = (CettaGsltFiniteFactProviderStatsV1){0};
    if (!set)
        return;
    stats->indexed_relations = set->indexed_relations;
    for (size_t index = 0u;
         index < set->registry.provider_count; index++) {
        const CettaGsltFiniteFactRelationV1 *relation =
            &set->relations[index];
        uint64_t queries = atomic_load_explicit(
            &relation->queries, memory_order_relaxed);
        uint64_t indexed_queries = atomic_load_explicit(
            &relation->indexed_queries, memory_order_relaxed);
        uint64_t rows_considered = atomic_load_explicit(
            &relation->rows_considered, memory_order_relaxed);
        uint64_t rows_skipped = atomic_load_explicit(
            &relation->rows_skipped, memory_order_relaxed);
        stats->queries = finite_fact_add_sat_v1(stats->queries, queries);
        stats->indexed_queries = finite_fact_add_sat_v1(
            stats->indexed_queries, indexed_queries);
        stats->rows_considered = finite_fact_add_sat_v1(
            stats->rows_considered, rows_considered);
        stats->rows_skipped = finite_fact_add_sat_v1(
            stats->rows_skipped, rows_skipped);
    }
}
