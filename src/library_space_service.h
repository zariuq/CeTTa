#ifndef CETTA_LIBRARY_SPACE_SERVICE_H
#define CETTA_LIBRARY_SPACE_SERVICE_H
#include "atom.h"
struct CettaLibraryContext;
struct CettaIpcSession;
/* Resolve only an owning connection holder in this exact context. The caller
 * releases the retained session; captured children do not retain a borrowed
 * library-context pointer or depend on the parent's slot remaining open. */
struct CettaIpcSession *cetta_space_service_session_acquire(
    struct CettaLibraryContext *,Atom *connection);
Atom *cetta_space_service_dispatch(struct CettaLibraryContext *context,
    Arena *arena, Atom *head, Atom **args, uint32_t nargs);
#endif
