#ifndef CETTA_NATIVE_MODULES_H
#define CETTA_NATIVE_MODULES_H

#include "atom.h"
#include "space.h"
#include "call_outcome.h"
#include <stdint.h>

struct CettaLibraryContext;

/* Authored and native imports share one catalog without aliasing bits. */
typedef uint64_t CettaLibraryMask;

/* Bits 0..24 belong to the shared library catalog. In particular, 24 is
 * tensor; gparse must not acquire authority when that library is imported.
 * 25..33 are existing native modules and 34 is reserved for crypto. */
#define CETTA_NATIVE_IMPORT_GPARSE (UINT64_C(1) << 35)

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
