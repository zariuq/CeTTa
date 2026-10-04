#ifndef CETTA_LIBRARY_IO_H
#define CETTA_LIBRARY_IO_H

#include "atom.h"

typedef struct CettaIoRuntime CettaIoRuntime;

CettaIoRuntime *cetta_io_runtime_new(void);
void cetta_io_runtime_free(CettaIoRuntime *runtime);

/*
 * Dispatch the internal operations exported by lib/io.metta.  Provider
 * work runs independently of evaluation. Native workers own only C data;
 * arena atoms are built here at the explicit poll/wait boundary. This adapter
 * is ephemeral; the durable host uses the worker's recording hooks instead.
 */
Atom *cetta_io_dispatch(CettaIoRuntime *runtime, Arena *arena,
                        Atom *head, Atom **args, uint32_t nargs);

#endif
