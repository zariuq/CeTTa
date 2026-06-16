#include "rhocalc_core.h"
#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "eval.h"
#include "match.h"
#include "parallel_executor.h"
#include "stats.h"

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
    return atom_var_with_spelling(arena, var->sym_id, var->var_id);
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
    return atom_var_with_spelling(arena, var->sym_id, id);
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

/* Read-only-snapshot support: a per-payload map from each original space to its
 * copy-on-write overlay.  Every space a payload could reach gets one, so writes
 * land in per-payload scratch and never escape to a sibling payload. */
#define RHO_OVERLAY_MAP_CAP 64
typedef struct {
    Space *orig[RHO_OVERLAY_MAP_CAP];
    Space *overlay[RHO_OVERLAY_MAP_CAP];
    int len;
} RhoOverlayMap;

#define RHO_STATE_CLONE_MAP_CAP 64
typedef struct {
    StateCell *orig[RHO_STATE_CLONE_MAP_CAP];
    StateCell *clone[RHO_STATE_CLONE_MAP_CAP];
    int len;
} RhoStateCloneMap;

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
    if (map->len >= RHO_OVERLAY_MAP_CAP)
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
    if (map->len >= RHO_OVERLAY_MAP_CAP)
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
    if (map->len >= RHO_STATE_CLONE_MAP_CAP)
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
    if (map->len >= RHO_STATE_CLONE_MAP_CAP)
        return false;
    map->orig[map->len] = orig;
    map->clone[map->len] = clone;
    map->len++;
    return true;
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
            if (src->entries[i].key == SYMBOL_ID_NONE || !src->entries[i].value) {
                continue;
            }
            value = rho_atom_rebind_resources(
                arena, src->entries[i].value, space_map, state_map);
            if (!value)
                return false;
            registry_bind_id(dst, src->entries[i].key, value);
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
        return atom_var_with_spelling(arena, atom->sym_id, atom->var_id);
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
        return atom_var_with_spelling(arena, atom->sym_id, atom->var_id);
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
    RhoOverlayMap map;
    RhoStateCloneMap state_map;
    RhoPromotedSpaceMap promoted_spaces = {0};
    RhoPromotedStateMap promoted_states = {0};
    Registry local_registry;
    Arena *persistent = NULL;
    CettaLibraryContext *prev_library_context;
    Space *self_overlay;
    uint64_t owner_epoch = 0;
    Atom *payload;
    bool alias_fault = false;

    if (!arena || !eval_context || !eval_context->space || !payload_expr || !out) {
        return false;
    }

    map.len = 0;
    state_map.len = 0;
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
    eval_payload_redirects_end();
    return true;

fail:
    rho_promoted_space_map_free(&promoted_spaces);
    rho_promoted_state_map_free(&promoted_states);
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

    if (!rhocalc_collect_successor_set(machine->arena, machine->current,
                                       machine->eval_context, &successors)) {
        return false;
    }
    *out_quiescent = successors.len == 0;
    *out_next = successors.len == 0 ? machine->current : successors.items[0];
    rhocalc_successor_set_free(&successors);
    return true;
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

typedef enum {
    RHOCOST_TERM_BAD = 0,
    RHOCOST_TERM_SIGNED,
    RHOCOST_TERM_PAR,
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
    Atom *result;
    Atom *consumed_sig;
} RhoCostStep;

typedef struct {
    RhoCostStep *items;
    char **keys;
    uint32_t len;
    uint32_t cap;
} RhoCostStepSetAcc;

typedef struct {
    RhoCostStep *items;
    uint32_t len;
} RhoCostStepSet;

typedef struct {
    RhoCostStep step;
    char *key;
    uint32_t ordinal;
} RhoCostKeyedStep;

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
    else if (strcmp(head, "rho:cost:stack-cons") == 0) view.kind = RHOCOST_TERM_STACK_CONS;
    return view;
}

static Atom *rhocost_stack_empty(Arena *arena) {
    return atom_symbol(arena, "rho:cost:stack-empty");
}

static Atom *rhocost_stack_cons(Arena *arena, Atom *sig, Atom *rest) {
    return rho_binary(arena, "rho:cost:stack-cons", sig, rest);
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

static void rhocost_step_set_acc_init(RhoCostStepSetAcc *acc) {
    acc->items = NULL;
    acc->keys = NULL;
    acc->len = 0;
    acc->cap = 0;
}

static void rhocost_step_set_acc_free(RhoCostStepSetAcc *acc) {
    if (!acc) return;
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
                                            char *key) {
    if (acc->len == acc->cap && !rhocost_step_set_acc_grow(acc)) {
        free(key);
        return false;
    }
    acc->items[acc->len].result = result;
    acc->items[acc->len].consumed_sig = consumed_sig;
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
}

static void rhocost_step_set_free(RhoCostStepSet *set) {
    if (!set) return;
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
    for (uint32_t i = 0; i < atoms.len; i++) {
        if (i > 0) (void)rho_str_append(&out, "*");
        (void)rho_str_append(&out, atom_name_cstr(atoms.items[i]));
    }
    rho_vec_free(&atoms);
    return out.data ? out.data : rho_heap_strdup("");
}

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
            "rhocalc cost first slice does not yet support dequotation");
        return false;
    case RHO_VAL:
        rho_validation_set("rhocalc cost first slice does not support rho:val");
        return false;
    case RHO_EVAL_PAYLOAD:
        rho_validation_set(
            "rhocalc cost first slice does not support rho:eval-payload");
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
    case RHOCOST_TERM_STACK_EMPTY:
        return true;
    case RHOCOST_TERM_STACK_CONS:
        if (view.nargs != 2) {
            rho_validation_set("rho:cost:stack-cons expects signature and rest");
            return false;
        }
        return rhocost_check_signature(view.args[0]) &&
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

static Atom *rhocost_normalize_term(Arena *arena, Atom *term);

static char *rhocost_key_term(Atom *term);

static char *rhocost_key_name(Atom *name) {
    if (!name) return rho_heap_strdup("?bad-name");
    if (name->kind == ATOM_VAR) {
        RhoStr out = {0};
        (void)rho_str_appendf(&out, "$%s:%llu", atom_name_cstr(name),
                              (unsigned long long)name->var_id);
        return out.data ? out.data : rho_heap_strdup("?bad-var");
    }
    {
        RhoView view = rho_view(name);
        if (view.kind == RHO_QUOTE && view.nargs == 1) {
            char *term_key = rhocost_key_term(view.args[0]);
            RhoStr out = {0};
            (void)rho_str_append(&out, "@(");
            (void)rho_str_append(&out, term_key);
            (void)rho_str_append(&out, ")");
            free(term_key);
            return out.data ? out.data : rho_heap_strdup("?bad-quote");
        }
    }
    if (rhocost_is_ground_signature(name) || rhocost_is_sig_mul(name)) {
        return rhocost_key_signature(name);
    }
    return rho_heap_strdup("?bad-name");
}

static char *rhocost_key_proc(Atom *proc) {
    RhoView view = rho_view(proc);
    RhoStr out = {0};
    switch (view.kind) {
    case RHO_NIL:
        (void)rho_str_append(&out, "0");
        break;
    case RHO_PAR:
        (void)rho_str_append(&out, "par(");
        for (uint32_t i = 0; i < view.nargs; i++) {
            char *sub = rhocost_key_proc(view.args[i]);
            if (i > 0) (void)rho_str_append(&out, "|");
            (void)rho_str_append(&out, sub);
            free(sub);
        }
        (void)rho_str_append(&out, ")");
        break;
    case RHO_SEND: {
        char *name_key = rhocost_key_name(view.args[0]);
        char *term_key = rhocost_key_term(view.args[1]);
        (void)rho_str_appendf(&out, "send(%s,%s)", name_key, term_key);
        free(name_key);
        free(term_key);
        break;
    }
    case RHO_RECV: {
        char *name_key = rhocost_key_name(view.args[0]);
        char *term_key = rhocost_key_term(view.args[2]);
        (void)rho_str_appendf(&out, "recv(%s,$%s:%llu,%s)",
                              name_key,
                              atom_name_cstr(view.args[1]),
                              (unsigned long long)view.args[1]->var_id,
                              term_key);
        free(name_key);
        free(term_key);
        break;
    }
    case RHO_DROP: {
        char *name_key = rhocost_key_name(view.args[0]);
        (void)rho_str_appendf(&out, "drop(%s)", name_key);
        free(name_key);
        break;
    }
    case RHO_VAL:
    case RHO_EVAL_PAYLOAD:
    case RHO_QUOTE:
    case RHO_BAD:
        (void)rho_str_append(&out, "?bad-proc");
        break;
    }
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
            return rho_unary(arena, "rho:quote",
                             rhocost_normalize_term(arena, view.args[0]));
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
    if (vec->len == 0) return rhocost_stack_empty(arena);
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

static Atom *rhocost_normalize_term(Arena *arena, Atom *term) {
    RhoCostTermView view = rhocost_term_view(term);
    switch (view.kind) {
    case RHOCOST_TERM_STACK_EMPTY:
        return rhocost_stack_empty(arena);
    case RHOCOST_TERM_STACK_CONS:
        return rhocost_stack_cons(arena,
                                  rhocost_normalize_signature(arena, view.args[0]),
                                  rhocost_normalize_term(arena, view.args[1]));
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
    if (view.kind == RHOCOST_TERM_STACK_EMPTY) return;
    if (view.kind == RHOCOST_TERM_PAR) {
        for (uint32_t i = 0; i < view.nargs; i++) {
            rhocost_collect_term_par(arena, view.args[i], out);
        }
        return;
    }
    (void)rho_vec_push(out, norm);
}

static char *rhocost_key_term(Atom *term) {
    RhoCostTermView view = rhocost_term_view(term);
    RhoStr out = {0};
    switch (view.kind) {
    case RHOCOST_TERM_STACK_EMPTY:
        (void)rho_str_append(&out, "()");
        break;
    case RHOCOST_TERM_STACK_CONS: {
        char *sig = rhocost_key_signature(view.args[0]);
        char *rest = rhocost_key_term(view.args[1]);
        (void)rho_str_appendf(&out, "stack(%s:%s)", sig, rest);
        free(sig);
        free(rest);
        break;
    }
    case RHOCOST_TERM_SIGNED: {
        char *body = rhocost_key_proc(view.args[0]);
        char *sig = rhocost_key_signature(view.args[1]);
        (void)rho_str_appendf(&out, "signed(%s,%s)", body, sig);
        free(body);
        free(sig);
        break;
    }
    case RHOCOST_TERM_PAR:
        (void)rho_str_append(&out, "tpar(");
        for (uint32_t i = 0; i < view.nargs; i++) {
            char *sub = rhocost_key_term(view.args[i]);
            if (i > 0) (void)rho_str_append(&out, "|");
            (void)rho_str_append(&out, sub);
            free(sub);
        }
        (void)rho_str_append(&out, ")");
        break;
    case RHOCOST_TERM_BAD:
        (void)rho_str_append(&out, "?bad-term");
        break;
    }
    return out.data ? out.data : rho_heap_strdup("");
}

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
        return view.nargs == 1 && rhocost_name_has_free_var(view.args[0], var_id);
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
    case RHOCOST_TERM_STACK_EMPTY:
    case RHOCOST_TERM_STACK_CONS:
        return false;
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
    case RHOCOST_TERM_STACK_EMPTY:
        return rhocost_stack_empty(arena);
    case RHOCOST_TERM_STACK_CONS:
        return rhocost_stack_cons(arena, view.args[0], view.args[1]);
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
    case RHOCOST_TERM_STACK_EMPTY:
        return rhocost_stack_empty(arena);
    case RHOCOST_TERM_STACK_CONS:
        return rhocost_stack_cons(arena, view.args[0], view.args[1]);
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
        extras[i + 1] = tokens->items[chosen_tokens->items[i]].rest;
    }

    next = rhocost_rebuild_result(arena, components, skip, skip_len,
                                  extras, extra_len);
    key = rhocost_step_transition_key(next, consumed_sig);
    return rhocost_step_set_acc_push_keyed(out, next, consumed_sig, key);
}

static bool rhocost_cover_tokens_rec(Arena *arena,
                                     RhoAtomVec *components,
                                     const uint32_t *participant_skip,
                                     uint32_t participant_skip_len,
                                     Atom *body,
                                     RhoCostTokenVec *tokens,
                                     RhoAtomVec *remaining_sig_atoms,
                                     uint32_t start,
                                     RhoIndexVec *chosen_tokens,
                                     Atom *consumed_sig,
                                     RhoCostStepSetAcc *out) {
    if (remaining_sig_atoms->len == 0) {
        return rhocost_emit_step(arena, components, participant_skip,
                                 participant_skip_len, body, tokens,
                                 chosen_tokens, consumed_sig, out);
    }

    for (uint32_t i = start; i < tokens->len; i++) {
        RhoAtomVec token_atoms;
        RhoAtomVec next_remaining;
        bool ok;

        rho_vec_init(&token_atoms);
        if (!rhocost_collect_signature_atoms(tokens->items[i].sig, &token_atoms)) {
            rho_vec_free(&token_atoms);
            return false;
        }
        rhocost_normalize_signature_vec(&token_atoms);
        if (!rhocost_signature_vec_is_subset(&token_atoms, remaining_sig_atoms)) {
            rho_vec_free(&token_atoms);
            continue;
        }
        ok = rhocost_signature_vec_subtract(remaining_sig_atoms, &token_atoms,
                                            &next_remaining);
        rho_vec_free(&token_atoms);
        if (!ok) return false;
        if (!rhocost_index_vec_push(chosen_tokens, i)) {
            rho_vec_free(&next_remaining);
            return false;
        }
        ok = rhocost_cover_tokens_rec(arena, components, participant_skip,
                                      participant_skip_len, body, tokens,
                                      &next_remaining, i + 1, chosen_tokens,
                                      consumed_sig, out);
        chosen_tokens->len--;
        rho_vec_free(&next_remaining);
        if (!ok) return false;
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

static bool rhocost_collect_steps(Arena *arena, Atom *term,
                                  RhoCostStepSetAcc *out) {
    RhoAtomVec components;
    RhoCostTokenVec tokens;
    RhoCostSignedEndpointVec recvs;
    RhoCostSignedEndpointVec sends;
    RhoCostWholeRedexVec wholes;
    bool ok = true;

    if (!arena || !term || !out) return false;
    if (!rhocost_term_well_formed(term)) return false;

    rho_vec_init(&components);
    rhocost_token_vec_init(&tokens);
    rhocost_signed_endpoint_vec_init(&recvs);
    rhocost_signed_endpoint_vec_init(&sends);
    rhocost_whole_redex_vec_init(&wholes);

    rhocost_collect_term_components(arena, rhocost_normalize_term(arena, term),
                                    &components);

    for (uint32_t i = 0; i < components.len && ok; i++) {
        RhoCostTermView view = rhocost_term_view(components.items[i]);
        if (view.kind == RHOCOST_TERM_STACK_CONS && view.nargs == 2 &&
            rhocost_check_signature(view.args[0])) {
            ok = rhocost_token_vec_push(&tokens, i, view.args[0], view.args[1]);
            continue;
        }
        ok = rhocost_signed_body_endpoint(i, components.items[i], &recvs, &sends) &&
             rhocost_whole_signed_redex(i, components.items[i], &wholes);
    }

    for (uint32_t w = 0; w < wholes.len && ok; w++) {
        RhoAtomVec required_sig_atoms;
        RhoIndexVec chosen_tokens;
        uint32_t skip[1] = {wholes.items[w].component_index};
        Atom *consumed_sig = rhocost_normalize_signature(arena, wholes.items[w].sig);
        Atom *body = rhocost_subst_term(arena, wholes.items[w].recv.args[2],
                                        wholes.items[w].recv.args[1]->var_id,
                                        wholes.items[w].send.args[1]);
        rho_vec_init(&required_sig_atoms);
        rhocost_index_vec_init(&chosen_tokens);
        ok = rhocost_collect_signature_atoms(consumed_sig, &required_sig_atoms);
        if (ok) {
            rhocost_normalize_signature_vec(&required_sig_atoms);
            ok = rhocost_cover_tokens_rec(arena, &components, skip, 1, body,
                                          &tokens, &required_sig_atoms, 0,
                                          &chosen_tokens, consumed_sig, out);
        }
        rhocost_index_vec_free(&chosen_tokens);
        rho_vec_free(&required_sig_atoms);
    }

    for (uint32_t r = 0; r < recvs.len && ok; r++) {
        for (uint32_t s = 0; s < sends.len && ok; s++) {
            RhoAtomVec required_sig_atoms;
            RhoIndexVec chosen_tokens;
            uint32_t skip[2];
            Atom *consumed_sig;
            Atom *body;
            if (strcmp(recvs.items[r].channel_key, sends.items[s].channel_key) != 0) {
                continue;
            }
            consumed_sig = rhocost_signature_product(arena, recvs.items[r].sig,
                                                     sends.items[s].sig);
            if (!consumed_sig) {
                ok = false;
                continue;
            }
            body = rhocost_subst_term(arena, recvs.items[r].view.args[2],
                                      recvs.items[r].view.args[1]->var_id,
                                      sends.items[s].view.args[1]);
            skip[0] = recvs.items[r].component_index;
            skip[1] = sends.items[s].component_index;
            rho_vec_init(&required_sig_atoms);
            rhocost_index_vec_init(&chosen_tokens);
            ok = rhocost_collect_signature_atoms(consumed_sig, &required_sig_atoms);
            if (ok) {
                rhocost_normalize_signature_vec(&required_sig_atoms);
                ok = rhocost_cover_tokens_rec(arena, &components, skip, 2, body,
                                              &tokens, &required_sig_atoms, 0,
                                              &chosen_tokens, consumed_sig, out);
            }
            rhocost_index_vec_free(&chosen_tokens);
            rho_vec_free(&required_sig_atoms);
        }
    }

    rhocost_whole_redex_vec_free(&wholes);
    rhocost_signed_endpoint_vec_free(&recvs);
    rhocost_signed_endpoint_vec_free(&sends);
    rhocost_token_vec_free(&tokens);
    rho_vec_free(&components);
    return ok;
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
        rho_validation_set("rho threaded execution is strict-core only");
        return false;
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
