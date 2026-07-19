#ifndef CETTA_ATOM_BLOB_H
#define CETTA_ATOM_BLOB_H

#include "atom.h"

typedef enum {
    ATOM_BLOB_OK = 0,
    ATOM_BLOB_NULL_ARGUMENT,
    ATOM_BLOB_BAD_MAGIC,
    ATOM_BLOB_BAD_VERSION,
    ATOM_BLOB_TRUNCATED,
    ATOM_BLOB_BAD_STRING_TABLE,
    ATOM_BLOB_BAD_ATOM_COUNT,
    ATOM_BLOB_UNSUPPORTED_GROUND_TAG,
    ATOM_BLOB_CAPACITY_OVERFLOW,
    ATOM_BLOB_TRAILING_DATA,
} AtomBlobStatus;

const char *atom_blob_status_name(AtomBlobStatus status);

/* Decode exactly one closed ground atom from CeTTa's bootstrap blob format.
   The accepted data fragment is Symbol, Int, and Expr, which is sufficient for
   BindingDecl data while deliberately rejecting variables and runtime values.
   Traversal is iterative and malformed blobs fail closed. */
Atom *atom_blob_decode_single_ground(Arena *arena,
                                     const unsigned char *blob,
                                     uint32_t blob_len,
                                     AtomBlobStatus *status_out);

#endif /* CETTA_ATOM_BLOB_H */
