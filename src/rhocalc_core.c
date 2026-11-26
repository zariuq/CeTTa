/*
 * rhocalc_core.c — the rho-calculus engine behind `cetta --lang rhocalc`.
 *
 * The object language is Meredith and Radestock's reflective higher-order
 * rho-calculus.  Processes and names are mutually defined — a name is a
 * quoted process @P, and *x (drop) runs the process a name quotes — and the
 * single computational rule is COMM, the rendezvous of a send and a receive
 * on one channel:
 *
 *     x!(m) | for(y <- x){P}   ——COMM——>   P[y := m]
 *
 * Two semantic profiles share this file:
 *
 *   strict-core   The pure asynchronous spine: exactly the six constructors
 *                 rho:nil, rho:par, rho:send, rho:recv, rho:quote, rho:drop,
 *                 with COMM as the only reduction.  A free-standing drop is
 *                 inert; dequotation happens only through substitution after
 *                 a COMM.  The rhometta extensions add deferred MeTTa
 *                 payloads (rho:eval-payload) and persistent input
 *                 (rho:recv-persistent).  A persistent COMM consumes its send
 *                 but retains the receive, matching Rholang's `for (... <=
 *                 channel)` contract.  Each deferred payload runs against
 *                 sibling-isolated copy-on-write scratch.
 *
 *   cost          Funded reactions.  Processes are signed
 *                 (rho:cost:signed body sig), channels carry purses of
 *                 tokens (rho:cost:purse location stack), and a COMM fires
 *                 only when purses located on its own channel exactly cover
 *                 the consumed signature product — an unfunded reaction is
 *                 disabled, not slowed.  Every firing can emit a causal
 *                 event (fresh id, producer-cause list, funding records,
 *                 consumed signature); a run's ordered events plus its
 *                 residual form a receipt.  Frontier enumeration is exact
 *                 and never silently truncated; bounded execution reports
 *                 fuel/search exhaustion honestly and never claims
 *                 quiescence it has not proven.
 *
 * Shared machinery: a structural total order on terms, alpha-invariant and
 * computed over constructors, gives both profiles a canonical form (rho:par
 * flattened and sorted by the order, the name equivalence @(*x) = x oriented
 * as a simplification), so structural congruence becomes equality in the
 * order.  Purse location follows the same name equivalence: aliases of a
 * channel are the same account.
 *
 * Concurrency: the strict-core threaded executor matches endpoints through
 * per-channel buckets; the cost profile executes waves of compatible
 * firings, where each worker atomically claims the exact endpoint and
 * funding-cover occurrences (in sorted resource order) before firing, and
 * every wave commit is re-validated against occurrence accounting.  Causal
 * receipts are an optional observer on either path: a state-only run and a
 * receipt-observed run consume identical resources.
 *
 * Interfaces: lib/lts/rho/cost.metta and lib/lts/rho.metta (MeTTa API),
 * .rho / .mrho text (rhocalc_syntax.c), and the CLI (main.c;
 * `--lang rhocalc [--profile cost] [--num-threads N]`).  Tests live in
 * tests/test_lts_rho_cost_*.metta, tests/rhocalc_run/, and
 * tests/rhocalc_cost_run/; a differential harness replays compiled-C
 * receipts as Lean derivations.  The design commitments — located funding,
 * causality from consumption, exact receipts with lossy valuations derived
 * afterward — follow the cost-accounting programme for the reflective
 * higher-order calculus; the companion papers state the theory.
 */

#include "rhocalc_core.h"
#include <inttypes.h>
#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "abt.h"
#include "eval.h"
#include "fallible_alloc.h"
#include "match.h"
#include "parallel_executor.h"
#include "stats.h"
#include "state_cell.h"
#include "runner.h"

#pragma weak state_cell_identity_pin
#pragma weak state_cell_identity_unpin
#pragma weak state_cell_is_managed
#pragma weak state_cell_snapshot_acquire
#pragma weak state_cell_snapshot_release
#pragma weak state_cell_snapshot_retain_into

typedef struct {
    StateCellSnapshot snapshot;
    void *identity_pin;
} RhoStateObservation;

#ifdef CETTA_TEST_HOOKS
static void (*rho_state_observed_test)(StateCell *);
#endif

static bool rho_state_observation_acquire(StateCell *cell,
                                           RhoStateObservation *observation) {
    *observation = (RhoStateObservation){0};
    if (!cell)
        return true;
    if (!state_cell_identity_pin || !state_cell_identity_unpin ||
        !state_cell_snapshot_acquire || !state_cell_snapshot_release)
        return !state_cell_is_managed || !state_cell_is_managed(cell);
    observation->identity_pin = state_cell_identity_pin(cell);
    if (!observation->identity_pin)
        return true;
    if (!state_cell_snapshot_acquire(cell, &observation->snapshot)) {
        state_cell_identity_unpin(observation->identity_pin);
        *observation = (RhoStateObservation){0};
        return false;
    }
    return true;
}

static void rho_state_observation_release(RhoStateObservation *observation) {
    if (observation->identity_pin) {
        state_cell_snapshot_release(&observation->snapshot);
        state_cell_identity_unpin(observation->identity_pin);
        *observation = (RhoStateObservation){0};
    }
}

static StateCellSnapshot rho_state_observation_view(
        StateCell *cell, const RhoStateObservation *observation) {
    return observation->identity_pin ? observation->snapshot :
        (StateCellSnapshot){.value = cell ? cell->value : NULL,
            .content_type = cell ? cell->content_type : NULL};
}

#ifndef CETTA_RHOCOST_COMMIT_AUDIT
#define CETTA_RHOCOST_COMMIT_AUDIT 0
#endif

#if CETTA_RHOCOST_COMMIT_AUDIT != 0 && CETTA_RHOCOST_COMMIT_AUDIT != 1
#error "CETTA_RHOCOST_COMMIT_AUDIT must be 0 or 1"
#endif

typedef enum {
    RHO_BAD = 0,
    RHO_NIL,
    RHO_PAR,
    RHO_SEND,
    RHO_RECV,
    RHO_RECV_PERSISTENT,
    RHO_QUOTE,
    RHO_DROP,
    RHO_VAL,
    RHO_EVAL_PAYLOAD
} RhoKind;

typedef struct {
    RhoKind kind;
    Atom **args;
    uint32_t nargs;
} RhoView;

typedef struct {
    VarId *items;
    uint32_t len;
    uint32_t cap;
} RhoScope;

typedef struct {
    Atom **items;
    uint32_t len;
    uint32_t cap;
} RhoAtomVec;

typedef struct {
    Atom **items;
    uint32_t len;
    uint32_t cap;
} RhoSuccessorSetAcc;

typedef struct {
    Atom **items;
    uint32_t len;
} RhoSuccessorSet;

static bool rho_collect_successors(Arena *arena, Atom *proc,
                                   const RhocalcEvalContext *eval_context,
                                   RhoSuccessorSetAcc *out);
static bool rhocalc_collect_successor_set(Arena *arena, Atom *proc,
                                          const RhocalcEvalContext *eval_context,
                                          RhoSuccessorSet *out);
static bool rhocalc_collect_quiescent_set(
    Arena *arena, Atom *proc, const RhocalcEvalContext *eval_context,
    RhoSuccessorSet *out);
static void rhocalc_successor_set_free(RhoSuccessorSet *set);

typedef struct {
    uint32_t component_index;
    RhoView view;
} RhoEndpoint;

typedef struct {
    RhoEndpoint *items;
    uint32_t len;
    uint32_t cap;
} RhoEndpointVec;

/* Hashes of atoms already hashed, by identity: payload expression nodes, and
 * names standing alone.  Valid while the atoms they name are alive. */
typedef struct {
    const Atom **keys;
    uint64_t *values;
    size_t len;
    size_t cap;
} RhoHashMemo;

typedef struct {
    Arena *arena;
    Arena *result_arena;
    Arena process_arenas[2];
    uint32_t active_process_arena;
    uint32_t reductions_since_compaction;
    bool process_arenas_initialized;
    bool compaction_safe;
    RhoHashMemo channel_hashes;   /* channels of the current generation */
    RhoRuntimeProfile profile;
    const RhocalcEvalContext *eval_context;
    Atom *current;
    uint64_t rotating_turn;
    /* Internal abstract-machine view: this COMM index is rebuilt from the
       residual process each round, not persisted as separate channel state. */
    RhoAtomVec components;
    RhoEndpointVec sends;
    RhoEndpointVec recvs;
    bool comm_index_loaded;
    bool current_canonical_order;
} RhoMachine;

/* The growable lists above and below push through Cesta's checked worklist,
 * declared in rhocalc_lists.export: capacity starts at 8 and doubles, size
 * overflow is refused, and a failed push leaves the list unchanged. */
typedef Atom *RhoAtomRef;
typedef void *RhoPointer;
static void *rho_resize(void *old, uint32_t bytes) { return cetta_realloc(old, bytes); }
static void *rho_resize_count(void *old, CettaCount bytes) { return cetta_realloc(old, (size_t)bytes); }
#include "generated/rhocalc_lists.generated.h"

/* A push to a list with room left is the plain store the worklist would make;
 * growth, and a length beyond the capacity, go through its checks.  (A value
 * written with braces is parenthesized, so that its commas stay one argument.) */
#define RHO_PUSH(push, list, value)                                            \
    (__builtin_expect((list)->len < (list)->cap, 1)                            \
         ? ((list)->items[(list)->len++] = (value), true)                      \
         : (bool)(push)(rho_resize, &(list)->items, &(list)->len, &(list)->cap, \
                        (value)))

typedef struct {
    bool ready;
    SymbolId nil;
    SymbolId par;
    SymbolId send;
    SymbolId recv;
    SymbolId recv_persistent;
    SymbolId quote;
    SymbolId drop;
    SymbolId val;
    SymbolId eval_payload;
    SymbolId cost_nil;
    SymbolId cost_par;
    SymbolId cost_signed;
    SymbolId cost_purse;
    SymbolId cost_stack_empty;
    SymbolId cost_stack_cons;
    SymbolId cost_sig_mul;
} RhoSymbolIds;

typedef enum {
    RHO_ASYNC_ENDPOINT_SEND = 0,
    RHO_ASYNC_ENDPOINT_RECV = 1
} RhoAsyncEndpointKind;

/* A task's region: the arena that holds the task's term and record and
 * nothing else.  It is released when the task ends -- at a COMM, a spawn or
 * a 0 -- and kept while the task waits on a channel or is stuck.  A region
 * is one allocation: the record below, followed by the arena's first block,
 * which the region lends it.  Released regions are kept for the next tasks
 * as long as the run's live tasks have recently needed them (the region
 * reuse below). */
typedef struct RhoTaskRegion RhoTaskRegion;
struct RhoTaskRegion {
    Arena arena;
    RhoTaskRegion *next;  /* in a worker's stock or the run's pool */
};

/* The symbol table's own atoms for the rho symbols, looked up once per
 * threaded run, for the heads of the terms copied into regions. */
typedef struct {
    Atom *nil, *par, *send, *recv, *recv_persistent, *quote, *drop, *val,
        *eval_payload;
} RhoHeadAtoms;

/* A live task of a threaded run: its process in normal form, which its
 * region holds with this record, and, while it waits or is stuck, the name
 * supply it ran with.  While it is queued, its supply is the queue's. */
typedef struct RhoTask RhoTask;
struct RhoTask {
    RhoTaskRegion *region;
    Atom *proc;
    RhoView view;
    RhoAsyncEndpointKind kind;
    RhoTask *next;            /* in a bucket's queue, or among the stuck */
    CettaNameSupply supply;
    bool holds_supply;
};

typedef struct RhoChannelBucket RhoChannelBucket;
struct RhoChannelBucket {
    Atom *channel;        /* the channel, copied into the table's arena */
    uint64_t hash;
    pthread_mutex_t mutex;
    RhoTask *send_head;
    RhoTask *send_tail;
    RhoTask *recv_head;
    RhoTask *recv_tail;
    RhoChannelBucket *next;
};

typedef struct {
    pthread_mutex_t mutex;
    RhoChannelBucket *head;
    Arena arena;          /* the buckets' channels */
} RhoChannelTable;

/* What one worker of a threaded run keeps across its tasks: released
 * regions at hand, and the task ends it has not yet counted in the run's. */
typedef struct {
    RhoTaskRegion *stock;
    uint32_t stock_len;
    uint32_t ends;
} RhoAsyncWorkerState;

typedef struct RhoAsyncExecutor RhoAsyncExecutor;

struct RhoAsyncExecutor {
    Arena *global_arena;
    RhoRuntimeProfile profile;
    const RhocalcEvalContext *eval_context;
    CettaParallelExecutor parallel;
    RhoChannelTable channels;
    RhoTask *stuck;                 /* the tasks that can never run again */
    pthread_mutex_t stuck_mutex;
    RhoHeadAtoms heads;
    RhoAsyncWorkerState *workers;
    pthread_mutex_t region_mutex;
    RhoTaskRegion *region_pool;     /* released regions beyond the stocks */
    _Atomic uint64_t region_pool_len;
    /* Under region_mutex: the fewest regions the pool held in the current
     * window, and in it and the one before: those no task needed while
     * the two lasted. */
    uint64_t region_pool_low;
    uint64_t region_pool_low_both;
    /* Task ends counted so far, and the count that closes the window. */
    _Atomic uint64_t region_ends;
    _Atomic uint64_t region_window_end;
    RhocalcThreadedTestHooks hooks;
    _Atomic uint64_t regions_made;
    _Atomic uint64_t regions_freed;
    _Atomic uint32_t budget;
    _Atomic uint32_t reductions_taken;
    _Atomic uint32_t branch_turn;
};

/*
 * Threaded run invariant: each bucket-locked rendezvous consumes one send and
 * one receive endpoint, computes the same COMM frontier the sequential machine
 * would see there, and then schedules exactly one continuation from that
 * frontier for the run path. The final state is materialized from unmatched
 * endpoints plus stuck residual processes, so a threaded run serializes to some
 * legal Reduces* path rather than accumulating an entire branching frontier.
 */
static RhoSymbolIds g_rho_syms = {0};
static __thread bool g_rho_async_worker_active = false;
static __thread char g_rhocalc_validation_error[256];
static _Atomic uint64_t g_rhocalc_cost_wave_turn = 0u;

/* ── Symbol interning and head dispatch ─────────────────────────────────── */

static void rho_symbols_ensure(void) {
    if (g_rho_syms.ready) return;
    g_rho_syms.nil = symbol_intern_cstr(g_symbols, "rho:nil");
    g_rho_syms.par = symbol_intern_cstr(g_symbols, "rho:par");
    g_rho_syms.send = symbol_intern_cstr(g_symbols, "rho:send");
    g_rho_syms.recv = symbol_intern_cstr(g_symbols, "rho:recv");
    g_rho_syms.recv_persistent =
        symbol_intern_cstr(g_symbols, "rho:recv-persistent");
    g_rho_syms.quote = symbol_intern_cstr(g_symbols, "rho:quote");
    g_rho_syms.drop = symbol_intern_cstr(g_symbols, "rho:drop");
    g_rho_syms.val = symbol_intern_cstr(g_symbols, "rho:val");
    g_rho_syms.eval_payload = symbol_intern_cstr(g_symbols, "rho:eval-payload");
    g_rho_syms.cost_nil = symbol_intern_cstr(g_symbols, "rho:cost:nil");
    g_rho_syms.cost_par = symbol_intern_cstr(g_symbols, "rho:cost:par");
    g_rho_syms.cost_signed = symbol_intern_cstr(g_symbols, "rho:cost:signed");
    g_rho_syms.cost_purse = symbol_intern_cstr(g_symbols, "rho:cost:purse");
    g_rho_syms.cost_stack_empty =
        symbol_intern_cstr(g_symbols, "rho:cost:stack-empty");
    g_rho_syms.cost_stack_cons =
        symbol_intern_cstr(g_symbols, "rho:cost:stack-cons");
    g_rho_syms.cost_sig_mul =
        symbol_intern_cstr(g_symbols, "rho:cost:sig-mul");
    g_rho_syms.ready = true;
}

static SymbolId rho_head_symbol_id(const char *head) {
    rho_symbols_ensure();
    if (strcmp(head, "rho:nil") == 0) return g_rho_syms.nil;
    if (strcmp(head, "rho:par") == 0) return g_rho_syms.par;
    if (strcmp(head, "rho:send") == 0) return g_rho_syms.send;
    if (strcmp(head, "rho:recv") == 0) return g_rho_syms.recv;
    if (strcmp(head, "rho:recv-persistent") == 0) {
        return g_rho_syms.recv_persistent;
    }
    if (strcmp(head, "rho:quote") == 0) return g_rho_syms.quote;
    if (strcmp(head, "rho:drop") == 0) return g_rho_syms.drop;
    if (strcmp(head, "rho:val") == 0) return g_rho_syms.val;
    if (strcmp(head, "rho:eval-payload") == 0) return g_rho_syms.eval_payload;
    if (strcmp(head, "rho:cost:nil") == 0) return g_rho_syms.cost_nil;
    if (strcmp(head, "rho:cost:par") == 0) return g_rho_syms.cost_par;
    if (strcmp(head, "rho:cost:signed") == 0) return g_rho_syms.cost_signed;
    if (strcmp(head, "rho:cost:purse") == 0) return g_rho_syms.cost_purse;
    if (strcmp(head, "rho:cost:stack-empty") == 0) {
        return g_rho_syms.cost_stack_empty;
    }
    if (strcmp(head, "rho:cost:stack-cons") == 0) {
        return g_rho_syms.cost_stack_cons;
    }
    if (strcmp(head, "rho:cost:sig-mul") == 0) return g_rho_syms.cost_sig_mul;
    assert(!g_rho_async_worker_active &&
           "rho worker attempted to intern a non-rho head symbol");
    return symbol_intern_cstr(g_symbols, head);
}

static void rho_validation_clear(void) {
    g_rhocalc_validation_error[0] = '\0';
}

static void rho_validation_set(const char *fmt, ...) {
    va_list ap;
    if (g_rhocalc_validation_error[0]) return;
    va_start(ap, fmt);
    vsnprintf(g_rhocalc_validation_error, sizeof(g_rhocalc_validation_error),
              fmt, ap);
    va_end(ap);
}

const char *rhocalc_last_validation_error(void) {
    return g_rhocalc_validation_error[0] ? g_rhocalc_validation_error : NULL;
}

const char *rhocalc_semantic_profile_name(RhocalcSemanticProfileId profile) {
    switch (profile) {
    case RHOCALC_SEMANTIC_PROFILE_STRICT_CORE:
        return "strict-core";
    case RHOCALC_SEMANTIC_PROFILE_COST:
        return "cost";
    case RHOCALC_SEMANTIC_PROFILE_RHOMETTA:
        return "rhometta";
    }
    return "unknown";
}

static bool rhocalc_semantic_profile_runtime_supported(
    RhocalcSemanticProfileId profile) {
    if (profile == RHOCALC_SEMANTIC_PROFILE_STRICT_CORE ||
        profile == RHOCALC_SEMANTIC_PROFILE_COST ||
        profile == RHOCALC_SEMANTIC_PROFILE_RHOMETTA) {
        return true;
    }
    rho_validation_clear();
    rho_validation_set(
        "unsupported rhocalc semantic profile '%s'",
        rhocalc_semantic_profile_name(profile));
    return false;
}

static bool rhocost_term_well_formed(Atom *term);
static Atom *rhocost_successor_frontier_expr(Arena *arena, Atom *term);
static bool rhocost_reduce_to_quiescence_with_profile(
    Arena *arena, Atom *term, const RhoRuntimeProfile *profile,
    RhoReductionResult *out);
static bool rho_runtime_profile_supported(const RhoRuntimeProfile *profile);

static bool rho_symbol_named(Atom *atom, const char *name) {
    return atom && atom->kind == ATOM_SYMBOL &&
           strcmp(atom_name_cstr(atom), name) == 0;
}

static RhoView rho_view(Atom *atom) {
    RhoView view = {RHO_BAD, NULL, 0};
    if (rho_symbol_named(atom, "rho:nil")) {
        view.kind = RHO_NIL;
        return view;
    }
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len == 0 ||
        atom->expr.elems[0]->kind != ATOM_SYMBOL) {
        return view;
    }

    const char *head = atom_name_cstr(atom->expr.elems[0]);
    view.args = atom->expr.elems + 1;
    view.nargs = atom->expr.len - 1;
    if (strcmp(head, "rho:par") == 0) view.kind = RHO_PAR;
    else if (strcmp(head, "rho:send") == 0) view.kind = RHO_SEND;
    else if (strcmp(head, "rho:recv") == 0) view.kind = RHO_RECV;
    else if (strcmp(head, "rho:recv-persistent") == 0) {
        view.kind = RHO_RECV_PERSISTENT;
    }
    else if (strcmp(head, "rho:quote") == 0) view.kind = RHO_QUOTE;
    else if (strcmp(head, "rho:drop") == 0) view.kind = RHO_DROP;
    else if (strcmp(head, "rho:val") == 0) view.kind = RHO_VAL;
    else if (strcmp(head, "rho:eval-payload") == 0) {
        view.kind = RHO_EVAL_PAYLOAD;
    }
    return view;
}

static Atom *rho_call(Arena *arena, const char *head,
                      Atom *const *args, uint32_t nargs) {
    Atom **elems = arena_alloc(arena, sizeof(Atom *) * (size_t)(nargs + 1));
    elems[0] = atom_symbol_id(arena, rho_head_symbol_id(head));
    for (uint32_t i = 0; i < nargs; i++) {
        elems[i + 1] = args[i];
    }
    return atom_expr(arena, elems, nargs + 1);
}

static Atom *rho_nil(Arena *arena) {
    rho_symbols_ensure();
    return atom_symbol_id(arena, g_rho_syms.nil);
}

static Atom *rho_unary(Arena *arena, const char *head, Atom *arg) {
    Atom *args[1] = {arg};
    return rho_call(arena, head, args, 1);
}

static Atom *rho_binary(Arena *arena, const char *head, Atom *a, Atom *b) {
    Atom *args[2] = {a, b};
    return rho_call(arena, head, args, 2);
}

static Atom *rho_ternary(Arena *arena, const char *head,
                         Atom *a, Atom *b, Atom *c) {
    Atom *args[3] = {a, b, c};
    return rho_call(arena, head, args, 3);
}

static Atom *rho_value_proc(Arena *arena, Atom *value) {
    return rho_unary(arena, "rho:val", value);
}

static bool rho_is_builtin_quote(Atom *atom) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == 2 &&
           atom->expr.elems[0]->kind == ATOM_SYMBOL &&
           atom_is_symbol_id(atom->expr.elems[0], g_builtin_syms.quote);
}

static Atom *rho_unquote_payload_atom(Atom *atom) {
    return rho_is_builtin_quote(atom) ? atom->expr.elems[1] : atom;
}

static bool rho_atom_contains_var_id(Atom *atom, VarId var_id) {
    if (!atom || !atom_has_vars(atom)) return false;
    if (atom->kind == ATOM_VAR) return var_id_eq(atom->var_id, var_id);
    if (atom->kind != ATOM_EXPR) return false;
    for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
        if (rho_atom_contains_var_id(atom->expr.elems[i], var_id)) return true;
    }
    return false;
}

static Atom *rho_atom_replace_var(Arena *arena, Atom *atom, VarId var_id,
                                  Atom *replacement) {
    Bindings subst;
    Atom *result;

    if (!atom_has_vars(atom)) return atom;
    bindings_init(&subst);
    if (!bindings_add_id(&subst, var_id, replacement ? replacement->sym_id
                                                     : SYMBOL_ID_NONE,
                         replacement)) {
        bindings_free(&subst);
        return atom;
    }
    result = bindings_apply_if_vars(&subst, arena, atom);
    bindings_free(&subst);
    return result;
}

static void rho_scope_init(RhoScope *scope) {
    scope->items = NULL;
    scope->len = 0;
    scope->cap = 0;
}

static void rho_scope_free(RhoScope *scope) {
    free(scope->items);
    scope->items = NULL;
    scope->len = 0;
    scope->cap = 0;
}

static uint32_t rho_scope_mark(const RhoScope *scope) {
    return scope->len;
}

static void rho_scope_pop(RhoScope *scope, uint32_t mark) {
    if (mark <= scope->len) scope->len = mark;
}

static bool rho_scope_push(RhoScope *scope, VarId var_id) {
    return RHO_PUSH(rho_push_var, scope, var_id);
}

static void rho_vec_init(RhoAtomVec *vec) {
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static void rho_vec_free(RhoAtomVec *vec) {
    free(vec->items);
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static bool rho_vec_push(RhoAtomVec *vec, Atom *atom) {
    return RHO_PUSH(rho_push_atom, vec, atom);
}

static void rho_successor_set_acc_init(RhoSuccessorSetAcc *acc) {
    *acc = (RhoSuccessorSetAcc){0};
}

static void rho_successor_set_acc_free(RhoSuccessorSetAcc *acc) {
    free(acc->items);
    *acc = (RhoSuccessorSetAcc){0};
}

static void rho_endpoint_vec_init(RhoEndpointVec *vec) {
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static void rho_endpoint_vec_free(RhoEndpointVec *vec) {
    free(vec->items);
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static bool rho_endpoint_vec_push(RhoEndpointVec *vec,
                                  uint32_t component_index,
                                  RhoView view) {
    return RHO_PUSH(rho_push_endpoint, vec, ((RhoEndpoint){component_index, view}));
}

/* ── Strict-core grammar validation ─────────────────────────────────────────
 *
 * Mutual recursion mirrors the calculus: names and processes check each
 * other, a recv binder opens a scope over its body, and a quotation seals
 * the surrounding scope (a quoted process starts a fresh scope).  Non-core
 * forms are rejected with a stored diagnostic, never left inert. */

static bool rho_check_proc(Atom *proc, RhoScope *scope,
                           bool allow_eval_payloads);

static bool rho_check_name(Atom *name, RhoScope *scope,
                           bool allow_eval_payloads) {
    (void)scope;
    if (!name) {
        rho_validation_set("missing rho name");
        return false;
    }
    if (name->kind == ATOM_VAR) {
        return true;
    }
    RhoView view = rho_view(name);
    if (view.kind == RHO_QUOTE && view.nargs == 1) {
        RhoScope quote_scope;
        bool ok;
        rho_scope_init(&quote_scope);
        ok = rho_check_proc(view.args[0], &quote_scope,
                            allow_eval_payloads);
        rho_scope_free(&quote_scope);
        return ok;
    }
    rho_validation_set("rho name must be a quoted process or bound variable");
    return false;
}

static bool rho_check_proc(Atom *proc, RhoScope *scope,
                           bool allow_eval_payloads) {
    RhoView view = rho_view(proc);
    switch (view.kind) {
    case RHO_NIL:
        return true;
    case RHO_PAR:
        for (uint32_t i = 0; i < view.nargs; i++) {
            if (!rho_check_proc(view.args[i], scope,
                                allow_eval_payloads)) {
                return false;
            }
        }
        return true;
    case RHO_SEND:
        if (view.nargs != 2) {
            rho_validation_set("rho:send expects channel and process payload");
            return false;
        }
        return rho_check_name(view.args[0], scope, allow_eval_payloads) &&
               rho_check_proc(view.args[1], scope, allow_eval_payloads);
    case RHO_RECV:
    case RHO_RECV_PERSISTENT: {
        if (view.kind == RHO_RECV_PERSISTENT && !allow_eval_payloads) {
            rho_validation_set(
                "unsupported rho core form 'rho:recv-persistent'");
            return false;
        }
        if (view.nargs != 3) {
            rho_validation_set(
                "%s expects channel, binder, and body",
                view.kind == RHO_RECV_PERSISTENT
                    ? "rho:recv-persistent" : "rho:recv");
            return false;
        }
        if (view.args[1]->kind != ATOM_VAR) {
            rho_validation_set(
                "%s binder must be a variable",
                view.kind == RHO_RECV_PERSISTENT
                    ? "rho:recv-persistent" : "rho:recv");
            return false;
        }
        if (!rho_check_name(view.args[0], scope, allow_eval_payloads)) {
            return false;
        }
        uint32_t mark = rho_scope_mark(scope);
        if (!rho_scope_push(scope, view.args[1]->var_id)) return false;
        bool ok = rho_check_proc(view.args[2], scope,
                                 allow_eval_payloads);
        rho_scope_pop(scope, mark);
        return ok;
    }
    case RHO_DROP:
        if (view.nargs != 1) {
            rho_validation_set("rho:drop expects one name");
            return false;
        }
        return rho_check_name(view.args[0], scope, allow_eval_payloads);
    case RHO_VAL:
        if (!allow_eval_payloads) {
            rho_validation_set("unsupported rho core form 'rho:val'");
            return false;
        }
        if (view.nargs != 1) {
            rho_validation_set("rho:val expects one wrapped value");
            return false;
        }
        return true;
    case RHO_EVAL_PAYLOAD:
        if (!allow_eval_payloads) {
            rho_validation_set(
                "unsupported rho core form 'rho:eval-payload'");
            return false;
        }
        if (view.nargs != 1) {
            rho_validation_set(
                "rho:eval-payload expects one MeTTa payload term");
            return false;
        }
        if (!rho_is_builtin_quote(view.args[0])) {
            rho_validation_set(
                "rho:eval-payload expects one quoted MeTTa payload term");
            return false;
        }
        return true;
    case RHO_QUOTE:
        rho_validation_set("rho:quote is a name, not a process");
        return false;
    case RHO_BAD:
        break;
    }
    if (proc && proc->kind == ATOM_EXPR && proc->expr.len > 0 &&
        proc->expr.elems[0]->kind == ATOM_SYMBOL) {
        rho_validation_set("unsupported rho core form '%s'",
                           atom_name_cstr(proc->expr.elems[0]));
    } else {
        rho_validation_set("unsupported rho process atom");
    }
    return false;
}

bool rhocalc_process_well_formed(Atom *proc, RhocalcSemanticProfileId semantic_profile) {
    if (!rhocalc_semantic_profile_runtime_supported(semantic_profile)) {
        return false;
    }
    if (semantic_profile == RHOCALC_SEMANTIC_PROFILE_COST) {
        return rhocost_term_well_formed(proc);
    }
    RhoScope scope;
    rho_validation_clear();
    rho_scope_init(&scope);
    bool ok = rho_check_proc(proc, &scope,
                             semantic_profile == RHOCALC_SEMANTIC_PROFILE_RHOMETTA);
    rho_scope_free(&scope);
    return ok;
}

/* ── Structural order and hash ──────────────────────────────────────────────
 *
 * Structural congruence is decided by a total order on processes and names,
 * computed over their constructors.  rho:par components sort by it,
 * successor sets dedup by it, and endpoint matching compares channels by it.
 * The order is the lexicographic order of a term's written form taken symbol
 * by symbol, where a symbol is either a constructor mark or a byte of a
 * leaf's text, a mark before a byte of the same value:
 *
 *   - process constructors rank as their names spell: 0 < (malformed) <
 *     defer < drop < par < recv < recv-persistent < send < val; a name is a
 *     variable, then a malformed name, then a quotation;
 *   - a parallel composition compares its components in turn, the shorter
 *     sequence first when one is a prefix of the other;
 *   - a bound variable is its binder's number, binders numbered in the order
 *     a walk of the whole term meets them (a quotation numbers its own from
 *     zero, its outer binders out of view), numbers compared by their
 *     decimal digits; a free variable is its spelling, base and epoch;
 *   - a payload (rho:val, rho:eval-payload) compares by its MeTTa atom: an
 *     expression by its elements in turn, a state cell as (State value) by
 *     its current value, a leaf by its text and the mark that ends it, and
 *     leaves whose texts coincide by their kind.
 *
 * Alpha-equivalent terms are equal, since a bound variable compares by its
 * binder's number and never by its identity, and the name equivalence
 * @(*x) = x is applied before comparing.  Canonical form (below) flattens
 * rho:par and sorts components by this order, so structurally congruent
 * processes normalize to equal terms.  The walk visits constructors and stops
 * at the first difference; identical atoms are equal without a walk wherever
 * their written form cannot depend on the binders in view.
 *
 * The hash is computed over the same structure, a bound variable by its
 * binder's number counted from where hashing starts (equality, unlike the
 * order of decimal digits, does not depend on that start), so equal terms
 * hash alike.  Expression nodes of payloads may be memoized by identity. */

/* Process and name constructors, ranked as their written names spell. */
enum {
    RHO_RANK_NIL = 0,
    RHO_RANK_BAD,
    RHO_RANK_DEFER,
    RHO_RANK_DROP,
    RHO_RANK_PAR,
    RHO_RANK_RECV,
    RHO_RANK_RECV_PERSISTENT,
    RHO_RANK_SEND,
    RHO_RANK_VAL,
};

enum {
    RHO_NAME_RANK_VAR = 0,
    RHO_NAME_RANK_BAD,
    RHO_NAME_RANK_QUOTE,
};

static int rho_proc_rank(const RhoView *view) {
    switch (view->kind) {
    case RHO_NIL:
        return RHO_RANK_NIL;
    case RHO_PAR:
        return RHO_RANK_PAR;
    case RHO_SEND:
        return view->nargs == 2u ? RHO_RANK_SEND : RHO_RANK_BAD;
    case RHO_RECV:
    case RHO_RECV_PERSISTENT:
        if (view->nargs != 3u || view->args[1]->kind != ATOM_VAR)
            return RHO_RANK_BAD;
        return view->kind == RHO_RECV ? RHO_RANK_RECV
                                      : RHO_RANK_RECV_PERSISTENT;
    case RHO_DROP:
        return view->nargs == 1u ? RHO_RANK_DROP : RHO_RANK_BAD;
    case RHO_VAL:
        return view->nargs == 1u ? RHO_RANK_VAL : RHO_RANK_BAD;
    case RHO_EVAL_PAYLOAD:
        return view->nargs == 1u ? RHO_RANK_DEFER : RHO_RANK_BAD;
    case RHO_QUOTE:
    case RHO_BAD:
        break;
    }
    return RHO_RANK_BAD;
}

/* The name equivalence @(*x) = x: a quotation of a drop is the dropped
 * name. */
static Atom *rho_name_collapse(Atom *name) {
    for (;;) {
        RhoView view;
        RhoView inner;
        if (!name || name->kind == ATOM_VAR) return name;
        view = rho_view(name);
        if (view.kind != RHO_QUOTE || view.nargs != 1u) return name;
        inner = rho_view(view.args[0]);
        if (inner.kind != RHO_DROP || inner.nargs != 1u) return name;
        name = inner.args[0];
    }
}

/* A collapsed name's rank; a quotation also gives the process it quotes. */
static int rho_name_rank(Atom *name, Atom **quoted) {
    RhoView view;
    *quoted = NULL;
    if (!name) return RHO_NAME_RANK_BAD;
    if (name->kind == ATOM_VAR) return RHO_NAME_RANK_VAR;
    view = rho_view(name);
    if (view.kind == RHO_QUOTE && view.nargs == 1u) {
        *quoted = view.args[0];
        return RHO_NAME_RANK_QUOTE;
    }
    return RHO_NAME_RANK_BAD;
}

/* The binders a process opens outside quotations: each receive, its body's,
 * a sent process's, and every component's. */
static uint32_t rho_proc_binder_count(Atom *proc) {
    RhoView view = rho_view(proc);
    uint32_t count = 0u;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_RHO_ORDER_VISIT);
    switch (rho_proc_rank(&view)) {
    case RHO_RANK_PAR:
        for (uint32_t i = 0u; i < view.nargs; i++)
            count += rho_proc_binder_count(view.args[i]);
        return count;
    case RHO_RANK_SEND:
        return rho_proc_binder_count(view.args[1]);
    case RHO_RANK_RECV:
    case RHO_RANK_RECV_PERSISTENT:
        return 1u + rho_proc_binder_count(view.args[2]);
    default:
        return 0u;
    }
}

/* The decimal digits of v, written to end before `end`. */
static char *rho_decimal(uint64_t v, char *end) {
    do {
        *--end = (char)('0' + (int)(v % 10u));
        v /= 10u;
    } while (v != 0u);
    return end;
}

/* Text a ended by the mark a_end against text b ended by b_end: bytes in
 * turn; where one text runs out, its mark against the other's next byte,
 * the mark first when they are the same value; equal texts by their marks. */
static int rho_text_order(const char *a, size_t a_len, char a_end,
                          const char *b, size_t b_len, char b_end) {
    size_t common = a_len < b_len ? a_len : b_len;
    int c = common > 0u ? memcmp(a, b, common) : 0;
    if (c != 0) return c < 0 ? -1 : 1;
    if (a_len == b_len) {
        if (a_end == b_end) return 0;
        return (unsigned char)a_end < (unsigned char)b_end ? -1 : 1;
    }
    if (a_len < b_len)
        return (unsigned char)a_end <= (unsigned char)b[a_len] ? -1 : 1;
    return (unsigned char)b_end <= (unsigned char)a[b_len] ? 1 : -1;
}

/* ── The binders in view of a comparison ── */

typedef struct {
    VarId var[2];          /* the variable each side binds */
    uint32_t number;
    bool aligned_before;
} RhoOrderBinder;

/* Two terms walked in step.  Both sides meet the same binders until the
 * first difference, so one number names a binder on either side. */
typedef struct {
    RhoOrderBinder *items;
    uint32_t len;
    uint32_t cap;
    uint32_t base;          /* binders below are outside the current quotation */
    uint32_t next;          /* the number the next binder takes */
    uint32_t quote_depth;
    bool aligned;           /* every binder in view binds one variable on both sides */
    /* Identical components of an outermost rho:par are passed over without a
     * walk; the binders they open are counted only if a bound variable's
     * number must be written out, which every later number includes. */
    Atom **passed;
    uint32_t passed_len;
    uint32_t passed_cap;
    uint32_t passed_counted;
    uint32_t passed_binders;
    RhoOrderBinder inline_items[16];
    Atom *inline_passed[16];
} RhoOrder;

static void rho_order_init(RhoOrder *o) {
    o->items = o->inline_items;
    o->len = 0u;
    o->cap = (uint32_t)(sizeof(o->inline_items) / sizeof(o->inline_items[0]));
    o->base = 0u;
    o->next = 0u;
    o->quote_depth = 0u;
    o->aligned = true;
    o->passed = o->inline_passed;
    o->passed_len = 0u;
    o->passed_cap = (uint32_t)(sizeof(o->inline_passed) / sizeof(o->inline_passed[0]));
    o->passed_counted = 0u;
    o->passed_binders = 0u;
}

static void rho_order_free(RhoOrder *o) {
    if (o->items != o->inline_items) free(o->items);
    if (o->passed != o->inline_passed) free(o->passed);
}

/* Grows an inline-backed array of `count` elements of `size` bytes. */
static void *rho_order_grow(void *items, void *inline_items, uint32_t *cap,
                            size_t size) {
    uint32_t next_cap = *cap * 2u;
    void *next;
    if (next_cap < *cap) cetta_oom(SIZE_MAX);
    if (items == inline_items) {
        next = cetta_malloc((size_t)next_cap * size);
        memcpy(next, items, (size_t)*cap * size);
    } else {
        next = cetta_realloc(items, (size_t)next_cap * size);
    }
    *cap = next_cap;
    return next;
}

static void rho_order_push(RhoOrder *o, VarId left, VarId right) {
    if (o->len == o->cap)
        o->items = rho_order_grow(o->items, o->inline_items, &o->cap,
                                  sizeof(*o->items));
    o->items[o->len++] = (RhoOrderBinder){
        .var = {left, right},
        .number = o->next++,
        .aligned_before = o->aligned,
    };
    o->aligned = o->aligned && var_id_eq(left, right);
}

static void rho_order_pop(RhoOrder *o) {
    o->aligned = o->items[--o->len].aligned_before;
}

static bool rho_order_lookup(const RhoOrder *o, int side, VarId var,
                             uint32_t *out_number) {
    for (uint32_t i = o->len; i > o->base; i--) {
        if (var_id_eq(o->items[i - 1u].var[side], var)) {
            *out_number = o->items[i - 1u].number;
            return true;
        }
    }
    return false;
}

/* Identical components are equal; the binders they open still advance the
 * numbering.  At the outermost level those are counted only on demand. */
static void rho_order_pass(RhoOrder *o, Atom *proc) {
    if (o->len == 0u && o->quote_depth == 0u) {
        if (o->passed_len == o->passed_cap)
            o->passed = rho_order_grow(o->passed, o->inline_passed,
                                       &o->passed_cap, sizeof(*o->passed));
        o->passed[o->passed_len++] = proc;
        return;
    }
    o->next += rho_proc_binder_count(proc);
}

/* A bound variable's written number. */
static uint64_t rho_order_written_number(RhoOrder *o, uint32_t number) {
    if (o->quote_depth > 0u) return number;
    while (o->passed_counted < o->passed_len)
        o->passed_binders +=
            rho_proc_binder_count(o->passed[o->passed_counted++]);
    return (uint64_t)number + o->passed_binders;
}

/* ── Leaves of payload atoms ── */

/* A leaf's text, as up to three byte runs, and its kind.  A string written
 * inside an expression shows its quotes and escapes; a payload that is a
 * string shows its bytes. */
typedef struct {
    const char *seg[3];
    size_t len[3];
    uint8_t nseg;
    uint8_t escaped;        /* the runs written with string escapes */
    uint32_t kind;
    Atom *spelling;         /* a variable's structural spelling, after its text */
    char local[80];
    char *owned;
} RhoLeaf;

static void rho_leaf_run(RhoLeaf *leaf, const char *text, size_t len,
                         bool escaped) {
    leaf->seg[leaf->nseg] = text ? text : "";
    leaf->len[leaf->nseg] = text ? len : 0u;
    if (escaped) leaf->escaped |= (uint8_t)(1u << leaf->nseg);
    leaf->nseg++;
}

static void rho_leaf_cstr(RhoLeaf *leaf, const char *text) {
    rho_leaf_run(leaf, text, text ? strlen(text) : 0u, false);
}

static void rho_leaf_format(RhoLeaf *leaf, const char *fmt, ...) {
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(leaf->local, sizeof(leaf->local), fmt, ap);
    va_end(ap);
    if (n < 0) n = 0;
    if ((size_t)n >= sizeof(leaf->local)) n = (int)sizeof(leaf->local) - 1;
    rho_leaf_run(leaf, leaf->local, (size_t)n, false);
}

/* A display text that may exceed the local buffer: `display` writes at most
 * `capacity` bytes and answers the full length. */
static void rho_leaf_display(RhoLeaf *leaf, const Atom *atom,
                             size_t (*display)(const Atom *, char *, size_t)) {
    size_t n = display(atom, leaf->local, sizeof(leaf->local));
    if (n < sizeof(leaf->local)) {
        rho_leaf_run(leaf, leaf->local, n, false);
        return;
    }
    leaf->owned = cetta_malloc(n + 1u);
    n = display(atom, leaf->owned, n + 1u);
    rho_leaf_run(leaf, leaf->owned, n, false);
}

static void rho_leaf_free(RhoLeaf *leaf) {
    free(leaf->owned);
    leaf->owned = NULL;
}

static void rho_leaf_literal(RhoLeaf *leaf, const char *text) {
    leaf->nseg = 0u;
    leaf->escaped = 0u;
    leaf->owned = NULL;
    leaf->spelling = NULL;
    leaf->kind = (uint32_t)ATOM_SYMBOL << 8;
    rho_leaf_cstr(leaf, text);
}

/* The leaf an atom that is no expression and no state cell writes. */
static void rho_leaf_init(RhoLeaf *leaf, Atom *atom, bool root) {
    leaf->nseg = 0u;
    leaf->escaped = 0u;
    leaf->owned = NULL;
    leaf->spelling = NULL;
    leaf->kind = (uint32_t)atom->kind << 8;
    switch (atom->kind) {
    case ATOM_SYMBOL:
        rho_leaf_cstr(leaf, atom_name_cstr(atom));
        return;
    case ATOM_VAR: {
        CettaFrameIdentity epoch = var_epoch_suffix(atom->var_id);
        bool slot = atom->name_key &&
            atom->name_key->kind == ATOM_GROUNDED &&
            atom->name_key->ground.gkind == GV_INTERNAL_TAG &&
            atom->name_key->ground.ival ==
                (int64_t)CETTA_INTERNAL_TAG_PRIME_LEXICAL_SLOT;
        char *end = leaf->local + sizeof(leaf->local);
        char *digits = end;
        if (!cetta_frame_identity_is_none(epoch)) {
            digits = rho_decimal(epoch.word, end);
            *--digits = '#';
        }
        if (atom->name_key && !slot) {
            rho_leaf_run(leaf, "$@", 2u, false);
            rho_leaf_run(leaf, digits, (size_t)(end - digits), false);
            leaf->spelling = atom->name_key;
            return;
        }
        rho_leaf_run(leaf, "$", 1u, false);
        if (slot) {
            rho_leaf_cstr(leaf, atom->sym_id == SYMBOL_ID_NONE
                ? "__prime_let" : symbol_bytes(g_symbols, atom->sym_id));
        } else {
            rho_leaf_cstr(leaf, atom_name_cstr(atom));
        }
        rho_leaf_run(leaf, digits, (size_t)(end - digits), false);
        return;
    }
    case ATOM_EXPR:
        return;
    case ATOM_GROUNDED:
        break;
    }
    leaf->kind |= (uint32_t)atom->ground.gkind;
    switch (atom->ground.gkind) {
    case GV_INT: {
        char *end = leaf->local + sizeof(leaf->local);
        int64_t v = atom->ground.ival;
        char *digits = rho_decimal(v < 0 ? 0u - (uint64_t)v : (uint64_t)v, end);
        if (v < 0) *--digits = '-';
        rho_leaf_run(leaf, digits, (size_t)(end - digits), false);
        return;
    }
    case GV_FLOAT: {
        int written = cetta_format_float(leaf->local, sizeof(leaf->local),
                                         atom->ground.fval);
        if (written < 0) written = 0;
        if ((size_t)written >= sizeof(leaf->local))
            written = (int)sizeof(leaf->local) - 1;
        rho_leaf_run(leaf, leaf->local, (size_t)written, false);
        return;
    }
    case GV_BOOL:
        rho_leaf_cstr(leaf, atom->ground.bval ? "True" : "False");
        return;
    case GV_STRING:
        if (root) {
            rho_leaf_cstr(leaf, atom->ground.sval);
        } else {
            rho_leaf_run(leaf, "\"", 1u, false);
            rho_leaf_run(leaf, atom->ground.sval,
                         atom->ground.sval ? strlen(atom->ground.sval) : 0u,
                         true);
            rho_leaf_run(leaf, "\"", 1u, false);
        }
        return;
    case GV_BIGINT:
        rho_leaf_cstr(leaf, atom_bigint_cstr(atom));
        return;
    case GV_RATIONAL:
        rho_leaf_cstr(leaf, atom_rational_cstr(atom));
        return;
    case GV_SPACE:
        rho_leaf_format(leaf, "<space %p>", atom->ground.ptr);
        return;
    case GV_BINDINGS:
        rho_leaf_format(leaf, "<bindings %p>", atom->ground.ptr);
        return;
    case GV_FOREIGN:
        rho_leaf_format(leaf, "<foreign %p>", atom->ground.ptr);
        return;
    case GV_TERM_GRAPH:
        rho_leaf_format(leaf, "<term-graph %p>", atom->ground.ptr);
        return;
    case GV_CAPTURE:
        rho_leaf_cstr(leaf, "capture");
        return;
    case GV_STRUCTURAL:
        rho_leaf_display(leaf, atom, atom_structural_display);
        return;
    case GV_SEAL:
        rho_leaf_display(leaf, atom, atom_seal_display_text);
        return;
    case GV_PRIME_NEED_CAPABILITY:
        rho_leaf_cstr(leaf, "<prime-need-capability>");
        return;
    case GV_PRIME_CONTEXT:
        rho_leaf_format(leaf, "<context %u>",
                        atom_prime_context_depth(atom->ground.prime_context));
        return;
    case GV_INTERNAL_TAG:
        rho_leaf_format(leaf, "<internal-tag %ld>", (long)atom->ground.ival);
        return;
    case GV_STATE:
        return;
    }
}

typedef struct {
    const RhoLeaf *leaf;
    uint8_t seg;
    size_t pos;
    char pending;
} RhoLeafCursor;

/* The leaf text's next byte, or -1 at its end. */
static int rho_leaf_next(RhoLeafCursor *c) {
    if (c->pending) {
        char pending = c->pending;
        c->pending = 0;
        return (unsigned char)pending;
    }
    while (c->seg < c->leaf->nseg) {
        if (c->pos < c->leaf->len[c->seg]) {
            unsigned char ch =
                (unsigned char)c->leaf->seg[c->seg][c->pos++];
            if (c->leaf->escaped & (1u << c->seg)) {
                if (ch == '\n') { c->pending = 'n'; return '\\'; }
                if (ch == '"') { c->pending = '"'; return '\\'; }
                if (ch == '\\') { c->pending = '\\'; return '\\'; }
            }
            return ch;
        }
        c->seg++;
        c->pos = 0u;
    }
    return -1;
}

static int rho_order_atom(Atom *a, char a_end, Atom *b, char b_end,
                          bool root);

/* Leaf texts with their marks, then kinds, then structural spellings. */
static int rho_leaf_order(const RhoLeaf *a, char a_end,
                          const RhoLeaf *b, char b_end) {
    int c;
    if (a->nseg == 1u && b->nseg == 1u && !a->escaped && !b->escaped) {
        c = rho_text_order(a->seg[0], a->len[0], a_end,
                           b->seg[0], b->len[0], b_end);
    } else {
        RhoLeafCursor ca = {.leaf = a};
        RhoLeafCursor cb = {.leaf = b};
        for (;;) {
            int x = rho_leaf_next(&ca);
            int y = rho_leaf_next(&cb);
            if (x < 0 && y < 0) {
                c = a_end == b_end ? 0
                    : (unsigned char)a_end < (unsigned char)b_end ? -1 : 1;
                break;
            }
            if (x < 0) {
                c = (unsigned char)a_end <= y ? -1 : 1;
                break;
            }
            if (y < 0) {
                c = (unsigned char)b_end <= x ? 1 : -1;
                break;
            }
            if (x != y) {
                c = x < y ? -1 : 1;
                break;
            }
        }
    }
    if (c != 0) return c;
    if (a->kind != b->kind) return a->kind < b->kind ? -1 : 1;
    if (a->spelling || b->spelling) {
        if (!a->spelling) return -1;
        if (!b->spelling) return 1;
        return rho_order_atom(a->spelling, ')', b->spelling, ')', false);
    }
    return 0;
}

/* A node, which written opens with the mark '(', against a leaf. */
static int rho_node_against_leaf(const RhoLeaf *leaf, char leaf_end) {
    RhoLeafCursor c = {.leaf = leaf};
    int y = rho_leaf_next(&c);
    if (y < 0) return (unsigned char)'(' < (unsigned char)leaf_end ? -1 : 1;
    return '(' <= y ? -1 : 1;
}

/* ── Payload atoms ── */

/* An expression, or a state cell read as the expression (State value). */
typedef struct {
    Atom **elems;
    CettaExprIndex len;
    bool state;
    StateCell *cell;
    RhoStateObservation observation;
    bool observed;
    Atom *value;            /* the state's value read through the snapshot */
} RhoNode;

static bool rho_atom_is_node(const Atom *atom) {
    return atom->kind == ATOM_EXPR ||
           (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_STATE);
}

/* Opens a node; a state cell is read through a pinned snapshot held until
 * the node closes. */
static void rho_node_open(RhoNode *node, Atom *atom) {
    *node = (RhoNode){0};
    if (atom->kind == ATOM_EXPR) {
        node->elems = atom->expr.elems;
        node->len = atom->expr.len;
        return;
    }
    node->state = true;
    node->cell = (StateCell *)atom->ground.ptr;
    node->len = 1u;
    if (node->cell &&
        rho_state_observation_acquire(node->cell, &node->observation)) {
        StateCellSnapshot view =
            rho_state_observation_view(node->cell, &node->observation);
        node->observed = true;
        if (view.value) {
            node->value = view.value;
            node->len = 2u;
        }
#ifdef CETTA_TEST_HOOKS
        if (rho_state_observed_test) rho_state_observed_test(node->cell);
#endif
    }
}

static void rho_node_close(RhoNode *node) {
    if (node->observed) rho_state_observation_release(&node->observation);
    node->observed = false;
}

/* The node's i-th element: an atom, or NULL for a state's written head. */
static Atom *rho_node_child(RhoNode *node, CettaExprIndex i) {
    if (!node->state) return node->elems[i];
    return i == 0u ? NULL : node->value;
}

static int rho_marks_order(char a_end, char b_end) {
    if (a_end == b_end) return 0;
    return (unsigned char)a_end < (unsigned char)b_end ? -1 : 1;
}

/* An element against an element: either may be a state's written head. */
static int rho_order_element(Atom *a, char a_end, Atom *b, char b_end,
                             bool *descend) {
    RhoLeaf la;
    RhoLeaf lb;
    int c;
    bool a_node = a && rho_atom_is_node(a);
    bool b_node = b && rho_atom_is_node(b);
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_RHO_ORDER_VISIT);
    *descend = false;
    if (a && b && (a == b ||
                   (a->kind == ATOM_SYMBOL && b->kind == ATOM_SYMBOL &&
                    a->sym_id == b->sym_id) ||
                   (a->kind == ATOM_GROUNDED && b->kind == ATOM_GROUNDED &&
                    a->ground.gkind == GV_INT && b->ground.gkind == GV_INT &&
                    a->ground.ival == b->ground.ival))) {
        return rho_marks_order(a_end, b_end);
    }
    if (a_node && b_node) {
        *descend = true;
        return 0;
    }
    if (a_node || b_node) {
        RhoLeaf *leaf = a_node ? &lb : &la;
        Atom *atom = a_node ? b : a;
        if (atom) rho_leaf_init(leaf, atom, false);
        else rho_leaf_literal(leaf, "State");
        c = rho_node_against_leaf(leaf, a_node ? b_end : a_end);
        rho_leaf_free(leaf);
        return a_node ? c : -c;
    }
    if (a) rho_leaf_init(&la, a, false);
    else rho_leaf_literal(&la, "State");
    if (b) rho_leaf_init(&lb, b, false);
    else rho_leaf_literal(&lb, "State");
    c = rho_leaf_order(&la, a_end, &lb, b_end);
    rho_leaf_free(&la);
    rho_leaf_free(&lb);
    return c;
}

typedef struct {
    RhoNode a;
    RhoNode b;
    char a_end;
    char b_end;
    CettaExprIndex at;
} RhoAtomOrderFrame;

/* The first symbol of an element written with its mark: '(' for a node, a
 * leaf's first byte, or its mark when its text is empty.  *is_mark tells a
 * mark from a byte. */
static int rho_element_first(Atom *atom, char end, bool *is_mark) {
    RhoLeaf leaf;
    RhoLeafCursor c;
    int first;
    if (atom && rho_atom_is_node(atom)) {
        *is_mark = true;
        return '(';
    }
    if (atom) rho_leaf_init(&leaf, atom, false);
    else rho_leaf_literal(&leaf, "State");
    c = (RhoLeafCursor){.leaf = &leaf};
    first = rho_leaf_next(&c);
    rho_leaf_free(&leaf);
    *is_mark = first < 0;
    return first < 0 ? (unsigned char)end : first;
}

/* An empty node "()" against a node with elements: the empty one's closing
 * mark against the other's first element's first symbol. */
static int rho_empty_node_order(RhoNode *full, char full_first_end) {
    bool is_mark = false;
    int first = rho_element_first(rho_node_child(full, 0u), full_first_end,
                                  &is_mark);
    if (first == ')' && is_mark) return 0;
    if ((unsigned char)')' < first) return -1;
    if ((unsigned char)')' > first) return 1;
    return is_mark ? 1 : -1;
}

/* Two payload atoms, each followed by its mark; `root` marks the payload
 * itself, whose string is written without quotes.  Walks with an explicit
 * stack: payloads nest deeply. */
static int rho_order_atom(Atom *a, char a_end, Atom *b, char b_end,
                          bool root) {
    RhoAtomOrderFrame inline_frames[16];
    RhoAtomOrderFrame *frames = inline_frames;
    uint32_t depth = 0u;
    uint32_t cap = (uint32_t)(sizeof(inline_frames) / sizeof(inline_frames[0]));
    int result = 0;

    if (a == b) return rho_marks_order(a_end, b_end);
    if (root && (!rho_atom_is_node(a) || !rho_atom_is_node(b))) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_RHO_ORDER_VISIT);
        RhoLeaf la;
        RhoLeaf lb;
        bool a_node = rho_atom_is_node(a);
        bool b_node = rho_atom_is_node(b);
        if (a_node || b_node) {
            RhoLeaf *leaf = a_node ? &lb : &la;
            rho_leaf_init(leaf, a_node ? b : a, true);
            result = rho_node_against_leaf(leaf, a_node ? b_end : a_end);
            rho_leaf_free(leaf);
            return a_node ? result : -result;
        }
        rho_leaf_init(&la, a, true);
        rho_leaf_init(&lb, b, true);
        result = rho_leaf_order(&la, a_end, &lb, b_end);
        rho_leaf_free(&la);
        rho_leaf_free(&lb);
        return result;
    }
    {
        bool descend = false;
        result = rho_order_element(a, a_end, b, b_end, &descend);
        if (!descend) return result;
    }
    for (;;) {
        /* a and b are nodes to open. */
        if (depth == cap)
            frames = rho_order_grow(frames, inline_frames, &cap, sizeof(*frames));
        {
            RhoAtomOrderFrame *f = &frames[depth++];
            rho_node_open(&f->a, a);
            rho_node_open(&f->b, b);
            f->a_end = a_end;
            f->b_end = b_end;
            f->at = 0u;
            if (f->a.len == 0u || f->b.len == 0u) {
                if (f->a.len != f->b.len) {
                    result = f->a.len == 0u
                        ? rho_empty_node_order(&f->b, f->b.len == 1u ? ')' : ' ')
                        : -rho_empty_node_order(&f->a, f->a.len == 1u ? ')' : ' ');
                    if (result != 0) goto done;
                    result = f->a.len < f->b.len ? -1 : 1;
                    goto done;
                }
                f->at = 0u;
            }
        }
        /* Compare elements of the innermost frame until one descends. */
        for (;;) {
            RhoAtomOrderFrame *f = &frames[depth - 1u];
            if (f->at < f->a.len && f->at < f->b.len) {
                CettaExprIndex i = f->at++;
                char ea = i + 1u < f->a.len ? ' ' : ')';
                char eb = i + 1u < f->b.len ? ' ' : ')';
                Atom *ca = rho_node_child(&f->a, i);
                Atom *cb = rho_node_child(&f->b, i);
                bool descend = false;
                result = rho_order_element(ca, ea, cb, eb, &descend);
                if (result != 0) goto done;
                if (descend) {
                    a = ca;
                    b = cb;
                    a_end = ea;
                    b_end = eb;
                    break;
                }
                continue;
            }
            if (f->a.len != f->b.len) {
                result = f->a.len < f->b.len ? -1 : 1;
                goto done;
            }
            result = rho_marks_order(f->a_end, f->b_end);
            if (result == 0 && f->a.state != f->b.state)
                result = f->a.state ? 1 : -1;
            if (result != 0) goto done;
            rho_node_close(&f->a);
            rho_node_close(&f->b);
            if (--depth == 0u) goto done;
        }
    }

done:
    while (depth > 0u) {
        depth--;
        rho_node_close(&frames[depth].a);
        rho_node_close(&frames[depth].b);
    }
    if (frames != inline_frames) free(frames);
    return result;
}

/* ── Names and processes ── */

static int rho_order_proc(RhoOrder *o, Atom *a, Atom *b);

/* A variable in name position is written "$#" and its binder's number when
 * a binder in view binds it, else "$", its spelling, ":", its base and ":"
 * its epoch. */
typedef struct {
    char local[112];
    char *owned;
    const char *text;
    size_t len;
} RhoVarText;

static void rho_var_text(RhoOrder *o, int side, Atom *var, RhoVarText *t) {
    uint32_t number = 0u;
    char *end = t->local + sizeof(t->local);
    t->owned = NULL;
    if (rho_order_lookup(o, side, var->var_id, &number)) {
        char *digits = rho_decimal(rho_order_written_number(o, number), end);
        *--digits = '#';
        *--digits = '$';
        t->text = digits;
        t->len = (size_t)(end - digits);
        return;
    }
    {
        const char *name = atom_name_cstr(var);
        size_t name_len = strlen(name);
        char tail[48];
        char *tail_end = tail + sizeof(tail);
        char *p = rho_decimal(var->var_id.epoch.word, tail_end);
        size_t tail_len;
        *--p = ':';
        p = rho_decimal(var->var_id.base, p);
        *--p = ':';
        tail_len = (size_t)(tail_end - p);
        char *out = 1u + name_len + tail_len <= sizeof(t->local)
            ? t->local : (t->owned = cetta_malloc(1u + name_len + tail_len));
        out[0] = '$';
        memcpy(out + 1u, name, name_len);
        memcpy(out + 1u + name_len, p, tail_len);
        t->text = out;
        t->len = 1u + name_len + tail_len;
    }
}

static int rho_order_name_var(RhoOrder *o, Atom *a, Atom *b, char end) {
    uint32_t na = 0u;
    uint32_t nb = 0u;
    bool bound_a = rho_order_lookup(o, 0, a->var_id, &na);
    bool bound_b = rho_order_lookup(o, 1, b->var_id, &nb);
    RhoVarText ta;
    RhoVarText tb;
    int c;
    if (bound_a && bound_b && na == nb) return 0;
    if (!bound_a && !bound_b && var_id_eq(a->var_id, b->var_id) &&
        a->sym_id == b->sym_id && !a->name_key == !b->name_key) {
        return 0;
    }
    rho_var_text(o, 0, a, &ta);
    rho_var_text(o, 1, b, &tb);
    c = rho_text_order(ta.text, ta.len, end, tb.text, tb.len, end);
    free(ta.owned);
    free(tb.owned);
    return c;
}

/* Names, each followed by the mark `end` that closes it where it stands. */
static int rho_order_name(RhoOrder *o, Atom *a, Atom *b, char end) {
    Atom *qa = NULL;
    Atom *qb = NULL;
    int ra;
    int rb;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_RHO_ORDER_VISIT);
    a = rho_name_collapse(a);
    b = rho_name_collapse(b);
    ra = rho_name_rank(a, &qa);
    rb = rho_name_rank(b, &qb);
    if (ra != rb) return ra < rb ? -1 : 1;
    if (ra == RHO_NAME_RANK_VAR) return rho_order_name_var(o, a, b, end);
    if (ra == RHO_NAME_RANK_BAD || qa == qb) return 0;
    {
        /* A quotation seals: its binders number from zero and the binders
         * outside it are out of view, so identical quotations are equal. */
        uint32_t base = o->base;
        uint32_t next = o->next;
        bool aligned = o->aligned;
        int c;
        o->base = o->len;
        o->next = 0u;
        o->aligned = true;
        o->quote_depth++;
        c = rho_order_proc(o, qa, qb);
        o->quote_depth--;
        o->base = base;
        o->next = next;
        o->aligned = aligned;
        return c;
    }
}

/* Components in turn, the shorter sequence first. */
static int rho_order_items(RhoOrder *o, Atom *const *a, uint32_t na,
                           Atom *const *b, uint32_t nb) {
    uint32_t common = na < nb ? na : nb;
    for (uint32_t i = 0u; i < common; i++) {
        int c = rho_order_proc(o, a[i], b[i]);
        if (c != 0) return c;
    }
    return na < nb ? -1 : na > nb ? 1 : 0;
}

static int rho_order_proc(RhoOrder *o, Atom *a, Atom *b) {
    RhoView va;
    RhoView vb;
    int ra;
    int rb;
    int c;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_RHO_ORDER_VISIT);
    if (a == b && (o->aligned || !atom_has_vars(a))) {
        rho_order_pass(o, a);
        return 0;
    }
    va = rho_view(a);
    vb = rho_view(b);
    ra = rho_proc_rank(&va);
    rb = rho_proc_rank(&vb);
    if (ra != rb) return ra < rb ? -1 : 1;
    switch (ra) {
    case RHO_RANK_PAR:
        return rho_order_items(o, va.args, va.nargs, vb.args, vb.nargs);
    case RHO_RANK_SEND:
        c = rho_order_name(o, va.args[0], vb.args[0], ',');
        return c != 0 ? c : rho_order_proc(o, va.args[1], vb.args[1]);
    case RHO_RANK_RECV:
    case RHO_RANK_RECV_PERSISTENT:
        c = rho_order_name(o, va.args[0], vb.args[0], ',');
        if (c != 0) return c;
        rho_order_push(o, va.args[1]->var_id, vb.args[1]->var_id);
        c = rho_order_proc(o, va.args[2], vb.args[2]);
        rho_order_pop(o);
        return c;
    case RHO_RANK_DROP:
        return rho_order_name(o, va.args[0], vb.args[0], ')');
    case RHO_RANK_VAL:
    case RHO_RANK_DEFER:
        return rho_order_atom(va.args[0], ')', vb.args[0], ')', true);
    default:
        return 0;
    }
}

/* Two processes, each numbering its binders from zero. */
static int rho_proc_order(Atom *a, Atom *b) {
    RhoOrder o;
    int c;
    if (a == b) return 0;
    rho_order_init(&o);
    c = rho_order_proc(&o, a, b);
    rho_order_free(&o);
    return c;
}

/* Two flat component sequences as the processes they compose: none is 0,
 * one is that component, more are their rho:par. */
static int rho_items_order(Atom *const *a, uint32_t na,
                           Atom *const *b, uint32_t nb) {
    RhoOrder o;
    int c;
    if (na <= 1u || nb <= 1u) {
        RhoView va = {RHO_PAR, NULL, 0u};
        RhoView vb = {RHO_PAR, NULL, 0u};
        int ra;
        int rb;
        if (na == 1u) va = rho_view(a[0]);
        else if (na == 0u) va.kind = RHO_NIL;
        if (nb == 1u) vb = rho_view(b[0]);
        else if (nb == 0u) vb.kind = RHO_NIL;
        ra = rho_proc_rank(&va);
        rb = rho_proc_rank(&vb);
        if (ra != rb) return ra < rb ? -1 : 1;
        if (na == 0u || nb == 0u) return 0;
        if (na == 1u && nb == 1u) return rho_proc_order(a[0], b[0]);
    }
    rho_order_init(&o);
    c = rho_order_items(&o, a, na, b, nb);
    rho_order_free(&o);
    return c;
}

/* Two names standing alone, as an endpoint's channel. */
static int rho_name_order(Atom *a, Atom *b) {
    RhoOrder o;
    int c;
    if (a == b) return 0;
    rho_order_init(&o);
    c = rho_order_name(&o, a, b, '\0');
    rho_order_free(&o);
    return c;
}

static bool rho_names_equal(Atom *a, Atom *b) {
    return rho_name_order(a, b) == 0;
}

/* ── Hash ── */

static void rho_hash_memo_free(RhoHashMemo *memo) {
    free(memo->keys);
    free(memo->values);
    *memo = (RhoHashMemo){0};
}

static size_t rho_pointer_slot(const void *pointer, size_t capacity) {
    uint64_t h = (uint64_t)(uintptr_t)pointer;
    h ^= h >> 33;
    h *= UINT64_C(0xff51afd7ed558ccd);
    h ^= h >> 33;
    return (size_t)h & (capacity - 1u);
}

static bool rho_hash_memo_get(const RhoHashMemo *memo, const Atom *atom,
                              uint64_t *out) {
    size_t slot;
    if (!memo || memo->cap == 0u) return false;
    slot = rho_pointer_slot(atom, memo->cap);
    while (memo->keys[slot]) {
        if (memo->keys[slot] == atom) {
            *out = memo->values[slot];
            return true;
        }
        slot = (slot + 1u) & (memo->cap - 1u);
    }
    return false;
}

static void rho_hash_memo_put(RhoHashMemo *memo, const Atom *atom,
                              uint64_t value) {
    size_t slot;
    if (!memo) return;
    if ((memo->len + 1u) * 4u >= memo->cap * 3u) {
        size_t cap = memo->cap ? memo->cap * 2u : 256u;
        const Atom **keys = cetta_malloc(cap * sizeof(*keys));
        uint64_t *values = cetta_malloc(cap * sizeof(*values));
        memset(keys, 0, cap * sizeof(*keys));
        for (size_t i = 0u; i < memo->cap; i++) {
            if (!memo->keys[i]) continue;
            slot = rho_pointer_slot(memo->keys[i], cap);
            while (keys[slot]) slot = (slot + 1u) & (cap - 1u);
            keys[slot] = memo->keys[i];
            values[slot] = memo->values[i];
        }
        free(memo->keys);
        free(memo->values);
        memo->keys = keys;
        memo->values = values;
        memo->cap = cap;
    }
    slot = rho_pointer_slot(atom, memo->cap);
    while (memo->keys[slot]) {
        if (memo->keys[slot] == atom) return;
        slot = (slot + 1u) & (memo->cap - 1u);
    }
    memo->keys[slot] = atom;
    memo->values[slot] = value;
    memo->len++;
}

static uint64_t rho_hash_mix(uint64_t h, uint64_t v) {
    h ^= v + UINT64_C(0x9e3779b97f4a7c15) + (h << 6) + (h >> 2);
    return h;
}

enum {
    RHO_HASH_EXPR = 0x45,
    RHO_HASH_STATE = 0x53,
    RHO_HASH_BOUND = 0x42,
    RHO_HASH_FREE = 0x46,
    RHO_HASH_QUOTE = 0x51,
};

static uint64_t rho_hash_atom(Atom *atom, bool root, RhoHashMemo *memo);

static uint64_t rho_hash_leaf(Atom *atom, bool root, RhoHashMemo *memo) {
    RhoLeaf leaf;
    RhoLeafCursor c;
    uint64_t h = UINT64_C(0xcbf29ce484222325);
    int byte;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_RHO_HASH_VISIT);
    rho_leaf_init(&leaf, atom, root);
    c = (RhoLeafCursor){.leaf = &leaf};
    while ((byte = rho_leaf_next(&c)) >= 0) {
        h ^= (uint64_t)byte;
        h *= UINT64_C(0x100000001b3);
    }
    h = rho_hash_mix(h, leaf.kind);
    if (leaf.spelling) h = rho_hash_mix(h, rho_hash_atom(leaf.spelling, false, memo));
    rho_leaf_free(&leaf);
    return h;
}

typedef struct {
    Atom *node;
    CettaExprIndex next;
    uint64_t hash;
} RhoHashFrame;

/* A payload atom's hash: its leaves' texts and kinds, its expressions'
 * shapes; a state cell hashes as every state cell does, since its value may
 * change.  Folds with an explicit stack. */
static uint64_t rho_hash_atom(Atom *atom, bool root, RhoHashMemo *memo) {
    RhoHashFrame inline_frames[16];
    RhoHashFrame *frames = inline_frames;
    uint32_t cap = (uint32_t)(sizeof(inline_frames) / sizeof(inline_frames[0]));
    uint32_t depth = 0u;
    uint64_t result;

    if (atom->kind != ATOM_EXPR) {
        if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_STATE)
            return RHO_HASH_STATE;
        return rho_hash_leaf(atom, root, memo);
    }
    if (rho_hash_memo_get(memo, atom, &result)) return result;
    frames[depth++] = (RhoHashFrame){atom, 0u,
        rho_hash_mix(RHO_HASH_EXPR, (uint64_t)atom->expr.len)};
    for (;;) {
        RhoHashFrame *f = &frames[depth - 1u];
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_RHO_HASH_VISIT);
        if (f->next < f->node->expr.len) {
            Atom *child = f->node->expr.elems[f->next++];
            uint64_t h;
            if (child->kind != ATOM_EXPR) {
                h = child->kind == ATOM_GROUNDED &&
                        child->ground.gkind == GV_STATE
                    ? RHO_HASH_STATE : rho_hash_leaf(child, false, memo);
            } else if (!rho_hash_memo_get(memo, child, &h)) {
                if (depth == cap)
                    frames = rho_order_grow(frames, inline_frames, &cap,
                                            sizeof(*frames));
                frames[depth++] = (RhoHashFrame){child, 0u,
                    rho_hash_mix(RHO_HASH_EXPR, (uint64_t)child->expr.len)};
                continue;
            }
            f->hash = rho_hash_mix(f->hash, h);
            continue;
        }
        result = f->hash;
        rho_hash_memo_put(memo, f->node, result);
        if (--depth == 0u) break;
        frames[depth - 1u].hash = rho_hash_mix(frames[depth - 1u].hash, result);
    }
    if (frames != inline_frames) free(frames);
    return result;
}

/* The binders in view while hashing, numbered from where hashing starts. */
typedef struct {
    VarId *vars;
    uint32_t *numbers;
    uint32_t len;
    uint32_t cap;
    uint32_t base;
    uint32_t next;
    RhoHashMemo *memo;
} RhoHashEnv;

static uint64_t rho_hash_proc_in(RhoHashEnv *env, Atom *proc);

static uint64_t rho_hash_name_in(RhoHashEnv *env, Atom *name) {
    Atom *quoted = NULL;
    int rank;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_RHO_HASH_VISIT);
    name = rho_name_collapse(name);
    rank = rho_name_rank(name, &quoted);
    if (rank == RHO_NAME_RANK_VAR) {
        for (uint32_t i = env->len; i > env->base; i--) {
            if (var_id_eq(env->vars[i - 1u], name->var_id))
                return rho_hash_mix(RHO_HASH_BOUND, env->numbers[i - 1u]);
        }
        {
            const char *spelling = atom_name_cstr(name);
            uint64_t h = RHO_HASH_FREE;
            for (const char *p = spelling; *p; p++)
                h = rho_hash_mix(h, (unsigned char)*p);
            h = rho_hash_mix(h, name->var_id.base);
            return rho_hash_mix(h, name->var_id.epoch.word);
        }
    }
    if (rank == RHO_NAME_RANK_BAD) return RHO_RANK_BAD;
    {
        uint32_t base = env->base;
        uint32_t next = env->next;
        uint64_t h;
        env->base = env->len;
        env->next = 0u;
        h = rho_hash_mix(RHO_HASH_QUOTE, rho_hash_proc_in(env, quoted));
        env->base = base;
        env->next = next;
        return h;
    }
}

static uint64_t rho_hash_proc_in(RhoHashEnv *env, Atom *proc) {
    RhoView view = rho_view(proc);
    int rank = rho_proc_rank(&view);
    uint64_t h = (uint64_t)rank + 1u;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_RHO_HASH_VISIT);
    switch (rank) {
    case RHO_RANK_PAR:
        h = rho_hash_mix(h, view.nargs);
        for (uint32_t i = 0u; i < view.nargs; i++)
            h = rho_hash_mix(h, rho_hash_proc_in(env, view.args[i]));
        return h;
    case RHO_RANK_SEND:
        h = rho_hash_mix(h, rho_hash_name_in(env, view.args[0]));
        return rho_hash_mix(h, rho_hash_proc_in(env, view.args[1]));
    case RHO_RANK_RECV:
    case RHO_RANK_RECV_PERSISTENT:
        h = rho_hash_mix(h, rho_hash_name_in(env, view.args[0]));
        if (env->len == env->cap) {
            env->cap = env->cap ? env->cap * 2u : 16u;
            env->vars = cetta_realloc(env->vars, env->cap * sizeof(*env->vars));
            env->numbers = cetta_realloc(env->numbers,
                                         env->cap * sizeof(*env->numbers));
        }
        env->vars[env->len] = view.args[1]->var_id;
        env->numbers[env->len++] = env->next++;
        h = rho_hash_mix(h, rho_hash_proc_in(env, view.args[2]));
        env->len--;
        return h;
    case RHO_RANK_DROP:
        return rho_hash_mix(h, rho_hash_name_in(env, view.args[0]));
    case RHO_RANK_VAL:
    case RHO_RANK_DEFER:
        return rho_hash_mix(h, rho_hash_atom(view.args[0], true, env->memo));
    default:
        return h;
    }
}

/* A process's hash: equal processes (rho_proc_order) hash alike. */
static uint64_t rho_proc_hash(Atom *proc, RhoHashMemo *memo) {
    RhoHashEnv env = {.memo = memo};
    uint64_t h = rho_hash_proc_in(&env, proc);
    free(env.vars);
    free(env.numbers);
    return h;
}

/* A name's hash, as an endpoint's channel. */
static uint64_t rho_name_hash(Atom *name) {
    RhoHashEnv env = {0};
    uint64_t h = rho_hash_name_in(&env, name);
    free(env.vars);
    free(env.numbers);
    return h;
}

/* A name standing alone hashes the same wherever it stands, so its hash is
 * kept by identity. */
static uint64_t rho_name_hash_memo(Atom *name, RhoHashMemo *memo) {
    uint64_t h;
    if (rho_hash_memo_get(memo, name, &h)) return h;
    h = rho_name_hash(name);
    rho_hash_memo_put(memo, name, h);
    return h;
}

/* Endpoints by channel, then by their place among the components. */
static int rho_endpoint_cmp_channel(const void *lhs, const void *rhs) {
    const RhoEndpoint *a = lhs;
    const RhoEndpoint *b = rhs;
    int by_channel = rho_name_order(a->view.args[0], b->view.args[0]);
    if (by_channel != 0) return by_channel;
    if (a->component_index < b->component_index) return -1;
    if (a->component_index > b->component_index) return 1;
    return 0;
}

/* The receives and the sends sorted by channel, joined: the next run of
 * receives [*recv_start, *recv_pos) and run of sends [*send_start, *send_pos)
 * on one channel, each receive able to meet each send.  False when no channel
 * is left with both. */
static inline bool rho_next_comm_run(const RhoEndpointVec *recvs, const RhoEndpointVec *sends,
                              uint32_t *recv_pos, uint32_t *send_pos,
                              uint32_t *recv_start, uint32_t *send_start) {
    while (*recv_pos < recvs->len && *send_pos < sends->len) {
        int cmp = rho_name_order(recvs->items[*recv_pos].view.args[0],
                                 sends->items[*send_pos].view.args[0]);
        if (cmp < 0) { (*recv_pos)++; continue; }
        if (cmp > 0) { (*send_pos)++; continue; }
        *recv_start = *recv_pos;
        *send_start = *send_pos;
        while (*recv_pos < recvs->len &&
               rho_names_equal(recvs->items[*recv_pos].view.args[0],
                               recvs->items[*recv_start].view.args[0]))
            (*recv_pos)++;
        while (*send_pos < sends->len &&
               rho_names_equal(sends->items[*send_pos].view.args[0],
                               sends->items[*send_start].view.args[0]))
            (*send_pos)++;
        return true;
    }
    return false;
}

/* ── Canonical form ─────────────────────────────────────────────────────────
 *
 * Normalization flattens nested rho:par, drops rho:nil components, sorts
 * the survivors by the structural order, and orients the name cancellation
 * @(*x) -> x as a directed simplification.  Two structurally congruent
 * processes normalize to equal terms: par's commutativity and associativity
 * and its unit are taken up by flattening and sorting, alpha-equivalence and
 * the name cancellation by the order itself. */

static Atom *rho_normalize_proc(Arena *arena, Atom *proc);
static Atom *rho_normalize_name(Arena *arena, Atom *name);

static void rho_collect_par(Arena *arena, Atom *proc, RhoAtomVec *out) {
    Atom *norm = rho_normalize_proc(arena, proc);
    RhoView view = rho_view(norm);
    if (view.kind == RHO_NIL) return;
    if (view.kind == RHO_PAR) {
        for (uint32_t i = 0; i < view.nargs; i++) {
            rho_collect_par(arena, view.args[i], out);
        }
        return;
    }
    (void)rho_vec_push(out, norm);
}

typedef struct {
    Atom *atom;
    uint32_t ordinal;
} RhoOrderedAtom;

typedef int (*RhoAtomOrderFn)(Atom *, Atom *);

/* The order sorting runs under: qsort passes no context, and a sort runs on
 * one thread. */
static _Thread_local RhoAtomOrderFn g_rho_sort_order;

/* Components by the order, equal ones in their original places. */
static int rho_ordered_atom_cmp(const void *lhs, const void *rhs) {
    const RhoOrderedAtom *a = lhs;
    const RhoOrderedAtom *b = rhs;
    int c = g_rho_sort_order(a->atom, b->atom);
    if (c != 0) return c;
    if (a->ordinal < b->ordinal) return -1;
    if (a->ordinal > b->ordinal) return 1;
    return 0;
}

/* Sorts atoms in place by `order`, equal ones kept in their places. */
static void rho_sort_atoms(Atom **atoms, uint32_t len, RhoAtomOrderFn order) {
    RhoOrderedAtom *items;
    RhoAtomOrderFn outer = g_rho_sort_order;
    if (len < 2u) return;
    items = cetta_malloc(sizeof(*items) * len);
    for (uint32_t i = 0; i < len; i++) {
        items[i] = (RhoOrderedAtom){atoms[i], i};
    }
    g_rho_sort_order = order;
    qsort(items, len, sizeof(*items), rho_ordered_atom_cmp);
    g_rho_sort_order = outer;
    for (uint32_t i = 0; i < len; i++) atoms[i] = items[i].atom;
    free(items);
}

/* The components as one rho:par, sorted by `order` (the strict core's or
 * the cost profile's). */
static inline Atom *rho_par_sorted(Arena *arena, RhoAtomVec *vec,
                                   RhoAtomOrderFn order) {
    Atom **args;
    if (vec->len == 0) return rho_nil(arena);
    if (vec->len == 1) return vec->items[0];
    args = arena_alloc(arena, sizeof(Atom *) * vec->len);
    memcpy(args, vec->items, sizeof(Atom *) * vec->len);
    rho_sort_atoms(args, vec->len, order);
    return rho_call(arena, "rho:par", args, vec->len);
}

static Atom *rho_par_from_vec(Arena *arena, RhoAtomVec *vec) {
    return rho_par_sorted(arena, vec, rho_proc_order);
}

/* A uniquely enabled COMM has no scheduler choice to encode.  Keep its flat
 * components in their current order and defer canonical sorting until an
 * observation boundary or until contention requires the general scheduler.
 * This is structural-congruence preservation, not an alternative reduction:
 * rho:par ordering is unobservable, and the exact path remains the oracle. */
static Atom *rho_par_from_vec_in_order(Arena *arena, RhoAtomVec *vec) {
    Atom **args;

    if (vec->len == 0) return rho_nil(arena);
    if (vec->len == 1) return vec->items[0];
    args = arena_alloc(arena, sizeof(Atom *) * vec->len);
    for (uint32_t i = 0; i < vec->len; i++) {
        args[i] = vec->items[i];
    }
    return rho_call(arena, "rho:par", args, vec->len);
}

static Atom *rho_copy_var(Arena *arena, Atom *var) {
    if (!var || var->kind != ATOM_VAR) return var;
    return atom_var_like(arena, var, var->var_id);
}

static Atom *rho_normalize_name(Arena *arena, Atom *name) {
    if (name->kind == ATOM_VAR) return rho_copy_var(arena, name);
    RhoView view = rho_view(name);
    if (view.kind == RHO_QUOTE && view.nargs == 1) {
        Atom *inner = rho_normalize_proc(arena, view.args[0]);
        RhoView inner_view = rho_view(inner);
        if (inner_view.kind == RHO_DROP && inner_view.nargs == 1) {
            return rho_normalize_name(arena, inner_view.args[0]);
        }
        return rho_unary(arena, "rho:quote", inner);
    }
    return name;
}

static Atom *rho_normalize_proc(Arena *arena, Atom *proc) {
    RhoView view = rho_view(proc);
    switch (view.kind) {
    case RHO_NIL:
        return rho_nil(arena);
    case RHO_PAR: {
        RhoAtomVec flat;
        rho_vec_init(&flat);
        for (uint32_t i = 0; i < view.nargs; i++) {
            rho_collect_par(arena, view.args[i], &flat);
        }
        Atom *out = rho_par_from_vec(arena, &flat);
        rho_vec_free(&flat);
        return out;
    }
    case RHO_SEND:
        return rho_binary(arena, "rho:send",
                          rho_normalize_name(arena, view.args[0]),
                          rho_normalize_proc(arena, view.args[1]));
    case RHO_RECV:
    case RHO_RECV_PERSISTENT:
        return rho_ternary(
                           arena,
                           view.kind == RHO_RECV_PERSISTENT
                               ? "rho:recv-persistent" : "rho:recv",
                           rho_normalize_name(arena, view.args[0]),
                           rho_copy_var(arena, view.args[1]),
                           rho_normalize_proc(arena, view.args[2]));
    case RHO_DROP:
        return rho_unary(arena, "rho:drop",
                         rho_normalize_name(arena, view.args[0]));
    case RHO_VAL:
        return rho_value_proc(arena, view.args[0]);
    case RHO_EVAL_PAYLOAD:
        return rho_unary(arena, "rho:eval-payload", view.args[0]);
    case RHO_QUOTE:
    case RHO_BAD:
        break;
    }
    return proc;
}

/* ── Free variables, renaming, capture-avoiding substitution ────────────────
 *
 * COMM substitutes the sent name for the receive binder.  When the
 * replacement's free variables would be captured by an inner binder, that
 * binder is freshened first.  Substituting a quotation into a drop position
 * unquotes it (*@P -> P): this is the calculus's only dequotation, and it
 * happens exclusively here, under a matched COMM. */

static bool rho_proc_has_free_var(Atom *proc, VarId var_id);

static bool rho_name_has_free_var(Atom *name, VarId var_id) {
    if (!name) return false;
    if (name->kind == ATOM_VAR) return var_id_eq(name->var_id, var_id);
    RhoView view = rho_view(name);
    if (view.kind == RHO_QUOTE && view.nargs == 1) {
        return rho_proc_has_free_var(view.args[0], var_id);
    }
    return false;
}

static bool rho_proc_has_free_var(Atom *proc, VarId var_id) {
    RhoView view = rho_view(proc);
    switch (view.kind) {
    case RHO_NIL:
        return false;
    case RHO_PAR:
        for (uint32_t i = 0; i < view.nargs; i++) {
            if (rho_proc_has_free_var(view.args[i], var_id)) return true;
        }
        return false;
    case RHO_SEND:
        return view.nargs == 2 &&
               (rho_name_has_free_var(view.args[0], var_id) ||
                rho_proc_has_free_var(view.args[1], var_id));
    case RHO_RECV:
    case RHO_RECV_PERSISTENT:
        if (view.nargs != 3) return false;
        if (rho_name_has_free_var(view.args[0], var_id)) return true;
        if (view.args[1]->kind == ATOM_VAR &&
            var_id_eq(view.args[1]->var_id, var_id)) {
            return false;
        }
        return rho_proc_has_free_var(view.args[2], var_id);
    case RHO_DROP:
        return view.nargs == 1 && rho_name_has_free_var(view.args[0], var_id);
    case RHO_VAL:
    case RHO_EVAL_PAYLOAD:
        return view.nargs == 1 && rho_atom_contains_var_id(view.args[0], var_id);
    case RHO_QUOTE:
    case RHO_BAD:
        break;
    }
    return false;
}

static Atom *rho_fresh_var_like(Arena *arena, Atom *var) {
    VarId id = fresh_var_id();
    return atom_var_like(arena, var, id);
}

static Atom *rho_subst_proc(Arena *arena, Atom *proc,
                            VarId var_id, Atom *replacement_name);
static Atom *rho_subst_name(Arena *arena, Atom *name,
                            VarId var_id, Atom *replacement_name);
static Atom *rho_subst_name_mark(Arena *arena, Atom *name,
                                 VarId var_id, Atom *replacement_name,
                                 bool *matched);

static Atom *rho_subst_name(Arena *arena, Atom *name,
                            VarId var_id, Atom *replacement_name) {
    return rho_subst_name_mark(arena, name, var_id, replacement_name, NULL);
}

static Atom *rho_subst_name_mark(Arena *arena, Atom *name,
                                 VarId var_id, Atom *replacement_name,
                                 bool *matched) {
    Atom *norm = rho_normalize_name(arena, name);
    if (matched) *matched = false;
    if (norm->kind == ATOM_VAR) {
        if (var_id_eq(norm->var_id, var_id)) {
            if (matched) *matched = true;
            return replacement_name;
        }
        return norm;
    }
    return norm;
}

static Atom *rho_subst_proc(Arena *arena, Atom *proc,
                            VarId var_id, Atom *replacement_name) {
    RhoView view = rho_view(proc);
    switch (view.kind) {
    case RHO_NIL:
        return rho_nil(arena);
    case RHO_PAR: {
        Atom **args = arena_alloc(arena, sizeof(Atom *) * view.nargs);
        for (uint32_t i = 0; i < view.nargs; i++) {
            args[i] = rho_subst_proc(arena, view.args[i], var_id,
                                     replacement_name);
        }
        return rho_normalize_proc(arena, rho_call(arena, "rho:par",
                                                  args, view.nargs));
    }
    case RHO_SEND:
        return rho_normalize_proc(arena,
            rho_binary(arena, "rho:send",
                       rho_subst_name(arena, view.args[0],
                                      var_id, replacement_name),
                       rho_subst_proc(arena, view.args[1],
                                      var_id, replacement_name)));
    case RHO_RECV:
    case RHO_RECV_PERSISTENT: {
        const char *head = view.kind == RHO_RECV_PERSISTENT
            ? "rho:recv-persistent" : "rho:recv";
        if (view.args[1]->kind == ATOM_VAR &&
            var_id_eq(view.args[1]->var_id, var_id)) {
            return rho_ternary(arena, head,
                               rho_subst_name(arena, view.args[0],
                                              var_id, replacement_name),
                               rho_copy_var(arena, view.args[1]),
                               rho_normalize_proc(arena, view.args[2]));
        }
        {
            Atom *binder = rho_copy_var(arena, view.args[1]);
            Atom *body = view.args[2];
            if (binder->kind == ATOM_VAR &&
                rho_name_has_free_var(replacement_name, binder->var_id)) {
                /* Renaming the binder apart is substituting a fresh
                 * variable for it: no binder captures a fresh variable,
                 * and a variable is no quote to drop. */
                Atom *fresh = rho_fresh_var_like(arena, binder);
                body = rho_subst_proc(arena, body, binder->var_id, fresh);
                binder = fresh;
            }
            return rho_normalize_proc(arena,
                rho_ternary(arena, head,
                            rho_subst_name(arena, view.args[0],
                                           var_id, replacement_name),
                            binder,
                            rho_subst_proc(arena, body,
                                           var_id, replacement_name)));
        }
    }
    case RHO_DROP: {
        bool matched = false;
        Atom *name = rho_subst_name_mark(arena, view.args[0], var_id,
                                         replacement_name, &matched);
        RhoView name_view = rho_view(name);
        if (matched && name_view.kind == RHO_QUOTE && name_view.nargs == 1) {
            return rho_normalize_proc(arena, name_view.args[0]);
        }
        return rho_unary(arena, "rho:drop", rho_normalize_name(arena, name));
    }
    case RHO_VAL:
        return rho_value_proc(arena,
                              rho_atom_replace_var(arena, view.args[0], var_id,
                                                   replacement_name));
    case RHO_EVAL_PAYLOAD:
        return rho_unary(arena, "rho:eval-payload",
                         rho_atom_replace_var(arena, view.args[0], var_id,
                                              replacement_name));
    case RHO_QUOTE:
    case RHO_BAD:
        break;
    }
    return proc;
}

static bool rho_abt_scope_index(const RhoScope *scope, VarId var_id,
                                uint32_t *out_index) {
    for (uint32_t i = scope->len; i > 0u; i--) {
        if (var_id_eq(scope->items[i - 1u], var_id)) {
            *out_index = scope->len - i;
            return true;
        }
    }
    return false;
}

static Atom *rho_abt_call(Arena *arena, const char *head,
                          Atom **args, uint32_t nargs) {
    Atom **elems = arena_alloc(arena, sizeof(*elems) * (nargs + 1u));
    elems[0] = atom_symbol(arena, head);
    for (uint32_t i = 0; i < nargs; i++) elems[i + 1u] = args[i];
    return atom_expr(arena, elems, nargs + 1u);
}

static Atom *rho_abt_encode_proc_canary(Arena *arena, Atom *proc,
                                        RhoScope *scope);

static Atom *rho_abt_encode_name_canary(Arena *arena, Atom *name,
                                        RhoScope *scope) {
    if (name->kind == ATOM_VAR) {
        uint32_t index = 0;
        if (!rho_abt_scope_index(scope, name->var_id, &index))
            return rho_copy_var(arena, name);
        Atom *args[] = {atom_int(arena, (int64_t)index)};
        return rho_abt_call(arena, "idx", args, 1u);
    }
    RhoView view = rho_view(name);
    if (view.kind == RHO_QUOTE && view.nargs == 1u) {
        RhoScope sealed;
        rho_scope_init(&sealed);
        Atom *quoted = rho_abt_encode_proc_canary(arena, view.args[0], &sealed);
        rho_scope_free(&sealed);
        if (!quoted) return NULL;
        Atom *args[] = {quoted};
        return rho_abt_call(arena, "RhoQuote", args, 1u);
    }
    return NULL;
}

static Atom *rho_abt_encode_proc_canary(Arena *arena, Atom *proc,
                                        RhoScope *scope) {
    RhoView view = rho_view(proc);
    switch (view.kind) {
    case RHO_NIL:
        return atom_symbol(arena, "RhoNil");
    case RHO_PAR: {
        Atom **args = arena_alloc(arena, sizeof(*args) * view.nargs);
        for (uint32_t i = 0; i < view.nargs; i++) {
            args[i] = rho_abt_encode_proc_canary(arena, view.args[i], scope);
            if (!args[i]) return NULL;
        }
        return rho_abt_call(arena, "RhoPar", args, view.nargs);
    }
    case RHO_SEND: {
        Atom *args[] = {
            rho_abt_encode_name_canary(arena, view.args[0], scope),
            rho_abt_encode_proc_canary(arena, view.args[1], scope),
        };
        return args[0] && args[1]
            ? rho_abt_call(arena, "RhoSend", args, 2u) : NULL;
    }
    case RHO_RECV: {
        Atom *channel = rho_abt_encode_name_canary(
            arena, view.args[0], scope);
        if (!channel || view.args[1]->kind != ATOM_VAR) return NULL;
        uint32_t mark = rho_scope_mark(scope);
        if (!rho_scope_push(scope, view.args[1]->var_id)) return NULL;
        Atom *body = rho_abt_encode_proc_canary(arena, view.args[2], scope);
        rho_scope_pop(scope, mark);
        if (!body) return NULL;
        Atom *args[] = {channel, body};
        return rho_abt_call(arena, "RhoRecv", args, 2u);
    }
    case RHO_RECV_PERSISTENT:
        return NULL;
    case RHO_DROP: {
        Atom *name = rho_abt_encode_name_canary(arena, view.args[0], scope);
        if (!name) return NULL;
        Atom *args[] = {name};
        return rho_abt_call(arena, "RhoDrop", args, 1u);
    }
    case RHO_VAL: {
        Atom *args[] = {view.args[0]};
        return rho_abt_call(arena, "RhoVal", args, 1u);
    }
    case RHO_EVAL_PAYLOAD: {
        Atom *args[] = {view.args[0]};
        return rho_abt_call(arena, "RhoEvalPayload", args, 1u);
    }
    case RHO_QUOTE:
    case RHO_BAD:
        return NULL;
    }
    return NULL;
}

/* Examples of the structural order: equal terms (alpha-equivalent, related
 * by @(*x) = x, congruent once normalized) and hash alike; distinct terms are
 * ordered as their written forms are, including terms whose written forms
 * once coincided; payloads nest deeper than any call stack. */
bool rhocalc_structural_order_selftest(Arena *arena) {
    bool ok = true;
    if (!arena || !g_symbols) return false;
    Atom *zero = rho_nil(arena);
    Atom *chan = rho_unary(arena, "rho:quote", zero);
    Atom *x = atom_var_with_id(arena, "x", var_id_from_base(31u));
    Atom *y = atom_var_with_id(arena, "y", var_id_from_base(32u));
    Atom *z = atom_var_with_id(arena, "z", var_id_from_base(33u));

    /* for (x <- @0) { x!(0) } and for (y <- @0) { y!(0) } are alpha-equivalent. */
    Atom *recv_x = rho_ternary(arena, "rho:recv", chan, x,
                               rho_binary(arena, "rho:send", x, zero));
    Atom *recv_y = rho_ternary(arena, "rho:recv", chan, y,
                               rho_binary(arena, "rho:send", y, zero));
    Atom *recv_free = rho_ternary(arena, "rho:recv", chan, y,
                                  rho_binary(arena, "rho:send", z, zero));
    ok = ok && rho_proc_order(recv_x, recv_y) == 0 &&
         rho_proc_hash(recv_x, NULL) == rho_proc_hash(recv_y, NULL);
    ok = ok && rho_proc_order(recv_x, recv_free) != 0 &&
         rho_proc_order(recv_x, recv_free) == -rho_proc_order(recv_free, recv_x);

    /* @(*z) is z. */
    Atom *quoted_drop = rho_unary(arena, "rho:quote",
                                  rho_unary(arena, "rho:drop", z));
    ok = ok && rho_names_equal(quoted_drop, z) &&
         rho_name_hash(quoted_drop) == rho_name_hash(z);

    /* (a | (b | 0)) and (b | a) normalize to equal terms. */
    Atom *a = rho_binary(arena, "rho:send", chan, zero);
    Atom *b = rho_value_proc(arena, atom_int(arena, 7));
    Atom *left = rho_normalize_proc(arena, rho_binary(arena, "rho:par", a,
        rho_binary(arena, "rho:par", b, zero)));
    Atom *right = rho_normalize_proc(arena, rho_binary(arena, "rho:par", b, a));
    ok = ok && rho_proc_order(left, right) == 0 &&
         rho_proc_hash(left, NULL) == rho_proc_hash(right, NULL);

    /* The written form orders: recv before send, val(10) before val(9),
     * an expression before the symbol spelled as it. */
    ok = ok && rho_proc_order(recv_x, a) < 0;
    ok = ok && rho_proc_order(rho_value_proc(arena, atom_int(arena, 10)),
                              rho_value_proc(arena, atom_int(arena, 9))) < 0;
    {
        Atom *alpha_items[] = {atom_symbol(arena, "alpha")};
        Atom *expr = rho_value_proc(arena, atom_expr(arena, alpha_items, 1u));
        Atom *symbol = rho_value_proc(arena, atom_symbol(arena, "(alpha)"));
        Atom *string = rho_value_proc(arena, atom_string(arena, "alpha"));
        Atom *plain = rho_value_proc(arena, atom_symbol(arena, "alpha"));
        ok = ok && rho_proc_order(expr, symbol) < 0 &&
             rho_proc_order(symbol, expr) > 0;
        ok = ok && rho_proc_order(string, plain) != 0 &&
             rho_proc_order(string, plain) == -rho_proc_order(plain, string);
    }

    /* Equal payloads in distinct atoms are equal and hash alike. */
    {
        Atom *payload = atom_expr3(arena, atom_symbol(arena, "f"),
                                   atom_string(arena, "line\n\"q\""),
                                   atom_var_with_id(arena, "w", var_id_from_base(34u)));
        Atom *p1 = rho_value_proc(arena, payload);
        Atom *p2 = rho_value_proc(arena, atom_deep_copy(arena, payload));
        ok = ok && rho_proc_order(p1, p2) == 0 &&
             rho_proc_hash(p1, NULL) == rho_proc_hash(p2, NULL);
    }

    /* Payloads nested far deeper than a call stack compare and hash. */
    {
        Atom *deep_a = atom_int(arena, 1);
        Atom *deep_b = atom_int(arena, 2);
        Atom *s = atom_symbol(arena, "s");
        for (uint32_t i = 0u; i < 200000u; i++) {
            deep_a = atom_expr2(arena, s, deep_a);
            deep_b = atom_expr2(arena, s, deep_b);
        }
        ok = ok && rho_proc_order(rho_value_proc(arena, deep_a),
                                  rho_value_proc(arena, deep_b)) < 0;
        ok = ok && rho_proc_hash(rho_value_proc(arena, deep_a), NULL) !=
                   rho_proc_hash(rho_value_proc(arena, deep_b), NULL);
    }

    /* A state cell compares by its value, never equal to an expression. */
    {
        StateCell first = {0};
        StateCell second = {0};
        first.value = atom_expr2(arena, atom_symbol(arena, "held"),
                                 atom_int(arena, 9));
        second.value = atom_expr2(arena, atom_symbol(arena, "held"),
                                  atom_int(arena, 9));
        Atom *state_items[] = {atom_symbol(arena, "State"), first.value};
        Atom *s1 = rho_value_proc(arena, atom_state(arena, &first));
        Atom *s2 = rho_value_proc(arena, atom_state(arena, &second));
        Atom *spelled = rho_value_proc(arena, atom_expr(arena, state_items, 2u));
        ok = ok && rho_proc_order(s1, s2) == 0 &&
             rho_proc_hash(s1, NULL) == rho_proc_hash(s2, NULL) &&
             rho_proc_order(s1, spelled) != 0;
    }
    return ok;
}

bool rhocalc_abt_substitution_correspondence_selftest(Arena *arena) {
    if (!arena || !g_symbols) return false;

    AbtSignature signature;
    abt_signature_init(&signature);
    Atom *fields_args[] = {
        atom_symbol(arena, "channel"), atom_symbol(arena, "body")};
    Atom *fields = rho_abt_call(arena, "Fields", fields_args, 2u);
    Atom *bind_args[] = {atom_symbol(arena, "body")};
    Atom *bind = rho_abt_call(arena, "Bind1", bind_args, 1u);
    Atom *decl_args[] = {
        atom_symbol(arena, "RhoRecv"), fields, bind};
    Atom *decl = rho_abt_call(arena, "AbtSignature", decl_args, 3u);
    if (!abt_signature_add_decl(&signature, decl)) {
        abt_signature_free(&signature);
        return false;
    }

    Atom *channel = rho_unary(arena, "rho:quote", rho_nil(arena));
    Atom *outer = atom_var_with_id(
        arena, "y", (VarId){11u, {1u}});
    Atom *inner = atom_var_with_id(
        arena, "x", (VarId){12u, {1u}});
    Atom *inner_body = rho_binary(
        arena, "rho:send", outer, rho_nil(arena));
    Atom *inner_recv = rho_ternary(
        arena, "rho:recv", channel, inner, inner_body);
    Atom *outer_recv = rho_ternary(
        arena, "rho:recv", channel, outer, inner_recv);

    /* The quoted occurrence is free even though it has the same VarId as the
       inner receive binder; quote reseals its scope. */
    Atom *replacement = rho_unary(
        arena, "rho:quote", rho_unary(arena, "rho:drop", inner));
    Atom *nameful_result = rho_subst_proc(
        arena, inner_recv, outer->var_id, replacement);
    RhoView nameful_view = rho_view(nameful_result);

    RhoScope scope;
    rho_scope_init(&scope);
    Atom *canonical_outer = rho_abt_encode_proc_canary(
        arena, outer_recv, &scope);
    Atom *normalized_replacement = rho_normalize_name(arena, replacement);
    Atom *canonical_replacement = rho_abt_encode_name_canary(
        arena, normalized_replacement, &scope);
    Atom *canonical_nameful = rho_abt_encode_proc_canary(
        arena, nameful_result, &scope);
    rho_scope_free(&scope);

    Atom *canonical_body =
        canonical_outer && canonical_outer->kind == ATOM_EXPR &&
        canonical_outer->expr.len == 3u
            ? canonical_outer->expr.elems[2] : NULL;
    Atom *canonical_result = canonical_body && canonical_replacement
        ? abt_subst(&signature, arena, 0u,
                    canonical_replacement, canonical_body)
        : NULL;
    bool nameful_recv = nameful_view.kind == RHO_RECV;
    bool renamed = nameful_recv &&
                   nameful_view.args[1]->kind == ATOM_VAR &&
                   !var_id_eq(nameful_view.args[1]->var_id, inner->var_id);
    bool encoded = canonical_result && canonical_nameful;
    bool equal = encoded && atom_eq(canonical_result, canonical_nameful);
    bool scoped = encoded &&
                  abt_scope_check(&signature, 0u, canonical_result);
    bool ok = nameful_recv && renamed && encoded && equal && scoped;
    if (!ok) {
        fprintf(stderr,
                "rho ABT canary: recv=%u renamed=%u encoded=%u equal=%u scoped=%u\n",
                nameful_recv ? 1u : 0u, renamed ? 1u : 0u,
                encoded ? 1u : 0u, equal ? 1u : 0u, scoped ? 1u : 0u);
        if (canonical_result) {
            fputs("rho ABT canary generic: ", stderr);
            atom_print(canonical_result, stderr);
            fputc('\n', stderr);
        }
        if (canonical_nameful) {
            fputs("rho ABT canary nameful: ", stderr);
            atom_print(canonical_nameful, stderr);
            fputc('\n', stderr);
        }
    }
    abt_signature_free(&signature);
    return ok;
}

/* Keeps `norm` in the set; a push that fails changes nothing. */
static bool rho_successor_set_acc_push(RhoSuccessorSetAcc *acc, Atom *norm) {
    return rho_push_atom(rho_resize, &acc->items, &acc->len, &acc->cap, norm) != 0;
}

/* The successors sorted by the structural order, one of each equal run (the
 * first met). */
static void rho_successor_set_acc_finish(RhoSuccessorSetAcc *acc, RhoSuccessorSet *out) {
    if (acc->len > 1) {
        uint32_t write = 1u;
        rho_sort_atoms(acc->items, acc->len, rho_proc_order);
        for (uint32_t i = 1u; i < acc->len; i++) {
            if (rho_proc_order(acc->items[write - 1u], acc->items[i]) == 0)
                continue;
            acc->items[write++] = acc->items[i];
        }
        acc->len = write;
    }
    out->items = acc->items;
    out->len = acc->len;
    acc->items = NULL;
    acc->len = 0;
    acc->cap = 0;
}

static Atom *rho_rebuild_reaction_with_order(
    Arena *arena, RhoAtomVec *components,
    uint32_t send_index, uint32_t recv_index,
    Atom *body, bool canonical_order) {
    RhoAtomVec out;
    bool retain_recv;

    if (!arena || !components || send_index >= components->len ||
        recv_index >= components->len || send_index == recv_index) {
        return NULL;
    }
    retain_recv =
        rho_view(components->items[recv_index]).kind == RHO_RECV_PERSISTENT;
    rho_vec_init(&out);
    for (uint32_t i = 0; i < components->len; i++) {
        if (i == send_index || (i == recv_index && !retain_recv)) continue;
        if (!rho_vec_push(&out, components->items[i])) {
            rho_vec_free(&out);
            return NULL;
        }
    }
    RhoView body_view = rho_view(body);
    if (body_view.kind == RHO_PAR) {
        for (uint32_t i = 0; i < body_view.nargs; i++) {
            if (!rho_vec_push(&out, body_view.args[i])) {
                rho_vec_free(&out);
                return NULL;
            }
        }
    } else if (body_view.kind != RHO_NIL) {
        if (!rho_vec_push(&out, body)) {
            rho_vec_free(&out);
            return NULL;
        }
    }
    Atom *proc = canonical_order
        ? rho_par_from_vec(arena, &out)
        : rho_par_from_vec_in_order(arena, &out);
    rho_vec_free(&out);
    return proc;
}

static Atom *rho_rebuild_reaction(Arena *arena, RhoAtomVec *components,
                                  uint32_t send_index, uint32_t recv_index,
                                  Atom *body) {
    return rho_rebuild_reaction_with_order(
        arena, components, send_index, recv_index, body, true);
}

static Atom *rho_rebuild_unique_reaction(
    Arena *arena, RhoAtomVec *components,
    uint32_t send_index, uint32_t recv_index, Atom *body) {
    return rho_rebuild_reaction_with_order(
        arena, components, send_index, recv_index, body, false);
}

static bool rho_collect_endpoints(RhoAtomVec *components,
                                  RhoEndpointVec *sends,
                                  RhoEndpointVec *recvs) {
    for (uint32_t i = 0; i < components->len; i++) {
        RhoView view = rho_view(components->items[i]);
        if (view.kind == RHO_SEND && view.nargs == 2) {
            if (!rho_endpoint_vec_push(sends, i, view)) return false;
        } else if ((view.kind == RHO_RECV ||
                    view.kind == RHO_RECV_PERSISTENT) &&
                   view.nargs == 3) {
            if (!rho_endpoint_vec_push(recvs, i, view)) return false;
        }
    }
    if (sends->len > 1) {
        qsort(sends->items, sends->len, sizeof(RhoEndpoint),
              rho_endpoint_cmp_channel);
    }
    if (recvs->len > 1) {
        qsort(recvs->items, recvs->len, sizeof(RhoEndpoint),
              rho_endpoint_cmp_channel);
    }
    return true;
}

/* The determinate fast path only needs to know whether there is exactly one
 * enabled COMM, so its endpoints stay in component order and channels are
 * compared for equality pair by pair. */
static bool rho_collect_endpoints_for_single_comm(
    RhoAtomVec *components, RhoEndpointVec *sends,
    RhoEndpointVec *recvs) {
    for (uint32_t i = 0u; i < components->len; i++) {
        RhoView view = rho_view(components->items[i]);
        if (view.kind == RHO_SEND && view.nargs == 2u) {
            if (!rho_endpoint_vec_push(sends, i, view)) return false;
        } else if ((view.kind == RHO_RECV ||
                    view.kind == RHO_RECV_PERSISTENT) &&
                   view.nargs == 3u) {
            if (!rho_endpoint_vec_push(recvs, i, view)) return false;
        }
    }
    return true;
}

/* ── rhometta payload isolation ─────────────────────────────────────────────
 *
 * Read-only-snapshot support: a per-payload map from each original space to its
 * copy-on-write overlay.  Every space a payload could reach gets one, so writes
 * land in per-payload scratch and never escape to a sibling payload. */
typedef struct {
    Space **orig;
    Space **overlay;
    int len, cap;
    Space *inline_orig[16];
    Space *inline_overlay[16];
} RhoOverlayMap;

typedef struct {
    StateCell **orig;
    StateCell **clone;
    int len, cap;
    RhoStateObservation *observations;
    int observation_cap;
    StateCell *inline_orig[16];
    StateCell *inline_clone[16];
} RhoStateCloneMap;

static bool rho_state_clone_observations_ensure(RhoStateCloneMap *map,
                                                int capacity) {
    if (map->observation_cap >= capacity)
        return true;
    if (capacity <= 0 || (size_t)capacity > SIZE_MAX / sizeof(*map->observations))
        return false;
    RhoStateObservation *next = cetta_fallible_calloc((size_t)capacity, sizeof(*next));
    if (!next)
        return false;
    if (map->observations)
        memcpy(next, map->observations, (size_t)map->len * sizeof(*next));
    free(map->observations);
    map->observations = next;
    map->observation_cap = capacity;
    return true;
}

static bool rho_state_clone_observe(RhoStateCloneMap *map, int index,
                                     StateCell *cell) {
    RhoStateObservation observation;
    if (!rho_state_observation_acquire(cell, &observation))
        return false;
    if (!observation.identity_pin)
        return true;
    if (!rho_state_clone_observations_ensure(map, map->cap)) {
        rho_state_observation_release(&observation);
        return false;
    }
    map->observations[index] = observation;
    return true;
}

static StateCellSnapshot rho_state_clone_view(const RhoStateCloneMap *map,
                                              int index) {
    if (map->observations && index < map->observation_cap &&
        map->observations[index].identity_pin)
        return map->observations[index].snapshot;
    return (StateCellSnapshot){.value = map->orig[index]->value,
        .content_type = map->orig[index]->content_type};
}

static bool rho_map_next_capacity(int current_cap, int min_needed,
                                  size_t elem_size, int *out_next) {
    int next = current_cap > 0 ? current_cap : 16;
    if (!out_next || min_needed <= 0 || elem_size == 0)
        return false;
    while (next < min_needed) {
        if (next > INT_MAX / 2)
            return false;
        next *= 2;
    }
    if ((size_t)next > SIZE_MAX / elem_size)
        return false;
    *out_next = next;
    return true;
}

static bool rho_overlay_map_ensure(RhoOverlayMap *map, int min_needed) {
    int next_cap;
    if (!map)
        return false;
    if (!map->orig) {
        map->orig = map->inline_orig;
        map->overlay = map->inline_overlay;
        map->cap = (int)(sizeof(map->inline_orig) / sizeof(map->inline_orig[0]));
    }
    if (map->cap >= min_needed)
        return true;
    if (!rho_map_next_capacity(map->cap, min_needed, sizeof(Space *),
                               &next_cap))
        return false;
    if (map->orig == map->inline_orig) {
        Space **orig = cetta_malloc(sizeof(Space *) * (size_t)next_cap);
        Space **overlay = cetta_malloc(sizeof(Space *) * (size_t)next_cap);
        if (map->len > 0) {
            memcpy(orig, map->orig, sizeof(Space *) * (size_t)map->len);
            memcpy(overlay, map->overlay, sizeof(Space *) * (size_t)map->len);
        }
        map->orig = orig;
        map->overlay = overlay;
    } else {
        map->orig =
            cetta_realloc(map->orig, sizeof(Space *) * (size_t)next_cap);
        map->overlay =
            cetta_realloc(map->overlay, sizeof(Space *) * (size_t)next_cap);
    }
    map->cap = next_cap;
    return true;
}

static void rho_overlay_map_free(RhoOverlayMap *map) {
    if (!map)
        return;
    if (map->orig && map->orig != map->inline_orig)
        free(map->orig);
    if (map->overlay && map->overlay != map->inline_overlay)
        free(map->overlay);
    map->orig = map->inline_orig;
    map->overlay = map->inline_overlay;
    map->len = 0;
    map->cap = (int)(sizeof(map->inline_orig) / sizeof(map->inline_orig[0]));
}

static bool rho_state_clone_map_ensure(RhoStateCloneMap *map, int min_needed) {
    int next_cap;
    if (!map)
        return false;
    if (!map->orig) {
        map->orig = map->inline_orig;
        map->clone = map->inline_clone;
        map->cap = (int)(sizeof(map->inline_orig) / sizeof(map->inline_orig[0]));
    }
    if (map->cap >= min_needed)
        return true;
    if (!rho_map_next_capacity(map->cap, min_needed, sizeof(StateCell *),
                               &next_cap))
        return false;
    if (map->observations && !rho_state_clone_observations_ensure(map, next_cap))
        return false;
    if (map->orig == map->inline_orig) {
        StateCell **orig =
            cetta_malloc(sizeof(StateCell *) * (size_t)next_cap);
        StateCell **clone =
            cetta_malloc(sizeof(StateCell *) * (size_t)next_cap);
        if (map->len > 0) {
            memcpy(orig, map->orig, sizeof(StateCell *) * (size_t)map->len);
            memcpy(clone, map->clone, sizeof(StateCell *) * (size_t)map->len);
        }
        map->orig = orig;
        map->clone = clone;
    } else {
        map->orig =
            cetta_realloc(map->orig, sizeof(StateCell *) * (size_t)next_cap);
        map->clone =
            cetta_realloc(map->clone, sizeof(StateCell *) * (size_t)next_cap);
    }
    map->cap = next_cap;
    return true;
}

static void rho_state_clone_map_free(RhoStateCloneMap *map) {
    if (!map)
        return;
    if (map->observations) {
        for (int i = 0; i < map->len; i++)
            rho_state_observation_release(&map->observations[i]);
        free(map->observations);
        map->observations = NULL;
        map->observation_cap = 0;
    }
    if (map->orig && map->orig != map->inline_orig)
        free(map->orig);
    if (map->clone && map->clone != map->inline_clone)
        free(map->clone);
    map->orig = map->inline_orig;
    map->clone = map->inline_clone;
    map->len = 0;
    map->cap = (int)(sizeof(map->inline_orig) / sizeof(map->inline_orig[0]));
}

static Space *rho_overlay_for(const RhoOverlayMap *map, Space *orig) {
    for (int i = 0; i < map->len; i++)
        if (map->orig[i] == orig) return map->overlay[i];
    return NULL;
}

static StateCell *rho_state_clone_for(const RhoStateCloneMap *map,
                                      StateCell *orig) {
    for (int i = 0; i < map->len; i++)
        if (map->orig[i] == orig) return map->clone[i];
    return NULL;
}

/* Add `orig` to the map (deduped), creating its copy-on-write overlay.  Returns
 * false only on allocation failure or if the map is full. */
static bool rho_overlay_map_add(RhoOverlayMap *map, Space *orig) {
    Space *ov;
    if (!orig || rho_overlay_for(map, orig))
        return true;
    if (!rho_overlay_map_ensure(map, map->len + 1))
        return false;
    ov = cetta_malloc(sizeof(Space));
    if (!ov)
        return false;
    space_init_overlay(ov, orig);
    if (!eval_payload_track_scratch_space(ov)) {
        space_free(ov);
        free(ov);
        return false;
    }
    map->orig[map->len] = orig;
    map->overlay[map->len] = ov;
    map->len++;
    return true;
}

static bool rho_overlay_map_bind_pair(RhoOverlayMap *map, Space *orig,
                                      Space *overlay) {
    if (!orig || !overlay)
        return false;
    for (int i = 0; i < map->len; i++) {
        if (map->orig[i] == orig) {
            map->overlay[i] = overlay;
            return true;
        }
    }
    if (!rho_overlay_map_ensure(map, map->len + 1))
        return false;
    map->orig[map->len] = orig;
    map->overlay[map->len] = overlay;
    map->len++;
    return true;
}

static bool rho_state_clone_map_note(RhoStateCloneMap *map, StateCell *orig,
                                     bool *inserted) {
    if (inserted)
        *inserted = false;
    if (!orig)
        return true;
    /* Discovery precedes materialization, so a seen identity can still have
     * a NULL clone. Testing the clone would revisit State cycles forever. */
    for (int i = 0; i < map->len; i++)
        if (map->orig[i] == orig)
            return true;
    if (!rho_state_clone_map_ensure(map, map->len + 1))
        return false;
    if (!rho_state_clone_observe(map, map->len, orig))
        return false;
    map->orig[map->len] = orig;
    map->clone[map->len] = NULL;
    map->len++;
    if (inserted)
        *inserted = true;
    return true;
}

static bool rho_state_clone_map_bind_pair(RhoStateCloneMap *map, StateCell *orig,
                                          StateCell *clone) {
    if (!orig || !clone)
        return false;
    for (int i = 0; i < map->len; i++) {
        if (map->orig[i] == orig) {
            map->clone[i] = clone;
            return true;
        }
    }
    if (!rho_state_clone_map_ensure(map, map->len + 1))
        return false;
    if (!rho_state_clone_observe(map, map->len, orig))
        return false;
    map->orig[map->len] = orig;
    map->clone[map->len] = clone;
    map->len++;
    return true;
}

bool rhocalc_payload_map_capacity_selftest(void) {
    RhoOverlayMap overlay_map = {0};
    RhoStateCloneMap state_map = {0};
    bool ok = true;
    for (uintptr_t i = 1; i <= 80; i++) {
        if (!rho_overlay_map_bind_pair(
                &overlay_map, (Space *)(i << 4),
                (Space *)((i + 1000u) << 4))) {
            ok = false;
            break;
        }
        if (!rho_state_clone_map_bind_pair(
                &state_map, (StateCell *)(i << 4),
                (StateCell *)((i + 2000u) << 4))) {
            ok = false;
            break;
        }
    }
    ok = ok &&
         overlay_map.len == 80 &&
         overlay_map.cap >= 80 &&
         overlay_map.orig != overlay_map.inline_orig &&
         overlay_map.overlay != overlay_map.inline_overlay &&
         state_map.len == 80 &&
         state_map.cap >= 80 &&
         state_map.orig != state_map.inline_orig &&
         state_map.clone != state_map.inline_clone;
    rho_overlay_map_free(&overlay_map);
    rho_state_clone_map_free(&state_map);
    return ok &&
           overlay_map.len == 0 &&
           overlay_map.orig == overlay_map.inline_orig &&
           state_map.len == 0 &&
           state_map.orig == state_map.inline_orig;
}

static Atom *rho_atom_rebind_resources(Arena *arena, Atom *atom,
                                       const RhoOverlayMap *space_map,
                                       const RhoStateCloneMap *state_map);

/* Collect every GV_SPACE literal baked into the payload term into the map. */
static bool rho_collect_space_ptrs_into_map(RhoOverlayMap *map, Atom *atom) {
    if (!atom)
        return true;
    if (atom->kind == ATOM_GROUNDED) {
        if (atom->ground.gkind == GV_SPACE)
            return rho_overlay_map_add(map, (Space *)atom->ground.ptr);
        return true;
    }
    if (atom->kind != ATOM_EXPR)
        return true;
    for (CettaExprIndex i = 0; i < atom->expr.len; i++)
        if (!rho_collect_space_ptrs_into_map(map, atom->expr.elems[i]))
            return false;
    return true;
}

static bool rho_collect_state_ptrs_into_map(RhoStateCloneMap *map, Atom *atom) {
    if (!atom)
        return true;
    if (atom->kind == ATOM_GROUNDED) {
        if (atom->ground.gkind == GV_STATE) {
            StateCell *cell = (StateCell *)atom->ground.ptr;
            bool inserted = false;
            if (!rho_state_clone_map_note(map, cell, &inserted))
                return false;
            if (inserted && cell) {
                StateCellSnapshot view = rho_state_clone_view(map, map->len - 1);
                if (!rho_collect_state_ptrs_into_map(map, view.value))
                    return false;
                if (!rho_collect_state_ptrs_into_map(map, view.content_type))
                    return false;
            }
        }
        return true;
    }
    if (atom->kind != ATOM_EXPR)
        return true;
    for (CettaExprIndex i = 0; i < atom->expr.len; i++)
        if (!rho_collect_state_ptrs_into_map(map, atom->expr.elems[i]))
            return false;
    return true;
}

static bool rho_state_clone_map_materialize(RhoStateCloneMap *map,
                                            Arena *owner,
                                            const RhoOverlayMap *space_map,
                                            uint64_t owner_epoch) {
    if (!map || !owner)
        return false;
    for (int i = 0; i < map->len; i++) {
        if (map->clone[i])
            continue;
        map->clone[i] = cetta_malloc(sizeof(StateCell));
        if (!map->clone[i])
            return false;
        map->clone[i]->value = NULL;
        map->clone[i]->content_type = NULL;
        map->clone[i]->payload_owner_epoch = owner_epoch;
        map->clone[i]->payload_export_owner_epoch = 0;
        if (!eval_payload_track_scratch_state(map->clone[i]))
            return false;
    }
    for (int i = 0; i < map->len; i++) {
        StateCell *orig = map->orig[i];
        StateCell *clone = map->clone[i];
        if (!orig || !clone)
            continue;
        StateCellSnapshot view = rho_state_clone_view(map, i);
        if (view.value) {
            clone->value =
                rho_atom_rebind_resources(owner, view.value, space_map, map);
            if (!clone->value)
                return false;
        }
        if (view.content_type) {
            clone->content_type = rho_atom_rebind_resources(
                owner, view.content_type, space_map, map);
            if (!clone->content_type)
                return false;
        }
    }
    return true;
}

static bool rho_eval_registry_init(Registry *dst, Registry *src,
                                   const RhoOverlayMap *space_map,
                                   const RhoStateCloneMap *state_map,
                                   Space *self_overlay,
                                   Arena *arena) {
    registry_init(dst);
    if (!dst) return true;
    if (src) {
        for (uint32_t i = 0; i < src->len; i++) {
            Atom *value;
            const Atom *name_key = registry_entry_name_key(src, i);
            if ((src->entries[i].key == SYMBOL_ID_NONE && !name_key) ||
                !src->entries[i].value) {
                continue;
            }
            value = rho_atom_rebind_resources(
                arena, src->entries[i].value, space_map, state_map);
            if (!value)
                return false;
            if (name_key) {
                if (!registry_bind_name(dst, (Atom *)name_key, value))
                    return false;
            } else {
                registry_bind_id(dst, src->entries[i].key, value);
            }
        }
    }
    if (g_builtin_syms.self != SYMBOL_ID_NONE && self_overlay) {
        registry_bind_id(dst, g_builtin_syms.self, atom_space(arena, self_overlay));
    }
    return true;
}

static Atom *rho_atom_rebind_resources(Arena *arena, Atom *atom,
                                       const RhoOverlayMap *space_map,
                                       const RhoStateCloneMap *state_map) {
    Atom **elems;
    if (!arena || !atom)
        return NULL;
    if (atom->kind == ATOM_GROUNDED) {
        if (atom->ground.gkind == GV_SPACE) {
            Space *ov = rho_overlay_for(space_map, (Space *)atom->ground.ptr);
            if (ov)
                return atom_space(arena, ov);
        } else if (atom->ground.gkind == GV_STATE) {
            StateCell *clone =
                rho_state_clone_for(state_map, (StateCell *)atom->ground.ptr);
            if (clone)
                return atom_state(arena, clone);
        }
        return atom_deep_copy(arena, atom);
    }
    if (atom->kind == ATOM_SYMBOL)
        return atom_symbol_id(arena, atom->sym_id);
    if (atom->kind == ATOM_VAR)
        return atom_var_like(arena, atom, atom->var_id);
    if (atom->kind != ATOM_EXPR)
        return atom_deep_copy(arena, atom);
    elems = arena_alloc(arena, sizeof(Atom *) * atom->expr.len);
    if (!elems)
        return NULL;
    for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
        elems[i] = rho_atom_rebind_resources(arena, atom->expr.elems[i],
                                             space_map, state_map);
        if (!elems[i])
            return NULL;
    }
    return atom_expr(arena, elems, atom->expr.len);
}

/* Resources promoted out of a payload's scratch: each original space or state
 * cell to its stable copy and the owner epoch it was exported under.  A state
 * cell also keeps an identity pin while it is mapped.  The stable copies and
 * the epochs run beside the originals, one each; the pins too, from the first
 * pin made on (the entries before it have none). */
typedef struct {
    void **orig;
    void **stable;
    uint64_t *export_owner_epoch;
    void **identity_pins;
    CettaCount len, cap;               /* the entries; orig's capacity */
    CettaCount stable_cap, epoch_cap;  /* the capacities of the lists beside it */
    CettaCount pin_len, pin_cap;
} RhoPromotedMap;

/* The stable copy of orig, or NULL.  orig met under another owner epoch is an
 * alias fault. */
static void *rho_promoted_for(const RhoPromotedMap *map, const void *orig,
                              uint64_t export_owner_epoch, bool *alias_fault) {
    if (!map || !orig)
        return NULL;
    for (CettaCount i = 0; i < map->len; i++) {
        if (map->orig[i] == orig) {
            if (map->export_owner_epoch &&
                map->export_owner_epoch[i] != export_owner_epoch) {
                if (alias_fault)
                    *alias_fault = true;
                return NULL;
            }
            return map->stable[i];
        }
    }
    return NULL;
}

/* Rebinds orig when it is mapped (false on an alias fault); otherwise says
 * in *is_new that it is to be appended. */
static bool rho_promoted_slot(RhoPromotedMap *map, void *orig, void *stable,
                              uint64_t export_owner_epoch, bool *alias_fault,
                              bool *is_new) {
    if (!map || !orig || !stable)
        return false;
    *is_new = false;
    for (CettaCount i = 0; i < map->len; i++) {
        if (map->orig[i] == orig) {
            if (map->export_owner_epoch &&
                map->export_owner_epoch[i] != export_owner_epoch) {
                if (alias_fault)
                    *alias_fault = true;
                return false;
            }
            map->stable[i] = stable;
            return true;
        }
    }
    *is_new = true;
    return true;
}

/* Appends an entry: the original, its stable copy and its epoch, all three or
 * none (false when memory ran out). */
static bool rho_promoted_append(RhoPromotedMap *map, void *orig, void *stable,
                                uint64_t export_owner_epoch) {
    CettaCount len = map->len, stables = len, epochs = len;
    if (!rho_push_pointer(rho_resize_count, &map->orig, &map->len, &map->cap, orig) ||
        !rho_push_pointer(rho_resize_count, &map->stable, &stables, &map->stable_cap, stable) ||
        !rho_push_epoch(rho_resize_count, &map->export_owner_epoch, &epochs, &map->epoch_cap,
                        export_owner_epoch)) {
        map->len = len;
        return false;
    }
    return true;
}

static bool rho_promoted_space_bind(RhoPromotedMap *map, Space *orig, Space *stable,
                                    uint64_t export_owner_epoch, bool *alias_fault) {
    bool is_new;
    if (!rho_promoted_slot(map, orig, stable, export_owner_epoch, alias_fault, &is_new))
        return false;
    return !is_new || rho_promoted_append(map, orig, stable, export_owner_epoch);
}

static bool rho_promoted_state_bind(RhoPromotedMap *map, StateCell *orig,
                                    StateCell *stable, uint64_t export_owner_epoch,
                                    bool *alias_fault) {
    bool is_new;
    if (!rho_promoted_slot(map, orig, stable, export_owner_epoch, alias_fault, &is_new))
        return false;
    if (!is_new)
        return true;
    if ((!state_cell_identity_pin || !state_cell_identity_unpin) &&
        state_cell_is_managed && state_cell_is_managed(orig))
        return false;
    void *pin = state_cell_identity_pin && state_cell_identity_unpin
        ? state_cell_identity_pin(orig) : NULL;
    bool ok = true;
    if (pin || map->identity_pins) {   /* none for the entries before the first pin */
        while (ok && map->pin_len < map->len)
            ok = rho_push_pointer(rho_resize_count, &map->identity_pins, &map->pin_len,
                                  &map->pin_cap, NULL);
        ok = ok && rho_push_pointer(rho_resize_count, &map->identity_pins, &map->pin_len,
                                    &map->pin_cap, pin);
    }
    ok = ok && rho_promoted_append(map, orig, stable, export_owner_epoch);
    if (!ok) {   /* the pin goes with the entry */
        if (map->pin_len > map->len)
            map->pin_len = map->len;
        if (pin)
            state_cell_identity_unpin(pin);
    }
    return ok;
}

static void rho_promoted_map_free(RhoPromotedMap *map) {
    if (!map)
        return;
    if (map->identity_pins) {
        for (CettaCount i = 0u; i < map->pin_len; i++)
            if (map->identity_pins[i])
                state_cell_identity_unpin(map->identity_pins[i]);
        free(map->identity_pins);
    }
    free(map->orig);
    free(map->stable);
    free(map->export_owner_epoch);
    *map = (RhoPromotedMap){0};
}

static Atom *rho_promote_payload_atom(Arena *arena,
                                      Arena *persistent,
                                      Atom *atom,
                                      const RhoOverlayMap *space_map,
                                      const RhoStateCloneMap *state_map,
                                      RhoPromotedMap *promoted_spaces,
                                      RhoPromotedMap *promoted_states,
                                      uint64_t owner_epoch,
                                      uint64_t export_owner_epoch,
                                      bool *alias_fault);

static Space *rho_promote_payload_space(Arena *persistent,
                                        Space *space,
                                        const RhoOverlayMap *space_map,
                                        const RhoStateCloneMap *state_map,
                                        RhoPromotedMap *promoted_spaces,
                                        RhoPromotedMap *promoted_states,
                                        uint64_t owner_epoch,
                                        uint64_t export_owner_epoch,
                                        bool *alias_fault) {
    Space *visible = space;
    Space *stable;
    CettaCount logical_len;

    if (!persistent || !space)
        return NULL;
    if (space_map) {
        Space *overlay = rho_overlay_for(space_map, space);
        if (overlay)
            visible = overlay;
    }
    stable = rho_promoted_for(promoted_spaces, visible,
                                    export_owner_epoch, alias_fault);
    if (stable)
        return stable;

    stable = arena_alloc(persistent, sizeof(Space));
    if (!stable)
        return NULL;
    if (!rho_promoted_space_bind(promoted_spaces, visible, stable,
                                 export_owner_epoch, alias_fault))
        return NULL;
    space_init_with_universe(stable,
                             visible ? visible->native.universe : NULL);
    stable->kind = visible->kind;
    space_set_strict(stable, visible->strict);
    stable->payload_owner_epoch = 0;
    stable->payload_export_owner_epoch = export_owner_epoch;
    if (!space_match_backend_try_set(stable, visible->match_backend.kind))
        return NULL;

    logical_len = space_length64(visible);
    for (CettaIndex i = 0; i < logical_len; i++) {
        Atom *source_atom = space_get_at64(visible, i);
        Atom *promoted_atom =
            rho_promote_payload_atom(persistent, persistent, source_atom,
                                     space_map, state_map,
                                     promoted_spaces, promoted_states,
                                     owner_epoch, export_owner_epoch,
                                     alias_fault);
        if (!promoted_atom)
            return NULL;
        if (!space_admit_atom(stable, persistent, promoted_atom)) {
            Atom *stored = space_store_atom(stable, persistent, promoted_atom);
            if (!stored)
                return NULL;
            space_add(stable, stored);
        }
    }
    eval_track_new_space(stable);
    return stable;
}

static StateCell *rho_promote_payload_state(Arena *persistent,
                                            StateCell *cell,
                                            const RhoOverlayMap *space_map,
                                            const RhoStateCloneMap *state_map,
                                            RhoPromotedMap *promoted_spaces,
                                            RhoPromotedMap *promoted_states,
                                            uint64_t owner_epoch,
                                            uint64_t export_owner_epoch,
                                            bool *alias_fault) {
    StateCell *visible = cell;
    StateCell *stable;

    if (!persistent || !cell)
        return NULL;
    if (state_map) {
        StateCell *clone = rho_state_clone_for(state_map, cell);
        if (clone)
            visible = clone;
    }
    stable = rho_promoted_for(promoted_states, visible,
                                    export_owner_epoch, alias_fault);
    if (stable)
        return stable;

    RhoStateObservation observation;
    if (!rho_state_observation_acquire(visible, &observation))
        return NULL;
    StateCellSnapshot view = rho_state_observation_view(visible, &observation);

    stable = arena_alloc(persistent, sizeof(StateCell));
    if (!stable)
        goto fail;
    stable->value = NULL;
    stable->content_type = NULL;
    stable->payload_owner_epoch = 0;
    stable->payload_export_owner_epoch = export_owner_epoch;
    if (!rho_promoted_state_bind(promoted_states, visible, stable,
                                 export_owner_epoch, alias_fault))
        goto fail;
    if (view.value) {
        stable->value =
            rho_promote_payload_atom(persistent, persistent, view.value,
                                     space_map, state_map,
                                     promoted_spaces, promoted_states,
                                     owner_epoch, export_owner_epoch,
                                     alias_fault);
        if (!stable->value)
            goto fail;
    }
    if (view.content_type) {
        stable->content_type =
            rho_promote_payload_atom(persistent, persistent,
                                     view.content_type,
                                     space_map, state_map,
                                     promoted_spaces, promoted_states,
                                     owner_epoch, export_owner_epoch,
                                     alias_fault);
        if (!stable->content_type)
            goto fail;
    }
    rho_state_observation_release(&observation);
    return stable;

fail:
    rho_state_observation_release(&observation);
    return NULL;
}

#ifdef CETTA_TEST_HOOKS
#pragma weak state_cell_image_prepare
#pragma weak state_cell_managed_new
#pragma weak state_cell_creator_release
#pragma weak state_cell_publish_prepared
#pragma weak state_cell_reclaim_bounded
#pragma weak state_cell_reclamation_due
#pragma weak state_cell_diagnostics

static StateCell *rho_test_managed_state(Arena *source, Atom *value, int64_t type) {
    StateCellImage *image = state_cell_image_prepare(value, atom_int(source, type), false);
    assert(image);
    StateCell *cell = state_cell_managed_new(image);
    assert(cell);
    return cell;
}

static void rho_test_state_drain(void) {
    size_t passes = 0u;
    while (state_cell_reclamation_due()) {
        state_cell_reclaim_bounded(16u);
        assert(++passes < 1000u);
    }
}

static StateCellImage *rho_test_text_replacement;
static void rho_test_replace_observed_state(StateCell *cell) {
    StateCellImage *image = rho_test_text_replacement;
    rho_test_text_replacement = NULL;
    rho_state_observed_test = NULL;
    assert(image && state_cell_publish_prepared(cell, image));
}

bool rhocalc_managed_state_observer_selftest(void (*refuse_allocation)(void)) {
    if (!state_cell_image_prepare || !state_cell_managed_new ||
        !state_cell_creator_release || !state_cell_publish_prepared ||
        !state_cell_reclaim_bounded || !state_cell_reclamation_due ||
        !state_cell_diagnostics || !refuse_allocation)
        return false;
    const StateCellDiagnostics baseline = state_cell_diagnostics();
    Arena source, destination;
    arena_init_detached(&source);
    arena_init_detached(&destination);
    assert(eval_payload_redirects_begin(&destination));
    StateCell *a = rho_test_managed_state(&source, atom_int(&source, 17), 101);
    StateCell *b = rho_test_managed_state(&source, atom_int(&source, 19), 102);
    Atom *pair = atom_expr3(&source, atom_symbol(&source, "captured-pair"),
        atom_state(&source, a), atom_state(&source, a));
    StateCell *outer = rho_test_managed_state(&source, pair, 103);
    Atom *root = atom_state(&source, outer);
    RhoStateCloneMap states = {0};
    RhoOverlayMap spaces = {0};
    assert(rho_collect_state_ptrs_into_map(&states, root) && states.len == 2);
    assert(!rho_state_clone_for(&states, b));
    StateCellImage *replacement = state_cell_image_prepare(atom_state(&source, b),
        atom_int(&source, 104), false);
    assert(replacement && state_cell_publish_prepared(outer, replacement));
    replacement = state_cell_image_prepare(atom_int(&source, 47),
        atom_int(&source, 105), false);
    assert(replacement && state_cell_publish_prepared(a, replacement));
    arena_free(&source);
    state_cell_creator_release(a);
    state_cell_creator_release(b);
    state_cell_creator_release(outer);
    rho_test_state_drain();
    /* The map is now the only native identity holder of the old topology. */
    assert(state_cell_is_managed(outer) && state_cell_is_managed(a));
    assert(rho_state_clone_map_materialize(&states, &destination, &spaces, 37u));
    StateCell *outer_copy = rho_state_clone_for(&states, outer);
    StateCell *a_copy = rho_state_clone_for(&states, a);
    assert(outer_copy && a_copy && outer_copy->value->kind == ATOM_EXPR &&
           outer_copy->value->expr.len == 3u &&
           outer_copy->value->expr.elems[1]->ground.ptr == a_copy &&
           outer_copy->value->expr.elems[2]->ground.ptr == a_copy &&
           outer_copy->content_type->ground.ival == 103 &&
           a_copy->value->ground.ival == 17 && a_copy->content_type->ground.ival == 101 &&
           !rho_state_clone_for(&states, b));
    rho_state_clone_map_free(&states);
    rho_test_state_drain();
    assert(!state_cell_is_managed(outer) && !state_cell_is_managed(a) &&
           !state_cell_is_managed(b));
    assert(a_copy->value->ground.ival == 17);
    eval_payload_redirects_end();
    arena_free(&destination);

    arena_init_detached(&source);
    arena_init_detached(&destination);
    assert(eval_payload_redirects_begin(&destination));
    StateCell *self = state_cell_managed_new(NULL);
    assert(self);
    Atom *self_ref = atom_state(&source, self);
    replacement = state_cell_image_prepare(self_ref, atom_int(&source, 1), false);
    assert(replacement && state_cell_publish_prepared(self, replacement));
    assert(rho_collect_state_ptrs_into_map(&states, self_ref) && states.len == 1);
    assert(rho_state_clone_map_materialize(&states, &destination, &spaces, 39u));
    StateCell *self_copy = rho_state_clone_for(&states, self);
    assert(self_copy && self_copy->value->ground.gkind == GV_STATE &&
           self_copy->value->ground.ptr == self_copy);
    replacement = state_cell_image_prepare(atom_int(&source, 23), atom_int(&source, 1), false);
    assert(replacement && state_cell_publish_prepared(self, replacement));
    rho_state_clone_map_free(&states);
    arena_free(&source);
    state_cell_creator_release(self);
    rho_test_state_drain();
    eval_payload_redirects_end();
    arena_free(&destination);

    arena_init_detached(&source);
    arena_init_detached(&destination);
    StateCell *promoted_source = rho_test_managed_state(&source,
        atom_string(&source, "retained-promotion"), 107);
    rho_test_text_replacement = state_cell_image_prepare(
        atom_string(&source, "new-current"), atom_int(&source, 107), false);
    assert(rho_test_text_replacement);
    rho_state_observed_test = rho_test_replace_observed_state;
    {
        /* The order reads a state through the snapshot it pinned, whatever
         * is published while it reads. */
        StateCell expected = {0};
        expected.value = atom_string(&source, "retained-promotion");
        assert(rho_order_atom(atom_state(&source, promoted_source), ')',
                              atom_state(&source, &expected), ')', true) == 0);
    }
    assert(!rho_state_observed_test && !rho_test_text_replacement);
    RhoPromotedMap promoted_spaces = {0};
    RhoPromotedMap promoted_states = {0};
    bool alias_fault = false;
    StateCell *promoted = rho_promote_payload_state(&destination, promoted_source,
        &spaces, &states, &promoted_spaces, &promoted_states, 41u, 1u, &alias_fault);
    assert(promoted && promoted != promoted_source && !alias_fault);
    arena_free(&source);
    state_cell_creator_release(promoted_source);
    rho_test_state_drain();
    assert(state_cell_is_managed(promoted_source));
    assert(strcmp(promoted->value->ground.sval, "new-current") == 0 &&
           promoted->content_type->ground.ival == 107);
    rho_promoted_map_free(&promoted_states);
    rho_promoted_map_free(&promoted_spaces);
    rho_test_state_drain();
    assert(!state_cell_is_managed(promoted_source));
    assert(strcmp(promoted->value->ground.sval, "new-current") == 0);
    arena_free(&destination);

    arena_init_detached(&source);
    StateCell *failed = rho_test_managed_state(&source, atom_int(&source, 17), 109);
    root = atom_state(&source, failed);
    refuse_allocation();
    assert(!rho_collect_state_ptrs_into_map(&states, root) && states.len == 0);
    rho_state_clone_map_free(&states);
    arena_free(&source);
    state_cell_creator_release(failed);
    rho_test_state_drain();
    StateCellDiagnostics final = state_cell_diagnostics();
    assert(final.nodes == baseline.nodes && final.images == baseline.images &&
           final.image_bytes == baseline.image_bytes);
    return true;
}
#endif

static Atom *rho_promote_payload_atom(Arena *arena,
                                      Arena *persistent,
                                      Atom *atom,
                                      const RhoOverlayMap *space_map,
                                      const RhoStateCloneMap *state_map,
                                      RhoPromotedMap *promoted_spaces,
                                      RhoPromotedMap *promoted_states,
                                      uint64_t owner_epoch,
                                      uint64_t export_owner_epoch,
                                      bool *alias_fault) {
    Atom **elems;
    if (!arena || !persistent || !atom)
        return NULL;
    if (atom->kind == ATOM_GROUNDED) {
        if (atom->ground.gkind == GV_SPACE) {
            Space *orig = (Space *)atom->ground.ptr;
            Space *visible = orig;
            Space *overlay = space_map ? rho_overlay_for(space_map, orig) : NULL;
            if (overlay)
                visible = overlay;
            if (overlay || (visible && (visible->overlay_base ||
                                        visible->payload_owner_epoch == owner_epoch))) {
                Space *stable =
                    rho_promote_payload_space(persistent, visible,
                                              space_map, state_map,
                                              promoted_spaces, promoted_states,
                                              owner_epoch,
                                              export_owner_epoch,
                                              alias_fault);
                return stable ? atom_space(arena, stable) : NULL;
            }
        } else if (atom->ground.gkind == GV_STATE) {
            StateCell *orig = (StateCell *)atom->ground.ptr;
            StateCell *visible = orig;
            StateCell *clone =
                state_map ? rho_state_clone_for(state_map, orig) : NULL;
            if (clone)
                visible = clone;
            if (clone || (visible &&
                          visible->payload_owner_epoch == owner_epoch)) {
                StateCell *stable =
                    rho_promote_payload_state(persistent, visible,
                                              space_map, state_map,
                                              promoted_spaces, promoted_states,
                                              owner_epoch,
                                              export_owner_epoch,
                                              alias_fault);
                return stable ? atom_state(arena, stable) : NULL;
            }
        }
        return atom_deep_copy(arena, atom);
    }
    if (atom->kind == ATOM_SYMBOL)
        return atom_symbol_id(arena, atom->sym_id);
    if (atom->kind == ATOM_VAR)
        return atom_var_like(arena, atom, atom->var_id);
    if (atom->kind != ATOM_EXPR)
        return atom_deep_copy(arena, atom);
    elems = arena_alloc(arena, sizeof(Atom *) * atom->expr.len);
    if (!elems)
        return NULL;
    for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
        elems[i] = rho_promote_payload_atom(arena, persistent,
                                            atom->expr.elems[i],
                                            space_map, state_map,
                                            promoted_spaces, promoted_states,
                                            owner_epoch,
                                            export_owner_epoch,
                                            alias_fault);
        if (!elems[i])
            return NULL;
    }
    return atom_expr(arena, elems, atom->expr.len);
}

/* Evaluate a deferred payload against a sibling-isolated snapshot of the
 * mutable environment. Shared spaces are rebound to per-payload overlays, and
 * explicitly reachable state cells are rebound to payload-owned clones. Reads
 * see the frozen shared bases plus local scratch; writes stay local to the
 * payload and never escape to a sibling. Escaping local spaces are materialized
 * as owned results; shared bases are never implicitly committed. */
static bool rho_eval_payload_results(Arena *arena,
                                     const RhocalcEvalContext *eval_context,
                                     Atom *payload_expr,
                                     ResultSet *out) {
    RhoOverlayMap map = {0};
    RhoStateCloneMap state_map = {0};
    RhoPromotedMap promoted_spaces = {0};
    RhoPromotedMap promoted_states = {0};
    Registry local_registry = {0};
    Arena *persistent = NULL;
    CettaLibraryContext *prev_library_context;
    Space *self_overlay;
    uint64_t owner_epoch = 0;
    Atom *payload;
    bool alias_fault = false;
    bool redirects_started = false;

    if (!arena || !eval_context || !eval_context->space || !payload_expr || !out) {
        return false;
    }

    payload = rho_unquote_payload_atom(payload_expr);

    persistent = eval_context->persistent_arena
        ? eval_context->persistent_arena
        : eval_current_persistent_arena();
    if (!persistent)
        persistent = arena;
    owner_epoch = eval_next_payload_owner_epoch();
    if (!eval_payload_redirects_begin(persistent))
        goto fail;
    redirects_started = true;

    /* Overlay the self space, every registry-bound space, and every baked space
     * literal in the payload term. State cells are rebound separately below. */
    if (!rho_overlay_map_add(&map, eval_context->space))
        goto fail;
    if (eval_context->registry) {
        for (uint32_t i = 0; i < eval_context->registry->len; i++) {
            Atom *v = eval_context->registry->entries[i].value;
            if (v && v->kind == ATOM_GROUNDED && v->ground.gkind == GV_SPACE) {
                if (!rho_overlay_map_add(&map, (Space *)v->ground.ptr))
                    goto fail;
            }
            if (v && !rho_collect_state_ptrs_into_map(&state_map, v))
                goto fail;
        }
    }
    if (!rho_collect_space_ptrs_into_map(&map, payload))
        goto fail;
    if (!rho_collect_state_ptrs_into_map(&state_map, payload))
        goto fail;
    for (int i = 0; i < state_map.len; i++) {
        StateCell *cell = state_map.orig[i];
        if (cell) {
            StateCellSnapshot view = rho_state_clone_view(&state_map, i);
            if (!rho_collect_space_ptrs_into_map(&map, view.value))
                goto fail;
            if (!rho_collect_space_ptrs_into_map(&map, view.content_type))
                goto fail;
        }
    }

    self_overlay = rho_overlay_for(&map, eval_context->space);

    if (!rho_state_clone_map_materialize(&state_map, persistent, &map,
                                         owner_epoch))
        goto fail;
    for (int i = 0; i < map.len; i++) {
        if (!rho_overlay_map_bind_pair(&map, map.orig[i], map.overlay[i]) ||
            !eval_payload_note_space_redirect(map.orig[i], map.overlay[i]))
            goto fail;
    }
    for (int i = 0; i < state_map.len; i++) {
        if (!rho_state_clone_map_bind_pair(&state_map, state_map.orig[i],
                                           state_map.clone[i]) ||
            !eval_payload_note_state_redirect(state_map.orig[i],
                                              state_map.clone[i]))
            goto fail;
    }
    if (!rho_eval_registry_init(&local_registry, eval_context->registry, &map,
                                &state_map, self_overlay, arena))
        goto fail;

    payload = rho_atom_rebind_resources(arena, payload, &map, &state_map);
    if (!payload)
        goto fail;

    prev_library_context = eval_current_library_context();
    if (eval_context->library_context != prev_library_context) {
        eval_set_library_context(eval_context->library_context);
    }
    {
        bool prev_transactional = eval_set_payload_transactional(true);
        uint64_t prev_epoch = eval_set_payload_owner_epoch(owner_epoch);
        eval_top_with_registry(self_overlay, arena, persistent, &local_registry,
                               payload, out);
        eval_set_payload_owner_epoch(prev_epoch);
        eval_set_payload_transactional(prev_transactional);
    }
    if (eval_context->library_context != prev_library_context) {
        eval_set_library_context(prev_library_context);
    }

    for (CettaCount i = 0; i < eval_payload_space_redirect_count(); i++) {
        Space *orig = NULL;
        Space *redirect = NULL;
        if (!eval_payload_space_redirect_at(i, &orig, &redirect) ||
            !rho_overlay_map_bind_pair(&map, orig, redirect))
            goto fail;
    }
    for (CettaCount i = 0; i < eval_payload_state_redirect_count(); i++) {
        StateCell *orig = NULL;
        StateCell *redirect = NULL;
        if (!eval_payload_state_redirect_at(i, &orig, &redirect) ||
            !rho_state_clone_map_bind_pair(&state_map, orig, redirect))
            goto fail;
    }
    for (CettaCount r = 0; r < out->len; r++) {
        out->items[r] = rho_atom_rebind_resources(arena, out->items[r], &map,
                                                  &state_map);
        if (!out->items[r])
            goto fail;
    }

    for (CettaCount r = 0; r < out->len; r++) {
        out->items[r] =
            rho_promote_payload_atom(arena, persistent, out->items[r],
                                     &map, &state_map,
                                     &promoted_spaces, &promoted_states,
                                     owner_epoch,
                                     (uint64_t)r + 1u,
                                     &alias_fault);
        if (!out->items[r]) {
            if (alias_fault) {
                if (eval_context->payload_failure)
                    *eval_context->payload_failure =
                        RHOCALC_PAYLOAD_FAILURE_OWNED_EXPORT_ALIASED;
                rho_validation_set("PayloadOwnedExportAliased");
            }
            goto fail;
        }
    }

    rho_promoted_map_free(&promoted_spaces);
    rho_promoted_map_free(&promoted_states);
    rho_overlay_map_free(&map);
    rho_state_clone_map_free(&state_map);
    registry_free(&local_registry);
    eval_payload_redirects_end();
    return true;

fail:
    rho_promoted_map_free(&promoted_spaces);
    rho_promoted_map_free(&promoted_states);
    rho_overlay_map_free(&map);
    rho_state_clone_map_free(&state_map);
    registry_free(&local_registry);
    if (redirects_started)
        eval_payload_redirects_end();
    return false;
}

static Atom *rho_embed_payload_result_name(Arena *arena, Atom *result) {
    if (!arena || !result) return NULL;
    if (rhocalc_process_well_formed(result, RHOCALC_SEMANTIC_PROFILE_RHOMETTA)) {
        return rho_unary(arena, "rho:quote", rho_normalize_proc(arena, result));
    }
    return rho_unary(arena, "rho:quote", rho_value_proc(arena, result));
}

/* ── COMM continuations and successor emission ──────────────────────────────
 *
 * A matched send/receive pair yields the receiver body with the sent name
 * substituted in.  An ordinary payload yields exactly one continuation; a
 * deferred rhometta payload is evaluated here, at the COMM, and yields one
 * continuation per MeTTa result — nondeterministic evaluation becomes a
 * branching reaction. */

static bool rho_compute_comm_continuations(Arena *arena,
                                           const RhoEndpoint *send_endpoint,
                                           const RhoEndpoint *recv_endpoint,
                                           const RhocalcEvalContext *eval_context,
                                           RhoAtomVec *out_bodies) {
    RhoView send = send_endpoint->view;
    RhoView recv = recv_endpoint->view;

    if (!arena || !send_endpoint || !recv_endpoint || !out_bodies ||
        send.kind != RHO_SEND || send.nargs != 2 ||
        (recv.kind != RHO_RECV &&
         recv.kind != RHO_RECV_PERSISTENT) || recv.nargs != 3 ||
        recv.args[1]->kind != ATOM_VAR) {
        return false;
    }
    if (send.args[1] && rho_view(send.args[1]).kind == RHO_EVAL_PAYLOAD) {
        ResultSet payload_results;
        result_set_init(&payload_results);
        if (!eval_context) {
            result_set_free(&payload_results);
            return true;
        }
        if (!rho_eval_payload_results(arena, eval_context,
                                      rho_view(send.args[1]).args[0],
                                      &payload_results)) {
            result_set_free(&payload_results);
            return false;
        }
        for (CettaCount i = 0; i < payload_results.len; i++) {
            Atom *replacement =
                rho_embed_payload_result_name(arena, payload_results.items[i]);
            Atom *body;
            if (!replacement) {
                result_set_free(&payload_results);
                return false;
            }
            body = rho_subst_proc(arena, recv.args[2], recv.args[1]->var_id,
                                  replacement);
            if (!body || !rho_vec_push(out_bodies, body)) {
                result_set_free(&payload_results);
                return false;
            }
        }
        result_set_free(&payload_results);
        return true;
    }

    Atom *replacement =
        rho_unary(arena, "rho:quote", rho_normalize_proc(arena, send.args[1]));
    Atom *body = rho_subst_proc(arena, recv.args[2], recv.args[1]->var_id,
                                replacement);
    return body && rho_vec_push(out_bodies, body);
}

static bool rho_emit_comm_results(Arena *arena, RhoAtomVec *components,
                                  const RhoEndpoint *send_endpoint,
                                  const RhoEndpoint *recv_endpoint,
                                  const RhocalcEvalContext *eval_context,
                                  RhoSuccessorSetAcc *out) {
    RhoAtomVec bodies;
    bool ok = true;

    if (!arena || !components || !send_endpoint || !recv_endpoint || !out) {
        return false;
    }

    rho_vec_init(&bodies);
    if (!rho_compute_comm_continuations(arena, send_endpoint, recv_endpoint,
                                        eval_context, &bodies)) {
        rho_vec_free(&bodies);
        return false;
    }
    for (uint32_t i = 0; i < bodies.len && ok; i++) {
        Atom *result = rho_rebuild_reaction(arena, components,
                                            send_endpoint->component_index,
                                            recv_endpoint->component_index,
                                            bodies.items[i]);
        if (!result || !rho_successor_set_acc_push(out, result)) {
            ok = false;
            break;
        }
    }
    rho_vec_free(&bodies);
    return ok;
}

/* ── Sequential machine and canonical successor selection ───────────────────
 *
 * The sequential machine rebuilds its COMM index (components, sends,
 * receives) from the residual each round.  The canonical scheduler always
 * fires the least successor in the structural order; it compares each
 * candidate against the least so far instead of materializing and sorting
 * the whole frontier.  The rotating scheduler cycles through the frontier for
 * fairness probes. */

static bool rho_runtime_profile_supported(const RhoRuntimeProfile *profile) {
    if (!profile || profile->reduction_limit == 0u) return false;
    if (profile->threaded && profile->thread_count == 0u) {
        rho_validation_set("rho threaded execution requires at least one worker");
        return false;
    }
    if (profile->thread_count > 1024u) {
        rho_validation_set("rho thread count is out of range");
        return false;
    }
    if (profile->scheduler_policy != RUN_SCHEDULE_CANONICAL &&
        profile->scheduler_policy != RUN_SCHEDULE_ROTATING) {
        rho_validation_set("rho scheduler policy is not implemented");
        return false;
    }
    return true;
}

/* A process arena opened for its next generation of the current process. */
static void rho_process_arena_open(Arena *arena) {
    arena_init(arena);
    arena_set_runtime_kind(arena, CETTA_ARENA_RUNTIME_KIND_SURVIVOR);
}

static void rho_machine_init(RhoMachine *machine, Arena *arena,
                             const RhoRuntimeProfile *profile,
                             const RhocalcEvalContext *eval_context) {
    rho_process_arena_open(&machine->process_arenas[0]);
    rho_process_arena_open(&machine->process_arenas[1]);
    machine->active_process_arena = 0u;
    machine->arena = &machine->process_arenas[0];
    machine->result_arena = arena;
    machine->reductions_since_compaction = 0u;
    machine->process_arenas_initialized = true;
    machine->compaction_safe =
        !eval_context || eval_context->persistent_arena != NULL;
    machine->channel_hashes = (RhoHashMemo){0};
    machine->profile = *profile;
    machine->eval_context = eval_context;
    machine->current = NULL;
    machine->rotating_turn = 0u;
    rho_vec_init(&machine->components);
    rho_endpoint_vec_init(&machine->sends);
    rho_endpoint_vec_init(&machine->recvs);
    machine->comm_index_loaded = false;
    machine->current_canonical_order = false;
}

static void rho_machine_clear_comm_index(RhoMachine *machine) {
    rho_endpoint_vec_free(&machine->sends);
    rho_endpoint_vec_free(&machine->recvs);
    rho_vec_free(&machine->components);
    rho_vec_init(&machine->components);
    rho_endpoint_vec_init(&machine->sends);
    rho_endpoint_vec_init(&machine->recvs);
    machine->comm_index_loaded = false;
}

static void rho_machine_free(RhoMachine *machine) {
    rho_machine_clear_comm_index(machine);
    rho_hash_memo_free(&machine->channel_hashes);
    if (machine->process_arenas_initialized) {
        arena_free(&machine->process_arenas[0]);
        arena_free(&machine->process_arenas[1]);
        machine->process_arenas_initialized = false;
    }
    machine->arena = NULL;
    machine->result_arena = NULL;
    machine->eval_context = NULL;
    machine->current = NULL;
}

static bool rho_machine_load_process(RhoMachine *machine, Atom *proc) {
    bool ok = rhocalc_process_well_formed(proc, machine && machine->eval_context
                                                    ? RHOCALC_SEMANTIC_PROFILE_RHOMETTA
                                                    : RHOCALC_SEMANTIC_PROFILE_STRICT_CORE);
    if (!ok) return false;
    machine->current = rho_normalize_proc(machine->arena, proc);
    machine->current_canonical_order = machine->current != NULL;
    return true;
}

#define RHO_MACHINE_COMPACTION_INTERVAL 64u

static bool rho_machine_compaction_enabled(const RhoMachine *machine) {
    return machine && machine->compaction_safe &&
           !getenv("CETTA_RHO_NO_GENERATION_COMPACTION");
}

static bool rho_machine_compact_current(RhoMachine *machine) {
    uint32_t next_index;
    Arena *next_arena;
    Arena *old_arena;
    Atom *copy;

    if (!machine || !machine->current ||
        !machine->process_arenas_initialized) {
        return false;
    }
    next_index = 1u - machine->active_process_arena;
    next_arena = &machine->process_arenas[next_index];
    old_arena = &machine->process_arenas[machine->active_process_arena];
    copy = atom_deep_copy(next_arena, machine->current);
    if (!copy) return false;
    /* The old generation's atoms are about to go. */
    rho_hash_memo_free(&machine->channel_hashes);
    machine->current = copy;
    machine->active_process_arena = next_index;
    machine->arena = next_arena;
    machine->reductions_since_compaction = 0u;
    /* The old generation ends here; its struct is opened again at once as
     * the next compaction's destination. */
    arena_free(old_arena);
    rho_process_arena_open(old_arena);
    return true;
}

static Atom *rho_machine_export_current(RhoMachine *machine) {
    Atom *copy;

    if (!machine || !machine->result_arena || !machine->current) return NULL;
    copy = atom_deep_copy(machine->result_arena, machine->current);
    return copy ? rho_normalize_proc(machine->result_arena, copy) : NULL;
}

/* The components of the successor a COMM leaves, in canonical order: the
 * current components but the send and (unless it persists) the receive,
 * with the continuation's components merged in at their places.  The
 * current components are in canonical order already, and a current
 * component stays before an equal continuation component, so this is the
 * order rho_rebuild_reaction's sort gives. */
static bool rho_reaction_components(RhoAtomVec *components,
                                    uint32_t send_index, uint32_t recv_index,
                                    Atom *body, RhoAtomVec *out) {
    RhoView body_view = rho_view(body);
    Atom *single = body;
    Atom **items = &single;
    Atom **sorted = NULL;
    uint32_t items_len = 0u;
    uint32_t base = 0u;
    bool retain_recv =
        rho_view(components->items[recv_index]).kind == RHO_RECV_PERSISTENT;

    out->len = 0u;
    if (body_view.kind == RHO_PAR) {
        items_len = body_view.nargs;
        if (items_len > 1u) {
            sorted = cetta_malloc(sizeof(Atom *) * items_len);
            memcpy(sorted, body_view.args, sizeof(Atom *) * items_len);
            rho_sort_atoms(sorted, items_len, rho_proc_order);
            items = sorted;
        } else {
            items = body_view.args;
        }
    } else if (body_view.kind != RHO_NIL) {
        items_len = 1u;
    }
    for (uint32_t k = 0u; k <= items_len; k++) {
        uint32_t upto = components->len;
        if (k < items_len) {
            /* The first current component above this one: equal ones stay
             * before it. */
            uint32_t lo = base;
            uint32_t hi = components->len;
            while (lo < hi) {
                uint32_t mid = lo + (hi - lo) / 2u;
                if (rho_proc_order(components->items[mid], items[k]) <= 0)
                    lo = mid + 1u;
                else
                    hi = mid;
            }
            upto = lo;
        }
        for (uint32_t i = base; i < upto; i++) {
            if (i == send_index || (i == recv_index && !retain_recv)) continue;
            if (!rho_vec_push(out, components->items[i])) {
                free(sorted);
                return false;
            }
        }
        base = upto;
        if (k < items_len && !rho_vec_push(out, items[k])) {
            free(sorted);
            return false;
        }
    }
    free(sorted);
    return true;
}

/* Canonical execution needs the least successor, whereas the public frontier
 * API must retain every successor.  Compare candidates in a resettable arena
 * and materialize only the selected successor persistently.  This remains
 * exact for deferred payloads: each candidate is evaluated against its
 * isolated snapshot, and the selected continuation is retained without
 * re-evaluation before it is committed.  Of equal candidates the first met
 * (in recv/send/body enumeration order) is kept, as
 * rho_successor_set_acc_finish keeps it. */
static bool rho_machine_collect_normalized_components(
    RhoMachine *machine, RhoAtomVec *out, bool *out_normalized);

static bool rho_machine_select_streaming_canonical_successor(
    RhoMachine *machine, Atom **out_next, bool *out_quiescent) {
    Arena scratch;
    Arena selected;
    RhoAtomVec components;
    RhoAtomVec candidate;
    RhoAtomVec best;
    RhoEndpointVec sends;
    RhoEndpointVec recvs;
    uint32_t recv_pos = 0u;
    uint32_t send_pos = 0u;
    uint32_t best_recv = 0u;
    uint32_t best_send = 0u;
    Atom *best_body = NULL;
    bool found = false;
    bool ok = true;

    if (!machine || !machine->arena || !machine->current ||
        !out_next || !out_quiescent) {
        return false;
    }

    *out_next = NULL;
    *out_quiescent = true;
    arena_init(&scratch);
    arena_init(&selected);
    rho_vec_init(&components);
    rho_vec_init(&candidate);
    rho_vec_init(&best);
    rho_endpoint_vec_init(&sends);
    rho_endpoint_vec_init(&recvs);
    if (!machine->current_canonical_order) {
        machine->current = rho_normalize_proc(
            machine->arena, machine->current);
        if (!machine->current) {
            ok = false;
            goto done;
        }
        machine->current_canonical_order = true;
    }
    {
        bool normalized = false;
        if (!rho_machine_collect_normalized_components(
                machine, &components, &normalized) || !normalized) {
            ok = false;
            goto done;
        }
    }
    if (!rho_collect_endpoints(&components, &sends, &recvs)) {
        ok = false;
        goto done;
    }

    uint32_t recv_start = 0u, send_start = 0u;
    while (ok && rho_next_comm_run(&recvs, &sends, &recv_pos, &send_pos,
                                   &recv_start, &send_start)) {
        for (uint32_t r = recv_start; r < recv_pos && ok; r++) {
            for (uint32_t s = send_start; s < send_pos && ok; s++) {
                ArenaMark mark = arena_mark(&scratch);
                RhoAtomVec bodies;
                uint32_t send_index = sends.items[s].component_index;
                uint32_t recv_index = recvs.items[r].component_index;
                rho_vec_init(&bodies);
                if (!rho_compute_comm_continuations(
                        &scratch, &sends.items[s], &recvs.items[r],
                        machine->eval_context, &bodies)) {
                    ok = false;
                }
                for (uint32_t b = 0u; b < bodies.len && ok; b++) {
                    if (!rho_reaction_components(&components, send_index,
                                                 recv_index, bodies.items[b],
                                                 &candidate)) {
                        ok = false;
                        break;
                    }
                    if (found &&
                        rho_items_order(candidate.items, candidate.len,
                                        best.items, best.len) >= 0) {
                        continue;
                    }
                    arena_free(&selected);
                    arena_init(&selected);
                    best_body = atom_deep_copy(&selected, bodies.items[b]);
                    if (!best_body ||
                        !rho_reaction_components(&components, send_index,
                                                 recv_index, best_body,
                                                 &best)) {
                        ok = false;
                        break;
                    }
                    best_recv = r;
                    best_send = s;
                    found = true;
                }
                rho_vec_free(&bodies);
                arena_reset(&scratch, mark);
            }
        }
    }

    if (ok && found) {
        Atom *committed_body = atom_deep_copy(machine->arena, best_body);
        ok = committed_body &&
             rho_reaction_components(&components,
                                     sends.items[best_send].component_index,
                                     recvs.items[best_recv].component_index,
                                     committed_body, &best);
        if (ok) {
            *out_next = rho_par_from_vec_in_order(machine->arena, &best);
            ok = *out_next != NULL;
            machine->current_canonical_order = true;
            *out_quiescent = false;
        }
    } else if (ok) {
        *out_next = machine->current;
    }

done:
    rho_endpoint_vec_free(&sends);
    rho_endpoint_vec_free(&recvs);
    rho_vec_free(&components);
    rho_vec_free(&candidate);
    rho_vec_free(&best);
    arena_free(&selected);
    arena_free(&scratch);
    return ok;
}

/* machine->current is normalized on entry and every committed successor is
 * rebuilt by rho_rebuild_reaction, so its top-level parallel components are
 * already flat.  Avoid recursively rebuilding the unchanged process merely to
 * rediscover that decomposition. */
static bool rho_machine_collect_normalized_components(
    RhoMachine *machine, RhoAtomVec *out, bool *out_normalized) {
    RhoView view;

    if (!machine || !machine->current || !out || !out_normalized) {
        return false;
    }
    *out_normalized = true;
    view = rho_view(machine->current);
    if (view.kind == RHO_NIL) {
        return true;
    }
    if (view.kind != RHO_PAR) {
        return rho_vec_push(out, machine->current);
    }
    for (uint32_t i = 0u; i < view.nargs; i++) {
        RhoView component = rho_view(view.args[i]);
        if (component.kind == RHO_NIL || component.kind == RHO_PAR) {
            *out_normalized = false;
            return true;
        }
        if (!rho_vec_push(out, view.args[i])) {
            return false;
        }
    }
    return true;
}

/* A determinate image commonly exposes exactly one enabled COMM pair.  In
 * that case canonical choice is trivial: evaluate that pair once and avoid
 * constructing, ordering, and discarding a singleton frontier.  Zero
 * or multiple payload results retain the exact public-frontier semantics. */
static bool rho_machine_try_single_comm_successor(
    RhoMachine *machine, Atom **out_next, bool *out_quiescent,
    bool *out_handled) {
    Arena scratch;
    RhoAtomVec components;
    RhoEndpointVec sends;
    RhoEndpointVec recvs;
    const RhoEndpoint *selected_send = NULL;
    const RhoEndpoint *selected_recv = NULL;
    bool normalized = false;
    bool ok = true;

    if (!machine || !machine->arena || !machine->current ||
        !machine->eval_context || !out_next || !out_quiescent ||
        !out_handled) {
        return false;
    }
    *out_handled = false;
    arena_init(&scratch);
    arena_set_runtime_kind(&scratch, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    rho_vec_init(&components);
    rho_endpoint_vec_init(&sends);
    rho_endpoint_vec_init(&recvs);
    if (!rho_machine_collect_normalized_components(
            machine, &components, &normalized)) {
        ok = false;
        goto done;
    }
    if (!normalized) goto done;
    if (!rho_collect_endpoints_for_single_comm(
            &components, &sends, &recvs)) {
        ok = false;
        goto done;
    }

    if (recvs.len > 0u && sends.len > 0u) {
        /* Channels whose hashes differ differ. */
        uint64_t *send_hashes = cetta_malloc(sizeof(uint64_t) * sends.len);
        bool contended = false;
        for (uint32_t s = 0u; s < sends.len; s++)
            send_hashes[s] = rho_name_hash_memo(sends.items[s].view.args[0],
                                                &machine->channel_hashes);
        for (uint32_t r = 0u; r < recvs.len && !contended; r++) {
            uint64_t recv_hash = rho_name_hash_memo(
                recvs.items[r].view.args[0], &machine->channel_hashes);
            for (uint32_t s = 0u; s < sends.len; s++) {
                if (send_hashes[s] != recv_hash ||
                    !rho_names_equal(recvs.items[r].view.args[0],
                                     sends.items[s].view.args[0])) {
                    continue;
                }
                if (selected_send) {
                    contended = true;
                    break;
                }
                selected_recv = &recvs.items[r];
                selected_send = &sends.items[s];
            }
        }
        free(send_hashes);
        if (contended) goto done;
    }

    *out_handled = true;
    if (!selected_send) {
        *out_next = machine->current;
        *out_quiescent = true;
        goto done;
    }

    {
        RhoAtomVec bodies;
        rho_vec_init(&bodies);
        if (!rho_compute_comm_continuations(
                &scratch, selected_send, selected_recv,
                machine->eval_context, &bodies)) {
            rho_vec_free(&bodies);
            ok = false;
            goto done;
        }
        if (bodies.len == 0u) {
            *out_next = machine->current;
            *out_quiescent = true;
        } else if (bodies.len == 1u) {
            Atom *body = atom_deep_copy(machine->arena, bodies.items[0]);
            if (!body) {
                ok = false;
            } else {
                *out_next = rho_rebuild_unique_reaction(
                    machine->arena, &components,
                    selected_send->component_index,
                    selected_recv->component_index, body);
                *out_quiescent = false;
                ok = *out_next != NULL;
                if (ok) machine->current_canonical_order = false;
            }
        } else {
            RhoSuccessorSetAcc acc;
            RhoSuccessorSet successors = {0};
            rho_successor_set_acc_init(&acc);
            for (uint32_t i = 0u; i < bodies.len && ok; i++) {
                Atom *body = atom_deep_copy(
                    machine->arena, bodies.items[i]);
                Atom *result = body
                    ? rho_rebuild_reaction(
                          machine->arena, &components,
                          selected_send->component_index,
                          selected_recv->component_index, body)
                    : NULL;
                if (!body || !result ||
                    !rho_successor_set_acc_push(&acc, result)) {
                    ok = false;
                }
            }
            if (ok) {
                rho_successor_set_acc_finish(&acc, &successors);
                *out_quiescent = successors.len == 0u;
                *out_next = successors.len == 0u
                    ? machine->current : successors.items[0];
                if (successors.len > 0u) {
                    machine->current_canonical_order = true;
                }
                rhocalc_successor_set_free(&successors);
            } else {
                rho_successor_set_acc_free(&acc);
            }
        }
        rho_vec_free(&bodies);
    }

done:
    rho_endpoint_vec_free(&sends);
    rho_endpoint_vec_free(&recvs);
    rho_vec_free(&components);
    arena_free(&scratch);
    return ok;
}

static bool rho_machine_select_canonical_successor(RhoMachine *machine,
                                                   Atom **out_next,
                                                   bool *out_quiescent) {
    RhoSuccessorSet successors = {0};

    if (!machine || !machine->arena || !machine->current ||
        !out_next || !out_quiescent) {
        return false;
    }
    *out_next = NULL;
    *out_quiescent = true;

    if (!getenv("CETTA_RHO_CANONICAL_EXHAUSTIVE_ORACLE")) {
        if (machine->eval_context) {
            bool handled = false;
            if (!rho_machine_try_single_comm_successor(
                    machine, out_next, out_quiescent, &handled)) {
                return false;
            }
            if (handled) {
                return true;
            }
        }
        return rho_machine_select_streaming_canonical_successor(
            machine, out_next, out_quiescent);
    }

    if (!rhocalc_collect_successor_set(machine->arena, machine->current,
                                       machine->eval_context, &successors)) {
        return false;
    }
    *out_quiescent = successors.len == 0;
    *out_next = successors.len == 0 ? machine->current : successors.items[0];
    if (successors.len > 0u) machine->current_canonical_order = true;
    rhocalc_successor_set_free(&successors);
    return true;
}

/* ── Threaded strict-core executor ──────────────────────────────────────────
 *
 * Worker threads run tasks: a process in normal form, held in a region of
 * its own with the name supply it draws from.  A task of a parallel
 * composition spawns its components; a send or a receive is published into
 * its channel's bucket, where a bucket-locked rendezvous pairs one send with
 * one receive, spends one unit of the shared reduction budget, and enqueues
 * one continuation; see the run invariant at RhoAsyncExecutor above.  The
 * final state is the waiting endpoints plus the stuck tasks.
 *
 * Memory is held by tasks, each in a region of its own, as in region-based
 * memory management (Tofte and Talpin 1997): a created task receives a copy
 * of exactly its own term in its region, the region is released whole when
 * the task ends, and a waiting endpoint keeps its region while it
 * waits.  What a task computes while it runs -- a COMM's substitution and
 * payload, the scratch of matching -- is in its worker's scratch arena,
 * reset when the task ends.  So what a run holds is what its live tasks
 * need, however many COMMs it runs.
 *
 * Placement is by the parties: a spawned component draws from its balanced
 * piece of the spawner's supply (the first ceil(k/2) of its k components
 * share the left half and the others the right half, each parted again in
 * the same way, so every piece lies within ceil(log2 k) halvings of the
 * spawner's share), and a COMM's continuation from the join of the
 * sender's and the receiver's supplies as each ran with it, so the run's
 * positions and names depend on its reduction history, never on which
 * party found the match or which worker ran it.  A persistent receive is
 * not consumed: it keeps its supply, and its continuation takes the
 * sender's. */

/* A region is one allocation of this many bytes, its record and its first
 * block: enough for a task's record and a small term (a receive whose
 * continuation is a send needs 632 bytes, a send of a send 416).  A larger
 * term takes blocks that bring the region to powers of two, at least this
 * many bytes each, so it holds what blocks of powers of two would, whatever
 * the first block.  A released region keeps its spare blocks, in order,
 * while they hold less than this many bytes, so less than this and one
 * block more: after a term that grew it by powers of two, 320, 1024 and
 * 2048 bytes, 4096 in all with its first block's 704; a worker
 * keeps this many released regions at hand; a window of the region reuse
 * lasts at least this many task ends, which a worker counts in the run's
 * this many at a time. */
#define RHO_TASK_REGION_BYTES 896u
#define RHO_TASK_REGION_FIRST_BLOCK (RHO_TASK_REGION_BYTES - sizeof(RhoTaskRegion))
#define RHO_TASK_REGION_LEAST_BLOCK 256u
#define RHO_TASK_REGION_KEEP_BYTES 2048u
#define RHO_TASK_REGION_STOCK_MAX 16u
#define RHO_TASK_REGION_WINDOW_MIN 1024u
#define RHO_TASK_REGION_COUNT_EVERY 64u

/* A test's hooks and the census of the last run, for the runs of the
   thread that set them. */
static _Thread_local RhocalcThreadedTestHooks g_rho_threaded_test_hooks;
static _Thread_local RhocalcThreadedCensus g_rho_threaded_census;

void rhocalc_threaded_test_hooks_set(const RhocalcThreadedTestHooks *hooks) {
    g_rho_threaded_test_hooks =
        hooks ? *hooks : (RhocalcThreadedTestHooks){0};
}

void rhocalc_threaded_last_census(RhocalcThreadedCensus *out) {
    if (out)
        *out = g_rho_threaded_census;
}

static void rho_async_fail(RhoAsyncExecutor *executor, const char *fmt, ...) {
    va_list ap;
    char message[256];

    if (!executor) return;
    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);
    cetta_parallel_executor_fail(&executor->parallel, "%s", message);
}

/* ── Copying a term into its region ── */

/* The run's atom for the rho symbol ID, NULL for any other symbol. */
static Atom *rho_head_atom(const RhoHeadAtoms *heads, SymbolId id) {
    if (id == g_rho_syms.send) return heads->send;
    if (id == g_rho_syms.recv) return heads->recv;
    if (id == g_rho_syms.nil) return heads->nil;
    if (id == g_rho_syms.drop) return heads->drop;
    if (id == g_rho_syms.quote) return heads->quote;
    if (id == g_rho_syms.par) return heads->par;
    if (id == g_rho_syms.recv_persistent) return heads->recv_persistent;
    if (id == g_rho_syms.val) return heads->val;
    if (id == g_rho_syms.eval_payload) return heads->eval_payload;
    return NULL;
}

/* ATOM, a process in normal form or a part of one, copied into REGION as a
 * tree: every expression of it is allocated there and a symbol is the
 * symbol table's own atom (a rho symbol's from HEADS, the run's), while the
 * payload of a value or of a deferred payload, and any other leaf, is copied
 * whole (atom_deep_copy, which keeps a payload's sharing and the resources
 * it holds), so the copy refers to nothing another region holds.
 * Normalizing a term rebuilds it as a tree in the same way. */
static Atom *rho_region_copy(const RhoHeadAtoms *heads, Arena *region,
                             Atom *atom) {
    switch (atom->kind) {
    case ATOM_SYMBOL: {
        Atom *canonical = rho_head_atom(heads, atom->sym_id);
        if (!canonical)
            canonical = atom_symbol_canonical(atom->sym_id);
        return canonical ? canonical : atom_symbol_id(region, atom->sym_id);
    }
    case ATOM_VAR:
        return atom_var_like(region, atom, atom->var_id);
    case ATOM_EXPR: {
        CettaExprLen len = atom->expr.len;
        if (len == 0u)
            break;
        Atom *head = atom->expr.elems[0];
        bool payload = len == 2u && head->kind == ATOM_SYMBOL &&
                       (head->sym_id == g_rho_syms.val ||
                        head->sym_id == g_rho_syms.eval_payload);
        Atom *inline_elems[8];
        Atom **elems = len <= 8u ? inline_elems
                                 : cetta_malloc(sizeof(Atom *) * (size_t)len);
        if (!elems)
            return NULL;
        bool ok = true;
        for (CettaExprLen i = 0; ok && i < len; i++) {
            Atom *part = atom->expr.elems[i];
            elems[i] = payload && i == 1u
                           ? atom_deep_copy(region, part)
                           : rho_region_copy(heads, region, part);
            ok = elems[i] != NULL;
        }
        Atom *out = ok ? atom_expr(region, elems, len) : NULL;
        if (elems != inline_elems)
            free(elems);
        return out;
    }
    default:
        break;
    }
    return atom_deep_copy(region, atom);
}

/* ── Task regions ── */

/* Region reuse.  A released region keeps its first block, and spare blocks
 * while they hold less than RHO_TASK_REGION_KEEP_BYTES, and is kept: up to
 * RHO_TASK_REGION_STOCK_MAX at hand by the worker that released it, and the
 * rest in the run's pool, from which a worker whose stock is empty takes,
 * so a region is made only when no released one is left.  The pool is
 * counted out in windows of task ends, as many as the regions the run holds
 * (at least RHO_TASK_REGION_WINDOW_MIN).  The regions it held all through a
 * window and the one before -- the lower of the two windows' low-water
 * marks -- were not needed by the tasks live at their peak in either, and
 * are given back when the window closes, beyond enough to refill every
 * worker's stock.  A worker takes from the pool only with its stock empty
 * and gives to it only with its stock full, so while as many tasks are live
 * the pool falls by at most what the stocks can hold: keeping that much, a
 * run whose live tasks hold steady never makes a region again.  So a run
 * keeps regions for the recent peak of its live tasks and holds less again
 * once that peak falls, however long it runs. */

static void rho_task_region_free(RhoAsyncExecutor *executor,
                                 RhoTaskRegion *region) {
    arena_free_lent(&region->arena, region + 1);
    free(region);
    atomic_fetch_add_explicit(&executor->regions_freed, 1u,
                              memory_order_relaxed);
}

/* N more task ends: when they close the window, the pool gives back the
 * regions it held all through this window and the one before. */
static void rho_task_regions_count_ends(RhoAsyncExecutor *executor,
                                        uint64_t n) {
    uint64_t ends = atomic_fetch_add_explicit(&executor->region_ends, n,
                                              memory_order_relaxed) + n;
    uint64_t window_end = atomic_load_explicit(&executor->region_window_end,
                                               memory_order_relaxed);
    if (ends < window_end)
        return;
    uint64_t freed = atomic_load_explicit(&executor->regions_freed,
                                          memory_order_relaxed);
    uint64_t made = atomic_load_explicit(&executor->regions_made,
                                         memory_order_relaxed);
    uint64_t held = made > freed ? made - freed : 0u;
    uint64_t length = held > RHO_TASK_REGION_WINDOW_MIN
                          ? held : RHO_TASK_REGION_WINDOW_MIN;
    if (!atomic_compare_exchange_strong_explicit(
            &executor->region_window_end, &window_end, ends + length,
            memory_order_relaxed, memory_order_relaxed))
        return;   /* another worker closed it */
    RhoTaskRegion *given = NULL;
    pthread_mutex_lock(&executor->region_mutex);
    uint64_t len = atomic_load_explicit(&executor->region_pool_len,
                                        memory_order_relaxed);
    uint64_t idle = executor->region_pool_low_both;
    uint64_t keep = (uint64_t)RHO_TASK_REGION_STOCK_MAX *
                    executor->profile.thread_count;
    uint64_t give_back = idle > keep ? idle - keep : 0u;
    for (uint64_t i = 0; i < give_back; i++) {
        RhoTaskRegion *region = executor->region_pool;
        executor->region_pool = region->next;
        region->next = given;
        given = region;
    }
    len -= give_back;
    atomic_store_explicit(&executor->region_pool_len, len,
                          memory_order_relaxed);
    /* The window that closes is the one before the next: its low-water
     * mark, less what was given back, which it held all through. */
    executor->region_pool_low_both = executor->region_pool_low - give_back;
    executor->region_pool_low = len;
    pthread_mutex_unlock(&executor->region_mutex);
    while (given) {
        RhoTaskRegion *next = given->next;
        rho_task_region_free(executor, given);
        given = next;
    }
}

/* A region for a task: one at hand in STATE's stock, else one from the
 * run's pool, else a new one, whose first block it lends its arena; a
 * release resets the arena to the mark of that block (arena_lent_mark), so
 * the block stays in use.  STATE is NULL on the thread that starts and ends
 * the run. */
static RhoTaskRegion *rho_task_region_take(RhoAsyncExecutor *executor,
                                           RhoAsyncWorkerState *state) {
    RhoTaskRegion *region = state ? state->stock : NULL;
    if (region) {
        state->stock = region->next;
        state->stock_len--;
        region->next = NULL;
        return region;
    }
    if (atomic_load_explicit(&executor->region_pool_len,
                             memory_order_relaxed) != 0u) {
        pthread_mutex_lock(&executor->region_mutex);
        region = executor->region_pool;
        if (region) {
            executor->region_pool = region->next;
            uint64_t len = atomic_load_explicit(&executor->region_pool_len,
                                                memory_order_relaxed) - 1u;
            atomic_store_explicit(&executor->region_pool_len, len,
                                  memory_order_relaxed);
            if (len < executor->region_pool_low)
                executor->region_pool_low = len;
            if (len < executor->region_pool_low_both)
                executor->region_pool_low_both = len;
        }
        pthread_mutex_unlock(&executor->region_mutex);
        if (region) {
            region->next = NULL;
            return region;
        }
    }
    region = cetta_malloc(RHO_TASK_REGION_BYTES);
    if (!region)
        return NULL;
    arena_init_detached(&region->arena);
    arena_set_block_growth_pow2(&region->arena, RHO_TASK_REGION_LEAST_BLOCK);
    arena_lend_block(&region->arena, region + 1, RHO_TASK_REGION_FIRST_BLOCK);
    region->next = NULL;
    atomic_fetch_add_explicit(&executor->regions_made, 1u,
                              memory_order_relaxed);
    return region;
}

/* Release REGION: emptied into STATE's stock or the run's pool, or freed on
   the thread that starts and ends the run (no STATE). */
static void rho_task_region_release(RhoAsyncExecutor *executor,
                                    RhoAsyncWorkerState *state,
                                    RhoTaskRegion *region) {
    if (!region)
        return;
    if (!state) {
        rho_task_region_free(executor, region);
        return;
    }
    if (++state->ends == RHO_TASK_REGION_COUNT_EVERY) {
        state->ends = 0u;
        rho_task_regions_count_ends(executor, RHO_TASK_REGION_COUNT_EVERY);
    }
    arena_reset(&region->arena, arena_lent_mark(region + 1));
    arena_release_spare(&region->arena, RHO_TASK_REGION_KEEP_BYTES);
    if (state->stock_len < RHO_TASK_REGION_STOCK_MAX) {
        region->next = state->stock;
        state->stock = region;
        state->stock_len++;
        return;
    }
    pthread_mutex_lock(&executor->region_mutex);
    region->next = executor->region_pool;
    executor->region_pool = region;
    atomic_store_explicit(
        &executor->region_pool_len,
        atomic_load_explicit(&executor->region_pool_len, memory_order_relaxed) + 1u,
        memory_order_relaxed);
    pthread_mutex_unlock(&executor->region_mutex);
}

static void rho_task_region_list_free(RhoAsyncExecutor *executor,
                                      RhoTaskRegion *region) {
    while (region) {
        RhoTaskRegion *next = region->next;
        rho_task_region_free(executor, region);
        region = next;
    }
}

/* The view of a task's term, rho_view's, read from its head's identity:
 * the run interned the rho symbols when it began. */
static RhoView rho_task_view(Atom *proc) {
    RhoView view = {RHO_BAD, NULL, 0};
    if (proc->kind == ATOM_SYMBOL) {
        if (proc->sym_id == g_rho_syms.nil)
            view.kind = RHO_NIL;
        return view;
    }
    if (proc->kind != ATOM_EXPR || proc->expr.len == 0 ||
        proc->expr.elems[0]->kind != ATOM_SYMBOL)
        return view;
    SymbolId head = proc->expr.elems[0]->sym_id;
    view.args = proc->expr.elems + 1;
    view.nargs = proc->expr.len - 1;
    if (head == g_rho_syms.send) view.kind = RHO_SEND;
    else if (head == g_rho_syms.recv) view.kind = RHO_RECV;
    else if (head == g_rho_syms.par) view.kind = RHO_PAR;
    else if (head == g_rho_syms.drop) view.kind = RHO_DROP;
    else if (head == g_rho_syms.recv_persistent) view.kind = RHO_RECV_PERSISTENT;
    else if (head == g_rho_syms.quote) view.kind = RHO_QUOTE;
    else if (head == g_rho_syms.val) view.kind = RHO_VAL;
    else if (head == g_rho_syms.eval_payload) view.kind = RHO_EVAL_PAYLOAD;
    return view;
}

/* A task of PROC, a process in normal form: a copy of PROC in a region of
 * its own, which holds its record too. */
static RhoTask *rho_task_make(RhoAsyncExecutor *executor,
                              RhoAsyncWorkerState *state, Atom *proc) {
    RhoTaskRegion *region = rho_task_region_take(executor, state);
    if (!region)
        return NULL;
    RhoTask *task = arena_alloc(&region->arena, sizeof(*task));
    Atom *copy = task ? rho_region_copy(&executor->heads, &region->arena, proc)
                      : NULL;
    if (!copy) {
        rho_task_region_release(executor, state, region);
        return NULL;
    }
    *task = (RhoTask){.region = region, .proc = copy, .view = rho_task_view(copy)};
    return task;
}

/* TASK ends: its supply ends, handing on its clocks, and its region is
   released. */
static void rho_task_end(RhoAsyncExecutor *executor, RhoAsyncWorkerState *state,
                         RhoTask *task) {
    if (task->holds_supply) {
        cetta_name_supply_dispose(&task->supply);
        task->holds_supply = false;
    }
    rho_task_region_release(executor, state, task->region);
}

/* TASK, which was never run, ends with the run: its supply is the queue's. */
static void rho_async_discard_task(void *task, void *user) {
    rho_task_end(user, NULL, task);
}

/* TASK keeps the supply it runs with, for when it waits or is stuck. */
static void rho_task_keep_supply(RhoTask *task) {
    task->supply = cetta_name_supply_copy(cetta_name_supply_current());
    task->holds_supply = true;
}

/* ── Channels ── */

static void rho_channel_table_init(RhoChannelTable *table) {
    pthread_mutex_init(&table->mutex, NULL);
    table->head = NULL;
    arena_init_detached(&table->arena);
}

static void rho_channel_table_free(RhoChannelTable *table) {
    RhoChannelBucket *bucket = table->head;
    while (bucket) {
        RhoChannelBucket *next = bucket->next;
        pthread_mutex_destroy(&bucket->mutex);
        free(bucket);
        bucket = next;
    }
    arena_free(&table->arena);
    pthread_mutex_destroy(&table->mutex);
    table->head = NULL;
}

/* The bucket of a channel, by the structural order's equality.  The bucket
 * keeps a copy of the channel in the table's arena, since the endpoint
 * that opened it ends. */
static RhoChannelBucket *rho_channel_table_get_or_create(RhoChannelTable *table,
                                                         const RhoHeadAtoms *heads,
                                                         Atom *channel) {
    RhoChannelBucket *bucket;
    uint64_t hash = rho_name_hash(channel);

    pthread_mutex_lock(&table->mutex);
    for (bucket = table->head; bucket; bucket = bucket->next) {
        if (bucket->hash == hash && rho_names_equal(bucket->channel, channel)) {
            pthread_mutex_unlock(&table->mutex);
            return bucket;
        }
    }

    Atom *copy = rho_region_copy(heads, &table->arena, channel);
    bucket = copy ? cetta_malloc(sizeof(RhoChannelBucket)) : NULL;
    if (!bucket) {
        pthread_mutex_unlock(&table->mutex);
        return NULL;
    }
    bucket->channel = copy;
    bucket->hash = hash;
    pthread_mutex_init(&bucket->mutex, NULL);
    bucket->send_head = NULL;
    bucket->send_tail = NULL;
    bucket->recv_head = NULL;
    bucket->recv_tail = NULL;
    bucket->next = table->head;
    table->head = bucket;
    pthread_mutex_unlock(&table->mutex);
    return bucket;
}

static void rho_async_endpoint_append(RhoTask **head, RhoTask **tail,
                                      RhoTask *task) {
    task->next = NULL;
    if (*tail) {
        (*tail)->next = task;
    } else {
        *head = task;
    }
    *tail = task;
}

static RhoTask *rho_async_endpoint_pop(RhoTask **head, RhoTask **tail) {
    RhoTask *task = *head;
    if (!task) return NULL;
    *head = task->next;
    if (!*head) *tail = NULL;
    task->next = NULL;
    return task;
}

static bool rho_async_try_spend_budget(RhoAsyncExecutor *executor) {
    uint32_t old = atomic_load_explicit(&executor->budget,
                                        memory_order_relaxed);
    while (old != 0) {
        if (atomic_compare_exchange_weak_explicit(&executor->budget, &old,
                                                  old - 1u,
                                                  memory_order_acq_rel,
                                                  memory_order_relaxed)) {
            atomic_fetch_add_explicit(&executor->reductions_taken, 1u,
                                      memory_order_relaxed);
            return true;
        }
    }
    return false;
}

static void rho_async_refund_budget(RhoAsyncExecutor *executor) {
    if (!executor)
        return;
    atomic_fetch_add_explicit(&executor->budget, 1u, memory_order_relaxed);
    atomic_fetch_sub_explicit(&executor->reductions_taken, 1u,
                              memory_order_relaxed);
}

/* Queue TASK to run with *NAMES, which the queue takes over; a task the
 * queue refuses ends at once. */
static bool rho_async_push(RhoAsyncExecutor *executor,
                           RhoAsyncWorkerState *state, RhoTask *task,
                           CettaNameSupply *names) {
    if (cetta_parallel_executor_push_supply(&executor->parallel, task, names))
        return true;
    rho_task_end(executor, state, task);
    return false;
}

/* TASK can never run again: it stays, with its region and supply, until the
 * run ends. */
static void rho_async_add_stuck(RhoAsyncExecutor *executor, RhoTask *task) {
    pthread_mutex_lock(&executor->stuck_mutex);
    task->next = executor->stuck;
    executor->stuck = task;
    pthread_mutex_unlock(&executor->stuck_mutex);
}

/* A task of the parallel composition VIEW, running with SUPPLY, spawns its
 * components, in the normal form's order, each from its balanced piece of
 * SUPPLY. */
static bool rho_async_spawn(RhoAsyncExecutor *executor,
                            RhoAsyncWorkerState *state, const RhoView *view,
                            const CettaNameSupply *supply) {
    CettaNameSupplyPieces pieces;
    cetta_name_supply_pieces_begin(&pieces, supply, view->nargs);
    for (uint32_t i = 0; i < view->nargs; i++) {
        CettaNameSupply piece = cetta_name_supply_pieces_next(&pieces);
        RhoTask *component = rho_task_make(executor, state, view->args[i]);
        if (!component) {
            cetta_name_supply_dispose(&piece);
            cetta_name_supply_pieces_end(&pieces);
            rho_async_fail(executor, "could not allocate a rho parallel component");
            return false;
        }
        if (!rho_async_push(executor, state, component, &piece)) {
            cetta_name_supply_pieces_end(&pieces);
            rho_async_fail(executor, "could not enqueue rho parallel component");
            return false;
        }
    }
    return true;
}

/* The COMM of FINDER, the running task, with WAITING, the endpoint it met.
 * FINDER_KIND is the finder's side.  *KEPT says whether FINDER lives on.
 * Inlined into its one caller, the publish of an endpoint. */
static inline __attribute__((always_inline)) bool rho_async_comm(
    RhoAsyncExecutor *executor, RhoAsyncWorkerState *state, Arena *scratch,
    RhoTask *finder, RhoTask *waiting, RhoAsyncEndpointKind finder_kind,
    bool *kept) {
    RhoTask *sender = finder_kind == RHO_ASYNC_ENDPOINT_SEND ? finder : waiting;
    RhoTask *receiver = finder_kind == RHO_ASYNC_ENDPOINT_SEND ? waiting : finder;
    CettaNameSupply *running = cetta_name_supply_current();
    const CettaNameSupply *sender_supply =
        sender == finder ? running : &waiting->supply;
    const CettaNameSupply *receiver_supply =
        receiver == finder ? running : &waiting->supply;
    bool persistent = receiver->view.kind == RHO_RECV_PERSISTENT;

    /* The continuation's supply: the join of the two parties' supplies as
     * each ran with it, or the sender's alone when the receive persists. */
    CettaNameSupply placed;
    if (executor->hooks.finder_placement)
        placed = cetta_name_supply_fork(running);
    else if (persistent)
        placed = cetta_name_supply_copy(sender_supply);
    else
        placed = cetta_name_supply_join(sender_supply, receiver_supply);

    RhoEndpoint send_endpoint = {0, sender->view};
    RhoEndpoint recv_endpoint = {0, receiver->view};
    RhoAtomVec bodies;
    rho_vec_init(&bodies);
    /* A payload evaluated at the COMM draws from the continuation's supply,
     * as the continuation's first draws. */
    CettaNameSupply *previous = cetta_name_supply_install(&placed);
    bool ok = rho_compute_comm_continuations(scratch, &send_endpoint,
                                             &recv_endpoint,
                                             executor->eval_context, &bodies);
    cetta_name_supply_install(previous);
    if (!ok) {
        rho_vec_free(&bodies);
        cetta_name_supply_dispose(&placed);
        rho_task_end(executor, state, waiting);
        rho_async_fail(executor, "could not compute rho COMM continuation");
        return false;
    }
    if (bodies.len == 0) {
        /* No continuation: both parties are stuck. */
        rho_vec_free(&bodies);
        cetta_name_supply_dispose(&placed);
        rho_async_refund_budget(executor);
        rho_task_keep_supply(finder);
        rho_async_add_stuck(executor, finder);
        rho_async_add_stuck(executor, waiting);
        *kept = true;
        return true;
    }
    uint32_t chosen = 0u;
    if (executor->profile.scheduler_policy == RUN_SCHEDULE_ROTATING &&
        bodies.len > 1u) {
        chosen = atomic_fetch_add_explicit(&executor->branch_turn, 1u,
                                           memory_order_relaxed) % bodies.len;
    }
    RhoTask *continuation = rho_task_make(executor, state, bodies.items[chosen]);
    rho_vec_free(&bodies);
    if (!continuation) {
        cetta_name_supply_dispose(&placed);
        rho_task_end(executor, state, waiting);
        rho_async_fail(executor, "could not allocate rho COMM continuation");
        return false;
    }
    if (persistent) {
        /* The receive is armed again, with its region and its supply. */
        bool rearmed;
        if (receiver == finder) {
            CettaNameSupply names = cetta_name_supply_copy(running);
            rearmed = rho_async_push(executor, state, finder, &names);
            *kept = true;
        } else {
            CettaNameSupply names = waiting->supply;
            waiting->holds_supply = false;
            rearmed = rho_async_push(executor, state, waiting, &names);
        }
        if (!rearmed) {
            /* The receive that could not be armed again has ended; the
               send it consumed ends too (the finder's ends with its task). */
            if (receiver == finder)
                rho_task_end(executor, state, waiting);
            cetta_name_supply_dispose(&placed);
            rho_task_end(executor, state, continuation);
            rho_async_fail(executor, "could not re-arm persistent rho receive");
            return false;
        }
    }
    if (!rho_async_push(executor, state, continuation, &placed)) {
        if (!(persistent && receiver == waiting))
            rho_task_end(executor, state, waiting);
        rho_async_fail(executor, "could not enqueue rho COMM continuation");
        return false;
    }
    if (!(persistent && receiver == waiting))
        rho_task_end(executor, state, waiting);
    return true;
}

/* TASK publishes its endpoint on its channel: it meets a waiting endpoint
 * of the other side, or waits itself, keeping its region and supply. */
static bool rho_async_publish(RhoAsyncExecutor *executor,
                              RhoAsyncWorkerState *state, Arena *scratch,
                              RhoTask *task, RhoAsyncEndpointKind kind,
                              bool *kept) {
    RhoChannelBucket *bucket =
        rho_channel_table_get_or_create(&executor->channels, &executor->heads,
                                        task->view.args[0]);
    RhoTask *opposite = NULL;

    if (!bucket) {
        rho_async_fail(executor, "could not open a rho channel");
        return false;
    }
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_RHO_ASYNC_ENDPOINT_PUBLISH);
    pthread_mutex_lock(&bucket->mutex);
    if (kind == RHO_ASYNC_ENDPOINT_SEND) {
        if (bucket->recv_head && rho_async_try_spend_budget(executor)) {
            opposite = rho_async_endpoint_pop(&bucket->recv_head,
                                              &bucket->recv_tail);
        } else {
            task->kind = kind;
            rho_task_keep_supply(task);
            rho_async_endpoint_append(&bucket->send_head, &bucket->send_tail,
                                      task);
            *kept = true;
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_RHO_ASYNC_ENDPOINT_QUEUED);
        }
    } else {
        if (bucket->send_head && rho_async_try_spend_budget(executor)) {
            opposite = rho_async_endpoint_pop(&bucket->send_head,
                                              &bucket->send_tail);
        } else {
            task->kind = kind;
            rho_task_keep_supply(task);
            rho_async_endpoint_append(&bucket->recv_head, &bucket->recv_tail,
                                      task);
            *kept = true;
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_RHO_ASYNC_ENDPOINT_QUEUED);
        }
    }
    pthread_mutex_unlock(&bucket->mutex);

    if (!opposite) return true;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_RHO_ASYNC_ENDPOINT_MATCH);
    return rho_async_comm(executor, state, scratch, task, opposite, kind, kept);
}

static bool rho_async_process_task(CettaParallelWorker *worker, void *raw_task,
                                   void *user) {
    RhoAsyncExecutor *executor = user;
    RhoTask *task = raw_task;
    RhoAsyncWorkerState *state =
        &executor->workers[cetta_parallel_worker_index(worker)];
    Arena *scratch = cetta_parallel_worker_arena(worker);
    ArenaMark mark = arena_mark(scratch);
    bool kept = false;
    bool ok = true;

    if (executor->hooks.draw_each_task) {
        CettaGeneratedName name = cetta_name_fresh(CETTA_NAME_KIND_SPACE);
        if (executor->hooks.drawn)
            executor->hooks.drawn(executor->hooks.context, name);
    }
    switch (task->view.kind) {
    case RHO_NIL:
        break;
    case RHO_PAR:
        ok = rho_async_spawn(executor, state, &task->view,
                             cetta_name_supply_current());
        break;
    case RHO_SEND:
        ok = rho_async_publish(executor, state, scratch, task,
                               RHO_ASYNC_ENDPOINT_SEND, &kept);
        break;
    case RHO_RECV:
    case RHO_RECV_PERSISTENT:
        ok = rho_async_publish(executor, state, scratch, task,
                               RHO_ASYNC_ENDPOINT_RECV, &kept);
        break;
    case RHO_DROP:
    case RHO_VAL:
    case RHO_EVAL_PAYLOAD:
        rho_task_keep_supply(task);
        rho_async_add_stuck(executor, task);
        kept = true;
        break;
    case RHO_QUOTE:
    case RHO_BAD:
        rho_async_fail(executor, "unsupported rho task during threaded reduction");
        ok = false;
        break;
    }
    /* A task that waits, is stuck or is armed again may already be in
     * another worker's hands: it is not touched again here. */
    if (!kept)
        rho_task_end(executor, state, task);
    if (!arena_at_mark(scratch, mark))
        arena_reset(scratch, mark);
    return ok;
}

static void rho_async_worker_enter(CettaParallelWorker *worker, void *user) {
    (void)worker;
    (void)user;
    g_rho_async_worker_active = true;
}

static void rho_async_worker_leave(CettaParallelWorker *worker, void *user) {
    (void)worker;
    (void)user;
    g_rho_async_worker_active = false;
    eval_cleanup_owned_new_spaces_for_current_thread();
    bindings_thread_cache_free();
}

static bool rho_channel_table_has_enabled_pair(RhoChannelTable *table) {
    bool found = false;
    pthread_mutex_lock(&table->mutex);
    for (RhoChannelBucket *bucket = table->head; bucket; bucket = bucket->next) {
        pthread_mutex_lock(&bucket->mutex);
        if (bucket->send_head && bucket->recv_head) found = true;
        pthread_mutex_unlock(&bucket->mutex);
        if (found) break;
    }
    pthread_mutex_unlock(&table->mutex);
    return found;
}

/* Visit every live task at the end of a run: the stuck ones, then the
 * waiting sends and receives of each channel. */
typedef void (*RhoAsyncLiveVisit)(RhoAsyncExecutor *executor, RhoTask *task,
                                  void *context);

static void rho_async_visit_live(RhoAsyncExecutor *executor,
                                 RhoAsyncLiveVisit visit, void *context) {
    for (RhoTask *task = executor->stuck; task;) {
        RhoTask *next = task->next;
        visit(executor, task, context);
        task = next;
    }
    for (RhoChannelBucket *bucket = executor->channels.head; bucket;
         bucket = bucket->next) {
        for (RhoTask *task = bucket->send_head; task;) {
            RhoTask *next = task->next;
            visit(executor, task, context);
            task = next;
        }
        for (RhoTask *task = bucket->recv_head; task;) {
            RhoTask *next = task->next;
            visit(executor, task, context);
            task = next;
        }
    }
}

typedef struct {
    RhoAtomVec items;
    bool ok;
} RhoAsyncResidual;

static void rho_async_residual_visit(RhoAsyncExecutor *executor, RhoTask *task,
                                     void *context) {
    RhoAsyncResidual *residual = context;
    if (residual->ok)
        residual->ok = rho_vec_push(
            &residual->items, atom_deep_copy(executor->global_arena, task->proc));
}

static bool rho_async_materialize_residual(RhoAsyncExecutor *executor,
                                           Atom **out) {
    RhoAsyncResidual residual = {.ok = true};
    rho_vec_init(&residual.items);
    rho_async_visit_live(executor, rho_async_residual_visit, &residual);
    if (residual.ok) {
        *out = rho_par_from_vec(executor->global_arena, &residual.items);
        residual.ok = *out != NULL;
    }
    rho_vec_free(&residual.items);
    return residual.ok;
}

static void rho_async_census_visit(RhoAsyncExecutor *executor, RhoTask *task,
                                   void *context) {
    RhocalcThreadedCensus *census = context;
    census->live_tasks++;
    census->live_task_bytes += arena_accounted_live_bytes(&task->region->arena);
    census->live_task_reserved += task->region->arena.reserved_bytes;
    if (executor->hooks.live)
        executor->hooks.live(executor->hooks.context, &task->supply, task->proc);
}

/* The memory a run holds at its end, for tests. */
static void rho_async_take_census(RhoAsyncExecutor *executor) {
    RhocalcThreadedCensus census = {0};
    rho_async_visit_live(executor, rho_async_census_visit, &census);
    census.regions_made = atomic_load_explicit(&executor->regions_made,
                                               memory_order_relaxed);
    census.regions_stocked = atomic_load_explicit(&executor->region_pool_len,
                                                  memory_order_relaxed);
    census.regions_given_back = atomic_load_explicit(&executor->regions_freed,
                                                     memory_order_relaxed);
    for (uint32_t i = 0; i < executor->profile.thread_count; i++) {
        census.regions_stocked += executor->workers ? executor->workers[i].stock_len : 0u;
        if (executor->parallel.workers)
            census.scratch_reserved +=
                executor->parallel.workers[i].arena.reserved_bytes;
    }
    for (RhoChannelBucket *bucket = executor->channels.head; bucket;
         bucket = bucket->next)
        census.channels++;
    g_rho_threaded_census = census;
}

static void rho_async_end_visit(RhoAsyncExecutor *executor, RhoTask *task,
                                void *context) {
    (void)context;
    rho_task_end(executor, NULL, task);
}

static void rho_async_executor_init(RhoAsyncExecutor *executor,
                                    Arena *global_arena,
                                    const RhoRuntimeProfile *profile,
                                    const RhocalcEvalContext *eval_context) {
    CettaParallelExecutorConfig parallel_config;

    executor->global_arena = global_arena;
    executor->profile = *profile;
    executor->eval_context = eval_context;
    executor->hooks = g_rho_threaded_test_hooks;
    rho_symbols_ensure();
    executor->heads = (RhoHeadAtoms){
        .nil = atom_symbol_canonical(g_rho_syms.nil),
        .par = atom_symbol_canonical(g_rho_syms.par),
        .send = atom_symbol_canonical(g_rho_syms.send),
        .recv = atom_symbol_canonical(g_rho_syms.recv),
        .recv_persistent = atom_symbol_canonical(g_rho_syms.recv_persistent),
        .quote = atom_symbol_canonical(g_rho_syms.quote),
        .drop = atom_symbol_canonical(g_rho_syms.drop),
        .val = atom_symbol_canonical(g_rho_syms.val),
        .eval_payload = atom_symbol_canonical(g_rho_syms.eval_payload),
    };
    rho_channel_table_init(&executor->channels);
    executor->stuck = NULL;
    pthread_mutex_init(&executor->stuck_mutex, NULL);
    executor->workers = calloc(profile->thread_count,
                               sizeof(*executor->workers));
    pthread_mutex_init(&executor->region_mutex, NULL);
    executor->region_pool = NULL;
    atomic_init(&executor->region_pool_len, 0u);
    executor->region_pool_low = 0u;
    executor->region_pool_low_both = 0u;
    atomic_init(&executor->region_ends, 0u);
    atomic_init(&executor->region_window_end, RHO_TASK_REGION_WINDOW_MIN);
    atomic_init(&executor->regions_made, 0u);
    atomic_init(&executor->regions_freed, 0u);
    atomic_init(&executor->budget, profile->reduction_limit);
    atomic_init(&executor->reductions_taken, 0u);
    atomic_init(&executor->branch_turn, 0u);

    parallel_config = (CettaParallelExecutorConfig){
        .thread_count = profile->thread_count,
        .prefer_persistent_workers = true,
        .user = executor,
        .task_fn = rho_async_process_task,
        .worker_enter = rho_async_worker_enter,
        .worker_leave = rho_async_worker_leave,
        .task_discard = rho_async_discard_task,
        .worker_failure_message = "rho threaded worker failed",
    };
    cetta_parallel_executor_init(&executor->parallel, &parallel_config);
}

/* The run's end, on the thread that ran it: every live task ends, handing
 * its supply's clocks to the executor, and then the executor gives the
 * coordinator's supply its place back at the latest clocks. */
static void rho_async_executor_free(RhoAsyncExecutor *executor) {
    rho_async_visit_live(executor, rho_async_end_visit, NULL);
    executor->stuck = NULL;
    for (RhoChannelBucket *bucket = executor->channels.head; bucket;
         bucket = bucket->next) {
        bucket->send_head = bucket->send_tail = NULL;
        bucket->recv_head = bucket->recv_tail = NULL;
    }
    cetta_parallel_executor_free(&executor->parallel);
    if (executor->workers) {
        for (uint32_t i = 0; i < executor->profile.thread_count; i++)
            rho_task_region_list_free(executor, executor->workers[i].stock);
        free(executor->workers);
        executor->workers = NULL;
    }
    rho_task_region_list_free(executor, executor->region_pool);
    executor->region_pool = NULL;
    pthread_mutex_destroy(&executor->region_mutex);
    rho_channel_table_free(&executor->channels);
    pthread_mutex_destroy(&executor->stuck_mutex);
}

static bool rhocalc_reduce_to_quiescence_threaded(
    Arena *arena, Atom *proc, const RhoRuntimeProfile *profile,
    const RhocalcEvalContext *eval_context, RhoReductionResult *out) {
    RhoAsyncExecutor executor;
    bool ok = true;

    if (!arena || !proc || !profile || !out) return false;
    out->residual = NULL;
    out->reductions_taken = 0;
    out->status = RHOCALC_REDUCTION_QUIESCENT;

    if (profile->thread_count == 0u) {
        rho_validation_set("rho threaded execution requires at least one worker");
        return false;
    }
    rho_symbols_ensure();
    if (!rhocalc_process_well_formed(proc, eval_context ? RHOCALC_SEMANTIC_PROFILE_RHOMETTA
                                                        : RHOCALC_SEMANTIC_PROFILE_STRICT_CORE)) {
        return false;
    }

    rho_async_executor_init(&executor, arena, profile, eval_context);
    if (!executor.workers) {
        rho_validation_set("could not allocate threaded rho workers");
        rho_async_executor_free(&executor);
        return false;
    }

    /* The program is the first task, drawing from the right half of the
     * caller's supply, which keeps the left half until the run gives it its
     * place back.  When it is a parallel composition, its first event is its
     * spawn, which runs here, as the task would run it: each component gets
     * a region of its own with its piece of the first task's supply, and the
     * program needs no region of its own. */
    {
        Arena normal;
        arena_init_detached(&normal);
        Atom *norm = rho_normalize_proc(&normal, proc);
        RhoView view = norm ? rho_view(norm) : (RhoView){RHO_BAD, NULL, 0};
        CettaNameSupply first_supply = cetta_name_supply_fork(cetta_name_supply_current());
        if (view.kind == RHO_PAR) {
            CettaNameSupply *previous = cetta_name_supply_install(&first_supply);
            if (executor.hooks.draw_each_task) {
                CettaGeneratedName name = cetta_name_fresh(CETTA_NAME_KIND_SPACE);
                if (executor.hooks.drawn)
                    executor.hooks.drawn(executor.hooks.context, name);
            }
            ok = rho_async_spawn(&executor, NULL, &view, &first_supply);
            cetta_name_supply_install(previous);
            cetta_name_supply_dispose(&first_supply);
        } else {
            RhoTask *first = norm ? rho_task_make(&executor, NULL, norm) : NULL;
            if (!first) {
                cetta_name_supply_dispose(&first_supply);
                rho_async_fail(&executor, "could not enqueue initial rho task");
                ok = false;
            } else if (!rho_async_push(&executor, NULL, first, &first_supply)) {
                rho_async_fail(&executor, "could not enqueue initial rho task");
                ok = false;
            }
        }
        arena_free(&normal);
    }

    if (ok && !cetta_parallel_executor_run(&executor.parallel)) {
        ok = false;
    }

    if (cetta_parallel_executor_error(&executor.parallel)) {
        rho_validation_set("%s",
                           cetta_parallel_executor_error(&executor.parallel));
        ok = false;
    }

    if (ok && !rho_async_materialize_residual(&executor, &out->residual)) {
        rho_validation_set("could not materialize threaded rho residual");
        ok = false;
    }

    if (ok) {
        out->reductions_taken =
            atomic_load_explicit(&executor.reductions_taken,
                                 memory_order_relaxed);
        out->status = rho_channel_table_has_enabled_pair(&executor.channels)
            ? RHOCALC_REDUCTION_LIMIT_EXHAUSTED
            : RHOCALC_REDUCTION_QUIESCENT;
    }
    rho_async_take_census(&executor);

    rho_async_executor_free(&executor);
    return ok;
}

static bool rho_collect_successors(Arena *arena, Atom *proc,
                                   const RhocalcEvalContext *eval_context,
                                   RhoSuccessorSetAcc *out);
static bool rhocalc_collect_successor_set(Arena *arena, Atom *proc,
                                          const RhocalcEvalContext *eval_context,
                                          RhoSuccessorSet *out);

static void rhocalc_successor_set_free(RhoSuccessorSet *set) {
    if (!set) return;
    free(set->items);
    set->items = NULL;
    set->len = 0;
}

static bool rhocalc_collect_successor_set(Arena *arena, Atom *proc,
                                          const RhocalcEvalContext *eval_context,
                                          RhoSuccessorSet *out) {
    RhoSuccessorSetAcc acc;

    if (!arena || !proc || !out) return false;
    out->items = NULL;
    out->len = 0;

    rho_successor_set_acc_init(&acc);
    if (!rho_collect_successors(arena, proc, eval_context, &acc)) {
        rho_successor_set_acc_free(&acc);
        return false;
    }
    rho_successor_set_acc_finish(&acc, out);
    return true;
}

/* ── Quiet-frontier macro step (partial-order reduction) ──────────────────
 *
 * COMM steps on distinct channels commute, so exploring every interleaving
 * of an independent frontier multiplies intermediate states without adding
 * quiescent outcomes.  When (a) every channel carries at most one send
 * and one receive endpoint, so no alternative pairing exists, and (b) every
 * continuation produced by firing the enabled pairs is QUIET -- it contains
 * no send, receive, or drop anywhere, so no firing order can create a new
 * rendezvous on any channel -- AND (C3) firing the deferred payloads cannot
 * interfere with one another, and their results are freely duplicable across
 * product children -- the whole frontier fires as one macro step, branching
 * only on payload multiplicity.  The macro children are exactly the quiescent
 * states the full interleaving lattice reaches.  Any state outside those side
 * conditions falls back to the exact per-redex exploration.
 *
 * C3 is the condition the original argument missed: payload evaluation can
 * interfere through shared mutable state.  Rather than syntactically guess
 * which payloads are effectful (fragile -- a foreign-space access hidden behind
 * a user equation defeats any syntactic check), the non-interference half of
 * C3 is delivered STRUCTURALLY by the evaluator: rho_eval_payload_results runs
 * every payload against sibling-isolated scratch -- per-payload overlays for
 * reachable spaces and payload-owned clones for explicitly reachable state
 * cells. Shared bases never escape to a sibling, while local scratch still
 * supports write-forward computation inside the payload. The only remaining
 * macro obligation is copy-stability of results: a result carrying an
 * identity-bearing grounded value (e.g. a space from new-space or a state
 * cell) cannot be shared across product children, so
 * rho_atom_has_identity_grounded forces fallback for those.  The permanent
 * differential audit (scripts/rhometta_macro_differential_audit.py) is the
 * soundness backstop.
 *
 * NB: C2's quietness here is via rho_proc_is_quiet, which is STRONGER than
 * "could enable a rendezvous": it rejects send/recv/drop even under an inert
 * rho:val/rho:quote wrapper (conservative).  And free rho:drop(quote P) is
 * inert in this strict core (it unquotes only under the substitution of a
 * matched COMM), so excluding drop in continuations is sufficient. */
static bool rho_atom_is_active_process(const Atom *atom, void *context) {
    RhoView view;
    (void)context;
    if (!atom || atom->kind != ATOM_EXPR)
        return false;
    view = rho_view((Atom *)atom);
    if (view.kind == RHO_SEND || view.kind == RHO_RECV ||
        view.kind == RHO_RECV_PERSISTENT ||
        view.kind == RHO_DROP)
        return true;
    return false;
}

static bool rho_proc_is_quiet(Atom *proc) {
    bool complete = false;
    bool active = atom_graph_any_checked(proc, rho_atom_is_active_process,
        NULL, NULL, SIZE_MAX, &complete);
    return complete && !active;
}

/* C3 support: a grounded value is identity-bearing (not freely copyable) when
 * it carries pointer identity -- a space, state cell, capture, or foreign
 * handle.  The leaf set and its compositional summary are generated from the
 * resource-lifetime presentation and shared with the evaluator. */

/* Post-result copy-stability: does this result tree carry any identity-bearing
 * grounded value (which the macro would duplicate across product children)? */
static bool rho_atom_has_identity_grounded(Atom *atom) {
    return atom_has_identity_grounded(atom);
}

typedef enum {
    RHO_QUIET_MACRO_MODE_BAIL_EXACT = 0,
    RHO_QUIET_MACRO_MODE_FULL_FIRE,
    RHO_QUIET_MACRO_MODE_PARTIAL_FIRE,
} RhoQuietMacroMode;

typedef enum {
    RHO_MACRO_REJECT_NONE = 0,
    RHO_MACRO_REJECT_CONTENTION,
    RHO_MACRO_REJECT_NONQUIET,
    RHO_MACRO_REJECT_UNSAFE_PAYLOAD,
    RHO_MACRO_REJECT_CHILD_CAP,
} RhoMacroRejectReason;

typedef struct {
    uint32_t send_start;
    uint32_t send_end;
    uint32_t recv_start;
    uint32_t recv_end;
    const RhoEndpoint *send;
    const RhoEndpoint *recv;
    RhoAtomVec bodies;
    bool macro_selected;
    RhoMacroRejectReason reject_reason;
} RhoMacroGroup;

#define RHO_MACRO_STEP_CHILD_CAP 65536u

static bool rho_emit_comm_results_range(
    Arena *arena, RhoAtomVec *components, const RhoEndpointVec *sends,
    uint32_t send_start, uint32_t send_end, const RhoEndpointVec *recvs,
    uint32_t recv_start, uint32_t recv_end,
    const RhocalcEvalContext *eval_context, RhoSuccessorSetAcc *out) {
    for (uint32_t r = recv_start; r < recv_end; r++) {
        for (uint32_t s = send_start; s < send_end; s++) {
            if (!rho_emit_comm_results(arena, components,
                                       &sends->items[s], &recvs->items[r],
                                       eval_context, out)) {
                return false;
            }
        }
    }
    return true;
}

static bool rhocalc_try_quiet_macro_step(
    Arena *arena, Atom *proc, const RhocalcEvalContext *eval_context,
    RhoQuietMacroMode *mode, RhoSuccessorSetAcc *out) {
    RhoAtomVec components;
    RhoEndpointVec sends;
    RhoEndpointVec recvs;
    RhoMacroGroup *groups = NULL;
    bool *consumed = NULL;
    uint32_t *odometer = NULL;
    uint32_t ngroups = 0;
    uint32_t macro_groups = 0;
    uint32_t exact_groups = 0;
    uint32_t unsafe_groups = 0;
    uint64_t total_children = 1;
    uint32_t send_pos = 0;
    uint32_t recv_pos = 0;
    bool ok = true;

    *mode = RHO_QUIET_MACRO_MODE_BAIL_EXACT;
    /* Audit/oracle toggle: CETTA_RHO_NO_MACRO forces the exact (un-optimized)
     * interleaving exploration, so the same binary can run any program both
     * macro-on and macro-off for differential soundness checking. */
    if (getenv("CETTA_RHO_NO_MACRO"))
        return true;
    rho_vec_init(&components);
    rho_endpoint_vec_init(&sends);
    rho_endpoint_vec_init(&recvs);
    rho_collect_par(arena, proc, &components);
    if (!rho_collect_endpoints(&components, &sends, &recvs)) {
        ok = false;
        goto done;
    }

    /* Partition by channel: macro-fire eligible single-pair channels,
     * exact-explore contended or otherwise ineligible channels in the same
     * successor round. */
    groups = cetta_malloc(sizeof(RhoMacroGroup) *
                          (sends.len < recvs.len ? sends.len + 1
                                                 : recvs.len + 1));
    if (!groups) {
        ok = false;
        goto done;
    }
    uint32_t recv_start = 0u, send_start = 0u;
    while (rho_next_comm_run(&recvs, &sends, &recv_pos, &send_pos,
                             &recv_start, &send_start)) {
        RhoMacroGroup *group = &groups[ngroups++];

        memset(group, 0, sizeof(*group));
        rho_vec_init(&group->bodies);
        group->recv_start = recv_start;
        group->send_start = send_start;
        group->recv_end = recv_pos;
        group->send_end = send_pos;

        if ((recv_pos - recv_start) != 1u || (send_pos - send_start) != 1u) {
            group->reject_reason = RHO_MACRO_REJECT_CONTENTION;
            exact_groups++;
            continue;
        }

        group->send = &sends.items[send_start];
        group->recv = &recvs.items[recv_start];
        if (group->recv->view.kind == RHO_RECV_PERSISTENT) {
            group->reject_reason = RHO_MACRO_REJECT_NONQUIET;
            exact_groups++;
            continue;
        }
        if (!rho_compute_comm_continuations(arena, group->send,
                                            group->recv, eval_context,
                                            &group->bodies)) {
            ok = false;
            goto done;
        }
        for (uint32_t b = 0; b < group->bodies.len; b++) {
            Atom *body = rho_normalize_proc(arena, group->bodies.items[b]);
            if (!body || !rho_proc_is_quiet(body)) {
                group->reject_reason = RHO_MACRO_REJECT_NONQUIET;
                exact_groups++;
                goto next_group;
            }
            if (rho_atom_has_identity_grounded(body)) {
                group->reject_reason = RHO_MACRO_REJECT_UNSAFE_PAYLOAD;
                unsafe_groups++;
                exact_groups++;
                goto next_group;
            }
            group->bodies.items[b] = body;
        }
        if (group->bodies.len == 0)
            goto next_group;
        if (total_children > (UINT64_MAX / group->bodies.len) ||
            total_children * group->bodies.len > RHO_MACRO_STEP_CHILD_CAP) {
            group->reject_reason = RHO_MACRO_REJECT_CHILD_CAP;
            exact_groups++;
            goto next_group;
        }
        group->macro_selected = true;
        macro_groups++;
        total_children *= group->bodies.len;

next_group:
        continue;
    }

    /* C3's non-interference half is guaranteed by the evaluator (each payload
     * runs against sibling-isolated scratch, see rho_eval_payload_results), so
     * the macro only has to demand quiet, copy-stable continuations below. */

    for (uint32_t g = 0; g < ngroups; g++) {
        switch (groups[g].reject_reason) {
        case RHO_MACRO_REJECT_CONTENTION:
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_RHO_QUIET_MACRO_FALLBACK_CONTENTION);
            break;
        case RHO_MACRO_REJECT_NONQUIET:
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_RHO_QUIET_MACRO_FALLBACK_NONQUIET);
            break;
        case RHO_MACRO_REJECT_UNSAFE_PAYLOAD:
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_RHO_QUIET_MACRO_FALLBACK_UNSAFE_PAYLOAD);
            break;
        case RHO_MACRO_REJECT_CHILD_CAP:
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_RHO_QUIET_MACRO_FALLBACK_CHILD_CAP);
            break;
        case RHO_MACRO_REJECT_NONE:
            break;
        }
    }
    if (unsafe_groups > 0 ||
        macro_groups == 0 ||
        (macro_groups == 1 && exact_groups == 0)) {
        if (macro_groups > 0 || exact_groups > 0) {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_RHO_QUIET_MACRO_BAIL_EXACT);
        }
        goto done;
    }

    /* Cartesian product over macro-fired groups' payload results. */
    consumed = cetta_malloc(sizeof(bool) * (components.len ? components.len
                                                           : 1u));
    odometer = cetta_malloc(sizeof(uint32_t) * (ngroups ? ngroups : 1u));
    if (!consumed || !odometer) {
        ok = false;
        goto done;
    }
    memset(consumed, 0, sizeof(bool) * (components.len ? components.len : 1u));
    memset(odometer, 0, sizeof(uint32_t) * (ngroups ? ngroups : 1u));
    for (uint32_t g = 0; g < ngroups; g++) {
        if (!groups[g].macro_selected)
            continue;
        consumed[groups[g].send->component_index] = true;
        consumed[groups[g].recv->component_index] = true;
    }

    for (;;) {
        RhoAtomVec child;
        Atom *child_proc;
        rho_vec_init(&child);
        for (uint32_t i = 0; i < components.len; i++) {
            if (consumed[i])
                continue;
            if (!rho_vec_push(&child, components.items[i])) {
                rho_vec_free(&child);
                ok = false;
                goto done;
            }
        }
        for (uint32_t g = 0; g < ngroups && ok; g++) {
            Atom *body;
            RhoView body_view;
            if (!groups[g].macro_selected)
                continue;
            body = groups[g].bodies.items[odometer[g]];
            body_view = rho_view(body);
            if (body_view.kind == RHO_PAR) {
                for (uint32_t i = 0; i < body_view.nargs; i++) {
                    if (!rho_vec_push(&child, body_view.args[i])) {
                        ok = false;
                        break;
                    }
                }
            } else if (body_view.kind != RHO_NIL) {
                if (!rho_vec_push(&child, body))
                    ok = false;
            }
        }
        if (!ok) {
            rho_vec_free(&child);
            goto done;
        }
        child_proc = rho_par_from_vec(arena, &child);
        rho_vec_free(&child);
        if (!child_proc || !rho_successor_set_acc_push(out, child_proc)) {
            ok = false;
            goto done;
        }

        /* advance the odometer over macro-fired groups */
        {
            uint32_t g = 0;
            while (g < ngroups) {
                if (!groups[g].macro_selected) {
                    g++;
                    continue;
                }
                odometer[g]++;
                if (odometer[g] < groups[g].bodies.len)
                    break;
                odometer[g] = 0;
                g++;
            }
            if (g == ngroups)
                break; /* product exhausted */
        }
    }

    for (uint32_t g = 0; g < ngroups && ok; g++) {
        if (groups[g].reject_reason == RHO_MACRO_REJECT_NONE)
            continue;
        if (!rho_emit_comm_results_range(arena, &components,
                                         &sends,
                                         groups[g].send_start,
                                         groups[g].send_end,
                                         &recvs,
                                         groups[g].recv_start,
                                         groups[g].recv_end,
                                         eval_context,
                                         out)) {
            ok = false;
        }
    }
    if (!ok)
        goto done;

    *mode = exact_groups == 0
        ? RHO_QUIET_MACRO_MODE_FULL_FIRE
        : RHO_QUIET_MACRO_MODE_PARTIAL_FIRE;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_RHO_QUIET_MACRO_APPLIED);
    cetta_runtime_stats_inc(
        *mode == RHO_QUIET_MACRO_MODE_FULL_FIRE
            ? CETTA_RUNTIME_COUNTER_RHO_QUIET_MACRO_FULL_FIRE
            : CETTA_RUNTIME_COUNTER_RHO_QUIET_MACRO_PARTIAL_FIRE);

done:
    if (groups) {
        for (uint32_t g = 0; g < ngroups; g++)
            rho_vec_free(&groups[g].bodies);
        free(groups);
    }
    free(consumed);
    free(odometer);
    rho_endpoint_vec_free(&sends);
    rho_endpoint_vec_free(&recvs);
    rho_vec_free(&components);
    return ok;
}

/* The search for the quiescent states a process reaches, on the runner's
 * breadth-first driver (runner.h), shared with the tcalc lane.  A state's
 * number is its place in `seen`, so the driver reaches the states in the
 * order they were first met.  States met are found again through their
 * hashes; equal hashes are confirmed by the structural order. */
typedef struct {
    Arena *arena;
    const RhocalcEvalContext *eval_context;
    RhoSuccessorSetAcc seen;
    uint64_t *seen_hashes; uint32_t seen_hashes_cap;  /* beside `seen`, one each */
    uint32_t *slots; uint32_t slots_cap;         /* open addressing: a place in `seen`, or UINT32_MAX */
    RhoHashMemo memo;
    RhoSuccessorSetAcc quiescent;
    uint32_t *next; size_t next_len, next_cap;   /* a state's successors, by number */
} RhoQuiescentSearch;

static void rho_quiescent_search_free(RhoQuiescentSearch *search) {
    rho_successor_set_acc_free(&search->seen);
    free(search->seen_hashes);
    free(search->slots);
    rho_hash_memo_free(&search->memo);
    free(search->next);
}

/* The number of the state equal to `state`, numbering it when first met. */
static bool rho_quiescent_search_number(RhoQuiescentSearch *search,
                                        Atom *state, uint32_t *out) {
    uint64_t hash = rho_proc_hash(state, &search->memo);
    uint32_t mask;
    uint32_t slot;
    uint32_t at;
    if ((search->seen.len + 1u) * 2u > search->slots_cap) {
        uint32_t cap = search->slots_cap ? search->slots_cap * 2u : 64u;
        uint32_t *slots = cetta_malloc(sizeof(uint32_t) * cap);
        memset(slots, 0xff, sizeof(uint32_t) * cap);
        for (uint32_t i = 0u; i < search->seen.len; i++) {
            uint32_t k = (uint32_t)search->seen_hashes[i] & (cap - 1u);
            while (slots[k] != UINT32_MAX) k = (k + 1u) & (cap - 1u);
            slots[k] = i;
        }
        free(search->slots);
        search->slots = slots;
        search->slots_cap = cap;
    }
    mask = search->slots_cap - 1u;
    slot = (uint32_t)hash & mask;
    while ((at = search->slots[slot]) != UINT32_MAX) {
        if (search->seen_hashes[at] == hash &&
            rho_proc_order(search->seen.items[at], state) == 0) {
            *out = at;
            return true;
        }
        slot = (slot + 1u) & mask;
    }
    at = search->seen.len;
    if (!rho_successor_set_acc_push(&search->seen, state)) return false;
    if (search->seen_hashes_cap < search->seen.cap) {
        search->seen_hashes = cetta_realloc(search->seen_hashes,
                                            sizeof(uint64_t) * search->seen.cap);
        search->seen_hashes_cap = search->seen.cap;
    }
    search->seen_hashes[at] = hash;
    search->slots[slot] = at;
    *out = at;
    return true;
}

/* One state of the search: its successors (a quiet macro step's when one
 * applies), each numbered when first met; with none, the state is kept as
 * quiescent. */
static RunExpansion rho_quiescent_expand(void *lane, uint32_t state, RunGraph *g) {
    RhoQuiescentSearch *search = lane;
    Atom *current = search->seen.items[state];
    RhoSuccessorSet successors = {0};
    RhoQuietMacroMode macro_mode = RHO_QUIET_MACRO_MODE_BAIL_EXACT;
    {
        RhoSuccessorSetAcc macro_acc;
        rho_successor_set_acc_init(&macro_acc);
        if (!rhocalc_try_quiet_macro_step(search->arena, current, search->eval_context,
                                          &macro_mode, &macro_acc)) {
            rho_successor_set_acc_free(&macro_acc);
            return RUN_FAILED;
        }
        if (macro_mode != RHO_QUIET_MACRO_MODE_BAIL_EXACT) {
            rho_successor_set_acc_finish(&macro_acc, &successors);
        } else {
            rho_successor_set_acc_free(&macro_acc);
        }
    }
    if (macro_mode == RHO_QUIET_MACRO_MODE_BAIL_EXACT &&
        !rhocalc_collect_successor_set(search->arena, current, search->eval_context,
                                       &successors))
        return RUN_FAILED;
    if (successors.len == 0) {
        bool kept = rho_successor_set_acc_push(&search->quiescent, current);
        rhocalc_successor_set_free(&successors);
        return kept ? RUN_EXPANDED : RUN_FAILED;
    }
    search->next_len = 0;
    for (uint32_t i = 0; i < successors.len; i++) {
        uint32_t at = UINT32_MAX;
        if (!rho_quiescent_search_number(search, successors.items[i], &at) ||
            !run_push_u32(realloc, &search->next, &search->next_len, &search->next_cap,
                          at)) {
            rhocalc_successor_set_free(&successors);
            return RUN_FAILED;
        }
    }
    rhocalc_successor_set_free(&successors);
    return run_steps(g, search->next, search->next_len) ? RUN_EXPANDED : RUN_FAILED;
}

static bool rhocalc_collect_quiescent_set(
    Arena *arena, Atom *proc, const RhocalcEvalContext *eval_context,
    RhoSuccessorSet *out) {
    if (!arena || !proc || !out) return false;
    out->items = NULL;
    out->len = 0;

    RhoQuiescentSearch search = {.arena = arena, .eval_context = eval_context};
    RunGraph graph = {0};                          /* no cycle is asked: no steps kept */
    bool finished = false;
    Atom *start = rho_normalize_proc(arena, proc);
    uint32_t start_number = UINT32_MAX;
    bool ok = start && rho_quiescent_search_number(&search, start, &start_number);
    ok = ok && run_breadth_first(&graph, 0, rho_quiescent_expand, &search, &finished);
    run_graph_free(&graph);
    rho_quiescent_search_free(&search);
    if (!ok) {
        rho_successor_set_acc_free(&search.quiescent);
        return false;
    }
    rho_successor_set_acc_finish(&search.quiescent, out);
    return true;
}

Atom *rhocalc_successor_frontier_expr(Arena *arena, Atom *proc) {
    return rhocalc_successor_frontier_expr_with_eval_context(arena, proc, NULL);
}

Atom *rhocalc_successor_frontier_expr_with_eval_context(
    Arena *arena, Atom *proc, const RhocalcEvalContext *eval_context) {
    RhoSuccessorSet successors = {0};
    Atom *list;
    Atom *result;

    if (!rhocalc_collect_successor_set(arena, proc, eval_context, &successors)) {
        return NULL;
    }
    list = atom_expr(arena, successors.items, successors.len);
    result = atom_expr2(arena, atom_symbol(arena, "superpose"), list);
    rhocalc_successor_set_free(&successors);
    return result;
}

Atom *rhocalc_quiescent_frontier_expr(Arena *arena, Atom *proc) {
    return rhocalc_quiescent_frontier_expr_with_eval_context(arena, proc, NULL);
}

Atom *rhocalc_quiescent_frontier_expr_with_eval_context(
    Arena *arena, Atom *proc, const RhocalcEvalContext *eval_context) {
    RhoSuccessorSet quiescent = {0};
    Atom *list;
    Atom *result;

    if (!rhocalc_collect_quiescent_set(arena, proc, eval_context, &quiescent)) {
        return NULL;
    }
    list = atom_expr(arena, quiescent.items, quiescent.len);
    result = atom_expr2(arena, atom_symbol(arena, "superpose"), list);
    rhocalc_successor_set_free(&quiescent);
    return result;
}

/* ── Cost profile: term views and constructors ──────────────────────────────
 *
 * Cost terms extend the process layer with rho:cost:signed (a process body
 * carrying a signature), rho:cost:purse (a token stack located at a channel
 * location), rho:cost:stack-empty / rho:cost:stack-cons, and signature
 * products rho:cost:sig-mul over ground signature symbols.  Signatures form
 * a free commutative monoid: products are kept as sorted multisets of
 * ground atoms, so signature equality is multiset equality. */

typedef enum {
    RHOCOST_TERM_BAD = 0,
    RHOCOST_TERM_NIL,
    RHOCOST_TERM_SIGNED,
    RHOCOST_TERM_PAR,
    RHOCOST_TERM_DROP,
    RHOCOST_TERM_PURSE,
    RHOCOST_TERM_STACK_EMPTY,
    RHOCOST_TERM_STACK_CONS
} RhoCostTermKind;

typedef struct {
    RhoCostTermKind kind;
    Atom **args;
    uint32_t nargs;
} RhoCostTermView;

typedef struct {
    uint32_t component_index;
    Atom *location;
    Atom *sig;
    Atom *rest;
} RhoCostToken;

typedef struct {
    RhoCostToken *items;
    uint32_t len;
    uint32_t cap;
} RhoCostTokenVec;

typedef struct {
    uint32_t component_index;
    Atom *sig;
    RhoView view;
} RhoCostSignedEndpoint;

typedef struct {
    RhoCostSignedEndpoint *items;
    uint32_t len;
    uint32_t cap;
} RhoCostSignedEndpointVec;

typedef struct {
    uint32_t component_index;
    Atom *sig;
    RhoView recv;
    RhoView send;
} RhoCostWholeRedex;

typedef struct {
    RhoCostWholeRedex *items;
    uint32_t len;
    uint32_t cap;
} RhoCostWholeRedexVec;

typedef struct {
    uint32_t *items;
    uint32_t len;
    uint32_t cap;
} RhoIndexVec;

typedef struct {
    uint32_t position;
    RhoAtomVec remaining_sig_atoms;
    RhoIndexVec chosen_tokens;
} RhoCostCoverFrame;

typedef struct {
    RhoCostCoverFrame *items;
    uint32_t len;
    uint32_t cap;
} RhoCostCoverFrameVec;

typedef struct {
    Atom *result;
    Atom *consumed_sig;
    Atom *contractum;
    uint32_t participant_indices[2];
    uint32_t participant_len;
    uint32_t *token_indices;
    uint32_t token_len;
} RhoCostStep;

typedef struct {
    RhoCostStep *items;
    uint32_t len;
    uint32_t cap;
    bool retain_provenance;
} RhoCostStepSetAcc;

typedef struct {
    RhoCostStep *items;
    uint32_t len;
} RhoCostStepSet;

typedef struct {
    bool bounded;
    uint64_t remaining;
    bool exhausted;
    bool stop_after_first;
    bool stopped;
} RhoCostSearchControl;

typedef bool (*RhoCostCandidateEmitter)(
    Arena *arena, RhoAtomVec *components,
    const uint32_t *participant_indices, uint32_t participant_len,
    Atom *contractum, RhoView recv, RhoView send,
    RhoCostTokenVec *tokens, RhoIndexVec *chosen_tokens,
    Atom *consumed_sig, void *context);

static Atom *rhocost_subst_term(Arena *arena, Atom *term,
                                VarId binder, Atom *replacement);

typedef struct {
    RhoCostStep step;
    uint32_t ordinal;
} RhoCostOrderedStep;

typedef struct {
    Atom *term;
    bool has_producer;
    uint64_t producer;
} RhoCostTraceComponent;

typedef struct {
    RhoCostTraceComponent *items;
    uint32_t len;
    uint32_t cap;
} RhoCostTraceComponentVec;

typedef struct {
    RhoCostTraceComponent component;
    uint32_t ordinal;
} RhoCostOrderedTraceComponent;

/*
 * Operational components are always just terms. Causal provenance is an
 * optional, parallel observation vector rather than part of the state that
 * every Cost-Rho execution must maintain.
 */
#define RHOCOST_NO_PRODUCER UINT64_MAX

typedef struct {
    uint64_t *items;
    uint32_t len;
    uint32_t cap;
} RhoCostProducerVec;

/*
 * Optional causal observation for the threaded executor.  A NULL observer is
 * the state-only fast path.  Keeping the producer table and event sink in one
 * object makes it impossible for callers to request only half of the causal
 * bookkeeping state.
 */
typedef struct {
    RhoCostProducerVec producers;
    RhoAtomVec *events;
} RhoCostCausalObserver;

typedef struct {
    Atom *term;
    uint64_t producer;
    uint32_t ordinal;
} RhoCostOrderedRuntimeComponent;

/*
 * Immutable, occurrence-indexed description of one legal funded firing.
 * Instances are created only by the canonical candidate enumerator.  Worker
 * threads receive this plan read-only and publish only the outcome fields in
 * RhoCostParallelTask below.
 */
typedef struct {
    Atom *consumed_sig;
    uint32_t participant_indices[2];
    uint32_t participant_len;
    uint32_t *token_indices;
    uint32_t token_len;
    Atom *channel;
    Atom *recv_body;
    VarId recv_binder;
    Atom *send_payload;
    uint32_t *resource_indices;
    uint32_t resource_len;
} RhoCostParallelPlan;

typedef struct {
    RhoCostParallelPlan plan;
    Atom *contractum;
    bool preclaimed;
    bool selected;
} RhoCostParallelTask;

typedef struct {
    RhoCostParallelTask *items;
    uint32_t len;
    uint32_t cap;
} RhoCostParallelTaskVec;

typedef struct {
    _Atomic unsigned char *claimed;
    uint32_t component_count;
    _Atomic uint32_t remaining_budget;
} RhoCostParallelWave;

/* The cost profile's growable lists, declared in rhocalc_cost_lists.export. */
#include "generated/rhocalc_cost_lists.generated.h"

static bool rhocost_search_halted(const RhoCostSearchControl *control) {
    return control && (control->exhausted || control->stopped);
}

static bool rhocost_search_consume(RhoCostSearchControl *control) {
    if (!control || !control->bounded) return true;
    if (control->remaining == 0) {
        control->exhausted = true;
        return false;
    }
    control->remaining--;
    return true;
}

static bool rhocost_is_ground_signature(Atom *sig) {
    return sig && sig->kind == ATOM_SYMBOL &&
           strncmp(atom_name_cstr(sig), "rho:", 4) != 0;
}

static bool rhocost_is_sig_mul(Atom *sig) {
    return sig && sig->kind == ATOM_EXPR && sig->expr.len >= 3 &&
           sig->expr.elems[0]->kind == ATOM_SYMBOL &&
           strcmp(atom_name_cstr(sig->expr.elems[0]), "rho:cost:sig-mul") == 0;
}

static RhoCostTermView rhocost_term_view(Atom *atom) {
    RhoCostTermView view = {RHOCOST_TERM_BAD, NULL, 0};
    if (rho_symbol_named(atom, "rho:cost:nil")) {
        view.kind = RHOCOST_TERM_NIL;
        return view;
    }
    if (rho_symbol_named(atom, "rho:cost:stack-empty")) {
        view.kind = RHOCOST_TERM_STACK_EMPTY;
        return view;
    }
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len == 0 ||
        atom->expr.elems[0]->kind != ATOM_SYMBOL) {
        return view;
    }
    const char *head = atom_name_cstr(atom->expr.elems[0]);
    view.args = atom->expr.elems + 1;
    view.nargs = atom->expr.len - 1;
    if (strcmp(head, "rho:cost:signed") == 0) view.kind = RHOCOST_TERM_SIGNED;
    else if (strcmp(head, "rho:cost:par") == 0) view.kind = RHOCOST_TERM_PAR;
    else if (strcmp(head, "rho:drop") == 0) view.kind = RHOCOST_TERM_DROP;
    else if (strcmp(head, "rho:cost:purse") == 0) view.kind = RHOCOST_TERM_PURSE;
    else if (strcmp(head, "rho:cost:stack-cons") == 0) view.kind = RHOCOST_TERM_STACK_CONS;
    return view;
}

static Atom *rhocost_nil(Arena *arena) {
    return atom_symbol(arena, "rho:cost:nil");
}

static Atom *rhocost_stack_empty(Arena *arena) {
    return atom_symbol(arena, "rho:cost:stack-empty");
}

static Atom *rhocost_stack_cons(Arena *arena, Atom *sig, Atom *rest) {
    return rho_binary(arena, "rho:cost:stack-cons", sig, rest);
}

static Atom *rhocost_purse(Arena *arena, Atom *location, Atom *stack) {
    return rho_binary(arena, "rho:cost:purse", location, stack);
}

static Atom *rhocost_signed(Arena *arena, Atom *body, Atom *sig) {
    return rho_binary(arena, "rho:cost:signed", body, sig);
}

static Atom *rhocost_par(Arena *arena, Atom *const *items, uint32_t len) {
    return rho_call(arena, "rho:cost:par", items, len);
}

static Atom *rhocost_sig_mul(Arena *arena, Atom *const *items, uint32_t len) {
    return rho_call(arena, "rho:cost:sig-mul", items, len);
}

static int rhocost_sig_atom_cmp(const void *lhs, const void *rhs) {
    Atom *const *a = lhs;
    Atom *const *b = rhs;
    return strcmp(atom_name_cstr(*a), atom_name_cstr(*b));
}

static int rhocost_step_order(const RhoCostStep *a, const RhoCostStep *b);
static int rhocost_term_order(Atom *a, Atom *b);

static int rhocost_ordinal_cmp(uint32_t a, uint32_t b) {
    return a < b ? -1 : a > b ? 1 : 0;
}

static int rhocost_ordered_step_cmp(const void *lhs, const void *rhs) {
    const RhoCostOrderedStep *a = lhs;
    const RhoCostOrderedStep *b = rhs;
    int cmp = rhocost_step_order(&a->step, &b->step);
    return cmp != 0 ? cmp : rhocost_ordinal_cmp(a->ordinal, b->ordinal);
}

static int rhocost_ordered_trace_component_cmp(const void *lhs,
                                               const void *rhs) {
    const RhoCostOrderedTraceComponent *a = lhs;
    const RhoCostOrderedTraceComponent *b = rhs;
    int cmp = rhocost_term_order(a->component.term, b->component.term);
    return cmp != 0 ? cmp : rhocost_ordinal_cmp(a->ordinal, b->ordinal);
}

static int rhocost_ordered_runtime_component_cmp(const void *lhs,
                                                 const void *rhs) {
    const RhoCostOrderedRuntimeComponent *a = lhs;
    const RhoCostOrderedRuntimeComponent *b = rhs;
    int cmp = rhocost_term_order(a->term, b->term);
    return cmp != 0 ? cmp : rhocost_ordinal_cmp(a->ordinal, b->ordinal);
}

static int rhocost_u32_cmp(const void *lhs, const void *rhs) {
    uint32_t a = *(const uint32_t *)lhs;
    uint32_t b = *(const uint32_t *)rhs;
    return a < b ? -1 : a > b ? 1 : 0;
}

static void rhocost_index_vec_init(RhoIndexVec *vec) {
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static void rhocost_index_vec_free(RhoIndexVec *vec) {
    free(vec->items);
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static bool rhocost_index_vec_push(RhoIndexVec *vec, uint32_t value) {
    return RHO_PUSH(rhocost_push_index, vec, value);
}

static bool rhocost_atom_vec_clone(RhoAtomVec *out,
                                   const RhoAtomVec *source) {
    rho_vec_init(out);
    for (uint32_t i = 0; i < source->len; i++) {
        if (!rho_vec_push(out, source->items[i])) {
            rho_vec_free(out);
            return false;
        }
    }
    return true;
}

static bool rhocost_index_vec_clone(RhoIndexVec *out,
                                    const RhoIndexVec *source) {
    rhocost_index_vec_init(out);
    for (uint32_t i = 0; i < source->len; i++) {
        if (!rhocost_index_vec_push(out, source->items[i])) {
            rhocost_index_vec_free(out);
            return false;
        }
    }
    return true;
}

static void rhocost_cover_frame_init(RhoCostCoverFrame *frame) {
    frame->position = 0;
    rho_vec_init(&frame->remaining_sig_atoms);
    rhocost_index_vec_init(&frame->chosen_tokens);
}

static void rhocost_cover_frame_free(RhoCostCoverFrame *frame) {
    rho_vec_free(&frame->remaining_sig_atoms);
    rhocost_index_vec_free(&frame->chosen_tokens);
    frame->position = 0;
}

static bool rhocost_cover_frame_clone(
    RhoCostCoverFrame *out, uint32_t position,
    const RhoAtomVec *remaining_sig_atoms,
    const RhoIndexVec *chosen_tokens) {
    rhocost_cover_frame_init(out);
    out->position = position;
    if (!rhocost_atom_vec_clone(&out->remaining_sig_atoms,
                                remaining_sig_atoms) ||
        !rhocost_index_vec_clone(&out->chosen_tokens, chosen_tokens)) {
        rhocost_cover_frame_free(out);
        return false;
    }
    return true;
}

static void rhocost_cover_frame_vec_init(RhoCostCoverFrameVec *vec) {
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static void rhocost_cover_frame_vec_free(RhoCostCoverFrameVec *vec) {
    for (uint32_t i = 0; i < vec->len; i++) {
        rhocost_cover_frame_free(&vec->items[i]);
    }
    free(vec->items);
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

/* Transfer ownership of one frame into the explicit depth-first stack. */
static bool rhocost_cover_frame_vec_push_take(RhoCostCoverFrameVec *vec,
                                              RhoCostCoverFrame *frame) {
    if (!RHO_PUSH(rhocost_push_cover_frame, vec, *frame))
        return false;
    rhocost_cover_frame_init(frame);
    return true;
}

static void rhocost_trace_component_vec_init(RhoCostTraceComponentVec *vec) {
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static void rhocost_trace_component_vec_free(RhoCostTraceComponentVec *vec) {
    if (!vec) return;
    free(vec->items);
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static bool rhocost_trace_component_vec_push(
    RhoCostTraceComponentVec *vec, Atom *term,
    bool has_producer, uint64_t producer) {
    return RHO_PUSH(rhocost_push_trace_component, vec,
                    ((RhoCostTraceComponent){term, has_producer, producer}));
}

static void rhocost_producer_vec_init(RhoCostProducerVec *vec) {
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static void rhocost_producer_vec_free(RhoCostProducerVec *vec) {
    if (!vec) return;
    free(vec->items);
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static bool rhocost_producer_vec_push(RhoCostProducerVec *vec,
                                      uint64_t producer) {
    if (!RHO_PUSH(rhocost_push_producer, vec, producer))
        return false;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_COST_RHO_RECEIPT_PRODUCER_PROPAGATED);
    return true;
}

static void rhocost_causal_observer_init(
    RhoCostCausalObserver *observer, RhoAtomVec *events) {
    rhocost_producer_vec_init(&observer->producers);
    observer->events = events;
}

static void rhocost_causal_observer_free(
    RhoCostCausalObserver *observer) {
    if (!observer) return;
    rhocost_producer_vec_free(&observer->producers);
    observer->events = NULL;
}

static void rhocost_step_set_acc_init(RhoCostStepSetAcc *acc) {
    *acc = (RhoCostStepSetAcc){0};
}

static void rhocost_step_set_acc_init_traced(RhoCostStepSetAcc *acc) {
    rhocost_step_set_acc_init(acc);
    acc->retain_provenance = true;
}

static void rhocost_step_release(RhoCostStep *step) {
    if (!step) return;
    free(step->token_indices);
    step->token_indices = NULL;
    step->token_len = 0;
}

static void rhocost_parallel_task_vec_init(RhoCostParallelTaskVec *vec) {
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static void rhocost_parallel_task_vec_free(RhoCostParallelTaskVec *vec) {
    if (!vec) return;
    for (uint32_t i = 0; i < vec->len; i++) {
        free(vec->items[i].plan.token_indices);
        free(vec->items[i].plan.resource_indices);
    }
    free(vec->items);
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static bool rhocost_parallel_task_vec_push(
    RhoCostParallelTaskVec *vec, const uint32_t *participant_indices,
    uint32_t participant_len, RhoCostTokenVec *tokens,
    RhoIndexVec *chosen_tokens, Atom *consumed_sig,
    RhoView recv, RhoView send) {
    uint32_t *token_indices = NULL;
    uint32_t *resource_indices = NULL;
    uint32_t resource_len = participant_len + chosen_tokens->len;
    RhoCostParallelTask *task;

    if (!vec || participant_len == 0u || participant_len > 2u ||
        recv.kind != RHO_RECV || recv.nargs != 3u ||
        recv.args[1]->kind != ATOM_VAR ||
        send.kind != RHO_SEND || send.nargs != 2u ||
        chosen_tokens->len == 0u) {
        return false;
    }
    token_indices = cetta_malloc(sizeof(uint32_t) * chosen_tokens->len);
    for (uint32_t i = 0; i < chosen_tokens->len; i++) {
        token_indices[i] =
            tokens->items[chosen_tokens->items[i]].component_index;
    }
    resource_indices = cetta_malloc(sizeof(uint32_t) * resource_len);
    for (uint32_t i = 0; i < participant_len; i++) {
        resource_indices[i] = participant_indices[i];
    }
    for (uint32_t i = 0; i < chosen_tokens->len; i++) {
        resource_indices[participant_len + i] = token_indices[i];
    }
    qsort(resource_indices, resource_len, sizeof(uint32_t), rhocost_u32_cmp);
    for (uint32_t i = 1; i < resource_len; i++) {
        if (resource_indices[i - 1] == resource_indices[i]) {
            free(resource_indices);
            free(token_indices);
            return false;
        }
    }
    if (!rhocost_push_parallel_task(rho_resize, &vec->items, &vec->len, &vec->cap,
                                    (RhoCostParallelTask){0})) {
        free(resource_indices);
        free(token_indices);
        return false;
    }
    task = &vec->items[vec->len - 1];
    memset(task, 0, sizeof(*task));
    task->plan.consumed_sig = consumed_sig;
    task->plan.participant_len = participant_len;
    for (uint32_t i = 0; i < participant_len; i++) {
        task->plan.participant_indices[i] = participant_indices[i];
    }
    task->plan.token_indices = token_indices;
    task->plan.token_len = chosen_tokens->len;
    task->plan.channel = recv.args[0];
    task->plan.recv_body = recv.args[2];
    task->plan.recv_binder = recv.args[1]->var_id;
    task->plan.send_payload = send.args[1];
    task->plan.resource_indices = resource_indices;
    task->plan.resource_len = resource_len;
    task->contractum = NULL;
    task->preclaimed = false;
    task->selected = false;
    return true;
}

/* ── Parallel waves: atomic occurrence claims ───────────────────────────────
 *
 * A wave runs compatible firings concurrently.  Before firing, a worker
 * atomically claims every exact occurrence its plan touches — both
 * endpoints and every funding token — in sorted resource order; a wave
 * admits only pairwise-disjoint claims, and each committed wave is
 * re-validated against occurrence accounting before the residual is
 * rebuilt.  The sequential semantics stays the reference: a wave is a legal
 * parallel refinement, not a new relation.
 *
 * Claim bytes are the resource-ownership boundary: acquisition is acq_rel,
 * failed observation is acquire, and relinquishment is release.  Candidate
 * data is immutable before worker creation, and the coordinator reads worker
 * results only after the executor has joined every thread.  The budget and
 * rotating wave ticket are independent numeric atomics; they publish no task
 * data, so their bookkeeping-only loads, refunds, and increments may be
 * relaxed without weakening occurrence ownership.
 */
static void rhocost_parallel_release_claims(
    RhoCostParallelWave *wave, const RhoCostParallelTask *task,
    uint32_t claimed_len) {
    if (!wave || !task) return;
    for (uint32_t i = 0; i < claimed_len; i++) {
        atomic_store_explicit(&wave->claimed[task->plan.resource_indices[i]], 0u,
                              memory_order_release);
    }
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_COST_RHO_PARALLEL_RELEASED_CLAIM, claimed_len);
}

static bool rhocost_parallel_try_claim(
    RhoCostParallelWave *wave, const RhoCostParallelTask *task,
    bool *out_claimed) {
    uint32_t acquired = 0;

    if (!wave || !task || !out_claimed || task->plan.resource_len == 0u) {
        return false;
    }
    *out_claimed = false;
    for (uint32_t i = 0; i < task->plan.resource_len; i++) {
        unsigned char expected = 0u;
        uint32_t index = task->plan.resource_indices[i];
        if (index >= wave->component_count) {
            cetta_runtime_stats_add(
                CETTA_RUNTIME_COUNTER_COST_RHO_PARALLEL_ACQUIRED_CLAIM,
                acquired);
            rhocost_parallel_release_claims(wave, task, acquired);
            return false;
        }
        if (!atomic_compare_exchange_strong_explicit(
                &wave->claimed[index], &expected, 1u,
                memory_order_acq_rel, memory_order_acquire)) {
            cetta_runtime_stats_add(
                CETTA_RUNTIME_COUNTER_COST_RHO_PARALLEL_ACQUIRED_CLAIM,
                acquired);
            rhocost_parallel_release_claims(wave, task, acquired);
            return true;
        }
        acquired++;
    }
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_COST_RHO_PARALLEL_ACQUIRED_CLAIM, acquired);
    *out_claimed = true;
    return true;
}

static bool rhocost_parallel_try_take_budget(RhoCostParallelWave *wave) {
    uint32_t old;
    if (!wave) return false;
    old = atomic_load_explicit(&wave->remaining_budget,
                               memory_order_relaxed);
    while (old != 0u) {
        if (atomic_compare_exchange_weak_explicit(
                &wave->remaining_budget, &old, old - 1u,
                memory_order_acq_rel, memory_order_relaxed)) {
            return true;
        }
    }
    return false;
}

static bool rhocost_parallel_process_task(CettaParallelWorker *worker,
                                          void *raw_task, void *user) {
    RhoCostParallelTask *task = raw_task;
    RhoCostParallelWave *wave = user;
    Arena *worker_arena = cetta_parallel_worker_arena(worker);
    bool claimed = task ? task->preclaimed : false;

    if (!task || !worker_arena) {
        return false;
    }
    if (!task->preclaimed &&
        !rhocost_parallel_try_claim(wave, task, &claimed)) return false;
    if (!claimed) return true;
    if (!task->preclaimed && !rhocost_parallel_try_take_budget(wave)) {
        rhocost_parallel_release_claims(
            wave, task, task->plan.resource_len);
        return true;
    }

    task->contractum =
        rhocost_subst_term(worker_arena, task->plan.recv_body,
                           task->plan.recv_binder, task->plan.send_payload);
    if (!task->contractum) {
        atomic_fetch_add_explicit(&wave->remaining_budget, 1u,
                                  memory_order_relaxed);
        rhocost_parallel_release_claims(wave, task, task->plan.resource_len);
        return false;
    }
    task->selected = true;
    return true;
}

static void rhocost_step_set_acc_free(RhoCostStepSetAcc *acc) {
    if (!acc) return;
    for (uint32_t i = 0; i < acc->len; i++) {
        rhocost_step_release(&acc->items[i]);
    }
    free(acc->items);
    *acc = (RhoCostStepSetAcc){0};
}

/* Keeps the step; a push that fails changes nothing. */
static bool rhocost_step_set_acc_push(RhoCostStepSetAcc *acc,
                                            Atom *result,
                                            Atom *consumed_sig,
                                            Atom *contractum,
                                            const uint32_t *participant_indices,
                                            uint32_t participant_len,
                                            RhoCostTokenVec *tokens,
                                            RhoIndexVec *chosen_tokens) {
    uint32_t *token_indices = NULL;
    if (participant_len > 2u) return false;
    if (acc->retain_provenance && chosen_tokens->len > 0) {
        token_indices = cetta_malloc(sizeof(uint32_t) * chosen_tokens->len);
        if (!token_indices) return false;
        for (uint32_t i = 0; i < chosen_tokens->len; i++) {
            token_indices[i] =
                tokens->items[chosen_tokens->items[i]].component_index;
        }
    }
    RhoCostStep step = {
        .result = result,
        .consumed_sig = consumed_sig,
        .contractum = acc->retain_provenance ? contractum : NULL,
        .participant_len = acc->retain_provenance ? participant_len : 0,
        .token_indices = token_indices,
        .token_len = acc->retain_provenance ? chosen_tokens->len : 0,
    };
    if (acc->retain_provenance) {
        for (uint32_t i = 0; i < participant_len; i++) {
            step.participant_indices[i] = participant_indices[i];
        }
    }
    if (!rhocost_push_step(rho_resize, &acc->items, &acc->len, &acc->cap, step)) {
        free(token_indices);
        return false;
    }
    return true;
}

static void rhocost_step_set_acc_finish(RhoCostStepSetAcc *acc,
                                        RhoCostStepSet *out) {
    if (acc->len > 1) {
        RhoCostOrderedStep *items =
            cetta_malloc(sizeof(RhoCostOrderedStep) * acc->len);
        for (uint32_t i = 0; i < acc->len; i++) {
            items[i].step = acc->items[i];
            items[i].ordinal = i;
        }
        qsort(items, acc->len, sizeof(RhoCostOrderedStep),
              rhocost_ordered_step_cmp);
        uint32_t write = 0;
        for (uint32_t i = 0; i < acc->len; i++) {
            if (write > 0 &&
                rhocost_step_order(&acc->items[write - 1], &items[i].step) == 0) {
                rhocost_step_release(&items[i].step);
                continue;
            }
            acc->items[write++] = items[i].step;
        }
        acc->len = write;
        free(items);
    }
    out->items = acc->items;
    out->len = acc->len;
    acc->items = NULL;
    acc->len = 0;
    acc->cap = 0;
    acc->retain_provenance = false;
}

static void rhocost_step_set_free(RhoCostStepSet *set) {
    if (!set) return;
    for (uint32_t i = 0; i < set->len; i++) {
        rhocost_step_release(&set->items[i]);
    }
    free(set->items);
    set->items = NULL;
    set->len = 0;
}

static bool rhocost_collect_signature_atoms(Atom *sig, RhoAtomVec *out) {
    if (rhocost_is_ground_signature(sig)) {
        return rho_vec_push(out, sig);
    }
    if (rhocost_is_sig_mul(sig)) {
        for (uint32_t i = 1; i < sig->expr.len; i++) {
            if (!rhocost_collect_signature_atoms(sig->expr.elems[i], out)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

static void rhocost_normalize_signature_vec(RhoAtomVec *vec) {
    if (vec->len > 1) {
        qsort(vec->items, vec->len, sizeof(Atom *), rhocost_sig_atom_cmp);
    }
}

static Atom *rhocost_signature_from_vec(Arena *arena, RhoAtomVec *vec) {
    rhocost_normalize_signature_vec(vec);
    if (vec->len == 0) return atom_symbol(arena, "rho:cost:sig-unit");
    if (vec->len == 1) return vec->items[0];
    return rhocost_sig_mul(arena, vec->items, vec->len);
}

static Atom *rhocost_normalize_signature(Arena *arena, Atom *sig) {
    RhoAtomVec atoms;
    Atom *out;
    rho_vec_init(&atoms);
    if (!rhocost_collect_signature_atoms(sig, &atoms)) {
        rho_vec_free(&atoms);
        return sig;
    }
    out = rhocost_signature_from_vec(arena, &atoms);
    rho_vec_free(&atoms);
    return out;
}

static bool rhocost_signature_vec_is_subset(RhoAtomVec *subset, RhoAtomVec *superset) {
    uint32_t i = 0;
    uint32_t j = 0;
    while (i < subset->len && j < superset->len) {
        int cmp = strcmp(atom_name_cstr(subset->items[i]),
                         atom_name_cstr(superset->items[j]));
        if (cmp == 0) {
            i++;
            j++;
            continue;
        }
        if (cmp > 0) {
            j++;
            continue;
        }
        return false;
    }
    return i == subset->len;
}

static bool rhocost_signature_vec_subtract(RhoAtomVec *superset,
                                           RhoAtomVec *subset,
                                           RhoAtomVec *out) {
    uint32_t i = 0;
    uint32_t j = 0;
    rho_vec_init(out);
    while (j < superset->len) {
        if (i < subset->len &&
            strcmp(atom_name_cstr(subset->items[i]),
                   atom_name_cstr(superset->items[j])) == 0) {
            i++;
            j++;
            continue;
        }
        if (!rho_vec_push(out, superset->items[j])) {
            rho_vec_free(out);
            return false;
        }
        j++;
    }
    return i == subset->len;
}

/* ── Cost grammar validation ────────────────────────────────────────────────
 *
 * The cost grammar is strict: signatures are ground symbols or sig-mul
 * products of them, raw token stacks occur only inside purses, a signed
 * body is a pure process (no rho:val / rho:eval-payload in this slice), and
 * drop is a wrapped term rather than a process body.  Rejections carry an
 * explicit diagnostic. */

static bool rhocost_check_term_rec(Atom *term);

static bool rhocost_check_signature(Atom *sig) {
    if (rhocost_is_ground_signature(sig)) return true;
    if (rhocost_is_sig_mul(sig)) {
        for (uint32_t i = 1; i < sig->expr.len; i++) {
            if (!rhocost_check_signature(sig->expr.elems[i])) {
                return false;
            }
        }
        return true;
    }
    rho_validation_set(
        "rhocalc cost slice only supports ground-signature products built with rho:cost:sig-mul");
    return false;
}

static bool rhocost_check_name(Atom *name) {
    if (!name) {
        rho_validation_set("missing cost-rho name");
        return false;
    }
    if (name->kind == ATOM_VAR) return true;
    {
        RhoView view = rho_view(name);
        if (view.kind == RHO_QUOTE && view.nargs == 1) {
            return rhocost_check_term_rec(view.args[0]);
        }
    }
    return rhocost_check_signature(name);
}

static bool rhocost_check_stack(Atom *term) {
    RhoCostTermView view = rhocost_term_view(term);
    if (view.kind == RHOCOST_TERM_STACK_EMPTY) return true;
    if (view.kind == RHOCOST_TERM_STACK_CONS && view.nargs == 2) {
        return rhocost_check_signature(view.args[0]) &&
               rhocost_check_stack(view.args[1]);
    }
    rho_validation_set("cost token stack must be () or s : S");
    return false;
}

static bool rhocost_check_proc(Atom *proc) {
    RhoView view = rho_view(proc);
    switch (view.kind) {
    case RHO_NIL:
        return true;
    case RHO_PAR:
        for (uint32_t i = 0; i < view.nargs; i++) {
            if (!rhocost_check_proc(view.args[i])) return false;
        }
        return true;
    case RHO_SEND:
        if (view.nargs != 2) {
            rho_validation_set(
                "cost-rho send expects channel and signed-term payload");
            return false;
        }
        return rhocost_check_name(view.args[0]) &&
               rhocost_check_term_rec(view.args[1]);
    case RHO_RECV:
        if (view.nargs != 3) {
            rho_validation_set(
                "cost-rho recv expects channel, binder, and signed-term body");
            return false;
        }
        if (view.args[1]->kind != ATOM_VAR) {
            rho_validation_set("cost-rho recv binder must be a variable");
            return false;
        }
        return rhocost_check_name(view.args[0]) &&
               rhocost_check_term_rec(view.args[2]);
    case RHO_RECV_PERSISTENT:
        rho_validation_set(
            "pure cost-rho excludes rho:recv-persistent");
        return false;
    case RHO_DROP:
        rho_validation_set(
            "cost-rho drop is a wrapped term, not a process body");
        return false;
    case RHO_VAL:
        rho_validation_set("pure cost-rho excludes rho:val");
        return false;
    case RHO_EVAL_PAYLOAD:
        rho_validation_set("pure cost-rho excludes rho:eval-payload");
        return false;
    case RHO_QUOTE:
        rho_validation_set("rho:quote is a name, not a process");
        return false;
    case RHO_BAD:
        break;
    }
    if (proc && proc->kind == ATOM_EXPR && proc->expr.len > 0 &&
        proc->expr.elems[0]->kind == ATOM_SYMBOL) {
        rho_validation_set("unsupported cost-rho process form '%s'",
                           atom_name_cstr(proc->expr.elems[0]));
    } else {
        rho_validation_set("unsupported cost-rho process atom");
    }
    return false;
}

static bool rhocost_check_term_rec(Atom *term) {
    RhoCostTermView view = rhocost_term_view(term);
    switch (view.kind) {
    case RHOCOST_TERM_NIL:
        return true;
    case RHOCOST_TERM_STACK_EMPTY:
    case RHOCOST_TERM_STACK_CONS:
        rho_validation_set(
            "raw cost stack must occur inside rho:cost:purse");
        return false;
    case RHOCOST_TERM_DROP:
        if (view.nargs != 1) {
            rho_validation_set("rho:drop expects one cost-rho name");
            return false;
        }
        return rhocost_check_name(view.args[0]);
    case RHOCOST_TERM_PURSE:
        if (view.nargs != 2) {
            rho_validation_set("rho:cost:purse expects location and stack");
            return false;
        }
        return rhocost_check_name(view.args[0]) &&
               rhocost_check_stack(view.args[1]);
    case RHOCOST_TERM_SIGNED:
        if (view.nargs != 2) {
            rho_validation_set("rho:cost:signed expects process body and signature");
            return false;
        }
        return rhocost_check_proc(view.args[0]) &&
               rhocost_check_signature(view.args[1]);
    case RHOCOST_TERM_PAR:
        for (uint32_t i = 0; i < view.nargs; i++) {
            if (!rhocost_check_term_rec(view.args[i])) return false;
        }
        return true;
    case RHOCOST_TERM_BAD:
        break;
    }
    rho_validation_set("unsupported cost-rho term");
    return false;
}

static bool rhocost_term_well_formed(Atom *term) {
    rho_validation_clear();
    return rhocost_check_term_rec(term);
}

static void rhocost_token_vec_init(RhoCostTokenVec *vec) {
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static void rhocost_token_vec_free(RhoCostTokenVec *vec) {
    free(vec->items);
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static bool rhocost_token_vec_push(RhoCostTokenVec *vec,
                                   uint32_t component_index,
                                   Atom *location,
                                   Atom *sig,
                                   Atom *rest) {
    return RHO_PUSH(rhocost_push_token, vec,
                    ((RhoCostToken){component_index, location, sig, rest}));
}

static void rhocost_signed_endpoint_vec_init(RhoCostSignedEndpointVec *vec) {
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static void rhocost_signed_endpoint_vec_free(RhoCostSignedEndpointVec *vec) {
    free(vec->items);
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static bool rhocost_signed_endpoint_vec_push(RhoCostSignedEndpointVec *vec,
                                             uint32_t component_index,
                                             Atom *sig,
                                             RhoView view) {
    return RHO_PUSH(rhocost_push_signed_endpoint, vec,
                    ((RhoCostSignedEndpoint){component_index, sig, view}));
}

static void rhocost_whole_redex_vec_init(RhoCostWholeRedexVec *vec) {
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static void rhocost_whole_redex_vec_free(RhoCostWholeRedexVec *vec) {
    free(vec->items);
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static bool rhocost_whole_redex_vec_push(RhoCostWholeRedexVec *vec,
                                         uint32_t component_index,
                                         Atom *sig,
                                         RhoView recv,
                                         RhoView send) {
    return RHO_PUSH(rhocost_push_whole_redex, vec,
                    ((RhoCostWholeRedex){component_index, sig, recv, send}));
}

/* ── Cost structural order and normalization ────────────────────────────────
 *
 * The cost layer orders its extended grammar as the strict core orders
 * processes: by the written form taken symbol by symbol, constructors ranked
 * as their names spell.  Terms: (malformed) < drop < purse < sempty < signed
 * < stack < tnil < tpar; processes: 0 < (malformed) < drop < par < recv <
 * send; names: variable < (malformed) < (malformed signature) < quotation <
 * signature.  A signature is its sorted multiset of ground atoms, each
 * written as its length, ":" and its spelling.  Term-par components sort by
 * this order, signatures normalize to sorted multisets, and purse locations
 * compare through the same name equivalence as channels. */

static Atom *rhocost_normalize_term(Arena *arena, Atom *term);

enum {
    RHOCOST_PROC_RANK_NIL = 0,
    RHOCOST_PROC_RANK_BAD,
    RHOCOST_PROC_RANK_DROP,
    RHOCOST_PROC_RANK_PAR,
    RHOCOST_PROC_RANK_RECV,
    RHOCOST_PROC_RANK_SEND,
};

enum {
    RHOCOST_TERM_RANK_BAD = 0,
    RHOCOST_TERM_RANK_DROP,
    RHOCOST_TERM_RANK_PURSE,
    RHOCOST_TERM_RANK_SEMPTY,
    RHOCOST_TERM_RANK_SIGNED,
    RHOCOST_TERM_RANK_STACK,
    RHOCOST_TERM_RANK_TNIL,
    RHOCOST_TERM_RANK_TPAR,
};

enum {
    RHOCOST_NAME_RANK_VAR = 0,
    RHOCOST_NAME_RANK_BAD,
    RHOCOST_NAME_RANK_BAD_SIGNATURE,
    RHOCOST_NAME_RANK_QUOTE,
    RHOCOST_NAME_RANK_SIGNATURE,
};

static int rhocost_proc_rank(const RhoView *view) {
    switch (view->kind) {
    case RHO_NIL:
        return RHOCOST_PROC_RANK_NIL;
    case RHO_PAR:
        return RHOCOST_PROC_RANK_PAR;
    case RHO_SEND:
        return view->nargs == 2u ? RHOCOST_PROC_RANK_SEND
                                 : RHOCOST_PROC_RANK_BAD;
    case RHO_RECV:
        return view->nargs == 3u && view->args[1]->kind == ATOM_VAR
            ? RHOCOST_PROC_RANK_RECV : RHOCOST_PROC_RANK_BAD;
    case RHO_DROP:
        return view->nargs == 1u ? RHOCOST_PROC_RANK_DROP
                                 : RHOCOST_PROC_RANK_BAD;
    default:
        return RHOCOST_PROC_RANK_BAD;
    }
}

static int rhocost_term_rank(const RhoCostTermView *view) {
    switch (view->kind) {
    case RHOCOST_TERM_NIL:
        return RHOCOST_TERM_RANK_TNIL;
    case RHOCOST_TERM_STACK_EMPTY:
        return RHOCOST_TERM_RANK_SEMPTY;
    case RHOCOST_TERM_STACK_CONS:
        return view->nargs == 2u ? RHOCOST_TERM_RANK_STACK
                                 : RHOCOST_TERM_RANK_BAD;
    case RHOCOST_TERM_DROP:
        return view->nargs == 1u ? RHOCOST_TERM_RANK_DROP
                                 : RHOCOST_TERM_RANK_BAD;
    case RHOCOST_TERM_PURSE:
        return view->nargs == 2u ? RHOCOST_TERM_RANK_PURSE
                                 : RHOCOST_TERM_RANK_BAD;
    case RHOCOST_TERM_SIGNED:
        return view->nargs == 2u ? RHOCOST_TERM_RANK_SIGNED
                                 : RHOCOST_TERM_RANK_BAD;
    case RHOCOST_TERM_PAR:
        return RHOCOST_TERM_RANK_TPAR;
    case RHOCOST_TERM_BAD:
        break;
    }
    return RHOCOST_TERM_RANK_BAD;
}

/* Whether a signature's atoms can be collected: a ground atom, or a product
 * of such. */
static bool rhocost_signature_collectible(Atom *sig) {
    if (rhocost_is_ground_signature(sig)) return true;
    if (!rhocost_is_sig_mul(sig)) return false;
    for (uint32_t i = 1u; i < sig->expr.len; i++) {
        if (!rhocost_signature_collectible(sig->expr.elems[i])) return false;
    }
    return true;
}

/* @(*x) = x through a cost drop. */
static Atom *rhocost_name_collapse(Atom *name) {
    for (;;) {
        RhoView view;
        RhoCostTermView inner;
        if (!name || name->kind == ATOM_VAR) return name;
        view = rho_view(name);
        if (view.kind != RHO_QUOTE || view.nargs != 1u) return name;
        inner = rhocost_term_view(view.args[0]);
        if (inner.kind != RHOCOST_TERM_DROP || inner.nargs != 1u) return name;
        name = inner.args[0];
    }
}

static int rhocost_name_rank(Atom *name, Atom **quoted) {
    RhoView view;
    *quoted = NULL;
    if (!name) return RHOCOST_NAME_RANK_BAD;
    if (name->kind == ATOM_VAR) return RHOCOST_NAME_RANK_VAR;
    view = rho_view(name);
    if (view.kind == RHO_QUOTE && view.nargs == 1u) {
        *quoted = view.args[0];
        return RHOCOST_NAME_RANK_QUOTE;
    }
    if (rhocost_is_ground_signature(name) || rhocost_is_sig_mul(name)) {
        return rhocost_signature_collectible(name)
            ? RHOCOST_NAME_RANK_SIGNATURE : RHOCOST_NAME_RANK_BAD_SIGNATURE;
    }
    return RHOCOST_NAME_RANK_BAD;
}

/* One atom of a signature written "length:spelling", followed by `end`. */
static int rhocost_signature_atom_order(Atom *a, char a_end,
                                        Atom *b, char b_end) {
    const char *na = atom_name_cstr(a);
    const char *nb = atom_name_cstr(b);
    size_t la = strlen(na);
    size_t lb = strlen(nb);
    char pa[24];
    char pb[24];
    char *da = rho_decimal(la, pa + sizeof(pa) - 1u);
    char *db = rho_decimal(lb, pb + sizeof(pb) - 1u);
    size_t dla = (size_t)(pa + sizeof(pa) - 1u - da);
    size_t dlb = (size_t)(pb + sizeof(pb) - 1u - db);
    int c;
    pa[sizeof(pa) - 1u] = ':';
    pb[sizeof(pb) - 1u] = ':';
    if (dla != dlb || memcmp(da, db, dla) != 0) {
        /* The lengths differ: their digits, each followed by ':'. */
        return rho_text_order(da, dla + 1u, a_end, db, dlb + 1u, b_end);
    }
    c = rho_text_order(na, la, a_end, nb, lb, b_end);
    return c;
}

/* Signatures as written: "?bad-sig" before any "sig(...)", whose atoms, in
 * sorted order, compare in turn. */
static int rhocost_order_signature(Atom *a, Atom *b) {
    RhoAtomVec va;
    RhoAtomVec vb;
    bool good_a;
    bool good_b;
    int c = 0;
    if (a == b) return 0;
    rho_vec_init(&va);
    rho_vec_init(&vb);
    good_a = rhocost_collect_signature_atoms(a, &va);
    good_b = rhocost_collect_signature_atoms(b, &vb);
    if (good_a != good_b) {
        c = good_a ? 1 : -1;
    } else if (good_a) {
        uint32_t common = va.len < vb.len ? va.len : vb.len;
        rhocost_normalize_signature_vec(&va);
        rhocost_normalize_signature_vec(&vb);
        for (uint32_t i = 0u; i < common && c == 0; i++) {
            c = rhocost_signature_atom_order(
                va.items[i], i + 1u < va.len ? ',' : ')',
                vb.items[i], i + 1u < vb.len ? ',' : ')');
        }
        if (c == 0 && va.len != vb.len) c = va.len < vb.len ? -1 : 1;
    }
    rho_vec_free(&va);
    rho_vec_free(&vb);
    return c;
}

static int rhocost_order_proc(RhoOrder *o, Atom *a, Atom *b);
static int rhocost_order_term(RhoOrder *o, Atom *a, Atom *b);

static int rhocost_order_name(RhoOrder *o, Atom *a, Atom *b, char end) {
    Atom *qa = NULL;
    Atom *qb = NULL;
    int ra;
    int rb;
    a = rhocost_name_collapse(a);
    b = rhocost_name_collapse(b);
    ra = rhocost_name_rank(a, &qa);
    rb = rhocost_name_rank(b, &qb);
    if (ra != rb) return ra < rb ? -1 : 1;
    switch (ra) {
    case RHOCOST_NAME_RANK_VAR:
        return rho_order_name_var(o, a, b, end);
    case RHOCOST_NAME_RANK_SIGNATURE:
        return rhocost_order_signature(a, b);
    case RHOCOST_NAME_RANK_QUOTE:
        if (qa != qb) {
            uint32_t base = o->base;
            uint32_t next = o->next;
            bool aligned = o->aligned;
            int c;
            o->base = o->len;
            o->next = 0u;
            o->aligned = true;
            o->quote_depth++;
            c = rhocost_order_term(o, qa, qb);
            o->quote_depth--;
            o->base = base;
            o->next = next;
            o->aligned = aligned;
            return c;
        }
        return 0;
    default:
        return 0;
    }
}

static int rhocost_order_proc(RhoOrder *o, Atom *a, Atom *b) {
    RhoView va;
    RhoView vb;
    int ra;
    int rb;
    int c;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_RHO_ORDER_VISIT);
    if (a == b && !atom_has_vars(a)) return 0;
    va = rho_view(a);
    vb = rho_view(b);
    ra = rhocost_proc_rank(&va);
    rb = rhocost_proc_rank(&vb);
    if (ra != rb) return ra < rb ? -1 : 1;
    switch (ra) {
    case RHOCOST_PROC_RANK_PAR: {
        uint32_t common = va.nargs < vb.nargs ? va.nargs : vb.nargs;
        for (uint32_t i = 0u; i < common; i++) {
            c = rhocost_order_proc(o, va.args[i], vb.args[i]);
            if (c != 0) return c;
        }
        return va.nargs < vb.nargs ? -1 : va.nargs > vb.nargs ? 1 : 0;
    }
    case RHOCOST_PROC_RANK_SEND:
        c = rhocost_order_name(o, va.args[0], vb.args[0], ',');
        return c != 0 ? c : rhocost_order_term(o, va.args[1], vb.args[1]);
    case RHOCOST_PROC_RANK_RECV:
        c = rhocost_order_name(o, va.args[0], vb.args[0], ',');
        if (c != 0) return c;
        rho_order_push(o, va.args[1]->var_id, vb.args[1]->var_id);
        c = rhocost_order_term(o, va.args[2], vb.args[2]);
        rho_order_pop(o);
        return c;
    case RHOCOST_PROC_RANK_DROP:
        return rhocost_order_name(o, va.args[0], vb.args[0], ')');
    default:
        return 0;
    }
}

static int rhocost_order_term(RhoOrder *o, Atom *a, Atom *b) {
    RhoCostTermView va;
    RhoCostTermView vb;
    int ra;
    int rb;
    int c;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_RHO_ORDER_VISIT);
    if (a == b && !atom_has_vars(a)) return 0;
    va = rhocost_term_view(a);
    vb = rhocost_term_view(b);
    ra = rhocost_term_rank(&va);
    rb = rhocost_term_rank(&vb);
    if (ra != rb) return ra < rb ? -1 : 1;
    switch (ra) {
    case RHOCOST_TERM_RANK_STACK:
        c = rhocost_order_signature(va.args[0], vb.args[0]);
        return c != 0 ? c : rhocost_order_term(o, va.args[1], vb.args[1]);
    case RHOCOST_TERM_RANK_DROP:
        return rhocost_order_name(o, va.args[0], vb.args[0], ')');
    case RHOCOST_TERM_RANK_PURSE:
        c = rhocost_order_name(o, va.args[0], vb.args[0], ',');
        return c != 0 ? c : rhocost_order_term(o, va.args[1], vb.args[1]);
    case RHOCOST_TERM_RANK_SIGNED:
        c = rhocost_order_proc(o, va.args[0], vb.args[0]);
        return c != 0 ? c : rhocost_order_signature(va.args[1], vb.args[1]);
    case RHOCOST_TERM_RANK_TPAR: {
        uint32_t common = va.nargs < vb.nargs ? va.nargs : vb.nargs;
        for (uint32_t i = 0u; i < common; i++) {
            c = rhocost_order_term(o, va.args[i], vb.args[i]);
            if (c != 0) return c;
        }
        return va.nargs < vb.nargs ? -1 : va.nargs > vb.nargs ? 1 : 0;
    }
    default:
        return 0;
    }
}

static int rhocost_proc_order(Atom *a, Atom *b) {
    RhoOrder o;
    int c;
    if (a == b) return 0;
    rho_order_init(&o);
    c = rhocost_order_proc(&o, a, b);
    rho_order_free(&o);
    return c;
}

static int rhocost_term_order(Atom *a, Atom *b) {
    RhoOrder o;
    int c;
    if (a == b) return 0;
    rho_order_init(&o);
    c = rhocost_order_term(&o, a, b);
    rho_order_free(&o);
    return c;
}

/* Two names standing alone, as a channel or a purse location. */
static bool rhocost_names_equal(Atom *a, Atom *b) {
    RhoOrder o;
    int c;
    if (a == b) return true;
    rho_order_init(&o);
    c = rhocost_order_name(&o, a, b, '\0');
    rho_order_free(&o);
    return c == 0;
}

/* Funded steps: by the term they reach, then by the signature they consume. */
static int rhocost_step_order(const RhoCostStep *a, const RhoCostStep *b) {
    int c = rhocost_term_order(a->result, b->result);
    return c != 0 ? c : rhocost_order_signature(a->consumed_sig,
                                                b->consumed_sig);
}

static void rhocost_collect_proc_par(Arena *arena, Atom *proc, RhoAtomVec *out);

static Atom *rhocost_proc_par_from_vec(Arena *arena, RhoAtomVec *vec) {
    return rho_par_sorted(arena, vec, rhocost_proc_order);
}

static Atom *rhocost_normalize_name(Arena *arena, Atom *name) {
    if (name->kind == ATOM_VAR) return name;
    if (rhocost_is_ground_signature(name) || rhocost_is_sig_mul(name)) {
        return rhocost_normalize_signature(arena, name);
    }
    {
        RhoView view = rho_view(name);
        if (view.kind == RHO_QUOTE && view.nargs == 1) {
            Atom *term = rhocost_normalize_term(arena, view.args[0]);
            RhoCostTermView term_view = rhocost_term_view(term);
            if (term_view.kind == RHOCOST_TERM_DROP &&
                term_view.nargs == 1) {
                return rhocost_normalize_name(arena, term_view.args[0]);
            }
            return rho_unary(arena, "rho:quote", term);
        }
    }
    return name;
}

static Atom *rhocost_normalize_proc(Arena *arena, Atom *proc) {
    RhoView view = rho_view(proc);
    switch (view.kind) {
    case RHO_NIL:
        return rho_nil(arena);
    case RHO_PAR: {
        RhoAtomVec flat;
        rho_vec_init(&flat);
        for (uint32_t i = 0; i < view.nargs; i++) {
            rhocost_collect_proc_par(arena, view.args[i], &flat);
        }
        proc = rhocost_proc_par_from_vec(arena, &flat);
        rho_vec_free(&flat);
        return proc;
    }
    case RHO_SEND:
        return rho_binary(arena, "rho:send",
                          rhocost_normalize_name(arena, view.args[0]),
                          rhocost_normalize_term(arena, view.args[1]));
    case RHO_RECV:
        return rho_ternary(arena, "rho:recv",
                           rhocost_normalize_name(arena, view.args[0]),
                           view.args[1],
                           rhocost_normalize_term(arena, view.args[2]));
    case RHO_DROP:
        return rho_unary(arena, "rho:drop",
                         rhocost_normalize_name(arena, view.args[0]));
    case RHO_VAL:
    case RHO_EVAL_PAYLOAD:
    case RHO_RECV_PERSISTENT:
    case RHO_QUOTE:
    case RHO_BAD:
        break;
    }
    return proc;
}

static void rhocost_collect_proc_par(Arena *arena, Atom *proc, RhoAtomVec *out) {
    Atom *norm = rhocost_normalize_proc(arena, proc);
    RhoView view = rho_view(norm);
    if (view.kind == RHO_NIL) return;
    if (view.kind == RHO_PAR) {
        for (uint32_t i = 0; i < view.nargs; i++) {
            rhocost_collect_proc_par(arena, view.args[i], out);
        }
        return;
    }
    (void)rho_vec_push(out, norm);
}

static void rhocost_collect_term_par(Arena *arena, Atom *term, RhoAtomVec *out);

static Atom *rhocost_term_par_from_vec(Arena *arena, RhoAtomVec *vec) {
    if (vec->len == 0) return rhocost_nil(arena);
    if (vec->len == 1) return vec->items[0];
    Atom **args = arena_alloc(arena, sizeof(Atom *) * vec->len);
    memcpy(args, vec->items, sizeof(Atom *) * vec->len);
    rho_sort_atoms(args, vec->len, rhocost_term_order);
    return rhocost_par(arena, args, vec->len);
}

static Atom *rhocost_normalize_stack(Arena *arena, Atom *stack) {
    RhoCostTermView view = rhocost_term_view(stack);
    if (view.kind == RHOCOST_TERM_STACK_EMPTY) {
        return rhocost_stack_empty(arena);
    }
    if (view.kind == RHOCOST_TERM_STACK_CONS && view.nargs == 2) {
        return rhocost_stack_cons(
            arena, rhocost_normalize_signature(arena, view.args[0]),
            rhocost_normalize_stack(arena, view.args[1]));
    }
    return stack;
}

static Atom *rhocost_normalize_term(Arena *arena, Atom *term) {
    RhoCostTermView view = rhocost_term_view(term);
    switch (view.kind) {
    case RHOCOST_TERM_NIL:
        return rhocost_nil(arena);
    case RHOCOST_TERM_STACK_EMPTY:
    case RHOCOST_TERM_STACK_CONS:
        return rhocost_normalize_stack(arena, term);
    case RHOCOST_TERM_DROP:
        return rho_unary(arena, "rho:drop",
                         rhocost_normalize_name(arena, view.args[0]));
    case RHOCOST_TERM_PURSE:
        return rhocost_purse(
            arena, rhocost_normalize_name(arena, view.args[0]),
            rhocost_normalize_stack(arena, view.args[1]));
    case RHOCOST_TERM_SIGNED:
        return rhocost_signed(arena,
                              rhocost_normalize_proc(arena, view.args[0]),
                              rhocost_normalize_signature(arena, view.args[1]));
    case RHOCOST_TERM_PAR: {
        RhoAtomVec flat;
        rho_vec_init(&flat);
        for (uint32_t i = 0; i < view.nargs; i++) {
            rhocost_collect_term_par(arena, view.args[i], &flat);
        }
        term = rhocost_term_par_from_vec(arena, &flat);
        rho_vec_free(&flat);
        return term;
    }
    case RHOCOST_TERM_BAD:
        break;
    }
    return term;
}

static void rhocost_collect_term_par(Arena *arena, Atom *term, RhoAtomVec *out) {
    Atom *norm = rhocost_normalize_term(arena, term);
    RhoCostTermView view = rhocost_term_view(norm);
    if (view.kind == RHOCOST_TERM_NIL) return;
    if (view.kind == RHOCOST_TERM_PAR) {
        for (uint32_t i = 0; i < view.nargs; i++) {
            rhocost_collect_term_par(arena, view.args[i], out);
        }
        return;
    }
    (void)rho_vec_push(out, norm);
}

/* ── Cost substitution ──────────────────────────────────────────────────────
 *
 * Cost COMM substitutes the sent signed term for the receive binder, with
 * the same capture-avoidance and drop-unquoting discipline as the pure
 * layer, extended through signed bodies and purse locations. */

static bool rhocost_term_has_free_var(Atom *term, VarId var_id);

static bool rhocost_name_has_free_var(Atom *name, VarId var_id) {
    if (!name) return false;
    if (name->kind == ATOM_VAR) return var_id_eq(name->var_id, var_id);
    {
        RhoView view = rho_view(name);
        if (view.kind == RHO_QUOTE && view.nargs == 1) {
            return rhocost_term_has_free_var(view.args[0], var_id);
        }
    }
    return false;
}

static bool rhocost_proc_has_free_var(Atom *proc, VarId var_id) {
    RhoView view = rho_view(proc);
    switch (view.kind) {
    case RHO_NIL:
        return false;
    case RHO_PAR:
        for (uint32_t i = 0; i < view.nargs; i++) {
            if (rhocost_proc_has_free_var(view.args[i], var_id)) return true;
        }
        return false;
    case RHO_SEND:
        return view.nargs == 2 &&
               (rhocost_name_has_free_var(view.args[0], var_id) ||
                rhocost_term_has_free_var(view.args[1], var_id));
    case RHO_RECV:
        if (view.nargs != 3) return false;
        if (rhocost_name_has_free_var(view.args[0], var_id)) return true;
        if (view.args[1]->kind == ATOM_VAR &&
            var_id_eq(view.args[1]->var_id, var_id)) {
            return false;
        }
        return rhocost_term_has_free_var(view.args[2], var_id);
    case RHO_DROP:
    case RHO_VAL:
    case RHO_EVAL_PAYLOAD:
    case RHO_RECV_PERSISTENT:
    case RHO_QUOTE:
    case RHO_BAD:
        break;
    }
    return false;
}

static bool rhocost_term_has_free_var(Atom *term, VarId var_id) {
    RhoCostTermView view = rhocost_term_view(term);
    switch (view.kind) {
    case RHOCOST_TERM_NIL:
    case RHOCOST_TERM_STACK_EMPTY:
    case RHOCOST_TERM_STACK_CONS:
        return false;
    case RHOCOST_TERM_DROP:
        return view.nargs == 1 &&
               rhocost_name_has_free_var(view.args[0], var_id);
    case RHOCOST_TERM_PURSE:
        return view.nargs == 2 &&
               rhocost_name_has_free_var(view.args[0], var_id);
    case RHOCOST_TERM_SIGNED:
        return rhocost_proc_has_free_var(view.args[0], var_id);
    case RHOCOST_TERM_PAR:
        for (uint32_t i = 0; i < view.nargs; i++) {
            if (rhocost_term_has_free_var(view.args[i], var_id)) return true;
        }
        return false;
    case RHOCOST_TERM_BAD:
        break;
    }
    return false;
}

static Atom *rhocost_rename_term(Arena *arena, Atom *term,
                                 VarId old_id, Atom *replacement_name);

static Atom *rhocost_rename_name(Arena *arena, Atom *name,
                                 VarId old_id, Atom *replacement_name) {
    Atom *norm = rhocost_normalize_name(arena, name);
    if (norm->kind == ATOM_VAR && var_id_eq(norm->var_id, old_id)) {
        return replacement_name;
    }
    return norm;
}

static Atom *rhocost_rename_proc(Arena *arena, Atom *proc,
                                 VarId old_id, Atom *replacement_name) {
    RhoView view = rho_view(proc);
    switch (view.kind) {
    case RHO_NIL:
        return rho_nil(arena);
    case RHO_PAR: {
        Atom **args = arena_alloc(arena, sizeof(Atom *) * view.nargs);
        for (uint32_t i = 0; i < view.nargs; i++) {
            args[i] = rhocost_rename_proc(arena, view.args[i], old_id,
                                          replacement_name);
        }
        return rhocost_normalize_proc(arena, rho_call(arena, "rho:par",
                                                      args, view.nargs));
    }
    case RHO_SEND:
        return rhocost_normalize_proc(arena,
            rho_binary(arena, "rho:send",
                       rhocost_rename_name(arena, view.args[0], old_id,
                                           replacement_name),
                       rhocost_rename_term(arena, view.args[1], old_id,
                                           replacement_name)));
    case RHO_RECV:
        if (view.args[1]->kind == ATOM_VAR &&
            var_id_eq(view.args[1]->var_id, old_id)) {
            return rho_ternary(arena, "rho:recv",
                               rhocost_rename_name(arena, view.args[0], old_id,
                                                   replacement_name),
                               view.args[1],
                               view.args[2]);
        }
        return rhocost_normalize_proc(arena,
            rho_ternary(arena, "rho:recv",
                        rhocost_rename_name(arena, view.args[0], old_id,
                                            replacement_name),
                        view.args[1],
                        rhocost_rename_term(arena, view.args[2], old_id,
                                            replacement_name)));
    case RHO_DROP:
        return rho_unary(arena, "rho:drop",
                         rhocost_rename_name(arena, view.args[0], old_id,
                                             replacement_name));
    case RHO_VAL:
    case RHO_EVAL_PAYLOAD:
    case RHO_RECV_PERSISTENT:
    case RHO_QUOTE:
    case RHO_BAD:
        break;
    }
    return proc;
}

static Atom *rhocost_rename_term(Arena *arena, Atom *term,
                                 VarId old_id, Atom *replacement_name) {
    RhoCostTermView view = rhocost_term_view(term);
    switch (view.kind) {
    case RHOCOST_TERM_NIL:
        return rhocost_nil(arena);
    case RHOCOST_TERM_STACK_EMPTY:
        return rhocost_stack_empty(arena);
    case RHOCOST_TERM_STACK_CONS:
        return rhocost_normalize_stack(arena, term);
    case RHOCOST_TERM_DROP:
        return rho_unary(
            arena, "rho:drop",
            rhocost_rename_name(arena, view.args[0], old_id,
                                replacement_name));
    case RHOCOST_TERM_PURSE:
        return rhocost_purse(
            arena,
            rhocost_rename_name(arena, view.args[0], old_id,
                                replacement_name),
            rhocost_normalize_stack(arena, view.args[1]));
    case RHOCOST_TERM_SIGNED:
        return rhocost_signed(arena,
                              rhocost_rename_proc(arena, view.args[0], old_id,
                                                  replacement_name),
                              view.args[1]);
    case RHOCOST_TERM_PAR: {
        Atom **args = arena_alloc(arena, sizeof(Atom *) * view.nargs);
        for (uint32_t i = 0; i < view.nargs; i++) {
            args[i] = rhocost_rename_term(arena, view.args[i], old_id,
                                          replacement_name);
        }
        return rhocost_normalize_term(arena, rhocost_par(arena, args, view.nargs));
    }
    case RHOCOST_TERM_BAD:
        break;
    }
    return term;
}

static Atom *rhocost_subst_term(Arena *arena, Atom *term,
                                VarId var_id, Atom *replacement_term);

static Atom *rhocost_subst_name_mark(Arena *arena, Atom *name,
                                     VarId var_id, Atom *replacement_term,
                                     bool *matched) {
    Atom *norm = rhocost_normalize_name(arena, name);
    *matched = false;
    if (norm->kind == ATOM_VAR && var_id_eq(norm->var_id, var_id)) {
        *matched = true;
        return rho_unary(arena, "rho:quote",
                         rhocost_normalize_term(arena, replacement_term));
    }
    return norm;
}

static Atom *rhocost_subst_name(Arena *arena, Atom *name,
                                VarId var_id, Atom *replacement_term) {
    bool matched;
    return rhocost_subst_name_mark(arena, name, var_id, replacement_term, &matched);
}

static Atom *rhocost_subst_proc(Arena *arena, Atom *proc,
                                VarId var_id, Atom *replacement_term) {
    RhoView view = rho_view(proc);
    switch (view.kind) {
    case RHO_NIL:
        return rho_nil(arena);
    case RHO_PAR: {
        Atom **args = arena_alloc(arena, sizeof(Atom *) * view.nargs);
        for (uint32_t i = 0; i < view.nargs; i++) {
            args[i] = rhocost_subst_proc(arena, view.args[i], var_id,
                                         replacement_term);
        }
        return rhocost_normalize_proc(arena, rho_call(arena, "rho:par",
                                                      args, view.nargs));
    }
    case RHO_SEND:
        return rhocost_normalize_proc(arena,
            rho_binary(arena, "rho:send",
                       rhocost_subst_name(arena, view.args[0], var_id,
                                          replacement_term),
                       rhocost_subst_term(arena, view.args[1], var_id,
                                          replacement_term)));
    case RHO_RECV:
        if (view.args[1]->kind == ATOM_VAR &&
            var_id_eq(view.args[1]->var_id, var_id)) {
            return rho_ternary(arena, "rho:recv",
                               rhocost_subst_name(arena, view.args[0], var_id,
                                                  replacement_term),
                               view.args[1],
                               view.args[2]);
        }
        {
            Atom *binder = view.args[1];
            Atom *body = view.args[2];
            if (binder->kind == ATOM_VAR &&
                rhocost_term_has_free_var(replacement_term, binder->var_id)) {
                Atom *fresh = rho_fresh_var_like(arena, binder);
                body = rhocost_rename_term(arena, body, binder->var_id, fresh);
                binder = fresh;
            }
            return rhocost_normalize_proc(arena,
                rho_ternary(arena, "rho:recv",
                            rhocost_subst_name(arena, view.args[0], var_id,
                                               replacement_term),
                            binder,
                            rhocost_subst_term(arena, body, var_id,
                                               replacement_term)));
        }
    case RHO_DROP:
        return rho_unary(arena, "rho:drop",
                         rhocost_subst_name(arena, view.args[0], var_id,
                                            replacement_term));
    case RHO_VAL:
    case RHO_EVAL_PAYLOAD:
    case RHO_RECV_PERSISTENT:
    case RHO_QUOTE:
    case RHO_BAD:
        break;
    }
    return proc;
}

static Atom *rhocost_subst_term(Arena *arena, Atom *term,
                                VarId var_id, Atom *replacement_term) {
    RhoCostTermView view = rhocost_term_view(term);
    switch (view.kind) {
    case RHOCOST_TERM_NIL:
        return rhocost_nil(arena);
    case RHOCOST_TERM_STACK_EMPTY:
        return rhocost_stack_empty(arena);
    case RHOCOST_TERM_STACK_CONS:
        return rhocost_normalize_stack(arena, term);
    case RHOCOST_TERM_DROP: {
        bool matched = false;
        Atom *name = rhocost_subst_name_mark(
            arena, view.args[0], var_id, replacement_term, &matched);
        RhoView name_view = rho_view(name);
        if (matched && name_view.kind == RHO_QUOTE &&
            name_view.nargs == 1) {
            return rhocost_normalize_term(arena, name_view.args[0]);
        }
        return rho_unary(arena, "rho:drop", name);
    }
    case RHOCOST_TERM_PURSE:
        return rhocost_purse(
            arena,
            rhocost_subst_name(arena, view.args[0], var_id,
                                replacement_term),
            rhocost_normalize_stack(arena, view.args[1]));
    case RHOCOST_TERM_SIGNED:
        return rhocost_signed(arena,
                              rhocost_subst_proc(arena, view.args[0], var_id,
                                                 replacement_term),
                              view.args[1]);
    case RHOCOST_TERM_PAR: {
        Atom **args = arena_alloc(arena, sizeof(Atom *) * view.nargs);
        for (uint32_t i = 0; i < view.nargs; i++) {
            args[i] = rhocost_subst_term(arena, view.args[i], var_id,
                                         replacement_term);
        }
        return rhocost_normalize_term(arena, rhocost_par(arena, args, view.nargs));
    }
    case RHOCOST_TERM_BAD:
        break;
    }
    return term;
}

/* ── Funded-candidate collection ────────────────────────────────────────────
 *
 * A firing candidate is either a split redex — a signed receive and a
 * signed send on the same channel, whose signatures multiply into the
 * demand — or a whole redex, one signed par of a matched receive/send pair
 * demanding its single signature.  Purse tokens on the same channel are
 * the only funding sources.  Every candidate must be exactly covered:
 * chosen token signatures partition the demand with nothing left over. */

static bool rhocost_signed_body_endpoint(uint32_t component_index,
                                         Atom *term,
                                         RhoCostSignedEndpointVec *recvs,
                                         RhoCostSignedEndpointVec *sends) {
    RhoCostTermView term_view = rhocost_term_view(term);
    if (term_view.kind != RHOCOST_TERM_SIGNED || term_view.nargs != 2 ||
        !rhocost_check_signature(term_view.args[1])) {
        return true;
    }
    RhoView body_view = rho_view(term_view.args[0]);
    if ((body_view.kind != RHO_RECV || body_view.nargs != 3) &&
        (body_view.kind != RHO_SEND || body_view.nargs != 2)) {
        return true;
    }
    return rhocost_signed_endpoint_vec_push(
        body_view.kind == RHO_RECV ? recvs : sends, component_index,
        term_view.args[1], body_view);
}

static bool rhocost_whole_signed_redex(uint32_t component_index,
                                       Atom *term,
                                       RhoCostWholeRedexVec *out) {
    RhoCostTermView term_view = rhocost_term_view(term);
    if (term_view.kind != RHOCOST_TERM_SIGNED || term_view.nargs != 2 ||
        !rhocost_check_signature(term_view.args[1])) {
        return true;
    }
    RhoView body_view = rho_view(term_view.args[0]);
    if (body_view.kind != RHO_PAR) return true;
    if (body_view.nargs == 2) {
        RhoView a = rho_view(body_view.args[0]);
        RhoView b = rho_view(body_view.args[1]);
        RhoView recv = {0};
        RhoView send = {0};
        if (a.kind == RHO_RECV && a.nargs == 3 &&
            b.kind == RHO_SEND && b.nargs == 2) {
            recv = a;
            send = b;
        } else if (b.kind == RHO_RECV && b.nargs == 3 &&
                   a.kind == RHO_SEND && a.nargs == 2) {
            recv = b;
            send = a;
        }
        if (recv.kind == RHO_RECV &&
            rhocost_names_equal(recv.args[0], send.args[0])) {
            return rhocost_whole_redex_vec_push(out, component_index,
                                                term_view.args[1],
                                                recv, send);
        }
    }
    return true;
}

static void rhocost_collect_term_components(Arena *arena, Atom *term, RhoAtomVec *out) {
    rhocost_collect_term_par(arena, term, out);
}

static bool rhocost_runtime_components_sort(
    RhoAtomVec *components, RhoCostProducerVec *producers) {
    RhoCostOrderedRuntimeComponent *ordered;

    if (!components || (producers && producers->len != components->len)) {
        return false;
    }
    if (components->len < 2u) return true;
    ordered = cetta_malloc(
        sizeof(RhoCostOrderedRuntimeComponent) * (size_t)components->len);
    if (!ordered) return false;
    for (uint32_t i = 0; i < components->len; i++) {
        ordered[i].term = components->items[i];
        ordered[i].producer = producers
            ? producers->items[i] : RHOCOST_NO_PRODUCER;
        ordered[i].ordinal = i;
    }
    qsort(ordered, components->len,
          sizeof(RhoCostOrderedRuntimeComponent),
          rhocost_ordered_runtime_component_cmp);
    for (uint32_t i = 0; i < components->len; i++) {
        components->items[i] = ordered[i].term;
        if (producers) producers->items[i] = ordered[i].producer;
    }
    free(ordered);
    return true;
}

static bool rhocost_runtime_components_add_term(
    Arena *arena, RhoAtomVec *components, RhoCostProducerVec *producers,
    Atom *term, uint64_t producer) {
    uint32_t component_start;
    uint32_t producer_start = 0u;
    Atom *normalized;
    RhoCostTermView view;

    if (!arena || !components || !term ||
        (producers && producers->len != components->len)) {
        return false;
    }
    component_start = components->len;
    if (producers) producer_start = producers->len;
    normalized = rhocost_normalize_term(arena, term);
    view = rhocost_term_view(normalized);
    if (view.kind == RHOCOST_TERM_NIL) return true;
    if (view.kind == RHOCOST_TERM_PAR) {
        for (uint32_t i = 0; i < view.nargs; i++) {
            if (!rhocost_runtime_components_add_term(
                    arena, components, producers, view.args[i], producer)) {
                components->len = component_start;
                if (producers) producers->len = producer_start;
                return false;
            }
        }
        return true;
    }
    if (view.kind == RHOCOST_TERM_BAD ||
        !rho_vec_push(components, normalized)) {
        components->len = component_start;
        if (producers) producers->len = producer_start;
        return false;
    }
    if (producers && !rhocost_producer_vec_push(producers, producer)) {
        components->len = component_start;
        producers->len = producer_start;
        return false;
    }
    return true;
}

/* Components are kept in canonical order after every committed wave. */
static Atom *rhocost_runtime_components_term(
    Arena *arena, const RhoAtomVec *components) {
    if (!arena || !components) return NULL;
    if (components->len == 0u) return rhocost_nil(arena);
    if (components->len == 1u) return components->items[0];
    return rhocost_par(arena, components->items, components->len);
}

static void rhocost_trace_components_sort(RhoCostTraceComponentVec *components) {
    if (components->len < 2) return;
    RhoCostOrderedTraceComponent *ordered =
        cetta_malloc(sizeof(RhoCostOrderedTraceComponent) * components->len);
    for (uint32_t i = 0; i < components->len; i++) {
        ordered[i].component = components->items[i];
        ordered[i].ordinal = i;
    }
    qsort(ordered, components->len, sizeof(RhoCostOrderedTraceComponent),
          rhocost_ordered_trace_component_cmp);
    for (uint32_t i = 0; i < components->len; i++) {
        components->items[i] = ordered[i].component;
    }
    free(ordered);
}

static bool rhocost_trace_components_add_term(
    Arena *arena, RhoCostTraceComponentVec *components, Atom *term,
    bool has_producer, uint64_t producer) {
    Atom *normalized = rhocost_normalize_term(arena, term);
    RhoCostTermView view = rhocost_term_view(normalized);
    if (view.kind == RHOCOST_TERM_NIL) return true;
    if (view.kind == RHOCOST_TERM_PAR) {
        for (uint32_t i = 0; i < view.nargs; i++) {
            if (!rhocost_trace_components_add_term(
                    arena, components, view.args[i],
                    has_producer, producer)) {
                return false;
            }
        }
        return true;
    }
    if (view.kind == RHOCOST_TERM_BAD) return false;
    return rhocost_trace_component_vec_push(
        components, normalized, has_producer, producer);
}

static bool rhocost_trace_components_project(
    RhoCostTraceComponentVec *components, RhoAtomVec *terms) {
    rho_vec_init(terms);
    for (uint32_t i = 0; i < components->len; i++) {
        if (!rho_vec_push(terms, components->items[i].term)) {
            rho_vec_free(terms);
            return false;
        }
    }
    return true;
}

static Atom *rhocost_trace_components_term(
    Arena *arena, RhoCostTraceComponentVec *components) {
    RhoAtomVec terms;
    Atom *result;
    if (!rhocost_trace_components_project(components, &terms)) return NULL;
    result = rhocost_term_par_from_vec(arena, &terms);
    rho_vec_free(&terms);
    return result;
}

/* ── Causal provenance and event records ────────────────────────────────────
 *
 * Each state component optionally remembers which event produced it.  When
 * a firing consumes components, the producers of those exact occurrences
 * become the event's cause list — repetitions kept, so consuming two
 * outputs of event 0 yields causes (0 0).  Initial components have no
 * producer and contribute no arc: indistinguishable initial occurrences
 * acquire no invented identity.  An event records its fresh run-local id,
 * that cause list, one funding record per consumed purse head (location and
 * head signature), and the raw consumed signature. */

static bool rhocost_step_consumes_component(const RhoCostStep *step,
                                            uint32_t component_index) {
    for (uint32_t i = 0; i < step->participant_len; i++) {
        if (step->participant_indices[i] == component_index) return true;
    }
    for (uint32_t i = 0; i < step->token_len; i++) {
        if (step->token_indices[i] == component_index) return true;
    }
    return false;
}

static bool rhocost_trace_apply_step(
    Arena *arena, RhoCostTraceComponentVec *components,
    const RhoCostStep *step, uint64_t event_id) {
    RhoCostTraceComponentVec next;
    rhocost_trace_component_vec_init(&next);

    for (uint32_t i = 0; i < components->len; i++) {
        if (rhocost_step_consumes_component(step, i)) continue;
        if (!rhocost_trace_component_vec_push(
                &next, components->items[i].term,
                components->items[i].has_producer,
                components->items[i].producer)) {
            rhocost_trace_component_vec_free(&next);
            return false;
        }
    }

    if (!rhocost_trace_components_add_term(
            arena, &next, step->contractum, true, event_id)) {
        rhocost_trace_component_vec_free(&next);
        return false;
    }
    for (uint32_t i = 0; i < step->token_len; i++) {
        uint32_t token_index = step->token_indices[i];
        if (token_index >= components->len) {
            rhocost_trace_component_vec_free(&next);
            return false;
        }
        RhoCostTermView token =
            rhocost_term_view(components->items[token_index].term);
        if (token.kind != RHOCOST_TERM_PURSE || token.nargs != 2) {
            rhocost_trace_component_vec_free(&next);
            return false;
        }
        RhoCostTermView stack = rhocost_term_view(token.args[1]);
        if (stack.kind != RHOCOST_TERM_STACK_CONS || stack.nargs != 2) {
            rhocost_trace_component_vec_free(&next);
            return false;
        }
        Atom *tail = rhocost_purse(arena, token.args[0], stack.args[1]);
        if (!rhocost_trace_components_add_term(
                arena, &next, tail, true, event_id)) {
            rhocost_trace_component_vec_free(&next);
            return false;
        }
    }

    rhocost_trace_components_sort(&next);
    rhocost_trace_component_vec_free(components);
    *components = next;
    return true;
}

static Atom *rhocost_trace_event(
    Arena *arena, RhoCostTraceComponentVec *components,
    const RhoCostStep *step, uint64_t event_id) {
    uint32_t max_causes = step->participant_len + step->token_len;
    Atom **cause_atoms =
        arena_alloc(arena, sizeof(Atom *) * (size_t)max_causes);
    uint32_t cause_len = 0;

    if (event_id > (uint64_t)INT64_MAX ||
        (max_causes > 0 && !cause_atoms)) {
        return NULL;
    }

    for (uint32_t group = 0; group < 2; group++) {
        const uint32_t *indices = group == 0
            ? step->participant_indices : step->token_indices;
        uint32_t len = group == 0 ? step->participant_len : step->token_len;
        for (uint32_t i = 0; i < len; i++) {
            uint32_t component_index = indices[i];
            if (component_index >= components->len) return NULL;
            RhoCostTraceComponent *component =
                &components->items[component_index];
            if (!component->has_producer) continue;
            if (component->producer >= event_id ||
                component->producer > (uint64_t)INT64_MAX) {
                return NULL;
            }
            cause_atoms[cause_len++] =
                atom_int(arena, (int64_t)component->producer);
        }
    }

    if (step->token_len == 0) return NULL;
    Atom **fundings =
        arena_alloc(arena, sizeof(Atom *) * (size_t)step->token_len);
    if (!fundings) return NULL;
    for (uint32_t i = 0; i < step->token_len; i++) {
        uint32_t component_index = step->token_indices[i];
        if (component_index >= components->len) return NULL;
        RhoCostTermView purse =
            rhocost_term_view(components->items[component_index].term);
        if (purse.kind != RHOCOST_TERM_PURSE || purse.nargs != 2) return NULL;
        RhoCostTermView stack = rhocost_term_view(purse.args[1]);
        if (stack.kind != RHOCOST_TERM_STACK_CONS || stack.nargs != 2) {
            return NULL;
        }
        Atom *funding_args[2] = {purse.args[0], stack.args[0]};
        fundings[i] =
            rho_call(arena, "lts:rho:cost:funding", funding_args, 2);
    }
    Atom *event_args[4] = {
        atom_int(arena, (int64_t)event_id),
        atom_expr(arena, cause_atoms, cause_len),
        atom_expr(arena, fundings, step->token_len),
        step->consumed_sig
    };
    return rho_call(arena, "lts:rho:cost:event", event_args, 4);
}

static bool rhocost_index_skipped(uint32_t index, const uint32_t *skip,
                                  uint32_t skip_len) {
    for (uint32_t i = 0; i < skip_len; i++) {
        if (skip[i] == index) return true;
    }
    return false;
}

static Atom *rhocost_rebuild_result(Arena *arena,
                                    RhoAtomVec *components,
                                    const uint32_t *skip,
                                    uint32_t skip_len,
                                    Atom **extras,
                                    uint32_t extra_len) {
    RhoAtomVec out;
    rho_vec_init(&out);
    for (uint32_t i = 0; i < components->len; i++) {
        if (rhocost_index_skipped(i, skip, skip_len)) continue;
        rhocost_collect_term_par(arena, components->items[i], &out);
    }
    for (uint32_t i = 0; i < extra_len; i++) {
        rhocost_collect_term_par(arena, extras[i], &out);
    }
    Atom *term = rhocost_term_par_from_vec(arena, &out);
    rho_vec_free(&out);
    return term;
}

static bool rhocost_emit_step(Arena *arena,
                              RhoAtomVec *components,
                              const uint32_t *participant_skip,
                              uint32_t participant_skip_len,
                              Atom *body,
                              RhoCostTokenVec *tokens,
                              RhoIndexVec *chosen_tokens,
                              Atom *consumed_sig,
                              RhoCostStepSetAcc *out) {
    uint32_t skip_len = participant_skip_len + chosen_tokens->len;
    uint32_t extra_len = 1u + chosen_tokens->len;
    uint32_t *skip = arena_alloc(arena, sizeof(uint32_t) * skip_len);
    Atom **extras = arena_alloc(arena, sizeof(Atom *) * extra_len);
    Atom *next;

    if ((skip_len > 0 && !skip) || (extra_len > 0 && !extras)) return false;

    for (uint32_t i = 0; i < participant_skip_len; i++) {
        skip[i] = participant_skip[i];
    }
    for (uint32_t i = 0; i < chosen_tokens->len; i++) {
        skip[participant_skip_len + i] =
            tokens->items[chosen_tokens->items[i]].component_index;
    }

    extras[0] = body;
    for (uint32_t i = 0; i < chosen_tokens->len; i++) {
        RhoCostToken *token = &tokens->items[chosen_tokens->items[i]];
        extras[i + 1] = rhocost_purse(arena, token->location, token->rest);
    }

    next = rhocost_rebuild_result(arena, components, skip, skip_len,
                                  extras, extra_len);
    return rhocost_step_set_acc_push(out, next, consumed_sig, body,
                                     participant_skip, participant_skip_len,
                                     tokens, chosen_tokens);
}

static bool rhocost_emit_sequential_candidate(
    Arena *arena, RhoAtomVec *components,
    const uint32_t *participant_indices, uint32_t participant_len,
    Atom *contractum, RhoView recv, RhoView send,
    RhoCostTokenVec *tokens, RhoIndexVec *chosen_tokens,
    Atom *consumed_sig, void *context) {
    (void)recv;
    (void)send;
    return rhocost_emit_step(
        arena, components, participant_indices, participant_len,
        contractum, tokens, chosen_tokens, consumed_sig,
        (RhoCostStepSetAcc *)context);
}

static bool rhocost_emit_parallel_candidate(
    Arena *arena, RhoAtomVec *components,
    const uint32_t *participant_indices, uint32_t participant_len,
    Atom *contractum, RhoView recv, RhoView send,
    RhoCostTokenVec *tokens, RhoIndexVec *chosen_tokens,
    Atom *consumed_sig, void *context) {
    (void)arena;
    (void)components;
    if (contractum != NULL) return false;
    return rhocost_parallel_task_vec_push(
        (RhoCostParallelTaskVec *)context, participant_indices,
        participant_len, tokens, chosen_tokens, consumed_sig, recv, send);
}

/* ── Exact-cover funding search ─────────────────────────────────────────────
 *
 * Funding selection is a genuine exact-cover problem (a demand of ten unit
 * coins over twenty unit purses admits C(20,10) covers), explored with an
 * explicit depth-first take/skip stack over the available tokens.  The
 * search control makes budgets honest: an unbounded search enumerates every
 * cover; a bounded search spends one unit per frame, records exhaustion
 * distinctly from emptiness, and can stop after the first witness for
 * lazy first-cover execution.  Exhaustion is never reported as quiescence. */

static bool rhocost_cover_tokens_cursor(Arena *arena,
                                        RhoAtomVec *components,
                                        const uint32_t *participant_skip,
                                        uint32_t participant_skip_len,
                                        Atom *body,
                                        RhoView recv,
                                        RhoView send,
                                        RhoCostTokenVec *tokens,
                                        RhoIndexVec *available_tokens,
                                        RhoAtomVec *remaining_sig_atoms,
                                        uint32_t position,
                                        RhoIndexVec *chosen_tokens,
                                        Atom *consumed_sig,
                                        RhoCostCandidateEmitter emitter,
                                        void *emitter_context,
                                        RhoCostSearchControl *control) {
    RhoCostCoverFrameVec stack;
    RhoCostCoverFrame initial;
    bool ok = true;

    rhocost_cover_frame_vec_init(&stack);
    if (!rhocost_cover_frame_clone(&initial, position, remaining_sig_atoms,
                                   chosen_tokens) ||
        !rhocost_cover_frame_vec_push_take(&stack, &initial)) {
        rhocost_cover_frame_free(&initial);
        rhocost_cover_frame_vec_free(&stack);
        return false;
    }

    while (stack.len > 0 && !rhocost_search_halted(control)) {
        RhoCostCoverFrame frame = stack.items[--stack.len];
        RhoAtomVec token_atoms;
        RhoCostCoverFrame take;
        bool selectable;
        uint32_t token_index;

        if (!rhocost_search_consume(control)) {
            rhocost_cover_frame_free(&frame);
            break;
        }
        if (frame.remaining_sig_atoms.len == 0) {
            ok = emitter(
                arena, components, participant_skip, participant_skip_len,
                body, recv, send, tokens, &frame.chosen_tokens, consumed_sig,
                emitter_context);
            rhocost_cover_frame_free(&frame);
            if (!ok) break;
            if (control && control->stop_after_first) control->stopped = true;
            continue;
        }
        if (frame.position >= available_tokens->len) {
            rhocost_cover_frame_free(&frame);
            continue;
        }

        token_index = available_tokens->items[frame.position];
        rho_vec_init(&token_atoms);
        if (!rhocost_collect_signature_atoms(tokens->items[token_index].sig,
                                             &token_atoms)) {
            rho_vec_free(&token_atoms);
            rhocost_cover_frame_free(&frame);
            ok = false;
            break;
        }
        rhocost_normalize_signature_vec(&token_atoms);
        selectable = rhocost_signature_vec_is_subset(
            &token_atoms, &frame.remaining_sig_atoms);

        rhocost_cover_frame_init(&take);
        if (selectable) {
            take.position = frame.position + 1;
            if (!rhocost_signature_vec_subtract(
                    &frame.remaining_sig_atoms, &token_atoms,
                    &take.remaining_sig_atoms) ||
                !rhocost_index_vec_clone(&take.chosen_tokens,
                                         &frame.chosen_tokens) ||
                !rhocost_index_vec_push(&take.chosen_tokens, token_index)) {
                rho_vec_free(&token_atoms);
                rhocost_cover_frame_free(&take);
                rhocost_cover_frame_free(&frame);
                ok = false;
                break;
            }
        }
        rho_vec_free(&token_atoms);

        /* LIFO order preserves the recursive take-before-skip enumeration. */
        frame.position++;
        if (!rhocost_cover_frame_vec_push_take(&stack, &frame) ||
            (selectable &&
             !rhocost_cover_frame_vec_push_take(&stack, &take))) {
            rhocost_cover_frame_free(&take);
            rhocost_cover_frame_free(&frame);
            ok = false;
            break;
        }
        rhocost_cover_frame_free(&take);
        rhocost_cover_frame_free(&frame);
    }

    rhocost_cover_frame_vec_free(&stack);
    return ok;
}

static bool rhocost_collect_available_tokens(RhoCostTokenVec *tokens,
                                             Atom *channel,
                                             RhoIndexVec *available) {
    rhocost_index_vec_init(available);
    for (uint32_t i = 0; i < tokens->len; i++) {
        if (rhocost_names_equal(tokens->items[i].location, channel) &&
            !rhocost_index_vec_push(available, i)) {
            rhocost_index_vec_free(available);
            return false;
        }
    }
    return true;
}

static Atom *rhocost_signature_product(Arena *arena, Atom *lhs, Atom *rhs) {
    RhoAtomVec atoms;
    Atom *out;
    rho_vec_init(&atoms);
    if (!rhocost_collect_signature_atoms(lhs, &atoms) ||
        !rhocost_collect_signature_atoms(rhs, &atoms)) {
        rho_vec_free(&atoms);
        return NULL;
    }
    out = rhocost_signature_from_vec(arena, &atoms);
    rho_vec_free(&atoms);
    return out;
}

static bool rhocost_collect_candidates_from_components(
    Arena *arena, RhoAtomVec *components,
    bool compute_contracta, RhoCostCandidateEmitter emitter,
    void *emitter_context, RhoCostSearchControl *control) {
    RhoCostTokenVec tokens;
    RhoCostSignedEndpointVec recvs;
    RhoCostSignedEndpointVec sends;
    RhoCostWholeRedexVec wholes;
    bool ok = true;

    if (!arena || !components || !emitter) return false;

    rhocost_token_vec_init(&tokens);
    rhocost_signed_endpoint_vec_init(&recvs);
    rhocost_signed_endpoint_vec_init(&sends);
    rhocost_whole_redex_vec_init(&wholes);

    for (uint32_t i = 0; i < components->len && ok; i++) {
        RhoCostTermView view = rhocost_term_view(components->items[i]);
        if (view.kind == RHOCOST_TERM_PURSE && view.nargs == 2) {
            RhoCostTermView stack = rhocost_term_view(view.args[1]);
            if (stack.kind == RHOCOST_TERM_STACK_CONS && stack.nargs == 2 &&
                rhocost_check_signature(stack.args[0])) {
                ok = rhocost_token_vec_push(
                    &tokens, i, view.args[0], stack.args[0], stack.args[1]);
                continue;
            }
        }
        ok = rhocost_signed_body_endpoint(
                 i, components->items[i], &recvs, &sends) &&
             rhocost_whole_signed_redex(i, components->items[i], &wholes);
    }

    for (uint32_t w = 0;
         w < wholes.len && ok && !rhocost_search_halted(control); w++) {
        RhoAtomVec required_sig_atoms;
        RhoIndexVec available_tokens;
        RhoIndexVec chosen_tokens;
        uint32_t skip[1] = {wholes.items[w].component_index};
        Atom *consumed_sig = rhocost_normalize_signature(arena, wholes.items[w].sig);
        Atom *body = compute_contracta
            ? rhocost_subst_term(arena, wholes.items[w].recv.args[2],
                                 wholes.items[w].recv.args[1]->var_id,
                                 wholes.items[w].send.args[1])
            : NULL;
        if (!rhocost_search_consume(control)) break;
        rho_vec_init(&required_sig_atoms);
        rhocost_index_vec_init(&chosen_tokens);
        ok = rhocost_collect_available_tokens(
                 &tokens, wholes.items[w].recv.args[0], &available_tokens) &&
             rhocost_collect_signature_atoms(consumed_sig, &required_sig_atoms);
        if (ok) {
            rhocost_normalize_signature_vec(&required_sig_atoms);
            ok = rhocost_cover_tokens_cursor(
                arena, components, skip, 1, body,
                wholes.items[w].recv, wholes.items[w].send,
                &tokens, &available_tokens, &required_sig_atoms, 0,
                &chosen_tokens, consumed_sig, emitter, emitter_context,
                control);
        }
        rhocost_index_vec_free(&available_tokens);
        rhocost_index_vec_free(&chosen_tokens);
        rho_vec_free(&required_sig_atoms);
    }

    for (uint32_t r = 0;
         r < recvs.len && ok && !rhocost_search_halted(control); r++) {
        for (uint32_t s = 0;
             s < sends.len && ok && !rhocost_search_halted(control); s++) {
            RhoAtomVec required_sig_atoms;
            RhoIndexVec available_tokens;
            RhoIndexVec chosen_tokens;
            uint32_t skip[2];
            Atom *consumed_sig;
            Atom *body;
            if (!rhocost_search_consume(control)) break;
            if (!rhocost_names_equal(recvs.items[r].view.args[0],
                                     sends.items[s].view.args[0])) {
                continue;
            }
            consumed_sig = rhocost_signature_product(arena, recvs.items[r].sig,
                                                     sends.items[s].sig);
            if (!consumed_sig) {
                ok = false;
                continue;
            }
            body = compute_contracta
                ? rhocost_subst_term(arena, recvs.items[r].view.args[2],
                                     recvs.items[r].view.args[1]->var_id,
                                     sends.items[s].view.args[1])
                : NULL;
            skip[0] = recvs.items[r].component_index;
            skip[1] = sends.items[s].component_index;
            rho_vec_init(&required_sig_atoms);
            rhocost_index_vec_init(&chosen_tokens);
            ok = rhocost_collect_available_tokens(
                     &tokens, recvs.items[r].view.args[0], &available_tokens) &&
                 rhocost_collect_signature_atoms(consumed_sig,
                                                &required_sig_atoms);
            if (ok) {
                rhocost_normalize_signature_vec(&required_sig_atoms);
                ok = rhocost_cover_tokens_cursor(
                    arena, components, skip, 2, body,
                    recvs.items[r].view, sends.items[s].view,
                    &tokens, &available_tokens, &required_sig_atoms, 0,
                    &chosen_tokens, consumed_sig, emitter, emitter_context,
                    control);
            }
            rhocost_index_vec_free(&available_tokens);
            rhocost_index_vec_free(&chosen_tokens);
            rho_vec_free(&required_sig_atoms);
        }
    }

    rhocost_whole_redex_vec_free(&wholes);
    rhocost_signed_endpoint_vec_free(&recvs);
    rhocost_signed_endpoint_vec_free(&sends);
    rhocost_token_vec_free(&tokens);
    return ok;
}

static bool rhocost_collect_steps_from_components(
    Arena *arena, RhoAtomVec *components, RhoCostStepSetAcc *out,
    RhoCostSearchControl *control) {
    return rhocost_collect_candidates_from_components(
        arena, components, true, rhocost_emit_sequential_candidate, out,
        control);
}

static bool rhocost_collect_parallel_tasks_from_components(
    Arena *arena, RhoAtomVec *components, RhoCostParallelTaskVec *tasks,
    RhoCostSearchControl *control) {
    return rhocost_collect_candidates_from_components(
        arena, components, false, rhocost_emit_parallel_candidate, tasks,
        control);
}

static bool rhocost_parallel_wave_selected_resources_valid(
    const RhoCostParallelWave *wave,
    const RhoCostParallelTaskVec *tasks) {
    if (!wave || !tasks) return false;
    for (uint32_t i = 0; i < tasks->len; i++) {
        const RhoCostParallelTask *task = &tasks->items[i];
        if (!task->selected) continue;
        for (uint32_t j = 0; j < task->plan.resource_len; j++) {
            uint32_t index = task->plan.resource_indices[j];
            if (index >= wave->component_count ||
                atomic_load_explicit(&wave->claimed[index],
                                     memory_order_acquire) == 0u) {
                return false;
            }
        }
    }
    for (uint32_t index = 0; index < wave->component_count; index++) {
        bool accounted = false;
        if (atomic_load_explicit(&wave->claimed[index],
                                 memory_order_acquire) == 0u) {
            continue;
        }
        for (uint32_t i = 0; i < tasks->len && !accounted; i++) {
            const RhoCostParallelTask *task = &tasks->items[i];
            if (!task->selected) continue;
            for (uint32_t j = 0; j < task->plan.resource_len; j++) {
                if (task->plan.resource_indices[j] == index) {
                    accounted = true;
                    break;
                }
            }
        }
        if (!accounted) return false;
    }
    return true;
}

static bool rhocost_parallel_wave_start(
    const RhoRuntimeProfile *profile, uint32_t component_count,
    uint32_t budget, RhoCostParallelTaskVec *tasks,
    RhoCostParallelWave *wave, CettaParallelExecutor *executor,
    bool *out_executor_initialized, uint32_t *out_selected) {
    CettaParallelExecutorConfig config;
    uint32_t selected = 0;
    uint32_t start;
    bool anchor_claimed = false;

    if (!profile || !tasks || !wave || !executor ||
        !out_executor_initialized || !out_selected ||
        profile->thread_count == 0u || component_count == 0u ||
        budget == 0u || tasks->len == 0u) {
        return false;
    }
    memset(wave, 0, sizeof(*wave));
    memset(executor, 0, sizeof(*executor));
    *out_executor_initialized = false;
    wave->claimed = cetta_malloc(
        sizeof(_Atomic unsigned char) * (size_t)component_count);
    wave->component_count = component_count;
    atomic_init(&wave->remaining_budget, budget);
    for (uint32_t i = 0; i < component_count; i++) {
        atomic_init(&wave->claimed[i], 0u);
    }
    start = (uint32_t)(atomic_fetch_add_explicit(
        &g_rhocalc_cost_wave_turn, 1u, memory_order_relaxed) % tasks->len);
    if (!rhocost_parallel_try_claim(
            wave, &tasks->items[start], &anchor_claimed) ||
        !anchor_claimed || !rhocost_parallel_try_take_budget(wave)) {
        free(wave->claimed);
        wave->claimed = NULL;
        rho_validation_set("cost-rho parallel wave could not reserve its fair anchor");
        return false;
    }
    tasks->items[start].preclaimed = true;

    config = (CettaParallelExecutorConfig){
        .thread_count = profile->thread_count,
        .prefer_persistent_workers = true,
        .user = wave,
        .task_fn = rhocost_parallel_process_task,
        .worker_enter = rho_async_worker_enter,
        .worker_leave = rho_async_worker_leave,
        .worker_failure_message = "cost-rho parallel wave worker failed",
    };
    if (!cetta_parallel_executor_init(executor, &config)) {
        free(wave->claimed);
        wave->claimed = NULL;
        return false;
    }
    *out_executor_initialized = true;
    for (uint32_t offset = 0; offset < tasks->len; offset++) {
        uint32_t i = (start + offset) % tasks->len;
        if (!cetta_parallel_executor_push(executor, &tasks->items[i])) {
            rho_validation_set("cost-rho parallel wave could not queue a firing");
            return false;
        }
    }
    if (!cetta_parallel_executor_run(executor)) {
        rho_validation_set("%s", cetta_parallel_executor_error(executor)
            ? cetta_parallel_executor_error(executor)
            : "cost-rho parallel wave execution failed");
        return false;
    }
    for (uint32_t i = 0; i < tasks->len; i++) {
        if (tasks->items[i].selected) selected++;
    }
    if (selected == 0u || selected > budget ||
        selected != budget - atomic_load_explicit(
            &wave->remaining_budget, memory_order_acquire) ||
        !rhocost_parallel_wave_selected_resources_valid(wave, tasks)) {
        rho_validation_set("cost-rho parallel wave violated occurrence accounting");
        return false;
    }
    *out_selected = selected;
    return true;
}

static void rhocost_parallel_wave_finish(RhoCostParallelWave *wave,
                                         CettaParallelExecutor *executor,
                                         bool executor_initialized) {
    if (executor_initialized) cetta_parallel_executor_free(executor);
    free(wave->claimed);
    wave->claimed = NULL;
    wave->component_count = 0u;
}

static bool rhocost_parallel_plan_consumes_component(
    const RhoCostParallelPlan *plan, uint32_t component_index) {
    if (!plan) return false;
    for (uint32_t i = 0; i < plan->participant_len; i++) {
        if (plan->participant_indices[i] == component_index) return true;
    }
    for (uint32_t i = 0; i < plan->token_len; i++) {
        if (plan->token_indices[i] == component_index) return true;
    }
    return false;
}

static bool rhocost_parallel_task_resource_shape_valid(
    const RhoAtomVec *components, const RhoCostParallelTask *task) {
    if (!components || !task || !task->selected ||
        (task->plan.participant_len != 1u &&
         task->plan.participant_len != 2u) ||
        task->plan.token_len == 0u ||
        task->plan.resource_len !=
            task->plan.participant_len + task->plan.token_len ||
        !task->plan.resource_indices || !task->plan.token_indices) {
        return false;
    }
    for (uint32_t group = 0; group < 2u; group++) {
        const uint32_t *indices = group == 0u
            ? task->plan.participant_indices : task->plan.token_indices;
        uint32_t len = group == 0u
            ? task->plan.participant_len : task->plan.token_len;
        for (uint32_t i = 0; i < len; i++) {
            uint32_t occurrences = 0u;
            if (indices[i] >= components->len) return false;
            for (uint32_t j = 0; j < task->plan.resource_len; j++) {
                if (task->plan.resource_indices[j] == indices[i]) {
                    occurrences++;
                }
            }
            if (occurrences != 1u) return false;
        }
    }
    for (uint32_t i = 0; i < task->plan.resource_len; i++) {
        uint32_t index = task->plan.resource_indices[i];
        if (index >= components->len ||
            !rhocost_parallel_plan_consumes_component(&task->plan, index) ||
            (i > 0u && task->plan.resource_indices[i - 1u] >= index)) {
            return false;
        }
    }
    return true;
}

#if CETTA_RHOCOST_COMMIT_AUDIT
static bool rhocost_parallel_task_redex_valid(
    Arena *arena, const RhoAtomVec *components,
    const RhoCostParallelTask *task, Atom **out_demand) {
    RhoView recv = {0};
    RhoView send = {0};
    Atom *demand = NULL;
    Atom *expected_contractum = NULL;

    if (!arena || !components || !task || !out_demand ||
        !task->plan.channel || !task->plan.recv_body ||
        !task->plan.send_payload || !task->plan.consumed_sig ||
        !task->contractum) {
        return false;
    }
    *out_demand = NULL;
    if (task->plan.participant_len == 1u) {
        uint32_t index = task->plan.participant_indices[0];
        RhoCostTermView signed_term;
        RhoView body;
        if (index >= components->len) return false;
        signed_term = rhocost_term_view(components->items[index]);
        if (signed_term.kind != RHOCOST_TERM_SIGNED ||
            signed_term.nargs != 2u ||
            !rhocost_check_signature(signed_term.args[1])) {
            return false;
        }
        body = rho_view(signed_term.args[0]);
        if (body.kind != RHO_PAR || body.nargs != 2u) return false;
        if (rho_view(body.args[0]).kind == RHO_RECV &&
            rho_view(body.args[1]).kind == RHO_SEND) {
            recv = rho_view(body.args[0]);
            send = rho_view(body.args[1]);
        } else if (rho_view(body.args[1]).kind == RHO_RECV &&
                   rho_view(body.args[0]).kind == RHO_SEND) {
            recv = rho_view(body.args[1]);
            send = rho_view(body.args[0]);
        } else {
            return false;
        }
        demand = rhocost_normalize_signature(arena, signed_term.args[1]);
    } else if (task->plan.participant_len == 2u) {
        Atom *recv_sig = NULL;
        Atom *send_sig = NULL;
        for (uint32_t i = 0; i < 2u; i++) {
            uint32_t index = task->plan.participant_indices[i];
            RhoCostTermView signed_term;
            RhoView body;
            if (index >= components->len) return false;
            signed_term = rhocost_term_view(components->items[index]);
            if (signed_term.kind != RHOCOST_TERM_SIGNED ||
                signed_term.nargs != 2u ||
                !rhocost_check_signature(signed_term.args[1])) {
                return false;
            }
            body = rho_view(signed_term.args[0]);
            if (body.kind == RHO_RECV && recv.kind == RHO_BAD) {
                recv = body;
                recv_sig = signed_term.args[1];
            } else if (body.kind == RHO_SEND && send.kind == RHO_BAD) {
                send = body;
                send_sig = signed_term.args[1];
            } else {
                return false;
            }
        }
        if (!recv_sig || !send_sig) return false;
        demand = rhocost_signature_product(arena, recv_sig, send_sig);
    } else {
        return false;
    }

    if (!demand || recv.kind != RHO_RECV || recv.nargs != 3u ||
        !recv.args[1] || recv.args[1]->kind != ATOM_VAR ||
        send.kind != RHO_SEND || send.nargs != 2u ||
        recv.args[1]->var_id != task->plan.recv_binder ||
        !atom_eq(recv.args[2], task->plan.recv_body) ||
        !atom_eq(send.args[1], task->plan.send_payload) ||
        !atom_eq(rhocost_normalize_signature(arena, task->plan.consumed_sig),
                 demand) ||
        !rhocost_check_term_rec(task->contractum)) {
        return false;
    }

    expected_contractum = rhocost_subst_term(
        arena, recv.args[2], recv.args[1]->var_id, send.args[1]);
    if (!expected_contractum ||
        !atom_eq(rhocost_normalize_term(arena, expected_contractum),
                 rhocost_normalize_term(arena, task->contractum))) {
        return false;
    }

    if (!rhocost_names_equal(task->plan.channel, recv.args[0]) ||
        !rhocost_names_equal(task->plan.channel, send.args[0])) {
        return false;
    }
    *out_demand = demand;
    return true;
}

static bool rhocost_parallel_task_funding_valid(
    Arena *arena, const RhoAtomVec *components,
    const RhoCostParallelTask *task) {
    RhoAtomVec funding_atoms;
    Atom *demand = NULL;
    Atom *funding = NULL;
    bool valid = false;

    if (!rhocost_parallel_task_resource_shape_valid(components, task) ||
        !rhocost_parallel_task_redex_valid(
            arena, components, task, &demand)) {
        return false;
    }
    rho_vec_init(&funding_atoms);
    for (uint32_t i = 0; i < task->plan.token_len; i++) {
        uint32_t index = task->plan.token_indices[i];
        RhoCostTermView purse = rhocost_term_view(components->items[index]);
        RhoCostTermView stack;
        if (purse.kind != RHOCOST_TERM_PURSE || purse.nargs != 2u) {
            goto done;
        }
        stack = rhocost_term_view(purse.args[1]);
        if (stack.kind != RHOCOST_TERM_STACK_CONS || stack.nargs != 2u) {
            goto done;
        }
        if (!rhocost_names_equal(task->plan.channel, purse.args[0])) {
            goto done;
        }
        if (!rhocost_collect_signature_atoms(stack.args[0], &funding_atoms)) {
            goto done;
        }
    }
    funding = rhocost_signature_from_vec(arena, &funding_atoms);
    valid = funding && demand && atom_eq(funding, demand);

done:
    rho_vec_free(&funding_atoms);
    return valid;
}
#endif

static bool rhocost_parallel_wave_commit_valid(
    Arena *arena, const RhoAtomVec *components,
    const RhoCostParallelTaskVec *tasks, uint32_t selected) {
    uint32_t checked = 0u;
    if (!arena || !components || !tasks || selected == 0u) return false;
    for (uint32_t i = 0; i < tasks->len; i++) {
        const RhoCostParallelTask *task = &tasks->items[i];
        if (!task->selected) continue;
        if (!rhocost_parallel_task_resource_shape_valid(components, task) ||
            !task->contractum || !rhocost_check_term_rec(task->contractum)) {
            return false;
        }
#if CETTA_RHOCOST_COMMIT_AUDIT
        if (!rhocost_parallel_task_funding_valid(arena, components, task)) {
            return false;
        }
#endif
        checked++;
    }
    return checked == selected;
}

static Atom *rhocost_parallel_trace_event(
    Arena *arena, const RhoAtomVec *components,
    const RhoCostCausalObserver *observer,
    const RhoCostParallelPlan *plan, uint64_t event_id) {
    uint32_t max_causes;
    Atom **cause_atoms;
    uint32_t cause_len = 0u;

    if (!arena || !components || !observer || !observer->events || !plan ||
        observer->producers.len != components->len ||
        event_id > (uint64_t)INT64_MAX) {
        return NULL;
    }
    max_causes = plan->participant_len + plan->token_len;
    cause_atoms = arena_alloc(
        arena, sizeof(Atom *) * (size_t)max_causes);
    if (max_causes > 0u && !cause_atoms) return NULL;
    for (uint32_t group = 0; group < 2u; group++) {
        const uint32_t *indices = group == 0u
            ? plan->participant_indices : plan->token_indices;
        uint32_t len = group == 0u
            ? plan->participant_len : plan->token_len;
        for (uint32_t i = 0; i < len; i++) {
            uint32_t index = indices[i];
            uint64_t producer;
            if (index >= components->len) return NULL;
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_COST_RHO_RECEIPT_CAUSE_SOURCE_SCANNED);
            producer = observer->producers.items[index];
            if (producer == RHOCOST_NO_PRODUCER) continue;
            if (producer >= event_id || producer > (uint64_t)INT64_MAX) {
                return NULL;
            }
            cause_atoms[cause_len++] = atom_int(arena, (int64_t)producer);
        }
    }
    if (plan->token_len == 0u) return NULL;
    {
        Atom **fundings = arena_alloc(
            arena, sizeof(Atom *) * (size_t)plan->token_len);
        Atom *event_args[4];
        if (!fundings) return NULL;
        for (uint32_t i = 0; i < plan->token_len; i++) {
            uint32_t index = plan->token_indices[i];
            RhoCostTermView purse;
            RhoCostTermView stack;
            Atom *funding_args[2];
            if (index >= components->len) return NULL;
            purse = rhocost_term_view(components->items[index]);
            if (purse.kind != RHOCOST_TERM_PURSE || purse.nargs != 2u) {
                return NULL;
            }
            stack = rhocost_term_view(purse.args[1]);
            if (stack.kind != RHOCOST_TERM_STACK_CONS || stack.nargs != 2u) {
                return NULL;
            }
            funding_args[0] = purse.args[0];
            funding_args[1] = stack.args[0];
            fundings[i] = rho_call(
                arena, "lts:rho:cost:funding", funding_args, 2u);
        }
        event_args[0] = atom_int(arena, (int64_t)event_id);
        event_args[1] = atom_expr(arena, cause_atoms, cause_len);
        event_args[2] = atom_expr(arena, fundings, plan->token_len);
        event_args[3] = plan->consumed_sig;
        return rho_call(arena, "lts:rho:cost:event", event_args, 4u);
    }
}

/*
 * Commit one checked occurrence-disjoint wave.  The operational transition is
 * shared by state-only and causal-observation runs; the latter supplies the
 * optional producer/event vectors.  Receipt construction therefore cannot
 * affect which resources are consumed or which residual is produced.
 */
static bool rhocost_parallel_apply_wave(
    Arena *arena, RhoAtomVec *components,
    RhoCostCausalObserver *observer,
    RhoCostParallelTaskVec *tasks, RhoCostParallelWave *wave,
    uint32_t selected, uint64_t first_event_id, uint32_t *out_applied) {
    bool observe_causality = observer != NULL;
    RhoAtomVec next;
    RhoCostProducerVec next_producers;
    uint32_t applied = 0u;

    if (!arena || !components || !tasks || !wave || !out_applied ||
        components->len != wave->component_count ||
        (observe_causality &&
         (!observer->events ||
          observer->producers.len != components->len ||
          first_event_id != (uint64_t)observer->events->len)) ||
        !rhocost_parallel_wave_commit_valid(
            arena, components, tasks, selected)) {
        return false;
    }

    if (observe_causality) {
        for (uint32_t i = 0; i < tasks->len; i++) {
            RhoCostParallelTask *task = &tasks->items[i];
            Atom *event;
            if (!task->selected) continue;
            if (first_event_id + applied > (uint64_t)INT64_MAX) return false;
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_COST_RHO_RECEIPT_EVENT_ALLOCATION);
            event = rhocost_parallel_trace_event(
                arena, components, observer, &task->plan,
                first_event_id + applied);
            if (!event) return false;
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_COST_RHO_RECEIPT_EVENT_MATERIALIZED);
            if (!rho_vec_push(observer->events, event)) return false;
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_COST_RHO_RECEIPT_EVENT_RETAINED);
            applied++;
        }
        if (applied != selected) return false;
    }

    rho_vec_init(&next);
    rhocost_producer_vec_init(&next_producers);
    for (uint32_t i = 0; i < components->len; i++) {
        if (atomic_load_explicit(
                &wave->claimed[i], memory_order_acquire) != 0u) {
            continue;
        }
        if (!rho_vec_push(&next, components->items[i]) ||
            (observe_causality && !rhocost_producer_vec_push(
                &next_producers, observer->producers.items[i]))) {
            rho_vec_free(&next);
            rhocost_producer_vec_free(&next_producers);
            return false;
        }
    }

    applied = 0u;
    for (uint32_t i = 0; i < tasks->len; i++) {
        RhoCostParallelTask *task = &tasks->items[i];
        uint64_t event_id;
        Atom *contractum;
        if (!task->selected) continue;
        event_id = first_event_id + applied;
        contractum = atom_deep_copy(arena, task->contractum);
        if (!contractum || !rhocost_runtime_components_add_term(
                arena, &next,
                observe_causality ? &next_producers : NULL,
                contractum,
                observe_causality ? event_id : RHOCOST_NO_PRODUCER)) {
            rho_vec_free(&next);
            rhocost_producer_vec_free(&next_producers);
            return false;
        }
        for (uint32_t j = 0; j < task->plan.token_len; j++) {
            uint32_t token_index = task->plan.token_indices[j];
            RhoCostTermView purse;
            RhoCostTermView stack;
            Atom *tail;
            if (token_index >= components->len) {
                rho_vec_free(&next);
                rhocost_producer_vec_free(&next_producers);
                return false;
            }
            purse = rhocost_term_view(components->items[token_index]);
            if (purse.kind != RHOCOST_TERM_PURSE || purse.nargs != 2u) {
                rho_vec_free(&next);
                rhocost_producer_vec_free(&next_producers);
                return false;
            }
            stack = rhocost_term_view(purse.args[1]);
            if (stack.kind != RHOCOST_TERM_STACK_CONS || stack.nargs != 2u) {
                rho_vec_free(&next);
                rhocost_producer_vec_free(&next_producers);
                return false;
            }
            tail = rhocost_purse(arena, purse.args[0], stack.args[1]);
            if (!tail || !rhocost_runtime_components_add_term(
                    arena, &next,
                    observe_causality ? &next_producers : NULL,
                    tail,
                    observe_causality ? event_id : RHOCOST_NO_PRODUCER)) {
                rho_vec_free(&next);
                rhocost_producer_vec_free(&next_producers);
                return false;
            }
        }
        applied++;
    }
    if (applied != selected ||
        !rhocost_runtime_components_sort(
            &next, observe_causality ? &next_producers : NULL)) {
        rho_vec_free(&next);
        rhocost_producer_vec_free(&next_producers);
        return false;
    }

    for (uint32_t i = 0; i < tasks->len; i++) {
        const RhoCostParallelTask *task = &tasks->items[i];
        if (!task->selected) continue;
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_COST_RHO_PARALLEL_COMMITTED_CLAIM,
            task->plan.resource_len);
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_COST_RHO_FUNDING_HEAD_CONSUMED,
            task->plan.token_len);
    }

    rho_vec_free(components);
    *components = next;
    if (observe_causality) {
        rhocost_producer_vec_free(&observer->producers);
        observer->producers = next_producers;
    } else {
        rhocost_producer_vec_free(&next_producers);
    }
    *out_applied = applied;
    return true;
}

static bool rhocost_nonnegative_int(Atom *atom, uint64_t *out) {
    if (!atom || !out || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_INT || atom->ground.ival < 0) {
        return false;
    }
    *out = (uint64_t)atom->ground.ival;
    return true;
}

static bool rhocost_validate_causal_events(Arena *arena,
                                           const RhoAtomVec *events) {
    if (!arena || !events) return false;
    for (uint32_t i = 0; i < events->len; i++) {
        Atom *event = events->items[i];
        Atom *causes;
        Atom *fundings;
        Atom *raw_spend;
        RhoAtomVec spend_atoms;
        Atom *funding_spend;
        Atom *normalized_raw;
        uint64_t id;

        if (!event || event->kind != ATOM_EXPR || event->expr.len != 5u ||
            !rho_symbol_named(event->expr.elems[0], "lts:rho:cost:event") ||
            !rhocost_nonnegative_int(event->expr.elems[1], &id) ||
            id != i) {
            return false;
        }
        causes = event->expr.elems[2];
        fundings = event->expr.elems[3];
        raw_spend = event->expr.elems[4];
        if (causes->kind != ATOM_EXPR || fundings->kind != ATOM_EXPR ||
            fundings->expr.len == 0u ||
            !rhocost_check_signature(raw_spend)) {
            return false;
        }
        for (uint32_t j = 0; j < causes->expr.len; j++) {
            uint64_t cause;
            if (!rhocost_nonnegative_int(causes->expr.elems[j], &cause) ||
                cause >= id) {
                return false;
            }
        }
        rho_vec_init(&spend_atoms);
        for (uint32_t j = 0; j < fundings->expr.len; j++) {
            Atom *funding = fundings->expr.elems[j];
            if (!funding || funding->kind != ATOM_EXPR ||
                funding->expr.len != 3u ||
                !rho_symbol_named(funding->expr.elems[0],
                                  "lts:rho:cost:funding") ||
                !rhocost_collect_signature_atoms(funding->expr.elems[2],
                                                  &spend_atoms)) {
                rho_vec_free(&spend_atoms);
                return false;
            }
        }
        funding_spend = rhocost_signature_from_vec(arena, &spend_atoms);
        normalized_raw = rhocost_normalize_signature(arena, raw_spend);
        rho_vec_free(&spend_atoms);
        if (!funding_spend || !normalized_raw ||
            !atom_eq(funding_spend, normalized_raw)) {
            return false;
        }
    }
    return true;
}

typedef enum {
    RHOCOST_PARALLEL_RUN_QUIESCENT = 0,
    RHOCOST_PARALLEL_RUN_FUEL_EXHAUSTED,
    RHOCOST_PARALLEL_RUN_SEARCH_EXHAUSTED
} RhoCostParallelRunStatus;

/* ── Parallel run driver ────────────────────────────────────────────────────
 *
 * Wave loop: enumerate funded candidates, start a wave under the remaining
 * fuel, apply the checked commit, repeat until the frontier is empty or an
 * allowance ends.  With a causal observer the emitted event order is a
 * checked linearization of the wave's causal order, validated at the end of
 * the run; without one the same transitions run state-only. */

static bool rhocost_parallel_run(
    Arena *arena, Atom *term, uint32_t thread_count,
    bool bounded_fuel, uint64_t fuel, bool fuel_precedes_frontier,
    bool bounded_search, uint64_t search_budget,
    RhoAtomVec *out_events, Atom **out_residual,
    uint64_t *out_reductions, RhoCostParallelRunStatus *out_status) {
    bool observe_causality = out_events != NULL;
    RhoAtomVec components;
    RhoCostCausalObserver causal_observer;
    RhoCostCausalObserver *observer = NULL;
    RhoCostSearchControl search = {
        .bounded = bounded_search,
        .remaining = search_budget,
        .exhausted = false,
        .stop_after_first = false,
        .stopped = false,
    };
    uint64_t reductions = 0u;
    bool ok = true;

    if (!out_residual || !out_reductions || !out_status) return false;
    if (out_events) rho_vec_init(out_events);
    *out_residual = NULL;
    *out_reductions = 0u;
    *out_status = RHOCOST_PARALLEL_RUN_QUIESCENT;
    if (!arena || !term || thread_count < 2u ||
        !rhocost_term_well_formed(term)) {
        return false;
    }

    cetta_runtime_stats_inc(
        observe_causality
            ? CETTA_RUNTIME_COUNTER_COST_RHO_PARALLEL_RECEIPT_RUN
            : CETTA_RUNTIME_COUNTER_COST_RHO_PARALLEL_STATE_ONLY_RUN);

    rho_vec_init(&components);
    if (observe_causality) {
        rhocost_causal_observer_init(&causal_observer, out_events);
        observer = &causal_observer;
    }
    rho_symbols_ensure();
    if (!rhocost_runtime_components_add_term(
            arena, &components,
            observer ? &observer->producers : NULL,
            term, RHOCOST_NO_PRODUCER) ||
        !rhocost_runtime_components_sort(
            &components, observer ? &observer->producers : NULL)) {
        rho_validation_set("cost-rho parallel run could not initialize its state");
        ok = false;
        goto done;
    }

    for (;;) {
        RhoCostParallelTaskVec tasks;
        RhoCostParallelWave wave;
        CettaParallelExecutor executor;
        RhoRuntimeProfile wave_profile = {
            .scheduler_policy = RUN_SCHEDULE_CANONICAL,
            .reduction_limit = UINT32_MAX,
            .thread_count = thread_count,
            .threaded = true,
        };
        bool executor_initialized = false;
        uint64_t remaining64;
        uint32_t remaining;
        uint32_t selected = 0u;
        uint32_t applied = 0u;

        if (bounded_fuel && fuel_precedes_frontier && reductions >= fuel) {
            *out_status = RHOCOST_PARALLEL_RUN_FUEL_EXHAUSTED;
            break;
        }
        rhocost_parallel_task_vec_init(&tasks);
        search.stopped = false;
        if (!rhocost_collect_parallel_tasks_from_components(
                arena, &components, &tasks,
                bounded_search ? &search : NULL)) {
            rhocost_parallel_task_vec_free(&tasks);
            rho_validation_set("cost-rho parallel run could not enumerate firings");
            ok = false;
            break;
        }
        if (tasks.len == 0u) {
            rhocost_parallel_task_vec_free(&tasks);
            *out_status = search.exhausted
                ? RHOCOST_PARALLEL_RUN_SEARCH_EXHAUSTED
                : RHOCOST_PARALLEL_RUN_QUIESCENT;
            break;
        }
        if (bounded_fuel && reductions >= fuel) {
            rhocost_parallel_task_vec_free(&tasks);
            *out_status = RHOCOST_PARALLEL_RUN_FUEL_EXHAUSTED;
            break;
        }
        remaining64 = bounded_fuel ? fuel - reductions : UINT32_MAX;
        remaining = remaining64 > UINT32_MAX
            ? UINT32_MAX : (uint32_t)remaining64;
        if (!rhocost_parallel_wave_start(
                &wave_profile, components.len, remaining, &tasks,
                &wave, &executor, &executor_initialized, &selected)) {
            rhocost_parallel_wave_finish(
                &wave, &executor, executor_initialized);
            rhocost_parallel_task_vec_free(&tasks);
            ok = false;
            break;
        }
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_COST_RHO_PARALLEL_WAVE);
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_COST_RHO_PARALLEL_FIRING, selected);
        cetta_runtime_stats_update_max(
            CETTA_RUNTIME_COUNTER_COST_RHO_PARALLEL_WAVE_WIDTH_PEAK,
            selected);
        if (!rhocost_parallel_apply_wave(
                arena, &components, observer, &tasks, &wave,
                selected, reductions, &applied) || applied != selected) {
            rho_validation_set(
                "cost-rho parallel wave failed checked commit");
            rhocost_parallel_wave_finish(&wave, &executor, true);
            rhocost_parallel_task_vec_free(&tasks);
            ok = false;
            break;
        }
        rhocost_parallel_wave_finish(&wave, &executor, true);
        rhocost_parallel_task_vec_free(&tasks);
        reductions += applied;
    }

    if (ok && observe_causality) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_COST_RHO_RECEIPT_VALIDATION);
        if (!rhocost_validate_causal_events(arena, out_events)) {
            rho_validation_set(
                "cost-rho parallel receipt failed causal validation");
            ok = false;
        }
    }
    if (ok) {
        *out_residual = rhocost_runtime_components_term(arena, &components);
        if (!*out_residual) {
            rho_validation_set(
                "cost-rho parallel run could not materialize its residual");
            ok = false;
        }
    }
    if (ok) *out_reductions = reductions;

done:
    rho_vec_free(&components);
    if (observer) rhocost_causal_observer_free(observer);
    if (!ok) {
        if (out_events) rho_vec_free(out_events);
        *out_residual = NULL;
        *out_reductions = 0u;
    }
    return ok;
}

static bool rhocost_collect_steps(Arena *arena, Atom *term,
                                  RhoCostStepSetAcc *out) {
    RhoAtomVec components;
    bool ok;

    if (!arena || !term || !out) return false;
    if (!rhocost_term_well_formed(term)) return false;

    rho_vec_init(&components);
    rhocost_collect_term_components(arena, rhocost_normalize_term(arena, term),
                                    &components);
    ok = rhocost_collect_steps_from_components(arena, &components, out, NULL);
    rho_vec_free(&components);
    return ok;
}

/* ── Sequential causal trace and bounded prefix ─────────────────────────────
 *
 * One terminating pure cost-rho path with a receipt.  Unbounded, the run
 * ends only at proven quiescence.  Bounded, it takes separate firing and
 * cover-search allowances and returns (lts:rho:cost:prefix STATUS RECEIPT)
 * where STATUS is quiescent only after exhaustive search, fuel-exhausted
 * when the firing allowance ends first, and search-exhausted when cover
 * search stops while further search remains semantically relevant. */

static Atom *rhocalc_cost_causal_run_expr(Arena *arena, Atom *term,
                                          bool bounded, uint64_t fuel,
                                          uint64_t search_budget) {
    RhoCostTraceComponentVec components;
    RhoAtomVec events;
    RhoCostSearchControl search = {
        .bounded = bounded,
        .remaining = search_budget,
        .exhausted = false,
        .stop_after_first = true,
        .stopped = false,
    };
    Atom *result = NULL;
    const char *prefix_status = "lts:rho:cost:fuel-exhausted";
    uint64_t event_id = 0;

    if (!arena || !term || !rhocost_term_well_formed(term)) return NULL;

    rhocost_trace_component_vec_init(&components);
    rho_vec_init(&events);
    if (!rhocost_trace_components_add_term(
            arena, &components, term, false, 0)) {
        rho_validation_set("rho cost trace could not initialize provenance");
        goto done;
    }
    rhocost_trace_components_sort(&components);

    for (;;) {
        RhoAtomVec projected_components;
        RhoCostStepSetAcc candidates;
        RhoCostStep *selected;
        Atom *event;
        Atom *projected_next;
        bool collected;

        if (event_id > (uint64_t)INT64_MAX) {
            rho_validation_set("rho cost trace exhausted local EventId range");
            goto done;
        }
        if (bounded && event_id >= fuel) {
            prefix_status = "lts:rho:cost:fuel-exhausted";
            break;
        }
        if (!rhocost_trace_components_project(
                &components, &projected_components)) {
            rho_validation_set("rho cost trace could not project its state");
            goto done;
        }
        rhocost_step_set_acc_init_traced(&candidates);
        search.stopped = false;
        collected = rhocost_collect_steps_from_components(
            arena, &projected_components, &candidates, &search);
        rho_vec_free(&projected_components);
        if (!collected) {
            rhocost_step_set_acc_free(&candidates);
            rho_validation_set("rho cost trace step construction failed");
            goto done;
        }
        if (search.exhausted) {
            rhocost_step_set_acc_free(&candidates);
            prefix_status = "lts:rho:cost:search-exhausted";
            break;
        }
        if (candidates.len == 0) {
            rhocost_step_set_acc_free(&candidates);
            prefix_status = "lts:rho:cost:quiescent";
            break;
        }

        /* Select a concrete firing before successor-state deduplication. */
        selected = &candidates.items[0];
        event = rhocost_trace_event(arena, &components, selected, event_id);
        if (!event || !rho_vec_push(&events, event) ||
            !rhocost_trace_apply_step(
                arena, &components, selected, event_id)) {
            rhocost_step_set_acc_free(&candidates);
            rho_validation_set("rho cost trace provenance update failed");
            goto done;
        }
        projected_next = rhocost_trace_components_term(arena, &components);
        if (!projected_next || !atom_eq(projected_next, selected->result)) {
            rhocost_step_set_acc_free(&candidates);
            rho_validation_set(
                "rho cost trace projection disagrees with the cost step");
            goto done;
        }
        rhocost_step_set_acc_free(&candidates);
        event_id++;
    }

    {
        Atom *final_state =
            rhocost_trace_components_term(arena, &components);
        Atom *receipt_args[2];
        Atom *receipt;
        if (!final_state) {
            rho_validation_set("rho cost trace could not project final state");
            goto done;
        }
        receipt_args[0] = atom_expr(arena, events.items, events.len);
        receipt_args[1] = final_state;
        receipt = rho_call(arena, "lts:rho:cost:receipt", receipt_args, 2);
        if (!bounded) {
            result = receipt;
        } else {
            Atom *prefix_args[2] = {
                atom_symbol(arena, prefix_status),
                receipt
            };
            result = rho_call(arena, "lts:rho:cost:prefix", prefix_args, 2);
        }
    }

done:
    rho_vec_free(&events);
    rhocost_trace_component_vec_free(&components);
    return result;
}

Atom *rhocalc_cost_causal_trace_expr(Arena *arena, Atom *term) {
    return rhocalc_cost_causal_run_expr(arena, term, false, 0, 0);
}

Atom *rhocalc_cost_causal_prefix_expr(Arena *arena, Atom *term,
                                      uint64_t fuel,
                                      uint64_t search_budget) {
    return rhocalc_cost_causal_run_expr(arena, term, true, fuel,
                                        search_budget);
}

static Atom *rhocost_parallel_receipt_expr(
    Arena *arena, RhoAtomVec *events, Atom *residual,
    bool bounded, RhoCostParallelRunStatus status) {
    Atom *receipt_args[2];
    Atom *receipt;
    if (!arena || !events || !residual) return NULL;
    receipt_args[0] = atom_expr(arena, events->items, events->len);
    receipt_args[1] = residual;
    receipt = rho_call(arena, "lts:rho:cost:receipt", receipt_args, 2);
    if (!bounded) return receipt;
    {
        const char *status_name = status == RHOCOST_PARALLEL_RUN_QUIESCENT
            ? "lts:rho:cost:quiescent"
            : status == RHOCOST_PARALLEL_RUN_SEARCH_EXHAUSTED
                ? "lts:rho:cost:search-exhausted"
                : "lts:rho:cost:fuel-exhausted";
        Atom *prefix_args[2] = {atom_symbol(arena, status_name), receipt};
        return rho_call(arena, "lts:rho:cost:prefix", prefix_args, 2);
    }
}

Atom *rhocalc_cost_causal_trace_parallel_expr(Arena *arena, Atom *term,
                                               uint32_t thread_count) {
    RhoAtomVec events;
    RhoCostParallelRunStatus status;
    Atom *residual = NULL;
    Atom *result;
    uint64_t reductions;
    if (!rhocost_parallel_run(
            arena, term, thread_count,
            false, 0u, false, false, 0u,
            &events, &residual, &reductions, &status)) {
        return NULL;
    }
    (void)reductions;
    result = rhocost_parallel_receipt_expr(
        arena, &events, residual, false, status);
    rho_vec_free(&events);
    return result;
}

Atom *rhocalc_cost_causal_prefix_parallel_expr(
    Arena *arena, Atom *term, uint64_t fuel,
    uint64_t search_budget, uint32_t thread_count) {
    RhoAtomVec events;
    RhoCostParallelRunStatus status;
    Atom *residual = NULL;
    Atom *result;
    uint64_t reductions;
    if (!rhocost_parallel_run(
            arena, term, thread_count,
            true, fuel, true, true, search_budget,
            &events, &residual, &reductions, &status)) {
        return NULL;
    }
    (void)reductions;
    result = rhocost_parallel_receipt_expr(
        arena, &events, residual, true, status);
    rho_vec_free(&events);
    return result;
}

static bool rhocost_collect_step_set(Arena *arena, Atom *term,
                                     RhoCostStepSet *out) {
    RhoCostStepSetAcc acc;
    if (!arena || !term || !out) return false;
    out->items = NULL;
    out->len = 0;
    rhocost_step_set_acc_init(&acc);
    if (!rhocost_collect_steps(arena, term, &acc)) {
        rhocost_step_set_acc_free(&acc);
        return false;
    }
    rhocost_step_set_acc_finish(&acc, out);
    return true;
}

/* The cost successors, (superpose (S ...)): each S the successor, or with its
 * cost (lts:rho:cost:step CONSUMED-SIGNATURE SUCCESSOR). */
static Atom *rhocost_frontier_expr(Arena *arena, Atom *term, bool with_cost) {
    RhoCostStepSet steps = {0};
    if (!rhocost_collect_step_set(arena, term, &steps)) {
        return NULL;
    }
    Atom **items = arena_alloc(arena, sizeof(Atom *) * steps.len);
    if (!items && steps.len > 0) {
        rhocost_step_set_free(&steps);
        return NULL;
    }
    for (uint32_t i = 0; i < steps.len; i++) {
        Atom *args[2] = {steps.items[i].consumed_sig, steps.items[i].result};
        items[i] = with_cost ? rho_call(arena, "lts:rho:cost:step", args, 2)
                             : steps.items[i].result;
    }
    Atom *list = atom_expr(arena, items, steps.len);
    Atom *result = atom_expr2(arena, atom_symbol(arena, "superpose"), list);
    rhocost_step_set_free(&steps);
    return result;
}

static Atom *rhocost_successor_frontier_expr(Arena *arena, Atom *term) {
    return rhocost_frontier_expr(arena, term, false);
}

Atom *rhocalc_cost_step_frontier_expr(Arena *arena, Atom *term) {
    return rhocost_frontier_expr(arena, term, true);
}

Atom *rhocalc_successor_frontier_expr_with_semantic_profile(
    Arena *arena, Atom *proc, RhocalcSemanticProfileId semantic_profile) {
    return rhocalc_successor_frontier_expr_with_semantic_profile_and_eval_context(
        arena, proc, semantic_profile, NULL);
}

Atom *rhocalc_successor_frontier_expr_with_semantic_profile_and_eval_context(
    Arena *arena, Atom *proc, RhocalcSemanticProfileId semantic_profile,
    const RhocalcEvalContext *eval_context) {
    if (!rhocalc_semantic_profile_runtime_supported(semantic_profile)) {
        return NULL;
    }
    if (semantic_profile == RHOCALC_SEMANTIC_PROFILE_COST) {
        return rhocost_successor_frontier_expr(arena, proc);
    }
    if (semantic_profile == RHOCALC_SEMANTIC_PROFILE_RHOMETTA &&
        eval_context == NULL) {
        rho_validation_set("rhometta execution requires an evaluation context");
        return NULL;
    }
    return rhocalc_successor_frontier_expr_with_eval_context(arena, proc,
                                                             eval_context);
}

static bool rho_machine_select_rotating_successor(RhoMachine *machine,
                                                  Atom **out_next,
                                                  bool *out_quiescent) {
    RhoSuccessorSetAcc successor_acc;
    RhoSuccessorSet successors = {0};

    if (!machine || !machine->arena || !machine->current ||
        !out_next || !out_quiescent) {
        return false;
    }
    *out_next = NULL;
    *out_quiescent = true;

    rho_successor_set_acc_init(&successor_acc);
    if (!rho_collect_successors(machine->arena, machine->current,
                                machine->eval_context, &successor_acc)) {
        rho_successor_set_acc_free(&successor_acc);
        return false;
    }
    rho_successor_set_acc_finish(&successor_acc, &successors);
    if (successors.len == 0) {
        free(successors.items);
        *out_next = machine->current;
        return true;
    }

    *out_quiescent = false;
    *out_next = successors.items[run_pick(RUN_SCHEDULE_ROTATING, successors.len,
                                          &machine->rotating_turn)];
    machine->current_canonical_order = true;
    free(successors.items);
    return true;
}

static bool rho_machine_select_successor(RhoMachine *machine,
                                         Atom **out_next,
                                         bool *out_quiescent) {
    if (!machine) return false;
    switch (machine->profile.scheduler_policy) {
    case RUN_SCHEDULE_CANONICAL:
        return rho_machine_select_canonical_successor(machine,
                                                      out_next,
                                                      out_quiescent);
    case RUN_SCHEDULE_ROTATING:
        return rho_machine_select_rotating_successor(machine,
                                                     out_next,
                                                     out_quiescent);
    case RUN_SCHEDULE_COUNT:
        break;
    }
    rho_validation_set("rho scheduler policy is not implemented");
    return false;
}

static bool rho_collect_successors(Arena *arena, Atom *proc,
                                   const RhocalcEvalContext *eval_context,
                                   RhoSuccessorSetAcc *out) {
    RhoAtomVec components;
    RhoEndpointVec sends;
    RhoEndpointVec recvs;
    uint32_t send_pos = 0;
    uint32_t recv_pos = 0;
    rho_vec_init(&components);
    rho_endpoint_vec_init(&sends);
    rho_endpoint_vec_init(&recvs);
    rho_collect_par(arena, proc, &components);

    if (!rho_collect_endpoints(&components, &sends, &recvs)) {
        rho_endpoint_vec_free(&sends);
        rho_endpoint_vec_free(&recvs);
        rho_vec_free(&components);
        return false;
    }

    uint32_t recv_start = 0u, send_start = 0u;
    while (rho_next_comm_run(&recvs, &sends, &recv_pos, &send_pos,
                             &recv_start, &send_start)) {
        for (uint32_t r = recv_start; r < recv_pos; r++) {
            for (uint32_t s = send_start; s < send_pos; s++) {
                if (!rho_emit_comm_results(arena, &components,
                                           &sends.items[s],
                                           &recvs.items[r],
                                           eval_context, out)) {
                    rho_endpoint_vec_free(&sends);
                    rho_endpoint_vec_free(&recvs);
                    rho_vec_free(&components);
                    return false;
                }
            }
        }
    }

    rho_endpoint_vec_free(&sends);
    rho_endpoint_vec_free(&recvs);
    rho_vec_free(&components);
    return true;
}

/* ── Public reduction entry points ──────────────────────────────────────────
 *
 * The CLI and library interfaces land here: run a term to quiescence under a
 * runtime profile (scheduler policy, reduction limit, worker count) and a
 * semantic profile (strict-core or cost), sequentially or threaded. */

/* The strict core and rhometta: a term to quiescence under a runtime profile. */
static bool rho_reduce_core(
    Arena *arena, Atom *proc, const RhoRuntimeProfile *profile,
    const RhocalcEvalContext *eval_context, RhoReductionResult *out) {
    RhoMachine machine;

    if (!arena || !proc || !profile || !out) return false;
    out->residual = NULL;
    out->reductions_taken = 0;
    out->status = RHOCALC_REDUCTION_QUIESCENT;

    if (!rho_runtime_profile_supported(profile)) {
        return false;
    }
    if (profile->threaded) {
        return rhocalc_reduce_to_quiescence_threaded(arena, proc, profile,
                                                     eval_context, out);
    }

    rho_machine_init(&machine, arena, profile, eval_context);
    if (!rho_machine_load_process(&machine, proc)) {
        rho_machine_free(&machine);
        return false;
    }

    RunBudget budget = {.limit = profile->reduction_limit};   /* the runner's budget */
    for (;;) {
        Atom *next = NULL;
        bool quiescent = false;
        if (!rho_machine_select_successor(&machine, &next, &quiescent)) {
            rho_machine_free(&machine);
            return false;
        }
        if (quiescent) {
            out->residual = rho_machine_export_current(&machine);
            rho_machine_free(&machine);
            return out->residual != NULL;
        }
        if (!run_spend(&budget)) {
            out->residual = rho_machine_export_current(&machine);
            out->status = RHOCALC_REDUCTION_LIMIT_EXHAUSTED;
            rho_machine_free(&machine);
            return out->residual != NULL;
        }
        machine.current = next;
        out->reductions_taken = (uint32_t)budget.spent;
        machine.reductions_since_compaction++;
        if (rho_machine_compaction_enabled(&machine) &&
            machine.reductions_since_compaction ==
                RHO_MACHINE_COMPACTION_INTERVAL &&
            !rho_machine_compact_current(&machine)) {
            rho_machine_free(&machine);
            return false;
        }
    }
}

static bool rhocost_reduce_to_quiescence_threaded(
    Arena *arena, Atom *term, const RhoRuntimeProfile *profile,
    RhoReductionResult *out) {
    RhoCostParallelRunStatus status;
    Atom *residual = NULL;
    uint64_t reductions = 0u;
    bool ok;

    if (!arena || !term || !profile || !out ||
        profile->thread_count < 2u) {
        return false;
    }
    ok = rhocost_parallel_run(
        arena, term, profile->thread_count,
        true, profile->reduction_limit, false,
        false, 0u, NULL, &residual, &reductions, &status);
    if (ok && reductions > UINT32_MAX) {
        rho_validation_set("cost-rho threaded run exceeded reduction counter range");
        ok = false;
    }
    if (ok) {
        out->residual = residual;
        out->reductions_taken = (uint32_t)reductions;
        out->status = status == RHOCOST_PARALLEL_RUN_QUIESCENT
            ? RHOCALC_REDUCTION_QUIESCENT
            : RHOCALC_REDUCTION_LIMIT_EXHAUSTED;
    }
    return ok;
}

static bool rhocost_reduce_to_quiescence_with_profile(
    Arena *arena, Atom *term, const RhoRuntimeProfile *profile,
    RhoReductionResult *out) {
    Atom *current;
    uint64_t rotating_turn = 0u;

    if (!arena || !term || !profile || !out) return false;
    out->residual = NULL;
    out->reductions_taken = 0;
    out->status = RHOCALC_REDUCTION_QUIESCENT;

    if (!rho_runtime_profile_supported(profile)) {
        return false;
    }
    if (profile->threaded) {
        return rhocost_reduce_to_quiescence_threaded(arena, term, profile,
                                                      out);
    }
    if (!rhocost_term_well_formed(term)) {
        return false;
    }
    current = rhocost_normalize_term(arena, term);

    RunBudget budget = {.limit = profile->reduction_limit};   /* the runner's budget */
    for (;;) {
        RhoCostStepSet successors = {0};
        Atom *next;
        if (!rhocost_collect_step_set(arena, current, &successors)) {
            rhocost_step_set_free(&successors);
            return false;
        }
        if (successors.len == 0) {
            out->residual = current;
            rhocost_step_set_free(&successors);
            return true;
        }
        if (!run_spend(&budget)) {
            out->residual = current;
            out->status = RHOCALC_REDUCTION_LIMIT_EXHAUSTED;
            rhocost_step_set_free(&successors);
            return true;
        }
        next = successors.items[run_pick(profile->scheduler_policy, successors.len,
                                         &rotating_turn)].result;
        current = next;
        out->reductions_taken = (uint32_t)budget.spent;
        rhocost_step_set_free(&successors);
    }
}

bool rhocalc_reduce_to_quiescence(
    Arena *arena, Atom *proc, RhocalcSemanticProfileId semantic_profile,
    const RhoRuntimeProfile *profile, const RhocalcEvalContext *eval_context,
    RhoReductionResult *out) {
    if (!rhocalc_semantic_profile_runtime_supported(semantic_profile)) {
        return false;
    }
    if (semantic_profile == RHOCALC_SEMANTIC_PROFILE_COST) {
        return rhocost_reduce_to_quiescence_with_profile(arena, proc, profile, out);
    }
    if (semantic_profile == RHOCALC_SEMANTIC_PROFILE_RHOMETTA &&
        eval_context == NULL) {
        rho_validation_set("rhometta execution requires an evaluation context");
        return false;
    }
    return rho_reduce_core(arena, proc, profile, eval_context, out);
}

static bool rho_observe_top_level_values_into(
    Atom *residual, Atom **values, size_t capacity,
    size_t *count, size_t depth) {
    RhoView view;

    if (!residual || !count || depth > 4096u) return false;
    view = rho_view(residual);
    if (view.kind == RHO_VAL) {
        if (values) {
            if (*count >= capacity) return false;
            values[*count] = view.args[0];
        }
        (*count)++;
        return true;
    }
    if (view.kind != RHO_PAR) return true;
    for (uint32_t index = 0u; index < view.nargs; index++) {
        if (!rho_observe_top_level_values_into(
                view.args[index], values, capacity, count, depth + 1u)) {
            return false;
        }
    }
    return true;
}

Atom *rhocalc_observe_top_level_values(Arena *arena, Atom *residual) {
    Atom **values = NULL;
    size_t count = 0u;
    size_t filled = 0u;

    if (!arena || !residual ||
        !rho_observe_top_level_values_into(
            residual, NULL, 0u, &count, 0u) ||
        count > UINT32_MAX) {
        return NULL;
    }
    if (count > 0u) {
        if (count > SIZE_MAX / sizeof(*values)) return NULL;
        values = arena_alloc(arena, count * sizeof(*values));
    }
    if (!rho_observe_top_level_values_into(
            residual, values, count, &filled, 0u) || filled != count) {
        return NULL;
    }
    return atom_expr(arena, values, (CettaExprLen)count);
}
