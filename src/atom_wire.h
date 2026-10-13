#ifndef CETTA_ATOM_WIRE_H
#define CETTA_ATOM_WIRE_H

#include "atom.h"

/* CAV1: bounded portable syntax. Variable indices are scoped to one packet;
 * each decode creates fresh identities. No heap handles or evaluator state. */
enum { CETTA_ATOM_WIRE_BYTES = 1024 * 1024, CETTA_ATOM_WIRE_NODES = 65536,
       CETTA_ATOM_WIRE_DEPTH = 64, CETTA_ATOM_WIRE_VARS = 4096 };
typedef enum { ATOM_WIRE_OK, ATOM_WIRE_INVALID, ATOM_WIRE_LIMIT,
    ATOM_WIRE_CORRUPT, ATOM_WIRE_UNSUPPORTED, ATOM_WIRE_NOMEM } CettaAtomWireStatus;
const char *cetta_atom_wire_status_name(CettaAtomWireStatus status);
CettaAtomWireStatus cetta_atom_wire_encode(const Atom *value,
    unsigned char **bytes, size_t *length);
CettaAtomWireStatus cetta_atom_wire_decode(Arena *arena,
    const unsigned char *bytes, size_t length, Atom **value);

#endif
