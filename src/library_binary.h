#ifndef CETTA_LIBRARY_BINARY_H
#define CETTA_LIBRARY_BINARY_H

#include "call_outcome.h"
struct CettaLibraryContext;
struct Space;

#define CETTA_NATIVE_IMPORT_BINARY (1u << 18)

/* The exact integer test is total and stable across owned atom copies.
 * Physical profile classification is not granted this permission. Other codec
 * calls may decline to authored equations. Native availability and the exact
 * arity remain separate admission facts. */
static inline bool cetta_binary_total_observer(SymbolId head, CettaExprLen arity) {
    return arity == 1u && head == g_builtin_syms.binary_is_unsigned;
}

bool cetta_binary_module_call(struct CettaLibraryContext *context, struct Space *space,
    Arena *arena, Atom *head, Atom **arguments, uint32_t count, CettaCallOutcome *out);

#endif
