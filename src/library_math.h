#ifndef CETTA_LIBRARY_MATH_H
#define CETTA_LIBRARY_MATH_H
#include "atom.h"

/* Pure numerical observations; no database or application policy. */
Atom *cetta_math_dispatch(Arena *arena, Atom *head, Atom **arguments, uint32_t count);
#endif
