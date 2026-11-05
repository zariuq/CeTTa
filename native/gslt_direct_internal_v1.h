#ifndef CETTA_GSLT_DIRECT_INTERNAL_V1_H
#define CETTA_GSLT_DIRECT_INTERNAL_V1_H

/*
 * The analysed form of a GSLT composition that the direct compilers share
 * (gslt_petta_direct_v1.c): its relation rules, and the binding modes the
 * analysis derived, each with its input and success groundness and whether
 * it is functional (at most one answer).  The MeTTa targets print these as
 * equations; the C target (gslt_native_direct_v1.c) prints them as C.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gslt_composition_v1.h"

typedef struct {
    const char *name;
    size_t arity;
} DirectOperatorV1;

typedef struct {
    DirectOperatorV1 *items;
    size_t len;
    size_t cap;
} DirectOperatorsV1;

typedef struct {
    const CettaGsltRewriteV1 *source;
    size_t source_index;
    Atom *head;
    Atom *body;
} DirectRuleV1;

typedef struct {
    DirectRuleV1 *items;
    size_t len;
    size_t cap;
} DirectRulesV1;

typedef struct {
    const char *relation;
    size_t arity;
    uint8_t *input_ground;
    uint8_t *success_ground;
    uint8_t functional;
    uint8_t input_heads_disjoint;
    uint8_t selected;
    uint8_t admitted;
} DirectBindingModeV1;

typedef struct {
    DirectBindingModeV1 *items;
    size_t len;
    size_t cap;
} DirectBindingModesV1;

/* The selected modes of a closed entry residual as one C translation unit
 * (gslt_native_direct_v1.c).  Every selected mode must be functional; the
 * entry modes become external functions.  Every operator of the
 * composition is a constructor the program can read (its inputs may carry
 * constructors its rules never mention). */
bool cetta_gslt_render_native_c_v1(
    const DirectRulesV1 *rules, const DirectBindingModesV1 *modes,
    const DirectOperatorsV1 *operators,
    const char *const *entry_modes, size_t entry_mode_count,
    const char *source_digest,
    uint8_t **out, size_t *out_len,
    char *error, size_t error_size);

/* The prefix of the C target's exported names (default "gn"). */
void cetta_gslt_native_set_prefix_v1(const char *prefix);

#endif
