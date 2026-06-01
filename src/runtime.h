#ifndef CETTA_RUNTIME_H
#define CETTA_RUNTIME_H

/* Runtime functions exported for LLVM-compiled MeTTa code.
   These are thin wrappers around atom.c/space.c functions,
   with C-linkage names that match the LLVM IR declarations. */

#include "atom.h"
#include "space.h"
#include "eval.h"

/* Pattern matching predicates */
bool cetta_atom_is_symbol(Atom *a, const char *name);
bool cetta_atom_is_int(Atom *a, int64_t val);
bool cetta_atom_is_bigint(Atom *a, const char *val);
bool cetta_atom_is_rational(Atom *a, const char *val);
bool cetta_atom_is_expr(Atom *a);
CettaExprLen cetta_expr_len(Atom *a);
Atom *cetta_expr_elem(Atom *a, CettaExprIndex i);

/* Atom constructors (use a global arena) */
Atom *cetta_atom_symbol(Arena *a, const char *name);
Atom *cetta_atom_int(Arena *a, int64_t val);
Atom *cetta_atom_bigint(Arena *a, const char *val);
Atom *cetta_atom_rational(Arena *a, const char *val);
Atom *cetta_atom_expr(Arena *a, Atom **elems, CettaExprLen len);

/* Result set */
void cetta_rs_add(ResultSet *rs, Atom *atom);

#endif
