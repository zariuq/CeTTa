#ifndef CETTA_GSLT_RHOMETTA_DIRECT_V1_H
#define CETTA_GSLT_RHOMETTA_DIRECT_V1_H

#include "atom.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CETTA_GSLT_RHOMETTA_TARGET_PACKAGE_DIGEST_V1 \
    "ba510c06a983e9c921325da27a8abaee41d023a98f76aec0506e0641b04cad29"

/* Lower an authored first-order GSLT relation composition to rho processes.
 * Communication and continuations execute in CeTTa's rho machine; the named
 * Rhometta boundary supplies only inert values and deferred unification at a
 * COMM. */
bool cetta_gslt_rhometta_direct_v1(
    Atom *const *presentations,
    size_t presentation_count,
    const char target_package_digest[65],
    uint8_t **program_out,
    size_t *program_len_out,
    size_t *rule_count_out,
    size_t *relation_count_out,
    char source_digest_out[65],
    char *error,
    size_t error_size);

/* As above, but start from named authored rewrite entries and retain their
 * complete body-relation dependency closure.  An empty entry list selects the
 * whole composition. */
bool cetta_gslt_rhometta_direct_selected_v1(
    Atom *const *presentations,
    size_t presentation_count,
    const char target_package_digest[65],
    const char *const *entry_rule_names,
    size_t entry_rule_count,
    uint8_t **program_out,
    size_t *program_len_out,
    size_t *rule_count_out,
    size_t *relation_count_out,
    char source_digest_out[65],
    char *error,
    size_t error_size);

#endif
