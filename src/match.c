#include "match.h"
#include "binding/slot_store_internal.h"
#include "stats.h"
#include "term_universe.h"
#include "term_canon.h"
#include "variant_shape.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── Bindings ───────────────────────────────────────────────────────────── */

/* Cycle-safety ownership is intentionally split by representation:
 *
 * - bindings_has_loop rejects cycles in the metavariable substitution graph;
 * - bindings_dereference_limit bounds variable-only graph walks by the size of
 *   that graph, catching recurrence without imposing a semantic depth budget;
 * - MatchPathSet rejects recurrence on the active structural-match path while
 *   permitting shared finite DAG subterms;
 * - the object-binder ABT traversals in abt.c reject cyclic term graphs and
 *   enforce de Bruijn scope, but do not own metavariable substitutions.
 *
 * These guards cover different graphs and are not interchangeable.  Removing
 * one requires a falsifier showing that another guard covers the same graph.
 */

#define BINDINGS_MIN_CAPACITY 8
#define BINDINGS_SEEN_STACK_CAP 32
#define BINDINGS_TEMP_STACK_CAP 32
#define BINDINGS_POOL_CLASS_COUNT 4
#define BINDINGS_MEMO_STACK_CAP 32
#define BINDINGS_MEMO_INDEX_THRESHOLD 16u
#define BINDINGS_LOOKUP_INDEX_THRESHOLD 16u
#define FRESHEN_EPOCH_MEMO_INLINE_CAP 64u
#define VAR_ID_SET_INLINE_CAP 8u
#define VAR_ID_SET_HASH_THRESHOLD 16u

enum {
    BINDINGS_CYCLE_UNKNOWN = 0u,
    BINDINGS_CYCLE_ACYCLIC = 1u,
    BINDINGS_CYCLE_PRESENT = 2u,
};

enum {
    BINDINGS_DERIVED_PRIVATE_ENTRY_NONZERO = 1u << 1,
    BINDINGS_DERIVED_PRIVATE_CONSTRAINT_NONZERO = 1u << 2,
};

typedef enum {
    BINDINGS_REACHABILITY_UNKNOWN = 0,
    BINDINGS_REACHABILITY_ABSENT = 1,
    BINDINGS_REACHABILITY_PRESENT = 2,
} BindingsReachability;

typedef struct {
    VarId id;
    union {
        VarId single_terminal;
        uint64_t closed_prefix;
    };
    uint32_t index_plus_one;
    uint32_t single_cache_kind;
} BindingsLookupIndexSlot;

enum {
    BINDINGS_SINGLE_REACH_CACHE_NONE = 0u,
    BINDINGS_SINGLE_REACH_CACHE_TERMINAL = 1u,
    BINDINGS_SINGLE_REACH_CACHE_GROUND = 2u,
    BINDINGS_SINGLE_REACH_CACHE_COMPLEX = 3u,
    BINDINGS_SINGLE_REACH_CACHE_KIND_MASK = 3u,
    BINDINGS_SINGLE_REACH_CACHE_LISTED = UINT32_C(1) << 31u,
};

struct BindingsLookupIndex {
    _Atomic uint32_t references;
    BindingsLookupIndexSlot *slots;
    size_t capacity;
    uint32_t count;
    uint32_t synced_len;
    bool has_duplicates;
    VarId *single_cached_ids;
    size_t single_cached_len;
    size_t single_cached_cap;
};


typedef struct {
    /* A local write is represented only by its authoritative frame slot.
     * UINT32_MAX denotes the exceptional write to an outer frame, whose
     * payload must remain private until the candidate freezes. */
    uint32_t local_slot;
    VarId external_id;
    SymbolId external_spelling;
    Atom *external_name_key;
    BindingValue external_value;
} BindingsExclusiveWrite;

/* The candidate-local realization of BindingVersions.ExclusiveRegion.  The
 * immutable schema is borrowed from the compiled equation.  `values` is the
 * direct current image for rule slots. `writes` preserves their exact order
 * without manufacturing Binding rows; only an exceptional write to an outer
 * frame carries a payload in the write log.  Neither array is visible through
 * Bindings until publication. */
struct BindingsExclusiveFrame {
    BindingsFrameSchema *schema;
    BindingValue *values;
    uint32_t value_cap;
    BindingsExclusiveWrite *writes;
    uint32_t write_len;
    uint32_t write_cap;
    uint32_t epoch;
    bool owns_identity;
    bool active;
    /* The identity was minted for this activation, so no binding outside the
     * frame mentions its variables.  While no stored value mentions them
     * either, a value that avoids the frame cannot reach a local slot: the
     * occurs check at such a local write, and again at publication, has
     * nothing to find.  The first stored value that mentions the frame, at
     * a local slot or an outer one, ends the certificate. */
    bool fresh;
    bool frame_mentioned;
};

static __thread int g_bindings_lookup_index_enabled = -1;
#if !defined(CETTA_MUTATION_BINDINGS_SINGLE_REACH_KEEP_ROLLBACK_CACHE)
static __thread int g_bindings_single_reach_support_invalidation_enabled = -1;
#endif
static __thread int g_match_shared_ground_reflexivity_enabled = -1;
static __thread int g_bindings_cycle_plan_support_enabled = -1;
static __thread size_t *g_bindings_single_reach_path;
static __thread size_t g_bindings_single_reach_path_cap;
static _Atomic uint64_t g_bindings_builder_instance_counter = 1u;

static bool bindings_frame_index_lookup_context_id(
        const Bindings *bindings, BindingValue context, VarId id,
        BindingValue *value_out);

static bool match_shared_ground_reflexivity_enabled(void) {
    if (g_match_shared_ground_reflexivity_enabled >= 0)
        return g_match_shared_ground_reflexivity_enabled != 0;
    const char *reference = getenv(
        "CETTA_MATCH_SHARED_GROUND_REFLEXIVITY_REFERENCE");
    g_match_shared_ground_reflexivity_enabled =
        !(reference && reference[0] != '\0' && reference[0] != '0');
    return g_match_shared_ground_reflexivity_enabled != 0;
}

static bool bindings_cycle_plan_support_enabled(void) {
    if (g_bindings_cycle_plan_support_enabled >= 0)
        return g_bindings_cycle_plan_support_enabled != 0;
    const char *reference = getenv(
        "CETTA_BINDINGS_CYCLE_PLAN_SUPPORT_REFERENCE");
    g_bindings_cycle_plan_support_enabled =
        !(reference && reference[0] != '\0' && reference[0] != '0');
    return g_bindings_cycle_plan_support_enabled != 0;
}

/* Published hash-cons entries are immutable DAG nodes.  Their global
 * occurrence identity therefore certifies both sides denote the same closed
 * structure.  Arena-local terms are deliberately excluded: even a stale
 * variable summary must not bypass the matcher's cycle audit. */
static bool match_shared_ground_reflexivity_certified(const Atom *atom) {
    const uint32_t required =
        ATOM_FLAG_HASHCONS_ELIGIBLE | ATOM_FLAG_ARENA_CLOSED;
    return atom && atom->kind == ATOM_EXPR && atom->arena_id == 0u &&
        !atom_has_vars(atom) && (atom->flags & required) == required;
}

/*
 * Same-occurrence identity is reflexivity of matching (SubstitutionAlgebra
 * subst is the identity on a term).  A finite DAG may re-enter through
 * sibling paths; a directed cycle must still fail the active-path audit.
 * Interned closed ground is already certified.  Eval-heap terms are certified
 * here by a path-colour DFS: gray is the active ancestor chain, black is a
 * finished subgraph.  Mutation-induced cycles stay visible.
 */
typedef struct {
    const Atom *atom;
    CettaExprIndex next_child;
} MatchDagFrame;

enum {
    MATCH_DAG_COLOR_EMPTY = 0,
    MATCH_DAG_COLOR_GRAY = 1,
    MATCH_DAG_COLOR_BLACK = 2
};

static __thread MatchDagFrame *g_match_dag_stack;
static __thread uint32_t g_match_dag_stack_cap;
static __thread const Atom **g_match_dag_keys;
static __thread uint8_t *g_match_dag_colors;
static __thread uint32_t *g_match_dag_stamp;
static __thread uint32_t g_match_dag_hash_cap;
static __thread uint32_t g_match_dag_hash_used;
static __thread uint32_t g_match_dag_epoch;

static uint32_t match_atom_ptr_probe(const Atom *atom) {
    uintptr_t x = (uintptr_t)atom;
    x ^= x >> 16;
    x *= UINT64_C(0x9e3779b97f4a7c15);
    x ^= x >> 32;
    return (uint32_t)x;
}

static bool match_dag_hash_grow(void);

static uint8_t match_dag_color(const Atom *atom) {
    uint32_t mask;
    uint32_t i;
    if (!atom || g_match_dag_hash_cap == 0u || g_match_dag_epoch == 0u)
        return MATCH_DAG_COLOR_EMPTY;
    mask = g_match_dag_hash_cap - 1u;
    i = match_atom_ptr_probe(atom) & mask;
    for (;;) {
        if (g_match_dag_stamp[i] != g_match_dag_epoch)
            return MATCH_DAG_COLOR_EMPTY;
        if (g_match_dag_keys[i] == atom)
            return g_match_dag_colors[i];
        i = (i + 1u) & mask;
    }
}

static bool match_dag_set_color(const Atom *atom, uint8_t color) {
    uint32_t mask;
    uint32_t i;
    if (!atom)
        return false;
    if (g_match_dag_hash_cap == 0u ||
        g_match_dag_hash_used * 2u > g_match_dag_hash_cap) {
        if (!match_dag_hash_grow())
            return false;
    }
    mask = g_match_dag_hash_cap - 1u;
    i = match_atom_ptr_probe(atom) & mask;
    for (;;) {
        if (g_match_dag_stamp[i] != g_match_dag_epoch) {
            g_match_dag_keys[i] = atom;
            g_match_dag_colors[i] = color;
            g_match_dag_stamp[i] = g_match_dag_epoch;
            g_match_dag_hash_used++;
            return true;
        }
        if (g_match_dag_keys[i] == atom) {
            g_match_dag_colors[i] = color;
            return true;
        }
        i = (i + 1u) & mask;
    }
}

static bool match_dag_hash_grow(void) {
    uint32_t old_cap = g_match_dag_hash_cap;
    uint32_t old_epoch = g_match_dag_epoch;
    uint32_t new_cap = old_cap == 0u ? 64u : old_cap * 2u;
    const Atom **old_keys = g_match_dag_keys;
    uint8_t *old_colors = g_match_dag_colors;
    uint32_t *old_stamp = g_match_dag_stamp;
    const Atom **new_keys = cetta_malloc(sizeof(*new_keys) * new_cap);
    uint8_t *new_colors = cetta_malloc(new_cap);
    uint32_t *new_stamp = cetta_malloc(sizeof(*new_stamp) * new_cap);
    uint32_t i;
    if (!new_keys || !new_colors || !new_stamp) {
        free(new_keys);
        free(new_colors);
        free(new_stamp);
        return false;
    }
    memset(new_keys, 0, sizeof(*new_keys) * new_cap);
    memset(new_colors, 0, new_cap);
    memset(new_stamp, 0, sizeof(*new_stamp) * new_cap);
    g_match_dag_keys = new_keys;
    g_match_dag_colors = new_colors;
    g_match_dag_stamp = new_stamp;
    g_match_dag_hash_cap = new_cap;
    g_match_dag_hash_used = 0u;
    for (i = 0u; i < old_cap; i++) {
        if (old_stamp[i] == old_epoch &&
            !match_dag_set_color(old_keys[i], old_colors[i])) {
            return false;
        }
    }
    free(old_keys);
    free(old_colors);
    free(old_stamp);
    return true;
}

/*
 * Same-pointer identity does not need a stamp hash: Loowoz pays that hash
 * on every commit (match_dag_set_color was 3.13 G).  Depth-0/1/2 ground
 * constructors are certified by a child peek; deeper terms use the ancestor
 * stack as gray (diamonds re-walk; 1-cycles and k-cycles still fail).
 * Copy-equality keeps the hash DFS below: J1 spines are long enough that
 * O(n^2) gray scans would dominate atom_eq.
 */
static bool match_occurrence_identity_finite(const Atom *root) {
    CettaExprIndex i;
    CettaExprIndex j;
    bool deeper = false;
    uint32_t sp;
    if (!root)
        return false;
    if (root->kind != ATOM_EXPR)
        return true;
    for (i = 0; i < root->expr.len; i++) {
        const Atom *child = root->expr.elems[i];
        if (!child || child->kind != ATOM_EXPR)
            continue;
        if (child == root)
            return false;
        for (j = 0; j < child->expr.len; j++) {
            const Atom *grand = child->expr.elems[j];
            if (!grand || grand->kind != ATOM_EXPR)
                continue;
            if (grand == root || grand == child)
                return false;
            deeper = true;
        }
    }
    if (!deeper)
        return true;
    if (g_match_dag_stack_cap == 0u) {
        g_match_dag_stack_cap = 32u;
        g_match_dag_stack = cetta_malloc(
            sizeof(*g_match_dag_stack) * g_match_dag_stack_cap);
        if (!g_match_dag_stack)
            return false;
    }
    sp = 0u;
    g_match_dag_stack[sp++] = (MatchDagFrame){root, 0u};
    while (sp > 0u) {
        MatchDagFrame *frame = &g_match_dag_stack[sp - 1u];
        const Atom *atom = frame->atom;
        const Atom *child;
        uint32_t k;
        if (atom->kind != ATOM_EXPR ||
            frame->next_child >= atom->expr.len) {
            sp--;
            continue;
        }
        child = atom->expr.elems[frame->next_child++];
        if (!child || child->kind != ATOM_EXPR)
            continue;
        for (k = 0u; k < sp; k++) {
            if (g_match_dag_stack[k].atom == child)
                return false;
        }
        if (sp >= g_match_dag_stack_cap) {
            uint32_t cap = g_match_dag_stack_cap * 2u;
            MatchDagFrame *grown = cetta_realloc(
                g_match_dag_stack,
                sizeof(*g_match_dag_stack) * cap);
            if (!grown)
                return false;
            g_match_dag_stack = grown;
            g_match_dag_stack_cap = cap;
        }
        g_match_dag_stack[sp++] = (MatchDagFrame){child, 0u};
    }
    return true;
}

static bool match_occurrence_is_finite_dag(const Atom *root) {
    uint32_t sp = 0u;
    if (!root)
        return false;
    if (root->kind != ATOM_EXPR)
        return true;
    if (g_match_dag_stack_cap == 0u) {
        g_match_dag_stack_cap = 32u;
        g_match_dag_stack = cetta_malloc(
            sizeof(*g_match_dag_stack) * g_match_dag_stack_cap);
        if (!g_match_dag_stack)
            return false;
    }
    if (g_match_dag_hash_cap == 0u && !match_dag_hash_grow())
        return false;
    if (g_match_dag_epoch == UINT32_MAX) {
        memset(g_match_dag_stamp, 0,
               sizeof(*g_match_dag_stamp) * g_match_dag_hash_cap);
        g_match_dag_epoch = 1u;
    } else {
        g_match_dag_epoch++;
    }
    g_match_dag_hash_used = 0u;
    if (!match_dag_set_color(root, MATCH_DAG_COLOR_GRAY))
        return false;
    g_match_dag_stack[sp++] = (MatchDagFrame){root, 0u};
    while (sp > 0u) {
        MatchDagFrame *frame = &g_match_dag_stack[sp - 1u];
        const Atom *atom = frame->atom;
        Atom *child;
        uint8_t child_color;
        if (atom->kind != ATOM_EXPR ||
            frame->next_child >= atom->expr.len) {
            if (!match_dag_set_color(atom, MATCH_DAG_COLOR_BLACK))
                return false;
            sp--;
            continue;
        }
        child = atom->expr.elems[frame->next_child++];
        if (!child || child->kind != ATOM_EXPR)
            continue;
        child_color = match_dag_color(child);
        if (child_color == MATCH_DAG_COLOR_GRAY)
            return false;
        if (child_color == MATCH_DAG_COLOR_BLACK)
            continue;
        if (sp >= g_match_dag_stack_cap) {
            uint32_t cap = g_match_dag_stack_cap * 2u;
            MatchDagFrame *grown = cetta_realloc(
                g_match_dag_stack,
                sizeof(*g_match_dag_stack) * cap);
            if (!grown)
                return false;
            g_match_dag_stack = grown;
            g_match_dag_stack_cap = cap;
        }
        if (!match_dag_set_color(child, MATCH_DAG_COLOR_GRAY))
            return false;
        g_match_dag_stack[sp++] = (MatchDagFrame){child, 0u};
    }
    return true;
}

/* One environment, one occurrence: the substitution is the identity
 * (SubstitutionAlgebra.subst_empty).  Distinct activation epochs keep
 * distinct keys.  A raw expression cycle still fails the finite-occurrence
 * audit (the path set, not this decision). */
static inline bool match_shared_occurrence_same_environment(
        bool left_original, bool right_original,
        uint32_t left_epoch, uint32_t right_epoch) {
    if (left_original != right_original)
        return false;
    if (left_original && left_epoch != right_epoch)
        return false;
    return true;
}

static inline bool match_shared_ground_reflexivity_try(
        Atom *left, Atom *right, bool allow_eval_heap,
        bool same_environment, const Bindings *bindings) {
    bool certified;
    if (left != right || !left || left->kind != ATOM_EXPR)
        return false;
#if CETTA_BUILD_WITH_RUNTIME_STATS
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_MATCH_SHARED_GROUND_REFLEXIVITY_ATTEMPT);
#endif
    if (atom_has_vars(left)) {
        if (!same_environment ||
            (bindings && (bindings->cycle_state != BINDINGS_CYCLE_ACYCLIC)) ||
            !match_shared_ground_reflexivity_enabled() ||
            !match_occurrence_identity_finite(left)) {
#if CETTA_BUILD_WITH_RUNTIME_STATS
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_MATCH_SHARED_GROUND_REFLEXIVITY_OPEN_DECLINE);
#endif
            return false;
        }
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_MATCH_SHARED_OPEN_REFLEXIVITY_COMMIT);
        return true;
    }
    if (!match_shared_ground_reflexivity_enabled())
        return false;
    certified = match_shared_ground_reflexivity_certified(left);
    if (!certified) {
        if (!allow_eval_heap || !match_occurrence_identity_finite(left)) {
#if CETTA_BUILD_WITH_RUNTIME_STATS
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_MATCH_SHARED_GROUND_REFLEXIVITY_UNCERTIFIED_DECLINE);
#endif
            return false;
        }
    }
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_MATCH_SHARED_GROUND_REFLEXIVITY_COMMIT);
    return true;
}

/* Two finite ground expressions have no substitution effect.  Interned
 * closed DAGs already factor through identity/hash; eval-heap trees use
 * structural equality once acyclicity is certified. */
static bool match_finite_ground_equal_try(
        Atom *left, Atom *right, bool *equal_out) {
    if (!left || !right || !equal_out || left == right ||
        left->kind != ATOM_EXPR || right->kind != ATOM_EXPR ||
        atom_has_vars(left) || atom_has_vars(right) ||
        !match_shared_ground_reflexivity_enabled())
        return false;
    if (!match_occurrence_is_finite_dag(left) ||
        (right != left && !match_occurrence_is_finite_dag(right)))
        return false;
    *equal_out = atom_eq(left, right);
    return true;
}

/* A pair of published immutable ground-expression DAGs has no substitution
 * effect.  The independent typed-plan matcher proves that this case factors
 * exactly through structural equality, so the generic matcher need not
 * schedule one work item per child.  Arena-local expressions are deliberately
 * excluded: their cached structural flags do not certify that a client has
 * not subsequently introduced a cycle.  The reference switch retains the
 * ordinary traversal for differential and cost qualification. */
static bool match_closed_expression_decision_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        const char *reference = getenv(
            "CETTA_MATCH_CLOSED_EXPRESSION_DECISION_REFERENCE");
        enabled = !reference || reference[0] == '\0' ||
            reference[0] == '0';
    }
    return enabled != 0;
}

static bool match_closed_expression_decision_certified(
        const Atom *atom) {
    const uint32_t required =
        ATOM_FLAG_HASHCONS_ELIGIBLE | ATOM_FLAG_ARENA_CLOSED;
    return atom && atom->kind == ATOM_EXPR && atom->arena_id == 0u &&
        !atom_has_vars(atom) && (atom->flags & required) == required;
}

static inline bool match_closed_expression_decision_try(
        Atom *left, Atom *right, bool *equal_out) {
    if (!left || !right || !equal_out ||
        !match_closed_expression_decision_certified(left) ||
        !match_closed_expression_decision_certified(right) ||
        !match_closed_expression_decision_enabled()) {
        return false;
    }
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_MATCH_CLOSED_EXPRESSION_DECISION_ATTEMPT);
    /*
     * A published interned closed DAG is a unique node in its intern table.
     * Same pointer is equality; distinct hashes are inequality.  Equal hashes
     * with distinct pointers still walk (independent intern tables).
     */
    if (left == right)
        *equal_out = true;
    else if (atom_hash(left) != atom_hash(right))
        *equal_out = false;
    else
        *equal_out = atom_eq(left, right);
    cetta_runtime_stats_inc(*equal_out
        ? CETTA_RUNTIME_COUNTER_MATCH_CLOSED_EXPRESSION_DECISION_EQUAL
        : CETTA_RUNTIME_COUNTER_MATCH_CLOSED_EXPRESSION_DECISION_UNEQUAL);
    return true;
}

typedef struct BindingPoolBlock {
    struct BindingPoolBlock *next;
    _Atomic uint32_t references;
    uint32_t capacity;
} BindingPoolBlock;

static inline Binding *bindings_exclusive_slot(Bindings *b, uint32_t i);
static bool bindings_flatten_unique(Bindings *b);
static void bindings_release_truncated_suffix(Bindings *b);
bool bindings_builder_trail_reserve(
    BindingsBuilder *bb, uint32_t needed);
bool bindings_builder_prime_trail_reserve(
    BindingsBuilder *bb, uint32_t needed);
static bool bindings_builder_snapshot(
    BindingsBuilder *bb, bool *created_out);
static bool bindings_alias_keeps_caller_identity(
        VarId id, BindingValue value, uint32_t identity, bool *cross_frame_alias) {
    if (!value.skeleton || value.skeleton->kind != ATOM_VAR)
        return true;
    bool key_local = var_epoch_suffix(id) == identity;
    bool value_local = var_epoch_suffix(binding_value_variable_id(value)) == identity;
    if (key_local != value_local && cross_frame_alias)
        *cross_frame_alias = true;
    return key_local || !value_local;
}

bool bindings_builder_aliases_normalized_since(
        const BindingsBuilder *builder, uint32_t trail_mark,
        uint32_t first_entry, uint32_t identity, bool *cross_frame_alias) {
    if (cross_frame_alias)
        *cross_frame_alias = false;
    if (!builder || !identity || trail_mark > builder->trail_len ||
        first_entry > builder->current.len)
        return false;
    const Bindings *bindings = &builder->current;
    for (uint32_t row = first_entry; row < bindings->len; row++) {
        const Binding *binding = bindings_entry_at(bindings, row);
        if (!bindings_alias_keeps_caller_identity(binding->var_id, binding->value,
                identity, cross_frame_alias))
            return false;
    }
    for (uint32_t cursor = builder->frame_undo_len; cursor > 0u; cursor--) {
        const BindingsFrameUndoEntry *undo = &builder->frame_undo[cursor - 1u];
        if (undo->trail_mark < trail_mark)
            break;
        VarId id = var_epoch_id(undo->source_id, undo->frame_ref.identity);
        BindingValue value = bindings_lookup_value_id((Bindings *)bindings, id);
        if (!bindings_alias_keeps_caller_identity(id, value, identity, cross_frame_alias))
            return false;
    }
    return true;
}


typedef struct {
    const Atom *src;
    Atom *dst;
} FreshenEpochMemoSlot;

typedef struct {
    FreshenEpochMemoSlot inline_slots[FRESHEN_EPOCH_MEMO_INLINE_CAP];
    FreshenEpochMemoSlot *slots;
    size_t cap;
    size_t used;
    uint64_t inline_occupied;
} FreshenEpochMemo;

_Static_assert(FRESHEN_EPOCH_MEMO_INLINE_CAP == 64u,
               "inline memo occupancy mask must cover every slot");

static const uint32_t BINDINGS_POOL_CAPS[BINDINGS_POOL_CLASS_COUNT] = {8, 16, 32, 64};
static __thread BindingPoolBlock *g_binding_entry_pools[BINDINGS_POOL_CLASS_COUNT];
static __thread BindingPoolBlock *g_binding_constraint_pools[BINDINGS_POOL_CLASS_COUNT];
static __thread size_t g_binding_entry_active_bytes = 0;
static __thread size_t g_binding_entry_pool_bytes = 0;
static __thread size_t g_binding_entry_retained_bytes = 0;
static __thread size_t g_binding_constraint_active_bytes = 0;
static __thread size_t g_binding_constraint_pool_bytes = 0;
static __thread size_t g_binding_constraint_retained_bytes = 0;

static void bindings_pool_free_all(BindingPoolBlock **pools) {
    for (uint32_t i = 0; i < BINDINGS_POOL_CLASS_COUNT; i++) {
        BindingPoolBlock *block = pools[i];
        while (block) {
            BindingPoolBlock *next = block->next;
            free(block);
            block = next;
        }
        pools[i] = NULL;
    }
}

void bindings_thread_cache_free(void) {
    bindings_pool_free_all(g_binding_entry_pools);
    bindings_pool_free_all(g_binding_constraint_pools);
    free(g_bindings_single_reach_path);
    g_bindings_single_reach_path = NULL;
    g_bindings_single_reach_path_cap = 0u;
    g_binding_entry_active_bytes = 0;
    g_binding_entry_pool_bytes = 0;
    g_binding_entry_retained_bytes = 0;
    g_binding_constraint_active_bytes = 0;
    g_binding_constraint_pool_bytes = 0;
    g_binding_constraint_retained_bytes = 0;
}

static void bindings_note_entry_pool_metrics(void) {
    cetta_runtime_stats_set(CETTA_RUNTIME_COUNTER_BINDINGS_ENTRY_POOL_BYTES,
                            (uint64_t)g_binding_entry_pool_bytes);
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_BINDINGS_ENTRY_POOL_BYTES_PEAK,
        (uint64_t)g_binding_entry_pool_bytes);
    cetta_runtime_stats_set(CETTA_RUNTIME_COUNTER_BINDINGS_ENTRY_RETAINED_BYTES,
                            (uint64_t)g_binding_entry_retained_bytes);
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_BINDINGS_ENTRY_RETAINED_BYTES_PEAK,
        (uint64_t)g_binding_entry_retained_bytes);
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_BINDINGS_ENTRY_ACTIVE_BYTES_PEAK,
        (uint64_t)g_binding_entry_active_bytes);
}

static void bindings_note_constraint_pool_metrics(void) {
    cetta_runtime_stats_set(
        CETTA_RUNTIME_COUNTER_BINDINGS_CONSTRAINT_POOL_BYTES,
        (uint64_t)g_binding_constraint_pool_bytes);
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_BINDINGS_CONSTRAINT_POOL_BYTES_PEAK,
        (uint64_t)g_binding_constraint_pool_bytes);
    cetta_runtime_stats_set(
        CETTA_RUNTIME_COUNTER_BINDINGS_CONSTRAINT_RETAINED_BYTES,
        (uint64_t)g_binding_constraint_retained_bytes);
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_BINDINGS_CONSTRAINT_RETAINED_BYTES_PEAK,
        (uint64_t)g_binding_constraint_retained_bytes);
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_BINDINGS_CONSTRAINT_ACTIVE_BYTES_PEAK,
        (uint64_t)g_binding_constraint_active_bytes);
}

size_t bindings_entry_active_bytes(void) {
    return g_binding_entry_active_bytes;
}

size_t bindings_constraint_active_bytes(void) {
    return g_binding_constraint_active_bytes;
}

static inline bool binding_var_eq(VarId lhs, VarId rhs) {
    return lhs == rhs;
}

static bool binding_id_has_frame(VarId id) {
    return var_epoch_suffix(id) != 0u && !variant_private_var_id(id);
}

static bool atom_contains_private_variant_var(const Atom *atom) {
    return atom_has_private_variant_vars(atom);
}

static bool binding_contains_private_variant_slot(const Binding *binding) {
    return binding &&
           (variant_private_var_id(binding->var_id) ||
            atom_contains_private_variant_var(binding->value.skeleton));
}

static bool constraint_contains_private_variant_slot(
    const BindingConstraint *constraint) {
    return constraint &&
           (atom_contains_private_variant_var(constraint->lhs.skeleton) ||
            atom_contains_private_variant_var(constraint->rhs.skeleton));
}

static void bindings_private_counts_slow(
    const Bindings *bindings, uint32_t *entries, uint32_t *constraints) {
    uint32_t entry_count = 0u;
    uint32_t constraint_count = 0u;
    if (bindings) {
        for (uint32_t i = 0u; i < bindings->len; i++)
            entry_count +=
                binding_contains_private_variant_slot(
                    bindings_entry_at(bindings, i)) ? 1u : 0u;
        for (uint32_t i = 0u; i < bindings->eq_len; i++)
            constraint_count +=
                constraint_contains_private_variant_slot(
                    &bindings->constraints[i]) ? 1u : 0u;
    }
    if (entries)
        *entries = entry_count;
    if (constraints)
        *constraints = constraint_count;
}



static uint8_t bindings_derived_nonzero(const Bindings *bindings) {
    if (!bindings)
        return 0u;
    uint8_t flags = 0u;
    if (bindings->private_entry_count != 0u)
        flags |= BINDINGS_DERIVED_PRIVATE_ENTRY_NONZERO;
    if (bindings->private_constraint_count != 0u)
        flags |= BINDINGS_DERIVED_PRIVATE_CONSTRAINT_NONZERO;
    return flags;
}

/*
 * A builder trail restores logical lengths.  These counts are accelerators,
 * not logical state, so the compact trail remembers only whether a scan can
 * be necessary and rebuilds exact values on that cold rollback path.
 */
static void bindings_restore_derived_counts(
    Bindings *bindings, uint8_t nonzero) {
    if (nonzero & (BINDINGS_DERIVED_PRIVATE_ENTRY_NONZERO |
                   BINDINGS_DERIVED_PRIVATE_CONSTRAINT_NONZERO)) {
        uint32_t entries = 0u;
        uint32_t constraints = 0u;
        bindings_private_counts_slow(
            bindings, &entries, &constraints);
        bindings->private_entry_count =
            (nonzero & BINDINGS_DERIVED_PRIVATE_ENTRY_NONZERO)
                ? entries
                : 0u;
        bindings->private_constraint_count =
            (nonzero & BINDINGS_DERIVED_PRIVATE_CONSTRAINT_NONZERO)
                ? constraints
                : 0u;
    } else {
        bindings->private_entry_count = 0u;
        bindings->private_constraint_count = 0u;
    }
}

static bool bindings_private_audit_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        const char *value = getenv("CETTA_BINDINGS_DERIVED_AUDIT");
        enabled = value && value[0] == '1';
    }
    return enabled != 0;
}

bool bindings_contains_private_variant_slots(const Bindings *b) {
    if (!b)
        return false;
    bool derived =
        b->private_entry_count != 0u ||
        b->private_constraint_count != 0u ||
        (b->frame_index && b->frame_index->private_value_count != 0u);
    if (bindings_private_audit_enabled()) {
        uint32_t entries = 0u;
        uint32_t constraints = 0u;
        bindings_private_counts_slow(b, &entries, &constraints);
        assert(entries == b->private_entry_count);
        assert(constraints == b->private_constraint_count);
        size_t framed_private = 0u;
        if (b->frame_index) {
            for (uint32_t i = 0u; i < b->frame_index->len; i++) {
                const BindingsFrameIndexEntry *frame = &b->frame_index->frames[i];
                uint32_t count = 0u;
                for (uint32_t slot = 0u; slot < frame->slot_len; slot++)
                    count += atom_contains_private_variant_var(frame->values[slot].skeleton);
                assert(count == frame->private_value_count);
                framed_private += count;
            }
            assert(framed_private == b->frame_index->private_value_count);
        }
    }
    return derived;
}

void bindings_assert_no_private_variant_slots(const Bindings *b) {
#ifndef NDEBUG
    assert(!bindings_contains_private_variant_slots(b));
#else
    (void)b;
#endif
}

static int bindings_pool_class(uint32_t cap) {
    for (uint32_t i = 0; i < BINDINGS_POOL_CLASS_COUNT; i++) {
        if (BINDINGS_POOL_CAPS[i] == cap)
            return (int)i;
    }
    return -1;
}

static BindingPoolBlock *bindings_pool_block(void *items) {
    return items ? ((BindingPoolBlock *)items) - 1 : NULL;
}

static bool bindings_pool_block_is_unique(
        const void *items, uint32_t capacity) {
    if (!items)
        return capacity == 0u;
    BindingPoolBlock *block = bindings_pool_block((void *)items);
    assert(block->capacity == capacity);
    return atomic_load_explicit(
        &block->references, memory_order_acquire) == 1u;
}

/* A frozen flat image may be retained by any number of independently live
 * continuations.  Refcount 1 remains exclusively writable; capture retains
 * the block, and a later write detaches.  This is the C realization of
 * BindingVersions.Version sharing, not the two-alias CowSnapshot model. */
static bool bindings_pool_block_retain(
        const void *items, uint32_t capacity) {
    if (!items)
        return capacity == 0u;
    BindingPoolBlock *block = bindings_pool_block((void *)items);
    assert(block->capacity == capacity);
    uint32_t current = atomic_load_explicit(
        &block->references, memory_order_acquire);
    do {
        if (current == 0u || current == UINT32_MAX)
            return false;
    } while (!atomic_compare_exchange_weak_explicit(
        &block->references, &current, current + 1u,
        memory_order_acq_rel, memory_order_acquire));
    return true;
}

static Binding *bindings_entries_alloc(uint32_t cap) {
    int klass = bindings_pool_class(cap);
    size_t bytes = sizeof(Binding) * cap;
    BindingPoolBlock *block;
    if (klass >= 0 && g_binding_entry_pools[klass]) {
        block = g_binding_entry_pools[klass];
        g_binding_entry_pools[klass] = block->next;
        if (g_binding_entry_pool_bytes >= bytes)
            g_binding_entry_pool_bytes -= bytes;
        else
            g_binding_entry_pool_bytes = 0;
        g_binding_entry_active_bytes += bytes;
        bindings_note_entry_pool_metrics();
        block->next = NULL;
        block->capacity = cap;
        atomic_store_explicit(
            &block->references, 1u, memory_order_relaxed);
        return (Binding *)(block + 1);
    }
    g_binding_entry_active_bytes += bytes;
    g_binding_entry_retained_bytes += bytes;
    bindings_note_entry_pool_metrics();
    block = cetta_malloc(sizeof(*block) + bytes);
    block->next = NULL;
    block->capacity = cap;
    atomic_init(&block->references, 1u);
    return (Binding *)(block + 1);
}

static void bindings_entries_release(Binding *entries, uint32_t cap) {
    size_t bytes;
    if (!entries) return;
    BindingPoolBlock *block = bindings_pool_block(entries);
    assert(block->capacity == cap);
    uint32_t previous = atomic_fetch_sub_explicit(
        &block->references, 1u, memory_order_acq_rel);
    assert(previous > 0u);
    if (previous != 1u)
        return;
    bytes = sizeof(Binding) * cap;
    int klass = bindings_pool_class(cap);
    if (klass < 0) {
        if (g_binding_entry_active_bytes >= bytes)
            g_binding_entry_active_bytes -= bytes;
        else
            g_binding_entry_active_bytes = 0;
        if (g_binding_entry_retained_bytes >= bytes)
            g_binding_entry_retained_bytes -= bytes;
        else
            g_binding_entry_retained_bytes = 0;
        bindings_note_entry_pool_metrics();
        free(block);
        return;
    }
    if (g_binding_entry_active_bytes >= bytes)
        g_binding_entry_active_bytes -= bytes;
    else
        g_binding_entry_active_bytes = 0;
    g_binding_entry_pool_bytes += bytes;
    block->next = g_binding_entry_pools[klass];
    g_binding_entry_pools[klass] = block;
    bindings_note_entry_pool_metrics();
}

static BindingConstraint *bindings_constraints_alloc(uint32_t cap) {
    int klass = bindings_pool_class(cap);
    size_t bytes = sizeof(BindingConstraint) * cap;
    BindingPoolBlock *block;
    if (klass >= 0 && g_binding_constraint_pools[klass]) {
        block = g_binding_constraint_pools[klass];
        g_binding_constraint_pools[klass] = block->next;
        if (g_binding_constraint_pool_bytes >= bytes)
            g_binding_constraint_pool_bytes -= bytes;
        else
            g_binding_constraint_pool_bytes = 0;
        g_binding_constraint_active_bytes += bytes;
        bindings_note_constraint_pool_metrics();
        block->next = NULL;
        block->capacity = cap;
        atomic_store_explicit(
            &block->references, 1u, memory_order_relaxed);
        return (BindingConstraint *)(block + 1);
    }
    g_binding_constraint_active_bytes += bytes;
    g_binding_constraint_retained_bytes += bytes;
    bindings_note_constraint_pool_metrics();
    block = cetta_malloc(sizeof(*block) + bytes);
    block->next = NULL;
    block->capacity = cap;
    atomic_init(&block->references, 1u);
    return (BindingConstraint *)(block + 1);
}

static void bindings_constraints_release(BindingConstraint *constraints, uint32_t cap) {
    size_t bytes;
    if (!constraints) return;
    BindingPoolBlock *block = bindings_pool_block(constraints);
    assert(block->capacity == cap);
    uint32_t previous = atomic_fetch_sub_explicit(
        &block->references, 1u, memory_order_acq_rel);
    assert(previous > 0u);
    if (previous != 1u)
        return;
    bytes = sizeof(BindingConstraint) * cap;
    int klass = bindings_pool_class(cap);
    if (klass < 0) {
        if (g_binding_constraint_active_bytes >= bytes)
            g_binding_constraint_active_bytes -= bytes;
        else
            g_binding_constraint_active_bytes = 0;
        if (g_binding_constraint_retained_bytes >= bytes)
            g_binding_constraint_retained_bytes -= bytes;
        else
            g_binding_constraint_retained_bytes = 0;
        bindings_note_constraint_pool_metrics();
        free(block);
        return;
    }
    if (g_binding_constraint_active_bytes >= bytes)
        g_binding_constraint_active_bytes -= bytes;
    else
        g_binding_constraint_active_bytes = 0;
    g_binding_constraint_pool_bytes += bytes;
    block->next = g_binding_constraint_pools[klass];
    g_binding_constraint_pools[klass] = block;
    bindings_note_constraint_pool_metrics();
}

static BindingConstraint *bindings_temp_constraints_alloc(
    uint32_t cap, BindingConstraint *stack, uint32_t stack_cap) {
    if (cap <= stack_cap)
        return stack;
    return bindings_constraints_alloc(cap);
}

static void bindings_temp_constraints_release(BindingConstraint *constraints,
                                              uint32_t cap,
                                              BindingConstraint *stack) {
    if (!constraints || constraints == stack)
        return;
    bindings_constraints_release(constraints, cap);
}



static size_t bindings_var_id_hash(VarId id) {
    /* Fold packed epoch/base structure through a bijective 64-bit mix before
     * the power-of-two tables select their low address bits. */
    uint64_t x = (uint64_t)id;
    x ^= x >> 24;
    x *= UINT64_C(0xff51afd7ed558ccd);
    x ^= x >> 29;
    return (size_t)x;
}

BindingsExclusiveFrame *bindings_exclusive_frame_new(void) {
    BindingsExclusiveFrame *frame =
        cetta_malloc(sizeof(BindingsExclusiveFrame));
    memset(frame, 0, sizeof(*frame));
    return frame;
}

void bindings_exclusive_frame_free(BindingsExclusiveFrame *frame) {
    if (!frame)
        return;
    if (frame->owns_identity)
        cetta_frame_identity_release(frame->epoch);
    free(frame->values);
    free(frame->writes);
    free(frame);
}

bool bindings_exclusive_frame_begin(
        BindingsExclusiveFrame *frame, BindingsFrameSchema *schema,
        uint32_t epoch) {
    if (!frame || !schema || epoch == 0u)
        return false;
    uint32_t len = schema->len;
    if (len > frame->value_cap) {
        uint32_t cap = frame->value_cap ? frame->value_cap : 8u;
        while (cap < len) {
            if (cap > UINT32_MAX / 2u)
                return false;
            cap *= 2u;
        }
        if ((size_t)cap > SIZE_MAX / sizeof(*frame->values))
            return false;
        frame->values = frame->values
            ? cetta_realloc(frame->values,
                  (size_t)cap * sizeof(*frame->values))
            : cetta_malloc((size_t)cap * sizeof(*frame->values));
        frame->value_cap = cap;
    }
    if (len > 0u)
        memset(frame->values, 0, (size_t)len * sizeof(*frame->values));
    frame->schema = schema;
    frame->write_len = 0u;
    /* The frame keeps its identity until the next begin, beyond the attempt
     * that minted it: an accepted activation's epoch outlives the minting
     * scope until the goal that runs it takes its own reference. */
    bool owns_identity = cetta_frame_identity_retain(epoch);
    if (frame->owns_identity)
        cetta_frame_identity_release(frame->epoch);
    frame->epoch = epoch;
    frame->owns_identity = owns_identity;
    frame->active = true;
    frame->fresh = false;
    frame->frame_mentioned = false;
    return true;
}

bool bindings_exclusive_frame_begin_fresh(
        BindingsExclusiveFrame *frame, BindingsFrameSchema *schema,
        uint32_t epoch) {
    if (!bindings_exclusive_frame_begin(frame, schema, epoch))
        return false;
    frame->fresh = true;
    return true;
}

#ifdef CETTA_TEST_HOOKS
bool bindings_exclusive_frame_test_certified(
        const BindingsExclusiveFrame *frame) {
    return frame && frame->fresh && !frame->frame_mentioned;
}
#endif

static bool bindings_exclusive_frame_source_slot(
        const BindingsExclusiveFrame *frame, VarId id,
        uint32_t *slot_out) {
    return frame && frame->active && frame->epoch != 0u &&
        var_epoch_suffix(id) == frame->epoch &&
        bindings_frame_schema_find_slot(
            frame->schema, (VarId)var_base_id(id), slot_out);
}

static BindingValue bindings_exclusive_frame_lookup(
        const BindingsExclusiveFrame *frame, const Bindings *outer,
        VarId id, bool *owned_out) {
    if (owned_out)
        *owned_out = false;
    if (frame && frame->active) {
        uint32_t slot = 0u;
        if (bindings_exclusive_frame_source_slot(frame, id, &slot)) {
            if (owned_out)
                *owned_out = true;
            return frame->values[slot];
        }
        for (uint32_t index = frame->write_len; index > 0u; index--) {
            const BindingsExclusiveWrite *write =
                &frame->writes[index - 1u];
            if (write->local_slot == UINT32_MAX &&
                binding_var_eq(write->external_id, id)) {
                if (owned_out)
                    *owned_out = true;
                return write->external_value;
            }
        }
    }
    return outer
        ? bindings_lookup_value_id((Bindings *)outer, id)
        : binding_value_from_atom(NULL);
}

bool bindings_exclusive_frame_has_external_writes(
        const BindingsExclusiveFrame *frame) {
    if (!frame || !frame->active)
        return false;
    for (uint32_t index = 0u; index < frame->write_len; index++) {
        if (frame->writes[index].local_slot == UINT32_MAX)
            return true;
    }
    return false;
}

bool bindings_exclusive_frame_aliases_normalized(
        const BindingsExclusiveFrame *frame, bool *cross_frame_alias) {
    if (cross_frame_alias)
        *cross_frame_alias = false;
    if (!frame || !frame->active || frame->epoch == 0u)
        return false;
    for (uint32_t index = 0u; index < frame->write_len; index++) {
        const BindingsExclusiveWrite *write = &frame->writes[index];
        VarId id = write->local_slot == UINT32_MAX
            ? write->external_id
            : var_epoch_id(
                  frame->schema->source_ids[write->local_slot],
                  frame->epoch);
        BindingValue value = write->local_slot == UINT32_MAX
            ? write->external_value
            : frame->values[write->local_slot];
        bool entry_is_local = var_epoch_suffix(id) == frame->epoch;
        bool value_is_local = value.skeleton &&
            value.skeleton->kind == ATOM_VAR &&
            var_epoch_suffix(binding_value_variable_id(value)) ==
                frame->epoch;
        if (value.skeleton && value.skeleton->kind == ATOM_VAR &&
            entry_is_local != value_is_local && cross_frame_alias) {
            *cross_frame_alias = true;
        }
        if (!entry_is_local && value_is_local)
            return false;
    }
    return true;
}


static bool bindings_lookup_index_enabled(void) {
    if (g_bindings_lookup_index_enabled < 0) {
        const char *setting = getenv("CETTA_BINDINGS_LOOKUP_INDEX");
        g_bindings_lookup_index_enabled =
            !(setting && setting[0] == '0') ? 1 : 0;
    }
    return g_bindings_lookup_index_enabled != 0;
}

#if !defined(CETTA_MUTATION_BINDINGS_SINGLE_REACH_KEEP_ROLLBACK_CACHE)
static void bindings_single_reach_configure_invalidation(void) {
    if (g_bindings_single_reach_support_invalidation_enabled >= 0)
        return;
    const char *reference = getenv(
        "CETTA_BINDINGS_SINGLE_REACH_CAPACITY_SCAN_REFERENCE");
    bool reference_enabled = reference && reference[0] != '\0' &&
        strncmp(reference, "0", 2u) != 0 &&
        strncmp(reference, "false", 6u) != 0 &&
        strncmp(reference, "off", 4u) != 0;
    g_bindings_single_reach_support_invalidation_enabled =
        reference_enabled ? 0 : 1;
}
#endif

static BindingsLookupIndex *bindings_lookup_index_alloc(size_t capacity) {
#if !defined(CETTA_MUTATION_BINDINGS_SINGLE_REACH_KEEP_ROLLBACK_CACHE)
    bindings_single_reach_configure_invalidation();
#endif
    BindingsLookupIndex *index = cetta_malloc(sizeof(*index));
    atomic_init(&index->references, 1u);
    index->slots =
        cetta_malloc(capacity * sizeof(*index->slots));
    memset(index->slots, 0, capacity * sizeof(*index->slots));
    index->capacity = capacity;
    index->count = 0u;
    index->synced_len = 0u;
    index->has_duplicates = false;
    index->single_cached_ids = NULL;
    index->single_cached_len = 0u;
    index->single_cached_cap = 0u;
    return index;
}

static void bindings_lookup_index_retain(BindingsLookupIndex *index) {
    if (!index)
        return;
    uint32_t previous = atomic_fetch_add_explicit(
        &index->references, 1u, memory_order_relaxed);
    assert(previous > 0u && previous < UINT32_MAX);
}

static void bindings_lookup_index_release(BindingsLookupIndex *index) {
    if (!index)
        return;
    uint32_t previous = atomic_fetch_sub_explicit(
        &index->references, 1u, memory_order_acq_rel);
    assert(previous > 0u);
    if (previous == 1u) {
        free(index->single_cached_ids);
        free(index->slots);
        free(index);
    }
}


/* Frame ownership is part of a contextual variable's identity, not a
 * property of one particular binding row.  Operations which compose or
 * extract substitutions therefore carry the registered frame schemas even
 * when some of their slots are currently unbound.  Slots are current-value
 * authority; chronological rows belong only to unframed variables. */


static bool bindings_value_collect_frame_ids(
        BindingValue value, VarId **ids_out, size_t *len_out,
        VarId *inline_ids, size_t inline_cap) {
    if (ids_out)
        *ids_out = inline_ids;
    if (len_out)
        *len_out = 0u;
    if (!ids_out || !len_out || !inline_ids || inline_cap == 0u ||
        !value.skeleton || !atom_has_vars(value.skeleton)) {
        return ids_out && len_out && inline_ids && inline_cap != 0u;
    }
    /* Constructors maintain an exact singleton-variable support fact.  When
     * it is present, the whole syntax graph denotes one frame coordinate and
     * there is no inventory to discover by traversal.  Contextual syntax
     * supplies the frame generation; materialized syntax already carries it
     * in the identifier. */
    VarId singleton = atom_single_variable_id(value.skeleton);
    if (singleton != VAR_ID_NONE) {
        uint32_t epoch = binding_value_is_contextual(value)
            ? value.epoch : var_epoch_suffix(singleton);
        if (epoch != 0u && !variant_private_var_id(singleton)) {
            inline_ids[0] = var_epoch_id(
                (VarId)var_base_id(singleton), epoch);
            *len_out = 1u;
        }
        return true;
    }
    Atom *inline_items[32];
    Atom **items = inline_items;
    size_t item_len = 1u;
    size_t item_cap = sizeof(inline_items) / sizeof(inline_items[0]);
    VarId *ids = inline_ids;
    size_t id_len = 0u;
    size_t id_cap = inline_cap;
    items[0] = value.skeleton;
    while (item_len > 0u) {
        Atom *atom = items[--item_len];
        if (!atom || !atom_has_vars(atom))
            continue;
        if (atom->kind == ATOM_VAR) {
            uint32_t epoch = binding_value_is_contextual(value)
                ? value.epoch : var_epoch_suffix(atom->var_id);
            if (epoch == 0u || variant_private_var_id(atom->var_id))
                continue;
            VarId id = var_epoch_id(
                (VarId)var_base_id(atom->var_id), epoch);
            bool known = false;
            for (size_t index = 0u; index < id_len; index++) {
                if (ids[index] == id) {
                    known = true;
                    break;
                }
            }
            if (known)
                continue;
            if (id_len == id_cap) {
                size_t next = id_cap <= SIZE_MAX / 2u
                    ? id_cap * 2u : SIZE_MAX;
                if (next <= id_cap || next > SIZE_MAX / sizeof(*ids))
                    goto fail;
                VarId *grown = cetta_malloc(next * sizeof(*grown));
                memcpy(grown, ids, id_len * sizeof(*grown));
                if (ids != inline_ids)
                    free(ids);
                ids = grown;
                id_cap = next;
            }
            ids[id_len++] = id;
            continue;
        }
        if (atom->kind != ATOM_EXPR || atom->expr.len == 0u)
            continue;
        if ((size_t)atom->expr.len > SIZE_MAX - item_len)
            goto fail;
        size_t needed = item_len + (size_t)atom->expr.len;
        if (needed > item_cap) {
            size_t next = item_cap;
            while (next < needed) {
                if (next > SIZE_MAX / 2u) {
                    next = needed;
                    break;
                }
                next *= 2u;
            }
            if (next > SIZE_MAX / sizeof(*items))
                goto fail;
            Atom **grown = cetta_malloc(next * sizeof(*grown));
            memcpy(grown, items, item_len * sizeof(*grown));
            if (items != inline_items)
                free(items);
            items = grown;
            item_cap = next;
        }
        for (CettaExprIndex child = 0u;
             child < atom->expr.len; child++) {
            items[item_len++] = atom->expr.elems[child];
        }
    }
    if (items != inline_items)
        free(items);
    *ids_out = ids;
    *len_out = id_len;
    return true;

fail:
    if (items != inline_items)
        free(items);
    if (ids != inline_ids)
        free(ids);
    return false;
}

#if CETTA_BUILD_WITH_RUNTIME_STATS
static bool bindings_materialized_value_has_epoch_variable(
        BindingValue value) {
    if (binding_value_is_contextual(value))
        return false;
    VarId inline_ids[16];
    VarId *ids = inline_ids;
    size_t id_len = 0u;
    bool collected = bindings_value_collect_frame_ids(
        value, &ids, &id_len, inline_ids,
        sizeof(inline_ids) / sizeof(inline_ids[0]));
    if (ids != inline_ids)
        free(ids);
    return collected && id_len > 0u;
}
#endif

static bool bindings_frame_index_note_value_context(
        Bindings *bindings, BindingsBuilder *builder,
        BindingValue value, bool note_store) {
    if (binding_value_is_contextual(value) && value.epoch != 0u &&
        bindings_frame_index_epoch_complete(bindings, value.epoch)) {
        if (note_store) {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_CONTEXT_STORE_OWNED);
        }
        return true;
    }
    VarId inline_ids[16];
    VarId *ids = inline_ids;
    size_t id_len = 0u;
    if (!bindings || !bindings_value_collect_frame_ids(
            value, &ids, &id_len, inline_ids,
            sizeof(inline_ids) / sizeof(inline_ids[0]))) {
        return false;
    }
    if (id_len == 0u) {
        return true;
    }
    if (note_store && binding_value_is_contextual(value)) {
        #if CETTA_BUILD_WITH_RUNTIME_STATS
        const BindingsFrameIndexEntry *known =
            bindings_frame_index_find_frame_const(
                bindings->frame_index, var_epoch_suffix(ids[0]));
        cetta_runtime_stats_inc(
            known
                ? CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_CONTEXT_STORE_OWNED
                : CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_CONTEXT_STORE_UNOWNED);
        #endif
    }
    for (size_t index = 0u; index < id_len; index++) {
        if (builder && &builder->current == bindings) {
            if (!bindings_builder_frame_index_ensure_id(builder, ids[index]))
                goto fail;
        } else if (!bindings_frame_index_ensure_id(
                       bindings, ids[index])) {
            goto fail;
        }
    }
    if (note_store && !binding_value_is_contextual(value)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_BINDINGS_MATERIALIZED_EPOCH_VALUE_STORE);
    }
    if (ids != inline_ids)
        free(ids);
    return true;

fail:
    if (ids != inline_ids)
        free(ids);
    return false;
}

static bool bindings_frame_index_prepare_value_context(
        Bindings *bindings, BindingsBuilder *builder,
        BindingValue *value, bool note_store) {
    /* Stored contextual values normally arrive with the generation-checked
     * reference of their owning frame.  Validation through the handle table
     * is the contextual identity check; looking the same activation up again
     * by epoch would reintroduce directory discovery on every write.  A
     * complete schema already owns every source variable in the skeleton, so
     * neither inventory traversal nor rebasing can add information here.
     *
     * Incomplete frames and values crossing a transport boundary without a
     * valid destination reference keep the general admission path below. */
    if (value && bindings && binding_value_is_contextual(*value) &&
        value->skeleton && atom_has_vars(value->skeleton)) {
        const BindingsFrameIndexEntry *entry =
            bindings_frame_index_find_ref_const(
                bindings->frame_index,
                binding_value_frame_ref(*value));
        if (entry && entry->schema_complete) {
            if (note_store) {
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_CONTEXT_STORE_OWNED);
            }
            return true;
        }
    }
    return value &&
        bindings_frame_index_note_value_context(
            bindings, builder, *value, note_store);
}


static bool bindings_lookup_index_single_cached_reserve(
        BindingsLookupIndex *index, size_t needed) {
    if (!index)
        return false;
    if (needed <= index->single_cached_cap)
        return true;
    size_t capacity = index->single_cached_cap
        ? index->single_cached_cap : 32u;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2u)
            return false;
        capacity *= 2u;
    }
    if (capacity > SIZE_MAX / sizeof(*index->single_cached_ids))
        return false;
    index->single_cached_ids = cetta_realloc(
        index->single_cached_ids,
        capacity * sizeof(*index->single_cached_ids));
    index->single_cached_cap = capacity;
    return true;
}

static uint32_t bindings_lookup_index_single_cache_kind(
        const BindingsLookupIndexSlot *slot) {
    return slot
        ? slot->single_cache_kind &
            BINDINGS_SINGLE_REACH_CACHE_KIND_MASK
        : BINDINGS_SINGLE_REACH_CACHE_NONE;
}

static bool bindings_lookup_index_record_single_cached_slot(
        BindingsLookupIndex *index, size_t slot_index) {
    if (!index || slot_index >= index->capacity)
        return false;
    BindingsLookupIndexSlot *slot = &index->slots[slot_index];
    if ((slot->single_cache_kind &
         BINDINGS_SINGLE_REACH_CACHE_LISTED) != 0u) {
        return true;
    }
    if (!bindings_lookup_index_single_cached_reserve(
            index, index->single_cached_len + 1u)) {
        return false;
    }
    index->single_cached_ids[index->single_cached_len++] =
        slot->id;
    slot->single_cache_kind |=
        BINDINGS_SINGLE_REACH_CACHE_LISTED;
    return true;
}

static void bindings_lookup_index_unrecord_single_cached_slot(
        BindingsLookupIndex *index, size_t slot_index) {
    if (!index || slot_index >= index->capacity)
        return;
    BindingsLookupIndexSlot *slot = &index->slots[slot_index];
    if ((slot->single_cache_kind &
         BINDINGS_SINGLE_REACH_CACHE_LISTED) == 0u) {
        return;
    }
    size_t cursor = 0u;
    while (cursor < index->single_cached_len &&
           index->single_cached_ids[cursor] != slot->id) {
        cursor++;
    }
    assert(cursor < index->single_cached_len);
    index->single_cached_len--;
    if (cursor < index->single_cached_len) {
        index->single_cached_ids[cursor] =
            index->single_cached_ids[index->single_cached_len];
    }
    slot->single_cache_kind &=
        ~BINDINGS_SINGLE_REACH_CACHE_LISTED;
}

static bool bindings_lookup_index_find_slot(
    const BindingsLookupIndex *index, VarId id, size_t *slot_out);

/* Closed components depend on a prefix of authoritative binding entries.
 * A rollback past that prefix invalidates the fact; removing a later suffix
 * does not. The sparse list uses logical keys so hash-cluster relocation
 * cannot disconnect retained facts from their invalidation records. */
static void bindings_lookup_index_clear_single_reach_cache(
        BindingsLookupIndex *index, uint32_t surviving_prefix) {
#if !defined(CETTA_MUTATION_BINDINGS_SINGLE_REACH_KEEP_ROLLBACK_CACHE)
    if (!index)
        return;
    assert(g_bindings_single_reach_support_invalidation_enabled >= 0);
    bool support_invalidation =
        g_bindings_single_reach_support_invalidation_enabled != 0;
    size_t cached_len = index->single_cached_len;
    size_t kept = 0u;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_SINGLE_REACH_CACHE_INVALIDATION);
    if (support_invalidation) {
        if (index->capacity > cached_len) {
            cetta_runtime_stats_add(
                CETTA_RUNTIME_COUNTER_BINDINGS_SINGLE_REACH_CACHE_SCAN_AVOIDED,
                index->capacity - cached_len);
        }
        for (size_t cursor = 0u; cursor < cached_len; cursor++) {
            size_t slot_index;
            VarId id = index->single_cached_ids[cursor];
            if (!bindings_lookup_index_find_slot(index, id, &slot_index))
                continue;
            BindingsLookupIndexSlot *slot = &index->slots[slot_index];
            /* Zero means that no rollback-stable evidence was recorded. */
            if (surviving_prefix != 0u && !index->has_duplicates &&
                bindings_lookup_index_single_cache_kind(slot) ==
                    BINDINGS_SINGLE_REACH_CACHE_GROUND &&
                slot->closed_prefix != 0u &&
                slot->closed_prefix <= surviving_prefix &&
                slot->index_plus_one <= surviving_prefix) {
                index->single_cached_ids[kept++] = id;
                continue;
            }
            slot->single_terminal = VAR_ID_NONE;
            slot->single_cache_kind = BINDINGS_SINGLE_REACH_CACHE_NONE;
        }
    } else {
        for (size_t slot = 0u; slot < index->capacity; slot++) {
            index->slots[slot].single_terminal = VAR_ID_NONE;
            index->slots[slot].single_cache_kind =
                BINDINGS_SINGLE_REACH_CACHE_NONE;
        }
    }
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_BINDINGS_SINGLE_REACH_CACHE_INVALIDATED_SLOT,
        support_invalidation ? cached_len - kept : index->capacity);
    index->single_cached_len = kept;
#else
    (void)index;
    (void)surviving_prefix;
#endif
}

static size_t bindings_lookup_index_capacity_for_len(uint32_t len) {
    size_t needed = (size_t)len * 2u;
    if (needed < len)
        return 0u;
    size_t capacity = 32u;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2u)
            return 0u;
        capacity *= 2u;
    }
    if (capacity > SIZE_MAX / sizeof(BindingsLookupIndexSlot))
        return 0u;
    return capacity;
}

static bool bindings_lookup_index_insert_raw(
    BindingsLookupIndex *index, VarId id, uint32_t entry_index) {
    if (!index || id == VAR_ID_NONE || index->capacity == 0u)
        return false;
    size_t mask = index->capacity - 1u;
    size_t slot = bindings_var_id_hash(id) & mask;
    while (index->slots[slot].id != VAR_ID_NONE &&
           !binding_var_eq(index->slots[slot].id, id)) {
        slot = (slot + 1u) & mask;
    }
    if (index->slots[slot].id == VAR_ID_NONE) {
        index->slots[slot].id = id;
        index->count++;
    } else if (index->slots[slot].index_plus_one != entry_index + 1u) {
        index->has_duplicates = true;
    }
    bindings_lookup_index_unrecord_single_cached_slot(index, slot);
    index->slots[slot].single_terminal = VAR_ID_NONE;
    index->slots[slot].single_cache_kind =
        BINDINGS_SINGLE_REACH_CACHE_NONE;
    index->slots[slot].index_plus_one = entry_index + 1u;
    return true;
}

static bool bindings_lookup_index_rehash(BindingsLookupIndex *index,
                                         size_t capacity) {
    if (!index || capacity < 32u ||
        (capacity & (capacity - 1u)) != 0u ||
        capacity > SIZE_MAX / sizeof(*index->slots)) {
        return false;
    }
    bindings_lookup_index_clear_single_reach_cache(index, 0u);
    BindingsLookupIndexSlot *old_slots = index->slots;
    size_t old_capacity = index->capacity;
    BindingsLookupIndexSlot *slots =
        cetta_malloc(capacity * sizeof(*slots));
    memset(slots, 0, capacity * sizeof(*slots));
    index->slots = slots;
    index->capacity = capacity;
    index->count = 0u;
    for (size_t i = 0u; i < old_capacity; i++) {
        if (old_slots[i].id == VAR_ID_NONE)
            continue;
        if (!bindings_lookup_index_insert_raw(
                index, old_slots[i].id,
                old_slots[i].index_plus_one - 1u)) {
            free(old_slots);
            return false;
        }
    }
    free(old_slots);
    return true;
}

static BindingsLookupIndex *bindings_lookup_index_build(
    const Bindings *bindings, uint32_t len) {
    size_t capacity = bindings_lookup_index_capacity_for_len(len);
    if (capacity == 0u)
        return NULL;
    BindingsLookupIndex *index =
        bindings_lookup_index_alloc(capacity);
    for (uint32_t i = 0u; i < len; i++) {
        if (bindings_frame_index_owns_id(
                bindings, bindings_entry_at(bindings, i)->var_id)) {
            continue;
        }
        if (!bindings_lookup_index_insert_raw(
                index, bindings_entry_at(bindings, i)->var_id, i)) {
            bindings_lookup_index_release(index);
            return NULL;
        }
    }
    index->synced_len = len;
    return index;
}

static bool bindings_lookup_index_detach(Bindings *bindings) {
    BindingsLookupIndex *index = bindings->lookup_index;
    if (!index ||
        atomic_load_explicit(
            &index->references, memory_order_acquire) == 1u) {
        return true;
    }
    BindingsLookupIndex *copy =
        bindings_lookup_index_alloc(index->capacity);
    memcpy(copy->slots, index->slots,
           index->capacity * sizeof(*copy->slots));
    copy->count = index->count;
    copy->synced_len = index->synced_len;
    copy->has_duplicates = index->has_duplicates;
    if (index->single_cached_len > 0u) {
        copy->single_cached_ids = cetta_malloc(
            index->single_cached_len *
            sizeof(*copy->single_cached_ids));
        memcpy(copy->single_cached_ids,
               index->single_cached_ids,
               index->single_cached_len *
               sizeof(*copy->single_cached_ids));
        copy->single_cached_len = index->single_cached_len;
        copy->single_cached_cap = index->single_cached_len;
    }
    bindings_lookup_index_release(index);
    bindings->lookup_index = copy;
    return true;
}

static bool bindings_lookup_index_find_slot(
    const BindingsLookupIndex *index, VarId id, size_t *slot_out) {
    if (!index || id == VAR_ID_NONE || index->capacity == 0u)
        return false;
    size_t mask = index->capacity - 1u;
    size_t slot = bindings_var_id_hash(id) & mask;
    while (index->slots[slot].id != VAR_ID_NONE) {
        if (binding_var_eq(index->slots[slot].id, id)) {
            if (slot_out)
                *slot_out = slot;
            return true;
        }
        slot = (slot + 1u) & mask;
    }
    return false;
}

static uint32_t bindings_lookup_index_find(
    const BindingsLookupIndex *index, VarId id) {
    size_t slot;
    if (bindings_lookup_index_find_slot(index, id, &slot))
        return index->slots[slot].index_plus_one;
    return 0u;
}

static void bindings_lookup_index_delete_unique(
    BindingsLookupIndex *index, VarId id) {
    if (!index || id == VAR_ID_NONE || index->capacity == 0u)
        return;
    size_t mask = index->capacity - 1u;
    size_t hole = bindings_var_id_hash(id) & mask;
    while (index->slots[hole].id != VAR_ID_NONE &&
           !binding_var_eq(index->slots[hole].id, id)) {
        hole = (hole + 1u) & mask;
    }
    if (index->slots[hole].id == VAR_ID_NONE)
        return;

    size_t scan = (hole + 1u) & mask;
    while (index->slots[scan].id != VAR_ID_NONE) {
        size_t home =
            bindings_var_id_hash(index->slots[scan].id) & mask;
        size_t scan_distance = (scan - home) & mask;
        size_t hole_distance = (hole - home) & mask;
        if (hole_distance < scan_distance) {
            index->slots[hole] = index->slots[scan];
            hole = scan;
        }
        scan = (scan + 1u) & mask;
    }
    memset(&index->slots[hole], 0, sizeof(index->slots[hole]));
    assert(index->count > 0u);
    index->count--;
}

static void bindings_lookup_index_truncate(Bindings *bindings,
                                           uint32_t new_len) {
    BindingsLookupIndex *index = bindings->lookup_index;
    if (!index || index->synced_len <= new_len)
        return;
    if (!bindings_lookup_index_detach(bindings))
        return;
    index = bindings->lookup_index;
    /* A later append may reuse the same logical length with a different
     * suffix.  Derived terminal roots from the discarded suffix must not
     * revive merely because the length matches again. */
    bindings_lookup_index_clear_single_reach_cache(index, new_len);

    if (index->has_duplicates) {
        BindingsLookupIndex *replacement =
            new_len >= BINDINGS_LOOKUP_INDEX_THRESHOLD
                ? bindings_lookup_index_build(bindings, new_len)
                : NULL;
        bindings_lookup_index_release(index);
        bindings->lookup_index = replacement;
        return;
    }

    for (uint32_t i = index->synced_len; i > new_len; i--) {
        VarId id = bindings_entry_at(bindings, i - 1u)->var_id;
        uint32_t found = bindings_lookup_index_find(index, id);
        if (found == i)
            bindings_lookup_index_delete_unique(index, id);
    }
    index->synced_len = new_len;
    if (new_len < BINDINGS_LOOKUP_INDEX_THRESHOLD) {
        bindings_lookup_index_release(index);
        bindings->lookup_index = NULL;
    }
}

static BindingsLookupIndex *bindings_lookup_index_sync(Bindings *bindings) {
    if (!bindings_lookup_index_enabled() ||
        bindings->len < BINDINGS_LOOKUP_INDEX_THRESHOLD) {
        return NULL;
    }
    if (!bindings->lookup_index) {
        bindings->lookup_index =
            bindings_lookup_index_build(bindings, bindings->len);
        return bindings->lookup_index;
    }
    BindingsLookupIndex *index = bindings->lookup_index;
    if (index->synced_len > bindings->len) {
        bindings_lookup_index_truncate(bindings, bindings->len);
        index = bindings->lookup_index;
        if (!index)
            return NULL;
    }
    if (index->synced_len == bindings->len)
        return index;
    if (!bindings_lookup_index_detach(bindings))
        return NULL;
    index = bindings->lookup_index;
    size_t required =
        bindings_lookup_index_capacity_for_len(bindings->len);
    if (required == 0u)
        return NULL;
    if (required > index->capacity &&
        !bindings_lookup_index_rehash(index, required)) {
        return NULL;
    }
    for (uint32_t i = index->synced_len; i < bindings->len; i++) {
        if (bindings_frame_index_owns_id(
                bindings, bindings_entry_at(bindings, i)->var_id)) {
            continue;
        }
        if (!bindings_lookup_index_insert_raw(
                index, bindings_entry_at(bindings, i)->var_id, i)) {
            bindings_lookup_index_release(index);
            bindings->lookup_index = NULL;
            return NULL;
        }
    }
    index->synced_len = bindings->len;
    return index;
}

static inline BindingsLookupIndex *bindings_lookup_index_current(
        Bindings *bindings) {
    BindingsLookupIndex *index = bindings->lookup_index;
    if (index && index->synced_len == bindings->len)
        return index;
    if (index && index->synced_len < bindings->len &&
        bindings->len - index->synced_len == 1u) {
#if !defined(CETTA_MUTATION_BINDINGS_LAZY_TAIL_MUTATE_SHARED)
        if (atomic_load_explicit(
                &index->references, memory_order_acquire) != 1u) {
            return bindings_lookup_index_sync(bindings);
        }
#endif
        if ((size_t)bindings->len > index->capacity / 2u) {
            return bindings_lookup_index_sync(bindings);
        }
#if !defined(CETTA_MUTATION_BINDINGS_LAZY_TAIL_SKIP_INSERT)
        VarId tail_id =
            bindings_entry_at(bindings, index->synced_len)->var_id;
        if (!bindings_frame_index_owns_id(bindings, tail_id)) {
            if (!bindings_lookup_index_insert_raw(
                    index, tail_id, index->synced_len)) {
                return bindings_lookup_index_sync(bindings);
            }
        }
#endif
        index->synced_len = bindings->len;
        return index;
    }
    /* Appends deliberately leave a shared index at its existing prefix.
     * Synchronizing the suffix only when a lookup needs it avoids a
     * copy-on-write mutation on every binding write. */
    return bindings_lookup_index_sync(bindings);
}

#ifdef CETTA_TEST_HOOKS
void bindings_lookup_index_test_clear(Bindings *bindings) {
    if (!bindings)
        return;
    bindings_lookup_index_release(bindings->lookup_index);
    bindings->lookup_index = NULL;
}

bool bindings_lookup_index_test_synced_len(const Bindings *bindings,
                                           uint32_t *synced_len_out) {
    if (!bindings || !bindings->lookup_index || !synced_len_out)
        return false;
    *synced_len_out = bindings->lookup_index->synced_len;
    return true;
}

bool bindings_lookup_index_test_single_cache_support(
        const Bindings *bindings, size_t *support_len_out,
        size_t *capacity_out) {
    if (!bindings || !bindings->lookup_index ||
        !support_len_out || !capacity_out) {
        return false;
    }
    *support_len_out =
        bindings->lookup_index->single_cached_len;
    *capacity_out = bindings->lookup_index->capacity;
    return true;
}

bool bindings_frame_index_test_lookup(
        const Bindings *bindings, VarId id, bool *known_out,
        uint32_t *entry_index_out) {
    if (!known_out || !entry_index_out ||
        !bindings_frame_index_lookup(bindings, id, known_out)) {
        return false;
    }
    *entry_index_out = UINT32_MAX;
    return true;
}

bool bindings_frame_storage_test_identity(
        const Bindings *bindings, uint32_t epoch,
        const void **schema_out, const void **slots_out) {
    if (!bindings || !schema_out || !slots_out)
        return false;
    const BindingsFrameIndexEntry *frame = bindings_frame_index_find_frame_const(
        bindings->frame_index, epoch);
    if (!frame)
        return false;
    *schema_out = frame->schema;
    *slots_out = frame->values;
    return true;
}

bool bindings_lookup_index_test_generic_contains(
        Bindings *bindings, VarId id, bool *present_out) {
    if (!bindings || !present_out)
        return false;
    BindingsLookupIndex *index = bindings_lookup_index_current(bindings);
    if (!index)
        return false;
    *present_out = bindings_lookup_index_find(index, id) != 0u;
    return true;
}

bool bindings_frame_ref_test_for_epoch(
        const Bindings *bindings, uint32_t epoch,
        BindingsFrameRef *ref_out) {
    if (ref_out)
        *ref_out = (BindingsFrameRef){0};
    if (!bindings || !ref_out || epoch == 0u)
        return false;
    const BindingsFrameIndexEntry *entry =
        bindings_frame_index_find_frame_const(
            bindings->frame_index, epoch);
    *ref_out = bindings_frame_ref_from_entry(
        bindings->frame_index, entry);
    return bindings_frame_ref_is_valid(*ref_out);
}

bool bindings_frame_ref_test_resolves(
        const Bindings *bindings, BindingsFrameRef ref,
        uint32_t *epoch_out) {
    if (epoch_out)
        *epoch_out = 0u;
    if (!bindings || !epoch_out)
        return false;
    const BindingsFrameIndexEntry *entry =
        bindings_frame_index_find_ref_const(
            bindings->frame_index, ref);
    if (!entry)
        return false;
    *epoch_out = entry->epoch;
    return true;
}
#endif

static int32_t bindings_lookup_index_slow(Bindings *b, VarId var_id) {
    BindingsLookupIndex *index = bindings_lookup_index_current(b);
    if (index) {
        uint32_t index_plus_one =
            bindings_lookup_index_find(index, var_id);
        if (index_plus_one > 0u) {
            uint32_t idx = index_plus_one - 1u;
            if (idx < b->len &&
                binding_var_eq(bindings_entry_at(b, idx)->var_id, var_id)) {
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_AUTHORITATIVE);
                return (int32_t)idx;
            }
            bindings_lookup_index_release(b->lookup_index);
            b->lookup_index = NULL;
        } else {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_AUTHORITATIVE);
            return -1;
        }
    }
    for (uint32_t i = b->len; i > 0; i--) {
        uint32_t idx = i - 1;
        if (binding_var_eq(bindings_entry_at(b, idx)->var_id, var_id)) {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_AUTHORITATIVE);
            return (int32_t)idx;
        }
    }
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_AUTHORITATIVE);
    return -1;
}

static inline int32_t bindings_lookup_index(Bindings *b, VarId var_id) {
    assert(!binding_id_has_frame(var_id));
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP);
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_GENERIC_MAP);
    cetta_runtime_stats_inc(
        var_epoch_suffix(var_id) == 0u
            ? CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_PLAIN_IDENTITY
            : CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_UNOWNED_CONTEXTUAL);
    BindingsLookupIndex *index = b->lookup_index;
    /* The synchronized index is the overwhelmingly common state on long
     * searches.  Answer it here instead of entering the catch-up/rebuild
     * path on every lookup.  The authoritative Binding is still checked
     * before a positive result is returned; this is only a derived view. */
    if (__builtin_expect(
            index && index->synced_len == b->len, true)) {
        uint32_t index_plus_one =
            bindings_lookup_index_find(index, var_id);
        if (index_plus_one == 0u) {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_AUTHORITATIVE);
            return -1;
        }
        uint32_t found = index_plus_one - 1u;
        if (found < b->len &&
            binding_var_eq(bindings_entry_at(b, found)->var_id, var_id)) {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_AUTHORITATIVE);
            return (int32_t)found;
        }
    }
    if (__builtin_expect(
            index && index->synced_len < b->len, false) &&
        b->len - index->synced_len == 1u) {
        uint32_t tail = index->synced_len;
        if (binding_var_eq(bindings_entry_at(b, tail)->var_id, var_id)) {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_LAZY_TAIL_HIT);
            return (int32_t)tail;
        }
    }
    return bindings_lookup_index_slow(b, var_id);
}

static size_t bindings_dereference_limit(const Bindings *bindings);
static BindingValue bindings_lookup_value(
        Bindings *bindings, BindingValue variable);

static size_t bindings_frame_value_count(const Bindings *bindings) {
    const BindingsFrameIndex *index = bindings
        ? bindings->frame_index : NULL;
    return index ? index->bound_value_count : 0u;
}

typedef bool (*BindingsCurrentBindingVisitor)(
    const Binding *binding, void *context);

bool bindings_iterator_next(BindingsIterator *iterator, Binding *binding_out) {
    if (!iterator || !iterator->bindings || !binding_out)
        return false;
    const Bindings *bindings = iterator->bindings;
    if (iterator->row < bindings->len) {
        *binding_out = *bindings_entry_at(bindings, iterator->row++);
        assert(!binding_id_has_frame(binding_out->var_id));
        return true;
    }
    const BindingsFrameIndex *index = bindings->frame_index;
    while (index && iterator->frame < index->len) {
        const BindingsFrameIndexEntry *frame = &index->frames[iterator->frame];
        while (iterator->slot < frame->slot_len) {
            uint32_t slot = iterator->slot++;
            if (!frame->values[slot].skeleton)
                continue;
            *binding_out = (Binding){
                .var_id = var_epoch_id(bindings_frame_slot_source_id(frame, slot), frame->epoch),
                .spelling = bindings_frame_slot_spelling(frame, slot),
                .name_key = bindings_frame_slot_name_key(frame, slot),
                .value = frame->values[slot],
            };
            return true;
        }
        iterator->frame++;
        iterator->slot = 0u;
    }
    return false;
}

static bool bindings_for_each_current_binding(
        const Bindings *bindings, BindingsCurrentBindingVisitor visitor,
        void *context) {
    if (!bindings || !visitor)
        return false;
    BindingsIterator iterator = {.bindings = bindings};
    Binding binding;
    while (bindings_iterator_next(&iterator, &binding)) {
        if (!visitor(&binding, context))
            return false;
    }
    return true;
}

bool bindings_current_binding_count(const Bindings *bindings, size_t *count_out) {
    if (!bindings || !count_out)
        return false;
    size_t framed = bindings_frame_value_count(bindings);
    if (framed > SIZE_MAX - bindings->len)
        return false;
    *count_out = framed + bindings->len;
    return true;
}

#ifdef CETTA_TEST_HOOKS
bool bindings_current_binding_count_test(
        const Bindings *bindings, size_t *count_out) {
    return bindings_current_binding_count(bindings, count_out);
}
#endif

bool bindings_has_bound_values(const Bindings *bindings) {
    return bindings &&
        (bindings->len != 0u ||
         bindings_frame_value_count(bindings) != 0u);
}

size_t bindings_frame_binding_count(const Bindings *bindings, BindingsFrameRef ref) {
    const BindingsFrameIndexEntry *frame = bindings
        ? bindings_frame_index_find_ref_const(bindings->frame_index, ref) : NULL;
    return frame ? frame->bound_value_count : 0u;
}

static bool bindings_resolve_value_root(
        Bindings *bindings, BindingValue value, BindingValue *out) {
    if (!out || !value.skeleton)
        return false;
    if (!bindings_has_bound_values(bindings)) {
        *out = value;
        return true;
    }
    size_t remaining = bindings_dereference_limit(bindings);
    while (value.skeleton->kind == ATOM_VAR) {
        VarId id = binding_value_variable_id(value);
        BindingValue next = bindings_lookup_value(bindings, value);
        if (!next.skeleton ||
            (next.skeleton->kind == ATOM_VAR && binding_value_variable_id(next) == id))
            break;
        if (remaining-- == 0u)
            return false;
        value = next;
    }
    *out = value;
    return true;
}

bool bindings_resolve_value_preview(
        Bindings *bindings, BindingValue value, BindingValue *out) {
    return bindings_resolve_value_root(bindings, value, out);
}

bool bindings_resolve_value_exact(
        Bindings *bindings, BindingValue value, BindingValue *out) {
    return bindings_resolve_value_root(bindings, value, out);
}

static bool binding_value_may_contain_unbound_var(Bindings *b, BindingValue value);
static bool binding_values_eq_under_bindings(Bindings *b, BindingValue lhs, BindingValue rhs);

static bool constraint_pair_eq(const BindingConstraint *lhs, const BindingConstraint *rhs) {
    return (binding_value_equal(lhs->lhs, rhs->lhs) && binding_value_equal(lhs->rhs, rhs->rhs)) ||
           (binding_value_equal(lhs->lhs, rhs->rhs) && binding_value_equal(lhs->rhs, rhs->lhs));
}

static inline Binding *bindings_exclusive_slot(Bindings *b, uint32_t i) {
    return &b->entries[i - b->shared_len];
}

static void bindings_release_truncated_suffix(Bindings *b) {
    if (!b)
        return;
    if (b->len < b->shared_len) {
        b->shared_len = b->len;
        if (b->shared_len == 0u) {
            bindings_entries_release(b->shared_entries, b->shared_cap);
            b->shared_entries = NULL;
            b->shared_cap = 0u;
        }
        bindings_entries_release(b->entries, b->cap);
        b->entries = NULL;
        b->cap = 0u;
        return;
    }
    if (b->len == b->shared_len) {
        bindings_entries_release(b->entries, b->cap);
        b->entries = NULL;
        b->cap = 0u;
    }
}

static bool bindings_flatten_unique(Bindings *b) {
    uint32_t exclusive;
    uint32_t next_cap;
    Binding *next;
    if (!b)
        return false;
    if (b->shared_len == 0u &&
        bindings_pool_block_is_unique(b->entries, b->cap))
        return true;
    exclusive = b->len - b->shared_len;
    next_cap = BINDINGS_MIN_CAPACITY;
    while (next_cap <= b->len)
        next_cap *= 2u;
    next = bindings_entries_alloc(next_cap);
    if (b->shared_len > 0u)
        memcpy(next, b->shared_entries,
               sizeof(Binding) * b->shared_len);
    if (exclusive > 0u)
        memcpy(next + b->shared_len, b->entries,
               sizeof(Binding) * exclusive);
    bindings_entries_release(b->entries, b->cap);
    bindings_entries_release(b->shared_entries, b->shared_cap);
    b->shared_entries = NULL;
    b->shared_len = 0u;
    b->shared_cap = 0u;
    b->entries = next;
    b->cap = next_cap;
    return true;
}

static bool bindings_reserve_entries(Bindings *b, uint32_t needed) {
    bool exclusive_unique;
    uint32_t exclusive_needed;
    uint32_t exclusive_have;
    uint32_t next_cap;
    Binding *next;
    if (!b)
        return false;
    if (needed < b->shared_len)
        return false;
    exclusive_unique = bindings_pool_block_is_unique(b->entries, b->cap);
    if (b->shared_len == 0u) {
        if (needed <= b->cap && exclusive_unique)
            return true;
        if (b->entries && !exclusive_unique) {
            b->shared_entries = b->entries;
            b->shared_len = b->len;
            b->shared_cap = b->cap;
            b->entries = NULL;
            b->cap = 0u;
            exclusive_unique = true;
        }
    }
    exclusive_needed = needed - b->shared_len;
    if (exclusive_needed == 0u)
        return true;
    if (exclusive_needed <= b->cap && exclusive_unique)
        return true;
    next_cap = b->cap ? b->cap : BINDINGS_MIN_CAPACITY;
    while (next_cap < exclusive_needed)
        next_cap *= 2u;
    if (b->entries && !exclusive_unique) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_BINDINGS_VERSION_ENTRY_DETACH);
    }
    next = bindings_entries_alloc(next_cap);
    exclusive_have = b->len - b->shared_len;
    if (exclusive_have > 0u)
        memcpy(next, b->entries, sizeof(Binding) * exclusive_have);
    bindings_entries_release(b->entries, b->cap);
    b->entries = next;
    b->cap = next_cap;
    return true;
}

static bool bindings_reserve_constraints(Bindings *b, uint32_t needed) {
    bool unique = bindings_pool_block_is_unique(
        b->constraints, b->eq_cap);
    if (needed <= b->eq_cap && unique)
        return true;
    uint32_t next_cap = b->eq_cap ? b->eq_cap : BINDINGS_MIN_CAPACITY;
    while (next_cap < needed) next_cap *= 2;
    if (b->constraints && !unique) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_BINDINGS_VERSION_CONSTRAINT_DETACH);
    }
    BindingConstraint *next = bindings_constraints_alloc(next_cap);
    if (b->eq_len > 0)
        memcpy(next, b->constraints, sizeof(BindingConstraint) * b->eq_len);
    bindings_constraints_release(b->constraints, b->eq_cap);
    b->constraints = next;
    b->eq_cap = next_cap;
    return true;
}

static bool bindings_store_constraint(Bindings *b, BindingValue lhs, BindingValue rhs) {
    BindingConstraint next = {.lhs = lhs, .rhs = rhs};
    for (uint32_t i = 0; i < b->eq_len; i++) {
        if (constraint_pair_eq(&b->constraints[i], &next))
            return true;
    }
    if (!bindings_reserve_constraints(b, b->eq_len + 1)) return false;
    if (!bindings_frame_index_prepare_value_context(
            b, NULL, &lhs, true) ||
        !bindings_frame_index_prepare_value_context(
            b, NULL, &rhs, true)) {
        return false;
    }
    next.lhs = lhs;
    next.rhs = rhs;
    b->constraints[b->eq_len++] = next;
    if (constraint_contains_private_variant_slot(&next))
        b->private_constraint_count++;
    return true;
}

static bool match_decoded_atoms_worklist(BindingValue left, BindingValue right,
                                         Bindings *bindings, BindingsBuilder *builder,
                                         bool undefined_is_wildcard);

static bool bindings_add_inplace_internal(Bindings *b, VarId var_id,
                                          SymbolId spelling, Atom *name_key,
                                          BindingValue val,
                                          bool normalize_constraints);
static bool bindings_add_internal(Bindings *b, VarId var_id, SymbolId spelling,
                                  Atom *name_key, BindingValue val,
                                  bool normalize_constraints);
static bool bindings_add_constraint_inplace_internal(Bindings *b, BindingValue lhs,
                                                     BindingValue rhs,
                                                     bool normalize_constraints);
static bool bindings_add_constraint_internal(Bindings *b, BindingValue lhs, BindingValue rhs,
                                             bool normalize_constraints);
static VarId binding_value_qualify_id(BindingValue value, VarId id) {
    return id != VAR_ID_NONE && binding_value_is_contextual(value)
        ? var_epoch_id(id, value.epoch) : id;
}

static VarId binding_value_single_variable_id(BindingValue value) {
    return binding_value_qualify_id(value, atom_single_variable_id(value.skeleton));
}

/* A contextual support cannot reuse bloom bits computed for source identities.
 * Until support is compiled for its frame, retain a conservative all-bits set. */
static uint32_t binding_value_variable_bloom(BindingValue value) {
    return binding_value_is_contextual(value) && atom_has_vars(value.skeleton)
        ? ATOM_FLAG_VAR_BLOOM_MASK : atom_variable_bloom(value.skeleton);
}

static BindingsReachability bindings_value_reaches_var(
    Bindings *bindings, BindingValue value, VarId target);

static bool bindings_single_reach_path_reserve(size_t needed) {
    if (needed <= g_bindings_single_reach_path_cap)
        return true;
    size_t capacity = g_bindings_single_reach_path_cap
        ? g_bindings_single_reach_path_cap : 32u;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2u) {
            capacity = needed;
            break;
        }
        capacity *= 2u;
    }
    if (capacity > SIZE_MAX / sizeof(*g_bindings_single_reach_path))
        return false;
    g_bindings_single_reach_path = cetta_realloc(
        g_bindings_single_reach_path,
        capacity * sizeof(*g_bindings_single_reach_path));
    g_bindings_single_reach_path_cap = capacity;
    return true;
}

/* Exact path compression for the single-support fragment of the
 * substitution graph.  The binding entries remain authoritative.  A cached
 * terminal variable is trusted only while it is still unbound; an appended
 * binding extends the path through that terminal.  Rollback clears the
 * derived roots in bindings_lookup_index_truncate. */
static BindingsReachability bindings_single_reach_cache_query(
    Bindings *bindings, VarId start, VarId target) {
    BindingsLookupIndex *index;
    uint32_t result_kind = BINDINGS_SINGLE_REACH_CACHE_NONE;
    VarId terminal = VAR_ID_NONE;
    size_t path_len = 0u;
    uint32_t dependency_prefix = 0u;
    bool immutable_path = true;
    VarId current = start;

    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_SINGLE_REACH_CACHE_QUERY);
    if (!bindings || start == VAR_ID_NONE || target == VAR_ID_NONE ||
        bindings->len < BINDINGS_LOOKUP_INDEX_THRESHOLD) {
        goto decline;
    }
    index = bindings_lookup_index_current(bindings);
    if (!index || index->has_duplicates ||
        index->synced_len != bindings->len ||
        !bindings_lookup_index_detach(bindings)) {
        goto decline;
    }
    index = bindings->lookup_index;
    if (!index || index->has_duplicates ||
        index->synced_len != bindings->len ||
        !bindings_single_reach_path_reserve(
            (size_t)bindings->len + 1u)) {
        goto decline;
    }

    for (uint32_t depth = 0u; depth <= bindings->len; depth++) {
        size_t slot_index;
        BindingsLookupIndexSlot *slot;
        BindingValue value;
        VarId single;

        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_BINDINGS_SINGLE_REACH_CACHE_STEP);
        /* Frame-owned coordinates are intentionally absent from this
         * generic-hash cache.  Absence here therefore says nothing about
         * whether the contextual metavariable is bound; let the exact
         * reachability walk follow its frame slot instead. */
        if (bindings_frame_index_owns_id(bindings, current))
            goto decline;
        if (!bindings_lookup_index_find_slot(
                index, current, &slot_index)) {
            result_kind = BINDINGS_SINGLE_REACH_CACHE_TERMINAL;
            terminal = current;
            break;
        }
        slot = &index->slots[slot_index];
        if (dependency_prefix < slot->index_plus_one)
            dependency_prefix = slot->index_plus_one;
        if (path_len >= g_bindings_single_reach_path_cap)
            goto decline;
        g_bindings_single_reach_path[path_len++] = slot_index;
        uint32_t cache_kind =
            bindings_lookup_index_single_cache_kind(slot);
        if (cache_kind ==
                BINDINGS_SINGLE_REACH_CACHE_TERMINAL) {
            if (slot->single_terminal == VAR_ID_NONE)
                goto decline;
            immutable_path = false;
            current = slot->single_terminal;
            continue;
        }
        if (cache_kind ==
                BINDINGS_SINGLE_REACH_CACHE_GROUND) {
            result_kind = BINDINGS_SINGLE_REACH_CACHE_GROUND;
            if (slot->closed_prefix == 0u)
                immutable_path = false;
            else if (dependency_prefix < slot->closed_prefix)
                dependency_prefix = (uint32_t)slot->closed_prefix;
            break;
        }
        if (cache_kind ==
                BINDINGS_SINGLE_REACH_CACHE_COMPLEX) {
            result_kind = BINDINGS_SINGLE_REACH_CACHE_COMPLEX;
            break;
        }
        if (slot->index_plus_one == 0u ||
            slot->index_plus_one > bindings->len) {
            goto decline;
        }
        value = bindings_entry_at(
            bindings, slot->index_plus_one - 1u)->value;
        if (!value.skeleton)
            goto decline;
        if ((value.skeleton->flags & ATOM_FLAG_HASH_STABLE) == 0u)
            immutable_path = false;
        if (!atom_has_vars(value.skeleton)) {
            result_kind = BINDINGS_SINGLE_REACH_CACHE_GROUND;
            break;
        }
        single = binding_value_single_variable_id(value);
        if (single == VAR_ID_NONE) {
            result_kind = BINDINGS_SINGLE_REACH_CACHE_COMPLEX;
            break;
        }
        current = single;
    }
    if (result_kind == BINDINGS_SINGLE_REACH_CACHE_NONE)
        goto decline;

    if (!bindings_lookup_index_single_cached_reserve(
            index, index->single_cached_len + path_len)) {
        goto decline;
    }
    for (size_t cursor = 0u; cursor < path_len; cursor++) {
        size_t slot_index =
            g_bindings_single_reach_path[cursor];
        if (!bindings_lookup_index_record_single_cached_slot(
                index, slot_index)) {
            goto decline;
        }
        BindingsLookupIndexSlot *slot = &index->slots[slot_index];
        slot->single_cache_kind =
            BINDINGS_SINGLE_REACH_CACHE_LISTED | result_kind;
        if (result_kind == BINDINGS_SINGLE_REACH_CACHE_TERMINAL)
            slot->single_terminal = terminal;
        else
            slot->closed_prefix =
                result_kind == BINDINGS_SINGLE_REACH_CACHE_GROUND && immutable_path
                    ? dependency_prefix : 0u;
    }
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_BINDINGS_SINGLE_REACH_CACHE_COMPRESSED,
        path_len);
    if (result_kind == BINDINGS_SINGLE_REACH_CACHE_COMPLEX)
        goto decline;
    if (result_kind == BINDINGS_SINGLE_REACH_CACHE_TERMINAL &&
        binding_var_eq(terminal, target)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_BINDINGS_SINGLE_REACH_CACHE_PRESENT);
        return BINDINGS_REACHABILITY_PRESENT;
    }
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_SINGLE_REACH_CACHE_ABSENCE);
    return BINDINGS_REACHABILITY_ABSENT;

decline:
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_SINGLE_REACH_CACHE_DECLINE);
    return BINDINGS_REACHABILITY_UNKNOWN;
}

static uint32_t bindings_rhs_variable_bloom(const Bindings *bindings) {
    if (!bindings)
        return 0u;
    return (uint32_t)bindings->rhs_variable_bloom[0] |
           ((uint32_t)bindings->rhs_variable_bloom[1] << 8u) |
           ((uint32_t)bindings->rhs_variable_bloom[2] << 16u);
}

static void bindings_rhs_variable_bloom_add(
        Bindings *bindings, BindingValue value) {
    if (!bindings || !value.skeleton)
        return;
    uint32_t compact =
        binding_value_variable_bloom(value) >> ATOM_FLAG_VAR_BLOOM_SHIFT;
    bindings->rhs_variable_bloom[0] |= (uint8_t)compact;
    bindings->rhs_variable_bloom[1] |= (uint8_t)(compact >> 8u);
    bindings->rhs_variable_bloom[2] |= (uint8_t)(compact >> 16u);
}

static void bindings_rhs_variable_bloom_rebuild(Bindings *bindings) {
    if (!bindings)
        return;
    memset(bindings->rhs_variable_bloom, 0,
           sizeof(bindings->rhs_variable_bloom));
    for (uint32_t index = 0u; index < bindings->len; index++) {
        bindings_rhs_variable_bloom_add(
            bindings, bindings_entry_at(bindings, index)->value);
    }
    if (bindings->frame_index) {
        for (uint32_t frame_index = 0u;
             frame_index < bindings->frame_index->len; frame_index++) {
            const BindingsFrameIndexEntry *frame =
                &bindings->frame_index->frames[frame_index];
            for (uint32_t slot = 0u;
                 slot < frame->slot_len; slot++) {
                if (frame->values[slot].skeleton) {
                    bindings_rhs_variable_bloom_add(
                        bindings, frame->values[slot]);
                }
            }
        }
    }
}

typedef enum {
    BINDINGS_BIND_ADMIT,
    BINDINGS_BIND_REFUSE,
    BINDINGS_BIND_AUDIT,
} BindingsBindVerdict;

static void bindings_cycle_memoize_audit(const Bindings *b, uint8_t verdict);

/* Evidence about the edge var_id -> value before it is written: does the
 * value already reach the variable?  Cheap proofs come first (a ground value,
 * the support summaries, the single-variable cache), then the reachability
 * walk.  Nothing here writes the memo. */
static BindingsReachability bindings_bind_evidence(
        Bindings *bindings, VarId var_id, BindingValue value) {
    if (!atom_has_vars(value.skeleton)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_BINDINGS_CYCLE_GROUND_VALUE);
        return BINDINGS_REACHABILITY_ABSENT;
    }
    uint32_t target =
        atom_var_bloom_for_id(var_id) >> ATOM_FLAG_VAR_BLOOM_SHIFT;
    uint32_t value_support =
        binding_value_variable_bloom(value) >> ATOM_FLAG_VAR_BLOOM_SHIFT;
    if ((value_support & target) != target &&
        (bindings_rhs_variable_bloom(bindings) & target) != target) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_BINDINGS_CYCLE_SUPPORT_ABSENCE);
        return BINDINGS_REACHABILITY_ABSENT;
    }
    VarId single = binding_value_single_variable_id(value);
    if (single != VAR_ID_NONE) {
        BindingsReachability cached =
            bindings_single_reach_cache_query(bindings, single, var_id);
        if (cached != BINDINGS_REACHABILITY_UNKNOWN)
            return cached;
    }
    return bindings_value_reaches_var(bindings, value, var_id);
}

/* The occurs check, at the bind.  A bind that would close a cycle is refused
 * here, so an environment built through the bind paths is acyclic after every
 * successful bind and needs no audit after a match.  The reachability walk is
 * exact on an acyclic identifier graph. An environment whose memo is not
 * acyclic may already hold a cycle; a trial write requires the full audit. */
static BindingsBindVerdict bindings_bind_verdict(
        Bindings *bindings, VarId var_id, BindingValue value, bool has_evidence,
        BindingsReachability evidence) {
    if (bindings->cycle_state != BINDINGS_CYCLE_ACYCLIC)
        return BINDINGS_BIND_AUDIT;
    BindingsReachability reaches =
        has_evidence && evidence != BINDINGS_REACHABILITY_UNKNOWN
            ? evidence
            : bindings_bind_evidence(bindings, var_id, value);
    if (reaches == BINDINGS_REACHABILITY_PRESENT)
        return BINDINGS_BIND_REFUSE;
    if (reaches == BINDINGS_REACHABILITY_ABSENT)
        return BINDINGS_BIND_ADMIT;
    return BINDINGS_BIND_AUDIT;
}

/* The audit after a trial write; the memo is opened so the audit walks and
 * then records its verdict. */
static bool bindings_trial_is_acyclic(Bindings *bindings) {
    bindings->cycle_state = BINDINGS_CYCLE_UNKNOWN;
    return !bindings_has_loop(bindings);
}

static bool bindings_normalize_constraints(Bindings *b) {
    if (b->eq_len == 0) return true;
    BindingConstraint pending_stack[BINDINGS_TEMP_STACK_CAP];
    BindingConstraint *pending = bindings_temp_constraints_alloc(
        b->eq_len, pending_stack, BINDINGS_TEMP_STACK_CAP);
    uint32_t npending = b->eq_len;
    for (uint32_t i = 0; i < npending; i++)
        pending[i] = b->constraints[i];
    b->eq_len = 0;
    b->private_constraint_count = 0u;
    for (uint32_t i = 0; i < npending; i++) {
        if (!bindings_add_constraint_inplace_internal(
                b, pending[i].lhs, pending[i].rhs, false)) {
            bindings_temp_constraints_release(pending, npending,
                                              pending_stack);
            return false;
        }
    }
    bindings_temp_constraints_release(pending, npending, pending_stack);
    return true;
}

/* --- Prime per-occurrence (prime_ext) accessors -------------------------- *
 * The occurrence is absent (NULL) in pure-HE evaluation; reads then resolve to
 * a shared zero-initialized singleton (top==NULL => "not present"), exactly
 * matching the former zero-inited inline fields.  Mutable views materialize the
 * occurrence lazily -- only Prime evaluation reaches them. */
static const PrimeOccurrence g_prime_occurrence_empty;

const PrimeNeedSnapshot *bindings_need_view(const Bindings *b) {
    return b->prime_ext ? &b->prime_ext->prime_need
                        : &g_prime_occurrence_empty.prime_need;
}

const PrimeNeedBranchState *bindings_branch_state_view(const Bindings *b) {
    return b->prime_ext ? &b->prime_ext->branch_state
                        : &g_prime_occurrence_empty.branch_state;
}

static PrimeOccurrence *bindings_prime_ext_materialize(Bindings *b) {
    if (!b->prime_ext) {
        PrimeOccurrence *ext = cetta_malloc(sizeof(PrimeOccurrence));
        prime_need_snapshot_init(&ext->prime_need);
        prime_need_branch_state_init(&ext->branch_state);
        ext->occurrence_token = 0u;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
        prime_need_receipt_init(&ext->receipt);
#endif
        b->prime_ext = ext;
    }
    return b->prime_ext;
}

PrimeNeedSnapshot *bindings_need_mut(Bindings *b) {
    return &bindings_prime_ext_materialize(b)->prime_need;
}

PrimeNeedBranchState *bindings_branch_state_mut(Bindings *b) {
    return &bindings_prime_ext_materialize(b)->branch_state;
}

#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
const PrimeNeedReceipt *bindings_receipt_view(const Bindings *b) {
    return b->prime_ext ? &b->prime_ext->receipt
                        : &g_prime_occurrence_empty.receipt;
}

PrimeNeedReceipt *bindings_receipt_mut(Bindings *b) {
    return &bindings_prime_ext_materialize(b)->receipt;
}
#endif

uint64_t bindings_occurrence_token(const Bindings *b) {
    return b && b->prime_ext ? b->prime_ext->occurrence_token : 0u;
}

bool bindings_refresh_occurrence_token(Bindings *b) {
    if (!b)
        return false;
    uint64_t token = prime_need_fresh_source_occurrence();
    if (token == 0u)
        return false;
    bindings_prime_ext_materialize(b)->occurrence_token = token;
    return true;
}

bool bindings_prime_present(const Bindings *b) {
    if (!b->prime_ext)
        return false;
    return prime_need_snapshot_present(&b->prime_ext->prime_need) ||
           prime_need_branch_state_present(&b->prime_ext->branch_state) ||
           b->prime_ext->occurrence_token != 0u
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
           || prime_need_receipt_present(&b->prime_ext->receipt)
#endif
           ;
}

/* Reset an existing occurrence to empty without freeing it (kept for reuse to
 * avoid alloc churn on hot Prime merge/rollback paths). */
static void prime_occurrence_reset(PrimeOccurrence *ext) {
    prime_need_snapshot_init(&ext->prime_need);
    prime_need_branch_state_init(&ext->branch_state);
    ext->occurrence_token = 0u;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    prime_need_receipt_init(&ext->receipt);
#endif
}

void bindings_prime_assign(Bindings *dst, const Bindings *src) {
    if (src->prime_ext &&
        (prime_need_snapshot_present(&src->prime_ext->prime_need) ||
         prime_need_branch_state_present(&src->prime_ext->branch_state) ||
         src->prime_ext->occurrence_token != 0u
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
         || prime_need_receipt_present(&src->prime_ext->receipt)
#endif
         )) {
        *bindings_prime_ext_materialize(dst) = *src->prime_ext;
    } else if (dst->prime_ext) {
        prime_occurrence_reset(dst->prime_ext);
    }
}

void bindings_prime_set(Bindings *dst, const PrimeNeedSnapshot *need,
                        const PrimeNeedBranchState *branch_state,
                        uint64_t occurrence_token,
                        const PrimeNeedReceipt *receipt) {
    bool present = (need && prime_need_snapshot_present(need)) ||
                   (branch_state &&
                    prime_need_branch_state_present(branch_state)) ||
                   occurrence_token != 0u;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    present = present ||
              (receipt && prime_need_receipt_present(receipt));
#else
    (void)receipt;
#endif
    if (present) {
        PrimeOccurrence *ext = bindings_prime_ext_materialize(dst);
        if (need) ext->prime_need = *need;
        else prime_need_snapshot_init(&ext->prime_need);
        if (branch_state) ext->branch_state = *branch_state;
        else prime_need_branch_state_init(&ext->branch_state);
        ext->occurrence_token = occurrence_token;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
        if (receipt) ext->receipt = *receipt;
        else prime_need_receipt_init(&ext->receipt);
#endif
    } else if (dst->prime_ext) {
        prime_occurrence_reset(dst->prime_ext);
    }
}

void bindings_init(Bindings *b) {
    b->owners = NULL;
    b->entries = NULL;
    b->len = 0;
    b->cap = 0;
    b->constraints = NULL;
    b->eq_len = 0;
    b->eq_cap = 0;

    b->private_entry_count = 0u;
    b->private_constraint_count = 0u;
    b->cycle_state = BINDINGS_CYCLE_ACYCLIC;
    memset(b->rhs_variable_bloom, 0, sizeof(b->rhs_variable_bloom));
    b->lookup_index = NULL;
    b->frame_index = NULL;
    b->shared_entries = NULL;
    b->shared_len = 0u;
    b->shared_cap = 0u;
    b->prime_ext = NULL;
}

void bindings_free(Bindings *b) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_FREE);
    if (b->cap > 0 || b->eq_cap > 0)
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_FREE_NONEMPTY);
    if (b->cap > 0)
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_BINDINGS_RELEASED_ENTRY_CAPACITY,
                                b->cap);
    if (b->eq_cap > 0)
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_BINDINGS_RELEASED_CONSTRAINT_CAPACITY,
            b->eq_cap);
    bindings_entries_release(b->entries, b->cap);
    bindings_entries_release(b->shared_entries, b->shared_cap);
    bindings_constraints_release(b->constraints, b->eq_cap);
    bindings_lookup_index_release(b->lookup_index);
    b->lookup_index = NULL;
    bindings_frame_index_release(b->frame_index);
    b->frame_index = NULL;
    if (b->prime_ext) {
        free(b->prime_ext);
        b->prime_ext = NULL;
    }
    if (b->owners)
        bindings_owners_release(b->owners);
    bindings_init(b);
}

static bool bindings_fork_version(Bindings *dst, const Bindings *src);

bool bindings_clone(Bindings *dst, const Bindings *src) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_CLONE);
    return bindings_fork_version(dst, src);
}

/* Capture an independently writable continuation image by retaining a frozen
 * prefix.  Any later in-place write detaches.  Lookup summaries have their
 * own existing COW lifetime; Prime occurrence state remains independently
 * owned. */
static bool bindings_fork_version(Bindings *dst, const Bindings *src) {
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_VERSION_FORK);
    bindings_init(dst);
    if (src->shared_len > 0u) {
        if (!bindings_pool_block_retain(
                src->shared_entries, src->shared_cap))
            return false;
        dst->shared_entries = src->shared_entries;
        dst->shared_len = src->shared_len;
        dst->shared_cap = src->shared_cap;
        dst->len = src->shared_len;
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_BINDINGS_VERSION_ENTRY_SHARE);
    }
    {
        uint32_t exclusive = src->len - src->shared_len;
        if (exclusive > 0u) {
            if (bindings_pool_block_retain(src->entries, src->cap)) {
                dst->entries = src->entries;
                dst->len = src->len;
                dst->cap = src->cap;
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_BINDINGS_VERSION_ENTRY_SHARE);
            } else {
                if (!bindings_reserve_entries(dst, src->len))
                    return false;
                memcpy(dst->entries, src->entries,
                       sizeof(Binding) * exclusive);
                dst->len = src->len;
            }
        } else if (src->shared_len == 0u && src->len > 0u) {
            if (bindings_pool_block_retain(src->entries, src->cap)) {
                dst->entries = src->entries;
                dst->len = src->len;
                dst->cap = src->cap;
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_BINDINGS_VERSION_ENTRY_SHARE);
            } else {
                if (!bindings_reserve_entries(dst, src->len))
                    return false;
                memcpy(dst->entries, src->entries,
                       sizeof(Binding) * src->len);
                dst->len = src->len;
            }
        }
    }
    if (src->eq_len > 0u) {
        if (bindings_pool_block_retain(src->constraints, src->eq_cap)) {
            dst->constraints = src->constraints;
            dst->eq_len = src->eq_len;
            dst->eq_cap = src->eq_cap;
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_BINDINGS_VERSION_CONSTRAINT_SHARE);
        } else {
            if (!bindings_reserve_constraints(dst, src->eq_len)) {
                bindings_free(dst);
                return false;
            }
            memcpy(dst->constraints, src->constraints,
                   sizeof(BindingConstraint) * src->eq_len);
            dst->eq_len = src->eq_len;
        }
    }
    if (src->lookup_index) {
        bindings_lookup_index_retain(src->lookup_index);
        dst->lookup_index = src->lookup_index;
    }
    if (src->frame_index) {
        bindings_frame_index_retain(src->frame_index);
        dst->frame_index = src->frame_index;
    }
    dst->private_entry_count = src->private_entry_count;
    dst->private_constraint_count = src->private_constraint_count;

    dst->cycle_state = src->cycle_state;
    memcpy(dst->rhs_variable_bloom, src->rhs_variable_bloom,
           sizeof(dst->rhs_variable_bloom));
    if (src->owners)
        bindings_inherit_owners(dst, src);
    bindings_prime_assign(dst, src);
    return true;
}

bool bindings_prepare_logical_write(Bindings *bindings) {
    if (!bindings)
        return false;
    return bindings_flatten_unique(bindings) &&
        bindings_reserve_entries(bindings, bindings->len) &&
        bindings_reserve_constraints(bindings, bindings->eq_len);
}

typedef struct {
    Bindings *destination;
    BindingsAtomTransportFn transport;
    void *transport_context;
    bool ok;
} BindingsTransportCurrentContext;

static bool bindings_transport_current_binding(
        const Binding *source, void *raw_context) {
    BindingsTransportCurrentContext *context = raw_context;
    Atom *name_key = source->name_key
        ? context->transport(context->transport_context, source->name_key)
        : NULL;
    BindingValue value = source->value;
    value.skeleton = context->transport(
        context->transport_context, source->value.skeleton);
    context->ok = (!source->name_key || name_key) && value.skeleton &&
        bindings_add_inplace_internal(
            context->destination, source->var_id,
            source->spelling, name_key, value,
            false);
    return context->ok;
}

static bool bindings_transport_frame_presentations(
        Bindings *bindings, BindingsAtomTransportFn transport,
        void *context) {
    if (!bindings || !bindings->frame_index)
        return true;
    if (!bindings_frame_index_detach(bindings))
        return false;
    for (uint32_t frame_index = 0u;
         frame_index < bindings->frame_index->len; frame_index++) {
        BindingsFrameIndexEntry *frame =
            &bindings->frame_index->frames[frame_index];
        bool has_key = false;
        for (uint32_t slot = 0u; slot < frame->schema->len; slot++)
            has_key = has_key || bindings_frame_slot_name_key(frame, slot) != NULL;
        if (!has_key)
            continue;
        BindingsFrameSchema *schema = bindings_frame_schema_clone(
            frame->schema);
        if (!schema)
            return false;
        for (uint32_t slot = 0u; slot < schema->len; slot++) {
            if (!schema->name_keys[slot])
                continue;
            schema->name_keys[slot] = transport(
                context, schema->name_keys[slot]);
            if (!schema->name_keys[slot]) {
                bindings_frame_schema_release(schema);
                return false;
            }
        }
        bindings_frame_schema_release(frame->schema);
        frame->schema = schema;
        if (bindings->frame_index->schema_revision != UINT64_MAX)
            bindings->frame_index->schema_revision++;
    }
    return true;
}

bool bindings_transport_logical(Bindings *dst, const Bindings *src,
                                BindingsAtomTransportFn transport,
                                void *context) {
    if (!dst || !src || dst == src)
        return false;

    bindings_init(dst);
    if (!transport || bindings_prime_present(src))
        return false;
    if (src->owners)
        bindings_inherit_owners(dst, src);
    if (src->eq_len > 0u &&
        !bindings_reserve_constraints(dst, src->eq_len)) {
        bindings_free(dst);
        return false;
    }

    if (!bindings_frame_index_merge_schemas(dst, src) ||
        !bindings_transport_frame_presentations(dst, transport, context))
        goto fail;

    BindingsTransportCurrentContext transport_context = {
        .destination = dst,
        .transport = transport,
        .transport_context = context,
        .ok = true,
    };
    if (!bindings_for_each_current_binding(
            src, bindings_transport_current_binding,
            &transport_context) || !transport_context.ok)
        goto fail;

    for (uint32_t i = 0u; i < src->eq_len; i++) {
        BindingConstraint target = src->constraints[i];
        target.lhs.skeleton = transport(context, target.lhs.skeleton);
        target.rhs.skeleton = transport(context, target.rhs.skeleton);
        if (!target.lhs.skeleton || !target.rhs.skeleton ||
            !bindings_frame_index_prepare_value_context(
                dst, NULL, &target.lhs, false) ||
            !bindings_frame_index_prepare_value_context(
                dst, NULL, &target.rhs, false)) {
            goto fail;
        }
        dst->constraints[dst->eq_len++] = target;
        if (constraint_contains_private_variant_slot(&target))
            dst->private_constraint_count++;
    }

    dst->cycle_state =
        src->cycle_state == BINDINGS_CYCLE_ACYCLIC
            ? BINDINGS_CYCLE_ACYCLIC
            : BINDINGS_CYCLE_UNKNOWN;
    return true;

fail:
    bindings_free(dst);
    return false;
}

static bool binding_prefix_item_equal(const Binding *left,
                                      const Binding *right) {
    if (!left || !right ||
        left->var_id != right->var_id ||
        left->spelling != right->spelling) {
        return false;
    }
    if ((left->name_key || right->name_key) &&
        (!left->name_key || !right->name_key ||
         !atom_eq(left->name_key, right->name_key))) {
        return false;
    }
    return binding_value_equal(left->value, right->value);
}

typedef struct {
    Bindings *other;
    bool equal;
} BindingsFactorBaseContext;

static bool bindings_factor_base_binding_present(
        const Binding *binding, void *raw_context) {
    BindingsFactorBaseContext *context = raw_context;
    BindingValue other = bindings_lookup_value_id(
        context->other, binding->var_id);
    context->equal = other.skeleton &&
        binding_value_equal(binding->value, other);
    return context->equal;
}

typedef struct {
    const Bindings *base;
    Bindings *suffix;
    bool ok;
} BindingsFactorSuffixContext;

static bool bindings_factor_append_suffix_binding(
        const Binding *binding, void *raw_context) {
    BindingsFactorSuffixContext *context = raw_context;
    BindingValue base_value = bindings_lookup_value_id(
        (Bindings *)context->base, binding->var_id);
    if (base_value.skeleton) {
        context->ok = binding_value_equal(
            base_value, binding->value);
        return context->ok;
    }
    context->ok = bindings_add_inplace_internal(
        context->suffix, binding->var_id,
        binding->spelling, binding->name_key,
        binding->value, false);
    return context->ok;
}

bool bindings_factor_prefix(Bindings *full, const Bindings *base,
                            bool *factored,
                            uint64_t *logical_items_elided) {
    if (factored)
        *factored = false;
    if (logical_items_elided)
        *logical_items_elided = 0u;
    if (!full || !base || full == base || !factored)
        return false;
    if ((base->len == 0u && base->eq_len == 0u &&
         bindings_frame_value_count(base) == 0u) ||
        full->len < base->len || full->eq_len < base->eq_len) {
        return true;
    }
    if (bindings_frame_value_count(base) != 0u ||
        bindings_frame_value_count(full) != 0u) {
        BindingsFactorBaseContext base_context = {
            .other = full,
            .equal = true,
        };
        if (!bindings_for_each_current_binding(
                base, bindings_factor_base_binding_present,
                &base_context) || !base_context.equal) {
            return true;
        }
        for (uint32_t i = 0u; i < base->eq_len; i++) {
            if (i >= full->eq_len || !constraint_pair_eq(
                    &full->constraints[i], &base->constraints[i])) {
                return true;
            }
        }

        Bindings suffix;
        bindings_init(&suffix);
        if (!bindings_frame_index_merge_schemas(&suffix, full)) {
            bindings_free(&suffix);
            return false;
        }
        BindingsFactorSuffixContext suffix_context = {
            .base = base,
            .suffix = &suffix,
            .ok = true,
        };
        if (!bindings_for_each_current_binding(
                full, bindings_factor_append_suffix_binding,
                &suffix_context) || !suffix_context.ok) {
            bindings_free(&suffix);
            return false;
        }
        for (uint32_t i = base->eq_len; i < full->eq_len; i++) {
            BindingConstraint item = full->constraints[i];
            if (!bindings_reserve_constraints(
                    &suffix, suffix.eq_len + 1u) ||
                !bindings_frame_index_prepare_value_context(
                    &suffix, NULL, &item.lhs, false) ||
                !bindings_frame_index_prepare_value_context(
                    &suffix, NULL, &item.rhs, false)) {
                bindings_free(&suffix);
                return false;
            }
            suffix.constraints[suffix.eq_len++] = item;
            if (constraint_contains_private_variant_slot(&item))
                suffix.private_constraint_count++;
        }
        suffix.cycle_state = bindings_has_bound_values(&suffix) &&
                full->cycle_state != BINDINGS_CYCLE_ACYCLIC
            ? BINDINGS_CYCLE_UNKNOWN : BINDINGS_CYCLE_ACYCLIC;

        bool same_need =
            prime_need_snapshot_is_ancestor(
                bindings_need_view(base), bindings_need_view(full)) &&
            prime_need_snapshot_is_ancestor(
                bindings_need_view(full), bindings_need_view(base));
        bool same_branch_state = prime_need_branch_state_equal(
            bindings_branch_state_view(base),
            bindings_branch_state_view(full));
        bool same_occurrence = bindings_occurrence_token(base) ==
            bindings_occurrence_token(full);
        bool same_receipt = true;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
        same_receipt = prime_need_receipt_equal(
            bindings_receipt_view(base), bindings_receipt_view(full));
#endif
        if (full->owners)
            bindings_inherit_owners(&suffix, full);
        if (!same_need || !same_branch_state || !same_receipt ||
            !same_occurrence)
            bindings_prime_assign(&suffix, full);

        size_t base_binding_count = 0u;
        if (!bindings_current_binding_count(
                base, &base_binding_count)) {
            bindings_free(&suffix);
            return false;
        }
        uint64_t elided = base_binding_count > UINT64_MAX - base->eq_len
            ? UINT64_MAX
            : (uint64_t)base_binding_count + base->eq_len;
        bindings_replace(full, &suffix);
        *factored = true;
        if (logical_items_elided)
            *logical_items_elided = elided;
        return true;
    }
    for (uint32_t i = 0u; i < base->len; i++) {
        if (!binding_prefix_item_equal(
                bindings_entry_at(full, i), bindings_entry_at(base, i))) {
            return true;
        }
    }
    for (uint32_t i = 0u; i < base->eq_len; i++) {
        if (!constraint_pair_eq(
                &full->constraints[i], &base->constraints[i])) {
            return true;
        }
    }

    Bindings suffix;
    bindings_init(&suffix);
    uint32_t suffix_len = full->len - base->len;
    uint32_t suffix_eq_len = full->eq_len - base->eq_len;
    if (suffix_len > 0u &&
        !bindings_reserve_entries(&suffix, suffix_len)) {
        bindings_free(&suffix);
        return false;
    }
    if (suffix_eq_len > 0u &&
        !bindings_reserve_constraints(&suffix, suffix_eq_len)) {
        bindings_free(&suffix);
        return false;
    }
    for (uint32_t i = base->len; i < full->len; i++) {
        Binding item = *bindings_entry_at(full, i);
        suffix.entries[suffix.len++] = item;
        bindings_rhs_variable_bloom_add(&suffix, item.value);

        if (binding_contains_private_variant_slot(&item))
            suffix.private_entry_count++;
    }
    for (uint32_t i = base->eq_len; i < full->eq_len; i++) {
        BindingConstraint item = full->constraints[i];
        suffix.constraints[suffix.eq_len++] = item;
        if (constraint_contains_private_variant_slot(&item))
            suffix.private_constraint_count++;
    }
    if (!bindings_frame_index_merge_schemas(&suffix, full)) {
        bindings_free(&suffix);
        return false;
    }
    suffix.cycle_state =
        !bindings_has_bound_values(&suffix) || full->cycle_state == BINDINGS_CYCLE_ACYCLIC
            ? BINDINGS_CYCLE_ACYCLIC
            : BINDINGS_CYCLE_UNKNOWN;

    bool same_need =
        prime_need_snapshot_is_ancestor(bindings_need_view(base),
                                        bindings_need_view(full)) &&
        prime_need_snapshot_is_ancestor(bindings_need_view(full),
                                        bindings_need_view(base));
    bool same_branch_state =
        prime_need_branch_state_equal(bindings_branch_state_view(base),
                                      bindings_branch_state_view(full));
    bool same_occurrence =
        bindings_occurrence_token(base) ==
        bindings_occurrence_token(full);
    bool same_receipt = true;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    same_receipt = prime_need_receipt_equal(
        bindings_receipt_view(base), bindings_receipt_view(full));
#endif
    if (full->owners)
        bindings_inherit_owners(&suffix, full);
    if (!same_need || !same_branch_state || !same_receipt ||
        !same_occurrence)
        bindings_prime_assign(&suffix, full);

    uint64_t elided = (uint64_t)base->len + (uint64_t)base->eq_len;
    bindings_replace(full, &suffix);
    *factored = true;
    if (logical_items_elided)
        *logical_items_elided = elided;
    return true;
}

bool bindings_copy(Bindings *dst, const Bindings *src) {
    if (dst == src) return true;
    Bindings tmp;
    if (!bindings_clone(&tmp, src)) return false;
    bindings_free(dst);
    *dst = tmp;
    return true;
}

bool bindings_promote_logical_atoms_with_session(
    Bindings *bindings, AtomDeepCopySession *session) {
    if (!bindings)
        return true;
    if (!session)
        return false;
    if (!bindings_prepare_logical_write(bindings))
        return false;
    for (uint32_t i = 0; i < bindings->len; i++) {
        Atom *promoted_name_key = bindings->entries[i].name_key
            ? atom_deep_copy_session_copy(
                  session, bindings->entries[i].name_key)
            : NULL;
        Atom *promoted = bindings->entries[i].value.skeleton
            ? atom_deep_copy_session_copy(
                  session, bindings->entries[i].value.skeleton)
            : NULL;
        if ((bindings->entries[i].name_key && !promoted_name_key) ||
            (bindings->entries[i].value.skeleton && !promoted))
            return false;
        bindings->entries[i].name_key = promoted_name_key;
        bindings->entries[i].value.skeleton = promoted;
    }
    for (uint32_t i = 0; i < bindings->eq_len; i++) {
        Atom *lhs = bindings->constraints[i].lhs.skeleton
            ? atom_deep_copy_session_copy(
                  session, bindings->constraints[i].lhs.skeleton)
            : NULL;
        Atom *rhs = bindings->constraints[i].rhs.skeleton
            ? atom_deep_copy_session_copy(
                  session, bindings->constraints[i].rhs.skeleton)
            : NULL;
        if ((bindings->constraints[i].lhs.skeleton && !lhs) ||
            (bindings->constraints[i].rhs.skeleton && !rhs)) {
            return false;
        }
        bindings->constraints[i].lhs.skeleton = lhs;
        bindings->constraints[i].rhs.skeleton = rhs;
    }
    if (bindings->frame_index) {
        /* A frame whose keys and values already live closed in the
         * destination arena stays shared: promotion would rewrite every
         * pointer to itself.  Only frames holding foreign syntax are
         * detached and copied, and the directory is detached only once
         * such a frame is found. */
        bool directory_private = false;
        for (uint32_t frame_index = 0u;
             frame_index < bindings->frame_index->len; frame_index++) {
            BindingsFrameIndexEntry *frame =
                &bindings->frame_index->frames[frame_index];
            bool foreign_name_keys = false;
            bool foreign_values = false;
            for (uint32_t slot = 0u;
                 slot < frame->slot_len; slot++) {
                Atom *name_key = bindings_frame_slot_name_key(frame, slot);
                if (name_key &&
                    !atom_deep_copy_session_settled(session, name_key))
                    foreign_name_keys = true;
                Atom *value = frame->values[slot].skeleton;
                if (value &&
                    !atom_deep_copy_session_settled(session, value))
                    foreign_values = true;
            }
            if (!foreign_name_keys && !foreign_values)
                continue;
            if (!directory_private) {
                if (!bindings_frame_index_detach(bindings))
                    return false;
                directory_private = true;
                frame = &bindings->frame_index->frames[frame_index];
            }
            if (foreign_name_keys) {
                BindingsFrameSchema *promoted_schema =
                    bindings_frame_schema_clone(frame->schema);
                if (!promoted_schema)
                    return false;
                for (uint32_t slot = 0u;
                     slot < promoted_schema->len; slot++) {
                    if (!promoted_schema->name_keys[slot])
                        continue;
                    promoted_schema->name_keys[slot] =
                        atom_deep_copy_session_copy(
                            session,
                            promoted_schema->name_keys[slot]);
                    if (!promoted_schema->name_keys[slot]) {
                        bindings_frame_schema_release(
                            promoted_schema);
                        return false;
                    }
                }
                bindings_frame_schema_release(frame->schema);
                frame->schema = promoted_schema;
            }
            if (!foreign_values)
                continue;
            if (!bindings_frame_index_entry_detach(frame))
                return false;
            for (uint32_t slot = 0u;
                 slot < frame->slot_len; slot++) {
                if (!frame->values[slot].skeleton) {
                    continue;
                }
                Atom *copy = atom_deep_copy_session_copy(
                    session, frame->values[slot].skeleton);
                if (!copy)
                    return false;
                frame->values[slot].skeleton = copy;
            }
        }
    }
    /* Promotion preserves variable identities and the retained frame inventory;
     * it changes only syntax ownership. No context rediscovery is required. */
    return true;
}

bool bindings_logical_atoms_closed_for_arena(
    const Bindings *bindings, const Arena *arena) {
    if (!bindings || !arena)
        return bindings == NULL;
    for (uint32_t i = 0u; i < bindings->len; i++) {
        const Binding *entry = bindings_entry_at(bindings, i);
        if ((entry->name_key &&
             !atom_graph_is_closed_for_arena(arena, entry->name_key)) ||
            (entry->value.skeleton &&
             !atom_graph_is_closed_for_arena(arena, entry->value.skeleton))) {
            return false;
        }
    }
    for (uint32_t i = 0u; i < bindings->eq_len; i++) {
        const BindingConstraint *constraint = &bindings->constraints[i];
        if ((constraint->lhs.skeleton &&
             !atom_graph_is_closed_for_arena(arena, constraint->lhs.skeleton)) ||
            (constraint->rhs.skeleton &&
             !atom_graph_is_closed_for_arena(arena, constraint->rhs.skeleton))) {
            return false;
        }
    }
    if (bindings->frame_index) {
        for (uint32_t frame_index = 0u;
             frame_index < bindings->frame_index->len; frame_index++) {
            const BindingsFrameIndexEntry *frame =
                &bindings->frame_index->frames[frame_index];
            for (uint32_t slot = 0u;
                 slot < frame->slot_len; slot++) {
                if (bindings_frame_slot_name_key(frame, slot) &&
                    !atom_graph_is_closed_for_arena(
                        arena, bindings_frame_slot_name_key(frame, slot))) {
                    return false;
                }
                if (frame->values[slot].skeleton &&
                    !atom_graph_is_closed_for_arena(
                        arena, frame->values[slot].skeleton)) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool bindings_logical_has_registry_refs(const Bindings *bindings) {
    if (!bindings)
        return false;
    for (uint32_t i = 0u; i < bindings->len; i++) {
        const Binding *entry = bindings_entry_at(bindings, i);
        if (atom_has_registry_refs(entry->name_key) ||
            atom_has_registry_refs(entry->value.skeleton)) {
            return true;
        }
    }
    for (uint32_t i = 0u; i < bindings->eq_len; i++) {
        const BindingConstraint *constraint = &bindings->constraints[i];
        if (atom_has_registry_refs(constraint->lhs.skeleton) ||
            atom_has_registry_refs(constraint->rhs.skeleton)) {
            return true;
        }
    }
    if (bindings->frame_index) {
        for (uint32_t frame_index = 0u;
             frame_index < bindings->frame_index->len; frame_index++) {
            const BindingsFrameIndexEntry *frame =
                &bindings->frame_index->frames[frame_index];
            for (uint32_t slot = 0u;
                 slot < frame->slot_len; slot++) {
                if (atom_has_registry_refs(
                        bindings_frame_slot_name_key(frame, slot))) {
                    return true;
                }
                if (atom_has_registry_refs(
                        frame->values[slot].skeleton)) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool bindings_promote_logical_atoms_to_arena(Bindings *bindings,
                                             Arena *dst) {
    if (!bindings || !dst)
        return true;
    AtomDeepCopySession *session =
        atom_deep_copy_session_new(dst);
    if (!session)
        return false;
    bool promoted =
        bindings_promote_logical_atoms_with_session(bindings, session);
    atom_deep_copy_session_free(session);
    return promoted;
}

bool bindings_promote_atoms_to_arena(Bindings *bindings, Arena *dst) {
    if (!bindings_promote_logical_atoms_to_arena(bindings, dst))
        return false;
    if (!bindings->prime_ext)
        return true;
    if (!prime_need_snapshot_promote(
            dst, &bindings->prime_ext->prime_need) ||
        !prime_need_branch_state_promote(
            dst, &bindings->prime_ext->branch_state))
        return false;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if (!prime_need_receipt_promote(dst, &bindings->prime_ext->receipt))
        return false;
#endif
    return true;
}

void bindings_move(Bindings *dst, Bindings *src) {
    *dst = *src;
    bindings_init(src);
}

void bindings_replace(Bindings *dst, Bindings *src) {
    bindings_free(dst);
    bindings_move(dst, src);
}

bool bindings_remove_entry_at(Bindings *bindings, uint32_t index) {
    if (!bindings || index >= bindings->len)
        return false;
    if (!bindings_flatten_unique(bindings) ||
        !bindings_reserve_entries(bindings, bindings->len))
        return false;
    if (binding_contains_private_variant_slot(
            &bindings->entries[index])) {
        assert(bindings->private_entry_count > 0u);
        bindings->private_entry_count--;
    }

    for (uint32_t i = index + 1u; i < bindings->len; i++)
        bindings->entries[i - 1u] = bindings->entries[i];
    bindings->len--;
    if (bindings->cycle_state != BINDINGS_CYCLE_ACYCLIC)
        bindings->cycle_state = BINDINGS_CYCLE_UNKNOWN;
    bindings_lookup_index_release(bindings->lookup_index);
    bindings->lookup_index = NULL;
    return true;
}

static bool bindings_rewrite_value_id_inplace(
        Bindings *bindings, VarId id, BindingValue value) {
    if (!bindings || !value.skeleton)
        return false;
    if (!bindings_frame_index_prepare_value_context(
            bindings, NULL, &value, true)) {
        return false;
    }
    if (bindings_frame_index_owns_id(bindings, id)) {
        BindingsFrameIndexEntry *frame = bindings_frame_index_find_frame(
            bindings->frame_index, var_epoch_suffix(id));
        uint32_t slot = 0u;
        if (!frame || !bindings_frame_index_find_slot(
                frame, (VarId)var_base_id(id), &slot) ||
            !frame->values[slot].skeleton ||
            !bindings_frame_index_record(bindings, id, value)) {
            return false;
        }
    } else {
        if (binding_id_has_frame(id))
            return false;
        int32_t found = bindings_lookup_index(bindings, id);
        if (found < 0 || !bindings_prepare_logical_write(bindings))
            return false;
        Binding *entry = &bindings->entries[(uint32_t)found];
        bool previous_private = binding_contains_private_variant_slot(entry);
        entry->value = value;
        bool current_private = binding_contains_private_variant_slot(entry);
        if (current_private && !previous_private)
            bindings->private_entry_count++;
        else if (previous_private && !current_private)
            bindings->private_entry_count--;
    }
    bindings->cycle_state = BINDINGS_CYCLE_UNKNOWN;
    bindings_rhs_variable_bloom_rebuild(bindings);
    return true;
}

bool bindings_rewrite_value_id(
        Bindings *bindings, VarId id, BindingValue value) {
    if (!bindings)
        return false;
    Bindings replacement;
    if (!bindings_clone(&replacement, bindings))
        return false;
    if (!bindings_rewrite_value_id_inplace(
            &replacement, id, value)) {
        bindings_free(&replacement);
        return false;
    }
    bindings_replace(bindings, &replacement);
    return true;
}

bool bindings_invalidate_after_key_rewrite(Bindings *bindings) {
    if (!bindings || !bindings_prepare_logical_write(bindings))
        return false;
    /* External translation can rewrite both a key and its value. Admit the
     * translated value's context before publishing the key. Consume framed
     * rows here, in order; duplicate imported keys leave the newest value in
     * their unique slot. Subsequent clone and rollback preserve this inventory. */
    for (uint32_t row = 0u; row < bindings->len; row++) {
        Binding item = bindings->entries[row];
        if (!bindings_frame_index_prepare_value_context(
                bindings, NULL, &item.value, true))
            return false;
        if (!binding_id_has_frame(item.var_id))
            continue;
        if (!bindings_frame_index_ensure_id(bindings, item.var_id) ||
            !bindings_frame_index_ensure_presentation(bindings, item.var_id,
                item.spelling, item.name_key) ||
            !bindings_frame_index_record(bindings, item.var_id, item.value))
            return false;
    }
    uint32_t kept = 0u;
    for (uint32_t row = 0u; row < bindings->len; row++) {
        if (!binding_id_has_frame(bindings->entries[row].var_id))
            bindings->entries[kept++] = bindings->entries[row];
    }
    bindings->len = kept;
    bindings->cycle_state = !bindings_has_bound_values(bindings)
        ? BINDINGS_CYCLE_ACYCLIC
        : BINDINGS_CYCLE_UNKNOWN;
    bindings_private_counts_slow(
        bindings, &bindings->private_entry_count,
        &bindings->private_constraint_count);
    bindings_rhs_variable_bloom_rebuild(bindings);
    bindings_lookup_index_release(bindings->lookup_index);
    bindings->lookup_index = NULL;
    return true;
}

static bool bindings_frame_index_lookup_value(
        const Bindings *bindings, VarId var_id,
        BindingValue *value_out) {
    if (value_out)
        *value_out = binding_value_from_atom(NULL);
    if (!bindings || !value_out || !binding_id_has_frame(var_id))
        return false;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP);
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_FRAME_COORDINATE);
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_AUTHORITATIVE);
    const BindingsFrameIndexEntry *frame = NULL;
    uint32_t slot = 0u;
    if (!bindings_frame_index_find_coordinate(bindings, var_id, &frame, &slot))
        return true;
    *value_out = frame->values[slot];
    return true;
}

static bool bindings_frame_index_lookup_value_slot(
        const Bindings *bindings, BindingsFrameRef ref, uint32_t slot,
        BindingValue *value_out) {
    if (value_out)
        *value_out = binding_value_from_atom(NULL);
    if (!bindings || !bindings->frame_index || !value_out ||
        !bindings_frame_ref_is_valid(ref)) {
        return false;
    }
    const BindingsFrameIndexEntry *frame =
        bindings_frame_index_find_ref_const(bindings->frame_index, ref);
    if (!frame || slot >= frame->slot_len)
        return false;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP);
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_FRAME_COORDINATE);
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_AUTHORITATIVE);
    *value_out = frame->values[slot];
    return true;
}

/* Apply one contextual environment directly to a source-variable identity.
 * The BindingValue is the closure's environment; converting its variable to
 * an epoch-qualified VarId and searching the epoch directory would merely
 * rediscover this same generation-checked frame. */
static bool bindings_frame_index_lookup_context_id(
        const Bindings *bindings, BindingValue context, VarId id,
        BindingValue *value_out) {
    if (value_out)
        *value_out = binding_value_from_atom(NULL);
    if (!bindings || !bindings->frame_index || !value_out ||
        !binding_value_is_contextual(context) ||
        id == VAR_ID_NONE || var_epoch_suffix(id) != context.epoch) {
        return false;
    }
    BindingsFrameRef ref = binding_value_frame_ref(context);
    if (!bindings_frame_ref_is_valid(ref))
        return false;
    const BindingsFrameIndexEntry *frame =
        bindings_frame_index_find_ref_const(bindings->frame_index, ref);
    if (!frame)
        return false;
    uint32_t slot = 0u;
    if (!bindings_frame_index_find_slot(
            frame, (VarId)var_base_id(id), &slot)) {
        return false;
    }
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP);
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_FRAME_COORDINATE);
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_AUTHORITATIVE);
    *value_out = frame->values[slot];
    return true;
}

static bool bindings_frame_index_lookup_value_ref(
        const Bindings *bindings, BindingValue variable,
        BindingValue *value_out) {
    if (value_out)
        *value_out = binding_value_from_atom(NULL);
    if (!bindings || !value_out || !variable.skeleton ||
        variable.skeleton->kind != ATOM_VAR ||
        !binding_value_is_contextual(variable)) {
        return false;
    }
    BindingsFrameRef ref = binding_value_frame_ref(variable);
    if (!bindings_frame_ref_is_valid(ref))
        return false;
    const BindingsFrameIndexEntry *frame =
        bindings_frame_index_find_ref_const(
            bindings->frame_index, ref);
    if (!frame)
        return false;
    uint32_t slot = 0u;
    if (!bindings_frame_index_find_slot(
            frame, (VarId)var_base_id(variable.skeleton->var_id), &slot)) {
        return false;
    }
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP);
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_FRAME_COORDINATE);
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_AUTHORITATIVE);
    *value_out = frame->values[slot];
    return true;
}

BindingValue bindings_lookup_value_id(Bindings *b, VarId var_id) {
    BindingValue framed;
    if (bindings_frame_index_lookup_value(b, var_id, &framed))
        return framed;
    int32_t idx = bindings_lookup_index(b, var_id);
    return idx >= 0 ? bindings_entry_at(b, (uint32_t)idx)->value
                    : binding_value_from_atom(NULL);
}

/* Internal closure lookup.  Once a contextual value has entered a binding
 * image, its generation-checked frame handle is the authority.  Epoch/name
 * recovery remains only for values crossing an external compatibility
 * boundary without a handle. */
static BindingValue bindings_lookup_value(
        Bindings *bindings, BindingValue variable) {
    BindingValue framed;
    if (bindings_frame_index_lookup_value_ref(
            bindings, variable, &framed)) {
        return framed;
    }
    return bindings_lookup_value_id(
        bindings, binding_value_variable_id(variable));
}

Atom *binding_variable_atom(Arena *a, const Binding *binding) {
    if (!a || !binding)
        return NULL;
    return atom_var_with_presentation(
        a, binding->spelling, binding->name_key, binding->var_id);
}

CettaGsltTermViewStatusV1 bindings_resolve_term_view_root_v1(
        void *context, Atom *source_variable, Atom **target_out) {
    if (target_out)
        *target_out = NULL;
    if (!source_variable || source_variable->kind != ATOM_VAR ||
        !target_out) {
        return CETTA_GSLT_TERM_VIEW_INVALID_V1;
    }
    BindingValue value;
    if (!bindings_resolve_value_preview(context, binding_value_from_atom(source_variable), &value) ||
        (binding_value_is_contextual(value) && atom_has_vars(value.skeleton)))
        return CETTA_GSLT_TERM_VIEW_DEFER_V1;
    *target_out = value.skeleton;
    return CETTA_GSLT_TERM_VIEW_OK_V1;
}




char *arena_tagged_var_name(Arena *a, const char *name, uint32_t suffix) {
    size_t name_len = strlen(name);
    size_t needed = name_len + 1 + 10 + 1;
    char *buf = arena_alloc(a, needed);
    snprintf(buf, needed, "%s#%u", name, suffix);
    return buf;
}

static Atom *epoch_var_atom(Arena *a, Atom *var, uint32_t epoch) {
    return atom_var_like(a, var, var_epoch_id(var->var_id, epoch));
}

/* The write itself, after admission: a framed variable goes to its slot,
 * an unframed one to a new row. */
static bool bindings_add_inplace_write(
        Bindings *b, VarId var_id, SymbolId spelling, Atom *name_key,
        BindingValue val, bool framed) {
    if (framed) {
        if (!bindings_frame_index_record(b, var_id, val))
            return false;
        bindings_rhs_variable_bloom_add(b, val);
        return true;
    }
    if (!bindings_reserve_entries(b, b->len + 1))
        return false;
    Binding *slot = bindings_exclusive_slot(b, b->len);
    slot->var_id = var_id;
    slot->spelling = spelling;
    slot->name_key = name_key;
    slot->value = val;
    bindings_rhs_variable_bloom_add(b, val);


    if (binding_contains_private_variant_slot(slot))
        b->private_entry_count++;
    b->len++;
    return true;
}

static bool bindings_add_inplace_internal(Bindings *b, VarId var_id,
                                          SymbolId spelling, Atom *name_key,
                                          BindingValue val,
                                          bool normalize_constraints) {
    if (binding_id_has_frame(var_id) &&
        !bindings_frame_index_ensure_id(b, var_id))
        return false;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_ADD);
    if (val.skeleton->kind == ATOM_VAR &&
        binding_var_eq(var_id, binding_value_variable_id(val))) {
        return true;
    }
    if (val.skeleton->kind == ATOM_VAR) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_ADD_GUARD);
        BindingValue other = bindings_lookup_value(b, val);
        if (other.skeleton && other.skeleton->kind == ATOM_VAR &&
            binding_var_eq(binding_value_variable_id(other), var_id)) {
            return true;
        }
    }
    bool framed = binding_id_has_frame(var_id);
    int32_t existing_idx = -1;
    BindingValue existing = binding_value_from_atom(NULL);
    if (framed) {
        if (!bindings_frame_index_ensure_presentation(
                b, var_id, spelling, name_key)) {
            return false;
        }
        existing = bindings_lookup_value_id(b, var_id);
    } else if (b->len != 0u) {
        /* An empty substitution proves freshness.  In particular, answer
         * publication starts with an empty external image, so constructing
         * its first row does not need to build or consult a lookup index. */
        existing_idx = bindings_lookup_index(b, var_id);
        existing = existing_idx >= 0
            ? bindings_entry_at(b, (uint32_t)existing_idx)->value
            : binding_value_from_atom(NULL);
    }
    if (existing.skeleton) {
        if (!framed) {
            Atom *existing_key =
                bindings_entry_at(b, (uint32_t)existing_idx)->name_key;
            if ((existing_key || name_key) &&
                (!existing_key || !name_key ||
                 !atom_eq(existing_key, name_key))) {
                return false;
            }
        }
        /* Already bound — unify structurally instead of demanding
           literal equality, so repeated higher-order type constraints
           can refine earlier variable bindings. */
        bool ok = binding_value_equal(existing, val);
        if (!ok) {
            Bindings probe;
            if (!bindings_clone(&probe, b))
                return false;
            ok = match_decoded_atoms_worklist(existing, val, &probe, NULL, false);
            if (!ok) {
                bindings_free(&probe);
                return false;
            }
            bindings_replace(b, &probe);
        }
        if (!ok) {
            return false;
        }

        if (normalize_constraints && !bindings_normalize_constraints(b))
            return false;
        return true;
    }
    if (!bindings_frame_index_prepare_value_context(
            b, NULL, &val, true)) {
        return false;
    }
    BindingsBindVerdict verdict = bindings_bind_verdict(
        b, var_id, val, false,
        BINDINGS_REACHABILITY_UNKNOWN);
    if (verdict == BINDINGS_BIND_REFUSE)
        return false;
    if (verdict == BINDINGS_BIND_AUDIT) {
        Bindings trial;
        if (!bindings_clone(&trial, b))
            return false;
        bool acyclic = bindings_add_inplace_write(
                &trial, var_id, spelling, name_key, val, framed) &&
            bindings_trial_is_acyclic(&trial);
        bindings_free(&trial);
        if (!acyclic)
            return false;
    }
    if (!bindings_add_inplace_write(
            b, var_id, spelling, name_key, val, framed))
        return false;
    if (verdict == BINDINGS_BIND_AUDIT)
        bindings_cycle_memoize_audit(b, BINDINGS_CYCLE_ACYCLIC);
    if (normalize_constraints && !bindings_normalize_constraints(b))
        return false;
    return true;
}

static bool bindings_add_internal(Bindings *b, VarId var_id, SymbolId spelling,
                                  Atom *name_key, BindingValue val,
                                  bool normalize_constraints) {
    Bindings next;
    if (!bindings_clone(&next, b))
        return false;
    if (!bindings_add_inplace_internal(&next, var_id, spelling, name_key, val,
                                       normalize_constraints)) {
        bindings_free(&next);
        return false;
    }
    bindings_replace(b, &next);
    return true;
}

bool bindings_add_id(Bindings *b, VarId var_id, SymbolId spelling, Atom *val) {
    return bindings_add_internal(
        b, var_id, spelling, NULL, binding_value_from_atom(val), true);
}

bool bindings_add_id_acyclic(Bindings *b, VarId var_id, SymbolId spelling,
                             Atom *val) {
    Bindings next;
    if (!bindings_clone(&next, b))
        return false;
    if (!bindings_add_inplace_internal(&next, var_id, spelling, NULL, binding_value_from_atom(val),
                                       true)) {
        bindings_free(&next);
        return false;
    }
    bindings_replace(b, &next);
    return true;
}

bool bindings_add_var(Bindings *b, Atom *var, Atom *val) {
    if (!var || var->kind != ATOM_VAR)
        return false;
    return bindings_add_internal(
        b, var->var_id, var->sym_id, var->name_key, binding_value_from_atom(val), true);
}

bool bindings_add_var_acyclic(Bindings *b, Atom *var, Atom *val) {
    if (!var || var->kind != ATOM_VAR)
        return false;
    Bindings next;
    if (!bindings_clone(&next, b))
        return false;
    if (!bindings_add_inplace_internal(
            &next, var->var_id, var->sym_id, var->name_key,
            binding_value_from_atom(val), true)) {
        bindings_free(&next);
        return false;
    }
    bindings_replace(b, &next);
    return true;
}

static bool bindings_add_constraint_inplace_internal(Bindings *b, BindingValue lhs,
                                                     BindingValue rhs,
                                                     bool normalize_constraints) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_CONSTRAINT_ADD);
    if (!bindings_resolve_value_preview(b, lhs, &lhs) ||
        !bindings_resolve_value_preview(b, rhs, &rhs))
        return false;

    if (binding_values_eq_under_bindings(b, lhs, rhs)) {
        return true;
    }
    if (lhs.skeleton->kind == ATOM_VAR) {
        if (!bindings_add_inplace_internal(
                b, binding_value_variable_id(lhs), lhs.skeleton->sym_id, lhs.skeleton->name_key,
                rhs, false)) {
            return false;
        }
    } else if (rhs.skeleton->kind == ATOM_VAR) {
        if (!bindings_add_inplace_internal(
                b, binding_value_variable_id(rhs), rhs.skeleton->sym_id, rhs.skeleton->name_key,
                lhs, false)) {
            return false;
        }
    } else if (!binding_value_may_contain_unbound_var(b, lhs) &&
               !binding_value_may_contain_unbound_var(b, rhs)) {
        return false;
    } else if (!bindings_store_constraint(b, lhs, rhs)) {
        return false;
    }

    if (normalize_constraints && !bindings_normalize_constraints(b)) {
        return false;
    }
    return true;
}

static bool bindings_add_constraint_internal(Bindings *b, BindingValue lhs, BindingValue rhs,
                                             bool normalize_constraints) {
    Bindings next;
    if (!bindings_clone(&next, b))
        return false;
    if (!bindings_add_constraint_inplace_internal(
            &next, lhs, rhs, normalize_constraints)) {
        bindings_free(&next);
        return false;
    }
    bindings_replace(b, &next);
    return true;
}

bool bindings_add_constraint(Bindings *b, Atom *lhs, Atom *rhs) {
    return bindings_add_constraint_internal(
        b, binding_value_from_atom(lhs), binding_value_from_atom(rhs), true);
}

static bool bindings_merged_occurrence_token(
    const Bindings *dst, const Bindings *src, uint64_t *out) {
    if (!out)
        return false;
    uint64_t left = bindings_occurrence_token(dst);
    uint64_t right = bindings_occurrence_token(src);
    if (left == 0u || left == right) {
        *out = right;
        return true;
    }
    if (right == 0u) {
        *out = left;
        return true;
    }
    *out = prime_need_fresh_source_occurrence();
    return *out != 0u;
}

typedef struct {
    Bindings *destination;
    bool ok;
} BindingsMergeCurrentContext;

static bool bindings_merge_current_binding(
        const Binding *binding, void *raw_context) {
    BindingsMergeCurrentContext *context = raw_context;
    context->ok = bindings_add_inplace_internal(
        context->destination, binding->var_id,
        binding->spelling, binding->name_key,
        binding->value, false);
    return context->ok;
}

static bool bindings_try_merge_inplace(Bindings *dst, const Bindings *src) {
    if (src->owners)
        bindings_inherit_owners(dst, src);
    bindings_assert_no_private_variant_slots(dst);
    bindings_assert_no_private_variant_slots(src);
    /* Only touch Prime state (and materialize dst's occurrence) when either
     * side carries any: HE merges stay allocation-free. */
    if (bindings_prime_present(dst) || bindings_prime_present(src)) {
        uint64_t occurrence_token = 0u;
        if (!bindings_merged_occurrence_token(
                dst, src, &occurrence_token))
            return false;
        if (!prime_need_snapshot_merge(bindings_need_mut(dst),
                                       bindings_need_view(src)))
            return false;
        if (!prime_need_branch_state_merge(
                bindings_branch_state_mut(dst),
                bindings_branch_state_view(src)))
            return false;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
        if (!prime_need_receipt_merge(bindings_receipt_mut(dst),
                                      bindings_receipt_view(src)))
            return false;
#endif
        bindings_prime_ext_materialize(dst)->occurrence_token =
            occurrence_token;
    }
    if (!bindings_frame_index_merge_schemas(dst, src))
        return false;
    uint32_t pending_cap = dst->eq_len + src->eq_len + 1;
    BindingConstraint pending_stack[BINDINGS_TEMP_STACK_CAP];
    BindingConstraint *pending = bindings_temp_constraints_alloc(
        pending_cap, pending_stack, BINDINGS_TEMP_STACK_CAP);
    uint32_t npending = 0;
    for (uint32_t i = 0; i < dst->eq_len; i++)
        pending[npending++] = dst->constraints[i];
    for (uint32_t i = 0; i < src->eq_len; i++) {
        pending[npending++] = src->constraints[i];
    }
    dst->eq_len = 0;
    dst->private_constraint_count = 0u;
    BindingsMergeCurrentContext merge_context = {
        .destination = dst,
        .ok = true,
    };
    if (!bindings_for_each_current_binding(
            src, bindings_merge_current_binding, &merge_context) ||
        !merge_context.ok) {
        bindings_temp_constraints_release(pending, pending_cap,
                                           pending_stack);
        return false;
    }
    for (uint32_t i = 0; i < npending; i++) {
        if (!bindings_add_constraint_inplace_internal(
                dst, pending[i].lhs, pending[i].rhs, false)) {
            bindings_temp_constraints_release(pending, pending_cap,
                                              pending_stack);
            return false;
        }
    }
    bindings_temp_constraints_release(pending, pending_cap, pending_stack);
    if (!bindings_normalize_constraints(dst)) {
        return false;
    }
    return true;
}

bool bindings_try_merge(Bindings *dst, const Bindings *src) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_MERGE);
    Bindings merged;
    if (!bindings_clone(&merged, dst)) return false;
    if (!bindings_try_merge_inplace(&merged, src)) {
        bindings_free(&merged);
        return false;
    }
    bindings_replace(dst, &merged);
    return true;
}

bool bindings_try_merge_live(Bindings *dst, const Bindings *src) {
    if (!src || (!bindings_has_bound_values(src) && src->eq_len == 0 &&
                 !bindings_prime_present(src)))
        return true;
    bindings_assert_no_private_variant_slots(dst);
    bindings_assert_no_private_variant_slots(src);
    if (dst && !bindings_has_bound_values(dst) && dst->eq_len == 0 &&
        !bindings_prime_present(dst)) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_MERGE);
        Bindings cloned;
        if (!bindings_clone(&cloned, src))
            return false;
        bindings_replace(dst, &cloned);
        return true;
    }

    BindingsBuilder builder;
    bindings_builder_init_owned(&builder, dst);
    bool ok = bindings_builder_try_merge(&builder, src);
    bindings_builder_take(&builder, dst);
    return ok;
}

bool bindings_clone_merge(Bindings *dst, const Bindings *base,
                          const Bindings *extra) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_MERGE);
    bindings_init(dst);
    if (!bindings_clone(dst, base))
        return false;
    if (!bindings_try_merge_inplace(dst, extra)) {
        bindings_free(dst);
        return false;
    }
    return true;
}

static bool bindings_seen_var(const VarId *seen, uint32_t len, VarId var_id) {
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_BINDINGS_SEEN_SCAN, len);
    for (uint32_t i = 0; i < len; i++) {
        if (binding_var_eq(seen[i], var_id)) return true;
    }
    return false;
}

typedef struct {
    VarId *ids;
    uint32_t cap;
    bool heap;
} BindingApplySeen;

static inline void bindings_apply_seen_init(
    BindingApplySeen *seen, VarId *ids, uint32_t cap) {
    seen->ids = ids;
    seen->cap = cap;
    seen->heap = false;
}

static void bindings_apply_seen_release(
    BindingApplySeen *seen) {
    if (!seen || !seen->heap)
        return;
    free(seen->ids);
    seen->ids = NULL;
    seen->cap = 0u;
    seen->heap = false;
}

static bool bindings_apply_seen_reserve(
    BindingApplySeen *seen, uint32_t used, uint32_t needed) {
    if (!seen)
        return false;
    if (needed <= seen->cap)
        return true;
    uint32_t next = seen->cap ? seen->cap : 1u;
    while (next < needed) {
        if (next > UINT32_MAX / 2u) {
            next = needed;
            break;
        }
        next *= 2u;
    }
    VarId *grown = cetta_malloc(sizeof(*grown) * (size_t)next);
    if (used > 0u)
        memcpy(grown, seen->ids, sizeof(*grown) * (size_t)used);
    if (seen->heap)
        free(seen->ids);
    seen->ids = grown;
    seen->cap = next;
    seen->heap = true;
    return true;
}

typedef struct {
    VarId id;
    uint32_t index_plus_one;
} BindingApplyMemoSlot;

typedef struct {
    VarId *ids;
    Atom **vals;
    uint32_t len;
    uint32_t cap;
    bool heap;
    BindingApplyMemoSlot *slots;
    size_t slot_cap;
} BindingApplyMemo;

static inline void bindings_apply_memo_init(BindingApplyMemo *memo, VarId *ids,
                                            Atom **vals, uint32_t cap) {
    memo->ids = ids;
    memo->vals = vals;
    memo->len = 0;
    memo->cap = cap;
    memo->heap = false;
    memo->slots = NULL;
    memo->slot_cap = 0u;
}

static void bindings_apply_memo_release(BindingApplyMemo *memo) {
    if (!memo)
        return;
    if (memo->heap) {
        free(memo->ids);
        free(memo->vals);
    }
    free(memo->slots);
    memo->ids = NULL;
    memo->vals = NULL;
    memo->len = 0;
    memo->cap = 0;
    memo->heap = false;
    memo->slots = NULL;
    memo->slot_cap = 0u;
}

static bool bindings_apply_memo_reserve(BindingApplyMemo *memo, uint32_t needed) {
    if (needed <= memo->cap)
        return true;
    uint32_t new_cap = memo->cap ? memo->cap : 1;
    while (new_cap < needed)
        new_cap *= 2;
    VarId *new_ids = cetta_malloc(sizeof(VarId) * new_cap);
    Atom **new_vals = cetta_malloc(sizeof(Atom *) * new_cap);
    if (memo->len > 0) {
        memcpy(new_ids, memo->ids, sizeof(VarId) * memo->len);
        memcpy(new_vals, memo->vals, sizeof(Atom *) * memo->len);
    }
    if (memo->heap) {
        free(memo->ids);
        free(memo->vals);
    }
    memo->ids = new_ids;
    memo->vals = new_vals;
    memo->cap = new_cap;
    memo->heap = true;
    return true;
}

static bool bindings_apply_memo_index_insert(
    BindingApplyMemoSlot *slots, size_t slot_cap,
    VarId id, uint32_t index) {
    if (!slots || slot_cap == 0u || id == VAR_ID_NONE)
        return false;
    size_t mask = slot_cap - 1u;
    size_t slot = bindings_var_id_hash(id) & mask;
    while (slots[slot].id != VAR_ID_NONE &&
           !binding_var_eq(slots[slot].id, id)) {
        slot = (slot + 1u) & mask;
    }
    slots[slot].id = id;
    slots[slot].index_plus_one = index + 1u;
    return true;
}

static bool bindings_apply_memo_index_build(
    BindingApplyMemo *memo, uint32_t needed) {
    if (!memo)
        return false;
    size_t minimum = (size_t)needed * 2u;
    if (minimum < needed)
        return false;
    size_t slot_cap = 32u;
    while (slot_cap < minimum) {
        if (slot_cap > SIZE_MAX / 2u)
            return false;
        slot_cap *= 2u;
    }
    if (slot_cap > SIZE_MAX / sizeof(*memo->slots))
        return false;
    BindingApplyMemoSlot *slots =
        cetta_malloc(slot_cap * sizeof(*slots));
    memset(slots, 0, slot_cap * sizeof(*slots));
    for (uint32_t i = 0u; i < memo->len; i++) {
        if (!bindings_apply_memo_index_insert(
                slots, slot_cap, memo->ids[i], i)) {
            free(slots);
            return false;
        }
    }
    free(memo->slots);
    memo->slots = slots;
    memo->slot_cap = slot_cap;
    return true;
}

static uint32_t bindings_apply_memo_index_find(
    const BindingApplyMemo *memo, VarId id) {
    if (!memo || !memo->slots || memo->slot_cap == 0u ||
        id == VAR_ID_NONE)
        return 0u;
    size_t mask = memo->slot_cap - 1u;
    size_t slot = bindings_var_id_hash(id) & mask;
    while (memo->slots[slot].id != VAR_ID_NONE) {
        if (binding_var_eq(memo->slots[slot].id, id))
            return memo->slots[slot].index_plus_one;
        slot = (slot + 1u) & mask;
    }
    return 0u;
}

static Atom *bindings_apply_memo_lookup(BindingApplyMemo *memo, VarId id) {
    if (memo->slots) {
        uint32_t found = bindings_apply_memo_index_find(memo, id);
        return found > 0u ? memo->vals[found - 1u] : NULL;
    }
    for (uint32_t i = memo->len; i > 0; i--) {
        if (binding_var_eq(memo->ids[i - 1], id))
            return memo->vals[i - 1];
    }
    return NULL;
}

static bool bindings_apply_memo_store(BindingApplyMemo *memo, VarId id, Atom *val) {
    if (memo->slots) {
        uint32_t found = bindings_apply_memo_index_find(memo, id);
        if (found > 0u) {
            memo->vals[found - 1u] = val;
            return true;
        }
    } else {
        for (uint32_t i = 0; i < memo->len; i++) {
            if (binding_var_eq(memo->ids[i], id)) {
                memo->vals[i] = val;
                return true;
            }
        }
    }
    if (!bindings_apply_memo_reserve(memo, memo->len + 1))
        return false;
    if (memo->slots && (size_t)(memo->len + 1u) * 2u > memo->slot_cap &&
        !bindings_apply_memo_index_build(memo, memo->len + 1u))
        return false;
    memo->ids[memo->len] = id;
    memo->vals[memo->len] = val;
    if (memo->slots && !bindings_apply_memo_index_insert(
            memo->slots, memo->slot_cap, id, memo->len))
        return false;
    memo->len++;
    if (!memo->slots && memo->len >= BINDINGS_MEMO_INDEX_THRESHOLD &&
        !bindings_apply_memo_index_build(memo, memo->len))
        return false;
    return true;
}

static Atom *bindings_apply_seen_with_rewrite(Bindings *b, Arena *a, BindingValue value,
                                              BindingApplySeen *seen,
                                              uint32_t seen_len,
                                              bool track_cycles,
                                              BindingApplyMemo *memo,
                                              BindingsRewriteVarFn rewrite_var,
                                              void *rewrite_ctx) {
    Atom *atom = value.skeleton;
    if (!atom_has_vars(atom))
        return atom;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_APPLY_REWRITE_NODE_VISIT);
    switch (atom->kind) {
    case ATOM_VAR: {
        VarId id = binding_value_variable_id(value);
        Atom *memoized = bindings_apply_memo_lookup(memo, id);
        if (memoized) return memoized;
        if (track_cycles &&
            bindings_seen_var(seen->ids, seen_len, id)) {
            Atom *result = binding_value_materialize(a, value);
            if (result && rewrite_var)
                result = rewrite_var(a, result, rewrite_ctx);
            if (result)
                bindings_apply_memo_store(memo, id, result);
            return result;
        }
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_APPLY);
        BindingValue val = bindings_lookup_value(b, value);
        if (!val.skeleton) {
            Atom *result = binding_value_materialize(a, value);
            if (result && rewrite_var)
                result = rewrite_var(a, result, rewrite_ctx);
            if (result)
                bindings_apply_memo_store(memo, id, result);
            return result;
        }
        uint32_t next_seen_len = seen_len;
        if (track_cycles) {
            if (!bindings_apply_seen_reserve(
                    seen, seen_len, seen_len + 1u))
                return NULL;
            seen->ids[seen_len] = id;
            next_seen_len++;
        }
        Atom *result = bindings_apply_seen_with_rewrite(
            b, a, val, seen, next_seen_len, track_cycles,
            memo, rewrite_var, rewrite_ctx);
        if (result)
            bindings_apply_memo_store(memo, id, result);
        return result;
    }
    case ATOM_EXPR: {
        Atom *draft = NULL;
        Atom **new_elems = NULL;
        for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
            Atom *child = atom->expr.elems[i];
            BindingValue child_value = value;
            child_value.skeleton = child;
            Atom *next = atom_has_vars(child)
                ? bindings_apply_seen_with_rewrite(b, a, child_value,
                                                   seen, seen_len,
                                                   track_cycles, memo,
                                                   rewrite_var, rewrite_ctx)
                : child;
            if (!next)
                return NULL;
            if (!new_elems && next != atom->expr.elems[i]) {
                draft = atom_expr_builder_begin(a, atom->expr.len);
                if (!draft)
                    return NULL;
                new_elems = draft->expr.elems;
                for (CettaExprIndex j = 0; j < i; j++)
                    new_elems[j] = atom->expr.elems[j];
            }
            if (new_elems)
                new_elems[i] = next;
        }
        if (!new_elems) return atom;
        return atom_expr_builder_finish(a, draft);
    }
    default:
        return atom;
    }
}

static Atom *bindings_apply_value_rewrite_vars(Bindings *b, Arena *a, BindingValue value,
                                  BindingsRewriteVarFn rewrite_var,
                                  void *rewrite_ctx) {
    Atom *atom = value.skeleton;
    if (!b || !a || !atom)
        return NULL;
    if (!bindings_has_bound_values(b) && !rewrite_var &&
        value.kind == BINDING_VALUE_MATERIALIZED)
        return atom;
    /* Ground-term sharing: a variable-free atom is canonical and immutable --
     * no binding can rewrite it -- so return it shared instead of walking and
     * deep-copying it once per result.  This is the same shared-return contract
     * the b->len==0 case above already relies on (callers must treat the result
     * as read-only); it only widens it to the (bindings present, atom ground)
     * case, e.g. a constant match template applied under a nonempty binding.
     * Only when no custom rewrite hook is installed (a hook may transform
     * non-variable atoms). */
    if (!rewrite_var && !atom_has_vars(atom))
        return atom;
    VarId seen_stack[BINDINGS_SEEN_STACK_CAP];
    VarId memo_id_stack[BINDINGS_MEMO_STACK_CAP];
    Atom *memo_val_stack[BINDINGS_MEMO_STACK_CAP];
    BindingApplySeen seen;
    bindings_apply_seen_init(
        &seen, seen_stack, BINDINGS_SEEN_STACK_CAP);
    BindingApplyMemo memo;
    bindings_apply_memo_init(&memo, memo_id_stack, memo_val_stack,
                             BINDINGS_MEMO_STACK_CAP);
    /* Every binding mutation updates the cached `cycle_state` summary.  In an
     * acyclic graph, active-path membership cannot affect the result, so do
     * not build or scan that transient structure.  Unknown and witnessed-
     * cyclic environments retain the independent general guard below. */
    bool track_cycles =
        b->cycle_state != BINDINGS_CYCLE_ACYCLIC;
    Atom *result = bindings_apply_seen_with_rewrite(
        b, a, value, &seen, 0, track_cycles, &memo,
        rewrite_var, rewrite_ctx);
    bindings_apply_memo_release(&memo);
    bindings_apply_seen_release(&seen);
    return result;
}

Atom *bindings_apply_rewrite_vars(Bindings *b, Arena *a, Atom *atom,
                                  BindingsRewriteVarFn rewrite_var, void *rewrite_ctx) {
    return bindings_apply_value_rewrite_vars(
        b, a, binding_value_from_atom(atom), rewrite_var, rewrite_ctx);
}

Atom *bindings_apply_value(const Bindings *b, Arena *a, BindingValue value) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_APPLY);
    return bindings_apply_value_rewrite_vars((Bindings *)b, a, value, NULL, NULL);
}

Atom *bindings_apply_value_without_id(
        const Bindings *b, Arena *a, VarId skip_id, BindingValue value) {
    if (!value.skeleton || !atom_has_vars(value.skeleton) ||
        !bindings_has_bound_values(b))
        return binding_value_materialize(a, value);
    Bindings reduced;
    if (!bindings_clone(&reduced, b))
        return NULL;
    if (binding_id_has_frame(skip_id)) {
        const BindingsFrameIndexEntry *source_frame = NULL;
        uint32_t slot = 0u;
        if (bindings_frame_index_find_coordinate(
                &reduced, skip_id, &source_frame, &slot)) {
            if (!bindings_frame_index_detach(&reduced)) {
                bindings_free(&reduced);
                return NULL;
            }
            BindingsFrameIndexEntry *frame = bindings_frame_index_find_frame(
                reduced.frame_index, var_epoch_suffix(skip_id));
            if (!frame || !bindings_frame_index_entry_detach(frame) ||
                !bindings_frame_index_replace_slot_value(
                    reduced.frame_index, frame, slot, binding_value_from_atom(NULL))) {
                bindings_free(&reduced);
                return NULL;
            }
            frame->write_version[slot] = 0u;
            reduced.cycle_state = BINDINGS_CYCLE_UNKNOWN;
        }
    }
    /* Raw binding transport permits duplicate keys. Hide the logical key,
     * including older occurrences, rather than revealing a shadowed value. */
    for (uint32_t i = reduced.len; i > 0u;) {
        --i;
        if (bindings_entry_at(&reduced, i)->var_id == skip_id &&
            !bindings_remove_entry_at(&reduced, i)) {
            bindings_free(&reduced);
            return NULL;
        }
    }
    Atom *result = bindings_apply_value(&reduced, a, value);
    bindings_free(&reduced);
    return result;
}

Atom *bindings_apply(Bindings *b, Arena *a, Atom *atom) {
    return bindings_apply_value(b, a, binding_value_from_atom(atom));
}

static size_t bindings_dereference_limit(const Bindings *bindings);

static BindingValue bindings_lookup_value_since(Bindings *b, VarId id,
                                                uint32_t first_entry) {
    const BindingsFrameIndexEntry *frame = NULL;
    uint32_t slot = 0u;
    if (bindings_frame_index_find_coordinate(
            b, id, &frame, &slot)) {
        bool frame_write_present = frame->values[slot].skeleton &&
            (first_entry == 0u ||
             (frame->has_activation_write_boundary &&
              frame->write_version[slot] >
                  frame->activation_write_boundary));
        return frame_write_present
            ? frame->values[slot]
            : binding_value_from_atom(NULL);
    }
    if (binding_id_has_frame(id))
        return binding_value_from_atom(NULL);
    int32_t index = bindings_lookup_index(b, id);
    return index < 0 || (uint32_t)index < first_entry
        ? binding_value_from_atom(NULL)
        : bindings_entry_at(b, (uint32_t)index)->value;
}

static bool bindings_activation_view_find_slot(
        const BindingsActivationView *frame, VarId source_id,
        uint32_t *slot_out) {
    if (!frame || !slot_out)
        return false;
    if (frame->source_ids_contiguous) {
        if (source_id < frame->source_first_id)
            return false;
        uint64_t offset = source_id - frame->source_first_id;
        if (offset >= frame->len)
            return false;
        *slot_out = (uint32_t)offset;
        return true;
    }
    uint32_t low = 0u;
    uint32_t high = frame->len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        if (frame->source_ids[middle] < source_id)
            low = middle + 1u;
        else
            high = middle;
    }
    if (low >= frame->len || frame->source_ids[low] != source_id)
        return false;
    *slot_out = low;
    return true;
}

static bool bindings_activation_read_lookup(
        const BindingsActivationRead *read, VarId source_id,
        BindingValue *value_out, bool *present_out) {
    const BindingsActivationView *frame = read ? read->view : NULL;
    if (value_out)
        *value_out = binding_value_from_atom(NULL);
    if (present_out)
        *present_out = false;
    if (!frame || !value_out || !present_out ||
        !frame->source_ids || !frame->source_variables ||
        source_id == VAR_ID_NONE) {
        return false;
    }
    uint32_t index;
    if (!bindings_activation_view_find_slot(
            frame, source_id, &index))
        return false;
    return bindings_activation_read_slot(
        read, index, value_out, present_out);
}

static bool bindings_activation_view_lookup(
        const Bindings *bindings, const BindingsActivationView *frame, VarId source_id,
        BindingValue *value_out, bool *present_out) {
    BindingsActivationRead read;
    if (!bindings_activation_view_borrow(bindings, frame, &read))
        return false;
    return bindings_activation_read_lookup(&read, source_id, value_out, present_out);
}

CettaGsltTermViewStatusV1 bindings_resolve_term_cursor_v1(
        void *raw_context, CettaGsltTermCursorV1 source,
        CettaGsltTermCursorV1 *target_out) {
    BindingsTermCursorContextV1 *context = raw_context;
    if (target_out)
        *target_out = (CettaGsltTermCursorV1){0};
    if (!context || !context->bindings || !source.source || !target_out)
        return CETTA_GSLT_TERM_VIEW_INVALID_V1;
    const BindingsActivationView *frame = context->frame;
    BindingsActivationRead read;
    if (source.scope &&
        (source.scope != frame ||
         !bindings_activation_view_borrow(context->bindings, frame, &read))) {
        return CETTA_GSLT_TERM_VIEW_DEFER_V1;
    }
    Atom *root = source.source;
    if (source.scope && root->kind == ATOM_VAR) {
        BindingValue value = binding_value_from_atom(NULL);
        bool present = false;
        bool known = bindings_activation_read_lookup(
            &read, root->var_id, &value, &present);
        if (!known) {
            value = bindings_lookup_value_since(
                (Bindings *)context->bindings,
                var_epoch_id(root->var_id, frame->epoch), frame->first_entry);
        } else if (!present) {
            value = binding_value_from_atom(NULL);
        }
        if (!value.skeleton) {
            /* An open authored variable remains in its source namespace.
             * Structural observation needs no allocated renamed variable. */
            *target_out = source;
            return CETTA_GSLT_TERM_VIEW_OK_V1;
        }
        if (binding_value_is_contextual(value) && atom_has_vars(value.skeleton))
            return CETTA_GSLT_TERM_VIEW_DEFER_V1;
        root = value.skeleton;
        source.scope = NULL;
    }
    if (!source.scope) {
        BindingValue value;
        if (!bindings_resolve_value_preview((Bindings *)context->bindings,
                                        binding_value_from_atom(root), &value) ||
            (binding_value_is_contextual(value) && atom_has_vars(value.skeleton)))
            return CETTA_GSLT_TERM_VIEW_DEFER_V1;
        root = value.skeleton;
    }
    *target_out = (CettaGsltTermCursorV1){root, source.scope};
    return CETTA_GSLT_TERM_VIEW_OK_V1;
}

static bool bindings_resolve_ground_value(
        const Bindings *bindings, BindingValue value, Atom **ground_out) {
    size_t dereferences = 0u;
    size_t dereference_limit;

    if (!bindings || !ground_out)
        return false;
    *ground_out = NULL;
    dereference_limit = bindings_dereference_limit(bindings);
    while (value.skeleton && value.skeleton->kind == ATOM_VAR) {
        BindingValue next;

        if (++dereferences > dereference_limit)
            return true;
        next = bindings_lookup_value(
            (Bindings *)bindings, value);
        if (!next.skeleton)
            return true;
        value = next;
    }
    if (value.skeleton && !atom_has_vars(value.skeleton))
        *ground_out = value.skeleton;
    return true;
}

bool bindings_resolve_epoch_view_ground(
        const Bindings *bindings, const Atom *source_variable,
        uint32_t epoch, uint32_t first_entry, Atom **ground_out) {
    BindingValue value;

    if (ground_out)
        *ground_out = NULL;
    if (!bindings || !source_variable || !ground_out ||
        source_variable->kind != ATOM_VAR ||
        first_entry > bindings->len)
        return false;
    value = bindings_lookup_value_since(
        (Bindings *)bindings,
        var_epoch_id(source_variable->var_id, epoch), first_entry);
    return bindings_resolve_ground_value(bindings, value, ground_out);
}

typedef struct {
    BindingsActivationRead activation;
    const BindingsExclusiveFrame *exclusive;
} BindingsEpochAccelerator;

static Atom *bindings_apply_seen_epoch(Bindings *b, Arena *a, Atom *atom,
                                       uint32_t epoch, bool original_side,
                                       uint32_t first_entry,
                                       bool resolve_outer,
                                       const BindingsEpochAccelerator *fast,
                                       BindingApplySeen *seen,
                                       uint32_t seen_len,
                                       bool track_cycles,
                                       BindingApplyMemo *local_memo,
                                       BindingApplyMemo *outer_memo,
                                       uint64_t *node_visits) {
    if (!atom_has_vars(atom))
        return atom;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_APPLY_EPOCH_NODE_VISIT);
    if (node_visits && *node_visits != UINT64_MAX)
        (*node_visits)++;
    switch (atom->kind) {
    case ATOM_VAR: {
        bool outer_lookup = resolve_outer && !original_side;
        uint32_t lookup_first = outer_lookup ? 0u : first_entry;
        /* Stored lexical values use the full substitution.  They must not
         * reuse an unbound result from the query's suffix-limited view. */
        bool full_view = resolve_outer && lookup_first == 0u;
        BindingApplyMemo *memo = full_view ? outer_memo : local_memo;
        VarId lookup_id = original_side
            ? var_epoch_id(atom->var_id, epoch) : atom->var_id;
        Atom *memoized = bindings_apply_memo_lookup(memo, lookup_id);
        if (memoized) return memoized;
        if (track_cycles &&
            bindings_seen_var(seen->ids, seen_len, lookup_id)) {
            Atom *result = original_side ? epoch_var_atom(a, atom, epoch) : atom;
            bindings_apply_memo_store(memo, lookup_id, result);
            return result;
        }
        BindingValue val = binding_value_from_atom(NULL);
        bool exclusive_owned = false;
        if (fast && fast->exclusive) {
            val = bindings_exclusive_frame_lookup(
                fast->exclusive, b, lookup_id, &exclusive_owned);
        }
        const BindingsActivationView *frame = fast ? fast->activation.view : NULL;
        bool dense_present = false;
        bool dense_known = !exclusive_owned && original_side && frame &&
            bindings_activation_read_lookup(
                &fast->activation, atom->var_id, &val, &dense_present);
        if (dense_known && !dense_present)
            val = binding_value_from_atom(NULL);
        if (!exclusive_owned && !dense_known && !val.skeleton)
            val = bindings_lookup_value_since(b, lookup_id, lookup_first);
        if (!val.skeleton) {
            Atom *result = original_side ? epoch_var_atom(a, atom, epoch) : atom;
            bindings_apply_memo_store(memo, lookup_id, result);
            return result;
        }
        if (track_cycles) {
            if (!bindings_apply_seen_reserve(
                    seen, seen_len, seen_len + 1u))
                return NULL;
            seen->ids[seen_len] = lookup_id;
        }
        Atom *result = bindings_apply_seen_epoch(
            b, a, val.skeleton,
            binding_value_is_contextual(val) ? val.epoch : epoch,
            binding_value_is_contextual(val),
            binding_value_is_contextual(val) ? 0u : first_entry,
            binding_value_is_contextual(val) ? true : resolve_outer,
            binding_value_is_contextual(val) &&
                    (!fast || !fast->exclusive)
                ? NULL : fast,
            seen, seen_len + (track_cycles ? 1u : 0u), track_cycles,
            local_memo, outer_memo, node_visits);
        bindings_apply_memo_store(memo, lookup_id, result);
        return result;
    }
    case ATOM_EXPR: {
        Atom *draft = NULL;
        Atom **new_elems = NULL;
        for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
            Atom *child = atom->expr.elems[i];
            Atom *next = atom_has_vars(child)
                ? bindings_apply_seen_epoch(
                      b, a, child, epoch, original_side,
                      first_entry, resolve_outer, fast,
                      seen, seen_len,
                      track_cycles, local_memo, outer_memo, node_visits)
                : child;
            if (!new_elems && next != atom->expr.elems[i]) {
                draft = atom_expr_builder_begin(a, atom->expr.len);
                if (!draft)
                    return NULL;
                new_elems = draft->expr.elems;
                for (uint32_t j = 0; j < i; j++)
                    new_elems[j] = atom->expr.elems[j];
            }
            if (new_elems)
                new_elems[i] = next;
        }
        if (!new_elems) return atom;
        return atom_expr_builder_finish(a, draft);
    }
    default:
        return atom;
    }
}

static Atom *bindings_apply_epoch_from(
        Bindings *b, Arena *a, Atom *atom, uint32_t epoch,
        uint32_t first_entry, bool resolve_outer,
        const BindingsEpochAccelerator *fast,
        bool original_side, VarId initial_seen_id,
        uint64_t *node_visits) {
    if (!b || !a || !atom || first_entry > b->len)
        return NULL;
    /* subst_of_vars_eq_nil: a ground term is the identity under every
     * environment, so observation reuses the existing node. */
    if (!atom_has_vars(atom))
        return atom;
    VarId seen_stack[BINDINGS_SEEN_STACK_CAP];
    VarId local_memo_id_stack[BINDINGS_MEMO_STACK_CAP];
    Atom *local_memo_val_stack[BINDINGS_MEMO_STACK_CAP];
    VarId outer_memo_id_stack[BINDINGS_MEMO_STACK_CAP];
    Atom *outer_memo_val_stack[BINDINGS_MEMO_STACK_CAP];
    BindingApplySeen seen;
    bindings_apply_seen_init(
        &seen, seen_stack, BINDINGS_SEEN_STACK_CAP);
    BindingApplyMemo local_memo;
    BindingApplyMemo outer_memo;
    bindings_apply_memo_init(
        &local_memo, local_memo_id_stack, local_memo_val_stack,
        BINDINGS_MEMO_STACK_CAP);
    bindings_apply_memo_init(
        &outer_memo, outer_memo_id_stack, outer_memo_val_stack,
                             BINDINGS_MEMO_STACK_CAP);
    /* The builder maintains an exact acyclicity summary for the current
     * substitution graph.  Epoch rewriting changes only the input variable
     * identifier used for the first lookup; the same graph contains every
     * traversed binding edge. */
    bool track_cycles =
        b->cycle_state != BINDINGS_CYCLE_ACYCLIC;
    uint32_t initial_seen_len = 0u;
    if (track_cycles && initial_seen_id != VAR_ID_NONE) {
        if (!bindings_apply_seen_reserve(&seen, 0u, 1u)) {
            bindings_apply_memo_release(&outer_memo);
            bindings_apply_memo_release(&local_memo);
            bindings_apply_seen_release(&seen);
            return NULL;
        }
        seen.ids[initial_seen_len++] = initial_seen_id;
    }
    Atom *result = bindings_apply_seen_epoch(
        b, a, atom, epoch, original_side, first_entry, resolve_outer,
        fast,
        &seen, initial_seen_len, track_cycles,
        &local_memo, &outer_memo, node_visits);
    bindings_apply_memo_release(&outer_memo);
    bindings_apply_memo_release(&local_memo);
    bindings_apply_seen_release(&seen);
    return result;
}

static Atom *bindings_apply_epoch_view(Bindings *b, Arena *a, Atom *atom,
                                       uint32_t epoch,
                                       uint32_t first_entry,
                                       bool resolve_outer,
                                       const BindingsActivationView *frame,
                                       uint64_t *node_visits) {
    BindingsEpochAccelerator accelerator = {0};
    if (frame && !bindings_activation_view_borrow(b, frame, &accelerator.activation))
        return NULL;
    return bindings_apply_epoch_from(
        b, a, atom, epoch, first_entry, resolve_outer,
        frame ? &accelerator : NULL,
        true, VAR_ID_NONE, node_visits);
}

Atom *bindings_apply_epoch_since(Bindings *b, Arena *a, Atom *atom,
                                 uint32_t epoch, uint32_t first_entry) {
    return bindings_apply_epoch_view(
        b, a, atom, epoch, first_entry, false,
        NULL, NULL);
}

Atom *bindings_apply_epoch_then_all(Bindings *b, Arena *a, Atom *atom,
                                    uint32_t epoch,
                                    uint32_t first_entry) {
    return bindings_apply_epoch_view(
        b, a, atom, epoch, first_entry, true,
        NULL, NULL);
}

Atom *bindings_apply_activation_view_then_all(
        Bindings *bindings, Arena *a, Atom *atom,
        const BindingsActivationView *frame) {
    if (!bindings || !a || !atom || !frame || frame->epoch == 0u ||
        frame->first_entry > bindings->len)
        return NULL;
    Bindings *b = bindings;
    return bindings_apply_epoch_view(
        b, a, atom, frame->epoch, frame->first_entry, true,
        frame, NULL);
}

Atom *bindings_apply_exclusive_frame_then_all(
        BindingsBuilder *builder, Arena *a, Atom *atom,
        const BindingsExclusiveFrame *frame) {
    if (!builder || !a || !atom || !frame || !frame->active ||
        !frame->schema || frame->epoch == 0u)
        return NULL;
    BindingsEpochAccelerator accelerator = {
        .exclusive = frame,
    };
    return bindings_apply_epoch_from(
        &builder->current, a, atom, frame->epoch,
        builder->current.len, true, &accelerator,
        true, VAR_ID_NONE, NULL);
}

Atom *bindings_apply_activation_view_slot_then_all(
        Bindings *bindings, Arena *a,
        const BindingsActivationView *frame,
        Atom *source_variable, uint32_t slot) {
    if (!bindings || !a || !frame || slot >= frame->len ||
        frame->epoch == 0u ||
        frame->first_entry > bindings->len ||
        !source_variable || source_variable->kind != ATOM_VAR ||
        !frame->source_ids || !frame->source_variables) {
        return NULL;
    }
    BindingsActivationRead read;
    if (!bindings_activation_view_borrow(bindings, frame, &read))
        return NULL;
    Bindings *b = bindings;
    Atom *template_variable = frame->source_variables[slot];
    VarId source_id = frame->source_ids[slot];
    if (source_variable->var_id != source_id ||
        !template_variable || template_variable->kind != ATOM_VAR ||
        template_variable->var_id != source_id)
        return NULL;
    BindingValue value = binding_value_from_atom(NULL);
    bool present = false;
    if (!bindings_activation_read_slot(
            &read, slot, &value, &present))
        return NULL;
    if (!present)
        return epoch_var_atom(a, source_variable, frame->epoch);
    if (!value.skeleton)
        return NULL;
    BindingsEpochAccelerator accelerator = {.activation = read};
    return bindings_apply_epoch_from(
        b, a, value.skeleton,
        binding_value_is_contextual(value) ? value.epoch : frame->epoch,
        binding_value_is_contextual(value) ? 0u : frame->first_entry, true,
        binding_value_is_contextual(value) ? NULL : &accelerator,
        binding_value_is_contextual(value), var_epoch_id(source_id, frame->epoch), NULL);
}

Atom *bindings_resolve_activation_view_slot_root(
        Bindings *bindings, Arena *a,
        const BindingsActivationView *frame,
        Atom *source_variable, uint32_t slot) {
    if (!bindings || !a || !frame || slot >= frame->len ||
        frame->epoch == 0u ||
        frame->first_entry > bindings->len ||
        !source_variable || source_variable->kind != ATOM_VAR ||
        !frame->source_ids || !frame->source_variables) {
        return NULL;
    }
    BindingsActivationRead read;
    if (!bindings_activation_view_borrow(bindings, frame, &read))
        return NULL;
    Bindings *b = bindings;
    Atom *template_variable = frame->source_variables[slot];
    VarId source_id = frame->source_ids[slot];
    if (source_variable->var_id != source_id ||
        !template_variable || template_variable->kind != ATOM_VAR ||
        template_variable->var_id != source_id)
        return NULL;
    BindingValue value = binding_value_from_atom(NULL);
    bool present = false;
    if (!bindings_activation_read_slot(
            &read, slot, &value, &present))
        return NULL;
    if (!present)
        return epoch_var_atom(a, source_variable, frame->epoch);
    BindingValue resolved;
    return bindings_resolve_value_preview(b, value, &resolved)
        ? binding_value_materialize(a, resolved) : NULL;
}

Atom *bindings_apply_epoch(Bindings *b, Arena *a, Atom *atom,
                           uint32_t epoch) {
    return bindings_apply_epoch_since(b, a, atom, epoch, 0u);
}

static void freshen_epoch_memo_clear(FreshenEpochMemoSlot *slots, size_t cap) {
    for (size_t i = 0; i < cap; i++) {
        slots[i].src = NULL;
        slots[i].dst = NULL;
    }
}

static void freshen_epoch_memo_init(FreshenEpochMemo *memo) {
    if (!memo)
        return;
    memo->slots = memo->inline_slots;
    memo->cap = FRESHEN_EPOCH_MEMO_INLINE_CAP;
    memo->used = 0;
    memo->inline_occupied = 0u;
}

static void freshen_epoch_memo_free(FreshenEpochMemo *memo) {
    if (!memo)
        return;
    if (memo->slots != memo->inline_slots)
        free(memo->slots);
    memo->slots = memo->inline_slots;
    memo->cap = FRESHEN_EPOCH_MEMO_INLINE_CAP;
    memo->used = 0;
    memo->inline_occupied = 0u;
}

static inline bool freshen_epoch_memo_slot_occupied(
    const FreshenEpochMemo *memo, size_t pos) {
    if (memo->slots == memo->inline_slots)
        return (memo->inline_occupied & (UINT64_C(1) << pos)) != 0u;
    return memo->slots[pos].src != NULL;
}

static inline void freshen_epoch_memo_mark_occupied(
    FreshenEpochMemo *memo, size_t pos) {
    if (memo->slots == memo->inline_slots)
        memo->inline_occupied |= UINT64_C(1) << pos;
}

static size_t freshen_epoch_memo_hash(const Atom *src) {
    uint64_t x = (uint64_t)(uintptr_t)src;
    x >>= 4;
    x ^= x >> 33;
    x *= UINT64_C(0xff51afd7ed558ccd);
    x ^= x >> 33;
    return (size_t)x;
}

static Atom *freshen_epoch_memo_lookup(FreshenEpochMemo *memo,
                                       const Atom *src) {
    if (!memo || !src || memo->cap == 0)
        return NULL;
    size_t mask = memo->cap - 1u;
    size_t pos = freshen_epoch_memo_hash(src) & mask;
    for (;;) {
        FreshenEpochMemoSlot *slot = &memo->slots[pos];
        if (!freshen_epoch_memo_slot_occupied(memo, pos))
            return NULL;
        if (slot->src == src)
            return slot->dst;
        pos = (pos + 1u) & mask;
    }
}

static bool freshen_epoch_memo_grow(FreshenEpochMemo *memo) {
    if (!memo)
        return false;
    size_t old_cap = memo->cap;
    size_t new_cap = old_cap ? old_cap * 2u : FRESHEN_EPOCH_MEMO_INLINE_CAP;
    FreshenEpochMemoSlot *old_slots = memo->slots;
    bool old_inline = old_slots == memo->inline_slots;
    uint64_t old_inline_occupied = memo->inline_occupied;
    FreshenEpochMemoSlot *new_slots =
        cetta_malloc(sizeof(FreshenEpochMemoSlot) * new_cap);
    freshen_epoch_memo_clear(new_slots, new_cap);
    memo->slots = new_slots;
    memo->cap = new_cap;
    memo->used = 0;
    memo->inline_occupied = 0u;
    for (size_t i = 0; i < old_cap; i++) {
        if (old_inline &&
            (old_inline_occupied & (UINT64_C(1) << i)) == 0u) {
            continue;
        }
        const Atom *src = old_slots[i].src;
        Atom *dst = old_slots[i].dst;
        if (!old_inline && !src)
            continue;
        size_t mask = memo->cap - 1u;
        size_t pos = freshen_epoch_memo_hash(src) & mask;
        while (memo->slots[pos].src)
            pos = (pos + 1u) & mask;
        memo->slots[pos].src = src;
        memo->slots[pos].dst = dst;
        memo->used++;
    }
    if (old_slots != memo->inline_slots)
        free(old_slots);
    return true;
}

static bool freshen_epoch_memo_store(FreshenEpochMemo *memo,
                                     const Atom *src, Atom *dst) {
    if (!memo || !src || !dst)
        return true;
    if ((memo->used + 1u) * 4u >= memo->cap * 3u &&
        !freshen_epoch_memo_grow(memo)) {
        return false;
    }
    size_t mask = memo->cap - 1u;
    size_t pos = freshen_epoch_memo_hash(src) & mask;
    for (;;) {
        FreshenEpochMemoSlot *slot = &memo->slots[pos];
        if (!freshen_epoch_memo_slot_occupied(memo, pos)) {
            slot->src = src;
            slot->dst = dst;
            freshen_epoch_memo_mark_occupied(memo, pos);
            memo->used++;
            return true;
        }
        if (slot->src == src) {
            slot->dst = dst;
            return true;
        }
        pos = (pos + 1u) & mask;
    }
}

static Atom *atom_freshen_epoch_impl(Arena *a, Atom *atom, uint32_t epoch,
                                     FreshenEpochMemo *memo) {
    if (!a || !atom)
        return NULL;
    if (!atom_has_vars(atom))
        return atom;
    Atom *memoized = freshen_epoch_memo_lookup(memo, atom);
    if (memoized)
        return memoized;
    Atom *out = NULL;
    switch (atom->kind) {
    case ATOM_VAR:
        out = epoch_var_atom(a, atom, epoch);
        break;
    case ATOM_EXPR: {
        Atom *draft = NULL;
        Atom **new_elems = NULL;
        for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
            Atom *child = atom->expr.elems[i];
            Atom *next = atom_has_vars(child)
                ? atom_freshen_epoch_impl(a, child, epoch, memo)
                : child;
            if (!next)
                return NULL;
            if (!new_elems && next != atom->expr.elems[i]) {
                draft = atom_expr_builder_begin(a, atom->expr.len);
                if (!draft)
                    return NULL;
                new_elems = draft->expr.elems;
                for (CettaExprIndex j = 0; j < i; j++)
                    new_elems[j] = atom->expr.elems[j];
            }
            if (new_elems)
                new_elems[i] = next;
        }
        out = new_elems ? atom_expr_builder_finish(a, draft) : atom;
        break;
    }
    default:
        out = atom;
        break;
    }
    if (!freshen_epoch_memo_store(memo, atom, out))
        return NULL;
    return out;
}

Atom *atom_freshen_epoch(Arena *a, Atom *atom, uint32_t epoch) {
    FreshenEpochMemo memo;
    freshen_epoch_memo_init(&memo);
    Atom *out = atom_freshen_epoch_impl(a, atom, epoch, &memo);
    freshen_epoch_memo_free(&memo);
    return out;
}

Atom *binding_value_materialize(Arena *a, BindingValue value) {
    if (!value.skeleton)
        return NULL;
    switch (value.kind) {
    case BINDING_VALUE_MATERIALIZED:
        return value.skeleton;
    case BINDING_VALUE_CONTEXTUAL:
    case BINDING_VALUE_CONTEXTUAL_PERSISTENT:
        return a ? atom_freshen_epoch(a, value.skeleton, value.epoch) : NULL;
    }
    return NULL;
}

bool binding_value_equal(BindingValue left, BindingValue right) {
    if (!left.skeleton || !right.skeleton)
        return false;
    if ((left.kind == BINDING_VALUE_MATERIALIZED &&
         right.kind == BINDING_VALUE_MATERIALIZED) ||
        (!atom_has_vars(left.skeleton) && !atom_has_vars(right.skeleton)))
        return atom_eq(left.skeleton, right.skeleton);
    return binding_values_eq_under_bindings(NULL, left, right);
}

typedef struct {
    const BindingsFrameIndexEntry *frame;
    uint32_t slot;
} BindingsObservedFrameSlot;

static int bindings_observed_frame_slot_compare(
        const void *left_raw, const void *right_raw) {
    const BindingsObservedFrameSlot *left = left_raw;
    const BindingsObservedFrameSlot *right = right_raw;
    uint64_t left_version = left->frame->write_version[left->slot];
    uint64_t right_version = right->frame->write_version[right->slot];
    if (left_version != right_version)
        return left_version < right_version ? -1 : 1;
    VarId left_id = var_epoch_id(
        bindings_frame_slot_source_id(left->frame, left->slot), left->frame->epoch);
    VarId right_id = var_epoch_id(
        bindings_frame_slot_source_id(right->frame, right->slot), right->frame->epoch);
    return (left_id > right_id) - (left_id < right_id);
}

Atom *bindings_to_atom(Arena *a, const Bindings *b) {
    Atom **assigns = NULL;
    size_t frame_value_count = bindings_frame_value_count(b);
    size_t assignment_count = 0u;
    if (!bindings_current_binding_count(b, &assignment_count))
        return NULL;
    if (assignment_count > 0u) {
        if (assignment_count > SIZE_MAX / sizeof(*assigns) ||
            assignment_count > UINT32_MAX)
            return NULL;
        assigns = arena_alloc(a, sizeof(*assigns) * assignment_count);
        size_t next_assignment = 0u;
        for (uint32_t i = 0; i < b->len; i++) {
            const Binding *entry = bindings_entry_at(b, i);
            Atom *key = binding_variable_atom(a, entry);
            if (!key)
                return NULL;
            Atom *value = binding_value_materialize(a, entry->value);
            if (!value)
                return NULL;
            assigns[next_assignment++] = atom_expr2(a, key, value);
        }
        if (frame_value_count > 0u) {
            BindingsObservedFrameSlot *observed = arena_alloc(
                a, frame_value_count * sizeof(*observed));
            size_t observed_len = 0u;
            for (uint32_t frame_index = 0u;
                 frame_index < b->frame_index->len; frame_index++) {
                const BindingsFrameIndexEntry *frame =
                    &b->frame_index->frames[frame_index];
                for (uint32_t slot = 0u;
                     slot < frame->slot_len; slot++) {
                    if (!frame->values[slot].skeleton)
                        continue;
                    observed[observed_len++] =
                        (BindingsObservedFrameSlot){frame, slot};
                }
            }
            assert(observed_len == frame_value_count);
            qsort(observed, observed_len, sizeof(*observed),
                  bindings_observed_frame_slot_compare);
            for (size_t index = 0u; index < observed_len; index++) {
                const BindingsFrameIndexEntry *frame =
                    observed[index].frame;
                uint32_t slot = observed[index].slot;
                VarId id = var_epoch_id(
                    bindings_frame_slot_source_id(frame, slot), frame->epoch);
                Atom *key = atom_var_with_presentation(
                    a, bindings_frame_slot_spelling(frame, slot),
                    bindings_frame_slot_name_key(frame, slot), id);
                Atom *value = binding_value_materialize(
                    a, frame->values[slot]);
                if (!key || !value)
                    return NULL;
                assigns[next_assignment++] = atom_expr2(a, key, value);
            }
        }
        assert(next_assignment == assignment_count);
    }
    Atom **equalities = NULL;
    if (b->eq_len > 0) {
        equalities = arena_alloc(a, sizeof(Atom *) * b->eq_len);
        for (uint32_t i = 0; i < b->eq_len; i++) {
            Atom *lhs = binding_value_materialize(a, b->constraints[i].lhs);
            Atom *rhs = binding_value_materialize(a, b->constraints[i].rhs);
            if (!lhs || !rhs)
                return NULL;
            equalities[i] = atom_expr2(a, lhs, rhs);
        }
    }
    return atom_expr3(a,
        atom_symbol_id(a, g_builtin_syms.bindings),
        atom_expr(a, assigns, (CettaExprLen)assignment_count),
        atom_expr(a, equalities, b->eq_len));
}



void binding_set_init(BindingSet *bs) {
    bs->items = NULL;
    bs->len = 0;
    bs->cap = 0;
}

void binding_set_free(BindingSet *bs) {
    for (CettaIndex i = 0; i < bs->len; i++)
        bindings_free(&bs->items[i]);
    free(bs->items);
    bs->items = NULL;
    bs->len = 0;
    bs->cap = 0;
}

static CettaCount binding_set_capacity_limit(void) {
    size_t limit = SIZE_MAX / sizeof(Bindings);
    if ((uint64_t)limit > CETTA_BINDING_SET_MAX_ROWS)
        return CETTA_BINDING_SET_MAX_ROWS;
    return (CettaCount)limit;
}

static bool binding_set_ensure_one(BindingSet *bs) {
    CettaCount limit;
    CettaCount need;
    CettaCount next;
    if (!bs)
        return false;
    limit = binding_set_capacity_limit();
    if (bs->len >= limit)
        return false;
    if (bs->len < bs->cap)
        return true;
    need = bs->len + 1u;
    next = bs->cap ? bs->cap * 2u : 8u;
    if (next <= bs->cap || next < need)
        next = need;
    if (next > limit)
        next = limit;
    if (next < need)
        return false;
    bs->items = cetta_realloc(bs->items, sizeof(Bindings) * (size_t)next);
    bs->cap = next;
    return true;
}

bool binding_set_push(BindingSet *bs, const Bindings *b) {
    if (!b || !binding_set_ensure_one(bs))
        return false;
    if (!bindings_clone(&bs->items[bs->len], b))
        return false;
    bs->len++;
    return true;
}

bool binding_set_push_move(BindingSet *bs, Bindings *b) {
    if (!b || !binding_set_ensure_one(bs))
        return false;
    bindings_move(&bs->items[bs->len], b);
    bs->len++;
    return true;
}

/* ── BindingsBuilder (branch-local speculative bindings) ───────────────── */

/*
 * INVARIANT: the builder only owns speculative branch-local edits.
 * No trail entry may reference atoms published into an OutcomeSet or any
 * other cross-branch accumulator. Freezing happens only at the
 * speculation -> publication boundary.
 *
 * This first tranche intentionally supports fresh binder growth rather than
 * arbitrary in-place unification updates. Positive example: dependent
 * telescope binders added while exploring one function-argument branch.
 * Negative example: trailing through published result environments.
 */

bool bindings_builder_trail_reserve(BindingsBuilder *bb, uint32_t needed) {
    if (needed <= bb->trail_cap)
        return true;
    uint32_t next_cap = bb->trail_cap;
    if (next_cap == 0u)
        next_cap = 8u;
    else if (next_cap > UINT32_MAX / 2u)
        next_cap = needed;
    else
        next_cap *= 2u;
    while (next_cap < needed) {
        if (next_cap > UINT32_MAX / 2u)
            next_cap = needed;
        else
            next_cap *= 2u;
    }
    if ((size_t)next_cap >
        SIZE_MAX / sizeof(BindingsBuilderTrailEntry)) {
        return false;
    }
    bb->trail = cetta_realloc(bb->trail,
                              sizeof(BindingsBuilderTrailEntry) *
                                  (size_t)next_cap);
    bb->trail_cap = next_cap;
    return true;
}

static bool bindings_builder_frame_undo_reserve(
        BindingsBuilder *bb, uint32_t needed) {
    if (needed <= bb->frame_undo_cap)
        return true;
    uint32_t next_cap = bb->frame_undo_cap ? bb->frame_undo_cap : 8u;
    while (next_cap < needed) {
        if (next_cap > UINT32_MAX / 2u) {
            next_cap = needed;
            break;
        }
        next_cap *= 2u;
    }
    if ((size_t)next_cap > SIZE_MAX / sizeof(*bb->frame_undo))
        return false;
    bb->frame_undo = cetta_realloc(
        bb->frame_undo, (size_t)next_cap * sizeof(*bb->frame_undo));
    bb->frame_undo_cap = next_cap;
    return true;
}

static bool bindings_builder_record_frame_slot_value(
        BindingsBuilder *bb, BindingsFrameRef ref, uint32_t slot,
        VarId id, BindingValue value,
        bool *authority_moved_out) {
    if (authority_moved_out)
        *authority_moved_out = false;
    if (!bb || !bindings_frame_ref_is_valid(ref) ||
        ref.identity != var_epoch_suffix(id) ||
        bb->trail_len == 0u) {
        return false;
    }
    const BindingsFrameIndexEntry *frame =
        bindings_frame_index_find_ref_const(
            bb->current.frame_index, ref);
    if (!frame)
        return false;
    /* Admitting the RHS context may extend a sparse captured inventory.
     * Its physical slot positions can change, but the variable identity
     * cannot. Complete activation inventories keep the supplied coordinate;
     * sparse images translate it at this write boundary before recording undo. */
    VarId source_id = (VarId)var_base_id(id);
    if ((slot >= frame->slot_len ||
         bindings_frame_slot_source_id(frame, slot) != source_id) &&
        !bindings_frame_index_find_slot(frame, source_id, &slot)) {
        return false;
    }
    if (bb->frame_undo_len == UINT32_MAX ||
        !bindings_builder_frame_undo_reserve(
            bb, bb->frame_undo_len + 1u)) {
        return false;
    }
    BindingsFrameUndoEntry undo = {
        .previous_value = frame->values[slot],
        .previous_write_version = frame->write_version[slot],
        .frame_ref = ref,
        .source_id = var_base_id(id),
        .slot = slot,
        .trail_mark = bb->trail_len - 1u,
    };
    bool authority_moved = false;
    if (!bindings_frame_index_write_slot_ref(
            &bb->current, ref, slot, value,
            &undo.written_write_version, &authority_moved)) {
        return false;
    }
    bb->frame_undo[bb->frame_undo_len++] = undo;
    if (authority_moved_out)
        *authority_moved_out = authority_moved;
    return true;
}


bool bindings_builder_prime_trail_reserve(
    BindingsBuilder *bb, uint32_t needed) {
    if (needed <= bb->prime_trail_cap)
        return true;
    uint32_t next_cap = bb->prime_trail_cap;
    if (next_cap == 0u)
        next_cap = 8u;
    else if (next_cap > UINT32_MAX / 2u)
        next_cap = needed;
    else
        next_cap *= 2u;
    while (next_cap < needed) {
        if (next_cap > UINT32_MAX / 2u)
            next_cap = needed;
        else
            next_cap *= 2u;
    }
    if ((size_t)next_cap > SIZE_MAX / sizeof(*bb->prime_trail))
        return false;
    bb->prime_trail = cetta_realloc(
        bb->prime_trail, sizeof(*bb->prime_trail) * (size_t)next_cap);
    bb->prime_trail_cap = next_cap;
    return true;
}

static const PrimeOccurrence *bindings_builder_checkpoint_prime(
    const BindingsBuilder *bb, const BindingsBuilderTrailEntry *entry) {
    if (!entry->prime_state_present)
        return NULL;
    if (entry->prime_state_mark >= bb->prime_trail_len)
        return NULL;
    return &bb->prime_trail[entry->prime_state_mark];
}

static bool bindings_builder_snapshot(BindingsBuilder *bb,
                                      bool *created_out) {
    if (created_out)
        *created_out = false;
    if (bb && bb->unobserved_write_region_active &&
        bb->unobserved_write_region_has_checkpoint &&
        !bb->frame_registration_save_barrier) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_BINDINGS_UNOBSERVED_REGION_ELISION);
        return true;
    }
    if (!bb || bb->trail_len == UINT32_MAX ||
        !bindings_builder_trail_reserve(bb, bb->trail_len + 1u)) {
        return false;
    }
    bool prime_present = bindings_prime_present(&bb->current);
    if (prime_present &&
        (bb->prime_trail_len == UINT32_MAX ||
         !bindings_builder_prime_trail_reserve(
             bb, bb->prime_trail_len + 1u))) {
        return false;
    }
    bb->trail[bb->trail_len++] = (BindingsBuilderTrailEntry){
        .len = bb->current.len,
        .eq_len = bb->current.eq_len,
        .prime_state_mark = bb->prime_trail_len,
        .cycle_state = bb->current.cycle_state,
        .derived_nonzero = bindings_derived_nonzero(&bb->current),
        .prime_state_present = prime_present,
    };
    if (prime_present)
        bb->prime_trail[bb->prime_trail_len++] = *bb->current.prime_ext;
    if (bb->unobserved_write_region_active) {
        bb->unobserved_write_region_has_checkpoint = true;
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_BINDINGS_UNOBSERVED_REGION_CHECKPOINT);
    }
    bb->frame_registration_save_barrier = false;
    if (created_out)
        *created_out = true;
    return true;
}

static void bindings_builder_discard_latest_snapshot(BindingsBuilder *bb) {
    assert(bb && bb->trail_len > 0u);
    const BindingsBuilderTrailEntry *entry =
        &bb->trail[bb->trail_len - 1u];
    assert(entry->prime_state_mark <= bb->prime_trail_len);
    bb->prime_trail_len = entry->prime_state_mark;
    bb->trail_len--;
    bb->frame_registration_save_barrier =
        bb->frame_registration_undo_len > 0u &&
        bb->frame_registration_undo[
            bb->frame_registration_undo_len - 1u].trail_mark ==
            bb->trail_len;
    if (bb->unobserved_write_region_active)
        bb->unobserved_write_region_has_checkpoint = false;
}

/* An activation frame may outlive one logical use of a builder.  Address plus
 * counters is not enough after free/reinit at the same address, so each
 * builder incarnation receives a never-recycled identity.  Exhaustion merely
 * disables revision-bound accelerators; generic binding semantics continue. */
static uint64_t bindings_builder_next_instance_id(void) {
    uint64_t current = atomic_load_explicit(
        &g_bindings_builder_instance_counter, memory_order_relaxed);
    while (current != 0u && current != UINT64_MAX) {
        uint64_t next = current + 1u;
        if (atomic_compare_exchange_weak_explicit(
                &g_bindings_builder_instance_counter, &current, next,
                memory_order_relaxed, memory_order_relaxed)) {
            return current;
        }
    }
    return 0u;
}

/* The undo owns the previous dependency set; current owns the new set. */
static void bindings_builder_inherit_owners(BindingsBuilder *bb, const Bindings *source) {
    if (!source->owners || source->owners == bb->current.owners) return;
    BindingsOwners *previous = bb->current.owners;
    bindings_owners_retain(previous);
    bindings_inherit_owners(&bb->current, source);
    if (bb->current.owners == previous) {
        bindings_owners_release(previous);
        return;
    }
    assert(bb->trail_len > 0u);
    if (bb->owner_undo_len == bb->owner_undo_cap) {
        uint32_t cap = bb->owner_undo_cap ? bb->owner_undo_cap * 2u : 4u;
        if (cap <= bb->owner_undo_len) abort();
        bb->owner_undo = cetta_realloc(bb->owner_undo, (size_t)cap * sizeof(*bb->owner_undo));
        bb->owner_undo_cap = cap;
    }
    bb->owner_undo[bb->owner_undo_len++] = (BindingsOwnerUndo){
        .previous = previous, .trail_mark = bb->trail_len - 1u,
    };
}

static void bindings_builder_discard_owner_history(BindingsBuilder *bb) {
    for (uint32_t i = 0; i < bb->owner_undo_len; i++)
        bindings_owners_release(bb->owner_undo[i].previous);
    bb->owner_undo_len = 0u;
}

bool bindings_builder_init(BindingsBuilder *bb, const Bindings *base) {
    if (!bb)
        return false;
    bindings_init(&bb->current);
    bb->instance_id = bindings_builder_next_instance_id();
    bb->trail = NULL;
    bb->trail_len = 0;
    bb->trail_cap = 0;
    bb->owner_undo = NULL;
    bb->owner_undo_len = bb->owner_undo_cap = 0u;
    bb->frame_undo = NULL;
    bb->frame_undo_len = 0u;
    bb->frame_undo_cap = 0u;
    bb->frame_registration_undo = NULL;
    bb->frame_registration_undo_len = 0u;
    bb->frame_registration_undo_cap = 0u;
    bb->prime_trail = NULL;
    bb->prime_trail_len = 0;
    bb->prime_trail_cap = 0;
    bb->growth_count = 0u;
    bb->rollback_count = 0u;
    bb->unobserved_write_region_active = false;
    bb->unobserved_write_region_has_checkpoint = false;
    bb->unobserved_write_region_entry_mark = 0u;
    bb->frame_registration_save_barrier = false;
    if (!base)
        return true;
    if (!bindings_clone(&bb->current, base)) {
        free(bb->trail);
        bb->trail = NULL;
        bb->trail_len = 0;
        bb->trail_cap = 0;
        bb->owner_undo = NULL;
        bb->owner_undo_len = bb->owner_undo_cap = 0u;
        free(bb->frame_undo);
        bb->frame_undo = NULL;
        bb->frame_undo_len = 0u;
        bb->frame_undo_cap = 0u;
        free(bb->frame_registration_undo);
        bb->frame_registration_undo = NULL;
        bb->frame_registration_undo_len = 0u;
        bb->frame_registration_undo_cap = 0u;
        free(bb->prime_trail);
        bb->prime_trail = NULL;
        bb->prime_trail_len = 0;
        bb->prime_trail_cap = 0;
        bb->instance_id = 0u;
        bindings_free(&bb->current);
        return false;
    }
    return true;
}

void bindings_builder_init_owned(BindingsBuilder *bb, Bindings *owned) {
    bb->current = *owned;
    bb->instance_id = bindings_builder_next_instance_id();
    bb->trail = NULL;
    bb->trail_len = 0;
    bb->trail_cap = 0;
    bb->owner_undo = NULL;
    bb->owner_undo_len = bb->owner_undo_cap = 0u;
    bb->frame_undo = NULL;
    bb->frame_undo_len = 0u;
    bb->frame_undo_cap = 0u;
    bb->frame_registration_undo = NULL;
    bb->frame_registration_undo_len = 0u;
    bb->frame_registration_undo_cap = 0u;
    bb->prime_trail = NULL;
    bb->prime_trail_len = 0;
    bb->prime_trail_cap = 0;
    bb->growth_count = 0u;
    bb->rollback_count = 0u;
    bb->unobserved_write_region_active = false;
    bb->unobserved_write_region_has_checkpoint = false;
    bb->unobserved_write_region_entry_mark = 0u;
    bb->frame_registration_save_barrier = false;
    bindings_init(owned);
}

bool bindings_builder_clone(BindingsBuilder *dst,
                            const BindingsBuilder *src) {
    if (!dst || !src || dst == src ||
        src->trail_len > src->trail_cap ||
        src->frame_undo_len > src->frame_undo_cap ||
        src->frame_registration_undo_len >
            src->frame_registration_undo_cap ||
        src->prime_trail_len > src->prime_trail_cap ||
        (src->trail_len > 0u && !src->trail) ||
        (src->frame_undo_len > 0u && !src->frame_undo) ||
        (src->frame_registration_undo_len > 0u &&
         !src->frame_registration_undo) ||
        (src->prime_trail_len > 0u && !src->prime_trail)) {
        return false;
    }
    for (uint32_t i = 0u; i < src->trail_len; i++) {
        if (src->trail[i].prime_state_mark > src->prime_trail_len) {
            return false;
        }
    }
    if (!bindings_builder_init(dst, NULL))
        return false;
    if (!bindings_fork_version(&dst->current, &src->current)) {
        bindings_builder_free(dst);
        return false;
    }
    if (src->owner_undo_len > 0u) {
        dst->owner_undo = cetta_malloc((size_t)src->owner_undo_len * sizeof(*dst->owner_undo));
        memcpy(dst->owner_undo, src->owner_undo,
               (size_t)src->owner_undo_len * sizeof(*dst->owner_undo));
        dst->owner_undo_len = dst->owner_undo_cap = src->owner_undo_len;
        for (uint32_t i = 0; i < dst->owner_undo_len; i++)
            bindings_owners_retain(dst->owner_undo[i].previous);
    }
    if (src->trail_len > 0u) {
        if ((size_t)src->trail_len >
            SIZE_MAX / sizeof(*dst->trail)) {
            bindings_builder_free(dst);
            return false;
        }
        dst->trail = cetta_malloc(
            (size_t)src->trail_len * sizeof(*dst->trail));
        memcpy(dst->trail, src->trail,
               (size_t)src->trail_len * sizeof(*dst->trail));
        dst->trail_len = src->trail_len;
        dst->trail_cap = src->trail_len;
    }
    if (src->frame_undo_len > 0u) {
        if ((size_t)src->frame_undo_len >
            SIZE_MAX / sizeof(*dst->frame_undo)) {
            bindings_builder_free(dst);
            return false;
        }
        dst->frame_undo = cetta_malloc(
            (size_t)src->frame_undo_len * sizeof(*dst->frame_undo));
        memcpy(dst->frame_undo, src->frame_undo,
               (size_t)src->frame_undo_len * sizeof(*dst->frame_undo));
        dst->frame_undo_len = src->frame_undo_len;
        dst->frame_undo_cap = src->frame_undo_len;
    }
    if (src->frame_registration_undo_len > 0u) {
        if ((size_t)src->frame_registration_undo_len >
            SIZE_MAX / sizeof(*dst->frame_registration_undo)) {
            bindings_builder_free(dst);
            return false;
        }
        dst->frame_registration_undo = cetta_malloc(
            (size_t)src->frame_registration_undo_len *
                sizeof(*dst->frame_registration_undo));
        memcpy(dst->frame_registration_undo,
               src->frame_registration_undo,
               (size_t)src->frame_registration_undo_len *
                   sizeof(*dst->frame_registration_undo));
        dst->frame_registration_undo_len =
            src->frame_registration_undo_len;
        dst->frame_registration_undo_cap =
            src->frame_registration_undo_len;
        for (uint32_t i = 0u;
             i < dst->frame_registration_undo_len; i++) {
            bindings_frame_schema_retain(
                dst->frame_registration_undo[i].previous_schema);
        }
    }
    if (src->prime_trail_len > 0u) {
        if ((size_t)src->prime_trail_len >
            SIZE_MAX / sizeof(*dst->prime_trail)) {
            bindings_builder_free(dst);
            return false;
        }
        dst->prime_trail = cetta_malloc(
            (size_t)src->prime_trail_len *
                sizeof(*dst->prime_trail));
        memcpy(dst->prime_trail, src->prime_trail,
               (size_t)src->prime_trail_len *
                   sizeof(*dst->prime_trail));
        dst->prime_trail_len = src->prime_trail_len;
        dst->prime_trail_cap = src->prime_trail_len;
    }
    dst->growth_count = src->growth_count;
    dst->rollback_count = src->rollback_count;
    /* A clone publishes its current logical state as a new physical owner.
     * It may start a fresh unobserved region, but never inherits the source's
     * open optimization scope. */
    dst->unobserved_write_region_active = false;
    dst->unobserved_write_region_has_checkpoint = false;
    dst->unobserved_write_region_entry_mark = 0u;
    dst->frame_registration_save_barrier =
        src->frame_registration_save_barrier;
    return true;
}

bool bindings_builder_promote_prime_atoms_to_arena(
        BindingsBuilder *bb, Arena *owner) {
    if (!bb || !owner ||
        bb->prime_trail_len > bb->prime_trail_cap ||
        (bb->prime_trail_len > 0u && !bb->prime_trail)) {
        return false;
    }
    if (bb->current.prime_ext &&
        (!prime_need_snapshot_promote(
             owner, &bb->current.prime_ext->prime_need) ||
         !prime_need_branch_state_promote(
             owner, &bb->current.prime_ext->branch_state))) {
        return false;
    }
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if (bb->current.prime_ext &&
        !prime_need_receipt_promote(
            owner, &bb->current.prime_ext->receipt)) {
        return false;
    }
#endif
    for (uint32_t i = 0u; i < bb->prime_trail_len; i++) {
        PrimeOccurrence *occurrence = &bb->prime_trail[i];
        if (!prime_need_snapshot_promote(
                owner, &occurrence->prime_need) ||
            !prime_need_branch_state_promote(
                owner, &occurrence->branch_state)) {
            return false;
        }
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
        if (!prime_need_receipt_promote(
                owner, &occurrence->receipt)) {
            return false;
        }
#endif
    }
    return true;
}

bool bindings_builder_promote_atoms_to_arena(
        BindingsBuilder *bb, Arena *owner) {
    if (!bb || !owner)
        return false;
    AtomDeepCopySession *session = atom_deep_copy_session_new(owner);
    if (!session)
        return false;
    bool promoted = bindings_promote_logical_atoms_with_session(
        &bb->current, session);
    for (uint32_t i = 0u; promoted && i < bb->frame_undo_len; i++) {
        Atom *source = bb->frame_undo[i].previous_value.skeleton;
        Atom *copy = source
            ? atom_deep_copy_session_copy(session, source)
            : NULL;
        if (source && !copy) {
            promoted = false;
            break;
        }
        bb->frame_undo[i].previous_value.skeleton = copy;
    }
    for (uint32_t i = 0u;
         promoted && i < bb->frame_registration_undo_len; i++) {
        BindingsFrameSchema *source_schema =
            bb->frame_registration_undo[i].previous_schema;
        if (!source_schema)
            continue;
        bool has_name_keys = false;
        for (uint32_t slot = 0u; slot < source_schema->len; slot++) {
            has_name_keys = has_name_keys ||
                source_schema->name_keys[slot] != NULL;
        }
        if (!has_name_keys)
            continue;
        /* The registration log may outlive the arena which authored its
         * presentation. Clone the retained immutable schema before moving
         * name keys so captured siblings keep their own ownership. */
        BindingsFrameSchema *copy_schema =
            bindings_frame_schema_clone(source_schema);
        if (!copy_schema) {
            promoted = false;
            break;
        }
        for (uint32_t slot = 0u; slot < copy_schema->len; slot++) {
            Atom *source = copy_schema->name_keys[slot];
            Atom *copy = source
                ? atom_deep_copy_session_copy(session, source)
                : NULL;
            if (source && !copy) {
                bindings_frame_schema_release(copy_schema);
                promoted = false;
                break;
            }
            copy_schema->name_keys[slot] = copy;
        }
        if (!promoted)
            break;
        bindings_frame_schema_release(source_schema);
        bb->frame_registration_undo[i].previous_schema = copy_schema;
    }
    atom_deep_copy_session_free(session);
    return promoted &&
        bindings_builder_promote_prime_atoms_to_arena(bb, owner);
}

void bindings_builder_free(BindingsBuilder *bb) {
    bindings_builder_discard_owner_history(bb);
    free(bb->owner_undo);
    free(bb->trail);
    bb->trail = NULL;
    bb->trail_len = 0;
    bb->trail_cap = 0;
    bb->owner_undo = NULL;
    bb->owner_undo_len = bb->owner_undo_cap = 0u;
    free(bb->frame_undo);
    bb->frame_undo = NULL;
    bb->frame_undo_len = 0u;
    bb->frame_undo_cap = 0u;
    bindings_builder_discard_frame_registration_history(bb, true);
    free(bb->prime_trail);
    bb->prime_trail = NULL;
    bb->prime_trail_len = 0;
    bb->prime_trail_cap = 0;
    bb->growth_count = 0u;
    bb->rollback_count = 0u;
    bb->unobserved_write_region_active = false;
    bb->unobserved_write_region_has_checkpoint = false;
    bb->unobserved_write_region_entry_mark = 0u;
    bb->frame_registration_save_barrier = false;
    bb->instance_id = 0u;
    bindings_free(&bb->current);
}

bool bindings_builder_begin_unobserved_write_region(
        BindingsBuilder *bb) {
    if (!bb || bb->unobserved_write_region_active)
        return false;
    bb->unobserved_write_region_active = true;
    bb->unobserved_write_region_has_checkpoint = false;
    bb->unobserved_write_region_entry_mark = bb->trail_len;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_UNOBSERVED_REGION_ENTER);
    return true;
}

void bindings_builder_end_unobserved_write_region(
        BindingsBuilder *bb, bool publish) {
    if (!bb || !bb->unobserved_write_region_active)
        return;
    uint32_t entry_mark = bb->unobserved_write_region_entry_mark;
    if (!publish)
        bindings_builder_rollback(bb, entry_mark);
    bb->unobserved_write_region_active = false;
    bb->unobserved_write_region_has_checkpoint = false;
    bb->unobserved_write_region_entry_mark = 0u;
}

uint32_t bindings_builder_save(BindingsBuilder *bb) {
    /* Saving closes the current physical checkpoint-coalescing segment even
     * though it leaves the denoted substitution unchanged. */
    if (bb->unobserved_write_region_active) {
        bb->unobserved_write_region_has_checkpoint = false;
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_BINDINGS_UNOBSERVED_REGION_SAVE_BARRIER);
    }
    if (bb->frame_registration_save_barrier) {
        bool created = false;
        bool recorded = bindings_builder_snapshot(bb, &created);
        assert(recorded && created);
        if (!recorded || !created)
            return UINT32_MAX;
    }
    return bb->trail_len;
}

void bindings_builder_rollback(BindingsBuilder *bb, uint32_t mark) {
    uint32_t old_len = bb->current.len;
    uint32_t old_eq_len = bb->current.eq_len;
    uint32_t old_frame_undo_len = bb->frame_undo_len;
    uint32_t old_frame_registration_undo_len =
        bb->frame_registration_undo_len;
    uint8_t restored_derived_nonzero = 0u;
    bool restored = false;
    while (bb->trail_len > mark) {
        BindingsBuilderTrailEntry *entry = &bb->trail[--bb->trail_len];
        const PrimeOccurrence *prime =
            bindings_builder_checkpoint_prime(bb, entry);
        bb->current.len = entry->len;
        bb->current.eq_len = entry->eq_len;
        bb->current.cycle_state = entry->cycle_state;
        restored_derived_nonzero = entry->derived_nonzero;
        restored = true;
        bindings_prime_set(
            &bb->current,
            prime ? &prime->prime_need : NULL,
            prime ? &prime->branch_state : NULL,
            prime ? prime->occurrence_token : 0u,
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
            prime ? &prime->receipt : NULL
#else
            NULL
#endif
            );
        bb->prime_trail_len = entry->prime_state_mark;
    }
    bool frame_values_restored =
        bindings_builder_restore_frame_values(bb, mark);
    bool frame_registrations_restored = frame_values_restored &&
        bindings_builder_restore_frame_registrations(bb, mark);
    if (!frame_registrations_restored) {
        fputs("fatal: binding rollback could not restore its authoritative slots\n", stderr);
        abort();
    }
    while (bb->owner_undo_len > 0u &&
           bb->owner_undo[bb->owner_undo_len - 1u].trail_mark >= mark) {
        BindingsOwnerUndo undo = bb->owner_undo[--bb->owner_undo_len];
        bindings_owners_release(bb->current.owners);
        bb->current.owners = undo.previous;
    }
    if (restored) {
        bindings_restore_derived_counts(
            &bb->current, restored_derived_nonzero);
        if (bb->rollback_count != UINT64_MAX)
            bb->rollback_count++;
    }
    bool restored_frame_write = bb->frame_undo_len < old_frame_undo_len;
    bool restored_frame_registration =
        bb->frame_registration_undo_len <
            old_frame_registration_undo_len;
    if (bb->current.len < old_len || bb->current.eq_len < old_eq_len ||
        restored_frame_write || restored_frame_registration || restored) {
        if (bb->current.len < old_len)
            bindings_lookup_index_truncate(&bb->current, bb->current.len);
        if (bb->current.len < old_len)
            bindings_release_truncated_suffix(&bb->current);
    }
    if (bb->unobserved_write_region_active)
        bb->unobserved_write_region_has_checkpoint = false;
}

void bindings_builder_commit(BindingsBuilder *bb) {
    bindings_builder_discard_owner_history(bb);
    bb->trail_len = 0;
    bb->frame_undo_len = 0u;
    bindings_builder_discard_frame_registration_history(bb, false);
    bb->prime_trail_len = 0;
    bb->unobserved_write_region_has_checkpoint = false;
}

bool bindings_builder_prime_present(const BindingsBuilder *bb) {
    return bb &&
        (bindings_prime_present(&bb->current) || bb->prime_trail_len > 0u);
}

bool bindings_builder_prepare_fresh_entries(
    BindingsBuilder *bb, uint32_t additional_entries) {
    uint32_t entry_capacity;
    uint32_t trail_capacity;
    uint32_t prime_capacity = 0u;

    if (!bb)
        return false;
    if (additional_entries == 0u)
        return true;
    if (additional_entries > UINT32_MAX - bb->current.len ||
        additional_entries > UINT32_MAX - bb->trail_len)
        return false;
    entry_capacity = bb->current.len + additional_entries;
    trail_capacity = bb->trail_len + additional_entries;
    bool prime_present = bindings_prime_present(&bb->current);
    if (prime_present) {
        if (additional_entries > UINT32_MAX - bb->prime_trail_len)
            return false;
        prime_capacity = bb->prime_trail_len + additional_entries;
    }
    return bindings_reserve_entries(&bb->current, entry_capacity) &&
        bindings_builder_trail_reserve(bb, trail_capacity) &&
        bindings_builder_frame_undo_reserve(bb, trail_capacity) &&
        (!prime_present ||
         bindings_builder_prime_trail_reserve(bb, prime_capacity));
}

static bool bindings_builder_add_constraint_internal(BindingsBuilder *bb,
                                                     BindingValue lhs, BindingValue rhs,
                                                     bool normalize_constraints);

static bool bindings_builder_add_id_internal_with_cycle_evidence(
        BindingsBuilder *bb, VarId var_id,
        SymbolId spelling, Atom *name_key, BindingValue val,
        bool has_cycle_evidence,
        BindingsReachability cycle_evidence,
        BindingValue *authoritative_value_out) {
    if (authoritative_value_out)
        *authoritative_value_out = binding_value_from_atom(NULL);
    if (!bb)
        return false;
    if (binding_id_has_frame(var_id) &&
        !bindings_builder_frame_index_ensure_id(bb, var_id))
        return false;
    if (val.skeleton->kind == ATOM_VAR &&
        binding_var_eq(var_id, binding_value_variable_id(val)))
        return true;
    if (val.skeleton->kind == ATOM_VAR) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_ADD_GUARD);
        BindingValue other = bindings_lookup_value(&bb->current, val);
        if (other.skeleton && other.skeleton->kind == ATOM_VAR &&
            binding_var_eq(binding_value_variable_id(other), var_id))
            return true;
    }

    const BindingsFrameIndexEntry *framed_entry = NULL;
    uint32_t framed_slot = 0u;
    bool framed = binding_id_has_frame(var_id) &&
        bindings_frame_index_find_coordinate(
            &bb->current, var_id, &framed_entry, &framed_slot);
    BindingsFrameRef framed_ref = framed
        ? bindings_frame_ref_from_entry(
              bb->current.frame_index, framed_entry)
        : (BindingsFrameRef){0};
    BindingValue existing = binding_value_from_atom(NULL);
    if (framed) {
        if (!bindings_frame_ref_is_valid(framed_ref) ||
            !bindings_frame_index_ensure_presentation_slot(
                &bb->current, framed_ref, framed_slot,
                var_id, spelling, name_key) ||
            !bindings_frame_index_lookup_value_slot(
                &bb->current, framed_ref, framed_slot, &existing)) {
            return false;
        }
    } else {
        int32_t existing_idx = bindings_lookup_index(
            &bb->current, var_id);
        if (existing_idx >= 0) {
            const Binding *existing_entry =
                bindings_entry_at(&bb->current, (uint32_t)existing_idx);
            Atom *existing_key = existing_entry->name_key;
            if ((existing_key || name_key) &&
                (!existing_key || !name_key ||
                 !atom_eq(existing_key, name_key))) {
                return false;
            }
            existing = existing_entry->value;

        }
    }
    if (existing.skeleton) {
        if (binding_value_equal(existing, val)) {
            if (authoritative_value_out) {
                *authoritative_value_out = existing;
            }
            return true;
        }
        uint32_t mark = bb->trail_len;
        if (match_decoded_atoms_worklist(existing, val, NULL, bb, false)) {
            if (authoritative_value_out) {
                *authoritative_value_out = existing;
            }
            return true;
        }
        bindings_builder_rollback(bb, mark);
        return false;
    }

    uint32_t rollback_mark = bb->trail_len;
    bool snapshot_created = false;
    if (!bindings_builder_snapshot(bb, &snapshot_created))
        return false;

    if (!framed &&
        !bindings_reserve_entries(&bb->current, bb->current.len + 1)) {
        if (snapshot_created)
            bindings_builder_discard_latest_snapshot(bb);
        return false;
    }

    if (!bindings_frame_index_prepare_value_context(
            &bb->current, bb, &val, true)) {
        bindings_builder_rollback(bb, rollback_mark);
        return false;
    }
    BindingsBindVerdict verdict = bindings_bind_verdict(
        &bb->current, var_id, val,
        has_cycle_evidence, cycle_evidence);
    if (verdict == BINDINGS_BIND_REFUSE) {
        bindings_builder_rollback(bb, rollback_mark);
        return false;
    }

    if (framed) {
        bool frame_authority_moved = false;
        if (!bindings_builder_record_frame_slot_value(
                bb, framed_ref, framed_slot, var_id, val,
                &frame_authority_moved)) {
            bindings_builder_rollback(bb, rollback_mark);
            return false;
        }
        bindings_rhs_variable_bloom_add(&bb->current, val);
        if (verdict == BINDINGS_BIND_AUDIT &&
            !bindings_trial_is_acyclic(&bb->current)) {
            bindings_builder_rollback(bb, rollback_mark);
            return false;
        }
        if (bb->growth_count != UINT64_MAX)
            bb->growth_count++;
        if (authoritative_value_out)
            *authoritative_value_out = val;
        return true;
    }

    {
        Binding *slot = bindings_exclusive_slot(&bb->current, bb->current.len);
        slot->var_id = var_id;
        slot->spelling = spelling;
        slot->name_key = name_key;
        slot->value = val;
        bindings_rhs_variable_bloom_add(&bb->current, val);


        if (binding_contains_private_variant_slot(slot))
            bb->current.private_entry_count++;
    }
    bb->current.len++;
    if (verdict == BINDINGS_BIND_AUDIT &&
        !bindings_trial_is_acyclic(&bb->current)) {
        bindings_builder_rollback(bb, rollback_mark);
        return false;
    }
    if (bb->growth_count != UINT64_MAX)
        bb->growth_count++;
    if (authoritative_value_out) {
        *authoritative_value_out = val;
    }
    /* The generic row is the authority only for this unframed variable. */
    return true;
}

static bool bindings_builder_add_id_internal(
        BindingsBuilder *bb, VarId var_id,
        SymbolId spelling, Atom *name_key, BindingValue val) {
    return bindings_builder_add_id_internal_with_cycle_evidence(
        bb, var_id, spelling, name_key, val, false,
        BINDINGS_REACHABILITY_UNKNOWN, NULL);
}

bool bindings_builder_add_id_fresh(BindingsBuilder *bb, VarId var_id,
                                   SymbolId spelling, Atom *val) {
    return bindings_builder_add_id_internal(
        bb, var_id, spelling, NULL, binding_value_from_atom(val));
}

bool bindings_builder_add_var_fresh(BindingsBuilder *bb, Atom *var, Atom *val) {
    if (!var || var->kind != ATOM_VAR)
        return false;
    return bindings_builder_add_id_internal(
        bb, var->var_id, var->sym_id, var->name_key, binding_value_from_atom(val));
}

typedef enum {
    MATCH_BIND_CONTEXT_STORED_EQUATION = 0,
    MATCH_BIND_CONTEXT_ACTIVATION_SOURCE,
} MatchBindContextRole;

#if CETTA_BUILD_WITH_RUNTIME_STATS
static CettaRuntimeCounter match_bind_context_store_counter(
        MatchBindContextRole role) {
    return role == MATCH_BIND_CONTEXT_STORED_EQUATION
        ? CETTA_RUNTIME_COUNTER_MATCH_BIND_STORED_EQUATION_CONTEXT_STORE
        : CETTA_RUNTIME_COUNTER_MATCH_BIND_ACTIVATION_SOURCE_CONTEXT_STORE;
}

static CettaRuntimeCounter match_bind_transport_root_counter(
        MatchBindContextRole role) {
    return role == MATCH_BIND_CONTEXT_STORED_EQUATION
        ? CETTA_RUNTIME_COUNTER_MATCH_BIND_STORED_EQUATION_TRANSPORT_ROOT
        : CETTA_RUNTIME_COUNTER_MATCH_BIND_ACTIVATION_SOURCE_TRANSPORT_ROOT;
}

static CettaRuntimeCounter match_bind_transport_bytes_counter(
        MatchBindContextRole role) {
    return role == MATCH_BIND_CONTEXT_STORED_EQUATION
        ? CETTA_RUNTIME_COUNTER_MATCH_BIND_STORED_EQUATION_TRANSPORT_ALLOCATED_BYTES
        : CETTA_RUNTIME_COUNTER_MATCH_BIND_ACTIVATION_SOURCE_TRANSPORT_ALLOCATED_BYTES;
}
#endif

/* A retained contextual source must survive the matcher's caller.  Reuse
 * destination-closed or published syntax; transport foreign syntax without
 * applying substitutions or changing its lexical context. Frame ownership
 * can discharge this boundary when both the schema and syntax owner survive. */
static bool bindings_match_store_value(
        Bindings *bindings, BindingsBuilder *builder, Arena *arena,
        AtomDeepCopySession **transport,
        MatchBindContextRole role, bool contextual_id,
        VarId id, SymbolId spelling, Atom *name_key, BindingValue value,
        BindingsReachability evidence, BindingValue *authoritative_out) {
    if (authoritative_out)
        *authoritative_out = binding_value_from_atom(NULL);
    if (!value.skeleton)
        return false;
    Bindings *target = builder ? &builder->current : bindings;
    if (contextual_id &&
        ((!builder && (!target || !bindings_frame_index_ensure_id(target, id))) ||
         (builder && !bindings_builder_frame_index_ensure_id(builder, id)))) {
        return false;
    }
    #if CETTA_BUILD_WITH_RUNTIME_STATS
    if (binding_value_is_contextual(value)) {
        cetta_runtime_stats_inc(match_bind_context_store_counter(role));
    } else if (bindings_materialized_value_has_epoch_variable(value)) {
        cetta_runtime_stats_inc(
            role == MATCH_BIND_CONTEXT_STORED_EQUATION
                ? CETTA_RUNTIME_COUNTER_MATCH_BIND_STORED_EQUATION_MATERIALIZED_EPOCH_STORE
                : CETTA_RUNTIME_COUNTER_MATCH_BIND_ACTIVATION_SOURCE_MATERIALIZED_EPOCH_STORE);
    }
    #else
    (void)role;
    #endif
    if (value.kind == BINDING_VALUE_CONTEXTUAL &&
        !atom_graph_is_closed_for_arena(arena, value.skeleton)) {
        if (!transport)
            return false;
        if (!*transport)
            *transport = atom_deep_copy_session_new(arena);
        #if CETTA_BUILD_WITH_RUNTIME_STATS
        size_t before = arena_accounted_live_bytes(arena);
        #endif
        value.skeleton = *transport
            ? atom_deep_copy_session_copy(*transport, value.skeleton)
            : NULL;
        if (!value.skeleton)
            return false;
        #if CETTA_BUILD_WITH_RUNTIME_STATS
        size_t after = arena_accounted_live_bytes(arena);
        cetta_runtime_stats_inc(match_bind_transport_root_counter(role));
        cetta_runtime_stats_add(
            match_bind_transport_bytes_counter(role),
            after >= before ? after - before : 0u);
        #endif
    }
    if (name_key && !atom_graph_is_closed_for_arena(arena, name_key)) {
        if (!transport)
            return false;
        if (!*transport)
            *transport = atom_deep_copy_session_new(arena);
        #if CETTA_BUILD_WITH_RUNTIME_STATS
        size_t before = arena_accounted_live_bytes(arena);
        #endif
        name_key = *transport
            ? atom_deep_copy_session_copy(*transport, name_key)
            : NULL;
        if (!name_key)
            return false;
        #if CETTA_BUILD_WITH_RUNTIME_STATS
        size_t after = arena_accounted_live_bytes(arena);
        cetta_runtime_stats_inc(match_bind_transport_root_counter(role));
        cetta_runtime_stats_add(
            match_bind_transport_bytes_counter(role),
            after >= before ? after - before : 0u);
        #endif
    }
    if (builder)
        return bindings_builder_add_id_internal_with_cycle_evidence(
            builder, id, spelling, name_key, value,
            evidence != BINDINGS_REACHABILITY_UNKNOWN, evidence,
            authoritative_out);
    bool added = bindings && bindings_add_internal(
        bindings, id, spelling, name_key, value, true);
    if (added && authoritative_out)
        *authoritative_out = bindings_lookup_value_id(bindings, id);
    return added;
}

typedef struct RuleFrameRegion {
    uint64_t eligible;
    const VarId *variable_ids;
    BindingValue *values;
    uint32_t value_count;
    uint32_t epoch;
    BindingsFrameRef authority_ref;
    bool frame_admitted;
    BindingsExclusiveFrame *exclusive;
} RuleFrameRegion;

static bool rule_frame_region_admit_frame(
        RuleFrameRegion *view, BindingsBuilder *builder,
        Atom *source_var, uint32_t epoch);
static bool rule_frame_region_store_exclusive(
        RuleFrameRegion *view, BindingsBuilder *builder,
        Arena *arena, AtomDeepCopySession **transport,
        MatchBindContextRole role, VarId id, SymbolId spelling,
        Atom *name_key, BindingValue value,
        BindingValue *authoritative_value_out, bool *handled_out);

/* Rule-local variables are logically keyed by their immutable source
 * identity and activation epoch.  Their frame region admits the complete
 * compiled variable inventory on the first write.  Subsequent writes in the
 * same attempt therefore never rediscover or extend the frame one variable
 * at a time. */
static bool bindings_builder_add_rule_epoch_key_fresh(
        BindingsBuilder *bb, Atom *source_var, uint32_t epoch,
        BindingValue value, Arena *arena, AtomDeepCopySession **transport,
        RuleFrameRegion *frame_region,
        BindingValue *authoritative_value_out) {
    if (authoritative_value_out)
        *authoritative_value_out = binding_value_from_atom(NULL);
    static _Thread_local int direct_enabled = -1;
    if (!bb || !source_var || source_var->kind != ATOM_VAR ||
        source_var->var_id == VAR_ID_NONE || epoch == 0u || !value.skeleton) {
        return false;
    }
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_RULE_EPOCH_DIRECT_KEY_ATTEMPT);
    if (!rule_frame_region_admit_frame(
            frame_region, bb, source_var, epoch)) {
        return false;
    }
    bool handled = false;
    bool stored = rule_frame_region_store_exclusive(
        frame_region, bb, arena, transport,
        MATCH_BIND_CONTEXT_ACTIVATION_SOURCE,
        var_epoch_id(source_var->var_id, epoch),
        source_var->sym_id, source_var->name_key, value,
        authoritative_value_out, &handled);
    if (handled)
        return stored;
    if (direct_enabled < 0) {
        direct_enabled = getenv(
            "CETTA_BINDINGS_RULE_EPOCH_DIRECT_KEY_REFERENCE") == NULL;
    }
    if (direct_enabled) {
        bool added = bindings_match_store_value(
            NULL, bb, arena, transport,
            MATCH_BIND_CONTEXT_ACTIVATION_SOURCE, true,
            var_epoch_id(source_var->var_id, epoch),
            source_var->sym_id, source_var->name_key, value,
            BINDINGS_REACHABILITY_UNKNOWN, authoritative_value_out);
        if (added) {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_BINDINGS_RULE_EPOCH_DIRECT_KEY_COMMIT);
        }
        return added;
    }
    Atom *materialized = epoch_var_atom(arena, source_var, epoch);
    bool added = materialized &&
        bindings_match_store_value(
            NULL, bb, arena, transport,
            MATCH_BIND_CONTEXT_ACTIVATION_SOURCE, true,
            materialized->var_id, materialized->sym_id,
            materialized->name_key, value, BINDINGS_REACHABILITY_UNKNOWN,
            authoritative_value_out);
    if (added && authoritative_value_out) {
        *authoritative_value_out = bindings_lookup_value_id(
            &bb->current, materialized->var_id);
    }
    return added;
}

static bool bindings_builder_store_constraint(BindingsBuilder *bb,
                                              BindingValue lhs, BindingValue rhs) {
    BindingConstraint next = {.lhs = lhs, .rhs = rhs};
    for (uint32_t i = 0; i < bb->current.eq_len; i++) {
        if (constraint_pair_eq(&bb->current.constraints[i], &next))
            return true;
    }
    uint32_t rollback_mark = bb->trail_len;
    bool snapshot_created = false;
    if (!bindings_builder_snapshot(bb, &snapshot_created))
        return false;
    if (!bindings_reserve_constraints(&bb->current, bb->current.eq_len + 1)) {
        if (snapshot_created)
            bindings_builder_discard_latest_snapshot(bb);
        return false;
    }
    if (!bindings_frame_index_prepare_value_context(
            &bb->current, bb, &lhs, true) ||
        !bindings_frame_index_prepare_value_context(
            &bb->current, bb, &rhs, true)) {
        bindings_builder_rollback(bb, rollback_mark);
        return false;
    }
    next.lhs = lhs;
    next.rhs = rhs;
    bb->current.constraints[bb->current.eq_len++] = next;
    if (bb->growth_count != UINT64_MAX)
        bb->growth_count++;
    if (constraint_contains_private_variant_slot(&next))
        bb->current.private_constraint_count++;
    return true;
}

static bool bindings_builder_add_constraint_internal(BindingsBuilder *bb,
                                                     BindingValue lhs, BindingValue rhs,
                                                     bool normalize_constraints);

static bool bindings_builder_normalize_constraints(BindingsBuilder *bb) {
    if (bb->current.eq_len == 0)
        return true;
    BindingConstraint pending_stack[BINDINGS_TEMP_STACK_CAP];
    BindingConstraint *pending = bindings_temp_constraints_alloc(
        bb->current.eq_len, pending_stack, BINDINGS_TEMP_STACK_CAP);
    uint32_t npending = bb->current.eq_len;
    for (uint32_t i = 0; i < npending; i++)
        pending[i] = bb->current.constraints[i];
    bb->current.eq_len = 0;
    bb->current.private_constraint_count = 0u;
    for (uint32_t i = 0; i < npending; i++) {
        if (!bindings_builder_add_constraint_internal(
                bb, pending[i].lhs, pending[i].rhs, false)) {
            bindings_temp_constraints_release(pending, npending,
                                              pending_stack);
            return false;
        }
    }
    bindings_temp_constraints_release(pending, npending, pending_stack);
    return true;
}

static bool bindings_builder_add_constraint_internal(BindingsBuilder *bb,
                                                     BindingValue lhs, BindingValue rhs,
                                                     bool normalize_constraints) {
    Bindings *current = &bb->current;
    if (!bindings_resolve_value_preview(current, lhs, &lhs) ||
        !bindings_resolve_value_preview(current, rhs, &rhs))
        return false;

    if (binding_values_eq_under_bindings(current, lhs, rhs))
        return true;
    if (lhs.skeleton->kind == ATOM_VAR) {
        if (!bindings_builder_add_id_internal(
                bb, binding_value_variable_id(lhs), lhs.skeleton->sym_id,
                lhs.skeleton->name_key, rhs)) {
            return false;
        }
    } else if (rhs.skeleton->kind == ATOM_VAR) {
        if (!bindings_builder_add_id_internal(
                bb, binding_value_variable_id(rhs), rhs.skeleton->sym_id,
                rhs.skeleton->name_key, lhs)) {
            return false;
        }
    } else if (!binding_value_may_contain_unbound_var(current, lhs) &&
               !binding_value_may_contain_unbound_var(current, rhs)) {
        return false;
    } else if (!bindings_builder_store_constraint(bb, lhs, rhs)) {
        return false;
    }

    if (normalize_constraints && !bindings_builder_normalize_constraints(bb))
        return false;
    return true;
}

typedef struct {
    BindingsBuilder *builder;
    bool ok;
} BindingsBuilderMergeCurrentContext;

static bool bindings_builder_merge_current_binding(
        const Binding *binding, void *raw_context) {
    BindingsBuilderMergeCurrentContext *context = raw_context;
    context->ok = bindings_builder_add_id_internal(
        context->builder, binding->var_id,
        binding->spelling, binding->name_key,
        binding->value);
    return context->ok;
}

bool bindings_builder_try_merge(BindingsBuilder *bb, const Bindings *src) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_MERGE);
    if (!bb || !src)
        return true;
    if (!bindings_has_bound_values(src) && src->eq_len == 0 &&
        !bindings_prime_present(src))
        return true;
    bindings_assert_no_private_variant_slots(&bb->current);
    bindings_assert_no_private_variant_slots(src);

    uint32_t mark = bb->trail_len;
    if (!bindings_builder_snapshot(bb, NULL))
        return false;
    bindings_builder_inherit_owners(bb, src);
    if (!bindings_builder_merge_frame_schemas(bb, src)) {
        bindings_builder_rollback(bb, mark);
        return false;
    }
    if (bindings_prime_present(&bb->current) || bindings_prime_present(src)) {
        uint64_t occurrence_token = 0u;
        if (!bindings_merged_occurrence_token(
                &bb->current, src, &occurrence_token)) {
            bindings_builder_rollback(bb, mark);
            return false;
        }
        if (!prime_need_snapshot_merge(bindings_need_mut(&bb->current),
                                       bindings_need_view(src))) {
            bindings_builder_rollback(bb, mark);
            return false;
        }
        if (!prime_need_branch_state_merge(
                bindings_branch_state_mut(&bb->current),
                bindings_branch_state_view(src))) {
            bindings_builder_rollback(bb, mark);
            return false;
        }
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
        if (!prime_need_receipt_merge(
                bindings_receipt_mut(&bb->current),
                bindings_receipt_view(src))) {
            bindings_builder_rollback(bb, mark);
            return false;
        }
#endif
        bindings_prime_ext_materialize(&bb->current)->occurrence_token =
            occurrence_token;
    }
    uint32_t pending_cap = bb->current.eq_len + src->eq_len + 1;
    BindingConstraint pending_stack[BINDINGS_TEMP_STACK_CAP];
    BindingConstraint *pending = bindings_temp_constraints_alloc(
        pending_cap, pending_stack, BINDINGS_TEMP_STACK_CAP);
    uint32_t npending = 0;
    for (uint32_t i = 0; i < bb->current.eq_len; i++)
        pending[npending++] = bb->current.constraints[i];
    for (uint32_t i = 0; i < src->eq_len; i++)
        pending[npending++] = src->constraints[i];

    bb->current.eq_len = 0;
    bb->current.private_constraint_count = 0u;
    BindingsBuilderMergeCurrentContext merge_context = {
        .builder = bb,
        .ok = true,
    };
    if (!bindings_for_each_current_binding(
            src, bindings_builder_merge_current_binding,
            &merge_context) || !merge_context.ok) {
        bindings_temp_constraints_release(pending, pending_cap,
                                           pending_stack);
        bindings_builder_rollback(bb, mark);
        return false;
    }
    for (uint32_t i = 0; i < npending; i++) {
        if (!bindings_builder_add_constraint_internal(
                bb, pending[i].lhs, pending[i].rhs, false)) {
            bindings_temp_constraints_release(pending, pending_cap,
                                              pending_stack);
            bindings_builder_rollback(bb, mark);
            return false;
        }
    }
    bindings_temp_constraints_release(pending, pending_cap, pending_stack);
    if (!bindings_builder_normalize_constraints(bb)) {
        bindings_builder_rollback(bb, mark);
        return false;
    }
    return true;
}

const Bindings *bindings_builder_bindings(const BindingsBuilder *bb) {
    return &bb->current;
}

void bindings_builder_take(BindingsBuilder *bb, Bindings *out) {
    bindings_builder_discard_owner_history(bb);
    free(bb->owner_undo);
    bindings_move(out, &bb->current);
    free(bb->trail);
    bb->trail = NULL;
    bb->trail_len = 0;
    bb->trail_cap = 0;
    bb->owner_undo = NULL;
    bb->owner_undo_len = bb->owner_undo_cap = 0u;
    free(bb->frame_undo);
    bb->frame_undo = NULL;
    bb->frame_undo_len = 0u;
    bb->frame_undo_cap = 0u;
    bindings_builder_discard_frame_registration_history(bb, true);
    free(bb->prime_trail);
    bb->prime_trail = NULL;
    bb->prime_trail_len = 0;
    bb->prime_trail_cap = 0;
    bb->growth_count = 0u;
    bb->rollback_count = 0u;
    bb->unobserved_write_region_active = false;
    bb->unobserved_write_region_has_checkpoint = false;
    bb->unobserved_write_region_entry_mark = 0u;
    bb->frame_registration_save_barrier = false;
    bb->instance_id = 0u;
}

/* ── Variable renaming (standardization apart) ─────────────────────────── */

typedef struct {
    VarId inline_items[VAR_ID_SET_INLINE_CAP];
    VarId *items;
    uint32_t len;
    uint32_t cap;
    VarId *slots;
    size_t slot_cap;
} VarIdSet;

typedef struct {
    VarId id;
    Atom *mapped;
} RenameVarEntry;

typedef struct {
    RenameVarEntry *items;
    uint32_t len;
    uint32_t cap;
} RenameVarMap;

static void var_id_set_init(VarIdSet *set) {
    set->items = set->inline_items;
    set->len = 0;
    set->cap = VAR_ID_SET_INLINE_CAP;
    set->slots = NULL;
    set->slot_cap = 0u;
}

static void var_id_set_free(VarIdSet *set) {
    if (set->items != set->inline_items)
        free(set->items);
    free(set->slots);
    set->items = set->inline_items;
    set->len = 0;
    set->cap = VAR_ID_SET_INLINE_CAP;
    set->slots = NULL;
    set->slot_cap = 0u;
}

static bool var_id_set_hash_insert(
        VarId *slots, size_t slot_cap, VarId id) {
    if (!slots || slot_cap == 0u ||
        (slot_cap & (slot_cap - 1u)) != 0u ||
        id == VAR_ID_NONE) {
        return false;
    }
    size_t slot = bindings_var_id_hash(id) & (slot_cap - 1u);
    for (size_t probes = 0u; probes < slot_cap; probes++) {
        if (slots[slot] == VAR_ID_NONE || slots[slot] == id) {
            slots[slot] = id;
            return true;
        }
        slot = (slot + 1u) & (slot_cap - 1u);
    }
    return false;
}

static bool var_id_set_rehash(
        VarIdSet *set, size_t requested_items) {
    if (!set || requested_items > SIZE_MAX / 2u)
        return false;
    size_t required = requested_items * 2u;
    size_t slot_cap = 32u;
    while (slot_cap < required) {
        if (slot_cap > SIZE_MAX / 2u)
            return false;
        slot_cap *= 2u;
    }
    if (slot_cap > SIZE_MAX / sizeof(*set->slots))
        return false;
    VarId *slots = cetta_malloc(slot_cap * sizeof(*slots));
    memset(slots, 0, slot_cap * sizeof(*slots));
    for (uint32_t index = 0u; index < set->len; index++) {
        if (!var_id_set_hash_insert(
                slots, slot_cap, set->items[index])) {
            free(slots);
            return false;
        }
    }
    free(set->slots);
    set->slots = slots;
    set->slot_cap = slot_cap;
    return true;
}

static bool var_id_set_contains(const VarIdSet *set, VarId id) {
    if (!set || id == VAR_ID_NONE)
        return false;
    if (set->slots) {
        size_t slot =
            bindings_var_id_hash(id) & (set->slot_cap - 1u);
        for (size_t probes = 0u;
             probes < set->slot_cap; probes++) {
            if (set->slots[slot] == VAR_ID_NONE)
                return false;
            if (set->slots[slot] == id)
                return true;
            slot = (slot + 1u) & (set->slot_cap - 1u);
        }
        return false;
    }
    for (uint32_t i = 0; i < set->len; i++) {
        if (set->items[i] == id)
            return true;
    }
    return false;
}

static bool var_id_set_add(VarIdSet *set, VarId id) {
    if (!set || id == VAR_ID_NONE)
        return false;
    if (var_id_set_contains(set, id))
        return true;
    if (set->len == UINT32_MAX)
        return false;
    size_t next_len = (size_t)set->len + 1u;
    if (next_len >= VAR_ID_SET_HASH_THRESHOLD &&
        (!set->slots || next_len > set->slot_cap / 2u) &&
        !var_id_set_rehash(set, next_len)) {
        return false;
    }
    if (set->len >= set->cap) {
        uint32_t next_cap = set->cap ? set->cap * 2u : 8u;
        if (next_cap < set->cap ||
            (size_t)next_cap > SIZE_MAX / sizeof(*set->items))
            return false;
        if (set->items == set->inline_items) {
            VarId *items = cetta_malloc(
                sizeof(*items) * (size_t)next_cap);
            memcpy(items, set->inline_items,
                   sizeof(*items) * (size_t)set->len);
            set->items = items;
        } else {
            set->items = cetta_realloc(
                set->items, sizeof(*set->items) * (size_t)next_cap);
        }
        set->cap = next_cap;
    }
    set->items[set->len++] = id;
    if (set->slots &&
        !var_id_set_hash_insert(set->slots, set->slot_cap, id)) {
        set->len--;
        return false;
    }
    return true;
}

typedef enum {
    RENAME_WALK_ENTER,
    RENAME_WALK_LEAVE,
} RenameWalkKind;

typedef struct {
    RenameWalkKind kind;
    Atom *atom;
} RenameWalkTask;

typedef struct {
    RenameWalkTask inline_tasks[64];
    RenameWalkTask *tasks;
    size_t len;
    size_t cap;
} RenameWalkStack;

static void rename_walk_stack_init(RenameWalkStack *stack) {
    stack->tasks = stack->inline_tasks;
    stack->len = 0u;
    stack->cap = sizeof(stack->inline_tasks) / sizeof(stack->inline_tasks[0]);
}

static void rename_walk_stack_free(RenameWalkStack *stack) {
    if (stack->tasks != stack->inline_tasks)
        free(stack->tasks);
}

static bool rename_walk_stack_push(RenameWalkStack *stack,
                                   RenameWalkTask task) {
    if (stack->len == stack->cap) {
        if (stack->cap > SIZE_MAX / 2u ||
            stack->cap * 2u > SIZE_MAX / sizeof(*stack->tasks))
            return false;
        size_t next_cap = stack->cap * 2u;
        if (stack->tasks == stack->inline_tasks) {
            RenameWalkTask *next = cetta_malloc(
                sizeof(*next) * next_cap);
            memcpy(next, stack->inline_tasks,
                   sizeof(*next) * stack->len);
            stack->tasks = next;
        } else {
            stack->tasks = cetta_realloc(
                stack->tasks, sizeof(*stack->tasks) * next_cap);
        }
        stack->cap = next_cap;
    }
    stack->tasks[stack->len++] = task;
    return true;
}

/* These addresses are private traversal states, never runtime atoms. */
static Atom g_rename_walk_active;
static Atom g_rename_walk_complete;

/* Hash-stable atoms are immutable published graphs assembled only from
 * hash-stable children. They cannot acquire a back edge after publication,
 * so a completed-node memo is sufficient: it preserves linear traversal of
 * shared DAGs without the active/complete cycle protocol used below. */
static bool collect_value_var_ids_hash_stable(BindingValue value, VarIdSet *set) {
    Atom *root = value.skeleton;
    RenameWalkStack stack;
    FreshenEpochMemo visited;
    rename_walk_stack_init(&stack);
    freshen_epoch_memo_init(&visited);
    if (!rename_walk_stack_push(
            &stack, (RenameWalkTask){RENAME_WALK_ENTER, root}))
        goto fail;
    while (stack.len > 0u) {
        Atom *atom = stack.tasks[--stack.len].atom;
        if (!atom)
            goto fail;
        if (!atom_has_vars(atom))
            continue;
        /* A singleton support summary gives this subtree's complete ordered
         * set contribution. Published hash-stable graphs are acyclic, so no
         * cycle check is lost by skipping their internal occurrences. */
        VarId single = atom_single_variable_id(atom);
        if (single != VAR_ID_NONE) {
            if (!var_id_set_add(set, binding_value_qualify_id(value, single)))
                goto fail;
            continue;
        }
        if (freshen_epoch_memo_lookup(&visited, atom))
            continue;
        if (!freshen_epoch_memo_store(
                &visited, atom, &g_rename_walk_complete))
            goto fail;
        if (atom->kind == ATOM_VAR) {
            if (!var_id_set_add(set, binding_value_qualify_id(value, atom->var_id)))
                goto fail;
            continue;
        }
        if (atom->kind != ATOM_EXPR)
            continue;
        for (CettaExprIndex i = atom->expr.len; i > 0u; i--) {
            if (!rename_walk_stack_push(
                    &stack,
                    (RenameWalkTask){RENAME_WALK_ENTER,
                                     atom->expr.elems[i - 1u]}))
                goto fail;
        }
    }
    freshen_epoch_memo_free(&visited);
    rename_walk_stack_free(&stack);
    return true;

fail:
    freshen_epoch_memo_free(&visited);
    rename_walk_stack_free(&stack);
    return false;
}

static bool collect_value_var_ids(BindingValue value, VarIdSet *set) {
    Atom *root = value.skeleton;
    if (!root || !set)
        return false;
    if ((root->flags & ATOM_FLAG_HASH_STABLE) != 0u)
        return collect_value_var_ids_hash_stable(value, set);
    RenameWalkStack stack;
    FreshenEpochMemo states;
    rename_walk_stack_init(&stack);
    freshen_epoch_memo_init(&states);
    if (!rename_walk_stack_push(
            &stack, (RenameWalkTask){RENAME_WALK_ENTER, root}))
        goto fail;
    while (stack.len > 0u) {
        RenameWalkTask task = stack.tasks[--stack.len];
        Atom *atom = task.atom;
        if (!atom)
            goto fail;
        if (!atom_has_vars(atom))
            continue;
        if (task.kind == RENAME_WALK_LEAVE) {
            if (!freshen_epoch_memo_store(
                    &states, atom, &g_rename_walk_complete))
                goto fail;
            continue;
        }
        if ((atom->flags & ATOM_FLAG_HASH_STABLE) != 0u) {
            VarId single = atom_single_variable_id(atom);
            if (single != VAR_ID_NONE) {
                if (!var_id_set_add(set, binding_value_qualify_id(value, single)))
                    goto fail;
                continue;
            }
        }
        Atom *state = freshen_epoch_memo_lookup(&states, atom);
        if (state == &g_rename_walk_active)
            goto fail;
        if (state == &g_rename_walk_complete)
            continue;
        if (!freshen_epoch_memo_store(
                &states, atom, &g_rename_walk_active))
            goto fail;
        if (atom->kind == ATOM_VAR) {
            if (!var_id_set_add(set, binding_value_qualify_id(value, atom->var_id)) ||
                !freshen_epoch_memo_store(
                    &states, atom, &g_rename_walk_complete))
                goto fail;
            continue;
        }
        if (atom->kind != ATOM_EXPR) {
            if (!freshen_epoch_memo_store(
                    &states, atom, &g_rename_walk_complete))
                goto fail;
            continue;
        }
        if (!rename_walk_stack_push(
                &stack, (RenameWalkTask){RENAME_WALK_LEAVE, atom}))
            goto fail;
        for (CettaExprIndex i = atom->expr.len; i > 0u; i--)
            if (!rename_walk_stack_push(
                    &stack,
                    (RenameWalkTask){RENAME_WALK_ENTER,
                                     atom->expr.elems[i - 1u]}))
                goto fail;
    }
    freshen_epoch_memo_free(&states);
    rename_walk_stack_free(&stack);
    return true;

fail:
    freshen_epoch_memo_free(&states);
    rename_walk_stack_free(&stack);
    return false;
}

static bool collect_var_ids(Atom *root, VarIdSet *set) {
    return collect_value_var_ids(binding_value_from_atom(root), set);
}

typedef struct {
    VarIdSet ids;
    VarIdSet visited_thunks;
    const PrimeNeedSnapshot *need;
    BindingValue value;
    BindingValue *pending;
    size_t len, cap;
} BindingsSupportCollector;

static bool bindings_support_push(BindingsSupportCollector *collector,
                                  BindingValue value) {
    if (!value.skeleton) return true;
    if (collector->len == collector->cap) {
        size_t cap = collector->cap ? collector->cap * 2u : 16u;
        if (cap < collector->cap || cap > SIZE_MAX / sizeof(*collector->pending))
            return false;
        collector->pending = cetta_realloc(
            collector->pending, cap * sizeof(*collector->pending));
        collector->cap = cap;
    }
    collector->pending[collector->len++] = value;
    return true;
}

/* true stops the traversal on failure. Captured domains remain opaque to
 * substitution, but their identities and suspended origins are dependencies
 * of a closure. Traverse resource payloads with an explicit worklist. */
static bool bindings_support_collect_atom(const Atom *atom, void *raw) {
    BindingsSupportCollector *collector = raw;
    if (atom->kind == ATOM_VAR)
        return !var_id_set_add(&collector->ids,
            binding_value_qualify_id(collector->value, atom->var_id));
    if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_BINDINGS) {
        const CettaBindingsValue *captured = atom->ground.ptr;
        for (size_t i = 0u; i < captured->support_count; i++)
            if (!var_id_set_add(&collector->ids, captured->support[i]))
                return true;
    }
    uint64_t thunk_id = 0u;
    if (collector->need &&
        prime_need_ref_belongs_to(atom, collector->need, &thunk_id)) {
        if (var_id_set_contains(&collector->visited_thunks, thunk_id))
            return false;
        PrimeNeedCellView cell;
        if (!var_id_set_add(&collector->visited_thunks, thunk_id) ||
            !prime_need_snapshot_lookup(collector->need, thunk_id, &cell))
            return true;
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
        if (cell.capture_known) {
            if (cell.capture_var_count && !cell.capture_var_ids)
                return true;
            for (size_t i = 0u; i < cell.capture_var_count; i++)
                if (!var_id_set_add(&collector->ids, cell.capture_var_ids[i]))
                    return true;
            return false;
        }
#endif
        return !cell.origin || !bindings_support_push(
            collector, binding_value_from_atom(cell.origin));
    }
    if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_PRIME_CONTEXT) {
        for (const CettaPrimeContext *frame = atom_prime_context_value(atom);
             frame; frame = frame->parent) {
            BindingValue child = collector->value;
            child.skeleton = frame->key;
            if (!bindings_support_push(collector, child)) return true;
            child.skeleton = frame->value;
            if (!bindings_support_push(collector, child)) return true;
        }
    }
    return false;
}

bool bindings_collect_support(const Bindings *bindings,
                              VarId **ids, size_t *count) {
    if (!bindings || !ids || !count) return false;
    *ids = NULL;
    *count = 0u;
    BindingsSupportCollector collector = {.need = bindings_need_view(bindings)};
    var_id_set_init(&collector.ids);
    var_id_set_init(&collector.visited_thunks);
    bool ok = false;
    BindingsIterator iterator = {.bindings = bindings};
    Binding entry;
    while (bindings_iterator_next(&iterator, &entry)) {
        if (!var_id_set_add(&collector.ids, entry.var_id) ||
            !bindings_support_push(&collector, entry.value))
            goto done;
    }
    for (uint32_t i = 0u; i < bindings->eq_len; i++) {
        if (!bindings_support_push(&collector, bindings->constraints[i].lhs) ||
            !bindings_support_push(&collector, bindings->constraints[i].rhs))
            goto done;
    }
    while (collector.len) {
        collector.value = collector.pending[--collector.len];
        if (atom_tree_any(collector.value.skeleton,
                          bindings_support_collect_atom, &collector))
            goto done;
    }
    if (collector.ids.len) {
        *ids = cetta_malloc(sizeof(**ids) * collector.ids.len);
        memcpy(*ids, collector.ids.items, sizeof(**ids) * collector.ids.len);
    }
    *count = collector.ids.len;
    ok = true;
done:
    free(collector.pending);
    var_id_set_free(&collector.visited_thunks);
    var_id_set_free(&collector.ids);
    return ok;
}

static int var_id_compare(const void *left, const void *right);

static bool binding_value_import_context(
        BindingValue *value, Arena *arena, CettaVarMap *inventory, uint32_t epoch) {
    if (binding_value_is_contextual(*value) || !atom_has_vars(value->skeleton))
        return true;
    Atom *syntax = cetta_import_frame_syntax(arena, value->skeleton, inventory, epoch);
    if (!syntax)
        return false;
    *value = binding_value_from_atom(syntax);
    return true;
}

bool bindings_contextualize_unframed(
        Bindings *bindings, Arena *arena, CettaVarMap *inventory, uint32_t epoch) {
    if (!bindings || !arena || !inventory || epoch == 0u)
        return false;
    Bindings converted;
    bindings_init(&converted);
    if (!bindings_clone(&converted, bindings) ||
        !bindings_prepare_logical_write(&converted)) {
        bindings_free(&converted);
        return false;
    }
    uint32_t original_len = converted.len;
    Binding *pending = original_len
        ? cetta_malloc((size_t)original_len * sizeof(*pending)) : NULL;
    uint32_t pending_len = 0u;
    for (uint32_t index = 0u; index < original_len; index++) {
        Binding entry = *bindings_entry_at(&converted, index);
        if (entry.var_id == VAR_ID_NONE ||
            var_epoch_suffix(entry.var_id) != 0u)
            continue;
        Atom *key = atom_var_with_presentation(
            arena, entry.spelling, entry.name_key, entry.var_id);
        Atom *slot = key ? cetta_compile_frame_syntax(arena, key, inventory) : NULL;
        if (!slot || !binding_value_import_context(
                &entry.value, arena, inventory, epoch))
            goto fail;
        entry.var_id = var_epoch_id(slot->var_id, epoch);
        pending[pending_len++] = entry;
    }
    for (uint32_t index = 0u; index < converted.eq_len; index++) {
        BindingConstraint *constraint = &converted.constraints[index];
        if (!binding_value_import_context(&constraint->lhs, arena, inventory, epoch) ||
            !binding_value_import_context(&constraint->rhs, arena, inventory, epoch))
            goto fail;
    }
    VarId *slots = inventory->len
        ? cetta_malloc((size_t)inventory->len * sizeof(*slots)) : NULL;
    Atom **variables = inventory->len
        ? cetta_malloc((size_t)inventory->len * sizeof(*variables)) : NULL;
    for (uint32_t slot = 0u; slot < inventory->len; slot++) {
        slots[slot] = (VarId)slot + 1u;
        variables[slot] = inventory->items[slot].mapped_var;
    }
    BindingsFrameSchema *schema = bindings_frame_schema_new_presented(
        slots, variables, inventory->len);
    free(slots);
    free(variables);
    bool registered = schema && bindings_frame_index_register_kind(
        &converted, schema->source_ids, schema->len, epoch, false, schema, NULL, false);
    bindings_frame_schema_release(schema);
    if (!registered)
        goto fail;
    for (uint32_t index = 0u; index < pending_len; index++) {
        Binding entry = pending[index];
        if (!bindings_add_inplace_internal(
                &converted, entry.var_id, entry.spelling,
                entry.name_key, entry.value, false))
            goto fail;
    }
    for (uint32_t index = original_len; index > 0u; index--) {
        const Binding *entry = bindings_entry_at(&converted, index - 1u);
        if (entry->var_id == VAR_ID_NONE ||
            var_epoch_suffix(entry->var_id) != 0u)
            continue;
        if (!bindings_remove_entry_at(&converted, index - 1u))
            goto fail;
    }
    /* The cycle memo of `converted` is exact here: every re-added edge went
     * through the audited add path, and removing the unframed originals only
     * deletes edges.  Resetting it to unknown would cost a full audit later. */
    free(pending);
    bindings_replace(bindings, &converted);
    return true;
fail:
    free(pending);
    bindings_free(&converted);
    return false;
}

static int var_id_compare(const void *left, const void *right) {
    VarId a = *(const VarId *)left;
    VarId b = *(const VarId *)right;
    return (a > b) - (a < b);
}

/* An unplanned stored pattern still denotes one contextual activation, not a
 * collection of unrelated keys discovered one occurrence at a time.  Build
 * its exact frame inventory once at the activation boundary.  Compiled
 * patterns provide the same inventory directly through RuleFrameRegion. */
static bool bindings_activation_source_ids(
        Atom *source, VarIdSet *variables) {
    if (!source || !variables)
        return false;
    if (!atom_has_vars(source))
        return true;
    bool collected = collect_var_ids(source, variables);
    if (collected) {
        for (uint32_t index = 0u; index < variables->len; index++) {
            variables->items[index] =
                (VarId)var_base_id(variables->items[index]);
            if (variables->items[index] == VAR_ID_NONE) {
                collected = false;
                break;
            }
        }
    }
    if (collected && variables->len > 1u) {
        qsort(variables->items, variables->len,
              sizeof(*variables->items), var_id_compare);
        uint32_t write = 1u;
        for (uint32_t read = 1u; read < variables->len; read++) {
            if (variables->items[read] != variables->items[write - 1u])
                variables->items[write++] = variables->items[read];
        }
        variables->len = write;
    }
    return collected;
}

bool bindings_register_activation_source_frame(
        Bindings *bindings, Atom *source, uint32_t epoch) {
    if (!bindings || !source || epoch == 0u)
        return false;
    VarIdSet variables;
    var_id_set_init(&variables);
    bool registered = bindings_activation_source_ids(source, &variables) &&
        bindings_frame_index_register(
            bindings, variables.items, variables.len, epoch);
    var_id_set_free(&variables);
    return registered;
}

bool bindings_builder_register_activation_source_frame(
        BindingsBuilder *builder, Atom *source, uint32_t epoch) {
    if (!builder || !source || epoch == 0u)
        return false;
    VarIdSet variables;
    var_id_set_init(&variables);
    bool registered = bindings_activation_source_ids(source, &variables) &&
        bindings_builder_register_frame(
            builder, variables.items, variables.len, epoch);
    var_id_set_free(&variables);
    return registered;
}

/*
 * If the current substitution graph is acyclic, adding x -> value can create
 * a cycle exactly when `value` already reaches x.  Explore only that reachable
 * slice.  This is the incremental occurs-check counterpart of
 * bindings_has_loop's full-graph oracle.
 */
static BindingsReachability bindings_value_reaches_var(
    Bindings *bindings, BindingValue value, VarId target) {
    uint64_t single_depth = 0u;

    if (!bindings || !value.skeleton || target == VAR_ID_NONE)
        return BINDINGS_REACHABILITY_UNKNOWN;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_CYCLE_REACH_QUERY);

    /* The caller maintains an acyclic substitution graph.  Follow both a
     * variable-only spine and expressions whose immutable support summary
     * proves that they contain exactly one distinct variable.  Large unary
     * terms then cost one binding lookup instead of a complete term walk.
     * Multi-variable expressions retain the exact general traversal below. */
    for (;;) {
        VarId single = binding_value_single_variable_id(value);
        if (single == VAR_ID_NONE)
            break;
        single_depth++;
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_BINDINGS_CYCLE_REACH_SINGLE_STEP);
        if (binding_var_eq(single, target)) {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_BINDINGS_CYCLE_REACH_SINGLE_PRESENT);
            cetta_runtime_stats_update_max(
                CETTA_RUNTIME_COUNTER_BINDINGS_CYCLE_REACH_SINGLE_DEPTH_PEAK,
                single_depth);
            return BINDINGS_REACHABILITY_PRESENT;
        }
        BindingValue next = binding_value_from_atom(NULL);
        if (!bindings_frame_index_lookup_context_id(
                bindings, value, single, &next)) {
            next = bindings_lookup_value_id(bindings, single);
        }
        if (!next.skeleton) {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_BINDINGS_CYCLE_REACH_SINGLE_ABSENT);
            cetta_runtime_stats_update_max(
                CETTA_RUNTIME_COUNTER_BINDINGS_CYCLE_REACH_SINGLE_DEPTH_PEAK,
                single_depth);
            return BINDINGS_REACHABILITY_ABSENT;
        }
        if (next.skeleton->kind == ATOM_VAR &&
            binding_var_eq(binding_value_variable_id(next), single)) {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_BINDINGS_CYCLE_REACH_SINGLE_ABSENT);
            cetta_runtime_stats_update_max(
                CETTA_RUNTIME_COUNTER_BINDINGS_CYCLE_REACH_SINGLE_DEPTH_PEAK,
                single_depth);
            return BINDINGS_REACHABILITY_ABSENT;
        }
        value = next;
    }
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_BINDINGS_CYCLE_REACH_SINGLE_DEPTH_PEAK,
        single_depth);
    if (!atom_has_vars(value.skeleton))
        return BINDINGS_REACHABILITY_ABSENT;

    /* A closed immutable dependency component cannot reach an unbound target.
     * Cache only that target-independent fact, never merely "not this target".
     * The existing index clears derived facts on rollback/rehash and detaches
     * shared branches before writes. Duplicate graphs retain traversal. */
    BindingsLookupIndex *ground_index = NULL;
    if (bindings->cycle_state == BINDINGS_CYCLE_ACYCLIC &&
        bindings->len >= BINDINGS_LOOKUP_INDEX_THRESHOLD) {
        BindingsLookupIndex *candidate = bindings_lookup_index_current(bindings);
        if (candidate && !candidate->has_duplicates &&
            candidate->synced_len == bindings->len &&
            !bindings_frame_index_owns_id(bindings, target) &&
            bindings_lookup_index_find(candidate, target) == 0u &&
            bindings_lookup_index_detach(bindings))
            ground_index = bindings->lookup_index;
    }
    bool closed_immutable = ground_index != NULL;
    uint32_t dependency_prefix = 0u;
    VarIdSet reachable;
    var_id_set_init(&reachable);
    if (!collect_value_var_ids(value, &reachable)) {
        var_id_set_free(&reachable);
        return BINDINGS_REACHABILITY_UNKNOWN;
    }

    for (uint32_t cursor = 0u; cursor < reachable.len; cursor++) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_BINDINGS_CYCLE_REACH_GENERAL_ITEM);
        VarId current = reachable.items[cursor];
        if (binding_var_eq(current, target)) {
            var_id_set_free(&reachable);
            return BINDINGS_REACHABILITY_PRESENT;
        }
        if (ground_index) {
            size_t slot;
            if (bindings_lookup_index_find_slot(ground_index, current, &slot) &&
                bindings_lookup_index_single_cache_kind(&ground_index->slots[slot]) ==
                    BINDINGS_SINGLE_REACH_CACHE_GROUND) {
                const BindingsLookupIndexSlot *cached = &ground_index->slots[slot];
                if (cached->closed_prefix == 0u)
                    closed_immutable = false;
                uint32_t prefix = cached->closed_prefix != 0u
                    ? (uint32_t)cached->closed_prefix : bindings->len;
                if (prefix < cached->index_plus_one)
                    prefix = cached->index_plus_one;
                if (dependency_prefix < prefix)
                    dependency_prefix = prefix;
                continue;
            }
        }
        BindingValue next = binding_value_from_atom(NULL);
        bool framed = bindings_frame_index_lookup_value(
            bindings, current, &next);
        int32_t index = -1;
        if (framed) {
            /* Frame slots are current-value authority, but they are not
             * justified by a chronological row prefix. */
            closed_immutable = false;
        } else {
            index = bindings_lookup_index(bindings, current);
            if (index >= 0)
                next = bindings_entry_at(
                    bindings, (uint32_t)index)->value;
        }
        if (!next.skeleton) {
            closed_immutable = false;
            continue;
        }
        if (index >= 0 &&
            dependency_prefix < (uint32_t)index + 1u) {
            dependency_prefix = (uint32_t)index + 1u;
        }
        if (!next.skeleton || (next.skeleton->flags & ATOM_FLAG_HASH_STABLE) == 0u)
            closed_immutable = false;
        if (next.skeleton && next.skeleton->kind == ATOM_VAR &&
            binding_var_eq(binding_value_variable_id(next), current)) {
            closed_immutable = false;
            continue;
        }
        if (next.skeleton && atom_has_vars(next.skeleton) &&
            !collect_value_var_ids(next, &reachable)) {
            var_id_set_free(&reachable);
            return BINDINGS_REACHABILITY_UNKNOWN;
        }
    }

    if (closed_immutable) {
        for (uint32_t cursor = 0u; cursor < reachable.len; cursor++) {
            size_t slot;
            if (!bindings_lookup_index_find_slot(ground_index, reachable.items[cursor], &slot) ||
                !bindings_lookup_index_record_single_cached_slot(ground_index, slot))
                break;
            ground_index->slots[slot].single_cache_kind =
                BINDINGS_SINGLE_REACH_CACHE_LISTED | BINDINGS_SINGLE_REACH_CACHE_GROUND;
            ground_index->slots[slot].closed_prefix = dependency_prefix;
        }
    }
    var_id_set_free(&reachable);
    return BINDINGS_REACHABILITY_ABSENT;
}

static BindingsReachability bindings_exclusive_frame_value_reaches(
        const BindingsExclusiveFrame *frame, Bindings *outer,
        BindingValue value, VarId target);

/* A compiled source subtree denotes exactly the variables selected by its
 * support mask.  Inspect their activation-frame bindings directly instead of
 * first constructing the epoch-substituted term and then rediscovering the
 * same support.  Any unavailable or indeterminate component declines to the
 * ordinary value traversal. */
static BindingsReachability bindings_cycle_source_support_reaches_var(
        Bindings *bindings, Atom *source,
        const VarId *variable_ids, uint64_t variable_mask,
        uint32_t epoch, VarId target,
        const RuleFrameRegion *frame_region) {
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_CYCLE_PLAN_SUPPORT_ATTEMPT);
    if (!bindings_cycle_plan_support_enabled() || !bindings || !source ||
        epoch == 0u || target == VAR_ID_NONE ||
        bindings->cycle_state != BINDINGS_CYCLE_ACYCLIC ||
        (atom_has_vars(source) &&
         (!variable_ids || variable_mask == 0u))) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_BINDINGS_CYCLE_PLAN_SUPPORT_DECLINE);
        return BINDINGS_REACHABILITY_UNKNOWN;
    }
    if (!atom_has_vars(source)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_BINDINGS_CYCLE_PLAN_SUPPORT_ABSENT);
        return BINDINGS_REACHABILITY_ABSENT;
    }

    bool indeterminate = false;
    uint64_t remaining = variable_mask;
    while (remaining != 0u) {
        uint32_t slot = (uint32_t)__builtin_ctzll(remaining);
        remaining &= remaining - 1u;
        VarId source_id = variable_ids[slot];
        if (source_id == VAR_ID_NONE) {
            indeterminate = true;
            continue;
        }
        VarId activated = var_epoch_id(source_id, epoch);
        if (binding_var_eq(activated, target)) {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_BINDINGS_CYCLE_PLAN_SUPPORT_PRESENT);
            return BINDINGS_REACHABILITY_PRESENT;
        }
        BindingValue value = bindings_exclusive_frame_lookup(
            frame_region ? frame_region->exclusive : NULL,
            bindings, activated, NULL);
        if (!value.skeleton)
            continue;
        BindingsReachability reaches =
            frame_region && frame_region->exclusive &&
                    frame_region->exclusive->active
                ? bindings_exclusive_frame_value_reaches(
                      frame_region->exclusive, bindings, value, target)
                : bindings_value_reaches_var(bindings, value, target);
        if (reaches == BINDINGS_REACHABILITY_PRESENT) {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_BINDINGS_CYCLE_PLAN_SUPPORT_PRESENT);
            return BINDINGS_REACHABILITY_PRESENT;
        }
        if (reaches == BINDINGS_REACHABILITY_UNKNOWN)
            indeterminate = true;
    }
    if (indeterminate) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_BINDINGS_CYCLE_PLAN_SUPPORT_DECLINE);
        return BINDINGS_REACHABILITY_UNKNOWN;
    }
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_CYCLE_PLAN_SUPPORT_ABSENT);
    return BINDINGS_REACHABILITY_ABSENT;
}

static bool bindings_exclusive_frame_reserve_writes(
        BindingsExclusiveFrame *frame, uint32_t needed) {
    if (!frame || needed < frame->write_len)
        return false;
    if (needed <= frame->write_cap)
        return true;
    uint32_t cap = frame->write_cap ? frame->write_cap * 2u : 16u;
    while (cap < needed) {
        if (cap > UINT32_MAX / 2u)
            return false;
        cap *= 2u;
    }
    if ((size_t)cap > SIZE_MAX / sizeof(*frame->writes))
        return false;
    frame->writes = frame->writes
        ? cetta_realloc(frame->writes,
              (size_t)cap * sizeof(*frame->writes))
        : cetta_malloc((size_t)cap * sizeof(*frame->writes));
    frame->write_cap = cap;
    return true;
}

/* Exact reachability through the candidate-local delta and the immutable
 * outer image.  `VarIdSet` is both the worklist and the visited set, so an
 * already malformed outer cycle cannot make this check diverge. */
static BindingsReachability bindings_exclusive_frame_value_reaches(
        const BindingsExclusiveFrame *frame, Bindings *outer,
        BindingValue value, VarId target) {
    if (!frame || !frame->active || !outer || !value.skeleton ||
        target == VAR_ID_NONE)
        return BINDINGS_REACHABILITY_UNKNOWN;
    size_t single_remaining = bindings_dereference_limit(outer);
    size_t local_bound = (size_t)frame->schema->len + frame->write_len;
    single_remaining = local_bound > SIZE_MAX - single_remaining
        ? SIZE_MAX : single_remaining + local_bound;
    for (;;) {
        VarId single = binding_value_single_variable_id(value);
        if (single == VAR_ID_NONE)
            break;
        if (binding_var_eq(single, target))
            return BINDINGS_REACHABILITY_PRESENT;
        if (single_remaining-- == 0u)
            return BINDINGS_REACHABILITY_UNKNOWN;

        bool candidate_owned = false;
        BindingValue next = bindings_exclusive_frame_lookup(
            frame, NULL, single, &candidate_owned);
        if (!candidate_owned &&
            !bindings_frame_index_lookup_context_id(
                outer, value, single, &next)) {
            next = bindings_lookup_value_id(outer, single);
        }
        if (!next.skeleton ||
            (next.skeleton->kind == ATOM_VAR &&
             binding_var_eq(binding_value_variable_id(next), single))) {
            return BINDINGS_REACHABILITY_ABSENT;
        }
        value = next;
    }
    if (!atom_has_vars(value.skeleton))
        return BINDINGS_REACHABILITY_ABSENT;

    VarIdSet reachable;
    var_id_set_init(&reachable);
    if (!collect_value_var_ids(value, &reachable)) {
        var_id_set_free(&reachable);
        return BINDINGS_REACHABILITY_UNKNOWN;
    }
    for (uint32_t cursor = 0u; cursor < reachable.len; cursor++) {
        VarId current = reachable.items[cursor];
        if (binding_var_eq(current, target)) {
            var_id_set_free(&reachable);
            return BINDINGS_REACHABILITY_PRESENT;
        }
        BindingValue next = bindings_exclusive_frame_lookup(
            frame, outer, current, NULL);
        if (!next.skeleton ||
            (next.skeleton->kind == ATOM_VAR &&
             binding_var_eq(binding_value_variable_id(next), current))) {
            continue;
        }
        if (atom_has_vars(next.skeleton) &&
            !collect_value_var_ids(next, &reachable)) {
            var_id_set_free(&reachable);
            return BINDINGS_REACHABILITY_UNKNOWN;
        }
    }
    var_id_set_free(&reachable);
    return BINDINGS_REACHABILITY_ABSENT;
}

static bool bindings_exclusive_frame_prepare_value(
        Arena *arena, AtomDeepCopySession **transport,
        MatchBindContextRole role, BindingValue *value,
        Atom **name_key) {
    if (!arena || !value || !value->skeleton)
        return false;
#if CETTA_BUILD_WITH_RUNTIME_STATS
    if (binding_value_is_contextual(*value)) {
        cetta_runtime_stats_inc(match_bind_context_store_counter(role));
    } else if (bindings_materialized_value_has_epoch_variable(*value)) {
        cetta_runtime_stats_inc(
            role == MATCH_BIND_CONTEXT_STORED_EQUATION
                ? CETTA_RUNTIME_COUNTER_MATCH_BIND_STORED_EQUATION_MATERIALIZED_EPOCH_STORE
                : CETTA_RUNTIME_COUNTER_MATCH_BIND_ACTIVATION_SOURCE_MATERIALIZED_EPOCH_STORE);
    }
#else
    (void)role;
#endif
    if (value->kind == BINDING_VALUE_CONTEXTUAL &&
        !atom_graph_is_closed_for_arena(arena, value->skeleton)) {
        if (!transport)
            return false;
        if (!*transport)
            *transport = atom_deep_copy_session_new(arena);
#if CETTA_BUILD_WITH_RUNTIME_STATS
        size_t before = arena_accounted_live_bytes(arena);
#endif
        value->skeleton = *transport
            ? atom_deep_copy_session_copy(*transport, value->skeleton)
            : NULL;
        if (!value->skeleton)
            return false;
#if CETTA_BUILD_WITH_RUNTIME_STATS
        size_t after = arena_accounted_live_bytes(arena);
        cetta_runtime_stats_inc(match_bind_transport_root_counter(role));
        cetta_runtime_stats_add(
            match_bind_transport_bytes_counter(role),
            after >= before ? after - before : 0u);
#endif
    }
    if (name_key && *name_key &&
        !atom_graph_is_closed_for_arena(arena, *name_key)) {
        if (!transport)
            return false;
        if (!*transport)
            *transport = atom_deep_copy_session_new(arena);
        *name_key = *transport
            ? atom_deep_copy_session_copy(*transport, *name_key)
            : NULL;
        if (!*name_key)
            return false;
    }
    return true;
}

/* A contextual value whose variables all resolve, in the current image, to
 * ground syntax denotes that ground term, and keeps denoting it while the
 * binding being made survives: each binding it resolves through is older,
 * so rollback removes the new binding first.  Build the term once, sharing
 * the resolved children, so later observations read one atom instead of
 * re-resolving a closure chain.  Only the value's own template is walked;
 * a root that is not already ground syntax declines, so the cost stays
 * bounded by the template rather than by the chain behind it. */
enum { BINDINGS_GROUND_BIND_NODE_LIMIT = 64 };

static Atom *binding_value_instantiate_ground_node(
        Bindings *bindings, Arena *arena, Atom *skeleton, uint32_t epoch,
        BindingValueKind kind, uint32_t *remaining) {
    if (!atom_has_vars(skeleton))
        return skeleton;
    if ((*remaining)-- == 0u)
        return NULL;
    if (skeleton->kind == ATOM_VAR) {
        BindingValue root;
        if (!bindings_resolve_value_root(
                bindings, binding_value_from_context_kind(
                    skeleton, epoch, kind), &root) ||
            !root.skeleton || atom_has_vars(root.skeleton))
            return NULL;
        return root.skeleton;
    }
    if (skeleton->kind != ATOM_EXPR ||
        !cetta_expr_len_fits_size(skeleton->expr.len))
        return NULL;
    Atom *draft = atom_expr_builder_begin(arena, skeleton->expr.len);
    if (!draft)
        return NULL;
    for (CettaExprIndex index = 0u; index < skeleton->expr.len; index++) {
        Atom *child = binding_value_instantiate_ground_node(
            bindings, arena, skeleton->expr.elems[index], epoch, kind,
            remaining);
        if (!child)
            return NULL;
        draft->expr.elems[index] = child;
    }
    return atom_expr_builder_finish(arena, draft);
}

static BindingValue binding_value_ground_or_self(
        Bindings *bindings, Arena *arena, BindingValue value) {
    if (!binding_value_is_contextual(value) || !value.skeleton ||
        value.skeleton->kind != ATOM_EXPR || !atom_has_vars(value.skeleton))
        return value;
    ArenaMark mark = arena_mark(arena);
    uint32_t remaining = BINDINGS_GROUND_BIND_NODE_LIMIT;
    Atom *ground = binding_value_instantiate_ground_node(
        bindings, arena, value.skeleton, value.epoch, value.kind, &remaining);
    if (!ground) {
        arena_reset(arena, mark);
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_MATCH_GROUND_BIND_DECLINE);
        return value;
    }
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_MATCH_GROUND_BIND_COMMIT);
    return binding_value_from_atom(ground);
}

/* Whether a value mentions no variable of the frame's identity.  A
 * contextual value's variables all take the value's context; a materialized
 * value carries its own qualified identities. */
static bool bindings_exclusive_frame_value_avoids(
        const BindingsExclusiveFrame *frame, BindingValue value) {
    if (!atom_has_vars(value.skeleton))
        return true;
    if (frame->epoch == 0u)
        return false;
    if (binding_value_is_contextual(value))
        return value.epoch != frame->epoch;
    VarIdSet support;
    var_id_set_init(&support);
    bool avoids = collect_value_var_ids(value, &support);
    for (uint32_t index = 0u; avoids && index < support.len; index++)
        avoids = var_epoch_suffix(support.items[index]) != frame->epoch;
    var_id_set_free(&support);
    return avoids;
}

static bool bindings_exclusive_frame_store(
        BindingsExclusiveFrame *frame, BindingsBuilder *builder,
        Arena *arena, AtomDeepCopySession **transport,
        MatchBindContextRole role, VarId id, SymbolId spelling,
        Atom *name_key, BindingValue value,
        BindingValue *authoritative_value_out) {
    if (authoritative_value_out)
        *authoritative_value_out = binding_value_from_atom(NULL);
    if (!frame || !frame->active || !builder || id == VAR_ID_NONE ||
        !value.skeleton ||
        !bindings_exclusive_frame_prepare_value(
            arena, transport, role, &value, &name_key)) {
        return false;
    }
    if (value.skeleton->kind == ATOM_VAR &&
        binding_var_eq(id, binding_value_variable_id(value))) {
        if (authoritative_value_out)
            *authoritative_value_out = value;
        return true;
    }
    BindingValue existing = bindings_exclusive_frame_lookup(
        frame, &builder->current, id, NULL);
    if (existing.skeleton) {
        if (!binding_value_equal(existing, value))
            return false;
        if (authoritative_value_out)
            *authoritative_value_out = existing;
        return true;
    }
    if (value.skeleton->kind == ATOM_VAR) {
        BindingValue other = bindings_exclusive_frame_lookup(
            frame, &builder->current,
            binding_value_variable_id(value), NULL);
        if (other.skeleton && other.skeleton->kind == ATOM_VAR &&
            binding_var_eq(binding_value_variable_id(other), id)) {
            if (authoritative_value_out)
                *authoritative_value_out = value;
            return true;
        }
    }
    uint32_t slot = 0u;
    bool local = bindings_exclusive_frame_source_slot(frame, id, &slot);
    bool certified = frame->fresh && !frame->frame_mentioned;
    bool avoids = certified &&
        bindings_exclusive_frame_value_avoids(frame, value);
    BindingsReachability reaches =
        local && avoids
            ? BINDINGS_REACHABILITY_ABSENT
            : bindings_exclusive_frame_value_reaches(
                  frame, &builder->current, value, id);
    if (reaches != BINDINGS_REACHABILITY_ABSENT)
        return false;
    /* A slot of this frame is new, so a ground value can take its ground
     * form here without changing what any existing binding denotes. */
    if (local)
        value = binding_value_ground_or_self(&builder->current, arena, value);
    if (frame->write_len == UINT32_MAX ||
        !bindings_exclusive_frame_reserve_writes(
            frame, frame->write_len + 1u)) {
        return false;
    }
    BindingsExclusiveWrite *write = &frame->writes[frame->write_len++];
    *write = (BindingsExclusiveWrite){
        .local_slot = local ? slot : UINT32_MAX,
        .external_id = local ? VAR_ID_NONE : id,
        .external_spelling = local ? SYMBOL_ID_NONE : spelling,
        .external_name_key = local ? NULL : name_key,
        .external_value = local
            ? binding_value_from_atom(NULL) : value,
    };
#ifndef CETTA_MUTATION_FRESH_FRAME_IGNORES_FRAME_MENTIONS
    if (certified && !avoids)
        frame->frame_mentioned = true;
#endif
    if (local)
        frame->values[slot] = value;
    if (authoritative_value_out)
        *authoritative_value_out = value;
    return true;
}

/* Whether the registration just made through `registrations` created the
 * frame's entry, under the schema its slots are numbered by.  Rolling back
 * past such a registration recycles the entry with every value in it, so its
 * local slots can be written at once.  Every stored write passed the occurs
 * check at store time against this same store; `certified` additionally asks
 * for the freshness certificate, for a caller that would otherwise re-derive
 * the verdict. */
static bool bindings_exclusive_frame_fresh_registration(
        const BindingsExclusiveFrame *frame, const BindingsBuilder *builder,
        uint32_t registrations, BindingsFrameRef ref, bool certified) {
    if ((certified && (!frame->fresh || frame->frame_mentioned)) ||
        builder->frame_registration_undo_len != registrations + 1u ||
        builder->frame_registration_undo[registrations].frame_existed)
        return false;
    const BindingsFrameIndexEntry *entry =
        bindings_frame_index_find_ref_const(
            builder->current.frame_index, ref);
    return entry && entry->schema == frame->schema;
}

/* Write every local slot of a freshly registered frame under one save.  An
 * AUDIT verdict at any slot is the whole-store audit, and a cycle once closed
 * stays closed, so one audit after the last write decides the same.  Failure
 * rolls back to `mark`. */
static bool bindings_exclusive_frame_fill_fresh_slots(
        BindingsExclusiveFrame *frame, BindingsBuilder *builder,
        BindingsFrameRef ref, uint32_t mark) {
    uint32_t filled = 0u;
    for (uint32_t index = 0u; index < frame->write_len; index++) {
        uint32_t slot = frame->writes[index].local_slot;
        if (slot == UINT32_MAX)
            continue;
        if (slot >= frame->schema->len ||
            !bindings_frame_index_prepare_value_context(
                &builder->current, builder, &frame->values[slot], true))
            goto fail;
    }
    if (!bindings_builder_snapshot(builder, NULL))
        goto fail;
    BindingsFrameIndexEntry *entry =
        bindings_frame_index_detach_entry_ref(&builder->current, ref);
    if (!entry)
        goto fail;
    for (uint32_t index = 0u; index < frame->write_len; index++) {
        uint32_t slot = frame->writes[index].local_slot;
        if (slot == UINT32_MAX)
            continue;
        BindingValue value = frame->values[slot];
        if (!bindings_frame_index_fill_slot(
                builder->current.frame_index, entry, slot, value))
            goto fail;
        bindings_rhs_variable_bloom_add(&builder->current, value);
        filled++;
    }
    builder->growth_count =
        builder->growth_count > UINT64_MAX - filled
            ? UINT64_MAX : builder->growth_count + filled;
    if (builder->current.cycle_state != BINDINGS_CYCLE_ACYCLIC &&
        !bindings_trial_is_acyclic(&builder->current))
        goto fail;
    return true;

fail:
    bindings_builder_rollback(builder, mark);
    return false;
}

bool bindings_exclusive_frame_freeze(
        BindingsExclusiveFrame *frame, BindingsBuilder *builder) {
    if (!frame || !frame->active || !frame->schema || !builder ||
        frame->epoch == 0u)
        return false;
    if (!bindings_builder_prepare_fresh_entries(
            builder, frame->write_len))
        return false;
    uint32_t mark = builder->trail_len;
    uint32_t registrations = builder->frame_registration_undo_len;
    BindingsFrameRef authority_ref = {0};
    if (!bindings_builder_register_complete_frame_schema_ref(
            builder, frame->schema, frame->epoch, &authority_ref))
        return false;
    bool filled = bindings_exclusive_frame_fresh_registration(
        frame, builder, registrations, authority_ref, false);
    if (filled && !bindings_exclusive_frame_fill_fresh_slots(
            frame, builder, authority_ref, mark))
        return false;
    for (uint32_t index = 0u; index < frame->write_len; index++) {
        const BindingsExclusiveWrite *write = &frame->writes[index];
        if (filled && write->local_slot != UINT32_MAX)
            continue;
        VarId id = write->local_slot == UINT32_MAX
            ? write->external_id
            : var_epoch_id(
                  frame->schema->source_ids[write->local_slot],
                  frame->epoch);
        SymbolId spelling = write->local_slot == UINT32_MAX
            ? write->external_spelling
            : frame->schema->spellings[write->local_slot];
        Atom *name_key = write->local_slot == UINT32_MAX
            ? write->external_name_key
            : frame->schema->name_keys[write->local_slot];
        BindingValue value = write->local_slot == UINT32_MAX
            ? write->external_value
            : frame->values[write->local_slot];
        if (!bindings_builder_add_id_internal_with_cycle_evidence(
                builder, id, spelling, name_key, value,
                true, BINDINGS_REACHABILITY_ABSENT, NULL)) {
            bindings_builder_rollback(builder, mark);
            return false;
        }
    }
    frame->active = false;
    return true;
}

uint64_t bindings_builder_frame_write_boundary(
        const BindingsBuilder *builder) {
    const BindingsFrameIndex *index = builder
        ? builder->current.frame_index : NULL;
    return index ? index->write_clock : 0u;
}

bool bindings_exclusive_frame_publish_slots(
        BindingsExclusiveFrame *frame, BindingsBuilder *builder) {
    if (!frame || !frame->active || !frame->schema || !builder ||
        frame->epoch == 0u ||
        bindings_exclusive_frame_has_external_writes(frame) ||
        frame->write_len > UINT32_MAX - builder->trail_len ||
        frame->write_len > UINT32_MAX - builder->frame_undo_len ||
        !bindings_builder_trail_reserve(
            builder, builder->trail_len + frame->write_len) ||
        !bindings_builder_frame_undo_reserve(
            builder, builder->frame_undo_len + frame->write_len)) {
        return false;
    }
    uint32_t mark = builder->trail_len;
    uint32_t registrations = builder->frame_registration_undo_len;
    BindingsFrameRef authority_ref = {0};
    if (!bindings_builder_register_complete_frame_schema_ref(
            builder, frame->schema, frame->epoch, &authority_ref)) {
        return false;
    }
    if (frame->write_len == 0u) {
        frame->active = false;
        return true;
    }
    uint64_t activation_boundary =
        bindings_builder_frame_write_boundary(builder);
    if (!bindings_frame_index_mark_activation_boundary_ref(
            &builder->current, authority_ref, activation_boundary)) {
        return false;
    }
    if (bindings_exclusive_frame_fresh_registration(
            frame, builder, registrations, authority_ref, true)) {
        if (!bindings_exclusive_frame_fill_fresh_slots(
                frame, builder, authority_ref, mark))
            return false;
        frame->active = false;
        return true;
    }
    for (uint32_t index = 0u; index < frame->write_len; index++) {
        const BindingsExclusiveWrite *write = &frame->writes[index];
        if (write->local_slot == UINT32_MAX ||
            write->local_slot >= frame->schema->len) {
            bindings_builder_rollback(builder, mark);
            return false;
        }
        uint32_t slot = write->local_slot;
        VarId id = var_epoch_id(
            frame->schema->source_ids[slot], frame->epoch);
        BindingValue value = frame->values[slot];
        BindingValue current = binding_value_from_atom(NULL);
        if (!bindings_frame_index_lookup_value_slot(
                &builder->current, authority_ref, slot, &current)) {
            bindings_builder_rollback(builder, mark);
            return false;
        }
        if (current.skeleton) {
            if (!binding_value_equal(current, value)) {
                bindings_builder_rollback(builder, mark);
                return false;
            }
            continue;
        }
        if (!bindings_builder_snapshot(builder, NULL)) {
            bindings_builder_rollback(builder, mark);
            return false;
        }
        /* Every slot value avoided the frame while the certificate held, so
         * the store-time verdict carries over unchanged. */
        bool fresh_evidence = frame->fresh && !frame->frame_mentioned;
        BindingsBindVerdict verdict = bindings_bind_verdict(
            &builder->current, id, value, fresh_evidence,
            fresh_evidence ? BINDINGS_REACHABILITY_ABSENT
                           : BINDINGS_REACHABILITY_UNKNOWN);
        if (verdict == BINDINGS_BIND_REFUSE ||
            !bindings_frame_index_prepare_value_context(
                &builder->current, builder, &value, true)) {
            bindings_builder_rollback(builder, mark);
            return false;
        }
        bool authority_moved = false;
        if (!bindings_builder_record_frame_slot_value(
                builder, authority_ref, slot, id, value,
                &authority_moved)) {
            bindings_builder_rollback(builder, mark);
            return false;
        }
        bindings_rhs_variable_bloom_add(&builder->current, value);
        if (verdict == BINDINGS_BIND_AUDIT &&
            !bindings_trial_is_acyclic(&builder->current)) {
            bindings_builder_rollback(builder, mark);
            return false;
        }
        if (builder->growth_count != UINT64_MAX)
            builder->growth_count++;
    }
    frame->active = false;
    return true;
}

static bool rule_frame_region_store_exclusive(
        RuleFrameRegion *view, BindingsBuilder *builder,
        Arena *arena, AtomDeepCopySession **transport,
        MatchBindContextRole role, VarId id, SymbolId spelling,
        Atom *name_key, BindingValue value,
        BindingValue *authoritative_value_out, bool *handled_out) {
    if (handled_out)
        *handled_out = false;
    if (!view || !view->exclusive || !view->exclusive->active)
        return false;
    if (handled_out)
        *handled_out = true;
    return bindings_exclusive_frame_store(
        view->exclusive, builder, arena, transport, role,
        id, spelling, name_key, value, authoritative_value_out);
}

static bool bindings_match_store_value_in_rule_region(
        RuleFrameRegion *view,
        Bindings *bindings, BindingsBuilder *builder, Arena *arena,
        AtomDeepCopySession **transport, MatchBindContextRole role,
        bool contextual_id, VarId id, SymbolId spelling,
        Atom *name_key, BindingValue value,
        BindingsReachability evidence,
        BindingValue *authoritative_value_out) {
    bool handled = false;
    bool stored = rule_frame_region_store_exclusive(
        view, builder, arena, transport, role, id, spelling,
        name_key, value, authoritative_value_out, &handled);
    if (handled)
        return stored;
    return bindings_match_store_value(
        bindings, builder, arena, transport, role, contextual_id,
        id, spelling, name_key, value, evidence,
        authoritative_value_out);
}

/*
 * Logical-environment projection
 * --------------------------------
 *
 * A long-lived explicit machine cannot retain every fresh equation variable
 * ever encountered.  At a semantic safe point it needs the transitive closure
 * of the variables still named by its continuation.  This is deliberately a
 * property of Bindings rather than of any one evaluator.
 *
 * The work set is hashed: a variable chain of length n is projected in O(n)
 * expected work instead of repeatedly rescanning an n-entry environment.
 */
typedef struct {
    VarId *slots;
    size_t slot_cap;
    size_t slot_len;
    VarId *work;
    size_t work_len;
    size_t work_cap;
    size_t work_next;
} BindingsReachableVars;

typedef struct {
    VarId id;
    uint32_t index_plus_one;
} BindingsReachableIndexSlot;

static void bindings_reachable_vars_init(BindingsReachableVars *vars) {
    memset(vars, 0, sizeof(*vars));
}

static void bindings_reachable_vars_free(BindingsReachableVars *vars) {
    free(vars->slots);
    free(vars->work);
    bindings_reachable_vars_init(vars);
}

static bool bindings_reachable_vars_rehash(
    BindingsReachableVars *vars, size_t requested_cap) {
    size_t cap = 16u;
    while (cap < requested_cap) {
        if (cap > SIZE_MAX / 2u)
            return false;
        cap *= 2u;
    }
    if (cap > SIZE_MAX / sizeof(*vars->slots))
        return false;
    VarId *slots = cetta_malloc(cap * sizeof(*slots));
    memset(slots, 0, cap * sizeof(*slots));
    for (size_t i = 0u; i < vars->slot_cap; i++) {
        VarId id = vars->slots[i];
        if (id == VAR_ID_NONE)
            continue;
        size_t slot = bindings_var_id_hash(id) & (cap - 1u);
        while (slots[slot] != VAR_ID_NONE)
            slot = (slot + 1u) & (cap - 1u);
        slots[slot] = id;
    }
    free(vars->slots);
    vars->slots = slots;
    vars->slot_cap = cap;
    return true;
}

static bool bindings_reachable_vars_contains(
    const BindingsReachableVars *vars, VarId id) {
    if (!vars || id == VAR_ID_NONE || vars->slot_cap == 0u)
        return false;
    size_t slot =
        bindings_var_id_hash(id) & (vars->slot_cap - 1u);
    while (vars->slots[slot] != VAR_ID_NONE) {
        if (vars->slots[slot] == id)
            return true;
        slot = (slot + 1u) & (vars->slot_cap - 1u);
    }
    return false;
}

static bool bindings_reachable_vars_add(
    BindingsReachableVars *vars, VarId id) {
    if (!vars || id == VAR_ID_NONE)
        return false;
    if (bindings_reachable_vars_contains(vars, id))
        return true;
    if (vars->slot_cap == 0u ||
        vars->slot_len + 1u >= vars->slot_cap / 2u) {
        size_t requested = vars->slot_cap
            ? vars->slot_cap * 2u : 16u;
        if (requested < vars->slot_cap ||
            !bindings_reachable_vars_rehash(vars, requested)) {
            return false;
        }
    }
    if (vars->work_len == vars->work_cap) {
        size_t next = vars->work_cap ? vars->work_cap * 2u : 16u;
        if (next < vars->work_cap ||
            next > SIZE_MAX / sizeof(*vars->work)) {
            return false;
        }
        vars->work = cetta_realloc(
            vars->work, next * sizeof(*vars->work));
        vars->work_cap = next;
    }
    size_t slot =
        bindings_var_id_hash(id) & (vars->slot_cap - 1u);
    while (vars->slots[slot] != VAR_ID_NONE)
        slot = (slot + 1u) & (vars->slot_cap - 1u);
    vars->slots[slot] = id;
    vars->slot_len++;
    vars->work[vars->work_len++] = id;
    return true;
}

static bool bindings_reachable_vars_add_value(
    BindingsReachableVars *vars, BindingValue value) {
    Atom *atom = value.skeleton;
    if (!atom || !atom_has_vars(atom))
        return true;
    VarId single = binding_value_single_variable_id(value);
    if (atom->kind == ATOM_VAR ||
        (single != VAR_ID_NONE && (atom->flags & ATOM_FLAG_HASH_STABLE)))
        return bindings_reachable_vars_add(vars, single);
    VarIdSet found;
    var_id_set_init(&found);
    if (!collect_value_var_ids(value, &found)) {
        var_id_set_free(&found);
        return false;
    }
    for (uint32_t i = 0u; i < found.len; i++) {
        if (!bindings_reachable_vars_add(vars, found.items[i])) {
            var_id_set_free(&found);
            return false;
        }
    }
    var_id_set_free(&found);
    return true;
}

static bool bindings_reachable_vars_add_atom(
    BindingsReachableVars *vars, Atom *atom) {
    return bindings_reachable_vars_add_value(vars, binding_value_from_atom(atom));
}

static bool bindings_reachable_vars_add_epoch_root(
    BindingsReachableVars *vars,
    const BindingsEpochRoot *root) {
    if (!vars || !root || !root->atom || root->epoch == 0u)
        return false;
    if (!atom_has_vars(root->atom))
        return true;
    if (root->variable_support) {
        CettaTermVariableSupportIterator iterator;
        cetta_term_variable_support_iterator_init(
            &iterator, root->variable_support);
        uint32_t base_id = 0u;
        while (cetta_term_variable_support_iterator_next(
                   &iterator, &base_id)) {
            if (!bindings_reachable_vars_add(
                    vars, var_epoch_id((VarId)base_id,
                                       root->epoch))) {
                return false;
            }
        }
        return true;
    }
    return bindings_reachable_vars_add_value(
        vars, binding_value_from_context(root->atom, root->epoch));
}

static bool bindings_reachable_value_intersects(
    const BindingsReachableVars *vars, BindingValue value, bool *intersects) {
    Atom *atom = value.skeleton;
    *intersects = false;
    if (!atom || !atom_has_vars(atom))
        return true;
    VarIdSet found;
    var_id_set_init(&found);
    if (!collect_value_var_ids(value, &found)) {
        var_id_set_free(&found);
        return false;
    }
    for (uint32_t i = 0u; i < found.len; i++) {
        if (bindings_reachable_vars_contains(vars, found.items[i])) {
            *intersects = true;
            break;
        }
    }
    var_id_set_free(&found);
    return true;
}

static bool bindings_reachable_index_build(
    const Bindings *src, BindingsReachableIndexSlot **slots_out,
    size_t *cap_out) {
    *slots_out = NULL;
    *cap_out = 0u;
    if (!src || src->len == 0u)
        return true;
    size_t needed = (size_t)src->len * 2u;
    if (needed < src->len)
        return false;
    size_t cap = 16u;
    while (cap < needed) {
        if (cap > SIZE_MAX / 2u)
            return false;
        cap *= 2u;
    }
    if (cap > SIZE_MAX / sizeof(**slots_out))
        return false;
    BindingsReachableIndexSlot *slots =
        cetta_malloc(cap * sizeof(*slots));
    memset(slots, 0, cap * sizeof(*slots));
    for (uint32_t i = 0u; i < src->len; i++) {
        VarId id = bindings_entry_at(src, i)->var_id;
        if (id == VAR_ID_NONE) {
            free(slots);
            return false;
        }
        size_t slot = bindings_var_id_hash(id) & (cap - 1u);
        while (slots[slot].id != VAR_ID_NONE &&
               slots[slot].id != id) {
            slot = (slot + 1u) & (cap - 1u);
        }
        /* Match lookup scans newest-to-oldest, so duplicates map to newest. */
        slots[slot].id = id;
        slots[slot].index_plus_one = i + 1u;
    }
    *slots_out = slots;
    *cap_out = cap;
    return true;
}

static uint32_t bindings_reachable_index_lookup(
    const BindingsReachableIndexSlot *slots, size_t cap, VarId id) {
    if (!slots || cap == 0u || id == VAR_ID_NONE)
        return 0u;
    size_t slot = bindings_var_id_hash(id) & (cap - 1u);
    while (slots[slot].id != VAR_ID_NONE) {
        if (slots[slot].id == id)
            return slots[slot].index_plus_one;
        slot = (slot + 1u) & (cap - 1u);
    }
    return 0u;
}

static int bindings_reachable_entry_index_compare(
        const void *left, const void *right) {
    uint32_t a = *(const uint32_t *)left;
    uint32_t b = *(const uint32_t *)right;
    return (a > b) - (a < b);
}

static bool bindings_projection_append_entry(
        const Bindings *src, Bindings *dst,
        const Binding *binding) {
    if (!src || !dst || !binding)
        return false;
    /* Framed variables are copied from their authoritative slots below.
     * A stale compatibility row must not re-enter the projected image. */
    if (bindings_frame_index_owns_id(src, binding->var_id))
        return true;
    BindingValue value = binding->value;
    if (!bindings_frame_index_prepare_value_context(
            dst, NULL, &value, false)) {
        return false;
    }
    if (bindings_frame_index_owns_id(dst, binding->var_id)) {
        if (!bindings_frame_index_ensure_presentation(
                dst, binding->var_id, binding->spelling,
                binding->name_key) ||
            !bindings_frame_index_record(
                dst, binding->var_id, value))
            return false;
        bindings_rhs_variable_bloom_add(dst, value);
        return true;
    }
    dst->entries[dst->len++] = *binding;
    dst->entries[dst->len - 1u].value = value;
    bindings_rhs_variable_bloom_add(dst, value);

    if (binding_contains_private_variant_slot(binding))
        dst->private_entry_count++;
    return true;
}

static bool bindings_projection_register_live_frames(
        const Bindings *src, Bindings *dst,
        const BindingsReachableVars *live) {
    if (!src || !dst || !live)
        return false;
    for (size_t index = 0u; index < live->work_len; index++) {
        VarId id = live->work[index];
        if (!bindings_frame_index_owns_id(src, id))
            continue;
        const BindingsFrameIndexEntry *frame = bindings_frame_index_find_frame_const(
            src->frame_index, var_epoch_suffix(id));
        if (!bindings_frame_index_register_kind(dst,
                frame->schema->source_ids, frame->schema->len, frame->epoch,
                frame->schema_complete, frame->schema, NULL, false) ||
            (frame->slot_len > frame->schema->len &&
             !bindings_frame_index_grow_slots(dst, frame->epoch, frame->slot_len)))
            return false;
    }
    return true;
}

static bool bindings_projection_copy_live_frame_values(
        const Bindings *src, Bindings *dst,
        const BindingsReachableVars *live) {
    if (!src || !dst || !live)
        return false;
    for (size_t index = 0u; index < live->work_len; index++) {
        VarId id = live->work[index];
        bool known = false;
        if (!bindings_frame_index_lookup(src, id, &known) || !known) {
            continue;
        }
        const BindingsFrameIndexEntry *source =
            bindings_frame_index_find_frame_const(
                src->frame_index, var_epoch_suffix(id));
        uint32_t source_slot = 0u;
        if (!source || !bindings_frame_index_find_slot(
                source, (VarId)var_base_id(id), &source_slot)) {
            return false;
        }
        BindingValue value = source->values[source_slot];
        if (!value.skeleton)
            continue;
        VarId source_id = (VarId)var_base_id(id);
        if (!bindings_frame_index_ensure_presentation(
                dst, id, bindings_frame_slot_spelling(source, source_slot),
                bindings_frame_slot_name_key(source, source_slot)) ||
            !bindings_frame_index_detach(dst)) {
            return false;
        }
        if (!bindings_frame_index_prepare_value_context(
                dst, NULL, &value, false)) {
            return false;
        }
        BindingsFrameIndexEntry *target =
            bindings_frame_index_find_frame(
                dst->frame_index, var_epoch_suffix(id));
        uint32_t target_slot = 0u;
        if (!target || !bindings_frame_index_find_slot(
                target, source_id, &target_slot) ||
            !bindings_frame_index_entry_detach(target)) {
            return false;
        }
        if (!bindings_frame_index_replace_slot_value(
                dst->frame_index, target, target_slot, value)) {
            return false;
        }
        bindings_rhs_variable_bloom_add(dst, value);
        target->write_version[target_slot] =
            source->write_version[source_slot];
        target->activation_write_boundary =
            source->activation_write_boundary;
        target->has_activation_write_boundary =
            source->has_activation_write_boundary;
        if (dst->frame_index->write_clock <
                src->frame_index->write_clock) {
            dst->frame_index->write_clock =
                src->frame_index->write_clock;
        }
    }
    return true;
}

/*
 * Project a modern unconstrained environment by following only the variables
 * reachable from the roots through the maintained VarId index.  The first
 * call may synchronize that derived index in O(|environment|); subsequent
 * projections visit O(|reachable bindings|) entries.  Sorting selected entry
 * positions restores their authoritative relative order.
 *
 * The dense projector below remains the semantic fallback for constraints, compactor selection maps, small environments, and an
 * explicitly disabled lookup index.
 */
static bool bindings_project_reachable_sparse(
    const Bindings *src, Atom *const *roots, size_t root_count,
    const BindingsEpochRoot *epoch_roots,
    size_t epoch_root_count,
    Bindings *dst) {
    if (!src || !dst || src == dst ||
        (root_count > 0u && !roots) ||
        (epoch_root_count > 0u && !epoch_roots) ||
        src->eq_len != 0u ||
        src->len < BINDINGS_LOOKUP_INDEX_THRESHOLD ||
        !bindings_lookup_index_enabled()) {
        return false;
    }

    Bindings *indexed = (Bindings *)src;
    BindingsLookupIndex *lookup =
        bindings_lookup_index_current(indexed);
    if (!lookup)
        return false;

    BindingsReachableVars live;
    bindings_reachable_vars_init(&live);
    uint32_t *selected = NULL;
    size_t selected_len = 0u;
    size_t selected_cap = 0u;
    bindings_init(dst);

    for (size_t i = 0u; i < root_count; i++) {
        if (!bindings_reachable_vars_add_atom(&live, roots[i]))
            goto fail;
    }
    for (size_t i = 0u; i < epoch_root_count; i++) {
        if (!bindings_reachable_vars_add_epoch_root(
                &live, &epoch_roots[i])) {
            goto fail;
        }
    }
    while (live.work_next < live.work_len) {
        VarId id = live.work[live.work_next++];
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_BINDINGS_PROJECT_SPARSE_INDEX_LOOKUP);
        BindingValue framed = binding_value_from_atom(NULL);
        if (bindings_frame_index_lookup_value(src, id, &framed) &&
            framed.skeleton &&
            !bindings_reachable_vars_add_value(&live, framed)) {
            goto fail;
        }
        uint32_t index_plus_one =
            bindings_frame_index_owns_id(src, id)
                ? 0u : bindings_lookup_index_find(lookup, id);
        if (index_plus_one == 0u)
            continue;
        uint32_t entry_index = index_plus_one - 1u;
        if (entry_index >= src->len ||
            !binding_var_eq(
                bindings_entry_at(src, entry_index)->var_id, id)) {
            goto fail;
        }
        if (selected_len == selected_cap) {
            size_t next = selected_cap ? selected_cap * 2u : 16u;
            if (next < selected_cap ||
                next > SIZE_MAX / sizeof(*selected)) {
                goto fail;
            }
            selected = cetta_realloc(
                selected, next * sizeof(*selected));
            selected_cap = next;
        }
        selected[selected_len++] = entry_index;
        if (!bindings_reachable_vars_add_atom(
                &live, bindings_entry_at(src, entry_index)->name_key) ||
            !bindings_reachable_vars_add_value(
                &live, bindings_entry_at(src, entry_index)->value)) {
            goto fail;
        }
    }

    if (selected_len > 1u) {
        qsort(
            selected, selected_len, sizeof(*selected),
            bindings_reachable_entry_index_compare);
    }
    if (selected_len > UINT32_MAX ||
        (selected_len > 0u &&
         !bindings_reserve_entries(
             dst, (uint32_t)selected_len))) {
        goto fail;
    }
    for (size_t i = 0u; i < selected_len; i++) {
        const Binding *binding =
            bindings_entry_at(src, selected[i]);
        if (!bindings_projection_append_entry(
                src, dst, binding)) {
            goto fail;
        }
    }
    if (!bindings_projection_register_live_frames(
            src, dst, &live) ||
        !bindings_projection_copy_live_frame_values(
            src, dst, &live))
        goto fail;
    dst->cycle_state =
        src->cycle_state == BINDINGS_CYCLE_ACYCLIC
            ? BINDINGS_CYCLE_ACYCLIC
            : BINDINGS_CYCLE_UNKNOWN;
    if (src->owners)
        bindings_inherit_owners(dst, src);
    bindings_prime_assign(dst, src);
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_PROJECT_SPARSE);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_BINDINGS_PROJECT_SPARSE_ENTRY,
        selected_len);
    free(selected);
    bindings_reachable_vars_free(&live);
    return true;

fail:
    free(selected);
    bindings_reachable_vars_free(&live);
    bindings_free(dst);
    bindings_init(dst);
    return false;
}

static bool bindings_project_reachable_selected(
    const Bindings *src, Atom *const *roots, size_t root_count,
    const BindingsEpochRoot *epoch_roots,
    size_t epoch_root_count,
    Bindings *dst, bool **keep_entries_out,
    bool **keep_constraints_out) {
    bool return_selection =
        keep_entries_out != NULL && keep_constraints_out != NULL;
    if ((keep_entries_out == NULL) !=
            (keep_constraints_out == NULL) ||
        !dst || src == dst || (root_count > 0u && !roots)) {
        return false;
    }
    if (epoch_root_count > 0u && !epoch_roots)
        return false;
    if (return_selection) {
        *keep_entries_out = NULL;
        *keep_constraints_out = NULL;
    }
    bindings_init(dst);
    if (!src)
        return true;
    if (!return_selection &&
        src->eq_len == 0u &&
        src->len >= BINDINGS_LOOKUP_INDEX_THRESHOLD &&
        bindings_lookup_index_enabled()) {
        return bindings_project_reachable_sparse(
            src, roots, root_count,
            epoch_roots, epoch_root_count, dst);
    }
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_PROJECT_DENSE);

    BindingsReachableVars live;
    BindingsReachableIndexSlot *index_slots = NULL;
    size_t index_cap = 0u;
    bool *keep_entries = NULL;
    bool *keep_constraints = NULL;
    bindings_reachable_vars_init(&live);

    /* A synchronized lookup index already names each variable's newest
     * binding. Borrow it for this read-only projection; a missing or partial
     * index retains the independent collector below. */
    const BindingsLookupIndex *lookup = src->lookup_index;
    if (lookup && lookup->synced_len != src->len)
        lookup = NULL;
    if (!lookup && !bindings_reachable_index_build(
            src, &index_slots, &index_cap)) {
        goto fail;
    }
    if (src->len > 0u) {
        keep_entries = cetta_malloc(
            (size_t)src->len * sizeof(*keep_entries));
        memset(keep_entries, 0,
               (size_t)src->len * sizeof(*keep_entries));
    }
    if (src->eq_len > 0u) {
        keep_constraints = cetta_malloc(
            (size_t)src->eq_len * sizeof(*keep_constraints));
        memset(keep_constraints, 0,
               (size_t)src->eq_len * sizeof(*keep_constraints));
    }
    for (size_t i = 0u; i < root_count; i++) {
        if (!bindings_reachable_vars_add_atom(&live, roots[i]))
            goto fail;
    }
    for (size_t i = 0u; i < epoch_root_count; i++) {
        if (!bindings_reachable_vars_add_epoch_root(
                &live, &epoch_roots[i])) {
            goto fail;
        }
    }


    for (;;) {
        while (live.work_next < live.work_len) {
            VarId id = live.work[live.work_next++];
            BindingValue framed = binding_value_from_atom(NULL);
            if (bindings_frame_index_lookup_value(src, id, &framed) &&
                framed.skeleton &&
                !bindings_reachable_vars_add_value(&live, framed)) {
                goto fail;
            }
            uint32_t index_plus_one = lookup
                ? (bindings_frame_index_owns_id(src, id)
                    ? 0u : bindings_lookup_index_find(lookup, id))
                : bindings_reachable_index_lookup(
                      index_slots, index_cap, id);
            if (index_plus_one == 0u)
                continue;
            uint32_t index = index_plus_one - 1u;
            if (keep_entries[index])
                continue;
            keep_entries[index] = true;
            if (!bindings_reachable_vars_add_atom(
                    &live, bindings_entry_at(src, index)->name_key) ||
                !bindings_reachable_vars_add_value(
                    &live, bindings_entry_at(src, index)->value)) {
                goto fail;
            }
        }

        bool added_constraint = false;
        for (uint32_t i = 0u; i < src->eq_len; i++) {
            if (keep_constraints[i])
                continue;
            bool lhs_live = false;
            bool rhs_live = false;
            bool ground =
                !atom_has_vars(src->constraints[i].lhs.skeleton) &&
                !atom_has_vars(src->constraints[i].rhs.skeleton);
            if (!ground) {
                if (!bindings_reachable_value_intersects(
                        &live, src->constraints[i].lhs, &lhs_live) ||
                    !bindings_reachable_value_intersects(
                        &live, src->constraints[i].rhs, &rhs_live)) {
                    goto fail;
                }
            }
            if (!ground && !lhs_live && !rhs_live)
                continue;
            keep_constraints[i] = true;
            added_constraint = true;
            if (!bindings_reachable_vars_add_value(
                    &live, src->constraints[i].lhs) ||
                !bindings_reachable_vars_add_value(
                    &live, src->constraints[i].rhs)) {
                goto fail;
            }
        }
        if (live.work_next == live.work_len && !added_constraint)
            break;
    }

    uint32_t kept_entries = 0u;
    for (uint32_t i = 0u; i < src->len; i++)
        kept_entries += keep_entries[i] ? 1u : 0u;
    uint32_t kept_constraints = 0u;
    for (uint32_t i = 0u; i < src->eq_len; i++)
        kept_constraints += keep_constraints[i] ? 1u : 0u;
    if (kept_entries > 0u &&
        !bindings_reserve_entries(dst, kept_entries)) {
        goto fail_dst;
    }
    if (kept_constraints > 0u &&
        !bindings_reserve_constraints(dst, kept_constraints)) {
        goto fail_dst;
    }
    for (uint32_t i = 0u; i < src->len; i++) {
        if (keep_entries[i]) {
            const Binding *src_entry = bindings_entry_at(src, i);
            if (!bindings_projection_append_entry(
                    src, dst, src_entry))
                goto fail_dst;
        }
    }
    for (uint32_t i = 0u; i < src->eq_len; i++) {
        if (keep_constraints[i]) {
            BindingConstraint constraint = src->constraints[i];
            if (!bindings_frame_index_prepare_value_context(
                    dst, NULL, &constraint.lhs, false) ||
                !bindings_frame_index_prepare_value_context(
                    dst, NULL, &constraint.rhs, false)) {
                goto fail_dst;
            }
            dst->constraints[dst->eq_len++] = constraint;
            if (constraint_contains_private_variant_slot(
                    &constraint)) {
                dst->private_constraint_count++;
            }
        }
    }
    dst->cycle_state =
        src->cycle_state == BINDINGS_CYCLE_ACYCLIC
            ? BINDINGS_CYCLE_ACYCLIC
            : BINDINGS_CYCLE_UNKNOWN;
    if (!bindings_projection_register_live_frames(
            src, dst, &live) ||
        !bindings_projection_copy_live_frame_values(
            src, dst, &live))
        goto fail_dst;
    if (src->owners)
        bindings_inherit_owners(dst, src);
    bindings_prime_assign(dst, src);

    free(index_slots);
    if (return_selection) {
        *keep_entries_out = keep_entries;
        *keep_constraints_out = keep_constraints;
    } else {
        free(keep_entries);
        free(keep_constraints);
    }
    bindings_reachable_vars_free(&live);
    return true;

fail_dst:
    bindings_free(dst);
    bindings_init(dst);
fail:
    free(index_slots);
    free(keep_entries);
    free(keep_constraints);
    bindings_reachable_vars_free(&live);
    return false;
}

bool bindings_project_reachable_with_epoch_roots_and_entry_marks(
    const Bindings *src, Atom *const *roots, size_t root_count,
    const BindingsEpochRoot *epoch_roots,
    size_t epoch_root_count, uint32_t *entry_marks,
    size_t entry_mark_count, Bindings *dst) {
    if (!dst ||
        (epoch_root_count > 0u && !epoch_roots) ||
        (entry_mark_count > 0u && (!src || !entry_marks)) ||
        entry_mark_count > SIZE_MAX / sizeof(uint32_t)) {
        return false;
    }
    for (size_t index = 0u; index < entry_mark_count; index++) {
        if (entry_marks[index] > src->len)
            return false;
    }

    /* Selection maps exist only to translate entry-prefix marks.  Asking for
     * them when there are no marks forces the dense projector and defeats the
     * maintained VarId index for every ordinary host/child projection. */
    if (entry_mark_count == 0u) {
        return bindings_project_reachable_selected(
            src, roots, root_count,
            epoch_roots, epoch_root_count, dst,
            NULL, NULL);
    }

    Bindings projected;
    bool *keep_entries = NULL;
    bool *keep_constraints = NULL;
    if (!bindings_project_reachable_selected(
            src, roots, root_count,
            epoch_roots, epoch_root_count, &projected,
            &keep_entries, &keep_constraints)) {
        return false;
    }

    if (entry_mark_count == 1u) {
        uint32_t retained = 0u;
        for (uint32_t entry = 0u; entry < entry_marks[0]; entry++)
            retained += keep_entries[entry] ? 1u : 0u;
        entry_marks[0] = retained;
    } else {
        uint32_t last = 0u;
        for (size_t index = 0u; index < entry_mark_count; index++) {
            if (entry_marks[index] > last)
                last = entry_marks[index];
        }
        uint64_t prefix_count = (uint64_t)last + 1u;
        uint32_t *prefix = prefix_count <= SIZE_MAX / sizeof(*prefix)
            ? malloc((size_t)prefix_count * sizeof(*prefix)) : NULL;
        if (!prefix) {
            bindings_free(&projected);
            free(keep_entries);
            free(keep_constraints);
            return false;
        }
        /* Every mark observes the same retained-entry prefix. Build that
         * prefix once; rescanning it per activation costs marks times entries.
         * Indexed readout preserves arbitrary mark order and duplicates. */
        prefix[0] = 0u;
        for (uint32_t entry = 0u; entry < last; entry++)
            prefix[entry + 1u] = prefix[entry] + (keep_entries[entry] ? 1u : 0u);
        for (size_t index = 0u; index < entry_mark_count; index++)
            entry_marks[index] = prefix[entry_marks[index]];
        free(prefix);
    }
    *dst = projected;
    free(keep_entries);
    free(keep_constraints);
    return true;
}

bool bindings_project_reachable_with_entry_marks(
    const Bindings *src, Atom *const *roots, size_t root_count,
    uint32_t *entry_marks, size_t entry_mark_count,
    Bindings *dst) {
    return bindings_project_reachable_with_epoch_roots_and_entry_marks(
        src, roots, root_count, NULL, 0u,
        entry_marks, entry_mark_count, dst);
}

bool bindings_project_reachable_with_epoch_roots(
    const Bindings *src, Atom *const *roots, size_t root_count,
    const BindingsEpochRoot *epoch_roots,
    size_t epoch_root_count, Bindings *dst) {
    return bindings_project_reachable_with_epoch_roots_and_entry_marks(
        src, roots, root_count, epoch_roots, epoch_root_count,
        NULL, 0u, dst);
}
bool bindings_project_reachable(
    const Bindings *src, Atom *const *roots, size_t root_count,
    Bindings *dst) {
    return bindings_project_reachable_with_entry_marks(
        src, roots, root_count, NULL, 0u, dst);
}

static int bindings_checkpoint_mark_compare(
    const void *left, const void *right) {
    uint32_t a = *(const uint32_t *)left;
    uint32_t b = *(const uint32_t *)right;
    return (a > b) - (a < b);
}

static size_t bindings_checkpoint_mark_find(
    const uint32_t *marks, size_t count, uint32_t target) {
    size_t low = 0u;
    size_t high = count;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        if (marks[middle] < target)
            low = middle + 1u;
        else
            high = middle;
    }
    return low;
}

bool bindings_builder_compact_reachable_with_epoch_roots_and_entry_marks(
    BindingsBuilder *bb, Atom *const *roots, size_t root_count,
    const BindingsEpochRoot *epoch_roots,
    size_t epoch_root_count,
    uint32_t *checkpoint_marks, size_t checkpoint_count,
    uint32_t *entry_marks, size_t entry_mark_count,
    uint64_t *discarded_logical_items,
    uint64_t *discarded_trail_entries) {
    if (discarded_logical_items)
        *discarded_logical_items = 0u;
    if (discarded_trail_entries)
        *discarded_trail_entries = 0u;
    if (!bb || (root_count > 0u && !roots) ||
        (epoch_root_count > 0u && !epoch_roots) ||
        (checkpoint_count > 0u && !checkpoint_marks) ||
        (entry_mark_count > 0u && !entry_marks) ||
        checkpoint_count > UINT32_MAX ||
        entry_mark_count > SIZE_MAX / sizeof(uint32_t)) {
        return false;
    }

    uint32_t old_trail_len = bb->trail_len;
    for (size_t i = 0u; i < checkpoint_count; i++) {
        if (checkpoint_marks[i] > old_trail_len)
            return false;
    }
    for (size_t i = 0u; i < entry_mark_count; i++) {
        if (entry_marks[i] > bb->current.len)
            return false;
    }

    Bindings projected;
    bool *keep_entries = NULL;
    bool *keep_constraints = NULL;
    if (!bindings_project_reachable_selected(
            &bb->current, roots, root_count,
            epoch_roots, epoch_root_count, &projected,
            &keep_entries, &keep_constraints)) {
        return false;
    }

    size_t entry_prefix_count = (size_t)bb->current.len + 1u;
    size_t constraint_prefix_count =
        (size_t)bb->current.eq_len + 1u;
    if (entry_prefix_count > SIZE_MAX / sizeof(uint32_t) ||
        constraint_prefix_count > SIZE_MAX / sizeof(uint32_t)) {
        bindings_free(&projected);
        free(keep_entries);
        free(keep_constraints);
        return false;
    }

    uint32_t *entry_prefix =
        cetta_malloc(entry_prefix_count * sizeof(*entry_prefix));
    uint32_t *private_entry_prefix =
        cetta_malloc(entry_prefix_count *
                     sizeof(*private_entry_prefix));
    uint32_t *constraint_prefix =
        cetta_malloc(constraint_prefix_count *
                     sizeof(*constraint_prefix));
    uint32_t *private_constraint_prefix =
        cetta_malloc(constraint_prefix_count *
                     sizeof(*private_constraint_prefix));
    entry_prefix[0] = 0u;

    private_entry_prefix[0] = 0u;
    for (uint32_t i = 0u; i < bb->current.len; i++) {
        bool keep = keep_entries[i];
        const Binding *current_entry = bindings_entry_at(&bb->current, i);
        entry_prefix[i + 1u] =
            entry_prefix[i] + (keep ? 1u : 0u);
        private_entry_prefix[i + 1u] =
            private_entry_prefix[i] +
            (keep && binding_contains_private_variant_slot(current_entry)
                 ? 1u : 0u);
    }
    constraint_prefix[0] = 0u;
    private_constraint_prefix[0] = 0u;
    for (uint32_t i = 0u; i < bb->current.eq_len; i++) {
        bool keep = keep_constraints[i];
        constraint_prefix[i + 1u] =
            constraint_prefix[i] + (keep ? 1u : 0u);
        private_constraint_prefix[i + 1u] =
            private_constraint_prefix[i] +
            (keep && constraint_contains_private_variant_slot(
                         &bb->current.constraints[i])
                 ? 1u : 0u);
    }

    uint32_t *sorted_marks = NULL;
    uint32_t *next_marks = NULL;
    uint32_t *next_entry_marks = NULL;
    BindingsBuilderTrailEntry *next_trail = NULL;
    BindingsFrameUndoEntry *next_frame_undo = NULL;
    uint32_t next_frame_undo_len = 0u;
    PrimeOccurrence *next_prime_trail = NULL;
    uint32_t next_prime_len = 0u;
    uint32_t next_prime_cap = 0u;
    size_t unique_count = 0u;
    if (checkpoint_count > 0u) {
        sorted_marks = cetta_malloc(
            checkpoint_count * sizeof(*sorted_marks));
        next_marks = cetta_malloc(
            checkpoint_count * sizeof(*next_marks));
        memcpy(sorted_marks, checkpoint_marks,
               checkpoint_count * sizeof(*sorted_marks));
        qsort(sorted_marks, checkpoint_count,
              sizeof(*sorted_marks),
              bindings_checkpoint_mark_compare);
        for (size_t i = 0u; i < checkpoint_count; i++) {
            if (unique_count == 0u ||
                sorted_marks[i] !=
                    sorted_marks[unique_count - 1u]) {
                sorted_marks[unique_count++] = sorted_marks[i];
            }
        }
        next_trail = cetta_malloc(
            unique_count * sizeof(*next_trail));
        for (size_t i = 0u; i < unique_count; i++) {
            uint32_t mark = sorted_marks[i];
            bool prime_present =
                mark == old_trail_len
                    ? bindings_prime_present(&bb->current)
                    : bb->trail[mark].prime_state_present != 0u;
            if (prime_present)
                next_prime_cap++;
        }
        if (next_prime_cap > 0u) {
            next_prime_trail = cetta_malloc(
                (size_t)next_prime_cap * sizeof(*next_prime_trail));
        }
    }
    if (entry_mark_count > 0u) {
        next_entry_marks = malloc(
            entry_mark_count * sizeof(*next_entry_marks));
        if (!next_entry_marks) {
            bindings_free(&projected);
            free(keep_entries);
            free(keep_constraints);
            free(entry_prefix);

            free(private_entry_prefix);
            free(constraint_prefix);
            free(private_constraint_prefix);
            free(sorted_marks);
            free(next_marks);
            free(next_trail);
            free(next_frame_undo);
            free(next_prime_trail);
            return false;
        }
        for (size_t i = 0u; i < entry_mark_count; i++)
            next_entry_marks[i] = entry_prefix[entry_marks[i]];
    }

    bool valid = true;
    for (size_t i = 0u; i < unique_count; i++) {
        uint32_t mark = sorted_marks[i];
        BindingsBuilderTrailEntry state;
        const PrimeOccurrence *prime = NULL;
        if (mark == old_trail_len) {
            state = (BindingsBuilderTrailEntry){
                        .len = bb->current.len,
                .eq_len = bb->current.eq_len,
                .cycle_state = bb->current.cycle_state,
                .derived_nonzero =
                    bindings_derived_nonzero(&bb->current),
            };
            if (bindings_prime_present(&bb->current))
                prime = bb->current.prime_ext;
        } else {
            state = bb->trail[mark];
            prime = bindings_builder_checkpoint_prime(bb, &state);
            if (state.prime_state_present && !prime) {
                valid = false;
                break;
            }
        }
        if (state.len > bb->current.len ||
            state.eq_len > bb->current.eq_len) {
            valid = false;
            break;
        }
        uint32_t original_len = state.len;
        uint32_t original_eq_len = state.eq_len;
        state.len = entry_prefix[state.len];
        state.eq_len = constraint_prefix[state.eq_len];
        state.derived_nonzero = 0u;

        if (private_entry_prefix[original_len] != 0u) {
            state.derived_nonzero |=
                BINDINGS_DERIVED_PRIVATE_ENTRY_NONZERO;
        }
        if (private_constraint_prefix[original_eq_len] != 0u) {
            state.derived_nonzero |=
                BINDINGS_DERIVED_PRIVATE_CONSTRAINT_NONZERO;
        }
        state.cycle_state =
            state.cycle_state == BINDINGS_CYCLE_ACYCLIC
                ? BINDINGS_CYCLE_ACYCLIC
                : BINDINGS_CYCLE_UNKNOWN;
        state.prime_state_mark = next_prime_len;
        state.prime_state_present = prime != NULL;
        if (prime) {
            if (next_prime_len >= next_prime_cap) {
                valid = false;
                break;
            }
            next_prime_trail[next_prime_len++] = *prime;
        }
        next_trail[i] = state;
    }
    if (valid) {
        for (size_t i = 0u; i < checkpoint_count; i++) {
            size_t mapped = bindings_checkpoint_mark_find(
                sorted_marks, unique_count, checkpoint_marks[i]);
            if (mapped >= unique_count ||
                sorted_marks[mapped] != checkpoint_marks[i] ||
                mapped > UINT32_MAX) {
                valid = false;
                break;
            }
            next_marks[i] = (uint32_t)mapped;
        }
    }
    if (valid && unique_count > 0u && bb->frame_undo_len > 0u) {
        next_frame_undo = cetta_malloc(
            (size_t)bb->frame_undo_len * sizeof(*next_frame_undo));
        for (uint32_t i = 0u; i < bb->frame_undo_len; i++) {
            BindingsFrameUndoEntry undo = bb->frame_undo[i];
            VarId id = var_epoch_id(
                (VarId)undo.source_id,
                undo.frame_ref.identity);
            const BindingsFrameIndexEntry *projected_frame = NULL;
            uint32_t projected_slot = 0u;
            if (undo.written_write_version == 0u ||
                !bindings_frame_index_find_coordinate(
                    &projected, id,
                    &projected_frame, &projected_slot) ||
                !projected_frame->values[projected_slot].skeleton) {
                continue;
            }
            undo.frame_ref = bindings_frame_ref_from_entry(
                projected.frame_index, projected_frame);
            undo.slot = projected_slot;
            if (!bindings_frame_ref_is_valid(undo.frame_ref)) {
                valid = false;
                break;
            }
            size_t mapped = bindings_checkpoint_mark_find(
                sorted_marks, unique_count, undo.trail_mark);
            if (mapped == unique_count ||
                sorted_marks[mapped] > undo.trail_mark) {
                if (mapped == 0u)
                    continue;
                mapped--;
            }
            if (mapped > UINT32_MAX) {
                valid = false;
                break;
            }
            undo.trail_mark = (uint32_t)mapped;
            next_frame_undo[next_frame_undo_len++] = undo;
        }
    }
    if (!valid) {
        bindings_free(&projected);
        free(keep_entries);
        free(keep_constraints);
        free(entry_prefix);

        free(private_entry_prefix);
        free(constraint_prefix);
        free(private_constraint_prefix);
        free(sorted_marks);
        free(next_marks);
        free(next_entry_marks);
        free(next_trail);
        free(next_frame_undo);
        free(next_prime_trail);
        return false;
    }

    uint32_t kept_owners = 0u;
    for (uint32_t i = 0; i < bb->owner_undo_len; i++) {
        BindingsOwnerUndo undo = bb->owner_undo[i];
        size_t mapped = bindings_checkpoint_mark_find(sorted_marks, unique_count, undo.trail_mark);
        if (mapped == unique_count || sorted_marks[mapped] > undo.trail_mark) {
            if (mapped == 0u) {
                bindings_owners_release(undo.previous);
                continue;
            }
            mapped--;
        }
        undo.trail_mark = (uint32_t)mapped;
        bb->owner_undo[kept_owners++] = undo;
    }
    bb->owner_undo_len = kept_owners;
    uint64_t old_logical_items =
        (uint64_t)bb->current.len + bindings_frame_value_count(&bb->current) + bb->current.eq_len;
    uint64_t next_logical_items =
        (uint64_t)projected.len + bindings_frame_value_count(&projected) + projected.eq_len;
    bindings_replace(&bb->current, &projected);
    /* Projection reconstructs the union of frames reachable from every
     * surviving root and checkpoint.  That reconstructed directory is the
     * common base for the translated checkpoints; registration records refer
     * to schemas in the discarded directory and must not cross this rebase.
     * Subsequent frame manufacture records fresh history against the new
     * base, while frame_undo below continues to restore per-slot values. */
    bindings_builder_discard_frame_registration_history(bb, true);
    if (bb->rollback_count != UINT64_MAX)
        bb->rollback_count++;
    free(bb->trail);
    free(bb->frame_undo);
    free(bb->prime_trail);
    bb->trail = next_trail;
    bb->trail_len = (uint32_t)unique_count;
    bb->trail_cap = (uint32_t)unique_count;
    bb->frame_undo = next_frame_undo;
    bb->frame_undo_len = next_frame_undo_len;
    bb->frame_undo_cap = next_frame_undo_len;
    bb->prime_trail = next_prime_trail;
    bb->prime_trail_len = next_prime_len;
    bb->prime_trail_cap = next_prime_cap;
    if (checkpoint_count > 0u) {
        memcpy(checkpoint_marks, next_marks,
               checkpoint_count * sizeof(*checkpoint_marks));
    }
    if (entry_mark_count > 0u) {
        memcpy(entry_marks, next_entry_marks,
               entry_mark_count * sizeof(*entry_marks));
    }
    if (discarded_logical_items &&
        old_logical_items > next_logical_items) {
        *discarded_logical_items =
            old_logical_items - next_logical_items;
    }
    if (discarded_trail_entries &&
        old_trail_len > unique_count) {
        *discarded_trail_entries =
            old_trail_len - unique_count;
    }

    free(keep_entries);
    free(keep_constraints);
    free(entry_prefix);

    free(private_entry_prefix);
    free(constraint_prefix);
    free(private_constraint_prefix);
    free(sorted_marks);
    free(next_marks);
    free(next_entry_marks);
    return true;
}

bool bindings_builder_compact_reachable_with_entry_marks(
    BindingsBuilder *bb, Atom *const *roots, size_t root_count,
    uint32_t *checkpoint_marks, size_t checkpoint_count,
    uint32_t *entry_marks, size_t entry_mark_count,
    uint64_t *discarded_logical_items,
    uint64_t *discarded_trail_entries) {
    return bindings_builder_compact_reachable_with_epoch_roots_and_entry_marks(
        bb, roots, root_count, NULL, 0u,
        checkpoint_marks, checkpoint_count,
        entry_marks, entry_mark_count,
        discarded_logical_items, discarded_trail_entries);
}

bool bindings_builder_compact_reachable_with_epoch_roots(
    BindingsBuilder *bb, Atom *const *roots, size_t root_count,
    const BindingsEpochRoot *epoch_roots,
    size_t epoch_root_count,
    uint32_t *checkpoint_marks, size_t checkpoint_count,
    uint64_t *discarded_logical_items,
    uint64_t *discarded_trail_entries) {
    return bindings_builder_compact_reachable_with_epoch_roots_and_entry_marks(
        bb, roots, root_count, epoch_roots, epoch_root_count,
        checkpoint_marks, checkpoint_count, NULL, 0u,
        discarded_logical_items, discarded_trail_entries);
}
bool bindings_builder_compact_reachable(
    BindingsBuilder *bb, Atom *const *roots, size_t root_count,
    uint32_t *checkpoint_marks, size_t checkpoint_count,
    uint64_t *discarded_logical_items,
    uint64_t *discarded_trail_entries) {
    return bindings_builder_compact_reachable_with_entry_marks(
        bb, roots, root_count,
        checkpoint_marks, checkpoint_count, NULL, 0u,
        discarded_logical_items, discarded_trail_entries);
}

static void rename_var_map_init(RenameVarMap *map) {
    map->items = NULL;
    map->len = 0;
    map->cap = 0;
}

static void rename_var_map_free(RenameVarMap *map) {
    free(map->items);
    map->items = NULL;
    map->len = 0;
    map->cap = 0;
}

static Atom *rename_var_map_lookup(RenameVarMap *map, VarId id) {
    for (uint32_t i = 0; i < map->len; i++) {
        if (map->items[i].id == id)
            return map->items[i].mapped;
    }
    return NULL;
}

static Atom *rename_var_map_add_fresh(RenameVarMap *map, Arena *a, Atom *var) {
    CETTA_FRAME_IDENTITY_SCOPE(frame_identity_scope);
    uint32_t suffix = cetta_frame_identity_scope_fresh(&frame_identity_scope);
    Atom *fresh = atom_var_like(a, var, var_epoch_id(var->var_id, suffix));
    if (map->len >= map->cap) {
        uint32_t next_cap = map->cap ? map->cap * 2u : 8u;
        if (next_cap < map->cap ||
            (size_t)next_cap > SIZE_MAX / sizeof(*map->items))
            return NULL;
        map->items = cetta_realloc(
            map->items, sizeof(*map->items) * (size_t)next_cap);
        map->cap = next_cap;
    }
    map->items[map->len].id = var->var_id;
    map->items[map->len].mapped = fresh;
    map->len++;
    return fresh;
}

typedef enum {
    RENAME_VARS_VISIT,
    RENAME_VARS_BUILD,
} RenameVarsTaskKind;

typedef struct {
    RenameVarsTaskKind kind;
    Atom *source;
    Atom **destination;
    Atom **children;
} RenameVarsTask;

typedef struct {
    RenameVarsTask inline_tasks[64];
    RenameVarsTask *tasks;
    size_t len;
    size_t cap;
} RenameVarsTaskStack;

static void rename_vars_task_stack_init(RenameVarsTaskStack *stack) {
    stack->tasks = stack->inline_tasks;
    stack->len = 0u;
    stack->cap = sizeof(stack->inline_tasks) / sizeof(stack->inline_tasks[0]);
}

static void rename_vars_task_stack_free(RenameVarsTaskStack *stack) {
    if (stack->tasks != stack->inline_tasks)
        free(stack->tasks);
}

static bool rename_vars_task_stack_push(RenameVarsTaskStack *stack,
                                        RenameVarsTask task) {
    if (stack->len == stack->cap) {
        if (stack->cap > SIZE_MAX / 2u ||
            stack->cap * 2u > SIZE_MAX / sizeof(*stack->tasks))
            return false;
        size_t next_cap = stack->cap * 2u;
        if (stack->tasks == stack->inline_tasks) {
            RenameVarsTask *next = cetta_malloc(
                sizeof(*next) * next_cap);
            memcpy(next, stack->inline_tasks,
                   sizeof(*next) * stack->len);
            stack->tasks = next;
        } else {
            stack->tasks = cetta_realloc(
                stack->tasks, sizeof(*stack->tasks) * next_cap);
        }
        stack->cap = next_cap;
    }
    stack->tasks[stack->len++] = task;
    return true;
}

static Atom *rename_vars_listed_iterative(Arena *a, Atom *root,
                                          const VarIdSet *listed,
                                          RenameVarMap *map,
                                          bool rename_listed) {
    Atom *result = NULL;
    RenameVarsTaskStack stack;
    FreshenEpochMemo memo;
    rename_vars_task_stack_init(&stack);
    freshen_epoch_memo_init(&memo);
    if (!rename_vars_task_stack_push(
            &stack,
            (RenameVarsTask){RENAME_VARS_VISIT, root, &result, NULL}))
        goto fail;
    while (stack.len > 0u) {
        RenameVarsTask task = stack.tasks[--stack.len];
        Atom *atom = task.source;
        if (!atom || !task.destination)
            goto fail;
        if (task.kind == RENAME_VARS_BUILD) {
            bool unchanged = true;
            for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
                if (!task.children[i])
                    goto fail;
                if (task.children[i] != atom->expr.elems[i])
                    unchanged = false;
            }
            Atom *built = unchanged
                ? atom
                : atom_expr(a, task.children, atom->expr.len);
            *task.destination = built;
            if (!freshen_epoch_memo_store(&memo, atom, built))
                goto fail;
            continue;
        }
        if (!atom_has_vars(atom)) {
            *task.destination = atom;
            continue;
        }
        Atom *memoized = freshen_epoch_memo_lookup(&memo, atom);
        if (memoized == &g_rename_walk_active)
            goto fail;
        if (memoized) {
            *task.destination = memoized;
            continue;
        }
        if (atom->kind == ATOM_VAR) {
            Atom *renamed = NULL;
            if (var_id_set_contains(listed, atom->var_id) !=
                rename_listed) {
                renamed = atom;
            } else {
                renamed = rename_var_map_lookup(map, atom->var_id);
                if (!renamed)
                    renamed = rename_var_map_add_fresh(map, a, atom);
            }
            if (!renamed ||
                !freshen_epoch_memo_store(&memo, atom, renamed))
                goto fail;
            *task.destination = renamed;
            continue;
        }
        if (atom->kind != ATOM_EXPR) {
            *task.destination = atom;
            if (!freshen_epoch_memo_store(&memo, atom, atom))
                goto fail;
            continue;
        }
        if (!cetta_expr_len_mul_fits_size(
                atom->expr.len, sizeof(Atom *)) ||
            !freshen_epoch_memo_store(
                &memo, atom, &g_rename_walk_active))
            goto fail;
        Atom **children = atom->expr.len
            ? arena_alloc(a, sizeof(*children) * (size_t)atom->expr.len)
            : NULL;
        if (!rename_vars_task_stack_push(
                &stack,
                (RenameVarsTask){RENAME_VARS_BUILD, atom,
                                 task.destination, children}))
            goto fail;
        for (CettaExprIndex i = atom->expr.len; i > 0u; i--)
            if (!rename_vars_task_stack_push(
                    &stack,
                    (RenameVarsTask){RENAME_VARS_VISIT,
                                     atom->expr.elems[i - 1u],
                                     &children[i - 1u], NULL}))
                goto fail;
    }
    freshen_epoch_memo_free(&memo);
    rename_vars_task_stack_free(&stack);
    return result;

fail:
    freshen_epoch_memo_free(&memo);
    rename_vars_task_stack_free(&stack);
    return NULL;
}

Atom *rename_vars(Arena *a, Atom *atom, uint32_t suffix) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_RENAME_VARS);
    switch (atom->kind) {
    case ATOM_VAR: {
        return atom_var_like(a, atom, var_epoch_id(atom->var_id, suffix));
    }
    case ATOM_EXPR: {
        Atom **new_elems = NULL;
        for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
            Atom *next = rename_vars(a, atom->expr.elems[i], suffix);
            if (!new_elems && next != atom->expr.elems[i]) {
                new_elems = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
                for (CettaExprIndex j = 0; j < i; j++)
                    new_elems[j] = atom->expr.elems[j];
            }
            if (new_elems)
                new_elems[i] = next;
        }
        if (!new_elems) return atom;
        return atom_expr(a, new_elems, atom->expr.len);
    }
    default:
        return atom;
    }
}

Atom *rename_vars_except(Arena *a, Atom *atom, Atom *ignore_spec) {
    if (!a || !atom || !ignore_spec)
        return NULL;
    VarIdSet ignore;
    RenameVarMap map;
    var_id_set_init(&ignore);
    rename_var_map_init(&map);
    Atom *result = collect_var_ids(ignore_spec, &ignore)
        ? rename_vars_listed_iterative(a, atom, &ignore, &map, false)
        : NULL;
    rename_var_map_free(&map);
    var_id_set_free(&ignore);
    return result;
}

/* Complement of rename_vars_except: freshen ONLY the variables occurring in
 * listed_spec, preserving the identity of every other variable (SWI's
 * copy_term/4 sharing contract).  An empty listed_spec is the identity. */
Atom *rename_vars_only(Arena *a, Atom *atom, Atom *listed_spec) {
    if (!a || !atom || !listed_spec)
        return NULL;
    VarIdSet listed;
    RenameVarMap map;
    var_id_set_init(&listed);
    rename_var_map_init(&map);
    Atom *result = collect_var_ids(listed_spec, &listed)
        ? rename_vars_listed_iterative(a, atom, &listed, &map, true)
        : NULL;
    rename_var_map_free(&map);
    var_id_set_free(&listed);
    return result;
}

/* ── One-way pattern matching ───────────────────────────────────────────── */

bool simple_match(Atom *pattern, Atom *target, Bindings *b) {
    /* Variable in pattern binds to target */
    if (pattern->kind == ATOM_VAR) {
        return bindings_add_var(b, pattern, target);
    }

    /* Same kind required */
    if (pattern->kind != target->kind) return false;

    switch (pattern->kind) {
    case ATOM_SYMBOL:
        return pattern->sym_id == target->sym_id;

    case ATOM_GROUNDED:
        if (pattern->ground.gkind != target->ground.gkind)
            return cetta_he_promoted_numbers_equal(pattern, target);
        switch (pattern->ground.gkind) {
        case GV_INT:    return pattern->ground.ival == target->ground.ival;
        case GV_FLOAT:  return pattern->ground.fval == target->ground.fval;
        case GV_BOOL:   return pattern->ground.bval == target->ground.bval;
        case GV_STRING: return strcmp(pattern->ground.sval, target->ground.sval) == 0;
        case GV_BIGINT:
            return cetta_bigint_compare_cstr(atom_bigint_cstr(pattern),
                                            atom_bigint_cstr(target)) == 0;
        case GV_RATIONAL:
            return cetta_rational_compare_cstr(atom_rational_cstr(pattern),
                                               atom_rational_cstr(target)) == 0;
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_FOREIGN:
            return pattern->ground.ptr == target->ground.ptr;
        case GV_BINDINGS:
            return atom_eq(pattern, target);
        case GV_PRIME_NEED_CAPABILITY:
        case GV_PRIME_CONTEXT:
        case GV_INTERNAL_TAG:
            return atom_eq(pattern, target);
        }
        return false;

    case ATOM_EXPR:
        if (pattern->expr.len != target->expr.len) return false;
        for (CettaExprIndex i = 0; i < pattern->expr.len; i++) {
            if (!simple_match(pattern->expr.elems[i], target->expr.elems[i], b))
                return false;
        }
        return true;

    case ATOM_VAR:
        /* Already handled above */
        return false;
    }
    return false;
}

static bool simple_match_builder_rec(Atom *pattern, Atom *target,
                                     BindingsBuilder *bb) {
    if (pattern->kind == ATOM_VAR)
        return bindings_builder_add_var_fresh(bb, pattern, target);

    if (pattern->kind != target->kind)
        return false;

    switch (pattern->kind) {
    case ATOM_SYMBOL:
        return pattern->sym_id == target->sym_id;

    case ATOM_GROUNDED:
        if (pattern->ground.gkind != target->ground.gkind)
            return cetta_he_promoted_numbers_equal(pattern, target);
        switch (pattern->ground.gkind) {
        case GV_INT:    return pattern->ground.ival == target->ground.ival;
        case GV_FLOAT:  return pattern->ground.fval == target->ground.fval;
        case GV_BOOL:   return pattern->ground.bval == target->ground.bval;
        case GV_STRING: return strcmp(pattern->ground.sval, target->ground.sval) == 0;
        case GV_BIGINT:
            return cetta_bigint_compare_cstr(atom_bigint_cstr(pattern),
                                            atom_bigint_cstr(target)) == 0;
        case GV_RATIONAL:
            return cetta_rational_compare_cstr(atom_rational_cstr(pattern),
                                               atom_rational_cstr(target)) == 0;
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_FOREIGN:
            return pattern->ground.ptr == target->ground.ptr;
        case GV_BINDINGS:
            return atom_eq(pattern, target);
        case GV_PRIME_NEED_CAPABILITY:
        case GV_PRIME_CONTEXT:
        case GV_INTERNAL_TAG:
            return atom_eq(pattern, target);
        }
        return false;

    case ATOM_EXPR:
        if (pattern->expr.len != target->expr.len)
            return false;
        for (CettaExprIndex i = 0; i < pattern->expr.len; i++) {
            if (!simple_match_builder_rec(pattern->expr.elems[i],
                                          target->expr.elems[i], bb)) {
                return false;
            }
        }
        return true;

    case ATOM_VAR:
        return false;
    }
    return false;
}

bool simple_match_builder(Atom *pattern, Atom *target, BindingsBuilder *bb) {
    return simple_match_builder_rec(pattern, target, bb);
}

/* ── Type matching (from HE spec Matching.lean:188-195) ────────────────── */

/* ── Loop-binding rejection (occurs check) ─────────────────────────────── */

static int32_t bindings_find_entry_index_for_loop(
        const Binding *logical, uint32_t logical_len,
        BindingValue value) {
    const Atom *var = value.skeleton;
    VarId id = binding_value_variable_id(value);
    if (!logical || !var || var->kind != ATOM_VAR)
        return -1;
    for (uint32_t i = logical_len; i > 0; i--) {
        uint32_t idx = i - 1;
        if (binding_var_eq(logical[idx].var_id, id))
            return (int32_t)idx;
    }
    return -1;
}

typedef enum {
    BINDINGS_LOOP_ATOM = 0,
    BINDINGS_LOOP_ENTRY_EXIT = 1,
} BindingsLoopFrameKind;

typedef struct {
    BindingsLoopFrameKind kind;
    BindingValue value;
    uint32_t entry;
} BindingsLoopFrame;

typedef struct {
    BindingsLoopFrame *items;
    size_t len;
    size_t cap;
    BindingsLoopFrame inline_items[32];
} BindingsLoopStack;

static bool bindings_loop_push(BindingsLoopStack *stack,
                               BindingsLoopFrame frame) {
    if (stack->len == stack->cap) {
        size_t next_cap = stack->cap * 2u;
        if (next_cap <= stack->cap ||
            next_cap > SIZE_MAX / sizeof(*stack->items))
            return false;
        BindingsLoopFrame *next = cetta_malloc(
            sizeof(*stack->items) * next_cap);
        memcpy(next, stack->items, sizeof(*stack->items) * stack->len);
        if (stack->items != stack->inline_items) free(stack->items);
        stack->items = next;
        stack->cap = next_cap;
    }
    stack->items[stack->len++] = frame;
    return true;
}

static bool bindings_entry_is_trivial_self(
        const Binding *logical, uint32_t idx) {
    const Binding *entry = &logical[idx];
    Atom *value = entry->value.skeleton;
    return value->kind == ATOM_VAR &&
           binding_value_variable_id(entry->value) == entry->var_id;
}

typedef struct {
    Binding *items;
    uint32_t len;
    uint32_t cap;
} BindingsLogicalArray;

static bool bindings_logical_array_append(
        const Binding *binding, void *raw_context) {
    BindingsLogicalArray *array = raw_context;
    if (array->len >= array->cap)
        return false;
    array->items[array->len++] = *binding;
    return true;
}

/* The cycle memo summarises derived truth about the current substitution
 * graph.  Writers keep it conservatively (unknown whenever they cannot
 * decide); a full audit is the ground truth it summarises, so the audit's
 * verdict is recorded through the read handle.  No Bindings object is ever
 * defined const.  Without this, one conservative writer would leave every
 * later audit a full traversal for the life of the environment. */
static void bindings_cycle_memoize_audit(const Bindings *b, uint8_t verdict) {
    ((Bindings *)b)->cycle_state = verdict;
}

bool bindings_has_loop(const Bindings *b) {
    size_t logical_count = 0u;
    if (!b)
        return false;
    if (b->cycle_state == BINDINGS_CYCLE_ACYCLIC)
        return false;
    if (!bindings_current_binding_count(b, &logical_count))
        return false;
    if (logical_count == 0u) {
        bindings_cycle_memoize_audit(b, BINDINGS_CYCLE_ACYCLIC);
        return false;
    }
    if (b->cycle_state == BINDINGS_CYCLE_PRESENT)
        return true;
    if (logical_count > UINT32_MAX ||
        logical_count > SIZE_MAX / sizeof(Binding))
        return true;
    Binding logical_stack[BINDINGS_TEMP_STACK_CAP];
    Binding *logical = logical_count <= BINDINGS_TEMP_STACK_CAP
        ? logical_stack
        : cetta_malloc(logical_count * sizeof(*logical));
    BindingsLogicalArray logical_array = {
        .items = logical,
        .len = 0u,
        .cap = (uint32_t)logical_count,
    };
    if (!bindings_for_each_current_binding(
            b, bindings_logical_array_append, &logical_array) ||
        logical_array.len != (uint32_t)logical_count) {
        if (logical != logical_stack)
            free(logical);
        return true;
    }
    uint8_t state_stack[BINDINGS_SEEN_STACK_CAP];
    uint8_t *state = logical_count <= BINDINGS_SEEN_STACK_CAP
        ? state_stack
        : cetta_malloc(sizeof(uint8_t) * logical_count);
    memset(state, 0, sizeof(uint8_t) * logical_count);
    BindingsLoopStack stack;
    stack.items = stack.inline_items;
    stack.len = 0;
    stack.cap = sizeof stack.inline_items / sizeof stack.inline_items[0];
    for (uint32_t i = 0; i < (uint32_t)logical_count; i++) {
        if (state[i] != 0) continue;
        if (bindings_entry_is_trivial_self(logical, i)) {
            state[i] = 2;
            continue;
        }
        state[i] = 1;
        if (!bindings_loop_push(
                &stack, (BindingsLoopFrame){.kind = BINDINGS_LOOP_ENTRY_EXIT, .entry = i}) ||
            !bindings_loop_push(
                &stack, (BindingsLoopFrame){.kind = BINDINGS_LOOP_ATOM,
                                     .value = logical[i].value}))
            goto representation_failure;

        while (stack.len > 0) {
            BindingsLoopFrame frame = stack.items[--stack.len];
            if (frame.kind == BINDINGS_LOOP_ENTRY_EXIT) {
                state[frame.entry] = 2;
                continue;
            }
            Atom *atom = frame.value.skeleton;
            if (!atom_has_vars(atom)) continue;
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_BINDINGS_LOOP_NODE_VISIT);
            if (atom->kind == ATOM_VAR) {
                int32_t found = bindings_find_entry_index_for_loop(
                    logical, (uint32_t)logical_count, frame.value);
                if (found < 0) continue;
                uint32_t entry = (uint32_t)found;
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_LOOP_CHECK);
                if (state[entry] == 1) goto loop_found;
                if (state[entry] == 2) continue;
                if (bindings_entry_is_trivial_self(logical, entry)) {
                    state[entry] = 2;
                    continue;
                }
                state[entry] = 1;
                if (!bindings_loop_push(
                        &stack,
                        (BindingsLoopFrame){.kind = BINDINGS_LOOP_ENTRY_EXIT, .entry = entry}) ||
                    !bindings_loop_push(
                        &stack,
                        (BindingsLoopFrame){.kind = BINDINGS_LOOP_ATOM,
                                     .value = logical[entry].value}))
                    goto representation_failure;
                continue;
            }
            if (atom->kind == ATOM_EXPR) {
                for (CettaExprIndex child = atom->expr.len; child > 0; child--) {
                    BindingValue child_value = frame.value;
                    child_value.skeleton = atom->expr.elems[child - 1u];
                    if (!bindings_loop_push(
                            &stack,
                            (BindingsLoopFrame){
                                .kind = BINDINGS_LOOP_ATOM, .value = child_value}))
                        goto representation_failure;
                }
            }
        }
    }
    if (stack.items != stack.inline_items) free(stack.items);
    if (state != state_stack)
        free(state);
    if (logical != logical_stack)
        free(logical);
    bindings_cycle_memoize_audit(b, BINDINGS_CYCLE_ACYCLIC);
    return false;

loop_found:
    bindings_cycle_memoize_audit(b, BINDINGS_CYCLE_PRESENT);
representation_failure:
    if (stack.items != stack.inline_items) free(stack.items);
    if (state != state_stack) free(state);
    if (logical != logical_stack) free(logical);
    return true;
}

/* ── Type matching ─────────────────────────────────────────────────────── */

static bool is_named_symbol(Atom *atom, const char *name) {
    return atom_is_symbol(atom, name);
}

bool type_match_uses_space_class_bridge(Atom *actual, Atom *expected);

static bool is_space_value_type(Atom *atom) {
    return atom &&
           atom->kind == ATOM_EXPR &&
           atom->expr.len == 2 &&
           is_named_symbol(atom->expr.elems[0], "Space");
}

/* Syntax is borrowed for the duration of a match; lexical interpretation is
 * owned by the value.  The dense frame is a borrowed accelerator, not an
 * authority for context identity or lifetime. */
typedef struct {
    BindingValue value;
    const BindingsActivationView *frame;
    uint32_t first_entry;
} MatchTerm;

static MatchTerm match_term_at(Atom *source, BindingValueKind kind,
                               uint32_t epoch, uint32_t first_entry,
                               const BindingsActivationView *frame) {
    bool contextual = kind != BINDING_VALUE_MATERIALIZED;
    BindingValue value = binding_value_from_context_kind(
        source, epoch, kind);
    return (MatchTerm){
        value,
        contextual ? frame : NULL,
        contextual ? first_entry : 0u,
    };
}

typedef struct {
    MatchTerm left;
    MatchTerm right;
    uint32_t previous;
} MatchPathEntry;

#define MATCH_PATH_INLINE_CAP 16u
#define MATCH_PATH_BUCKET_INLINE_CAP 32u

typedef struct {
    MatchPathEntry inline_entries[MATCH_PATH_INLINE_CAP];
    MatchPathEntry *entries;
    size_t len;
    size_t entry_cap;
    uint32_t *buckets;
    size_t bucket_cap;
} MatchPathSet;

static void match_path_init(MatchPathSet *path) {
    path->entries = path->inline_entries;
    path->len = 0u;
    path->entry_cap = MATCH_PATH_INLINE_CAP;
    path->buckets = NULL;
    path->bucket_cap = 0u;
}

static void match_path_free(MatchPathSet *path) {
    if (path->entries != path->inline_entries) free(path->entries);
    free(path->buckets);
}

static bool match_term_same_occurrence(MatchTerm left, MatchTerm right) {
    if (left.value.skeleton != right.value.skeleton ||
        left.value.kind != right.value.kind)
        return false;
    return left.value.kind == BINDING_VALUE_MATERIALIZED ||
        (left.value.epoch == right.value.epoch &&
         left.first_entry == right.first_entry);
}

static uintptr_t match_term_path_hash(MatchTerm term) {
    uintptr_t hash = (uintptr_t)term.value.skeleton >> 4;
    if (binding_value_is_contextual(term.value)) {
        hash ^= (uintptr_t)term.value.epoch * (uintptr_t)0x85ebca6bu;
        hash ^= (uintptr_t)term.first_entry * (uintptr_t)0xc2b2ae35u;
        hash ^= (uintptr_t)0x27d4eb2fu;
    }
    return hash;
}

static size_t match_path_hash(MatchTerm left, MatchTerm right) {
    uintptr_t x = match_term_path_hash(left);
    uintptr_t y = match_term_path_hash(right);
    x ^= y + (uintptr_t)0x9e3779b9u + (x << 6) + (x >> 2);
    x ^= x >> 17;
    x *= (uintptr_t)0xed5ad4bbu;
    x ^= x >> 11;
    return (size_t)x;
}

static bool match_path_reserve_entries(MatchPathSet *path) {
    if (path->len < path->entry_cap)
        return true;
    if (path->entry_cap > SIZE_MAX / 2u)
        return false;
    size_t new_cap = path->entry_cap * 2u;
    if (new_cap > SIZE_MAX / sizeof(*path->entries))
        return false;
    MatchPathEntry *next = cetta_malloc(
        sizeof(*next) * new_cap);
    memcpy(next, path->entries,
           sizeof(*next) * path->len);
    if (path->entries != path->inline_entries)
        free(path->entries);
    path->entries = next;
    path->entry_cap = new_cap;
    return true;
}

static bool match_path_grow_buckets(MatchPathSet *path) {
    if (path->bucket_cap > SIZE_MAX / 2u)
        return false;
    size_t new_cap = path->bucket_cap
        ? path->bucket_cap * 2u
        : MATCH_PATH_BUCKET_INLINE_CAP;
    if (new_cap > SIZE_MAX / sizeof(*path->buckets) ||
        path->len > UINT32_MAX)
        return false;
    uint32_t *next = cetta_malloc(sizeof(*next) * new_cap);
    memset(next, 0, sizeof(*next) * new_cap);
    size_t mask = new_cap - 1u;
    for (size_t i = 0u; i < path->len; i++) {
        MatchPathEntry *entry = &path->entries[i];
        size_t bucket = match_path_hash(
            entry->left, entry->right) & mask;
        entry->previous = next[bucket];
        next[bucket] = (uint32_t)i + 1u;
    }
    free(path->buckets);
    path->buckets = next;
    path->bucket_cap = new_cap;
    return true;
}

/* Expression pairs a matcher decomposes over a certified-acyclic
 * environment before it starts recording its active path. */
enum { MATCH_UNTRACKED_PAIR_LIMIT = 4096u };

/* Enter and leave are strictly nested by both structural matcher worklists.
 * Store exactly that active ancestry.  Shallow paths use the inline entries
 * directly, avoiding a bucket table that most matches never need.  Deeper
 * paths switch once to the same hashed active set: each bucket head is the
 * newest active entry and each entry links to its predecessor.  Shared finite
 * DAG nodes may therefore re-enter after their sibling path leaves, while a
 * genuine active-path recurrence still fails. */
static bool match_path_enter(MatchPathSet *path, MatchTerm left, MatchTerm right) {
    if (!match_path_reserve_entries(path))
        return false;
    if (!path->buckets) {
        for (size_t i = 0u; i < path->len; i++) {
            MatchPathEntry *entry = &path->entries[i];
            if (match_term_same_occurrence(entry->left, left) &&
                match_term_same_occurrence(entry->right, right)) {
                return false;
            }
        }
        if (path->len < MATCH_PATH_INLINE_CAP) {
            path->entries[path->len++] = (MatchPathEntry){
                .left = left,
                .right = right,
                .previous = 0u,
            };
            return true;
        }
        if (!match_path_grow_buckets(path))
            return false;
    }
    if ((path->len + 1u) * 4u >= path->bucket_cap * 3u &&
        !match_path_grow_buckets(path))
        return false;
    size_t bucket = match_path_hash(left, right) &
        (path->bucket_cap - 1u);
    uint32_t cursor = path->buckets[bucket];
    while (cursor != 0u) {
        MatchPathEntry *entry = &path->entries[cursor - 1u];
        if (match_term_same_occurrence(entry->left, left) &&
            match_term_same_occurrence(entry->right, right))
            return false;
        cursor = entry->previous;
    }
    if (path->len >= UINT32_MAX)
        return false;
    MatchPathEntry *entry = &path->entries[path->len];
    *entry = (MatchPathEntry){
        .left = left,
        .right = right,
        .previous = path->buckets[bucket],
    };
    path->buckets[bucket] = (uint32_t)path->len + 1u;
    path->len++;
    return true;
}

static void match_path_leave(MatchPathSet *path, MatchTerm left, MatchTerm right) {
    assert(path->len > 0u);
    size_t index = path->len - 1u;
    MatchPathEntry *entry = &path->entries[index];
    assert(match_term_same_occurrence(entry->left, left) &&
           match_term_same_occurrence(entry->right, right));
    if (!path->buckets) {
        path->len = index;
        return;
    }
    size_t bucket = match_path_hash(left, right) &
        (path->bucket_cap - 1u);
    assert(path->buckets[bucket] == (uint32_t)index + 1u);
    path->buckets[bucket] = entry->previous;
    path->len = index;
}

typedef enum {
    DECODED_MATCH_PAIR = 0,
    DECODED_MATCH_EXIT = 1,
} DecodedMatchFrameKind;

typedef struct {
    DecodedMatchFrameKind kind;
    MatchTerm left;
    MatchTerm right;
} DecodedMatchPair;

typedef struct {
    DecodedMatchPair *items;
    size_t len;
    size_t cap;
    DecodedMatchPair inline_items[16];
} DecodedMatchWorklist;

static void decoded_match_worklist_init(DecodedMatchWorklist *work) {
    work->items = work->inline_items;
    work->len = 0;
    work->cap = sizeof work->inline_items / sizeof work->inline_items[0];
}

static void decoded_match_worklist_free(DecodedMatchWorklist *work) {
    if (work->items != work->inline_items) free(work->items);
}

static bool decoded_match_push(DecodedMatchWorklist *work,
                               MatchTerm left, MatchTerm right) {
    if (work->len == work->cap) {
        size_t next_cap = work->cap * 2u;
        if (next_cap <= work->cap ||
            next_cap > SIZE_MAX / sizeof(*work->items))
            return false;
        DecodedMatchPair *next = cetta_malloc(
            sizeof(*work->items) * next_cap);
        memcpy(next, work->items, sizeof(*work->items) * work->len);
        if (work->items != work->inline_items) free(work->items);
        work->items = next;
        work->cap = next_cap;
    }
    work->items[work->len++] =
        (DecodedMatchPair){DECODED_MATCH_PAIR, left, right};
    return true;
}

static bool decoded_match_push_exit(DecodedMatchWorklist *work,
                                    MatchTerm left, MatchTerm right) {
    if (!decoded_match_push(work, left, right)) return false;
    work->items[work->len - 1u].kind = DECODED_MATCH_EXIT;
    return true;
}

static bool binding_value_may_contain_unbound_var(Bindings *b, BindingValue value) {
    DecodedMatchWorklist work;
    MatchPathSet path;
    decoded_match_worklist_init(&work);
    match_path_init(&path);
    bool unbound = true;
    MatchTerm root = {.value = value};
    if (!decoded_match_push(&work, root, root))
        goto done;
    while (work.len > 0u) {
        DecodedMatchPair pair = work.items[--work.len];
        if (pair.kind == DECODED_MATCH_EXIT) {
            match_path_leave(&path, pair.left, pair.right);
            continue;
        }
        value = pair.left.value;
        if (!bindings_resolve_value_preview(b, value, &value))
            goto done;
        Atom *atom = value.skeleton;
        if (atom->kind == ATOM_VAR)
            goto done;
        if (atom->kind != ATOM_EXPR || !atom_has_vars(atom))
            continue;
        MatchTerm term = {.value = value};
        /* A cyclic or uninspectable dependency cannot certify groundness. */
        if (!match_path_enter(&path, term, term) ||
            !decoded_match_push_exit(&work, term, term))
            goto done;
        for (CettaExprIndex i = atom->expr.len; i > 0u; i--) {
            BindingValue child = value;
            child.skeleton = atom->expr.elems[i - 1u];
            MatchTerm child_term = {.value = child};
            if (!decoded_match_push(&work, child_term, child_term))
                goto done;
        }
    }
    unbound = false;
done:
    decoded_match_worklist_free(&work);
    match_path_free(&path);
    return unbound;
}

static bool binding_values_eq_under_bindings(Bindings *b, BindingValue lhs, BindingValue rhs) {
    DecodedMatchWorklist work;
    MatchPathSet path;
    decoded_match_worklist_init(&work);
    match_path_init(&path);
    bool equal = false;
    if (!decoded_match_push(&work, (MatchTerm){.value = lhs}, (MatchTerm){.value = rhs}))
        goto done;
    while (work.len > 0u) {
        DecodedMatchPair pair = work.items[--work.len];
        if (pair.kind == DECODED_MATCH_EXIT) {
            match_path_leave(&path, pair.left, pair.right);
            continue;
        }
        lhs = pair.left.value;
        rhs = pair.right.value;
        if (!bindings_resolve_value_preview(b, lhs, &lhs) ||
            !bindings_resolve_value_preview(b, rhs, &rhs))
            goto done;
        Atom *left = lhs.skeleton, *right = rhs.skeleton;
        if (left->kind != right->kind)
            goto done;
        if (left->kind == ATOM_VAR) {
            if (binding_value_variable_id(lhs) != binding_value_variable_id(rhs))
                goto done;
            continue;
        }
        if (left->kind != ATOM_EXPR) {
            if (!atom_eq(left, right))
                goto done;
            continue;
        }
        if (left == right &&
            match_shared_occurrence_same_environment(
                binding_value_is_contextual(lhs),
                binding_value_is_contextual(rhs), lhs.epoch, rhs.epoch))
            continue;
        if (!atom_has_vars(left) && !atom_has_vars(right)) {
            if (!atom_eq(left, right))
                goto done;
            continue;
        }
        if (left->expr.len != right->expr.len)
            goto done;
        MatchTerm left_term = {.value = lhs}, right_term = {.value = rhs};
        if (!match_path_enter(&path, left_term, right_term) ||
            !decoded_match_push_exit(&work, left_term, right_term))
            goto done;
        for (CettaExprIndex i = left->expr.len; i > 0u; i--) {
            BindingValue left_child = lhs, right_child = rhs;
            left_child.skeleton = left->expr.elems[i - 1u];
            right_child.skeleton = right->expr.elems[i - 1u];
            if (!decoded_match_push(&work, (MatchTerm){.value = left_child},
                                    (MatchTerm){.value = right_child}))
                goto done;
        }
    }
    equal = true;
done:
    decoded_match_worklist_free(&work);
    match_path_free(&path);
    return equal;
}

static size_t bindings_dereference_limit(const Bindings *bindings) {
    size_t len = bindings ? (size_t)bindings->len : 0u;
    const BindingsFrameIndex *frames = bindings
        ? bindings->frame_index : NULL;
    size_t frame_slots = frames
        ? (size_t)frames->len * (size_t)frames->max_schema_len : 0u;
    if (frames && frames->max_schema_len != 0u &&
        frame_slots / (size_t)frames->max_schema_len != frames->len) {
        frame_slots = SIZE_MAX;
    }
    len = frame_slots > SIZE_MAX - len ? SIZE_MAX : len + frame_slots;
    return len > (SIZE_MAX - 2u) / 2u ? SIZE_MAX : len * 2u + 2u;
}

/* Upstream HE treats each nested %Undefined% as an independent wildcard.
   Type matching itself is a finite structural walk, so nesting depth is not a
   semantic budget. The optional builder selects transactional binding without
   changing the relation. */
/* Count matcher invocations, including nested comparisons of bound values.
 * These are not equation candidates: a candidate can invoke several matchers.
 * Failure categories partition the counted invocations; an after-bind failure
 * is still a matching failure, not evidence that an equation body was run. */
typedef struct {
    uint32_t row_len;
    uint32_t staged_len;
    uint64_t frame_write_clock;
} MatchWriteStamp;

static MatchWriteStamp match_write_stamp(
        const Bindings *bindings, uint32_t staged_len) {
    return (MatchWriteStamp){
        .row_len = bindings ? bindings->len : 0u,
        .staged_len = staged_len,
        .frame_write_clock = bindings && bindings->frame_index
            ? bindings->frame_index->write_clock : 0u,
    };
}

static bool match_write_stamp_changed(
        MatchWriteStamp start, MatchWriteStamp end) {
    return start.row_len != end.row_len ||
        start.staged_len != end.staged_len ||
        start.frame_write_clock != end.frame_write_clock;
}

static void match_note_unification_attempt(
        bool success, bool wrote_binding, bool repeated_var_fail) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT);
    if (success) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT_SUCCESS);
        return;
    }
    if (repeated_var_fail) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT_REPEATED_VAR_FAIL);
        return;
    }
    if (!wrote_binding) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT_HEAD_FAIL);
    } else {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_UNIFICATION_ATTEMPT_AFTER_BIND_FAIL);
    }
}

/* Persistent open head against a goal skeleton whose variables are the
 * current environment (SubstitutionAlgebra closures).  Callers must not
 * force a substituted instance first; observation is bindings_apply. */
static bool match_decoded_atoms_worklist(BindingValue left_value, BindingValue right_value,
                                         Bindings *bindings,
                                         BindingsBuilder *builder,
                                         bool undefined_is_wildcard) {
    Atom *left, *right;
    DecodedMatchWorklist work;
    MatchPathSet path;
    Bindings *initial = builder ? &builder->current : bindings;
    MatchWriteStamp attempt_start = match_write_stamp(initial, 0u);
    bool attempt_repeated_var = false;
    bool attempt_resolving_bound = false;
    /* As in the epoch-view matcher: record the active path from the start
     * only when the environment is not certified acyclic, and otherwise once
     * the walk outgrows a finite match of ordinary size. */
    bool track_path = !initial ||
        initial->cycle_state != BINDINGS_CYCLE_ACYCLIC;
    uint32_t untracked_pairs = 0u;
    decoded_match_worklist_init(&work);
    match_path_init(&path);
    if (!decoded_match_push(&work, (MatchTerm){.value = left_value},
                            (MatchTerm){.value = right_value})) goto fail;

    while (work.len > 0) {
        DecodedMatchPair pair = work.items[--work.len];
        if (pair.kind == DECODED_MATCH_EXIT) {
            match_path_leave(&path, pair.left, pair.right);
            continue;
        }
        left_value = pair.left.value;
        right_value = pair.right.value;
        attempt_resolving_bound = false;
        Bindings *current = builder ? &builder->current : bindings;
        /* A variable-only dereference chain cannot be longer than the binding
           table unless it contains a cycle. This is cycle detection derived
           from the graph in hand, not an arbitrary depth cutoff. */
        size_t dereferences = 0;
        size_t dereference_limit = bindings_dereference_limit(current);

retry_pair:
        left = left_value.skeleton;
        right = right_value.skeleton;
        if (undefined_is_wildcard &&
            (atom_is_symbol_id(left, g_builtin_syms.undefined_type) ||
             atom_is_symbol_id(right, g_builtin_syms.undefined_type)))
            continue;
        if (left == right) {
            if (left && left->kind == ATOM_VAR &&
                binding_value_variable_id(left_value) == binding_value_variable_id(right_value) &&
                (!current || (current->cycle_state == BINDINGS_CYCLE_ACYCLIC)))
                continue;
            if (match_shared_ground_reflexivity_try(
                    left, right, true,
                    match_shared_occurrence_same_environment(
                        binding_value_is_contextual(left_value),
                        binding_value_is_contextual(right_value),
                        left_value.epoch, right_value.epoch), current))
                continue;
        }
        bool closed_equal = false;
        if (!undefined_is_wildcard &&
            match_closed_expression_decision_try(
                left, right, &closed_equal)) {
            if (!closed_equal)
                goto fail;
            continue;
        }
        if (!undefined_is_wildcard &&
            match_finite_ground_equal_try(left, right, &closed_equal)) {
            if (!closed_equal)
                goto fail;
            continue;
        }
        if (left->kind == ATOM_VAR) {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_MATCH_DECODED_LOOKUP);
            BindingValue existing = bindings_lookup_value(
                current, left_value);
            if (existing.skeleton) {
                if (++dereferences > dereference_limit) {
                    goto fail;
                }
                attempt_resolving_bound = true;
                left_value = existing;
                goto retry_pair;
            }
            if (right->kind == ATOM_VAR) {
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_MATCH_DECODED_LOOKUP);
                BindingValue right_existing = bindings_lookup_value(
                    current, right_value);
                if (right_existing.skeleton) {
                    if (++dereferences > dereference_limit) {
                        goto fail;
                    }
                    right_value = right_existing;
                    goto retry_pair;
                }
                if (binding_value_variable_id(left_value) ==
                    binding_value_variable_id(right_value)) continue;
            }
            bool added = builder
                ? bindings_builder_add_id_internal(
                      builder, binding_value_variable_id(left_value),
                      left->sym_id, left->name_key, right_value)
                : bindings_add_internal(bindings, binding_value_variable_id(left_value),
                      left->sym_id, left->name_key, right_value, true);
            if (!added) {
                goto fail;
            }
            continue;
        }
        if (right->kind == ATOM_VAR) {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_MATCH_DECODED_LOOKUP);
            BindingValue existing = bindings_lookup_value(
                current, right_value);
            if (existing.skeleton) {
                if (++dereferences > dereference_limit) {
                    goto fail;
                }
                attempt_resolving_bound = true;
                right_value = existing;
                goto retry_pair;
            }
            bool added = builder
                ? bindings_builder_add_id_internal(
                      builder, binding_value_variable_id(right_value),
                      right->sym_id, right->name_key, left_value)
                : bindings_add_internal(bindings, binding_value_variable_id(right_value),
                      right->sym_id, right->name_key, left_value, true);
            if (!added) {
                goto fail;
            }
            continue;
        }
        if (left->kind == ATOM_SYMBOL && right->kind == ATOM_SYMBOL) {
            if (left->sym_id != right->sym_id) {
                if (attempt_resolving_bound)
                    attempt_repeated_var = true;
                goto fail;
            }
            continue;
        }
        if (left->kind == ATOM_GROUNDED && right->kind == ATOM_GROUNDED) {
            if (!atom_eq(left, right)) {
                if (attempt_resolving_bound)
                    attempt_repeated_var = true;
                goto fail;
            }
            continue;
        }
        if (left->kind != ATOM_EXPR || right->kind != ATOM_EXPR ||
            left->expr.len != right->expr.len) {
            if (attempt_resolving_bound)
                attempt_repeated_var = true;
            goto fail;
        }
        if (!track_path &&
            ++untracked_pairs > MATCH_UNTRACKED_PAIR_LIMIT)
            track_path = true;
        if (track_path &&
            (!match_path_enter(&path, (MatchTerm){.value = left_value},
                               (MatchTerm){.value = right_value}) ||
             !decoded_match_push_exit(&work, (MatchTerm){.value = left_value},
                                      (MatchTerm){.value = right_value})))
            goto fail;
        /* Push in reverse so binding effects retain the recursive
           implementation's left-to-right traversal order. */
        for (CettaExprIndex i = left->expr.len; i > 1u; i--) {
            CettaExprIndex child = i - 1u;
            BindingValue left_child = left_value, right_child = right_value;
            left_child.skeleton = left->expr.elems[child];
            right_child.skeleton = right->expr.elems[child];
            if (!decoded_match_push(&work,
                    (MatchTerm){.value = left_child},
                    (MatchTerm){.value = right_child})) {
                goto fail;
            }
        }
        /* The first child is next in the reference LIFO traversal. Reuse
         * this frame; leave siblings and the parent's exit marker queued.
         * No matching test, binding, or failure is moved past another. */
        if (left->expr.len != 0u) {
            Atom *next_left = left->expr.elems[0];
            Atom *next_right = right->expr.elems[0];
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_MATCH_WORKLIST_FRAME_ELIDED);
            left_value.skeleton = next_left;
            right_value.skeleton = next_right;
            dereferences = 0u;
            dereference_limit = bindings_dereference_limit(current);
            goto retry_pair;
        }
    }
    decoded_match_worklist_free(&work);
    match_path_free(&path);
    match_note_unification_attempt(
        true, match_write_stamp_changed(
                  attempt_start,
                  match_write_stamp(
                      builder ? &builder->current : bindings, 0u)),
        false);
    return true;

fail:
    decoded_match_worklist_free(&work);
    match_path_free(&path);
    match_note_unification_attempt(
        false, match_write_stamp_changed(
                   attempt_start,
                   match_write_stamp(
                       builder ? &builder->current : bindings, 0u)),
        attempt_repeated_var);
    return false;
}

/* The public SpaceType name and a concrete (Space discipline) value type
 * inhabit one runtime space class.  Negative type decisions must consult
 * this positive relation: it is not derivable from structural consistency
 * alone. */
bool type_match_uses_space_class_bridge(Atom *actual, Atom *expected) {
    return
        (is_named_symbol(actual, "SpaceType") &&
         is_space_value_type(expected)) ||
        (is_named_symbol(expected, "SpaceType") &&
         is_space_value_type(actual));
}

bool match_types(Atom *actual, Atom *expected, Bindings *b) {
    /* Atom is the expected-side value top. An actual Atom is not evidence for
       an arbitrary concrete expected type. */
    if (atom_is_symbol_id(expected, g_builtin_syms.atom)) return true;
    if (type_match_uses_space_class_bridge(actual, expected)) {
        return true;
    }
    return match_decoded_atoms_worklist(
        binding_value_from_atom(actual), binding_value_from_atom(expected), b, NULL, true);
}

bool match_types_builder(Atom *actual, Atom *expected, BindingsBuilder *bb) {
    if (atom_is_symbol_id(expected, g_builtin_syms.atom)) return true;
    if (type_match_uses_space_class_bridge(actual, expected)) {
        return true;
    }
    return match_decoded_atoms_worklist(
        binding_value_from_atom(actual), binding_value_from_atom(expected), NULL, bb, true);
}

/* ── Bidirectional matching (match_atoms from HE spec metta.md:577-617) ── */

static bool match_atoms_epoch_worklist(Atom *left, Atom *right,
                                       Bindings *bindings,
                                       BindingsBuilder *builder,
                                       Arena *a, uint32_t epoch);
static bool match_atoms_epoch_rule_local_worklist(
    Atom *left, Atom *right, BindingsBuilder *builder,
    Arena *a, uint32_t epoch);
static bool match_atoms_epoch_rule_local_planned_worklist(
    Atom *left, Atom *right, const CettaOpenPatternPlan *right_plan,
    BindingsBuilder *builder, Arena *a, uint32_t epoch);
static bool match_atoms_epoch_view_worklist(
    Atom *left, uint32_t left_epoch, uint32_t left_first_entry,
    Atom *right, Bindings *bindings, BindingsBuilder *builder,
    Arena *a, uint32_t right_epoch);
static bool match_atoms_epoch_view_current_worklist(
    Atom *left, uint32_t left_epoch, uint32_t left_first_entry,
    Atom *right, Bindings *bindings, BindingsBuilder *builder,
    Arena *a);
static bool match_atoms_epoch_view_rule_local_worklist(
    Atom *left, uint32_t left_epoch, uint32_t left_first_entry,
    Atom *right, BindingsBuilder *builder, Arena *a, uint32_t right_epoch);
static bool match_atoms_epoch_view_rule_local_planned_worklist(
    Atom *left, uint32_t left_epoch, uint32_t left_first_entry,
    Atom *right, const CettaOpenPatternPlan *right_plan,
    BindingsBuilder *builder, Arena *a, uint32_t right_epoch);
static bool match_atoms_activation_view_worklist(
    Atom *left, const BindingsActivationView *left_frame,
    Atom *right, Bindings *bindings, BindingsBuilder *builder,
    Arena *a, uint32_t right_epoch, bool right_original,
    bool prefer_right_rule_slot,
    const CettaOpenPatternPlan *right_plan);
static bool match_atoms_atom_id_epoch_worklist(
    BindingValue left_value, const TermUniverse *candidate_universe, AtomId right_id,
    Bindings *b, Arena *a, uint32_t epoch);
static bool match_atoms_epoch_views_linear(
    Atom *left, BindingValueKind left_kind, uint32_t left_epoch,
    uint32_t left_first_entry, Atom *right,
    Bindings *bindings, BindingsBuilder *builder,
    Arena *a, uint32_t right_epoch, BindingValueKind right_kind,
    bool prefer_right_rule_slot,
    const BindingsActivationView *left_frame,
    const CettaOpenPatternPlan *right_plan,
    BindingsExclusiveFrame *exclusive);

bool match_binding_values(BindingValue left, BindingValue right, Bindings *b) {
    return left.skeleton && right.skeleton &&
        match_decoded_atoms_worklist(left, right, b, NULL, false);
}

bool match_atoms(Atom *left, Atom *right, Bindings *b) {
    return match_binding_values(binding_value_from_atom(left), binding_value_from_atom(right), b);
}

bool match_binding_values_builder(BindingValue left, BindingValue right, BindingsBuilder *bb) {
    return left.skeleton && right.skeleton && bb &&
        match_decoded_atoms_worklist(left, right, NULL, bb, false);
}

bool match_atoms_builder(Atom *left, Atom *right, BindingsBuilder *bb) {
    return match_binding_values_builder(binding_value_from_atom(left), binding_value_from_atom(right), bb);
}

bool match_atoms_builder_with_attempt_frame(
        Atom *left, Atom *right, BindingsBuilder *bb,
        const BindingsActivationView *left_frame) {
    (void)left_frame;
    return match_atoms_builder(left, right, bb);
}

/* Leaf-patch view: OFF by default (env CETTA_LEAF_PATCH_VIEW=1 opts in).  When
 * on, an eligible reduction uses the positional bind below instead of the
 * general matcher, so an OFF-vs-ON differential can prove byte-identity across
 * the full suite before any default flip. */
bool match_leaf_patch_view_enabled(void) {
    static _Thread_local int cached = -1;
    if (cached < 0) {
        const char *v = getenv("CETTA_LEAF_PATCH_VIEW");
        cached = (v && v[0] == '1') ? 1 : 0;
    }
    return cached == 1;
}

/* Positional leaf-patch match: a FLAT LINEAR pattern (lhs = head + distinct
 * variable args, guaranteed linear by the eligibility guard) against a query
 * (head + NON-variable args) reduces to the epoch matcher's right-var binding
 * per position -- epoch_var_atom + bindings_add_var -- with no worklist.  This
 * is licensed by LeafPatchViewKernel.matchP_complete_linear (positional read ==
 * matcher on linear patterns).  Conservative: the whole shape is pre-checked
 * before any binding, and the bindings are built transactionally, so returning
 * false (caller falls back to the general matcher) never leaves a partial
 * binding in b.  Anything outside the shape (nesting, a non-var pattern arg, a
 * variable query arg, or a pre-bound epoched var) falls back. */
bool match_atoms_epoch_positional_linear(Atom *query, Atom *lhs, Bindings *b,
                                         Arena *a, uint32_t epoch) {
    if (!query || !lhs || !b || !a)
        return false;
    if (query->kind != ATOM_EXPR || lhs->kind != ATOM_EXPR)
        return false;
    if (lhs->expr.len == 0 || query->expr.len != lhs->expr.len)
        return false;
    Atom *lh = lhs->expr.elems[0];
    Atom *qh = query->expr.elems[0];
    if (!lh || !qh || lh->kind != ATOM_SYMBOL || qh->kind != ATOM_SYMBOL ||
        lh->sym_id != qh->sym_id)
        return false;
    /* Pre-check the whole shape AND linearity before binding anything, so a
     * refusal never leaves a partial binding.  A repeated epoched var here means
     * the pattern is non-linear (an equality constraint the positional bind
     * cannot honour) -- refuse and let the general matcher enforce it. */
    VarId seen[16];
    uint32_t nseen = 0;
    for (CettaExprIndex i = 1; i < lhs->expr.len; i++) {
        Atom *pi = lhs->expr.elems[i];
        Atom *qi = query->expr.elems[i];
        if (!pi || !qi || pi->kind != ATOM_VAR || qi->kind == ATOM_VAR)
            return false;
        VarId eid = var_epoch_id(pi->var_id, epoch);
        if (bindings_lookup_value_id(b, eid).skeleton)
            return false; /* epoched var already bound -> matcher dereferences */
        for (uint32_t j = 0; j < nseen; j++)
            if (seen[j] == eid)
                return false; /* repeated variable -> non-linear -> refuse */
        if (nseen >= (sizeof seen / sizeof seen[0]))
            return false; /* arity beyond the small cap -> conservative refuse */
        seen[nseen++] = eid;
    }
    Bindings trial;
    if (!bindings_clone(&trial, b))
        return false;
    for (CettaExprIndex i = 1; i < lhs->expr.len; i++) {
        Atom *pi = lhs->expr.elems[i];
        Atom *qi = query->expr.elems[i];
        Atom *binding_var = epoch_var_atom(a, pi, epoch);
        if (!binding_var || !bindings_add_var(&trial, binding_var, qi)) {
            bindings_free(&trial);
            return false;
        }
    }
    bindings_replace(b, &trial);
    return true;
}

bool match_atoms_epoch_positional_linear_builder(
        Atom *query, Atom *lhs, BindingsBuilder *bb,
        Arena *a, uint32_t epoch) {
    if (!query || !lhs || !bb || !a)
        return false;
    if (query->kind != ATOM_EXPR || lhs->kind != ATOM_EXPR)
        return false;
    if (lhs->expr.len == 0u || query->expr.len != lhs->expr.len)
        return false;
    Atom *lh = lhs->expr.elems[0];
    Atom *qh = query->expr.elems[0];
    if (!lh || !qh || lh->kind != ATOM_SYMBOL || qh->kind != ATOM_SYMBOL ||
        lh->sym_id != qh->sym_id)
        return false;

    /* This realization is deliberately narrower than head-linearity alone.
     * It is the flat, ground-argument leaf view proved equivalent to matching;
     * the generated flow fact selects candidates, while these checks keep the
     * runtime generic and fail closed outside the theorem's domain. */
    VarId seen[16];
    uint32_t nseen = 0u;
    Bindings *current = &bb->current;
    if (current->eq_len != 0u)
        return false;
    for (CettaExprIndex i = 1u; i < lhs->expr.len; i++) {
        Atom *pi = lhs->expr.elems[i];
        Atom *qi = query->expr.elems[i];
        if (!pi || !qi || pi->kind != ATOM_VAR || atom_has_vars(qi))
            return false;
        VarId eid = var_epoch_id(pi->var_id, epoch);
        if (bindings_lookup_value_id(current, eid).skeleton)
            return false;
        for (uint32_t j = 0u; j < nseen; j++)
            if (seen[j] == eid)
                return false;
        if (nseen >= (sizeof seen / sizeof seen[0]))
            return false;
        seen[nseen++] = eid;
    }

    for (CettaExprIndex i = 1u; i < lhs->expr.len; i++) {
        Atom *binding_var = epoch_var_atom(a, lhs->expr.elems[i], epoch);
        if (!binding_var || !bindings_builder_add_var_fresh(
                bb, binding_var, query->expr.elems[i]))
            return false;
    }
    return true;
}

bool match_atoms_epoch(Atom *left, Atom *right, Bindings *b, Arena *a, uint32_t epoch) {
    return match_atoms_epoch_worklist(left, right, b, NULL, a, epoch);
}

bool match_atoms_epoch_builder(Atom *left, Atom *right,
                               BindingsBuilder *bb, Arena *a,
                               uint32_t epoch) {
    return match_atoms_epoch_worklist(left, right, NULL, bb, a, epoch);
}

bool match_atoms_epoch_builder_rule_local(
        Atom *left, Atom *right, BindingsBuilder *bb,
        Arena *a, uint32_t epoch) {
    return match_atoms_epoch_rule_local_worklist(
        left, right, bb, a, epoch);
}

bool match_atoms_epoch_builder_rule_local_planned(
        Atom *left, Atom *right,
        const CettaOpenPatternPlan *right_plan,
        BindingsBuilder *bb, Arena *a, uint32_t epoch) {
    return match_atoms_epoch_rule_local_planned_worklist(
        left, right, right_plan, bb, a, epoch);
}

bool match_atoms_epoch_builder_rule_local_linear(
        Atom *left, Atom *right,
        const CettaOpenPatternPlan *right_plan,
        BindingsBuilder *bb, Arena *a, uint32_t epoch) {
    return match_atoms_epoch_views_linear(
        left, BINDING_VALUE_MATERIALIZED, 0u, 0u,
        right, NULL, bb, a, epoch,
        BINDING_VALUE_CONTEXTUAL, true, NULL, right_plan, NULL);
}

bool match_atoms_epoch_view_builder(
        Atom *left_original, uint32_t left_epoch,
        uint32_t left_first_entry, Atom *right_original,
        BindingsBuilder *bb, Arena *a, uint32_t right_epoch) {
    return match_atoms_epoch_view_worklist(
        left_original, left_epoch, left_first_entry,
        right_original, NULL, bb, a, right_epoch);
}

bool match_atoms_epoch_view_builder_current(
        Atom *left_original, uint32_t left_epoch,
        uint32_t left_first_entry, Atom *right,
        BindingsBuilder *bb, Arena *a) {
    return match_atoms_epoch_view_current_worklist(
        left_original, left_epoch, left_first_entry,
        right, NULL, bb, a);
}

bool match_atoms_epoch_view_builder_rule_local(
        Atom *left_original, uint32_t left_epoch,
        uint32_t left_first_entry, Atom *right_original,
        BindingsBuilder *bb, Arena *a, uint32_t right_epoch) {
    return match_atoms_epoch_view_rule_local_worklist(
        left_original, left_epoch, left_first_entry,
        right_original, bb, a, right_epoch);
}

bool match_atoms_epoch_view_builder_rule_local_planned(
        Atom *left_original, uint32_t left_epoch,
        uint32_t left_first_entry, Atom *right_original,
        const CettaOpenPatternPlan *right_plan,
        BindingsBuilder *bb, Arena *a, uint32_t right_epoch) {
    return match_atoms_epoch_view_rule_local_planned_worklist(
        left_original, left_epoch, left_first_entry,
        right_original, right_plan, bb, a, right_epoch);
}

bool match_atoms_epoch_view_builder_rule_local_linear(
        Atom *left_original, uint32_t left_epoch,
        uint32_t left_first_entry, Atom *right_original,
        const CettaOpenPatternPlan *right_plan,
        BindingsBuilder *bb, Arena *a, uint32_t right_epoch) {
    return match_atoms_epoch_views_linear(
        left_original, BINDING_VALUE_CONTEXTUAL,
        left_epoch, left_first_entry,
        right_original, NULL, bb, a, right_epoch,
        BINDING_VALUE_CONTEXTUAL, true, NULL, right_plan, NULL);
}

bool match_atoms_activation_view_builder(
        Atom *left_original, const BindingsActivationView *left_frame,
        Atom *right_original, BindingsBuilder *bb, Arena *a,
        uint32_t right_epoch) {
    return match_atoms_activation_view_worklist(
        left_original, left_frame, right_original,
        NULL, bb, a, right_epoch, true, false, NULL);
}

bool match_atoms_activation_view_builder_current(
        Atom *left_original, const BindingsActivationView *left_frame,
        Atom *right, BindingsBuilder *bb, Arena *a) {
    return match_atoms_activation_view_worklist(
        left_original, left_frame, right,
        NULL, bb, a, 0u, false, false, NULL);
}

bool match_atoms_activation_view_builder_rule_local(
        Atom *left_original, const BindingsActivationView *left_frame,
        Atom *right_original, BindingsBuilder *bb, Arena *a,
        uint32_t right_epoch) {
    return match_atoms_activation_view_worklist(
        left_original, left_frame, right_original,
        NULL, bb, a, right_epoch, true, true, NULL);
}

bool match_atoms_activation_view_builder_rule_local_planned(
        Atom *left_original,
        const BindingsActivationView *left_frame,
        Atom *right_original,
        const CettaOpenPatternPlan *right_plan,
        BindingsBuilder *bb, Arena *a, uint32_t right_epoch) {
    return match_atoms_activation_view_worklist(
        left_original, left_frame, right_original,
        NULL, bb, a, right_epoch, true, true, right_plan);
}

bool match_atoms_activation_view_builder_rule_local_linear(
        Atom *left_original,
        const BindingsActivationView *left_frame,
        Atom *right_original,
        const CettaOpenPatternPlan *right_plan,
        BindingsBuilder *bb, Arena *a, uint32_t right_epoch) {
    if (!bindings_activation_view_available(left_frame, &bb->current))
        return false;
    return match_atoms_epoch_views_linear(
        left_original, left_frame->source_kind, left_frame->epoch,
        left_frame->first_entry, right_original,
        NULL, bb, a, right_epoch,
        BINDING_VALUE_CONTEXTUAL, true,
        left_frame, right_plan, NULL);
}

bool match_atoms_atom_id_epoch(Atom *left, const TermUniverse *candidate_universe,
                               AtomId right_id, Bindings *b, Arena *a,
                               uint32_t epoch) {
    return match_binding_value_atom_id_epoch(
        binding_value_from_atom(left), candidate_universe, right_id, b, a, epoch);
}

bool match_binding_value_atom_id_epoch(
        BindingValue left, const TermUniverse *candidate_universe,
        AtomId right_id, Bindings *b, Arena *a, uint32_t epoch) {
    return match_atoms_atom_id_epoch_worklist(left, candidate_universe, right_id, b, a, epoch);
}

typedef struct {
    VarId left;
    VarId right;
} AlphaPair;

typedef struct {
    AlphaPair *items;
    uint32_t len;
    uint32_t cap;
} AlphaPairSet;

static void alpha_pair_set_init(AlphaPairSet *pairs) {
    pairs->items = NULL;
    pairs->len = 0;
    pairs->cap = 0;
}

static void alpha_pair_set_free(AlphaPairSet *pairs) {
    free(pairs->items);
    pairs->items = NULL;
    pairs->len = 0;
    pairs->cap = 0;
}

static VarId alpha_lookup_left(const AlphaPairSet *pairs, VarId left) {
    for (uint32_t i = 0; i < pairs->len; i++) {
        if (pairs->items[i].left == left)
            return pairs->items[i].right;
    }
    return VAR_ID_NONE;
}

static VarId alpha_lookup_right(const AlphaPairSet *pairs, VarId right) {
    for (uint32_t i = 0; i < pairs->len; i++) {
        if (pairs->items[i].right == right)
            return pairs->items[i].left;
    }
    return VAR_ID_NONE;
}

static bool alpha_add_pair(AlphaPairSet *pairs, VarId left, VarId right) {
    if (pairs->len >= pairs->cap) {
        pairs->cap = pairs->cap ? pairs->cap * 2 : 8;
        pairs->items = cetta_realloc(pairs->items, sizeof(AlphaPair) * pairs->cap);
    }
    pairs->items[pairs->len].left = left;
    pairs->items[pairs->len].right = right;
    pairs->len++;
    return true;
}

static bool atom_alpha_eq_rec(Atom *left, Atom *right, AlphaPairSet *pairs) {
    if (left->kind == ATOM_VAR || right->kind == ATOM_VAR) {
        if (left->kind != ATOM_VAR || right->kind != ATOM_VAR)
            return false;
        VarId mapped_right = alpha_lookup_left(pairs, left->var_id);
        VarId mapped_left = alpha_lookup_right(pairs, right->var_id);
        if (mapped_right || mapped_left) {
            return mapped_right == right->var_id && mapped_left == left->var_id;
        }
        return alpha_add_pair(pairs, left->var_id, right->var_id);
    }

    if (left->kind != right->kind)
        return false;

    switch (left->kind) {
    case ATOM_SYMBOL:
        return left->sym_id == right->sym_id;
    case ATOM_GROUNDED:
        return atom_eq(left, right);
    case ATOM_EXPR:
        if (left->expr.len != right->expr.len)
            return false;
        for (CettaExprIndex i = 0; i < left->expr.len; i++) {
            if (!atom_alpha_eq_rec(left->expr.elems[i], right->expr.elems[i], pairs))
                return false;
        }
        return true;
    case ATOM_VAR:
        return false;
    }
    return false;
}

bool atom_alpha_eq(Atom *left, Atom *right) {
    AlphaPairSet pairs;
    alpha_pair_set_init(&pairs);
    bool ok = atom_alpha_eq_rec(left, right, &pairs);
    alpha_pair_set_free(&pairs);
    return ok;
}

/* One pending comparison.  An entry with child_count > 0 stands for the
 * children [next_child, child_count) of the expression pair it holds, which
 * are produced one at a time in order rather than copied onto the stack
 * individually.  An exit entry closes the pair's path after its children. */
typedef struct {
    bool exit;
    MatchTerm left;
    MatchTerm right;
    const CettaOpenPatternPlan *right_plan;
    CettaExprIndex next_child;
    CettaExprIndex child_count;
} EpochMatchPair;

typedef struct {
    EpochMatchPair *items;
    size_t len;
    size_t cap;
    EpochMatchPair inline_items[16];
} EpochMatchWorklist;


/* Exclusive borrow of one rule frame's authoritative slots.  The plan's
 * singleton support bit is the slot, while `values` is the branch-local
 * version owned by Bindings.  No values are copied into this region. */
static inline void rule_frame_region_init_exclusive(
        RuleFrameRegion *view,
        const CettaOpenPatternPlan *root,
        BindingsExclusiveFrame *exclusive) {
    memset(view, 0, sizeof(*view));
    view->eligible = root && root->variable_ids
        ? root->variable_mask : 0u;
    view->variable_ids = root ? root->variable_ids : NULL;
    view->exclusive = exclusive;
    if (exclusive && exclusive->active) {
        view->values = exclusive->values;
        view->value_count = exclusive->schema->len;
        view->epoch = exclusive->epoch;
        view->frame_admitted = true;
    }
}

static inline void rule_frame_region_init(
        RuleFrameRegion *view,
        const CettaOpenPatternPlan *root) {
    rule_frame_region_init_exclusive(view, root, NULL);
}

static bool rule_frame_region_borrow(
        RuleFrameRegion *view, BindingsBuilder *builder,
        uint32_t epoch) {
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_MATCH_RULE_FRAME_REGION_BORROW_ATTEMPT);
    if (view && view->exclusive && view->exclusive->active) {
        if (view->exclusive->epoch != epoch)
            return false;
        view->values = view->exclusive->values;
        view->value_count = view->exclusive->schema->len;
        view->epoch = epoch;
        view->authority_ref = (BindingsFrameRef){0};
        view->frame_admitted = true;
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_MATCH_RULE_FRAME_REGION_BORROW_DIRECT);
        return true;
    }
    if (!view || !builder || epoch == 0u ||
        !builder->current.frame_index) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_MATCH_RULE_FRAME_REGION_BORROW_GENERAL);
        return false;
    }
    BindingsFrameIndexEntry *frame = bindings_frame_index_find_frame(
        builder->current.frame_index, epoch);
    if (!frame || !frame->schema_complete ||
        frame->schema->len > 64u ||
        frame->schema->source_ids != view->variable_ids ||
        atomic_load_explicit(
            &builder->current.frame_index->references,
            memory_order_acquire) != 1u ||
        !frame->storage_references ||
        atomic_load_explicit(
            frame->storage_references, memory_order_acquire) != 1u) {
        /* Partial or shared frames use the authoritative general lookup.
         * An exclusive complete frame cannot extend, so its storage address
         * remains stable for the lifetime of this match region. */
        view->values = NULL;
        view->value_count = 0u;
        view->epoch = epoch;
        view->authority_ref = bindings_frame_ref_from_entry(
            builder->current.frame_index, frame);
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_MATCH_RULE_FRAME_REGION_BORROW_GENERAL);
        return true;
    }
    view->values = frame->values;
    view->value_count = frame->schema->len;
    view->epoch = epoch;
    view->authority_ref = bindings_frame_ref_from_entry(
        builder->current.frame_index, frame);
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_MATCH_RULE_FRAME_REGION_BORROW_DIRECT);
    return true;
}

static bool rule_frame_region_admit_inventory(
        RuleFrameRegion *view, BindingsBuilder *builder,
        uint32_t epoch) {
    if (!view || !builder || epoch == 0u ||
        !view->variable_ids || view->eligible == 0u) {
        return false;
    }
    if (view->exclusive && view->exclusive->active)
        return rule_frame_region_borrow(view, builder, epoch);
    if (view->frame_admitted)
        return true;
    if (bindings_frame_index_epoch_complete(
            &builder->current, epoch)) {
        view->frame_admitted = true;
        return rule_frame_region_borrow(view, builder, epoch);
    }
    VarId source_ids[64];
    uint32_t source_len = 0u;
    uint64_t remaining = view->eligible;
    while (remaining != 0u) {
        uint32_t slot = (uint32_t)__builtin_ctzll(remaining);
        source_ids[source_len++] = view->variable_ids[slot];
        remaining &= remaining - 1u;
    }
    if (!bindings_builder_register_frame(
            builder, source_ids, source_len, epoch)) {
        return false;
    }
    view->frame_admitted = true;
    return rule_frame_region_borrow(view, builder, epoch);
}

static bool rule_frame_region_admit_source(
        RuleFrameRegion *view, BindingsBuilder *builder,
        Atom *source, uint32_t epoch) {
    if (!builder || !source || epoch == 0u)
        return false;
    if (!atom_has_vars(source))
        return true;
    if (view && view->exclusive && view->exclusive->active)
        return rule_frame_region_borrow(view, builder, epoch);
    if (view && view->frame_admitted)
        return true;
    if (bindings_frame_index_epoch_complete(
            &builder->current, epoch)) {
        if (view)
            view->frame_admitted = true;
        return !view || rule_frame_region_borrow(view, builder, epoch);
    }
    if (view && view->variable_ids && view->eligible != 0u)
        return rule_frame_region_admit_inventory(view, builder, epoch);
    if (!bindings_builder_register_activation_source_frame(
            builder, source, epoch)) {
        return false;
    }
    if (view) {
        view->frame_admitted = true;
        return rule_frame_region_borrow(view, builder, epoch);
    }
    return true;
}

static bool rule_frame_region_admit_frame(
        RuleFrameRegion *view, BindingsBuilder *builder,
        Atom *source_var, uint32_t epoch) {
    if (!builder || !source_var || source_var->kind != ATOM_VAR ||
        source_var->var_id == VAR_ID_NONE || epoch == 0u) {
        return false;
    }
    if (view && view->exclusive && view->exclusive->active)
        return rule_frame_region_borrow(view, builder, epoch);
    if (view && view->frame_admitted)
        return true;
    if (bindings_frame_index_epoch_complete(
            &builder->current, epoch)) {
        if (view)
            view->frame_admitted = true;
        return !view || rule_frame_region_borrow(view, builder, epoch);
    }
    if (view && view->variable_ids && view->eligible != 0u)
        return rule_frame_region_admit_inventory(view, builder, epoch);
    VarId source_id = (VarId)var_base_id(source_var->var_id);
    if (!bindings_builder_register_frame(
            builder, &source_id, 1u, epoch)) {
        return false;
    }
    if (view) {
        view->frame_admitted = true;
        return rule_frame_region_borrow(view, builder, epoch);
    }
    return true;
}

/* The worklist admits a plan only when its root source is the exact right
 * pattern, and advances child atoms and child plans together.  Consequently
 * a variable plan's singleton support bit is already the validated dense
 * slot; do not repeat source classification at every occurrence. */
static inline bool rule_frame_region_index(
        const RuleFrameRegion *view,
        uint64_t variable_mask,
        uint32_t *slot_out) {
    if (!view || !slot_out)
        return false;
    uint64_t bit = variable_mask;
    if ((view->eligible & bit) == 0u ||
        (bit & (bit - 1u)) != 0u) {
        return false;
    }
    *slot_out = (uint32_t)__builtin_ctzll(bit);
    return true;
}

static inline BindingValue rule_frame_region_lookup(
        RuleFrameRegion *view,
        uint64_t variable_mask,
        Bindings *bindings, VarId activated_id) {
    uint32_t slot = 0u;
    if (!bindings || activated_id == VAR_ID_NONE ||
        !rule_frame_region_index(view, variable_mask, &slot)) {
        return activated_id != VAR_ID_NONE
            ? bindings_exclusive_frame_lookup(
                  view ? view->exclusive : NULL, bindings,
                  activated_id, NULL)
            : binding_value_from_atom(NULL);
    }
    if (!view->values ||
        view->epoch != var_epoch_suffix(activated_id) ||
        slot >= view->value_count) {
        return bindings_exclusive_frame_lookup(
            view ? view->exclusive : NULL, bindings,
            activated_id, NULL);
    }
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_MATCH_RULE_FRAME_REGION_DIRECT_READ);
    return view->values[slot];
}

static inline BindingValue rule_frame_region_lookup_value(
        RuleFrameRegion *view, uint64_t variable_mask,
        Bindings *bindings, BindingValue variable) {
    VarId id = binding_value_variable_id(variable);
    if (variable_mask != 0u)
        return rule_frame_region_lookup(
            view, variable_mask, bindings, id);
    if (view && view->exclusive && view->exclusive->active &&
        binding_value_is_contextual(variable) &&
        variable.epoch == view->exclusive->epoch) {
        return bindings_exclusive_frame_lookup(
            view->exclusive, bindings, id, NULL);
    }
    return bindings_lookup_value(bindings, variable);
}

static uint32_t rule_frame_region_staged_len(
        const RuleFrameRegion *view) {
    return view && view->exclusive && view->exclusive->active
        ? view->exclusive->write_len : 0u;
}

static bool epoch_match_push(EpochMatchWorklist *work,
                             MatchTerm left, MatchTerm right,
                             const CettaOpenPatternPlan *right_plan) {
    if (work->len == work->cap) {
        size_t next_cap = work->cap * 2u;
        if (next_cap <= work->cap ||
            next_cap > SIZE_MAX / sizeof(*work->items))
            return false;
        EpochMatchPair *next = cetta_malloc(
            sizeof(*work->items) * next_cap);
        memcpy(next, work->items, sizeof(*work->items) * work->len);
        if (work->items != work->inline_items) free(work->items);
        work->items = next;
        work->cap = next_cap;
    }
    work->items[work->len++] =
        (EpochMatchPair){false, left, right, right_plan, 0u, 0u};
    return true;
}

static bool epoch_match_push_children(EpochMatchWorklist *work,
                                      MatchTerm left, MatchTerm right,
                                      const CettaOpenPatternPlan *right_plan,
                                      CettaExprIndex first,
                                      CettaExprIndex count) {
    if (!epoch_match_push(work, left, right, right_plan))
        return false;
    work->items[work->len - 1u].next_child = first;
    work->items[work->len - 1u].child_count = count;
    return true;
}

static bool epoch_match_push_exit(EpochMatchWorklist *work,
                                  MatchTerm left, MatchTerm right) {
    if (!epoch_match_push(work, left, right, NULL))
        return false;
    work->items[work->len - 1u].exit = true;
    return true;
}

typedef struct {
    MatchTerm left;
    uint32_t cursor;
} OpenPatternProgramItem;

typedef struct {
    OpenPatternProgramItem *items;
    size_t len;
    size_t cap;
    OpenPatternProgramItem inline_items[32];
} OpenPatternProgramStack;

static bool open_pattern_program_stack_push(
        OpenPatternProgramStack *stack,
        OpenPatternProgramItem item) {
    if (!stack)
        return false;
    if (stack->len == stack->cap) {
        size_t next_cap = stack->cap * 2u;
        if (next_cap <= stack->cap ||
            next_cap > SIZE_MAX / sizeof(*stack->items)) {
            return false;
        }
        OpenPatternProgramItem *next = cetta_malloc(
            next_cap * sizeof(*next));
        memcpy(next, stack->items,
               stack->len * sizeof(*next));
        if (stack->items != stack->inline_items)
            free(stack->items);
        stack->items = next;
        stack->cap = next_cap;
    }
    stack->items[stack->len++] = item;
    return true;
}

/* Plan-time coordinate order (Maranget): constructors, then repeated
 * variables, then first-occurrence bindings.  Unplanned matching keeps
 * source order so the failure prefix stays left-to-right. */
enum { MATCH_PLAN_ORDER_CAP = 64 };

static void match_plan_discriminating_order(
        const CettaOpenPatternPlan *plan, CettaExprIndex len,
        CettaExprIndex *order) {
    CettaExprIndex rigid[MATCH_PLAN_ORDER_CAP];
    CettaExprIndex repeated[MATCH_PLAN_ORDER_CAP];
    CettaExprIndex fresh[MATCH_PLAN_ORDER_CAP];
    CettaExprIndex n_rigid = 0, n_repeated = 0, n_fresh = 0, k = 0;
    CettaExprIndex i;
    if (!order)
        return;
    if (!plan || !plan->children ||
        plan->child_count != len || len > MATCH_PLAN_ORDER_CAP) {
        for (i = 0; i < len; i++)
            order[i] = i;
        return;
    }
    for (i = 0; i < len; i++) {
        const CettaOpenPatternPlan *child = &plan->children[i];
        if (child->kind != ATOM_VAR)
            rigid[n_rigid++] = i;
        else if ((plan->repeated_variable_mask & child->variable_mask) != 0u ||
                 child->repeated_variable_mask != 0u)
            repeated[n_repeated++] = i;
        else
            fresh[n_fresh++] = i;
    }
    for (i = 0; i < n_rigid; i++)
        order[k++] = rigid[i];
    for (i = 0; i < n_repeated; i++)
        order[k++] = repeated[i];
    for (i = 0; i < n_fresh; i++)
        order[k++] = fresh[i];
}

/* Constructor mismatch and repeated-variable kind clash are BindingDecision
 * facts of the equation (Maranget): they are decided before this expression's
 * children extend the attempt.  Bind order stays source order so planned and
 * linear substitutions remain the same triangular environment. */
static bool match_query_child_rigid_impossible(
        Atom *query, const CettaOpenPatternPlan *child) {
    if (!query || !child || child->kind == ATOM_VAR)
        return false;
    if (query->kind == ATOM_VAR)
        return false;
    if (query->kind != child->kind)
        return true;
    if (query->kind == ATOM_SYMBOL)
        return child->source && child->source->kind == ATOM_SYMBOL &&
            query->sym_id != child->source->sym_id;
    if (query->kind == ATOM_EXPR)
        return query->expr.len != child->child_count;
    if (query->kind == ATOM_GROUNDED)
        return child->source && child->source->kind == ATOM_GROUNDED &&
            !atom_eq(query, child->source);
    return false;
}

static bool match_plan_fails_before_bind(
        Atom *left, const CettaOpenPatternPlan *plan, CettaExprIndex nch) {
    CettaExprIndex i;
    CettaExprIndex j;
    CettaExprIndex order[MATCH_PLAN_ORDER_CAP];
    if (!left || left->kind != ATOM_EXPR || !plan || !plan->children ||
        plan->child_count != nch || nch > MATCH_PLAN_ORDER_CAP)
        return false;
    {
        bool seen_var = false;
        bool constructor_after_var = false;
        for (i = 0; i < nch; i++) {
            if (plan->children[i].kind == ATOM_VAR)
                seen_var = true;
            else if (seen_var) {
                constructor_after_var = true;
                break;
            }
        }
        /* Source order already checks constructors first. */
        if (!constructor_after_var)
            return false;
    }
    match_plan_discriminating_order(plan, nch, order);
    for (i = 0; i < nch; i++) {
        CettaExprIndex child = order[i];
        if (match_query_child_rigid_impossible(
                left->expr.elems[child], &plan->children[child]))
            return true;
    }
    if (plan->repeated_variable_mask == 0u)
        return false;
    for (i = 0; i < nch; i++) {
        const CettaOpenPatternPlan *ci = &plan->children[i];
        uint64_t bit;
        Atom *qi;
        if (ci->kind != ATOM_VAR)
            continue;
        bit = ci->variable_mask;
        if ((plan->repeated_variable_mask & bit) == 0u ||
            (bit & (bit - 1u)) != 0u)
            continue;
        qi = left->expr.elems[i];
        if (!qi || qi->kind == ATOM_VAR || atom_has_vars(qi))
            continue;
        for (j = 0; j < i; j++) {
            const CettaOpenPatternPlan *cj = &plan->children[j];
            Atom *qj;
            if (cj->kind != ATOM_VAR || cj->variable_mask != bit)
                continue;
            qj = left->expr.elems[j];
            if (!qj || qj->kind == ATOM_VAR || atom_has_vars(qj))
                continue;
            if (qi == qj)
                break;
            if (qi->kind != qj->kind)
                return true;
            if (qi->kind == ATOM_SYMBOL && qi->sym_id != qj->sym_id)
                return true;
            break;
        }
    }
    return false;
}

static bool match_atoms_epoch_views_worklist(
    Atom *left, BindingValueKind left_kind, uint32_t left_epoch,
    uint32_t left_first_entry, Atom *right,
    Bindings *bindings, BindingsBuilder *builder,
    Arena *a, uint32_t right_epoch, BindingValueKind right_kind,
    bool prefer_right_rule_slot,
    const BindingsActivationView *left_frame,
    const CettaOpenPatternPlan *right_plan,
    RuleFrameRegion *right_frame_region) {
    EpochMatchWorklist work;
    MatchPathSet path;
    Bindings *initial = builder ? &builder->current : bindings;
    const uint32_t rule_epoch = right_frame_region &&
            right_frame_region->exclusive &&
            right_frame_region->exclusive->active
        ? right_frame_region->exclusive->epoch
        : right_frame_region && right_frame_region->epoch != 0u
            ? right_frame_region->epoch
            : right_epoch;

    if (!left || !right || !initial || !a ||
        (right_plan && right_plan->source != right) ||
        (left_kind != BINDING_VALUE_MATERIALIZED &&
         left_first_entry > initial->len))
        return false;
    if (binding_value_kind_is_contextual(right_kind) &&
        atom_has_vars(right)) {
        bool registered = builder && right_frame_region
            ? rule_frame_region_admit_source(
                  right_frame_region, builder, right, right_epoch)
            : builder
                ? bindings_builder_register_activation_source_frame(
                      builder, right, right_epoch)
                : bindings_register_activation_source_frame(
                      initial, right, right_epoch);
        if (!registered)
            return false;
    }
    AtomDeepCopySession *transport = NULL;
    MatchWriteStamp attempt_start = match_write_stamp(
        initial, rule_frame_region_staged_len(right_frame_region));
    bool attempt_repeated_var = false;
    bool attempt_resolving_bound = false;
    /* A pair recurs on its own active path only through a cycle, in the
     * substitution or in a corrupted atom graph.  Every bind refuses a
     * substitution cycle, so for an environment certified acyclic the path
     * set starts only once the walk has decomposed more pairs than a finite
     * match of ordinary size needs; a cycle repeats without end, so its pair
     * recurs on the recorded path after at most one period. */
    bool track_path = initial->cycle_state != BINDINGS_CYCLE_ACYCLIC;
    uint32_t untracked_pairs = 0u;
    work.items = work.inline_items;
    work.len = 0;
    work.cap = sizeof work.inline_items / sizeof work.inline_items[0];
    match_path_init(&path);
    MatchTerm left_root = match_term_at(
        left, left_kind, left_epoch, left_first_entry, left_frame);
    MatchTerm right_root = match_term_at(
        right, right_kind, right_epoch, 0u, NULL);
    if (!epoch_match_push(
            &work, left_root, right_root,
            right_plan))
        goto fail;

    while (work.len > 0) {
        EpochMatchPair pair;
        EpochMatchPair *top = &work.items[work.len - 1u];
        if (top->child_count != 0u) {
            CettaExprIndex child = top->next_child++;
            pair = (EpochMatchPair){
                .left = top->left,
                .right = top->right,
                .right_plan = top->right_plan
                    ? &top->right_plan->children[child] : NULL,
            };
            pair.left.value.skeleton =
                top->left.value.skeleton->expr.elems[child];
            pair.right.value.skeleton =
                top->right.value.skeleton->expr.elems[child];
            if (top->next_child == top->child_count)
                work.len--;
        } else {
            pair = work.items[--work.len];
        }
        if (pair.exit) {
            match_path_leave(&path, pair.left, pair.right);
            continue;
        }
        BindingValue left_value = pair.left.value;
        BindingValue right_value = pair.right.value;
        left = left_value.skeleton;
        right = right_value.skeleton;
        left_kind = left_value.kind;
        right_kind = right_value.kind;
        bool left_original = binding_value_is_contextual(left_value);
        bool right_original = binding_value_is_contextual(right_value);
        left_epoch = left_value.epoch;
        left_first_entry = pair.left.first_entry;
        left_frame = pair.left.frame;
        right_epoch = right_value.epoch;
        right_plan = pair.right_plan;
        attempt_resolving_bound = false;
        Bindings *current = builder ? &builder->current : bindings;
        size_t dereferences = 0;
        size_t dereference_limit = bindings_dereference_limit(current);
        size_t local_dereference_allowance =
            right_frame_region && right_frame_region->exclusive &&
                    right_frame_region->exclusive->active
                ? (size_t)right_frame_region->exclusive->write_len * 2u
                : 0u;
        if (dereference_limit <= SIZE_MAX - local_dereference_allowance) {
            dereference_limit += local_dereference_allowance;
        }

retry_pair:
        if (right_plan &&
            (!right_original || right_plan->source != right ||
             right_plan->kind != right->kind)) {
            goto fail;
        }
        if (left == right &&
            match_shared_ground_reflexivity_try(
                left, right, right_plan == NULL,
                match_shared_occurrence_same_environment(
                    left_original, right_original,
                    left_epoch, right_epoch), current))
            continue;
        bool closed_equal = false;
        if (match_closed_expression_decision_try(
                left, right, &closed_equal)) {
            if (!closed_equal)
                goto fail;
            continue;
        }
        if (!right_plan &&
            match_finite_ground_equal_try(left, right, &closed_equal)) {
            if (!closed_equal)
                goto fail;
            continue;
        }
        if (left->kind == ATOM_VAR) {
            VarId left_id = left_original
                ? var_epoch_id(left->var_id, left_epoch) : left->var_id;
            BindingValue existing = binding_value_from_atom(NULL);
            bool left_lookup_done = false;
            if (left_original) {
                bool dense_present = false;
                bool dense_known = left_frame &&
                    bindings_activation_view_lookup(
                        current, left_frame, left->var_id,
                        &existing, &dense_present);
                if (!dense_known && right_frame_region &&
                    right_frame_region->exclusive) {
                    bool local_known = false;
                    existing = bindings_exclusive_frame_lookup(
                        right_frame_region->exclusive, NULL,
                        left_id, &local_known);
                    if (local_known) {
                        dense_known = true;
                        dense_present = existing.skeleton != NULL;
                    }
                }
                if (!dense_known &&
                    bindings_frame_ref_is_valid(
                        binding_value_frame_ref(left_value))) {
                    dense_known = bindings_frame_index_lookup_value_ref(
                        current, left_value, &existing);
                    dense_present = existing.skeleton != NULL;
                }
                if (!dense_known) {
                    dense_known = bindings_frame_index_lookup_value(
                        current, left_id, &existing);
                    dense_present = existing.skeleton != NULL;
                }
                if (!dense_known) {
                    existing = bindings_lookup_value_since(
                        current, left_id, left_first_entry);
                } else if (!dense_present) {
                    existing = binding_value_from_atom(NULL);
                }
                left_lookup_done = true;

                if (existing.skeleton) {
                    if (++dereferences > dereference_limit)
                        goto fail;
                    attempt_resolving_bound = true;
                    left_value = existing;
                    left = existing.skeleton;
                    left_kind = existing.kind;
                    left_original = binding_value_is_contextual(existing);
                    left_epoch = existing.epoch;
                    left_first_entry = 0u;
                    left_frame = NULL;
                    goto retry_pair;
                }
                /* Both writers accept the effective identity directly. */
            }
            if (!left_lookup_done) {
                bool local_known = false;
                existing = bindings_exclusive_frame_lookup(
                    right_frame_region
                        ? right_frame_region->exclusive : NULL,
                    NULL, left_id, &local_known);
                if (local_known || bindings_frame_index_lookup_value(
                        current, left_id, &existing)) {
                    cetta_runtime_stats_inc(
                        CETTA_RUNTIME_COUNTER_MATCH_LEFTOVER_FRAME_OWNED);
                } else {
                    cetta_runtime_stats_inc(
                        CETTA_RUNTIME_COUNTER_MATCH_LEFTOVER_HASH);
                    cetta_runtime_stats_inc(
                        var_epoch_suffix(left_id) == 0u
                            ? CETTA_RUNTIME_COUNTER_MATCH_LEFTOVER_UNFRAMED_PLAIN
                            : CETTA_RUNTIME_COUNTER_MATCH_LEFTOVER_UNFRAMED_EPOCH);
                    existing = bindings_lookup_value_id(current, left_id);
                    if (existing.skeleton) {
                        cetta_runtime_stats_inc(
                            CETTA_RUNTIME_COUNTER_MATCH_LEFTOVER_HASH_HIT);
                    }
                }
            }
            if (existing.skeleton) {
                if (++dereferences > dereference_limit) goto fail;
                attempt_resolving_bound = true;
                left_value = existing;
                left = existing.skeleton;
                left_kind = existing.kind;
                left_original = binding_value_is_contextual(existing);
                left_epoch = existing.epoch;
                left_first_entry = 0u;
                left_frame = NULL;
                goto retry_pair;
            }
            if (right->kind == ATOM_VAR) {
                VarId right_id = right_original
                    ? var_epoch_id(right->var_id, right_epoch)
                    : right->var_id;
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_MATCH);
                BindingValue right_existing =
                    rule_frame_region_lookup_value(
                        right_frame_region,
                        right_original && right_plan
                            ? right_plan->variable_mask : 0u,
                        current, right_value);
                if (right_existing.skeleton) {
                    if (++dereferences > dereference_limit) goto fail;
                    attempt_resolving_bound = true;
                    right_value = right_existing;
                    right = right_existing.skeleton;
                    right_kind = right_existing.kind;
                    right_original = binding_value_is_contextual(right_existing);
                    right_epoch = right_existing.epoch;
                    right_plan = NULL;
                    goto retry_pair;
                }
                if (left_id == right_id) continue;
                if (prefer_right_rule_slot && right_original &&
                    right_epoch == rule_epoch) {
                    BindingValue value = left_value;
                    value.skeleton = left;
                    bool added = builder && value.skeleton
                        ? bindings_builder_add_rule_epoch_key_fresh(
                              builder, right, right_epoch, value, a, &transport,
                              right_frame_region, NULL)
                        : false;
                    if (!added) goto fail;
                    continue;
                }
                BindingValue value = right_value;
                value.skeleton = right;
                bool added = bindings_match_store_value_in_rule_region(
                    right_frame_region,
                    bindings, builder, a, &transport,
                    MATCH_BIND_CONTEXT_STORED_EQUATION, left_original,
                    left_id, left->sym_id,
                    left->name_key, value, BINDINGS_REACHABILITY_UNKNOWN, NULL);
                if (!added) goto fail;
                continue;
            }
            BindingsReachability cycle_evidence =
                BINDINGS_REACHABILITY_UNKNOWN;
            if (builder && right_original && right_plan) {
                if (atom_has_vars(right) &&
                    !rule_frame_region_admit_source(
                        right_frame_region, builder, right, right_epoch)) {
                    goto fail;
                }
                cycle_evidence =
                    bindings_cycle_source_support_reaches_var(
                        current, right_plan->source,
                        right_plan->variable_ids,
                        right_plan->variable_mask,
                        right_epoch, left_id,
                        right_frame_region);
            }
            BindingValue value = right_value;
            value.skeleton = right;
            bool added = false;
            if (builder &&
                cycle_evidence != BINDINGS_REACHABILITY_UNKNOWN) {
                added =
                    bindings_match_store_value_in_rule_region(
                        right_frame_region,
                        NULL, builder, a, &transport,
                        MATCH_BIND_CONTEXT_STORED_EQUATION, left_original,
                        left_id, left->sym_id,
                        left->name_key, value, cycle_evidence, NULL);
            } else {
                added = bindings_match_store_value_in_rule_region(
                    right_frame_region,
                    bindings, builder, a, &transport,
                    MATCH_BIND_CONTEXT_STORED_EQUATION, left_original,
                    left_id, left->sym_id,
                    left->name_key, value, BINDINGS_REACHABILITY_UNKNOWN, NULL);
            }
            if (!added) goto fail;
            continue;
        }
        if (right->kind == ATOM_VAR) {
            VarId right_id = right_original
                ? var_epoch_id(right->var_id, right_epoch)
                : right->var_id;
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_MATCH);
            BindingValue existing = rule_frame_region_lookup_value(
                right_frame_region,
                right_plan ? right_plan->variable_mask : 0u,
                current, right_value);
            if (existing.skeleton) {
                if (++dereferences > dereference_limit) goto fail;
                attempt_resolving_bound = true;
                right_value = existing;
                right = existing.skeleton;
                right_kind = existing.kind;
                right_original = binding_value_is_contextual(existing);
                right_epoch = existing.epoch;
                right_plan = NULL;
                goto retry_pair;
            }
            BindingValue binding_value = left_value;
            binding_value.skeleton = left;
            bool added = false;
            if (builder && right_original && right_epoch == rule_epoch &&
                (prefer_right_rule_slot || !right->name_key)) {
                added = bindings_builder_add_rule_epoch_key_fresh(
                    builder, right, right_epoch, binding_value, a, &transport,
                    right_frame_region, NULL);
            } else {
                added = bindings_match_store_value_in_rule_region(
                    right_frame_region,
                    bindings, builder, a, &transport,
                    MATCH_BIND_CONTEXT_ACTIVATION_SOURCE, right_original,
                    right_id, right->sym_id,
                    right->name_key, binding_value, BINDINGS_REACHABILITY_UNKNOWN, NULL);
            }
            if (!added) goto fail;
            continue;
        }
        if (left->kind == ATOM_SYMBOL && right->kind == ATOM_SYMBOL) {
            if (left->sym_id != right->sym_id) {
                if (attempt_resolving_bound)
                    attempt_repeated_var = true;
                goto fail;
            }
            continue;
        }
        if (left->kind == ATOM_GROUNDED && right->kind == ATOM_GROUNDED) {
            if (!atom_eq(left, right)) {
                if (attempt_resolving_bound)
                    attempt_repeated_var = true;
                goto fail;
            }
            continue;
        }
        if (left->kind != ATOM_EXPR || right->kind != ATOM_EXPR ||
            left->expr.len != right->expr.len ||
            (right_plan &&
             (right_plan->child_count != right->expr.len ||
              (right->expr.len != 0u && !right_plan->children)))) {
            if (attempt_resolving_bound)
                attempt_repeated_var = true;
            goto fail;
        }
        if (!track_path &&
            ++untracked_pairs > MATCH_UNTRACKED_PAIR_LIMIT)
            track_path = true;
        if (!right_plan && track_path &&
            (!match_path_enter(
                 &path,
                 (MatchTerm){
                     .value = left_value,
                     .frame = left_frame,
                     .first_entry = left_first_entry,
                 },
                 (MatchTerm){.value = right_value}) ||
             !epoch_match_push_exit(
                 &work,
                 (MatchTerm){
                     .value = left_value,
                     .frame = left_frame,
                     .first_entry = left_first_entry,
                 },
                 (MatchTerm){.value = right_value}))) {
            goto fail;
        }
        {
            CettaExprIndex nch = left->expr.len;
            CettaExprIndex first = 0u;
            if (right_plan && nch > 0u &&
                match_plan_fails_before_bind(left, right_plan, nch)) {
                if (attempt_resolving_bound)
                    attempt_repeated_var = true;
                goto fail;
            }
            if (nch > 1u) {
                BindingValue left_parent_value = left_value;
                BindingValue right_parent_value = right_value;
                left_parent_value.skeleton = left;
                right_parent_value.skeleton = right;
                if (!epoch_match_push_children(
                        &work,
                        (MatchTerm){
                            .value = left_parent_value,
                            .frame = left_frame,
                            .first_entry = left_first_entry,
                        },
                        (MatchTerm){.value = right_parent_value},
                        right_plan, 1u, nch))
                    goto fail;
            }
        /* Preserve activation flags and validate the child plan normally.
         * Only its otherwise immediate enqueue/dequeue is removed. */
        if (nch != 0u) {
            Atom *next_left = left->expr.elems[first];
            Atom *next_right = right->expr.elems[first];
            const CettaOpenPatternPlan *next_plan = right_plan
                ? &right_plan->children[first] : NULL;
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_MATCH_WORKLIST_FRAME_ELIDED);
            left = next_left;
            right = next_right;
            left_value.skeleton = next_left;
            right_value.skeleton = next_right;
            right_plan = next_plan;
            dereferences = 0u;
            dereference_limit = bindings_dereference_limit(current);
            goto retry_pair;
        }
        }
    }
    if (work.items != work.inline_items) free(work.items);
    match_path_free(&path);
    atom_deep_copy_session_free(transport);
    match_note_unification_attempt(
        true, match_write_stamp_changed(
                  attempt_start,
                  match_write_stamp(
                      initial,
                      rule_frame_region_staged_len(right_frame_region))),
        false);
    return true;

fail:
    if (work.items != work.inline_items) free(work.items);
    match_path_free(&path);
    atom_deep_copy_session_free(transport);
    match_note_unification_attempt(
        false, match_write_stamp_changed(
                   attempt_start,
                   match_write_stamp(
                       initial,
                       rule_frame_region_staged_len(right_frame_region))),
        attempt_repeated_var);
    return false;
}


/* Execute the immutable rule-pattern plan as one contiguous preorder
 * program.  Dynamic query observations and all binding writes remain the
 * authoritative matcher's work.  A bound rule variable hands that one pair
 * to the unchanged generic matcher; a variable matched against an unentered
 * rigid subtree advances by the subtree's exact compiled span. */
static bool match_atoms_epoch_views_linear(
    Atom *left, BindingValueKind left_kind, uint32_t left_epoch,
    uint32_t left_first_entry, Atom *right,
    Bindings *bindings, BindingsBuilder *builder,
    Arena *a, uint32_t right_epoch, BindingValueKind right_kind,
    bool prefer_right_rule_slot,
    const BindingsActivationView *left_frame,
    const CettaOpenPatternPlan *right_plan,
    BindingsExclusiveFrame *exclusive) {
    Bindings *initial = builder ? &builder->current : bindings;
    const CettaOpenPatternInstruction *program =
        right_plan ? right_plan->linear_program : NULL;
    uint32_t program_len = right_plan
        ? right_plan->linear_program_len : 0u;
    bool right_original = right_kind != BINDING_VALUE_MATERIALIZED;
    if (!left || !right || !initial || !a || !right_plan ||
        !right_original || right_plan->source != right ||
        !program || program_len == 0u ||
        program[0].source != right_plan->source ||
        program[0].variable_mask != right_plan->variable_mask ||
        program[0].subtree_span != program_len ||
        (left_kind != BINDING_VALUE_MATERIALIZED &&
         left_first_entry > initial->len)) {
        return false;
    }
    AtomDeepCopySession *transport = NULL;

    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_MATCH_OPEN_LINEAR_ATTEMPT);
    MatchWriteStamp attempt_start = match_write_stamp(
        initial, exclusive && exclusive->active
            ? exclusive->write_len : 0u);
    bool attempt_repeated_var = false;
    OpenPatternProgramStack stack = {
        .items = stack.inline_items,
        .cap = sizeof stack.inline_items /
            sizeof stack.inline_items[0],
    };
    RuleFrameRegion frame_region;
    rule_frame_region_init_exclusive(
        &frame_region, right_plan, exclusive);
    if (!open_pattern_program_stack_push(
            &stack,
            (OpenPatternProgramItem){
                .left = match_term_at(
                    left, left_kind, left_epoch,
                    left_first_entry, left_frame),
                .cursor = 0u,
            })) {
        goto fail;
    }

    while (stack.len != 0u) {
        OpenPatternProgramItem item = stack.items[--stack.len];
        left = item.left.value.skeleton;
        left_kind = item.left.value.kind;
        bool left_original = binding_value_is_contextual(item.left.value);
        left_epoch = item.left.value.epoch;
        left_first_entry = item.left.first_entry;
        left_frame = item.left.frame;
        size_t cursor = item.cursor;
        if (cursor >= program_len)
            goto fail;
        const CettaOpenPatternInstruction *instruction =
            &program[cursor];
        if (!instruction->source ||
            instruction->subtree_span == 0u ||
            instruction->subtree_span > program_len - cursor) {
            goto fail;
        }
        right = instruction->source;
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_MATCH_OPEN_LINEAR_NODE_VISIT);
        Bindings *current = builder
            ? &builder->current : bindings;
        size_t dereferences = 0u;
        size_t dereference_limit =
            bindings_dereference_limit(current);
        size_t local_dereference_allowance =
            exclusive && exclusive->active
                ? (size_t)exclusive->write_len * 2u : 0u;
        if (dereference_limit <= SIZE_MAX - local_dereference_allowance)
            dereference_limit += local_dereference_allowance;

retry_pair:
        if (left == right &&
            match_shared_ground_reflexivity_try(
                left, right, false,
                match_shared_occurrence_same_environment(
                    left_original, true, left_epoch, right_epoch), current))
            continue;
        bool closed_equal = false;
        if (match_closed_expression_decision_try(
                left, right, &closed_equal)) {
            if (!closed_equal)
                goto fail;
            continue;
        }
        if (left->kind == ATOM_VAR) {
            VarId left_id = left_original
                ? var_epoch_id(left->var_id, left_epoch) : left->var_id;
            BindingValue existing = binding_value_from_atom(NULL);
            bool left_lookup_done = false;
            if (left_original) {
                bool dense_present = false;
                bool dense_known = left_frame &&
                    bindings_activation_view_lookup(
                        current, left_frame, left->var_id,
                        &existing, &dense_present);
                if (!dense_known && exclusive) {
                    bool local_known = false;
                    existing = bindings_exclusive_frame_lookup(
                        exclusive, NULL, left_id, &local_known);
                    if (local_known) {
                        dense_known = true;
                        dense_present = existing.skeleton != NULL;
                    }
                }
                if (!dense_known) {
                    dense_known = bindings_frame_index_lookup_value(
                        current, left_id, &existing);
                    dense_present = existing.skeleton != NULL;
                }
                if (!dense_known) {
                    existing = bindings_lookup_value_since(
                        current, left_id, left_first_entry);
                } else if (!dense_present) {
                    existing = binding_value_from_atom(NULL);
                }
                left_lookup_done = true;
                if (existing.skeleton) {
                    if (++dereferences > dereference_limit)
                        goto fail;
                    left = existing.skeleton;
                    left_kind = existing.kind;
                    left_original = binding_value_is_contextual(existing);
                    left_epoch = existing.epoch;
                    left_first_entry = 0u;
                    left_frame = NULL;
                    goto retry_pair;
                }
            }
            if (!left_lookup_done) {
                bool local_known = false;
                existing = bindings_exclusive_frame_lookup(
                    exclusive, NULL, left_id, &local_known);
                if (local_known || bindings_frame_index_lookup_value(
                        current, left_id, &existing)) {
                    cetta_runtime_stats_inc(
                        CETTA_RUNTIME_COUNTER_MATCH_LEFTOVER_FRAME_OWNED);
                } else {
                    cetta_runtime_stats_inc(
                        CETTA_RUNTIME_COUNTER_MATCH_LEFTOVER_HASH);
                    cetta_runtime_stats_inc(
                        var_epoch_suffix(left_id) == 0u
                            ? CETTA_RUNTIME_COUNTER_MATCH_LEFTOVER_UNFRAMED_PLAIN
                            : CETTA_RUNTIME_COUNTER_MATCH_LEFTOVER_UNFRAMED_EPOCH);
                    existing = bindings_lookup_value_id(current, left_id);
                    if (existing.skeleton) {
                        cetta_runtime_stats_inc(
                            CETTA_RUNTIME_COUNTER_MATCH_LEFTOVER_HASH_HIT);
                    }
                }
            }
            if (existing.skeleton) {
                if (++dereferences > dereference_limit)
                    goto fail;
                left = existing.skeleton;
                left_kind = existing.kind;
                left_original = binding_value_is_contextual(existing);
                left_epoch = existing.epoch;
                left_first_entry = 0u;
                left_frame = NULL;
                goto retry_pair;
            }
            if (right->kind == ATOM_VAR) {
                VarId right_id = var_epoch_id(
                    right->var_id, right_epoch);
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_MATCH);
                BindingValue right_existing =
                    rule_frame_region_lookup(
                        &frame_region, instruction->variable_mask,
                        current, right_id);
                if (right_existing.skeleton) {
                    cetta_runtime_stats_inc(
                        CETTA_RUNTIME_COUNTER_MATCH_OPEN_LINEAR_DYNAMIC_FALLBACK);
                    if (!match_atoms_epoch_views_worklist(
                            left, left_kind,
                            left_epoch, left_first_entry,
                            right_existing.skeleton, bindings, builder,
                            a, right_existing.epoch, right_existing.kind,
                            prefer_right_rule_slot,
                            left_frame, NULL, &frame_region)) {
                        attempt_repeated_var = true;
                        goto fail;
                    }
                    continue;
                }
                if (left_id == right_id)
                    continue;
                if (prefer_right_rule_slot) {
                    bool added = builder &&
                        bindings_builder_add_rule_epoch_key_fresh(
                            builder, right, right_epoch,
                            binding_value_from_context_kind(
                                left, left_epoch, left_kind),
                            a, &transport,
                            &frame_region, NULL);
                    if (!added)
                        goto fail;
                    continue;
                }
                BindingValue value = binding_value_from_context_kind(
                    right, right_epoch, right_kind);
                bool added = bindings_match_store_value_in_rule_region(
                    &frame_region,
                    bindings, builder, a, &transport,
                    MATCH_BIND_CONTEXT_STORED_EQUATION, left_original,
                    left_id, left->sym_id,
                    left->name_key, value, BINDINGS_REACHABILITY_UNKNOWN, NULL);
                if (!added)
                    goto fail;
                continue;
            }
            BindingsReachability cycle_evidence =
                BINDINGS_REACHABILITY_UNKNOWN;
            if (builder) {
                if (atom_has_vars(instruction->source) &&
                    !rule_frame_region_admit_source(
                        &frame_region, builder, right,
                        right_epoch)) {
                    goto fail;
                }
                cycle_evidence =
                    bindings_cycle_source_support_reaches_var(
                        current, instruction->source,
                        right_plan->variable_ids,
                        instruction->variable_mask,
                        right_epoch, left_id,
                        &frame_region);
            }
            BindingValue value = binding_value_from_context_kind(
                right, right_epoch, right_kind);
            bool added = false;
            if (builder &&
                cycle_evidence != BINDINGS_REACHABILITY_UNKNOWN) {
                added =
                    bindings_match_store_value_in_rule_region(
                        &frame_region,
                        NULL, builder, a, &transport,
                        MATCH_BIND_CONTEXT_STORED_EQUATION, left_original,
                        left_id, left->sym_id,
                        left->name_key, value, cycle_evidence, NULL);
            } else {
                added = bindings_match_store_value_in_rule_region(
                    &frame_region,
                    bindings, builder, a, &transport,
                    MATCH_BIND_CONTEXT_STORED_EQUATION, left_original,
                    left_id, left->sym_id,
                    left->name_key, value, BINDINGS_REACHABILITY_UNKNOWN, NULL);
            }
            if (!added)
                goto fail;
            continue;
        }
        if (right->kind == ATOM_VAR) {
            VarId right_id = var_epoch_id(
                right->var_id, right_epoch);
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_MATCH);
            BindingValue existing = rule_frame_region_lookup(
                &frame_region, instruction->variable_mask,
                current, right_id);
            if (existing.skeleton) {
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_MATCH_OPEN_LINEAR_DYNAMIC_FALLBACK);
                if (!match_atoms_epoch_views_worklist(
                        left, left_kind,
                        left_epoch, left_first_entry,
                        existing.skeleton, bindings, builder,
                        a, existing.epoch, existing.kind,
                        prefer_right_rule_slot,
                        left_frame, NULL, &frame_region)) {
                    attempt_repeated_var = true;
                    goto fail;
                }
                continue;
            }
            BindingValue binding_value = binding_value_from_context_kind(
                left, left_epoch, left_kind);
            bool added = false;
            if (builder && prefer_right_rule_slot) {
                added = bindings_builder_add_rule_epoch_key_fresh(
                    builder, right, right_epoch, binding_value, a, &transport,
                    &frame_region, NULL);
            } else {
                added = bindings_match_store_value_in_rule_region(
                    &frame_region,
                    bindings, builder, a, &transport,
                    MATCH_BIND_CONTEXT_ACTIVATION_SOURCE, true,
                    right_id, right->sym_id,
                    right->name_key, binding_value, BINDINGS_REACHABILITY_UNKNOWN, NULL);
            }
            if (!added)
                goto fail;
            continue;
        }
        if (left->kind == ATOM_SYMBOL && right->kind == ATOM_SYMBOL) {
            if (left->sym_id != right->sym_id)
                goto fail;
            continue;
        }
        if (left->kind == ATOM_GROUNDED &&
            right->kind == ATOM_GROUNDED) {
            if (!atom_eq(left, right))
                goto fail;
            continue;
        }
        if (left->kind != ATOM_EXPR ||
            right->kind != ATOM_EXPR ||
            left->expr.len != right->expr.len ||
            (right->expr.len == 0u &&
             instruction->subtree_span != 1u)) {
            goto fail;
        }
        if (right->expr.len == 0u)
            continue;
        size_t subtree_end = cursor + instruction->subtree_span;
        size_t child_cursor = cursor + 1u;
        size_t pushed_begin = stack.len;
        for (CettaExprIndex child = 0u;
             child < right->expr.len; child++) {
            if (child_cursor >= subtree_end ||
                child_cursor >= program_len ||
                right->expr.elems[child] !=
                    program[child_cursor].source ||
                program[child_cursor].subtree_span == 0u ||
                program[child_cursor].subtree_span >
                    subtree_end - child_cursor ||
                !open_pattern_program_stack_push(
                    &stack,
                    (OpenPatternProgramItem){
                        .left = match_term_at(
                            left->expr.elems[child], left_kind,
                            left_epoch, left_first_entry, left_frame),
                        .cursor = (uint32_t)child_cursor,
                    })) {
                goto fail;
            }
            child_cursor += program[child_cursor].subtree_span;
        }
        if (child_cursor != subtree_end)
            goto fail;
        for (size_t lower = pushed_begin, upper = stack.len;
             lower < upper && lower < --upper; lower++) {
            OpenPatternProgramItem swap = stack.items[lower];
            stack.items[lower] = stack.items[upper];
            stack.items[upper] = swap;
        }
    }

    if (stack.items != stack.inline_items)
        free(stack.items);
    atom_deep_copy_session_free(transport);
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_MATCH_OPEN_LINEAR_COMMIT);
    match_note_unification_attempt(
        true, match_write_stamp_changed(
                  attempt_start,
                  match_write_stamp(
                      initial,
                      rule_frame_region_staged_len(&frame_region))),
        false);
    return true;

fail:
    if (stack.items != stack.inline_items)
        free(stack.items);
    atom_deep_copy_session_free(transport);
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_MATCH_OPEN_LINEAR_MISMATCH);
    match_note_unification_attempt(
        false, match_write_stamp_changed(
                   attempt_start,
                   match_write_stamp(
                       initial,
                       rule_frame_region_staged_len(&frame_region))),
        attempt_repeated_var);
    return false;
}
static bool match_atoms_epoch_worklist(Atom *left, Atom *right,
                                       Bindings *bindings,
                                       BindingsBuilder *builder,
                                       Arena *a, uint32_t epoch) {
    return match_atoms_epoch_views_worklist(
        left, BINDING_VALUE_MATERIALIZED, 0u, 0u,
        right, bindings, builder, a, epoch,
        BINDING_VALUE_CONTEXTUAL, false, NULL, NULL, NULL);
}

static bool match_atoms_epoch_view_worklist(
        Atom *left, uint32_t left_epoch, uint32_t left_first_entry,
        Atom *right, Bindings *bindings, BindingsBuilder *builder,
        Arena *a, uint32_t right_epoch) {
    return match_atoms_epoch_views_worklist(
        left, BINDING_VALUE_CONTEXTUAL, left_epoch, left_first_entry,
        right, bindings, builder, a, right_epoch,
        BINDING_VALUE_CONTEXTUAL, false, NULL, NULL, NULL);
}

static bool match_atoms_epoch_view_current_worklist(
        Atom *left, uint32_t left_epoch, uint32_t left_first_entry,
        Atom *right, Bindings *bindings, BindingsBuilder *builder,
        Arena *a) {
    return match_atoms_epoch_views_worklist(
        left, BINDING_VALUE_CONTEXTUAL, left_epoch, left_first_entry,
        right, bindings, builder, a, 0u,
        BINDING_VALUE_MATERIALIZED, false, NULL, NULL, NULL);
}

static bool match_atoms_epoch_view_rule_local_worklist(
        Atom *left, uint32_t left_epoch, uint32_t left_first_entry,
        Atom *right, BindingsBuilder *builder, Arena *a,
        uint32_t right_epoch) {
    return match_atoms_epoch_views_worklist(
        left, BINDING_VALUE_CONTEXTUAL, left_epoch, left_first_entry,
        right, NULL, builder, a, right_epoch,
        BINDING_VALUE_CONTEXTUAL, true, NULL, NULL, NULL);
}

static bool match_atoms_epoch_view_rule_local_planned_worklist(
        Atom *left, uint32_t left_epoch, uint32_t left_first_entry,
        Atom *right, const CettaOpenPatternPlan *right_plan,
        BindingsBuilder *builder, Arena *a, uint32_t right_epoch) {
    RuleFrameRegion frame_region;
    rule_frame_region_init(&frame_region, right_plan);
    return match_atoms_epoch_views_worklist(
        left, BINDING_VALUE_CONTEXTUAL, left_epoch, left_first_entry,
        right, NULL, builder, a, right_epoch,
        BINDING_VALUE_CONTEXTUAL, true, NULL, right_plan,
        right_plan ? &frame_region : NULL);
}

static bool match_atoms_activation_view_worklist(
        Atom *left, const BindingsActivationView *left_frame,
        Atom *right, Bindings *bindings, BindingsBuilder *builder,
    Arena *a, uint32_t right_epoch, bool right_original,
    bool prefer_right_rule_slot,
    const CettaOpenPatternPlan *right_plan) {
    if (!bindings_activation_view_available(
            left_frame, &builder->current))
        return false;
    RuleFrameRegion frame_region;
    rule_frame_region_init(&frame_region, right_plan);
    return match_atoms_epoch_views_worklist(
        left, left_frame->source_kind,
        left_frame->epoch, left_frame->first_entry,
        right, bindings, builder, a, right_epoch,
        right_original ? BINDING_VALUE_CONTEXTUAL
                       : BINDING_VALUE_MATERIALIZED,
        prefer_right_rule_slot, left_frame,
        right_plan, right_plan ? &frame_region : NULL);
}

static bool match_atoms_epoch_rule_local_worklist(
        Atom *left, Atom *right, BindingsBuilder *builder,
        Arena *a, uint32_t epoch) {
    return match_atoms_epoch_views_worklist(
        left, BINDING_VALUE_MATERIALIZED, 0u, 0u,
        right, NULL, builder, a, epoch,
        BINDING_VALUE_CONTEXTUAL, true, NULL, NULL, NULL);
}

static bool match_atoms_epoch_rule_local_planned_worklist(
        Atom *left, Atom *right,
        const CettaOpenPatternPlan *right_plan,
        BindingsBuilder *builder, Arena *a, uint32_t epoch) {
    RuleFrameRegion frame_region;
    rule_frame_region_init(&frame_region, right_plan);
    return match_atoms_epoch_views_worklist(
        left, BINDING_VALUE_MATERIALIZED, 0u, 0u,
        right, NULL, builder, a, epoch,
        BINDING_VALUE_CONTEXTUAL, true, NULL, right_plan,
        right_plan ? &frame_region : NULL);
}

bool match_binding_value_epoch_builder_rule_local(
        BindingValue left, Atom *right, BindingsBuilder *bb,
        Arena *a, uint32_t right_epoch) {
    return left.skeleton && right && bb && a && right_epoch != 0u &&
        match_atoms_epoch_views_worklist(
            left.skeleton, left.kind, left.epoch, 0u,
            right, NULL, bb, a, right_epoch,
            BINDING_VALUE_CONTEXTUAL, true, NULL, NULL, NULL);
}

bool match_binding_value_epoch_builder_rule_local_planned(
        BindingValue left, Atom *right,
        const CettaOpenPatternPlan *right_plan,
        BindingsBuilder *bb, Arena *a, uint32_t right_epoch) {
    RuleFrameRegion frame_region;
    rule_frame_region_init(&frame_region, right_plan);
    return left.skeleton && right && bb && a && right_epoch != 0u &&
        match_atoms_epoch_views_worklist(
            left.skeleton, left.kind, left.epoch, 0u,
            right, NULL, bb, a, right_epoch,
            BINDING_VALUE_CONTEXTUAL, true, NULL, right_plan,
            right_plan ? &frame_region : NULL);
}

/* A rule pattern that is a bare variable, meeting a query value that is not
 * itself a variable, binds its slot on the first occurrence and does nothing
 * else: there is no subterm to walk and no path to record.  This is the
 * worklist's own step for that pair; a slot already bound, or a query
 * variable, takes the full walk. */
typedef enum {
    MATCH_BARE_VARIABLE_DECLINE = 0,
    MATCH_BARE_VARIABLE_BOUND,
    MATCH_BARE_VARIABLE_FAIL,
} MatchBareVariableStep;

static MatchBareVariableStep match_exclusive_bare_variable(
        BindingValue left, Atom *right,
        const CettaOpenPatternPlan *right_plan,
        BindingsBuilder *bb, Arena *a, uint32_t right_epoch,
        BindingsExclusiveFrame *exclusive) {
    if (right->kind != ATOM_VAR || !left.skeleton ||
        left.skeleton->kind == ATOM_VAR ||
        (right_plan && right_plan->source != right))
        return MATCH_BARE_VARIABLE_DECLINE;
    RuleFrameRegion region;
    rule_frame_region_init_exclusive(&region, right_plan, exclusive);
    if (!rule_frame_region_admit_source(&region, bb, right, right_epoch))
        return MATCH_BARE_VARIABLE_DECLINE;
    bool local = false;
    BindingValue existing = bindings_exclusive_frame_lookup(
        exclusive, NULL, var_epoch_id(right->var_id, right_epoch), &local);
    if (!local || existing.skeleton)
        return MATCH_BARE_VARIABLE_DECLINE;
    AtomDeepCopySession *transport = NULL;
    MatchWriteStamp start = match_write_stamp(
        &bb->current, rule_frame_region_staged_len(&region));
    bool bound = bindings_builder_add_rule_epoch_key_fresh(
        bb, right, right_epoch, left, a, &transport, &region, NULL);
    atom_deep_copy_session_free(transport);
    match_note_unification_attempt(
        bound,
        match_write_stamp_changed(
            start,
            match_write_stamp(
                &bb->current, rule_frame_region_staged_len(&region))),
        false);
    return bound ? MATCH_BARE_VARIABLE_BOUND : MATCH_BARE_VARIABLE_FAIL;
}

bool match_binding_value_epoch_builder_rule_local_in_exclusive_frame(
        BindingValue left, Atom *right, BindingValueKind right_kind,
        const CettaOpenPatternPlan *right_plan,
        BindingsBuilder *bb, Arena *a, uint32_t right_epoch,
        BindingsExclusiveFrame *exclusive, bool linear) {
    if (!left.skeleton || !right || !bb || !a || right_epoch == 0u ||
        !binding_value_kind_is_contextual(right_kind) ||
        !exclusive || !exclusive->active ||
        exclusive->epoch != right_epoch)
        return false;
    MatchBareVariableStep bare = match_exclusive_bare_variable(
        left, right, right_plan, bb, a, right_epoch, exclusive);
    if (bare != MATCH_BARE_VARIABLE_DECLINE)
        return bare == MATCH_BARE_VARIABLE_BOUND;
    if (linear && right_plan) {
        return match_atoms_epoch_views_linear(
            left.skeleton, left.kind, left.epoch, 0u,
            right, NULL, bb, a, right_epoch,
            right_kind, true, NULL,
            right_plan, exclusive);
    }
    RuleFrameRegion frame_region;
    rule_frame_region_init_exclusive(
        &frame_region, right_plan, exclusive);
    return match_atoms_epoch_views_worklist(
        left.skeleton, left.kind, left.epoch, 0u,
        right, NULL, bb, a, right_epoch,
        right_kind, true, NULL, right_plan,
        &frame_region);
}

bool match_atoms_epoch_builder_rule_local_in_exclusive_frame(
        Atom *left, Atom *right, BindingValueKind right_kind,
        const CettaOpenPatternPlan *right_plan,
        BindingsBuilder *bb, Arena *a, uint32_t epoch,
        BindingsExclusiveFrame *exclusive, bool linear) {
    return match_binding_value_epoch_builder_rule_local_in_exclusive_frame(
        binding_value_from_atom(left), right, right_kind, right_plan,
        bb, a, epoch, exclusive, linear);
}

bool match_atoms_epoch_view_builder_rule_local_in_exclusive_frame(
        Atom *left, uint32_t left_epoch, uint32_t left_first_entry,
        Atom *right, BindingValueKind right_kind,
        const CettaOpenPatternPlan *right_plan,
        BindingsBuilder *bb, Arena *a, uint32_t right_epoch,
        BindingsExclusiveFrame *exclusive, bool linear) {
    if (!left || !right || !bb || !a || right_epoch == 0u ||
        !binding_value_kind_is_contextual(right_kind) ||
        !exclusive || !exclusive->active ||
        exclusive->epoch != right_epoch)
        return false;
    MatchBareVariableStep bare = match_exclusive_bare_variable(
        binding_value_from_context_kind(
            left, left_epoch, BINDING_VALUE_CONTEXTUAL),
        right, right_plan, bb, a, right_epoch, exclusive);
    if (bare != MATCH_BARE_VARIABLE_DECLINE)
        return bare == MATCH_BARE_VARIABLE_BOUND;
    if (linear && right_plan) {
        return match_atoms_epoch_views_linear(
            left, BINDING_VALUE_CONTEXTUAL,
            left_epoch, left_first_entry,
            right, NULL, bb, a, right_epoch,
            right_kind, true, NULL,
            right_plan, exclusive);
    }
    RuleFrameRegion frame_region;
    rule_frame_region_init_exclusive(
        &frame_region, right_plan, exclusive);
    return match_atoms_epoch_views_worklist(
        left, BINDING_VALUE_CONTEXTUAL,
        left_epoch, left_first_entry,
        right, NULL, bb, a, right_epoch,
        right_kind, true, NULL, right_plan,
        &frame_region);
}

bool match_atoms_activation_view_builder_rule_local_in_exclusive_frame(
        Atom *left, const BindingsActivationView *left_frame,
        Atom *right, BindingValueKind right_kind,
        const CettaOpenPatternPlan *right_plan,
        BindingsBuilder *bb, Arena *a, uint32_t right_epoch,
        BindingsExclusiveFrame *exclusive, bool linear) {
    if (!left || !left_frame || !right || !bb || !a ||
        !binding_value_kind_is_contextual(right_kind) ||
        !exclusive || !exclusive->active ||
        exclusive->epoch != right_epoch ||
        !bindings_activation_view_available(left_frame, &bb->current))
        return false;
    MatchBareVariableStep bare = match_exclusive_bare_variable(
        match_term_at(left, left_frame->source_kind, left_frame->epoch,
                      left_frame->first_entry, left_frame).value,
        right, right_plan, bb, a, right_epoch, exclusive);
    if (bare != MATCH_BARE_VARIABLE_DECLINE)
        return bare == MATCH_BARE_VARIABLE_BOUND;
    if (linear && right_plan) {
        return match_atoms_epoch_views_linear(
            left, left_frame->source_kind,
            left_frame->epoch, left_frame->first_entry,
            right, NULL, bb, a, right_epoch,
            right_kind, true, left_frame,
            right_plan, exclusive);
    }
    RuleFrameRegion frame_region;
    rule_frame_region_init_exclusive(
        &frame_region, right_plan, exclusive);
    return match_atoms_epoch_views_worklist(
        left, left_frame->source_kind,
        left_frame->epoch, left_frame->first_entry,
        right, NULL, bb, a, right_epoch,
        right_kind, true, left_frame, right_plan,
        &frame_region);
}

typedef struct {
    BindingValue left;
    AtomId right_id;
} StoredMatchPair;

typedef struct {
    StoredMatchPair *items;
    size_t len;
    size_t cap;
    StoredMatchPair inline_items[16];
} StoredMatchWorklist;

static bool stored_match_push(StoredMatchWorklist *work, BindingValue left,
                              AtomId right_id) {
    if (work->len == work->cap) {
        size_t next_cap = work->cap * 2u;
        if (next_cap <= work->cap ||
            next_cap > SIZE_MAX / sizeof(*work->items))
            return false;
        StoredMatchPair *next = cetta_malloc(
            sizeof(*work->items) * next_cap);
        memcpy(next, work->items, sizeof(*work->items) * work->len);
        if (work->items != work->inline_items) free(work->items);
        work->items = next;
        work->cap = next_cap;
    }
    work->items[work->len++] = (StoredMatchPair){left, right_id};
    return true;
}

static bool stored_grounded_equal(Atom *left,
                                  const TermUniverse *candidate_universe,
                                  AtomId right_id) {
    if (tu_kind(candidate_universe, right_id) != ATOM_GROUNDED)
        return false;
    {
        int right_kind = tu_ground_kind(candidate_universe, right_id);
        if (left->ground.gkind != right_kind) {
            int64_t right_int = right_kind == GV_INT
                ? tu_int(candidate_universe, right_id) : 0;
            double right_float = right_kind == GV_FLOAT
                ? tu_float(candidate_universe, right_id) : 0.0;
            return cetta_he_promoted_kind_equal(
                left->ground.gkind, left->ground.ival, left->ground.fval,
                right_kind, right_int, right_float);
        }
    }
    switch (left->ground.gkind) {
    case GV_INT:
        return tu_ground_kind(candidate_universe, right_id) == GV_INT &&
               left->ground.ival == tu_int(candidate_universe, right_id);
    case GV_FLOAT:
        return tu_ground_kind(candidate_universe, right_id) == GV_FLOAT &&
               left->ground.fval == tu_float(candidate_universe, right_id);
    case GV_BOOL:
        return tu_ground_kind(candidate_universe, right_id) == GV_BOOL &&
               left->ground.bval == tu_bool(candidate_universe, right_id);
    case GV_STRING: {
        const char *rhs = tu_string_cstr(candidate_universe, right_id);
        return tu_ground_kind(candidate_universe, right_id) == GV_STRING &&
               rhs && strcmp(left->ground.sval, rhs) == 0;
    }
    case GV_BIGINT: {
        const char *rhs = tu_bigint_cstr(candidate_universe, right_id);
        return tu_ground_kind(candidate_universe, right_id) == GV_BIGINT &&
               rhs &&
               cetta_bigint_compare_cstr(atom_bigint_cstr(left), rhs) == 0;
    }
    case GV_RATIONAL: {
        const char *rhs = tu_rational_cstr(candidate_universe, right_id);
        return tu_ground_kind(candidate_universe, right_id) == GV_RATIONAL &&
               rhs &&
               cetta_rational_compare_cstr(atom_rational_cstr(left), rhs) == 0;
    }
    case GV_SPACE:
    case GV_STATE:
    case GV_CAPTURE:
    case GV_BINDINGS:
    case GV_FOREIGN:
    case GV_PRIME_NEED_CAPABILITY:
    case GV_PRIME_CONTEXT:
    case GV_INTERNAL_TAG:
        return false;
    }
    return false;
}

/* A decoded TermUniverse node belongs to its persistent arena.  Keep open
 * nodes as syntax plus activation context; only a universe without a
 * decodable persistent node needs the arena-local materialized fallback. */
static bool term_universe_match_binding_value(
        const TermUniverse *universe, Arena *fallback,
        AtomId id, uint32_t epoch, BindingValue *out) {
    if (!universe || id == CETTA_ATOM_ID_NONE || !out)
        return false;
    bool open = tu_has_vars(universe, id);
    Atom *persistent = term_universe_get_atom(universe, id);
    if (persistent) {
        *out = open
            ? binding_value_from_persistent_context(persistent, epoch)
            : binding_value_from_atom(persistent);
        return true;
    }
    Atom *materialized = open
        ? term_universe_copy_atom_epoch(universe, fallback, id, epoch)
        : term_universe_copy_atom(universe, fallback, id);
    if (!materialized)
        return false;
    *out = binding_value_from_atom(materialized);
    return true;
}

static bool match_atoms_atom_id_epoch_worklist(
    BindingValue left_value, const TermUniverse *candidate_universe, AtomId right_id,
    Bindings *b, Arena *a, uint32_t epoch) {
    Atom *left = left_value.skeleton;
    if (!left || !candidate_universe || right_id == CETTA_ATOM_ID_NONE)
        return false;
    /* The encoded frontier does not keep an active substitution path. A
     * cyclic environment uses the existing guarded contextual matcher so
     * only cycles reachable from this comparison can reject it. */
    if (bindings_has_loop(b)) {
        Atom *right = term_universe_get_atom((TermUniverse *)candidate_universe, right_id);
        return right && match_decoded_atoms_worklist(
            left_value, binding_value_from_persistent_context(right, epoch),
            b, NULL, false);
    }
    StoredMatchWorklist work;
    work.items = work.inline_items;
    work.len = 0;
    work.cap = sizeof work.inline_items / sizeof work.inline_items[0];
    MatchWriteStamp attempt_start = match_write_stamp(b, 0u);
    bool attempt_repeated_var = false;
    if (!stored_match_push(&work, left_value, right_id)) goto fail;

    while (work.len > 0) {
        StoredMatchPair pair = work.items[--work.len];
        left_value = pair.left;
        right_id = pair.right_id;
        size_t dereferences = 0;
        size_t dereference_limit = bindings_dereference_limit(b);

retry_pair:
        left = left_value.skeleton;
        if (!tu_hdr(candidate_universe, right_id)) {
            Atom *right = term_universe_get_atom(
                (TermUniverse *)candidate_universe, right_id);
            if (!right || !match_atoms_epoch_views_worklist(
                    left, left_value.kind,
                    left_value.epoch, 0u, right, b, NULL, a, epoch,
                    BINDING_VALUE_CONTEXTUAL, false,
                    NULL, NULL, NULL))
                goto fail;
            continue;
        }
        AtomKind right_kind = tu_kind(candidate_universe, right_id);
        if (left->kind == ATOM_VAR) {
            BindingValue existing = bindings_lookup_value(b, left_value);
            if (existing.skeleton) {
                if (++dereferences > dereference_limit) goto fail;
                left_value = existing;
                goto retry_pair;
            }
            if (right_kind == ATOM_VAR) {
                VarId right_var_id = var_epoch_id(
                    tu_var_id(candidate_universe, right_id), epoch);
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_MATCH);
                BindingValue right_existing = bindings_lookup_value_id(b, right_var_id);
                if (right_existing.skeleton) {
                    if (!match_decoded_atoms_worklist(
                            left_value, right_existing, b, NULL, false)) {
                        attempt_repeated_var = true;
                        goto fail;
                    }
                    continue;
                }
                if (binding_value_variable_id(left_value) == right_var_id) continue;
                BindingValue value = binding_value_from_atom(NULL);
                if (!term_universe_match_binding_value(
                        candidate_universe, a, right_id, epoch, &value) ||
                    !bindings_add_internal(
                        b, binding_value_variable_id(left_value), left->sym_id,
                        left->name_key, value, true)) goto fail;
                continue;
            }
            BindingValue value = binding_value_from_atom(NULL);
            if (!term_universe_match_binding_value(
                    candidate_universe, a, right_id, epoch, &value) ||
                !bindings_add_internal(
                        b, binding_value_variable_id(left_value), left->sym_id,
                        left->name_key, value, true)) goto fail;
            continue;
        }
        if (right_kind == ATOM_VAR) {
            VarId right_var_id = var_epoch_id(
                tu_var_id(candidate_universe, right_id), epoch);
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_MATCH);
            BindingValue existing = bindings_lookup_value_id(b, right_var_id);
            if (existing.skeleton) {
                if (!match_decoded_atoms_worklist(
                        left_value, existing, b, NULL, false)) {
                    attempt_repeated_var = true;
                    goto fail;
                }
                continue;
            }
            BindingValue binding_var = binding_value_from_atom(NULL);
            if (!term_universe_match_binding_value(
                    candidate_universe, a, right_id, epoch,
                    &binding_var) ||
                !binding_var.skeleton ||
                binding_var.skeleton->kind != ATOM_VAR ||
                !bindings_add_internal(
                    b, binding_value_variable_id(binding_var),
                    binding_var.skeleton->sym_id,
                    binding_var.skeleton->name_key,
                    left_value, true))
                goto fail;
            continue;
        }
        switch (left->kind) {
        case ATOM_SYMBOL:
            if (right_kind != ATOM_SYMBOL ||
                left->sym_id != tu_sym(candidate_universe, right_id))
                goto fail;
            break;
        case ATOM_VAR:
            goto fail;
        case ATOM_GROUNDED:
            if (!stored_grounded_equal(left, candidate_universe, right_id))
                goto fail;
            break;
        case ATOM_EXPR:
            if (right_kind != ATOM_EXPR ||
                left->expr.len != tu_arity(candidate_universe, right_id))
                goto fail;
            for (CettaExprIndex i = left->expr.len; i > 0; i--) {
                CettaExprIndex child = i - 1u;
                BindingValue child_value = left_value;
                child_value.skeleton = left->expr.elems[child];
                if (!stored_match_push(
                        &work, child_value,
                        tu_child(candidate_universe, right_id, child)))
                    goto fail;
            }
            break;
        }
    }
    if (work.items != work.inline_items) free(work.items);
    match_note_unification_attempt(
        true, match_write_stamp_changed(
                  attempt_start, match_write_stamp(b, 0u)), false);
    return true;

fail:
    if (work.items != work.inline_items) free(work.items);
    match_note_unification_attempt(
        false, match_write_stamp_changed(
                   attempt_start, match_write_stamp(b, 0u)),
        attempt_repeated_var);
    return false;
}

typedef struct {
    Bindings *other;
    bool equal;
} BindingsEqualityContext;

static bool bindings_current_binding_equal_in_other(
        const Binding *binding, void *raw_context) {
    BindingsEqualityContext *context = raw_context;
    BindingValue other = bindings_lookup_value_id(
        context->other, binding->var_id);
    context->equal = binding_value_equal(other, binding->value);
    return context->equal;
}

bool bindings_eq(Bindings *a, Bindings *b) {
    if (!(prime_need_snapshot_is_ancestor(bindings_need_view(a),
                                          bindings_need_view(b)) &&
          prime_need_snapshot_is_ancestor(bindings_need_view(b),
                                          bindings_need_view(a))))
        return false;
    if (!prime_need_branch_state_equal(bindings_branch_state_view(a),
                                       bindings_branch_state_view(b)))
        return false;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if (!prime_need_receipt_equal(bindings_receipt_view(a),
                                  bindings_receipt_view(b)))
        return false;
#endif
    if (bindings_occurrence_token(a) != bindings_occurrence_token(b))
        return false;
    size_t a_binding_count = 0u;
    size_t b_binding_count = 0u;
    if (!bindings_current_binding_count(a, &a_binding_count) ||
        !bindings_current_binding_count(b, &b_binding_count) ||
        a_binding_count != b_binding_count)
        return false;
    if (a->eq_len != b->eq_len)
        return false;
    BindingsEqualityContext equality = {
        .other = b,
        .equal = true,
    };
    if (!bindings_for_each_current_binding(
            a, bindings_current_binding_equal_in_other, &equality) ||
        !equality.equal) {
        return false;
    }
    bool matched_stack[BINDINGS_TEMP_STACK_CAP];
    bool *matched = NULL;
    if (b->eq_len > 0) {
        matched = b->eq_len <= BINDINGS_TEMP_STACK_CAP
            ? matched_stack
            : cetta_malloc(sizeof(bool) * b->eq_len);
        memset(matched, 0, sizeof(bool) * b->eq_len);
    }
    for (uint32_t i = 0; i < a->eq_len; i++) {
        bool found = false;
        for (uint32_t j = 0; j < b->eq_len; j++) {
            if (!matched[j] &&
                constraint_pair_eq(&a->constraints[i], &b->constraints[j])) {
                matched[j] = true;
                found = true;
                break;
            }
        }
        if (!found) {
            if (matched && matched != matched_stack)
                free(matched);
            return false;
        }
    }
    if (matched && matched != matched_stack)
        free(matched);
    return true;
}
