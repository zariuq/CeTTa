#define _GNU_SOURCE
#include "eval.h"
#include "match.h"
#include "grounded.h"
#include "library.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int g_default_fuel = -1;  /* -1 = unlimited (HE spec); positive = opt-in limit */

/* Global registry for named spaces/values (set by eval_top_with_registry) */
static Registry *g_registry = NULL;
/* Persistent arena for atoms that outlive a single evaluation (space, states) */
static Arena *g_persistent_arena = NULL;
/* pragma! type-check auto: when true, grounded ops type-check args */
static bool g_type_check_auto = false;

typedef struct {
    Space **items;
    uint32_t len, cap;
} TempSpaceSet;

static TempSpaceSet g_temp_spaces = {0};

/* ── Result Set ─────────────────────────────────────────────────────────── */

void result_set_init(ResultSet *rs) {
    rs->items = NULL;
    rs->len = 0;
    rs->cap = 0;
}

void result_set_add(ResultSet *rs, Atom *atom) {
    if (rs->len >= rs->cap) {
        rs->cap = rs->cap ? rs->cap * 2 : 8;
        rs->items = cetta_realloc(rs->items, sizeof(Atom *) * rs->cap);
    }
    rs->items[rs->len++] = atom;
}

void result_set_free(ResultSet *rs) {
    free(rs->items);
    rs->items = NULL;
    rs->len = rs->cap = 0;
}

/* ── Helpers ────────────────────────────────────────────────────────────── */

static bool expr_head_is(Atom *a, const char *name) {
    return a->kind == ATOM_EXPR && a->expr.len >= 1 &&
           atom_is_symbol(a->expr.elems[0], name);
}

static uint32_t expr_nargs(Atom *a) {
    return a->kind == ATOM_EXPR ? a->expr.len - 1 : 0;
}

static Atom *expr_arg(Atom *a, uint32_t i) {
    return a->expr.elems[i + 1];
}

static bool is_true_atom(Atom *a) {
    return atom_is_symbol(a, "True") ||
           (a->kind == ATOM_GROUNDED && a->ground.gkind == GV_BOOL && a->ground.bval);
}

/* ── Outcome set (unified result type: atom + bindings) ─────────────────── */

void outcome_set_init(OutcomeSet *os) {
    os->items = NULL;
    os->len = 0;
    os->cap = 0;
}

void outcome_set_add(OutcomeSet *os, Atom *atom, const Bindings *env) {
    if (os->len >= os->cap) {
        os->cap = os->cap ? os->cap * 2 : 8;
        os->items = cetta_realloc(os->items, sizeof(Outcome) * os->cap);
    }
    os->items[os->len].atom = atom;
    os->items[os->len].env = *env;
    os->len++;
}

void outcome_set_free(OutcomeSet *os) {
    free(os->items);
    os->items = NULL;
    os->len = os->cap = 0;
}

/* Active importable library set */
static CettaLibraryContext *g_library_context = NULL;

static Atom *dispatch_native_op(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    Atom *result = grounded_dispatch(a, head, args, nargs);
    if (result) return result;
    if (g_library_context) {
        return cetta_library_dispatch_native(g_library_context, a, head, args, nargs);
    }
    return NULL;
}

static void bindings_merge_into(Bindings *dst, const Bindings *src) {
    for (uint32_t i = 0; i < src->len; i++) {
        bindings_add(dst, src->entries[i].var, src->entries[i].val);
    }
}

static Atom *resolve_registry_refs(Arena *a, Atom *atom) {
    if (atom->kind == ATOM_SYMBOL && atom->name[0] == '&' && g_registry) {
        Atom *val = registry_lookup(g_registry, atom->name);
        if (val) return val;
    }
    if (atom->kind == ATOM_EXPR) {
        Atom **new_elems = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
        bool changed = false;
        for (uint32_t i = 0; i < atom->expr.len; i++) {
            new_elems[i] = resolve_registry_refs(a, atom->expr.elems[i]);
            if (new_elems[i] != atom->expr.elems[i]) changed = true;
        }
        if (changed) return atom_expr(a, new_elems, atom->expr.len);
    }
    return atom;
}

static void temp_space_register(Space *space) {
    if (g_temp_spaces.len >= g_temp_spaces.cap) {
        g_temp_spaces.cap = g_temp_spaces.cap ? g_temp_spaces.cap * 2 : 4;
        g_temp_spaces.items = cetta_realloc(g_temp_spaces.items, sizeof(Space *) * g_temp_spaces.cap);
    }
    g_temp_spaces.items[g_temp_spaces.len++] = space;
}

void eval_release_temporary_spaces(void) {
    for (uint32_t i = 0; i < g_temp_spaces.len; i++) {
        space_free(g_temp_spaces.items[i]);
        free(g_temp_spaces.items[i]);
    }
    free(g_temp_spaces.items);
    g_temp_spaces.items = NULL;
    g_temp_spaces.len = 0;
    g_temp_spaces.cap = 0;
}

/* Lightweight snapshot: shares atom pointers (no deep copy).
   The snapshot's atoms[] is a frozen copy of the source's pointer array.
   Safe because: atoms in the source live in the persistent arena and are
   never mutated or freed during evaluation.  The snapshot only needs its
   own atoms[] array (malloc'd) and indexes; space_free handles that. */
static Space *space_snapshot_clone(Space *src, Arena *a) {
    (void)a;
    Space *clone = cetta_malloc(sizeof(Space));
    space_init(clone);
    (void)space_match_backend_try_set(clone, src->match_backend.kind);
    if (src->len > 0) {
        clone->cap = src->len;
        clone->atoms = cetta_malloc(sizeof(Atom *) * clone->cap);
        memcpy(clone->atoms, src->atoms, sizeof(Atom *) * src->len);
        clone->len = src->len;
    }
    /* Backend indexes are rebuilt lazily on first match against the snapshot. */
    temp_space_register(clone);
    return clone;
}

/* ── Function type utilities (Types.lean:260-281) ──────────────────────── */

static bool is_function_type(Atom *a) {
    return a->kind == ATOM_EXPR && a->expr.len >= 3 &&
           atom_is_symbol(a->expr.elems[0], "->");
}

/* Extract arg types from (-> t1 t2 ... tN ret). Returns count.
   Writes to out[], up to max entries. Excludes "->" and last (return type). */
static uint32_t get_function_arg_types(Atom *ft, Atom **out, uint32_t max) {
    if (!is_function_type(ft)) return 0;
    uint32_t n = ft->expr.len - 2; /* skip "->" at [0] and ret at [len-1] */
    if (n > max) n = max;
    for (uint32_t i = 0; i < n; i++)
        out[i] = ft->expr.elems[i + 1];
    return n;
}

static Atom *get_function_ret_type(Atom *ft) {
    if (!is_function_type(ft)) return NULL;
    return ft->expr.elems[ft->expr.len - 1];
}

/* ── Type cast (TypeCheck.lean:126-148) ────────────────────────────────── */

static void type_cast_fn(Space *s, Arena *a, Atom *atom, Atom *expectedType,
                         int fuel, ResultSet *rs) {
    Atom **types;
    uint32_t ntypes = get_atom_types(s, a, atom, &types);
    /* Try each type — return on FIRST match (early return per spec) */
    for (uint32_t i = 0; i < ntypes; i++) {
        Bindings mb;
        bindings_init(&mb);
        if (match_types(types[i], expectedType, &mb)) {
            result_set_add(rs, atom);
            free(types);
            return;
        }
    }
    /* No match — return errors for all types */
    for (uint32_t i = 0; i < ntypes; i++) {
        Atom *reason = atom_expr3(a, atom_symbol(a, "BadType"),
                                  expectedType, types[i]);
        result_set_add(rs, atom_error(a, atom, reason));
    }
    free(types);
}

/* ── Check if function type is applicable (TypeCheck.lean:55-116) ──────── */

/* Returns true if applicable, filling success_bindings[0..n_success-1].
   Returns false if not applicable, filling errors[0..n_errors-1]. */
static bool check_function_applicable(
    Atom *expr, Atom *funcType, Atom *expectedType,
    Space *s, Arena *a, int fuel,
    /* out */ Atom **errors, uint32_t *n_errors,
    Bindings *success_bindings, uint32_t *n_success) {

    *n_errors = 0;
    *n_success = 0;

    Atom *arg_types[32];
    uint32_t nargs = get_function_arg_types(funcType, arg_types, 32);
    uint32_t expr_narg = (expr->kind == ATOM_EXPR && expr->expr.len > 0)
                         ? expr->expr.len - 1 : 0;

    /* Step 1: arity check */
    if (expr_narg != nargs) {
        errors[(*n_errors)++] = atom_error(a, expr,
            atom_symbol(a, "IncorrectNumberOfArguments"));
        return false;
    }

    Atom *retType = get_function_ret_type(funcType);
    if (!retType) retType = atom_undefined_type(a);

    /* Step 2: check each argument type, threading bindings */
    Bindings results[64];
    uint32_t nresults = 1;
    bindings_init(&results[0]);

    for (uint32_t i = 0; i < nargs && nresults > 0; i++) {
        Atom *arg = expr->expr.elems[i + 1];
        Atom **atypes;
        uint32_t natypes = get_atom_types(s, a, arg, &atypes);

        Bindings next[64];
        uint32_t nnext = 0;

        for (uint32_t r = 0; r < nresults; r++) {
            bool found = false;
            /* Apply accumulated bindings to expected arg type
               (resolves type variables bound by previous args) */
            Atom *expected = bindings_apply(&results[r], a, arg_types[i]);
            for (uint32_t t = 0; t < natypes; t++) {
                Bindings mb = results[r];
                if (match_types(expected, atypes[t], &mb)) {
                    if (nnext < 64) next[nnext++] = mb;
                    found = true;
                }
            }
            if (!found && natypes > 0) {
                /* Report first mismatching type */
                Atom *reason = atom_expr(a, (Atom*[]){
                    atom_symbol(a, "BadArgType"),
                    atom_int(a, (int64_t)(i + 1)),
                    expected,
                    atypes[0]
                }, 4);
                if (*n_errors < 64)
                    errors[(*n_errors)++] = atom_error(a, expr, reason);
            }
        }
        free(atypes);
        memcpy(results, next, sizeof(Bindings) * nnext);
        nresults = nnext;
    }

    if (nresults == 0) return false;

    /* Step 3: check return type */
    uint32_t ret_ok = 0;
    for (uint32_t r = 0; r < nresults; r++) {
        Bindings mb = results[r];
        if (match_types(expectedType, retType, &mb)) {
            if (ret_ok < 64)
                success_bindings[ret_ok++] = mb;
        } else {
            Atom *reason = atom_expr3(a, atom_symbol(a, "BadType"),
                                      expectedType, retType);
            if (*n_errors < 64)
                errors[(*n_errors)++] = atom_error(a, expr, reason);
        }
    }

    *n_success = ret_ok;
    return ret_ok > 0;
}

/* ── Forward declarations ───────────────────────────────────────────────── */

static void metta_call(Space *s, Arena *a, Atom *atom, int fuel, ResultSet *rs);
/* Like metta_eval but also returns bindings produced by equation queries */
static void metta_eval_bind(Space *s, Arena *a, Atom *atom, int fuel, ResultBindSet *rbs);

/* ── metta_eval: full recursive evaluation (metta.md lines 240-272) ────── */

void metta_eval(Space *s, Arena *a, Atom *type, Atom *atom, int fuel, ResultSet *rs) {
    if (fuel == 0) {
        /* Fuel exhausted — return empty result set (matches HE behavior:
           infinite recursion produces no output, not an error atom) */
        return;
    }

    Atom *etype = type ? type : atom_undefined_type(a);

    /* Resolve registry tokens (&name → bound value) */
    if (atom->kind == ATOM_SYMBOL && atom->name[0] == '&' && g_registry) {
        Atom *val = registry_lookup(g_registry, atom->name);
        if (val) {
            result_set_add(rs, val);
            return;
        }
    }

    /* Empty/Error: return as-is (spec line 253) */
    if (atom_is_empty(atom) || atom_is_error(atom)) {
        result_set_add(rs, atom);
        return;
    }

    /* Type == Atom or matches meta-type, or meta-type is Variable:
       return as-is (spec line 255) — THIS is the laziness control */
    Atom *meta = get_meta_type(a, atom);
    if (atom_is_symbol(etype, "Atom") || atom_eq(etype, meta) ||
        atom_is_symbol(meta, "Variable")) {
        result_set_add(rs, atom);
        return;
    }

    /* Symbol/Grounded/empty-expr: typeCast (spec line 260) */
    if (atom->kind == ATOM_SYMBOL || atom->kind == ATOM_GROUNDED ||
        (atom->kind == ATOM_EXPR && atom->expr.len == 0)) {
        type_cast_fn(s, a, atom, etype, fuel, rs);
        return;
    }

    /* Variable: return as-is (already handled by meta==Variable above,
       but be safe) */
    if (atom->kind == ATOM_VAR) {
        result_set_add(rs, atom);
        return;
    }

    /* Expression: interpret_expression → metta_call (spec line 262) */
    metta_call(s, a, atom, fuel > 0 ? fuel - 1 : fuel, rs);
}

/* ── metta_eval_bind: like metta_eval but returns bindings too ──────────── */
/* Used by interpret_tuple to thread bindings between sub-expressions.
   For most atoms, bindings are empty. For equation queries, bindings
   contain variable assignments from bidirectional matching. */

static void metta_call_bind(Space *s, Arena *a, Atom *atom, int fuel, ResultBindSet *rbs);

static void metta_eval_bind(Space *s, Arena *a, Atom *atom, int fuel, ResultBindSet *rbs) {
    Bindings empty;
    bindings_init(&empty);

    if (fuel == 0 || atom->kind == ATOM_SYMBOL || atom->kind == ATOM_GROUNDED ||
        atom->kind == ATOM_VAR ||
        (atom->kind == ATOM_EXPR && atom->expr.len == 0)) {
        rb_set_add(rbs, atom, &empty);
        return;
    }
    metta_call_bind(s, a, atom, fuel > 0 ? fuel - 1 : fuel, rbs);
}

/* ── interpret_tuple: Cartesian product of sub-expression evaluations ──── */
/* Per HE spec metta.md lines 358-381:
   Evaluate each element of the expression. If an element returns multiple
   results, produce all combinations (Cartesian product).
   The bindings parameter threads variable bindings from earlier elements
   to later ones (spec line 376: interpret_tuple($tail, $space, $hb)). */

static void interpret_tuple(Space *s, Arena *a,
                            Atom **orig_elems, uint32_t len,
                            uint32_t idx, Atom **prefix,
                            Bindings *ctx, int fuel, ResultBindSet *rbs) {
    if (idx == len) {
        rb_set_add(rbs, atom_expr(a, prefix, len), ctx);
        return;
    }
    /* Apply accumulated bindings to this element before evaluating */
    Atom *elem = bindings_apply(ctx, a, orig_elems[idx]);
    ResultBindSet sub;
    rb_set_init(&sub);
    metta_eval_bind(s, a, elem, fuel, &sub);
    if (sub.len == 0) {
        free(sub.items);
        return;
    }
    Bindings empty;
    bindings_init(&empty);
    for (uint32_t i = 0; i < sub.len; i++) {
        if (atom_is_empty(sub.items[i].atom) || atom_is_error(sub.items[i].atom)) {
            rb_set_add(rbs, sub.items[i].atom, &empty);
        } else {
            prefix[idx] = sub.items[i].atom;
            Bindings merged = *ctx;
            for (uint32_t j = 0; j < sub.items[i].env.len; j++) {
                bindings_add(&merged,
                    sub.items[i].env.entries[j].var,
                    sub.items[i].env.entries[j].val);
            }
            interpret_tuple(s, a, orig_elems, len, idx + 1, prefix,
                            &merged, fuel, rbs);
        }
    }
    free(sub.items);
}

static __attribute__((noinline)) bool
handle_match(Space *s, Arena *a, Atom *atom, int fuel, ResultSet *rs) {
    uint32_t nargs = expr_nargs(atom);
    if (!expr_head_is(atom, "match") || nargs != 3) return false;

    Atom *space_ref = expr_arg(atom, 0);
    Atom *pattern = resolve_registry_refs(a, expr_arg(atom, 1));
    Atom *template = resolve_registry_refs(a, expr_arg(atom, 2));
    Space *ms = g_registry ? resolve_space(g_registry, space_ref) : NULL;
    if (!ms) ms = s;

    if (pattern->kind == ATOM_EXPR && pattern->expr.len >= 3 &&
        atom_is_symbol(pattern->expr.elems[0], ",")) {
        uint32_t n_conjuncts = pattern->expr.len - 1;
        BindingSet matches;
        space_query_conjunction(ms, a, pattern->expr.elems + 1, n_conjuncts,
                                NULL, &matches);
        for (uint32_t bi = 0; bi < matches.len; bi++) {
            Atom *result = bindings_apply(&matches.items[bi], a, template);
            metta_eval(s, a, NULL, result, fuel, rs);
        }
        binding_set_free(&matches);
        return true;
    }

    {
        #define MAX_CHAIN 16
        #define MAX_VARS_PER_PAT 32
        typedef struct { Space *space; Atom *pattern; } MatchStep;

        MatchStep steps[MAX_CHAIN];
        uint32_t nsteps = 0;

        steps[nsteps].space = ms;
        steps[nsteps].pattern = pattern;
        nsteps++;

        Atom *body = template;
        while (nsteps < MAX_CHAIN &&
               body->kind == ATOM_EXPR && body->expr.len == 4 &&
               atom_is_symbol(body->expr.elems[0], "match")) {
            Atom *inner_ref = resolve_registry_refs(a, body->expr.elems[1]);
            Atom *inner_pat = resolve_registry_refs(a, body->expr.elems[2]);
            Space *inner_sp = g_registry ? resolve_space(g_registry, inner_ref) : NULL;
            if (!inner_sp) inner_sp = s;
            steps[nsteps].space = inner_sp;
            steps[nsteps].pattern = inner_pat;
            nsteps++;
            body = body->expr.elems[3];
        }

        if (nsteps >= 3) {
            const char *pat_vars[MAX_CHAIN][MAX_VARS_PER_PAT];
            uint32_t pat_nvars[MAX_CHAIN];
            for (uint32_t i = 0; i < nsteps; i++) {
                pat_nvars[i] = 0;
                Atom *stack[64];
                uint32_t sp = 0;
                stack[sp++] = steps[i].pattern;
                while (sp > 0) {
                    Atom *cur = stack[--sp];
                    if (cur->kind == ATOM_VAR) {
                        bool dup = false;
                        for (uint32_t v = 0; v < pat_nvars[i]; v++) {
                            if (strcmp(pat_vars[i][v], cur->name) == 0) {
                                dup = true;
                                break;
                            }
                        }
                        if (!dup && pat_nvars[i] < MAX_VARS_PER_PAT)
                            pat_vars[i][pat_nvars[i]++] = cur->name;
                    }
                    if (cur->kind == ATOM_EXPR) {
                        for (uint32_t j = 0; j < cur->expr.len && sp < 64; j++)
                            stack[sp++] = cur->expr.elems[j];
                    }
                }
            }

            const char *bound[MAX_CHAIN * MAX_VARS_PER_PAT];
            uint32_t nbound = 0;
            for (uint32_t v = 0; v < pat_nvars[0]; v++)
                bound[nbound++] = pat_vars[0][v];

            bool scheduled[MAX_CHAIN];
            memset(scheduled, 0, sizeof(scheduled));
            scheduled[0] = true;

            MatchStep reordered[MAX_CHAIN];
            reordered[0] = steps[0];
            for (uint32_t round = 1; round < nsteps; round++) {
                int best = -1;
                uint32_t best_score = 0;
                for (uint32_t j = 1; j < nsteps; j++) {
                    if (scheduled[j]) continue;
                    uint32_t score = 0;
                    for (uint32_t v = 0; v < pat_nvars[j]; v++) {
                        for (uint32_t b = 0; b < nbound; b++) {
                            if (strcmp(pat_vars[j][v], bound[b]) == 0) {
                                score++;
                                break;
                            }
                        }
                    }
                    if (best < 0 || score > best_score) {
                        best = (int)j;
                        best_score = score;
                    }
                }
                scheduled[best] = true;
                reordered[round] = steps[best];
                for (uint32_t v = 0; v < pat_nvars[best]; v++) {
                    bool dup = false;
                    for (uint32_t b = 0; b < nbound; b++) {
                        if (strcmp(bound[b], pat_vars[best][v]) == 0) {
                            dup = true;
                            break;
                        }
                    }
                    if (!dup) bound[nbound++] = pat_vars[best][v];
                }
            }
            memcpy(steps, reordered, sizeof(MatchStep) * nsteps);
        }

        Bindings *cur_binds = cetta_malloc(sizeof(Bindings));
        uint32_t ncur = 1;
        bindings_init(&cur_binds[0]);

        uint32_t accum_steps = (nsteps > 1) ? nsteps - 1 : nsteps;
        for (uint32_t si = 0; si < accum_steps; si++) {
            MatchStep *step = &steps[si];
            Bindings *next_binds = NULL;
            uint32_t nnext = 0, cnext = 0;
            for (uint32_t bi = 0; bi < ncur; bi++) {
                Atom *grounded = bindings_apply(&cur_binds[bi], a, step->pattern);
                SubstMatchSet smr;
                smset_init(&smr);
                space_subst_query(step->space, a, grounded, &smr);
                for (uint32_t ci = 0; ci < smr.len; ci++) {
                    Bindings mb;
                    if (space_subst_match_with_seed(step->space, grounded, &smr.items[ci],
                                                    &cur_binds[bi], a, &mb)) {
                        if (nnext >= cnext) {
                            cnext = cnext ? cnext * 2 : 8;
                            next_binds = cetta_realloc(next_binds, sizeof(Bindings) * cnext);
                        }
                        next_binds[nnext++] = mb;
                    }
                }
                free(smr.items);
            }
            free(cur_binds);
            cur_binds = next_binds;
            ncur = nnext;
            if (ncur == 0) break;
        }

        if (nsteps > 1 && ncur > 0) {
            MatchStep *last = &steps[nsteps - 1];
            static uint64_t g_chain_progress = 0;
            for (uint32_t bi = 0; bi < ncur; bi++) {
                Atom *grounded = bindings_apply(&cur_binds[bi], a, last->pattern);
                SubstMatchSet smr;
                smset_init(&smr);
                space_subst_query(last->space, a, grounded, &smr);
                for (uint32_t ci = 0; ci < smr.len; ci++) {
                    Bindings mb;
                    if (space_subst_match_with_seed(last->space, grounded, &smr.items[ci],
                                                    &cur_binds[bi], a, &mb)) {
                        Atom *result = bindings_apply(&mb, a, body);
                        metta_eval(s, a, NULL, result, fuel, rs);
                        g_chain_progress++;
                        if ((g_chain_progress % 100000) == 0) {
                            fprintf(stderr, "[chain] %luk results  (step %u/%u, bi=%u/%u)\n",
                                (unsigned long)(g_chain_progress / 1000),
                                nsteps, nsteps, bi, ncur);
                        }
                    }
                }
                free(smr.items);
            }
        } else {
            for (uint32_t bi = 0; bi < ncur; bi++) {
                Atom *result = bindings_apply(&cur_binds[bi], a, body);
                metta_eval(s, a, NULL, result, fuel, rs);
            }
        }
        free(cur_binds);
    }

    return true;
}

static __attribute__((noinline)) bool
handle_dispatch(Space *s, Arena *a, Atom *atom, int fuel, Atom **tail_next, ResultSet *rs) {
    *tail_next = NULL;
    if (atom->kind != ATOM_EXPR || atom->expr.len < 1) return false;

    Atom *op = atom->expr.elems[0];
    Atom **op_types;
    uint32_t n_op_types = get_atom_types(s, a, op, &op_types);

    bool has_func_type = false;
    bool has_non_func_type = false;
    ResultSet func_results;
    result_set_init(&func_results);
    Atom *func_errors[64];
    uint32_t n_func_errors = 0;

    for (uint32_t ti = 0; ti < n_op_types; ti++) {
        if (is_function_type(op_types[ti])) {
            has_func_type = true;
            Atom *errors[64];
            uint32_t n_errors = 0;
            Bindings succs[64];
            uint32_t n_succs = 0;
            Atom *exp_type = atom_undefined_type(a);
            Atom *fresh_ft = rename_vars(a, op_types[ti], fresh_var_suffix());
            if (check_function_applicable(atom, fresh_ft, exp_type,
                                          s, a, fuel,
                                          errors, &n_errors,
                                          succs, &n_succs)) {
                Atom *arg_types[32];
                uint32_t narg = get_function_arg_types(fresh_ft, arg_types, 32);
                Atom *ret_type = get_function_ret_type(fresh_ft);
                if (atom_is_symbol(ret_type, "Expression"))
                    ret_type = atom_undefined_type(a);

                uint32_t expr_narg = atom->expr.len - 1;
                ResultBindSet arg_tuples;
                rb_set_init(&arg_tuples);
                Atom **arg_prefix = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
                arg_prefix[0] = op;
                Bindings empty_ctx;
                bindings_init(&empty_ctx);

                ResultBindSet *arg_rbs = arena_alloc(a, sizeof(ResultBindSet) * expr_narg);
                Bindings arg_ctx;
                bindings_init(&arg_ctx);
                bool arg_ok = true;
                bool arg_error = false;
                for (uint32_t ai = 0; ai < expr_narg && ai < narg; ai++) {
                    rb_set_init(&arg_rbs[ai]);
                    Atom *bound_arg = bindings_apply(&arg_ctx, a, atom->expr.elems[ai + 1]);
                    if (atom_is_symbol(arg_types[ai], "Atom") ||
                        atom->expr.elems[ai + 1]->kind == ATOM_VAR) {
                        Bindings empty_b;
                        bindings_init(&empty_b);
                        rb_set_add(&arg_rbs[ai], atom->expr.elems[ai + 1], &empty_b);
                    } else {
                        metta_eval_bind(s, a, bound_arg, fuel, &arg_rbs[ai]);
                    }
                    if (arg_rbs[ai].len == 0) { arg_ok = false; break; }
                    if (arg_rbs[ai].len == 1 &&
                        atom_is_error(arg_rbs[ai].items[0].atom) &&
                        !atom_eq(arg_rbs[ai].items[0].atom, atom->expr.elems[ai + 1])) {
                        result_set_add(&func_results, arg_rbs[ai].items[0].atom);
                        arg_error = true;
                        break;
                    }
                    for (uint32_t k = 0; k < arg_rbs[ai].items[0].env.len; k++) {
                        bindings_add(&arg_ctx,
                            arg_rbs[ai].items[0].env.entries[k].var,
                            arg_rbs[ai].items[0].env.entries[k].val);
                    }
                }

                if (arg_ok && !arg_error && expr_narg > 0) {
                    Atom **prefix = arena_alloc(a, sizeof(Atom *) * expr_narg);
                    uint32_t stack_idx[32];
                    memset(stack_idx, 0, sizeof(stack_idx));
                    for (;;) {
                        Bindings combo_ctx;
                        bindings_init(&combo_ctx);
                        for (uint32_t ai = 0; ai < expr_narg; ai++) {
                            Bindings *rb = &arg_rbs[ai].items[stack_idx[ai]].env;
                            for (uint32_t k = 0; k < rb->len; k++)
                                bindings_add(&combo_ctx, rb->entries[k].var, rb->entries[k].val);
                        }
                        for (uint32_t ai = 0; ai < expr_narg; ai++) {
                            Atom *raw = arg_rbs[ai].items[stack_idx[ai]].atom;
                            prefix[ai] = bindings_apply(&combo_ctx, a, raw);
                        }
                        Atom **call_elems = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
                        call_elems[0] = op;
                        for (uint32_t ai = 0; ai < expr_narg; ai++)
                            call_elems[ai + 1] = prefix[ai];
                        Atom *call_atom = atom_expr(a, call_elems, atom->expr.len);

                        bool dispatched = false;
                        if (call_atom->kind == ATOM_EXPR && call_atom->expr.len >= 2) {
                            Atom *h = call_atom->expr.elems[0];
                            if (h->kind == ATOM_SYMBOL && is_grounded_op(h->name)) {
                                Atom *gr = dispatch_native_op(a, h,
                                    call_atom->expr.elems + 1, call_atom->expr.len - 1);
                                if (gr) {
                                    metta_eval(s, a, ret_type, gr, fuel, &func_results);
                                    dispatched = true;
                                }
                            }
                        }
                        if (!dispatched) {
                            QueryResults qr;
                            query_results_init(&qr);
                            query_equations(s, call_atom, a, &qr);
                            if (qr.len > 0) {
                                for (uint32_t qi = 0; qi < qr.len; qi++)
                                    metta_eval(s, a, ret_type, qr.items[qi].result, fuel, &func_results);
                                dispatched = true;
                            }
                            free(qr.items);
                        }
                        if (!dispatched)
                            result_set_add(&func_results, call_atom);

                        {
                            uint32_t carry = expr_narg;
                            while (carry > 0) {
                                carry--;
                                stack_idx[carry]++;
                                if (stack_idx[carry] < arg_rbs[carry].len) break;
                                stack_idx[carry] = 0;
                                if (carry == 0) goto func_done;
                            }
                        }
                    }
                }
func_done:
                for (uint32_t ai = 0; ai < expr_narg && ai < narg; ai++)
                    free(arg_rbs[ai].items);
                free(arg_tuples.items);
            } else {
                for (uint32_t ei = 0; ei < n_errors && n_func_errors < 64; ei++)
                    func_errors[n_func_errors++] = errors[ei];
            }
        } else {
            has_non_func_type = true;
        }
    }
    free(op_types);

    if (func_results.len > 0 && !has_non_func_type) {
        for (uint32_t i = 0; i < func_results.len; i++)
            result_set_add(rs, func_results.items[i]);
        free(func_results.items);
        return true;
    }
    if (func_results.len > 0) {
        for (uint32_t i = 0; i < func_results.len; i++)
            result_set_add(rs, func_results.items[i]);
        free(func_results.items);
        if (rs->len > 0) return true;
    }
    free(func_results.items);

    if (has_func_type && n_func_errors > 0 &&
        (!has_non_func_type || g_type_check_auto)) {
        for (uint32_t i = 0; i < n_func_errors; i++)
            result_set_add(rs, func_errors[i]);
        return true;
    }

    {
        ResultBindSet tuples;
        rb_set_init(&tuples);
        Atom **prefix = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
        Bindings empty_ctx;
        bindings_init(&empty_ctx);
        interpret_tuple(s, a, atom->expr.elems, atom->expr.len,
                        0, prefix, &empty_ctx, fuel, &tuples);

        if (tuples.len == 1) {
            Atom *call_atom = bindings_apply(&tuples.items[0].env, a, tuples.items[0].atom);
            if (atom_is_empty(call_atom) || atom_is_error(call_atom)) {
                result_set_add(rs, call_atom);
                free(tuples.items);
                return true;
            }
            if (call_atom->kind == ATOM_EXPR && call_atom->expr.len >= 2) {
                Atom *h = call_atom->expr.elems[0];
                if (h->kind == ATOM_SYMBOL && is_grounded_op(h->name)) {
                    Atom *result = dispatch_native_op(a, h,
                        call_atom->expr.elems + 1, call_atom->expr.len - 1);
                    if (result) {
                        free(tuples.items);
                        *tail_next = result;
                        return true;
                    }
                }
            }

            QueryResults qr;
            query_results_init(&qr);
            query_equations(s, call_atom, a, &qr);
            if (qr.len == 1) {
                *tail_next = qr.items[0].result;
                free(qr.items);
                free(tuples.items);
                return true;
            }
            if (qr.len > 0) {
                for (uint32_t i = 0; i < qr.len; i++)
                    metta_eval(s, a, NULL, qr.items[i].result, fuel, rs);
                free(qr.items);
                free(tuples.items);
                return true;
            }
            free(qr.items);
            result_set_add(rs, call_atom);
            free(tuples.items);
            return true;
        }

        for (uint32_t ti = 0; ti < tuples.len; ti++) {
            Atom *call_atom = tuples.items[ti].atom;

            if (atom_is_empty(call_atom) || atom_is_error(call_atom)) {
                result_set_add(rs, call_atom);
                continue;
            }

            call_atom = bindings_apply(&tuples.items[ti].env, a, call_atom);

            if (call_atom->kind == ATOM_EXPR && call_atom->expr.len >= 2) {
                Atom *h = call_atom->expr.elems[0];
                if (h->kind == ATOM_SYMBOL && is_grounded_op(h->name)) {
                    Atom *result = dispatch_native_op(a, h,
                        call_atom->expr.elems + 1, call_atom->expr.len - 1);
                    if (result) {
                        metta_eval(s, a, NULL, result, fuel, rs);
                        continue;
                    }
                }
            }

            QueryResults qr;
            query_results_init(&qr);
            query_equations(s, call_atom, a, &qr);
            if (qr.len > 0) {
                for (uint32_t i = 0; i < qr.len; i++)
                    metta_eval(s, a, NULL, qr.items[i].result, fuel, rs);
                free(qr.items);
                continue;
            }
            free(qr.items);

            result_set_add(rs, call_atom);
        }
        free(tuples.items);
    }

    return true;
}

/* ── metta_call: dispatch expressions ───────────────────────────────────── */

static void metta_call(Space *s, Arena *a, Atom *atom, int fuel, ResultSet *rs) {
#define TAIL_REENTER(next_atom) do { \
    atom = resolve_registry_refs(a, (next_atom)); \
    if (fuel == 0) return; \
    if (fuel > 0) fuel--; \
    goto tail_call; \
} while (0)
tail_call: ;
    if (atom->kind != ATOM_EXPR || atom->expr.len == 0) {
        result_set_add(rs, atom);
        return;
    }
    if (atom_is_error(atom) || atom_is_empty(atom)) {
        result_set_add(rs, atom);
        return;
    }

    uint32_t nargs = expr_nargs(atom);

    /* ── Special forms (arguments NOT pre-evaluated) ───────────────────── */

    /* ── superpose ─────────────────────────────────────────────────────── */
    if (expr_head_is(atom, "superpose") && nargs == 1) {
        Atom *list = expr_arg(atom, 0);
        if (list->kind == ATOM_EXPR) {
            for (uint32_t i = 0; i < list->expr.len; i++)
                result_set_add(rs, list->expr.elems[i]);
        }
        return;
    }

    /* ── collapse ──────────────────────────────────────────────────────── */
    if (expr_head_is(atom, "collapse") && nargs == 1) {
        ResultSet inner;
        result_set_init(&inner);
        metta_eval(s, a, NULL,expr_arg(atom, 0), fuel, &inner);
        Atom *collected = atom_expr(a, inner.items, inner.len);
        free(inner.items);
        result_set_add(rs, collected);
        return;
    }

    /* ── cons-atom ─────────────────────────────────────────────────────── */
    if (expr_head_is(atom, "cons-atom") && nargs == 2) {
        Atom *hd = expr_arg(atom, 0);
        Atom *tl = expr_arg(atom, 1);
        if (tl->kind == ATOM_EXPR) {
            Atom **elems = arena_alloc(a, sizeof(Atom *) * (tl->expr.len + 1));
            elems[0] = hd;
            for (uint32_t i = 0; i < tl->expr.len; i++)
                elems[i + 1] = tl->expr.elems[i];
            result_set_add(rs, atom_expr(a, elems, tl->expr.len + 1));
        } else {
            result_set_add(rs, atom_expr2(a, hd, tl));
        }
        return;
    }

    /* ── decons-atom ───────────────────────────────────────────────────── */
    if (expr_head_is(atom, "decons-atom") && nargs == 1) {
        Atom *e = expr_arg(atom, 0);
        if (e->kind == ATOM_EXPR && e->expr.len > 0) {
            Atom *hd = e->expr.elems[0];
            Atom *tl = atom_expr(a, e->expr.elems + 1, e->expr.len - 1);
            result_set_add(rs, atom_expr2(a, hd, tl));
        } else {
            result_set_add(rs, atom);
        }
        return;
    }

    /* ── car-atom / cdr-atom ─────────────────────────────────────────── */
    if (expr_head_is(atom, "car-atom") && nargs == 1) {
        Atom *e = expr_arg(atom, 0);
        if (e->kind == ATOM_EXPR && e->expr.len > 0)
            result_set_add(rs, e->expr.elems[0]);
        else
            result_set_add(rs, atom_empty(a));
        return;
    }
    if (expr_head_is(atom, "cdr-atom") && nargs == 1) {
        Atom *e = expr_arg(atom, 0);
        if (e->kind == ATOM_EXPR && e->expr.len > 0)
            result_set_add(rs, atom_expr(a, e->expr.elems + 1, e->expr.len - 1));
        else
            result_set_add(rs, atom_unit(a));
        return;
    }

    /* ── match (with nested-match fusion + join reordering) ──────────── */
    if (handle_match(s, a, atom, fuel, rs)) {
        return;
    }

    /* ── unify ─────────────────────────────────────────────────────────── */
    if (expr_head_is(atom, "unify") && nargs == 4) {
        Atom *target = expr_arg(atom, 0);
        Atom *pattern = expr_arg(atom, 1);
        Atom *then_br = expr_arg(atom, 2);
        Atom *else_br = expr_arg(atom, 3);
        Bindings b;
        bindings_init(&b);
        if (match_atoms(target, pattern, &b) && !bindings_has_loop(&b)) {
            Atom *result = bindings_apply(&b, a, then_br);
            result_set_add(rs, result);
        } else {
            result_set_add(rs, else_br);
        }
        return;
    }

    /* ── case ──────────────────────────────────────────────────────────── */
    if (expr_head_is(atom, "case") && nargs == 2) {
        ResultSet scrut;
        result_set_init(&scrut);
        metta_eval(s, a, NULL,expr_arg(atom, 0), fuel, &scrut);
        Atom *branches = expr_arg(atom, 1);
        for (uint32_t si = 0; si < scrut.len; si++) {
            Atom *sv = scrut.items[si];
            bool matched = false;
            if (branches->kind == ATOM_EXPR) {
                for (uint32_t i = 0; i < branches->expr.len; i++) {
                    Atom *branch = branches->expr.elems[i];
                    if (branch->kind == ATOM_EXPR && branch->expr.len == 2) {
                        Bindings b;
                        bindings_init(&b);
                        if (simple_match(branch->expr.elems[0], sv, &b)) {
                            Atom *result = bindings_apply(&b, a, branch->expr.elems[1]);
                            metta_eval(s, a, NULL,result, fuel, rs);
                            matched = true;
                            break;
                        }
                    }
                }
            }
            (void)matched;
        }
        free(scrut.items);
        return;
    }

    /* ── switch ────────────────────────────────────────────────────────── */
    if ((expr_head_is(atom, "switch") || expr_head_is(atom, "switch-minimal")) && nargs == 2) {
        Atom *scrutinee = expr_arg(atom, 0);
        Atom *branches = expr_arg(atom, 1);
        if (branches->kind == ATOM_EXPR) {
            for (uint32_t i = 0; i < branches->expr.len; i++) {
                Atom *branch = branches->expr.elems[i];
                if (branch->kind == ATOM_EXPR && branch->expr.len == 2) {
                    Bindings b;
                    bindings_init(&b);
                    if (simple_match(branch->expr.elems[0], scrutinee, &b)) {
                        Atom *result = bindings_apply(&b, a, branch->expr.elems[1]);
                        metta_eval(s, a, NULL,result, fuel, rs);
                        return;
                    }
                }
            }
        }
        return;
    }

    /* ── let* (nested let sugar) ──────────────────────────────────────── */
    if (expr_head_is(atom, "let*") && nargs == 2) {
        Atom *blist = expr_arg(atom, 0);
        Atom *body = expr_arg(atom, 1);
        if (blist->kind != ATOM_EXPR || blist->expr.len == 0) {
            metta_eval(s, a, NULL, body, fuel, rs);
        } else {
            Atom *first = blist->expr.elems[0];
            Atom *rest = atom_expr(a, blist->expr.elems + 1, blist->expr.len - 1);
            if (first->kind == ATOM_EXPR && first->expr.len == 2) {
                Atom *inner = atom_expr3(a, atom_symbol(a, "let*"), rest, body);
                Atom *elems[4] = { atom_symbol(a, "let"),
                    first->expr.elems[0], first->expr.elems[1], inner };
                Atom *desugared = atom_expr(a, elems, 4);
                metta_eval(s, a, NULL, desugared, fuel, rs);
            }
        }
        return;
    }

    /* ── let ───────────────────────────────────────────────────────────── */
    if (expr_head_is(atom, "let") && nargs == 3) {
        Atom *pat = expr_arg(atom, 0);
        Atom *val_expr = expr_arg(atom, 1);
        Atom *body_let = expr_arg(atom, 2);
        ResultSet vals;
        result_set_init(&vals);
        metta_eval(s, a, NULL, val_expr, fuel, &vals);
        if (vals.len == 1) {
            /* Single-result fast path with TCO */
            Bindings b; bindings_init(&b);
            bool ok = false;
            if (pat->kind == ATOM_VAR) {
                bindings_add(&b, pat->name, vals.items[0]);
                ok = true;
            } else {
                ok = simple_match(pat, vals.items[0], &b);
            }
            free(vals.items);
            if (ok) { TAIL_REENTER(bindings_apply(&b, a, body_let)); }
            return;
        }
        /* Multi-result: no TCO */
        for (uint32_t i = 0; i < vals.len; i++) {
            Bindings b; bindings_init(&b);
            if (pat->kind == ATOM_VAR) {
                bindings_add(&b, pat->name, vals.items[i]);
                metta_eval(s, a, NULL, bindings_apply(&b, a, body_let), fuel, rs);
            } else if (simple_match(pat, vals.items[i], &b)) {
                metta_eval(s, a, NULL, bindings_apply(&b, a, body_let), fuel, rs);
            }
        }
        free(vals.items);
        return;
    }

    /* ── chain ─────────────────────────────────────────────────────────── */
    if (expr_head_is(atom, "chain") && nargs == 3) {
        Atom *to_eval = expr_arg(atom, 0);
        Atom *var = expr_arg(atom, 1);
        Atom *tmpl_chain = expr_arg(atom, 2);
        ResultSet inner;
        result_set_init(&inner);
        metta_eval(s, a, NULL, to_eval, fuel, &inner);
        if (inner.len == 0) {
            result_set_add(rs, atom_empty(a));
            free(inner.items);
            return;
        }
        if (inner.len == 1 && !atom_is_empty(inner.items[0])) {
            /* Single-result fast path with TCO */
            Atom *next_atom;
            if (var->kind == ATOM_VAR) {
                Bindings b; bindings_init(&b);
                bindings_add(&b, var->name, inner.items[0]);
                next_atom = bindings_apply(&b, a, tmpl_chain);
            } else {
                next_atom = tmpl_chain;
            }
            free(inner.items);
            TAIL_REENTER(next_atom);
        }
        /* Multi-result: no TCO */
        for (uint32_t i = 0; i < inner.len; i++) {
            Atom *r = inner.items[i];
            if (atom_is_empty(r)) continue;
            if (var->kind == ATOM_VAR) {
                Bindings b; bindings_init(&b);
                bindings_add(&b, var->name, r);
                Atom *subst = bindings_apply(&b, a, tmpl_chain);
                metta_eval(s, a, NULL, subst, fuel, rs);
            } else {
                result_set_add(rs, tmpl_chain);
            }
        }
        if (inner.len == 0)
            result_set_add(rs, atom_empty(a));
        free(inner.items);
        return;
    }

    /* ── function / return ─────────────────────────────────────────────── */
    if (expr_head_is(atom, "function") && nargs == 1) {
        Atom *body = expr_arg(atom, 0);
        ResultSet inner;
        result_set_init(&inner);
        metta_eval(s, a, NULL,body, fuel, &inner);
        for (uint32_t i = 0; i < inner.len; i++) {
            Atom *r = inner.items[i];
            if (expr_head_is(r, "return") && r->expr.len == 2) {
                result_set_add(rs, r->expr.elems[1]);
            } else {
                result_set_add(rs, atom_error(a,
                    atom_expr2(a, atom_symbol(a, "function"), body),
                    atom_symbol(a, "NoReturn")));
            }
        }
        if (inner.len == 0)
            result_set_add(rs, atom_error(a,
                atom_expr2(a, atom_symbol(a, "function"), body),
                atom_symbol(a, "NoReturn")));
        free(inner.items);
        return;
    }

    /* ── assert ────────────────────────────────────────────────────────── */
    if (expr_head_is(atom, "assert") && nargs == 1) {
        ResultSet inner;
        result_set_init(&inner);
        metta_eval(s, a, NULL,expr_arg(atom, 0), fuel, &inner);
        for (uint32_t i = 0; i < inner.len; i++) {
            if (is_true_atom(inner.items[i])) {
                result_set_add(rs, atom_unit(a));
            } else {
                result_set_add(rs, atom_error(a,
                    atom_expr2(a, atom_symbol(a, "assert"), expr_arg(atom, 0)),
                    atom_expr3(a, expr_arg(atom, 0),
                        atom_symbol(a, "not"), atom_symbol(a, "True"))));
            }
        }
        free(inner.items);
        return;
    }

    /* ── return (data, not evaluated further) ──────────────────────────── */
    if (expr_head_is(atom, "return")) {
        result_set_add(rs, atom);
        return;
    }

    /* ── eval (minimal instruction) ────────────────────────────────────── */
    if (expr_head_is(atom, "eval") && nargs == 1) {
        TAIL_REENTER(expr_arg(atom, 0));
    }

    /* ── new-space ──────────────────────────────────────────────────────── */
    if (expr_head_is(atom, "new-space") && nargs == 0) {
        Arena *pa = g_persistent_arena ? g_persistent_arena : a;
        Space *ns = arena_alloc(pa, sizeof(Space));
        space_init(ns);
        result_set_add(rs, atom_space(pa, ns));
        return;
    }

    /* ── with-space-snapshot ───────────────────────────────────────────── */
    if (expr_head_is(atom, "with-space-snapshot") && nargs == 3 && g_registry) {
        Atom *binder = expr_arg(atom, 0);
        Atom *space_ref = resolve_registry_refs(a, expr_arg(atom, 1));
        Atom *body = expr_arg(atom, 2);
        Space *target = resolve_space(g_registry, space_ref);
        if (!target) {
            result_set_add(rs, atom);
            return;
        }
        Space *snapshot = space_snapshot_clone(target, a);
        Atom *snapshot_atom = atom_space(a, snapshot);
        Bindings b;
        bindings_init(&b);
        if (binder->kind == ATOM_VAR) {
            bindings_add(&b, binder->name, snapshot_atom);
            Atom *subst = bindings_apply(&b, a, body);
            metta_eval(s, a, NULL, subst, fuel, rs);
        } else if (simple_match(binder, snapshot_atom, &b)) {
            Atom *subst = bindings_apply(&b, a, body);
            metta_eval(s, a, NULL, subst, fuel, rs);
        }
        return;
    }

    /* ── bind! ─────────────────────────────────────────────────────────── */
    if (expr_head_is(atom, "bind!") && nargs == 2 && g_registry) {
        Atom *name = expr_arg(atom, 0);
        Atom *val_expr = expr_arg(atom, 1);
        ResultSet val_rs;
        result_set_init(&val_rs);
        metta_eval(s, a, NULL, val_expr, fuel, &val_rs);
        Atom *val = (val_rs.len > 0) ? val_rs.items[0] : val_expr;
        if (name->kind == ATOM_SYMBOL) {
            /* Deep-copy to persistent arena so value survives eval_arena reset */
            Arena *dst = g_persistent_arena ? g_persistent_arena : a;
            Atom *stored = (dst == g_persistent_arena)
                ? atom_deep_copy_shared(dst, val)
                : atom_deep_copy(dst, val);
            registry_bind(g_registry, name->name, stored);
        }
        free(val_rs.items);
        result_set_add(rs, atom_unit(a));
        return;
    }

    /* ── add-atom ──────────────────────────────────────────────────────── */
    if (expr_head_is(atom, "add-atom") && nargs == 2 && g_registry) {
        Atom *space_ref = expr_arg(atom, 0);
        Atom *atom_to_add = expr_arg(atom, 1);
        Space *target = resolve_space(g_registry, space_ref);
        if (target) {
            /* Deep-copy to persistent arena so atom survives eval_arena reset */
            Arena *dst = g_persistent_arena ? g_persistent_arena : a;
            Atom *stored = (dst == g_persistent_arena)
                ? atom_deep_copy_shared(dst, atom_to_add)
                : atom_deep_copy(dst, atom_to_add);
            space_add(target, stored);
        }
        result_set_add(rs, atom_unit(a));
        return;
    }

    /* ── add-atom-nodup (dedup variant for forward chaining) ────────────── */
    if (expr_head_is(atom, "add-atom-nodup") && nargs == 2 && g_registry) {
        Atom *space_ref = expr_arg(atom, 0);
        Atom *atom_to_add = expr_arg(atom, 1);
        Space *target = resolve_space(g_registry, space_ref);
        if (target) {
            /* Check if already in space (O(N) but prevents exponential blowup) */
            bool found = false;
            for (uint32_t i = 0; i < target->len && !found; i++)
                if (atom_eq(target->atoms[i], atom_to_add)) found = true;
            if (!found) {
                Arena *dst = g_persistent_arena ? g_persistent_arena : a;
                Atom *stored = (dst == g_persistent_arena)
                    ? atom_deep_copy_shared(dst, atom_to_add)
                    : atom_deep_copy(dst, atom_to_add);
                space_add(target, stored);
            }
        }
        result_set_add(rs, atom_unit(a));
        return;
    }

    /* ── remove-atom ───────────────────────────────────────────────────── */
    if (expr_head_is(atom, "remove-atom") && nargs == 2 && g_registry) {
        Atom *space_ref = expr_arg(atom, 0);
        Atom *atom_to_rm = expr_arg(atom, 1);
        Space *target = resolve_space(g_registry, space_ref);
        if (target) space_remove(target, atom_to_rm);
        result_set_add(rs, atom_unit(a));
        return;
    }

    /* ── get-atoms ─────────────────────────────────────────────────────── */
    if (expr_head_is(atom, "get-atoms") && nargs == 1 && g_registry) {
        Atom *space_ref = expr_arg(atom, 0);
        Space *target = resolve_space(g_registry, space_ref);
        if (target) {
            for (uint32_t i = 0; i < target->len; i++)
                result_set_add(rs, target->atoms[i]);
        }
        return;
    }

    /* ── count-atoms ──────────────────────────────────────────────────── */
    if (expr_head_is(atom, "count-atoms") && nargs == 1 && g_registry) {
        Atom *space_ref = expr_arg(atom, 0);
        Space *target = resolve_space(g_registry, space_ref);
        int64_t count = target ? (int64_t)target->len : 0;
        result_set_add(rs, atom_int(a, count));
        return;
    }

    /* ── collapse-bind ───────────────────────────────────────────────────── */
    if (expr_head_is(atom, "collapse-bind") && nargs == 1) {
        ResultBindSet inner;
        rb_set_init(&inner);
        metta_eval_bind(s, a, expr_arg(atom, 0), fuel, &inner);
        /* Build list of (atom bindings_grounded) pairs.
           HE format: each pair is (atom {  }) where {  } is grounded Bindings.
           Result is a single expression containing all pairs. */
        Atom **pairs = arena_alloc(a, sizeof(Atom *) * inner.len);
        for (uint32_t i = 0; i < inner.len; i++) {
            /* Format bindings as HE's grounded Bindings: { $var = val, ... }
               Empty bindings = {  } (with spaces) */
            char buf[512];
            int pos = 0;
            pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "{");
            if (inner.items[i].env.len == 0) {
                pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "  ");
            } else {
                for (uint32_t j = 0; j < inner.items[i].env.len && pos < 480; j++) {
                    if (j > 0) pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, ", ");
                    else pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, " ");
                    pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "$%s",
                        inner.items[i].env.entries[j].var);
                }
                pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, " ");
            }
            pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "}");
            (void)pos;
            /* Wrap as expression: (atom bindings_symbol) */
            pairs[i] = atom_expr2(a, inner.items[i].atom, atom_symbol(a, buf));
        }
        /* Each pair is a separate result (matching HE behavior) */
        for (uint32_t i = 0; i < inner.len; i++) {
            Atom *wrapped = atom_expr(a, &pairs[i], 1);
            result_set_add(rs, wrapped);
        }
        free(inner.items);
        return;
    }

    /* ── superpose-bind ────────────────────────────────────────────────── */
    if (expr_head_is(atom, "superpose-bind") && nargs == 1) {
        Atom *list = expr_arg(atom, 0);
        if (list->kind == ATOM_EXPR) {
            for (uint32_t i = 0; i < list->expr.len; i++) {
                Atom *pair = list->expr.elems[i];
                if (pair->kind == ATOM_EXPR && pair->expr.len >= 1)
                    result_set_add(rs, pair->expr.elems[0]);
                else
                    result_set_add(rs, pair);
            }
        }
        return;
    }

    /* ── metta (self-referential eval with type/space) ─────────────────── */
    if (expr_head_is(atom, "metta") && nargs == 3 && g_registry) {
        Atom *to_eval = expr_arg(atom, 0);
        Atom *type_arg = expr_arg(atom, 1);
        Atom *space_ref = expr_arg(atom, 2);
        Space *target = resolve_space(g_registry, space_ref);
        if (!target) target = s;
        Atom *etype = atom_is_symbol(type_arg, "%Undefined%") ? NULL : type_arg;
        metta_eval(target, a, etype, to_eval, fuel, rs);
        return;
    }

    /* ── evalc (eval in context space) ─────────────────────────────────── */
    if (expr_head_is(atom, "evalc") && nargs == 2 && g_registry) {
        Atom *to_eval = expr_arg(atom, 0);
        Atom *space_ref = expr_arg(atom, 1);
        Space *target = resolve_space(g_registry, space_ref);
        if (!target) target = s;
        metta_eval(target, a, NULL, to_eval, fuel, rs);
        return;
    }

    /* ── new-state / get-state / change-state! ───────────────────────────── */
    if (expr_head_is(atom, "new-state") && nargs == 1) {
        Atom *initial = expr_arg(atom, 0);
        /* Allocate state in persistent arena (survives eval_arena reset) */
        Arena *pa = g_persistent_arena ? g_persistent_arena : a;
        StateCell *cell = arena_alloc(pa, sizeof(StateCell));
        cell->value = atom_deep_copy(pa, initial);
        /* Infer content type from initial value */
        Atom **itypes;
        uint32_t nit = get_atom_types(s, a, initial, &itypes);
        cell->content_type = (nit > 0) ? atom_deep_copy(pa, itypes[0]) : atom_undefined_type(pa);
        free(itypes);
        result_set_add(rs, atom_state(pa, cell));
        return;
    }
    if (expr_head_is(atom, "get-state") && nargs == 1) {
        /* Evaluate first arg to resolve to a state atom */
        ResultSet ref_rs;
        result_set_init(&ref_rs);
        metta_eval(s, a, NULL, expr_arg(atom, 0), fuel, &ref_rs);
        Atom *state_ref = (ref_rs.len > 0) ? ref_rs.items[0] : expr_arg(atom, 0);
        free(ref_rs.items);
        /* Also resolve through registry if it's a symbol */
        if (state_ref->kind == ATOM_SYMBOL && g_registry) {
            Atom *val = registry_lookup(g_registry, state_ref->name);
            if (val) state_ref = val;
        }
        if (state_ref->kind == ATOM_GROUNDED && state_ref->ground.gkind == GV_STATE) {
            StateCell *cell = (StateCell *)state_ref->ground.ptr;
            result_set_add(rs, cell->value);
        } else {
            result_set_add(rs, atom);
        }
        return;
    }
    if (expr_head_is(atom, "change-state!") && nargs == 2) {
        /* Evaluate first arg to resolve to a state atom */
        ResultSet ref_rs;
        result_set_init(&ref_rs);
        metta_eval(s, a, NULL, expr_arg(atom, 0), fuel, &ref_rs);
        Atom *state_ref = (ref_rs.len > 0) ? ref_rs.items[0] : expr_arg(atom, 0);
        free(ref_rs.items);
        Atom *new_val = expr_arg(atom, 1);
        if (state_ref->kind == ATOM_SYMBOL && g_registry) {
            Atom *val = registry_lookup(g_registry, state_ref->name);
            if (val) state_ref = val;
        }
        if (state_ref->kind == ATOM_GROUNDED && state_ref->ground.gkind == GV_STATE) {
            StateCell *cell = (StateCell *)state_ref->ground.ptr;
            /* Evaluate the new value */
            ResultSet val_rs;
            result_set_init(&val_rs);
            metta_eval(s, a, NULL, new_val, fuel, &val_rs);
            Atom *new_v = (val_rs.len > 0) ? val_rs.items[0] : new_val;
            free(val_rs.items);
            /* Type check: new value must match content type */
            Atom **new_types;
            uint32_t nnt = get_atom_types(s, a, new_v, &new_types);
            bool type_ok = false;
            for (uint32_t ti = 0; ti < nnt; ti++) {
                Bindings tb;
                bindings_init(&tb);
                if (match_types(cell->content_type, new_types[ti], &tb)) {
                    type_ok = true; break;
                }
            }
            free(new_types);
            if (type_ok) {
                cell->value = g_persistent_arena ? atom_deep_copy(g_persistent_arena, new_v) : new_v;
                result_set_add(rs, state_ref);
            } else {
                /* Type mismatch error */
                Atom **full = arena_alloc(a, sizeof(Atom *) * 3);
                full[0] = atom_symbol(a, "change-state!");
                full[1] = expr_arg(atom, 0);
                full[2] = new_val;
                /* Get the actual type again for error message */
                Atom **et;
                uint32_t net = get_atom_types(s, a, new_v, &et);
                Atom *actual_t = (net > 0) ? et[0] : atom_undefined_type(a);
                free(et);
                Atom *reason = atom_expr(a, (Atom*[]){
                    atom_symbol(a, "BadArgType"), atom_int(a, 2),
                    cell->content_type, actual_t
                }, 4);
                result_set_add(rs, atom_error(a, atom_expr(a, full, 3), reason));
            }
        } else {
            result_set_add(rs, atom_unit(a));
        }
        return;
    }

    /* ── pragma! ────────────────────────────────────────────────────────── */
    if (expr_head_is(atom, "pragma!") && nargs == 2) {
        if (atom_is_symbol(expr_arg(atom, 0), "type-check") &&
            atom_is_symbol(expr_arg(atom, 1), "auto"))
            g_type_check_auto = true;
        result_set_add(rs, atom_unit(a));
        return;
    }

    /* ── nop (evaluate for side effects, return unit) ────────────────────── */
    if (expr_head_is(atom, "nop") && nargs == 1) {
        ResultSet inner;
        result_set_init(&inner);
        metta_eval(s, a, NULL, expr_arg(atom, 0), fuel, &inner);
        free(inner.items);
        result_set_add(rs, atom_unit(a));
        return;
    }

    /* ── get-type ───────────────────────────────────────────────────────── */
    if (expr_head_is(atom, "get-type") && nargs == 1) {
        Atom *target = expr_arg(atom, 0);
        /* Resolve registry tokens for get-type */
        if (target->kind == ATOM_SYMBOL && target->name[0] == '&' && g_registry) {
            Atom *val = registry_lookup(g_registry, target->name);
            if (val) target = val;
        }
        Atom **types;
        uint32_t n = get_atom_types(s, a, target, &types);
        /* If only %Undefined% and arg is an expression, try evaluating first */
        if (n == 1 && atom_is_symbol(types[0], "%Undefined%") &&
            target->kind == ATOM_EXPR) {
            free(types);
            ResultSet evr;
            result_set_init(&evr);
            metta_eval(s, a, NULL, target, fuel, &evr);
            if (evr.len > 0) {
                n = get_atom_types(s, a, evr.items[0], &types);
            } else {
                types = cetta_malloc(sizeof(Atom *));
                types[0] = atom_undefined_type(a);
                n = 1;
            }
            free(evr.items);
        }
        for (uint32_t i = 0; i < n; i++)
            result_set_add(rs, types[i]);
        free(types);
        return;
    }

    /* ── get-doc / help! ──────────────────────────────────────────────── */
    /* Current HE oracle behavior for he_g1_docs.metta is to fail the very
       first get-doc query with IncorrectNumberOfArguments and stop the file.
       Mirror that oracle here until the upstream docs behavior is stable. */
    if (expr_head_is(atom, "get-doc") && nargs == 1) {
        result_set_add(rs, atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")));
        return;
    }

    /* ── if (builtin special form — lazy branches) ─────────────────────── */
    if (expr_head_is(atom, "if") && nargs == 3) {
        ResultBindSet cond;
        rb_set_init(&cond);
        metta_eval_bind(s, a, expr_arg(atom, 0), fuel, &cond);
        if (cond.len == 1) {
            /* Single-result fast path with TCO */
            Atom *branch = is_true_atom(cond.items[0].atom)
                ? expr_arg(atom, 1)
                : expr_arg(atom, 2);
            Atom *next = bindings_apply(&cond.items[0].env, a, branch);
            free(cond.items);
            TAIL_REENTER(next);
        }
        for (uint32_t i = 0; i < cond.len; i++) {
            Atom *branch = is_true_atom(cond.items[i].atom)
                ? expr_arg(atom, 1)
                : expr_arg(atom, 2);
            Atom *bound_branch = bindings_apply(&cond.items[i].env, a, branch);
            metta_eval(s, a, NULL, bound_branch, fuel, rs);
        }
        free(cond.items);
        return;
    }

    /* ── assertEqual ───────────────────────────────────────────────────── */
    if (expr_head_is(atom, "assertEqual") && nargs == 2) {
        ResultSet actual, expected;
        result_set_init(&actual);
        result_set_init(&expected);
        metta_eval(s, a, NULL, expr_arg(atom, 0), fuel, &actual);
        metta_eval(s, a, NULL, expr_arg(atom, 1), fuel, &expected);
        bool ok = (actual.len == expected.len);
        if (ok && actual.len > 0) {
            bool *used = calloc(expected.len, sizeof(bool));
            for (uint32_t i = 0; i < actual.len && ok; i++) {
                bool found = false;
                for (uint32_t j = 0; j < expected.len; j++) {
                    if (!used[j] && atom_eq(actual.items[i], expected.items[j])) {
                        used[j] = true; found = true; break;
                    }
                }
                if (!found) ok = false;
            }
            free(used);
        }
        if (ok) {
            free(actual.items);
            free(expected.items);
            result_set_add(rs, atom_unit(a));
        } else {
            /* Build HE-compatible error message:
               \nExpected: [e1, e2]\nGot: [a1, a2]\nMissed/Excessive */
            char buf[2048];
            int pos = 0;
            pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "\nExpected: [");
            for (uint32_t i = 0; i < expected.len; i++) {
                if (i > 0) pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, ", ");
                FILE *tmp = fmemopen(buf + pos, sizeof(buf) - (size_t)pos, "w");
                if (tmp) { atom_print(expected.items[i], tmp); pos += (int)ftell(tmp); fclose(tmp); }
            }
            pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "]\nGot: [");
            for (uint32_t i = 0; i < actual.len; i++) {
                if (i > 0) pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, ", ");
                FILE *tmp = fmemopen(buf + pos, sizeof(buf) - (size_t)pos, "w");
                if (tmp) { atom_print(actual.items[i], tmp); pos += (int)ftell(tmp); fclose(tmp); }
            }
            pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "]");
            /* Missed results (in expected but not actual) */
            bool has_missed = false;
            for (uint32_t i = 0; i < expected.len; i++) {
                bool found = false;
                for (uint32_t j = 0; j < actual.len; j++)
                    if (atom_eq(expected.items[i], actual.items[j])) { found = true; break; }
                if (!found) {
                    pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                        has_missed ? ", " : "\nMissed results: ");
                    has_missed = true;
                    FILE *tmp = fmemopen(buf + pos, sizeof(buf) - (size_t)pos, "w");
                    if (tmp) { atom_print(expected.items[i], tmp); pos += (int)ftell(tmp); fclose(tmp); }
                }
            }
            /* Excessive results (in actual but not expected) */
            bool has_excess = false;
            for (uint32_t i = 0; i < actual.len; i++) {
                bool found = false;
                for (uint32_t j = 0; j < expected.len; j++)
                    if (atom_eq(actual.items[i], expected.items[j])) { found = true; break; }
                if (!found) {
                    pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                        has_excess ? ", " : "\nExcessive results: ");
                    has_excess = true;
                    FILE *tmp = fmemopen(buf + pos, sizeof(buf) - (size_t)pos, "w");
                    if (tmp) { atom_print(actual.items[i], tmp); pos += (int)ftell(tmp); fclose(tmp); }
                }
            }
            free(actual.items);
            free(expected.items);
            result_set_add(rs, atom_error(a,
                atom_expr3(a, atom_symbol(a, "assertEqual"),
                    expr_arg(atom, 0), expr_arg(atom, 1)),
                atom_string(a, buf)));
        }
        return;
    }

    /* ── assertEqualToResult ───────────────────────────────────────────── */
    if (expr_head_is(atom, "assertEqualToResult") && nargs == 2) {
        ResultSet actual;
        result_set_init(&actual);
        metta_eval(s, a, NULL,expr_arg(atom, 0), fuel, &actual);
        Atom *expected_list = expr_arg(atom, 1);
        bool ok = false;
        if (expected_list->kind == ATOM_EXPR) {
            if (actual.len == expected_list->expr.len) {
                ok = true;
                for (uint32_t i = 0; i < actual.len && ok; i++) {
                    if (!atom_eq(actual.items[i], expected_list->expr.elems[i]))
                        ok = false;
                }
            }
        }
        /* Empty expected () matches empty result set */
        if (actual.len == 0 && expected_list->kind == ATOM_EXPR &&
            expected_list->expr.len == 0) {
            ok = true;
        }
        free(actual.items);
        if (ok) {
            result_set_add(rs, atom_unit(a));
        } else {
            result_set_add(rs, atom_error(a,
                atom_expr3(a, atom_symbol(a, "assertEqualToResult"),
                    expr_arg(atom, 0), expected_list),
                atom_string(a, "mismatch")));
        }
        return;
    }

    /* ── assertEqualMsg ──────────────────────────────────────────────────── */
    if (expr_head_is(atom, "assertEqualMsg") && nargs == 3) {
        ResultSet actual, expected;
        result_set_init(&actual);
        result_set_init(&expected);
        metta_eval(s, a, NULL, expr_arg(atom, 0), fuel, &actual);
        metta_eval(s, a, NULL, expr_arg(atom, 1), fuel, &expected);
        bool ok = (actual.len == expected.len);
        if (ok && actual.len > 0) {
            bool *used = calloc(expected.len, sizeof(bool));
            for (uint32_t i = 0; i < actual.len && ok; i++) {
                bool found = false;
                for (uint32_t j = 0; j < expected.len; j++) {
                    if (!used[j] && atom_eq(actual.items[i], expected.items[j])) {
                        used[j] = true; found = true; break;
                    }
                }
                if (!found) ok = false;
            }
            free(used);
        }
        free(actual.items);
        free(expected.items);
        if (ok) {
            result_set_add(rs, atom_unit(a));
        } else {
            /* Error message is the 3rd arg (user-provided string) */
            result_set_add(rs, atom_error(a,
                atom_expr(a, (Atom*[]){atom_symbol(a, "assertEqualMsg"),
                    expr_arg(atom, 0), expr_arg(atom, 1)}, 3),
                expr_arg(atom, 2)));
        }
        return;
    }

    /* ── assertEqualToResultMsg ────────────────────────────────────────── */
    if (expr_head_is(atom, "assertEqualToResultMsg") && nargs == 3) {
        ResultSet actual;
        result_set_init(&actual);
        metta_eval(s, a, NULL, expr_arg(atom, 0), fuel, &actual);
        Atom *expected_list = expr_arg(atom, 1);
        bool ok = false;
        if (expected_list->kind == ATOM_EXPR) {
            if (actual.len == expected_list->expr.len) {
                ok = true;
                for (uint32_t i = 0; i < actual.len && ok; i++) {
                    if (!atom_eq(actual.items[i], expected_list->expr.elems[i]))
                        ok = false;
                }
            }
        }
        if (actual.len == 0 && expected_list->kind == ATOM_EXPR &&
            expected_list->expr.len == 0)
            ok = true;
        free(actual.items);
        if (ok) {
            result_set_add(rs, atom_unit(a));
        } else {
            result_set_add(rs, atom_error(a,
                atom_expr(a, (Atom*[]){atom_symbol(a, "assertEqualToResultMsg"),
                    expr_arg(atom, 0), expected_list}, 3),
                expr_arg(atom, 2)));
        }
        return;
    }

    /* ── assertAlphaEqualToResult (alpha-equivalence comparison) ─────── */
    if (expr_head_is(atom, "assertAlphaEqualToResult") && nargs == 2) {
        ResultSet actual;
        result_set_init(&actual);
        metta_eval(s, a, NULL, expr_arg(atom, 0), fuel, &actual);
        Atom *expected_list = expr_arg(atom, 1);
        /* Alpha-normalize both sides: rename all vars to $v0, $v1, etc. */
        bool ok = false;
        if (expected_list->kind == ATOM_EXPR && actual.len == expected_list->expr.len) {
            ok = true;
            for (uint32_t i = 0; i < actual.len && ok; i++) {
                /* Try matching with bidirectional unification (handles alpha equiv) */
                Bindings b;
                bindings_init(&b);
                if (!match_atoms(actual.items[i], expected_list->expr.elems[i], &b))
                    ok = false;
            }
        }
        if (actual.len == 0 && expected_list->kind == ATOM_EXPR &&
            expected_list->expr.len == 0)
            ok = true;
        free(actual.items);
        if (ok) {
            result_set_add(rs, atom_unit(a));
        } else {
            result_set_add(rs, atom_error(a,
                atom_expr3(a, atom_symbol(a, "assertAlphaEqualToResult"),
                    expr_arg(atom, 0), expected_list),
                atom_string(a, "mismatch")));
        }
        return;
    }

    /* ── assertIncludes ────────────────────────────────────────────────── */
    if (expr_head_is(atom, "assertIncludes") && nargs == 2) {
        ResultSet actual;
        result_set_init(&actual);
        metta_eval(s, a, NULL, expr_arg(atom, 0), fuel, &actual);
        Atom *expected_list = expr_arg(atom, 1);
        /* Check that every expected item is in the actual results */
        bool ok = true;
        if (expected_list->kind == ATOM_EXPR) {
            for (uint32_t i = 0; i < expected_list->expr.len && ok; i++) {
                bool found = false;
                for (uint32_t j = 0; j < actual.len; j++) {
                    if (atom_eq(expected_list->expr.elems[i], actual.items[j])) {
                        found = true; break;
                    }
                }
                if (!found) ok = false;
            }
        }
        if (ok) {
            result_set_add(rs, atom_unit(a));
        } else {
            /* Build error: (assertIncludes error: <expected> not included in result: <actual>) */
            Atom *actual_expr = atom_expr(a, actual.items, actual.len);
            Atom *msg = atom_expr(a, (Atom*[]){
                atom_symbol(a, "assertIncludes"), atom_symbol(a, "error:"),
                expected_list, atom_symbol(a, "not"), atom_symbol(a, "included"),
                atom_symbol(a, "in"), atom_symbol(a, "result:"),
                actual_expr}, 8);
            result_set_add(rs, atom_error(a,
                atom_expr3(a, atom_symbol(a, "assertIncludes"),
                    expr_arg(atom, 0), expected_list),
                msg));
        }
        free(actual.items);
        return;
    }

    {
        Atom *tail_next = NULL;
        if (handle_dispatch(s, a, atom, fuel, &tail_next, rs)) {
            if (tail_next) TAIL_REENTER(tail_next);
            return;
        }
    }
}

static bool typed_dispatch_bind(Space *s, Arena *a, Atom *atom, int fuel, ResultBindSet *rbs) {
    if (atom->kind != ATOM_EXPR || atom->expr.len < 2) return false;

    Atom *op = atom->expr.elems[0];
    Atom **op_types;
    uint32_t n_op_types = get_atom_types(s, a, op, &op_types);

    bool has_func_type = false;
    bool has_non_func_type = false;
    ResultBindSet func_results;
    rb_set_init(&func_results);
    Atom *func_errors[64];
    uint32_t n_func_errors = 0;

    for (uint32_t ti = 0; ti < n_op_types; ti++) {
        if (!is_function_type(op_types[ti])) {
            has_non_func_type = true;
            continue;
        }

        has_func_type = true;
        Atom *errors[64];
        uint32_t n_errors = 0;
        Bindings succs[64];
        uint32_t n_succs = 0;
        Atom *exp_type = atom_undefined_type(a);
        Atom *fresh_ft = rename_vars(a, op_types[ti], fresh_var_suffix());
        if (!check_function_applicable(atom, fresh_ft, exp_type,
                                       s, a, fuel,
                                       errors, &n_errors,
                                       succs, &n_succs)) {
            for (uint32_t ei = 0; ei < n_errors && n_func_errors < 64; ei++)
                func_errors[n_func_errors++] = errors[ei];
            continue;
        }

        Atom *arg_types[32];
        uint32_t narg = get_function_arg_types(fresh_ft, arg_types, 32);
        uint32_t expr_narg = atom->expr.len - 1;
        ResultBindSet *arg_rbs = arena_alloc(a, sizeof(ResultBindSet) * expr_narg);
        Bindings arg_ctx;
        bindings_init(&arg_ctx);
        bool arg_ok = true;
        bool arg_error = false;

        for (uint32_t ai = 0; ai < expr_narg && ai < narg; ai++) {
            rb_set_init(&arg_rbs[ai]);
            Atom *bound_arg = bindings_apply(&arg_ctx, a, atom->expr.elems[ai + 1]);
            if (atom_is_symbol(arg_types[ai], "Atom") ||
                atom->expr.elems[ai + 1]->kind == ATOM_VAR) {
                Bindings empty_b;
                bindings_init(&empty_b);
                rb_set_add(&arg_rbs[ai], atom->expr.elems[ai + 1], &empty_b);
            } else {
                metta_eval_bind(s, a, bound_arg, fuel, &arg_rbs[ai]);
            }
            if (arg_rbs[ai].len == 0) {
                arg_ok = false;
                break;
            }
            if (arg_rbs[ai].len == 1 &&
                atom_is_error(arg_rbs[ai].items[0].atom) &&
                !atom_eq(arg_rbs[ai].items[0].atom, atom->expr.elems[ai + 1])) {
                rb_set_add(&func_results, arg_rbs[ai].items[0].atom,
                           &arg_rbs[ai].items[0].env);
                arg_error = true;
                break;
            }
            bindings_merge_into(&arg_ctx, &arg_rbs[ai].items[0].env);
        }

        if (arg_ok && !arg_error && expr_narg > 0) {
            Atom **prefix = arena_alloc(a, sizeof(Atom *) * expr_narg);
            uint32_t stack_idx[32];
            memset(stack_idx, 0, sizeof(stack_idx));
            for (;;) {
                Bindings combo_ctx;
                bindings_init(&combo_ctx);
                for (uint32_t ai = 0; ai < expr_narg; ai++)
                    bindings_merge_into(&combo_ctx, &arg_rbs[ai].items[stack_idx[ai]].env);

                for (uint32_t ai = 0; ai < expr_narg; ai++) {
                    Atom *raw = arg_rbs[ai].items[stack_idx[ai]].atom;
                    prefix[ai] = bindings_apply(&combo_ctx, a, raw);
                }

                Atom **call_elems = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
                call_elems[0] = op;
                for (uint32_t ai = 0; ai < expr_narg; ai++)
                    call_elems[ai + 1] = prefix[ai];
                Atom *call_atom = atom_expr(a, call_elems, atom->expr.len);

                bool dispatched = false;
                if (call_atom->kind == ATOM_EXPR && call_atom->expr.len >= 2) {
                    Atom *h = call_atom->expr.elems[0];
                    if (h->kind == ATOM_SYMBOL && is_grounded_op(h->name)) {
                        Atom *gr = dispatch_native_op(a, h,
                            call_atom->expr.elems + 1, call_atom->expr.len - 1);
                        if (gr) {
                            ResultBindSet inner;
                            rb_set_init(&inner);
                            metta_eval_bind(s, a, gr, fuel, &inner);
                            for (uint32_t ii = 0; ii < inner.len; ii++) {
                                Bindings merged = combo_ctx;
                                bindings_merge_into(&merged, &inner.items[ii].env);
                                rb_set_add(&func_results, inner.items[ii].atom, &merged);
                            }
                            free(inner.items);
                            dispatched = true;
                        }
                    }
                }
                if (!dispatched) {
                    QueryResults qr;
                    query_results_init(&qr);
                    query_equations(s, call_atom, a, &qr);
                    if (qr.len > 0) {
                        for (uint32_t qi = 0; qi < qr.len; qi++) {
                            ResultBindSet inner;
                            rb_set_init(&inner);
                            metta_eval_bind(s, a, qr.items[qi].result, fuel, &inner);
                            for (uint32_t ii = 0; ii < inner.len; ii++) {
                                Bindings merged = combo_ctx;
                                bindings_merge_into(&merged, &qr.items[qi].bindings);
                                bindings_merge_into(&merged, &inner.items[ii].env);
                                rb_set_add(&func_results, inner.items[ii].atom, &merged);
                            }
                            free(inner.items);
                        }
                        dispatched = true;
                    }
                    free(qr.items);
                }
                if (!dispatched)
                    rb_set_add(&func_results, call_atom, &combo_ctx);

                uint32_t carry = expr_narg;
                while (carry > 0) {
                    carry--;
                    stack_idx[carry]++;
                    if (stack_idx[carry] < arg_rbs[carry].len) break;
                    stack_idx[carry] = 0;
                    if (carry == 0) goto typed_func_done;
                }
            }
        }

typed_func_done:
        for (uint32_t ai = 0; ai < expr_narg && ai < narg; ai++)
            free(arg_rbs[ai].items);
    }
    free(op_types);

    if (func_results.len > 0 && !has_non_func_type) {
        for (uint32_t i = 0; i < func_results.len; i++)
            rb_set_add(rbs, func_results.items[i].atom, &func_results.items[i].env);
        free(func_results.items);
        return true;
    }
    if (func_results.len > 0) {
        for (uint32_t i = 0; i < func_results.len; i++)
            rb_set_add(rbs, func_results.items[i].atom, &func_results.items[i].env);
        free(func_results.items);
        if (rbs->len > 0) return true;
    }
    free(func_results.items);

    if (has_func_type && n_func_errors > 0 &&
        (!has_non_func_type || g_type_check_auto)) {
        Bindings empty;
        bindings_init(&empty);
        for (uint32_t i = 0; i < n_func_errors; i++)
            rb_set_add(rbs, func_errors[i], &empty);
        return true;
    }

    return false;
}

/* ── metta_call_bind: like metta_call but returns bindings ──────────────── */

static void metta_call_bind(Space *s, Arena *a, Atom *atom, int fuel, ResultBindSet *rbs) {
    Bindings empty;
    bindings_init(&empty);

    /* For most ops, just delegate to metta_call and wrap with empty bindings */
    if (atom->kind != ATOM_EXPR || atom->expr.len == 0) {
        rb_set_add(rbs, atom, &empty);
        return;
    }

    uint32_t nargs = expr_nargs(atom);

    /* Check if this is a special form — delegate to metta_call */
    bool is_special = false;
    if (atom->expr.elems[0]->kind == ATOM_SYMBOL) {
        const char *hd = atom->expr.elems[0]->name;
        is_special = (strcmp(hd, "superpose") == 0 || strcmp(hd, "collapse") == 0 ||
                      strcmp(hd, "cons-atom") == 0 || strcmp(hd, "decons-atom") == 0 ||
                      strcmp(hd, "car-atom") == 0 || strcmp(hd, "cdr-atom") == 0 ||
                      strcmp(hd, "match") == 0 || strcmp(hd, "unify") == 0 ||
                      strcmp(hd, "case") == 0 || strcmp(hd, "switch") == 0 ||
                      strcmp(hd, "switch-minimal") == 0 ||
                      strcmp(hd, "let*") == 0 || strcmp(hd, "let") == 0 ||
                      strcmp(hd, "chain") == 0 || strcmp(hd, "function") == 0 ||
                      strcmp(hd, "assert") == 0 || strcmp(hd, "return") == 0 ||
                      strcmp(hd, "eval") == 0 || strcmp(hd, "get-type") == 0 ||
                      strcmp(hd, "new-space") == 0 || strcmp(hd, "bind!") == 0 ||
                      strcmp(hd, "add-atom") == 0 || strcmp(hd, "add-atom-nodup") == 0 ||
                      strcmp(hd, "remove-atom") == 0 ||
                      strcmp(hd, "get-atoms") == 0 || strcmp(hd, "count-atoms") == 0 ||
                      strcmp(hd, "pragma!") == 0 || strcmp(hd, "nop") == 0 ||
                      strcmp(hd, "new-state") == 0 || strcmp(hd, "get-state") == 0 ||
                      strcmp(hd, "change-state!") == 0 ||
                      strcmp(hd, "collapse-bind") == 0 || strcmp(hd, "superpose-bind") == 0 ||
                      strcmp(hd, "metta") == 0 || strcmp(hd, "evalc") == 0 ||
                      strcmp(hd, "if") == 0 ||
                      strcmp(hd, "assertEqual") == 0 ||
                      strcmp(hd, "assertEqualToResult") == 0 ||
                      strcmp(hd, "assertAlphaEqualToResult") == 0 ||
                      strcmp(hd, "assertEqualMsg") == 0 ||
                      strcmp(hd, "assertEqualToResultMsg") == 0 ||
                      strcmp(hd, "assertIncludes") == 0);
    }

    /* Handle match directly (not via metta_call) to preserve bindings.
       match creates bindings ($x=Plato etc.) that must propagate to callers
       for interpret_tuple to thread them to subsequent elements. */
    if (expr_head_is(atom, "match") && nargs == 3) {
        Atom *space_ref2 = expr_arg(atom, 0);
        Atom *pattern = resolve_registry_refs(a, expr_arg(atom, 1));
        Atom *template = resolve_registry_refs(a, expr_arg(atom, 2));
        Space *ms = g_registry ? resolve_space(g_registry, space_ref2) : NULL;
        if (!ms) ms = s;

        /* Conjunction handling */
        if (pattern->kind == ATOM_EXPR && pattern->expr.len >= 3 &&
            atom_is_symbol(pattern->expr.elems[0], ",")) {
            uint32_t n_conjuncts = pattern->expr.len - 1;
            BindingSet matches;
            space_query_conjunction(ms, a, pattern->expr.elems + 1, n_conjuncts,
                                    NULL, &matches);
            for (uint32_t bi = 0; bi < matches.len; bi++) {
                Atom *result = bindings_apply(&matches.items[bi], a, template);
                ResultBindSet inner;
                rb_set_init(&inner);
                metta_eval_bind(s, a, result, fuel, &inner);
                for (uint32_t j = 0; j < inner.len; j++) {
                    Bindings merged = matches.items[bi];
                    for (uint32_t k = 0; k < inner.items[j].env.len; k++)
                        bindings_add(&merged,
                            inner.items[j].env.entries[k].var,
                            inner.items[j].env.entries[k].val);
                    rb_set_add(rbs, inner.items[j].atom, &merged);
                }
                free(inner.items);
            }
            binding_set_free(&matches);
        } else {
            /* Simple match with bindings preservation */
            SubstMatchSet smr;
            smset_init(&smr);
            space_subst_query(ms, a, pattern, &smr);
            for (uint32_t i = 0; i < smr.len; i++) {
                Bindings b;
                Bindings empty;
                bindings_init(&empty);
                if (space_subst_match_with_seed(ms, pattern, &smr.items[i], &empty, a, &b)) {
                    Atom *result = bindings_apply(&b, a, template);
                    ResultBindSet inner;
                    rb_set_init(&inner);
                    metta_eval_bind(s, a, result, fuel, &inner);
                    for (uint32_t j = 0; j < inner.len; j++) {
                        /* Merge match bindings + eval bindings */
                        Bindings merged = b;
                        for (uint32_t k = 0; k < inner.items[j].env.len; k++)
                            bindings_add(&merged,
                                inner.items[j].env.entries[k].var,
                                inner.items[j].env.entries[k].val);
                        rb_set_add(rbs, inner.items[j].atom, &merged);
                    }
                    free(inner.items);
                }
            }
            free(smr.items);
        }
        return;
    }

    if (expr_head_is(atom, "if") && nargs == 3) {
        ResultBindSet cond;
        rb_set_init(&cond);
        metta_eval_bind(s, a, expr_arg(atom, 0), fuel, &cond);
        for (uint32_t i = 0; i < cond.len; i++) {
            Atom *branch = is_true_atom(cond.items[i].atom)
                ? expr_arg(atom, 1)
                : expr_arg(atom, 2);
            Atom *bound_branch = bindings_apply(&cond.items[i].env, a, branch);
            ResultBindSet inner;
            rb_set_init(&inner);
            metta_eval_bind(s, a, bound_branch, fuel, &inner);
            for (uint32_t j = 0; j < inner.len; j++) {
                Bindings merged = cond.items[i].env;
                bindings_merge_into(&merged, &inner.items[j].env);
                rb_set_add(rbs, inner.items[j].atom, &merged);
            }
            free(inner.items);
        }
        free(cond.items);
        return;
    }

    if (expr_head_is(atom, "get-state") && nargs == 1) {
        ResultBindSet refs;
        rb_set_init(&refs);
        metta_eval_bind(s, a, expr_arg(atom, 0), fuel, &refs);
        for (uint32_t i = 0; i < refs.len; i++) {
            Atom *state_ref = bindings_apply(&refs.items[i].env, a, refs.items[i].atom);
            state_ref = resolve_registry_refs(a, state_ref);
            if (state_ref->kind == ATOM_GROUNDED && state_ref->ground.gkind == GV_STATE) {
                StateCell *cell = (StateCell *)state_ref->ground.ptr;
                rb_set_add(rbs, cell->value, &refs.items[i].env);
            } else {
                rb_set_add(rbs, atom, &refs.items[i].env);
            }
        }
        free(refs.items);
        return;
    }

    if (expr_head_is(atom, "change-state!") && nargs == 2) {
        ResultBindSet refs;
        rb_set_init(&refs);
        metta_eval_bind(s, a, expr_arg(atom, 0), fuel, &refs);
        for (uint32_t i = 0; i < refs.len; i++) {
            Atom *state_ref = bindings_apply(&refs.items[i].env, a, refs.items[i].atom);
            state_ref = resolve_registry_refs(a, state_ref);
            if (!(state_ref->kind == ATOM_GROUNDED && state_ref->ground.gkind == GV_STATE)) {
                rb_set_add(rbs, atom_unit(a), &refs.items[i].env);
                continue;
            }

            ResultBindSet vals;
            rb_set_init(&vals);
            Atom *bound_val_expr = bindings_apply(&refs.items[i].env, a, expr_arg(atom, 1));
            metta_eval_bind(s, a, bound_val_expr, fuel, &vals);
            for (uint32_t vi = 0; vi < vals.len; vi++) {
                Bindings merged = refs.items[i].env;
                bindings_merge_into(&merged, &vals.items[vi].env);
                StateCell *cell = (StateCell *)state_ref->ground.ptr;
                Atom *new_v = bindings_apply(&vals.items[vi].env, a, vals.items[vi].atom);
                Atom **new_types;
                uint32_t nnt = get_atom_types(s, a, new_v, &new_types);
                bool type_ok = false;
                for (uint32_t ti = 0; ti < nnt; ti++) {
                    Bindings tb;
                    bindings_init(&tb);
                    if (match_types(cell->content_type, new_types[ti], &tb)) {
                        type_ok = true;
                        break;
                    }
                }
                free(new_types);
                if (type_ok) {
                    cell->value = g_persistent_arena ? atom_deep_copy(g_persistent_arena, new_v) : new_v;
                    rb_set_add(rbs, state_ref, &merged);
                } else {
                    Atom **et;
                    uint32_t net = get_atom_types(s, a, new_v, &et);
                    Atom *actual_t = (net > 0) ? et[0] : atom_undefined_type(a);
                    free(et);
                    Atom *reason = atom_expr(a, (Atom*[]){
                        atom_symbol(a, "BadArgType"), atom_int(a, 2),
                        cell->content_type, actual_t
                    }, 4);
                    rb_set_add(rbs, atom_error(a, atom, reason), &merged);
                }
            }
            free(vals.items);
        }
        free(refs.items);
        return;
    }

    if (is_special) {
        ResultSet rs;
        result_set_init(&rs);
        metta_call(s, a, atom, fuel, &rs);
        for (uint32_t i = 0; i < rs.len; i++)
            rb_set_add(rbs, rs.items[i], &empty);
        free(rs.items);
        return;
    }

    if (typed_dispatch_bind(s, a, atom, fuel, rbs)) {
        return;
    }

    /* Default path: interpret_tuple then equation query / grounded dispatch */
    {
        ResultBindSet tuples;
        rb_set_init(&tuples);
        Atom **prefix = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
        Bindings empty_ctx;
        bindings_init(&empty_ctx);
        interpret_tuple(s, a, atom->expr.elems, atom->expr.len,
                        0, prefix, &empty_ctx, fuel, &tuples);

        for (uint32_t ti = 0; ti < tuples.len; ti++) {
            Atom *call_atom = tuples.items[ti].atom;
            Bindings tuple_bindings = tuples.items[ti].env;

            if (atom_is_empty(call_atom) || atom_is_error(call_atom)) {
                rb_set_add(rbs, call_atom, &empty);
                continue;
            }

            /* Apply accumulated bindings from interpret_tuple */
            call_atom = bindings_apply(&tuple_bindings, a, call_atom);

            /* Try grounded dispatch */
            if (call_atom->kind == ATOM_EXPR && call_atom->expr.len >= 2) {
                Atom *h = call_atom->expr.elems[0];
                if (h->kind == ATOM_SYMBOL && is_grounded_op(h->name)) {
                    Atom *result = dispatch_native_op(a, h,
                        call_atom->expr.elems + 1, call_atom->expr.len - 1);
                    if (result) {
                        rb_set_add(rbs, result, &tuple_bindings);
                        continue;
                    }
                }
            }

            /* Try equation query — merge tuple bindings with query bindings */
            QueryResults qr;
            query_results_init(&qr);
            query_equations(s, call_atom, a, &qr);
            if (qr.len > 0) {
                for (uint32_t i = 0; i < qr.len; i++) {
                    ResultBindSet inner;
                    rb_set_init(&inner);
                    metta_eval_bind(s, a, qr.items[i].result, fuel, &inner);
                    for (uint32_t j = 0; j < inner.len; j++) {
                        Bindings merged = tuple_bindings;
                        for (uint32_t k = 0; k < qr.items[i].bindings.len; k++)
                            bindings_add(&merged,
                                qr.items[i].bindings.entries[k].var,
                                qr.items[i].bindings.entries[k].val);
                        for (uint32_t k = 0; k < inner.items[j].env.len; k++)
                            bindings_add(&merged,
                                inner.items[j].env.entries[k].var,
                                inner.items[j].env.entries[k].val);
                        rb_set_add(rbs, inner.items[j].atom, &merged);
                    }
                    free(inner.items);
                }
                free(qr.items);
                continue;
            }
            free(qr.items);

            rb_set_add(rbs, call_atom, &tuple_bindings);
        }
        free(tuples.items);
    }
    (void)nargs;
}

/* ── Top-level evaluation ───────────────────────────────────────────────── */

void eval_top(Space *s, Arena *a, Atom *expr, ResultSet *rs) {
    metta_eval(s, a, NULL,expr, g_default_fuel, rs);
}

void eval_top_with_registry(Space *s, Arena *a, Arena *persistent, Registry *r, Atom *expr, ResultSet *rs) {
    g_registry = r;
    g_persistent_arena = persistent;
    metta_eval(s, a, NULL, expr, g_default_fuel, rs);
}

void eval_set_default_fuel(int fuel) {
    if (fuel > 0) g_default_fuel = fuel;
}

int eval_get_default_fuel(void) {
    return g_default_fuel;
}

void eval_set_library_context(CettaLibraryContext *ctx) {
    g_library_context = ctx;
}
