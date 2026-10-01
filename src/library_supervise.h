#ifndef CETTA_LIBRARY_SUPERVISE_H
#define CETTA_LIBRARY_SUPERVISE_H
#include "atom.h"
#include "space.h"
struct CettaLibraryContext;
Atom *cetta_supervise_dispatch(struct CettaLibraryContext *ctx, Space *space,
    Arena *arena, Atom *head, Atom **args, uint32_t nargs);
#endif
