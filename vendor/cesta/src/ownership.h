#ifndef CESTA_OWNERSHIP_H
#define CESTA_OWNERSHIP_H

#include "cesta.h"

/* What a passing check tells the build. interfaces names the contract files
   kept beside headers (NAME.h.ownership) that took part, for the caller's
   dependency record. contracts is the unit's link record: one
   (unit ...) item naming the functions it defines and those its checked
   bodies call in other units, each with a contract that does not depend on
   the unit, so that separately checked units can be matched at link time. */
typedef struct {
    const char **interfaces; size_t ninterfaces;
    CeText contracts;
    /* Asked for by the caller: the contracts this unit gives what HEADER
       declares, written as HEADER.ownership would hold them. */
    const char *interface_of;
    CeText interface;
} CeOwnershipResult;

/* Check the preprocessed, bound and elaborated C tree, using the invocation's
   compiler environment. The declaration itself is erased from generated C. */
bool ce_ownership_check(CeArena *arena, const CeAtom *declaration, CeText text,
                        const char *identity, size_t argc, char **arguments,
                        FILE *plan, CeOwnershipResult *result, CeError *error);

#endif
