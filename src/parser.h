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

/* Canonicalize reader sugar for qualified namespaces.
   This rewrites dotted qualified names such as mm2.foo, mork.bar, and
   import_pkg.moduleA into their canonical colon form before interning.
   Numeric literals, filesystem paths, and known source-file tokens such as
   foo.metta or bench.act must be left unchanged. The returned pointer is
   either the original token or an arena-owned canonical copy. */
const char *parser_canonicalize_namespace_token(Arena *a, const char *tok);

#endif /* CETTA_PARSER_H */
