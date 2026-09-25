#ifndef CETTA_GSLT2PARSE_GROUND_ATOM_WIRE_V1_H
#define CETTA_GSLT2PARSE_GROUND_ATOM_WIRE_V1_H

#include "atom.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Decode the reader-independent constructor wire emitted by the Lean model.
 * The result is data in out_arena; no evaluator or host call is involved.
 * depth_limit bounds adversarial nesting before any recursive descent. */
bool pp_ground_atom_wire_v1_decode(
    const Atom *wire,
    Arena *out_arena,
    uint32_t depth_limit,
    Atom **out,
    char *error_buf,
    size_t error_buf_size);

#endif
