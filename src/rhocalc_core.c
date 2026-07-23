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
 *                 a COMM.  The rhometta extension (rho:eval-payload) defers a
 *                 MeTTa term as a payload evaluated at the COMM, with each
 *                 payload run against sibling-isolated copy-on-write scratch.
 *
 *   cost          Funded reactions.  Processes are signed
 *                 (rho:cost:signed body sig), channels carry purses of
 *                 tokens (rho:cost:purse surface stack), and a COMM fires
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
 * Shared machinery: alpha-invariant canonical keys give both profiles a
 * canonical form (rho:par flattened and key-sorted, the name equivalence
 * @(*x) = x oriented as a simplification), so structural congruence becomes
 * string equality on keys.  Purse location follows the same name
 * equivalence: aliases of a channel are the same account.
 *
 * Concurrency: the strict-core threaded executor matches endpoints through
 * per-channel buckets; the cost profile executes waves of compatible
 * firings, where each worker atomically claims the exact endpoint and
 * funding-cover occurrences (in sorted resource order) before firing, and
 * every wave commit is re-validated against occurrence accounting.  Causal
 * receipts are an optional observer on either path: a state-only run and a
 * receipt-observed run consume identical resources.
 *
 * Surfaces: lib/lts/rho/cost.metta and lib/lts/rho.metta (MeTTa API),
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
#include "match.h"
#include "parallel_executor.h"
#include "stats.h"

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
    Atom *atom;
    char *key;
    uint32_t ordinal;
} RhoKeyedAtom;

typedef struct {
    Atom **items;
    char **keys;
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
    char *key;
} RhoEndpoint;

typedef struct {
    RhoEndpoint *items;
    uint32_t len;
    uint32_t cap;
} RhoEndpointVec;

typedef struct {
    Arena *arena;
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
} RhoMachine;

typedef struct {
    VarId var_id;
    uint32_t index;
} RhoAlphaEntry;

typedef struct {
    RhoAlphaEntry *items;
    uint32_t len;
    uint32_t cap;
    uint32_t next_index;
} RhoAlphaEnv;

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} RhoStr;

typedef struct {
    bool ready;
    SymbolId nil;
    SymbolId par;
    SymbolId send;
    SymbolId recv;
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

typedef struct RhoAsyncEndpoint RhoAsyncEndpoint;
struct RhoAsyncEndpoint {
    RhoAsyncEndpointKind kind;
    Atom *atom;
    RhoView view;
    RhoAsyncEndpoint *next;
};

typedef struct RhoChannelBucket RhoChannelBucket;
struct RhoChannelBucket {
    char *key;
    pthread_mutex_t mutex;
    RhoAsyncEndpoint *send_head;
    RhoAsyncEndpoint *send_tail;
    RhoAsyncEndpoint *recv_head;
    RhoAsyncEndpoint *recv_tail;
    RhoChannelBucket *next;
};

typedef struct {
    pthread_mutex_t mutex;
    RhoChannelBucket *head;
} RhoChannelTable;

typedef struct RhoAsyncExecutor RhoAsyncExecutor;

struct RhoAsyncExecutor {
    Arena *global_arena;
    RhoRuntimeProfile profile;
    const RhocalcEvalContext *eval_context;
    CettaParallelExecutor parallel;
    RhoChannelTable channels;
    RhoAtomVec residuals;
    pthread_mutex_t residual_mutex;
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

static char *rho_heap_strdup(const char *text) {
    size_t len = strlen(text) + 1u;
    char *out = cetta_malloc(len);
    memcpy(out, text, len);
    return out;
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
    }
    return "unknown";
}

static bool rhocalc_semantic_profile_runtime_supported(
    RhocalcSemanticProfileId profile) {
    if (profile == RHOCALC_SEMANTIC_PROFILE_STRICT_CORE ||
        profile == RHOCALC_SEMANTIC_PROFILE_COST) {
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
    if (atom->kind == ATOM_VAR) return atom->var_id == var_id;
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

static char *rho_atom_text_key(Atom *atom) {
    Arena scratch;
    char *rendered;
    char *out;

    arena_init(&scratch);
    rendered = atom_to_string(&scratch, atom);
    out = rendered ? rho_heap_strdup(rendered) : rho_heap_strdup("");
    arena_free(&scratch);
    return out;
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
    if (scope->len == scope->cap) {
        uint32_t next_cap = scope->cap ? scope->cap * 2u : 8u;
        VarId *next = cetta_realloc(scope->items, sizeof(VarId) * next_cap);
        if (!next) return false;
        scope->items = next;
        scope->cap = next_cap;
    }
    scope->items[scope->len++] = var_id;
    return true;
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
    if (vec->len == vec->cap) {
        uint32_t next_cap = vec->cap ? vec->cap * 2u : 8u;
        Atom **next = cetta_realloc(vec->items, sizeof(Atom *) * next_cap);
        if (!next) return false;
        vec->items = next;
        vec->cap = next_cap;
    }
    vec->items[vec->len++] = atom;
    return true;
}

static void rho_successor_set_acc_init(RhoSuccessorSetAcc *acc) {
    acc->items = NULL;
    acc->keys = NULL;
    acc->len = 0;
    acc->cap = 0;
}

static void rho_successor_set_acc_free(RhoSuccessorSetAcc *acc) {
    if (acc->keys) {
        for (uint32_t i = 0; i < acc->len; i++) {
            free(acc->keys[i]);
        }
    }
    free(acc->items);
    free(acc->keys);
    acc->items = NULL;
    acc->keys = NULL;
    acc->len = 0;
    acc->cap = 0;
}

static bool rho_successor_set_acc_grow(RhoSuccessorSetAcc *acc) {
    uint32_t next_cap = acc->cap ? acc->cap * 2u : 8u;
    Atom **next_items = cetta_realloc(acc->items,
                                      sizeof(Atom *) * next_cap);
    if (!next_items) return false;
    acc->items = next_items;
    char **next_keys = cetta_realloc(acc->keys,
                                     sizeof(char *) * next_cap);
    if (!next_keys) return false;
    acc->keys = next_keys;
    acc->cap = next_cap;
    return true;
}

static void rho_endpoint_vec_init(RhoEndpointVec *vec) {
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static void rho_endpoint_vec_free(RhoEndpointVec *vec) {
    for (uint32_t i = 0; i < vec->len; i++) {
        free(vec->items[i].key);
    }
    free(vec->items);
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static bool rho_endpoint_vec_push(RhoEndpointVec *vec,
                                  uint32_t component_index,
                                  RhoView view,
                                  char *key) {
    if (vec->len == vec->cap) {
        uint32_t next_cap = vec->cap ? vec->cap * 2u : 8u;
        RhoEndpoint *next = cetta_realloc(vec->items,
                                          sizeof(RhoEndpoint) * next_cap);
        if (!next) return false;
        vec->items = next;
        vec->cap = next_cap;
    }
    vec->items[vec->len].component_index = component_index;
    vec->items[vec->len].view = view;
    vec->items[vec->len].key = key;
    vec->len++;
    return true;
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
    case RHO_RECV: {
        if (view.nargs != 3) {
            rho_validation_set("rho:recv expects channel, binder, and body");
            return false;
        }
        if (view.args[1]->kind != ATOM_VAR) {
            rho_validation_set("rho:recv binder must be a variable");
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

bool rhocalc_process_well_formed(Atom *proc) {
    RhoScope scope;
    bool ok;
    rho_validation_clear();
    rho_scope_init(&scope);
    ok = rho_check_proc(proc, &scope, false);
    rho_scope_free(&scope);
    return ok;
}

bool rhocalc_process_well_formed_with_eval_payloads(Atom *proc) {
    RhoScope scope;
    bool ok;
    rho_validation_clear();
    rho_scope_init(&scope);
    ok = rho_check_proc(proc, &scope, true);
    rho_scope_free(&scope);
    return ok;
}

bool rhocalc_process_well_formed_with_semantic_profile(
    Atom *proc, RhocalcSemanticProfileId semantic_profile) {
    if (!rhocalc_semantic_profile_runtime_supported(semantic_profile)) {
        return false;
    }
    if (semantic_profile == RHOCALC_SEMANTIC_PROFILE_COST) {
        return rhocost_term_well_formed(proc);
    }
    return rhocalc_process_well_formed(proc);
}

/* ── Alpha-invariant canonical keys ─────────────────────────────────────────
 *
 * Every term gets a text key in which bound variables are numbered by
 * binding order ($#0, $#1, ...) and free variables keep their identity, so
 * alpha-equivalent terms share one key.  Keys make structural congruence a
 * string comparison: rho:par components sort by key, successor sets dedup
 * by key, and endpoint matching compares channel keys.  The name
 * equivalence @(*x) = x is collapsed while keying, so aliases of a channel
 * compare equal. */

static void rho_alpha_init(RhoAlphaEnv *env) {
    env->items = NULL;
    env->len = 0;
    env->cap = 0;
    env->next_index = 0;
}

static void rho_alpha_free(RhoAlphaEnv *env) {
    free(env->items);
    env->items = NULL;
    env->len = 0;
    env->cap = 0;
    env->next_index = 0;
}

static uint32_t rho_alpha_mark(const RhoAlphaEnv *env) {
    return env->len;
}

static void rho_alpha_pop(RhoAlphaEnv *env, uint32_t mark) {
    if (mark <= env->len) env->len = mark;
}

static bool rho_alpha_push(RhoAlphaEnv *env, VarId var_id) {
    if (env->len == env->cap) {
        uint32_t next_cap = env->cap ? env->cap * 2u : 8u;
        RhoAlphaEntry *next =
            cetta_realloc(env->items, sizeof(RhoAlphaEntry) * next_cap);
        if (!next) return false;
        env->items = next;
        env->cap = next_cap;
    }
    env->items[env->len].var_id = var_id;
    env->items[env->len].index = env->next_index++;
    env->len++;
    return true;
}

static bool rho_alpha_lookup(const RhoAlphaEnv *env, VarId var_id,
                             uint32_t *out_index) {
    for (uint32_t i = env->len; i > 0; i--) {
        if (env->items[i - 1].var_id == var_id) {
            if (out_index) *out_index = env->items[i - 1].index;
            return true;
        }
    }
    return false;
}

static bool rho_str_reserve(RhoStr *str, size_t extra) {
    size_t needed = str->len + extra + 1u;
    if (needed <= str->cap) return true;
    size_t next_cap = str->cap ? str->cap * 2u : 128u;
    while (next_cap < needed) next_cap *= 2u;
    char *next = realloc(str->data, next_cap);
    if (!next) return false;
    str->data = next;
    str->cap = next_cap;
    return true;
}

static bool rho_str_append_n(RhoStr *str, const char *text, size_t len) {
    if (!rho_str_reserve(str, len)) return false;
    memcpy(str->data + str->len, text, len);
    str->len += len;
    str->data[str->len] = '\0';
    return true;
}

static bool rho_str_append(RhoStr *str, const char *text) {
    return rho_str_append_n(str, text, strlen(text));
}

static bool rho_str_appendf(RhoStr *str, const char *fmt, ...) {
    va_list ap;
    va_list ap2;
    int needed;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (needed < 0) {
        va_end(ap2);
        return false;
    }
    if (!rho_str_reserve(str, (size_t)needed)) {
        va_end(ap2);
        return false;
    }
    vsnprintf(str->data + str->len, str->cap - str->len, fmt, ap2);
    va_end(ap2);
    str->len += (size_t)needed;
    return true;
}

static void rho_key_proc_into(Atom *proc, RhoAlphaEnv *env, RhoStr *out);
static void rho_key_name_into(Atom *name, RhoAlphaEnv *env, RhoStr *out);

static void rho_key_var_into(Atom *var, RhoAlphaEnv *env, RhoStr *out) {
    uint32_t index = 0;
    if (rho_alpha_lookup(env, var->var_id, &index)) {
        (void)rho_str_appendf(out, "$#%u", index);
    } else {
        (void)rho_str_appendf(out, "$%s:%llu", atom_name_cstr(var),
                              (unsigned long long)var->var_id);
    }
}

static char *rho_key_proc_quoted(Atom *proc) {
    RhoAlphaEnv env;
    RhoStr out = {0};
    rho_alpha_init(&env);
    rho_key_proc_into(proc, &env, &out);
    rho_alpha_free(&env);
    if (!out.data) return rho_heap_strdup("");
    return out.data;
}

static void rho_key_name_into(Atom *name, RhoAlphaEnv *env, RhoStr *out) {
    if (name->kind == ATOM_VAR) {
        rho_key_var_into(name, env, out);
        return;
    }
    RhoView view = rho_view(name);
    if (view.kind == RHO_QUOTE && view.nargs == 1) {
        RhoView inner = rho_view(view.args[0]);
        if (inner.kind == RHO_DROP && inner.nargs == 1) {
            rho_key_name_into(inner.args[0], env, out);
            return;
        }
        (void)rho_str_append(out, "@(");
        {
            char *quoted_key = rho_key_proc_quoted(view.args[0]);
            (void)rho_str_append(out, quoted_key);
            free(quoted_key);
        }
        (void)rho_str_append(out, ")");
        return;
    }
    (void)rho_str_append(out, "?bad-name");
}

static void rho_key_proc_into(Atom *proc, RhoAlphaEnv *env, RhoStr *out) {
    RhoView view = rho_view(proc);
    switch (view.kind) {
    case RHO_NIL:
        (void)rho_str_append(out, "0");
        return;
    case RHO_PAR:
        (void)rho_str_append(out, "par(");
        for (uint32_t i = 0; i < view.nargs; i++) {
            if (i > 0) (void)rho_str_append(out, "|");
            rho_key_proc_into(view.args[i], env, out);
        }
        (void)rho_str_append(out, ")");
        return;
    case RHO_SEND:
        (void)rho_str_append(out, "send(");
        rho_key_name_into(view.args[0], env, out);
        (void)rho_str_append(out, ",");
        rho_key_proc_into(view.args[1], env, out);
        (void)rho_str_append(out, ")");
        return;
    case RHO_RECV: {
        uint32_t mark = rho_alpha_mark(env);
        (void)rho_str_append(out, "recv(");
        rho_key_name_into(view.args[0], env, out);
        (void)rho_str_append(out, ",");
        (void)rho_alpha_push(env, view.args[1]->var_id);
        rho_key_proc_into(view.args[2], env, out);
        rho_alpha_pop(env, mark);
        (void)rho_str_append(out, ")");
        return;
    }
    case RHO_DROP:
        (void)rho_str_append(out, "drop(");
        rho_key_name_into(view.args[0], env, out);
        (void)rho_str_append(out, ")");
        return;
    case RHO_VAL: {
        char *payload_key = rho_atom_text_key(view.args[0]);
        (void)rho_str_append(out, "val(");
        (void)rho_str_append(out, payload_key);
        (void)rho_str_append(out, ")");
        free(payload_key);
        return;
    }
    case RHO_EVAL_PAYLOAD: {
        char *payload_key = rho_atom_text_key(view.args[0]);
        (void)rho_str_append(out, "defer(");
        (void)rho_str_append(out, payload_key);
        (void)rho_str_append(out, ")");
        free(payload_key);
        return;
    }
    case RHO_QUOTE:
    case RHO_BAD:
        break;
    }
    (void)rho_str_append(out, "?bad-proc");
}

static char *rho_key_proc(Atom *proc) {
    RhoAlphaEnv env;
    RhoStr out = {0};
    rho_alpha_init(&env);
    rho_key_proc_into(proc, &env, &out);
    rho_alpha_free(&env);
    if (!out.data) return rho_heap_strdup("");
    return out.data;
}

static char *rho_key_name(Atom *name) {
    RhoAlphaEnv env;
    RhoStr out = {0};
    rho_alpha_init(&env);
    rho_key_name_into(name, &env, &out);
    rho_alpha_free(&env);
    if (!out.data) return rho_heap_strdup("");
    return out.data;
}

static int rho_keyed_atom_cmp(const void *lhs, const void *rhs) {
    const RhoKeyedAtom *a = lhs;
    const RhoKeyedAtom *b = rhs;
    int by_key = strcmp(a->key, b->key);
    if (by_key != 0) return by_key;
    if (a->ordinal < b->ordinal) return -1;
    if (a->ordinal > b->ordinal) return 1;
    return 0;
}

static int rho_endpoint_cmp_key(const void *lhs, const void *rhs) {
    const RhoEndpoint *a = lhs;
    const RhoEndpoint *b = rhs;
    int by_key = strcmp(a->key, b->key);
    if (by_key != 0) return by_key;
    if (a->component_index < b->component_index) return -1;
    if (a->component_index > b->component_index) return 1;
    return 0;
}

/* ── Canonical form ─────────────────────────────────────────────────────────
 *
 * Normalization flattens nested rho:par, drops rho:nil components, sorts
 * the survivors under their canonical keys, and orients the name
 * cancellation @(*x) -> x as a directed simplification.  Two structurally
 * congruent processes normalize to the same term. */

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

static Atom *rho_par_from_vec(Arena *arena, RhoAtomVec *vec) {
    if (vec->len == 0) return rho_nil(arena);
    if (vec->len == 1) return vec->items[0];

    RhoKeyedAtom *items = cetta_malloc(sizeof(RhoKeyedAtom) * vec->len);
    for (uint32_t i = 0; i < vec->len; i++) {
        items[i].atom = vec->items[i];
        items[i].key = rho_key_proc(vec->items[i]);
        items[i].ordinal = i;
    }
    qsort(items, vec->len, sizeof(RhoKeyedAtom), rho_keyed_atom_cmp);

    Atom **args = arena_alloc(arena, sizeof(Atom *) * vec->len);
    for (uint32_t i = 0; i < vec->len; i++) {
        args[i] = items[i].atom;
        free(items[i].key);
    }
    free(items);

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
        return rho_ternary(arena, "rho:recv",
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
    if (name->kind == ATOM_VAR) return name->var_id == var_id;
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
        if (view.nargs != 3) return false;
        if (rho_name_has_free_var(view.args[0], var_id)) return true;
        if (view.args[1]->kind == ATOM_VAR &&
            view.args[1]->var_id == var_id) {
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

static Atom *rho_rename_proc(Arena *arena, Atom *proc,
                             VarId old_id, Atom *replacement_name);

static Atom *rho_rename_name(Arena *arena, Atom *name,
                             VarId old_id, Atom *replacement_name) {
    Atom *norm = rho_normalize_name(arena, name);
    if (norm->kind == ATOM_VAR && norm->var_id == old_id) {
        return replacement_name;
    }
    return norm;
}

static Atom *rho_rename_proc(Arena *arena, Atom *proc,
                             VarId old_id, Atom *replacement_name) {
    RhoView view = rho_view(proc);
    switch (view.kind) {
    case RHO_NIL:
        return rho_nil(arena);
    case RHO_PAR: {
        Atom **args = arena_alloc(arena, sizeof(Atom *) * view.nargs);
        for (uint32_t i = 0; i < view.nargs; i++) {
            args[i] = rho_rename_proc(arena, view.args[i], old_id,
                                      replacement_name);
        }
        return rho_normalize_proc(arena, rho_call(arena, "rho:par",
                                                  args, view.nargs));
    }
    case RHO_SEND:
        return rho_normalize_proc(arena,
            rho_binary(arena, "rho:send",
                       rho_rename_name(arena, view.args[0], old_id,
                                       replacement_name),
                       rho_rename_proc(arena, view.args[1], old_id,
                                       replacement_name)));
    case RHO_RECV:
        if (view.args[1]->kind == ATOM_VAR &&
            view.args[1]->var_id == old_id) {
            return rho_ternary(arena, "rho:recv",
                               rho_rename_name(arena, view.args[0], old_id,
                                               replacement_name),
                               rho_copy_var(arena, view.args[1]),
                               rho_normalize_proc(arena, view.args[2]));
        }
        return rho_normalize_proc(arena,
            rho_ternary(arena, "rho:recv",
                        rho_rename_name(arena, view.args[0], old_id,
                                        replacement_name),
                        rho_copy_var(arena, view.args[1]),
                        rho_rename_proc(arena, view.args[2], old_id,
                                        replacement_name)));
    case RHO_DROP:
        return rho_unary(arena, "rho:drop",
                         rho_rename_name(arena, view.args[0], old_id,
                                         replacement_name));
    case RHO_VAL:
        return rho_value_proc(arena,
                              rho_atom_replace_var(arena, view.args[0], old_id,
                                                   replacement_name));
    case RHO_EVAL_PAYLOAD:
        return rho_unary(arena, "rho:eval-payload",
                         rho_atom_replace_var(arena, view.args[0], old_id,
                                              replacement_name));
    case RHO_QUOTE:
    case RHO_BAD:
        break;
    }
    return proc;
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
        if (norm->var_id == var_id) {
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
        if (view.args[1]->kind == ATOM_VAR &&
            view.args[1]->var_id == var_id) {
            return rho_ternary(arena, "rho:recv",
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
                Atom *fresh = rho_fresh_var_like(arena, binder);
                body = rho_rename_proc(arena, body, binder->var_id, fresh);
                binder = fresh;
            }
            return rho_normalize_proc(arena,
                rho_ternary(arena, "rho:recv",
                            rho_subst_name(arena, view.args[0],
                                           var_id, replacement_name),
                            binder,
                            rho_subst_proc(arena, body,
                                           var_id, replacement_name)));
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
        if (scope->items[i - 1u] == var_id) {
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
        arena, "y", ((VarId)1u << 32) | (VarId)11u);
    Atom *inner = atom_var_with_id(
        arena, "x", ((VarId)1u << 32) | (VarId)12u);
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
                   nameful_view.args[1]->var_id != inner->var_id;
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

static bool rho_successor_set_acc_push_keyed(RhoSuccessorSetAcc *acc, Atom *norm, char *key) {
    if (acc->len == acc->cap && !rho_successor_set_acc_grow(acc)) {
        free(key);
        return false;
    }
    acc->items[acc->len] = norm;
    acc->keys[acc->len] = key;
    acc->len++;
    return true;
}

static bool rho_successor_set_acc_contains_key(const RhoSuccessorSetAcc *acc,
                                               const char *key) {
    if (!acc || !key) return false;
    for (uint32_t i = 0; i < acc->len; i++) {
        if (acc->keys[i] && strcmp(acc->keys[i], key) == 0) {
            return true;
        }
    }
    return false;
}

static void rho_successor_set_acc_finish(RhoSuccessorSetAcc *acc, RhoSuccessorSet *out) {
    if (acc->len > 1) {
        RhoKeyedAtom *items = cetta_malloc(sizeof(RhoKeyedAtom) * acc->len);
        for (uint32_t i = 0; i < acc->len; i++) {
            items[i].atom = acc->items[i];
            items[i].key = acc->keys[i];
            items[i].ordinal = i;
        }
        qsort(items, acc->len, sizeof(RhoKeyedAtom), rho_keyed_atom_cmp);
        uint32_t write = 0;
        char *last_key = NULL;
        for (uint32_t i = 0; i < acc->len; i++) {
            if (last_key && strcmp(items[i].key, last_key) == 0) {
                free(items[i].key);
                continue;
            }
            if (last_key) free(last_key);
            acc->items[write++] = items[i].atom;
            last_key = items[i].key;
        }
        if (last_key) free(last_key);
        acc->len = write;
        free(items);
    } else if (acc->len == 1) {
        free(acc->keys[0]);
    }
    free(acc->keys);
    out->items = acc->items;
    out->len = acc->len;
    acc->items = NULL;
    acc->keys = NULL;
    acc->len = 0;
    acc->cap = 0;
}

static Atom *rho_rebuild_reaction(Arena *arena, RhoAtomVec *components,
                                  uint32_t send_index, uint32_t recv_index,
                                  Atom *body) {
    RhoAtomVec out;
    rho_vec_init(&out);
    for (uint32_t i = 0; i < components->len; i++) {
        if (i == send_index || i == recv_index) continue;
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
    Atom *proc = rho_par_from_vec(arena, &out);
    rho_vec_free(&out);
    return proc;
}

static bool rho_collect_endpoints(RhoAtomVec *components,
                                  RhoEndpointVec *sends,
                                  RhoEndpointVec *recvs) {
    for (uint32_t i = 0; i < components->len; i++) {
        RhoView view = rho_view(components->items[i]);
        if (view.kind == RHO_SEND && view.nargs == 2) {
            char *key = rho_key_name(view.args[0]);
            if (!rho_endpoint_vec_push(sends, i, view, key)) {
                free(key);
                return false;
            }
        } else if (view.kind == RHO_RECV && view.nargs == 3) {
            char *key = rho_key_name(view.args[0]);
            if (!rho_endpoint_vec_push(recvs, i, view, key)) {
                free(key);
                return false;
            }
        }
    }
    if (sends->len > 1) {
        qsort(sends->items, sends->len, sizeof(RhoEndpoint),
              rho_endpoint_cmp_key);
    }
    if (recvs->len > 1) {
        qsort(recvs->items, recvs->len, sizeof(RhoEndpoint),
              rho_endpoint_cmp_key);
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
    StateCell *inline_orig[16];
    StateCell *inline_clone[16];
} RhoStateCloneMap;

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
    if (!orig || rho_state_clone_for(map, orig))
        return true;
    if (!rho_state_clone_map_ensure(map, map->len + 1))
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
                if (!rho_collect_state_ptrs_into_map(map, cell->value))
                    return false;
                if (!rho_collect_state_ptrs_into_map(map, cell->content_type))
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
        if (orig->value) {
            clone->value =
                rho_atom_rebind_resources(owner, orig->value, space_map, map);
            if (!clone->value)
                return false;
        }
        if (orig->content_type) {
            clone->content_type = rho_atom_rebind_resources(
                owner, orig->content_type, space_map, map);
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

typedef struct {
    Space **orig;
    Space **stable;
    uint64_t *export_owner_epoch;
    CettaCount len, cap;
} RhoPromotedSpaceMap;

typedef struct {
    StateCell **orig;
    StateCell **stable;
    uint64_t *export_owner_epoch;
    CettaCount len, cap;
} RhoPromotedStateMap;

static Space *rho_promoted_space_for(const RhoPromotedSpaceMap *map,
                                     Space *orig,
                                     uint64_t export_owner_epoch,
                                     bool *alias_fault) {
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

static StateCell *rho_promoted_state_for(const RhoPromotedStateMap *map,
                                         StateCell *orig,
                                         uint64_t export_owner_epoch,
                                         bool *alias_fault) {
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

static bool rho_promoted_space_bind(RhoPromotedSpaceMap *map,
                                    Space *orig,
                                    Space *stable,
                                    uint64_t export_owner_epoch,
                                    bool *alias_fault) {
    CettaCount next_cap;
    if (!map || !orig || !stable)
        return false;
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
    if (map->len >= map->cap) {
        next_cap = map->cap ? map->cap * 2u : 8u;
        map->orig = cetta_realloc(map->orig, sizeof(Space *) * (size_t)next_cap);
        map->stable =
            cetta_realloc(map->stable, sizeof(Space *) * (size_t)next_cap);
        map->export_owner_epoch =
            cetta_realloc(map->export_owner_epoch,
                          sizeof(uint64_t) * (size_t)next_cap);
        map->cap = next_cap;
    }
    map->orig[map->len] = orig;
    map->stable[map->len] = stable;
    map->export_owner_epoch[map->len] = export_owner_epoch;
    map->len++;
    return true;
}

static bool rho_promoted_state_bind(RhoPromotedStateMap *map,
                                    StateCell *orig,
                                    StateCell *stable,
                                    uint64_t export_owner_epoch,
                                    bool *alias_fault) {
    CettaCount next_cap;
    if (!map || !orig || !stable)
        return false;
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
    if (map->len >= map->cap) {
        next_cap = map->cap ? map->cap * 2u : 8u;
        map->orig =
            cetta_realloc(map->orig, sizeof(StateCell *) * (size_t)next_cap);
        map->stable =
            cetta_realloc(map->stable, sizeof(StateCell *) * (size_t)next_cap);
        map->export_owner_epoch =
            cetta_realloc(map->export_owner_epoch,
                          sizeof(uint64_t) * (size_t)next_cap);
        map->cap = next_cap;
    }
    map->orig[map->len] = orig;
    map->stable[map->len] = stable;
    map->export_owner_epoch[map->len] = export_owner_epoch;
    map->len++;
    return true;
}

static void rho_promoted_space_map_free(RhoPromotedSpaceMap *map) {
    if (!map)
        return;
    free(map->orig);
    free(map->stable);
    free(map->export_owner_epoch);
    map->orig = NULL;
    map->stable = NULL;
    map->export_owner_epoch = NULL;
    map->len = 0;
    map->cap = 0;
}

static void rho_promoted_state_map_free(RhoPromotedStateMap *map) {
    if (!map)
        return;
    free(map->orig);
    free(map->stable);
    free(map->export_owner_epoch);
    map->orig = NULL;
    map->stable = NULL;
    map->export_owner_epoch = NULL;
    map->len = 0;
    map->cap = 0;
}

static Atom *rho_promote_payload_atom(Arena *arena,
                                      Arena *persistent,
                                      Atom *atom,
                                      const RhoOverlayMap *space_map,
                                      const RhoStateCloneMap *state_map,
                                      RhoPromotedSpaceMap *promoted_spaces,
                                      RhoPromotedStateMap *promoted_states,
                                      uint64_t owner_epoch,
                                      uint64_t export_owner_epoch,
                                      bool *alias_fault);

static Space *rho_promote_payload_space(Arena *persistent,
                                        Space *space,
                                        const RhoOverlayMap *space_map,
                                        const RhoStateCloneMap *state_map,
                                        RhoPromotedSpaceMap *promoted_spaces,
                                        RhoPromotedStateMap *promoted_states,
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
    stable = rho_promoted_space_for(promoted_spaces, visible,
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
                                            RhoPromotedSpaceMap *promoted_spaces,
                                            RhoPromotedStateMap *promoted_states,
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
    stable = rho_promoted_state_for(promoted_states, visible,
                                    export_owner_epoch, alias_fault);
    if (stable)
        return stable;

    stable = arena_alloc(persistent, sizeof(StateCell));
    if (!stable)
        return NULL;
    stable->value = NULL;
    stable->content_type = NULL;
    stable->payload_owner_epoch = 0;
    stable->payload_export_owner_epoch = export_owner_epoch;
    if (!rho_promoted_state_bind(promoted_states, visible, stable,
                                 export_owner_epoch, alias_fault))
        return NULL;
    if (visible->value) {
        stable->value =
            rho_promote_payload_atom(persistent, persistent, visible->value,
                                     space_map, state_map,
                                     promoted_spaces, promoted_states,
                                     owner_epoch, export_owner_epoch,
                                     alias_fault);
        if (!stable->value)
            return NULL;
    }
    if (visible->content_type) {
        stable->content_type =
            rho_promote_payload_atom(persistent, persistent,
                                     visible->content_type,
                                     space_map, state_map,
                                     promoted_spaces, promoted_states,
                                     owner_epoch, export_owner_epoch,
                                     alias_fault);
        if (!stable->content_type)
            return NULL;
    }
    return stable;
}

static Atom *rho_promote_payload_atom(Arena *arena,
                                      Arena *persistent,
                                      Atom *atom,
                                      const RhoOverlayMap *space_map,
                                      const RhoStateCloneMap *state_map,
                                      RhoPromotedSpaceMap *promoted_spaces,
                                      RhoPromotedStateMap *promoted_states,
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
    RhoPromotedSpaceMap promoted_spaces = {0};
    RhoPromotedStateMap promoted_states = {0};
    Registry local_registry = {0};
    Arena *persistent = NULL;
    CettaLibraryContext *prev_library_context;
    Space *self_overlay;
    uint64_t owner_epoch = 0;
    Atom *payload;
    bool alias_fault = false;

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
            if (!rho_collect_space_ptrs_into_map(&map, cell->value))
                goto fail;
            if (!rho_collect_space_ptrs_into_map(&map, cell->content_type))
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
                result_set_free(out);
                result_set_init(out);
                result_set_add(out,
                               atom_error(arena,
                                          atom_symbol(arena, "rhometta:eval"),
                                          atom_symbol(arena,
                                                      "PayloadOwnedExportAliased")));
                break;
            }
            goto fail;
        }
    }

    rho_promoted_space_map_free(&promoted_spaces);
    rho_promoted_state_map_free(&promoted_states);
    rho_overlay_map_free(&map);
    rho_state_clone_map_free(&state_map);
    registry_free(&local_registry);
    eval_payload_redirects_end();
    return true;

fail:
    rho_promoted_space_map_free(&promoted_spaces);
    rho_promoted_state_map_free(&promoted_states);
    rho_overlay_map_free(&map);
    rho_state_clone_map_free(&state_map);
    registry_free(&local_registry);
    eval_payload_redirects_end();
    return false;
}

static Atom *rho_embed_payload_result_name(Arena *arena, Atom *result) {
    if (!arena || !result) return NULL;
    if (rhocalc_process_well_formed_with_eval_payloads(result)) {
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
        recv.kind != RHO_RECV || recv.nargs != 3 ||
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
        char *key;
        if (!result) {
            ok = false;
            break;
        }
        key = rho_key_proc(result);
        if (!key || !rho_successor_set_acc_push_keyed(out, result, key)) {
            free(key);
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
 * fires the key-least successor; on pure terms it streams candidate keys
 * against the best key so far (rho_pure_reaction_key) instead of
 * materializing and sorting the whole frontier.  The rotating scheduler
 * cycles through the frontier for fairness probes. */

static RhoRuntimeProfile rho_runtime_profile_default(uint32_t reduction_limit) {
    RhoRuntimeProfile profile;
    profile.scheduler_policy = RHO_SCHEDULER_CANONICAL;
    profile.reduction_limit = reduction_limit;
    profile.thread_count = 1u;
    profile.threaded = false;
    return profile;
}

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
    if (profile->scheduler_policy != RHO_SCHEDULER_CANONICAL &&
        profile->scheduler_policy != RHO_SCHEDULER_ROTATING) {
        rho_validation_set("rho scheduler policy is not implemented");
        return false;
    }
    return true;
}

static void rho_machine_init(RhoMachine *machine, Arena *arena,
                             const RhoRuntimeProfile *profile,
                             const RhocalcEvalContext *eval_context) {
    machine->arena = arena;
    machine->profile = *profile;
    machine->eval_context = eval_context;
    machine->current = NULL;
    machine->rotating_turn = 0u;
    rho_vec_init(&machine->components);
    rho_endpoint_vec_init(&machine->sends);
    rho_endpoint_vec_init(&machine->recvs);
    machine->comm_index_loaded = false;
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
    machine->arena = NULL;
    machine->eval_context = NULL;
    machine->current = NULL;
}

static bool rho_machine_load_process(RhoMachine *machine, Atom *proc) {
    bool ok = machine && machine->eval_context
        ? rhocalc_process_well_formed_with_eval_payloads(proc)
        : rhocalc_process_well_formed(proc);
    if (!ok) return false;
    machine->current = rho_normalize_proc(machine->arena, proc);
    return true;
}

typedef struct {
    const char *bound;
    size_t bound_len;
    size_t checked;
    int order;
} RhoKeyBound;

static void rho_key_bound_note(RhoKeyBound *cmp, const RhoStr *candidate) {
    if (!cmp || !candidate || !cmp->bound || cmp->order != 0) return;
    while (cmp->checked < candidate->len && cmp->checked < cmp->bound_len) {
        unsigned char lhs = (unsigned char)candidate->data[cmp->checked];
        unsigned char rhs = (unsigned char)cmp->bound[cmp->checked];
        if (lhs < rhs) {
            cmp->order = -1;
            return;
        }
        if (lhs > rhs) {
            cmp->order = 1;
            return;
        }
        cmp->checked++;
    }
    if (cmp->checked == cmp->bound_len &&
        cmp->checked < candidate->len) {
        cmp->order = 1;
    }
}

/* Build the exact key rho_rebuild_reaction followed by rho_key_proc would
 * produce, but without first materializing and sorting an entire residual.
 * The base components are already in rho_par_from_vec order.  Body components
 * are sorted with the same key/ordinal comparator and merged into that base.
 * Once a candidate is known to exceed bound, construction stops early. */
static bool rho_pure_reaction_key(
    RhoAtomVec *components, char **component_keys,
    uint32_t send_index, uint32_t recv_index, Atom *body,
    const char *bound, char **out_key, int *out_order) {
    RhoKeyedAtom *body_items = NULL;
    uint32_t body_len = 0u;
    uint32_t base_pos = 0u;
    uint32_t body_pos = 0u;
    uint32_t emitted = 0u;
    uint32_t total;
    RhoAlphaEnv env;
    RhoStr key = {0};
    RhoKeyBound cmp = {
        .bound = bound,
        .bound_len = bound ? strlen(bound) : 0u,
        .checked = 0u,
        .order = bound ? 0 : -1,
    };
    RhoView body_view;
    bool ok = true;

    if (!components || !component_keys || !body || !out_key || !out_order ||
        send_index >= components->len || recv_index >= components->len ||
        send_index == recv_index) {
        return false;
    }
    *out_key = NULL;
    *out_order = 0;

    body_view = rho_view(body);
    if (body_view.kind == RHO_PAR) {
        body_len = body_view.nargs;
    } else if (body_view.kind != RHO_NIL) {
        body_len = 1u;
    }
    if (body_len > 0u) {
        body_items = cetta_malloc(sizeof(RhoKeyedAtom) * body_len);
        if (!body_items) return false;
        for (uint32_t i = 0u; i < body_len; i++) {
            Atom *item = body_view.kind == RHO_PAR ? body_view.args[i] : body;
            body_items[i].atom = item;
            body_items[i].key = rho_key_proc(item);
            body_items[i].ordinal = i;
            if (!body_items[i].key) {
                body_len = i;
                ok = false;
                goto done;
            }
        }
        if (body_len > 1u) {
            qsort(body_items, body_len, sizeof(RhoKeyedAtom),
                  rho_keyed_atom_cmp);
        }
    }

    total = components->len - 2u + body_len;
    rho_alpha_init(&env);
    if (total == 0u) {
        ok = rho_str_append(&key, "0");
        rho_key_bound_note(&cmp, &key);
    } else {
        if (total > 1u) {
            ok = rho_str_append(&key, "par(");
            rho_key_bound_note(&cmp, &key);
        }
        while (emitted < total && ok && cmp.order <= 0) {
            while (base_pos < components->len &&
                   (base_pos == send_index || base_pos == recv_index)) {
                base_pos++;
            }
            Atom *next;
            if (base_pos >= components->len) {
                next = body_items[body_pos++].atom;
            } else if (body_pos >= body_len ||
                       strcmp(component_keys[base_pos],
                              body_items[body_pos].key) <= 0) {
                next = components->items[base_pos++];
            } else {
                next = body_items[body_pos++].atom;
            }
            if (total > 1u && emitted > 0u) {
                ok = rho_str_append(&key, "|");
            }
            if (ok) {
                rho_key_proc_into(next, &env, &key);
                ok = key.data != NULL;
            }
            if (ok) rho_key_bound_note(&cmp, &key);
            emitted++;
        }
        if (ok && cmp.order <= 0 && total > 1u) {
            ok = rho_str_append(&key, ")");
            rho_key_bound_note(&cmp, &key);
        }
        if (ok && cmp.order == 0 && key.len < cmp.bound_len) {
            cmp.order = -1;
        }
        rho_alpha_free(&env);
    }

done:
    for (uint32_t i = 0u; i < body_len; i++) free(body_items[i].key);
    free(body_items);
    if (!ok) {
        free(key.data);
        return false;
    }
    *out_key = key.data;
    *out_order = cmp.order;
    return true;
}

/* Canonical execution needs the least successor, whereas the public frontier
 * API must retain every successor.  On pure rho terms, compare candidates in
 * a resettable arena and materialize only the selected successor persistently.
 * Equal keys retain the original recv/send/body enumeration order, matching
 * rho_successor_set_acc_finish exactly. */
static bool rho_machine_select_pure_canonical_successor(
    RhoMachine *machine, Atom **out_next, bool *out_quiescent) {
    RhoAtomVec components;
    RhoEndpointVec sends;
    RhoEndpointVec recvs;
    uint32_t recv_pos = 0u;
    uint32_t send_pos = 0u;
    uint32_t best_recv = 0u;
    uint32_t best_send = 0u;
    uint32_t best_body = 0u;
    uint64_t ordinal_base = 0u;
    uint64_t best_ordinal = UINT64_MAX;
    char **component_keys = NULL;
    char *best_key = NULL;
    bool found = false;
    bool ok = true;

    if (!machine || !machine->arena || !machine->current ||
        !out_next || !out_quiescent || machine->eval_context) {
        return false;
    }

    *out_next = NULL;
    *out_quiescent = true;
    rho_vec_init(&components);
    rho_endpoint_vec_init(&sends);
    rho_endpoint_vec_init(&recvs);
    rho_collect_par(machine->arena, machine->current, &components);
    component_keys = cetta_malloc(sizeof(char *) * components.len);
    if (!component_keys && components.len > 0u) {
        ok = false;
        goto done;
    }
    if (component_keys) {
        memset(component_keys, 0, sizeof(char *) * components.len);
    }
    for (uint32_t i = 0u; i < components.len; i++) {
        component_keys[i] = rho_key_proc(components.items[i]);
        if (!component_keys[i] ||
            (i > 0u && strcmp(component_keys[i - 1u],
                              component_keys[i]) > 0)) {
            ok = false;
            goto done;
        }
    }
    if (!rho_collect_endpoints(&components, &sends, &recvs)) {
        ok = false;
        goto done;
    }

    while (recv_pos < recvs.len && send_pos < sends.len && ok) {
        int cmp = strcmp(recvs.items[recv_pos].key,
                         sends.items[send_pos].key);
        if (cmp < 0) {
            recv_pos++;
            continue;
        }
        if (cmp > 0) {
            send_pos++;
            continue;
        }

        uint32_t recv_start = recv_pos;
        uint32_t send_start = send_pos;
        while (recv_pos < recvs.len &&
               strcmp(recvs.items[recv_pos].key,
                      recvs.items[recv_start].key) == 0) {
            recv_pos++;
        }
        while (send_pos < sends.len &&
               strcmp(sends.items[send_pos].key,
                      sends.items[send_start].key) == 0) {
            send_pos++;
        }

        uint32_t send_count = send_pos - send_start;
        for (uint32_t r = recv_pos; r-- > recv_start && ok;) {
            for (uint32_t s = send_pos; s-- > send_start && ok;) {
                ArenaMark mark = arena_mark(machine->arena);
                RhoAtomVec bodies;
                uint64_t ordinal =
                    ordinal_base +
                    (uint64_t)(r - recv_start) * send_count +
                    (uint64_t)(s - send_start);
                rho_vec_init(&bodies);
                if (!rho_compute_comm_continuations(
                        machine->arena, &sends.items[s], &recvs.items[r],
                        NULL, &bodies)) {
                    ok = false;
                }
                if (bodies.len > 1u) {
                    ok = false;
                }
                for (uint32_t b = 0u; b < bodies.len && ok; b++) {
                    char *key = NULL;
                    int order = 0;
                    if (!rho_pure_reaction_key(
                            &components, component_keys,
                            sends.items[s].component_index,
                            recvs.items[r].component_index, bodies.items[b],
                            best_key, &key, &order)) {
                        ok = false;
                        break;
                    }
                    if (!found || order < 0 ||
                        (order == 0 && ordinal < best_ordinal)) {
                        free(best_key);
                        best_key = key;
                        best_recv = r;
                        best_send = s;
                        best_body = b;
                        best_ordinal = ordinal;
                        found = true;
                    } else {
                        free(key);
                    }
                }
                rho_vec_free(&bodies);
                arena_reset(machine->arena, mark);
            }
        }
        ordinal_base +=
            (uint64_t)(recv_pos - recv_start) *
            (uint64_t)(send_pos - send_start);
    }

    if (ok && found) {
        RhoAtomVec bodies;
        rho_vec_init(&bodies);
        if (!rho_compute_comm_continuations(
                machine->arena, &sends.items[best_send],
                &recvs.items[best_recv], NULL, &bodies) ||
            best_body >= bodies.len) {
            ok = false;
        } else {
            *out_next = rho_rebuild_reaction(
                machine->arena, &components,
                sends.items[best_send].component_index,
                recvs.items[best_recv].component_index,
                bodies.items[best_body]);
            ok = *out_next != NULL;
            *out_quiescent = false;
        }
        rho_vec_free(&bodies);
    } else if (ok) {
        *out_next = machine->current;
    }

done:
    if (component_keys) {
        for (uint32_t i = 0u; i < components.len; i++) {
            free(component_keys[i]);
        }
    }
    free(component_keys);
    free(best_key);
    rho_endpoint_vec_free(&sends);
    rho_endpoint_vec_free(&recvs);
    rho_vec_free(&components);
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

    if (!machine->eval_context) {
        return rho_machine_select_pure_canonical_successor(
            machine, out_next, out_quiescent);
    }

    if (!rhocalc_collect_successor_set(machine->arena, machine->current,
                                       machine->eval_context, &successors)) {
        return false;
    }
    *out_quiescent = successors.len == 0;
    *out_next = successors.len == 0 ? machine->current : successors.items[0];
    rhocalc_successor_set_free(&successors);
    return true;
}

/* ── Threaded strict-core executor ──────────────────────────────────────────
 *
 * Worker threads decompose tasks into components and publish send/receive
 * endpoints into per-channel buckets.  A bucket-locked rendezvous pairs one
 * send with one receive, spends one unit of the shared reduction budget,
 * and enqueues one continuation; see the run invariant at RhoAsyncExecutor
 * above.  The final state is unmatched endpoints plus stuck residuals. */

static void rho_async_fail(RhoAsyncExecutor *executor, const char *fmt, ...) {
    va_list ap;
    char message[256];

    if (!executor) return;
    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);
    cetta_parallel_executor_fail(&executor->parallel, "%s", message);
}

static void rho_channel_table_init(RhoChannelTable *table) {
    pthread_mutex_init(&table->mutex, NULL);
    table->head = NULL;
}

static void rho_async_endpoint_free(RhoAsyncEndpoint *endpoint) {
    free(endpoint);
}

static void rho_async_endpoint_list_free(RhoAsyncEndpoint *endpoint) {
    while (endpoint) {
        RhoAsyncEndpoint *next = endpoint->next;
        rho_async_endpoint_free(endpoint);
        endpoint = next;
    }
}

static void rho_channel_table_free(RhoChannelTable *table) {
    RhoChannelBucket *bucket = table->head;
    while (bucket) {
        RhoChannelBucket *next = bucket->next;
        rho_async_endpoint_list_free(bucket->send_head);
        rho_async_endpoint_list_free(bucket->recv_head);
        pthread_mutex_destroy(&bucket->mutex);
        free(bucket->key);
        free(bucket);
        bucket = next;
    }
    pthread_mutex_destroy(&table->mutex);
    table->head = NULL;
}

static RhoChannelBucket *rho_channel_table_get_or_create(RhoChannelTable *table,
                                                         const char *key) {
    RhoChannelBucket *bucket;

    pthread_mutex_lock(&table->mutex);
    for (bucket = table->head; bucket; bucket = bucket->next) {
        if (strcmp(bucket->key, key) == 0) {
            pthread_mutex_unlock(&table->mutex);
            return bucket;
        }
    }

    bucket = cetta_malloc(sizeof(RhoChannelBucket));
    bucket->key = rho_heap_strdup(key);
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

static void rho_async_endpoint_append(RhoAsyncEndpoint **head,
                                      RhoAsyncEndpoint **tail,
                                      RhoAsyncEndpoint *endpoint) {
    endpoint->next = NULL;
    if (*tail) {
        (*tail)->next = endpoint;
    } else {
        *head = endpoint;
    }
    *tail = endpoint;
}

static RhoAsyncEndpoint *rho_async_endpoint_pop(RhoAsyncEndpoint **head,
                                                RhoAsyncEndpoint **tail) {
    RhoAsyncEndpoint *endpoint = *head;
    if (!endpoint) return NULL;
    *head = endpoint->next;
    if (!*head) *tail = NULL;
    endpoint->next = NULL;
    return endpoint;
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

static bool rho_async_enqueue_proc(RhoAsyncExecutor *executor, Atom *proc) {
    if (!executor || !proc) return false;
    return cetta_parallel_executor_push(&executor->parallel, proc);
}

static bool rho_async_add_residual(RhoAsyncExecutor *executor, Atom *proc) {
    bool ok;
    pthread_mutex_lock(&executor->residual_mutex);
    ok = rho_vec_push(&executor->residuals, proc);
    pthread_mutex_unlock(&executor->residual_mutex);
    if (!ok) rho_async_fail(executor, "could not record rho residual");
    return ok;
}

static bool rho_async_publish_endpoint(RhoAsyncExecutor *executor,
                                       Arena *worker_arena,
                                       RhoAsyncEndpointKind kind,
                                       Atom *proc,
                                       RhoView view) {
    char *key = rho_key_name(view.args[0]);
    RhoChannelBucket *bucket =
        rho_channel_table_get_or_create(&executor->channels, key);
    RhoAsyncEndpoint *current = cetta_malloc(sizeof(RhoAsyncEndpoint));
    RhoAsyncEndpoint *opposite = NULL;
    bool fire = false;

    current->kind = kind;
    current->atom = proc;
    current->view = view;
    current->next = NULL;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_RHO_ASYNC_ENDPOINT_PUBLISH);

    pthread_mutex_lock(&bucket->mutex);
    if (kind == RHO_ASYNC_ENDPOINT_SEND) {
        if (bucket->recv_head && rho_async_try_spend_budget(executor)) {
            opposite = rho_async_endpoint_pop(&bucket->recv_head,
                                              &bucket->recv_tail);
            fire = true;
        } else {
            rho_async_endpoint_append(&bucket->send_head, &bucket->send_tail,
                                      current);
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_RHO_ASYNC_ENDPOINT_QUEUED);
        }
    } else {
        if (bucket->send_head && rho_async_try_spend_budget(executor)) {
            opposite = rho_async_endpoint_pop(&bucket->send_head,
                                              &bucket->send_tail);
            fire = true;
        } else {
            rho_async_endpoint_append(&bucket->recv_head, &bucket->recv_tail,
                                      current);
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_RHO_ASYNC_ENDPOINT_QUEUED);
        }
    }
    pthread_mutex_unlock(&bucket->mutex);
    free(key);

    if (!fire) return true;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_RHO_ASYNC_ENDPOINT_MATCH);

    {
        RhoEndpoint send_endpoint;
        RhoEndpoint recv_endpoint;
        RhoAtomVec bodies;
        bool ok;
        uint32_t chosen_body = 0u;
        RhoAsyncEndpoint *send_async;
        RhoAsyncEndpoint *recv_async;

        if (kind == RHO_ASYNC_ENDPOINT_SEND) {
            send_endpoint = (RhoEndpoint){0, current->view, NULL};
            recv_endpoint = (RhoEndpoint){0, opposite->view, NULL};
            send_async = current;
            recv_async = opposite;
        } else {
            send_endpoint = (RhoEndpoint){0, opposite->view, NULL};
            recv_endpoint = (RhoEndpoint){0, current->view, NULL};
            send_async = opposite;
            recv_async = current;
        }

        rho_vec_init(&bodies);
        ok = rho_compute_comm_continuations(worker_arena,
                                            &send_endpoint,
                                            &recv_endpoint,
                                            executor->eval_context,
                                            &bodies);
        if (!ok) {
            rho_vec_free(&bodies);
            rho_async_endpoint_free(opposite);
            rho_async_endpoint_free(current);
            rho_async_fail(executor, "could not compute rho COMM continuation");
            return false;
        }
        if (bodies.len == 0) {
            rho_async_refund_budget(executor);
            if (!rho_async_add_residual(executor, send_async->atom) ||
                !rho_async_add_residual(executor, recv_async->atom)) {
                rho_vec_free(&bodies);
                rho_async_endpoint_free(opposite);
                rho_async_endpoint_free(current);
                return false;
            }
            rho_async_endpoint_free(opposite);
            rho_async_endpoint_free(current);
            rho_vec_free(&bodies);
            return true;
        }
        rho_async_endpoint_free(opposite);
        rho_async_endpoint_free(current);
        if (executor->profile.scheduler_policy == RHO_SCHEDULER_ROTATING &&
            bodies.len > 1u) {
            chosen_body =
                atomic_fetch_add_explicit(&executor->branch_turn, 1u,
                                          memory_order_relaxed) % bodies.len;
        }
        if (!rho_async_enqueue_proc(executor, bodies.items[chosen_body])) {
            rho_vec_free(&bodies);
            rho_async_fail(executor, "could not enqueue rho COMM continuation");
            return false;
        }
        rho_vec_free(&bodies);
    }

    return true;
}

static bool rho_async_process_task(CettaParallelWorker *worker, void *task,
                                   void *user) {
    RhoAsyncExecutor *executor = user;
    Arena *worker_arena = cetta_parallel_worker_arena(worker);
    Atom *proc = task;
    Atom *norm = rho_normalize_proc(worker_arena, proc);
    RhoView view = rho_view(norm);

    switch (view.kind) {
    case RHO_NIL:
        return true;
    case RHO_PAR:
        for (uint32_t i = 0; i < view.nargs; i++) {
            if (!rho_async_enqueue_proc(executor, view.args[i])) {
                rho_async_fail(executor, "could not enqueue rho parallel component");
                return false;
            }
        }
        return true;
    case RHO_SEND:
        return rho_async_publish_endpoint(executor, worker_arena,
                                          RHO_ASYNC_ENDPOINT_SEND,
                                          norm, view);
    case RHO_RECV:
        return rho_async_publish_endpoint(executor, worker_arena,
                                          RHO_ASYNC_ENDPOINT_RECV,
                                          norm, view);
    case RHO_DROP:
    case RHO_VAL:
    case RHO_EVAL_PAYLOAD:
        return rho_async_add_residual(executor, norm);
    case RHO_QUOTE:
    case RHO_BAD:
        break;
    }
    rho_async_fail(executor, "unsupported rho task during threaded reduction");
    return false;
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

static bool rho_async_materialize_residual(RhoAsyncExecutor *executor,
                                           Atom **out) {
    RhoAtomVec items;
    bool ok = true;

    rho_vec_init(&items);
    for (uint32_t i = 0; i < executor->residuals.len; i++) {
        if (!rho_vec_push(&items,
                          atom_deep_copy(executor->global_arena,
                                         executor->residuals.items[i]))) {
            ok = false;
            break;
        }
    }

    pthread_mutex_lock(&executor->channels.mutex);
    for (RhoChannelBucket *bucket = executor->channels.head;
         ok && bucket;
         bucket = bucket->next) {
        pthread_mutex_lock(&bucket->mutex);
        for (RhoAsyncEndpoint *ep = bucket->send_head; ok && ep; ep = ep->next) {
            ok = rho_vec_push(&items,
                              atom_deep_copy(executor->global_arena, ep->atom));
        }
        for (RhoAsyncEndpoint *ep = bucket->recv_head; ok && ep; ep = ep->next) {
            ok = rho_vec_push(&items,
                              atom_deep_copy(executor->global_arena, ep->atom));
        }
        pthread_mutex_unlock(&bucket->mutex);
    }
    pthread_mutex_unlock(&executor->channels.mutex);

    if (ok) {
        *out = rho_par_from_vec(executor->global_arena, &items);
        ok = *out != NULL;
    }
    rho_vec_free(&items);
    return ok;
}

static void rho_async_executor_init(RhoAsyncExecutor *executor,
                                    Arena *global_arena,
                                    const RhoRuntimeProfile *profile,
                                    const RhocalcEvalContext *eval_context) {
    CettaParallelExecutorConfig parallel_config;

    executor->global_arena = global_arena;
    executor->profile = *profile;
    executor->eval_context = eval_context;
    rho_channel_table_init(&executor->channels);
    rho_vec_init(&executor->residuals);
    pthread_mutex_init(&executor->residual_mutex, NULL);
    atomic_init(&executor->budget, profile->reduction_limit);
    atomic_init(&executor->reductions_taken, 0u);
    atomic_init(&executor->branch_turn, 0u);

    parallel_config = (CettaParallelExecutorConfig){
        .thread_count = profile->thread_count,
        .user = executor,
        .task_fn = rho_async_process_task,
        .worker_enter = rho_async_worker_enter,
        .worker_leave = rho_async_worker_leave,
        .worker_failure_message = "rho threaded worker failed",
    };
    cetta_parallel_executor_init(&executor->parallel, &parallel_config);
}

static void rho_async_executor_free(RhoAsyncExecutor *executor) {
    cetta_parallel_executor_free(&executor->parallel);
    rho_vec_free(&executor->residuals);
    rho_channel_table_free(&executor->channels);
    pthread_mutex_destroy(&executor->residual_mutex);
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
    if (!(eval_context
              ? rhocalc_process_well_formed_with_eval_payloads(proc)
              : rhocalc_process_well_formed(proc))) {
        return false;
    }

    rho_async_executor_init(&executor, arena, profile, eval_context);

    if (!rho_async_enqueue_proc(&executor, proc)) {
        rho_async_fail(&executor, "could not enqueue initial rho task");
        ok = false;
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
 * quiescent outcomes.  When (a) every channel key carries at most one send
 * and one receive endpoint, so no alternative pairing exists, and (b) every
 * continuation produced by firing the enabled pairs is QUIET -- it contains
 * no send, receive, or drop anywhere, so no firing order can create a new
 * rendezvous on any key -- AND (C3) firing the deferred payloads cannot
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
 * inert in this strict core (it only unquotes under matched-COMM substitution,
 * see semanticCommSubst), so excluding drop in continuations is sufficient. */
static bool rho_proc_is_quiet(Atom *proc) {
    RhoView view;
    if (!proc)
        return true;
    if (proc->kind != ATOM_EXPR)
        return true;
    view = rho_view(proc);
    if (view.kind == RHO_SEND || view.kind == RHO_RECV ||
        view.kind == RHO_DROP) {
        return false;
    }
    for (CettaExprIndex i = 0; i < proc->expr.len; i++) {
        if (!rho_proc_is_quiet(proc->expr.elems[i]))
            return false;
    }
    return true;
}

/* C3 support: a grounded value is identity-bearing (not freely copyable) when
 * it carries pointer identity -- a space, state cell, capture, or foreign
 * handle.  Mirrors hyperpose_atom_is_thread_local_resource (eval.c). */
static bool rho_gkind_is_identity(GroundedKind k) {
    return k == GV_SPACE || k == GV_STATE || k == GV_CAPTURE || k == GV_FOREIGN;
}

/* Post-result copy-stability: does this result tree carry any identity-bearing
 * grounded value (which the macro would duplicate across product children)? */
static bool rho_atom_has_identity_grounded(Atom *atom) {
    if (!atom)
        return false;
    if (atom->kind == ATOM_GROUNDED)
        return rho_gkind_is_identity(atom->ground.gkind);
    if (atom->kind != ATOM_EXPR)
        return false;
    for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
        if (rho_atom_has_identity_grounded(atom->expr.elems[i]))
            return true;
    }
    return false;
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

    /* Partition keywise: macro-fire eligible single-pair keys, exact-explore
     * contended or otherwise ineligible keys in the same successor round. */
    groups = cetta_malloc(sizeof(RhoMacroGroup) *
                          (sends.len < recvs.len ? sends.len + 1
                                                 : recvs.len + 1));
    if (!groups) {
        ok = false;
        goto done;
    }
    while (recv_pos < recvs.len && send_pos < sends.len) {
        int cmp = strcmp(recvs.items[recv_pos].key, sends.items[send_pos].key);
        if (cmp < 0) {
            recv_pos++;
            continue;
        }
        if (cmp > 0) {
            send_pos++;
            continue;
        }

        uint32_t recv_start = recv_pos;
        uint32_t send_start = send_pos;
        RhoMacroGroup *group = &groups[ngroups++];

        memset(group, 0, sizeof(*group));
        rho_vec_init(&group->bodies);
        group->recv_start = recv_start;
        group->send_start = send_start;
        while (recv_pos < recvs.len &&
               strcmp(recvs.items[recv_pos].key, recvs.items[recv_start].key) == 0) {
            recv_pos++;
        }
        while (send_pos < sends.len &&
               strcmp(sends.items[send_pos].key, sends.items[send_start].key) == 0) {
            send_pos++;
        }
        group->recv_end = recv_pos;
        group->send_end = send_pos;

        if ((recv_pos - recv_start) != 1u || (send_pos - send_start) != 1u) {
            group->reject_reason = RHO_MACRO_REJECT_CONTENTION;
            exact_groups++;
            continue;
        }

        group->send = &sends.items[send_start];
        group->recv = &recvs.items[recv_start];
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
        char *key;
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
        key = child_proc ? rho_key_proc(child_proc) : NULL;
        if (!child_proc || !key ||
            !rho_successor_set_acc_push_keyed(out, child_proc, key)) {
            free(key);
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

static bool rhocalc_collect_quiescent_set(
    Arena *arena, Atom *proc, const RhocalcEvalContext *eval_context,
    RhoSuccessorSet *out) {
    RhoAtomVec worklist;
    RhoSuccessorSetAcc seen;
    RhoSuccessorSetAcc quiescent;
    Atom *start;
    char *start_key;
    uint32_t head = 0;

    if (!arena || !proc || !out) return false;
    out->items = NULL;
    out->len = 0;

    rho_vec_init(&worklist);
    rho_successor_set_acc_init(&seen);
    rho_successor_set_acc_init(&quiescent);

    start = rho_normalize_proc(arena, proc);
    start_key = rho_key_proc(start);
    if (!start_key || !rho_vec_push(&worklist, start) ||
        !rho_successor_set_acc_push_keyed(&seen, start, start_key)) {
        free(start_key);
        rho_vec_free(&worklist);
        rho_successor_set_acc_free(&seen);
        rho_successor_set_acc_free(&quiescent);
        return false;
    }

    while (head < worklist.len) {
        Atom *current = worklist.items[head++];
        RhoSuccessorSet successors = {0};
        RhoQuietMacroMode macro_mode = RHO_QUIET_MACRO_MODE_BAIL_EXACT;

        {
            RhoSuccessorSetAcc macro_acc;
            rho_successor_set_acc_init(&macro_acc);
            if (!rhocalc_try_quiet_macro_step(arena, current, eval_context,
                                              &macro_mode, &macro_acc)) {
                rho_successor_set_acc_free(&macro_acc);
                rho_vec_free(&worklist);
                rho_successor_set_acc_free(&seen);
                rho_successor_set_acc_free(&quiescent);
                return false;
            }
            if (macro_mode != RHO_QUIET_MACRO_MODE_BAIL_EXACT) {
                rho_successor_set_acc_finish(&macro_acc, &successors);
            } else {
                rho_successor_set_acc_free(&macro_acc);
            }
        }

        if (macro_mode == RHO_QUIET_MACRO_MODE_BAIL_EXACT &&
            !rhocalc_collect_successor_set(arena, current, eval_context,
                                           &successors)) {
            rho_vec_free(&worklist);
            rho_successor_set_acc_free(&seen);
            rho_successor_set_acc_free(&quiescent);
            return false;
        }
        if (successors.len == 0) {
            char *key = rho_key_proc(current);
            if (!key || !rho_successor_set_acc_push_keyed(&quiescent, current,
                                                          key)) {
                free(key);
                rhocalc_successor_set_free(&successors);
                rho_vec_free(&worklist);
                rho_successor_set_acc_free(&seen);
                rho_successor_set_acc_free(&quiescent);
                return false;
            }
            rhocalc_successor_set_free(&successors);
            continue;
        }

        for (uint32_t i = 0; i < successors.len; i++) {
            Atom *next = successors.items[i];
            char *key = rho_key_proc(next);
            if (!key) {
                rhocalc_successor_set_free(&successors);
                rho_vec_free(&worklist);
                rho_successor_set_acc_free(&seen);
                rho_successor_set_acc_free(&quiescent);
                return false;
            }
            if (rho_successor_set_acc_contains_key(&seen, key)) {
                free(key);
                continue;
            }
            if (!rho_vec_push(&worklist, next) ||
                !rho_successor_set_acc_push_keyed(&seen, next, key)) {
                free(key);
                rhocalc_successor_set_free(&successors);
                rho_vec_free(&worklist);
                rho_successor_set_acc_free(&seen);
                rho_successor_set_acc_free(&quiescent);
                return false;
            }
        }
        rhocalc_successor_set_free(&successors);
    }

    rho_vec_free(&worklist);
    rho_successor_set_acc_free(&seen);
    rho_successor_set_acc_finish(&quiescent, out);
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
 * surface), rho:cost:stack-empty / rho:cost:stack-cons, and signature
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
    Atom *surface;
    char *surface_key;
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
    char *channel_key;
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
    char *channel_key;
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
    char **keys;
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
    char *key;
    uint32_t ordinal;
} RhoCostKeyedStep;

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
    char *key;
    uint32_t ordinal;
} RhoCostKeyedTraceComponent;

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
    char *key;
    uint32_t ordinal;
} RhoCostKeyedRuntimeComponent;

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

static Atom *rhocost_purse(Arena *arena, Atom *surface, Atom *stack) {
    return rho_binary(arena, "rho:cost:purse", surface, stack);
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

static int rhocost_keyed_step_cmp(const void *lhs, const void *rhs) {
    const RhoCostKeyedStep *a = lhs;
    const RhoCostKeyedStep *b = rhs;
    int cmp = strcmp(a->key, b->key);
    if (cmp != 0) return cmp;
    if (a->ordinal < b->ordinal) return -1;
    if (a->ordinal > b->ordinal) return 1;
    return 0;
}

static int rhocost_keyed_trace_component_cmp(const void *lhs,
                                             const void *rhs) {
    const RhoCostKeyedTraceComponent *a = lhs;
    const RhoCostKeyedTraceComponent *b = rhs;
    int cmp = strcmp(a->key, b->key);
    if (cmp != 0) return cmp;
    if (a->ordinal < b->ordinal) return -1;
    if (a->ordinal > b->ordinal) return 1;
    return 0;
}

static int rhocost_keyed_runtime_component_cmp(const void *lhs,
                                               const void *rhs) {
    const RhoCostKeyedRuntimeComponent *a = lhs;
    const RhoCostKeyedRuntimeComponent *b = rhs;
    int cmp = strcmp(a->key, b->key);
    if (cmp != 0) return cmp;
    if (a->ordinal < b->ordinal) return -1;
    if (a->ordinal > b->ordinal) return 1;
    return 0;
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
    if (vec->len == vec->cap) {
        uint32_t next_cap = vec->cap ? vec->cap * 2u : 8u;
        uint32_t *next =
            cetta_realloc(vec->items, sizeof(uint32_t) * next_cap);
        if (!next) return false;
        vec->items = next;
        vec->cap = next_cap;
    }
    vec->items[vec->len++] = value;
    return true;
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
    if (vec->len == vec->cap) {
        uint32_t next_cap = vec->cap ? vec->cap * 2u : 8u;
        RhoCostCoverFrame *next = cetta_realloc(
            vec->items, sizeof(RhoCostCoverFrame) * next_cap);
        if (!next) return false;
        vec->items = next;
        vec->cap = next_cap;
    }
    vec->items[vec->len++] = *frame;
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
    if (vec->len == vec->cap) {
        uint32_t next_cap = vec->cap ? vec->cap * 2u : 8u;
        RhoCostTraceComponent *next =
            cetta_realloc(vec->items,
                          sizeof(RhoCostTraceComponent) * next_cap);
        if (!next) return false;
        vec->items = next;
        vec->cap = next_cap;
    }
    vec->items[vec->len].term = term;
    vec->items[vec->len].has_producer = has_producer;
    vec->items[vec->len].producer = producer;
    vec->len++;
    return true;
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
    if (vec->len == vec->cap) {
        uint32_t next_cap = vec->cap ? vec->cap * 2u : 8u;
        uint64_t *next = cetta_realloc(
            vec->items, sizeof(uint64_t) * (size_t)next_cap);
        if (!next) return false;
        vec->items = next;
        vec->cap = next_cap;
    }
    vec->items[vec->len++] = producer;
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
    acc->items = NULL;
    acc->keys = NULL;
    acc->len = 0;
    acc->cap = 0;
    acc->retain_provenance = false;
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
    if (vec->len == vec->cap) {
        uint32_t next_cap = vec->cap ? vec->cap * 2u : 8u;
        RhoCostParallelTask *next = cetta_realloc(
            vec->items, sizeof(RhoCostParallelTask) * next_cap);
        if (!next) {
            free(resource_indices);
            free(token_indices);
            return false;
        }
        vec->items = next;
        vec->cap = next_cap;
    }
    task = &vec->items[vec->len++];
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
    if (acc->keys) {
        for (uint32_t i = 0; i < acc->len; i++) {
            free(acc->keys[i]);
        }
    }
    free(acc->items);
    free(acc->keys);
    acc->items = NULL;
    acc->keys = NULL;
    acc->len = 0;
    acc->cap = 0;
    acc->retain_provenance = false;
}

static bool rhocost_step_set_acc_grow(RhoCostStepSetAcc *acc) {
    uint32_t next_cap = acc->cap ? acc->cap * 2u : 8u;
    RhoCostStep *next_items =
        cetta_realloc(acc->items, sizeof(RhoCostStep) * next_cap);
    char **next_keys = cetta_realloc(acc->keys, sizeof(char *) * next_cap);
    if (!next_items || !next_keys) return false;
    acc->items = next_items;
    acc->keys = next_keys;
    acc->cap = next_cap;
    return true;
}

static bool rhocost_step_set_acc_push_keyed(RhoCostStepSetAcc *acc,
                                            Atom *result,
                                            Atom *consumed_sig,
                                            Atom *contractum,
                                            const uint32_t *participant_indices,
                                            uint32_t participant_len,
                                            RhoCostTokenVec *tokens,
                                            RhoIndexVec *chosen_tokens,
                                            char *key) {
    uint32_t *token_indices = NULL;
    if (participant_len > 2u) {
        free(key);
        return false;
    }
    if (acc->retain_provenance && chosen_tokens->len > 0) {
        token_indices = cetta_malloc(sizeof(uint32_t) * chosen_tokens->len);
        if (!token_indices) {
            free(key);
            return false;
        }
        for (uint32_t i = 0; i < chosen_tokens->len; i++) {
            token_indices[i] =
                tokens->items[chosen_tokens->items[i]].component_index;
        }
    }
    if (acc->len == acc->cap && !rhocost_step_set_acc_grow(acc)) {
        free(token_indices);
        free(key);
        return false;
    }
    acc->items[acc->len].result = result;
    acc->items[acc->len].consumed_sig = consumed_sig;
    acc->items[acc->len].contractum =
        acc->retain_provenance ? contractum : NULL;
    acc->items[acc->len].participant_len =
        acc->retain_provenance ? participant_len : 0;
    if (acc->retain_provenance) {
        for (uint32_t i = 0; i < participant_len; i++) {
            acc->items[acc->len].participant_indices[i] = participant_indices[i];
        }
    }
    acc->items[acc->len].token_indices = token_indices;
    acc->items[acc->len].token_len =
        acc->retain_provenance ? chosen_tokens->len : 0;
    acc->keys[acc->len] = key;
    acc->len++;
    return true;
}

static void rhocost_step_set_acc_finish(RhoCostStepSetAcc *acc,
                                        RhoCostStepSet *out) {
    if (acc->len > 1) {
        RhoCostKeyedStep *items =
            cetta_malloc(sizeof(RhoCostKeyedStep) * acc->len);
        for (uint32_t i = 0; i < acc->len; i++) {
            items[i].step = acc->items[i];
            items[i].key = acc->keys[i];
            items[i].ordinal = i;
        }
        qsort(items, acc->len, sizeof(RhoCostKeyedStep), rhocost_keyed_step_cmp);
        uint32_t write = 0;
        char *last_key = NULL;
        for (uint32_t i = 0; i < acc->len; i++) {
            if (last_key && strcmp(items[i].key, last_key) == 0) {
                rhocost_step_release(&items[i].step);
                free(items[i].key);
                continue;
            }
            if (last_key) free(last_key);
            acc->items[write++] = items[i].step;
            last_key = items[i].key;
        }
        if (last_key) free(last_key);
        acc->len = write;
        free(items);
    } else if (acc->len == 1) {
        free(acc->keys[0]);
    }
    free(acc->keys);
    out->items = acc->items;
    out->len = acc->len;
    acc->items = NULL;
    acc->keys = NULL;
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

static char *rhocost_key_signature(Atom *sig) {
    RhoAtomVec atoms;
    RhoStr out = {0};
    rho_vec_init(&atoms);
    if (!rhocost_collect_signature_atoms(sig, &atoms)) {
        rho_vec_free(&atoms);
        return rho_heap_strdup("?bad-sig");
    }
    rhocost_normalize_signature_vec(&atoms);
    (void)rho_str_append(&out, "sig(");
    for (uint32_t i = 0; i < atoms.len; i++) {
        const char *atom = atom_name_cstr(atoms.items[i]);
        if (i > 0) (void)rho_str_append(&out, ",");
        (void)rho_str_appendf(&out, "%zu:%s", strlen(atom), atom);
    }
    (void)rho_str_append(&out, ")");
    rho_vec_free(&atoms);
    return out.data ? out.data : rho_heap_strdup("sig()");
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
            rho_validation_set("rho:cost:purse expects surface and stack");
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
    for (uint32_t i = 0; i < vec->len; i++) {
        free(vec->items[i].surface_key);
    }
    free(vec->items);
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static bool rhocost_token_vec_push(RhoCostTokenVec *vec,
                                   uint32_t component_index,
                                   Atom *surface,
                                   char *surface_key,
                                   Atom *sig,
                                   Atom *rest) {
    if (vec->len == vec->cap) {
        uint32_t next_cap = vec->cap ? vec->cap * 2u : 8u;
        RhoCostToken *next =
            cetta_realloc(vec->items, sizeof(RhoCostToken) * next_cap);
        if (!next) return false;
        vec->items = next;
        vec->cap = next_cap;
    }
    vec->items[vec->len].component_index = component_index;
    vec->items[vec->len].surface = surface;
    vec->items[vec->len].surface_key = surface_key;
    vec->items[vec->len].sig = sig;
    vec->items[vec->len].rest = rest;
    vec->len++;
    return true;
}

static void rhocost_signed_endpoint_vec_init(RhoCostSignedEndpointVec *vec) {
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static void rhocost_signed_endpoint_vec_free(RhoCostSignedEndpointVec *vec) {
    for (uint32_t i = 0; i < vec->len; i++) {
        free(vec->items[i].channel_key);
    }
    free(vec->items);
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static bool rhocost_signed_endpoint_vec_push(RhoCostSignedEndpointVec *vec,
                                             uint32_t component_index,
                                             Atom *sig,
                                             RhoView view,
                                             char *channel_key) {
    if (vec->len == vec->cap) {
        uint32_t next_cap = vec->cap ? vec->cap * 2u : 8u;
        RhoCostSignedEndpoint *next =
            cetta_realloc(vec->items,
                          sizeof(RhoCostSignedEndpoint) * next_cap);
        if (!next) return false;
        vec->items = next;
        vec->cap = next_cap;
    }
    vec->items[vec->len].component_index = component_index;
    vec->items[vec->len].sig = sig;
    vec->items[vec->len].view = view;
    vec->items[vec->len].channel_key = channel_key;
    vec->len++;
    return true;
}

static void rhocost_whole_redex_vec_init(RhoCostWholeRedexVec *vec) {
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static void rhocost_whole_redex_vec_free(RhoCostWholeRedexVec *vec) {
    for (uint32_t i = 0; i < vec->len; i++) {
        free(vec->items[i].channel_key);
    }
    free(vec->items);
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static bool rhocost_whole_redex_vec_push(RhoCostWholeRedexVec *vec,
                                         uint32_t component_index,
                                         Atom *sig,
                                         RhoView recv,
                                         RhoView send,
                                         char *channel_key) {
    if (vec->len == vec->cap) {
        uint32_t next_cap = vec->cap ? vec->cap * 2u : 8u;
        RhoCostWholeRedex *next =
            cetta_realloc(vec->items,
                          sizeof(RhoCostWholeRedex) * next_cap);
        if (!next) return false;
        vec->items = next;
        vec->cap = next_cap;
    }
    vec->items[vec->len].component_index = component_index;
    vec->items[vec->len].sig = sig;
    vec->items[vec->len].recv = recv;
    vec->items[vec->len].send = send;
    vec->items[vec->len].channel_key = channel_key;
    vec->len++;
    return true;
}

/* ── Cost canonical keys and normalization ──────────────────────────────────
 *
 * The cost layer mirrors the strict-core key/normal-form discipline over
 * the extended grammar: term-par components sort under alpha-invariant
 * keys, signatures normalize to sorted multisets, and purse locations key
 * through the same name equivalence as channels. */

static Atom *rhocost_normalize_term(Arena *arena, Atom *term);

static void rhocost_key_name_into(Atom *name, RhoAlphaEnv *env, RhoStr *out);
static void rhocost_key_proc_into(Atom *proc, RhoAlphaEnv *env, RhoStr *out);
static void rhocost_key_term_into(Atom *term, RhoAlphaEnv *env, RhoStr *out);

static void rhocost_key_name_into(Atom *name, RhoAlphaEnv *env, RhoStr *out) {
    if (!name) {
        (void)rho_str_append(out, "?bad-name");
        return;
    }
    if (name->kind == ATOM_VAR) {
        rho_key_var_into(name, env, out);
        return;
    }
    {
        RhoView view = rho_view(name);
        if (view.kind == RHO_QUOTE && view.nargs == 1) {
            RhoCostTermView inner = rhocost_term_view(view.args[0]);
            if (inner.kind == RHOCOST_TERM_DROP && inner.nargs == 1) {
                rhocost_key_name_into(inner.args[0], env, out);
                return;
            }
            RhoAlphaEnv sealed;
            rho_alpha_init(&sealed);
            (void)rho_str_append(out, "@(");
            rhocost_key_term_into(view.args[0], &sealed, out);
            (void)rho_str_append(out, ")");
            rho_alpha_free(&sealed);
            return;
        }
    }
    if (rhocost_is_ground_signature(name) || rhocost_is_sig_mul(name)) {
        char *signature_key = rhocost_key_signature(name);
        (void)rho_str_append(out, signature_key);
        free(signature_key);
        return;
    }
    (void)rho_str_append(out, "?bad-name");
}

static void rhocost_key_proc_into(Atom *proc, RhoAlphaEnv *env, RhoStr *out) {
    RhoView view = rho_view(proc);
    switch (view.kind) {
    case RHO_NIL:
        (void)rho_str_append(out, "0");
        return;
    case RHO_PAR:
        (void)rho_str_append(out, "par(");
        for (uint32_t i = 0; i < view.nargs; i++) {
            if (i > 0) (void)rho_str_append(out, "|");
            rhocost_key_proc_into(view.args[i], env, out);
        }
        (void)rho_str_append(out, ")");
        return;
    case RHO_SEND:
        (void)rho_str_append(out, "send(");
        rhocost_key_name_into(view.args[0], env, out);
        (void)rho_str_append(out, ",");
        rhocost_key_term_into(view.args[1], env, out);
        (void)rho_str_append(out, ")");
        return;
    case RHO_RECV: {
        uint32_t mark = rho_alpha_mark(env);
        (void)rho_str_append(out, "recv(");
        rhocost_key_name_into(view.args[0], env, out);
        (void)rho_str_append(out, ",");
        if (view.nargs == 3 && view.args[1]->kind == ATOM_VAR) {
            (void)rho_alpha_push(env, view.args[1]->var_id);
            rhocost_key_term_into(view.args[2], env, out);
        } else {
            (void)rho_str_append(out, "?bad-binder");
        }
        rho_alpha_pop(env, mark);
        (void)rho_str_append(out, ")");
        return;
    }
    case RHO_DROP:
        (void)rho_str_append(out, "drop(");
        rhocost_key_name_into(view.args[0], env, out);
        (void)rho_str_append(out, ")");
        return;
    case RHO_VAL:
    case RHO_EVAL_PAYLOAD:
    case RHO_QUOTE:
    case RHO_BAD:
        (void)rho_str_append(out, "?bad-proc");
        return;
    }
}

static void rhocost_key_term_into(Atom *term, RhoAlphaEnv *env, RhoStr *out) {
    RhoCostTermView view = rhocost_term_view(term);
    switch (view.kind) {
    case RHOCOST_TERM_NIL:
        (void)rho_str_append(out, "tnil");
        return;
    case RHOCOST_TERM_STACK_EMPTY:
        (void)rho_str_append(out, "sempty");
        return;
    case RHOCOST_TERM_STACK_CONS: {
        char *signature_key = rhocost_key_signature(view.args[0]);
        (void)rho_str_appendf(out, "stack(%s:", signature_key);
        free(signature_key);
        rhocost_key_term_into(view.args[1], env, out);
        (void)rho_str_append(out, ")");
        return;
    }
    case RHOCOST_TERM_DROP:
        (void)rho_str_append(out, "drop(");
        rhocost_key_name_into(view.args[0], env, out);
        (void)rho_str_append(out, ")");
        return;
    case RHOCOST_TERM_PURSE:
        (void)rho_str_append(out, "purse(");
        rhocost_key_name_into(view.args[0], env, out);
        (void)rho_str_append(out, ",");
        rhocost_key_term_into(view.args[1], env, out);
        (void)rho_str_append(out, ")");
        return;
    case RHOCOST_TERM_SIGNED: {
        char *signature_key = rhocost_key_signature(view.args[1]);
        (void)rho_str_append(out, "signed(");
        rhocost_key_proc_into(view.args[0], env, out);
        (void)rho_str_appendf(out, ",%s)", signature_key);
        free(signature_key);
        return;
    }
    case RHOCOST_TERM_PAR:
        (void)rho_str_append(out, "tpar(");
        for (uint32_t i = 0; i < view.nargs; i++) {
            if (i > 0) (void)rho_str_append(out, "|");
            rhocost_key_term_into(view.args[i], env, out);
        }
        (void)rho_str_append(out, ")");
        return;
    case RHOCOST_TERM_BAD:
        (void)rho_str_append(out, "?bad-term");
        return;
    }
}

static char *rhocost_key_name(Atom *name) {
    RhoAlphaEnv env;
    RhoStr out = {0};
    rho_alpha_init(&env);
    rhocost_key_name_into(name, &env, &out);
    rho_alpha_free(&env);
    return out.data ? out.data : rho_heap_strdup("");
}

static char *rhocost_key_proc(Atom *proc) {
    RhoAlphaEnv env;
    RhoStr out = {0};
    rho_alpha_init(&env);
    rhocost_key_proc_into(proc, &env, &out);
    rho_alpha_free(&env);
    return out.data ? out.data : rho_heap_strdup("");
}

static char *rhocost_key_term(Atom *term) {
    RhoAlphaEnv env;
    RhoStr out = {0};
    rho_alpha_init(&env);
    rhocost_key_term_into(term, &env, &out);
    rho_alpha_free(&env);
    return out.data ? out.data : rho_heap_strdup("");
}

static void rhocost_collect_proc_par(Arena *arena, Atom *proc, RhoAtomVec *out);

static Atom *rhocost_proc_par_from_vec(Arena *arena, RhoAtomVec *vec) {
    if (vec->len == 0) return rho_nil(arena);
    if (vec->len == 1) return vec->items[0];

    RhoKeyedAtom *items = cetta_malloc(sizeof(RhoKeyedAtom) * vec->len);
    for (uint32_t i = 0; i < vec->len; i++) {
        items[i].atom = vec->items[i];
        items[i].key = rhocost_key_proc(vec->items[i]);
        items[i].ordinal = i;
    }
    qsort(items, vec->len, sizeof(RhoKeyedAtom), rho_keyed_atom_cmp);

    Atom **args = arena_alloc(arena, sizeof(Atom *) * vec->len);
    for (uint32_t i = 0; i < vec->len; i++) {
        args[i] = items[i].atom;
        free(items[i].key);
    }
    free(items);
    return rho_call(arena, "rho:par", args, vec->len);
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
    RhoKeyedAtom *items = cetta_malloc(sizeof(RhoKeyedAtom) * vec->len);
    for (uint32_t i = 0; i < vec->len; i++) {
        items[i].atom = vec->items[i];
        items[i].key = rhocost_key_term(vec->items[i]);
        items[i].ordinal = i;
    }
    qsort(items, vec->len, sizeof(RhoKeyedAtom), rho_keyed_atom_cmp);
    Atom **args = arena_alloc(arena, sizeof(Atom *) * vec->len);
    for (uint32_t i = 0; i < vec->len; i++) {
        args[i] = items[i].atom;
        free(items[i].key);
    }
    free(items);
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
 * layer, extended through signed bodies and purse surfaces. */

static bool rhocost_term_has_free_var(Atom *term, VarId var_id);

static bool rhocost_name_has_free_var(Atom *name, VarId var_id) {
    if (!name) return false;
    if (name->kind == ATOM_VAR) return name->var_id == var_id;
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
            view.args[1]->var_id == var_id) {
            return false;
        }
        return rhocost_term_has_free_var(view.args[2], var_id);
    case RHO_DROP:
    case RHO_VAL:
    case RHO_EVAL_PAYLOAD:
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
    if (norm->kind == ATOM_VAR && norm->var_id == old_id) {
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
            view.args[1]->var_id == old_id) {
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

static Atom *rhocost_subst_name(Arena *arena, Atom *name,
                                VarId var_id, Atom *replacement_term) {
    Atom *norm = rhocost_normalize_name(arena, name);
    if (norm->kind == ATOM_VAR && norm->var_id == var_id) {
        return rho_unary(arena, "rho:quote",
                         rhocost_normalize_term(arena, replacement_term));
    }
    return norm;
}

static Atom *rhocost_subst_name_mark(Arena *arena, Atom *name,
                                     VarId var_id, Atom *replacement_term,
                                     bool *matched) {
    Atom *norm = rhocost_normalize_name(arena, name);
    *matched = false;
    if (norm->kind == ATOM_VAR && norm->var_id == var_id) {
        *matched = true;
        return rho_unary(arena, "rho:quote",
                         rhocost_normalize_term(arena, replacement_term));
    }
    return norm;
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
            view.args[1]->var_id == var_id) {
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
 * signed send on the same channel key, whose signatures multiply into the
 * demand — or a whole redex, one signed par of a matched receive/send pair
 * demanding its single signature.  Purse tokens on the same channel key are
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
    char *key = rhocost_key_name(body_view.args[0]);
    bool ok;
    if (body_view.kind == RHO_RECV) {
        ok = rhocost_signed_endpoint_vec_push(recvs, component_index,
                                              term_view.args[1], body_view, key);
    } else {
        ok = rhocost_signed_endpoint_vec_push(sends, component_index,
                                              term_view.args[1], body_view, key);
    }
    if (!ok) free(key);
    return ok;
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
        if (recv.kind == RHO_RECV) {
            char *recv_key = rhocost_key_name(recv.args[0]);
            char *send_key = rhocost_key_name(send.args[0]);
            bool same = strcmp(recv_key, send_key) == 0;
            free(send_key);
            if (same) {
                bool ok = rhocost_whole_redex_vec_push(out, component_index,
                                                       term_view.args[1],
                                                       recv, send, recv_key);
                if (!ok) free(recv_key);
                return ok;
            }
            free(recv_key);
        }
    }
    return true;
}

static void rhocost_collect_term_components(Arena *arena, Atom *term, RhoAtomVec *out) {
    rhocost_collect_term_par(arena, term, out);
}

static bool rhocost_runtime_components_sort(
    RhoAtomVec *components, RhoCostProducerVec *producers) {
    RhoCostKeyedRuntimeComponent *keyed;

    if (!components || (producers && producers->len != components->len)) {
        return false;
    }
    if (components->len < 2u) return true;
    keyed = cetta_malloc(
        sizeof(RhoCostKeyedRuntimeComponent) * (size_t)components->len);
    if (!keyed) return false;
    for (uint32_t i = 0; i < components->len; i++) {
        keyed[i].term = components->items[i];
        keyed[i].producer = producers
            ? producers->items[i] : RHOCOST_NO_PRODUCER;
        keyed[i].key = rhocost_key_term(components->items[i]);
        keyed[i].ordinal = i;
        if (!keyed[i].key) {
            for (uint32_t j = 0; j < i; j++) free(keyed[j].key);
            free(keyed);
            return false;
        }
    }
    qsort(keyed, components->len,
          sizeof(RhoCostKeyedRuntimeComponent),
          rhocost_keyed_runtime_component_cmp);
    for (uint32_t i = 0; i < components->len; i++) {
        components->items[i] = keyed[i].term;
        if (producers) producers->items[i] = keyed[i].producer;
        free(keyed[i].key);
    }
    free(keyed);
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

/* Components are kept in canonical key order after every committed wave. */
static Atom *rhocost_runtime_components_term(
    Arena *arena, const RhoAtomVec *components) {
    if (!arena || !components) return NULL;
    if (components->len == 0u) return rhocost_nil(arena);
    if (components->len == 1u) return components->items[0];
    return rhocost_par(arena, components->items, components->len);
}

static void rhocost_trace_components_sort(RhoCostTraceComponentVec *components) {
    if (components->len < 2) return;
    RhoCostKeyedTraceComponent *keyed =
        cetta_malloc(sizeof(RhoCostKeyedTraceComponent) * components->len);
    for (uint32_t i = 0; i < components->len; i++) {
        keyed[i].component = components->items[i];
        keyed[i].key = rhocost_key_term(components->items[i].term);
        keyed[i].ordinal = i;
    }
    qsort(keyed, components->len, sizeof(RhoCostKeyedTraceComponent),
          rhocost_keyed_trace_component_cmp);
    for (uint32_t i = 0; i < components->len; i++) {
        components->items[i] = keyed[i].component;
        free(keyed[i].key);
    }
    free(keyed);
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
 * that cause list, one funding record per consumed purse head (surface and
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

static char *rhocost_step_transition_key(Atom *result, Atom *consumed_sig) {
    char *term_key = rhocost_key_term(result);
    char *sig_key = rhocost_key_signature(consumed_sig);
    RhoStr out = {0};
    (void)rho_str_appendf(&out, "%s//cost(%s)", term_key, sig_key);
    free(term_key);
    free(sig_key);
    return out.data ? out.data : rho_heap_strdup("");
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
    char *key;

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
        extras[i + 1] = rhocost_purse(arena, token->surface, token->rest);
    }

    next = rhocost_rebuild_result(arena, components, skip, skip_len,
                                  extras, extra_len);
    key = rhocost_step_transition_key(next, consumed_sig);
    return rhocost_step_set_acc_push_keyed(
        out, next, consumed_sig, body, participant_skip,
        participant_skip_len, tokens, chosen_tokens, key);
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
                                             const char *surface_key,
                                             RhoIndexVec *available) {
    rhocost_index_vec_init(available);
    for (uint32_t i = 0; i < tokens->len; i++) {
        if (strcmp(tokens->items[i].surface_key, surface_key) == 0 &&
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
                char *surface_key = rhocost_key_name(view.args[0]);
                ok = rhocost_token_vec_push(
                    &tokens, i, view.args[0], surface_key,
                    stack.args[0], stack.args[1]);
                if (!ok) free(surface_key);
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
                 &tokens, wholes.items[w].channel_key, &available_tokens) &&
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
            if (strcmp(recvs.items[r].channel_key, sends.items[s].channel_key) != 0) {
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
                     &tokens, recvs.items[r].channel_key, &available_tokens) &&
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
    char *expected_channel_key = NULL;
    char *recv_channel_key = NULL;
    char *send_channel_key = NULL;
    bool valid = false;

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

    expected_channel_key = rhocost_key_name(task->plan.channel);
    recv_channel_key = rhocost_key_name(recv.args[0]);
    send_channel_key = rhocost_key_name(send.args[0]);
    valid = expected_channel_key && recv_channel_key && send_channel_key &&
        strcmp(expected_channel_key, recv_channel_key) == 0 &&
        strcmp(expected_channel_key, send_channel_key) == 0;
    free(expected_channel_key);
    free(recv_channel_key);
    free(send_channel_key);
    if (!valid) return false;
    *out_demand = demand;
    return true;
}

static bool rhocost_parallel_task_funding_valid(
    Arena *arena, const RhoAtomVec *components,
    const RhoCostParallelTask *task) {
    RhoAtomVec funding_atoms;
    Atom *demand = NULL;
    Atom *funding = NULL;
    char *channel_key = NULL;
    bool valid = false;

    if (!rhocost_parallel_task_resource_shape_valid(components, task) ||
        !rhocost_parallel_task_redex_valid(
            arena, components, task, &demand)) {
        return false;
    }
    channel_key = rhocost_key_name(task->plan.channel);
    if (!channel_key) return false;
    rho_vec_init(&funding_atoms);
    for (uint32_t i = 0; i < task->plan.token_len; i++) {
        uint32_t index = task->plan.token_indices[i];
        RhoCostTermView purse = rhocost_term_view(components->items[index]);
        RhoCostTermView stack;
        char *surface_key;
        if (purse.kind != RHOCOST_TERM_PURSE || purse.nargs != 2u) {
            goto done;
        }
        stack = rhocost_term_view(purse.args[1]);
        if (stack.kind != RHOCOST_TERM_STACK_CONS || stack.nargs != 2u) {
            goto done;
        }
        surface_key = rhocost_key_name(purse.args[0]);
        if (!surface_key || strcmp(channel_key, surface_key) != 0) {
            free(surface_key);
            goto done;
        }
        free(surface_key);
        if (!rhocost_collect_signature_atoms(stack.args[0], &funding_atoms)) {
            goto done;
        }
    }
    funding = rhocost_signature_from_vec(arena, &funding_atoms);
    valid = funding && demand && atom_eq(funding, demand);

done:
    free(channel_key);
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
            .scheduler_policy = RHO_SCHEDULER_CANONICAL,
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

static Atom *rhocost_successor_frontier_expr(Arena *arena, Atom *term) {
    RhoCostStepSet steps = {0};
    Atom *list;
    Atom *result;
    Atom **items;
    if (!rhocost_collect_step_set(arena, term, &steps)) {
        return NULL;
    }
    items = arena_alloc(arena, sizeof(Atom *) * steps.len);
    if (!items && steps.len > 0) {
        rhocost_step_set_free(&steps);
        return NULL;
    }
    for (uint32_t i = 0; i < steps.len; i++) {
        items[i] = steps.items[i].result;
    }
    list = atom_expr(arena, items, steps.len);
    result = atom_expr2(arena, atom_symbol(arena, "superpose"), list);
    rhocost_step_set_free(&steps);
    return result;
}

Atom *rhocalc_cost_step_frontier_expr(Arena *arena, Atom *term) {
    RhoCostStepSet steps = {0};
    Atom *list;
    Atom *result;
    Atom **items;
    if (!rhocost_collect_step_set(arena, term, &steps)) {
        return NULL;
    }
    items = arena_alloc(arena, sizeof(Atom *) * steps.len);
    if (!items && steps.len > 0) {
        rhocost_step_set_free(&steps);
        return NULL;
    }
    for (uint32_t i = 0; i < steps.len; i++) {
        Atom *args[2] = {steps.items[i].consumed_sig, steps.items[i].result};
        items[i] = rho_call(arena, "lts:rho:cost:step", args, 2);
    }
    list = atom_expr(arena, items, steps.len);
    result = atom_expr2(arena, atom_symbol(arena, "superpose"), list);
    rhocost_step_set_free(&steps);
    return result;
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
    *out_next = successors.items[machine->rotating_turn % successors.len];
    machine->rotating_turn++;
    free(successors.items);
    return true;
}

static bool rho_machine_select_successor(RhoMachine *machine,
                                         Atom **out_next,
                                         bool *out_quiescent) {
    if (!machine) return false;
    switch (machine->profile.scheduler_policy) {
    case RHO_SCHEDULER_CANONICAL:
        return rho_machine_select_canonical_successor(machine,
                                                      out_next,
                                                      out_quiescent);
    case RHO_SCHEDULER_ROTATING:
        return rho_machine_select_rotating_successor(machine,
                                                     out_next,
                                                     out_quiescent);
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

    while (recv_pos < recvs.len && send_pos < sends.len) {
        int cmp = strcmp(recvs.items[recv_pos].key, sends.items[send_pos].key);
        if (cmp < 0) {
            recv_pos++;
            continue;
        }
        if (cmp > 0) {
            send_pos++;
            continue;
        }

        uint32_t recv_start = recv_pos;
        uint32_t send_start = send_pos;
        while (recv_pos < recvs.len &&
               strcmp(recvs.items[recv_pos].key,
                      recvs.items[recv_start].key) == 0) {
            recv_pos++;
        }
        while (send_pos < sends.len &&
               strcmp(sends.items[send_pos].key,
                      sends.items[send_start].key) == 0) {
            send_pos++;
        }

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
 * The CLI and library surfaces land here: run a term to quiescence under a
 * runtime profile (scheduler policy, reduction limit, worker count) and a
 * semantic profile (strict-core or cost), sequentially or threaded. */

bool rhocalc_reduce_to_quiescence_with_profile(Arena *arena, Atom *proc,
                                               const RhoRuntimeProfile *profile,
                                               RhoReductionResult *out) {
    return rhocalc_reduce_to_quiescence_with_eval_context(arena, proc, profile,
                                                          NULL, out);
}

bool rhocalc_reduce_to_quiescence_with_eval_context(
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

    for (;;) {
        Atom *next = NULL;
        bool quiescent = false;
        if (!rho_machine_select_successor(&machine, &next, &quiescent)) {
            rho_machine_free(&machine);
            return false;
        }
        if (quiescent) {
            out->residual = machine.current;
            rho_machine_free(&machine);
            return true;
        }
        if (out->reductions_taken == profile->reduction_limit) {
            out->residual = machine.current;
            out->status = RHOCALC_REDUCTION_LIMIT_EXHAUSTED;
            rho_machine_free(&machine);
            return true;
        }
        machine.current = next;
        out->reductions_taken++;
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
        if (out->reductions_taken == profile->reduction_limit) {
            out->residual = current;
            out->status = RHOCALC_REDUCTION_LIMIT_EXHAUSTED;
            rhocost_step_set_free(&successors);
            return true;
        }
        if (profile->scheduler_policy == RHO_SCHEDULER_ROTATING) {
            next = successors.items[rotating_turn % successors.len].result;
            rotating_turn++;
        } else {
            next = successors.items[0].result;
        }
        current = next;
        out->reductions_taken++;
        rhocost_step_set_free(&successors);
    }
}

bool rhocalc_reduce_to_quiescence_with_semantic_profile(
    Arena *arena, Atom *proc, RhocalcSemanticProfileId semantic_profile,
    const RhoRuntimeProfile *profile, RhoReductionResult *out) {
    return rhocalc_reduce_to_quiescence_with_semantic_profile_and_eval_context(
        arena, proc, semantic_profile, profile, NULL, out);
}

bool rhocalc_reduce_to_quiescence_with_semantic_profile_and_eval_context(
    Arena *arena, Atom *proc, RhocalcSemanticProfileId semantic_profile,
    const RhoRuntimeProfile *profile, const RhocalcEvalContext *eval_context,
    RhoReductionResult *out) {
    if (!rhocalc_semantic_profile_runtime_supported(semantic_profile)) {
        return false;
    }
    if (semantic_profile == RHOCALC_SEMANTIC_PROFILE_COST) {
        return rhocost_reduce_to_quiescence_with_profile(arena, proc, profile, out);
    }
    return rhocalc_reduce_to_quiescence_with_eval_context(arena, proc, profile,
                                                          eval_context, out);
}

bool rhocalc_reduce_to_quiescence(Arena *arena, Atom *proc,
                                  uint32_t reduction_limit, RhoReductionResult *out) {
    RhoRuntimeProfile profile = rho_runtime_profile_default(reduction_limit);
    return rhocalc_reduce_to_quiescence_with_profile(arena, proc,
                                                     &profile, out);
}
