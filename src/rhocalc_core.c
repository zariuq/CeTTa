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
#include "match.h"
#include "parallel_executor.h"
#include "stats.h"
#include "term_canon.h"
#include "segmented_array.h"

extern const RhocalcPayloadProvider *eval_payload_synchronous_provider(void)
    __attribute__((weak));
extern Atom *eval_open_payload_code(Arena *, Atom *) __attribute__((weak));
extern bool eval_payload_frame_source_state(CettaEvalPayloadFrame *,Arena *,
    StateCell *,Atom **,Atom **) __attribute__((weak));

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
    bool borrowed;
} RhoAtomVec;

typedef struct {
    Atom *atom;
    char *key;
    uint32_t ordinal;
    uint32_t binders;
} RhoKeyedAtom;

/* Indexed rows share the retained merge algorithm. Atomic callers may use
 * contiguous backing; retained callers prepare and retire fixed blocks. */
typedef struct {
    void *linear;
    uint32_t capacity;
    bool paged;
    CettaSegmentedArray pages;
} RhoSortBuffer;

static void *rho_sort_buffer_at(const RhoSortBuffer *buffer,
                               uint32_t index, size_t size) {
    return buffer->paged ? cetta_segmented_array_at(&buffer->pages, index)
        : (char *)buffer->linear + (size_t)index * size;
}

static RhoKeyedAtom *rho_keyed_row(const RhoSortBuffer *buffer, uint32_t index) {
    return rho_sort_buffer_at(buffer, index, sizeof(RhoKeyedAtom));
}

static CettaSegmentedStep rho_sort_buffer_prepare(RhoSortBuffer *buffer,
                                                 uint32_t index, size_t size) {
    if (!buffer->paged)
        return index < buffer->capacity ? CETTA_SEGMENTED_READY : CETTA_SEGMENTED_REFUSED;
    if (!buffer->pages.element_size && !cetta_segmented_array_init(&buffer->pages, size))
        return CETTA_SEGMENTED_REFUSED;
    return cetta_segmented_array_prepare(&buffer->pages, index);
}

static bool rho_sort_buffer_has_storage(const RhoSortBuffer *buffer) {
    return buffer->paged ? buffer->pages.blocks != NULL : buffer->linear != NULL;
}

static bool rho_sort_buffer_release_one(RhoSortBuffer *buffer) {
    if (buffer->paged) return cetta_segmented_array_release_one(&buffer->pages);
    free(buffer->linear);
    buffer->linear = NULL; buffer->capacity = 0u;
    return true;
}

static void rho_sort_buffer_free(RhoSortBuffer *buffer) {
    while (!rho_sort_buffer_release_one(buffer)) {}
}

/* A completed standalone key also records its contribution to the enclosing
 * monotone alpha counter. Quoted binders contribute only inside the quote. */
typedef struct {
    char *text;
    uint32_t binders;
} RhoKeyProduct;

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
    Atom *direct_value_channel;
    bool key_owned;
} RhoEndpoint;

typedef struct {
    RhoSortBuffer storage;
    uint32_t len;
} RhoEndpointVec;

static RhoEndpoint *rho_endpoint_at(const RhoEndpointVec *vec, uint32_t index) {
    return rho_sort_buffer_at(&vec->storage,index,sizeof(RhoEndpoint));
}

typedef struct {
    CettaNodeMemo memo;
    uint32_t owner_arena_id;
    uint32_t persistent_arena_id;
} RhoProcessKeyCache;

typedef struct {
    Arena *arena;
    Arena *result_arena;
    Arena process_arenas[2];
    uint32_t active_process_arena;
    uint32_t reductions_since_compaction;
    bool process_arenas_initialized;
    bool compaction_safe;
    RhoProcessKeyCache process_key_cache;
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
    bool failed;
} RhoStr;

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

static bool rho_run_charge(uint64_t *field, uint64_t amount) {
    if (amount > UINT64_MAX - *field) {
        rho_validation_set("rho retained preparation account exhausted");
        return false;
    }
    *field += amount;
    return true;
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
    for (uint32_t i=0u;i<nargs;i++) if (!args[i]) return NULL;
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
    return atom_is_quotation(atom);
}

static Atom *rho_unquote_payload_atom(Atom *atom) {
    return rho_is_builtin_quote(atom) ? atom->expr.elems[1] : atom;
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
    *vec=(RhoAtomVec){0};
}

static void rho_vec_free(RhoAtomVec *vec) {
    if (!vec->borrowed) free(vec->items);
    *vec=(RhoAtomVec){0};
}

static bool rho_vec_push(RhoAtomVec *vec, Atom *atom) {
    if (vec->borrowed || vec->len==UINT32_MAX) return false;
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
    *vec=(RhoEndpointVec){0};
}

static void rho_endpoint_vec_free(RhoEndpointVec *vec) {
    for (uint32_t i=0u;i<vec->len;i++) {
        RhoEndpoint *row=rho_endpoint_at(vec,i);
        if (row->key_owned) free(row->key);
    }
    rho_sort_buffer_free(&vec->storage);
    *vec=(RhoEndpointVec){0};
}

static bool rho_endpoint_vec_release_one(RhoEndpointVec *vec) {
    if (vec->len) {
        RhoEndpoint *row=rho_endpoint_at(vec,--vec->len);
        char *key=row->key_owned ? row->key : NULL;
        row->key=NULL;row->key_owned=false;
        free(key);
        return true;
    }
    if (rho_sort_buffer_has_storage(&vec->storage)) {
        rho_sort_buffer_release_one(&vec->storage);
        return true;
    }
    return false;
}

static bool rho_endpoint_vec_push(RhoEndpointVec *vec,
                                  uint32_t component_index,
                                  RhoView view,
                                  char *key, bool key_owned) {
    if (vec->len==UINT32_MAX) return false;
    if (vec->storage.paged) {
        if (rho_sort_buffer_prepare(&vec->storage,vec->len,sizeof(RhoEndpoint))!=CETTA_SEGMENTED_READY)
            return false;
    } else if (vec->len==vec->storage.capacity) {
        uint32_t capacity=vec->storage.capacity;
        if (capacity>UINT32_MAX/2u) return false;
        uint32_t next_cap=capacity ? capacity*2u : 8u;
        if ((uint64_t)next_cap>SIZE_MAX/sizeof(RhoEndpoint)) return false;
        RhoEndpoint *next=cetta_realloc(vec->storage.linear,sizeof(RhoEndpoint)*next_cap);
        if (!next) return false;
        vec->storage.linear=next;
        vec->storage.capacity=next_cap;
    }
    *rho_endpoint_at(vec,vec->len++)=(RhoEndpoint){.component_index=component_index,
        .view=view,.key=key,.key_owned=key_owned};
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

static bool rho_check_name_shape(Atom *name, RhoView view) {
    if (!name) { rho_validation_set("missing rho name"); return false; }
    if (name->kind == ATOM_VAR || (view.kind == RHO_QUOTE && view.nargs == 1u)) return true;
    rho_validation_set("rho name must be a quoted process or bound variable");
    return false;
}

static bool rho_check_name(Atom *name, RhoScope *scope,
                           bool allow_eval_payloads) {
    (void)scope;
    RhoView view = rho_view(name);
    if (!rho_check_name_shape(name, view)) return false;
    if (name->kind == ATOM_VAR) return true;
    RhoScope quote_scope;
    rho_scope_init(&quote_scope);
    bool okay = rho_check_proc(view.args[0], &quote_scope, allow_eval_payloads);
    rho_scope_free(&quote_scope);
    return okay;
}

static bool rho_check_proc_shape(Atom *proc, RhoView view, bool allow_eval_payloads) {
    switch (view.kind) {
    case RHO_NIL:
        return true;
    case RHO_PAR:
        return true;
    case RHO_SEND:
        if (view.nargs != 2) {
            rho_validation_set("rho:send expects channel and process payload");
            return false;
        }
        return true;
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
        return true;
    }
    case RHO_DROP:
        if (view.nargs != 1) {
            rho_validation_set("rho:drop expects one name");
            return false;
        }
        return true;
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

static bool rho_check_proc(Atom *proc, RhoScope *scope,
                           bool allow_eval_payloads) {
    RhoView view = rho_view(proc);
    if (!rho_check_proc_shape(proc, view, allow_eval_payloads)) return false;
    switch (view.kind) {
    case RHO_PAR:
        for (uint32_t i = 0u; i < view.nargs; i++)
            if (!rho_check_proc(view.args[i], scope, allow_eval_payloads)) return false;
        return true;
    case RHO_SEND:
        return rho_check_name(view.args[0], scope, allow_eval_payloads) &&
               rho_check_proc(view.args[1], scope, allow_eval_payloads);
    case RHO_RECV:
    case RHO_RECV_PERSISTENT: {
        if (!rho_check_name(view.args[0], scope, allow_eval_payloads)) return false;
        uint32_t mark = rho_scope_mark(scope);
        if (!rho_scope_push(scope, view.args[1]->var_id)) return false;
        bool okay = rho_check_proc(view.args[2], scope, allow_eval_payloads);
        rho_scope_pop(scope, mark);
        return okay;
    }
    case RHO_DROP:
        return rho_check_name(view.args[0], scope, allow_eval_payloads);
    default:
        return true;
    }
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
    if (semantic_profile == RHOCALC_SEMANTIC_PROFILE_RHOMETTA) {
        return rhocalc_process_well_formed_with_eval_payloads(proc);
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
    if (env->next_index==UINT32_MAX) return false;
    if (env->len == env->cap) {
        uint32_t next_cap=env->cap>UINT32_MAX/2u ? UINT32_MAX : env->cap ? env->cap*2u : 8u;
        size_t count=next_cap;
        if (next_cap<=env->cap || count>SIZE_MAX/sizeof(RhoAlphaEntry)) return false;
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
        if (var_id_eq(env->items[i - 1].var_id, var_id)) {
            if (out_index) *out_index = env->items[i - 1].index;
            return true;
        }
    }
    return false;
}

static bool rho_str_fail(RhoStr *str) {
    str->failed=true;
    rho_validation_set("rho canonical text construction failed");
    return false;
}

static bool rho_str_reserve(RhoStr *str, size_t extra) {
    if (str->failed) return false;
    if (str->len==SIZE_MAX || extra>SIZE_MAX-str->len-1u) return rho_str_fail(str);
    size_t needed = str->len + extra + 1u;
    if (needed <= str->cap) return true;
    size_t next_cap = str->cap ? str->cap : 128u;
    while (next_cap < needed) {
        if (next_cap>SIZE_MAX/2u) {next_cap=needed;break;}
        next_cap*=2u;
    }
    char *next = realloc(str->data, next_cap);
    if (!next) return rho_str_fail(str);
    str->data = next;
    str->cap = next_cap;
    return true;
}

static bool rho_str_append_n(RhoStr *str, const char *text, size_t len) {
    if ((!text && len) || !rho_str_reserve(str, len)) return rho_str_fail(str);
    if (len) memcpy(str->data + str->len, text, len);
    str->len += len;
    str->data[str->len] = '\0';
    return true;
}

static bool rho_str_append(RhoStr *str, const char *text) {
    return text ? rho_str_append_n(str, text, strlen(text)) : rho_str_fail(str);
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
        return rho_str_fail(str);
    }
    if (!rho_str_reserve(str, (size_t)needed)) {
        va_end(ap2);
        return false;
    }
    int written=vsnprintf(str->data + str->len, str->cap - str->len, fmt, ap2);
    va_end(ap2);
    if (written!=needed) return rho_str_fail(str);
    str->len += (size_t)needed;
    return true;
}

/* A failed append cannot license the already allocated prefix as a key. */
static char *rho_str_take(RhoStr *str,const char *empty) {
    if (str->failed) {
        free(str->data);*str=(RhoStr){0};
        return NULL;
    }
    char *text=str->data ? str->data : rho_heap_strdup(empty);
    *str=(RhoStr){0};
    return text;
}

typedef enum {
    RHO_ATOM_TEXT_ATOM = 0,
    RHO_ATOM_TEXT_CHAR,
} RhoAtomTextActionKind;

typedef struct {
    RhoAtomTextActionKind kind;
    Atom *atom;
    char character;
} RhoAtomTextAction;

typedef struct {
    RhoAtomTextAction *items;
    size_t len;
    size_t cap;
} RhoAtomTextStack;

static bool rho_atom_text_stack_grow(RhoAtomTextStack *stack) {
    size_t next_cap;
    RhoAtomTextAction *next;

    if (!stack || stack->cap > SIZE_MAX / 2u) return false;
    next_cap = stack->cap ? stack->cap * 2u : 64u;
    if (next_cap > SIZE_MAX / sizeof(*next)) return false;
    next = cetta_realloc(stack->items, next_cap * sizeof(*next));
    if (!next) return false;
    stack->items = next;
    stack->cap = next_cap;
    return true;
}

static bool rho_atom_text_stack_push_atom(RhoAtomTextStack *stack,
                                          Atom *atom) {
    if (!stack || !atom) return false;
    if (stack->len == stack->cap && !rho_atom_text_stack_grow(stack)) {
        return false;
    }
    stack->items[stack->len++] = (RhoAtomTextAction){
        .kind = RHO_ATOM_TEXT_ATOM,
        .atom = atom,
        .character = '\0',
    };
    return true;
}

static bool rho_atom_text_stack_push_char(RhoAtomTextStack *stack,
                                          char character) {
    if (!stack) return false;
    if (stack->len == stack->cap && !rho_atom_text_stack_grow(stack)) {
        return false;
    }
    stack->items[stack->len++] = (RhoAtomTextAction){
        .kind = RHO_ATOM_TEXT_CHAR,
        .atom = NULL,
        .character = character,
    };
    return true;
}

static char *rho_atom_text_key_slow(Atom *atom) {
    Arena scratch;
    char *rendered;
    char *out;

    arena_init(&scratch);
    rendered = atom_to_string(&scratch, atom);
    out = rendered ? rho_heap_strdup(rendered) : NULL;
    arena_free(&scratch);
    return out;
}

typedef struct {
    RhoStr *text;
} RhoAtomTextSink;

static void rho_atom_text_sink_init(RhoAtomTextSink *sink, RhoStr *text) {
    sink->text = text;
}

static bool rho_atom_text_sink_append_n(
    RhoAtomTextSink *sink, const char *text, size_t len) {
    if (!sink || (!text && len > 0u)) return false;
    return !sink->text || rho_str_append_n(sink->text, text, len);
}

static bool rho_atom_text_sink_append(
    RhoAtomTextSink *sink, const char *text) {
    return text && rho_atom_text_sink_append_n(sink, text, strlen(text));
}

static bool rho_atom_text_sink_appendf(
    RhoAtomTextSink *sink, const char *fmt, ...) {
    char local[128];
    char *rendered = local;
    va_list ap;
    va_list ap2;
    int needed;
    bool ok;

    va_start(ap, fmt);
    va_copy(ap2, ap);
    needed = vsnprintf(local, sizeof(local), fmt, ap);
    va_end(ap);
    if (needed < 0) {
        va_end(ap2);
        return false;
    }
    if ((size_t)needed >= sizeof(local)) {
        rendered = cetta_malloc((size_t)needed + 1u);
        if (!rendered) {
            va_end(ap2);
            return false;
        }
        if (vsnprintf(rendered, (size_t)needed + 1u, fmt, ap2) != needed) {
            free(rendered);
            va_end(ap2);
            return false;
        }
    }
    va_end(ap2);
    ok = rho_atom_text_sink_append_n(sink, rendered, (size_t)needed);
    if (rendered != local) free(rendered);
    return ok;
}

/* Append byte-for-byte atom_to_string rendering without FILE/open_memstream
 * or a scratch arena.  Canonical rho keys use ordinary (not PeTTa) atom
 * syntax.  Appending directly avoids materializing and then copying a second
 * payload-sized string into the enclosing process key. */
static bool rho_atom_text_key_direct_emit(
    Atom *root, RhoAtomTextSink *sink) {
    RhoAtomTextStack stack = {0};
    bool ok = root != NULL && sink != NULL;

    if (ok && root->kind == ATOM_GROUNDED &&
        root->ground.gkind == GV_STRING) {
        ok = rho_atom_text_sink_append(sink, root->ground.sval);
        goto done;
    }
    if (ok) ok = rho_atom_text_stack_push_atom(&stack, root);
    while (ok && stack.len > 0u) {
        RhoAtomTextAction action = stack.items[--stack.len];
        Atom *atom;
        if (action.kind == RHO_ATOM_TEXT_CHAR) {
            ok = rho_atom_text_sink_append_n(
                sink, &action.character, 1u);
            continue;
        }

        atom = action.atom;
        switch (atom->kind) {
        case ATOM_SYMBOL:
            ok = rho_atom_text_sink_append(sink, atom_name_cstr(atom));
            break;
        case ATOM_VAR: {
            CettaFrameIdentity epoch = var_epoch_suffix(atom->var_id);
            if (atom->name_key &&
                atom->name_key->kind == ATOM_GROUNDED &&
                atom->name_key->ground.gkind == GV_INTERNAL_TAG &&
                atom->name_key->ground.ival ==
                    (int64_t)CETTA_INTERNAL_TAG_PRIME_LEXICAL_SLOT) {
                ok = rho_atom_text_sink_appendf(
                    sink, "$%s", atom_symbol_current_id(atom) == SYMBOL_ID_NONE
                        ? "__prime_let" : atom_symbol_bytes(atom));
            } else if (atom->name_key) {
                ok = rho_atom_text_sink_append(sink, "$@") &&
                     (cetta_frame_identity_is_none(epoch) ||
                      rho_atom_text_sink_appendf(
                          sink, "#%" PRIu64, epoch.word)) &&
                     rho_atom_text_stack_push_atom(&stack, atom->name_key);
                break;
            } else {
                ok = rho_atom_text_sink_appendf(
                    sink, "$%s", atom_name_cstr(atom));
            }
            if (ok && !cetta_frame_identity_is_none(epoch)) {
                ok = rho_atom_text_sink_appendf(sink, "#%" PRIu64, epoch.word);
            }
            break;
        }
        case ATOM_GROUNDED:
            switch (atom->ground.gkind) {
            case GV_INT:
                ok = rho_atom_text_sink_appendf(
                    sink, "%ld", (long)atom->ground.ival);
                break;
            case GV_FLOAT: {
                char buffer[64];
                int written = cetta_format_float(
                    buffer, sizeof(buffer), atom->ground.fval);
                ok = written > 0 && (size_t)written < sizeof(buffer) &&
                     rho_atom_text_sink_append_n(
                         sink, buffer, (size_t)written);
                break;
            }
            case GV_BOOL:
                ok = rho_atom_text_sink_append(
                    sink, atom->ground.bval ? "True" : "False");
                break;
            case GV_STRING:
                ok = rho_atom_text_sink_append_n(sink, "\"", 1u);
                for (const char *p = atom->ground.sval; ok && *p; p++) {
                    if (*p == '\n') {
                        ok = rho_atom_text_sink_append(sink, "\\n");
                    } else if (*p == '"') {
                        ok = rho_atom_text_sink_append(sink, "\\\"");
                    } else if (*p == '\\') {
                        ok = rho_atom_text_sink_append(sink, "\\\\");
                    } else {
                        ok = rho_atom_text_sink_append_n(sink, p, 1u);
                    }
                }
                if (ok) {
                    ok = rho_atom_text_sink_append_n(sink, "\"", 1u);
                }
                break;
            case GV_BIGINT:
                ok = rho_atom_text_sink_append(
                    sink, atom_bigint_cstr(atom));
                break;
            case GV_SPACE:
                ok = rho_atom_text_sink_appendf(
                    sink, "<space %p>", atom->ground.ptr);
                break;
            case GV_STATE: {
                StateCell *cell = (StateCell *)atom->ground.ptr;
                ok = cell && cell->value &&
                     rho_atom_text_sink_append(sink, "(State ") &&
                     rho_atom_text_stack_push_char(&stack, ')') &&
                     rho_atom_text_stack_push_atom(&stack, cell->value);
                break;
            }
            case GV_CAPTURE:
                ok = rho_atom_text_sink_append(sink, "capture");
                break;
            case GV_BINDINGS:
                ok = rho_atom_text_sink_appendf(
                    sink, "<bindings %p>", atom->ground.ptr);
                break;
            case GV_TENSOR: {
                /* Preserve the existing text-key oracle. Tensor debug text
                 * describes layout, so tensors are not content-bearing channel names. */
                Arena scratch;
                arena_init_detached(&scratch);
                ok = rho_atom_text_sink_append(sink, atom_to_string(&scratch, atom));
                arena_free(&scratch);
                break;
            }
            case GV_FOREIGN:
                ok = rho_atom_text_sink_appendf(
                    sink, "<foreign %p>", atom->ground.ptr);
                break;
            case GV_STRUCTURAL: {
                char text[256];
                size_t length = atom_structural_display(atom, text, sizeof(text));
                if (length < sizeof(text)) ok = rho_atom_text_sink_append(sink, text);
                else {
                    char *long_text = cetta_malloc(length + 1u);
                    atom_structural_display(atom, long_text, length + 1u);
                    ok = rho_atom_text_sink_append(sink, long_text);
                    free(long_text);
                }
                break;
            }
            case GV_SEAL: {
                char text[256];
                size_t length = atom_seal_display_text(atom, text,
                                                       sizeof(text));
                ok = length < sizeof(text) &&
                     rho_atom_text_sink_append(sink, text);
                break;
            }
            case GV_TERM_GRAPH:
                ok = rho_atom_text_sink_appendf(
                    sink, "<term-graph %p>", atom->ground.ptr);
                break;
            case GV_RATIONAL:
                ok = rho_atom_text_sink_append(
                    sink, atom_rational_cstr(atom));
                break;
            case GV_PRIME_NEED_CAPABILITY:
                ok = rho_atom_text_sink_append(
                    sink, "<prime-need-capability>");
                break;
            case GV_PRIME_CONTEXT:
                ok = rho_atom_text_sink_appendf(
                    sink, "<context %u>",
                    atom_prime_context_depth(atom->ground.prime_context));
                break;
            case GV_INTERNAL_TAG:
                ok = rho_atom_text_sink_appendf(
                    sink, "<internal-tag %ld>",
                    (long)atom->ground.ival);
                break;
            }
            break;
        case ATOM_EXPR:
            ok = rho_atom_text_sink_append_n(sink, "(", 1u) &&
                 rho_atom_text_stack_push_char(&stack, ')');
            for (CettaExprIndex i = atom->expr.len; ok && i > 0u; i--) {
                ok = rho_atom_text_stack_push_atom(
                    &stack, atom->expr.elems[i - 1u]);
                if (ok && i > 1u) {
                    ok = rho_atom_text_stack_push_char(&stack, ' ');
                }
            }
            break;
        }
    }

done:
    free(stack.items);
    return ok;
}

static bool rho_atom_text_key_direct_into(Atom *root, RhoStr *out) {
    RhoAtomTextSink sink;
    rho_atom_text_sink_init(&sink, out);
    return rho_atom_text_key_direct_emit(root, &sink);
}

static char *rho_atom_text_key_direct(Atom *atom) {
    RhoStr out = {0};
    if (!rho_atom_text_key_direct_into(atom, &out)) {
        free(out.data);
        return NULL;
    }
    return rho_str_take(&out,"");
}

static bool rho_atom_text_unambiguous_symbol(const Atom *atom) {
    const unsigned char *text;

    if (!atom || atom->kind != ATOM_SYMBOL) return false;
    text = (const unsigned char *)atom_name_cstr((Atom *)atom);
    if (!text || !(('A' <= text[0] && text[0] <= 'Z') ||
                   ('a' <= text[0] && text[0] <= 'z') ||
                   text[0] == '_')) {
        return false;
    }
    if (strcmp((const char *)text, "True") == 0 ||
        strcmp((const char *)text, "False") == 0 ||
        strcmp((const char *)text, "capture") == 0) {
        return false;
    }
    for (size_t i = 1u; text[i] != '\0'; i++) {
        unsigned char ch = text[i];
        if (!(('A' <= ch && ch <= 'Z') ||
              ('a' <= ch && ch <= 'z') ||
              ('0' <= ch && ch <= '9') ||
              ch == '_' || ch == ':' || ch == '-' || ch == '.' ||
              ch == '+')) {
            return false;
        }
    }
    return true;
}

/* On this deliberately small constructor-term subset, ordinary atom syntax
 * is injective with respect to atom structure.  Its cached structural hash is
 * therefore a sound negative discriminator for the existing textual channel
 * equality.  Unsupported atoms and hash collisions retain exact text-key
 * comparison. */
static bool rho_atom_text_structural_hash_admitted(
    const Atom *atom, bool root) {
    if (!atom) return false;
    switch (atom->kind) {
    case ATOM_SYMBOL:
        return rho_atom_text_unambiguous_symbol(atom);
    case ATOM_VAR:
        return false;
    case ATOM_GROUNDED:
        switch (atom->ground.gkind) {
        case GV_INT:
        case GV_BOOL:
            return true;
        case GV_STRING:
            return !root;
        case GV_FLOAT:
        case GV_BIGINT:
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_BINDINGS:
        case GV_TENSOR:
        case GV_FOREIGN:
        case GV_STRUCTURAL:
        case GV_SEAL:
        case GV_TERM_GRAPH:
        case GV_RATIONAL:
        case GV_PRIME_NEED_CAPABILITY:
        case GV_PRIME_CONTEXT:
        case GV_INTERNAL_TAG:
            return false;
        }
        return false;
    case ATOM_EXPR:
        for (CettaExprIndex i = 0u; i < atom->expr.len; i++) {
            if (!rho_atom_text_structural_hash_admitted(
                    atom->expr.elems[i], false)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

typedef enum {
    RHO_ATOM_TEXT_RELATION_UNKNOWN = 0,
    RHO_ATOM_TEXT_RELATION_EQUAL,
    RHO_ATOM_TEXT_RELATION_DIFFERENT,
} RhoAtomTextRelation;

/* Decide ordinary atom-text equality directly from constructor structure
 * whenever that decision is representation-independent.  UNKNOWN is a
 * deliberate result: ambiguous symbol/constructor spellings and contextual
 * grounded values retain the exact textual comparison as their oracle.
 *
 * Recursing only through a common unambiguous constructor prefix matters.
 * It prevents an unsafe child spelling from shifting a later textual
 * boundary while still rejecting the overwhelmingly common case where two
 * generated channels first differ at a rigid constructor or literal. */
static RhoAtomTextRelation rho_atom_text_structural_relation(
    const Atom *left, const Atom *right, bool root) {
    bool left_admitted;
    bool right_admitted;

    if (!left || !right) return RHO_ATOM_TEXT_RELATION_UNKNOWN;
    if (left == right) return RHO_ATOM_TEXT_RELATION_EQUAL;

    left_admitted =
        rho_atom_text_structural_hash_admitted(left, root);
    right_admitted =
        rho_atom_text_structural_hash_admitted(right, root);
    if (left_admitted && right_admitted) {
        if (atom_hash((Atom *)left) != atom_hash((Atom *)right)) {
            return RHO_ATOM_TEXT_RELATION_DIFFERENT;
        }
        return atom_eq((Atom *)left, (Atom *)right)
            ? RHO_ATOM_TEXT_RELATION_EQUAL
            : RHO_ATOM_TEXT_RELATION_DIFFERENT;
    }

    if (left->kind != ATOM_EXPR || right->kind != ATOM_EXPR ||
        left->expr.len == 0u || right->expr.len == 0u ||
        !rho_atom_text_unambiguous_symbol(left->expr.elems[0]) ||
        !rho_atom_text_unambiguous_symbol(right->expr.elems[0])) {
        return RHO_ATOM_TEXT_RELATION_UNKNOWN;
    }
    if (!atom_symbols_equal(left->expr.elems[0], right->expr.elems[0])) {
        return RHO_ATOM_TEXT_RELATION_DIFFERENT;
    }

    {
        CettaExprIndex common = left->expr.len < right->expr.len
            ? left->expr.len : right->expr.len;
        for (CettaExprIndex i = 1u; i < common; i++) {
            RhoAtomTextRelation child =
                rho_atom_text_structural_relation(
                    left->expr.elems[i], right->expr.elems[i], false);
            if (child != RHO_ATOM_TEXT_RELATION_EQUAL) return child;
        }
    }
    return left->expr.len == right->expr.len
        ? RHO_ATOM_TEXT_RELATION_EQUAL
        : RHO_ATOM_TEXT_RELATION_DIFFERENT;
}

static bool rho_atom_text_key_into(Atom *atom, RhoStr *out) {
    static atomic_int oracle_mode = ATOMIC_VAR_INIT(-1);
    int mode = atomic_load_explicit(&oracle_mode, memory_order_relaxed);
    char *slow;
    size_t mark;
    bool ok;

    if (!out) return false;
    if (mode < 0) {
        int selected = getenv("CETTA_RHO_TEXT_KEY_SLOW_ORACLE") ? 1 : 0;
        int expected = -1;
        if (!atomic_compare_exchange_strong_explicit(
                &oracle_mode, &expected, selected,
                memory_order_relaxed, memory_order_relaxed)) {
            selected = expected;
        }
        mode = selected;
    }
    mark = out->len;
    if (mode == 0) {
        if (rho_atom_text_key_direct_into(atom, out)) return true;
        if (out->failed) return false;
        out->len = mark;
        if (out->data) out->data[mark] = '\0';
    }
    slow = rho_atom_text_key_slow(atom);
    if (!slow) return rho_str_fail(out);
    ok = rho_str_append(out, slow);
    free(slow);
    return ok;
}

static void rho_key_proc_into(Atom *proc, RhoAlphaEnv *env, RhoStr *out);
static void rho_key_name_into(Atom *name, RhoAlphaEnv *env, RhoStr *out);

static void rho_key_var_into(Atom *var, RhoAlphaEnv *env, RhoStr *out) {
    uint32_t index = 0;
    if (rho_alpha_lookup(env, var->var_id, &index)) {
        (void)rho_str_appendf(out, "$#%u", index);
    } else {
        (void)rho_str_appendf(out, "$%s:%" PRIu64 ":%" PRIu64,
                              atom_name_cstr(var), var->var_id.base,
                              var->var_id.epoch.word);
    }
}

static char *rho_key_proc_quoted(Atom *proc) {
    RhoAlphaEnv env;
    RhoStr out = {0};
    rho_alpha_init(&env);
    rho_key_proc_into(proc, &env, &out);
    rho_alpha_free(&env);
    return rho_str_take(&out,"");
}

static void rho_key_name_into(Atom *name, RhoAlphaEnv *env, RhoStr *out) {
    if (!name) {rho_str_fail(out);return;}
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
    if (!proc) {rho_str_fail(out);return;}
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
    case RHO_RECV:
    case RHO_RECV_PERSISTENT: {
        uint32_t mark = rho_alpha_mark(env);
        (void)rho_str_append(
            out, view.kind == RHO_RECV_PERSISTENT
                ? "recv-persistent(" : "recv(");
        rho_key_name_into(view.args[0], env, out);
        (void)rho_str_append(out, ",");
        if (!rho_alpha_push(env,view.args[1]->var_id)) {rho_str_fail(out);return;}
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
        (void)rho_str_append(out, "val(");
        (void)rho_atom_text_key_into(view.args[0], out);
        (void)rho_str_append(out, ")");
        return;
    }
    case RHO_EVAL_PAYLOAD: {
        (void)rho_str_append(out, "defer(");
        (void)rho_atom_text_key_into(view.args[0], out);
        (void)rho_str_append(out, ")");
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
    return rho_str_take(&out,"");
}

static char *rho_key_name(Atom *name) {
    RhoAlphaEnv env;
    RhoStr out = {0};
    rho_alpha_init(&env);
    rho_key_name_into(name, &env, &out);
    rho_alpha_free(&env);
    return rho_str_take(&out,"");
}

/* A retained rendering of the existing process/name key grammar. Literal
 * bytes and alpha-environment probes are individual actions. A quotation
 * starts a fresh numbering environment without overwriting its parent's
 * bindings. Payload atom rendering remains an explicitly atomic provider. */
typedef struct {
    Atom *atom;
    RhoView view;
    uint32_t stage, position, mark, saved_base, saved_next;
    bool name;
} RhoKeyFrame;

typedef struct {
    CettaRecordStack frames;
    RhoKeyFrame pending_frame;
    RhoAlphaEnv env;
    uint32_t base;
    RhoStr text;
    const char *pending;
    char scalar[128];
    bool done;
} RhoKeyCursor;

static RhoKeyFrame *rho_key_frame_at(RhoKeyCursor *cursor, uint32_t index) {
    return cetta_record_stack_at(&cursor->frames,index,&cursor->pending_frame);
}

static bool rho_key_cursor_push(RhoKeyCursor *cursor, Atom *atom, bool name) {
    if (!atom) return rho_str_fail(&cursor->text);
    if (!cetta_record_stack_request(&cursor->frames,sizeof(RhoKeyFrame))) return false;
    cursor->pending_frame=(RhoKeyFrame){.atom = atom, .name = name};
    return true;
}

static RhoKeyCursor *rho_key_cursor_new_at(Atom *atom, bool name, uint32_t next_index) {
    RhoKeyCursor *cursor = calloc(1u, sizeof(*cursor));
    if (!cursor) return NULL;
    cursor->env.next_index = next_index;
    if (!rho_key_cursor_push(cursor, atom, name)) {
        while (cursor->frames.length) (void)cetta_record_stack_pop(&cursor->frames);
        (void)cetta_record_stack_free_storage(&cursor->frames);
        free(cursor);return NULL;
    }
    return cursor;
}

static RhoKeyCursor *rho_key_cursor_new(Atom *atom, bool name) {
    return rho_key_cursor_new_at(atom, name, 0u);
}

static bool rho_key_cursor_release_frames_one(RhoKeyCursor *cursor) {
    if (!cursor) return false;
    if (cursor->frames.length) {
        (void)cetta_record_stack_pop(&cursor->frames);
        return true;
    }
    if (cursor->frames.slots.blocks) {
        cetta_record_stack_release_storage_one(&cursor->frames);
        return true;
    }
    return false;
}

static bool rho_key_cursor_release_one(RhoKeyCursor **owner) {
    if (!owner || !*owner) return false;
    RhoKeyCursor *cursor=*owner;
    if (rho_key_cursor_release_frames_one(cursor)) return true;
    if (cursor->text.data) {
        free(cursor->text.data);cursor->text.data=NULL;
    } else if (cursor->env.items) {
        rho_alpha_free(&cursor->env);
    } else {
        free(cursor);*owner=NULL;
    }
    return true;
}

static void rho_key_cursor_free(RhoKeyCursor *cursor) {
    if (!cursor) return;
    free(cursor->text.data);
    while (cursor->frames.length) (void)cetta_record_stack_pop(&cursor->frames);
    (void)cetta_record_stack_free_storage(&cursor->frames);
    rho_alpha_free(&cursor->env);
    free(cursor);
}

static bool rho_key_cursor_advance(RhoKeyCursor *cursor) {
    if (!cursor || cursor->text.failed) return false;
    if (cursor->done) return true;
    if (cursor->pending) {
        if (*cursor->pending) {
            if (!rho_str_append_n(&cursor->text, cursor->pending, 1u)) return false;
            cursor->pending++;
        } else cursor->pending = NULL;
        return true;
    }
    if (cursor->frames.pending)
        return cetta_record_stack_prepare(&cursor->frames,&cursor->pending_frame)!=CETTA_SEGMENTED_REFUSED;
    if (!cursor->frames.length) {
        if (cursor->frames.slots.blocks) {
            cetta_record_stack_release_storage_one(&cursor->frames);
            return true;
        }
        cursor->done=true;return true;
    }
    RhoKeyFrame *frame = rho_key_frame_at(cursor,cursor->frames.length - 1u);
    if (frame->stage == UINT32_MAX) { if (!cetta_record_stack_pop(&cursor->frames)) return false; return true; }
    if (frame->name) {
        if (frame->atom->kind == ATOM_VAR) {
            switch (frame->stage) {
            case 0u:
                frame->position = cursor->env.len;
                frame->stage = 10u;
                return true;
            case 10u:
                if (frame->position > cursor->base) {
                    const RhoAlphaEntry *entry = &cursor->env.items[--frame->position];
                    if (var_id_eq(entry->var_id, frame->atom->var_id)) {
                        snprintf(cursor->scalar, sizeof(cursor->scalar), "$#%u", entry->index);
                        cursor->pending = cursor->scalar;
                        frame->stage = UINT32_MAX;
                    }
                } else {
                    cursor->pending = "$";
                    frame->stage = 11u;
                }
                return true;
            case 11u:
                cursor->pending = atom_name_cstr(frame->atom);
                frame->stage = 12u;
                return cursor->pending != NULL;
            case 12u:
                snprintf(cursor->scalar, sizeof(cursor->scalar), ":%" PRIu64 ":%" PRIu64,
                    frame->atom->var_id.base, frame->atom->var_id.epoch.word);
                cursor->pending = cursor->scalar;
                frame->stage = UINT32_MAX;
                return true;
            default:
                return false;
            }
        }
        switch (frame->stage) {
        case 0u: {
            frame->view = rho_view(frame->atom);
            if (frame->view.kind != RHO_QUOTE || frame->view.nargs != 1u) {
                cursor->pending = "?bad-name"; frame->stage = UINT32_MAX; return true;
            }
            RhoView inner = rho_view(frame->view.args[0]);
            if (inner.kind == RHO_DROP && inner.nargs == 1u) {
                frame->atom = inner.args[0]; return true;
            }
            frame->mark = cursor->env.len;
            frame->saved_next = cursor->env.next_index;
            frame->saved_base = cursor->base;
            cursor->base = cursor->env.len;
            cursor->env.next_index = 0u;
            cursor->pending = "@(";
            frame->stage = 1u;
            return true;
        }
        case 1u:
            frame->stage = 2u;
            return rho_key_cursor_push(cursor, frame->view.args[0], false);
        case 2u:
            rho_alpha_pop(&cursor->env, frame->mark);
            cursor->env.next_index = frame->saved_next;
            cursor->base = frame->saved_base;
            cursor->pending = ")";
            frame->stage = UINT32_MAX;
            return true;
        default:
            return false;
        }
    }
    if (!frame->stage) {
        frame->view = rho_view(frame->atom);
        frame->mark = cursor->env.len;
        frame->stage = 1u;
        switch (frame->view.kind) {
        case RHO_NIL: cursor->pending = "0"; frame->stage = UINT32_MAX; break;
        case RHO_PAR: cursor->pending = "par("; break;
        case RHO_SEND: cursor->pending = "send("; break;
        case RHO_RECV: cursor->pending = "recv("; break;
        case RHO_RECV_PERSISTENT: cursor->pending = "recv-persistent("; break;
        case RHO_DROP: cursor->pending = "drop("; break;
        case RHO_VAL: cursor->pending = "val("; break;
        case RHO_EVAL_PAYLOAD: cursor->pending = "defer("; break;
        default: cursor->pending = "?bad-proc"; frame->stage = UINT32_MAX; break;
        }
        return true;
    }
    switch (frame->view.kind) {
    case RHO_PAR:
        if (frame->position == frame->view.nargs) break;
        if (frame->stage == 1u) {
            if (frame->position) cursor->pending = "|";
            frame->stage = 2u;
            return true;
        }
        frame->stage = 1u;
        return rho_key_cursor_push(cursor, frame->view.args[frame->position++], false);
    case RHO_SEND:
    case RHO_RECV:
    case RHO_RECV_PERSISTENT:
        switch (frame->stage++) {
        case 1u: return rho_key_cursor_push(cursor, frame->view.args[0], true);
        case 2u: cursor->pending = ","; return true;
        case 3u:
            if (frame->view.kind == RHO_SEND)
                return rho_key_cursor_push(cursor, frame->view.args[1], false);
            if (!rho_alpha_push(&cursor->env, frame->view.args[1]->var_id))
                return rho_str_fail(&cursor->text);
            return true;
        case 4u:
            if (frame->view.kind != RHO_SEND)
                return rho_key_cursor_push(cursor, frame->view.args[2], false);
            break;
        default: break;
        }
        rho_alpha_pop(&cursor->env, frame->mark);
        break;
    case RHO_DROP:
        if (frame->stage++ == 1u)
            return rho_key_cursor_push(cursor, frame->view.args[0], true);
        break;
    case RHO_VAL:
    case RHO_EVAL_PAYLOAD:
        if (frame->stage++ == 1u)
            return rho_atom_text_key_into(frame->view.args[0], &cursor->text);
        break;
    default:
        return false;
    }
    cursor->pending = ")";
    frame->stage = UINT32_MAX;
    return true;
}

static void rho_key_product_free(RhoKeyProduct *product) {
    if (!product) return;
    free(product->text); free(product);
}

static void rho_process_key_cache_init(
    RhoProcessKeyCache *cache, const Arena *owner,
    const Arena *persistent) {
    *cache = (RhoProcessKeyCache){
        .owner_arena_id = owner ? owner->identity : 0u,
        .persistent_arena_id = persistent ? persistent->identity : 0u,
    };
}

static void rho_process_key_cache_release_keys(RhoProcessKeyCache *cache) {
    if (!cache) return;
    /* Each stored product was allocated once by its successful key job.
     * Repeated source/role lookups borrow that product without another put. */
    for (size_t i=0u;i<cache->memo.inline_len;i++)
        rho_key_product_free((RhoKeyProduct *)cache->memo.inline_slots[i].value);
    for (size_t i=0u;i<cache->memo.cap;i++) {
        const CettaNodeMemoSlot *entry=cetta_node_memo_entry(&cache->memo,i);
        if (entry && entry->node) rho_key_product_free((RhoKeyProduct *)entry->value);
    }
    cetta_node_memo_free(&cache->memo);
}

static void rho_process_key_cache_clear(
    RhoProcessKeyCache *cache, const Arena *owner) {
    if (!cache) return;
    rho_process_key_cache_release_keys(cache);
    cache->owner_arena_id = owner ? owner->identity : 0u;
}

static void rho_process_key_cache_free(RhoProcessKeyCache *cache) {
    if (!cache) return;
    rho_process_key_cache_release_keys(cache);
    *cache = (RhoProcessKeyCache){0};
}

static bool rho_process_key_cache_admits(
    const RhoProcessKeyCache *cache, const Atom *atom) {
    return cache && atom &&
           (atom->flags & ATOM_FLAG_TERM_STABLE) != 0u &&
           (atom->arena_id == 0u ||
            atom->arena_id == cache->owner_arena_id ||
            atom->arena_id == cache->persistent_arena_id);
}

/* Completed reset-context key products only. Mutable/foreign input stays
 * uncached; a pending key or memo replacement is never published. Shifted
 * keys use rho_key_cursor_new_at with their explicit alpha context instead. */
typedef struct {
    RhoProcessKeyCache *cache;
    Atom *atom;
    RhoKeyCursor *key;
    CettaNodeMemoCursor memo;
    RhoKeyProduct *result;
    unsigned stage;
    bool name, admitted, owned, done;
} RhoProcessKeyCursor;

static bool rho_process_key_cursor_begin(RhoProcessKeyCursor *cursor,
    RhoProcessKeyCache *cache, Atom *atom, bool name) {
    if (!atom) return false;
    *cursor=(RhoProcessKeyCursor){.cache=cache,.atom=atom,.name=name,
        .admitted=rho_process_key_cache_admits(cache,atom)};
    if (cursor->admitted)
        cetta_node_memo_find_begin(&cursor->memo,&cache->memo,atom,name);
    else cursor->stage=1u;
    return true;
}

static void rho_process_key_cursor_free(RhoProcessKeyCursor *cursor) {
    cetta_node_memo_cancel(&cursor->memo);
    rho_key_cursor_free(cursor->key);
    if (cursor->owned) rho_key_product_free(cursor->result);
    *cursor=(RhoProcessKeyCursor){0};
}

static bool rho_process_key_cursor_advance(RhoProcessKeyCursor *cursor,
                                            uint64_t *memo_work) {
    if (cursor->done) return true;
    if ((cursor->stage==0u || cursor->stage==3u) && memo_work &&
            !rho_run_charge(memo_work,1u)) return false;
    switch (cursor->stage) {
    case 0u: {
        CettaNodeMemoStatus status=cetta_node_memo_advance(&cursor->memo);
        if (status==CETTA_NODE_MEMO_PENDING) return true;
        if (status==CETTA_NODE_MEMO_FOUND) {
            cursor->result=(RhoKeyProduct *)cursor->memo.value;
            cursor->done=true;
        } else if (status==CETTA_NODE_MEMO_ABSENT) cursor->stage=1u;
        else return false;
        cetta_node_memo_cancel(&cursor->memo);
        return true;
    }
    case 1u:
        cursor->key=rho_key_cursor_new(cursor->atom,cursor->name);
        cursor->stage=2u;
        return cursor->key!=NULL;
    case 2u:
        if (!rho_key_cursor_advance(cursor->key)) return false;
        if (!cursor->key->done) return true;
        cursor->result=malloc(sizeof(*cursor->result));
        if (!cursor->result) return false;
        cursor->owned=true;
        *cursor->result=(RhoKeyProduct){.text=rho_str_take(&cursor->key->text,""),
            .binders=cursor->key->env.next_index};
        rho_key_cursor_free(cursor->key); cursor->key=NULL;
        if (!cursor->result->text) return false;
        if (cursor->admitted) {
            cetta_node_memo_store_begin(&cursor->memo,&cursor->cache->memo,
                cursor->atom,cursor->name,(uintptr_t)cursor->result,SIZE_MAX);
            cursor->stage=3u;
        } else cursor->done=true;
        return true;
    case 3u: {
        CettaNodeMemoStatus status=cetta_node_memo_advance(&cursor->memo);
        if (status==CETTA_NODE_MEMO_PENDING) return true;
        if (status!=CETTA_NODE_MEMO_STORED) return false;
        cursor->owned=false; cursor->done=true;
        cetta_node_memo_cancel(&cursor->memo);
        return true;
    }
    default:
        return false;
    }
}

static RhoKeyProduct *rho_process_key_cursor_take(RhoProcessKeyCursor *cursor,
                                                  bool *owned) {
    if (!cursor->done || !cursor->result || !owned) return NULL;
    RhoKeyProduct *result=cursor->result;
    *owned=cursor->owned;
    cursor->result=NULL; cursor->owned=false;
    return result;
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

static int rho_endpoint_key_compare(
    const RhoEndpoint *left, const RhoEndpoint *right) {
    return strcmp(left->key, right->key);
}

static int rho_endpoint_cmp_key(const void *lhs, const void *rhs) {
    const RhoEndpoint *a = lhs;
    const RhoEndpoint *b = rhs;
    int by_key = rho_endpoint_key_compare(a, b);
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

static bool rho_collect_par(Arena *arena, Atom *proc, RhoAtomVec *out) {
    Atom *norm=rho_normalize_proc(arena,proc);
    if (!norm) return false;
    RhoView view=rho_view(norm);
    if (view.kind==RHO_NIL) return true;
    if (view.kind==RHO_PAR) {
        for (uint32_t i=0u;i<view.nargs;i++)
            if (!rho_collect_par(arena,view.args[i],out)) return false;
        return true;
    }
    return rho_vec_push(out,norm);
}

static RhoKeyedAtom *rho_keyed_atoms_new(const RhoAtomVec *vec,char *(*key)(Atom *)) {
    RhoKeyedAtom *items=cetta_malloc(sizeof(*items)*(size_t)vec->len);
    for (uint32_t i=0u;i<vec->len;i++) {
        items[i]=(RhoKeyedAtom){.atom=vec->items[i],.key=key(vec->items[i]),.ordinal=i};
        if (!items[i].key) {
            for (uint32_t j=0u;j<i;j++) free(items[j].key);
            free(items);return NULL;
        }
    }
    return items;
}

static Atom *rho_par_from_vec(Arena *arena, RhoAtomVec *vec) {
    if (vec->len == 0) return rho_nil(arena);
    if (vec->len == 1) return vec->items[0];

    RhoKeyedAtom *items=rho_keyed_atoms_new(vec,rho_key_proc);
    if (!items) return NULL;
    qsort(items, vec->len, sizeof(RhoKeyedAtom), rho_keyed_atom_cmp);

    Atom **args = arena_alloc(arena, sizeof(Atom *) * vec->len);
    for (uint32_t i = 0; i < vec->len; i++) {
        args[i] = items[i].atom;
        free(items[i].key);
    }
    free(items);

    return rho_call(arena, "rho:par", args, vec->len);
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
    if (!name) return NULL;
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
            if (!rho_collect_par(arena,view.args[i],&flat)) {rho_vec_free(&flat);return NULL;}
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

/* The synchronous entry drains the same retained semantic task provider used
 * by canonical COMM. Its definition follows the shared normalization cursor. */
static Atom *rho_subst_proc(Arena *arena, Atom *proc,
                            VarId var_id, Atom *replacement_name);
static Atom *rho_subst_payload_proc(Arena *arena, Atom *proc,
                                    VarId var_id, Atom *payload);

static Atom *rho_fresh_var_like(Arena *arena, Atom *var) {
    return atom_var_like(arena, var, fresh_var_id());
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

bool rhocalc_atom_text_key_correspondence_selftest(Arena *arena) {
    StateCell state = {0};
    Atom *cases[12];
    Atom *different_left;
    Atom *different_right;
    size_t count = 0u;

    if (!arena || !g_symbols) return false;
    cases[count++] = atom_symbol(arena, "alpha");
    cases[count++] = atom_var_with_id(arena, "x", var_id_from_base(17u));
    cases[count++] = atom_var_with_id(
        arena, "epoch", (VarId){19u, {3u}});
    cases[count++] = atom_int(arena, -42);
    cases[count++] = atom_float(arena, 1.25);
    cases[count++] = atom_bool(arena, false);
    cases[count++] = atom_string(arena, "line\n\"quoted\"\\tail");
    cases[count++] = atom_bigint(arena, "123456789012345678901234567890");
#if CETTA_BUILD_WITH_GMP
    cases[count++] = atom_rational(arena, "-22/7");
#else
    /* Rational construction is unavailable without GMP; NULL is not an
     * atom and must not be passed to the rendering correspondence check. */
    if (atom_rational(arena, "-22/7") != NULL) return false;
#endif
    cases[count++] = atom_expr3(
        arena, atom_symbol(arena, "f"),
        atom_string(arena, "nested\nstring"),
        atom_var_with_id(arena, "z", var_id_from_base(23u)));
    state.value = atom_expr2(
        arena, atom_symbol(arena, "held"), atom_int(arena, 9));
    state.content_type = atom_symbol(arena, "Atom");
    cases[count++] = atom_state(arena, &state);
    cases[count++] = rho_value_proc(
        arena, atom_expr2(arena, atom_symbol(arena, "payload"),
                          atom_bool(arena, true)));

    for (size_t i = 0u; i < count; i++) {
        if (!cases[i]) return false;
        char *direct = rho_atom_text_key_direct(cases[i]);
        char *slow = rho_atom_text_key_slow(cases[i]);
        bool equal = direct && slow && strcmp(direct, slow) == 0;
        free(direct);
        free(slow);
        if (!equal) return false;
    }

    {
        Atom *safe_left = atom_expr3(
            arena, atom_symbol(arena, "gslt-rho:rule-channel-v1"),
            atom_symbol(arena, "sha256-safe"), atom_int(arena, 17));
        Atom *safe_right = atom_expr3(
            arena, atom_symbol(arena, "gslt-rho:rule-channel-v1"),
            atom_symbol(arena, "sha256-safe"), atom_int(arena, 17));
        Atom *safe_different = atom_expr3(
            arena, atom_symbol(arena, "gslt-rho:rule-channel-v1"),
            atom_symbol(arena, "sha256-safe"), atom_int(arena, 18));
        Atom *ambiguous_symbol = atom_symbol(arena, "(alpha)");
        Atom *alpha_items[] = {atom_symbol(arena, "alpha")};
        Atom *ambiguous_expr = atom_expr(arena, alpha_items, 1u);
        Atom *nested_unknown_left = atom_expr2(
            arena, atom_symbol(arena, "channel"), ambiguous_symbol);
        Atom *nested_unknown_right = atom_expr2(
            arena, atom_symbol(arena, "channel"), ambiguous_expr);
        Atom *root_string = atom_string(arena, "alpha");
        char *symbol_key = rho_atom_text_key_direct(ambiguous_symbol);
        char *expr_key = rho_atom_text_key_direct(ambiguous_expr);
        bool admitted =
            rho_atom_text_structural_hash_admitted(safe_left, true) &&
            rho_atom_text_structural_hash_admitted(safe_right, true) &&
            atom_hash(safe_left) == atom_hash(safe_right);
        bool rejected =
            !rho_atom_text_structural_hash_admitted(
                ambiguous_symbol, true) &&
            rho_atom_text_structural_hash_admitted(
                ambiguous_expr, true) &&
            !rho_atom_text_structural_hash_admitted(root_string, true) &&
            !rho_atom_text_structural_hash_admitted(cases[1], true) &&
            !rho_atom_text_structural_hash_admitted(cases[4], true);
        bool ambiguity_witness = symbol_key && expr_key &&
            strcmp(symbol_key, expr_key) == 0;
        bool structural_examples =
            rho_atom_text_structural_relation(
                safe_left, safe_right, true) ==
                RHO_ATOM_TEXT_RELATION_EQUAL &&
            rho_atom_text_structural_relation(
                safe_left, safe_different, true) ==
                RHO_ATOM_TEXT_RELATION_DIFFERENT &&
            rho_atom_text_structural_relation(
                nested_unknown_left, nested_unknown_right, true) ==
                RHO_ATOM_TEXT_RELATION_UNKNOWN;
        free(symbol_key);
        free(expr_key);
        if (!admitted || !rejected || !ambiguity_witness ||
            !structural_examples) {
            return false;
        }
    }

    different_left = atom_expr2(
        arena, atom_symbol(arena, "different"), atom_symbol(arena, "left"));
    different_right = atom_expr2(
        arena, atom_symbol(arena, "different"), atom_symbol(arena, "right"));
    {
        char *left = rho_atom_text_key_direct(different_left);
        char *right = rho_atom_text_key_direct(different_right);
        bool distinct = left && right && strcmp(left, right) != 0;
        free(left);
        free(right);
        return distinct;
    }
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

static Atom *rho_endpoint_direct_value_channel(RhoView endpoint) {
    RhoView channel;
    RhoView quoted;

    if (endpoint.nargs == 0u) return NULL;
    channel = rho_view(endpoint.args[0]);
    if (channel.kind != RHO_QUOTE || channel.nargs != 1u) return NULL;
    quoted = rho_view(channel.args[0]);
    if (quoted.kind != RHO_VAL || quoted.nargs != 1u) return NULL;
    return quoted.args[0];
}

static bool rho_collect_endpoint(RhoAtomVec *components,uint32_t index,
        RhoEndpointVec *sends,RhoEndpointVec *recvs) {
    RhoView view=rho_view(components->items[index]);
    RhoEndpointVec *target=NULL;
    if (view.kind==RHO_SEND && view.nargs==2u) target=sends;
    else if ((view.kind==RHO_RECV || view.kind==RHO_RECV_PERSISTENT) && view.nargs==3u)
        target=recvs;
    if (!target) return true;
    char *key=rho_key_name(view.args[0]);
    if (!key) return false;
    if (rho_endpoint_vec_push(target,index,view,key,true)) return true;
    free(key);
    return false;
}

static bool rho_collect_endpoints(RhoAtomVec *components,
                                  RhoEndpointVec *sends,
                                  RhoEndpointVec *recvs) {
    for (uint32_t i=0u;i<components->len;i++)
        if (!rho_collect_endpoint(components,i,sends,recvs)) return false;
    if (sends->len > 1) {
        qsort(sends->storage.linear, sends->len, sizeof(RhoEndpoint),
              rho_endpoint_cmp_key);
    }
    if (recvs->len > 1) {
        qsort(recvs->storage.linear, recvs->len, sizeof(RhoEndpoint),
              rho_endpoint_cmp_key);
    }
    return true;
}

/* The determinate fast path only needs to know whether there is exactly one
 * enabled COMM; it does not need a lexical ordering of every channel.  Keep
 * direct @(val(payload)) channels in their constructor representation so
 * equality can be decided lazily.  Other channel forms retain the ordinary
 * textual key. */
static bool rho_endpoint_prepare_single_comm_key(RhoEndpoint *endpoint) {
    Atom *payload;

    if (!endpoint || endpoint->view.nargs == 0u) return false;
    payload = rho_endpoint_direct_value_channel(endpoint->view);
    if (payload) {
        endpoint->direct_value_channel = payload;
        return true;
    }
    endpoint->key = rho_key_name(endpoint->view.args[0]);
    return endpoint->key != NULL;
}

static bool rho_collect_endpoints_for_single_comm(
    RhoAtomVec *components, RhoEndpointVec *sends,
    RhoEndpointVec *recvs) {
    for (uint32_t i = 0u; i < components->len; i++) {
        RhoView view = rho_view(components->items[i]);
        RhoEndpointVec *target = NULL;
        if (view.kind == RHO_SEND && view.nargs == 2u) {
            target = sends;
        } else if ((view.kind == RHO_RECV ||
                    view.kind == RHO_RECV_PERSISTENT) &&
                   view.nargs == 3u) {
            target = recvs;
        }
        if (!target) continue;
        if (!rho_endpoint_vec_push(target, i, view, NULL,true)) return false;
        if (!rho_endpoint_prepare_single_comm_key(
                rho_endpoint_at(target,target->len-1u))) {
            return false;
        }
    }
    return true;
}

static bool rho_single_comm_channels_equal(
    const RhoEndpoint *left, const RhoEndpoint *right, bool *out_equal) {
    static atomic_int oracle_mode = ATOMIC_VAR_INIT(-1);
    int mode = atomic_load_explicit(&oracle_mode, memory_order_relaxed);
    char *left_key;
    char *right_key;

    if (!left || !right || !out_equal) return false;
    if (mode < 0) {
        int selected = getenv("CETTA_RHO_STRUCTURAL_KEY_ORACLE") ? 1 : 0;
        int expected = -1;
        if (!atomic_compare_exchange_strong_explicit(
                &oracle_mode, &expected, selected,
                memory_order_relaxed, memory_order_relaxed)) {
            selected = expected;
        }
        mode = selected;
    }
    *out_equal = false;
    if (left->direct_value_channel && right->direct_value_channel) {
        RhoAtomTextRelation relation =
            rho_atom_text_structural_relation(
                left->direct_value_channel,
                right->direct_value_channel, true);
        if (relation != RHO_ATOM_TEXT_RELATION_UNKNOWN) {
            bool structural_equal =
                relation == RHO_ATOM_TEXT_RELATION_EQUAL;
            if (mode != 0) {
                bool text_equal;
                left_key = rho_key_name(left->view.args[0]);
                right_key = rho_key_name(right->view.args[0]);
                if (!left_key || !right_key) {
                    free(left_key);
                    free(right_key);
                    return false;
                }
                text_equal = strcmp(left_key, right_key) == 0;
                free(left_key);
                free(right_key);
                if (structural_equal != text_equal) {
                    rho_validation_set(
                        "rho structural channel equality disagrees with text oracle");
                    return false;
                }
            }
            *out_equal = structural_equal;
            return true;
        }
    } else if (!left->direct_value_channel &&
               !right->direct_value_channel) {
        if (!left->key || !right->key) return false;
        *out_equal = strcmp(left->key, right->key) == 0;
        return true;
    }

    /* Structural UNKNOWN and mixed channel forms defer to the existing exact
     * textual channel-key oracle. */
    left_key = rho_key_name(left->view.args[0]);
    right_key = rho_key_name(right->view.args[0]);
    if (!left_key || !right_key) {
        free(left_key);
        free(right_key);
        return false;
    }
    *out_equal = strcmp(left_key, right_key) == 0;
    free(left_key);
    free(right_key);
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
    CettaEvalPayloadFrame *source_frame;
    Arena *source_arena;
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

static StateCell *rho_state_visible(StateCell *cell) {
    return eval_resolve_state_read ? eval_resolve_state_read(cell) : cell;
}

static bool rho_state_source_descriptor(const RhoStateCloneMap *map,StateCell *cell,
    Atom **value,Atom **content_type) {
    if (!cell || !value || !content_type) return false;
    if (map->source_frame && eval_payload_frame_source_state)
        return eval_payload_frame_source_state(map->source_frame,map->source_arena,
            cell,value,content_type);
    *value=cell->value;*content_type=cell->content_type;
    return true;
}

/* Add `orig` to the map (deduped), creating its copy-on-write overlay. */
static bool rho_overlay_map_bind_pair(RhoOverlayMap *map,Space *orig,Space *overlay);

static bool rho_overlay_map_add(RhoOverlayMap *map, Space *orig) {
    Space *ov;
    if (!orig || rho_overlay_for(map, orig))
        return true;
    Space *visible=eval_resolve_space_read ? eval_resolve_space_read(orig) : orig;
    if (!visible) return false;
    ov=rho_overlay_for(map,visible);
    if (ov) return rho_overlay_map_bind_pair(map,orig,ov);
    int needed=visible==orig ? 1 : 2;
    if (map->len>INT_MAX-needed || !rho_overlay_map_ensure(map,map->len+needed))
        return false;
    ov = cetta_malloc(sizeof(Space));
    if (!ov)
        return false;
    if (!space_init_overlay_local(ov, visible)) {
        free(ov);
        return false;
    }
    if (!eval_payload_track_scratch_space(ov)) {
        space_free(ov);
        free(ov);
        return false;
    }
    /* Raw references and the parent's already visible reference name one
     * child view, including when both occur in the same payload graph. */
    return rho_overlay_map_bind_pair(map,orig,ov) &&
        (visible==orig || rho_overlay_map_bind_pair(map,visible,ov));
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
    if (map->len==INT_MAX || !rho_overlay_map_ensure(map, map->len + 1))
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
    if (!map) return false;
    if (!orig)
        return true;
    /* Discovery precedes materialization. A recorded source with a pending
     * NULL image is already known, including on a resource backedge. */
    for (int i=0;i<map->len;i++)
        if (map->orig[i]==orig) return true;
    StateCell *visible=rho_state_visible(orig);
    if (!visible) return false;
    bool known=false;
    for (int i=0;i<map->len;i++) known=known || map->orig[i]==visible;
    int needed=(known ? 0 : 1)+(orig!=visible ? 1 : 0);
    if (map->len>INT_MAX-needed || !rho_state_clone_map_ensure(map,map->len+needed)) return false;
    if (!known) {
        map->orig[map->len]=visible;map->clone[map->len++]=NULL;
    }
    if (orig!=visible) {
        map->orig[map->len]=orig;map->clone[map->len++]=NULL;
    }
    if (inserted) *inserted=!known;
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
            cell=rho_state_visible(cell);
            if (inserted && cell) {
                Atom *value=NULL,*content_type=NULL;
                if (!rho_state_source_descriptor(map,cell,&value,&content_type) ||
                    !rho_collect_state_ptrs_into_map(map,value) ||
                    !rho_collect_state_ptrs_into_map(map,content_type)) return false;
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
        StateCell *visible=rho_state_visible(map->orig[i]);
        for (int j=0;j<i;j++)
            if (rho_state_visible(map->orig[j])==visible) {
                map->clone[i]=map->clone[j];break;
            }
        if (map->clone[i]) continue;
        map->clone[i] = cetta_malloc(sizeof(StateCell));
        if (!map->clone[i])
            return false;
        map->clone[i]->value = NULL;
        map->clone[i]->content_type = NULL;
        map->clone[i]->payload_owner_epoch = owner_epoch;
        map->clone[i]->payload_export_owner_epoch = 0;
        if (!eval_payload_track_scratch_state(map->clone[i])) {
            free(map->clone[i]);
            map->clone[i] = NULL;
            return false;
        }
    }
    for (int i = 0; i < map->len; i++) {
        StateCell *orig = rho_state_visible(map->orig[i]);
        StateCell *clone = map->clone[i];
        if (!orig || !clone)
            continue;
        bool filled=false;
        for (int j=0;j<i;j++) filled=filled || map->clone[j]==clone;
        if (filled) continue;
        Atom *value=NULL,*content_type=NULL;
        if (!rho_state_source_descriptor(map,orig,&value,&content_type)) return false;
        if (value) {
            clone->value =
                rho_atom_rebind_resources(owner,value,space_map,map);
            if (!clone->value)
                return false;
        }
        if (content_type) {
            clone->content_type = rho_atom_rebind_resources(
                owner,content_type,space_map,map);
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
            if (!src->entries[i].value) {
                continue;
            }
            value = rho_atom_rebind_resources(
                arena, src->entries[i].value, space_map, state_map);
            if (!value)
                return false;
            if (!registry_copy_entry(dst, src, i, value)) return false;
        }
    }
    if (g_builtin_syms.self != SYMBOL_ID_NONE && self_overlay &&
        !registry_lookup_id(dst,g_builtin_syms.self)) {
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
        return atom_symbol_copy(arena, atom);
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
    return atom_expr_rebuild(arena, atom, elems, atom->expr.len);
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
    const RhocalcPayloadProvider *provider;
    void *provider_job;
    CettaCount result_index;
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
    space_init_with_universe(stable,
                             visible ? visible->native.universe : NULL);
    stable->kind = visible->kind;
    space_set_strict(stable, visible->strict);
    stable->payload_owner_epoch = 0;
    stable->payload_export_owner_epoch = export_owner_epoch;
    /* Cleanup ownership precedes recursive exposure. A later refusal can
     * leave backedges from other private shells to this initialized space. */
    if (!eval_track_new_space(stable)) {
        space_free(stable);
        return NULL;
    }
    if (!rho_promoted_space_bind(promoted_spaces, visible, stable,
                                 export_owner_epoch, alias_fault))
        return NULL;
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
    Atom *value=visible->value, *content_type=visible->content_type;
    if (promoted_states->provider && promoted_states->provider->result_state &&
        !promoted_states->provider->result_state(promoted_states->provider_job,
            promoted_states->result_index,visible,&value,&content_type))
        return NULL;
    if (value) {
        stable->value =
            rho_promote_payload_atom(persistent, persistent, value,
                                     space_map, state_map,
                                     promoted_spaces, promoted_states,
                                     owner_epoch, export_owner_epoch,
                                     alias_fault);
        if (!stable->value)
            return NULL;
    }
    if (content_type) {
        stable->content_type =
            rho_promote_payload_atom(persistent, persistent,
                                     content_type,
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
        /* A stored row can reveal a resource absent from the initial root
         * inventory. Its publication still uses the same canonical image
         * and affine owner check before descending into its actual slots. */
        if (atom->ground.gkind == GV_SPACE) {
            Space *orig = (Space *)atom->ground.ptr;
            Space *visible = orig;
            Space *overlay = space_map ? rho_overlay_for(space_map, orig) : NULL;
            if (overlay)
                visible = overlay;
            if (!overlay && eval_resolve_space_read) visible=eval_resolve_space_read(visible);
            Space *stable = rho_promote_payload_space(persistent,visible,
                space_map,state_map,promoted_spaces,promoted_states,
                owner_epoch,export_owner_epoch,alias_fault);
            return stable?atom_space(arena,stable):NULL;
        } else if (atom->ground.gkind == GV_STATE) {
            StateCell *orig = (StateCell *)atom->ground.ptr;
            StateCell *visible = orig;
            StateCell *clone =
                state_map ? rho_state_clone_for(state_map, orig) : NULL;
            if (clone)
                visible = clone;
            if (!clone) visible=rho_state_visible(visible);
            StateCell *stable = rho_promote_payload_state(persistent,visible,
                space_map,state_map,promoted_spaces,promoted_states,
                owner_epoch,export_owner_epoch,alias_fault);
            return stable?atom_state(arena,stable):NULL;
        }
        return atom_deep_copy(arena, atom);
    }
    if (atom->kind == ATOM_SYMBOL)
        return atom_symbol_copy(arena, atom);
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
    return atom_expr_rebuild(arena, atom, elems, atom->expr.len);
}

/* Evaluate a deferred payload against a sibling-isolated snapshot of the
 * mutable environment. Shared spaces are rebound to per-payload overlays, and
 * explicitly reachable state cells are rebound to payload-owned clones. Reads
 * see the frozen shared bases plus local scratch; writes stay local to the
 * payload and never escape to a sibling. Escaping local spaces are materialized
 * as owned results; shared bases are never implicitly committed. */
typedef enum {
    RHO_PAYLOAD_PREPARE, RHO_PAYLOAD_EVALUATE, RHO_PAYLOAD_PROMOTE,
    RHO_PAYLOAD_COMPLETE, RHO_PAYLOAD_REFUSED
} RhoPayloadPhase;
typedef struct {
    Arena *arena, *persistent;
    RhocalcEvalContext context;
    CettaEvalPayloadFrame *frame;
    RhoOverlayMap map;
    RhoStateCloneMap state_map;
    RhoPromotedSpaceMap promoted_spaces;
    RhoPromotedStateMap promoted_states;
    Registry local_registry;
    ResultSet results;
    Atom *payload;
    Space *self_overlay;
    uint64_t owner_epoch;
    void *provider_job;
    RhoPayloadPhase phase;
    bool alias_fault, running;
} RhoPayloadJob;

static RhoPayloadJob *rho_payload_open(Arena *arena,
    const RhocalcEvalContext *context,Atom *expression) {
    if (!arena || !context || !context->space || !expression) return NULL;
    const RhocalcPayloadProvider *provider=context->payload_provider;
    if (!provider && eval_payload_synchronous_provider)
        provider=eval_payload_synchronous_provider();
    if (provider && (!provider->open || !provider->step || !provider->close)) return NULL;
    RhoPayloadJob *job=calloc(1u,sizeof(*job));
    if (!job) return NULL;
    job->arena=arena;job->context=*context;
    job->context.payload_provider=provider;
    job->persistent=context->persistent_arena?context->persistent_arena:
        eval_current_persistent_arena();
    if (!job->persistent) job->persistent=arena;
    job->payload=expression;
    job->owner_epoch=eval_next_payload_owner_epoch();
    result_set_init(&job->results);
    job->frame=eval_payload_frame_open(job->persistent);
    if (!job->frame) {free(job);return NULL;}
    return job;
}
static bool rho_payload_prepare(RhoPayloadJob *job) {
    job->payload=eval_open_payload_code?eval_open_payload_code(job->arena,job->payload):
        rho_unquote_payload_atom(job->payload);
    if (!job->payload) return false;
    job->state_map.source_frame=job->frame;
    job->state_map.source_arena=job->persistent;
    /* Overlay the self space, every registry-bound space, and every baked space
     * literal in the payload term. State cells are rebound separately below. */
    if (!rho_overlay_map_add(&job->map, job->context.space))
        return false;
    if (job->context.registry) {
        for (uint32_t i = 0; i < job->context.registry->len; i++) {
            Atom *v = job->context.registry->entries[i].value;
            if (v && v->kind == ATOM_GROUNDED && v->ground.gkind == GV_SPACE) {
                if (!rho_overlay_map_add(&job->map, (Space *)v->ground.ptr))
                    return false;
            }
            if (v && !rho_collect_state_ptrs_into_map(&job->state_map, v))
                return false;
        }
    }
    if (!rho_collect_space_ptrs_into_map(&job->map, job->payload))
        return false;
    if (!rho_collect_state_ptrs_into_map(&job->state_map, job->payload))
        return false;
    for (int i = 0; i < job->state_map.len; i++) {
        StateCell *cell = rho_state_visible(job->state_map.orig[i]);
        if (cell) {
            Atom *value=NULL,*content_type=NULL;
            if (!rho_state_source_descriptor(&job->state_map,cell,&value,&content_type) ||
                !rho_collect_space_ptrs_into_map(&job->map,value) ||
                !rho_collect_space_ptrs_into_map(&job->map,content_type)) return false;
        }
    }

    job->self_overlay = rho_overlay_for(&job->map, job->context.space);

    if (!rho_state_clone_map_materialize(&job->state_map, job->persistent, &job->map,
                                         job->owner_epoch))
        return false;
    for (int i = 0; i < job->map.len; i++) {
        if (!rho_overlay_map_bind_pair(&job->map, job->map.orig[i], job->map.overlay[i]) ||
            !eval_payload_note_space_redirect(job->map.orig[i], job->map.overlay[i]))
            return false;
    }
    for (int i = 0; i < job->state_map.len; i++) {
        if (!rho_state_clone_map_bind_pair(&job->state_map, job->state_map.orig[i],
                                           job->state_map.clone[i]) ||
            !eval_payload_note_state_redirect(job->state_map.orig[i],
                                              job->state_map.clone[i]))
            return false;
    }
    if (!rho_eval_registry_init(&job->local_registry, job->context.registry, &job->map,
                                &job->state_map, job->self_overlay, job->arena))
        return false;

    job->payload = rho_atom_rebind_resources(job->arena, job->payload, &job->map, &job->state_map);
    if (!job->payload)
        return false;


    if (job->context.payload_provider) {
        job->provider_job=job->context.payload_provider->open(job->self_overlay,
            job->arena,job->persistent,&job->local_registry,job->payload);
        if (!job->provider_job) return false;
    }
    return true;
}
static bool rho_payload_promote(RhoPayloadJob *job) {
    ResultSet *out=&job->results;
    for (CettaCount i = 0; i < eval_payload_space_redirect_count(); i++) {
        Space *orig = NULL;
        Space *redirect = NULL;
        if (!eval_payload_space_redirect_at(i, &orig, &redirect) ||
            !rho_overlay_map_bind_pair(&job->map, orig, redirect))
            return false;
    }
    for (CettaCount i = 0; i < eval_payload_state_redirect_count(); i++) {
        StateCell *orig = NULL;
        StateCell *redirect = NULL;
        if (!eval_payload_state_redirect_at(i, &orig, &redirect) ||
            !rho_state_clone_map_bind_pair(&job->state_map, orig, redirect))
            return false;
    }
    for (CettaCount r = 0; r < out->len; r++) {
        out->items[r] = rho_atom_rebind_resources(job->arena, out->items[r], &job->map,
                                                  &job->state_map);
        if (!out->items[r])
            return false;
    }

    job->promoted_states.provider=job->context.payload_provider;
    job->promoted_states.provider_job=job->provider_job;
    for (CettaCount r = 0; r < out->len; r++) {
        job->promoted_states.result_index=r;
        out->items[r] =
            rho_promote_payload_atom(job->arena, job->persistent, out->items[r],
                                     &job->map, &job->state_map,
                                     &job->promoted_spaces, &job->promoted_states,
                                     job->owner_epoch,
                                     (uint64_t)r + 1u,
                                     &job->alias_fault);
        if (!out->items[r]) {
            if (job->alias_fault) {
                if (job->context.payload_failure)
                    *job->context.payload_failure =
                        RHOCALC_PAYLOAD_FAILURE_OWNED_EXPORT_ALIASED;
                rho_validation_set("PayloadOwnedExportAliased");
            }
            return false;
        }
    }


    return true;
}
/* One transaction phase or one provider grant. The selected frame is always
 * left before returning, including refusal. Setup and promotion retain their
 * own atomic service cost rather than pretending to be one primitive step. */
static RhocalcPayloadStatus rho_payload_step(RhoPayloadJob *job,uint64_t allowance) {
    if (!job || job->running || job->phase==RHO_PAYLOAD_REFUSED)
        return RHOCALC_PAYLOAD_FAILED;
    if (job->phase==RHO_PAYLOAD_COMPLETE) return RHOCALC_PAYLOAD_DONE;
    if (!allowance) return RHOCALC_PAYLOAD_PENDING;
    CettaEvalPayloadScope scope={0};
    if (!eval_payload_frame_enter(job->frame,&scope)) return RHOCALC_PAYLOAD_FAILED;
    CettaLibraryContext *library=eval_swap_library_context(job->context.library_context);
    bool transactional=eval_set_payload_transactional(true);
    uint64_t epoch=eval_set_payload_owner_epoch(job->owner_epoch);
    job->running=true;
    bool okay=true;
    if (job->phase==RHO_PAYLOAD_PREPARE) {
        okay=rho_payload_prepare(job);
        if (okay) job->phase=RHO_PAYLOAD_EVALUATE;
    } else if (job->phase==RHO_PAYLOAD_EVALUATE) {
        RhocalcPayloadStatus status;
        if (job->context.payload_provider)
            status=job->context.payload_provider->step(job->provider_job,allowance,&job->results);
        else {
            CettaEvalCompletion completion=eval_top_with_registry_completion(job->self_overlay,
                job->arena,job->persistent,&job->local_registry,job->payload,&job->results);
            status=completion==CETTA_EVAL_COMPLETE?RHOCALC_PAYLOAD_DONE:RHOCALC_PAYLOAD_FAILED;
        }
        okay=status!=RHOCALC_PAYLOAD_FAILED;
        if (status==RHOCALC_PAYLOAD_DONE) job->phase=RHO_PAYLOAD_PROMOTE;
    } else if (job->phase==RHO_PAYLOAD_PROMOTE) {
        okay=rho_payload_promote(job);
        if (okay) job->phase=RHO_PAYLOAD_COMPLETE;
    }
    if (!okay) job->phase=RHO_PAYLOAD_REFUSED;
    job->running=false;
    eval_set_payload_owner_epoch(epoch);eval_set_payload_transactional(transactional);
    eval_swap_library_context(library);eval_payload_frame_leave(&scope);
    return job->phase==RHO_PAYLOAD_COMPLETE?RHOCALC_PAYLOAD_DONE:
        job->phase==RHO_PAYLOAD_REFUSED?RHOCALC_PAYLOAD_FAILED:RHOCALC_PAYLOAD_PENDING;
}
static void rho_payload_close(RhoPayloadJob *job) {
    if (!job) return;
    assert(!job->running);
    if (job->provider_job) {
        CettaEvalPayloadScope scope={0};
        bool entered=eval_payload_frame_enter(job->frame,&scope);
        assert(entered);(void)entered;
        CettaLibraryContext *library=eval_swap_library_context(job->context.library_context);
        bool transactional=eval_set_payload_transactional(true);
        uint64_t epoch=eval_set_payload_owner_epoch(job->owner_epoch);
        job->context.payload_provider->close(job->provider_job);
        eval_set_payload_owner_epoch(epoch);eval_set_payload_transactional(transactional);
        eval_swap_library_context(library);eval_payload_frame_leave(&scope);
    }
    rho_promoted_space_map_free(&job->promoted_spaces);
    rho_promoted_state_map_free(&job->promoted_states);
    rho_overlay_map_free(&job->map);rho_state_clone_map_free(&job->state_map);
    registry_free(&job->local_registry);result_set_free(&job->results);
    eval_payload_frame_close(job->frame);free(job);
}
static bool rho_eval_payload_results(Arena *arena,
    const RhocalcEvalContext *context,Atom *expression,ResultSet *out) {
    if (!out) return false;
    RhoPayloadJob *job=rho_payload_open(arena,context,expression);
    if (!job) return false;
    RhocalcPayloadStatus status;
    do {status=rho_payload_step(job,UINT64_MAX);} while(status==RHOCALC_PAYLOAD_PENDING);
    bool okay=status==RHOCALC_PAYLOAD_DONE;
    if (okay) for (CettaCount i=0u;i<job->results.len;i++)
        result_set_add(out,job->results.items[i]);
    rho_payload_close(job);return okay;
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
            Atom *body = rho_subst_payload_proc(arena, recv.args[2],
                recv.args[1]->var_id, payload_results.items[i]);
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
    if (!replacement) return false;
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
 * fires the key-least successor; it streams candidate keys against the best
 * key so far instead of materializing and sorting the whole frontier.  The
 * rotating scheduler cycles through the frontier for fairness probes. */

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
    rho_process_key_cache_init(
        &machine->process_key_cache, machine->arena, arena);
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
    rho_process_key_cache_free(&machine->process_key_cache);
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
    bool ok = machine && machine->eval_context
        ? rhocalc_process_well_formed_with_eval_payloads(proc)
        : rhocalc_process_well_formed(proc);
    if (!ok) return false;
    machine->current = rho_normalize_proc(machine->arena, proc);
    machine->current_canonical_order = machine->current != NULL;
    return machine->current!=NULL;
}

#define RHO_MACHINE_COMPACTION_INTERVAL 64u

static bool rho_machine_compaction_enabled(const RhoMachine *machine) {
    return machine && machine->compaction_safe &&
           !getenv("CETTA_RHO_NO_GENERATION_COMPACTION");
}

static bool rho_machine_commit_compaction(RhoMachine *machine, Atom *copy) {
    uint32_t next_index;
    Arena *next_arena;
    Arena *old_arena;
    if (!machine || !machine->current || !copy ||
        !machine->process_arenas_initialized) {
        return false;
    }
    next_index = 1u - machine->active_process_arena;
    next_arena = &machine->process_arenas[next_index];
    old_arena = &machine->process_arenas[machine->active_process_arena];
    rho_process_key_cache_clear(
        &machine->process_key_cache, next_arena);
    machine->current = copy;
    machine->active_process_arena = next_index;
    machine->arena = next_arena;
    machine->reductions_since_compaction = 0u;
    /* The old generation ends here; its struct is opened again at once as
     * the next compaction's destination, with the same allocation profile. */
    HashConsTable *hashcons=old_arena->hashcons;
    arena_free(old_arena);
    rho_process_arena_open(old_arena);
    arena_set_hashcons(old_arena,hashcons);
    return true;
}

static bool rho_machine_compact_current(RhoMachine *machine) {
    if (!machine || !machine->current || !machine->process_arenas_initialized) return false;
    Atom *copy=atom_deep_copy(&machine->process_arenas[1u-machine->active_process_arena],
        machine->current);
    return copy && rho_machine_commit_compaction(machine,copy);
}

static Atom *rho_machine_export_current(RhoMachine *machine) {
    Atom *copy;

    if (!machine || !machine->result_arena || !machine->current) return NULL;
    copy = atom_deep_copy(machine->result_arena, machine->current);
    return copy ? rho_normalize_proc(machine->result_arena, copy) : NULL;
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
typedef struct {
    RhoKeyProduct *product;
    bool owned;
} RhoComponentKey;

static RhoComponentKey *rho_component_key_row(const RhoSortBuffer *keys, uint32_t index) {
    return rho_sort_buffer_at(keys,index,sizeof(RhoComponentKey));
}

static RhoKeyProduct *rho_component_key_at(const RhoSortBuffer *keys, uint32_t index) {
    return rho_component_key_row(keys,index)->product;
}

static bool rho_reaction_key_against_bound(
    RhoAtomVec *components, RhoSortBuffer *component_keys,
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
    bool retain_recv;
    bool ok = true;

    if (!components || !component_keys || !body || !out_key || !out_order ||
        send_index >= components->len || recv_index >= components->len ||
        send_index == recv_index) {
        return false;
    }
    *out_key = NULL;
    *out_order = 0;
    retain_recv =
        rho_view(components->items[recv_index]).kind == RHO_RECV_PERSISTENT;

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

    total = components->len - (retain_recv ? 1u : 2u) + body_len;
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
                   (base_pos == send_index ||
                    (base_pos == recv_index && !retain_recv))) {
                base_pos++;
            }
            Atom *next;
            if (base_pos >= components->len) {
                next = body_items[body_pos++].atom;
            } else if (body_pos >= body_len ||
                       strcmp(rho_component_key_at(component_keys,base_pos)->text,
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
                ok = key.data != NULL && !key.failed;
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
    if (!ok || key.failed) {
        free(key.data);
        return false;
    }
    *out_key = key.data;
    *out_order = cmp.order;
    return true;
}

typedef struct {
    const char *left,*right;
    size_t offset;
    int order;
    bool done;
} RhoKeyProbe;

static bool rho_key_probe_init(RhoKeyProbe *probe,const char *left,const char *right) {
    if (!left || !right) return false;
    *probe=(RhoKeyProbe){.left=left,.right=right,.done=left==right};
    return true;
}

static void rho_key_probe_advance(RhoKeyProbe *probe) {
    if (probe->done) return;
    unsigned char left=(unsigned char)probe->left[probe->offset];
    unsigned char right=(unsigned char)probe->right[probe->offset];
    if (left==right && left) probe->offset++;
    else {
        probe->order=left<right ? -1 : left>right ? 1 : 0;
        probe->done=true;
    }
}

typedef struct {
    RhoAtomVec *components;
    RhoSortBuffer *component_keys;
    uint32_t send_index;
    uint32_t recv_index;
    Atom *body;
    RhoSortBuffer body_items;
    uint32_t body_len;
    uint32_t total;
    bool retain_recv;
} RhoReactionKeyView;

/* Pending is not an absent component or an end byte. Both producers below
 * retain their own progress; a client may keep one side's value while the
 * other side is still computing. */
typedef enum {
    RHO_KEY_PENDING, RHO_KEY_VALUE, RHO_KEY_END, RHO_KEY_REFUSED
} RhoKeyStep;

/* A borrowed observation of one actual source occurrence. Body ordinals
 * address the original unsorted body, even when its prepared rows are sorted.
 * Neither equal keys nor shared atom pointers identify two occurrences. */
typedef struct {
    Atom *atom;
    const char *key;
    uint32_t binders, ordinal;
    bool from_body;
} RhoReactionRow;

typedef struct {
    const RhoReactionKeyView *view;
    uint32_t base_pos, body_pos, emitted;
    RhoKeyProbe merge;
    bool merge_live;
} RhoReactionComponentCursor;

typedef struct {
    RhoReactionComponentCursor components;
    Atom *current;
    const char *current_local_key;
    RhoKeyCursor *shifted;
    size_t current_key_pos;
    uint32_t next_alpha_index, current_binder_count;
    bool whole_key, done;
    uint8_t prefix_pos;
} RhoReactionTextCursor;

static void rho_reaction_key_view_free(RhoReactionKeyView *view) {
    if (!view) return;
    for (uint32_t i = 0u; i < view->body_len; i++) {
        free(rho_keyed_row(&view->body_items, i)->key);
    }
    rho_sort_buffer_free(&view->body_items);
    *view = (RhoReactionKeyView){0};
}

static bool rho_reaction_key_view_init(
    RhoReactionKeyView *view, RhoAtomVec *components,
    RhoSortBuffer *component_keys, uint32_t send_index,
    uint32_t recv_index, Atom *body) {
    if (!view || !components || !component_keys || !body ||
        send_index>=components->len || recv_index>=components->len || send_index==recv_index)
        return false;
    *view=(RhoReactionKeyView){.components=components,.component_keys=component_keys,
        .send_index=send_index,.recv_index=recv_index,.body=body,.body_items={.paged=true},
        .retain_recv=rho_view(components->items[recv_index]).kind==RHO_RECV_PERSISTENT};
    RhoView body_view=rho_view(body);
    uint32_t length=body_view.kind==RHO_PAR ? body_view.nargs : body_view.kind==RHO_NIL ? 0u : 1u;
    uint32_t retained=components->len-(view->retain_recv ? 1u : 2u);
    if (length>UINT32_MAX-retained) {
        rho_validation_set("rho reaction component count exhausted");
        return false;
    }
    view->total=retained+length;
    return true;
}

static void rho_reaction_component_cursor_init(
    RhoReactionComponentCursor *cursor,
    const RhoReactionKeyView *view) {
    *cursor = (RhoReactionComponentCursor){.view = view};
}

static RhoKeyStep rho_reaction_component_cursor_next(
    RhoReactionComponentCursor *cursor, RhoReactionRow *out) {
    if (!cursor || !cursor->view || !out)
        return RHO_KEY_REFUSED;
    const RhoReactionKeyView *view=cursor->view;
    if (cursor->emitted>=view->total) return RHO_KEY_END;
    /* Only the two consumed endpoint positions can be skipped here. */
    while (cursor->base_pos<view->components->len &&
           (cursor->base_pos==view->send_index ||
            (cursor->base_pos==view->recv_index && !view->retain_recv)))
        cursor->base_pos++;
    bool base=cursor->base_pos<view->components->len;
    bool body=cursor->body_pos<view->body_len;
    if (!base && !body) return RHO_KEY_REFUSED;
    if (base && body) {
        if (!cursor->merge_live) {
            if (!rho_key_probe_init(&cursor->merge,
                    rho_component_key_at(view->component_keys,cursor->base_pos)->text,
                    rho_keyed_row(&view->body_items, cursor->body_pos)->key)) return RHO_KEY_REFUSED;
            cursor->merge_live=true;
        }
        rho_key_probe_advance(&cursor->merge);
        if (!cursor->merge.done) return RHO_KEY_PENDING;
        base=cursor->merge.order<=0;
        cursor->merge_live=false;
    }
    if (base) {
        uint32_t index=cursor->base_pos++;
        RhoKeyProduct *product=rho_component_key_at(view->component_keys,index);
        *out=(RhoReactionRow){.atom=view->components->items[index],
            .key=product->text,.binders=product->binders,.ordinal=index};
    } else {
        const RhoKeyedAtom *item=rho_keyed_row(&view->body_items,cursor->body_pos++);
        *out=(RhoReactionRow){.atom=item->atom,.key=item->key,.binders=item->binders,
            .ordinal=item->ordinal,.from_body=true};
    }
    cursor->emitted++;
    return RHO_KEY_VALUE;
}

static void rho_reaction_text_cursor_free(RhoReactionTextCursor *cursor) {
    if (!cursor) return;
    rho_key_cursor_free(cursor->shifted);
    *cursor=(RhoReactionTextCursor){0};
}

static bool rho_reaction_text_cursor_prepare_current(RhoReactionTextCursor *cursor) {
    if (!cursor->current || !cursor->current_local_key ||
            cursor->current_binder_count>UINT32_MAX-cursor->next_alpha_index) return false;
    if (cursor->next_alpha_index) {
        cursor->shifted=rho_key_cursor_new_at(cursor->current,false,cursor->next_alpha_index);
        if (!cursor->shifted) return false;
    }
    cursor->current_key_pos=0u;
    return true;
}

static bool rho_reaction_text_cursor_init_at_current(
    RhoReactionTextCursor *cursor,
    const RhoReactionComponentCursor *components_after_current,
    Atom *current, const char *current_local_key, uint32_t binders,
    uint32_t next_alpha_index) {
    if (!cursor || !components_after_current || !current || !current_local_key) return false;
    *cursor=(RhoReactionTextCursor){.components=*components_after_current,
        .current=current,.current_local_key=current_local_key,
        .current_binder_count=binders,.next_alpha_index=next_alpha_index};
    return rho_reaction_text_cursor_prepare_current(cursor);
}

static RhoKeyStep rho_reaction_text_cursor_next_byte(
    RhoReactionTextCursor *cursor, unsigned char *out_byte) {
    if (!cursor || !out_byte || !cursor->components.view) return RHO_KEY_REFUSED;
    if (cursor->done) return RHO_KEY_END;
    uint32_t total=cursor->components.view->total;
    if (cursor->whole_key && !total) {
        *out_byte=(unsigned char)'0'; cursor->done=true;
        return RHO_KEY_VALUE;
    }
    if (cursor->whole_key && total>1u && cursor->prefix_pos<4u) {
        *out_byte=(unsigned char)"par("[cursor->prefix_pos++];
        return RHO_KEY_VALUE;
    }
    if (!cursor->current) {
        RhoReactionRow row={0};
        RhoKeyStep status=rho_reaction_component_cursor_next(&cursor->components,&row);
        if (status!=RHO_KEY_VALUE) return status==RHO_KEY_END ? RHO_KEY_REFUSED : status;
        cursor->current=row.atom; cursor->current_local_key=row.key;
        cursor->current_binder_count=row.binders;
        if (!rho_reaction_text_cursor_prepare_current(cursor)) return RHO_KEY_REFUSED;
        return RHO_KEY_PENDING;
    }
    if (cursor->shifted) {
        if (cursor->current_key_pos<cursor->shifted->text.len) {
            *out_byte=(unsigned char)cursor->shifted->text.data[cursor->current_key_pos++];
            return RHO_KEY_VALUE;
        }
        if (!cursor->shifted->done) {
            if (!rho_key_cursor_advance(cursor->shifted)) return RHO_KEY_REFUSED;
            return RHO_KEY_PENDING;
        }
        if (cursor->shifted->env.next_index!=cursor->next_alpha_index+cursor->current_binder_count)
            return RHO_KEY_REFUSED;
    } else if (cursor->current_local_key[cursor->current_key_pos]) {
        *out_byte=(unsigned char)cursor->current_local_key[cursor->current_key_pos++];
        return RHO_KEY_VALUE;
    }
    cursor->next_alpha_index+=cursor->current_binder_count;
    rho_key_cursor_free(cursor->shifted); cursor->shifted=NULL;
    cursor->current=NULL;
    if (cursor->components.emitted<total) {
        *out_byte=(unsigned char)'|';
        return RHO_KEY_VALUE;
    }
    cursor->done=true;
    if (!cursor->whole_key || total>1u) {
        *out_byte=(unsigned char)')';
        return RHO_KEY_VALUE;
    }
    return RHO_KEY_END;
}

static bool rho_reaction_text_cursor_init_whole(
    RhoReactionTextCursor *cursor,const RhoReactionKeyView *view) {
    if (!cursor || !view) return false;
    *cursor=(RhoReactionTextCursor){.whole_key=true};
    rho_reaction_component_cursor_init(&cursor->components,view);
    return true;
}

static bool rho_reaction_key_view_text_compare(
    const RhoReactionKeyView *left,
    const RhoReactionKeyView *right, int *out_order) {
    char *left_key = NULL;
    char *right_key = NULL;
    int ignored_order = 0;
    bool ok;

    if (!left || !right || !out_order) return false;
    ok = rho_reaction_key_against_bound(
        left->components, left->component_keys,
        left->send_index, left->recv_index, left->body,
        NULL, &left_key, &ignored_order) &&
         rho_reaction_key_against_bound(
        right->components, right->component_keys,
        right->send_index, right->recv_index, right->body,
        NULL, &right_key, &ignored_order);
    if (ok) {
        int order = strcmp(left_key, right_key);
        *out_order = order < 0 ? -1 : order > 0 ? 1 : 0;
    }
    free(left_key);
    free(right_key);
    return ok;
}

typedef enum {
    RHO_COMPARE_COMPONENT,
    RHO_COMPARE_TEXT,
    RHO_COMPARE_FINISH,
    RHO_COMPARE_DONE
} RhoReactionComparePhase;

/* A probe advances each unfinished provider at most once and retains the
 * other provider's completed observation. Component merging compares one
 * byte; shifted keys use the existing retained emitter. Atomic allocation,
 * scalar/payload rendering and cleanup still need separate native prices. */
typedef struct {
    const RhoReactionKeyView *left,*right;
    RhoReactionComponentCursor left_components,right_components;
    RhoReactionTextCursor left_text,right_text;
    uint32_t left_alpha_index,right_alpha_index;
    RhoReactionRow left_row,right_row;
    unsigned char left_byte,right_byte;
    RhoKeyStep left_status,right_status;
    RhoReactionComparePhase phase;
    uint64_t component_probes,text_probes;
    int order;
    bool text_live;
} RhoReactionCompare;

static void rho_reaction_compare_free(RhoReactionCompare *comparison) {
    if (comparison->text_live) {
        rho_reaction_text_cursor_free(&comparison->left_text);
        rho_reaction_text_cursor_free(&comparison->right_text);
    }
    *comparison=(RhoReactionCompare){0};
}

static bool rho_reaction_compare_init(RhoReactionCompare *comparison,
        const RhoReactionKeyView *left,const RhoReactionKeyView *right) {
    *comparison=(RhoReactionCompare){.left=left,.right=right};
    rho_reaction_component_cursor_init(&comparison->left_components,left);
    rho_reaction_component_cursor_init(&comparison->right_components,right);
    if (left->total>1u && right->total>1u) return true;
    comparison->phase=RHO_COMPARE_TEXT;
    comparison->text_live=true;
    return rho_reaction_text_cursor_init_whole(&comparison->left_text,left) &&
        rho_reaction_text_cursor_init_whole(&comparison->right_text,right);
}

static bool rho_reaction_key_view_oracle_enabled(void) {
    static atomic_int oracle_mode=ATOMIC_VAR_INIT(-1);
    int mode=atomic_load_explicit(&oracle_mode,memory_order_relaxed);
    if (mode<0) {
        int selected=getenv("CETTA_RHO_STRUCTURAL_KEY_ORACLE") ? 1 : 0;
        int expected=-1;
        if (!atomic_compare_exchange_strong_explicit(&oracle_mode,&expected,selected,
                memory_order_relaxed,memory_order_relaxed)) selected=expected;
        mode=selected;
    }
    return mode!=0;
}

static bool rho_reaction_compare_advance(RhoReactionCompare *comparison) {
    switch (comparison->phase) {
    case RHO_COMPARE_COMPONENT: {
        comparison->component_probes++;
        if (comparison->left_status==RHO_KEY_PENDING)
            comparison->left_status=rho_reaction_component_cursor_next(&comparison->left_components,
                &comparison->left_row);
        if (comparison->right_status==RHO_KEY_PENDING)
            comparison->right_status=rho_reaction_component_cursor_next(&comparison->right_components,
                &comparison->right_row);
        RhoKeyStep left=comparison->left_status,right=comparison->right_status;
        if (left==RHO_KEY_REFUSED || right==RHO_KEY_REFUSED) return false;
        if (left==RHO_KEY_PENDING || right==RHO_KEY_PENDING) return true;
        if (left==RHO_KEY_END || right==RHO_KEY_END) {
            comparison->order=left==right ? 0 : left==RHO_KEY_END ? -1 : 1;
            comparison->phase=RHO_COMPARE_FINISH;
            return true;
        }
        if (comparison->left_row.atom==comparison->right_row.atom &&
                comparison->left_alpha_index==comparison->right_alpha_index) {
            uint32_t binders=comparison->left_row.binders;
            if (binders!=comparison->right_row.binders ||
                    binders>UINT32_MAX-comparison->left_alpha_index) return false;
            comparison->left_alpha_index+=binders;
            comparison->right_alpha_index+=binders;
            comparison->left_status=comparison->right_status=RHO_KEY_PENDING;
            bool left_end=comparison->left_components.emitted==comparison->left->total;
            bool right_end=comparison->right_components.emitted==comparison->right->total;
            if (left_end || right_end) {
                comparison->order=left_end ? right_end ? 0 : -1 : 1;
                comparison->phase=RHO_COMPARE_FINISH;
            }
            return true;
        }
        comparison->text_live=true;
        if (!rho_reaction_text_cursor_init_at_current(&comparison->left_text,
                &comparison->left_components,comparison->left_row.atom,comparison->left_row.key,
                comparison->left_row.binders,comparison->left_alpha_index) ||
            !rho_reaction_text_cursor_init_at_current(&comparison->right_text,
                &comparison->right_components,comparison->right_row.atom,comparison->right_row.key,
                comparison->right_row.binders,comparison->right_alpha_index)) return false;
        comparison->left_status=comparison->right_status=RHO_KEY_PENDING;
        comparison->phase=RHO_COMPARE_TEXT;
        return true;
    }
    case RHO_COMPARE_TEXT: {
        comparison->text_probes++;
        if (comparison->left_status==RHO_KEY_PENDING)
            comparison->left_status=rho_reaction_text_cursor_next_byte(&comparison->left_text,
                &comparison->left_byte);
        if (comparison->right_status==RHO_KEY_PENDING)
            comparison->right_status=rho_reaction_text_cursor_next_byte(&comparison->right_text,
                &comparison->right_byte);
        RhoKeyStep left=comparison->left_status,right=comparison->right_status;
        if (left==RHO_KEY_REFUSED || right==RHO_KEY_REFUSED) return false;
        if (left==RHO_KEY_PENDING || right==RHO_KEY_PENDING) return true;
        if (left==RHO_KEY_END || right==RHO_KEY_END || comparison->left_byte!=comparison->right_byte) {
            comparison->order=left==RHO_KEY_END || right==RHO_KEY_END
                ? left==right ? 0 : left==RHO_KEY_END ? -1 : 1
                : comparison->left_byte<comparison->right_byte ? -1 : 1;
            comparison->phase=RHO_COMPARE_FINISH;
        } else comparison->left_status=comparison->right_status=RHO_KEY_PENDING;
        return true;
    }
    case RHO_COMPARE_FINISH:
        if (rho_reaction_key_view_oracle_enabled()) {
            int text_order=0;
            if (!rho_reaction_key_view_text_compare(comparison->left,comparison->right,&text_order))
                return false;
            if (comparison->order!=text_order) {
                rho_validation_set("rho structural reaction order disagrees with text oracle");
                return false;
            }
        }
        comparison->phase=RHO_COMPARE_DONE;
        return true;
    case RHO_COMPARE_DONE:
        return true;
    }
    return false;
}

/* Canonical execution needs the least successor, whereas the public frontier
 * API must retain every successor.  Compare candidates in a resettable arena
 * and materialize only the selected successor persistently.  This remains
 * exact for deferred payloads: each candidate is evaluated against its
 * isolated snapshot, and the selected continuation is retained without
 * re-evaluation before it is committed.
 * Equal keys retain the original recv/send/body enumeration order, matching
 * rho_successor_set_acc_finish exactly. */
static bool rho_machine_collect_normalized_components(
    RhoMachine *machine, RhoAtomVec *out, bool *out_normalized);

/* Stable bottom-up endpoint sorting. The primary vector always owns a
 * complete permutation; the unpublished scratch pass borrows its keys. A
 * pause/cancel may discard that pass without freeing or duplicating a key.
 * One advance performs one setup, byte comparison/copy, or pass swap. */
typedef struct {
    RhoSortBuffer items, scratch;
    uint32_t length;
    uint32_t width, output, left, left_end, right, right_end;
    RhoKeyProbe key_probe;
    bool endpoints, probe_initialized, range_ready, done;
} RhoStableSort;

static size_t rho_stable_sort_item_size(const RhoStableSort *sort) {
    return sort->endpoints ? sizeof(RhoEndpoint) : sizeof(RhoKeyedAtom);
}

static const char *rho_stable_sort_key(const RhoStableSort *sort, uint32_t index) {
    return sort->endpoints ? ((RhoEndpoint *)rho_sort_buffer_at(&sort->items,
        index,sizeof(RhoEndpoint)))->key : rho_keyed_row(&sort->items,index)->key;
}

static uint32_t rho_stable_sort_ordinal(const RhoStableSort *sort, uint32_t index) {
    return sort->endpoints ? ((RhoEndpoint *)rho_sort_buffer_at(&sort->items,
        index,sizeof(RhoEndpoint)))->component_index : rho_keyed_row(&sort->items,index)->ordinal;
}

static void rho_stable_sort_free(RhoStableSort *sort) {
    rho_sort_buffer_free(&sort->scratch);
    *sort = (RhoStableSort){0};
}

static bool rho_stable_sort_init(RhoStableSort *sort, RhoSortBuffer items,
                                 uint32_t length, bool endpoints) {
    *sort = (RhoStableSort){.items = items, .length = length,
        .width = 1u, .endpoints = endpoints, .done = length < 2u};
    sort->scratch.paged = items.paged;
    if (sort->done || items.paged) return true;
    size_t size = rho_stable_sort_item_size(sort);
    if (length > SIZE_MAX / size) return false;
    sort->scratch.linear = malloc((size_t)length * size);
    sort->scratch.capacity = length;
    return sort->scratch.linear != NULL;
}

static bool rho_stable_sort_advance(RhoStableSort *sort) {
    if (sort->done) return true;
    if (!sort->range_ready) {
        if (sort->output == sort->length) {
            RhoSortBuffer old = sort->items;
            sort->items = sort->scratch;
            sort->scratch = old;
            if (sort->width >= sort->length - sort->width) sort->done = true;
            else { sort->width *= 2u; sort->output = 0u; }
            return true;
        }
        sort->left = sort->output;
        uint32_t left_count = sort->length - sort->left;
        if (left_count > sort->width) left_count = sort->width;
        sort->left_end = sort->left + left_count;
        sort->right = sort->left_end;
        uint32_t right_count = sort->length - sort->right;
        if (right_count > sort->width) right_count = sort->width;
        sort->right_end = sort->right + right_count;
        sort->range_ready = true; sort->probe_initialized = false;
        return true;
    }
    if (sort->left == sort->left_end && sort->right == sort->right_end) {
        sort->range_ready = false; return true;
    }
    CettaSegmentedStep prepared = rho_sort_buffer_prepare(&sort->scratch,
        sort->output,rho_stable_sort_item_size(sort));
    if (prepared != CETTA_SEGMENTED_READY) return prepared == CETTA_SEGMENTED_PENDING;
    bool take_left = sort->right == sort->right_end;
    if (!take_left && sort->left < sort->left_end) {
        if (!sort->probe_initialized) {
            if (!rho_key_probe_init(&sort->key_probe,
                    rho_stable_sort_key(sort, sort->left),
                    rho_stable_sort_key(sort, sort->right))) return false;
            sort->probe_initialized = true;
        }
        rho_key_probe_advance(&sort->key_probe);
        if (!sort->key_probe.done) return true;
        int order = sort->key_probe.order;
        take_left = order < 0 || (order == 0 &&
            rho_stable_sort_ordinal(sort, sort->left) <=
            rho_stable_sort_ordinal(sort, sort->right));
    }
    size_t size = rho_stable_sort_item_size(sort);
    uint32_t from = take_left ? sort->left++ : sort->right++;
    memcpy(rho_sort_buffer_at(&sort->scratch,sort->output++,size),
           rho_sort_buffer_at(&sort->items,from,size),size);
    sort->probe_initialized = false;
    return true;
}

typedef struct {
    RhoEndpointVec *vector;
    RhoStableSort sort;
    bool done;
} RhoEndpointSort;

static void rho_endpoint_sort_free(RhoEndpointSort *sort) {
    rho_stable_sort_free(&sort->sort);
    *sort = (RhoEndpointSort){0};
}

static bool rho_endpoint_sort_init(RhoEndpointSort *sort, RhoEndpointVec *vector) {
    *sort = (RhoEndpointSort){.vector = vector};
    RhoSortBuffer items=vector->storage;
    bool okay = rho_stable_sort_init(&sort->sort, items, vector->len, true);
    return okay;
}

static bool rho_endpoint_sort_advance(RhoEndpointSort *sort) {
    if (sort->sort.done) {
        if (rho_sort_buffer_has_storage(&sort->sort.scratch))
            rho_sort_buffer_release_one(&sort->sort.scratch);
        else sort->done=true;
        return true;
    }
    bool okay=rho_stable_sort_advance(&sort->sort);
    sort->vector->storage=sort->sort.items;
    return okay;
}

/* Shape admission is shared with the recursive checker. Its scope stack
 * does not restrict free variables; quotation/binder scoping becomes relevant
 * when keys and substitutions are computed, not during grammar admission. */
typedef struct {
    Atom *atom;
    RhoView view;
    uint32_t position;
    bool name, entered;
    unsigned memo_stage;
} RhoCheckFrame;

typedef enum {
    RHO_CHECK_PENDING, RHO_CHECK_COMPLETE, RHO_CHECK_INCOMPATIBLE, RHO_CHECK_REFUSED
} RhoCheckStatus;

typedef struct {
    CettaRecordStack frames;
    RhoCheckFrame pending_frame;
    bool allow_payloads, quiet_shape;
    RhoCheckStatus status;
    CettaNodeMemo memo;
    CettaNodeMemoCursor memo_cursor;
    char diagnostic[sizeof(g_rhocalc_validation_error)];
} RhoCheckCursor;

static RhoCheckFrame *rho_check_frame_at(RhoCheckCursor *cursor, uint32_t index) {
    return cetta_record_stack_at(&cursor->frames,index,&cursor->pending_frame);
}

/* After rejection or caller cancellation, no source interpretation remains.
 * Pure checker records and memo backing retire one actual owner at a time. */
static bool rho_check_cursor_retire_one(RhoCheckCursor *cursor) {
    if (cursor->frames.length) {
        (void)cetta_record_stack_pop(&cursor->frames);
    } else if (cursor->frames.slots.blocks) {
        cetta_record_stack_release_storage_one(&cursor->frames);
    } else if (cursor->memo_cursor.grown.blocks) {
        cetta_segmented_array_release_one(&cursor->memo_cursor.grown);
    } else if (cursor->memo.slots.blocks) {
        cetta_node_memo_release_one(&cursor->memo);
    } else return false;
    return true;
}

static void rho_check_cursor_free(RhoCheckCursor *cursor) {
    cetta_node_memo_cancel(&cursor->memo_cursor);
    cetta_node_memo_free(&cursor->memo);
    while (cursor->frames.length) (void)cetta_record_stack_pop(&cursor->frames);
    (void)cetta_record_stack_free_storage(&cursor->frames);
    *cursor=(RhoCheckCursor){0};
}

static bool rho_check_cursor_push(RhoCheckCursor *cursor, Atom *atom, bool name) {
    if (!cetta_record_stack_request(&cursor->frames,sizeof(RhoCheckFrame))) return false;
    cursor->pending_frame=(RhoCheckFrame){.atom = atom, .name = name};
    return true;
}

static bool rho_check_cursor_step(RhoCheckCursor *cursor, RhocalcRunAccount *account) {
    if (cursor->frames.pending)
        return cetta_record_stack_prepare(&cursor->frames,&cursor->pending_frame)!=CETTA_SEGMENTED_REFUSED;
    if (!cursor->frames.length) return true;
    RhoCheckFrame *frame = rho_check_frame_at(cursor,cursor->frames.length - 1u);
    if (!frame->memo_stage || frame->memo_stage==2u) {
        if (!rho_run_charge(&account->validation_memo_steps,1u)) return false;
        if (!cursor->memo_cursor.memo) {
            if (!frame->memo_stage)
                cetta_node_memo_find_begin(&cursor->memo_cursor,&cursor->memo,
                    frame->atom,frame->name);
            else cetta_node_memo_store_begin(&cursor->memo_cursor,&cursor->memo,
                    frame->atom,frame->name,1u,SIZE_MAX);
        }
        CettaNodeMemoStatus status=cetta_node_memo_advance(&cursor->memo_cursor);
        if (status==CETTA_NODE_MEMO_PENDING) return true;
        if (status==CETTA_NODE_MEMO_REFUSED) {
            rho_validation_set("rho grammar memo refused");
            return false;
        }
        cetta_node_memo_cancel(&cursor->memo_cursor);
        if (status==CETTA_NODE_MEMO_FOUND || status==CETTA_NODE_MEMO_STORED) {
            if (!cetta_record_stack_pop(&cursor->frames)) return false;
        } else frame->memo_stage=1u;
        return true;
    }
    if (!frame->entered) {
        frame->entered = true;
        frame->view = rho_view(frame->atom);
        char diagnostic[sizeof(g_rhocalc_validation_error)];
        if (cursor->quiet_shape)
            memcpy(diagnostic,g_rhocalc_validation_error,sizeof(diagnostic));
        bool accepted = frame->name ? rho_check_name_shape(frame->atom,frame->view)
            : rho_check_proc_shape(frame->atom,frame->view,cursor->allow_payloads);
        if (!accepted) {
            cursor->status=RHO_CHECK_INCOMPATIBLE;
            if (cursor->quiet_shape)
                memcpy(g_rhocalc_validation_error,diagnostic,sizeof(diagnostic));
            else memcpy(cursor->diagnostic,g_rhocalc_validation_error,sizeof(cursor->diagnostic));
        }
        return accepted;
    }
    if (frame->name) {
        if (frame->atom->kind != ATOM_VAR && !frame->position++)
            return rho_check_cursor_push(cursor, frame->view.args[0], false);
    } else {
        RhoView view = frame->view;
        switch (view.kind) {
        case RHO_PAR:
            if (frame->position < view.nargs)
                return rho_check_cursor_push(cursor, view.args[frame->position++], false);
            break;
        case RHO_SEND:
        case RHO_RECV:
        case RHO_RECV_PERSISTENT:
            if (!frame->position++) return rho_check_cursor_push(cursor, view.args[0], true);
            if (frame->position == 2u) return rho_check_cursor_push(cursor,
                view.args[view.kind == RHO_SEND ? 1u : 2u], false);
            break;
        case RHO_DROP:
            if (!frame->position++) return rho_check_cursor_push(cursor, view.args[0], true);
            break;
        default:
            break;
        }
    }
    frame->memo_stage=2u;
    return true;
}

/* Shape rejection is evidence that an atom is data here. Failed preparation
 * or memo growth supplies no such evidence and remains an explicit refusal. */
static RhoCheckStatus rho_check_cursor_advance(
        RhoCheckCursor *cursor, RhocalcRunAccount *account) {
    if (cursor->status==RHO_CHECK_INCOMPATIBLE) {
        if (!cursor->frames.length && !cursor->frames.slots.blocks &&
            (cursor->memo_cursor.grown.blocks || cursor->memo.slots.blocks) &&
            !rho_run_charge(&account->validation_memo_steps,1u)) return RHO_CHECK_REFUSED;
        if (rho_check_cursor_retire_one(cursor)) return RHO_CHECK_PENDING;
        if (!cursor->quiet_shape)
            memcpy(g_rhocalc_validation_error,cursor->diagnostic,sizeof(cursor->diagnostic));
        return RHO_CHECK_INCOMPATIBLE;
    }
    if (cursor->status!=RHO_CHECK_PENDING) return cursor->status;
    if (!cursor->frames.length && cursor->frames.slots.blocks) {
        cetta_record_stack_release_storage_one(&cursor->frames);
        return cursor->status;
    }
    if (!cursor->frames.length && cursor->memo.slots.blocks) {
        if (!rho_run_charge(&account->validation_memo_steps,1u)) cursor->status=RHO_CHECK_REFUSED;
        else cetta_node_memo_release_one(&cursor->memo);
        return cursor->status;
    }
    if (!rho_check_cursor_step(cursor,account)) {
        if (cursor->status==RHO_CHECK_PENDING) cursor->status=RHO_CHECK_REFUSED;
    } else if (!cursor->frames.length && !cursor->frames.slots.blocks && !cursor->memo.slots.blocks) cursor->status=RHO_CHECK_COMPLETE;
    return cursor->status==RHO_CHECK_INCOMPATIBLE ? RHO_CHECK_PENDING : cursor->status;
}

typedef struct {
    Atom *atom, *left, *result;
    RhoView view, flattened;
    RhoSortBuffer items;
    uint32_t length, position, flatten_position, stage;
    RhoKeyCursor *key;
    RhoStableSort sort;
    AtomExprAssembly *assembly;
    bool name;
    unsigned memo_stage;
} RhoNormalizeFrame;

typedef struct {
    CettaRecordStack frames;
    RhoNormalizeFrame pending_frame;
    Arena *arena;
    Atom *returned;
    bool done;
    CettaNodeMemo memo;
    CettaNodeMemoCursor memo_cursor;
} RhoNormalizeCursor;

static RhoNormalizeFrame *rho_normalize_frame_at(RhoNormalizeCursor *cursor, uint32_t index) {
    return cetta_record_stack_at(&cursor->frames,index,&cursor->pending_frame);
}

static void rho_normalize_frame_free(RhoNormalizeFrame *frame) {
    rho_key_cursor_free(frame->key);
    rho_stable_sort_free(&frame->sort);
    atom_expr_assembly_free(frame->assembly);
    for (uint32_t i = 0u; i < frame->length; i++) free(rho_keyed_row(&frame->items,i)->key);
    rho_sort_buffer_free(&frame->items);
    *frame = (RhoNormalizeFrame){0};
}

static bool rho_normalize_record_pop(RhoNormalizeCursor *cursor) {
    if (!cursor->frames.length) return false;
    RhoNormalizeFrame *frame=rho_normalize_frame_at(cursor,cursor->frames.length-1u);
    if (!frame || frame->length || frame->key || frame->assembly ||
        rho_sort_buffer_has_storage(&frame->items) ||
        rho_sort_buffer_has_storage(&frame->sort.scratch)) return false;
    return cetta_record_stack_pop(&cursor->frames);
}

/* Drain the actual primary key permutation, never its borrowed sort scratch.
 * Every yield leaves released entries outside the owned prefix. No source
 * syntax, allocator metadata or cleanup catalogue is traversed to begin. */
static void rho_normalize_cursor_release_one(RhoNormalizeCursor *cursor) {
    assert(cursor && cursor->frames.length);
    RhoNormalizeFrame *frame = rho_normalize_frame_at(cursor,cursor->frames.length - 1u);
    if (frame->length) {
        uint32_t last = --frame->length;
        RhoKeyedAtom *row = rho_keyed_row(&frame->items,last);
        char *key = row->key;
        row->key = NULL;
        free(key);
    } else if (rho_key_cursor_release_one(&frame->key)) {
        /* The nested key owner and its backing retire before this record. */
    } else if (frame->assembly) {
        atom_expr_assembly_free(frame->assembly);frame->assembly=NULL;
    } else if (rho_sort_buffer_has_storage(&frame->sort.scratch)) {
        rho_sort_buffer_release_one(&frame->sort.scratch);
    } else if (rho_sort_buffer_has_storage(&frame->items)) {
        rho_sort_buffer_release_one(&frame->items);
    } else {
        rho_normalize_frame_free(frame);
        (void)rho_normalize_record_pop(cursor);
    }
}

static void rho_normalize_cursor_free(RhoNormalizeCursor *cursor) {
    cetta_node_memo_cancel(&cursor->memo_cursor);
    cetta_node_memo_free(&cursor->memo);
    while (cursor->frames.length) rho_normalize_cursor_release_one(cursor);
    (void)cetta_record_stack_free_storage(&cursor->frames);
    *cursor = (RhoNormalizeCursor){0};
}

static bool rho_normalize_cursor_push(RhoNormalizeCursor *cursor, Atom *atom, bool name) {
    if (!atom) return false;
    if (!cetta_record_stack_request(&cursor->frames,sizeof(RhoNormalizeFrame))) return false;
    cursor->pending_frame=(RhoNormalizeFrame){.atom = atom, .name = name,.items={.paged=true}};
    return true;
}

static bool rho_normalize_cursor_return(RhoNormalizeCursor *cursor, Atom *atom) {
    if (!atom) return false;
    RhoNormalizeFrame *frame=rho_normalize_frame_at(cursor,cursor->frames.length-1u);
    frame->result=atom;
    frame->memo_stage=2u;
    return true;
}

static CettaSegmentedStep rho_normalize_append(RhoNormalizeFrame *frame, Atom *atom) {
    if (frame->length == UINT32_MAX) return CETTA_SEGMENTED_REFUSED;
    CettaSegmentedStep status = rho_sort_buffer_prepare(&frame->items,
        frame->length,sizeof(RhoKeyedAtom));
    if (status != CETTA_SEGMENTED_READY) return status;
    *rho_keyed_row(&frame->items,frame->length) =
        (RhoKeyedAtom){.atom=atom,.ordinal=frame->length};
    frame->length++;
    return CETTA_SEGMENTED_READY;
}

/* One action visits a constructor, advances one child/key/sort position,
 * writes one expression child, or retires one temporary key. Whole allocation,
 * scalar/name construction and payload atom rendering retain their explicit
 * provider costs. No provisional term becomes machine.current. */
static bool rho_normalize_cursor_advance(RhoNormalizeCursor *cursor, RhocalcRunAccount *account) {
    if (cursor->done) return true;
    if (cursor->frames.pending)
        return cetta_record_stack_prepare(&cursor->frames,&cursor->pending_frame)!=CETTA_SEGMENTED_REFUSED;
    if (!cursor->frames.length) {
        if (cursor->frames.slots.blocks) {
            cetta_record_stack_release_storage_one(&cursor->frames);
        } else if (cursor->memo.slots.blocks) {
            if (!rho_run_charge(&account->normalization_memo_steps,1u)) return false;
            cetta_node_memo_release_one(&cursor->memo);
        } else cursor->done=true;
        return true;
    }
    RhoNormalizeFrame *frame = rho_normalize_frame_at(cursor,cursor->frames.length - 1u);
    if (!frame->memo_stage || frame->memo_stage==2u) {
        if (!rho_run_charge(&account->normalization_memo_steps,1u)) return false;
        if (!cursor->memo_cursor.memo) {
            if (!frame->memo_stage)
                cetta_node_memo_find_begin(&cursor->memo_cursor,&cursor->memo,
                    frame->atom,frame->name);
            else cetta_node_memo_store_begin(&cursor->memo_cursor,&cursor->memo,
                    frame->atom,frame->name,(uintptr_t)frame->result,SIZE_MAX);
        }
        CettaNodeMemoStatus status=cetta_node_memo_advance(&cursor->memo_cursor);
        if (status==CETTA_NODE_MEMO_PENDING) return true;
        Atom *result=status==CETTA_NODE_MEMO_FOUND
            ? (Atom *)cursor->memo_cursor.value : frame->result;
        if (status==CETTA_NODE_MEMO_REFUSED) {
            rho_validation_set("rho normalization memo refused");
            return false;
        }
        cetta_node_memo_cancel(&cursor->memo_cursor);
        if (status==CETTA_NODE_MEMO_FOUND || status==CETTA_NODE_MEMO_STORED) {
            rho_normalize_frame_free(frame);
            cursor->returned=result;
            if (!rho_normalize_record_pop(cursor)) return false;
            cursor->done=!cursor->frames.length && !cursor->frames.slots.blocks && !cursor->memo.slots.blocks;
        } else frame->memo_stage=1u;
        return true;
    }
    if (frame->name) {
        switch (frame->stage) {
        case 0u:
            if (frame->atom->kind == ATOM_VAR)
                return rho_normalize_cursor_return(cursor, rho_copy_var(cursor->arena, frame->atom));
            frame->view = rho_view(frame->atom);
            frame->stage = 1u;
            return rho_normalize_cursor_push(cursor, frame->view.args[0], false);
        case 1u: {
            RhoView inner = rho_view(cursor->returned);
            if (inner.kind == RHO_DROP && inner.nargs == 1u) {
                /* The completed drop already holds its normalized name. */
                return rho_normalize_cursor_return(cursor, inner.args[0]);
            }
            return rho_normalize_cursor_return(cursor,
                rho_unary(cursor->arena, "rho:quote", cursor->returned));
        }
        default:
            return false;
        }
    }
    if (!frame->stage) {
        frame->view = rho_view(frame->atom);
        switch (frame->view.kind) {
        case RHO_NIL: return rho_normalize_cursor_return(cursor, rho_nil(cursor->arena));
        case RHO_VAL:
            return rho_normalize_cursor_return(cursor, rho_value_proc(cursor->arena, frame->view.args[0]));
        case RHO_EVAL_PAYLOAD:
            return rho_normalize_cursor_return(cursor,
                rho_unary(cursor->arena, "rho:eval-payload", frame->view.args[0]));
        case RHO_PAR: frame->stage = 1u; return true;
        case RHO_SEND:
        case RHO_RECV:
        case RHO_RECV_PERSISTENT:
        case RHO_DROP:
            frame->stage = 1u;
            return rho_normalize_cursor_push(cursor, frame->view.args[0], true);
        default:
            return false;
        }
    }
    if (frame->view.kind != RHO_PAR) {
        if (frame->view.kind == RHO_DROP)
            return rho_normalize_cursor_return(cursor,
                rho_unary(cursor->arena, "rho:drop", cursor->returned));
        if (frame->stage == 1u) {
            frame->left = cursor->returned; frame->stage = 2u;
            return rho_normalize_cursor_push(cursor,
                frame->view.args[frame->view.kind == RHO_SEND ? 1u : 2u], false);
        }
        Atom *out = frame->view.kind == RHO_SEND
            ? rho_binary(cursor->arena, "rho:send", frame->left, cursor->returned)
            : rho_ternary(cursor->arena,
                frame->view.kind == RHO_RECV ? "rho:recv" : "rho:recv-persistent",
                frame->left, rho_copy_var(cursor->arena, frame->view.args[1]), cursor->returned);
        return rho_normalize_cursor_return(cursor, out);
    }
    switch (frame->stage) {
    case 1u:
        if (frame->position < frame->view.nargs) {
            frame->stage = 2u;
            return rho_normalize_cursor_push(cursor, frame->view.args[frame->position++], false);
        }
        if (frame->length < 2u) {
            frame->result = frame->length ? rho_keyed_row(&frame->items,0u)->atom
                : rho_nil(cursor->arena);
            if (!frame->result) return false;
            frame->position=0u;frame->stage=9u;
            return true;
        }
        frame->position = 0u; frame->stage = 4u;
        return true;
    case 2u: {
        RhoView child = rho_view(cursor->returned);
        if (child.kind == RHO_PAR) {
            frame->flattened = child; frame->flatten_position = 0u; frame->stage = 3u;
        } else {
            if (child.kind != RHO_NIL) {
                CettaSegmentedStep status = rho_normalize_append(frame,cursor->returned);
                if (status != CETTA_SEGMENTED_READY) return status == CETTA_SEGMENTED_PENDING;
            }
            frame->stage = 1u;
        }
        return true;
    }
    case 3u:
        if (frame->flatten_position < frame->flattened.nargs) {
            CettaSegmentedStep status = rho_normalize_append(frame,
                frame->flattened.args[frame->flatten_position]);
            if (status == CETTA_SEGMENTED_READY) frame->flatten_position++;
            return status != CETTA_SEGMENTED_REFUSED;
        }
        frame->stage = 1u; return true;
    case 4u:
        if (frame->position == frame->length) {
            if (!rho_stable_sort_init(&frame->sort, frame->items, frame->length, false))
                return false;
            frame->stage = 5u; return true;
        }
        if (!frame->key) {
            frame->key = rho_key_cursor_new(rho_keyed_row(&frame->items,frame->position)->atom, false);
            return frame->key != NULL;
        }
        if (!rho_run_charge(&account->key_steps, 1u)) return false;
        size_t before = frame->key->text.len;
        if (!rho_key_cursor_advance(frame->key) ||
            !rho_run_charge(&account->key_bytes, frame->key->text.len - before)) return false;
        if (frame->key->done) {
            rho_keyed_row(&frame->items,frame->position++)->key = rho_str_take(&frame->key->text, "");
            rho_key_cursor_free(frame->key); frame->key = NULL;
            return rho_keyed_row(&frame->items,frame->position - 1u)->key != NULL;
        }
        return true;
    case 5u: {
        if (!frame->sort.done) {
            bool okay = rho_stable_sort_advance(&frame->sort);
            frame->items = frame->sort.items;
            return okay;
        }
        if (rho_sort_buffer_has_storage(&frame->sort.scratch)) {
            rho_sort_buffer_release_one(&frame->sort.scratch);
            return true;
        }
        rho_stable_sort_free(&frame->sort);
        frame->stage = 6u;
        return true;
    }
    case 6u:
        if (frame->length == UINT32_MAX ||
            !rho_run_charge(&account->construction_bytes,
                sizeof(Atom) + ((uint64_t)frame->length + 1u) * sizeof(Atom *))) return false;
        frame->assembly = atom_expr_assembly_new(cursor->arena, frame->length + 1u);
        if (!frame->assembly) return false;
        frame->position = 0u; frame->stage = 7u;
        return true;
    case 7u:
        if (frame->position > frame->length) { frame->stage = 8u; return true; }
        if (!rho_run_charge(&account->construction_children, 1u)) return false;
        if (!atom_expr_assembly_append(frame->assembly, frame->position
                ? rho_keyed_row(&frame->items,frame->position - 1u)->atom
                : atom_symbol_id(cursor->arena, rho_head_symbol_id("rho:par")))) return false;
        frame->position++;
        return true;
    case 8u:
        frame->result = atom_expr_assembly_finish(frame->assembly);
        if (!frame->result) return false;
        atom_expr_assembly_free(frame->assembly); frame->assembly = NULL;
        frame->position = 0u; frame->stage = 9u;
        return true;
    case 9u:
        if (frame->position < frame->length) {
            free(rho_keyed_row(&frame->items,frame->position)->key);
            rho_keyed_row(&frame->items,frame->position++)->key = NULL;
            return true;
        } else {
            frame->length=0u;frame->stage=10u;
            return true;
        }
    case 10u:
        if (rho_sort_buffer_has_storage(&frame->items)) {
            rho_sort_buffer_release_one(&frame->items);
            return true;
        }
        return rho_normalize_cursor_return(cursor,frame->result);
    default:
        return false;
    }
}

/* Semantic substitution retains the actual replies of normalization, capture
 * queries, alpha-renaming and payload application. A quotation is opaque to
 * substitution after name normalization; capture queries inspect its free
 * variables. Only a matched name reply may expose a quoted drop payload. */
typedef enum {
    RHO_SUBST_PROC, RHO_SUBST_NAME, RHO_SUBST_FREE_PROC,
    RHO_SUBST_FREE_NAME, RHO_SUBST_FREE_ATOM,
    RHO_SUBST_NORMAL_PROC, RHO_SUBST_NORMAL_NAME, RHO_SUBST_APPLY,
    RHO_SUBST_COMM, RHO_SUBST_ROLE_COUNT
} RhoSubstitutionRole;

typedef struct {
    Atom *source, *replacement, *channel, *binder, *body;
    VarId variable;
    RhoView view;
    AtomExprAssembly *assembly;
    Atom **interned_children;
    CettaExprIndex position;
    RhoSubstitutionRole role;
    unsigned stage;
    bool rename;
} RhoSubstitutionFrame;

typedef struct {
    CettaRecordStack frames;
    RhoSubstitutionFrame pending_frame;
    Arena *arena;
    uint32_t arena_identity;
    uint64_t arena_epoch;
    HashConsTable *hashcons;
    Atom *returned;
    bool query, matched, done, bindings_live;
    RhoNormalizeCursor normalization;
    RhoCheckCursor check;
    Bindings bindings;
    BindingsApplySession *application;
    RhocalcRunAccount account;
} RhoSubstitutionCursor;

static RhoSubstitutionFrame *rho_substitution_frame_at(RhoSubstitutionCursor *cursor, uint32_t index) {
    return cetta_record_stack_at(&cursor->frames,index,&cursor->pending_frame);
}

static void rho_substitution_frame_free(RhoSubstitutionFrame *frame) {
    atom_expr_assembly_free(frame->assembly);
    free(frame->interned_children);
    *frame = (RhoSubstitutionFrame){0};
}

static bool rho_substitution_record_pop(RhoSubstitutionCursor *cursor) {
    if (!cursor->frames.length) return false;
    RhoSubstitutionFrame *frame=rho_substitution_frame_at(cursor,cursor->frames.length-1u);
    if (!frame || frame->assembly || frame->interned_children) return false;
    return cetta_record_stack_pop(&cursor->frames);
}

/* Retire candidate-local storage before its source generation can reset.
 * Paid work survives that retirement in the enclosing selector's account. */
static void rho_substitution_cursor_clear(RhoSubstitutionCursor *cursor) {
    RhocalcRunAccount account = cursor->account;
    bindings_apply_session_free(cursor->application);
    if (cursor->bindings_live) bindings_free(&cursor->bindings);
    rho_normalize_cursor_free(&cursor->normalization);
    rho_check_cursor_free(&cursor->check);
    while (cursor->frames.length) {
        rho_substitution_frame_free(rho_substitution_frame_at(cursor,cursor->frames.length-1u));
        (void)rho_substitution_record_pop(cursor);
    }
    (void)cetta_record_stack_free_storage(&cursor->frames);
    *cursor = (RhoSubstitutionCursor){.account = account};
}

static bool rho_substitution_push(RhoSubstitutionCursor *cursor,
        RhoSubstitutionRole role, Atom *source, VarId variable,
        Atom *replacement, bool rename) {
    if (!source) return false;
    if (!cetta_record_stack_request(&cursor->frames,sizeof(RhoSubstitutionFrame))) return false;
    cursor->pending_frame=(RhoSubstitutionFrame){
        .role = role, .source = source, .variable = variable,
        .replacement = replacement, .rename = rename};
    return true;
}

static bool rho_substitution_begin(RhoSubstitutionCursor *cursor, Arena *arena,
        Atom *source, VarId variable, Atom *replacement) {
    if (!arena || !source || !replacement || cursor->frames.length || cursor->arena)
        return false;
    cursor->arena = arena;cursor->arena_identity = arena->identity;
    cursor->arena_epoch = arena->reset_epoch;cursor->hashcons = arena->hashcons;
    cursor->normalization.arena = arena;
    return rho_substitution_push(cursor, RHO_SUBST_PROC, source, variable, replacement, false);
}

static bool rho_substitution_begin_payload(RhoSubstitutionCursor *cursor, Arena *arena,
        Atom *body, VarId variable, Atom *payload) {
    if (!body || !rho_substitution_begin(cursor,arena,payload,variable,payload)) return false;
    rho_substitution_frame_at(cursor,0)->role=RHO_SUBST_COMM;
    rho_substitution_frame_at(cursor,0)->body=body;
    return true;
}

static bool rho_substitution_return(RhoSubstitutionCursor *cursor,
        Atom *result, bool query, bool matched) {
    rho_substitution_frame_free(rho_substitution_frame_at(cursor,cursor->frames.length-1u));
    if (!rho_substitution_record_pop(cursor)) return false;
    cursor->returned = result;cursor->query = query;cursor->matched = matched;
    cursor->done = !cursor->frames.length && !cursor->frames.slots.blocks;
    return true;
}

static bool rho_substitution_return_atom(RhoSubstitutionCursor *cursor, Atom *result) {
    return result && rho_substitution_return(cursor, result, false, false);
}

static bool rho_substitution_child(RhoSubstitutionCursor *cursor,
        RhoSubstitutionFrame *frame, RhoSubstitutionRole role, Atom *source) {
    return rho_substitution_push(cursor, role, source,
        frame->variable, frame->replacement, frame->rename);
}

static bool rho_substitution_advance(RhoSubstitutionCursor *cursor) {
    if (cursor->done) return true;
    if (!cursor->arena ||
            cursor->arena->identity != cursor->arena_identity ||
            cursor->arena->reset_epoch != cursor->arena_epoch ||
            cursor->arena->hashcons != cursor->hashcons ||
            !rho_run_charge(&cursor->account.substitution_steps, 1u)) return false;
    if (cursor->frames.pending)
        return cetta_record_stack_prepare(&cursor->frames,&cursor->pending_frame)!=CETTA_SEGMENTED_REFUSED;
    if (!cursor->frames.length) {
        if (cursor->frames.slots.blocks) cetta_record_stack_release_storage_one(&cursor->frames);
        else cursor->done=true;
        return true;
    }
    RhoSubstitutionFrame *frame = rho_substitution_frame_at(cursor,cursor->frames.length - 1u);
    Arena *arena = cursor->arena;

    if (frame->role == RHO_SUBST_COMM) {
        switch (frame->stage) {
        case 0u:
            cursor->check.allow_payloads=true;cursor->check.quiet_shape=true;
            frame->stage=1u;
            return rho_check_cursor_push(&cursor->check,frame->source,false);
        case 1u: {
            if (!rho_run_charge(&cursor->account.validation_steps,1u)) return false;
            uint64_t before=cursor->account.validation_memo_steps;
            RhoCheckStatus status=rho_check_cursor_advance(&cursor->check,&cursor->account);
            if (!rho_run_charge(&cursor->account.substitution_memo_steps,
                    cursor->account.validation_memo_steps-before)) return false;
            if (status==RHO_CHECK_REFUSED) return false;
            if (status==RHO_CHECK_PENDING) return true;
            rho_check_cursor_free(&cursor->check);
            frame->stage=2u;
            if (status==RHO_CHECK_COMPLETE)
                return rho_substitution_child(cursor,frame,RHO_SUBST_NORMAL_PROC,frame->source);
            cursor->returned=rho_value_proc(arena,frame->source);
            return cursor->returned!=NULL;
        }
        case 2u:
            frame->replacement=rho_unary(arena,"rho:quote",cursor->returned);
            if (!frame->replacement) return false;
            frame->stage=3u;
            return rho_substitution_child(cursor,frame,RHO_SUBST_PROC,frame->body);
        case 3u: return rho_substitution_return_atom(cursor,cursor->returned);
        default: return false;
        }
    }

    if (frame->role == RHO_SUBST_NORMAL_PROC || frame->role == RHO_SUBST_NORMAL_NAME) {
        bool name = frame->role == RHO_SUBST_NORMAL_NAME;
        /* Synchronous interning profiles retain their ordinary constructor
         * provider. Canonical retained invocations use private draft storage. */
        if (arena->hashcons)
            return rho_substitution_return_atom(cursor, name
                ? rho_normalize_name(arena, frame->source)
                : rho_normalize_proc(arena, frame->source));
        if (!frame->stage) {
            if (cursor->normalization.frames.length) return false;
            cursor->normalization.done = false;
            cursor->normalization.returned = NULL;frame->stage = 1u;
            return rho_normalize_cursor_push(&cursor->normalization, frame->source, name);
        }
        uint64_t before_memo = cursor->account.normalization_memo_steps;
        if (!rho_run_charge(&cursor->account.normalization_steps, 1u)) return false;
        bool okay = rho_normalize_cursor_advance(&cursor->normalization, &cursor->account);
        if (!rho_run_charge(&cursor->account.substitution_memo_steps,
                cursor->account.normalization_memo_steps - before_memo)) return false;
        if (!okay) return false;
        return !cursor->normalization.done ||
            rho_substitution_return_atom(cursor, cursor->normalization.returned);
    }

    if (frame->role == RHO_SUBST_APPLY) {
        if (!frame->stage) {
            if (!atom_has_vars(frame->source))
                return rho_substitution_return_atom(cursor, frame->source);
            bindings_init(&cursor->bindings);cursor->bindings_live = true;
            if (!bindings_add_id(&cursor->bindings, frame->variable,
                    atom_symbol_current_id(frame->replacement), frame->replacement))
                return false;
            cursor->application = bindings_apply_session_new(&cursor->bindings,
                arena, binding_value_from_atom(frame->source), false);
            if (!cursor->application) return false;
            frame->stage = 1u;return true;
        }
        Atom *result = NULL;uint64_t used = 0u;
        BindingsApplyStatus status = bindings_apply_session_advance(
            cursor->application, 1u, &result, &used);
        if (used > 1u || !rho_run_charge(&cursor->account.substitution_apply_steps, used) ||
                status == BINDINGS_APPLY_REFUSED) return false;
        if (status == BINDINGS_APPLY_PENDING) return true;
        bindings_apply_session_free(cursor->application);cursor->application = NULL;
        bindings_free(&cursor->bindings);cursor->bindings_live = false;
        return rho_substitution_return_atom(cursor, result);
    }

    if (frame->role == RHO_SUBST_FREE_ATOM) {
        if (!frame->stage) {
            if (!atom_has_vars(frame->source))
                return rho_substitution_return(cursor, NULL, false, false);
            if (frame->source->kind == ATOM_VAR)
                return rho_substitution_return(cursor, NULL,
                    var_id_eq(frame->source->var_id, frame->variable), false);
            if (frame->source->kind != ATOM_EXPR)
                return rho_substitution_return(cursor, NULL, false, false);
            frame->stage = 1u;
        } else if (cursor->query)
            return rho_substitution_return(cursor, NULL, true, false);
        if (frame->position == frame->source->expr.len)
            return rho_substitution_return(cursor, NULL, false, false);
        Atom *child = frame->source->expr.elems[frame->position++];
        return rho_substitution_child(cursor, frame, RHO_SUBST_FREE_ATOM, child);
    }

    if (frame->role == RHO_SUBST_FREE_NAME) {
        if (frame->stage)
            return rho_substitution_return(cursor, NULL, cursor->query, false);
        if (frame->source->kind == ATOM_VAR)
            return rho_substitution_return(cursor, NULL,
                var_id_eq(frame->source->var_id, frame->variable), false);
        frame->view = rho_view(frame->source);
        if (frame->view.kind != RHO_QUOTE || frame->view.nargs != 1u)
            return rho_substitution_return(cursor, NULL, false, false);
        frame->stage = 1u;
        return rho_substitution_child(cursor, frame, RHO_SUBST_FREE_PROC, frame->view.args[0]);
    }

    if (frame->role == RHO_SUBST_FREE_PROC) {
        if (!frame->stage) {
            frame->view = rho_view(frame->source);frame->stage = 1u;
        } else if (cursor->query)
            return rho_substitution_return(cursor, NULL, true, false);
        RhoView *view = &frame->view;
        switch (view->kind) {
        case RHO_PAR:
            if (frame->position < view->nargs)
                return rho_substitution_child(cursor, frame, RHO_SUBST_FREE_PROC,
                    view->args[frame->position++]);
            break;
        case RHO_SEND:
        case RHO_RECV:
        case RHO_RECV_PERSISTENT:
            if (!frame->position++)
                return rho_substitution_child(cursor, frame, RHO_SUBST_FREE_NAME, view->args[0]);
            if (frame->position == 2u && (view->kind == RHO_SEND ||
                    view->args[1]->kind != ATOM_VAR ||
                    !var_id_eq(view->args[1]->var_id, frame->variable)))
                return rho_substitution_child(cursor, frame, RHO_SUBST_FREE_PROC,
                    view->args[view->kind == RHO_SEND ? 1u : 2u]);
            break;
        case RHO_DROP:
        case RHO_VAL:
        case RHO_EVAL_PAYLOAD:
            if (!frame->position++)
                return rho_substitution_child(cursor, frame,
                    view->kind == RHO_DROP ? RHO_SUBST_FREE_NAME : RHO_SUBST_FREE_ATOM,
                    view->args[0]);
            break;
        default: break;
        }
        return rho_substitution_return(cursor, NULL, false, false);
    }

    if (frame->role == RHO_SUBST_NAME) {
        if (!frame->stage) {
            frame->stage = 1u;
            return rho_substitution_child(cursor, frame, RHO_SUBST_NORMAL_NAME, frame->source);
        }
        bool matched = cursor->returned && cursor->returned->kind == ATOM_VAR &&
            var_id_eq(cursor->returned->var_id, frame->variable);
        return rho_substitution_return(cursor, matched ? frame->replacement : cursor->returned,
            false, matched && !frame->rename);
    }

    if (!frame->stage) {
        frame->view = rho_view(frame->source);
        switch (frame->view.kind) {
        case RHO_NIL: return rho_substitution_return_atom(cursor, rho_nil(arena));
        case RHO_PAR:
            if (frame->view.nargs == UINT32_MAX) return false;
            if (!rho_run_charge(&cursor->account.construction_bytes,
                    sizeof(Atom) + ((uint64_t)frame->view.nargs + 1u) * sizeof(Atom *)))
                return false;
            if (arena->hashcons) {
                frame->interned_children = calloc(frame->view.nargs + 1u, sizeof(Atom *));
                if (!frame->interned_children) return false;
            } else {
                frame->assembly = atom_expr_assembly_new(arena, frame->view.nargs + 1u);
                if (!frame->assembly) return false;
            }
            frame->stage = 1u;return true;
        case RHO_RECV:
        case RHO_RECV_PERSISTENT:
            frame->binder = rho_copy_var(arena, frame->view.args[1]);
            frame->body = frame->view.args[2];
            if (!frame->binder) return false;
            if (var_id_eq(frame->binder->var_id, frame->variable)) {
                frame->stage = 12u;
                return rho_substitution_child(cursor, frame, RHO_SUBST_NAME, frame->view.args[0]);
            }
            if (!frame->rename) {
                frame->stage = 10u;
                return rho_substitution_push(cursor, RHO_SUBST_FREE_NAME,
                    frame->replacement, frame->binder->var_id, NULL, false);
            }
            frame->stage = 14u;
            return rho_substitution_child(cursor, frame, RHO_SUBST_NAME, frame->view.args[0]);
        case RHO_SEND:
        case RHO_DROP:
            frame->stage = 20u;
            return rho_substitution_child(cursor, frame, RHO_SUBST_NAME, frame->view.args[0]);
        case RHO_VAL:
        case RHO_EVAL_PAYLOAD:
            frame->stage = 30u;
            return rho_substitution_child(cursor, frame, RHO_SUBST_APPLY, frame->view.args[0]);
        default: return rho_substitution_return_atom(cursor, frame->source);
        }
    }

    switch (frame->stage) {
    case 1u: {
        Atom *head = atom_symbol_id(arena, rho_head_symbol_id("rho:par"));
        if (!head || !rho_run_charge(&cursor->account.construction_children, 1u)) return false;
        if (frame->assembly) {
            if (!atom_expr_assembly_append(frame->assembly, head)) return false;
        } else frame->interned_children[0] = head;
        frame->stage = 2u;return true;
    }
    case 2u:
        if (frame->position < frame->view.nargs) {
            frame->stage = 3u;
            return rho_substitution_child(cursor, frame, RHO_SUBST_PROC,
                frame->view.args[frame->position]);
        }
        frame->body = frame->assembly ? atom_expr_assembly_finish(frame->assembly)
            : atom_expr(arena, frame->interned_children, frame->view.nargs + 1u);
        if (!frame->body) return false;
        frame->stage = 40u;
        return rho_substitution_child(cursor, frame, RHO_SUBST_NORMAL_PROC, frame->body);
    case 3u:
        if (!cursor->returned || !rho_run_charge(&cursor->account.construction_children, 1u))
            return false;
        if (frame->assembly) {
            if (!atom_expr_assembly_append(frame->assembly, cursor->returned)) return false;
        } else frame->interned_children[frame->position + 1u] = cursor->returned;
        frame->position++;frame->stage = 2u;return true;
    case 10u:
        if (cursor->query) {
            Atom *fresh = rho_fresh_var_like(arena, frame->binder);
            if (!fresh) return false;
            VarId old = frame->binder->var_id;
            frame->binder = fresh;frame->stage = 11u;
            return rho_substitution_push(cursor, RHO_SUBST_PROC, frame->body, old, fresh, true);
        }
        frame->stage = 14u;
        return rho_substitution_child(cursor, frame, RHO_SUBST_NAME, frame->view.args[0]);
    case 11u:
        frame->body = cursor->returned;frame->stage = 14u;
        return rho_substitution_child(cursor, frame, RHO_SUBST_NAME, frame->view.args[0]);
    case 12u:
        frame->channel = cursor->returned;frame->stage = 15u;
        return rho_substitution_child(cursor, frame, RHO_SUBST_NORMAL_PROC, frame->body);
    case 14u:
        frame->channel = cursor->returned;frame->stage = 15u;
        return rho_substitution_child(cursor, frame, RHO_SUBST_PROC, frame->body);
    case 15u:
        frame->body = rho_ternary(arena,
            frame->view.kind == RHO_RECV ? "rho:recv" : "rho:recv-persistent",
            frame->channel, frame->binder, cursor->returned);
        if (!frame->body) return false;
        frame->stage = 40u;
        return rho_substitution_child(cursor, frame, RHO_SUBST_NORMAL_PROC, frame->body);
    case 20u:
        if (frame->view.kind == RHO_SEND) {
            frame->channel = cursor->returned;frame->stage = 21u;
            return rho_substitution_child(cursor, frame, RHO_SUBST_PROC, frame->view.args[1]);
        }
        if (frame->rename)
            return rho_substitution_return_atom(cursor, rho_unary(arena, "rho:drop", cursor->returned));
        {
            RhoView name = rho_view(cursor->returned);
            if (cursor->matched && name.kind == RHO_QUOTE && name.nargs == 1u) {
                frame->stage = 40u;
                return rho_substitution_child(cursor, frame, RHO_SUBST_NORMAL_PROC, name.args[0]);
            }
            frame->stage = 22u;
            return rho_substitution_child(cursor, frame, RHO_SUBST_NORMAL_NAME, cursor->returned);
        }
    case 21u:
        frame->body = rho_binary(arena, "rho:send", frame->channel, cursor->returned);
        if (!frame->body) return false;
        frame->stage = 40u;
        return rho_substitution_child(cursor, frame, RHO_SUBST_NORMAL_PROC, frame->body);
    case 22u:
        return rho_substitution_return_atom(cursor, rho_unary(arena, "rho:drop", cursor->returned));
    case 30u:
        return rho_substitution_return_atom(cursor, frame->view.kind == RHO_VAL
            ? rho_value_proc(arena, cursor->returned)
            : rho_unary(arena, "rho:eval-payload", cursor->returned));
    case 40u: return rho_substitution_return_atom(cursor, cursor->returned);
    default: return false;
    }
}

static Atom *rho_subst_proc(Arena *arena, Atom *proc,
        VarId variable, Atom *replacement) {
    RhoSubstitutionCursor cursor = {0};
    bool okay = rho_substitution_begin(&cursor, arena, proc, variable, replacement);
    while (okay && !cursor.done) okay = rho_substitution_advance(&cursor);
    Atom *result = okay ? cursor.returned : NULL;
    rho_substitution_cursor_clear(&cursor);
    return result;
}

static Atom *rho_subst_payload_proc(Arena *arena, Atom *proc,
        VarId variable, Atom *payload) {
    RhoSubstitutionCursor cursor={0};
    bool okay=rho_substitution_begin_payload(&cursor,arena,proc,variable,payload);
    while (okay && !cursor.done) okay=rho_substitution_advance(&cursor);
    Atom *result=okay ? cursor.returned : NULL;
    rho_substitution_cursor_clear(&cursor);
    return result;
}

/* Body preparation holds only completed key products in the owned prefix.
 * Sorting borrows those keys until its complete primary permutation is ready. */
typedef struct {
    RhoReactionKeyView *view;
    RhoView body;
    RhoProcessKeyCursor key;
    RhoStableSort sort;
    uint32_t expected;
    unsigned stage;
    bool done;
} RhoReactionViewCursor;

static bool rho_reaction_view_cursor_begin(RhoReactionViewCursor *cursor,
    RhoReactionKeyView *view, RhoAtomVec *components, RhoSortBuffer *component_keys,
    uint32_t send, uint32_t recv, Atom *body) {
    *cursor=(RhoReactionViewCursor){.view=view,.body=rho_view(body)};
    if (!rho_reaction_key_view_init(view,components,component_keys,send,recv,body)) return false;
    cursor->expected=cursor->body.kind==RHO_PAR ? cursor->body.nargs
        : cursor->body.kind==RHO_NIL ? 0u : 1u;
    return true;
}

static void rho_reaction_view_cursor_free(RhoReactionViewCursor *cursor) {
    rho_process_key_cursor_free(&cursor->key);
    rho_stable_sort_free(&cursor->sort);
    if (cursor->view) rho_reaction_key_view_free(cursor->view);
    *cursor=(RhoReactionViewCursor){0};
}

static bool rho_reaction_view_cursor_advance(RhoReactionViewCursor *cursor) {
    if (cursor->done) return true;
    RhoReactionKeyView *view=cursor->view;
    switch (cursor->stage) {
    case 0u:
        cursor->stage=1u;
        return true;
    case 1u:
        if (view->body_len<cursor->expected) {
            uint32_t i=view->body_len;
            CettaSegmentedStep prepared = rho_sort_buffer_prepare(&view->body_items,
                i,sizeof(RhoKeyedAtom));
            if (prepared != CETTA_SEGMENTED_READY) return prepared == CETTA_SEGMENTED_PENDING;
            Atom *atom=cursor->body.kind==RHO_PAR ? cursor->body.args[i] : view->body;
            if (!cursor->key.atom && !rho_process_key_cursor_begin(&cursor->key,NULL,atom,false)) return false;
            if (!rho_process_key_cursor_advance(&cursor->key,NULL)) return false;
            if (!cursor->key.done) return true;
            bool owned=false;
            RhoKeyProduct *product=rho_process_key_cursor_take(&cursor->key,&owned);
            rho_process_key_cursor_free(&cursor->key);
            if (!product || !owned) return false;
            *rho_keyed_row(&view->body_items,view->body_len++)=(RhoKeyedAtom){.atom=atom,.key=product->text,
                .ordinal=i,.binders=product->binders};
            product->text=NULL;rho_key_product_free(product);
            return true;
        }
        if (!rho_stable_sort_init(&cursor->sort,view->body_items,view->body_len,false)) return false;
        cursor->stage=2u;
        return true;
    case 2u: {
        bool okay=rho_stable_sort_advance(&cursor->sort);
        view->body_items=cursor->sort.items;
        if (!okay) return false;
        if (cursor->sort.done) cursor->stage=3u;
        return true;
    }
    case 3u:
        if (rho_sort_buffer_has_storage(&cursor->sort.scratch)) {
            rho_sort_buffer_release_one(&cursor->sort.scratch);
            return true;
        }
        rho_stable_sort_free(&cursor->sort);
        cursor->done=true;
        return true;
    default:
        return false;
    }
}

static bool rho_reaction_view_cursor_take(RhoReactionViewCursor *cursor) {
    if (!cursor->done || !cursor->view) return false;
    cursor->view=NULL;
    return true;
}

typedef enum {
    RHO_BUILD_COPY_BEGIN,
    RHO_BUILD_COPY,
    RHO_BUILD_ALLOCATE,
    RHO_BUILD_HEAD,
    RHO_BUILD_ROWS,
    RHO_BUILD_FINISH
} RhoReactionBuildPhase;

/* The selected view and its key owners stay borrowed and unchanged. A body
 * copy belongs to the destination arena; a source body's original ordinal
 * selects its copied child only when that actual row is emitted. No second
 * row vector or mutation of the incumbent view is required. */
typedef struct {
    const RhoReactionKeyView *view;
    Arena *arena;
    AtomDeepCopySession *copy;
    AtomExprAssembly *assembly;
    RhoReactionComponentCursor merge;
    Atom *copied_body, *single, *result;
    uint32_t written;
    RhoReactionBuildPhase phase;
    uint64_t steps, copy_steps, merge_steps, children, bytes;
    bool done;
} RhoReactionBuildCursor;

static bool rho_reaction_build_cursor_begin(RhoReactionBuildCursor *cursor,
    Arena *arena, const RhoReactionKeyView *view) {
    if (!cursor || !arena || !view || !view->components || !view->component_keys ||
        !view->body || view->send_index>=view->components->len ||
        view->recv_index>=view->components->len || view->send_index==view->recv_index)
        return false;
    uint32_t retained=view->components->len-(view->retain_recv ? 1u : 2u);
    RhoView body=rho_view(view->body);
    uint32_t length=body.kind==RHO_PAR ? body.nargs : body.kind==RHO_NIL ? 0u : 1u;
    if (view->body_len!=length || (length && !rho_keyed_row(&view->body_items,length-1u)) ||
        length>UINT32_MAX-retained || view->total!=retained+length) return false;
    *cursor=(RhoReactionBuildCursor){.view=view,.arena=arena};
    rho_reaction_component_cursor_init(&cursor->merge,view);
    return true;
}

static void rho_reaction_build_cursor_free(RhoReactionBuildCursor *cursor) {
    atom_deep_copy_session_free(cursor->copy);
    atom_expr_assembly_free(cursor->assembly);
    *cursor=(RhoReactionBuildCursor){0};
}

static bool rho_reaction_build_cursor_advance(RhoReactionBuildCursor *cursor) {
    if (cursor->done) return true;
    if (!rho_run_charge(&cursor->steps,1u)) return false;
    switch (cursor->phase) {
    case RHO_BUILD_COPY_BEGIN:
        if (!cursor->view->body_len) cursor->phase=RHO_BUILD_ALLOCATE;
        else {
            cursor->copy=atom_deep_copy_session_new(cursor->arena);
            if (!cursor->copy) return false;
            cursor->phase=RHO_BUILD_COPY;
        }
        return true;
    case RHO_BUILD_COPY: {
        uint64_t used=0u;
        AtomDeepCopyStep status=atom_deep_copy_session_advance(cursor->copy,
            cursor->view->body,1u,&cursor->copied_body,&used);
        if (used>1u || !rho_run_charge(&cursor->copy_steps,used) || status==ATOM_DEEP_COPY_REFUSED)
            return false;
        if (status==ATOM_DEEP_COPY_COMPLETE) {
            atom_deep_copy_session_free(cursor->copy);cursor->copy=NULL;
            cursor->phase=RHO_BUILD_ALLOCATE;
        }
        return true;
    }
    case RHO_BUILD_ALLOCATE:
        if (cursor->view->total>1u) {
            if (cursor->view->total==UINT32_MAX ||
                !rho_run_charge(&cursor->bytes,sizeof(Atom)+
                    ((uint64_t)cursor->view->total+1u)*sizeof(Atom *))) return false;
            cursor->assembly=atom_expr_assembly_new(cursor->arena,cursor->view->total+1u);
            if (!cursor->assembly) return false;
            cursor->phase=RHO_BUILD_HEAD;
        } else cursor->phase=RHO_BUILD_ROWS;
        return true;
    case RHO_BUILD_HEAD:
        if (!atom_expr_assembly_append(cursor->assembly,
                atom_symbol_id(cursor->arena,rho_head_symbol_id("rho:par"))) ||
            !rho_run_charge(&cursor->children,1u)) return false;
        cursor->phase=RHO_BUILD_ROWS;
        return true;
    case RHO_BUILD_ROWS: {
        RhoReactionRow row={0};
        if (!rho_run_charge(&cursor->merge_steps,1u)) return false;
        RhoKeyStep status=rho_reaction_component_cursor_next(&cursor->merge,&row);
        if (status==RHO_KEY_PENDING) return true;
        if (status==RHO_KEY_REFUSED) return false;
        if (status==RHO_KEY_END) {
            if (cursor->written!=cursor->view->total) return false;
            cursor->phase=RHO_BUILD_FINISH;
            return true;
        }
        if (cursor->written>=cursor->view->total) return false;
        Atom *next=row.atom;
        if (row.from_body) {
            RhoView copied=rho_view(cursor->copied_body);
            if (copied.kind==RHO_PAR) {
                if (row.ordinal>=copied.nargs) return false;
                next=copied.args[row.ordinal];
            } else {
                if (row.ordinal || cursor->view->body_len!=1u) return false;
                next=cursor->copied_body;
            }
        }
        if (!next) return false;
        if (cursor->assembly) {
            if (!atom_expr_assembly_append(cursor->assembly,next) ||
                !rho_run_charge(&cursor->children,1u)) return false;
        } else cursor->single=next;
        cursor->written++;
        return true;
    }
    case RHO_BUILD_FINISH:
        cursor->result=cursor->assembly ? atom_expr_assembly_finish(cursor->assembly)
            : cursor->view->total ? cursor->single : rho_nil(cursor->arena);
        if (!cursor->result) return false;
        atom_expr_assembly_free(cursor->assembly);cursor->assembly=NULL;
        cursor->done=true;
        return true;
    }
    return false;
}

typedef enum {
    RHO_CANONICAL_PREPARE,
    RHO_CANONICAL_COLLECT,
    RHO_CANONICAL_ALLOCATE,
    RHO_CANONICAL_KEYS,
    RHO_CANONICAL_ENDPOINTS,
    RHO_CANONICAL_SORT_SENDS,
    RHO_CANONICAL_SORT_RECVS,
    RHO_CANONICAL_ALIGN,
    RHO_CANONICAL_RECV_RANGE,
    RHO_CANONICAL_SEND_RANGE,
    RHO_CANONICAL_PAIR,
    RHO_CANONICAL_PAYLOAD,
    RHO_CANONICAL_PAYLOAD_BODY,
    RHO_CANONICAL_SUBSTITUTION,
    RHO_CANONICAL_BODY,
    RHO_CANONICAL_BODY_PREPARE,
    RHO_CANONICAL_COMPARE,
    RHO_CANONICAL_CHOOSE_COPY,
    RHO_CANONICAL_CHOOSE_REMAP,
    RHO_CANONICAL_DISCARD,
    RHO_CANONICAL_FINISH,
    RHO_CANONICAL_BUILD
} RhoCanonicalPhase;

/* The selector owns its actual partially scanned candidate stream. Scratch
 * bodies survive a pause; only a newly selected body is copied persistently.
 * Each advance is one selector phase/probe, not a bound on key construction,
 * payload evaluation, allocation or rebuilding the selected successor. */
typedef struct {
    RhoMachine *machine;
    Arena scratch, selected[2];
    uint32_t selected_slot, remap_position;
    AtomDeepCopySession *best_copy;
    Atom *prepared_best_body;
    ArenaMark pair_mark;
    RhoAtomVec bodies;
    RhoPayloadJob *payload;
    RhoSubstitutionCursor substitution;
    CettaCount payload_position;
    RhoCanonicalPhase phase;
    uint32_t recv_pos, send_pos, recv_start, send_start, recv_end, send_end;
    uint32_t recv, send, body, best_recv, best_send, best_body_index;
    uint64_t next_ordinal, best_ordinal;
    uint64_t matching_pairs, body_candidates, completed_scans;
    Atom *best_body, *next;
    RhoReactionKeyView best_view,candidate_view;
    RhoReactionCompare comparison;
    RhoEndpointSort endpoint_sort;
    RhoReactionViewCursor view_job;
    RhoReactionBuildCursor build;
    RhoView process_view;
    uint32_t preparation_position;
    uint64_t candidate_ordinal,component_probes,text_probes,preparation_steps,channel_scan_probes;
    uint64_t memo_steps;
    RhoKeyProbe channel_probe;
    bool channel_probe_initialized;
    RhoKeyProbe preparation_order;
    bool preparation_order_initialized;
    RhoProcessKeyCursor key_job;
    RhoSortBuffer component_keys;
    uint32_t component_key_count;
    bool best_view_ready,candidate_ready,comparison_live,found,done,quiescent;
    bool retire_selected;
} RhoCanonicalCursor;

static void rho_canonical_cursor_init(RhoCanonicalCursor *cursor, RhoMachine *machine) {
    *cursor = (RhoCanonicalCursor){.machine=machine,
        .phase=RHO_CANONICAL_PREPARE,.best_ordinal=UINT64_MAX};
    arena_init(&cursor->scratch);
    arena_set_hashcons(&cursor->scratch, NULL);
    arena_init_detached(&cursor->selected[0]);
    arena_init_detached(&cursor->selected[1]);
    rho_vec_init(&cursor->bodies);
    cursor->component_keys.paged=true;
    machine->sends.storage.paged=true;
    machine->recvs.storage.paged=true;
    assert(!machine->comm_index_loaded && !machine->components.len &&
        !machine->sends.len && !machine->recvs.len);
}

static void rho_canonical_cursor_free(RhoCanonicalCursor *cursor) {
    RhoMachine *machine=cursor->machine;
    rho_reaction_build_cursor_free(&cursor->build);
    rho_process_key_cursor_free(&cursor->key_job);
    rho_reaction_view_cursor_free(&cursor->view_job);
    atom_deep_copy_session_free(cursor->best_copy);
    rho_substitution_cursor_clear(&cursor->substitution);
    rho_payload_close(cursor->payload);
    rho_endpoint_sort_free(&cursor->endpoint_sort);
    if (cursor->comparison_live) rho_reaction_compare_free(&cursor->comparison);
    if (cursor->candidate_ready) rho_reaction_key_view_free(&cursor->candidate_view);
    if (cursor->best_view_ready) rho_reaction_key_view_free(&cursor->best_view);
    for (uint32_t i=0u;i<cursor->component_key_count;i++) {
        RhoComponentKey *row=rho_component_key_row(&cursor->component_keys,i);
        if (row->owned) rho_key_product_free(row->product);
    }
    rho_sort_buffer_free(&cursor->component_keys);
    rho_vec_free(&cursor->bodies);
    rho_machine_clear_comm_index(machine);
    arena_free(&cursor->selected[0]);
    arena_free(&cursor->selected[1]);
    arena_free(&cursor->scratch);
    *cursor=(RhoCanonicalCursor){0};
}

/* The producer has stopped before this entry: either a completed selection
 * has captured its address, or cancellation has revoked further advances.
 * Its remaining job destructors dispose their own owners without reading
 * these borrowed records. Source arenas and the key cache stay alive until
 * those jobs and this indexed storage have both been retired. */
static bool rho_canonical_storage_release_one(RhoCanonicalCursor *cursor) {
    if (rho_sort_buffer_has_storage(&cursor->endpoint_sort.sort.scratch)) {
        rho_sort_buffer_release_one(&cursor->endpoint_sort.sort.scratch);
        return true;
    }
    if (rho_endpoint_vec_release_one(&cursor->machine->sends) ||
        rho_endpoint_vec_release_one(&cursor->machine->recvs)) return true;
    if (cursor->component_key_count) {
        RhoComponentKey *row=rho_component_key_row(&cursor->component_keys,
            --cursor->component_key_count);
        RhoKeyProduct *product=row->owned ? row->product : NULL;
        *row=(RhoComponentKey){0};
        rho_key_product_free(product);
        return true;
    }
    if (rho_sort_buffer_has_storage(&cursor->component_keys)) {
        rho_sort_buffer_release_one(&cursor->component_keys);
        return true;
    }
    return false;
}

static bool rho_canonical_cursor_prepare(RhoCanonicalCursor *cursor) {
    RhoMachine *machine=cursor->machine;
    if (!machine->current_canonical_order) {
        machine->current=rho_normalize_proc(machine->arena,machine->current);
        if (!machine->current) return false;
        machine->current_canonical_order=true;
    }
    cursor->process_view=rho_view(machine->current);
    /* The normalized source generation stays alive through selection and all
     * borrowed-key consumers. Collection checks one source occurrence at a
     * time; it need not allocate another copy of this immutable flat list. */
    RhoView *view=&cursor->process_view;
    machine->components=(RhoAtomVec){.items=view->kind==RHO_PAR ? view->args : &machine->current,
        .cap=view->kind==RHO_PAR ? view->nargs : 1u,.borrowed=true};
    cursor->phase=RHO_CANONICAL_COLLECT;
    return true;
}

static bool rho_canonical_cursor_choose(RhoCanonicalCursor *cursor,int order) {
    if (!cursor->found || order<0 || (order==0 && cursor->candidate_ordinal<cursor->best_ordinal)) {
        cursor->phase=RHO_CANONICAL_CHOOSE_COPY;
        cursor->remap_position=0u;
    } else cursor->phase=RHO_CANONICAL_DISCARD;
    return true;
}

static bool rho_canonical_cursor_body(RhoCanonicalCursor *cursor) {
    RhoMachine *machine=cursor->machine;
    if (cursor->next_ordinal==UINT64_MAX) {
        rho_validation_set("rho canonical candidate ordinal exhausted");
        return false;
    }
    cursor->candidate_ordinal=cursor->next_ordinal++;
    cursor->body_candidates++;
    if (!rho_reaction_view_cursor_begin(&cursor->view_job,&cursor->candidate_view,&machine->components,
            &cursor->component_keys,rho_endpoint_at(&machine->sends,cursor->send)->component_index,
            rho_endpoint_at(&machine->recvs,cursor->recv)->component_index,cursor->bodies.items[cursor->body]))
        return false;
    cursor->phase=RHO_CANONICAL_BODY_PREPARE;
    return true;
}

static bool rho_canonical_cursor_channel_probe(RhoCanonicalCursor *cursor,
        const RhoEndpoint *left,const RhoEndpoint *right,bool *ready,int *order) {
    if (!cursor->channel_probe_initialized) {
        if (!rho_key_probe_init(&cursor->channel_probe,left->key,right->key)) return false;
        cursor->channel_probe_initialized=true;
    }
    if (!cursor->channel_probe.done) {
        cursor->channel_scan_probes++;
        rho_key_probe_advance(&cursor->channel_probe);
    }
    *ready=cursor->channel_probe.done;
    if (*ready) {
        *order=cursor->channel_probe.order;
        cursor->channel_probe_initialized=false;
    }
    return true;
}

static bool rho_canonical_cursor_advance(RhoCanonicalCursor *cursor) {
    RhoMachine *machine=cursor->machine;
    if (cursor->done) return true;
    if (cursor->phase<RHO_CANONICAL_ALIGN || cursor->phase==RHO_CANONICAL_BODY_PREPARE ||
        cursor->phase==RHO_CANONICAL_CHOOSE_COPY || cursor->phase==RHO_CANONICAL_CHOOSE_REMAP ||
        cursor->phase==RHO_CANONICAL_DISCARD || cursor->phase==RHO_CANONICAL_BUILD ||
        cursor->phase==RHO_CANONICAL_SUBSTITUTION)
        cursor->preparation_steps++;
    switch (cursor->phase) {
    case RHO_CANONICAL_PREPARE:
        return rho_canonical_cursor_prepare(cursor);
    case RHO_CANONICAL_COLLECT: {
        RhoView *view=&cursor->process_view;
        if (view->kind==RHO_NIL) cursor->phase=RHO_CANONICAL_ALLOCATE;
        else if (view->kind!=RHO_PAR) {
            machine->components.len=1u;
            cursor->phase=RHO_CANONICAL_ALLOCATE;
        } else if (cursor->preparation_position<view->nargs) {
            Atom *part=view->args[cursor->preparation_position];
            RhoKind kind=rho_view(part).kind;
            if (kind==RHO_NIL || kind==RHO_PAR) return false;
            machine->components.len++;
            cursor->preparation_position++;
        } else cursor->phase=RHO_CANONICAL_ALLOCATE;
        return true;
    }
    case RHO_CANONICAL_ALLOCATE:
        cursor->preparation_position=0u;
        cursor->phase=RHO_CANONICAL_KEYS;
        return true;
    case RHO_CANONICAL_KEYS:
        if (cursor->preparation_position<machine->components.len) {
            uint32_t i=cursor->preparation_position;
            CettaSegmentedStep prepared=rho_sort_buffer_prepare(&cursor->component_keys,
                i,sizeof(RhoComponentKey));
            if (prepared!=CETTA_SEGMENTED_READY) return prepared==CETTA_SEGMENTED_PENDING;
            RhoComponentKey *row=rho_component_key_row(&cursor->component_keys,i);
            if (i==cursor->component_key_count) {
                *row=(RhoComponentKey){0};
                cursor->component_key_count++;
            }
            if (!row->product) {
                if (!cursor->key_job.atom && !rho_process_key_cursor_begin(&cursor->key_job,
                        &machine->process_key_cache,machine->components.items[i],false)) return false;
                if (!rho_process_key_cursor_advance(&cursor->key_job,&cursor->memo_steps)) return false;
                if (cursor->key_job.done) {
                    row->product=rho_process_key_cursor_take(&cursor->key_job,&row->owned);
                    rho_process_key_cursor_free(&cursor->key_job);
                    if (!row->product) return false;
                }
                return true;
            }
            if (i) {
                if (!cursor->preparation_order_initialized) {
                    if (!rho_key_probe_init(&cursor->preparation_order,
                            rho_component_key_at(&cursor->component_keys,i-1u)->text,row->product->text)) return false;
                    cursor->preparation_order_initialized=true;
                }
                rho_key_probe_advance(&cursor->preparation_order);
                if (!cursor->preparation_order.done) return true;
                if (cursor->preparation_order.order>0) return false;
                cursor->preparation_order_initialized=false;
            }
            cursor->preparation_position++;
        } else {
            cursor->preparation_position=0u;
            cursor->phase=RHO_CANONICAL_ENDPOINTS;
        }
        return true;
    case RHO_CANONICAL_ENDPOINTS:
        if (cursor->preparation_position<machine->components.len) {
            uint32_t i=cursor->preparation_position;
            RhoView view=rho_view(machine->components.items[i]);
            RhoEndpointVec *target=NULL;
            if (view.kind==RHO_SEND && view.nargs==2u) target=&machine->sends;
            else if ((view.kind==RHO_RECV || view.kind==RHO_RECV_PERSISTENT) && view.nargs==3u)
                target=&machine->recvs;
            if (!target) {cursor->preparation_position++;return true;}
            /* Reserve before taking a completed key. A pending/failed block
             * install retains the exact key producer and source occurrence. */
            CettaSegmentedStep prepared=rho_sort_buffer_prepare(&target->storage,
                target->len,sizeof(RhoEndpoint));
            if (prepared!=CETTA_SEGMENTED_READY) return prepared==CETTA_SEGMENTED_PENDING;
            if (!cursor->key_job.atom && !rho_process_key_cursor_begin(&cursor->key_job,
                    &machine->process_key_cache,view.args[0],true)) return false;
            if (!rho_process_key_cursor_advance(&cursor->key_job,&cursor->memo_steps)) return false;
            if (!cursor->key_job.done) return true;
            bool owned=false;
            RhoKeyProduct *product=rho_process_key_cursor_take(&cursor->key_job,&owned);
            rho_process_key_cursor_free(&cursor->key_job);
            if (!product) return false;
            bool okay=rho_endpoint_vec_push(target,i,view,product->text,owned);
            if (owned) {
                if (okay) product->text=NULL;
                rho_key_product_free(product);
            }
            if (!okay) return false;
            cursor->preparation_position++;
            return true;
        }
        if (!rho_endpoint_sort_init(&cursor->endpoint_sort,&machine->sends)) return false;
        cursor->phase=RHO_CANONICAL_SORT_SENDS;
        return true;
    case RHO_CANONICAL_SORT_SENDS:
        if (!rho_endpoint_sort_advance(&cursor->endpoint_sort)) return false;
        if (cursor->endpoint_sort.done) {
            rho_endpoint_sort_free(&cursor->endpoint_sort);
            if (!rho_endpoint_sort_init(&cursor->endpoint_sort,&machine->recvs)) return false;
            cursor->phase=RHO_CANONICAL_SORT_RECVS;
        }
        return true;
    case RHO_CANONICAL_SORT_RECVS:
        if (!rho_endpoint_sort_advance(&cursor->endpoint_sort)) return false;
        if (cursor->endpoint_sort.done) {
            rho_endpoint_sort_free(&cursor->endpoint_sort);
            machine->comm_index_loaded=true;
            cursor->phase=RHO_CANONICAL_ALIGN;
        }
        return true;
    case RHO_CANONICAL_ALIGN: {
        if (cursor->recv_pos==machine->recvs.len || cursor->send_pos==machine->sends.len) {
            cursor->phase=RHO_CANONICAL_FINISH;
            return true;
        }
        bool ready=false;int order=0;
        if (!rho_canonical_cursor_channel_probe(cursor,rho_endpoint_at(&machine->recvs,cursor->recv_pos),
                rho_endpoint_at(&machine->sends,cursor->send_pos),&ready,&order)) return false;
        if (!ready) return true;
        if (order<0) cursor->recv_pos++;
        else if (order>0) cursor->send_pos++;
        else {
            cursor->recv_start=cursor->recv_end=cursor->recv_pos;
            cursor->send_start=cursor->send_end=cursor->send_pos;
            cursor->phase=RHO_CANONICAL_RECV_RANGE;
        }
        return true;
    }
    case RHO_CANONICAL_RECV_RANGE: {
        if (cursor->recv_end==machine->recvs.len) {
            cursor->phase=RHO_CANONICAL_SEND_RANGE;return true;
        }
        bool ready=false;int order=0;
        if (!rho_canonical_cursor_channel_probe(cursor,rho_endpoint_at(&machine->recvs,cursor->recv_end),
                rho_endpoint_at(&machine->recvs,cursor->recv_start),&ready,&order)) return false;
        if (!ready) return true;
        if (!order) cursor->recv_end++;
        else cursor->phase=RHO_CANONICAL_SEND_RANGE;
        return true;
    }
    case RHO_CANONICAL_SEND_RANGE: {
        if (cursor->send_end<machine->sends.len) {
            bool ready=false;int order=0;
            if (!rho_canonical_cursor_channel_probe(cursor,rho_endpoint_at(&machine->sends,cursor->send_end),
                    rho_endpoint_at(&machine->sends,cursor->send_start),&ready,&order)) return false;
            if (!ready) return true;
            if (!order) {cursor->send_end++;return true;}
        }
        cursor->recv=cursor->recv_start;
        cursor->send=cursor->send_start;
        cursor->phase=RHO_CANONICAL_PAIR;
        return true;
    }
    case RHO_CANONICAL_PAIR:
        if (cursor->matching_pairs==UINT64_MAX) {
            rho_validation_set("rho canonical pair count exhausted");
            return false;
        }
        cursor->pair_mark=arena_mark(&cursor->scratch);
        cursor->matching_pairs++;
        if (machine->eval_context &&
            rho_view(rho_endpoint_at(&machine->sends,cursor->send)->view.args[1]).kind==RHO_EVAL_PAYLOAD) {
            Atom *expression=rho_view(rho_endpoint_at(&machine->sends,cursor->send)->view.args[1]).args[0];
            cursor->payload=rho_payload_open(&cursor->scratch,machine->eval_context,expression);
            if (!cursor->payload) return false;
            cursor->payload_position=0u;
            cursor->phase=RHO_CANONICAL_PAYLOAD;
            return true;
        }
        /* This machine's receiver body is already normalized. With no
         * variable anywhere in it, literal substitution is the identity and
         * can retain its syntax sharing. A deferred producer still owns its
         * evaluation and occurrence count, even when the binder is unused. */
        if (rho_view(rho_endpoint_at(&machine->sends,cursor->send)->view.args[1]).kind!=RHO_EVAL_PAYLOAD &&
            !atom_has_vars(rho_endpoint_at(&machine->recvs,cursor->recv)->view.args[2])) {
            if (!rho_vec_push(&cursor->bodies,rho_endpoint_at(&machine->recvs,cursor->recv)->view.args[2]))
                return false;
        } else if (rho_view(rho_endpoint_at(&machine->sends,cursor->send)->view.args[1]).kind!=RHO_EVAL_PAYLOAD) {
            /* The source machine already normalized this literal payload.
             * Retain that actual source rather than re-running a whole walk. */
            Atom *replacement=rho_unary(&cursor->scratch,"rho:quote",
                rho_endpoint_at(&machine->sends,cursor->send)->view.args[1]);
            RhoView *receive=&rho_endpoint_at(&machine->recvs,cursor->recv)->view;
            if (!replacement || !rho_substitution_begin(&cursor->substitution,
                    &cursor->scratch,receive->args[2],receive->args[1]->var_id,replacement))
                return false;
            cursor->phase=RHO_CANONICAL_SUBSTITUTION;return true;
        }
        cursor->body=0u;
        cursor->phase=RHO_CANONICAL_BODY;
        return true;
    case RHO_CANONICAL_PAYLOAD: {
        RhocalcPayloadStatus status=rho_payload_step(cursor->payload,1u);
        if (status==RHOCALC_PAYLOAD_FAILED) return false;
        if (status==RHOCALC_PAYLOAD_DONE) cursor->phase=RHO_CANONICAL_PAYLOAD_BODY;
        return true;
    }
    case RHO_CANONICAL_PAYLOAD_BODY:
        if (cursor->payload_position<cursor->payload->results.len) {
            RhoView *receive=&rho_endpoint_at(&machine->recvs,cursor->recv)->view;
            if (!rho_substitution_begin_payload(&cursor->substitution,&cursor->scratch,
                    receive->args[2],receive->args[1]->var_id,
                    cursor->payload->results.items[cursor->payload_position]))
                return false;
            cursor->phase=RHO_CANONICAL_SUBSTITUTION;
        } else {
            rho_payload_close(cursor->payload);cursor->payload=NULL;
            cursor->body=0u;cursor->phase=RHO_CANONICAL_BODY;
        }
        return true;
    case RHO_CANONICAL_SUBSTITUTION:
        if (!rho_substitution_advance(&cursor->substitution)) return false;
        if (!cursor->substitution.done) return true;
        if (!cursor->substitution.returned ||
                !rho_vec_push(&cursor->bodies,cursor->substitution.returned)) return false;
        rho_substitution_cursor_clear(&cursor->substitution);
        if (cursor->payload) {
            cursor->payload_position++;
            cursor->phase=RHO_CANONICAL_PAYLOAD_BODY;
        } else {
            cursor->body=0u;cursor->phase=RHO_CANONICAL_BODY;
        }
        return true;
    case RHO_CANONICAL_BODY:
        if (cursor->body<cursor->bodies.len) return rho_canonical_cursor_body(cursor);
        rho_vec_free(&cursor->bodies);
        rho_vec_init(&cursor->bodies);
        arena_reset(&cursor->scratch,cursor->pair_mark);
        cursor->send++;
        if (cursor->send==cursor->send_end) {
            cursor->send=cursor->send_start;
            cursor->recv++;
        }
        if (cursor->recv==cursor->recv_end) {
            cursor->recv_pos=cursor->recv_end;
            cursor->send_pos=cursor->send_end;
            cursor->phase=RHO_CANONICAL_ALIGN;
        } else cursor->phase=RHO_CANONICAL_PAIR;
        return true;
    case RHO_CANONICAL_BODY_PREPARE:
        if (!rho_reaction_view_cursor_advance(&cursor->view_job)) return false;
        if (!cursor->view_job.done) return true;
        if (!rho_reaction_view_cursor_take(&cursor->view_job)) return false;
        rho_reaction_view_cursor_free(&cursor->view_job);
        cursor->candidate_ready=true;
        if (!cursor->found) return rho_canonical_cursor_choose(cursor,0);
        cursor->comparison_live=true;
        if (!rho_reaction_compare_init(&cursor->comparison,&cursor->candidate_view,&cursor->best_view))
            return false;
        cursor->phase=RHO_CANONICAL_COMPARE;
        return true;
    case RHO_CANONICAL_COMPARE:
        if (!rho_reaction_compare_advance(&cursor->comparison)) return false;
        if (cursor->comparison.phase==RHO_COMPARE_DONE) {
            int order=cursor->comparison.order;
            cursor->component_probes+=cursor->comparison.component_probes;
            cursor->text_probes+=cursor->comparison.text_probes;
            rho_reaction_compare_free(&cursor->comparison);
            cursor->comparison_live=false;
            return rho_canonical_cursor_choose(cursor,order);
        }
        return true;
    case RHO_CANONICAL_CHOOSE_COPY: {
        if (!cursor->best_copy) {
            cursor->best_copy=atom_deep_copy_session_new(&cursor->selected[1u-cursor->selected_slot]);
            return cursor->best_copy!=NULL;
        }
        uint64_t used=0u;
        AtomDeepCopyStep status=atom_deep_copy_session_advance(cursor->best_copy,
            cursor->bodies.items[cursor->body],1u,&cursor->prepared_best_body,&used);
        if (used>1u) return false;
        if (status==ATOM_DEEP_COPY_REFUSED) return false;
        if (status==ATOM_DEEP_COPY_COMPLETE) {
            atom_deep_copy_session_free(cursor->best_copy);cursor->best_copy=NULL;
            cursor->phase=RHO_CANONICAL_CHOOSE_REMAP;
        }
        return true;
    }
    case RHO_CANONICAL_CHOOSE_REMAP:
        if (cursor->remap_position<cursor->candidate_view.body_len) {
            RhoKeyedAtom *item=rho_keyed_row(&cursor->candidate_view.body_items,cursor->remap_position++);
            RhoView copy=rho_view(cursor->prepared_best_body);
            if (copy.kind==RHO_PAR) {
                if (item->ordinal>=copy.nargs) return false;
                item->atom=copy.args[item->ordinal];
            } else item->atom=cursor->prepared_best_body;
            return true;
        } else {
            /* Text products belong to the same completed body interpretation;
             * copying changes storage, not keys, binders or source ordinals. */
            cursor->candidate_view.body=cursor->prepared_best_body;
            RhoReactionKeyView retired=cursor->best_view;
            cursor->best_view=cursor->candidate_view;
            cursor->candidate_view=retired;
            cursor->candidate_ready=cursor->best_view_ready;
            cursor->best_view_ready=true;
            cursor->best_body=cursor->prepared_best_body;cursor->prepared_best_body=NULL;
            cursor->best_recv=cursor->recv;cursor->best_send=cursor->send;
            cursor->best_body_index=cursor->body;cursor->best_ordinal=cursor->candidate_ordinal;
            cursor->selected_slot=1u-cursor->selected_slot;
            cursor->found=true;cursor->retire_selected=true;
            cursor->phase=RHO_CANONICAL_DISCARD;
            return true;
        }
    case RHO_CANONICAL_DISCARD:
        if (cursor->candidate_ready) {
            if (cursor->candidate_view.body_len) {
                free(rho_keyed_row(&cursor->candidate_view.body_items,--cursor->candidate_view.body_len)->key);
                return true;
            }
            if (rho_sort_buffer_has_storage(&cursor->candidate_view.body_items)) {
                rho_sort_buffer_release_one(&cursor->candidate_view.body_items);
                return true;
            }
            rho_reaction_key_view_free(&cursor->candidate_view);
            cursor->candidate_ready=false;
            return true;
        }
        if (cursor->retire_selected) {
            Arena *retired=&cursor->selected[1u-cursor->selected_slot];
            arena_free(retired);arena_init_detached(retired);
            cursor->retire_selected=false;
            return true;
        }
        cursor->body++;
        cursor->phase=RHO_CANONICAL_BODY;
        return true;
    case RHO_CANONICAL_FINISH:
        cursor->quiescent=!cursor->found;
        if (cursor->found) {
            /* Draft assembly requires a noninterning arena. Synchronous
             * interning profiles keep their admitted atomic constructor. */
            if (machine->arena->hashcons) {
                Atom *body=atom_deep_copy(machine->arena,cursor->best_body);
                if (!body) return false;
                cursor->next=rho_rebuild_reaction(machine->arena,&machine->components,
                    cursor->best_view.send_index,cursor->best_view.recv_index,body);
                if (!cursor->next) return false;
                machine->current_canonical_order=true;
                cursor->completed_scans=1u;cursor->done=true;
                return true;
            }
            if (!rho_reaction_build_cursor_begin(&cursor->build,machine->arena,&cursor->best_view))
                return false;
            cursor->phase=RHO_CANONICAL_BUILD;
            return true;
        }
        cursor->next=machine->current;
        cursor->completed_scans=1u;
        cursor->done=true;
        return true;
    case RHO_CANONICAL_BUILD:
        if (!rho_reaction_build_cursor_advance(&cursor->build)) return false;
        if (!cursor->build.done) return true;
        cursor->next=cursor->build.result;
        machine->current_canonical_order=true;
        cursor->completed_scans=1u;
        cursor->done=true;
        return true;
    }
    return false;
}

static void rho_canonical_cursor_account(const RhoCanonicalCursor *cursor,
        RhocalcRunAccount *account) {
    account->preparation_steps+=cursor->preparation_steps;
    account->selector_memo_steps+=cursor->memo_steps;
    account->substitution_steps+=cursor->substitution.account.substitution_steps;
    account->substitution_apply_steps+=cursor->substitution.account.substitution_apply_steps;
    account->substitution_memo_steps+=cursor->substitution.account.substitution_memo_steps;
    account->validation_steps+=cursor->substitution.account.validation_steps;
    account->validation_memo_steps+=cursor->substitution.account.validation_memo_steps;
    account->normalization_steps+=cursor->substitution.account.normalization_steps;
    account->normalization_memo_steps+=cursor->substitution.account.normalization_memo_steps;
    account->key_steps+=cursor->substitution.account.key_steps;
    account->key_bytes+=cursor->substitution.account.key_bytes;
    account->construction_children+=cursor->substitution.account.construction_children;
    account->construction_bytes+=cursor->substitution.account.construction_bytes;
    account->reaction_build_steps+=cursor->build.steps;
    account->reaction_copy_steps+=cursor->build.copy_steps;
    account->reaction_merge_steps+=cursor->build.merge_steps;
    account->reaction_children+=cursor->build.children;
    account->reaction_bytes+=cursor->build.bytes;
    account->channel_scan_probes+=cursor->channel_scan_probes;
    account->matching_pairs+=cursor->matching_pairs;
    account->body_candidates+=cursor->body_candidates;
    account->completed_scans+=cursor->completed_scans;
    account->comparison_component_probes+=cursor->component_probes;
    account->comparison_text_probes+=cursor->text_probes;
    if (cursor->comparison_live) {
        account->comparison_component_probes+=cursor->comparison.component_probes;
        account->comparison_text_probes+=cursor->comparison.text_probes;
    }
}

static bool rho_machine_select_streaming_canonical_successor(
    RhoMachine *machine, Atom **out_next, bool *out_quiescent) {
    if (!machine || !machine->arena || !machine->current || !out_next || !out_quiescent)
        return false;
    *out_next=NULL;
    *out_quiescent=true;
    RhoCanonicalCursor cursor;
    rho_canonical_cursor_init(&cursor,machine);
    bool okay=true;
    while (!cursor.done && okay) okay=rho_canonical_cursor_advance(&cursor);
    if (okay) {
        *out_next=cursor.next;
        *out_quiescent=cursor.quiescent;
    }
    rho_canonical_cursor_free(&cursor);
    return okay;
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
 * constructing, keying, sorting, and discarding a singleton frontier.  Zero
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

    for (uint32_t r = 0u; r < recvs.len; r++) {
        for (uint32_t s = 0u; s < sends.len; s++) {
            bool equal = false;
            if (!rho_single_comm_channels_equal(
                    rho_endpoint_at(&recvs,r), rho_endpoint_at(&sends,s), &equal)) {
                ok = false;
                goto done;
            }
            if (!equal) continue;
            if (selected_send) goto done;
            selected_recv = rho_endpoint_at(&recvs,r);
            selected_send = rho_endpoint_at(&sends,s);
        }
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
                char *key = result ? rho_key_proc(result) : NULL;
                if (!body || !result || !key ||
                    !rho_successor_set_acc_push_keyed(&acc, result, key)) {
                    free(key);
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
    if (!key) {
        rho_async_fail(executor, "could not construct rho channel key");
        return false;
    }
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
        Atom *persistent_recv = NULL;

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
        if (recv_async->view.kind == RHO_RECV_PERSISTENT) {
            persistent_recv = recv_async->atom;
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
        if (persistent_recv &&
            !rho_async_enqueue_proc(executor, persistent_recv)) {
            rho_vec_free(&bodies);
            rho_async_fail(executor,
                           "could not re-arm persistent rho receive");
            return false;
        }
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
    case RHO_RECV_PERSISTENT:
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
        .prefer_persistent_workers = true,
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
    return !atom_graph_any(proc, rho_atom_is_active_process, NULL);
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
                                       rho_endpoint_at(sends,s), rho_endpoint_at(recvs,r),
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
    if (!rho_collect_par(arena,proc,&components) ||
        !rho_collect_endpoints(&components,&sends,&recvs)) {
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
        int cmp = rho_endpoint_key_compare(
            rho_endpoint_at(&recvs,recv_pos), rho_endpoint_at(&sends,send_pos));
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
               rho_endpoint_key_compare(
                   rho_endpoint_at(&recvs,recv_pos),
                   rho_endpoint_at(&recvs,recv_start)) == 0) {
            recv_pos++;
        }
        while (send_pos < sends.len &&
               rho_endpoint_key_compare(
                   rho_endpoint_at(&sends,send_pos),
                   rho_endpoint_at(&sends,send_start)) == 0) {
            send_pos++;
        }
        group->recv_end = recv_pos;
        group->send_end = send_pos;

        if ((recv_pos - recv_start) != 1u || (send_pos - send_start) != 1u) {
            group->reject_reason = RHO_MACRO_REJECT_CONTENTION;
            exact_groups++;
            continue;
        }

        group->send = rho_endpoint_at(&sends,send_start);
        group->recv = rho_endpoint_at(&recvs,recv_start);
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
            if (!body) {
                ok = false;
                goto done;
            }
            if (!rho_proc_is_quiet(body)) {
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
    char *location_key;
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
        return NULL;
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
        return NULL;
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
    return rho_str_take(&out,"sig()");
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
    for (uint32_t i = 0; i < vec->len; i++) {
        free(vec->items[i].location_key);
    }
    free(vec->items);
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static bool rhocost_token_vec_push(RhoCostTokenVec *vec,
                                   uint32_t component_index,
                                   Atom *location,
                                   char *location_key,
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
    vec->items[vec->len].location = location;
    vec->items[vec->len].location_key = location_key;
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
    if (!proc) {rho_str_fail(out);return;}
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
            if (!rho_alpha_push(env,view.args[1]->var_id)) {rho_str_fail(out);return;}
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
    case RHO_RECV_PERSISTENT:
    case RHO_QUOTE:
    case RHO_BAD:
        (void)rho_str_append(out, "?bad-proc");
        return;
    }
}

static void rhocost_key_term_into(Atom *term, RhoAlphaEnv *env, RhoStr *out) {
    if (!term) {rho_str_fail(out);return;}
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
        if (!signature_key) {rho_str_fail(out);return;}
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
        if (!signature_key) {rho_str_fail(out);return;}
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
    return rho_str_take(&out,"");
}

static char *rhocost_key_proc(Atom *proc) {
    RhoAlphaEnv env;
    RhoStr out = {0};
    rho_alpha_init(&env);
    rhocost_key_proc_into(proc, &env, &out);
    rho_alpha_free(&env);
    return rho_str_take(&out,"");
}

static char *rhocost_key_term(Atom *term) {
    RhoAlphaEnv env;
    RhoStr out = {0};
    rho_alpha_init(&env);
    rhocost_key_term_into(term, &env, &out);
    rho_alpha_free(&env);
    return rho_str_take(&out,"");
}

static bool rhocost_collect_proc_par(Arena *arena, Atom *proc, RhoAtomVec *out);

static Atom *rhocost_proc_par_from_vec(Arena *arena, RhoAtomVec *vec) {
    if (vec->len == 0) return rho_nil(arena);
    if (vec->len == 1) return vec->items[0];

    RhoKeyedAtom *items=rho_keyed_atoms_new(vec,rhocost_key_proc);
    if (!items) return NULL;
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
    if (!name) return NULL;
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
            if (!rhocost_collect_proc_par(arena,view.args[i],&flat)) {rho_vec_free(&flat);return NULL;}
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

static bool rhocost_collect_proc_par(Arena *arena, Atom *proc, RhoAtomVec *out) {
    Atom *norm=rhocost_normalize_proc(arena,proc);
    if (!norm) return false;
    RhoView view=rho_view(norm);
    if (view.kind==RHO_NIL) return true;
    if (view.kind==RHO_PAR) {
        for (uint32_t i=0u;i<view.nargs;i++)
            if (!rhocost_collect_proc_par(arena,view.args[i],out)) return false;
        return true;
    }
    return rho_vec_push(out,norm);
}

static bool rhocost_collect_term_par(Arena *arena, Atom *term, RhoAtomVec *out);

static Atom *rhocost_term_par_from_vec(Arena *arena, RhoAtomVec *vec) {
    if (vec->len == 0) return rhocost_nil(arena);
    if (vec->len == 1) return vec->items[0];
    RhoKeyedAtom *items=rho_keyed_atoms_new(vec,rhocost_key_term);
    if (!items) return NULL;
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
            if (!rhocost_collect_term_par(arena,view.args[i],&flat)) {rho_vec_free(&flat);return NULL;}
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

static bool rhocost_collect_term_par(Arena *arena, Atom *term, RhoAtomVec *out) {
    Atom *norm=rhocost_normalize_term(arena,term);
    if (!norm) return false;
    RhoCostTermView view=rhocost_term_view(norm);
    if (view.kind==RHOCOST_TERM_NIL) return true;
    if (view.kind==RHOCOST_TERM_PAR) {
        for (uint32_t i=0u;i<view.nargs;i++)
            if (!rhocost_collect_term_par(arena,view.args[i],out)) return false;
        return true;
    }
    return rho_vec_push(out,norm);
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
    if (!norm) return NULL;
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

static Atom *rhocost_subst_name(Arena *arena, Atom *name,
                                VarId var_id, Atom *replacement_term) {
    Atom *norm = rhocost_normalize_name(arena, name);
    if (!norm) return NULL;
    if (norm->kind == ATOM_VAR && var_id_eq(norm->var_id, var_id)) {
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
    if (!norm) return NULL;
    if (norm->kind == ATOM_VAR && var_id_eq(norm->var_id, var_id)) {
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
    if (!key) return false;
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
            if (!recv_key || !send_key) {
                free(recv_key);
                free(send_key);
                return false;
            }
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

static bool rhocost_collect_term_components(Arena *arena, Atom *term, RhoAtomVec *out) {
    return rhocost_collect_term_par(arena,term,out);
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

static bool rhocost_trace_components_sort(RhoCostTraceComponentVec *components) {
    if (components->len < 2) return true;
    RhoCostKeyedTraceComponent *keyed =
        cetta_malloc(sizeof(RhoCostKeyedTraceComponent) * components->len);
    for (uint32_t i = 0; i < components->len; i++) {
        keyed[i].component = components->items[i];
        keyed[i].key = rhocost_key_term(components->items[i].term);
        keyed[i].ordinal = i;
        if (!keyed[i].key) {
            for (uint32_t j = 0; j < i; j++) free(keyed[j].key);
            free(keyed);
            return false;
        }
    }
    qsort(keyed, components->len, sizeof(RhoCostKeyedTraceComponent),
          rhocost_keyed_trace_component_cmp);
    for (uint32_t i = 0; i < components->len; i++) {
        components->items[i] = keyed[i].component;
        free(keyed[i].key);
    }
    free(keyed);
    return true;
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

    if (!rhocost_trace_components_sort(&next)) {
        rhocost_trace_component_vec_free(&next);
        return false;
    }
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
        if (!rhocost_collect_term_par(arena,components->items[i],&out)) {
            rho_vec_free(&out);return NULL;
        }
    }
    for (uint32_t i = 0; i < extra_len; i++) {
        if (!rhocost_collect_term_par(arena,extras[i],&out)) {
            rho_vec_free(&out);return NULL;
        }
    }
    Atom *term = rhocost_term_par_from_vec(arena, &out);
    rho_vec_free(&out);
    return term;
}

static char *rhocost_step_transition_key(Atom *result, Atom *consumed_sig) {
    char *term_key = rhocost_key_term(result);
    char *sig_key = rhocost_key_signature(consumed_sig);
    RhoStr out = {0};
    if (!term_key || !sig_key) {free(term_key);free(sig_key);return NULL;}
    (void)rho_str_appendf(&out, "%s//cost(%s)", term_key, sig_key);
    free(term_key);
    free(sig_key);
    return rho_str_take(&out,"");
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
        extras[i + 1] = rhocost_purse(arena, token->location, token->rest);
    }

    next = rhocost_rebuild_result(arena, components, skip, skip_len,
                                  extras, extra_len);
    if (!next) return false;
    key = rhocost_step_transition_key(next, consumed_sig);
    if (!key) return false;
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
                                             const char *location_key,
                                             RhoIndexVec *available) {
    rhocost_index_vec_init(available);
    for (uint32_t i = 0; i < tokens->len; i++) {
        if (strcmp(tokens->items[i].location_key, location_key) == 0 &&
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
                char *location_key = rhocost_key_name(view.args[0]);
                if (!location_key) {
                    ok = false;
                    break;
                }
                ok = rhocost_token_vec_push(
                    &tokens, i, view.args[0], location_key,
                    stack.args[0], stack.args[1]);
                if (!ok) free(location_key);
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
        if (!consumed_sig || (compute_contracta && !body)) {
            ok = false;
            break;
        }
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
            if (compute_contracta && !body) {
                ok = false;
                break;
            }
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
    Atom *normalized_signature = NULL;
    Atom *expected_contractum = NULL;
    Atom *normalized_expected = NULL;
    Atom *normalized_contractum = NULL;
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
        !rhocost_check_term_rec(task->contractum)) {
        return false;
    }
    normalized_signature = rhocost_normalize_signature(
        arena, task->plan.consumed_sig);
    if (!normalized_signature || !atom_eq(normalized_signature, demand))
        return false;

    expected_contractum = rhocost_subst_term(
        arena, recv.args[2], recv.args[1]->var_id, send.args[1]);
    if (!expected_contractum) return false;
    normalized_expected = rhocost_normalize_term(arena, expected_contractum);
    normalized_contractum = rhocost_normalize_term(arena, task->contractum);
    if (!normalized_expected || !normalized_contractum ||
        !atom_eq(normalized_expected, normalized_contractum)) {
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
        char *location_key;
        if (purse.kind != RHOCOST_TERM_PURSE || purse.nargs != 2u) {
            goto done;
        }
        stack = rhocost_term_view(purse.args[1]);
        if (stack.kind != RHOCOST_TERM_STACK_CONS || stack.nargs != 2u) {
            goto done;
        }
        location_key = rhocost_key_name(purse.args[0]);
        if (!location_key || strcmp(channel_key, location_key) != 0) {
            free(location_key);
            goto done;
        }
        free(location_key);
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
    Atom *normalized = rhocost_normalize_term(arena, term);
    if (!normalized || !rhocost_collect_term_components(arena, normalized,
                                                       &components)) {
        rho_vec_free(&components);
        return false;
    }
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
    if (!rhocost_trace_components_sort(&components)) goto done;

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
    *out_next = successors.items[machine->rotating_turn % successors.len];
    machine->current_canonical_order = true;
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
    if (!rho_collect_par(arena,proc,&components) ||
        !rho_collect_endpoints(&components,&sends,&recvs)) {
        rho_endpoint_vec_free(&sends);
        rho_endpoint_vec_free(&recvs);
        rho_vec_free(&components);
        return false;
    }

    while (recv_pos < recvs.len && send_pos < sends.len) {
        int cmp = rho_endpoint_key_compare(
            rho_endpoint_at(&recvs,recv_pos), rho_endpoint_at(&sends,send_pos));
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
               rho_endpoint_key_compare(
                   rho_endpoint_at(&recvs,recv_pos),
                   rho_endpoint_at(&recvs,recv_start)) == 0) {
            recv_pos++;
        }
        while (send_pos < sends.len &&
               rho_endpoint_key_compare(
                   rho_endpoint_at(&sends,send_pos),
                   rho_endpoint_at(&sends,send_start)) == 0) {
            send_pos++;
        }

        for (uint32_t r = recv_start; r < recv_pos; r++) {
            for (uint32_t s = send_start; s < send_pos; s++) {
                if (!rho_emit_comm_results(arena, &components,
                                           rho_endpoint_at(&sends,s),
                                           rho_endpoint_at(&recvs,r),
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

bool rhocalc_reduce_to_quiescence_with_profile(Arena *arena, Atom *proc,
                                               const RhoRuntimeProfile *profile,
                                               RhoReductionResult *out) {
    return rhocalc_reduce_to_quiescence_with_eval_context(arena, proc, profile,
                                                          NULL, out);
}

typedef enum {
    RHO_RUN_LOAD,
    RHO_RUN_VALIDATE,
    RHO_RUN_NORMALIZE,
    RHO_RUN_SELECT,
    RHO_RUN_SELECTOR_RETIRE,
    RHO_RUN_COMMIT,
    RHO_RUN_COMPACT,
    RHO_RUN_COMPACT_COPY,
    RHO_RUN_COMPACT_COMMIT,
    RHO_RUN_EXPORT,
    RHO_RUN_EXPORT_COPY,
    RHO_RUN_EXPORT_NORMALIZE
} RhoRunPhase;

struct RhocalcRun {
    RhoMachine machine;
    RhocalcEvalContext context;
    _Atomic(RhocalcPayloadFailure) payload_failure;
    RhoCanonicalCursor cursor;
    RhoCheckCursor check;
    RhoNormalizeCursor normalization;
    AtomDeepCopySession *transfer;
    RhocalcRunAccount account;
    RhocalcCommAddress pending_comm, last_comm;
    RhoReductionResult result;
    Atom *input, *next, *transfer_root;
    RhoRunPhase phase;
    RhocalcRunStatus status;
    bool cursor_live, quiescent, atomic_selection, has_pending_comm, has_last_comm;
    bool retiring, normalization_retired;
    char error[256];
};

static void rho_run_retire_cursor(RhocalcRun *run) {
    if (!run->cursor_live) return;
    rho_canonical_cursor_account(&run->cursor,&run->account);
    rho_canonical_cursor_free(&run->cursor);
    run->cursor_live=false;
}

static bool rho_run_fail(RhocalcRun *run) {
    const char *message=rhocalc_last_validation_error();
    snprintf(run->error,sizeof(run->error),"%s",
        message && *message ? message : "rho run transition failed");
    run->status=RHOCALC_RUN_FAILED;
    return false;
}

static RhocalcRun *rho_run_open(Arena *arena, Atom *proc,
                                const RhoRuntimeProfile *profile,
                                const RhocalcEvalContext *eval_context,
                                bool atomic_selection, bool held_input) {
    rho_validation_clear();
    if (!arena || !proc || !rho_runtime_profile_supported(profile)) return NULL;
    if (profile->threaded) {
        rho_validation_set("resumable rho runs require sequential execution");
        return NULL;
    }
    RhocalcRun *run=calloc(1u,sizeof(*run));
    if (!run) return NULL;
    atomic_init(&run->payload_failure,RHOCALC_PAYLOAD_FAILURE_NONE);
    if (eval_context) {
        run->context=*eval_context;
        run->context.payload_failure=&run->payload_failure;
    }
    rho_machine_init(&run->machine,arena,profile,eval_context ? &run->context : NULL);
    if (!atomic_selection) {
        arena_set_hashcons(&run->machine.process_arenas[0], NULL);
        arena_set_hashcons(&run->machine.process_arenas[1], NULL);
    }
    run->input=atomic_selection || held_input ? proc : atom_deep_copy(run->machine.arena,proc);
    if (!run->input) {
        rhocalc_run_close(run);
        return NULL;
    }
    run->phase=RHO_RUN_LOAD;
    run->status=RHOCALC_RUN_PENDING;
    run->atomic_selection=atomic_selection;
    return run;
}

RhocalcRun *rhocalc_run_open(Arena *arena, Atom *proc,
                            const RhoRuntimeProfile *profile,
                            const RhocalcEvalContext *eval_context) {
    return rho_run_open(arena,proc,profile,eval_context,false,false);
}

RhocalcRun *rhocalc_run_open_held(Arena *arena, Atom *proc,
                                 const RhoRuntimeProfile *profile,
                                 const RhocalcEvalContext *eval_context) {
    return rho_run_open(arena,proc,profile,eval_context,false,true);
}

/* A transfer borrows machine.current until its copy completes. In particular,
 * compaction cannot retire that generation, and export cannot publish its
 * destination root, while the copy session is pending. */
static bool rho_run_copy_begin(RhocalcRun *run, Arena *destination) {
    if (run->transfer) return false;
    run->transfer_root=NULL;
    run->transfer=atom_deep_copy_session_new(destination);
    return run->transfer!=NULL;
}

static AtomDeepCopyStep rho_run_copy_advance(RhocalcRun *run, uint64_t *account) {
    uint64_t used=0u;
    AtomDeepCopyStep status=atom_deep_copy_session_advance(run->transfer,
        run->machine.current,1u,&run->transfer_root,&used);
    if (used>1u || !rho_run_charge(account,used)) return ATOM_DEEP_COPY_REFUSED;
    if (status==ATOM_DEEP_COPY_COMPLETE) {
        atom_deep_copy_session_free(run->transfer);run->transfer=NULL;
    }
    return status;
}

static bool rho_run_advance(RhocalcRun *run) {
    RhoMachine *machine=&run->machine;
    switch (run->phase) {
    case RHO_RUN_LOAD:
        if (!run->atomic_selection) {
            run->check.allow_payloads=machine->eval_context!=NULL;
            if (!rho_check_cursor_push(&run->check,run->input,false)) return rho_run_fail(run);
            run->phase=RHO_RUN_VALIDATE;
            return true;
        }
        if (!rho_machine_load_process(machine,run->input)) return rho_run_fail(run);
        run->input=NULL;
        run->phase=RHO_RUN_SELECT;
        return true;
    case RHO_RUN_VALIDATE:
        if (!rho_run_charge(&run->account.validation_steps,1u)) return rho_run_fail(run);
        RhoCheckStatus checked=rho_check_cursor_advance(&run->check,&run->account);
        if (checked==RHO_CHECK_REFUSED || checked==RHO_CHECK_INCOMPATIBLE)
            return rho_run_fail(run);
        if (checked==RHO_CHECK_COMPLETE) {
            rho_check_cursor_free(&run->check);
            run->normalization.arena=machine->arena;
            if (!rho_normalize_cursor_push(&run->normalization,run->input,false)) return rho_run_fail(run);
            run->phase=RHO_RUN_NORMALIZE;
        }
        return true;
    case RHO_RUN_NORMALIZE:
        if (!rho_run_charge(&run->account.normalization_steps,1u) ||
            !rho_normalize_cursor_advance(&run->normalization,&run->account)) return rho_run_fail(run);
        if (run->normalization.done) {
            machine->current=run->normalization.returned;
            machine->current_canonical_order=true;
            rho_normalize_cursor_free(&run->normalization);
            run->input=NULL;
            run->phase=RHO_RUN_SELECT;
        }
        return true;
    case RHO_RUN_SELECT:
        if (run->atomic_selection || machine->profile.scheduler_policy==RHO_SCHEDULER_ROTATING) {
            if (!rho_machine_select_successor(machine,&run->next,&run->quiescent))
                return rho_run_fail(run);
        } else {
            if (!run->cursor_live) {
                rho_canonical_cursor_init(&run->cursor,machine);
                run->cursor_live=true;
            }
            run->account.selector_steps++;
            if (!rho_canonical_cursor_advance(&run->cursor)) return rho_run_fail(run);
            if (!run->cursor.done) return true;
            run->next=run->cursor.next;
            run->quiescent=run->cursor.quiescent;
            run->has_pending_comm=run->cursor.found;
            if (run->has_pending_comm) {
                RhoCanonicalCursor *cursor=&run->cursor;
                run->pending_comm=(RhocalcCommAddress){
                    .source_reductions=run->result.reductions_taken,
                    .send_component=rho_endpoint_at(&machine->sends,cursor->best_send)->component_index,
                    .recv_component=rho_endpoint_at(&machine->recvs,cursor->best_recv)->component_index,
                    .body_index=cursor->best_body_index,
                    .candidate_ordinal=cursor->best_ordinal};
            }
            run->phase=RHO_RUN_SELECTOR_RETIRE;
            return true;
        }
        run->phase=RHO_RUN_COMMIT;
        return true;
    case RHO_RUN_SELECTOR_RETIRE:
        run->account.selector_steps++;
        if (rho_canonical_storage_release_one(&run->cursor)) return true;
        rho_run_retire_cursor(run);
        run->phase=RHO_RUN_COMMIT;
        return true;
    case RHO_RUN_COMMIT:
        /* Preserve the public driver's select-before-limit ordering, including
         * payload work used to establish that another reduction exists. */
        if (run->quiescent || run->result.reductions_taken==machine->profile.reduction_limit) {
            run->result.status=run->quiescent ? RHOCALC_REDUCTION_QUIESCENT
                : RHOCALC_REDUCTION_LIMIT_EXHAUSTED;
            run->phase=RHO_RUN_EXPORT;
            return true;
        }
        machine->current=run->next;
        run->next=NULL;
        if (run->has_pending_comm) {
            run->last_comm=run->pending_comm;
            run->has_last_comm=true;
            run->has_pending_comm=false;
        }
        run->result.reductions_taken++;
        machine->reductions_since_compaction++;
        run->phase=rho_machine_compaction_enabled(machine) &&
            machine->reductions_since_compaction==RHO_MACHINE_COMPACTION_INTERVAL
                ? RHO_RUN_COMPACT : RHO_RUN_SELECT;
        return true;
    case RHO_RUN_COMPACT:
        if (run->atomic_selection) {
            if (!rho_machine_compact_current(machine)) return rho_run_fail(run);
            run->phase=RHO_RUN_SELECT;
        } else {
            if (!rho_run_copy_begin(run,&machine->process_arenas[1u-machine->active_process_arena]))
                return rho_run_fail(run);
            run->phase=RHO_RUN_COMPACT_COPY;
        }
        return true;
    case RHO_RUN_COMPACT_COPY: {
        AtomDeepCopyStep status=rho_run_copy_advance(run,&run->account.compaction_copy_steps);
        if (status==ATOM_DEEP_COPY_REFUSED) return rho_run_fail(run);
        if (status==ATOM_DEEP_COPY_COMPLETE) run->phase=RHO_RUN_COMPACT_COMMIT;
        return true;
    }
    case RHO_RUN_COMPACT_COMMIT:
        if (!rho_machine_commit_compaction(machine,run->transfer_root)) return rho_run_fail(run);
        run->transfer_root=NULL;run->phase=RHO_RUN_SELECT;
        return true;
    case RHO_RUN_EXPORT:
        if (run->atomic_selection) {
            run->result.residual=rho_machine_export_current(machine);
            if (!run->result.residual) return rho_run_fail(run);
            run->status=RHOCALC_RUN_DONE;
        } else if (!machine->current_canonical_order) {
            run->normalization.arena=machine->arena;
            if (!rho_normalize_cursor_push(&run->normalization,machine->current,false))
                return rho_run_fail(run);
            run->phase=RHO_RUN_EXPORT_NORMALIZE;
        } else {
            if (!rho_run_copy_begin(run,machine->result_arena)) return rho_run_fail(run);
            run->phase=RHO_RUN_EXPORT_COPY;
        }
        return true;
    case RHO_RUN_EXPORT_COPY: {
        AtomDeepCopyStep status=rho_run_copy_advance(run,&run->account.export_copy_steps);
        if (status==ATOM_DEEP_COPY_REFUSED) return rho_run_fail(run);
        if (status!=ATOM_DEEP_COPY_COMPLETE) return true;
        run->result.residual=run->transfer_root;run->transfer_root=NULL;
        run->status=RHOCALC_RUN_DONE;
        return true;
    }
    case RHO_RUN_EXPORT_NORMALIZE:
        if (!rho_run_charge(&run->account.normalization_steps,1u) ||
            !rho_normalize_cursor_advance(&run->normalization,&run->account)) return rho_run_fail(run);
        if (!run->normalization.done) return true;
        machine->current=run->normalization.returned;
        machine->current_canonical_order=true;
        rho_normalize_cursor_free(&run->normalization);
        run->phase=RHO_RUN_EXPORT;
        return true;
    }
    return rho_run_fail(run);
}

RhocalcRunStatus rhocalc_run_poll(RhocalcRun *run, uint64_t allowance, uint64_t *used) {
    uint64_t spent=0u;
    if (used) *used=0u;
    if (!run || run->retiring) return RHOCALC_RUN_FAILED;
    if (allowance && run->status==RHOCALC_RUN_PENDING) rho_validation_clear();
    while (spent<allowance && run->status==RHOCALC_RUN_PENDING) {
        if (run->account.driver_steps==UINT64_MAX) {
            rho_validation_set("rho driver step account exhausted");
            rho_run_fail(run);
            break;
        }
        run->account.driver_steps++;
        spent++;
        if (!rho_run_advance(run)) break;
    }
    if (used) *used=spent;
    return run->status;
}

bool rhocalc_run_result(const RhocalcRun *run, RhoReductionResult *out) {
    if (!run || run->retiring || !out || run->status!=RHOCALC_RUN_DONE) return false;
    *out=run->result;
    return true;
}

RhocalcRunAccount rhocalc_run_account(const RhocalcRun *run) {
    if (!run) return (RhocalcRunAccount){0};
    RhocalcRunAccount account=run->account;
    if (run->cursor_live) rho_canonical_cursor_account(&run->cursor,&account);
    account.reductions_taken=run->result.reductions_taken;
    return account;
}

bool rhocalc_run_last_comm(const RhocalcRun *run, RhocalcCommAddress *out) {
    if (!run || !out || !run->has_last_comm) return false;
    *out=run->last_comm;
    return true;
}

RhocalcPayloadFailure rhocalc_run_payload_failure(const RhocalcRun *run) {
    return run ? atomic_load_explicit(&run->payload_failure,memory_order_relaxed)
        : RHOCALC_PAYLOAD_FAILURE_NONE;
}

const char *rhocalc_run_error(const RhocalcRun *run) {
    return run && run->error[0] ? run->error : NULL;
}

CettaOwnedReleaseStep rhocalc_run_release_poll(RhocalcRun **owner,
    uint64_t allowance, bool allow_atomic, uint64_t *used) {
    uint64_t spent = 0u;
    if (used) *used = 0u;
    if (!owner || !*owner) return CETTA_OWNED_RELEASE_COMPLETE;
    RhocalcRun *run = *owner;
    if (!allowance) return CETTA_OWNED_RELEASE_PENDING;
    if (!run->retiring) { run->retiring = true; spent++; }
    while (spent < allowance) {
        if (run->normalization.frames.length) {
            rho_normalize_cursor_release_one(&run->normalization);
        } else if (run->normalization.frames.slots.blocks) {
            cetta_record_stack_release_storage_one(&run->normalization.frames);
        } else if (run->normalization.memo_cursor.grown.blocks) {
            cetta_segmented_array_release_one(&run->normalization.memo_cursor.grown);
        } else if (run->normalization.memo.slots.blocks) {
            cetta_node_memo_release_one(&run->normalization.memo);
        } else if (!run->normalization_retired) {
            rho_normalize_cursor_free(&run->normalization);
            run->normalization_retired = true;
        } else if (run->cursor_live && rho_canonical_storage_release_one(&run->cursor)) {
            /* One row disposition or backing block; opaque jobs and cache
             * finalizers remain retained for their separate admission. */
        } else {
            /* Copy metadata, nested payloads, canonical caches and arena
             * finalizers retain their existing indivisible providers.
             * A responsive caller must retain this owner until that work
             * is separately admitted; no pending prefix is discarded. */
            if (!allow_atomic) {
                if (used) *used = spent;
                return CETTA_OWNED_RELEASE_ATOMIC;
            }
            atom_deep_copy_session_free(run->transfer);
            rho_run_retire_cursor(run);
            rho_check_cursor_free(&run->check);
            rho_machine_free(&run->machine);
            free(run);
            *owner = NULL;
            spent++;
            if (used) *used = spent;
            return CETTA_OWNED_RELEASE_COMPLETE;
        }
        spent++;
    }
    if (used) *used = spent;
    return CETTA_OWNED_RELEASE_PENDING;
}

void rhocalc_run_close(RhocalcRun *run) {
    while (rhocalc_run_release_poll(&run, UINT64_MAX, true, NULL) !=
            CETTA_OWNED_RELEASE_COMPLETE) {}
}

bool rhocalc_reduce_to_quiescence_with_eval_context(
    Arena *arena, Atom *proc, const RhoRuntimeProfile *profile,
    const RhocalcEvalContext *eval_context, RhoReductionResult *out) {
    if (!arena || !proc || !profile || !out) return false;
    *out=(RhoReductionResult){0};
    if (!rho_runtime_profile_supported(profile)) return false;
    if (profile->threaded)
        return rhocalc_reduce_to_quiescence_threaded(arena,proc,profile,eval_context,out);
    /* The synchronous client uses the same phases while retaining the existing
     * singleton/exhaustive selection optimizations. */
    RhocalcRun *run=rho_run_open(arena,proc,profile,eval_context,true,false);
    if (!run) return false;
    while (rhocalc_run_poll(run,UINT64_MAX,NULL)==RHOCALC_RUN_PENDING) {}
    bool okay=rhocalc_run_result(run,out);
    if (eval_context && eval_context->payload_failure) {
        RhocalcPayloadFailure failure=rhocalc_run_payload_failure(run);
        if (failure!=RHOCALC_PAYLOAD_FAILURE_NONE)
            atomic_store_explicit(eval_context->payload_failure,failure,memory_order_relaxed);
    }
    if (!okay && rhocalc_run_error(run)) rho_validation_set("%s",rhocalc_run_error(run));
    rhocalc_run_close(run);
    return okay;
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
    if (!current) return false;

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
    if (semantic_profile == RHOCALC_SEMANTIC_PROFILE_RHOMETTA &&
        eval_context == NULL) {
        rho_validation_set("rhometta execution requires an evaluation context");
        return false;
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

typedef struct {
    Atom *node;
    uint32_t next;
} RhoValueFrame;

struct RhocalcValueObservation {
    Arena *arena;
    Atom *root, *result;
    Atom **values;
    RhoValueFrame *stack;
    RhoValueFrame inline_stack[16];
    size_t depth, capacity, count, filled;
    RhocalcRunStatus status;
    enum { RHO_VALUES_COUNT, RHO_VALUES_ALLOCATE,
           RHO_VALUES_FILL, RHO_VALUES_FINISH } phase;
};

static bool rho_values_push(RhocalcValueObservation *o, Atom *node) {
    /* Preserve the synchronous observer's depth boundary. */
    if (!node || o->depth > 4096u) return false;
    if (o->depth == o->capacity) {
        size_t next = o->capacity ? o->capacity * 2u : 16u;
        RhoValueFrame *grown = realloc(o->stack == o->inline_stack ? NULL : o->stack,
            next * sizeof(*grown));
        if (!grown) return false;
        if (o->stack == o->inline_stack)
            memcpy(grown, o->stack, o->depth * sizeof(*grown));
        o->stack = grown; o->capacity = next;
    }
    o->stack[o->depth++] = (RhoValueFrame){.node = node};
    return true;
}

static bool rho_values_init(RhocalcValueObservation *o, Arena *arena, Atom *residual) {
    *o = (RhocalcValueObservation){.arena = arena, .root = residual,
        .stack = o->inline_stack, .capacity = sizeof(o->inline_stack) / sizeof(*o->inline_stack),
        .status = RHOCALC_RUN_PENDING};
    return arena && residual && rho_values_push(o, residual);
}

RhocalcValueObservation *rhocalc_values_open(Arena *arena, Atom *residual) {
    if (!arena || !residual) return NULL;
    RhocalcValueObservation *o = malloc(sizeof(*o));
    if (!o) return NULL;
    if (!rho_values_init(o, arena, residual)) {rhocalc_values_close(o); return NULL;}
    return o;
}

static bool rho_values_advance(RhocalcValueObservation *o) {
    if (o->phase == RHO_VALUES_ALLOCATE) {
        if (o->count > UINT32_MAX || o->count > SIZE_MAX / sizeof(*o->values))
            return false;
        if (o->count) {
            o->values = arena_alloc(o->arena, o->count * sizeof(*o->values));
            if (!o->values) return false;
        }
        o->phase = RHO_VALUES_FILL;
        return rho_values_push(o, o->root);
    }
    if (o->phase == RHO_VALUES_FINISH) {
        if (o->filled != o->count) return false;
        o->result = atom_expr(o->arena, o->values, (CettaExprLen)o->count);
        if (!o->result) return false;
        o->status = RHOCALC_RUN_DONE;
        return true;
    }
    if (!o->depth) {
        o->phase = o->phase == RHO_VALUES_COUNT
            ? RHO_VALUES_ALLOCATE : RHO_VALUES_FINISH;
        return true;
    }
    RhoValueFrame *frame = &o->stack[o->depth - 1u];
    RhoView view = rho_view(frame->node);
    if (view.kind == RHO_VAL) {
        if (o->phase == RHO_VALUES_COUNT) {
            if (o->count == UINT32_MAX) return false;
            o->count++;
        } else {
            if (o->filled >= o->count) return false;
            o->values[o->filled++] = view.args[0];
        }
        o->depth--;
    } else if (view.kind == RHO_PAR && frame->next < view.nargs) {
        Atom *child = view.args[frame->next++];
        return rho_values_push(o, child);
    } else o->depth--;
    return true;
}

RhocalcRunStatus rhocalc_values_poll(RhocalcValueObservation *o,
    uint64_t allowance, uint64_t *used) {
    if (used) *used = 0u;
    if (!o) return RHOCALC_RUN_FAILED;
    uint64_t spent = 0u;
    while (spent < allowance && o->status == RHOCALC_RUN_PENDING) {
        spent++;
        if (!rho_values_advance(o)) o->status = RHOCALC_RUN_FAILED;
    }
    if (used) *used = spent;
    return o->status;
}

Atom *rhocalc_values_result(const RhocalcValueObservation *o) {
    return o && o->status == RHOCALC_RUN_DONE ? o->result : NULL;
}

void rhocalc_values_close(RhocalcValueObservation *o) {
    if (o) {
        if (o->stack != o->inline_stack) free(o->stack);
        free(o);
    }
}

Atom *rhocalc_observe_top_level_values(Arena *arena, Atom *residual) {
    RhocalcValueObservation o;
    if (!rho_values_init(&o, arena, residual)) return NULL;
    RhocalcRunStatus status = rhocalc_values_poll(&o, UINT64_MAX, NULL);
    Atom *result = status == RHOCALC_RUN_DONE ? rhocalc_values_result(&o) : NULL;
    if (o.stack != o.inline_stack) free(o.stack);
    return result;
}
