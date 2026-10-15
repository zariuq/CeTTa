#ifndef CETTA_LIBRARY_SPACE_SERVICE_H
#define CETTA_LIBRARY_SPACE_SERVICE_H
#include "atom.h"
Atom *cetta_space_service_dispatch(Arena *arena, Atom *head, Atom **args, uint32_t nargs);
#endif
