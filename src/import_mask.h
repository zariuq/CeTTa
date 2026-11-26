#ifndef CETTA_IMPORT_MASK_H
#define CETTA_IMPORT_MASK_H

#include <stdint.h>

/* The libraries and native modules a session has imported, one bit each:
 * the libraries of library.c in bits 0 to 31, the native modules from bit 32
 * on (CETTA_NATIVE_IMPORT_BIT), so that no library and no module share one. */
typedef uint64_t CettaImportMask;

#define CETTA_NATIVE_IMPORT_BIT(n) (UINT64_C(1) << (32 + (n)))

#endif
