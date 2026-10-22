#ifndef CETTA_LIBRARY_GROUP_H
#define CETTA_LIBRARY_GROUP_H
#include "call_outcome.h"
bool cetta_group_sum_call(Arena *arena, Atom *head, Atom **args,
    uint32_t nargs, CettaCallOutcome *out);
#endif
