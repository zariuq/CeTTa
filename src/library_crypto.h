#ifndef CETTA_LIBRARY_CRYPTO_H
#define CETTA_LIBRARY_CRYPTO_H
#include "call_outcome.h"
struct CettaLibraryContext;

/* (__cetta_lib_crypto OPERATION (OPERANDS...)), the operations of lib/crypto. */
bool cetta_crypto_call(struct CettaLibraryContext *, Arena *, Atom *, Atom **,
                       uint32_t, CettaCallOutcome *);
#endif
