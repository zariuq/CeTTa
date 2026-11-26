#ifndef CETTA_NATIVE_MODULES_H
#define CETTA_NATIVE_MODULES_H

#include "atom.h"
#include "space.h"
#include "call_outcome.h"
#include <stdint.h>
#include "import_mask.h"

struct CettaLibraryContext;

typedef struct {
    const char *name;
    CettaImportMask import_bit;
    Atom *(*dispatch)(struct CettaLibraryContext *ctx, Space *space, Arena *a,
                      Atom *head, Atom **args, uint32_t nargs);
    const char *head_prefix; /* NULL keeps the module's ordinary dispatch policy. */
    bool (*call)(struct CettaLibraryContext *ctx, Space *space, Arena *a,
                 Atom *head, Atom **args, uint32_t nargs, CettaCallOutcome *out);
    const char *builtin_words; /* Explicitly imported evaluator controls. */
    /* Its values are CeTTa's term language, whose sequences are
     * (cons Head Tail) ... nil: in the PeTTa lane its arguments go out as
     * terms and its answers come back as PeTTa values
     * (petta_semantics_term_export, petta_semantics_term_import). */
    bool term_language;
} CettaNativeBuiltinModule;

const CettaNativeBuiltinModule *cetta_native_module_lookup(const char *name);
CettaImportMask cetta_native_module_available_mask(const struct CettaLibraryContext *ctx,
    CettaImportMask active_mask, CettaImportMask requested_mask);
bool cetta_native_module_builtin_enabled(struct CettaLibraryContext *ctx,
    CettaImportMask active_mask, const char *name);
bool cetta_native_module_call_active(struct CettaLibraryContext *ctx,
                                          Space *space, Arena *a,
                                          Atom *head, Atom **args, uint32_t nargs,
                                     CettaImportMask active_mask, CettaCallOutcome *out);

#endif /* CETTA_NATIVE_MODULES_H */
