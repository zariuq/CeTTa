#ifndef CETTA_PARSER_H
#define CETTA_PARSER_H

#include "atom.h"

/* Parse a .metta file into a list of top-level atoms.
   Returns number of atoms parsed, or -1 on error.
   Atoms are allocated in the provided arena. */
int parse_metta_file(const char *filename, Arena *a, Atom ***out_atoms);

/* Parse a single S-expression from a string.
   Advances *pos past the parsed expression.
   Returns NULL on end-of-input or error. */
Atom *parse_sexpr(Arena *a, const char *text, size_t *pos);

#endif /* CETTA_PARSER_H */
