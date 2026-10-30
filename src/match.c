#include "match.h"
#include "binding/term_cursor.h"
#include "eval.h"
#include "binding/slot_store_internal.h"
#include "stats.h"
#include "native_cost.h"
#include "term_universe.h"
#include "term_canon.h"
#include "variant_shape.h"
#include <inttypes.h>
#include <assert.h>
#include <pthread.h>
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
    CettaFrameIdentity epoch;
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
static _Thread_local bool g_match_dag_registered;
static pthread_key_t g_match_dag_key;
static pthread_once_t g_match_dag_key_once = PTHREAD_ONCE_INIT;
static bool g_match_dag_key_ready;

/* These buffers contain borrowed pointers only. Keep them between worker
 * episodes, but release their allocations when the owning thread retires. */
static void match_dag_thread_release(void *unused) {
    (void)unused;
    free(g_match_dag_stack);
    free(g_match_dag_keys);
    free(g_match_dag_colors);
    free(g_match_dag_stamp);
    g_match_dag_stack = NULL;
    g_match_dag_keys = NULL;
    g_match_dag_colors = NULL;
    g_match_dag_stamp = NULL;
    g_match_dag_stack_cap = 0u;
    g_match_dag_hash_cap = 0u;
    g_match_dag_hash_used = 0u;
    g_match_dag_epoch = 0u;
    g_match_dag_registered = false;
}

static void match_dag_key_create(void) {
    g_match_dag_key_ready =
        pthread_key_create(&g_match_dag_key, match_dag_thread_release) == 0;
}

static bool match_dag_thread_register(void) {
    if (g_match_dag_registered)
        return true;
    pthread_once(&g_match_dag_key_once, match_dag_key_create);
    if (!g_match_dag_key_ready ||
        pthread_setspecific(g_match_dag_key, &g_match_dag_registered) != 0)
        return false;
    g_match_dag_registered = true;
    return true;
}

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
        if (!match_dag_thread_register())
            return false;
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

static bool match_occurrence_is_finite_dag_qualified(
        const Atom *root, bool require_closed_immutable) {
    uint32_t sp = 0u;
    if (!root)
        return false;
    if (require_closed_immutable &&
        (root->kind == ATOM_VAR ||
         !(root->flags & ATOM_FLAG_HASHCONS_ELIGIBLE)))
        return false;
    if (root->kind != ATOM_EXPR)
        return true;
    if (g_match_dag_stack_cap == 0u) {
        if (!match_dag_thread_register())
            return false;
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
        if (require_closed_immutable &&
            (!child || child->kind == ATOM_VAR ||
             !(child->flags & ATOM_FLAG_HASHCONS_ELIGIBLE)))
            return false;
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

static bool match_occurrence_is_finite_dag(const Atom *root) {
    return match_occurrence_is_finite_dag_qualified(root, false);
}

/* One environment, one occurrence: the substitution is the identity
 * (SubstitutionAlgebra.subst_empty).  Distinct activation epochs keep
 * distinct keys.  A raw expression cycle still fails the finite-occurrence
 * audit (the path set, not this decision). */
static inline bool match_shared_occurrence_same_environment(
        bool left_original, bool right_original,
        CettaFrameIdentity left_epoch, CettaFrameIdentity right_epoch) {
    if (left_original != right_original)
        return false;
    if (left_original && !cetta_frame_identity_eq(left_epoch, right_epoch))
        return false;
    return true;
}

typedef enum {
    MATCH_REFLEXIVITY_DECLINED,
    MATCH_REFLEXIVITY_CERTIFIED,
    MATCH_REFLEXIVITY_FINITE_AUDIT
} MatchReflexivityAdmission;

static inline void match_shared_reflexivity_decline(const Atom *root) {
    cetta_runtime_stats_inc(atom_has_vars(root)
        ? CETTA_RUNTIME_COUNTER_MATCH_SHARED_GROUND_REFLEXIVITY_OPEN_DECLINE
        : CETTA_RUNTIME_COUNTER_MATCH_SHARED_GROUND_REFLEXIVITY_UNCERTIFIED_DECLINE);
}

static inline void match_shared_reflexivity_commit(const Atom *root) {
    cetta_runtime_stats_inc(atom_has_vars(root)
        ? CETTA_RUNTIME_COUNTER_MATCH_SHARED_OPEN_REFLEXIVITY_COMMIT
        : CETTA_RUNTIME_COUNTER_MATCH_SHARED_GROUND_REFLEXIVITY_COMMIT);
}

/* Admission reads only the shared root's metadata. The caller either already
 * owns a finite-occurrence certificate or must obtain one before committing
 * reflexivity; a retained matcher can schedule that audit independently. */
static inline MatchReflexivityAdmission match_shared_reflexivity_admit(
        Atom *left, Atom *right, bool allow_eval_heap,
        bool same_environment, const Bindings *bindings) {
    if (left != right || !left || left->kind != ATOM_EXPR)
        return MATCH_REFLEXIVITY_DECLINED;
    /* Identity is not reflexive for an HE value containing NaN. */
    if (atom_structural_may_have_nan(left) && eval_current_language_id &&
        eval_current_language_id() == CETTA_LANGUAGE_HE)
        return MATCH_REFLEXIVITY_DECLINED;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_MATCH_SHARED_GROUND_REFLEXIVITY_ATTEMPT);
    if (atom_has_vars(left)) {
        if (!same_environment ||
            (bindings && bindings->cycle_state != BINDINGS_CYCLE_ACYCLIC) ||
            !match_shared_ground_reflexivity_enabled()) {
            match_shared_reflexivity_decline(left);
            return MATCH_REFLEXIVITY_DECLINED;
        }
        return MATCH_REFLEXIVITY_FINITE_AUDIT;
    }
    if (!match_shared_ground_reflexivity_enabled())
        return MATCH_REFLEXIVITY_DECLINED;
    if (match_shared_ground_reflexivity_certified(left))
        return MATCH_REFLEXIVITY_CERTIFIED;
    if (!allow_eval_heap) {
        match_shared_reflexivity_decline(left);
        return MATCH_REFLEXIVITY_DECLINED;
    }
    return MATCH_REFLEXIVITY_FINITE_AUDIT;
}

static inline bool match_shared_ground_reflexivity_try(
        Atom *left, Atom *right, bool allow_eval_heap,
        bool same_environment, const Bindings *bindings) {
    MatchReflexivityAdmission admission = match_shared_reflexivity_admit(
        left, right, allow_eval_heap, same_environment, bindings);
    if (admission == MATCH_REFLEXIVITY_DECLINED) return false;
    if (admission == MATCH_REFLEXIVITY_FINITE_AUDIT &&
        !match_occurrence_identity_finite(left)) {
        match_shared_reflexivity_decline(left);
        return false;
    }
    match_shared_reflexivity_commit(left);
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
    *equal_out = atom_eq_with_resolved_spaces(left, right);
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
    /* atom_hash describes representation. HE may equate an integer with a
     * floating value whose representation hash differs, so this shortcut's
     * negative hash decision is unavailable in that dialect. */
    if (!left || !right || !equal_out ||
        (eval_current_language_id &&
         eval_current_language_id() == CETTA_LANGUAGE_HE) ||
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
static void bindings_builder_drop_prime_trail(BindingsBuilder *builder, uint32_t length) {
    assert(length<=builder->prime_trail_len);
    while (builder->prime_trail_len>length)
        prime_occurrence_clear(&builder->prime_trail[--builder->prime_trail_len]);
}

static bool bindings_builder_snapshot(
    BindingsBuilder *bb, bool *created_out);
static bool bindings_alias_keeps_caller_identity(
        VarId id, BindingValue value, CettaFrameIdentity identity, bool *cross_frame_alias) {
    if (!value.skeleton || value.skeleton->kind != ATOM_VAR)
        return true;
    bool key_local = cetta_frame_identity_eq(var_epoch_suffix(id), identity);
    bool value_local = cetta_frame_identity_eq(var_epoch_suffix(binding_value_variable_id(value)), identity);
    if (key_local != value_local && cross_frame_alias)
        *cross_frame_alias = true;
    return key_local || !value_local;
}

bool bindings_builder_aliases_normalized_since(
        const BindingsBuilder *builder, uint32_t trail_mark,
        uint32_t first_entry, CettaFrameIdentity identity, bool *cross_frame_alias) {
    if (cross_frame_alias)
        *cross_frame_alias = false;
    if (!builder || cetta_frame_identity_is_none(identity) ||
        trail_mark > builder->trail_len ||
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
        VarId id = var_epoch_id(var_id_from_base(undo->source_id),
                            undo->frame_ref.identity);
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

static Atom g_rename_walk_active;
static Atom g_rename_walk_complete;

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


static void rename_walk_stack_init(RenameWalkStack *stack);
static void rename_walk_stack_free(RenameWalkStack *stack);
static bool rename_walk_stack_push(RenameWalkStack *stack,
                                   RenameWalkTask task);
static void freshen_epoch_memo_init(FreshenEpochMemo *memo);
static void freshen_epoch_memo_free(FreshenEpochMemo *memo);
static Atom *freshen_epoch_memo_lookup(FreshenEpochMemo *memo,
                                       const Atom *src);
static bool freshen_epoch_memo_store(FreshenEpochMemo *memo,
                                     const Atom *src, Atom *dst);


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
    return var_id_eq(lhs, rhs);
}

static bool binding_id_has_frame(VarId id) {
    return !cetta_frame_identity_is_none(var_epoch_suffix(id)) && !variant_private_var_id(id);
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
                    count += atom_contains_private_variant_var(bindings_frame_slot_value(frame, slot).skeleton);
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
    /* Both words mixed before the power-of-two tables select their low
     * address bits. */
    uint64_t x = var_id_mix(id);
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

void bindings_exclusive_frame_storage_census(const BindingsExclusiveFrame *frame,
        CettaStorageCensus *census) {
    if (!frame || !cetta_storage_object(census, frame, sizeof(*frame), CETTA_STORAGE_BLOCK))
        return;
    if (frame->write_len > frame->write_cap ||
        (frame->value_cap && !frame->values) || (frame->write_cap && !frame->writes)) {
        cetta_storage_refuse(census, "ExclusiveFrameStorageShape"); return;
    }
    cetta_storage_array(census, frame->values, frame->value_cap, sizeof(*frame->values), 0u);
    cetta_storage_array(census, frame->writes, frame->write_cap, sizeof(*frame->writes), 0u);
    /* The compiled equation lends the schema; stored terms borrow the
     * enclosing machine's retained arenas, just as during publication. */
}

bool bindings_exclusive_frame_begin(
        BindingsExclusiveFrame *frame, BindingsFrameSchema *schema,
        CettaFrameIdentity epoch) {
    if (!frame || !schema || cetta_frame_identity_is_none(epoch))
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
        CettaFrameIdentity epoch) {
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
    return frame && frame->active && !cetta_frame_identity_is_none(frame->epoch) &&
        cetta_frame_identity_eq(var_epoch_suffix(id), frame->epoch) &&
        bindings_frame_schema_find_slot(
            frame->schema, var_source_id(id), slot_out);
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
    if (!frame || !frame->active || cetta_frame_identity_is_none(frame->epoch))
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
        bool entry_is_local = cetta_frame_identity_eq(var_epoch_suffix(id), frame->epoch);
        bool value_is_local = value.skeleton &&
            value.skeleton->kind == ATOM_VAR &&
            cetta_frame_identity_eq(var_epoch_suffix(binding_value_variable_id(value)), frame->epoch);
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
    if (!var_id_is_none(singleton)) {
        CettaFrameIdentity epoch = binding_value_is_contextual(value)
            ? binding_value_epoch(value) : var_epoch_suffix(singleton);
        if (!cetta_frame_identity_is_none(epoch) && !variant_private_var_id(singleton)) {
            inline_ids[0] = var_epoch_id(
                var_source_id(singleton), epoch);
            *len_out = 1u;
        }
        return true;
    }
    RenameWalkStack stack;
    FreshenEpochMemo states;
    rename_walk_stack_init(&stack);
    freshen_epoch_memo_init(&states);
    VarId *ids = inline_ids;
    size_t id_len = 0u;
    size_t id_cap = inline_cap;
    /* Past a few identifiers, whether one is already known is asked of an
     * open-addressing set of them, not of the list: a value may mention many
     * frame coordinates.  The list keeps the order they were found in. */
    enum { FRAME_ID_SCAN_LIMIT = 16u };
    VarId *known_set = NULL;
    size_t known_mask = 0u;
    if (!rename_walk_stack_push(
            &stack, (RenameWalkTask){RENAME_WALK_ENTER, value.skeleton}))
        goto fail;
    while (stack.len > 0u) {
        RenameWalkTask task = stack.tasks[--stack.len];
        Atom *atom = task.atom;
        if (!atom || !atom_has_vars(atom))
            continue;
        /* The same exact constructor fact applies below a mixed root: a
         * singleton subtree contributes one qualified coordinate. */
        VarId subtree_support = atom_single_variable_id(atom);
        if (!var_id_is_none(subtree_support)) {
            CettaFrameIdentity epoch = binding_value_is_contextual(value)
                ? binding_value_epoch(value) : var_epoch_suffix(subtree_support);
            if (cetta_frame_identity_is_none(epoch) || variant_private_var_id(subtree_support))
                continue;
            VarId id = var_epoch_id(
                var_source_id(subtree_support), epoch);
            bool known = false;
            if (!known_set) {
                for (size_t index = 0u; index < id_len; index++) {
                    if (var_id_eq(ids[index], id)) {
                        known = true;
                        break;
                    }
                }
            } else {
                size_t slot = bindings_var_id_hash(id) & known_mask;
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_ID_SET_LOOKUP);
                while (!var_id_is_none(known_set[slot]) && !var_id_eq(known_set[slot], id)) {
                    cetta_runtime_stats_inc(
                        CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_ID_SET_PROBE);
                    slot = (slot + 1u) & known_mask;
                }
                known = var_id_eq(known_set[slot], id);
                if (!known)
                    known_set[slot] = id;
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
            /* The set holds every identifier found so far, at most half
             * full; it starts when the list outgrows a scan. */
            if (id_len >= FRAME_ID_SCAN_LIMIT &&
                (!known_set || id_len * 2u > known_mask + 1u)) {
                size_t capacity = known_mask ? (known_mask + 1u) * 2u : 64u;
                while (capacity < id_len * 2u)
                    capacity *= 2u;
                if (capacity > SIZE_MAX / sizeof(*known_set))
                    goto fail;
                VarId *grown = cetta_malloc(capacity * sizeof(*grown));
                for (size_t slot = 0u; slot < capacity; slot++)
                    grown[slot] = VAR_ID_NONE;
                size_t mask = capacity - 1u;
                for (size_t index = 0u; index < id_len; index++) {
                    size_t slot = bindings_var_id_hash(ids[index]) & mask;
                    while (!var_id_is_none(grown[slot]))
                        slot = (slot + 1u) & mask;
                    grown[slot] = ids[index];
                }
                free(known_set);
                known_set = grown;
                known_mask = mask;
            }
            continue;
        }
        if (atom->kind != ATOM_EXPR || atom->expr.len == 0u)
            continue;
        if (task.kind == RENAME_WALK_LEAVE) {
            if (!freshen_epoch_memo_store(
                    &states, atom, &g_rename_walk_complete))
                goto fail;
            continue;
        }
        Atom *state = freshen_epoch_memo_lookup(&states, atom);
        if (state == &g_rename_walk_active)
            goto fail;
        if (state == &g_rename_walk_complete)
            continue;
        if (!freshen_epoch_memo_store(
                &states, atom, &g_rename_walk_active) ||
            !rename_walk_stack_push(
                &stack, (RenameWalkTask){RENAME_WALK_LEAVE, atom}))
            goto fail;
        /* Keep the former right-to-left first-discovery order, visiting
         * a shared expression once in this fixed lexical context. The
         * enter/leave states also reject a malformed raw back edge. */
        for (CettaExprIndex child = 0u;
             child < atom->expr.len; child++) {
            if (!rename_walk_stack_push(
                    &stack, (RenameWalkTask){RENAME_WALK_ENTER,
                                             atom->expr.elems[child]}))
                goto fail;
        }
    }
    freshen_epoch_memo_free(&states);
    rename_walk_stack_free(&stack);
    free(known_set);
    *ids_out = ids;
    *len_out = id_len;
    return true;

fail:
    freshen_epoch_memo_free(&states);
    rename_walk_stack_free(&stack);
    if (ids != inline_ids)
        free(ids);
    free(known_set);
    return false;
}

#ifdef CETTA_TEST_HOOKS
bool bindings_frame_support_test(BindingValue value, VarId *out, size_t capacity,
                                 size_t *count_out) {
    VarId inline_ids[16];
    VarId *ids = inline_ids;
    size_t count = 0u;
    bool ok = count_out && (out || capacity == 0u) &&
        bindings_value_collect_frame_ids(value, &ids, &count, inline_ids,
            sizeof(inline_ids) / sizeof(inline_ids[0]));
    if (ok) {
        *count_out = count;
        ok = count <= capacity;
        if (ok && count != 0u)
            memcpy(out, ids, count * sizeof(*out));
    }
    if (ids != inline_ids)
        free(ids);
    return ok;
}
#endif

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
    if (binding_value_is_contextual(value) && !cetta_frame_identity_is_none(binding_value_epoch(value)) &&
        bindings_frame_index_epoch_complete(bindings, binding_value_epoch(value))) {
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
           !var_id_eq(index->single_cached_ids[cursor], slot->id)) {
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
    if (!index || var_id_is_none(id) || index->capacity == 0u)
        return false;
    size_t mask = index->capacity - 1u;
    size_t slot = bindings_var_id_hash(id) & mask;
    while (!var_id_is_none(index->slots[slot].id) &&
           !binding_var_eq(index->slots[slot].id, id)) {
        slot = (slot + 1u) & mask;
    }
    if (var_id_is_none(index->slots[slot].id)) {
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
        if (var_id_is_none(old_slots[i].id))
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
    if (!index || var_id_is_none(id) || index->capacity == 0u)
        return false;
    size_t mask = index->capacity - 1u;
    size_t slot = bindings_var_id_hash(id) & mask;
    while (!var_id_is_none(index->slots[slot].id)) {
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
    if (!index || var_id_is_none(id) || index->capacity == 0u)
        return;
    size_t mask = index->capacity - 1u;
    size_t hole = bindings_var_id_hash(id) & mask;
    while (!var_id_is_none(index->slots[hole].id) &&
           !binding_var_eq(index->slots[hole].id, id)) {
        hole = (hole + 1u) & mask;
    }
    if (var_id_is_none(index->slots[hole].id))
        return;

    size_t scan = (hole + 1u) & mask;
    while (!var_id_is_none(index->slots[scan].id)) {
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
        const Bindings *bindings, CettaFrameIdentity epoch,
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
        const Bindings *bindings, CettaFrameIdentity epoch,
        BindingsFrameRef *ref_out) {
    if (ref_out)
        *ref_out = (BindingsFrameRef){0};
    if (!bindings || !ref_out || cetta_frame_identity_is_none(epoch))
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
        CettaFrameIdentity *epoch_out) {
    if (epoch_out)
        *epoch_out = CETTA_FRAME_IDENTITY_NONE;
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
        cetta_frame_identity_is_none(var_epoch_suffix(var_id))
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
            if (!bindings_frame_slot_value(frame, slot).skeleton)
                continue;
            *binding_out = (Binding){
                .var_id = var_epoch_id(bindings_frame_slot_source_id(frame, slot), frame->epoch),
                .spelling = bindings_frame_slot_spelling(frame, slot),
                .name_key = bindings_frame_slot_name_key(frame, slot),
                .value = bindings_frame_slot_value(frame, slot),
            };
            return true;
        }
        iterator->frame++;
        iterator->slot = 0u;
    }
    return false;
}

bool bindings_frame_state_directory(const Bindings *bindings,
                                    BindingsFrameDirectoryStateView *view) {
    if (!bindings || !view)
        return false;
    const BindingsFrameIndex *index = bindings->frame_index;
    *view = (BindingsFrameDirectoryStateView){
        .bindings = bindings,
        .directory = index,
        .frame_count = index ? index->len : 0u,
        .schema_revision = index ? index->schema_revision : 0u,
        .write_clock = index ? index->write_clock : 0u,
    };
    return true;
}

bool bindings_frame_state_count(const Bindings *bindings, uint32_t *count_out) {
    BindingsFrameDirectoryStateView view;
    if (!count_out || !bindings_frame_state_directory(bindings, &view))
        return false;
    *count_out = view.frame_count;
    return true;
}

bool bindings_frame_state_next(BindingsFrameStateIterator *iterator,
                                BindingsFrameStateView *view) {
    if (!iterator || !iterator->bindings || !view)
        return false;
    const BindingsFrameIndex *index = iterator->bindings->frame_index;
    if (!index || iterator->frame >= index->len)
        return false;
    const BindingsFrameIndexEntry *frame = &index->frames[iterator->frame];
    BindingsFrameRef ref = bindings_frame_ref_from_entry(index, frame);
    if (!bindings_frame_ref_is_valid(ref))
        return false;
    *view = (BindingsFrameStateView){
        .bindings = iterator->bindings,
        .directory = index,
        .ref = ref,
        .slot_count = frame->slot_len,
        .schema_slot_count = frame->schema->len,
        .schema_revision = index->schema_revision,
        .write_clock = index->write_clock,
        .activation_write_boundary = frame->activation_write_boundary,
        .has_activation_write_boundary = frame->has_activation_write_boundary,
        .schema_complete = frame->schema_complete,
        .owns_identity = frame->owns_identity,
    };
    iterator->frame++;
    return true;
}

bool bindings_frame_state_read_slot(const BindingsFrameStateView *view,
                                    uint32_t slot, BindingsFrameSlotStateView *out) {
    if (!view || !view->bindings || !out || slot >= view->slot_count)
        return false;
    const BindingsFrameIndex *index = view->bindings->frame_index;
    if (!index || index != view->directory ||
        index->schema_revision != view->schema_revision ||
        index->write_clock != view->write_clock)
        return false;
    const BindingsFrameIndexEntry *frame = bindings_frame_index_find_ref_const(index, view->ref);
    if (!frame || frame->slot_len != view->slot_count)
        return false;
    VarId source = bindings_frame_slot_source_id(frame, slot);
    bool declared = slot < frame->schema->len;
    *out = (BindingsFrameSlotStateView){
        .source_id = source,
        .var_id = var_epoch_id(source, frame->epoch),
        .spelling = declared ? bindings_frame_slot_spelling(frame, slot) : SYMBOL_ID_NONE,
        .name_key = declared ? bindings_frame_slot_name_key(frame, slot) : NULL,
        .declared_in_schema = declared,
        .value = bindings_frame_slot_value(frame, slot),
        .write_version = bindings_frame_slot_version(frame, slot),
    };
    return true;
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

bool bindings_has_frame_authority(const Bindings *bindings) {
    return bindings && bindings->frame_index && bindings->frame_index->len != 0u;
}

bool bindings_retain_frame_state(Bindings *dst, const Bindings *src) {
    if (!dst || !src)
        return false;
    if (dst == src || !bindings_has_frame_authority(src))
        return true;
    if (!bindings_frame_index_merge_schemas(dst, src) ||
        !bindings_frame_index_detach(dst))
        return false;
    if (dst->frame_index->write_clock < src->frame_index->write_clock)
        dst->frame_index->write_clock = src->frame_index->write_clock;
    for (uint32_t i = 0u; i < src->frame_index->len; i++) {
        const BindingsFrameIndexEntry *source = &src->frame_index->frames[i];
        BindingsFrameIndexEntry *target =
            bindings_frame_index_find_frame(dst->frame_index, source->epoch);
        if (!target || !bindings_frame_index_entry_detach(target))
            return false;
        target->activation_write_boundary = source->activation_write_boundary;
        target->has_activation_write_boundary = source->has_activation_write_boundary;
        for (uint32_t slot = 0u; slot < source->slot_len; slot++) {
            uint32_t target_slot = 0u;
            if (!bindings_frame_index_find_slot(target,
                    bindings_frame_slot_source_id(source, slot), &target_slot))
                return false;
            if (!bindings_frame_index_replace_slot_version(
                    target, target_slot, bindings_frame_slot_version(source, slot)))
                return false;
        }
    }
    return true;
}

size_t bindings_frame_binding_count(const Bindings *bindings, BindingsFrameRef ref) {
    const BindingsFrameIndexEntry *frame = bindings
        ? bindings_frame_index_find_ref_const(bindings->frame_index, ref) : NULL;
    return frame ? frame->bound_value_count : 0u;
}

static bool bindings_resolve_value_root(
        Bindings *bindings, BindingValue value, BindingValue *out,
        bool count_apply_lookups) {
    if (!out || !value.skeleton)
        return false;
    if (!bindings_has_bound_values(bindings)) {
        *out = value;
        return true;
    }
    size_t remaining = bindings_dereference_limit(bindings);
    while (value.skeleton->kind == ATOM_VAR) {
        VarId id = binding_value_variable_id(value);
        if (count_apply_lookups)
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_APPLY);
        BindingValue next = bindings_lookup_value(bindings, value);
        if (!next.skeleton ||
            (next.skeleton->kind == ATOM_VAR && var_id_eq(binding_value_variable_id(next), id)))
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
    return bindings_resolve_value_root(bindings, value, out, false);
}

bool bindings_observe_value_root(
        const Bindings *bindings, BindingValue source, BindingValue *out) {
    if (!out)
        return false;
    *out = binding_value_from_atom(NULL);
    if ((bindings && bindings->cycle_state != BINDINGS_CYCLE_ACYCLIC) ||
        binding_value_kind(source) > BINDING_VALUE_CONTEXTUAL_PERSISTENT)
        return false;
    BindingValue value;
    if (!bindings_resolve_value_root((Bindings *)bindings, source, &value, false) ||
        binding_value_kind(value) > BINDING_VALUE_CONTEXTUAL_PERSISTENT ||
        (binding_value_is_contextual(value) && atom_has_vars(value.skeleton) &&
         (!bindings || !bindings_frame_index_find_ref_const(
             bindings->frame_index, binding_value_frame_ref(value)))))
        return false;
    *out = value;
    return true;
}

bool bindings_resolve_value_exact(
        Bindings *bindings, BindingValue value, BindingValue *out) {
    return bindings_resolve_value_root(bindings, value, out, false);
}

static bool binding_value_may_contain_unbound_var(Bindings *b, BindingValue value);
static bool binding_values_eq_under_bindings(Bindings *b, BindingValue lhs, BindingValue rhs);
static bool binding_values_eq_under_bindings_checked(
    Bindings *bindings, BindingValue lhs, BindingValue rhs, CettaMatchCompletion *failure);
static bool binding_value_match_equal(BindingValue left, BindingValue right);

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
                                         Arena *a, bool undefined_is_wildcard);

static bool match_decoded_atoms_worklist_checked(
    BindingValue left, BindingValue right, Bindings *bindings,
    BindingsBuilder *builder, Arena *arena, bool undefined_is_wildcard,
    CettaMatchCompletion *failure);
static bool bindings_builder_try_merge_kernel(
    BindingsBuilder *builder, const Bindings *source,
    CettaMatchCompletion *failure);

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
    return !var_id_is_none(id) && binding_value_is_contextual(value)
        ? var_epoch_id(id, binding_value_epoch(value)) : id;
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
    if (!bindings || var_id_is_none(start) || var_id_is_none(target) ||
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
            if (var_id_is_none(slot->single_terminal))
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
        if (var_id_is_none(single)) {
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
                if (bindings_frame_slot_value(frame, slot).skeleton) {
                    bindings_rhs_variable_bloom_add(
                        bindings, bindings_frame_slot_value(frame, slot));
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
    if (!var_id_is_none(single)) {
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

const CettaWeightLedger *bindings_weight_view(const Bindings *b) {
    return b && b->prime_ext ? &b->prime_ext->weights
                            : &g_prime_occurrence_empty.weights;
}

static PrimeOccurrence *bindings_prime_ext_materialize(Bindings *b) {
    if (!b->prime_ext) {
        PrimeOccurrence *ext = cetta_malloc(sizeof(PrimeOccurrence));
        prime_occurrence_init(ext);
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

CettaWeightLedger *bindings_weight_mut(Bindings *b) {
    return &bindings_prime_ext_materialize(b)->weights;
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
           b->prime_ext->weights.length != 0u ||
           b->prime_ext->occurrence_token != 0u ||
           b->prime_ext->reader_context != NULL
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
           || prime_need_receipt_present(&b->prime_ext->receipt)
#endif
           ;
}

PrimeNeedReaderContext *bindings_reader_context_view(const Bindings *bindings) {
    return bindings && bindings->prime_ext ? bindings->prime_ext->reader_context : NULL;
}

void bindings_reader_context_set(Bindings *bindings, PrimeNeedReaderContext *context) {
    if (!bindings || bindings_reader_context_view(bindings)==context) return;
    if (!context && !bindings->prime_ext) return;
    prime_need_reader_context_retain(context);
    PrimeOccurrence *occurrence=bindings_prime_ext_materialize(bindings);
    PrimeNeedReaderContext *old=occurrence->reader_context;
    occurrence->reader_context=context;
    prime_need_reader_context_release(old);
}

void bindings_prime_assign(Bindings *dst, const Bindings *src) {
    if (bindings_prime_present(src))
        prime_occurrence_assign(bindings_prime_ext_materialize(dst),src->prime_ext);
    else if (dst->prime_ext) prime_occurrence_clear(dst->prime_ext);
}

void bindings_prime_set(Bindings *dst, const PrimeNeedSnapshot *need,
                        const PrimeNeedBranchState *branch_state,
                        uint64_t occurrence_token,
                        const PrimeNeedReceipt *receipt,
                        const CettaWeightLedger *weights) {
    bool present = (need && prime_need_snapshot_present(need)) ||
                   (branch_state &&
                    prime_need_branch_state_present(branch_state)) ||
                   occurrence_token != 0u || (weights && weights->length);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    present = present ||
              (receipt && prime_need_receipt_present(receipt));
#else
    (void)receipt;
#endif
    if (present) {
        PrimeOccurrence value={.prime_need=need?*need:(PrimeNeedSnapshot){0},
            .branch_state=branch_state?*branch_state:(PrimeNeedBranchState){0},
            .occurrence_token=occurrence_token,.weights=weights?*weights:(CettaWeightLedger){0}};
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
        if (receipt) value.receipt=*receipt;
#endif
        prime_occurrence_assign(bindings_prime_ext_materialize(dst),&value);
    } else if (dst->prime_ext) prime_occurrence_clear(dst->prime_ext);
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
        prime_occurrence_clear(b->prime_ext);
        free(b->prime_ext);
        b->prime_ext = NULL;
    }
    if (b->owners)
        bindings_owners_release(b->owners);
    bindings_init(b);
}

static bool bindings_fork_version(Bindings *dst, const Bindings *src);

bool bindings_clone(Bindings *dst, const Bindings *src) {
    cetta_native_cost_charge(CETTA_COST_BINDING_CLONE, 1u);
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
        for (uint32_t slot = 0u; slot < frame->schema->cap; slot++)
            has_key = has_key || bindings_frame_slot_name_key(frame, slot) != NULL;
        if (!has_key)
            continue;
        BindingsFrameSchema *schema = bindings_frame_schema_clone(
            frame->schema);
        if (!schema)
            return false;
        for (uint32_t slot = 0u; slot < schema->cap; slot++) {
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
        !var_id_eq(left->var_id, right->var_id) ||
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
            !same_occurrence || !cetta_weight_ledger_equal(
                bindings_weight_view(base), bindings_weight_view(full)))
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
        !same_occurrence || !cetta_weight_ledger_equal(
                bindings_weight_view(base), bindings_weight_view(full)))
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

typedef struct {
    BindingsFrameIndexEntry source;
    BindingsFrameIndexEntry image;
} BindingsFrameRelocation;

typedef struct {
    BindingsFrameRelocation *entries;
    size_t capacity;
    size_t count;
} BindingsFrameRelocations;

static const char bindings_frame_relocation_inventory_kind;

static size_t bindings_frame_relocation_hash(const BindingsFrameIndexEntry *frame) {
    uintptr_t key = (uintptr_t)frame->storage_references;
    key ^= (uintptr_t)frame->schema + (key << 6u) + (key >> 2u);
    key ^= (uintptr_t)frame->slot_len * UINT64_C(0x9e3779b97f4a7c15);
    key ^= key >> 17u;
    return (size_t)key;
}

static BindingsFrameRelocation *bindings_frame_relocation_slot(
    BindingsFrameRelocations *memo, const BindingsFrameIndexEntry *frame) {
    size_t index = bindings_frame_relocation_hash(frame) & (memo->capacity - 1u);
    while (memo->entries[index].source.storage_references) {
        BindingsFrameIndexEntry *source = &memo->entries[index].source;
        if (source->storage_references == frame->storage_references &&
            source->schema == frame->schema && source->slot_len == frame->slot_len)
            break;
        index = (index + 1u) & (memo->capacity - 1u);
    }
    return &memo->entries[index];
}

static void bindings_frame_relocations_release(void *inventory) {
    BindingsFrameRelocations *memo = inventory;
    for (size_t i = 0u; i < memo->capacity; i++) {
        if (!memo->entries[i].source.storage_references) continue;
        bindings_frame_index_entry_drop(&memo->entries[i].source);
        bindings_frame_index_entry_drop(&memo->entries[i].image);
    }
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_ATOM_TRANSPORT_METADATA_FREE_BYTES,
        sizeof(*memo) + memo->capacity * sizeof(*memo->entries));
    free(memo->entries);
    free(memo);
}

static void bindings_frame_storage_census(const BindingsFrameIndexEntry *entry,
    CettaStorageCensus *census);

/* Reuse the frame owner's existing schema/value/version storage walk. Both
 * retained snapshots belong to this copy episode; pointer aliases are counted
 * by the census's shared visited set. This neither copies nor forces a value. */
static void bindings_frame_relocations_storage(const void *raw, CettaStorageCensus *census) {
    const BindingsFrameRelocations *memo = raw;
    if (!memo || !cetta_storage_object(census, memo, sizeof(*memo), CETTA_STORAGE_BLOCK)) return;
    cetta_storage_array(census, memo->entries, memo->capacity, sizeof(*memo->entries), 0u);
    for (size_t i = 0u; i < memo->capacity; i++) {
        const BindingsFrameRelocation *entry = &memo->entries[i];
        if (!entry->source.storage_references) continue;
        bindings_frame_storage_census(&entry->source, census);
        bindings_frame_storage_census(&entry->image, census);
    }
}

static BindingsFrameRelocations *bindings_frame_relocations(
    AtomDeepCopySession *session) {
    BindingsFrameRelocations *memo = atom_deep_copy_session_inventory(
        session, &bindings_frame_relocation_inventory_kind);
    if (memo) return memo;
    memo = cetta_malloc(sizeof(*memo));
    memset(memo, 0, sizeof(*memo));
    memo->capacity = 64u;
    memo->entries = cetta_malloc(memo->capacity * sizeof(*memo->entries));
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_ATOM_TRANSPORT_METADATA_ALLOC_BYTES,
        sizeof(*memo) + memo->capacity * sizeof(*memo->entries));
    memset(memo->entries, 0, memo->capacity * sizeof(*memo->entries));
    if (!atom_deep_copy_session_install_inventory(session,
            &bindings_frame_relocation_inventory_kind, memo, bindings_frame_relocations_release,
            bindings_frame_relocations_storage)) {
        bindings_frame_relocations_release(memo);
        return NULL;
    }
    return memo;
}

static bool bindings_frame_relocation_store(BindingsFrameRelocations *memo,
    const BindingsFrameIndexEntry *source, const BindingsFrameIndexEntry *image) {
    if (memo->count >= memo->capacity / 2u) {
        if (memo->capacity > SIZE_MAX / 2u / sizeof(*memo->entries)) return false;
        BindingsFrameRelocations grown = {.capacity = memo->capacity * 2u, .count = memo->count};
        grown.entries = cetta_malloc(grown.capacity * sizeof(*grown.entries));
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_ATOM_TRANSPORT_METADATA_ALLOC_BYTES,
            grown.capacity * sizeof(*grown.entries));
        memset(grown.entries, 0, grown.capacity * sizeof(*grown.entries));
        for (size_t i = 0u; i < memo->capacity; i++) {
            BindingsFrameRelocation *entry = &memo->entries[i];
            if (entry->source.storage_references)
                *bindings_frame_relocation_slot(&grown, &entry->source) = *entry;
        }
        free(memo->entries);
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_ATOM_TRANSPORT_METADATA_FREE_BYTES,
            memo->capacity * sizeof(*memo->entries));
        *memo = grown;
    }
    BindingsFrameRelocation *entry = bindings_frame_relocation_slot(memo, source);
    assert(!entry->source.storage_references);
    /* Both snapshots are retained: source addresses cannot be recycled during
     * lookup, and a published image cannot disappear before a later alias. */
    bindings_frame_index_entry_hold(&entry->source, source);
    bindings_frame_index_entry_hold(&entry->image, image);
    memo->count++;
    return true;
}

static bool bindings_delta_value_equal(BindingValue a, BindingValue b) {
    return binding_value_kind(a) == binding_value_kind(b) &&
        cetta_frame_identity_eq(binding_value_epoch(a), binding_value_epoch(b)) &&
        ((!a.skeleton && !b.skeleton) ||
         (a.skeleton && b.skeleton && atom_eq(a.skeleton, b.skeleton)));
}

static bool bindings_delta_schema_equal(const BindingsFrameSchema *a,
        const BindingsFrameSchema *b) {
    if (a == b) return true;
    if (!a || !b || a->len != b->len || a->cap != b->cap) return false;
    for (uint32_t i = 0u; i < a->len; ++i)
        if (!var_id_eq(a->source_ids[i], b->source_ids[i])) return false;
    for (uint32_t i = 0u; i < a->cap; ++i) {
        if (a->spellings[i] != b->spellings[i]) return false;
        if (a->name_keys[i] != b->name_keys[i] &&
            (!a->name_keys[i] || !b->name_keys[i] || !atom_eq(a->name_keys[i], b->name_keys[i])))
            return false;
    }
    return true;
}

static bool bindings_delta_base_value(const BindingsFrameIndexEntry *frame,
        uint32_t slot, const BindingsFrameIndexEntry *base, BindingValue *value) {
    uint32_t base_slot;
    if (!base || !bindings_frame_index_find_slot(base,
            bindings_frame_slot_source_id(frame, slot), &base_slot) || base_slot >= base->slot_len)
        return false;
    BindingValue original = bindings_frame_slot_value(base, base_slot);
    if (!bindings_delta_value_equal(bindings_frame_slot_value(frame, slot), original)) return false;
    if (value) *value = original;
    return true;
}

static Atom *bindings_delta_base_name(const BindingsFrameIndexEntry *frame,
        uint32_t slot, const BindingsFrameIndexEntry *base, Atom *name) {
    uint32_t base_slot;
    if (!name || !base || slot >= frame->slot_len || !bindings_frame_index_find_slot(base,
            bindings_frame_slot_source_id(frame, slot), &base_slot) || base_slot >= base->slot_len) return NULL;
    Atom *original = bindings_frame_slot_name_key(base, base_slot);
    return original && atom_eq(name, original) ? original : NULL;
}

static bool bindings_promote_logical_with_base(Bindings *bindings,
        AtomDeepCopySession *session, const Bindings *base) {
    if (!bindings)
        return true;
    if (atom_deep_copy_session_failed(session))
        return false;
    if (!bindings_prepare_logical_write(bindings))
        return false;
    if (base && base->owners) bindings_inherit_owners(bindings, base);
    for (uint32_t i = 0; i < bindings->len; i++) {
        const Binding *original = base && i < base->len ? bindings_entry_at(base, i) : NULL;
        if (original && !var_id_eq(bindings->entries[i].var_id, original->var_id)) original = NULL;
        bool inherited_key = original && (bindings->entries[i].name_key == original->name_key ||
            (bindings->entries[i].name_key && original->name_key && atom_eq(bindings->entries[i].name_key, original->name_key)));
        bool inherited_value = original && bindings_delta_value_equal(bindings->entries[i].value, original->value);
        Atom *promoted_name_key = inherited_key ? original->name_key : bindings->entries[i].name_key
            ? atom_deep_copy_session_copy(
                  session, bindings->entries[i].name_key)
            : NULL;
        Atom *promoted = inherited_value ? original->value.skeleton : bindings->entries[i].value.skeleton
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
        const BindingConstraint *inherited = NULL;
        if (base) {
            /* Normalization can discharge an earlier constraint, shifting
             * unchanged survivors. Their borrowed roots still belong to base. */
            for (uint32_t j = 0u; j < base->eq_len; j++) {
                uint32_t index = i < base->eq_len ? (uint32_t)(((uint64_t)i + j) % base->eq_len) : j;
                if (bindings_delta_value_equal(bindings->constraints[i].lhs, base->constraints[index].lhs) &&
                    bindings_delta_value_equal(bindings->constraints[i].rhs, base->constraints[index].rhs)) {
                    inherited = &base->constraints[index]; break;
                }
            }
        }
        if (inherited) {
            bindings->constraints[i] = *inherited;
            continue;
        }
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
            const BindingsFrameIndexEntry *original = base
                ? bindings_frame_index_find_frame_const(base->frame_index, frame->epoch) : NULL;
            bool same_schema = original && bindings_delta_schema_equal(frame->schema, original->schema);
            bool same_payload = same_schema && original->slot_len == frame->slot_len && original->storage_references &&
                frame->bound_value_count == original->bound_value_count &&
                frame->private_value_count == original->private_value_count &&
                frame->schema_complete == original->schema_complete &&
                frame->has_activation_write_boundary == original->has_activation_write_boundary &&
                frame->activation_write_boundary == original->activation_write_boundary;
            for (uint32_t slot = 0u; same_payload && slot < frame->slot_len; ++slot)
                same_payload = bindings_delta_value_equal(bindings_frame_slot_value(frame, slot),
                    bindings_frame_slot_value(original, slot)) &&
                    bindings_frame_slot_version(frame, slot) == bindings_frame_slot_version(original, slot);
            if (original) {
                if (!directory_private) {
                    if (!bindings_frame_index_detach(bindings)) return false;
                    directory_private = true;
                    frame = &bindings->frame_index->frames[frame_index];
                }
                if (same_payload) {
                    bindings_frame_index_entry_share_payload(frame, original);
                    continue;
                }
                if (same_schema && frame->schema != original->schema) {
                    bindings_frame_schema_retain(original->schema);
                    bindings_frame_schema_release(frame->schema);
                    frame->schema = original->schema;
                }
                if (!bindings_frame_index_entry_detach(frame)) return false;
                for (uint32_t slot = 0u; slot < frame->cap && slot < frame->slot_len; ++slot) {
                    BindingValue inherited;
                    if (bindings_delta_base_value(frame, slot, original, &inherited))
                        frame->values[slot] = inherited;
                }
            }
            bool foreign_name_keys = false;
            bool foreign_values = false;
            for (uint32_t slot = 0u; !same_schema && frame->schema && slot < frame->schema->cap; slot++) {
                Atom *key = frame->schema->name_keys[slot];
                Atom *inherited = bindings_delta_base_name(frame, slot, original, key);
                if (key && ((inherited && inherited != key) ||
                    (!inherited && !atom_deep_copy_session_settled(session, key))))
                    foreign_name_keys = true;
            }
            for (uint32_t slot = 0u;
                 slot < frame->slot_len; slot++) {
                Atom *value = bindings_frame_slot_value(frame, slot).skeleton;
                bool inherited = bindings_delta_base_value(frame, slot, original, NULL);
                if (!inherited && value &&
                    !atom_deep_copy_session_settled(session, value))
                    foreign_values = true;
            }
            bool mapped_schema = !same_schema && bindings_frame_schema_copy_lookup(
                frame->schema, session) != NULL;
            if (!foreign_name_keys && !foreign_values && !mapped_schema)
                continue;
            if (!directory_private) {
                if (!bindings_frame_index_detach(bindings))
                    return false;
                directory_private = true;
                frame = &bindings->frame_index->frames[frame_index];
            }
            bool shared_payload = frame->storage_references &&
                atomic_load_explicit(frame->storage_references, memory_order_acquire) > 1u;
            BindingsFrameRelocations *memo = shared_payload
                ? bindings_frame_relocations(session) : NULL;
            if (shared_payload && !memo) return false;
            BindingsFrameIndexEntry source;
            bool retained_source = false;
            if (memo) {
                BindingsFrameRelocation *entry = bindings_frame_relocation_slot(memo, frame);
                if (entry->source.storage_references) {
                    bindings_frame_index_entry_share_payload(frame, &entry->image);
                    continue;
                }
                bindings_frame_index_entry_hold(&source, frame);
                retained_source = true;
            }
            if (foreign_name_keys || mapped_schema) {
                BindingsFrameSchema *promoted_schema = NULL;
                if (original) {
                    promoted_schema = bindings_frame_schema_clone(frame->schema);
                    for (uint32_t slot = 0u; promoted_schema && slot < promoted_schema->cap; ++slot) {
                        Atom *key = promoted_schema->name_keys[slot];
                        if (!key) continue;
                        Atom *inherited = bindings_delta_base_name(frame, slot, original, key);
                        Atom *image = inherited ? inherited : atom_deep_copy_session_copy(session, key);
                        if (!image) {
                            bindings_frame_schema_release(promoted_schema);
                            promoted_schema = NULL;
                            break;
                        }
                        promoted_schema->name_keys[slot] = image;
                    }
                } else {
                    promoted_schema = bindings_frame_schema_copy_with_session(frame->schema, session);
                }
                if (!promoted_schema) {
                    if (retained_source) bindings_frame_index_entry_drop(&source);
                    return false;
                }
                bindings_frame_schema_release(frame->schema);
                frame->schema = promoted_schema;
            }
            if (foreign_values && !bindings_frame_index_entry_detach(frame)) {
                if (retained_source) bindings_frame_index_entry_drop(&source);
                return false;
            }
            for (uint32_t slot = 0u; foreign_values && slot < frame->slot_len; slot++) {
                if (bindings_delta_base_value(frame, slot, original, NULL)) continue;
                if (!bindings_frame_slot_value(frame, slot).skeleton) {
                    continue;
                }
                Atom *copy = atom_deep_copy_session_copy(
                    session, bindings_frame_slot_value(frame, slot).skeleton);
                if (!copy) {
                    if (retained_source) bindings_frame_index_entry_drop(&source);
                    return false;
                }
                frame->values[slot].skeleton = copy;
            }
            if (retained_source) {
                bool stored = bindings_frame_relocation_store(memo, &source, frame);
                bindings_frame_index_entry_drop(&source);
                if (!stored) return false;
            }
        }
    }
    /* Promotion preserves variable identities and the retained frame inventory;
     * it changes only syntax ownership. No context rediscovery is required. */
    return true;
}

bool bindings_promote_logical_atoms_with_session(Bindings *bindings,
        AtomDeepCopySession *session) {
    return bindings_promote_logical_with_base(bindings, session, NULL);
}

bool bindings_promote_logical_delta_to_arena(Bindings *bindings,
        const Bindings *base, Arena *dst) {
    if (!bindings || !base || bindings == base || !dst) return false;
    AtomDeepCopySession *session = atom_deep_copy_session_new(dst);
    if (!session) return false;
    bool ok = bindings_promote_logical_with_base(bindings, session, base);
    atom_deep_copy_session_free(session);
    return ok;
}

bool bindings_logical_atoms_satisfy(
    const Bindings *bindings, AtomTreePredicate predicate, void *context) {
    if (!bindings || !predicate)
        return bindings == NULL;
    for (uint32_t i = 0u; i < bindings->len; i++) {
        const Binding *entry = bindings_entry_at(bindings, i);
        if ((entry->name_key &&
             !predicate(entry->name_key, context)) ||
            (entry->value.skeleton &&
             !predicate(entry->value.skeleton, context))) {
            return false;
        }
    }
    for (uint32_t i = 0u; i < bindings->eq_len; i++) {
        const BindingConstraint *constraint = &bindings->constraints[i];
        if ((constraint->lhs.skeleton &&
             !predicate(constraint->lhs.skeleton, context)) ||
            (constraint->rhs.skeleton &&
             !predicate(constraint->rhs.skeleton, context))) {
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
                    !predicate(bindings_frame_slot_name_key(frame, slot), context)) {
                    return false;
                }
                if (bindings_frame_slot_value(frame, slot).skeleton &&
                    !predicate(bindings_frame_slot_value(frame, slot).skeleton, context)) {
                    return false;
                }
            }
        }
    }
    return true;
}

static bool bindings_atom_closed(const Atom *atom, void *context) {
    return atom_graph_is_closed_for_arena(context, atom);
}

static bool bindings_occurrence_carriers_satisfy(const PrimeOccurrence *occurrence,
        AtomTreePredicate atoms, PrimeNeedStateCellPredicate cells, void *context) {
    if (!occurrence) return true;
    if (!prime_need_snapshot_atoms_satisfy(&occurrence->prime_need, atoms, context) ||
        !prime_need_branch_state_payloads_satisfy(
            &occurrence->branch_state, atoms, cells, context)) return false;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if (!prime_need_branch_state_payloads_satisfy(
            &occurrence->receipt, atoms, cells, context)) return false;
#endif
    size_t remaining = occurrence->weights.length;
    for (const CettaWeightFactor *factor = occurrence->weights.last; factor;
         factor = factor->previous) {
        if (!remaining || !factor->value || !atoms(factor->value, context))
            return false;
        remaining--;
    }
    return remaining == 0u;
}

static bool bindings_occurrence_payloads_satisfy(const PrimeOccurrence *occurrence,
        AtomTreePredicate atoms, PrimeNeedStateCellPredicate cells, void *context) {
    return !occurrence ||
        (prime_need_reader_context_payloads(occurrence->reader_context,atoms,cells,context) &&
         bindings_occurrence_carriers_satisfy(occurrence,atoms,cells,context));
}

bool bindings_payloads_satisfy(const Bindings *bindings,
        AtomTreePredicate atoms, PrimeNeedStateCellPredicate cells, void *context) {
    return bindings && atoms && cells &&
        bindings_logical_atoms_satisfy(bindings, atoms, context) &&
        bindings_occurrence_payloads_satisfy(bindings->prime_ext, atoms, cells, context);
}

bool bindings_carriers_payloads_satisfy(const Bindings *bindings,
        AtomTreePredicate atoms, PrimeNeedStateCellPredicate cells, void *context) {
    return bindings && atoms && cells &&
        bindings_logical_atoms_satisfy(bindings,atoms,context) &&
        bindings_occurrence_carriers_satisfy(bindings->prime_ext,atoms,cells,context);
}

bool bindings_logical_atoms_closed_for_arena(
    const Bindings *bindings, const Arena *arena) {
    if (!arena) return bindings == NULL;
    return bindings_logical_atoms_satisfy(
        bindings, bindings_atom_closed, (void *)arena);
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
                        bindings_frame_slot_value(frame, slot).skeleton)) {
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

static bool bindings_occurrence_readers_qualified(const PrimeOccurrence *occurrence,bool owned_copy,
    const Arena *owner,const AtomDeepCopySession *session) {
    return !occurrence || (owned_copy ? (!occurrence->reader_context ||
        prime_need_reader_context_copy_lookup(occurrence->reader_context,session)!=NULL) :
        prime_need_reader_context_can_promote(occurrence->reader_context,&occurrence->prime_need,owner)==CETTA_MATCH_MATCHED);
}

static bool bindings_promote_occurrence_with_session(
    Arena *dst, AtomDeepCopySession *copy_session, PrimeOccurrence *occurrence,
    bool owned_copy) {
    if (!occurrence || !bindings_occurrence_readers_qualified(occurrence,owned_copy,dst,copy_session) ||
        !atom_deep_copy_session_targets(copy_session, dst))
        return false;
    PrimeNeedReaderContext *readers=NULL;
    if (owned_copy) {
        readers=prime_need_reader_context_copy_lookup(occurrence->reader_context,copy_session);
        prime_need_reader_context_retain(readers);
    } else if (prime_need_reader_context_promote(occurrence->reader_context,
            &occurrence->prime_need,dst,&readers)!=CETTA_MATCH_MATCHED) return false;
    PrimeOccurrence promoted = *occurrence;
    promoted.reader_context=readers;
    bool need_copied = owned_copy
        ? prime_need_snapshot_copy_with_session(dst, copy_session, &promoted.prime_need)
        : prime_need_snapshot_promote_with_session(dst, copy_session, &promoted.prime_need);
    bool branch_copied = need_copied && (owned_copy
        ? prime_need_branch_state_copy_with_session(dst, copy_session, &promoted.branch_state)
        : prime_need_branch_state_promote_with_session(dst, copy_session, &promoted.branch_state));
    bool weights_copied = branch_copied && (owned_copy
        ? cetta_weight_ledger_copy_with_session(dst, copy_session, &promoted.weights)
        : cetta_weight_ledger_promote_with_session(dst, copy_session, &promoted.weights));
    if (!weights_copied) {
        prime_need_reader_context_release(readers);
        return false;
    }
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    bool receipt_copied = owned_copy
        ? prime_need_receipt_copy_carrier_with_session(dst, copy_session, &promoted.receipt)
        : prime_need_receipt_carry_with_session(dst, copy_session, &promoted.receipt);
    if (!receipt_copied) {
        prime_need_reader_context_release(readers);
        return false;
    }
#endif
    PrimeNeedReaderContext *old=occurrence->reader_context;
    *occurrence = promoted;
    prime_need_reader_context_release(old);
    return true;
}

bool bindings_promote_atoms_to_arena(Bindings *bindings, Arena *dst) {
    if (!bindings || !dst)
        return false;
    AtomDeepCopySession *session = atom_deep_copy_session_new(dst);
    if (!session)
        return false;
    bool ok = bindings_promote_atoms_with_session(bindings, dst, session);
    atom_deep_copy_session_free(session);
    return ok;
}

bool bindings_promote_atoms_with_session(
    Bindings *bindings, Arena *dst, AtomDeepCopySession *session) {
    if (!bindings || !bindings_occurrence_readers_qualified(bindings->prime_ext,false,dst,session) ||
        !atom_deep_copy_session_targets(session, dst) ||
        !bindings_promote_logical_atoms_with_session(bindings, session))
        return false;
    return !bindings->prime_ext || bindings_promote_occurrence_with_session(
        dst, session, bindings->prime_ext, false);
}

bool bindings_copy_owned_atoms_with_session(
    Bindings *bindings, Arena *dst, AtomDeepCopySession *session) {
    if (!bindings || !bindings_occurrence_readers_qualified(bindings->prime_ext,true,dst,session) ||
        !atom_deep_copy_session_targets(session, dst) ||
        !bindings_promote_logical_atoms_with_session(bindings, session))
        return false;
    return !bindings->prime_ext || bindings_promote_occurrence_with_session(
        dst, session, bindings->prime_ext, true);
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
                frame, var_source_id(id), &slot) ||
            !bindings_frame_slot_value(frame, slot).skeleton ||
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
    *value_out = bindings_frame_slot_value(frame, slot);
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
    *value_out = bindings_frame_slot_value(frame, slot);
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
        var_id_is_none(id) || !cetta_frame_identity_eq(var_epoch_suffix(id), binding_value_epoch(context))) {
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
            frame, var_source_id(id), &slot)) {
        return false;
    }
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP);
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_FRAME_COORDINATE);
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_AUTHORITATIVE);
    *value_out = bindings_frame_slot_value(frame, slot);
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
            frame, var_source_id(variable.skeleton->var_id), &slot)) {
        return false;
    }
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP);
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_FRAME_COORDINATE);
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_AUTHORITATIVE);
    *value_out = bindings_frame_slot_value(frame, slot);
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

BindingValue bindings_lookup_context_id(
        Bindings *b, BindingValue context, VarId source_id) {
    VarId id = binding_value_is_contextual(context)
        ? var_epoch_id(source_id, binding_value_epoch(context)) : source_id;
    BindingValue image;
    if (bindings_frame_index_lookup_context_id(b, context, id, &image))
        return image;
    return bindings_lookup_value_id(b, id);
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




char *arena_tagged_var_name(Arena *a, const char *name, CettaFrameIdentity suffix) {
    size_t name_len = strlen(name);
    size_t needed = name_len + 1 + 20 + 1;
    char *buf = arena_alloc(a, needed);
    snprintf(buf, needed, "%s#%" PRIu64, name, suffix.word);
    return buf;
}

static Atom *epoch_var_atom(Arena *a, Atom *var, CettaFrameIdentity epoch) {
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
            ok = match_decoded_atoms_worklist(existing, val, &probe, NULL, NULL, false);
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
        b, var->var_id, atom_symbol_current_id(var), var->name_key, binding_value_from_atom(val), true);
}

bool bindings_add_var_owned(Bindings *b, Atom *var, Atom *val) {
    if (!b)
        return false;
    if (var && var->kind == ATOM_VAR &&
        bindings_add_inplace_internal(
            b, var->var_id, atom_symbol_current_id(var), var->name_key,
            binding_value_from_atom(val), true)) {
        return true;
    }
    bindings_free(b);
    return false;
}


bool bindings_add_var_acyclic(Bindings *b, Atom *var, Atom *val) {
    if (!var || var->kind != ATOM_VAR)
        return false;
    Bindings next;
    if (!bindings_clone(&next, b))
        return false;
    if (!bindings_add_inplace_internal(
            &next, var->var_id, atom_symbol_current_id(var), var->name_key,
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

    if (binding_values_eq_under_bindings_checked(b, lhs, rhs, NULL)) {
        return true;
    }
    if (lhs.skeleton->kind == ATOM_VAR) {
        if (!bindings_add_inplace_internal(
                b, binding_value_variable_id(lhs), atom_symbol_current_id(lhs.skeleton), lhs.skeleton->name_key,
                rhs, false)) {
            return false;
        }
    } else if (rhs.skeleton->kind == ATOM_VAR) {
        if (!bindings_add_inplace_internal(
                b, binding_value_variable_id(rhs), atom_symbol_current_id(rhs.skeleton), rhs.skeleton->name_key,
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

bool bindings_add_constraint_owned(Bindings *b, Atom *lhs, Atom *rhs) {
    if (!b)
        return false;
    if (bindings_add_constraint_inplace_internal(
            b, binding_value_from_atom(lhs), binding_value_from_atom(rhs), true)) {
        return true;
    }
    bindings_free(b);
    return false;
}

/* The complete occurrence product is joined once, before logical bindings.
 * Scope entry selects actual operations; old receipt values remain data when
 * no recorder is active. The caller retains its existing rollback contract. */
static bool bindings_prime_join_identity(
    const PrimeOccurrence *dst, const PrimeOccurrence *src, uint64_t *token) {
    uint64_t left = dst->occurrence_token;
    uint64_t right = src->occurrence_token;
    if (left == 0u || left == right)
        *token = right;
    else if (right == 0u)
        *token = left;
    else {
        *token = prime_need_fresh_source_occurrence();
        if (!*token)
            return false;
    }
    return true;
}

static inline __attribute__((always_inline)) bool bindings_prime_merge_kernel(
    PrimeOccurrence *dst, const PrimeOccurrence *src, uint64_t occurrence_token,
    bool recorded) {
#if !CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    (void)recorded;
#endif
    if (!prime_need_snapshot_merge(&dst->prime_need, &src->prime_need) ||
        !prime_need_branch_state_merge(&dst->branch_state, &src->branch_state) ||
        !cetta_weight_ledger_merge(&dst->weights, &src->weights))
        return false;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if (recorded) {
        if (!prime_need_recording_merge_scoped(&dst->receipt, &src->receipt))
            return false;
    } else if (dst->receipt.recording || src->receipt.recording) {
        /* Transport validates old recorded data even after recording stops.
         * Erased weak carriers normalize; unreadable live readers refuse. */
        if (!prime_need_recording_merge(&dst->receipt, &src->receipt))
            return false;
    } else if (src->receipt.session_id &&
               !prime_need_branch_state_merge(&dst->receipt, &src->receipt)) {
        return false;
    }
#endif
    dst->occurrence_token = occurrence_token;
    return true;
}

static bool bindings_prime_merge_data(
    PrimeOccurrence *dst, const PrimeOccurrence *src, uint64_t occurrence_token) {
    return bindings_prime_merge_kernel(dst, src, occurrence_token, false);
}

#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
static bool bindings_prime_merge_recorded(
    PrimeOccurrence *dst, const PrimeOccurrence *src, uint64_t occurrence_token) {
    return bindings_prime_merge_kernel(dst, src, occurrence_token, true);
}

static _Thread_local bool (*g_bindings_prime_merge)(
    PrimeOccurrence *, const PrimeOccurrence *, uint64_t) =
    bindings_prime_merge_data;

void bindings_prime_observation_policy_changed(void) {
    g_bindings_prime_merge = g_prime_need_recording_scope
        ? bindings_prime_merge_recorded : bindings_prime_merge_data;
}
#endif

static inline __attribute__((always_inline)) bool bindings_merge_prime_occurrence(
    Bindings *dst, const Bindings *src, CettaMatchCompletion *failure) {
    /* HE remains allocation-free. One materialization supplies every mutable
     * component, including any optional history already supplied as data. */
    if (!bindings_prime_present(dst) && !bindings_prime_present(src))
        return true;
    const PrimeOccurrence *previous = dst->prime_ext
        ? dst->prime_ext : &g_prime_occurrence_empty;
    const PrimeOccurrence *source = src->prime_ext
        ? src->prime_ext : &g_prime_occurrence_empty;
    PrimeNeedReaderContext *readers=NULL;
    if (previous->reader_context || source->reader_context) {
        CettaMatchCompletion joined=prime_need_reader_context_merge(
            previous->reader_context,source->reader_context,&readers);
        if (joined!=CETTA_MATCH_MATCHED) {if (failure) *failure=joined;return false;}
    }
    uint64_t occurrence_token;
    if (!bindings_prime_join_identity(previous, source, &occurrence_token)) {
        prime_need_reader_context_release(readers);return false;
    }
    PrimeOccurrence *destination = bindings_prime_ext_materialize(dst);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    bool merged=g_bindings_prime_merge(destination, source, occurrence_token);
#else
    bool merged=bindings_prime_merge_data(destination, source, occurrence_token);
#endif
    if (merged && (destination->reader_context || readers)) {
        prime_need_reader_context_release(destination->reader_context);
        destination->reader_context=readers;
    } else if (readers) prime_need_reader_context_release(readers);
    return merged;
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
    if (!bindings_merge_prime_occurrence(dst, src, NULL))
        return false;
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

static bool bindings_try_merge_live_kernel(
    Bindings *dst, const Bindings *src, CettaMatchCompletion *failure) {
    if (failure) *failure = CETTA_MATCH_INCOMPLETE;
    if (!src || (!bindings_has_bound_values(src) && src->eq_len == 0 &&
                 !bindings_prime_present(src) &&
                 !bindings_has_frame_authority(src)))
        return true;
    bindings_assert_no_private_variant_slots(dst);
    bindings_assert_no_private_variant_slots(src);
    /* A shared immutable directory is the same complete framed snapshot,
     * not merely equal-valued data. With no flat rows, constraints or Prime
     * state, and the identical owner chain, live merge has nothing to add.
     * Preserve every frame, context tag and version without enumerating it. */
    if (dst && dst->len == 0u && src->len == 0u &&
        dst->eq_len == 0u && src->eq_len == 0u &&
        dst->frame_index == src->frame_index && dst->owners == src->owners &&
        !bindings_prime_present(dst) && !bindings_prime_present(src)) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_MERGE);
        return true;
    }
    if (dst && !bindings_has_bound_values(dst) && dst->eq_len == 0 &&
        !bindings_prime_present(dst) && !bindings_has_frame_authority(dst)) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_MERGE);
        Bindings cloned;
        if (!bindings_clone(&cloned, src))
            return false;
        bindings_replace(dst, &cloned);
        return true;
    }

    BindingsBuilder builder;
    bindings_builder_init_owned(&builder, dst);
    bool ok = bindings_builder_try_merge_kernel(&builder, src, failure);
    bindings_builder_take(&builder, dst);
    return ok;
}

bool bindings_try_merge_live(Bindings *dst, const Bindings *src) {
    return bindings_try_merge_live_kernel(dst, src, NULL);
}

CettaMatchCompletion bindings_try_merge_live_checked(
    Bindings *dst, const Bindings *src) {
    CettaMatchCompletion failure = CETTA_MATCH_INCOMPLETE;
    if (!dst) return failure;
    return bindings_try_merge_live_kernel(dst, src, &failure)
        ? CETTA_MATCH_MATCHED : failure;
}

CettaMatchCompletion bindings_import_production_checked(Arena *owner,
    const PrimeNeedReadCapture *reads, uint64_t thunk_id,
    const Bindings *common, const Bindings *entrance,
    const Bindings *production, const Bindings *recipient,
    PrimeNeedStateInitialValue initial_value, void *context, Bindings *out) {
    if (!owner || !common || !entrance || !production || !recipient || !out ||
        !prime_need_read_capture_has_cuts(reads, bindings_need_view(common),
                                         bindings_need_view(entrance)))
        return CETTA_MATCH_INCOMPLETE;
    PrimeNeedSnapshot need;
    PrimeNeedBranchState state;
    CettaWeightLedger weights;
    CettaMatchCompletion result = prime_need_snapshot_import_production_checked(
        owner, reads, bindings_need_view(production), thunk_id,
        bindings_need_view(recipient), &need);
    if (result != CETTA_MATCH_MATCHED) return result;
    result = prime_need_branch_state_import_suffix(owner,
        bindings_branch_state_view(common), bindings_branch_state_view(entrance),
        bindings_branch_state_view(production), bindings_branch_state_view(recipient),
        initial_value, context, &state);
    if (result != CETTA_MATCH_MATCHED) return result;
    result = cetta_weight_ledger_import_suffix_checked(owner,
        bindings_weight_view(entrance), bindings_weight_view(production),
        bindings_weight_view(recipient), &weights);
    if (result != CETTA_MATCH_MATCHED) return result;

    Bindings staged, lexical;
    if (!bindings_clone(&staged, recipient)) return CETTA_MATCH_INCOMPLETE;
    if (!bindings_clone(&lexical, production)) {
        bindings_free(&staged);
        return CETTA_MATCH_INCOMPLETE;
    }
    uint64_t occurrence = 0u;
    if (!bindings_prime_join_identity(
            recipient->prime_ext ? recipient->prime_ext : &g_prime_occurrence_empty,
            production->prime_ext ? production->prime_ext : &g_prime_occurrence_empty,
            &occurrence)) {
        bindings_free(&lexical);
        bindings_free(&staged);
        return CETTA_MATCH_INCOMPLETE;
    }
    const PrimeNeedReceipt *receipt = NULL;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    PrimeNeedReceipt receipt_value = {0};
    receipt_value = *bindings_receipt_view(recipient);
    receipt = &receipt_value;
#endif
    /* Join logical support without invoking the ambient occurrence/history
     * merge policy. All operational carriers were prepared above, and a live
     * recorder cannot move a reader's held receipt into an unrelated scope. */
    bindings_prime_set(&staged, NULL, NULL, 0u, NULL, NULL);
    bindings_prime_set(&lexical, NULL, NULL, 0u, NULL, NULL);
    result = bindings_try_merge_live_checked(&staged, &lexical);
    bindings_free(&lexical);
    if (result == CETTA_MATCH_MATCHED) {
        PrimeNeedReaderContext *readers=NULL;
        result=prime_need_reader_context_import(bindings_reader_context_view(entrance),
            bindings_reader_context_view(production),bindings_reader_context_view(recipient),&readers);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
        if (result==CETTA_MATCH_MATCHED) {
            result=prime_need_receipt_join_qualified(owner,&receipt_value,
                bindings_receipt_view(production));
        }
#endif
        if (result==CETTA_MATCH_MATCHED) {
            bindings_prime_set(&staged, &need, &state, occurrence, receipt, &weights);
            bindings_reader_context_set(&staged,readers);
            prime_need_reader_context_release(readers);
            bindings_replace(out, &staged);
        } else {
            prime_need_reader_context_release(readers);
            bindings_free(&staged);
        }
    }
    else bindings_free(&staged);
    return result;
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
    if (!slots || slot_cap == 0u || var_id_is_none(id))
        return false;
    size_t mask = slot_cap - 1u;
    size_t slot = bindings_var_id_hash(id) & mask;
    while (!var_id_is_none(slots[slot].id) &&
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
        var_id_is_none(id))
        return 0u;
    size_t mask = memo->slot_cap - 1u;
    size_t slot = bindings_var_id_hash(id) & mask;
    while (!var_id_is_none(memo->slots[slot].id)) {
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

/* Applying a substitution is one fold over the term graph.  A variable's
 * image is the image of its bound value (memoised per variable; a variable met
 * again on its own binding path is its own image), or its unbound reading; an
 * expression's image shares every unchanged child, and is the expression
 * itself when no child changed.  The fold keeps its pending expressions and
 * variables on an explicit stack: its depth is the term's nesting plus the
 * binding chains it follows, and a list bound cell by cell reaches a depth of
 * its length. */

struct BindingsEpochAccelerator;

/* The expressions ordinary substitution opens before it keeps node images. */
#define BINDINGS_APPLY_GRAPH_AFTER 64u

/* How a skeleton's variables are read.  The rewrite fold reads them in the
 * value's own context (`epoch`, `kind`).  The epoch fold reads them on the
 * original or the stored side of an activation (`original_side`), from an
 * entry on (`first_entry`), in the full substitution for stored values
 * (`resolve_outer`), through an optional accelerator (`fast`). */
typedef struct {
    const struct BindingsEpochAccelerator *fast;
    CettaFrameIdentity epoch;
    uint32_t first_entry;
    BindingValueKind kind;
    bool original_side;
    bool resolve_outer;
} BindingsApplyScope;

/* A graph observation distinguishes a source node's lexical readings. The
 * same skeleton can occur in several rule frames, so a pointer alone is not
 * a substitution-cache key. Quotation depth also determines how the language's
 * structure hooks read and form an image. Completed images retain that full
 * context; the active-source table uses only source and lexical scope, so a
 * malformed raw cycle cannot evade detection by adding quotation depth.
 * Both tables belong to one immutable environment observation and are never
 * retained across a binding mutation. */
typedef struct {
    Atom *source;
    Atom *image;
    CettaFrameIdentity epoch;
    BindingValueKind kind;
    uint32_t code_depth;
    bool active;
} BindingApplyGraphEntry;

typedef struct BindingApplyGraph {
    BindingApplyGraphEntry *entries;
    size_t len, cap;
    struct BindingApplyGraph *active_path;
} BindingApplyGraph;

static size_t bindings_apply_graph_hash(Atom *source, CettaFrameIdentity epoch,
                                        BindingValueKind kind,
                                        uint32_t code_depth) {
    uint64_t hash = (uint64_t)(uintptr_t)source;
    hash ^= epoch.word * UINT64_C(0x9e3779b97f4a7c15);
    hash ^= (uint64_t)kind * UINT64_C(0xbf58476d1ce4e5b9);
    hash ^= (uint64_t)code_depth * UINT64_C(0x94d049bb133111eb);
    hash ^= hash >> 30u;
    hash *= UINT64_C(0xbf58476d1ce4e5b9);
    return (size_t)(hash ^ (hash >> 27u));
}

static bool bindings_apply_graph_key_equal(const BindingApplyGraphEntry *entry,
        Atom *source, BindingsApplyScope scope, uint32_t code_depth) {
    return entry->source == source && cetta_frame_identity_eq(entry->epoch, scope.epoch) &&
        entry->kind == scope.kind && entry->code_depth == code_depth;
}

static BindingApplyGraphEntry *bindings_apply_graph_slot(
        BindingApplyGraph *graph, Atom *source, BindingsApplyScope scope,
        uint32_t code_depth) {
    size_t slot = bindings_apply_graph_hash(
        source, scope.epoch, scope.kind, code_depth) &
        (graph->cap - 1u);
    while (graph->entries[slot].source &&
           !bindings_apply_graph_key_equal(&graph->entries[slot], source, scope, code_depth))
        slot = (slot + 1u) & (graph->cap - 1u);
    return &graph->entries[slot];
}

static BindingApplyGraphEntry *bindings_apply_graph_enter(
        BindingApplyGraph *graph, Atom *source, BindingsApplyScope scope,
        uint32_t code_depth, bool *created) {
    if (graph->cap && (graph->len + 1u) <= graph->cap / 2u) {
        BindingApplyGraphEntry *entry = bindings_apply_graph_slot(
            graph, source, scope, code_depth);
        *created = !entry->source;
        if (*created) {
            *entry = (BindingApplyGraphEntry){.source = source,
                .epoch = scope.epoch, .kind = scope.kind,
                .code_depth = code_depth};
            ++graph->len;
        }
        return entry;
    }
    size_t capacity = graph->cap ? graph->cap * 2u : 32u;
    if (capacity < graph->cap || capacity > SIZE_MAX / sizeof(*graph->entries))
        return NULL;
    BindingApplyGraph grown = {.cap = capacity, .len = graph->len,
                               .active_path = graph->active_path};
    grown.entries = calloc(capacity, sizeof(*grown.entries));
    if (!grown.entries) return NULL;
    for (size_t i = 0u; i < graph->cap; ++i) {
        BindingApplyGraphEntry entry = graph->entries[i];
        if (entry.source)
            *bindings_apply_graph_slot(&grown, entry.source,
                (BindingsApplyScope){.epoch = entry.epoch, .kind = entry.kind},
                entry.code_depth) = entry;
    }
    free(graph->entries);
    *graph = grown;
    return bindings_apply_graph_enter(graph, source, scope, code_depth, created);
}

typedef struct {
    Bindings *b;
    Arena *a;
    BindingApplySeen *seen;
    bool track_cycles;
    /* The rewrite fold's memo and hook. */
    BindingApplyMemo *memo;
    BindingsRewriteVarFn rewrite_var;
    void *rewrite_ctx;
    /* The epoch fold's memos: variables read from an entry on, and variables
     * read in the full substitution. */
    BindingApplyMemo *local_memo;
    BindingApplyMemo *outer_memo;
    uint64_t *node_visits;
} BindingsApplyFold;

static Atom *bindings_apply_fold(BindingsApplyFold *fold, Atom *atom,
                                 BindingsApplyScope scope, uint32_t seen_len);
static Atom *bindings_apply_shared_fold(BindingsApplyFold *fold,
    BindingApplyGraph *graph, Atom *atom, BindingsApplyScope scope, uint32_t seen_len);

#if defined(__GNUC__)
__attribute__((always_inline))
#endif
static inline Atom *bindings_apply_value_rewrite_vars(Bindings *b, Arena *a, BindingValue value,
                                  BindingsRewriteVarFn rewrite_var,
                                  void *rewrite_ctx, bool shared_graph) {
    Atom *atom = value.skeleton;
    if (!b || !a || !atom)
        return NULL;
    if (!bindings_has_bound_values(b) && !rewrite_var &&
        binding_value_kind(value) == BINDING_VALUE_MATERIALIZED)
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
    /* In an acyclic store, root aliases do not depend on a substitution
     * traversal's active path. Resolve them with the shared root service
     * before constructing that traversal's memo and work stack. The terminal
     * value retains its own lexical scope; open expressions still receive
     * the original full substitution below. Custom rewrites and cyclic or
     * unknown stores retain their independent traversal. */
    if (!rewrite_var && atom->kind == ATOM_VAR &&
        b->cycle_state == BINDINGS_CYCLE_ACYCLIC) {
        BindingValue root;
        if (bindings_resolve_value_root(b, value, &root, true)) {
            value = root;
            atom = value.skeleton;
            if (atom->kind == ATOM_VAR)
                return binding_value_materialize(a, value);
            if (!atom_has_vars(atom))
                return atom;
        }
    }
    if (!rewrite_var && atom->kind == ATOM_EXPR &&
        binding_value_kind(value) == BINDING_VALUE_MATERIALIZED) {
        VarId support = atom_single_variable_id(atom);
        if (!var_id_is_none(support) &&
            !bindings_lookup_context_id(b, value, support).skeleton)
            return atom;
    }
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
    BindingApplyGraph active_path = {0};
    BindingApplyGraph graph;
    if (shared_graph && !track_cycles)
        graph = (BindingApplyGraph){.active_path =
            bindings_structure_hooks() ? &active_path : NULL};
    BindingsApplyFold fold = {
        .b = b,
        .a = a,
        .seen = &seen,
        .track_cycles = track_cycles,
        .memo = &memo,
        .rewrite_var = rewrite_var,
        .rewrite_ctx = rewrite_ctx,
    };
    BindingsApplyScope scope = {.epoch = binding_value_epoch(value), .kind = binding_value_kind(value)};
    /* Graph observation owns its extra state. Ordinary substitution keeps
     * its original frame, without an unused graph field on each call.
     * Cyclic images still depend on the active variable path. */
    Atom *result = shared_graph && !track_cycles
        ? bindings_apply_shared_fold(&fold, &graph, value.skeleton, scope, 0u)
        : bindings_apply_fold(&fold, value.skeleton, scope, 0u);
    bindings_apply_memo_release(&memo);
    bindings_apply_seen_release(&seen);
    if (shared_graph && !track_cycles) {
        free(graph.entries);
        free(active_path.entries);
    }
    return result;
}

Atom *bindings_apply_rewrite_vars(Bindings *b, Arena *a, Atom *atom,
                                  BindingsRewriteVarFn rewrite_var, void *rewrite_ctx) {
    return bindings_apply_value_rewrite_vars(
        b, a, binding_value_from_atom(atom), rewrite_var, rewrite_ctx, false);
}

Atom *bindings_apply_graph(Bindings *b, Arena *a, Atom *atom) {
    return bindings_apply_value_rewrite_vars(
        b, a, binding_value_from_atom(atom), NULL, NULL, true);
}

Atom *bindings_apply_value(const Bindings *b, Arena *a, BindingValue value) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_APPLY);
    return bindings_apply_value_rewrite_vars((Bindings *)b, a, value, NULL, NULL, false);
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
        if (var_id_eq(bindings_entry_at(&reduced, i)->var_id, skip_id) &&
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
        bool frame_write_present = bindings_frame_slot_value(frame, slot).skeleton &&
            (first_entry == 0u ||
             (frame->has_activation_write_boundary &&
              bindings_frame_slot_version(frame, slot) >
                  frame->activation_write_boundary));
        return frame_write_present
            ? bindings_frame_slot_value(frame, slot)
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
        if (source_id.base < frame->source_first_id.base)
            return false;
        uint64_t offset = source_id.base - frame->source_first_id.base;
        if (offset >= frame->len)
            return false;
        *slot_out = (uint32_t)offset;
        return true;
    }
    uint32_t low = 0u;
    uint32_t high = frame->len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        if (frame->source_ids[middle].base < source_id.base)
            low = middle + 1u;
        else
            high = middle;
    }
    if (low >= frame->len || !var_id_eq(frame->source_ids[low], source_id))
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
        var_id_is_none(source_id)) {
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

/* A directory scope must belong to this binding image before it is read.
 * Sharing the slot payload does not make another image's directory valid. */
static const BindingsFrameIndexEntry *bindings_cursor_scope_entry(
        const Bindings *bindings, const void *scope) {
    const BindingsFrameIndex *index = bindings->frame_index;
    if (!index || !index->frames || !scope)
        return NULL;
    uintptr_t address = (uintptr_t)scope;
    uintptr_t begin = (uintptr_t)index->frames;
    if (address < begin)
        return NULL;
    uintptr_t offset = address - begin;
    if (offset % sizeof(*index->frames) != 0u ||
        offset / sizeof(*index->frames) >= index->len)
        return NULL;
    const BindingsFrameIndexEntry *entry = scope;
    return bindings_frame_index_find_frame_const(index, entry->epoch) == entry
        ? entry : NULL;
}

CettaGsltTermViewStatusV1 bindings_value_term_cursor_v1(
        const Bindings *bindings, BindingValue value,
        CettaGsltTermCursorV1 *target_out) {
    if (target_out)
        *target_out = (CettaGsltTermCursorV1){0};
    if (!bindings || !value.skeleton || !target_out)
        return CETTA_GSLT_TERM_VIEW_INVALID_V1;
    const void *scope = NULL;
    if (binding_value_is_contextual(value) && atom_has_vars(value.skeleton)) {
        scope = bindings_frame_index_find_frame_const(
            bindings->frame_index, binding_value_epoch(value));
        if (!scope)
            return CETTA_GSLT_TERM_VIEW_DEFER_V1;
    }
    *target_out = (CettaGsltTermCursorV1){value.skeleton, scope};
    return CETTA_GSLT_TERM_VIEW_OK_V1;
}

static CettaGsltTermViewStatusV1 bindings_resolve_cursor(
        void *raw_context, CettaGsltTermCursorV1 source,
        CettaGsltTermCursorV1 *target_out, bool scoped) {
    const BindingsTermCursorContextV1 *context = raw_context;
    if (target_out)
        *target_out = (CettaGsltTermCursorV1){0};
    if (!context || !context->bindings || !source.source || !target_out)
        return CETTA_GSLT_TERM_VIEW_INVALID_V1;
    const BindingsActivationView *frame = context->frame;
    Atom *root = source.source;
    BindingValue value = binding_value_from_atom(root);
    if (source.scope && source.scope != frame) {
        const BindingsFrameIndexEntry *entry = scoped
            ? bindings_cursor_scope_entry(context->bindings, source.scope) : NULL;
        if (!entry)
            return CETTA_GSLT_TERM_VIEW_DEFER_V1;
        value = binding_value_from_context(root, entry->epoch);
    } else if (source.scope) {
        BindingsActivationRead read;
        if (!bindings_activation_view_borrow(context->bindings, frame, &read))
            return CETTA_GSLT_TERM_VIEW_DEFER_V1;
        if (root->kind != ATOM_VAR) {
            *target_out = source;
            return CETTA_GSLT_TERM_VIEW_OK_V1;
        }
        value = binding_value_from_atom(NULL);
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
    }
    if ((!scoped && binding_value_is_contextual(value) && atom_has_vars(value.skeleton)) ||
        !bindings_resolve_value_preview((Bindings *)context->bindings, value, &value))
        return CETTA_GSLT_TERM_VIEW_DEFER_V1;
    if (!scoped && binding_value_is_contextual(value) && atom_has_vars(value.skeleton))
        return CETTA_GSLT_TERM_VIEW_DEFER_V1;
    return bindings_value_term_cursor_v1(context->bindings, value, target_out);
}

CettaGsltTermViewStatusV1 bindings_resolve_term_cursor_v1(
        void *context, CettaGsltTermCursorV1 source,
        CettaGsltTermCursorV1 *target_out) {
    return bindings_resolve_cursor(context, source, target_out, false);
}

CettaGsltTermViewStatusV1 bindings_resolve_scoped_term_cursor_v1(
        void *context, CettaGsltTermCursorV1 source,
        CettaGsltTermCursorV1 *target_out) {
    return bindings_resolve_cursor(context, source, target_out, true);
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
        CettaFrameIdentity epoch, uint32_t first_entry, Atom **ground_out) {
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

typedef struct BindingsEpochAccelerator {
    BindingsActivationRead activation;
    const BindingsExclusiveFrame *exclusive;
} BindingsEpochAccelerator;

/* A pending expression, whose children are read in `scope` on a binding path
 * of `seen_len` variables, and whose image is built in `draft` from its first
 * changed child on; or a pending variable (`atom` NULL), whose image is stored
 * under `id` in `memo` once its bound value's image is found. */
typedef struct {
    Atom *atom;
    Atom *draft;
    AtomExprAssembly *assembly;
    BindingApplyMemo *memo;
    VarId id;
    BindingsApplyScope scope;
    CettaExprIndex next;
    uint32_t seen_len;
    /* How many quotations enclose `atom` (read only while structure hooks
     * are set). */
    uint32_t code_depth;
} BindingsApplyFrame;

/* The binding structure of the session's language (match.h). */
static __thread const BindingsStructureHooks *g_bindings_structure_hooks = NULL;

void bindings_structure_hooks_set(const BindingsStructureHooks *hooks) {
    g_bindings_structure_hooks = hooks;
}

const BindingsStructureHooks *bindings_structure_hooks(void) {
    return g_bindings_structure_hooks;
}

enum { BINDINGS_APPLY_INLINE_FRAMES = 32u };

typedef struct {
    BindingsApplyFrame *items;
    uint32_t len;
    uint32_t cap;
    bool heap;
} BindingsApplyFrames;

static bool bindings_apply_frames_push(BindingsApplyFrames *frames,
                                       BindingsApplyFrame frame) {
    if (frames->len == frames->cap) {
        if (frames->cap > UINT32_MAX / 2u)
            return false;
        uint32_t cap = frames->cap * 2u;
        BindingsApplyFrame *grown =
            cetta_malloc(sizeof(*grown) * (size_t)cap);
        memcpy(grown, frames->items, sizeof(*grown) * (size_t)frames->len);
        if (frames->heap)
            free(frames->items);
        frames->items = grown;
        frames->cap = cap;
        frames->heap = true;
    }
    frames->items[frames->len++] = frame;
    return true;
}

typedef enum {
    BINDINGS_APPLY_VAR_IMAGE,
    BINDINGS_APPLY_VAR_BOUND,
    BINDINGS_APPLY_VAR_FAILED,
} BindingsApplyVarStep;

/* A variable read in its value's own context: its memoised image, its unbound
 * reading (passed through the hook), or its bound value.  Without `memo` the
 * image is neither read from nor stored in the memo. */
#if defined(__GNUC__)
__attribute__((always_inline))
#endif
static inline BindingsApplyVarStep bindings_apply_rewrite_var_step(
        BindingsApplyFold *fold, Atom *atom, const BindingsApplyScope *scope,
        uint32_t seen_len, bool memo, VarId *id_out, Atom **image_out,
        BindingValue *bound_out) {
    BindingValue value = binding_value_make(atom, scope->epoch, scope->kind);
    VarId id = binding_value_variable_id(value);
    Atom *memoized = memo ? bindings_apply_memo_lookup(fold->memo, id) : NULL;
    if (memoized) {
        *image_out = memoized;
        return BINDINGS_APPLY_VAR_IMAGE;
    }
    BindingValue val = binding_value_from_atom(NULL);
    if (!fold->track_cycles ||
        !bindings_seen_var(fold->seen->ids, seen_len, id)) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_APPLY);
        val = bindings_lookup_value(fold->b, value);
    }
    if (!val.skeleton) {
        Atom *result = binding_value_materialize(fold->a, value);
        if (result && fold->rewrite_var)
            result = fold->rewrite_var(fold->a, result, fold->rewrite_ctx);
        if (!result)
            return BINDINGS_APPLY_VAR_FAILED;
        if (memo)
            bindings_apply_memo_store(fold->memo, id, result);
        *image_out = result;
        return BINDINGS_APPLY_VAR_IMAGE;
    }
    *id_out = id;
    *bound_out = val;
    return BINDINGS_APPLY_VAR_BOUND;
}

/* A variable read on either side of an activation: its memoised image, its
 * unbound reading (renamed into the activation's epoch on the original side),
 * or its bound value, with the memo its image belongs to.  Without
 * `use_memo` the image is neither read from nor stored in a memo
 * (*memo_out is NULL). */
static BindingsApplyVarStep bindings_apply_epoch_var_step(
        BindingsApplyFold *fold, Atom *atom, const BindingsApplyScope *scope,
        uint32_t seen_len, bool use_memo, BindingApplyMemo **memo_out,
        VarId *id_out, Atom **image_out, BindingValue *bound_out) {
    bool outer_lookup = scope->resolve_outer && !scope->original_side;
    uint32_t lookup_first = outer_lookup ? 0u : scope->first_entry;
    /* Stored lexical values use the full substitution.  They must not
     * reuse an unbound result from the query's suffix-limited view. */
    bool full_view = scope->resolve_outer && lookup_first == 0u;
    BindingApplyMemo *memo = !use_memo ? NULL
        : full_view ? fold->outer_memo : fold->local_memo;
    VarId lookup_id = scope->original_side
        ? var_epoch_id(atom->var_id, scope->epoch) : atom->var_id;
    Atom *memoized = memo ? bindings_apply_memo_lookup(memo, lookup_id) : NULL;
    if (memoized) {
        *image_out = memoized;
        return BINDINGS_APPLY_VAR_IMAGE;
    }
    BindingValue val = binding_value_from_atom(NULL);
    if (!fold->track_cycles ||
        !bindings_seen_var(fold->seen->ids, seen_len, lookup_id)) {
        const BindingsEpochAccelerator *fast = scope->fast;
        bool exclusive_owned = false;
        if (fast && fast->exclusive) {
            val = bindings_exclusive_frame_lookup(
                fast->exclusive, fold->b, lookup_id, &exclusive_owned);
        }
        const BindingsActivationView *frame =
            fast ? fast->activation.view : NULL;
        bool dense_present = false;
        bool dense_known = !exclusive_owned && scope->original_side &&
            frame &&
            bindings_activation_read_lookup(
                &fast->activation, atom->var_id, &val, &dense_present);
        if (dense_known && !dense_present)
            val = binding_value_from_atom(NULL);
        if (!exclusive_owned && !dense_known && !val.skeleton)
            val = bindings_lookup_value_since(
                fold->b, lookup_id, lookup_first);
    }
    if (!val.skeleton) {
        Atom *result = scope->original_side
            ? epoch_var_atom(fold->a, atom, scope->epoch) : atom;
        if (memo)
            bindings_apply_memo_store(memo, lookup_id, result);
        *image_out = result;
        return BINDINGS_APPLY_VAR_IMAGE;
    }
    *memo_out = memo;
    *id_out = lookup_id;
    *bound_out = val;
    return BINDINGS_APPLY_VAR_BOUND;
}

/* The scope a bound value is read in. */
static BindingsApplyScope bindings_apply_bound_scope(
        bool epoch_fold, const BindingsApplyScope *scope,
        BindingValue bound) {
    if (!epoch_fold)
        return (BindingsApplyScope){.epoch = binding_value_epoch(bound), .kind = binding_value_kind(bound)};
    if (!binding_value_is_contextual(bound)) {
        /* Stored syntax: its variables are read on the stored side. */
        BindingsApplyScope stored = *scope;
        stored.original_side = false;
        return stored;
    }
    return (BindingsApplyScope){
        .fast = scope->fast && scope->fast->exclusive ? scope->fast : NULL,
        .epoch = binding_value_epoch(bound),
        .first_entry = 0u,
        .original_side = true,
        .resolve_outer = true,
    };
}

typedef enum {
    BINDINGS_WALK_DESCEND,
    BINDINGS_WALK_ASCEND,
    BINDINGS_WALK_PREFIX,
    BINDINGS_WALK_CHILD,
    BINDINGS_WALK_COMPLETE,
    BINDINGS_WALK_REFUSED
} BindingsApplyWalkPhase;

typedef struct {
    BindingsApplyFold *fold;
    Atom *atom, *result;
    BindingsApplyScope scope;
    uint32_t seen_len, depth, expressions;
    CettaExprIndex prefix;
    const BindingsStructureHooks *hooks;
    BindingApplyGraph *graph;
    BindingApplyGraph observed;
    BindingsApplyFrame inline_items[BINDINGS_APPLY_INLINE_FRAMES];
    BindingsApplyFrames frames;
    BindingsApplyWalkPhase phase;
    bool may_observe;
} BindingsApplyWalk;

static inline void bindings_apply_walk_init(BindingsApplyWalk *walk,
        BindingsApplyFold *fold, Atom *atom, BindingsApplyScope scope,
        uint32_t seen_len, bool epoch_fold, BindingApplyGraph *graph) {
    *walk = (BindingsApplyWalk){.fold = fold, .atom = atom, .scope = scope,
        .seen_len = seen_len, .hooks = g_bindings_structure_hooks, .graph = graph};
    walk->frames = (BindingsApplyFrames){.items = walk->inline_items,
        .cap = BINDINGS_APPLY_INLINE_FRAMES};
    walk->may_observe = !walk->hooks && !graph && !epoch_fold &&
        !fold->rewrite_var && !fold->track_cycles;
}

static void bindings_apply_walk_release(BindingsApplyWalk *walk) {
    for (uint32_t i = 0u; i < walk->frames.len; i++)
        atom_expr_assembly_free(walk->frames.items[i].assembly);
    if (walk->frames.heap) free(walk->frames.items);
    free(walk->observed.entries);
}

/* One traversal serves synchronous and retained callers. Synchronous entry
 * points specialize away the yield boundaries and keep their ordinary atom
 * constructor. Retained private construction folds each child's summary on
 * arrival; an interning arena keeps its atomic finishing provider.
 * Binding/cache operations, hooks, allocation and retirement have separate
 * service prices. A walk action is not a wall-clock or whole-slot bound. */
#if defined(__GNUC__)
__attribute__((always_inline))
#endif
static inline BindingsApplyWalkPhase bindings_apply_walk_drive(
        BindingsApplyWalk *walk, bool epoch_fold, bool yielding) {
    BindingsApplyFold *fold = walk->fold;
    const BindingsStructureHooks *hooks = walk->hooks;
#define BINDINGS_WALK_NEXT(next, label) do { \
    walk->phase = (next); \
    if (yielding) return walk->phase; \
    goto label; \
} while (0)
    switch (walk->phase) {
    case BINDINGS_WALK_DESCEND: goto descend;
    case BINDINGS_WALK_ASCEND: goto ascend;
    case BINDINGS_WALK_PREFIX: goto prefix;
    case BINDINGS_WALK_CHILD: goto child;
    case BINDINGS_WALK_COMPLETE: case BINDINGS_WALK_REFUSED: return walk->phase;
    }
    goto failed;
descend:
    walk->result = NULL;
    if (!atom_has_vars(walk->atom)) {
        walk->result = walk->atom;
    } else if (epoch_fold) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_APPLY_EPOCH_NODE_VISIT);
        if (fold->node_visits && *fold->node_visits != UINT64_MAX) (*fold->node_visits)++;
    } else {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_APPLY_REWRITE_NODE_VISIT);
    }
    /* Unbound materialized support is unchanged at every subtree, not
     * only at the outer entry. Hooks and lexical renaming still walk. */
    if (!walk->result && walk->frames.len > 0u && !epoch_fold && !fold->rewrite_var &&
        walk->atom->kind == ATOM_EXPR && walk->scope.kind == BINDING_VALUE_MATERIALIZED) {
        VarId support = atom_single_variable_id(walk->atom);
        if (!var_id_is_none(support) &&
            !bindings_lookup_context_id(fold->b, binding_value_from_atom(walk->atom),
                                        support).skeleton)
            walk->result = walk->atom;
    }
    if (walk->graph && !walk->result && walk->atom->kind == ATOM_EXPR) {
        bool created = false;
        BindingApplyGraphEntry *active = walk->graph->active_path
            ? bindings_apply_graph_enter(walk->graph->active_path, walk->atom,
                walk->scope, 0u, &created) : NULL;
        if (walk->graph->active_path && (!active || active->active)) goto failed;
        BindingApplyGraphEntry *entry = bindings_apply_graph_enter(walk->graph,
            walk->atom, walk->scope, walk->depth, &created);
        if (!entry && walk->graph == &walk->observed) walk->graph = NULL;
        else if (!entry) goto failed;
        else if (!created) {
            if (!entry->image) goto failed;
            walk->result = entry->image;
        }
        if (!walk->result && active) active->active = true;
    }
    if (!walk->result && walk->atom->kind == ATOM_VAR) {
        bool use_memo = !hooks || walk->depth == 0u;
        BindingApplyMemo *memo = use_memo ? fold->memo : NULL;
        VarId id = VAR_ID_NONE;
        BindingValue bound = binding_value_from_atom(NULL);
        BindingsApplyVarStep step = epoch_fold
            ? bindings_apply_epoch_var_step(fold, walk->atom, &walk->scope,
                walk->seen_len, use_memo, &memo, &id, &walk->result, &bound)
            : bindings_apply_rewrite_var_step(fold, walk->atom, &walk->scope,
                walk->seen_len, use_memo, &id, &walk->result, &bound);
        if (step == BINDINGS_APPLY_VAR_FAILED) goto failed;
        if (step == BINDINGS_APPLY_VAR_BOUND) {
            if (fold->track_cycles) {
                if (!bindings_apply_seen_reserve(fold->seen, walk->seen_len,
                        walk->seen_len + 1u)) goto failed;
                fold->seen->ids[walk->seen_len++] = id;
            }
            if (!bindings_apply_frames_push(&walk->frames,
                    (BindingsApplyFrame){.memo = memo, .id = id,
                        .code_depth = walk->depth})) goto failed;
            walk->scope = bindings_apply_bound_scope(epoch_fold, &walk->scope, bound);
            walk->atom = bound.skeleton;
            BINDINGS_WALK_NEXT(BINDINGS_WALK_DESCEND, descend);
        }
    } else if (!walk->result && walk->atom->kind == ATOM_EXPR) {
        if (!bindings_apply_frames_push(&walk->frames,
                (BindingsApplyFrame){.atom = walk->atom, .scope = walk->scope,
                    .seen_len = walk->seen_len, .code_depth = walk->depth})) goto failed;
        if (walk->may_observe && ++walk->expressions == BINDINGS_APPLY_GRAPH_AFTER)
            walk->graph = &walk->observed;
    } else if (!walk->result) {
        walk->result = walk->atom;
    }
    BINDINGS_WALK_NEXT(BINDINGS_WALK_ASCEND, ascend);
ascend:
    if (!walk->frames.len) {
        walk->phase = BINDINGS_WALK_COMPLETE;
        return walk->phase;
    } else {
        BindingsApplyFrame *top = &walk->frames.items[walk->frames.len - 1u];
        if (!top->atom) {
            if (top->memo) bindings_apply_memo_store(top->memo, top->id, walk->result);
            walk->frames.len--;
            BINDINGS_WALK_NEXT(BINDINGS_WALK_ASCEND, ascend);
        }
        Atom **elems = top->atom->expr.elems;
        CettaExprLen len = top->atom->expr.len;
        uint32_t child_depth = hooks ? top->code_depth +
            (bindings_structure_quotes_children(top->atom) ? 1u : 0u) : 0u;
        if (walk->result) {
            if (hooks && hooks->code_reading && child_depth > 0u &&
                elems[top->next]->kind == ATOM_VAR) {
                walk->result = hooks->code_reading(walk->result);
                if (!walk->result) goto failed;
            }
            if (!top->draft && !top->assembly && walk->result != elems[top->next]) {
                if (yielding && !fold->a->hashcons) {
                    top->assembly = atom_expr_assembly_new(fold->a, len);
                    if (!top->assembly) goto failed;
                } else {
                    top->draft = atom_expr_builder_begin(fold->a, len);
                    if (!top->draft) goto failed;
                }
                walk->prefix = 0u;
                BINDINGS_WALK_NEXT(BINDINGS_WALK_PREFIX, prefix);
            }
            BINDINGS_WALK_NEXT(BINDINGS_WALK_CHILD, child);
        }
        if (top->next < len) {
            if (!atom_has_vars(elems[top->next])) {
                if (top->assembly) {
                    if (!atom_expr_assembly_append(top->assembly, elems[top->next])) goto failed;
                } else if (top->draft) top->draft->expr.elems[top->next] = elems[top->next];
                top->next++;
                BINDINGS_WALK_NEXT(BINDINGS_WALK_ASCEND, ascend);
            }
            walk->atom = elems[top->next];
            walk->scope = top->scope;
            walk->seen_len = top->seen_len;
            walk->depth = child_depth;
            BINDINGS_WALK_NEXT(BINDINGS_WALK_DESCEND, descend);
        }
        bool changed = top->draft || top->assembly;
        walk->result = top->assembly ? atom_expr_assembly_finish_splicing(top->assembly)
            : top->draft ? atom_expr_builder_finish(fold->a, top->draft) : top->atom;
        atom_expr_assembly_free(top->assembly);top->assembly = NULL;
        if (walk->result && changed && hooks && hooks->formed_node &&
            len > 0u && elems[0]->kind == ATOM_VAR &&
            walk->result->expr.elems[0] && walk->result->expr.elems[0]->kind == ATOM_SYMBOL)
            walk->result = hooks->formed_node(fold->a, walk->result, top->code_depth > 0u);
        else if (walk->result && changed && hooks && hooks->own_list &&
                atom_sequence_is_own_list(top->atom))
            walk->result = hooks->own_list(fold->a, walk->result);
        if (walk->graph && walk->graph->cap) {
            BindingApplyGraphEntry *entry = bindings_apply_graph_slot(walk->graph,
                top->atom, top->scope, top->code_depth);
            if (entry->source) entry->image = walk->result;
            if (walk->graph->active_path)
                bindings_apply_graph_slot(walk->graph->active_path, top->atom,
                    top->scope, 0u)->active = false;
        }
        walk->frames.len--;
        if (!walk->result) goto failed;
        BINDINGS_WALK_NEXT(BINDINGS_WALK_ASCEND, ascend);
    }
prefix: {
        BindingsApplyFrame *top = &walk->frames.items[walk->frames.len - 1u];
        if (walk->prefix < top->next) {
            Atom *item = top->atom->expr.elems[walk->prefix];
            if (top->assembly) {
                if (!atom_expr_assembly_append(top->assembly, item)) goto failed;
            } else top->draft->expr.elems[walk->prefix] = item;
            walk->prefix++;
            BINDINGS_WALK_NEXT(BINDINGS_WALK_PREFIX, prefix);
        }
        BINDINGS_WALK_NEXT(BINDINGS_WALK_CHILD, child);
    }
child: {
        BindingsApplyFrame *top = &walk->frames.items[walk->frames.len - 1u];
        if (top->assembly) {
            if (!atom_expr_assembly_append(top->assembly, walk->result)) goto failed;
        } else if (top->draft) top->draft->expr.elems[top->next] = walk->result;
        top->next++;
        walk->result = NULL;
        BINDINGS_WALK_NEXT(BINDINGS_WALK_ASCEND, ascend);
    }
failed:
    walk->phase = BINDINGS_WALK_REFUSED;
    return walk->phase;
#undef BINDINGS_WALK_NEXT
}

/* The existing synchronous entries drain the same traversal without retaining
 * scheduling cuts or changing their constructor provider. */
#if defined(__GNUC__)
__attribute__((always_inline))
#endif
static inline Atom *bindings_apply_fold_impl(BindingsApplyFold *fold, Atom *atom,
        BindingsApplyScope scope, uint32_t seen_len, bool epoch_fold,
        BindingApplyGraph *graph) {
    BindingsApplyWalk walk;
    bindings_apply_walk_init(&walk, fold, atom, scope, seen_len, epoch_fold, graph);
    BindingsApplyWalkPhase phase = bindings_apply_walk_drive(&walk, epoch_fold, false);
    Atom *result = phase == BINDINGS_WALK_COMPLETE ? walk.result : NULL;
    bindings_apply_walk_release(&walk);
    return result;
}

struct BindingsApplySession {
    BindingsApplyFold fold;
    BindingsApplyWalk walk;
    BindingApplySeen seen;
    BindingApplyMemo memo;
    BindingApplyGraph graph, active_path;
    BindingsStructureHooks hooks;
    VarId seen_inline[BINDINGS_SEEN_STACK_CAP];
    VarId memo_ids[BINDINGS_MEMO_STACK_CAP];
    Atom *memo_values[BINDINGS_MEMO_STACK_CAP];
    Arena *arena;
    HashConsTable *hashcons;
    uint32_t arena_identity;
    uint64_t arena_epoch, steps;
};

BindingsApplySession *bindings_apply_session_new(const Bindings *b, Arena *a,
        BindingValue source, bool shared_graph) {
    if (!b || !a || !source.skeleton) return NULL;
    BindingsApplySession *session = calloc(1u, sizeof(*session));
    if (!session) return NULL;
    session->arena = a;session->hashcons = a->hashcons;
    session->arena_identity = a->identity;session->arena_epoch = a->reset_epoch;
    bindings_apply_seen_init(&session->seen, session->seen_inline, BINDINGS_SEEN_STACK_CAP);
    bindings_apply_memo_init(&session->memo, session->memo_ids,
        session->memo_values, BINDINGS_MEMO_STACK_CAP);
    session->fold = (BindingsApplyFold){.b = (Bindings *)b, .a = a,
        .seen = &session->seen, .memo = &session->memo,
        .track_cycles = b->cycle_state != BINDINGS_CYCLE_ACYCLIC};
    const BindingsStructureHooks *hooks = bindings_structure_hooks();
    if (hooks) session->hooks = *hooks;
    if (shared_graph && !session->fold.track_cycles)
        session->graph.active_path = hooks ? &session->active_path : NULL;
    bindings_apply_walk_init(&session->walk, &session->fold, source.skeleton,
        (BindingsApplyScope){.epoch = binding_value_epoch(source),
            .kind = binding_value_kind(source)}, 0u, false,
        shared_graph && !session->fold.track_cycles ? &session->graph : NULL);
    session->walk.hooks = hooks ? &session->hooks : NULL;
    /* Preserve the existing identity cases, including their borrowed root. */
    if ((!bindings_has_bound_values(b) &&
            binding_value_kind(source) == BINDING_VALUE_MATERIALIZED) ||
            !atom_has_vars(source.skeleton)) {
        session->walk.phase = BINDINGS_WALK_ASCEND;
        session->walk.result = source.skeleton;
    }
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_APPLY);
    return session;
}

BindingsApplyStatus bindings_apply_session_advance(BindingsApplySession *session,
        uint64_t allowance, Atom **result, uint64_t *used) {
    if (result) *result = NULL;
    if (used) *used = 0u;
    if (!session || !result || !used) return BINDINGS_APPLY_REFUSED;
    if (session->arena->identity != session->arena_identity ||
            session->arena->reset_epoch != session->arena_epoch ||
            session->arena->hashcons != session->hashcons) {
        session->walk.phase = BINDINGS_WALK_REFUSED;
        return BINDINGS_APPLY_REFUSED;
    }
    while (*used < allowance && session->walk.phase != BINDINGS_WALK_COMPLETE &&
            session->walk.phase != BINDINGS_WALK_REFUSED) {
        if (session->steps == UINT64_MAX) {
            session->walk.phase = BINDINGS_WALK_REFUSED;
            break;
        }
        session->steps++;(*used)++;
        bindings_apply_walk_drive(&session->walk, false, true);
    }
    if (session->walk.phase == BINDINGS_WALK_REFUSED) return BINDINGS_APPLY_REFUSED;
    if (session->walk.phase == BINDINGS_WALK_COMPLETE) {
        *result = session->walk.result;
        return BINDINGS_APPLY_COMPLETE;
    }
    return BINDINGS_APPLY_PENDING;
}

uint64_t bindings_apply_session_steps(const BindingsApplySession *session) {
    return session ? session->steps : 0u;
}

void bindings_apply_session_free(BindingsApplySession *session) {
    if (!session) return;
    bindings_apply_walk_release(&session->walk);
    bindings_apply_memo_release(&session->memo);
    bindings_apply_seen_release(&session->seen);
    free(session->graph.entries);free(session->active_path.entries);
    free(session);
}

static Atom *bindings_apply_fold(BindingsApplyFold *fold, Atom *atom,
                                 BindingsApplyScope scope, uint32_t seen_len) {
    return bindings_apply_fold_impl(fold, atom, scope, seen_len, false, NULL);
}

static Atom *bindings_apply_shared_fold(BindingsApplyFold *fold,
    BindingApplyGraph *graph, Atom *atom, BindingsApplyScope scope, uint32_t seen_len) {
    return bindings_apply_fold_impl(fold, atom, scope, seen_len, false, graph);
}

static Atom *bindings_apply_epoch_fold(BindingsApplyFold *fold, Atom *atom,
        BindingsApplyScope scope, uint32_t seen_len) {
    return bindings_apply_fold_impl(fold, atom, scope, seen_len, true, NULL);
}

static Atom *bindings_apply_epoch_from(
        Bindings *b, Arena *a, Atom *atom, CettaFrameIdentity epoch,
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
    if (track_cycles && !var_id_is_none(initial_seen_id)) {
        if (!bindings_apply_seen_reserve(&seen, 0u, 1u)) {
            bindings_apply_memo_release(&outer_memo);
            bindings_apply_memo_release(&local_memo);
            bindings_apply_seen_release(&seen);
            return NULL;
        }
        seen.ids[initial_seen_len++] = initial_seen_id;
    }
    BindingsApplyFold fold = {
        .b = b,
        .a = a,
        .seen = &seen,
        .track_cycles = track_cycles,
        .local_memo = &local_memo,
        .outer_memo = &outer_memo,
        .node_visits = node_visits,
    };
    Atom *result = bindings_apply_epoch_fold(
        &fold, atom,
        (BindingsApplyScope){
            .fast = fast,
            .epoch = epoch,
            .first_entry = first_entry,
            .original_side = original_side,
            .resolve_outer = resolve_outer,
        },
        initial_seen_len);
    bindings_apply_memo_release(&outer_memo);
    bindings_apply_memo_release(&local_memo);
    bindings_apply_seen_release(&seen);
    return result;
}

static Atom *bindings_apply_epoch_view(Bindings *b, Arena *a, Atom *atom,
                                       CettaFrameIdentity epoch,
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
                                 CettaFrameIdentity epoch, uint32_t first_entry) {
    return bindings_apply_epoch_view(
        b, a, atom, epoch, first_entry, false,
        NULL, NULL);
}

Atom *bindings_apply_epoch_then_all(Bindings *b, Arena *a, Atom *atom,
                                    CettaFrameIdentity epoch,
                                    uint32_t first_entry) {
    return bindings_apply_epoch_view(
        b, a, atom, epoch, first_entry, true,
        NULL, NULL);
}

Atom *bindings_apply_activation_view_then_all(
        Bindings *bindings, Arena *a, Atom *atom,
        const BindingsActivationView *frame) {
    if (!bindings || !a || !atom || !frame || cetta_frame_identity_is_none(frame->epoch) ||
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
        !frame->schema || cetta_frame_identity_is_none(frame->epoch))
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
        cetta_frame_identity_is_none(frame->epoch) ||
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
    if (!var_id_eq(source_variable->var_id, source_id) ||
        !template_variable || template_variable->kind != ATOM_VAR ||
        !var_id_eq(template_variable->var_id, source_id))
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
        binding_value_is_contextual(value) ? binding_value_epoch(value) : frame->epoch,
        binding_value_is_contextual(value) ? 0u : frame->first_entry, true,
        binding_value_is_contextual(value) ? NULL : &accelerator,
        binding_value_is_contextual(value), var_epoch_id(source_id, frame->epoch), NULL);
}

Atom *bindings_resolve_activation_view_slot_root(
        Bindings *bindings, Arena *a,
        const BindingsActivationView *frame,
        Atom *source_variable, uint32_t slot) {
    if (!bindings || !a || !frame || slot >= frame->len ||
        cetta_frame_identity_is_none(frame->epoch) ||
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
    if (!var_id_eq(source_variable->var_id, source_id) ||
        !template_variable || template_variable->kind != ATOM_VAR ||
        !var_id_eq(template_variable->var_id, source_id))
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
                           CettaFrameIdentity epoch) {
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
    if (old_cap > SIZE_MAX / 2u)
        return false;
    size_t new_cap = old_cap ? old_cap * 2u : FRESHEN_EPOCH_MEMO_INLINE_CAP;
    if (new_cap > SIZE_MAX / sizeof(FreshenEpochMemoSlot))
        return false;
    FreshenEpochMemoSlot *old_slots = memo->slots;
    bool old_inline = old_slots == memo->inline_slots;
    uint64_t old_inline_occupied = memo->inline_occupied;
    FreshenEpochMemoSlot *new_slots =
        malloc(sizeof(FreshenEpochMemoSlot) * new_cap);
    if (!new_slots)
        return false;
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
    if (memo->used + 1u >= memo->cap - memo->cap / 4u &&
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

typedef Atom *(*RenameVariable)(Arena *, Atom *, void *);

typedef struct {
    Atom *source;
    Atom *draft;
    CettaExprIndex next;
} RenameVarsFrame;

/* All variable-renaming policies share one postorder traversal.  Only a
 * variable is interpreted by the policy; grounded values retain their own
 * context.  A draft is allocated only after a child changes, and the memo
 * preserves source sharing without accepting a recurrence on the active path. */
static Atom *rename_vars_walk(Arena *a, Atom *root, RenameVariable rename,
                              void *context, bool count_visits) {
    if (!a || !root || !rename)
        return NULL;
    RenameVarsFrame inline_frames[32];
    RenameVarsFrame *frames = inline_frames;
    size_t len = 0u, cap = sizeof inline_frames / sizeof inline_frames[0];
    FreshenEpochMemo memo;
    freshen_epoch_memo_init(&memo);
    Atom *current = root;
    Atom *result = NULL;
    for (;;) {
        if (count_visits)
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_RENAME_VARS);
        if (!current)
            goto fail;
        if (!atom_has_vars(current)) {
            result = current;
        } else {
            result = freshen_epoch_memo_lookup(&memo, current);
            if (result == &g_rename_walk_active)
                goto fail;
            if (!result) {
                if (current->kind == ATOM_VAR) {
                    result = rename(a, current, context);
                } else if (current->kind == ATOM_EXPR && current->expr.len) {
                    if (!freshen_epoch_memo_store(
                            &memo, current, &g_rename_walk_active))
                        goto fail;
                    if (len == cap) {
                        if (cap > SIZE_MAX / 2u ||
                            cap * 2u > SIZE_MAX / sizeof(*frames))
                            goto fail;
                        size_t next_cap = cap * 2u;
                        RenameVarsFrame *next = frames == inline_frames
                            ? malloc(sizeof(*next) * next_cap)
                            : realloc(frames, sizeof(*next) * next_cap);
                        if (!next)
                            goto fail;
                        if (frames == inline_frames)
                            memcpy(next, inline_frames, sizeof(*next) * len);
                        frames = next;
                        cap = next_cap;
                    }
                    frames[len++] = (RenameVarsFrame){current, NULL, 0u};
                    current = current->expr.elems[0];
                    continue;
                } else {
                    result = current;
                }
                if (!result || !freshen_epoch_memo_store(&memo, current, result))
                    goto fail;
            }
        }
        while (len) {
            RenameVarsFrame *frame = &frames[len - 1u];
            Atom *source = frame->source;
            if (!frame->draft && result != source->expr.elems[frame->next]) {
                frame->draft = atom_expr_builder_begin(a, source->expr.len);
                if (!frame->draft)
                    goto fail;
                for (CettaExprIndex i = 0; i < frame->next; i++)
                    frame->draft->expr.elems[i] = source->expr.elems[i];
            }
            if (frame->draft)
                frame->draft->expr.elems[frame->next] = result;
            if (++frame->next < source->expr.len) {
                current = source->expr.elems[frame->next];
                break;
            }
            result = frame->draft
                ? (atom_is_he_evaluated(source)
                    ? atom_expr_rebuild(a, source, frame->draft->expr.elems, source->expr.len)
                    : atom_expr_builder_finish(a, frame->draft)) : source;
            if (!result || !freshen_epoch_memo_store(&memo, source, result))
                goto fail;
            len--;
        }
        if (!len)
            break;
    }
    freshen_epoch_memo_free(&memo);
    if (frames != inline_frames)
        free(frames);
    return result;

fail:
    freshen_epoch_memo_free(&memo);
    if (frames != inline_frames)
        free(frames);
    return NULL;
}

static Atom *rename_epoch_variable(Arena *a, Atom *atom, void *context) {
    return epoch_var_atom(a, atom, *(const CettaFrameIdentity *)context);
}

/* Keep the address-taken epoch and walk frame out of materialized reads. */
__attribute__((noinline))
Atom *atom_freshen_epoch(Arena *a, Atom *atom, CettaFrameIdentity epoch) {
    return rename_vars_walk(a, atom, rename_epoch_variable, &epoch, false);
}

Atom *binding_value_materialize(Arena *a, BindingValue value) {
    if (!value.skeleton)
        return NULL;
    switch (binding_value_kind(value)) {
    case BINDING_VALUE_MATERIALIZED:
        return value.skeleton;
    case BINDING_VALUE_CONTEXTUAL:
    case BINDING_VALUE_CONTEXTUAL_PERSISTENT:
        return a ? atom_freshen_epoch(a, value.skeleton, binding_value_epoch(value)) : NULL;
    }
    return NULL;
}

bool binding_value_equal(BindingValue left, BindingValue right) {
    if (!left.skeleton || !right.skeleton)
        return false;
    if ((binding_value_kind(left) == BINDING_VALUE_MATERIALIZED &&
         binding_value_kind(right) == BINDING_VALUE_MATERIALIZED) ||
        (!atom_has_vars(left.skeleton) && !atom_has_vars(right.skeleton)))
        return atom_eq(left.skeleton, right.skeleton);
    return binding_values_eq_under_bindings(NULL, left, right);
}

/* Matching may identify two Space representations in the currently held
 * read view. Storage equality and captured-binding factoring stay raw. */
static bool binding_value_match_equal(BindingValue left, BindingValue right) {
    return binding_value_equal(left, right) ||
        (left.skeleton && right.skeleton &&
         binding_values_eq_under_bindings_checked(NULL, left, right, NULL));
}

typedef struct {
    const BindingsFrameIndexEntry *frame;
    uint32_t slot;
} BindingsObservedFrameSlot;

static int bindings_observed_frame_slot_compare(
        const void *left_raw, const void *right_raw) {
    const BindingsObservedFrameSlot *left = left_raw;
    const BindingsObservedFrameSlot *right = right_raw;
    uint64_t left_version = bindings_frame_slot_version(left->frame, left->slot);
    uint64_t right_version = bindings_frame_slot_version(right->frame, right->slot);
    if (left_version != right_version)
        return left_version < right_version ? -1 : 1;
    VarId left_id = var_epoch_id(
        bindings_frame_slot_source_id(left->frame, left->slot), left->frame->epoch);
    VarId right_id = var_epoch_id(
        bindings_frame_slot_source_id(right->frame, right->slot), right->frame->epoch);
    return var_id_cmp(left_id, right_id);
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
                    if (!bindings_frame_slot_value(frame, slot).skeleton)
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
                    a, bindings_frame_slot_value(frame, slot));
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
        !cetta_frame_identity_eq(ref.identity, var_epoch_suffix(id)) ||
        bb->trail_len == 0u) {
        return false;
    }
    BindingsFrameIndexEntry *frame =
        bindings_frame_index_find_ref(
            bb->current.frame_index, ref);
    if (!frame)
        return false;
    /* Admitting the RHS context may extend a sparse captured inventory.
     * Its physical slot positions can change, but the variable identity
     * cannot. Complete activation inventories keep the supplied coordinate;
     * sparse images translate it at this write boundary before recording undo. */
    VarId source_id = var_source_id(id);
    if ((slot >= frame->slot_len ||
         !var_id_eq(bindings_frame_slot_source_id(frame, slot), source_id)) &&
        !bindings_frame_index_find_slot(frame, source_id, &slot)) {
        return false;
    }
    if (bb->frame_undo_len == UINT32_MAX ||
        !bindings_builder_frame_undo_reserve(
            bb, bb->frame_undo_len + 1u)) {
        return false;
    }
    BindingsFrameUndoEntry undo = {
        .previous_value = bindings_frame_slot_value(frame, slot),
        .previous_write_version = bindings_frame_slot_version(frame, slot),
        .frame_ref = ref,
        .source_id = var_base_id(id),
        .slot = slot,
        .trail_mark = bb->trail_len - 1u,
    };
    bool authority_moved = false;
    if (!bindings_frame_index_write_resolved_slot(
            &bb->current, frame, slot, value,
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
        prime_occurrence_copy(&bb->prime_trail[bb->prime_trail_len++],bb->current.prime_ext);
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
    bindings_builder_drop_prime_trail(bb,entry->prime_state_mark);
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
    bb->history = (BindingsBuilderHistory){0};
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
        bindings_builder_drop_prime_trail(bb,0u);
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
    bb->history = (BindingsBuilderHistory){0};
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
        for (uint32_t i=0u;i<dst->prime_trail_len;i++)
            prime_need_reader_context_retain(dst->prime_trail[i].reader_context);
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
    if (!bb || !owner)
        return false;
    if (!bb->current.prime_ext && !bb->prime_trail_len)
        return true;
    AtomDeepCopySession *session = atom_deep_copy_session_new(owner);
    if (!session)
        return false;
    bool ok = bindings_builder_promote_prime_atoms_with_session(bb, owner, session);
    atom_deep_copy_session_free(session);
    return ok;
}

static bool bindings_builder_readers_qualified(const BindingsBuilder *builder,bool owned_copy,
    const Arena *owner,const AtomDeepCopySession *session) {
    if (!bindings_occurrence_readers_qualified(builder->current.prime_ext,owned_copy,owner,session) ||
        builder->prime_trail_len>builder->prime_trail_cap ||
        (builder->prime_trail_len && !builder->prime_trail)) return false;
    for (uint32_t i=0u;i<builder->prime_trail_len;i++)
        if (!bindings_occurrence_readers_qualified(&builder->prime_trail[i],owned_copy,owner,session)) return false;
    return true;
}

static bool bindings_builder_move_prime_atoms_with_session(
        BindingsBuilder *bb, Arena *owner, AtomDeepCopySession *session,
        bool owned_copy) {
    if (!bb || !bindings_builder_readers_qualified(bb,owned_copy,owner,session) ||
        !atom_deep_copy_session_targets(session, owner)) {
        return false;
    }
    if (bb->current.prime_ext && !bindings_promote_occurrence_with_session(
            owner, session, bb->current.prime_ext, owned_copy)) {
        return false;
    }
    for (uint32_t i = 0u; i < bb->prime_trail_len; i++) {
        PrimeOccurrence *occurrence = &bb->prime_trail[i];
        if (!bindings_promote_occurrence_with_session(
                owner, session, occurrence, owned_copy)) {
            return false;
        }
    }
    return true;
}

bool bindings_builder_promote_prime_atoms_with_session(
        BindingsBuilder *bb, Arena *owner, AtomDeepCopySession *session) {
    return bindings_builder_move_prime_atoms_with_session(
        bb, owner, session, false);
}

bool bindings_builder_promote_atoms_to_arena(
        BindingsBuilder *bb, Arena *owner) {
    if (!bb || !owner)
        return false;
    AtomDeepCopySession *session = atom_deep_copy_session_new(owner);
    if (!session)
        return false;
    bool promoted = bindings_builder_promote_atoms_with_session(bb, owner, session);
    atom_deep_copy_session_free(session);
    return promoted;
}

static bool bindings_builder_move_atoms_with_session(
        BindingsBuilder *bb, Arena *owner, AtomDeepCopySession *session,
        bool owned_copy) {
    if (!bb || !bindings_builder_readers_qualified(bb,owned_copy,owner,session) ||
        !atom_deep_copy_session_targets(session, owner))
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
        for (uint32_t slot = 0u; slot < source_schema->cap; slot++) {
            has_name_keys = has_name_keys ||
                source_schema->name_keys[slot] != NULL;
        }
        if (!has_name_keys &&
            !bindings_frame_schema_copy_lookup(source_schema, session))
            continue;
        /* The registration log and compiled plans may share this immutable
         * inventory. Use their enclosing session's one presentation image. */
        BindingsFrameSchema *copy_schema =
            bindings_frame_schema_copy_with_session(source_schema, session);
        if (!copy_schema) {
            promoted = false;
            break;
        }
        bindings_frame_schema_release(source_schema);
        bb->frame_registration_undo[i].previous_schema = copy_schema;
    }
    return promoted &&
        bindings_builder_move_prime_atoms_with_session(
            bb, owner, session, owned_copy);
}

bool bindings_builder_promote_atoms_with_session(
        BindingsBuilder *bb, Arena *owner, AtomDeepCopySession *session) {
    return bindings_builder_move_atoms_with_session(bb, owner, session, false);
}

bool bindings_builder_copy_owned_atoms_with_session(
        BindingsBuilder *bb, Arena *owner, AtomDeepCopySession *session) {
    return bindings_builder_move_atoms_with_session(bb, owner, session, true);
}

bool bindings_builder_payloads_satisfy(const BindingsBuilder *bb,
        AtomTreePredicate atoms, PrimeNeedStateCellPredicate cells, void *context) {
    if (!bb || !bindings_payloads_satisfy(&bb->current, atoms, cells, context) ||
        bb->prime_trail_len > bb->prime_trail_cap ||
        bb->frame_undo_len > bb->frame_undo_cap ||
        bb->frame_registration_undo_len > bb->frame_registration_undo_cap ||
        (bb->prime_trail_len && !bb->prime_trail) ||
        (bb->frame_undo_len && !bb->frame_undo) ||
        (bb->frame_registration_undo_len && !bb->frame_registration_undo)) return false;
    for (uint32_t i = 0u; i < bb->prime_trail_len; i++)
        if (!bindings_occurrence_payloads_satisfy(&bb->prime_trail[i], atoms, cells, context))
            return false;
    for (uint32_t i = 0u; i < bb->frame_undo_len; i++) {
        Atom *atom = bb->frame_undo[i].previous_value.skeleton;
        if (atom && !atoms(atom, context)) return false;
    }
    for (uint32_t i = 0u; i < bb->frame_registration_undo_len; i++) {
        const BindingsFrameSchema *schema = bb->frame_registration_undo[i].previous_schema;
        for (uint32_t slot = 0u; schema && slot < schema->cap; slot++)
            if (schema->name_keys[slot] && !atoms(schema->name_keys[slot], context))
                return false;
    }
    return true;
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
    bindings_builder_drop_prime_trail(bb,0u);
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
        if (prime || bb->current.prime_ext) {
            PrimeOccurrence *current=bindings_prime_ext_materialize(&bb->current);
            if (prime) prime_occurrence_assign(current,prime);
            else prime_occurrence_clear(current);
        }
        bindings_builder_drop_prime_trail(bb,entry->prime_state_mark);
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
    if (bb->history.rollback)
        bb->history.rollback(bb->history.context, mark);
}

void bindings_builder_commit(BindingsBuilder *bb) {
    bindings_builder_discard_owner_history(bb);
    bb->trail_len = 0;
    bb->frame_undo_len = 0u;
    bindings_builder_discard_frame_registration_history(bb, false);
    bindings_builder_drop_prime_trail(bb,0u);
    bb->unobserved_write_region_has_checkpoint = false;
    if (bb->history.commit)
        bb->history.commit(bb->history.context);
}

void bindings_builder_set_history(BindingsBuilder *bb,
                                  BindingsBuilderHistory history) {
    if (bb)
        bb->history = history;
}

BindingsBuilderWriteMark bindings_builder_write_mark(
    const BindingsBuilder *bb) {
    return bb ? (BindingsBuilderWriteMark){bb->current.len, bb->frame_undo_len}
              : (BindingsBuilderWriteMark){0u, 0u};
}

bool bindings_builder_visit_bound_since(
    const BindingsBuilder *bb, BindingsBuilderWriteMark since,
    bool (*visit)(void *context, VarId var), void *context) {
    if (!bb || !visit)
        return false;
    for (uint32_t row = since.entries; row < bb->current.len; row++) {
        if (!visit(context, bindings_entry_at(&bb->current, row)->var_id))
            return false;
    }
    for (uint32_t index = since.frame_writes; index < bb->frame_undo_len;
         index++) {
        const BindingsFrameUndoEntry *undo = &bb->frame_undo[index];
        if (!visit(context, var_epoch_id(var_id_from_base(undo->source_id),
                                         undo->frame_ref.identity)))
            return false;
    }
    return true;
}

uint32_t bindings_builder_history_mark(BindingsBuilder *bb) {
    if (!bb)
        return UINT32_MAX;
    bb->unobserved_write_region_has_checkpoint = false;
    uint32_t mark = bb->trail_len;
    bool created = false;
    if (!bindings_builder_snapshot(bb, &created) || !created)
        return UINT32_MAX;
    return mark;
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
                                                     bool normalize_constraints,
                                                     CettaMatchCompletion *failure);

static bool bindings_builder_add_id_internal_with_cycle_evidence(
        BindingsBuilder *bb, VarId var_id,
        SymbolId spelling, Atom *name_key, BindingValue val,
        bool has_cycle_evidence,
        BindingsReachability cycle_evidence,
        BindingValue *authoritative_value_out, CettaMatchCompletion *failure) {
    if (failure) *failure = CETTA_MATCH_INCOMPLETE;
    if (authoritative_value_out)
        *authoritative_value_out = binding_value_from_atom(NULL);
    if (!bb)
        return false;
    const BindingsFrameIndexEntry *framed_entry = NULL;
    uint32_t framed_slot = 0u;
    bool framed = binding_id_has_frame(var_id);
    if (framed && !bindings_frame_index_find_coordinate(
            &bb->current, var_id, &framed_entry, &framed_slot)) {
        if (!bindings_builder_frame_index_ensure_id(bb, var_id) ||
            !bindings_frame_index_find_coordinate(
                &bb->current, var_id, &framed_entry, &framed_slot))
            return false;
    }
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

    BindingsFrameRef framed_ref = framed
        ? (BindingsFrameRef){.identity = framed_entry->epoch}
        : (BindingsFrameRef){0};
    BindingValue existing = binding_value_from_atom(NULL);
    if (framed) {
        /* Presentation may detach the directory; copy the value before that
         * boundary and keep only the stable reference afterward. RHS admission
         * below may also move sparse slots, so the write resolves them again. */
        existing = bindings_frame_slot_value(framed_entry, framed_slot);
        if (!bindings_frame_index_ensure_presentation_slot(
                &bb->current, framed_ref, framed_slot,
                var_id, spelling, name_key)) {
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
                if (failure) *failure = CETTA_MATCH_REFUTED;
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
        if (match_decoded_atoms_worklist_checked(
                existing, val, NULL, bb, NULL, false, failure)) {
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

static bool bindings_builder_add_id_internal_checked(
        BindingsBuilder *bb, VarId var_id,
        SymbolId spelling, Atom *name_key, BindingValue val,
        CettaMatchCompletion *failure) {
    return bindings_builder_add_id_internal_with_cycle_evidence(
        bb, var_id, spelling, name_key, val, false,
        BINDINGS_REACHABILITY_UNKNOWN, NULL, failure);
}

static bool bindings_builder_add_id_internal(
        BindingsBuilder *bb, VarId var_id,
        SymbolId spelling, Atom *name_key, BindingValue val) {
    return bindings_builder_add_id_internal_with_cycle_evidence(
        bb, var_id, spelling, name_key, val, false,
        BINDINGS_REACHABILITY_UNKNOWN, NULL, NULL);
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
        bb, var->var_id, atom_symbol_current_id(var), var->name_key, binding_value_from_atom(val));
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
    if (binding_value_kind(value) == BINDING_VALUE_CONTEXTUAL &&
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
            authoritative_out, NULL);
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
    CettaFrameIdentity epoch;
    BindingsFrameRef authority_ref;
    bool frame_admitted;
    BindingsExclusiveFrame *exclusive;
} RuleFrameRegion;

static bool rule_frame_region_admit_frame(
        RuleFrameRegion *view, BindingsBuilder *builder,
        Atom *source_var, CettaFrameIdentity epoch);
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
        BindingsBuilder *bb, Atom *source_var, CettaFrameIdentity epoch,
        BindingValue value, Arena *arena, AtomDeepCopySession **transport,
        RuleFrameRegion *frame_region,
        BindingValue *authoritative_value_out, BindingsReachability cycle_evidence) {
    if (authoritative_value_out)
        *authoritative_value_out = binding_value_from_atom(NULL);
    static _Thread_local int direct_enabled = -1;
    if (!bb || !source_var || source_var->kind != ATOM_VAR ||
        var_id_is_none(source_var->var_id) || cetta_frame_identity_is_none(epoch) || !value.skeleton) {
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
        atom_symbol_current_id(source_var), source_var->name_key, value,
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
            atom_symbol_current_id(source_var), source_var->name_key, value,
            cycle_evidence, authoritative_value_out);
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
            materialized->var_id, atom_symbol_current_id(materialized),
            materialized->name_key, value, cycle_evidence,
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
                                                     bool normalize_constraints,
                                                     CettaMatchCompletion *failure);

static bool bindings_builder_normalize_constraints(
    BindingsBuilder *bb, CettaMatchCompletion *failure) {
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
                bb, pending[i].lhs, pending[i].rhs, false, failure)) {
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
                                                     bool normalize_constraints,
                                                     CettaMatchCompletion *failure) {
    if (failure) *failure = CETTA_MATCH_INCOMPLETE;
    Bindings *current = &bb->current;
    if (!bindings_resolve_value_preview(current, lhs, &lhs) ||
        !bindings_resolve_value_preview(current, rhs, &rhs))
        return false;

    CettaMatchCompletion equality = CETTA_MATCH_INCOMPLETE;
    if (binding_values_eq_under_bindings_checked(
            current, lhs, rhs, failure ? &equality : NULL))
        return true;
    if (lhs.skeleton->kind == ATOM_VAR) {
        if (!bindings_builder_add_id_internal_checked(
                bb, binding_value_variable_id(lhs), atom_symbol_current_id(lhs.skeleton),
                lhs.skeleton->name_key, rhs, failure)) {
            return false;
        }
    } else if (rhs.skeleton->kind == ATOM_VAR) {
        if (!bindings_builder_add_id_internal_checked(
                bb, binding_value_variable_id(rhs), atom_symbol_current_id(rhs.skeleton),
                rhs.skeleton->name_key, lhs, failure)) {
            return false;
        }
    } else if (!binding_value_may_contain_unbound_var(current, lhs) &&
               !binding_value_may_contain_unbound_var(current, rhs)) {
        if (failure) *failure = equality;
        return false;
    } else if (!bindings_builder_store_constraint(bb, lhs, rhs)) {
        return false;
    }

    if (normalize_constraints && !bindings_builder_normalize_constraints(bb, failure))
        return false;
    return true;
}

typedef struct {
    BindingsBuilder *builder;
    bool ok;
    CettaMatchCompletion *failure;
} BindingsBuilderMergeCurrentContext;

static bool bindings_builder_merge_current_binding(
        const Binding *binding, void *raw_context) {
    BindingsBuilderMergeCurrentContext *context = raw_context;
    context->ok = bindings_builder_add_id_internal_checked(
        context->builder, binding->var_id,
        binding->spelling, binding->name_key,
        binding->value, context->failure);
    return context->ok;
}

static bool bindings_builder_try_merge_kernel(
    BindingsBuilder *bb, const Bindings *src, CettaMatchCompletion *failure) {
    if (failure) *failure = CETTA_MATCH_INCOMPLETE;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_MERGE);
    if (!bb || !src)
        return true;
    if (!bindings_has_bound_values(src) && src->eq_len == 0 &&
        !bindings_prime_present(src) && !bindings_has_frame_authority(src))
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
    if (!bindings_merge_prime_occurrence(&bb->current, src, failure)) {
        bindings_builder_rollback(bb, mark);
        return false;
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
        .failure = failure,
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
                bb, pending[i].lhs, pending[i].rhs, false, failure)) {
            bindings_temp_constraints_release(pending, pending_cap,
                                              pending_stack);
            bindings_builder_rollback(bb, mark);
            return false;
        }
    }
    bindings_temp_constraints_release(pending, pending_cap, pending_stack);
    if (!bindings_builder_normalize_constraints(bb, failure)) {
        bindings_builder_rollback(bb, mark);
        return false;
    }
    return true;
}

bool bindings_builder_try_merge(BindingsBuilder *bb, const Bindings *src) {
    return bindings_builder_try_merge_kernel(bb, src, NULL);
}

CettaMatchCompletion bindings_builder_try_merge_checked(
    BindingsBuilder *builder, const Bindings *source) {
    CettaMatchCompletion failure = CETTA_MATCH_INCOMPLETE;
    if (!builder) return failure;
    return bindings_builder_try_merge_kernel(builder, source, &failure)
        ? CETTA_MATCH_MATCHED : failure;
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
    bindings_builder_drop_prime_trail(bb,0u);
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
        var_id_is_none(id)) {
        return false;
    }
    size_t slot = bindings_var_id_hash(id) & (slot_cap - 1u);
    for (size_t probes = 0u; probes < slot_cap; probes++) {
        if (var_id_is_none(slots[slot]) || var_id_eq(slots[slot], id)) {
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
    if (!set || var_id_is_none(id))
        return false;
    if (set->slots) {
        size_t slot =
            bindings_var_id_hash(id) & (set->slot_cap - 1u);
        for (size_t probes = 0u;
             probes < set->slot_cap; probes++) {
            if (var_id_is_none(set->slots[slot]))
                return false;
            if (var_id_eq(set->slots[slot], id))
                return true;
            slot = (slot + 1u) & (set->slot_cap - 1u);
        }
        return false;
    }
    for (uint32_t i = 0; i < set->len; i++) {
        if (var_id_eq(set->items[i], id))
            return true;
    }
    return false;
}

static bool var_id_set_add(VarIdSet *set, VarId id) {
    if (!set || var_id_is_none(id))
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
        if (!var_id_is_none(single)) {
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

static bool collect_value_var_ids_roots(BindingValue value,
                                       Atom *const *roots, size_t count,
                                       VarIdSet *set) {
    if (!set || (count && !roots))
        return false;
    RenameWalkStack stack;
    FreshenEpochMemo states;
    rename_walk_stack_init(&stack);
    freshen_epoch_memo_init(&states);
    for (size_t index = count; index > 0u; index--) {
        if (!rename_walk_stack_push(
                &stack, (RenameWalkTask){RENAME_WALK_ENTER, roots[index - 1u]}))
            goto fail;
    }
    while (stack.len > 0u) {
        RenameWalkTask task = stack.tasks[--stack.len];
        Atom *atom = task.atom;
        if (!atom)
            goto fail;
        if (!atom_has_vars(atom))
            continue;
        /* A variable is a support leaf even when its spelling is structured
         * or it has not been hash-consed. The VarId set already merges repeated
         * leaves; only expressions need an active/complete graph memo. */
        if (atom->kind == ATOM_VAR) {
            if (!var_id_set_add(set, binding_value_qualify_id(value, atom->var_id)))
                goto fail;
            continue;
        }
        if (task.kind == RENAME_WALK_LEAVE) {
            if (!freshen_epoch_memo_store(
                    &states, atom, &g_rename_walk_complete))
                goto fail;
            continue;
        }
        if ((atom->flags & ATOM_FLAG_HASH_STABLE) != 0u) {
            VarId single = atom_single_variable_id(atom);
            if (!var_id_is_none(single)) {
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

static bool collect_value_var_ids(BindingValue value, VarIdSet *set) {
    Atom *root = value.skeleton;
    if (!root || !set)
        return false;
    if ((root->flags & ATOM_FLAG_HASH_STABLE) != 0u)
        return collect_value_var_ids_hash_stable(value, set);
    return collect_value_var_ids_roots(value, &root, 1u, set);
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
        if (!captured || (captured->support_count && !captured->support))
            return true;
        for (size_t i = 0u; i < captured->support_count; i++)
            if (!var_id_set_add(&collector->ids, captured->support[i]))
                return true;
    }
    uint64_t thunk_id = 0u;
    if (collector->need &&
        prime_need_ref_belongs_to(atom, collector->need, &thunk_id)) {
        if (var_id_set_contains(&collector->visited_thunks,
                                var_id_from_base(thunk_id)))
            return false;
        PrimeNeedCellView cell;
        if (!var_id_set_add(&collector->visited_thunks, var_id_from_base(thunk_id)) ||
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

static bool bindings_support_drain(BindingsSupportCollector *collector) {
    BindingApplyGraph visited = {0};
    bool ok = false;
    while (collector->len) {
        collector->value = collector->pending[--collector->len];
        Atom *atom = collector->value.skeleton;
        bool created = false;
        if (!bindings_apply_graph_enter(&visited, atom,
                (BindingsApplyScope){.epoch = binding_value_epoch(collector->value),
                                     .kind = binding_value_kind(collector->value)}, 0u, &created))
            goto done;
        if (!created) continue;
        if (bindings_support_collect_atom(atom, collector)) goto done;
        if (atom->kind == ATOM_EXPR) {
            for (CettaExprIndex i = atom->expr.len; i > 0u; --i) {
                BindingValue child = collector->value;
                child.skeleton = atom->expr.elems[i - 1u];
                if (!bindings_support_push(collector, child)) goto done;
            }
        }
    }
    ok = true;
done:
    free(visited.entries);
    return ok;
}

static bool bindings_captured_support_present(const Atom *atom, void *unused) {
    (void)unused;
    if (atom->kind != ATOM_GROUNDED || atom->ground.gkind != GV_BINDINGS)
        return false;
    const CettaBindingsValue *captured = atom->ground.ptr;
    return !captured || captured->support_count != 0u;
}

bool bindings_value_has_captured_support(const Atom *value) {
    return value && atom_has_identity_grounded(value) &&
        atom_graph_any(value, bindings_captured_support_present, NULL);
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
    if (!bindings_support_drain(&collector)) goto done;
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
        BindingValue *value, Arena *arena, CettaVarMap *inventory, CettaFrameIdentity epoch) {
    if (binding_value_is_contextual(*value) || !atom_has_vars(value->skeleton))
        return true;
    Atom *syntax = cetta_import_frame_syntax(arena, value->skeleton, inventory, epoch);
    if (!syntax)
        return false;
    *value = binding_value_from_atom(syntax);
    return true;
}

bool bindings_contextualize_unframed(
        Bindings *bindings, Arena *arena, CettaVarMap *inventory, CettaFrameIdentity epoch) {
    if (!bindings || !arena || !inventory || cetta_frame_identity_is_none(epoch))
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
        if (var_id_is_none(entry.var_id) ||
            !cetta_frame_identity_is_none(var_epoch_suffix(entry.var_id)))
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
        slots[slot] = var_id_from_base((uint64_t)slot + 1u);
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
        if (var_id_is_none(entry->var_id) ||
            !cetta_frame_identity_is_none(var_epoch_suffix(entry->var_id)))
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
    return var_id_cmp(a, b);
}

/* Reserve frame coordinates without binding them. One cycle-aware support
 * traversal visits shared subgraphs once across all exported roots. */
static bool bindings_prepare_var_batch(Bindings *bindings,
                                      Atom *const *variables,
                                      Atom *const *values, uint32_t count) {
    if ((size_t)count > SIZE_MAX / (2u * sizeof(Atom *)))
        return false;
    Atom **roots = cetta_malloc((size_t)count * 2u * sizeof(*roots));
    for (uint32_t i = 0u; i < count; i++) {
        roots[2u * (size_t)i] = variables[i];
        roots[2u * (size_t)i + 1u] = values[i];
    }
    VarIdSet ids;
    var_id_set_init(&ids);
    bool ok = collect_value_var_ids_roots(
        binding_value_from_atom(roots[0]), roots, (size_t)count * 2u, &ids);
    free(roots);
    if (!ok) {
        var_id_set_free(&ids);
        return false;
    }
    if (ids.len > 1u)
        qsort(ids.items, ids.len, sizeof(*ids.items), var_id_compare);
    for (uint32_t start = 0u; start < ids.len && ok;) {
        CettaFrameIdentity epoch = var_epoch_suffix(ids.items[start]);
        uint32_t end = start + 1u;
        while (end < ids.len && cetta_frame_identity_eq(var_epoch_suffix(ids.items[end]), epoch))
            end++;
        if (!cetta_frame_identity_is_none(epoch) && !variant_private_var_id(ids.items[start])) {
            const BindingsFrameIndexEntry *known =
                bindings_frame_index_find_frame_const(bindings->frame_index, epoch);
            if (known && known->schema->source_ids_contiguous &&
                var_id_eq(known->schema->source_first_id, var_id_from_base(1u))) {
                /* Growing a runtime slot array must retain its existing
                 * coordinates. Reserve its largest mentioned slot first. */
                ok = bindings_frame_index_ensure_id(bindings, ids.items[end - 1u]);
                for (uint32_t i = start; i < end && ok; i++)
                    ok = bindings_frame_index_ensure_id(bindings, ids.items[i]);
            } else {
                for (uint32_t i = start; i < end; i++)
                    ids.items[i] = var_source_id(ids.items[i]);
                ok = bindings_frame_index_register(
                    bindings, ids.items + start, end - start, epoch);
            }
        }
        start = end;
    }
    var_id_set_free(&ids);
    return ok;
}

bool bindings_add_vars(Bindings *bindings, Atom *const *variables,
                       Atom *const *values, uint32_t count) {
    if (!bindings || (count && (!variables || !values)))
        return false;
    for (uint32_t i = 0u; i < count; i++)
        if (!variables[i] || variables[i]->kind != ATOM_VAR || !values[i])
            return false;
    if (count == 0u)
        return true;
    Bindings candidate;
    if (!bindings_clone(&candidate, bindings))
        return false;
    if (!bindings_prepare_var_batch(&candidate, variables, values, count)) {
        bindings_free(&candidate);
        return false;
    }
    BindingsBuilder builder;
    bindings_builder_init_owned(&builder, &candidate);
    /* No intermediate state escapes and no checkpoint is observed. Use
     * exactly the sequential operation's checked insertion, including its
     * alias shortcuts, occurs audit and constraint normalization. The whole
     * owned image is discarded on failure (BindingPublication). */
    for (uint32_t i = 0u; i < count; i++) {
        Atom *var = variables[i];
        if (!bindings_add_inplace_internal(
                &builder.current, var->var_id, atom_symbol_current_id(var), var->name_key,
                binding_value_from_atom(values[i]), true)) {
            bindings_builder_free(&builder);
            return false;
        }
    }
    bindings_builder_take(&builder, &candidate);
    bindings_replace(bindings, &candidate);
    return true;
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
                var_source_id(variables->items[index]);
            if (var_id_is_none(variables->items[index])) {
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
            if (!var_id_eq(variables->items[read], variables->items[write - 1u]))
                variables->items[write++] = variables->items[read];
        }
        variables->len = write;
    }
    return collected;
}

bool bindings_register_activation_source_frame(
        Bindings *bindings, Atom *source, CettaFrameIdentity epoch) {
    if (!bindings || !source || cetta_frame_identity_is_none(epoch))
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
        BindingsBuilder *builder, Atom *source, CettaFrameIdentity epoch) {
    if (!builder || !source || cetta_frame_identity_is_none(epoch))
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

    if (!bindings || !value.skeleton || var_id_is_none(target))
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
        if (var_id_is_none(single))
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
        CettaFrameIdentity epoch, VarId target,
        const RuleFrameRegion *frame_region) {
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_CYCLE_PLAN_SUPPORT_ATTEMPT);
    if (!bindings_cycle_plan_support_enabled() || !bindings || !source ||
        cetta_frame_identity_is_none(epoch) || var_id_is_none(target) ||
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
        if (var_id_is_none(source_id)) {
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
        var_id_is_none(target))
        return BINDINGS_REACHABILITY_UNKNOWN;
    size_t single_remaining = bindings_dereference_limit(outer);
    size_t local_bound = (size_t)frame->schema->len + frame->write_len;
    single_remaining = local_bound > SIZE_MAX - single_remaining
        ? SIZE_MAX : single_remaining + local_bound;
    for (;;) {
        VarId single = binding_value_single_variable_id(value);
        if (var_id_is_none(single))
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
    if (binding_value_kind(*value) == BINDING_VALUE_CONTEXTUAL &&
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
        Bindings *bindings, Arena *arena, Atom *skeleton, CettaFrameIdentity epoch,
        BindingValueKind kind, uint32_t *remaining) {
    if (!atom_has_vars(skeleton))
        return skeleton;
    if ((*remaining)-- == 0u)
        return NULL;
    if (skeleton->kind == ATOM_VAR) {
        BindingValue root;
        if (!bindings_resolve_value_root(
                bindings, binding_value_from_context_kind(
                    skeleton, epoch, kind), &root, false) ||
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
        bindings, arena, value.skeleton, binding_value_epoch(value), binding_value_kind(value), &remaining);
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
    if (cetta_frame_identity_is_none(frame->epoch))
        return false;
    if (binding_value_is_contextual(value))
        return !cetta_frame_identity_eq(binding_value_epoch(value), frame->epoch);
    VarIdSet support;
    var_id_set_init(&support);
    bool avoids = collect_value_var_ids(value, &support);
    for (uint32_t index = 0u; avoids && index < support.len; index++)
        avoids = !cetta_frame_identity_eq(var_epoch_suffix(support.items[index]), frame->epoch);
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
    if (!frame || !frame->active || !builder || var_id_is_none(id) ||
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
        if (!binding_value_match_equal(existing, value))
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
        cetta_frame_identity_is_none(frame->epoch))
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
                true, BINDINGS_REACHABILITY_ABSENT, NULL, NULL)) {
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
        cetta_frame_identity_is_none(frame->epoch) ||
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
            if (!binding_value_match_equal(current, value)) {
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
        if (var_id_is_none(id))
            continue;
        size_t slot = bindings_var_id_hash(id) & (cap - 1u);
        while (!var_id_is_none(slots[slot]))
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
    if (!vars || var_id_is_none(id) || vars->slot_cap == 0u)
        return false;
    size_t slot =
        bindings_var_id_hash(id) & (vars->slot_cap - 1u);
    while (!var_id_is_none(vars->slots[slot])) {
        if (var_id_eq(vars->slots[slot], id))
            return true;
        slot = (slot + 1u) & (vars->slot_cap - 1u);
    }
    return false;
}

static bool bindings_reachable_vars_add(
    BindingsReachableVars *vars, VarId id) {
    if (!vars || var_id_is_none(id))
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
    while (!var_id_is_none(vars->slots[slot]))
        slot = (slot + 1u) & (vars->slot_cap - 1u);
    vars->slots[slot] = id;
    vars->slot_len++;
    vars->work[vars->work_len++] = id;
    return true;
}

static bool bindings_reachable_vars_add_value(
    BindingsReachableVars *vars, BindingValue value) {
    Atom *atom = value.skeleton;
    /* Opaque binding values have identity-bearing ports even when their
     * enclosing syntax is ground. Reuse the closure-support traversal; the
     * ordinary-variable path and occurs-check semantics stay separate. */
    if (bindings_value_has_captured_support(atom)) {
        BindingsSupportCollector collector = {0};
        var_id_set_init(&collector.ids);
        var_id_set_init(&collector.visited_thunks);
        bool ok = bindings_support_push(&collector, value) &&
            bindings_support_drain(&collector);
        for (uint32_t i = 0u; ok && i < collector.ids.len; i++)
            ok = bindings_reachable_vars_add(vars, collector.ids.items[i]);
        free(collector.pending);
        var_id_set_free(&collector.visited_thunks);
        var_id_set_free(&collector.ids);
        return ok;
    }
    if (!atom || !atom_has_vars(atom))
        return true;
    VarId single = binding_value_single_variable_id(value);
    if (atom->kind == ATOM_VAR ||
        (!var_id_is_none(single) && (atom->flags & ATOM_FLAG_HASH_STABLE)))
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
    if (!vars || !root || !root->atom || cetta_frame_identity_is_none(root->epoch))
        return false;
    if (bindings_value_has_captured_support(root->atom))
        return bindings_reachable_vars_add_value(
            vars, binding_value_from_context(root->atom, root->epoch));
    if (!atom_has_vars(root->atom))
        return true;
    if (root->variable_support) {
        CettaTermVariableSupportIterator iterator;
        cetta_term_variable_support_iterator_init(
            &iterator, root->variable_support);
        uint64_t base_id = 0u;
        while (cetta_term_variable_support_iterator_next(
                   &iterator, &base_id)) {
            if (!bindings_reachable_vars_add(
                    vars, var_epoch_id(var_id_from_base(base_id),
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
        if (var_id_is_none(id)) {
            free(slots);
            return false;
        }
        size_t slot = bindings_var_id_hash(id) & (cap - 1u);
        while (!var_id_is_none(slots[slot].id) &&
               !var_id_eq(slots[slot].id, id)) {
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
    if (!slots || cap == 0u || var_id_is_none(id))
        return 0u;
    size_t slot = bindings_var_id_hash(id) & (cap - 1u);
    while (!var_id_is_none(slots[slot].id)) {
        if (var_id_eq(slots[slot].id, id))
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
    /* The directory clock is an allocation frontier, not the maximum version
     * of a selected bound slot. Dropping an old write cannot rewind it. */
    if (src->frame_index && dst->frame_index) {
        if (!bindings_frame_index_detach(dst))
            return false;
        if (dst->frame_index->write_clock < src->frame_index->write_clock)
            dst->frame_index->write_clock = src->frame_index->write_clock;
    }
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
                source, var_source_id(id), &source_slot)) {
            return false;
        }
        VarId source_id = var_source_id(id);
        if (!bindings_frame_index_ensure_presentation(
                dst, id, bindings_frame_slot_spelling(source, source_slot),
                bindings_frame_slot_name_key(source, source_slot)) ||
            !bindings_frame_index_detach(dst)) {
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
        target->activation_write_boundary = source->activation_write_boundary;
        target->has_activation_write_boundary = source->has_activation_write_boundary;
        /* A slot past the backing prefix reads as unbound with version zero;
         * only a different version needs that prefix to grow. */
        uint64_t source_version = bindings_frame_slot_version(source, source_slot);
        if (!bindings_frame_index_replace_slot_version(
                target, target_slot, source_version))
            return false;
        BindingValue value = bindings_frame_slot_value(source, source_slot);
        if (!value.skeleton)
            continue;
        if (!bindings_frame_index_prepare_value_context(dst, NULL, &value, false))
            return false;
        /* Preparing a contextual payload can grow or detach the directory. */
        target = bindings_frame_index_find_frame(dst->frame_index, var_epoch_suffix(id));
        if (!target || !bindings_frame_index_find_slot(target, source_id, &target_slot) ||
            !bindings_frame_index_entry_detach(target))
            return false;
        if (!bindings_frame_index_replace_slot_value(
                dst->frame_index, target, target_slot, value)) {
            return false;
        }
        bindings_rhs_variable_bloom_add(dst, value);
        target->write_version[target_slot] = source_version;
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
    /* The live frames first, with their schemas and slot counts: a value
     * admitted below may name a slot a frame made past its schema, which
     * the frame already owns, rather than register a frame of that slot
     * alone, which the frame's own schema could not join. */
    if (!bindings_projection_register_live_frames(src, dst, &live))
        goto fail;
    for (size_t i = 0u; i < selected_len; i++) {
        const Binding *binding =
            bindings_entry_at(src, selected[i]);
        if (!bindings_projection_append_entry(
                src, dst, binding)) {
            goto fail;
        }
    }
    if (!bindings_projection_copy_live_frame_values(src, dst, &live))
        goto fail;
    dst->cycle_state =
        src->cycle_state == BINDINGS_CYCLE_ACYCLIC
            ? BINDINGS_CYCLE_ACYCLIC
            : BINDINGS_CYCLE_UNKNOWN;
    if (src->owners)
        bindings_inherit_owners(dst, src);
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

static bool bindings_project_reachable_selected_history(
    const Bindings *src, Atom *const *roots, size_t root_count,
    const BindingsEpochRoot *epoch_roots,
    size_t epoch_root_count,
    const BindingsFrameUndoEntry *frame_history, uint32_t history_count,
    uint32_t rollback_floor,
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
        /* A live checkpoint can restore an older contextual RHS even when
         * the current slot is ground. Its context is a retained dependency
         * of that coordinate, and may itself expose further history. */
        for (uint32_t i = 0u; i < history_count; i++) {
            const BindingsFrameUndoEntry *undo = &frame_history[i];
            VarId id = var_epoch_id(var_id_from_base(undo->source_id),
                                   undo->frame_ref.identity);
            if (undo->written_write_version != 0u &&
                undo->trail_mark >= rollback_floor &&
                bindings_reachable_vars_contains(&live, id) &&
                !bindings_reachable_vars_add_value(&live, undo->previous_value)) {
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
    /* The live frames first, as in the sparse projection. */
    if (!bindings_projection_register_live_frames(src, dst, &live))
        goto fail_dst;
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
    if (!bindings_projection_copy_live_frame_values(src, dst, &live))
        goto fail_dst;
    if (src->owners)
        bindings_inherit_owners(dst, src);

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

static bool bindings_project_reachable_selected(
    const Bindings *src, Atom *const *roots, size_t root_count,
    const BindingsEpochRoot *epoch_roots, size_t epoch_root_count,
    Bindings *dst, bool **keep_entries_out, bool **keep_constraints_out) {
    return bindings_project_reachable_selected_history(
        src, roots, root_count, epoch_roots, epoch_root_count,
        NULL, 0u, 0u, dst, keep_entries_out, keep_constraints_out);
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
        bool projected = bindings_project_reachable_selected(
            src, roots, root_count,
            epoch_roots, epoch_root_count, dst,
            NULL, NULL);
        if (projected && src)
            bindings_prime_assign(dst, src);
        return projected;
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
    if (src)
        bindings_prime_assign(&projected, src);
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

/* The same closure and frame projection without an execution occurrence.
 * Callers installing a new context do not copy the old one first. */
bool bindings_project_reachable_logical(
    const Bindings *src, Atom *const *roots, size_t root_count,
    Bindings *dst) {
    return bindings_project_reachable_selected(
        src, roots, root_count, NULL, 0u, dst, NULL, NULL);
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

    uint32_t rollback_floor = old_trail_len;
    for (size_t i = 0u; i < checkpoint_count; i++) {
        if (checkpoint_marks[i] < rollback_floor)
            rollback_floor = checkpoint_marks[i];
    }
    Bindings projected;
    bool *keep_entries = NULL;
    bool *keep_constraints = NULL;
    if (!bindings_project_reachable_selected_history(
            &bb->current, roots, root_count,
            epoch_roots, epoch_root_count,
            bb->frame_undo, checkpoint_count ? bb->frame_undo_len : 0u,
            rollback_floor, &projected,
            &keep_entries, &keep_constraints)) {
        return false;
    }
    bindings_prime_assign(&projected, &bb->current);

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
            for (uint32_t i=0u;i<next_prime_len;i++) prime_occurrence_clear(&next_prime_trail[i]);
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
            prime_occurrence_copy(&next_prime_trail[next_prime_len++],prime);
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
                var_id_from_base(undo.source_id),
                undo.frame_ref.identity);
            const BindingsFrameIndexEntry *projected_frame = NULL;
            uint32_t projected_slot = 0u;
            if (undo.written_write_version == 0u ||
                !bindings_frame_index_find_coordinate(
                    &projected, id,
                    &projected_frame, &projected_slot) ||
                !bindings_frame_slot_value(projected_frame, projected_slot).skeleton) {
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
        for (uint32_t i=0u;i<next_prime_len;i++) prime_occurrence_clear(&next_prime_trail[i]);
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
    bindings_builder_drop_prime_trail(bb,0u);
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
        if (var_id_eq(map->items[i].id, id))
            return map->items[i].mapped;
    }
    return NULL;
}

static Atom *rename_var_map_add_fresh(RenameVarMap *map, Arena *a, Atom *var) {
    CETTA_FRAME_IDENTITY_SCOPE(frame_identity_scope);
    CettaFrameIdentity suffix = cetta_frame_identity_scope_fresh(&frame_identity_scope);
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

typedef struct {
    const VarIdSet *listed;
    RenameVarMap *map;
    bool rename_listed;
} ListedVariableRename;

static Atom *rename_listed_variable(Arena *a, Atom *atom, void *context) {
    ListedVariableRename *policy = context;
    if (var_id_set_contains(policy->listed, atom->var_id) != policy->rename_listed)
        return atom;
    Atom *renamed = rename_var_map_lookup(policy->map, atom->var_id);
    return renamed ? renamed : rename_var_map_add_fresh(policy->map, a, atom);
}

static Atom *rename_vars_listed_iterative(Arena *a, Atom *root,
                                          const VarIdSet *listed,
                                          RenameVarMap *map,
                                          bool rename_listed) {
    ListedVariableRename policy = {listed, map, rename_listed};
    return rename_vars_walk(a, root, rename_listed_variable, &policy, false);
}

Atom *rename_vars(Arena *a, Atom *atom, CettaFrameIdentity suffix) {
    return rename_vars_walk(a, atom, rename_epoch_variable, &suffix, true);
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

/* ── Lists ──────────────────────────────────────────────────────────────── */

/* A list [x1, ..., xn] is (LIST x1 ... xn) and a list pattern [x1, ..., xk |
 * r] is (LIST_REST x1 ... xk r); see atom.h.  A list never meets an
 * expression.  Two lists meet element by element.  A list pattern meets a
 * list or another list pattern by its k leading elements, and its rest meets
 * what the other has left: the rest of a list is itself a list, built in the
 * matcher's arena, and the rest of an exhausted list is []. */
typedef enum {
    MATCH_LIST_EXPRESSIONS, /* neither is a list: match them as expressions */
    MATCH_LIST_MISMATCH,
    MATCH_LIST_INCOMPLETE, /* a required derived remainder was not built */
    MATCH_LIST_ELEMENTS,
} MatchListOutcome;

typedef struct {
    /* Elements 1..prefix of the two sides meet pairwise. */
    CettaExprLen prefix;
    /* Then left_rest meets right_rest, when they are not NULL.  A derived
     * side was built here from that side's elements, so it carries that
     * side's context but no plan and no owner of its own. */
    Atom *left_rest;
    Atom *right_rest;
    bool left_derived;
    bool right_derived;
} MatchListMeeting;

/* An expression that is a list or a list pattern.  Most expressions carry
 * no internal tag at all, which one fact bit decides. */
static inline bool match_expr_is_list(const Atom *e) {
    if ((e->structural_facts &
         (ATOM_STRUCTURAL_FACTS_VALID | ATOM_STRUCTURAL_HAS_INTERNAL_TAG)) ==
        ATOM_STRUCTURAL_FACTS_VALID)
        return false;
    return e->expr.len > 0u && atom_is_list_tag(e->expr.elems[0]);
}

/* A braces node {x ...}: it meets only a braces node, so no variable is
 * ever bound to its tag. */
static inline bool match_expr_is_braces(const Atom *e) {
    if ((e->structural_facts &
         (ATOM_STRUCTURAL_FACTS_VALID | ATOM_STRUCTURAL_HAS_INTERNAL_TAG)) ==
        ATOM_STRUCTURAL_FACTS_VALID)
        return false;
    return atom_is_prime_braces(e);
}

/* Elements from..from+count-1 of a list or list pattern, closed by rest. */
static Atom *match_list_remainder_unowned(const Atom *list,
                                          CettaExprIndex from,
                                          CettaExprLen count, Atom *rest);

static Atom *match_list_remainder(Arena *a, const Atom *list,
                                  CettaExprIndex from, CettaExprLen count,
                                  Atom *rest) {
    if (!a)
        return match_list_remainder_unowned(list, from, count, rest);
    Atom *const *elems = list->expr.elems + from;
    return rest ? atom_list_with_rest(a, elems, count, rest)
                : atom_list(a, elems, count);
}

/* The binding paths that unify an already-bound variable's two values carry
 * no arena; a rest derived there is kept for the process.  Every matcher
 * entry point takes its caller's arena and never comes here. */
static pthread_mutex_t g_match_list_unowned_lock = PTHREAD_MUTEX_INITIALIZER;
static Arena g_match_list_unowned_arena;
static bool g_match_list_unowned_ready;

static Atom *match_list_remainder_unowned(const Atom *list,
                                          CettaExprIndex from,
                                          CettaExprLen count, Atom *rest) {
    pthread_mutex_lock(&g_match_list_unowned_lock);
    if (!g_match_list_unowned_ready) {
        arena_init(&g_match_list_unowned_arena);
        g_match_list_unowned_ready = true;
    }
    Atom *const *elems = list->expr.elems + from;
    Atom *result = rest
        ? atom_list_with_rest(&g_match_list_unowned_arena, elems, count, rest)
        : atom_list(&g_match_list_unowned_arena, elems, count);
    pthread_mutex_unlock(&g_match_list_unowned_lock);
    return result;
}

static MatchListOutcome match_list_meet(Arena *a, Atom *left, Atom *right,
                                        MatchListMeeting *m) {
    if (match_expr_is_braces(left) != match_expr_is_braces(right))
        return MATCH_LIST_MISMATCH;
    bool left_list = match_expr_is_list(left);
    bool right_list = match_expr_is_list(right);
    if (!left_list && !right_list)
        return MATCH_LIST_EXPRESSIONS;
    if (!left_list || !right_list)
        return MATCH_LIST_MISMATCH;
    bool left_open = atom_is_list_rest(left);
    bool right_open = atom_is_list_rest(right);
    Atom *left_rest = left_open ? left->expr.elems[left->expr.len - 1u] : NULL;
    Atom *right_rest = right_open ? right->expr.elems[right->expr.len - 1u] : NULL;
    CettaExprLen p = left->expr.len - 1u - (left_open ? 1u : 0u);
    CettaExprLen q = right->expr.len - 1u - (right_open ? 1u : 0u);
    *m = (MatchListMeeting){.prefix = p < q ? p : q};
    if (p == q) {
        if (left_open || right_open) {
            m->left_rest = left_open ? left_rest : atom_list(a, NULL, 0u);
            m->right_rest = right_open ? right_rest : atom_list(a, NULL, 0u);
            m->left_derived = !left_open;
            m->right_derived = !right_open;
            if (!m->left_rest || !m->right_rest)
                return MATCH_LIST_INCOMPLETE;
        }
        return MATCH_LIST_ELEMENTS;
    }
    if (p < q) {
        if (!left_open)
            return MATCH_LIST_MISMATCH;
        m->left_rest = left_rest;
        m->right_rest = match_list_remainder(a, right, 1u + p, q - p, right_rest);
        m->right_derived = true;
    } else {
        if (!right_open)
            return MATCH_LIST_MISMATCH;
        m->left_rest = match_list_remainder(a, left, 1u + q, p - q, left_rest);
        m->right_rest = right_rest;
        m->left_derived = true;
    }
    return m->left_rest && m->right_rest ? MATCH_LIST_ELEMENTS
                                         : MATCH_LIST_INCOMPLETE;
}

/* ── One-way pattern matching ───────────────────────────────────────────── */

typedef struct {
    const CettaFrameIdentity *left_epochs, *right_epochs;
    size_t left_count, right_count;
} AlphaFrameScope;

static bool alpha_frame_local(VarId id, const CettaFrameIdentity *epochs,
                              size_t count) {
    CettaFrameIdentity epoch = var_epoch_suffix(id);
    if (cetta_frame_identity_is_none(epoch)) return false;
    for (size_t i = 0u; i < count; i++)
        if (cetta_frame_identity_eq(epochs[i], epoch)) return true;
    return false;
}

static CettaMatchCompletion atom_pair_walk(Atom *left, Atom *right, Bindings *bindings,
                            BindingsBuilder *builder, Arena *arena, bool alpha,
                            const AlphaFrameScope *scope, bool resolve_spaces);

bool simple_match(Atom *pattern, Atom *target, Bindings *b, Arena *a) {
    return atom_pair_walk(pattern, target, b, NULL, a, false, NULL, false) == CETTA_MATCH_MATCHED;
}

bool simple_match_builder(Atom *pattern, Atom *target, BindingsBuilder *bb,
                          Arena *a) {
    return atom_pair_walk(pattern, target, NULL, bb, a, false, NULL, false) == CETTA_MATCH_MATCHED;
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
           var_id_eq(binding_value_variable_id(entry->value), entry->var_id);
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
                               CettaFrameIdentity epoch, uint32_t first_entry,
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
        binding_value_kind(left.value) != binding_value_kind(right.value))
        return false;
    return binding_value_kind(left.value) == BINDING_VALUE_MATERIALIZED ||
        (cetta_frame_identity_eq(binding_value_epoch(left.value), binding_value_epoch(right.value)) &&
         left.first_entry == right.first_entry);
}

static uintptr_t match_term_path_hash(MatchTerm term) {
    uintptr_t hash = (uintptr_t)term.value.skeleton >> 4;
    if (binding_value_is_contextual(term.value)) {
        hash ^= (uintptr_t)binding_value_epoch(term.value).word *
                (uintptr_t)0x85ebca6bu;
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
    MatchPathEntry *next = malloc(
        sizeof(*next) * new_cap);
    if (!next)
        return false;
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
    uint32_t *next = malloc(sizeof(*next) * new_cap);
    if (!next)
        return false;
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
        if (var_id_eq(pairs->items[i].left, left))
            return pairs->items[i].right;
    }
    return VAR_ID_NONE;
}

static VarId alpha_lookup_right(const AlphaPairSet *pairs, VarId right) {
    for (uint32_t i = 0; i < pairs->len; i++) {
        if (var_id_eq(pairs->items[i].right, right))
            return pairs->items[i].left;
    }
    return VAR_ID_NONE;
}

static bool alpha_add_pair(AlphaPairSet *pairs, VarId left, VarId right) {
    if (pairs->len >= pairs->cap) {
        if (pairs->cap > UINT32_MAX / 2u)
            return false;
        uint32_t next_cap = pairs->cap ? pairs->cap * 2u : 8u;
        if (!cetta_expr_len_mul_fits_size(next_cap, sizeof(*pairs->items)))
            return false;
        AlphaPair *next = realloc(pairs->items, sizeof(*next) * next_cap);
        if (!next)
            return false;
        pairs->items = next;
        pairs->cap = next_cap;
    }
    pairs->items[pairs->len].left = left;
    pairs->items[pairs->len].right = right;
    pairs->len++;
    return true;
}

static bool simple_match_leaf(Atom *pattern, Atom *target) {
    switch (pattern->kind) {
    case ATOM_SYMBOL:
        return atom_symbols_equal(pattern, target);

    case ATOM_GROUNDED:
        if (pattern->ground.gkind != target->ground.gkind)
            return cetta_he_promoted_numbers_equal(pattern, target);
        switch (pattern->ground.gkind) {
        case GV_INT:    return pattern->ground.ival == target->ground.ival;
        case GV_FLOAT:  return pattern->ground.fval == target->ground.fval;
        case GV_BOOL:   return pattern->ground.bval == target->ground.bval;
        case GV_STRING: return atom_string_equal(pattern, target);
        case GV_BIGINT:
            return cetta_bigint_compare_cstr(atom_bigint_cstr(pattern),
                                            atom_bigint_cstr(target)) == 0;
        case GV_RATIONAL:
            return cetta_rational_compare_cstr(atom_rational_cstr(pattern),
                                               atom_rational_cstr(target)) == 0;
        case GV_TENSOR:
            return atom_eq(pattern, target);
        case GV_SPACE:
            return atom_eq_with_resolved_spaces(pattern, target);
        case GV_STATE:
        case GV_CAPTURE:
        case GV_FOREIGN:
        case GV_SEAL:
            return pattern->ground.ptr == target->ground.ptr;
        case GV_STRUCTURAL:
            return atom_structural_equal(pattern, target);
        /* A rational term is equal to any node with its unfolding. */
        case GV_TERM_GRAPH:
        case GV_BINDINGS:
            return atom_eq_with_resolved_spaces(pattern, target);
        case GV_PRIME_NEED_CAPABILITY:
        case GV_PRIME_CONTEXT:
        case GV_INTERNAL_TAG:
            return atom_eq_with_resolved_spaces(pattern, target);
        }
        return false;

    case ATOM_EXPR:
    case ATOM_VAR:
        return false;
    }
    return false;
}

/* A comparison frame retains only its next child and optional list rest.
 * Traversal is ordered: a failure leaves the same earlier bindings in place.
 * Active pairs detect malformed raw cycles; completed siblings may be visited
 * again because control representation does not license answer sharing. */
typedef struct {
    Atom *left;
    Atom *right;
    CettaExprIndex next;
    CettaExprIndex end;
    Atom *left_rest;
    Atom *right_rest;
} AtomPairFrame;

static CettaMatchCompletion atom_pair_walk(Atom *left, Atom *right, Bindings *bindings,
                            BindingsBuilder *builder, Arena *arena, bool alpha,
                            const AlphaFrameScope *scope, bool resolve_spaces) {
    AtomPairFrame inline_frames[16];
    AtomPairFrame *frames = inline_frames;
    size_t len = 0u, cap = sizeof inline_frames / sizeof inline_frames[0];
    MatchPathSet path;
    match_path_init(&path);
    AlphaPairSet pairs;
    alpha_pair_set_init(&pairs);
    CettaMatchCompletion completion = CETTA_MATCH_INCOMPLETE;
    if (!alpha && !bindings && !builder)
        goto done;
    for (;;) {
        if (!left || !right) {
            if (alpha && left == right)
                goto advance_pair;
            goto done;
        }
        if (alpha && (left->kind == ATOM_VAR || right->kind == ATOM_VAR)) {
            if (left->kind != ATOM_VAR || right->kind != ATOM_VAR)
                goto mismatch;
            if (scope) {
                bool local_left = alpha_frame_local(left->var_id, scope->left_epochs, scope->left_count);
                bool local_right = alpha_frame_local(right->var_id, scope->right_epochs, scope->right_count);
                if (local_left != local_right)
                    goto mismatch;
                if (!local_left) {
                    if (!var_id_eq(left->var_id, right->var_id))
                        goto mismatch;
                    goto advance_pair;
                }
            }
            VarId mapped_right = alpha_lookup_left(&pairs, left->var_id);
            VarId mapped_left = alpha_lookup_right(&pairs, right->var_id);
            if (!var_id_is_none(mapped_right) || !var_id_is_none(mapped_left)) {
                if (!var_id_eq(mapped_right, right->var_id) || !var_id_eq(mapped_left, left->var_id))
                    goto mismatch;
            } else if (!alpha_add_pair(&pairs, left->var_id, right->var_id)) {
                goto done;
            }
        } else if (!alpha && left->kind == ATOM_VAR) {
            if (!(builder ? bindings_builder_add_var_fresh(builder, left, right)
                          : bindings_add_var(bindings, left, right)))
                goto done;
        } else {
            if (left->kind != right->kind)
                goto mismatch;
            if (left->kind != ATOM_EXPR) {
                if (!(alpha ? (resolve_spaces ? atom_eq_with_resolved_spaces(left, right)
                                             : atom_eq(left, right))
                            : simple_match_leaf(left, right)))
                    goto mismatch;
            } else {
                AtomPairFrame frame = {.left = left, .right = right};
                if (alpha) {
                    if (atom_authored_len(left) != atom_authored_len(right))
                        goto mismatch;
                    frame.end = atom_authored_len(left);
                } else {
                    if (!atom_petta_decomposition_compatible(left, right))
                        goto mismatch;
                    MatchListMeeting meet;
                    MatchListOutcome outcome = match_list_meet(arena, left, right, &meet);
                    if (outcome == MATCH_LIST_MISMATCH)
                        goto mismatch;
                    if (outcome == MATCH_LIST_INCOMPLETE)
                        goto done;
                    if (outcome == MATCH_LIST_ELEMENTS) {
                        frame.next = 1u;
                        frame.end = 1u + meet.prefix;
                        frame.left_rest = meet.left_rest;
                        frame.right_rest = meet.right_rest;
                    } else {
                        if (atom_authored_len(left) != atom_authored_len(right))
                            goto mismatch;
                        frame.end = atom_authored_len(left);
                    }
                }
                if (frame.next < frame.end || frame.left_rest) {
                    MatchTerm l = {.value = binding_value_from_atom(left)};
                    MatchTerm r = {.value = binding_value_from_atom(right)};
                    if (!match_path_enter(&path, l, r))
                        goto done;
                    if (len == cap) {
                        if (cap > SIZE_MAX / 2u || cap * 2u > SIZE_MAX / sizeof(*frames))
                            goto done;
                        size_t next_cap = cap * 2u;
                        AtomPairFrame *next = frames == inline_frames
                            ? malloc(sizeof(*next) * next_cap)
                            : realloc(frames, sizeof(*next) * next_cap);
                        if (!next)
                            goto done;
                        if (frames == inline_frames)
                            memcpy(next, inline_frames, sizeof(*next) * len);
                        frames = next;
                        cap = next_cap;
                    }
                    frames[len++] = frame;
                }
            }
        }
advance_pair:
        while (len) {
            AtomPairFrame *frame = &frames[len - 1u];
            if (frame->next < frame->end) {
                CettaExprIndex child = frame->next++;
                left = frame->left->expr.elems[child];
                right = frame->right->expr.elems[child];
                break;
            }
            if (frame->left_rest) {
                left = frame->left_rest;
                right = frame->right_rest;
                frame->left_rest = NULL;
                frame->right_rest = NULL;
                break;
            }
            match_path_leave(&path,
                (MatchTerm){.value = binding_value_from_atom(frame->left)},
                (MatchTerm){.value = binding_value_from_atom(frame->right)});
            len--;
        }
        if (!len) {
            completion = CETTA_MATCH_MATCHED;
            break;
        }
    }
    goto done;
mismatch:
    completion = CETTA_MATCH_REFUTED;
done:
    alpha_pair_set_free(&pairs);
    match_path_free(&path);
    if (frames != inline_frames)
        free(frames);
    return completion;
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

static inline __attribute__((always_inline)) bool binding_values_eq_under_bindings_mode(
    Bindings *b, BindingValue lhs, BindingValue rhs, CettaMatchCompletion *failure,
    bool resolve_spaces) {
    if (failure) *failure = CETTA_MATCH_INCOMPLETE;
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
            goto mismatch;
        if (left->kind == ATOM_VAR) {
            if (!var_id_eq(binding_value_variable_id(lhs), binding_value_variable_id(rhs)))
                goto mismatch;
            continue;
        }
        if (left->kind != ATOM_EXPR) {
            if (!(resolve_spaces ? atom_eq_with_resolved_spaces(left, right)
                                 : atom_eq(left, right)))
                goto mismatch;
            continue;
        }
        if (left == right &&
            match_shared_occurrence_same_environment(
                binding_value_is_contextual(lhs),
                binding_value_is_contextual(rhs), binding_value_epoch(lhs), binding_value_epoch(rhs)))
            continue;
        if (!atom_has_vars(left) && !atom_has_vars(right)) {
            if (!(resolve_spaces ? atom_eq_with_resolved_spaces(left, right)
                                 : atom_eq(left, right)))
                goto mismatch;
            continue;
        }
        if (atom_authored_len(left) != atom_authored_len(right))
            goto mismatch;
        MatchTerm left_term = {.value = lhs}, right_term = {.value = rhs};
        if (!match_path_enter(&path, left_term, right_term) ||
            !decoded_match_push_exit(&work, left_term, right_term))
            goto done;
        for (CettaExprIndex i = atom_authored_len(left); i > 0u; i--) {
            BindingValue left_child = lhs, right_child = rhs;
            left_child.skeleton = left->expr.elems[i - 1u];
            right_child.skeleton = right->expr.elems[i - 1u];
            if (!decoded_match_push(&work, (MatchTerm){.value = left_child},
                                    (MatchTerm){.value = right_child}))
                goto done;
        }
    }
    equal = true;
    goto done;
mismatch:
    if (failure) *failure = CETTA_MATCH_REFUTED;
done:
    decoded_match_worklist_free(&work);
    match_path_free(&path);
    return equal;
}

static bool binding_values_eq_under_bindings(Bindings *b, BindingValue lhs, BindingValue rhs) {
    return binding_values_eq_under_bindings_mode(b, lhs, rhs, NULL, false);
}

static bool binding_values_eq_under_bindings_checked(
    Bindings *bindings, BindingValue lhs, BindingValue rhs, CettaMatchCompletion *failure) {
    return binding_values_eq_under_bindings_mode(bindings, lhs, rhs, failure, true);
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

/* One more step along a variable-only chain.  The bound depends only on the
 * store in hand, which a pair does not change before its chain ends, so it
 * is computed when a chain first reaches the cached value (0 at the start
 * of each pair) rather than for every pair. */
static inline bool bindings_dereference_step(
    size_t *dereferences, size_t *limit, const Bindings *current,
    size_t allowance) {
    if (++*dereferences <= *limit)
        return true;
    size_t bound = bindings_dereference_limit(current);
    *limit = bound <= SIZE_MAX - allowance ? bound + allowance : SIZE_MAX;
    return *dereferences <= *limit;
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
/* Select numerical observation at the synchronous match entry. Both
 * compiled workers use this walk; the plain body omits direct cost-scope
 * tests and event calls. Observed walks retain attempted-node counts even
 * when matching fails. */
static inline __attribute__((always_inline)) bool match_decoded_atoms_worklist_mode(
    BindingValue left_value, BindingValue right_value, Bindings *bindings,
    BindingsBuilder *builder, Arena *a, bool undefined_is_wildcard,
    bool meter_cost, CettaMatchCompletion *failure) {
    if (failure) *failure = CETTA_MATCH_INCOMPLETE;
    if (meter_cost)
        cetta_native_cost_charge_observed(CETTA_COST_MATCH_ATTEMPT, 1u);
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
    if (!left_value.skeleton || !right_value.skeleton) goto fail;
    if (!decoded_match_push(&work, (MatchTerm){.value = left_value},
                            (MatchTerm){.value = right_value})) goto fail;

    while (work.len > 0) {
        if (meter_cost)
            cetta_native_cost_charge_observed(CETTA_COST_MATCH_NODE, 1u);
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
        size_t dereference_limit = 0u;

retry_pair:
        left = left_value.skeleton;
        right = right_value.skeleton;
        if (undefined_is_wildcard &&
            (atom_is_symbol_id(left, g_builtin_syms.undefined_type) ||
             atom_is_symbol_id(right, g_builtin_syms.undefined_type)))
            continue;
        if (left == right) {
            if (left && left->kind == ATOM_VAR &&
                var_id_eq(binding_value_variable_id(left_value), binding_value_variable_id(right_value)) &&
                (!current || (current->cycle_state == BINDINGS_CYCLE_ACYCLIC)))
                continue;
            if (match_shared_ground_reflexivity_try(
                    left, right, true,
                    match_shared_occurrence_same_environment(
                        binding_value_is_contextual(left_value),
                        binding_value_is_contextual(right_value),
                        binding_value_epoch(left_value), binding_value_epoch(right_value)), current))
                continue;
        }
        bool closed_equal = false;
        if (!undefined_is_wildcard &&
            match_closed_expression_decision_try(
                left, right, &closed_equal)) {
            if (!closed_equal)
                goto mismatch;
            continue;
        }
        if (!undefined_is_wildcard &&
            match_finite_ground_equal_try(left, right, &closed_equal)) {
            if (!closed_equal)
                goto mismatch;
            continue;
        }
        if (left->kind == ATOM_VAR) {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_MATCH_DECODED_LOOKUP);
            BindingValue existing = bindings_lookup_value(
                current, left_value);
            if (existing.skeleton) {
                if (!bindings_dereference_step(
                        &dereferences, &dereference_limit, current, 0u)) {
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
                    if (!bindings_dereference_step(
                            &dereferences, &dereference_limit, current, 0u)) {
                        goto fail;
                    }
                    right_value = right_existing;
                    goto retry_pair;
                }
                if (var_id_eq(binding_value_variable_id(left_value), binding_value_variable_id(right_value))) continue;
            }
            bool added = builder
                ? bindings_builder_add_id_internal_checked(
                      builder, binding_value_variable_id(left_value),
                      atom_symbol_current_id(left), left->name_key, right_value, failure)
                : bindings_add_internal(bindings, binding_value_variable_id(left_value),
                      atom_symbol_current_id(left), left->name_key, right_value, true);
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
                if (!bindings_dereference_step(
                        &dereferences, &dereference_limit, current, 0u)) {
                    goto fail;
                }
                attempt_resolving_bound = true;
                right_value = existing;
                goto retry_pair;
            }
            bool added = builder
                ? bindings_builder_add_id_internal_checked(
                      builder, binding_value_variable_id(right_value),
                      atom_symbol_current_id(right), right->name_key, left_value, failure)
                : bindings_add_internal(bindings, binding_value_variable_id(right_value),
                      atom_symbol_current_id(right), right->name_key, left_value, true);
            if (!added) {
                goto fail;
            }
            continue;
        }
        if (left->kind == ATOM_SYMBOL && right->kind == ATOM_SYMBOL) {
            if (!atom_symbols_equal(left, right)) {
                if (attempt_resolving_bound)
                    attempt_repeated_var = true;
                goto mismatch;
            }
            continue;
        }
        if (left->kind == ATOM_GROUNDED && right->kind == ATOM_GROUNDED) {
            if (!atom_eq_with_resolved_spaces(left, right)) {
                if (attempt_resolving_bound)
                    attempt_repeated_var = true;
                goto mismatch;
            }
            continue;
        }
        if (!atom_petta_decomposition_compatible(left, right))
            goto mismatch;
        MatchListMeeting meet;
        MatchListOutcome list_outcome =
            left->kind == ATOM_EXPR && right->kind == ATOM_EXPR
                ? match_list_meet(a, left, right, &meet)
                : MATCH_LIST_EXPRESSIONS;
        if (list_outcome == MATCH_LIST_INCOMPLETE) goto fail;
        if (list_outcome == MATCH_LIST_MISMATCH ||
            left->kind != ATOM_EXPR || right->kind != ATOM_EXPR ||
            (list_outcome == MATCH_LIST_EXPRESSIONS &&
             atom_authored_len(left) != atom_authored_len(right))) {
            if (attempt_resolving_bound)
                attempt_repeated_var = true;
            goto mismatch;
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
        if (list_outcome == MATCH_LIST_ELEMENTS) {
            /* The rest meets last, after the elements, left to right. */
            if (meet.left_rest &&
                !decoded_match_push(
                    &work,
                    (MatchTerm){.value = meet.left_derived
                        ? binding_value_with_derived_skeleton(left_value, meet.left_rest)
                        : binding_value_with_skeleton(left_value, meet.left_rest)},
                    (MatchTerm){.value = meet.right_derived
                        ? binding_value_with_derived_skeleton(right_value, meet.right_rest)
                        : binding_value_with_skeleton(right_value, meet.right_rest)}))
                goto fail;
            for (CettaExprIndex i = meet.prefix; i >= 1u; i--) {
                BindingValue left_child = left_value, right_child = right_value;
                left_child.skeleton = left->expr.elems[i];
                right_child.skeleton = right->expr.elems[i];
                if (!decoded_match_push(&work,
                        (MatchTerm){.value = left_child},
                        (MatchTerm){.value = right_child}))
                    goto fail;
            }
            continue;
        }
        /* Push in reverse so binding effects retain the recursive
           implementation's left-to-right traversal order. */
        for (CettaExprIndex i = atom_authored_len(left); i > 1u; i--) {
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
        if (atom_authored_len(left) != 0u) {
            Atom *next_left = left->expr.elems[0];
            Atom *next_right = right->expr.elems[0];
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_MATCH_WORKLIST_FRAME_ELIDED);
            left_value.skeleton = next_left;
            right_value.skeleton = next_right;
            dereferences = 0u;
            dereference_limit = 0u;
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

mismatch:
    if (failure) *failure = CETTA_MATCH_REFUTED;
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

static __attribute__((noinline)) bool match_decoded_atoms_worklist_plain(
    BindingValue left_value, BindingValue right_value, Bindings *bindings,
    BindingsBuilder *builder, Arena *a, bool undefined_is_wildcard) {
    return match_decoded_atoms_worklist_mode(
        left_value, right_value, bindings, builder, a, undefined_is_wildcard, false, NULL);
}

static __attribute__((noinline)) bool match_decoded_atoms_worklist_observed(
    BindingValue left_value, BindingValue right_value, Bindings *bindings,
    BindingsBuilder *builder, Arena *a, bool undefined_is_wildcard) {
    return match_decoded_atoms_worklist_mode(
        left_value, right_value, bindings, builder, a, undefined_is_wildcard, true, NULL);
}

static bool match_decoded_atoms_worklist(
    BindingValue left_value, BindingValue right_value, Bindings *bindings,
    BindingsBuilder *builder, Arena *a, bool undefined_is_wildcard) {
    return cetta_native_cost_active()
        ? match_decoded_atoms_worklist_observed(
            left_value, right_value, bindings, builder, a, undefined_is_wildcard)
        : match_decoded_atoms_worklist_plain(
            left_value, right_value, bindings, builder, a, undefined_is_wildcard);
}

/* A failure observer is explicit and local to this checked operation. The
 * plain and observed legacy entries keep their compile-time-null observer. */
static bool match_decoded_atoms_worklist_checked(
    BindingValue left, BindingValue right, Bindings *bindings,
    BindingsBuilder *builder, Arena *arena, bool undefined_is_wildcard,
    CettaMatchCompletion *failure) {
    if (!failure)
        return match_decoded_atoms_worklist(
            left, right, bindings, builder, arena, undefined_is_wildcard);
    return cetta_native_cost_active()
        ? match_decoded_atoms_worklist_mode(
            left, right, bindings, builder, arena, undefined_is_wildcard, true, failure)
        : match_decoded_atoms_worklist_mode(
            left, right, bindings, builder, arena, undefined_is_wildcard, false, failure);
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

/* A held value, typed (Data ...), is an atom, and its payload has a syntax
 * shape that only inspection can tell, so it is accepted where Atom or a
 * shape type is expected. */
static bool match_types_is_data(Atom *type) {
    return type &&
        ((type->kind == ATOM_SYMBOL && atom_is_symbol(type, "Data")) ||
         (type->kind == ATOM_EXPR && type->expr.len >= 1u &&
          atom_is_symbol(type->expr.elems[0], "Data")));
}

static bool match_types_held_accepted(Atom *actual, Atom *expected) {
    /* A position expecting Data holds whatever it receives: raw syntax is
     * held at (Data Atom) without checking, and checking held code at a
     * result type is a separate judgment. */
    if (match_types_is_data(expected)) return true;
    bool held = match_types_is_data(actual);
    if (!held || !expected || expected->kind != ATOM_SYMBOL) return false;
    static const char *const accepting[] = {
        "Atom", "Expression", "Symbol", "Variable", "Grounded"};
    for (size_t i = 0u; i < sizeof(accepting) / sizeof(accepting[0]); i++)
        if (atom_is_symbol(expected, accepting[i])) return true;
    return false;
}

bool match_types(Atom *actual, Atom *expected, Bindings *b, Arena *a) {
    if (match_types_held_accepted(actual, expected)) return true;
    /* Atom is the expected-side value top. An actual Atom is not evidence for
       an arbitrary concrete expected type. */
    if (atom_is_symbol_id(expected, g_builtin_syms.atom) ||
        (eval_current_language_id && eval_current_language_id()==CETTA_LANGUAGE_PRIME &&
            atom_is_symbol_id(expected,g_builtin_syms.prime_proc))) return true;
    if (type_match_uses_space_class_bridge(actual, expected)) {
        return true;
    }
    return match_decoded_atoms_worklist(
        binding_value_from_atom(actual), binding_value_from_atom(expected), b, NULL,
        a, true);
}

bool match_types_builder(Atom *actual, Atom *expected, BindingsBuilder *bb,
                         Arena *a) {
    if (match_types_held_accepted(actual, expected)) return true;
    if (atom_is_symbol_id(expected, g_builtin_syms.atom) ||
        (eval_current_language_id && eval_current_language_id()==CETTA_LANGUAGE_PRIME &&
            atom_is_symbol_id(expected,g_builtin_syms.prime_proc))) return true;
    if (type_match_uses_space_class_bridge(actual, expected)) {
        return true;
    }
    return match_decoded_atoms_worklist(
        binding_value_from_atom(actual), binding_value_from_atom(expected), NULL, bb,
        a, true);
}

/* ── Bidirectional matching (match_atoms from HE spec metta.md:577-617) ── */

static bool match_atoms_epoch_worklist(Atom *left, Atom *right,
                                       Bindings *bindings,
                                       BindingsBuilder *builder,
                                       Arena *a, CettaFrameIdentity epoch);
static bool match_atoms_epoch_rule_local_worklist(
    Atom *left, Atom *right, BindingsBuilder *builder,
    Arena *a, CettaFrameIdentity epoch);
static bool match_atoms_epoch_rule_local_planned_worklist(
    Atom *left, Atom *right, const CettaOpenPatternPlan *right_plan,
    BindingsBuilder *builder, Arena *a, CettaFrameIdentity epoch);
static bool match_atoms_epoch_view_worklist(
    Atom *left, CettaFrameIdentity left_epoch, uint32_t left_first_entry,
    Atom *right, Bindings *bindings, BindingsBuilder *builder,
    Arena *a, CettaFrameIdentity right_epoch);
static bool match_atoms_epoch_view_current_worklist(
    Atom *left, CettaFrameIdentity left_epoch, uint32_t left_first_entry,
    Atom *right, Bindings *bindings, BindingsBuilder *builder,
    Arena *a);
static bool match_atoms_epoch_view_rule_local_worklist(
    Atom *left, CettaFrameIdentity left_epoch, uint32_t left_first_entry,
    Atom *right, BindingsBuilder *builder, Arena *a, CettaFrameIdentity right_epoch);
static bool match_atoms_epoch_view_rule_local_planned_worklist(
    Atom *left, CettaFrameIdentity left_epoch, uint32_t left_first_entry,
    Atom *right, const CettaOpenPatternPlan *right_plan,
    BindingsBuilder *builder, Arena *a, CettaFrameIdentity right_epoch);
static bool match_atoms_activation_view_worklist(
    Atom *left, const BindingsActivationView *left_frame,
    Atom *right, Bindings *bindings, BindingsBuilder *builder,
    Arena *a, CettaFrameIdentity right_epoch, bool right_original,
    bool prefer_right_rule_slot,
    const CettaOpenPatternPlan *right_plan);
static bool match_atoms_atom_id_epoch_worklist(
    BindingValue left_value, const TermUniverse *candidate_universe, AtomId right_id,
    Bindings *b, Arena *a, CettaFrameIdentity epoch);
static bool match_atoms_epoch_views_linear(
    Atom *left, BindingValueKind left_kind, CettaFrameIdentity left_epoch,
    uint32_t left_first_entry, Atom *right,
    Bindings *bindings, BindingsBuilder *builder,
    Arena *a, CettaFrameIdentity right_epoch, BindingValueKind right_kind,
    bool prefer_right_rule_slot,
    const BindingsActivationView *left_frame,
    const CettaOpenPatternPlan *right_plan,
    BindingsExclusiveFrame *exclusive);

bool match_binding_values(BindingValue left, BindingValue right, Bindings *b,
                          Arena *a) {
    return left.skeleton && right.skeleton &&
        match_decoded_atoms_worklist(left, right, b, NULL, a, false);
}

bool match_atoms(Atom *left, Atom *right, Bindings *b, Arena *a) {
    return match_binding_values(binding_value_from_atom(left),
                                binding_value_from_atom(right), b, a);
}

bool match_binding_values_builder(BindingValue left, BindingValue right,
                                  BindingsBuilder *bb, Arena *a) {
    return left.skeleton && right.skeleton && bb &&
        match_decoded_atoms_worklist(left, right, NULL, bb, a, false);
}

bool match_atoms_builder(Atom *left, Atom *right, BindingsBuilder *bb,
                         Arena *a) {
    return match_binding_values_builder(binding_value_from_atom(left),
                                        binding_value_from_atom(right), bb, a);
}

bool match_atoms_builder_with_attempt_frame(
        Atom *left, Atom *right, BindingsBuilder *bb,
        const BindingsActivationView *left_frame, Arena *a) {
    (void)left_frame;
    return match_atoms_builder(left, right, bb, a);
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
                                         Arena *a, CettaFrameIdentity epoch) {
    if (!query || !lhs || !b || !a)
        return false;
    if (query->kind != ATOM_EXPR || lhs->kind != ATOM_EXPR)
        return false;
    if (lhs->expr.len == 0 || query->expr.len != lhs->expr.len)
        return false;
    if (!atom_petta_decomposition_compatible(query, lhs))
        return false;
    Atom *lh = lhs->expr.elems[0];
    Atom *qh = query->expr.elems[0];
    if (!lh || !qh || lh->kind != ATOM_SYMBOL || qh->kind != ATOM_SYMBOL ||
        !atom_symbols_equal(lh, qh))
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
            if (var_id_eq(seen[j], eid))
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
        Arena *a, CettaFrameIdentity epoch) {
    if (!query || !lhs || !bb || !a)
        return false;
    if (query->kind != ATOM_EXPR || lhs->kind != ATOM_EXPR)
        return false;
    if (lhs->expr.len == 0u || query->expr.len != lhs->expr.len)
        return false;
    if (!atom_petta_decomposition_compatible(query, lhs))
        return false;
    Atom *lh = lhs->expr.elems[0];
    Atom *qh = query->expr.elems[0];
    if (!lh || !qh || lh->kind != ATOM_SYMBOL || qh->kind != ATOM_SYMBOL ||
        !atom_symbols_equal(lh, qh))
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
            if (var_id_eq(seen[j], eid))
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

bool match_atoms_epoch(Atom *left, Atom *right, Bindings *b, Arena *a, CettaFrameIdentity epoch) {
    return match_atoms_epoch_worklist(left, right, b, NULL, a, epoch);
}

bool match_atoms_epoch_builder(Atom *left, Atom *right,
                               BindingsBuilder *bb, Arena *a,
                               CettaFrameIdentity epoch) {
    return match_atoms_epoch_worklist(left, right, NULL, bb, a, epoch);
}

bool match_atoms_epoch_builder_rule_local(
        Atom *left, Atom *right, BindingsBuilder *bb,
        Arena *a, CettaFrameIdentity epoch) {
    return match_atoms_epoch_rule_local_worklist(
        left, right, bb, a, epoch);
}

bool match_atoms_epoch_builder_rule_local_planned(
        Atom *left, Atom *right,
        const CettaOpenPatternPlan *right_plan,
        BindingsBuilder *bb, Arena *a, CettaFrameIdentity epoch) {
    return match_atoms_epoch_rule_local_planned_worklist(
        left, right, right_plan, bb, a, epoch);
}

bool match_atoms_epoch_builder_rule_local_linear(
        Atom *left, Atom *right,
        const CettaOpenPatternPlan *right_plan,
        BindingsBuilder *bb, Arena *a, CettaFrameIdentity epoch) {
    return match_atoms_epoch_views_linear(
        left, BINDING_VALUE_MATERIALIZED, CETTA_FRAME_IDENTITY_NONE, 0u,
        right, NULL, bb, a, epoch,
        BINDING_VALUE_CONTEXTUAL, true, NULL, right_plan, NULL);
}

bool match_atoms_epoch_view_builder(
        Atom *left_original, CettaFrameIdentity left_epoch,
        uint32_t left_first_entry, Atom *right_original,
        BindingsBuilder *bb, Arena *a, CettaFrameIdentity right_epoch) {
    return match_atoms_epoch_view_worklist(
        left_original, left_epoch, left_first_entry,
        right_original, NULL, bb, a, right_epoch);
}

bool match_atoms_epoch_view_builder_current(
        Atom *left_original, CettaFrameIdentity left_epoch,
        uint32_t left_first_entry, Atom *right,
        BindingsBuilder *bb, Arena *a) {
    return match_atoms_epoch_view_current_worklist(
        left_original, left_epoch, left_first_entry,
        right, NULL, bb, a);
}

bool match_atoms_epoch_view_builder_rule_local(
        Atom *left_original, CettaFrameIdentity left_epoch,
        uint32_t left_first_entry, Atom *right_original,
        BindingsBuilder *bb, Arena *a, CettaFrameIdentity right_epoch) {
    return match_atoms_epoch_view_rule_local_worklist(
        left_original, left_epoch, left_first_entry,
        right_original, bb, a, right_epoch);
}

bool match_atoms_epoch_view_builder_rule_local_planned(
        Atom *left_original, CettaFrameIdentity left_epoch,
        uint32_t left_first_entry, Atom *right_original,
        const CettaOpenPatternPlan *right_plan,
        BindingsBuilder *bb, Arena *a, CettaFrameIdentity right_epoch) {
    return match_atoms_epoch_view_rule_local_planned_worklist(
        left_original, left_epoch, left_first_entry,
        right_original, right_plan, bb, a, right_epoch);
}

bool match_atoms_epoch_view_builder_rule_local_linear(
        Atom *left_original, CettaFrameIdentity left_epoch,
        uint32_t left_first_entry, Atom *right_original,
        const CettaOpenPatternPlan *right_plan,
        BindingsBuilder *bb, Arena *a, CettaFrameIdentity right_epoch) {
    return match_atoms_epoch_views_linear(
        left_original, BINDING_VALUE_CONTEXTUAL,
        left_epoch, left_first_entry,
        right_original, NULL, bb, a, right_epoch,
        BINDING_VALUE_CONTEXTUAL, true, NULL, right_plan, NULL);
}

bool match_atoms_activation_view_builder(
        Atom *left_original, const BindingsActivationView *left_frame,
        Atom *right_original, BindingsBuilder *bb, Arena *a,
        CettaFrameIdentity right_epoch) {
    return match_atoms_activation_view_worklist(
        left_original, left_frame, right_original,
        NULL, bb, a, right_epoch, true, false, NULL);
}

bool match_atoms_activation_view_builder_current(
        Atom *left_original, const BindingsActivationView *left_frame,
        Atom *right, BindingsBuilder *bb, Arena *a) {
    return match_atoms_activation_view_worklist(
        left_original, left_frame, right,
        NULL, bb, a, CETTA_FRAME_IDENTITY_NONE, false, false, NULL);
}

bool match_atoms_activation_view_builder_rule_local(
        Atom *left_original, const BindingsActivationView *left_frame,
        Atom *right_original, BindingsBuilder *bb, Arena *a,
        CettaFrameIdentity right_epoch) {
    return match_atoms_activation_view_worklist(
        left_original, left_frame, right_original,
        NULL, bb, a, right_epoch, true, true, NULL);
}

bool match_atoms_activation_view_builder_rule_local_planned(
        Atom *left_original,
        const BindingsActivationView *left_frame,
        Atom *right_original,
        const CettaOpenPatternPlan *right_plan,
        BindingsBuilder *bb, Arena *a, CettaFrameIdentity right_epoch) {
    return match_atoms_activation_view_worklist(
        left_original, left_frame, right_original,
        NULL, bb, a, right_epoch, true, true, right_plan);
}

bool match_atoms_activation_view_builder_rule_local_linear(
        Atom *left_original,
        const BindingsActivationView *left_frame,
        Atom *right_original,
        const CettaOpenPatternPlan *right_plan,
        BindingsBuilder *bb, Arena *a, CettaFrameIdentity right_epoch) {
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
                               CettaFrameIdentity epoch) {
    return match_binding_value_atom_id_epoch(
        binding_value_from_atom(left), candidate_universe, right_id, b, a, epoch);
}

bool match_binding_value_atom_id_epoch(
        BindingValue left, const TermUniverse *candidate_universe,
        AtomId right_id, Bindings *b, Arena *a, CettaFrameIdentity epoch) {
    return match_atoms_atom_id_epoch_worklist(left, candidate_universe, right_id, b, a, epoch);
}

static bool alpha_scalar_leaf(const Atom *atom) {
    if (!atom)
        return false;
    if (atom->kind == ATOM_SYMBOL)
        return true;
    if (atom->kind != ATOM_GROUNDED)
        return false;
    switch (atom->ground.gkind) {
    case GV_INT:
    case GV_FLOAT:
    case GV_BOOL:
    case GV_STRING:
    case GV_BIGINT:
    case GV_RATIONAL:
    case GV_INTERNAL_TAG:
        return true;
    default:
        return false;
    }
}

static CettaMatchCompletion atom_alpha_eq_mode(Atom *left, Atom *right, bool resolve_spaces) {
    if (!left || !right)
        return CETTA_MATCH_INCOMPLETE;
    if (left->kind != right->kind)
        return CETTA_MATCH_REFUTED;
    if (left->kind != ATOM_EXPR)
        return left->kind == ATOM_VAR ||
            (resolve_spaces ? atom_eq_with_resolved_spaces(left, right) : atom_eq(left, right))
            ? CETTA_MATCH_MATCHED : CETTA_MATCH_REFUTED;
    if (atom_authored_len(left) != atom_authored_len(right))
        return CETTA_MATCH_REFUTED;
    /* Ground flat terms need no renaming environment or comparison frames.
     * Classify the entire pair before comparing: an opaque equality service
     * must never run once here and again after a later child causes fallback. */
    for (CettaExprIndex i = 0u; i < atom_authored_len(left); i++) {
        if (!alpha_scalar_leaf(left->expr.elems[i]) ||
            !alpha_scalar_leaf(right->expr.elems[i]))
            return atom_pair_walk(left, right, NULL, NULL, NULL, true, NULL, resolve_spaces);
    }
    for (CettaExprIndex i = 0u; i < atom_authored_len(left); i++) {
        if (!atom_eq(left->expr.elems[i], right->expr.elems[i]))
            return CETTA_MATCH_REFUTED;
    }
    return CETTA_MATCH_MATCHED;
}

bool atom_alpha_eq(Atom *left, Atom *right) {
    return atom_alpha_eq_mode(left, right, false) == CETTA_MATCH_MATCHED;
}

CettaMatchCompletion atom_alpha_eq_checked(Atom *left, Atom *right) {
    return atom_alpha_eq_mode(left, right, false);
}

CettaMatchCompletion atom_alpha_eq_with_resolved_spaces_checked(Atom *left, Atom *right) {
    return atom_alpha_eq_mode(left, right, true);
}

bool atom_alpha_eq_scoped(Atom *left, Atom *right,
        const CettaFrameIdentity *left_epochs, size_t left_count,
        const CettaFrameIdentity *right_epochs, size_t right_count) {
    if ((!left_epochs && left_count) || (!right_epochs && right_count)) return false;
    AlphaFrameScope scope = {left_epochs, right_epochs, left_count, right_count};
    return atom_pair_walk(left, right, NULL, NULL, NULL, true, &scope, false) == CETTA_MATCH_MATCHED;
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
        CettaFrameIdentity epoch) {
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_MATCH_RULE_FRAME_REGION_BORROW_ATTEMPT);
    if (view && view->exclusive && view->exclusive->active) {
        if (!cetta_frame_identity_eq(view->exclusive->epoch, epoch))
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
    if (!view || !builder || cetta_frame_identity_is_none(epoch) ||
        !builder->current.frame_index) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_MATCH_RULE_FRAME_REGION_BORROW_GENERAL);
        return false;
    }
    BindingsFrameIndexEntry *frame = bindings_frame_index_find_frame(
        builder->current.frame_index, epoch);
    if (!frame || !frame->schema_complete ||
        frame->schema->len > BINDINGS_DIRECT_FRAME_SLOT_CAP ||
        frame->cap < frame->slot_len ||
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
        CettaFrameIdentity epoch) {
    if (!view || !builder || cetta_frame_identity_is_none(epoch) ||
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
        Atom *source, CettaFrameIdentity epoch) {
    if (!builder || !source || cetta_frame_identity_is_none(epoch))
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
        Atom *source_var, CettaFrameIdentity epoch) {
    if (!builder || !source_var || source_var->kind != ATOM_VAR ||
        var_id_is_none(source_var->var_id) || cetta_frame_identity_is_none(epoch)) {
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
    VarId source_id = var_source_id(source_var->var_id);
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
    if (!bindings || var_id_is_none(activated_id) ||
        !rule_frame_region_index(view, variable_mask, &slot)) {
        return !var_id_is_none(activated_id)
            ? bindings_exclusive_frame_lookup(
                  view ? view->exclusive : NULL, bindings,
                  activated_id, NULL)
            : binding_value_from_atom(NULL);
    }
    if (!view->values ||
        !cetta_frame_identity_eq(view->epoch, var_epoch_suffix(activated_id)) ||
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
    if (view && view->exclusive && view->exclusive->active) {
        if (binding_value_is_contextual(variable) &&
            cetta_frame_identity_eq(binding_value_epoch(variable), view->exclusive->epoch)) {
            return bindings_exclusive_frame_lookup(
                view->exclusive, bindings, id, NULL);
        }
        /* A variable of a stored value that this match already bound is
         * bound by the frame's writes, which are not yet published: as for
         * a variable on the left, they are read first. */
        bool written = false;
        BindingValue pending = bindings_exclusive_frame_lookup(
            view->exclusive, NULL, id, &written);
        if (written)
            return pending;
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
            !atom_symbols_equal(query, child->source);
    if (query->kind == ATOM_EXPR)
        return query->expr.len != child->child_count;
    if (query->kind == ATOM_GROUNDED)
        return child->source && child->source->kind == ATOM_GROUNDED &&
            !atom_eq_with_resolved_spaces(query, child->source);
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
            if (qi->kind == ATOM_SYMBOL && !atom_symbols_equal(qi, qj))
                return true;
            break;
        }
    }
    return false;
}

/* Physical finite-occurrence and lexical occurs queries share one retained
 * graph traversal. Active marks reject physical cycles; completed marks reuse
 * shared subgraphs. Occurs queries additionally follow qualified variable
 * bindings. Neither query publishes a binding while its verdict is pending.
 * Memo clearing, probing, migration and stack copying retain their own
 * positions in the same query. Allocator calls, transport and publication
 * still require separate prices; these are not processor-time bounds. */
typedef enum {
    MATCH_GRAPH_OCCURS,
    MATCH_GRAPH_FINITE
} MatchGraphQuery;

typedef struct {
    BindingValue value;
    CettaExprIndex child;
    bool entered;
} MatchGraphFrame;

typedef enum {
    MATCH_GRAPH_MEMO_IDLE,
    MATCH_GRAPH_MEMO_CLEAR,
    MATCH_GRAPH_MEMO_SCAN,
    MATCH_GRAPH_MEMO_MOVE,
    MATCH_GRAPH_MEMO_INSTALL,
    MATCH_GRAPH_MEMO_LOOKUP
} MatchGraphMemoPhase;

typedef struct {
    MatchGraphFrame *items;
    size_t len, cap;
    BindingApplyGraph graph;
    BindingApplyGraph growing;
    BindingApplyGraphEntry migrating;
    size_t cleared, scanned, probe;
    MatchGraphMemoPhase memo_phase;
    MatchGraphFrame *growing_items;
    size_t growing_cap, copied;
    VarIdSet variables;
    VarId target;
    MatchGraphQuery query;
} MatchGraphCheck;

static void match_graph_check_free(MatchGraphCheck *check) {
    if (!check) return;
    free(check->items);
    free(check->graph.entries);
    free(check->growing.entries);
    free(check->growing_items);
    var_id_set_free(&check->variables);
    free(check);
}

static bool match_graph_check_push(MatchGraphCheck *check, BindingValue value) {
    if (!value.skeleton) return false;
    if (check->len == check->cap) {
        size_t cap = check->cap ? check->cap * 2u : 16u;
        if (cap < check->cap || cap > SIZE_MAX / sizeof(*check->items)) return false;
        MatchGraphFrame *items = realloc(check->items, cap * sizeof(*items));
        if (!items) return false;
        check->items = items;
        check->cap = cap;
    }
    check->items[check->len++] = (MatchGraphFrame){.value = value};
    return true;
}

static MatchGraphCheck *match_graph_check_new(
        MatchGraphQuery query, VarId target, BindingValue value) {
    MatchGraphCheck *check = calloc(1u, sizeof(*check));
    if (!check) return NULL;
    var_id_set_init(&check->variables);
    check->target = target;
    check->query = query;
    if (!match_graph_check_push(check, value)) {
        match_graph_check_free(check);
        return NULL;
    }
    return check;
}

/* One debit precedes one fixed-size representation action. malloc/free are
 * counted calls here; their byte-dependent service costs are not hidden in
 * the index/analysis valuation. No destination slot is read before clearing. */
static void match_graph_check_debit(uint64_t *budget, CettaNativeCostKind kind) {
    --*budget;
    cetta_native_cost_charge(kind, 1u);
}

#ifdef CETTA_TEST_HOOKS
static _Thread_local bool match_graph_test_allocation_refused;
void match_atoms_cursor_test_refuse_graph_allocation(void) {
    match_graph_test_allocation_refused = true;
}
#endif

static void *match_graph_check_allocate(size_t bytes) {
#ifdef CETTA_TEST_HOOKS
    if (match_graph_test_allocation_refused) {
        match_graph_test_allocation_refused = false;
        return NULL;
    }
#endif
    return malloc(bytes);
}

/* NULL with valid=true is a saved maintenance/probe position. The caller
 * keeps the same top frame until an entry is returned. */
static BindingApplyGraphEntry *match_graph_check_entry(MatchGraphCheck *check,
        Atom *atom, BindingsApplyScope scope, bool entering,
        uint64_t *budget, bool *valid) {
    *valid = true;
    if (!*budget) return NULL;
    if (check->memo_phase == MATCH_GRAPH_MEMO_IDLE) {
        if (entering && (!check->graph.cap ||
                check->graph.len + 1u > check->graph.cap / 2u)) {
            size_t capacity = check->graph.cap ? check->graph.cap * 2u : 32u;
            if (capacity < check->graph.cap ||
                    capacity > SIZE_MAX / sizeof(*check->growing.entries)) {
                *valid = false;
                return NULL;
            }
            match_graph_check_debit(budget, CETTA_COST_ANALYSIS_STEP);
            check->growing.entries = match_graph_check_allocate(
                capacity * sizeof(*check->growing.entries));
            if (!check->growing.entries) { *valid = false; return NULL; }
            check->growing.cap = capacity;
            check->growing.len = check->graph.len;
            check->cleared = check->scanned = 0u;
            check->memo_phase = MATCH_GRAPH_MEMO_CLEAR;
            return NULL;
        }
        if (!check->graph.cap) { *valid = false; return NULL; }
        check->probe = bindings_apply_graph_hash(atom, scope.epoch, scope.kind, 0u) &
            (check->graph.cap - 1u);
        check->memo_phase = MATCH_GRAPH_MEMO_LOOKUP;
    }
    switch (check->memo_phase) {
    case MATCH_GRAPH_MEMO_CLEAR:
        match_graph_check_debit(budget, CETTA_COST_INDEX_VISIT);
        check->growing.entries[check->cleared++] = (BindingApplyGraphEntry){0};
        if (check->cleared == check->growing.cap)
            check->memo_phase = MATCH_GRAPH_MEMO_SCAN;
        return NULL;
    case MATCH_GRAPH_MEMO_SCAN:
        if (check->scanned == check->graph.cap) {
            check->memo_phase = MATCH_GRAPH_MEMO_INSTALL;
            /* The phase transition and installation share one debit. */
            return match_graph_check_entry(check, atom, scope, entering, budget, valid);
        }
        match_graph_check_debit(budget, CETTA_COST_INDEX_VISIT);
        check->migrating = check->graph.entries[check->scanned++];
        if (check->migrating.source) {
            BindingApplyGraphEntry *entry = &check->migrating;
            check->probe = bindings_apply_graph_hash(
                entry->source, entry->epoch, entry->kind, entry->code_depth) &
                (check->growing.cap - 1u);
            check->memo_phase = MATCH_GRAPH_MEMO_MOVE;
        } else if (check->scanned == check->graph.cap) {
            check->memo_phase = MATCH_GRAPH_MEMO_INSTALL;
        }
        return NULL;
    case MATCH_GRAPH_MEMO_MOVE:
        match_graph_check_debit(budget, CETTA_COST_INDEX_VISIT);
        if (check->growing.entries[check->probe].source) {
            check->probe = (check->probe + 1u) & (check->growing.cap - 1u);
        } else {
            check->growing.entries[check->probe] = check->migrating;
            check->migrating = (BindingApplyGraphEntry){0};
            check->memo_phase = check->scanned == check->graph.cap
                ? MATCH_GRAPH_MEMO_INSTALL : MATCH_GRAPH_MEMO_SCAN;
        }
        return NULL;
    case MATCH_GRAPH_MEMO_INSTALL:
        match_graph_check_debit(budget, CETTA_COST_ANALYSIS_STEP);
        free(check->graph.entries);
        check->graph = check->growing;
        check->growing = (BindingApplyGraph){0};
        check->memo_phase = MATCH_GRAPH_MEMO_IDLE;
        return NULL;
    case MATCH_GRAPH_MEMO_LOOKUP: {
        match_graph_check_debit(budget, CETTA_COST_INDEX_VISIT);
        BindingApplyGraphEntry *entry = &check->graph.entries[check->probe];
        if (entry->source && !bindings_apply_graph_key_equal(entry, atom, scope, 0u)) {
            check->probe = (check->probe + 1u) & (check->graph.cap - 1u);
            return NULL;
        }
        if (!entry->source) {
            if (!entering) { *valid = false; return NULL; }
            *entry = (BindingApplyGraphEntry){.source = atom, .epoch = scope.epoch,
                .kind = scope.kind};
            ++check->graph.len;
        }
        check->memo_phase = MATCH_GRAPH_MEMO_IDLE;
        return entry;
    }
    case MATCH_GRAPH_MEMO_IDLE:
        break;
    }
    *valid = false;
    return NULL;
}

/* Keep the old stack installed until every live frame has been copied.
 * The pending parent edge is advanced only after installation. */
static bool match_graph_check_stack(MatchGraphCheck *check, uint64_t *budget,
        bool *valid) {
    *valid = true;
    if (check->len < check->cap && !check->growing_items) return true;
    if (!*budget) return false;
    if (!check->growing_items) {
        size_t capacity = check->cap * 2u;
        if (capacity <= check->cap || capacity > SIZE_MAX / sizeof(*check->items)) {
            *valid = false;
            return false;
        }
        match_graph_check_debit(budget, CETTA_COST_ANALYSIS_STEP);
        check->growing_items = match_graph_check_allocate(capacity * sizeof(*check->items));
        if (!check->growing_items) { *valid = false; return false; }
        check->growing_cap = capacity;
        check->copied = 0u;
    } else if (check->copied < check->len) {
        match_graph_check_debit(budget, CETTA_COST_ANALYSIS_STEP);
        check->growing_items[check->copied] = check->items[check->copied];
        ++check->copied;
    } else {
        match_graph_check_debit(budget, CETTA_COST_ANALYSIS_STEP);
        free(check->items);
        check->items = check->growing_items;
        check->cap = check->growing_cap;
        check->growing_items = NULL;
        check->growing_cap = check->copied = 0u;
    }
    return false;
}

/* True means a complete verdict; false with remaining work is a scheduling
 * pause. False with no work is an invalid/capacity boundary, never absence. */
static bool match_graph_check_step(MatchGraphCheck *check, Bindings *bindings,
        uint64_t *budget, BindingsReachability *verdict) {
    *verdict = BINDINGS_REACHABILITY_UNKNOWN;
    while (check->len && *budget) {
        MatchGraphFrame *frame = &check->items[check->len - 1u];
        BindingValue value = frame->value;
        Atom *atom = value.skeleton;
        if (!atom || (check->query == MATCH_GRAPH_OCCURS && !atom_has_vars(atom))) {
            match_graph_check_debit(budget, CETTA_COST_ANALYSIS_STEP);
            --check->len;
            continue;
        }
        if (check->query == MATCH_GRAPH_OCCURS && atom->kind == ATOM_VAR) {
            match_graph_check_debit(budget, CETTA_COST_ANALYSIS_STEP);
            --check->len;
            VarId id = binding_value_qualify_id(value, atom->var_id);
            if (var_id_is_none(id)) goto invalid;
            if (binding_var_eq(id, check->target)) {
                *verdict = BINDINGS_REACHABILITY_PRESENT;
                return true;
            }
            if (var_id_set_contains(&check->variables, id)) continue;
            if (!var_id_set_add(&check->variables, id)) goto invalid;
            BindingValue next = binding_value_from_atom(NULL);
            if (!bindings_frame_index_lookup_context_id(bindings, value, id, &next))
                next = bindings_lookup_value_id(bindings, id);
            if (!next.skeleton || (next.skeleton->kind == ATOM_VAR &&
                    binding_var_eq(binding_value_variable_id(next), id))) continue;
            if (!match_graph_check_push(check, next)) goto invalid;
            continue;
        }
        if (atom->kind != ATOM_EXPR) {
            match_graph_check_debit(budget, CETTA_COST_ANALYSIS_STEP);
            --check->len;
            continue;
        }
        BindingsApplyScope scope = {.epoch = binding_value_epoch(value),
                                    .kind = binding_value_kind(value)};
        if (!frame->entered) {
            size_t previous = check->graph.len;
            bool valid = true;
            BindingApplyGraphEntry *entry = match_graph_check_entry(
                check, atom, scope, true, budget, &valid);
            if (!valid) goto invalid;
            if (!entry) continue;
            bool created = previous != check->graph.len;
            if (!created && entry->active) {
                if (check->query != MATCH_GRAPH_FINITE) goto invalid;
                *verdict = BINDINGS_REACHABILITY_PRESENT;
                return true;
            }
            if (!created) {
                --check->len;
                continue;
            }
            entry->active = true;
            frame->entered = true;
        } else if (frame->child < atom->expr.len) {
            bool valid = true;
            if (!match_graph_check_stack(check, budget, &valid)) {
                if (!valid) goto invalid;
                continue;
            }
            match_graph_check_debit(budget, CETTA_COST_ANALYSIS_STEP);
            value.skeleton = atom->expr.elems[frame->child++];
            if (!value.skeleton && check->query == MATCH_GRAPH_FINITE) continue;
            if (!match_graph_check_push(check, value)) goto invalid;
        } else {
            bool valid = true;
            BindingApplyGraphEntry *entry = match_graph_check_entry(
                check, atom, scope, false, budget, &valid);
            if (!valid) goto invalid;
            if (!entry) continue;
            entry->active = false;
            --check->len;
        }
    }
    if (check->len) return false;
    *verdict = BINDINGS_REACHABILITY_ABSENT;
    return true;
invalid:
    check->len = 0u;
    return false;
}

static MatchGraphCheck *match_graph_check_clone(const MatchGraphCheck *source) {
    if (!source) return NULL;
    MatchGraphCheck *copy = calloc(1u, sizeof(*copy));
    if (!copy) return NULL;
    var_id_set_init(&copy->variables);
    copy->target = source->target;
    copy->query = source->query;
    copy->memo_phase = source->memo_phase;
    copy->cleared = source->cleared;
    copy->scanned = source->scanned;
    copy->probe = source->probe;
    copy->migrating = source->migrating;
    if (source->cap) {
        if (source->cap > SIZE_MAX / sizeof(*copy->items)) goto fail;
        copy->items = malloc(source->cap * sizeof(*copy->items));
        if (!copy->items) goto fail;
        memcpy(copy->items, source->items, source->len * sizeof(*copy->items));
        copy->len = source->len;
        copy->cap = source->cap;
    }
    if (source->graph.cap) {
        if (source->graph.cap > SIZE_MAX / sizeof(*copy->graph.entries)) goto fail;
        copy->graph.entries = malloc(source->graph.cap * sizeof(*copy->graph.entries));
        if (!copy->graph.entries) goto fail;
        memcpy(copy->graph.entries, source->graph.entries,
               source->graph.cap * sizeof(*copy->graph.entries));
        copy->graph.cap = source->graph.cap;
        copy->graph.len = source->graph.len;
    }
    if (source->growing.cap) {
        copy->growing.entries = malloc(source->growing.cap * sizeof(*copy->growing.entries));
        if (!copy->growing.entries) goto fail;
        size_t initialized = source->memo_phase == MATCH_GRAPH_MEMO_CLEAR
            ? source->cleared : source->growing.cap;
        memcpy(copy->growing.entries, source->growing.entries,
               initialized * sizeof(*copy->growing.entries));
        copy->growing.cap = source->growing.cap;
        copy->growing.len = source->growing.len;
    }
    if (source->growing_items) {
        copy->growing_items = malloc(source->growing_cap * sizeof(*copy->growing_items));
        if (!copy->growing_items) goto fail;
        memcpy(copy->growing_items, source->growing_items,
               source->copied * sizeof(*copy->growing_items));
        copy->growing_cap = source->growing_cap;
        copy->copied = source->copied;
    }
    for (uint32_t i = 0u; i < source->variables.len; ++i)
        if (!var_id_set_add(&copy->variables, source->variables.items[i])) goto fail;
    return copy;
fail:
    match_graph_check_free(copy);
    return NULL;
}

static bool epoch_match_bind_evidence(MatchGraphCheck **check, Bindings *bindings,
        VarId target, BindingValue value, uint64_t *budget,
        BindingsReachability *verdict, bool *pending) {
    *pending = false;
    *verdict = BINDINGS_REACHABILITY_UNKNOWN;
    if (bindings->cycle_state != BINDINGS_CYCLE_ACYCLIC) return true;
    if (!atom_has_vars(value.skeleton)) {
        *verdict = BINDINGS_REACHABILITY_ABSENT;
        return true;
    }
    if (!*check) *check = match_graph_check_new(MATCH_GRAPH_OCCURS, target, value);
    if (!*check || (*check)->query != MATCH_GRAPH_OCCURS || !binding_var_eq((*check)->target, target))
        return false;
    if (!match_graph_check_step(*check, bindings, budget, verdict)) {
        *pending = (*check)->len != 0u;
        return false;
    }
    match_graph_check_free(*check);
    *check = NULL;
    return *verdict != BINDINGS_REACHABILITY_PRESENT;
}

static bool epoch_match_identity_evidence(MatchGraphCheck **check, Bindings *bindings,
        Atom *left, Atom *right, bool allow_eval_heap, bool same_environment,
        uint64_t *budget, bool *identical, bool *pending) {
    *identical = false;
    *pending = false;
    MatchReflexivityAdmission admission = match_shared_reflexivity_admit(
        left, right, allow_eval_heap, same_environment, bindings);
    if (admission == MATCH_REFLEXIVITY_DECLINED) return true;
    if (admission == MATCH_REFLEXIVITY_FINITE_AUDIT) {
        if (!*check) *check = match_graph_check_new(
            MATCH_GRAPH_FINITE, VAR_ID_NONE, binding_value_from_atom(left));
        if (!*check || (*check)->query != MATCH_GRAPH_FINITE) return false;
        BindingsReachability verdict = BINDINGS_REACHABILITY_UNKNOWN;
        if (!match_graph_check_step(*check, bindings, budget, &verdict)) {
            *pending = (*check)->len != 0u;
            return false;
        }
        match_graph_check_free(*check);
        *check = NULL;
        if (verdict != BINDINGS_REACHABILITY_ABSENT) {
            match_shared_reflexivity_decline(left);
            return false;
        }
    }
    match_shared_reflexivity_commit(left);
    *identical = true;
    return true;
}

/* The ordinary matcher keeps this state on its C stack. A cursor owns the
 * same worklist, cycle path and term transport across a finite node grant.
 * An undecided primitive retains its attempted pair but is not polled as
 * runnable work; only a budget boundary is automatically resumable. */
typedef struct {
    EpochMatchWorklist work;
    MatchPathSet path;
    AtomDeepCopySession *transport;
    MatchGraphCheck *reach;
    MatchWriteStamp attempt_start;
    EpochMatchPair unresolved;
    size_t retry_dereferences, retry_limit;
    uint32_t untracked_pairs;
    bool active;
    bool resumable;
    bool has_unresolved;
    bool track_path;
    bool attempt_repeated_var;
    bool retry_resolving_bound;
    bool retry_primitive;
} EpochMatchSuspension;

static void epoch_match_suspend(EpochMatchSuspension *saved,
        const EpochMatchWorklist *work, const MatchPathSet *path,
        AtomDeepCopySession *transport, MatchWriteStamp attempt_start,
        bool repeated, bool track_path, uint32_t untracked_pairs,
        const EpochMatchPair *unresolved, bool resumable, MatchGraphCheck *reach) {
    saved->work = *work;
    if (work->items == work->inline_items)
        saved->work.items = saved->work.inline_items;
    saved->path = *path;
    if (path->entries == path->inline_entries)
        saved->path.entries = saved->path.inline_entries;
    saved->transport = transport;
    saved->reach = reach;
    saved->attempt_start = attempt_start;
    saved->attempt_repeated_var = repeated;
    saved->track_path = track_path;
    saved->untracked_pairs = untracked_pairs;
    saved->has_unresolved = unresolved != NULL;
    if (unresolved) saved->unresolved = *unresolved;
    saved->retry_dereferences = saved->retry_limit = 0u;
    saved->retry_resolving_bound = false;
    saved->retry_primitive = false;
    saved->resumable = resumable;
    saved->active = true;
}

static void epoch_match_suspension_free(EpochMatchSuspension *saved) {
    if (!saved->active) return;
    if (saved->work.items != saved->work.inline_items) free(saved->work.items);
    match_path_free(&saved->path);
    atom_deep_copy_session_free(saved->transport);
    match_graph_check_free(saved->reach);
    saved->active = false;
}

static bool epoch_match_suspension_clone(EpochMatchSuspension *copy,
                                         const EpochMatchSuspension *source) {
    *copy = (EpochMatchSuspension){0};
    if (!source->active) return true;
    if (!source->work.items || source->work.len > source->work.cap ||
        source->work.cap > SIZE_MAX / sizeof(*source->work.items) ||
        !source->path.entries || source->path.len > source->path.entry_cap ||
        source->path.entry_cap > SIZE_MAX / sizeof(*source->path.entries) ||
        source->path.bucket_cap > SIZE_MAX / sizeof(*source->path.buckets) ||
        (source->path.bucket_cap && !source->path.buckets)) return false;
    *copy = *source;
    copy->work.items = copy->work.inline_items;
    copy->path.entries = copy->path.inline_entries;
    copy->path.buckets = NULL;
    copy->transport = NULL;
    copy->reach = NULL;
    if (source->work.items != source->work.inline_items) {
        copy->work.items = malloc(source->work.cap * sizeof(*copy->work.items));
        if (!copy->work.items) goto fail;
        memcpy(copy->work.items, source->work.items,
               source->work.len * sizeof(*copy->work.items));
    }
    if (source->path.entries != source->path.inline_entries) {
        copy->path.entries = malloc(
            source->path.entry_cap * sizeof(*copy->path.entries));
        if (!copy->path.entries) goto fail;
        memcpy(copy->path.entries, source->path.entries,
               source->path.len * sizeof(*copy->path.entries));
    }
    if (source->path.bucket_cap) {
        copy->path.buckets = malloc(
            source->path.bucket_cap * sizeof(*copy->path.buckets));
        if (!copy->path.buckets) goto fail;
        memcpy(copy->path.buckets, source->path.buckets,
               source->path.bucket_cap * sizeof(*copy->path.buckets));
    }
    if (source->transport) {
        copy->transport = atom_deep_copy_session_clone(source->transport);
        if (!copy->transport) goto fail;
    }
    if (source->reach) {
        copy->reach = match_graph_check_clone(source->reach);
        if (!copy->reach) goto fail;
    }
    return true;
fail:
    epoch_match_suspension_free(copy);
    return false;
}

static bool match_atoms_epoch_views_worklist_checked(
    Atom *left, BindingValueKind left_kind, CettaFrameIdentity left_epoch,
    uint32_t left_first_entry, Atom *right,
    Bindings *bindings, BindingsBuilder *builder,
    Arena *a, CettaFrameIdentity right_epoch, BindingValueKind right_kind,
    bool prefer_right_rule_slot,
    const BindingsActivationView *left_frame,
    const CettaOpenPatternPlan *right_plan,
    RuleFrameRegion *right_frame_region, CettaMatchCompletion *decision,
    uint64_t pair_budget, EpochMatchSuspension *suspension) {
    if (decision)
        *decision = CETTA_MATCH_INCOMPLETE;
    EpochMatchWorklist work;
    MatchPathSet path;
    Bindings *initial = builder ? &builder->current : bindings;
    const CettaFrameIdentity rule_epoch = right_frame_region &&
            right_frame_region->exclusive &&
            right_frame_region->exclusive->active
        ? right_frame_region->exclusive->epoch
        : right_frame_region && !cetta_frame_identity_is_none(right_frame_region->epoch)
            ? right_frame_region->epoch
            : right_epoch;

    if (!left || !right || !initial || !a ||
        (right_plan && right_plan->source != right) ||
        (left_kind != BINDING_VALUE_MATERIALIZED &&
         left_first_entry > initial->len))
        return false;
    if ((!suspension || !suspension->active) &&
        binding_value_kind_is_contextual(right_kind) &&
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
    MatchGraphCheck *reach = NULL;
    bool primitive_pending = false;
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
    EpochMatchPair unresolved = {0};
    bool has_unresolved = false;
    bool resume_retry = false, saved_resolving_bound = false;
    bool saved_retry_primitive = false;
    size_t saved_dereferences = 0u, saved_limit = 0u;
    if (suspension && suspension->active) {
        work = suspension->work;
        if (suspension->work.items == suspension->work.inline_items)
            work.items = work.inline_items;
        path = suspension->path;
        if (suspension->path.entries == suspension->path.inline_entries)
            path.entries = path.inline_entries;
        transport = suspension->transport;
        reach = suspension->reach;
        suspension->reach = NULL;
        saved_retry_primitive = suspension->retry_primitive;
        attempt_start = suspension->attempt_start;
        attempt_repeated_var = suspension->attempt_repeated_var;
        track_path = suspension->track_path;
        untracked_pairs = suspension->untracked_pairs;
        resume_retry = suspension->resumable && suspension->has_unresolved;
        saved_dereferences = suspension->retry_dereferences;
        saved_limit = suspension->retry_limit;
        saved_resolving_bound = suspension->retry_resolving_bound;
        suspension->active = false;
        goto continue_work;
    }
    cetta_native_cost_charge(CETTA_COST_MATCH_ATTEMPT, 1u);
    MatchTerm left_root = match_term_at(
        left, left_kind, left_epoch, left_first_entry, left_frame);
    MatchTerm right_root = match_term_at(
        right, right_kind, right_epoch, 0u, NULL);
    if (!epoch_match_push(
            &work, left_root, right_root,
            right_plan))
        goto fail;

continue_work:
    while (work.len > 0 || resume_retry) {
        bool retained_pair = resume_retry;
        resume_retry = false;
        if (!retained_pair && decision && !pair_budget) {
            if (suspension) {
                epoch_match_suspend(suspension, &work, &path, transport,
                    attempt_start, attempt_repeated_var, track_path,
                    untracked_pairs, NULL, true, reach);
                return false;
            }
            goto fail;
        }
        EpochMatchPair pair;
        if (retained_pair) {
            pair = suspension->unresolved;
        } else {
            if (decision)
                pair_budget--;
            cetta_native_cost_charge(CETTA_COST_MATCH_NODE, 1u);
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
        }
        unresolved = pair;
        has_unresolved = true;
        if (pair.exit) {
            match_path_leave(&path, pair.left, pair.right);
            continue;
        }
        BindingValue left_value = pair.left.value;
        BindingValue right_value = pair.right.value;
        left = left_value.skeleton;
        right = right_value.skeleton;
        left_kind = binding_value_kind(left_value);
        right_kind = binding_value_kind(right_value);
        bool left_original = binding_value_is_contextual(left_value);
        bool right_original = binding_value_is_contextual(right_value);
        left_epoch = binding_value_epoch(left_value);
        left_first_entry = pair.left.first_entry;
        left_frame = pair.left.frame;
        right_epoch = binding_value_epoch(right_value);
        right_plan = pair.right_plan;
        attempt_resolving_bound = retained_pair && saved_resolving_bound;
        Bindings *current = builder ? &builder->current : bindings;
        size_t dereferences = retained_pair ? saved_dereferences : 0u;
        size_t dereference_limit = retained_pair ? saved_limit : 0u;
        size_t local_dereference_allowance =
            right_frame_region && right_frame_region->exclusive &&
                    right_frame_region->exclusive->active
                ? (size_t)right_frame_region->exclusive->write_len * 2u
                : 0u;

        if (!retained_pair || saved_retry_primitive) goto retry_pair;
retry_pair_with_budget:
        if (suspension) {
            if (!pair_budget) {
                /* The pair entry is already paid. Retain its normalized
                 * coordinates and cumulative cycle guard; retrying the
                 * root would replay lookups and reset that guard. */
                EpochMatchPair pending = {
                    .left = {left_value, left_frame, left_first_entry},
                    .right = {.value = right_value},
                    .right_plan = right_plan,
                };
                pending.left.value.skeleton = left;
                pending.right.value.skeleton = right;
                epoch_match_suspend(suspension, &work, &path, transport,
                    attempt_start, attempt_repeated_var, track_path,
                    untracked_pairs, &pending, true, reach);
                suspension->retry_dereferences = dereferences;
                suspension->retry_limit = dereference_limit;
                suspension->retry_resolving_bound = attempt_resolving_bound;
                return false;
            }
            pair_budget--;
        }
retry_pair:
        if (right_plan &&
            (!right_original || right_plan->source != right ||
             right_plan->kind != right->kind)) {
            goto fail;
        }
        if (left == right) {
            bool same_environment = match_shared_occurrence_same_environment(
                left_original, right_original, left_epoch, right_epoch);
            if (decision) {
                bool identical = false;
                if (!epoch_match_identity_evidence(&reach, current,
                        left, right, right_plan == NULL, same_environment,
                        &pair_budget, &identical, &primitive_pending))
                    goto pause_primitive;
                if (identical) continue;
            } else if (match_shared_ground_reflexivity_try(
                    left, right, right_plan == NULL, same_environment, current))
                continue;
        }
        bool closed_equal = false;
        if (!decision && match_closed_expression_decision_try(
                left, right, &closed_equal)) {
            if (!closed_equal)
                goto fail;
            continue;
        }
        if (!decision && !right_plan &&
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
                    if (!bindings_dereference_step(
                            &dereferences, &dereference_limit, current,
                            local_dereference_allowance))
                        goto fail;
                    attempt_resolving_bound = true;
                    left_value = existing;
                    left = existing.skeleton;
                    left_kind = binding_value_kind(existing);
                    left_original = binding_value_is_contextual(existing);
                    left_epoch = binding_value_epoch(existing);
                    left_first_entry = 0u;
                    left_frame = NULL;
                    goto retry_pair_with_budget;
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
                        cetta_frame_identity_is_none(var_epoch_suffix(left_id))
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
                if (!bindings_dereference_step(
                            &dereferences, &dereference_limit, current,
                            local_dereference_allowance)) goto fail;
                attempt_resolving_bound = true;
                left_value = existing;
                left = existing.skeleton;
                left_kind = binding_value_kind(existing);
                left_original = binding_value_is_contextual(existing);
                left_epoch = binding_value_epoch(existing);
                left_first_entry = 0u;
                left_frame = NULL;
                goto retry_pair_with_budget;
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
                    if (!bindings_dereference_step(
                            &dereferences, &dereference_limit, current,
                            local_dereference_allowance)) goto fail;
                    attempt_resolving_bound = true;
                    right_value = right_existing;
                    right = right_existing.skeleton;
                    right_kind = binding_value_kind(right_existing);
                    right_original = binding_value_is_contextual(right_existing);
                    right_epoch = binding_value_epoch(right_existing);
                    right_plan = NULL;
                    goto retry_pair_with_budget;
                }
                if (var_id_eq(left_id, right_id)) continue;
                BindingsReachability binding_evidence = BINDINGS_REACHABILITY_UNKNOWN;
                if (suspension && !right_frame_region) {
                    bool bind_right = prefer_right_rule_slot && right_original &&
                        cetta_frame_identity_eq(right_epoch, rule_epoch);
                    BindingValue checked_value = bind_right ? left_value : right_value;
                    checked_value.skeleton = bind_right ? left : right;
                    if (!epoch_match_bind_evidence(&reach, current,
                            bind_right ? right_id : left_id, checked_value, &pair_budget,
                            &binding_evidence, &primitive_pending))
                        goto pause_primitive;
                }
                if (prefer_right_rule_slot && right_original &&
                    cetta_frame_identity_eq(right_epoch, rule_epoch)) {
                    BindingValue value = left_value;
                    value.skeleton = left;
                    bool added = builder && value.skeleton
                        ? bindings_builder_add_rule_epoch_key_fresh(
                              builder, right, right_epoch, value, a, &transport,
                              right_frame_region, NULL, binding_evidence)
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
                    left_id, atom_symbol_current_id(left),
                    left->name_key, value, binding_evidence, NULL);
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
            if (suspension && !right_frame_region &&
                !epoch_match_bind_evidence(&reach, current, left_id, value, &pair_budget,
                    &cycle_evidence, &primitive_pending))
                goto pause_primitive;
            bool added = false;
            if (builder &&
                cycle_evidence != BINDINGS_REACHABILITY_UNKNOWN) {
                added =
                    bindings_match_store_value_in_rule_region(
                        right_frame_region,
                        NULL, builder, a, &transport,
                        MATCH_BIND_CONTEXT_STORED_EQUATION, left_original,
                        left_id, atom_symbol_current_id(left),
                        left->name_key, value, cycle_evidence, NULL);
            } else {
                added = bindings_match_store_value_in_rule_region(
                    right_frame_region,
                    bindings, builder, a, &transport,
                    MATCH_BIND_CONTEXT_STORED_EQUATION, left_original,
                    left_id, atom_symbol_current_id(left),
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
                if (!bindings_dereference_step(
                            &dereferences, &dereference_limit, current,
                            local_dereference_allowance)) goto fail;
                attempt_resolving_bound = true;
                right_value = existing;
                right = existing.skeleton;
                right_kind = binding_value_kind(existing);
                right_original = binding_value_is_contextual(existing);
                right_epoch = binding_value_epoch(existing);
                right_plan = NULL;
                goto retry_pair_with_budget;
            }
            BindingValue binding_value = left_value;
            binding_value.skeleton = left;
            BindingsReachability binding_evidence = BINDINGS_REACHABILITY_UNKNOWN;
            if (suspension && !right_frame_region &&
                !epoch_match_bind_evidence(&reach, current, right_id, binding_value, &pair_budget,
                    &binding_evidence, &primitive_pending))
                goto pause_primitive;
            bool added = false;
            if (builder && right_original && cetta_frame_identity_eq(right_epoch, rule_epoch) &&
                (prefer_right_rule_slot || !right->name_key)) {
                added = bindings_builder_add_rule_epoch_key_fresh(
                    builder, right, right_epoch, binding_value, a, &transport,
                    right_frame_region, NULL, binding_evidence);
            } else {
                added = bindings_match_store_value_in_rule_region(
                    right_frame_region,
                    bindings, builder, a, &transport,
                    MATCH_BIND_CONTEXT_ACTIVATION_SOURCE, right_original,
                    right_id, atom_symbol_current_id(right),
                    right->name_key, binding_value, binding_evidence, NULL);
            }
            if (!added) goto fail;
            continue;
        }
        if (left->kind == ATOM_SYMBOL && right->kind == ATOM_SYMBOL) {
            if (!atom_symbols_equal(left, right)) {
                if (attempt_resolving_bound)
                    attempt_repeated_var = true;
                goto mismatch;
            }
            continue;
        }
        if (left->kind == ATOM_GROUNDED && right->kind == ATOM_GROUNDED) {
            if (!atom_eq_with_resolved_spaces(left, right)) {
                if (attempt_resolving_bound)
                    attempt_repeated_var = true;
                goto mismatch;
            }
            continue;
        }
        if (!atom_petta_decomposition_compatible(left, right))
            goto mismatch;
        if (left->kind == ATOM_EXPR && right->kind == ATOM_EXPR &&
            match_expr_is_braces(left) != match_expr_is_braces(right)) {
            if (attempt_resolving_bound)
                attempt_repeated_var = true;
            goto fail;
        }
        if (left->kind == ATOM_EXPR && right->kind == ATOM_EXPR &&
            (match_expr_is_list(left) || match_expr_is_list(right))) {
            MatchListMeeting meet;
            MatchListOutcome list_outcome = match_list_meet(a, left, right, &meet);
            if (list_outcome == MATCH_LIST_MISMATCH) {
                if (attempt_resolving_bound)
                    attempt_repeated_var = true;
                goto mismatch;
            }
            if (list_outcome != MATCH_LIST_ELEMENTS ||
                (right_plan &&
                 (right_plan->child_count != right->expr.len ||
                  !right_plan->children))) {
                if (attempt_resolving_bound)
                    attempt_repeated_var = true;
                goto fail;
            }
            if (!track_path &&
                ++untracked_pairs > MATCH_UNTRACKED_PAIR_LIMIT)
                track_path = true;
            MatchTerm left_term = {
                .value = left_value,
                .frame = left_frame,
                .first_entry = left_first_entry,
            };
            MatchTerm right_term = {.value = right_value};
            left_term.value.skeleton = left;
            right_term.value.skeleton = right;
            if (!right_plan && track_path &&
                (!match_path_enter(&path, left_term, right_term) ||
                 !epoch_match_push_exit(&work, left_term, right_term)))
                goto fail;
            /* The rest meets last, after the elements, left to right. */
            if (meet.left_rest) {
                MatchTerm left_rest = left_term, right_rest = right_term;
                left_rest.value = meet.left_derived
                    ? binding_value_with_derived_skeleton(left_term.value, meet.left_rest)
                    : binding_value_with_skeleton(left_value, meet.left_rest);
                right_rest.value = meet.right_derived
                    ? binding_value_with_derived_skeleton(right_term.value, meet.right_rest)
                    : binding_value_with_skeleton(right_value, meet.right_rest);
                if (!epoch_match_push(
                        &work, left_rest, right_rest,
                        right_plan && !meet.right_derived
                            ? &right_plan->children[right->expr.len - 1u]
                            : NULL))
                    goto fail;
            }
            if (meet.prefix > 0u &&
                !epoch_match_push_children(
                    &work, left_term, right_term, right_plan, 1u,
                    1u + meet.prefix))
                goto fail;
            continue;
        }
        if (left->kind != ATOM_EXPR || right->kind != ATOM_EXPR ||
            atom_authored_len(left) != atom_authored_len(right) ||
            (right_plan &&
             (right_plan->child_count != right->expr.len ||
              (right->expr.len != 0u && !right_plan->children)))) {
            if (attempt_resolving_bound)
                attempt_repeated_var = true;
            if (left->kind != ATOM_EXPR || right->kind != ATOM_EXPR ||
                left->expr.len != right->expr.len)
                goto mismatch;
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
            CettaExprIndex nch = atom_authored_len(left);
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
            if (suspension) {
                left_value.skeleton = next_left;
                right_value.skeleton = next_right;
                if (!epoch_match_push(&work,
                        (MatchTerm){left_value, left_frame, left_first_entry},
                        (MatchTerm){.value = right_value}, next_plan))
                    goto fail;
                continue;
            }
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_MATCH_WORKLIST_FRAME_ELIDED);
            left = next_left;
            right = next_right;
            left_value.skeleton = next_left;
            right_value.skeleton = next_right;
            right_plan = next_plan;
            dereferences = 0u;
            dereference_limit = 0u;
            goto retry_pair;
        }
        }
        continue;
pause_primitive:
        if (!suspension || !primitive_pending) goto fail;
        {
            EpochMatchPair pending = {
                .left = {left_value, left_frame, left_first_entry},
                .right = {.value = right_value},
                .right_plan = right_plan,
            };
            pending.left.value.skeleton = left;
            pending.right.value.skeleton = right;
            epoch_match_suspend(suspension, &work, &path, transport,
                attempt_start, attempt_repeated_var, track_path,
                untracked_pairs, &pending, true, reach);
            suspension->retry_dereferences = dereferences;
            suspension->retry_limit = dereference_limit;
            suspension->retry_resolving_bound = attempt_resolving_bound;
            suspension->retry_primitive = true;
            return false;
        }
    }
    if (reach) match_graph_check_free(reach);
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
    if (decision)
        *decision = CETTA_MATCH_MATCHED;
    return true;

mismatch:
    if (decision)
        *decision = CETTA_MATCH_REFUTED;
fail:
    if (suspension && decision && *decision == CETTA_MATCH_INCOMPLETE) {
        epoch_match_suspend(suspension, &work, &path, transport,
            attempt_start, attempt_repeated_var, track_path, untracked_pairs,
            has_unresolved ? &unresolved : NULL, false, reach);
        return false;
    }
    if (reach) match_graph_check_free(reach);
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

static bool match_atoms_epoch_views_worklist(
    Atom *left, BindingValueKind left_kind, CettaFrameIdentity left_epoch,
    uint32_t left_first_entry, Atom *right,
    Bindings *bindings, BindingsBuilder *builder,
    Arena *a, CettaFrameIdentity right_epoch, BindingValueKind right_kind,
    bool prefer_right_rule_slot,
    const BindingsActivationView *left_frame,
    const CettaOpenPatternPlan *right_plan,
    RuleFrameRegion *right_frame_region) {
    return match_atoms_epoch_views_worklist_checked(left, left_kind, left_epoch,
        left_first_entry, right, bindings, builder, a, right_epoch, right_kind,
        prefer_right_rule_slot, left_frame, right_plan, right_frame_region,
        NULL, UINT64_MAX, NULL);
}

CettaMatchCompletion match_atoms_epoch_builder_checked(
        Atom *left, Atom *right, BindingsBuilder *builder, Arena *arena,
        CettaFrameIdentity epoch, uint64_t pair_budget) {
    CettaMatchCompletion decision = CETTA_MATCH_INCOMPLETE;
    (void)match_atoms_epoch_views_worklist_checked(
        left, BINDING_VALUE_MATERIALIZED, CETTA_FRAME_IDENTITY_NONE, 0u,
        right, NULL, builder, arena, epoch, BINDING_VALUE_CONTEXTUAL,
        false, NULL, NULL, NULL, &decision, pair_budget, NULL);
    return decision;
}

CettaMatchCompletion match_atoms_builder_checked(
        Atom *left, Atom *right, BindingsBuilder *builder, Arena *arena,
        uint64_t pair_budget) {
    CettaMatchCompletion decision = CETTA_MATCH_INCOMPLETE;
    (void)match_atoms_epoch_views_worklist_checked(
        left, BINDING_VALUE_MATERIALIZED, CETTA_FRAME_IDENTITY_NONE, 0u,
        right, NULL, builder, arena, CETTA_FRAME_IDENTITY_NONE,
        BINDING_VALUE_MATERIALIZED, false, NULL, NULL, NULL,
        &decision, pair_budget, NULL);
    return decision;
}

struct CettaMatchCursor {
    Atom *left;
    Atom *right;
    Arena *arena;
    BindingsBuilder builder;
    EpochMatchSuspension saved;
    CettaFrameIdentityScope identities;
    CettaFrameIdentity epoch;
    CettaMatchCursorStep result;
    int support_invalidation;
    bool started;
    bool taken;
};

CettaMatchCursor *match_atoms_cursor_new(Atom *left, Atom *right,
        const Bindings *seed, Arena *arena) {
    if (!left || !right || !arena) return NULL;
    CettaMatchCursor *cursor = cetta_malloc(sizeof(*cursor));
    if (!cursor) return NULL;
    memset(cursor, 0, sizeof(*cursor));
    cursor->left = left;
    cursor->right = right;
    cursor->arena = arena;
    Bindings unit;
    bindings_init(&unit);
    bool ready = bindings_builder_init(&cursor->builder, seed ? seed : &unit);
    bindings_free(&unit);
    if (!ready) { bindings_builder_free(&cursor->builder); free(cursor); return NULL; }
#if !defined(CETTA_MUTATION_BINDINGS_SINGLE_REACH_KEEP_ROLLBACK_CACHE)
    bindings_single_reach_configure_invalidation();
    cursor->support_invalidation=g_bindings_single_reach_support_invalidation_enabled;
#endif
    return cursor;
}

/* The retained index can arrive on a thread which never allocated an index.
 * Its optimization profile belongs to the captured matcher, not that cold
 * thread's uninitialized TLS cache or a newly read environment setting. */
static int match_cursor_enter(const CettaMatchCursor *cursor) {
#if !defined(CETTA_MUTATION_BINDINGS_SINGLE_REACH_KEEP_ROLLBACK_CACHE)
    int previous=g_bindings_single_reach_support_invalidation_enabled;
    g_bindings_single_reach_support_invalidation_enabled=cursor->support_invalidation;
    return previous;
#else
    (void)cursor;return 0;
#endif
}

static void match_cursor_leave(int previous) {
#if !defined(CETTA_MUTATION_BINDINGS_SINGLE_REACH_KEEP_ROLLBACK_CACHE)
    g_bindings_single_reach_support_invalidation_enabled=previous;
#else
    (void)previous;
#endif
}

CettaMatchCursor *match_atoms_cursor_clone(const CettaMatchCursor *source) {
    if (!source || source->identities.len > source->identities.cap ||
        (source->identities.len && !source->identities.identities)) return NULL;
    CettaMatchCursor *copy = calloc(1u, sizeof(*copy));
    if (!copy) return NULL;
    copy->left = source->left;
    copy->right = source->right;
    copy->arena = source->arena;
    copy->epoch = source->epoch;
    copy->result = source->result;
    copy->support_invalidation = source->support_invalidation;
    copy->started = source->started;
    copy->taken = source->taken;
    int previous = match_cursor_enter(source);
    bool ready = bindings_builder_clone(&copy->builder, &source->builder);
    match_cursor_leave(previous);
    if (!ready) { free(copy); return NULL; }
    for (uint32_t i = 0u; i < source->identities.len; i++) {
        if (!cetta_frame_identity_scope_retain(
                &copy->identities, source->identities.identities[i])) {
            match_atoms_cursor_free(copy);
            return NULL;
        }
    }
    if (!epoch_match_suspension_clone(&copy->saved, &source->saved)) {
        match_atoms_cursor_free(copy);
        return NULL;
    }
    return copy;
}

static bool match_cursor_copy_term(MatchTerm *term,
                                   AtomDeepCopySession *session) {
    if (term->frame) return false;
    if (!term->value.skeleton) return true;
    Atom *copy = atom_deep_copy_session_copy(session, term->value.skeleton);
    if (!copy) return false;
    term->value.skeleton = copy;
    return true;
}

static bool match_cursor_copy_pair(EpochMatchPair *pair,
                                   AtomDeepCopySession *session) {
    return !pair->right_plan &&
        match_cursor_copy_term(&pair->left, session) &&
        match_cursor_copy_term(&pair->right, session);
}

/* During migration, old row coordinates are the retained scan. They are not
 * queried as a hash table again. Keep those coordinates while relocating the
 * keys; the destination memo is rehashed before further insertion. */
static bool match_cursor_copy_graph(BindingApplyGraph *graph,
        AtomDeepCopySession *session, bool retain_rows) {
    if (!graph->cap) return true;
    if (retain_rows) {
        for (size_t i = 0u; i < graph->cap; ++i) {
            if (!graph->entries[i].source) continue;
            Atom *atom = atom_deep_copy_session_copy(session, graph->entries[i].source);
            if (!atom) return false;
            graph->entries[i].source = atom;
        }
        return true;
    }
    BindingApplyGraph relocated = {.cap = graph->cap, .len = graph->len};
    relocated.entries = calloc(relocated.cap, sizeof(*relocated.entries));
    if (!relocated.entries) return false;
    for (size_t i = 0u; i < graph->cap; ++i) {
        BindingApplyGraphEntry entry = graph->entries[i];
        if (!entry.source) continue;
        entry.source = atom_deep_copy_session_copy(session, entry.source);
        if (!entry.source) { free(relocated.entries); return false; }
        *bindings_apply_graph_slot(&relocated, entry.source,
            (BindingsApplyScope){.epoch = entry.epoch, .kind = entry.kind},
            entry.code_depth) = entry;
    }
    free(graph->entries);
    *graph = relocated;
    return true;
}

static bool match_cursor_copy_reach(MatchGraphCheck *check,
                                    AtomDeepCopySession *session) {
    if (!check) return true;
    for (size_t i = 0u; i < check->len; ++i) {
        Atom *atom = atom_deep_copy_session_copy(session, check->items[i].value.skeleton);
        if (!atom) return false;
        check->items[i].value.skeleton = atom;
    }
    for (size_t i = 0u; i < check->copied; ++i)
        check->growing_items[i] = check->items[i];
    if (!match_cursor_copy_graph(&check->graph, session, check->growing.cap != 0u))
        return false;
    /* Clearing has initialized only a prefix; it contains no keys yet. */
    if (check->growing.cap && check->memo_phase != MATCH_GRAPH_MEMO_CLEAR &&
            !match_cursor_copy_graph(&check->growing, session, false)) return false;
    if (check->memo_phase == MATCH_GRAPH_MEMO_MOVE) {
        BindingApplyGraphEntry *entry = &check->migrating;
        entry->source = atom_deep_copy_session_copy(session, entry->source);
        if (!entry->source) return false;
        check->probe = bindings_apply_graph_hash(entry->source,
            entry->epoch, entry->kind, entry->code_depth) & (check->growing.cap - 1u);
    } else if (check->memo_phase == MATCH_GRAPH_MEMO_LOOKUP) {
        BindingValue value = check->items[check->len - 1u].value;
        check->probe = bindings_apply_graph_hash(value.skeleton,
            binding_value_epoch(value), binding_value_kind(value), 0u) & (check->graph.cap - 1u);
    }
    return true;
}

CettaMatchCursor *match_atoms_cursor_copy_to_arena(
        const CettaMatchCursor *source, Arena *destination,
        AtomDeepCopySession *session) {
    if (!source || !atom_deep_copy_session_relocation_targets(session, destination) ||
        !atom_deep_copy_session_maps_arena(session, source->arena, destination))
        return NULL;
    CettaMatchCursor *copy = match_atoms_cursor_clone(source);
    if (!copy) return NULL;
    int previous = match_cursor_enter(source);
    EpochMatchSuspension *saved = &copy->saved;
    if (saved->active && saved->transport) {
        AtomDeepCopySession *transport = atom_deep_copy_session_relocate(
            saved->transport, session);
        if (!transport) goto fail;
        atom_deep_copy_session_free(saved->transport);
        saved->transport = transport;
    }
    copy->left = atom_deep_copy_session_copy(session, copy->left);
    copy->right = atom_deep_copy_session_copy(session, copy->right);
    if (!copy->left || !copy->right ||
        !bindings_builder_copy_owned_atoms_with_session(
            &copy->builder, destination, session)) goto fail;
    if (saved->active) {
        if (!match_cursor_copy_reach(saved->reach, session)) goto fail;
        for (size_t i = 0u; i < saved->work.len; i++)
            if (!match_cursor_copy_pair(&saved->work.items[i], session)) goto fail;
        if (saved->has_unresolved &&
            !match_cursor_copy_pair(&saved->unresolved, session)) goto fail;
        if (saved->path.bucket_cap)
            memset(saved->path.buckets, 0,
                   saved->path.bucket_cap * sizeof(*saved->path.buckets));
        for (size_t i = 0u; i < saved->path.len; i++) {
            MatchPathEntry *entry = &saved->path.entries[i];
            if (!match_cursor_copy_term(&entry->left, session) ||
                !match_cursor_copy_term(&entry->right, session)) goto fail;
            if (saved->path.bucket_cap) {
                size_t bucket = match_path_hash(entry->left, entry->right) &
                    (saved->path.bucket_cap - 1u);
                entry->previous = saved->path.buckets[bucket];
                saved->path.buckets[bucket] = (uint32_t)i + 1u;
            }
        }
    }
    copy->arena = destination;
    match_cursor_leave(previous);
    return copy;
fail:
    match_cursor_leave(previous);
    match_atoms_cursor_free(copy);
    return NULL;
}

CettaMatchCursorStep match_atoms_cursor_step(CettaMatchCursor *cursor,
                                             uint64_t node_budget) {
    if (!cursor) return CETTA_MATCH_CURSOR_CAPACITY;
    if (cursor->result != CETTA_MATCH_CURSOR_PROGRESS || !node_budget)
        return cursor->result;
    if (!cursor->started) {
        if (!cetta_frame_identity_scope_try(&cursor->identities, &cursor->epoch)) {
            cursor->result = CETTA_MATCH_CURSOR_CAPACITY;
            return cursor->result;
        }
        cursor->started = true;
    }
    int previous=match_cursor_enter(cursor);
    CettaMatchCompletion decision = CETTA_MATCH_INCOMPLETE;
    (void)match_atoms_epoch_views_worklist_checked(
        cursor->left, BINDING_VALUE_MATERIALIZED, CETTA_FRAME_IDENTITY_NONE, 0u,
        cursor->right,
        NULL, &cursor->builder, cursor->arena, cursor->epoch,
        BINDING_VALUE_CONTEXTUAL, false, NULL, NULL, NULL,
        &decision, node_budget, &cursor->saved);
    match_cursor_leave(previous);
    if (decision == CETTA_MATCH_MATCHED) cursor->result = CETTA_MATCH_CURSOR_MATCHED;
    else if (decision == CETTA_MATCH_REFUTED) cursor->result = CETTA_MATCH_CURSOR_REFUTED;
    else if (!cursor->saved.active || !cursor->saved.resumable)
        cursor->result = CETTA_MATCH_CURSOR_INCOMPLETE;
    return cursor->result;
}

CettaFrameIdentity match_atoms_cursor_epoch(const CettaMatchCursor *cursor) {
    return cursor && cursor->started ? cursor->epoch : CETTA_FRAME_IDENTITY_NONE;
}

#ifdef CETTA_TEST_HOOKS
bool match_atoms_cursor_test_graph(const CettaMatchCursor *cursor,
        CettaMatchGraphTestState *state) {
    const MatchGraphCheck *check = cursor && cursor->saved.active
        ? cursor->saved.reach : NULL;
    if (!check || !state) return false;
    *state = (CettaMatchGraphTestState){
        .phase = check->memo_phase, .capacity = check->graph.cap,
        .next_capacity = check->growing.cap, .cleared = check->cleared,
        .scanned = check->scanned, .probe = check->probe,
        .stack_length = check->len, .stack_capacity = check->cap,
        .next_stack_capacity = check->growing_cap, .copied = check->copied};
    return true;
}

size_t match_atoms_cursor_test_bucket(Atom *atom, size_t mask) {
    return bindings_apply_graph_hash(atom, CETTA_FRAME_IDENTITY_NONE, BINDING_VALUE_MATERIALIZED, 0u) & mask;
}
#endif

bool match_atoms_cursor_take(CettaMatchCursor *cursor, Bindings *out) {
    if (!cursor || !out || cursor->taken || cursor->result != CETTA_MATCH_CURSOR_MATCHED)
        return false;
    int previous=match_cursor_enter(cursor);
    bindings_builder_take(&cursor->builder, out);
    match_cursor_leave(previous);
    cursor->taken = true;
    return true;
}

void match_atoms_cursor_free(CettaMatchCursor *cursor) {
    if (!cursor) return;
    int previous=match_cursor_enter(cursor);
    epoch_match_suspension_free(&cursor->saved);
    bindings_builder_free(&cursor->builder);
    cetta_frame_identity_scope_clear(&cursor->identities);
    match_cursor_leave(previous);
    free(cursor);
}

void match_atoms_cursor_storage_census(const CettaMatchCursor *cursor,
        CettaStorageCensus *census) {
    if (!cursor || !cetta_storage_object(census,cursor,sizeof(*cursor),CETTA_STORAGE_BLOCK))
        return;
    arena_storage_census(cursor->arena,census);
    bindings_builder_storage_census(&cursor->builder,census);
    if (cursor->identities.identities != cursor->identities.inline_identities)
        cetta_storage_array(census,cursor->identities.identities,cursor->identities.cap,
            sizeof(*cursor->identities.identities),0u);
    const EpochMatchSuspension *saved = &cursor->saved;
    if (!saved->active) return;
    if (saved->work.items != saved->work.inline_items)
        cetta_storage_array(census,saved->work.items,saved->work.cap,
            sizeof(*saved->work.items),0u);
    if (saved->path.entries != saved->path.inline_entries)
        cetta_storage_array(census,saved->path.entries,saved->path.entry_cap,
            sizeof(*saved->path.entries),0u);
    cetta_storage_array(census,saved->path.buckets,saved->path.bucket_cap,
        sizeof(*saved->path.buckets),0u);
    atom_deep_copy_session_storage_census(saved->transport,census);
    const MatchGraphCheck *reach = saved->reach;
    if (reach && cetta_storage_object(census, reach, sizeof(*reach), CETTA_STORAGE_BLOCK)) {
        cetta_storage_array(census, reach->items, reach->cap, sizeof(*reach->items), 0u);
        cetta_storage_array(census, reach->graph.entries, reach->graph.cap,
            sizeof(*reach->graph.entries), 0u);
        cetta_storage_array(census, reach->growing.entries, reach->growing.cap,
            sizeof(*reach->growing.entries), 0u);
        cetta_storage_array(census, reach->growing_items, reach->growing_cap,
            sizeof(*reach->growing_items), 0u);
        if (reach->variables.items != reach->variables.inline_items)
            cetta_storage_array(census, reach->variables.items, reach->variables.cap,
                sizeof(*reach->variables.items), 0u);
        cetta_storage_array(census, reach->variables.slots, reach->variables.slot_cap,
            sizeof(*reach->variables.slots), 0u);
    }
}


/* Execute the immutable rule-pattern plan as one contiguous preorder
 * program.  Dynamic query observations and all binding writes remain the
 * authoritative matcher's work.  A bound rule variable hands that one pair
 * to the unchanged generic matcher; a variable matched against an unentered
 * rigid subtree advances by the subtree's exact compiled span. */
static bool match_atoms_epoch_views_linear(
    Atom *left, BindingValueKind left_kind, CettaFrameIdentity left_epoch,
    uint32_t left_first_entry, Atom *right,
    Bindings *bindings, BindingsBuilder *builder,
    Arena *a, CettaFrameIdentity right_epoch, BindingValueKind right_kind,
    bool prefer_right_rule_slot,
    const BindingsActivationView *left_frame,
    const CettaOpenPatternPlan *right_plan,
    BindingsExclusiveFrame *exclusive) {
    cetta_native_cost_charge(CETTA_COST_MATCH_ATTEMPT, 1u);
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
        cetta_native_cost_charge(CETTA_COST_MATCH_NODE, 1u);
        OpenPatternProgramItem item = stack.items[--stack.len];
        left = item.left.value.skeleton;
        left_kind = binding_value_kind(item.left.value);
        bool left_original = binding_value_is_contextual(item.left.value);
        left_epoch = binding_value_epoch(item.left.value);
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
        size_t dereference_limit = 0u;
        size_t local_dereference_allowance =
            exclusive && exclusive->active
                ? (size_t)exclusive->write_len * 2u : 0u;

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
                    if (!bindings_dereference_step(
                        &dereferences, &dereference_limit, current,
                        local_dereference_allowance))
                        goto fail;
                    left = existing.skeleton;
                    left_kind = binding_value_kind(existing);
                    left_original = binding_value_is_contextual(existing);
                    left_epoch = binding_value_epoch(existing);
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
                        cetta_frame_identity_is_none(var_epoch_suffix(left_id))
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
                if (!bindings_dereference_step(
                        &dereferences, &dereference_limit, current,
                        local_dereference_allowance))
                    goto fail;
                left = existing.skeleton;
                left_kind = binding_value_kind(existing);
                left_original = binding_value_is_contextual(existing);
                left_epoch = binding_value_epoch(existing);
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
                            a, binding_value_epoch(right_existing), binding_value_kind(right_existing),
                            prefer_right_rule_slot,
                            left_frame, NULL, &frame_region)) {
                        attempt_repeated_var = true;
                        goto fail;
                    }
                    continue;
                }
                if (var_id_eq(left_id, right_id))
                    continue;
                if (prefer_right_rule_slot) {
                    bool added = builder &&
                        bindings_builder_add_rule_epoch_key_fresh(
                            builder, right, right_epoch,
                            binding_value_from_context_kind(
                                left, left_epoch, left_kind),
                            a, &transport,
                            &frame_region, NULL, BINDINGS_REACHABILITY_UNKNOWN);
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
                    left_id, atom_symbol_current_id(left),
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
                        left_id, atom_symbol_current_id(left),
                        left->name_key, value, cycle_evidence, NULL);
            } else {
                added = bindings_match_store_value_in_rule_region(
                    &frame_region,
                    bindings, builder, a, &transport,
                    MATCH_BIND_CONTEXT_STORED_EQUATION, left_original,
                    left_id, atom_symbol_current_id(left),
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
                        a, binding_value_epoch(existing), binding_value_kind(existing),
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
                    &frame_region, NULL, BINDINGS_REACHABILITY_UNKNOWN);
            } else {
                added = bindings_match_store_value_in_rule_region(
                    &frame_region,
                    bindings, builder, a, &transport,
                    MATCH_BIND_CONTEXT_ACTIVATION_SOURCE, true,
                    right_id, atom_symbol_current_id(right),
                    right->name_key, binding_value, BINDINGS_REACHABILITY_UNKNOWN, NULL);
            }
            if (!added)
                goto fail;
            continue;
        }
        if (left->kind == ATOM_SYMBOL && right->kind == ATOM_SYMBOL) {
            if (!atom_symbols_equal(left, right))
                goto fail;
            continue;
        }
        if (left->kind == ATOM_GROUNDED &&
            right->kind == ATOM_GROUNDED) {
            if (!atom_eq_with_resolved_spaces(left, right))
                goto fail;
            continue;
        }
        if (!atom_petta_decomposition_compatible(left, right))
            goto fail;
        /* Rule patterns holding a list compile no program (petta_program.c),
         * so here a list meets only an expression, which it never matches. */
        if (left->kind != ATOM_EXPR ||
            right->kind != ATOM_EXPR ||
            atom_authored_len(left) != atom_authored_len(right) ||
            match_expr_is_list(left) || match_expr_is_list(right) ||
            match_expr_is_braces(left) != match_expr_is_braces(right) ||
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
             child < atom_authored_len(right); child++) {
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
                                       Arena *a, CettaFrameIdentity epoch) {
    return match_atoms_epoch_views_worklist(
        left, BINDING_VALUE_MATERIALIZED, CETTA_FRAME_IDENTITY_NONE, 0u,
        right, bindings, builder, a, epoch,
        BINDING_VALUE_CONTEXTUAL, false, NULL, NULL, NULL);
}

static bool match_atoms_epoch_view_worklist(
        Atom *left, CettaFrameIdentity left_epoch, uint32_t left_first_entry,
        Atom *right, Bindings *bindings, BindingsBuilder *builder,
        Arena *a, CettaFrameIdentity right_epoch) {
    return match_atoms_epoch_views_worklist(
        left, BINDING_VALUE_CONTEXTUAL, left_epoch, left_first_entry,
        right, bindings, builder, a, right_epoch,
        BINDING_VALUE_CONTEXTUAL, false, NULL, NULL, NULL);
}

static bool match_atoms_epoch_view_current_worklist(
        Atom *left, CettaFrameIdentity left_epoch, uint32_t left_first_entry,
        Atom *right, Bindings *bindings, BindingsBuilder *builder,
        Arena *a) {
    return match_atoms_epoch_views_worklist(
        left, BINDING_VALUE_CONTEXTUAL, left_epoch, left_first_entry,
        right, bindings, builder, a, CETTA_FRAME_IDENTITY_NONE,
        BINDING_VALUE_MATERIALIZED, false, NULL, NULL, NULL);
}

static bool match_atoms_epoch_view_rule_local_worklist(
        Atom *left, CettaFrameIdentity left_epoch, uint32_t left_first_entry,
        Atom *right, BindingsBuilder *builder, Arena *a,
        CettaFrameIdentity right_epoch) {
    return match_atoms_epoch_views_worklist(
        left, BINDING_VALUE_CONTEXTUAL, left_epoch, left_first_entry,
        right, NULL, builder, a, right_epoch,
        BINDING_VALUE_CONTEXTUAL, true, NULL, NULL, NULL);
}

static bool match_atoms_epoch_view_rule_local_planned_worklist(
        Atom *left, CettaFrameIdentity left_epoch, uint32_t left_first_entry,
        Atom *right, const CettaOpenPatternPlan *right_plan,
        BindingsBuilder *builder, Arena *a, CettaFrameIdentity right_epoch) {
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
    Arena *a, CettaFrameIdentity right_epoch, bool right_original,
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
        Arena *a, CettaFrameIdentity epoch) {
    return match_atoms_epoch_views_worklist(
        left, BINDING_VALUE_MATERIALIZED, CETTA_FRAME_IDENTITY_NONE, 0u,
        right, NULL, builder, a, epoch,
        BINDING_VALUE_CONTEXTUAL, true, NULL, NULL, NULL);
}

static bool match_atoms_epoch_rule_local_planned_worklist(
        Atom *left, Atom *right,
        const CettaOpenPatternPlan *right_plan,
        BindingsBuilder *builder, Arena *a, CettaFrameIdentity epoch) {
    RuleFrameRegion frame_region;
    rule_frame_region_init(&frame_region, right_plan);
    return match_atoms_epoch_views_worklist(
        left, BINDING_VALUE_MATERIALIZED, CETTA_FRAME_IDENTITY_NONE, 0u,
        right, NULL, builder, a, epoch,
        BINDING_VALUE_CONTEXTUAL, true, NULL, right_plan,
        right_plan ? &frame_region : NULL);
}

bool match_binding_value_epoch_builder_rule_local(
        BindingValue left, Atom *right, BindingsBuilder *bb,
        Arena *a, CettaFrameIdentity right_epoch) {
    return left.skeleton && right && bb && a && !cetta_frame_identity_is_none(right_epoch) &&
        match_atoms_epoch_views_worklist(
            left.skeleton, binding_value_kind(left), binding_value_epoch(left), 0u,
            right, NULL, bb, a, right_epoch,
            BINDING_VALUE_CONTEXTUAL, true, NULL, NULL, NULL);
}

bool match_binding_value_epoch_builder_rule_local_planned(
        BindingValue left, Atom *right,
        const CettaOpenPatternPlan *right_plan,
        BindingsBuilder *bb, Arena *a, CettaFrameIdentity right_epoch) {
    RuleFrameRegion frame_region;
    rule_frame_region_init(&frame_region, right_plan);
    return left.skeleton && right && bb && a && !cetta_frame_identity_is_none(right_epoch) &&
        match_atoms_epoch_views_worklist(
            left.skeleton, binding_value_kind(left), binding_value_epoch(left), 0u,
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
        BindingsBuilder *bb, Arena *a, CettaFrameIdentity right_epoch,
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
        bb, right, right_epoch, left, a, &transport, &region, NULL, BINDINGS_REACHABILITY_UNKNOWN);
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
        BindingsBuilder *bb, Arena *a, CettaFrameIdentity right_epoch,
        BindingsExclusiveFrame *exclusive, bool linear) {
    if (!left.skeleton || !right || !bb || !a || cetta_frame_identity_is_none(right_epoch) ||
        !binding_value_kind_is_contextual(right_kind) ||
        !exclusive || !exclusive->active ||
        !cetta_frame_identity_eq(exclusive->epoch, right_epoch))
        return false;
    MatchBareVariableStep bare = match_exclusive_bare_variable(
        left, right, right_plan, bb, a, right_epoch, exclusive);
    if (bare != MATCH_BARE_VARIABLE_DECLINE)
        return bare == MATCH_BARE_VARIABLE_BOUND;
    if (linear && right_plan) {
        return match_atoms_epoch_views_linear(
            left.skeleton, binding_value_kind(left), binding_value_epoch(left), 0u,
            right, NULL, bb, a, right_epoch,
            right_kind, true, NULL,
            right_plan, exclusive);
    }
    RuleFrameRegion frame_region;
    rule_frame_region_init_exclusive(
        &frame_region, right_plan, exclusive);
    return match_atoms_epoch_views_worklist(
        left.skeleton, binding_value_kind(left), binding_value_epoch(left), 0u,
        right, NULL, bb, a, right_epoch,
        right_kind, true, NULL, right_plan,
        &frame_region);
}

bool match_atoms_epoch_builder_rule_local_in_exclusive_frame(
        Atom *left, Atom *right, BindingValueKind right_kind,
        const CettaOpenPatternPlan *right_plan,
        BindingsBuilder *bb, Arena *a, CettaFrameIdentity epoch,
        BindingsExclusiveFrame *exclusive, bool linear) {
    return match_binding_value_epoch_builder_rule_local_in_exclusive_frame(
        binding_value_from_atom(left), right, right_kind, right_plan,
        bb, a, epoch, exclusive, linear);
}

bool match_atoms_epoch_view_builder_rule_local_in_exclusive_frame(
        Atom *left, CettaFrameIdentity left_epoch, uint32_t left_first_entry,
        Atom *right, BindingValueKind right_kind,
        const CettaOpenPatternPlan *right_plan,
        BindingsBuilder *bb, Arena *a, CettaFrameIdentity right_epoch,
        BindingsExclusiveFrame *exclusive, bool linear) {
    if (!left || !right || !bb || !a || cetta_frame_identity_is_none(right_epoch) ||
        !binding_value_kind_is_contextual(right_kind) ||
        !exclusive || !exclusive->active ||
        !cetta_frame_identity_eq(exclusive->epoch, right_epoch))
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
        BindingsBuilder *bb, Arena *a, CettaFrameIdentity right_epoch,
        BindingsExclusiveFrame *exclusive, bool linear) {
    if (!left || !left_frame || !right || !bb || !a ||
        !binding_value_kind_is_contextual(right_kind) ||
        !exclusive || !exclusive->active ||
        !cetta_frame_identity_eq(exclusive->epoch, right_epoch) ||
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

typedef struct {
    const Atom *left;
    AtomId right;
} StoredGroundPair;

/* Scoped to one synchronous closed-ground comparison and one TermUniverse.
 * No variable binding, source epoch or mutable resource can be memoized here.
 * Repeated graph pairs skip construction work, not stored row occurrences. */
typedef struct {
    StoredGroundPair local[64];
    StoredGroundPair *slots;
    size_t capacity, used;
    unsigned warmup;
} StoredGroundPairs;

static size_t stored_ground_pair_hash(const Atom *left, AtomId right) {
    uint64_t x = ((uint64_t)(uintptr_t)left >> 4u) ^
                 (right * UINT64_C(0x9e3779b97f4a7c15));
    x ^= x >> 33u;
    x *= UINT64_C(0xff51afd7ed558ccd);
    return (size_t)(x ^ (x >> 33u));
}

static bool stored_ground_pair_seen(StoredGroundPairs *memo,
                                    const Atom *left, AtomId right) {
    if (memo->warmup < 32u) {
        memo->warmup++;
        return false;
    }
    if (!memo->slots) {
        memset(memo->local, 0, sizeof(memo->local));
        memo->slots = memo->local;
        memo->capacity = 64u;
    }
    if (memo->used >= memo->capacity / 2u) {
        if (memo->capacity > SIZE_MAX / (2u * sizeof(*memo->slots)))
            return false;
        size_t capacity = memo->capacity * 2u;
        StoredGroundPair *slots = cetta_malloc(capacity * sizeof(*slots));
        memset(slots, 0, capacity * sizeof(*slots));
        for (size_t i = 0u; i < memo->capacity; i++) {
            StoredGroundPair pair = memo->slots[i];
            if (!pair.left)
                continue;
            size_t at = stored_ground_pair_hash(pair.left, pair.right) & (capacity - 1u);
            while (slots[at].left)
                at = (at + 1u) & (capacity - 1u);
            slots[at] = pair;
        }
        if (memo->slots != memo->local)
            free(memo->slots);
        memo->slots = slots;
        memo->capacity = capacity;
    }
    size_t at = stored_ground_pair_hash(left, right) & (memo->capacity - 1u);
    while (memo->slots[at].left) {
        if (memo->slots[at].left == left && memo->slots[at].right == right)
            return true;
        at = (at + 1u) & (memo->capacity - 1u);
    }
    memo->slots[at] = (StoredGroundPair){left, right};
    memo->used++;
    return false;
}

static void stored_ground_pairs_free(StoredGroundPairs *memo) {
    if (memo->slots != memo->local)
        free(memo->slots);
}

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
        if (left->ground.gkind != right_kind &&
            (left->ground.gkind == GV_BIGINT ||
             left->ground.gkind == GV_RATIONAL ||
             right_kind == GV_BIGINT || right_kind == GV_RATIONAL)) {
            /* A bigint or rational may equal a number of another kind. */
            Atom *right = term_universe_get_atom(
                candidate_universe, right_id);
            return right && cetta_he_promoted_numbers_equal(left, right);
        }
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
               rhs &&
               atom_string_equals_bytes(
                   left, rhs, tu_string_len(candidate_universe, right_id));
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
    case GV_INTERNAL_TAG:
        return cetta_internal_tag_is_term_stable(left->ground.ival) &&
               left->ground.ival == tu_internal_tag(candidate_universe, right_id);
    case GV_SPACE:
    case GV_STATE:
    case GV_CAPTURE:
    case GV_BINDINGS:
    case GV_TENSOR:
    case GV_FOREIGN:
    case GV_STRUCTURAL:
    case GV_SEAL:
    case GV_TERM_GRAPH:
    case GV_PRIME_NEED_CAPABILITY:
    case GV_PRIME_CONTEXT:
        return false;
    }
    return false;
}

/* A decoded TermUniverse node belongs to its persistent arena.  Keep open
 * nodes as syntax plus activation context; only a universe without a
 * decodable persistent node needs the arena-local materialized fallback. */
static bool term_universe_match_binding_value(
        const TermUniverse *universe, Arena *fallback,
        AtomId id, CettaFrameIdentity epoch, BindingValue *out) {
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
    Bindings *b, Arena *a, CettaFrameIdentity epoch) {
    cetta_native_cost_charge(CETTA_COST_MATCH_ATTEMPT, 1u);
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
            b, NULL, a, false);
    }
    StoredMatchWorklist work;
    StoredGroundPairs ground_pairs = {.slots = NULL, .capacity = 0u,
        .used = 0u, .warmup = 0u};
    bool closed = left->kind == ATOM_EXPR && !atom_has_vars(left) &&
        (left->flags & ATOM_FLAG_HASHCONS_ELIGIBLE) != 0u &&
        tu_hdr(candidate_universe, right_id) &&
        !tu_has_vars(candidate_universe, right_id);
    const Atom *ground_root = left;
    bool ground_qualified = match_shared_ground_reflexivity_certified(left);
    work.items = work.inline_items;
    work.len = 0;
    work.cap = sizeof work.inline_items / sizeof work.inline_items[0];
    MatchWriteStamp attempt_start = match_write_stamp(b, 0u);
    bool attempt_repeated_var = false;
    if (!stored_match_push(&work, left_value, right_id)) goto fail;

    while (work.len > 0) {
        cetta_native_cost_charge(CETTA_COST_MATCH_NODE, 1u);
        StoredMatchPair pair = work.items[--work.len];
        left_value = pair.left;
        right_id = pair.right_id;
        size_t dereferences = 0;
        size_t dereference_limit = 0u;

retry_pair:
        left = left_value.skeleton;
        const CettaTermHdr *right_header = tu_hdr(candidate_universe, right_id);
        if (!right_header) {
            Atom *right = term_universe_get_atom(
                (TermUniverse *)candidate_universe, right_id);
            if (!right || !match_atoms_epoch_views_worklist(
                    left, binding_value_kind(left_value),
                    binding_value_epoch(left_value), 0u, right, b, NULL, a, epoch,
                    BINDING_VALUE_CONTEXTUAL, false,
                    NULL, NULL, NULL))
                goto fail;
            continue;
        }
        AtomKind right_kind = (AtomKind)right_header->tag;
        if (left->kind == ATOM_VAR) {
            BindingValue existing = bindings_lookup_value(b, left_value);
            if (existing.skeleton) {
                if (!bindings_dereference_step(
                        &dereferences, &dereference_limit, b, 0u))
                    goto fail;
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
                            left_value, right_existing, b, NULL, a, false)) {
                        attempt_repeated_var = true;
                        goto fail;
                    }
                    continue;
                }
                if (var_id_eq(binding_value_variable_id(left_value), right_var_id)) continue;
                BindingValue value = binding_value_from_atom(NULL);
                if (!term_universe_match_binding_value(
                        candidate_universe, a, right_id, epoch, &value) ||
                    !bindings_add_internal(
                        b, binding_value_variable_id(left_value), atom_symbol_current_id(left),
                        left->name_key, value, true)) goto fail;
                continue;
            }
            BindingValue value = binding_value_from_atom(NULL);
            if (!term_universe_match_binding_value(
                    candidate_universe, a, right_id, epoch, &value) ||
                !bindings_add_internal(
                        b, binding_value_variable_id(left_value), atom_symbol_current_id(left),
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
                        left_value, existing, b, NULL, a, false)) {
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
                    atom_symbol_current_id(binding_var.skeleton),
                    binding_var.skeleton->name_key,
                    left_value, true))
                goto fail;
            continue;
        }
        switch (left->kind) {
        case ATOM_SYMBOL:
            if (right_kind != ATOM_SYMBOL ||
                atom_symbol_current_id(left) != right_header->sym_or_head)
                goto fail;
            break;
        case ATOM_VAR:
            goto fail;
        case ATOM_GROUNDED:
            if (!stored_grounded_equal(left, candidate_universe, right_id))
                goto fail;
            break;
        case ATOM_EXPR:
            if (atom_petta_value_representation(left) !=
                tu_petta_value_representation(candidate_universe, right_id))
                goto fail;
            if (right_kind == ATOM_EXPR &&
                (match_expr_is_list(left) ||
                 ((right_header->aux32 & CETTA_TERM_HDR_DATA_MASK) > 0u &&
                  cetta_internal_tag_is_list(tu_internal_tag(
                      candidate_universe,
                      tu_child_from_header(candidate_universe, right_header, 0u)))))) {
                /* Lists meet in the contextual matcher, which builds the
                 * rest a list pattern is bound to. */
                Atom *right = term_universe_get_atom(
                    (TermUniverse *)candidate_universe, right_id);
                if (!right || !match_atoms_epoch_views_worklist(
                        left, binding_value_kind(left_value),
                        binding_value_epoch(left_value), 0u, right, b, NULL, a, epoch,
                        BINDING_VALUE_CONTEXTUAL, false,
                        NULL, NULL, NULL))
                    goto fail;
                break;
            }
            if (right_kind != ATOM_EXPR ||
                atom_authored_len(left) !=
                    tu_authored_arity(candidate_universe, right_id))
                goto fail;
            /* A braces node meets only a braces node.  Only a braces node
             * or a variable head could meet the other side's tag. */
            if (left->expr.len > 0u &&
                (match_expr_is_braces(left) ||
                 left->expr.elems[0]->kind == ATOM_VAR) &&
                match_expr_is_braces(left) !=
                    (tu_internal_tag(
                         candidate_universe,
                         tu_child(candidate_universe, right_id, 0u)) ==
                     (int64_t)CETTA_INTERNAL_TAG_PRIME_BRACES))
                goto fail;
            /* Small comparisons do not allocate a memo or scan their whole
             * source. Before the first memo entry, audit arena-local graphs
             * including their leaves; a stale root summary cannot admit a
             * variable, mutable payload or cycle. */
            if (closed && !ground_qualified && ground_pairs.warmup >= 32u) {
                closed = match_occurrence_is_finite_dag_qualified(ground_root, true);
                ground_qualified = true;
            }
            if (closed && stored_ground_pair_seen(&ground_pairs, left, right_id))
                break;
            for (CettaExprIndex i = atom_authored_len(left); i > 0; i--) {
                CettaExprIndex child = i - 1u;
                BindingValue child_value = left_value;
                child_value.skeleton = left->expr.elems[child];
                if (!stored_match_push(
                        &work, child_value,
                        tu_child_from_header(candidate_universe, right_header, child)))
                    goto fail;
            }
            break;
        }
    }
    if (work.items != work.inline_items) free(work.items);
    stored_ground_pairs_free(&ground_pairs);
    match_note_unification_attempt(
        true, match_write_stamp_changed(
                  attempt_start, match_write_stamp(b, 0u)), false);
    return true;

fail:
    if (work.items != work.inline_items) free(work.items);
    stored_ground_pairs_free(&ground_pairs);
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

static CettaMatchCompletion binding_capture_value_compare(Arena *arena, BindingValue left, BindingValue right) {
    if (!left.skeleton || !right.skeleton)
        return left.skeleton==right.skeleton ? CETTA_MATCH_MATCHED : CETTA_MATCH_REFUTED;
    if (left.context == right.context)
        return atom_capture_identical_checked(left.skeleton, right.skeleton);
    Atom *a = binding_value_materialize(arena, left);
    Atom *b = binding_value_materialize(arena, right);
    return a && b ? atom_capture_identical_checked(a,b) : CETTA_MATCH_INCOMPLETE;
}

static CettaMatchCompletion bindings_capture_frame_compare(Arena *arena,
    const BindingsFrameStateView *left, const BindingsFrameStateView *right) {
    if (left->ref.identity.word != right->ref.identity.word ||
        left->slot_count != right->slot_count ||
        left->schema_slot_count != right->schema_slot_count ||
        left->schema_revision != right->schema_revision ||
        left->schema_complete != right->schema_complete ||
        left->has_activation_write_boundary != right->has_activation_write_boundary ||
        (left->has_activation_write_boundary &&
         left->activation_write_boundary != right->activation_write_boundary)) return CETTA_MATCH_REFUTED;
    for (uint32_t i = 0u; i < left->slot_count; i++) {
        BindingsFrameSlotStateView a, b;
        if (!bindings_frame_state_read_slot(left, i, &a) ||
            !bindings_frame_state_read_slot(right, i, &b)) return CETTA_MATCH_INCOMPLETE;
        if (!var_id_eq(a.source_id,b.source_id) || !var_id_eq(a.var_id,b.var_id) ||
            a.spelling!=b.spelling || a.declared_in_schema!=b.declared_in_schema)
            return CETTA_MATCH_REFUTED;
        if (a.name_key!=b.name_key) {
            if (!a.name_key || !b.name_key) return CETTA_MATCH_REFUTED;
            CettaMatchCompletion name=atom_capture_identical_checked(a.name_key,b.name_key);
            if (name!=CETTA_MATCH_MATCHED) return name;
        }
        CettaMatchCompletion value=binding_capture_value_compare(arena,a.value,b.value);
        if (value!=CETTA_MATCH_MATCHED) return value;
    }
    return CETTA_MATCH_MATCHED;
}

static CettaMatchCompletion binding_capture_constraint_compare(Arena *arena,
    const BindingConstraint *left,const BindingConstraint *right) {
    BindingValue a[]={left->lhs,left->rhs},b[]={right->lhs,right->rhs};
    for (size_t orientation=0u;orientation<2u;orientation++) {
        CettaMatchCompletion first=binding_capture_value_compare(arena,a[0],b[orientation]);
        if (first==CETTA_MATCH_INCOMPLETE) return first;
        if (first==CETTA_MATCH_REFUTED) continue;
        CettaMatchCompletion second=binding_capture_value_compare(arena,a[1],b[1u-orientation]);
        if (second!=CETTA_MATCH_REFUTED) return second;
    }
    return CETTA_MATCH_REFUTED;
}

CettaMatchCompletion bindings_capture_equal_checked(Arena *arena, Bindings *left, Bindings *right) {
    if (!arena || !left || !right) return CETTA_MATCH_INCOMPLETE;
    size_t left_values = 0u, right_values = 0u;
    uint32_t left_frames = 0u, right_frames = 0u;
    if (!bindings_current_binding_count(left, &left_values) ||
        !bindings_current_binding_count(right, &right_values) ||
        !bindings_frame_state_count(left, &left_frames) ||
        !bindings_frame_state_count(right, &right_frames)) return CETTA_MATCH_INCOMPLETE;
    if (left_values!=right_values || left_frames!=right_frames || left->eq_len!=right->eq_len)
        return CETTA_MATCH_REFUTED;
    BindingsIterator values = {.bindings = left};
    Binding binding;
    size_t visited = 0u;
    while (bindings_iterator_next(&values, &binding)) {
        visited++;
        BindingValue other = bindings_lookup_value_id(right, binding.var_id);
        if (!other.skeleton) return CETTA_MATCH_REFUTED;
        CettaMatchCompletion value=binding_capture_value_compare(arena,binding.value,other);
        if (value!=CETTA_MATCH_MATCHED) return value;
    }
    if (visited != left_values) return CETTA_MATCH_INCOMPLETE;
    BindingsFrameStateIterator frames = {.bindings = left};
    for (uint32_t i = 0u; i < left_frames; i++) {
        BindingsFrameStateView a, b;
        if (!bindings_frame_state_next(&frames, &a)) return CETTA_MATCH_INCOMPLETE;
        BindingsFrameStateIterator candidates = {.bindings = right};
        bool found = false;
        for (uint32_t j = 0u; j < right_frames; j++) {
            if (!bindings_frame_state_next(&candidates, &b)) return CETTA_MATCH_INCOMPLETE;
            if (a.ref.identity.word != b.ref.identity.word) continue;
            CettaMatchCompletion compared=bindings_capture_frame_compare(arena,&a,&b);
            if (compared!=CETTA_MATCH_MATCHED) return compared;
            found = true;
            break;
        }
        if (!found) return CETTA_MATCH_REFUTED;
    }
    bool inline_matched[BINDINGS_TEMP_STACK_CAP] = {0};
    bool *matched = right->eq_len > BINDINGS_TEMP_STACK_CAP
        ? calloc(right->eq_len, sizeof(*matched)) : inline_matched;
    if (!matched) return CETTA_MATCH_INCOMPLETE;
    CettaMatchCompletion equal=CETTA_MATCH_MATCHED;
    for (uint32_t i = 0u; equal==CETTA_MATCH_MATCHED && i < left->eq_len; i++) {
        const BindingConstraint *a = &left->constraints[i];
        bool found = false;
        for (uint32_t j = 0u; j < right->eq_len; j++) {
            if (matched[j]) continue;
            const BindingConstraint *b = &right->constraints[j];
            CettaMatchCompletion compared=binding_capture_constraint_compare(arena,a,b);
            if (compared==CETTA_MATCH_INCOMPLETE) {equal=compared;break;}
            if (compared==CETTA_MATCH_MATCHED) {
                matched[j] = true;
                found = true;
                break;
            }
        }
        if (!found && equal==CETTA_MATCH_MATCHED) equal=CETTA_MATCH_REFUTED;
    }
    if (matched != inline_matched) free(matched);
    return equal;
}

bool bindings_capture_equal(Arena *arena,Bindings *left,Bindings *right) {
    return bindings_capture_equal_checked(arena,left,right)==CETTA_MATCH_MATCHED;
}

bool bindings_eq(Bindings *a, Bindings *b) {
    if (bindings_reader_context_view(a)!=bindings_reader_context_view(b)) return false;
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
    if (!cetta_weight_ledger_equal(bindings_weight_view(a), bindings_weight_view(b)))
        return false;
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


void bindings_frame_schema_storage_census(const BindingsFrameSchema *schema,
        CettaStorageCensus *census) {
    if (!schema || !cetta_storage_array(census, schema, schema->cap,
            sizeof(*schema->source_ids), sizeof(*schema))) return;
    cetta_storage_array(census, schema->spellings, schema->cap, sizeof(*schema->spellings), 0u);
    cetta_storage_array(census, schema->name_keys, schema->cap, sizeof(*schema->name_keys), 0u);
}

static void bindings_frame_storage_census(const BindingsFrameIndexEntry *entry,
        CettaStorageCensus *census) {
    bindings_frame_schema_storage_census(entry->schema, census);
    cetta_storage_array(census, entry->storage_references, 1u,
        sizeof(*entry->storage_references), 0u);
    cetta_storage_array(census, entry->values, entry->cap, sizeof(*entry->values), 0u);
    cetta_storage_array(census, entry->write_version, entry->cap, sizeof(*entry->write_version), 0u);
}

static void bindings_storage_visit(const void *raw, CettaStorageCensus *census);
void bindings_storage_census(const Bindings *bindings, CettaStorageCensus *census) {
    cetta_storage_schedule(census, bindings, bindings_storage_visit);
}
void bindings_builder_storage_census(const BindingsBuilder *builder,
        CettaStorageCensus *census) {
    if (!builder) return;
    bindings_storage_census(&builder->current,census);
    cetta_storage_array(census,builder->trail,builder->trail_cap,
        sizeof(*builder->trail),0u);
    cetta_storage_array(census,builder->owner_undo,builder->owner_undo_cap,
        sizeof(*builder->owner_undo),0u);
    for (uint32_t i=0u;i<builder->owner_undo_len;i++)
        bindings_owners_storage_census(builder->owner_undo[i].previous,census);
    cetta_storage_array(census,builder->frame_undo,builder->frame_undo_cap,
        sizeof(*builder->frame_undo),0u);
    cetta_storage_array(census,builder->frame_registration_undo,builder->frame_registration_undo_cap,
        sizeof(*builder->frame_registration_undo),0u);
    for (uint32_t i=0u;i<builder->frame_registration_undo_len;i++)
        bindings_frame_schema_storage_census(builder->frame_registration_undo[i].previous_schema,census);
    cetta_storage_array(census,builder->prime_trail,builder->prime_trail_cap,
        sizeof(*builder->prime_trail),0u);
    for (uint32_t i=0u;i<builder->prime_trail_len;i++)
        prime_occurrence_storage_census(&builder->prime_trail[i],census);
    if (builder->history.context)
        cetta_storage_refuse(census,"ExternalBindingHistory");
}
static void bindings_storage_visit(const void *raw, CettaStorageCensus *census) {
    const Bindings *bindings = raw;
    if (!bindings || !census) return;
    BindingPoolBlock *block = bindings->entries ? bindings_pool_block(bindings->entries) : NULL;
    if (block) cetta_storage_array(census, block, block->capacity, sizeof(Binding), sizeof(*block));
    block = bindings->shared_entries ? bindings_pool_block(bindings->shared_entries) : NULL;
    if (block) cetta_storage_array(census, block, block->capacity, sizeof(Binding), sizeof(*block));
    block = bindings->constraints ? bindings_pool_block(bindings->constraints) : NULL;
    if (block) cetta_storage_array(census, block, block->capacity, sizeof(BindingConstraint), sizeof(*block));
    const BindingsLookupIndex *lookup = bindings->lookup_index;
    if (lookup && cetta_storage_object(census, lookup, sizeof(*lookup), CETTA_STORAGE_BLOCK)) {
        cetta_storage_array(census, lookup->slots, lookup->capacity, sizeof(*lookup->slots), 0u);
        cetta_storage_array(census, lookup->single_cached_ids, lookup->single_cached_cap,
            sizeof(*lookup->single_cached_ids), 0u);
    }
    const BindingsFrameIndex *index = bindings->frame_index;
    if (index && cetta_storage_object(census, index, sizeof(*index), CETTA_STORAGE_BLOCK)) {
        cetta_storage_array(census, index->frames, index->cap, sizeof(*index->frames), 0u);
        cetta_storage_array(census, index->handle_table, (size_t)index->handle_table_mask + 1u,
            sizeof(*index->handle_table), 0u);
        cetta_storage_array(census, index->spares, index->spare_cap, sizeof(*index->spares), 0u);
        for (uint32_t i = 0; i < index->len; i++) bindings_frame_storage_census(&index->frames[i], census);
        for (uint32_t i = 0; i < index->spare_len; i++) bindings_frame_storage_census(&index->spares[i], census);
    }
    bindings_owners_storage_census(bindings->owners, census);
    if (bindings->prime_ext && cetta_storage_object(census, bindings->prime_ext,
            sizeof(*bindings->prime_ext), CETTA_STORAGE_BLOCK))
        prime_occurrence_storage_census(bindings->prime_ext, census);
}

/* The authoritative frame slots, chronological rows and constraints are read
 * without applying bindings or materializing a value. Derived table layouts
 * are observed separately and require the existing lookup invariant. */
#define BC_S(n,a,b) do { if (!read->scalar(read->context,n,(uint64_t)(a),(uint64_t)(b))) return false; } while (0)
#define BC_F(n,a,b) do { if (!read->physical(read->context,n,(uint64_t)(a),(uint64_t)(b))) return false; } while (0)
#define BC_P(n,a,b) do { if (!read->address(read->context,n,a,b)) return false; } while (0)
#define BC_I(n,r,a,b) do { if (!read->identity(read->context,n,r,(uint64_t)(a),(uint64_t)(b))) return false; } while (0)
#define BC_FRAME(n,a,b) do { if (!cetta_checkpoint_frame(read,n,a,b)) return false; } while (0)
#define BC_VAR(n,a,b) do { if (!cetta_checkpoint_var(read,n,a,b)) return false; } while (0)
#define BC_A(n,a,b) do { if (!read->atom(read->context,n,a,b)) return false; } while (0)

static bool bindings_compare_value(BindingValue a, BindingValue b,
    const CettaCheckpointCompare *read) {
    BC_S("binding.value_kind",binding_value_kind(a),binding_value_kind(b));
    BC_FRAME("binding.value_epoch",binding_value_epoch(a),binding_value_epoch(b));
    BC_A("binding.value",a.skeleton,b.skeleton);
    return true;
}

bool bindings_frame_schema_compare(const BindingsFrameSchema *a,
    const BindingsFrameSchema *b,const CettaCheckpointCompare *read) {
    if (!cetta_checkpoint_compare_valid(read)) return false;
    BC_P("schema.address",a,b);
    if (!a || !b) return !a && !b;
    uint32_t ar=atomic_load_explicit(&a->references,memory_order_relaxed);
    uint32_t br=atomic_load_explicit(&b->references,memory_order_relaxed);
    if (!ar || !br || a->len!=b->len || a->len>a->cap || b->len>b->cap ||
        (a->cap&&(!a->spellings||!a->name_keys)) ||
        (b->cap&&(!b->spellings||!b->name_keys))) return false;
    BC_F("schema.references",ar,br);BC_F("schema.capacity",a->cap,b->cap);
    BC_S("schema.length",a->len,b->len);
    BC_VAR("schema.first",a->source_first_id,b->source_first_id);
    BC_S("schema.contiguous",a->source_ids_contiguous,b->source_ids_contiguous);
    BC_P("schema.spellings",a->spellings,b->spellings);
    BC_P("schema.name_keys",a->name_keys,b->name_keys);
    for (uint32_t i=0u;i<a->len;i++) {
        BC_S("schema.slot",i,i);
        BC_VAR("schema.source_id",a->source_ids[i],b->source_ids[i]);
    }
    uint32_t capacity = a->cap > b->cap ? a->cap : b->cap;
    for (uint32_t i=0u;i<capacity;i++) {
        BC_S("schema.spelling",i<a->cap?a->spellings[i]:SYMBOL_ID_NONE,
            i<b->cap?b->spellings[i]:SYMBOL_ID_NONE);
        BC_A("schema.name_key",i<a->cap?a->name_keys[i]:NULL,
            i<b->cap?b->name_keys[i]:NULL);
    }
    return true;
}

static bool bindings_compare_frame_table(const BindingsFrameIndex *index,
    const CettaCheckpointCompare *read,bool right) {
    if(!index->handle_table)return index->len==0u;
    size_t cap=(size_t)index->handle_table_mask+1u,count=0u;
    if(!cap||(cap&(cap-1u))||index->handle_table_shift>=32u||
        (UINT64_C(1)<<(32u-index->handle_table_shift))!=cap||index->len>cap/2u)return false;
    for(size_t i=0u;i<cap;i++) {
        BC_F("binding.table_slot",right?0u:i,right?i:0u);
        const BindingsFrameIndexEntry *entry=index->handle_table[i];
        if(!entry)continue;
        bool found=false;
        for(size_t j=0u;j<index->len;j++) {
            BC_F("binding.table_member_probe",right?0u:1u,right?1u:0u);
            if(entry==&index->frames[j]){found=true;break;}
        }
        if(!found)return false;
        for(size_t j=0u;j<i;j++) {
            BC_F("binding.table_unique_probe",right?0u:1u,right?1u:0u);
            if(index->handle_table[j]==entry)return false;
        }
        size_t at=bindings_frame_table_home(index,cetta_frame_handle(entry->epoch)),steps=0u;
        if(at>=cap)return false;
        while(at!=i) {
            BC_F("binding.table_lookup_probe",right?0u:1u,right?1u:0u);
            if(!index->handle_table[at]||++steps>=cap)return false;
            at=(at+1u)&index->handle_table_mask;
        }
        count++;
    }
    return count==index->len;
}

static bool bindings_compare_frames(const BindingsFrameIndex *a,
    const BindingsFrameIndex *b,const CettaCheckpointCompare *read) {
    BC_F("binding.frame_index_storage",(uintptr_t)a,(uintptr_t)b);
    if (!a || !b) return !a && !b;
    uint32_t ar=atomic_load_explicit(&a->references,memory_order_relaxed);
    uint32_t br=atomic_load_explicit(&b->references,memory_order_relaxed);
    if (!ar || !br || !cetta_checkpoint_extent(a->frames,a->len,a->cap,b->frames,b->len,b->cap))
        return false;
    BC_F("binding.frame_references",ar,br);BC_F("binding.frame_capacity",a->cap,b->cap);
    BC_S("binding.schema_revision",a->schema_revision,b->schema_revision);
    BC_S("binding.write_clock",a->write_clock,b->write_clock);
    BC_S("binding.bound_count",a->bound_value_count,b->bound_value_count);
    BC_S("binding.private_count",a->private_value_count,b->private_value_count);
    BC_S("binding.max_schema_len",a->max_schema_len,b->max_schema_len);
    BC_S("binding.frame_count",a->len,b->len);
    BC_F("binding.frames_storage",(uintptr_t)a->frames,(uintptr_t)b->frames);
    BC_F("binding.handle_mask",a->handle_table_mask,b->handle_table_mask);
    BC_F("binding.handle_shift",a->handle_table_shift,b->handle_table_shift);
    BC_F("binding.spare_length",a->spare_len,b->spare_len);
    BC_F("binding.spare_capacity",a->spare_cap,b->spare_cap);
    if (a->spare_len>a->spare_cap || b->spare_len>b->spare_cap) return false;
    /* Spares carry no logical frame. The source helper's reset/reuse law is
     * separate from comparing the live initialized frame inventory. */
    for (uint32_t i=0u;i<a->len;i++) {
        const BindingsFrameIndexEntry *x=&a->frames[i],*y=&b->frames[i];
        /* Only a prefix of the logical slots may be backed; a slot past it
         * is unbound with version zero (slot_store_internal.h). The extents
         * compared are the logical ones, read through the slot accessors. */
        uint32_t x_rows=x->slot_len<x->cap?x->slot_len:x->cap;
        uint32_t y_rows=y->slot_len<y->cap?y->slot_len:y->cap;
        if (x->slot_len!=y->slot_len ||
            (x_rows&&(!x->values||!x->write_version)) ||
            (y_rows&&(!y->values||!y->write_version))) return false;
        BC_S("binding.frame_slot",i,i);
        BC_F("binding.frame_storage",(uintptr_t)x,(uintptr_t)y);
        BC_FRAME("binding.frame_epoch",x->epoch,y->epoch);
        BC_S("binding.owns_identity",x->owns_identity,y->owns_identity);
        if (!bindings_frame_schema_compare(x->schema,y->schema,read)) return false;
        BC_F("binding.values_storage",(uintptr_t)x->values,(uintptr_t)y->values);
        BC_F("binding.write_version_storage",(uintptr_t)x->write_version,(uintptr_t)y->write_version);
        BC_F("binding.slot_capacity",x->cap,y->cap);
        BC_S("binding.slot_length",x->slot_len,y->slot_len);
        BC_S("binding.frame_bound_count",x->bound_value_count,y->bound_value_count);
        BC_S("binding.frame_private_count",x->private_value_count,y->private_value_count);
        BC_S("binding.activation_boundary",x->activation_write_boundary,y->activation_write_boundary);
        BC_S("binding.has_activation_boundary",x->has_activation_write_boundary,y->has_activation_write_boundary);
        BC_S("binding.schema_complete",x->schema_complete,y->schema_complete);
        if (x->storage_references) {
            uint32_t count=atomic_load_explicit(x->storage_references,memory_order_relaxed);
            if (!count) return false;
            BC_F("binding.left_storage_references",count,0u);
        }
        if (y->storage_references) {
            uint32_t count=atomic_load_explicit(y->storage_references,memory_order_relaxed);
            if (!count) return false;
            BC_F("binding.right_storage_references",0u,count);
        }
        for (uint32_t j=0u;j<x->slot_len;j++) {
            BC_S("binding.value_slot",j,j);
            if (!bindings_compare_value(bindings_frame_slot_value(x,j),
                                        bindings_frame_slot_value(y,j),read)) return false;
            BC_S("binding.write_version",bindings_frame_slot_version(x,j),
                 bindings_frame_slot_version(y,j));
        }
    }
    return bindings_compare_frame_table(a,read,false)&&bindings_compare_frame_table(b,read,true);
}

static bool bindings_compare_lookup_probe(const BindingsLookupIndex *index,
    const CettaCheckpointCompare *read,bool right) {
    if(!index->capacity||(index->capacity&(index->capacity-1u))||
        index->count>=index->capacity)return false;
    size_t count=0u,mask=index->capacity-1u;
    for(size_t i=0u;i<index->capacity;i++) {
        BC_F("bindings.lookup_probe_slot",right?0u:i,right?i:0u);
        const BindingsLookupIndexSlot *entry=&index->slots[i];
        if(var_id_is_none(entry->id))continue;
        count++;
        size_t at=bindings_var_id_hash(entry->id)&mask,steps=0u;
        while(at!=i) {
            BC_F("bindings.lookup_probe_edge",right?0u:1u,right?1u:0u);
            if(var_id_is_none(index->slots[at].id)||binding_var_eq(index->slots[at].id,entry->id)||
                ++steps>=index->capacity)return false;
            at=(at+1u)&mask;
        }
    }
    return count==index->count;
}

static bool bindings_compare_lookup(const Bindings *left,const Bindings *right,
    const CettaCheckpointCompare *read) {
    const BindingsLookupIndex *a=left->lookup_index,*b=right->lookup_index;
    BC_S("bindings.lookup_present",a!=NULL,b!=NULL);
    BC_F("bindings.lookup_storage",(uintptr_t)a,(uintptr_t)b);
    if (!a || !b) return !a && !b;
    uint32_t ar=atomic_load_explicit(&a->references,memory_order_relaxed);
    uint32_t br=atomic_load_explicit(&b->references,memory_order_relaxed);
    if (!ar||!br||!a->capacity||!b->capacity||!a->slots||!b->slots||
        a->count>a->capacity||b->count>b->capacity||a->count!=b->count||
        a->synced_len>left->len||b->synced_len>right->len||
        a->synced_len!=b->synced_len||
        !cetta_checkpoint_extent(a->single_cached_ids,a->single_cached_len,a->single_cached_cap,
            b->single_cached_ids,b->single_cached_len,b->single_cached_cap)) return false;
    BC_F("bindings.lookup_references",ar,br);
    BC_F("bindings.lookup_capacity",a->capacity,b->capacity);
    BC_S("bindings.lookup_count",a->count,b->count);
    BC_S("bindings.lookup_synced",a->synced_len,b->synced_len);
    BC_S("bindings.lookup_duplicates",a->has_duplicates,b->has_duplicates);
    BC_S("bindings.lookup_cached_length",a->single_cached_len,b->single_cached_len);
    BC_F("bindings.lookup_cached_capacity",a->single_cached_cap,b->single_cached_cap);
    size_t ac=0u,bc=0u;
    /* The chronological input index, not a hash-table slot or a renamed
     * numeric ID, identifies each current entry in this derived table. */
    for (uint32_t i=0u;i<a->synced_len;i++) {
        const BindingsLookupIndexSlot *x=NULL,*y=NULL;
        for (size_t j=0u;j<a->capacity;j++) {
            BC_F("bindings.lookup_left_slot",j,0u);
            const BindingsLookupIndexSlot *slot=&a->slots[j];
            if (!var_id_is_none(slot->id) && slot->index_plus_one==i+1u) {
                if (x) return false;
                x=slot;ac++;
            }
        }
        for (size_t j=0u;j<b->capacity;j++) {
            BC_F("bindings.lookup_right_slot",0u,j);
            const BindingsLookupIndexSlot *slot=&b->slots[j];
            if (!var_id_is_none(slot->id) && slot->index_plus_one==i+1u) {
                if (y) return false;
                y=slot;bc++;
            }
        }
        BC_S("bindings.lookup_entry_present",x!=NULL,y!=NULL);
        if (!x||!y) { if(x||y)return false;continue; }
        if (!var_id_eq(x->id,bindings_entry_at(left,i)->var_id)||!var_id_eq(y->id,bindings_entry_at(right,i)->var_id))
            return false;
        BC_S("bindings.lookup_input",i,i);
        BC_VAR("bindings.lookup_id",x->id,y->id);
        BC_S("bindings.lookup_cache_kind",x->single_cache_kind,y->single_cache_kind);
        uint32_t kind=bindings_lookup_index_single_cache_kind(x);
        if (kind!=bindings_lookup_index_single_cache_kind(y)) return false;
        if (kind==BINDINGS_SINGLE_REACH_CACHE_TERMINAL)
            BC_VAR("bindings.lookup_terminal",x->single_terminal,y->single_terminal);
        else if (kind==BINDINGS_SINGLE_REACH_CACHE_GROUND)
            BC_S("bindings.lookup_closed_prefix",x->closed_prefix,y->closed_prefix);
    }
    if (ac!=a->count||bc!=b->count) return false;
    for (size_t i=0u;i<a->single_cached_len;i++) {
        BC_S("bindings.cached_slot",i,i);
        BC_VAR("bindings.cached_id",a->single_cached_ids[i],b->single_cached_ids[i]);
    }
    return bindings_compare_lookup_probe(a,read,false)&&bindings_compare_lookup_probe(b,read,true);
}

bool bindings_checkpoint_compare(const Bindings *a,const Bindings *b,
    const CettaCheckpointCompare *read) {
    if (!a || !b || !cetta_checkpoint_compare_valid(read) || a->shared_len>a->len ||
        b->shared_len>b->len || a->len!=b->len || a->eq_len!=b->eq_len ||
        a->len-a->shared_len>a->cap || b->len-b->shared_len>b->cap ||
        a->shared_len>a->shared_cap || b->shared_len>b->shared_cap ||
        (a->shared_len&&!a->shared_entries)||(b->shared_len&&!b->shared_entries) ||
        (a->len>a->shared_len&&!a->entries)||(b->len>b->shared_len&&!b->entries) ||
        !cetta_checkpoint_extent(a->constraints,a->eq_len,a->eq_cap,b->constraints,b->eq_len,b->eq_cap)) return false;
    if (!cetta_checkpoint_container(read,"bindings",CETTA_CHECKPOINT_BINDINGS_CONTAINER,a,b))
        return false;
    /* Captured arbitrary binding-value owners are outside this pure profile.
     * Lifetime retention by itself would not compare their provider state. */
    if (a->owners || b->owners || bindings_reader_context_view(a) || bindings_reader_context_view(b)) return false;
    BC_P("bindings.owners",a->owners,b->owners);
    BC_S("bindings.length",a->len,b->len);BC_S("bindings.constraints",a->eq_len,b->eq_len);
    BC_S("bindings.cycle_state",a->cycle_state,b->cycle_state);
    for (size_t i=0u;i<3u;i++) BC_S("bindings.rhs_bloom",a->rhs_variable_bloom[i],b->rhs_variable_bloom[i]);
    BC_S("bindings.private_entries",a->private_entry_count,b->private_entry_count);
    BC_S("bindings.private_constraints",a->private_constraint_count,b->private_constraint_count);
    BC_F("bindings.capacity",a->cap,b->cap);BC_F("bindings.constraint_capacity",a->eq_cap,b->eq_cap);
    BC_F("bindings.shared_length",a->shared_len,b->shared_len);BC_F("bindings.shared_capacity",a->shared_cap,b->shared_cap);
    for (uint32_t i=0u;i<a->len;i++) {
        const Binding *x=bindings_entry_at(a,i),*y=bindings_entry_at(b,i);
        BC_S("bindings.row",i,i);
        BC_F("bindings.row_storage",(uintptr_t)x,(uintptr_t)y);
        BC_VAR("bindings.variable",x->var_id,y->var_id);
        BC_S("bindings.spelling",x->spelling,y->spelling);
        BC_A("bindings.name_key",x->name_key,y->name_key);
        if (!bindings_compare_value(x->value,y->value,read)) return false;
    }
    for (uint32_t i=0u;i<a->eq_len;i++) {
        BC_S("bindings.constraint",i,i);
        if (!bindings_compare_value(a->constraints[i].lhs,b->constraints[i].lhs,read) ||
            !bindings_compare_value(a->constraints[i].rhs,b->constraints[i].rhs,read)) return false;
    }
    if (!bindings_compare_frames(a->frame_index,b->frame_index,read)) return false;
    if (!bindings_compare_lookup(a,b,read)) return false;
    if (!cetta_checkpoint_container(read,"bindings.occurrence",
            CETTA_CHECKPOINT_BINDINGS_OCCURRENCE_CONTAINER,a->prime_ext,b->prime_ext))
        return false;
    BC_I("bindings.occurrence_id",CETTA_CHECKPOINT_BINDING_OCCURRENCE,
        bindings_occurrence_token(a),bindings_occurrence_token(b));
    if (!prime_need_snapshot_compare(bindings_need_view(a),bindings_need_view(b),read) ||
        !prime_need_branch_state_compare(bindings_branch_state_view(a),bindings_branch_state_view(b),read) ||
        !cetta_weight_ledger_compare(bindings_weight_view(a),bindings_weight_view(b),read)) return false;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if (!prime_need_receipt_compare(bindings_receipt_view(a),bindings_receipt_view(b),read)) return false;
#endif
    return true;
}
#undef BC_S
#undef BC_F
#undef BC_P
#undef BC_I
#undef BC_A
