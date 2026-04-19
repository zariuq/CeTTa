#include "lang_adapter.h"

#include "grounded.h"
#include "parser.h"
#include "symbol.h"
#include "text_source.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
    Arena *arena;
    uint64_t fresh_counter;
    struct CettaPettaCallableSet *callables;
} CettaPettaLowerContext;

typedef struct CettaPettaCallableSet {
    SymbolId *items;
    uint32_t len;
    uint32_t cap;
} CettaPettaCallableSet;

typedef struct {
    char **items;
    uint32_t len;
    uint32_t cap;
} CettaPettaPathSet;

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

static Atom *petta_lower_term(CettaPettaLowerContext *ctx, Atom *term);

static void petta_callable_set_init(CettaPettaCallableSet *set) {
    memset(set, 0, sizeof(*set));
}

static void petta_callable_set_free(CettaPettaCallableSet *set) {
    if (!set) return;
    free(set->items);
    set->items = NULL;
    set->len = 0;
    set->cap = 0;
}

static bool petta_callable_set_contains(const CettaPettaCallableSet *set,
                                        SymbolId id) {
    if (!set || id == SYMBOL_ID_NONE) return false;
    for (uint32_t i = 0; i < set->len; i++) {
        if (set->items[i] == id) return true;
    }
    return false;
}

static bool petta_callable_set_add(CettaPettaCallableSet *set, SymbolId id) {
    if (!set || id == SYMBOL_ID_NONE || petta_callable_set_contains(set, id)) {
        return true;
    }
    if (set->len == set->cap) {
        uint32_t new_cap = set->cap ? set->cap * 2 : 16;
        SymbolId *new_items =
            cetta_realloc(set->items, sizeof(SymbolId) * new_cap);
        if (!new_items) return false;
        set->items = new_items;
        set->cap = new_cap;
    }
    set->items[set->len++] = id;
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

static bool petta_declares_callable_head(Atom *term, SymbolId *out_head) {
    if (!term || term->kind != ATOM_EXPR || term->expr.len != 3) return false;
    if (!expr_head_is_name(term, "=")) return false;
    Atom *lhs = term->expr.elems[1];
    if (!lhs || lhs->kind != ATOM_EXPR || lhs->expr.len == 0) return false;
    Atom *head = lhs->expr.elems[0];
    if (!head || head->kind != ATOM_SYMBOL) return false;
    if (out_head) *out_head = head->sym_id;
    return true;
}

static void petta_collect_program_callable_heads(Atom **atoms, int n,
                                                 CettaPettaCallableSet *set) {
    if (!atoms || n <= 0 || !set) return;
    for (int i = 0; i < n; i++) {
        SymbolId head = SYMBOL_ID_NONE;
        if (petta_declares_callable_head(atoms[i], &head)) {
            petta_callable_set_add(set, head);
        }
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

static bool petta_resolve_static_import(const char *current_path,
                                        const char *spec,
                                        char *resolved, size_t resolved_sz) {
    char base_dir[PATH_MAX];

    if (!current_path || !*current_path || !spec || !*spec) return false;
    if (strchr(spec, ':')) return false;
    if (!cetta_text_path_parent_dir(base_dir, sizeof(base_dir), current_path))
        return false;
    while (true) {
        if (petta_try_import_candidate(base_dir, spec, resolved, resolved_sz))
            return true;
        char parent[PATH_MAX];
        if (!cetta_text_path_parent_dir(parent, sizeof(parent), base_dir))
            break;
        if (strcmp(parent, base_dir) == 0)
            break;
        snprintf(base_dir, sizeof(base_dir), "%s", parent);
    }
    return false;
}

static bool petta_static_import_spec_at(Atom **atoms, int n, int i,
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
    const char *spec = petta_string_like_atom(expr->expr.elems[2]);
    if (!spec || !*spec)
        return false;
    *spec_out = spec;
    return true;
}

static void petta_collect_imported_callable_heads_from_file(
    const char *path,
    CettaPettaCallableSet *callables,
    CettaPettaPathSet *visited
) {
    Arena parse_arena;
    Atom **atoms = NULL;
    int n = 0;

    if (!path || !callables || !visited || petta_path_set_contains(visited, path))
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

    petta_collect_program_callable_heads(atoms, n, callables);
    for (int i = 0; i + 1 < n; i++) {
        const char *spec = NULL;
        if (!petta_static_import_spec_at(atoms, n, i, &spec))
            continue;
        char resolved[PATH_MAX];
        if (petta_resolve_static_import(path, spec, resolved, sizeof(resolved))) {
            petta_collect_imported_callable_heads_from_file(resolved, callables, visited);
        }
        i++;
    }

    free(atoms);
    arena_free(&parse_arena);
}

static bool petta_head_is_core_callable(Atom *head) {
    return atom_is_symbol_name(head, "empty");
}

static bool petta_head_is_callable(CettaPettaLowerContext *ctx, Atom *head) {
    if (!head || head->kind != ATOM_SYMBOL) return false;
    return is_grounded_op(head->sym_id) ||
           petta_head_is_core_callable(head) ||
           petta_callable_set_contains(ctx ? ctx->callables : NULL, head->sym_id);
}

static Atom *petta_lower_pattern_against(CettaPettaLowerContext *ctx,
                                         Atom *pattern, Atom *actual,
                                         Atom *body);

static Atom *petta_lower_pattern_operand(CettaPettaLowerContext *ctx,
                                         Atom *pattern, Atom **body) {
    if (!pattern || pattern->kind != ATOM_EXPR || pattern->expr.len == 0) {
        return pattern;
    }

    Atom *head = pattern->expr.elems[0];
    if (atom_is_symbol_name(head, "quote") && pattern->expr.len == 2) {
        return pattern;
    }

    if (petta_head_is_callable(ctx, head)) {
        Atom *placeholder = petta_fresh_var(ctx, "pat");
        *body = petta_lower_pattern_against(ctx, pattern, placeholder, *body);
        return placeholder;
    }

    Atom **elems = arena_alloc(ctx->arena, sizeof(Atom *) * pattern->expr.len);
    elems[0] = head;
    for (uint32_t i = 1; i < pattern->expr.len; i++) {
        elems[i] = petta_lower_pattern_operand(ctx, pattern->expr.elems[i], body);
    }
    return atom_expr(ctx->arena, elems, pattern->expr.len);
}

static Atom *petta_lower_callable_pattern(CettaPettaLowerContext *ctx,
                                          Atom *pattern, Atom *actual,
                                          Atom *body) {
    uint32_t nargs = pattern->expr.len - 1;
    Atom **args = arena_alloc(ctx->arena, sizeof(Atom *) * nargs);
    Atom *result_var = petta_fresh_var(ctx, "call_result");
    for (uint32_t i = 0; i < nargs; i++) {
        args[i] = petta_lower_pattern_operand(ctx, pattern->expr.elems[i + 1], &body);
    }
    Atom *call_expr = make_expr_with_head(ctx->arena, pattern->expr.elems[0], args, nargs);
    Atom *guarded_body = petta_wrap_let(ctx, actual, result_var, body);
    Atom *chain_args[] = { call_expr, result_var, guarded_body };
    return make_expr_with_head(ctx->arena,
                               atom_symbol_id(ctx->arena, g_builtin_syms.chain),
                               chain_args, 3);
}

static Atom *petta_lower_pattern_against(CettaPettaLowerContext *ctx,
                                         Atom *pattern, Atom *actual,
                                         Atom *body) {
    if (pattern &&
        pattern->kind == ATOM_EXPR &&
        pattern->expr.len > 0 &&
        petta_head_is_callable(ctx, pattern->expr.elems[0])) {
        return petta_lower_callable_pattern(ctx, pattern, actual, body);
    }
    Atom *shape = petta_lower_pattern_operand(ctx, pattern, &body);
    return petta_wrap_let(ctx, shape, actual, body);
}

static Atom *petta_lower_equation_decl(CettaPettaLowerContext *ctx, Atom *lhs,
                                       Atom *rhs) {
    Atom *lowered_rhs = petta_lower_term(ctx, rhs);
    if (!lhs || lhs->kind != ATOM_EXPR || lhs->expr.len == 0) {
        return atom_expr3(ctx->arena,
                          atom_symbol_id(ctx->arena, g_builtin_syms.equals),
                          lhs, lowered_rhs);
    }

    Atom *head = lhs->expr.elems[0];
    uint32_t nargs = lhs->expr.len - 1;
    Atom **lowered_args = arena_alloc(ctx->arena, sizeof(Atom *) * nargs);
    Atom *body = lowered_rhs;
    for (uint32_t i = nargs; i > 0; i--) {
        Atom *actual = petta_fresh_var(ctx, "arg");
        lowered_args[i - 1] = actual;
        body = petta_lower_pattern_against(ctx, lhs->expr.elems[i], actual, body);
    }
    Atom *lowered_lhs = make_expr_with_head(ctx->arena, head, lowered_args, nargs);
    return atom_expr3(ctx->arena,
                      atom_symbol_id(ctx->arena, g_builtin_syms.equals),
                      lowered_lhs, body);
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
    Atom *t_goal = petta_lower_term(ctx, goal);
    Atom *t_init = petta_lower_term(ctx, init);

    Atom *collector_args[] = { t_goal };
    Atom *collector = make_expr_with_head(ctx->arena,
                                          atom_symbol_id(ctx->arena, g_builtin_syms.collapse),
                                          collector_args, 1);

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
        Atom *collapse_args[] = { petta_lower_term(ctx, arg->expr.elems[1]) };
        Atom *collapse_call = make_expr_with_head(
            ctx->arena, atom_symbol_id(ctx->arena, g_builtin_syms.collapse),
            collapse_args, 1);
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

static Atom *petta_lower_term(CettaPettaLowerContext *ctx, Atom *term) {
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

    if (atom_is_symbol_name(head, "quote") && nargs == 1) {
        return term;
    }

    if (atom_is_symbol_name(head, "=") && nargs == 2) {
        return petta_lower_equation_decl(ctx, args[0], args[1]);
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

    if (atom_is_symbol_name(head, "length") && nargs == 1) {
        return petta_lower_length(ctx, args[0]);
    }

    if (atom_is_symbol_name(head, "unique-atom") &&
        nargs == 1 &&
        expr_head_is_name(args[0], "collapse") &&
        args[0]->expr.len == 2) {
        Atom *tuple_var = petta_fresh_var(ctx, "unique_tuple");
        Atom *collapse_args[] = { petta_lower_term(ctx, args[0]->expr.elems[1]) };
        Atom *collapse_call = make_expr_with_head(
            ctx->arena, atom_symbol_id(ctx->arena, g_builtin_syms.collapse),
            collapse_args, 1);
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
        Atom *collapse_args[] = { petta_lower_term(ctx, args[0]->expr.elems[1]) };
        Atom *collapse_call = make_expr_with_head(
            ctx->arena, atom_symbol_id(ctx->arena, g_builtin_syms.collapse),
            collapse_args, 1);
        Atom *alpha_unique_args[] = { tuple_var };
        Atom *alpha_unique_call = make_expr_with_head(
            ctx->arena, atom_symbol_id(ctx->arena, g_builtin_syms.alpha_unique_atom),
            alpha_unique_args, 1);
        return petta_wrap_let(ctx, tuple_var, collapse_call, alpha_unique_call);
    }

    if (atom_is_symbol_name(head, "test") && nargs == 2) {
        Atom *test_args[] = {
            petta_lower_term(ctx, args[0]),
            petta_lower_term(ctx, args[1])
        };
        return make_expr_with_head(ctx->arena,
                                   atom_symbol_id(ctx->arena, g_builtin_syms.assertEqual),
                                   test_args, 2);
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

    bool changed = false;
    Atom **lowered_args = arena_alloc(ctx->arena, sizeof(Atom *) * nargs);
    for (uint32_t i = 0; i < nargs; i++) {
        lowered_args[i] = petta_lower_term(ctx, args[i]);
        changed = changed || lowered_args[i] != args[i];
    }
    if (!changed) return term;
    return make_expr_with_head(ctx->arena, head, lowered_args, nargs);
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

static bool petta_program_defines_length(Atom *term) {
    if (!term || term->kind != ATOM_EXPR || term->expr.len != 3) return false;
    if (!expr_head_is_name(term, "=")) return false;
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

static int petta_parse_atoms_to_ids(Atom **atoms, int n, Arena *arena,
                                    TermUniverse *universe, const char *source_path,
                                    AtomId **out_ids) {
    if (n < 0) return n;
    bool uses_length = false;
    bool defines_length = false;
    for (int i = 0; i < n; i++) {
        uses_length = uses_length || petta_term_uses_length(atoms[i]);
        defines_length = defines_length || petta_program_defines_length(atoms[i]);
    }

    int extra = (uses_length && !defines_length) ? 1 : 0;
    int out_len = n + extra;
    AtomId *ids = out_len > 0 ? cetta_malloc(sizeof(AtomId) * (size_t)out_len) : NULL;
    CettaPettaCallableSet callables;
    CettaPettaPathSet visited_imports;
    petta_callable_set_init(&callables);
    petta_path_set_init(&visited_imports);
    petta_collect_program_callable_heads(atoms, n, &callables);
    if (source_path && *source_path) {
        for (int i = 0; i + 1 < n; i++) {
            const char *spec = NULL;
            if (!petta_static_import_spec_at(atoms, n, i, &spec))
                continue;
            char resolved[PATH_MAX];
            if (petta_resolve_static_import(source_path, spec, resolved, sizeof(resolved))) {
                petta_collect_imported_callable_heads_from_file(
                    resolved, &callables, &visited_imports);
            }
            i++;
        }
    }
    CettaPettaLowerContext ctx = {
        .arena = arena,
        .fresh_counter = 0,
        .callables = &callables,
    };

    int out_i = 0;
    if (extra) {
        ids[out_i++] = term_universe_store_atom_id(universe, arena,
                                                   petta_length_compat_decl(&ctx));
    }
    for (int i = 0; i < n; i++) {
        ids[out_i++] = term_universe_store_atom_id(
            universe, arena, petta_lower_term(&ctx, atoms[i]));
    }

    petta_path_set_free(&visited_imports);
    petta_callable_set_free(&callables);
    *out_ids = ids;
    return out_len;
}

static int parse_ids_with_adapter(const CettaLanguageSpec *lang, bool is_file,
                                  const char *source, Arena *arena,
                                  TermUniverse *universe, AtomId **out_ids) {
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
    int out_n = petta_parse_atoms_to_ids(atoms, n, arena, universe,
                                         is_file ? source : NULL, out_ids);
    free(atoms);
    return out_n;
}

int cetta_language_parse_text_ids(const CettaLanguageSpec *lang, const char *text,
                                  Arena *arena, TermUniverse *universe,
                                  AtomId **out_ids) {
    return parse_ids_with_adapter(lang, false, text, arena, universe, out_ids);
}

int cetta_language_parse_file_ids(const CettaLanguageSpec *lang, const char *path,
                                  Arena *arena, TermUniverse *universe,
                                  AtomId **out_ids) {
    return parse_ids_with_adapter(lang, true, path, arena, universe, out_ids);
}
