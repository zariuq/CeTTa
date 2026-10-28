#ifndef CETTA_LIBRARY_STORE_H
#define CETTA_LIBRARY_STORE_H
#include "atom.h"
struct CettaLibraryContext;
Atom *cetta_store_dispatch(struct CettaLibraryContext *context,Arena *arena,
    Atom *head,Atom **arguments,uint32_t count);
#endif
