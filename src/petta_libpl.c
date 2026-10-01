#define _GNU_SOURCE
#include "petta_libpl.h"

#include "eval.h"
#include "grounded.h"
#include "library.h"
#include "parallel_executor.h"
#include "stats.h"
#include "symbol.h"
#include "delay_service.h"
#include "term_graph.h"
#include "var_index.h"
#include "foreign_region.h"
#include "shared_transition.h"

#include <SWI-Prolog.h>

#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    SymbolId symbol;
    uint64_t symbol_table_instance;
    char *name;
    size_t name_len;
    size_t *function_arities;
    size_t arity_len;
    size_t arity_cap;
    uint64_t scanned_revision;
    /*
     * Plan-time engine resolution registers names the user never imported.
     * Those entries carry call authority ONLY at occurrences the planner
     * compiled as calls; every other consumer (pattern matching, currying,
     * data classification) must treat them as absent, or engine builtins
     * such as rule/2 and aggregate/3 would capture the chainer's own
     * constructor vocabulary.
     */
    bool auto_resolved;
    /*
     * A name SWI-PeTTa's prelude registers.  It has the arities the
     * prelude's registration records for it, whatever this module
     * implements (petta_semantics_registered_builtin_arities): a call at
     * one of them is the reference's call, and any other application is
     * partial or over-applied, as there.  They are known without starting
     * the engine.
     */
    bool reference_stdlib;
} PettaLibplImport;

typedef struct {
    record_t record;
    uint64_t generation;
    size_t next_free;
    bool live;
} PettaLibplPlrefSlot;

typedef struct {
    uint64_t runtime_id;
    size_t slot;
    uint64_t generation;
} PettaLibplPlrefHandle;

typedef struct {
    SymbolId name;
    Atom *code;
} PettaLibplCallable;

struct CettaLibPrologRuntime {
    bool prepared;
    /* The static-import! clauses are in the module (installed on first use),
     * and the module the imported files are consulted into. */
    bool static_import_ready;
    char *static_module_name;
    size_t static_module_name_len;
    uint64_t instance_id;
    char *module_name;
    size_t module_name_len;
    char *working_dir;
    module_t module;
    char *solver_module_name;
    module_t solver_module;
    bool solver_monitor_ready;
    PettaLibplImport *imports;
    size_t import_len;
    size_t import_cap;
    uint64_t symbol_table_instance;
    uint64_t revision;
    _Atomic uint64_t capability_revision;
    /*
     * Names proven to have neither a live predicate at any arity nor an
     * arity/2 declaration.  The reference resolves every application head
     * against the engine, so unknown heads must be probed once; recording
     * the misses keeps ordinary constructors from re-entering the engine on
     * every classification.  Any revision change discards the record.
     */
    SymbolId *probe_misses;
    size_t probe_miss_len;
    size_t probe_miss_cap;
    uint64_t probe_miss_revision;
    _Atomic uint64_t import_admission[1024];
    _Atomic bool import_admission_saturated;
    PettaLibplPlrefSlot *plrefs;
    size_t plref_len;
    size_t plref_cap;
    size_t plref_free_head;
    uint64_t plref_next_generation;
    size_t plref_live;
    /* Generated names cross as atoms, while this runtime owns their neutral
     * executable code. Captures cross separately as partial/2 arguments. */
    Arena callable_codes;
    PettaLibplCallable *callables;
    size_t callable_len;
    size_t callable_cap;
};

/* Query release: PL_cut_query/PL_close_query return true, false, or
 * PL_S_NOT_INNER.  Detailed contract sits by petta_libpl_release_query. */
typedef enum {
    PETTA_LIBPL_RELEASE_CLOSE = 0,
    PETTA_LIBPL_RELEASE_CUT = 1,
} PettaLibplReleaseKind;

typedef enum {
    PETTA_LIBPL_RELEASE_OK,
    PETTA_LIBPL_RELEASE_CLEANUP_ERROR,
    PETTA_LIBPL_RELEASE_NOT_INNER,
} PettaLibplReleaseStatus;

static PettaLibplReleaseStatus petta_libpl_release_query(
    Arena *arena, Atom **cleanup_raised, qid_t query,
    PettaLibplReleaseKind kind);

static void petta_solver_collect_retired(void);


static bool petta_libpl_register_grounded_bridge(
    CettaLibPrologRuntime *runtime, SymbolId head,
    size_t predicate_arity);
static foreign_t petta_libpl_grounded_bridge(
    term_t arguments, int supplied_arity,
    control_t control);

static void petta_libpl_advance_revision(
    CettaLibPrologRuntime *runtime) {
    if (!runtime)
        return;
    uint64_t capability = atomic_load_explicit(
        &runtime->capability_revision, memory_order_relaxed);
    atomic_store_explicit(
        &runtime->capability_revision,
        capability == UINT64_MAX ? 1u : capability + 1u,
        memory_order_release);
    if (runtime->revision != UINT64_MAX) {
        runtime->revision++;
        return;
    }

    /* Preserve cache inequality when the epoch wraps. */
    runtime->revision = 1u;
    runtime->probe_miss_len = 0u;
    runtime->probe_miss_revision = 0u;
    for (size_t index = 0u; index < runtime->import_len; index++)
        runtime->imports[index].scanned_revision = 0u;
}

typedef struct {
    VarId id;
    Atom *prototype;
    term_t term;
} PettaLibplVar;

typedef struct {
    PettaLibplVar *items;
    size_t len;
    size_t cap;
    CettaVarIndex index;
    term_t (*slot)(void *context, Atom *variable);
    void *slot_context;
    /* A retained client can recognize an unbound variable outside the small
     * argument map. The callback reads its owned identity, never its spelling. */
    Atom *(*native_variable)(void *context, Arena *arena, term_t variable);
    /* Convert a term that is not a finite tree to its graph
     * (petta_libpl_graph_from_term); set only to convert a solution again
     * after it failed as a tree, so the tree path never tests for cycles. */
    bool cyclic_graphs;
    /* The call's delayed goals, when its caller keeps them
     * (petta_libpl_delay_collect). */
    struct PettaLibplDelay *delay;
    /* Read the runtime's module as itself rather than as `user`: an error
     * term unqualifies it instead (petta_libpl_exception_value). */
    bool private_module;
} PettaLibplVarMap;

typedef struct {
    term_t term;
    Atom *variable;
} PettaLibplBackVar;

typedef struct {
    PettaLibplBackVar *items;
    size_t len;
    size_t cap;
    /* Sorted only for one observed, quiescent solution. Never retained across
     * PL_next_solution, which may bind, unbind or merge its variables. Ties
     * keep the first native variable, matching the original conversion. */
    size_t *known;
    size_t known_len;
    bool known_ready;
} PettaLibplBackVarMap;

static void petta_libpl_var_map_free(PettaLibplVarMap *variables) {
    cetta_var_index_free(&variables->index);
    free(variables->items);
}

static void petta_libpl_back_map_free(PettaLibplBackVarMap *variables) {
    free(variables->known);
    free(variables->items);
}

/* The delayed goals of one call from a caller that keeps them: the goals of
 * the caller's service it carries into Prolog, in one conjunction, and the
 * variables they wait on.  Each answer withdraws those and suspends what SWI
 * still delays on its variables (petta_libpl_delay_answer). */
typedef struct PettaLibplDelay {
    const CettaDelayService *service;
    Atom *carried;
    Atom **carried_vars;
    size_t carried_len;
} PettaLibplDelay;

static pthread_once_t g_petta_libpl_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_petta_libpl_lock = PTHREAD_MUTEX_INITIALIZER;
/* Set when this process first starts the engine. */
static _Atomic bool g_petta_libpl_started;
static bool g_petta_libpl_ready;
/* The message_hook/3 clause for the private modules is in `user`. */
static bool g_petta_libpl_message_hook_installed;
static bool g_petta_libpl_owns_engine;
static PL_engine_t g_petta_libpl_worker_engine;
/* SWI's main engine was given back by the worker thread that started it. */
static bool g_petta_libpl_main_released;
static _Atomic uint64_t g_petta_libpl_runtime_counter = 1u;
static _Thread_local Arena *g_petta_libpl_active_arena;
static _Thread_local CettaLibPrologRuntime *g_petta_libpl_active_runtime;
static _Thread_local uint32_t g_petta_libpl_enter_depth;

/*
 * Opaque Prolog terms (clause references from assertz/2, stream handles,
 * other blobs) have no structural image on the native side.  Each one is
 * recorded engine-side in the owning runtime and crosses the boundary as
 * (PettaClauseRef <runtime> <slot> <generation>).  The three-part identity
 * rejects handles from another runtime and stale handles after slot reuse.
 * Released slots form an O(1) free list, and their engine records are erased
 * after successful erase/1 or when the runtime is destroyed.
 */
#define PETTA_LIBPL_PLREF_TAG "PettaClauseRef"

static void petta_libpl_register_reference_stdlib(
    CettaLibPrologRuntime *runtime);

typedef struct {
    const char *name;
    size_t function_arity;
    pl_function_t implementation;
    bool nondeterministic;
} PettaLibplStandardBridge;

static bool petta_libpl_to_term(
    Atom *atom, term_t output,
    PettaLibplVarMap *variables, uint32_t depth);
static Atom *petta_libpl_from_term(
    Arena *arena, term_t term,
    PettaLibplVarMap *variables,
    PettaLibplBackVarMap *unknown,
    uint32_t depth);
static Atom *petta_libpl_graph_from_term(Arena *arena, term_t term,
                                         PettaLibplVarMap *variables,
                                         PettaLibplBackVarMap *unknown,
                                         uint32_t depth);
static bool petta_libpl_graph_to_term(const Atom *value, term_t output,
                                      PettaLibplVarMap *variables,
                                      uint32_t depth);

typedef struct {
    term_t left;
    term_t right;
} PettaLibplTermPair;

typedef struct {
    PettaLibplTermPair *items;
    size_t len;
    size_t cap;
} PettaLibplTermPairs;

static void petta_libpl_term_pairs_push(PettaLibplTermPairs *pairs,
                                        term_t left, term_t right) {
    if (pairs->len == pairs->cap) {
        size_t cap = pairs->cap ? pairs->cap * 2u : 32u;
        pairs->items = pairs->items
            ? cetta_realloc(pairs->items, sizeof(*pairs->items) * cap)
            : cetta_malloc(sizeof(*pairs->items) * cap);
        pairs->cap = cap;
    }
    pairs->items[pairs->len++] = (PettaLibplTermPair){left, right};
}

static bool petta_libpl_numbers_equal(Arena *arena, term_t left,
                                      term_t right) {
    PettaLibplVarMap variables = {0};
    PettaLibplBackVarMap unknown = {0};
    Atom *left_value = petta_libpl_from_term(
        arena, left, &variables, &unknown, 0u);
    Atom *right_value = left_value
        ? petta_libpl_from_term(arena, right, &variables, &unknown, 0u)
        : NULL;
    petta_libpl_var_map_free(&variables);
    petta_libpl_back_map_free(&unknown);
    return left_value && right_value &&
           atom_value_eq(left_value, right_value);
}

/* A node met with a factor, in that factor's list. */
typedef struct {
    term_t node;
    size_t next;
} PettaLibplPartner;

/* The shared compound subterms of a pair of terms, after '$factorize_term':
 * each is now an unbound variable in the terms, standing for its subterm.
 * order sorts them by the standard order, which places unbound variables by
 * address, and nothing moves them while no Prolog runs.  met holds the pairs
 * of factors already compared, as (i + 1) << 32 | (j + 1), 0 for empty; a
 * factor's pairs with other nodes are listed from left_heads or right_heads,
 * by the factor's side, and matched by identity. */
typedef struct {
    term_t vars;
    term_t values;
    size_t *order;
    size_t len;
    uint64_t *met;
    size_t met_cap;
    size_t met_len;
    size_t *left_heads;
    size_t *right_heads;
    PettaLibplPartner *partners;
    size_t partners_len;
    size_t partners_cap;
} PettaLibplFactors;

static _Thread_local PettaLibplFactors *g_petta_libpl_sorting_factors;

static int petta_libpl_factor_order(const void *left, const void *right) {
    const PettaLibplFactors *factors = g_petta_libpl_sorting_factors;
    return PL_compare(factors->vars + *(const size_t *)left,
                      factors->vars + *(const size_t *)right);
}

/* The factor a term is, or SIZE_MAX. */
static size_t petta_libpl_factor_index(const PettaLibplFactors *factors,
                                       term_t term) {
    if (!PL_is_variable(term))
        return SIZE_MAX;
    size_t low = 0u;
    size_t high = factors->len;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        int order = PL_compare(term, factors->vars + factors->order[middle]);
        if (order == 0)
            return factors->order[middle];
        if (order < 0)
            high = middle;
        else
            low = middle + 1u;
    }
    return SIZE_MAX;
}

/* Records a pair of factors; false when it was already met. */
static bool petta_libpl_factors_meet(PettaLibplFactors *factors, size_t left,
                                     size_t right) {
    if (factors->met_len * 2u >= factors->met_cap) {
        size_t cap = factors->met_cap ? factors->met_cap * 2u : 64u;
        uint64_t *met = cetta_malloc(sizeof(*met) * cap);
        memset(met, 0, sizeof(*met) * cap);
        for (size_t i = 0u; i < factors->met_cap; i++) {
            uint64_t key = factors->met[i];
            if (!key)
                continue;
            size_t slot = (size_t)(key * UINT64_C(0x9E3779B97F4A7C15) >> 32) &
                          (cap - 1u);
            while (met[slot])
                slot = (slot + 1u) & (cap - 1u);
            met[slot] = key;
        }
        free(factors->met);
        factors->met = met;
        factors->met_cap = cap;
    }
    uint64_t key = ((uint64_t)left + 1u) << 32 | ((uint64_t)right + 1u);
    size_t slot = (size_t)(key * UINT64_C(0x9E3779B97F4A7C15) >> 32) &
                  (factors->met_cap - 1u);
    while (factors->met[slot]) {
        if (factors->met[slot] == key)
            return false;
        slot = (slot + 1u) & (factors->met_cap - 1u);
    }
    factors->met[slot] = key;
    factors->met_len++;
    return true;
}

/* Records a pair of a factor and a compound node that is not one; false when
 * it was already met. */
static bool petta_libpl_factor_meets_node(PettaLibplFactors *factors,
                                          size_t *heads, size_t factor,
                                          term_t node) {
    if (!PL_is_compound(node))
        return true;
    for (size_t at = heads[factor]; at != SIZE_MAX;
         at = factors->partners[at].next) {
        if (PL_same_compound(factors->partners[at].node, node))
            return false;
    }
    if (factors->partners_len == factors->partners_cap) {
        size_t cap = factors->partners_cap ? factors->partners_cap * 2u : 32u;
        factors->partners = factors->partners
            ? cetta_realloc(factors->partners,
                            sizeof(*factors->partners) * cap)
            : cetta_malloc(sizeof(*factors->partners) * cap);
        factors->partners_cap = cap;
    }
    factors->partners[factors->partners_len] =
        (PettaLibplPartner){node, heads[factor]};
    heads[factor] = factors->partners_len++;
    return true;
}

/* Factors both terms at once, so a subterm they share is one factor; the
 * change to the terms lasts until the caller's frame is discarded.  The
 * skeletons of the two terms land in left_out and right_out. */
static bool petta_libpl_factorize(term_t left, term_t right,
                                  PettaLibplFactors *factors,
                                  term_t left_out, term_t right_out) {
    term_t call = PL_new_term_refs(3);
    predicate_t factorize = PL_predicate("$factorize_term", 3, "system");
    atom_t minus = PL_new_atom("-");
    functor_t pair = PL_new_functor(minus, 2);
    PL_unregister_atom(minus);
    if (!call || !factorize ||
        !PL_cons_functor(call, pair, left, right) ||
        !PL_call_predicate(NULL, PL_Q_NODEBUG | PL_Q_CATCH_EXCEPTION,
                           factorize, call) ||
        !PL_get_arg(1, call + 1, left_out) ||
        !PL_get_arg(2, call + 1, right_out))
        return false;
    size_t len = 0u;
    if (PL_skip_list(call + 2, 0, &len) != PL_LIST)
        return false;
    factors->vars = PL_new_term_refs(len ? len : 1u);
    factors->values = PL_new_term_refs(len ? len : 1u);
    if (!factors->vars || !factors->values)
        return false;
    term_t list = PL_copy_term_ref(call + 2);
    term_t head = PL_new_term_ref();
    for (size_t i = 0u; i < len; i++) {
        if (!PL_get_list(list, head, list) ||
            !PL_get_arg(1, head, factors->vars + i) ||
            !PL_get_arg(2, head, factors->values + i))
            return false;
    }
    factors->order = cetta_malloc(sizeof(*factors->order) * (len ? len : 1u));
    factors->left_heads =
        cetta_malloc(sizeof(*factors->left_heads) * (len ? len : 1u));
    factors->right_heads =
        cetta_malloc(sizeof(*factors->right_heads) * (len ? len : 1u));
    for (size_t i = 0u; i < len; i++) {
        factors->order[i] = i;
        factors->left_heads[i] = SIZE_MAX;
        factors->right_heads[i] = SIZE_MAX;
    }
    factors->len = len;
    g_petta_libpl_sorting_factors = factors;
    qsort(factors->order, len, sizeof(*factors->order),
          petta_libpl_factor_order);
    g_petta_libpl_sorting_factors = NULL;
    return true;
}

typedef enum {
    PETTA_LIBPL_VALUES_EQUAL,
    PETTA_LIBPL_VALUES_DIFFERENT,
    PETTA_LIBPL_VALUES_UNDECIDED,
} PettaLibplValuesVerdict;

/* A tree walk past this many branching pairs goes over to the graph walk. */
#define PETTA_LIBPL_VALUES_TREE_STEPS (UINT32_C(1) << 16)

/* Walks a pair of terms, as trees when factors is NULL, else as the graphs
 * the factors describe, where a pair of nodes one of which is shared is
 * taken as equal when met again: the walk is a conjunction that stops at the
 * first difference, so such a pair is equal or still being compared, and a
 * shared subterm that holds a NaN still differs from itself the first time.
 * Every infinite path passes shared nodes, so the graph walk ends.  The tree
 * walk stops undecided past its step budget. */
static PettaLibplValuesVerdict petta_libpl_values_walk(
        term_t left_term, term_t right_term, PettaLibplFactors *factors,
        Arena **arena, Arena *scratch, bool *scratch_live) {
    PettaLibplTermPairs pending = {0};
    PettaLibplValuesVerdict verdict = PETTA_LIBPL_VALUES_EQUAL;
    uint32_t steps = 0u;
    petta_libpl_term_pairs_push(&pending, left_term, right_term);
    while (verdict == PETTA_LIBPL_VALUES_EQUAL && pending.len > 0u) {
        PettaLibplTermPair pair = pending.items[--pending.len];
        if (factors) {
            size_t left_factor = petta_libpl_factor_index(factors, pair.left);
            size_t right_factor =
                petta_libpl_factor_index(factors, pair.right);
            bool first_meeting = true;
            if (left_factor != SIZE_MAX && right_factor != SIZE_MAX)
                first_meeting = petta_libpl_factors_meet(
                    factors, left_factor, right_factor);
            else if (left_factor != SIZE_MAX)
                first_meeting = petta_libpl_factor_meets_node(
                    factors, factors->left_heads, left_factor, pair.right);
            else if (right_factor != SIZE_MAX)
                first_meeting = petta_libpl_factor_meets_node(
                    factors, factors->right_heads, right_factor, pair.left);
            if (!first_meeting)
                continue;
            if (left_factor != SIZE_MAX)
                pair.left = factors->values + left_factor;
            if (right_factor != SIZE_MAX)
                pair.right = factors->values + right_factor;
        }
        int left_type = PL_term_type(pair.left);
        int right_type = PL_term_type(pair.right);
        if (left_type == PL_VARIABLE || right_type == PL_VARIABLE) {
            if (left_type != right_type ||
                PL_compare(pair.left, pair.right) != 0)
                verdict = PETTA_LIBPL_VALUES_DIFFERENT;
            continue;
        }
        bool left_number = PL_is_number(pair.left);
        bool right_number = PL_is_number(pair.right);
        if (left_number || right_number) {
            if (left_number && right_number && !*arena) {
                arena_init(scratch);
                *scratch_live = true;
                *arena = scratch;
            }
            if (!left_number || !right_number ||
                !petta_libpl_numbers_equal(*arena, pair.left, pair.right))
                verdict = PETTA_LIBPL_VALUES_DIFFERENT;
            continue;
        }
        bool left_compound = PL_is_compound(pair.left);
        bool right_compound = PL_is_compound(pair.right);
        if (!left_compound || !right_compound) {
            if (left_compound || right_compound ||
                PL_compare(pair.left, pair.right) != 0)
                verdict = PETTA_LIBPL_VALUES_DIFFERENT;
            continue;
        }
        atom_t left_name = 0;
        atom_t right_name = 0;
        size_t left_arity = 0u;
        size_t right_arity = 0u;
        if (!PL_get_compound_name_arity_sz(
                pair.left, &left_name, &left_arity) ||
            !PL_get_compound_name_arity_sz(
                pair.right, &right_name, &right_arity) ||
            left_name != right_name || left_arity != right_arity) {
            verdict = PETTA_LIBPL_VALUES_DIFFERENT;
            continue;
        }
        /* The budget counts the pairs the walk branches into: a list's
         * elements, but not its spine. */
        bool branch = !PL_is_pair(pair.left);
        for (size_t index = left_arity; index >= 1u; index--) {
            term_t left_arg = PL_new_term_ref();
            term_t right_arg = PL_new_term_ref();
            if (!left_arg || !right_arg ||
                !PL_get_arg_sz(index, pair.left, left_arg) ||
                !PL_get_arg_sz(index, pair.right, right_arg)) {
                verdict = PETTA_LIBPL_VALUES_DIFFERENT;
                break;
            }
            if (index == 1u && PL_is_compound(left_arg))
                branch = true;
            petta_libpl_term_pairs_push(&pending, left_arg, right_arg);
        }
        if (!factors && branch && ++steps > PETTA_LIBPL_VALUES_TREE_STEPS)
            verdict = PETTA_LIBPL_VALUES_UNDECIDED;
    }
    free(pending.items);
    return verdict;
}

/* PeTTa's == and != for Prolog callers, deciding what metta.pl's same_value
 * decides: numbers by exact value (atom_value_eq), variables by identity and
 * never bound, other atomic terms as terms, and compounds by constructor and
 * arguments.  Terms that hold themselves, or share subterms too deeply for
 * their trees, compare by their unfolding: the terms are factored and the
 * graphs compared, each pair of shared nodes once.  No depth limit applies. */
static bool petta_libpl_values_equal(term_t left_term, term_t right_term) {
    fid_t frame = PL_open_foreign_frame();
    if (!frame)
        return false;
    Arena scratch;
    bool scratch_live = false;
    Arena *arena = g_petta_libpl_active_arena;
    PettaLibplValuesVerdict verdict =
        !PL_is_acyclic(left_term) || !PL_is_acyclic(right_term)
            ? PETTA_LIBPL_VALUES_UNDECIDED
            : petta_libpl_values_walk(left_term, right_term, NULL, &arena,
                                      &scratch, &scratch_live);
    if (verdict == PETTA_LIBPL_VALUES_UNDECIDED) {
        PettaLibplFactors factors = {0};
        term_t left_graph = PL_new_term_ref();
        term_t right_graph = PL_new_term_ref();
        verdict = left_graph && right_graph &&
                  petta_libpl_factorize(left_term, right_term, &factors,
                                        left_graph, right_graph)
            ? petta_libpl_values_walk(left_graph, right_graph, &factors,
                                      &arena, &scratch, &scratch_live)
            : PETTA_LIBPL_VALUES_DIFFERENT;
        free(factors.order);
        free(factors.met);
        free(factors.left_heads);
        free(factors.right_heads);
        free(factors.partners);
    }
    if (scratch_live)
        arena_free(&scratch);
    PL_discard_foreign_frame(frame);
    return verdict == PETTA_LIBPL_VALUES_EQUAL;
}

static foreign_t petta_libpl_standard_equal(
    term_t arguments, int supplied_arity,
    control_t control) {
    (void)control;
    if (!arguments || supplied_arity != 3)
        return false;
    bool equal = petta_libpl_values_equal(arguments, arguments + 1u);
    return PL_unify_atom_chars(
        arguments + 2u, equal ? "true" : "false");
}

static foreign_t petta_libpl_standard_not_equal(
    term_t arguments, int supplied_arity,
    control_t control) {
    (void)control;
    if (!arguments || supplied_arity != 3)
        return false;
    bool equal = petta_libpl_values_equal(arguments, arguments + 1u);
    return PL_unify_atom_chars(
        arguments + 2u, equal ? "false" : "true");
}

typedef struct {
    ResultSet results;
    CettaCount next;
} PettaLibplEvalState;

static void petta_libpl_eval_state_free(PettaLibplEvalState *state) {
    if (!state)
        return;
    result_set_free(&state->results);
    free(state);
}

static foreign_t petta_libpl_standard_eval(
    term_t arguments, int supplied_arity,
    control_t control) {
    if (!arguments || supplied_arity != 2)
        return false;
    int phase = PL_foreign_control(control);
    PettaLibplEvalState *state = NULL;
    if (phase == PL_PRUNED) {
        petta_libpl_eval_state_free(
            PL_foreign_context_address(control));
        return true;
    }
    if (phase == PL_REDO) {
        state = PL_foreign_context_address(control);
    } else if (phase == PL_FIRST_CALL) {
        if (!g_petta_libpl_active_arena)
            return false;
        PettaLibplVarMap variables = {0};
        PettaLibplBackVarMap unknown = {0};
        Atom *expression = petta_libpl_from_term(
            g_petta_libpl_active_arena, arguments,
            &variables, &unknown, 0u);
        petta_libpl_var_map_free(&variables);
        petta_libpl_back_map_free(&unknown);
        if (!expression)
            return false;
        state = cetta_malloc(sizeof(*state));
        memset(state, 0, sizeof(*state));
        if (!eval_petta_from_lib_prolog(
                g_petta_libpl_active_arena,
                expression, &state->results)) {
            free(state);
            return false;
        }
    } else {
        return false;
    }

    while (state && state->next < state->results.len) {
        Atom *answer = state->results.items[state->next++];
        term_t encoded = PL_new_term_ref();
        PettaLibplVarMap variables = {0};
        bool matched = encoded &&
            petta_libpl_to_term(answer, encoded, &variables, 0u) &&
            PL_unify(arguments + 1u, encoded);
        petta_libpl_var_map_free(&variables);
        if (!matched)
            continue;
        if (state->next < state->results.len)
            PL_retry_address(state);
        petta_libpl_eval_state_free(state);
        return true;
    }
    petta_libpl_eval_state_free(state);
    return false;
}

static foreign_t petta_libpl_standard_swrite(
    term_t arguments, int supplied_arity,
    control_t control) {
    (void)control;
    if (!arguments || supplied_arity != 2 ||
        !g_petta_libpl_active_arena) {
        return false;
    }
    PettaLibplVarMap variables = {0};
    PettaLibplBackVarMap unknown = {0};
    Atom *value = petta_libpl_from_term(
        g_petta_libpl_active_arena, arguments,
        &variables, &unknown, 0u);
    petta_libpl_var_map_free(&variables);
    petta_libpl_back_map_free(&unknown);
    if (!value)
        return false;
    /* PeTTa's swrite is the repr text, and it is UTF-8: handing SWI the
     * bytes as Latin-1 would turn every non-ASCII character into several. */
    char *rendered = atom_to_parseable_string_petta(
        g_petta_libpl_active_arena, value);
    return rendered &&
        PL_unify_chars(
            arguments + 1u, PL_STRING | REP_UTF8,
            strlen(rendered), rendered);
}

/*
 * The embedded engine does not load PeTTa's metta.pl.  Standard predicates
 * that are neither CeTTa-native nor supplied by SWI therefore live in this
 * explicit bridge table.  The table is also the installation invariant: a
 * row cannot advertise an arity without registering its implementation.
 */
static bool petta_libpl_install_standard_bridges(
    CettaLibPrologRuntime *runtime) {
    static const PettaLibplStandardBridge bridges[] = {
        {
            .name = "==",
            .function_arity = 2u,
            .implementation =
                (pl_function_t)petta_libpl_standard_equal,
        },
        {
            .name = "!=",
            .function_arity = 2u,
            .implementation =
                (pl_function_t)petta_libpl_standard_not_equal,
        },
        {
            .name = "eval",
            .function_arity = 1u,
            .implementation =
                (pl_function_t)petta_libpl_standard_eval,
            .nondeterministic = true,
        },
        {
            .name = "swrite",
            .function_arity = 1u,
            .implementation =
                (pl_function_t)petta_libpl_standard_swrite,
        },
        {
            .name = "py-call",
            .function_arity = 1u,
            .implementation =
                (pl_function_t)petta_libpl_grounded_bridge,
        },
    };
    if (!runtime || !runtime->module_name)
        return false;
    for (size_t index = 0u;
         index < sizeof(bridges) / sizeof(bridges[0]); index++) {
        const PettaLibplStandardBridge *bridge = &bridges[index];
        if (bridge->function_arity >= (size_t)INT_MAX ||
            !PL_register_foreign_in_module(
                runtime->module_name, bridge->name,
                (int)(bridge->function_arity + 1u),
                bridge->implementation,
                PL_FA_VARARGS |
                    (bridge->nondeterministic
                        ? PL_FA_NONDETERMINISTIC : 0))) {
            return false;
        }
    }
    return true;
}

/* Symbol-named PeTTa spaces are dynamic predicates in the embedded module.
 * The adapter deliberately does not load the reference's metta.pl/spaces.pl,
 * so install the one relational rule needed to enumerate rows of any arity.
 * Native/token spaces remain owned by CeTTa and never enter this clause. */
static bool petta_libpl_install_symbol_space_rules(
    CettaLibPrologRuntime *runtime) {
    static const char get_atoms_rule[] =
        "('get-atoms'(Space, Pattern) :- "
        "current_predicate(Space/Arity), "
        "functor(Head, Space, Arity), "
        "clause(Head, true), "
        "Head =.. [Space|Pattern])";
    if (!runtime || !runtime->module)
        return false;
    fid_t frame = PL_open_foreign_frame();
    if (!frame)
        return false;
    term_t rule = PL_new_term_ref();
    bool ok = rule &&
              PL_chars_to_term(get_atoms_rule, rule) &&
              PL_assert(rule, runtime->module, PL_ASSERTZ);
    PL_discard_foreign_frame(frame);
    return ok;
}

/* The reference prelude's own clauses for the registered functions this
 * module serves (SWI-PeTTa's metta.pl, which the module does not load), so
 * a call at a registered arity runs exactly the reference's definition.
 * Native routes own every other registered name. */
static bool petta_libpl_install_reference_prelude(
    CettaLibPrologRuntime *runtime) {
    static const char *const clauses[] = {
        "(exp(Arg, R) :- R is exp(Arg))",
        "bool(true)",
        "bool(false)",
        "(implies(A, B, C) :- bool(A), bool(B), "
        "(A == true -> (B == true -> C = true ; B == false -> C = false) "
        "; A == false -> C = true))",
        "('=?'(A, B, R) :- (\\+ \\+ A = B -> R = true ; R = false))",
        "(callPredicate(G, true) :- call(G))",
        /* CLP(FD) (metta.pl:126-142), in canonical syntax, so reading them
         * needs no operator of library(clpfd), which is loaded first. */
        "('#+'(A, B, R) :- '#='(R, A + B))",
        "('#-'(A, B, R) :- '#='(R, A - B))",
        "('#*'(A, B, R) :- '#='(R, A * B))",
        "('#div'(A, B, R) :- '#='(R, A div B))",
        "('#//'(A, B, R) :- '#='(R, A // B))",
        "('#mod'(A, B, R) :- '#='(R, A mod B))",
        "('#min'(A, B, R) :- '#='(R, min(A, B)))",
        "('#max'(A, B, R) :- '#='(R, max(A, B)))",
        "('#<'(A, B, true) :- '#<'(A, B), !)",
        "'#<'(_, _, false)",
        "('#>'(A, B, true) :- '#>'(A, B), !)",
        "'#>'(_, _, false)",
        "('#='(A, B, true) :- '#='(A, B), !)",
        "'#='(_, _, false)",
        "('#\\\\='(A, B, true) :- '#\\\\='(A, B), !)",
        "'#\\\\='(_, _, false)",
    };
    if (!runtime || !runtime->module)
        return false;
    fid_t frame = PL_open_foreign_frame();
    if (!frame)
        return false;
    term_t load = PL_new_term_ref();
    bool ok = load &&
              PL_chars_to_term("use_module(library(clpfd))", load) &&
              PL_call(load, runtime->module);
    for (size_t index = 0u;
         ok && index < sizeof(clauses) / sizeof(clauses[0]); index++) {
        term_t clause = PL_new_term_ref();
        ok = clause &&
             PL_chars_to_term(clauses[index], clause) &&
             PL_assert(clause, runtime->module, PL_ASSERTZ);
    }
    /* SWI names a procedure outside `user` with its module; the private
     * modules read back as `user`, so the warning that a load redefined one
     * of their procedures names it as the reference's warning does.  Once
     * per process: message_hook/3 is user's, shared by every runtime. */
    if (ok && !g_petta_libpl_message_hook_installed) {
        term_t hook = PL_new_term_ref();
        atom_t user_name = PL_new_atom("user");
        module_t user = PL_new_module(user_name);
        PL_unregister_atom(user_name);
        ok = hook && user &&
             PL_chars_to_term(
                 "(message_hook(redefined_procedure(Type, Module:Indicator), "
                 "warning, _) :- atom(Module), "
                 "sub_atom(Module, 0, _, _, cetta_prolog_), "
                 "print_message(warning, "
                 "redefined_procedure(Type, Indicator)))",
                 hook) &&
             PL_assert(hook, user, PL_ASSERTZ);
        g_petta_libpl_message_hook_installed = ok;
    }
    PL_discard_foreign_frame(frame);
    return ok;
}

static bool petta_libpl_plref_register(
    CettaLibPrologRuntime *runtime, term_t term,
    PettaLibplPlrefHandle *handle) {
    if (!runtime || !term || !handle ||
        runtime->instance_id == 0u ||
        runtime->instance_id > (uint64_t)INT64_MAX ||
        runtime->plref_next_generation == 0u ||
        runtime->plref_next_generation > (uint64_t)INT64_MAX) {
        return false;
    }
    record_t record = PL_record(term);
    if (!record)
        return false;

    size_t slot_index = runtime->plref_free_head;
    if (slot_index != SIZE_MAX) {
        runtime->plref_free_head =
            runtime->plrefs[slot_index].next_free;
    } else {
        if (runtime->plref_len == runtime->plref_cap) {
            size_t next = runtime->plref_cap
                ? runtime->plref_cap * 2u : 16u;
            if (next <= runtime->plref_cap ||
                next > SIZE_MAX / sizeof(*runtime->plrefs) ||
                next > (size_t)INT64_MAX) {
                PL_erase(record);
                return false;
            }
            PettaLibplPlrefSlot *grown = runtime->plrefs
                ? cetta_realloc(
                      runtime->plrefs,
                      sizeof(*runtime->plrefs) * next)
                : cetta_malloc(
                      sizeof(*runtime->plrefs) * next);
            if (!grown) {
                PL_erase(record);
                return false;
            }
            memset(
                grown + runtime->plref_cap, 0,
                sizeof(*grown) * (next - runtime->plref_cap));
            runtime->plrefs = grown;
            runtime->plref_cap = next;
        }
        slot_index = runtime->plref_len++;
    }

    uint64_t generation = runtime->plref_next_generation++;
    runtime->plrefs[slot_index] = (PettaLibplPlrefSlot){
        .record = record,
        .generation = generation,
        .next_free = SIZE_MAX,
        .live = true,
    };
    *handle = (PettaLibplPlrefHandle){
        .runtime_id = runtime->instance_id,
        .slot = slot_index,
        .generation = generation,
    };
    runtime->plref_live++;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PETTA_LIBPL_PLREF_REGISTER);
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_PETTA_LIBPL_PLREF_LIVE_PER_RUNTIME_PEAK,
        runtime->plref_live);
    return true;
}

static bool petta_libpl_plref_matches(
    CettaLibPrologRuntime *runtime,
    const PettaLibplPlrefHandle *handle) {
    if (!runtime || !handle ||
        handle->runtime_id != runtime->instance_id ||
        handle->slot >= runtime->plref_len) {
        return false;
    }
    PettaLibplPlrefSlot *slot =
        &runtime->plrefs[handle->slot];
    return slot->live && slot->record &&
           slot->generation == handle->generation;
}

static bool petta_libpl_plref_fetch(
    CettaLibPrologRuntime *runtime,
    const PettaLibplPlrefHandle *handle, term_t output) {
    if (!output || !petta_libpl_plref_matches(runtime, handle) ||
        PL_recorded(runtime->plrefs[handle->slot].record, output) == 0) {
        return false;
    }
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PETTA_LIBPL_PLREF_FETCH);
    return true;
}

static bool petta_libpl_plref_release(
    CettaLibPrologRuntime *runtime,
    const PettaLibplPlrefHandle *handle) {
    if (!petta_libpl_plref_matches(runtime, handle))
        return false;
    PettaLibplPlrefSlot *slot =
        &runtime->plrefs[handle->slot];
    PL_erase(slot->record);
    slot->record = 0;
    slot->live = false;
    slot->next_free = runtime->plref_free_head;
    runtime->plref_free_head = handle->slot;
    if (runtime->plref_live > 0u)
        runtime->plref_live--;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PETTA_LIBPL_PLREF_RELEASE);
    return true;
}

static void petta_libpl_plref_release_all(
    CettaLibPrologRuntime *runtime) {
    if (!runtime)
        return;
    for (size_t index = 0u; index < runtime->plref_len; index++) {
        PettaLibplPlrefSlot *slot = &runtime->plrefs[index];
        if (slot->live && slot->record) {
            PL_erase(slot->record);
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_PETTA_LIBPL_PLREF_RELEASE);
        }
        slot->record = 0;
        slot->live = false;
    }
    runtime->plref_len = 0u;
    runtime->plref_free_head = SIZE_MAX;
    runtime->plref_live = 0u;
}

static bool petta_libpl_debug_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0)
        enabled = getenv("CETTA_PETTA_LIBPL_DEBUG") ? 1 : 0;
    return enabled == 1;
}

static void petta_libpl_debug_named_arity(
    const char *which, SymbolId head, CettaExprLen supplied,
    PeTTaNamedArity result) {
    if (!petta_libpl_debug_enabled())
        return;
    const char *name =
        g_symbols ? symbol_bytes(g_symbols, head) : NULL;
    fprintf(stderr,
            "[petta-libpl] %s %s/%llu known=%d exact=%d larger=%d smaller=%d\n",
            which, name ? name : "?",
            (unsigned long long)supplied,
            result.known ? 1 : 0, result.exact ? 1 : 0,
            result.larger ? 1 : 0, result.smaller ? 1 : 0);
}

static bool petta_libpl_plref_view(
    Atom *atom, PettaLibplPlrefHandle *handle) {
    if (!atom || atom->kind != ATOM_EXPR ||
        atom->expr.len != 4u ||
        !atom->expr.elems[0] ||
        atom->expr.elems[0]->kind != ATOM_SYMBOL ||
        !atom->expr.elems[1] || !atom->expr.elems[2] ||
        !atom->expr.elems[3])
        return false;
    const char *name = symbol_bytes(
        g_symbols, atom->expr.elems[0]->sym_id);
    if (!name || strcmp(name, PETTA_LIBPL_PLREF_TAG) != 0)
        return false;
    for (CettaExprIndex index = 1u; index < 4u; index++) {
        Atom *part = atom->expr.elems[index];
        if (part->kind != ATOM_GROUNDED ||
            part->ground.gkind != GV_INT ||
            part->ground.ival <= 0) {
            return false;
        }
    }
    int64_t slot = atom->expr.elems[2]->ground.ival;
    if ((uint64_t)(slot - 1) > (uint64_t)SIZE_MAX)
        return false;
    if (handle) {
        *handle = (PettaLibplPlrefHandle){
            .runtime_id =
                (uint64_t)atom->expr.elems[1]->ground.ival,
            .slot = (size_t)(slot - 1),
            .generation =
                (uint64_t)atom->expr.elems[3]->ground.ival,
        };
    }
    return true;
}

static bool petta_libpl_plref_tagged(Atom *atom) {
    if (!atom || atom->kind != ATOM_EXPR ||
        atom->expr.len == 0u || !atom->expr.elems[0] ||
        atom->expr.elems[0]->kind != ATOM_SYMBOL) {
        return false;
    }
    const char *name = symbol_bytes(
        g_symbols, atom->expr.elems[0]->sym_id);
    return name && strcmp(name, PETTA_LIBPL_PLREF_TAG) == 0;
}

static Atom *petta_libpl_plref_atom(
    CettaLibPrologRuntime *runtime, Arena *arena,
    term_t term) {
    PettaLibplPlrefHandle handle;
    if (!runtime || !arena ||
        !petta_libpl_plref_register(runtime, term, &handle)) {
        return NULL;
    }
    Atom *items[4] = {
        atom_symbol(arena, PETTA_LIBPL_PLREF_TAG),
        atom_int(arena, (int64_t)handle.runtime_id),
        atom_int(arena, (int64_t)(handle.slot + 1u)),
        atom_int(arena, (int64_t)handle.generation),
    };
    for (size_t index = 0u; index < 4u; index++) {
        if (!items[index]) {
            (void)petta_libpl_plref_release(runtime, &handle);
            return NULL;
        }
    }
    Atom *result = atom_expr(arena, items, 4u);
    if (!result)
        (void)petta_libpl_plref_release(runtime, &handle);
    return result;
}

typedef struct {
#if defined(SIGSTKSZ) && defined(SA_ONSTACK)
    stack_t value;
    bool saved;
#endif
} PettaLibplHostAltStack;

/*
 * CeTTa owns the host thread's signal stack.  SWI allocates and selects a
 * private alternate stack while initialising each engine even when embedded
 * with --no-signals.  Preserve the host selection around those initialisers:
 * the engine retains ownership of its allocation, while short-lived CeTTa
 * workers keep the sanitizer/runtime stack they entered with.
 */
static bool petta_libpl_host_altstack_save(
    PettaLibplHostAltStack *saved) {
    if (!saved)
        return false;
#if defined(SIGSTKSZ) && defined(SA_ONSTACK)
    memset(saved, 0, sizeof(*saved));
    if (sigaltstack(NULL, &saved->value) != 0)
        return false;
    saved->saved = true;
#else
    (void)saved;
#endif
    return true;
}

static bool petta_libpl_host_altstack_restore(
    const PettaLibplHostAltStack *saved) {
    if (!saved)
        return false;
#if defined(SIGSTKSZ) && defined(SA_ONSTACK)
    return !saved->saved ||
           sigaltstack(&saved->value, NULL) == 0;
#else
    (void)saved;
    return true;
#endif
}

static uint64_t petta_libpl_import_hash(SymbolId symbol) {
    uint64_t value = (uint64_t)symbol +
                     UINT64_C(0x9e3779b97f4a7c15);
    value = (value ^ (value >> 30)) *
            UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) *
            UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}

static void petta_libpl_import_admission_clear(
    CettaLibPrologRuntime *runtime) {
    if (!runtime)
        return;
    atomic_store_explicit(
        &runtime->import_admission_saturated, true,
        memory_order_release);
    for (size_t index = 0u; index < 1024u; index++) {
        atomic_store_explicit(
            &runtime->import_admission[index], 0u,
            memory_order_release);
    }
    atomic_store_explicit(
        &runtime->import_admission_saturated, false,
        memory_order_release);
}

static bool petta_libpl_import_admission_add(
    CettaLibPrologRuntime *runtime, SymbolId symbol) {
    if (!runtime || symbol == SYMBOL_ID_NONE)
        return false;
    uint64_t hash = petta_libpl_import_hash(symbol);
    size_t slot = (size_t)(hash & 1023u);
    size_t stride = (size_t)(((hash >> 32) & 1023u) | 1u);
    for (size_t probe = 0u; probe < 1024u; probe++) {
        uint64_t current = atomic_load_explicit(
            &runtime->import_admission[slot],
            memory_order_relaxed);
        if (current == (uint64_t)symbol)
            return true;
        if (current == 0u) {
            atomic_store_explicit(
                &runtime->import_admission[slot],
                (uint64_t)symbol, memory_order_release);
            return true;
        }
        slot = (slot + stride) & 1023u;
    }
    atomic_store_explicit(
        &runtime->import_admission_saturated, true,
        memory_order_release);
    return false;
}

static bool petta_libpl_import_admission_maybe_contains(
    const CettaLibPrologRuntime *runtime, SymbolId symbol) {
    if (!runtime || symbol == SYMBOL_ID_NONE)
        return false;
    /*
     * Saturation is a performance state, never a semantic failure: readers
     * conservatively fall through to the locked registry when the derived
     * index cannot represent another name.
     */
    if (atomic_load_explicit(
            &runtime->import_admission_saturated,
            memory_order_acquire)) {
        return true;
    }
    uint64_t hash = petta_libpl_import_hash(symbol);
    size_t slot = (size_t)(hash & 1023u);
    size_t stride = (size_t)(((hash >> 32) & 1023u) | 1u);
    for (size_t probe = 0u; probe < 1024u; probe++) {
        uint64_t current = atomic_load_explicit(
            &runtime->import_admission[slot],
            memory_order_acquire);
        if (current == (uint64_t)symbol)
            return true;
        if (current == 0u)
            return false;
        slot = (slot + stride) & 1023u;
    }
    return true;
}

static void petta_libpl_global_cleanup(void) {
    if (!g_petta_libpl_ready)
        return;
    /* PL_cleanup runs in the main engine, so the thread that ends the run
     * takes it back first. */
    if (g_petta_libpl_main_released &&
        PL_set_engine(PL_ENGINE_MAIN, NULL) == PL_ENGINE_SET)
        g_petta_libpl_main_released = false;
    if (g_petta_libpl_worker_engine) {
        if (!PL_destroy_engine(g_petta_libpl_worker_engine)) {
            fputs(
                "fatal: failed to destroy pooled libpl engine\n",
                stderr);
            abort();
        }
        g_petta_libpl_worker_engine = NULL;
    }
    /* The memory stays until the process ends, as with halt/1: a worker
     * thread that crossed may still end after this, and its thread-exit
     * handlers must not meet freed engine state. */
    if (g_petta_libpl_owns_engine)
        (void)PL_cleanup(PL_CLEANUP_NO_CANCEL | PL_CLEANUP_NO_RECLAIM_MEMORY);
    g_petta_libpl_ready = false;
    g_petta_libpl_owns_engine = false;
}

void cetta_lib_prolog_global_shutdown(void) {
    if (pthread_mutex_lock(&g_petta_libpl_lock) != 0)
        return;
    petta_libpl_global_cleanup();
    (void)pthread_mutex_unlock(&g_petta_libpl_lock);
}

static void petta_libpl_global_init(void) {
    atomic_store_explicit(&g_petta_libpl_started, true,
                          memory_order_release);
    if (PL_is_initialised(NULL, NULL)) {
        g_petta_libpl_ready = true;
    } else {
        static char program[] = "cetta-libpl";
        static char quiet[] = "-q";
        static char no_signals[] = "--no-signals";
        static char no_alert_signal[] = "--sigalert=0";
        static char no_init[] = "-f";
        static char no_init_file[] = "none";
        char *arguments[] = {
            program, quiet, no_signals, no_alert_signal,
            no_init, no_init_file, NULL,
        };
        PettaLibplHostAltStack host_altstack;
        if (!petta_libpl_host_altstack_save(&host_altstack))
            return;
        bool initialised = PL_initialise(6, arguments);
        bool restored =
            petta_libpl_host_altstack_restore(&host_altstack);
        if (!initialised || !restored) {
            if (initialised)
                (void)PL_cleanup(PL_CLEANUP_NO_CANCEL);
            return;
        }
        g_petta_libpl_ready = true;
        if (!g_petta_libpl_ready)
            return;
        g_petta_libpl_owns_engine = true;
        /* A parallel worker that starts the engine does not keep SWI's main
         * engine: it may outlive nothing (a one-shot worker) or idle until
         * the run ends (a pooled one), and the run's end must be able to
         * stop every Prolog thread.  It gives the engine back; every
         * crossing then claims the pooled engine, as a worker always does,
         * and the cleanup takes the main engine back. */
        if (cetta_parallel_worker_active() &&
            PL_set_engine(NULL, NULL) == PL_ENGINE_SET)
            g_petta_libpl_main_released = true;
    }

    if (atexit(petta_libpl_global_cleanup) != 0) {
        petta_libpl_global_cleanup();
    }
}

/* Every entry into the engine may run Prolog that changes its flags, so
 * values read from the engine are cached only until the next entry. */
static _Atomic uint64_t g_petta_libpl_entries;

static bool petta_libpl_enter(bool *claimed) {
    if (claimed)
        *claimed = false;
    if (!claimed)
        return false;
    atomic_fetch_add_explicit(&g_petta_libpl_entries, 1u,
                              memory_order_acq_rel);
    if (g_petta_libpl_enter_depth > 0u) {
        if (g_petta_libpl_enter_depth == UINT32_MAX)
            return false;
        g_petta_libpl_enter_depth++;
        return true;
    }
    if (
        pthread_once(
            &g_petta_libpl_once,
            petta_libpl_global_init) != 0 ||
        !g_petta_libpl_ready ||
        pthread_mutex_lock(&g_petta_libpl_lock) != 0) {
        return false;
    }
    if (!PL_current_engine()) {
        /*
         * Keep the optional foreign boundary absent from native-only PeTTa
         * executions.  SWI's many-to-many engine interface is designed for
         * infrequent calls from a larger native worker pool: create one
         * detached engine on the first actual crossing, then claim it under
         * this lock for each boundary call.  This avoids both eager engine
         * construction and per-worker attach/destroy churn.
         */
        if (!g_petta_libpl_worker_engine) {
            PettaLibplHostAltStack host_altstack;
            if (!petta_libpl_host_altstack_save(
                    &host_altstack)) {
                (void)pthread_mutex_unlock(
                    &g_petta_libpl_lock);
                return false;
            }
            PL_engine_t engine = PL_create_engine(NULL);
            bool restored =
                petta_libpl_host_altstack_restore(
                    &host_altstack);
            if (!restored) {
                if (engine)
                    (void)PL_destroy_engine(engine);
                (void)pthread_mutex_unlock(
                    &g_petta_libpl_lock);
                return false;
            }
            g_petta_libpl_worker_engine = engine;
        }
        if (!g_petta_libpl_worker_engine ||
            PL_set_engine(
                g_petta_libpl_worker_engine,
                NULL) != PL_ENGINE_SET) {
            (void)pthread_mutex_unlock(
                &g_petta_libpl_lock);
            return false;
        }
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_LIB_PROLOG_ENGINE_CLAIM);
        *claimed = true;
    }
    g_petta_libpl_enter_depth = 1u;
    return true;
}

static void petta_libpl_leave(bool claimed) {
    if (g_petta_libpl_enter_depth == 1u && !PL_current_query())
        petta_solver_collect_retired();
    if (g_petta_libpl_enter_depth == 0u) {
        fputs("fatal: unbalanced libpl boundary leave\n", stderr);
        abort();
    }
    g_petta_libpl_enter_depth--;
    if (g_petta_libpl_enter_depth > 0u)
        return;
    if (claimed) {
        if (PL_set_engine(NULL, NULL) != PL_ENGINE_SET) {
            fputs(
                "fatal: failed to release libpl worker engine\n",
                stderr);
            abort();
        }
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_LIB_PROLOG_ENGINE_RELEASE);
    }
    (void)pthread_mutex_unlock(&g_petta_libpl_lock);
}

static char *petta_libpl_copy_bytes(
    const char *bytes, size_t length) {
    if (!bytes || length == SIZE_MAX)
        return NULL;
    char *copy = cetta_malloc(length + 1u);
    if (length)
        memcpy(copy, bytes, length);
    copy[length] = '\0';
    return copy;
}

/*
 * CeTTa symbols and strings are UTF-8 byte sequences.  SWI's legacy
 * *_nchars entry points do not declare that representation, so every
 * dynamic text crossing declares UTF-8 explicitly.  Keep the policy in
 * these helpers so new bridge call sites cannot accidentally choose a
 * different representation.
 */
static bool petta_libpl_put_utf8(
    term_t output, int kind,
    const char *bytes, size_t length) {
    return output && bytes &&
           PL_put_chars(
               output, kind | REP_UTF8,
               length, bytes);
}

static atom_t petta_libpl_new_utf8_atom(
    const char *bytes, size_t length) {
    return bytes
        ? PL_new_atom_mbchars(REP_UTF8, length, bytes)
        : 0;
}

static bool petta_libpl_atom_utf8(
    atom_t atom, char **bytes, size_t *length) {
    return atom && bytes && length &&
           PL_atom_mbchars(
               atom, length, bytes,
               BUF_MALLOC | REP_UTF8);
}

static bool petta_libpl_install_working_dir(
    CettaLibPrologRuntime *runtime, const char *path,
    bool replace) {
    if (!runtime || !runtime->module || !path)
        return false;
    fid_t frame = PL_open_foreign_frame();
    if (!frame)
        return false;

    atom_t name = PL_new_atom("working_dir");
    functor_t functor = name ? PL_new_functor(name, 1u) : 0;
    bool ok = functor != 0;
    if (ok && replace) {
        term_t pattern_arg = PL_new_term_ref();
        term_t pattern = PL_new_term_ref();
        predicate_t retractall1 =
            PL_predicate("retractall", 1, "system");
        ok = pattern_arg && pattern && retractall1 &&
             PL_put_variable(pattern_arg) &&
             PL_cons_functor_v(pattern, functor, pattern_arg) &&
             PL_call_predicate(
                 runtime->module, PL_Q_NODEBUG,
                 retractall1, pattern);
    }
    if (ok) {
        term_t path_term = PL_new_term_ref();
        term_t fact = PL_new_term_ref();
        ok = path_term && fact &&
             petta_libpl_put_utf8(
                 path_term, PL_ATOM,
                 path, strlen(path)) &&
             PL_cons_functor_v(fact, functor, path_term) &&
             PL_assert(fact, runtime->module, PL_ASSERTZ);
    }
    if (name)
        PL_unregister_atom(name);
    PL_discard_foreign_frame(frame);
    return ok;
}

/*
 * The process-wide SWI engine and each context-private module are both
 * demand-created.  This function runs only while g_petta_libpl_lock is held
 * and the calling thread owns an engine.
 */
static bool petta_libpl_refresh_arities(
    CettaLibPrologRuntime *runtime, PettaLibplImport *entry);

static bool petta_libpl_prepare_locked(
    CettaLibPrologRuntime *runtime) {
    if (!runtime)
        return false;
    if (runtime->prepared)
        return true;

    uint64_t number = runtime->instance_id;
    if (number == 0u || number > (uint64_t)INT64_MAX)
        return false;
    char name[96];
    int length = snprintf(
        name, sizeof(name),
        "cetta_prolog_%llu",
        (unsigned long long)number);
    if (length <= 0 || (size_t)length >= sizeof(name))
        return false;

    char *module_name =
        petta_libpl_copy_bytes(name, (size_t)length);
    if (!module_name)
        return false;
    atom_t module_symbol =
        petta_libpl_new_utf8_atom(name, (size_t)length);
    module_t module = module_symbol
        ? PL_new_module(module_symbol) : NULL;
    if (module_symbol)
        PL_unregister_atom(module_symbol);
    if (!module) {
        free(module_name);
        return false;
    }

    runtime->module_name = module_name;
    runtime->module_name_len = (size_t)length;
    runtime->module = module;

    /* PeTTa fixes working_dir/1 to the top-level source directory. */
    if (!petta_libpl_install_working_dir(
            runtime,
            runtime->working_dir ? runtime->working_dir : ".",
            false)) {
        return false;
    }

    runtime->symbol_table_instance =
        symbol_table_instance_id(g_symbols);
    if (!petta_libpl_install_standard_bridges(runtime) ||
        !petta_libpl_install_symbol_space_rules(runtime) ||
        !petta_libpl_install_reference_prelude(runtime))
        return false;
    petta_libpl_register_reference_stdlib(runtime);
    runtime->prepared = true;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_LIB_PROLOG_PREPARE);
    return true;
}

static bool petta_libpl_symbol_is(
    SymbolId symbol, const char *name) {
    return g_symbols && name &&
           symbol_eq_cstr(g_symbols, symbol, name);
}

static bool petta_libpl_atom_is_symbol(
    const Atom *atom, const char *name) {
    return atom && atom->kind == ATOM_SYMBOL &&
           petta_libpl_symbol_is(atom->sym_id, name);
}

static void petta_libpl_import_clear(
    PettaLibplImport *entry) {
    if (!entry)
        return;
    free(entry->name);
    free(entry->function_arities);
    memset(entry, 0, sizeof(*entry));
}

static void petta_libpl_sync_symbol_table(
    CettaLibPrologRuntime *runtime) {
    if (!runtime)
        return;
    uint64_t current =
        symbol_table_instance_id(g_symbols);
    if (runtime->symbol_table_instance == current)
        return;
    for (size_t index = 0u;
         index < runtime->import_len; index++) {
        petta_libpl_import_clear(
            &runtime->imports[index]);
    }
    runtime->import_len = 0u;
    free(runtime->callables);
    runtime->callables = NULL;
    runtime->callable_len = runtime->callable_cap = 0u;
    arena_free(&runtime->callable_codes);
    arena_init_detached(&runtime->callable_codes);
    petta_libpl_import_admission_clear(runtime);
    runtime->symbol_table_instance = current;
    petta_libpl_advance_revision(runtime);
}

static PettaLibplImport *petta_libpl_find_import(
    CettaLibPrologRuntime *runtime, SymbolId symbol) {
    if (!runtime || symbol == SYMBOL_ID_NONE)
        return NULL;
    petta_libpl_sync_symbol_table(runtime);
    for (size_t index = 0u;
         index < runtime->import_len; index++) {
        PettaLibplImport *entry =
            &runtime->imports[index];
        if (entry->symbol_table_instance ==
                runtime->symbol_table_instance &&
            entry->symbol == symbol) {
            return entry;
        }
    }
    return NULL;
}

/*
 * PeTTa's language basis contains a small set of Prolog-backed functions
 * that are callable without an authored import.  Keep this boundary explicit:
 * arbitrary unknown MeTTa heads remain inert, while the listed translator
 * predicates may use the optional adapter.
 */
static size_t petta_libpl_standard_function_arities(
    SymbolId symbol, size_t arities[2]) {
    if (arities) {
        arities[0] = 0u;
        arities[1] = 0u;
    }
    if (!g_symbols || symbol == SYMBOL_ID_NONE)
        return 0u;
    const char *name = symbol_bytes(g_symbols, symbol);
    if (name && strcmp(name, "exists_file") == 0) {
        if (arities)
            arities[0] = 0u;
        return 1u;
    }
    if (name && strcmp(name, "library") == 0) {
        if (arities) {
            arities[0] = 1u;
            arities[1] = 2u;
        }
        return 2u;
    }
    return 0u;
}

static PettaLibplImport *petta_libpl_register_import(
    CettaLibPrologRuntime *runtime, SymbolId symbol) {
    PettaLibplImport *existing =
        petta_libpl_find_import(runtime, symbol);
    if (existing)
        return existing;
    if (!runtime || !g_symbols ||
        symbol == SYMBOL_ID_NONE) {
        return NULL;
    }

    if (runtime->import_len == runtime->import_cap) {
        size_t next =
            runtime->import_cap
                ? runtime->import_cap * 2u : 16u;
        if (next <= runtime->import_cap ||
            next > SIZE_MAX / sizeof(*runtime->imports)) {
            return NULL;
        }
        runtime->imports = runtime->imports
            ? cetta_realloc(
                  runtime->imports,
                  sizeof(*runtime->imports) * next)
            : cetta_malloc(
                  sizeof(*runtime->imports) * next);
        memset(
            runtime->imports + runtime->import_cap, 0,
            sizeof(*runtime->imports) *
                (next - runtime->import_cap));
        runtime->import_cap = next;
    }

    uint32_t length = symbol_len(g_symbols, symbol);
    const char *name = symbol_bytes(g_symbols, symbol);
    char *copy = petta_libpl_copy_bytes(
        name, (size_t)length);
    if (!copy)
        return NULL;
    PettaLibplImport *entry =
        &runtime->imports[runtime->import_len++];
    *entry = (PettaLibplImport){
        .symbol = symbol,
        .symbol_table_instance =
            runtime->symbol_table_instance,
        .name = copy,
        .name_len = length,
    };
    (void)petta_libpl_import_admission_add(runtime, symbol);
    petta_libpl_advance_revision(runtime);
    return entry;
}

static bool petta_libpl_import_add_arity(
    PettaLibplImport *entry, size_t arity) {
    if (!entry)
        return false;
    for (size_t index = 0u;
         index < entry->arity_len; index++) {
        if (entry->function_arities[index] == arity)
            return true;
    }
    if (entry->arity_len == entry->arity_cap) {
        size_t next =
            entry->arity_cap ? entry->arity_cap * 2u : 4u;
        if (next <= entry->arity_cap ||
            next > SIZE_MAX /
                sizeof(*entry->function_arities)) {
            return false;
        }
        entry->function_arities =
            entry->function_arities
                ? cetta_realloc(
                      entry->function_arities,
                      sizeof(*entry->function_arities) * next)
                : cetta_malloc(
                      sizeof(*entry->function_arities) * next);
        entry->arity_cap = next;
    }
    entry->function_arities[entry->arity_len++] =
        arity;
    return true;
}

static bool petta_libpl_make_indicator(
    const PettaLibplImport *entry,
    term_t indicator, term_t arity) {
    if (!entry || !indicator || !arity)
        return false;
    term_t arguments = PL_new_term_refs(2u);
    if (!arguments ||
        !petta_libpl_put_utf8(
            arguments, PL_ATOM,
            entry->name, entry->name_len) ||
        !PL_put_variable(arguments + 1u)) {
        return false;
    }
    atom_t slash = PL_new_atom("/");
    functor_t indicator_functor =
        PL_new_functor(slash, 2u);
    bool built =
        indicator_functor &&
        PL_cons_functor_v(
            indicator, indicator_functor, arguments) &&
        /*
         * Read the variable back from the constructed indicator.  The FLI
         * copies the argument vector into the compound; retaining the
         * pre-construction reference is not a portable way to observe the
         * binding produced by current_predicate/1.
         */
        PL_get_arg(2, indicator, arity);
    PL_unregister_atom(slash);
    return built;
}

static bool petta_libpl_refresh_arities(
    CettaLibPrologRuntime *runtime,
    PettaLibplImport *entry) {
    if (!runtime || !entry)
        return false;
    if (entry->scanned_revision == runtime->revision)
        return true;

    entry->arity_len = 0u;
    fid_t frame = PL_open_foreign_frame();
    term_t indicator = PL_new_term_ref();
    term_t arity = PL_new_term_ref();
    if (!frame || !indicator || !arity ||
        !petta_libpl_make_indicator(
            entry, indicator, arity)) {
        if (frame)
            PL_discard_foreign_frame(frame);
        return false;
    }

    predicate_t current_predicate =
        PL_predicate("current_predicate", 1, NULL);
    qid_t query = PL_open_query(
        runtime->module,
        PL_Q_NODEBUG | PL_Q_CATCH_EXCEPTION |
            PL_Q_EXT_STATUS,
        current_predicate, indicator);
    if (!query) {
        PL_discard_foreign_frame(frame);
        return false;
    }

    bool ok = true;
    for (;;) {
        int status = PL_next_solution(query);
        if (status == PL_S_FALSE)
            break;
        if (status == PL_S_EXCEPTION ||
            status == PL_S_YIELD ||
            status == PL_S_YIELD_DEBUG ||
            (status != PL_S_TRUE &&
             status != PL_S_LAST)) {
            ok = false;
            break;
        }
        int64_t predicate_arity = 0;
        if (!PL_get_int64(arity, &predicate_arity) ||
            predicate_arity <= 0 ||
            (uint64_t)predicate_arity >
                (uint64_t)SIZE_MAX ||
            !petta_libpl_import_add_arity(
                entry,
                (size_t)predicate_arity - 1u)) {
            ok = false;
            break;
        }
        if (status == PL_S_LAST)
            break;
    }
    /* Internal fixed-predicate scan: a release fault invalidates the scan
     * (next revision rescans rather than trusting a half-released query).
     * The release is unconditional: the query must discharge even after a
     * scan failure. */
    const PettaLibplReleaseStatus release_status =
        petta_libpl_release_query(NULL, NULL, query,
                                  PETTA_LIBPL_RELEASE_CLOSE);
    ok = ok && release_status == PETTA_LIBPL_RELEASE_OK;
    PL_discard_foreign_frame(frame);
    if (ok)
        entry->scanned_revision = runtime->revision;
    return ok;
}

static bool petta_libpl_probe_miss_contains(
    CettaLibPrologRuntime *runtime, SymbolId head) {
    if (!runtime ||
        runtime->probe_miss_revision != runtime->revision)
        return false;
    for (size_t index = 0u;
         index < runtime->probe_miss_len; index++) {
        if (runtime->probe_misses[index] == head)
            return true;
    }
    return false;
}

static void petta_libpl_probe_miss_add(
    CettaLibPrologRuntime *runtime, SymbolId head) {
    if (!runtime)
        return;
    if (runtime->probe_miss_revision != runtime->revision) {
        runtime->probe_miss_len = 0u;
        runtime->probe_miss_revision = runtime->revision;
    }
    if (runtime->probe_miss_len == runtime->probe_miss_cap) {
        size_t next = runtime->probe_miss_cap
            ? runtime->probe_miss_cap * 2u : 64u;
        if (next > SIZE_MAX / sizeof(SymbolId))
            return;
        SymbolId *grown = runtime->probe_misses
            ? cetta_realloc(
                  runtime->probe_misses,
                  sizeof(SymbolId) * next)
            : cetta_malloc(sizeof(SymbolId) * next);
        if (!grown)
            return;
        runtime->probe_misses = grown;
        runtime->probe_miss_cap = next;
    }
    runtime->probe_misses[runtime->probe_miss_len++] = head;
}

/* arity/2 rows are the reference's own escape hatch for autoload targets
 * that current_predicate/1 will not enumerate: its call compiler accepts
 * `current_predicate(F/A) ; arity(F, A)`, and PeTTaChainer asserts such
 * rows for the heap library.  The stored arity is the predicate arity;
 * the function convention drops the appended result argument. */
static void petta_libpl_probe_arity_facts(
    CettaLibPrologRuntime *runtime,
    PettaLibplImport *entry) {
    if (!runtime || !entry)
        return;
    fid_t frame = PL_open_foreign_frame();
    if (!frame)
        return;
    term_t arguments = PL_new_term_refs(2u);
    if (!arguments ||
        !petta_libpl_put_utf8(
            arguments, PL_ATOM,
            entry->name, entry->name_len) ||
        !PL_put_variable(arguments + 1u)) {
        PL_discard_foreign_frame(frame);
        return;
    }
    predicate_t arity_predicate =
        PL_predicate("arity", 2, runtime->module_name);
    qid_t query = PL_open_query(
        runtime->module,
        PL_Q_NODEBUG | PL_Q_CATCH_EXCEPTION |
            PL_Q_EXT_STATUS,
        arity_predicate, arguments);
    if (!query) {
        PL_discard_foreign_frame(frame);
        return;
    }
    for (;;) {
        int status = PL_next_solution(query);
        if (status != PL_S_TRUE && status != PL_S_LAST)
            break;
        int64_t predicate_arity = 0;
        if (PL_get_int64(
                arguments + 1u, &predicate_arity) &&
            predicate_arity > 0 &&
            (uint64_t)predicate_arity <=
                (uint64_t)SIZE_MAX) {
            (void)petta_libpl_import_add_arity(
                entry, (size_t)predicate_arity - 1u);
        }
        if (status == PL_S_LAST)
            break;
    }
    /* arity/2 rows are internal; the release is unconditional (discharge
     * even after a partial scan) and unconditionally cleared by the helper.
     */
    (void)petta_libpl_release_query(NULL, NULL, query,
                                    PETTA_LIBPL_RELEASE_CLOSE);
    PL_discard_foreign_frame(frame);
}

/*
 * The reference's call dispatcher accepts a bare name only when fun(F)
 * holds — its curated stdlib registration list plus explicit
 * import_prolog_function calls — and only then consults
 * current_predicate/1 for the concrete arity.  Mirror the registration
 * side here: the stdlib syntax below is metta.pl's register_fun list
 * verbatim, minus the names cetta's native machine and evaluator own
 * (forms, grounded operations, shared builtin syntax), whose engine
 * spellings must never shadow native ownership.  cons is excluded exactly
 * as the reference translator excludes it.  Their arities are those the
 * reference's registration records (petta_libpl_reference_stdlib_arity).
 */
static void petta_libpl_register_reference_stdlib(
    CettaLibPrologRuntime *runtime) {
    static const char *const reference_stdlib[] = {
        "superpose", "empty", "let", "let*", "+", "-", "*", "/", "%",
        "min", "max", "change-state!", "get-state", "bind!",
        "<", ">", "==", "!=", "=", "=?", "<=", ">=",
        "and", "or", "xor", "implies", "not",
        "sqrt", "exp", "log", "cos", "sin",
        "first-from-pair", "second-from-pair", "car-atom", "cdr-atom",
        "unique-atom", "alpha-unique-atom",
        "repr", "repra", "parse", "println!", "readln!", "test",
        "assert", "mm2-exec", "atom_concat", "atom_chars", "copy_term",
        "term_hash", "foldl", "first", "last", "append", "length",
        "size-atom", "sort", "msort", "member", "is-member",
        "is-alpha-member", "exclude-item", "list_to_set", "maplist",
        "eval", "reduce", "import!",
        "add-atom", "remove-atom", "get-atoms", "match", "is-var",
        "is-ground", "is-expr", "is-space", "get-mettatype",
        "decons", "decons-atom", "py-call", "get-type", "get-metatype",
        "=alpha", "concat", "sread", "reverse",
        "#+", "#-", "#*", "#div", "#//", "#mod", "#min", "#max",
        "#<", "#>", "#=", "#\\=", "set_hook",
        "union-atom", "cons-atom", "intersection-atom",
        "subtraction-atom", "index-atom", "id",
        "pow-math", "sqrt-math", "sort-atom", "abs-math", "log-math",
        "trunc-math", "ceil-math", "floor-math", "round-math",
        "sin-math", "cos-math", "tan-math", "asin-math", "random-int",
        "random-float", "acos-math", "atan-math", "isnan-math",
        "isinf-math", "min-atom", "max-atom",
        "foldl-atom", "map-atom", "filter-atom", "current-time",
        "format-time", "library", "exists_file", "library-import!",
        "import_prolog_function", "Predicate", "callPredicate",
        "assertaPredicate", "assertzPredicate", "retractPredicate",
        "add-translator-rule!", "remove-translator-rule!",
    };
    if (!runtime || !g_symbols)
        return;
    for (size_t index = 0u;
         index < sizeof(reference_stdlib) /
                     sizeof(reference_stdlib[0]);
         index++) {
        const char *name = reference_stdlib[index];
        SymbolId symbol = symbol_intern_bytes(
            g_symbols, (const uint8_t *)name, (uint32_t)strlen(name));
        if (symbol == SYMBOL_ID_NONE ||
            petta_semantics_form(symbol) != PETTA_FORM_NONE ||
            is_grounded_op(symbol) ||
            symbol_id_is_builtin(symbol) ||
            petta_semantics_is_mm2_exec(symbol))
            continue;
        PettaLibplImport *entry =
            petta_libpl_register_import(runtime, symbol);
        if (entry)
            entry->reference_stdlib = true;
    }
}

/*
 * Engine-existence probing is a MATCH concern, not a function-name rule: a
 * symbol space's dynamic predicate is never fun-registered on the
 * reference either — match enumerates it directly.  The probe therefore
 * serves only the match-lowering existence gate (and the auto entries it
 * registers stay excluded from every call seam).  A name with no evidence
 * is unregistered again and recorded as a miss for the current revision.
 * Callers hold the engine and the runtime lock.
 */
static PettaLibplImport *petta_libpl_probe_system_function(
    CettaLibPrologRuntime *runtime, SymbolId head) {
    if (!runtime || head == SYMBOL_ID_NONE)
        return NULL;
    /*
     * Adapter forms, grounded operations, and shared builtin syntax names
     * are owned by the native machine and evaluator: whatever helper
     * predicates the engine module happens to define under the same
     * spelling, resolving them as foreign functions would shadow that
     * ownership (callPredicate and assertz are the motivating cases — the
     * latter turned reified Predicate bodies into evaluated calls).  So is
     * mm2-exec, which the evaluator runs on the MORK space.  Only genuinely
     * free names participate in the engine-existence rule.
     */
    if (petta_semantics_form(head) != PETTA_FORM_NONE ||
        is_grounded_op(head) ||
        symbol_id_is_builtin(head) ||
        petta_semantics_is_mm2_exec(head))
        return NULL;
    PettaLibplImport *existing =
        petta_libpl_find_import(runtime, head);
    if (existing)
        return existing;
    if (petta_libpl_probe_miss_contains(runtime, head))
        return NULL;
    size_t previous_len = runtime->import_len;
    PettaLibplImport *entry =
        petta_libpl_register_import(runtime, head);
    if (!entry)
        return NULL;
    entry->auto_resolved = true;
    (void)petta_libpl_refresh_arities(runtime, entry);
    petta_libpl_probe_arity_facts(runtime, entry);
    if (entry->arity_len > 0u)
        return entry;
    if (runtime->import_len == previous_len + 1u &&
        entry == &runtime->imports[previous_len]) {
        free(entry->name);
        free(entry->function_arities);
        memset(entry, 0, sizeof(*entry));
        runtime->import_len = previous_len;
    }
    petta_libpl_probe_miss_add(runtime, head);
    return NULL;
}

static PettaLibplVar *petta_libpl_var_find(
    PettaLibplVarMap *variables, VarId id) {
    if (!variables || id == VAR_ID_NONE)
        return NULL;
    size_t index = cetta_var_index_find_records(
        &variables->index, variables->items, sizeof(*variables->items),
        variables->len, id);
    return index == SIZE_MAX ? NULL : &variables->items[index];
}

static PettaLibplVar *petta_libpl_var_add(
    PettaLibplVarMap *variables, Atom *variable) {
    if (!variables || !variable ||
        variable->kind != ATOM_VAR) {
        return NULL;
    }
    PettaLibplVar *existing =
        petta_libpl_var_find(
            variables, variable->var_id);
    if (existing)
        return existing;
    if (variables->len == variables->cap) {
        size_t next =
            variables->cap ? variables->cap * 2u : 16u;
        if (next <= variables->cap ||
            next > SIZE_MAX / sizeof(*variables->items)) {
            return NULL;
        }
        variables->items = variables->items
            ? cetta_realloc(
                  variables->items,
                  sizeof(*variables->items) * next)
            : cetta_malloc(
                  sizeof(*variables->items) * next);
        variables->cap = next;
    }
    term_t term = variables->slot
        ? variables->slot(variables->slot_context, variable) : PL_new_term_ref();
    if (!term || (!variables->slot && !PL_put_variable(term)))
        return NULL;
    PettaLibplVar *entry =
        &variables->items[variables->len++];
    *entry = (PettaLibplVar){
        .id = variable->var_id,
        .prototype = variable,
        .term = term,
    };
    if (!cetta_var_index_note_records(
            &variables->index, variables->items, sizeof(*variables->items),
            variables->len)) {
        variables->len--;
        return NULL;
    }
    return entry;
}

static bool petta_libpl_to_term(
    Atom *atom, term_t output,
    PettaLibplVarMap *variables, uint32_t depth);

static size_t petta_libpl_callable_slot(
    const PettaLibplCallable *entries, size_t capacity, SymbolId name) {
    size_t slot = ((uint64_t)name * UINT64_C(11400714819323198485)) & (capacity - 1u);
    while (entries[slot].name != SYMBOL_ID_NONE && entries[slot].name != name)
        slot = (slot + 1u) & (capacity - 1u);
    return slot;
}

static Atom *petta_libpl_callable_find(CettaLibPrologRuntime *runtime, SymbolId name) {
    if (!runtime || !runtime->callable_cap || name == SYMBOL_ID_NONE)
        return NULL;
    size_t slot = petta_libpl_callable_slot(runtime->callables, runtime->callable_cap, name);
    return runtime->callables[slot].name == name ? runtime->callables[slot].code : NULL;
}

static SymbolId petta_libpl_callable_name(CettaLibPrologRuntime *runtime, Atom *code) {
    int64_t identity = 0;
    if (!runtime || !atom_petta_callable_identity(code, &identity))
        return SYMBOL_ID_NONE;
    char spelling[64];
    int length = snprintf(spelling, sizeof(spelling), "lambda_%llu",
                          (unsigned long long)identity);
    if (length <= 0 || (size_t)length >= sizeof(spelling))
        return SYMBOL_ID_NONE;
    SymbolId name = symbol_intern_bytes(g_symbols, (const uint8_t *)spelling, (uint32_t)length);
    if (name == SYMBOL_ID_NONE)
        return name;
    Atom *existing = petta_libpl_callable_find(runtime, name);
    if (existing)
        return atom_eq(existing, code) ? name : SYMBOL_ID_NONE;
    if (!runtime->callable_cap || (runtime->callable_len + 1u) * 10u > runtime->callable_cap * 7u) {
        size_t capacity = runtime->callable_cap ? runtime->callable_cap * 2u : 16u;
        if (capacity <= runtime->callable_cap || capacity > SIZE_MAX / sizeof(*runtime->callables))
            return SYMBOL_ID_NONE;
        PettaLibplCallable *entries = calloc(capacity, sizeof(*entries));
        if (!entries)
            return SYMBOL_ID_NONE;
        for (size_t i = 0u; i < runtime->callable_cap; i++) {
            if (runtime->callables[i].name != SYMBOL_ID_NONE)
                entries[petta_libpl_callable_slot(entries, capacity, runtime->callables[i].name)] = runtime->callables[i];
        }
        free(runtime->callables);
        runtime->callables = entries;
        runtime->callable_cap = capacity;
    }
    Atom *retained = atom_deep_copy(&runtime->callable_codes, code);
    if (!retained)
        return SYMBOL_ID_NONE;
    size_t slot = petta_libpl_callable_slot(runtime->callables, runtime->callable_cap, name);
    runtime->callables[slot] = (PettaLibplCallable){name, retained};
    runtime->callable_len++;
    return name;
}

static bool petta_libpl_quote_body(
    Atom *atom, Atom **body) {
    if (body)
        *body = NULL;
    if (!atom || !body ||
        atom->kind != ATOM_EXPR ||
        atom->expr.len != 2u ||
        !petta_libpl_atom_is_symbol(
            atom->expr.elems[0], "quote")) {
        return false;
    }
    *body = atom->expr.elems[1];
    return *body != NULL;
}

static bool petta_libpl_predicate_body(
    Atom *atom, Atom **body) {
    if (body)
        *body = NULL;
    if (!atom || !body)
        return false;
    /*
     * A demanded Predicate has already crossed the list/compound boundary
     * and carries CeTTa's private Prolog-compound tag.  Predicate operations
     * accept that value just as they accept the visible source wrapper.
     */
    if (atom_petta_prolog_compound_body(atom, body))
        return true;
    if (atom_prolog_compound_body(atom, body))
        return true;
    if (
        atom->kind != ATOM_EXPR ||
        atom->expr.len != 2u ||
        !petta_libpl_atom_is_symbol(
            atom->expr.elems[0], "Predicate")) {
        return false;
    }
    *body = atom->expr.elems[1];
    return *body != NULL;
}

static bool petta_libpl_to_callable(
    Atom *atom, term_t output,
    PettaLibplVarMap *variables, uint32_t depth) {
    if (!atom || !output || !variables ||
        depth > 1024u) {
        return false;
    }
    Atom *unquoted = NULL;
    if (petta_libpl_quote_body(atom, &unquoted))
        return petta_libpl_to_callable(
            unquoted, output, variables, depth + 1u);

    Atom *predicate = NULL;
    if (petta_libpl_predicate_body(atom, &predicate))
        return petta_libpl_to_callable(
            predicate, output, variables, depth + 1u);
    if (atom_petta_prolog_compound_body(
            atom, &predicate)) {
        return petta_libpl_to_callable(
            predicate, output, variables, depth + 1u);
    }

    if (atom->kind == ATOM_SYMBOL) {
        return petta_libpl_put_utf8(
            output, PL_ATOM,
            symbol_bytes(g_symbols, atom->sym_id),
            symbol_len(g_symbols, atom->sym_id));
    }
    if (atom->kind != ATOM_EXPR ||
        atom->expr.len == 0u ||
        atom->expr.elems[0]->kind != ATOM_SYMBOL ||
        atom->expr.len - 1u > (CettaExprLen)INT_MAX) {
        return false;
    }

    /*
     * Predicate/1 is the explicit list-to-Prolog-compound constructor, and
     * a cons cell inside it is LIST syntax: (cons S (r1 .. rn)) denotes the
     * list [S, r1, .., rn], whose compound image is S(r1, .., rn).  This is
     * how the chainer builds space rows (Term =.. [Space | Row] on the
     * reference); marshalling the cell structurally as cons/2 would store
     * every row under the wrong predicate.
     */
    if (atom->expr.len == 3u &&
        petta_semantics_form(atom->expr.elems[0]->sym_id) ==
            PETTA_FORM_CONS &&
        atom->expr.elems[1] &&
        atom->expr.elems[1]->kind == ATOM_SYMBOL &&
        atom->expr.elems[2]) {
        /*
         * The tail denotes a LOGICAL list: a flat expression carries its
         * elements literally, while an open-cons chain carries one element
         * per cell (the chainer's rows arrive that way).  Univ over the
         * logical elements, never over a carrier's implementation fields.
         */
        CettaExprLen logical_arity = 0u;
        bool tail_ok = petta_semantics_logical_list_length(
            atom->expr.elems[2], &logical_arity);
        if (tail_ok && logical_arity > 0u) {
            if (logical_arity > (CettaExprLen)INT_MAX ||
                !cetta_expr_len_fits_size(logical_arity)) {
                return false;
            }
            size_t univ_arity = (size_t)logical_arity;
            term_t univ_args = PL_new_term_refs(univ_arity);
            if (!univ_args)
                return false;
            PeTTaLogicalListCursor cursor;
            petta_semantics_logical_list_cursor_init(
                &cursor, atom->expr.elems[2]);
            for (size_t index = 0u; index < univ_arity;
                 index++) {
                Atom *argument = NULL;
                if (petta_semantics_logical_list_cursor_next(
                        &cursor, &argument) !=
                    PETTA_LOGICAL_LIST_ITEM) {
                    return false;
                }
                Atom *wrapped = NULL;
                bool ok = petta_libpl_predicate_body(
                              argument, &wrapped)
                    ? petta_libpl_to_callable(
                          wrapped, univ_args + index,
                          variables, depth + 1u)
                    : petta_libpl_to_term(
                          argument, univ_args + index,
                          variables, depth + 1u);
                if (!ok)
                    return false;
            }
            Atom *extra = NULL;
            if (petta_semantics_logical_list_cursor_next(
                    &cursor, &extra) != PETTA_LOGICAL_LIST_END) {
                return false;
            }
            atom_t univ_name = petta_libpl_new_utf8_atom(
                symbol_bytes(
                    g_symbols, atom->expr.elems[1]->sym_id),
                symbol_len(
                    g_symbols, atom->expr.elems[1]->sym_id));
            functor_t univ_functor =
                PL_new_functor_sz(univ_name, univ_arity);
            bool univ_built = univ_functor &&
                              PL_cons_functor_v(
                                  output, univ_functor,
                                  univ_args);
            PL_unregister_atom(univ_name);
            return univ_built;
        }
    }

    size_t arity = (size_t)(atom->expr.len - 1u);
    term_t arguments =
        arity ? PL_new_term_refs(arity) : 0;
    if (arity && !arguments)
        return false;
    for (size_t index = 0u; index < arity; index++) {
        Atom *argument =
            atom->expr.elems[index + 1u];
        Atom *wrapped = NULL;
        bool ok = petta_libpl_predicate_body(
                      argument, &wrapped)
            ? petta_libpl_to_callable(
                  wrapped, arguments + index,
                  variables, depth + 1u)
            : petta_libpl_to_term(
                  argument, arguments + index,
                  variables, depth + 1u);
        if (!ok)
            return false;
    }
    /* user:Goal names the program's module, which this runtime's module
     * stands in for (petta_libpl_from_term_mode reads it back as user). */
    const CettaLibPrologRuntime *active = g_petta_libpl_active_runtime;
    if (arity == 2u && active && active->module_name &&
        petta_libpl_atom_is_symbol(atom->expr.elems[0], ":") &&
        petta_libpl_atom_is_symbol(atom->expr.elems[1], "user") &&
        !petta_libpl_put_utf8(arguments, PL_ATOM, active->module_name,
                              active->module_name_len))
        return false;

    atom_t name = petta_libpl_new_utf8_atom(
        symbol_bytes(
            g_symbols, atom->expr.elems[0]->sym_id),
        symbol_len(
            g_symbols, atom->expr.elems[0]->sym_id));
    functor_t functor = PL_new_functor_sz(name, arity);
    bool built = functor &&
                 PL_cons_functor_v(
                     output, functor, arguments);
    PL_unregister_atom(name);
    return built;
}

static bool petta_libpl_to_list(
    Atom *atom, term_t output,
    PettaLibplVarMap *variables, uint32_t depth) {
    if (!atom || atom->kind != ATOM_EXPR ||
        !output || !variables || depth > 1024u ||
        !PL_put_nil(output)) {
        return false;
    }
    term_t item = PL_new_term_ref();
    term_t tail = PL_new_term_ref();
    if (!item || !tail)
        return false;
    for (CettaExprIndex index = atom->expr.len;
         index > 0u; index--) {
        if (!PL_put_variable(item) ||
            !petta_libpl_to_term(
                atom->expr.elems[index - 1u], item,
                variables, depth + 1u) ||
            !PL_put_term(tail, output) ||
            !PL_cons_list(output, item, tail)) {
            return false;
        }
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PETTA_LIBPL_STRUCTURAL_LIST_CELL_TO_PROLOG);
    }
    return true;
}

static bool petta_libpl_to_term(
    Atom *atom, term_t output,
    PettaLibplVarMap *variables, uint32_t depth) {
    if (!atom || !output || !variables ||
        depth > 1024u) {
        return false;
    }
    switch (atom->kind) {
    case ATOM_SYMBOL:
        return petta_libpl_put_utf8(
            output, PL_ATOM,
            symbol_bytes(g_symbols, atom->sym_id),
            symbol_len(g_symbols, atom->sym_id));
    case ATOM_VAR: {
        PettaLibplVar *variable =
            petta_libpl_var_add(variables, atom);
        return variable &&
               PL_put_term(output, variable->term);
    }
    case ATOM_EXPR: {
        if (petta_semantics_lambda_body(atom, NULL) ||
            petta_semantics_nullary_lambda_body(atom, NULL)) {
            SymbolId name = petta_libpl_callable_name(g_petta_libpl_active_runtime, atom);
            return name != SYMBOL_ID_NONE && petta_libpl_put_utf8(
                output, PL_ATOM, symbol_bytes(g_symbols, name), symbol_len(g_symbols, name));
        }
        Atom *partial_base = NULL;
        Atom *partial_arguments = NULL;
        if (petta_semantics_partial_view(atom, &partial_base, &partial_arguments)) {
            term_t arguments = PL_new_term_refs(2);
            atom_t name = PL_new_atom("partial");
            functor_t functor = name ? PL_new_functor(name, 2u) : 0;
            bool ok = arguments && functor &&
                petta_libpl_to_term(partial_base, arguments, variables, depth + 1u) &&
                petta_libpl_to_term(partial_arguments, arguments + 1u, variables, depth + 1u) &&
                PL_cons_functor_v(output, functor, arguments);
            if (name)
                PL_unregister_atom(name);
            return ok;
        }
        PettaLibplPlrefHandle plref;
        if (petta_libpl_plref_view(atom, &plref))
            return petta_libpl_plref_fetch(
                g_petta_libpl_active_runtime,
                &plref, output);
        /* A rational term with free variables crosses as its graph. */
        if (atom_is_rational_value(atom))
            return petta_libpl_graph_to_term(atom, output, variables, depth);
        /*
         * An open-cons carrier is one logical list cell; marshalling its
         * three implementation fields would leak the private carrier tag
         * into the engine as data (the chainer's ccls rows were stored that
         * way).  Emit real './2' cells: a closed chain becomes a proper
         * list, an unbound tail a partial one.
         */
        if (petta_semantics_is_open_cons_value(atom)) {
            term_t item = PL_new_term_ref();
            if (!item)
                return false;
            Atom *cursor = atom;
            Atom **items = NULL;
            size_t item_len = 0u;
            size_t item_cap = 0u;
            Atom *tail_atom = NULL;
            for (cursor = atom;
                 petta_semantics_is_open_cons_value(cursor);
                 cursor = cursor->expr.elems[2]) {
                if (item_len == item_cap) {
                    size_t next = item_cap ? item_cap * 2u : 8u;
                    Atom **grown = items
                        ? cetta_realloc(
                              items, sizeof(*items) * next)
                        : cetta_malloc(sizeof(*items) * next);
                    if (!grown) {
                        free(items);
                        return false;
                    }
                    items = grown;
                    item_cap = next;
                }
                items[item_len++] = cursor->expr.elems[1];
            }
            tail_atom = cursor;
            bool tail_is_nil =
                tail_atom->kind == ATOM_EXPR &&
                tail_atom->expr.len == 0u;
            term_t list = PL_new_term_ref();
            bool ok = list != 0;
            if (ok) {
                if (tail_is_nil) {
                    ok = PL_put_nil(list);
                } else {
                    ok = petta_libpl_to_term(
                        tail_atom, list, variables,
                        depth + 1u);
                }
            }
            for (size_t index = item_len; ok && index > 0u;
                 index--) {
                ok = PL_put_variable(item) &&
                     petta_libpl_to_term(
                         items[index - 1u], item,
                         variables, depth + 1u) &&
                     PL_cons_list(list, item, list);
                if (ok) {
                    cetta_runtime_stats_inc(
                        CETTA_RUNTIME_COUNTER_PETTA_LIBPL_STRUCTURAL_LIST_CELL_TO_PROLOG);
                }
            }
            free(items);
            return ok && PL_put_term(output, list);
        }
        Atom *compound = NULL;
        if (atom_petta_prolog_compound_body(
                atom, &compound)) {
            return petta_libpl_to_callable(
                compound, output, variables,
                depth + 1u);
        }
        Atom *predicate = NULL;
        if (petta_libpl_predicate_body(
                atom, &predicate)) {
            return petta_libpl_to_callable(
                predicate, output, variables,
                depth + 1u);
        }
        return petta_libpl_to_list(
            atom, output, variables, depth + 1u);
    }
    case ATOM_GROUNDED:
        break;
    }

    switch (atom->ground.gkind) {
    case GV_INT:
        return PL_put_int64(output, atom->ground.ival);
    case GV_FLOAT:
        return PL_put_float(output, atom->ground.fval);
    case GV_BOOL:
        return PL_put_bool(
            output, atom->ground.bval ? 1 : 0);
    case GV_STRING: {
        const char *value =
            atom->ground.sval ? atom->ground.sval : "";
        return petta_libpl_put_utf8(
            output, PL_STRING, value, strlen(value));
    }
    case GV_BIGINT: {
        const char *value = atom_bigint_cstr(atom);
        return value &&
               PL_put_term_from_chars(
                   output, REP_UTF8,
                   strlen(value), value);
    }
    case GV_RATIONAL: {
        /* CeTTa spells a rational 1/2, which Prolog reads as the compound
         * /(1,2); Prolog's own spelling is 1r2. */
        const char *value = atom_rational_cstr(atom);
        if (!value)
            return false;
        size_t length = strlen(value);
        char *spelled = cetta_malloc(length + 1u);
        memcpy(spelled, value, length + 1u);
        char *slash = strchr(spelled, '/');
        if (slash)
            *slash = 'r';
        bool ok = PL_put_term_from_chars(
            output, REP_UTF8, length, spelled);
        free(spelled);
        return ok;
    }
    case GV_TERM_GRAPH:
        return petta_libpl_graph_to_term(atom, output, variables, depth);
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

static int petta_libpl_known_compare(const void *left, const void *right,
                                     void *context) {
    const PettaLibplVarMap *variables = context;
    size_t l = *(const size_t *)left, r = *(const size_t *)right;
    int order = PL_compare(variables->items[l].term, variables->items[r].term);
    return order ? order : (l > r) - (l < r);
}

static bool petta_libpl_known_variables(PettaLibplVarMap *variables,
                                       PettaLibplBackVarMap *back) {
    if (variables->len > SIZE_MAX / sizeof(*back->known))
        return false;
    back->known = variables->len
        ? cetta_malloc(variables->len * sizeof(*back->known)) : NULL;
    if (variables->len && !back->known)
        return false;
    for (size_t at = 0; at < variables->len; at++) {
        if (PL_is_variable(variables->items[at].term))
            back->known[back->known_len++] = at;
    }
    if (back->known_len > 1u)
        qsort_r(back->known, back->known_len, sizeof(*back->known),
                petta_libpl_known_compare, variables);
    back->known_ready = true;
    return true;
}

static size_t petta_libpl_known_variable(term_t term,
                                        PettaLibplVarMap *variables,
                                        const PettaLibplBackVarMap *back) {
    if (!back->known_ready) {
        for (size_t at = 0; at < variables->len; at++) {
            if (PL_compare(term, variables->items[at].term) == 0)
                return at;
        }
        return SIZE_MAX;
    }
    size_t low = 0, high = back->known_len;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        int order = PL_compare(variables->items[back->known[middle]].term, term);
        if (order < 0)
            low = middle + 1u;
        else
            high = middle;
    }
    return low < back->known_len &&
           PL_compare(variables->items[back->known[low]].term, term) == 0
        ? back->known[low] : SIZE_MAX;
}

static Atom *petta_libpl_back_variable(
    Arena *arena, term_t term,
    PettaLibplVarMap *variables,
    PettaLibplBackVarMap *unknown) {
    if (!arena || !term || !variables || !unknown)
        return NULL;
    if (variables->native_variable) {
        Atom *owned = variables->native_variable(variables->slot_context, arena, term);
        if (owned)
            return owned;
    }
    size_t known = petta_libpl_known_variable(term, variables, unknown);
    if (known != SIZE_MAX)
        return atom_var_like(arena, variables->items[known].prototype,
                              variables->items[known].id);
    for (size_t index = 0u;
         index < unknown->len; index++) {
        if (PL_compare(
                term, unknown->items[index].term) == 0) {
            return unknown->items[index].variable;
        }
    }
    if (unknown->len == unknown->cap) {
        size_t next =
            unknown->cap ? unknown->cap * 2u : 8u;
        if (next <= unknown->cap ||
            next > SIZE_MAX / sizeof(*unknown->items)) {
            return NULL;
        }
        unknown->items = unknown->items
            ? cetta_realloc(
                  unknown->items,
                  sizeof(*unknown->items) * next)
            : cetta_malloc(
                  sizeof(*unknown->items) * next);
        unknown->cap = next;
    }
    term_t copy = PL_new_term_ref();
    Atom *variable = atom_var_with_id(
        arena, "$foreign", fresh_var_id());
    if (!copy || !variable ||
        !PL_put_term(copy, term)) {
        return NULL;
    }
    unknown->items[unknown->len++] =
        (PettaLibplBackVar){
            .term = copy,
            .variable = variable,
        };
    return variable;
}

static Atom *petta_libpl_from_term_mode(
    Arena *arena, term_t term,
    PettaLibplVarMap *variables,
    PettaLibplBackVarMap *unknown,
    bool visible_compounds, uint32_t depth);

static Atom *petta_libpl_from_term(
    Arena *arena, term_t term,
    PettaLibplVarMap *variables,
    PettaLibplBackVarMap *unknown,
    uint32_t depth) {
    return petta_libpl_from_term_mode(
        arena, term, variables, unknown,
        false, depth);
}

static Atom *petta_libpl_from_list(
    Arena *arena, term_t term,
    PettaLibplVarMap *variables,
    PettaLibplBackVarMap *unknown,
    bool visible_compounds, uint32_t depth) {
    if (!arena || !term || !variables || !unknown ||
        depth > 1024u || !PL_is_acyclic(term)) {
        return NULL;
    }

    term_t cursor = PL_new_term_ref();
    term_t head = PL_new_term_ref();
    term_t tail = PL_new_term_ref();
    if (!cursor || !head || !tail ||
        !PL_put_term(cursor, term)) {
        return NULL;
    }

    Atom **items = NULL;
    size_t length = 0u;
    size_t capacity = 0u;
    while (!PL_get_nil(cursor)) {
        if (!PL_get_list(cursor, head, tail)) {
            free(items);
            return NULL;
        }
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PETTA_LIBPL_STRUCTURAL_LIST_CELL_FROM_PROLOG);
        if (length == capacity) {
            size_t next = capacity ? capacity * 2u : 8u;
            if (next <= capacity ||
                next > SIZE_MAX / sizeof(*items)) {
                free(items);
                return NULL;
            }
            items = items
                ? cetta_realloc(
                      items, sizeof(*items) * next)
                : cetta_malloc(
                      sizeof(*items) * next);
            capacity = next;
        }
        Atom *item = petta_libpl_from_term_mode(
            arena, head, variables, unknown,
            visible_compounds, depth + 1u);
        if (!item) {
            free(items);
            return NULL;
        }
        items[length++] = item;
        if (!PL_put_term(cursor, tail)) {
            free(items);
            return NULL;
        }
    }
    Atom *result = atom_expr(
        arena, items, (CettaExprLen)length);
    free(items);
    return result;
}

/* ── Rational terms across the boundary (term_graph.h) ─────────────────── */

typedef struct {
    term_t variable;
    uint32_t node;
} PettaLibplSharedNode;

typedef struct {
    uint32_t parent;
    uint32_t index;
    term_t term;
} PettaLibplGraphWork;

typedef struct {
    term_t variable;
    uint32_t node;
    Atom *value;
} PettaLibplGraphParam;

typedef struct {
    CettaTermGraph *graph;
    PettaLibplSharedNode *shared;
    size_t shared_len;
    PettaLibplGraphWork *work;
    size_t work_len;
    size_t work_cap;
    Arena *arena;
    /* The term's free variables, each a parameter numbered by its place
     * here, with its node and its value as the enclosing conversion reads
     * it, so a variable is the same variable inside and outside the term. */
    PettaLibplGraphParam *params;
    size_t params_len;
    size_t params_cap;
    PettaLibplVarMap *variables;
    PettaLibplBackVarMap *unknown;
    uint32_t depth;
} PettaLibplGraphBuild;

static bool petta_libpl_graph_push(PettaLibplGraphBuild *build,
                                   uint32_t parent, uint32_t index,
                                   term_t term) {
    if (build->work_len == build->work_cap) {
        size_t next = build->work_cap ? build->work_cap * 2u : 64u;
        PettaLibplGraphWork *grown = cetta_realloc(
            build->work, sizeof(*grown) * next);
        if (!grown)
            return false;
        build->work = grown;
        build->work_cap = next;
    }
    build->work[build->work_len++] =
        (PettaLibplGraphWork){parent, index, term};
    return true;
}

/* A free variable's parameter node, one per variable. */
static uint32_t petta_libpl_graph_param(PettaLibplGraphBuild *build,
                                        term_t term) {
    for (size_t index = 0u; index < build->params_len; index++) {
        if (PL_compare(term, build->params[index].variable) == 0)
            return build->params[index].node;
    }
    if (build->params_len == build->params_cap) {
        size_t next = build->params_cap ? build->params_cap * 2u : 8u;
        PettaLibplGraphParam *grown = cetta_realloc(
            build->params, sizeof(*grown) * next);
        if (!grown)
            return CETTA_TERM_GRAPH_NO_NODE;
        build->params = grown;
        build->params_cap = next;
    }
    term_t variable = PL_copy_term_ref(term);
    Atom *value = variable
        ? petta_libpl_from_term_mode(build->arena, variable,
                                     build->variables, build->unknown,
                                     false, build->depth + 1u)
        : NULL;
    uint32_t node = value
        ? term_graph_add_param(build->graph, (uint32_t)build->params_len)
        : CETTA_TERM_GRAPH_NO_NODE;
    if (node != CETTA_TERM_GRAPH_NO_NODE)
        build->params[build->params_len++] =
            (PettaLibplGraphParam){variable, node, value};
    return node;
}

/* The node of one cell, its children queued: a name term_factorized/3
 * gave a shared or cyclic subterm is that subterm's node, and any other
 * variable is a free variable of the term, a parameter. */
static uint32_t petta_libpl_graph_node(PettaLibplGraphBuild *build,
                                       term_t term) {
    int type = PL_term_type(term);
    if (type == PL_VARIABLE) {
        for (size_t index = 0u; index < build->shared_len; index++) {
            if (PL_compare(term, build->shared[index].variable) == 0)
                return build->shared[index].node;
        }
        return petta_libpl_graph_param(build, term);
    }
    if (type == PL_NIL)
        return term_graph_add_nil(build->graph);
    if (PL_is_pair(term)) {
        uint32_t node = term_graph_add_cell(build->graph);
        term_t head = PL_new_term_ref();
        term_t tail = PL_new_term_ref();
        if (node == CETTA_TERM_GRAPH_NO_NODE || !head || !tail ||
            !PL_get_list(term, head, tail) ||
            !petta_libpl_graph_push(build, node, 0u, head) ||
            !petta_libpl_graph_push(build, node, 1u, tail))
            return CETTA_TERM_GRAPH_NO_NODE;
        return node;
    }
    if (type == PL_TERM) {
        atom_t name = 0;
        size_t arity = 0u;
        size_t name_len = 0u;
        char *name_bytes = NULL;
        if (!PL_get_compound_name_arity_sz(term, &name, &arity) ||
            arity > UINT32_MAX ||
            !petta_libpl_atom_utf8(name, &name_bytes, &name_len) ||
            name_len > UINT32_MAX) {
            if (name_bytes)
                PL_free(name_bytes);
            return CETTA_TERM_GRAPH_NO_NODE;
        }
        SymbolId symbol = symbol_intern_bytes(
            g_symbols, (const uint8_t *)name_bytes, (uint32_t)name_len);
        PL_free(name_bytes);
        uint32_t node = term_graph_add_compound(
            build->graph, symbol, (uint32_t)arity);
        for (size_t index = 0u;
             node != CETTA_TERM_GRAPH_NO_NODE && index < arity; index++) {
            term_t argument = PL_new_term_ref();
            if (!argument ||
                !PL_get_arg_sz(index + 1u, term, argument) ||
                !petta_libpl_graph_push(build, node, (uint32_t)index,
                                        argument))
                node = CETTA_TERM_GRAPH_NO_NODE;
        }
        return node;
    }
    PettaLibplVarMap variables = {0};
    PettaLibplBackVarMap unknown = {0};
    Atom *leaf = petta_libpl_from_term_mode(
        build->arena, term, &variables, &unknown, false, 0u);
    petta_libpl_var_map_free(&variables);
    petta_libpl_back_map_free(&unknown);
    return leaf ? term_graph_add_leaf(build->graph, leaf)
                : CETTA_TERM_GRAPH_NO_NODE;
}

/*
 * A term that is not a finite tree, as the graph it presents
 * (RationalTermGraph.IsTransport): term_factorized/3 names every subterm
 * the term shares or repeats by a variable, and each cell of the factorized
 * term becomes one node, so a cycle closes on the node of its name.  The
 * atom is the root's term one level open.
 */
static Atom *petta_libpl_graph_from_term(Arena *arena, term_t term,
                                         PettaLibplVarMap *variables,
                                         PettaLibplBackVarMap *unknown,
                                         uint32_t depth) {
    static predicate_t factorized;
    if (!factorized)
        factorized = PL_predicate("term_factorized", 3, "system");
    term_t args = PL_new_term_refs(3);
    if (!args || !PL_put_term(args, term) ||
        !PL_call_predicate(NULL, PL_Q_NODEBUG | PL_Q_CATCH_EXCEPTION,
                           factorized, args))
        return NULL;
    PettaLibplGraphBuild build = {
        .arena = arena,
        .variables = variables,
        .unknown = unknown,
        .depth = depth,
    };
    build.graph = term_graph_new();
    if (!build.graph)
        return NULL;
    /* Every name first, so a cycle can close on a node not yet filled. */
    term_t list = PL_copy_term_ref(args + 2);
    term_t head = PL_new_term_ref();
    term_t defs = 0;
    size_t count = 0u;
    bool ok = list && head;
    while (ok && PL_get_list(list, head, list))
        count++;
    if (ok && count) {
        build.shared = cetta_malloc(sizeof(*build.shared) * count);
        defs = PL_new_term_refs((int)count);
        ok = build.shared && defs;
        list = PL_copy_term_ref(args + 2);
        for (size_t index = 0u; ok && index < count; index++) {
            term_t binding = PL_new_term_ref();
            term_t variable = PL_new_term_ref();
            ok = binding && variable && PL_get_list(list, binding, list) &&
                 PL_get_arg(1, binding, variable) &&
                 PL_get_arg(2, binding, defs + index);
            if (ok)
                build.shared[index] =
                    (PettaLibplSharedNode){variable, CETTA_TERM_GRAPH_NO_NODE};
        }
        build.shared_len = ok ? count : 0u;
        /* A name's node is its definition's own cell. */
        for (size_t index = 0u; ok && index < count; index++) {
            uint32_t node = petta_libpl_graph_node(&build, defs + index);
            build.shared[index].node = node;
            ok = node != CETTA_TERM_GRAPH_NO_NODE;
        }
    }
    uint32_t root = ok ? petta_libpl_graph_node(&build, args + 1)
                       : CETTA_TERM_GRAPH_NO_NODE;
    ok = root != CETTA_TERM_GRAPH_NO_NODE;
    while (ok && build.work_len > 0u) {
        PettaLibplGraphWork work = build.work[--build.work_len];
        uint32_t child = petta_libpl_graph_node(&build, work.term);
        ok = child != CETTA_TERM_GRAPH_NO_NODE &&
             term_graph_set_child(build.graph, work.parent, work.index, child);
    }
    Atom *result = NULL;
    if (ok && term_graph_seal(build.graph)) {
        Atom **values = build.params_len
            ? arena_alloc(arena, sizeof(*values) * build.params_len) : NULL;
        for (size_t index = 0u; index < build.params_len; index++)
            values[index] = build.params[index].value;
        Atom *value = term_graph_node_value(arena, build.graph, root, values);
        result = value ? term_graph_open_value(arena, value) : NULL;
    }
    term_graph_release(build.graph);
    free(build.shared);
    free(build.work);
    free(build.params);
    return result;
}

/*
 * A graph node as a Prolog term, sharing and cycles kept: one cell per node
 * reached, built with fresh arguments first; setarg/3 then fills each
 * argument with its child's cell, which closes a cycle whatever the
 * occurs_check flag says.
 */
static bool petta_libpl_graph_to_term(const Atom *value, term_t output,
                                      PettaLibplVarMap *variables,
                                      uint32_t depth) {
    const CettaTermGraphRef *ref = NULL;
    Atom *const *value_args = NULL;
    uint32_t value_nargs = 0u;
    if (!term_graph_value_node(value, &ref, &value_args, &value_nargs))
        return false;
    CettaTermGraph *graph = ref->graph;
    uint32_t count = term_graph_node_count(graph);
    term_t *terms = cetta_malloc(sizeof(*terms) * (count ? count : 1u));
    uint32_t *stack = cetta_malloc(sizeof(*stack) * (count ? count : 1u));
    uint32_t *reached = cetta_malloc(sizeof(*reached) * (count ? count : 1u));
    bool *queued = cetta_malloc(sizeof(*queued) * (count ? count : 1u));
    if (!terms || !stack || !reached || !queued) {
        free(terms);
        free(stack);
        free(reached);
        free(queued);
        return false;
    }
    memset(terms, 0, sizeof(*terms) * count);
    memset(queued, 0, sizeof(*queued) * count);
    uint32_t stack_len = 0u;
    uint32_t reached_len = 0u;
    bool ok = true;
    /* Each node is queued once, so the stack never holds more than the
     * graph's nodes. */
    stack[stack_len++] = ref->node;
    queued[ref->node] = true;
    while (ok && stack_len > 0u) {
        uint32_t node = stack[--stack_len];
        term_t term = PL_new_term_ref();
        ok = term != 0;
        switch (ok ? term_graph_kind(graph, node) : CETTA_TERM_GRAPH_NIL) {
        case CETTA_TERM_GRAPH_LEAF:
            ok = ok && petta_libpl_to_term(term_graph_leaf(graph, node), term,
                                           variables, depth + 1u);
            break;
        case CETTA_TERM_GRAPH_PARAM: {
            /* A free variable is its value, a variable of this call when it
             * is still unbound. */
            Atom *param = term_graph_value_param(
                value, term_graph_param(graph, node));
            ok = ok && param &&
                 petta_libpl_to_term(param, term, variables, depth + 1u);
            break;
        }
        case CETTA_TERM_GRAPH_NIL:
            ok = ok && PL_put_nil(term);
            break;
        case CETTA_TERM_GRAPH_CELL:
            ok = ok && PL_put_list(term);
            break;
        case CETTA_TERM_GRAPH_COMPOUND: {
            const char *name = symbol_bytes(g_symbols,
                                            term_graph_name(graph, node));
            atom_t atom = petta_libpl_new_utf8_atom(
                name, symbol_len(g_symbols, term_graph_name(graph, node)));
            functor_t functor = PL_new_functor_sz(
                atom, term_graph_arity(graph, node));
            term_t arguments = term_graph_arity(graph, node)
                ? PL_new_term_refs((int)term_graph_arity(graph, node)) : 0;
            ok = ok && functor &&
                 (!term_graph_arity(graph, node) || arguments) &&
                 PL_cons_functor_v(term, functor, arguments);
            PL_unregister_atom(atom);
            break;
        }
        }
        if (!ok)
            break;
        terms[node] = term;
        reached[reached_len++] = node;
        CettaTermGraphKind kind = term_graph_kind(graph, node);
        uint32_t arity = kind == CETTA_TERM_GRAPH_CELL ||
                kind == CETTA_TERM_GRAPH_COMPOUND
            ? term_graph_arity(graph, node) : 0u;
        for (uint32_t index = 0u; index < arity; index++) {
            uint32_t child = term_graph_child(graph, node, index);
            if (!queued[child]) {
                queued[child] = true;
                stack[stack_len++] = child;
            }
        }
    }
    static predicate_t setarg;
    if (!setarg)
        setarg = PL_predicate("setarg", 3, "system");
    for (uint32_t r = 0u; ok && r < reached_len; r++) {
        uint32_t node = reached[r];
        CettaTermGraphKind kind = term_graph_kind(graph, node);
        if (kind != CETTA_TERM_GRAPH_CELL && kind != CETTA_TERM_GRAPH_COMPOUND)
            continue;
        uint32_t arity = term_graph_arity(graph, node);
        for (uint32_t index = 0u; ok && index < arity; index++) {
            term_t call = PL_new_term_refs(3);
            ok = call &&
                 PL_put_int64(call, (int64_t)index + 1) &&
                 PL_put_term(call + 1, terms[node]) &&
                 PL_put_term(call + 2,
                             terms[term_graph_child(graph, node, index)]) &&
                 PL_call_predicate(NULL, PL_Q_NODEBUG, setarg, call);
        }
    }
    ok = ok && PL_put_term(output, terms[ref->node]);
    free(terms);
    free(stack);
    free(reached);
    free(queued);
    return ok;
}

/* The symbol an atom term reads as: its text, except that this runtime's
 * module, CeTTa's private stand-in for the reference's `user`, reads as the
 * reference's does, so a goal Prolog qualifies with it reads as the
 * reference's. */
static SymbolId petta_libpl_atom_term_symbol(
    term_t term, const PettaLibplVarMap *variables) {
    char *text = NULL;
    size_t length = 0u;
    if (!PL_get_nchars(term, &length, &text,
                       CVT_ATOM | BUF_MALLOC | REP_UTF8) ||
        length > UINT32_MAX) {
        if (text)
            PL_free(text);
        return SYMBOL_ID_NONE;
    }
    const CettaLibPrologRuntime *active = g_petta_libpl_active_runtime;
    bool module_name = !variables->private_module && active &&
                       active->module_name &&
                       length == active->module_name_len &&
                       memcmp(text, active->module_name, length) == 0;
    SymbolId symbol = module_name
        ? symbol_intern_cstr(g_symbols, "user")
        : symbol_intern_bytes(g_symbols, (const uint8_t *)text,
                              (uint32_t)length);
    PL_free(text);
    return symbol;
}

static Atom *petta_libpl_from_term_mode(
    Arena *arena, term_t term,
    PettaLibplVarMap *variables,
    PettaLibplBackVarMap *unknown,
    bool visible_compounds, uint32_t depth) {
    if (!arena || !term || !variables || !unknown ||
        depth > 1024u) {
        return NULL;
    }

    int type = PL_term_type(term);
    if (type == PL_VARIABLE) {
        return petta_libpl_back_variable(
            arena, term, variables, unknown);
    }
    if (type == PL_NIL)
        return atom_expr(arena, NULL, 0u);
    if (type == PL_INTEGER) {
        int64_t value = 0;
        if (PL_get_int64(term, &value))
            return atom_int(arena, value);
        char *text = NULL;
        size_t length = 0u;
        if (!PL_get_nchars(
                term, &length, &text,
                CVT_INTEGER | BUF_MALLOC |
                    REP_UTF8)) {
            return NULL;
        }
        Atom *result = atom_bigint(arena, text);
        PL_free(text);
        return result;
    }
    if (type == PL_FLOAT) {
        double value = 0.0;
        return PL_get_float(term, &value)
            ? atom_float(arena, value) : NULL;
    }
    if (type == PL_RATIONAL) {
        char *text = NULL;
        size_t length = 0u;
        if (!PL_get_nchars(
                term, &length, &text,
                CVT_RATIONAL | CVT_WRITEQ |
                    BUF_MALLOC | REP_UTF8)) {
            return NULL;
        }
        /* Prolog writes 1r2; CeTTa's rational text is 1/2. */
        char *r = strchr(text, 'r');
        if (r)
            *r = '/';
        Atom *result = atom_rational(arena, text);
        PL_free(text);
        return result;
    }
    if (type == PL_STRING) {
        char *text = NULL;
        size_t length = 0u;
        if (!PL_get_nchars(
                term, &length, &text,
                CVT_STRING | BUF_MALLOC | REP_UTF8))
            return NULL;
        char *copy = arena_alloc(arena, length + 1u);
        if (!copy) {
            PL_free(text);
            return NULL;
        }
        if (length)
            memcpy(copy, text, length);
        copy[length] = '\0';
        PL_free(text);
        return atom_string(arena, copy);
    }
    if (type == PL_ATOM) {
        SymbolId symbol = petta_libpl_atom_term_symbol(term, variables);
        Atom *code = petta_libpl_callable_find(g_petta_libpl_active_runtime, symbol);
        if (code)
            return atom_deep_copy(arena, code);
        return symbol == SYMBOL_ID_NONE
            ? NULL : atom_symbol_id(arena, symbol);
    }
    if (variables->cyclic_graphs &&
        (type == PL_TERM || PL_is_pair(term)) && !PL_is_acyclic(term))
        return petta_libpl_graph_from_term(arena, term, variables, unknown,
                                           depth);
    if (PL_is_list(term)) {
        return petta_libpl_from_list(
            arena, term, variables, unknown,
            visible_compounds, depth + 1u);
    }
    if (PL_is_pair(term)) {
        /*
         * A pair that is not a proper list is a PARTIAL list — the image of
         * an open-cons chain whose tail is still unbound.  Rebuild the
         * carrier chain so the native side sees the same logical list value
         * it marshalled out.
         */
        term_t head = PL_new_term_ref();
        term_t tail = PL_new_term_ref();
        if (!head || !tail ||
            !PL_get_list(term, head, tail))
            return NULL;
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PETTA_LIBPL_STRUCTURAL_LIST_CELL_FROM_PROLOG);
        Atom *head_atom = petta_libpl_from_term_mode(
            arena, head, variables, unknown,
            visible_compounds, depth + 1u);
        Atom *tail_atom = petta_libpl_from_term_mode(
            arena, tail, variables, unknown,
            visible_compounds, depth + 1u);
        if (!head_atom || !tail_atom)
            return NULL;
        return petta_semantics_open_cons_value(
            arena, head_atom, tail_atom);
    }
    if (type != PL_TERM) {
        /*
         * Whatever the engine answers that has no structural image here —
         * clause references, stream handles, other blobs — crosses as a
         * recorded opaque handle rather than failing the whole call.
         */
        return petta_libpl_plref_atom(
            g_petta_libpl_active_runtime, arena, term);
    }

    atom_t name = 0;
    size_t arity = 0u;
    if (!PL_get_compound_name_arity_sz(
            term, &name, &arity) ||
        arity > UINT64_MAX - 1u ||
        arity + 1u >
            SIZE_MAX / sizeof(Atom *)) {
        return NULL;
    }
    size_t name_len = 0u;
    char *name_bytes = NULL;
    if (!petta_libpl_atom_utf8(
            name, &name_bytes, &name_len) ||
        name_len > UINT32_MAX) {
        if (name_bytes)
            PL_free(name_bytes);
        return NULL;
    }
    SymbolId symbol = symbol_intern_bytes(
        g_symbols, (const uint8_t *)name_bytes,
        (uint32_t)name_len);
    PL_free(name_bytes);
    if (symbol == SYMBOL_ID_NONE)
        return NULL;

    Atom **items = cetta_malloc(
        sizeof(*items) * (arity + 1u));
    items[0] = atom_symbol_id(arena, symbol);
    term_t argument = PL_new_term_ref();
    bool ok = items[0] && argument;
    for (size_t index = 0u; ok && index < arity;
         index++) {
        ok = PL_get_arg_sz(
                 index + 1u, term, argument) &&
             (items[index + 1u] =
                  petta_libpl_from_term_mode(
                      arena, argument, variables,
                      unknown, visible_compounds,
                      depth + 1u));
    }
    Atom *body = ok
        ? atom_expr(
              arena, items,
              (CettaExprLen)(arity + 1u))
        : NULL;
    Atom *result = NULL;
    if (body) {
        if (!visible_compounds && arity == 2u &&
            petta_libpl_symbol_is(symbol, "partial") &&
            eval_current_language_id && eval_current_language_id() == CETTA_LANGUAGE_PETTA &&
            items[2]->kind == ATOM_EXPR &&
            petta_semantics_value_representation(items[2]) == PETTA_VALUE_ORDINARY) {
            result = petta_semantics_partial_value(
                arena, items[1], items[2]->expr.elems, items[2]->expr.len);
        } else if (visible_compounds) {
            Atom *wrapper =
                atom_symbol(arena, "prolog:compound");
            result = wrapper
                ? atom_expr2(arena, wrapper, body)
                : NULL;
        } else {
            result =
                atom_petta_prolog_compound(arena, body);
        }
    }
    free(items);
    return result;
}

/* ── Delayed goals across the boundary (delay_service.h) ────────────────── */

typedef struct {
    Atom **items;
    size_t len;
    size_t cap;
} PettaLibplVarList;

static bool petta_libpl_var_list_add(PettaLibplVarList *list, Atom *var) {
    if (list->len == list->cap) {
        size_t next = list->cap ? list->cap * 2u : 8u;
        Atom **grown = cetta_realloc(list->items, sizeof(*grown) * next);
        if (!grown)
            return false;
        list->items = grown;
        list->cap = next;
    }
    list->items[list->len++] = var;
    return true;
}

static int petta_libpl_var_compare(const void *left, const void *right) {
    VarId a = (*(Atom *const *)left)->var_id;
    VarId b = (*(Atom *const *)right)->var_id;
    return (a > b) - (a < b);
}

/* Each variable of `list` once, ordered by id. */
static void petta_libpl_var_list_unique(PettaLibplVarList *list) {
    if (list->len < 2u)
        return;
    qsort(list->items, list->len, sizeof(*list->items),
          petta_libpl_var_compare);
    size_t kept = 1u;
    for (size_t index = 1u; index < list->len; index++) {
        if (list->items[index]->var_id != list->items[kept - 1u]->var_id)
            list->items[kept++] = list->items[index];
    }
    list->len = kept;
}

typedef struct {
    VarId var;
    uint32_t goal;
} PettaLibplVarPair;

static int petta_libpl_var_pair_compare(const void *left, const void *right) {
    const PettaLibplVarPair *a = left, *b = right;
    if (a->var != b->var)
        return (a->var > b->var) - (a->var < b->var);
    return (a->goal > b->goal) - (a->goal < b->goal);
}

/* The occurrences of variables in `atom`, appended to `list`. */
static bool petta_libpl_collect_vars(Atom *atom, PettaLibplVarList *list) {
    Atom *inline_stack[32];
    Atom **stack = inline_stack;
    size_t len = 0u;
    size_t cap = 32u;
    bool ok = true;
    if (atom && atom_has_vars(atom))
        stack[len++] = atom;
    while (ok && len > 0u) {
        Atom *at = stack[--len];
        if (at->kind == ATOM_VAR) {
            ok = petta_libpl_var_list_add(list, at);
            continue;
        }
        if (at->kind != ATOM_EXPR)
            continue;
        for (CettaExprIndex index = at->expr.len; ok && index > 0u; index--) {
            Atom *child = at->expr.elems[index - 1u];
            if (!child || !atom_has_vars(child))
                continue;
            if (len == cap) {
                size_t next = cap * 2u;
                Atom **grown = stack == inline_stack
                    ? cetta_malloc(sizeof(*grown) * next)
                    : cetta_realloc(stack, sizeof(*grown) * next);
                if (!grown) {
                    ok = false;
                    break;
                }
                if (stack == inline_stack)
                    memcpy(grown, inline_stack, sizeof(*grown) * len);
                stack = grown;
                cap = next;
            }
            stack[len++] = child;
        }
    }
    if (stack != inline_stack)
        free(stack);
    return ok;
}

/* The conjunction of goals[0..len) in order, as a balanced tree of ','
 * nodes, so its depth grows with the logarithm of its length; the goal
 * itself for one.  Each ',' is a Prolog compound, as the goals read back
 * are: a nested conjunction is a goal, not the list (',' g c). */
static Atom *petta_libpl_conjunction(Arena *arena, Atom *const *goals,
                                     size_t len) {
    if (len == 0u)
        return NULL;
    if (len == 1u)
        return goals[0];
    size_t half = len / 2u;
    Atom *left = petta_libpl_conjunction(arena, goals, half);
    Atom *right = petta_libpl_conjunction(arena, goals + half, len - half);
    Atom *comma = atom_symbol(arena, ",");
    Atom *node = left && right && comma
        ? atom_expr3(arena, comma, left, right) : NULL;
    return node ? atom_petta_prolog_compound(arena, node) : NULL;
}

/*
 * The goals the caller's service holds on the call's variables, carried
 * into the call as one conjunction posted before its goal, so SWI's
 * constraints and attributes stand as they did when the goals were read
 * back.  Each goal is carried once with every variable it waits on; the
 * variables are recorded for the answers to withdraw.
 */
static bool petta_libpl_delay_collect(PettaLibplDelay *delay, Arena *arena,
                                      const Bindings *environment,
                                      Atom *expression) {
    if (!delay || !cetta_delay_watching(delay->service))
        return true;
    /* The goals connected to the call's variables, directly or through one
     * another, in the order they were suspended (cetta_delay_reach). */
    PettaLibplVarList vars = {0};
    PettaLibplVarList carried = {0};
    VarId *ids = NULL;
    const CettaDelayGoal **reached = NULL;
    uint32_t reached_len = 0u;
    bool ok = petta_libpl_collect_vars(expression, &vars) &&
              vars.len <= UINT32_MAX;
    if (ok && vars.len > 0u) {
        ids = cetta_malloc(sizeof(*ids) * vars.len);
        ok = ids != NULL;
        for (size_t index = 0u; ok && index < vars.len; index++)
            ids[index] = vars.items[index]->var_id;
        ok = ok && cetta_delay_reach(delay->service, ids,
                                     (uint32_t)vars.len, &reached,
                                     &reached_len);
    }
    Atom **goals = ok && reached_len
        ? arena_alloc(arena, sizeof(*goals) * reached_len) : NULL;
    for (uint32_t index = 0u; ok && index < reached_len; index++) {
        /* Each goal is carried with every variable it waits on. */
        ok = petta_libpl_collect_vars(reached[index]->goal, &carried);
        goals[index] = ok ? bindings_apply_if_vars(environment, arena,
                                                   reached[index]->goal)
                          : NULL;
        ok = ok && goals[index] != NULL;
    }
    if (ok && reached_len > 0u) {
        delay->carried = petta_libpl_conjunction(arena, goals, reached_len);
        ok = delay->carried != NULL;
    }
    petta_libpl_var_list_unique(&carried);
    if (ok && carried.len > 0u) {
        delay->carried_vars = arena_alloc(
            arena, sizeof(*delay->carried_vars) * carried.len);
        memcpy(delay->carried_vars, carried.items,
               sizeof(*delay->carried_vars) * carried.len);
        delay->carried_len = carried.len;
    }
    free(reached);
    free(ids);
    free(carried.items);
    free(vars.items);
    return ok;
}

/* `goal` after the call's carried goals, in one conjunction over the call's
 * variables. */
static bool petta_libpl_delay_wrap(PettaLibplVarMap *variables,
                                   term_t goal) {
    PettaLibplDelay *delay = variables->delay;
    if (!delay || !delay->carried)
        return true;
    term_t parts = PL_new_term_refs(2);
    atom_t comma = PL_new_atom(",");
    functor_t conjunction = PL_new_functor(comma, 2);
    PL_unregister_atom(comma);
    return parts &&
           petta_libpl_to_callable(delay->carried, parts, variables, 0u) &&
           PL_put_term(parts + 1, goal) &&
           PL_cons_functor_v(goal, conjunction, parts);
}

/*
 * What SWI still delays on an answer's variables: copy_term/3 writes the
 * attributes, frozen goals, dif/2 and when/2 conditions and CLP(FD)
 * constraints as goals over a copy, which is unified back with the
 * variables so the goals are over the answer's own variables, read through
 * the answer's maps.  The goals are split into the connected components of
 * the variables they share.  The payload is (carried components), each
 * component (goal var...); NULL when there is nothing to carry or suspend.
 */
static bool petta_libpl_delay_answer(Arena *arena,
                                     PettaLibplVarMap *variables,
                                     PettaLibplBackVarMap *unknown,
                                     Atom **payload) {
    *payload = NULL;
    PettaLibplDelay *delay = variables->delay;
    Atom *goals = NULL;
    if (variables->len > 0u) {
        static predicate_t term_attvars;
        static predicate_t copy_term;
        if (!term_attvars)
            term_attvars = PL_predicate("term_attvars", 2, "system");
        if (!copy_term)
            copy_term = PL_predicate("copy_term", 3, "system");
        term_t list = PL_new_term_ref();
        if (!list || !PL_put_nil(list))
            return false;
        for (size_t index = variables->len; index > 0u; index--) {
            if (!PL_cons_list(list, variables->items[index - 1u].term, list))
                return false;
        }
        term_t found = PL_new_term_refs(2);
        if (!found || !PL_put_term(found, list) ||
            !PL_call_predicate(NULL, PL_Q_NODEBUG | PL_Q_CATCH_EXCEPTION,
                               term_attvars, found))
            return false;
        if (!PL_get_nil(found + 1)) {
            term_t copied = PL_new_term_refs(3);
            if (!copied || !PL_put_term(copied, list) ||
                !PL_call_predicate(NULL,
                                   PL_Q_NODEBUG | PL_Q_CATCH_EXCEPTION,
                                   copy_term, copied) ||
                !PL_unify(copied + 1, copied))
                return false;
            if (!PL_get_nil(copied + 2)) {
                goals = petta_libpl_from_term(arena, copied + 2, variables,
                                              unknown, 0u);
                if (!goals || goals->kind != ATOM_EXPR)
                    return false;
            }
        }
    }
    size_t carried = delay ? delay->carried_len : 0u;
    CettaExprLen goal_len = goals ? goals->expr.len : 0u;
    if (goal_len == 0u && carried == 0u)
        return true;
    /* Components: goals sharing a variable, by union-find over goals, the
     * shared variables found as runs of one id among (variable, goal) pairs
     * ordered by id. */
    uint32_t *parent = cetta_malloc(sizeof(*parent) * (goal_len ? goal_len : 1u));
    PettaLibplVarList *vars = cetta_malloc(sizeof(*vars) * (goal_len ? goal_len : 1u));
    PettaLibplVarPair *pairs = NULL;
    size_t pair_len = 0u;
    bool ok = parent && vars;
    for (CettaExprLen index = 0u; ok && index < goal_len; index++) {
        parent[index] = (uint32_t)index;
        vars[index] = (PettaLibplVarList){0};
        ok = petta_libpl_collect_vars(goals->expr.elems[index], &vars[index]);
        if (ok)
            pair_len += vars[index].len;
    }
    if (ok && pair_len > 0u) {
        pairs = cetta_malloc(sizeof(*pairs) * pair_len);
        ok = pairs != NULL;
        pair_len = 0u;
        for (CettaExprLen index = 0u; ok && index < goal_len; index++) {
            for (size_t x = 0u; x < vars[index].len; x++)
                pairs[pair_len++] = (PettaLibplVarPair){
                    vars[index].items[x]->var_id, (uint32_t)index};
        }
        if (ok)
            qsort(pairs, pair_len, sizeof(*pairs),
                  petta_libpl_var_pair_compare);
    }
    for (size_t index = 1u; ok && index < pair_len; index++) {
        if (pairs[index].var != pairs[index - 1u].var)
            continue;
        /* Roots are the least goals of their sets; finds halve paths. */
        uint32_t ra = pairs[index - 1u].goal;
        while (parent[ra] != ra) {
            parent[ra] = parent[parent[ra]];
            ra = parent[ra];
        }
        uint32_t rb = pairs[index].goal;
        while (parent[rb] != rb) {
            parent[rb] = parent[parent[rb]];
            rb = parent[rb];
        }
        if (ra != rb)
            parent[rb < ra ? ra : rb] = rb < ra ? rb : ra;
    }
    free(pairs);
    Atom **components = ok && goal_len
        ? arena_alloc(arena, sizeof(*components) * goal_len) : NULL;
    CettaExprLen component_len = 0u;
    /* Each goal's root, then the members of each root in goal order. */
    for (CettaExprLen index = 0u; ok && index < goal_len; index++) {
        uint32_t r = (uint32_t)index;
        while (parent[r] != r)
            r = parent[r];
        parent[index] = r;
    }
    for (CettaExprLen root = 0u; ok && root < goal_len; root++) {
        if (parent[root] != (uint32_t)root)
            continue;
        Atom **members = arena_alloc(arena, sizeof(*members) * goal_len);
        PettaLibplVarList component_vars = {0};
        size_t member_len = 0u;
        for (CettaExprLen index = root; ok && index < goal_len; index++) {
            if (parent[index] != (uint32_t)root)
                continue;
            members[member_len++] = goals->expr.elems[index];
            for (size_t x = 0u; ok && x < vars[index].len; x++)
                ok = petta_libpl_var_list_add(&component_vars,
                                              vars[index].items[x]);
        }
        petta_libpl_var_list_unique(&component_vars);
        Atom **items = ok
            ? arena_alloc(arena, sizeof(*items) * (component_vars.len + 1u))
            : NULL;
        if (items) {
            items[0] = petta_libpl_conjunction(arena, members, member_len);
            for (size_t x = 0u; x < component_vars.len; x++)
                items[x + 1u] = component_vars.items[x];
            components[component_len] = items[0]
                ? atom_expr(arena, items,
                            (CettaExprLen)component_vars.len + 1u)
                : NULL;
            ok = components[component_len++] != NULL;
        } else {
            ok = false;
        }
        free(component_vars.items);
    }
    for (CettaExprLen index = 0u; vars && index < goal_len; index++)
        free(vars[index].items);
    free(vars);
    free(parent);
    if (!ok)
        return false;
    Atom *carried_atom = atom_expr(arena, delay ? delay->carried_vars : NULL,
                                   (CettaExprLen)carried);
    Atom *component_atom = atom_expr(arena, components, component_len);
    *payload = carried_atom && component_atom
        ? atom_expr2(arena, carried_atom, component_atom) : NULL;
    return *payload != NULL;
}

static bool petta_libpl_solution_bindings(
    Arena *arena, PettaLibplVarMap *variables,
    PettaLibplBackVarMap *unknown, Bindings *bindings) {
    if (!arena || !variables || !unknown || !bindings || variables->len > UINT32_MAX)
        return false;
    bindings_init(bindings);
    if (!petta_libpl_known_variables(variables, unknown))
        return false;
    enum { LOCAL_BINDINGS = 32 };
    Atom *local[LOCAL_BINDINGS * 2];
    if (variables->len > SIZE_MAX / (2u * sizeof(Atom *)))
        return false;
    Atom **batch = variables->len <= LOCAL_BINDINGS ? local
        : cetta_malloc(variables->len * 2u * sizeof(*batch));
    if (!batch)
        return false;
    Atom **values = batch + (variables->len <= LOCAL_BINDINGS ? LOCAL_BINDINGS : variables->len);
    uint32_t count = 0u;
    bool ok = true;
    for (size_t index = 0u; ok && index < variables->len; index++) {
        PettaLibplVar *variable = &variables->items[index];
        Atom *value = NULL;
        if (PL_is_variable(variable->term)) {
            if (variables->native_variable) {
                value = variables->native_variable(variables->slot_context, arena,
                                                    variable->term);
                if (value && value->var_id == variable->id)
                    continue;
            }
            if (!value) {
                size_t prior = petta_libpl_known_variable(variable->term, variables, unknown);
                if (prior == index)
                    continue;
                if (prior == SIZE_MAX || prior > index) {
                    ok = false;
                    break;
                }
                value = atom_var_like(arena, variables->items[prior].prototype,
                                       variables->items[prior].id);
            }
        } else {
            value = petta_libpl_from_term(arena, variable->term, variables, unknown, 0u);
        }
        Atom *prototype = value ? atom_var_like(arena, variable->prototype, variable->id) : NULL;
        if (!prototype) {
            ok = false;
            break;
        }
        batch[count] = prototype;
        values[count++] = value;
    }
    /* One owned publication, with the same ordered checked insertions. No
     * intermediate binding image escapes while the solution is decoded. */
    ok = ok && bindings_add_vars(bindings, batch, values, count);
    if (batch != local)
        free(batch);
    if (!ok)
        bindings_free(bindings);
    return ok;
}

static bool petta_libpl_emit_solution(
    Arena *arena, term_t result_term,
    Atom *constant_result,
    PettaLibplVarMap *variables,
    OutcomeSet *outcomes) {
    if (!arena || !variables || !outcomes ||
        (!result_term && !constant_result)) {
        return false;
    }
    /* One map from the solution's own unbound variables to fresh ones, for
     * its bindings, its value and its delayed goals alike. */
    PettaLibplBackVarMap unknown = {0};
    Bindings bindings;
    if (!petta_libpl_solution_bindings(
            arena, variables, &unknown, &bindings)) {
        petta_libpl_back_map_free(&unknown);
        return false;
    }
    Atom *result = constant_result
        ? constant_result
        : petta_libpl_from_term(
              arena, result_term, variables,
              &unknown, 0u);
    Atom *delayed = NULL;
    bool read = result &&
        (!variables->delay ||
         petta_libpl_delay_answer(arena, variables, &unknown, &delayed));
    petta_libpl_back_map_free(&unknown);
    if (!read) {
        bindings_free(&bindings);
        return false;
    }
    CettaCount before = outcomes->len;
    outcome_set_add(outcomes, result, &bindings);
    bindings_free(&bindings);
    if (delayed && outcomes->len == before + 1u)
        outcome_set_delay_last(outcomes, delayed);
    return outcomes->len == before + 1u;
}

/* CeTTa's atoms are finite trees, so a cyclic Prolog term has no atom.  A
 * solution that binds one raises an error the program can catch, rather than
 * failing the host. */
static bool petta_libpl_solution_is_cyclic(term_t result_term,
                                           const PettaLibplVarMap *variables) {
    if (result_term && !PL_is_acyclic(result_term))
        return true;
    for (size_t index = 0u; variables && index < variables->len; index++) {
        if (!PL_is_acyclic(variables->items[index].term))
            return true;
    }
    return false;
}

static Atom *petta_libpl_cyclic_value_error(Arena *arena) {
    Atom *head = atom_symbol(arena, "Error");
    Atom *kind = atom_symbol(arena, "representation_error");
    Atom *what = atom_symbol(arena, "acyclic_term");
    Atom *formal = kind && what ? atom_expr2(arena, kind, what) : NULL;
    return head && formal ? atom_expr2(arena, head, formal) : NULL;
}

/* The error term with this runtime's module unqualified.  The module is
 * CeTTa's private stand-in for the reference's `user`, which SWI leaves
 * unqualified in an error's culprit and context. */
static Atom *petta_libpl_unqualify_module(
    Arena *arena, Atom *atom, const char *module, uint32_t depth) {
    if (!atom || !module || depth > 1024u)
        return atom;
    Atom *body = NULL;
    bool compound = atom_petta_prolog_compound_body(atom, &body);
    Atom *view = compound ? body : atom;
    if (!view || view->kind != ATOM_EXPR)
        return atom;
    if (view->expr.len == 3u &&
        petta_libpl_atom_is_symbol(view->expr.elems[0], ":") &&
        petta_libpl_atom_is_symbol(view->expr.elems[1], module))
        return petta_libpl_unqualify_module(
            arena, view->expr.elems[2], module, depth + 1u);
    Atom **items = NULL;
    for (CettaExprIndex index = 0u; index < view->expr.len; index++) {
        Atom *item = view->expr.elems[index];
        Atom *next = petta_libpl_unqualify_module(
            arena, item, module, depth + 1u);
        if (!next)
            return NULL;
        if (next == item && !items)
            continue;
        if (!items) {
            items = arena_alloc(arena, sizeof(*items) * view->expr.len);
            if (!items)
                return NULL;
            memcpy(items, view->expr.elems, sizeof(*items) * index);
        }
        items[index] = next;
    }
    if (!items)
        return atom;
    Atom *rebuilt = atom_expr(arena, items, view->expr.len);
    return rebuilt && compound
        ? atom_petta_prolog_compound(arena, rebuilt) : rebuilt;
}

/* error(Formal, Context) reads as (Error Formal Context); any other thrown
 * term as (Error Term). */
static Atom *petta_libpl_exception_value(Arena *arena, term_t exception) {
    PettaLibplVarMap variables = {.private_module = true};
    PettaLibplBackVarMap unknown = {0};
    Atom *thrown = petta_libpl_from_term(
        arena, exception, &variables, &unknown, 0u);
    petta_libpl_var_map_free(&variables);
    petta_libpl_back_map_free(&unknown);
    Atom *head = atom_symbol(arena, "Error");
    if (thrown && g_petta_libpl_active_runtime &&
        g_petta_libpl_active_runtime->module_name)
        thrown = petta_libpl_unqualify_module(
            arena, thrown, g_petta_libpl_active_runtime->module_name, 0u);
    if (!thrown || !head)
        return NULL;
    Atom *compound = NULL;
    if (atom_petta_prolog_compound_body(thrown, &compound) && compound)
        thrown = compound;
    if (thrown->kind == ATOM_EXPR && thrown->expr.len == 3u &&
        petta_libpl_atom_is_symbol(thrown->expr.elems[0], "error")) {
        return atom_expr3(arena, head,
                          thrown->expr.elems[1], thrown->expr.elems[2]);
    }
    return atom_expr2(arena, head, thrown);
}

/* --- Query release ------------------------------------------------------
 * Behavior established for the build used here (SWI 10.1.9), verified by the
 * lifecycle gate alongside the permanent fixtures:
 *  - PL_cut_query/PL_close_query return true, false, or PL_S_NOT_INNER.
 *    false means the release's cleanup raised a NEW exception, pending at
 *    PL_exception(0); PL_S_NOT_INNER means the release was refused and the
 *    query remains open (the release obligation persists; nothing was
 *    cancelled silently).
 *  - PL_close_query undoes the trailing choice; PL_cut_query keeps the
 *    solved state (run_query's first_only and static-import keep effects).
 *  - A body exception consumed through PL_exception(query) takes
 *    precedence: cleanup then ran during the body exception and its own
 *    fault does not reach the release.  So a cleanup fault reaching us here
 *    is new and must be propagated without replacing the body's payload.
 */
/* Release `query` per `kind`.  The pending cleanup exception, if any, is
 * converted into *(cleanup_raised) when that parameter is non-NULL and the
 * slot is not already occupied — a consumed body exception transported by
 * the caller takes precedence over a cleanup fault (SWI swallows the
 * cleanup fault in that shape anyway); the pending exception after the
 * release is cleared either way, so a later query on this thread does not
 * read it as its own error. PETTA_LIBPL_RELEASE_NOT_INNER means the query
 * is still open and its id stays valid. */
static PettaLibplReleaseStatus petta_libpl_release_query(
    Arena *arena, Atom **cleanup_raised, qid_t query,
    PettaLibplReleaseKind kind) {
    if (!query)
        return PETTA_LIBPL_RELEASE_OK;
    const int rc = kind == PETTA_LIBPL_RELEASE_CUT
        ? PL_cut_query(query) : PL_close_query(query);
    if (rc == PL_S_NOT_INNER)
        return PETTA_LIBPL_RELEASE_NOT_INNER;
    if (rc)
        return PETTA_LIBPL_RELEASE_OK;
    /* The exact SWI value crosses as-is (PeTTa catch must see the same term
     * the reference's catch sees); provenance lives in the returned
     * status, never in the payload. */
    if (cleanup_raised && !*cleanup_raised) {
        term_t exception = PL_exception(0);
        if (exception)
            *cleanup_raised =
                petta_libpl_exception_value(arena, exception);
    }
    PL_clear_exception();
    return PETTA_LIBPL_RELEASE_CLEANUP_ERROR;
}

static bool petta_libpl_run_query(
    CettaLibPrologRuntime *runtime, Arena *arena,
    predicate_t predicate, term_t arguments,
    term_t result_term, Atom *constant_result,
    PettaLibplVarMap *variables,
    OutcomeSet *outcomes, bool first_only,
    bool *succeeded, Atom **raised) {
    if (succeeded)
        *succeeded = false;
    if (raised)
        *raised = NULL;
    if (!runtime || !arena || !predicate ||
        !arguments || !variables || !outcomes ||
        !succeeded) {
        return false;
    }
    qid_t query = PL_open_query(
        runtime->module,
        PL_Q_NODEBUG | PL_Q_CATCH_EXCEPTION |
            PL_Q_EXT_STATUS,
        predicate, arguments);
    if (!query)
        return false;

    Arena *previous_active_arena =
        g_petta_libpl_active_arena;
    g_petta_libpl_active_arena = arena;
    bool ok = true;
    for (;;) {
        int status = PL_next_solution(query);
        if (status == PL_S_FALSE)
            break;
        if (status == PL_S_EXCEPTION) {
            /* The raised error, read while the query still holds it; the
             * caller decides whether it is a value or a failed call. */
            term_t exception = PL_exception(query);
            if (raised && exception)
                *raised = petta_libpl_exception_value(arena, exception);
            ok = false;
            break;
        }
        if (status == PL_S_YIELD ||
            status == PL_S_YIELD_DEBUG ||
            (status != PL_S_TRUE &&
             status != PL_S_LAST)) {
            ok = false;
            break;
        }
        *succeeded = true;
        /* Solution traversal references must not become roots into a later
         * backtracked answer. Native outcomes own their converted values. */
        fid_t solution_frame = PL_open_foreign_frame();
        if (!solution_frame) {
            ok = false;
            break;
        }
        bool emitted = petta_libpl_emit_solution(
            arena, result_term, constant_result, variables, outcomes);
        if (!emitted && petta_libpl_solution_is_cyclic(
                            result_term, variables)) {
            /* A solution that is not finite trees crosses as the term
             * graphs it presents (term_graph.h). */
            variables->cyclic_graphs = true;
            emitted = petta_libpl_emit_solution(
                arena, result_term, constant_result, variables, outcomes);
            variables->cyclic_graphs = false;
            if (!emitted && raised)
                *raised = petta_libpl_cyclic_value_error(arena);
        }
        PL_discard_foreign_frame(solution_frame);
        if (!emitted) {
            ok = false;
            break;
        }
        if (first_only || status == PL_S_LAST)
            break;
    }
    /* The release is unconditional: when the body or a conversion already
     * failed, the query must still be discharged. */
    {
        const PettaLibplReleaseStatus release_status =
            petta_libpl_release_query(
                arena, raised, query,
                first_only && *succeeded
                    ? PETTA_LIBPL_RELEASE_CUT
                    : PETTA_LIBPL_RELEASE_CLOSE);
        ok = ok && release_status == PETTA_LIBPL_RELEASE_OK;
    }
    g_petta_libpl_active_arena =
        previous_active_arena;
    return ok;
}

static bool petta_libpl_registered_call(
    CettaLibPrologRuntime *runtime, Arena *arena,
    Atom *expression, Atom *expected,
    PettaLibplImport *entry,
    OutcomeSet *outcomes, Atom **raised, PettaLibplDelay *delay) {
    if (raised)
        *raised = NULL;
    if (!runtime || !arena || !expression ||
        expression->kind != ATOM_EXPR ||
        expression->expr.len == 0u || !expected ||
        !entry || !outcomes ||
        expression->expr.len > (CettaExprLen)INT_MAX) {
        return false;
    }
    if (!entry->reference_stdlib)
        petta_libpl_advance_revision(runtime);

    PettaLibplPlrefHandle erased_handle;
    bool releases_handle =
        expression->expr.len == 2u &&
        strcmp(entry->name, "erase") == 0 &&
        petta_libpl_plref_tagged(expression->expr.elems[1]);
    if (releases_handle &&
        (!petta_libpl_plref_view(
             expression->expr.elems[1], &erased_handle) ||
         !petta_libpl_plref_matches(runtime, &erased_handle))) {
        return true;
    }

    size_t predicate_arity =
        (size_t)expression->expr.len;
    term_t arguments =
        PL_new_term_refs(predicate_arity);
    if (!arguments)
        return false;
    PettaLibplVarMap variables = {0};
    bool converted = true;
    for (size_t index = 0u;
         converted && index + 1u < predicate_arity;
         index++) {
        converted = petta_libpl_to_term(
            expression->expr.elems[index + 1u],
            arguments + index, &variables, 0u);
    }
    converted =
        converted &&
        petta_libpl_to_term(
            expected,
            arguments + predicate_arity - 1u,
            &variables, 0u);
    if (!converted) {
        petta_libpl_var_map_free(&variables);
        return false;
    }

    term_t callable = PL_new_term_ref();
    term_t result = PL_new_term_ref();
    atom_t name = petta_libpl_new_utf8_atom(
        entry->name, entry->name_len);
    functor_t functor =
        PL_new_functor_sz(name, predicate_arity);
    bool built =
        callable && result && functor &&
        PL_cons_functor_v(
            callable, functor, arguments) &&
        PL_get_arg(
            (int)predicate_arity, callable, result);
    PL_unregister_atom(name);
    if (!built) {
        petta_libpl_var_map_free(&variables);
        return false;
    }

    /*
     * Invoke through call/1 in the runtime's context module.  This is the
     * public FLI path that preserves SWI's module import and autoload
     * semantics; constructing a predicate handle for the fresh module can
     * instead select an undefined local predicate with the same indicator.
     */
    variables.delay = delay;
    if (!petta_libpl_delay_wrap(&variables, callable)) {
        petta_libpl_var_map_free(&variables);
        return false;
    }
    predicate_t call = PL_predicate(
        "call", 1, runtime->module_name);
    bool succeeded = false;
    bool ok = petta_libpl_run_query(
        runtime, arena, call, callable,
        result, NULL,
        &variables, outcomes, false, &succeeded, raised);
    if (ok && succeeded && releases_handle &&
        !petta_libpl_plref_release(runtime, &erased_handle)) {
        ok = false;
    }
    petta_libpl_var_map_free(&variables);
    if (ok && succeeded &&
        (strcmp(entry->name, "consult") == 0 ||
         strcmp(entry->name, "use_module") == 0 ||
         strcmp(entry->name, "ensure_loaded") == 0 ||
         strcmp(entry->name, "load_files") == 0)) {
        petta_libpl_advance_revision(runtime);
    }
    return ok;
}

/*
 * Run one goal in the embedded Prolog: every solution is an outcome carrying
 * the bindings of the goal's free variables.  With `answers_success`, the
 * goal is callPredicate's argument, a value that may be any term, which
 * call/1 takes as it is (metta.pl:390), and each answer's value is the
 * success value.  Otherwise it is translatePredicate's goal, an application
 * naming its predicate, and the value stays unbound (a fresh variable).  A
 * goal is an opaque effect boundary; even one that later fails may already
 * have changed the dynamic predicate database.
 */
static bool petta_libpl_call_goal(
    CettaLibPrologRuntime *runtime, Arena *arena,
    Atom *body, bool answers_success, OutcomeSet *outcomes,
    bool *succeeded, Atom **raised, PettaLibplDelay *delay) {
    if (succeeded)
        *succeeded = false;
    /* The goal runs as the reference runs it, so what it raises carries the
     * same context: callPredicate's argument through the reference's own
     * clause, callPredicate(G, true) :- call(G), and translatePredicate's
     * goal inside a conjunction, as it sits in the reference's compiled
     * body.  Carried goals come before the whole call. */
    term_t goal = PL_new_term_ref();
    term_t parts = PL_new_term_refs(2);
    PettaLibplVarMap variables = {.delay = delay};
    atom_t name = PL_new_atom(answers_success ? "callPredicate" : ",");
    functor_t shape = name ? PL_new_functor(name, 2) : 0;
    if (name)
        PL_unregister_atom(name);
    bool converted =
        goal && parts && shape &&
        (answers_success
             ? petta_libpl_to_term(body, parts, &variables, 0u) &&
                   PL_put_variable(parts + 1)
             : petta_libpl_put_utf8(parts, PL_ATOM, "true", 4u) &&
                   petta_libpl_to_callable(
                       body, parts + 1, &variables, 0u)) &&
        PL_cons_functor_v(goal, shape, parts) &&
        petta_libpl_delay_wrap(&variables, goal);
    if (!converted) {
        petta_libpl_var_map_free(&variables);
        return false;
    }
    predicate_t call = PL_predicate(
        "call", 1, runtime->module_name);
    Atom *success = answers_success
        ? petta_semantics_success_value(arena)
        : atom_var_with_id(arena, "$", fresh_var_id());
    bool solved = false;
    bool attempted = success != NULL;
    bool ok = attempted && petta_libpl_run_query(
        runtime, arena, call, goal, 0,
        success, &variables, outcomes,
        false, &solved, raised);
    /* The goal's predicate exists, so a raised error is reported to the
     * caller, which propagates it as a MeTTa error. */
    if (!ok && raised && *raised)
        ok = true;
    if (attempted)
        petta_libpl_advance_revision(runtime);
    if (succeeded)
        *succeeded = solved;
    petta_libpl_var_map_free(&variables);
    return ok;
}

/* callPredicate(G, true) :- call(G) (metta.pl:390): `goal` is the value
 * of the argument, any term. */
static bool petta_libpl_call_predicate(
    CettaLibPrologRuntime *runtime, Arena *arena,
    Atom *goal, OutcomeSet *outcomes, Atom **raised,
    PettaLibplDelay *delay) {
    if (!runtime || !arena || !goal || !outcomes)
        return false;
    Atom *body = NULL;
    PettaLibplPlrefHandle erased_handle;
    bool releases_handle =
        petta_libpl_predicate_body(goal, &body) &&
        body->kind == ATOM_EXPR &&
        body->expr.len == 2u &&
        petta_libpl_atom_is_symbol(
            body->expr.elems[0], "erase") &&
        petta_libpl_plref_tagged(body->expr.elems[1]);
    if (releases_handle &&
        (!petta_libpl_plref_view(
             body->expr.elems[1], &erased_handle) ||
         !petta_libpl_plref_matches(runtime, &erased_handle))) {
        return true;
    }
    bool succeeded = false;
    bool ok = petta_libpl_call_goal(
        runtime, arena, goal, true, outcomes, &succeeded, raised, delay);
    if (ok && succeeded && releases_handle &&
        !petta_libpl_plref_release(runtime, &erased_handle)) {
        ok = false;
    }
    return ok;
}

/* Raise (Error Formal Context) as the Prolog exception error(Formal,
 * Context), and (Error Term) as Term: the reading of
 * petta_libpl_exception_value, reversed.  False either way, as a foreign
 * predicate that raises returns. */
static foreign_t petta_libpl_raise(Atom *error) {
    if (!error || error->kind != ATOM_EXPR ||
        error->expr.len < 2u || error->expr.len > 3u)
        return false;
    PettaLibplVarMap variables = {0};
    term_t thrown = PL_new_term_ref();
    bool built = thrown != 0;
    if (built && error->expr.len == 3u) {
        term_t parts = PL_new_term_refs(2);
        atom_t name = PL_new_atom("error");
        functor_t error_functor = PL_new_functor(name, 2);
        PL_unregister_atom(name);
        built = parts &&
                petta_libpl_to_term(
                    error->expr.elems[1], parts, &variables, 0u) &&
                petta_libpl_to_term(
                    error->expr.elems[2], parts + 1, &variables, 0u) &&
                PL_cons_functor_v(thrown, error_functor, parts);
    } else if (built) {
        built = petta_libpl_to_term(
            error->expr.elems[1], thrown, &variables, 0u);
    }
    petta_libpl_var_map_free(&variables);
    if (built)
        (void)PL_raise_exception(thrown);
    return false;
}

/*
 * Asserted PeTTa clauses may contain Predicate-wrapped calls to the same
 * grounded operations used by the native evaluator.  Register those exact
 * function-convention arities in the private SWI module and dispatch them
 * back through grounded_dispatch.  Pure operations are admitted generally;
 * py-call is the one explicit callback needed by consulted PeTTa dispatchers.
 * This keeps one implementation and one authority boundary.
 */
static foreign_t petta_libpl_grounded_bridge(
    term_t arguments, int supplied_arity,
    control_t control) {
    if (!g_petta_libpl_active_arena ||
        !g_symbols || supplied_arity < 1) {
        return false;
    }
    predicate_t predicate =
        PL_foreign_context_predicate(control);
    atom_t predicate_name = 0;
    size_t predicate_arity = 0u;
    if (!predicate ||
        !PL_predicate_info(
            predicate, &predicate_name,
            &predicate_arity, NULL) ||
        predicate_arity != (size_t)supplied_arity ||
        predicate_arity - 1u > UINT32_MAX) {
        return false;
    }
    size_t name_len = 0u;
    char *name = NULL;
    if (!petta_libpl_atom_utf8(
            predicate_name, &name, &name_len) ||
        name_len > UINT32_MAX) {
        if (name)
            PL_free(name);
        return false;
    }
    SymbolId head_id = symbol_intern_bytes(
        g_symbols, (const uint8_t *)name,
        (uint32_t)name_len);
    PL_free(name);
    CettaExprLen function_arity = 0u;
    bool explicit_python_callback =
        head_id != SYMBOL_ID_NONE &&
        symbol_eq_cstr(g_symbols, head_id, "py-call");
    bool arity_matches = explicit_python_callback
        ? predicate_arity == 2u
        : petta_semantics_intrinsic_partial_arity(
              head_id, &function_arity) &&
          function_arity ==
              (CettaExprLen)(predicate_arity - 1u);
    if (head_id == SYMBOL_ID_NONE ||
        (!explicit_python_callback &&
         !grounded_op_is_type_pure(head_id)) ||
        !arity_matches) {
        return false;
    }

    uint32_t input_count =
        (uint32_t)(predicate_arity - 1u);
    Atom **inputs = input_count
        ? arena_alloc(
              g_petta_libpl_active_arena,
              sizeof(*inputs) * (size_t)input_count)
        : NULL;
    if (input_count && !inputs)
        return false;

    PettaLibplVarMap variables = {0};
    PettaLibplBackVarMap unknown = {0};
    bool converted = true;
    for (uint32_t index = 0u;
         converted && index < input_count; index++) {
        term_t input = arguments + index;
        converted =
            PL_is_ground(input) &&
            (inputs[index] = petta_libpl_from_term(
                 g_petta_libpl_active_arena,
                 input, &variables, &unknown, 0u));
    }
    petta_libpl_var_map_free(&variables);
    petta_libpl_back_map_free(&unknown);
    if (!converted)
        return false;

    Atom *head = atom_symbol_id(
        g_petta_libpl_active_arena, head_id);
    CettaLibraryContext *library = eval_current_library_context();
    CettaCallOutcome outcome = cetta_call_failure();
    bool called = false;
    if (head && explicit_python_callback && library &&
        library->foreign_runtime) {
        called = cetta_foreign_call_native(
            library->foreign_runtime, NULL,
            g_petta_libpl_active_arena,
            head, inputs, input_count, &outcome);
    } else if (head && !explicit_python_callback) {
        called = grounded_call(
            g_petta_libpl_active_arena,
            head, inputs, input_count, &outcome);
    }
    if (!called)
        return false;
    /* The operation's error is a Prolog exception here, as the reference's
     * own predicate raises it. */
    if (outcome.kind == CETTA_CALL_RAISED)
        return petta_libpl_raise(outcome.term);
    Atom *result = outcome.kind == CETTA_CALL_VALUE ? outcome.term : NULL;
    if (!result)
        return false;

    term_t result_term = PL_new_term_ref();
    PettaLibplVarMap result_variables = {0};
    bool encoded =
        result_term &&
        petta_libpl_to_term(
            result, result_term,
            &result_variables, 0u);
    petta_libpl_var_map_free(&result_variables);
    return encoded &&
           PL_unify(
               arguments + predicate_arity - 1u,
               result_term);
}

static bool petta_libpl_predicate_exists(
    CettaLibPrologRuntime *runtime,
    const char *name, size_t name_len, size_t arity,
    bool *exists) {
    if (exists)
        *exists = false;
    if (!runtime || !runtime->module || !name ||
        !exists || arity > INT64_MAX) {
        return false;
    }
    term_t indicator = PL_new_term_ref();
    term_t indicator_args = PL_new_term_refs(2u);
    atom_t slash = PL_new_atom("/");
    functor_t indicator_functor =
        PL_new_functor_sz(slash, 2u);
    bool built =
        indicator && indicator_args &&
        indicator_functor &&
        petta_libpl_put_utf8(
            indicator_args, PL_ATOM,
            name, name_len) &&
        PL_put_int64(
            indicator_args + 1u, (int64_t)arity) &&
        PL_cons_functor_v(
            indicator, indicator_functor,
            indicator_args);
    PL_unregister_atom(slash);
    if (!built)
        return false;

    predicate_t current_predicate =
        PL_predicate("current_predicate", 1, NULL);
    qid_t query = current_predicate
        ? PL_open_query(
              runtime->module,
              PL_Q_NODEBUG | PL_Q_CATCH_EXCEPTION |
                  PL_Q_EXT_STATUS,
              current_predicate, indicator)
        : 0;
    if (!query)
        return false;
    int status = PL_next_solution(query);
    *exists =
        status == PL_S_TRUE ||
        status == PL_S_LAST;
    bool ok =
        *exists || status == PL_S_FALSE;
    /* Fixed current_predicate probe: a failed release makes the answer
     * untrustworthy; the release itself is unconditional. */
    {
        const PettaLibplReleaseStatus release_status =
            petta_libpl_release_query(NULL, NULL, query,
                                      PETTA_LIBPL_RELEASE_CLOSE);
        ok = ok && release_status == PETTA_LIBPL_RELEASE_OK;
    }
    if (!ok || *exists)
        return ok;
    /* A library predicate SWI autoloads when first called, as dif/2 and
     * when/2 are, exists too: predicate_property/2 with `defined` loads it,
     * and still fails for a name that is nowhere. */
    static predicate_t property;
    if (!property)
        property = PL_predicate("predicate_property", 2, "system");
    term_t args = PL_new_term_refs(2);
    term_t head = PL_new_term_ref();
    term_t module = PL_new_term_ref();
    atom_t name_atom = petta_libpl_new_utf8_atom(name, name_len);
    functor_t head_functor = name_atom
        ? PL_new_functor_sz(name_atom, (size_t)arity) : 0;
    atom_t colon = PL_new_atom(":");
    functor_t qualified = PL_new_functor(colon, 2);
    PL_unregister_atom(colon);
    atom_t defined = PL_new_atom("defined");
    atom_t module_atom = PL_new_atom(
        runtime->module_name ? runtime->module_name : "user");
    term_t parts = PL_new_term_refs(2);
    bool asked = args && head && module && parts && head_functor &&
                 PL_put_functor(head, head_functor) &&
                 PL_put_atom(module, module_atom) &&
                 PL_put_term(parts, module) && PL_put_term(parts + 1, head) &&
                 PL_cons_functor_v(args, qualified, parts) &&
                 PL_put_atom(args + 1, defined);
    if (name_atom)
        PL_unregister_atom(name_atom);
    PL_unregister_atom(defined);
    PL_unregister_atom(module_atom);
    if (!asked)
        return false;
    *exists = PL_call_predicate(NULL, PL_Q_NODEBUG | PL_Q_CATCH_EXCEPTION,
                                property, args);
    return true;
}

static bool petta_libpl_register_grounded_bridge(
    CettaLibPrologRuntime *runtime, SymbolId head,
    size_t predicate_arity) {
    if (!runtime || !runtime->module_name ||
        !g_symbols || head == SYMBOL_ID_NONE ||
        predicate_arity > INT_MAX) {
        return false;
    }
    const char *name = symbol_bytes(g_symbols, head);
    size_t name_len = symbol_len(g_symbols, head);
    bool exists = false;
    if (!name ||
        !petta_libpl_predicate_exists(
            runtime, name, name_len,
            predicate_arity, &exists)) {
        return false;
    }
    if (exists)
        return true;
    return PL_register_foreign_in_module(
        runtime->module_name, name,
        (int)predicate_arity,
        (pl_function_t)petta_libpl_grounded_bridge,
        PL_FA_VARARGS);
}

static bool petta_libpl_register_clause_grounded_calls(
    CettaLibPrologRuntime *runtime, Atom *atom,
    uint32_t depth) {
    if (!runtime || !atom || depth > 1024u)
        return false;
    Atom *body = NULL;
    if (petta_libpl_predicate_body(atom, &body)) {
        if (body->kind == ATOM_EXPR &&
            body->expr.len >= 2u &&
            body->expr.elems[0]->kind == ATOM_SYMBOL) {
            SymbolId head =
                body->expr.elems[0]->sym_id;
            CettaExprLen function_arity = 0u;
            size_t predicate_arity =
                (size_t)(body->expr.len - 1u);
            if (grounded_op_is_type_pure(head) &&
                petta_semantics_intrinsic_partial_arity(
                    head, &function_arity) &&
                function_arity + 1u ==
                    (CettaExprLen)predicate_arity &&
                !petta_libpl_register_grounded_bridge(
                    runtime, head, predicate_arity)) {
                return false;
            }
        }
        return petta_libpl_register_clause_grounded_calls(
            runtime, body, depth + 1u);
    }
    if (atom->kind != ATOM_EXPR)
        return true;
    for (CettaExprIndex index = 0u;
         index < atom->expr.len; index++) {
        if (!petta_libpl_register_clause_grounded_calls(
                runtime, atom->expr.elems[index],
                depth + 1u)) {
            return false;
        }
    }
    return true;
}

/* assertaPredicate(G, true) :- asserta(G), and assertzPredicate with
 * assertz (metta.pl:391-392): `clause_value` is the value of the argument,
 * any term, and what asserting it raises is the call's error. */
static bool petta_libpl_assert_predicate(
    CettaLibPrologRuntime *runtime, Arena *arena,
    Atom *clause_value, OutcomeSet *outcomes,
    bool at_start, Atom **raised) {
    if (raised)
        *raised = NULL;
    if (!runtime || !arena || !clause_value || !outcomes)
        return false;
    term_t clause = PL_new_term_ref();
    PettaLibplVarMap variables = {0};
    bool converted =
        clause &&
        petta_libpl_to_term(
            clause_value, clause, &variables, 0u);
    converted =
        converted &&
        petta_libpl_register_clause_grounded_calls(
            runtime, clause_value, 0u);
    if (!converted) {
        petta_libpl_var_map_free(&variables);
        return false;
    }
    Atom *success = petta_semantics_success_value(arena);
    bool asserted = PL_assert(
        clause, runtime->module,
        at_start ? PL_ASSERTA : PL_ASSERTZ);
    if (!asserted) {
        /* Refused, the clause is not stored: asserted through the
         * predicate itself, it raises that predicate's error, context
         * included, as the reference's call does. */
        PL_clear_exception();
        predicate_t predicate = PL_predicate(
            at_start ? "asserta" : "assertz", 1, runtime->module_name);
        bool succeeded = false;
        bool ok = success && petta_libpl_run_query(
            runtime, arena, predicate, clause, 0, success, &variables,
            outcomes, true, &succeeded, raised);
        if (succeeded)
            petta_libpl_advance_revision(runtime);
        petta_libpl_var_map_free(&variables);
        return ok || (raised && *raised);
    }
    asserted = success &&
               petta_libpl_emit_solution(
                   arena, 0, success, &variables, outcomes);
    petta_libpl_advance_revision(runtime);
    petta_libpl_var_map_free(&variables);
    return asserted;
}

/* retractPredicate(G, true) :- retract(G), !.  retractPredicate(_, false).
 * (metta.pl:393-394): `clause_value` is the value of the argument, any
 * term; what retract/1 raises is the call's error. */
static bool petta_libpl_retract_predicate(
    CettaLibPrologRuntime *runtime, Arena *arena,
    Atom *clause_value, OutcomeSet *outcomes, Atom **raised) {
    if (raised)
        *raised = NULL;
    if (!runtime || !arena || !clause_value || !outcomes)
        return false;
    term_t clause = PL_new_term_ref();
    PettaLibplVarMap variables = {0};
    bool converted =
        clause &&
        petta_libpl_to_term(
            clause_value, clause, &variables, 0u);
    if (!converted) {
        petta_libpl_var_map_free(&variables);
        return false;
    }
    predicate_t retract_predicate = PL_predicate(
        "retract", 1, runtime->module_name);
    Atom *success =
        petta_semantics_success_value(arena);
    bool succeeded = false;
    bool ok = success &&
              petta_libpl_run_query(
                  runtime, arena, retract_predicate,
                  clause, 0, success, &variables,
                  outcomes, true, &succeeded, raised);
    if (!ok && raised && *raised) {
        petta_libpl_var_map_free(&variables);
        return true;
    }
    if (ok && succeeded) {
        petta_libpl_advance_revision(runtime);
    } else if (ok) {
        Atom *failure =
            petta_semantics_boolean_value(arena, false);
        ok = failure &&
             petta_libpl_emit_solution(
                 arena, 0, failure, &variables,
                 outcomes);
    }
    petta_libpl_var_map_free(&variables);
    return ok;
}

CettaLibPrologRuntime *cetta_lib_prolog_runtime_new(void) {
    CettaLibPrologRuntime *runtime =
        cetta_malloc(sizeof(*runtime));
    memset(runtime, 0, sizeof(*runtime));
    arena_init_detached(&runtime->callable_codes);
    uint64_t instance_id = atomic_fetch_add_explicit(
        &g_petta_libpl_runtime_counter, 1u,
        memory_order_relaxed);
    if (instance_id == 0u ||
        instance_id > (uint64_t)INT64_MAX) {
        arena_free(&runtime->callable_codes);
        free(runtime);
        return NULL;
    }
    runtime->instance_id = instance_id;
    runtime->revision = 1u;
    atomic_init(&runtime->capability_revision, 1u);
    runtime->plref_free_head = SIZE_MAX;
    runtime->plref_next_generation = 1u;
    for (size_t index = 0u; index < 1024u; index++)
        atomic_init(&runtime->import_admission[index], 0u);
    atomic_init(&runtime->import_admission_saturated, false);
    /*
     * Capability discovery is source-planning metadata, not execution.
     * Publish the reference PeTTa syntax while the language context is
     * built, without starting an SWI engine.  This lets the first authored
     * equation receive the same call/data classification as every later
     * equation.  Engine preparation remains lazy and merely fills the
     * concrete arities of these already-admitted names.
     */
    petta_libpl_register_reference_stdlib(runtime);
    return runtime;
}

uint64_t petta_libpl_capability_revision(
    const CettaLibPrologRuntime *runtime) {
    return runtime
        ? atomic_load_explicit(
              &runtime->capability_revision, memory_order_acquire)
        : 0u;
}

bool cetta_lib_prolog_runtime_set_working_dir(
    CettaLibPrologRuntime *runtime, const char *path) {
    if (!runtime || !path || path[0] == '\0')
        return false;
    char *copy = petta_libpl_copy_bytes(path, strlen(path));
    if (!copy)
        return false;

    if (!runtime->prepared) {
        free(runtime->working_dir);
        runtime->working_dir = copy;
        return true;
    }

    bool claimed = false;
    if (!petta_libpl_enter(&claimed)) {
        free(copy);
        return false;
    }
    bool installed = petta_libpl_install_working_dir(
        runtime, copy, true);
    if (installed) {
        free(runtime->working_dir);
        runtime->working_dir = copy;
    } else {
        free(copy);
    }
    petta_libpl_leave(claimed);
    return installed;
}

void cetta_lib_prolog_runtime_free(
    CettaLibPrologRuntime *runtime) {
    if (!runtime)
        return;
    if (runtime->plref_len > 0u) {
        bool claimed = false;
        if (petta_libpl_enter(&claimed)) {
            petta_libpl_plref_release_all(runtime);
            petta_libpl_leave(claimed);
        }
    }
    for (size_t index = 0u;
         index < runtime->import_len; index++) {
        petta_libpl_import_clear(
            &runtime->imports[index]);
    }
    free(runtime->imports);
    free(runtime->probe_misses);
    free(runtime->plrefs);
    free(runtime->callables);
    arena_free(&runtime->callable_codes);
    free(runtime->module_name);
    free(runtime->static_module_name);
    free(runtime->solver_module_name);
    free(runtime->working_dir);
    free(runtime);
}

CettaLibPrologReadToken cetta_lib_prolog_read_token(
    CettaLibPrologRuntime *runtime) {
    CettaLibPrologReadToken token = {0};
    if (!runtime)
        return token;
    /* Instance identity is immutable for the runtime lifetime.  Capability
     * mutation publishes through the dedicated atomic revision, so a validity
     * read neither needs the engine mutex nor risks recursive-entry deadlock. */
    token.instance_id = runtime->instance_id;
    token.revision = atomic_load_explicit(
        &runtime->capability_revision, memory_order_acquire);
    return token;
}

bool cetta_lib_prolog_runtime_available(
    CettaLibPrologRuntime *runtime) {
    if (!runtime)
        return false;
    bool claimed = false;
    if (!petta_libpl_enter(&claimed))
        return false;
    bool available = petta_libpl_prepare_locked(runtime);
    petta_libpl_leave(claimed);
    return available;
}

CettaLibPrologQueryStatus cetta_lib_prolog_query(
    CettaLibPrologRuntime *runtime, Arena *arena,
    Atom *goal, Atom *projection, Atom **answers) {
    if (answers)
        *answers = NULL;
    if (!runtime || !arena || !goal || !projection ||
        !answers) {
        return runtime
            ? CETTA_LIB_PROLOG_QUERY_FAILED
            : CETTA_LIB_PROLOG_QUERY_UNAVAILABLE;
    }

    bool claimed = false;
    if (!petta_libpl_enter(&claimed))
        return CETTA_LIB_PROLOG_QUERY_UNAVAILABLE;
    if (!petta_libpl_prepare_locked(runtime)) {
        petta_libpl_leave(claimed);
        return CETTA_LIB_PROLOG_QUERY_UNAVAILABLE;
    }

    fid_t frame = PL_open_foreign_frame();
    if (!frame) {
        petta_libpl_leave(claimed);
        return CETTA_LIB_PROLOG_QUERY_FAILED;
    }

    CettaLibPrologRuntime *previous_active_runtime =
        g_petta_libpl_active_runtime;
    g_petta_libpl_active_runtime = runtime;

    term_t goal_term = PL_new_term_ref();
    term_t projection_term = PL_new_term_ref();
    PettaLibplVarMap variables = {0};
    Atom *projection_body = projection;
    Atom *unquoted = NULL;
    if (petta_libpl_quote_body(
            projection, &unquoted)) {
        projection_body = unquoted;
    }
    bool converted =
        goal_term && projection_term &&
        petta_libpl_to_callable(
            goal, goal_term, &variables, 0u) &&
        petta_libpl_to_term(
            projection_body, projection_term,
            &variables, 0u);

    Atom **items = NULL;
    size_t length = 0u;
    size_t capacity = 0u;
    qid_t query = 0;
    bool ok = converted;
    if (ok) {
        predicate_t call = PL_predicate(
            "call", 1, runtime->module_name);
        query = call
            ? PL_open_query(
                  runtime->module,
                  PL_Q_NODEBUG |
                  PL_Q_CATCH_EXCEPTION |
                      PL_Q_EXT_STATUS,
                  call, goal_term)
            : 0;
        ok = query != 0;
    }

    Arena *previous_active_arena =
        g_petta_libpl_active_arena;
    g_petta_libpl_active_arena = arena;
    while (ok) {
        int status = PL_next_solution(query);
        if (status == PL_S_FALSE)
            break;
        if (status == PL_S_EXCEPTION ||
            status == PL_S_YIELD ||
            status == PL_S_YIELD_DEBUG ||
            (status != PL_S_TRUE &&
             status != PL_S_LAST)) {
            ok = false;
            break;
        }

        fid_t solution_frame = PL_open_foreign_frame();
        PettaLibplBackVarMap unknown = {0};
        Atom *item = solution_frame
            ? petta_libpl_from_term_mode(
                  arena, projection_term, &variables, &unknown, true, 0u)
            : NULL;
        petta_libpl_back_map_free(&unknown);
        if (solution_frame)
            PL_discard_foreign_frame(solution_frame);
        if (!item) {
            ok = false;
            break;
        }
        if (length == capacity) {
            size_t next =
                capacity ? capacity * 2u : 8u;
            if (next <= capacity ||
                next > SIZE_MAX / sizeof(*items)) {
                ok = false;
                break;
            }
            items = items
                ? cetta_realloc(
                      items, sizeof(*items) * next)
                : cetta_malloc(
                      sizeof(*items) * next);
            capacity = next;
        }
        items[length++] = item;
        if (status == PL_S_LAST)
            break;
    }
    g_petta_libpl_active_arena =
        previous_active_arena;

    if (query) {
        /* This public entry accepts an arbitrary Prolog goal, so execution
         * invalidates namespace observations whether or not it succeeds.
         * A cleanup fault making the release fail is FAILED like the goal's
         * own exceptions here; the release is unconditional. */
        const PettaLibplReleaseStatus release_status =
            petta_libpl_release_query(arena, NULL, query,
                                      PETTA_LIBPL_RELEASE_CLOSE);
        ok = ok && release_status == PETTA_LIBPL_RELEASE_OK;
        petta_libpl_advance_revision(runtime);
    }
    if (ok) {
        *answers = atom_expr(
            arena, items, (CettaExprLen)length);
        ok = *answers != NULL;
    }
    free(items);
    petta_libpl_var_map_free(&variables);
    PL_discard_foreign_frame(frame);
    g_petta_libpl_active_runtime =
        previous_active_runtime;
    petta_libpl_leave(claimed);
    return ok
        ? CETTA_LIB_PROLOG_QUERY_OK
        : CETTA_LIB_PROLOG_QUERY_FAILED;
}

/* A reference-stdlib name's answer: the arities the reference's
 * registration records for it (petta_semantics_registered_builtin_arities),
 * not the predicates this module happens to hold, so an application at
 * another arity is partial or over-applied as there, never an engine call.
 * False for any other entry. */
static bool petta_libpl_reference_stdlib_arity(
    const PettaLibplImport *entry, SymbolId head,
    CettaExprLen supplied, PeTTaNamedArity *result) {
    uint16_t arities = 0u;
    if (!entry || !entry->reference_stdlib || entry->auto_resolved ||
        !petta_semantics_registered_builtin_arities(head, &arities))
        return false;
    *result = petta_semantics_registered_named_arity(arities, supplied);
    return true;
}

/* A reference-stdlib name before the engine has started, in this process or
 * its host, answered without starting it.  False for any other name or once
 * the engine has started, when the locked path below answers. */
static bool petta_libpl_unstarted_named_arity(
    CettaLibPrologRuntime *runtime, SymbolId head,
    CettaExprLen supplied, PeTTaNamedArity *result) {
    if (atomic_load_explicit(&g_petta_libpl_started,
                             memory_order_acquire) ||
        PL_is_initialised(NULL, NULL) ||
        pthread_mutex_lock(&g_petta_libpl_lock) != 0)
        return false;
    bool answered = petta_libpl_reference_stdlib_arity(
        petta_libpl_find_import(runtime, head), head, supplied, result);
    (void)pthread_mutex_unlock(&g_petta_libpl_lock);
    return answered;
}

static PeTTaNamedArity petta_libpl_named_arity_impl(
    CettaLibPrologRuntime *runtime, SymbolId head,
    CettaExprLen supplied) {
    PeTTaNamedArity result = {0};
    if (!runtime ||
        supplied > (CettaExprLen)SIZE_MAX) {
        return result;
    }
    /*
     * Most evaluator heads are native MeTTa names.  The lock-free admission
     * set proves that an absent SymbolId cannot name an optional Prolog
     * import.  A saturated index conservatively falls through to the locked
     * registry.  Built-in adapter names remain visible without initializing
     * or claiming a Prolog engine merely to classify them.
     */
    if (!petta_libpl_import_admission_maybe_contains(
            runtime, head)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PETTA_LIBPL_ADMISSION_NEGATIVE);
        size_t standard_arities[2] = {0u, 0u};
        size_t standard_arity_count =
            petta_libpl_standard_function_arities(
                head, standard_arities);
        for (size_t index = 0u;
             index < standard_arity_count; index++) {
            result.known = true;
            result.exact =
                result.exact ||
                standard_arities[index] ==
                    (size_t)supplied;
            result.larger =
                result.larger ||
                standard_arities[index] >
                    (size_t)supplied;
            result.smaller =
                result.smaller ||
                standard_arities[index] <
                    (size_t)supplied;
        }
        return result;
    }
    if (petta_libpl_unstarted_named_arity(runtime, head, supplied, &result))
        return result;
    bool claimed = false;
    if (!petta_libpl_enter(&claimed))
        return result;
    if (!petta_libpl_prepare_locked(runtime)) {
        petta_libpl_leave(claimed);
        return result;
    }
    PettaLibplImport *entry =
        petta_libpl_find_import(runtime, head);
    if (petta_libpl_debug_enabled()) {
        fprintf(stderr,
                "[petta-libpl] lookup %s entry=%d auto=%d\n",
                g_symbols ? symbol_bytes(g_symbols, head) : "?",
                entry != NULL,
                entry ? (int)entry->auto_resolved : -1);
    }
    if (petta_libpl_reference_stdlib_arity(entry, head, supplied, &result)) {
        petta_libpl_leave(claimed);
        return result;
    }
    if (entry && entry->auto_resolved)
        entry = NULL;
    size_t standard_arities[2] = {0u, 0u};
    size_t standard_arity_count =
        !entry
            ? petta_libpl_standard_function_arities(
                  head, standard_arities)
            : 0u;
    if (standard_arity_count > 0u) {
        result.known = true;
        for (size_t index = 0u;
             index < standard_arity_count; index++) {
            result.exact =
                result.exact ||
                standard_arities[index] ==
                    (size_t)supplied;
            result.larger =
                result.larger ||
                standard_arities[index] >
                    (size_t)supplied;
            result.smaller =
                result.smaller ||
                standard_arities[index] <
                    (size_t)supplied;
        }
    }
    if (entry &&
        petta_libpl_refresh_arities(runtime, entry)) {
        /*
         * SWI's current_predicate/1 intentionally does not enumerate an
         * unloaded autoload candidate.  An explicit PeTTa import nevertheless
         * makes the name callable: the first concrete application supplies
         * the arity, call/1 performs ordinary SWI autoloading, and a successful
         * boundary call records that arity below.
         */
        if (entry->arity_len == 0u) {
            result.known = true;
            result.exact = true;
        }
        for (size_t index = 0u;
             index < entry->arity_len; index++) {
            size_t arity =
                entry->function_arities[index];
            result.known = true;
            result.exact =
                result.exact ||
                arity == (size_t)supplied;
            result.larger =
                result.larger ||
                arity > (size_t)supplied;
            result.smaller =
                result.smaller ||
                arity < (size_t)supplied;
        }
    }
    petta_libpl_leave(claimed);
    return result;
}

PeTTaNamedArity petta_libpl_named_arity(
    CettaLibPrologRuntime *runtime, SymbolId head,
    CettaExprLen supplied) {
    PeTTaNamedArity result =
        petta_libpl_named_arity_impl(runtime, head, supplied);
    petta_libpl_debug_named_arity(
        "named-arity", head, supplied, result);
    return result;
}

PeTTaNamedArity petta_libpl_named_arity_including_resolved(
    CettaLibPrologRuntime *runtime, SymbolId head,
    CettaExprLen supplied) {
    PeTTaNamedArity result =
        petta_libpl_named_arity(runtime, head, supplied);
    if (result.known || !runtime ||
        head == SYMBOL_ID_NONE ||
        supplied > (CettaExprLen)SIZE_MAX) {
        return result;
    }
    /* Auto-resolved names are published into the same admission index as
       explicit imports.  Its negative answer therefore proves there is no
       resolved entry to inspect and keeps native calls out of the engine. */
    if (!petta_libpl_import_admission_maybe_contains(
            runtime, head)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PETTA_LIBPL_ADMISSION_NEGATIVE);
        return result;
    }
    bool claimed = false;
    if (!petta_libpl_enter(&claimed))
        return result;
    if (!petta_libpl_prepare_locked(runtime)) {
        petta_libpl_leave(claimed);
        return result;
    }
    PettaLibplImport *entry =
        petta_libpl_find_import(runtime, head);
    if (entry && entry->auto_resolved &&
        petta_libpl_refresh_arities(runtime, entry)) {
        for (size_t index = 0u;
             index < entry->arity_len; index++) {
            size_t arity = entry->function_arities[index];
            result.known = true;
            result.exact =
                result.exact ||
                arity == (size_t)supplied;
            result.larger =
                result.larger ||
                arity > (size_t)supplied;
            result.smaller =
                result.smaller ||
                arity < (size_t)supplied;
        }
    }
    petta_libpl_leave(claimed);
    return result;
}

PeTTaNamedArity petta_libpl_named_arity_resolving(
    CettaLibPrologRuntime *runtime, SymbolId head,
    CettaExprLen supplied) {
    PeTTaNamedArity result =
        petta_libpl_named_arity(runtime, head, supplied);
    if (result.known || !runtime ||
        head == SYMBOL_ID_NONE ||
        supplied > (CettaExprLen)SIZE_MAX) {
        return result;
    }
    bool claimed = false;
    if (!petta_libpl_enter(&claimed))
        return result;
    if (!petta_libpl_prepare_locked(runtime)) {
        petta_libpl_leave(claimed);
        return result;
    }
    PettaLibplImport *probed =
        petta_libpl_probe_system_function(runtime, head);
    if (probed) {
        for (size_t index = 0u;
             index < probed->arity_len; index++) {
            size_t arity = probed->function_arities[index];
            result.known = true;
            result.exact =
                result.exact ||
                arity == (size_t)supplied;
            result.larger =
                result.larger ||
                arity > (size_t)supplied;
            result.smaller =
                result.smaller ||
                arity < (size_t)supplied;
        }
    }
    petta_libpl_leave(claimed);
    return result;
}

static bool petta_libpl_numeric_call(
    Arena *arena, const char *functor, Atom **args, uint32_t nargs,
    bool comparison, CettaCallOutcome *out) {
    if (out)
        *out = cetta_call_failure();
    CettaLibraryContext *context = eval_current_library_context();
    CettaLibPrologRuntime *runtime = context ? context->lib_prolog : NULL;
    if (!runtime || !arena || !out || !args || nargs == 0u ||
        (!functor && nargs != 1u))
        return false;
    bool claimed = false;
    if (!petta_libpl_enter(&claimed))
        return false;
    if (!petta_libpl_prepare_locked(runtime)) {
        petta_libpl_leave(claimed);
        return false;
    }
    fid_t frame = PL_open_foreign_frame();
    if (!frame) {
        petta_libpl_leave(claimed);
        return false;
    }
    CettaLibPrologRuntime *previous_runtime = g_petta_libpl_active_runtime;
    Arena *previous_arena = g_petta_libpl_active_arena;
    g_petta_libpl_active_runtime = runtime;
    g_petta_libpl_active_arena = arena;
    PettaLibplVarMap variables = {0};
    term_t call = PL_new_term_refs(2);
    bool built = call != 0;
    if (built && comparison) {
        for (uint32_t i = 0u; built && i < nargs; i++)
            built = petta_libpl_to_term(
                args[i], call + i, &variables, 0u);
    } else if (built && functor) {
        term_t operands = PL_new_term_refs((int)nargs);
        built = operands != 0;
        for (uint32_t i = 0u; built && i < nargs; i++)
            built = petta_libpl_to_term(
                args[i], operands + i, &variables, 0u);
        if (built) {
            atom_t name = PL_new_atom(functor);
            functor_t function = PL_new_functor(name, nargs);
            PL_unregister_atom(name);
            built = PL_cons_functor_v(call + 1, function, operands);
        }
    } else if (built) {
        built = petta_libpl_to_term(args[0], call + 1, &variables, 0u);
    }
    bool ok = false;
    if (built) {
        qid_t query = PL_open_query(
            runtime->module,
            PL_Q_NODEBUG | PL_Q_CATCH_EXCEPTION | PL_Q_EXT_STATUS,
            PL_predicate(comparison ? functor : "is", 2, "system"), call);
        if (query) {
            int status = PL_next_solution(query);
            if (status == PL_S_TRUE || status == PL_S_LAST) {
                if (comparison) {
                    *out = cetta_call_value(atom_symbol(arena, "true"));
                } else {
                    PettaLibplBackVarMap unknown = {0};
                    *out = cetta_call_value(petta_libpl_from_term(
                        arena, call, &variables, &unknown, 0u));
                    petta_libpl_back_map_free(&unknown);
                }
            } else if (comparison && status == PL_S_FALSE) {
                *out = cetta_call_value(atom_symbol(arena, "false"));
            } else if (status == PL_S_EXCEPTION) {
                term_t exception = PL_exception(query);
                if (exception)
                    *out = cetta_call_raised(
                        petta_libpl_exception_value(arena, exception));
            }
            ok = out->term != NULL;
            /* System is/2 and comparisons: a cleanup fault releases into
             * the same raised channel as the call's own error values. */
            {
                Atom *cleanup = NULL;
                const PettaLibplReleaseStatus release_status =
                    petta_libpl_release_query(
                        arena, &cleanup, query,
                        PETTA_LIBPL_RELEASE_CLOSE);
                if (release_status != PETTA_LIBPL_RELEASE_OK) {
                    /* The exact SWI value crosses unmodified; a refused
                     * release (NOT_INNER) is a failure with no invented
                     * payload. */
                    if (cleanup)
                        *out = cetta_call_raised(cleanup);
                    ok = false;
                }
            }
        }
    }
    petta_libpl_var_map_free(&variables);
    PL_discard_foreign_frame(frame);
    g_petta_libpl_active_arena = previous_arena;
    g_petta_libpl_active_runtime = previous_runtime;
    petta_libpl_leave(claimed);
    return ok;
}

bool petta_libpl_evaluate_arithmetic(
    Arena *arena, const char *functor, Atom **args, uint32_t nargs,
    CettaCallOutcome *out) {
    return petta_libpl_numeric_call(arena, functor, args, nargs, false, out);
}

bool petta_libpl_compare_arithmetic(
    Arena *arena, const char *relation, Atom **args, CettaCallOutcome *out) {
    if (!relation || (strcmp(relation, "<") != 0 &&
                      strcmp(relation, ">") != 0 &&
                      strcmp(relation, "=<") != 0 &&
                      strcmp(relation, ">=") != 0))
        return false;
    return petta_libpl_numeric_call(arena, relation, args, 2u, true, out);
}

bool petta_libpl_predicate_defined(
    CettaLibPrologRuntime *runtime, SymbolId name, size_t arity) {
    if (!runtime || !runtime->prepared || name == SYMBOL_ID_NONE ||
        !g_symbols)
        return false;
    bool claimed = false;
    if (!petta_libpl_enter(&claimed))
        return false;
    bool exists = false;
    fid_t frame = PL_open_foreign_frame();
    if (frame) {
        if (!petta_libpl_predicate_exists(
                runtime, symbol_bytes(g_symbols, name),
                symbol_len(g_symbols, name), arity, &exists))
            exists = false;
        PL_discard_foreign_frame(frame);
    }
    petta_libpl_leave(claimed);
    return exists;
}

/*
 * SWI-PeTTa's static-import! (lib_import.pl): a file's facts are read from its
 * .qlf, else from its .pl compiled to a .qlf, else from its .metta converted
 * to both, as the reference converts it (one row per line, its outer
 * parentheses dropped, parentheses to brackets and spaces to commas outside
 * strings), then compiled and consulted, as the reference does both in one
 * module, into the one module that holds every file the session imports.  So
 * SWI's own consult rules decide the facts: a file loaded again is reloaded,
 * a multifile predicate (the conversion declares Space/3 so) gathers the facts
 * of every file, and any other predicate a file defines is redefined, its
 * earlier facts gone.  A load reports the predicates it changed, by their
 * modification generations, with whether each is multifile; a predicate's
 * facts are then read as rows [Rel, Arg...].  Predicates a file imports from
 * a library are not its own and are never reported.
 */
static const char *const petta_libpl_static_import_clauses[] = {
    "('$cetta_static_rows'(Head, Visitor) :- "
    "( predicate_property(Head, static), "
    "predicate_property(Head, number_of_rules(0)), "
    "\\+ predicate_property(Head, tabled) "
    "-> forall(Head, Visitor) "
    "; forall(clause(Head, true), Visitor) ))",
    "('$cetta_static_import'(M, Space, File, Touched) :- "
    "style_check(-discontiguous), atom_string(File, SFile), "
    "working_dir(Base), "
    "atomic_list_concat([Base, '/', SFile, '.qlf'], Qlf), "
    "atomic_list_concat([Base, '/', SFile, '.pl'], Pl), "
    "atomic_list_concat([Base, '/', SFile, '.metta'], Metta), "
    "'$cetta_static_generations'(M, Before), "
    "( exists_file(Qlf) -> M:consult(Qlf) "
    "; exists_file(Pl) -> M:qcompile(Pl), M:consult(Qlf) "
    "; '$cetta_metta_file_to_prolog'(Metta, Space, Pl), "
    "M:qcompile(Pl), M:consult(Qlf) ), "
    "'$cetta_static_generations'(M, After), "
    "findall(P-N-Multifile, "
    "( member(P/N-G, After), \\+ memberchk(P/N-G, Before), "
    "functor(H, P, N), "
    "( predicate_property(M:H, multifile) -> Multifile = true "
    "; Multifile = false ) ), Changed), "
    "findall(P-N-false, "
    "( member(P/N-_, Before), \\+ memberchk(P/N-_, After) ), Gone), "
    "append(Changed, Gone, Touched))",
    "('$cetta_static_generations'(M, Generations) :- "
    "findall(P/N-G, "
    "( current_predicate(M:P/N), functor(H, P, N), "
    "\\+ predicate_property(M:H, imported_from(_)), "
    "predicate_property(M:H, last_modified_generation(G)) ), "
    "Generations))",
    "('$cetta_metta_file_to_prolog'(Input, Space, Output) :- "
    "setup_call_cleanup(open(Input, read, In), "
    "setup_call_cleanup(open(Output, write, Out), "
    "( format(Out, \":- multifile '~w'/3.~n\", [Space]), "
    "format(Out, \":- discontiguous '~w'/3.~n~n\", [Space]), "
    "'$cetta_metta_convert_stream'(In, Out, Space) ), "
    "close(Out)), close(In)))",
    "('$cetta_metta_convert_stream'(In, Out, Space) :- "
    "read_line_to_string(In, Line), "
    "( Line == end_of_file -> true "
    "; '$cetta_metta_convert_line'(Line, Space, Out), "
    "'$cetta_metta_convert_stream'(In, Out, Space) ))",
    "('$cetta_metta_convert_line'(Line, Space, Out) :- "
    "sub_string(Line, 1, _, 1, Inner), string_chars(Inner, Chars), "
    "'$cetta_metta_chars'(Chars, false, Converted), "
    "string_chars(Text, Converted), "
    "format(Out, \"'~w'(~w).~n\", [Space, Text]))",
    "('$cetta_metta_chars'([], _, []) :- !)",
    "('$cetta_metta_chars'(['\"'|T], false, ['\"'|R]) :- !, "
    "'$cetta_metta_chars'(T, true, R))",
    "('$cetta_metta_chars'(['\"'|T], true, ['\"'|R]) :- !, "
    "'$cetta_metta_chars'(T, false, R))",
    "('$cetta_metta_chars'(['('|T], false, ['['|R]) :- !, "
    "'$cetta_metta_chars'(T, false, R))",
    "('$cetta_metta_chars'([')'|T], false, [']'|R]) :- !, "
    "'$cetta_metta_chars'(T, false, R))",
    "('$cetta_metta_chars'([' '|T], false, [','|R]) :- !, "
    "'$cetta_metta_chars'(T, false, R))",
    "('$cetta_metta_chars'([H|T], Q, [H|R]) :- "
    "'$cetta_metta_chars'(T, Q, R))",
};

static bool petta_libpl_install_static_import(
    CettaLibPrologRuntime *runtime) {
    if (runtime->static_import_ready)
        return true;
    if (!runtime->static_module_name) {
        static const char suffix[] = "_static";
        size_t length = runtime->module_name_len + sizeof(suffix) - 1u;
        char *name = malloc(length + 1u);
        if (!name)
            return false;
        memcpy(name, runtime->module_name, runtime->module_name_len);
        memcpy(name + runtime->module_name_len, suffix, sizeof(suffix));
        runtime->static_module_name = name;
        runtime->static_module_name_len = length;
    }
    fid_t frame = PL_open_foreign_frame();
    if (!frame)
        return false;
    bool ok = true;
    size_t count = sizeof(petta_libpl_static_import_clauses) /
                   sizeof(petta_libpl_static_import_clauses[0]);
    for (size_t index = 0u; ok && index < count; index++) {
        term_t clause = PL_new_term_ref();
        ok = clause &&
             PL_chars_to_term(petta_libpl_static_import_clauses[index],
                              clause) &&
             PL_assert(clause, runtime->module, PL_ASSERTZ);
    }
    PL_discard_foreign_frame(frame);
    runtime->static_import_ready = ok;
    return ok;
}

void petta_libpl_warn_redefined(
    CettaLibPrologRuntime *runtime, SymbolId name, size_t arity) {
    if (!runtime || !runtime->prepared || name == SYMBOL_ID_NONE ||
        !g_symbols || arity > INT_MAX)
        return;
    bool claimed = false;
    if (!petta_libpl_enter(&claimed))
        return;
    fid_t frame = PL_open_foreign_frame();
    /* print_message(warning, redefined_procedure(static, Name/Arity)) */
    term_t arguments = frame ? PL_new_term_refs(2) : 0;
    term_t indicator = frame ? PL_new_term_refs(2) : 0;
    term_t message = frame ? PL_new_term_refs(2) : 0;
    atom_t slash = PL_new_atom("/");
    atom_t redefined = PL_new_atom("redefined_procedure");
    functor_t indicator_functor = PL_new_functor(slash, 2u);
    functor_t message_functor = PL_new_functor(redefined, 2u);
    PL_unregister_atom(slash);
    PL_unregister_atom(redefined);
    predicate_t print_message = PL_predicate("print_message", 2, "system");
    if (arguments && indicator && message && print_message &&
        petta_libpl_put_utf8(indicator, PL_ATOM,
                             symbol_bytes(g_symbols, name),
                             symbol_len(g_symbols, name)) &&
        PL_put_integer(indicator + 1, (int)arity) &&
        petta_libpl_put_utf8(message, PL_ATOM, "static", 6u) &&
        PL_cons_functor_v(message + 1, indicator_functor, indicator) &&
        petta_libpl_put_utf8(arguments, PL_ATOM, "warning", 7u) &&
        PL_cons_functor_v(arguments + 1, message_functor, message)) {
        (void)PL_call_predicate(runtime->module,
                                PL_Q_NODEBUG | PL_Q_CATCH_EXCEPTION,
                                print_message, arguments);
    }
    if (frame)
        PL_discard_foreign_frame(frame);
    petta_libpl_leave(claimed);
}

/* The symbols of the atoms one import has read, by handle: a data file
 * names few atoms many times.  It lives for one import, while the facts that
 * hold the atoms stand. */
typedef struct {
    atom_t *handles;
    SymbolId *symbols;
    AtomId *literal_ids;
    uint32_t cap;
    uint32_t len;
} PettaLibplAtomSymbols;

static uint32_t petta_libpl_atom_symbols_slot(const PettaLibplAtomSymbols *cache,
                                              atom_t handle) {
    uint32_t mask = cache->cap - 1u;
    uint32_t slot = (uint32_t)(((uint64_t)handle *
                                UINT64_C(0x9E3779B97F4A7C15)) >> 32) & mask;
    while (cache->handles[slot] && cache->handles[slot] != handle)
        slot = (slot + 1u) & mask;
    return slot;
}

static SymbolId petta_libpl_atom_symbols_find(PettaLibplAtomSymbols *cache,
                                              atom_t handle, term_t term) {
    if (!handle)
        return SYMBOL_ID_NONE;
    if ((cache->len + 1u) * 2u > cache->cap) {
        PettaLibplAtomSymbols grown = {
            .cap = cache->cap ? cache->cap * 2u : 1024u,
            .len = cache->len,
        };
        grown.handles = cetta_malloc(sizeof(*grown.handles) * grown.cap);
        grown.symbols = cetta_malloc(sizeof(*grown.symbols) * grown.cap);
        grown.literal_ids = cetta_malloc(sizeof(*grown.literal_ids) * grown.cap);
        memset(grown.handles, 0, sizeof(*grown.handles) * grown.cap);
        for (uint32_t i = 0u; i < grown.cap; i++)
            grown.literal_ids[i] = CETTA_ATOM_ID_NONE;
        for (uint32_t index = 0u; index < cache->cap; index++) {
            if (!cache->handles[index])
                continue;
            uint32_t slot = petta_libpl_atom_symbols_slot(
                &grown, cache->handles[index]);
            grown.handles[slot] = cache->handles[index];
            grown.symbols[slot] = cache->symbols[index];
            grown.literal_ids[slot] = cache->literal_ids[index];
        }
        free(cache->handles);
        free(cache->symbols);
        free(cache->literal_ids);
        *cache = grown;
    }
    uint32_t slot = petta_libpl_atom_symbols_slot(cache, handle);
    if (cache->handles[slot])
        return cache->symbols[slot];
    PettaLibplVarMap plain = {0};
    SymbolId symbol = petta_libpl_atom_term_symbol(term, &plain);
    if (symbol == SYMBOL_ID_NONE)
        return symbol;
    cache->handles[slot] = handle;
    cache->symbols[slot] = symbol;
    cache->len++;
    return symbol;
}

/* One fact P(Arg...) as the row (Arg...): an atom or an integer that fits is
 * read as it stands, and any other argument as every term crossing is. */
static Atom *petta_libpl_static_row(Arena *arena, term_t arguments, int arity,
                                    PettaLibplAtomSymbols *cache) {
    Atom *inline_items[8];
    Atom **items = arity <= 8
        ? inline_items : cetta_malloc(sizeof(*items) * (size_t)arity);
    PettaLibplVarMap variables = {0};
    PettaLibplBackVarMap unknown = {0};
    bool ok = true;
    for (int index = 0; ok && index < arity; index++) {
        Atom *item = NULL;
        atom_t handle = 0;
        int64_t value = 0;
        term_t argument = arguments + index;
        switch (PL_term_type(argument)) {
        case PL_ATOM: {
            SymbolId symbol = PL_get_atom(argument, &handle)
                ? petta_libpl_atom_symbols_find(cache, handle, argument)
                : SYMBOL_ID_NONE;
            if (symbol != SYMBOL_ID_NONE)
                item = petta_libpl_callable_find(g_petta_libpl_active_runtime, symbol);
            item = item ? atom_deep_copy(arena, item)
                        : symbol != SYMBOL_ID_NONE ? atom_symbol_id(arena, symbol) : NULL;
            break;
        }
        case PL_INTEGER:
            if (PL_get_int64(argument, &value))
                item = atom_int(arena, value);
            break;
        default:
            break;
        }
        if (!item)
            item = petta_libpl_from_term_mode(arena, argument, &variables,
                                              &unknown, false, 2u);
        items[index] = item;
        ok = item != NULL;
    }
    Atom *row = ok ? atom_expr(arena, items, (CettaExprLen)arity) : NULL;
    petta_libpl_var_map_free(&variables);
    petta_libpl_back_map_free(&unknown);
    if (items != inline_items)
        free(items);
    return row;
}

/* Copy scalar coordinates out of a clause solution. Integers are admitted
 * together when the bounded batch flushes; symbol ids already have the sink's
 * immutable identity. No SWI reference survives the solution's frame. */
static bool petta_libpl_static_literal(
    TermUniverse *universe, term_t arguments, int arity,
    PettaLibplAtomSymbols *cache, AtomId *items, int64_t *integers) {
    if (!universe || arity < 0)
        return false;
    __attribute__((cleanup(cetta_shared_transition_guard_leave)))
    CettaSharedTransitionGuard transition = {0};
    cetta_shared_transition_guard_enter(&transition);
    for (int index = 0; index < arity; index++) {
        AtomId item = CETTA_ATOM_ID_NONE;
        atom_t handle = 0;
        term_t argument = arguments + index;
        switch (PL_term_type(argument)) {
        case PL_ATOM: {
            SymbolId symbol = PL_get_atom(argument, &handle)
                ? petta_libpl_atom_symbols_find(cache, handle, argument)
                : SYMBOL_ID_NONE;
            if (symbol == SYMBOL_ID_NONE ||
                petta_libpl_callable_find(g_petta_libpl_active_runtime, symbol) ||
                (index == 0 && (symbol == g_builtin_syms.equals ||
                                symbol == g_builtin_syms.colon ||
                                petta_semantics_form(symbol) == PETTA_FORM_CONS)))
                return false;
            uint32_t slot = petta_libpl_atom_symbols_slot(cache, handle);
            item = cache->literal_ids[slot];
            if (item == CETTA_ATOM_ID_NONE) {
                item = tu_intern_symbol(universe, symbol);
                cache->literal_ids[slot] = item;
            }
            if (item == CETTA_ATOM_ID_NONE)
                return false;
            break;
        }
        case PL_INTEGER:
            if (!PL_get_int64(argument, integers + index))
                return false;
            break;
        default:
            return false;
        }
        items[index] = item;
    }
    return true;
}

typedef struct {
    TermUniverse *universe;
    uint32_t arity;
    AtomId *coordinates;
    int64_t *integers, *values;
    AtomId *value_ids;
    bool present[256];
} PettaLibplStaticEncoding;

static bool petta_libpl_static_encode(
    PettaLibplStaticEncoding *encoding, AtomId *ids, uint32_t count) {
    for (uint32_t row = 0u; row < count; row++)
        ids[row] = CETTA_ATOM_ID_NONE;
    if (!encoding->universe)
        return true;
    __attribute__((cleanup(cetta_shared_transition_guard_leave)))
    CettaSharedTransitionGuard transition = {0};
    cetta_shared_transition_guard_enter(&transition);
    size_t integers = 0u;
    for (uint32_t row = 0u; row < count; row++) {
        if (!encoding->present[row])
            continue;
        for (uint32_t column = 0u; column < encoding->arity; column++) {
            size_t index = (size_t)row * encoding->arity + column;
            if (encoding->coordinates[index] == CETTA_ATOM_ID_NONE)
                encoding->values[integers++] = encoding->integers[index];
        }
    }
    if (!tu_intern_ints(encoding->universe, encoding->values, integers,
                        encoding->value_ids))
        return false;
    size_t next = 0u;
    for (uint32_t row = 0u; row < count; row++) {
        if (!encoding->present[row])
            continue;
        AtomId *coordinates = encoding->arity
            ? encoding->coordinates + (size_t)row * encoding->arity : NULL;
        for (uint32_t column = 0u; column < encoding->arity; column++)
            if (coordinates[column] == CETTA_ATOM_ID_NONE)
                coordinates[column] = encoding->value_ids[next++];
    }
    return tu_exprs_from_ids(encoding->universe, encoding->coordinates, count,
                             encoding->arity, encoding->present, ids);
}

/* A query-owned sink. The foreign arguments are borrowed only for the
 * callback; a bounded batch contains native coordinates or owning syntax,
 * never SWI references. Nested invocations restore the preceding sink. */
enum { PETTA_STATIC_BATCH_ROWS = 256, PETTA_STATIC_BATCH_BYTES = 64 * 1024 };
typedef struct {
    PettaLibplStaticPredicateVisit visit;
    void *context;
    Atom *target;
    int arity;
    bool multifile, ok;
    TermUniverse *universe;
    PettaLibplAtomSymbols cache;
    PettaLibplStaticEncoding encoding;
    Arena scratch;
    ArenaMark origin;
    Atom *rows[PETTA_STATIC_BATCH_ROWS];
    AtomId literal_ids[PETTA_STATIC_BATCH_ROWS];
    uint32_t count, limit;
} PettaLibplStaticSink;
static _Thread_local PettaLibplStaticSink *g_petta_static_sink;

static bool petta_libpl_static_sink_flush(PettaLibplStaticSink *sink) {
    if (!sink->count) return sink->ok;
    PettaLibplStaticRows batch = {
        .arena = &sink->scratch, .rows = sink->rows,
        .literal_ids = sink->literal_ids, .count = sink->count,
    };
    sink->ok = petta_libpl_static_encode(&sink->encoding, sink->literal_ids, sink->count) &&
        sink->visit(sink->context, sink->target, (CettaExprLen)sink->arity,
            sink->multifile, false, &sink->universe, &batch);
    sink->count = 0u;
    arena_reset(&sink->scratch, sink->origin);
    return sink->ok;
}

static foreign_t petta_libpl_static_sink_row(term_t arguments, int arity,
                                            control_t control) {
    (void)control;
    PettaLibplStaticSink *sink = g_petta_static_sink;
    if (!sink || !sink->ok || arity != sink->arity) return FALSE;
    uint32_t row = sink->count;
    sink->encoding.present[row] = sink->encoding.universe &&
        petta_libpl_static_literal(sink->encoding.universe, arguments, arity,
            &sink->cache,
            arity ? sink->encoding.coordinates + (size_t)row * arity : NULL,
            arity ? sink->encoding.integers + (size_t)row * arity : NULL);
    sink->rows[row] = NULL;
    if (!sink->encoding.present[row]) {
        fid_t frame = PL_open_foreign_frame();
        sink->rows[row] = frame ? petta_libpl_static_row(
            &sink->scratch, arguments, arity, &sink->cache) : NULL;
        if (frame) PL_discard_foreign_frame(frame);
    }
    sink->ok = sink->rows[row] || sink->encoding.present[row];
    sink->count++;
    if (sink->ok && (sink->count == sink->limit ||
        arena_accounted_live_bytes(&sink->scratch) >= PETTA_STATIC_BATCH_BYTES))
        sink->ok = petta_libpl_static_sink_flush(sink);
    return sink->ok ? TRUE : FALSE;
}

/* A certified static, untabled fact predicate executes directly; all other
 * predicates enumerate stored facts through clause/2. SWI owns variable
 * freshness and clause order, including duplicate occurrences. */
static bool petta_libpl_static_import_predicate(
    CettaLibPrologRuntime *runtime, Arena *arena, term_t module,
    term_t predicate, int arity, bool multifile,
    PettaLibplStaticPredicateVisit visit, void *context,
    CettaCallOutcome *end) {
    PettaLibplVarMap variables = {0};
    PettaLibplBackVarMap unknown = {0};
    Atom *target = petta_libpl_from_term(arena, predicate, &variables, &unknown, 0u);
    petta_libpl_var_map_free(&variables);
    petta_libpl_back_map_free(&unknown);
    if (!target || arity < 0) return false;
    PettaLibplStaticSink sink = {
        .visit = visit, .context = context, .target = target, .arity = arity,
        .multifile = multifile, .ok = true, .limit = PETTA_STATIC_BATCH_ROWS,
    };
    if (!visit(context, target, (CettaExprLen)arity, multifile, true, &sink.universe, NULL))
        return false;
    if (sink.universe && (size_t)arity <= PETTA_STATIC_BATCH_BYTES / (4u*sizeof(AtomId))) {
        sink.encoding.universe = sink.universe;
        sink.encoding.arity = (uint32_t)arity;
        if (arity) {
            size_t limit = PETTA_STATIC_BATCH_BYTES / (4u*sizeof(AtomId)*(size_t)arity);
            sink.limit = limit < PETTA_STATIC_BATCH_ROWS ? (uint32_t)limit : PETTA_STATIC_BATCH_ROWS;
            size_t cells = (size_t)sink.limit * (size_t)arity;
            sink.encoding.coordinates = cetta_malloc(cells*sizeof(*sink.encoding.coordinates));
            sink.encoding.integers = cetta_malloc(cells*sizeof(*sink.encoding.integers));
            sink.encoding.values = cetta_malloc(cells*sizeof(*sink.encoding.values));
            sink.encoding.value_ids = cetta_malloc(cells*sizeof(*sink.encoding.value_ids));
        }
    }
    arena_init_detached(&sink.scratch);
    sink.origin = arena_mark(&sink.scratch);
    atom_t name = 0;
    term_t forall_args = PL_new_term_refs(2);
    term_t head = PL_new_term_ref();
    term_t visitor_args = arity ? PL_new_term_refs(arity) : 0;
    atom_t colon = PL_new_atom(":");
    atom_t visitor = PL_new_atom("$cetta_static_row");
    functor_t qualified = PL_new_functor(colon,2);
    functor_t visitor_functor = PL_new_functor(visitor,(size_t)arity);
    PL_unregister_atom(colon); PL_unregister_atom(visitor);
    bool ok = forall_args && head && (!arity || visitor_args) &&
        PL_get_atom(predicate,&name) &&
        PL_register_foreign_in_module(runtime->module_name,"$cetta_static_row",arity,
            (pl_function_t)petta_libpl_static_sink_row,PL_FA_VARARGS) &&
        PL_put_functor(head,PL_new_functor(name,(size_t)arity));
    for (int i=0;ok && i<arity;i++) ok=PL_get_arg(i+1,head,visitor_args+i);
    ok = ok && PL_cons_functor(forall_args,qualified,module,head) &&
        (arity ? PL_cons_functor_v(forall_args+1,visitor_functor,visitor_args)
               : PL_put_functor(forall_args+1,visitor_functor));
    predicate_t forall_predicate = PL_predicate("$cetta_static_rows",2,runtime->module_name);
    qid_t query = ok ? PL_open_query(runtime->module,
        PL_Q_NODEBUG | PL_Q_CATCH_EXCEPTION | PL_Q_EXT_STATUS,forall_predicate,forall_args) : 0;
    PettaLibplStaticSink *previous = g_petta_static_sink;
    g_petta_static_sink = &sink;
    if (query) {
        int status = PL_next_solution(query);
        ok = (status==PL_S_TRUE || status==PL_S_LAST) && sink.ok;
        if (status==PL_S_EXCEPTION) {
            term_t exception = PL_exception(query);
            Atom *error = exception ? petta_libpl_exception_value(arena,exception) : NULL;
            if (error) *end = cetta_call_raised(error);
        }
        PettaLibplReleaseStatus released = petta_libpl_release_query(arena,NULL,query,PETTA_LIBPL_RELEASE_CLOSE);
        ok = ok && released==PETTA_LIBPL_RELEASE_OK;
    } else ok=false;
    g_petta_static_sink = previous;
    if (ok) ok = petta_libpl_static_sink_flush(&sink);
    free(sink.cache.handles); free(sink.cache.symbols); free(sink.cache.literal_ids);
    free(sink.encoding.coordinates); free(sink.encoding.integers);
    free(sink.encoding.values); free(sink.encoding.value_ids);
    arena_free(&sink.scratch);
    return ok;
}

bool petta_libpl_static_import(
    CettaLibPrologRuntime *runtime, Arena *arena, SymbolId space,
    const char *file, PettaLibplStaticPredicateVisit visit, void *context,
    CettaCallOutcome *end) {
    if (end)
        *end = cetta_call_failure();
    if (!runtime || !arena || space == SYMBOL_ID_NONE || !g_symbols ||
        !file || !visit || !end)
        return false;
    bool claimed = false;
    if (!petta_libpl_enter(&claimed))
        return false;
    if (!petta_libpl_prepare_locked(runtime) ||
        !petta_libpl_install_static_import(runtime)) {
        petta_libpl_leave(claimed);
        return false;
    }
    CettaLibPrologRuntime *previous_active_runtime =
        g_petta_libpl_active_runtime;
    g_petta_libpl_active_runtime = runtime;
    fid_t frame = PL_open_foreign_frame();
    term_t arguments = frame ? PL_new_term_refs(4) : 0;
    predicate_t import = PL_predicate(
        "$cetta_static_import", 4, runtime->module_name);
    bool ok = frame && arguments && import &&
              petta_libpl_put_utf8(arguments, PL_ATOM,
                                   runtime->static_module_name,
                                   runtime->static_module_name_len) &&
              petta_libpl_put_utf8(arguments + 1, PL_ATOM,
                                   symbol_bytes(g_symbols, space),
                                   symbol_len(g_symbols, space)) &&
              petta_libpl_put_utf8(arguments + 2, PL_ATOM, file,
                                   strlen(file));
    qid_t query = ok
        ? PL_open_query(runtime->module,
                        PL_Q_NODEBUG | PL_Q_CATCH_EXCEPTION |
                            PL_Q_EXT_STATUS,
                        import, arguments)
        : 0;
    ok = ok && query;
    if (ok) {
        int status = PL_next_solution(query);
        if (status == PL_S_EXCEPTION) {
            term_t exception = PL_exception(query);
            Atom *error = exception
                ? petta_libpl_exception_value(arena, exception) : NULL;
            if (error)
                *end = cetta_call_raised(error);
            ok = false;
        } else if (status != PL_S_TRUE && status != PL_S_LAST) {
            ok = false;
        }
    }
    /* Cutting keeps the load and the list of changed predicates. A failed
     * cut means the import did not complete: report it through `end` like
     * any load-time error, never as a successful import. */
    if (query) {
        Atom *cleanup = NULL;
        const PettaLibplReleaseStatus release_status =
            petta_libpl_release_query(
                arena, &cleanup, query, PETTA_LIBPL_RELEASE_CUT);
        if (release_status != PETTA_LIBPL_RELEASE_OK) {
            /* The exact SWI value crosses unmodified; a refused release is
             * the initialized failure, not an invented payload. */
            if (cleanup)
                *end = cetta_call_raised(cleanup);
            ok = false;
        }
    }
    term_t list = ok ? PL_copy_term_ref(arguments + 3) : 0;
    term_t head = ok ? PL_new_term_ref() : 0;
    term_t pair = ok ? PL_new_term_ref() : 0;
    term_t name = ok ? PL_new_term_ref() : 0;
    term_t arity_term = ok ? PL_new_term_ref() : 0;
    term_t multifile_term = ok ? PL_new_term_ref() : 0;
    ok = ok && list && head && pair && name && arity_term && multifile_term;
    while (ok && PL_get_list(list, head, list)) {
        int arity = 0;
        int multifile = 0;
        ok = PL_get_arg(1, head, pair) &&
             PL_get_arg(2, head, multifile_term) &&
             PL_get_arg(1, pair, name) &&
             PL_get_arg(2, pair, arity_term) &&
             PL_get_integer(arity_term, &arity) && arity >= 0 &&
             PL_get_bool(multifile_term, &multifile) &&
             petta_libpl_static_import_predicate(
                 runtime, arena, arguments, name, arity,
                 multifile != 0, visit, context, end);
    }
    ok = ok && PL_get_nil(list);
    if (frame)
        PL_discard_foreign_frame(frame);
    g_petta_libpl_active_runtime = previous_active_runtime;
    petta_libpl_leave(claimed);
    return ok || end->kind == CETTA_CALL_RAISED;
}

/* (entries << 1) | flag, as of the last read; 0 when never read. */
static _Atomic uint64_t g_petta_libpl_prefer_rationals_cache;

bool petta_libpl_prefer_rationals(void) {
    CettaLibraryContext *context = eval_current_library_context();
    CettaLibPrologRuntime *runtime = context ? context->lib_prolog : NULL;
    /* An engine that has not started still has SWI's default, false. */
    if (!runtime || !runtime->prepared)
        return false;
    uint64_t cached = atomic_load_explicit(
        &g_petta_libpl_prefer_rationals_cache, memory_order_acquire);
    if (cached != 0u &&
        (cached >> 1) == atomic_load_explicit(
                             &g_petta_libpl_entries, memory_order_acquire))
        return (cached & 1u) != 0u;
    bool claimed = false;
    if (!petta_libpl_enter(&claimed))
        return false;
    uint64_t entries = atomic_load_explicit(
        &g_petta_libpl_entries, memory_order_acquire);
    bool prefer = false;
    fid_t frame = PL_open_foreign_frame();
    if (frame) {
        term_t goal = PL_new_term_ref();
        prefer = goal &&
            PL_chars_to_term(
                "current_prolog_flag(prefer_rationals, true)", goal) &&
            PL_call(goal, runtime->module);
        PL_discard_foreign_frame(frame);
    }
    petta_libpl_leave(claimed);
    atomic_store_explicit(&g_petta_libpl_prefer_rationals_cache,
                          (entries << 1) | (prefer ? 1u : 0u),
                          memory_order_release);
    return prefer;
}

#include "petta_solver_client.inc"

static bool petta_libpl_call_raising(
    CettaLibPrologRuntime *runtime, Arena *arena,
    Atom *expression, Atom *expected,
    const Bindings *environment, OutcomeSet *outcomes,
    bool *recognized, Atom **raised, const CettaDelayView *delay_view) {
    if (recognized)
        *recognized = false;
    if (raised)
        *raised = NULL;
    if (!runtime || !arena ||
        !expression || !expected || !environment ||
        !outcomes || !recognized ||
        expression->kind != ATOM_EXPR ||
        expression->expr.len == 0u ||
        expression->expr.elems[0]->kind != ATOM_SYMBOL) {
        return runtime && arena && expression && expected &&
               environment && outcomes && recognized;
    }

    SymbolId head = expression->expr.elems[0]->sym_id;
    PeTTaForm form = petta_semantics_form(head);
    bool adapter_form =
        form == PETTA_FORM_IMPORT_PROLOG_FUNCTION ||
        form == PETTA_FORM_TRANSLATE_PREDICATE ||
        form == PETTA_FORM_CALL_PREDICATE ||
        form == PETTA_FORM_ASSERTA_PREDICATE ||
        form == PETTA_FORM_ASSERTZ_PREDICATE ||
        form == PETTA_FORM_RETRACT_PREDICATE;
    size_t standard_arities[2] = {0u, 0u};
    size_t standard_arity_count =
        petta_libpl_standard_function_arities(
            head, standard_arities);
    /* The admission index is a lock-free negative certificate: an ordinary
       native form, equation call, or inert constructor cannot acquire an
       embedded Prolog engine.  Saturation conservatively falls through, and
       adapter forms plus standard functions retain their explicit route. */
    if (!adapter_form && standard_arity_count == 0u &&
        !petta_libpl_import_admission_maybe_contains(
            runtime, head)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PETTA_LIBPL_ADMISSION_NEGATIVE);
        return true;
    }

    /* A foreign call observes the current logical substitution.  Materialize
     * bound inputs once at the boundary while retaining genuinely free
     * variables as output slots for the Prolog-to-Bindings result map. */
    Atom *resolved_expression = bindings_apply_if_vars(
        environment, arena, expression);
    if (!resolved_expression)
        return false;
    expression = resolved_expression;
    /* A caller that keeps delayed goals carries those on the call's
     * variables into it and reads back what each answer leaves delayed. */
    PettaLibplDelay delay = {
        .service = delay_view ? delay_view->service : NULL,
    };
    bool claimed = false;
    if (!petta_libpl_enter(&claimed))
        return false;
    if (!petta_libpl_prepare_locked(runtime)) {
        petta_libpl_leave(claimed);
        return false;
    }
    CettaLibPrologRuntime *previous_solver_runtime = g_petta_libpl_active_runtime;
    g_petta_libpl_active_runtime = runtime;
    bool solver_handled = false;
    bool solver_ok = petta_solver_try_call(runtime, arena, expression, expected,
                                           outcomes, raised, delay_view, &solver_handled);
    g_petta_libpl_active_runtime = previous_solver_runtime;
    if (!solver_ok || solver_handled) {
        if (solver_handled)
            *recognized = true;
        petta_libpl_leave(claimed);
        return solver_ok;
    }
    /* An opaque foreign call sees the ordinary residual representation. Its
     * own attribute/effect semantics never run inside the retained FD client. */
    CettaDelayService *service = delay_view
        ? (CettaDelayService *)delay_view->service : NULL;
    if (!service && delay_view && delay_view->acquire)
        service = delay_view->acquire(delay_view->context);
    if (service && cetta_delay_owner(service, CETTA_DELAY_CLIENT_PROLOG)) {
        uint32_t mark = delay_view->mark ? delay_view->mark(delay_view->context) : UINT32_MAX;
        if (mark == UINT32_MAX || !cetta_delay_materialize(service, mark)) {
            petta_libpl_leave(claimed);
            return false;
        }
    }
    delay.service = service;
    if (delay_view && !petta_libpl_delay_collect(&delay, arena, environment, expression)) {
        petta_libpl_leave(claimed);
        return false;
    }
    fid_t frame = PL_open_foreign_frame();
    if (!frame) {
        petta_libpl_leave(claimed);
        return false;
    }

    CettaLibPrologRuntime *previous_active_runtime =
        g_petta_libpl_active_runtime;
    g_petta_libpl_active_runtime = runtime;

    bool ok = true;
    if (form == PETTA_FORM_IMPORT_PROLOG_FUNCTION &&
        expression->expr.len == 2u) {
        *recognized = true;
        Atom *name = expression->expr.elems[1];
        PeTTaForm imported_form =
            name && name->kind == ATOM_SYMBOL
            ? petta_semantics_form(name->sym_id)
            : PETTA_FORM_NONE;
        bool native_form =
            name && name->kind == ATOM_SYMBOL &&
            imported_form != PETTA_FORM_NONE &&
            imported_form != PETTA_FORM_PROCESS_METTA_STRING;
        PettaLibplImport *entry =
            name && name->kind == ATOM_SYMBOL &&
            !native_form
                ? petta_libpl_register_import(
                      runtime, name->sym_id)
                : NULL;
        /* An explicit import grants full authority even when plan-time
         * resolution registered the name first. */
        if (entry) {
            entry->auto_resolved = false;
            entry->reference_stdlib = false;
        }
        if (native_form || entry) {
            Bindings empty;
            bindings_init(&empty);
            Atom *success =
                petta_semantics_success_value(arena);
            if (success)
                outcome_set_add(outcomes, success, &empty);
            else
                ok = false;
        }
    } else if (form == PETTA_FORM_TRANSLATE_PREDICATE &&
               expression->expr.len == 2u) {
        /* The machine hands over only goals it has no native view of.  A
         * goal named by a symbol is a Prolog call binding its free
         * variables, as the reference compiles it (translator.pl:597-601):
         * a predicate Prolog does not define raises its existence error
         * there too. */
        Atom *goal = expression->expr.elems[1];
        Atom *name = goal && goal->kind == ATOM_EXPR &&
                     goal->expr.len > 0u
            ? goal->expr.elems[0] : goal;
        if (name && name->kind == ATOM_SYMBOL) {
            *recognized = true;
            ok = petta_libpl_call_goal(
                runtime, arena, goal, false, outcomes, NULL, raised,
                delay_view ? &delay : NULL);
        }
    } else if (form == PETTA_FORM_CALL_PREDICATE &&
               expression->expr.len == 2u) {
        *recognized = true;
        ok = petta_libpl_call_predicate(
            runtime, arena,
            expression->expr.elems[1], outcomes, raised,
            delay_view ? &delay : NULL);
    } else if (
        (form == PETTA_FORM_ASSERTA_PREDICATE ||
         form == PETTA_FORM_ASSERTZ_PREDICATE) &&
        expression->expr.len == 2u) {
        *recognized = true;
        ok = petta_libpl_assert_predicate(
            runtime, arena,
            expression->expr.elems[1], outcomes,
            form == PETTA_FORM_ASSERTA_PREDICATE, raised);
    } else if (
        form == PETTA_FORM_RETRACT_PREDICATE &&
        expression->expr.len == 2u) {
        *recognized = true;
        ok = petta_libpl_retract_predicate(
            runtime, arena,
            expression->expr.elems[1], outcomes, raised);
    } else {
        PettaLibplImport *entry =
            petta_libpl_find_import(runtime, head);
        size_t available_standard_arity_count =
            entry ? 0u : standard_arity_count;
        if (!entry && available_standard_arity_count > 0u) {
            size_t supplied =
                (size_t)(expression->expr.len - 1u);
            for (size_t index = 0u;
                 index < available_standard_arity_count; index++) {
                if (standard_arities[index] != supplied)
                    continue;
                entry = petta_libpl_register_import(
                    runtime, head);
                if (entry &&
                    !petta_libpl_import_add_arity(
                        entry, supplied)) {
                    ok = false;
                }
                break;
            }
        }
        if (entry &&
            ok &&
            petta_libpl_refresh_arities(
                runtime, entry)) {
            size_t function_arity =
                (size_t)(expression->expr.len - 1u);
            bool arity_matched = false;
            for (size_t index = 0u;
                 index < entry->arity_len; index++) {
                if (entry->function_arities[index] ==
                    function_arity) {
                    arity_matched = true;
                    *recognized = true;
                    Atom *thrown = NULL;
                    ok = petta_libpl_registered_call(
                        runtime, arena, expression,
                        expected, entry, outcomes, &thrown,
                        delay_view ? &delay : NULL);
                    if (!ok && thrown) {
                        if (raised)
                            *raised = thrown;
                        ok = true;
                    }
                    break;
                }
            }
            if (!arity_matched &&
                entry->arity_len == 0u &&
                !entry->reference_stdlib) {
                *recognized = true;
                Atom *thrown = NULL;
                ok = petta_libpl_registered_call(
                    runtime, arena, expression,
                    expected, entry, outcomes, &thrown,
                    delay_view ? &delay : NULL);
                if (!ok) {
                    /* An explicit import registers a PeTTa function name
                     * before SWI necessarily has a predicate for it.  A
                     * successful call may autoload and thereby establish
                     * the arity; an undefined name instead remains an
                     * uninterpreted partial application, as in reference
                     * PeTTa.  Errors from a predicate that does exist remain
                     * genuine foreign-boundary failures. */
                    bool exists = false;
                    if (petta_libpl_predicate_exists(
                            runtime, entry->name, entry->name_len,
                            function_arity + 1u, &exists) &&
                        !exists) {
                        Atom *partial =
                            petta_semantics_partial_value(
                                arena,
                                expression->expr.elems[0],
                                expression->expr.elems + 1u,
                                expression->expr.len - 1u);
                        Bindings empty;
                        bindings_init(&empty);
                        CettaCount prior_len = outcomes->len;
                        if (partial) {
                            outcome_set_add(
                                outcomes, partial, &empty);
                        }
                        ok = partial &&
                            outcomes->len == prior_len + 1u;
                    } else if (thrown) {
                        if (raised)
                            *raised = thrown;
                        ok = true;
                    }
                }
                if (ok && *recognized) {
                    ok = petta_libpl_import_add_arity(
                        entry, function_arity);
                }
            }
        }
    }

    PL_discard_foreign_frame(frame);
    g_petta_libpl_active_runtime =
        previous_active_runtime;
    petta_libpl_leave(claimed);
    return ok;
}

bool petta_libpl_call(
    CettaLibPrologRuntime *runtime, Arena *arena,
    Atom *expression, Atom *expected,
    const Bindings *environment, OutcomeSet *outcomes,
    bool *recognized, CettaCallOutcome *end,
    const CettaDelayView *delay) {
    Atom *raised = NULL;
    bool called = petta_libpl_call_raising(
        runtime, arena, expression, expected, environment,
        outcomes, recognized, &raised, delay);
    if (end)
        *end = raised ? cetta_call_raised(raised) : cetta_call_failure();
    return called;
}
