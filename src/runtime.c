#include "runtime.h"
#include <string.h>

/* Thin wrappers for LLVM-compiled code to call.
   These have stable C-linkage names matching the LLVM IR declarations. */

bool cetta_atom_is_symbol(Atom *a, const char *name) {
    return a && a->kind == ATOM_SYMBOL && strcmp(a->name, name) == 0;
}

bool cetta_atom_is_int(Atom *a, int64_t val) {
    return a && a->kind == ATOM_GROUNDED &&
           a->ground.gkind == GV_INT && a->ground.ival == val;
}

bool cetta_atom_is_expr(Atom *a) {
    return a && a->kind == ATOM_EXPR;
}

uint32_t cetta_expr_len(Atom *a) {
    return (a && a->kind == ATOM_EXPR) ? a->expr.len : 0;
}

Atom *cetta_expr_elem(Atom *a, uint32_t i) {
    if (!a || a->kind != ATOM_EXPR || i >= a->expr.len) return NULL;
    return a->expr.elems[i];
}

Atom *cetta_atom_symbol(Arena *a, const char *name) {
    return atom_symbol(a, name);
}

Atom *cetta_atom_int(Arena *a, int64_t val) {
    return atom_int(a, val);
}

Atom *cetta_atom_expr(Arena *a, Atom **elems, uint32_t len) {
    return atom_expr(a, elems, len);
}

void cetta_rs_add(ResultSet *rs, Atom *atom) {
    result_set_add(rs, atom);
}
