#ifndef CESTA_OWNERSHIP_SIGNATURE_H
#define CESTA_OWNERSHIP_SIGNATURE_H

#include "cesta.h"

/* A nonnull result can implement the same nullable result contract. Argument
   requirements, origins, extents, access rights, families and record effects
   remain identical. C prototype and callback/layout checks are separate. */
bool ce_ownership_signature_refines(const CeAtom *provided, const CeAtom *expected);

#endif
