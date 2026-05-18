#ifndef CETTA_RHOCALC_SYNTAX_H
#define CETTA_RHOCALC_SYNTAX_H

#include <stdio.h>

#include "atom.h"
#include "lang.h"

int rhocalc_parse_text(const char *text,
                       CettaSyntaxId syntax,
                       Arena *arena,
                       Atom ***out_atoms);
int rhocalc_parse_file(const char *path,
                       CettaSyntaxId syntax,
                       Arena *arena,
                       Atom ***out_atoms);
const char *rhocalc_last_parse_error(void);
void rhocalc_print_atom_syntax(Atom *atom, CettaSyntaxId syntax, FILE *out);

#endif /* CETTA_RHOCALC_SYNTAX_H */
