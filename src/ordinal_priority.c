#include "ordinal_priority.h"

#include "grounded.h"

static bool natural(const Atom *value, bool positive) {
    if (!value || value->kind != ATOM_GROUNDED ||
        (value->ground.gkind != GV_INT && value->ground.gkind != GV_BIGINT))
        return false;
    Atom zero;
    atom_scalar_leaf_int(&zero, 0);
    int ordering;
    return grounded_compare_numeric_atoms((Atom *)value, &zero, &ordering) &&
        (positive ? ordering > 0 : ordering >= 0);
}

bool cetta_ordinal_cnf_valid(const Atom *atom, SymbolId constructor) {
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len != 2u ||
        !atom_is_symbol_id(atom->expr.elems[0], constructor))
        return false;
    Atom *rows = atom->expr.elems[1];
    if (!rows || rows->kind != ATOM_EXPR)
        return false;
    Atom *previous = NULL;
    for (CettaExprIndex i = 0u; i < rows->expr.len; i++) {
        Atom *row = rows->expr.elems[i];
        if (!row || row->kind != ATOM_EXPR || row->expr.len != 2u ||
            !natural(row->expr.elems[0], false) ||
            !natural(row->expr.elems[1], true))
            return false;
        Atom *exponent = row->expr.elems[0];
        int ordering;
        if (previous && (!grounded_compare_numeric_atoms(previous, exponent, &ordering) ||
                         ordering <= 0))
            return false;
        previous = exponent;
    }
    return true;
}

bool cetta_ordinal_cnf_compare(const Atom *left, const Atom *right,
                              SymbolId constructor, int *ordering) {
    if (!ordering || !cetta_ordinal_cnf_valid(left, constructor) ||
        !cetta_ordinal_cnf_valid(right, constructor))
        return false;
    Atom *ls = left->expr.elems[1], *rs = right->expr.elems[1];
    CettaExprLen common = ls->expr.len < rs->expr.len ? ls->expr.len : rs->expr.len;
    for (CettaExprIndex i = 0u; i < common; i++) {
        Atom *l = ls->expr.elems[i], *r = rs->expr.elems[i];
        for (CettaExprIndex column = 0u; column < 2u; column++) {
            if (!grounded_compare_numeric_atoms(l->expr.elems[column],
                                                r->expr.elems[column], ordering))
                return false;
            if (*ordering)
                return true;
        }
    }
    *ordering = (ls->expr.len > rs->expr.len) - (ls->expr.len < rs->expr.len);
    return true;
}
