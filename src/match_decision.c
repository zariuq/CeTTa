#include "match_decision.h"
#include "stats.h"
#include "generated/match_decision_policy_v1.generated.h"

#include <stdlib.h>
#include <string.h>

enum {
    CETTA_MATCH_DECISION_DEFAULT_MAX_DEPTH = 7u,
    CETTA_MATCH_DECISION_HARD_MAX_DEPTH = 32u,
    CETTA_MATCH_DECISION_MAX_PATHS = 4096u,
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
    CettaMatchDecisionKey *keys;
    uint32_t key_count;
    uint32_t key_capacity;
    uint32_t *wildcard_refs;
    uint32_t wildcard_count;
    uint32_t wildcard_capacity;
} CettaMatchDecisionPath;

typedef struct {
    const uint32_t *refs;
    uint32_t count;
    uint32_t cursor;
} CettaMatchDecisionRefList;

struct CettaMatchDecision {
    SpaceReadToken read;
    CettaMatchDecisionSemanticIdentity semantic_identity;
    CettaMatchDecisionMode mode;
    uint32_t max_depth;
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

static CettaMatchDecisionKey *match_decision_path_key(
    CettaMatchDecisionPath *descriptor,
    CettaMatchDecisionKeyKind kind,
    CettaExprLen expression_length, Atom *atom) {
    if (!descriptor)
        return NULL;
    for (uint32_t index = 0u; index < descriptor->key_count; index++) {
        if (match_decision_key_equal(
                &descriptor->keys[index], kind,
                expression_length, atom)) {
            return &descriptor->keys[index];
        }
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
            descriptor, kind, expression_length, key_atom);
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
    free(path->keys);
    free(path->wildcard_refs);
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

CettaMatchDecision *cetta_match_decision_compile(
    SpaceReadToken read,
    CettaMatchDecisionSemanticIdentity semantic_identity,
    const CettaMatchDecisionClause *clauses,
    size_t clause_count,
    CettaMatchDecisionMode mode,
    uint32_t max_depth,
    CettaMatchDecisionClassifyPatternFn classify,
    void *classify_context) {
    if (!clauses || clause_count == 0u || clause_count > UINT32_MAX ||
        !space_read_token_is_current(read) ||
        (mode != CETTA_MATCH_DECISION_LINEAR &&
         mode != CETTA_MATCH_DECISION_DEEP)) {
        return NULL;
    }
    if (max_depth == 0u)
        max_depth = CETTA_MATCH_DECISION_DEFAULT_MAX_DEPTH;
    if (max_depth > CETTA_MATCH_DECISION_HARD_MAX_DEPTH)
        return NULL;

    CettaMatchDecision *decision = calloc(1u, sizeof(*decision));
    if (!decision)
        return NULL;
    decision->read = read;
    decision->semantic_identity = semantic_identity;
    decision->mode = mode;
    decision->max_depth = max_depth;
    decision->clauses = malloc(sizeof(*decision->clauses) * clause_count);
    if (!decision->clauses) {
        cetta_match_decision_free(decision);
        return NULL;
    }
    memcpy(decision->clauses, clauses,
           sizeof(*decision->clauses) * clause_count);
    decision->clause_count = clause_count;
    decision->stats.compilations = 1u;

    if (mode == CETTA_MATCH_DECISION_DEEP) {
        if (!match_decision_gather_paths(
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
    }
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_MATCH_DECISION_COMPILE);
    return decision;
}

void cetta_match_decision_free(CettaMatchDecision *decision) {
    if (!decision)
        return;
    for (size_t path = 0u; path < decision->path_count; path++)
        match_decision_path_free(&decision->paths[path]);
    free(decision->paths);
    free(decision->clauses);
    free(decision->candidate_locals);
    free(decision->candidate_sources);
    free(decision->working_lists);
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

static uint32_t match_decision_path_lists(
    const CettaMatchDecisionPath *path, Atom *value,
    bool value_absent,
    CettaMatchDecisionRefList *lists, uint32_t list_capacity,
    uint32_t *accepted_count) {
    if (!path || !lists || list_capacity == 0u || !accepted_count)
        return 0u;
    uint32_t list_count = 0u;
    *accepted_count = 0u;
    CettaMatchDecisionQueryState query_state = value_absent
        ? CETTA_MATCH_DECISION_QUERY_ABSENT
        : CETTA_MATCH_DECISION_QUERY_VALUE;
    if (path->wildcard_count > 0u &&
        match_decision_policy(query_state, NULL, value) !=
            CETTA_MD_POLICY_REFUTE) {
        lists[list_count++] = (CettaMatchDecisionRefList){
            .refs = path->wildcard_refs,
            .count = path->wildcard_count,
        };
        *accepted_count = path->wildcard_count;
    }
    for (uint32_t key = 0u; key < path->key_count; key++) {
        if (match_decision_policy(
                query_state, &path->keys[key], value) ==
            CETTA_MD_POLICY_REFUTE)
            continue;
        if (list_count >= list_capacity ||
            *accepted_count > UINT32_MAX -
                path->keys[key].clause_count) {
            return 0u;
        }
        lists[list_count++] = (CettaMatchDecisionRefList){
            .refs = path->keys[key].clause_refs,
            .count = path->keys[key].clause_count,
        };
        *accepted_count += path->keys[key].clause_count;
    }
    return list_count;
}

static uint32_t match_decision_path_candidate_count(
    const CettaMatchDecisionPath *path, Atom *value,
    bool value_absent) {
    if (!path)
        return 0u;
    CettaMatchDecisionQueryState query_state = value_absent
        ? CETTA_MATCH_DECISION_QUERY_ABSENT
        : CETTA_MATCH_DECISION_QUERY_VALUE;
    uint32_t accepted =
        match_decision_policy(query_state, NULL, value) !=
            CETTA_MD_POLICY_REFUTE
        ? path->wildcard_count : 0u;
    for (uint32_t key = 0u; key < path->key_count; key++) {
        if (match_decision_policy(
                query_state, &path->keys[key], value) ==
            CETTA_MD_POLICY_REFUTE)
            continue;
        if (accepted > UINT32_MAX - path->keys[key].clause_count)
            return UINT32_MAX;
        accepted += path->keys[key].clause_count;
    }
    return accepted;
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
                match_decision_query_at_path(
                    query, path->path, path->path_len,
                    ready_arguments, &value);
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
            uint32_t accepted =
                match_decision_path_candidate_count(
                    path, value,
                    query_state == CETTA_MATCH_DECISION_QUERY_ABSENT);
            if (!selected_pivot || accepted < best_count) {
                best_count = accepted;
                best_path = path_index;
                selected_pivot = true;
            }
            if (accepted == 0u)
                break;
        }
    }

    bool ok = true;
    if (!selected_pivot) {
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
        uint32_t list_capacity = path->key_count + 1u;
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
            match_decision_query_at_path(
                query, path->path, path->path_len,
                ready_arguments, &value);
        if (query_state == CETTA_MATCH_DECISION_QUERY_UNKNOWN)
            return CETTA_MATCH_DECISION_SELECT_ERROR;
        uint32_t accepted = 0u;
        uint32_t list_count = match_decision_path_lists(
            path, value,
            query_state == CETTA_MATCH_DECISION_QUERY_ABSENT,
            decision->working_lists, list_capacity, &accepted);
        ok = accepted == best_count &&
             !(list_count == 0u && accepted != 0u) &&
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
