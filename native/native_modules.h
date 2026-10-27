#ifndef CETTA_NATIVE_MODULES_H
#define CETTA_NATIVE_MODULES_H

#include "atom.h"
#include "space.h"
#include "call_outcome.h"
#include <stdint.h>

struct CettaLibraryContext;

typedef struct {
    const char *name;
    uint32_t import_bit;
    Atom *(*dispatch)(struct CettaLibraryContext *ctx, Space *space, Arena *a,
                      Atom *head, Atom **args, uint32_t nargs);
    const char *head_prefix; /* NULL keeps the module's ordinary dispatch policy. */
    bool (*call)(struct CettaLibraryContext *ctx, Space *space, Arena *a,
                 Atom *head, Atom **args, uint32_t nargs, CettaCallOutcome *out);
    const char *builtin_words; /* Explicitly imported evaluator controls. */
} CettaNativeBuiltinModule;

const CettaNativeBuiltinModule *cetta_native_module_lookup(const char *name);
uint32_t cetta_native_module_available_mask(const struct CettaLibraryContext *ctx,
    uint32_t active_mask, uint32_t requested_mask);
bool cetta_native_module_builtin_enabled(struct CettaLibraryContext *ctx,
    uint32_t active_mask, const char *name);
bool cetta_native_module_call_active(struct CettaLibraryContext *ctx,
                                          Space *space, Arena *a,
                                          Atom *head, Atom **args, uint32_t nargs,
                                     uint32_t active_mask, CettaCallOutcome *out);

#endif /* CETTA_NATIVE_MODULES_H */
