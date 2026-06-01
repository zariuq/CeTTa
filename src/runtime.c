#include "runtime.h"
#include <string.h>
#include <stdlib.h>

/* Thin wrappers for LLVM-compiled code to call.
   These have stable C-linkage names matching the LLVM IR declarations. */

bool cetta_atom_is_symbol(Atom *a, const char *name) {
    return atom_is_symbol(a, name);
}

bool cetta_atom_is_int(Atom *a, int64_t val) {
    return a && a->kind == ATOM_GROUNDED &&
           a->ground.gkind == GV_INT && a->ground.ival == val;
}

bool cetta_atom_is_bigint(Atom *a, const char *val) {
    return a && a->kind == ATOM_GROUNDED &&
           a->ground.gkind == GV_BIGINT && strcmp(atom_bigint_cstr(a), val) == 0;
}

bool cetta_atom_is_rational(Atom *a, const char *val) {
    return a && a->kind == ATOM_GROUNDED &&
           a->ground.gkind == GV_RATIONAL &&
           cetta_rational_compare_cstr(atom_rational_cstr(a), val) == 0;
}

bool cetta_atom_is_expr(Atom *a) {
    return a && a->kind == ATOM_EXPR;
}

CettaExprLen cetta_expr_len(Atom *a) {
    return (a && a->kind == ATOM_EXPR) ? a->expr.len : 0;
}

Atom *cetta_expr_elem(Atom *a, CettaExprIndex i) {
    if (!a || a->kind != ATOM_EXPR || i >= a->expr.len) return NULL;
    return a->expr.elems[i];
}

Atom *cetta_atom_symbol(Arena *a, const char *name) {
    return atom_symbol(a, name);
}

Atom *cetta_atom_int(Arena *a, int64_t val) {
    return atom_int(a, val);
}

Atom *cetta_atom_bigint(Arena *a, const char *val) {
    return atom_bigint(a, val);
}

Atom *cetta_atom_rational(Arena *a, const char *val) {
    bool too_large = false;
    Atom *out = atom_rational_limited(
        a, val, CETTA_RATIONAL_DEFAULT_MAX_DIGITS, &too_large);
    if (too_large)
        return atom_error(a, atom_symbol(a, val),
                          atom_symbol(a, "RationalTooLarge"));
    return out;
}

Atom *cetta_atom_expr(Arena *a, Atom **elems, CettaExprLen len) {
    return atom_expr(a, elems, len);
}

bool cetta_atom_is_float(Atom *a, double val) {
    return a && a->kind == ATOM_GROUNDED &&
           a->ground.gkind == GV_FLOAT && a->ground.fval == val;
}

bool cetta_atom_is_bool(Atom *a, bool val) {
    return a && a->kind == ATOM_GROUNDED &&
           a->ground.gkind == GV_BOOL && a->ground.bval == val;
}

bool cetta_atom_is_string(Atom *a, const char *val) {
    return a && a->kind == ATOM_GROUNDED &&
           a->ground.gkind == GV_STRING && strcmp(a->ground.sval, val) == 0;
}

bool cetta_atom_eq(Atom *a, Atom *b) {
    return atom_eq(a, b);
}

Atom *cetta_atom_var(Arena *a, const char *name) {
    return atom_var(a, name);
}

Atom *cetta_atom_float(Arena *a, double val) {
    return atom_float(a, val);
}

Atom *cetta_atom_bool(Arena *a, bool val) {
    return atom_bool(a, val);
}

Atom *cetta_atom_string(Arena *a, const char *val) {
    return atom_string(a, val);
}

void cetta_rs_init(ResultSet *rs) {
    result_set_init(rs);
}

ResultSet *cetta_rs_alloc(void) {
    ResultSet *rs = cetta_malloc(sizeof(ResultSet));
    result_set_init(rs);
    return rs;
}

void cetta_rs_free(ResultSet *rs) {
    if (rs) { free(rs->items); free(rs); }
}

void cetta_rs_add(ResultSet *rs, Atom *atom) {
    result_set_add(rs, atom);
}

Atom *cetta_rs_first(ResultSet *rs) {
    return (rs->len > 0) ? rs->items[0] : NULL;
}
