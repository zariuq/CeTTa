#ifndef CETTA_GSLT_PETTA_DIRECT_V1_H
#define CETTA_GSLT_PETTA_DIRECT_V1_H

#include "atom.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Lower the relation-rule fragment of an ordered GSLT composition directly
 * to ordinary PeTTa equations.  The output contains no reflected rule data
 * and requires no source-language interpreter. */
bool cetta_gslt_petta_direct_v1(
    Atom *const *presentations,
    size_t presentation_count,
    uint8_t **program_out,
    size_t *program_len_out,
    size_t *rule_count_out,
    char source_digest_out[65],
    char *error,
    size_t error_size);

/* Request externally callable binding modes in addition to modes reached from
 * rule bodies.  Each entry is RELATION:BITS, with one 0/1 bit per argument;
 * selected mode bodies remain derived from the authored rules. */
bool cetta_gslt_petta_direct_selected_v1(
    Atom *const *presentations,
    size_t presentation_count,
    const char *const *entry_modes,
    size_t entry_mode_count,
    uint8_t **program_out,
    size_t *program_len_out,
    size_t *rule_count_out,
    char source_digest_out[65],
    char *error,
    size_t error_size);

/* Specialize transitively from the requested entry modes and emit only those
 * mode projections.  Unlike selected_v1, this is a closed residual: no
 * unspecialized relation rules are retained as runtime fallbacks. */
bool cetta_gslt_petta_direct_closed_v1(
    Atom *const *presentations,
    size_t presentation_count,
    const char *const *entry_modes,
    size_t entry_mode_count,
    uint8_t **program_out,
    size_t *program_len_out,
    size_t *rule_count_out,
    char source_digest_out[65],
    char *error,
    size_t error_size);

#endif
