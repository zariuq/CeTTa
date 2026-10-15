#ifndef CETTA_LIBRARY_SQLITE_H
#define CETTA_LIBRARY_SQLITE_H
#include "atom.h"
Atom *cetta_sqlite_dispatch(Arena *arena, Atom *head, Atom **args, uint32_t nargs);
#endif
