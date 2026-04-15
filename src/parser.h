#ifndef CETTA_PARSER_H
#define CETTA_PARSER_H

#include "term_universe.h"

/* Parse a .metta file into a list of top-level atoms.
   Returns number of atoms parsed, or -1 on error.
   Atoms are allocated in the provided arena. */
int parse_metta_file(const char *filename, Arena *a, Atom ***out_atoms);

/* Parse MeTTa source text into a list of top-level atoms.
   Returns number of atoms parsed, or -1 on error.
   Atoms are allocated in the provided arena. */
int parse_metta_text(const char *text, Arena *a, Atom ***out_atoms);

/* Parse a .metta file into a list of top-level AtomIds born directly in the
   provided term universe. Returns number of atoms parsed, or -1 on error. */
int parse_metta_file_ids(const char *filename, TermUniverse *universe,
                         AtomId **out_ids);

/* Parse MeTTa source text into a list of top-level AtomIds born directly in
   the provided term universe. Returns number of atoms parsed, or -1 on error. */
int parse_metta_text_ids(const char *text, TermUniverse *universe,
                         AtomId **out_ids);

/* Parse a single S-expression from a string.
   Advances *pos past the parsed expression.
   Returns NULL on end-of-input or error. */
Atom *parse_sexpr(Arena *a, const char *text, size_t *pos);

/* Parse a single S-expression directly into the term universe and return its
   canonical AtomId. Returns CETTA_ATOM_ID_NONE on end-of-input or error. */
AtomId parse_sexpr_to_id(TermUniverse *universe, const char *text, size_t *pos);

/* Canonicalize reader sugar for qualified namespaces.
   This rewrites dotted qualified names such as mork.foo, runtime.bar, and
   import_pkg.moduleA into their canonical colon form before interning.
   Numeric literals, filesystem paths, and known source-file tokens such as
   foo.metta or bench.act must be left unchanged. The returned pointer is
   either the original token or an arena-owned canonical copy. */
const char *parser_canonicalize_namespace_token(Arena *a, const char *tok);

#endif /* CETTA_PARSER_H */
