#ifndef CETTA_LIBRARY_PROC_H
#define CETTA_LIBRARY_PROC_H

#include "atom.h"

/*
 * Dispatch the internal operation exported by lib/proc.metta: run a program
 * as a child process with an explicit argument vector, working directory and
 * environment, bounded by a timeout and an output cap. No shell is involved
 * and nothing is inherited from this process but its identity.
 */
Atom *cetta_proc_dispatch(Arena *arena, Atom *head, Atom **args, uint32_t nargs);

#endif
