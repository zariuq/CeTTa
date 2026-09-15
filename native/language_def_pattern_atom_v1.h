#ifndef CETTA_LANGUAGE_DEF_PATTERN_ATOM_V1_H
#define CETTA_LANGUAGE_DEF_PATTERN_ATOM_V1_H

#include "language_def_core_v1.h"
#include "src/atom.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Exact Atom representation of the constructor Pattern profile.
 *
 * Values use the same public constructors as a LanguageDef wire:
 *
 *   (FVar "name")
 *   (PApp "head" (LCons pattern ... LNil))
 *
 * This representation is intentionally distinct from an ordinary guest term.
 * In particular, a nullary PApp is not silently identified with either a
 * symbol Atom or a grounded String.  Typed guest-term adapters may be layered
 * above this exact boundary.
 */

typedef enum {
    CETTA_LD_PATTERN_ATOM_V1_OK = 0,
    CETTA_LD_PATTERN_ATOM_V1_BAD_ARGUMENT,
    CETTA_LD_PATTERN_ATOM_V1_MALFORMED,
    CETTA_LD_PATTERN_ATOM_V1_UNSUPPORTED_PROFILE,
    CETTA_LD_PATTERN_ATOM_V1_RESOURCE_LIMIT,
    CETTA_LD_PATTERN_ATOM_V1_ALLOCATION_FAILURE
} CettaLdPatternAtomV1Status;

/* Atomic replacement: failure leaves an existing output pattern unchanged. */
bool cetta_ld_pattern_atom_v1_decode(
    CettaLdPatternV1 *out,
    const Atom *source,
    uint32_t depth_limit,
    uint64_t work_limit,
    CettaLdPatternAtomV1Status *status,
    char *error_buf,
    size_t error_buf_size);

/* Returns NULL on failure.  The returned Atom is owned by arena. */
Atom *cetta_ld_pattern_atom_v1_encode(
    Arena *arena,
    const CettaLdPatternV1 *source,
    uint32_t depth_limit,
    uint64_t work_limit,
    CettaLdPatternAtomV1Status *status,
    char *error_buf,
    size_t error_buf_size);

const char *cetta_ld_pattern_atom_v1_status_name(
    CettaLdPatternAtomV1Status status);

#endif /* CETTA_LANGUAGE_DEF_PATTERN_ATOM_V1_H */
