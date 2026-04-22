#include "lang_adapter.h"

#include "grounded.h"
#include "petta_contract.h"
#include "parser.h"
#include "space.h"
#include "symbol.h"
#include "text_source.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    Arena *arena;
    uint64_t fresh_counter;
    struct CettaPettaCallableInventory *callables;
    SymbolId suppressed_recursive_value_head;
    uint32_t suppressed_recursive_value_arity;
} CettaPettaLowerContext;

typedef enum {
    PETTA_GUARDED_PATTERN_NONE = 0,
    PETTA_GUARDED_PATTERN_CONS,
    PETTA_GUARDED_PATTERN_CALLABLE,
} PettaGuardedPatternKind;

typedef struct PettaGuardedPattern {
    PettaGuardedPatternKind kind;
    Atom *skeleton;
    Atom *guard_term;
    struct PettaGuardedPattern *children;
    uint32_t child_count;
} PettaGuardedPattern;

typedef struct {
    SymbolId head_id;
    SymbolId relation_head_id;
    uint32_t arity;
    bool imported;
    bool needs_relation_lowering;
    bool lhs_needs_relation_lowering;
    bool goal_relation_lowering;
    uint32_t syntax_arg_mask;
} CettaPettaCallableInfo;

typedef enum {
    PETTA_BUILTIN_REL_NONE = 0,
    PETTA_BUILTIN_REL_APPEND,
    PETTA_BUILTIN_REL_IS_MEMBER,
    PETTA_BUILTIN_REL_MEMBER,
    PETTA_BUILTIN_REL_SIZE_ATOM,
    PETTA_BUILTIN_REL_HASH_PLUS,
    PETTA_BUILTIN_REL_HASH_EQ,
    PETTA_BUILTIN_REL_HASH_LT,
    PETTA_BUILTIN_REL_HASH_GT,
} PettaBuiltinRelationKind;

typedef enum {
    PETTA_ARG_VALUE = 0,
    PETTA_ARG_PATTERN,
    PETTA_ARG_GOAL,
    PETTA_ARG_DECL_HEAD,
    PETTA_ARG_DECL_BODY,
    PETTA_ARG_TEMPLATE,
    PETTA_ARG_MUTATION_PAYLOAD,
    PETTA_ARG_CASE_PAIRS,
} PettaArgMode;

typedef enum {
    PETTA_HEAD_SPECIAL_NONE = 0,
    PETTA_HEAD_SPECIAL_IF,
    PETTA_HEAD_SPECIAL_LET,
    PETTA_HEAD_SPECIAL_CASE,
    PETTA_HEAD_SPECIAL_ONLY,
    PETTA_HEAD_SPECIAL_ONCE,
    PETTA_HEAD_SPECIAL_MATCH,
    PETTA_HEAD_SPECIAL_ADD_ATOM,
    PETTA_HEAD_SPECIAL_ADD_ATOM_NODUP,
    PETTA_HEAD_SPECIAL_ADD_REDUCT,
    PETTA_HEAD_SPECIAL_REMOVE_ATOM,
} PettaHeadSpecial;

#define PETTA_HEAD_PROFILE_MAX_ARGS 4

typedef struct {
    const char *head_name;
    uint32_t arity;
    PettaArgMode arg_modes[PETTA_HEAD_PROFILE_MAX_ARGS];
    PettaBuiltinRelationKind builtin_relation;
    PettaHeadSpecial special;
    bool core_callable;
    bool relation_goal_form;
} PettaHeadProfile;

static const PettaHeadProfile PETTA_HEAD_PROFILES[] = {
    { "=", 2, { PETTA_ARG_DECL_HEAD, PETTA_ARG_DECL_BODY },
      PETTA_BUILTIN_REL_NONE, PETTA_HEAD_SPECIAL_NONE, false, true },
    { "and", 2, { PETTA_ARG_GOAL, PETTA_ARG_GOAL },
      PETTA_BUILTIN_REL_NONE, PETTA_HEAD_SPECIAL_NONE, false, true },
    { "assert", 1, { PETTA_ARG_GOAL },
      PETTA_BUILTIN_REL_NONE, PETTA_HEAD_SPECIAL_NONE, false, true },
    { "if", 2, { PETTA_ARG_GOAL, PETTA_ARG_VALUE },
      PETTA_BUILTIN_REL_NONE, PETTA_HEAD_SPECIAL_IF, false, false },
    { "if", 3, { PETTA_ARG_GOAL, PETTA_ARG_VALUE, PETTA_ARG_VALUE },
      PETTA_BUILTIN_REL_NONE, PETTA_HEAD_SPECIAL_IF, false, false },
    { "let", 3, { PETTA_ARG_PATTERN, PETTA_ARG_VALUE, PETTA_ARG_VALUE },
      PETTA_BUILTIN_REL_NONE, PETTA_HEAD_SPECIAL_LET, false, false },
    { "match", 3, { PETTA_ARG_TEMPLATE, PETTA_ARG_PATTERN, PETTA_ARG_TEMPLATE },
      PETTA_BUILTIN_REL_NONE, PETTA_HEAD_SPECIAL_MATCH, false, false },
    { "case", 2, { PETTA_ARG_VALUE, PETTA_ARG_CASE_PAIRS },
      PETTA_BUILTIN_REL_NONE, PETTA_HEAD_SPECIAL_CASE, false, false },
    { "only", 2, { PETTA_ARG_GOAL, PETTA_ARG_VALUE },
      PETTA_BUILTIN_REL_NONE, PETTA_HEAD_SPECIAL_ONLY, false, false },
    { "once", 1, { PETTA_ARG_VALUE },
      PETTA_BUILTIN_REL_NONE, PETTA_HEAD_SPECIAL_ONCE, false, true },
    { "add-atom", 2, { PETTA_ARG_VALUE, PETTA_ARG_MUTATION_PAYLOAD },
      PETTA_BUILTIN_REL_NONE, PETTA_HEAD_SPECIAL_ADD_ATOM, false, false },
    { "add-atom-nodup", 2, { PETTA_ARG_VALUE, PETTA_ARG_MUTATION_PAYLOAD },
      PETTA_BUILTIN_REL_NONE, PETTA_HEAD_SPECIAL_ADD_ATOM_NODUP, false, false },
    { "add-reduct", 2, { PETTA_ARG_VALUE, PETTA_ARG_MUTATION_PAYLOAD },
      PETTA_BUILTIN_REL_NONE, PETTA_HEAD_SPECIAL_ADD_REDUCT, false, false },
    { "remove-atom", 2, { PETTA_ARG_VALUE, PETTA_ARG_MUTATION_PAYLOAD },
      PETTA_BUILTIN_REL_NONE, PETTA_HEAD_SPECIAL_REMOVE_ATOM, false, false },
    { "empty", 0, { PETTA_ARG_VALUE },
      PETTA_BUILTIN_REL_NONE, PETTA_HEAD_SPECIAL_NONE, true, false },
    { "append", 2, { PETTA_ARG_VALUE, PETTA_ARG_VALUE },
      PETTA_BUILTIN_REL_APPEND, PETTA_HEAD_SPECIAL_NONE, false, false },
    { "union-atom", 2, { PETTA_ARG_VALUE, PETTA_ARG_VALUE },
      PETTA_BUILTIN_REL_APPEND, PETTA_HEAD_SPECIAL_NONE, false, false },
    { "is-member", 2, { PETTA_ARG_VALUE, PETTA_ARG_VALUE },
      PETTA_BUILTIN_REL_IS_MEMBER, PETTA_HEAD_SPECIAL_NONE, false, false },
    { "member", 2, { PETTA_ARG_VALUE, PETTA_ARG_VALUE },
      PETTA_BUILTIN_REL_MEMBER, PETTA_HEAD_SPECIAL_NONE, false, false },
    { "size-atom", 1, { PETTA_ARG_VALUE },
      PETTA_BUILTIN_REL_SIZE_ATOM, PETTA_HEAD_SPECIAL_NONE, false, false },
    { "==", 2, { PETTA_ARG_VALUE, PETTA_ARG_VALUE },
      PETTA_BUILTIN_REL_NONE, PETTA_HEAD_SPECIAL_NONE, false, true },
    { "=alpha", 2, { PETTA_ARG_VALUE, PETTA_ARG_VALUE },
      PETTA_BUILTIN_REL_NONE, PETTA_HEAD_SPECIAL_NONE, false, true },
    { ">", 2, { PETTA_ARG_VALUE, PETTA_ARG_VALUE },
      PETTA_BUILTIN_REL_NONE, PETTA_HEAD_SPECIAL_NONE, false, true },
    { "#+", 2, { PETTA_ARG_VALUE, PETTA_ARG_VALUE },
      PETTA_BUILTIN_REL_HASH_PLUS, PETTA_HEAD_SPECIAL_NONE, false, false },
    { "#=", 2, { PETTA_ARG_VALUE, PETTA_ARG_VALUE },
      PETTA_BUILTIN_REL_HASH_EQ, PETTA_HEAD_SPECIAL_NONE, false, true },
    { "#<", 2, { PETTA_ARG_VALUE, PETTA_ARG_VALUE },
      PETTA_BUILTIN_REL_HASH_LT, PETTA_HEAD_SPECIAL_NONE, false, true },
    { "#>", 2, { PETTA_ARG_VALUE, PETTA_ARG_VALUE },
      PETTA_BUILTIN_REL_HASH_GT, PETTA_HEAD_SPECIAL_NONE, false, true },
};

typedef struct CettaPettaCallableInventory {
    CettaPettaCallableInfo *items;
    uint32_t len;
    uint32_t cap;
} CettaPettaCallableInventory;

typedef struct {
    char **items;
    uint32_t len;
    uint32_t cap;
} CettaPettaPathSet;

typedef struct {
    VarId *items;
    uint32_t len;
    uint32_t cap;
} PettaBoundVarSet;

static bool atom_is_symbol_name(Atom *atom, const char *name) {
    const char *spelling = atom_name_cstr(atom);
    return spelling && strcmp(spelling, name) == 0;
}

static bool expr_head_is_name(Atom *atom, const char *name) {
    return atom &&
           atom->kind == ATOM_EXPR &&
           atom->expr.len > 0 &&
           atom_is_symbol_name(atom->expr.elems[0], name);
}

static const PettaHeadProfile *petta_head_profile(Atom *head, uint32_t arity) {
    const char *name = atom_name_cstr(head);
    if (!name)
        return NULL;
    for (uint32_t i = 0;
         i < sizeof(PETTA_HEAD_PROFILES) / sizeof(PETTA_HEAD_PROFILES[0]);
         i++) {
        if (PETTA_HEAD_PROFILES[i].arity == arity &&
            strcmp(PETTA_HEAD_PROFILES[i].head_name, name) == 0) {
            return &PETTA_HEAD_PROFILES[i];
        }
    }
    return NULL;
}

static const PettaHeadProfile *petta_expr_head_profile(Atom *expr) {
    if (!expr || expr->kind != ATOM_EXPR || expr->expr.len == 0)
        return NULL;
    return petta_head_profile(expr->expr.elems[0], expr->expr.len - 1);
}

static bool petta_profile_is_declaration(const PettaHeadProfile *profile) {
    return profile &&
           profile->arity == 2 &&
           profile->arg_modes[0] == PETTA_ARG_DECL_HEAD &&
           profile->arg_modes[1] == PETTA_ARG_DECL_BODY;
}

static bool petta_expr_is_declaration_form(Atom *expr) {
    return petta_profile_is_declaration(petta_expr_head_profile(expr));
}

static bool petta_bound_var_set_contains(const PettaBoundVarSet *set, VarId var_id) {
    if (!set || var_id == VAR_ID_NONE)
        return false;
    for (uint32_t i = 0; i < set->len; i++) {
        if (set->items[i] == var_id)
            return true;
    }
    return false;
}

static bool petta_bound_var_set_push(PettaBoundVarSet *set, VarId var_id) {
    if (!set || var_id == VAR_ID_NONE || petta_bound_var_set_contains(set, var_id))
        return true;
    if (set->len == set->cap) {
        uint32_t new_cap = set->cap ? set->cap * 2 : 8;
        VarId *new_items = cetta_realloc(set->items, sizeof(VarId) * new_cap);
        if (!new_items)
            return false;
        set->items = new_items;
        set->cap = new_cap;
    }
    set->items[set->len++] = var_id;
    return true;
}

static void petta_bound_var_set_free(PettaBoundVarSet *set) {
    if (!set) return;
    free(set->items);
    set->items = NULL;
    set->len = 0;
    set->cap = 0;
}

static bool petta_atom_is_true_literal(Atom *atom) {
    return atom &&
           (atom_is_symbol_name(atom, "True") ||
            atom_is_symbol_id(atom, g_builtin_syms.true_text) ||
            (atom->kind == ATOM_GROUNDED &&
             atom->ground.gkind == GV_BOOL &&
             atom->ground.bval));
}

static char *petta_owned_strdup(const char *text) {
    size_t len;
    char *copy;
    if (!text) return NULL;
    len = strlen(text);
    copy = cetta_malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, text, len + 1);
    return copy;
}

static Atom *petta_rewrite_empty_literals(Arena *arena, Atom *term) {
    if (!term) return term;
    if (term->kind == ATOM_SYMBOL) {
        if (atom_is_symbol_name(term, "Empty")) {
            return atom_symbol_id(arena, g_builtin_syms.petta_empty_literal);
        }
        return term;
    }
    if (term->kind != ATOM_EXPR || term->expr.len == 0)
        return term;

    Atom **rewritten = NULL;
    for (uint32_t i = 0; i < term->expr.len; i++) {
        Atom *child = term->expr.elems[i];
        Atom *next = petta_rewrite_empty_literals(arena, child);
        if (!rewritten && next != child) {
            rewritten = arena_alloc(arena, sizeof(Atom *) * term->expr.len);
            for (uint32_t j = 0; j < i; j++) {
                rewritten[j] = term->expr.elems[j];
            }
        }
        if (rewritten) {
            rewritten[i] = next;
        }
    }
    if (!rewritten)
        return term;
    return atom_expr(arena, rewritten, term->expr.len);
}

static Atom *petta_fresh_var(CettaPettaLowerContext *ctx, const char *prefix) {
    char buf[96];
    snprintf(buf, sizeof(buf), "$__petta_%s_%" PRIu64, prefix, ++ctx->fresh_counter);
    return atom_var(ctx->arena, buf);
}

static Atom *make_expr_with_head(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    Atom **elems = arena_alloc(a, sizeof(Atom *) * (nargs + 1));
    elems[0] = head;
    for (uint32_t i = 0; i < nargs; i++) elems[i + 1] = args[i];
    return atom_expr(a, elems, nargs + 1);
}

static Atom *petta_wrap_let(CettaPettaLowerContext *ctx, Atom *pattern,
                            Atom *value, Atom *body) {
    Atom *let_args[] = { pattern, value, body };
    return make_expr_with_head(ctx->arena,
                               atom_symbol_id(ctx->arena, g_builtin_syms.let),
                               let_args, 3);
}

static Atom *petta_wrap_noeval(CettaPettaLowerContext *ctx, Atom *value) {
    Atom *args[] = { value };
    return make_expr_with_head(ctx->arena,
                               atom_symbol(ctx->arena, "noeval"),
                               args, 1);
}

static Atom *petta_rel_goal_true(CettaPettaLowerContext *ctx) {
    return atom_symbol_id(ctx->arena, g_builtin_syms.petta_rel_true);
}

static bool petta_rel_goal_is_true(Atom *goal) {
    return atom_is_symbol_id(goal, g_builtin_syms.petta_rel_true);
}

static Atom *petta_wrap_rel_call(CettaPettaLowerContext *ctx,
                                 Atom *forward_expr,
                                 Atom *relation_expr,
                                 Atom *actual) {
    Atom *args[] = {
        forward_expr,
        relation_expr ? relation_expr : atom_unit(ctx->arena),
        actual,
    };
    return make_expr_with_head(ctx->arena,
                               atom_symbol_id(ctx->arena, g_builtin_syms.petta_rel_call),
                               args, 3);
}

static Atom *petta_wrap_rel_conj(CettaPettaLowerContext *ctx,
                                 Atom *lhs,
                                 Atom *rhs) {
    if (!lhs) return rhs;
    if (!rhs) return lhs;
    if (petta_rel_goal_is_true(lhs)) return rhs;
    if (petta_rel_goal_is_true(rhs)) return lhs;
    Atom *args[] = { lhs, rhs };
    return make_expr_with_head(ctx->arena,
                               atom_symbol_id(ctx->arena, g_builtin_syms.petta_rel_conj),
                               args, 2);
}

static Atom *petta_wrap_rel_run(CettaPettaLowerContext *ctx,
                                Atom *goal,
                                Atom *body) {
    Atom *args[] = { goal, body };
    return make_expr_with_head(ctx->arena,
                               atom_symbol_id(ctx->arena, g_builtin_syms.petta_rel_run),
                               args, 2);
}

static Atom *petta_miss_expr(CettaPettaLowerContext *ctx) {
    Atom *superpose_args[] = { atom_unit(ctx->arena) };
    return make_expr_with_head(ctx->arena,
                               atom_symbol_id(ctx->arena, g_builtin_syms.superpose),
                               superpose_args, 1);
}

static Atom *petta_export_pack(CettaPettaLowerContext *ctx,
                               Atom *const *items,
                               uint32_t count) {
    if (count == 0)
        return atom_unit(ctx->arena);
    return atom_expr(ctx->arena, (Atom **)items, count);
}

static Atom *petta_rel_result_pack(CettaPettaLowerContext *ctx,
                                   Atom *out_actual,
                                   Atom *export_pack) {
    Atom *items[] = { out_actual, export_pack };
    return atom_expr(ctx->arena, items, 2);
}

static Atom *petta_lower_term(CettaPettaLowerContext *ctx, Atom *term);
static Atom *petta_lower_value(CettaPettaLowerContext *ctx, Atom *term);
static Atom *petta_lower_goal_value(CettaPettaLowerContext *ctx, Atom *term);
static Atom *petta_lower_pattern(CettaPettaLowerContext *ctx, Atom *term);
static Atom *petta_lower_decl_head(CettaPettaLowerContext *ctx, Atom *term);
static Atom *petta_lower_decl_body(CettaPettaLowerContext *ctx, Atom *term);
static Atom *petta_lower_template(CettaPettaLowerContext *ctx, Atom *term);
static Atom *petta_lower_mutation_payload(CettaPettaLowerContext *ctx, Atom *term);
static Atom *petta_lower_case_pairs(CettaPettaLowerContext *ctx, Atom *term);
static Atom *petta_lower_arg_by_mode(CettaPettaLowerContext *ctx,
                                     PettaArgMode mode,
                                     Atom *arg);

static Atom *petta_lower_collapse_call(CettaPettaLowerContext *ctx, Atom *arg) {
    Atom *collapse_args[] = { arg };
    Atom *collapse_call = make_expr_with_head(
        ctx->arena, atom_symbol_id(ctx->arena, g_builtin_syms.collapse),
        collapse_args, 1);
    return petta_lower_term(ctx, collapse_call);
}

static bool petta_term_needs_relation(
    Atom *term,
    const CettaPettaCallableInventory *callables);
static Atom *petta_lower_profile_call(CettaPettaLowerContext *ctx,
                                      Atom *head,
                                      const PettaHeadProfile *profile,
                                      Atom *const *args,
                                      uint32_t nargs);
static Atom *petta_compile_relation_value_goal(CettaPettaLowerContext *ctx,
                                               Atom *expr,
                                               Atom *actual);
static Atom *petta_compile_relation_goal_expr(CettaPettaLowerContext *ctx,
                                              Atom *expr);
static Atom *petta_lower_relation_goal_bool(CettaPettaLowerContext *ctx,
                                            Atom *goal_expr);
static void petta_mark_relation_lowering_callables(Atom **atoms, int n,
                                                   CettaPettaCallableInventory *callables,
                                                   bool include_imported);
static void petta_mark_callable_syntax_args(Atom **atoms, int n,
                                            CettaPettaCallableInventory *callables,
                                            bool include_imported);
static bool petta_term_contains_vars(Atom *term);

static void petta_callable_set_init(CettaPettaCallableInventory *set) {
    memset(set, 0, sizeof(*set));
}

static void petta_callable_set_free(CettaPettaCallableInventory *set) {
    if (!set) return;
    free(set->items);
    set->items = NULL;
    set->len = 0;
    set->cap = 0;
}

static const CettaPettaCallableInfo *petta_callable_set_find(
    const CettaPettaCallableInventory *set,
    SymbolId head_id,
    uint32_t arity
) {
    if (!set || head_id == SYMBOL_ID_NONE) return NULL;
    for (uint32_t i = 0; i < set->len; i++) {
        if (set->items[i].head_id == head_id &&
            set->items[i].arity == arity) {
            return &set->items[i];
        }
    }
    return NULL;
}

static bool petta_callable_set_add(CettaPettaCallableInventory *set,
                                   Arena *arena,
                                   SymbolId head_id,
                                   uint32_t arity,
                                   bool imported) {
    if (!set || !arena || head_id == SYMBOL_ID_NONE) {
        return true;
    }
    const CettaPettaCallableInfo *existing = petta_callable_set_find(set, head_id, arity);
    if (existing) {
        CettaPettaCallableInfo *mutable_existing = &set->items[existing - set->items];
        mutable_existing->imported = mutable_existing->imported && imported;
        return true;
    }
    if (set->len == set->cap) {
        uint32_t new_cap = set->cap ? set->cap * 2 : 16;
        CettaPettaCallableInfo *new_items =
            cetta_realloc(set->items, sizeof(CettaPettaCallableInfo) * new_cap);
        if (!new_items) return false;
        set->items = new_items;
        set->cap = new_cap;
    }
    set->items[set->len++] = (CettaPettaCallableInfo){
        .head_id = head_id,
        .relation_head_id = cetta_petta_relation_head_id(arena, head_id, arity),
        .arity = arity,
        .imported = imported,
        .needs_relation_lowering = false,
        .lhs_needs_relation_lowering = false,
        .goal_relation_lowering = false,
        .syntax_arg_mask = 0,
    };
    return true;
}

static void petta_path_set_init(CettaPettaPathSet *set) {
    memset(set, 0, sizeof(*set));
}

static void petta_path_set_free(CettaPettaPathSet *set) {
    if (!set) return;
    for (uint32_t i = 0; i < set->len; i++) {
        free(set->items[i]);
    }
    free(set->items);
    set->items = NULL;
    set->len = 0;
    set->cap = 0;
}

static bool petta_path_set_contains(const CettaPettaPathSet *set,
                                    const char *path) {
    if (!set || !path) return false;
    for (uint32_t i = 0; i < set->len; i++) {
        if (strcmp(set->items[i], path) == 0) return true;
    }
    return false;
}

static bool petta_path_set_add(CettaPettaPathSet *set, const char *path) {
    if (!set || !path || petta_path_set_contains(set, path)) return true;
    if (set->len == set->cap) {
        uint32_t new_cap = set->cap ? set->cap * 2 : 16;
        char **new_items = cetta_realloc(set->items, sizeof(char *) * new_cap);
        if (!new_items) return false;
        set->items = new_items;
        set->cap = new_cap;
    }
    char *copy = petta_owned_strdup(path);
    if (!copy) return false;
    set->items[set->len++] = copy;
    return true;
}

static bool petta_declares_callable_head(Atom *term, SymbolId *out_head,
                                         uint32_t *out_arity) {
    if (!term || term->kind != ATOM_EXPR || term->expr.len != 3) return false;
    if (!petta_expr_is_declaration_form(term)) return false;
    Atom *lhs = term->expr.elems[1];
    if (!lhs || lhs->kind != ATOM_EXPR || lhs->expr.len == 0) return false;
    Atom *head = lhs->expr.elems[0];
    if (!head || head->kind != ATOM_SYMBOL) return false;
    if (out_head) *out_head = head->sym_id;
    if (out_arity) *out_arity = lhs->expr.len - 1;
    return true;
}

static void petta_collect_program_callable_heads(Atom **atoms, int n,
                                                 Arena *arena,
                                                 CettaPettaCallableInventory *set,
                                                 bool imported) {
    if (!atoms || n <= 0 || !set) return;
    for (int i = 0; i < n; i++) {
        SymbolId head = SYMBOL_ID_NONE;
        uint32_t arity = 0;
        if (petta_declares_callable_head(atoms[i], &head, &arity)) {
            petta_callable_set_add(set, arena, head, arity, imported);
        }
    }
}

static void petta_collect_space_callable_heads(Space *space,
                                               Arena *arena,
                                               CettaPettaCallableInventory *set) {
    if (!space || !arena || !set)
        return;
    for (uint32_t i = 0; i < space_length(space); i++) {
        Atom *term = space_get_at(space, i);
        SymbolId head = SYMBOL_ID_NONE;
        uint32_t arity = 0;
        if (!petta_declares_callable_head(term, &head, &arity))
            continue;
        Atom *lhs = term->expr.elems[1];
        Atom *head_atom = lhs && lhs->kind == ATOM_EXPR && lhs->expr.len > 0
                              ? lhs->expr.elems[0]
                              : NULL;
        if (cetta_petta_relation_symbol_is_hidden(head_atom))
            continue;
        petta_callable_set_add(set, arena, head, arity, true);
    }
}

static const char *petta_string_like_atom(Atom *atom) {
    if (!atom) return NULL;
    if (atom->kind == ATOM_SYMBOL) return atom_name_cstr(atom);
    if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_STRING) {
        return atom->ground.sval;
    }
    return NULL;
}

static const char *petta_import_spec_atom(Atom *atom, char *buf, size_t buf_sz) {
    const char *spec = petta_string_like_atom(atom);
    if (spec)
        return spec;
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len != 2 ||
        !expr_head_is_name(atom, "library")) {
        return NULL;
    }
    const char *name = petta_string_like_atom(atom->expr.elems[1]);
    if (!name || !*name)
        return NULL;
    if (strncmp(name, "lib_", 4) == 0) {
        int n = snprintf(buf, buf_sz, "lib/%s", name);
        return n > 0 && (size_t)n < buf_sz ? buf : NULL;
    }
    return name;
}

static bool petta_path_is_existing_file(const char *path) {
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static bool petta_try_import_candidate(const char *base_dir, const char *spec,
                                       char *resolved, size_t resolved_sz) {
    char candidate[PATH_MAX];
    if (!cetta_text_path_resolve(base_dir, spec, candidate, sizeof(candidate)))
        return false;
    if (petta_path_is_existing_file(candidate)) {
        return snprintf(resolved, resolved_sz, "%s", candidate) < (int)resolved_sz;
    }
    if (snprintf(candidate, sizeof(candidate), "%s.metta", spec) >= (int)sizeof(candidate))
        return false;
    if (!cetta_text_path_resolve(base_dir, candidate, resolved, resolved_sz))
        return false;
    return petta_path_is_existing_file(resolved);
}

static void petta_copy_parent_dir(char *out, size_t out_sz, const char *path) {
    size_t len;
    char *slash;

    if (!out || out_sz == 0)
        return;
    if (!path || !*path) {
        snprintf(out, out_sz, ".");
        return;
    }
    snprintf(out, out_sz, "%s", path);
    len = strlen(out);
    while (len > 1 && out[len - 1] == '/') {
        out[--len] = '\0';
    }
    slash = strrchr(out, '/');
    if (!slash) {
        snprintf(out, out_sz, ".");
        return;
    }
    if (slash == out) {
        slash[1] = '\0';
    } else {
        *slash = '\0';
    }
}

static void petta_get_working_dir(char *out, size_t out_sz) {
    if (!out || out_sz == 0)
        return;
    if (!getcwd(out, out_sz)) {
        snprintf(out, out_sz, ".");
    }
}

static bool petta_resolve_static_import(CettaRelativeModulePolicy policy,
                                        const char *working_dir,
                                        const char *source_path,
                                        const char *spec,
                                        char *resolved, size_t resolved_sz) {
    char current_dir[PATH_MAX];
    char probe_dir[PATH_MAX];

    if (!working_dir || !*working_dir || !spec || !*spec)
        return false;
    if (strchr(spec, ':'))
        return false;

    if (policy == CETTA_RELATIVE_MODULE_POLICY_WORKING_DIR_ONLY) {
        return petta_try_import_candidate(working_dir, spec, resolved, resolved_sz);
    }

    if (source_path && *source_path) {
        petta_copy_parent_dir(current_dir, sizeof(current_dir), source_path);
    } else {
        snprintf(current_dir, sizeof(current_dir), "%s", working_dir);
    }

    if (policy == CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY) {
        return petta_try_import_candidate(current_dir, spec, resolved, resolved_sz);
    }

    snprintf(probe_dir, sizeof(probe_dir), "%s", current_dir);
    while (true) {
        if (petta_try_import_candidate(probe_dir, spec, resolved, resolved_sz))
            return true;
        if (strcmp(probe_dir, "/") == 0 || strcmp(probe_dir, ".") == 0)
            break;
        char parent[PATH_MAX];
        petta_copy_parent_dir(parent, sizeof(parent), probe_dir);
        if (strcmp(parent, probe_dir) == 0)
            break;
        snprintf(probe_dir, sizeof(probe_dir), "%s", parent);
    }
    return false;
}

static bool petta_static_import_spec_at(Atom **atoms, int n, int i,
                                        char *spec_buf, size_t spec_buf_sz,
                                        const char **spec_out) {
    if (!atoms || i < 0 || i + 1 >= n || !spec_out) return false;
    if (!atom_is_symbol_name(atoms[i], "!")) return false;
    Atom *expr = atoms[i + 1];
    if (!expr || expr->kind != ATOM_EXPR || expr->expr.len != 3)
        return false;
    if (!expr_head_is_name(expr, "import!"))
        return false;
    Atom *dest = expr->expr.elems[1];
    if (!dest || !atom_is_symbol_name(dest, "&self"))
        return false;
    const char *spec = petta_import_spec_atom(expr->expr.elems[2],
                                             spec_buf, spec_buf_sz);
    if (!spec || !*spec)
        return false;
    *spec_out = spec;
    return true;
}

static void petta_collect_imported_callable_heads_from_file(
    const char *path,
    CettaRelativeModulePolicy policy,
    const char *working_dir,
    CettaPettaCallableInventory *callables,
    CettaPettaPathSet *visited
) {
    Arena parse_arena;
    Atom **atoms = NULL;
    int n = 0;

    if (!path || !working_dir || !callables || !visited ||
        petta_path_set_contains(visited, path))
        return;
    if (!petta_path_set_add(visited, path))
        return;

    arena_init(&parse_arena);
    arena_set_runtime_kind(&parse_arena, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    n = parse_metta_file(path, &parse_arena, &atoms);
    if (n < 0) {
        free(atoms);
        arena_free(&parse_arena);
        return;
    }
    for (int i = 0; i < n; i++) {
        atoms[i] = petta_rewrite_empty_literals(&parse_arena, atoms[i]);
    }

    petta_collect_program_callable_heads(atoms, n, &parse_arena, callables, true);
    for (int i = 0; i + 1 < n; i++) {
        const char *spec = NULL;
        char spec_buf[PATH_MAX];
        if (!petta_static_import_spec_at(atoms, n, i, spec_buf,
                                         sizeof(spec_buf), &spec))
            continue;
        char resolved[PATH_MAX];
        if (petta_resolve_static_import(policy, working_dir, path, spec, resolved,
                                        sizeof(resolved))) {
            petta_collect_imported_callable_heads_from_file(
                resolved, policy, working_dir, callables, visited);
        }
        i++;
    }

    petta_mark_callable_syntax_args(atoms, n, callables, true);
    petta_mark_relation_lowering_callables(atoms, n, callables, true);

    free(atoms);
    arena_free(&parse_arena);
}

static bool petta_head_is_core_callable(Atom *head) {
    const PettaHeadProfile *profile = petta_head_profile(head, 0);
    return profile && profile->core_callable;
}

static PettaBuiltinRelationKind petta_builtin_relation_kind(Atom *head,
                                                            uint32_t arity) {
    const PettaHeadProfile *profile = NULL;
    if (!head || head->kind != ATOM_SYMBOL)
        return PETTA_BUILTIN_REL_NONE;
    profile = petta_head_profile(head, arity);
    return profile ? profile->builtin_relation : PETTA_BUILTIN_REL_NONE;
}

static bool petta_head_has_builtin_relation(Atom *head, uint32_t arity) {
    return petta_builtin_relation_kind(head, arity) != PETTA_BUILTIN_REL_NONE;
}

static const CettaPettaCallableInfo *petta_callable_info(
    CettaPettaLowerContext *ctx,
    Atom *head,
    uint32_t arity
) {
    if (!head || head->kind != ATOM_SYMBOL) return NULL;
    return petta_callable_set_find(ctx ? ctx->callables : NULL, head->sym_id, arity);
}

static CettaPettaCallableInfo *petta_callable_info_mut(
    CettaPettaCallableInventory *set,
    Atom *head,
    uint32_t arity
) {
    if (!set || !head || head->kind != ATOM_SYMBOL) return NULL;
    for (uint32_t i = 0; i < set->len; i++) {
        if (set->items[i].head_id == head->sym_id &&
            set->items[i].arity == arity) {
            return &set->items[i];
        }
    }
    return NULL;
}

static bool petta_head_is_callable(CettaPettaLowerContext *ctx, Atom *head,
                                   uint32_t arity) {
    if (!head || head->kind != ATOM_SYMBOL) return false;
    return is_grounded_op(head->sym_id) ||
           petta_head_has_builtin_relation(head, arity) ||
           petta_head_is_core_callable(head) ||
           petta_callable_info(ctx, head, arity) != NULL;
}

static bool petta_is_suppressed_recursive_value_call(
    CettaPettaLowerContext *ctx,
    Atom *head,
    uint32_t arity
) {
    return ctx &&
           head &&
           head->kind == ATOM_SYMBOL &&
           ctx->suppressed_recursive_value_head != SYMBOL_ID_NONE &&
           head->sym_id == ctx->suppressed_recursive_value_head &&
           arity == ctx->suppressed_recursive_value_arity;
}

static bool petta_may_lower_relation_subterm(CettaPettaLowerContext *ctx,
                                             Atom *term) {
    if (!term || term->kind != ATOM_EXPR || term->expr.len == 0)
        return true;
    Atom *head = term->expr.elems[0];
    uint32_t nargs = term->expr.len - 1;
    return !petta_is_suppressed_recursive_value_call(ctx, head, nargs);
}

static CettaPettaBuiltinRelationKind
petta_contract_builtin_relation_kind(PettaBuiltinRelationKind kind) {
    switch (kind) {
    case PETTA_BUILTIN_REL_APPEND:
        return CETTA_PETTA_BUILTIN_REL_APPEND;
    case PETTA_BUILTIN_REL_MEMBER:
        return CETTA_PETTA_BUILTIN_REL_MEMBER;
    case PETTA_BUILTIN_REL_SIZE_ATOM:
        return CETTA_PETTA_BUILTIN_REL_SIZE_ATOM;
    case PETTA_BUILTIN_REL_HASH_PLUS:
        return CETTA_PETTA_BUILTIN_REL_HASH_PLUS;
    case PETTA_BUILTIN_REL_HASH_EQ:
        return CETTA_PETTA_BUILTIN_REL_HASH_EQ;
    case PETTA_BUILTIN_REL_HASH_LT:
        return CETTA_PETTA_BUILTIN_REL_HASH_LT;
    case PETTA_BUILTIN_REL_HASH_GT:
        return CETTA_PETTA_BUILTIN_REL_HASH_GT;
    default:
        return CETTA_PETTA_BUILTIN_REL_NONE;
    }
}

typedef enum {
    PETTA_GUARDED_APPLY_FORWARD = 0,
    PETTA_GUARDED_APPLY_RELATION = 1,
} PettaGuardedApplyMode;

static SymbolId petta_builtin_relation_head(CettaPettaLowerContext *ctx,
                                            Atom *head,
                                            uint32_t arity) {
    if (!ctx || !head || head->kind != ATOM_SYMBOL)
        return SYMBOL_ID_NONE;
    return cetta_petta_builtin_relation_head_id(
        ctx->arena,
        petta_contract_builtin_relation_kind(
            petta_builtin_relation_kind(head, arity)));
}

static bool petta_callable_has_relation_form(const CettaPettaCallableInfo *info) {
    return info != NULL;
}

static bool petta_term_has_relation_form(Atom *term,
                                         const CettaPettaCallableInventory *callables) {
    if (!term || term->kind != ATOM_EXPR || term->expr.len == 0)
        return false;
    if (expr_head_is_name(term, "quote") && term->expr.len == 2)
        return false;

    Atom *head = term->expr.elems[0];
    uint32_t nargs = term->expr.len - 1;
    if (petta_head_has_builtin_relation(head, nargs))
        return true;
    if (head && head->kind == ATOM_SYMBOL && callables) {
        const CettaPettaCallableInfo *info =
            petta_callable_set_find(callables, head->sym_id, nargs);
        if (petta_callable_has_relation_form(info))
            return true;
    }

    uint32_t child_start = (head && head->kind == ATOM_SYMBOL) ? 1 : 0;
    for (uint32_t i = child_start; i < term->expr.len; i++) {
        if (petta_term_has_relation_form(term->expr.elems[i], callables))
            return true;
    }
    return false;
}

static bool petta_collect_pattern_bound_vars(Atom *term, PettaBoundVarSet *bound) {
    if (!term)
        return true;
    if (term->kind == ATOM_VAR)
        return petta_bound_var_set_push(bound, term->var_id);
    if (term->kind != ATOM_EXPR || term->expr.len == 0)
        return true;
    if (expr_head_is_name(term, "quote") && term->expr.len == 2)
        return true;
    for (uint32_t i = 0; i < term->expr.len; i++) {
        if (!petta_collect_pattern_bound_vars(term->expr.elems[i], bound))
            return false;
    }
    return true;
}

static bool petta_term_contains_free_vars_rec(Atom *term, PettaBoundVarSet *bound) {
    if (!term)
        return false;
    if (term->kind == ATOM_VAR)
        return !petta_bound_var_set_contains(bound, term->var_id);
    if (term->kind != ATOM_EXPR || term->expr.len == 0)
        return false;
    if (expr_head_is_name(term, "quote") && term->expr.len == 2)
        return false;

    Atom *head = term->expr.elems[0];
    uint32_t nargs = term->expr.len - 1;

    if (atom_is_symbol_name(head, "match") && nargs == 3) {
        uint32_t saved_len = bound ? bound->len : 0;
        if (petta_term_contains_free_vars_rec(term->expr.elems[1], bound))
            return true;
        if (bound && !petta_collect_pattern_bound_vars(term->expr.elems[2], bound))
            return true;
        bool body_has_free =
            petta_term_contains_free_vars_rec(term->expr.elems[3], bound);
        if (bound)
            bound->len = saved_len;
        return body_has_free;
    }

    if (atom_is_symbol_name(head, "let") && nargs == 3) {
        uint32_t saved_len = bound ? bound->len : 0;
        if (petta_term_contains_free_vars_rec(term->expr.elems[2], bound))
            return true;
        if (bound && !petta_collect_pattern_bound_vars(term->expr.elems[1], bound))
            return true;
        bool body_has_free =
            petta_term_contains_free_vars_rec(term->expr.elems[3], bound);
        if (bound)
            bound->len = saved_len;
        return body_has_free;
    }

    if (atom_is_symbol_name(head, "let*") && nargs == 2) {
        Atom *bindings = term->expr.elems[1];
        uint32_t saved_len = bound ? bound->len : 0;
        if (bindings && bindings->kind == ATOM_EXPR) {
            for (uint32_t i = 0; i < bindings->expr.len; i++) {
                Atom *binding = bindings->expr.elems[i];
                if (!binding || binding->kind != ATOM_EXPR || binding->expr.len != 2)
                    continue;
                if (petta_term_contains_free_vars_rec(binding->expr.elems[1], bound)) {
                    if (bound)
                        bound->len = saved_len;
                    return true;
                }
                if (bound &&
                    !petta_collect_pattern_bound_vars(binding->expr.elems[0], bound)) {
                    bound->len = saved_len;
                    return true;
                }
            }
        }
        bool body_has_free =
            petta_term_contains_free_vars_rec(term->expr.elems[2], bound);
        if (bound)
            bound->len = saved_len;
        return body_has_free;
    }

    for (uint32_t i = 0; i < term->expr.len; i++) {
        if (petta_term_contains_free_vars_rec(term->expr.elems[i], bound))
            return true;
    }
    return false;
}

static bool petta_term_contains_vars(Atom *term) {
    PettaBoundVarSet bound = {0};
    bool has_free = petta_term_contains_free_vars_rec(term, &bound);
    petta_bound_var_set_free(&bound);
    return has_free;
}

static bool petta_collect_all_vars_rec(Atom *term, PettaBoundVarSet *vars) {
    if (!term)
        return true;
    if (term->kind == ATOM_VAR)
        return petta_bound_var_set_push(vars, term->var_id);
    if (term->kind != ATOM_EXPR || term->expr.len == 0)
        return true;
    if (expr_head_is_name(term, "quote") && term->expr.len == 2)
        return true;
    for (uint32_t i = 0; i < term->expr.len; i++) {
        if (!petta_collect_all_vars_rec(term->expr.elems[i], vars))
            return false;
    }
    return true;
}

static bool petta_arg_mode_preserves_syntax(PettaArgMode mode) {
    return mode == PETTA_ARG_PATTERN ||
           mode == PETTA_ARG_TEMPLATE ||
           mode == PETTA_ARG_DECL_HEAD;
}

static bool petta_collect_rhs_syntax_param_vars(Atom *term,
                                                PettaBoundVarSet *vars) {
    if (!term)
        return true;
    if (term->kind != ATOM_EXPR || term->expr.len == 0)
        return true;
    if (expr_head_is_name(term, "quote") && term->expr.len == 2)
        return petta_collect_all_vars_rec(term->expr.elems[1], vars);

    Atom *head = term->expr.elems[0];
    uint32_t nargs = term->expr.len - 1;
    const PettaHeadProfile *profile = petta_head_profile(head, nargs);
    if (profile) {
        for (uint32_t i = 0; i < nargs; i++) {
            Atom *arg = term->expr.elems[i + 1];
            if (petta_arg_mode_preserves_syntax(profile->arg_modes[i])) {
                if (!petta_collect_all_vars_rec(arg, vars))
                    return false;
            } else if (!petta_collect_rhs_syntax_param_vars(arg, vars)) {
                return false;
            }
        }
        return true;
    }

    if (atom_is_symbol_name(head, "noeval") && nargs == 1)
        return petta_collect_all_vars_rec(term->expr.elems[1], vars);

    if (atom_is_symbol_name(head, "unify") && nargs == 4) {
        return petta_collect_all_vars_rec(term->expr.elems[1], vars) &&
               petta_collect_all_vars_rec(term->expr.elems[2], vars) &&
               petta_collect_rhs_syntax_param_vars(term->expr.elems[3], vars) &&
               petta_collect_rhs_syntax_param_vars(term->expr.elems[4], vars);
    }

    if (atom_is_symbol_name(head, "chain") && nargs == 3) {
        return petta_collect_rhs_syntax_param_vars(term->expr.elems[1], vars) &&
               petta_collect_all_vars_rec(term->expr.elems[2], vars) &&
               petta_collect_rhs_syntax_param_vars(term->expr.elems[3], vars);
    }

    if (atom_is_symbol_name(head, "let") && nargs == 3) {
        return petta_collect_all_vars_rec(term->expr.elems[1], vars) &&
               petta_collect_rhs_syntax_param_vars(term->expr.elems[2], vars) &&
               petta_collect_rhs_syntax_param_vars(term->expr.elems[3], vars);
    }

    for (uint32_t i = 0; i < term->expr.len; i++) {
        if (!petta_collect_rhs_syntax_param_vars(term->expr.elems[i], vars))
            return false;
    }
    return true;
}

static Atom *petta_relation_call_expr(CettaPettaLowerContext *ctx,
                                      Atom *call_expr,
                                      Atom *actual) {
    if (!call_expr || call_expr->kind != ATOM_EXPR || call_expr->expr.len == 0)
        return NULL;
    Atom *head = call_expr->expr.elems[0];
    uint32_t nargs = call_expr->expr.len - 1;
    SymbolId relation_head = petta_builtin_relation_head(ctx, head, nargs);
    if (relation_head == SYMBOL_ID_NONE) {
        const CettaPettaCallableInfo *info = petta_callable_info(ctx, head, nargs);
        if (petta_callable_has_relation_form(info))
            relation_head = info->relation_head_id;
    }
    if (relation_head == SYMBOL_ID_NONE)
        return NULL;

    Atom **args = arena_alloc(ctx->arena, sizeof(Atom *) * (nargs + 2));
    for (uint32_t i = 0; i < nargs; i++) {
        args[i] = call_expr->expr.elems[i + 1];
    }
    args[nargs] = actual;
    args[nargs + 1] = petta_export_pack(ctx, call_expr->expr.elems + 1, nargs);
    return make_expr_with_head(ctx->arena,
                               atom_symbol_id(ctx->arena, relation_head),
                               args, nargs + 2);
}

static Atom *petta_prepare_relation_arg_for_result(CettaPettaLowerContext *ctx,
                                                   Atom *arg,
                                                   Atom **post_goal,
                                                   bool *changed) {
    if (!arg || arg->kind != ATOM_EXPR || arg->expr.len == 0)
        return arg;
    if (expr_head_is_name(arg, "quote") && arg->expr.len == 2)
        return arg;

    Atom *head = arg->expr.elems[0];
    if (head && head->kind == ATOM_SYMBOL &&
        petta_may_lower_relation_subterm(ctx, arg) &&
        petta_term_contains_vars(arg) &&
        petta_term_has_relation_form(arg, ctx->callables)) {
        Atom *actual = petta_fresh_var(ctx, "result_part");
        Atom *goal = petta_compile_relation_value_goal(ctx, arg, actual);
        if (goal) {
            *post_goal = petta_wrap_rel_conj(ctx, *post_goal, goal);
            if (changed)
                *changed = true;
            return actual;
        }
    }

    uint32_t child_start = (head && head->kind == ATOM_SYMBOL) ? 1 : 0;
    Atom **elems = NULL;
    for (uint32_t i = child_start; i < arg->expr.len; i++) {
        Atom *child = arg->expr.elems[i];
        Atom *next =
            petta_prepare_relation_arg_for_result(ctx, child, post_goal, changed);
        if (!elems && next != child) {
            elems = arena_alloc(ctx->arena, sizeof(Atom *) * arg->expr.len);
            for (uint32_t j = 0; j < arg->expr.len; j++) {
                elems[j] = arg->expr.elems[j];
            }
        }
        if (elems)
            elems[i] = next;
    }
    return elems ? atom_expr(ctx->arena, elems, arg->expr.len) : arg;
}

static Atom *petta_relation_call_expr_with_result_guards(
    CettaPettaLowerContext *ctx,
    Atom *call_expr,
    Atom *actual,
    Atom **post_goal
) {
    if (!call_expr || call_expr->kind != ATOM_EXPR || call_expr->expr.len == 0)
        return NULL;
    Atom *head = call_expr->expr.elems[0];
    uint32_t nargs = call_expr->expr.len - 1;
    SymbolId relation_head = petta_builtin_relation_head(ctx, head, nargs);
    if (relation_head == SYMBOL_ID_NONE) {
        const CettaPettaCallableInfo *info = petta_callable_info(ctx, head, nargs);
        if (petta_callable_has_relation_form(info))
            relation_head = info->relation_head_id;
    }
    if (relation_head == SYMBOL_ID_NONE)
        return NULL;

    Atom *goal = post_goal && *post_goal ? *post_goal : petta_rel_goal_true(ctx);
    bool changed = false;
    Atom **args = arena_alloc(ctx->arena, sizeof(Atom *) * (nargs + 2));
    for (uint32_t i = 0; i < nargs; i++) {
        args[i] = petta_prepare_relation_arg_for_result(
            ctx, call_expr->expr.elems[i + 1], &goal, &changed);
    }
    args[nargs] = actual;
    args[nargs + 1] = petta_export_pack(ctx, args, nargs);
    if (post_goal)
        *post_goal = goal;
    (void)changed;
    return make_expr_with_head(ctx->arena,
                               atom_symbol_id(ctx->arena, relation_head),
                               args, nargs + 2);
}

static Atom *petta_lower_decl_body(CettaPettaLowerContext *ctx, Atom *term) {
    return petta_lower_value(ctx, term);
}

static Atom *petta_lower_goal_value(CettaPettaLowerContext *ctx, Atom *term) {
    Atom *goal_value = petta_lower_relation_goal_bool(ctx, term);
    return goal_value ? goal_value : petta_lower_value(ctx, term);
}

static Atom *petta_lower_pattern(CettaPettaLowerContext *ctx, Atom *term) {
    (void)ctx;
    return term;
}

static Atom *petta_lower_decl_head(CettaPettaLowerContext *ctx, Atom *term) {
    return petta_lower_pattern(ctx, term);
}

static Atom *petta_lower_template(CettaPettaLowerContext *ctx, Atom *term) {
    return petta_lower_value(ctx, term);
}

static Atom *petta_lower_mutation_payload(CettaPettaLowerContext *ctx,
                                          Atom *term) {
    (void)ctx;
    return term;
}

static Atom *petta_lower_case_pairs(CettaPettaLowerContext *ctx, Atom *term) {
    (void)ctx;
    return term;
}

static Atom *petta_lower_arg_by_mode(CettaPettaLowerContext *ctx,
                                     PettaArgMode mode,
                                     Atom *arg) {
    switch (mode) {
    case PETTA_ARG_VALUE:
        return petta_lower_value(ctx, arg);
    case PETTA_ARG_GOAL:
        return petta_lower_goal_value(ctx, arg);
    case PETTA_ARG_PATTERN:
        return petta_lower_pattern(ctx, arg);
    case PETTA_ARG_DECL_HEAD:
        return petta_lower_decl_head(ctx, arg);
    case PETTA_ARG_DECL_BODY:
        return petta_lower_decl_body(ctx, arg);
    case PETTA_ARG_TEMPLATE:
        return petta_lower_template(ctx, arg);
    case PETTA_ARG_MUTATION_PAYLOAD:
        return petta_lower_mutation_payload(ctx, arg);
    case PETTA_ARG_CASE_PAIRS:
        return petta_lower_case_pairs(ctx, arg);
    }
    return petta_lower_value(ctx, arg);
}

static PettaArgMode petta_profile_arg_mode(const PettaHeadProfile *profile,
                                           uint32_t index,
                                           PettaArgMode fallback) {
    if (!profile || index >= profile->arity)
        return fallback;
    return profile->arg_modes[index];
}

static Atom *petta_lower_profile_arg(CettaPettaLowerContext *ctx,
                                     const PettaHeadProfile *profile,
                                     uint32_t index,
                                     Atom *arg) {
    return petta_lower_arg_by_mode(
        ctx, petta_profile_arg_mode(profile, index, PETTA_ARG_VALUE), arg);
}

static Atom *petta_compile_profile_goal_arg(CettaPettaLowerContext *ctx,
                                            const PettaHeadProfile *profile,
                                            uint32_t index,
                                            Atom *arg) {
    if (petta_profile_arg_mode(profile, index, PETTA_ARG_VALUE) != PETTA_ARG_GOAL)
        return NULL;
    return petta_compile_relation_goal_expr(ctx, arg);
}

static Atom *petta_lower_profile_call(CettaPettaLowerContext *ctx,
                                      Atom *head,
                                      const PettaHeadProfile *profile,
                                      Atom *const *args,
                                      uint32_t nargs) {
    if (!profile || profile->arity != nargs)
        return NULL;
    Atom **lowered_args = arena_alloc(ctx->arena, sizeof(Atom *) * nargs);
    for (uint32_t i = 0; i < nargs; i++) {
        lowered_args[i] = petta_lower_profile_arg(ctx, profile, i, args[i]);
    }
    return make_expr_with_head(ctx->arena, head, lowered_args, nargs);
}

static Atom *petta_relation_unify_goal(CettaPettaLowerContext *ctx,
                                       Atom *lhs,
                                       Atom *rhs) {
    Atom *unify_args[] = {
        petta_lower_value(ctx, lhs),
        petta_lower_value(ctx, rhs),
        atom_true(ctx->arena),
        atom_false(ctx->arena),
    };
    Atom *unify_expr = make_expr_with_head(
        ctx->arena,
        atom_symbol_id(ctx->arena, g_builtin_syms.unify),
        unify_args, 4);
    return petta_wrap_rel_call(ctx, unify_expr, atom_unit(ctx->arena),
                               atom_true(ctx->arena));
}

static Atom *petta_compile_relation_value_bind_goal(CettaPettaLowerContext *ctx,
                                                    Atom *value_expr,
                                                    Atom *actual) {
    if (petta_term_has_relation_form(value_expr, ctx->callables)) {
        Atom *value_goal =
            petta_compile_relation_value_goal(ctx, value_expr, actual);
        if (value_goal)
            return value_goal;
    }
    return petta_relation_unify_goal(ctx, actual, value_expr);
}

static Atom *petta_compile_relation_value_goal(CettaPettaLowerContext *ctx,
                                               Atom *expr,
                                               Atom *actual) {
    if (!expr || expr->kind != ATOM_EXPR || expr->expr.len == 0)
        return NULL;
    if (expr_head_is_name(expr, "quote") && expr->expr.len == 2)
        return NULL;

    Atom *head = expr->expr.elems[0];
    uint32_t nargs = expr->expr.len - 1;
    Atom *const *args = expr->expr.elems + 1;
    if (atom_is_symbol_name(head, "only") && nargs == 2) {
        Atom *constraint_goal = petta_compile_relation_goal_expr(ctx, args[0]);
        if (!constraint_goal)
            return NULL;
        Atom *value_goal =
            petta_compile_relation_value_bind_goal(ctx, args[1], actual);
        if (!value_goal)
            return NULL;
        return petta_wrap_rel_conj(ctx, constraint_goal, value_goal);
    }

    bool is_callable = petta_head_is_callable(ctx, head, nargs);
    if (!is_callable)
        return NULL;

    Atom *post_goal = petta_rel_goal_true(ctx);
    Atom *relation_call =
        petta_relation_call_expr_with_result_guards(ctx, expr, actual, &post_goal);
    if (!relation_call)
        return NULL;
    Atom *call_goal = petta_wrap_rel_call(
        ctx, expr, relation_call, actual);
    return petta_wrap_rel_conj(ctx, call_goal, post_goal);
}

static bool petta_expr_contains_head_name(Atom *expr, const char *name) {
    if (!expr || expr->kind != ATOM_EXPR || expr->expr.len == 0)
        return false;
    if (expr_head_is_name(expr, "quote") && expr->expr.len == 2)
        return false;
    if (atom_is_symbol_name(expr->expr.elems[0], name))
        return true;
    for (uint32_t i = 0; i < expr->expr.len; i++) {
        if (petta_expr_contains_head_name(expr->expr.elems[i], name))
            return true;
    }
    return false;
}

static bool petta_expr_is_size_atom_constraint(Atom *expr) {
    if (!expr || expr->kind != ATOM_EXPR || expr->expr.len != 3)
        return false;
    Atom *head = expr->expr.elems[0];
    if (!(atom_is_symbol_name(head, "==") ||
          atom_is_symbol_name(head, "=") ||
          atom_is_symbol_name(head, "#="))) {
        return false;
    }
    return expr_head_is_name(expr->expr.elems[1], "size-atom") ||
           expr_head_is_name(expr->expr.elems[2], "size-atom");
}

static bool petta_relation_expr_prefers_goal_lowering(Atom *expr) {
    if (!expr || expr->kind != ATOM_EXPR || expr->expr.len == 0)
        return false;

    Atom *head = expr->expr.elems[0];
    if (!head || head->kind != ATOM_SYMBOL)
        return true;

    const PettaHeadProfile *profile = petta_expr_head_profile(expr);
    return profile && profile->relation_goal_form;
}

static Atom *petta_compile_relation_goal_expr(CettaPettaLowerContext *ctx,
                                              Atom *expr) {
    if (!expr)
        return NULL;
    if (expr->kind != ATOM_EXPR || expr->expr.len == 0)
        return NULL;
    if (expr_head_is_name(expr, "quote") && expr->expr.len == 2)
        return NULL;

    Atom *head = expr->expr.elems[0];
    if (!head || head->kind != ATOM_SYMBOL) {
        Atom *goal = petta_rel_goal_true(ctx);
        bool saw_child_goal = false;
        for (uint32_t i = 0; i < expr->expr.len; i++) {
            Atom *child_goal =
                petta_compile_relation_goal_expr(ctx, expr->expr.elems[i]);
            if (!child_goal)
                return NULL;
            goal = petta_wrap_rel_conj(ctx, goal, child_goal);
            saw_child_goal = true;
        }
        return saw_child_goal ? goal : NULL;
    }

    if (expr->expr.len == 3 &&
        (atom_is_symbol_name(head, "=") || atom_is_symbol_name(head, "=="))) {
        Atom *goal = petta_rel_goal_true(ctx);
        Atom *lhs_value = expr->expr.elems[1];
        Atom *rhs_value = expr->expr.elems[2];
        bool lhs_is_relation = petta_term_has_relation_form(lhs_value, ctx->callables);
        bool rhs_is_relation = petta_term_has_relation_form(rhs_value, ctx->callables);

        if (lhs_is_relation && rhs_is_relation) {
            Atom *shared_actual = petta_fresh_var(ctx, "goal_eq_term");
            Atom *lhs_goal =
                petta_compile_relation_value_goal(ctx, lhs_value, shared_actual);
            Atom *rhs_goal =
                petta_compile_relation_value_goal(ctx, rhs_value, shared_actual);
            if (!lhs_goal || !rhs_goal)
                return NULL;
            goal = petta_wrap_rel_conj(ctx, goal, lhs_goal);
            goal = petta_wrap_rel_conj(ctx, goal, rhs_goal);
            return goal;
        }

        if (lhs_is_relation) {
            Atom *rhs_actual = petta_lower_term(ctx, rhs_value);
            Atom *lhs_goal =
                petta_compile_relation_value_goal(ctx, lhs_value, rhs_actual);
            if (!lhs_goal)
                return NULL;
            return petta_wrap_rel_conj(ctx, goal, lhs_goal);
        }

        if (rhs_is_relation) {
            Atom *lhs_actual = petta_lower_term(ctx, lhs_value);
            Atom *rhs_goal =
                petta_compile_relation_value_goal(ctx, rhs_value, lhs_actual);
            if (!rhs_goal)
                return NULL;
            return petta_wrap_rel_conj(ctx, goal, rhs_goal);
        }

        lhs_value = petta_lower_term(ctx, lhs_value);
        rhs_value = petta_lower_term(ctx, rhs_value);
        Atom *unify_args[] = {
            lhs_value,
            rhs_value,
            atom_true(ctx->arena),
            atom_false(ctx->arena),
        };
        Atom *unify_expr = make_expr_with_head(
            ctx->arena,
            atom_symbol_id(ctx->arena, g_builtin_syms.unify),
            unify_args, 4);
        Atom *unify_goal =
            petta_wrap_rel_call(ctx, unify_expr, atom_unit(ctx->arena),
                                atom_true(ctx->arena));
        return petta_wrap_rel_conj(ctx, goal, unify_goal);
    }

    if (expr->expr.len == 2 && atom_is_symbol_name(head, "assert")) {
        Atom *inner_goal =
            petta_compile_relation_goal_expr(ctx, expr->expr.elems[1]);
        if (inner_goal)
            return inner_goal;
        return petta_wrap_rel_call(ctx,
                                   petta_lower_term(ctx, expr->expr.elems[1]),
                                   atom_unit(ctx->arena),
                                   atom_true(ctx->arena));
    }

    if (expr->expr.len == 3 && atom_is_symbol_name(head, "and")) {
        Atom *lhs_expr = expr->expr.elems[1];
        Atom *rhs_expr = expr->expr.elems[2];
        if (petta_expr_is_size_atom_constraint(rhs_expr) &&
            petta_expr_contains_head_name(lhs_expr, "member")) {
            Atom *tmp = lhs_expr;
            lhs_expr = rhs_expr;
            rhs_expr = tmp;
        }
        Atom *lhs_goal = petta_compile_relation_goal_expr(ctx, lhs_expr);
        Atom *rhs_goal = petta_compile_relation_goal_expr(ctx, rhs_expr);
        if (!lhs_goal || !rhs_goal)
            return NULL;
        return petta_wrap_rel_conj(ctx, lhs_goal, rhs_goal);
    }

    if (expr->expr.len == 2 && atom_is_symbol_name(head, "once")) {
        return petta_compile_relation_goal_expr(ctx, expr->expr.elems[1]);
    }

    if (expr->expr.len == 3 &&
        petta_builtin_relation_kind(head, 2) == PETTA_BUILTIN_REL_HASH_EQ) {
        Atom *goal = petta_rel_goal_true(ctx);
        Atom *lhs_value = expr->expr.elems[1];
        Atom *rhs_value = expr->expr.elems[2];
        bool lhs_is_relation = petta_term_has_relation_form(lhs_value, ctx->callables);
        bool rhs_is_relation = petta_term_has_relation_form(rhs_value, ctx->callables);

        if (lhs_is_relation && rhs_is_relation) {
            Atom *shared_actual = petta_fresh_var(ctx, "goal_eq");
            Atom *lhs_goal =
                petta_compile_relation_value_goal(ctx, lhs_value, shared_actual);
            Atom *rhs_goal =
                petta_compile_relation_value_goal(ctx, rhs_value, shared_actual);
            if (!lhs_goal || !rhs_goal)
                return NULL;
            goal = petta_wrap_rel_conj(ctx, goal, lhs_goal);
            goal = petta_wrap_rel_conj(ctx, goal, rhs_goal);
            return goal;
        }

        if (lhs_is_relation) {
            Atom *rhs_actual = petta_lower_term(ctx, rhs_value);
            Atom *lhs_goal =
                petta_compile_relation_value_goal(ctx, lhs_value, rhs_actual);
            if (!lhs_goal)
                return NULL;
            return petta_wrap_rel_conj(ctx, goal, lhs_goal);
        }

        if (rhs_is_relation) {
            Atom *lhs_actual = petta_lower_term(ctx, lhs_value);
            Atom *rhs_goal =
                petta_compile_relation_value_goal(ctx, rhs_value, lhs_actual);
            if (!rhs_goal)
                return NULL;
            return petta_wrap_rel_conj(ctx, goal, rhs_goal);
        }

        lhs_value = petta_lower_term(ctx, lhs_value);
        rhs_value = petta_lower_term(ctx, rhs_value);
        Atom *cmp_args[] = { lhs_value, rhs_value };
        Atom *cmp_expr = make_expr_with_head(ctx->arena, head, cmp_args, 2);
        Atom *relation_call =
            petta_relation_call_expr(ctx, cmp_expr, atom_true(ctx->arena));
        Atom *cmp_goal =
            petta_wrap_rel_call(ctx, cmp_expr,
                                relation_call ? relation_call : atom_unit(ctx->arena),
                                atom_true(ctx->arena));
        return petta_wrap_rel_conj(ctx, goal, cmp_goal);
    }

    if (expr->expr.len == 3 &&
        (petta_builtin_relation_kind(head, 2) == PETTA_BUILTIN_REL_HASH_LT ||
         petta_builtin_relation_kind(head, 2) == PETTA_BUILTIN_REL_HASH_GT)) {
        Atom *goal = petta_rel_goal_true(ctx);
        Atom *lhs_value = expr->expr.elems[1];
        Atom *rhs_value = expr->expr.elems[2];

        if (petta_term_has_relation_form(lhs_value, ctx->callables)) {
            Atom *lhs_actual = petta_fresh_var(ctx, "goal_cmp_lhs");
            Atom *lhs_goal =
                petta_compile_relation_value_goal(ctx, lhs_value, lhs_actual);
            if (!lhs_goal)
                return NULL;
            goal = petta_wrap_rel_conj(ctx, goal, lhs_goal);
            lhs_value = lhs_actual;
        } else {
            lhs_value = petta_lower_term(ctx, lhs_value);
        }

        if (petta_term_has_relation_form(rhs_value, ctx->callables)) {
            Atom *rhs_actual = petta_fresh_var(ctx, "goal_cmp_rhs");
            Atom *rhs_goal =
                petta_compile_relation_value_goal(ctx, rhs_value, rhs_actual);
            if (!rhs_goal)
                return NULL;
            goal = petta_wrap_rel_conj(ctx, goal, rhs_goal);
            rhs_value = rhs_actual;
        } else {
            rhs_value = petta_lower_term(ctx, rhs_value);
        }

        Atom *cmp_args[] = { lhs_value, rhs_value };
        Atom *cmp_expr = make_expr_with_head(ctx->arena, head, cmp_args, 2);
        Atom *relation_call =
            petta_relation_call_expr(ctx, cmp_expr, atom_true(ctx->arena));
        Atom *cmp_goal =
            petta_wrap_rel_call(ctx, cmp_expr,
                                relation_call ? relation_call : atom_unit(ctx->arena),
                                atom_true(ctx->arena));
        return petta_wrap_rel_conj(ctx, goal, cmp_goal);
    }

    if (atom_is_symbol_name(head, ">") && expr->expr.len == 3) {
        Atom *goal = petta_rel_goal_true(ctx);
        Atom *lhs_value = expr->expr.elems[1];
        Atom *rhs_value = expr->expr.elems[2];

        if (petta_term_has_relation_form(lhs_value, ctx->callables)) {
            Atom *lhs_actual = petta_fresh_var(ctx, "goal_lhs");
            Atom *lhs_goal =
                petta_compile_relation_value_goal(ctx, lhs_value, lhs_actual);
            if (!lhs_goal)
                return NULL;
            goal = petta_wrap_rel_conj(ctx, goal, lhs_goal);
            lhs_value = lhs_actual;
        } else {
            lhs_value = petta_lower_term(ctx, lhs_value);
        }

        if (petta_term_has_relation_form(rhs_value, ctx->callables)) {
            Atom *rhs_actual = petta_fresh_var(ctx, "goal_rhs");
            Atom *rhs_goal =
                petta_compile_relation_value_goal(ctx, rhs_value, rhs_actual);
            if (!rhs_goal)
                return NULL;
            goal = petta_wrap_rel_conj(ctx, goal, rhs_goal);
            rhs_value = rhs_actual;
        } else {
            rhs_value = petta_lower_term(ctx, rhs_value);
        }

        Atom *cmp_args[] = { lhs_value, rhs_value };
        Atom *cmp_expr = make_expr_with_head(ctx->arena, head, cmp_args, 2);
        Atom *cmp_goal =
            petta_wrap_rel_call(ctx, cmp_expr, atom_unit(ctx->arena),
                                atom_true(ctx->arena));
        return petta_wrap_rel_conj(ctx, goal, cmp_goal);
    }

    uint32_t nargs = expr->expr.len - 1;
    if (!petta_head_is_callable(ctx, head, nargs))
        return NULL;
    const CettaPettaCallableInfo *info = petta_callable_info(ctx, head, nargs);
    if (!petta_relation_expr_prefers_goal_lowering(expr) &&
        !petta_term_needs_relation(expr, ctx->callables) &&
        !(info && info->goal_relation_lowering))
        return NULL;

    Atom *relation_call = petta_relation_call_expr(ctx, expr, atom_true(ctx->arena));
    return petta_wrap_rel_call(ctx,
                               expr,
                               relation_call ? relation_call : atom_unit(ctx->arena),
                               atom_true(ctx->arena));
}

static Atom *petta_lower_pattern_operand(CettaPettaLowerContext *ctx,
                                         Atom *pattern, Atom **body);
static PettaGuardedPattern petta_compile_guarded_pattern(
    CettaPettaLowerContext *ctx,
    Atom *pattern
);
static Atom *petta_apply_guarded_pattern(CettaPettaLowerContext *ctx,
                                         const PettaGuardedPattern *guarded,
                                         Atom *actual,
                                         Atom *body,
                                         PettaGuardedApplyMode mode);
static Atom *petta_lower_relation_against(CettaPettaLowerContext *ctx,
                                          Atom *expr,
                                          Atom *actual,
                                          Atom *body);
static Atom *petta_lower_relational_subterms(CettaPettaLowerContext *ctx,
                                             Atom *term);

static PettaGuardedPattern petta_compile_guarded_pattern(
    CettaPettaLowerContext *ctx,
    Atom *pattern
) {
    PettaGuardedPattern guarded = {
        .kind = PETTA_GUARDED_PATTERN_NONE,
        .skeleton = pattern,
        .guard_term = NULL,
        .children = NULL,
        .child_count = 0,
    };

    if (!pattern || pattern->kind != ATOM_EXPR || pattern->expr.len == 0) {
        return guarded;
    }

    Atom *head = pattern->expr.elems[0];
    if (atom_is_symbol_name(head, "quote") && pattern->expr.len == 2) {
        return guarded;
    }

    if (atom_is_symbol_name(head, "cons") && pattern->expr.len == 3) {
        PettaGuardedPattern *children =
            arena_alloc(ctx->arena, sizeof(PettaGuardedPattern) * 2);
        children[0] = petta_compile_guarded_pattern(ctx, pattern->expr.elems[1]);
        children[1] = petta_compile_guarded_pattern(ctx, pattern->expr.elems[2]);
        Atom *pair_elems[] = { children[0].skeleton, children[1].skeleton };
        guarded.kind = PETTA_GUARDED_PATTERN_CONS;
        guarded.skeleton = petta_fresh_var(ctx, "cons_pat");
        guarded.guard_term = atom_expr(ctx->arena, pair_elems, 2);
        guarded.children = children;
        guarded.child_count = 2;
        return guarded;
    }

    if (petta_head_is_callable(ctx, head, pattern->expr.len - 1)) {
        uint32_t nargs = pattern->expr.len - 1;
        Atom **args = arena_alloc(ctx->arena, sizeof(Atom *) * nargs);
        PettaGuardedPattern *children =
            nargs > 0
                ? arena_alloc(ctx->arena, sizeof(PettaGuardedPattern) * nargs)
                : NULL;
        for (uint32_t i = 0; i < nargs; i++) {
            children[i] = petta_compile_guarded_pattern(ctx, pattern->expr.elems[i + 1]);
            args[i] = children[i].skeleton;
        }
        guarded.kind = PETTA_GUARDED_PATTERN_CALLABLE;
        guarded.skeleton = petta_fresh_var(ctx, "pat");
        guarded.guard_term =
            make_expr_with_head(ctx->arena, pattern->expr.elems[0], args, nargs);
        guarded.children = children;
        guarded.child_count = nargs;
        return guarded;
    }

    if (!head || head->kind != ATOM_SYMBOL) {
        Atom **elems = arena_alloc(ctx->arena, sizeof(Atom *) * pattern->expr.len);
        PettaGuardedPattern *children =
            pattern->expr.len > 0
                ? arena_alloc(ctx->arena,
                              sizeof(PettaGuardedPattern) * pattern->expr.len)
                : NULL;
        for (uint32_t i = 0; i < pattern->expr.len; i++) {
            children[i] = petta_compile_guarded_pattern(ctx, pattern->expr.elems[i]);
            elems[i] = children[i].skeleton;
        }
        guarded.skeleton = atom_expr(ctx->arena, elems, pattern->expr.len);
        guarded.children = children;
        guarded.child_count = pattern->expr.len;
        return guarded;
    }

    Atom **elems = arena_alloc(ctx->arena, sizeof(Atom *) * pattern->expr.len);
    elems[0] = head;
    PettaGuardedPattern *children =
        pattern->expr.len > 1
            ? arena_alloc(ctx->arena,
                          sizeof(PettaGuardedPattern) * (pattern->expr.len - 1))
            : NULL;
    for (uint32_t i = 1; i < pattern->expr.len; i++) {
        children[i - 1] = petta_compile_guarded_pattern(ctx, pattern->expr.elems[i]);
        elems[i] = children[i - 1].skeleton;
    }
    guarded.skeleton = atom_expr(ctx->arena, elems, pattern->expr.len);
    guarded.children = children;
    guarded.child_count = pattern->expr.len > 0 ? pattern->expr.len - 1 : 0;
    return guarded;
}

static Atom *petta_apply_guarded_pattern(CettaPettaLowerContext *ctx,
                                         const PettaGuardedPattern *guarded,
                                         Atom *actual,
                                         Atom *body,
                                         PettaGuardedApplyMode mode) {
    if (!guarded) {
        return petta_wrap_let(ctx, actual, actual, body);
    }

    Atom *current = body;
    for (uint32_t i = 0; i < guarded->child_count; i++) {
        if (mode == PETTA_GUARDED_APPLY_RELATION &&
            guarded->kind == PETTA_GUARDED_PATTERN_CALLABLE &&
            guarded->children[i].kind == PETTA_GUARDED_PATTERN_CALLABLE &&
            petta_relation_call_expr(ctx, guarded->children[i].guard_term,
                                     guarded->children[i].skeleton) != NULL) {
            continue;
        }
        current = petta_apply_guarded_pattern(ctx,
                                              &guarded->children[i],
                                              guarded->children[i].skeleton,
                                              current,
                                              mode);
    }

    if (guarded->kind == PETTA_GUARDED_PATTERN_NONE) {
        if (actual != guarded->skeleton) {
            current = petta_wrap_let(ctx, guarded->skeleton, actual, current);
        }
    } else if (guarded->kind == PETTA_GUARDED_PATTERN_CONS) {
        Atom *pair_actual = petta_fresh_var(ctx, "cons_pair");
        Atom *miss = petta_miss_expr(ctx);
        Atom *decons_args[] = { actual };
        Atom *decons_call = make_expr_with_head(
            ctx->arena, atom_symbol_id(ctx->arena, g_builtin_syms.decons_atom),
            decons_args, 1);
        Atom *unify_args[] = {
            pair_actual,
            guarded->guard_term,
            current,
            miss,
        };
        Atom *unify_call = make_expr_with_head(
            ctx->arena,
            atom_symbol_id(ctx->arena, g_builtin_syms.unify),
            unify_args, 4);
        Atom *chain_args[] = { decons_call, pair_actual, unify_call };
        Atom *shape_guard = make_expr_with_head(
            ctx->arena, atom_symbol_id(ctx->arena, g_builtin_syms.chain),
            chain_args, 3);
        Atom *meta_args[] = { actual };
        Atom *meta = make_expr_with_head(ctx->arena,
                                         atom_symbol(ctx->arena, "get-metatype"),
                                         meta_args, 1);
        Atom *empty_cond_args[] = { actual, atom_unit(ctx->arena) };
        Atom *empty_cond = make_expr_with_head(
            ctx->arena, atom_symbol_id(ctx->arena, g_builtin_syms.alpha_eq),
            empty_cond_args, 2);
        Atom *empty_guard_args[] = { empty_cond, miss, shape_guard };
        Atom *empty_guard = make_expr_with_head(ctx->arena,
                                                atom_symbol(ctx->arena, "if"),
                                                empty_guard_args, 3);
        Atom *meta_cond_args[] = { meta, atom_expression_type(ctx->arena) };
        Atom *meta_cond = make_expr_with_head(
            ctx->arena, atom_symbol_id(ctx->arena, g_builtin_syms.alpha_eq),
            meta_cond_args, 2);
        Atom *guard_args[] = { meta_cond, empty_guard, miss };
        current = make_expr_with_head(ctx->arena,
                                      atom_symbol(ctx->arena, "if"),
                                      guard_args, 3);
    } else {
        if (mode == PETTA_GUARDED_APPLY_RELATION) {
            Atom *goal = petta_rel_goal_true(ctx);
            for (uint32_t i = 0; i < guarded->child_count; i++) {
                const PettaGuardedPattern *child = &guarded->children[i];
                if (child->kind != PETTA_GUARDED_PATTERN_CALLABLE)
                    continue;
                Atom *child_rel_call =
                    petta_relation_call_expr(ctx, child->guard_term, child->skeleton);
                if (!child_rel_call)
                    continue;
                goal = petta_wrap_rel_conj(ctx, goal,
                                           petta_wrap_rel_call(ctx,
                                                               child->guard_term,
                                                               child_rel_call,
                                                               child->skeleton));
            }
            Atom *rel_call = petta_relation_call_expr(ctx, guarded->guard_term, actual);
            if (rel_call) {
                goal = petta_wrap_rel_conj(ctx, goal,
                                           petta_wrap_rel_call(ctx,
                                                               guarded->guard_term,
                                                               rel_call,
                                                               actual));
                current = petta_wrap_rel_run(ctx, goal, current);
            } else {
                Atom *result_var = petta_fresh_var(ctx, "call_result");
                Atom *guarded_body =
                    petta_wrap_let(ctx, actual, result_var, current);
                Atom *chain_args[] = { guarded->guard_term, result_var, guarded_body };
                current = make_expr_with_head(
                    ctx->arena,
                    atom_symbol_id(ctx->arena, g_builtin_syms.chain),
                    chain_args, 3);
            }
        } else {
            Atom *result_var = petta_fresh_var(ctx, "call_result");
            Atom *guarded_body = petta_wrap_let(ctx, actual, result_var, current);
            Atom *chain_args[] = { guarded->guard_term, result_var, guarded_body };
            current = make_expr_with_head(
                ctx->arena,
                atom_symbol_id(ctx->arena, g_builtin_syms.chain),
                chain_args, 3);
        }
    }
    return current;
}

static Atom *petta_lower_pattern_operand(CettaPettaLowerContext *ctx,
                                         Atom *pattern, Atom **body) {
    PettaGuardedPattern guarded =
        petta_compile_guarded_pattern(ctx, pattern);
    *body = petta_apply_guarded_pattern(
        ctx, &guarded, guarded.skeleton, *body,
        PETTA_GUARDED_APPLY_RELATION);
    return guarded.skeleton;
}

static Atom *petta_lower_pattern_against(CettaPettaLowerContext *ctx,
                                         Atom *pattern, Atom *actual,
                                         Atom *body) {
    PettaGuardedPattern guarded =
        petta_compile_guarded_pattern(ctx, pattern);
    return petta_apply_guarded_pattern(ctx, &guarded, actual, body,
                                       PETTA_GUARDED_APPLY_RELATION);
}

static Atom *petta_lower_relation_let(CettaPettaLowerContext *ctx,
                                      Atom *pattern, Atom *value,
                                      Atom *body_expr,
                                      Atom *actual,
                                      Atom *body) {
    Atom *inner = petta_lower_relation_against(ctx, body_expr, actual, body);
    Atom *goal = NULL;
    if (petta_atom_is_true_literal(pattern) &&
        (goal = petta_compile_relation_goal_expr(ctx, value)) != NULL) {
        return petta_wrap_rel_run(ctx, goal, inner);
    }

    Atom *bound_value = petta_fresh_var(ctx, "rel_let_value");
    inner = petta_lower_pattern_against(ctx, pattern, bound_value, inner);
    if (petta_term_has_relation_form(value, ctx->callables)) {
        return petta_lower_relation_against(ctx, value, bound_value, inner);
    }
    return petta_wrap_let(ctx, bound_value, petta_lower_term(ctx, value), inner);
}

static Atom *petta_desugar_letstar(CettaPettaLowerContext *ctx,
                                   Atom *bindings,
                                   Atom *body) {
    if (!bindings || bindings->kind != ATOM_EXPR)
        return NULL;
    Atom *nested = body;
    for (uint32_t i = bindings->expr.len; i > 0; i--) {
        Atom *binding = bindings->expr.elems[i - 1];
        if (!binding || binding->kind != ATOM_EXPR || binding->expr.len != 2)
            return NULL;
        Atom *let_args[] = {
            binding->expr.elems[0],
            binding->expr.elems[1],
            nested,
        };
        nested = make_expr_with_head(ctx->arena,
                                     atom_symbol_id(ctx->arena, g_builtin_syms.let),
                                     let_args, 3);
    }
    return nested;
}

static Atom *petta_lower_relation_only(CettaPettaLowerContext *ctx,
                                       Atom *constraint_expr,
                                       Atom *value_expr,
                                       Atom *actual,
                                       Atom *body) {
    Atom *inner = petta_lower_relation_against(ctx, value_expr, actual, body);
    Atom *goal = petta_compile_relation_goal_expr(ctx, constraint_expr);
    if (!goal) {
        PettaGuardedPattern guarded =
            petta_compile_guarded_pattern(ctx, value_expr);
        return petta_apply_guarded_pattern(ctx, &guarded, actual, body,
                                           PETTA_GUARDED_APPLY_RELATION);
    }
    return petta_wrap_rel_run(ctx, goal, inner);
}

static Atom *petta_lower_only_term(CettaPettaLowerContext *ctx,
                                   const PettaHeadProfile *profile,
                                   Atom *const *args,
                                   uint32_t nargs) {
    if (!profile || nargs != 2)
        return NULL;
    Atom *goal = petta_compile_profile_goal_arg(ctx, profile, 0, args[0]);
    if (!goal) {
        return petta_lower_profile_call(
            ctx, atom_symbol(ctx->arena, profile->head_name), profile, args, nargs);
    }
    return petta_wrap_rel_run(ctx, goal,
                              petta_lower_profile_arg(ctx, profile, 1, args[1]));
}

static Atom *petta_lower_relation_against(CettaPettaLowerContext *ctx,
                                          Atom *expr,
                                          Atom *actual,
                                          Atom *body) {
    if (!expr || expr->kind != ATOM_EXPR || expr->expr.len == 0) {
        Atom *unify_args[] = {
            actual,
            petta_lower_term(ctx, expr),
            body,
            petta_miss_expr(ctx),
        };
        return make_expr_with_head(ctx->arena,
                                   atom_symbol_id(ctx->arena, g_builtin_syms.unify),
                                   unify_args, 4);
    }

    Atom *head = expr->expr.elems[0];
    uint32_t nargs = expr->expr.len - 1;
    Atom *const *args = expr->expr.elems + 1;

    if (atom_is_symbol_name(head, "quote") && nargs == 1) {
        return petta_wrap_let(ctx, expr, actual, body);
    }

    if (atom_is_symbol_name(head, "let") && nargs == 3) {
        return petta_lower_relation_let(ctx, args[0], args[1], args[2],
                                        actual, body);
    }

    if (atom_is_symbol_name(head, "let*") && nargs == 2) {
        Atom *desugared = petta_desugar_letstar(ctx, args[0], args[1]);
        if (desugared)
            return petta_lower_relation_against(ctx, desugared, actual, body);
    }

    if (atom_is_symbol_name(head, "if") && (nargs == 2 || nargs == 3)) {
        Atom *value_actual = petta_fresh_var(ctx, "rel_if_value");
        Atom *if_expr = petta_lower_term(ctx, expr);
        Atom *unify_args[] = {
            actual,
            value_actual,
            body,
            petta_miss_expr(ctx),
        };
        Atom *unify_body = make_expr_with_head(
            ctx->arena,
            atom_symbol_id(ctx->arena, g_builtin_syms.unify),
            unify_args, 4);
        return petta_wrap_let(ctx, value_actual, if_expr, unify_body);
    }

    if (atom_is_symbol_name(head, "once") && nargs == 1) {
        Atom *value_actual = petta_fresh_var(ctx, "rel_once_value");
        Atom *once_expr = petta_lower_term(ctx, expr);
        Atom *unify_args[] = {
            actual,
            value_actual,
            body,
            petta_miss_expr(ctx),
        };
        Atom *unify_body = make_expr_with_head(
            ctx->arena,
            atom_symbol_id(ctx->arena, g_builtin_syms.unify),
            unify_args, 4);
        return petta_wrap_let(ctx, value_actual, once_expr, unify_body);
    }

    if (atom_is_symbol_name(head, "match") && nargs == 3) {
        Atom *value_actual = petta_fresh_var(ctx, "rel_match_value");
        Atom *match_expr = petta_lower_term(ctx, expr);
        Atom *unify_args[] = {
            actual,
            value_actual,
            body,
            petta_miss_expr(ctx),
        };
        Atom *unify_body = make_expr_with_head(
            ctx->arena,
            atom_symbol_id(ctx->arena, g_builtin_syms.unify),
            unify_args, 4);
        return petta_wrap_let(ctx, value_actual, match_expr, unify_body);
    }

    if (atom_is_symbol_name(head, "only") && nargs == 2) {
        return petta_lower_relation_only(ctx, args[0], args[1], actual, body);
    }

    Atom *goal = NULL;
    if (petta_relation_expr_prefers_goal_lowering(expr)) {
        goal = petta_compile_relation_goal_expr(ctx, expr);
    }
    if (goal) {
        Atom *success_body_args[] = {
            actual,
            atom_true(ctx->arena),
            body,
            petta_miss_expr(ctx),
        };
        Atom *success_body = make_expr_with_head(
            ctx->arena,
            atom_symbol_id(ctx->arena, g_builtin_syms.unify),
            success_body_args, 4);
        return petta_wrap_rel_run(ctx, goal, success_body);
    }

    Atom *value_goal = petta_compile_relation_value_goal(ctx, expr, actual);
    if (value_goal) {
        return petta_wrap_rel_run(ctx, value_goal, body);
    }

    if (!goal) {
        goal = petta_compile_relation_goal_expr(ctx, expr);
    }
    if (goal) {
        Atom *success_body_args[] = {
            actual,
            atom_true(ctx->arena),
            body,
            petta_miss_expr(ctx),
        };
        Atom *success_body = make_expr_with_head(
            ctx->arena,
            atom_symbol_id(ctx->arena, g_builtin_syms.unify),
            success_body_args, 4);
        return petta_wrap_rel_run(ctx, goal, success_body);
    }

    PettaGuardedPattern guarded =
        petta_compile_guarded_pattern(ctx, expr);
    return petta_apply_guarded_pattern(ctx, &guarded, actual, body,
                                       PETTA_GUARDED_APPLY_RELATION);
}

static Atom *petta_lower_equation_decl(CettaPettaLowerContext *ctx, Atom *lhs,
                                       Atom *rhs) {
    PettaBoundVarSet syntax_params = {0};
    (void)petta_collect_rhs_syntax_param_vars(rhs, &syntax_params);
    SymbolId saved_suppressed_head = ctx->suppressed_recursive_value_head;
    uint32_t saved_suppressed_arity = ctx->suppressed_recursive_value_arity;
    if (!lhs || lhs->kind != ATOM_EXPR || lhs->expr.len == 0) {
        Atom *lowered_rhs = petta_lower_decl_body(ctx, rhs);
        petta_bound_var_set_free(&syntax_params);
        return atom_expr3(ctx->arena,
                          atom_symbol_id(ctx->arena, g_builtin_syms.equals),
                          lhs, lowered_rhs);
    }

    Atom *head = lhs->expr.elems[0];
    uint32_t nargs = lhs->expr.len - 1;
    if (head && head->kind == ATOM_SYMBOL) {
        ctx->suppressed_recursive_value_head = head->sym_id;
        ctx->suppressed_recursive_value_arity = nargs;
    } else {
        ctx->suppressed_recursive_value_head = SYMBOL_ID_NONE;
        ctx->suppressed_recursive_value_arity = 0;
    }
    Atom *lowered_rhs = petta_lower_decl_body(ctx, rhs);
    ctx->suppressed_recursive_value_head = saved_suppressed_head;
    ctx->suppressed_recursive_value_arity = saved_suppressed_arity;
    Atom **lowered_args = arena_alloc(ctx->arena, sizeof(Atom *) * nargs);
    Atom *body = lowered_rhs;
    for (uint32_t i = nargs; i > 0; i--) {
        Atom *actual = petta_fresh_var(ctx, "arg");
        Atom *pattern = lhs->expr.elems[i];
        if (pattern &&
            pattern->kind == ATOM_VAR &&
            petta_bound_var_set_contains(&syntax_params, pattern->var_id)) {
            lowered_args[i - 1] = pattern;
        } else {
            lowered_args[i - 1] = actual;
            body = petta_lower_pattern_against(ctx, pattern, actual, body);
        }
    }
    Atom *lowered_lhs = make_expr_with_head(ctx->arena, head, lowered_args, nargs);
    petta_bound_var_set_free(&syntax_params);
    return atom_expr3(ctx->arena,
                      atom_symbol_id(ctx->arena, g_builtin_syms.equals),
                      lowered_lhs, body);
}

static Atom *petta_lower_relation_equation_decl(CettaPettaLowerContext *ctx,
                                                Atom *lhs,
                                                Atom *rhs) {
    if (!lhs || lhs->kind != ATOM_EXPR || lhs->expr.len == 0) {
        return NULL;
    }

    Atom *head = lhs->expr.elems[0];
    uint32_t nargs = lhs->expr.len - 1;
    const CettaPettaCallableInfo *info = petta_callable_info(ctx, head, nargs);
    if (!info)
        return NULL;

    Atom **lowered_args = arena_alloc(ctx->arena, sizeof(Atom *) * (nargs + 2));
    PettaGuardedPattern *lhs_patterns =
        nargs > 0
            ? arena_alloc(ctx->arena, sizeof(PettaGuardedPattern) * nargs)
            : NULL;
    Atom *out_actual = petta_fresh_var(ctx, "rel_out");
    lowered_args[nargs] = out_actual;
    Atom *body = NULL;
    for (uint32_t i = 0; i < nargs; i++) {
        lhs_patterns[i] = petta_compile_guarded_pattern(ctx, lhs->expr.elems[i + 1]);
        lowered_args[i] = lhs_patterns[i].skeleton;
    }
    Atom *export_pack = petta_export_pack(ctx, lowered_args, nargs);
    lowered_args[nargs + 1] = export_pack;
    body = petta_rel_result_pack(ctx, out_actual, export_pack);
    body = petta_lower_relation_against(ctx, rhs, out_actual, body);
    for (uint32_t i = nargs; i > 0; i--) {
        body = petta_apply_guarded_pattern(ctx, &lhs_patterns[i - 1],
                                           lowered_args[i - 1], body,
                                           PETTA_GUARDED_APPLY_RELATION);
    }

    Atom *rel_head = atom_symbol_id(ctx->arena, info->relation_head_id);
    Atom *lowered_lhs = make_expr_with_head(ctx->arena, rel_head,
                                            lowered_args, nargs + 2);
    return atom_expr3(ctx->arena,
                      atom_symbol_id(ctx->arena, g_builtin_syms.equals),
                      lowered_lhs, body);
}

static Atom *petta_rel_append_base_decl(CettaPettaLowerContext *ctx) {
    Atom *lhs_actual = petta_fresh_var(ctx, "rel_append_lhs");
    Atom *rhs_actual = petta_fresh_var(ctx, "rel_append_rhs");
    Atom *out_actual = petta_fresh_var(ctx, "rel_append_out");
    Atom *export_items[] = { lhs_actual, rhs_actual };
    Atom *export_pack = petta_export_pack(ctx, export_items, 2);
    Atom *args[] = {
        lhs_actual,
        rhs_actual,
        out_actual,
        export_pack,
    };
    Atom *lhs = make_expr_with_head(
        ctx->arena,
        atom_symbol_id(ctx->arena,
                       cetta_petta_builtin_relation_head_id(
                           ctx->arena, CETTA_PETTA_BUILTIN_REL_APPEND)),
        args, 4);
    Atom *body = petta_rel_result_pack(ctx, out_actual, export_pack);
    body = petta_wrap_let(ctx, rhs_actual, out_actual, body);
    body = petta_wrap_let(ctx, atom_unit(ctx->arena), lhs_actual, body);
    return atom_expr3(ctx->arena,
                      atom_symbol_id(ctx->arena, g_builtin_syms.equals),
                      lhs, body);
}

static Atom *petta_rel_append_step_decl(CettaPettaLowerContext *ctx) {
    Atom *lhs_actual = petta_fresh_var(ctx, "rel_append_lhs");
    Atom *rhs_actual = petta_fresh_var(ctx, "rel_append_rhs");
    Atom *out_actual = petta_fresh_var(ctx, "rel_append_out");
    Atom *head_var = petta_fresh_var(ctx, "rel_append_head");
    Atom *tail_var = petta_fresh_var(ctx, "rel_append_tail");
    Atom *rest_var = petta_fresh_var(ctx, "rel_append_rest");
    Atom *rest_export = petta_fresh_var(ctx, "rel_append_rest_export");

    Atom *lhs_export_items[] = { lhs_actual, rhs_actual };
    Atom *lhs_export_pack = petta_export_pack(ctx, lhs_export_items, 2);
    Atom *lhs_args[] = {
        lhs_actual,
        rhs_actual,
        out_actual,
        lhs_export_pack,
    };
    Atom *lhs = make_expr_with_head(
        ctx->arena,
        atom_symbol_id(ctx->arena,
                       cetta_petta_builtin_relation_head_id(
                           ctx->arena, CETTA_PETTA_BUILTIN_REL_APPEND)),
        lhs_args, 4);

    Atom *rec_export_items[] = { tail_var, rhs_actual };
    Atom *rec_export_pack = petta_export_pack(ctx, rec_export_items, 2);
    Atom *rec_args[] = {
        tail_var,
        rhs_actual,
        rest_var,
        rec_export_pack,
    };
    Atom *rec_call = make_expr_with_head(
        ctx->arena,
        atom_symbol_id(ctx->arena,
                       cetta_petta_builtin_relation_head_id(
                           ctx->arena, CETTA_PETTA_BUILTIN_REL_APPEND)),
        rec_args, 4);
    Atom *rec_pattern_items[] = { rest_var, rest_export };
    Atom *rec_pattern = atom_expr(ctx->arena, rec_pattern_items, 2);
    Atom *body = petta_rel_result_pack(ctx, out_actual, lhs_export_pack);
    body = petta_wrap_let(ctx, rec_pattern, rec_call, body);

    Atom *out_cons_elems[] = {
        atom_symbol(ctx->arena, "cons"),
        head_var,
        rest_var,
    };
    body = petta_lower_relation_against(
        ctx, atom_expr(ctx->arena, out_cons_elems, 3), out_actual, body);

    Atom *lhs_cons_elems[] = {
        atom_symbol(ctx->arena, "cons"),
        head_var,
        tail_var,
    };
    body = petta_lower_relation_against(
        ctx, atom_expr(ctx->arena, lhs_cons_elems, 3), lhs_actual, body);

    return atom_expr3(ctx->arena,
                      atom_symbol_id(ctx->arena, g_builtin_syms.equals),
                      lhs, body);
}

static Atom *petta_lower_progn(CettaPettaLowerContext *ctx, Atom *const *args,
                               uint32_t nargs) {
    if (nargs == 0) return atom_unit(ctx->arena);
    Atom *tail = petta_lower_term(ctx, args[nargs - 1]);
    for (uint32_t i = nargs - 1; i > 0; i--) {
        Atom *discard = petta_fresh_var(ctx, "discard");
        Atom *prefix = petta_lower_term(ctx, args[i - 1]);
        Atom *let_args[] = { discard, prefix, tail };
        tail = make_expr_with_head(ctx->arena,
                                   atom_symbol_id(ctx->arena, g_builtin_syms.let),
                                   let_args, 3);
    }
    return tail;
}

static Atom *petta_lower_prog1(CettaPettaLowerContext *ctx, Atom *const *args,
                               uint32_t nargs) {
    if (nargs == 0) return atom_unit(ctx->arena);
    if (nargs == 1) return petta_lower_term(ctx, args[0]);

    Atom *result_var = petta_fresh_var(ctx, "result");
    Atom *tail = result_var;
    for (uint32_t i = nargs; i > 1; i--) {
        Atom *discard = petta_fresh_var(ctx, "discard");
        Atom *expr = petta_lower_term(ctx, args[i - 1]);
        Atom *let_args[] = { discard, expr, tail };
        tail = make_expr_with_head(ctx->arena,
                                   atom_symbol_id(ctx->arena, g_builtin_syms.let),
                                   let_args, 3);
    }

    Atom *first = petta_lower_term(ctx, args[0]);
    Atom *top_args[] = { result_var, first, tail };
    return make_expr_with_head(ctx->arena,
                               atom_symbol_id(ctx->arena, g_builtin_syms.let),
                               top_args, 3);
}

static Atom *petta_lower_foldall(CettaPettaLowerContext *ctx, Atom *agg,
                                 Atom *goal, Atom *init) {
    Atom *list_var = petta_fresh_var(ctx, "collapsed");
    Atom *acc_var = petta_fresh_var(ctx, "acc");
    Atom *item_var = petta_fresh_var(ctx, "item");
    Atom *t_agg = petta_lower_term(ctx, agg);
    Atom *t_init = petta_lower_term(ctx, init);
    Atom *collector = petta_lower_collapse_call(ctx, goal);

    Atom *agg_call_args[] = { acc_var, item_var };
    Atom *agg_call = make_expr_with_head(ctx->arena, t_agg, agg_call_args, 2);
    Atom *eval_args[] = { agg_call };
    Atom *eval_call = make_expr_with_head(ctx->arena,
                                          atom_symbol_id(ctx->arena, g_builtin_syms.eval),
                                          eval_args, 1);

    Atom *fold_args[] = { list_var, t_init, acc_var, item_var, eval_call };
    Atom *fold_call = make_expr_with_head(ctx->arena,
                                          atom_symbol_id(ctx->arena, g_builtin_syms.foldl_atom),
                                          fold_args, 5);

    Atom *let_args[] = { list_var, collector, fold_call };
    return make_expr_with_head(ctx->arena,
                               atom_symbol_id(ctx->arena, g_builtin_syms.let),
                               let_args, 3);
}

static Atom *petta_lower_short_foldl(CettaPettaLowerContext *ctx, Atom *list,
                                     Atom *init, Atom *agg) {
    Atom *acc_var = petta_fresh_var(ctx, "acc");
    Atom *item_var = petta_fresh_var(ctx, "item");
    Atom *t_list = petta_lower_term(ctx, list);
    Atom *t_init = petta_lower_term(ctx, init);
    Atom *t_agg = petta_lower_term(ctx, agg);

    Atom *agg_call_args[] = { acc_var, item_var };
    Atom *agg_call = make_expr_with_head(ctx->arena, t_agg, agg_call_args, 2);
    Atom *eval_args[] = { agg_call };
    Atom *eval_call = make_expr_with_head(ctx->arena,
                                          atom_symbol_id(ctx->arena, g_builtin_syms.eval),
                                          eval_args, 1);
    Atom *fold_args[] = { t_list, t_init, acc_var, item_var, eval_call };
    return make_expr_with_head(ctx->arena,
                               atom_symbol_id(ctx->arena, g_builtin_syms.foldl_atom),
                               fold_args, 5);
}

static Atom *petta_lower_length(CettaPettaLowerContext *ctx, Atom *arg) {
    Atom *t_arg = petta_lower_term(ctx, arg);
    if (expr_head_is_name(arg, "collapse") && arg->expr.len == 2) {
        Atom *tuple_var = petta_fresh_var(ctx, "tuple");
        Atom *collapse_call = petta_lower_collapse_call(ctx, arg->expr.elems[1]);
        Atom *size_args[] = { tuple_var };
        Atom *size_call = make_expr_with_head(
            ctx->arena, atom_symbol_id(ctx->arena, g_builtin_syms.size_atom),
            size_args, 1);
        Atom *let_args[] = { tuple_var, collapse_call, size_call };
        return make_expr_with_head(ctx->arena,
                                   atom_symbol_id(ctx->arena, g_builtin_syms.let),
                                   let_args, 3);
    }
    Atom *length_args[] = { t_arg };
    return make_expr_with_head(ctx->arena, atom_symbol(ctx->arena, "length"),
                               length_args, 1);
}

static bool petta_case_empty_default_branch(Atom *branch, Atom **default_expr_out) {
    if (!branch || branch->kind != ATOM_EXPR || branch->expr.len != 2)
        return false;
    if (!atom_is_symbol_id(branch->expr.elems[0], g_builtin_syms.petta_empty_literal))
        return false;
    if (default_expr_out)
        *default_expr_out = branch->expr.elems[1];
    return true;
}

static Atom *petta_lower_case_branch(CettaPettaLowerContext *ctx, Atom *branch) {
    if (!branch || branch->kind != ATOM_EXPR || branch->expr.len != 2) {
        return petta_lower_term(ctx, branch);
    }

    Atom *body = petta_lower_term(ctx, branch->expr.elems[1]);
    Atom *pattern = petta_lower_pattern_operand(ctx, branch->expr.elems[0], &body);
    Atom *branch_elems[] = { pattern, body };
    return atom_expr(ctx->arena, branch_elems, 2);
}

static Atom *petta_lower_if(CettaPettaLowerContext *ctx,
                            const PettaHeadProfile *profile,
                            Atom *const *args,
                            uint32_t nargs) {
    if (!profile || (nargs != 2 && nargs != 3))
        return NULL;
    Atom *cond_expr = args[0];
    Atom *then_expr = args[1];
    Atom *else_expr = nargs == 3 ? args[2] : NULL;
    bool cond_is_once = expr_head_is_name(cond_expr, "once") &&
                        cond_expr->kind == ATOM_EXPR &&
                        cond_expr->expr.len == 2;
    Atom *goal = cond_is_once
        ? petta_compile_profile_goal_arg(ctx, profile, 0, cond_expr->expr.elems[1])
        : petta_compile_profile_goal_arg(ctx, profile, 0, cond_expr);
    Atom *then_run = NULL;
    if (goal) {
        then_run = petta_wrap_rel_run(
            ctx, goal, petta_lower_profile_arg(ctx, profile, 1, then_expr));
        if (cond_is_once) {
            Atom *once_args[] = { then_run };
            then_run = make_expr_with_head(
                ctx->arena,
                atom_symbol_id(ctx->arena, g_builtin_syms.once),
                once_args, 1);
        }
    } else if (petta_term_needs_relation(cond_expr, ctx->callables)) {
        Atom *cond_actual = petta_fresh_var(ctx, "if_cond");
        Atom *value_goal =
            petta_compile_relation_value_goal(ctx, cond_expr, cond_actual);
        if (!value_goal)
            return NULL;
        then_run =
            petta_wrap_rel_run(
                ctx, value_goal, petta_lower_profile_arg(ctx, profile, 1, then_expr));
    } else {
        if (nargs == 2) {
            Atom *if_args[] = {
                petta_lower_profile_arg(ctx, profile, 0, cond_expr),
                petta_lower_profile_arg(ctx, profile, 1, then_expr),
                petta_miss_expr(ctx),
            };
            return make_expr_with_head(ctx->arena,
                                       atom_symbol(ctx->arena, "if"),
                                       if_args, 3);
        }
        return petta_lower_profile_call(
            ctx, atom_symbol(ctx->arena, profile->head_name), profile, args, nargs);
    }

    Atom *then_tuple = petta_fresh_var(ctx, "if_tuple");
    Atom *collapse_args[] = { then_run };
    Atom *collapsed = make_expr_with_head(
        ctx->arena,
        atom_symbol_id(ctx->arena, g_builtin_syms.collapse),
        collapse_args, 1);

    Atom *size_args[] = { then_tuple };
    Atom *tuple_size = make_expr_with_head(
        ctx->arena,
        atom_symbol_id(ctx->arena, g_builtin_syms.size_atom),
        size_args, 1);
    Atom *has_results_args[] = { tuple_size, atom_int(ctx->arena, 0) };
    Atom *has_results = make_expr_with_head(
        ctx->arena,
        atom_symbol_id(ctx->arena, g_builtin_syms.op_gt),
        has_results_args, 2);

    Atom *superpose_args[] = { then_tuple };
    Atom *then_value = make_expr_with_head(
        ctx->arena,
        atom_symbol_id(ctx->arena, g_builtin_syms.superpose),
        superpose_args, 1);
    Atom *if_args[] = {
        has_results,
        then_value,
        else_expr ? petta_lower_profile_arg(ctx, profile, 2, else_expr)
                  : petta_miss_expr(ctx),
    };
    Atom *lowered_if = make_expr_with_head(
        ctx->arena,
        atom_symbol(ctx->arena, "if"),
        if_args, 3);
    return petta_wrap_let(ctx, then_tuple, collapsed, lowered_if);
}

static Atom *petta_lower_let(CettaPettaLowerContext *ctx,
                             const PettaHeadProfile *profile,
                             Atom *const *args,
                             uint32_t nargs) {
    if (!profile || nargs != 3)
        return NULL;
    Atom *pattern = args[0];
    Atom *value = args[1];
    Atom *body_expr = args[2];
    Atom *bound_value = petta_fresh_var(ctx, "let_value");
    Atom *body = petta_lower_profile_arg(ctx, profile, 2, body_expr);
    Atom *goal = NULL;
    if (petta_atom_is_true_literal(pattern) &&
        value &&
        value->kind == ATOM_EXPR &&
        value->expr.len == 3 &&
        atom_is_symbol_name(value->expr.elems[0], ">") &&
        petta_term_has_relation_form(value->expr.elems[1], ctx->callables)) {
        Atom *lhs_actual = petta_fresh_var(ctx, "let_goal_lhs");
        Atom *lhs_goal =
            petta_compile_relation_value_goal(ctx, value->expr.elems[1], lhs_actual);
        if (lhs_goal) {
            Atom *cmp_args[] = {
                lhs_actual,
                petta_lower_value(ctx, value->expr.elems[2]),
            };
            Atom *cmp_expr =
                make_expr_with_head(ctx->arena, value->expr.elems[0], cmp_args, 2);
            Atom *cmp_goal =
                petta_wrap_rel_call(ctx, cmp_expr, atom_unit(ctx->arena),
                                    atom_true(ctx->arena));
            return petta_wrap_rel_run(ctx,
                                      petta_wrap_rel_conj(ctx, lhs_goal, cmp_goal),
                                      body);
        }
    }
    if (petta_atom_is_true_literal(pattern) &&
        (goal = petta_compile_relation_goal_expr(ctx, value)) != NULL) {
        return petta_wrap_rel_run(ctx, goal, body);
    }
    body = petta_lower_pattern_against(ctx, pattern, bound_value, body);
    return petta_wrap_let(ctx, bound_value,
                          petta_lower_profile_arg(ctx, profile, 1, value), body);
}

static Atom *petta_lower_case(CettaPettaLowerContext *ctx,
                              const PettaHeadProfile *profile,
                              Atom *const *args,
                              uint32_t nargs) {
    if (!profile || nargs != 2)
        return NULL;
    Atom *key = args[0];
    Atom *branches = args[1];
    Atom *default_expr = NULL;
    uint32_t default_index = UINT32_MAX;

    if (!branches || branches->kind != ATOM_EXPR) {
        Atom *case_args[] = {
            petta_lower_profile_arg(ctx, profile, 0, key),
            petta_lower_value(ctx, branches),
        };
        return make_expr_with_head(ctx->arena,
                                   atom_symbol_id(ctx->arena, g_builtin_syms.case_text),
                                   case_args, 2);
    }

    for (uint32_t i = 0; i < branches->expr.len; i++) {
        if (petta_case_empty_default_branch(branches->expr.elems[i], &default_expr)) {
            default_index = i;
            break;
        }
    }

    if (!default_expr) {
        Atom **lowered_branches =
            arena_alloc(ctx->arena, sizeof(Atom *) * branches->expr.len);
        for (uint32_t i = 0; i < branches->expr.len; i++) {
            lowered_branches[i] =
                petta_lower_case_branch(ctx, branches->expr.elems[i]);
        }
        Atom *case_args[] = {
            petta_lower_profile_arg(ctx, profile, 0, key),
            atom_expr(ctx->arena, lowered_branches, branches->expr.len),
        };
        return make_expr_with_head(ctx->arena,
                                   atom_symbol_id(ctx->arena, g_builtin_syms.case_text),
                                   case_args, 2);
    }

    Atom **normal_branches =
        arena_alloc(ctx->arena, sizeof(Atom *) * branches->expr.len);
    uint32_t normal_len = 0;
    for (uint32_t i = 0; i < branches->expr.len; i++) {
        if (i == default_index)
            continue;
        normal_branches[normal_len++] =
            petta_lower_case_branch(ctx, branches->expr.elems[i]);
    }

    Atom *results_var = petta_fresh_var(ctx, "case_results");
    Atom *item_var = petta_fresh_var(ctx, "case_item");
    Atom *collapse_args[] = { petta_lower_profile_arg(ctx, profile, 0, key) };
    Atom *collapsed = make_expr_with_head(
        ctx->arena, atom_symbol_id(ctx->arena, g_builtin_syms.collapse),
        collapse_args, 1);

    Atom *inner_case_args[] = {
        item_var,
        atom_expr(ctx->arena, normal_branches, normal_len),
    };
    Atom *inner_case = make_expr_with_head(
        ctx->arena, atom_symbol_id(ctx->arena, g_builtin_syms.case_text),
        inner_case_args, 2);

    Atom *superpose_args[] = { results_var };
    Atom *iter = petta_wrap_let(
        ctx, item_var,
        make_expr_with_head(
            ctx->arena, atom_symbol_id(ctx->arena, g_builtin_syms.superpose),
            superpose_args, 1),
        inner_case);

    Atom *empty_branch_elems[] = {
        atom_unit(ctx->arena),
        petta_lower_value(ctx, default_expr),
    };
    Atom *nonempty_branch_elems[] = { results_var, iter };
    Atom *outer_branches_elems[] = {
        atom_expr(ctx->arena, empty_branch_elems, 2),
        atom_expr(ctx->arena, nonempty_branch_elems, 2),
    };
    Atom *outer_case_args[] = {
        results_var,
        atom_expr(ctx->arena, outer_branches_elems, 2),
    };
    Atom *outer_case = make_expr_with_head(
        ctx->arena, atom_symbol_id(ctx->arena, g_builtin_syms.case_text),
        outer_case_args, 2);
    return petta_wrap_let(ctx, results_var, collapsed, outer_case);
}

static Atom *petta_lower_once(CettaPettaLowerContext *ctx,
                              const PettaHeadProfile *profile,
                              Atom *const *args,
                              uint32_t nargs) {
    if (!profile || nargs != 1)
        return NULL;
    Atom *once_args[] = { petta_lower_profile_arg(ctx, profile, 0, args[0]) };
    return make_expr_with_head(ctx->arena,
                               atom_symbol_id(ctx->arena, g_builtin_syms.once),
                               once_args, 1);
}

static Atom *petta_lower_profile_special_call(CettaPettaLowerContext *ctx,
                                              const PettaHeadProfile *profile,
                                              Atom *const *args,
                                              uint32_t nargs) {
    if (!profile || profile->arity != nargs)
        return NULL;
    switch (profile->special) {
    case PETTA_HEAD_SPECIAL_IF:
        return petta_lower_if(ctx, profile, args, nargs);
    case PETTA_HEAD_SPECIAL_LET:
        return petta_lower_let(ctx, profile, args, nargs);
    case PETTA_HEAD_SPECIAL_CASE:
        return petta_lower_case(ctx, profile, args, nargs);
    case PETTA_HEAD_SPECIAL_ONLY:
        return petta_lower_only_term(ctx, profile, args, nargs);
    case PETTA_HEAD_SPECIAL_ONCE:
        return petta_lower_once(ctx, profile, args, nargs);
    case PETTA_HEAD_SPECIAL_MATCH:
        return petta_lower_profile_call(
            ctx, atom_symbol_id(ctx->arena, g_builtin_syms.match),
            profile, args, nargs);
    case PETTA_HEAD_SPECIAL_ADD_ATOM:
        return petta_lower_profile_call(
            ctx, atom_symbol_id(ctx->arena, g_builtin_syms.add_atom),
            profile, args, nargs);
    case PETTA_HEAD_SPECIAL_ADD_ATOM_NODUP:
        return petta_lower_profile_call(
            ctx, atom_symbol_id(ctx->arena, g_builtin_syms.add_atom_nodup),
            profile, args, nargs);
    case PETTA_HEAD_SPECIAL_ADD_REDUCT:
        return petta_lower_profile_call(
            ctx, atom_symbol_id(ctx->arena, g_builtin_syms.add_reduct),
            profile, args, nargs);
    case PETTA_HEAD_SPECIAL_REMOVE_ATOM:
        return petta_lower_profile_call(
            ctx, atom_symbol_id(ctx->arena, g_builtin_syms.remove_atom),
            profile, args, nargs);
    case PETTA_HEAD_SPECIAL_NONE:
        return NULL;
    }
    return NULL;
}

static Atom *petta_lower_relation_goal_bool(CettaPettaLowerContext *ctx,
                                            Atom *goal_expr) {
    Atom *goal = petta_compile_relation_goal_expr(ctx, goal_expr);
    if (!goal)
        return NULL;

    Atom *results_var = petta_fresh_var(ctx, "goal_bool_results");
    Atom *collapse_args[] = {
        petta_wrap_rel_run(ctx, goal, atom_true(ctx->arena))
    };
    Atom *collapsed = make_expr_with_head(
        ctx->arena,
        atom_symbol_id(ctx->arena, g_builtin_syms.collapse),
        collapse_args, 1);
    Atom *size_args[] = { results_var };
    Atom *tuple_size = make_expr_with_head(
        ctx->arena,
        atom_symbol_id(ctx->arena, g_builtin_syms.size_atom),
        size_args, 1);
    Atom *has_results_args[] = { tuple_size, atom_int(ctx->arena, 0) };
    Atom *has_results = make_expr_with_head(
        ctx->arena,
        atom_symbol_id(ctx->arena, g_builtin_syms.op_gt),
        has_results_args, 2);
    Atom *if_args[] = {
        has_results,
        atom_true(ctx->arena),
        atom_false(ctx->arena),
    };
    Atom *bool_expr = make_expr_with_head(
        ctx->arena,
        atom_symbol(ctx->arena, "if"),
        if_args, 3);
    return petta_wrap_let(ctx, results_var, collapsed, bool_expr);
}

static Atom *petta_lower_relational_subterms(CettaPettaLowerContext *ctx,
                                             Atom *term) {
    if (!term || term->kind != ATOM_EXPR || term->expr.len == 0)
        return term;
    if (expr_head_is_name(term, "quote") && term->expr.len == 2)
        return term;

    Atom *goal = petta_rel_goal_true(ctx);
    bool changed = false;

    if (term->expr.elems[0] &&
        term->expr.elems[0]->kind == ATOM_SYMBOL) {
        Atom *head = term->expr.elems[0];
        uint32_t nargs = term->expr.len - 1;
        Atom **lowered_args = arena_alloc(ctx->arena, sizeof(Atom *) * nargs);
        for (uint32_t i = 0; i < nargs; i++) {
            Atom *child = term->expr.elems[i + 1];
            if (child &&
                child->kind == ATOM_EXPR &&
                petta_may_lower_relation_subterm(ctx, child) &&
                petta_term_contains_vars(child) &&
                petta_term_needs_relation(child, ctx->callables)) {
                Atom *actual = petta_fresh_var(ctx, "value");
                Atom *child_goal =
                    petta_compile_relation_value_goal(ctx, child, actual);
                if (child_goal) {
                    goal = petta_wrap_rel_conj(ctx, goal, child_goal);
                    lowered_args[i] = actual;
                    changed = true;
                    continue;
                }
            }
            lowered_args[i] = petta_lower_term(ctx, child);
            changed = changed || lowered_args[i] != child;
        }
        if (!changed)
            return term;
        Atom *body = make_expr_with_head(ctx->arena, head, lowered_args, nargs);
        if (!petta_rel_goal_is_true(goal))
            return petta_wrap_rel_run(ctx, goal, body);
        return body;
    }

    Atom **elems = arena_alloc(ctx->arena, sizeof(Atom *) * term->expr.len);
    for (uint32_t i = 0; i < term->expr.len; i++) {
        Atom *child = term->expr.elems[i];
        if (child &&
            child->kind == ATOM_EXPR &&
            petta_may_lower_relation_subterm(ctx, child) &&
            petta_term_contains_vars(child) &&
            petta_term_needs_relation(child, ctx->callables)) {
            Atom *actual = petta_fresh_var(ctx, "value");
            Atom *child_goal =
                petta_compile_relation_value_goal(ctx, child, actual);
            if (child_goal) {
                goal = petta_wrap_rel_conj(ctx, goal, child_goal);
                elems[i] = actual;
                changed = true;
                continue;
            }
        }
        elems[i] = petta_lower_term(ctx, child);
        changed = changed || elems[i] != child;
    }
    if (!changed)
        return term;
    Atom *body = atom_expr(ctx->arena, elems, term->expr.len);
    if (!petta_rel_goal_is_true(goal))
        return petta_wrap_rel_run(ctx, goal, body);
    return body;
}

static Atom *petta_lower_superpose_literal(CettaPettaLowerContext *ctx,
                                           Atom *branches) {
    Atom *superpose_args[] = { branches };
    return make_expr_with_head(ctx->arena,
                               atom_symbol_id(ctx->arena, g_builtin_syms.superpose),
                               superpose_args, 1);
}

static Atom *petta_lower_value(CettaPettaLowerContext *ctx, Atom *term) {
    if (!term) return term;
    if (term->kind == ATOM_SYMBOL) {
        if (atom_is_symbol_name(term, "true")) return atom_true(ctx->arena);
        if (atom_is_symbol_name(term, "false")) return atom_false(ctx->arena);
        return term;
    }
    if (term->kind != ATOM_EXPR || term->expr.len == 0) return term;

    Atom *head = term->expr.elems[0];
    uint32_t nargs = term->expr.len - 1;
    Atom *const *args = term->expr.elems + 1;
    const PettaHeadProfile *profile = petta_head_profile(head, nargs);
    const CettaPettaCallableInfo *callable_info =
        petta_callable_info(ctx, head, nargs);

    if (atom_is_symbol_name(head, "quote") && nargs == 1) {
        return term;
    }

    if (atom_is_symbol_name(head, "empty") && nargs == 0) {
        return petta_miss_expr(ctx);
    }

    if (atom_is_symbol_name(head, "=") && nargs == 2) {
        Atom *lowered_eq = petta_lower_relation_goal_bool(ctx, term);
        if (lowered_eq)
            return lowered_eq;
    }

    if (atom_is_symbol_name(head, "and") && nargs == 2) {
        Atom *lowered_and = petta_lower_relation_goal_bool(ctx, term);
        if (lowered_and)
            return lowered_and;
    }

    if (atom_is_symbol_name(head, ":") && nargs == 2) {
        return term;
    }

    if (atom_is_symbol_name(head, "progn")) {
        return petta_lower_progn(ctx, args, nargs);
    }

    if (atom_is_symbol_name(head, "prog1")) {
        return petta_lower_prog1(ctx, args, nargs);
    }

    if (profile && profile->special != PETTA_HEAD_SPECIAL_NONE) {
        Atom *lowered_special =
            petta_lower_profile_special_call(ctx, profile, args, nargs);
        if (lowered_special)
            return lowered_special;
    }

    if (atom_is_symbol_name(head, "let*") && nargs == 2) {
        Atom *desugared = petta_desugar_letstar(ctx, args[0], args[1]);
        if (desugared)
            return petta_lower_term(ctx, desugared);
    }

    if (atom_is_symbol_name(head, "foldall") && nargs == 3) {
        return petta_lower_foldall(ctx, args[0], args[1], args[2]);
    }

    if (atom_is_symbol_name(head, "foldl-atom") && nargs == 3) {
        return petta_lower_short_foldl(ctx, args[0], args[1], args[2]);
    }

    if (atom_is_symbol_name(head, "reduce") && nargs == 1) {
        Atom *eval_args[] = { petta_lower_term(ctx, args[0]) };
        return make_expr_with_head(ctx->arena,
                                   atom_symbol_id(ctx->arena, g_builtin_syms.eval),
                                   eval_args, 1);
    }

    if (atom_is_symbol_name(head, "collapse") && nargs == 1 &&
        args[0] &&
        args[0]->kind == ATOM_EXPR &&
        args[0]->expr.len > 0 &&
        args[0]->expr.elems[0] &&
        args[0]->expr.elems[0]->kind == ATOM_SYMBOL &&
        petta_head_is_callable(ctx, args[0]->expr.elems[0], args[0]->expr.len - 1) &&
        petta_term_contains_vars(args[0])) {
        Atom *actual = petta_fresh_var(ctx, "collapse_value");
        Atom *goal = petta_compile_relation_value_goal(ctx, args[0], actual);
        if (goal) {
            Atom *collapse_args[] = {
                petta_wrap_rel_run(ctx, goal, actual)
            };
            return make_expr_with_head(ctx->arena,
                                       atom_symbol_id(ctx->arena, g_builtin_syms.collapse),
                                       collapse_args, 1);
        }
    }

    if (atom_is_symbol_name(head, "length") && nargs == 1) {
        return petta_lower_length(ctx, args[0]);
    }

    if (atom_is_symbol_name(head, "unique-atom") &&
        nargs == 1 &&
        expr_head_is_name(args[0], "collapse") &&
        args[0]->expr.len == 2) {
        Atom *tuple_var = petta_fresh_var(ctx, "unique_tuple");
        Atom *collapse_call = petta_lower_collapse_call(ctx, args[0]->expr.elems[1]);
        Atom *unique_args[] = { tuple_var };
        Atom *unique_call = make_expr_with_head(
            ctx->arena, atom_symbol_id(ctx->arena, g_builtin_syms.unique_atom),
            unique_args, 1);
        return petta_wrap_let(ctx, tuple_var, collapse_call, unique_call);
    }

    if (atom_is_symbol_name(head, "alpha-unique-atom") &&
        nargs == 1 &&
        expr_head_is_name(args[0], "collapse") &&
        args[0]->expr.len == 2) {
        Atom *tuple_var = petta_fresh_var(ctx, "alpha_unique_tuple");
        Atom *collapse_call = petta_lower_collapse_call(ctx, args[0]->expr.elems[1]);
        Atom *alpha_unique_args[] = { tuple_var };
        Atom *alpha_unique_call = make_expr_with_head(
            ctx->arena, atom_symbol_id(ctx->arena, g_builtin_syms.alpha_unique_atom),
            alpha_unique_args, 1);
        return petta_wrap_let(ctx, tuple_var, collapse_call, alpha_unique_call);
    }

    if (atom_is_symbol_name(head, "test") && nargs == 2) {
        Atom *test_args[] = {
            petta_lower_term(ctx, args[0]),
            petta_lower_term(ctx, args[1]),
        };
        return make_expr_with_head(ctx->arena, head, test_args, 2);
    }

    if (atom_is_symbol_name(head, "chain") && nargs == 3) {
        Atom *chain_args[] = {
            petta_lower_term(ctx, args[0]),
            args[1],
            petta_lower_term(ctx, args[2]),
        };
        return make_expr_with_head(ctx->arena,
                                   atom_symbol_id(ctx->arena, g_builtin_syms.chain),
                                   chain_args, 3);
    }

    if (atom_is_symbol_name(head, "eval") && nargs == 1) {
        Atom *eval_args[] = { petta_lower_term(ctx, args[0]) };
        return make_expr_with_head(ctx->arena,
                                   atom_symbol_id(ctx->arena, g_builtin_syms.eval),
                                   eval_args, 1);
    }

    if (atom_is_symbol_name(head, "unify") && nargs == 4) {
        Atom *unify_args[] = {
            args[0],
            args[1],
            petta_lower_term(ctx, args[2]),
            petta_lower_term(ctx, args[3]),
        };
        return make_expr_with_head(ctx->arena,
                                   atom_symbol_id(ctx->arena, g_builtin_syms.unify),
                                   unify_args, 4);
    }

    if (atom_is_symbol_name(head, "superpose") &&
        nargs == 1 &&
        args[0] &&
        args[0]->kind == ATOM_EXPR) {
        return petta_lower_superpose_literal(ctx, args[0]);
    }

    if (atom_is_symbol_name(head, "cons") && nargs == 2) {
        Atom *cons_args[] = {
            petta_lower_term(ctx, args[0]),
            petta_lower_term(ctx, args[1]),
        };
        return make_expr_with_head(ctx->arena,
                                   atom_symbol_id(ctx->arena, g_builtin_syms.cons_atom),
                                   cons_args, 2);
    }

    if (atom_is_symbol_name(head, "decons") && nargs == 1) {
        Atom *decons_args[] = { petta_lower_term(ctx, args[0]) };
        return make_expr_with_head(ctx->arena,
                                   atom_symbol_id(ctx->arena, g_builtin_syms.decons_atom),
                                   decons_args, 1);
    }

    if (atom_is_symbol_name(head, "#+") && nargs == 2) {
        Atom *plus_args[] = {
            petta_lower_term(ctx, args[0]),
            petta_lower_term(ctx, args[1]),
        };
        return make_expr_with_head(ctx->arena,
                                   atom_symbol_id(ctx->arena, g_builtin_syms.op_plus),
                                   plus_args, 2);
    }

    if (atom_is_symbol_name(head, "#=") && nargs == 2) {
        Atom *actual = petta_fresh_var(ctx, "hash_eq_value");
        Atom *goal = petta_compile_relation_value_goal(ctx, term, actual);
        if (!goal)
            return term;
        return petta_wrap_rel_run(ctx, goal, actual);
    }

    if ((atom_is_symbol_name(head, "#<") || atom_is_symbol_name(head, "#>")) &&
        nargs == 2) {
        Atom *actual = petta_fresh_var(ctx, "hash_cmp_value");
        Atom *goal = petta_compile_relation_value_goal(ctx, term, actual);
        if (!goal)
            return term;
        return petta_wrap_rel_run(ctx, goal, actual);
    }

    if (callable_info && callable_info->syntax_arg_mask) {
        Atom **lowered_args = arena_alloc(ctx->arena, sizeof(Atom *) * nargs);
        bool changed = false;
        for (uint32_t i = 0; i < nargs; i++) {
            if (i < 32 && (callable_info->syntax_arg_mask & ((uint32_t)1u << i))) {
                lowered_args[i] = expr_head_is_name(args[i], "noeval")
                    ? args[i]
                    : petta_wrap_noeval(ctx, args[i]);
            } else {
                lowered_args[i] = petta_lower_term(ctx, args[i]);
            }
            changed = changed || lowered_args[i] != args[i];
        }
        if (changed)
            return make_expr_with_head(ctx->arena, head, lowered_args, nargs);
    }

    bool callable_needs_relation =
        callable_info && callable_info->lhs_needs_relation_lowering;
    if (!petta_is_suppressed_recursive_value_call(ctx, head, nargs) &&
        petta_term_needs_relation(term, ctx->callables) &&
        (petta_term_contains_vars(term) || callable_needs_relation)) {
        Atom *actual = petta_fresh_var(ctx, "rel_value");
        Atom *goal = petta_compile_relation_value_goal(ctx, term, actual);
        if (goal)
            return petta_wrap_rel_run(ctx, goal, actual);
    }

    if (atom_is_symbol_name(head, "@<") && nargs == 2) {
        Atom *lt_args[] = {
            petta_lower_term(ctx, args[0]),
            petta_lower_term(ctx, args[1])
        };
        return make_expr_with_head(ctx->arena, atom_symbol(ctx->arena, "<s"),
                                   lt_args, 2);
    }

    if (atom_is_symbol_name(head, "@>") && nargs == 2) {
        Atom *lt_args[] = {
            petta_lower_term(ctx, args[0]),
            petta_lower_term(ctx, args[1])
        };
        Atom *lt = make_expr_with_head(ctx->arena, atom_symbol(ctx->arena, "<s"),
                                       lt_args, 2);
        Atom *not_args[] = { lt };
        return make_expr_with_head(ctx->arena,
                                   atom_symbol_id(ctx->arena, g_builtin_syms.op_not),
                                   not_args, 1);
    }

    Atom *relational = petta_lower_relational_subterms(ctx, term);
    if (relational != term)
        return relational;

    bool changed = false;
    Atom **lowered_args = arena_alloc(ctx->arena, sizeof(Atom *) * nargs);
    for (uint32_t i = 0; i < nargs; i++) {
        lowered_args[i] = petta_lower_term(ctx, args[i]);
        changed = changed || lowered_args[i] != args[i];
    }
    if (!changed) return term;
    return make_expr_with_head(ctx->arena, head, lowered_args, nargs);
}

static Atom *petta_lower_term(CettaPettaLowerContext *ctx, Atom *term) {
    return petta_lower_value(ctx, term);
}

static bool petta_term_uses_length(Atom *term) {
    if (!term || term->kind != ATOM_EXPR || term->expr.len == 0) return false;
    if (expr_head_is_name(term, "quote") && term->expr.len == 2) return false;
    if (expr_head_is_name(term, "length") && term->expr.len == 2) return true;
    for (uint32_t i = 0; i < term->expr.len; i++) {
        if (petta_term_uses_length(term->expr.elems[i])) return true;
    }
    return false;
}

static bool petta_term_uses_append_relation(Atom *term) {
    if (!term || term->kind != ATOM_EXPR || term->expr.len == 0)
        return false;
    if (expr_head_is_name(term, "quote") && term->expr.len == 2)
        return false;
    if ((expr_head_is_name(term, "append") ||
         expr_head_is_name(term, "union-atom")) &&
        term->expr.len == 3 &&
        petta_term_contains_vars(term))
        return true;
    for (uint32_t i = 0; i < term->expr.len; i++) {
        if (petta_term_uses_append_relation(term->expr.elems[i]))
            return true;
    }
    return false;
}

static bool petta_callable_inventory_has_head(
    const CettaPettaCallableInventory *callables,
    Arena *arena,
    const char *name,
    uint32_t arity
) {
    Atom *symbol = NULL;
    if (!callables || !arena || !name)
        return false;
    symbol = atom_symbol(arena, name);
    return petta_callable_set_find(callables, symbol->sym_id, arity) != NULL;
}

static bool petta_term_needs_relation(Atom *term,
                                      const CettaPettaCallableInventory *callables) {
    if (!term || term->kind != ATOM_EXPR || term->expr.len == 0)
        return false;
    if (expr_head_is_name(term, "quote") && term->expr.len == 2)
        return false;
    Atom *head = term->expr.elems[0];
    uint32_t nargs = term->expr.len - 1;
    if (petta_head_has_builtin_relation(head, nargs))
        return true;
    if (head && head->kind == ATOM_SYMBOL && callables) {
        const CettaPettaCallableInfo *info =
            petta_callable_set_find(callables, head->sym_id, nargs);
        if (info && info->needs_relation_lowering)
            return true;
    }
    uint32_t child_start = (head && head->kind == ATOM_SYMBOL) ? 1 : 0;
    for (uint32_t i = child_start; i < term->expr.len; i++) {
        if (petta_term_needs_relation(term->expr.elems[i], callables))
            return true;
    }
    return false;
}

static bool petta_term_uses_goal_relation(
    Atom *term,
    const CettaPettaCallableInventory *callables
) {
    if (!term || term->kind != ATOM_EXPR || term->expr.len == 0)
        return false;
    if (expr_head_is_name(term, "quote") && term->expr.len == 2)
        return false;
    Atom *head = term->expr.elems[0];
    uint32_t nargs = term->expr.len - 1;
    const PettaHeadProfile *profile = petta_head_profile(head, nargs);
    if (profile && profile->relation_goal_form)
        return true;
    if (head && head->kind == ATOM_SYMBOL && callables) {
        const CettaPettaCallableInfo *info =
            petta_callable_set_find(callables, head->sym_id, nargs);
        if (info && (info->goal_relation_lowering ||
                     info->needs_relation_lowering))
            return true;
    }
    uint32_t child_start = (head && head->kind == ATOM_SYMBOL) ? 1 : 0;
    for (uint32_t i = child_start; i < term->expr.len; i++) {
        if (petta_term_uses_goal_relation(term->expr.elems[i], callables))
            return true;
    }
    return false;
}

static void petta_mark_relation_lowering_callables(Atom **atoms, int n,
                                                   CettaPettaCallableInventory *callables,
                                                   bool include_imported) {
    if (!atoms || n <= 0 || !callables)
        return;
    bool changed;
    do {
        changed = false;
        for (int i = 0; i < n; i++) {
            if (!atoms[i] || atoms[i]->kind != ATOM_EXPR || atoms[i]->expr.len != 3 ||
                !petta_expr_is_declaration_form(atoms[i]))
                continue;
            Atom *lhs = atoms[i]->expr.elems[1];
            Atom *rhs = atoms[i]->expr.elems[2];
            if (!lhs || lhs->kind != ATOM_EXPR || lhs->expr.len == 0)
                continue;
            CettaPettaCallableInfo *info =
                petta_callable_info_mut(callables, lhs->expr.elems[0], lhs->expr.len - 1);
            if (!info || (!include_imported && info->imported))
                continue;
            bool changed_info = false;
            if (petta_atom_is_true_literal(rhs) &&
                !info->goal_relation_lowering) {
                info->goal_relation_lowering = true;
                changed_info = true;
            }
            bool rhs_needs_relation =
                petta_term_needs_relation(rhs, callables) ||
                petta_term_uses_goal_relation(rhs, callables);
            bool lhs_needs_relation = false;
            for (uint32_t argi = 1; !lhs_needs_relation && argi < lhs->expr.len; argi++) {
                lhs_needs_relation =
                    petta_term_needs_relation(lhs->expr.elems[argi], callables) ||
                    petta_term_uses_goal_relation(lhs->expr.elems[argi], callables);
            }
            if (lhs_needs_relation && !info->lhs_needs_relation_lowering) {
                info->lhs_needs_relation_lowering = true;
                changed_info = true;
            }
            if ((rhs_needs_relation || lhs_needs_relation) &&
                !info->needs_relation_lowering) {
                info->needs_relation_lowering = true;
                changed_info = true;
            }
            changed = changed || changed_info;
        }
    } while (changed);
}

static void petta_mark_callable_syntax_args(Atom **atoms, int n,
                                            CettaPettaCallableInventory *callables,
                                            bool include_imported) {
    if (!atoms || n <= 0 || !callables)
        return;
    for (int i = 0; i < n; i++) {
        if (!atoms[i] || atoms[i]->kind != ATOM_EXPR || atoms[i]->expr.len != 3 ||
            !petta_expr_is_declaration_form(atoms[i]))
            continue;
        Atom *lhs = atoms[i]->expr.elems[1];
        Atom *rhs = atoms[i]->expr.elems[2];
        if (!lhs || lhs->kind != ATOM_EXPR || lhs->expr.len == 0)
            continue;
        CettaPettaCallableInfo *info =
            petta_callable_info_mut(callables, lhs->expr.elems[0], lhs->expr.len - 1);
        if (!info || (!include_imported && info->imported))
            continue;

        PettaBoundVarSet syntax_params = {0};
        if (!petta_collect_rhs_syntax_param_vars(rhs, &syntax_params)) {
            petta_bound_var_set_free(&syntax_params);
            continue;
        }
        for (uint32_t argi = 1; argi < lhs->expr.len && argi <= 32; argi++) {
            Atom *arg = lhs->expr.elems[argi];
            if (arg &&
                arg->kind == ATOM_VAR &&
                petta_bound_var_set_contains(&syntax_params, arg->var_id)) {
                info->syntax_arg_mask |= (uint32_t)1u << (argi - 1);
            }
        }
        petta_bound_var_set_free(&syntax_params);
    }
}

static bool petta_program_defines_length(Atom *term) {
    if (!term || term->kind != ATOM_EXPR || term->expr.len != 3) return false;
    if (!petta_expr_is_declaration_form(term)) return false;
    Atom *lhs = term->expr.elems[1];
    return expr_head_is_name(lhs, "length");
}

static Atom *petta_length_compat_decl(CettaPettaLowerContext *ctx) {
    Atom *expr_var = petta_fresh_var(ctx, "length_expr");
    Atom *tuple_var = petta_fresh_var(ctx, "length_tuple");
    Atom *lhs_args[] = { expr_var };
    Atom *lhs = make_expr_with_head(ctx->arena, atom_symbol(ctx->arena, "length"),
                                    lhs_args, 1);
    Atom *eval_args[] = { expr_var };
    Atom *eval_call = make_expr_with_head(ctx->arena,
                                          atom_symbol_id(ctx->arena, g_builtin_syms.eval),
                                          eval_args, 1);
    Atom *size_args[] = { tuple_var };
    Atom *size_call = make_expr_with_head(ctx->arena,
                                          atom_symbol_id(ctx->arena, g_builtin_syms.size_atom),
                                          size_args, 1);
    Atom *let_args[] = { tuple_var, eval_call, size_call };
    Atom *rhs = make_expr_with_head(ctx->arena,
                                    atom_symbol_id(ctx->arena, g_builtin_syms.let),
                                    let_args, 3);
    return atom_expr3(ctx->arena,
                      atom_symbol_id(ctx->arena, g_builtin_syms.equals),
                      lhs, rhs);
}

static int petta_adapt_surface_atoms(Atom **atoms, int n, Arena *arena,
                                     CettaRelativeModulePolicy relative_module_policy,
                                     const char *source_path,
                                     Space *context_space,
                                     bool inject_runtime_helpers,
                                     Atom ***out_atoms) {
    if (n < 0 || !out_atoms) return -1;
    for (int i = 0; i < n; i++) {
        atoms[i] = petta_rewrite_empty_literals(arena, atoms[i]);
    }
    bool uses_length = false;
    bool defines_length = false;
    int relation_equation_count = 0;
    for (int i = 0; i < n; i++) {
        uses_length = uses_length || petta_term_uses_length(atoms[i]);
        defines_length = defines_length || petta_program_defines_length(atoms[i]);
        if ((i == 0 || !atom_is_symbol_name(atoms[i - 1], "!")) &&
            petta_declares_callable_head(atoms[i], NULL, NULL))
            relation_equation_count++;
    }

    int extra = (inject_runtime_helpers && uses_length && !defines_length) ? 1 : 0;
    int relation_builtin_extra = inject_runtime_helpers ? 2 : 0;
    int out_len = n + extra + relation_builtin_extra + relation_equation_count;
    Atom **adapted =
        out_len > 0 ? cetta_malloc(sizeof(Atom *) * (size_t)out_len) : NULL;
    if (out_len > 0 && !adapted)
        return -1;
    CettaPettaCallableInventory callables;
    CettaPettaPathSet visited_imports;
    char import_root_dir[PATH_MAX];
    petta_callable_set_init(&callables);
    petta_path_set_init(&visited_imports);
    petta_collect_space_callable_heads(context_space, arena, &callables);
    petta_collect_program_callable_heads(atoms, n, arena, &callables, false);
    petta_get_working_dir(import_root_dir, sizeof(import_root_dir));
    if (source_path) {
        for (int i = 0; i + 1 < n; i++) {
            const char *spec = NULL;
            char spec_buf[PATH_MAX];
            if (!petta_static_import_spec_at(atoms, n, i, spec_buf,
                                             sizeof(spec_buf), &spec))
                continue;
            char resolved[PATH_MAX];
            if (petta_resolve_static_import(relative_module_policy, import_root_dir,
                                            source_path, spec, resolved,
                                            sizeof(resolved))) {
                petta_collect_imported_callable_heads_from_file(
                    resolved, relative_module_policy, import_root_dir,
                    &callables, &visited_imports);
            }
            i++;
        }
    }
    petta_mark_callable_syntax_args(atoms, n, &callables, false);
    petta_mark_relation_lowering_callables(atoms, n, &callables, false);
    if (!inject_runtime_helpers) {
        bool needs_length_helper =
            uses_length && !defines_length &&
            !petta_callable_inventory_has_head(&callables, arena, "length", 1);
        bool needs_append_helper =
            !petta_callable_inventory_has_head(
                &callables, arena,
                cetta_petta_builtin_relation_head_name(
                    CETTA_PETTA_BUILTIN_REL_APPEND), 4);
        if (needs_append_helper) {
            bool uses_append_relation = false;
            for (int i = 0; i < n && !uses_append_relation; i++) {
                uses_append_relation = petta_term_uses_append_relation(atoms[i]);
            }
            needs_append_helper = uses_append_relation;
        }
        if (needs_length_helper || needs_append_helper) {
            petta_path_set_free(&visited_imports);
            petta_callable_set_free(&callables);
            free(adapted);
            return CETTA_RUNTIME_ADAPT_ERR_HELPER_MISSING;
        }
    }
    CettaPettaLowerContext ctx = {
        .arena = arena,
        .fresh_counter = 0,
        .callables = &callables,
    };

    int out_i = 0;
    if (extra) {
        adapted[out_i++] = petta_length_compat_decl(&ctx);
    }
    if (relation_builtin_extra) {
        adapted[out_i++] = petta_rel_append_base_decl(&ctx);
        adapted[out_i++] = petta_rel_append_step_decl(&ctx);
    }
    for (int i = 0; i < n; i++) {
        bool declaration_position = (i == 0 || !atom_is_symbol_name(atoms[i - 1], "!"));
        if (declaration_position &&
            atoms[i] && atoms[i]->kind == ATOM_EXPR && atoms[i]->expr.len == 3 &&
            petta_expr_is_declaration_form(atoms[i])) {
            Atom *rel_decl =
                petta_lower_relation_equation_decl(&ctx,
                                                   atoms[i]->expr.elems[1],
                                                   atoms[i]->expr.elems[2]);
            if (rel_decl) {
                adapted[out_i++] = rel_decl;
            }
        }
        Atom *lowered = NULL;
        if (declaration_position &&
            atoms[i] &&
            atoms[i]->kind == ATOM_EXPR &&
            atoms[i]->expr.len == 3 &&
            petta_expr_is_declaration_form(atoms[i])) {
            lowered = petta_lower_equation_decl(&ctx,
                                                atoms[i]->expr.elems[1],
                                                atoms[i]->expr.elems[2]);
        } else {
            lowered = petta_lower_term(&ctx, atoms[i]);
        }
        adapted[out_i++] = lowered;
    }

    petta_path_set_free(&visited_imports);
    petta_callable_set_free(&callables);
    *out_atoms = adapted;
    return out_len;
}

static int petta_parse_atoms_to_ids(Atom **atoms, int n, Arena *arena,
                                    TermUniverse *universe,
                                    CettaRelativeModulePolicy relative_module_policy,
                                    const char *source_path,
                                    AtomId **out_ids) {
    Atom **adapted = NULL;
    int out_n = petta_adapt_surface_atoms(atoms, n, arena,
                                          relative_module_policy, source_path,
                                          NULL, true, &adapted);
    if (out_n < 0)
        return out_n;
    AtomId *ids = out_n > 0 ? cetta_malloc(sizeof(AtomId) * (size_t)out_n) : NULL;
    if (out_n > 0 && !ids) {
        free(adapted);
        return -1;
    }
    for (int i = 0; i < out_n; i++) {
        ids[i] = term_universe_store_atom_id(universe, arena, adapted[i]);
    }
    free(adapted);
    *out_ids = ids;
    return out_n;
}

static int parse_ids_with_adapter(const CettaLanguageSpec *lang, bool is_file,
                                  const char *source, Arena *arena,
                                  TermUniverse *universe,
                                  const CettaEvalSession *session,
                                  AtomId **out_ids) {
    if (!lang || !out_ids) return -1;
    if (lang->id != CETTA_LANGUAGE_PETTA) {
        return is_file
            ? parse_metta_file_ids(source, universe, out_ids)
            : parse_metta_text_ids(source, universe, out_ids);
    }

    Atom **atoms = NULL;
    int n = is_file
        ? parse_metta_file(source, arena, &atoms)
        : parse_metta_text(source, arena, &atoms);
    if (n < 0) {
        free(atoms);
        return n;
    }
    CettaRelativeModulePolicy relative_module_policy =
        session ? cetta_eval_session_relative_module_policy(session)
                : cetta_language_relative_module_policy(lang);
    int out_n = petta_parse_atoms_to_ids(atoms, n, arena, universe,
                                         relative_module_policy,
                                         is_file ? source : NULL, out_ids);
    free(atoms);
    return out_n;
}

int cetta_language_parse_text_ids(const CettaLanguageSpec *lang, const char *text,
                                  Arena *arena, TermUniverse *universe,
                                  AtomId **out_ids) {
    return parse_ids_with_adapter(lang, false, text, arena, universe, NULL,
                                  out_ids);
}

int cetta_language_parse_file_ids(const CettaLanguageSpec *lang, const char *path,
                                  Arena *arena, TermUniverse *universe,
                                  AtomId **out_ids) {
    return parse_ids_with_adapter(lang, true, path, arena, universe, NULL,
                                  out_ids);
}

int cetta_language_parse_text_ids_for_session(const CettaEvalSession *session,
                                              const char *text,
                                              Arena *arena,
                                              TermUniverse *universe,
                                              AtomId **out_ids) {
    const CettaLanguageSpec *lang = session ? session->language : NULL;
    return parse_ids_with_adapter(lang, false, text, arena, universe, session,
                                  out_ids);
}

int cetta_language_parse_file_ids_for_session(const CettaEvalSession *session,
                                              const char *path,
                                              Arena *arena,
                                              TermUniverse *universe,
                                              AtomId **out_ids) {
    const CettaLanguageSpec *lang = session ? session->language : NULL;
    return parse_ids_with_adapter(lang, true, path, arena, universe, session,
                                  out_ids);
}

int cetta_language_adapt_runtime_atoms_for_session(
    const CettaEvalSession *session,
    Space *context_space,
    Atom *surface_atom,
    Arena *arena,
    Atom ***out_atoms
) {
    Atom *atoms[1];
    const CettaLanguageSpec *lang = session ? session->language
                                            : cetta_language_current();
    if (!out_atoms || !arena)
        return -1;
    atoms[0] = surface_atom;
    if (!lang || lang->id != CETTA_LANGUAGE_PETTA) {
        Atom **passthrough = cetta_malloc(sizeof(Atom *));
        if (!passthrough)
            return -1;
        passthrough[0] = surface_atom;
        *out_atoms = passthrough;
        return 1;
    }
    return petta_adapt_surface_atoms(
        atoms, 1, arena,
        session ? cetta_eval_session_relative_module_policy(session)
                : cetta_language_relative_module_policy(lang),
        NULL, context_space, false, out_atoms);
}
