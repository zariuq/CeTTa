#ifndef CETTA_GRAMMAR_CANONICAL_BNF_V1_H
#define CETTA_GRAMMAR_CANONICAL_BNF_V1_H

#include "native/grammar_canonical_term_v1.h"

#include <stdbool.h>
#include <stddef.h>

/*
 * Canonical terms of lib_bnf grammars.  The lowered grammar document, its
 * helper origins and the grammar authority are read as plain atoms: a rule
 * is authored unless an origin names it a group, repetition or option, a
 * reference to a declared lexical class is a token named by the reference,
 * and a literal is a fixed terminal.  Derivation leaves of lexical classes
 * are token terms whose text is their scalars.
 */

bool cetta_grammar_canonical_bnf_table_v1(const Atom *document, const Atom *origins,
                                          const Atom *authority,
                                          CettaGrammarCanonicalTableV1 **out, char *error,
                                          size_t error_size);

/* One canonical term per tree of a parser's CST tuple. */
bool cetta_grammar_canonical_bnf_terms_v1(const Atom *document, const Atom *origins,
                                          const Atom *authority, const Atom *trees,
                                          Arena *arena, Atom **out, char *error,
                                          size_t error_size);

bool cetta_grammar_canonical_bnf_language_v1(const Atom *document, const Atom *origins,
                                             const Atom *authority, const char *name,
                                             Arena *arena, Atom **out, char *error,
                                             size_t error_size);

/* The text of a canonical term occupying a position of the named rule. */
bool cetta_grammar_canonical_bnf_print_v1(const Atom *document, const Atom *origins,
                                          const Atom *authority, const char *rule,
                                          const Atom *term, Arena *arena, Atom **out,
                                          char *error, size_t error_size);

/* The sorts at which two productions give canonical values of one shape
 * (cetta_grammar_canonical_collisions_v1), as the expression
 * ((EBNF:Collision KIND SORT FIRST SECOND?) ...). */
bool cetta_grammar_canonical_bnf_collisions_v1(const Atom *document, const Atom *origins,
                                               const Atom *authority, Arena *arena, Atom **out,
                                               char *error, size_t error_size);

#endif /* CETTA_GRAMMAR_CANONICAL_BNF_V1_H */
