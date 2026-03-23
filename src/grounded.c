#include "grounded.h"
#include <string.h>
#include <math.h>

static Atom *grounded_call_expr(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    Atom **elems = arena_alloc(a, sizeof(Atom *) * (nargs + 1));
    elems[0] = head;
    for (uint32_t i = 0; i < nargs; i++)
        elems[i + 1] = args[i];
    return atom_expr(a, elems, nargs + 1);
}

static Atom *grounded_bad_arg_type(Arena *a, Atom *head, Atom **args, uint32_t nargs,
                                   int bad_idx, Atom *expected_type, Atom *actual_atom) {
    Atom *reason = atom_expr(a, (Atom*[]){
        atom_symbol(a, "BadArgType"),
        atom_int(a, bad_idx),
        expected_type,
        get_meta_type(a, actual_atom)
    }, 4);
    return atom_error(a, grounded_call_expr(a, head, args, nargs), reason);
}

bool is_grounded_op(const char *name) {
    return strcmp(name, "+") == 0 || strcmp(name, "-") == 0 ||
           strcmp(name, "*") == 0 || strcmp(name, "/") == 0 ||
           strcmp(name, "%") == 0 || strcmp(name, "<") == 0 ||
           strcmp(name, ">") == 0 || strcmp(name, "<=") == 0 ||
           strcmp(name, ">=") == 0 || strcmp(name, "==") == 0 ||
           strcmp(name, "and") == 0 || strcmp(name, "or") == 0 ||
           strcmp(name, "not") == 0 ||
           strcmp(name, "size-atom") == 0 || strcmp(name, "index-atom") == 0;
}

/* ── Numeric arg extraction (int or float, promote to double) ──────────── */

typedef struct { double val; bool is_float; } NumArg;

static bool get_numeric_arg(Atom *a, NumArg *out) {
    if (a->kind != ATOM_GROUNDED) return false;
    if (a->ground.gkind == GV_INT) {
        out->val = (double)a->ground.ival;
        out->is_float = false;
        return true;
    }
    if (a->ground.gkind == GV_FLOAT) {
        out->val = a->ground.fval;
        out->is_float = true;
        return true;
    }
    return false;
}

/* Return int if both inputs were int and result is exact, otherwise float */
static Atom *make_numeric(Arena *a, double val, bool any_float) {
    /* If result is exact integer, return int (even for float inputs) */
    long lv = (long)val;
    if ((double)lv == val && val >= -9e18 && val <= 9e18)
        return atom_int(a, (int64_t)lv);
    (void)any_float;
    return atom_float(a, val);
}

/* ── Boolean arg extraction (True/False symbols) ──────────────────────── */

static bool get_bool_arg(Atom *a, bool *out) {
    if (atom_is_symbol(a, "True"))  { *out = true;  return true; }
    if (atom_is_symbol(a, "False")) { *out = false; return true; }
    if (a->kind == ATOM_GROUNDED && a->ground.gkind == GV_BOOL) {
        *out = a->ground.bval; return true;
    }
    return false;
}

/* ── Dispatch ──────────────────────────────────────────────────────────── */

Atom *grounded_dispatch(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    if (head->kind != ATOM_SYMBOL) return NULL;
    const char *op = head->name;

    /* ── Expression introspection ─────────────────────────────────────── */
    if (strcmp(op, "size-atom") == 0 && nargs == 1) {
        if (args[0]->kind == ATOM_EXPR)
            return atom_int(a, args[0]->expr.len);
        if (args[0]->kind == ATOM_GROUNDED)
            return grounded_bad_arg_type(a, head, args, nargs, 1,
                                         atom_expression_type(a), args[0]);
        return NULL;
    }

    if (strcmp(op, "index-atom") == 0 && nargs == 2) {
        if (args[0]->kind != ATOM_EXPR) {
            if (args[0]->kind == ATOM_GROUNDED)
                return grounded_bad_arg_type(a, head, args, nargs, 1,
                                             atom_expression_type(a), args[0]);
            return NULL;
        }
        if (args[1]->kind != ATOM_GROUNDED || args[1]->ground.gkind != GV_INT) {
            if (args[1]->kind == ATOM_GROUNDED)
                return grounded_bad_arg_type(a, head, args, nargs, 2,
                                             atom_symbol(a, "Number"), args[1]);
            return NULL;
        }
        int64_t idx = args[1]->ground.ival;
        if (idx < 0 || (uint64_t)idx >= args[0]->expr.len)
            return atom_error(a, grounded_call_expr(a, head, args, nargs),
                              atom_string(a, "Index is out of bounds"));
        return args[0]->expr.elems[idx];
    }

    /* ── Structural equality (any atom type) ───────────────────────────── */
    if (strcmp(op, "==") == 0 && nargs == 2) {
        return atom_eq(args[0], args[1]) ? atom_true(a) : atom_false(a);
    }

    /* ── Boolean ops ───────────────────────────────────────────────────── */
    if (strcmp(op, "not") == 0 && nargs == 1) {
        bool bv;
        if (get_bool_arg(args[0], &bv))
            return bv ? atom_false(a) : atom_true(a);
        return NULL;
    }
    if (nargs == 2) {
        bool bx, by;
        if (strcmp(op, "and") == 0 && get_bool_arg(args[0], &bx) && get_bool_arg(args[1], &by))
            return (bx && by) ? atom_true(a) : atom_false(a);
        if (strcmp(op, "or") == 0 && get_bool_arg(args[0], &bx) && get_bool_arg(args[1], &by))
            return (bx || by) ? atom_true(a) : atom_false(a);
    }

    /* ── Numeric ops ───────────────────────────────────────────────────── */
    if (nargs != 2) return NULL;

    /* Check if this is an arithmetic op that expects numeric args */
    bool is_arith = (strcmp(op, "+") == 0 || strcmp(op, "-") == 0 ||
                     strcmp(op, "*") == 0 || strcmp(op, "/") == 0 ||
                     strcmp(op, "%") == 0 || strcmp(op, "<") == 0 ||
                     strcmp(op, ">") == 0 || strcmp(op, "<=") == 0 ||
                     strcmp(op, ">=") == 0);
    NumArg na = {0, false}, nb = {0, false};
    bool na_ok = get_numeric_arg(args[0], &na);
    bool nb_ok = get_numeric_arg(args[1], &nb);
    if (is_arith && (!na_ok || !nb_ok)) {
        /* Only produce BadArgType for grounded non-numeric args (like strings).
           For symbols and variables, return NULL (expression unchanged) —
           matches HE behavior where type-checker handles symbols. */
        Atom *bad_arg = !na_ok ? args[0] : args[1];
        if (bad_arg->kind == ATOM_GROUNDED) {
            return grounded_bad_arg_type(a, head, args, nargs,
                                         !na_ok ? 1 : 2,
                                         atom_symbol(a, "Number"),
                                         bad_arg);
        }
        return NULL; /* Symbol/variable args → return unchanged */
    }
    if (!na_ok || !nb_ok)
        return NULL;
    /* Both args are numeric from here */
    bool fl = na.is_float || nb.is_float;

    if (strcmp(op, "+") == 0) return make_numeric(a, na.val + nb.val, fl);
    if (strcmp(op, "-") == 0) return make_numeric(a, na.val - nb.val, fl);
    if (strcmp(op, "*") == 0) return make_numeric(a, na.val * nb.val, fl);
    if (strcmp(op, "/") == 0) return nb.val != 0 ? make_numeric(a, na.val / nb.val, fl) : NULL;
    if (strcmp(op, "%") == 0) return nb.val != 0 ? make_numeric(a, fmod(na.val, nb.val), fl) : NULL;
    if (strcmp(op, "<") == 0)  return na.val < nb.val  ? atom_true(a) : atom_false(a);
    if (strcmp(op, ">") == 0)  return na.val > nb.val  ? atom_true(a) : atom_false(a);
    if (strcmp(op, "<=") == 0) return na.val <= nb.val ? atom_true(a) : atom_false(a);
    if (strcmp(op, ">=") == 0) return na.val >= nb.val ? atom_true(a) : atom_false(a);

    return NULL;
}
