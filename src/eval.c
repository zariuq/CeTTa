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
/* pragma! interpreter bare-minimal: unsupported directives stay unreduced */
static bool g_pragma_bare_minimal = false;

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

static void outcome_set_filter_errors_if_success(OutcomeSet *os) {
    bool has_success = false;
    for (uint32_t i = 0; i < os->len; i++) {
        if (!atom_is_error(os->items[i].atom)) {
            has_success = true;
            break;
        }
    }
    if (!has_success) return;

    uint32_t out = 0;
    for (uint32_t i = 0; i < os->len; i++) {
        if (atom_is_error(os->items[i].atom))
            continue;
        if (out != i)
            os->items[out] = os->items[i];
        out++;
    }
    os->len = out;
}

void outcome_set_free(OutcomeSet *os) {
    free(os->items);
    os->items = NULL;
    os->len = os->cap = 0;
}

/* Active importable library set */
static CettaLibraryContext *g_library_context = NULL;

static Atom *make_call_expr(Arena *a, Atom *head, Atom **args, uint32_t nargs);

static const CettaProfile *active_profile(void) {
    return g_library_context ? g_library_context->session.profile : NULL;
}

static Atom *profile_surface_error(Arena *a, Atom *call, const char *surface_name) {
    const CettaProfile *profile = active_profile();
    char buf[256];
    snprintf(buf, sizeof(buf), "surface %s is unavailable in profile %s",
             surface_name, profile ? profile->name : "unknown");
    return atom_error(a, call, atom_symbol(a, buf));
}

static Atom *bad_arg_type_error(Space *s, Arena *a, Atom *call, int64_t arg_index,
                                Atom *expected_type, Atom *actual) {
    Atom **actual_types = NULL;
    uint32_t nat = get_atom_types(s, a, actual, &actual_types);
    Atom *actual_type = (nat > 0) ? actual_types[0] : atom_undefined_type(a);
    Atom *reason = atom_expr(a, (Atom *[]) {
        atom_symbol(a, "BadArgType"),
        atom_int(a, arg_index),
        expected_type,
        actual_type
    }, 4);
    free(actual_types);
    return atom_error(a, call, reason);
}

static Atom *state_bad_arg_type_error(Space *s, Arena *a, Atom *call,
                                      int64_t arg_index, Atom *actual) {
    char fresh_name[48];
    snprintf(fresh_name, sizeof(fresh_name), "$__state#%u", fresh_var_suffix());
    Atom *expected_type =
        atom_expr2(a, atom_symbol(a, "StateMonad"), atom_var(a, fresh_name));
    return bad_arg_type_error(s, a, call, arg_index, expected_type, actual);
}

static Atom *dispatch_native_op(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    if (head && head->kind == ATOM_SYMBOL && active_profile() &&
        !cetta_profile_allows_surface(active_profile(), head->name)) {
        return profile_surface_error(a, make_call_expr(a, head, args, nargs), head->name);
    }
    Atom *result = grounded_dispatch(a, head, args, nargs);
    if (result) return result;
    if (g_library_context) {
        return cetta_library_dispatch_native(g_library_context, a, head, args, nargs);
    }
    return NULL;
}

static Atom *make_call_expr(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    Atom **elems = arena_alloc(a, sizeof(Atom *) * (nargs + 1));
    elems[0] = head;
    for (uint32_t i = 0; i < nargs; i++)
        elems[i + 1] = args[i];
    return atom_expr(a, elems, nargs + 1);
}

static bool is_capture_closure(Atom *atom) {
    return atom && atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_CAPTURE;
}

static Atom *materialize_runtime_token(Space *s, Arena *a, Atom *atom) {
    if (!atom_is_symbol(atom, "capture"))
        return atom;
    Arena *dst = g_persistent_arena ? g_persistent_arena : a;
    CaptureClosure *closure = arena_alloc(dst, sizeof(CaptureClosure));
    closure->space_ptr = s;
    closure->type_check_auto = g_type_check_auto;
    closure->pragma_bare_minimal = g_pragma_bare_minimal;
    return atom_capture(dst, closure);
}

static Atom *result_eval_type_hint(Atom *declared_type, Atom *result_atom) {
    if (result_atom && result_atom->kind == ATOM_EXPR && result_atom->expr.len >= 1 &&
        atom_is_symbol(result_atom->expr.elems[0], "function")) {
        return NULL;
    }
    return declared_type;
}

static void __attribute__((unused)) bindings_merge_into(Bindings *dst, const Bindings *src) {
    for (uint32_t i = 0; i < src->len; i++) {
        bindings_add(dst, src->entries[i].var, src->entries[i].val);
    }
}

static Atom *bindings_to_atom(Arena *a, const Bindings *b) {
    Atom **assigns = NULL;
    if (b->len > 0) {
        assigns = arena_alloc(a, sizeof(Atom *) * b->len);
        for (uint32_t i = 0; i < b->len; i++) {
            assigns[i] = atom_expr2(a,
                atom_symbol(a, b->entries[i].var),
                b->entries[i].val);
        }
    }
    return atom_expr3(a,
        atom_symbol(a, "Bindings"),
        atom_expr(a, assigns, b->len),
        atom_unit(a));
}

static bool bindings_of_atom(Atom *atom, Bindings *out) {
    bindings_init(out);
    if (atom->kind != ATOM_EXPR || atom->expr.len != 3 ||
        !atom_is_symbol(atom->expr.elems[0], "Bindings")) {
        return false;
    }

    Atom *assigns = atom->expr.elems[1];
    Atom *equalities = atom->expr.elems[2];
    if (assigns->kind != ATOM_EXPR || equalities->kind != ATOM_EXPR ||
        equalities->expr.len != 0) {
        return false;
    }

    for (uint32_t i = 0; i < assigns->expr.len; i++) {
        Atom *assign = assigns->expr.elems[i];
        if (assign->kind != ATOM_EXPR || assign->expr.len != 2 ||
            assign->expr.elems[0]->kind != ATOM_SYMBOL) {
            return false;
        }
        if (!bindings_add(out, assign->expr.elems[0]->name, assign->expr.elems[1])) {
            return false;
        }
    }

    return true;
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

static const char *string_like_atom(Atom *atom) {
    if (!atom) return NULL;
    if (atom->kind == ATOM_SYMBOL) return atom->name;
    if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_STRING) {
        return atom->ground.sval;
    }
    return NULL;
}

typedef struct {
    Space *space;
    const char *bind_name;
    bool is_fresh;
} ImportDestination;

static ImportDestination resolve_import_destination(Arena *a, Atom *target, Atom **error_out) {
    ImportDestination dest = {0};
    if (!g_registry || !target || target->kind != ATOM_SYMBOL || target->name[0] != '&') {
        *error_out = atom_symbol(a, "import! destination must be &self or a fresh &name");
        return dest;
    }

    if (strcmp(target->name, "&self") == 0) {
        Space *self = resolve_space(g_registry, target);
        if (!self) {
            *error_out = atom_symbol(a, "import! destination space not found");
        }
        dest.space = self;
        return dest;
    }

    if (registry_lookup(g_registry, target->name)) {
        *error_out = atom_symbol(a,
            "import! destination must be a new &name, or &self");
        return dest;
    }

    Space *ns = cetta_malloc(sizeof(Space));
    space_init(ns);
    dest.space = ns;
    dest.bind_name = target->name;
    dest.is_fresh = true;
    return dest;
}

static void collect_resolved_spaces(Space *s, Arena *a, Atom *space_expr,
                                    int fuel, Space ***spaces_out,
                                    uint32_t *len_out) {
    ResultSet rs;
    result_set_init(&rs);
    metta_eval(s, a, NULL, space_expr, fuel, &rs);

    Space **spaces = NULL;
    uint32_t len = 0;
    if (rs.len > 0) {
        spaces = cetta_malloc(sizeof(Space *) * rs.len);
        for (uint32_t i = 0; i < rs.len; i++) {
            Space *sp = resolve_space(g_registry, rs.items[i]);
            if (sp) spaces[len++] = sp;
        }
    }
    free(rs.items);
    *spaces_out = spaces;
    *len_out = len;
}

static Space *resolve_single_space_arg(Space *s, Arena *a, Atom *space_expr, int fuel) {
    if (!g_registry) return NULL;

    Atom *direct = resolve_registry_refs(a, space_expr);
    Space *sp = resolve_space(g_registry, direct);
    if (sp) return sp;

    ResultSet rs;
    result_set_init(&rs);
    metta_eval(s, a, NULL, space_expr, fuel, &rs);
    for (uint32_t i = 0; i < rs.len; i++) {
        sp = resolve_space(g_registry, rs.items[i]);
        if (sp) break;
    }
    free(rs.items);
    return sp;
}

static Atom *space_arg_error(Arena *a, Atom *call, const char *message) {
    return atom_error(a, call, atom_symbol(a, message));
}

static Atom *call_signature_error(Arena *a, Atom *call, const char *expected) {
    char buf[1024];
    int pos = snprintf(buf, sizeof(buf), "expected: %s, found: ", expected);
    if (pos < 0) pos = 0;
    if ((size_t)pos < sizeof(buf)) {
        FILE *tmp = fmemopen(buf + pos, sizeof(buf) - (size_t)pos, "w");
        if (tmp) {
            atom_print(call, tmp);
            pos += (int)ftell(tmp);
            fclose(tmp);
        }
    }
    buf[sizeof(buf) - 1] = '\0';
    return atom_error(a, call, atom_symbol(a, buf));
}

static void result_set_resolve_registry_refs(Arena *a, ResultSet *rs) {
    for (uint32_t i = 0; i < rs->len; i++) {
        rs->items[i] = resolve_registry_refs(a, rs->items[i]);
    }
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
        Bindings next[64];
        uint32_t nnext = 0;

        for (uint32_t r = 0; r < nresults; r++) {
            bool found = false;
            /* Apply accumulated bindings to expected arg type
               (resolves type variables bound by previous args) */
            Atom *expected = bindings_apply(&results[r], a, arg_types[i]);
            if (atom_is_symbol(expected, "Atom") ||
                atom_is_symbol(expected, "%Undefined%")) {
                if (nnext < 64) next[nnext++] = results[r];
                continue;
            }

            Atom **atypes;
            uint32_t natypes = get_atom_types(s, a, arg, &atypes);
            if (natypes == 0) {
                if (nnext < 64) next[nnext++] = results[r];
                free(atypes);
                continue;
            }
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
            free(atypes);
        }
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

static void metta_call(Space *s, Arena *a, Atom *atom, Atom *etype, int fuel,
                       bool preserve_bindings, OutcomeSet *os);
/* Like metta_eval but also returns bindings produced by equation queries */
static void metta_eval_bind(Space *s, Arena *a, Atom *atom, int fuel, OutcomeSet *os);
/* Like metta_eval but preserves bindings in an OutcomeSet result. */
static void metta_eval_bind_typed(Space *s, Arena *a, Atom *type, Atom *atom, int fuel, OutcomeSet *os);

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
    atom = materialize_runtime_token(s, a, atom);

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
    {
        OutcomeSet os;
        outcome_set_init(&os);
        metta_call(s, a, atom, etype, fuel > 0 ? fuel - 1 : fuel, false, &os);
        outcome_set_filter_errors_if_success(&os);
        for (uint32_t oi = 0; oi < os.len; oi++)
            result_set_add(rs, os.items[oi].atom);
        outcome_set_free(&os);
    }
}

/* ── metta_eval_bind: like metta_eval but returns bindings too ──────────── */
/* Used by interpret_tuple to thread bindings between sub-expressions.
   For most atoms, bindings are empty. For equation queries, bindings
   contain variable assignments from bidirectional matching. */

static void metta_eval_bind(Space *s, Arena *a, Atom *atom, int fuel, OutcomeSet *os) {
    Bindings empty;
    bindings_init(&empty);
    atom = materialize_runtime_token(s, a, atom);

    if (fuel == 0 || atom->kind == ATOM_SYMBOL || atom->kind == ATOM_GROUNDED ||
        atom->kind == ATOM_VAR ||
        (atom->kind == ATOM_EXPR && atom->expr.len == 0)) {
        outcome_set_add(os, atom, &empty);
        return;
    }
    metta_call(s, a, atom, NULL, fuel > 0 ? fuel - 1 : fuel, true, os);
    outcome_set_filter_errors_if_success(os);
}

static void metta_eval_bind_typed(Space *s, Arena *a, Atom *type, Atom *atom, int fuel, OutcomeSet *os) {
    Bindings empty;
    bindings_init(&empty);

    if (!type) {
        metta_eval_bind(s, a, atom, fuel, os);
        return;
    }

    if (fuel == 0) {
        return;
    }

    Atom *etype = type ? type : atom_undefined_type(a);

    if (atom->kind == ATOM_SYMBOL && atom->name[0] == '&' && g_registry) {
        Atom *val = registry_lookup(g_registry, atom->name);
        if (val) {
            outcome_set_add(os, val, &empty);
            return;
        }
    }
    atom = materialize_runtime_token(s, a, atom);

    if (atom_is_empty(atom) || atom_is_error(atom)) {
        outcome_set_add(os, atom, &empty);
        return;
    }

    Atom *meta = get_meta_type(a, atom);
    if (atom_is_symbol(etype, "Atom") || atom_eq(etype, meta) ||
        atom_is_symbol(meta, "Variable")) {
        outcome_set_add(os, atom, &empty);
        return;
    }

    if (atom->kind == ATOM_SYMBOL || atom->kind == ATOM_GROUNDED ||
        (atom->kind == ATOM_EXPR && atom->expr.len == 0)) {
        ResultSet rs;
        result_set_init(&rs);
        type_cast_fn(s, a, atom, etype, fuel, &rs);
        for (uint32_t i = 0; i < rs.len; i++)
            outcome_set_add(os, rs.items[i], &empty);
        result_set_free(&rs);
        return;
    }

    if (atom->kind == ATOM_VAR) {
        outcome_set_add(os, atom, &empty);
        return;
    }

    metta_call(s, a, atom, etype, fuel > 0 ? fuel - 1 : fuel, true, os);
    outcome_set_filter_errors_if_success(os);
}

static void eval_with_prefix_bindings(Space *s, Arena *a, Atom *type, Atom *atom, int fuel,
                                      const Bindings *prefix, OutcomeSet *os) {
    OutcomeSet inner;
    outcome_set_init(&inner);
    metta_eval_bind_typed(s, a, type, atom, fuel, &inner);
    for (uint32_t i = 0; i < inner.len; i++) {
        Bindings merged = *prefix;
        bindings_merge_into(&merged, &inner.items[i].env);
        outcome_set_add(os, inner.items[i].atom, &merged);
    }
    outcome_set_free(&inner);
}

static void eval_direct_outcomes(Space *s, Arena *a, Atom *type, Atom *atom, int fuel,
                                 OutcomeSet *os) {
    Bindings empty;
    bindings_init(&empty);
    eval_with_prefix_bindings(s, a, type, atom, fuel, &empty, os);
}

static void interpret_function_args(Space *s, Arena *a, Atom *op,
                                    Atom **orig_args, Atom **arg_types, uint32_t nargs,
                                    uint32_t idx, Atom **prefix,
                                    const Bindings *env, int fuel, OutcomeSet *os) {
    if (idx == nargs) {
        Atom **call_elems = arena_alloc(a, sizeof(Atom *) * (nargs + 1));
        call_elems[0] = op;
        for (uint32_t i = 0; i < nargs; i++)
            call_elems[i + 1] = prefix[i];
        outcome_set_add(os, atom_expr(a, call_elems, nargs + 1), env);
        return;
    }

    Atom *orig_arg = orig_args[idx];
    Atom *arg_type = bindings_apply((Bindings *)env, a, arg_types[idx]);
    Atom *bound_arg = bindings_apply((Bindings *)env, a, orig_arg);
    if (atom_is_symbol(arg_type, "Atom") || orig_arg->kind == ATOM_VAR) {
        prefix[idx] = bound_arg;
        interpret_function_args(s, a, op, orig_args, arg_types, nargs,
                                idx + 1, prefix, env, fuel, os);
        return;
    }

    OutcomeSet arg_os;
    outcome_set_init(&arg_os);
    metta_eval_bind_typed(s, a, arg_type, bound_arg, fuel, &arg_os);

    for (uint32_t i = 0; i < arg_os.len; i++) {
        Bindings merged = *env;
        bindings_merge_into(&merged, &arg_os.items[i].env);
        if (atom_is_empty_or_error(arg_os.items[i].atom) &&
            !atom_eq(arg_os.items[i].atom, orig_arg)) {
            outcome_set_add(os, arg_os.items[i].atom, &merged);
            continue;
        }
        prefix[idx] = arg_os.items[i].atom;
        interpret_function_args(s, a, op, orig_args, arg_types, nargs,
                                idx + 1, prefix, &merged, fuel, os);
    }
    outcome_set_free(&arg_os);
}

static void dispatch_capture_outcomes(Arena *a, Atom *head, Atom **args, uint32_t nargs,
                                      int fuel, const Bindings *prefix, OutcomeSet *os) {
    if (!is_capture_closure(head))
        return;

    if (nargs != 1) {
        Atom *err = atom_error(a, make_call_expr(a, head, args, nargs),
                               atom_symbol(a, "IncorrectNumberOfArguments"));
        outcome_set_add(os, err, prefix);
        return;
    }

    CaptureClosure *closure = (CaptureClosure *)head->ground.ptr;
    bool old_type_check = g_type_check_auto;
    bool old_bare_minimal = g_pragma_bare_minimal;
    g_type_check_auto = closure->type_check_auto;
    g_pragma_bare_minimal = closure->pragma_bare_minimal;
    eval_with_prefix_bindings((Space *)closure->space_ptr, a, NULL, args[0], fuel, prefix, os);
    g_type_check_auto = old_type_check;
    g_pragma_bare_minimal = old_bare_minimal;
}

static bool try_dynamic_capture_dispatch(Space *s, Arena *a, Atom *atom, Atom *etype, int fuel,
                                         OutcomeSet *os) {
    if (atom->kind != ATOM_EXPR || atom->expr.len < 1)
        return false;

    Atom *op = atom->expr.elems[0];
    OutcomeSet heads;
    outcome_set_init(&heads);
    metta_eval_bind(s, a, op, fuel, &heads);

    bool saw_capture = false;
    bool saw_other = false;
    for (uint32_t hi = 0; hi < heads.len; hi++) {
        Atom *head_atom = heads.items[hi].atom;
        if (atom_is_empty_or_error(head_atom))
            continue;
        if (is_capture_closure(head_atom))
            saw_capture = true;
        else
            saw_other = true;
    }

    if (!saw_capture || saw_other) {
        outcome_set_free(&heads);
        return false;
    }

    Atom *exp_type = etype ? etype : atom_undefined_type(a);
    for (uint32_t hi = 0; hi < heads.len; hi++) {
        Atom *head_atom = heads.items[hi].atom;
        Bindings head_env = heads.items[hi].env;

        if (atom_is_empty_or_error(head_atom)) {
            outcome_set_add(os, head_atom, &head_env);
            continue;
        }

        Atom *head_type = get_grounded_type(a, head_atom);
        Atom *errors[64];
        uint32_t n_errors = 0;
        Bindings succs[64];
        uint32_t n_succs = 0;
        if (!check_function_applicable(atom, head_type, exp_type, s, a, fuel,
                                       errors, &n_errors, succs, &n_succs)) {
            for (uint32_t ei = 0; ei < n_errors; ei++)
                outcome_set_add(os, errors[ei], &head_env);
            continue;
        }

        Atom *arg_types[32];
        uint32_t expr_narg = atom->expr.len - 1;
        get_function_arg_types(head_type, arg_types, 32);

        OutcomeSet call_terms;
        outcome_set_init(&call_terms);
        Atom **prefix = arena_alloc(a, sizeof(Atom *) * expr_narg);
        interpret_function_args(s, a, head_atom, atom->expr.elems + 1, arg_types,
                                expr_narg, 0, prefix, &head_env, fuel, &call_terms);

        for (uint32_t ci = 0; ci < call_terms.len; ci++) {
            Atom *call_atom = call_terms.items[ci].atom;
            Bindings combo_ctx = call_terms.items[ci].env;
            if (call_atom->kind == ATOM_EXPR && call_atom->expr.len >= 1 &&
                is_capture_closure(call_atom->expr.elems[0])) {
                dispatch_capture_outcomes(a, call_atom->expr.elems[0],
                                          call_atom->expr.elems + 1,
                                          call_atom->expr.len - 1,
                                          fuel, &combo_ctx, os);
            } else {
                outcome_set_add(os, call_atom, &combo_ctx);
            }
        }
        outcome_set_free(&call_terms);
    }

    outcome_set_free(&heads);
    return true;
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
handle_match(Space *s, Arena *a, Atom *atom, int fuel, OutcomeSet *os) {
    Bindings _empty; bindings_init(&_empty);
    uint32_t nargs = expr_nargs(atom);
    if (!expr_head_is(atom, "match") || nargs != 3) return false;

    Atom *space_ref = expr_arg(atom, 0);
    Atom *pattern = resolve_registry_refs(a, expr_arg(atom, 1));
    Atom *template = resolve_registry_refs(a, expr_arg(atom, 2));
    Space *ms = resolve_single_space_arg(s, a, space_ref, fuel);
    if (g_registry && !ms) {
        outcome_set_add(os, space_arg_error(a, atom,
            "match expects a space as the first argument"), &_empty);
        return true;
    }
    if (!ms) ms = s;

    if (pattern->kind == ATOM_EXPR && pattern->expr.len >= 3 &&
        atom_is_symbol(pattern->expr.elems[0], ",")) {
        uint32_t n_conjuncts = pattern->expr.len - 1;
        BindingSet matches;
        space_query_conjunction(ms, a, pattern->expr.elems + 1, n_conjuncts,
                                NULL, &matches);
        for (uint32_t bi = 0; bi < matches.len; bi++) {
            Atom *result = bindings_apply(&matches.items[bi], a, template);
            eval_with_prefix_bindings(s, a, NULL, result, fuel, &matches.items[bi], os);
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
                        eval_with_prefix_bindings(s, a, NULL, result, fuel, &mb, os);
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
                eval_with_prefix_bindings(s, a, NULL, result, fuel, &cur_binds[bi], os);
            }
        }
        free(cur_binds);
    }

    return true;
}

static __attribute__((noinline)) bool
handle_dispatch(Space *s, Arena *a, Atom *atom, Atom *etype, int fuel,
                bool preserve_bindings, Atom **tail_next, Atom **tail_type,
                OutcomeSet *os) {
    Bindings _empty; bindings_init(&_empty);
    *tail_next = NULL;
    *tail_type = NULL;
    if (atom->kind != ATOM_EXPR || atom->expr.len < 1) return false;

    Atom *op = atom->expr.elems[0];
    Atom **op_types;
    uint32_t n_op_types = get_atom_types(s, a, op, &op_types);
    bool only_function_types = (n_op_types > 0);
    for (uint32_t ti = 0; ti < n_op_types; ti++) {
        if (!is_function_type(op_types[ti])) {
            only_function_types = false;
            break;
        }
    }

    bool has_func_type = false;
    bool has_non_func_type = false;
    OutcomeSet func_results;
    outcome_set_init(&func_results);
    Atom *func_errors[64];
    uint32_t n_func_errors = 0;

    for (uint32_t ti = 0; ti < n_op_types; ti++) {
        if (is_function_type(op_types[ti])) {
            has_func_type = true;
            Atom *errors[64];
            uint32_t n_errors = 0;
            Bindings succs[64];
            uint32_t n_succs = 0;
            Atom *exp_type = etype ? etype : atom_undefined_type(a);
            Atom *fresh_ft = rename_vars(a, op_types[ti], fresh_var_suffix());
        if (check_function_applicable(atom, fresh_ft, exp_type,
                                      s, a, fuel,
                                      errors, &n_errors,
                                      succs, &n_succs)) {
                Atom *arg_types[32];
                get_function_arg_types(fresh_ft, arg_types, 32);
                Atom *ret_type = get_function_ret_type(fresh_ft);
                if (atom_is_symbol(ret_type, "Expression"))
                    ret_type = atom_undefined_type(a);

                OutcomeSet heads;
                outcome_set_init(&heads);
                metta_eval_bind_typed(s, a, fresh_ft, op, fuel, &heads);

                for (uint32_t hi = 0; hi < heads.len; hi++) {
                    Atom *head_atom = heads.items[hi].atom;
                    Bindings head_env = heads.items[hi].env;
                    Atom *inst_ret_type = bindings_apply(&head_env, a, ret_type);

                    if (atom_is_empty_or_error(head_atom) && !atom_eq(head_atom, op)) {
                        outcome_set_add(&func_results, head_atom, &head_env);
                        continue;
                    }

                    uint32_t expr_narg = atom->expr.len - 1;
                    OutcomeSet call_terms;
                    outcome_set_init(&call_terms);
                    Atom **prefix = arena_alloc(a, sizeof(Atom *) * expr_narg);
                    interpret_function_args(s, a, head_atom, atom->expr.elems + 1, arg_types,
                                            expr_narg, 0, prefix, &head_env, fuel, &call_terms);

                    for (uint32_t ci = 0; ci < call_terms.len; ci++) {
                        Atom *call_atom = call_terms.items[ci].atom;
                        Bindings combo_ctx = call_terms.items[ci].env;

                        bool dispatched = false;
                        if (call_atom->kind == ATOM_EXPR && call_atom->expr.len >= 2) {
                            Atom *h = call_atom->expr.elems[0];
                            if (is_capture_closure(h)) {
                                dispatch_capture_outcomes(a, h,
                                    call_atom->expr.elems + 1, call_atom->expr.len - 1,
                                    fuel, &combo_ctx, &func_results);
                                dispatched = true;
                            } else if (h->kind == ATOM_SYMBOL && is_grounded_op(h->name)) {
                                Atom *gr = dispatch_native_op(a, h,
                                    call_atom->expr.elems + 1, call_atom->expr.len - 1);
                                if (gr) {
                                    if (!preserve_bindings && only_function_types &&
                                        n_op_types == 1 && heads.len == 1 &&
                                        call_terms.len == 1) {
                                        *tail_next = (combo_ctx.len == 0)
                                            ? gr
                                            : bindings_apply(&combo_ctx, a, gr);
                                        *tail_type = inst_ret_type;
                                        outcome_set_free(&call_terms);
                                        outcome_set_free(&heads);
                                        outcome_set_free(&func_results);
                                        free(op_types);
                                        return true;
                                    }
                                    eval_with_prefix_bindings(s, a, inst_ret_type, gr, fuel,
                                        &combo_ctx, &func_results);
                                    dispatched = true;
                                }
                            }
                        }
                        if (!dispatched) {
                            QueryResults qr;
                            query_results_init(&qr);
                            query_equations(s, call_atom, a, &qr);
                            if (qr.len > 0) {
                                if (!preserve_bindings && only_function_types &&
                                    n_op_types == 1 && heads.len == 1 &&
                                    call_terms.len == 1 && qr.len == 1) {
                                    Bindings merged = combo_ctx;
                                    bindings_merge_into(&merged, &qr.items[0].bindings);
                                    Atom *tail_hint = result_eval_type_hint(inst_ret_type,
                                                                            qr.items[0].result);
                                    *tail_next = (merged.len == 0)
                                        ? qr.items[0].result
                                        : bindings_apply(&merged, a, qr.items[0].result);
                                    *tail_type = tail_hint;
                                    free(qr.items);
                                    outcome_set_free(&call_terms);
                                    outcome_set_free(&heads);
                                    outcome_set_free(&func_results);
                                    free(op_types);
                                    return true;
                                }
                                for (uint32_t qi = 0; qi < qr.len; qi++) {
                                    Bindings merged = combo_ctx;
                                    bindings_merge_into(&merged, &qr.items[qi].bindings);
                                    eval_with_prefix_bindings(s, a,
                                        result_eval_type_hint(inst_ret_type, qr.items[qi].result),
                                        qr.items[qi].result, fuel, &merged, &func_results);
                                }
                                dispatched = true;
                            }
                            free(qr.items);
                        }
                        if (!dispatched)
                            outcome_set_add(&func_results, call_atom, &combo_ctx);
                    }
                    outcome_set_free(&call_terms);
                }
                outcome_set_free(&heads);
            } else {
                for (uint32_t ei = 0; ei < n_errors && n_func_errors < 64; ei++)
                    func_errors[n_func_errors++] = errors[ei];
            }
        } else {
            has_non_func_type = true;
        }
    }
    free(op_types);

    if (func_results.len > 0) {
        for (uint32_t i = 0; i < func_results.len; i++)
            outcome_set_add(os, func_results.items[i].atom, &func_results.items[i].env);
        outcome_set_free(&func_results);
        if (!has_non_func_type) return true;
    } else {
        outcome_set_free(&func_results);
    }

    if (has_func_type && n_func_errors > 0 &&
        (!has_non_func_type || g_type_check_auto)) {
        for (uint32_t i = 0; i < n_func_errors; i++)
            outcome_set_add(os, func_errors[i], &_empty);
        if (!has_non_func_type) return true;
    }

    if (has_func_type && !has_non_func_type) {
        return true;
    }

    if (!has_func_type && try_dynamic_capture_dispatch(s, a, atom, etype, fuel, os)) {
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
            Bindings tuple_bindings = tuples.items[0].env;
            Atom *call_atom = bindings_apply(&tuple_bindings, a, tuples.items[0].atom);
            if (atom_is_empty(call_atom) || atom_is_error(call_atom)) {
                outcome_set_add(os, call_atom, &_empty);
                free(tuples.items);
                return true;
            }
            if (call_atom->kind == ATOM_EXPR && call_atom->expr.len >= 2) {
                Atom *h = call_atom->expr.elems[0];
                if (is_capture_closure(h)) {
                    dispatch_capture_outcomes(a, h,
                        call_atom->expr.elems + 1, call_atom->expr.len - 1,
                        fuel, &tuple_bindings, os);
                    free(tuples.items);
                    return true;
                }
                if (h->kind == ATOM_SYMBOL && is_grounded_op(h->name)) {
                    Atom *result = dispatch_native_op(a, h,
                        call_atom->expr.elems + 1, call_atom->expr.len - 1);
                    if (result) {
                        if (!preserve_bindings) {
                            free(tuples.items);
                            *tail_next = (tuple_bindings.len == 0)
                                ? result
                                : bindings_apply(&tuple_bindings, a, result);
                            *tail_type = etype;
                            return true;
                        }
                        eval_with_prefix_bindings(s, a, NULL, result, fuel, &tuple_bindings, os);
                        free(tuples.items);
                        return true;
                    }
                }
            }

            QueryResults qr;
            query_results_init(&qr);
            query_equations(s, call_atom, a, &qr);
            if (qr.len == 1) {
                Bindings merged = tuple_bindings;
                bindings_merge_into(&merged, &qr.items[0].bindings);
                if (!preserve_bindings) {
                    Atom *tail_hint = result_eval_type_hint(etype, qr.items[0].result);
                    *tail_next = (merged.len == 0)
                        ? qr.items[0].result
                        : bindings_apply(&merged, a, qr.items[0].result);
                    *tail_type = tail_hint;
                    free(qr.items);
                    free(tuples.items);
                    return true;
                }
                eval_with_prefix_bindings(s, a,
                    result_eval_type_hint(NULL, qr.items[0].result),
                    qr.items[0].result, fuel, &merged, os);
                free(qr.items);
                free(tuples.items);
                return true;
            }
            if (qr.len > 0) {
                for (uint32_t i = 0; i < qr.len; i++) {
                    Bindings merged = tuple_bindings;
                    bindings_merge_into(&merged, &qr.items[i].bindings);
                    eval_with_prefix_bindings(s, a,
                        result_eval_type_hint(NULL, qr.items[i].result),
                        qr.items[i].result, fuel, &merged, os);
                }
                free(qr.items);
                free(tuples.items);
                return true;
            }
            free(qr.items);
            outcome_set_add(os, call_atom, &tuple_bindings);
            free(tuples.items);
            return true;
        }

        for (uint32_t ti = 0; ti < tuples.len; ti++) {
            Atom *call_atom = tuples.items[ti].atom;
            Bindings tuple_bindings = tuples.items[ti].env;

            if (atom_is_empty(call_atom) || atom_is_error(call_atom)) {
                outcome_set_add(os, call_atom, &_empty);
                continue;
            }

            call_atom = bindings_apply(&tuple_bindings, a, call_atom);

            if (call_atom->kind == ATOM_EXPR && call_atom->expr.len >= 2) {
                Atom *h = call_atom->expr.elems[0];
                if (is_capture_closure(h)) {
                    dispatch_capture_outcomes(a, h,
                        call_atom->expr.elems + 1, call_atom->expr.len - 1,
                        fuel, &tuple_bindings, os);
                    continue;
                }
                if (h->kind == ATOM_SYMBOL && is_grounded_op(h->name)) {
                    Atom *result = dispatch_native_op(a, h,
                        call_atom->expr.elems + 1, call_atom->expr.len - 1);
                    if (result) {
                        eval_with_prefix_bindings(s, a, NULL, result, fuel, &tuple_bindings, os);
                        continue;
                    }
                }
            }

            QueryResults qr;
            query_results_init(&qr);
            query_equations(s, call_atom, a, &qr);
            if (qr.len > 0) {
                for (uint32_t i = 0; i < qr.len; i++) {
                    Bindings merged = tuple_bindings;
                    bindings_merge_into(&merged, &qr.items[i].bindings);
                    eval_with_prefix_bindings(s, a,
                        result_eval_type_hint(NULL, qr.items[i].result),
                        qr.items[i].result, fuel, &merged, os);
                }
                free(qr.items);
                continue;
            }
            free(qr.items);

            outcome_set_add(os, call_atom, &tuple_bindings);
        }
        free(tuples.items);
    }

    return true;
}

/* ── metta_call: dispatch expressions ───────────────────────────────────── */

static void metta_call(Space *s, Arena *a, Atom *atom, Atom *etype, int fuel,
                       bool preserve_bindings, OutcomeSet *os) {
    Bindings _empty; bindings_init(&_empty);
    if (!etype) etype = atom_undefined_type(a);
#define TAIL_REENTER(next_atom) do { \
    atom = resolve_registry_refs(a, (next_atom)); \
    if (fuel == 0) return; \
    if (fuel > 0) fuel--; \
    goto tail_call; \
} while (0)
tail_call: ;
    atom = materialize_runtime_token(s, a, atom);
    if (atom_is_error(atom) || atom_is_empty(atom)) {
        outcome_set_add(os, atom, &_empty);
        return;
    }

    Atom *meta = get_meta_type(a, atom);
    if (atom_is_symbol(etype, "Atom") || atom_eq(etype, meta) ||
        atom_is_symbol(meta, "Variable")) {
        outcome_set_add(os, atom, &_empty);
        return;
    }

    if (atom->kind == ATOM_SYMBOL || atom->kind == ATOM_GROUNDED ||
        (atom->kind == ATOM_EXPR && atom->expr.len == 0)) {
        ResultSet rs;
        result_set_init(&rs);
        type_cast_fn(s, a, atom, etype, fuel, &rs);
        for (uint32_t i = 0; i < rs.len; i++)
            outcome_set_add(os, rs.items[i], &_empty);
        result_set_free(&rs);
        return;
    }

    if (atom->kind == ATOM_VAR) {
        outcome_set_add(os, atom, &_empty);
        return;
    }

    if (atom->kind != ATOM_EXPR || atom->expr.len == 0) {
        outcome_set_add(os, atom, &_empty);
        return;
    }

    uint32_t nargs = expr_nargs(atom);

    /* ── Special forms (arguments NOT pre-evaluated) ───────────────────── */

    /* ── superpose ─────────────────────────────────────────────────────── */
    if (expr_head_is(atom, "superpose")) {
        if (nargs != 1) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Atom *list = expr_arg(atom, 0);
        if (list->kind == ATOM_EXPR) {
            for (uint32_t i = 0; i < list->expr.len; i++)
                outcome_set_add(os, list->expr.elems[i], &_empty);
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
        outcome_set_add(os, collected, &_empty);
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
            outcome_set_add(os, atom_expr(a, elems, tl->expr.len + 1), &_empty);
        } else {
            outcome_set_add(os, atom_expr2(a, hd, tl), &_empty);
        }
        return;
    }

    /* ── union-atom ────────────────────────────────────────────────────── */
    if (expr_head_is(atom, "union-atom") && nargs == 2) {
        Atom *lhs = expr_arg(atom, 0);
        Atom *rhs = expr_arg(atom, 1);
        if (lhs->kind == ATOM_EXPR && rhs->kind == ATOM_EXPR) {
            uint32_t len = lhs->expr.len + rhs->expr.len;
            Atom **elems = arena_alloc(a, sizeof(Atom *) * len);
            for (uint32_t i = 0; i < lhs->expr.len; i++)
                elems[i] = lhs->expr.elems[i];
            for (uint32_t i = 0; i < rhs->expr.len; i++)
                elems[lhs->expr.len + i] = rhs->expr.elems[i];
            outcome_set_add(os, atom_expr(a, elems, len), &_empty);
        } else {
            outcome_set_add(os, atom, &_empty);
        }
        return;
    }

    /* ── decons-atom ───────────────────────────────────────────────────── */
    if (expr_head_is(atom, "decons-atom")) {
        if (nargs != 1) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Atom *e = expr_arg(atom, 0);
        if (e->kind == ATOM_EXPR && e->expr.len > 0) {
            Atom *hd = e->expr.elems[0];
            Atom *tl = atom_expr(a, e->expr.elems + 1, e->expr.len - 1);
            outcome_set_add(os, atom_expr2(a, hd, tl), &_empty);
        } else {
            outcome_set_add(os,
                call_signature_error(a, atom,
                    "(decons-atom (: <expr> Expression))"),
                &_empty);
        }
        return;
    }

    /* ── car-atom / cdr-atom ─────────────────────────────────────────── */
    if (expr_head_is(atom, "car-atom") && nargs == 1) {
        Atom *e = expr_arg(atom, 0);
        if (e->kind == ATOM_EXPR && e->expr.len > 0)
            outcome_set_add(os, e->expr.elems[0], &_empty);
        else
            outcome_set_add(os,
                atom_error(a, atom,
                    atom_string(a, "car-atom expects a non-empty expression as an argument")),
                &_empty);
        return;
    }
    if (expr_head_is(atom, "cdr-atom") && nargs == 1) {
        Atom *e = expr_arg(atom, 0);
        if (e->kind == ATOM_EXPR && e->expr.len > 0)
            outcome_set_add(os, atom_expr(a, e->expr.elems + 1, e->expr.len - 1), &_empty);
        else
            outcome_set_add(os,
                atom_error(a, atom,
                    atom_string(a, "cdr-atom expects a non-empty expression as an argument")),
                &_empty);
        return;
    }

    /* ── match (with nested-match fusion + join reordering) ──────────── */
    if (handle_match(s, a, atom, fuel, os)) {
        return;
    }

    /* ── unify ─────────────────────────────────────────────────────────── */
    if (expr_head_is(atom, "unify")) {
        if (nargs != 4) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Atom *target = expr_arg(atom, 0);
        Atom *pattern = expr_arg(atom, 1);
        Atom *then_br = expr_arg(atom, 2);
        Atom *else_br = expr_arg(atom, 3);
        Bindings b;
        bindings_init(&b);
        if (match_atoms(target, pattern, &b) && !bindings_has_loop(&b)) {
            Atom *result = bindings_apply(&b, a, then_br);
            eval_with_prefix_bindings(s, a, NULL, result, fuel, &b, os);
        } else {
            eval_with_prefix_bindings(s, a, NULL, else_br, fuel, &_empty, os);
        }
        return;
    }

    /* ── case ──────────────────────────────────────────────────────────── */
    if (expr_head_is(atom, "case")) {
        if (nargs != 2) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
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
                            eval_with_prefix_bindings(s, a, NULL, result, fuel, &b, os);
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
    if (expr_head_is(atom, "switch") || expr_head_is(atom, "switch-minimal")) {
        if (nargs != 2) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
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
                        eval_with_prefix_bindings(s, a, NULL, result, fuel, &b, os);
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
            eval_direct_outcomes(s, a, NULL, body, fuel, os);
        } else {
            Atom *first = blist->expr.elems[0];
            Atom *rest = atom_expr(a, blist->expr.elems + 1, blist->expr.len - 1);
            if (first->kind == ATOM_EXPR && first->expr.len == 2) {
                Atom *inner = atom_expr3(a, atom_symbol(a, "let*"), rest, body);
                Atom *elems[4] = { atom_symbol(a, "let"),
                    first->expr.elems[0], first->expr.elems[1], inner };
                Atom *desugared = atom_expr(a, elems, 4);
                eval_direct_outcomes(s, a, NULL, desugared, fuel, os);
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
        bool all_errors = vals.len > 0;
        for (uint32_t i = 0; i < vals.len; i++) {
            if (!atom_is_error(vals.items[i])) {
                all_errors = false;
                break;
            }
        }
        if (all_errors) {
            for (uint32_t i = 0; i < vals.len; i++)
                outcome_set_add(os, vals.items[i], &_empty);
            free(vals.items);
            return;
        }
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
                eval_with_prefix_bindings(s, a, NULL, bindings_apply(&b, a, body_let), fuel, &b, os);
            } else if (simple_match(pat, vals.items[i], &b)) {
                eval_with_prefix_bindings(s, a, NULL, bindings_apply(&b, a, body_let), fuel, &b, os);
            }
        }
        free(vals.items);
        return;
    }

    /* ── chain ─────────────────────────────────────────────────────────── */
    if (expr_head_is(atom, "chain")) {
        if (nargs != 3) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Atom *to_eval = expr_arg(atom, 0);
        Atom *var = expr_arg(atom, 1);
        Atom *tmpl_chain = expr_arg(atom, 2);
        ResultSet inner;
        result_set_init(&inner);
        metta_eval(s, a, NULL, to_eval, fuel, &inner);
        if (inner.len == 0) {
            outcome_set_add(os, atom_empty(a), &_empty);
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
                eval_with_prefix_bindings(s, a, NULL, subst, fuel, &b, os);
            } else {
                outcome_set_add(os, tmpl_chain, &_empty);
            }
        }
        if (inner.len == 0)
            outcome_set_add(os, atom_empty(a), &_empty);
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
                outcome_set_add(os, r->expr.elems[1], &_empty);
            } else {
                outcome_set_add(os, atom_error(a,
                    atom_expr2(a, atom_symbol(a, "function"), body),
                    atom_symbol(a, "NoReturn")), &_empty);
            }
        }
        if (inner.len == 0)
            outcome_set_add(os, atom_error(a,
                atom_expr2(a, atom_symbol(a, "function"), body),
                atom_symbol(a, "NoReturn")), &_empty);
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
                outcome_set_add(os, atom_unit(a), &_empty);
            } else {
                outcome_set_add(os, atom_error(a,
                    atom_expr2(a, atom_symbol(a, "assert"), expr_arg(atom, 0)),
                    atom_expr3(a, expr_arg(atom, 0),
                        atom_symbol(a, "not"), atom_symbol(a, "True"))), &_empty);
            }
        }
        free(inner.items);
        return;
    }

    /* ── return (data, not evaluated further) ──────────────────────────── */
    if (expr_head_is(atom, "return")) {
        outcome_set_add(os, atom, &_empty);
        return;
    }

    /* ── quote ─────────────────────────────────────────────────────────── */
    if (expr_head_is(atom, "quote")) {
        if (nargs == 1) {
            outcome_set_add(os, atom, &_empty);
        } else {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
        }
        return;
    }

    /* ── eval (minimal instruction) ────────────────────────────────────── */
    if (expr_head_is(atom, "eval")) {
        if (nargs != 1) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        TAIL_REENTER(expr_arg(atom, 0));
    }

    /* ── foldl-atom-in-space (clean extension surface) ────────────────── */
    if (expr_head_is(atom, "foldl-atom-in-space")) {
        if (active_profile() &&
            !cetta_profile_allows_surface(active_profile(), "foldl-atom-in-space")) {
            outcome_set_add(os,
                profile_surface_error(a, atom, "foldl-atom-in-space"), &_empty);
            return;
        }
        if (nargs != 6) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Atom **helper_elems = arena_alloc(a, sizeof(Atom *) * 7);
        helper_elems[0] = atom_symbol(a, "_minimal-foldl-atom");
        for (uint32_t i = 0; i < 6; i++) {
            helper_elems[i + 1] = expr_arg(atom, i);
        }
        Atom *helper_call = atom_expr(a, helper_elems, 7);
        Atom *eval_helper = atom_expr2(a, atom_symbol(a, "eval"), helper_call);
        Atom *rewrite = atom_expr2(a, atom_symbol(a, "function"), eval_helper);
        TAIL_REENTER(rewrite);
    }

    /* ── new-space ──────────────────────────────────────────────────────── */
    if (expr_head_is(atom, "new-space")) {
        if (nargs != 0) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Arena *pa = g_persistent_arena ? g_persistent_arena : a;
        Space *ns = arena_alloc(pa, sizeof(Space));
        space_init(ns);
        outcome_set_add(os, atom_space(pa, ns), &_empty);
        return;
    }

    /* ── context-space ─────────────────────────────────────────────────── */
    if (expr_head_is(atom, "context-space")) {
        if (nargs == 0) {
            outcome_set_add(os, atom_space(a, s), &_empty);
        } else {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
        }
        return;
    }

    /* ── call-native ──────────────────────────────────────────────────── */
    if (expr_head_is(atom, "call-native")) {
        /* HE documents this as an internal instruction. Direct user-level
           calls surface as an error instead of silently passing through. */
        outcome_set_add(os, call_signature_error(a, atom,
            "(call-native func args)"), &_empty);
        return;
    }

    /* ── register-module! / import! ───────────────────────────────────── */
    if (expr_head_is(atom, "git-module!") && g_library_context) {
        if (nargs != 1) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        const char *url = string_like_atom(expr_arg(atom, 0));
        Atom *error = NULL;
        if (!url) {
            error = atom_symbol(a, "git-module! expects a URL; use quotes if needed");
        }
        if (!error && cetta_library_register_git_module(g_library_context, url, a, &error)) {
            outcome_set_add(os, atom_unit(a), &_empty);
        } else {
            outcome_set_add(os, atom_error(a, atom,
                error ? error : atom_symbol(a, "git-module! failed")), &_empty);
        }
        return;
    }

    if (expr_head_is(atom, "register-module!") && nargs != 1) {
        outcome_set_add(os,
            atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
            &_empty);
        return;
    }

    if (expr_head_is(atom, "register-module!") && nargs == 1 && g_library_context) {
        const char *path = string_like_atom(expr_arg(atom, 0));
        Atom *error = NULL;
        if (path && cetta_library_register_module(g_library_context, path, a, &error)) {
            outcome_set_add(os, atom_unit(a), &_empty);
        } else {
            outcome_set_add(os, atom_error(a, atom,
                error ? error : atom_symbol(a, "register-module! failed")), &_empty);
        }
        return;
    }

    if (expr_head_is(atom, "import!") && nargs != 2) {
        outcome_set_add(os,
            atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
            &_empty);
        return;
    }

    if (expr_head_is(atom, "import!") && nargs == 2 && g_registry && g_library_context) {
        const char *spec = string_like_atom(expr_arg(atom, 1));
        Atom *error = NULL;
        ImportDestination dest = {0};
        if (spec) {
            dest = resolve_import_destination(a, expr_arg(atom, 0), &error);
        }
        if (!spec && !error) {
            error = atom_symbol(a, "import! expects a module name");
        }
        if (!error && dest.space &&
            cetta_library_import_module(g_library_context, spec, dest.space, dest.is_fresh,
                                        a, g_persistent_arena ? g_persistent_arena : a,
                                        g_registry, fuel, &error)) {
            if (dest.is_fresh) {
                Arena *pa = g_persistent_arena ? g_persistent_arena : a;
                registry_bind(g_registry, dest.bind_name, atom_space(pa, dest.space));
            }
            outcome_set_add(os, atom_unit(a), &_empty);
        } else if (error) {
            if (dest.is_fresh && dest.space) {
                space_free(dest.space);
                free(dest.space);
            }
            outcome_set_add(os, atom_error(a, atom, error), &_empty);
        }
        return;
    }

    if (expr_head_is(atom, "include") && g_library_context) {
        if (nargs != 1) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        const char *spec = string_like_atom(expr_arg(atom, 0));
        Atom *error = NULL;
        if (!spec) {
            error = atom_symbol(a, "include expects a module name argument");
        }
        if (!error &&
            cetta_library_include_module(g_library_context, spec, s, a,
                                        g_persistent_arena ? g_persistent_arena : a,
                                        g_registry, fuel, &error)) {
            outcome_set_add(os, atom_unit(a), &_empty);
        } else {
            outcome_set_add(os, atom_error(a, atom,
                error ? error : atom_symbol(a, "include failed")), &_empty);
        }
        return;
    }

    if (expr_head_is(atom, "mod-space!") && g_library_context) {
        if (nargs != 1) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        const char *spec = string_like_atom(expr_arg(atom, 0));
        Atom *error = NULL;
        if (!spec) {
            error = atom_symbol(a, "mod-space! expects a module name argument");
        }
        Atom *space_atom = NULL;
        if (!error) {
            space_atom = cetta_library_mod_space(g_library_context, spec, a,
                                                g_persistent_arena ? g_persistent_arena : a,
                                                g_registry, fuel, &error);
        }
        if (space_atom) {
            outcome_set_add(os, space_atom, &_empty);
        } else {
            outcome_set_add(os, atom_error(a, atom,
                error ? error : atom_symbol(a, "mod-space! failed")), &_empty);
        }
        return;
    }

    if (expr_head_is(atom, "print-mods!") && g_library_context) {
        if (nargs != 0) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Atom *error = NULL;
        if (cetta_library_print_loaded_modules(g_library_context, stdout, a, &error)) {
            outcome_set_add(os, atom_unit(a), &_empty);
        } else {
            outcome_set_add(os, atom_error(a, atom,
                error ? error : atom_symbol(a, "print-mods! failed")), &_empty);
        }
        return;
    }

    if (expr_head_is(atom, "module-inventory!") && g_library_context) {
        if (nargs != 0) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        if (active_profile() &&
            !cetta_profile_allows_surface(active_profile(), "module-inventory!")) {
            outcome_set_add(os, profile_surface_error(a, atom, "module-inventory!"), &_empty);
            return;
        }
        Atom *error = NULL;
        Atom *inventory = cetta_library_module_inventory_space(
            g_library_context, a, g_persistent_arena ? g_persistent_arena : a, &error);
        if (inventory) {
            outcome_set_add(os, inventory, &_empty);
        } else {
            outcome_set_add(os, atom_error(a, atom,
                error ? error : atom_symbol(a, "module-inventory! failed")), &_empty);
        }
        return;
    }

    /* ── with-space-snapshot ───────────────────────────────────────────── */
    if (expr_head_is(atom, "with-space-snapshot") && nargs == 3 && g_registry) {
        Atom *binder = expr_arg(atom, 0);
        Atom *space_ref = resolve_registry_refs(a, expr_arg(atom, 1));
        Atom *body = expr_arg(atom, 2);
        Space *target = resolve_space(g_registry, space_ref);
        if (!target) {
            outcome_set_add(os, atom, &_empty);
            return;
        }
        Space *snapshot = space_snapshot_clone(target, a);
        Atom *snapshot_atom = atom_space(a, snapshot);
        Bindings b;
        bindings_init(&b);
        if (binder->kind == ATOM_VAR) {
            bindings_add(&b, binder->name, snapshot_atom);
            Atom *subst = bindings_apply(&b, a, body);
            eval_with_prefix_bindings(s, a, NULL, subst, fuel, &b, os);
        } else if (simple_match(binder, snapshot_atom, &b)) {
            Atom *subst = bindings_apply(&b, a, body);
            eval_with_prefix_bindings(s, a, NULL, subst, fuel, &b, os);
        }
        return;
    }

    /* ── bind! ─────────────────────────────────────────────────────────── */
    if (expr_head_is(atom, "bind!") && nargs != 2) {
        outcome_set_add(os,
            atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
            &_empty);
        return;
    }

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
        outcome_set_add(os, atom_unit(a), &_empty);
        return;
    }

    /* ── add-reduct ────────────────────────────────────────────────────── */
    if (expr_head_is(atom, "add-reduct") && nargs == 2 && g_registry) {
        Space **targets = NULL;
        uint32_t ntargets = 0;
        collect_resolved_spaces(s, a, expr_arg(atom, 0), fuel, &targets, &ntargets);

        OutcomeSet vals;
        outcome_set_init(&vals);
        metta_eval_bind(s, a, expr_arg(atom, 1), fuel, &vals);

        if (ntargets > 0) {
            Arena *dst = g_persistent_arena ? g_persistent_arena : a;
            for (uint32_t ti = 0; ti < ntargets; ti++) {
                for (uint32_t vi = 0; vi < vals.len; vi++) {
                    Atom *stored = (dst == g_persistent_arena)
                        ? atom_deep_copy_shared(dst, vals.items[vi].atom)
                        : atom_deep_copy(dst, vals.items[vi].atom);
                    space_add(targets[ti], stored);
                    outcome_set_add(os, atom_unit(a), &vals.items[vi].env);
                }
            }
        }

        free(targets);
        outcome_set_free(&vals);
        return;
    }

    /* ── add-atom ──────────────────────────────────────────────────────── */
    if (expr_head_is(atom, "add-atom") && nargs == 2 && g_registry) {
        Atom *space_ref = expr_arg(atom, 0);
        Atom *atom_to_add = expr_arg(atom, 1);
        Space *target = resolve_single_space_arg(s, a, space_ref, fuel);
        if (!target) {
            outcome_set_add(os, space_arg_error(a, atom,
                "add-atom expects a space as the first argument"), &_empty);
            return;
        }
        /* Deep-copy to persistent arena so atom survives eval_arena reset */
        Arena *dst = g_persistent_arena ? g_persistent_arena : a;
        Atom *stored = (dst == g_persistent_arena)
            ? atom_deep_copy_shared(dst, atom_to_add)
            : atom_deep_copy(dst, atom_to_add);
        space_add(target, stored);
        outcome_set_add(os, atom_unit(a), &_empty);
        return;
    }

    /* ── add-atom-nodup (dedup variant for forward chaining) ────────────── */
    if (expr_head_is(atom, "add-atom-nodup") && nargs == 2 && g_registry) {
        Atom *space_ref = expr_arg(atom, 0);
        Atom *atom_to_add = expr_arg(atom, 1);
        Space *target = resolve_single_space_arg(s, a, space_ref, fuel);
        if (!target) {
            outcome_set_add(os, space_arg_error(a, atom,
                "add-atom expects a space as the first argument"), &_empty);
            return;
        }
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
        outcome_set_add(os, atom_unit(a), &_empty);
        return;
    }

    /* ── remove-atom ───────────────────────────────────────────────────── */
    if (expr_head_is(atom, "remove-atom") && nargs == 2 && g_registry) {
        Atom *space_ref = expr_arg(atom, 0);
        Atom *atom_to_rm = expr_arg(atom, 1);
        Space *target = resolve_single_space_arg(s, a, space_ref, fuel);
        if (!target) {
            outcome_set_add(os, space_arg_error(a, atom,
                "remove-atom expects a space as the first argument"), &_empty);
            return;
        }
        space_remove(target, atom_to_rm);
        outcome_set_add(os, atom_unit(a), &_empty);
        return;
    }

    /* ── get-atoms ─────────────────────────────────────────────────────── */
    if (expr_head_is(atom, "get-atoms") && nargs == 1 && g_registry) {
        Atom *space_ref = expr_arg(atom, 0);
        Space *target = resolve_single_space_arg(s, a, space_ref, fuel);
        if (!target) {
            outcome_set_add(os, space_arg_error(a, atom,
                "get-atoms expects a space as its argument"), &_empty);
            return;
        }
        for (uint32_t i = 0; i < target->len; i++)
            outcome_set_add(os, target->atoms[i], &_empty);
        return;
    }

    /* ── count-atoms ──────────────────────────────────────────────────── */
    if (expr_head_is(atom, "count-atoms") && nargs == 1 && g_registry) {
        if (active_profile() && !cetta_profile_allows_surface(active_profile(), "count-atoms")) {
            outcome_set_add(os, profile_surface_error(a, atom, "count-atoms"), &_empty);
            return;
        }
        Atom *space_ref = expr_arg(atom, 0);
        Space *target = resolve_single_space_arg(s, a, space_ref, fuel);
        if (!target) {
            outcome_set_add(os, atom, &_empty);
            return;
        }
        outcome_set_add(os, atom_int(a, (int64_t)target->len), &_empty);
        return;
    }

    /* ── collapse-bind ───────────────────────────────────────────────────── */
    if (expr_head_is(atom, "collapse-bind")) {
        if (nargs != 1) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        ResultBindSet inner;
        rb_set_init(&inner);
        metta_eval_bind(s, a, expr_arg(atom, 0), fuel, &inner);
        /* Spec shape: a single expression whose elements are
           (<atom> <Bindings.toAtom>) pairs. */
        Atom **pairs = arena_alloc(a, sizeof(Atom *) * inner.len);
        for (uint32_t i = 0; i < inner.len; i++) {
            pairs[i] = atom_expr2(a, inner.items[i].atom,
                                  bindings_to_atom(a, &inner.items[i].env));
        }
        outcome_set_add(os, atom_expr(a, pairs, inner.len), &_empty);
        rb_set_free(&inner);
        return;
    }

    /* ── superpose-bind ────────────────────────────────────────────────── */
    if (expr_head_is(atom, "superpose-bind")) {
        if (nargs != 1) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Atom *list = expr_arg(atom, 0);
        if (list->kind != ATOM_EXPR) {
            outcome_set_add(os,
                bad_arg_type_error(s, a, atom, 1, atom_expression_type(a), list),
                &_empty);
            return;
        }
        if (list->kind == ATOM_EXPR) {
            for (uint32_t i = 0; i < list->expr.len; i++) {
                Atom *pair = list->expr.elems[i];
                if (pair->kind != ATOM_EXPR || pair->expr.len != 2) continue;
                Bindings restored;
                if (!bindings_of_atom(pair->expr.elems[1], &restored)) continue;
                outcome_set_add(os, pair->expr.elems[0], &restored);
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
        eval_direct_outcomes(target, a, etype, to_eval, fuel, os);
        return;
    }

    /* ── evalc (eval in context space) ─────────────────────────────────── */
    if (expr_head_is(atom, "evalc") && g_registry) {
        if (nargs != 2) {
            outcome_set_add(os, call_signature_error(a, atom,
                "(evalc <atom> <space>)"), &_empty);
            return;
        }
        Atom *to_eval = expr_arg(atom, 0);
        Atom *space_ref = expr_arg(atom, 1);
        Space *target = resolve_single_space_arg(s, a, space_ref, fuel);
        if (!target) {
            outcome_set_add(os, call_signature_error(a, atom,
                "(evalc <atom> <space>)"), &_empty);
            return;
        }
        eval_direct_outcomes(target, a, NULL, to_eval, fuel, os);
        return;
    }

    /* ── new-state / get-state / change-state! ───────────────────────────── */
    if (expr_head_is(atom, "new-state")) {
        if (nargs != 1) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
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
        outcome_set_add(os, atom_state(pa, cell), &_empty);
        return;
    }
    if (expr_head_is(atom, "get-state")) {
        if (nargs != 1) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        ResultBindSet refs;
        rb_set_init(&refs);
        metta_eval_bind(s, a, expr_arg(atom, 0), fuel, &refs);
        for (uint32_t i = 0; i < refs.len; i++) {
            Atom *state_ref = bindings_apply(&refs.items[i].env, a, refs.items[i].atom);
            state_ref = resolve_registry_refs(a, state_ref);
            if (state_ref->kind == ATOM_GROUNDED && state_ref->ground.gkind == GV_STATE) {
                StateCell *cell = (StateCell *)state_ref->ground.ptr;
                outcome_set_add(os, cell->value, &refs.items[i].env);
            } else {
                outcome_set_add(os,
                    state_bad_arg_type_error(s, a, atom, 1, state_ref),
                    &refs.items[i].env);
            }
        }
        outcome_set_free(&refs);
        return;
    }
    if (expr_head_is(atom, "change-state!")) {
        if (nargs != 2) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        ResultBindSet refs;
        rb_set_init(&refs);
        metta_eval_bind(s, a, expr_arg(atom, 0), fuel, &refs);
        for (uint32_t i = 0; i < refs.len; i++) {
            Atom *state_ref = bindings_apply(&refs.items[i].env, a, refs.items[i].atom);
            state_ref = resolve_registry_refs(a, state_ref);
            if (!(state_ref->kind == ATOM_GROUNDED && state_ref->ground.gkind == GV_STATE)) {
                outcome_set_add(os,
                    state_bad_arg_type_error(s, a, atom, 1, state_ref),
                    &refs.items[i].env);
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
                    outcome_set_add(os, state_ref, &merged);
                } else {
                    Atom *error_state_arg = expr_arg(atom, 0);
                    if (error_state_arg->kind == ATOM_SYMBOL &&
                        error_state_arg->name[0] == '&')
                        error_state_arg = state_ref;
                    Atom **full = arena_alloc(a, sizeof(Atom *) * 3);
                    full[0] = atom_symbol(a, "change-state!");
                    full[1] = error_state_arg;
                    full[2] = expr_arg(atom, 1);
                    Atom **et;
                    uint32_t net = get_atom_types(s, a, new_v, &et);
                    Atom *actual_t = (net > 0) ? et[0] : atom_undefined_type(a);
                    free(et);
                    Atom *reason = atom_expr(a, (Atom*[]){
                        atom_symbol(a, "BadArgType"), atom_int(a, 2),
                        cell->content_type, actual_t
                    }, 4);
                    outcome_set_add(os, atom_error(a, atom_expr(a, full, 3), reason), &merged);
                }
            }
            outcome_set_free(&vals);
        }
        outcome_set_free(&refs);
        return;
    }

    /* ── pragma! ────────────────────────────────────────────────────────── */
    if (expr_head_is(atom, "pragma!")) {
        if (nargs < 2) {
            outcome_set_add(os, atom_error(a, atom,
                atom_symbol(a, "pragma! expects key and value as arguments")),
                &_empty);
            return;
        }

        Atom *key = expr_arg(atom, 0);
        Atom *value = expr_arg(atom, 1);
        bool handled = false;

        if (atom_is_symbol(key, "type-check") &&
            atom_is_symbol(value, "auto")) {
            g_type_check_auto = true;
            handled = true;
        } else if (atom_is_symbol(key, "interpreter") &&
                   atom_is_symbol(value, "bare-minimal")) {
            g_pragma_bare_minimal = true;
            handled = true;
        } else if (!g_pragma_bare_minimal &&
                   atom_is_symbol(key, "max-stack-depth")) {
            if (value->kind == ATOM_GROUNDED &&
                value->ground.gkind == GV_INT &&
                value->ground.ival >= 0) {
                eval_set_default_fuel((int)value->ground.ival);
                handled = true;
            } else {
                outcome_set_add(os, atom_error(a, atom,
                    atom_symbol(a, "UnsignedIntegerIsExpected")),
                    &_empty);
                return;
            }
        } else if (!g_pragma_bare_minimal && key->kind != ATOM_SYMBOL) {
            outcome_set_add(os, atom_error(a, atom,
                atom_symbol(a, "pragma! expects symbol atom as a key")),
                &_empty);
            return;
        } else if (!g_pragma_bare_minimal) {
            handled = true;
        }

        outcome_set_add(os, handled ? atom_unit(a) : atom, &_empty);
        return;
    }

    /* ── nop (evaluate for side effects, return unit) ────────────────────── */
    if (expr_head_is(atom, "nop") && nargs == 1) {
        ResultSet inner;
        result_set_init(&inner);
        metta_eval(s, a, NULL, expr_arg(atom, 0), fuel, &inner);
        free(inner.items);
        outcome_set_add(os, atom_unit(a), &_empty);
        return;
    }

    /* ── get-metatype ───────────────────────────────────────────────────── */
    if (expr_head_is(atom, "get-metatype")) {
        if (nargs != 1) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Atom *target = expr_arg(atom, 0);
        if (target->kind == ATOM_SYMBOL && target->name[0] == '&' && g_registry) {
            Atom *val = registry_lookup(g_registry, target->name);
            if (val) target = val;
        }
        outcome_set_add(os, get_meta_type(a, target), &_empty);
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
            outcome_set_add(os, types[i], &_empty);
        free(types);
        return;
    }

    /* ── get-type-space ────────────────────────────────────────────────── */
    if (expr_head_is(atom, "get-type-space")) {
        Atom *resolved_space = NULL;
        if (nargs >= 1) {
            resolved_space = expr_arg(atom, 0);
            if (resolved_space->kind == ATOM_SYMBOL &&
                resolved_space->name[0] == '&' && g_registry) {
                Atom *val = registry_lookup(g_registry, resolved_space->name);
                if (val) resolved_space = val;
            }
        }

        if (nargs != 2) {
            Atom **err_elems = arena_alloc(a, sizeof(Atom *) * (nargs + 1));
            err_elems[0] = atom_symbol(a, "get-type-space");
            for (uint32_t i = 0; i < nargs; i++) {
                Atom *arg = expr_arg(atom, i);
                if (i == 0 && resolved_space) {
                    if (arg->kind == ATOM_SYMBOL && atom_is_symbol(arg, "&self"))
                        arg = atom_symbol(a, "ModuleSpace(GroundingSpace-top)");
                    else
                        arg = resolved_space;
                }
                err_elems[i + 1] = arg;
            }
            outcome_set_add(os, atom_error(a, atom_expr(a, err_elems, nargs + 1),
                atom_symbol(a, "IncorrectNumberOfArguments")), &_empty);
            return;
        }

        if (!(resolved_space &&
              resolved_space->kind == ATOM_GROUNDED &&
              resolved_space->ground.gkind == GV_SPACE)) {
            outcome_set_add(os, atom_error(a, atom,
                atom_symbol(a, "get-type-space expects a space as the first argument")),
                &_empty);
            return;
        }

        Atom *target = expr_arg(atom, 1);
        if (target->kind == ATOM_SYMBOL && target->name[0] == '&' && g_registry) {
            Atom *val = registry_lookup(g_registry, target->name);
            if (val) target = val;
        }

        Atom **types;
        uint32_t n = get_atom_types((Space *)resolved_space->ground.ptr, a, target, &types);
        for (uint32_t i = 0; i < n; i++)
            outcome_set_add(os, types[i], &_empty);
        free(types);
        return;
    }

    /* ── get-doc / help! ──────────────────────────────────────────────── */
    /* Current HE oracle behavior for he_g1_docs.metta is to fail the very
       first get-doc query with IncorrectNumberOfArguments and stop the file.
       Mirror that oracle here until the upstream docs behavior is stable. */
    if (expr_head_is(atom, "get-doc") && nargs == 1) {
        outcome_set_add(os, atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")), &_empty);
        return;
    }

    /* ── assertEqual ───────────────────────────────────────────────────── */
    if (expr_head_is(atom, "assertEqual") && nargs == 2) {
        ResultSet actual, expected;
        result_set_init(&actual);
        result_set_init(&expected);
        metta_eval(s, a, NULL, expr_arg(atom, 0), fuel, &actual);
        metta_eval(s, a, NULL, expr_arg(atom, 1), fuel, &expected);
        result_set_resolve_registry_refs(a, &actual);
        result_set_resolve_registry_refs(a, &expected);
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
            outcome_set_add(os, atom_unit(a), &_empty);
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
            outcome_set_add(os, atom_error(a,
                atom_expr3(a, atom_symbol(a, "assertEqual"),
                    expr_arg(atom, 0), expr_arg(atom, 1)),
                atom_string(a, buf)), &_empty);
        }
        return;
    }

    /* ── assertEqualToResult ───────────────────────────────────────────── */
    if (expr_head_is(atom, "assertEqualToResult") && nargs == 2) {
        ResultSet actual;
        result_set_init(&actual);
        metta_eval(s, a, NULL,expr_arg(atom, 0), fuel, &actual);
        result_set_resolve_registry_refs(a, &actual);
        Atom *expected_list = expr_arg(atom, 1);
        bool ok = false;
        if (expected_list->kind == ATOM_EXPR) {
            if (actual.len == expected_list->expr.len) {
                ok = true;
                for (uint32_t i = 0; i < actual.len && ok; i++) {
                    Atom *expected_item = resolve_registry_refs(a, expected_list->expr.elems[i]);
                    if (!atom_eq(actual.items[i], expected_item))
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
            outcome_set_add(os, atom_unit(a), &_empty);
        } else {
            outcome_set_add(os, atom_error(a,
                atom_expr3(a, atom_symbol(a, "assertEqualToResult"),
                    expr_arg(atom, 0), expected_list),
                atom_string(a, "mismatch")), &_empty);
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
        result_set_resolve_registry_refs(a, &actual);
        result_set_resolve_registry_refs(a, &expected);
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
            outcome_set_add(os, atom_unit(a), &_empty);
        } else {
            /* Error message is the 3rd arg (user-provided string) */
            outcome_set_add(os, atom_error(a,
                atom_expr(a, (Atom*[]){atom_symbol(a, "assertEqualMsg"),
                    expr_arg(atom, 0), expr_arg(atom, 1)}, 3),
                expr_arg(atom, 2)), &_empty);
        }
        return;
    }

    /* ── assertEqualToResultMsg ────────────────────────────────────────── */
    if (expr_head_is(atom, "assertEqualToResultMsg") && nargs == 3) {
        ResultSet actual;
        result_set_init(&actual);
        metta_eval(s, a, NULL, expr_arg(atom, 0), fuel, &actual);
        result_set_resolve_registry_refs(a, &actual);
        Atom *expected_list = expr_arg(atom, 1);
        bool ok = false;
        if (expected_list->kind == ATOM_EXPR) {
            if (actual.len == expected_list->expr.len) {
                ok = true;
                for (uint32_t i = 0; i < actual.len && ok; i++) {
                    Atom *expected_item = resolve_registry_refs(a, expected_list->expr.elems[i]);
                    if (!atom_eq(actual.items[i], expected_item))
                        ok = false;
                }
            }
        }
        if (actual.len == 0 && expected_list->kind == ATOM_EXPR &&
            expected_list->expr.len == 0)
            ok = true;
        free(actual.items);
        if (ok) {
            outcome_set_add(os, atom_unit(a), &_empty);
        } else {
            outcome_set_add(os, atom_error(a,
                atom_expr(a, (Atom*[]){atom_symbol(a, "assertEqualToResultMsg"),
                    expr_arg(atom, 0), expected_list}, 3),
                expr_arg(atom, 2)), &_empty);
        }
        return;
    }

    /* ── assertAlphaEqualToResult (alpha-equivalence comparison) ─────── */
    if (expr_head_is(atom, "assertAlphaEqualToResult") && nargs == 2) {
        ResultSet actual;
        result_set_init(&actual);
        metta_eval(s, a, NULL, expr_arg(atom, 0), fuel, &actual);
        Atom *expected_list = expr_arg(atom, 1);
        bool ok = false;
        if (expected_list->kind == ATOM_EXPR && actual.len == expected_list->expr.len) {
            ok = true;
            for (uint32_t i = 0; i < actual.len && ok; i++) {
                if (!atom_alpha_eq(actual.items[i], expected_list->expr.elems[i]))
                    ok = false;
            }
        }
        if (actual.len == 0 && expected_list->kind == ATOM_EXPR &&
            expected_list->expr.len == 0)
            ok = true;
        free(actual.items);
        if (ok) {
            outcome_set_add(os, atom_unit(a), &_empty);
        } else {
            outcome_set_add(os, atom_error(a,
                atom_expr3(a, atom_symbol(a, "assertAlphaEqualToResult"),
                    expr_arg(atom, 0), expected_list),
                atom_string(a, "mismatch")), &_empty);
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
            outcome_set_add(os, atom_unit(a), &_empty);
        } else {
            /* Build error: (assertIncludes error: <expected> not included in result: <actual>) */
            Atom *actual_expr = atom_expr(a, actual.items, actual.len);
            Atom *msg = atom_expr(a, (Atom*[]){
                atom_symbol(a, "assertIncludes"), atom_symbol(a, "error:"),
                expected_list, atom_symbol(a, "not"), atom_symbol(a, "included"),
                atom_symbol(a, "in"), atom_symbol(a, "result:"),
                actual_expr}, 8);
            outcome_set_add(os, atom_error(a,
                atom_expr3(a, atom_symbol(a, "assertIncludes"),
                    expr_arg(atom, 0), expected_list),
                msg), &_empty);
        }
        free(actual.items);
        return;
    }

    {
        Atom *tail_next = NULL;
        Atom *tail_type = NULL;
        if (handle_dispatch(s, a, atom, etype, fuel, preserve_bindings,
                            &tail_next, &tail_type, os)) {
            if (tail_next) {
                if (tail_type) etype = tail_type;
                TAIL_REENTER(tail_next);
            }
            return;
        }
    }
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
    g_default_fuel = fuel > 0 ? fuel : -1;
}

int eval_get_default_fuel(void) {
    return g_default_fuel;
}

void eval_set_library_context(CettaLibraryContext *ctx) {
    g_library_context = ctx;
}
