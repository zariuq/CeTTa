#include "match_decision.h"
#include "stats.h"
#include "generated/match_decision_policy_v1.generated.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

enum {
    CETTA_MATCH_DECISION_DEFAULT_MAX_DEPTH = 7u,
    CETTA_MATCH_DECISION_HARD_MAX_DEPTH = 32u,
    CETTA_MATCH_DECISION_MAX_PATHS = 4096u,
    CETTA_MATCH_DECISION_KEY_INDEX_MIN_KEYS = 8u,
    CETTA_MATCH_DECISION_KEY_INDEX_MIN_CAPACITY = 16u,
    CETTA_MATCH_DECISION_CONJUNCTIVE_MAX_MASK_BYTES = 64u * 1024u * 1024u,
    CETTA_MATCH_DECISION_MAX_EQUALITIES = 4096u,
};

typedef enum {
    CETTA_MATCH_DECISION_KEY_LITERAL = 1,
    CETTA_MATCH_DECISION_KEY_EXPR_ARITY = 2,
    CETTA_MATCH_DECISION_KEY_EXPR_HEAD = 3,
} CettaMatchDecisionKeyKind;

_Static_assert(CETTA_MATCH_DECISION_KEY_LITERAL == 1,
               "generated policy key ABI");
_Static_assert(CETTA_MATCH_DECISION_KEY_EXPR_ARITY == 2,
               "generated policy key ABI");
_Static_assert(CETTA_MATCH_DECISION_KEY_EXPR_HEAD == 3,
               "generated policy key ABI");

uint64_t cetta_match_decision_compiler_identity(void) {
    return CETTA_MATCH_DECISION_POLICY_ID;
}

typedef struct {
    CettaMatchDecisionKeyKind kind;
    CettaExprLen expression_length;
    Atom *atom;
    uint32_t *clause_refs;
    uint32_t clause_count;
    uint32_t clause_capacity;
} CettaMatchDecisionKey;

typedef struct {
    CettaExprIndex *path;
    uint32_t path_len;
    uint32_t observation_node;
    CettaMatchDecisionKey *keys;
    uint32_t key_count;
    uint32_t key_capacity;
    uint32_t *key_slots;
    size_t key_slot_capacity;
    uint32_t *wildcard_refs;
    uint32_t wildcard_count;
    uint32_t wildcard_capacity;
    uint64_t *wildcard_bits;
} CettaMatchDecisionPath;

/* One immutable node in the union of every demanded path prefix.  Parent
 * indices are strictly smaller than child indices, so a private selection
 * cursor can fill demanded prefixes without recursion. */
typedef struct {
    uint32_t parent;
    CettaExprIndex edge;
} CettaMatchDecisionObservationNode;

typedef struct {
    const uint32_t *refs;
    uint32_t count;
    uint32_t cursor;
} CettaMatchDecisionRefList;

/* One source-local repeated-variable equality.  Both paths begin at the
 * complete call and remain owned by the compiled, revision-pinned decision. */
typedef struct {
    CettaExprIndex *paths;
    uint32_t first_len;
    uint32_t second_len;
    uint32_t first_observation_node;
    uint32_t second_observation_node;
} CettaMatchDecisionEquality;

typedef struct {
    VarId id;
    CettaExprIndex *path;
    uint32_t path_len;
} CettaMatchDecisionFirstVariable;

struct CettaMatchDecision {
    _Atomic size_t owner_count;
    SpaceReadToken read;
    CettaMatchDecisionSemanticIdentity semantic_identity;
    CettaMatchDecisionMode mode;
    uint32_t max_depth;
    CettaMatchDecisionRealization realization;
    CettaMatchDecisionClause *clauses;
    size_t clause_count;
    CettaMatchDecisionPath *paths;
    size_t path_count;
    size_t path_capacity;
    uint32_t *candidate_locals;
    size_t candidate_local_capacity;
    uint32_t *candidate_sources;
    size_t candidate_source_capacity;
    CettaMatchDecisionRefList *working_lists;
    size_t working_list_capacity;
    uint64_t *candidate_bits;
    uint64_t *path_bits;
    size_t bit_word_count;
    CettaMatchDecisionEquality *equalities;
    size_t equality_count;
    size_t equality_capacity;
    size_t *equality_offsets;
    CettaMatchDecisionObservationNode *observation_nodes;
    size_t observation_node_count;
    size_t observation_selector_node_count;
    unsigned char *observation_states;
    Atom **observation_values;
    uint64_t *observation_stamps;
    uint64_t observation_epoch;
    bool observation_ready;
    CettaMatchDecisionStats stats;
};

static bool match_decision_reserve(
    void **items, size_t *capacity, size_t needed, size_t width) {
    if (needed <= *capacity)
        return true;
    if (!items || !capacity || width == 0u || needed > SIZE_MAX / width)
        return false;
    size_t next = *capacity ? *capacity : 8u;
    while (next < needed) {
        if (next > SIZE_MAX / 2u) {
            next = needed;
            break;
        }
        next *= 2u;
    }
    if (next > SIZE_MAX / width)
        return false;
    void *grown = realloc(*items, next * width);
    if (!grown)
        return false;
    *items = grown;
    *capacity = next;
    return true;
}

static bool match_decision_u32_push(
    uint32_t **items, uint32_t *length, uint32_t *capacity,
    uint32_t value) {
    if (!items || !length || !capacity)
        return false;
    size_t cap = *capacity;
    if (!match_decision_reserve(
            (void **)items, &cap, (size_t)*length + 1u,
            sizeof(**items)) || cap > UINT32_MAX) {
        return false;
    }
    *capacity = (uint32_t)cap;
    (*items)[(*length)++] = value;
    return true;
}

static bool match_decision_path_equal(
    const CettaMatchDecisionPath *candidate,
    const CettaExprIndex *path, uint32_t path_len) {
    return candidate && candidate->path_len == path_len &&
           (path_len == 0u ||
            memcmp(candidate->path, path,
                   sizeof(*path) * path_len) == 0);
}

static bool match_decision_add_path(
    CettaMatchDecision *decision,
    const CettaExprIndex *path, uint32_t path_len) {
    if (!decision || !path || path_len == 0u ||
        path_len > decision->max_depth)
        return false;
    for (size_t index = 0u; index < decision->path_count; index++) {
        if (match_decision_path_equal(
                &decision->paths[index], path, path_len)) {
            return true;
        }
    }
    if (decision->path_count >= CETTA_MATCH_DECISION_MAX_PATHS ||
        !match_decision_reserve(
            (void **)&decision->paths,
            &decision->path_capacity,
            decision->path_count + 1u,
            sizeof(*decision->paths))) {
        return false;
    }
    CettaExprIndex *copy = malloc(sizeof(*copy) * path_len);
    if (!copy)
        return false;
    memcpy(copy, path, sizeof(*copy) * path_len);
    decision->paths[decision->path_count++] =
        (CettaMatchDecisionPath){
            .path = copy,
            .path_len = path_len,
            .observation_node = UINT32_MAX,
        };
    return true;
}

static CettaMatchDecisionPatternClass match_decision_classify(
    CettaMatchDecisionClassifyPatternFn classify,
    void *context, uint32_t source_ref,
    const CettaExprIndex *path, uint32_t path_len,
    Atom *pattern) {
    return classify
        ? classify(context, source_ref, path, path_len, pattern)
        : CETTA_MATCH_DECISION_PATTERN_STRUCTURAL;
}

static bool match_decision_gather_node_paths(
    CettaMatchDecision *decision,
    CettaMatchDecisionClassifyPatternFn classify,
    void *classify_context, uint32_t source_ref,
    Atom *node, CettaExprIndex path[CETTA_MATCH_DECISION_HARD_MAX_DEPTH],
    uint32_t path_len) {
    if (!decision || !node || path_len == 0u ||
        path_len > decision->max_depth)
        return false;
    if (match_decision_classify(
            classify, classify_context, source_ref,
            path, path_len, node) ==
        CETTA_MATCH_DECISION_PATTERN_OPAQUE) {
        return true;
    }
    if (!match_decision_add_path(decision, path, path_len))
        return false;
    if (node->kind != ATOM_EXPR || path_len >= decision->max_depth)
        return true;
    for (CettaExprIndex child = 0u; child < node->expr.len; child++) {
        path[path_len] = child;
        if (!match_decision_gather_node_paths(
                decision, classify, classify_context, source_ref,
                node->expr.elems[child], path, path_len + 1u)) {
            return false;
        }
    }
    return true;
}

static bool match_decision_gather_paths(
    CettaMatchDecision *decision,
    CettaMatchDecisionClassifyPatternFn classify,
    void *classify_context) {
    if (!decision)
        return false;
    CettaExprIndex path[CETTA_MATCH_DECISION_HARD_MAX_DEPTH] = {0};
    for (size_t clause = 0u; clause < decision->clause_count; clause++) {
        Atom *pattern = decision->clauses[clause].pattern;
        if (!pattern || pattern->kind != ATOM_EXPR)
            continue;
        /* The relation head is already selected by the owning snapshot.
         * Start at arguments so a callable root does not make every useful
         * descendant opaque. */
        for (CettaExprIndex child = 1u;
             child < pattern->expr.len; child++) {
            path[0] = child;
            if (!match_decision_gather_node_paths(
                    decision, classify, classify_context,
                    decision->clauses[clause].source_ref,
                    pattern->expr.elems[child], path, 1u)) {
                return false;
            }
        }
    }
    return true;
}

static void match_decision_first_variables_free(
    CettaMatchDecisionFirstVariable *variables, size_t variable_count) {
    if (!variables)
        return;
    for (size_t index = 0u; index < variable_count; index++)
        free(variables[index].path);
    free(variables);
}

static bool match_decision_add_equality(
    CettaMatchDecision *decision,
    const CettaExprIndex *first, uint32_t first_len,
    const CettaExprIndex *second, uint32_t second_len) {
    if (!decision || !first || first_len == 0u ||
        !second || second_len == 0u)
        return false;
    /* Equality refutation is an optional accelerator.  A very large or cyclic
     * pattern remains a safe candidate superset after the bounded prefix. */
    if (decision->equality_count >=
        CETTA_MATCH_DECISION_MAX_EQUALITIES)
        return true;
    size_t path_len = (size_t)first_len + (size_t)second_len;
    if (path_len < first_len ||
        path_len > SIZE_MAX / sizeof(CettaExprIndex) ||
        !match_decision_reserve(
            (void **)&decision->equalities,
            &decision->equality_capacity,
            decision->equality_count + 1u,
            sizeof(*decision->equalities))) {
        return false;
    }
    CettaExprIndex *paths = malloc(sizeof(*paths) * path_len);
    if (!paths)
        return false;
    memcpy(paths, first, sizeof(*paths) * first_len);
    memcpy(paths + first_len, second, sizeof(*paths) * second_len);
    decision->equalities[decision->equality_count++] =
        (CettaMatchDecisionEquality){
            .paths = paths,
            .first_len = first_len,
            .second_len = second_len,
            .first_observation_node = UINT32_MAX,
            .second_observation_node = UINT32_MAX,
        };
    return true;
}

static bool match_decision_collect_equalities_node(
    CettaMatchDecision *decision,
    CettaMatchDecisionClassifyPatternFn classify,
    void *classify_context, uint32_t source_ref,
    Atom *node,
    CettaExprIndex path[CETTA_MATCH_DECISION_HARD_MAX_DEPTH],
    uint32_t path_len,
    CettaMatchDecisionFirstVariable **variables,
    size_t *variable_count, size_t *variable_capacity) {
    if (!decision || !node || !path || path_len == 0u ||
        path_len > decision->max_depth || !variables ||
        !variable_count || !variable_capacity)
        return false;
    if (match_decision_classify(
            classify, classify_context, source_ref,
            path, path_len, node) ==
        CETTA_MATCH_DECISION_PATTERN_OPAQUE) {
        return true;
    }
    if (node->kind == ATOM_VAR) {
        if (node->var_id == VAR_ID_NONE)
            return true;
        for (size_t index = 0u; index < *variable_count; index++) {
            if ((*variables)[index].id != node->var_id)
                continue;
            return match_decision_add_equality(
                decision, (*variables)[index].path,
                (*variables)[index].path_len, path, path_len);
        }
        if (*variable_count >= CETTA_MATCH_DECISION_MAX_EQUALITIES)
            return true;
        if (!match_decision_reserve(
                (void **)variables, variable_capacity,
                *variable_count + 1u, sizeof(**variables))) {
            return false;
        }
        CettaExprIndex *copy = malloc(sizeof(*copy) * path_len);
        if (!copy)
            return false;
        memcpy(copy, path, sizeof(*copy) * path_len);
        (*variables)[(*variable_count)++] =
            (CettaMatchDecisionFirstVariable){
                .id = node->var_id,
                .path = copy,
                .path_len = path_len,
            };
        return true;
    }
    if (node->kind != ATOM_EXPR || path_len >= decision->max_depth)
        return true;
    for (CettaExprIndex child = 0u; child < node->expr.len; child++) {
        path[path_len] = child;
        if (!match_decision_collect_equalities_node(
                decision, classify, classify_context, source_ref,
                node->expr.elems[child], path, path_len + 1u,
                variables, variable_count, variable_capacity)) {
            return false;
        }
    }
    return true;
}

static bool match_decision_compile_equalities(
    CettaMatchDecision *decision,
    CettaMatchDecisionClassifyPatternFn classify,
    void *classify_context) {
    if (!decision || decision->clause_count == 0u)
        return false;
    if (decision->clause_count == SIZE_MAX ||
        decision->clause_count + 1u >
            SIZE_MAX / sizeof(*decision->equality_offsets)) {
        return false;
    }
    decision->equality_offsets = calloc(
        decision->clause_count + 1u,
        sizeof(*decision->equality_offsets));
    if (!decision->equality_offsets)
        return false;
    CettaExprIndex path[CETTA_MATCH_DECISION_HARD_MAX_DEPTH] = {0};
    for (size_t clause = 0u; clause < decision->clause_count; clause++) {
        decision->equality_offsets[clause] = decision->equality_count;
        Atom *pattern = decision->clauses[clause].pattern;
        if (!pattern || pattern->kind != ATOM_EXPR)
            continue;
        CettaMatchDecisionFirstVariable *variables = NULL;
        size_t variable_count = 0u;
        size_t variable_capacity = 0u;
        bool ok = true;
        for (CettaExprIndex child = 1u;
             child < pattern->expr.len && ok; child++) {
            path[0] = child;
            ok = match_decision_collect_equalities_node(
                decision, classify, classify_context,
                decision->clauses[clause].source_ref,
                pattern->expr.elems[child], path, 1u,
                &variables, &variable_count, &variable_capacity);
        }
        match_decision_first_variables_free(variables, variable_count);
        if (!ok)
            return false;
    }
    decision->equality_offsets[decision->clause_count] =
        decision->equality_count;
    return true;
}

static Atom *match_decision_pattern_at_path(
    Atom *pattern, const CettaExprIndex *path, uint32_t path_len) {
    Atom *node = pattern;
    for (uint32_t depth = 0u; depth < path_len; depth++) {
        if (!node || node->kind != ATOM_EXPR ||
            path[depth] >= node->expr.len) {
            return NULL;
        }
        node = node->expr.elems[path[depth]];
    }
    return node;
}

static bool match_decision_key_from_pattern(
    Atom *pattern, CettaMatchDecisionKeyKind *kind,
    CettaExprLen *expression_length, Atom **atom) {
    if (!pattern || !kind || !expression_length || !atom ||
        pattern->kind == ATOM_VAR) {
        return false;
    }
    *expression_length = 0u;
    *atom = pattern;
    if (pattern->kind != ATOM_EXPR) {
        *kind = CETTA_MATCH_DECISION_KEY_LITERAL;
        return true;
    }
    *expression_length = pattern->expr.len;
    Atom *head = pattern->expr.len > 0u
        ? pattern->expr.elems[0] : NULL;
    if (head && head->kind != ATOM_VAR && head->kind != ATOM_EXPR) {
        *kind = CETTA_MATCH_DECISION_KEY_EXPR_HEAD;
        *atom = head;
    } else {
        *kind = CETTA_MATCH_DECISION_KEY_EXPR_ARITY;
        *atom = NULL;
    }
    return true;
}

static bool match_decision_key_equal(
    const CettaMatchDecisionKey *key,
    CettaMatchDecisionKeyKind kind,
    CettaExprLen expression_length, Atom *atom) {
    if (!key || key->kind != kind ||
        key->expression_length != expression_length)
        return false;
    if (!key->atom || !atom)
        return key->atom == atom;
    return key->atom == atom || atom_eq(key->atom, atom);
}

static uint64_t match_decision_key_hash(
    CettaMatchDecisionKeyKind kind,
    CettaExprLen expression_length, Atom *atom) {
    uint64_t hash = UINT64_C(0x9e3779b97f4a7c15);
    hash ^= (uint64_t)(unsigned int)kind + UINT64_C(0x9e3779b97f4a7c15) +
        (hash << 6u) + (hash >> 2u);
    hash ^= (uint64_t)expression_length + UINT64_C(0x9e3779b97f4a7c15) +
        (hash << 6u) + (hash >> 2u);
    hash ^= (uint64_t)atom_hash(atom) + UINT64_C(0x9e3779b97f4a7c15) +
        (hash << 6u) + (hash >> 2u);
    hash ^= hash >> 30u;
    hash *= UINT64_C(0xbf58476d1ce4e5b9);
    hash ^= hash >> 27u;
    hash *= UINT64_C(0x94d049bb133111eb);
    hash ^= hash >> 31u;
    return hash;
}

static const CettaMatchDecisionKey *match_decision_path_find_key(
    const CettaMatchDecisionPath *descriptor,
    CettaMatchDecisionKeyKind kind,
    CettaExprLen expression_length, Atom *atom,
    uint64_t *probe_count) {
    if (!descriptor)
        return NULL;
    if (!descriptor->key_slots || descriptor->key_slot_capacity == 0u) {
        for (uint32_t index = 0u; index < descriptor->key_count; index++) {
            if (probe_count)
                (*probe_count)++;
            if (match_decision_key_equal(
                    &descriptor->keys[index], kind,
                    expression_length, atom)) {
                return &descriptor->keys[index];
            }
        }
        return NULL;
    }

    size_t mask = descriptor->key_slot_capacity - 1u;
    size_t slot = (size_t)match_decision_key_hash(
        kind, expression_length, atom) & mask;
    for (size_t probe = 0u;
         probe < descriptor->key_slot_capacity; probe++) {
        if (probe_count)
            (*probe_count)++;
        uint32_t encoded = descriptor->key_slots[slot];
        if (encoded == 0u)
            return NULL;
        uint32_t index = encoded - 1u;
        if (index < descriptor->key_count &&
            match_decision_key_equal(
                &descriptor->keys[index], kind,
                expression_length, atom)) {
            return &descriptor->keys[index];
        }
        slot = (slot + 1u) & mask;
    }
    return NULL;
}

static bool match_decision_path_rebuild_key_index(
    CettaMatchDecisionPath *descriptor, size_t capacity,
    uint64_t *probe_count) {
    if (!descriptor || capacity < CETTA_MATCH_DECISION_KEY_INDEX_MIN_CAPACITY ||
        (capacity & (capacity - 1u)) != 0u ||
        capacity > SIZE_MAX / sizeof(*descriptor->key_slots)) {
        return false;
    }
    uint32_t *slots = calloc(capacity, sizeof(*slots));
    if (!slots)
        return false;
    size_t mask = capacity - 1u;
    for (uint32_t index = 0u; index < descriptor->key_count; index++) {
        const CettaMatchDecisionKey *key = &descriptor->keys[index];
        size_t slot = (size_t)match_decision_key_hash(
            key->kind, key->expression_length, key->atom) & mask;
        while (slots[slot] != 0u) {
            if (probe_count)
                (*probe_count)++;
            slot = (slot + 1u) & mask;
        }
        if (probe_count)
            (*probe_count)++;
        slots[slot] = index + 1u;
    }
    free(descriptor->key_slots);
    descriptor->key_slots = slots;
    descriptor->key_slot_capacity = capacity;
    return true;
}

static bool match_decision_path_reserve_key_index(
    CettaMatchDecisionPath *descriptor, uint32_t needed,
    uint64_t *probe_count) {
    if (!descriptor)
        return false;
    if (needed <= CETTA_MATCH_DECISION_KEY_INDEX_MIN_KEYS &&
        !descriptor->key_slots) {
        return true;
    }
    size_t capacity = descriptor->key_slot_capacity
        ? descriptor->key_slot_capacity
        : CETTA_MATCH_DECISION_KEY_INDEX_MIN_CAPACITY;
    while ((uint64_t)needed * UINT64_C(10) >
           (uint64_t)capacity * UINT64_C(7)) {
        if (capacity > SIZE_MAX / 2u)
            return false;
        capacity *= 2u;
    }
    if (capacity == descriptor->key_slot_capacity)
        return true;
    return match_decision_path_rebuild_key_index(
        descriptor, capacity, probe_count);
}

static bool match_decision_path_insert_key_index(
    CettaMatchDecisionPath *descriptor, uint32_t key_index,
    uint64_t *probe_count) {
    if (!descriptor || !descriptor->key_slots ||
        descriptor->key_slot_capacity == 0u ||
        key_index >= descriptor->key_count) {
        return false;
    }
    const CettaMatchDecisionKey *key = &descriptor->keys[key_index];
    size_t mask = descriptor->key_slot_capacity - 1u;
    size_t slot = (size_t)match_decision_key_hash(
        key->kind, key->expression_length, key->atom) & mask;
    for (size_t probe = 0u;
         probe < descriptor->key_slot_capacity; probe++) {
        if (probe_count)
            (*probe_count)++;
        if (descriptor->key_slots[slot] == 0u) {
            descriptor->key_slots[slot] = key_index + 1u;
            return true;
        }
        slot = (slot + 1u) & mask;
    }
    return false;
}

static CettaMatchDecisionKey *match_decision_path_key(
    CettaMatchDecision *decision,
    CettaMatchDecisionPath *descriptor,
    CettaMatchDecisionKeyKind kind,
    CettaExprLen expression_length, Atom *atom) {
    if (!decision || !descriptor)
        return NULL;
    const CettaMatchDecisionKey *existing = match_decision_path_find_key(
        descriptor, kind, expression_length, atom,
        &decision->stats.key_index_build_probes);
    if (existing)
        return (CettaMatchDecisionKey *)existing;
    if (descriptor->key_count == UINT32_MAX ||
        !match_decision_path_reserve_key_index(
            descriptor, descriptor->key_count + 1u,
            &decision->stats.key_index_build_probes)) {
        return NULL;
    }
    size_t capacity = descriptor->key_capacity;
    if (!match_decision_reserve(
            (void **)&descriptor->keys, &capacity,
            (size_t)descriptor->key_count + 1u,
            sizeof(*descriptor->keys)) || capacity > UINT32_MAX) {
        return NULL;
    }
    descriptor->key_capacity = (uint32_t)capacity;
    CettaMatchDecisionKey *key =
        &descriptor->keys[descriptor->key_count++];
    *key = (CettaMatchDecisionKey){
        .kind = kind,
        .expression_length = expression_length,
        .atom = atom,
    };
    if (descriptor->key_slots &&
        !match_decision_path_insert_key_index(
            descriptor, descriptor->key_count - 1u,
            &decision->stats.key_index_build_probes)) {
        return NULL;
    }
    return key;
}

static bool match_decision_compile_path(
    CettaMatchDecision *decision,
    CettaMatchDecisionPath *descriptor,
    CettaMatchDecisionClassifyPatternFn classify,
    void *classify_context) {
    if (!decision || !descriptor)
        return false;
    for (size_t clause = 0u; clause < decision->clause_count; clause++) {
        const CettaMatchDecisionClause *source = &decision->clauses[clause];
        Atom *node = match_decision_pattern_at_path(
            source->pattern, descriptor->path, descriptor->path_len);
        CettaMatchDecisionKeyKind kind = 0;
        CettaExprLen expression_length = 0u;
        Atom *key_atom = NULL;
        bool keyed = node &&
            match_decision_classify(
                classify, classify_context, source->source_ref,
                descriptor->path, descriptor->path_len, node) ==
                CETTA_MATCH_DECISION_PATTERN_STRUCTURAL &&
            match_decision_key_from_pattern(
                node, &kind, &expression_length, &key_atom);
        if (!keyed) {
            if (!match_decision_u32_push(
                    &descriptor->wildcard_refs,
                    &descriptor->wildcard_count,
                    &descriptor->wildcard_capacity,
                    (uint32_t)clause)) {
                return false;
            }
            continue;
        }
        CettaMatchDecisionKey *key = match_decision_path_key(
            decision, descriptor, kind, expression_length, key_atom);
        if (!key || !match_decision_u32_push(
                &key->clause_refs, &key->clause_count,
                &key->clause_capacity, (uint32_t)clause)) {
            return false;
        }
    }
    return true;
}

static void match_decision_path_free(CettaMatchDecisionPath *path) {
    if (!path)
        return;
    for (uint32_t key = 0u; key < path->key_count; key++)
        free(path->keys[key].clause_refs);
    free(path->key_slots);
    free(path->keys);
    free(path->wildcard_refs);
    free(path->wildcard_bits);
    free(path->path);
    memset(path, 0, sizeof(*path));
}

static void match_decision_remove_empty_paths(
    CettaMatchDecision *decision) {
    if (!decision)
        return;
    size_t write = 0u;
    for (size_t read = 0u; read < decision->path_count; read++) {
        CettaMatchDecisionPath *path = &decision->paths[read];
        if (path->key_count == 0u ||
            path->wildcard_count == decision->clause_count) {
            match_decision_path_free(path);
            continue;
        }
        if (write != read) {
            decision->paths[write] = *path;
            memset(path, 0, sizeof(*path));
        }
        write++;
    }
    decision->path_count = write;
}

static bool match_decision_process_switch(const char *name) {
    const char *value = name ? getenv(name) : NULL;
    return value && value[0] != '\0' && strcmp(value, "0") != 0;
}

CettaMatchDecisionRealization
cetta_match_decision_realization_from_process(void) {
    return (CettaMatchDecisionRealization){
        .use_direct_prefix_observation = match_decision_process_switch(
            "CETTA_MATCH_DECISION_PREFIX_OBSERVATION_REFERENCE"),
        .use_eager_prefix_observation = match_decision_process_switch(
            "CETTA_MATCH_DECISION_PREFIX_OBSERVATION_EAGER_REFERENCE"),
        .use_direct_equality_observation = match_decision_process_switch(
            "CETTA_MATCH_DECISION_EQUALITY_OBSERVATION_REFERENCE"),
    };
}

static uint64_t match_decision_prefix_edge_hash(
    uint32_t parent, CettaExprIndex edge) {
    uint64_t hash = (uint64_t)parent * UINT64_C(0x9e3779b97f4a7c15) ^
        (uint64_t)edge;
    hash ^= hash >> 30u;
    hash *= UINT64_C(0xbf58476d1ce4e5b9);
    hash ^= hash >> 27u;
    hash *= UINT64_C(0x94d049bb133111eb);
    return hash ^ (hash >> 31u);
}

static void match_decision_prefix_observation_reset_terminals(
    CettaMatchDecision *decision) {
    if (!decision)
        return;
    for (size_t path = 0u; path < decision->path_count; path++)
        decision->paths[path].observation_node = UINT32_MAX;
    for (size_t equality = 0u;
         equality < decision->equality_count; equality++) {
        decision->equalities[equality].first_observation_node = UINT32_MAX;
        decision->equalities[equality].second_observation_node = UINT32_MAX;
    }
}

static bool match_decision_prefix_observation_insert(
    const CettaExprIndex *path, uint32_t path_len,
    CettaMatchDecisionObservationNode *nodes, size_t node_capacity,
    size_t *node_count, uint32_t *slots, size_t slot_capacity,
    uint32_t *terminal) {
    if (!path || path_len == 0u || !nodes || !node_count || !slots ||
        slot_capacity == 0u || !terminal)
        return false;
    uint32_t parent = 0u;
    for (uint32_t depth = 0u; depth < path_len; depth++) {
        CettaExprIndex edge = path[depth];
        size_t slot = (size_t)match_decision_prefix_edge_hash(
            parent, edge) & (slot_capacity - 1u);
        uint32_t child = UINT32_MAX;
        for (;;) {
            uint32_t stored = slots[slot];
            if (stored == 0u)
                break;
            uint32_t candidate = stored - 1u;
            if (candidate >= *node_count)
                return false;
            if (nodes[candidate].parent == parent &&
                nodes[candidate].edge == edge) {
                child = candidate;
                break;
            }
            slot = (slot + 1u) & (slot_capacity - 1u);
        }
        if (child == UINT32_MAX) {
            if (*node_count >= node_capacity ||
                *node_count >= UINT32_MAX)
                return false;
            child = (uint32_t)(*node_count);
            (*node_count)++;
            nodes[child] = (CettaMatchDecisionObservationNode){
                .parent = parent,
                .edge = edge,
            };
            slots[slot] = child + 1u;
        }
        parent = child;
    }
    *terminal = parent;
    return true;
}

/* Compile the union of every read-only coordinate demanded by selection or
 * repeated-variable refutation.  Direct cost retains consumer occurrence
 * multiplicity; the graph stores each prefix once.  The accelerator remains
 * optional: allocation failure or an insufficient static saving retains the
 * independent walker.  Charging one additional unit per trie edge accounts
 * for cache metadata and requires a strict two-for-one reduction before
 * admission.  Runtime filling is demand-driven, so equality endpoints of
 * candidates removed by selection are never observed speculatively. */
static bool match_decision_build_prefix_observation(
    CettaMatchDecision *decision) {
    bool include_equalities = decision &&
        !decision->realization.use_direct_equality_observation;
    if (!decision ||
        (decision->path_count == 0u &&
         (!include_equalities || decision->equality_count == 0u)) ||
        decision->realization.use_direct_prefix_observation) {
        return true;
    }
    decision->stats.prefix_observation_build_attempts++;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_MATCH_DECISION_PREFIX_OBSERVATION_BUILD_ATTEMPT);
    match_decision_prefix_observation_reset_terminals(decision);

    size_t direct_edges = 0u;
    for (size_t path = 0u; path < decision->path_count; path++) {
        uint32_t path_len = decision->paths[path].path_len;
        if (!decision->paths[path].path || path_len == 0u ||
            direct_edges > SIZE_MAX - path_len) {
            goto decline;
        }
        direct_edges += path_len;
    }
    for (size_t equality_index = 0u;
         include_equalities &&
         equality_index < decision->equality_count; equality_index++) {
        CettaMatchDecisionEquality *equality =
            &decision->equalities[equality_index];
        if (!equality->paths || equality->first_len == 0u ||
            equality->second_len == 0u ||
            direct_edges > SIZE_MAX - equality->first_len) {
            goto decline;
        }
        direct_edges += equality->first_len;
        if (direct_edges > SIZE_MAX - equality->second_len)
            goto decline;
        direct_edges += equality->second_len;
    }
    if (direct_edges == 0u || direct_edges >= UINT32_MAX ||
        direct_edges > (SIZE_MAX / 2u) - 1u) {
        goto decline;
    }

    size_t slot_capacity = 16u;
    size_t required_slots = (direct_edges + 1u) * 2u;
    while (slot_capacity < required_slots) {
        if (slot_capacity > SIZE_MAX / 2u)
            goto decline;
        slot_capacity *= 2u;
    }
    if (slot_capacity > SIZE_MAX / sizeof(uint32_t) ||
        direct_edges + 1u >
            SIZE_MAX / sizeof(CettaMatchDecisionObservationNode)) {
        goto decline;
    }
    uint32_t *slots = calloc(slot_capacity, sizeof(*slots));
    CettaMatchDecisionObservationNode *nodes = malloc(
        (direct_edges + 1u) * sizeof(*nodes));
    if (!slots || !nodes) {
        free(nodes);
        free(slots);
        goto decline;
    }
    size_t node_count = 1u;
    nodes[0] = (CettaMatchDecisionObservationNode){
        .parent = UINT32_MAX,
        .edge = 0u,
    };

    for (size_t path_index = 0u;
         path_index < decision->path_count; path_index++) {
        CettaMatchDecisionPath *path = &decision->paths[path_index];
        if (!match_decision_prefix_observation_insert(
                path->path, path->path_len,
                nodes, direct_edges + 1u, &node_count,
                slots, slot_capacity, &path->observation_node)) {
            free(nodes);
            free(slots);
            goto decline;
        }
    }
    const size_t selector_node_count = node_count;
    for (size_t equality_index = 0u;
         include_equalities &&
         equality_index < decision->equality_count; equality_index++) {
        CettaMatchDecisionEquality *equality =
            &decision->equalities[equality_index];
        if (!match_decision_prefix_observation_insert(
                equality->paths, equality->first_len,
                nodes, direct_edges + 1u, &node_count,
                slots, slot_capacity,
                &equality->first_observation_node) ||
            !match_decision_prefix_observation_insert(
                equality->paths + equality->first_len,
                equality->second_len,
                nodes, direct_edges + 1u, &node_count,
                slots, slot_capacity,
                &equality->second_observation_node)) {
            free(nodes);
            free(slots);
            goto decline;
        }
    }
    free(slots);

    size_t trie_edges = node_count - 1u;
    decision->stats.prefix_observation_direct_edges = direct_edges;
    decision->stats.prefix_observation_trie_edges = trie_edges;
    if (trie_edges > (SIZE_MAX - 1u) / 2u ||
        1u + 2u * trie_edges >= direct_edges) {
        free(nodes);
        goto decline;
    }

    unsigned char *states = malloc(node_count * sizeof(*states));
    Atom **values = malloc(node_count * sizeof(*values));
    uint64_t *stamps = calloc(node_count, sizeof(*stamps));
    if (!states || !values || !stamps) {
        free(stamps);
        free(values);
        free(states);
        free(nodes);
        goto decline;
    }
    decision->observation_nodes = nodes;
    decision->observation_node_count = node_count;
    decision->observation_selector_node_count = selector_node_count;
    decision->observation_states = states;
    decision->observation_values = values;
    decision->observation_stamps = stamps;
    decision->stats.prefix_observation_build_commits++;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_MATCH_DECISION_PREFIX_OBSERVATION_BUILD_COMMIT);
    return true;

decline:
    match_decision_prefix_observation_reset_terminals(decision);
    decision->observation_selector_node_count = 0u;
    decision->stats.prefix_observation_build_declines++;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_MATCH_DECISION_PREFIX_OBSERVATION_BUILD_DECLINE);
    return true;
}

static bool match_decision_build_conjunctive_masks(
    CettaMatchDecision *decision) {
    if (!decision || decision->clause_count == 0u)
        return false;
    size_t words = decision->clause_count / 64u +
        (decision->clause_count % 64u != 0u ? 1u : 0u);
    if (words == 0u || words > SIZE_MAX / sizeof(uint64_t) ||
        decision->path_count > SIZE_MAX / words)
        return false;
    size_t path_words = decision->path_count * words;
    if (path_words >
        CETTA_MATCH_DECISION_CONJUNCTIVE_MAX_MASK_BYTES /
            sizeof(uint64_t)) {
        return false;
    }
    decision->candidate_bits = calloc(words, sizeof(uint64_t));
    decision->path_bits = calloc(words, sizeof(uint64_t));
    if (!decision->candidate_bits || !decision->path_bits)
        return false;
    decision->bit_word_count = words;
    for (size_t path_index = 0u;
         path_index < decision->path_count; path_index++) {
        CettaMatchDecisionPath *path = &decision->paths[path_index];
        path->wildcard_bits = calloc(words, sizeof(uint64_t));
        if (!path->wildcard_bits)
            return false;
        for (uint32_t index = 0u;
             index < path->wildcard_count; index++) {
            uint32_t clause = path->wildcard_refs[index];
            if (clause >= decision->clause_count)
                return false;
            path->wildcard_bits[clause / 64u] |=
                UINT64_C(1) << (clause % 64u);
        }
    }
    return true;
}

CettaMatchDecision *cetta_match_decision_compile(
    SpaceReadToken read,
    CettaMatchDecisionSemanticIdentity semantic_identity,
    const CettaMatchDecisionClause *clauses,
    size_t clause_count,
    CettaMatchDecisionMode mode,
    uint32_t max_depth,
    CettaMatchDecisionRealization realization,
    CettaMatchDecisionClassifyPatternFn classify,
    void *classify_context) {
    if (!clauses || clause_count == 0u || clause_count > UINT32_MAX ||
        !space_read_token_is_current(read) ||
        (mode != CETTA_MATCH_DECISION_LINEAR &&
         mode != CETTA_MATCH_DECISION_DEEP &&
         mode != CETTA_MATCH_DECISION_CONJUNCTIVE)) {
        return NULL;
    }
    if (max_depth == 0u)
        max_depth = CETTA_MATCH_DECISION_DEFAULT_MAX_DEPTH;
    if (max_depth > CETTA_MATCH_DECISION_HARD_MAX_DEPTH)
        return NULL;

    CettaMatchDecision *decision = calloc(1u, sizeof(*decision));
    if (!decision)
        return NULL;
    atomic_init(&decision->owner_count, 1u);
    decision->read = read;
    decision->semantic_identity = semantic_identity;
    decision->mode = mode;
    decision->max_depth = max_depth;
    decision->realization = realization;
    decision->clauses = malloc(sizeof(*decision->clauses) * clause_count);
    if (!decision->clauses) {
        cetta_match_decision_free(decision);
        return NULL;
    }
    memcpy(decision->clauses, clauses,
           sizeof(*decision->clauses) * clause_count);
    decision->clause_count = clause_count;
    decision->stats.compilations = 1u;

    if (mode == CETTA_MATCH_DECISION_DEEP ||
        mode == CETTA_MATCH_DECISION_CONJUNCTIVE) {
        if (!match_decision_gather_paths(
                decision, classify, classify_context) ||
            !match_decision_compile_equalities(
                decision, classify, classify_context)) {
            cetta_match_decision_free(decision);
            return NULL;
        }
        for (size_t path = 0u; path < decision->path_count; path++) {
            if (!match_decision_compile_path(
                    decision, &decision->paths[path],
                    classify, classify_context)) {
                cetta_match_decision_free(decision);
                return NULL;
            }
        }
        match_decision_remove_empty_paths(decision);
        if (!match_decision_build_prefix_observation(decision) ||
            (mode == CETTA_MATCH_DECISION_CONJUNCTIVE &&
             !match_decision_build_conjunctive_masks(decision))) {
            cetta_match_decision_free(decision);
            return NULL;
        }
    }
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_MATCH_DECISION_COMPILE);
    return decision;
}

CettaMatchDecision *cetta_match_decision_retain(
    CettaMatchDecision *decision) {
    if (!decision)
        return NULL;
    size_t owners = atomic_load_explicit(
        &decision->owner_count, memory_order_relaxed);
    while (owners != 0u && owners != SIZE_MAX) {
        if (atomic_compare_exchange_weak_explicit(
                &decision->owner_count, &owners, owners + 1u,
                memory_order_relaxed, memory_order_relaxed)) {
            return decision;
        }
    }
    return NULL;
}

void cetta_match_decision_free(CettaMatchDecision *decision) {
    if (!decision)
        return;
    size_t owners = atomic_load_explicit(
        &decision->owner_count, memory_order_relaxed);
    for (;;) {
        if (owners == 0u)
            abort();
        if (atomic_compare_exchange_weak_explicit(
                &decision->owner_count, &owners, owners - 1u,
                memory_order_acq_rel, memory_order_relaxed)) {
            break;
        }
    }
    if (owners != 1u)
        return;
    for (size_t path = 0u; path < decision->path_count; path++)
        match_decision_path_free(&decision->paths[path]);
    free(decision->paths);
    free(decision->clauses);
    free(decision->candidate_locals);
    free(decision->candidate_sources);
    free(decision->working_lists);
    free(decision->candidate_bits);
    free(decision->path_bits);
    free(decision->observation_nodes);
    free(decision->observation_states);
    free(decision->observation_values);
    free(decision->observation_stamps);
    for (size_t equality = 0u;
         equality < decision->equality_count; equality++) {
        free(decision->equalities[equality].paths);
    }
    free(decision->equalities);
    free(decision->equality_offsets);
    free(decision);
}

static bool match_decision_semantic_identity_equal(
    CettaMatchDecisionSemanticIdentity left,
    CettaMatchDecisionSemanticIdentity right) {
    return left.language_id == right.language_id &&
           left.profile_id == right.profile_id &&
           left.match_policy_id == right.match_policy_id &&
           left.demand_policy_id == right.demand_policy_id &&
           left.presentation_identity == right.presentation_identity &&
           left.compiler_identity == right.compiler_identity;
}

bool cetta_match_decision_is_current(
    const CettaMatchDecision *decision, const Space *live_space,
    CettaMatchDecisionSemanticIdentity semantic_identity) {
    return decision && live_space &&
           space_read_token_matches_live_space(
               decision->read, live_space) &&
           match_decision_semantic_identity_equal(
               decision->semantic_identity, semantic_identity);
}

typedef enum {
    CETTA_MATCH_DECISION_QUERY_UNKNOWN = 0,
    CETTA_MATCH_DECISION_QUERY_ABSENT = 1,
    CETTA_MATCH_DECISION_QUERY_VALUE = 2,
} CettaMatchDecisionQueryState;

typedef struct {
    Atom *whole;
    Atom *head;
    Atom *const *arguments;
    size_t arity;
} CettaMatchDecisionQuery;

static unsigned char match_decision_policy(
    CettaMatchDecisionQueryState query_state,
    const CettaMatchDecisionKey *key, Atom *value) {
    unsigned int observation = 0u;
    if (query_state == CETTA_MATCH_DECISION_QUERY_ABSENT) {
        observation = 1u;
    } else if (query_state == CETTA_MATCH_DECISION_QUERY_VALUE) {
        observation = value && value->kind == ATOM_EXPR ? 3u : 2u;
    }
    unsigned int key_kind = key ? (unsigned int)key->kind : 0u;
    bool arity_equal = false;
    bool identity_equal = false;
    if (key && value) {
        if (key->kind == CETTA_MATCH_DECISION_KEY_LITERAL) {
            identity_equal = value->kind != ATOM_EXPR && key->atom &&
                (key->atom == value || atom_eq(key->atom, value));
        } else if (value->kind == ATOM_EXPR) {
            arity_equal =
                value->expr.len == key->expression_length;
            if (key->kind == CETTA_MATCH_DECISION_KEY_EXPR_HEAD &&
                key->atom && value->expr.len > 0u) {
                Atom *head = value->expr.elems[0];
                identity_equal = head &&
                    (head == key->atom || atom_eq(head, key->atom));
            }
        }
    }
    return cetta_md_policy_v1[observation][key_kind]
                             [arity_equal ? 1u : 0u]
                             [identity_equal ? 1u : 0u];
}

static CettaMatchDecisionQueryState match_decision_query_at_path(
    const CettaMatchDecisionQuery *query,
    const CettaExprIndex *path, uint32_t path_len,
    uint64_t ready_arguments, Atom **value) {
    if (value)
        *value = NULL;
    if (!query || !path || path_len == 0u || !value ||
        (!query->whole && !query->head))
        return CETTA_MATCH_DECISION_QUERY_UNKNOWN;
    Atom *node = query->whole;
    uint32_t depth = 0u;
    if (!node) {
        CettaExprIndex child = path[0];
        if (child == 0u) {
            node = query->head;
        } else {
            uint64_t argument = (uint64_t)child - 1u;
            if (ready_arguments != UINT64_MAX &&
                (argument >= 64u ||
                 (ready_arguments & (UINT64_C(1) << argument)) == 0u)) {
                return CETTA_MATCH_DECISION_QUERY_UNKNOWN;
            }
            if (argument >= query->arity)
                return CETTA_MATCH_DECISION_QUERY_ABSENT;
            if (!query->arguments)
                return CETTA_MATCH_DECISION_QUERY_UNKNOWN;
            node = query->arguments[argument];
        }
        depth = 1u;
    }
    for (; depth < path_len; depth++) {
        CettaExprIndex child = path[depth];
        if (depth == 0u && child > 0u &&
            ready_arguments != UINT64_MAX) {
            uint64_t argument = (uint64_t)child - 1u;
            if (argument >= 64u ||
                (ready_arguments & (UINT64_C(1) << argument)) == 0u) {
                return CETTA_MATCH_DECISION_QUERY_UNKNOWN;
            }
        }
        if (!node || node->kind == ATOM_VAR)
            return CETTA_MATCH_DECISION_QUERY_UNKNOWN;
        if (node->kind != ATOM_EXPR || child >= node->expr.len)
            return CETTA_MATCH_DECISION_QUERY_ABSENT;
        node = node->expr.elems[child];
    }
    if (!node || node->kind == ATOM_VAR)
        return CETTA_MATCH_DECISION_QUERY_UNKNOWN;
    *value = node;
    return CETTA_MATCH_DECISION_QUERY_VALUE;
}

static bool match_decision_has_prefix_observation(
    const CettaMatchDecision *decision) {
    return decision && decision->observation_nodes &&
           decision->observation_node_count > 1u &&
           decision->observation_states &&
           decision->observation_values &&
           decision->observation_stamps;
}

static CettaMatchDecisionQueryState
match_decision_prefix_observation_step(
    const CettaMatchDecisionQuery *query, uint32_t parent,
    CettaMatchDecisionQueryState parent_state, Atom *parent_value,
    CettaExprIndex edge, uint64_t ready_arguments, Atom **value);

static bool match_decision_query_state_absorbs_suffix(
    CettaMatchDecisionQueryState state);

static void match_decision_record_absorbed_suffix(
    CettaMatchDecision *decision, uint32_t skipped_edges);

static void match_decision_prefix_observation_begin(
    CettaMatchDecision *decision,
    const CettaMatchDecisionQuery *query,
    uint64_t ready_arguments) {
    if (!match_decision_has_prefix_observation(decision) || !query)
        return;
    decision->observation_ready = false;
    if (decision->observation_epoch == UINT64_MAX) {
        memset(decision->observation_stamps, 0,
               decision->observation_node_count *
                   sizeof(*decision->observation_stamps));
        decision->observation_epoch = 1u;
    } else {
        decision->observation_epoch++;
        if (decision->observation_epoch == 0u)
            decision->observation_epoch = 1u;
    }
    decision->observation_states[0] =
        CETTA_MATCH_DECISION_QUERY_VALUE;
    decision->observation_values[0] = query->whole;
    decision->observation_stamps[0] = decision->observation_epoch;
    bool eager_reference =
        decision->realization.use_eager_prefix_observation;
    size_t selector_node_count =
        decision->observation_selector_node_count;
    if (selector_node_count > decision->observation_node_count)
        selector_node_count = decision->observation_node_count;
    /* Selection consumes the complete selector graph, so stream that compact
     * region once in topological order.  UNKNOWN and ABSENT are absorbing:
     * the optimized realization propagates them without re-entering the
     * structural observer.  Repeated-variable equality endpoints remain
     * demand-driven because selection may remove their clauses first. */
    uint32_t absorbed_edges = 0u;
    for (size_t node = 1u; node < selector_node_count; node++) {
        const CettaMatchDecisionObservationNode *entry =
            &decision->observation_nodes[node];
        uint32_t parent = entry->parent;
        if (parent >= node ||
            decision->observation_stamps[parent] !=
                decision->observation_epoch) {
            break;
        }
        CettaMatchDecisionQueryState parent_state =
            (CettaMatchDecisionQueryState)
                decision->observation_states[parent];
        Atom *observed = NULL;
        CettaMatchDecisionQueryState state;
        if (!eager_reference &&
            match_decision_query_state_absorbs_suffix(parent_state)) {
            state = parent_state;
            absorbed_edges++;
        } else {
            state = match_decision_prefix_observation_step(
                query, parent, parent_state,
                decision->observation_values[parent],
                entry->edge, ready_arguments, &observed);
            decision->stats.prefix_observation_node_visits++;
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_MATCH_DECISION_PREFIX_OBSERVATION_NODE_VISIT);
        }
        decision->observation_states[node] = (unsigned char)state;
        decision->observation_values[node] = observed;
        decision->observation_stamps[node] =
            decision->observation_epoch;
    }
    match_decision_record_absorbed_suffix(decision, absorbed_edges);
    decision->observation_ready = true;
    decision->stats.prefix_observation_runs++;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_MATCH_DECISION_PREFIX_OBSERVATION_RUN);
}

static CettaMatchDecisionQueryState
match_decision_prefix_observation_step(
    const CettaMatchDecisionQuery *query, uint32_t parent,
    CettaMatchDecisionQueryState parent_state, Atom *parent_value,
    CettaExprIndex edge, uint64_t ready_arguments, Atom **value) {
    if (value)
        *value = NULL;
    if (!query || !value)
        return CETTA_MATCH_DECISION_QUERY_UNKNOWN;
    if (parent_state == CETTA_MATCH_DECISION_QUERY_UNKNOWN)
        return CETTA_MATCH_DECISION_QUERY_UNKNOWN;
    if (parent_state == CETTA_MATCH_DECISION_QUERY_ABSENT)
        return CETTA_MATCH_DECISION_QUERY_ABSENT;

    Atom *node = parent_value;
    if (parent == 0u && !query->whole) {
        if (edge == 0u) {
            node = query->head;
        } else {
            uint64_t argument = (uint64_t)edge - 1u;
            if (ready_arguments != UINT64_MAX &&
                (argument >= 64u ||
                 (ready_arguments &
                  (UINT64_C(1) << argument)) == 0u)) {
                return CETTA_MATCH_DECISION_QUERY_UNKNOWN;
            }
            if (argument >= query->arity)
                return CETTA_MATCH_DECISION_QUERY_ABSENT;
            if (!query->arguments)
                return CETTA_MATCH_DECISION_QUERY_UNKNOWN;
            node = query->arguments[argument];
        }
    } else {
        if (parent == 0u && edge > 0u &&
            ready_arguments != UINT64_MAX) {
            uint64_t argument = (uint64_t)edge - 1u;
            if (argument >= 64u ||
                (ready_arguments &
                 (UINT64_C(1) << argument)) == 0u) {
                return CETTA_MATCH_DECISION_QUERY_UNKNOWN;
            }
        }
        if (!node || node->kind == ATOM_VAR)
            return CETTA_MATCH_DECISION_QUERY_UNKNOWN;
        if (node->kind != ATOM_EXPR || edge >= node->expr.len)
            return CETTA_MATCH_DECISION_QUERY_ABSENT;
        node = node->expr.elems[edge];
    }
    if (!node || node->kind == ATOM_VAR)
        return CETTA_MATCH_DECISION_QUERY_UNKNOWN;
    *value = node;
    return CETTA_MATCH_DECISION_QUERY_VALUE;
}

static bool match_decision_query_state_absorbs_suffix(
        CettaMatchDecisionQueryState state) {
    return state == CETTA_MATCH_DECISION_QUERY_UNKNOWN ||
           state == CETTA_MATCH_DECISION_QUERY_ABSENT;
}

static void match_decision_record_absorbed_suffix(
        CettaMatchDecision *decision, uint32_t skipped_edges) {
    if (!decision || skipped_edges == 0u)
        return;
    decision->stats.prefix_observation_absorbed_suffixes++;
    decision->stats.prefix_observation_skipped_edges += skipped_edges;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_MATCH_DECISION_PREFIX_OBSERVATION_ABSORBED_SUFFIX);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_MATCH_DECISION_PREFIX_OBSERVATION_SKIPPED_EDGE,
        skipped_edges);
}

/* Demand one compiled terminal.  Only the unstamped suffix from the nearest
 * cached ancestor is evaluated, so unused equality coordinates do no query
 * work and overlapping requests share every observed prefix. */
static CettaMatchDecisionQueryState
match_decision_query_at_observation_node(
    CettaMatchDecision *decision,
    const CettaMatchDecisionQuery *query,
    const CettaExprIndex *path, uint32_t path_len,
    uint32_t terminal, uint64_t ready_arguments,
    Atom **value, bool *compiled, uint32_t *graph_edges) {
    if (value)
        *value = NULL;
    if (compiled)
        *compiled = false;
    if (graph_edges)
        *graph_edges = 0u;
    if (!match_decision_has_prefix_observation(decision) ||
        !decision->observation_ready || terminal == UINT32_MAX ||
        terminal >= decision->observation_node_count || !value) {
        return match_decision_query_at_path(
            query, path, path_len, ready_arguments, value);
    }

    if (decision->observation_stamps[terminal] ==
        decision->observation_epoch) {
        if (compiled)
            *compiled = true;
        *value = decision->observation_values[terminal];
        return (CettaMatchDecisionQueryState)
            decision->observation_states[terminal];
    }

    uint32_t pending[CETTA_MATCH_DECISION_HARD_MAX_DEPTH];
    uint32_t pending_count = 0u;
    uint32_t node = terminal;
    while (decision->observation_stamps[node] !=
           decision->observation_epoch) {
        if (node == 0u ||
            pending_count >= CETTA_MATCH_DECISION_HARD_MAX_DEPTH) {
            return match_decision_query_at_path(
                query, path, path_len, ready_arguments, value);
        }
        const CettaMatchDecisionObservationNode *entry =
            &decision->observation_nodes[node];
        if (entry->parent >= node) {
            return match_decision_query_at_path(
                query, path, path_len, ready_arguments, value);
        }
        pending[pending_count++] = node;
        node = entry->parent;
    }

    uint32_t filled_edges = 0u;
    while (pending_count > 0u) {
        CettaMatchDecisionQueryState parent_state =
            (CettaMatchDecisionQueryState)
                decision->observation_states[node];
        if (match_decision_query_state_absorbs_suffix(parent_state)) {
            uint32_t skipped_edges = pending_count;
            while (pending_count > 0u) {
                uint32_t child = pending[--pending_count];
                decision->observation_states[child] =
                    (unsigned char)parent_state;
                decision->observation_values[child] = NULL;
                decision->observation_stamps[child] =
                    decision->observation_epoch;
            }
            match_decision_record_absorbed_suffix(
                decision, skipped_edges);
            break;
        }
        uint32_t child = pending[--pending_count];
        const CettaMatchDecisionObservationNode *entry =
            &decision->observation_nodes[child];
        uint32_t parent = entry->parent;
        if (parent >= child ||
            decision->observation_stamps[parent] !=
                decision->observation_epoch) {
            return match_decision_query_at_path(
                query, path, path_len, ready_arguments, value);
        }
        Atom *observed = NULL;
        CettaMatchDecisionQueryState state =
            match_decision_prefix_observation_step(
                query, parent,
                (CettaMatchDecisionQueryState)
                    decision->observation_states[parent],
                decision->observation_values[parent],
                entry->edge, ready_arguments, &observed);
        decision->observation_states[child] = (unsigned char)state;
        decision->observation_values[child] = observed;
        decision->observation_stamps[child] =
            decision->observation_epoch;
        decision->stats.prefix_observation_node_visits++;
        filled_edges++;
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_MATCH_DECISION_PREFIX_OBSERVATION_NODE_VISIT);
        node = child;
    }

    if (compiled)
        *compiled = true;
    if (graph_edges)
        *graph_edges = filled_edges;
    *value = decision->observation_values[terminal];
    return (CettaMatchDecisionQueryState)
        decision->observation_states[terminal];
}

/* Read one terminal from the topologically evaluated observation graph.  A
 * missing stamp is a malformed or interrupted optional artifact, so selection
 * fails closed to the independent path observer. */
static CettaMatchDecisionQueryState
match_decision_query_at_compiled_path(
    CettaMatchDecision *decision,
    const CettaMatchDecisionQuery *query,
    const CettaMatchDecisionPath *path,
    uint64_t ready_arguments, Atom **value) {
    if (!path) {
        return match_decision_query_at_path(
            query, NULL, 0u, ready_arguments, value);
    }
    if (value && match_decision_has_prefix_observation(decision) &&
        decision->observation_ready &&
        path->observation_node != UINT32_MAX &&
        path->observation_node < decision->observation_node_count &&
        decision->observation_stamps[path->observation_node] ==
            decision->observation_epoch) {
        *value = decision->observation_values[path->observation_node];
        return (CettaMatchDecisionQueryState)
            decision->observation_states[path->observation_node];
    }
    return match_decision_query_at_observation_node(
        decision, query, path->path, path->path_len,
        path->observation_node, ready_arguments, value, NULL, NULL);
}

/* A repeated source variable is an equality constraint between two query
 * coordinates.  Refute only when both coordinates are observable and their
 * shaped ground observations disagree.  Unknown values retain the candidate;
 * the canonical matcher remains authoritative for every survivor. */
static bool match_decision_equality_refutes(
    CettaMatchDecision *decision,
    const CettaMatchDecisionQuery *query,
    uint64_t ready_arguments, uint32_t local_clause) {
    if (!decision || !query || !decision->equality_offsets ||
        local_clause >= decision->clause_count)
        return false;
    size_t begin = decision->equality_offsets[local_clause];
    size_t end = decision->equality_offsets[local_clause + 1u];
    if (begin > end || end > decision->equality_count)
        return false;
    for (size_t index = begin; index < end; index++) {
        CettaMatchDecisionEquality *equality =
            &decision->equalities[index];
        if (!equality->paths || equality->first_len == 0u ||
            equality->second_len == 0u)
            continue;
        decision->stats.equality_checks++;
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_MATCH_DECISION_EQUALITY_CHECK);
        Atom *first = NULL;
        Atom *second = NULL;
        bool first_compiled = false;
        bool second_compiled = false;
        uint32_t first_graph_edges = 0u;
        uint32_t second_graph_edges = 0u;
        CettaMatchDecisionQueryState first_state =
            match_decision_query_at_observation_node(
                decision, query,
                equality->paths, equality->first_len,
                equality->first_observation_node,
                ready_arguments, &first, &first_compiled,
                &first_graph_edges);
        CettaMatchDecisionQueryState second_state =
            match_decision_query_at_observation_node(
                decision, query,
                equality->paths + equality->first_len,
                equality->second_len,
                equality->second_observation_node,
                ready_arguments, &second, &second_compiled,
                &second_graph_edges);
        uint64_t compiled_reads =
            (first_compiled ? 1u : 0u) +
            (second_compiled ? 1u : 0u);
        uint64_t fallback_reads = 2u - compiled_reads;
        decision->stats.equality_observation_reads += compiled_reads;
        decision->stats.equality_observation_fallbacks += fallback_reads;
        decision->stats.equality_observation_direct_edges +=
            (uint64_t)equality->first_len + equality->second_len;
        decision->stats.equality_observation_graph_edges +=
            (uint64_t)first_graph_edges + second_graph_edges;
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_MATCH_DECISION_EQUALITY_OBSERVATION_READ,
            compiled_reads);
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_MATCH_DECISION_EQUALITY_OBSERVATION_FALLBACK,
            fallback_reads);
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_MATCH_DECISION_EQUALITY_OBSERVATION_DIRECT_EDGE,
            (uint64_t)equality->first_len + equality->second_len);
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_MATCH_DECISION_EQUALITY_OBSERVATION_GRAPH_EDGE,
            (uint64_t)first_graph_edges + second_graph_edges);
        bool refuted =
            (first_state == CETTA_MATCH_DECISION_QUERY_ABSENT &&
             second_state == CETTA_MATCH_DECISION_QUERY_VALUE) ||
            (first_state == CETTA_MATCH_DECISION_QUERY_VALUE &&
             second_state == CETTA_MATCH_DECISION_QUERY_ABSENT);
        if (!refuted &&
            first_state == CETTA_MATCH_DECISION_QUERY_VALUE &&
            second_state == CETTA_MATCH_DECISION_QUERY_VALUE &&
            first && second && !atom_has_vars(first) &&
            !atom_has_vars(second) &&
            first != second && !atom_eq(first, second)) {
            refuted = true;
        }
        if (refuted) {
            decision->stats.equality_refutations++;
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_MATCH_DECISION_EQUALITY_REFUTATION);
            return true;
        }
    }
    return false;
}

static bool match_decision_exact_key_plan_enabled(void) {
#ifdef CETTA_MATCH_DECISION_DISABLE_EXACT_KEY_INDEX
    return false;
#else
    return CETTA_MATCH_DECISION_POLICY_EXACT_KEY_PLAN_V1 == 1;
#endif
}

static uint32_t match_decision_path_exact_keys(
    CettaMatchDecision *decision,
    const CettaMatchDecisionPath *path, Atom *value,
    bool value_absent,
    const CettaMatchDecisionKey *keys[2]) {
    if (!decision || !path || !keys || value_absent || !value)
        return 0u;
    uint32_t count = 0u;
    if (value->kind != ATOM_EXPR) {
        const CettaMatchDecisionKey *literal =
            match_decision_path_find_key(
                path, CETTA_MATCH_DECISION_KEY_LITERAL,
                0u, value,
                &decision->stats.key_index_select_probes);
        if (literal)
            keys[count++] = literal;
        return count;
    }

    const CettaMatchDecisionKey *arity =
        match_decision_path_find_key(
            path, CETTA_MATCH_DECISION_KEY_EXPR_ARITY,
            value->expr.len, NULL,
            &decision->stats.key_index_select_probes);
    if (arity)
        keys[count++] = arity;
    Atom *head = value->expr.len > 0u
        ? value->expr.elems[0] : NULL;
    if (head && head->kind != ATOM_VAR && head->kind != ATOM_EXPR) {
        const CettaMatchDecisionKey *headed =
            match_decision_path_find_key(
                path, CETTA_MATCH_DECISION_KEY_EXPR_HEAD,
                value->expr.len, head,
                &decision->stats.key_index_select_probes);
        if (headed)
            keys[count++] = headed;
    }
    return count;
}

static bool match_decision_add_ref_list(
    CettaMatchDecisionRefList *lists, uint32_t list_capacity,
    uint32_t *list_count, uint32_t *accepted_count,
    const uint32_t *refs, uint32_t count) {
    if (!list_count || !accepted_count)
        return false;
    if (count == 0u)
        return true;
    if (!lists || *list_count >= list_capacity ||
        *accepted_count > UINT32_MAX - count) {
        return false;
    }
    lists[(*list_count)++] = (CettaMatchDecisionRefList){
        .refs = refs,
        .count = count,
    };
    *accepted_count += count;
    return true;
}

static bool match_decision_path_exact_lists(
    CettaMatchDecision *decision,
    const CettaMatchDecisionPath *path, Atom *value,
    bool value_absent,
    CettaMatchDecisionRefList *lists, uint32_t list_capacity,
    uint32_t *list_count, uint32_t *accepted_count) {
    if (!decision || !path || !lists || list_capacity == 0u ||
        !list_count || !accepted_count) {
        return false;
    }
    *list_count = 0u;
    *accepted_count = 0u;
    CettaMatchDecisionQueryState query_state = value_absent
        ? CETTA_MATCH_DECISION_QUERY_ABSENT
        : CETTA_MATCH_DECISION_QUERY_VALUE;
    if (path->wildcard_count > 0u &&
        match_decision_policy(query_state, NULL, value) !=
            CETTA_MD_POLICY_REFUTE) {
        if (!match_decision_add_ref_list(
                lists, list_capacity, list_count, accepted_count,
                path->wildcard_refs, path->wildcard_count)) {
            return false;
        }
    }

    const CettaMatchDecisionKey *keys[2] = {NULL, NULL};
    uint32_t key_count = match_decision_path_exact_keys(
        decision, path, value, value_absent, keys);
    for (uint32_t index = 0u; index < key_count; index++) {
        if (!match_decision_add_ref_list(
                lists, list_capacity, list_count, accepted_count,
                keys[index]->clause_refs, keys[index]->clause_count)) {
            return false;
        }
    }
    return true;
}

static bool match_decision_path_generic_lists(
    CettaMatchDecision *decision,
    const CettaMatchDecisionPath *path, Atom *value,
    bool value_absent,
    CettaMatchDecisionRefList *lists, uint32_t list_capacity,
    uint32_t *list_count, uint32_t *accepted_count) {
    if (!decision || !path || !lists || list_capacity == 0u ||
        !list_count || !accepted_count) {
        return false;
    }
    *list_count = 0u;
    *accepted_count = 0u;
    CettaMatchDecisionQueryState query_state = value_absent
        ? CETTA_MATCH_DECISION_QUERY_ABSENT
        : CETTA_MATCH_DECISION_QUERY_VALUE;
    if (path->wildcard_count > 0u &&
        match_decision_policy(query_state, NULL, value) !=
            CETTA_MD_POLICY_REFUTE &&
        !match_decision_add_ref_list(
            lists, list_capacity, list_count, accepted_count,
            path->wildcard_refs, path->wildcard_count)) {
        return false;
    }
    for (uint32_t key = 0u; key < path->key_count; key++) {
        decision->stats.generic_key_policy_scans++;
        if (match_decision_policy(
                query_state, &path->keys[key], value) ==
            CETTA_MD_POLICY_REFUTE)
            continue;
        if (!match_decision_add_ref_list(
                lists, list_capacity, list_count, accepted_count,
                path->keys[key].clause_refs,
                path->keys[key].clause_count)) {
            return false;
        }
    }
    return true;
}

static bool match_decision_path_lists(
    CettaMatchDecision *decision,
    const CettaMatchDecisionPath *path, Atom *value,
    bool value_absent,
    CettaMatchDecisionRefList *lists, uint32_t list_capacity,
    uint32_t *list_count, uint32_t *accepted_count) {
    return match_decision_exact_key_plan_enabled()
        ? match_decision_path_exact_lists(
              decision, path, value, value_absent,
              lists, list_capacity, list_count, accepted_count)
        : match_decision_path_generic_lists(
              decision, path, value, value_absent,
              lists, list_capacity, list_count, accepted_count);
}

static bool match_decision_path_exact_candidate_count(
    CettaMatchDecision *decision,
    const CettaMatchDecisionPath *path, Atom *value,
    bool value_absent, uint32_t *accepted_count) {
    if (!decision || !path || !accepted_count)
        return false;
    CettaMatchDecisionQueryState query_state = value_absent
        ? CETTA_MATCH_DECISION_QUERY_ABSENT
        : CETTA_MATCH_DECISION_QUERY_VALUE;
    uint64_t accepted =
        match_decision_policy(query_state, NULL, value) !=
            CETTA_MD_POLICY_REFUTE
        ? path->wildcard_count : 0u;

    const CettaMatchDecisionKey *keys[2] = {NULL, NULL};
    uint32_t key_count = match_decision_path_exact_keys(
        decision, path, value, value_absent, keys);
    for (uint32_t index = 0u; index < key_count; index++)
        accepted += keys[index]->clause_count;
    if (accepted > UINT32_MAX)
        return false;
    *accepted_count = (uint32_t)accepted;
    return true;
}

static bool match_decision_path_generic_candidate_count(
    CettaMatchDecision *decision,
    const CettaMatchDecisionPath *path, Atom *value,
    bool value_absent, uint32_t *accepted_count) {
    if (!decision || !path || !accepted_count)
        return false;
    CettaMatchDecisionQueryState query_state = value_absent
        ? CETTA_MATCH_DECISION_QUERY_ABSENT
        : CETTA_MATCH_DECISION_QUERY_VALUE;
    uint32_t accepted =
        match_decision_policy(query_state, NULL, value) !=
            CETTA_MD_POLICY_REFUTE
        ? path->wildcard_count : 0u;
    for (uint32_t key = 0u; key < path->key_count; key++) {
        decision->stats.generic_key_policy_scans++;
        if (match_decision_policy(
                query_state, &path->keys[key], value) ==
            CETTA_MD_POLICY_REFUTE)
            continue;
        if (accepted > UINT32_MAX - path->keys[key].clause_count)
            return false;
        accepted += path->keys[key].clause_count;
    }
    *accepted_count = accepted;
    return true;
}

static bool match_decision_path_candidate_count(
    CettaMatchDecision *decision,
    const CettaMatchDecisionPath *path, Atom *value,
    bool value_absent, uint32_t *accepted_count) {
    return match_decision_exact_key_plan_enabled()
        ? match_decision_path_exact_candidate_count(
              decision, path, value, value_absent, accepted_count)
        : match_decision_path_generic_candidate_count(
              decision, path, value, value_absent, accepted_count);
}

static bool match_decision_bits_add_refs(
    CettaMatchDecision *decision, uint64_t *bits,
    const uint32_t *refs, uint32_t count) {
    if (!decision || !bits || (count > 0u && !refs))
        return false;
    for (uint32_t index = 0u; index < count; index++) {
        uint32_t clause = refs[index];
        if (clause >= decision->clause_count)
            return false;
        bits[clause / 64u] |= UINT64_C(1) << (clause % 64u);
    }
    return true;
}

static bool match_decision_conjunctive_candidates(
    CettaMatchDecision *decision, const CettaMatchDecisionQuery *query,
    uint64_t ready_arguments, size_t *candidate_count,
    bool *observed_path) {
    if (!decision || !query || !candidate_count || !observed_path ||
        !decision->candidate_bits || !decision->path_bits ||
        decision->bit_word_count == 0u) {
        return false;
    }
    *candidate_count = 0u;
    *observed_path = false;
    for (size_t word = 0u; word < decision->bit_word_count; word++)
        decision->candidate_bits[word] = UINT64_MAX;
    size_t tail_bits = decision->clause_count % 64u;
    if (tail_bits != 0u) {
        decision->candidate_bits[decision->bit_word_count - 1u] =
            (UINT64_C(1) << tail_bits) - 1u;
    }

    for (size_t path_index = 0u;
         path_index < decision->path_count; path_index++) {
        CettaMatchDecisionPath *path = &decision->paths[path_index];
        Atom *value = NULL;
        CettaMatchDecisionQueryState query_state =
            match_decision_query_at_compiled_path(
                decision, query, path, ready_arguments, &value);
        if (query_state == CETTA_MATCH_DECISION_QUERY_UNKNOWN) {
            decision->stats.unavailable_path_fallbacks++;
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_MATCH_DECISION_UNAVAILABLE_PATH);
            continue;
        }
        *observed_path = true;
        bool value_absent =
            query_state == CETTA_MATCH_DECISION_QUERY_ABSENT;
        memset(decision->path_bits, 0,
               decision->bit_word_count * sizeof(uint64_t));
        if (match_decision_policy(query_state, NULL, value) !=
            CETTA_MD_POLICY_REFUTE) {
            memcpy(decision->path_bits, path->wildcard_bits,
                   decision->bit_word_count * sizeof(uint64_t));
        }
        if (match_decision_exact_key_plan_enabled()) {
            const CettaMatchDecisionKey *keys[2] = {NULL, NULL};
            uint32_t key_count = match_decision_path_exact_keys(
                decision, path, value, value_absent, keys);
            for (uint32_t key = 0u; key < key_count; key++) {
                if (!match_decision_bits_add_refs(
                        decision, decision->path_bits,
                        keys[key]->clause_refs,
                        keys[key]->clause_count)) {
                    return false;
                }
            }
        } else {
            for (uint32_t key = 0u; key < path->key_count; key++) {
                decision->stats.generic_key_policy_scans++;
                if (match_decision_policy(
                        query_state, &path->keys[key], value) ==
                    CETTA_MD_POLICY_REFUTE) {
                    continue;
                }
                if (!match_decision_bits_add_refs(
                        decision, decision->path_bits,
                        path->keys[key].clause_refs,
                        path->keys[key].clause_count)) {
                    return false;
                }
            }
        }
        bool any = false;
        for (size_t word = 0u;
             word < decision->bit_word_count; word++) {
            decision->candidate_bits[word] &= decision->path_bits[word];
            any = any || decision->candidate_bits[word] != 0u;
        }
        if (!any)
            break;
    }

    if (!*observed_path)
        return true;
    size_t count = 0u;
    for (size_t word = 0u; word < decision->bit_word_count; word++) {
        uint64_t remaining = decision->candidate_bits[word];
        while (remaining != 0u) {
            remaining &= remaining - 1u;
            count++;
        }
    }
    if (!match_decision_reserve(
            (void **)&decision->candidate_locals,
            &decision->candidate_local_capacity,
            count, sizeof(*decision->candidate_locals))) {
        return false;
    }
    size_t write = 0u;
    for (size_t clause = 0u; clause < decision->clause_count; clause++) {
        if ((decision->candidate_bits[clause / 64u] &
             (UINT64_C(1) << (clause % 64u))) != 0u) {
            decision->candidate_locals[write++] = (uint32_t)clause;
        }
    }
    if (write != count)
        return false;
    *candidate_count = count;
    return true;
}

static bool match_decision_merge_lists(
    CettaMatchDecision *decision,
    CettaMatchDecisionRefList *lists, uint32_t list_count,
    uint32_t expected_count, size_t *result_count) {
    if (!decision || !lists || !result_count ||
        !match_decision_reserve(
            (void **)&decision->candidate_locals,
            &decision->candidate_local_capacity,
            expected_count, sizeof(*decision->candidate_locals))) {
        return false;
    }
    size_t out = 0u;
    for (;;) {
        uint32_t best = UINT32_MAX;
        int best_list = -1;
        for (uint32_t list = 0u; list < list_count; list++) {
            if (lists[list].cursor >= lists[list].count)
                continue;
            uint32_t candidate = lists[list].refs[lists[list].cursor];
            if (best_list < 0 || candidate < best) {
                best = candidate;
                best_list = (int)list;
            }
        }
        if (best_list < 0)
            break;
        if (out >= expected_count)
            return false;
        decision->candidate_locals[out++] = best;
        lists[best_list].cursor++;
    }
    if (out != expected_count)
        return false;
    *result_count = out;
    return true;
}

static bool match_decision_linear_candidates(
    CettaMatchDecision *decision, size_t *candidate_count) {
    if (!decision || !candidate_count ||
        !match_decision_reserve(
            (void **)&decision->candidate_locals,
            &decision->candidate_local_capacity,
            decision->clause_count,
            sizeof(*decision->candidate_locals))) {
        return false;
    }
    for (size_t clause = 0u; clause < decision->clause_count; clause++)
        decision->candidate_locals[clause] = (uint32_t)clause;
    *candidate_count = decision->clause_count;
    return true;
}

static CettaMatchDecisionSelectState match_decision_select_query(
    CettaMatchDecision *decision, const Space *live_space,
    CettaMatchDecisionSemanticIdentity semantic_identity,
    const CettaMatchDecisionQuery *query, uint64_t ready_arguments,
    CettaMatchDecisionVerifyCandidateFn verify,
    void *verify_context,
    const uint32_t **source_refs, size_t *source_ref_count) {
    if (source_refs)
        *source_refs = NULL;
    if (source_ref_count)
        *source_ref_count = 0u;
    if (!decision || !live_space || !query ||
        (!query->whole && !query->head) ||
        !source_refs || !source_ref_count)
        return CETTA_MATCH_DECISION_SELECT_ERROR;
    if (!cetta_match_decision_is_current(
            decision, live_space, semantic_identity)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_MATCH_DECISION_INVALIDATION);
        return CETTA_MATCH_DECISION_SELECT_INVALIDATED;
    }

    decision->stats.runs++;
    decision->stats.clause_inputs += decision->clause_count;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_MATCH_DECISION_RUN);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_MATCH_DECISION_CLAUSE_INPUT,
        decision->clause_count);
    match_decision_prefix_observation_begin(
        decision, query, ready_arguments);
    size_t local_count = 0u;
    bool selected_pivot = false;
    uint32_t best_count = (uint32_t)decision->clause_count;
    size_t best_path = SIZE_MAX;

    if (decision->mode == CETTA_MATCH_DECISION_DEEP &&
        decision->path_count > 0u) {
        for (size_t path_index = 0u;
             path_index < decision->path_count; path_index++) {
            CettaMatchDecisionPath *path = &decision->paths[path_index];
            Atom *value = NULL;
            CettaMatchDecisionQueryState query_state =
                match_decision_query_at_compiled_path(
                    decision, query, path, ready_arguments, &value);
            if (query_state == CETTA_MATCH_DECISION_QUERY_UNKNOWN) {
                if (match_decision_policy(
                        query_state, NULL, NULL) !=
                    CETTA_MD_POLICY_FALLBACK) {
                    return CETTA_MATCH_DECISION_SELECT_ERROR;
                }
                decision->stats.unavailable_path_fallbacks++;
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_MATCH_DECISION_UNAVAILABLE_PATH);
                continue;
            }
            uint32_t accepted = 0u;
            if (!match_decision_path_candidate_count(
                    decision, path, value,
                    query_state == CETTA_MATCH_DECISION_QUERY_ABSENT,
                    &accepted)) {
                return CETTA_MATCH_DECISION_SELECT_ERROR;
            }
            if (!selected_pivot || accepted < best_count) {
                best_count = accepted;
                best_path = path_index;
                selected_pivot = true;
            }
            /* The lane-owned verifier checks every survivor against the
             * complete structural pattern.  Once a path leaves at most one
             * candidate, later paths cannot reduce matcher work: the verifier
             * already distinguishes that survivor from the empty set. */
            if (accepted <= 1u)
                break;
        }
    }

    bool ok = true;
    if (decision->mode == CETTA_MATCH_DECISION_CONJUNCTIVE) {
        bool observed_path = false;
        ok = match_decision_conjunctive_candidates(
            decision, query, ready_arguments,
            &local_count, &observed_path);
        if (ok && !observed_path) {
            decision->stats.linear_fallbacks++;
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_MATCH_DECISION_LINEAR_FALLBACK);
            ok = match_decision_linear_candidates(
                decision, &local_count);
        }
    } else if (!selected_pivot) {
        decision->stats.linear_fallbacks++;
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_MATCH_DECISION_LINEAR_FALLBACK);
        ok = match_decision_linear_candidates(decision, &local_count);
    } else if (best_count == 0u) {
        local_count = 0u;
    } else {
        if (best_path >= decision->path_count)
            return CETTA_MATCH_DECISION_SELECT_ERROR;
        CettaMatchDecisionPath *path = &decision->paths[best_path];
        uint32_t list_capacity = match_decision_exact_key_plan_enabled()
            ? 3u : path->key_count + 1u;
        if (!match_decision_reserve(
                (void **)&decision->working_lists,
                &decision->working_list_capacity,
                list_capacity, sizeof(*decision->working_lists))) {
            return CETTA_MATCH_DECISION_SELECT_ERROR;
        }
        memset(decision->working_lists, 0,
               sizeof(*decision->working_lists) * list_capacity);
        Atom *value = NULL;
        CettaMatchDecisionQueryState query_state =
            match_decision_query_at_compiled_path(
                decision, query, path, ready_arguments, &value);
        if (query_state == CETTA_MATCH_DECISION_QUERY_UNKNOWN)
            return CETTA_MATCH_DECISION_SELECT_ERROR;
        uint32_t accepted = 0u;
        uint32_t list_count = 0u;
        bool listed = match_decision_path_lists(
            decision, path, value,
            query_state == CETTA_MATCH_DECISION_QUERY_ABSENT,
            decision->working_lists, list_capacity,
            &list_count, &accepted);
        ok = listed && accepted == best_count &&
             match_decision_merge_lists(
                 decision, decision->working_lists, list_count,
                 best_count, &local_count);
    }
    if (!ok || !match_decision_reserve(
            (void **)&decision->candidate_sources,
            &decision->candidate_source_capacity,
            local_count, sizeof(*decision->candidate_sources))) {
        return CETTA_MATCH_DECISION_SELECT_ERROR;
    }

    size_t write = 0u;
    for (size_t index = 0u; index < local_count; index++) {
        uint32_t local = decision->candidate_locals[index];
        if (local >= decision->clause_count)
            return CETTA_MATCH_DECISION_SELECT_ERROR;
        const CettaMatchDecisionClause *clause =
            &decision->clauses[local];
        if (match_decision_equality_refutes(
                decision, query, ready_arguments, local)) {
            continue;
        }
        if (verify && !verify(
                verify_context, clause->source_ref,
                clause->pattern, query->whole)) {
            continue;
        }
        decision->candidate_sources[write++] = clause->source_ref;
    }
    decision->stats.clause_survivors += write;
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_MATCH_DECISION_CLAUSE_SURVIVOR, write);
    *source_refs = decision->candidate_sources;
    *source_ref_count = write;
    return CETTA_MATCH_DECISION_SELECT_READY;
}

CettaMatchDecisionSelectState cetta_match_decision_select(
    CettaMatchDecision *decision, const Space *live_space,
    CettaMatchDecisionSemanticIdentity semantic_identity,
    Atom *query, uint64_t ready_arguments,
    CettaMatchDecisionVerifyCandidateFn verify,
    void *verify_context,
    const uint32_t **source_refs, size_t *source_ref_count) {
    CettaMatchDecisionQuery view = {
        .whole = query,
    };
    return match_decision_select_query(
        decision, live_space, semantic_identity,
        &view, ready_arguments, verify, verify_context,
        source_refs, source_ref_count);
}

CettaMatchDecisionSelectState cetta_match_decision_select_parts(
    CettaMatchDecision *decision, const Space *live_space,
    CettaMatchDecisionSemanticIdentity semantic_identity,
    Atom *head, Atom *const *arguments, size_t arity,
    uint64_t ready_arguments,
    const uint32_t **source_refs, size_t *source_ref_count) {
    CettaMatchDecisionQuery view = {
        .head = head,
        .arguments = arguments,
        .arity = arity,
    };
    return match_decision_select_query(
        decision, live_space, semantic_identity,
        &view, ready_arguments, NULL, NULL,
        source_refs, source_ref_count);
}

void cetta_match_decision_stats(
    const CettaMatchDecision *decision,
    CettaMatchDecisionStats *stats) {
    if (!stats)
        return;
    *stats = decision
        ? decision->stats : (CettaMatchDecisionStats){0};
}
