#ifndef CESTA_OWNERSHIP_H
#define CESTA_OWNERSHIP_H

#include "cesta.h"

/* Check the preprocessed, bound and elaborated C tree, using the invocation's
   compiler environment. The declaration itself is erased from generated C. */
bool ce_ownership_check(CeArena *arena, const CeAtom *declaration, CeText text,
                        const char *identity, size_t argc, char **arguments,
                        FILE *plan, CeError *error);

#endif
