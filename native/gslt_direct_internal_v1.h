#ifndef CETTA_GSLT_DIRECT_INTERNAL_V1_H
#define CETTA_GSLT_DIRECT_INTERNAL_V1_H

/*
 * The analysed form of a GSLT composition that the direct compilers share
 * (gslt_petta_direct_v1.c): its relation rules, and the binding modes the
 * analysis derived, each with its input and success groundness and whether
 * it is functional (at most one answer).  The MeTTa targets print these as
 * equations; the C target (gslt_rulec_direct_v1.c) reads them as a rule
 * module that rulec compiles to C; the tcalc target (gslt_tcalc_direct_v1.c)
 * prints them as tcalc rules read by demand.
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
    uint8_t decreasing_self;
    uint8_t input_heads_disjoint;
    uint8_t selected;
    uint8_t admitted;
} DirectBindingModeV1;

typedef struct {
    DirectBindingModeV1 *items;
    size_t len;
    size_t cap;
} DirectBindingModesV1;

/* The selected modes of a closed entry residual as a rule module of rulec's
 * IR (gslt_rulec_direct_v1.c): its core rows (the wire) and its binding
 * table.  Every operator of the composition is a constructor the program can
 * read (its inputs may carry constructors its rules never mention).  With
 * ADMITTED_ENTRY (an entry mode of one input, or NULL), the module also has
 * the admitted entry gslt:admitted-entry:MODE of a language and the input,
 * which runs the entry on the input. */
bool cetta_gslt_render_rulec_v1(
    const DirectRulesV1 *rules, const DirectBindingModesV1 *modes,
    const DirectOperatorsV1 *operators,
    const char *const *entry_modes, size_t entry_mode_count,
    const char *admitted_entry,
    const char *source_digest,
    uint8_t **wire_out, size_t *wire_len,
    uint8_t **bindings_out, size_t *bindings_len,
    char *error, size_t error_size);

/* The C target: the wire and binding table compiled by rulec (linked in)
 * into one C translation unit.  The wire, binding table, decision DAGs and
 * census are written beside the C file, at the path
 * cetta_gslt_rulec_set_output_v1 names, as PATH.wire, PATH.bindings,
 * PATH.dag and PATH.census, for rulec_validate and readers. */
void cetta_gslt_rulec_set_output_v1(const char *c_path);
bool cetta_gslt_rulec_compile_v1(
    const uint8_t *wire, size_t wire_len,
    const uint8_t *bindings, size_t bindings_len,
    const char *source_digest,
    uint8_t **c_out, size_t *c_len,
    char *error, size_t error_size);

/* The selected modes of a closed entry residual as a tcalc program read by
 * demand (gslt_tcalc_direct_v1.c).  RULES are the composition's relation
 * rules, its target equations left out: they are another dialect's
 * realization of the capabilities, and tcalc's is its own.  Every selected
 * mode is read; none need be functional.  *rule_count_out is the number of
 * tcalc rules written. */
bool cetta_gslt_render_tcalc_v1(
    const DirectRulesV1 *rules, const DirectBindingModesV1 *modes,
    const DirectOperatorsV1 *operators,
    const char *const *entry_modes, size_t entry_mode_count,
    const char *source_digest,
    uint8_t **out, size_t *out_len, size_t *rule_count_out,
    char *error, size_t error_size);

#endif
