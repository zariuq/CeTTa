#ifndef CETTA_LIBRARY_HOL_H
#define CETTA_LIBRARY_HOL_H

#include "call_outcome.h"
#include "library.h"

#define CETTA_NATIVE_IMPORT_HOL (1u << 28)
#define CETTA_NATIVE_IMPORT_SET (1u << 29)

/* `import hol` and `import set` in the HE and PeTTa extended profiles:
 * Prime's native logic and its set theories, judged on an explicit theory
 * space. */
bool cetta_hol_module_call(CettaLibraryContext *context, Space *space,
    Arena *arena, Atom *head, Atom **arguments, uint32_t count, CettaCallOutcome *out);

#endif
