#include "rhocalc_core.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    RHO_BAD = 0,
    RHO_NIL,
    RHO_PAR,
    RHO_SEND,
    RHO_RECV,
    RHO_QUOTE,
    RHO_DROP
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

static __thread char g_rhocalc_validation_error[256];

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
    return view;
}

static Atom *rho_call(Arena *arena, const char *head,
                      Atom *const *args, uint32_t nargs) {
    Atom **elems = arena_alloc(arena, sizeof(Atom *) * (size_t)(nargs + 1));
    elems[0] = atom_symbol(arena, head);
    for (uint32_t i = 0; i < nargs; i++) {
        elems[i + 1] = args[i];
    }
    return atom_expr(arena, elems, nargs + 1);
}

static Atom *rho_nil(Arena *arena) {
    return atom_symbol(arena, "rho:nil");
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

static bool rho_check_proc(Atom *proc, RhoScope *scope);

static bool rho_check_name(Atom *name, RhoScope *scope) {
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
        ok = rho_check_proc(view.args[0], &quote_scope);
        rho_scope_free(&quote_scope);
        return ok;
    }
    rho_validation_set("rho name must be a quoted process or bound variable");
    return false;
}

static bool rho_check_proc(Atom *proc, RhoScope *scope) {
    RhoView view = rho_view(proc);
    switch (view.kind) {
    case RHO_NIL:
        return true;
    case RHO_PAR:
        for (uint32_t i = 0; i < view.nargs; i++) {
            if (!rho_check_proc(view.args[i], scope)) return false;
        }
        return true;
    case RHO_SEND:
        if (view.nargs != 2) {
            rho_validation_set("rho:send expects channel and process payload");
            return false;
        }
        return rho_check_name(view.args[0], scope) &&
               rho_check_proc(view.args[1], scope);
    case RHO_RECV: {
        if (view.nargs != 3) {
            rho_validation_set("rho:recv expects channel, binder, and body");
            return false;
        }
        if (view.args[1]->kind != ATOM_VAR) {
            rho_validation_set("rho:recv binder must be a variable");
            return false;
        }
        if (!rho_check_name(view.args[0], scope)) return false;
        uint32_t mark = rho_scope_mark(scope);
        if (!rho_scope_push(scope, view.args[1]->var_id)) return false;
        bool ok = rho_check_proc(view.args[2], scope);
        rho_scope_pop(scope, mark);
        return ok;
    }
    case RHO_DROP:
        if (view.nargs != 1) {
            rho_validation_set("rho:drop expects one name");
            return false;
        }
        return rho_check_name(view.args[0], scope);
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
    ok = rho_check_proc(proc, &scope);
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

static Atom *rho_normalize_name(Arena *arena, Atom *name) {
    if (name->kind == ATOM_VAR) return name;
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
                           view.args[1],
                           rho_normalize_proc(arena, view.args[2]));
    case RHO_DROP:
        return rho_unary(arena, "rho:drop",
                         rho_normalize_name(arena, view.args[0]));
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
    case RHO_QUOTE:
    case RHO_BAD:
        break;
    }
    return false;
}

static Atom *rho_fresh_var_like(Arena *arena, Atom *var) {
    VarId id = fresh_var_id();
    const char *base = atom_name_cstr(var);
    char suffix[64];
    size_t base_len;
    size_t suffix_len;
    char *name;
    snprintf(suffix, sizeof(suffix), "_rho%llu", (unsigned long long)id);
    base_len = strlen(base);
    suffix_len = strlen(suffix);
    name = arena_alloc(arena, base_len + suffix_len + 1u);
    memcpy(name, base, base_len);
    memcpy(name + base_len, suffix, suffix_len + 1u);
    return atom_var_with_id(arena, name, id);
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
                               view.args[1],
                               view.args[2]);
        }
        return rho_normalize_proc(arena,
            rho_ternary(arena, "rho:recv",
                        rho_rename_name(arena, view.args[0], old_id,
                                        replacement_name),
                        view.args[1],
                        rho_rename_proc(arena, view.args[2], old_id,
                                        replacement_name)));
    case RHO_DROP:
        return rho_unary(arena, "rho:drop",
                         rho_rename_name(arena, view.args[0], old_id,
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
                               view.args[1],
                               view.args[2]);
        }
        {
            Atom *binder = view.args[1];
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

static bool rho_compute_comm_result(Arena *arena,
                                    RhoAtomVec *components,
                                    const RhoEndpoint *send_endpoint,
                                    const RhoEndpoint *recv_endpoint,
                                    Atom **out_result,
                                    char **out_key) {
    RhoView send = send_endpoint->view;
    RhoView recv = recv_endpoint->view;
    Atom *replacement;
    Atom *body;
    Atom *result;

    if (!arena || !components || !send_endpoint || !recv_endpoint ||
        !out_result || !out_key ||
        send.kind != RHO_SEND || send.nargs != 2 ||
        recv.kind != RHO_RECV || recv.nargs != 3 ||
        recv.args[1]->kind != ATOM_VAR) {
        return false;
    }

    replacement =
        rho_unary(arena, "rho:quote", rho_normalize_proc(arena, send.args[1]));
    body = rho_subst_proc(arena, recv.args[2],
                          recv.args[1]->var_id,
                          replacement);
    result = rho_rebuild_reaction(arena, components,
                                  send_endpoint->component_index,
                                  recv_endpoint->component_index,
                                  body);
    if (!result) return false;

    *out_result = result;
    *out_key = rho_key_proc(result);
    return *out_key != NULL;
}

static RhoRuntimeProfile rho_runtime_profile_default(uint32_t reduction_limit) {
    RhoRuntimeProfile profile;
    profile.scheduler_policy = RHO_SCHEDULER_CANONICAL;
    profile.reduction_limit = reduction_limit;
    return profile;
}

static bool rho_runtime_profile_supported(const RhoRuntimeProfile *profile) {
    if (!profile || profile->reduction_limit == 0u) return false;
    if (profile->scheduler_policy != RHO_SCHEDULER_CANONICAL &&
        profile->scheduler_policy != RHO_SCHEDULER_ROTATING) {
        rho_validation_set("rho scheduler policy is not implemented");
        return false;
    }
    return true;
}

static void rho_machine_init(RhoMachine *machine, Arena *arena,
                             const RhoRuntimeProfile *profile) {
    machine->arena = arena;
    machine->profile = *profile;
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
    machine->current = NULL;
}

static bool rho_machine_load_process(RhoMachine *machine, Atom *proc) {
    if (!rhocalc_process_well_formed(proc)) return false;
    machine->current = rho_normalize_proc(machine->arena, proc);
    return true;
}

static bool rho_machine_refresh_comm_index(RhoMachine *machine) {
    rho_machine_clear_comm_index(machine);
    rho_collect_par(machine->arena, machine->current, &machine->components);
    if (!rho_collect_endpoints(&machine->components,
                               &machine->sends,
                               &machine->recvs)) {
        return false;
    }
    machine->comm_index_loaded = true;
    return true;
}

static bool rho_machine_select_canonical_successor(RhoMachine *machine,
                                                   Atom **out_next,
                                                   bool *out_quiescent) {
    uint32_t send_pos = 0;
    uint32_t recv_pos = 0;
    Atom *best_result = NULL;
    char *best_key = NULL;

    if (!machine || !machine->arena || !machine->current ||
        !out_next || !out_quiescent) {
        return false;
    }
    *out_next = NULL;
    *out_quiescent = true;

    if (!rho_machine_refresh_comm_index(machine)) return false;

    while (recv_pos < machine->recvs.len &&
           send_pos < machine->sends.len) {
        int cmp = strcmp(machine->recvs.items[recv_pos].key,
                         machine->sends.items[send_pos].key);
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
        while (recv_pos < machine->recvs.len &&
               strcmp(machine->recvs.items[recv_pos].key,
                      machine->recvs.items[recv_start].key) == 0) {
            recv_pos++;
        }
        while (send_pos < machine->sends.len &&
               strcmp(machine->sends.items[send_pos].key,
                      machine->sends.items[send_start].key) == 0) {
            send_pos++;
        }

        for (uint32_t r = recv_start; r < recv_pos; r++) {
            for (uint32_t s = send_start; s < send_pos; s++) {
                Atom *next;
                char *key;
                if (!rho_compute_comm_result(machine->arena,
                                             &machine->components,
                                             &machine->sends.items[s],
                                             &machine->recvs.items[r],
                                             &next, &key)) {
                    free(best_key);
                    return false;
                }
                if (!best_key || strcmp(key, best_key) < 0) {
                    free(best_key);
                    best_key = key;
                    best_result = next;
                } else {
                    free(key);
                }
            }
        }
    }

    *out_quiescent = best_result == NULL;
    *out_next = best_result ? best_result : machine->current;
    free(best_key);
    return true;
}

static bool rho_collect_successors(Arena *arena, Atom *proc, RhoSuccessorSetAcc *out);

static void rhocalc_successor_set_free(RhoSuccessorSet *set) {
    if (!set) return;
    free(set->items);
    set->items = NULL;
    set->len = 0;
}

static bool rhocalc_collect_successor_set(Arena *arena, Atom *proc, RhoSuccessorSet *out) {
    RhoSuccessorSetAcc acc;

    if (!arena || !proc || !out) return false;
    out->items = NULL;
    out->len = 0;

    rho_successor_set_acc_init(&acc);
    if (!rho_collect_successors(arena, proc, &acc)) {
        rho_successor_set_acc_free(&acc);
        return false;
    }
    rho_successor_set_acc_finish(&acc, out);
    return true;
}

Atom *rhocalc_successor_frontier_expr(Arena *arena, Atom *proc) {
    RhoSuccessorSet successors = {0};
    Atom *list;
    Atom *result;

    if (!rhocalc_collect_successor_set(arena, proc, &successors)) {
        return NULL;
    }
    list = atom_expr(arena, successors.items, successors.len);
    result = atom_expr2(arena, atom_symbol(arena, "superpose"), list);
    rhocalc_successor_set_free(&successors);
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
    if (!rhocalc_semantic_profile_runtime_supported(semantic_profile)) {
        return NULL;
    }
    if (semantic_profile == RHOCALC_SEMANTIC_PROFILE_COST) {
        return rhocost_successor_frontier_expr(arena, proc);
    }
    return rhocalc_successor_frontier_expr(arena, proc);
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
    if (!rho_collect_successors(machine->arena, machine->current, &successor_acc)) {
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

static bool rho_collect_successors(Arena *arena, Atom *proc, RhoSuccessorSetAcc *out) {
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
                Atom *next;
                char *key;
                if (!rho_compute_comm_result(arena, &components,
                                             &sends.items[s],
                                             &recvs.items[r],
                                             &next, &key) ||
                    !rho_successor_set_acc_push_keyed(out, next, key)) {
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
    RhoMachine machine;

    if (!arena || !proc || !profile || !out) return false;
    out->residual = NULL;
    out->reductions_taken = 0;
    out->status = RHOCALC_REDUCTION_QUIESCENT;

    if (!rho_runtime_profile_supported(profile)) {
        return false;
    }

    rho_machine_init(&machine, arena, profile);
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
    if (!rhocalc_semantic_profile_runtime_supported(semantic_profile)) {
        return false;
    }
    if (semantic_profile == RHOCALC_SEMANTIC_PROFILE_COST) {
        return rhocost_reduce_to_quiescence_with_profile(arena, proc, profile, out);
    }
    return rhocalc_reduce_to_quiescence_with_profile(arena, proc, profile, out);
}

bool rhocalc_reduce_to_quiescence(Arena *arena, Atom *proc,
                                  uint32_t reduction_limit, RhoReductionResult *out) {
    RhoRuntimeProfile profile = rho_runtime_profile_default(reduction_limit);
    return rhocalc_reduce_to_quiescence_with_profile(arena, proc,
                                                     &profile, out);
}
