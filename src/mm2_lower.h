#ifndef CETTA_MM2_LOWER_H
#define CETTA_MM2_LOWER_H

#include "atom.h"

/* Lower raw MM2 surface forms into inert internal IR before ordinary HE
   evaluation sees them. The lowering happens in-place on the parsed top-level
   atom array; returned atoms live in the provided arena. */
void cetta_mm2_lower_atoms(Arena *a, Atom **atoms, int n);

/* MM2 program rules are the atoms that belong in the executable program lane. */
bool cetta_mm2_atom_is_exec_rule(Atom *atom);

/* MM2 files are raw surface syntax, not HE scripts. */
bool cetta_mm2_atoms_have_top_level_eval(Atom **atoms, int n);

/* Raise one raw or lowered MM2 atom back to executable MM2 surface syntax.
   This is the inverse bridge used by the MORK runtime seam: lowered IR and
   already-surface MM2 terms both round-trip through one honest serializer. */
Atom *cetta_mm2_raise_atom(Arena *a, Atom *atom);

/* Render one raw or lowered MM2 atom as surface MM2 S-expression text. */
char *cetta_mm2_atom_to_surface_string(Arena *a, Atom *atom);

#endif /* CETTA_MM2_LOWER_H */
