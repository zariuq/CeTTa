#ifndef CETTA_LIBRARY_DURABLE_H
#define CETTA_LIBRARY_DURABLE_H
#include "atom.h"

typedef struct CettaDurableRuntime CettaDurableRuntime;
CettaDurableRuntime *cetta_durable_runtime_new(void);
void cetta_durable_runtime_free(CettaDurableRuntime *runtime);
Atom *cetta_durable_dispatch(CettaDurableRuntime *runtime, Arena *arena,
    Atom *head, Atom **args, uint32_t nargs);
/* Trusted host/CLI only: never invoke while exploring evaluator branches. */
Atom *cetta_durable_admin_dispatch(CettaDurableRuntime *runtime, Arena *arena,
    Atom *head, Atom **args, uint32_t nargs);
#endif
