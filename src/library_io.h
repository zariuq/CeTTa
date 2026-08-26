#ifndef CETTA_LIBRARY_IO_H
#define CETTA_LIBRARY_IO_H

#include "atom.h"

typedef struct CettaIoRuntime CettaIoRuntime;

CettaIoRuntime *cetta_io_runtime_new(void);
void cetta_io_runtime_free(CettaIoRuntime *runtime);

/*
 * Dispatch the internal operations exported by lib/io.metta.  Provider
 * callbacks only mutate the runtime's completion queue; evaluator entry
 * remains here, at an explicit poll boundary.
 */
Atom *cetta_io_dispatch(CettaIoRuntime *runtime, Arena *arena,
                        Atom *head, Atom **args, uint32_t nargs);

#endif
