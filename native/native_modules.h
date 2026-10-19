#ifndef CETTA_NATIVE_MODULES_H
#define CETTA_NATIVE_MODULES_H

#include "atom.h"
#include "space.h"
#include "call_outcome.h"
#include <stdint.h>

struct CettaLibraryContext;

/* Authored and native imports share one catalog without aliasing bits. */
typedef uint64_t CettaLibraryMask;

typedef struct {
    const char *name;
    CettaLibraryMask import_bit;
    Atom *(*dispatch)(struct CettaLibraryContext *ctx, Space *space, Arena *a,
                      Atom *head, Atom **args, uint32_t nargs);
    const char *head_prefix; /* NULL keeps the module's ordinary dispatch policy. */
    bool (*call)(struct CettaLibraryContext *ctx, Space *space, Arena *a,
                 Atom *head, Atom **args, uint32_t nargs, CettaCallOutcome *out);
} CettaNativeBuiltinModule;

const CettaNativeBuiltinModule *cetta_native_module_lookup(const char *name);
bool cetta_native_module_call_active(struct CettaLibraryContext *ctx,
                                          Space *space, Arena *a,
                                          Atom *head, Atom **args, uint32_t nargs,
                                     CettaLibraryMask active_mask, CettaCallOutcome *out);

#endif /* CETTA_NATIVE_MODULES_H */
