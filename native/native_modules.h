#ifndef CETTA_NATIVE_MODULES_H
#define CETTA_NATIVE_MODULES_H

#include "atom.h"
#include "space.h"
#include "call_outcome.h"
#include <stdint.h>

struct CettaLibraryContext;

/* A session's imports are one 64-bit mask: libraries (src/library.c) hold the
 * lower half, native modules the upper half, each module its own bit
 * (native_modules.c checks this at compile time).  A module is enabled when
 * its bit is set or a module of its name was loaded. */
typedef struct {
    const char *name;
    uint64_t import_bit;
    Atom *(*dispatch)(struct CettaLibraryContext *ctx, Space *space, Arena *a,
                      Atom *head, Atom **args, uint32_t nargs);
    const char *head_prefix; /* NULL keeps the module's ordinary dispatch policy. */
    bool (*call)(struct CettaLibraryContext *ctx, Space *space, Arena *a,
                 Atom *head, Atom **args, uint32_t nargs, CettaCallOutcome *out);
    const char *builtin_words; /* Explicitly imported evaluator controls. */
    /* A module that speaks CeTTa's term language, whose sequences are
     * (cons Head Tail) ... nil: in the PeTTa lane its arguments go out as
     * terms (petta_semantics_term_export), and the operations this names
     * answer PeTTa values (petta_semantics_term_import).  A language's own
     * terms, as a parse tree, come back with their authored constructors.
     * NULL: the module speaks PeTTa's values. */
    bool (*term_language)(const Atom *head);
} CettaNativeBuiltinModule;

const CettaNativeBuiltinModule *cetta_native_module_lookup(const char *name);
uint64_t cetta_native_module_available_mask(const struct CettaLibraryContext *ctx,
    uint64_t active_mask, uint64_t requested_mask);
bool cetta_native_module_builtin_enabled(struct CettaLibraryContext *ctx,
    uint64_t active_mask, const char *name);
bool cetta_native_module_call_active(struct CettaLibraryContext *ctx,
                                          Space *space, Arena *a,
                                          Atom *head, Atom **args, uint32_t nargs,
                                     uint64_t active_mask, CettaCallOutcome *out);

#endif /* CETTA_NATIVE_MODULES_H */
