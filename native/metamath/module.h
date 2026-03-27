#ifndef CETTA_NATIVE_METAMATH_MODULE_H
#define CETTA_NATIVE_METAMATH_MODULE_H

#include "atom.h"
#include "space.h"
#include <stdint.h>

struct CettaLibraryContext;

#define CETTA_METAMATH_NATIVE_IMPORT_BIT (1u << 16)

Atom *cetta_metamath_native_dispatch(struct CettaLibraryContext *ctx, Space *space,
                                     Arena *a, Atom *head, Atom **args,
                                     uint32_t nargs);

#endif /* CETTA_NATIVE_METAMATH_MODULE_H */
