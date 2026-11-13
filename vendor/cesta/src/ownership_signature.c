#include "ownership_signature.h"

static bool symbol(const CeAtom *atom, const char *name) {
    return atom && atom->kind == CE_ATOM_SYMBOL && ce_is(atom->text,name);
}

static bool head(const CeAtom *atom, const char *name) {
    return atom && atom->kind == CE_ATOM_LIST && atom->len && symbol(atom->items[0],name);
}

static bool result_refines(const CeAtom *provided, const CeAtom *expected) {
    if (ce_atom_equal(provided,expected)) return true;
    if (!head(provided,"result") || !head(expected,"result") || provided->len != expected->len)
        return false;
    size_t count = provided->len;
    bool nullable_shape = (count == 3 && symbol(provided->items[1],"static")) ||
                          (count == 4 && symbol(provided->items[1],"owned")) ||
                          (count == 7 && symbol(provided->items[1],"borrowed"));
    if (!nullable_shape || !symbol(provided->items[count-1],"nonnull") ||
        !symbol(expected->items[count-1],"nullable")) return false;
    for (size_t i = 0; i+1 < count; ++i)
        if (!ce_atom_equal(provided->items[i],expected->items[i])) return false;
    return true;
}

bool ce_ownership_signature_refines(const CeAtom *provided, const CeAtom *expected) {
    if (ce_atom_equal(provided,expected)) return true;
    if (!head(provided,"signature") || !head(expected,"signature") ||
        provided->len != expected->len || provided->len < 4 || provided->len > 6)
        return false;
    for (size_t i = 1; i < provided->len; ++i) {
        if (i == 3) {
            if (!result_refines(provided->items[i],expected->items[i])) return false;
        } else if (!ce_atom_equal(provided->items[i],expected->items[i])) return false;
    }
    return true;
}
