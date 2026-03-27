#ifndef CETTA_NATIVE_MODULES_H
#define CETTA_NATIVE_MODULES_H

#include "atom.h"
#include "space.h"
#include <stdint.h>

struct CettaLibraryContext;

typedef struct {
    const char *name;
    uint32_t import_bit;
    Atom *(*dispatch)(struct CettaLibraryContext *ctx, Space *space, Arena *a,
                      Atom *head, Atom **args, uint32_t nargs);
} CettaNativeBuiltinModule;

const CettaNativeBuiltinModule *cetta_native_module_lookup(const char *name);
Atom *cetta_native_module_dispatch_active(struct CettaLibraryContext *ctx,
                                          Space *space, Arena *a,
                                          Atom *head, Atom **args, uint32_t nargs,
                                          uint32_t active_mask);

#endif /* CETTA_NATIVE_MODULES_H */
