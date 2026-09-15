#ifndef CETTA_GSLT_PETTA_DIRECT_V1_H
#define CETTA_GSLT_PETTA_DIRECT_V1_H

#include "atom.h"
#include "language_def_core_v1.h"

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
 * selected mode bodies remain derived from the authored rules. Each requested
 * mode has a gslt:entry:RELATION:BITS entry preserving its complete answer
 * stream regardless of whether functional specialization is selected. */
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

/* Consume source-inferred integer completion types, authenticated against the
 * complete ordered composition. The present consumer specializes literal
 * integer calls only; it does not infer integer types from groundness. Like
 * the source operational model, its authority is a fixed ordered program:
 * adding matching runtime equations or replacing its primitives is outside
 * this contract. Variable-domain transport from BNF admission is separate. */
bool cetta_gslt_petta_direct_native_types_v1(
    Atom *const *presentations, size_t presentation_count,
    const Atom *native_type_packet,
    const char *const *entry_modes, size_t entry_mode_count,
    bool closed_entry_residual,
    uint8_t **program_out, size_t *program_len_out, size_t *rule_count_out,
    char source_digest_out[65], char *error, size_t error_size);

/* Keep the ordinary closed entries and additionally compile a separate
 * domain-specialized family. RELATION:TYPE selects a declared closed entry
 * with one input. Its checked wrapper takes a live language handle, verifies
 * the exact wire identity and admits the input before using that family;
 * failed checks use the ordinary entry. No ground-mode bit acquires a new
 * typing meaning. The caller must supply the digest of the wire decoded into
 * admission_language (the CLI establishes this pairing). As with the ordinary
 * compiler, execution assumes the fixed program and its primitive meanings.
 * Generated worker names are not an access-control boundary: only the checked
 * entry establishes admission. Whole compiler correctness remains separate
 * from source-type inference and the selected guard-partition laws. */
bool cetta_gslt_petta_direct_admitted_v1(
    Atom *const *presentations, size_t presentation_count,
    const Atom *native_type_packet,
    const char *const *entry_modes, size_t entry_mode_count,
    const CettaLanguageDefCoreV1 *admission_language,
    const char admission_wire_digest[65], const char *admission_entry,
    uint8_t **program_out, size_t *program_len_out, size_t *rule_count_out,
    char source_digest_out[65], char *error, size_t error_size);

#endif
