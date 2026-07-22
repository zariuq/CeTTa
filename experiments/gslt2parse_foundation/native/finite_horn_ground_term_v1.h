#ifndef CETTA_GSLT2PARSE_FINITE_HORN_GROUND_TERM_V1_H
#define CETTA_GSLT2PARSE_FINITE_HORN_GROUND_TERM_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "atom.h"

/*
 * Canonical finite-Horn ground terms carried by CeTTa Atoms.
 *
 * The finite-Horn presentation carrier also admits variables and strings
 * containing U+0000.  Neither is representable by this ground Atom boundary:
 * variables are deliberately excluded, and U+0000 strings fail closed because
 * GV_STRING is NUL-terminated.  Every accepted byte stream round-trips through
 * the canonical renderer exactly.
 */

/* The returned byte allocation is NUL-terminated; out_len excludes it. */
bool fh_ground_term_v1_render(const Atom *term,
                              uint8_t **out,
                              size_t *out_len,
                              char *error,
                              size_t error_cap);

bool fh_ground_term_v1_parse(Arena *arena,
                             const uint8_t *bytes,
                             size_t len,
                             Atom **out,
                             char *error,
                             size_t error_cap);

#endif
