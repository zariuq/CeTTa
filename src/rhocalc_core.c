#include "rhocalc_core.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Atom **items;
    uint32_t len;
} RhoSuccessorSet;

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

bool rhocalc_reduce_to_quiescence(Arena *arena, Atom *proc,
                                  uint32_t reduction_limit, RhoReductionResult *out) {
    RhoRuntimeProfile profile = rho_runtime_profile_default(reduction_limit);
    return rhocalc_reduce_to_quiescence_with_profile(arena, proc,
                                                     &profile, out);
}
