#ifndef CETTA_ORDINAL_PRIORITY_H
#define CETTA_ORDINAL_PRIORITY_H

#include "atom.h"

/* Normalized finite Cantor sums below omega^omega.  Each row is a natural
 * exponent and a positive natural coefficient, in decreasing exponent
 * order.  This order supplies scheduling advice, not eventual service. */
bool cetta_ordinal_cnf_valid(const Atom *atom, SymbolId constructor);
bool cetta_ordinal_cnf_compare(const Atom *left, const Atom *right,
                              SymbolId constructor, int *ordering);

#endif
